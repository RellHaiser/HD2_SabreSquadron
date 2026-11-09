// Differential test of I3D_morph (src/i3d/morph.cpp) against the original LS3DRF.dll, through visual_check.h: the frame, visual and object operations, the morph interface at +0x120 (this adjusted), the mesh keeping the morph data's bound, Duplicate, Init / Uninit. Morph data comes from files: compare_load.
//
// Usage: compare_morph.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "visual_check.h"

#include "../src/i3d/morph.h"
using namespace vc;
using namespace ls3d::i3d;

namespace {

constexpr int kN = 4;
Pair g_v[kN], g_root;

// The morph interface (the secondary vtable, at +0x120).
Pair Morph(Pair x) { return {x.o + 0x120, x.p + 0x120}; }

void SetUp()
{
    g_root = MakeFrame(FRAME_USER);
    for (Pair &v : g_v)
        {
        v = MakeVisual(FourCC("MRPH"));
        Seed(v, 0x22C, 0x230);
    }
}

void Op()
{
    Pair x = g_v[Rand(kN)];
    switch (Rand(6)) {
    case 0: FrameOp(x, g_root); break;
    case 1: VisualOp(x); break;
    case 2: ObjectOp(x); break;
    case 3:
        g_lastOp = "morph interface";
        Get<int>("NumMorphTargets", Morph(x), 0x00);
        Get<int>("NumMorphChannels", Morph(x), 0x04);
        Get<I3D_RESULT>("SetMorphWeight", Morph(x), 0x08, Rand(3), Rand(3), RandF(0, 1));
        Count("morph interface");
        break;
    case 4:
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
        Compare("I3D_morph", v, kLayout_I3D_morph, {VC_VISUAL_SKIP});
}

}  // namespace

int main(int argc, char **argv) { return Run(argc, argv, "compare_morph", &SetUp, &Op, &CompareAll); }
