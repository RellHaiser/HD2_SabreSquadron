// Part 20 of the human harness (HUMAN_SELF builds, build_human.bat; included by human_test.cpp after base_test.inc): the
// human itself (game/human.cpp) against the original's, on the base harness's human (both layouts). The modules
// human.cpp talks to are fakes: their calls logged (the original's redirected to the same loggers), their answers
// scripted.
#include "game/first_person_view.h"
#include "game/camera_module.h"
#include "game/profile.h"
#include "game/team.h"
#include "game/interface.h"
#include <chrono>

// ---- the port's side: the modules (raw storage under their names: their constructors and vtables not linked)
asm(".globl _g_sightModule\n\t.bss\n\t.balign 16\n_g_sightModule:\n\t.space 1024\n\t.text");
asm(".globl _g_itemsModule\n\t.bss\n\t.balign 16\n_g_itemsModule:\n\t.space 4096\n\t.text");
asm(".globl _g_texts\n\t.bss\n\t.balign 16\n_g_texts:\n\t.space 256\n\t.text");
asm(".globl _g_netGame\n\t.bss\n\t.balign 16\n_g_netGame:\n\t.space 8192\n\t.text");
asm(".globl _g_gameProfile\n\t.bss\n\t.balign 16\n_g_gameProfile:\n\t.space 4096\n\t.text");
int g_localPlayerSlot = 0;
alignas(16) uint8_t g_netBytes[0x40], g_netObjectBytes[0x40], g_portTrack[0x100], g_origProperty[0x200];
C_NetReplication *g_netReplication = reinterpret_cast<C_NetReplication *>(g_netBytes);
namespace {
C_ActorCreator *g_creatorTable[64];
}  // namespace
C_ActorCreator **g_actorCreators = g_creatorTable;

namespace {
// What the fakes answer.
struct S_SelfScript {
    int difficulty = 2;                                 // C_GameProfile::GetDifficulty
    bool coop = false;                                  // IsCooperativeGame
    uint32_t moveMode = 0;                              // C_Human::GetMoveMode
    uint32_t stateFlags = 0;                            // C_Human::GetStateFlags
    bool unconscious = false;
    int sightId = 7;                                    // AddObserver / AddTarget
    int listener = 3;                                   // AddListener
    int periodicLink = 11;                              // PeriodicLinks_Add
    S_SoldierProfile *profile = nullptr;                // GetSoldierProfile
    uint32_t bodyFlags = 0;                             // S_HumanBody::TakeFlags
    bool member = false;                                // the player team's IsMember
    void *speechSet = nullptr;                          // C_SpeechSets::Acquire
    bool uniform = false;                               // the uniform container's GetItem
    bool items = false;                                 // CreateItem makes one
    bool actorSetFrame = true;                          // C_Actor::SetFrame
    bool (*memberOf)(const void *human) = nullptr;      // the player team's IsMember by object (part 21; else `member`)
} g_self;
// The debug console's lines (the original's Print is empty: both sides log the text).
void LogPrint(const char *format, va_list args)
{
    char text[256];
    std::vsnprintf(text, sizeof text, format, args);
    Log(nullptr, (std::string("Print ") + text).c_str());
}
}  // namespace

int C_GameProfile::GetDifficulty() const
{
    Log(nullptr, "GetDifficulty");
    return g_self.difficulty;
}
bool C_GameProfile::IsCooperativeGame() const
{
    Log(nullptr, "IsCooperativeGame");
    return g_self.coop;
}
C_str C_GameTexts::Get(int id) const
{
    Log(nullptr, "Text", {double(id)});
    return C_str(("text" + std::to_string(id)).c_str());
}
void C_game::Print(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogPrint(format, args);
    va_end(args);
}
void C_game::ShowFirstPersonView(bool show) { Log(nullptr, "ShowFirstPersonView", {double(show)}); }
C_CameraModule *C_game::GetCameraModule() { return nullptr; }
alignas(16) uint8_t g_portCamModule[0x100];
C_CameraModule *GetCameraModule() { return reinterpret_cast<C_CameraModule *>(g_portCamModule); }
C_GameApp *GetGameApp() { return nullptr; }
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    Log(nullptr, "PostCommand", {double(command), double(a), double(b), double(flush)});
}
extern "C" void dbgPrintf2(const char *format, ...) {}

// The sight and sound modules.
int C_SightModule::AddObserver(I3D_frame *frame, uint32_t mask)
{
    Log(nullptr, ("AddObserver " + NameOf(frame)).c_str(), {double(mask)});
    return g_self.sightId;
}
int C_SightModule::AddTarget(I3D_frame *frame, uint32_t mask)
{
    Log(nullptr, ("AddTarget " + NameOf(frame)).c_str(), {double(mask)});
    return g_self.sightId + 1;
}
bool C_SightModule::RemoveObserver(int id)
{
    Log(nullptr, "RemoveObserver", {double(id)});
    return true;
}
bool C_SightModule::RemoveTarget(int id)
{
    Log(nullptr, "RemoveTarget", {double(id)});
    return true;
}
void C_SightModule::SetObserverState(int id, int state) { Log(nullptr, "SetObserverState", {double(id), double(state)}); }
void C_SightModule::SetTargetState(int id, int state) { Log(nullptr, "SetTargetState", {double(id), double(state)}); }
int C_SoundModule::AddListener(I3D_frame *frame, T_SoundHeardProc)
{
    Log(this, ("AddListener " + NameOf(frame)).c_str());
    return g_self.listener;
}
bool C_SoundModule::RemoveListener(int index)
{
    Log(this, "RemoveListener", {double(index)});
    return true;
}
int PeriodicLinks_Add(C_Actor *actor, int arg)
{
    Log(actor, "PeriodicLinks_Add", {double(arg)});
    return g_self.periodicLink;
}
void PeriodicLinks_Remove(int id) { Log(nullptr, "PeriodicLinks_Remove", {double(id)}); }

// The items.
alignas(16) uint8_t g_itemBytes[8][0x80];   // (part 20e)
int g_itemNext = 0;
void *NextItem()
{
    if (!g_self.items)
        return nullptr;
    return g_itemBytes[g_itemNext++ % 8];
}
C_Item *C_ItemsModule::CreateItem(int typeId, int container, C_Actor *owner, int uid)
{
    Log(owner, "CreateItem", {double(typeId), double(container), double(uid)});
    return static_cast<C_Item *>(NextItem());
}
void ClearActorContainers(C_Actor *actor) { Log(actor, "ClearActorContainers"); }
S_SoldierProfile *GetSoldierProfile(int index)
{
    Log(nullptr, "GetSoldierProfile", {double(index)});
    return g_self.profile;
}
C_SpeechSet *C_SpeechSets::Acquire(int id)
{
    Log(nullptr, "SpeechSetAcquire", {double(id)});
    return static_cast<C_SpeechSet *>(g_self.speechSet);
}
void C_SpeechSets::Release(C_SpeechSet *) { Log(nullptr, "SpeechSetRelease"); }

// The network game.
S_NetPlayerState *C_NetGame::LockPlayers() { return nullptr; }
void C_NetGame::UnlockPlayers() {}
void C_NetGame::SetPlayerTeam(int slot, int team, bool pending)
{
    Log(nullptr, "SetPlayerTeam", {double(slot), double(team), double(pending)});
}
C_NetObject *C_NetReplication::FindObject(int id, bool create)
{
    Log(nullptr, "FindNetObject", {double(id), double(create)});
    return reinterpret_cast<C_NetObject *>(g_netObjectBytes);
}
C_NetProperty *C_NetObject::GetProperty(int index)
{
    Log(this, "GetNetProperty", {double(index)});
    return reinterpret_cast<C_NetProperty *>(g_origProperty);
}
C_NetPositionTrack &C_NetProperty::GetTrack() { return *reinterpret_cast<C_NetPositionTrack *>(g_portTrack); }

// The interface, the team, the first person view, the surface marks, mines, orders and sounds.
void C_Interface::ShowTimer(int textId, int ms) { Log(nullptr, "ShowTimer", {double(textId), double(ms)}); }
void C_Interface::ShowSquadMember(C_Human *human) { Log(human, "ShowSquadMember"); }
void C_Interface::SetScreen(int screen) { Log(nullptr, "SetScreen", {double(screen)}); }
bool C_TeamBase::IsMember(const C_Human *human) const
{
    Log(human, "IsTeamMember");
    return g_self.memberOf ? g_self.memberOf(human) : g_self.member;
}
void C_Team::UpdateCameraTarget() { Log(nullptr, "UpdateCameraTarget"); }
void C_Team::SetWatchedHuman(C_Human *human) { Log(human, "SetWatchedHuman"); }
void C_Team::SelectMember(C_Human *human) { Log(human, "SelectMember"); }
void C_FirstPersonView::Refresh() { Log(nullptr, "FirstPersonRefresh"); }
void SurfaceMarks_SetHumanVisible(C_Human *human, bool hide) { Log(human, "SurfaceMarks_SetHumanVisible", {double(hide)}); }
void C_Mine::OnSteppedOn(C_Actor *actor, int unused) { Log(actor, "MineSteppedOn", {double(unused)}); }
void ReleaseHumanOrder(S_HumanOrder *) { Log(nullptr, "ReleaseHumanOrder"); }
void FreeRememberedSound(S_RememberedSound *) { Log(nullptr, "FreeRememberedSound"); }

// The human's parts in other files: the events, the AI memory, the body.
void C_HumanEvents::Reset() { Log(this, "EventsReset"); }
void C_HumanEvents::Tick(int timeDelta) { Log(this, "EventsTick", {double(timeDelta)}); }
bool C_HumanEvents::SaveGame(C_chunk &)
{
    Log(this, "EventsSave");
    return true;
}
bool C_HumanEvents::LoadGame(C_chunk &)
{
    Log(this, "EventsLoad");
    return true;
}
bool C_HumanAIMemory::SaveGame(C_chunk &)
{
    Log(this, "AIMemorySave");
    return true;
}
bool C_HumanAIMemory::LoadGame(C_chunk &)
{
    Log(nullptr, "AIMemoryLoad");
    return true;
}
bool S_HumanBody::Create(C_Human *owner, int group)
{
    Log(owner, "BodyCreate", {double(group)});
    return true;
}
bool S_HumanBody::Destroy()
{
    Log(nullptr, "BodyDestroy");
    return true;
}
void S_HumanBody::Reset() { Log(nullptr, "BodyReset"); }
bool S_HumanBody::SaveGame(C_chunk &)
{
    Log(nullptr, "BodySave");
    return true;
}
bool S_HumanBody::LoadGame(C_chunk &)
{
    Log(nullptr, "BodyLoad");
    return true;
}
void S_HumanBody::Update(int frameTime) { Log(nullptr, "BodyUpdate", {double(frameTime)}); }
void S_HumanBody::Tick(int timeDelta) { Log(nullptr, "BodyTick", {double(timeDelta)}); }
void S_HumanBody::SetFacingDir(const S_vector &dir) { Log(nullptr, "BodySetFacingDir", {dir.x, dir.y, dir.z}); }
void S_HumanBody::SetLookAt(const S_vector4 &t) { Log(nullptr, "BodySetLookAt", {t.x, t.y, t.z, t.w}); }
void S_HumanBody::SetViewDir(const S_vector &dir) { Log(nullptr, "BodySetViewDir", {dir.x, dir.y, dir.z}); }
uint32_t S_HumanBody::TakeFlags()
{
    Log(nullptr, "BodyTakeFlags");
    return g_self.bodyFlags;
}
void S_HumanBody::SetModelVisible(bool on) { Log(nullptr, "BodySetModelVisible", {double(on)}); }

