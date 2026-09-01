#include "wifi_manager.h"
#include <WiFi.h>

#define CONFIG_LWIP_IPV6 0
#define CONFIG_LWIP_IPV6_AUTOCONFIG 0

static unsigned long s_lastAttempt = 0;

void wifiMgrInit() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(WIFI_PS_MIN_MODEM);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  s_lastAttempt = millis();
//  Serial.printf("[WiFi] Laczenie z '%s'\n", WIFI_SSID);
}

void wifiMgrLoop() {
  if (WiFi.status() != WL_CONNECTED &&
      millis() - s_lastAttempt >= WIFI_RETRY_INTERVAL_MS) {
    s_lastAttempt = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
//    Serial.println("[WiFi] Ponawiam polaczenie...");
  }
}

bool wifiMgrIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String wifiMgrGetIP() {
  return WiFi.localIP().toString();
}
