#pragma once
#include <Arduino.h>

// === UZUPELNIJ SWOJE DANE WIFI ===
#define WIFI_SSID "UseYours"
#define WIFI_PASS "UseYours"

// Czas miedzy kolejnymi probami polaczenia gdy WiFi niedostepne (ms)
#define WIFI_RETRY_INTERVAL_MS 30000

void   wifiMgrInit();
void   wifiMgrLoop();
bool   wifiMgrIsConnected();
String wifiMgrGetIP();
