// Part 7d's test (the actors build: build_item.bat also links src/game/mp_zone.cpp, submarine.cpp,
// train.cpp and track.cpp; main runs MovingActorsTest after WorldActorsTest on every model): the
// train, the submarine and the multiplayer zone against oracles written from the original's code
// (the asm where the decompile is unreliable: the train's wheels, rods and movement, the
// submarine's movement and exits, the zone's capture). Engine and game calls are logged in order
// through item_test.inc's log; the harness's records give the activations, volumes, sounds and
// messages; saved games and properties are compared byte for byte. Uses the earlier parts'
// helpers. check_item.py reads the "moving:" line.

#include "game/frame_volume.h"
#include "game/interface.h"
#include "game/mp_zone.h"
#include "game/submarine.h"
#include "game/track.h"
#include "game/train.h"

// --- LS3DF's math the new actors use (../LS3DF's port) ---------------------------------------------

S_matrix __stdcall RotateXMatrix(float angle)
{
    S_matrix r;
    r.Identity();
    const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
    const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
    r.m[1][1] = c;
    r.m[1][2] = s;
    r.m[2][1] = -s;
    r.m[2][2] = c;
    return r;
}
S_matrix __stdcall RotateYMatrix(float angle)
{
    S_matrix r;
    r.Identity();
    const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
    const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
    r.m[0][0] = c;
    r.m[0][2] = s;
    r.m[2][0] = -s;
    r.m[2][2] = c;
    return r;
}
S_vector __stdcall S_vector::RotateByMatrix(const S_matrix &m) const
{
    return S_vector((x * m.m[0][0] + y * m.m[1][0]) + z * m.m[2][0], (x * m.m[0][1] + y * m.m[1][1]) + z * m.m[2][1],
                    (x * m.m[0][2] + y * m.m[1][2]) + z * m.m[2][2]);
}
S_vector __stdcall S_quat::GetDir() const
{
    if (std::fabs(s) >= 1.0f)
        return S_vector(0.0f, 0.0f, 1.0f);
    const float xx = (v.x + v.x) * v.x;
    const float yy = (v.y + v.y) * v.y;
    const float zy = (v.z + v.z) * v.y;
    const float xw = (v.x + v.x) * s;
    return S_vector((v.z + v.z) * v.x - (v.y + v.y) * s, xw + zy, 1.0f - (yy + xx));
}
double MaAcosD(float x)
{
    const float t = std::sqrt((1.0f + x) * (1.0f - x));
    return std::atan2(static_cast<double>(t), static_cast<double>(x));
}
S_quat __stdcall S_quat::Slerp(const S_quat &q, float t, bool) const
{
    float cosom = ((q.v.z * v.z + q.v.y * v.y) + q.v.x * v.x) + s * q.s;
    S_quat to = q;
    if (cosom < 0.0f) {
        cosom = -cosom;
        to.v = S_vector(-q.v.x, -q.v.y, -q.v.z);
        to.s = -q.s;
    }
    float k0, k1;
    if (1.0f - cosom > 0.0001) {
        const double omegaD = MaAcosD(cosom);
        const float omega = static_cast<float>(omegaD);
        const float inv = static_cast<float>(1.0 / std::sin(omegaD));
        k0 = static_cast<float>(std::sin(static_cast<double>((1.0f - t) * omega)) * inv);
        k1 = static_cast<float>(std::sin(static_cast<double>(omega * t)) * inv);
    } else {
        k0 = 1.0f - t;
        k1 = t;
    }
    S_quat r;
    r.v.x = k0 * v.x + k1 * to.v.x;
    r.v.y = to.v.y * k1 + k0 * v.y;
    r.v.z = to.v.z * k1 + k0 * v.z;
    r.s = k0 * s + k1 * to.s;
    return r;
}

// --- the sound driver, the sound module's frames and slots ----------------------------------------

