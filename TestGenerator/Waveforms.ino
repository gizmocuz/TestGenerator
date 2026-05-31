// ============================================================
// Waveforms.ino — DDS waveform engine, output via I²S DMA → built-in DACs
//
// The I²S peripheral clocks samples out of a DMA ring at an exact rate
// set by its master clock — completely immune to CPU/WiFi jitter, which
// is what was causing visible distortion when we busy-waited on micros().
//
// A FreeRTOS task generates samples into a small staging buffer and
// hands them to i2s_write(); the call blocks when the DMA ring is full,
// so the task is naturally paced by the hardware.
//
// (c) 2026 PA1DVB
// ============================================================

#include <Arduino.h>
#include <math.h>
#include "driver/i2s.h"   // legacy I²S driver — still the only path that
                          // supports the built-in DAC on classic ESP32.

#include "Config.h"
#include "Waveforms.h"
#include "Vectors.h"
#include "ClockOut.h"
#include "Si5351RF.h"

// ---- Sample rate ----
// 100 kSPS: 50 kHz Nyquist, divides 160 MHz PLL_D2 exactly (÷1600).
static constexpr uint32_t SAMPLE_RATE_HZ = 100000;

// DMA ring: 16 buffers × 512 frames ≈ 80 ms of slack at 100 kSPS.
// A DMA underrun is what makes the trace flicker/glitch on the scope —
// the I²S peripheral inserts a buffer-of-silence at the gap, which
// looks like a brief phase reset. WiFi bursts can stall the filler
// task for tens of ms, so size the ring well above that.
static constexpr i2s_port_t I2S_PORT     = I2S_NUM_0;
static constexpr int        DMA_BUF_LEN  = 512;   // frames per buffer
static constexpr int        DMA_BUF_CNT  = 16;

// ---- Waveform catalogue ----
const WaveformInfo kWaveforms[WAVE_COUNT] = {
    { "sine",         "Sine",                "Standard", false },
    { "square",       "Square",              "Standard", false },
    { "triangle",     "Triangle",            "Standard", false },
    { "saw_up",       "Sawtooth (rising)",   "Standard", false },
    { "saw_down",     "Sawtooth (falling)",  "Standard", false },
    { "pulse",        "Pulse (variable duty)","Standard", false },
    { "dc",           "DC level",            "Standard", false },
    { "noise_white",  "White Noise",         "Standard", false },

    { "two_tone",     "Two-Tone (sum of sines)", "Advanced", false },
    { "am",           "AM (sine on sine)",       "Advanced", false },
    { "fm",           "FM (sine on sine)",       "Advanced", false },
    { "chirp",        "Chirp / Sweep",           "Advanced", false },
    { "burst",        "Sine Burst",              "Advanced", false },
    { "staircase",    "Staircase",               "Advanced", false },
    { "gaussian",     "Gaussian Pulse",          "Advanced", false },
    { "sinc",         "Sinc",                    "Advanced", false },
    { "ecg",          "ECG (cardiac)",           "Advanced", false },
    { "heartbeat",    "Heartbeat",               "Advanced", false },

    { "xy_circle",    "Circle",                  "X/Y Patterns", true },
    { "xy_liss_1_2",  "Lissajous 1:2",           "X/Y Patterns", true },
    { "xy_liss_2_3",  "Lissajous 2:3",           "X/Y Patterns", true },
    { "xy_liss_3_4",  "Lissajous 3:4",           "X/Y Patterns", true },
    { "xy_liss_3_5",  "Lissajous 3:5",           "X/Y Patterns", true },
    { "xy_spiral",    "Spiral",                  "X/Y Patterns", true },
    { "xy_rose",      "Rose Curve",              "X/Y Patterns", true },
    { "xy_star",      "5-Point Star",            "X/Y Patterns", true },
    { "xy_heart",     "Heart",                   "X/Y Patterns", true },
    { "xy_butterfly", "Butterfly",               "X/Y Patterns", true },
    { "xy_infinity",  "Infinity (Lemniscate)",   "X/Y Patterns", true },

    { "vec_domoticz", "Domoticz logo",           "Vector / Image", true },
    { "vec_callsign", "PA1DVB callsign",         "Vector / Image", true },
    { "vec_heart",    "Heart",                   "Vector / Image", true },
    { "vec_star",     "Star",                    "Vector / Image", true },
    { "vec_house",    "House",                   "Vector / Image", true },

    { "clk_probe_cal","1 kHz Probe Cal",         "Clock / RF (GPIO27)", false },
    { "clk_1m",       "1 MHz",                   "Clock / RF (GPIO27)", false },
    { "clk_10m",      "10 MHz",                  "Clock / RF (GPIO27)", false },
    { "clk_fm",       "FM broadcast (30 MHz → 3rd harm ≈ 90 MHz)", "Clock / RF (GPIO27)", false },
    { "clk_2m",       "2 m amateur (29 MHz → 5th harm ≈ 145 MHz)", "Clock / RF (GPIO27)", false },
    { "clk_var",      "Variable (1 kHz – 40 MHz, Frequency knob)", "Clock / RF (GPIO27)", false },

#if ENABLE_SI5351
    { "si_10m_ref",   "10 MHz reference / calibration tone", "Si5351 RF (CLK0)", false },
    { "si_wspr_40m",  "WSPR 40 m (7.0401 MHz)",              "Si5351 RF (CLK0)", false },
    { "si_wspr_20m",  "WSPR 20 m (14.0971 MHz)",             "Si5351 RF (CLK0)", false },
    { "si_fm",        "FM broadcast (90.000 MHz)",           "Si5351 RF (CLK0)", false },
    { "si_2m",        "2 m amateur (145.500 MHz)",           "Si5351 RF (CLK0)", false },
    { "si_70cm",      "70 cm amateur (435.000 MHz)",         "Si5351 RF (CLK0)", false },
    { "si_var",       "Variable (Frequency knob, Hz)",       "Si5351 RF (CLK0)", false },
#endif
};

