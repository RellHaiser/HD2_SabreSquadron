// The visuals' light block and the light flags of LS3DF's renderer (0x100637a0 - 0x100647f0), kept
// for the layout. LS3DRF draws nothing: none of LS3DF's device state helpers (z bias, materials, the
// sectors' ambient colors, EnableLights / SelectLights / MirrorLights) is ported.
#pragma once

#include <cstdint>

// Light flags the renderer keeps in I3D_light::lightFlags.
enum : uint32_t {
    LIGHTF_DIRTY = 0x01,                    // d3dLight changed: SetLight before the next enable
    LIGHTF_MIRRORED = 0x02,                 // a copy made for a mirror's reflection (LS3DF's linkedLight: the source)
    LIGHTF_MOVED = 0x04,                    // changed since its mirrored copy was made
};

namespace ls3d::i3d {

// The device lights a visual is lit by (object, lightmap, billboard and single-mesh visuals keep
// this at +0x1F4). Kept for the layout: object Init clears it, nothing else touches it.
struct S_visualLights {
    uint16_t indices[8];                    // +0x1F4 device light indices
    int count;                              // +0x204
    int stamp;                              // +0x208 the light sector's lightChanges + the visual's
                                            // changeCount when they were chosen (-1: never)
};
static_assert(sizeof(S_visualLights) == 0x18, "S_visualLights layout");

}  // namespace ls3d::i3d
