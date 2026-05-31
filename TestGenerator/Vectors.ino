// ============================================================
// Vectors.ino — Mesh data for vector / image XY display
//
// Coordinate system: int8 in -100..+100. The renderer rescales to
// -1.0..+1.0 before feeding pack() so amplitude/offset knobs still
// behave the same as for periodic waveforms.
//
// (c) 2026 PA1DVB
// ============================================================

#include "Vectors.h"

// =============================================================================
// Domoticz logo:  outer circle  +  stylised "D"  +  central notch.
// =============================================================================
static const int8_t domoticz_v[] PROGMEM = {
    // --- outer circle: 24 points around radius 95 ---
     95,   0,    92,  25,    82,  47,    67,  67,    47,  82,    25,  92,
      0,  95,   -25,  92,   -47,  82,   -67,  67,   -82,  47,   -92,  25,
    -95,   0,   -92, -25,   -82, -47,   -67, -67,   -47, -82,   -25, -92,
      0, -95,    25, -92,    47, -82,    67, -67,    82, -47,    92, -25,
    // --- stylised D outline (9 verts) ---
    -35, -50,   -35,  50,   -10,  50,    15,  42,    35,  25,
     45,   0,    35, -25,    15, -42,   -10, -50,
    // --- inner notch (rectangle, 4 verts) ---
    -20, -10,    15, -10,    15,  10,   -20,  10,
};
// 24 + 9 + 4 = 37 vertices

static const uint16_t domoticz_e[] PROGMEM = {
    // outer circle: 24 edges, wrap last → first
     0, 1,   1, 2,   2, 3,   3, 4,   4, 5,   5, 6,
     6, 7,   7, 8,   8, 9,   9,10,  10,11,  11,12,
    12,13,  13,14,  14,15,  15,16,  16,17,  17,18,
    18,19,  19,20,  20,21,  21,22,  22,23,  23, 0,
    // D outline: 9 edges (closed: last → first)
    24,25,  25,26,  26,27,  27,28,  28,29,
    29,30,  30,31,  31,32,  32,24,
    // notch: 4 edges (closed)
    33,34,  34,35,  35,36,  36,33,
};
// 24 + 9 + 4 = 37 edges

const VectorMesh kMeshDomoticz = { domoticz_v, 37, domoticz_e, 37 };

// =============================================================================
// Heart (angular)
// =============================================================================
static const int8_t heart_v[] PROGMEM = {
      0,  55,    30,  80,    60,  60,    80,  20,    60, -20,
      0, -85,   -60, -20,   -80,  20,   -60,  60,   -30,  80,
};
static const uint16_t heart_e[] PROGMEM = {
    0,1, 1,2, 2,3, 3,4, 4,5, 5,6, 6,7, 7,8, 8,9, 9,0
};
const VectorMesh kMeshHeart = { heart_v, 10, heart_e, 10 };

// =============================================================================
// 5-point star
// =============================================================================
static const int8_t star_v[] PROGMEM = {
      0,  90,   -21,  29,   -86,  28,   -34, -11,   -53, -73,
      0, -35,    53, -73,    34, -11,    86,  28,    21,  29,
};
static const uint16_t star_e[] PROGMEM = {
    0,1, 1,2, 2,3, 3,4, 4,5, 5,6, 6,7, 7,8, 8,9, 9,0
};
const VectorMesh kMeshStar = { star_v, 10, star_e, 10 };

// =============================================================================
// House (walls + pitched roof + door)
// =============================================================================
static const int8_t house_v[] PROGMEM = {
    // outline
    -60, -60,    60, -60,    60,  30,     0,  75,   -60,  30,
    // door
    -12, -60,   -12, -15,    12, -15,    12, -60,
};
static const uint16_t house_e[] PROGMEM = {
    0,1, 1,2, 2,3, 3,4, 4,0,    // shell
    5,6, 6,7, 7,8,              // door (3 sides; 4th overlaps the floor)
};
const VectorMesh kMeshHouse = { house_v, 9, house_e, 8 };

// =============================================================================
// PA1DVB callsign
//
// Each letter is laid out in a 14×20 character cell, then mapped to scope
// coordinates via   scope_x = (char_x + x_offset) * 2 - 84
//                   scope_y =  char_y * 6 - 60
// giving a callsign that spans x ∈ [-84..+80], y ∈ [-60..+60].
//
// Letters are drawn as continuous polylines where possible, and short
// connector strokes between letters (and a couple inside complex
// letters like B) are tagged with the 0x8000 flyback bit so the
// renderer races over them and they appear dim. Without that trick the
// flyback lines are as bright as the actual strokes and the text reads
// as a chaotic blob.
//
// Letter offsets:  P=0  A=14  1=28  D=42  V=56  B=70
// =============================================================================

static const int8_t callsign_v[] PROGMEM = {
    // --- P ---
    -84, -60,   -84,  60,   -64,  60,   -60,  48,   -60,  12,
    -64,   0,   -84,   0,                                        // 0..6
    // --- A ---
    -56, -60,   -44,  60,   -32, -60,   -50, -18,   -38, -18,    // 7..11
    // --- 1 ---
    -20,  36,   -16,  60,   -16, -60,   -24, -60,   -8, -60,     // 12..16
    // --- D ---
      0, -60,    0,  60,    12,  60,    24,  24,    24, -24,
     12, -60,                                                    // 17..22
    // --- V ---
     28,  60,    40, -60,    52,  60,                            // 23..25
    // --- B ---
     56, -60,    56,  60,    72,  60,    80,  42,    80,  18,
     72,   0,    56,   0,    80, -18,    80, -42,    72, -60,    // 26..35
};
// 36 vertices

#define FLY 0x8000u

static const uint16_t callsign_e[] PROGMEM = {
    // P — continuous polyline (one stroke, 6 edges)
    0,1,   1,2,   2,3,   3,4,   4,5,   5,6,
    6, 7|FLY,                                  // flyback P → A

    // A — two outline strokes + crossbar (with internal flyback)
    7,8,   8,9,
    9, 10|FLY,                                 // flyback to crossbar start
    10,11,
    11, 12|FLY,                                // flyback A → 1

    // 1 — flag + vertical, then base (internal flyback)
    12,13,  13,14,
    14, 15|FLY,                                // flyback to base
    15,16,
    16, 17|FLY,                                // flyback 1 → D

    // D — closed continuous polyline
    17,18,  18,19,  19,20,  20,21,  21,22,  22,17,
    17, 23|FLY,                                // flyback D → V

    // V — two strokes (continuous, no internal flyback)
    23,24,  24,25,
    25, 26|FLY,                                // flyback V → B

    // B — upper loop, mid-bar, lower loop (one internal flyback)
    26,27,  27,28,  28,29,  29,30,  30,31,  31,32,
    32, 31|FLY,                                // retrace mid-bar to lower-loop start
    31,33,  33,34,  34,35,  35,26,
    26, 0|FLY,                                 // wrap back to P for the next refresh
};
// Edge count:
//   P 6 + 1 + A 3 + 1 + 1 + 1 ("1") 2 + 1 + 1 + 1 + D 6 + 1 + V 2 + 1 + B 10 + 1 + 1 = 39

const VectorMesh kMeshCallsign = { callsign_v, 36, callsign_e, 39 };

#undef FLY
