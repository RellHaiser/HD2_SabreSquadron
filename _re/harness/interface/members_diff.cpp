// Human part 65: the squad members' panels (game/interface_members.cpp) against the original's (0x004b30e0..0x004b48d0).
// MinGW; build_members.bat; runs members_diff.exe ("members_diff: N checks, 0 failures"). interface_members.cpp is compiled
// into this file; the kit (interface_kit.inc) fakes the frames, the scene, the driver and the graph. Scenarios of
// operations (Init, SetHuman, SetSelected, Update, Render, Relayout, Done; between them the humans' answers and fields, the
// active soldier, the game's role and type, the VIP slot, the co-operative flag) run from one start on each side (the
// original's panel a block given the port's start values, the port's its object), every call out logged by the same
// fakes, the panel's fields snapped after each operation, the frames compared at the end.
// Faked on both sides with the same loggers: the text boxes (the port's C_TextBox defined here; the original builds its
// own inline (operator new, the graph's screen, AcquireFontManager 0x0063df00): its AttachTo 0x00644da0, SetText
// 0x006446e0, SetTextId 0x00644520, AddText 0x00644830, Clear 0x00644c80, Render 0x00648200 and deleting destructor
// 0x0048ecd0 redirected; boxes named by their first appearance), the parts' meshes, levels, face groups (DuplicateMaterial)
// and materials (SetAmbientColor, SetTexture, ResolveTexture), the graph's LoadTexture, the humans' methods (the original's
// redirected; role, health, frame, net id, containers, profile, inventory flag and scale in raw blocks at both sides'
// offsets), the uniform's container (GetItem: each side its own object, the vtables differ by g++'s second destructor
// slot), the texts (C_GameTexts::Get 0x006e5380: "T" + the id), the rank names (the original's 0x008ae960), the
// multiplayer settings (the original's pointer 0x008aea30), the profile's IsCooperativeGame (0x006b69d0), the game's
// GetVipSlot (0x0069e340), the active player (0x0045d1d0). C_fstr and the CRT's printf: each side's own.
// HD2_MEMBERS_ROUNDS: scenarios (default 30,000; HD2_MEMBERS_QUICK: 5,000); HD2_INTERFACE_ULPS, HD2_INTERFACE_SHOW,
// HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/interface_members.cpp"
#include "game/game_texts.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"

namespace H {

// ---- the frames: the panel, its parts, the blank icon, the humans' ----
const char *const PARTS[] = {"player0*", "ear*", "select*", "photo*", "healthbar*", "healthcr*", "health0*", "stamina0*", "staminabar*",
                             "disguise0*", "disguisebar*", "command*", "mind*", "visibility*", "AImode*", "silence*", "medic*", "mp_vip*"};
constexpr int PART_COUNT = 18;
constexpr int PANEL = 0, PART_FIRST = 1, BLANK = 19, HUMAN_FRAME = 20;   // f1..f18 the parts, f20..f23 the humans'
constexpr int HUMANS = 4;
bool g_partMissing[PART_COUNT];
int g_panelIndex;   // the scene's "Soldier%02d" answered for this index (others: none)
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    if (FrameIndex(f) != PANEL)
        return nullptr;
    for (int k = 0; k < PART_COUNT; ++k)
        if (std::strcmp(name, PARTS[k]) == 0)
            return g_partMissing[k] ? nullptr : FrameAt(PART_FIRST + k);
    return nullptr;
}
I3D_frame *FindFrame(const char *name)
{
    char panel[16];
    std::snprintf(panel, sizeof panel, "Soldier%02d", g_panelIndex);
    if (std::strcmp(name, panel) == 0)
        return FrameAt(PANEL);
    if (std::strcmp(name, "univerz") == 0)
        return FrameAt(BLANK);
    return nullptr;
}

// ---- the meshes, levels, face groups, materials, textures (per frame; materials from a pool) ----
struct S_Gfx {
    uint8_t meshes[FRAMES][0x10];
    uint8_t levels[FRAMES][0x80];   // +0x5C the face groups
    uint8_t groups[FRAMES][0x10];   // vtable, material
    uint8_t materials[48][0x40];    // vtable, +0x08 flags, +0x0C texture
    uint8_t textures[24][0x10];     // 0..5 the state icons, 6..11 the mode icons, 12 the blank, 13 death, 14..17 faces, 18.. others
    int nextMaterial;
};
S_Gfx g_gfx, g_gfxStart;
void *g_meshVt[8], *g_groupVt[16], *g_materialVt[24], *g_textureVt[4];
I3D_texture *g_stateIcons[6], *g_modeIcons[6];
std::string GfxName(const void *p)
{
    const auto *b = static_cast<const uint8_t *>(p);
    const auto in = [b](const void *base, size_t size) { return b >= static_cast<const uint8_t *>(base) && b < static_cast<const uint8_t *>(base) + size; };
    char text[32];
    if (in(g_gfx.materials, sizeof g_gfx.materials))
        std::snprintf(text, sizeof text, "m%d", int((b - g_gfx.materials[0]) / 0x40));
    else if (in(g_gfx.textures, sizeof g_gfx.textures))
        std::snprintf(text, sizeof text, "t%d", int((b - g_gfx.textures[0]) / 0x10));
    else if (in(g_gfx.groups, sizeof g_gfx.groups))
        std::snprintf(text, sizeof text, "g%d", int((b - g_gfx.groups[0]) / 0x10));
    else if (in(g_gfx.meshes, sizeof g_gfx.meshes))
        std::snprintf(text, sizeof text, "mesh%d", int((b - g_gfx.meshes[0]) / 0x10));
    else
        return "";
    return text;
}
void *__stdcall M_GetLevel(void *mesh, int index)
{
    Log("%s GetLevel %d", Name(mesh).c_str(), index);
    return g_gfx.levels[(static_cast<uint8_t *>(mesh) - g_gfx.meshes[0]) / 0x10];
}
void __stdcall G_DuplicateMaterial(void *group)
{
    const int m = g_gfx.nextMaterial < 48 ? g_gfx.nextMaterial++ : 47;
    Log("%s DuplicateMaterial -> m%d", Name(group).c_str(), m);
    *reinterpret_cast<void **>(static_cast<uint8_t *>(group) + 4) = g_gfx.materials[m];
}
void __stdcall MAT_SetAmbient(void *m, const S_vector &c) { Log("%s SetAmbientColor %s", Name(m).c_str(), VecText(c).c_str()); }
void __stdcall MAT_SetTexture(void *m, void *t)
{
    Log("%s SetTexture %s", Name(m).c_str(), Name(t).c_str());
    *reinterpret_cast<void **>(static_cast<uint8_t *>(m) + 0xC) = t;
}
void __stdcall MAT_Resolve(void *m)
{
    Log("%s ResolveTexture", Name(m).c_str());
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(m) + 8) &= ~0x4000000u;
}
void __stdcall GFX_Other(void *p) { Log("%s other slot", Name(p).c_str()); }
void __stdcall F_DuplicateMesh(void *f) { Log("%s DuplicateMesh", Name(f).c_str()); }
int g_loadResult;
int __stdcall G_LoadTexture(void *, const char *name, const char *alpha, uint32_t mode, void **out)
{
    Log("graph LoadTexture %s %s %x", name, alpha ? alpha : "null", mode);
    if (g_loadResult >= 0)
        *out = g_gfx.textures[13];
    return g_loadResult;
}
uint32_t __stdcall TEX_Release(void *t)
{
    Log("%s Release", Name(t).c_str());
    return 0;
}
int __stdcall G_ScrnX(void *)
{
    Log("graph ScrnX");
    return 1024;
}
int __stdcall G_ScrnY(void *)
{
    Log("graph ScrnY");
    return 768;
}

