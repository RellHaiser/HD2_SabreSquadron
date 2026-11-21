// Human part 75a: the game interface's life cycle (game/interface.cpp lines 119-431: the file's helpers, C_Interface's
// Init, Done, Reset, InitPanels, DonePanels, InitDownPanel, ClearPicks, LoadStateIcons, LoadActionIcons) against the
// original's (0x0049a940, 0x0049b1b0, 0x0049f5e0, 0x0049b280, 0x0049b490, 0x0049b5e0, 0x0049b610, 0x0049b620,
// 0x0049b730; the helpers 0x0049c1a0, 0x0049c1d0, 0x004bf210). MinGW; build_ui.bat; runs ui_diff.exe ("ui_diff: N
// checks, 0 failures"). interface.cpp is compiled into this file; ui_common.inc builds both interfaces and fakes the
// panels, the game's objects and the engine's (see there). Scenarios: from the interface's pristine state (or set up
// by Init), operations of the life cycle in any order, the world changing between them (the model's frames found or
// not, the scene made or not, the viewport, the play mode); the logs (every call out, the interface's fields after
// each operation) and the arena compared.
// Human part 75b (the "Squad" case): the squad and the small setters (lines 119-122, 436-612, 1302-1324, 1446-1523:
// PlaceExplosiveTimer; AddSquadMember .. OnMemberClicked2, FilterInput, IsCapturingInput, AddMessage, SetTimer ..
// RefreshInventory; the original's 0x0049b7e0 .. 0x0049fc10): the team's members, the squad commander's groups
// (CollectGroup through each side's vtable), the active index, the clock (timeGetTime: the port's renamed, the
// original's import slot) and the last member click (each side's static); the panels' fields snapped.
// Human part 75c (the "Draw" case): the drawing (lines 614-788: RenderOverlay, Render, DrawPickOutline; the original's
// 0x0049b9b0, 0x0049b9f0, 0x0049e260): the camera's scripted view and the console's NO_GI from the world, the humans
// as actors (GetBoxCorners through each side's vtable), the game scene's TransformPoints (the points moved off and
// to the screen's edges), the graph's SetTexture and DrawPrimitives (every vertex but the unset specular), the panels'
// fields drawn again between the operations.
// HD2_UI_ROUNDS: scenarios (default 6,000; HD2_UI_QUICK: 2,000); HD2_UI_CASE: one case only; HD2_INTERFACE_ULPS,
// HD2_INTERFACE_SHOW, HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include <mmsystem.h>
// (The interface's clock: the harness's, deterministic.)
DWORD HarnessTimeGetTime();
#define timeGetTime HarnessTimeGetTime

#define INTERFACE_FRAMES 128
#define protected public
#define private public
#include "game/interface.cpp"
#include "game/game_texts.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"
#include "interface_gfx.inc"
#include "ui_common.inc"

// ---- the port's globals the interface reads ----
uint32_t g_gameRoleValue;
alignas(16) static uint8_t g_playModeBlock[2][0x40];
static S_PlayMode *g_playModePointer = reinterpret_cast<S_PlayMode *>(g_playModeBlock[1]);
S_PlayMode *&g_playMode = g_playModePointer;

