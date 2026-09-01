#include "wifi_manager.h"
#include <WiFi.h>
#include <Preferences.h>
#include "esp_wps.h"

static const char* NVS_NS   = "wifi_mgr";
static const char* NVS_SSID = "ssid";
static const char* NVS_PASS = "pass";

// volatile: flagi pisane z WiFi task (core 0), czytane z loop (core 1)
static volatile WMEvent s_pendingEvent = WM_EVT_NONE;
static volatile bool    s_connecting   = false;
static volatile bool    s_wpsActive    = false;

static char s_pendingSSID[33] = {};
static char s_pendingPass[65] = {};

static void saveCredentials(const char* ssid, const char* pass) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putString(NVS_SSID, ssid);
  prefs.putString(NVS_PASS, pass);
  prefs.end();
}

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      if (s_connecting || s_wpsActive) {
        if (s_wpsActive) {
          saveCredentials(WiFi.SSID().c_str(), WiFi.psk().c_str());
        } else {
          saveCredentials(s_pendingSSID, s_pendingPass);
        }
        s_connecting   = false;
        s_wpsActive    = false;
        s_pendingEvent = WM_EVT_CONNECTED;
      }
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (s_connecting || s_wpsActive) {
        s_connecting   = false;
        s_wpsActive    = false;
        s_pendingEvent = WM_EVT_FAILED;
      }
      break;

    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      esp_wifi_wps_disable();
      WiFi.begin(); // polacz z danymi otrzymanymi przez WPS
      break;

    case ARDUINO_EVENT_WPS_ER_FAILED:
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      esp_wifi_wps_disable();
      s_wpsActive    = false;
      s_pendingEvent = WM_EVT_FAILED;
      break;

    default:
      break;
  }
}

void wifiMgrInit() {
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  Preferences prefs;
  prefs.begin(NVS_NS, true);
  String ssid = prefs.getString(NVS_SSID, "");
  String pass = prefs.getString(NVS_PASS, "");
  prefs.end();

  if (ssid.length() > 0) {
    strncpy(s_pendingSSID, ssid.c_str(), sizeof(s_pendingSSID) - 1);
    strncpy(s_pendingPass, pass.c_str(), sizeof(s_pendingPass) - 1);
    s_connecting = true;
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[WiFi] Auto-connect do '%s'\n", ssid.c_str());
  }
}

bool wifiMgrIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String wifiMgrGetIP() {
  return WiFi.localIP().toString();
}

void wifiMgrScanStart() {
  WiFi.scanDelete();
  WiFi.scanNetworks(/*async=*/true);
}

int wifiMgrScanCount() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  if (n < 0)                  return 0;
  return n;
}

String wifiMgrScanSSID(int i) {
  return WiFi.SSID(i);
}

int wifiMgrScanRSSI(int i) {
  return WiFi.RSSI(i);
}

void wifiMgrConnectPassword(const char* ssid, const char* password) {
  strncpy(s_pendingSSID, ssid,     sizeof(s_pendingSSID) - 1);
  strncpy(s_pendingPass, password, sizeof(s_pendingPass) - 1);
  s_pendingSSID[sizeof(s_pendingSSID) - 1] = '\0';
  s_pendingPass[sizeof(s_pendingPass) - 1] = '\0';
  s_connecting = true;
  WiFi.begin(ssid, password);
}

void wifiMgrConnectWPS() {
  esp_wps_config_t config;
  memset(&config, 0, sizeof(config));
  config.wps_type = WPS_TYPE_PBC;
  strncpy(config.factory_info.manufacturer, "M5Stack",
          sizeof(config.factory_info.manufacturer) - 1);
  strncpy(config.factory_info.model_number,  "M5Dial",
          sizeof(config.factory_info.model_number)  - 1);
  strncpy(config.factory_info.model_name,    "M5Dial",
          sizeof(config.factory_info.model_name)    - 1);
  strncpy(config.factory_info.device_name,   "M5Dial",
          sizeof(config.factory_info.device_name)   - 1);

  s_wpsActive = true;
  WiFi.disconnect();
  esp_wifi_wps_enable(&config);
  esp_wifi_wps_start(0); // 0 = domyslny timeout (120s)
}

void wifiMgrAbort() {
  if (s_wpsActive) {
    esp_wifi_wps_disable();
  }
  // Flagi czyszczone PRZED disconnect, zeby event handler nie wygenerował WM_EVT_FAILED
  s_wpsActive    = false;
  s_connecting   = false;
  s_pendingEvent = WM_EVT_NONE;
  WiFi.disconnect();
}

WMEvent wifiMgrLoop() {
  WMEvent evt    = (WMEvent)s_pendingEvent;
  s_pendingEvent = WM_EVT_NONE;
  return evt;
}
