/*
 * Host UI - ideaspark ESP32 1.9" ST7789 (170x320, portret)
 * Wyświetla temperaturę i ciśnienie z 2 sensorów (góra/dół)
 * + pływające okienko z godziną/datą na środku.
 *
 * WAŻNE: kolejność inicjalizacji - WiFi/BLE PRZED tft.init(),
 * bo GPIO2 (LCD_DC) to pin bootstrapping ESP32. Na razie w tym
 * szkielecie samego WiFi/BLE jeszcze nie ma, więc kolejność
 * nieistotna - dopilnuj tego przy scalaniu z resztą projektu.
 */
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <time.h>
#include "ble_scanner.h"
#include "wifi_manager.h"

#define LCD_MOSI 23
#define LCD_SCLK 18
#define LCD_CS   15
#define LCD_DC   2
#define LCD_RST  4
#define LCD_BLK  32   // podświetlenie

Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);

// ---- Wymiary ekranu (portret) ----
const int16_t SCREEN_W = 170;
const int16_t SCREEN_H = 320;
const int16_t HALF_H   = SCREEN_H / 2; // 160

// ---- Kolory ----
#define COL_BG       ST77XX_WHITE
#define COL_TEXT     ST77XX_BLACK
#define COL_DIVIDER  0xC618   // jasnoszary
#define COL_BOX      0x1B5D   // granatowo-niebieski (jak na screenie)
#define COL_BOXTEXT  ST77XX_WHITE
#define COL_TEMP     ST77XX_MAGENTA    // magenta/roz - temperatura

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t COL_GREEN = rgb565(60, 160, 60); // kolor "°C" / "hPa" jak na screenie

// ---- Dane sensorów (na razie testowe - podepniesz z BLE) ----
// struct SensorReading {
//   bool  valid = false;
//   float temperature = 0;
//   float pressure = 0;
//   unsigned long lastUpdate = 0;
// };

SensorReading sensor[2];

struct SensorNameMap {
  const char* mac;
  const char* name;
};

static const SensorNameMap knownNames[MAX_SENSORS] = {
  {"e1:62:7c:ae:71:40", "Gora"},
  {"fe:73:39:8c:2c:cd", "Dwor"},
  {"db:cb:7d:51:9a:c5", "Kuchnia"},
  {"d8:55:70:b6:26:5e", "Dol"}
  // dodaj kolejne w miare potrzeb (max 10 wynika z MAX_SENSORS w ble_scanner.h)
};


// ---- Zegar (aktualizowany z NTP) ----
int clockHour = 12, clockMin = 0;
int dateDay = 1, dateMonth = 1, dateYear = 0;

// ---- NTP / czas systemowy ----

// Edinburgh/London: GMT0BST,M3.5.0/1,M10.5.0
// (DST: ostatnia niedziela marca godz. 1:00 -> ostatnia niedziela pazdziernika)
static const char* TZ_LONDON = "GMT0BST,M3.5.0/1,M10.5.0";