namespace H {

uint32_t g_seed;
template <class T> void Put(void *b, uint32_t o, T v) { std::memcpy(static_cast<uint8_t *>(b) + o, &v, sizeof v); }

// ---- the world ----
struct S_World {
    uint32_t missing;      // the model's frames missing: a frame name's hash below it (of 2^32)
    int viewport[4];
    int playMode;
    uint32_t role;
    int modelOpen;   // the model's Open succeeds in this search directory (1, 2; 0: none)
    uint32_t state15, state19;   // the driver's states
    int hints;                   // the information screen's hints at the start
    // Part 75b: the team's members (more than six: the port's fix), the squad commander's groups (a bit per human), the
    // active index, the clock and the last member click.
    int memberCount, activeIndex;
    uint32_t groups[6];
    uint32_t time, lastClick;
    // Part 75c: the camera's scripted view, the console's NO_GI, the actors' boxes, the screen points moved.
    bool scripted;
    int noGi;
    float boxScale[HUMANS], shiftX, shiftY;
    bool noScene;
};
S_World g_w, g_wStart;
I3D_frame *FindFrame(const char *name)
{
    const uint32_t h = Fnv(name, 0x1234567u);
    if (h < g_w.missing)
        return nullptr;
    return FrameAt(F_POOL + int(h % uint32_t(FRAMES - F_POOL)));
}
bool RetBool(const char *name, int, bool &value)
{
    if (std::strcmp(name, "C_CameraModule::HasScriptedView") == 0) {
        value = g_w.scripted;
        return true;
    }
    return false;
}
bool RetInt(const char *name, int count, int &value)
{
    if (std::strcmp(name, "GetCheatValue") == 0) {
        value = g_w.noGi;
        return true;
    }
    if (std::strcmp(name, "C_TeamBase::GetMemberCount") == 0) {
        value = g_w.memberCount;
        return true;
    }
    if (std::strcmp(name, "I3D_model::Open") == 0) {
        value = count == g_w.modelOpen ? 0 : int(0x80004005);
        return true;
    }
    return false;
}
void *RetPtr(const char *name, uint32_t)
{
    if (std::strcmp(name, "I3D_scene::GetPrimarySector") == 0)
        return FrameAt(F_SECTOR);
    return nullptr;
}
uint32_t &OrigRole() { return *OriginalAt<uint32_t *>(0x00982100); }
void SetGlobals(int side)
{
    g_gameRole = g_w.role;
    g_noScene = g_w.noScene;
    g_states[0x15] = g_w.state15;
    g_time = g_w.time;
    std::copy(g_w.boxScale, g_w.boxScale + HUMANS, g_boxScale);
    g_shiftX = g_w.shiftX;
    g_shiftY = g_w.shiftY;
    SetActorVts(side);
    Put<int>(OriginalAt<uint8_t *>(0x0089ae78 + 0x1C), 0x2C, g_w.activeIndex);
    Put<int>(&g_teamManager, uint32_t(offsetof(C_TeamManager, m_playerTeam) + offsetof(C_Team, m_active)), g_w.activeIndex);
    g_states[0x19] = g_w.state19;
    OrigRole() = g_w.role;
    for (int s = 0; s < 2; ++s)
        std::memcpy(g_playModeBlock[s] + offsetof(S_PlayMode, mode), &g_w.playMode, 4);
    *OriginalAt<void **>(0x008aea34) = g_playModeBlock[0];
    *reinterpret_cast<void **>(g_uiScene) = g_bigSceneVt;
    Put<void *>(g_uiScene, 0x1F8, FrameAt(F_SECTOR));
    std::memcpy(g_uiScene + 0x224, g_w.viewport, 16);
    std::memcpy(g_uiScene + 0x244, g_w.viewport, 16);
    (void)side;
}
void RandomWorld()
{
    g_w.missing = P(0.5) ? 0u : P(0.5) ? U32() / 4 : U32();
    const bool whole = P(0.6);
    g_w.viewport[0] = whole ? 0 : Pick({0, 1, 5});
    g_w.viewport[1] = whole ? 0 : Pick({0, 2});
    g_w.viewport[2] = whole ? 1024 : Pick({1024, 800, 640});
    g_w.viewport[3] = whole ? 768 : Pick({768, 600, 480});
    g_w.playMode = P(0.5) ? int(PLAY_MODE_CARNAGE) : I(0, 4);
    g_w.role = P(0.7) ? 7u : Pick({0u, 1u, 3u, 6u});
    g_w.noScene = P(0.05);
    g_w.modelOpen = P(0.85) ? 1 : P(0.5) ? 2 : 0;
    g_w.state15 = P(0.5) ? 0u : Pick({1u, 2u, 7u});
    g_w.state19 = P(0.5) ? 0u : Pick({1u, 3u});
    g_w.hints = P(0.5) ? 0 : I(1, 4);
    g_w.memberCount = P(0.9) ? I(0, 6) : Pick({7, 8, -1});
    g_w.activeIndex = P(0.2) ? -1 : I(0, 5);
    for (uint32_t &g : g_w.groups)
        g = U32() & 0xFF;
    g_w.time = P(0.3) ? 0u : U32() % 100000;
    g_w.lastClick = P(0.5) ? g_w.time - uint32_t(I(0, 400)) : P(0.5) ? 0u : U32();
    g_w.scripted = P(0.1);
    g_w.noGi = P(0.9) ? 0 : I(1, 3);
    for (float &b : g_w.boxScale)
        b = P(0.5) ? 1.0f : P(0.5) ? F(0.0f, 0.2f) : Pick({0.0f, 0.05f, 2.0f, 30.0f});
    g_w.shiftX = P(0.8) ? 0.0f : Pick({-3000.0f, 3000.0f, -600.0f, 600.0f});
    // (370..380: a box's bottom within the 10 pixels above the screen's edge.)
    g_w.shiftY = P(0.7) ? 0.0f : Pick({-3000.0f, 3000.0f, -450.0f, 450.0f, 370.0f, 375.0f, 380.0f});
}

// ---- the squad commander's CollectGroup (virtual on both sides: each its vtable) ----
void *g_commanderVt[2][16];
std::vector<int> GroupOf(int group)
{
    std::vector<int> out;
    if (group >= 0 && group < 6)
        for (int h = 0; h < HUMANS; ++h)
            if (g_w.groups[group] >> h & 1)
                out.push_back(h);
    return out;
}
// (The original's: an MSVC vector {allocator, first, last, end}; its memory freed by the caller's delete: malloc'd.)
void __attribute__((thiscall)) O_CollectGroup(void *, void **vector, int group)
{
    Log("squadCommander CollectGroup %d", group);
    const std::vector<int> g = GroupOf(group);
    void **data = g.empty() ? nullptr : static_cast<void **>(std::malloc(4 * g.size()));
    for (size_t k = 0; k < g.size(); ++k)
        data[k] = Hum(g[k]);
    vector[1] = data;
    vector[2] = vector[3] = data ? data + g.size() : nullptr;
}
void __attribute__((thiscall)) P_CollectGroup(void *, std::vector<C_Human *> *out, int group)
{
    Log("squadCommander CollectGroup %d", group);
    for (int h : GroupOf(group))
        out->push_back(static_cast<C_Human *>(Hum(h)));
}
void __attribute__((thiscall)) V_CommanderOther(void *self) { Log("squadCommander other slot"); }

// ---- the original's game, model and sound caches, debug prints ----
alignas(16) uint8_t g_origGame[0x200], g_portGame[0x800];
S_Fake g_gameScene{g_sceneVt};
void *__cdecl O_GetGame() { return g_origGame; }
HRESULT CacheOpen(const char *which, const void *object, const char *name, uint32_t flags)
{
    const bool ok = (RetWord(which) & 7) != 0;
    Log("%s OpenInto %s %s %x -> %d", which, Name(object).c_str(), name, flags, ok);
    return ok ? I3D_OK : HRESULT(0x80004005);
}
HRESULT __attribute__((thiscall)) O_ModelOpen(void *, void *object, const char *name, uint32_t flags, void *, void *, int)
{
    return CacheOpen("models", object, name, flags);
}
HRESULT __attribute__((thiscall)) O_SoundOpen(void *, void *object, const char *name, uint32_t flags, void *, void *, int)
{
    return CacheOpen("sounds", object, name, flags);
}
void __cdecl O_DbgPrintf(const char *format, ...) { Log("dbgPrintf %s", format); }
// (The original's atexit: its static constructors register their destructors with the unstarted CRT; not kept.)
int __cdecl O_Atexit(void (*)()) { return 0; }
// (The original's sprintf takes the unstarted CRT's locks; the interface formats integers only: "%02d".)
int __cdecl O_Sprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsprintf(buffer, format, args);
    va_end(args);
    return n;
}