// ---- the text boxes ----
std::vector<const void *> g_boxes;   // this side's, in order of first appearance
std::string BoxName(const void *b)
{
    auto it = std::find(g_boxes.begin(), g_boxes.end(), b);
    if (it == g_boxes.end()) {
        g_boxes.push_back(b);
        it = g_boxes.end() - 1;
    }
    return "box" + std::to_string(it - g_boxes.begin());
}
std::string Text(const C_str &s) { return s.m_rep ? "'" + std::string(s.c_str()) + "'" : "(null)"; }
void BoxAttach(const void *b, const void *scene, const void *frame) { Log("%s AttachTo %s %s", BoxName(b).c_str(), Name(scene).c_str(), Name(frame).c_str()); }
void BoxSetText(const void *b, const C_str &t, int style, int index) { Log("%s SetText %s %d %d", BoxName(b).c_str(), Text(t).c_str(), style, index); }
void BoxSetTextId(const void *b, int id, int style, int index) { Log("%s SetTextId %d %d %d", BoxName(b).c_str(), id, style, index); }
void BoxAddText(const void *b, const C_str &t, int style) { Log("%s AddText %s %d", BoxName(b).c_str(), Text(t).c_str(), style); }
void BoxClear(const void *b) { Log("%s Clear", BoxName(b).c_str()); }
void BoxRender(const void *b) { Log("%s Render", BoxName(b).c_str()); }
// (A deleted box gives up its name: a new box at its address is another.)
void BoxDelete(const void *b)
{
    Log("%s delete", BoxName(b).c_str());
    *std::find(g_boxes.begin(), g_boxes.end(), b) = nullptr;
}
void __attribute__((thiscall)) OB_AttachTo(void *b, void *scene, void *frame) { BoxAttach(b, scene, frame); }
bool __attribute__((thiscall)) OB_SetText(void *b, const C_str &t, int style, int index)
{
    BoxSetText(b, t, uint8_t(style), index);
    return true;
}
bool __attribute__((thiscall)) OB_SetTextId(void *b, int id, int style, int index)
{
    BoxSetTextId(b, id, style, index);
    return true;
}
bool __attribute__((thiscall)) OB_AddText(void *b, const C_str &t, int style)
{
    BoxAddText(b, t, uint8_t(style));
    return true;
}
void __attribute__((thiscall)) OB_Clear(void *b) { BoxClear(b); }
void __attribute__((thiscall)) OB_Render(void *b) { BoxRender(b); }
void *__attribute__((thiscall)) OB_Delete(void *b, unsigned flags)
{
    BoxDelete(b);
    if (flags & 1)
        std::free(b);
    return b;
}
void *__cdecl O_AcquireFonts()
{
    Log("AcquireFontManager");
    return &g_scene;   // (a pointer the snaps do not look into)
}

// ---- the humans, their items, the uniform's container ----
alignas(16) uint8_t g_humans[HUMANS][0x800], g_humansStart[HUMANS][0x800];
alignas(16) uint8_t g_items[4][0x40], g_defs[4][0x100], g_profiles[HUMANS][0x40];
struct S_Answers {
    int rank, team, health, stamina, disguise, statusIcon, modeIcon, orders, firstAid;
    bool localPlayer;
    float visibility;
    int hands;           // GetHandsWeapon: an item (0..3) or -1
    uint32_t silenced;   // bits: 0x16, 7, 0x104 found
};
S_Answers g_answers[HUMANS], g_answersStart[HUMANS];
const char *const LAST_NAMES[HUMANS] = {"Smith", "Jones", "O'Hara", ""};
const char *const SHORT_NAMES[HUMANS] = {"Pvt", "Sgt", "Cpl", "X"};
int HumanIndex(const void *h)
{
    for (int i = 0; i < HUMANS; ++i)
        if (h == g_humans[i])
            return i;
    return -1;
}
C_Human *HumanAt(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Human *>(g_humans[i]); }
const S_Answers &A(const void *h) { return g_answers[HumanIndex(h)]; }
#define HLOG(what) Log("h%d " what, HumanIndex(h))
int HRank(const void *h)
{
    HLOG("GetProfileUnknown24");
    return A(h).rank;
}
const char *HLastName(const void *h)
{
    HLOG("GetLastName");
    return LAST_NAMES[HumanIndex(h)];
}
const char *HShortName(const void *h)
{
    HLOG("GetShortName");
    return SHORT_NAMES[HumanIndex(h)];
}
int HTeam(const void *h, bool pending)
{
    Log("h%d GetTeam %d", HumanIndex(h), pending);
    return A(h).team;
}
void *HHands(const void *h)
{
    HLOG("GetHandsWeapon");
    return A(h).hands < 0 ? nullptr : g_items[A(h).hands];
}
void *HFindItem(const void *h, int type, int *container)
{
    Log("h%d FindItem %x %s", HumanIndex(h), type, container ? "ptr" : "null");
    const uint32_t bit = type == 0x16 ? 1 : type == 7 ? 2 : type == 0x104 ? 4 : 0;
    return (A(h).silenced & bit) ? g_items[3] : nullptr;
}
int HStat(const void *h, int stat)
{
    Log("h%d GetStat %d", HumanIndex(h), stat);
    return stat == STAT_FIRST_AID ? A(h).firstAid : 0;
}
int HHealth(const void *h)
{
    HLOG("GetHealthPercent");
    return A(h).health;
}
int HStamina(const void *h)
{
    HLOG("GetStaminaPercent");
    return A(h).stamina;
}
int HDisguise(const void *h)
{
    HLOG("GetDisguisePercent");
    return A(h).disguise;
}
float HVisibility(const void *h)
{
    HLOG("GetVisibility");
    return A(h).visibility;
}
int HOrders(const void *h)
{
    HLOG("GetOrderCount");
    return A(h).orders;
}
int HStatusIcon(const void *h)
{
    HLOG("GetSquadStatusIcon");
    return A(h).statusIcon;
}
int HModeIcon(const void *h)
{
    HLOG("GetSquadModeIcon");
    return A(h).modeIcon;
}
bool HLocalPlayer(const void *h)
{
    HLOG("IsLocalPlayer");
    return A(h).localPlayer;
}
int __attribute__((thiscall)) O_Rank(void *h) { return HRank(h); }
const char *__attribute__((thiscall)) O_LastName(void *h) { return HLastName(h); }
const char *__attribute__((thiscall)) O_ShortName(void *h) { return HShortName(h); }
int __attribute__((thiscall)) O_Team(void *h, bool pending) { return HTeam(h, pending); }
void *__attribute__((thiscall)) O_Hands(void *h) { return HHands(h); }
void *__attribute__((thiscall)) O_FindItem(void *h, int type, int *container) { return HFindItem(h, type, container); }
int __attribute__((thiscall)) O_Stat(void *h, int stat) { return HStat(h, stat); }
int __attribute__((thiscall)) O_Health(void *h) { return HHealth(h); }
int __attribute__((thiscall)) O_Stamina(void *h) { return HStamina(h); }
int __attribute__((thiscall)) O_Disguise(void *h) { return HDisguise(h); }
float __attribute__((thiscall)) O_Visibility(void *h) { return HVisibility(h); }
int __attribute__((thiscall)) O_Orders(void *h) { return HOrders(h); }
int __attribute__((thiscall)) O_StatusIcon(void *h) { return HStatusIcon(h); }
int __attribute__((thiscall)) O_ModeIcon(void *h) { return HModeIcon(h); }
bool __attribute__((thiscall)) O_LocalPlayer(void *h) { return HLocalPlayer(h); }

