// Differential test of I3D_projector (src/i3d/projector.cpp, through its vtable) against the original LS3DRF.dll, through visual_check.h: the frame and visual operations, every setter (a fake texture, the materials), Build, Duplicate, Init / Uninit.
//
// Usage: compare_projector.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "visual_check.h"

#include "../src/i3d/projector.h"
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
        v = MakeVisual(FourCC("PROJ"));
        Seed(v, 0x1EC, 0x1F0);
    }
}

void Op()
{
    Pair x = g_v[Rand(kN)];
    switch (Rand(13)) {
    case 0: FrameOp(x, g_root); break;
    case 1: VisualOp(x); break;
    case 2:
        g_lastOp = "SetTexture";
        Do(x, 0x94, Rand(3) ? FakeTexture() : static_cast<void *>(nullptr));
        Count("SetTexture");
        break;
    case 3:
        g_lastOp = "SetMaterial";
        if (Rand(3))
            Do(x, 0x98, g_mats[Rand(kMats)]);
        else
            Do(x, 0x98, static_cast<uint8_t *>(nullptr));
        Count("SetMaterial");
        break;
    case 4: g_lastOp = "SetOrthogonal"; Do(x, 0x9C, Rand(2)); Count("SetOrthogonal"); break;
    case 5: g_lastOp = "SetIntensity"; Do(x, 0xA0, RandF(-1, 5)); Count("SetIntensity"); break;
    case 6: g_lastOp = "SetOpacity"; Do(x, 0xA4, RandF(-1, 2)); Count("SetOpacity"); break;
    case 7: {
        g_lastOp = "SetColor";
        const S_vector4 c = {RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f), 0};
        Do(x, 0xA8, static_cast<const void *>(&c));
        Count("SetColor");
        break;
    }
    case 8: g_lastOp = "SetMode"; Do(x, 0xAC, Rand(4)); Count("SetMode"); break;
    case 9: g_lastOp = "SetBuffered"; Do(x, 0xB0, static_cast<uint32_t>(Rand(2))); Count("SetBuffered"); break;
    case 10:
        g_lastOp = "Build";
        Do(x, 0xB4, static_cast<uint32_t>(Rand(4)), Rand(2) ? g_root : Pair{});
        Count("Build");
        break;
    case 11:
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
        Compare("I3D_projector", v, kLayout_I3D_projector, {VC_VISUAL_SKIP});
}

}  // namespace

int main(int argc, char **argv) { return Run(argc, argv, "compare_projector", &SetUp, &Op, &CompareAll); }
