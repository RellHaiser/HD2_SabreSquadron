// Part 7f's test (the actors build: build_item.bat also links src/game/door.cpp, supply_box.cpp and
// barrel.cpp; main runs PropActorsTest after SeatActorsTest on every model): the door, the supply box
// and the barrel against oracles written from the original's code (the asm where the decompile is
// unreliable: the doors' swing tests, the supply box's fall and feet, the barrel's holes). Engine
// and game calls are logged in order through item_test.inc's log; the harness's records give the
// activations, volumes, rays, sounds and messages; saved games and properties are compared byte for
// byte. Uses the earlier parts' helpers. check_item.py reads the "prop:" line.

#include "game/barrel.h"
#include "game/door.h"
#include "game/supply_box.h"

// --- the effects: the fades, the debug lines, the particles stopped, the parts' materials ----------------

void C_GEffectsModule::FadeInModel(I3D_frame *model, int time, bool turnOn)
{
    ItLog("fadein %s %d %d", ItName(model).c_str(), time, turnOn);
}
void C_GEffectsModule::FadeOutHierarchy(I3D_frame *model, int time, bool unknown)
{
    ItLog("fadeout %s %d %d", ItName(model).c_str(), time, unknown);
}
float g_scanMax[3] = {0.0f, 1000.0f, 0.0f};
struct S_PrLine {
    S_vector from, to;
    uint32_t color, color2;
};
std::vector<S_PrLine> g_prLines;
void DrawDebugLine(const float *from, const float *to, uint32_t color, uint32_t color2)
{
    g_prLines.push_back({S_vector(from[0], from[1], from[2]), S_vector(to[0], to[1], to[2]), color, color2});
}
void C_ParticlesModule::Finish(int handle)
{
    ItLog("finish %d", handle);
}
std::map<const I3D_frame *, int> g_prMaterials;
int GetFrameMaterial(const I3D_frame *frame)
{
    auto it = g_prMaterials.find(frame);
    return it == g_prMaterials.end() ? -1 : it->second;
}
C_Human *g_prPlayer;
C_Human *C_Team::GetActivePlayer() const
{
    return g_prPlayer;
}

// S_quat::SetDir (the barrel's slosh; level directions, no roll here): the rotation looking along
// `dir` with y up.
std::vector<std::pair<S_vector, float>> g_prSetDirs;    // (the directions and rolls asked)
void __stdcall S_quat::SetDir(const S_vector &dir, float roll)
{
    g_prSetDirs.emplace_back(dir, roll);
    S_matrix m;
    m.Identity();
    m.SetDir3(dir, S_vector(0.0f, 1.0f, 0.0f));
    Make(m);
}

// --- the model slots the supply box calls (its crate is a model): StopAnimation (0x8C),
// SetAnimWeight (0x94), GetAnimTime (0xA0: g_prAnimTime), IsAnimActive (0xAC: g_prAnimActive);
// the morph's DuplicateMesh (0xA0 of its own vtable) -----------------------------------------------------

int g_prAnimTime = 123;
bool g_prAnimActive = true;
void __stdcall F_PrStopAnimation(I3D_frame *self, int stage)
{
    ItLog("stopanim %s %d", ItName(self).c_str(), stage);
}
void __stdcall F_PrAnimWeight(I3D_frame *self, int stage, float weight)
{
    ItLog("animweight %s %d %.9g", ItName(self).c_str(), stage, weight);
}
int __stdcall F_PrAnimTime(I3D_frame *self, int stage)
{
    ItLog("getanimtime %s %d", ItName(self).c_str(), stage);
    return g_prAnimTime;
}
bool __stdcall F_PrAnimActive(I3D_frame *self, int stage)
{
    ItLog("animactive %s %d", ItName(self).c_str(), stage);
    return g_prAnimActive;
}
void *g_prVisualVtable[SLOTS];
void __stdcall F_PrDuplicateMesh(I3D_frame *self)
{
    ItLog("dupmesh %s", ItName(self).c_str());
}

// --- the walls' mesh (the supply box's feet): a level of 0x20-byte vertices --------------------------------

