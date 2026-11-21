// Human part 76: the first-person view (game/first_person_view.cpp: C_FirstPersonView) against the original's
// (0x00490740 .. 0x00493610). MinGW; build_fpv.bat; runs fpv_diff.exe ("fpv_diff: N checks, 0 failures").
// first_person_view.cpp is compiled into this file. What the view reaches outside itself is faked on both sides with
// the same loggers: the interface kit's frames (interface_kit.inc), the engine's generated slot loggers (ui_vt.inc:
// the models' animation slots), the game's methods (fpv_fakes.inc, gen_ifakes.py), by hand the camera module (its
// fields of view kept), the game, the soldiers (their answers by the soldier asked), their items and definitions (the
// original's inline GetItemDef: its table at 0x0098b030), the caches, the sounds, the effects, the meshes the sleeves
// are looked for in, the animation sets, RandFloat, dbgPrintf (its text); the chunks real on both sides
// (common/chunk.cpp, the original's own) over the animation table in memory (the port's rw_data functions, the
// original's imports redirected to them).
// Cases: "View": both views from one start (the fields as an earlier life left them, within what the game reaches),
// Init now and then, operations in any order (the public ones and the private ones the original has on their own: the
// updates, PlayAnim, StopAnims, ApplySleeves, PlayEventSound, LoadHands, LoadAnimTable), the world changing between
// them; "Targeted": the states the random scenarios seldom combine (the drops' exact ends, the sights' animation
// ending, shots and clicks during the sights' animations, shots through the sights, stage 3); "Destructors": a view
// with animation sets loaded destroyed on each side; "Fixes": the port's fixes, alone. The logs (every call out, the
// view's fields after each operation), the arena, the meshes and the animation tables compared. Left out (both crash, or
// the original reads or writes outside its table): see the skips and the world's comments.
// HD2_FPV_ROUNDS: scenarios (default 6,000, half as many targeted; HD2_FPV_QUICK: 2,500); HD2_FPV_TABLES=round: that
// round's tables printed; HD2_FPV_DUMP=round: its table file written; HD2_FPV_STACK: an access violation's registers and
// stack; HD2_INTERFACE_ULPS, HD2_INTERFACE_SHOW, HD2_INTERFACE_ECHO, HD2_INTERFACE_FIRST_CHANCE: the kit's.
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
#include <map>
#include <random>
#include <string>
#include <vector>

#define INTERFACE_FRAMES 64
#define protected public
#define private public
#include "game/resource_cache.h"
// (Faked here: declared before the view instantiates them.)
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *, const char *, uint32_t, void *, void *, int);
template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *, I3D_animation_set **, T_CacheErrorProc, void *);
#include "game/first_person_view.cpp"
#include "game/geffects.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"

namespace H {

int g_side;
template <class T> void Put(void *b, uint32_t o, T v) { std::memcpy(static_cast<uint8_t *>(b) + o, &v, sizeof v); }
template <class T> T At(const void *b, uint32_t o)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(b) + o, sizeof v);
    return v;
}
uint32_t Fnv(const char *s, uint32_t h)
{
    for (; *s; ++s)
        h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

// ---- the frames: f0 the first-person root, f1 the primary sector, f2..f9 the models CreateFrame makes (in turn),
// f10..f13 the items' first-person models, f14..f17 their magazines, f18..f21 the visuals "a" (their meshes),
// f22..f25 the soldiers' frames, f26.. the pool (children, the flash, the warhead, the dummies) ----
constexpr int F_ROOT = 0, F_SECTOR = 1, F_MODEL = 2, MODELS = 8, F_WEAPON = 10, F_MAG = 14, F_VISUAL = 18, F_BODY = 22, F_POOL = 26;

// ---- the soldiers (h0..h3), the vehicles (v0..v2), the items, the definitions, the components (both sides' blocks) ----
constexpr int HUMANS = 4, VEHICLES = 3, WEAPONS = 4, UNIFORMS = 3, GRENADES = 2, ITEMS = WEAPONS + UNIFORMS + GRENADES, DEFS = 8;
alignas(16) uint8_t g_humans[HUMANS][0x2000], g_vehicles[2][VEHICLES][0x400], g_items[2][ITEMS][0x100], g_defs[2][DEFS][0x100],
    g_comps[2][DEFS][0x40];
void *g_vehicleVt[2][0x80], *g_itemVt[2][0x40], *g_defVt[2][0x40], *g_compVt[2][0x40];
void *Hum(int i) { return i < 0 || i >= HUMANS ? nullptr : g_humans[i]; }

// ---- the meshes of the visuals "a": a mesh, a level, face groups, materials, textures (shared, compared) ----
constexpr int GROUPS = 5, TEXTURES = 6;
struct alignas(16) S_Meshes {
    uint8_t mesh[4][0x60], level[4][0x80], group[4][GROUPS][0x20], material[4][GROUPS][0x40], texture[TEXTURES][0x40];
};
S_Meshes g_meshes, g_meshesStart;
void *g_meshVt[16], *g_levelVt[16], *g_materialVt[32], *g_textureVt[4];
const char *const TEXTURE_NAMES[TEXTURES] = {"US_ARM1.bmp", "GE_ARS2.bmp", "hands.bmp", "w_default.bmp", "ARMY.bmp", "skin.bmp"};

// ---- the animation sets the cache hands out ----
constexpr int ANIMS = 6;
alignas(16) uint8_t g_anims[ANIMS][0x40];
void *g_animVt[16];

// ---- the game, the camera module, the interface, the effects, the sound module, the scene ----
alignas(16) uint8_t g_origGame[0x200], g_portGame[0x800], g_cameraModule[2][0x100], g_ui[0x100], g_effects[2][0x100], g_soundModule[2][0x100],
    g_gameScene[0x400];
void *g_origCameraVt[0x40], *g_effectsVt[2][0x40], *g_origSoundVt[0x40];

// ---- the answers ----
uint32_t g_retSeed;
std::vector<std::pair<std::string, int>> g_retCounts;
uint32_t RetWord(const char *name)
{
    int count = 0;
    for (auto &c : g_retCounts)
        if (c.first == name) {
            count = ++c.second;
            break;
        }
    if (!count)
        g_retCounts.emplace_back(name, count = 1);
    uint32_t h = Fnv(name, g_retSeed) ^ uint32_t(count) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

// ---- the world ----
struct S_World {
    uint32_t role;
    int cameraMode;
    float fov;            // the game's (cutscene) field of view
    bool hudShown;
    int active;           // GetActivePlayer: a soldier, -1 none
    // The soldiers: vehicle (-1 none), seat, health, velocity, hands weapon (-1 none), uniform (-1 none), belt grenade
    // (-1 none), weapon stance, view mode, the equipment flag (put away), light level.
    int vehicle[HUMANS], seat[HUMANS], health[HUMANS], weapon[HUMANS], uniform[HUMANS], grenade[HUMANS], stance[HUMANS], viewMode[HUMANS];
    float velocity[HUMANS][3];
    bool putAway[HUMANS];
    int vehicleType[VEHICLES];
    // The items: type, amount, definition; the definitions: first-person model (-1 none), magazine (-1 none),
    // behaviour, armed sound, shot sound, the uniforms' first-person model name.
    int itemType[ITEMS], itemDef[ITEMS];
    float amount[ITEMS];
    int fpvModel[DEFS], magazine[DEFS], behaviour[DEFS], armedSound[DEFS], shotSound[DEFS], fpvName[DEFS];
    bool defMissing;      // a used type without a definition (the port's fix, alone)
    // The frames: children counts, the flash, the dummies found; the warhead found (else the port's fix, alone).
    int children[8];
    bool flash, dummies, warhead;
    // The models' Open (the hands): fails when the call's hash says so.
    uint32_t openFails;
    bool openAlways;
    // The animation sets: Get fails (hash), their end times; the models' animation times and activity.
    uint32_t getFails;
    int endTime[ANIMS];
    int animTimeBase;
    // The camera's position and directions.
    float cameraPos[4], cameraDir[2][4];
    float lightLevel;
    float rand[8];
};
S_World g_w, g_wStart;
const int TYPES[] = {38, 1, 7, 12, 37, 120, 250, 499};   // the items' types (38: the Panzerfaust)
const char *const FPV_NAMES[] = {"Hands_US.i3d", "w_default_hands.i3d", "Hands_GE", "Hands.UK.i3d", "", "x.i3d"};

int g_randCalls, g_anim;
float RandF()
{
    const float f = g_w.rand[g_randCalls++ % 8];
    Log("RandFloat -> %s", FloatText(f).c_str());
    return f;
}

std::string PartName(const void *p);
void *RetPtrHook(const char *name, uint32_t w);
template <class T> T Ret(const char *name);
template <> bool Ret<bool>(const char *name) { return (RetWord(name) & 3) != 0; }
template <> int Ret<int>(const char *name)
{
    const uint32_t w = RetWord(name);
    if (std::strcmp(name, "I3D_model::GetAnimTime") == 0)
        return (w & 3) == 0 ? 0 : g_w.animTimeBase + int(w % 120);
    if (std::strcmp(name, "C_Human::GetWeaponStance") == 0 || std::strcmp(name, "C_Human::GetPlayerViewMode") == 0)
        return -1;   // (by the soldier: the hooks below)
    return (w & 3) == 0 ? -1 : int(w >> 8 & 7);
}
template <> float Ret<float>(const char *name)
{
    const uint32_t w = RetWord(name);
    return (w & 3) == 0 ? 0.0f : float(w >> 8 & 0xFF) / 64.0f;
}
template <> void *Ret<void *>(const char *name)
{
    const uint32_t w = RetWord(name);
    return RetPtrHook(name, w);
}
template <> const char *Ret<const char *>(const char *) { return "text"; }
std::string F3(const float *v) { return FloatText(v[0]) + " " + FloatText(v[1]) + " " + FloatText(v[2]); }
std::string V4(const float *v) { return F3(v) + " " + FloatText(v[3]); }

}  // namespace H

// ---- the generated loggers: the engine's slots, the game's methods ----
#include "ui_vt.inc"
#include "fpv_fakes.inc"
#define IFAKES_PORT
#include "fpv_fakes.inc"
#undef IFAKES_PORT

namespace H {

int HumIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p >= g_humans[i] && p < g_humans[i] + sizeof g_humans[i])
            return i;
    return -1;
}
// The soldiers' answers by the soldier asked (the generated loggers' self).
int SoldierOf(const void *self) { return HumIndex(self); }
void *RetPtrHook(const char *name, uint32_t w)
{
    if (std::strcmp(name, "C_Team::GetActivePlayer") == 0)
        return Hum(g_w.active);
    if (std::strstr(name, "::GetCameraModule"))
        return g_cameraModule[g_side];
    if (std::strcmp(name, "I3D_frame::GetChild") == 0)
        return FrameAt(F_POOL + int(w % uint32_t(FRAMES - F_POOL)));
    return nullptr;
}

// ---- names ----
std::string PartName(const void *p)
{
    const int h = HumIndex(p);
    if (h >= 0) {
        // (The body: the original's at +0x98, the port's at its member's offset: g++ lays C_Human out otherwise.)
        const uint32_t off = uint32_t(static_cast<const uint8_t *>(p) - g_humans[h]);
        if (off == (g_side ? uint32_t(offsetof(C_Human, m_body)) : 0x98u))
            return "h" + std::to_string(h) + ".body";
        return off == 0 ? "h" + std::to_string(h) : "h" + std::to_string(h) + "+" + std::to_string(off);
    }
    for (int side = 0; side < 2; ++side) {
        for (int k = 0; k < VEHICLES; ++k)
            if (p == g_vehicles[side][k])
                return "v" + std::to_string(k);
        for (int k = 0; k < ITEMS; ++k)
            if (p == g_items[side][k])
                return "item" + std::to_string(k);
        for (int k = 0; k < DEFS; ++k) {
            if (p == g_defs[side][k])
                return "def" + std::to_string(k);
            if (p == g_comps[side][k])
                return "comp" + std::to_string(k);
        }
        if (p == g_cameraModule[side])
            return "cameraModule";
        if (p == g_effects[side])
            return "effects";
        if (p == g_soundModule[side])
            return "sounds";
    }
    for (int k = 0; k < 4; ++k) {
        if (p == g_meshes.mesh[k])
            return "mesh" + std::to_string(k);
        if (p == g_meshes.level[k])
            return "level" + std::to_string(k);
        for (int g = 0; g < GROUPS; ++g) {
            if (p == g_meshes.group[k][g])
                return "group" + std::to_string(k) + "." + std::to_string(g);
            if (p == g_meshes.material[k][g])
                return "material" + std::to_string(k) + "." + std::to_string(g);
        }
    }
    for (int k = 0; k < TEXTURES; ++k)
        if (p == g_meshes.texture[k])
            return "texture" + std::to_string(k);
    for (int k = 0; k < ANIMS; ++k)
        if (p == g_anims[k])
            return "anim" + std::to_string(k);
    if (p == g_origGame || p == g_portGame)
        return "game";
    if (p == g_ui)
        return "ui";
    if (p == g_gameScene)
        return "scene";
    if (p == OriginalAt<uint8_t *>(0x0089ae78 + 0x1C) || p == &g_teamManager.m_playerTeam)
        return "team";
    return "";
}

