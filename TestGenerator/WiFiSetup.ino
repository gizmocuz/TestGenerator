// ============================================================
// WiFiSetup.ino — WiFiManager (AP captive portal) + ArduinoOTA
//
// (c) 2026 PA1DVB
// ============================================================

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

extern char         identifier[24];
extern WiFiManager  wifiManager;
extern bool         shouldSaveConfig;

static void saveConfigCallback() {
    shouldSaveConfig = true;
}

void setupWifi() {
    wifiManager.setDebugOutput(false);
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setConnectTimeout(60);
    wifiManager.setConfigPortalTimeout(0);

    // setHostname() needs the netif to exist — that's created by
    // WiFi.mode(). Without this explicit mode call, WiFiManager only
    // calls mode() later inside autoConnect(), by which point
    // setHostname() has already silently failed and OTA's mDNS
    // advertisement never gets the right name.
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(identifier);

    Serial.println("WiFi: connecting...");
    if (!wifiManager.autoConnect(identifier)) {
        // Saved credentials failed — captive portal is up. Keep retrying
        // the stored SSID in parallel so we self-heal when the router
        // comes back, without forcing a reboot.
        Serial.println("WiFi: AP portal open — connect to it to configure.");
        uint32_t apStart   = millis();
        uint32_t lastRetry = millis();
        while (WiFi.status() != WL_CONNECTED) {
            wifiManager.process();
            delay(20);
            if (millis() - lastRetry > 30000UL) {
                lastRetry = millis();
                WiFi.reconnect();
            }
            // Safety net: reboot after 12h still-disconnected.
            if (millis() - apStart > 12UL * 60UL * 60UL * 1000UL) {
                Serial.println("WiFi: AP open >12h, rebooting...");
                ESP.restart();
            }
        }
    }

    // Disable WiFi modem sleep. By default the ESP32 parks the radio between DTIM
    // beacons, adding ~70-140 ms latency to every web/OTA request when the device is
    // otherwise idle. Costs ~20-30 mA more idle current.
    WiFi.setSleep(false);

    Serial.printf("WiFi: connected.  IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void setupOTA() {
    ArduinoOTA.onStart([]() { Serial.println("OTA: start"); });
    ArduinoOTA.onEnd  ([]() { Serial.println("\nOTA: end");  });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Serial.printf("OTA progress: %u%%\r", (p / (t / 100)));
    });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("OTA error[%u]\n", err);
    });
    ArduinoOTA.setHostname(identifier);
    ArduinoOTA.setPassword(identifier);
    ArduinoOTA.begin();   // also starts MDNS responder and advertises _arduino._tcp

    // Belt-and-suspenders: advertise the web UI on Bonjour too. If MDNS
    // is already running (it is, after ArduinoOTA.begin), this just adds
    // the extra service record.
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS: hostname %s.local\n", identifier);
}

void resetWifiSettingsAndReboot() {
    wifiManager.resetSettings();
    delay(500);
    ESP.restart();
}