// The human's members in other files.
// The port's controller object (parts 20b..: the fakes that make a controller install it).
alignas(16) uint8_t g_portController[0x400];
void C_Human::CreateController(E_HumanController type)
{
    Log(this, "CreateController", {double(type)});
    m_controller = reinterpret_cast<C_HumanController *>(g_portController);
}
const char *C_Human::GetShortName() const { return "shortName"; }
const char *C_Human::GetFullName() const { return "fullName"; }
void C_Human::OnHandsChanged() { Log(this, "OnHandsChanged"); }
void __stdcall C_Human::OnSoundHeard(const S_HeardSound *) {}
void C_Human::AssignPlayerController(bool active)
{
    Log(this, "AssignPlayerController", {double(active)});
    m_controller = reinterpret_cast<C_HumanController *>(g_portController);
}
void C_Human::SetLeader(C_Human *leader) { Log(this, "SetLeader", {double(leader != nullptr)}); }
bool C_Human::PutOnUniform(int uid)
{
    Log(this, "PutOnUniform", {double(uid)});
    return true;
}
uint32_t C_Human::GetStateFlags() const { return g_self.stateFlags; }
uint32_t C_Human::GetMoveMode() const { return g_self.moveMode; }
void C_Human::DisbandFormation() { Log(this, "DisbandFormation"); }
#ifndef HUMAN_COMBAT   // (the combat build links game/human_combat.cpp's)
void C_Human::UpdateVelocity(int timeDelta) { Log(this, "UpdateVelocity", {double(timeDelta)}); }
void C_Human::UpdateDisguise(float amount) { Log(this, "UpdateDisguise", {amount}); }
#endif
void C_Human::Done() { Log(this, "Done"); }
void C_Human::Init() { Log(this, "Init"); }
void C_Human::WriteState(C_BitStream *) { Log(this, "WriteState"); }
void C_Human::ReadState(C_BitStream *) { Log(this, "ReadState"); }
void C_Human::Unknown20(C_BitStream *) { Log(this, "Unknown20"); }
void C_Human::OnMessage(C_BitStream *) { Log(this, "OnMessage"); }
void C_Human::UpdateSeenHumans() { Log(this, "UpdateSeenHumans"); }
#ifndef HUMAN_COMBAT
void C_Human::UpdateBleeding(int frameTime) { Log(this, "UpdateBleeding", {double(frameTime)}); }
void C_Human::TickBleeding(int timeDelta) { Log(this, "TickBleeding", {double(timeDelta)}); }
bool C_Human::OnCollisionEx(C_Actor *, const S_vector *, int, int, int, int, int) { return false; }
void C_Human::Hit(E_HitType, float, const S_vector &, const S_vector &, C_Actor *, int) {}
void C_Human::GetBoxCorners(S_vector4 *) {}
#endif
bool C_Human::IsUnconscious() const { return g_self.unconscious; }
void C_Human::ForgetOldSounds() { Log(this, "ForgetOldSounds"); }
void C_Human::ClearContainers() { Log(this, "ClearContainers"); }
#ifndef HUMAN_COMBAT
void C_Human::ApplyRemoteHit(int type, int zone, int health, int killerId, const S_vector &, const S_vector &)
{
    Log(this, "ApplyRemoteHit", {double(type), double(zone), double(health), double(killerId)});
}
#endif
// The actor's base (game/actor.cpp is not linked; the human's calls of its base members logged).
C_Actor::C_Actor(E_ActorType type) : m_type(type) {}
C_Actor::~C_Actor() {}
void C_Actor::Reactivate(bool server) { Log(this, "Reactivate", {double(server)}); }
void C_Actor::Activate(bool server) { Log(this, "Activate", {double(server)}); }
void C_Actor::Deactivate(bool server) { Log(this, "Deactivate", {double(server)}); }
void C_Actor::CopyFrom(const C_Actor *) { Log(this, "ActorCopyFrom"); }
void C_Actor::Init() { Log(this, "ActorInit"); }
void C_Actor::Done() { Log(this, "ActorDone"); }
bool C_Actor::SetFrame(I3D_frame *frame)
{
    Log(this, ("ActorSetFrame " + NameOf(frame)).c_str());
    return g_self.actorSetFrame;
}
bool C_Actor::SaveGame(C_chunk &)
{
    Log(this, "ActorSave");
    return true;
}
bool C_Actor::LoadGame(C_chunk &)
{
    Log(this, "ActorLoad");
    return true;
}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}

// The camera, the team's enemy records, the formation shapes, the squad's soldiers.
C_CameraHumanEffects *C_CameraModule::GetHumanEffects() { return nullptr; }
void C_CameraModule::SetSpecialMode(int mode, bool on, I3D_frame *target)
{
    Log(nullptr, "SetSpecialMode", {double(mode), double(on), double(target != nullptr)});
}
void C_CameraHumanEffects::Remove(C_Human *human) { Log(human, "CameraEffectsRemove"); }
void C_EnemyRecords::Add(C_Human *enemy) { Log(enemy, "EnemyRecordsAdd"); }
C_FormationShape::C_FormationShape() {}
void C_FormationShape::Tick(int timeDelta) { Log(this, "FormationTick", {double(timeDelta)}); }
bool C_FormationShape::SaveGame(C_chunk &chunk)
{
    Log(this, "FormationSave");
    const int word = 7;
    chunk.Write(&word, 4);
    return true;
}
bool C_FormationShape::LoadGame(C_chunk &)
{
    Log(nullptr, "FormationLoad");
    return true;
}
S_SquadSoldier *C_GameProfile::FindSoldierState(int profileIndex)
{
    Log(nullptr, "FindSoldierState", {double(profileIndex)});
    return nullptr;
}

namespace {
#define ST __attribute__((thiscall))
// ---- the original's side
void O_Print(void *, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogPrint(format, args);
    va_end(args);
}
const char *ST O_GetShortName(void *) { return "shortName"; }
const char *ST O_GetFullName(void *) { return "fullName"; }
uint32_t ST O_GetMoveMode(void *) { return g_self.moveMode; }
uint32_t ST O_GetStateFlags(void *) { return g_self.stateFlags; }
int ST O_GetDifficulty(void *)
{
    Log(nullptr, "GetDifficulty");
    return g_self.difficulty;
}
void *OrigProfile(const void *port);
// (The original's is a method of the soldier store, 0x008aea1c.)
void *ST O_GetSoldierProfile(void *, int index)
{
    Log(nullptr, "GetSoldierProfile", {double(index)});
    return OrigProfile(g_self.profile);
}
// C_GameTexts::Get returns a C_str: built by the original's own assignment (the port's C_str has its layout).
typedef void *(ST *T_StrAssign)(void *, const char *);
void *ST O_Text(void *, void *out, int id)
{
    Log(nullptr, "Text", {double(id)});
    *static_cast<void **>(out) = nullptr;
    OriginalAt<T_StrAssign>(0x00405180)(out, ("text" + std::to_string(id)).c_str());
    return out;
}
// The graphic effects' face texture (slot 0xAC; the port's fake object on both sides).
bool ST GE_SwitchFaceTexture(void *self, I3D_frame *model, const char *texture)
{
    Log(self, ("SwitchFaceTexture " + NameOf(model) + " " + texture).c_str());
    return true;
}
// (Each side its own object: g++'s slot of it is not the original's 0xAC.)
void *g_selfGeVt[64], *g_origGeVt[64];
alignas(16) uint8_t g_origGe[64];
void *O_SelfGEffects() { return g_origGe; }

// A C_str's text in the original's layout (its rep: the count, the length, the characters).
std::string OrigStr(const uint8_t *at)
{
    const uint8_t *rep = *reinterpret_cast<uint8_t *const *>(at);
    return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string();
}
// A C_str the human holds, never freed (each side's assignment releases the one it replaces).
void KeepStr(C_str &field, const char *text)
{
    static std::vector<C_str *> kept;
    kept.push_back(new C_str(text));
    for (int i = 0; i < 1000; ++i)
        kept.back()->AddRef();
    new (&field) C_str(*kept.back());
}

// Two soldier profiles, each side its own (the original's std::vector is 16 bytes, g++'s 12: its stats are at +0x48);
// the human's profile pointer is translated per side.
alignas(16) uint8_t g_profileBytes[2][sizeof(S_SoldierProfile)], g_origProfileBytes[2][0x80];
S_SoldierProfile *Profile(int i) { return reinterpret_cast<S_SoldierProfile *>(g_profileBytes[i]); }
void *OrigProfile(const void *port)
{
    for (int i = 0; i < 2; ++i)
        if (port == Profile(i))
            return g_origProfileBytes[i];
    return const_cast<void *>(port);
}
alignas(16) uint8_t g_origController[0x400];   // (part 20b; the port's is defined with the fakes above)
void SelfSide(bool original)
{
    void *&controller = *reinterpret_cast<void **>(o_human + 0xB0);
    if (original && controller == g_portController)
        controller = g_origController;
    else if (!original && controller == g_origController)
        controller = g_portController;
    void *&profile = *reinterpret_cast<void **>(o_human + 0xB8);
    for (int i = 0; i < 2; ++i) {
        if (original && profile == Profile(i))
            profile = g_origProfileBytes[i];
        else if (!original && profile == g_origProfileBytes[i])
            profile = Profile(i);
    }
}

void SetUpSelf()
{
    for (int i = 0; i < 64; ++i)
        g_selfGeVt[i] = g_origGeVt[i] = nullptr;
    g_selfGeVt[VSlot(&C_GEffectsModule::SwitchFaceTexture)] = reinterpret_cast<void *>(GE_SwitchFaceTexture);
    g_origGeVt[0xAC / 4] = reinterpret_cast<void *>(GE_SwitchFaceTexture);
    *reinterpret_cast<void ***>(g_fakeGEffects) = g_selfGeVt;
    *reinterpret_cast<void ***>(g_origGe) = g_origGeVt;
    g_names[g_fakeGEffects] = g_names[g_origGe] = "geffects";
    RedirectOriginal(0x00486510, reinterpret_cast<const void *>(O_SelfGEffects));
    RedirectOriginal(0x0069c2b0, reinterpret_cast<const void *>(O_Print));
    RedirectOriginal(0x005a82c0, reinterpret_cast<const void *>(O_GetShortName));
    RedirectOriginal(0x005a8320, reinterpret_cast<const void *>(O_GetFullName));
    RedirectOriginal(0x005a5c70, reinterpret_cast<const void *>(O_GetMoveMode));
    RedirectOriginal(0x005a5c80, reinterpret_cast<const void *>(O_GetStateFlags));
    RedirectOriginal(0x006b6a20, reinterpret_cast<const void *>(O_GetDifficulty));
    RedirectOriginal(0x006a1f50, reinterpret_cast<const void *>(O_GetSoldierProfile));
    RedirectOriginal(0x006e5380, reinterpret_cast<const void *>(O_Text));
    // Two soldier profiles (only their face textures and stats are read here).
    for (int i = 0; i < 2; ++i) {
        std::memset(g_profileBytes[i], 0, sizeof g_profileBytes[i]);
        KeepStr(Profile(i)->faceTexture, i ? "face_b" : "face_a");
        std::memset(g_origProfileBytes[i], 0, sizeof g_origProfileBytes[i]);
        std::memcpy(g_origProfileBytes[i] + 0x1C, &Profile(i)->faceTexture, 4);
        g_names[Profile(i)] = g_names[g_origProfileBytes[i]] = i ? "profile1" : "profile0";
    }
    g_sideHook = SelfSide;
}

const uint32_t ROLES[] = {GAME_ROLE_LOCAL, GAME_ROLE_LOCAL, GAME_ROLE_CLIENT, GAME_ROLE_SERVER};

// The human's skill state: the role, its progress, stats, timers, names.
void RandomSelf()
{
    C_Human *h = g_h.human;
    g_gameRole = ROLES[I(0, 3)];
    S_SoldierProgress &pr = h->m_progress;
    for (float *f : {&pr.unknown00, &pr.unknown04, &pr.endurance, &pr.morale, &pr.shooting, &pr.stealth, &pr.firstAid, &pr.lockpick})
        *f = P(0.2f) ? U(0, 40) : U(0, 6);
    pr.experience = I(0, 5000);
    for (int i = 0; i < 12; ++i) {
        h->m_stats[i] = I(0, 120);
        h->m_initialStats[i] = P(0.2f) ? I(-5, 300) : I(0, 120);
    }
    h->m_enduranceTime = P(0.3f) ? I(24900, 25100) : I(0, 30000);
    h->m_stealthTime = P(0.3f) ? I(19900, 20100) : I(0, 25000);
    h->m_role = static_cast<E_HumanRole>(I(0, 6));
    h->m_nameTextId = P(0.4f) ? -1 : I(0, 9000);
    h->m_profileIndex = I(-1, 9);
    h->m_profile = P(0.3f) ? nullptr : Profile(I(0, 1));
    for (int i = 0; i < 2; ++i)
        for (float &f : Profile(i)->stats)
            f = P(0.2f) ? U(-2, 300) : float(I(0, 100)) + (P(0.5f) ? 0.0f : U(0, 1));
    for (int i = 0; i < 2; ++i)
        std::memcpy(g_origProfileBytes[i] + 0x48, Profile(i)->stats, sizeof Profile(i)->stats);
    KeepStr(h->m_faceTexture, "face_old");
    g_self.profile = P(0.3f) ? nullptr : Profile(I(0, 1));
    g_self.difficulty = I(0, 5);
    g_self.moveMode = P(0.2f) ? 0u : uint32_t(I(0, 0xFF));
    g_self.stateFlags = uint32_t(I(0, 3)) * 0x1000u | (P(0.5f) ? uint32_t(STATE_MARCHING) : 0u);
}

typedef void(ST *T_SFloat)(void *, float);
typedef void(ST *T_SInt)(void *, int);
typedef void(ST *T_SVoid)(void *);
typedef void *(ST *T_SName)(void *);
typedef void(ST *T_SText)(void *, const char *);

// The progress and skills: the gains (capped, printed), walking and sneaking training them, the difficulty's scaling of
// an enemy's stats.
void TestSelfSkills()
{
    C_Human *h = g_h.human;
    std::printf("self skills\n");
    struct S_Add {
        const char *name;
        uint32_t address;
        void (C_Human::*port)(float);
    };
    static const S_Add adds[] = {{"AddUnknownProgress", 0x005a92f0, &C_Human::AddUnknownProgress},
                                 {"AddEndurance", 0x005a93a0, &C_Human::AddEndurance},
                                 {"AddMorale", 0x005a9410, &C_Human::AddMorale},
                                 {"AddShooting", 0x005a9480, &C_Human::AddShooting},
                                 {"AddStealth", 0x005a94f0, &C_Human::AddStealth},
                                 {"AddFirstAid", 0x005a9560, &C_Human::AddFirstAid},
                                 {"AddLockpick", 0x005a95d0, &C_Human::AddLockpick}};
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomSelf();
        switch (i % 10) {
        case 7: {
            const int amount = I(-100, 500);
            DiffCall("AddExperience", [&] { h->AddExperience(amount); return 0; },
                     [&](void *) { OriginalAt<T_SInt>(0x005a9640)(o_human, amount); return 0; });
            break;
        }
        case 8: {
            const int time = P(0.1f) ? 0 : I(1, 300);
            if (P(0.4f)) {
                // (The timers reaching their thresholds exactly, or one past.)
                h->m_enduranceTime = 25000 + I(0, 1) - time;
                h->m_stealthTime = 20000 + I(0, 1) - time;
            }
            DiffCall("TrainSkills", [&] { h->TrainSkills(time); return 0; },
                     [&](void *) { OriginalAt<T_SInt>(0x005a88f0)(o_human, time); return 0; });
            break;
        }
        case 9:
            DiffCall("ApplyDifficulty", [&] { h->ApplyDifficulty(); return 0; },
                     [&](void *) { OriginalAt<T_SVoid>(0x005a8990)(o_human); return 0; });
            break;
        default: {
            const S_Add &a = adds[i % 10];
            const float amount = P(0.1f) ? 0.0f : U(-1, 3);
            DiffCall(a.name, [&] { (h->*a.port)(amount); return 0; },
                     [&](void *) { OriginalAt<T_SFloat>(a.address)(o_human, amount); return 0; });
            break;
        }
        }
    }
}

