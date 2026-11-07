// Part 7b's test (the actors build: build_item.bat also links src/game/radio.cpp,
// document_place.cpp, mine.cpp, ts_explosion.cpp and spawn_point.cpp; main runs ObjectiveActorsTest
// after SimpleActorsTest on every model): the radio, the document place, the mine, the TS explosion
// and the spawn point against oracles written from the original's code. Engine and game calls are
// logged in order through item_test.inc's log; the harness's records give the activations,
// volumes, sounds, particles, explosions, messages and script uses; saved games and properties are
// compared byte for byte. Uses simple_actors_test.inc's helpers. check_item.py reads the
// "objective:" line.

#include "game/document_place.h"
#include "game/game_app.h"
#include "game/human_controller.h"
#include "game/mine.h"
#include "game/radio.h"
#include "game/spawn_point.h"
#include "game/ts_explosion.h"

extern "C" __declspec(dllimport) unsigned long __stdcall timeGetTime();   // (winmm; the soldiers' activity)

// --- the game's side the actors call ---------------------------------------------------------------

std::map<int, C_Actor *> g_obHumans;                     // GetHuman / GetActorById by id
C_Actor *C_ActorsModule::GetHuman(int id) const
{
    ItLog("gethuman %d", id);
    auto it = g_obHumans.find(id);
    return it == g_obHumans.end() ? nullptr : it->second;
}
int g_obFreeSlot = 3;
int C_ActorsModule::FindFreePlayerSlot() const
{
    ItLog("freeslot");
    return g_obFreeSlot;
}
std::vector<C_Actor *> g_obSpawnPoints;                  // FindRandomSpawnPoint's answers in turn
size_t g_obSpawnPick;
C_Actor *C_ActorsModule::FindRandomSpawnPoint(int team, int kind) const
{
    ItLog("randompoint %d %d", team, kind);
    return g_obSpawnPoints[g_obSpawnPick++ % g_obSpawnPoints.size()];
}
// The humans made (under their slot's id, as the real module); their controller's type, the local
// player's flag, the controller, already on the player's team.
std::vector<C_Human *> g_obMadeHumans;
bool g_obNoHuman;
int g_obControllerType = HUMAN_CONTROLLER_ENEMY;
bool g_obLocalHuman, g_obMakeMember;
std::vector<C_HumanController *> g_obControllers;
C_Actor *C_ActorsModule::CreateActor(E_ActorType type, I3D_frame *frame, bool active, int slot)
{
    ItLog("createactor %d %s %d %d", type, ItName(frame).c_str(), active, slot);
    if (g_obNoHuman)
        return nullptr;
    auto *human = new C_Human();
    human->controllerType = g_obControllerType;
    human->localPlayer = g_obLocalHuman;
    if (g_obControllerType == HUMAN_CONTROLLER_PLAYER)
        g_obControllers.push_back(new C_PlayerController());
    else
        g_obControllers.push_back(new C_RemoteController());
    human->controller = g_obControllers.back();
    if (g_obMakeMember)
        g_playerTeam.insert(human);
    g_obMadeHumans.push_back(human);
    g_actorsById[slot] = human;
    return human;
}
int8_t g_obSlotUse[32];
int C_ActorsModule::GetPlayerSlotUse(int slot) const
{
    return slot >= 0 && slot < 32 ? g_obSlotUse[slot] : -1;
}
void C_ActorsModule::SetPlayerSlotUse(int slot, int use)
{
    ItLog("slotuse %d %d", slot, use);
    if (slot >= 0 && slot < 32)
        g_obSlotUse[slot] = static_cast<int8_t>(use);
}