// The uniform's container (each side its own: GetItem the original's slot 0x40, the port's 0x44) and its item.
int g_uniform;   // an item (0..3) or -1
void *ContainerItem(const char *side)
{
    Log("container GetItem");
    (void)side;
    return g_uniform < 0 ? nullptr : g_items[g_uniform];
}
void *__attribute__((thiscall)) OC_GetItem(void *) { return ContainerItem("orig"); }
void *__attribute__((thiscall)) PC_GetItem(void *) { return ContainerItem("port"); }
void __attribute__((thiscall)) C_Other(void *) { Log("container other slot"); }
void *g_origContainerVt[32], *g_portContainerVt[32];
S_Fake g_origContainer{g_origContainerVt}, g_portContainer{g_portContainerVt};
void *g_origContainers[8], *g_portContainers[8];

// ---- the texts, the ranks, the game, the profile, the active player ----
C_str TextOf(int id)
{
    Log("texts Get %x", id);
    char text[16];
    std::snprintf(text, sizeof text, "T%x", id);
    return C_str(text);
}
void *__attribute__((thiscall)) O_TextsGet(void *, void *out, int id)
{
    C_str s = TextOf(id);
    *static_cast<void **>(out) = s.m_rep;
    s.m_rep = nullptr;
    return out;
}
int g_active, g_vipSlot;
bool g_coop;
alignas(16) uint8_t g_game[0x100], g_settings[0x400];
void *__attribute__((thiscall)) O_GetActivePlayer(void *) { return HumanAt(g_active); }
int __attribute__((thiscall)) O_GetVipSlot(void *game)
{
    Log("%s GetVipSlot", Name(game).c_str());
    return g_vipSlot;
}
bool __attribute__((thiscall)) O_IsCoop(void *)
{
    Log("profile IsCooperativeGame");
    return g_coop;
}

std::string PartName(const void *p)
{
    const std::string g = GfxName(p);
    if (!g.empty())
        return g;
    if (HumanIndex(p) >= 0)
        return "h" + std::to_string(HumanIndex(p));
    for (int i = 0; i < 4; ++i) {
        if (p == g_items[i])
            return "item" + std::to_string(i);
        if (p == g_defs[i])
            return "def" + std::to_string(i);
    }
    if (p == g_game)
        return "game";
    for (const void *b : g_boxes)
        if (p == b)
            return BoxName(p);
    return "";
}

uint32_t &OrigRole() { return *OriginalAt<uint32_t *>(0x00982100); }

}  // namespace H

// ---- the port's side ----
C_TextBox::C_TextBox()
{
    m_screenWidth = static_cast<int16_t>(GetIGraph()->ScrnX());
    m_screenHeight = static_cast<int16_t>(GetIGraph()->ScrnY());
    m_fonts = static_cast<C_FontManager *>(H::O_AcquireFonts());
}
C_TextBox::~C_TextBox() { H::BoxDelete(this); }
void C_TextBox::UpdateScrollRange() { H::Log("%s UpdateScrollRange", H::BoxName(this).c_str()); }
void C_TextBox::Render() { H::BoxRender(this); }
bool C_TextBox::ScrollTo(int line)
{
    H::Log("%s ScrollTo %d", H::BoxName(this).c_str(), line);
    return true;
}
void C_TextBox::AttachTo(I3D_scene *scene, I3D_frame *visual) { H::BoxAttach(this, scene, visual); }
bool C_TextBox::SetText(const C_str &text, uint8_t style, int index)
{
    H::BoxSetText(this, text, style, index);
    return true;
}
bool C_TextBox::SetTextId(int textId, int style, int index)
{
    H::BoxSetTextId(this, textId, style, index);
    return true;
}
bool C_TextBox::AddText(const C_str &text, uint8_t style)
{
    H::BoxAddText(this, text, style);
    return true;
}
void C_TextBox::Clear() { H::BoxClear(this); }
int C_Human::GetProfileUnknown24() const { return H::HRank(this); }
const char *C_Human::GetLastName() const { return H::HLastName(this); }
const char *C_Human::GetShortName() const { return H::HShortName(this); }
int C_Human::GetTeam(bool pending) const { return H::HTeam(this, pending); }
C_WeaponItem *C_Human::GetHandsWeapon() const { return static_cast<C_WeaponItem *>(H::HHands(this)); }
C_Item *C_Human::FindItem(int typeId, int *container) const { return static_cast<C_Item *>(H::HFindItem(this, typeId, container)); }
int C_Human::GetStat(int stat) const { return H::HStat(this, stat); }
int C_Human::GetHealthPercent() const { return H::HHealth(this); }
int C_Human::GetStaminaPercent() const { return H::HStamina(this); }
int C_Human::GetDisguisePercent() const { return H::HDisguise(this); }
float C_Human::GetVisibility() const { return H::HVisibility(this); }
int C_Human::GetOrderCount() const { return H::HOrders(this); }
int C_Human::GetSquadStatusIcon() const { return H::HStatusIcon(this); }
int C_Human::GetSquadModeIcon() const { return H::HModeIcon(this); }
bool C_Human::IsLocalPlayer() const { return H::HLocalPlayer(this); }
C_str C_GameTexts::Get(int id) const { return H::TextOf(id); }
S_RankName g_rankNames[8];
namespace {
S_MultiplayerSettings *g_settingsPointer = reinterpret_cast<S_MultiplayerSettings *>(H::g_settings);
}
S_MultiplayerSettings *&g_mpSettings = g_settingsPointer;
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_game); }
int C_game::GetVipSlot() const { return H::O_GetVipSlot(const_cast<C_game *>(this)); }
bool C_GameProfile::IsCooperativeGame() const { return H::O_IsCoop(nullptr); }
C_Human *C_Team::GetActivePlayer() const { return H::HumanAt(H::g_active); }

