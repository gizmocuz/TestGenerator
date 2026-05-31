// ============================================================
// Config.h — Persistent configuration (SPIFFS / config.json)
//
// (c) 2026 PA1DVB
// ============================================================
#pragma once

#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>

namespace Config {
    // Last-used waveform settings (restored on boot)
    int      wave_id      = 0;       // index into kWaveforms[]
    float    frequency    = 1000.0f; // Hz (or pattern cycles/sec for X/Y)
    float    amplitude    = 100.0f;  // 0..100 %
    float    offset       = 0.0f;    // -100..+100 %  (DC offset)
    float    duty         = 50.0f;   // 0..100 %  (square/pulse duty cycle)
    float    mod_freq     = 5.0f;    // Hz  modulation frequency (AM/FM/burst rate)
    float    mod_depth    = 50.0f;   // 0..100 % modulation depth / FM deviation
    float    sweep_low    = 100.0f;  // Hz chirp low
    float    sweep_high   = 5000.0f; // Hz chirp high
    float    sweep_time   = 1.0f;    // seconds per sweep
    bool     running      = true;    // output enabled

    inline void save() {
        DynamicJsonDocument json(1024);
        json["wave_id"]    = wave_id;
        json["frequency"]  = frequency;
        json["amplitude"]  = amplitude;
        json["offset"]     = offset;
        json["duty"]       = duty;
        json["mod_freq"]   = mod_freq;
        json["mod_depth"]  = mod_depth;
        json["sweep_low"]  = sweep_low;
        json["sweep_high"] = sweep_high;
        json["sweep_time"] = sweep_time;
        json["running"]    = running;

        File f = SPIFFS.open("/config.json", "w");
        if (!f) return;
        serializeJson(json, f);
        f.close();
    }

    inline void load() {
        if (!SPIFFS.exists("/config.json")) return;
        File f = SPIFFS.open("/config.json", "r");
        if (!f) return;
        const size_t size = f.size();
        std::unique_ptr<char[]> buf(new char[size + 1]);
        f.readBytes(buf.get(), size);
        buf.get()[size] = '\0';
        f.close();

        DynamicJsonDocument json(1024);
        if (deserializeJson(json, buf.get()) != DeserializationError::Ok) return;

        wave_id    = json["wave_id"]    | 0;
        frequency  = json["frequency"]  | 1000.0f;
        amplitude  = json["amplitude"]  | 100.0f;
        offset     = json["offset"]     | 0.0f;
        duty       = json["duty"]       | 50.0f;
        mod_freq   = json["mod_freq"]   | 5.0f;
        mod_depth  = json["mod_depth"]  | 50.0f;
        sweep_low  = json["sweep_low"]  | 100.0f;
        sweep_high = json["sweep_high"] | 5000.0f;
        sweep_time = json["sweep_time"] | 1.0f;
        running    = json["running"]    | true;
    }
}