S_NetPlayerState g_obPlayers[8];
S_NetPlayerState *C_NetGame::LockPlayers()
{
    ItLog("lock");
    return g_obPlayers;
}
void C_NetGame::UnlockPlayers()
{
    ItLog("unlock");
}
void C_NetGame::SetPlayer(int slot, int connection, const char *name)
{
    ItLog("setplayer %d %d %s", slot, connection, name);
}
void C_NetGame::UpdateVoiceTeam()
{
    ItLog("voiceteam");
}
// A soldier state in the fake streams: its profile index and uniform (bit count -12).
void NetWriteSoldierState(C_NetMessage *msg, S_SoldierState &state, bool unknown)
{
    ItLog("writestate %d", unknown);
    auto *s = reinterpret_cast<S_FakeStream *>(msg);
    s->bits.emplace_back(static_cast<uint32_t>(state.profileIndex), -12);
    s->bits.emplace_back(static_cast<uint32_t>(state.uniform.typeId), -12);
}
void NetReadSoldierState(C_NetMessage *, S_SoldierState &state, bool unknown)
{
    ItLog("readstate %d", unknown);
    for (int i = 0; i < 2; ++i) {
        if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != -12) {
            g_readMismatch = true;
            return;
        }
        const auto value = static_cast<int>(g_readBits[g_readPos++].first);
        if (i == 0)
            state.profileIndex = value;
        else
            state.uniform.typeId = static_cast<int16_t>(value);
    }
}
// Quantised positions: their three floats (bit count -11, the sender's quantisation and mask tested).
int g_obSender;
S_NetPlayer *C_NetMessage::GetSender(int playerIndex) const
{
    return playerIndex == -1 ? reinterpret_cast<S_NetPlayer *>(&g_obSender) : nullptr;
}
bool C_NetMessage::WriteQuantized(const float *values, const S_NetQuantization *q, uint8_t mask)
{
    const auto *player = reinterpret_cast<const S_NetPlayer *>(q);
    auto *s = reinterpret_cast<S_FakeStream *>(this);
    for (int i = 0; i < 3; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &values[i], 4);
        s->bits.emplace_back(bits, player == reinterpret_cast<S_NetPlayer *>(&g_obSender) && mask == 7 ? -11 : -13);
    }
    return false;
}
void C_NetMessage::ReadQuantized(float *values, const S_NetQuantization *q, uint32_t mask)
{
    const auto *player = reinterpret_cast<const S_NetPlayer *>(q);
    for (int i = 0; i < 3; ++i) {
        if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != -11 ||
            player != reinterpret_cast<S_NetPlayer *>(&g_obSender) || mask != 7) {
            g_readMismatch = true;
            return;
        }
        std::memcpy(&values[i], &g_readBits[g_readPos++].first, 4);
    }
}

bool g_obZoneOk = true;
S_vector4 g_obZonePos, g_obZoneDir;
const void *g_obZone;                                   // the zone the spawns should ask
// (The real zone is linked since part 7d: the spawn point's calls reach this fake through ld's
// --wrap, build_item.bat; the zone's test calls the real one as __real_.)
bool __attribute__((thiscall)) ObPickSpawnPosition(C_MPZone *self, S_vector4 *pos, S_vector4 *dir, bool cycle)
    __asm__("___wrap__ZN8C_MPZone17PickSpawnPositionEP9S_vector4S1_b");
bool __attribute__((thiscall)) ObPickSpawnPosition(C_MPZone *self, S_vector4 *pos, S_vector4 *dir, bool cycle)
{
    ItLog("pickspawn %d %s", cycle, self == g_obZone ? "zone" : "?");
    if (!g_obZoneOk)
        return false;
    *pos = g_obZonePos;
    *dir = g_obZoneDir;
    return true;
}
int g_obBlocked;                                         // IsSpawnBlocked's trues before a false
bool IsSpawnBlocked(const S_vector4 &pos)
{
    ItLog("blocked %s", ItV(S_vector(pos.x, pos.y, pos.z)).c_str());
    return g_obBlocked-- > 0;
}
void FaceTargetZone(S_vector4 *dir, const S_vector4 &pos, int team)
{
    ItLog("facezone %s %d", ItV(S_vector(pos.x, pos.y, pos.z)).c_str(), team);
    dir->x = -dir->x;
}
alignas(16) uint8_t g_obGameApp[sizeof(C_GameApp)];
C_GameApp *GetGameApp()
{
    return reinterpret_cast<C_GameApp *>(g_obGameApp);
}
void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    ItLog("gamemsg %d %d %d %d %d", type, to, static_cast<int>(a), b, reliable);
}
void C_game::SetCameraMode(int mode)
{
    ItLog("cameramode %d", mode);
}
void C_game::OnPlayerSpawned(int slot, C_Human *human)
{
    ItLog("playerspawned %d %d", slot, human != nullptr);
}
alignas(16) uint8_t g_obCameraModule[0x400];
C_CameraModule *GetCameraModule()
{
    return reinterpret_cast<C_CameraModule *>(g_obCameraModule);
}
void C_CameraModule::SetSpecialMode(int mode, bool on, I3D_frame *target)
{
    ItLog("specialmode %d %d %d", mode, on, target != nullptr);
}
void C_Team::SelectMember(C_Human *human)
{
    ItLog("selectmember %d", human != nullptr);
}
bool __attribute__((thiscall)) F_ObAddMember(void *self, C_Human *)
{
    ItLog("addmember %s", self == g_teamManager.GetPlayerTeam() ? "player" : self == g_teamManager.GetCoopTeam() ? "coop" : "?");
    return true;
}
void *g_obTeamVtable[32];
void S_SquadSoldier::PutItem(S_SoldierItem *displaced, uint8_t slot, const S_SoldierItem &item)
{
    ItLog("putitem %d %d %d %d", slot, item.uid, item.typeId, item.present);
    *displaced = uniform;
    if (slot == SQUAD_SLOT_UNIFORM)
        uniform = item;
}
void S_SquadSoldier::SplitStacks()
{
    ItLog("splitstacks");
}
S_DetailSettings g_obDetails;
S_DetailSettings *C_GameProfile::GetDetailSettings()
{
    return &g_obDetails;
}
std::vector<S_Explosion> g_obExplosions;
void S_Explosion::Execute()
{
    ItLog("explosion");
    g_obExplosions.push_back(*this);
}
std::vector<bool> g_obReactivations;
void C_Actor::Reactivate(bool server)
{
    g_obReactivations.push_back(server);
}
// The mine items' component (the test's own; never saved here).
void C_MineComp::Save(int *ioSize, void *)
{
    *ioSize = 0;
}
void C_MineComp::Load(int, const void *) {}
// The item definitions by type (GetItemDef; out of 0..499 none, as the real one).
std::map<int, C_ItemDef *> g_obDefs;
C_ItemDef *GetItemDef(int typeId)
{
    auto it = g_obDefs.find(typeId);
    return typeId < 0 || typeId >= 500 || it == g_obDefs.end() ? nullptr : it->second;
}

