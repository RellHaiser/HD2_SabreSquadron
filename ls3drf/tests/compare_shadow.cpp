// Differential test of I3D_shadow (src/i3d/shadow.cpp) against the original LS3DRF.dll, through visual_check.h: the frame operations, the settings, Open / Close, LinkTo, Duplicate, Init / Uninit. The shadow draws nothing in LS3DRF; casters come from the scene: compare_scene.
//
// Usage: compare_shadow.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "visual_check.h"

#include "../src/i3d/shadow.h"
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
        v = MakeFrame(FRAME_SHADOW);
        Seed(v, 0x144, 0x170);
        Seed(v, 0x17C, 0x180);
        Seed(v, 0x190, 0x194);
    }
}

void Op()
{
    Pair x = g_v[Rand(kN)];
    switch (Rand(11)) {
    case 0: FrameOp(x, g_root); break;
    case 1: g_lastOp = "SetSize"; Get<I3D_RESULT>("SetSize", x, 0x7C, 1 << Rand(9), 1 << Rand(9)); Count("SetSize"); break;
    case 2: g_lastOp = "SetDepth"; Do(x, 0x80, RandF(-1, 50)); Count("SetDepth"); break;
    case 3: g_lastOp = "SetMaxHeight"; Get<I3D_RESULT>("SetMaxHeight", x, 0x84, RandF(-10, 400)); Count("SetMaxHeight"); break;
    case 4: g_lastOp = "SetMode"; Do(x, 0x88, Rand(5)); Count("SetMode"); break;
    case 5: g_lastOp = "SetOpacity"; Do(x, 0x8C, RandF(-1, 2)); Count("SetOpacity"); break;
    case 6: {
        g_lastOp = "SetColor";
        const S_vector4 c = {RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f), 0};
        Do(x, 0x90, static_cast<const void *>(&c));
        Count("SetColor");
        break;
    }
    case 7: g_lastOp = "SetOrthogonal"; Do(x, 0x94, Rand(2)); Count("SetOrthogonal"); break;
    case 8:
        g_lastOp = "Open / Close";
        Get<I3D_RESULT>("Open", x, 0x74, "none", 0u);
        Do(x, 0x78);
        Count("Open");
        break;
    case 9:
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

// (+0x1AC: the light direction's w: Init writes an unset local.)
void CompareAll()
{
    for (const Pair &v : g_v)
        Compare("I3D_shadow", v, kLayout_I3D_shadow, {VC_FRAME_SKIP, 0x1AC});
}

}  // namespace

int main(int argc, char **argv) { return Run(argc, argv, "compare_shadow", &SetUp, &Op, &CompareAll); }
