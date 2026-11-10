// Differential test of the file loaders (src/i3d/load.cpp, every frame type's Load, I3D_model::Open,
// I3D_scene::Open) against the original LS3DRF.dll, through visual_check.h. Both read the game's
// files from its .dta archives (one rw_data.dll serves both): models (MODELS\*.4ds, in an order
// from the seed) and, when asked, each mission's scene. After each file the two object graphs are
// walked together: every frame by its class's field table, its children, each visual's mesh
// (levels, vertices, face groups, faces) and materials; then both are released.
//
// The file names come from the unpacked data (test_paths.h, %HD2_UNPACKED%).
//
// Usage: compare_load.exe [original LS3DRF.dll] [files] [seed] [scenes]
//   files: how many (0: all); scenes: 1 to load every mission's SCENE.4ds as well (item 3's run).
//
// test-sources: src/i3d/*.cpp src/core/*.cpp src/math/*.cpp src/igraph/*.cpp src/dx/*.cpp src/platform/win32/*.cpp
#include "load_check.h"

using namespace lc;

namespace {

void LoadOne()
{
    const S_file &file = g_files[g_next++];
    g_lastOp = file.name;
    StartGraph();
    I3D_RESULT r;
    Pair root = LoadFile(file.name.c_str(), file.scene, r);
    Count(r ? "open failed" : "opened");
    if (!r)
        WalkFrame(root);
    Do(root, 0x00);
}

void SetUp() {}
void Compare_() {}

void Op()
{
    if (g_next == g_files.size() || false) {
        std::string text;
        for (const auto &c : g_coverage)
            text += " " + c.first + " " + std::to_string(c.second) + ",";
        std::printf("coverage:%s\ncompare_load: %zu files: identical\n", text.c_str(), g_files.size());
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    LoadOne();
}

}  // namespace

int main(int argc, char **argv)
{
    const char *orig = OriginalDllPath(argc > 1 ? argv[1] : nullptr, "LS3DRF.dll");
    g_gameDir = std::string(orig).substr(0, std::string(orig).find_last_of("\\/"));
    const long count = argc > 2 ? std::atol(argv[2]) : 200;
    const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)) : 1;
    const bool scenes = argc > 4 && std::atoi(argv[4]) == 1;
    static long s_count;
    s_count = count;
    static bool s_scenes;
    s_scenes = scenes;
    static uint32_t s_seed;
    s_seed = seed;
    auto setUp = +[] {
        MountArchives();
        g_rng.seed(s_seed);
        ListFiles(s_scenes);
        if (s_count && static_cast<size_t>(s_count) < g_files.size())
            g_files.resize(s_count);
        std::printf("%zu files\n", g_files.size());
    };
    // (Each op is a file, Run's count large enough for them all: Op ends the run.)
    char *runArgv[4] = {argv[0], argc > 1 ? argv[1] : const_cast<char *>(""), const_cast<char *>("100000000"), argc > 3 ? argv[3] : const_cast<char *>("1")};
    return Run(4, runArgv, "compare_load", setUp, &Op, &Compare_);
}