bool g_maSoundOn = true;                                // ISND_driver::IsInitialized
bool __stdcall F_MaSndInitialized(void *)
{
    return g_maSoundOn;
}
void *g_maSndVtable[8] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&F_MaSndInitialized)};
struct {
    void **vtable;
} g_maSndDriver = {g_maSndVtable};
extern "C" ISND_driver *ISndGetDriver()
{
    return reinterpret_cast<ISND_driver *>(&g_maSndDriver);
}
// The sound frames RegisterSoundFrame made, their SetSectorFactor calls logged.
std::vector<S_SoundFrame *> g_maSoundFrames;
S_SoundFrame::S_SoundFrame(I3D_sound *s, bool track) : sound(s), trackSector(track), sector(nullptr), baseVolume(1.0f) {}
S_SoundFrame::~S_SoundFrame() {}
void S_SoundFrame::SetSectorFactor(float factor)
{
    ItLog("sectorfactor %s %.9g %.9g", ItName(sound).c_str(), factor, baseVolume);
    sectorFactor = factor;
}
S_SoundFrame *C_SoundModule::RegisterSoundFrame(I3D_sound *sound, bool trackSector)
{
    ItLog("register %s %d", ItName(sound).c_str(), trackSector);
    g_maSoundFrames.push_back(new S_SoundFrame(sound, trackSector));
    g_maSoundFrames.back()->sectorFactor = 0.5f + 0.125f * static_cast<float>(g_maSoundFrames.size() % 4);
    return g_maSoundFrames.back();
}
// The module's sounds by slot (the submarine's).
std::map<int, I3D_sound *> g_maSlotSounds;
I3D_sound *C_SoundModule::GetSound(int slot) const
{
    auto it = g_maSlotSounds.find(slot);
    return it == g_maSlotSounds.end() ? nullptr : it->second;
}
// The driver's frames: sounds through the harness's fake sound (named "snd<n>" by creation), the
// rest as the objective test's; CreateFrame fails after g_maFramesLeft frames (when >= 0).
int g_maFramesLeft = -1;
int g_maSoundsMade;
I3D_frame *__stdcall F_MaCreateFrame(void *driver, int type)
{
    if (type != FRAME_SOUND)
        return F_ObCreateFrame(driver, type);
    ItLog("createframe %d", type);
    if (g_maFramesLeft == 0)
        return nullptr;
    if (g_maFramesLeft > 0)
        --g_maFramesLeft;
    I3D_frame *f = MakeSound();
    g_itNames[f] = "snd" + std::to_string(g_maSoundsMade++);
    auto *stub = new S_StubFrame();                     // (a stub: the hierarchy boxes go through it)
    stub->frame = f;
    stub->name = g_itNames[f];
    g_stubs[f] = stub;
    return f;
}

// The scene's frames by name (FindFrame, slot 0x7C: the type mask honoured).
std::map<std::string, I3D_frame *> g_maSceneFrames;
I3D_frame *__stdcall F_MaFindFrame(void *, const char *name, uint32_t mask)
{
    ItLog("find %s %x", name, mask);
    auto it = g_maSceneFrames.find(name);
    return it != g_maSceneFrames.end() && (mask & (1u << (it->second->GetType() - 1))) ? it->second : nullptr;
}

// The sphere sweeps (the train's runs over humans): recorded; the `g_maSphereHitAt`-th (from 0)
// meets g_maSphereHit.
struct S_MaSphere {
    S_vector from, dir;
    float radius;
    uint32_t types, flags;
};
std::vector<S_MaSphere> g_maSpheres;
int g_maSphereHitAt = -1;
std::set<int> g_maSphereHitsAt;                         // (more of them)
S_CollisionVolume *g_maSphereHit;
S_CollisionVolume *MaSphereTest(const S_vector &from, const S_vector &dir, float radius, uint32_t types, uint32_t flags)
{
    g_maSpheres.push_back({from, dir, radius, types, flags});
    const int at = static_cast<int>(g_maSpheres.size()) - 1;
    return at == g_maSphereHitAt || g_maSphereHitsAt.count(at) ? g_maSphereHit : nullptr;
}

