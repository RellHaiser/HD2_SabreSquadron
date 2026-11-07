// The copy protection's pass-through and a frame's game data (part 11d: game/copy_protection.cpp). MinGW; build_protection.bat; run
// protection_test.exe ("protection: N checks, 0 failures").
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define protected public
#define private public
#include "game/copy_protection.h"
#include "game/pending.h"
#include "ls3d/i3d.h"
#undef private
#undef protected

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

alignas(16) unsigned char g_frameA[0x200], g_frameB[0x200];
I3D_frame *Frame(unsigned char *storage) { return reinterpret_cast<I3D_frame *>(storage); }
uint32_t g_enumMask, g_enumFlags, g_enumContext;
int g_enums;
using EnumProc = uint32_t(__stdcall *)(I3D_frame *, void *);
uint32_t __stdcall F_EnumFrames(void *, EnumProc proc, void *context, uint32_t mask, uint32_t flags)
{
    ++g_enums;
    g_enumMask = mask;
    g_enumFlags = flags;
    g_enumContext = context == nullptr;
    CHECK(proc(Frame(g_frameA), context) == 0);
    CHECK(proc(Frame(g_frameB), context) == 0);
    return 0;
}
void *g_sceneVt[128];
struct S_FakeScene { void **vtable; } g_scene;
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Seeded: not the sequence of the seed before (from the tick: tested over two different seeds).
    std::srand(1);
    const int first = std::rand();
    std::srand(1);
    InitProtectionTables();
    const int afterA = std::rand();
    Sleep(20);
    InitProtectionTables();
    const int afterB = std::rand();
    CHECK(!(afterA == first && afterB == first));

    // Data: copied into a new block; then into the same block.
    I3D_frame *frame = Frame(g_frameA);
    S_FrameData data = {};
    data.unknown08 = 8;
    data.unknown1C = 0x1C;
    CHECK(frame->GetData() == nullptr);
    SetFrameData(frame, data);
    S_FrameData *block = frame->GetData();
    CHECK(block != nullptr && block != &data && block->unknown08 == 8 && block->unknown1C == 0x1C);
    data.unknown08 = 9;
    SetFrameData(frame, data);
    CHECK(frame->GetData() == block && block->unknown08 == 9 && block->unknown1C == 0x1C);
    FreeFrameData(frame);
    CHECK(frame->GetData() == nullptr);
    FreeFrameData(frame);                               // (none: nothing)
    CHECK(frame->GetData() == nullptr);

    // All of a scene's; no scene: nothing.
    g_scene.vtable = g_sceneVt;
    // (EnumFrames: every slot of the fake, whichever the class says.)
    for (void *&entry : g_sceneVt)
        entry = reinterpret_cast<void *>(F_EnumFrames);
    SetFrameData(Frame(g_frameA), data);
    SetFrameData(Frame(g_frameB), data);
    FreeAllFrameData(nullptr);
    CHECK(g_enums == 0 && Frame(g_frameA)->GetData() != nullptr);
    FreeAllFrameData(reinterpret_cast<I3D_scene *>(&g_scene));
    CHECK(g_enums == 1 && g_enumMask == 0xFFFF && g_enumFlags == 0 && g_enumContext == 1);
    CHECK(Frame(g_frameA)->GetData() == nullptr && Frame(g_frameB)->GetData() == nullptr);
    std::printf("protection: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