const char* waveformIdToStr(uint8_t id) {
    if (id >= WAVE_COUNT) return "sine";
    return kWaveforms[id].id;
}

int waveformStrToId(const char* s) {
    if (!s) return -1;
    for (int i = 0; i < WAVE_COUNT; ++i) {
        if (strcmp(s, kWaveforms[i].id) == 0) return i;
    }
    return -1;
}

// ---- Engine state (written from web handlers, read from task) ----
// volatile is sufficient — single writer (web) / single reader (task).
static volatile uint8_t  g_wave       = WAVE_SINE;
static volatile uint32_t g_phaseInc   = 0;
static volatile uint32_t g_modInc     = 0;
static volatile float    g_amp        = 1.0f;
static volatile float    g_offset     = 0.0f;
static volatile float    g_duty       = 0.5f;
static volatile float    g_modDepth   = 0.5f;
static volatile float    g_sweepLow   = 100.0f;
static volatile float    g_sweepHigh  = 5000.0f;
static volatile float    g_sweepTime  = 1.0f;
static volatile bool     g_running    = true;

static uint32_t g_phase      = 0;
static uint32_t g_modPhase   = 0;
static uint32_t g_chirpPhase = 0;
static uint32_t g_fmPhase    = 0;   // dedicated accumulator for FM carrier

// ---- Vector renderer state ----
// The current mesh, and a 16-bit "position along current edge" that
// advances by g_vecStepInc per sample. When it wraps, move to the
// next edge; when the edge index wraps, restart the pattern.
static const VectorMesh* g_vecMesh    = nullptr;
static uint16_t          g_vecEdgeIdx = 0;
static uint32_t          g_vecStep    = 0;
static uint32_t          g_vecStepInc = 0x10000 / 32;  // 32 samples per edge default

// ---- 256-entry sine LUT (signed -1..+1 as int8) ----
static int8_t g_sineLUT[256];

static void buildLUT() {
    for (int i = 0; i < 256; ++i) {
        float a = (float)i * (2.0f * (float)M_PI / 256.0f);
        g_sineLUT[i] = (int8_t)roundf(sinf(a) * 127.0f);
    }
}