std::string PartName(const void *p)
{
    const std::string g = GfxName(p);
    if (!g.empty())
        return g;
    const std::string m = MemberName(p);
    if (!m.empty())
        return m;
    if (HumIndex(p) >= 0)
        return "h" + std::to_string(HumIndex(p));
    if (p == &g_sndDriver)
        return "snddriver";
    if (p == &g_gameScene)
        return "gscene";
    if (p == g_origGame || p == g_portGame)
        return "game";
    if (p == g_playModeBlock[0] || p == g_playModeBlock[1])
        return "playMode";
    if (p == g_w.viewport)
        return "viewport";
    if (p == g_uiScene)
        return "uiscene";
    if (p == OriginalAt<uint8_t *>(0x0089ae78 + 0x1C) || p == &g_teamManager.m_playerTeam)
        return "team";
    if (!CommonName(p).empty())
        return CommonName(p);
    for (const void *b : g_boxes)
        if (b && p == b)
            return BoxName(p);
    return "";
}

}  // namespace H

C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGame); }

namespace H {

// ---- each side's interface: its fields snapped ----
template <class T> T At(const void *b, uint32_t o)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(b) + o, sizeof v);
    return v;
}
std::string Snap(int side)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    auto o = [side](uint32_t orig, uint32_t port) { return side ? port : orig; };
    char text[400];
    std::snprintf(text, sizeof text, "ui scene %s camera %s model %s flags %x pick %d %d saved %x forced %d %d down %s picked %s usable %s click %s carnage %s",
                  Name(At<void *>(b, o(0, OF(m_scene)))).c_str(), Name(At<void *>(b, o(4, OF(m_camera)))).c_str(),
                  Name(At<void *>(b, o(8, OF(m_model)))).c_str(), At<uint32_t>(b, o(0xC, OF(m_flags))), At<int>(b, o(0x10, OF(m_pickTime))),
                  At<int>(b, o(0x14, OF(m_pickMisses))), At<uint32_t>(b, o(0x18, OF(m_savedFlags))), At<uint8_t>(b, o(0x20, OF(m_infoForced))),
                  At<uint8_t>(b, o(0x21, OF(m_respawnForced))), Name(At<void *>(b, o(0x24, OF(m_downPanel)))).c_str(),
                  Name(At<void *>(b, o(0x6B8F8, OF(m_pickedActor)))).c_str(), Name(At<void *>(b, o(0x6B8FC, OF(m_usableActor)))).c_str(),
                  Name(side ? static_cast<void *>(g_clickSound) : *OriginalAt<void **>(0x0089bc8c)).c_str(),
                  Name(At<void *>(b, o(0x6B6B0, OF(m_carnage) + offsetof(C_CarnageStats, m_frame)))).c_str());
    std::string s = text;
    s += " click " + std::to_string(side ? s_lastMemberClick : *OriginalAt<uint32_t *>(0x0089bc90));
    for (int k = 0; k < PANEL_FIELD_COUNT; ++k) {
        const S_PanelField &f = PANEL_FIELDS[k];
        const uint8_t *at = b + (side ? f.port : f.orig);
        s += " ";
        if (f.kind == PF_FRAME || f.kind == PF_HUMAN || f.kind == PF_RADAR)
            s += Name(At<void *>(at, 0));
        else if (f.kind == PF_BOOL)
            s += std::to_string(*at);
        else
            s += std::to_string(At<int>(at, 0));
    }
    s += " hints " + std::to_string(side ? int(g_port->m_info.m_hints.size())
                                         : int((At<uint8_t *>(b, 0x6B3A0 + 8) - At<uint8_t *>(b, 0x6B3A0 + 4)) / 4));
    for (const auto &icons : {std::make_pair(o(0x6B6D0, OF(m_stateIcons)), 20), std::make_pair(o(0x6B720, OF(m_stateIcons2)), 40),
                              std::make_pair(o(0x6B7C0, OF(m_actionIcons)), 39)}) {
        s += " |";
        for (int k = 0; k < icons.second; ++k)
            s += " " + Name(At<void *>(b, icons.first + 4 * k));
    }
    return s;
}

// The panels' fields the interface reads inline, as their (faked) Inits leave them: the frames of the carnage panel,
// the inventory, the information screen and the big message (their on flags the frames'), the message log's mode.
void PanelFields(int side)
{
    void *b = side ? static_cast<void *>(g_port) : g_orig;
    const struct {
        uint32_t orig, port;
        int frame;
    } frames[] = {
        {0x6B6B0, OF(m_carnage) + uint32_t(offsetof(C_CarnageStats, m_frame)), 0},
        {0x30 + 4, OF(m_inventory) + uint32_t(offsetof(C_InventoryScreen, m_frame)), 1},
        {0x6B3A0 + 0x14, OF(m_info) + uint32_t(offsetof(C_InfoScreen, m_frame)), 2},
        {0x6B550, OF(m_bigMessage) + uint32_t(offsetof(C_BigMessage, m_frame)), 3},
    };
    for (const auto &f : frames)
        Put<void *>(b, side ? f.port : f.orig, FrameAt(F_POOL + int((g_seed >> (4 * f.frame)) % 20)));
    Put<int>(b, side ? OF(m_messages) + uint32_t(offsetof(C_MessageLog, m_mode)) : 0x2F90 + 0x10, int(g_seed >> 20) % 3);
}

