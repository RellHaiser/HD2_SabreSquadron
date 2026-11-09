// Differential test of I3D_joint (src/i3d/joint.cpp), on the joints of a skinned model from the game's
// files: the frame operations, Update and MatrixChanged (a moved joint makes its mesh's bounds stale),
// Duplicate between joints.
//
// Usage: compare_joint.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "load_check.h"

using namespace lc;

namespace {

std::vector<Pair> g_joints;
Pair g_root;

void CollectJoints(Pair f)
{
    if (*reinterpret_cast<uint16_t *>(f.o + 0xF8) == FRAME_JOINT)
        g_joints.push_back(f);
    const int n = CallV<int>(f.o, 0x3C);
    for (int k = 0; k < n; ++k)
        CollectJoints({CallV<uint8_t *>(f.o, 0x40, k), CallV<uint8_t *>(f.p, 0x40, k)});
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
            CollectJoints(root);
            if (g_joints.size() >= 3) {
                g_lastOp = f.name;
                WalkFrame(root);
                g_root = MakeFrame(FRAME_USER);
                std::printf("model %s, %zu joints\n", f.name.c_str(), g_joints.size());
                for (Pair &j : g_joints)
                    Seed(j, 0x120, 0x160);      // (modelMatrix: unset until the skin binds)
                return;
            }
            g_joints.clear();
        }
        Do(root, 0x00);
    }
    Fail("no skinned model");
}

void Op()
{
    Pair x = g_joints[Rand(static_cast<int>(g_joints.size()))];
    switch (Rand(4)) {
    case 0:
    case 1: FrameOp(x, g_root); break;
    case 2: {
        g_lastOp = "Duplicate";
        Pair s = g_joints[Rand(static_cast<int>(g_joints.size()))];
        if (s.o != x.o) {
            Get<I3D_RESULT>("Duplicate", x, 0x4C, s);
            Count("Duplicate");
        }
        break;
    }
    default:
        g_lastOp = "Update";
        Do(x, 0x24);
        Count("Update");
        break;
    }
}

void CompareAll()
{
    for (const Pair &j : g_joints)
        Compare("I3D_joint", j, kLayout_I3D_joint, {VC_FRAME_SKIP, 0x16C});
}

}  // namespace

int main(int argc, char **argv)
{
    FindGame(argc > 1 ? argv[1] : nullptr);
    return Run(argc, argv, "compare_joint", &SetUp, &Op, &CompareAll);
}