static inline uint8_t pack(float s) {
    float v = s * g_amp + g_offset;
    int code = (int)lroundf(v * 127.0f + 128.0f);
    if (code < 0)   code = 0;
    if (code > 255) code = 255;
    return (uint8_t)code;
}

static inline float sinLUT(uint32_t phase) {
    return (float)g_sineLUT[phase >> 24] * (1.0f / 127.0f);
}
static inline float cosLUT(uint32_t phase) {
    return (float)g_sineLUT[(uint8_t)((phase >> 24) + 64)] * (1.0f / 127.0f);
}

static inline uint32_t xorshift32() {
    static uint32_t s = 0xC0FFEE42u;
    s ^= s << 13;  s ^= s >> 17;  s ^= s << 5;
    return s;
}

// Flyback edges (marked with 0x8000 on the `to` index) traverse this
// many times faster than normal edges → spend that much less time at
// each point → look dim on a phosphor or persistence scope. 32× is
// enough that the connectors between letters are barely visible while
// the actual strokes stay bright.
static constexpr uint32_t VEC_FLYBACK_BOOST = 32;

// Render one sample along the current vector mesh edge, advance state.
static inline void renderVectorSample(float& s1, float& s2) {
    if (!g_vecMesh || g_vecMesh->edgeCount == 0) {
        s1 = 0.0f; s2 = 0.0f;
        return;
    }
    const uint16_t e = g_vecEdgeIdx * 2;
    const uint16_t aIdx  = pgm_read_word(&g_vecMesh->edges[e + 0]);
    const uint16_t toRaw = pgm_read_word(&g_vecMesh->edges[e + 1]);
    const bool     fly   = (toRaw & 0x8000) != 0;
    const uint16_t bIdx  = toRaw & 0x7FFF;
    const int8_t ax = (int8_t)pgm_read_byte(&g_vecMesh->vertices[aIdx * 2 + 0]);
    const int8_t ay = (int8_t)pgm_read_byte(&g_vecMesh->vertices[aIdx * 2 + 1]);
    const int8_t bx = (int8_t)pgm_read_byte(&g_vecMesh->vertices[bIdx * 2 + 0]);
    const int8_t by = (int8_t)pgm_read_byte(&g_vecMesh->vertices[bIdx * 2 + 1]);

    // Interpolate using the high 16 bits of step (0..65535 → 0..1).
    const float t = (float)(g_vecStep & 0xFFFF) * (1.0f / 65536.0f);
    s1 = ((float)ax + ((float)bx - (float)ax) * t) * 0.01f; // → -1..+1
    s2 = ((float)ay + ((float)by - (float)ay) * t) * 0.01f;

    const uint32_t inc = fly ? (g_vecStepInc * VEC_FLYBACK_BOOST) : g_vecStepInc;
    g_vecStep += inc;
    if (g_vecStep >= 0x10000) {
        g_vecStep = 0;
        if (++g_vecEdgeIdx >= g_vecMesh->edgeCount) g_vecEdgeIdx = 0;
    }
}

