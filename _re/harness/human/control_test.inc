// Part 22 of the human harness (HUMAN_CONTROL builds, build_control.bat; included by human_test.cpp after
// combat_test.inc): the human's control functions (game/human_control.cpp) against the original's, on part 21's harness
// (its fakes of the control functions left out: this build links the real ones). The controllers the human makes and
// forwards to, the body, the formations, the events and the network game are fakes: their calls logged on both sides
// and their answers scripted.
//
// Part 22a: the controller made (CreateController, AssignPlayerController, SetScripted, StartScriptedController) and
// the wrappers of its virtuals and of the player's, squad's, AI's and scripted controllers' members. 22b: the body,
// names, formations, the scripts' commands.
#include "game/car.h"
#include "game/controller_messages.h"
#include "game/squad_commander.h"

// ---- the port's side: what the controllers, the body and the other modules answer
namespace {
struct S_ControlScript {
    // S_HumanBody
    float bodySpeed = 0.0f;
    int bodyStance = 0;
    int bodyWorkState = 0;
    bool leftFoot = false;
    // C_ScriptedController
    int playActivity = 0;
    bool seesEnemy = false;
    bool hasAIMode = false;
    int checkpoint = 0;
    // C_AIController::GetPathPoints
    uint16_t pathCurrent = 0, pathNext = 0;
    // C_Car
    uint32_t carEnv = 0;
    // C_NetGame::GetPlayerName
    const char *playerName = "player";
    // The controller's virtuals
    bool answer = false;                                // the bool ones
    int count = 0;                                      // the int ones
    float aimBonus = 0.0f;
    float dispersion = 0.0f;                            // the weapon component's GetAimedDispersion
    int carryMode = 0;                                  // the weapon definition's GetCarryMode
    bool weapon = false;                                // GetHandsWeapon answers the weapon
    S_vector dir = S_vector(0, 0, 1);                   // GetFacingDir / GetViewDir
} g_ctl;
}  // namespace

// The controllers CreateController makes (the derived ones' vtables in raw storage under their symbols' names: their
// own code is not linked): each logs its constructor's type, its Attach, Detach and deletion.
void *g_vtHumanController[200] asm("__ZTV17C_HumanController");
void *g_vtAIController[200] asm("__ZTV14C_AIController");
void *g_vtPlayer[200] asm("__ZTV18C_PlayerController");
void *g_vtEnemy[200] asm("__ZTV17C_EnemyController");
void *g_vtScripted[200] asm("__ZTV20C_ScriptedController");
void *g_vtNeutral[200] asm("__ZTV19C_NeutralController");
void *g_vtRemote[200] asm("__ZTV18C_RemoteController");
void *g_vtSniper[200] asm("__ZTV18C_SniperController");
C_HumanController::C_HumanController(E_HumanController type) : m_type(type)
{
    g_names[this] = "newController";
    Log(this, "ControllerCtor", {double(type)});
}
C_AIController::C_AIController(E_HumanController type) : C_HumanController(type) {}
C_PlayerController::C_PlayerController() : C_HumanController(HUMAN_CONTROLLER_PLAYER) {}
alignas(16) uint8_t g_portSquadBytes[64][0x800];
int g_portSquads = 0;
void *g_portSquadVt[200];
C_SquadController *C_SquadController::Create()
{
    Log(nullptr, "SquadCreate");
    uint8_t *bytes = g_portSquadBytes[g_portSquads++ % 64];
    std::memset(bytes, 0, 0x800);
    *reinterpret_cast<void ***>(bytes) = g_portSquadVt;
    auto *c = reinterpret_cast<C_SquadController *>(bytes);
    c->m_type = HUMAN_CONTROLLER_SQUAD;
    g_names[bytes] = "newController";
    return c;
}

