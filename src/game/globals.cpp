// Globals of the original's data that ported code declares and that have no better home yet, and small functions that nothing else owns.
#include <cstdlib>
#include <cstring>

#include "game/app.h"
#include "game/console.h"
#include "game/game.h"
#include "game/game_menu.h"
#include "game/human_movement.h"
#include "game/melee_tracker.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/team.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"
#include "net/bitstream.h"
#include "net/net_game.h"

// All of these start as zero in the original (its .bss).
bool g_scriptModuleRestarting;                          // 0x008693e0
char g_netDefaultSessionText[16];                       // 0x00867a90 (the original's shared empty string)
char g_serverName[0x100];                               // 0x00981e70 (nothing writes it; size not known)
C_FrameListener **g_frameListenersBegin;                // 0x008aeb4c (nothing adds any)
C_FrameListener **g_frameListenersEnd;                  // 0x008aeb50
I3D_scene *g_carScene;                                  // 0x0089c1d0
float g_levelMinY;                                      // 0x0097ca34
float g_netSceneTop;                                    // 0x0097ca44
int g_localPlayerSlot;                                  // 0x00982104
int g_netPlayerCount;                                   // 0x00982df0 (set by C_NetQuantizationTable::Set: its members)
S_NetPlayer *g_netPlayerTable;                          // 0x00982dec
S_MaterialDef *g_materials;                             // 0x0097b9b4
uint32_t g_materialCount;                               // 0x0097b9b8
// Quirk kept: nothing writes this in the original, so the scale followers' formations get is zero: all their places are the leader's, then kept 1.2 m apart.
S_vector g_defaultFormationScale(0.0f, 0.0f, 0.0f);     // 0x0089ae50
uint32_t g_graphFlags;                                  // 0x00981e6c
void *g_netQueryThread;                                 // 0x009820f8

// ---- small functions ------------------------------------------------------------------------------------------------------------

// (0x00475190) Between 0 and `max`.
float RandomFloat(float max)
{
    if (max == 0.0f)
        return 0.0f;
    return static_cast<float>(std::rand()) * max * 3.05176e-05f;
}

// (0x006ed2a0) A message box, `flags` 1..4 its buttons: 1 yes or retry, 0 no, 2 cancel, -1 anything else.
int ShowMessage(const char *text, const char *title, unsigned int flags)
{
    UINT type = 0;
    switch (flags) {
    case 1: type = 4; break;
    case 2: type = 0x203; break;
    case 3: type = 0x105; break;
    case 4: type = 0x101; break;
    default: break;
    }
    if (!title)
        title = "<no title>";
    if (!text)
        text = "<no text>";
    IGraph *const graph = GetIGraph();
    switch (MessageBoxA(graph ? graph->GetHWND() : nullptr, text, title, type)) {
    case IDCANCEL: return 2;
    case IDRETRY:
    case IDYES: return 1;
    case IDNO: return 0;
    default: return -1;
    }
}

// (0x0069f4b0)
C_Console *GetConsole()
{
    static C_Console *s_console = nullptr;              // 0x0098c100
    if (!s_console) {
        s_console = new C_Console();
        s_console->Create();
    }
    return s_console;
}

// (0x0066b680) The characters typed, to `maxLength` wide characters. False: none.
bool ReadUnicodeInput(wchar_t *buffer, int maxLength)
{
    char typed[0x400];
    int count = 0;
    buffer[0] = 0;
    IGraph *const graph = GetIGraph();
    do {
        typed[count] = static_cast<char>(graph->ReadChar());
        ++count;
    } while (typed[count - 1] != 0 && count < 0x3FF);
    typed[count] = 0;                                   // (Fix: terminated when the input fills it.)
    MultiByteToWideChar(CP_ACP, 0, typed, -1, buffer, maxLength);
    return count > 1;
}

// (0x0069e9b0)
void ShowGameMenuMessage(int textId)
{
    C_GameState *const state = GetGameApp()->m_state;
    if (textId >= 0 && state && state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU)
        static_cast<C_GameMenu *>(state)->ShowMessage(textId, -1);
}

// (0x005c6c70) The original's countdown 0x008ac5b8 is never set above zero.
void MeleeTrackerBarkTurn(C_MeleeTracker *meleeTracker)
{
    S_vector dir = meleeTracker->GetDirection();
    RotateAboutY(dir, 2.8f);
    const float facing[4] = {dir.x, dir.y, dir.z, meleeTracker->GetDirection4().w};
    meleeTracker->GetMovement().SetFacing(facing);
}

// (Inline in C_GameProfile's constructor; 0x006b50c0. Here, not in mp_settings.cpp: harnesses build that with a stub of the struct.)
S_MultiplayerSettings::S_MultiplayerSettings()
    : setup(nullptr)
{
    Reset();
}

S_MultiplayerSettings::~S_MultiplayerSettings()
{
    Clear();
}

// ---- the debug statistics' font --------------------------------------------------------------------------------------------------
// The original keeps a list of statistics to draw (0x008ca4bc, 0x006e6fe0..0x006e8830); nothing ever adds to it, so all that it does is below.

namespace {
IImage *g_debugFont;                                    // 0x008ca4c8
uint32_t g_debugFontMatrix[0x500];                      // 0x008c90b8
}  // namespace

// (0x006e8960, 0x006e8900)
void InitUnknown8CA4C8()
{
    if (GetIGraph()->LoadTexture("editfont.bmp", nullptr, 8, &g_debugFont) < 0) {
        g_debugFont = nullptr;
        return;
    }
    std::memset(g_debugFontMatrix, 0, sizeof g_debugFontMatrix);
    const int file = dtaOpen("tables\\fnt_matrix.bin", 0);
    if (file != -1) {
        dtaRead(file, g_debugFontMatrix, sizeof g_debugFontMatrix);
        dtaClose(file);
    }
}

// (0x006e8970)
void FreeUnknown8CA4C8()
{
    if (g_debugFont)
        g_debugFont->Release();
    g_debugFont = nullptr;
}

// (0x006e8860) Its flag 0x00844044 is 1 for ever, and it leaves the texture set.
void RenderDebugStats()
{
    I3D_driver *const driver = I3DGetDriver();
    const uint32_t state = driver->GetState(3);
    driver->SetState(3, 0);
    GetIGraph()->SetTexture(g_debugFont, 0);
    driver->SetState(3, state);
}