// The port's offsets of what the panel reads inline (each written at both sides' offsets).
constexpr uint32_t P_NETID = offsetof(C_Actor, m_netId), P_FRAME = offsetof(C_Actor, m_frame), P_CONTAINERS = offsetof(C_Actor, m_containers),
                   P_PROFILE = offsetof(C_Human, m_profile), P_HEALTH = offsetof(C_Human, m_stats), P_INVENTORY = offsetof(C_Human, m_inventoryChanged),
                   P_ROLE = offsetof(C_Human, m_role), P_SCALE = offsetof(C_Human, m_scale), P_DEF = offsetof(C_Item, m_def),
                   P_TEXTID = offsetof(C_ItemDef, m_textId), P_TEAM = offsetof(C_ItemDef, m_team), P_FACE = offsetof(S_SoldierProfile, faceImage),
                   P_GAMETYPE = offsetof(S_MultiplayerSettings, gameType);
static_assert(sizeof(C_Human) <= 0x800, "human block");
static_assert(P_DEF == 4 && P_TEXTID == 0x14 && P_TEAM == 0x44 && P_FACE == 0x20, "items and profiles as the original's");

namespace H {

template <class T> void Both(uint8_t *block, uint32_t orig, uint32_t port, T value)
{
    std::memcpy(block + orig, &value, sizeof value);
    std::memcpy(block + port, &value, sizeof value);
}

// ---- the scenarios ----
enum E_Op { OP_INIT, OP_SET_HUMAN, OP_SELECT, OP_UPDATE, OP_RENDER, OP_RELAYOUT, OP_DONE, OP_ANSWERS, OP_FIELDS, OP_ACTIVE, OP_GAME, OP_FRAME, OP_SHOW, OP_EDGE };
const char *const OP_NAMES[] = {"Init", "SetHuman", "SetSelected", "Update", "Render", "Relayout", "Done", "Answers", "Fields", "Active", "Game", "Frame", "Show", "Edge"};
struct S_Op {
    E_Op kind;
    int a = 0, b = 0;
    S_Answers answers{};
    int health = 0, netId = 0;
    bool inventory = false;
    S_vector4 scale{};
    uint32_t role = 7, gameType = 1;
    int vip = 0, uniform = -1;
    bool coop = false, on = false;
    S_vector pos;
};
struct S_Scenario {
    std::vector<S_Op> ops;
    uint32_t seed;
    int index, active, vip, uniform, loadResult;
    uint32_t role, gameType;
    bool coop;
    // The panel's fields Init does not set (both sides alike).
    bool selected, alive, unknown88;
    float health, photoW;
    int stamina, disguise, statusIcon, modeIcon, unknown78;
};

S_Answers RandomAnswers()
{
    S_Answers a;
    a.rank = P(0.8) ? I(0, 7) : I(0, 7);
    a.team = P(0.8) ? I(0, 1) : I(-1, 3);
    a.health = P(0.2) ? Pick({0, -5, 49, 50, 51, 100, 1}) : I(0, 100);
    a.stamina = P(0.3) ? Pick({0, 1, 100}) : I(0, 100);
    a.disguise = P(0.3) ? Pick({0, 1, 100}) : I(0, 100);
    a.statusIcon = I(-1, 5);
    a.modeIcon = I(-1, 5);
    a.orders = P(0.5) ? 0 : I(1, 3);
    a.firstAid = P(0.3) ? Pick({39, 40, 41}) : I(0, 100);
    a.localPlayer = P(0.3);
    a.visibility = P(0.2) ? Pick({0.0f, 12.5f, 13.5f, 0.5f, 99.5f, 1e9f}) : F(0, 300);
    a.hands = P(0.3) ? -1 : I(0, 3);
    a.silenced = P(0.6) ? 0 : U32() & 7;
    return a;
}
S_vector4 RandomScale()
{
    S_vector4 v;
    v.x = P(0.5) ? 1.0f : F(0, 1);
    v.y = P(0.5) ? 1.0f : F(0, 1);
    v.z = P(0.5) ? 1.0f : F(0, 1);
    v.w = F(0, 1);
    return v;
}
void SetHumanFields(int h, int health, int netId, bool inventory, const S_vector4 &scale, uint32_t role)
{
    uint8_t *b = g_humans[h];
    Both(b, 0xEC, P_HEALTH, health);
    Both(b, 0x10, P_NETID, netId);
    Both(b, 0x178, P_INVENTORY, inventory);
    Both(b, 0x1E0, P_SCALE, scale);
    Both(b, 0x19C, P_ROLE, role);
}

S_Scenario RandomScenario()
{
    S_Scenario s;
    s.seed = U32();
    s.index = I(0, 5);
    s.active = I(0, HUMANS - 1);
    s.vip = I(-1, 4);
    s.uniform = P(0.2) ? -1 : I(0, 3);
    s.loadResult = P(0.9) ? 0 : -1;
    s.role = P(0.6) ? 7u : Pick({0u, 1u, 3u, 6u});
    s.gameType = Pick({0u, 0u, 1u, 2u, 3u});
    s.coop = P(0.5);
    s.selected = P(0.5);
    s.alive = P(0.5);
    s.unknown88 = P(0.5);
    s.health = F(0, 100);
    s.photoW = F(0, 1);
    s.stamina = I(0, 100);
    s.disguise = I(0, 100);
    s.statusIcon = I(-1, 5);
    s.modeIcon = I(-1, 5);
    s.unknown78 = I(0, 2);
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    for (int k = 0; k < PART_COUNT; ++k)
        g_partMissing[k] = k == 17 && P(0.25);   // (mp_vip* may be missing; AImode* only in TestNoAiMode: the original crashes)
    // The graphics: each frame its mesh, level, face group; the groups' materials m0..m3 (shared), the pool after them.
    std::memset(&g_gfx, 0, sizeof g_gfx);
    for (int i = 0; i < FRAMES; ++i) {
        *reinterpret_cast<void **>(g_arena.frames[i] + 0x1E0) = g_gfx.meshes[i];
        *reinterpret_cast<uint32_t *>(g_arena.frames[i] + 0x1D0) = U32() & 0x3F3F;
        *reinterpret_cast<void ***>(g_gfx.meshes[i]) = g_meshVt;
        *reinterpret_cast<void **>(g_gfx.levels[i] + 0x5C) = g_gfx.groups[i];
        *reinterpret_cast<void ***>(g_gfx.groups[i]) = g_groupVt;
        *reinterpret_cast<void **>(g_gfx.groups[i] + 4) = g_gfx.materials[I(0, 3)];
    }
    for (int m = 0; m < 48; ++m) {
        *reinterpret_cast<void ***>(g_gfx.materials[m]) = g_materialVt;
        *reinterpret_cast<uint32_t *>(g_gfx.materials[m] + 8) = P(0.3) ? 0x4000000u : 0u;
        *reinterpret_cast<void **>(g_gfx.materials[m] + 0xC) = g_gfx.textures[I(12, 23)];
    }
    for (int t = 0; t < 24; ++t)
        *reinterpret_cast<void ***>(g_gfx.textures[t]) = g_textureVt;
    g_gfx.nextMaterial = 4;
    // The humans, their items' defs, their profiles.
    for (int h = 0; h < HUMANS; ++h) {
        std::memset(g_humans[h], 0, sizeof g_humans[h]);
        SetHumanFields(h, P(0.8) ? I(1, 100) : Pick({0, -3}), I(-1, 4), P(0.3), RandomScale(), P(0.3) ? Pick({4u, 5u}) : Pick({0u, 1u, 2u, 3u}));
        Both(g_humans[h], 0x20, P_FRAME, static_cast<void *>(FrameAt(HUMAN_FRAME + h)));
        std::memset(g_profiles[h], 0, sizeof g_profiles[h]);
        *reinterpret_cast<void **>(g_profiles[h] + 0x20) = P(0.7) ? g_gfx.textures[14 + h] : nullptr;
        Both(g_humans[h], 0xB8, P_PROFILE, static_cast<void *>(P(0.9) ? g_profiles[h] : nullptr));
        g_answers[h] = RandomAnswers();
    }
    for (int i = 0; i < 4; ++i) {
        std::memset(g_items[i], 0, sizeof g_items[i]);
        std::memset(g_defs[i], 0, sizeof g_defs[i]);
        *reinterpret_cast<void **>(g_items[i] + 4) = g_defs[i];
        *reinterpret_cast<int *>(g_defs[i] + 0x14) = I(0x100, 0x2000);
        *reinterpret_cast<int *>(g_defs[i] + 0x44) = P(0.8) ? I(0, 1) : I(2, 3);
    }
    // The ops: Init first; SetHuman before Update; Done ends the panel until the next Init.
    int stage = 0;   // 0 none, 1 made, 2 with a human
    const int count = I(1, 20);
    for (int n = 0; n < count; ++n) {
        S_Op op;
        if (stage == 0)
            op.kind = OP_INIT;
        else if (stage == 1)
            op.kind = static_cast<E_Op>(Pick({1, 1, 1, 2, 4, 5, 6, 7, 8, 9, 10, 12}));
        else
            op.kind = static_cast<E_Op>(Pick({1, 2, 3, 3, 3, 3, 3, 3, 4, 4, 5, 6, 7, 7, 7, 8, 8, 9, 10, 11, 11, 12, 13}));
        if (op.kind == OP_DONE && P(0.6))
            op.kind = stage == 2 ? OP_UPDATE : OP_RENDER;
        switch (op.kind) {
        case OP_INIT:
            stage = 1;
            op.a = s.index;
            break;
        case OP_SET_HUMAN:
            stage = 2;
            op.a = I(0, HUMANS - 1);
            break;
        case OP_SELECT:
            op.a = P(0.5);
            break;
        case OP_DONE:
            stage = 0;
            break;
        case OP_ANSWERS:
            op.a = I(0, HUMANS - 1);
            op.answers = g_answers[op.a];
            switch (I(0, 5)) {   // (mostly one answer changing: the panel's own changes follow it)
            case 0: op.answers.health = P(0.4) ? Pick({0, 1, 2, 49, 50, 100}) : I(0, 100); break;
            case 1: op.answers.stamina = I(0, 100); op.answers.disguise = I(0, 100); break;
            case 2: op.answers.statusIcon = I(-1, 5); op.answers.modeIcon = I(-1, 5); break;
            case 3: op.answers.rank = I(0, 7); break;
            case 4: op.answers.orders = I(0, 2); op.answers.localPlayer = P(0.3); break;
            default: op.answers = RandomAnswers(); break;
            }
            break;
        case OP_FIELDS:
            op.a = I(0, HUMANS - 1);
            op.health = P(0.8) ? I(1, 100) : Pick({0, -3});
            op.netId = I(-1, 4);
            op.inventory = P(0.4);
            op.scale = RandomScale();
            op.role = P(0.3) ? Pick({4u, 5u}) : Pick({0u, 1u, 2u, 3u});
            break;
        case OP_ACTIVE:
            op.a = I(0, HUMANS - 1);
            break;
        case OP_GAME:
            op.role = P(0.6) ? 7u : Pick({0u, 1u, 3u, 6u});
            op.gameType = Pick({0u, 0u, 1u, 2u, 3u});
            op.vip = I(-1, 4);
            op.coop = P(0.5);
            op.uniform = P(0.2) ? -1 : I(0, 3);
            break;
        case OP_EDGE:   // (the active soldier at a height, the others exactly 40 m away at it: the squares 1600)
            op.pos = S_vector(0.0f, Pick({0.0f, 2.0f, -3.0f}), 0.0f);
            break;
        case OP_SHOW:   // (the interface shows or hides the panel)
            op.a = P(0.7);
            break;
        case OP_FRAME:
            op.a = I(0, HUMANS - 1);
            // (Now and then on the 40 m edge, at equal heights other than 0: the squares exactly 1600, the heights' sign.)
            op.pos = P(0.6) ? S_vector(Pick({0.0f, 40.0f, -40.0f, 39.999f, 40.001f, 39.95f, 40.05f}), Pick({0.0f, 0.0f, 2.0f, -2.0f}), 0.0f)
                            : S_vector(F(-60, 60), F(-5, 5), F(-60, 60));
            op.on = P(0.7);
            break;
        default:
            break;
        }
        s.ops.push_back(op);
    }
    // (A failed death image only without Done: both sides release it unchecked there.)
    for (const S_Op &op : s.ops)
        if (op.kind == OP_DONE)
            s.loadResult = 0;
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_gfxStart, &g_gfx, sizeof g_gfx);
    std::memcpy(g_humansStart, g_humans, sizeof g_humans);
    std::memcpy(g_answersStart, g_answers, sizeof g_answers);
    return s;
}