// ---- the scenarios ----
enum {
    OP_INIT, OP_DONE, OP_RESET, OP_INITPANELS, OP_DONEPANELS, OP_INITDOWN, OP_CLEARPICKS, OP_STATEICONS, OP_ACTIONICONS,
    OP_CLEARMOVE, OP_CLEARALL, OP_LOOKAT, OP_FIELDS,
    OP_ADDMEMBER, OP_REMOVEMEMBER, OP_REMOVEMEMBERS, OP_REMOVEFROMMAP, OP_SHOWENEMY, OP_GETSHOWN, OP_SHOWMEMBER, OP_HIGHLIGHT,
    OP_SWITCH, OP_CLICK, OP_CLICK2, OP_FILTER, OP_CAPTURING, OP_ADDMESSAGE, OP_SETTIMER, OP_ROUNDRESULT, OP_COUNTDOWN,
    OP_SHOWMESSAGE, OP_SHOWTIMER, OP_SOUNDDIR, OP_SHOWSCORES, OP_RESPAWN, OP_ISRESPAWN, OP_FLASH, OP_OBJICON, OP_PHOTO,
    OP_REFRESHINV, OP_TIME,
    OP_RENDEROVERLAY, OP_RENDER, OP_OUTLINE, W_PANELS,
    W_WORLD = 100,
};
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0;
};
using T_Bool = bool(__attribute__((thiscall)) *)(void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Arrays = void(__stdcall *)(float *, float *);
using T_Ptr = void(__stdcall *)(void *);
using T_Ctor = void *(__attribute__((thiscall)) *)(void *);
using T_BoolPI = bool(__attribute__((thiscall)) *)(void *, void *, int);
using T_RetBool = bool(__attribute__((thiscall)) *)(void *);
using T_P = void(__attribute__((thiscall)) *)(void *, void *);
using T_PB = void(__attribute__((thiscall)) *)(void *, void *, bool);
using T_RetP = void *(__attribute__((thiscall)) *)(void *);
using T_PI = void(__attribute__((thiscall)) *)(void *, void *, int);
using T_I = void(__attribute__((thiscall)) *)(void *, int);
using T_II = void(__attribute__((thiscall)) *)(void *, int, int);
using T_III = void(__attribute__((thiscall)) *)(void *, int, int, int);
using T_B = void(__attribute__((thiscall)) *)(void *, bool);
using T_Arr = void(__attribute__((thiscall)) *)(void *, float *, float *);
using T_Msg = void(__attribute__((thiscall)) *)(void *, const float *, const char *);
const float COLORS[3][3] = {{255, 0, 0}, {0, 128, 255}, {12.5f, 200, 7}};
const char *const TEXTS[] = {"Squad ready", "", "Enemy spotted near the bridge"};

// Both sides crash: the model's frames looked for without a scene (before Init, after Done).
const char *Skip(int side, const S_Op &op)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    const bool scene = At<void *>(b, side ? OF(m_scene) : 0) != nullptr;
    if (!scene && (op.kind == OP_STATEICONS || op.kind == OP_ACTIONICONS || op.kind == OP_INITDOWN))
        return "no scene";
    // (Render without a scene on the network screen: both draw the null scene.)
    if (op.kind == OP_RENDER && !scene && !(At<uint32_t>(b, side ? OF(m_flags) : 0xC) & INTERFACE_MEMBERS) && (g_w.role & 7) != 7)
        return "the network screen without a scene";
    if (op.kind == OP_HIGHLIGHT && g_w.memberCount > 6)
        return "a squad of more than six highlighted";
    // (Init loading the model where no directory has it: the original keeps the model, the port releases it.)
    const uint32_t flags = At<uint32_t>(b, side ? OF(m_flags) : 0xC);
    // (Reset's Init always runs: its Done clears the flags. A cached model is copied, whatever the directories.)
    const bool opens = op.kind == OP_RESET || (op.kind == OP_INIT && !(flags & INTERFACE_ACTIVE));
    const bool cached = side ? g_modelCache.NumEntries() > 0 : *OriginalAt<uint32_t *>(0x008aedbc) != *OriginalAt<uint32_t *>(0x008aedb8);
    if (opens && !g_w.noScene && g_w.modelOpen == 0 && !cached)
        return "Init without the model";
    return nullptr;
}