// ---- the frames' children and finds ----
int g_models;
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    const int i = FrameIndex(f);
    if (std::strcmp(name, "a") == 0)
        return FrameAt(F_VISUAL + (i < 0 ? 0 : i) % 4);
    const bool found = std::strcmp(name, "flash") == 0    ? g_w.flash
                       : std::strcmp(name, "ammo") == 0   ? g_w.warhead
                       : std::strcmp(name, "cardum") == 0 || std::strcmp(name, "blsdum") == 0 ? g_w.dummies
                                                          : false;
    return found ? FrameAt(F_POOL + int(Fnv(name, uint32_t(i) * 2654435761u) % uint32_t(FRAMES - F_POOL))) : nullptr;
}
int __stdcall V_NumChildren(void *f)
{
    const int n = g_w.children[uint32_t(FrameIndex(f)) % 8];
    Log("%s NumChildren -> %d", Name(f).c_str(), n);
    return n;
}
void *__stdcall V_GetChild(void *f, int i)
{
    void *c = FrameAt(F_POOL + int(Fnv("child", uint32_t(FrameIndex(f) * 16 + i)) % uint32_t(FRAMES - F_POOL)));
    Log("%s GetChild %d -> %s", Name(f).c_str(), i, Name(c).c_str());
    return c;
}
void *g_bigDriverVt[0x30], *g_modelVt[64];
void *__stdcall D_CreateFrame(void *, uint32_t type)
{
    void *f = type == FRAME_MODEL ? static_cast<void *>(FrameAt(F_MODEL + g_models++ % MODELS)) : nullptr;
    Log("driver CreateFrame %u -> %s", type, Name(f).c_str());
    return f;
}
void __stdcall V_Other0(void *self) { Log("%s other slot", Name(self).c_str()); }
void SetKinds()
{
    for (int k = F_MODEL; k < F_VISUAL; ++k)
        *reinterpret_cast<void ***>(FrameAt(k)) = g_modelVt;
}

// ---- the meshes: GetLevel (0x04), GetFaceGroup (0x14), the material's ResolveTexture (0x44) and SetTexture (0x1C) ----
void *__stdcall M_GetLevel(void *mesh, int index)
{
    Log("%s GetLevel %d", Name(mesh).c_str(), index);
    for (int k = 0; k < 4; ++k)
        if (mesh == g_meshes.mesh[k])
            return g_meshes.level[k];
    return nullptr;
}
void *__stdcall L_GetFaceGroup(void *level, int index)
{
    Log("%s GetFaceGroup %d", Name(level).c_str(), index);
    for (int k = 0; k < 4; ++k)
        if (level == g_meshes.level[k])
            return index >= 0 && index < GROUPS ? g_meshes.group[k][index] : nullptr;
    return nullptr;
}
void __stdcall MT_Resolve(void *material) { Log("%s ResolveTexture", Name(material).c_str()); }
void __stdcall MT_SetTexture(void *material, void *texture)
{
    Log("%s SetTexture %s", Name(material).c_str(), Name(texture).c_str());
    Put<void *>(material, 0x0C, texture);
}
void __stdcall X_Other(void *self) { Log("%s mesh other slot", Name(self).c_str()); }
// A world's meshes: each visual's mesh, its level of n face groups, their materials' textures (or none), the pending
// bit now and then.
void RandomMeshes()
{
    std::memset(&g_meshes, 0, sizeof g_meshes);
    for (int t = 0; t < TEXTURES; ++t) {
        *reinterpret_cast<void ***>(g_meshes.texture[t]) = g_textureVt;
        Put<const char *>(g_meshes.texture[t], 0x18, P(0.95) ? TEXTURE_NAMES[t] : nullptr);
    }
    for (int k = 0; k < 4; ++k) {
        *reinterpret_cast<void ***>(g_meshes.mesh[k]) = g_meshVt;
        *reinterpret_cast<void ***>(g_meshes.level[k]) = g_levelVt;
        Put<int>(g_meshes.level[k], 0x58, P(0.1) ? 0 : I(1, GROUPS));
        for (int g = 0; g < GROUPS; ++g) {
            Put<void *>(g_meshes.group[k][g], 4, g_meshes.material[k][g]);
            *reinterpret_cast<void ***>(g_meshes.material[k][g]) = g_materialVt;
            Put<uint32_t>(g_meshes.material[k][g], 8, P(0.2) ? I3DMATERIAL_TEXTURE_PENDING_BIT : 0u);
            Put<void *>(g_meshes.material[k][g], 0x0C, P(0.15) ? nullptr : static_cast<void *>(g_meshes.texture[I(0, TEXTURES - 1)]));
        }
    }
    for (int k = 0; k < 4; ++k)
        Put<void *>(g_arena.frames[F_VISUAL + k], 0x1E0, g_meshes.mesh[k]);
}

// ---- the animation sets: Release (0x00), GetEndTime (0x08) ----
uint32_t __stdcall A_Release(void *a)
{
    Log("%s Release", Name(a).c_str());
    return 0;
}
int __stdcall A_EndTime(void *a)
{
    for (int k = 0; k < ANIMS; ++k)
        if (a == g_anims[k]) {
            Log("%s GetEndTime -> %d", Name(a).c_str(), g_w.endTime[k]);
            return g_w.endTime[k];
        }
    return 0;
}
// The cache's Get: a set by the name's hash, or nothing (the world's hash).
HRESULT AnimGet(const char *name, I3D_animation_set **out)
{
    const uint32_t h = Fnv(name, 77u);
    const bool fails = (h ^ g_w.getFails) % 7 == 0;
    void *a = fails ? nullptr : static_cast<void *>(g_anims[h % ANIMS]);
    Log("anims Get %s -> %s", name, Name(a).c_str());
    *out = static_cast<I3D_animation_set *>(a);
    return fails ? HRESULT(0x80004005) : I3D_OK;
}
HRESULT __attribute__((thiscall)) O_AnimGet(void *, const char *name, I3D_animation_set **out, void *, void *) { return AnimGet(name, out); }
// The model cache's OpenInto (the hands).
HRESULT ModelOpen(const void *object, const char *name, uint32_t flags)
{
    const bool ok = g_w.openAlways || (RetWord("models::OpenInto") ^ g_w.openFails) % 5 != 0;
    Log("models OpenInto %s %s %x -> %d", Name(object).c_str(), name, flags, ok);
    return ok ? I3D_OK : HRESULT(0x80004005);
}
HRESULT __attribute__((thiscall)) O_ModelOpen(void *, void *object, const char *name, uint32_t flags, void *, void *, int)
{
    return ModelOpen(object, name, flags);
}

// ---- the camera module (the original's through its vtable): the fields of view kept, the position, the directions ----
float g_fov[2], g_fov2[2];   // the camera's, each side's
void CamSpecial(int mode, bool on, const void *target) { Log("cameraModule SetSpecialMode %d %d %s", mode, on, Name(target).c_str()); }
bool CamSetFov(float fov)
{
    Log("cameraModule SetFov %s", FloatText(fov).c_str());
    g_fov[g_side] = fov;
    return true;
}
float CamGetFov()
{
    Log("cameraModule GetFov -> %s", FloatText(g_fov[g_side]).c_str());
    return g_fov[g_side];
}
bool CamSetFov2(float fov)
{
    Log("cameraModule SetNearSectorFov %s", FloatText(fov).c_str());
    g_fov2[g_side] = fov;
    return true;
}
float CamGetFov2()
{
    Log("cameraModule GetNearSectorFov -> %s", FloatText(g_fov2[g_side]).c_str());
    return g_fov2[g_side];
}
bool CamPos(int index, float *pos)
{
    Log("cameraModule GetCameraPos %d", index);
    std::memcpy(pos, g_w.cameraPos, 16);
    return true;
}
bool CamDir(int index, float *dir)
{
    Log("cameraModule GetCameraDir %d", index);
    std::memcpy(dir, g_w.cameraDir[index & 1], 16);
    return true;
}
void __attribute__((thiscall)) OC_Special(void *, int mode, bool on, void *target) { CamSpecial(mode, on, target); }
bool __attribute__((thiscall)) OC_SetFov(void *, float fov) { return CamSetFov(fov); }
float __attribute__((thiscall)) OC_GetFov(void *) { return CamGetFov(); }
bool __attribute__((thiscall)) OC_SetFov2(void *, float fov) { return CamSetFov2(fov); }
float __attribute__((thiscall)) OC_GetFov2(void *) { return CamGetFov2(); }
bool __attribute__((thiscall)) OC_Pos(void *, int index, float *pos) { return CamPos(index, pos); }
bool __attribute__((thiscall)) OC_Dir(void *, int index, float *dir) { return CamDir(index, dir); }
void __attribute__((thiscall)) OC_Other(void *) { Log("cameraModule other slot"); }
void *__cdecl O_GetCameraModule() { return g_cameraModule[0]; }

// ---- the effects (SetModelLightLevel, SetLensFlareOn), the sounds (Play) ----
bool __attribute__((thiscall)) E_LightLevel(void *, void *model, float level)
{
    Log("effects SetModelLightLevel %s %s", Name(model).c_str(), FloatText(level).c_str());
    return true;
}
void __attribute__((thiscall)) E_LensFlare(void *, bool on) { Log("effects SetLensFlareOn %d", on); }
void __attribute__((thiscall)) E_Other(void *) { Log("effects other slot"); }
void *__cdecl O_GetGEffects() { return g_effects[0]; }
int PlaySound(const S_SoundRequest &r)
{
    Log("sounds Play %s %s flags %x attach %s source %s chapter %d group %d file %s %s %s channel %d %s %s %s", VecText(r.pos).c_str(),
        FloatText(r.posW).c_str(), r.flags, Name(r.attachTo).c_str(), Name(r.source).c_str(), r.chapter, r.group, r.fileName ? r.fileName : "null",
        FloatText(r.nearRange).c_str(), FloatText(r.farRange).c_str(), r.channel, FloatText(r.nearScale).c_str(), FloatText(r.farScale).c_str(),
        FloatText(r.volume).c_str());
    return 0;
}
int __attribute__((thiscall)) O_Play(void *, const S_SoundRequest *r) { return PlaySound(*r); }
void *__cdecl O_GetSoundModule() { return g_soundModule[0]; }
void *__cdecl O_GetGame() { return g_origGame; }
void DbgText(const char *format, va_list args)
{
    char text[400];
    std::vsnprintf(text, sizeof text, format, args);
    Log("dbgPrintf %s", text);
}
void __cdecl O_DbgPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    DbgText(format, args);
    va_end(args);
}
float __cdecl O_RandFloat() { return RandF(); }
int __cdecl O_Sprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsprintf(buffer, format, args);
    va_end(args);
    return n;
}

// ---- the items: GetAmount (the original's 0x0C), the definitions' GetBehaviour (0x10), the components' GetShotSound
// (0x18), the vehicles' ShowSeatView (0xC4) ----
int ItemIndex(const void *p)
{
    for (int k = 0; k < ITEMS; ++k)
        if (p == g_items[g_side][k])
            return k;
    return -1;
}
int DefIndex(const void *p)
{
    for (int k = 0; k < DEFS; ++k)
        if (p == g_defs[g_side][k])
            return k;
    return -1;
}
float __attribute__((thiscall)) V_Amount(void *item)
{
    const int k = ItemIndex(item);
    const float a = k < 0 ? -1.0f : g_w.amount[k];
    Log("%s GetAmount -> %s", Name(item).c_str(), FloatText(a).c_str());
    return a;
}
int __attribute__((thiscall)) V_Behaviour(void *def)
{
    const int k = DefIndex(def);
    const int b = k < 0 ? 0 : g_w.behaviour[k];
    Log("%s GetBehaviour -> %d", Name(def).c_str(), b);
    return b;
}
int __attribute__((thiscall)) V_ShotSound(void *comp)
{
    int s = 0;
    for (int k = 0; k < DEFS; ++k)
        if (comp == g_comps[g_side][k])
            s = g_w.shotSound[k];
    Log("%s GetShotSound -> %d", Name(comp).c_str(), s);
    return s;
}
void __attribute__((thiscall)) V_SeatView(void *vehicle, int seat, bool on) { Log("%s ShowSeatView %d %d", Name(vehicle).c_str(), seat, on); }
void __attribute__((thiscall)) V_Other(void *self) { Log("%s other slot", Name(self).c_str()); }
template <class M> int Slot(M m)   // a port virtual's byte offset in its vtable (g++: the member pointer holds 1 + it)
{
    struct {
        uintptr_t p;
        ptrdiff_t adjust;
    } r;
    static_assert(sizeof r == sizeof m, "member pointer");
    std::memcpy(&r, &m, sizeof r);
    return int(r.p - 1);
}

