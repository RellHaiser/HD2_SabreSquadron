// Differential test of I3D_scene (src/i3d/scene.cpp) against the original LS3DRF.dll, on a mission scene
// from the game's files (the smallest): finding frames, sectors and light intensity, the lights' colour, the
// active camera and its projections, adding and removing frames, the tick list, updating the sectors, the
// frames' sectors. After each operation the scene's fields; every 200 operations the whole graph, portals too.
//
// Usage: compare_scene.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "load_check.h"

using namespace lc;

namespace {

Pair g_scene, g_camera, g_user;
std::vector<Pair> g_frames;

void Collect(Pair f)
{
    g_frames.push_back(f);
    const int n = CallV<int>(f.o, 0x3C);
    for (int k = 0; k < n; ++k)
        Collect({CallV<uint8_t *>(f.o, 0x40, k), CallV<uint8_t *>(f.p, 0x40, k)});
}

void SetUp()
{
    MountArchives();
    namespace fs = std::filesystem;
    const fs::path root = UnpackedDir(g_gameDir);
    std::string best;
    uintmax_t bestSize = ~uintmax_t(0);
    for (const auto &e : fs::directory_iterator(root / "MISSIONS")) {
        const fs::path sc = e.path() / "SCENE.4ds";
        if (e.is_directory() && fs::exists(sc) && fs::file_size(sc) < bestSize) {
            bestSize = fs::file_size(sc);
            best = "MISSIONS\\" + e.path().filename().string() + "\\SCENE.4ds";
        }
    }
    g_lastOp = best;
    StartGraph();
    I3D_RESULT r;
    g_scene = LoadFile(best.c_str(), true, r);
    if (r != I3D_OK)
        Fail("the scene did not load");
    std::printf("scene %s\n", best.c_str());
    Collect(g_scene);
    g_camera = MakeFrame(FRAME_CAMERA);
    Get<I3D_RESULT>("LinkTo camera", g_camera, 0x38, g_scene, 0u);
    g_user = MakeFrame(FRAME_USER);
}

Pair Sector()
{
    Pair x = g_frames[Rand(static_cast<int>(g_frames.size()))];
    return *reinterpret_cast<uint16_t *>(x.o + 0xF8) == FRAME_SECTOR ? x : Pair{};
}

void Op()
{
    Pair x = g_scene;
    const S_vector pos = RandVector();
    switch (Rand(15)) {
    case 0:
        g_lastOp = "UpdateSectors";
        Do(x, 0xEC);
        Count("UpdateSectors");
        break;
    case 1:
        g_lastOp = "FindFrame";
        Get<void *>("FindFrame", x, 0x78, kNames[Rand(6)], static_cast<uint32_t>(Rand(4)));
        Count("FindFrame");
        break;
    case 2: {
        g_lastOp = "GetLightIntensity";
        const float a = CallV<float>(x.o, 0x88, &pos, static_cast<uint8_t *>(nullptr), 0u, static_cast<float *>(nullptr));
        const float b = CallV<float>(x.p, 0x88, &pos, static_cast<uint8_t *>(nullptr), 0u, static_cast<float *>(nullptr));
        if (std::memcmp(&a, &b, 4))
            Fail("GetLightIntensity");
        Count("GetLightIntensity");
        break;
    }
    case 3: {
        g_lastOp = "SetLightsColor";
        const S_vector c(RandF(0, 1), RandF(0, 1), RandF(0, 1));
        Do(x, 0x84, &c);
        Count("SetLightsColor");
        break;
    }
    case 4:
        g_lastOp = "SetActiveCamera";
        if (Rand(3))
            Do(x, 0x8C, g_camera);
        else
            Do(x, 0x8C, static_cast<uint8_t *>(nullptr));
        Count("SetActiveCamera");
        break;
    case 5:
        g_lastOp = "camera move";
        Do(g_camera, 0x08, &pos);
        Do(g_camera, 0x24);
        break;
    case 6: {
        g_lastOp = "TransformPoints";
        const S_vector4 pts[2] = {{pos.x, pos.y, pos.z, 1}, {pos.y, pos.z, pos.x, 1}};
        S_vector4 oo[2], op[2];
        const I3D_RESULT ra = CallV<I3D_RESULT>(x.o, 0x94, pts, oo, 2), rb = CallV<I3D_RESULT>(x.p, 0x94, pts, op, 2);
        if (ra != rb || (ra == I3D_OK && std::memcmp(oo, op, sizeof oo)))
            Fail("TransformPoints");
        Count("TransformPoints");
        break;
    }
    case 7: {
        g_lastOp = "UnmapScreenPoint";
        S_vector4 oo[2], op[2];
        const int px = Rand(800), py = Rand(600);
        const I3D_RESULT ra = CallV<I3D_RESULT>(x.o, 0x90, px, py, &oo[0], &oo[1]);
        const I3D_RESULT rb = CallV<I3D_RESULT>(x.p, 0x90, px, py, &op[0], &op[1]);
        if (ra != rb || (ra == I3D_OK && (std::memcmp(&oo[0], &op[0], 12) || std::memcmp(&oo[1], &op[1], 12))))
            Fail("UnmapScreenPoint");
        Count("UnmapScreenPoint");
        break;
    }
    case 8: {
        g_lastOp = "IsFrameVisible";
        Pair f = g_frames[Rand(static_cast<int>(g_frames.size()))];
        Get<bool>("IsFrameVisible", x, 0x98, f, g_camera);
        Count("IsFrameVisible");
        break;
    }
    case 9:
        g_lastOp = "AddFrame / RemoveFrame";
        Do(x, 0x7C, g_user);
        Get<I3D_RESULT>("RemoveFrame", x, 0x80, g_user);
        Count("AddFrame");
        break;
    case 10:
        g_lastOp = "AddTickFrame";
        Do(x, 0xC4, g_user);
        Get<I3D_RESULT>("DeleteTickFrame", x, 0xC8, g_user);
        Count("TickFrame");
        break;
    case 11:
        g_lastOp = "GetSector";
        Get<void *>("GetSector", x, 0xDC, &pos);
        Count("GetSector");
        break;
    case 12:
        g_lastOp = "LocateSector";
        Get<void *>("LocateSector", x, 0xD8, &pos, Sector());
        Count("LocateSector");
        break;
    case 13:
        g_lastOp = "SetBackdropRange";
        Do(x, 0xE0, RandF(0, 200), RandF(0, 2000));
        Count("SetBackdropRange");
        break;
    default:
        g_lastOp = "UpdateFrameSector";
        Do(x, 0xE8, g_user, &pos);
        Count("UpdateFrameSector");
        break;
    }
}

void CompareAll()
{
    CompareScene(g_scene);
    if (g_op % 200 == 0) {
        g_walked.clear();
        WalkFrame(g_scene);
        Count("graph walks");
    }
}

}  // namespace

int main(int argc, char **argv)
{
    FindGame(argc > 1 ? argv[1] : nullptr);
    return Run(argc, argv, "compare_scene", &SetUp, &Op, &CompareAll);
}