// The name by the role (and a text id), the profile's stats and face, the face texture.
void TestSelfNames()
{
    C_Human *h = g_h.human;
    std::printf("self names\n");
    g_skipFields.insert("m_name");
    g_skipFields.insert("m_faceTexture");
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomSelf();
        std::string portText, origText;
        switch (i % 4) {
        case 0:
            DiffCall("GetName",
                     [&] {
                         portText = h->C_Human::GetName()->c_str();
                         return 0;
                     },
                     [&](void *) {
                         const void *name = OriginalAt<T_SName>(0x005a9980)(o_human);
                         origText = OrigStr(static_cast<const uint8_t *>(name));
                         return int(name != o_human + 0x80);
                     });
            break;
        case 1: {
            const int index = I(-2, 9);
            DiffCall("SetProfile",
                     [&] {
                         h->SetProfile(index);
                         portText = h->m_faceTexture.c_str();
                         return 0;
                     },
                     [&](void *) {
                         OriginalAt<T_SInt>(0x0059ea90)(o_human, index);
                         origText = OrigStr(o_human + 0xBC);
                         return 0;
                     });
            break;
        }
        case 2: {
            const std::string name = "face_" + std::to_string(I(0, 99));
            DiffCall("SetTexture",
                     [&] {
                         h->SetTexture(name.c_str());
                         portText = h->m_faceTexture.c_str();
                         return 0;
                     },
                     [&](void *) {
                         OriginalAt<T_SText>(0x005a87c0)(o_human, name.c_str());
                         origText = OrigStr(o_human + 0xBC);
                         return 0;
                     });
            break;
        }
        default:
            DiffCall("ApplyProfileFace",
                     [&] {
                         h->ApplyProfileFace();
                         portText = h->m_faceTexture.c_str();
                         return 0;
                     },
                     [&](void *) {
                         OriginalAt<T_SVoid>(0x005a8790)(o_human);
                         origText = OrigStr(o_human + 0xBC);
                         return 0;
                     });
            break;
        }
        CHECK(portText == origText);
    }
    g_skipFields.erase("m_name");
    g_skipFields.erase("m_faceTexture");
}

// ---- part 20b: the updates (Update every frame, Tick every 100 ms)

// The controller: each side its own object (the original reads the look-at at +0x30, its type at +0x48), its virtuals
// logged (the port's slots by its member pointers, the original's by their offsets).
void *g_portControllerVt[128], *g_origControllerVt[128];
S_vector g_facingDir, g_viewDir;
void ST CT_Update(void *self, int time) { Log(self, "ControllerUpdate", {double(time)}); }
void ST CT_Tick(void *self, int time) { Log(self, "ControllerTick", {double(time)}); }
const S_vector *ST CT_GetFacingDir(void *self)
{
    Log(self, "ControllerGetFacingDir");
    return &g_facingDir;
}
const S_vector *ST CT_GetViewDir(void *self)
{
    Log(self, "ControllerGetViewDir");
    return &g_viewDir;
}
void ST CT_OnBodyFlags(void *self, uint32_t flags) { Log(self, "ControllerOnBodyFlags", {double(flags)}); }
void ST CT_Unexpected(void *self)
{
    std::printf("unexpected controller call on %s\n", NameOf(self).c_str());
    std::exit(3);
}

// The original's calls of the human's other parts, the body, the events, the formation, the team, the application.
void ST OU_OnHandsChanged(void *self) { Log(self, "OnHandsChanged"); }
void ST OU_UpdateBleeding(void *self, int time) { Log(self, "UpdateBleeding", {double(time)}); }
void ST OU_UpdateVelocity(void *self, int time) { Log(self, "UpdateVelocity", {double(time)}); }
void ST OU_TickBleeding(void *self, int time) { Log(self, "TickBleeding", {double(time)}); }
void ST OU_UpdateSeenHumans(void *self) { Log(self, "UpdateSeenHumans"); }
void ST OU_ForgetOldSounds(void *self) { Log(self, "ForgetOldSounds"); }
void ST OU_UpdateDisguise(void *self, float amount) { Log(self, "UpdateDisguise", {amount}); }
void ST OU_EventsTick(void *self, int time) { Log(self, "EventsTick", {double(time)}); }
void ST OU_FormationTick(void *self, int time) { Log(self, "FormationTick", {double(time)}); }
void ST OU_BodySetFacingDir(void *, const S_vector *dir) { Log(nullptr, "BodySetFacingDir", {dir->x, dir->y, dir->z}); }
void ST OU_BodySetLookAt(void *, const S_vector4 *t) { Log(nullptr, "BodySetLookAt", {t->x, t->y, t->z, t->w}); }
void ST OU_BodySetViewDir(void *, const S_vector *dir) { Log(nullptr, "BodySetViewDir", {dir->x, dir->y, dir->z}); }
void ST OU_BodyUpdate(void *, int time) { Log(nullptr, "BodyUpdate", {double(time)}); }
void ST OU_BodyTick(void *, int time) { Log(nullptr, "BodyTick", {double(time)}); }
uint32_t ST OU_BodyTakeFlags(void *)
{
    Log(nullptr, "BodyTakeFlags");
    return g_self.bodyFlags;
}
int ST OU_IsMember(void *, const void *human)
{
    Log(human, "IsTeamMember");
    return g_self.member;
}
int ST OU_IsActive(void *self) { return *reinterpret_cast<const int *>(static_cast<uint8_t *>(self) + 0x14) >= 0; }
alignas(16) uint8_t g_fakeApp[64];
void *OU_GetGameApp() { return g_fakeApp; }
void ST OU_PostCommand(void *, int command, intptr_t a, intptr_t b, int flush)
{
    Log(nullptr, "PostCommand", {double(command), double(a), double(b), double(B(flush))});
}
alignas(16) uint8_t g_eventsBytes[0x200], g_formationBytes[0x100];
// A mine's trigger volume (its actor any object: OnSteppedOn is logged on both sides).
alignas(16) uint8_t g_mineBytes[0x100];
const S_vector MINE_AT(40, 1.2f, 40);
void ST OU_OnSteppedOn(void *, void *actor, int unused) { Log(actor, "MineSteppedOn", {double(unused)}); }

void SetUpUpdate()
{
    for (int i = 0; i < 128; ++i)
        g_portControllerVt[i] = g_origControllerVt[i] = reinterpret_cast<void *>(CT_Unexpected);
    g_portControllerVt[VSlot(&C_HumanController::Update)] = reinterpret_cast<void *>(CT_Update);
    g_portControllerVt[VSlot(&C_HumanController::Tick)] = reinterpret_cast<void *>(CT_Tick);
    g_portControllerVt[VSlot(&C_HumanController::GetFacingDir)] = reinterpret_cast<void *>(CT_GetFacingDir);
    g_portControllerVt[VSlot(&C_HumanController::GetViewDir)] = reinterpret_cast<void *>(CT_GetViewDir);
    g_portControllerVt[VSlot(&C_HumanController::OnBodyFlags)] = reinterpret_cast<void *>(CT_OnBodyFlags);
    g_origControllerVt[0x24 / 4] = reinterpret_cast<void *>(CT_Update);
    g_origControllerVt[0x28 / 4] = reinterpret_cast<void *>(CT_Tick);
    g_origControllerVt[0x48 / 4] = reinterpret_cast<void *>(CT_GetFacingDir);
    g_origControllerVt[0x4C / 4] = reinterpret_cast<void *>(CT_GetViewDir);
    g_origControllerVt[0x80 / 4] = reinterpret_cast<void *>(CT_OnBodyFlags);
    *reinterpret_cast<void ***>(g_portController) = g_portControllerVt;
    *reinterpret_cast<void ***>(g_origController) = g_origControllerVt;
    g_names[g_portController] = g_names[g_origController] = "controller";
    g_names[g_eventsBytes] = "events";
    g_names[g_formationBytes] = "formation";
    o_humanVt[0x64 / 4] = reinterpret_cast<void *>(OU_IsActive);
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005a6a90, reinterpret_cast<const void *>(OU_OnHandsChanged)},
        {0x005a0480, reinterpret_cast<const void *>(OU_UpdateBleeding)},
        {0x005a0660, reinterpret_cast<const void *>(OU_UpdateVelocity)},
        {0x005a0420, reinterpret_cast<const void *>(OU_TickBleeding)},
        {0x005a50a0, reinterpret_cast<const void *>(OU_UpdateSeenHumans)},
        {0x005a5380, reinterpret_cast<const void *>(OU_ForgetOldSounds)},
        {0x005a60d0, reinterpret_cast<const void *>(OU_UpdateDisguise)},
        {0x005ad700, reinterpret_cast<const void *>(OU_EventsTick)},
        {0x0059ba70, reinterpret_cast<const void *>(OU_FormationTick)},
        {0x005ac0a0, reinterpret_cast<const void *>(OU_BodySetFacingDir)},
        {0x005ac080, reinterpret_cast<const void *>(OU_BodySetLookAt)},
        {0x005ac0c0, reinterpret_cast<const void *>(OU_BodySetViewDir)},
        {0x005abc20, reinterpret_cast<const void *>(OU_BodyUpdate)},
        {0x005abc70, reinterpret_cast<const void *>(OU_BodyTick)},
        {0x005ac0d0, reinterpret_cast<const void *>(OU_BodyTakeFlags)},
        {0x00461a20, reinterpret_cast<const void *>(OU_IsMember)},
        {0x006dcd10, reinterpret_cast<const void *>(OU_GetGameApp)},
        {0x0072b590, reinterpret_cast<const void *>(OU_PostCommand)},
        {0x005fefb0, reinterpret_cast<const void *>(OU_OnSteppedOn)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    AddContact("mine", -2, MINE_AT, S_vector(0.2f, 0.2f, 0.2f));
    g_contacts.back()->volume.type = COLLISION_TYPE_MINE;
    g_contacts.back()->volume.actor = reinterpret_cast<C_Actor *>(g_mineBytes);
    g_names[g_mineBytes] = "mine";
}

