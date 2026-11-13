// Differential test of I3D_billboard, the slots compare_billboard does not reach (src/i3d/billboard.cpp) against the original LS3DRF.dll, through visual_check.h: the frame, visual and object operations, the mesh and its bound, Duplicate between billboards, Init / Uninit, with every axis and mode.
//
// Usage: compare_billboard_visual.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "visual_check.h"

#include "../src/i3d/billboard.h"
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
        v = MakeVisual(FourCC("BBRD"));
        Seed(v, 0x218, 0x260);              // (the facing matrix is made when drawn)
    }
}

void Op()
{
    Pair x = g_v[Rand(kN)];
    switch (Rand(6)) {
    case 0: FrameOp(x, g_root); break;
    case 1: VisualOp(x); break;
    case 2: ObjectOp(x); break;
    case 3: {
        g_lastOp = "axis and mode";
        const int axis = Rand(3), mode = Rand(5);
        for (uint8_t *m : {x.o, x.p}) {
            *reinterpret_cast<int *>(m + 0x210) = axis;
            *reinterpret_cast<int *>(m + 0x214) = mode;
        }
        Count("axis and mode");
        break;
    }
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
        Compare("I3D_billboard", v, kLayout_I3D_billboard, {VC_VISUAL_SKIP});
}

}  // namespace

int main(int argc, char **argv) { return Run(argc, argv, "compare_billboard_visual", &SetUp, &Op, &CompareAll); }
