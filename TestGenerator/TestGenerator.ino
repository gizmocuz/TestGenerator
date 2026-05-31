// ============================================================
// TestGenerator — ESP32 WROOM dual-DAC test pattern generator
//   DAC1 = GPIO25  (X / mono)
//   DAC2 = GPIO26  (Y / inverted mono / second tone)
//
// Web UI selects waveform and parameters.
// WiFiManager AP portal on first boot + OTA updates.
//
// (c) 2026 PA1DVB
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>           // needed by Si5351 path; harmless when disabled

#include "Config.h"
#include "Features.h"
#include "Waveforms.h"
#include "Vectors.h"
#include "ClockOut.h"
#include "Si5351RF.h"

// --- Firmware identity ---
#define FIRMWARE_PREFIX   "esp32-testgen"
#define APP_VERSION       "2026.05.25 rev 1.0"

// --- Globals ---
char         identifier[24];
WebServer    webServer(80);
WiFiManager  wifiManager;
bool         shouldSaveConfig = false;

// Forward decls (other .ino files)
void setupWifi();
void setupOTA();
void registerWebHandlers();
void waveformTaskStart();

// ============================================================
// setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("\nTestGenerator %s starting...\n", APP_VERSION);

    if (!SPIFFS.begin(false)) {
        Serial.println("SPIFFS mount failed — formatting");
        SPIFFS.format();
        SPIFFS.begin(false);
    }
    Config::load();

    // DAC pads are configured by the I²S driver in waveformTaskStart() —
    // no dacWrite() priming needed.
    clockOutSetup();        // GPIO27 as output, idle low
#if ENABLE_SI5351
    Wire.begin();           // default ESP32 I²C pins: SDA=GPIO21, SCL=GPIO22
    si5351Setup();
#endif

    // Use the factory eFuse MAC — it's available immediately, whereas
    // WiFi.macAddress() can return zeros if WiFi isn't fully up yet.
    uint64_t chipmac = ESP.getEfuseMac();
    snprintf(identifier, sizeof(identifier), "TESTGEN-%02X%02X%02X%02X%02X%02X",
             (uint8_t)(chipmac >> 40), (uint8_t)(chipmac >> 32),
             (uint8_t)(chipmac >> 24), (uint8_t)(chipmac >> 16),
             (uint8_t)(chipmac >>  8), (uint8_t)(chipmac));
    Serial.printf("Device ID: %s\n", identifier);

    waveformApplyConfig();      // copy persisted settings into engine

    // Bring up networking BEFORE the sample task starts. The task busy-waits
    // on core 0 and would otherwise starve WiFi during init / AP bring-up.
    setupWifi();
    Serial.printf("WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());

    setupOTA();

    registerWebHandlers();
    webServer.begin();
    Serial.println("HTTP server started.");

    waveformTaskStart();        // launch sample task on core 0
    Serial.println("Waveform task started.");
}

// ============================================================
// loop
// ============================================================
void loop() {
    ArduinoOTA.handle();
    webServer.handleClient();
    delay(1);
}
