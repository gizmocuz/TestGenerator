// ============================================================
// ClockOut.ino — square-wave clock output on GPIO27 via LEDC
//
// LEDC max frequency depends on the duty-cycle resolution: the
// product   freq × 2^resolution_bits   must fit under the LEDC
// source clock (80 MHz APB on classic ESP32). We auto-pick the
// highest resolution the requested frequency allows so duty stays
// close to 50 %:
//
//   ≤ 312 kHz  → 8-bit (256 duty steps, very fine)
//   ≤   5 MHz  → 4-bit
//   ≤  20 MHz  → 2-bit
//   ≤  40 MHz  → 1-bit (just toggle, max throughput)
//
// (c) 2026 PA1DVB
// ============================================================

#include "ClockOut.h"

static bool     g_active = false;
static uint32_t g_freq   = 0;

void clockOutSetup() {
    pinMode(CLOCK_OUT_PIN, OUTPUT);
    digitalWrite(CLOCK_OUT_PIN, LOW);
}

void clockOutEnable(uint32_t freqHz) {
    if (freqHz < 1000 || freqHz > 40000000) return;
    if (g_active && g_freq == freqHz) return;        // nothing to do

    if (g_active) {
        ledcDetach(CLOCK_OUT_PIN);
        g_active = false;
    }

    uint8_t res;
    if      (freqHz <=    312500) res = 8;
    else if (freqHz <=   5000000) res = 4;
    else if (freqHz <=  20000000) res = 2;
    else                          res = 1;

    if (ledcAttach(CLOCK_OUT_PIN, freqHz, res)) {
        ledcWrite(CLOCK_OUT_PIN, 1u << (res - 1));   // 50 % duty
        g_active = true;
        g_freq   = freqHz;
    }
}

void clockOutDisable() {
    if (!g_active) return;
    ledcDetach(CLOCK_OUT_PIN);
    digitalWrite(CLOCK_OUT_PIN, LOW);
    g_active = false;
    g_freq   = 0;
}

bool     clockOutActive()    { return g_active; }
uint32_t clockOutFrequency() { return g_freq;   }