void Run(int side, const S_Op &op)
{
    C_Interface *port = g_port;
    uint8_t *orig = g_orig;
    if (const char *why = Skip(side, op)) {
        Log("skipped: %s", why);
        return;
    }
    switch (op.kind) {
    case OP_INIT:
        Log("Init -> %d", side ? port->Init() : OriginalAt<T_Bool>(0x0049a940)(orig));
        break;
    case OP_DONE:
        Log("Done -> %d", side ? port->Done() : OriginalAt<T_Bool>(0x0049b1b0)(orig));
        break;
    case OP_RESET:
        side ? port->Reset() : OriginalAt<T_Void>(0x0049f5e0)(orig);
        break;
    case OP_INITPANELS:
        Log("InitPanels -> %d", side ? port->InitPanels() : OriginalAt<T_Bool>(0x0049b280)(orig));
        break;
    case OP_DONEPANELS:
        side ? port->DonePanels() : OriginalAt<T_Void>(0x0049b490)(orig);
        break;
    case OP_INITDOWN:
        Log("InitDownPanel -> %d", side ? port->InitDownPanel() : OriginalAt<T_Bool>(0x0049b5e0)(orig));
        break;
    case OP_CLEARPICKS:
        side ? port->ClearPicks() : OriginalAt<T_Void>(0x0049b610)(orig);
        break;
    case OP_STATEICONS:
        side ? port->LoadStateIcons() : OriginalAt<T_Void>(0x0049b620)(orig);
        break;
    case OP_ACTIONICONS:
        side ? port->LoadActionIcons() : OriginalAt<T_Void>(0x0049b730)(orig);
        break;
    case OP_CLEARMOVE:
    case OP_CLEARALL: {
        float held[128], pressed[128];
        for (int k = 0; k < 128; ++k) {
            held[k] = float((k * 7 + op.a) % 5) - 1.0f;
            pressed[k] = float((k * 3 + op.a) % 4) * 0.5f;
        }
        if (side)
            op.kind == OP_CLEARMOVE ? ClearMoveControls(held, pressed) : ClearAllControls(held, pressed);
        else
            OriginalAt<T_Arrays>(op.kind == OP_CLEARMOVE ? 0x0049c1a0 : 0x0049c1d0)(held, pressed);
        uint32_t h = 2166136261u;
        for (int k = 0; k < 128; ++k) {
            uint32_t a, b;
            std::memcpy(&a, &held[k], 4);
            std::memcpy(&b, &pressed[k], 4);
            h = (h ^ a) * 16777619u ^ b;
        }
        Log("arrays after %08x", h);
        break;
    }
    case OP_LOOKAT:
        side ? LookAtOnCommandMap(static_cast<C_Human *>(Hum(op.a))) : OriginalAt<T_Ptr>(0x004bf210)(Hum(op.a));
        break;
    case OP_ADDMEMBER:
        Log("AddSquadMember -> %d", side ? port->AddSquadMember(static_cast<C_Human *>(Hum(op.a)), op.b)
                                         : OriginalAt<T_BoolPI>(0x0049b7e0)(orig, Hum(op.a), op.b));
        break;
    case OP_REMOVEMEMBER:
        Log("RemoveSquadMember -> %d", side ? port->RemoveSquadMember(static_cast<C_Human *>(Hum(op.a)), op.b)
                                            : OriginalAt<T_BoolPI>(0x0049b870)(orig, Hum(op.a), op.b));
        break;
    case OP_REMOVEMEMBERS:
        Log("RemoveSquadMembers -> %d", side ? port->RemoveSquadMembers() : OriginalAt<T_RetBool>(0x0049b900)(orig));
        break;
    case OP_REMOVEFROMMAP:
        side ? port->RemoveFromMap(static_cast<C_Human *>(Hum(op.a))) : OriginalAt<T_P>(0x0049b960)(orig, Hum(op.a));
        break;
    case OP_SHOWENEMY:
        side ? port->ShowEnemy(static_cast<C_Human *>(Hum(op.a)), op.b != 0) : OriginalAt<T_PB>(0x0049b980)(orig, Hum(op.a), op.b != 0);
        break;
    case OP_GETSHOWN:
        Log("GetShownSquadMember -> %s", Name(side ? static_cast<void *>(port->GetShownSquadMember()) : OriginalAt<T_RetP>(0x0049f0e0)(orig)).c_str());
        break;
    case OP_SHOWMEMBER:
        side ? port->ShowSquadMember(static_cast<C_Human *>(Hum(op.a))) : OriginalAt<T_P>(0x0049f100)(orig, Hum(op.a));
        break;
    case OP_HIGHLIGHT:
        side ? port->HighlightMembers(static_cast<C_Human *>(Hum(op.a)), op.b) : OriginalAt<T_PI>(0x0049d340)(orig, Hum(op.a), op.b);
        break;
    case OP_SWITCH:
        side ? port->SwitchToMember(op.a) : OriginalAt<T_I>(0x0049c200)(orig, op.a);
        break;
    case OP_CLICK:
        side ? port->OnMemberClicked(op.a) : OriginalAt<T_I>(0x0049dce0)(orig, op.a);
        break;
    case OP_CLICK2:
        side ? port->OnMemberClicked2(op.a) : OriginalAt<T_I>(0x0049dd60)(orig, op.a);
        break;
    case OP_FILTER: {
        float held[128], pressed[128];
        for (int k = 0; k < 128; ++k) {
            held[k] = float((k * 7 + op.a) % 5) - 1.0f;
            pressed[k] = float((k * 3 + op.a) % 4) * 0.5f;
        }
        side ? port->FilterInput(held, pressed) : OriginalAt<T_Arr>(0x0049fc10)(orig, held, pressed);
        uint32_t h = 2166136261u;
        for (int k = 0; k < 128; ++k) {
            uint32_t x, y;
            std::memcpy(&x, &held[k], 4);
            std::memcpy(&y, &pressed[k], 4);
            h = (h ^ x) * 16777619u ^ y;
        }
        Log("arrays after %08x", h);
        break;
    }
    case OP_CAPTURING:
        Log("IsCapturingInput -> %d", side ? port->IsCapturingInput() : OriginalAt<T_RetBool>(0x0049d300)(orig));
        break;
    case OP_ADDMESSAGE: {
        const S_vector color(COLORS[op.a % 3][0], COLORS[op.a % 3][1], COLORS[op.a % 3][2]);
        side ? port->AddMessage(color, TEXTS[op.b % 3]) : OriginalAt<T_Msg>(0x0049d2e0)(orig, &color.x, TEXTS[op.b % 3]);
        break;
    }
    case OP_SETTIMER:
        side ? port->SetTimer(op.a) : OriginalAt<T_I>(0x0049dde0)(orig, op.a);
        break;
    case OP_ROUNDRESULT:
        side ? port->ShowRoundResult(op.a, op.b, op.c) : OriginalAt<T_III>(0x0049ddf0)(orig, op.a, op.b, op.c);
        break;
    case OP_COUNTDOWN:
        side ? port->ShowCountdown(op.a, op.b) : OriginalAt<T_II>(0x0049de10)(orig, op.a, op.b);
        break;
    case OP_SHOWMESSAGE:
        side ? port->ShowMessage(op.a) : OriginalAt<T_I>(0x0049de30)(orig, op.a);
        break;
    case OP_SHOWTIMER:
        side ? port->ShowTimer(op.a, op.b) : OriginalAt<T_II>(0x0049de50)(orig, op.a, op.b);
        break;
    case OP_SOUNDDIR: {
        const S_vector dir(float(op.a % 5) * 0.25f - 0.5f, 0.0f, float(op.a % 3) * 0.5f - 0.5f);
        side ? port->ShowSoundDirection(dir, op.b) : OriginalAt<T_PI>(0x0049de80)(orig, const_cast<float *>(&dir.x), op.b);
        break;
    }
    case OP_SHOWSCORES:
        side ? port->ShowScores(op.a != 0) : OriginalAt<T_B>(0x0049f020)(orig, op.a != 0);
        break;
    case OP_RESPAWN:
        side ? port->ShowRespawnScreen(op.a != 0) : OriginalAt<T_B>(0x0049f040)(orig, op.a != 0);
        break;
    case OP_ISRESPAWN:
        Log("IsRespawnScreenShown -> %d", side ? port->IsRespawnScreenShown() : OriginalAt<T_RetBool>(0x0049f090)(orig));
        break;
    case OP_FLASH:
        side ? port->FlashDamage() : OriginalAt<T_Void>(0x0049f0b0)(orig);
        break;
    case OP_OBJICON:
        side ? port->SetObjectiveIcon(op.a, op.b) : OriginalAt<T_II>(0x0049f0c0)(orig, op.a, op.b);
        break;
    case OP_PHOTO:
        side ? port->ShowPhotoFlash() : OriginalAt<T_Void>(0x0049f5d0)(orig);
        break;
    case OP_REFRESHINV:
        side ? port->RefreshInventory() : OriginalAt<T_Void>(0x0049fbf0)(orig);
        break;
    case OP_TIME:
        g_time += uint32_t(op.a);
        Log("time %u", g_time);
        break;
    case OP_RENDEROVERLAY:
        side ? port->RenderOverlay() : OriginalAt<T_Void>(0x0049b9b0)(orig);
        break;
    case OP_RENDER:
        side ? port->Render() : OriginalAt<T_Void>(0x0049b9f0)(orig);
        break;
    case OP_OUTLINE:
        side ? port->DrawPickOutline() : OriginalAt<T_Void>(0x0049e260)(orig);
        break;
    case W_PANELS:
        // (The panels' fields drawn again: their frames shown or hidden, the menus open, ...)
        g_rng.seed(uint32_t(op.a));
        RandomPanels();
        SetPanelFields(side);
        Log("panels %d", op.a);
        break;
    case OP_FIELDS: {
        // (The test's: the flags, the picks and the forced flags as the other parts leave them.)
        uint8_t *b = side ? reinterpret_cast<uint8_t *>(g_port) : g_orig;
        const uint32_t flags = uint32_t(op.a) & 0x3FF;
        std::memcpy(b + (side ? OF(m_flags) : 0xC), &flags, 4);
        void *picked = Hum(op.b % 9 - 1), *usable = Hum(op.b / 9 % 9 - 1);
        std::memcpy(b + (side ? OF(m_pickedActor) : 0x6B8F8), &picked, 4);
        std::memcpy(b + (side ? OF(m_usableActor) : 0x6B8FC), &usable, 4);
        const int pickTime = op.b % 7 * 20, misses = op.b % 5;
        const uint32_t saved = uint32_t(op.a) >> 3 & 0x7F;
        std::memcpy(b + (side ? OF(m_pickTime) : 0x10), &pickTime, 4);
        std::memcpy(b + (side ? OF(m_pickMisses) : 0x14), &misses, 4);
        std::memcpy(b + (side ? OF(m_savedFlags) : 0x18), &saved, 4);
        b[side ? OF(m_infoForced) : 0x20] = uint8_t(op.b & 1);
        b[side ? OF(m_respawnForced) : 0x21] = uint8_t(op.b >> 1 & 1);
        Log("fields %x %d", flags, op.b);
        break;
    }
    }
}

