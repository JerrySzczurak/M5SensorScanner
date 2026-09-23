/*
 * Host UI - M5Stack Dial (K130-V11, ESP32-S3, 1.28" okragly GC9A01)
 * Wyswietla 1 sensor na raz (nazwa, temperatura, cisnienie, wilgotnosc)
 * + pasek czasu/daty na dole. Przelaczanie miedzy sensorami - pokretlo.
 *
 * Wymagane biblioteki (Arduino Library Manager):
 *   - M5Dial (oficjalna, m5stack/M5Dial)
 *   - M5Unified, M5GFX (zainstaluja sie jako zaleznosci M5Dial)
 *   - NimBLE-Arduino (h2zero)
 *
 * Wymagane pliki w tym samym folderze projektu:
 *   - ble_scanner.h / ble_scanner.cpp  (MAX_SENSORS=10)
 */

// Minimalizuj WiFi stack - wyłącz IPv6, mDNS, zbędne funkcje
#define CONFIG_LWIP_IPV6 0
#define CONFIG_LWIP_IPV6_AUTOCONFIG 0

#include "M5Dial.h"
#include "ble_scanner.h"
#include "m5dial_text_input.h"
#include "wifi_manager.h"

#define DEBUG_ENABLED 1
// ============================================================
// Mapowanie MAC -> nazwa sensora (na razie hardcoded).
// UZUPELNIJ realnymi adresami MAC swoich sensorow ISP1907-LL.
// Format MAC z NimBLE: male litery, dwukropki, np. "aa:bb:cc:dd:ee:01"
// ============================================================
struct SensorNameMap {
  const char* mac;
  const char* name;
};

static const SensorNameMap knownNames[MAX_SENSORS] = {
  {"e1:62:7c:ae:71:40", "Gora"},
  {"db:cb:7d:51:9a:c5", "Kuchnia"},
  {"d8:55:70:b6:26:5e", "Dol"},
  {"fe:73:39:8c:2c:cd", "Dwor"}
  // dodaj kolejne w miare potrzeb (max 10 wynika z MAX_SENSORS w ble_scanner.h)
};
static const int knownNamesCount = sizeof(knownNames) / sizeof(knownNames[0]);

M5Canvas canvas(&M5Dial.Display);
// ============================================================
// Nazwy nadane przez uzytkownika (nadpisuja lookupSensorName)
// ============================================================
// static char customSensorNames[MAX_SENSORS][TEXT_INPUT_MAX_LEN + 1] = {};

static String lookupSensorName(const std::string& mac, int slot = -1) {
  // if (slot >= 0 && slot < MAX_SENSORS && customSensorNames[slot][0] != '\0') {
  //   return String(customSensorNames[slot]);
  // }
  for (int i = 0; i < knownNamesCount; i++) {
    if (mac == knownNames[i].mac) {
      return String(knownNames[i].name);
    }
  }
  // MAC nieznany - pokaz ostatnie 2 bajty adresu jako identyfikator
  String macStr(mac.c_str());
  int len = macStr.length();
  return "Sensor " + macStr.substring(len >= 5 ? len - 5 : 0);
}

// ============================================================
// Kolory (RGB565)
// ============================================================
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t COL_BG       = rgb565(224, 236, 249);  // jasny blekit tla
const uint16_t COL_NAME     = rgb565(30, 90, 160);     // niebieski naglowek (nazwa sensora)
const uint16_t COL_TEMP     = rgb565(200, 20, 150);    // magenta/roz - temperatura
const uint16_t COL_TEXT     = rgb565(20, 20, 20);       // czarny - cisnienie/wilgotnosc
const uint16_t COL_BAND     = rgb565(20, 75, 105);      // granatowo-teal - dolny pasek
const uint16_t COL_BANDTEXT = rgb565(255, 255, 255);    // bialy tekst na pasku

//Kolory baterii
const uint16_t COL_BATTERY_OK = rgb565(0, 255, 0);
const uint16_t COL_BATTERY_WARN = rgb565(255, 255, 0);
const uint16_t COL_BATTERY_LOW = rgb565(255, 0, 0);


