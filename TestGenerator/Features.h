// ============================================================
// Features.h — Compile-time feature flags
//
// Toggle optional hardware support here.  Setting a flag to 1
// pulls in the relevant code (and its library dependency); 0
// strips it out completely so the sketch still builds without
// the library installed.
//
// (c) 2026 PA1DVB
// ============================================================
#pragma once

// ---- Si5351A breakout (RF clock generator, 2.5 kHz – 200 MHz) -----------
// Set to 1 once you have:
//   1. The breakout wired to ESP32: VIN→3V3, GND→GND, SDA→GPIO21, SCL→GPIO22
//   2. The "Etherkit Si5351" library installed via the Arduino Library Manager
//
// When 0 (default), the Si5351 code is entirely excluded — the sketch
// compiles without the library and the "Si5351 RF" waveforms don't
// appear in the dropdown.
#ifndef ENABLE_SI5351
#define ENABLE_SI5351 0
#endif