// --- the engine slots the actors use ----------------------------------------------------------------

// The driver's frames (the spawned soldiers' models; "new" until named).
std::vector<I3D_frame *> g_obMadeFrames;
bool g_obNoFrame;
int g_obFramesLeft = -1;                                // (>= 0: that many made, then none)
I3D_frame *__stdcall F_ObCreateFrame(void *, int type)
{
    ItLog("createframe %d", type);
    if (g_obNoFrame || g_obFramesLeft == 0)
        return nullptr;
    if (g_obFramesLeft > 0)
        --g_obFramesLeft;
    I3D_frame *f = MakeFrame(g_frameVtable, static_cast<E_I3D_FRAME_TYPE>(type), false);
    g_itNames[f] = "new";
    g_obMadeFrames.push_back(f);
    return f;
}

void __stdcall F_ObSetPos(I3D_frame *self, const S_vector &pos)
{
    ItLog("setpos %s %s", ItName(self).c_str(), ItV(pos).c_str());
    self->m_localMatrix(3) = pos;
    self->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
}
void __stdcall F_ObSetDir(I3D_frame *self, const S_vector &dir, float roll)
{
    ItLog("setdir %s %s %.9g", ItName(self).c_str(), ItV(dir).c_str(), roll);
}
void __stdcall F_ObSetWorldDir(I3D_frame *self, const S_vector &dir, float roll)
{
    ItLog("worlddir %s %s %.9g", ItName(self).c_str(), ItV(dir).c_str(), roll);
}
void __stdcall F_ObDuplicate(I3D_frame *self, I3D_frame *source)
{
    ItLog("duplicate %s %s", ItName(self).c_str(), ItName(source).c_str());
}
void __stdcall F_ObAddFrame(void *, I3D_frame *frame)
{
    ItLog("addframe %s", ItName(frame).c_str());
}
int __stdcall F_ObRemoveFrame(void *, I3D_frame *frame)
{
    ItLog("removeframe %s", ItName(frame).c_str());
    return 1;
}
void __stdcall F_ObProjTexture(I3D_frame *self, void *texture)
{
    ItLog("texture %s %d", ItName(self).c_str(), texture != nullptr);
}
void __stdcall F_ObProjA4(I3D_frame *self, int value)
{
    ItLog("proja4 %s %d", ItName(self).c_str(), value);
}
void __stdcall F_ObProjB4(I3D_frame *self, int value)
{
    ItLog("projb4 %s %d", ItName(self).c_str(), value);
}
void __stdcall F_ObProjMode(I3D_frame *self, int mode)
{
    ItLog("projmode %s %d", ItName(self).c_str(), mode);
}
HRESULT __stdcall F_ObLoadTextureFail(void *, const char *name, const char *, uint32_t mode, S_Image **out)
{
    ItLog("loadtexture %s %x", name, mode);
    *out = nullptr;
    return -1;
}
HRESULT __stdcall F_ObLoadTexture(void *self, const char *name, const char *alpha, uint32_t mode, S_Image **out)
{
    ItLog("loadtexture %s %x %d", name, mode, alpha != nullptr);
    return F_LoadTexture(self, name, alpha, mode, out);
}