// ============================================================
// Stan przelaczania sensorow enkoderem
// ============================================================
static long encoderOldPosition = 0;
static int  displayedSlot      = -1; // ktory slot sensors[] aktualnie pokazujemy
static unsigned long lastRedraw = 0;

// ============================================================
// Maszyna stanow UI
// ============================================================
enum UIState {
  UI_SENSOR_VIEW, UI_MENU, UI_KEYBOARD_NAME
};
static UIState uiState = UI_SENSOR_VIEW;

static const char* MENU_ITEMS[]   = { "Change name" };
static const int   MENU_ITEM_COUNT = 1;
static int         menuSelectedItem = 0;

// Zwraca liste indeksow slotow, ktore maja realne dane (nie puste MAC)
// ============================================================
static int getActiveSlots(int* outIndices, int maxOut) {
  int count = 0;
  for (int i = 0; i < MAX_SENSORS && count < maxOut; i++) {
    SensorReading r;
    if (getSensorReading((uint8_t)i, r)) {
      outIndices[count++] = i;
    }
  }
  return count;
}

// ============================================================
// Rysuje symbol stopnia (kolko) + litere C
// ============================================================
static void drawDegreeC(M5Canvas &c, int16_t x, int16_t y, uint16_t color) {
  c.drawCircle(x + 5, y + 5, 5, color);
  c.setTextDatum(top_left);
  c.setTextColor(color);
  c.setTextSize(2);
  c.drawString("C", x + 14, y);
}

// ============================================================
// Rysuje caly ekran dla danego slotu sensora
// ============================================================
static void drawSensorScreen(int slot) {
  int w = M5Dial.Display.width();
  int h = M5Dial.Display.height();
  int cx = w / 2;

  canvas.fillScreen(COL_BG);

  if (slot < 0) {
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_TEXT);
    canvas.setTextSize(2);
    canvas.drawString("Searching...", cx, h / 2);
    canvas.pushSprite(0, 0);
    return;
  }

  SensorReading r;
  getSensorReading((uint8_t)slot, r);  // czytamy bez sprawdzania wyniku

  String name = lookupSensorName(r.mac_address, slot);
  auto dt = M5Dial.Rtc.getDateTime();
  int tempX = cx + 50;

    unsigned long timeSinceUpdate = millis() - r.lastUpdate;
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_NAME);
    canvas.setTextSize(2);
    canvas.drawString(name, cx, h * 0.16);
  if (timeSinceUpdate > 180000) {  // > 3 minuty bez aktualizacji
    // sensor się nie odzywa - pokaż ostrzeżenie
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_TEMP);
    canvas.setTextSize(2);
    canvas.drawString("Sensor offline", cx, h * 0.40);
  }
  else
  {
    char tempStr[8];
    snprintf(tempStr, sizeof(tempStr), "%.1f", r.temperature);
    canvas.setTextColor(COL_TEMP);
    canvas.setTextSize(5);
    canvas.setTextDatum(middle_right);
    
    if (r.temperature >= 10) {
      tempX += 10;
    }
    int tempY = h * 0.38;
    canvas.drawString(tempStr, tempX, tempY);
    drawDegreeC(canvas, tempX + 6, tempY - 20, COL_TEMP);

    char pressStr[16];
    snprintf(pressStr, sizeof(pressStr), "%.0f hPa", r.pressure);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_TEXT);
    canvas.setTextSize(3);
    canvas.drawString(pressStr, cx, h * 0.56);

    tempX = cx - 50;
    char humStr[8];
    snprintf(humStr, sizeof(humStr), "%.0f%%", r.humidity);
    canvas.setTextSize(2);
    canvas.drawString(humStr, tempX, h * 0.68);
  }
  tempX = cx + 50;
  char batteryStr[8];
  snprintf(batteryStr, sizeof(batteryStr), "%d%%", r.battery_level);
  canvas.setTextColor(COL_TEXT);//getBatteryColor(r.battery_level));
  canvas.setTextSize(2);
  canvas.drawString(batteryStr, tempX, h * 0.68);

  //Rysowanie ikonki baterii. X = 20, Y = (Height/2)-20, szerokość 20, wyskokość 40.
  drawBatteryIcon(canvas, 20, (h / 2) - 50, 20, 40, r.battery_level);


  int bandH = h * 0.24;
  int bandY = h - bandH;
  canvas.fillRect(0, bandY, w, bandH, COL_BAND);

  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", dt.time.hours, dt.time.minutes);
  char dateStr[11];
  snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%02d",
           dt.date.date, dt.date.month, dt.date.year-2000);

  canvas.setTextDatum(middle_center);
  canvas.setTextColor(COL_BANDTEXT);
  canvas.setTextSize(3);
  canvas.drawString(timeStr, cx, bandY + bandH * 0.33);

  canvas.setTextSize(2);
  canvas.drawString(dateStr, cx, bandY + bandH * 0.70);

  canvas.pushSprite(0, 0);   // <-- JEDEN transfer całego gotowego kadru na ekran
}

