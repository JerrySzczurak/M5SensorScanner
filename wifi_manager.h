#pragma once
#include <Arduino.h>

// === UZUPELNIJ SWOJE DANE WIFI ===
#define WIFI_SSID "TwojaWiFi"
#define WIFI_PASS "TwojeHaslo"

// Czas miedzy kolejnymi probami polaczenia gdy WiFi niedostepne (ms)
#define WIFI_RETRY_INTERVAL_MS 30000

// Minimalizuj WiFi stack - wyłącz IPv6 i zbędne cechy
#define CONFIG_LWIP_IPV6 0
#define CONFIG_LWIP_IPV6_AUTOCONFIG 0

void   wifiMgrInit();       // wywolaj raz w setup()
void   wifiMgrLoop();       // wywolaj w kazdym loop()
bool   wifiMgrIsConnected();
String wifiMgrGetIP();