// ---- the item definitions' table (the original's inline GetItemDef reads g_itemsModule's at 0x0098b030) ----
C_ItemDef *g_portDefTable[C_FirstPersonView::MAX_ITEM_TYPES];
void **OrigDefTable() { return OriginalAt<void **>(0x0098b030); }

// Each side's world: the soldiers' fields, the vehicles, the items, the definitions, the table, the game.
void SetWorld(int side)
{
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
    for (int h = 0; h < HUMANS; ++h) {
        uint8_t *b = g_humans[h];
        std::memset(b, 0, sizeof g_humans[h]);
        void *frame = FrameAt(F_BODY + h);
        void *vehicle = g_w.vehicle[h] < 0 ? nullptr : static_cast<void *>(g_vehicles[side][g_w.vehicle[h]]);
        S_vector4 v = {g_w.velocity[h][0], g_w.velocity[h][1], g_w.velocity[h][2], 0.0f};
        if (!side) {
            Put<int>(b, 0x18, int(ACTOR_HUMAN));
            Put<void *>(b, 0x20, frame);
            Put<void *>(b, 0x160, vehicle);
            Put<int>(b, 0x164, g_w.seat[h]);
            Put<int>(b, 0xEC, g_w.health[h]);
            Put<S_vector4>(b, 0x1B0, v);
        } else {
            Put<int>(b, offsetof(C_Actor, m_type), int(ACTOR_HUMAN));
            Put<void *>(b, offsetof(C_Actor, m_frame), frame);
            Put<void *>(b, offsetof(C_Human, m_vehicle), vehicle);
            Put<int>(b, offsetof(C_Human, m_seat), g_w.seat[h]);
            Put<int>(b, uint32_t(offsetof(C_Human, m_stats) + 4 * STAT_HEALTH), g_w.health[h]);
            Put<S_vector4>(b, offsetof(C_Human, m_velocity), v);
        }
    }
    for (int k = 0; k < VEHICLES; ++k) {
        uint8_t *v = g_vehicles[side][k];
        std::memset(v, 0, sizeof g_vehicles[side][k]);
        *reinterpret_cast<void ***>(v) = g_vehicleVt[side];
        Put<int>(v, side ? uint32_t(offsetof(C_Actor, m_type)) : 0x18, g_w.vehicleType[k]);
    }
    for (int k = 0; k < DEFS; ++k) {
        uint8_t *d = g_defs[side][k], *c = g_comps[side][k];
        std::memset(d, 0, 0x100);
        std::memset(c, 0, 0x40);
        *reinterpret_cast<void ***>(d) = g_defVt[side];
        *reinterpret_cast<void ***>(c) = g_compVt[side];
        void *magazine = g_w.magazine[k] < 0 ? nullptr : static_cast<void *>(FrameAt(F_MAG + g_w.magazine[k]));
        const char *name = g_w.fpvName[k] < 0 ? nullptr : FPV_NAMES[g_w.fpvName[k]];
        Put<const char *>(d, side ? uint32_t(offsetof(C_ItemDef, m_fpvModelName)) : 0x24, name);
        Put<int>(d, side ? uint32_t(offsetof(C_ItemDef, m_armedSound)) : 0x40, g_w.armedSound[k]);
        Put<void *>(d, side ? uint32_t(offsetof(C_ItemDef, m_component)) : 0x48, c);
        Put<void *>(d, side ? uint32_t(offsetof(C_AnimatedItemDef, m_fpvMagazine)) : 0x64, magazine);
    }
    for (int k = 0; k < ITEMS; ++k) {
        uint8_t *item = g_items[side][k];
        std::memset(item, 0, 0x100);
        *reinterpret_cast<void ***>(item) = g_itemVt[side];
        Put<void *>(item, side ? uint32_t(offsetof(C_Item, m_def)) : 4, g_defs[side][g_w.itemDef[k]]);
        Put<int>(item, side ? uint32_t(offsetof(C_Item, m_typeId)) : 0xC, g_w.itemType[k]);
    }
    // The table: each item's definition at its type (the same type: the last item's).
    std::fill(g_portDefTable, g_portDefTable + C_FirstPersonView::MAX_ITEM_TYPES, nullptr);
    std::fill(OrigDefTable(), OrigDefTable() + 500, nullptr);
    // (Every type of the world has one, 0 too (Init's type): the types no item has, a definition by the type.)
    for (int t : {38, 1, 7, 12, 37, 120, 250, 499, 0}) {
        g_portDefTable[t] = reinterpret_cast<C_ItemDef *>(g_defs[1][t % DEFS]);
        OrigDefTable()[t] = g_defs[0][t % DEFS];
    }
    for (int k = 0; k < ITEMS; ++k) {
        g_portDefTable[g_w.itemType[k]] = reinterpret_cast<C_ItemDef *>(g_defs[1][g_w.itemDef[k]]);
        OrigDefTable()[g_w.itemType[k]] = g_defs[0][g_w.itemDef[k]];
    }
    if (g_w.defMissing)
        g_portDefTable[g_w.itemType[0]] = nullptr;   // (the port's fix, alone)
    // The game.
    uint8_t *game = side ? g_portGame : g_origGame;
    Put<void *>(game, side ? uint32_t(offsetof(C_game, m_scene)) : 0x18, g_gameScene);
    Put<void *>(game, side ? uint32_t(offsetof(C_game, m_interface)) : 0x58, g_ui);
    Put<int>(game, side ? uint32_t(offsetof(C_game, m_cameraMode)) : 0xB8, g_w.cameraMode);
    Put<float>(game, side ? uint32_t(offsetof(C_game, m_fov)) : 0xBC, g_w.fov);
    Put<void *>(g_gameScene, 0x1F8, FrameAt(F_SECTOR));
    Put<void *>(g_gameScene, 0x204, FrameAt(F_ROOT));
    g_randCalls = 0;
}
// The soldiers' answers (the generated loggers' Ret by name; these by the soldier asked).
struct S_HookedAnswers {
    static void *Ptr(const char *name, const void *self)
    {
        const int h = SoldierOf(self);
        if (h < 0)
            return nullptr;
        if (std::strcmp(name, "weapon") == 0)
            return g_w.weapon[h] < 0 ? nullptr : static_cast<void *>(g_items[g_side][g_w.weapon[h]]);
        if (std::strcmp(name, "uniform") == 0)
            return g_w.uniform[h] < 0 ? nullptr : static_cast<void *>(g_items[g_side][WEAPONS + g_w.uniform[h]]);
        if (std::strcmp(name, "grenade") == 0)
            return g_w.grenade[h] < 0 ? nullptr : static_cast<void *>(g_items[g_side][WEAPONS + UNIFORMS + g_w.grenade[h]]);
        return nullptr;
    }
};

}  // namespace H

// ---- the port's side of the hand-written fakes ----
alignas(16) static uint8_t g_hudBlock[16];
static S_HudOption *g_hudPointer = reinterpret_cast<S_HudOption *>(g_hudBlock);
S_HudOption *&g_hudOption = g_hudPointer;
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGame); }
C_CameraModule *GetCameraModule() { return reinterpret_cast<C_CameraModule *>(H::g_cameraModule[1]); }
void C_CameraModule::SetSpecialMode(int mode, bool on, I3D_frame *target) { H::CamSpecial(mode, on, target); }
bool C_CameraModule::SetFov(float fov) { return H::CamSetFov(fov); }
float C_CameraModule::GetFov() { return H::CamGetFov(); }
bool C_CameraModule::SetNearSectorFov(float fov) { return H::CamSetFov2(fov); }
float C_CameraModule::GetNearSectorFov() { return H::CamGetFov2(); }
bool C_CameraModule::GetCameraPos(int index, S_vector4 *pos) { return H::CamPos(index, &pos->x); }
bool C_CameraModule::GetCameraDir(int index, S_vector4 *dir) { return H::CamDir(index, &dir->x); }
C_GEffectsModule *GetGEffects() { return reinterpret_cast<C_GEffectsModule *>(H::g_effects[1]); }
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(H::g_soundModule[1]); }
int C_SoundModule::Play(const S_SoundRequest &request) { return H::PlaySound(request); }
C_ItemDef *GetItemDef(int typeId)
{
    return typeId >= 0 && typeId < C_FirstPersonView::MAX_ITEM_TYPES ? H::g_portDefTable[typeId] : nullptr;
}
extern "C" float RandFloat() { return H::RandF(); }
extern "C" void dbgPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    H::DbgText(format, args);
    va_end(args);
}
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *object, const char *fileName, uint32_t flags, void *, void *, int)
{
    return H::ModelOpen(object, fileName, flags);
}
template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *fileName, I3D_animation_set **out, T_CacheErrorProc, void *)
{
    return H::AnimGet(fileName, out);
}
// (campaign.cpp's, not compiled here: the same as the original's 0x00415d00, which runs on its side.)
void ReadChunkString(C_chunk &chunk, C_str &out)
{
    char buffer[1000];
    const int remaining = chunk.RemainingSize();
    const int size = remaining < 0 ? 0 : remaining < 1000 ? remaining : 999;
    chunk.Read(buffer, size);
    buffer[size] = '\0';
    out = buffer;
    chunk.Ascend();
}
// LS3DF's math (the original's imports go to the same DLL).
S_vector __stdcall S_vector::RotateByMatrix(const S_matrix &m) const
{
    using T = S_vector *(__stdcall *)(const S_vector *, S_vector *, const S_matrix &);
    static const T rotate = reinterpret_cast<T>(H::Ls3df("?RotateByMatrix@S_vector@@QBG?AU1@ABUS_matrix@@@Z"));
    S_vector out;
    rotate(this, &out, m);
    return out;
}
void __stdcall S_quat::Make(const S_vector &axis, float angle)
{
    using T = void(__stdcall *)(S_quat *, const S_vector &, float);
    static const T make = reinterpret_cast<T>(H::Ls3df("?Make@S_quat@@QAGXABUS_vector@@M@Z"));
    make(this, axis, angle);
}
void __stdcall S_matrix::SetRot3(const S_quat &q)
{
    using T = void(__stdcall *)(S_matrix *, const S_quat &);
    static const T set = reinterpret_cast<T>(H::Ls3df("?SetRot3@S_matrix@@QAGXABUS_quat@@@Z"));
    set(this, q);
}
// The data files in memory (both sides).
namespace H {
std::map<std::string, std::string> g_files;
struct S_OpenFile {
    std::string name;
    size_t at = 0;
};
std::map<int, S_OpenFile> g_openFiles;
int g_nextFile = 1;
int FileOpen(const char *name, bool write)
{
    if (!write && !g_files.count(name)) {
        Log("dta open %s -> -1", name);
        return -1;
    }
    g_openFiles[g_nextFile] = S_OpenFile{name, 0};
    Log("dta open %s %d -> %d", name, write, g_nextFile);
    return g_nextFile++;
}
int FileSeek(int handle, int offset, int origin)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    o.at = origin == 0 ? size_t(offset) : origin == 1 ? o.at + offset : f.size() + offset;
    return int(o.at);
}
int FileRead(int handle, void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    // (A chunk's garbage size can seek before the start or past the end: nothing read there.)
    const int got = o.at >= f.size() ? 0 : std::max(0, std::min(int(f.size() - o.at), size));
    if (got)
        std::memcpy(data, f.data() + o.at, size_t(got));
    o.at += size_t(got);
    return got;
}
bool FileClose(int handle)
{
    Log("dta close %d", handle);
    return g_openFiles.erase(handle) != 0;
}
int __stdcall O_dtaOpen(const char *name, bool) { return FileOpen(name, false); }
int __stdcall O_dtaSeek(int h, int offset, int origin) { return FileSeek(h, offset, origin); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
bool __stdcall O_dtaClose(int h) { return FileClose(h); }
}  // namespace H
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name, false); }
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpen(name, true); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
int RW_DATA_CALL dtaWrite(int, const void *, int size) { return size; }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }

namespace H {

// ---- the animation table's file: 0x3039 { 100 + type { 2000 + event (13) { variants { 1000 { name }, ratio } } } } ----
std::string g_table;
std::vector<int> g_tableTypes;   // the types the table has
void MakeTable()
{
    std::string f;
    auto chunk = [](uint16_t id, const std::string &payload) {
        std::string c;
        const uint32_t size = uint32_t(6 + payload.size());
        c.append(reinterpret_cast<const char *>(&id), 2);
        c.append(reinterpret_cast<const char *>(&size), 4);
        return c + payload;
    };
    auto int4 = [](int x) { return std::string(reinterpret_cast<const char *>(&x), 4); };
    static const char *const NAMES[] = {"#FPVcolt_Idle1.i3d", "#FPVcolt_Shot.i3d", "#FPVmp40_Aim.i3d", "#FPVkar_Arm.i3d", "#FPVgrenade_Shot.i3d",
                                        "#FPVpanzer_Idle1.i3d", "a.i3d", "#FPVthompson_Reload.i3d"};
    std::string items;
    g_tableTypes.clear();
    const int n = I(1, 7);
    for (int k = 0; k < n; ++k) {
        // (Mostly the world's types: their animations played.)
        const int type = P(0.85) ? TYPES[I(0, 7)] : I(0, 499);
        std::string events;
        for (int e = 0; e < FPV_EVENT_COUNT; ++e) {
            std::string variants;
            const int vn = P(0.25) ? 0 : I(1, 5);
            for (int v = 0; v < vn; ++v) {
                std::string inner;
                // (Always the file chunk, never empty: a variant without one, or with an empty one, leaves the reader a level
                // too deep, the next chunks' headers then read as items and events, and the original writes names far
                // outside its table; not compared.)
                inner += chunk(1000, std::string(NAMES[I(0, 7)]) + std::string(1, '\0'));
                if (P(0.95))
                    inner += int4(P(0.7) ? Pick({25, 50, 75, 100, 100, 100, 1, 2}) : I(-5, 120));
                variants += chunk(uint16_t(P(0.9) ? 0x10 + v : I(0, 3000)), P(0.05) ? std::string() : inner);
            }
            events += chunk(uint16_t(CHUNK_FPV_EVENT_BASE + (P(0.95) ? e : I(0, FPV_EVENT_COUNT - 1))), variants);
        }
        items += chunk(uint16_t(CHUNK_FPV_ITEM_BASE + type), events);
        g_tableTypes.push_back(type);
    }
    f = chunk(P(0.95) ? uint16_t(CHUNK_FPV_ANIMS) : uint16_t(0x3038), items);
    g_table = f;
    if (std::getenv("HD2_FPV_DUMP") && std::atoi(std::getenv("HD2_FPV_DUMP")) == g_round)
        if (FILE *out = std::fopen("fpv_table.bin", "wb")) {
            std::fwrite(f.data(), 1, f.size(), out);
            std::fclose(out);
        }
}

void RandomWorld()
{
    g_w.role = P(0.85) ? 7u : Pick({6u, 2u, 3u, 0u, 1u});
    g_w.cameraMode = P(0.7) ? 0 : I(0, 3);
    g_w.fov = P(0.5) ? 1.3089969f : F(0.6f, 1.6f);
    g_w.hudShown = P(0.7);
    g_w.active = P(0.12) ? -1 : I(0, HUMANS - 1);
    for (int h = 0; h < HUMANS; ++h) {
        g_w.vehicle[h] = P(0.7) ? -1 : I(0, VEHICLES - 1);
        g_w.seat[h] = P(0.5) ? 0 : I(0, 3);
        g_w.health[h] = P(0.85) ? I(1, 100) : Pick({0, -3});
        g_w.weapon[h] = P(0.15) ? -1 : I(0, WEAPONS - 1);
        g_w.uniform[h] = P(0.2) ? -1 : I(0, UNIFORMS - 1);
        g_w.grenade[h] = P(0.3) ? -1 : I(0, GRENADES - 1);
        g_w.stance[h] = P(0.8) ? Pick({int(FPV_POSE_LOWERED), int(FPV_POSE_RAISED)}) : I(0, 5);
        g_w.viewMode[h] = I(0, 4);
        g_w.putAway[h] = P(0.2);
        for (float &v : g_w.velocity[h])
            v = P(0.3) ? 0.0f : F(-5, 5);
        if (P(0.1)) {
            // (A speed of exactly 1.)
            g_w.velocity[h][0] = 1.0f;
            g_w.velocity[h][1] = g_w.velocity[h][2] = 0.0f;
        }
    }
    for (int &t : g_w.vehicleType)
        t = P(0.5) ? int(ACTOR_CAR) : Pick({int(ACTOR_STATIC_WEAPON), int(ACTOR_JUNKERS88), 0});
    for (int k = 0; k < ITEMS; ++k) {
        g_w.itemType[k] = TYPES[I(0, 7)];
        g_w.itemDef[k] = I(0, DEFS - 1);
        g_w.amount[k] = P(0.3) ? 0.0f : P(0.5) ? F(0.5f, 30) : float(I(1, 30));
    }
    for (int k = 0; k < DEFS; ++k) {
        g_w.fpvModel[k] = P(0.15) ? -1 : I(0, 3);
        g_w.magazine[k] = P(0.4) ? -1 : I(0, 3);
        g_w.behaviour[k] = P(0.4) ? int(ITEM_BEHAVIOUR_THROWN) : I(0, 10);
        g_w.armedSound[k] = I(0, 40);
        g_w.shotSound[k] = I(0, 40);
        g_w.fpvName[k] = I(0, 5);
    }
    // (A Panzerfaust's warhead is looked for in the model unchecked, on both sides: with one, the hands always load and
    // its definition has a first-person model.)
    g_w.openAlways = false;
    // (The belt grenades too: their model replaces the weapon's and is turned at once, unchecked on both sides.)
    for (int k = WEAPONS + UNIFORMS; k < ITEMS; ++k)
        if (g_w.fpvModel[g_w.itemDef[k]] < 0)
            g_w.fpvModel[g_w.itemDef[k]] = I(0, 3);
    for (int k = 0; k < ITEMS; ++k)
        if (g_w.itemType[k] == 38) {
            g_w.openAlways = true;
            if (g_w.fpvModel[g_w.itemDef[k]] < 0)
                g_w.fpvModel[g_w.itemDef[k]] = I(0, 3);
        }
    g_w.defMissing = false;
    for (int &c : g_w.children)
        c = P(0.4) ? 0 : I(1, 3);
    g_w.flash = P(0.7);
    g_w.dummies = P(0.6);
    g_w.warhead = true;
    g_w.openFails = U32();
    g_w.getFails = U32();
    for (int &t : g_w.endTime)
        t = P(0.2) ? 0 : P(0.3) ? Pick({1, 16, 30, 59}) : I(10, 1500);
    g_w.animTimeBase = P(0.5) ? 0 : I(0, 1500);
    for (float &f : g_w.cameraPos)
        f = F(-50, 50);
    for (auto &d : g_w.cameraDir)
        for (float &f : d)
            f = F(-1, 1);
    for (float &r : g_w.rand)
        r = P(0.2) ? Pick({0.0f, 0.5f, 0.999f}) : F(0, 1);
}
// The soldiers' hooked answers (GetHandsWeapon .. GetPlayerViewMode by the soldier asked).
void *__attribute__((thiscall)) OH_Weapon(void *h)
{
    Log("%s C_Human::GetHandsWeapon", Name(h).c_str());
    return S_HookedAnswers::Ptr("weapon", h);
}
void *__attribute__((thiscall)) OH_Uniform(void *h)
{
    Log("%s C_Human::GetUniformItem", Name(h).c_str());
    return S_HookedAnswers::Ptr("uniform", h);
}
void *__attribute__((thiscall)) OH_Belt(void *h, int behaviour)
{
    Log("%s C_Human::FindBeltItem %d", Name(h).c_str(), behaviour);
    return S_HookedAnswers::Ptr("grenade", h);
}
int __attribute__((thiscall)) OH_Stance(void *h)
{
    const int k = SoldierOf(h);
    Log("%s C_Human::GetWeaponStance", Name(h).c_str());
    return k < 0 ? 0 : g_w.stance[k];
}
int __attribute__((thiscall)) OH_ViewMode(void *h)
{
    const int k = SoldierOf(h);
    Log("%s C_Human::GetPlayerViewMode", Name(h).c_str());
    return k < 0 ? 0 : g_w.viewMode[k];
}
bool __attribute__((thiscall)) OH_PutAway(void *body)
{
    const int k = SoldierOf(body);
    Log("%s S_HumanBody::GetEquipmentFlag1", Name(body).c_str());
    return k >= 0 && g_w.putAway[k];
}
void *__attribute__((thiscall)) OH_FpvModel(void *def)
{
    const int k = DefIndex(def);
    Log("%s C_ItemDef::GetFpvModel", Name(def).c_str());
    return k < 0 || g_w.fpvModel[k] < 0 ? nullptr : static_cast<void *>(FrameAt(F_WEAPON + g_w.fpvModel[k]));
}

}  // namespace H

// (The port's: the same hooks.)
C_WeaponItem *C_Human::GetHandsWeapon() const { return static_cast<C_WeaponItem *>(H::OH_Weapon(const_cast<C_Human *>(this))); }
C_Item *C_Human::GetUniformItem() const { return static_cast<C_Item *>(H::OH_Uniform(const_cast<C_Human *>(this))); }
C_Item *C_Human::FindBeltItem(int behaviour) const { return static_cast<C_Item *>(H::OH_Belt(const_cast<C_Human *>(this), behaviour)); }
int C_Human::GetWeaponStance() const { return H::OH_Stance(const_cast<C_Human *>(this)); }
int C_Human::GetPlayerViewMode() const { return H::OH_ViewMode(const_cast<C_Human *>(this)); }
bool S_HumanBody::GetEquipmentFlag1() const { return H::OH_PutAway(const_cast<S_HumanBody *>(this)); }
I3D_frame *C_ItemDef::GetFpvModel() { return static_cast<I3D_frame *>(H::OH_FpvModel(this)); }

namespace H {

// ---- each side's view: its fields snapped ----
uint8_t *g_orig, *g_origPristine, *g_portPristine;
C_FirstPersonView *g_port;
constexpr uint32_t ORIG_SIZE = 0x4C370;
#define OF(f) uint32_t(offsetof(C_FirstPersonView, f))
std::string Stage(const void *b, uint32_t o)
{
    return Name(At<void *>(b, o)) + "/" + std::to_string(At<int>(b, o + 4)) + "/" + FloatText(At<float>(b, o + 8)) + "/" + FloatText(At<float>(b, o + 12));
}
std::string Snap(int side)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    auto o = [side](uint32_t orig, uint32_t port) { return side ? port : orig; };
    char text[600];
    std::snprintf(text, sizeof text,
                  "fpv fov %s %s human %s init %d model %s hands %s weapon %s mag %s type %d on %d bino %d aim %d pose %d drop %s %s light %s "
                  "variant %d event %d next %d sway %s flash %s %s",
                  FloatText(At<float>(b, o(0x30, OF(m_fov)))).c_str(), FloatText(At<float>(b, o(0x34, OF(m_fov2)))).c_str(),
                  Name(At<void *>(b, o(0x38, OF(m_human)))).c_str(), At<uint8_t>(b, o(0x3C, OF(m_initialized))),
                  Name(At<void *>(b, o(0x40, OF(m_model)))).c_str(), Name(At<void *>(b, o(0x44, OF(m_hands)))).c_str(),
                  Name(At<void *>(b, o(0x48, OF(m_weapon)))).c_str(), Name(At<void *>(b, o(0x4C, OF(m_magazine)))).c_str(),
                  At<int>(b, o(0x54, OF(m_itemType))), At<uint8_t>(b, o(0x58, OF(m_on))), At<uint8_t>(b, o(0x59, OF(m_binoculars))),
                  At<uint8_t>(b, o(0x5A, OF(m_aimView))), At<int>(b, o(0x60, OF(m_pose))), FloatText(At<float>(b, o(0x64, OF(m_drop)))).c_str(),
                  FloatText(At<float>(b, o(0x68, OF(m_dropSpeed)))).c_str(), FloatText(At<float>(b, o(0x6C, OF(m_lightLevel)))).c_str(),
                  At<int>(b, o(0x90, OF(m_variant))), At<int>(b, o(0x94, OF(m_event))), At<int>(b, o(0x98, OF(m_nextEvent))),
                  FloatText(At<float>(b, o(0x9C, OF(m_swayPhase)))).c_str(), FloatText(At<float>(b, o(0x4C360, OF(m_flashTime)))).c_str(),
                  Name(At<void *>(b, o(0x4C364, OF(m_flash)))).c_str());
    std::string s = text;
    s += " current " + Stage(b, o(0x70, OF(m_current))) + " fading";
    if (side) {
        for (const S_FpvStage &f : g_port->m_fading)
            s += " " + Stage(&f, 0);
    } else {
        const uint8_t *first = At<uint8_t *>(b, 0x84), *last = At<uint8_t *>(b, 0x88);
        for (const uint8_t *f = first; f && f < last; f += 0x10)
            s += " " + Stage(f, 0);
    }
    // (The unused vectors; +0x0C, the first one's w: the original's Init leaves a stack word there.)
    for (uint32_t k = 0; k < 0x30; k += 4)
        if (k != 0x0C)
            s += " " + FloatText(At<float>(b, k));
    return s;
}
// The animation tables (names, ratios, sets), entry by entry.
std::string TableText(int side)
{
    std::string s;
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) + OF(m_anims) : g_orig + 0xA0;
    for (int k = 0; k < C_FirstPersonView::MAX_ITEM_TYPES * FPV_EVENT_COUNT; ++k) {
        const uint8_t *e = b + 0x30 * k;
        bool any = false;
        for (int v = 0; v < 4; ++v)
            any = any || At<void *>(e, 4 * v) || At<void *>(e, 0x10 + 4 * v) || At<int>(e, 0x20 + 4 * v);
        if (!any)
            continue;
        s += "[" + std::to_string(k / FPV_EVENT_COUNT) + "." + std::to_string(k % FPV_EVENT_COUNT) + "]";
        for (int v = 0; v < 4; ++v) {
            const C_str &name = *reinterpret_cast<const C_str *>(e + 0x10 + 4 * v);
            s += " " + Name(At<void *>(e, 4 * v)) + "," + name.c_str() + "," + std::to_string(At<int>(e, 0x20 + 4 * v));
        }
        s += "\n";
    }
    return s;
}