struct S_PrLevel {
    void **vtable;
    uint8_t unknown04[0x50];
    int count;                                          // +0x54
    std::vector<float> vertices;
};
struct S_PrMesh {
    void **vtable;
    S_PrLevel *level;
};
void *g_prLevelVtable[4];
void *g_prMeshVtable[4];
void *__stdcall F_PrGetVertices(S_PrLevel *self, int unknown)
{
    ItLog("getvertices %d", unknown);
    return self->vertices.data();
}
void __stdcall F_PrUnlockVertices(S_PrLevel *)
{
    ItLog("unlockvertices");
}
S_PrLevel *__stdcall F_PrGetLevel(S_PrMesh *self, int index)
{
    ItLog("getlevel %d", index);
    return self->level;
}
// Gives `visual` a mesh of `points` (x, y, z each).
S_PrMesh *PrMesh(I3D_frame *visual, const std::vector<S_vector> &points)
{
    g_prLevelVtable[1] = reinterpret_cast<void *>(&F_PrGetVertices);
    g_prLevelVtable[2] = reinterpret_cast<void *>(&F_PrUnlockVertices);
    g_prMeshVtable[1] = reinterpret_cast<void *>(&F_PrGetLevel);
    auto *level = new S_PrLevel();
    level->vtable = g_prLevelVtable;
    level->count = static_cast<int>(points.size());
    for (const S_vector &p : points) {
        const float v[8] = {p.x, p.y, p.z, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f};
        level->vertices.insert(level->vertices.end(), v, v + 8);
    }
    auto *mesh = new S_PrMesh{g_prMeshVtable, level};
    *reinterpret_cast<S_PrMesh **>(reinterpret_cast<uint8_t *>(visual) + 0x1E0) = mesh;
    return mesh;
}

// --- portals (the doors'): SetOn logged by name; a sector's own and incoming ones ---------------------------

void *g_prPortalVtable[2];
std::map<const void *, std::string> g_prPortalNames;
void __stdcall F_PrPortalSetOn(I3D_portal *self, bool on)
{
    ItLog("portal %s %d", g_prPortalNames[self].c_str(), on);
}
I3D_portal *PrPortal(const std::string &name, const S_vector &center, float radius)
{
    g_prPortalVtable[0] = reinterpret_cast<void *>(&F_PrPortalSetOn);
    auto *mem = static_cast<uint8_t *>(::operator new(0x1A0));
    std::memset(mem, 0, 0x1A0);
    *reinterpret_cast<void ***>(mem) = g_prPortalVtable;
    std::memcpy(mem + 0x180, &center, sizeof center);
    std::memcpy(mem + 0x190, &radius, sizeof radius);
    auto *portal = reinterpret_cast<I3D_portal *>(mem);
    g_prPortalNames[portal] = name;
    return portal;
}
void PrSetOpposite(I3D_portal *portal, I3D_portal *opposite)
{
    std::memcpy(reinterpret_cast<uint8_t *>(portal) + 0x19C, &opposite, sizeof opposite);
}
// The sector's own portals (+0x24C / +0x254) and incoming ones (+0x25C / +0x264).
void PrSetPortals(I3D_frame *sector, std::vector<I3D_portal *> *own, std::vector<I3D_portal *> *incoming)
{
    auto *base = reinterpret_cast<uint8_t *>(sector);
    I3D_portal **ownData = own->data();
    I3D_portal **inData = incoming->data();
    const int ownCount = static_cast<int>(own->size()), inCount = static_cast<int>(incoming->size());
    std::memcpy(base + 0x24C, &ownData, sizeof ownData);
    std::memcpy(base + 0x254, &ownCount, sizeof ownCount);
    std::memcpy(base + 0x25C, &inData, sizeof inData);
    std::memcpy(base + 0x264, &inCount, sizeof inCount);
}

// --- the test ----------------------------------------------------------------------------------------------

struct S_Pr {
    bool door = true, doorProps = true, doorStart = true, doorState = true, doorMove = true, doorSlide = true,
         doorNet = true, doorSave = true, doorPortals = true, doorStop = true, box = true, boxProps = true,
         boxStart = true, boxFeet = true, boxStates = true, boxFall = true, boxNet = true, boxSave = true,
         boxStop = true, barrel = true, barrelProps = true, barrelStart = true, barrelHit = true, barrelLeak = true,
         barrelSlosh = true, barrelDestroy = true, barrelNet = true, barrelSave = true,
         barrelStop = true;
};
#define PR(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

