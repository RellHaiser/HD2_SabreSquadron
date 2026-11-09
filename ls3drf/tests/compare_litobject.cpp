// Differential test of I3D_lit_object (src/i3d/litobject.cpp) against the original LS3DRF.dll, through visual_check.h: the frame, visual and object operations, the sets' flags and current set, freeing lightmaps and the texture byte count. Lightmap sets come from files (LoadLightmaps): compare_load.
//
// Usage: compare_litobject.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "visual_check.h"

#include "../src/i3d/litobject.h"
using namespace vc;
using namespace ls3d::i3d;

namespace {

constexpr int kN = 4;
Pair g_v[kN], g_root;

void SetUp()
{
    g_root = MakeFrame(FRAME_USER);
    for (Pair &v : g_v)
        {
        v = MakeVisual(FourCC("LMAP"));
        for (unsigned off : {0x214, 0x24C, 0x284, 0x2BC, 0x2F4, 0x32C})
            Seed(v, off, off + 4);              // (the sets' padding)
        Seed(v, 0x364, 0x370);
    }
}

void Op()
{
    Pair x = g_v[Rand(kN)];
    switch (Rand(8)) {
    case 0: FrameOp(x, g_root); break;
    case 1: VisualOp(x); break;
    case 2: ObjectOp(x); break;
    case 3:
        g_lastOp = "FreeLightmaps";
        Get<I3D_RESULT>("FreeLightmaps", x, 0xB0, 0);
        Count("FreeLightmaps");
        break;
    case 4:
        g_lastOp = "SetSetFlags";
        Get<I3D_RESULT>("SetSetFlags", x, 0xBC, static_cast<uint32_t>(Rand(16)));
        Count("SetSetFlags");
        break;
    case 5:
        g_lastOp = "GetTextureBytes";
        Get<int>("GetTextureBytes", x, 0xC4);
        break;
    case 6:
        g_lastOp = "Duplicate";
        Get<I3D_RESULT>("Duplicate", x, 0x4C, g_v[Rand(kN)]);
        Count("Duplicate");
        break;
    default:
        g_lastOp = "Uninit / Init";
        Recycle(x);
        Count("Init");
        break;
    }
}

void CompareAll()
{
    for (const Pair &v : g_v)
        Compare("I3D_lit_object", v, kLayout_I3D_lit_object, {VC_VISUAL_SKIP});
}

}  // namespace

int main(int argc, char **argv) { return Run(argc, argv, "compare_litobject", &SetUp, &Op, &CompareAll); }
