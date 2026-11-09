// Differential test of I3D_singlemesh (src/i3d/singlemesh.cpp), on a skinned model from the game's files
// (skin data and joints come from model files): the frame operations on the visual, its light level and
// alpha, level refresh, the joint boxes, a vertex's joint, the skinned vertices, releasing the skinned levels.
//
// Usage: compare_singlemesh.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "load_check.h"

using namespace lc;

namespace {

Pair g_root, g_mesh;

Pair FindSngm(Pair f)
{
    if (*reinterpret_cast<uint16_t *>(f.o + 0xF8) == FRAME_VISUAL && CallV<uint32_t>(f.o, 0x04) == FourCC("SNGM"))
        return f;
    const int n = CallV<int>(f.o, 0x3C);
    for (int k = 0; k < n; ++k) {
        Pair c{CallV<uint8_t *>(f.o, 0x40, k), CallV<uint8_t *>(f.p, 0x40, k)};
        if (Pair r = FindSngm(c))
            return r;
    }
    return {};
}

void SetUp()
{
    MountArchives();
    ListFiles(false);
    for (const S_file &f : g_files) {
        I3D_RESULT r;
        StartGraph();
        Pair root = LoadFile(f.name.c_str(), false, r);
        if (r == I3D_OK) {
            if (Pair v = FindSngm(root)) {
                g_lastOp = f.name;
                g_mesh = v;
                WalkFrame(root);
                g_root = MakeFrame(FRAME_USER);
                std::printf("model %s\n", f.name.c_str());
                return;
            }
        }
        Do(root, 0x00);
    }
    Fail("no skinned model");
}

void Op()
{
    Pair x = g_mesh;
    const int joints = reinterpret_cast<I3D_singlemesh *>(x.p)->numJoints;
    switch (Rand(11)) {
    case 0: FrameOp(x, g_root); break;
    case 1: VisualOp(x); break;
    case 2: g_lastOp = "SetLightLevel"; Do(x, 0x9C, RandF(0, 1)); Count("SetLightLevel"); break;
    case 3: g_lastOp = "SetAlpha"; Do(x, 0xA0, RandF(0, 1)); Count("SetAlpha"); break;
    case 4: g_lastOp = "RefreshLevel"; Do(x, 0xA4, Rand(3)); Count("RefreshLevel"); break;
    case 5: g_lastOp = "RefreshLevels"; Do(x, 0xA8); Count("RefreshLevels"); break;
    case 6: g_lastOp = "GetJointBBox"; Get<void *>("GetJointBBox", x, 0xAC, Rand(joints + 2) - 1); Count("GetJointBBox"); break;
    case 7:
        g_lastOp = "GetVertexJoint";
        Get<void *>("GetVertexJoint", x, 0xB0, Rand(4), 0);
        Count("GetVertexJoint");
        break;
    case 8: {
        g_lastOp = "SkinnedVertices";
        const int level = 0;
        const auto *a = CallV<const S_vector4 *>(x.o, 0xB4, level);
        const auto *b = CallV<const S_vector4 *>(x.p, 0xB4, level);
        if (!a != !b)
            Fail("SkinnedVertices: one is null");
        Count("SkinnedVertices");
        break;
    }
    case 9: g_lastOp = "ReleaseLevel"; Do(x, 0xB8, Rand(3)); Count("ReleaseLevel"); break;
    default: g_lastOp = "ReleaseLevels"; Do(x, 0xBC); Count("ReleaseLevels"); break;
    }
}

void CompareAll()
{
    Compare("I3D_singlemesh", g_mesh, kLayout_I3D_singlemesh, {VC_VISUAL_SKIP, 0x24C});
}

}  // namespace

int main(int argc, char **argv)
{
    FindGame(argc > 1 ? argv[1] : nullptr);
    return Run(argc, argv, "compare_singlemesh", &SetUp, &Op, &CompareAll);
}
