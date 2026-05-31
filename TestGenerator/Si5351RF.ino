// ============================================================
// Si5351RF.ino — Optional Si5351A clock-generator support
//
// The entire file is a no-op when ENABLE_SI5351 == 0 (the default),
// which lets the sketch compile without the Etherkit library being
// present.
//
// (c) 2026 PA1DVB
// ============================================================

#include "Si5351RF.h"

#if ENABLE_SI5351

static Si5351 g_si5351;
static bool   g_ok = false;

void si5351Setup() {
    // 8 pF crystal load matches the Adafruit 2045 breakout (also most
    // generic clones). If yours uses a 6 or 10 pF crystal, change here.
    g_ok = g_si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
    if (!g_ok) {
        Serial.println("Si5351: not detected on I²C (continuing without it)");
        return;
    }
    Serial.println("Si5351: initialised");

    // Healthy default drive — 8 mA gives ~+10 dBm into 50 Ω. Lower it
    // (2 / 4 / 6 mA) for less spectral splatter at high frequencies.
    g_si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
    g_si5351.drive_strength(SI5351_CLK1, SI5351_DRIVE_8MA);
    g_si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_8MA);

    si5351DisableAll();
}

bool si5351Available() { return g_ok; }

static si5351_clock chanOf(uint8_t channel) {
    if (channel == 1) return SI5351_CLK1;
    if (channel == 2) return SI5351_CLK2;
    return SI5351_CLK0;
}

void si5351SetFreq(uint64_t freqHz, uint8_t channel) {
    if (!g_ok) return;
    const si5351_clock ch = chanOf(channel);
    // Etherkit takes frequency in centi-Hz (i.e. ×100) so sub-Hz tuning works.
    g_si5351.set_freq(freqHz * 100ULL, ch);
    g_si5351.output_enable(ch, 1);
}

void si5351DisableAll() {
    if (!g_ok) return;
    g_si5351.output_enable(SI5351_CLK0, 0);
    g_si5351.output_enable(SI5351_CLK1, 0);
    g_si5351.output_enable(SI5351_CLK2, 0);
}

#endif // ENABLE_SI5351