// --- the test ----------------------------------------------------------------------------------------------

struct S_Ob {
    bool radio = true, radioUse = true, radioSave = true, doc = true, docState = true, docNet = true,
         docSave = true, mine = true, mineArm = true, mineStep = true, mineSave = true, ts = true,
         tsStep = true, tsSave = true, spawn = true, spawnNet = true, spawnSetup = true, spawnProps = true;
};
#define OB(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

// A C_Item of `typeId` with `uid` (the stub human's FindItem returns it).
C_Item *ObItem(int typeId, int uid)
{
    alignas(16) static uint8_t pool[64][sizeof(C_Item)];
    static int next;
    auto *item = reinterpret_cast<C_Item *>(pool[next++ % 64]);
    std::memset(static_cast<void *>(item), 0, sizeof(C_Item));
    item->SetUid(uid);
    (void)typeId;
    return item;
}

void ObjectiveActorsTest()
{
    const int frameSlots[] = {0x00, 0x08, 0x0C, 0x1C, 0x24, 0x30, 0x34, 0x38, 0x3C, 0x4C};
    void *oldFrame[10];
    for (int i = 0; i < 10; ++i)
        oldFrame[i] = g_frameVtable[frameSlots[i] / 4];
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_frameVtable[0x0C / 4] = reinterpret_cast<void *>(&F_ObSetDir);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_ObSetWorldDir);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x34 / 4] = reinterpret_cast<void *>(&F_AnSetName);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x4C / 4] = reinterpret_cast<void *>(&F_ObDuplicate);
    const int projSlots[] = {0x00, 0x08, 0x0C, 0x24, 0x30, 0x38, 0x9C, 0xA4, 0xB4, 0xB8};
    void *oldProj[10];
    for (int i = 0; i < 10; ++i)
        oldProj[i] = g_projectorVtable[projSlots[i] / 4];
    g_projectorVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_projectorVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_projectorVtable[0x0C / 4] = reinterpret_cast<void *>(&F_ObSetDir);
    g_projectorVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_projectorVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_projectorVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_projectorVtable[0x9C / 4] = reinterpret_cast<void *>(&F_ObProjTexture);
    g_projectorVtable[0xA4 / 4] = reinterpret_cast<void *>(&F_ObProjA4);
    g_projectorVtable[0xB4 / 4] = reinterpret_cast<void *>(&F_ObProjB4);
    g_projectorVtable[0xB8 / 4] = reinterpret_cast<void *>(&F_ObProjMode);
    const int sceneSlots[] = {0x80, 0x84, 0xE0, 0xEC};
    void *oldScene[4];
    for (int i = 0; i < 4; ++i)
        oldScene[i] = g_sceneVtable[sceneSlots[i] / 4];
    g_sceneVtable[0x80 / 4] = reinterpret_cast<void *>(&F_ObAddFrame);
    g_sceneVtable[0x84 / 4] = reinterpret_cast<void *>(&F_ObRemoveFrame);
    g_sceneVtable[0xE0 / 4] = reinterpret_cast<void *>(&F_ItGetSector);
    g_sceneVtable[0xEC / 4] = reinterpret_cast<void *>(&F_ItUpdateFrameSector);
    void *const oldLoadTexture = g_igraphVtable[0x3C / 4];
    g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_ObLoadTexture);
    I3D_scene *const oldScenePtr = GetGame()->m_scene;
    GetGame()->m_scene = g_carScene;
    const uint8_t oldRole = g_gameRole;
    g_textIds = true;
    g_reliableActorStreams = true;
    S_Ob r;

    std::vector<I3D_bbox> shapes;
    for (S_StubFrame *s : g_frames) {
        const I3D_bbox b = RbBox(s->frame);
        if (shapes.size() < 3 && b.min.x <= b.max.x && b.max.x - b.min.x > 0.05f && b.max.y - b.min.y > 0.05f &&
            b.max.z - b.min.z > 0.05f)
            shapes.push_back(b);
    }
    while (shapes.size() < 3) {
        I3D_bbox b;
        b.min = S_vector(-0.3f, -0.2f, -0.9f - shapes.size());
        b.max = S_vector(0.7f, 0.8f, 1.1f + shapes.size());
        shapes.push_back(b);
    }
    int seed = static_cast<int>(g_frames.size()) * 29 + 11;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    int frameNo = 0;
    const auto placed = [&](E_I3D_FRAME_TYPE type, const std::string &name, bool parented) {
        I3D_frame *parent = nullptr;
        if (parented) {
            parent = AnFrame(FRAME_DUMMY, name + ".p" + std::to_string(frameNo), nullptr);
            parent->m_localMatrix.m[3][0] = rnd(-30, 30);
            parent->m_localMatrix.m[3][2] = rnd(-30, 30);
            parent->m_localMatrix.m[1][1] = rnd(0.5f, 2.0f);
        }
        I3D_frame *f = AnFrame(type, name + std::to_string(frameNo++), parent);
        RbTilt(f, rnd(-1, 1), rnd(-1, 1), rnd(0, 1) > 0.5f);
        f->m_localMatrix.m[3][0] = rnd(-10, 10);
        f->m_localMatrix.m[3][1] = rnd(-2, 4);
        f->m_localMatrix.m[3][2] = rnd(-10, 10);
        f->m_scale = S_vector(rnd(0.5f, 2), rnd(0.5f, 2), rnd(0.5f, 2));
        return f;
    };
    const auto boxFrame = [&](I3D_frame *parent, const char *name, int k) {
        const I3D_bbox &b = shapes[static_cast<size_t>(k) % shapes.size()];
        I3D_frame *f = RbBoxFrame(parent, name, b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z,
                                  S_vector(rnd(-1, 1), rnd(0, 1), rnd(-1, 1)));
        f->m_name = Stub(f).name.c_str();
        return f;
    };
    S_FrameData scriptData = {};
    scriptData.script = reinterpret_cast<C_Script *>(&scriptData);
    const auto reset = [&] {
        g_itLog.clear();
        g_activations.clear();
        g_deactivations.clear();
        g_volumeLog.clear();
        g_soundRequests.clear();
        g_uses.clear();
        g_itDestroyed.clear();
        ClearStreams();
        g_itTemps.clear();
        g_explosions.clear();
        g_explosionsShown.clear();
        g_rbSplashes.clear();
        g_obExplosions.clear();
        g_obReactivations.clear();
        g_actorLoads.clear();
    };
    const auto lastStream = [&]() -> S_FakeStream * { return g_streams.empty() ? nullptr : g_streams.back(); };
    const auto newStream = [&] {
        g_streams.push_back(new S_FakeStream{0, false, {}, false});
        return reinterpret_cast<C_BitStream *>(g_streams.back());
    };
    using T_Bits = std::vector<std::pair<uint32_t, int>>;
    const auto sameSound = [](const S_SoundRequest &q, const S_vector &pos, float posW, int chapter, int group) {
        return RbSame(q.pos, pos) && q.posW == posW && q.flags == SOUND_SILENT_TO_AI && q.attachTo == nullptr &&
               q.source == nullptr && q.chapter == chapter && q.group == group && q.fileName == nullptr &&
               q.channel == SOUND_CHANNEL_NONE && q.volume == 1.0f && q.nearScale == 1.0f && q.farScale == 1.0f;
    };

