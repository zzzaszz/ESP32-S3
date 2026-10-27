#include "BatteryTransport.h"
#include "BatteryDashboard.h"
#include "Display_ST7789.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <stdlib.h>

static WebServer server(80);
static String serialLine;
static bool serialLineOverflow = false;
static bool received[2] = {};
static bool expired[2] = {};
static uint32_t lastReceived[2] = {};
static bool wifiConnected = false;
static bool mdnsStarted = false;
static bool mdnsAttempted = false;
static IPAddress announcedIp;
static uint32_t lastReconnectAttempt = 0;
static uint32_t lastMdnsAttempt = 0;
static Preferences preferences;
static bool preferencesReady = false;
static String panelToken;
static bool tokenStorageInvalid = false;
static const char *const kPanelVersion = "BatteryPanel/1";
static String httpFrame;
static bool httpBodyTooLong = false;
static bool httpContentLengthInvalid = false;
static bool httpRequestRejected = false;
static bool httpFrameReady = false;

static void resetHttpState() {
  httpFrame = "";
  httpBodyTooLong = false;
  httpContentLengthInvalid = false;
  httpRequestRejected = false;
  httpFrameReady = false;
}

static void appendDeviceHealth(String &body, size_t device, uint32_t now) {
  body += "\ndevice";
  body += static_cast<unsigned>(device);
  body += "=";
  if (!received[device]) body += "waiting";
  else if (expired[device]) body += "expired";
  else body += "fresh";
  body += "\nage";
  body += static_cast<unsigned>(device);
  body += "=";
  if (!received[device]) body += "-";
  else body += static_cast<unsigned long>((now - lastReceived[device]) / 1000);
}

static bool isTokenHex(const String &value) {
  if (value.length() != 32) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

static bool parseBrightness(const String &command, int &value) {
  if (!command.startsWith("BRIGHTNESS ")) return false;
  const String number = command.substring(11);
  if (number.length() == 0) return false;
  char *end = nullptr;
  const long parsed = strtol(number.c_str(), &end, 10);
  if (end == number.c_str() || *end != '\0' || parsed < 0 || parsed > Backlight_MAX) return false;
  value = static_cast<int>(parsed);
  return true;
}

static bool configureWifi(const String &command) {
  if (!command.startsWith("WIFI\t")) return false;
  const int split = command.indexOf('\t', 5);
  if (split <= 5) return false;
  const String ssid = command.substring(5, split);
  const String passphrase = command.substring(split + 1);
  // IEEE 802.11 SSIDs are at most 32 bytes; WPA-PSK passphrases are at most 63.
  if (ssid.length() > 32 || passphrase.length() > 63) return false;
  WiFi.begin(ssid.c_str(), passphrase.c_str());
  Serial.println("WIFI CONNECTING");
  return true;
}

static void startMdns(const IPAddress &ip) {
  mdnsAttempted = true;
  lastMdnsAttempt = millis();
  if (mdnsStarted) MDNS.end();
  mdnsStarted = MDNS.begin("battery-panel");
  if (mdnsStarted) {
    MDNS.addService("http", "tcp", 80);
    announcedIp = ip;
    Serial.printf("WIFI READY %s battery-panel.local\n", ip.toString().c_str());
  } else {
    Serial.println("WIFI MDNS FAILED");
  }
}

static String healthBody() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  String body = String(kPanelVersion) + "\nwifi=";
  body += connected ? "connected" : "disconnected";
  body += "\nip=";
  body += connected ? WiFi.localIP().toString() : "-";
  body += "\nmdns=";
  body += mdnsStarted ? "battery-panel.local" : "-";
  const uint32_t now = millis();
  appendDeviceHealth(body, 0, now);
  appendDeviceHealth(body, 1, now);
  return body;
}

static void sendHealth() {
  const String body = healthBody();
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/plain", body);
}

static void printHelp() {
  Serial.println("HELP WIFI<TAB>SSID<TAB>PASSWORD");
  Serial.println("HELP TOKEN<TAB>HEX|CLEAR or TOKEN?");
  Serial.println("HELP BRIGHTNESS 0-100 or BRIGHTNESS?");
  Serial.println("HELP HEALTH? or IP");
  Serial.println("HELP VERSION?");
  Serial.println("HELP BAT2,device,level,connected,charging");
  Serial.println("HELP BAT1,level,connected,charging");
}

