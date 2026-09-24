#include "ble_scanner.h"

// #define DEBUG_ENABLED

// ============================================================
// BTHome v2 Service Data UUID
// ============================================================
static const uint16_t BTHOME_UUID = 0xFCD2;

// ============================================================
// Parametry skanowania (jednostki: ms - NimBLE-Arduino v2.x
// przyjmuje setInterval/setWindow bezposrednio w ms)
// ============================================================
static const uint16_t SCAN_INTERVAL_MS = 100;
static const uint16_t SCAN_WINDOW_MS   = 100;
static const bool     SCAN_ACTIVE      = false; // pasywne - my tylko sluchamy adv,
                                                  // nie potrzebujemy scan response

SensorReading sensors[MAX_SENSORS];

static SemaphoreHandle_t sensorsMutex = nullptr;

// ============================================================
// Parsowanie BTHome v2 Service Data (z Object ID'ami)
// Format: [DEVICE_INFO][OBJ_ID][value...][OBJ_ID][value...]...
//
// Oczekiwany layout (little-endian):
// [0]:        DEVICE_INFO (0x40)
// [1-2]:      OBJ_PACKET_ID (0x00) + packet_id (uint8)
// [3-4]:      OBJ_BATTERY   (0x01) + battery_level (uint8, 0-100%)
// [5-7]:      OBJ_TEMPERATURE (0x02) + temp_raw (int16, 0.01°C)
// [8-10]:     OBJ_HUMIDITY  (0x03) + humidity_raw (uint16, 0.01%)
// [11-14]:    OBJ_PRESSURE  (0x04) + pressure_raw (uint24, 0.01hPa)
// ============================================================
static bool parsePayload(const uint8_t* data, size_t len, SensorReading& out) {
  if (len < 5) return false;

  uint8_t pos = 0;
  uint8_t device_info = data[pos++];
  (void)device_info;

  while (pos < len) {
    uint8_t obj_id = data[pos++];
    if (obj_id == BTHOME_OBJ_PACKET_ID) {
      if (pos >= len) return false;
      out.transaction_id = (uint8_t)data[pos++];
    } else if (obj_id == BTHOME_OBJ_BATTERY) {
      if (pos >= len) return false;
      out.battery_level = data[pos++];
    } else if (obj_id == BTHOME_OBJ_TEMPERATURE) {
      if (pos + 1 >= len) return false;
      int16_t tempRaw = (int16_t)(data[pos] | (data[pos + 1] << 8));
      out.temperature = tempRaw / 100.0f;
      pos += 2;
    } else if (obj_id == BTHOME_OBJ_HUMIDITY) {
      if (pos + 1 >= len) return false;
      uint16_t humRaw = (uint16_t)(data[pos] | (data[pos + 1] << 8));
      out.humidity = humRaw / 100.0f;
      pos += 2;
    } else if (obj_id == BTHOME_OBJ_PRESSURE) {
      if (pos + 2 >= len) return false;
      uint32_t pressRaw = (uint32_t)(data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16));
      out.pressure = pressRaw / 100.0f;
      pos += 3;
    } else {
      return false;
    }
  }
  return true;
}

// ============================================================
// Znajdz slot dla danego MAC: istniejacy, albo pierwszy wolny.
// Zwraca -1, jesli MAC nieznany i brak wolnych slotow (limit MAX_SENSORS).
// ============================================================
static int findSlot(const std::string& mac) {
  int freeSlot = -1;
  for (int i = 0; i < MAX_SENSORS; i++) {
    if (sensors[i].mac_address == mac) {
      return i; // znaleziono istniejacy sensor
    }
    if (freeSlot < 0 && sensors[i].mac_address.empty()) {
      freeSlot = i; // zapamietaj pierwszy pusty na wypadek, gdyby MAC byl nowy
    }
  }
  return freeSlot;
}

