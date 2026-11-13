// Part 20 of the human harness (HUMAN_SELF builds, build_human.bat; included by human_test.cpp after base_test.inc): the
// human itself (game/human.cpp) against the original's, on the base harness's human (both layouts). The modules
// human.cpp talks to are fakes: their calls logged (the original's redirected to the same loggers), their answers
// scripted.
#include "game/first_person_view.h"
#include "game/camera_module.h"
#include "game/profile.h"
#include "game/team.h"
#include "game/interface.h"

// ---- the port's side: the modules (raw storage under their names: their constructors and vtables not linked)
asm(".globl _g_sightModule\n\t.bss\n\t.balign 16\n_g_sightModule:\n\t.space 1024\n\t.text");
asm(".globl _g_itemsModule\n\t.bss\n\t.balign 16\n_g_itemsModule:\n\t.space 4096\n\t.text");
asm(".globl _g_texts\n\t.bss\n\t.balign 16\n_g_texts:\n\t.space 256\n\t.text");
asm(".globl _g_netGame\n\t.bss\n\t.balign 16\n_g_netGame:\n\t.space 8192\n\t.text");
asm(".globl _g_gameProfile\n\t.bss\n\t.balign 16\n_g_gameProfile:\n\t.space 4096\n\t.text");
int g_localPlayerSlot = 0;
C_NetReplication *g_netReplication = nullptr;
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
C_CameraModule *GetCameraModule() { return nullptr; }
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
C_Item *C_ItemsModule::CreateItem(int typeId, int container, C_Actor *owner, int uid)
{
    Log(owner, "CreateItem", {double(typeId), double(container), double(uid)});
    return nullptr;
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
    return nullptr;
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
    return nullptr;
}
C_NetProperty *C_NetObject::GetProperty(int index) { return nullptr; }
C_NetPositionTrack &C_NetProperty::GetTrack() { return *static_cast<C_NetPositionTrack *>(nullptr); }

// The interface, the team, the first person view, the surface marks, mines, orders and sounds.
void C_Interface::ShowTimer(int textId, int ms) { Log(nullptr, "ShowTimer", {double(textId), double(ms)}); }
void C_Interface::ShowSquadMember(C_Human *human) { Log(human, "ShowSquadMember"); }
void C_Interface::SetScreen(int screen) { Log(nullptr, "SetScreen", {double(screen)}); }
bool C_TeamBase::IsMember(const C_Human *human) const
{
    Log(human, "IsTeamMember");
    return false;
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
    Log(this, "AIMemoryLoad");
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
    return 0;
}
void S_HumanBody::SetModelVisible(bool on) { Log(nullptr, "BodySetModelVisible", {double(on)}); }

// The human's members in other files.
void C_Human::CreateController(E_HumanController type) { Log(this, "CreateController", {double(type)}); }
const char *C_Human::GetShortName() const { return "shortName"; }
const char *C_Human::GetFullName() const { return "fullName"; }
void C_Human::OnHandsChanged() { Log(this, "OnHandsChanged"); }
void __stdcall C_Human::OnSoundHeard(const S_HeardSound *) {}
void C_Human::AssignPlayerController(bool active) { Log(this, "AssignPlayerController", {double(active)}); }
void C_Human::SetLeader(C_Human *leader) { Log(this, "SetLeader", {double(leader != nullptr)}); }
bool C_Human::PutOnUniform(int uid)
{
    Log(this, "PutOnUniform", {double(uid)});
    return true;
}
uint32_t C_Human::GetStateFlags() const { return g_self.stateFlags; }
uint32_t C_Human::GetMoveMode() const { return g_self.moveMode; }
void C_Human::DisbandFormation() { Log(this, "DisbandFormation"); }
void C_Human::UpdateVelocity(int timeDelta) { Log(this, "UpdateVelocity", {double(timeDelta)}); }
void C_Human::UpdateDisguise(float amount) { Log(this, "UpdateDisguise", {amount}); }
void C_Human::Done() { Log(this, "Done"); }
void C_Human::Init() { Log(this, "Init"); }
void C_Human::WriteState(C_BitStream *) { Log(this, "WriteState"); }
void C_Human::ReadState(C_BitStream *) { Log(this, "ReadState"); }
void C_Human::Unknown20(C_BitStream *) { Log(this, "Unknown20"); }
void C_Human::OnMessage(C_BitStream *) { Log(this, "OnMessage"); }
void C_Human::UpdateSeenHumans() { Log(this, "UpdateSeenHumans"); }
void C_Human::UpdateBleeding(int frameTime) { Log(this, "UpdateBleeding", {double(frameTime)}); }
void C_Human::TickBleeding(int timeDelta) { Log(this, "TickBleeding", {double(timeDelta)}); }
bool C_Human::OnCollisionEx(C_Actor *, const S_vector *, int, int, int, int, int) { return false; }
bool C_Human::IsUnconscious() const { return g_self.unconscious; }
void C_Human::Hit(E_HitType, float, const S_vector &, const S_vector &, C_Actor *, int) {}
void C_Human::GetBoxCorners(S_vector4 *) {}
void C_Human::ForgetOldSounds() { Log(this, "ForgetOldSounds"); }
void C_Human::ClearContainers() { Log(this, "ClearContainers"); }
void C_Human::ApplyRemoteHit(int, int, int, int, const S_vector &, const S_vector &) {}
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
    return true;
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
void C_CameraModule::SetSpecialMode(int mode, bool on, I3D_frame *target) { Log(this, "SetSpecialMode", {double(mode), double(on)}); }
void C_CameraHumanEffects::Remove(C_Human *human) { Log(human, "CameraEffectsRemove"); }
void C_EnemyRecords::Add(C_Human *enemy) { Log(enemy, "EnemyRecordsAdd"); }
C_FormationShape::C_FormationShape() {}
void C_FormationShape::Tick(int timeDelta) { Log(this, "FormationTick", {double(timeDelta)}); }
bool C_FormationShape::SaveGame(C_chunk &)
{
    Log(this, "FormationSave");
    return true;
}
bool C_FormationShape::LoadGame(C_chunk &)
{
    Log(this, "FormationLoad");
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
void SelfSide(bool original)
{
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

void TestSelf()
{
    SetUpSelf();
    TestSelfSkills();
    TestSelfNames();
}

}  // namespace