// The state the updates read: the timers, the role and the network game's respawn, the controller's directions, the
// body's flags, a team member or not, the velocity (a mine's test), a formation.
void RandomUpdate()
{
    RandomSelf();
    C_Human *h = g_h.human;
    h->m_controller = reinterpret_cast<C_HumanController *>(g_portController);
    h->m_events = reinterpret_cast<C_HumanEvents *>(g_eventsBytes);
    h->m_formation = P(0.3f) ? reinterpret_cast<C_FormationShape *>(g_formationBytes) : nullptr;
    h->m_activeIndex = P(0.85f) ? I(0, 20) : -1;
    h->m_actionTime = P(0.3f) ? 0 : I(-10, 400);
    h->m_chatTime = P(0.3f) ? 0 : I(-10, 400);
    std::snprintf(h->m_chatText, sizeof h->m_chatText, "chat %d", I(0, 99));
    h->m_aliveTime = I(0, 100000);
    h->m_deadTime = P(0.2f) ? I(-5000, 0) : I(0, 40000);
    h->m_stats[STAT_HEALTH] = P(0.4f) ? I(-20, 0) : I(1, 100);
    h->m_velocity = {U(-3, 3), U(-1, 1), U(-3, 3), 0.0f};
    if (P(0.2f))
        h->m_velocity = {0.0f, 0.0f, 0.0f, 0.0f};
    S_MultiplayerSettings *mp = g_mpSettings;
    mp->gameType = P(0.6f) ? MP_GAME_DEATHMATCH : I(0, 4);
    mp->respawnTime = I(0, 20);
    mp->allowRespawn = P(0.7f) ? 1 : 0;
    // (The original's slot is set by ToOriginal from g_script.localPlayer: the human's net id or 99.)
    g_script.localPlayer = P(0.6f);
    g_localPlayerSlot = g_script.localPlayer ? h->m_netId : 99;
    const auto dir = [] { return S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(); };
    g_facingDir = dir();
    g_viewDir = dir();
    const S_vector4 look = {U(-50, 50), U(-5, 5), U(-50, 50), 1.0f};
    std::memcpy(g_origController + 0x30, &look, 16);
    reinterpret_cast<C_HumanController *>(g_portController)->m_lookAt = look;
    g_origController[0x48] = g_controller[0x48];
    g_self.bodyFlags = P(0.3f) ? 0u : uint32_t(I(0, 0x7FFFFFFF));
    g_self.member = P(0.5f);
    g_self.stateFlags = P(0.5f) ? uint32_t(STATE_TREADING) & (P(0.5f) ? 0x2000u : 0x10000u) : g_self.stateFlags;
    // Standing on the mine sometimes (or near it), elsewhere otherwise.
    const S_vector at = P(0.4f) ? MINE_AT + S_vector(U(-0.6f, 0.6f), U(-0.4f, 0.4f), U(-0.6f, 0.6f)) : S_vector(U(4, 30), 1.0f, U(4, 30));
    g_h.frame->SetPosInline(at);
}

typedef void(ST *T_UInt)(void *, int);

void TestSelfUpdate()
{
    C_Human *h = g_h.human;
    std::printf("self update\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomUpdate();
        const int time = P(0.1f) ? 0 : I(1, 150);
        if (i % 8 == 7) {
            // Asking for a respawn: dead long enough, the movement not blocking it (dying, or its other flag).
            h->m_deadTime = P(0.2f) ? 3000 + I(0, 1) : I(0, 6000);
            g_h.movement->m_unknown1AC = P(0.7f) ? 0 : I(1, 3);
            g_h.movement->m_unknown9C0 = P(0.7f) ? 0 : 1;
            DiffCall("RequestRespawn", [&] { h->RequestRespawn(); return 0; },
                     [&](void *) { OriginalAt<T_SVoid>(0x0059f010)(o_human); return 0; });
        } else if (i % 2 == 0)
            DiffCall("Update", [&] { h->C_Human::Update(time); return 0; },
                     [&](void *) { OriginalAt<T_UInt>(0x0059fe30)(o_human, time); return 0; });
        else
            DiffCall("Tick", [&] { h->C_Human::Tick(time); return 0; },
                     [&](void *) { OriginalAt<T_UInt>(0x0059ffd0)(o_human, time); return 0; });
    }
}

// ---- part 20c: the mission's start and stop

void ST OM_Activate(void *self, int server) { Log(self, "Activate", {double(B(server))}); }
void ST OM_Deactivate(void *self, int server) { Log(self, "Deactivate", {double(B(server))}); }
void ST OM_ShowTimer(void *, int textId, int ms) { Log(nullptr, "ShowTimer", {double(textId), double(ms)}); }
int ST OM_BodyCreate(void *, void *owner, int group)
{
    Log(owner, "BodyCreate", {double(group)});
    return 1;
}
int ST OM_BodyDestroy(void *)
{
    Log(nullptr, "BodyDestroy");
    return 1;
}
int ST OM_IsCoop(void *)
{
    Log(nullptr, "IsCooperativeGame");
    return g_self.coop;
}
void ST OM_CreateController(void *self, int type) { Log(self, "CreateController", {double(type)}); }
void ST OM_AssignPlayerController(void *self, int active) { Log(self, "AssignPlayerController", {double(B(active))}); }
void *ST OM_SpeechAcquire(void *, int id)
{
    Log(nullptr, "SpeechSetAcquire", {double(id)});
    return g_self.speechSet;
}
void ST OM_SpeechRelease(void *, void *) { Log(nullptr, "SpeechSetRelease"); }
int ST OM_AddObserver(void *, I3D_frame *frame, uint32_t mask)
{
    Log(nullptr, ("AddObserver " + NameOf(frame)).c_str(), {double(mask)});
    return g_self.sightId;
}
int ST OM_AddTarget(void *, I3D_frame *frame, uint32_t mask)
{
    Log(nullptr, ("AddTarget " + NameOf(frame)).c_str(), {double(mask)});
    return g_self.sightId + 1;
}
void ST OM_SetTargetState(void *, int id, int state) { Log(nullptr, "SetTargetState", {double(id), double(state)}); }
int ST OM_RemoveObserver(void *, int id)
{
    Log(nullptr, "RemoveObserver", {double(id)});
    return 1;
}
int ST OM_RemoveTarget(void *, int id)
{
    Log(nullptr, "RemoveTarget", {double(id)});
    return 1;
}
int __stdcall OM_PeriodicAdd(void *actor, int arg)   // (stdcall, as the original's)
{
    Log(actor, "PeriodicLinks_Add", {double(arg)});
    return g_self.periodicLink;
}
void __stdcall OM_PeriodicRemove(int id) { Log(nullptr, "PeriodicLinks_Remove", {double(id)}); }
void ST OM_ApplyRemoteHit(void *self, int type, int zone, int health, int killer, const S_vector *, const S_vector *)
{
    Log(self, "ApplyRemoteHit", {double(type), double(zone), double(health), double(killer)});
}
int ST OM_AddListener(void *self, I3D_frame *frame, void *)
{
    Log(self, ("AddListener " + NameOf(frame)).c_str());
    return g_self.listener;
}
int ST OM_RemoveListener(void *self, int index)
{
    Log(self, "RemoveListener", {double(index)});
    return 1;
}
// The camera module's human effects (the module's slot 0x88).
alignas(16) uint8_t g_camModule[64], g_camEffects[64];
void *g_camModuleVt[64];
void *ST OM_GetHumanEffects(void *) { return g_camEffects; }
void *ST OM_GetCameraModule(void *) { return g_camModule; }
void ST OM_EffectsRemove(void *, void *human) { Log(human, "CameraEffectsRemove"); }
void ST OM_ClearContainers(void *self) { Log(self, "ClearContainers"); }
void ST OM_DisbandFormation(void *self) { Log(self, "DisbandFormation"); }
void ST OM_SetLeader(void *self, void *leader) { Log(self, "SetLeader", {double(leader != nullptr)}); }
void ST OM_EventsReset(void *self) { Log(self, "EventsReset"); }
// The controller's Detach and deleting destructor (the port's two destructor slots, the original's 0x18).
void ST CT_Detach(void *self) { Log(self, "ControllerDetach"); }
void ST CT_DeletePort(void *self) { Log(self, "ControllerDelete"); }
void ST CT_DeleteOrig(void *self, int) { Log(self, "ControllerDelete"); }

alignas(16) uint8_t g_aiMemoryBytes[0x120], g_speechSetBytes[16];
// The player team (raw storage; the original's GetPlayerTeam answers the same object): one vtable for both sides, its
// members' slots logged (AddMember: the original's 0x08; RemoveMember(C_Human *): its 0x24, g++'s next to it).
void *g_teamVt[32];
int ST TM_AddMember(void *, void *human)
{
    Log(human, "TeamAddMember");
    return 1;
}
int ST TM_RemoveMember(void *, void *human)
{
    Log(human, "TeamRemoveMember");
    return 1;
}
// The scene's sector at a place (an LS3D method: stdcall, this on the stack; slot 0xE0 on both sides).
I3D_sector *__stdcall SC_GetSector(void *, const S_vector *pos)
{
    Log(nullptr, "SceneGetSector", {pos->x, pos->y, pos->z});
    return reinterpret_cast<I3D_sector *>(g_sector);
}
// A vehicle to leave the seat of (one vtable for both sides: LeaveSeat is the original's slot 0x80).
alignas(16) uint8_t g_vehicleBytes[0x100];
void *g_vehicleVt[64];
void *ST VH_LeaveSeat(void *self, int seat)
{
    Log(self, "LeaveSeat", {double(seat)});
    return nullptr;
}
// Dummies of the model: the body's frame ("z", sometimes not under the model) and the head ("f"); a parent that is
// not a sector.
I3D_frame *g_zFrame, *g_fFrame, *g_notSector;
S_FrameData g_humanData = {};