// A side's snap of the panel's fields.
std::string Snap(const void *const *ptrs, int index, int statusIcon, int modeIcon, const void *human, int unknown78, float health, int stamina,
                 int disguise, bool unknown88, bool selected, int rank, float unknown98, const S_vector4 &photo, bool alive, int margin0, int margin1)
{
    // (ptrs: scene, frame, select, active, photo, health bar, critical, health icon, stamina bar, disguise bar, visibility,
    // AI mode, ear, silencer, medic, VIP, command material, mind material, death image, photo material, state icons, mode
    // icons, blank texture, name box, visibility box.)
    std::string s = "panel " + std::to_string(index);
    for (int k = 0; k < 25; ++k)
        s += " " + Name(ptrs[k]);
    char text[300];
    // (Not the photo colour's w: the original's Init copies a stack word there; nothing reads it.)
    std::snprintf(text, sizeof text, " icons %d %d human %s %d health %s %d %d %d sel %d rank %d %s photo %s %s %s alive %d margins %d %d",
                  statusIcon, modeIcon, Name(human).c_str(), unknown78, FloatText(health).c_str(), stamina, disguise, unknown88, selected, rank,
                  FloatText(unknown98).c_str(), FloatText(photo.x).c_str(), FloatText(photo.y).c_str(), FloatText(photo.z).c_str(), alive,
                  margin0, margin1);
    return s + text;
}
std::string SnapPort(const C_MemberPanel &m)
{
    const void *ptrs[25] = {m.m_scene, m.m_frame, m.m_select, m.m_activeMark, m.m_photo, m.m_healthBar, m.m_healthCritical, m.m_healthIcon,
                            m.m_staminaBar, m.m_disguiseBar, m.m_visibility, m.m_aiMode, m.m_ear, m.m_silencer, m.m_medic, m.m_vip,
                            m.m_commandMaterial, m.m_mindMaterial, m.m_deathImage, m.m_photoMaterial, m.m_stateIcons, m.m_modeIcons,
                            m.m_blankTexture, m.m_name, m.m_visibilityText};
    return Snap(ptrs, m.m_index, m.m_statusIcon, m.m_modeIcon, m.m_human, m.m_unknown78, m.m_health, m.m_stamina, m.m_disguise, m.m_unknown88,
                m.m_selected, m.m_rank, m.m_unknown98, m.m_photoColor, m.m_alive, m.m_name ? m.m_name->m_margin : -1,
                m.m_visibilityText ? m.m_visibilityText->m_margin : -1);
}
template <class T> T At(const uint8_t *b, uint32_t o)
{
    T v;
    std::memcpy(&v, b + o, sizeof v);
    return v;
}
std::string SnapOrig(const uint8_t *m)
{
    const uint32_t offsets[25] = {0x00, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x34, 0x38,
                                  0x3C, 0x40, 0x44, 0x48, 0x4C, 0x54, 0x5C, 0x60, 0x64, 0x68, 0x90, 0x94};
    const void *ptrs[25];
    for (int k = 0; k < 25; ++k)
        ptrs[k] = At<void *>(m, offsets[k]);
    const uint8_t *name = At<uint8_t *>(m, 0x90), *vis = At<uint8_t *>(m, 0x94);
    return Snap(ptrs, At<int>(m, 4), At<int>(m, 0x6C), At<int>(m, 0x70), At<void *>(m, 0x74), At<int>(m, 0x78), At<float>(m, 0x7C), At<int>(m, 0x80),
                At<int>(m, 0x84), At<bool>(m, 0x88), At<bool>(m, 0x89), At<int>(m, 0x8C), At<float>(m, 0x98), At<S_vector4>(m, 0xA0), At<bool>(m, 0xB0),
                name ? name[0x90] : -1, vis ? vis[0x90] : -1);
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
    S_Gfx gfx;
};
S_Run g_portRun, g_origRun;

