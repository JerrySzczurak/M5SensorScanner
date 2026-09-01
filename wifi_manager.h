#pragma once
#include <Arduino.h>

enum WMEvent { WM_EVT_NONE, WM_EVT_CONNECTED, WM_EVT_FAILED };

// Inicjalizacja - wywolaj raz w setup(); laduje zapisane dane i probuje auto-connect
void wifiMgrInit();

bool   wifiMgrIsConnected();
String wifiMgrGetIP();

// Skanowanie sieci (asynchroniczne)
void   wifiMgrScanStart();
int    wifiMgrScanCount();      // -1 = skanowanie w toku
String wifiMgrScanSSID(int i);
int    wifiMgrScanRSSI(int i);

// Polaczenie z haslem
void wifiMgrConnectPassword(const char* ssid, const char* password);

// Polaczenie WPS-PBC
void wifiMgrConnectWPS();

// Przerwij laczenie / WPS; czyści oczekujące zdarzenia
void wifiMgrAbort();

// Wywoluj w loop() w stanach UI_WIFI_CONNECTING / UI_WIFI_WPS
WMEvent wifiMgrLoop();
