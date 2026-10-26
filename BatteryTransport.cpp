#include "BatteryTransport.h"
#include "BatteryDashboard.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);
static String serialLine;
static bool received[2] = {};
static bool expired[2] = {};
static uint32_t lastReceived[2] = {};

// Fixed versioned frame: BAT1,level(-1 unknown),connected(0/1),charging(0/1).
static bool applyFrame(const String &frame) {
  int device = 1, level, connected, charging, consumed = 0;
  const bool version2 = frame.startsWith("BAT2,");
  const int fields = version2
      ? sscanf(frame.c_str(), "BAT2,%d,%d,%d,%d%n", &device, &level, &connected, &charging, &consumed)
      : sscanf(frame.c_str(), "BAT1,%d,%d,%d%n", &level, &connected, &charging, &consumed);
  if (fields != (version2 ? 4 : 3) || device < 0 || device > 1 ||
      consumed != static_cast<int>(frame.length()) || level < -1 || level > 100 ||
      connected < 0 || connected > 1 || charging < (version2 ? -1 : 0) || charging > 1) return false;
  BatteryDashboard_UpdateDevice(device, level, connected, charging, true);
  lastReceived[device] = millis();
  received[device] = true;
  expired[device] = false;
  if (version2) Serial.printf("ACK BAT2 %d %d\n", device, level);
  else Serial.printf("ACK BAT1 %d\n", level);
  return true;
}

void BatteryTransport_Init() {
  // Wi-Fi credentials stay in NVS and are supplied through USB, never in source.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
  server.on("/battery", HTTP_POST, []() {
    const bool ok = applyFrame(server.arg("plain"));
    server.send(ok ? 200 : 400, "text/plain", ok ? "OK" : "Invalid battery frame");
  });
  server.on("/health", HTTP_GET, []() { server.send(200, "text/plain", "BatteryPanel/1"); });
  server.begin();
}

void BatteryTransport_Loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n') {
      serialLine.trim();
      if (serialLine.startsWith("WIFI\t")) {
        const int split = serialLine.indexOf('\t', 5);
        if (split > 5) {
          WiFi.begin(serialLine.substring(5, split).c_str(), serialLine.substring(split + 1).c_str());
          Serial.println("WIFI CONFIGURED");
        }
      } else if (serialLine == "IP") {
        Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "WIFI DISCONNECTED");
      } else if (!applyFrame(serialLine)) Serial.println("ERR FRAME");
      serialLine = "";
    } else if (c != '\r') {
      if (serialLine.length() < 256) serialLine += c;
      else serialLine = "";
    }
  }
  server.handleClient();
  for (int device = 0; device < 2; ++device) {
    if (received[device] && !expired[device] && static_cast<uint32_t>(millis() - lastReceived[device]) > 45000) {
      BatteryDashboard_UpdateDevice(device, -1, false, -1, false);
      expired[device] = true;
    }
  }
}
