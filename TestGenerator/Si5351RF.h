// ============================================================
// Si5351RF.h — Optional Si5351A clock-generator support
//
// Wraps the Etherkit Si5351 library to expose a small API used by
// the waveform engine. All symbols are stripped at compile time
// when ENABLE_SI5351 == 0, so the sketch stays buildable without
// the library installed.
//
// Wiring (I²C on default ESP32 pins):
//   Si5351 VIN ──► ESP32 3V3
//   Si5351 GND ──► ESP32 GND
//   Si5351 SDA ──► ESP32 GPIO21
//   Si5351 SCL ──► ESP32 GPIO22
//   Si5351 CLK0/1/2 ──► SMA/coax to receiver / antenna
//
// (c) 2026 PA1DVB
// ============================================================
#pragma once
#include "Features.h"

#if ENABLE_SI5351
#include <si5351.h>

void     si5351Setup();                            // call once from setup()
bool     si5351Available();                        // true if the chip ACKed
void     si5351SetFreq(uint64_t freqHz, uint8_t channel = 0);  // 0..2 → CLK0..CLK2
void     si5351DisableAll();                       // mute all three outputs
#endif