// Per-sample generator — unchanged from the busy-wait version.
static inline void generateSample(uint8_t& out1, uint8_t& out2) {
    g_phase    += g_phaseInc;
    g_modPhase += g_modInc;

    const uint8_t wave = g_wave;
    float s1 = 0.0f, s2 = 0.0f;
    bool  xy = false;

    switch (wave) {
    case WAVE_SINE:      s1 = sinLUT(g_phase); break;
    case WAVE_SQUARE:    s1 = (g_phase < 0x80000000u) ? 1.0f : -1.0f; break;
    case WAVE_TRIANGLE: {
        float f = (float)g_phase * (1.0f / 4294967296.0f);
        s1 = (f < 0.5f) ? (4.0f * f - 1.0f) : (3.0f - 4.0f * f);
        break;
    }
    case WAVE_SAW_UP:   s1 = 2.0f * (float)g_phase * (1.0f / 4294967296.0f) - 1.0f; break;
    case WAVE_SAW_DOWN: s1 = 1.0f - 2.0f * (float)g_phase * (1.0f / 4294967296.0f); break;
    case WAVE_PULSE: {
        uint32_t threshold = (uint32_t)(g_duty * 4294967296.0f);
        s1 = (g_phase < threshold) ? 1.0f : -1.0f;
        break;
    }
    case WAVE_DC: s1 = 0.0f; break;
    case WAVE_NOISE_WHITE: {
        uint32_t r = xorshift32();
        s1 = ((int32_t)r) * (1.0f / 2147483648.0f);
        break;
    }
    case WAVE_TWO_TONE:
        s1 = 0.5f * (sinLUT(g_phase) + sinLUT(g_modPhase));
        break;
    case WAVE_AM: {
        float m = 1.0f + g_modDepth * sinLUT(g_modPhase);
        s1 = sinLUT(g_phase) * 0.5f * m;
        break;
    }
    case WAVE_FM: {
        // True FM: modulate the *frequency* (= phase increment), not the
        // phase itself. Carrier swings between (1-depth)·f and (1+depth)·f.
        //
        // Cast to int32_t first — going straight from a negative float to
        // uint32_t is undefined behaviour in C. int32_t→uint32_t conversion
        // is well-defined (two's-complement wrap), and that wrap gives the
        // correct modular arithmetic when we add it to g_phaseInc below.
        float    mod       = sinLUT(g_modPhase) * g_modDepth;     // -depth..+depth
        int32_t  inc_delta = (int32_t)((float)g_phaseInc * mod);
        g_fmPhase += g_phaseInc + (uint32_t)inc_delta;
        s1 = sinLUT(g_fmPhase);
        break;
    }
    case WAVE_CHIRP: {
        float pos  = (float)g_modPhase * (1.0f / 4294967296.0f);
        float freq = g_sweepLow + (g_sweepHigh - g_sweepLow) * pos;
        uint32_t inc = (uint32_t)((double)freq * 4294967296.0 / SAMPLE_RATE_HZ);
        g_chirpPhase += inc;
        s1 = sinLUT(g_chirpPhase);
        break;
    }
    case WAVE_BURST: {
        bool on = (g_modPhase < (uint32_t)(g_duty * 4294967296.0f));
        s1 = on ? sinLUT(g_phase) : 0.0f;
        break;
    }
    case WAVE_STAIRCASE: {
        uint8_t step = (uint8_t)(g_phase >> 29);
        s1 = ((float)step / 3.5f) - 1.0f;
        break;
    }
    case WAVE_GAUSSIAN: {
        float t = (float)g_phase * (1.0f / 4294967296.0f) - 0.5f;
        s1 = 2.0f * expf(-50.0f * t * t) - 1.0f;
        break;
    }
    case WAVE_SINC: {
        float t = ((float)g_phase * (1.0f / 4294967296.0f) - 0.5f) * 20.0f;
        s1 = (fabsf(t) < 1e-4f) ? 1.0f : (sinf(t) / t);
        break;
    }
    case WAVE_ECG: {
        float t = (float)g_phase * (1.0f / 4294967296.0f);
        float p  =  0.10f * expf(-powf((t - 0.20f) * 30.0f, 2.0f));
        float q  = -0.15f * expf(-powf((t - 0.45f) * 80.0f, 2.0f));
        float r  =  1.00f * expf(-powf((t - 0.50f) * 90.0f, 2.0f));
        float ss = -0.25f * expf(-powf((t - 0.55f) * 80.0f, 2.0f));
        float tt =  0.25f * expf(-powf((t - 0.75f) * 25.0f, 2.0f));
        s1 = p + q + r + ss + tt;
        break;
    }
    case WAVE_HEARTBEAT: {
        float t = (float)g_phase * (1.0f / 4294967296.0f);
        float a =        expf(-powf((t - 0.15f) * 40.0f, 2.0f));
        float b = 0.7f * expf(-powf((t - 0.35f) * 40.0f, 2.0f));
        s1 = (a + b) * 1.2f - 0.5f;
        break;
    }
    case WAVE_XY_CIRCLE:        xy = true; s1 = sinLUT(g_phase);     s2 = cosLUT(g_phase); break;
    case WAVE_XY_LISSAJOUS_1_2: xy = true; s1 = sinLUT(g_phase);     s2 = sinLUT(g_phase * 2); break;
    case WAVE_XY_LISSAJOUS_2_3: xy = true; s1 = sinLUT(g_phase * 2); s2 = sinLUT(g_phase * 3); break;
    case WAVE_XY_LISSAJOUS_3_4: xy = true; s1 = sinLUT(g_phase * 3); s2 = sinLUT(g_phase * 4); break;
    case WAVE_XY_LISSAJOUS_3_5: xy = true; s1 = sinLUT(g_phase * 3); s2 = sinLUT(g_phase * 5); break;
    case WAVE_XY_SPIRAL: {
        xy = true;
        float r = (float)g_modPhase * (1.0f / 4294967296.0f);
        s1 = r * sinLUT(g_phase);
        s2 = r * cosLUT(g_phase);
        break;
    }
    case WAVE_XY_ROSE: {
        xy = true;
        float th = (float)g_phase * (2.0f * (float)M_PI / 4294967296.0f);
        float r  = cosf(4.0f * th);
        s1 = r * cosf(th);  s2 = r * sinf(th);
        break;
    }
    case WAVE_XY_STAR: {
        xy = true;
        float th = (float)g_phase * (4.0f * (float)M_PI / 4294967296.0f);
        float r  = 0.5f + 0.5f * cosf(5.0f * th * 0.5f);
        s1 = r * cosf(th);  s2 = r * sinf(th);
        break;
    }
    case WAVE_XY_HEART: {
        xy = true;
        float t = (float)g_phase * (2.0f * (float)M_PI / 4294967296.0f);
        float st = sinf(t);
        float x  = 16.0f * st * st * st;
        float y  = 13.0f * cosf(t) - 5.0f * cosf(2*t) - 2.0f * cosf(3*t) - cosf(4*t);
        s1 = x * (1.0f / 17.0f);  s2 = y * (1.0f / 17.0f);
        break;
    }
    case WAVE_XY_BUTTERFLY: {
        xy = true;
        float t = (float)g_phase * (12.0f * (float)M_PI / 4294967296.0f);
        float e = expf(cosf(t)) - 2.0f * cosf(4*t) - powf(sinf(t / 12.0f), 5.0f);
        s1 = sinf(t) * e * 0.25f;  s2 = cosf(t) * e * 0.25f;
        if (s1 >  1.0f) s1 =  1.0f;  if (s1 < -1.0f) s1 = -1.0f;
        if (s2 >  1.0f) s2 =  1.0f;  if (s2 < -1.0f) s2 = -1.0f;
        break;
    }
    case WAVE_XY_INFINITY: {
        xy = true;
        float t = (float)g_phase * (2.0f * (float)M_PI / 4294967296.0f);
        float d = 1.0f + sinf(t) * sinf(t);
        s1 = cosf(t) / d;  s2 = sinf(t) * cosf(t) / d;
        break;
    }

    // ---------- Vector / image meshes ----------
    case WAVE_VEC_DOMOTICZ:
    case WAVE_VEC_CALLSIGN:
    case WAVE_VEC_HEART:
    case WAVE_VEC_STAR:
    case WAVE_VEC_HOUSE:
        xy = true;
        renderVectorSample(s1, s2);
        break;

    // ---------- Clock / RF: DACs idle, LEDC runs on GPIO27 ----------
    case WAVE_CLK_PROBE_CAL:
    case WAVE_CLK_1MHZ:
    case WAVE_CLK_10MHZ:
    case WAVE_CLK_FM_BCAST:
    case WAVE_CLK_2M_HAM:
    case WAVE_CLK_VARIABLE:
        xy = true;          // emit s1=s2=0 so both DACs sit at mid-scale
        s1 = 0.0f; s2 = 0.0f;
        break;

#if ENABLE_SI5351
    // ---------- Si5351 RF: DACs idle, output is on the breakout's SMA ----------
    case WAVE_SI_10MHZ_REF:
    case WAVE_SI_WSPR_40M:
    case WAVE_SI_WSPR_20M:
    case WAVE_SI_FM_BCAST:
    case WAVE_SI_2M_HAM:
    case WAVE_SI_70CM_HAM:
    case WAVE_SI_VARIABLE:
        xy = true;
        s1 = 0.0f; s2 = 0.0f;
        break;
#endif

    default: s1 = sinLUT(g_phase); break;
    }

    if (xy) {
        out1 = pack(s1);
        out2 = pack(s2);
    } else {
        out1 = pack(s1);
        out2 = pack(-s1);
    }
}

