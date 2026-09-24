#include "wifi_manager.h"
#include <WiFi.h>

static unsigned long s_lastAttempt = 0;

void wifiMgrInit() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(WIFI_PS_MIN_MODEM);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  s_lastAttempt = millis();
}

void wifiMgrLoop() {
  if (WiFi.status() != WL_CONNECTED &&
      millis() - s_lastAttempt >= WIFI_RETRY_INTERVAL_MS) {
    s_lastAttempt = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

bool wifiMgrIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String wifiMgrGetIP() {
  return WiFi.localIP().toString();
}