// ============================================================
// Callback wywolywany przez NimBLE dla kazdego odebranego advertisingu
// ============================================================
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {

    // Pobranie Service Data dla UUID 0xFCD2 (BTHome v2)
    std::string svcData = device->getServiceData(NimBLEUUID(BTHOME_UUID));
    if (svcData.length() < 1) return;

    const uint8_t* payload    = (const uint8_t*)svcData.data();
    size_t         payloadLen = svcData.length();

    SensorReading parsed;
    if (!parsePayload(payload, payloadLen, parsed)) {
#ifdef DEBUG_ENABLED
      Serial.println("[BLE] Odrzucono pakiet - blad parsowania BTHome v2");
#endif
      return;
    }

    std::string mac = device->getAddress().toString();

    // --- Sekcja krytyczna: dostep do wspoldzielonej tablicy sensors[] ---
    if (xSemaphoreTake(sensorsMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
#ifdef DEBUG_ENABLED
      Serial.println("[BLE] Nie udalo sie pobrac mutexa - pomijam pakiet");
#endif
      return;
    }

    int slot = findSlot(mac);
    if (slot < 0) {
#ifdef DEBUG_ENABLED
      Serial.printf("[BLE] Brak wolnego slotu dla nowego sensora %s (limit %d)\n",
                    mac.c_str(), MAX_SENSORS);
#endif
      xSemaphoreGive(sensorsMutex);
      return;
    }

    // Deduplikacja po transaction_id
    if (sensors[slot].mac_address == mac &&
        sensors[slot].transaction_id == parsed.transaction_id) {
      xSemaphoreGive(sensorsMutex);
      return;
    }

    sensors[slot].mac_address    = mac;
    sensors[slot].transaction_id = parsed.transaction_id;
    sensors[slot].battery_level  = parsed.battery_level;
    sensors[slot].temperature    = parsed.temperature;
    sensors[slot].pressure       = parsed.pressure;
    sensors[slot].humidity       = parsed.humidity;
    sensors[slot].lastUpdate     = millis();

#ifdef DEBUG_ENABLED
    Serial.printf("[BLE] slot=%d mac=%s txid=%u bat=%u%% T=%.1fC P=%.0fhPa H=%.0f%%\n",
                  slot, mac.c_str(), sensors[slot].transaction_id,
                  sensors[slot].battery_level, sensors[slot].temperature,
                  sensors[slot].pressure, sensors[slot].humidity);
#endif

    xSemaphoreGive(sensorsMutex);
  }
};

static ScanCallbacks scanCallbacks;

// ============================================================
// Inicjalizacja - wywolaj raz w setup()
// ============================================================
void bleScanInit() {
  sensorsMutex = xSemaphoreCreateMutex();
  NimBLEDevice::init("");
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks, /*wantDuplicates=*/true);
  pScan->setActiveScan(SCAN_ACTIVE);
  pScan->setInterval(SCAN_INTERVAL_MS);
  pScan->setWindow(SCAN_WINDOW_MS);
  pScan->start(0, false);
#ifdef DEBUG_ENABLED
  Serial.printf("[BLE] Skaner BTHome v2 uruchomiony (UUID 0x%04X)\n", BTHOME_UUID);
#endif
}

// ============================================================
// Obsluga w petli - wywoluj cyklicznie w loop()
// ============================================================
void bleScanLoop() {
  NimBLEScan* pScan = NimBLEDevice::getScan();

  // Zabezpieczenie: gdyby skanowanie z jakiegos powodu sie zatrzymalo
  // (np. chwilowy konflikt z WiFi na wspoldzielonym radiu 2.4GHz,
  // albo host BLE zrobil reset i wywolal onScanEnd) - wznow je.
  if (!pScan->isScanning()) {
    Serial.println("[BLE] Skanowanie nieaktywne - wznawiam");
    pScan->start(0, false);
  }
}

// ============================================================
// Bezpieczny watkowo odczyt danych sensora (do uzycia w UI/WiFi)
// ============================================================
bool getSensorReading(uint8_t index, SensorReading& out) {
  if (index >= MAX_SENSORS) {
    return false;
  }

  bool ok = false;
  if (xSemaphoreTake(sensorsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!sensors[index].mac_address.empty()) {
      out = sensors[index];
      ok = true;
    }
    xSemaphoreGive(sensorsMutex);
  }
  return ok;
}
