// ============================================================
// Waveforms.h — Waveform catalogue and engine interface
//
// (c) 2026 PA1DVB
// ============================================================
#pragma once

#include <Arduino.h>
#include "Features.h"

enum WaveformId : uint8_t {
    // --- Standard ---
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_SAW_UP,
    WAVE_SAW_DOWN,
    WAVE_PULSE,
    WAVE_DC,
    WAVE_NOISE_WHITE,

    // --- Advanced / signal-analysis ---
    WAVE_TWO_TONE,
    WAVE_AM,
    WAVE_FM,
    WAVE_CHIRP,
    WAVE_BURST,
    WAVE_STAIRCASE,
    WAVE_GAUSSIAN,
    WAVE_SINC,
    WAVE_ECG,
    WAVE_HEARTBEAT,

    // --- X/Y scope patterns (DAC1=X, DAC2=Y) ---
    WAVE_XY_CIRCLE,
    WAVE_XY_LISSAJOUS_1_2,
    WAVE_XY_LISSAJOUS_2_3,
    WAVE_XY_LISSAJOUS_3_4,
    WAVE_XY_LISSAJOUS_3_5,
    WAVE_XY_SPIRAL,
    WAVE_XY_ROSE,
    WAVE_XY_STAR,
    WAVE_XY_HEART,
    WAVE_XY_BUTTERFLY,
    WAVE_XY_INFINITY,

    // --- Vector / image (mesh data in Vectors.ino) ---
    WAVE_VEC_DOMOTICZ,
    WAVE_VEC_CALLSIGN,
    WAVE_VEC_HEART,
    WAVE_VEC_STAR,
    WAVE_VEC_HOUSE,

    // --- Clock / RF square wave on GPIO27 (LEDC, 1 kHz .. 40 MHz) ---
    WAVE_CLK_PROBE_CAL,   // 1 kHz — scope probe compensation
    WAVE_CLK_1MHZ,
    WAVE_CLK_10MHZ,
    WAVE_CLK_FM_BCAST,    // 30 MHz → 3rd harmonic = 90 MHz (broadcast FM)
    WAVE_CLK_2M_HAM,      // 29 MHz → 5th harmonic = 145 MHz (2 m amateur)
    WAVE_CLK_VARIABLE,    // user picks frequency via the Frequency knob

#if ENABLE_SI5351
    // --- Si5351A RF clock generator on CLK0 (only when ENABLE_SI5351=1) ---
    WAVE_SI_10MHZ_REF,    // 10 MHz — high-accuracy reference / calibration tone
    WAVE_SI_WSPR_40M,     // 7.040100 MHz — WSPR centre on 40 m
    WAVE_SI_WSPR_20M,     // 14.097100 MHz — WSPR centre on 20 m
    WAVE_SI_FM_BCAST,     // 90.000 MHz — FM broadcast band centre
    WAVE_SI_2M_HAM,       // 145.500 MHz — 2 m calling
    WAVE_SI_70CM_HAM,     // 435.000 MHz — 70 cm calling
    WAVE_SI_VARIABLE,     // user picks frequency via the Frequency knob (2.5 kHz – 200 MHz)
#endif

    WAVE_COUNT
};

struct WaveformInfo {
    const char* id;       // url / json id
    const char* label;    // ui label
    const char* group;    // grouping label for <optgroup>
    bool        isXY;     // true → parametric, uses both channels independently
};

extern const WaveformInfo kWaveforms[WAVE_COUNT];

// Engine API
void waveformTaskStart();
void waveformApplyConfig();   // re-read Config:: values into the engine
const char* waveformIdToStr(uint8_t id);
int  waveformStrToId(const char* s);  // returns -1 if unknown