bool g_squad;   // the squad case (75b) or the life cycle (75a)
bool g_draw;    // the drawing case (75c)
S_Op RandomOp()
{
    static const int SQUAD[] = {OP_ADDMEMBER, OP_ADDMEMBER, OP_REMOVEMEMBER, OP_REMOVEMEMBER, OP_REMOVEMEMBERS, OP_REMOVEFROMMAP,
                                OP_SHOWENEMY, OP_GETSHOWN, OP_SHOWMEMBER, OP_HIGHLIGHT, OP_HIGHLIGHT, OP_HIGHLIGHT, OP_SWITCH,
                                OP_CLICK, OP_CLICK, OP_CLICK2, OP_CLICK2, OP_FILTER, OP_CAPTURING, OP_ADDMESSAGE, OP_SETTIMER,
                                OP_ROUNDRESULT, OP_COUNTDOWN, OP_SHOWMESSAGE, OP_SHOWTIMER, OP_SOUNDDIR, OP_SHOWSCORES, OP_RESPAWN,
                                OP_ISRESPAWN, OP_FLASH, OP_OBJICON, OP_PHOTO, OP_REFRESHINV, OP_TIME, OP_TIME, OP_DONE, OP_FIELDS,
                                W_WORLD};
    static const int DRAW[] = {OP_RENDEROVERLAY, OP_RENDER, OP_RENDER, OP_RENDER, OP_OUTLINE, OP_OUTLINE, OP_FIELDS, OP_FIELDS,
                               W_PANELS, W_PANELS, W_WORLD, OP_DONE};
    S_Op op{g_draw    ? Pick(DRAW)
            : g_squad ? Pick(SQUAD)
                    : Pick({OP_INIT, OP_INIT, OP_DONE, OP_DONE, OP_RESET, OP_INITPANELS, OP_DONEPANELS, OP_INITDOWN, OP_CLEARPICKS,
                            OP_STATEICONS, OP_ACTIONICONS, OP_CLEARMOVE, OP_CLEARALL, OP_LOOKAT, OP_FIELDS, W_WORLD})};
    switch (op.kind) {
    case OP_CLEARMOVE:
    case OP_CLEARALL:
        op.a = I(0, 100);
        break;
    case OP_LOOKAT:
        op.a = P(0.9) ? I(0, HUMANS - 1) : -1;
        break;
    case OP_ADDMEMBER:
    case OP_REMOVEMEMBER:
        op.a = P(0.9) ? I(0, HUMANS - 1) : -1;
        op.b = P(0.9) ? I(0, 5) : Pick({-1, 6, 7});
        break;
    case OP_REMOVEFROMMAP:
    case OP_SHOWMEMBER:
        op.a = P(0.8) ? I(0, HUMANS - 1) : -1;
        break;
    case OP_SHOWENEMY:
        op.a = I(0, HUMANS - 1);
        op.b = P(0.5);
        break;
    case OP_HIGHLIGHT:
        op.a = P(0.5) ? -1 : I(0, HUMANS - 1);
        op.b = P(0.6) ? Pick({3, 5}) : I(0, 6);
        break;
    case OP_SWITCH:
        op.a = P(0.8) ? I(0, 5) : Pick({-1, 6});
        break;
    case OP_CLICK:
    case OP_CLICK2:
        op.a = P(0.8) ? 100 + I(0, 5) : Pick({99, 106, 0, 5});
        break;
    case OP_FILTER:
    case OP_ADDMESSAGE:
        op.a = I(0, 100);
        op.b = I(0, 2);
        break;
    case OP_SETTIMER:
    case OP_SHOWMESSAGE:
        op.a = P(0.3) ? Pick({-1, 0}) : I(1, 0x2000);
        break;
    case OP_ROUNDRESULT:
    case OP_COUNTDOWN:
    case OP_SHOWTIMER:
    case OP_OBJICON:
        op.a = P(0.3) ? Pick({-1, 0, 15, 16}) : I(1, 0x2000);
        op.b = I(-2, 3000);
        op.c = I(0, 9);
        break;
    case OP_SOUNDDIR:
        op.a = I(0, 100);
        op.b = I(0, 4);
        break;
    case OP_SHOWSCORES:
    case OP_RESPAWN:
        op.a = P(0.5);
        break;
    case W_PANELS:
        op.a = int(U32() & 0x7FFFFFFF);
        break;
    case OP_TIME:
        op.a = P(0.3) ? 300 : P(0.5) ? I(0, 299) : I(301, 5000);   // (300: the double click's limit exactly)
        break;
    case OP_FIELDS:
        op.a = int(U32() & 0x3FF);
        op.b = I(0, 80);
        break;
    case W_WORLD:
        op.a = int(U32() & 0x7FFFFFFF);
        break;
    }
    return op;
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
    S_Gfx gfx;
};
S_Run g_runs[2];
void TestScenarios()
{
    g_seed = U32();
    g_retSeed = U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomGfx();
    // (Every face group with a material: the icons' textures are read unchecked, on both sides.)
    for (int i = 0; i < FRAMES; ++i)
        for (int g = 0; g < GROUPS; ++g)
            if (!At<void *>(g_gfx.groups[i][g], 4))
                *reinterpret_cast<void **>(g_gfx.groups[i][g] + 4) = g_gfx.materials[g];
    SetKinds();
    RandomWorld();
    RandomPanels();
    int panelStart[PANEL_FIELD_COUNT];
    std::copy(g_panelValues, g_panelValues + PANEL_FIELD_COUNT, panelStart);
    std::vector<S_Op> ops;
    if (P(0.3)) {
        // (The fields as an earlier life left them: Init sets them.)
        S_Op fields{OP_FIELDS};
        fields.a = int(U32() & 0x3FE);
        fields.b = I(0, 80);
        ops.push_back(fields);
    }
    if (P(0.8))
        ops.push_back(S_Op{OP_INIT});
    for (int n = I(1, 14); n > 0; --n)
        ops.push_back(RandomOp());
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_gfxStart, &g_gfx, sizeof g_gfx);
    g_wStart = g_w;
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
        std::memcpy(&g_gfx, &g_gfxStart, sizeof g_gfx);
        g_w = g_wStart;
        g_log.clear();
        g_boxes.clear();
        g_retCounts.clear();
        std::srand(g_seed);
        std::memcpy(g_orig, g_origPristine, ORIG_SIZE);
        std::memcpy(static_cast<void *>(g_port), g_portPristine, sizeof(C_Interface));
        // (The caches emptied: what an earlier run left; not logged.)
        if (side) {
            g_modelCache.Clear();
            g_soundCache.Clear();
        } else {
            using T_Clear = void(__attribute__((thiscall)) *)(void *);
            for (uint32_t cache : {0x008aeda0u, 0x008aed64u})
                reinterpret_cast<T_Clear>((*OriginalAt<void ***>(cache))[0x10 / 4])(OriginalAt<void *>(cache));
        }
        g_models = g_sounds = 0;
        g_log.clear();
        g_clickSound = nullptr;
        *OriginalAt<void **>(0x0089bc8c) = nullptr;
        SetGlobals(side);
        std::copy(panelStart, panelStart + PANEL_FIELD_COUNT, g_panelValues);
        SetPanelFields(side);
        s_lastMemberClick = g_w.lastClick;
        *OriginalAt<uint32_t *>(0x0089bc90) = g_w.lastClick;
        if (side)
            g_port->m_info.m_hints.assign(size_t(g_w.hints), 0x1C80);
        else {
            int *hints = g_w.hints ? static_cast<int *>(std::malloc(4 * size_t(g_w.hints))) : nullptr;
            for (int k = 0; k < g_w.hints; ++k)
                hints[k] = 0x1C80;
            Put<int *>(g_orig, 0x6B3A0 + 4, hints);
            Put<int *>(g_orig, 0x6B3A0 + 8, hints ? hints + g_w.hints : nullptr);
            Put<int *>(g_orig, 0x6B3A0 + 0xC, hints ? hints + g_w.hints : nullptr);
        }
        for (const S_Op &op : ops) {
            Log("-- %d %d %d", op.kind, op.a, op.b);
            if (op.kind == W_WORLD) {
                std::mt19937 rng(uint32_t(op.a));
                g_rng.seed(uint32_t(op.a));
                RandomWorld();
                SetGlobals(side);
            } else
                Run(side, op);
            g_log.push_back(Snap(side));
        }
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
        std::memcpy(&g_runs[side].gfx, &g_gfx, sizeof g_gfx);
    }
    // (+0xDC, m_scaleW: the inline SetScale's stack word; put back from the start on both sides.)
    for (int i = 0; i < FRAMES; ++i)
        for (S_Run &r : g_runs)
            std::memcpy(r.arena.frames[i] + 0xDC, g_arenaStart.frames[i] + 0xDC, 4);
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ++checks;
    if (std::memcmp(&g_runs[0].gfx, &g_runs[1].gfx, sizeof g_gfx) != 0)
        CHECK(false, "the graphics differ");
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