// The real C_MPZone::PickSpawnPosition (the spawn point's calls reach the 7b fake through --wrap).
bool __attribute__((thiscall)) MaPickSpawnPosition(C_MPZone *self, S_vector4 *pos, S_vector4 *dir, bool cycle)
    __asm__("___real__ZN8C_MPZone17PickSpawnPositionEP9S_vector4S1_b");

// --- the checkpoints' links -------------------------------------------------------------------------

S_CheckpointLink *S_Checkpoint::GetLink(int index)
{
    return index >= 0 && index < GetLinkCount() ? &links[index] : nullptr;
}

// --- the multiplayer zone's game side: the players, the scores, the objectives, the flag's vertex
// animation (logged) ------------------------------------------------------------------------------------

int g_maLocalTeam;
int g_maPlayerCount;
int C_NetGame::GetLocalTeam()
{
    return g_maLocalTeam;
}
int C_NetGame::GetPlayerCount() const
{
    return g_maPlayerCount;
}
void C_NetGame::AddScore(int slot, int points)
{
    ItLog("score %d %d", slot, points);
}
void C_NetGame::AddTeamScore(int team, float points)
{
    ItLog("teamscore %d %.9g", team, points);
}
void C_Interface::SetObjectiveIcon(int index, int state)
{
    ItLog("icon %d %d", index, state);
}
void C_game::SetObjectiveStatus(int objective, int status)
{
    ItLog("objective %d %d", objective, status);
}
C_VertexAnimModule g_vertexAnimModule;
std::map<std::string, S_VertexAnimation *> g_maAnimations;
S_VertexAnimation *C_VertexAnimModule::FindAnimation(const C_str &name)
{
    const std::string n = static_cast<const char *>(name);
    ItLog("findanim %s", n.c_str());
    auto it = g_maAnimations.find(n);
    return it == g_maAnimations.end() ? nullptr : it->second;
}
void C_VertexAnimModule::RemovePlayer(S_VertexAnimPlayer *player, bool destroy)
{
    ItLog("removeplayer %s %d", ItName(player->visual).c_str(), destroy);
    if (destroy)
        delete player;
}
void C_VertexAnimModule::AddPlayer(S_VertexAnimPlayer *player)
{
    ItLog("addplayer %s %d", ItName(player->visual).c_str(), player->frameTime);
}
void S_VertexAnimPlayer::Reset()
{
    ItLog("animreset %s %d", ItName(visual).c_str(), frame);
}
void S_VertexAnimPlayer::Init(I3D_frame *v, S_VertexAnimation *a)
{
    ItLog("animinit %s", ItName(v).c_str());
    visual = v;
    animation = a;
}

// --- the test ----------------------------------------------------------------------------------------------

struct S_Ma {
    bool train = true, trainProps = true, trainStart = true, trainPath = true, trainMove = true, trainWheels = true,
         trainSounds = true, trainSave = true, sub = true, subProps = true, subStart = true, subSeats = true,
         subExit = true, subMove = true, subSounds = true, subSave = true, zone = true, zoneProps = true,
         zoneStart = true, zoneCapture = true, zoneState = true, zoneSpawn = true;
};
#define MA(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