// The fields as an earlier life left them (both sides the same; Init sets most of them).
struct S_Fields {
    float fov, fov2, drop, dropSpeed, light, sway, flashTime, unused[12];
    int human, model, hands, weapon, magazine, flash, itemType, pose, variant, event, next, current, stage;
    bool initialized, on, bino, aim;
    float weight, speed;
    int fadingCount, fading[3][2];   // the stages blending out: their sets and stages
    float fadingWeight[3];
};
S_Fields g_fields;
void RandomFields()
{
    S_Fields &f = g_fields;
    f.fov = P(0.5) ? 0.0f : F(0.1f, 1.5f);
    f.fov2 = P(0.5) ? 0.0f : F(0.1f, 1.5f);
    // (-0.79, -0.01: the drops' ends within one update.)
    // (Exact ends: -0.4 (100 ms) and -0.796 (1 ms) the grenade's bottom, -0.377 back to lowered (59 ms), -0.3 back to 0
    // (100 ms).)
    f.drop = P(0.5) ? 0.0f : Pick({-0.2f, -0.3f, -0.8f, -0.5f, -0.1f, -0.79f, -0.01f, -0.21f, -0.4f, -0.796f, -0.377f, -0.19f});
    f.dropSpeed = P(0.2) ? 0.0f : F(0, 2);
    f.light = P(0.5) ? 1.0f : F(0, 1);
    f.sway = P(0.5) ? 0.0f : F(0, 10);
    f.flashTime = P(0.5) ? -1.0f : Pick({0.0f, 50.0f, 20.0f, 3.0f, 0.5f});
    for (float &u : f.unused)
        u = P(0.5) ? 0.0f : F(-1, 1);
    f.human = P(0.3) ? -1 : I(0, HUMANS - 1);
    f.model = P(0.5) ? -1 : I(0, MODELS - 1);
    f.hands = I(0, MODELS - 1);   // (none: only after a failed load, the Panzerfaust's crash)
    f.weapon = P(0.4) ? -1 : I(0, 3);
    f.magazine = P(0.5) ? -1 : I(0, 3);
    f.flash = P(0.5) ? -1 : I(F_POOL, FRAMES - 1);
    f.itemType = P(0.3) ? -1 : TYPES[I(0, 7)];
    f.pose = I(0, 5);
    f.variant = I(0, 3);
    f.event = P(0.2) ? -1 : I(0, 12);
    f.next = P(0.2) ? -1 : I(0, 12);
    // (A flash's time runs only with a flash: Update switches it off unchecked, on both sides.)
    if (f.flash < 0)
        f.flashTime = -1.0f;
    // (With a model the view has an item type and a next event in the table: the original indexes it unchecked.)
    if (f.model >= 0) {
        f.itemType = TYPES[I(0, 7)];
        f.next = I(0, 12);
    }
    f.current = P(0.5) ? -1 : I(0, ANIMS - 1);
    f.stage = P(0.2) ? 3 : I(0, 2);
    f.initialized = P(0.5);
    f.on = P(0.5);
    f.bino = P(0.2);
    f.aim = P(0.3);
    f.weight = P(0.5) ? 1.0f : P(0.3) ? 0.9f : F(0, 1);
    f.speed = P(0.5) ? 0.0f : 5.0f;
    // (The animations' ends that lead somewhere: a belt grenade's throw, a put away before a draw, the sights raised.)
    switch (P(0.6) ? I(0, 7) : -1) {
    case 0:
        f.pose = FPV_POSE_GRENADE;
        f.event = FPV_EVENT_FIRE;
        break;
    case 1:
        f.event = FPV_EVENT_PUT_AWAY;
        f.next = FPV_EVENT_DRAW;
        break;
    case 2:
        f.event = FPV_EVENT_AIM_IN;
        f.aim = true;
        break;
    case 3:
        f.pose = FPV_POSE_GRENADE_OUT;
        f.drop = Pick({-0.8f, -0.796f, -0.4f, -0.79f});
        break;
    case 4:
        f.pose = FPV_POSE_WEAPON_BACK;
        f.drop = Pick({-0.3f, -0.377f, -0.01f, -0.8f});
        break;
    case 5:
        f.pose = FPV_POSE_LOWERED;
        f.drop = Pick({-0.1f, -0.19f, 0.0f});
        break;
    case 6:
        f.event = Pick({int(FPV_EVENT_AIM_IN), int(FPV_EVENT_AIM_OUT)});
        f.aim = P(0.7);
        break;
    case 7:
        f.aim = true;
        f.next = Pick({int(FPV_EVENT_IDLE), int(FPV_EVENT_IDLE_EMPTY)});
        break;
    default:
        break;
    }
    if (f.model >= 0 || f.current >= 0)
        f.current = I(0, ANIMS - 1);
    f.fadingCount = P(0.5) ? 0 : I(1, 3);
    for (int k = 0; k < 3; ++k) {
        f.fading[k][0] = I(0, ANIMS - 1);
        f.fading[k][1] = P(0.5) ? k : I(0, 2);   // (stages 0, 1, 2: all three taken)
        f.fadingWeight[k] = P(0.3) ? 1.0f : P(0.3) ? 0.5f : F(0, 1);
    }
}
void SetFields(int side)
{
    uint8_t *b = side ? reinterpret_cast<uint8_t *>(g_port) : g_orig;
    auto o = [side](uint32_t orig, uint32_t port) { return side ? port : orig; };
    const S_Fields &f = g_fields;
    for (int k = 0; k < 12; ++k)
        Put<float>(b, uint32_t(4 * k), f.unused[k]);
    Put<float>(b, o(0x30, OF(m_fov)), f.fov);
    Put<float>(b, o(0x34, OF(m_fov2)), f.fov2);
    Put<void *>(b, o(0x38, OF(m_human)), Hum(f.human));
    Put<bool>(b, o(0x3C, OF(m_initialized)), f.initialized);
    Put<void *>(b, o(0x40, OF(m_model)), f.model < 0 ? nullptr : static_cast<void *>(FrameAt(F_MODEL + f.model)));
    Put<void *>(b, o(0x44, OF(m_hands)), f.hands < 0 ? nullptr : static_cast<void *>(FrameAt(F_MODEL + f.hands)));
    Put<void *>(b, o(0x48, OF(m_weapon)), f.weapon < 0 ? nullptr : static_cast<void *>(FrameAt(F_WEAPON + f.weapon)));
    Put<void *>(b, o(0x4C, OF(m_magazine)), f.magazine < 0 ? nullptr : static_cast<void *>(FrameAt(F_MAG + f.magazine)));
    Put<int>(b, o(0x54, OF(m_itemType)), f.itemType);
    Put<bool>(b, o(0x58, OF(m_on)), f.on);
    Put<bool>(b, o(0x59, OF(m_binoculars)), f.bino);
    Put<bool>(b, o(0x5A, OF(m_aimView)), f.aim);
    Put<int>(b, o(0x60, OF(m_pose)), f.pose);
    Put<float>(b, o(0x64, OF(m_drop)), f.drop);
    Put<float>(b, o(0x68, OF(m_dropSpeed)), f.dropSpeed);
    Put<float>(b, o(0x6C, OF(m_lightLevel)), f.light);
    Put<void *>(b, o(0x70, OF(m_current)), f.current < 0 ? nullptr : static_cast<void *>(g_anims[f.current]));
    Put<int>(b, o(0x74, OF(m_current) + 4), f.stage);
    Put<float>(b, o(0x78, OF(m_current) + 8), f.speed);
    Put<float>(b, o(0x7C, OF(m_current) + 12), f.weight);
    Put<int>(b, o(0x90, OF(m_variant)), f.variant);
    Put<int>(b, o(0x94, OF(m_event)), f.event);
    Put<int>(b, o(0x98, OF(m_nextEvent)), f.next);
    Put<float>(b, o(0x9C, OF(m_swayPhase)), f.sway);
    Put<float>(b, o(0x4C360, OF(m_flashTime)), f.flashTime);
    Put<void *>(b, o(0x4C364, OF(m_flash)), f.flash < 0 ? nullptr : static_cast<void *>(FrameAt(f.flash)));
    // The stages blending out (the original's MSVC vector {allocator, first, last, end}, malloc'd: its delete frees).
    if (side) {
        g_port->m_fading.clear();
        for (int k = 0; k < f.fadingCount; ++k)
            g_port->m_fading.push_back(S_FpvStage{static_cast<I3D_animation_set *>(static_cast<void *>(g_anims[f.fading[k][0]])), f.fading[k][1],
                                                  5.0f, f.fadingWeight[k]});
    } else {
        uint8_t *data = f.fadingCount ? static_cast<uint8_t *>(std::malloc(0x10 * size_t(f.fadingCount))) : nullptr;
        for (int k = 0; k < f.fadingCount; ++k) {
            Put<void *>(data, 0x10 * k, g_anims[f.fading[k][0]]);
            Put<int>(data, 0x10 * k + 4, f.fading[k][1]);
            Put<float>(data, 0x10 * k + 8, 5.0f);
            Put<float>(data, 0x10 * k + 12, f.fadingWeight[k]);
        }
        Put<uint8_t *>(b, 0x84, data);
        Put<uint8_t *>(b, 0x88, data ? data + 0x10 * f.fadingCount : nullptr);
        Put<uint8_t *>(b, 0x8C, data ? data + 0x10 * f.fadingCount : nullptr);
    }
}

