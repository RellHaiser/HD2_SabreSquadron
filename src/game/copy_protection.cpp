// See copy_protection.h. Beside it in the original, the game data of an engine frame (0x006e9ea0..0x006e9f40).
#include "game/copy_protection.h"

#include <windows.h>

#include <cstdlib>

#include "game/pending.h"
#include "ls3d/i3d.h"

void InitProtectionTables()
{
    std::srand(GetTickCount());
}

// ---- a frame's game data -------------------------------------------------------------------------------------------------

void SetFrameData(I3D_frame *frame, const S_FrameData &data)
{
    if (frame->GetData()) {
        *frame->GetData() = data;
        return;
    }
    frame->SetData(new S_FrameData(data));
}

void FreeFrameData(I3D_frame *frame)
{
    if (frame->GetData()) {
        delete frame->GetData();
        frame->SetData(nullptr);
    }
}

namespace {

uint32_t __stdcall FreeFrameDataCallback(I3D_frame *frame, void *)   // 0x006e9f40
{
    FreeFrameData(frame);
    return 0;
}

}  // namespace

void FreeAllFrameData(I3D_scene *scene)
{
    if (scene)
        scene->EnumFrames(FreeFrameDataCallback, nullptr, 0xFFFF, 0);
}