// ---- I²S setup (built-in DAC mode) ----
static void i2sBegin() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate          = SAMPLE_RATE_HZ;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);
    cfg.intr_alloc_flags     = 0;
    cfg.dma_buf_count        = DMA_BUF_CNT;
    cfg.dma_buf_len          = DMA_BUF_LEN;
    // APLL is supposed to give a drift-free clock, but on the classic
    // ESP32 it conflicts with the WiFi PLL under load and produces
    // worse glitches than the default PLL_D2 path. 100 kSPS divides
    // 160 MHz PLL_D2 exactly (÷1600) so the default clock is precise.
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;

    i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
    // Enable both built-in DACs: right channel → DAC1 (GPIO25),
    // left channel → DAC2 (GPIO26).
    i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    i2s_zero_dma_buffer(I2S_PORT);
}

// ---- Sample-feeder task ----
// Generates a chunk of stereo frames and pushes them to the DMA ring.
// i2s_write blocks when the ring is full, so the task is paced exactly
// by the sample rate — no busy-waiting, no jitter.
static void waveformTask(void*) {
    constexpr int CHUNK = 256;
    static uint16_t buf[CHUNK * 2]; // stereo: 2× 16-bit per frame

    while (true) {
        if (!g_running) {
            // Fill with mid-scale to silence the output. Both 16-bit halves
            // carry 0x80 in their upper byte → DAC outputs ~1.65 V.
            for (int i = 0; i < CHUNK; ++i) {
                buf[2*i + 0] = 0x8000;
                buf[2*i + 1] = 0x8000;
            }
        } else {
            for (int i = 0; i < CHUNK; ++i) {
                uint8_t v1, v2;
                generateSample(v1, v2);
                // I²S DAC mode: only the upper byte of each 16-bit word
                // reaches the DAC. Channel mapping (verified empirically):
                //   buf[2i+0] (right) → DAC1 / GPIO25
                //   buf[2i+1] (left)  → DAC2 / GPIO26
                // If your channels come out swapped on the scope, just
                // flip the two indices below.
                buf[2*i + 0] = (uint16_t)v1 << 8;
                buf[2*i + 1] = (uint16_t)v2 << 8;
            }
        }
        size_t written = 0;
        i2s_write(I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
    }
}

void waveformTaskStart() {
    buildLUT();
    i2sBegin();
    // High priority on core 1 — i2s_write() blocks on the DMA ring so
    // this can't starve anything. Core 0 is left to WiFi entirely.
    // We need priority above the loopTask (1) so HTTP request bursts
    // can't delay buffer refills enough to cause an underrun.
    xTaskCreatePinnedToCore(waveformTask, "waveform", 4096, nullptr,
                            10, nullptr, 1);
}

void waveformApplyConfig() {
    g_wave      = (uint8_t)constrain(Config::wave_id, 0, (int)WAVE_COUNT - 1);
    g_amp       = constrain(Config::amplitude, 0.0f, 100.0f) * 0.01f;
    g_offset    = constrain(Config::offset, -100.0f, 100.0f) * 0.01f;
    g_duty      = constrain(Config::duty, 0.1f, 99.9f) * 0.01f;
    g_modDepth  = constrain(Config::mod_depth, 0.0f, 100.0f) * 0.01f;
    g_sweepLow  = max(1.0f, Config::sweep_low);
    g_sweepHigh = max(g_sweepLow + 1.0f, Config::sweep_high);
    g_sweepTime = max(0.05f, Config::sweep_time);
    g_running   = Config::running;

    float f = max(0.01f, Config::frequency);
    g_phaseInc = (uint32_t)((double)f * 4294967296.0 / SAMPLE_RATE_HZ);

    float mf;
    if (g_wave == WAVE_CHIRP)          mf = 1.0f / g_sweepTime;
    else if (g_wave == WAVE_TWO_TONE)  mf = f * 1.25f;
    else                               mf = max(0.01f, Config::mod_freq);
    g_modInc = (uint32_t)((double)mf * 4294967296.0 / SAMPLE_RATE_HZ);

    // ---- Vector mesh selection ----
    // For vector waveforms, "frequency" is reinterpreted as the pattern
    // refresh rate in Hz (typical useful range 20–200 Hz). Anything much
    // below 20 Hz visibly flickers on a scope; above ~200 the trace gets
    // dim because each edge is drawn with too few samples.
    const VectorMesh* mesh = nullptr;
    switch (g_wave) {
        case WAVE_VEC_DOMOTICZ: mesh = &kMeshDomoticz; break;
        case WAVE_VEC_CALLSIGN: mesh = &kMeshCallsign; break;
        case WAVE_VEC_HEART:    mesh = &kMeshHeart;    break;
        case WAVE_VEC_STAR:     mesh = &kMeshStar;     break;
        case WAVE_VEC_HOUSE:    mesh = &kMeshHouse;    break;
        default: break;
    }
    if (mesh != g_vecMesh) {
        g_vecMesh    = mesh;
        g_vecEdgeIdx = 0;
        g_vecStep    = 0;
    }
    if (mesh) {
        // refresh × edges × 65536 / sample_rate = step increment per sample
        float refresh = constrain(f, 5.0f, 500.0f);
        float inc = refresh * (float)mesh->edgeCount * 65536.0f / (float)SAMPLE_RATE_HZ;
        if (inc < 1.0f) inc = 1.0f;
        g_vecStepInc = (uint32_t)inc;
    }

    // ---- Clock / RF: enable LEDC on GPIO27, or disable it for any non-CLK wave ----
    uint32_t clkHz = 0;
    switch (g_wave) {
        case WAVE_CLK_PROBE_CAL: clkHz = 1000;       break;
        case WAVE_CLK_1MHZ:      clkHz = 1000000;    break;
        case WAVE_CLK_10MHZ:     clkHz = 10000000;   break;
        case WAVE_CLK_FM_BCAST:  clkHz = 30000000;   break;  // 3rd harm @ 90 MHz
        case WAVE_CLK_2M_HAM:    clkHz = 29000000;   break;  // 5th harm @ 145 MHz
        case WAVE_CLK_VARIABLE:
            // For Variable, the Frequency knob is reinterpreted in Hz —
            // accept anything from 1 kHz up to the LEDC ceiling.
            clkHz = (uint32_t)constrain(Config::frequency, 1000.0f, 40000000.0f);
            break;
        default: break;
    }
    if (clkHz) clockOutEnable(clkHz);
    else       clockOutDisable();

#if ENABLE_SI5351
    // ---- Si5351 RF: dispatch the selected preset / variable frequency ----
    uint64_t siHz = 0;
    switch (g_wave) {
        case WAVE_SI_10MHZ_REF: siHz =    10000000ULL; break;
        case WAVE_SI_WSPR_40M:  siHz =     7040100ULL; break;
        case WAVE_SI_WSPR_20M:  siHz =    14097100ULL; break;
        case WAVE_SI_FM_BCAST:  siHz =    90000000ULL; break;
        case WAVE_SI_2M_HAM:    siHz =   145500000ULL; break;
        case WAVE_SI_70CM_HAM:  siHz =   435000000ULL; break;
        case WAVE_SI_VARIABLE:
            // 2.5 kHz – 200 MHz is the Si5351A's quoted range.
            siHz = (uint64_t)constrain(Config::frequency, 2500.0f, 200000000.0f);
            break;
        default: break;
    }
    if (siHz) si5351SetFreq(siHz, 0);
    else      si5351DisableAll();
#endif
}