// S_vector::Normalize as the game's inline copies compile (the square summed by the caller).
void PrNormalize(S_vector &v, float sq)
{
    if (std::fabs(sq - 1.0f) < 1e-08f)
        return;
    if (!(sq < 1e-08f)) {
        const float k = 1.0f / std::sqrt(sq);
        v = S_vector(v.x * k, v.y * k, v.z * k);
    } else if (v.x != 0.0f) {
        v.x = v.x < 0.0f ? -1.0f : 1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z < 0.0f ? -1.0f : 1.0f;
    } else {
        v.y = v.y < 0.0f ? -1.0f : 1.0f;
    }
}
// S_vector::Magnitude inline (the bits of the square against 1e-8).
float PrLength(float sq)
{
    uint32_t bits;
    std::memcpy(&bits, &sq, 4);
    if (static_cast<int32_t>(bits) < 0x322bcc77)
        return 0.0f;
    if (!(std::fabs(sq - 1.0f) < 1e-08f))
        return std::sqrt(sq);
    return 1.0f;
}
// A stream's bits (value, count) as written.
std::vector<std::pair<uint32_t, int>> PrBits(size_t index)
{
    return index < g_streams.size() ? g_streams[index]->bits : std::vector<std::pair<uint32_t, int>>{};
}
// (Debugging: two values with a tag, on stderr.)
void PrShow(const char *what, double a, double b)
{
    std::fprintf(stderr, "%s %.9g %.9g\n", what, a, b);
}
void PrDump(const char *what)
{
    std::fprintf(stderr, "%s:", what);
    for (const std::string &x : g_itLog)
        std::fprintf(stderr, " [%s]", x.c_str());
    std::fprintf(stderr, "\n");
}
uint32_t PrFloatBits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

void PropActorsTest()
{
    const int frameSlots[] = {0x00, 0x08, 0x0C, 0x1C, 0x20, 0x24, 0x30, 0x34, 0x38, 0x3C, 0x58, 0x84,
                              0x8C, 0x90, 0x94, 0x9C, 0xA0, 0xAC};
    constexpr int FRAME_SLOTS = sizeof frameSlots / sizeof frameSlots[0];
    void *oldFrame[FRAME_SLOTS];
    for (int i = 0; i < FRAME_SLOTS; ++i)
        oldFrame[i] = g_frameVtable[frameSlots[i] / 4];
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_frameVtable[0x0C / 4] = reinterpret_cast<void *>(&F_ObSetDir);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_ObSetWorldDir);
    g_frameVtable[0x20 / 4] = reinterpret_cast<void *>(&F_SeTick);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x34 / 4] = reinterpret_cast<void *>(&F_SeSetName);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_WaUpdateBound);
    g_frameVtable[0x84 / 4] = reinterpret_cast<void *>(&F_SeSetAnimation);
    g_frameVtable[0x8C / 4] = reinterpret_cast<void *>(&F_PrStopAnimation);
    g_frameVtable[0x90 / 4] = reinterpret_cast<void *>(&F_SeLocalBound);
    g_frameVtable[0x94 / 4] = reinterpret_cast<void *>(&F_PrAnimWeight);
    g_frameVtable[0x9C / 4] = reinterpret_cast<void *>(&F_Se9C);
    g_frameVtable[0xA0 / 4] = reinterpret_cast<void *>(&F_PrAnimTime);
    g_frameVtable[0xAC / 4] = reinterpret_cast<void *>(&F_PrAnimActive);
    std::memcpy(g_prVisualVtable, g_frameVtable, sizeof g_prVisualVtable);
    g_prVisualVtable[0xA0 / 4] = reinterpret_cast<void *>(&F_PrDuplicateMesh);
    void *const oldCreateFrame = g_driverVtable[0x54 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_ObCreateFrame);
    I3D_scene *const oldScenePtr = GetGame()->m_scene;
    GetGame()->m_scene = g_carScene;
    void *const oldFind = g_sceneVtable[0x7C / 4];
    void *const oldSector = g_sceneVtable[0xEC / 4];
    void *const oldGetSector = g_sceneVtable[0xE0 / 4];
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_MaFindFrame);
    g_sceneVtable[0xEC / 4] = reinterpret_cast<void *>(&F_ItUpdateFrameSector);
    g_sceneVtable[0xE0 / 4] = reinterpret_cast<void *>(&F_ItGetSector);
    auto *sceneBase = reinterpret_cast<uint8_t *>(g_carScene);
    I3D_frame *const oldDynamicRoot = *reinterpret_cast<I3D_frame **>(sceneBase + 0x200);
    const int oldStamp = *reinterpret_cast<int *>(sceneBase + 0x11C);
    const uint8_t oldRole = g_gameRole;
    const bool oldTextIds = g_textIds, oldReliable = g_reliableActorStreams;
    const int oldSoundSlot = g_soundSlot, oldSoundStep = g_soundSlotStep;
    g_reliableActorStreams = true;
    g_textIds = true;
    S_Pr r;

    int seed = static_cast<int>(g_frames.size()) * 31 + 7;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    const auto reset = [&] {
        g_itLog.clear();
        g_activations.clear();
        g_deactivations.clear();
        g_volumeLog.clear();
        g_waRays.clear();
        g_soundRequests.clear();
        g_soundFiles.clear();
        g_soundLog.clear();
        g_uses.clear();
        g_prLines.clear();
        g_rbSplashes.clear();
        g_particlesDestroyed.clear();
        ClearStreams();
    };

