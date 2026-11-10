// Differential test of I3D_emitor (src/i3d/emitter.cpp) against the original LS3DRF.dll, through visual_check.h: LS3DRF's emitor makes no particles: the frame and visual operations, SetDefaults, every parameter (set and read back), emitting, the blend setters, the groups (added, replaced, removed, by content), the empty bound, Duplicate, Init / Uninit.
//
// Usage: compare_emitor.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "visual_check.h"

#include "../src/i3d/emitter.h"
using namespace vc;
using namespace ls3d::i3d;

namespace {

constexpr int kN = 4;
Pair g_v[kN], g_root;

void SetUp()
{
    g_root = MakeFrame(FRAME_USER);
    for (Pair &v : g_v) {
        v = MakeVisual(FourCC("EMTR"));
        Do(v, 0xA0);
        Seed(v, 0x214, 0x21C);                  // (no Init writes the blends)
        Seed(v, 0x254, 0x260);
        Seed(v, 0x294, 0x2A0);
    }
}

S_vector4 g_color;

void SetParam(Pair x)
{
    const int index = Rand(0x26);
    uint32_t value = Rand(2) ? static_cast<uint32_t>(Rand(2000)) : g_rng();
    // (The ms values stay small: they are multiplied by a random float and truncated, and the two
    // sides differ in the last bits above 2^24, which no game value reaches.)
    if (index <= EMTP_LIFE_RANDOM || index == EMTP_TURN_INTERVAL || index == EMTP_TURN_INTERVAL_RANDOM)
        value = static_cast<uint32_t>(Rand(2000));
    if (index == EMTP_START_COLOR || index == EMTP_END_COLOR) {
        g_color = {RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f), RandF(-0.5f, 1.5f), RandF(0, 1)};
        value = Rand(4) ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_color)) : 0;
    } else if (index <= EMTP_LIFE_RANDOM || index == EMTP_TURN_INTERVAL || index == EMTP_TURN_INTERVAL_RANDOM) {
    } else if (index == EMTP_START_VELOCITY_WORLD || index == EMTP_END_VELOCITY_WORLD) {
        value = Rand(2);                        // (LS3DRF keeps the byte as given, the port 0 or 1: see the report)
    } else if (Rand(2)) {
        const float f = RandF(-10, 10);
        std::memcpy(&value, &f, 4);
    }
    char t[48];
    std::snprintf(t, sizeof t, "SetParam 0x%x %08x", index, value);
    g_lastOp = t;
    Get<I3D_RESULT>("SetParam", x, 0x98, index, value);
    if (index != EMTP_START_COLOR && index != EMTP_END_COLOR)
        Get<uint32_t>("GetParam", x, 0x9C, index);
    Count("SetParam");
}

void Groups(Pair x)
{
    S_emitorGroup g;
    g.rect = {RandF(0, 1), RandF(0, 1), RandF(0, 1), RandF(0, 1)};
    g.columns = 1 + Rand(4);
    g.rows = 1 + Rand(4);
    g.numFrames = 1;
    g.loopFrame = 0;
    g.frameTime = Rand(100);
    g.loopStart = g.loopLength = 0;
    const int n = reinterpret_cast<I3D_emitor *>(x.p)->numGroups;
    if (n && Rand(3) == 0) {
        g_lastOp = "RemoveGroup";
        Do(x, 0xC0, Rand(n));
        Count("RemoveGroup");
    } else {
        g_lastOp = "AddGroup";
        Get<int>("AddGroup", x, 0xBC, static_cast<const void *>(&g), n && Rand(2) ? Rand(n) : -1);
        Count("AddGroup");
    }
}

void Op()
{
    Pair x = g_v[Rand(kN)];
    switch (Rand(14)) {
    case 0: FrameOp(x, g_root); break;
    case 1: VisualOp(x); break;
    case 2:
    case 3:
    case 4: SetParam(x); break;
    case 5: g_lastOp = "SetDefaults"; Do(x, 0xA0); Count("SetDefaults"); break;
    case 6: g_lastOp = "EmitOn"; Do(x, 0xA4, Rand(2)); Count("EmitOn"); break;
    case 7: {
        g_lastOp = "blends";
        const uint32_t a = static_cast<uint32_t>(Rand(12)), b = static_cast<uint32_t>(Rand(12));
        Do(x, 0xA8, a);
        Do(x, 0xB0, b);
        Get<uint32_t>("GetSrcBlend", x, 0xAC);
        Get<uint32_t>("GetDestBlend", x, 0xB4);
        Count("blends");
        break;
    }
    case 8: g_lastOp = "SetGroup"; Do(x, 0xB8, Rand(4) - 1); Count("SetGroup"); break;
    case 9: Groups(x); break;
    case 10: g_lastOp = "ClearParticles"; Do(x, 0xC4); Count("ClearParticles"); break;
    case 11: g_lastOp = "UpdateParticleBound"; Do(x, 0xC8); Count("UpdateParticleBound"); break;
    case 12:
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

// (+0x26C / +0x27C: the velocities' w, SetDefaults writes an unset local.)
void CompareAll()
{
    for (const Pair &v : g_v) {
        Compare("I3D_emitor", v, kLayout_I3D_emitor, {VC_VISUAL_SKIP, 0x26C, 0x27C});
        auto *p = reinterpret_cast<I3D_emitor *>(v.p);
        auto *og = *reinterpret_cast<uint8_t **>(v.o + offsetof(I3D_emitor, groups));
        for (int k = 0; k < p->numGroups; ++k) {
            if (std::memcmp(og + k * sizeof(S_emitorGroup), &p->groups[k], 0x24) ||
                std::memcmp(og + k * sizeof(S_emitorGroup) + 0x34, reinterpret_cast<uint8_t *>(&p->groups[k]) + 0x34, 8))
                Fail("emitor group");
        }
    }
}

}  // namespace

int main(int argc, char **argv) { return Run(argc, argv, "compare_emitor", &SetUp, &Op, &CompareAll); }