#include "objective_radio_doc.inc"
#include "objective_mine_ts.inc"
#include "objective_spawn.inc"

    g_gameRole = oldRole;
    g_textIds = false;
    g_reliableActorStreams = false;
    GetGame()->m_scene = oldScenePtr;
    for (int i = 0; i < 10; ++i)
        g_frameVtable[frameSlots[i] / 4] = oldFrame[i];
    for (int i = 0; i < 10; ++i)
        g_projectorVtable[projSlots[i] / 4] = oldProj[i];
    for (int i = 0; i < 4; ++i)
        g_sceneVtable[sceneSlots[i] / 4] = oldScene[i];
    g_igraphVtable[0x3C / 4] = oldLoadTexture;
    reset();
    std::printf("objective: radio %d radioUse %d radioSave %d doc %d docState %d docNet %d docSave %d mine %d mineArm %d "
                "mineStep %d mineSave %d ts %d tsStep %d tsSave %d spawn %d spawnNet %d spawnSetup %d spawnProps %d\n",
                r.radio, r.radioUse, r.radioSave, r.doc, r.docState, r.docNet, r.docSave, r.mine, r.mineArm, r.mineStep,
                r.mineSave, r.ts, r.tsStep, r.tsSave, r.spawn, r.spawnNet, r.spawnSetup, r.spawnProps);
}
