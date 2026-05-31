// ============================================================
// WebServer.ino — Web UI + JSON API
//
// Routes:
//   GET  /              status + controls page
//   POST /set           form-encoded parameter updates → redirect to /
//   GET  /api/state     current settings as JSON
//   POST /api/state     JSON body → apply + persist
//   GET  /reset_wifi    forget WiFi creds, reboot to AP portal
//
// (c) 2026 PA1DVB
// ============================================================

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "Config.h"
#include "Waveforms.h"

extern WebServer webServer;
extern char      identifier[24];
#define APP_VERSION_STR "2026.05.25 rev 1.0"

void resetWifiSettingsAndReboot();

// ---------------------------------------------------------------------------

static void sendStatePayload() {
    DynamicJsonDocument doc(512);
    doc["wave"]        = waveformIdToStr(Config::wave_id);
    doc["frequency"]   = Config::frequency;
    doc["amplitude"]   = Config::amplitude;
    doc["offset"]      = Config::offset;
    doc["duty"]        = Config::duty;
    doc["mod_freq"]    = Config::mod_freq;
    doc["mod_depth"]   = Config::mod_depth;
    doc["sweep_low"]   = Config::sweep_low;
    doc["sweep_high"]  = Config::sweep_high;
    doc["sweep_time"]  = Config::sweep_time;
    doc["running"]     = Config::running;
    doc["ip"]          = WiFi.localIP().toString();
    doc["rssi"]        = WiFi.RSSI();
    doc["device"]      = identifier;
    doc["version"]     = APP_VERSION_STR;

    String out;
    serializeJson(doc, out);
    webServer.send(200, "application/json", out);
}

// ---------------------------------------------------------------------------

static void handleApiStateGet() {
    sendStatePayload();
}

static void handleApiStatePost() {
    if (!webServer.hasArg("plain") || webServer.arg("plain").length() == 0) {
        webServer.send(400, "application/json", "{\"error\":\"empty body\"}");
        return;
    }
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, webServer.arg("plain")) != DeserializationError::Ok) {
        webServer.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    if (doc.containsKey("wave")) {
        int id = waveformStrToId(doc["wave"].as<const char*>());
        if (id >= 0) Config::wave_id = id;
    }
    if (doc.containsKey("frequency"))  Config::frequency  = doc["frequency"];
    if (doc.containsKey("amplitude"))  Config::amplitude  = doc["amplitude"];
    if (doc.containsKey("offset"))     Config::offset     = doc["offset"];
    if (doc.containsKey("duty"))       Config::duty       = doc["duty"];
    if (doc.containsKey("mod_freq"))   Config::mod_freq   = doc["mod_freq"];
    if (doc.containsKey("mod_depth"))  Config::mod_depth  = doc["mod_depth"];
    if (doc.containsKey("sweep_low"))  Config::sweep_low  = doc["sweep_low"];
    if (doc.containsKey("sweep_high")) Config::sweep_high = doc["sweep_high"];
    if (doc.containsKey("sweep_time")) Config::sweep_time = doc["sweep_time"];
    if (doc.containsKey("running"))    Config::running    = doc["running"];

    waveformApplyConfig();
    Config::save();
    sendStatePayload();
}

// ---------------------------------------------------------------------------