void SetUpMission()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00610240, reinterpret_cast<const void *>(OM_Activate)},
        {0x00610510, reinterpret_cast<const void *>(OM_Deactivate)},
        {0x0049de50, reinterpret_cast<const void *>(OM_ShowTimer)},
        {0x005ab330, reinterpret_cast<const void *>(OM_BodyCreate)},
        {0x005aba90, reinterpret_cast<const void *>(OM_BodyDestroy)},
        {0x006b69d0, reinterpret_cast<const void *>(OM_IsCoop)},
        {0x005a3f20, reinterpret_cast<const void *>(OM_CreateController)},
        {0x005a3e60, reinterpret_cast<const void *>(OM_AssignPlayerController)},
        {0x005afe00, reinterpret_cast<const void *>(OM_SpeechAcquire)},
        {0x005b0390, reinterpret_cast<const void *>(OM_SpeechRelease)},
        {0x0046f350, reinterpret_cast<const void *>(OM_AddObserver)},
        {0x0046f9d0, reinterpret_cast<const void *>(OM_AddTarget)},
        {0x0046f700, reinterpret_cast<const void *>(OM_SetTargetState)},
        {0x0046fd50, reinterpret_cast<const void *>(OM_RemoveObserver)},
        {0x0046fdf0, reinterpret_cast<const void *>(OM_RemoveTarget)},
        {0x006e0080, reinterpret_cast<const void *>(OM_PeriodicAdd)},
        {0x006e0220, reinterpret_cast<const void *>(OM_PeriodicRemove)},
        {0x005a15d0, reinterpret_cast<const void *>(OM_ApplyRemoteHit)},
        {0x0069a620, reinterpret_cast<const void *>(OM_GetCameraModule)},
        {0x00463040, reinterpret_cast<const void *>(OM_EffectsRemove)},
        {0x005a29c0, reinterpret_cast<const void *>(OM_ClearContainers)},
        {0x005a9120, reinterpret_cast<const void *>(OM_DisbandFormation)},
        {0x005a91a0, reinterpret_cast<const void *>(OM_SetLeader)},
        {0x005ae450, reinterpret_cast<const void *>(OM_EventsReset)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_origSoundVt[0x44 / 4] = reinterpret_cast<void *>(OM_AddListener);
    g_origSoundVt[0x40 / 4] = reinterpret_cast<void *>(OM_RemoveListener);
    for (void *&slot : g_camModuleVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_camModuleVt[0x88 / 4] = reinterpret_cast<void *>(OM_GetHumanEffects);
    *reinterpret_cast<void ***>(g_camModule) = g_camModuleVt;
    g_names[g_camEffects] = "cameraEffects";
    g_portControllerVt[VSlot(&C_HumanController::Detach)] = reinterpret_cast<void *>(CT_Detach);
    g_portControllerVt[6] = g_portControllerVt[7] = reinterpret_cast<void *>(CT_DeletePort);
    g_origControllerVt[0x20 / 4] = reinterpret_cast<void *>(CT_Detach);
    g_origControllerVt[0x18 / 4] = reinterpret_cast<void *>(CT_DeleteOrig);
    // The speech sets made already (the original's g_speechSets; the port's makes its own on its first call).
    *OriginalAt<void **>(0x008ac448) = g_speechSetBytes;
    g_self.speechSet = g_speechSetBytes;
    g_names[g_speechSetBytes] = "speechSet";
    g_names[g_aiMemoryBytes] = "aiMemory";
    g_h.frame->SetData(&g_humanData);
    for (void *&slot : g_teamVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_teamVt[0x08 / 4] = reinterpret_cast<void *>(TM_AddMember);
    g_teamVt[0x24 / 4] = reinterpret_cast<void *>(TM_RemoveMember);
    g_teamVt[VSlot(&C_Team::AddMember)] = reinterpret_cast<void *>(TM_AddMember);
    g_teamVt[VSlot(static_cast<bool (C_Team::*)(C_Human *)>(&C_Team::RemoveMember))] = reinterpret_cast<void *>(TM_RemoveMember);
    *reinterpret_cast<void ***>(g_teamManager.GetPlayerTeam()) = g_teamVt;
    g_sceneVt[0xE0 / 4] = reinterpret_cast<void *>(SC_GetSector);
    for (void *&slot : g_vehicleVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_vehicleVt[0x80 / 4] = reinterpret_cast<void *>(VH_LeaveSeat);
    g_vehicleVt[VSlot(&C_Actor::LeaveSeat)] = reinterpret_cast<void *>(VH_LeaveSeat);
    *reinterpret_cast<void ***>(g_vehicleBytes) = g_vehicleVt;
    g_names[g_vehicleBytes] = "vehicle";
    S_quat none;
    none.Identity();
    g_zFrame = NewFrame(FRAME_DUMMY, "z", S_vector(0, 1.0f, 0), none, Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_h.frame);
    g_fFrame = NewFrame(FRAME_DUMMY, "f", S_vector(0, 1.6f, 0), none, Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_h.frame);
    g_notSector = NewFrame(FRAME_DUMMY, "notSector", S_vector(20, 1, 20), none, Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_sector);
}

// A human to start or stop: initialised or not, its role and side, the network game's spawn protection, the ids it
// holds (or none), a script, a profile, a speech set.
void RandomMission()
{
    RandomUpdate();
    C_Human *h = g_h.human;
    h->m_initialized = P(0.5f);
    h->m_aiMemory = reinterpret_cast<C_HumanAIMemory *>(g_aiMemoryBytes);
    for (size_t k = 0; k < 0x110; ++k)
        g_aiMemoryBytes[k] = uint8_t(I(0, 255));
    h->m_stats[STAT_SPEECH_SET] = I(1, 6);
    h->m_initialStats[STAT_SPEECH_SET] = I(1, 6);
    h->m_speechSet = P(0.5f) ? reinterpret_cast<C_SpeechSet *>(g_speechSetBytes) : nullptr;
    h->m_sightId = P(0.4f) ? -1 : I(0, 20);
    h->m_sightTargetId = P(0.4f) ? -1 : I(0, 20);
    h->m_soundListener = P(0.4f) ? -1 : I(0, 20);
    h->m_periodicLink = P(0.4f) ? -1 : I(0, 20);
    h->m_disguise = U(0, 1);
    h->m_unknown94 = U(0, 1);
    h->m_actionText = I(0, 100);
    h->m_actionTotal = I(0, 1000);
    h->m_mpZone = I(0, 5);
    h->m_inPlay = P(0.5f);
    h->m_unknown200 = I(0, 9);
    h->m_scale = {U(0.5f, 2), U(0.5f, 2), U(0.5f, 2), 1.0f};
    g_mpSettings->spawnProtection = P(0.5f) ? 0 : I(1, 10);
    // The fields a start sets, other values first.
    h->m_bleeding = U(0, 2);
    h->m_bloodDropTime = I(0, 900);
    h->m_unknown1ac = I(0, 9);
    h->m_carried = P(0.5f) ? reinterpret_cast<C_Actor *>(g_vehicleBytes) : nullptr;
    h->m_legHits = I(0, 4);
    h->m_inventoryChanged = P(0.5f);
    h->m_bodyFrame = P(0.5f) ? g_sector : nullptr;
    // A seat, the body's dummy or only the head's, the model outside a sector, a speech set missing.
    h->m_vehicle = P(0.3f) ? reinterpret_cast<C_Actor *>(g_vehicleBytes) : nullptr;
    h->m_seat = I(-1, 5);
    g_zFrame->LinkTo(P(0.5f) ? g_h.frame : g_sector, 0);
    g_h.frame->LinkTo(P(0.2f) ? g_notSector : g_sector, 0);
    if (P(0.05f))
        h->m_stats[STAT_SPEECH_SET] = h->m_initialStats[STAT_SPEECH_SET] = 0;
    g_self.speechSet = P(0.05f) ? nullptr : g_speechSetBytes;
    g_humanData.script = P(0.3f) ? reinterpret_cast<decltype(g_humanData.script)>(g_eventsBytes) : nullptr;
    g_self.coop = P(0.5f);
    for (int i = 0; i < 2; ++i) {
        const bool dead = P(0.5f);
        Profile(i)->dead = dead;
        g_origProfileBytes[i][0x70] = dead;
    }
}

// The AI memory's bytes the original's reset writes (inline at the end of StartMission, 0x0059f9a0). The port's
// Reset also clears the fields the original leaves as they were (its comment says so): those are not compared. (The
// owner, +0xE8, is each side's own human.)
const std::vector<std::pair<int, int>> AI_RESET_BYTES = {
    {0x00, 12}, {0x10, 1}, {0x20, 12}, {0x40, 12}, {0x70, 4}, {0x80, 4}, {0xE0, 2}, {0xEC, 8},
    {0x100, 4}, {0x104, 3}, {0x109, 2}};

void TestSelfMission()
{
    C_Human *h = g_h.human;
    std::printf("self mission\n");
    g_skipFields.insert("m_faceTexture");
    // (The scale's w: the original copies an undefined one from a temp; the port keeps its own.)
    g_skipWords.insert({"m_scale", 12});
    for (int i = 0; i < 16000 / QUICK; ++i) {
        RandomMission();
        const bool server = P(0.5f);
        if (i % 2 == 0) {
            std::vector<uint8_t> portMemory, origMemory;
            std::string portFace, origFace;
            DiffCall(server ? "StartMission server" : "StartMission",
                     [&] {
                         h->C_Human::StartMission(server);
                         portMemory.assign(g_aiMemoryBytes, g_aiMemoryBytes + 0x110);
                         portFace = h->m_faceTexture.c_str();
                         return 0;
                     },
                     [&](void *) {
                         OriginalAt<T_UInt>(0x0059f2c0)(o_human, server);
                         origMemory.assign(g_aiMemoryBytes, g_aiMemoryBytes + 0x110);
                         origFace = OrigStr(o_human + 0xBC);
                         return 0;
                     });
            CHECK(portFace == origFace);
            const bool reset = !portMemory.empty() && portMemory[0x104] == 1;
            void *portOwner, *origOwner;
            std::memcpy(&portOwner, &portMemory[0xE8], 4);
            std::memcpy(&origOwner, &origMemory[0xE8], 4);
            CHECK(!reset || (portOwner == h && origOwner == o_human));
            for (const auto &r : AI_RESET_BYTES)
                if (std::memcmp(&portMemory[r.first], &origMemory[r.first], r.second) != 0) {
                    std::printf("  aiMemory +0x%x differs:", r.first);
                    for (int k = 0; k < r.second; ++k)
                        std::printf(" %02x/%02x", portMemory[r.first + k], origMemory[r.first + k]);
                    std::printf("\n");
                    CHECK(false);
                }
        } else
            DiffCall(server ? "StopMission server" : "StopMission",
                     [&] { h->C_Human::StopMission(server); return 0; },
                     [&](void *) { OriginalAt<T_UInt>(0x0059fa60)(o_human, server); return 0; });
        // (The profile's death flag: each side its own profile.)
        for (int k = 0; k < 2; ++k)
            CHECK(Profile(k)->dead == (g_origProfileBytes[k][0x70] != 0));
    }
    g_skipFields.erase("m_faceTexture");
    g_skipWords.erase({"m_scale", 12});
}

// ---- part 20d: the restarts (single player, co-op, multiplayer)

void ST OR_Reactivate(void *self, int server) { Log(self, "Reactivate", {double(B(server))}); }
void ST OR_ShowFirstPersonView(void *, int show) { Log(nullptr, "ShowFirstPersonView", {double(B(show))}); }
void ST OR_FirstPersonRefresh(void *) { Log(nullptr, "FirstPersonRefresh"); }
// (fastcall in the original: the human in ECX, the flag in DL.)
void __attribute__((fastcall)) OR_SetHumanVisible(void *human, int hide)
{
    Log(human, "SurfaceMarks_SetHumanVisible", {double(B(hide))});
}
void ST OR_BodyReset(void *) { Log(nullptr, "BodyReset"); }
void ST OR_BodySetModelVisible(void *, int on) { Log(nullptr, "BodySetModelVisible", {double(B(on))}); }
void *ST OR_FindSoldierState(void *, int profileIndex)
{
    Log(nullptr, "FindSoldierState", {double(profileIndex)});
    return nullptr;
}
int ST OR_PutOnUniform(void *self, int uid)
{
    Log(self, "PutOnUniform", {double(uid)});
    return 1;
}
void ST GE_FadeHuman(void *self, void *human, int time, int in)
{
    Log(self, "FadeHuman", {double(human != nullptr), double(time), double(B(in))});
}
// The controller a restart makes (each side its own object, of the scripted type) and its SetSpectating.
void ST OR_CreateController(void *self, int type)
{
    Log(self, "CreateController", {double(type)});
    *reinterpret_cast<void **>(static_cast<uint8_t *>(self) + 0xB0) = g_origController;
}
void ST OR_AssignPlayerController(void *self, int active)
{
    Log(self, "AssignPlayerController", {double(B(active))});
    *reinterpret_cast<void **>(static_cast<uint8_t *>(self) + 0xB0) = g_origController;
}
void ST CT_SetSpectating(void *self, int on) { Log(self, "ControllerSetSpectating", {double(B(on))}); }
// The uniform's container and item (their slot 0x40 GetItem: one vtable for both sides; the item's uid at +0x10).
alignas(16) uint8_t g_uniformContainer[0x80], g_uniformItem[0x80];
void *g_containerVt[64];
C_ItemContainer *g_selfContainers[16];
void *ST IC_GetItem(void *self)
{
    Log(self, "ContainerGetItem");
    return g_self.uniform ? g_uniformItem : nullptr;
}

void SetUpRestart()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00610710, reinterpret_cast<const void *>(OR_Reactivate)},
        {0x0069a770, reinterpret_cast<const void *>(OR_ShowFirstPersonView)},
        {0x004922a0, reinterpret_cast<const void *>(OR_FirstPersonRefresh)},
        {0x0078b5e0, reinterpret_cast<const void *>(OR_SetHumanVisible)},
        {0x005ab4c0, reinterpret_cast<const void *>(OR_BodyReset)},
        {0x005ac0f0, reinterpret_cast<const void *>(OR_BodySetModelVisible)},
        {0x006b76b0, reinterpret_cast<const void *>(OR_FindSoldierState)},
        {0x005a6930, reinterpret_cast<const void *>(OR_PutOnUniform)},
        {0x005a3f20, reinterpret_cast<const void *>(OR_CreateController)},
        {0x005a3e60, reinterpret_cast<const void *>(OR_AssignPlayerController)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_origGeVt[0x74 / 4] = reinterpret_cast<void *>(GE_FadeHuman);
    g_selfGeVt[VSlot(&C_GEffectsModule::FadeHuman)] = reinterpret_cast<void *>(GE_FadeHuman);
    g_portControllerVt[VSlot(&C_HumanController::SetSpectating)] = reinterpret_cast<void *>(CT_SetSpectating);
    g_origControllerVt[0x60 / 4] = reinterpret_cast<void *>(CT_SetSpectating);
    for (void *&slot : g_containerVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_containerVt[0x40 / 4] = reinterpret_cast<void *>(IC_GetItem);
    g_containerVt[VSlot(&C_ItemContainer::GetItem)] = reinterpret_cast<void *>(IC_GetItem);
    *reinterpret_cast<void ***>(g_uniformContainer) = g_containerVt;
    g_selfContainers[ITEM_CONTAINER_UNIFORM] = reinterpret_cast<C_ItemContainer *>(g_uniformContainer);
    g_names[g_uniformContainer] = "uniformContainer";
}

// A human to restart: its controller of a scripted type, faded out or not, a uniform or none, a script or none.
void RandomRestart()
{
    RandomMission();
    C_Human *h = g_h.human;
    h->m_fadedOut = P(0.3f);
    h->m_containers = g_selfContainers;
    g_self.uniform = P(0.5f);
    const int uid = I(1, 900);
    reinterpret_cast<C_Item *>(g_uniformItem)->SetUid(uid);
    std::memcpy(g_uniformItem + 0x10, &uid, 4);
    // The type of the controller the restart makes (and of the one it had).
    static const E_HumanController types[] = {HUMAN_CONTROLLER_PLAYER, HUMAN_CONTROLLER_REMOTE, HUMAN_CONTROLLER_ENEMY,
                                              HUMAN_CONTROLLER_SQUAD};
    const E_HumanController type = types[I(0, 3)];
    reinterpret_cast<C_HumanController *>(g_portController)->m_type = type;
    std::memcpy(g_origController + 0x48, &type, 4);
    g_firstPerson = P(0.5f);
    // A player slot or the first id past them; the model switched off sometimes.
    if (P(0.3f))
        h->m_netId = C_ActorsModule::PLAYER_SLOTS - 1 + I(0, 1);
    g_localPlayerSlot = g_script.localPlayer ? h->m_netId : 99;   // (as ToOriginal sets the original's)
    g_h.frame->SetOn(P(0.7f));
}

void TestSelfRestart()
{
    C_Human *h = g_h.human;
    std::printf("self restart\n");
    g_skipFields.insert("m_faceTexture");
    g_skipWords.insert({"m_scale", 12});
    for (int i = 0; i < 16000 / QUICK; ++i) {
        RandomRestart();
        const bool server = P(0.5f);
        std::string portFace, origFace;
        bool portOn = false, origOn = false;
        const auto faces = [&](bool original) {
            (original ? origFace : portFace) = original ? OrigStr(o_human + 0xBC) : std::string(h->m_faceTexture.c_str());
            (original ? origOn : portOn) = g_h.frame->IsOn();
        };
        switch (i % 4) {
        case 0:
            DiffCall(server ? "RestartSinglePlayer server" : "RestartSinglePlayer",
                     [&] { h->RestartSinglePlayer(server); faces(false); return 0; },
                     [&](void *) { OriginalAt<T_UInt>(0x0059c4d0)(o_human, server); faces(true); return 0; });
            break;
        case 1:
            DiffCall("RestartCoop", [&] { h->RestartCoop(); faces(false); return 0; },
                     [&](void *) { OriginalAt<T_SVoid>(0x0059ca00)(o_human); faces(true); return 0; });
            break;
        case 2:
            DiffCall("RestartMultiplayer", [&] { h->RestartMultiplayer(); faces(false); return 0; },
                     [&](void *) { OriginalAt<T_SVoid>(0x0059cec0)(o_human); faces(true); return 0; });
            break;
        default:
            DiffCall(server ? "RestartMission server" : "RestartMission",
                     [&] { h->C_Human::RestartMission(server); faces(false); return 0; },
                     [&](void *) { OriginalAt<T_UInt>(0x0059d100)(o_human, server); faces(true); return 0; });
            break;
        }
        CHECK(portFace == origFace);
        CHECK(portOn == origOn);
    }
    g_skipFields.erase("m_faceTexture");
    g_skipWords.erase({"m_scale", 12});
}

// ---- part 20e: the soldier's setup and equipment

// The items the items module makes (scripted: an item or none; their uids at +0x10 and at the port's offset).
void *ST OE_CreateItem(void *, int typeId, int container, void *owner, int uid)
{
    Log(owner, "CreateItem", {double(typeId), double(container), double(uid)});
    return NextItem();
}
// (A method of the items module in the original, 0x0098b020.)
void ST OE_ClearActorContainers(void *, void *actor) { Log(actor, "ClearActorContainers"); }
void ST OE_SetPlayerTeam(void *, int slot, int team, int pending)
{
    Log(nullptr, "SetPlayerTeam", {double(slot), double(team), double(B(pending))});
}

void SetUpEquipment()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x007d7c70, reinterpret_cast<const void *>(OE_CreateItem)},
        {0x007de350, reinterpret_cast<const void *>(OE_ClearActorContainers)},
        {0x00730510, reinterpret_cast<const void *>(OE_SetPlayerTeam)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    for (int k = 0; k < 8; ++k) {
        const int uid = 100 + k;
        reinterpret_cast<C_Item *>(g_itemBytes[k])->SetUid(uid);
        std::memcpy(g_itemBytes[k] + 0x10, &uid, 4);
        g_names[g_itemBytes[k]] = "item" + std::to_string(k);
    }
}

// A soldier's state, and the original's copy of it: the same bytes up to the belt (+0x6C); the two item lists as
// MSVC6 vectors (16 bytes: their first and last at +4 and +8) over copies of the entries; the load at +0x8C.
static_assert(offsetof(S_SoldierState, head) == 0x4C && offsetof(S_SoldierState, belt) == 0x6C, "the original's layout up to the belt");
struct S_OrigState {
    alignas(16) uint8_t bytes[0xAC];
    std::vector<S_SoldierItem> belt, backpack;
};
void ToOrigState(const S_SoldierState &port, S_OrigState &orig)
{
    std::memset(orig.bytes, 0, sizeof orig.bytes);
    std::memcpy(orig.bytes, &port, 0x6C);
    orig.belt = port.belt;
    orig.backpack = port.backpackContents;
    const auto vector = [&](size_t at, std::vector<S_SoldierItem> &v) {
        S_SoldierItem *first = v.data(), *last = v.data() + v.size();
        std::memcpy(orig.bytes + at + 4, &first, 4);
        std::memcpy(orig.bytes + at + 8, &last, 4);
        std::memcpy(orig.bytes + at + 12, &last, 4);
    };
    vector(0x6C, orig.belt);
    vector(0x7C, orig.backpack);
    std::memcpy(orig.bytes + 0x8C, &port.load, sizeof port.load);
}
void RandomItem(S_SoldierItem &e)
{
    e.typeId = int16_t(P(0.3f) ? -1 : I(0, 60));
    e.uid = int16_t(P(0.5f) ? -1 : I(1, 500));
    e.present = int16_t(I(0, 3));
}
void RandomState(S_SoldierState &st)
{
    st.spectating = P(0.5f);
    st.vip = P(0.3f);
    st.profileIndex = I(-1, 9);
    st.team = I(0, 3);
    st.speechSet = I(-1, 6);
    for (S_SoldierItem *e : {&st.head, &st.uniform, &st.backpack, &st.longGun, &st.hands})
        RandomItem(*e);
    st.belt.resize(size_t(I(0, 5)));
    for (S_SoldierItem &e : st.belt)
        RandomItem(e);
    st.backpackContents.resize(size_t(I(0, 4)));
    for (S_SoldierItem &e : st.backpackContents)
        RandomItem(e);
}

typedef void(ST *T_ESetup)(void *, void *, int, int, const int *, int);
typedef void(ST *T_EEquip)(void *, void *);

void TestSelfEquipment()
{
    C_Human *h = g_h.human;
    std::printf("self equipment\n");
    g_skipFields.insert("m_faceTexture");
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomRestart();
        S_SoldierState state;
        RandomState(state);
        S_OrigState orig;
        ToOrigState(state, orig);
        g_self.items = P(0.7f);
        g_itemNext = 0;
        int stats[12];
        for (int &v : stats)
            v = I(0, 120);
        const bool withStats = P(0.4f);
        const int role = I(0, 5), group = I(0, 6), slot = I(0, 31);
        const bool none = P(0.1f);
        std::string portFace, origFace;
        if (i % 3 == 0) {
            DiffCall("CreateEquipment",
                     [&] {
                         g_itemNext = 0;
                         h->CreateEquipment(none ? nullptr : &state);
                         return 0;
                     },
                     [&](void *) {
                         g_itemNext = 0;
                         OriginalAt<T_EEquip>(0x0059eb60)(o_human, none ? nullptr : orig.bytes);
                         return 0;
                     });
        } else {
            // (The VIP's slot: the game's +0x78 on both sides.)
            int portVip = 0, origVip = 0;
            DiffCall("Setup",
                     [&] {
                         g_itemNext = 0;
                         GetGame()->m_vipSlot = -1;
                         h->Setup(state, role, group, withStats ? stats : nullptr, slot);
                         portFace = h->m_faceTexture.c_str();
                         portVip = GetGame()->m_vipSlot;
                         return 0;
                     },
                     [&](void *) {
                         g_itemNext = 0;
                         *reinterpret_cast<int *>(g_origGame + 0x78) = -1;
                         OriginalAt<T_ESetup>(0x0059edc0)(o_human, orig.bytes, role, group, withStats ? stats : nullptr, slot);
                         origFace = OrigStr(o_human + 0xBC);
                         origVip = *reinterpret_cast<int *>(g_origGame + 0x78);
                         return 0;
                     });
            CHECK(portFace == origFace);
            CHECK(portVip == origVip);
        }
        // The uids the network game's items gave the state's entries.
        CHECK(std::memcmp(reinterpret_cast<const uint8_t *>(&state) + 0x4C, orig.bytes + 0x4C, 0x1E) == 0);
        CHECK(state.belt.size() == orig.belt.size() &&
              std::memcmp(state.belt.data(), orig.belt.data(), state.belt.size() * sizeof(S_SoldierItem)) == 0);
        CHECK(state.backpackContents.size() == orig.backpack.size() &&
              std::memcmp(state.backpackContents.data(), orig.backpack.data(),
                          state.backpackContents.size() * sizeof(S_SoldierItem)) == 0);
    }
    g_skipFields.erase("m_faceTexture");
}

// ---- part 20f: the multiplayer spawn

// The original's: the replication's FindObject and the object's GetProperty answer fakes; the position track is the
// property's +0x30 (each side its own track object: g++'s slots of SetValue / SetNetValue / SetRate are not the
// original's 0x1C / 0x20 / 0x24).
void *ST ON_FindObject(void *, int id, int create)
{
    Log(nullptr, "FindNetObject", {double(id), double(B(create))});
    return g_netObjectBytes;
}
void *ST ON_GetProperty(void *self, int index)
{
    Log(self, "GetNetProperty", {double(index)});
    return g_origProperty;
}
void ST TR_SetValue(void *self, const S_vector4 *v) { Log(self, "TrackSetValue", {v->x, v->y, v->z, v->w}); }
void ST TR_SetNetValue(void *self, const S_vector4 *v) { Log(self, "TrackSetNetValue", {v->x, v->y, v->z, v->w}); }
void ST TR_SetRate(void *self, const S_vector4 *v) { Log(self, "TrackSetRate", {v->x, v->y, v->z, v->w}); }
void *g_portTrackVt[32], *g_origTrackVt[32];
// The team's and the interface's members, the camera module (the original's global GetCameraModule; its
// SetSpecialMode is slot 0x44).
void ST OP_SelectMember(void *, void *human) { Log(human, "SelectMember"); }
void ST OP_UpdateCameraTarget(void *) { Log(nullptr, "UpdateCameraTarget"); }
void ST OP_SetWatchedHuman(void *, void *human) { Log(human, "SetWatchedHuman"); }
void ST OP_ShowSquadMember(void *, void *human) { Log(human, "ShowSquadMember"); }
void ST OP_SetScreen(void *, int screen) { Log(nullptr, "SetScreen", {double(screen)}); }
void *OP_GetCameraModule() { return g_camModule; }
void ST OP_SetSpecialMode(void *, int mode, int on, void *target)
{
    Log(nullptr, "SetSpecialMode", {double(mode), double(B(on)), double(target != nullptr)});
}
// The human's own StartMission / StopMission (its slots 0x3C / 0x40; tested in part 20c): logged.
void ST HV_StartMission(void *self, int server) { Log(self, "VStartMission", {double(B(server))}); }
void ST HV_StopMission(void *self, int server) { Log(self, "VStopMission", {double(B(server))}); }

void SetUpRespawn()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0073f9c0, reinterpret_cast<const void *>(ON_FindObject)},
        {0x0073e690, reinterpret_cast<const void *>(ON_GetProperty)},
        {0x0045da60, reinterpret_cast<const void *>(OP_SelectMember)},
        {0x0045dac0, reinterpret_cast<const void *>(OP_UpdateCameraTarget)},
        {0x0045e8f0, reinterpret_cast<const void *>(OP_SetWatchedHuman)},
        {0x0049f100, reinterpret_cast<const void *>(OP_ShowSquadMember)},
        {0x0049f250, reinterpret_cast<const void *>(OP_SetScreen)},
        {0x00464e90, reinterpret_cast<const void *>(OP_GetCameraModule)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    *OriginalAt<void **>(0x0097d084) = g_netBytes;
    for (int k = 0; k < 32; ++k)
        g_portTrackVt[k] = g_origTrackVt[k] = reinterpret_cast<void *>(CT_Unexpected);
    g_portTrackVt[VSlot(&C_NetTrack::SetValue)] = reinterpret_cast<void *>(TR_SetValue);
    g_portTrackVt[VSlot(&C_NetTrack::SetNetValue)] = reinterpret_cast<void *>(TR_SetNetValue);
    g_portTrackVt[VSlot(static_cast<void (C_NetTrack::*)(const S_vector4 &)>(&C_NetTrack::SetRate))] = reinterpret_cast<void *>(TR_SetRate);
    g_origTrackVt[0x1C / 4] = reinterpret_cast<void *>(TR_SetValue);
    g_origTrackVt[0x20 / 4] = reinterpret_cast<void *>(TR_SetNetValue);
    g_origTrackVt[0x24 / 4] = reinterpret_cast<void *>(TR_SetRate);
    *reinterpret_cast<void ***>(g_portTrack) = g_portTrackVt;
    *reinterpret_cast<void ***>(g_origProperty + 0x30) = g_origTrackVt;
    g_names[g_portTrack] = g_names[g_origProperty + 0x30] = "track";
    g_names[g_netObjectBytes] = "netObject";
    g_camModuleVt[0x44 / 4] = reinterpret_cast<void *>(OP_SetSpecialMode);
    p_humanVt[VSlot(&C_Actor::StartMission)] = reinterpret_cast<void *>(HV_StartMission);
    p_humanVt[VSlot(&C_Actor::StopMission)] = reinterpret_cast<void *>(HV_StopMission);
    o_humanVt[0x3C / 4] = reinterpret_cast<void *>(HV_StartMission);
    o_humanVt[0x40 / 4] = reinterpret_cast<void *>(HV_StopMission);
}