#include "prop_door.inc"
#include "prop_supply.inc"
#include "prop_barrel.inc"

    g_gameRole = oldRole;
    g_textIds = oldTextIds;
    g_reliableActorStreams = oldReliable;
    g_soundSlot = oldSoundSlot;
    g_soundSlotStep = oldSoundStep;
    g_waBlockAt = -1;
    g_prPlayer = nullptr;
    g_prMaterials.clear();
    g_maSceneFrames.clear();
    g_maSlotSounds.clear();
    g_readBits.clear();
    g_readPos = 0;
    g_flatGround = false;
    g_rayTestHook = nullptr;
    g_sphereTestHook = nullptr;
    *reinterpret_cast<I3D_frame **>(sceneBase + 0x200) = oldDynamicRoot;
    *reinterpret_cast<int *>(sceneBase + 0x11C) = oldStamp;
    // (The next model frees this one's frames: none left in the primary sector's children, none of
    // their names kept for the addresses.)
    g_sector->m_firstChild = nullptr;
    for (S_StubFrame *stub : g_frames)
        g_itNames.erase(stub->frame);
    GetGame()->m_scene = oldScenePtr;
    g_sceneVtable[0x7C / 4] = oldFind;
    g_sceneVtable[0xEC / 4] = oldSector;
    g_sceneVtable[0xE0 / 4] = oldGetSector;
    for (int i = 0; i < FRAME_SLOTS; ++i)
        g_frameVtable[frameSlots[i] / 4] = oldFrame[i];
    g_driverVtable[0x54 / 4] = oldCreateFrame;
    reset();
    std::printf("prop: door %d doorProps %d doorStart %d doorState %d doorMove %d doorSlide %d doorNet %d "
                "doorSave %d doorPortals %d doorStop %d box %d boxProps %d boxStart %d boxFeet %d boxStates %d "
                "boxFall %d boxNet %d boxSave %d boxStop %d barrel %d barrelProps %d barrelStart %d barrelHit %d "
                "barrelLeak %d barrelSlosh %d barrelDestroy %d barrelNet %d barrelSave %d barrelStop %d\n",
                r.door, r.doorProps, r.doorStart, r.doorState, r.doorMove, r.doorSlide, r.doorNet, r.doorSave,
                r.doorPortals, r.doorStop, r.box, r.boxProps, r.boxStart, r.boxFeet, r.boxStates, r.boxFall,
                r.boxNet, r.boxSave, r.boxStop, r.barrel, r.barrelProps, r.barrelStart, r.barrelHit, r.barrelLeak,
                r.barrelSlosh, r.barrelDestroy, r.barrelNet, r.barrelSave, r.barrelStop);
}
