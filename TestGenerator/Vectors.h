// ============================================================
// Vectors.h — Vector / image mesh data for XY scope display
//
// Each mesh is a flat list of vertices (int8 x/y in -100..+100)
// plus a list of edges (pairs of vertex indices). The renderer in
// Waveforms.ino walks the edges, interpolates between endpoints,
// and emits one (x,y) sample per DAC tick.
//
// Inspired by bitluni's OsciDisplay project; the data is ours.
//
// (c) 2026 PA1DVB
// ============================================================
#pragma once
#include <Arduino.h>

// Edge format: from,to vertex-index pairs. If the high bit of `to` is
// set (0x8000), the edge is a *flyback* (pen-up move) and the renderer
// traces it ~32× faster — on the scope it shows as a dim ghost line
// instead of a bright stroke. Used in the callsign mesh so the letter
// strokes look bright and the connectors between them fade away.
struct VectorMesh {
    const int8_t*   vertices;     // x,y pairs (2 bytes per vertex)
    uint16_t        vertexCount;
    const uint16_t* edges;        // from,to index pairs (to | 0x8000 = flyback)
    uint16_t        edgeCount;
};

extern const VectorMesh kMeshDomoticz;
extern const VectorMesh kMeshHeart;
extern const VectorMesh kMeshStar;
extern const VectorMesh kMeshHouse;
extern const VectorMesh kMeshCallsign;