void StartSide(const S_Scenario &s)
{
    std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
    std::memcpy(&g_gfx, &g_gfxStart, sizeof g_gfx);
    std::memcpy(g_humans, g_humansStart, sizeof g_humans);
    std::memcpy(g_answers, g_answersStart, sizeof g_answers);
    g_log.clear();
    g_boxes.clear();
    g_panelIndex = s.index;
    g_active = s.active;
    g_vipSlot = s.vip;
    g_uniform = s.uniform;
    g_coop = s.coop;
    g_loadResult = s.loadResult;
    g_gameRole = s.role;
    OrigRole() = s.role;
    Both(g_settings, 0x224, P_GAMETYPE, s.gameType);
    for (int h = 0; h < HUMANS; ++h) {
        Both(g_humans[h], 0x74, P_CONTAINERS, static_cast<void *>(nullptr));
        *reinterpret_cast<void **>(g_humans[h] + 0x74) = g_origContainers;
        *reinterpret_cast<void **>(g_humans[h] + P_CONTAINERS) = g_portContainers;
    }
    std::srand(s.seed);
}
void ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case OP_ANSWERS:
        g_answers[op.a] = op.answers;
        break;
    case OP_FIELDS:
        SetHumanFields(op.a, op.health, op.netId, op.inventory, op.scale, op.role);
        break;
    case OP_ACTIVE:
        g_active = op.a;
        break;
    case OP_GAME:
        g_gameRole = op.role;
        OrigRole() = op.role;
        Both(g_settings, 0x224, P_GAMETYPE, op.gameType);
        g_vipSlot = op.vip;
        g_coop = op.coop;
        g_uniform = op.uniform;
        break;
    case OP_EDGE:
        for (int h = 0; h < HUMANS; ++h) {
            I3D_frame *f = FrameAt(HUMAN_FRAME + h);
            f->m_worldMatrix(3) = h == g_active ? op.pos : S_vector(h & 1 ? 40.0f : -40.0f, op.pos.y, 0.0f);
            f->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
        }
        break;
    case OP_SHOW:
        FrameAt(PANEL)->m_frameFlags = op.a ? FrameAt(PANEL)->m_frameFlags | 1u : FrameAt(PANEL)->m_frameFlags & ~1u;
        break;
    case OP_FRAME: {
        I3D_frame *f = FrameAt(HUMAN_FRAME + op.a);
        f->m_worldMatrix(3) = op.pos;
        f->m_frameFlags = op.on ? f->m_frameFlags | FRMFLAGS_WMATRIX_VALID : f->m_frameFlags & ~FRMFLAGS_WMATRIX_VALID;
        break;
    }
    default:
        break;
    }
}