typedef void(ST *T_FRespawn)(void *, const S_vector4 *, const S_vector4 *, void *, const int *);

// A respawn: a place and a direction, a soldier's state or none, stats or none, the game's roles, the local player's
// human or another's.
void TestSelfRespawn()
{
    C_Human *h = g_h.human;
    std::printf("self respawn\n");
    g_skipFields.insert("m_faceTexture");
    for (int i = 0; i < 8000 / QUICK; ++i) {
        RandomRestart();
        h->m_controller = reinterpret_cast<C_HumanController *>(g_portController);
        S_SoldierState state;
        RandomState(state);
        S_OrigState orig;
        ToOrigState(state, orig);
        g_self.items = P(0.5f);
        int stats[12];
        for (int &v : stats)
            v = I(0, 120);
        const bool withState = P(0.6f), withStats = P(0.4f);
        const S_vector4 pos = {U(4, 30), U(0.5f, 3), U(4, 30), 1.0f};
        const S_vector dir3 = S_vector(U(-1, 1), 0.0f, U(-1, 1)).Normalized();
        const S_vector4 dir = {dir3.x, dir3.y, dir3.z, 0.0f};
        // (The model's place and turn compared too: its local matrix after each side.)
        S_matrix portModel, origModel;
        DiffCall("Respawn",
                 [&] {
                     g_itemNext = 0;
                     h->Respawn(pos, dir, withState ? &state : nullptr, withStats ? stats : nullptr);
                     portModel = g_h.frame->GetLocalMatrix();
                     return 0;
                 },
                 [&](void *) {
                     g_itemNext = 0;
                     OriginalAt<T_FRespawn>(0x0059f050)(o_human, &pos, &dir, withState ? orig.bytes : nullptr,
                                                        withStats ? stats : nullptr);
                     origModel = g_h.frame->GetLocalMatrix();
                     return 0;
                 });
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK(std::fabs(portModel.m[r][c] - origModel.m[r][c]) <= 1e-5f);
    }
    g_skipFields.erase("m_faceTexture");
}

