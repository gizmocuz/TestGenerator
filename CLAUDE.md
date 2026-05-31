# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build / flash

Arduino IDE 2.x with the **esp32** board package (Espressif). Board: *ESP32 Dev Module* (or *ESP32-WROOM-DA Module*). Partition scheme must include SPIFFS (e.g. "Default 4MB with spiffs"). Open `TestGenerator/TestGenerator.ino` and Upload. After the first serial upload, subsequent flashes can go over WiFi: the device advertises mDNS hostname `TESTGEN-<mac>` with OTA password equal to the hostname.

Required libraries: `WiFiManager` (tzapu) and `ArduinoJson`. Everything else (`WiFi`, `WebServer`, `ArduinoOTA`, `SPIFFS`, `driver/dac`) ships with the ESP32 core.

There is no automated test suite — verification is by scoping the DAC outputs (GPIO25/26) while exercising the web UI.

## Architecture

A real-time signal generator. Output timing is owned by hardware (I²S DMA), not the CPU — this is the single most important property to preserve when modifying the engine.

**Output path — I²S DMA** (`Waveforms.ino`, `i2sBegin`): the legacy `driver/i2s.h` driver runs in `I2S_MODE_DAC_BUILT_IN`, configured for stereo 16-bit at 100 kSPS. Only the upper byte of each 16-bit word reaches the DAC. A ring of 8 × 256-frame DMA buffers (~20 ms) clocks samples to both built-in DACs (GPIO25/26) from hardware, immune to CPU/WiFi jitter. **Do not switch to `dacWrite()` or direct register pokes** — that was the previous design and produced visible distortion on every waveform because WiFi interrupts jittered the sample period. The legacy driver is the only path that supports the built-in DAC; the new `i2s_std` API in arduino-esp32 3.x does not.

Channel mapping in DAC mode: right (`buf[2i+0]`) → DAC1 / GPIO25, left (`buf[2i+1]`) → DAC2 / GPIO26. If a future SDK revision swaps these, just flip the two indices in `waveformTask`.

**Filler task** (`waveformTask`): pinned to core 1 at low priority, generates a chunk of frames and hands them to `i2s_write()` which blocks on the DMA ring. No busy-waiting — the task is paced exactly by the sample rate via DMA backpressure. WiFi can never starve it; conversely it can never starve WiFi.

**Core 1 — Arduino loop**: WiFiManager captive portal, `WebServer`, `ArduinoOTA`. The web/API handlers mutate a set of `volatile` engine globals (`g_wave`, `g_phaseInc`, `g_amp`, etc.) declared in `Waveforms.ino`. Single-writer (web) / single-reader (filler task) — brief torn reads mid-update are harmless for a bench test generator. **Do not introduce locking** between the two; the filler task should not block on anything other than DMA.

Web handlers never touch the engine globals directly — they update `Config::` (the persisted source of truth), then call `waveformApplyConfig()`, which translates user-facing values (Hz, %, etc.) into the internal phase increments / 0..1 floats the task expects. Then `Config::save()` to SPIFFS. This is the contract — any new parameter follows the same flow.

**Adding a waveform**: append an enum to `WaveformId` in `Waveforms.h`, add a matching `kWaveforms[]` entry (id / label / group / `isXY` flag) in `Waveforms.ino`, add a `case` to `generateSample()`. For mono waveforms set `s1` and the engine fills DAC2 with the inverted signal. For X/Y patterns set `xy = true` and assign both `s1`/`s2`. Sample values are signed -1..+1; `pack()` applies amplitude/offset and clamps to 0..255.

**Adding a vector / image**: define a new `VectorMesh` in `Vectors.ino` — a flat `int8_t[]` of x,y vertex pairs in the -100..+100 range and a `uint16_t[]` of from/to index pairs. Declare it `extern` in `Vectors.h`. Append a `WAVE_VEC_*` enum, a `kWaveforms[]` entry (group `"Vector / Image"`, `isXY = true`), a `case` in `generateSample()` that calls `renderVectorSample()`, and the mesh-pointer selection in `waveformApplyConfig()`. The renderer walks edges; for vector waves the user-facing **Frequency** knob means *pattern refresh rate (Hz)*, not a tone frequency. Useful range is ~20–200 Hz. Vertices/edges are stored `PROGMEM`-tagged (harmless no-op on ESP32 but kept for ESP8266 compatibility) — read with `pgm_read_byte`/`pgm_read_word`.

**Flyback bit (0x8000)**: an edge's `to` index can have the 0x8000 bit set to mark it as a *flyback* / pen-up move. The renderer (`renderVectorSample()` in `Waveforms.ino`) traverses such edges `VEC_FLYBACK_BOOST` (currently 32) times faster, so on the scope they appear dim relative to the actual letter / shape strokes. This is essential for any mesh that needs to "lift the pen" — chains of letters, multiple separate shapes — otherwise the connector lines are as bright as the real strokes and the image reads as noise. Closed continuous polylines (like the Domoticz outer circle or the D outline) don't need flybacks.

**Adding a parameter**: add a field to `Config::` (with default + save/load lines), expose it through the web form in `WebServer.ino` (both the HTML render in `handleRoot` and the `webServer.hasArg(...)` chain in `handleSet`), surface it in `/api/state` GET/POST, then consume it in `waveformApplyConfig()` and the relevant `generateSample()` case.

## Hardware notes

- DACs are 8-bit, so dynamic range is fixed; amplitude/offset just scale within 0..255.
- **Two output paths run in parallel, plus a third optional one.** GPIO25/26 = DACs (analog waveforms via I²S DMA). GPIO27 = LEDC square wave (1 kHz – 40 MHz, used for scope probe cal and odd-harmonic RF — 90 MHz from a 30 MHz fundamental, 145 MHz from 29 MHz × 5). I²C on GPIO21/22 = Si5351A RF clock generator (2.5 kHz – 200 MHz, three channels) — *compiled out by default*. Toggle via `ENABLE_SI5351` in `Features.h`; flag-guarded throughout so the sketch builds cleanly without the Etherkit Si5351 library installed. When a Clock waveform is active, the DACs sit at mid-scale (`generateSample` returns s1=s2=0) and `clockOutEnable()` is called from `waveformApplyConfig`. Picking any non-CLK waveform calls `clockOutDisable()` and the DACs resume. The two are independent peripherals so they don't interfere.
- Sample rate is 100 kSPS, default I²S clock (PLL_D2 ÷1600 = exact). We tried APLL at 48 kSPS to try to fix flicker but it was worse: APLL on the classic ESP32 fights the WiFi PLL under load. The flicker turned out to be DMA underruns from HTTP-request bursts on a too-small ring; fixed by 16×512-frame ring (~80 ms slack at 100 kSPS) and bumping the filler task priority to 10. **Don't shrink the DMA ring** without testing under sustained web-UI traffic.
- DAC1 = GPIO25, DAC2 = GPIO26 — these are fixed in silicon, do not parameterise.
- For mono waveforms DAC2 carries the inverted signal — useful for differential probing. This is set in `generateSample()`, not configurable from the UI.