// ---- the scenarios ----
enum {
    OP_INIT, OP_DONE, OP_RESET, OP_SAVE, OP_LOAD, OP_LOADTABLE, OP_SETWEAPONVIEW, OP_REFRESH, OP_REFRESHWEAPON, OP_SLEEVES, OP_SHOW,
    OP_BINOCULARS, OP_RESETAIM, OP_SETAIM, OP_UPDATE, OP_SETPOSE, OP_UPDATEVIEW, OP_UPDATEANIM, OP_UPDATEBLEND, OP_UPDATELIGHT, OP_EVENT,
    OP_PLAYANIM, OP_STOPANIMS, OP_EVENTSOUND, OP_CANUSE, OP_CARTRIDGE, OP_BLAST, OP_AIMORIGIN, OP_AIMDIR, OP_LOADHANDS, OP_COUNT,
    W_WORLD = 100,
};
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0;
    float f = 0.0f;
};
const int EVENTS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 20, 21, 2, 2, 8, 9, 11, 12, 0, 0};
S_Op RandomOp()
{
    S_Op op;
    if (P(0.06)) {
        op.kind = W_WORLD;
        op.a = int(U32());
        return op;
    }
    // (The updates and the events: the most.)
    static const int WEIGHTED[] = {OP_INIT, OP_DONE, OP_RESET, OP_SAVE, OP_LOAD, OP_LOADTABLE, OP_SETWEAPONVIEW, OP_REFRESH, OP_REFRESH,
                                   OP_REFRESHWEAPON, OP_SLEEVES, OP_SHOW, OP_SHOW, OP_BINOCULARS, OP_RESETAIM, OP_SETAIM, OP_SETAIM,
                                   OP_UPDATE, OP_UPDATE, OP_UPDATE, OP_UPDATE, OP_SETPOSE, OP_UPDATEVIEW, OP_UPDATEANIM, OP_UPDATEBLEND,
                                   OP_UPDATELIGHT, OP_EVENT, OP_EVENT, OP_EVENT, OP_EVENT, OP_PLAYANIM, OP_PLAYANIM, OP_STOPANIMS,
                                   OP_EVENTSOUND, OP_CANUSE, OP_CARTRIDGE, OP_BLAST, OP_AIMORIGIN, OP_AIMDIR, OP_LOADHANDS};
    op.kind = Pick(WEIGHTED);
    switch (op.kind) {
    case OP_SETWEAPONVIEW:
        op.a = P(0.25) ? -1 : I(0, 3);
        op.b = P(0.5) ? -1 : I(0, 3);
        op.c = op.a < 0 ? (P(0.5) ? -1 : TYPES[I(0, 7)]) : TYPES[I(0, 7)];
        break;
    case OP_SHOW:
    case OP_BINOCULARS:
        op.a = P(0.5);
        break;
    case OP_SETAIM:
        op.a = P(0.6);
        op.f = P(0.5) ? 0.69813174f : F(0.1f, 1.5f);
        break;
    case OP_UPDATE:
    case OP_UPDATEVIEW:
    case OP_UPDATEANIM:
    case OP_UPDATEBLEND:
        op.a = P(0.25) ? Pick({0, 1, 100, 1000, 59, 1, 100}) : I(1, 60);
        break;
    case OP_SETPOSE:
        op.a = I(0, 5);
        break;
    case OP_EVENT:
        op.a = Pick(EVENTS);
        op.b = P(0.5) ? 0 : P(0.3) ? int(FPV_EVENT_DRAW) : I(0, 12);
        break;
    case OP_PLAYANIM:
    case OP_EVENTSOUND:
        op.a = I(0, 12);
        break;
    case OP_LOADHANDS:
        op.a = I(0, 5);
        break;
    default:
        break;
    }
    return op;
}
using T_V = void(__attribute__((thiscall)) *)(void *);
using T_I = void(__attribute__((thiscall)) *)(void *, int);
using T_B = void(__attribute__((thiscall)) *)(void *, bool);
using T_BF = void(__attribute__((thiscall)) *)(void *, bool, float);
using T_II = void(__attribute__((thiscall)) *)(void *, int, int);
using T_PPI = void(__attribute__((thiscall)) *)(void *, void *, void *, int);
using T_RetB = bool(__attribute__((thiscall)) *)(void *);
using T_RetP = void *(__attribute__((thiscall)) *)(void *);
using T_Vec = S_vector4 *(__attribute__((thiscall)) *)(void *, S_vector4 *);
using T_Chunk = bool(__attribute__((thiscall)) *)(void *, void *);
using T_S = void(__attribute__((thiscall)) *)(void *, const char *);
using T_Ctor = void *(__attribute__((thiscall)) *)(void *);
// A side's own state (the same while the sides agree): what both sides would crash on is skipped.
bool Skip(int side, const S_Op &op, std::string &why)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    auto o = [side](uint32_t orig, uint32_t port) { return side ? port : orig; };
    const bool client = (g_gameRole & GAME_ROLE_CLIENT) != 0, initialized = At<uint8_t>(b, o(0x3C, OF(m_initialized))) != 0;
    void *human = At<void *>(b, o(0x38, OF(m_human))), *model = At<void *>(b, o(0x40, OF(m_model)));
    const bool aim = At<uint8_t>(b, o(0x5A, OF(m_aimView))) != 0;
    const int h = HumIndex(human);
    switch (op.kind) {
    case OP_SLEEVES:
        // (Both read the soldier's and the model's frames unchecked.)
        if (client && (!human || !model))
            why = "sleeves without a soldier or a model";
        break;
    case OP_SETAIM:
        if (client && initialized && !human && bool(op.a) != aim)
            why = "aim without a soldier";
        break;
    case OP_BINOCULARS:
        if (client && initialized && !human)
            why = "binoculars without a soldier";
        break;
    case OP_UPDATEVIEW:
        if (client && initialized && (!human || !model))
            why = "the view without a soldier or a model";
        break;
    case OP_UPDATEANIM:
        if (client && initialized && At<void *>(b, o(0x70, OF(m_current))) && (!human || !model))
            why = "the animation without a soldier or a model";
        break;
    case OP_EVENT:
        // (The original takes the hands weapon for granted at the idle event: the port's fix, alone.)
        if (op.a == FPV_EVENT_IDLE && client && initialized && model && !aim && h >= 0 && g_w.weapon[h] < 0)
            why = "idle without a weapon";
        if (client && initialized && model && !human)
            why = "an event without a soldier";
        break;
    case OP_EVENTSOUND: {
        // (The draw's sound of a type without a definition: the original reads it; the port's fix, alone.)
        const int type = At<int>(b, o(0x54, OF(m_itemType)));
        const bool on = At<uint8_t>(b, o(0x58, OF(m_on))) || At<uint8_t>(b, o(0x59, OF(m_binoculars)));
        if (op.a == FPV_EVENT_DRAW && on && (type < 0 || type >= 500 || !g_portDefTable[type]))
            why = "the draw's sound without a definition";
        break;
    }
    case OP_AIMORIGIN: {
        // (A grenade's origin: the first-person weapon's matrix, read unchecked on both sides.)
        const int type = At<int>(b, o(0x54, OF(m_itemType)));
        C_ItemDef *def = side ? GetItemDef(type) : (type >= 0 && type < 500 ? static_cast<C_ItemDef *>(OrigDefTable()[type]) : nullptr);
        if (client && def && g_w.behaviour[DefIndex(def)] == ITEM_BEHAVIOUR_THROWN && !At<void *>(b, o(0x48, OF(m_weapon))))
            why = "a grenade's origin without a weapon";
        break;
    }
    case OP_LOADHANDS:
        break;
    default:
        break;
    }
    // (Every path that reaches the soldier with the view's soldier gone: both crash.)
    if (why.empty() && client && initialized && model && !human &&
        (op.kind == OP_UPDATE || op.kind == OP_UPDATELIGHT || op.kind == OP_SETAIM || op.kind == OP_PLAYANIM))
        why = "the model without a soldier";
    return !why.empty();
}
void Run(int side, const S_Op &op)
{
    std::string why;
    if (Skip(side, op, why)) {
        Log("skip: %s", why.c_str());
        return;
    }
    void *o = g_orig;
    C_FirstPersonView *p = g_port;
    alignas(16) uint8_t chunk[0x500] = {};
    S_vector4 v = {};
    switch (op.kind) {
    case OP_INIT:
        side ? p->Init() : OriginalAt<T_V>(0x004909d0)(o);
        break;
    case OP_DONE:
        side ? p->Done() : OriginalAt<T_V>(0x00490b50)(o);
        break;
    case OP_RESET:
        side ? p->Reset() : OriginalAt<T_V>(0x00493360)(o);
        break;
    case OP_SAVE:
        Log("result %d", side ? p->SaveGame(*reinterpret_cast<C_chunk *>(chunk)) : OriginalAt<T_Chunk>(0x00493390)(o, chunk));
        break;
    case OP_LOAD:
        Log("result %d", side ? p->LoadGame(*reinterpret_cast<C_chunk *>(chunk)) : OriginalAt<T_Chunk>(0x004933a0)(o, chunk));
        break;
    case OP_LOADTABLE:
        side ? p->LoadAnimTable() : OriginalAt<T_V>(0x00490c80)(o);
        break;
    case OP_SETWEAPONVIEW: {
        void *weapon = op.a < 0 ? nullptr : static_cast<void *>(FrameAt(F_WEAPON + op.a));
        void *magazine = op.b < 0 ? nullptr : static_cast<void *>(FrameAt(F_MAG + op.b));
        side ? p->SetWeaponView(static_cast<I3D_model *>(weapon), static_cast<I3D_frame *>(magazine), op.c)
             : OriginalAt<T_PPI>(0x00491130)(o, weapon, magazine, op.c);
        break;
    }
    case OP_REFRESH:
        side ? p->Refresh() : OriginalAt<T_V>(0x004922a0)(o);
        break;
    case OP_REFRESHWEAPON:
        side ? p->RefreshWeapon() : OriginalAt<T_V>(0x00492500)(o);
        break;
    case OP_SLEEVES:
        side ? p->ApplySleeves() : OriginalAt<T_V>(0x00492610)(o);
        break;
    case OP_SHOW:
        side ? p->Show(op.a != 0) : OriginalAt<T_B>(0x00491fd0)(o, op.a != 0);
        break;
    case OP_BINOCULARS:
        side ? p->ShowBinoculars(op.a != 0) : OriginalAt<T_B>(0x004921f0)(o, op.a != 0);
        break;
    case OP_RESETAIM:
        side ? p->ResetAimView() : OriginalAt<T_V>(0x004920b0)(o);
        break;
    case OP_SETAIM:
        side ? p->SetAimView(op.a != 0, op.f) : OriginalAt<T_BF>(0x00492130)(o, op.a != 0, op.f);
        break;
    case OP_UPDATE:
        side ? p->Update(op.a) : OriginalAt<T_I>(0x00491e90)(o, op.a);
        break;
    case OP_SETPOSE:
        side ? p->SetPose(op.a) : OriginalAt<T_I>(0x00491640)(o, op.a);
        break;
    case OP_UPDATEVIEW:
        side ? p->UpdateView(op.a) : OriginalAt<T_I>(0x00491660)(o, op.a);
        break;
    case OP_UPDATEANIM:
        side ? p->UpdateAnim(op.a) : OriginalAt<T_I>(0x00491c60)(o, op.a);
        break;
    case OP_UPDATEBLEND:
        side ? p->UpdateBlend(op.a) : OriginalAt<T_I>(0x00491440)(o, op.a);
        break;
    case OP_UPDATELIGHT:
        side ? p->UpdateLight() : OriginalAt<T_V>(0x00491e30)(o);
        break;
    case OP_EVENT:
        side ? p->OnInventoryEvent(op.a, op.b) : OriginalAt<T_II>(0x00492930)(o, op.a, op.b);
        break;
    case OP_PLAYANIM:
        side ? p->PlayAnim(op.a) : OriginalAt<T_I>(0x00492a90)(o, op.a);
        break;
    case OP_STOPANIMS:
        side ? p->StopAnims() : OriginalAt<T_V>(0x004932a0)(o);
        break;
    case OP_EVENTSOUND:
        side ? p->PlayEventSound(op.a) : OriginalAt<T_I>(0x004933c0)(o, op.a);
        break;
    case OP_CANUSE:
        Log("result %d", side ? p->CanUseWeapon() : OriginalAt<T_RetB>(0x00493500)(o));
        break;
    case OP_CARTRIDGE:
        Log("result %s", Name(side ? static_cast<void *>(p->GetCartridgeDummy()) : OriginalAt<T_RetP>(0x004928d0)(o)).c_str());
        break;
    case OP_BLAST:
        Log("result %s", Name(side ? static_cast<void *>(p->GetBlastDummy()) : OriginalAt<T_RetP>(0x00492900)(o)).c_str());
        break;
    case OP_AIMORIGIN:
        if (side)
            v = p->GetAimOrigin();
        else
            OriginalAt<T_Vec>(0x00492790)(o, &v);
        Log("result %s %s", VecText(S_vector(v.x, v.y, v.z)).c_str(), FloatText(v.w).c_str());
        break;
    case OP_AIMDIR:
        if (side)
            v = p->GetAimDir();
        else
            OriginalAt<T_Vec>(0x00492880)(o, &v);
        Log("result %s %s", VecText(S_vector(v.x, v.y, v.z)).c_str(), FloatText(v.w).c_str());
        break;
    case OP_LOADHANDS:
        side ? p->LoadHands(FPV_NAMES[op.a]) : OriginalAt<T_S>(0x00490930)(o, FPV_NAMES[op.a]);
        break;
    default:
        break;
    }
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
    S_Meshes meshes;
    std::string table;
};
S_Run g_runs[2];
// The targeted case: an initialised view of a living soldier with a model, in one of the states the random scenarios
// seldom combine, and the operations that end it: the drops at their exact ends, the sights' animation ending (the
// scope's view mode), shots and clicks during the sights' animations, shots through the sights, all three stages
// blending out with the current one at stage 3.
bool g_targeted;
void Target(std::vector<S_Op> &ops)
{
    S_Fields &f = g_fields;
    g_w.role = 7;
    f.initialized = true;
    f.human = I(0, HUMANS - 1);
    g_w.health[f.human] = 50;
    f.model = I(0, MODELS - 1);
    f.next = I(0, 12);   // (with a model, in the table: the original indexes it unchecked)
    f.bino = false;
    // (A type the table has and the world defines: a draw's sound reads its definition unchecked in the original.)
    std::vector<int> types;
    for (int t : g_tableTypes)
        if (std::find(std::begin(TYPES), std::end(TYPES), t) != std::end(TYPES))
            types.push_back(t);
    f.itemType = types.empty() ? TYPES[I(0, 7)] : types[size_t(I(0, int(types.size()) - 1))];
    f.current = I(0, ANIMS - 1);
    // (The table loaded first: Init does not run here.)
    ops.push_back(S_Op{OP_LOADTABLE});
    f.pose = FPV_POSE_RAISED;
    switch (I(0, 4)) {
    case 0: {   // the drops: a start and the update that ends it exactly, or any
        static const struct {
            int pose;
            float drop;
            int dt, stance;
        } ENDS[] = {
            {FPV_POSE_GRENADE_OUT, -0.4f, 100, 0}, {FPV_POSE_GRENADE_OUT, -0.796f, 1, 0}, {FPV_POSE_GRENADE_OUT, -0.8f, 10, 0},
            {FPV_POSE_WEAPON_BACK, -0.377f, 59, WEAPON_STANCE_LOWERED}, {FPV_POSE_WEAPON_BACK, -0.3f, 100, WEAPON_STANCE_RAISED},
            {FPV_POSE_WEAPON_BACK, -0.01f, 20, WEAPON_STANCE_RAISED}, {FPV_POSE_LOWERED, -0.19f, 24, 0}, {FPV_POSE_RAISED, -0.1f, 20, 0},
        };
        const auto &e = Pick(ENDS);
        f.pose = e.pose;
        f.drop = e.drop;
        f.dropSpeed = P(0.5) ? 0.0f : F(0, 2);
        if (e.stance)
            g_w.stance[f.human] = e.stance;
        for (int n = I(1, 3); n > 0; --n) {
            S_Op op{OP_UPDATEVIEW};
            op.a = n == 1 || P(0.5) ? e.dt : I(1, 60);
            ops.push_back(op);
        }
        break;
    }
    case 1: {   // the sights' animation ends
        f.aim = true;
        f.event = FPV_EVENT_AIM_IN;
        S_Op op{OP_UPDATEANIM};
        op.a = 1000;
        ops.push_back(op);
        break;
    }
    case 2: {   // shots and clicks during the sights' animations
        f.aim = P(0.5);
        f.event = Pick({int(FPV_EVENT_AIM_IN), int(FPV_EVENT_AIM_OUT)});
        S_Op op{OP_EVENT};
        op.a = Pick({int(FPV_EVENT_FIRE), int(FPV_EVENT_FIRE_LAST), int(FPV_EVENT_JAMMED)});
        ops.push_back(op);
        break;
    }
    case 3: {   // shots through the sights
        f.aim = true;
        f.event = FPV_EVENT_IDLE;
        S_Op op{OP_EVENT};
        op.a = Pick({int(FPV_EVENT_FIRE), int(FPV_EVENT_FIRE_LAST)});
        ops.push_back(op);
        break;
    }
    default: {   // stage 3
        f.fadingCount = 3;
        for (int k = 0; k < 3; ++k)
            f.fading[k][1] = k;
        f.stage = 3;
        S_Op op{OP_PLAYANIM};
        op.a = I(0, 12);
        ops.push_back(op);
        break;
    }
    }
}
void TestScenarios()
{
    g_retSeed = U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    // (Some pool frames, the flashes among them, already straight ahead: their local z axis (0, 0, 1).)
    for (int i = F_POOL; i < FRAMES; ++i)
        if (P(0.3)) {
            // (SetScale keeps the rotation and rebuilds the matrix from it: the identity's.)
            FrameAt(i)->m_frameFlags |= FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_DIRTY;
            FrameAt(i)->m_localMatrix(2) = S_vector(0.0f, 0.0f, 1.0f);
            FrameAt(i)->m_rot.v = S_vector(0.0f, 0.0f, 0.0f);
            FrameAt(i)->m_rot.s = 1.0f;
            if (P(0.4)) {
                FrameAt(i)->m_rot.v = S_vector(0.3f, 0.0f, 0.0f);
                FrameAt(i)->m_rot.s = 0.9539392f;
            }
        }
    SetKinds();
    RandomWorld();
    RandomMeshes();
    MakeTable();
    RandomFields();
    const bool tableMissing = P(0.05);
    // (Always: the original's constructor leaves most fields unset, calloc's zeros here; the port's sets them.)
    const bool fields = true;
    std::vector<S_Op> ops;
    if (g_targeted)
        Target(ops);
    else {
        // (Else the fields as an earlier life left them, mostly initialised: the animations' ends they lead to.)
        if (P(0.6))
            ops.push_back(S_Op{OP_INIT});
        else if (P(0.7))
            g_fields.initialized = true;
        if (P(0.3))
            ops.push_back(S_Op{OP_REFRESH});
        for (int n = I(1, 16); n > 0; --n)
            ops.push_back(RandomOp());
    }
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_meshesStart, &g_meshes, sizeof g_meshes);
    g_wStart = g_w;
    const float fovStart = P(0.5) ? g_w.fov : F(0.1f, 1.6f), fov2Start = P(0.5) ? 1.13446403f : F(0.1f, 1.6f);
    const std::string table = g_table;
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        if (std::getenv("HD2_INTERFACE_ECHO") && std::atoi(std::getenv("HD2_INTERFACE_ECHO")) == g_round)
            std::printf("  == side %d\n", side);
        std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
        std::memcpy(&g_meshes, &g_meshesStart, sizeof g_meshes);
        g_w = g_wStart;
        g_log.clear();
        g_retCounts.clear();
        g_models = 0;
        g_fov[side] = fovStart;
        g_fov2[side] = fov2Start;
        std::memcpy(g_orig, g_origPristine, ORIG_SIZE);
        std::memcpy(static_cast<void *>(g_port), g_portPristine, sizeof(C_FirstPersonView));
        g_files.clear();
        g_openFiles.clear();
        g_nextFile = 1;
        if (!tableMissing)
            g_files["tables\\FpvAnims.sav"] = table;
        SetWorld(side);
        g_hudBlock[0] = g_w.hudShown;
        **OriginalAt<uint8_t **>(0x008aea18) = g_w.hudShown;
        if (fields)
            SetFields(side);
        for (const S_Op &op : ops) {
            Log("-- %d %d %d %d %s", op.kind, op.a, op.b, op.c, FloatText(op.f).c_str());
            if (op.kind == W_WORLD) {
                g_rng.seed(uint32_t(op.a));
                RandomWorld();
                SetWorld(side);
                g_hudBlock[0] = g_w.hudShown;
                **OriginalAt<uint8_t **>(0x008aea18) = g_w.hudShown;
            } else
                Run(side, op);
            g_log.push_back(Snap(side));
            g_log.push_back("camera " + FloatText(g_fov[side]) + " " + FloatText(g_fov2[side]));
        }
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
        std::memcpy(&g_runs[side].meshes, &g_meshes, sizeof g_meshes);
        g_runs[side].table = TableText(side);
    }
    // (+0xDC: the inline SetScale's stack word; put back from the start on both sides.)
    for (int i = 0; i < FRAMES; ++i)
        for (S_Run &r : g_runs)
            std::memcpy(r.arena.frames[i] + 0xDC, g_arenaStart.frames[i] + 0xDC, 4);
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ++checks;
    if (std::memcmp(&g_runs[0].meshes, &g_runs[1].meshes, sizeof g_meshes) != 0)
        CHECK(false, "the meshes differ");
    if (g_runs[0].table != g_runs[1].table) {
        // (The first line that differs.)
        size_t at = 0;
        while (at < g_runs[0].table.size() && at < g_runs[1].table.size() && g_runs[0].table[at] == g_runs[1].table[at])
            ++at;
        const size_t line = g_runs[0].table.rfind('\n', at == 0 ? 0 : at - 1);
        const size_t from = line == std::string::npos ? 0 : line + 1;
        CHECK(false, "the animation tables differ:\n  orig %s\n  port %s", g_runs[0].table.substr(from, g_runs[0].table.find('\n', from) - from).c_str(),
              g_runs[1].table.substr(from, g_runs[1].table.find('\n', from) - from).c_str());
    } else
        ++checks;
    if (std::getenv("HD2_FPV_TABLES") && std::atoi(std::getenv("HD2_FPV_TABLES")) == g_round) {
        std::printf("orig:\n%s\nport:\n%s\n", g_runs[0].table.c_str(), g_runs[1].table.c_str());
        if (FILE *f = std::fopen("fpv_table.bin", "wb")) {
            std::fwrite(table.data(), 1, table.size(), f);
            std::fclose(f);
        }
    }
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