void MovingActorsTest()
{
    const int frameSlots[] = {0x00, 0x08, 0x0C, 0x1C, 0x24, 0x30, 0x38, 0x3C, 0x58};
    void *oldFrame[9];
    for (int i = 0; i < 9; ++i)
        oldFrame[i] = g_frameVtable[frameSlots[i] / 4];
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_frameVtable[0x0C / 4] = reinterpret_cast<void *>(&F_ObSetDir);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_ObSetWorldDir);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_WaUpdateBound);
    const int soundSlots[] = {0x00, 0x24};
    void *oldSound[2];
    for (int i = 0; i < 2; ++i)
        oldSound[i] = g_soundVtable[soundSlots[i] / 4];
    g_soundVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_soundVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    void *const oldCreateFrame = g_driverVtable[0x54 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_MaCreateFrame);
    I3D_scene *const oldScenePtr = GetGame()->m_scene;
    GetGame()->m_scene = g_carScene;
    void *const oldFind = g_sceneVtable[0x7C / 4];
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_MaFindFrame);
    const uint8_t oldRole = g_gameRole;
    // (Everything else the earlier parts' tests read, put back at the end: several models run in turn.)
    const S_MultiplayerSettings oldSettings = g_mpSettingsValue;
    const int oldLocalSlot = g_localPlayerSlot;
    C_Human *const oldLocalHuman = g_localHuman;
    const uint32_t oldMissionTime = GetGame()->m_missionTime, oldTime = GetGame()->m_time;
    std::vector<uint8_t> oldPlayers(sizeof g_obPlayers);
    std::memcpy(oldPlayers.data(), g_obPlayers, sizeof g_obPlayers);
    const int oldBlocked = g_obBlocked;
    const bool oldCoop = g_coop, oldFirstPerson = g_firstPerson, oldTextIds = g_textIds, oldReliable = g_reliableActorStreams;
    g_sphereTestHook = &MaSphereTest;
    C_Checkpoints *const oldCheckpoints = GetGame()->m_checkpoints;
    S_Ma r;

    int seed = static_cast<int>(g_frames.size()) * 17 + 3;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    const auto reset = [&] {
        g_itLog.clear();
        g_activations.clear();
        g_deactivations.clear();
        g_volumeLog.clear();
        g_soundLog.clear();
        g_maSpheres.clear();
        g_finds.clear();
        ClearStreams();
    };

#include "moving_train.inc"
#include "moving_submarine.inc"
#include "moving_zone.inc"

    g_gameRole = oldRole;
    g_mpSettingsValue = oldSettings;
    g_localPlayerSlot = oldLocalSlot;
    g_localHuman = oldLocalHuman;
    GetGame()->m_missionTime = oldMissionTime;
    GetGame()->m_time = oldTime;
    std::memcpy(g_obPlayers, oldPlayers.data(), sizeof g_obPlayers);
    g_obBlocked = oldBlocked;
    g_coop = oldCoop;
    g_firstPerson = oldFirstPerson;
    g_textIds = oldTextIds;
    g_reliableActorStreams = oldReliable;
    g_maSoundOn = true;
    g_maFramesLeft = -1;
    g_sphereTestHook = nullptr;
    // (The next model frees this one's frames: none left in the primary sector's children, none of
    // their names kept for the addresses.)
    g_sector->m_firstChild = nullptr;
    for (S_StubFrame *stub : g_frames)
        g_itNames.erase(stub->frame);
    GetGame()->m_checkpoints = oldCheckpoints;
    GetGame()->m_scene = oldScenePtr;
    g_sceneVtable[0x7C / 4] = oldFind;
    for (int i = 0; i < 9; ++i)
        g_frameVtable[frameSlots[i] / 4] = oldFrame[i];
    g_soundSlot = 0;
    g_soundSlotStep = 0;
    g_sphereHookSetsHit = false;
    for (int i = 0; i < 2; ++i)
        g_soundVtable[soundSlots[i] / 4] = oldSound[i];
    g_driverVtable[0x54 / 4] = oldCreateFrame;
    reset();
    std::printf("moving: train %d trainProps %d trainStart %d trainPath %d trainMove %d trainWheels %d trainSounds %d "
                "trainSave %d sub %d subProps %d subStart %d subSeats %d subExit %d subMove %d subSounds %d subSave %d zone %d zoneProps %d zoneStart %d zoneCapture %d zoneState %d zoneSpawn %d\n",
                r.train, r.trainProps, r.trainStart, r.trainPath, r.trainMove, r.trainWheels, r.trainSounds,
                r.trainSave, r.sub, r.subProps, r.subStart, r.subSeats, r.subExit, r.subMove, r.subSounds, r.subSave,
                r.zone, r.zoneProps, r.zoneStart, r.zoneCapture, r.zoneState, r.zoneSpawn);
}