void syncTimeFromNTP() {
  if (!wifiMgrIsConnected()) {
    Serial.println("[NTP] Brak WiFi - pomijam sync");
    return;
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", TZ_LONDON, 1);
  tzset();
  Serial.print("[NTP] Czekam na sync");
  int timeout = 20;
  time_t now = time(nullptr);
  while (now < 86400 && timeout-- > 0) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();
  if (now > 86400) {
    struct tm* ti = localtime(&now);
    updateClock(ti->tm_hour, ti->tm_min, ti->tm_mday,
                ti->tm_mon + 1, (ti->tm_year + 1900) % 100);
    Serial.printf("[NTP] OK %02d:%02d %02d/%02d/%02d\n",
                  ti->tm_hour, ti->tm_min, ti->tm_mday,
                  ti->tm_mon + 1, (ti->tm_year + 1900) % 100);
  } else {
    Serial.println("[NTP] timeout");
  }
}



void setup() {
  Serial.begin(115200);

  pinMode(LCD_BLK, OUTPUT);
  digitalWrite(LCD_BLK, HIGH);

  // BLE + WiFi PRZED tft.init() - GPIO2 jest zarazem LCD_DC i pin bootstrapping ESP32
  bleScanInit();
  wifiMgrInit();

  // Czekaj chwile na polaczenie WiFi przed NTP
  unsigned long wifiWait = millis();
  while (!wifiMgrIsConnected() && millis() - wifiWait < 5000) {
    delay(200);
  }
//  syncTimeFromNTP();

  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(2);
  tft.fillScreen(COL_BG);

  drawDivider();
  drawSensorPanel(0);
  drawSensorPanel(1);
  drawClockBox();
}

void loop() {
  bleScanLoop();
  wifiMgrLoop();

  // Aktualizacja danych sensorow (gdy BLE dostarczyl nowy odczyt)
  static unsigned long lastSensorUpdate[2] = {0, 0};
  for (uint8_t i = 0; i < 2; i++) {
    SensorReading r;
    for (uint8_t t = 0; t < MAX_SENSORS; t++)
    {
      getSensorReading(t, r);
      if (r.lastUpdate == 0)
        continue;
      //Serial.printf("Update odczytu %s\n", r.mac_address.c_str());
      if (r.mac_address == knownNames[i].mac)
      {
        if (r.lastUpdate != lastSensorUpdate[i])
        {
          Serial.printf("Update odczytu %s\n", r.mac_address.c_str());

          lastSensorUpdate[i] = r.lastUpdate;
          updateSensor(i, r.temperature, r.pressure);
          break;
        }
      }
    }
    // if (getSensorReading(i, r) && r.lastUpdate != lastSensorUpdate[i]) {
    //   lastSensorUpdate[i] = r.lastUpdate;
    //   updateSensor(i, r.temperature, r.pressure);
    // }
  }

  // Aktualizacja zegara co 30s z czasu systemowego (po NTP sync)
  static bool     ntpSynced        = false;
  static unsigned long lastClockMs = 0;
  if (!ntpSynced && wifiMgrIsConnected()) {
    syncTimeFromNTP();
    ntpSynced = (time(nullptr) > 86400);
  }
  if (millis() - lastClockMs >= 30000UL) {
    lastClockMs = millis();
    time_t now = time(nullptr);
    if (now > 86400) {
      struct tm* ti = localtime(&now);
      updateClock(ti->tm_hour, ti->tm_min, ti->tm_mday,
                  ti->tm_mon + 1, (ti->tm_year + 1900) % 100);
    }
  }

  delay(100);
}

// ============================================================
// Rysowanie stałych elementów układu
// ============================================================

void drawDivider() {
  tft.drawFastHLine(0, HALF_H, SCREEN_W, COL_DIVIDER);
}

// Rysuje symbol stopnia (mały okrąg) + literę C, bo domyślna czcionka
// GFX nie ma niezawodnie znaku '°' w print().
void drawDegreeC(int16_t x, int16_t y, uint8_t textSize, uint16_t color) {
  uint8_t r = 2 * textSize; // promień kółka skalowany z rozmiarem tekstu
  tft.drawCircle(x + r, y + r, r, color);
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  tft.setCursor(x + 2 * r + 4, y);
  tft.print("C");
}

// Rysuje panel jednego sensora (0 = gorny, 1 = dolny)
void drawSensorPanel(uint8_t index) {
  if (index > 1) return;

  int16_t yBase = (index == 0) ? 15 : HALF_H; // gorna albo dolna polowa
  int16_t padX  = 20;

  // wyczysc polowe panelu (przydatne przy pozniejszym odswiezaniu)
  tft.fillRect(0, yBase, SCREEN_W, HALF_H, COL_BG);
  drawDivider();

/*  if (!sensor[index].valid) {
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(padX, yBase + HALF_H / 2 - 8);
    tft.print("brak danych");
    return;
  }
*/
  // --- Temperatura (duza liczba) ---
  int16_t numY = yBase + 36;
  tft.setTextSize(5);
  tft.setTextColor(COL_TEMP);
  tft.setCursor(padX, numY);
  tft.print((int)round(sensor[index].temperature));

  // --- "°C" obok, mniejsze, zielone ---
  int16_t unitX = padX + (String((int)round(sensor[index].temperature)).length() * 30) + 6;
  drawDegreeC(unitX, numY, 2, COL_GREEN);
  tft.setTextColor(COL_TEXT);
  // --- Cisnienie ---
  int16_t pressY = numY + 50;
  tft.setTextSize(3);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(padX, pressY);
  tft.print((int)round(sensor[index].pressure));

  // --- "hPa" obok, zielone ---
  int16_t hpaX = padX + (String((int)round(sensor[index].pressure)).length() * 18) + 8;
  tft.setTextSize(2);
  tft.setTextColor(COL_GREEN);
  tft.setCursor(hpaX, pressY + 5);
  tft.print("hPa");
}

// Plywajace okienko zegara na srodku, nachodzace na linie podzialu
void drawClockBox() {
  int16_t boxW = 150;
  int16_t boxH = 42;
  int16_t boxX = (SCREEN_W - boxW) / 2;
  int16_t boxY = HALF_H - boxH / 2;

  tft.fillRoundRect(boxX, boxY, boxW, boxH, 8, COL_BOX);

  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", clockHour, clockMin);

  char dateStr[11];
  snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%02d", dateDay, dateMonth, dateYear);

  int16_t x1, y1;
  uint16_t w, h;

  tft.setTextSize(2);
  tft.setTextColor(COL_BOXTEXT);
  tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(boxX + (boxW - w) / 2, boxY + 6);
  tft.print(timeStr);

  tft.setTextSize(1);
  tft.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(boxX + (boxW - w) / 2, boxY + 27);
  tft.print(dateStr);
}

// ============================================================
// Funkcje pomocnicze do pozniejszej integracji z BLE / NTP
// ============================================================

// Wywolaj po odebraniu nowego odczytu z sensora przez BLE
void updateSensor(uint8_t index, float temperature, float pressure) {
  if (index > 1) return;
//  sensor[index].valid = true;
  sensor[index].temperature = temperature;
  sensor[index].pressure = pressure;
  sensor[index].lastUpdate = millis();
  drawSensorPanel(index);
  drawClockBox(); // box zegara nachodzi na panel, wiec odrysuj go ponownie na wierzchu
}

// Wywolaj po synchronizacji NTP / co minute z RTC
void updateClock(int hour, int minute, int day, int month, int year) {
  clockHour = hour;
  clockMin = minute;
  dateDay = day;
  dateMonth = month;
  dateYear = year;
  drawClockBox();
}