void SetUp()
{
    g_partName = PartName;
    g_findChild = FindChild;
    // The frames' slots: the kit's, else the generated loggers'; the children by the world.
    {
        void *base[64];
        for (void *&v : base)
            v = reinterpret_cast<void *>(V_Other0);
        FillVt_I3D_frame(base);
        for (int k = 0; k < 64; ++k)
            if (!g_frameVt[k] || g_frameVt[k] == reinterpret_cast<void *>(F_Other))
                g_frameVt[k] = base[k];
    }
    g_frameVt[0x3C / 4] = reinterpret_cast<void *>(V_NumChildren);
    g_frameVt[0x40 / 4] = reinterpret_cast<void *>(V_GetChild);
    std::copy(g_frameVt, g_frameVt + 64, g_modelVt);
    FillVt_I3D_model(g_modelVt);
    std::copy(g_driverVt, g_driverVt + 32, g_bigDriverVt);
    for (int k = 32; k < 0x30; ++k)
        g_bigDriverVt[k] = reinterpret_cast<void *>(D_Other);
    g_bigDriverVt[0x54 / 4] = reinterpret_cast<void *>(D_CreateFrame);
    g_driver.vt = g_bigDriverVt;
    for (void **vt : {g_meshVt, g_levelVt, g_textureVt})
        for (int k = 0; k < 4; ++k)
            vt[k] = reinterpret_cast<void *>(X_Other);
    for (void *&v : g_meshVt)
        v = reinterpret_cast<void *>(X_Other);
    for (void *&v : g_levelVt)
        v = reinterpret_cast<void *>(X_Other);
    for (void *&v : g_materialVt)
        v = reinterpret_cast<void *>(X_Other);
    g_meshVt[0x04 / 4] = reinterpret_cast<void *>(M_GetLevel);
    g_levelVt[0x14 / 4] = reinterpret_cast<void *>(L_GetFaceGroup);
    g_materialVt[0x1C / 4] = reinterpret_cast<void *>(MT_SetTexture);
    g_materialVt[0x44 / 4] = reinterpret_cast<void *>(MT_Resolve);
    for (void *&v : g_animVt)
        v = reinterpret_cast<void *>(X_Other);
    g_animVt[0] = reinterpret_cast<void *>(A_Release);
    g_animVt[0x08 / 4] = reinterpret_cast<void *>(A_EndTime);
    for (auto &a : g_anims)
        *reinterpret_cast<void ***>(a) = g_animVt;
    // The camera module (the original's through its vtable; the port's methods by hand).
    for (void *&v : g_origCameraVt)
        v = reinterpret_cast<void *>(OC_Other);
    g_origCameraVt[0x44 / 4] = reinterpret_cast<void *>(OC_Special);
    g_origCameraVt[0x50 / 4] = reinterpret_cast<void *>(OC_SetFov);
    g_origCameraVt[0x54 / 4] = reinterpret_cast<void *>(OC_GetFov);
    g_origCameraVt[0x5C / 4] = reinterpret_cast<void *>(OC_SetFov2);
    g_origCameraVt[0x60 / 4] = reinterpret_cast<void *>(OC_GetFov2);
    g_origCameraVt[0x64 / 4] = reinterpret_cast<void *>(OC_Pos);
    g_origCameraVt[0x68 / 4] = reinterpret_cast<void *>(OC_Dir);
    *reinterpret_cast<void ***>(g_cameraModule[0]) = g_origCameraVt;
    // The effects, the sounds, the vehicles, the items, the definitions, the components: each side its vtable.
    for (int side = 0; side < 2; ++side) {
        for (void *&v : g_effectsVt[side])
            v = reinterpret_cast<void *>(E_Other);
        *reinterpret_cast<void ***>(g_effects[side]) = g_effectsVt[side];
        for (void **vt : {g_vehicleVt[side], g_itemVt[side], g_defVt[side], g_compVt[side]})
            for (int k = 0; k < 0x40; ++k)
                vt[k] = reinterpret_cast<void *>(V_Other);
        for (int k = 0x40; k < 0x80; ++k)
            g_vehicleVt[side][k] = reinterpret_cast<void *>(V_Other);
    }
    g_effectsVt[0][0x54 / 4] = reinterpret_cast<void *>(E_LightLevel);
    g_effectsVt[0][0xBC / 4] = reinterpret_cast<void *>(E_LensFlare);
    g_effectsVt[1][Slot(&C_GEffectsModule::SetModelLightLevel) / 4] = reinterpret_cast<void *>(E_LightLevel);
    g_effectsVt[1][Slot(&C_GEffectsModule::SetLensFlareOn) / 4] = reinterpret_cast<void *>(E_LensFlare);
    for (void *&v : g_origSoundVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origSoundVt[0x54 / 4] = reinterpret_cast<void *>(O_Play);
    *reinterpret_cast<void ***>(g_soundModule[0]) = g_origSoundVt;
    g_vehicleVt[0][0xC4 / 4] = reinterpret_cast<void *>(V_SeatView);
    g_vehicleVt[1][Slot(&C_Actor::ShowSeatView) / 4] = reinterpret_cast<void *>(V_SeatView);
    g_itemVt[0][0x0C / 4] = reinterpret_cast<void *>(V_Amount);
    g_itemVt[1][Slot(&C_Item::GetAmount) / 4] = reinterpret_cast<void *>(V_Amount);
    g_defVt[0][0x10 / 4] = reinterpret_cast<void *>(V_Behaviour);
    g_defVt[1][Slot(&C_ItemDef::GetBehaviour) / 4] = reinterpret_cast<void *>(V_Behaviour);
    g_compVt[0][0x18 / 4] = reinterpret_cast<void *>(V_ShotSound);
    g_compVt[1][Slot(&C_WeaponComp::GetShotSound) / 4] = reinterpret_cast<void *>(V_ShotSound);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        IFAKE_REDIRECTS
        {0x005a2b10, reinterpret_cast<const void *>(OH_Weapon)},
        {0x005a6da0, reinterpret_cast<const void *>(OH_Uniform)},
        {0x005a6730, reinterpret_cast<const void *>(OH_Belt)},
        {0x005a80f0, reinterpret_cast<const void *>(OH_Stance)},
        {0x005a8170, reinterpret_cast<const void *>(OH_ViewMode)},
        {0x005ac400, reinterpret_cast<const void *>(OH_PutAway)},
        {0x007df970, reinterpret_cast<const void *>(OH_FpvModel)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x00464e90, reinterpret_cast<const void *>(O_GetCameraModule)},
        {0x00486510, reinterpret_cast<const void *>(O_GetGEffects)},
        {0x004336c0, reinterpret_cast<const void *>(O_GetSoundModule)},
        {0x0041fc10, reinterpret_cast<const void *>(O_AnimGet)},
        {0x004290a0, reinterpret_cast<const void *>(O_ModelOpen)},
        {0x007e52d8, reinterpret_cast<const void *>(O_DbgPrintf)},
        {0x007e5302, reinterpret_cast<const void *>(O_RandFloat)},
        {0x007e6334, reinterpret_cast<const void *>(O_Sprintf)},
        {0x007e53a4, reinterpret_cast<const void *>(O_dtaOpen)},
        {0x007e53b0, reinterpret_cast<const void *>(O_dtaClose)},
        {0x007e5398, reinterpret_cast<const void *>(O_dtaSeek)},
        {0x007e539e, reinterpret_cast<const void *>(O_dtaRead)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // (The HUD option: the original's pointer to a block of the harness's.)
    static uint8_t origHud[16];
    *OriginalAt<uint8_t **>(0x008aea18) = origHud;
    // The views, built once: the original's by its constructor, the port's by its own.
    g_orig = static_cast<uint8_t *>(_aligned_malloc(ORIG_SIZE, 16));
    std::memset(g_orig, 0, ORIG_SIZE);
    OriginalAt<T_Ctor>(0x00490740)(g_orig);
    g_origPristine = static_cast<uint8_t *>(std::malloc(ORIG_SIZE));
    std::memcpy(g_origPristine, g_orig, ORIG_SIZE);
    g_port = new (_aligned_malloc(sizeof(C_FirstPersonView), 16)) C_FirstPersonView();
    g_portPristine = static_cast<uint8_t *>(std::malloc(sizeof(C_FirstPersonView)));
    std::memcpy(g_portPristine, static_cast<void *>(g_port), sizeof(C_FirstPersonView));
    g_log.clear();
}

// ---- the destructors: a view with animation sets loaded, names, stages blending out, destroyed on each side ----
void TestDestructors()
{
    std::vector<std::string> logs[2];
    const uint32_t seed = U32();
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_log.clear();
        std::mt19937 rng(seed);
        uint8_t *block = static_cast<uint8_t *>(_aligned_malloc(ORIG_SIZE + 0x100, 16));
        std::memset(block, 0, ORIG_SIZE + 0x100);
        C_FirstPersonView *p = nullptr;
        if (side)
            p = new (block) C_FirstPersonView();
        else
            OriginalAt<T_Ctor>(0x00490740)(block);
        const uint32_t table = side ? OF(m_anims) : 0xA0;
        for (int n = int(rng() % 6); n > 0; --n) {
            const int k = int(rng() % uint32_t(C_FirstPersonView::MAX_ITEM_TYPES * FPV_EVENT_COUNT)), v = int(rng() % 4);
            Put<void *>(block, table + 0x30 * uint32_t(k) + 4 * uint32_t(v), g_anims[rng() % ANIMS]);
        }
        Log("destroy");
        if (side) {
            p->~C_FirstPersonView();
        } else {
            OriginalAt<T_V>(0x004908b0)(block);
        }
        _aligned_free(block);
        logs[side].swap(g_log);
    }
    CompareLogs(logs[0], logs[1]);
}

// ---- the port's fixes, alone (the original crashes or reads and writes outside its table) ----
bool Logged(const char *what)
{
    return std::any_of(g_log.begin(), g_log.end(), [what](const std::string &l) { return l.find(what) != std::string::npos; });
}
void TestFixes()
{
    g_side = 1;
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    SetKinds();
    RandomWorld();
    RandomMeshes();
    g_w.role = 7;
    g_w.active = 0;
    g_w.weapon[0] = 0;
    g_w.putAway[0] = false;
    g_w.health[0] = 50;
    g_w.vehicle[0] = -1;
    g_w.openAlways = true;
    g_w.uniform[0] = 0;
    g_w.itemType[0] = 38;
    g_w.amount[0] = 1.0f;
    g_w.fpvModel[g_w.itemDef[0]] = 1;
    // The table: item ids past the types and event ids past the events (the original writes there), then a good one.
    {
        std::string items;
        auto chunk = [](uint16_t id, const std::string &payload) {
            std::string c;
            const uint32_t size = uint32_t(6 + payload.size());
            c.append(reinterpret_cast<const char *>(&id), 2);
            c.append(reinterpret_cast<const char *>(&size), 4);
            return c + payload;
        };
        const int ratio = 100;
        const std::string variant = chunk(0x10, chunk(1000, std::string("#FPVfix.i3d") + std::string(1, '\0')) +
                                                    std::string(reinterpret_cast<const char *>(&ratio), 4));
        for (int item : {700, 102, 101}) {
            std::string events;
            for (int e = 0; e < FPV_EVENT_COUNT; ++e)
                events += chunk(uint16_t(item == 102 ? 2000 + 13 + e : 2000 + e), variant);
            items += chunk(uint16_t(item), events);
        }
        g_files.clear();
        g_files["tables\\FpvAnims.sav"] = chunk(CHUNK_FPV_ANIMS, items);
    }
    SetWorld(1);
    std::memcpy(static_cast<void *>(g_port), g_portPristine, sizeof(C_FirstPersonView));
    g_retCounts.clear();
    g_models = 0;
    // A uniform without a first-person model: the default hands' name, "".
    g_w.fpvName[g_w.itemDef[WEAPONS + 0]] = -1;
    SetWorld(1);
    g_log.clear();
    g_port->Init();
    int written = 0;
    for (int t = 0; t < C_FirstPersonView::MAX_ITEM_TYPES; ++t)
        for (int e = 0; e < FPV_EVENT_COUNT; ++e)
            written += !g_port->m_anims[t][e].names[0].IsEmpty();
    CHECK(written == FPV_EVENT_COUNT && !g_port->m_anims[1][0].names[0].IsEmpty(), "the table's ids out of range skipped: %d written", written);
    CHECK(Logged("models OpenInto f2  10 ->") || Logged("models OpenInto f2  0 ->"), "a uniform without a first-person model: no name");
    // The Panzerfaust's warhead missing: Refresh goes on (the original took the frame for granted).
    g_w.warhead = false;
    g_log.clear();
    g_port->Refresh();
    CHECK(Logged("FindChildFrame ammo") && g_port->m_model != nullptr, "the warhead missing: Refresh went on");
    // The idle event without a hands weapon (the original took it for granted).
    g_w.weapon[0] = -1;
    g_port->m_aimView = false;
    g_port->m_pose = FPV_POSE_RAISED;
    g_log.clear();
    g_port->OnInventoryEvent(FPV_EVENT_IDLE, FPV_EVENT_IDLE);
    CHECK(Logged("C_Human::GetHandsWeapon") && !Logged("GetAmount"), "the idle event without a weapon");
    // PlayAnim outside the table: events past it, item types past it (the original read outside): the neighbouring
    // entries a read there would reach hold animations.
    g_port->m_itemType = 38;
    for (S_FpvAnim *a : {&g_port->m_anims[39][0], &g_port->m_anims[39][6], &g_port->m_anims[37][12]}) {
        a->names[0] = "#FPVfix.i3d";
        a->ratios[0] = 100;
    }
    for (int event : {13, 19, -1}) {
        g_log.clear();
        g_port->PlayAnim(event);
        CHECK(!Logged("SetAnimation") && !Logged("anims Get"), "PlayAnim %d: nothing played", event);
    }
    for (int type : {-1, 500}) {
        g_port->m_itemType = type;
        g_log.clear();
        g_port->PlayAnim(FPV_EVENT_IDLE);
        CHECK(!Logged("SetAnimation") && !Logged("anims Get"), "PlayAnim of type %d: nothing played", type);
    }
    // The draw's sound of a type without a definition (the original read the missing one).
    g_port->m_itemType = 38;
    g_portDefTable[38] = nullptr;
    g_port->m_on = true;
    g_log.clear();
    g_port->PlayEventSound(FPV_EVENT_DRAW);
    CHECK(!Logged("sounds Play"), "the draw's sound without a definition: none");
}

}  // namespace H