// ============================================================
static void drawMenu() {
  int w  = M5Dial.Display.width();
  int h  = M5Dial.Display.height();
  int cx = w / 2;

  M5Dial.Display.startWrite();
  M5Dial.Display.fillScreen(rgb565(20, 20, 40));

  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setTextColor(rgb565(180, 180, 180));
  M5Dial.Display.setTextSize(1);
  M5Dial.Display.drawString("OPTIONS", cx, (int)(h * 0.18));

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    int y = (int)(h * 0.38) + i * 46;
    if (i == menuSelectedItem) {
      M5Dial.Display.fillRoundRect(cx - 88, y - 14, 176, 30, 6, rgb565(40, 110, 200));
      M5Dial.Display.setTextColor(rgb565(255, 255, 255));
    } else {
      M5Dial.Display.setTextColor(rgb565(160, 160, 160));
    }
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextSize(2);
    M5Dial.Display.drawString(MENU_ITEMS[i], cx, y);
  }

  M5Dial.Display.endWrite();
}

// ============================================================
// Funkcje pomocnicze dla baterii
// ============================================================

//funkcja lerp (linear interpolation) dla 8-bitowych wartosci
static inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(a + (static_cast<int16_t>(b) - a) * t);
}

// Zwraca kolor baterii w zaleznosci od procentowego poziomu naladowania
uint16_t getBatteryColor(uint8_t percent) {
    percent = (percent > 100) ? 100 : percent;

    uint8_t r, g, b = 0;

    if (percent >= 50) {
        // segment WARN (50%) -> OK (100%): żółty -> zielony
        float t = (percent - 50) / 50.0f;
        r = lerp8(255, 0, t);
        g = 255;
    } else {
        // segment LOW (0%) -> WARN (50%): czerwony -> żółty
        float t = percent / 50.0f;
        r = 255;
        g = lerp8(0, 255, t);
    }

    return rgb565(r, g, b);
}

//Funkcja tworzy canvas który wyświetla iknokę baterii z odpowienim kolorem w zależności od poziomu naładowania. 
//Ikonka baterii jest pionowa i ma też cyfrową reprezentację procentowego poziomu naładowania.
void drawBatteryIcon(M5Canvas& canvas, int x, int y, int width, int height, uint8_t batteryLevel) {
  uint16_t color = getBatteryColor(batteryLevel);
  
  // Rysowanie obramowania baterii
  canvas.drawRect(x, y, width, height, color);

  // Rysowanie "napięcia" baterii
  int fillHeight = (height - 4) * batteryLevel / 100;
  canvas.fillRect(x + 2, y + height - 2 - fillHeight, width - 4, fillHeight, color);

  // Rysowanie procentowego poziomu naładowania
  canvas.setTextColor(COL_TEXT);
  canvas.setTextDatum(middle_center);
  canvas.setTextSize(1);
  canvas.drawString(String(batteryLevel) + "%", x + width / 2, y + height / 2);
}

