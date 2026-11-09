// Differential test of I3D_lens (src/i3d/lens.cpp) against the original LS3DRF.dll: the frame and
// visual operations, Init / Uninit and Duplicate between lenses of each side, whose settings and
// element lists the test fills the same in both (a lens gets its elements from a model file; Load
// is covered by compare_load). The elements' materials are compared as objects.
//
// Usage: compare_lens.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "visual_check.h"

#include "../src/i3d/lens.h"

using namespace vc;

namespace {

constexpr int kLenses = 4;
Pair g_lens[kLenses];
Pair g_root;

// (LS3DRF keeps a lens's settings through the same fields, unwritten by any slot: put here as Load
// and the renderer would.)
void Settings(Pair x)
{
    const float v[4] = {RandF(0, 5), RandF(0, 5), RandF(1, 20), RandF(1, 20)};
    for (uint8_t *m : {x.o, x.p}) {
        std::memcpy(m + 0x1E0, &v[0], 8);
        std::memcpy(m + 0x220, &v[2], 8);
    }
    Count("settings");
}

// Each side's element list, the same elements (Uninit first released the old ones).
void Elements(Pair x)
{
    const int n = Rand(4);
    for (int k = 0; k < n; ++k) {
        const float pos = RandF(-1, 2), size = RandF(0, 3);
        const Pair mat = g_mats[Rand(kMats)];
        ++reinterpret_cast<I3D_material *>(mat.o)->refCount;
        ++reinterpret_cast<I3D_material *>(mat.p)->refCount;
        S_lensElement e = {pos, size, reinterpret_cast<I3D_material *>(mat.p)};
        reinterpret_cast<I3D_lens *>(x.p)->elements.PushBack(e);
        uint8_t *v = x.o + offsetof(I3D_lens, elements);
        auto *grown = static_cast<S_lensElement *>(OrigNew(sizeof(S_lensElement) * (k + 1)));
        std::memcpy(grown, VecData(v), sizeof(S_lensElement) * k);
        grown[k] = {pos, size, reinterpret_cast<I3D_material *>(mat.o)};
        if (VecData(v))
            OrigDelete(VecData(v));
        VecData(v) = reinterpret_cast<uint8_t *>(grown);
        *reinterpret_cast<int *>(v + 4) = k + 1;
        VecCount(v) = k + 1;
    }
    Count("elements", n);
}

void CompareElements(Pair x)
{
    uint8_t *v = x.o + offsetof(I3D_lens, elements);
    const auto &e = reinterpret_cast<I3D_lens *>(x.p)->elements;
    if (VecCount(v) != e.Size())
        Fail("element count");
    auto *o = reinterpret_cast<S_lensElement *>(VecData(v));
    for (int k = 0; k < e.Size(); ++k) {
        if (std::memcmp(&o[k], &e[k], 8))
            Fail("element values");
        if (g_partner[o[k].material] != e[k].material)
            Fail("element material");
    }
}

void SetUp()
{
    g_root = MakeFrame(FRAME_USER);
    for (Pair &l : g_lens) {
        l = MakeVisual(FourCC("LENS"));
        Settings(l);
    }
}

void Op()
{
    Pair x = g_lens[Rand(kLenses)];
    switch (Rand(5)) {
    case 0:
        FrameOp(x, g_root);
        break;
    case 1:
        VisualOp(x);
        break;
    case 2:
        g_lastOp = "settings";
        Settings(x);
        break;
    case 3: {
        g_lastOp = "Duplicate";
        Pair s = g_lens[Rand(kLenses)];
        // (The original drops the old elements without releasing their materials; the port fixes
        // that: the test releases them on the original's side.)
        if (s.o != x.o) {
            uint8_t *v = x.o + offsetof(I3D_lens, elements);
            for (int k = 0; k < VecCount(v); ++k)
                CallV<uint32_t>(reinterpret_cast<S_lensElement *>(VecData(v))[k].material, 0x00);
        }
        Get<I3D_RESULT>("Duplicate", x, 0x4C, s);
        Count("Duplicate");
        break;
    }
    default:
        g_lastOp = "Uninit / Init";
        Recycle(x);
        Elements(x);
        Count("Init");
        break;
    }
}

void CompareAll()
{
    // (color.w / transmit.w: "an unset local", the original's stack; `elements` by content; the
    // Vec capacity: the test grows the original's by one each.)
    for (const Pair &l : g_lens) {
        Compare("I3D_lens", l, kLayout_I3D_lens, {VC_VISUAL_SKIP, 0x20C, 0x21C, 0x22C});
        CompareElements(l);
    }
    for (const Pair &m : g_mats)
        if (reinterpret_cast<I3D_material *>(m.o)->refCount != reinterpret_cast<I3D_material *>(m.p)->refCount)
            Fail("material reference count");
}

}  // namespace

int main(int argc, char **argv) { return Run(argc, argv, "compare_lens", &SetUp, &Op, &CompareAll); }