// (HD2_FPV_STACK: an access violation's stack words that point into this image, linked addresses.)
LONG WINAPI StackWords(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uint32_t *sp = reinterpret_cast<const uint32_t *>(e->ContextRecord->Esp);
    const CONTEXT &c = *e->ContextRecord;
    std::printf("eip %08lx eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx (orig object %p)\n", c.Eip, c.Eax, c.Ebx, c.Ecx,
                c.Edx, c.Esi, c.Edi, c.Ebp, static_cast<void *>(H::g_orig));
    std::printf("stack words:");
    for (int k = 0; k < 256; ++k)
        if (sp[k] >= base && sp[k] < base + 0x200000)
            std::printf(" %08x", unsigned(sp[k] - base + 0x20000000));
    std::printf("\n");
    return EXCEPTION_CONTINUE_SEARCH;
}
int main()
{
    using namespace H;
    int exitCode = 0;
    if (std::getenv("HD2_FPV_STACK"))
        AddVectoredExceptionHandler(1, StackWords);
    if (!StartKit("fpv_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_FPV_QUICK") ? 2500 : 6000;
    if (const char *r = std::getenv("HD2_FPV_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "View";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    g_case = "Targeted";
    g_targeted = true;
    for (g_round = 0; g_round < rounds / 2; ++g_round)
        TestScenarios();
    g_targeted = false;
    g_case = "Destructors";
    for (g_round = 0; g_round < 30; ++g_round)
        TestDestructors();
    g_case = "Fixes";
    g_round = 0;
    TestFixes();
    std::printf("fpv_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