// ============================================================
// Oblicza czy aktualnie obowiązuje czas letni (DST) dla Edynburga
// Reguła: ostatnia niedziela marca - ostatnia niedziela października
// ============================================================
static bool isDSTActive(int year, int month, int day) {
  // Edynburg: BST od ostatniej niedzieli marca do ostatniej niedzieli października
  if (month < 3 || month > 10) return false;  // zimowy czas poza marcem-październikiem
  if (month > 3 && month < 10) return true;   // letni czas marzec-październik (bez końców)
  
  // Znaleź ostatnią niedzielę miesiąca
  auto getLastSunday = [](int y, int m) -> int {
    // Ostatni dzień miesiąca
    int daysInMonth[] = {31, (y%4==0 && (y%100!=0 || y%400==0)) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int lastDay = daysInMonth[m - 1];
    
    // Oblicz dzień tygodnia ostatniego dnia (0=niedziela, 6=sobota)
    // Zeller's congruence uproszczona
    int q = lastDay, m_adj = m, y_adj = y;
    if (m < 3) { m_adj += 12; y_adj--; }
    int K = y_adj % 100;
    int J = y_adj / 100;
    int h = (q + (13*(m_adj+1))/5 + K + K/4 + J/4 - 2*J) % 7;
    int dayOfWeek = (h + 6) % 7;  // 0=niedziela
    
    // Wróć do ostatniej niedzieli
    return lastDay - dayOfWeek;
  };
  
  if (month == 3) return day >= getLastSunday(year, 3);   // od ostatniej niedzieli marca
  if (month == 10) return day < getLastSunday(year, 10);  // do ostatniej niedzieli października
  
  return true;  // pomiędzy marcem a październikiem
}

void syncTimeFromNTP() {
  // Sprawdź czy RTC ma już ustawiony rozsądny czas (rok >= 2024)
  // auto dt = M5Dial.Rtc.getDateTime();
  // if (dt.date.year >= 2024) {
  //   Serial.printf("RTC już ustawiony: %04d-%02d-%02d %02d:%02d:%02d - pomijam NTP sync\n",
  //                 dt.date.year, dt.date.month, dt.date.date, dt.time.hours, dt.time.minutes, dt.time.seconds);
  //   return;  // RTC ma już poprawny czas, nie synchronizuj
  // }
  
  // Pobierz czas z NTP (UTC) - tylko jeśli RTC nie ma czasu
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  Serial.print("Czekam na NTP...");
  time_t now = time(nullptr);
  struct tm nowtimeinfo = *localtime(&now);

  Serial.printf(" now=%04d-%02d-%02d %02d:%02d:%02d\n",
              nowtimeinfo.tm_year + 1900, nowtimeinfo.tm_mon + 1, nowtimeinfo.tm_mday,
              nowtimeinfo.tm_hour, nowtimeinfo.tm_min, nowtimeinfo.tm_sec);
  int timeout = 20;
  // Czekaj aż time() zmieni się z 0 (znaczy że zsynchronizowano z NTP)
  while (/*now < 86400 && */timeout-- > 0) {  // 86400 = 1 dzień
    Serial.printf(" now=%04d-%02d-%02d %02d:%02d:%02d\n",
                  nowtimeinfo.tm_year + 1900, nowtimeinfo.tm_mon + 1, nowtimeinfo.tm_mday,
                  nowtimeinfo.tm_hour, nowtimeinfo.tm_min, nowtimeinfo.tm_sec);
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();
  
  if (now > 86400) {  // Synchronizacja się powiodła
    struct tm timeinfo = *localtime(&now);  // now to UTC
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    
    // Sprawdź czy obowiązuje DST dla Edynburga
    bool dst = isDSTActive(year, month, day);
    int offset_hours = dst ? 1 : 0;  // +1h jeśli BST
    
    // Ustaw RTC z prawidłowym offsetem
    m5::rtc_date_t date;
    date.year = year;
    date.month = month;
    date.date = day;
    date.weekDay = timeinfo.tm_wday;
    
    m5::rtc_time_t rtc_time;
    rtc_time.hours = (timeinfo.tm_hour + offset_hours) % 24;
    rtc_time.minutes = timeinfo.tm_min;
    rtc_time.seconds = timeinfo.tm_sec;
    
    M5Dial.Rtc.setDate(&date);
    M5Dial.Rtc.setTime(&rtc_time);
    Serial.printf("RTC zsynchronizowany: %04d-%02d-%02d %02d:%02d:%02d %s\n",
                  date.year, date.month, date.date, rtc_time.hours, rtc_time.minutes, rtc_time.seconds,
                  dst ? "(BST)" : "(GMT)");
  } else {
    Serial.println("NTP timeout - nie udało się zsynchronizować");
  }
}

// ============================================================
void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, /*enableEncoder=*/true, /*enableRFID=*/false);
  M5.Display.setBrightness(25);
  canvas.createSprite(M5Dial.Display.width(), M5Dial.Display.height());
  Serial.begin(115200);

  // UWAGA: jesli RTC nie byl wczesniej ustawiony (pierwsze uruchomienie
  // plytki), dt.time/dt.date beda zerowe. Odkomentuj i ustaw raz recznie:
  //
  m5::rtc_date_t initDate;
  initDate.year = 2026; initDate.month = 9; initDate.date = 1; initDate.weekDay = 1;
  m5::rtc_time_t initTime;
  initTime.hours = 14; initTime.minutes = 18; initTime.seconds = 0;
  M5Dial.Rtc.setDate(&initDate);
  M5Dial.Rtc.setTime(&initTime);
  //
  // Docelowo zastapimy to synchronizacja NTP po dodaniu WiFi.

  bleScanInit();
  wifiMgrInit();
  syncTimeFromNTP();

  drawSensorScreen(-1); // ekran startowy - "Szukam sensorow..."
  encoderOldPosition = M5Dial.Encoder.read();
}