static bool applyFrame(const String &frame, bool wifi);

static void handleBatteryUpload() {
  HTTPRaw &raw = server.raw();
  if (raw.status == RAW_START) {
    resetHttpState();
    const int contentLength = server.clientContentLength();
    httpContentLengthInvalid = contentLength < 0;
    httpBodyTooLong = httpContentLengthInvalid || contentLength > 64;
    httpRequestRejected = !preferencesReady || tokenStorageInvalid ||
        (panelToken.length() && server.header("X-Battery-Token") != panelToken);
    return;
  }
  if (raw.status == RAW_END) {
    httpFrameReady = true;
    return;
  }
  if (raw.status != RAW_WRITE || httpBodyTooLong || httpRequestRejected) return;

  const size_t previousSize = raw.totalSize >= raw.currentSize
      ? raw.totalSize - raw.currentSize
      : 0;
  if (raw.totalSize > 64 || raw.currentSize > 64 - previousSize ||
      !httpFrame.concat(raw.buf, raw.currentSize)) {
    httpBodyTooLong = true;
  }
}

static void handleBattery() {
  const bool bodyTooLong = httpBodyTooLong;
  const bool contentLengthInvalid = httpContentLengthInvalid;
  const bool requestRejected = httpRequestRejected;
  const bool frameReady = httpFrameReady;
  const String frame = httpFrame;
  resetHttpState();
  if (!preferencesReady || tokenStorageInvalid) {
    server.send(503, "text/plain", "Settings unavailable");
    return;
  }
  if (requestRejected || (panelToken.length() && server.header("X-Battery-Token") != panelToken)) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }
  if (!frameReady) {
    server.send(400, "text/plain", "Invalid battery frame");
    return;
  }
  if (contentLengthInvalid) {
    server.send(400, "text/plain", "Invalid Content-Length");
    return;
  }
  if (bodyTooLong || server.clientContentLength() > 64) {
    server.send(413, "text/plain", "Battery frame too long");
    return;
  }
  if (frame.length() > 64) {
    server.send(413, "text/plain", "Battery frame too long");
    return;
  }
  const bool ok = applyFrame(frame, true);
  server.send(ok ? 200 : 400, "text/plain", ok ? "OK" : "Invalid battery frame");
}

// BAT1 remains the legacy mouse frame; BAT2 carries an explicit device index.
static bool applyFrame(const String &frame, bool wifi = false) {
  int device = 1, level, connected, charging, consumed = 0;
  const bool version2 = frame.startsWith("BAT2,");
  const int fields = version2
      ? sscanf(frame.c_str(), "BAT2,%d,%d,%d,%d%n", &device, &level, &connected, &charging, &consumed)
      : sscanf(frame.c_str(), "BAT1,%d,%d,%d%n", &level, &connected, &charging, &consumed);
  if (fields != (version2 ? 4 : 3) || device < 0 || device > 1 ||
      consumed != static_cast<int>(frame.length()) || level < -1 || level > 100 ||
      connected < 0 || connected > 1 || charging < (version2 ? -1 : 0) || charging > 1) return false;
  BatteryDashboard_UpdateDevice(device, level, connected, charging, true);
  BatteryDashboard_SetTransport(wifi);
  lastReceived[device] = millis();
  received[device] = true;
  expired[device] = false;
  if (version2) Serial.printf("ACK BAT2 %d %d\n", device, level);
  else Serial.printf("ACK BAT1 %d\n", level);
  return true;
}

void BatteryTransport_Init() {
  // Wi-Fi credentials stay in NVS and are supplied through USB, never in source.
  preferencesReady = preferences.begin("battery-panel", false);
  if (preferencesReady) {
    const uint8_t savedBrightness = preferences.getUChar("brightness", LCD_Backlight);
    if (savedBrightness <= Backlight_MAX) Set_Backlight(savedBrightness);
    panelToken = preferences.getString("http_token", "");
    tokenStorageInvalid = panelToken.length() && !isTokenHex(panelToken);
    if (tokenStorageInvalid) panelToken = "";
  } else {
    Serial.println("ERR SETTINGS STORAGE");
  }
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
  const char *headerKeys[] = {"X-Battery-Token"};
  server.collectHeaders(headerKeys, 1);
  server.on("/battery", HTTP_POST, handleBattery, handleBatteryUpload);
  server.on("/health", HTTP_GET, sendHealth);
  server.begin();
}