void RunPort(const S_Scenario &s)
{
    StartSide(s);
    C_MemberPanel *m = new C_MemberPanel;
    m->m_selected = s.selected;
    m->m_alive = s.alive;
    m->m_unknown88 = s.unknown88;
    m->m_health = s.health;
    m->m_photoColor.w = s.photoW;
    m->m_stamina = s.stamina;
    m->m_disguise = s.disguise;
    m->m_statusIcon = s.statusIcon;
    m->m_modeIcon = s.modeIcon;
    m->m_unknown78 = s.unknown78;
    for (const S_Op &op : s.ops) {
        Log("-- %s %d", OP_NAMES[op.kind], op.a);
        switch (op.kind) {
        case OP_INIT:
            Log("Init -> %d", m->Init(reinterpret_cast<I3D_scene *>(&g_scene), op.a, g_stateIcons, g_modeIcons));
            break;
        case OP_SET_HUMAN:
            m->SetHuman(HumanAt(op.a));
            break;
        case OP_SELECT:
            m->SetSelected(op.a != 0);
            break;
        case OP_UPDATE:
            m->Update();
            break;
        case OP_RENDER:
            m->Render();
            break;
        case OP_RELAYOUT:
            m->Relayout();
            break;
        case OP_DONE:
            m->Done();
            break;
        default:
            ApplyWorld(op);
            break;
        }
        g_log.push_back(SnapPort(*m));
    }
    delete m;
    g_portRun.log.swap(g_log);
    std::memcpy(&g_portRun.arena, &g_arena, sizeof g_arena);
    std::memcpy(&g_portRun.gfx, &g_gfx, sizeof g_gfx);
}
using T_Init = bool(__attribute__((thiscall)) *)(void *, void *, int, void *, void *);
using T_Ptr = void(__attribute__((thiscall)) *)(void *, void *);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_Void = void(__attribute__((thiscall)) *)(void *);
void RunOrig(const S_Scenario &s)
{
    StartSide(s);
    uint8_t *m = static_cast<uint8_t *>(std::calloc(1, 0x100));
    // (The port's start values, then the scenario's.)
    *reinterpret_cast<float *>(m + 0x98) = -1.0f;
    *reinterpret_cast<S_vector4 *>(m + 0xA0) = {1.0f, 1.0f, 1.0f, s.photoW};
    m[0x89] = s.selected;
    m[0xB0] = s.alive;
    m[0x88] = s.unknown88;
    *reinterpret_cast<float *>(m + 0x7C) = s.health;
    *reinterpret_cast<int *>(m + 0x80) = s.stamina;
    *reinterpret_cast<int *>(m + 0x84) = s.disguise;
    *reinterpret_cast<int *>(m + 0x6C) = s.statusIcon;
    *reinterpret_cast<int *>(m + 0x70) = s.modeIcon;
    *reinterpret_cast<int *>(m + 0x78) = s.unknown78;
    for (const S_Op &op : s.ops) {
        Log("-- %s %d", OP_NAMES[op.kind], op.a);
        switch (op.kind) {
        case OP_INIT:
            Log("Init -> %d", OriginalAt<T_Init>(0x004b30e0)(m, &g_scene, op.a, g_stateIcons, g_modeIcons));
            break;
        case OP_SET_HUMAN:
            OriginalAt<T_Ptr>(0x004b3c60)(m, HumanAt(op.a));
            break;
        case OP_SELECT:
            OriginalAt<T_Bool>(0x004b3e70)(m, op.a != 0);
            break;
        case OP_UPDATE:
            OriginalAt<T_Void>(0x004b3ec0)(m);
            break;
        case OP_RENDER:
            OriginalAt<T_Void>(0x004b4760)(m);
            break;
        case OP_RELAYOUT:
            OriginalAt<T_Void>(0x004b47a0)(m);
            break;
        case OP_DONE:
            OriginalAt<T_Void>(0x004b3840)(m);
            break;
        default:
            ApplyWorld(op);
            break;
        }
        g_log.push_back(SnapOrig(m));
    }
    std::free(m);
    g_origRun.log.swap(g_log);
    std::memcpy(&g_origRun.arena, &g_arena, sizeof g_arena);
    std::memcpy(&g_origRun.gfx, &g_gfx, sizeof g_gfx);
}

void TestScenarios()
{
    const S_Scenario s = RandomScenario();
    RunOrig(s);
    RunPort(s);
    // (+0xDC, m_scaleW: the original's inline SetScale copies the caller's 12-byte scale in as 16 bytes from a stack
    // temporary; its fourth word is whatever the stack held. Put back from the start on both sides.)
    for (int i = 0; i < FRAMES; ++i) {
        std::memcpy(g_origRun.arena.frames[i] + 0xDC, g_arenaStart.frames[i] + 0xDC, 4);
        std::memcpy(g_portRun.arena.frames[i] + 0xDC, g_arenaStart.frames[i] + 0xDC, 4);
    }
    CompareLogs(g_origRun.log, g_portRun.log);
    CompareArenas(g_origRun.arena, g_portRun.arena);
    ++checks;
    if (std::memcmp(&g_origRun.gfx, &g_portRun.gfx, sizeof g_gfx) != 0)
        CHECK(false, "the graphics' blocks differ");
    ShowLogs(g_origRun.log, g_portRun.log);
}