// ---- the port's fix, alone: Init without the model (the original kept the model and leaked it; the port releases
// it); and the destructor (the game's one interface lives to the end) ----
void TestFixes()
{
    g_side = 1;
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomGfx();
    SetKinds();
    RandomWorld();
    g_w.noScene = false;
    g_w.modelOpen = 0;
    SetGlobals(1);
    g_modelCache.Clear();
    g_models = g_sounds = 0;
    std::memcpy(static_cast<void *>(g_port), g_portPristine, sizeof(C_Interface));
    g_log.clear();
    g_retCounts.clear();
    CHECK(!g_port->Init(), "Init without the model: failed");
    CHECK(g_port->m_scene == nullptr && g_port->m_model == nullptr && g_port->m_flags == 0, "Init without the model: nothing kept");
    const std::string model = Name(FrameAt(F_MODEL)) + " I3D_frame Release";
    CHECK(std::count(g_log.begin(), g_log.end(), model) + std::count(g_log.begin(), g_log.end(), Name(FrameAt(F_MODEL)) + " Release") == 1,
          "Init without the model: the model released");
    CHECK(std::count(g_log.begin(), g_log.end(), std::string("uiscene I3D_scene Release")) == 1 ||
              std::count(g_log.begin(), g_log.end(), std::string("uiscene other slot")) == 1,
          "Init without the model: the scene released");
    // HighlightMembers with a squad of eight: the six panels only (the original went past them).
    std::memcpy(static_cast<void *>(g_port), g_portPristine, sizeof(C_Interface));
    g_w.memberCount = 8;
    for (int group : {3, 5, 0}) {
        g_log.clear();
        g_port->HighlightMembers(nullptr, group);
        int selected = 0;
        for (const std::string &l : g_log)
            selected += l.find("C_MemberPanel::SetSelected") != std::string::npos;
        CHECK(selected == 6, "HighlightMembers with eight members: %d panels", selected);
    }
    // (The destructor: a second interface made and destroyed.)
    auto *other = new (_aligned_malloc(sizeof(C_Interface), 16)) C_Interface();
    other->~C_Interface();
    _aligned_free(other);
    CHECK(true, "destructor");
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    g_retPtr = RetPtr;
    g_retInt = RetInt;
    g_retBool = RetBool;
    SetUpGfx();
    SetUpEngine();
    SetUpCameraModule();
    SetUpDrawing();
    for (int side = 0; side < 2; ++side) {
        for (void *&v : g_commanderVt[side])
            v = reinterpret_cast<void *>(V_CommanderOther);
        *reinterpret_cast<void ***>(g_squadCommander[side]) = g_commanderVt[side];
    }
    g_commanderVt[0][0x08 / 4] = reinterpret_cast<void *>(O_CollectGroup);
    g_commanderVt[1][Slot(&C_SquadCommander::CollectGroup) / 4] = reinterpret_cast<void *>(P_CollectGroup);
    // (The original's timeGetTime: its import slot.)
    {
        void **slot = OriginalAt<void **>(0x0080f3f8);
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        *slot = reinterpret_cast<void *>(O_TimeGetTime);
        VirtualProtect(slot, 4, old, &old);
    }
    Put<void *>(g_origGame, 0x18, &g_gameScene);
    Put<void *>(g_portGame, offsetof(C_game, m_scene), &g_gameScene);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        IFAKE_REDIRECTS
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x007e531a, reinterpret_cast<const void *>(O_ISndGetDriver)},
        {0x007e52d8, reinterpret_cast<const void *>(O_DbgPrintf)},
        {0x00464e90, reinterpret_cast<const void *>(O_GetCameraModule)},
        {0x004563e0, reinterpret_cast<const void *>(O_GetSquadCommander)},
        {0x00720f50, reinterpret_cast<const void *>(O_GetCheatValue)},
        {0x004a26d0, reinterpret_cast<const void *>(O_InventoryRefresh)},
        {0x007e562e, reinterpret_cast<const void *>(O_Atexit)},
        {0x007e6334, reinterpret_cast<const void *>(O_Sprintf)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // The model and sound caches (real on both sides: the original's Init inlines the model cache's OpenInto): the
    // original's built by its static initialisers' constructors; two search directories each.
    OriginalAt<void (*)()>(0x006d5030)();
    OriginalAt<void (*)()>(0x006d5370)();
    using T_AddDir = void(__attribute__((thiscall)) *)(void *, const char *);
    for (const char *dir : {"data\\", "maps\\"}) {
        for (uint32_t cache : {0x008aeda0u, 0x008aed64u})
            reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(cache))[0x18 / 4])(OriginalAt<void *>(cache), dir);
        g_modelCache.AddDir(dir);
        g_soundCache.AddDir(dir);
    }
    // The interfaces, built once: the original's by its constructor, the port's by its own.
    g_orig = static_cast<uint8_t *>(std::calloc(1, ORIG_SIZE));
    OriginalAt<T_Ctor>(0x0049a420)(g_orig);
    g_origPristine = static_cast<uint8_t *>(std::malloc(ORIG_SIZE));
    std::memcpy(g_origPristine, g_orig, ORIG_SIZE);
    g_port = new (_aligned_malloc(sizeof(C_Interface), 16)) C_Interface();
    g_portPristine = static_cast<uint8_t *>(std::malloc(sizeof(C_Interface)));
    std::memcpy(g_portPristine, static_cast<void *>(g_port), sizeof(C_Interface));
    g_log.clear();
    g_boxes.clear();
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("ui_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_UI_QUICK") ? 2000 : 6000;
    if (const char *r = std::getenv("HD2_UI_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "Life";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    g_case = "Squad";
    g_squad = true;
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    g_squad = false;
    g_case = "Draw";
    g_draw = true;
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    g_draw = false;
    g_case = "Fixes";
    for (g_round = 0; g_round < 50; ++g_round)
        TestFixes();
    std::printf("ui_diff: %d checks, %d failures\n", checks, failures);
    // (The port's caches emptied here: their static destructors would release into the fakes after the log is gone.)
    g_modelCache.Clear();
    g_soundCache.Clear();
    return failures != 0;
}