// The player's, the AI's and the scripted controllers' members the human calls.
void C_PlayerController::HandleInput(float *held, float *pressed)
{
    Log(this, "PlHandleInput", {double(held != nullptr), double(pressed != nullptr), held ? held[0] : 0.0f});
}
void C_PlayerController::HandleMessage(C_BitStream *msg) { Log(msg, "PlHandleMessage"); }
void C_PlayerController::HandleMessage2(C_BitStream *msg) { Log(msg, "PlHandleMessage2"); }
void C_PlayerController::RequestDamage(int type, int damage, int attackerId, int zone)
{
    Log(this, "PlRequestDamage", {double(type), double(damage), double(attackerId), double(zone)});
}
void C_PlayerController::RequestEquipment(int humanId) { Log(this, "PlRequestEquipment", {double(humanId)}); }
void C_PlayerController::SendActionCommand(const S_ActionCommand &c)
{
    Log(this, ("PlSendActionCommand " + NameOf(c.actor) + " " + NameOf(c.member)).c_str(),
        {double(c.category), double(c.unknown08), double(c.unknown0C), c.dir.x, c.dir.y, c.dir.z, c.dir.w,
         double(c.action), double(c.command), double(c.value)});
}
void C_PlayerController::SetViewMode(int mode) { Log(this, "PlSetViewMode", {double(mode)}); }
void C_AIController::GetPathPoints(uint16_t *current, uint16_t *next)
{
    Log(this, "AiGetPathPoints");
    *current = g_ctl.pathCurrent;
    *next = g_ctl.pathNext;
}
void C_AIController::LookAround(int type) { Log(this, "AiLookAround", {double(type)}); }
#define SC_VOID(name)                                                                                                  \
    void C_ScriptedController::name() { Log(this, "Sc" #name); }
SC_VOID(ActivityTurk)
SC_VOID(DisableEvents)
SC_VOID(DropObject)
SC_VOID(EnableEvents)
SC_VOID(HolsterWeapon)
SC_VOID(Reload)
SC_VOID(ResetAI)
SC_VOID(SetMoveModeCrouch)
SC_VOID(SetMoveModeGuard)
SC_VOID(SetMoveModeLie)
SC_VOID(SetMoveModeRun)
SC_VOID(SetMoveModeSilence)
SC_VOID(SetMoveModeStand)
SC_VOID(SetMoveModeStandFast)
SC_VOID(SetMoveModeWalk)
SC_VOID(StartScript)
SC_VOID(StopAction)
SC_VOID(TakeWeapon)
#undef SC_VOID
#define SC_INT(name)                                                                                                   \
    void C_ScriptedController::name(int a) { Log(this, "Sc" #name, {double(a)}); }
SC_INT(LookAround1)
SC_INT(MoveToAlarm)
SC_INT(PlaySpeech)
SC_INT(SetMode)
SC_INT(SetOptimized)
#undef SC_INT
void C_ScriptedController::AdvanceScript(int a, int b) { Log(this, "ScAdvanceScript", {double(a), double(b)}); }
void C_ScriptedController::AttackActor(C_Actor *target, int time, bool fake, int label)
{
    Log(this, ("ScAttackActor " + NameOf(target)).c_str(), {double(time), double(fake), double(label)});
}
void C_ScriptedController::AttackFrame(I3D_frame *target, int time, bool fake, int label)
{
    Log(this, ("ScAttackFrame " + NameOf(target)).c_str(), {double(time), double(fake), double(label)});
}
void C_ScriptedController::AttackMoveTo(int16_t checkpoint, int label)
{
    Log(this, "ScAttackMoveTo", {double(checkpoint), double(label)});
}
void C_ScriptedController::MoveTo(int16_t checkpoint, int label) { Log(this, "ScMoveTo", {double(checkpoint), double(label)}); }
uint16_t C_ScriptedController::FindCheckpoint(const char *name) const
{
    Log(this, (std::string("ScFindCheckpoint ") + name).c_str());
    return static_cast<uint16_t>(g_ctl.checkpoint);
}
bool C_ScriptedController::HasAIMode() const
{
    Log(this, "ScHasAIMode");
    return g_ctl.hasAIMode;
}
void C_ScriptedController::PickObject(I3D_frame *object, bool oneHand)
{
    Log(this, ("ScPickObject " + NameOf(object)).c_str(), {double(oneHand)});
}
int C_ScriptedController::PlayActivity(int activity)
{
    Log(this, "ScPlayActivity", {double(activity)});
    return g_ctl.playActivity;
}
int C_ScriptedController::PlayActivity(int activity, I3D_frame *target)
{
    Log(this, ("ScPlayActivityAt " + NameOf(target)).c_str(), {double(activity)});
    return g_ctl.playActivity;
}
bool C_ScriptedController::SeesEnemy() const
{
    Log(this, "ScSeesEnemy");
    return g_ctl.seesEnemy;
}
void C_ScriptedController::SetAlarm(bool on) { Log(this, "ScSetAlarm", {double(on)}); }
void C_ScriptedController::SetAlarmType(uint32_t flags, bool set) { Log(this, "ScSetAlarmType", {double(flags), double(set)}); }
void C_ScriptedController::SetAnimation(const char *name, int blendTime, int cancelTime, bool loop)
{
    Log(this, (std::string("ScSetAnimation ") + name).c_str(), {double(blendTime), double(cancelTime), double(loop)});
}
void C_ScriptedController::SetWaitForPlayer(bool on, float stopDist2, float resumeDist2, bool watch)
{
    Log(this, "ScSetWaitForPlayer", {double(on), stopDist2, resumeDist2, double(watch)});
}
void C_ScriptedController::Sit(I3D_frame *seat) { Log(this, ("ScSit " + NameOf(seat)).c_str()); }
void C_ScriptedController::TurnAt(I3D_frame *target) { Log(this, ("ScTurnAt " + NameOf(target)).c_str()); }
void C_HumanController::RaiseWeapon() { Log(this, "RaiseWeapon"); }
void C_HumanController::LowerWeapon() { Log(this, "LowerWeapon"); }

// The body, the events, the movement, the car, the network game, the squad commander, the hands weapon.
void S_HumanBody::Execute(const S_BodyCommand &command) { Log(&command, "BodyExecute"); }
float S_HumanBody::GetSpeed() const
{
    Log(nullptr, "BodyGetSpeed");
    return g_ctl.bodySpeed;
}
int S_HumanBody::GetStance() const
{
    Log(nullptr, "BodyGetStance");
    return g_ctl.bodyStance;
}
int S_HumanBody::GetWorkState() const
{
    Log(nullptr, "BodyGetWorkState");
    return g_ctl.bodyWorkState;
}
bool S_HumanBody::IsLeftFootForward() const
{
    Log(nullptr, "BodyIsLeftFootForward");
    return g_ctl.leftFoot;
}
void S_HumanBody::PlayAnimation(const char *name, int a, int b, int c)
{
    Log(nullptr, (std::string("BodyPlayAnimation ") + name).c_str(), {double(a), double(b), double(c)});
}
void S_HumanBody::SetCarryMode(int mode) { Log(nullptr, "BodySetCarryMode", {double(mode)}); }
void S_HumanBody::SetPlayerControlled(bool on) { Log(nullptr, "BodySetPlayerControlled", {double(on)}); }
void C_HumanEvents::SetSniper(bool on, float radius, const S_vector &pos)
{
    Log(this, "EventsSetSniper", {double(on), radius, pos.x, pos.y, pos.z});
}
void C_HumanMovement::SetUnknown94(float value) { Log(this, "MovementSetUnknown94", {value}); }
uint32_t C_Car::GetEnvironmentFlags() const
{
    Log(this, "CarGetEnvironmentFlags");
    return g_ctl.carEnv;
}
const char *C_NetGame::GetPlayerName(int slot)
{
    Log(nullptr, "NetGetPlayerName", {double(slot)});
    return g_ctl.playerName;
}
alignas(16) uint8_t g_portCommander[0x40], g_origCommander[0x40];
void *g_portCommanderVt[64], *g_origCommanderVt[64];
C_SquadCommander *GetSquadCommander() { return reinterpret_cast<C_SquadCommander *>(g_portCommander); }
alignas(16) uint8_t g_portWeapon[0x100], g_origWeapon[0x100], g_portWeaponDef[0x200], g_origWeaponDef[0x200],
    g_weaponComp[0x40];
void *g_portDefVt[64], *g_origDefVt[64], *g_compVt[64];
C_WeaponItem *C_Human::GetHandsWeapon() const
{
    Log(this, "GetHandsWeapon");
    return g_ctl.weapon ? reinterpret_cast<C_WeaponItem *>(g_portWeapon) : nullptr;
}

// The formations (each side its own objects; the slots' members each side's humans): the shape's members, leader,
// type and scale kept as the original keeps them (its offsets are the port's: +0x74 the leader, +0x78 the type, +0x7C
// the member count, +0x90 the slots, 0xA0 bytes each with the member at +0x70).
namespace {
void FormationInit(void *f)
{
    auto *b = static_cast<uint8_t *>(f);
    std::memset(b, 0, 0xA0);
    void *slots = _aligned_malloc(6 * 0xA0, 16);
    std::memset(slots, 0, 6 * 0xA0);
    std::memcpy(b + 0x90, &slots, 4);
    const int follow = FORMATION_FOLLOW;
    std::memcpy(b + 0x78, &follow, 4);
    g_names[f] = "newFormation";
}
uint8_t *FormationSlots(const void *f) { return *reinterpret_cast<uint8_t *const *>(static_cast<const uint8_t *>(f) + 0x90); }
int &FormationCount(void *f) { return *reinterpret_cast<int *>(static_cast<uint8_t *>(f) + 0x7C); }
bool FormationAdd(void *f, void *member)
{
    Log(f, ("FormationAddMember " + NameOf(member)).c_str());
    uint8_t *slots = FormationSlots(f);
    for (int i = 0; i < 6 && slots; ++i) {
        void *&m = *reinterpret_cast<void **>(slots + i * 0xA0 + 0x70);
        if (!m) {
            m = member;
            ++FormationCount(f);
            return true;
        }
    }
    return false;
}
bool FormationRemove(void *f, void *member)
{
    Log(f, ("FormationRemoveMember " + NameOf(member)).c_str());
    uint8_t *slots = FormationSlots(f);
    for (int i = 0; i < 6 && slots; ++i) {
        void *&m = *reinterpret_cast<void **>(slots + i * 0xA0 + 0x70);
        if (m == member) {
            m = nullptr;
            --FormationCount(f);
            return true;
        }
    }
    return false;
}
}  // namespace
C_FormationShape::C_FormationShape()
{
    Log(nullptr, "FormationNew");
    FormationInit(this);
}
C_FormationShape::~C_FormationShape() { Log(this, "FormationDelete"); }
bool C_FormationShape::AddMember(C_Human *member) { return FormationAdd(this, member); }
bool C_FormationShape::RemoveMember(C_Human *member) { return FormationRemove(this, member); }
bool C_FormationShape::SetLeader(C_Human *leader)
{
    Log(this, ("FormationSetLeader " + NameOf(leader)).c_str());
    m_leader = leader;
    return true;
}
void C_FormationShape::SetType(E_FormationType type)
{
    Log(this, "FormationSetType", {double(type)});
    m_type = type;
}
void C_FormationShape::SetScale(const S_vector &scale) { Log(this, "FormationSetScale", {scale.x, scale.y, scale.z}); }
S_vector g_defaultFormationScale(1.5f, 1.0f, 2.5f);

namespace {
// ---- the original's side
#define CT_ __attribute__((thiscall))
// Its constructors: the controller's (the player's and the remote one's) and the AI's (the others'), the squad's
// Create, the aligned allocation (operator new for 16-byte aligned classes) and its release.
void *OC_AlignedNew(size_t size, size_t align)
{
    void *p = _aligned_malloc(size, align);
    std::memset(p, 0, size);
    return p;
}
void OC_AlignedDelete(void *) {}
void CT_ OC_ControllerCtor(void *self, int type)
{
    g_names[self] = "newController";
    Log(self, "ControllerCtor", {double(type)});
    std::memcpy(static_cast<uint8_t *>(self) + 0x48, &type, 4);
}
alignas(16) uint8_t g_origSquadBytes[64][0x800];
int g_origSquads = 0;
void *g_origSquadVt[200];
void *OC_SquadCreate()
{
    Log(nullptr, "SquadCreate");
    uint8_t *bytes = g_origSquadBytes[g_origSquads++ % 64];
    std::memset(bytes, 0, 0x800);
    *reinterpret_cast<void ***>(bytes) = g_origSquadVt;
    const int type = HUMAN_CONTROLLER_SQUAD;
    std::memcpy(bytes + 0x48, &type, 4);
    g_names[bytes] = "newController";
    return bytes;
}
// The controllers' Attach, Detach and deletion (one logger for both sides).
void CT_ CC_Attach(void *self, void *human, void *previous)
{
    Log(self, ("ControllerAttach " + NameOf(human) + " " + NameOf(previous)).c_str());
}
void CT_ CC_Detach(void *self) { Log(self, "ControllerDetach"); }
void CT_ CC_DeletePort(void *self) { Log(self, "ControllerDelete"); }
// (The original's deleting destructor frees with flag 1, as the port's delete does.)
void CT_ CC_DeleteOrig(void *self, int flags) { Log(self, flags == 1 ? "ControllerDelete" : "ControllerDestroy"); }
void CT_ CC_CancelOrders(void *self, int turn) { Log(self, "SquadCancelOrders", {double(B(turn))}); }

// The player's, the AI's and the scripted controllers' members.
void CT_ OC_HandleInput(void *self, float *held, float *pressed)
{
    Log(self, "PlHandleInput", {double(held != nullptr), double(pressed != nullptr), held ? held[0] : 0.0f});
}
void CT_ OC_HandleMessage(void *msg) { Log(msg, "PlHandleMessage"); }
void CT_ OC_HandleMessage2(void *msg) { Log(msg, "PlHandleMessage2"); }
void CT_ OC_RequestDamage(void *self, int type, int damage, int attackerId, int zone)
{
    Log(self, "PlRequestDamage", {double(type), double(damage), double(attackerId), double(zone)});
}
void CT_ OC_RequestEquipment(void *self, int humanId) { Log(self, "PlRequestEquipment", {double(humanId)}); }
void CT_ OC_SendActionCommand(void *self, const S_ActionCommand *c)
{
    Log(self, ("PlSendActionCommand " + NameOf(c->actor) + " " + NameOf(c->member)).c_str(),
        {double(c->category), double(c->unknown08), double(c->unknown0C), c->dir.x, c->dir.y, c->dir.z, c->dir.w,
         double(c->action), double(c->command), double(c->value)});
}
void CT_ OC_SetViewMode(void *self, int mode) { Log(self, "PlSetViewMode", {double(mode)}); }
void CT_ OC_GetPathPoints(void *self, uint16_t *current, uint16_t *next)
{
    Log(self, "AiGetPathPoints");
    *current = g_ctl.pathCurrent;
    *next = g_ctl.pathNext;
}
void CT_ OC_LookAround(void *self, int type) { Log(self, "AiLookAround", {double(type)}); }
#define OS_VOID(name)                                                                                                  \
    void CT_ OS_##name(void *self) { Log(self, "Sc" #name); }
OS_VOID(ActivityTurk)
OS_VOID(DisableEvents)
OS_VOID(DropObject)
OS_VOID(EnableEvents)
OS_VOID(HolsterWeapon)
OS_VOID(Reload)
OS_VOID(ResetAI)
OS_VOID(SetMoveModeCrouch)
OS_VOID(SetMoveModeGuard)
OS_VOID(SetMoveModeLie)
OS_VOID(SetMoveModeRun)
OS_VOID(SetMoveModeSilence)
OS_VOID(SetMoveModeStand)
OS_VOID(SetMoveModeStandFast)
OS_VOID(SetMoveModeWalk)
OS_VOID(StartScript)
OS_VOID(StopAction)
OS_VOID(TakeWeapon)
#undef OS_VOID
#define OS_INT(name)                                                                                                   \
    void CT_ OS_##name(void *self, int a) { Log(self, "Sc" #name, {double(a)}); }
OS_INT(LookAround1)
OS_INT(MoveToAlarm)
OS_INT(PlaySpeech)
OS_INT(SetMode)
OS_INT(SetOptimized)
#undef OS_INT
void CT_ OS_AdvanceScript(void *self, int a, int b) { Log(self, "ScAdvanceScript", {double(a), double(b)}); }
void CT_ OS_AttackActor(void *self, void *target, int time, int fake, int label)
{
    Log(self, ("ScAttackActor " + NameOf(target)).c_str(), {double(time), double(B(fake)), double(label)});
}
void CT_ OS_AttackFrame(void *self, void *target, int time, int fake, int label)
{
    Log(self, ("ScAttackFrame " + NameOf(target)).c_str(), {double(time), double(B(fake)), double(label)});
}
void CT_ OS_AttackMoveTo(void *self, int checkpoint, int label)
{
    Log(self, "ScAttackMoveTo", {double(int16_t(checkpoint)), double(label)});
}
void CT_ OS_MoveTo(void *self, int checkpoint, int label) { Log(self, "ScMoveTo", {double(int16_t(checkpoint)), double(label)}); }
uint32_t CT_ OS_FindCheckpoint(void *self, const char *name)
{
    Log(self, (std::string("ScFindCheckpoint ") + name).c_str());
    return static_cast<uint16_t>(g_ctl.checkpoint);
}
uint32_t CT_ OS_HasAIMode(void *self)
{
    Log(self, "ScHasAIMode");
    return g_ctl.hasAIMode;
}
void CT_ OS_PickObject(void *self, void *object, int oneHand)
{
    Log(self, ("ScPickObject " + NameOf(object)).c_str(), {double(B(oneHand))});
}
int CT_ OS_PlayActivity(void *self, int activity)
{
    Log(self, "ScPlayActivity", {double(activity)});
    return g_ctl.playActivity;
}
int CT_ OS_PlayActivityAt(void *self, int activity, void *target)
{
    Log(self, ("ScPlayActivityAt " + NameOf(target)).c_str(), {double(activity)});
    return g_ctl.playActivity;
}
uint32_t CT_ OS_SeesEnemy(void *self)
{
    Log(self, "ScSeesEnemy");
    return g_ctl.seesEnemy;
}
void CT_ OS_SetAlarm(void *self, int on) { Log(self, "ScSetAlarm", {double(B(on))}); }
void CT_ OS_SetAlarmType(void *self, uint32_t flags, int set) { Log(self, "ScSetAlarmType", {double(flags), double(B(set))}); }
void CT_ OS_SetAnimation(void *self, const char *name, int blendTime, int cancelTime, int loop)
{
    Log(self, (std::string("ScSetAnimation ") + name).c_str(), {double(blendTime), double(cancelTime), double(B(loop))});
}
void CT_ OS_SetWaitForPlayer(void *self, int on, float stopDist2, float resumeDist2, int watch)
{
    Log(self, "ScSetWaitForPlayer", {double(B(on)), stopDist2, resumeDist2, double(B(watch))});
}
void CT_ OS_Sit(void *self, void *seat) { Log(self, ("ScSit " + NameOf(seat)).c_str()); }
void CT_ OS_TurnAt(void *self, void *target) { Log(self, ("ScTurnAt " + NameOf(target)).c_str()); }
void CT_ OS_RaiseWeapon(void *self) { Log(self, "RaiseWeapon"); }
void CT_ OS_LowerWeapon(void *self) { Log(self, "LowerWeapon"); }

// The body (`this` the human's body record), the events, the movement, the car, the network game, the squad
// commander, the hands weapon.
void CT_ OB_Execute(void *, const void *command) { Log(command, "BodyExecute"); }
float CT_ OB_GetSpeed(void *)
{
    Log(nullptr, "BodyGetSpeed");
    return g_ctl.bodySpeed;
}
int CT_ OB_GetStance(void *)
{
    Log(nullptr, "BodyGetStance");
    return g_ctl.bodyStance;
}
int CT_ OB_GetWorkState(void *)
{
    Log(nullptr, "BodyGetWorkState");
    return g_ctl.bodyWorkState;
}
uint32_t CT_ OB_IsLeftFootForward(void *)
{
    Log(nullptr, "BodyIsLeftFootForward");
    return g_ctl.leftFoot;
}
void CT_ OB_PlayAnimation(void *, const char *name, int a, int b, int c)
{
    Log(nullptr, (std::string("BodyPlayAnimation ") + name).c_str(), {double(a), double(b), double(c)});
}
void CT_ OB_SetCarryMode(void *, int mode) { Log(nullptr, "BodySetCarryMode", {double(mode)}); }
void CT_ OB_SetPlayerControlled(void *, int on) { Log(nullptr, "BodySetPlayerControlled", {double(B(on))}); }
void CT_ OE_SetSniper(void *self, int on, float radius, const S_vector *pos)
{
    Log(self, "EventsSetSniper", {double(B(on)), radius, pos->x, pos->y, pos->z});
}
void CT_ OM_SetUnknown94(void *self, float value) { Log(self, "MovementSetUnknown94", {value}); }
uint32_t CT_ OV_GetEnvironmentFlags(void *self)
{
    Log(self, "CarGetEnvironmentFlags");
    return g_ctl.carEnv;
}
const char *CT_ ON_GetPlayerName(void *, int slot)
{
    Log(nullptr, "NetGetPlayerName", {double(slot)});
    return g_ctl.playerName;
}
void *OS_GetSquadCommander() { return g_origCommander; }
void CT_ CM_OnPlayerNoise(void *self) { Log(self, "CommanderOnPlayerNoise"); }
void *CT_ OH_GetHandsWeapon(void *self)
{
    Log(self, "GetHandsWeapon");
    return g_ctl.weapon ? g_origWeapon : nullptr;
}
int CT_ WD_GetCarryMode(void *self)
{
    Log(self, "DefGetCarryMode");
    return g_ctl.carryMode;
}
float CT_ WC_GetAimedDispersion(void *self)
{
    Log(self, "CompGetAimedDispersion");
    return g_ctl.dispersion;
}
// The formations.
// (Returns `this`: SetLeader uses it.)
void *CT_ OF_Ctor(void *self)
{
    Log(nullptr, "FormationNew");
    FormationInit(self);
    return self;
}
void CT_ OF_Dtor(void *self) { Log(self, "FormationDelete"); }
uint32_t CT_ OF_AddMember(void *self, void *member) { return FormationAdd(self, member); }
uint32_t CT_ OF_RemoveMember(void *self, void *member) { return FormationRemove(self, member); }
uint32_t CT_ OF_SetLeader(void *self, void *leader)
{
    Log(self, ("FormationSetLeader " + NameOf(leader)).c_str());
    std::memcpy(static_cast<uint8_t *>(self) + 0x74, &leader, 4);
    return true;
}
void CT_ OF_SetType(void *self, int type)
{
    Log(self, "FormationSetType", {double(type)});
    std::memcpy(static_cast<uint8_t *>(self) + 0x78, &type, 4);
}
void CT_ OF_SetScale(void *self, const S_vector *s) { Log(self, "FormationSetScale", {s->x, s->y, s->z}); }

// The controller's virtuals the wrappers call (one logger for both sides: the port's slots by its member pointers, the
// original's by their offsets).
S_vector g_ctlDir;
void CT_ CV_TestSightOfMeleeTracker(void *self, void *tracker, int *status, float *visibility)
{
    Log(self, ("CtlTestSightOfMeleeTracker " + NameOf(tracker)).c_str(), {double(*status), *visibility});
    *status = 99;
    *visibility = 100.0f;
}
void CT_ CV_TestSightOfHuman(void *self, void *target, int *status, float *visibility)
{
    Log(self, ("CtlTestSightOfHuman " + NameOf(target)).c_str(), {double(*status), *visibility});
    *status = 98;
    *visibility = 50.0f;
}
void CT_ CV_RequestLeaveVehicle(void *self) { Log(self, "CtlRequestLeaveVehicle"); }
const S_vector *CT_ CV_GetFacingDir(void *self)
{
    Log(self, "CtlGetFacingDir");
    g_ctlDir = g_ctl.dir;
    return &g_ctlDir;
}
const S_vector *CT_ CV_GetViewDir(void *self)
{
    Log(self, "CtlGetViewDir");
    g_ctlDir = S_vector(g_ctl.dir.z, g_ctl.dir.x, g_ctl.dir.y);
    return &g_ctlDir;
}
void CT_ CV_SetFacingDir(void *self, const S_vector4 *d) { Log(self, "CtlSetFacingDir", {d->x, d->y, d->z, d->w}); }
void CT_ CV_SetViewDir(void *self, const S_vector4 *d) { Log(self, "CtlSetViewDir", {d->x, d->y, d->z, d->w}); }
uint32_t CT_ CV_AdjustToPathHeight(void *self, S_vector *pos)
{
    Log(self, "CtlAdjustToPathHeight", {pos->x, pos->y, pos->z});
    pos->y += 0.25f;
    return g_ctl.answer;
}
uint32_t CT_ CV_Unknown5C(void *self, float *intensity, float *reference)
{
    Log(self, "CtlUnknown5C", {double(intensity != nullptr), double(reference != nullptr)});
    *intensity = 0.75f;
    *reference = 0.5f;
    return g_ctl.answer;
}
uint32_t CT_ CV_IsSpectating(void *self)
{
    Log(self, "CtlIsSpectating");
    return g_ctl.answer;
}
void CT_ CV_SetUnconscious(void *self) { Log(self, "CtlSetUnconscious"); }
void CT_ CV_OnMessage(void *self, void *msg) { Log(self, ("CtlOnMessage " + NameOf(msg)).c_str()); }
void CT_ CV_OnMessage2(void *self, void *msg) { Log(self, ("CtlOnMessage2 " + NameOf(msg)).c_str()); }
void CT_ CV_SetAIMode(void *self, int mode) { Log(self, "CtlSetAIMode", {double(mode)}); }
void CT_ CV_SetMoveFlags(void *self, uint32_t flags) { Log(self, "CtlSetMoveFlags", {double(flags)}); }
void CT_ CV_IssueCommand(void *self, const S_HumanCommand *c)
{
    Log(self, "CtlIssueCommand", {double(c->category), double(c->type), double(c->param1), double(c->param2)});
}
float CT_ CV_GetAimBonus(void *self)
{
    Log(self, "CtlGetAimBonus");
    return g_ctl.aimBonus;
}
void CT_ CV_SendRemoteRequest(void *self, int request) { Log(self, "CtlSendRemoteRequest", {double(request)}); }
void CT_ CV_FlushMessages(void *self, int toClients) { Log(self, "CtlFlushMessages", {double(B(toClients))}); }
uint32_t CT_ CV_IsSurrendered(void *self)
{
    Log(self, "CtlIsSurrendered");
    return g_ctl.answer;
}
uint32_t CT_ CV_Unknown104(void *self)
{
    Log(self, "CtlUnknown104");
    return g_ctl.answer;
}
// The squad controller's.
void CT_ CV_AddOrder(void *self, void *order) { Log(self, ("SquadAddOrder " + NameOf(order)).c_str()); }
void CT_ CV_InsertOrder(void *self, void *order, int index)
{
    Log(self, ("SquadInsertOrder " + NameOf(order)).c_str(), {double(index)});
}
void CT_ CV_SetOrder(void *self, int index, void *order) { Log(self, ("SquadSetOrder " + NameOf(order)).c_str(), {double(index)}); }
void CT_ CV_RemoveOrder(void *self, int index, int turn) { Log(self, "SquadRemoveOrder", {double(index), double(B(turn))}); }
uint32_t CT_ CV_HaveOrdersChanged(void *self)
{
    Log(self, "SquadHaveOrdersChanged");
    return g_ctl.answer;
}
int CT_ CV_GetOrderCount(void *self)
{
    Log(self, "SquadGetOrderCount");
    return g_ctl.count;
}
alignas(16) uint8_t g_orderBytes[0x40];
void *CT_ CV_GetOrder(void *self, int index)
{
    Log(self, "SquadGetOrder", {double(index)});
    return g_orderBytes;
}
int CT_ CV_GetStatusIcon(void *self)
{
    Log(self, "SquadGetStatusIcon");
    return g_ctl.count + 1;
}
int CT_ CV_GetModeIcon(void *self)
{
    Log(self, "SquadGetModeIcon");
    return g_ctl.count + 2;
}

// Each side's controller objects for the wrappers (the tested human's and the other's), their vtables.
alignas(16) uint8_t g_cPort[0x800], g_cOrig[0x800], g_cPort2[0x800], g_cOrig2[0x800];
void *g_cPortVt[200], *g_cOrigVt[200];
C_PlayerController *PortPlayer(uint8_t *bytes) { return reinterpret_cast<C_PlayerController *>(bytes); }
C_ScriptedController *PortScripted(uint8_t *bytes) { return reinterpret_cast<C_ScriptedController *>(bytes); }

// (The definition's protected component through a derived accessor.)
struct S_CompAccess : C_ItemDef {
    static void Set(void *def, void *comp)
    {
        static_cast<S_CompAccess *>(static_cast<C_ItemDef *>(def))->m_component = static_cast<C_ItemComponent *>(comp);
    }
};

// The human's own OnMessage / Unknown20 (HandleMessage calls them through its vtable): the real ones on both sides.
void CT_ PH_OnMessage(C_Human *self, C_BitStream *msg) { self->C_Human::OnMessage(msg); }
void CT_ PH_Unknown20(C_Human *self, C_BitStream *msg) { self->C_Human::Unknown20(msg); }

template <class F> void SetSlot(void **portVt, void **origVt, F member, size_t origOffset, const void *fake)
{
    portVt[VSlot(member)] = const_cast<void *>(fake);
    origVt[origOffset / 4] = const_cast<void *>(fake);
}
#define FAKE(f) reinterpret_cast<const void *>(f)

void SetUpControl()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        // constructors
        {0x007e53c2, FAKE(OC_AlignedNew)},
        {0x007e559c, FAKE(OC_AlignedDelete)},
        {0x00582ff0, FAKE(OC_ControllerCtor)},
        {0x0056ee90, FAKE(OC_ControllerCtor)},
        {0x00524c60, FAKE(OC_SquadCreate)},
        // the player's, the AI's and the scripted controllers' members
        {0x0053bf90, FAKE(OC_HandleInput)},
        {0x005418e0, FAKE(OC_HandleMessage)},
        {0x00541cb0, FAKE(OC_HandleMessage2)},
        {0x00547270, FAKE(OC_RequestDamage)},
        {0x0053f1b0, FAKE(OC_RequestEquipment)},
        {0x0053d910, FAKE(OC_SendActionCommand)},
        {0x005469e0, FAKE(OC_SetViewMode)},
        {0x0057a390, FAKE(OC_GetPathPoints)},
        {0x00570a10, FAKE(OC_LookAround)},
        {0x0058f540, FAKE(OS_ActivityTurk)},
        {0x0058f5d0, FAKE(OS_DisableEvents)},
        {0x0058f790, FAKE(OS_DropObject)},
        {0x0058f5f0, FAKE(OS_EnableEvents)},
        {0x0058f880, FAKE(OS_HolsterWeapon)},
        {0x0058f8c0, FAKE(OS_Reload)},
        {0x0058f680, FAKE(OS_ResetAI)},
        {0x0058f370, FAKE(OS_SetMoveModeCrouch)},
        {0x0058f330, FAKE(OS_SetMoveModeGuard)},
        {0x0058f350, FAKE(OS_SetMoveModeLie)},
        {0x0058f2d0, FAKE(OS_SetMoveModeRun)},
        {0x0058f310, FAKE(OS_SetMoveModeSilence)},
        {0x0058f390, FAKE(OS_SetMoveModeStand)},
        {0x0058f420, FAKE(OS_SetMoveModeStandFast)},
        {0x0058f2f0, FAKE(OS_SetMoveModeWalk)},
        {0x00590700, FAKE(OS_StartScript)},
        {0x0058f640, FAKE(OS_StopAction)},
        {0x0058f830, FAKE(OS_TakeWeapon)},
        {0x00590750, FAKE(OS_LookAround1)},
        {0x00590760, FAKE(OS_MoveToAlarm)},
        {0x0058fa10, FAKE(OS_PlaySpeech)},
        {0x0058f610, FAKE(OS_SetMode)},
        {0x0058fcc0, FAKE(OS_SetOptimized)},
        {0x0058e880, FAKE(OS_AdvanceScript)},
        {0x0058f1f0, FAKE(OS_AttackActor)},
        {0x0058f260, FAKE(OS_AttackFrame)},
        {0x00591470, FAKE(OS_AttackMoveTo)},
        {0x0058e780, FAKE(OS_MoveTo)},
        {0x0058e8a0, FAKE(OS_FindCheckpoint)},
        {0x0058f630, FAKE(OS_HasAIMode)},
        {0x0058f700, FAKE(OS_PickObject)},
        {0x0058f8d0, FAKE(OS_PlayActivity)},
        {0x0058f970, FAKE(OS_PlayActivityAt)},
        {0x0058fcf0, FAKE(OS_SeesEnemy)},
        {0x0058e710, FAKE(OS_SetAlarm)},
        {0x0058e650, FAKE(OS_SetAlarmType)},
        {0x00591230, FAKE(OS_SetAnimation)},
        {0x0058e850, FAKE(OS_SetWaitForPlayer)},
        {0x0058f4b0, FAKE(OS_Sit)},
        {0x005904c0, FAKE(OS_TurnAt)},
        {0x0058c330, FAKE(OS_RaiseWeapon)},
        {0x0058c400, FAKE(OS_LowerWeapon)},
        // the body, the events, the movement, the car, the network game, the commander, the hands weapon
        {0x005abd90, FAKE(OB_Execute)},
        {0x005ac230, FAKE(OB_GetSpeed)},
        {0x005ac210, FAKE(OB_GetStance)},
        {0x005ac220, FAKE(OB_GetWorkState)},
        {0x005ac410, FAKE(OB_IsLeftFootForward)},
        {0x005ac4a0, FAKE(OB_PlayAnimation)},
        {0x005ac110, FAKE(OB_SetCarryMode)},
        {0x005ac070, FAKE(OB_SetPlayerControlled)},
        {0x005ae610, FAKE(OE_SetSniper)},
        {0x00786ab0, FAKE(OM_SetUnknown94)},
        {0x004dd090, FAKE(OV_GetEnvironmentFlags)},
        {0x0072efa0, FAKE(ON_GetPlayerName)},
        {0x004563e0, FAKE(OS_GetSquadCommander)},
        {0x005a2b10, FAKE(OH_GetHandsWeapon)},
        // the formations
        {0x005949c0, FAKE(OF_Ctor)},
        {0x00594a70, FAKE(OF_Dtor)},
        {0x00597f80, FAKE(OF_AddMember)},
        {0x005981c0, FAKE(OF_RemoveMember)},
        {0x00597c50, FAKE(OF_SetLeader)},
        {0x00597420, FAKE(OF_SetType)},
        {0x005977f0, FAKE(OF_SetScale)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // The functions tested: the real ones on both sides (earlier parts redirected some).
    for (uint32_t at : {0x005a3f20u, 0x005a3e60u, 0x005a3df0u, 0x005af680u, 0x005a80e0u, 0x005a80d0u, 0x005a5c70u,
                        0x005a5c80u, 0x005a5d10u, 0x005a6380u, 0x005a6450u, 0x005a5c90u, 0x005a66f0u, 0x005a6710u,
                        0x005a9680u, 0x005a9bf0u, 0x005a6430u, 0x005a81b0u, 0x005a82c0u, 0x005a8320u, 0x005a91a0u,
                        0x005a9120u})
        RestoreOriginal(at);

    // The derived controllers' vtables: the port's in their symbols, the original's own (their Attach, Detach and
    // deleting destructor logged).
    for (void **vt : {g_vtHumanController, g_vtAIController, g_vtPlayer, g_vtEnemy, g_vtScripted, g_vtNeutral,
                      g_vtRemote, g_vtSniper}) {
        for (int i = 0; i < 200; ++i)
            vt[i] = reinterpret_cast<void *>(CT_Unexpected);
        vt[0] = vt[1] = nullptr;
        void **slots = vt + 2;
        slots[VSlot(&C_HumanController::Attach)] = reinterpret_cast<void *>(CC_Attach);
        slots[VSlot(&C_HumanController::Detach)] = reinterpret_cast<void *>(CC_Detach);
        slots[6] = slots[7] = reinterpret_cast<void *>(CC_DeletePort);
    }
    for (void *&slot : g_portSquadVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    for (void *&slot : g_origSquadVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_portSquadVt[VSlot(&C_HumanController::Attach)] = reinterpret_cast<void *>(CC_Attach);
    g_portSquadVt[VSlot(&C_HumanController::Detach)] = reinterpret_cast<void *>(CC_Detach);
    g_portSquadVt[6] = g_portSquadVt[7] = reinterpret_cast<void *>(CC_DeletePort);
    g_portSquadVt[VSlot(&C_SquadController::CancelOrders)] = reinterpret_cast<void *>(CC_CancelOrders);
    g_origSquadVt[0x1C / 4] = reinterpret_cast<void *>(CC_Attach);
    g_origSquadVt[0x20 / 4] = reinterpret_cast<void *>(CC_Detach);
    g_origSquadVt[0x18 / 4] = reinterpret_cast<void *>(CC_DeleteOrig);
    g_origSquadVt[0x128 / 4] = reinterpret_cast<void *>(CC_CancelOrders);
    for (uint32_t vt : {0x00811848u, 0x00811730u, 0x00811618u, 0x00811500u, 0x008113e8u, 0x008112d8u}) {
        void **slots = OriginalAt<void **>(vt);
        slots[0x1C / 4] = reinterpret_cast<void *>(CC_Attach);
        slots[0x20 / 4] = reinterpret_cast<void *>(CC_Detach);
        slots[0x18 / 4] = reinterpret_cast<void *>(CC_DeleteOrig);
    }

    p_humanVt[VSlot(&C_Actor::OnMessage)] = reinterpret_cast<void *>(PH_OnMessage);
    p_humanVt[VSlot(&C_Actor::Unknown20)] = reinterpret_cast<void *>(PH_Unknown20);
    o_humanVt[0x1C / 4] = OriginalAt<void *>(0x005a5f60);
    o_humanVt[0x20 / 4] = OriginalAt<void *>(0x005a5f70);

    // The wrappers' controllers.
    for (int i = 0; i < 200; ++i)
        g_cPortVt[i] = g_cOrigVt[i] = reinterpret_cast<void *>(CT_Unexpected);
    void **pv = g_cPortVt, **ov = g_cOrigVt;
    SetSlot(pv, ov, &C_HumanController::Attach, 0x1C, FAKE(CC_Attach));
    SetSlot(pv, ov, &C_HumanController::Detach, 0x20, FAKE(CC_Detach));
    pv[6] = pv[7] = reinterpret_cast<void *>(CC_DeletePort);
    ov[0x18 / 4] = reinterpret_cast<void *>(CC_DeleteOrig);
    SetSlot(pv, ov, &C_HumanController::TestSightOfMeleeTracker, 0x34, FAKE(CV_TestSightOfMeleeTracker));
    SetSlot(pv, ov, &C_HumanController::TestSightOfHuman, 0x38, FAKE(CV_TestSightOfHuman));
    SetSlot(pv, ov, &C_HumanController::RequestLeaveVehicle, 0x44, FAKE(CV_RequestLeaveVehicle));
    SetSlot(pv, ov, &C_HumanController::GetFacingDir, 0x48, FAKE(CV_GetFacingDir));
    SetSlot(pv, ov, &C_HumanController::GetViewDir, 0x4C, FAKE(CV_GetViewDir));
    SetSlot(pv, ov, &C_HumanController::SetFacingDir, 0x50, FAKE(CV_SetFacingDir));
    SetSlot(pv, ov, &C_HumanController::SetViewDir, 0x54, FAKE(CV_SetViewDir));
    SetSlot(pv, ov, &C_HumanController::AdjustToPathHeight, 0x58, FAKE(CV_AdjustToPathHeight));
    SetSlot(pv, ov, &C_HumanController::Unknown5C, 0x5C, FAKE(CV_Unknown5C));
    SetSlot(pv, ov, &C_HumanController::IsSpectating, 0x64, FAKE(CV_IsSpectating));
    SetSlot(pv, ov, &C_HumanController::SetUnconscious, 0x90, FAKE(CV_SetUnconscious));
    SetSlot(pv, ov, &C_HumanController::OnMessage, 0xC0, FAKE(CV_OnMessage));
    SetSlot(pv, ov, &C_HumanController::OnMessage2, 0xC4, FAKE(CV_OnMessage2));
    SetSlot(pv, ov, &C_HumanController::SetAIMode, 0xCC, FAKE(CV_SetAIMode));
    SetSlot(pv, ov, &C_HumanController::SetMoveFlags, 0xD0, FAKE(CV_SetMoveFlags));
    SetSlot(pv, ov, &C_HumanController::IssueCommand, 0xD8, FAKE(CV_IssueCommand));
    SetSlot(pv, ov, &C_HumanController::GetAimBonus, 0xEC, FAKE(CV_GetAimBonus));
    SetSlot(pv, ov, &C_HumanController::SendRemoteRequest, 0xF8, FAKE(CV_SendRemoteRequest));
    SetSlot(pv, ov, &C_HumanController::FlushMessages, 0xFC, FAKE(CV_FlushMessages));
    SetSlot(pv, ov, &C_HumanController::IsSurrendered, 0x100, FAKE(CV_IsSurrendered));
    SetSlot(pv, ov, &C_HumanController::Unknown104, 0x104, FAKE(CV_Unknown104));
    SetSlot(pv, ov, &C_SquadController::AddOrder, 0x11C, FAKE(CV_AddOrder));
    SetSlot(pv, ov, &C_SquadController::InsertOrder, 0x120, FAKE(CV_InsertOrder));
    SetSlot(pv, ov, &C_SquadController::SetOrder, 0x124, FAKE(CV_SetOrder));
    SetSlot(pv, ov, &C_SquadController::CancelOrders, 0x128, FAKE(CC_CancelOrders));
    SetSlot(pv, ov, &C_SquadController::RemoveOrder, 0x12C, FAKE(CV_RemoveOrder));
    SetSlot(pv, ov, &C_SquadController::HaveOrdersChanged, 0x130, FAKE(CV_HaveOrdersChanged));
    SetSlot(pv, ov, &C_SquadController::GetOrderCount, 0x134, FAKE(CV_GetOrderCount));
    SetSlot(pv, ov, &C_SquadController::GetOrder, 0x138, FAKE(CV_GetOrder));
    SetSlot(pv, ov, &C_SquadController::GetStatusIcon, 0x13C, FAKE(CV_GetStatusIcon));
    SetSlot(pv, ov, &C_SquadController::GetModeIcon, 0x140, FAKE(CV_GetModeIcon));
    for (uint8_t *b : {g_cPort, g_cPort2})
        *reinterpret_cast<void ***>(b) = g_cPortVt;
    for (uint8_t *b : {g_cOrig, g_cOrig2})
        *reinterpret_cast<void ***>(b) = g_cOrigVt;
    g_names[g_cPort] = g_names[g_cOrig] = "ctl";
    g_names[g_cPort2] = g_names[g_cOrig2] = "ctl2";
    g_names[g_orderBytes] = "order";
    g_extraPointers.push_back({g_cPort, g_cOrig});
    g_extraPointers.push_back({g_cPort2, g_cOrig2});
    CHECK(sizeof(C_PlayerController) <= sizeof g_cPort && sizeof(C_ScriptedController) <= sizeof g_cPort);

    // The squad commander, the hands weapon (its definition and component).
    for (int i = 0; i < 64; ++i) {
        g_portCommanderVt[i] = g_origCommanderVt[i] = g_portDefVt[i] = g_origDefVt[i] = g_compVt[i] =
            reinterpret_cast<void *>(CT_Unexpected);
    }
    g_portCommanderVt[VSlot(&C_SquadCommander::OnPlayerNoise)] = reinterpret_cast<void *>(CM_OnPlayerNoise);
    g_origCommanderVt[0x24 / 4] = reinterpret_cast<void *>(CM_OnPlayerNoise);
    *reinterpret_cast<void ***>(g_portCommander) = g_portCommanderVt;
    *reinterpret_cast<void ***>(g_origCommander) = g_origCommanderVt;
    g_names[g_portCommander] = g_names[g_origCommander] = "commander";
    g_portDefVt[VSlot(&C_ItemDef::GetCarryMode)] = reinterpret_cast<void *>(WD_GetCarryMode);
    g_origDefVt[0x0C / 4] = reinterpret_cast<void *>(WD_GetCarryMode);
    g_compVt[VSlot(&C_WeaponComp::GetAimedDispersion)] = reinterpret_cast<void *>(WC_GetAimedDispersion);
    g_compVt[0x10 / 4] = reinterpret_cast<void *>(WC_GetAimedDispersion);
    *reinterpret_cast<void ***>(g_portWeaponDef) = g_portDefVt;
    *reinterpret_cast<void ***>(g_origWeaponDef) = g_origDefVt;
    *reinterpret_cast<void ***>(g_weaponComp) = g_compVt;
    S_ItemAccess::SetDef(g_portWeapon, g_portWeaponDef);
    void *origDef = g_origWeaponDef, *comp = g_weaponComp;
    std::memcpy(g_origWeapon + 0x04, &origDef, 4);
    S_CompAccess::Set(g_portWeaponDef, g_weaponComp);
    std::memcpy(g_origWeaponDef + 0x48, &comp, 4);
    g_names[g_portWeapon] = g_names[g_origWeapon] = "weapon";
    g_names[g_portWeaponDef] = g_names[g_origWeaponDef] = "weaponDef";
    g_names[g_weaponComp] = "weaponComp";
    *OriginalAt<S_vector *>(0x0089ae50) = g_defaultFormationScale;
}

// ---- part 22a: the controller made

// The controller's type after the call (-1: none).
int PortControllerType() { return g_h.human->m_controller ? int(g_h.human->m_controller->m_type) : -1; }
int OrigControllerType()
{
    const uint8_t *c = *reinterpret_cast<uint8_t *const *>(o_human + 0xB0);
    return c ? *reinterpret_cast<const int *>(c + 0x48) : -1;
}

// The controller the human has: the wrappers' one of a given type, or none.
const E_HumanController CONTROLLER_TYPES[] = {HUMAN_CONTROLLER_PLAYER, HUMAN_CONTROLLER_SQUAD, HUMAN_CONTROLLER_ENEMY,
                                              HUMAN_CONTROLLER_AI, HUMAN_CONTROLLER_NEUTRAL, HUMAN_CONTROLLER_REMOTE,
                                              HUMAN_CONTROLLER_SNIPER};
void SetControllerType(int type)
{
    reinterpret_cast<C_HumanController *>(g_cPort)->m_type = static_cast<E_HumanController>(type);
    std::memcpy(g_cOrig + 0x48, &type, 4);
}

void RandomControl()
{
    RandomSelf();
    C_Human *h = g_h.human;
    h->m_controller = reinterpret_cast<C_HumanController *>(g_cPort);
    SetControllerType(CONTROLLER_TYPES[I(0, 6)]);
    g_localPlayerSlot = P(0.5f) ? h->m_netId : h->m_netId + 1;
    g_script.activeIsOwner = P(0.5f);
}

typedef void(CT_ *T_CInt)(void *, int);
typedef void(CT_ *T_CVoid)(void *);

void TestControlCreate()
{
    C_Human *h = g_h.human;
    std::printf("control create\n");
    g_skipFields.insert("m_controller");
    for (int i = 0; i < 16000 / QUICK; ++i) {
        RandomControl();
        if (P(0.15f))
            h->m_controller = nullptr;
        switch (i % 4) {
        case 0: {
            const int type = CONTROLLER_TYPES[I(0, 6)];
            DiffCall("CreateController", [&] { h->CreateController(E_HumanController(type)); return PortControllerType(); },
                     [&](void *) {
                         OriginalAt<T_CInt>(0x005a3f20)(o_human, type);
                         return OrigControllerType();
                     });
            break;
        }
        case 1: {
            if (!h->m_controller)
                break;
            const bool active = P(0.5f);
            DiffCall("AssignPlayerController", [&] { h->AssignPlayerController(active); return PortControllerType(); },
                     [&](void *) {
                         OriginalAt<T_CInt>(0x005a3e60)(o_human, active);
                         return OrigControllerType();
                     });
            break;
        }
        case 2: {
            const bool on = P(0.5f);
            DiffCall("SetScripted", [&] { h->SetScripted(on); return PortControllerType(); },
                     [&](void *) {
                         OriginalAt<T_CInt>(0x005a3df0)(o_human, on);
                         return OrigControllerType();
                     });
            break;
        }
        default:
            DiffCall("StartScriptedController", [&] { h->StartScriptedController(); return PortControllerType(); },
                     [&](void *) {
                         OriginalAt<T_CVoid>(0x005af680)(o_human);
                         return OrigControllerType();
                     });
            break;
        }
    }
    g_skipFields.erase("m_controller");
}

// The wrappers: the controller's state in both layouts (its own fields, and the player's or the scripted controller's
// by its type), the answers of its virtuals and of the modules.
alignas(16) uint8_t g_msgBytes[0x40], g_trackerBytes[0x40];
void RandomWrappers()
{
    RandomOther();
    RandomControl();
    C_Human *h = g_h.human;
    const int type = reinterpret_cast<C_HumanController *>(g_cPort)->m_type;
    for (uint8_t *b : {g_cPort, g_cOrig})
        std::memset(b + 4, 0, sizeof g_cPort - 4);
    SetControllerType(type);
    auto *pc = reinterpret_cast<C_HumanController *>(g_cPort);
    const auto put = [&](uint32_t origOffset, const void *value, size_t size) {
        std::memcpy(g_cOrig + origOffset, value, size);
    };
    pc->m_moveFlags = uint32_t(I(0, 0xFFFF)) | (P(0.3f) ? uint32_t(MOVE_UNCONSCIOUS) : 0u) |
                      (P(0.3f) ? uint32_t(MOVE_SWIMMING) : 0u) | (P(0.2f) ? uint32_t(I(0, 0xFF)) << 24 : 0u);
    if (P(0.2f))
        pc->m_moveFlags &= ~uint32_t(MOVE_MOVING_MASK);
    put(0x50, &pc->m_moveFlags, 4);
    pc->m_stateFlags = uint32_t(I(0, 0x1F)) << 12;
    put(0x5C, &pc->m_stateFlags, 4);
    pc->m_interaction = P(0.5f) ? -1 : I(0, 0x26);
    put(0x60, &pc->m_interaction, 4);
    pc->m_weaponRaised = P(0.5f);
    put(0xAD, &pc->m_weaponRaised, 1);
    pc->m_quietTime = P(0.3f) ? 1000 + I(-1, 1) : I(0, 5000);
    put(0xB0, &pc->m_quietTime, 4);
    if (type == HUMAN_CONTROLLER_PLAYER) {
        C_PlayerController *pl = PortPlayer(g_cPort);
        pl->m_holdingBreath = P(0.5f);
        put(0x4D4, &pl->m_holdingBreath, 1);
        pl->m_ordersChanged = P(0.5f);
        put(0x525, &pl->m_ordersChanged, 1);
        pl->m_viewMode = I(0, 5);
        put(0x52C, &pl->m_viewMode, 4);
    } else if (type == HUMAN_CONTROLLER_AI) {
        C_ScriptedController *sc = PortScripted(g_cPort);
        sc->m_suspended = P(0.5f);
        put(0x1D2, &sc->m_suspended, 1);
        sc->m_alarmType = uint32_t(I(0, 15));
        put(0x1B4, &sc->m_alarmType, 4);
    }
    // The other human's controller (HandleMessage's human).
    const int otherType = CONTROLLER_TYPES[I(0, 6)];
    reinterpret_cast<C_HumanController *>(g_cPort2)->m_type = static_cast<E_HumanController>(otherType);
    std::memcpy(g_cOrig2 + 0x48, &otherType, 4);
    void *portCtl2 = P(0.2f) ? nullptr : g_cPort2, *origCtl2 = portCtl2 ? g_cOrig2 : nullptr;
    PortOther()->m_controller = static_cast<C_HumanController *>(portCtl2);
    std::memcpy(g_origOther + 0xB0, &origCtl2, 4);
    // The answers.
    g_ctl.answer = P(0.5f);
    g_ctl.count = I(-1, 9);
    g_ctl.aimBonus = P(0.2f) ? 0.0f : U(-5, 5);
    g_ctl.dispersion = P(0.2f) ? 0.0f : U(0, 300);
    g_ctl.carryMode = P(0.3f) ? ITEM_CARRY_EQUIPMENT : I(0, 8);
    g_ctl.weapon = P(0.8f);
    g_ctl.dir = S_vector(U(-1, 1), U(-1, 1), U(-1, 1));
    g_ctl.bodySpeed = U(0, 8);
    g_ctl.bodyStance = I(0, 4);
    g_ctl.bodyWorkState = P(0.4f) ? 5 : P(0.3f) ? 2 : I(0, 7);
    g_ctl.pathCurrent = uint16_t(I(0, 0xFFFF));
    g_ctl.pathNext = uint16_t(I(0, 0xFFFF));
    // The human's state the wrappers read.
    h->m_inPlay = P(0.5f);
    h->m_stats[STAT_HEALTH] = P(0.2f) ? I(-5, 0) : I(1, 100);
    if (P(0.3f))
        h->m_role = HUMAN_ROLE_PLAYER;
    if (P(0.3f))
        h->m_role = HUMAN_ROLE_ENEMY;
    h->m_vehicle = P(0.3f) ? reinterpret_cast<C_Actor *>(g_vehicleBytes) : nullptr;
    h->m_seat = I(0, 5);
    h->m_sightId = P(0.3f) ? -1 : I(0, 40);
    h->m_sightTargetId = P(0.3f) ? -1 : I(0, 40);
    h->m_soundListener = P(0.3f) ? -1 : I(0, 40);
    h->m_periodicLink = P(0.3f) ? -1 : I(0, 40);
    // No controller (the wrappers that test for one).
    if (P(0.1f))
        h->m_controller = nullptr;
}

template <class R, class... A> R OH(uint32_t at, A... a)
{
    using F = R(CT_ *)(void *, A...);
    return OriginalAt<F>(at)(static_cast<void *>(o_human), a...);
}
int OB(uint32_t at) { return int(OH<uint32_t>(at) & 0xFF); }
typedef void(__attribute__((fastcall)) * T_Fast2)(void *, void *);

void TestControlWrappers()
{
    C_Human *h = g_h.human;
    std::printf("control wrappers\n");
    g_names[g_msgBytes] = "msg";
    g_names[g_trackerBytes] = "meleeTracker";
    C_BitStream *msg = reinterpret_cast<C_BitStream *>(g_msgBytes);
    for (int i = 0; i < 60000 / QUICK; ++i) {
        RandomWrappers();
        const bool hasController = h->m_controller != nullptr;
        const int type = reinterpret_cast<C_HumanController *>(g_cPort)->m_type;
        const int which = I(0, 60);
        // (The original reads a missing controller in most of them: those run only with one.)
        if (!hasController && which != 16 && which != 17 && which != 18 && which != 19 && which != 20 && which != 23 &&
            which != 24 && which != 33 && which != 34 && which != 36 && which != 37 && which != 55 && which != 56)
            continue;
        const int a = I(-2, 40), b = I(-2, 40), c = I(-2, 40), d = I(-2, 40);
        switch (which) {
        case 0:
            DiffCall("GetControllerType", [&] { return h->GetControllerType(); }, [&](void *) { return OH<int>(0x005a5fe0); });
            break;
        case 1: {
            S_HumanCommand command;
            command.category = I(0, 3);
            command.type = I(0, 0x26);
            command.param1 = a;
            command.param2 = b;
            DiffCall("IssueCommand", [&] { h->IssueCommand(command); return 0; },
                     [&](void *) { OH<void>(0x005a45b0, &command); return 0; });
            break;
        }
        case 2:
            DiffCall("HasOrder", [&] { return int(h->HasOrder()); }, [&](void *) { return OB(0x005a45c0); });
            break;
        case 3:
            DiffCall("GetFacingDir", [&] { return VectorHash(h->GetFacingDir()); },
                     [&](void *) { return VectorHash(*OH<const S_vector *>(0x005a80d0)); });
            break;
        case 4:
            DiffCall("GetViewDir", [&] { return VectorHash(h->GetViewDir()); },
                     [&](void *) { return VectorHash(*OH<const S_vector *>(0x005a80e0)); });
            break;
        case 5:
        case 6: {
            const S_vector4 v = {U(-1, 1), U(-1, 1), U(-1, 1), U(-1, 1)};
            if (which == 5)
                DiffCall("SetFacingDir", [&] { h->SetFacingDir(v); return 0; },
                         [&](void *) { OH<void>(0x005a84d0, &v); return 0; });
            else
                DiffCall("SetViewDir", [&] { h->SetViewDir(v); return 0; }, [&](void *) { OH<void>(0x005a84e0, &v); return 0; });
            break;
        }
        case 7:
            DiffCall("GetAccuracy", [&] { return FloatBits(h->GetAccuracy()); },
                     [&](void *) { return FloatBits(OH<float>(0x005a8100)); });
            break;
        case 8:
            DiffCall("GetMoveMode", [&] { return int(h->GetMoveMode()); }, [&](void *) { return OH<int>(0x005a5c70); });
            break;
        case 9:
            DiffCall("GetStateFlags", [&] { return int(h->GetStateFlags()); }, [&](void *) { return OH<int>(0x005a5c80); });
            break;
        case 10:
            DiffCall("GetAIModeFlags", [&] { return int(h->GetAIModeFlags()); }, [&](void *) { return OH<int>(0x005a5cb0); });
            break;
        case 11:
            DiffCall("GetStanceFlags", [&] { return int(h->GetStanceFlags()); }, [&](void *) { return OH<int>(0x005a5d10); });
            break;
        case 12:
            DiffCall("GetMoveSpeedClass", [&] { return h->GetMoveSpeedClass(); }, [&](void *) { return OH<int>(0x005a5cc0); });
            break;
        case 13:
            DiffCall("GetStance", [&] { return h->GetStance(); }, [&](void *) { return OH<int>(0x005a5d20); });
            break;
        case 14:
            DiffCall("SetControllerAIMode", [&] { h->SetControllerAIMode(a * 64); return 0; },
                     [&](void *) { OH<void>(0x005a5ca0, a * 64); return 0; });
            break;
        case 15:
            DiffCall("MakeNoise", [&] { h->MakeNoise(); return reinterpret_cast<C_HumanController *>(g_cPort)->m_quietTime; },
                     [&](void *) {
                         OH<void>(0x005a6480);
                         return *reinterpret_cast<const int *>(g_cOrig + 0xB0);
                     });
            break;
        case 16:
            DiffCall("IsUnconscious", [&] { return int(h->IsUnconscious()); }, [&](void *) { return OB(0x005a6380); });
            break;
        case 17:
            DiffCall("IsInPlay", [&] { return int(h->IsInPlay()); }, [&](void *) { return OB(0x005a63b0); });
            break;
        case 18:
            DiffCall("DetachFromWorld", [&] { h->DetachFromWorld(); return 0; }, [&](void *) { OH<void>(0x005a7fe0); return 0; });
            break;
        case 19:
            DiffCall("IsSpectating", [&] { return int(h->IsSpectating()); }, [&](void *) { return OB(0x005a63f0); });
            break;
        case 20:
            DiffCall("IsRemote", [&] { return int(h->IsRemote()); }, [&](void *) { return OB(0x005a08d0); });
            break;
        case 21:
            DiffCall("GetMoveSpeed", [&] { return FloatBits(h->GetMoveSpeed()); },
                     [&](void *) { return FloatBits(OH<float>(0x005a5c50)); });
            break;
        case 22:
            DiffCall("IsInWater", [&] { return int(h->IsInWater()); }, [&](void *) { return OB(0x005a6420); });
            break;
        case 23:
            DiffCall("IsSurrendered", [&] { return int(h->IsSurrendered()); }, [&](void *) { return OB(0x005a6450); });
            break;
        case 24:
            DiffCall("IsAISuspended", [&] { return int(h->IsAISuspended()); }, [&](void *) { return OB(0x005a5d30); });
            break;
        case 25:
            DiffCall("OnMessage", [&] { h->C_Human::OnMessage(msg); return 0; },
                     [&](void *) { OH<void>(0x005a5f60, static_cast<void *>(msg)); return 0; });
            break;
        case 26:
            DiffCall("Unknown20", [&] { h->C_Human::Unknown20(msg); return 0; },
                     [&](void *) { OH<void>(0x005a5f70, static_cast<void *>(msg)); return 0; });
            break;
        case 27:
        case 28: {
            // The human: the tested one, the other one (its controller or none), none.
            const int who = I(0, 2);
            C_Human *portHuman = who == 0 ? h : who == 1 ? PortOther() : nullptr;
            void *origHuman = who == 0 ? static_cast<void *>(o_human) : who == 1 ? static_cast<void *>(g_origOther) : nullptr;
            if (which == 27)
                DiffCall("HandleMessage", [&] { C_Human::HandleMessage(portHuman, msg); return 0; },
                         [&](void *) { OriginalAt<T_Fast2>(0x005a5fb0)(origHuman, msg); return 0; });
            else
                DiffCall("HandleMessage2", [&] { C_Human::HandleMessage2(portHuman, msg); return 0; },
                         [&](void *) { OriginalAt<T_Fast2>(0x005a5f80)(origHuman, msg); return 0; });
            break;
        }
        case 29:
        case 30: {
            const int status0 = I(0, 600);
            const float vis0 = U(0, 100);
            const auto run = [&](bool original) {
                int status = status0;
                float vis = vis0;
                if (which == 29) {
                    if (original)
                        OH<void>(0x005a59a0, static_cast<void *>(g_trackerBytes), &status, &vis);
                    else
                        h->TestSightOfMeleeTracker(reinterpret_cast<C_Actor *>(g_trackerBytes), &status, &vis);
                } else {
                    if (original)
                        OH<void>(0x005a59d0, static_cast<void *>(g_origOther), &status, &vis);
                    else
                        h->TestSightOfHuman(PortOther(), &status, &vis);
                }
                return status * 7 + FloatBits(vis);
            };
            DiffCall(which == 29 ? "TestSightOfMeleeTracker" : "TestSightOfHuman", [&] { return run(false); },
                     [&](void *) { return run(true); });
            break;
        }
        case 31: {
            const uint32_t flags = uint32_t(I(0, 0x7FFFFFFF));
            DiffCall("SetMoveFlags", [&] { h->SetMoveFlags(flags); return 0; },
                     [&](void *) { OH<void>(0x005a5c90, flags); return 0; });
            break;
        }
        case 32:
            DiffCall("SetUnconscious", [&] { h->SetUnconscious(PortOther()); return 0; },
                     [&](void *) { OH<void>(0x005a6360, static_cast<void *>(g_origOther)); return 0; });
            break;
        case 33: {
            const S_vector pos0(U(-5, 5), U(-5, 5), U(-5, 5));
            const auto run = [&](bool original) {
                S_vector pos = pos0;
                const int r = original ? int(OH<uint32_t>(0x005a66f0, &pos) & 0xFF)
                                       : int(h->AdjustToPathHeight(&pos));
                return r + VectorHash(pos) * 2;
            };
            DiffCall("AdjustToPathHeight", [&] { return run(false); }, [&](void *) { return run(true); });
            break;
        }
        case 34: {
            const auto run = [&](bool original) {
                float intensity = 0.125f, reference = 0.25f;
                const int r = original ? int(OH<uint32_t>(0x005a6710, &intensity, &reference) & 0xFF)
                                       : int(h->ControllerUnknown5C(&intensity, &reference));
                return r + FloatBits(intensity) * 2 + FloatBits(reference) * 3;
            };
            DiffCall("ControllerUnknown5C", [&] { return run(false); }, [&](void *) { return run(true); });
            break;
        }
        case 35:
            DiffCall("RequestLeaveVehicle", [&] { h->RequestLeaveVehicle(); return 0; },
                     [&](void *) { OH<void>(0x005a97b0); return 0; });
            break;
        case 36:
            DiffCall("SendRemoteRequest", [&] { h->SendRemoteRequest(a); return 0; },
                     [&](void *) { OH<void>(0x005a9680, a); return 0; });
            break;
        case 37:
            DiffCall("ControllerUnknown104", [&] { return int(h->ControllerUnknown104()); },
                     [&](void *) { return OB(0x005aa7b0); });
            break;
        case 38:
            DiffCall("GetWeaponStance", [&] { return h->GetWeaponStance(); }, [&](void *) { return OH<int>(0x005a80f0); });
            break;
        case 39: {
            const auto run = [&](bool original) {
                uint16_t current = 0x1111, next = 0x2222;
                if (original)
                    OH<void>(0x005a85d0, &current, &next);
                else
                    h->GetAIPathPoints(&current, &next);
                return int(current) << 16 | next;
            };
            DiffCall("GetAIPathPoints", [&] { return run(false); }, [&](void *) { return run(true); });
            break;
        }
        case 40: {
            float held[64] = {U(0, 1)}, pressed[64] = {};
            DiffCall("HandlePlayerInput", [&] { h->HandlePlayerInput(held, pressed); return 0; },
                     [&](void *) { OH<void>(0x005a3de0, held, pressed); return 0; });
            break;
        }
        case 41:
            DiffCall("PlayerRequestEquipment", [&] { h->PlayerRequestEquipment(a); return 0; },
                     [&](void *) { OH<void>(0x005a45a0, a); return 0; });
            break;
        case 42: {
            S_ActionCommand command;
            command.category = a;
            command.unknown08 = b;
            command.unknown0C = c;
            command.dir = {U(-1, 1), U(-1, 1), U(-1, 1), U(-1, 1)};
            command.action = d;
            command.command = I(0, 0x26);
            command.actor = P(0.5f) ? reinterpret_cast<C_Actor *>(g_vehicleBytes) : nullptr;
            command.value = I(-1, 9);
            command.member = P(0.5f) ? h : nullptr;
            S_ActionCommand origCommand = command;
            origCommand.member = command.member ? reinterpret_cast<C_Human *>(o_human) : nullptr;
            DiffCall("PlayerSendActionCommand", [&] { h->PlayerSendActionCommand(command); return 0; },
                     [&](void *) { OH<void>(0x005a45e0, &origCommand); return 0; });
            break;
        }
        case 43:
            DiffCall("PlayerSetViewMode", [&] { h->PlayerSetViewMode(a); return 0; },
                     [&](void *) { OH<void>(0x005a8190, a); return 0; });
            break;
        case 44:
            DiffCall("PlayerRequestDamage", [&] { h->PlayerRequestDamage(a, b, c, d); return 0; },
                     [&](void *) { OH<void>(0x005a9bf0, a, b, c, d); return 0; });
            break;
        case 45:
            DiffCall("IsPlayerHoldingBreath", [&] { return int(h->IsPlayerHoldingBreath()); },
                     [&](void *) { return OB(0x005a6430); });
            break;
        case 46:
            // (The local player's view mode: its controller is the player's.)
            if (type != HUMAN_CONTROLLER_PLAYER)
                break;
            DiffCall("GetPlayerViewMode", [&] { return h->GetPlayerViewMode(); }, [&](void *) { return OH<int>(0x005a8170); });
            break;
        case 47: {
            S_HumanOrder *order = reinterpret_cast<S_HumanOrder *>(g_orderBytes);
            DiffCall("AddOrder", [&] { h->AddOrder(order); return 0; },
                     [&](void *) { OH<void>(0x005a5f30, static_cast<void *>(order)); return 0; });
            break;
        }
        case 48: {
            S_HumanOrder *order = reinterpret_cast<S_HumanOrder *>(g_orderBytes);
            DiffCall("InsertOrder", [&] { h->InsertOrder(order, a); return 0; },
                     [&](void *) { OH<void>(0x005a5f20, static_cast<void *>(order), a); return 0; });
            break;
        }
        case 49: {
            S_HumanOrder *order = reinterpret_cast<S_HumanOrder *>(g_orderBytes);
            DiffCall("SetOrder", [&] { h->SetOrder(a, order); return 0; },
                     [&](void *) { OH<void>(0x005a5f40, a, static_cast<void *>(order)); return 0; });
            break;
        }
        case 50:
            DiffCall("CancelOrders", [&] { h->CancelOrders(); return 0; }, [&](void *) { OH<void>(0x005a5e90); return 0; });
            break;
        case 51:
            DiffCall("RemoveOrder", [&] { h->RemoveOrder(a); return 0; }, [&](void *) { OH<void>(0x005a5eb0, a); return 0; });
            break;
        case 52:
            DiffCall("HaveOrdersChanged", [&] { return int(h->HaveOrdersChanged()); }, [&](void *) { return OB(0x005a5ed0); });
            break;
        case 53:
            DiffCall("GetOrderCount", [&] { return h->GetOrderCount(); }, [&](void *) { return OH<int>(0x005a5f00); });
            break;
        case 54:
            DiffCall("GetOrder", [&] { return int(static_cast<void *>(h->GetOrder(a)) == g_orderBytes); },
                     [&](void *) { return int(OH<void *>(0x005a5f50, a) == g_orderBytes); });
            break;
        case 55:
            DiffCall("GetSquadStatusIcon", [&] { return h->GetSquadStatusIcon(); }, [&](void *) { return OH<int>(0x005a88b0); });
            break;
        case 56:
            DiffCall("GetSquadModeIcon", [&] { return h->GetSquadModeIcon(); }, [&](void *) { return OH<int>(0x005a88d0); });
            break;
        case 57:
            DiffCall("LookAround", [&] { h->LookAround(a); return 0; }, [&](void *) { OH<void>(0x005af6a0, a); return 0; });
            break;
        case 58:
            DiffCall("RaiseWeapon", [&] { h->RaiseWeapon(); return 0; }, [&](void *) { OH<void>(0x005af740); return 0; });
            break;
        case 59:
            DiffCall("LowerWeapon", [&] { h->LowerWeapon(); return 0; }, [&](void *) { OH<void>(0x005af750); return 0; });
            break;
        default:
            DiffCall("IsAISuspended", [&] { return int(h->IsAISuspended()); }, [&](void *) { return OB(0x005a5d30); });
            break;
        }
    }
}

// ---- part 22b: the body, the names, the formations, the scripts' commands

// The network game's players (GetProfileUnknown24's ranks): the port's in its layout, the original's (0xC4 bytes each,
// the stats at +0x40) behind its LockPlayers' vector (first at +4).
// (Raw storage: its constructor is not linked.)
alignas(16) uint8_t g_ctlPlayersBytes[4 * sizeof(S_NetPlayerState)];
S_NetPlayerState *const g_ctlPlayers = reinterpret_cast<S_NetPlayerState *>(g_ctlPlayersBytes);
alignas(16) uint8_t g_origPlayers[4][0xC4];
uint32_t g_origPlayersVector[4];
size_t g_portPlayersOffset = 0;
void *CT_ ON_LockPlayers(void *) { return g_origPlayersVector; }
void CT_ ON_UnlockPlayers(void *) { ++g_unlockedPlayers; }
void SetPlayerCount(int count)
{
    // (The port's m_players: its begin and end words in the network game's raw storage.)
    uint8_t *net = reinterpret_cast<uint8_t *>(&g_netGame);
    S_NetPlayerState *begin = g_ctlPlayers, *end = g_ctlPlayers + count;
    std::memcpy(net + g_portPlayersOffset, &begin, 4);
    std::memcpy(net + g_portPlayersOffset + 4, &end, 4);
    std::memcpy(net + g_portPlayersOffset + 8, &end, 4);
}

// The formations' summary on each side: the humans' leaders, their formations (members by slot, count, type, leader).
std::string FormationText(const void *f)
{
    if (!f)
        return "none";
    const uint8_t *b = static_cast<const uint8_t *>(f);
    std::string s = NameOf(f) + "[" + NameOf(*reinterpret_cast<void *const *>(b + 0x74)) + " type " +
                    std::to_string(*reinterpret_cast<const int *>(b + 0x78)) + " count " +
                    std::to_string(*reinterpret_cast<const int *>(b + 0x7C));
    for (int i = 0; i < 6 && FormationSlots(f); ++i)
        s += " " + NameOf(*reinterpret_cast<void *const *>(FormationSlots(f) + i * 0xA0 + 0x70));
    return s + "]";
}
std::string g_portFormations, g_origFormations;
size_t g_formationPairs = 0;   // (this iteration's formations at the end of g_extraPointers)
int FormationsHash(bool original)
{
    const uint8_t *self = original ? o_human : reinterpret_cast<const uint8_t *>(g_h.human);
    const uint8_t *other = original ? g_origOther : g_portOther;
    const size_t leader = original ? 0x2CC : offsetof(C_Human, m_leader);
    const size_t formation = original ? 0x2C8 : offsetof(C_Human, m_formation);
    std::string &s = original ? g_origFormations : g_portFormations;
    s = "self " + NameOf(*reinterpret_cast<void *const *>(self + leader)) + " " +
        FormationText(*reinterpret_cast<void *const *>(self + formation)) + ", other " +
        NameOf(*reinterpret_cast<void *const *>(other + leader)) + " " +
        FormationText(*reinterpret_cast<void *const *>(other + formation));
    return int(std::hash<std::string>()(s) & 0x7FFFFFFF);
}
// A formation in both layouts (each side its own object; the members each side's humans), named.
std::pair<C_FormationShape *, void *> MakeFormations(const char *name, int members, int count)
{
    C_FormationShape *port = new C_FormationShape();
    void *orig = OC_AlignedNew(0xA0, 16);
    FormationInit(orig);
    g_names[port] = g_names[orig] = name;
    // Members: 1 the tested human, 2 the other one (in slots 0..5), the count as given (it may disagree).
    for (int i = 0; i < 6; ++i) {
        const int who = members >> (2 * i) & 3;
        void *p = who == 1 ? static_cast<void *>(g_h.human) : who == 2 ? static_cast<void *>(g_portOther) : nullptr;
        void *o = who == 1 ? static_cast<void *>(o_human) : who == 2 ? static_cast<void *>(g_origOther) : nullptr;
        std::memcpy(FormationSlots(port) + i * 0xA0 + 0x70, &p, 4);
        std::memcpy(FormationSlots(orig) + i * 0xA0 + 0x70, &o, 4);
    }
    FormationCount(port) = FormationCount(orig) = count;
    // (ToOriginal maps the human's pointer to it.)
    g_extraPointers.push_back({port, orig});
    ++g_formationPairs;
    const int type = I(0, 6);
    std::memcpy(reinterpret_cast<uint8_t *>(port) + 0x78, &type, 4);
    std::memcpy(static_cast<uint8_t *>(orig) + 0x78, &type, 4);
    return {port, orig};
}
void SetFormation(C_Human *portHuman, void *origHuman, const std::pair<C_FormationShape *, void *> &f)
{
    portHuman->m_formation = f.first;
    std::memcpy(static_cast<uint8_t *>(origHuman) + 0x2C8, &f.second, 4);
}
void SetLeaderOf(C_Human *portHuman, void *origHuman, int who)
{
    C_Human *p = who == 1 ? g_h.human : who == 2 ? PortOther() : nullptr;
    void *o = who == 1 ? static_cast<void *>(o_human) : who == 2 ? static_cast<void *>(g_origOther) : nullptr;
    portHuman->m_leader = p;
    std::memcpy(static_cast<uint8_t *>(origHuman) + 0x2CC, &o, 4);
}
// The formations: the tested human's own (it leads), the other's (it leads the tested one, or not), their slots.
void RandomFormations()
{
    C_Human *h = g_h.human;
    g_extraPointers.resize(g_extraPointers.size() - g_formationPairs);
    g_formationPairs = 0;
    // As the game keeps them: nobody leads, the other one leads the tested one, or the tested one leads the other one
    // (the leader and its follower in two of the formation's slots; the count sometimes more: an absent member).
    const std::pair<C_FormationShape *, void *> none{nullptr, nullptr};
    SetFormation(h, o_human, none);
    SetFormation(PortOther(), g_origOther, none);
    SetLeaderOf(h, o_human, 0);
    SetLeaderOf(PortOther(), g_origOther, 0);
    const int state = I(0, 2);
    if (state == 0)
        return;
    const int leaderCode = state == 1 ? 2 : 1, followerCode = state == 1 ? 1 : 2;
    int s1 = I(0, 5), s2 = I(0, 4);
    if (s2 >= s1)
        ++s2;
    const int members = leaderCode << (2 * s1) | followerCode << (2 * s2);
    const auto f = MakeFormations(state == 1 ? "formOther" : "formSelf", members, P(0.7f) ? 2 : I(3, 4));
    if (state == 1) {
        SetFormation(PortOther(), g_origOther, f);
        SetLeaderOf(h, o_human, 2);
    } else {
        SetFormation(h, o_human, f);
        SetLeaderOf(PortOther(), g_origOther, 1);
        // (A slot array or none.)
        if (P(0.1f)) {
            std::memset(reinterpret_cast<uint8_t *>(f.first) + 0x90, 0, 4);
            std::memset(static_cast<uint8_t *>(f.second) + 0x90, 0, 4);
        }
    }
}

// The profile's names on both sides (each side's own strings), the frame's.
typedef void(CT_ *T_StrSet)(void *, const char *);
void SetNames()
{
    static const char *texts[] = {"", "Smith", "J. Smith", "John (Jack) Smith", "Custom"};
    for (int i = 0; i < 2; ++i) {
        S_SoldierProfile *p = Profile(i);
        // first, last, nick, full, short, custom (+0x00 .. +0x14)
        C_str *fields[] = {&p->firstName, &p->lastName, &p->nickname, &p->fullName, &p->shortName, &p->customName};
        for (int k = 0; k < 6; ++k) {
            const char *t = k == 5 ? (P(0.6f) ? "" : texts[4]) : texts[I(0, 3)];
            *fields[k] = t;
            OriginalAt<T_StrSet>(0x00405180)(g_origProfileBytes[i] + 4 * k, t);
        }
        p->unknown24 = I(0, 9);
        std::memcpy(g_origProfileBytes[i] + 0x24, &p->unknown24, 4);
    }
}

void SetUpNames()
{
    RedirectOriginal(0x0072f040, FAKE(ON_LockPlayers));
    RedirectOriginal(0x0072f100, FAKE(ON_UnlockPlayers));
    for (uint32_t at : {0x005a81c0u, 0x005a8220u, 0x005a8380u, 0x005a9960u})
        RestoreOriginal(at);
    g_lockedPlayers = g_ctlPlayers;
    for (int k = 0; k < 4; ++k)
        g_origPlayersVector[k] = 0;
    void *first = g_origPlayers[0], *last = g_origPlayers[4];
    std::memcpy(&g_origPlayersVector[1], &first, 4);
    std::memcpy(&g_origPlayersVector[2], &last, 4);
    std::memcpy(&g_origPlayersVector[3], &last, 4);
    // (The port's player vector: found by its GetPlayerCount.)
    uint8_t *net = reinterpret_cast<uint8_t *>(&g_netGame);
    for (size_t off = 0; off + 12 <= 8192; off += 4) {
        uint8_t saved[12];
        std::memcpy(saved, net + off, 12);
        g_portPlayersOffset = off;
        SetPlayerCount(3);
        const bool found = g_netGame.GetPlayerCount() == 3;
        std::memcpy(net + off, saved, 12);
        if (found)
            break;
    }
    CHECK(g_netGame.GetPlayerCount() == 0);
    SetPlayerCount(1);
    CHECK(g_netGame.GetPlayerCount() == 1);
    SetPlayerCount(0);
    // Each side's other human and car.
    g_extraPointers.push_back({g_portOther, g_origOther});
    g_extraPointers.push_back({g_portCar, g_origCar});
    g_names[g_portCar] = g_names[g_origCar] = "car";
    // (GetShortName without a profile makes the name: each side's own string rep, as in part 21c.)
    g_skipFields.insert("m_name");
}

int TextHash(const char *s) { return int(std::hash<std::string>()(s ? s : "(null)") & 0x7FFFFFFF); }
typedef const char *(CT_ *T_CText)(void *);

void TestControlBody()
{
    C_Human *h = g_h.human;
    std::printf("control body and names\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomWrappers();
        if (!h->m_controller)
            h->m_controller = reinterpret_cast<C_HumanController *>(g_cPort);
        const int a = I(-2, 40), b = I(-2, 40), c = I(-2, 40);
        switch (i % 12) {
        case 0: {
            alignas(16) uint8_t command[0x40] = {};
            g_names[command] = "bodyCommand";
            DiffCall("ExecuteBodyCommand", [&] { h->ExecuteBodyCommand(*reinterpret_cast<const S_BodyCommand *>(command)); return 0; },
                     [&](void *) { OH<void>(0x005a45f0, static_cast<void *>(command)); return 0; });
            g_names.erase(command);
            break;
        }
        case 1:
            DiffCall("SetCarryMode", [&] { h->SetCarryMode(a); return 0; }, [&](void *) { OH<void>(0x005a5e40, a); return 0; });
            break;
        case 2:
            DiffCall("PlayAnimation", [&] { h->PlayAnimation("anim1", a, b, c); return 0; },
                     [&](void *) { OH<void>(0x005a9c20, "anim1", a, b, c); return 0; });
            break;
        case 3:
            DiffCall("IsLeftFootForward", [&] { return int(h->IsLeftFootForward()); }, [&](void *) { return OB(0x005a8160); });
            break;
        case 4: {
            // The vehicle: none (the ground's flags, or no ground), a car, another kind.
            const int v = I(0, 2);
            const int carType = P(0.5f) ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
            reinterpret_cast<C_Actor *>(g_portCar)->m_type = static_cast<E_ActorType>(carType);
            std::memcpy(g_origCar + 0x18, &carType, 4);
            h->m_vehicle = v ? reinterpret_cast<C_Actor *>(g_portCar) : nullptr;
            g_combat.groundOn = P(0.8f);
            g_combat.ground[0] = uint32_t(I(0, 0xFFFF));
            g_ctl.carEnv = uint32_t(I(0, 0xFFFF));
            DiffCall("GetEnvironmentFlags", [&] { return int(h->GetEnvironmentFlags()); },
                     [&](void *) { return OH<int>(0x005a9720); });
            break;
        }
        case 5: {
            const float value = P(0.3f) ? h->m_unknown94 : U(-2, 2);
            DiffCall("SetUnknown94", [&] { h->SetUnknown94(value); return 0; },
                     [&](void *) { OH<void>(0x005a9760, value); return 0; });
            break;
        }
        case 6:
        case 7:
        case 8:
        case 9: {
            SetNames();
            static const uint32_t at[] = {0x005a81c0, 0x005a8220, 0x005a82c0, 0x005a8320};
            static const char *names[] = {"GetLastName", "GetFirstName", "GetShortName", "GetFullName"};
            const int k = i % 12 - 6;
            DiffCall(names[k],
                     [&] {
                         const char *s = k == 0 ? h->GetLastName() : k == 1 ? h->GetFirstName() : k == 2 ? h->GetShortName()
                                                                                                     : h->GetFullName();
                         return TextHash(s);
                     },
                     [&](void *) { return TextHash(OriginalAt<T_CText>(at[k])(o_human)); });
            break;
        }
        case 10:
            DiffCall("GetPlayerName", [&] { return TextHash(h->GetPlayerName()); },
                     [&](void *) { return TextHash(OriginalAt<T_CText>(0x005a81b0)(o_human)); });
            break;
        default: {
            SetNames();
            // A network game: the player's slot in the table (the original reads past it otherwise).
            const int count = I(1, 4);
            SetPlayerCount(count);
            h->m_netId = I(0, count - 1);
            for (int k = 0; k < 4; ++k) {
                g_ctlPlayers[k].stats[NET_STAT_RANK] = I(0, 7);
                std::memcpy(g_origPlayers[k] + 0x40 + 4 * NET_STAT_RANK, &g_ctlPlayers[k].stats[NET_STAT_RANK], 4);
            }
            // (With the number of UnlockPlayers calls.)
            DiffCall("GetProfileUnknown24",
                     [&] {
                         g_unlockedPlayers = 0;
                         const int rank = h->GetProfileUnknown24();
                         return rank * 16 + g_unlockedPlayers;
                     },
                     [&](void *) {
                         g_unlockedPlayers = 0;
                         const int rank = OH<int>(0x005a8380);
                         return rank * 16 + g_unlockedPlayers;
                     });
            break;
        }
        }
    }
}

void TestControlFormations()
{
    C_Human *h = g_h.human;
    std::printf("control formations\n");
    g_skipFields.insert("m_leader");
    g_skipFields.insert("m_formation");
    for (int i = 0; i < 16000 / QUICK; ++i) {
        RandomWrappers();
        RandomFormations();
        const auto report = [&](const char *what) {
            static int shown;
            if (g_portFormations != g_origFormations && shown++ < 10)
                std::printf("  %s formations: port %s\n    original %s\n", what, g_portFormations.c_str(),
                            g_origFormations.c_str());
        };
        switch (i % 3) {
        case 0: {
            const int who = P(0.2f) ? 0 : P(0.85f) ? 2 : 1;
            C_Human *portLeader = who == 1 ? h : who == 2 ? PortOther() : nullptr;
            void *origLeader = who == 1 ? static_cast<void *>(o_human) : who == 2 ? static_cast<void *>(g_origOther) : nullptr;
            DiffCall("SetLeader", [&] { h->SetLeader(portLeader); return FormationsHash(false); },
                     [&](void *) { OH<void>(0x005a91a0, origLeader); return FormationsHash(true); });
            report("SetLeader");
            break;
        }
        case 1:
            DiffCall("DisbandFormation", [&] { h->DisbandFormation(); return FormationsHash(false); },
                     [&](void *) { OH<void>(0x005a9120); return FormationsHash(true); });
            report("DisbandFormation");
            break;
        default:
            DiffCall("GetFormationType", [&] { return h->GetFormationType(); }, [&](void *) { return OH<int>(0x005a9960); });
            break;
        }
    }
    g_skipFields.erase("m_leader");
    g_skipFields.erase("m_formation");
}

void TestControlScripts()
{
    C_Human *h = g_h.human;
    std::printf("control scripts\n");
    I3D_frame *frame = g_h.frame;
    C_Actor *actor = reinterpret_cast<C_Actor *>(g_vehicleBytes);
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomWrappers();
        if (!h->m_controller)
            h->m_controller = reinterpret_cast<C_HumanController *>(g_cPort);
        // The scripted controller half the time.
        if (P(0.5f))
            SetControllerType(HUMAN_CONTROLLER_AI);
        g_ctl.playActivity = I(-1, 3000);
        g_ctl.seesEnemy = P(0.5f);
        g_ctl.hasAIMode = P(0.5f);
        g_ctl.checkpoint = P(0.2f) ? 0xFFFF : I(0, 0x8000);
        const int a = P(0.2f) ? I(-40000, 70000) : I(-2, 40), b = I(-2, 40), c = I(-2, 40);
        const bool flag = P(0.5f), flag2 = P(0.5f);
        const float f1 = U(0, 100), f2 = U(0, 100);
        const int which = I(0, 41);
        switch (which) {
#define VOID0(k, name, at)                                                                                             \
    case k:                                                                                                            \
        DiffCall(#name, [&] { h->name(); return 0; }, [&](void *) { OH<void>(at); return 0; });                         \
        break;
            VOID0(0, SetMoveModeRun, 0x005af270)
            VOID0(1, SetMoveModeWalk, 0x005af290)
            VOID0(2, SetMoveModeSilence, 0x005af2b0)
            VOID0(3, SetMoveModeGuard, 0x005af2d0)
            VOID0(4, SetMoveModeLie, 0x005af2f0)
            VOID0(5, SetMoveModeCrouch, 0x005af310)
            VOID0(6, SetMoveModeStand, 0x005af330)
            VOID0(7, SetMoveModeStandFast, 0x005af350)
            VOID0(8, ActivityTurk, 0x005af390)
            VOID0(9, DisableEvents, 0x005af3b0)
            VOID0(10, EnableEvents, 0x005af3d0)
            VOID0(11, StopAction, 0x005af430)
            VOID0(12, ResetAI, 0x005af450)
            VOID0(13, DropObject, 0x005af490)
            VOID0(14, TakeWeapon, 0x005af4b0)
            VOID0(15, HolsterWeapon, 0x005af4d0)
            VOID0(16, Reload, 0x005af4f0)
#undef VOID0
#define INT1(k, name, at)                                                                                              \
    case k:                                                                                                            \
        DiffCall(#name, [&] { h->name(a); return 0; }, [&](void *) { OH<void>(at, a); return 0; });                     \
        break;
            INT1(17, SetAIMode, 0x005af3f0)
            INT1(18, SetAIOptimized, 0x005af5c0)
            INT1(19, MoveToAlarm, 0x005af6e0)
            INT1(20, ScriptedPlaySpeech, 0x005af550)
            INT1(21, ScriptedLookAround1, 0x005af6c0)
#undef INT1
        case 22:
            DiffCall("SetAlarmType", [&] { h->SetAlarmType(uint32_t(a), flag); return 0; },
                     [&](void *) { OH<void>(0x005af150, uint32_t(a), int(flag)); return 0; });
            break;
        case 23:
            DiffCall("GetAlarmType", [&] { return int(h->GetAlarmType()); }, [&](void *) { return OH<int>(0x005af170); });
            break;
        case 24:
            DiffCall("SetAlarm", [&] { h->SetAlarm(flag); return 0; }, [&](void *) { OH<void>(0x005af190, int(flag)); return 0; });
            break;
        case 25:
            DiffCall("AttackMoveTo", [&] { h->AttackMoveTo(a, b); return 0; },
                     [&](void *) { OH<void>(0x005af1b0, a, b); return 0; });
            break;
        case 26:
            DiffCall("MoveTo", [&] { h->MoveTo(a, b); return 0; }, [&](void *) { OH<void>(0x005af1d0, a, b); return 0; });
            break;
        case 27:
            // (A 16-bit result: the original leaves EAX's high word as it was.)
            DiffCall("FindCheckpoint", [&] { return h->FindCheckpoint("cp1") & 0xFFFF; },
                     [&](void *) { return OH<int>(0x005af210, "cp1") & 0xFFFF; });
            break;
        case 28: {
            C_Actor *target = P(0.2f) ? nullptr : actor;
            DiffCall("AttackActor", [&] { h->AttackActor(target, a, flag, b); return 0; },
                     [&](void *) { OH<void>(0x005af230, static_cast<void *>(target), a, int(flag), b); return 0; });
            break;
        }
        case 29:
            DiffCall("AttackFrame", [&] { h->AttackFrame(frame, a, flag, b); return 0; },
                     [&](void *) { OH<void>(0x005af250, static_cast<void *>(frame), a, int(flag), b); return 0; });
            break;
        case 30:
            DiffCall("Sit", [&] { h->Sit(frame); return 0; }, [&](void *) { OH<void>(0x005af370, static_cast<void *>(frame)); return 0; });
            break;
        case 31:
            DiffCall("PickObject", [&] { h->PickObject(frame, flag); return 0; },
                     [&](void *) { OH<void>(0x005af470, static_cast<void *>(frame), int(flag)); return 0; });
            break;
        case 32:
            DiffCall("PlayActivity", [&] { return h->PlayActivity(a); }, [&](void *) { return OH<int>(0x005af510, a); });
            break;
        case 33:
            DiffCall("PlayActivityAt", [&] { return h->PlayActivity(a, frame); },
                     [&](void *) { return OH<int>(0x005af530, a, static_cast<void *>(frame)); });
            break;
        case 34:
            DiffCall("SetWaitForPlayer", [&] { h->SetWaitForPlayer(flag, f1, f2, flag2); return 0; },
                     [&](void *) { OH<void>(0x005af5e0, int(flag), f1, f2, int(flag2)); return 0; });
            break;
        case 35:
            DiffCall("SeesEnemy", [&] { return int(h->SeesEnemy()); }, [&](void *) { return OB(0x005af620); });
            break;
        case 36:
            DiffCall("SeesFriend", [&] { return int(h->SeesFriend()); }, [&](void *) { return OB(0x005af640); });
            break;
        case 37:
            DiffCall("TurnAt", [&] { h->TurnAt(frame); return 0; }, [&](void *) { OH<void>(0x005af660, static_cast<void *>(frame)); return 0; });
            break;
        case 38:
            DiffCall("ScriptedAdvanceScript", [&] { h->ScriptedAdvanceScript(a, b); return 0; },
                     [&](void *) { OH<void>(0x005af1f0, a, b); return 0; });
            break;
        case 39:
            DiffCall("ScriptedHasAIMode", [&] { return int(h->ScriptedHasAIMode()); }, [&](void *) { return OB(0x005af410); });
            break;
        case 40:
            DiffCall("SetAnimation", [&] { h->SetAnimation("anim2", a, b, flag); return 0; },
                     [&](void *) { OH<void>(0x005af780, "anim2", a, b, int(flag)); return 0; });
            break;
        default: {
            h->m_events = reinterpret_cast<C_HumanEvents *>(g_eventsBytes);
            DiffCall("SetSniper", [&] { h->SetSniper(flag, f1); return 0; },
                     [&](void *) { OH<void>(0x005af700, int(flag), f1); return 0; });
            break;
        }
        }
        (void)c;
    }
}

void TestControl()
{
    SetUpCombat();
    SetUpMotion();
    SetUpDeaths();
    SetUpControl();
    // CONTROL_ONLY: one part's tests alone (22a, 22b ...), for its mutation list.
    const char *only = std::getenv("CONTROL_ONLY");
    const auto run = [&](const char *part) { return !only || std::strcmp(only, part) == 0; };
    if (run("22a")) {
        TestControlCreate();
        TestControlWrappers();
    }
    if (run("22b")) {
        SetUpNames();
        TestControlBody();
        TestControlFormations();
        TestControlScripts();
    }
}

}  // namespace
