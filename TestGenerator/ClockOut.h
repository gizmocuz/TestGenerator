// ============================================================
// ClockOut.h — High-frequency square-wave output via LEDC
//
// Generates a clean 1 kHz – 40 MHz square wave on GPIO27 using the
// LEDC peripheral (Arduino-ESP32's PWM driver). The DAC engine on
// GPIO25/26 keeps running independently — picking a Clock waveform
// just parks the DACs at mid-scale and enables LEDC on GPIO27.
//
// Useful for:
//   • Scope probe compensation (1 kHz standard).
//   • Scope bandwidth / rise-time checks (10–40 MHz square edges).
//   • Tuning FM / ham receivers using odd harmonics: a 30 MHz square
//     has a 3rd harmonic at 90 MHz (broadcast FM), and a 29 MHz square
//     has a 5th at 145 MHz (2 m amateur band). A short wire on GPIO27
//     acts as a more-than-adequate antenna for nearby reception.
//
// (c) 2026 PA1DVB
// ============================================================
#pragma once
#include <Arduino.h>

#define CLOCK_OUT_PIN   27

void clockOutSetup();                   // call once from setup()
void clockOutEnable(uint32_t freqHz);   // 1 kHz .. 40 MHz; ignored otherwise
void clockOutDisable();                 // pin returns to driven-low

bool     clockOutActive();
uint32_t clockOutFrequency();