// ---- part 20g: the saved games, the mission's properties, SetFrame and CopyFrom

alignas(16) uint8_t g_carriedBytes[0x100];   // an actor carried
// The human's parts' own saves and loads (each logged, nothing written: the file holds the human's own chunks).
int ST OS_ActorSave(void *self, C_chunk *)
{
    Log(self, "ActorSave");
    return 1;
}
int ST OS_ActorLoad(void *self, C_chunk *)
{
    Log(self, "ActorLoad");
    return 1;
}
int ST OS_ActorSetFrame(void *self, I3D_frame *frame)
{
    Log(self, ("ActorSetFrame " + NameOf(frame)).c_str());
    return g_self.actorSetFrame;
}
void ST OS_ActorCopyFrom(void *self, const void *) { Log(self, "ActorCopyFrom"); }
int ST OS_BodySave(void *, C_chunk *)
{
    Log(nullptr, "BodySave");
    return 1;
}
int ST OS_BodyLoad(void *, C_chunk *)
{
    Log(nullptr, "BodyLoad");
    return 1;
}
int ST OS_EventsSave(void *self, C_chunk *)
{
    Log(self, "EventsSave");
    return 1;
}
int ST OS_EventsLoad(void *self, C_chunk *)
{
    Log(self, "EventsLoad");
    return 1;
}
int ST OS_AIMemorySave(void *self, C_chunk *)
{
    Log(self, "AIMemorySave");
    return 1;
}
int ST OS_AIMemoryLoad(void *, C_chunk *)
{
    Log(nullptr, "AIMemoryLoad");
    return 1;
}
int ST OS_FormationSave(void *self, C_chunk *chunk)
{
    Log(self, "FormationSave");
    const int word = 7;
    chunk->Write(&word, 4);
    return 1;
}
void *ST OS_FormationCtor(void *self) { return self; }
int ST OS_FormationLoad(void *, C_chunk *)
{
    Log(nullptr, "FormationLoad");
    return 1;
}
void ST CT_Save(void *self, C_chunk *) { Log(self, "ControllerSave"); }
void ST CT_Load(void *self, C_chunk *) { Log(self, "ControllerLoad"); }
int g_vehicleId = 21, g_carriedId = 22;
void *ST OS_GetActorById(void *, int id)
{
    Log(&g_actorsModule, "GetActorById", {double(id)});
    return id == g_vehicleId ? g_vehicleBytes : id == g_carriedId ? g_carriedBytes : nullptr;
}
int ST OS_IsUnconscious(void *) { return g_self.unconscious; }
void ST OS_EnemyRecordsAdd(void *, void *enemy) { Log(enemy, "EnemyRecordsAdd"); }
void ST OS_SetObserverState(void *, int id, int state) { Log(nullptr, "SetObserverState", {double(id), double(state)}); }
void ST VH_SetSeatOccupant(void *self, int seat, void *occupant)
{
    Log(self, "SetSeatOccupant", {double(seat), double(occupant != nullptr)});
}

// The port's: the actor lookup by id (the vehicle 21, a carried actor 22).
C_Actor *SelfActorById(int id, bool)
{
    return id == g_vehicleId ? reinterpret_cast<C_Actor *>(g_vehicleBytes)
                             : id == g_carriedId ? reinterpret_cast<C_Actor *>(g_carriedBytes) : nullptr;
}

void SetUpSaves()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00610780, reinterpret_cast<const void *>(OS_ActorSave)},
        {0x00610a80, reinterpret_cast<const void *>(OS_ActorLoad)},
        {0x00610200, reinterpret_cast<const void *>(OS_ActorSetFrame)},
        {0x006101f0, reinterpret_cast<const void *>(OS_ActorCopyFrom)},
        {0x005ab4e0, reinterpret_cast<const void *>(OS_BodySave)},
        {0x005ab7a0, reinterpret_cast<const void *>(OS_BodyLoad)},
        {0x005ad040, reinterpret_cast<const void *>(OS_EventsSave)},
        {0x005ad390, reinterpret_cast<const void *>(OS_EventsLoad)},
        {0x005641b0, reinterpret_cast<const void *>(OS_AIMemorySave)},
        {0x005643e0, reinterpret_cast<const void *>(OS_AIMemoryLoad)},
        {0x005942d0, reinterpret_cast<const void *>(OS_FormationSave)},
        {0x00594560, reinterpret_cast<const void *>(OS_FormationLoad)},
        {0x00475d50, reinterpret_cast<const void *>(OS_GetActorById)},
        {0x005a6380, reinterpret_cast<const void *>(OS_IsUnconscious)},
        {0x0045e410, reinterpret_cast<const void *>(OS_EnemyRecordsAdd)},
        {0x0046f8c0, reinterpret_cast<const void *>(OS_SetObserverState)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_portControllerVt[VSlot(&C_HumanController::SaveGame)] = reinterpret_cast<void *>(CT_Save);
    g_portControllerVt[VSlot(&C_HumanController::LoadGame)] = reinterpret_cast<void *>(CT_Load);
    g_origControllerVt[0x2C / 4] = reinterpret_cast<void *>(CT_Save);
    g_origControllerVt[0x30 / 4] = reinterpret_cast<void *>(CT_Load);
    g_vehicleVt[0x74 / 4] = reinterpret_cast<void *>(VH_SetSeatOccupant);
    g_vehicleVt[VSlot(&C_Actor::SetSeatOccupant)] = reinterpret_cast<void *>(VH_SetSeatOccupant);
    g_actorById = SelfActorById;
    // (A load makes a formation with the original's _aligned_malloc: the harness's, as part 7's.)
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(O_AlignedMalloc));
    RedirectOriginal(0x007e559c, reinterpret_cast<const void *>(O_AlignedFree));
    // (Its constructor empty, as the port's fake: the original's builds its slots with code this test does not set up.)
    RedirectOriginal(0x005949c0, reinterpret_cast<const void *>(OS_FormationCtor));
    // The vehicle's and the carried actor's net ids (the port's C_Actor at its own offset, the original's at +0x10).
    reinterpret_cast<C_Actor *>(g_vehicleBytes)->SetNetId(21);
    reinterpret_cast<C_Actor *>(g_carriedBytes)->SetNetId(22);
    const int ids[] = {21, 22};
    std::memcpy(g_vehicleBytes + 0x10, &ids[0], 4);
    std::memcpy(g_carriedBytes + 0x10, &ids[1], 4);
    g_names[g_carriedBytes] = "carried";
}