// The port's fix alone (the original's flaw: "AImode*" missing, Init allows it, Update and the death use it unchecked: a
// crash): the port's panel through Update (living, dying, dead) without the part, never asked for it.
void TestNoAiMode()
{
    S_Scenario s = RandomScenario();
    g_partMissing[14] = true;
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    StartSide(s);
    g_gameRole = 7;
    g_loadResult = 0;
    C_MemberPanel m;
    m.Init(reinterpret_cast<I3D_scene *>(&g_scene), s.index, g_stateIcons, g_modeIcons);
    CHECK(m.m_aiMode == nullptr, "AImode* found");
    m.SetHuman(HumanAt(0));
    for (int k = 0; k < 3; ++k) {
        g_answers[0].health = k == 0 ? I(1, 100) : 0;
        m.Update();
    }
    CHECK(!m.m_alive, "the member did not die");
    m.Done();
    // (Its frame never touched: its index not the parts', so a call on it would log "?" or crash.)
    for (const std::string &line : g_log)
        CHECK(line.compare(0, 4, "f15 ") != 0, "AImode*'s frame used: %s", line.c_str());
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    g_findChild = FindChild;
    g_frameVt[0xA0 / 4] = reinterpret_cast<void *>(F_DuplicateMesh);
    for (void *&v : g_meshVt)
        v = reinterpret_cast<void *>(GFX_Other);
    g_meshVt[0x04 / 4] = reinterpret_cast<void *>(M_GetLevel);
    for (void *&v : g_groupVt)
        v = reinterpret_cast<void *>(GFX_Other);
    g_groupVt[0x18 / 4] = reinterpret_cast<void *>(G_DuplicateMaterial);
    for (void *&v : g_materialVt)
        v = reinterpret_cast<void *>(GFX_Other);
    g_materialVt[0x04 / 4] = reinterpret_cast<void *>(MAT_SetAmbient);
    g_materialVt[0x1C / 4] = reinterpret_cast<void *>(MAT_SetTexture);
    g_materialVt[0x44 / 4] = reinterpret_cast<void *>(MAT_Resolve);
    for (void *&v : g_textureVt)
        v = reinterpret_cast<void *>(TEX_Release);
    g_graphVt[0x18 / 4] = reinterpret_cast<void *>(G_ScrnX);
    g_graphVt[0x1C / 4] = reinterpret_cast<void *>(G_ScrnY);
    g_graphVt[0x3C / 4] = reinterpret_cast<void *>(G_LoadTexture);
    for (int k = 0; k < 6; ++k) {
        g_stateIcons[k] = reinterpret_cast<I3D_texture *>(g_gfx.textures[k]);
        g_modeIcons[k] = reinterpret_cast<I3D_texture *>(g_gfx.textures[6 + k]);
    }
    for (void *&v : g_origContainerVt)
        v = reinterpret_cast<void *>(C_Other);
    for (void *&v : g_portContainerVt)
        v = reinterpret_cast<void *>(C_Other);
    g_origContainerVt[0x40 / 4] = reinterpret_cast<void *>(OC_GetItem);
    g_portContainerVt[0x40 / 4 + 1] = reinterpret_cast<void *>(PC_GetItem);
    for (int k = 0; k < 8; ++k) {
        g_origContainers[k] = &g_origContainer;
        g_portContainers[k] = &g_portContainer;
    }
    for (int r = 0; r < 8; ++r) {
        g_rankNames[r].textId = 0x2790 + r;
        *OriginalAt<int *>(0x008ae960 + r * 0x10) = 0x2790 + r;
    }
    *OriginalAt<void **>(0x008ae8f4) = g_game;
    *OriginalAt<void **>(0x008aea30) = g_settings;
    // (The original CRT's float formatting: its _cfltcvt_init never ran in the mapped copy.)
    OriginalAt<void(__cdecl *)()>(0x007e599c)();
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x0063df00, reinterpret_cast<const void *>(O_AcquireFonts)},
        {0x00644da0, reinterpret_cast<const void *>(OB_AttachTo)},
        {0x006446e0, reinterpret_cast<const void *>(OB_SetText)},
        {0x00644520, reinterpret_cast<const void *>(OB_SetTextId)},
        {0x00644830, reinterpret_cast<const void *>(OB_AddText)},
        {0x00644c80, reinterpret_cast<const void *>(OB_Clear)},
        {0x00648200, reinterpret_cast<const void *>(OB_Render)},
        {0x0048ecd0, reinterpret_cast<const void *>(OB_Delete)},
        {0x005a8380, reinterpret_cast<const void *>(O_Rank)},
        {0x005a81c0, reinterpret_cast<const void *>(O_LastName)},
        {0x005a82c0, reinterpret_cast<const void *>(O_ShortName)},
        {0x005a8820, reinterpret_cast<const void *>(O_Team)},
        {0x005a2b10, reinterpret_cast<const void *>(O_Hands)},
        {0x005a78f0, reinterpret_cast<const void *>(O_FindItem)},
        {0x005a6600, reinterpret_cast<const void *>(O_Stat)},
        {0x005a5ff0, reinterpret_cast<const void *>(O_Health)},
        {0x005a62a0, reinterpret_cast<const void *>(O_Stamina)},
        {0x005a6280, reinterpret_cast<const void *>(O_Disguise)},
        {0x005a59e0, reinterpret_cast<const void *>(O_Visibility)},
        {0x005a5f00, reinterpret_cast<const void *>(O_Orders)},
        {0x005a88b0, reinterpret_cast<const void *>(O_StatusIcon)},
        {0x005a88d0, reinterpret_cast<const void *>(O_ModeIcon)},
        {0x005a0890, reinterpret_cast<const void *>(O_LocalPlayer)},
        {0x006e5380, reinterpret_cast<const void *>(O_TextsGet)},
        {0x0045d1d0, reinterpret_cast<const void *>(O_GetActivePlayer)},
        {0x0069e340, reinterpret_cast<const void *>(O_GetVipSlot)},
        {0x006b69d0, reinterpret_cast<const void *>(O_IsCoop)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("members_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_MEMBERS_QUICK") ? 5000 : 30000;
    if (const char *r = std::getenv("HD2_MEMBERS_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "Scenarios";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    g_case = "NoAiMode";
    for (g_round = 0; g_round < rounds / 20; ++g_round)
        TestNoAiMode();
    std::printf("members_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