// ============================================================
void loop() {
  bleScanLoop();
  wifiMgrLoop();

  // --- Klawiatura: M5Dial.update() wywolywane wewnatrz textInputUpdate() ---
  if (uiState == UI_KEYBOARD_NAME) {
    if (textInputUpdate()) {
      const char* result = textInputGetResult();
      encoderOldPosition = M5Dial.Encoder.read();
      if (displayedSlot >= 0) {
        // strncpy(customSensorNames[displayedSlot], result, TEXT_INPUT_MAX_LEN);
        // customSensorNames[displayedSlot][TEXT_INPUT_MAX_LEN] = '\0';
      }
      uiState = UI_SENSOR_VIEW;
      drawSensorScreen(displayedSlot);
      lastRedraw = millis();
    }
    return;
  }

  M5Dial.update();

  // --- Tryb menu ---
  if (uiState == UI_MENU) {
    long newPosition = M5Dial.Encoder.read();
    if (newPosition != encoderOldPosition) {
      int step = (newPosition > encoderOldPosition) ? 1 : -1;
      encoderOldPosition = newPosition;
      menuSelectedItem = (menuSelectedItem + step + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
      M5Dial.Speaker.tone(3500, 10);
      drawMenu();
    }
    if (M5Dial.BtnA.wasPressed()) {
      encoderOldPosition = M5Dial.Encoder.read();
      if (menuSelectedItem == 0) {         // "Change name"
        uiState = UI_KEYBOARD_NAME;
        textInputBegin("Sensor name:");
      }
    }
    return;
  }

  // --- Tryb widoku sensora (UI_SENSOR_VIEW) ---
  int activeSlots[MAX_SENSORS];
  int activeCount = getActiveSlots(activeSlots, MAX_SENSORS);

  if (M5Dial.BtnA.wasPressed() && displayedSlot >= 0) {
    menuSelectedItem = 0;
    encoderOldPosition = M5Dial.Encoder.read();
    uiState = UI_MENU;
    drawMenu();
    return;
  }

  long newPosition = M5Dial.Encoder.read();
  if (newPosition != encoderOldPosition && activeCount > 0) {
    int step = (newPosition > encoderOldPosition) ? 1 : -1;
    encoderOldPosition = newPosition;

    int curPos = 0;
    for (int i = 0; i < activeCount; i++) {
      if (activeSlots[i] == displayedSlot) { curPos = i; break; }
    }
    curPos = (curPos + step + activeCount) % activeCount;
    displayedSlot = activeSlots[curPos];

    M5Dial.Speaker.tone(4000, 15);
    drawSensorScreen(displayedSlot);
  }

  if (displayedSlot < 0 && activeCount > 0) {
    displayedSlot = activeSlots[0];
    drawSensorScreen(displayedSlot);
  }

  if (millis() - lastRedraw > 2000) {
    lastRedraw = millis();
    if (displayedSlot >= 0) {
      drawSensorScreen(displayedSlot);
    }
  }

  delay(20);
}