typedef int(ST *T_GChunk)(void *, C_chunk *);
typedef void(ST *T_GState)(void *, C_chunk *);
typedef int(ST *T_GFrame)(void *, I3D_frame *);
typedef void(ST *T_GCopy)(void *, const void *);

// A human to save: its controller's type, the state's fields, a seat or none, carrying or not, a face texture or none.
void RandomSave()
{
    RandomRestart();
    C_Human *h = g_h.human;
    h->m_controller = reinterpret_cast<C_HumanController *>(g_portController);
    h->m_stamina = U(0, 120);
    h->m_bleeding = U(0, 3);
    h->m_bloodDropTime = I(0, 900);
    h->m_legHits = I(0, 4);
    h->m_disguise = U(0, 100);
    h->m_actionTime = I(0, 5000);
    h->m_actionTotal = I(0, 5000);
    h->m_actionText = I(0, 9000);
    h->m_vehicle = P(0.3f) ? reinterpret_cast<C_Actor *>(g_vehicleBytes) : nullptr;
    h->m_carried = P(0.3f) ? reinterpret_cast<C_Actor *>(g_carriedBytes) : nullptr;
    KeepStr(h->m_faceTexture, P(0.3f) ? "" : ("face_" + std::to_string(I(0, 99))).c_str());
    h->m_stats[STAT_HEALTH] = P(0.3f) ? 0 : I(1, 100);
    g_self.unconscious = P(0.2f);
    // A formation or none, the AI memory or none (a load makes them), the vehicle or the carried actor at id 0.
    h->m_formation = P(0.5f) ? reinterpret_cast<C_FormationShape *>(g_formationBytes) : nullptr;
    h->m_aiMemory = P(0.3f) ? nullptr : reinterpret_cast<C_HumanAIMemory *>(g_aiMemoryBytes);
    h->m_unknown2b8 = I(0, 99);
    h->m_unknown2bc = I(0, 99);
    h->m_group = I(0, 6);
    h->m_role = static_cast<E_HumanRole>(I(0, 5));
    h->m_carnage = P(0.5f);
    h->m_unknown1a5 = P(0.5f);
    h->m_hostileToPlayers = P(0.5f);
}

// The vehicle's and the carried actor's net ids, one of them 0 (kept from a save to its load).
void PickActorIds()
{
    const bool vehicleZero = P(0.5f);
    g_vehicleId = vehicleZero ? 0 : 21;
    g_carriedId = vehicleZero ? 22 : 0;
    reinterpret_cast<C_Actor *>(g_vehicleBytes)->SetNetId(g_vehicleId);
    reinterpret_cast<C_Actor *>(g_carriedBytes)->SetNetId(g_carriedId);
    std::memcpy(g_vehicleBytes + 0x10, &g_vehicleId, 4);
    std::memcpy(g_carriedBytes + 0x10, &g_carriedId, 4);
}

void TestSelfSaves()
{
    C_Human *h = g_h.human;
    std::printf("self saves\n");
    const std::string portFile = TempFile("human_port.sav"), origFile = TempFile("human_orig.sav");
    g_skipFields.insert("m_faceTexture");
    g_skipWords.insert({"m_scale", 12});
    for (int i = 0; i < 1500 / QUICK; ++i) {
        PickActorIds();
        RandomSave();
        const bool state = i % 3 == 0;
        std::remove(portFile.c_str());
        std::remove(origFile.c_str());
        // Saved: the files compared byte for byte.
        DiffCall(state ? "SaveState" : "SaveGame",
                 [&] {
                     C_chunk c;
                     c.Open(portFile.c_str(), true);
                     c.Begin(0x7777);
                     int r = 0;
                     if (state)
                         h->SaveState(c);
                     else
                         r = int(h->C_Human::SaveGame(c));
                     c.Ascend();
                     return r;
                 },
                 [&](void *) {
                     C_chunk c;
                     c.Open(origFile.c_str(), true);
                     c.Begin(0x7777);
                     int r = 0;
                     if (state)
                         OriginalAt<T_GState>(0x0059d190)(o_human, &c);
                     else
                         r = OriginalAt<T_GChunk>(0x0059d5d0)(o_human, &c) & 0xFF;
                     c.Ascend();
                     return r;
                 });
        const std::vector<uint8_t> saved = FileBytes(portFile);
        CHECK(saved == FileBytes(origFile));
        // Loaded into another human state from the port's file.
        RandomSave();
        std::string portFace, origFace;
        uint16_t outer = 0;
        if (state) {
            // (LoadState reads the chunks inside CHUNK_HUMAN_STATE: the file holds them at its top.)
            DiffCall("LoadState",
                     [&] {
                         C_chunk c;
                         c.Open(portFile.c_str(), false);
                         c.Descend(outer);
                         h->LoadState(c);
                         return 0;
                     },
                     [&](void *) {
                         C_chunk c;
                         c.Open(portFile.c_str(), false);
                         c.Descend(outer);
                         OriginalAt<T_GState>(0x0059d970)(o_human, &c);
                         return 0;
                     });
        } else {
            g_skipFields.insert("m_formation");
            g_skipFields.insert("m_aiMemory");
            bool portParts[2] = {}, origParts[2] = {};
            DiffCall("LoadGame",
                     [&] {
                         C_chunk c;
                         c.Open(portFile.c_str(), false);
                         c.Descend(outer);
                         const int r = int(h->C_Human::LoadGame(c));
                         portFace = h->m_faceTexture.c_str();
                         portParts[0] = h->m_formation != nullptr;
                         portParts[1] = h->m_aiMemory != nullptr;
                         return r;
                     },
                     [&](void *) {
                         C_chunk c;
                         c.Open(portFile.c_str(), false);
                         c.Descend(outer);
                         const int r = OriginalAt<T_GChunk>(0x0059ddf0)(o_human, &c) & 0xFF;
                         origFace = OrigStr(o_human + 0xBC);
                         origParts[0] = *reinterpret_cast<void **>(o_human + 0x2C8) != nullptr;
                         origParts[1] = *reinterpret_cast<void **>(o_human + 0x2C4) != nullptr;
                         return r;
                     });
            CHECK(portFace == origFace);
            CHECK(portParts[0] == origParts[0] && portParts[1] == origParts[1]);
            g_skipFields.erase("m_formation");
            g_skipFields.erase("m_aiMemory");
        }
    }
    std::remove(portFile.c_str());
    std::remove(origFile.c_str());
    g_skipFields.erase("m_faceTexture");
    g_skipWords.erase({"m_scale", 12});
}

// The mission's properties: written (the files compared), read back from properties of every version, short ones too.
void TestSelfProperties()
{
    C_Human *h = g_h.human;
    std::printf("self properties\n");
    const std::string portFile = TempFile("human_props_port.chk"), origFile = TempFile("human_props_orig.chk");
    for (int i = 0; i < 2000 / QUICK; ++i) {
        RandomSave();
        h->m_carnage = P(0.5f);
        h->m_unknown1a5 = P(0.5f);
        h->m_hostileToPlayers = P(0.5f);
        if (i % 2 == 0) {
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            DiffCall("WriteProperties",
                     [&] {
                         C_chunk c;
                         c.Open(portFile.c_str(), true);
                         return int(h->C_Human::WriteProperties(c));
                     },
                     [&](void *) {
                         C_chunk c;
                         c.Open(origFile.c_str(), true);
                         return OriginalAt<T_GChunk>(0x0059e6c0)(o_human, &c) & 0xFF;
                     });
            CHECK(FileBytes(portFile) == FileBytes(origFile));
            continue;
        }
        // A properties chunk: a version 9..15 and as many bytes as it has (or fewer).
        std::vector<uint8_t> bytes(1 + 4 + 4 + 48 + 4 + 1 + 1 + 1 + 4);
        for (uint8_t &b : bytes)
            b = uint8_t(I(0, 255));
        bytes[0] = uint8_t(I(9, 15));
        if (P(0.3f))
            std::memset(&bytes[bytes.size() - 4], 0, 4);           // (a text id of 0: none)
        if (P(0.3f))
            bytes.resize(size_t(I(1, int(bytes.size()))));
        {
            C_chunk c;
            std::remove(portFile.c_str());
            c.Open(portFile.c_str(), true);
            c.Begin(0x4321);
            c.Write(bytes.data(), int(bytes.size()));
            c.Ascend();
        }
        uint16_t id = 0;
        DiffCall("ReadProperties",
                 [&] {
                     C_chunk c;
                     c.Open(portFile.c_str(), false);
                     c.Descend(id);
                     return int(h->C_Human::ReadProperties(c));
                 },
                 [&](void *) {
                     C_chunk c;
                     c.Open(portFile.c_str(), false);
                     c.Descend(id);
                     return OriginalAt<T_GChunk>(0x0059e800)(o_human, &c) & 0xFF;
                 });
    }
    std::remove(portFile.c_str());
    std::remove(origFile.c_str());
}

// SetFrame (a model or another frame, the actor's own answer) and CopyFrom (another human's properties).
alignas(16) uint8_t g_portSource[sizeof(C_Human) + 0x100], g_origSource[0x1000];
void TestSelfFrame()
{
    C_Human *h = g_h.human;
    std::printf("self frame\n");
    for (int i = 0; i < 8000 / QUICK; ++i) {
        if (i % 2 == 0) {
            RandomSave();
            I3D_frame *frame = P(0.6f) ? g_h.frame : g_zFrame;
            g_self.actorSetFrame = P(0.8f);
            DiffCall("SetFrame", [&] { return int(h->C_Human::SetFrame(frame)); },
                     [&](void *) { return OriginalAt<T_GFrame>(0x0059e5e0)(o_human, frame) & 0xFF; });
            continue;
        }
        // The source: a human in each layout (the same values), then the target given others.
        RandomSave();
        h->m_carnage = P(0.5f);
        h->m_unknown1a5 = P(0.5f);
        h->m_hostileToPlayers = P(0.5f);
        ToOriginal();
        std::memcpy(g_portSource, static_cast<const void *>(h), sizeof(C_Human));
        std::memcpy(g_origSource, o_human, sizeof g_origSource);
        RandomSave();
        DiffCall("CopyFrom", [&] { h->C_Human::CopyFrom(reinterpret_cast<const C_Actor *>(g_portSource)); return 0; },
                 [&](void *) { OriginalAt<T_GCopy>(0x0059e650)(o_human, g_origSource); return 0; });
    }
}

void TestSelf()
{
    SetUpSelf();
    SetUpUpdate();
    SetUpMission();
    SetUpRestart();
    SetUpEquipment();
    SetUpRespawn();
    SetUpSaves();
    // SELF_ONLY: the tests of one part alone (20a skills, 20b update, 20c mission, 20d restart, 20e equipment,
    // 20f saves): its mutation list runs them alone (all together take two minutes, past the runner's limit).
    const char *only = std::getenv("SELF_ONLY");
    const auto run = [&](const char *part) { return !only || std::strcmp(only, part) == 0; };
    const auto timed = [](const char *name, void (*test)()) {
        const auto start = std::chrono::steady_clock::now();
        test();
        std::printf("(%s: %.1f s)\n", name,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    };
    if (run("20a")) {
        timed("skills", TestSelfSkills);
        timed("names", TestSelfNames);
    }
    if (run("20b"))
        timed("update", TestSelfUpdate);
    if (run("20c"))
        timed("mission", TestSelfMission);
    if (run("20d"))
        timed("restart", TestSelfRestart);
    if (run("20e")) {
        timed("equipment", TestSelfEquipment);
        timed("respawn", TestSelfRespawn);
    }
    if (run("20f")) {
        timed("saves", TestSelfSaves);
        timed("properties", TestSelfProperties);
        timed("frame", TestSelfFrame);
    }
}

}  // namespace
