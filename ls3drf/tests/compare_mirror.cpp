// Differential test of I3D_mirror (src/i3d/mirror.cpp) against the original LS3DRF.dll, through visual_check.h: the frame and visual operations, the plane and color setters, LockFaces, Duplicate, Init / Uninit, with the bound, plane and color the slots write. Geometry comes from model files: compare_load.
//
// Usage: compare_mirror.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "visual_check.h"

#include "../src/i3d/mirror.h"
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
        v = MakeVisual(FourCC("MIRR"));
        Seed(v, 0x22C, 0x234);
    }
}

void Op()
{
    Pair x = g_v[Rand(kN)];
    switch (Rand(7)) {
    case 0: FrameOp(x, g_root); break;
    case 1: VisualOp(x); break;
    case 2: {
        g_lastOp = "SetPlane";
        I3D_bbox box;
        box.min = RandVector();
        box.minW = 0;
        box.max = S_vector(box.min.x + RandF(0.1f, 9), box.min.y + RandF(0.1f, 9), box.min.z + RandF(0.1f, 9));
        box.maxW = 0;
        S_matrix m;
        m.Identity();
        const S_vector d = RandUnit();
        m.SetDir3(d, S_vector(0.0f, 1.0f, 0.0f));
        m(3) = RandVector();
        Do(x, 0x94, static_cast<const void *>(&box), static_cast<const void *>(&m));
        Count("SetPlane");
        break;
    }
    case 3: {
        g_lastOp = "SetColor";
        const S_vector4 c = {RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f)};
        Do(x, 0x98, static_cast<const void *>(&c));  // (the original's result is not set on one path)
        Count("SetColor");
        break;
    }
    case 4:
        g_lastOp = "LockFaces";
        Get<void *>("LockFaces", x, 0x9C, 0u);
        Count("LockFaces");
        break;
    case 5:
        g_lastOp = "Duplicate";
        {
            // (Not itself: the frame's Duplicate of itself reads freed name memory in the original;
            // the port fixes that.)
            Pair s = g_v[Rand(kN)];
            if (s.o == x.o)
                s = g_v[(Rand(kN - 1) + 1) % kN];
            if (s.o == x.o)
                break;
            Get<I3D_RESULT>("Duplicate", x, 0x4C, s);
        }
        Count("Duplicate");
        break;
    default:
        g_lastOp = "Uninit / Init";
        Recycle(x);
        Count("Init");
        break;
    }
}

// (+0x21C: SetPlane copies the last row's w from an unset local.)
void CompareAll()
{
    for (const Pair &v : g_v)
        Compare("I3D_mirror", v, kLayout_I3D_mirror, {VC_VISUAL_SKIP, 0x21C});
}

}  // namespace

int main(int argc, char **argv) { return Run(argc, argv, "compare_mirror", &SetUp, &Op, &CompareAll); }