void BatteryTransport_Loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n') {
      if (serialLineOverflow) {
        Serial.println("ERR LINE TOO LONG");
      } else if (serialLine == "HELP" || serialLine == "?") {
        printHelp();
      } else if (serialLine.startsWith("WIFI\t")) {
        if (!configureWifi(serialLine)) Serial.println("ERR WIFI FORMAT");
      } else if (serialLine == "TOKEN?") {
        if (tokenStorageInvalid) Serial.println("TOKEN INVALID");
        else Serial.println(panelToken.length() ? "TOKEN CONFIGURED" : "TOKEN NOT CONFIGURED");
      } else if (serialLine == "VERSION?") {
        Serial.print("VERSION ");
        Serial.println(kPanelVersion);
      } else if (serialLine == "HEALTH?") {
        Serial.println(healthBody());
      } else if (serialLine.startsWith("TOKEN\t")) {
        const String candidate = serialLine.substring(6);
        if (candidate == "CLEAR") {
          if (!preferencesReady ||
              (preferences.isKey("http_token") && !preferences.remove("http_token"))) {
            Serial.println("ERR TOKEN CLEAR");
          } else {
            panelToken = "";
            tokenStorageInvalid = false;
            Serial.println("TOKEN CLEARED");
          }
        } else if (!isTokenHex(candidate)) {
          Serial.println("ERR TOKEN FORMAT");
        } else if (!preferencesReady || preferences.putString("http_token", candidate) == 0) {
          Serial.println("ERR TOKEN SAVE");
        } else {
          panelToken = candidate;
          tokenStorageInvalid = false;
          Serial.println("TOKEN SAVED");
        }
      } else if (serialLine == "BRIGHTNESS?") {
        Serial.printf("BRIGHTNESS %u\n", static_cast<unsigned>(LCD_Backlight));
      } else {
        int brightness = 0;
        if (parseBrightness(serialLine, brightness)) {
          Set_Backlight(static_cast<uint8_t>(brightness));
          if (preferencesReady && preferences.putUChar("brightness", static_cast<uint8_t>(brightness)) == 1) {
            Serial.printf("BRIGHTNESS %d SAVED\n", brightness);
          } else {
            Serial.printf("BRIGHTNESS %d UNSAVED\n", brightness);
          }
        } else if (serialLine == "IP") {
          Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "WIFI DISCONNECTED");
        } else if (!applyFrame(serialLine)) Serial.println("ERR FRAME");
      }
      serialLine = "";
      serialLineOverflow = false;
    } else if (c != '\r') {
      if (!serialLineOverflow) {
        if (serialLine.length() < 256) serialLine += c;
        else {
          serialLine = "";
          serialLineOverflow = true;
        }
      }
    }
  }
  server.handleClient();
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifiConnected) {
    wifiConnected = connected;
    BatteryDashboard_SetNetwork(connected);
    Serial.println(connected ? "WIFI CONNECTED" : "WIFI DISCONNECTED");
  }
  if (connected) {
    const IPAddress ip = WiFi.localIP();
    const bool addressChanged = mdnsStarted && ip != announcedIp;
    const bool retryMdns = !mdnsStarted &&
        (!mdnsAttempted || static_cast<uint32_t>(millis() - lastMdnsAttempt) >= 10000);
    if (addressChanged || retryMdns) startMdns(ip);
  } else {
    if (mdnsStarted) {
      MDNS.end();
      mdnsStarted = false;
    }
    if (static_cast<uint32_t>(millis() - lastReconnectAttempt) >= 10000) {
      lastReconnectAttempt = millis();
      WiFi.reconnect();
    }
  }
  for (int device = 0; device < 2; ++device) {
    if (received[device] && !expired[device] && static_cast<uint32_t>(millis() - lastReceived[device]) > 45000) {
      BatteryDashboard_UpdateDevice(device, -1, false, -1, false);
      expired[device] = true;
    }
  }
}
