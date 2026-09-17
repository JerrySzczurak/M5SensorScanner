#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "M5Dial.h"

// ============================================================
// BTHome v2 Object ID's (z bthome.io/format)
// ============================================================
#define BTHOME_OBJ_PACKET_ID    0x00
#define BTHOME_OBJ_BATTERY      0x01
#define BTHOME_OBJ_TEMPERATURE  0x02
#define BTHOME_OBJ_HUMIDITY     0x03
#define BTHOME_OBJ_PRESSURE     0x04

// ============================================================
// Struktura pojedynczego odczytu z sensora (zgodnie ze specyfikacja)
// ============================================================
struct SensorReading {
  std::string   mac_address;
  uint16_t      transaction_id;
  uint8_t       battery_level;
  float         temperature = 0;
  float         pressure = 0;
  float         humidity;
  unsigned long lastUpdate;
};

#define MAX_SENSORS 10

// Tablica odczytow - dostepna globalnie do odczytu przez UI/WiFi.
// UWAGA: dostep z innego zadania/petli niz callback BLE powinien
// isc przez getSensorReading() (bezpieczne watkowo, patrz nizej).
extern SensorReading sensors[MAX_SENSORS];

// Inicjalizacja skanera BLE - wywolaj RAZ w setup()
void bleScanInit();

// Obsluga skanera - wywoluj cyklicznie w loop()
// (pilnuje, zeby skanowanie nigdy nie zostalo trwale zatrzymane)
void bleScanLoop();

// Bezpieczne watkowo pobranie odczytu danego sensora (index 0..MAX_SENSORS-1).
// Zwraca false, jesli slot jest pusty (brak jeszcze danych z tego sensora).
bool getSensorReading(uint8_t index, SensorReading& out);