static void handleSet() {
    if (webServer.hasArg("wave")) {
        int id = waveformStrToId(webServer.arg("wave").c_str());
        if (id >= 0) Config::wave_id = id;
    }
    if (webServer.hasArg("frequency"))  Config::frequency  = webServer.arg("frequency").toFloat();
    if (webServer.hasArg("amplitude"))  Config::amplitude  = webServer.arg("amplitude").toFloat();
    if (webServer.hasArg("offset"))     Config::offset     = webServer.arg("offset").toFloat();
    if (webServer.hasArg("duty"))       Config::duty       = webServer.arg("duty").toFloat();
    if (webServer.hasArg("mod_freq"))   Config::mod_freq   = webServer.arg("mod_freq").toFloat();
    if (webServer.hasArg("mod_depth"))  Config::mod_depth  = webServer.arg("mod_depth").toFloat();
    if (webServer.hasArg("sweep_low"))  Config::sweep_low  = webServer.arg("sweep_low").toFloat();
    if (webServer.hasArg("sweep_high")) Config::sweep_high = webServer.arg("sweep_high").toFloat();
    if (webServer.hasArg("sweep_time")) Config::sweep_time = webServer.arg("sweep_time").toFloat();
    if (webServer.hasArg("run"))        Config::running    = (webServer.arg("run") == "1" || webServer.arg("run") == "on");
    if (webServer.hasArg("stop"))       Config::running    = false;
    if (webServer.hasArg("start"))      Config::running    = true;

    waveformApplyConfig();
    Config::save();
    webServer.sendHeader("Location", "/");
    webServer.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------

static void handleRoot() {
    char buf[640];

    webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer.send(200, "text/html", "");

    webServer.sendContent(R"html(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="data:,"><title>ESP32 Test Generator</title><style>body{font-family:Helvetica,Arial,sans-serif;max-width:560px;margin:0 auto;padding:14px;color:#222}h1{font-size:1.4em;text-align:center;margin:0 0 4px}.sub{text-align:center;color:#888;font-size:0.85em;margin-bottom:14px}fieldset{border:1px solid #ddd;border-radius:6px;margin:10px 0;padding:10px 14px}legend{font-weight:bold;color:#195B6A;padding:0 6px}label{display:block;margin:6px 0 2px;font-weight:bold;font-size:0.9em}input[type=number],select{width:100%;box-sizing:border-box;padding:6px;font-size:1em;border:1px solid #ccc;border-radius:4px}input[type=range]{width:100%}.row{display:flex;gap:8px}.row>div{flex:1}.btn{display:inline-block;background:#195B6A;color:#fff;padding:10px 22px;border:none;border-radius:4px;font-size:1em;cursor:pointer;text-decoration:none;margin:4px 2px}.btn-on{background:#17A2FC}.btn-off{background:#888}.btn-stop{background:#E74C3C}.state-on{display:inline-block;padding:3px 12px;border-radius:4px;background:#17A2FC;color:#fff;font-weight:bold}.state-off{display:inline-block;padding:3px 12px;border-radius:4px;background:#888;color:#fff;font-weight:bold}.center{text-align:center}small{color:#888}hr{border:0;border-top:1px solid #eee;margin:14px 0}</style></head><body>)html");

    webServer.sendContent("<h1>ESP32 Test Generator</h1>");

    snprintf(buf, sizeof(buf),
        "<div class=\"sub\">%s &middot; %s &middot; IP %s</div>",
        identifier, APP_VERSION_STR, WiFi.localIP().toString().c_str());
    webServer.sendContent(buf);

    snprintf(buf, sizeof(buf),
        "<div class=\"center\">Output: <span class=\"%s\">%s</span> &nbsp; "
        "<a class=\"btn btn-on\" href=\"/set?start=1\">Start</a> "
        "<a class=\"btn btn-stop\" href=\"/set?stop=1\">Stop</a></div>",
        Config::running ? "state-on" : "state-off",
        Config::running ? "RUNNING" : "STOPPED");
    webServer.sendContent(buf);

    webServer.sendContent("<form method=\"GET\" action=\"/set\">");

    // ---- Waveform select ----
    webServer.sendContent("<fieldset><legend>Waveform</legend><select name=\"wave\" onchange=\"this.form.submit()\">");
    const char* lastGroup = "";
    for (int i = 0; i < WAVE_COUNT; ++i) {
        if (strcmp(kWaveforms[i].group, lastGroup) != 0) {
            if (lastGroup[0]) webServer.sendContent("</optgroup>");
            snprintf(buf, sizeof(buf), "<optgroup label=\"%s\">", kWaveforms[i].group);
            webServer.sendContent(buf);
            lastGroup = kWaveforms[i].group;
        }
        snprintf(buf, sizeof(buf), "<option value=\"%s\"%s>%s</option>",
                 kWaveforms[i].id,
                 (Config::wave_id == i) ? " selected" : "",
                 kWaveforms[i].label);
        webServer.sendContent(buf);
    }
    if (lastGroup[0]) webServer.sendContent("</optgroup>");
    webServer.sendContent("</select></fieldset>");

    // ---- Common parameters ----
    snprintf(buf, sizeof(buf),
        "<fieldset><legend>Signal</legend>"
        "<label>Frequency (Hz)</label>"
        "<input type=\"number\" name=\"frequency\" step=\"0.1\" min=\"0.1\" max=\"40000000\" value=\"%.2f\">"
        "<small>X/Y patterns: pattern cycles/sec. Vector shapes: refresh rate (Hz). Clock Variable: square-wave frequency in Hz (1 k – 40 M).</small>"
        "<div class=\"row\"><div><label>Amplitude (%%)</label>"
        "<input type=\"number\" name=\"amplitude\" step=\"1\" min=\"0\" max=\"100\" value=\"%.0f\"></div>"
        "<div><label>DC Offset (%%)</label>"
        "<input type=\"number\" name=\"offset\" step=\"1\" min=\"-100\" max=\"100\" value=\"%.0f\"></div></div>"
        "<label>Duty cycle (%%) <small>— pulse / burst</small></label>"
        "<input type=\"number\" name=\"duty\" step=\"1\" min=\"1\" max=\"99\" value=\"%.0f\">"
        "</fieldset>",
        Config::frequency, Config::amplitude, Config::offset, Config::duty);
    webServer.sendContent(buf);

    // ---- Modulation ----
    snprintf(buf, sizeof(buf),
        "<fieldset><legend>Modulation</legend>"
        "<div class=\"row\"><div><label>Mod Freq (Hz)</label>"
        "<input type=\"number\" name=\"mod_freq\" step=\"0.1\" min=\"0.1\" max=\"5000\" value=\"%.2f\"></div>"
        "<div><label>Mod Depth (%%)</label>"
        "<input type=\"number\" name=\"mod_depth\" step=\"1\" min=\"0\" max=\"100\" value=\"%.0f\"></div></div>"
        "<small>AM/FM index, burst rate, or spiral expansion speed.</small>"
        "</fieldset>",
        Config::mod_freq, Config::mod_depth);
    webServer.sendContent(buf);

    // ---- Sweep ----
    snprintf(buf, sizeof(buf),
        "<fieldset><legend>Chirp / Sweep</legend>"
        "<div class=\"row\"><div><label>Low (Hz)</label>"
        "<input type=\"number\" name=\"sweep_low\" step=\"1\" min=\"1\" value=\"%.0f\"></div>"
        "<div><label>High (Hz)</label>"
        "<input type=\"number\" name=\"sweep_high\" step=\"1\" min=\"2\" value=\"%.0f\"></div>"
        "<div><label>Time (s)</label>"
        "<input type=\"number\" name=\"sweep_time\" step=\"0.05\" min=\"0.05\" value=\"%.2f\"></div></div>"
        "</fieldset>",
        Config::sweep_low, Config::sweep_high, Config::sweep_time);
    webServer.sendContent(buf);

    webServer.sendContent("<div class=\"center\"><button class=\"btn\" type=\"submit\">Apply</button></div></form>");

    webServer.sendContent(
        "<hr><div class=\"center\"><small>"
        "DAC1 = GPIO25 &middot; DAC2 = GPIO26 (inverted for mono, Y for X/Y)<br>"
        "Clock / RF square-wave output: GPIO27 (1 kHz – 40 MHz)<br>"
        "8-bit DAC, 100 kSPS, Nyquist 50 kHz<br>"
        "<a href=\"/reset_wifi\" onclick=\"return confirm('Forget WiFi and reboot into AP mode?')\" style=\"color:#E74C3C\">Reset WiFi</a>"
        "</small><br><br><small>(c) 2026 PA1DVB</small></div>"
        "</body></html>");
}

// ---------------------------------------------------------------------------

static void handleResetWifi() {
    webServer.send(200, "text/plain", "Resetting WiFi. Rebooting into AP mode.");
    delay(300);
    resetWifiSettingsAndReboot();
}

static void handleNotFound() {
    webServer.send(404, "text/plain", "Not found: " + webServer.uri());
}

// ---------------------------------------------------------------------------

void registerWebHandlers() {
    webServer.on("/",            HTTP_GET,  handleRoot);
    webServer.on("/set",         HTTP_GET,  handleSet);
    webServer.on("/api/state",   HTTP_GET,  handleApiStateGet);
    webServer.on("/api/state",   HTTP_POST, handleApiStatePost);
    webServer.on("/reset_wifi",  HTTP_GET,  handleResetWifi);
    webServer.onNotFound(handleNotFound);
}
