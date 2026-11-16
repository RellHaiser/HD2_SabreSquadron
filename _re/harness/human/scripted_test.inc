// Part 39 of the human harness (HUMAN_SCRIPTED builds, build_scripted.bat; included by human_test.cpp after
// aicombat_test.inc, whose fakes and helpers it uses): the AI the mission script drives (game/scripted_controller.cpp)
// against the original's, on the derived AI controllers' common harness (derived_ai.inc).
//
// The port's scripted controller (a test class: ExecuteCommand logged) is the AI pair's port side; the original's is the
// AI constructor's object with a copy of its vtable (0x00811500, 0x1F0 bytes). Its own fields (+0x140 .. +0x1F0) by
// _re/tools/layout_offsets.py's table (the port's at other offsets), converted and compared around each call (the
// looped animation's name as a string: the port pads it with zeros, the original copies the name alone).
//
// Faked on both sides and logged: the script's OnAlarm, OnAlarmDone, OnActionDone and Advance (0x0041da20, 0x0041da80,
// 0x00416d50 / 0x00416d70 / 0x00416d90, 0x00416cf0), the formation's GetSlotTarget (0x00598320).
//
// Part 39a: the life cycle, the alarm, the moves, the move modes, the objects and weapons, the activities and
// animations, the saved games.

struct S_ScriptedScript {
    bool alarmDone = true;                              // OnAlarmDone's answer
    bool slot = true;                                   // GetSlotTarget's
    S_vector4 slotPos = {};
    uint16_t slotPoint = 0;
    int find = 0;                                       // the checkpoints' Find
    bool handsWeapon = true;                            // GetHandsWeapon: a weapon (part 23's first) or none
    int animLength = 600;                               // GetActivityAnimLength
    uint32_t time = 100000;                             // timeGetTime
    S_vector4 lead = {};                                // GetLeadPoint
} g_scs;

// ---- the port's side of the fakes (the original's below)
int C_AIController::UpdateDriving(int timeDelta)
{
    Log(this, "UpdateDriving", {double(timeDelta)});
    return 0;
}
int C_CheckpointNetwork::Find(const char *name) const
{
    Log(this, (std::string("CheckpointsFind ") + name).c_str());
    return g_scs.find;
}
void C_Human::Freeze(bool on) { Log(this, "Freeze", {double(on)}); }
void C_Human::PlayAnimation(const char *name, int a, int b, int c)
{
    Log(this, (std::string("PlayAnimation ") + (name ? name : "(null)")).c_str(), {double(a), double(b), double(c)});
}
void C_Human::SetCarryMode(int mode) { Log(this, "SetCarryMode", {double(mode)}); }
void C_Human::SetScripted(bool on) { Log(this, "SetScripted", {double(on)}); }
void C_Human::StartListening() { Log(this, "StartListening"); }
void C_Human::StopListening() { Log(this, "StopListening"); }
int S_HumanBody::GetActivityAnimLength(int id, int variant)
{
    Log(nullptr, "GetActivityAnimLength", {double(id), double(variant)});
    return g_scs.animLength;
}
// (enemy_gunner.cpp's, copied: inline in the original; that file is linked by the enemy's part.)
S_vector4 GetLeadPoint(const C_Human *human)
{
    S_vector4 point;
    const uint32_t flags = human->GetController()->GetMoveFlags();
    if (flags & (MOVE_STANDING | MOVE_CROUCHING)) {
        const S_vector &body = human->GetBodyFrame()->GetWorldPos();
        point = {body.x, body.y - 0.3f, body.z, 0.0f};
    } else {
        point = WorldPos4Of(human->GetFrame());
    }
    const S_vector &velocity = human->GetVelocity();
    point.x += velocity.x * 0.125f;
    point.y += velocity.y * 0.125f;
    point.z += velocity.z * 0.125f;
    return point;
}
// (human_events.cpp's, copied: GetActorById is the test's on both sides; the original runs its own.)
C_Actor *C_HumanEvents::GetNearbyActor(int index) const
{
    if (index < 0 || static_cast<size_t>(index) >= m_nearbyActors.size())
        return nullptr;
    const int id = m_nearbyActors[static_cast<size_t>(index)];
    if (id == -1)
        return nullptr;
    return g_actorsModule.GetActorById(id);
}
// timeGetTime (the port's import slot: this build links no winmm; the original's 0x0080f3f8).
extern "C" DWORD __stdcall ScTimeGetTime()
{
    Log(nullptr, "timeGetTime");
    return g_scs.time;
}
extern "C" {
void *g_impTimeGetTime asm("__imp__timeGetTime@0") = reinterpret_cast<void *>(ScTimeGetTime);
}

void C_Script::OnAlarm() { Log(this, "ScriptOnAlarm"); }
bool C_Script::OnAlarmDone()
{
    Log(this, "ScriptOnAlarmDone");
    return g_scs.alarmDone;
}
void C_Script::OnActionDone(int label) { Log(this, "ScriptOnActionDone", {double(label)}); }
void C_Script::Advance() { Log(this, "ScriptAdvance"); }
bool C_FormationShape::GetSlotTarget(const C_Human *member, S_vector4 *pos, uint16_t *checkpoint)
{
    Log(member, "FormationGetSlotTarget");
    *pos = g_scs.slotPos;
    *checkpoint = g_scs.slotPoint;
    return g_scs.slot;
}

#include "derived_ai.inc"
#include "game/sound_table.h"

C_WeaponItem *C_Human::GetHandsWeapon() const
{
    Log(this, "GetHandsWeapon");
    return g_scs.handsWeapon ? reinterpret_cast<C_WeaponItem *>(g_weaponPort[0]) : nullptr;
}
bool C_Human::RequestMoveItem(C_Item *item, C_Actor *other, int fromContainer, int toContainer, int slot)
{
    Log(this, ("RequestMoveItem " + NameOf(item) + " " + NameOf(other)).c_str(), {double(fromContainer), double(toContainer), double(slot)});
    return true;
}

namespace {
int AC SC_UpdateDriving(void *self, int timeDelta)
{
    Log(self, "UpdateDriving", {double(timeDelta)});
    return 0;
}
int AC SC_Find(void *self, const char *name)
{
    Log(self, (std::string("CheckpointsFind ") + name).c_str());
    return g_scs.find;
}
void AC SC_Freeze(void *self, int on) { Log(self, "Freeze", {double(on & 0xFF)}); }
void AC SC_PlayAnimation(void *self, const char *name, int a, int b, int c)
{
    Log(self, (std::string("PlayAnimation ") + (name ? name : "(null)")).c_str(), {double(a), double(b), double(c)});
}
void AC SC_SetCarryMode(void *self, int mode) { Log(self, "SetCarryMode", {double(mode)}); }
void AC SC_SetScripted(void *self, int on) { Log(self, "SetScripted", {double(on & 0xFF)}); }
void AC SC_StartListening(void *self) { Log(self, "StartListening"); }
void AC SC_StopListening(void *self) { Log(self, "StopListening"); }
int AC SC_ActivityAnimLength(void *, int id, int variant)
{
    Log(nullptr, "GetActivityAnimLength", {double(id), double(variant)});
    return g_scs.animLength;
}
void *AC SC_GetHandsWeapon(void *self)
{
    Log(self, "GetHandsWeapon");
    return g_scs.handsWeapon ? g_weaponOrig[0] : nullptr;
}
int AC SC_RequestMoveItem(void *self, void *item, void *other, int fromContainer, int toContainer, int slot)
{
    Log(self, ("RequestMoveItem " + NameOf(item) + " " + NameOf(other)).c_str(), {double(fromContainer), double(toContainer), double(slot)});
    return 1;
}
// The scene's FindFrame (slot 0x7C, COM: both sides'): the frames the saves name.
I3D_frame *__stdcall SC_SceneFindFrame(void *, const char *name, uint32_t)
{
    Log(nullptr, (std::string("SceneFindFrame ") + name).c_str());
    for (I3D_frame *f : {g_asThirdFrame, g_h.frame})
        if (std::strcmp(f->GetName(), name) == 0)
            return f;
    return nullptr;
}
DWORD __stdcall SC_TimeGetTime()
{
    Log(nullptr, "timeGetTime");
    return g_scs.time;
}
int CT_ SC_SilentBehaviour(void *) { return g_behaviour; }
void AC SC_OnAlarm(void *self) { Log(self, "ScriptOnAlarm"); }
int AC SC_OnAlarmDone(void *self)
{
    Log(self, "ScriptOnAlarmDone");
    return g_scs.alarmDone;
}
void AC SC_OnActionDone(void *self, int label) { Log(self, "ScriptOnActionDone", {double(label)}); }
void AC SC_Advance(void *self) { Log(self, "ScriptAdvance"); }
int AC SC_GetSlotTarget(void *, const void *member, S_vector4 *pos, uint16_t *checkpoint)
{
    Log(member, "FormationGetSlotTarget");
    *pos = g_scs.slotPos;
    *checkpoint = g_scs.slotPoint;
    return g_scs.slot;
}

typedef C_ScriptedController S_Sc;
DA_ROB(ScRaiseAlarm, void (S_Sc::*)(uint32_t, const S_vector4 &), &S_Sc::RaiseAlarm)
DA_ROB(ScStartMove, bool (S_Sc::*)(int16_t, int), &S_Sc::StartMove)
DA_ROB(ScFollowFormation, bool (S_Sc::*)(), &S_Sc::FollowFormation)
DA_ROB(ScAttack, S_Sc::S_Attack S_Sc::*, &S_Sc::m_attack)
DA_ROB(ScAnimation, char (S_Sc::*)[56], &S_Sc::m_animation)
DA_ROB(ScAlarmPos, S_vector4 S_Sc::*, &S_Sc::m_alarmPos)
DA_ROB(ScAnimationFrame, int S_Sc::*, &S_Sc::m_animationFrame)
DA_ROB(ScWaitForPlayer, bool S_Sc::*, &S_Sc::m_waitForPlayer)
DA_ROB(ScWatchPlayer, bool S_Sc::*, &S_Sc::m_watchPlayer)
DA_ROB(ScStopDist2, float S_Sc::*, &S_Sc::m_stopDist2)
DA_ROB(ScResumeDist2, float S_Sc::*, &S_Sc::m_resumeDist2)
DA_ROB(ScScript, C_Script *S_Sc::*, &S_Sc::m_script)
DA_ROB(ScScriptFlags, uint32_t S_Sc::*, &S_Sc::m_scriptFlags)
DA_ROB(ScAlarmType, uint32_t S_Sc::*, &S_Sc::m_alarmType)
DA_ROB(ScIgnoredAlarms, uint32_t S_Sc::*, &S_Sc::m_ignoredAlarms)
DA_ROB(ScHoldingObject, bool S_Sc::*, &S_Sc::m_holdingObject)
DA_ROB(ScFireFromSeat, bool S_Sc::*, &S_Sc::m_fireFromSeat)
DA_ROB(ScUnknown1BE, uint8_t (S_Sc::*)[2], &S_Sc::m_unknown1BE)
DA_ROB(ScFireTime, int S_Sc::*, &S_Sc::m_fireTime)
DA_ROB(ScFireTarget, C_Actor *S_Sc::*, &S_Sc::m_fireTarget)
DA_ROB(ScFireFrame, I3D_frame *S_Sc::*, &S_Sc::m_fireFrame)
DA_ROB(ScTurnTarget, I3D_frame *S_Sc::*, &S_Sc::m_turnTarget)
DA_ROB(ScTurning, bool S_Sc::*, &S_Sc::m_turning)
DA_ROB(ScSeated, bool S_Sc::*, &S_Sc::m_seated)
DA_ROB(ScSuspended, bool S_Sc::*, &S_Sc::m_suspended)
DA_ROB(ScCarryMode, int S_Sc::*, &S_Sc::m_carryMode)
DA_ROB(ScLabel, int S_Sc::*, &S_Sc::m_label)
DA_ROB(ScFireLabel, int S_Sc::*, &S_Sc::m_fireLabel)
DA_ROB(ScFormation, C_FormationShape *S_Sc::*, &S_Sc::m_formation)

// ---- the pair
struct S_TestScripted : C_ScriptedController {
    bool ExecuteCommand(const S_HumanCommand &c) override
    {
        LogCommand(this, c);
        return true;
    }
};
S_TestScripted *g_psc;
uint8_t *g_osc;
void *g_oscVt[0x48];
S_Sc *Sc() { return g_psc; }

// The fields (_re/tools/layout_offsets.py's table): {original offset, port offset, size, name, kind}.
const S_LayoutField kScriptedFields[] = {
    S_LayoutField{0x140, 292, 16, "m_alarmPos", 'f'},
    S_LayoutField{0x150, 308, 56, "m_animation", 'i'},
    S_LayoutField{0x188, 364, 4, "m_animationFrame", 'i'},
    S_LayoutField{0x18C, 368, 1, "m_waitForPlayer", 'i'},
    S_LayoutField{0x18D, 369, 1, "m_watchPlayer", 'i'},
    S_LayoutField{0x190, 372, 4, "m_stopDist2", 'f'},
    S_LayoutField{0x194, 376, 4, "m_resumeDist2", 'f'},
    S_LayoutField{0x198, 380, 20, "m_attack", 'm'},
    S_LayoutField{0x1AC, 400, 4, "m_script", 'p'},
    S_LayoutField{0x1B0, 404, 4, "m_scriptFlags", 'i'},
    S_LayoutField{0x1B4, 408, 4, "m_alarmType", 'i'},
    S_LayoutField{0x1B8, 412, 4, "m_ignoredAlarms", 'i'},
    S_LayoutField{0x1BC, 416, 1, "m_holdingObject", 'i'},
    S_LayoutField{0x1BD, 417, 1, "m_fireFromSeat", 'i'},
    S_LayoutField{0x1BE, 418, 2, "m_unknown1BE", 'i'},
    S_LayoutField{0x1C0, 420, 4, "m_fireTime", 'i'},
    S_LayoutField{0x1C4, 424, 4, "m_fireTarget", 'p'},
    S_LayoutField{0x1C8, 428, 4, "m_fireFrame", 'p'},
    S_LayoutField{0x1CC, 432, 4, "m_turnTarget", 'p'},
    S_LayoutField{0x1D0, 436, 1, "m_turning", 'i'},
    S_LayoutField{0x1D1, 437, 1, "m_seated", 'i'},
    S_LayoutField{0x1D2, 438, 1, "m_suspended", 'i'},
    S_LayoutField{0x1D4, 440, 4, "m_carryMode", 'i'},
    S_LayoutField{0x1D8, 444, 4, "m_label", 'i'},
    S_LayoutField{0x1DC, 448, 4, "m_fireLabel", 'i'},
    S_LayoutField{0x1E0, 452, 4, "m_formation", 'p'},
};
void ScriptedToOrig()
{
    const auto *p = reinterpret_cast<const uint8_t *>(Sc());
    for (const S_LayoutField &f : kScriptedFields)
        CopyMapped(p + f.port, g_osc + f.orig, f.size);
}
std::string ScriptedDiffers()
{
    const auto *p = reinterpret_cast<const uint8_t *>(Sc());
    for (const S_LayoutField &f : kScriptedFields) {
        if (std::strcmp(f.name, "m_animation") == 0) {
            // (The name: the port pads it with zeros, the original copies the name alone.)
            if (std::strncmp(reinterpret_cast<const char *>(p + f.port), reinterpret_cast<const char *>(g_osc + f.orig), 56) != 0)
                return "m_animation";
            continue;
        }
        const int at = FirstDiff(p + f.port, g_osc + f.orig, f.size);
        if (at >= 0) {
            uint32_t a = 0, b = 0;
            std::memcpy(&a, p + f.port + at, std::min<size_t>(4, f.size - at));
            std::memcpy(&b, g_osc + f.orig + at, std::min<size_t>(4, f.size - at));
            char text[128];
            std::snprintf(text, sizeof text, "%s+%d: port %08x original %08x", f.name, at, a, b);
            return text;
        }
    }
    return "";
}
// (The look direction's w, the facing's +0x2C: the turns pass a stack word they do not set, the port 0.)
template <class P, class O> void ScCall(const char *what, P port, O original)
{
    DaCall(what, ScriptedToOrig, ScriptedDiffers,
           [] {
               uint8_t *facing;
               std::memcpy(&facing, g_osc + 0x124, 4);
               std::memcpy(facing + 0x2C, reinterpret_cast<const uint8_t *>(g_psc->m_facing) + 0x2C, 4);
           },
           port, original);
}

// The formation (the leader at the port's member and the original's +0x74), frames to fire at and turn to.
alignas(16) uint8_t g_scFormation[0x100], g_scOrigFormation[0x100];   // (a block per side: the leader at +0x74 on both)
C_FormationShape *ScFormationObj() { return reinterpret_cast<C_FormationShape *>(g_scFormation); }
DA_ROB(FmLeader, C_Human *C_FormationShape::*, &C_FormationShape::m_leader)

void SetUpScripted()
{
    SetUpDerivedAi();
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0041da20, FAKE(SC_OnAlarm)},
        {0x0041da80, FAKE(SC_OnAlarmDone)},
        {0x00416d50, FAKE(SC_OnActionDone)},
        {0x00416d70, FAKE(SC_OnActionDone)},
        {0x00416d90, FAKE(SC_OnActionDone)},
        {0x00416cf0, FAKE(SC_Advance)},
        {0x00598320, FAKE(SC_GetSlotTarget)},
        {0x00578770, FAKE(SC_UpdateDriving)},
        {0x0051e1a0, FAKE(SC_Find)},
        {0x005a5d50, FAKE(SC_Freeze)},
        {0x005a9c20, FAKE(SC_PlayAnimation)},
        {0x005a5e40, FAKE(SC_SetCarryMode)},
        {0x005a3df0, FAKE(SC_SetScripted)},
        {0x005a7fa0, FAKE(SC_StartListening)},
        {0x005a7f50, FAKE(SC_StopListening)},
        {0x005ac1f0, FAKE(SC_ActivityAnimLength)},
        {0x005a2b10, FAKE(SC_GetHandsWeapon)},
        {0x005a2b40, FAKE(SC_RequestMoveItem)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    *OriginalAt<void **>(0x0080f3f8) = reinterpret_cast<void *>(SC_TimeGetTime);
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(SC_SceneFindFrame);
    g_psc = new S_TestScripted;
    g_osc = DaOrigController(g_psc, 0x1F0, HUMAN_CONTROLLER_AI, 0x00811500, g_oscVt, "scripted");
    g_names[g_scFormation] = g_names[g_scOrigFormation] = "formation";
    g_extraPointers.push_back({g_scFormation, g_scOrigFormation});
}

// The fighting's state (part 36's), then the scripted controller's fields, the events, the script, the formation.
void RandomScripted(bool aiming)
{
    RandomFireOrder(true);
    S_Sc *s = Sc();
    C_Human *h = g_h.human;
    C_Human *const humans[2] = {h, aiming ? h : PortOther()};
    if (aiming) {
        h->m_seenEnemies.clear();
        for (int k = P(0.4f) ? 0 : I(1, 2); k > 0; --k)
            h->m_seenEnemies.push_back(h);
    }
    // (The human anywhere: at the origin, a difference and a sum about it look alike.)
    h->m_position = {U(-30, 30), U(-2, 2), U(-30, 30), 1.0f};
    s->m_events = &g_daEvents;
    h->m_events = &g_daEvents;
    s->*DaGet(ScAlarmPos()) = AiDir();
    char *animation = s->*DaGet(ScAnimation());
    std::memset(animation, 0, 56);
    if (P(0.5f))
        std::snprintf(animation, 56, "anim%d", I(0, 99));
    s->*DaGet(ScAnimationFrame()) = AiInt();
    s->*DaGet(ScWaitForPlayer()) = P(0.3f);
    s->*DaGet(ScWatchPlayer()) = P(0.5f);
    s->*DaGet(ScStopDist2()) = U(0, 400);
    s->*DaGet(ScResumeDist2()) = U(0, 100);
    S_Sc::S_Attack &attack = s->*DaGet(ScAttack());
    std::memset(static_cast<void *>(&attack), 0, sizeof attack);
    attack.active = P(0.5f);
    static const uint32_t states[] = {0, 0x10, 0x40, 0x80};
    attack.state = states[I(0, 3)];
    attack.time = AiInt();
    attack.stance = P(0.5f) ? 0u : uint32_t(I(0, 7));
    attack.target = P(0.4f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 1)]);
    s->*DaGet(ScScript()) = P(0.85f) ? reinterpret_cast<C_Script *>(g_daScriptBytes) : nullptr;
    s->*DaGet(ScScriptFlags()) = uint32_t(I(0, 7)) | (uint32_t(I(0, 15)) << 12);
    s->*DaGet(ScAlarmType()) = uint32_t(I(0, 0x3FF));
    s->*DaGet(ScIgnoredAlarms()) = P(0.3f) ? 0u : uint32_t(I(0, 0x3FF));
    s->*DaGet(ScHoldingObject()) = P(0.5f);
    s->*DaGet(ScFireFromSeat()) = P(0.3f);
    (s->*DaGet(ScUnknown1BE()))[0] = uint8_t(I(0, 255));
    (s->*DaGet(ScUnknown1BE()))[1] = uint8_t(I(0, 255));
    s->*DaGet(ScFireTime()) = P(0.4f) ? 0 : I(-100, 5000);
    s->*DaGet(ScFireTarget()) = P(0.4f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 1)]);
    s->*DaGet(ScFireFrame()) = P(0.5f) ? nullptr : (P(0.5f) ? g_asThirdFrame : g_h.frame);
    s->*DaGet(ScTurnTarget()) = P(0.5f) ? nullptr : (P(0.5f) ? g_asThirdFrame : g_h.frame);
    s->*DaGet(ScTurning()) = P(0.3f);
    s->*DaGet(ScSeated()) = P(0.3f);
    s->*DaGet(ScSuspended()) = P(0.3f);
    s->*DaGet(ScCarryMode()) = I(-1, 4);
    s->*DaGet(ScLabel()) = I(-1, 30);
    s->*DaGet(ScFireLabel()) = I(-1, 30);
    // The formation: none, led by the human, or by another.
    std::memset(g_scFormation, 0, sizeof g_scFormation);
    std::memset(g_scOrigFormation, 0, sizeof g_scOrigFormation);
    C_Human *leader = P(0.3f) ? h : PortOther();
    ScFormationObj()->*DaGet(FmLeader()) = leader;
    void *oleader = ToOrig(leader);
    std::memcpy(g_scOrigFormation + 0x74, &oleader, 4);
    s->*DaGet(ScFormation()) = P(0.4f) ? nullptr : ScFormationObj();
    g_scs.alarmDone = P(0.5f);
    g_scs.slot = P(0.8f);
    const S_vector own = g_h.frame->GetWorldPos();
    const float d = P(0.3f) ? U(0, 1.2f) : P(0.5f) ? U(0, 4) : U(0, 16);
    const float yaw = U(0, 6.2831f);
    const S_vector4 &at = h->m_position;   // (FollowFormation measures from the position)
    g_scs.slotPos = {at.x + std::sin(yaw) * d, at.y + U(-0.3f, 0.3f), at.z + std::cos(yaw) * d, 1.0f};
    g_scs.slotPoint = uint16_t(I(0, 7));
    // The moving flags: alarmed (0x80000000) and deaf now and then.
    s->m_moveFlags = (s->m_moveFlags & ~0x80000000u) | (P(0.5f) ? 0x80000000u : 0u);
    s->m_moveFlags = P(0.3f) ? s->m_moveFlags | MOVE_DEAF : s->m_moveFlags & ~uint32_t(MOVE_DEAF);
    // The pending events (part 39b's meleeTracker alerts), the stimuli, the nearby actors.
    C_HumanEvents &e = g_daEvents;
    e.*DaGet(EvPending()) = {};
    e.*DaGet(EvStimuli()) = {};
    e.*DaGet(EvNearby()) = {};
    for (S_HumanEvent &ev : g_daEv) {
        ev.type = P(0.5f) ? 0x10000017u : uint32_t(I(1, 9));
        // (An alert's source someone there, as the meleeTracker barks at: the AI turns toward a sound's source unchecked.)
        ev.sourceId = P(0.2f) && ev.type != 0x10000017u ? -1 : humans[I(0, 1)]->GetNetId();
        ev.timeLeft = I(0, 30000);
        ev.pos = AiDir();
    }
    // The script (OnAlarmDone or none) in the frame's data.
    const int onAlarmDone = P(0.5f) ? -1 : I(0, 50);
    reinterpret_cast<C_Script *>(g_daScriptBytes)->*DaGet(ScOnAlarmDone()) = onAlarmDone;
    std::memcpy(g_daScriptBytes + 0xEC, &onAlarmDone, 4);
    g_daFrameData = S_FrameData();
    g_daFrameData.actor = h;
    g_daFrameData.script = P(0.8f) ? reinterpret_cast<C_Script *>(g_daScriptBytes) : nullptr;
    g_daHandsLocked = P(0.2f);
    g_da.move = I(0, 3);
    g_da.longGun = P(0.5f);
    if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL && P(0.8f))
        s->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
    if (P(0.5f))
        RandomAiQueue(g_pa->m_toClients, g_oa + 0x90);
    DaRandomPoints(s);
}

// ---- part 39a
void TestScriptedA()
{
    std::printf("scripted: life cycle, alarm, moves, modes, objects, saves\n");
    C_Human *const humans[2] = {g_h.human, PortOther()};
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomScripted(false);
        S_Sc *s = Sc();
        S_Sc::S_Attack &attack = s->*DaGet(ScAttack());
        // (The handlers three times as often: eight kinds share one case.)
        int which = I(0, 41);
        if (which >= 40)
            which = 2;
        switch (which) {
        case 0: {
            C_HumanController *previous = P(0.5f) ? nullptr : static_cast<C_HumanController *>(g_pc2);
            g_h.frame->SetData(&g_daFrameData);
            ScCall("Attach", [&] { s->Attach(g_h.human, previous); return 0; },
                   [&](void *) { OAi<void>(0x0058d810, ToOrig(g_h.human), previous ? ToOrig(previous) : nullptr); return 0; });
            break;
        }
        case 1:
            ScCall("Detach", [&] { s->Detach(); return 0; }, [&](void *) { OAi<void>(0x0058d8e0); return 0; });
            break;
        case 2: {
            // The handlers: a human, an actor, a sound, a hit.
            C_Human *human = humans[I(0, 1)];
            const int k = I(0, 7);
            static const char *const names[] = {"OnEnemySeen", "OnBodySeen", "OnBodyHeard", "OnFriendHeard",
                                                "OnLoudSound", "OnShotAt", "OnHit", "OnSoundHeard"};
            const S_vector v(U(-30, 30), U(-3, 3), U(-30, 30)), w(U(-1, 1), U(-1, 1), U(-1, 1));
            C_Actor *actor = P(0.8f) ? static_cast<C_Actor *>(human) : (P(0.5f) ? nullptr : static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1))));
            if (k == 3 || k == 4 || k == 5)
                actor = human;   // (a friend heard, a loud sound's source and a shooter: humans; the original asks them unchecked)
            if (k == 3)
                actor = g_h.human;   // (the friend's enemies in view: only the tested human's lists are converted)
            const int type = I(0, 5), zone = I(0, 7), health = I(-5, 200);
            alignas(16) S_RememberedSound ps;
            alignas(16) uint8_t os[sizeof(S_RememberedSound)];
            ps.pos = v;
            ps.posW = U(0, 1);
            static const int chapters[] = {SOUND_CHAPTER_PUPPET_MOVEMENT, SOUND_CHAPTER_OBJECT_MANIPULATION, SOUND_CHAPTER_WEAPON_SHOOTING,
                                           SOUND_CHAPTER_EXPLOSION, 0, 3};
            ps.chapter = chapters[I(0, 5)];
            ps.source = human;   // (the AI base turns toward a sound's source unchecked)
            ps.time = uint32_t(I(0, 100000));
            ps.refreshed = P(0.5f);
            std::memcpy(os, &ps, sizeof ps);
            void *oSource = ps.source ? ToOrig(ps.source) : nullptr;
            std::memcpy(os + offsetof(S_RememberedSound, source), &oSource, 4);
            static const uint32_t at[] = {0x00590060, 0x0058ffe0, 0x00590250, 0x00590170, 0x005900e0, 0x00590380, 0x00590400, 0x005902d0};
            void *oActor = actor ? ToOrig(actor) : nullptr;
            void *oHuman = ToOrig(human);
            ScCall(names[k],
                   [&] {
                       switch (k) {
                       case 0: s->OnEnemySeen(human); break;
                       case 1: s->OnBodySeen(human); break;
                       case 2: s->OnBodyHeard(actor ? actor : human); break;
                       case 3: s->OnFriendHeard(actor); break;
                       case 4: s->OnLoudSound(actor); break;
                       case 5: s->OnShotAt(actor, v); break;
                       case 6: s->OnHit(type, zone, health, actor, v, w); break;
                       default: s->OnSoundHeard(&ps); break;
                       }
                       return 0;
                   },
                   [&](void *) {
                       switch (k) {
                       case 0: case 1: OAi<void>(at[k], oHuman); break;
                       case 2: OAi<void>(at[k], oActor ? oActor : oHuman); break;
                       case 3: case 4: OAi<void>(at[k], oActor); break;
                       case 5: OAi<void>(at[k], oActor, &v); break;
                       case 6: OAi<void>(at[k], type, zone, health, oActor, &v, &w); break;
                       default: OAi<void>(at[k], static_cast<void *>(os)); break;
                       }
                       return 0;
                   });
            break;
        }
        case 3: {
            const uint32_t flags = uint32_t(I(0, 0x3FF));
            const bool set = P(0.5f);
            ScCall("SetAlarmType", [&] { s->SetAlarmType(flags, set); return 0; }, [&](void *) { OAi<void>(0x0058e650, flags, int(set)); return 0; });
            break;
        }
        case 4:
            ScCall("GetAlarmType", [&] { return int(s->GetAlarmType()); }, [&](void *) { return int(OAi<uint32_t>(0x0058e700)); });
            break;
        case 5: {
            const bool on = P(0.3f);
            ScCall("SetAlarm", [&] { s->SetAlarm(on); return 0; }, [&](void *) { OAi<void>(0x0058e710, int(on)); return 0; });
            break;
        }
        case 6: {
            const int label = I(-1, 30);
            ScCall("MoveToAlarm", [&] { s->MoveToAlarm(label); return 0; }, [&](void *) { OAi<void>(0x00590760, label); return 0; });
            break;
        }
        case 7:
            g_h.frame->SetData(&g_daFrameData);
            ScCall("StartScript", [&] { s->StartScript(); return 0; }, [&](void *) { OAi<void>(0x00590700); return 0; });
            break;
        case 8:
        case 9: {
            // (The point the human stands at now and then.)
            int point = I(-1, 7);
            if (P(0.3f) && s->m_currentCheckpoint)
                point = s->m_checkpoints->GetIndex(s->m_currentCheckpoint);
            const int label = I(-1, 30);
            if (which == 8)
                ScCall("MoveTo", [&] { s->MoveTo(int16_t(point), label); return 0; }, [&](void *) { OAi<void>(0x0058e780, point, label); return 0; });
            else
                ScCall("AttackMoveTo", [&] { s->AttackMoveTo(int16_t(point), label); return 0; },
                       [&](void *) { OAi<void>(0x00591470, point, label); return 0; });
            break;
        }
        case 10: {
            const bool on = P(0.5f), watch = P(0.5f);
            const float stop = U(0, 400), resume = U(0, 100);
            ScCall("SetWaitForPlayer", [&] { s->SetWaitForPlayer(on, stop, resume, watch); return 0; },
                   [&](void *) { OAi<void>(0x0058e850, int(on), stop, resume, int(watch)); return 0; });
            break;
        }
        case 11: {
            C_FormationShape *formation = P(0.3f) ? nullptr : ScFormationObj();
            ScCall("SetFormation", [&] { s->SetFormation(formation); return 0; },
                   [&](void *) { OAi<void>(0x0058d900, formation ? ToOrig(formation) : nullptr); return 0; });
            break;
        }
        case 12:
        {
            // (The human leading the formation: the original returns the human's address' low byte, the port false; the
            // result is not compared then.)
            const bool leads = s->*DaGet(ScFormation()) && ScFormationObj()->*DaGet(FmLeader()) == g_h.human;
            ScCall("FollowFormation", [&] { const int r = int((s->*DaGet(ScFollowFormation()))()); return leads ? 0 : r; },
                   [&](void *) { const int r = int(OAi<uint32_t>(0x0058d970) & 0xFF); return leads ? 0 : r; });
        }
            break;
        case 13: {
            static const uint32_t at[] = {0x0058f2d0, 0x0058f2f0, 0x0058f310, 0x0058f330, 0x0058f350, 0x0058f370, 0x0058f390, 0x0058f420};
            static const char *const names[] = {"SetMoveModeRun", "SetMoveModeWalk", "SetMoveModeSilence", "SetMoveModeGuard",
                                                "SetMoveModeLie", "SetMoveModeCrouch", "SetMoveModeStand", "SetMoveModeStandFast"};
            const int k = I(0, 7);
            ScCall(names[k],
                   [&] {
                       switch (k) {
                       case 0: s->SetMoveModeRun(); break;
                       case 1: s->SetMoveModeWalk(); break;
                       case 2: s->SetMoveModeSilence(); break;
                       case 3: s->SetMoveModeGuard(); break;
                       case 4: s->SetMoveModeLie(); break;
                       case 5: s->SetMoveModeCrouch(); break;
                       case 6: s->SetMoveModeStand(); break;
                       default: s->SetMoveModeStandFast(); break;
                       }
                       return 0;
                   },
                   [&](void *) { OAi<void>(at[k]); return 0; });
            break;
        }
        case 14: {
            I3D_frame *seat = P(0.5f) ? g_asThirdFrame : g_h.frame;
            ScCall("Sit", [&] { s->Sit(seat); return 0; }, [&](void *) { OAi<void>(0x0058f4b0, static_cast<void *>(seat)); return 0; });
            break;
        }
        case 15:
            ScCall("ActivityTurk", [&] { s->ActivityTurk(); return 0; }, [&](void *) { OAi<void>(0x0058f540); return 0; });
            break;
        case 16:
            if (P(0.5f))
                ScCall("DisableEvents", [&] { s->DisableEvents(); return 0; }, [&](void *) { OAi<void>(0x0058f5d0); return 0; });
            else
                ScCall("EnableEvents", [&] { s->EnableEvents(); return 0; }, [&](void *) { OAi<void>(0x0058f5f0); return 0; });
            break;
        case 17: {
            const int mode = P(0.5f) ? I(0, 0x1F) << 8 : (std::rand() << 17) ^ std::rand();
            ScCall("SetMode", [&] { s->SetMode(mode); return 0; }, [&](void *) { OAi<void>(0x0058f610, mode); return 0; });
            break;
        }
        case 18:
            ScCall("HasAIMode", [&] { return int(s->HasAIMode()); }, [&](void *) { return int(OAi<uint32_t>(0x0058f630) & 0xFF); });
            break;
        case 19:
            ScCall("StopAction", [&] { s->StopAction(); return 0; }, [&](void *) { OAi<void>(0x0058f640); return 0; });
            break;
        case 20: {
            g_h.human->m_role = static_cast<E_HumanRole>(I(0, 6));
            g_daEvents.*DaGet(EvSniperRadius()) = U(0, 1);
            ScCall("ResetAI", [&] { s->ResetAI(); return 0; }, [&](void *) { OAi<void>(0x0058f680); return 0; });
            break;
        }
        case 21: {
            I3D_frame *object = P(0.5f) ? g_asThirdFrame : g_h.frame;
            const bool oneHand = P(0.5f);
            ScCall("PickObject", [&] { s->PickObject(object, oneHand); return 0; },
                   [&](void *) { OAi<void>(0x0058f700, static_cast<void *>(object), int(oneHand)); return 0; });
            break;
        }
        case 22:
            ScCall("DropObject", [&] { s->DropObject(); return 0; }, [&](void *) { OAi<void>(0x0058f790); return 0; });
            break;
        case 23:
            ScCall("TakeWeapon", [&] { s->TakeWeapon(); return 0; }, [&](void *) { OAi<void>(0x0058f830); return 0; });
            break;
        case 24:
            // (The original asks the item's behaviour first and drops the answer: its getter silent here.)
            ScCall("HolsterWeapon", [&] { s->HolsterWeapon(); return 0; },
                   [&](void *) {
                       g_defVt[0x10 / 4] = reinterpret_cast<void *>(SC_SilentBehaviour);
                       OAi<void>(0x0058f880);
                       return 0;
                   });
            break;
        case 25:
            ScCall("Reload", [&] { s->Reload(); return 0; }, [&](void *) { OAi<void>(0x0058f8c0); return 0; });
            break;
        case 26: {
            const int activity = I(0, 0x30);
            I3D_frame *target = P(0.5f) ? nullptr : g_asThirdFrame;
            if (!target)
                ScCall("PlayActivity", [&] { return s->PlayActivity(activity); }, [&](void *) { return OAi<int>(0x0058f8d0, activity); });
            else
                ScCall("PlayActivityAt", [&] { return s->PlayActivity(activity, target); },
                       [&](void *) { return OAi<int>(0x0058f970, activity, static_cast<void *>(target)); });
            break;
        }
        case 27: {
            const bool on = P(0.5f);
            ScCall("Suspend", [&] { s->Suspend(on); return 0; }, [&](void *) { OAi<void>(0x0058faa0, int(on)); return 0; });
            break;
        }
        case 28: {
            const int mode = I(-1, 4);
            ScCall("SetOptimized", [&] { s->SetOptimized(mode); return 0; }, [&](void *) { OAi<void>(0x0058fcc0, mode); return 0; });
            break;
        }
        case 29:
            if (P(0.5f))
                ScCall("SeesEnemy", [&] { return int(s->SeesEnemy()); }, [&](void *) { return int(OAi<uint32_t>(0x0058fcf0) & 0xFF); });
            else
                ScCall("SeesFriend", [&] { return int(s->SeesFriend()); }, [&](void *) { return int(OAi<uint32_t>(0x0058fd20) & 0xFF); });
            break;
        case 30: {
            char name[40];
            std::snprintf(name, sizeof name, "%s%d", P(0.5f) ? "walk" : "", I(0, 99));
            const int blend = I(0, 500), cancel = I(0, 500);
            const bool loop = P(0.5f);
            ScCall("SetAnimation", [&] { s->SetAnimation(name, blend, cancel, loop); return 0; },
                   [&](void *) { OAi<void>(0x00591230, static_cast<const char *>(name), blend, cancel, int(loop)); return 0; });
            break;
        }
        case 31:
            ScCall("AdvanceScript", [&] { s->AdvanceScript(1, 2); return 0; }, [&](void *) { OAi<void>(0x0058e880, 1, 2); return 0; });
            break;
        case 32: {
            const int group = I(0, 0x8A);
            ScCall("PlaySpeech", [&] { s->PlaySpeech(group); return 0; }, [&](void *) { OAi<void>(0x0058fa10, group); return 0; });
            break;
        }
        case 33:
            ScCall("LookAround1", [&] { s->LookAround1(3); return 0; }, [&](void *) { OAi<void>(0x00590750, 3); return 0; });
            break;
        case 34: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x00591eb0, 0x00591ed0, 0x00591ee0};
            static const char *const names[] = {"AttackStart", "AttackStop", "AttackClear"};
            ScCall(names[k],
                   [&] {
                       if (k == 0)
                           attack.Start();
                       else if (k == 1)
                           attack.Stop();
                       else
                           attack.Clear();
                       return 0;
                   },
                   [&](void *) {
                       typedef void(AC * T_Fn)(void *);
                       OriginalAt<T_Fn>(at[k])(g_osc + 0x198);
                       return 0;
                   });
            break;
        }
        case 35: {
            const uint32_t kind = uint32_t(1) << I(0, 9);
            const S_vector4 pos = AiDir();
            ScCall("RaiseAlarm (ResetAI's flags)", [&] { (s->*DaGet(ScRaiseAlarm()))(kind, pos); return 0; },
                   [&](void *) {
                       // (Inline in the original: its handlers' common tail; the same fields and the script's call.)
                       std::memcpy(g_osc + 0x1B4, &kind, 4);
                       uint32_t flags;
                       std::memcpy(&flags, g_osc + 0x50, 4);
                       flags |= 0x80000000u;
                       std::memcpy(g_osc + 0x50, &flags, 4);
                       std::memcpy(g_osc + 0x140, &pos, 16);
                       C_Script *script;
                       std::memcpy(&script, g_osc + 0x1AC, 4);
                       if (script)
                           SC_OnAlarm(script);
                       return 0;
                   });
            break;
        }
        case 36:
            ScCall("Unknown104", [&] { return int(s->Unknown104()); }, [&](void *) { return int(OAi<uint32_t>(0x005a42a0) & 0xFF); });
            break;
        case 37: {
            const S_vector dir(U(-1, 1), U(-1, 1), U(-1, 1));
            if (P(0.5f)) {
                ScCall("Unknown114", [&] { return int(s->Unknown114(&dir)); }, [&](void *) { return int(OAi<uint32_t>(0x005a4290, &dir) & 0xFF); });
            } else {
                g_scs.find = P(0.2f) ? -1 : I(0, 7);
                const char *name = P(0.5f) ? "point1" : "post";
                ScCall("FindCheckpoint", [&] { return int(s->FindCheckpoint(name)); },
                       [&](void *) { return int(OAi<uint32_t>(0x0058e8a0, name) & 0xFFFF); });
            }
            break;
        }
        default: {
            // The saved games (an interaction with its target: part 33's).
            static const int kinds[] = {0, 1, 2, 9, 11, KIND_HUMAN};
            if (P(0.6f)) {
                s->m_interaction = P(0.2f) ? HUMAN_COMMAND_HEAL_SELF : I(0, 0x26);
                s->m_interactionTarget = static_cast<C_Actor *>(PortActor(kinds[I(0, 5)]));
            } else {
                s->m_interaction = -1;
            }
            // (The frames named in the scene: the fire frame and the turn target by name.)
            const std::string portFile = AiTempFile("_scport.sav"), origFile = AiTempFile("_scorig.sav");
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            auto save = [&](const std::string &file, bool orig) {
                C_chunk c;
                c.Open(file.c_str(), true);
                c.Begin(0x7777);
                if (orig)
                    OAi<void>(0x005907d0, &c);
                else
                    s->SaveGame(c);
                c.Ascend();
                c.Begin(0x0BD1);
                c.Ascend();
                return 0;
            };
            ScCall("SaveGame", [&] { return save(portFile, false); }, [&](void *) { return save(origFile, true); });
            const bool same = AiFileBytes(portFile) == AiFileBytes(origFile);
            CHECK(same);
            static int shown;
            if (!same && shown++ < 3)
                std::printf("  SaveGame: the files differ\n");
            RandomScripted(false);
            g_daGoneId = -1000;
            auto load = [&](bool orig) {
                C_chunk c;
                c.Open(portFile.c_str(), false);
                uint16_t id;
                c.Descend(id);
                if (orig)
                    OAi<void>(0x00590b60, &c);
                else
                    s->LoadGame(c);
                int depth;
                std::memcpy(&depth, reinterpret_cast<const uint8_t *>(&c) + 0x410, 4);
                const int at = c.GetPosition();
                c.Ascend();
                return depth * 0x10000 + (at & 0xFFFF) + AiNextChunk(c) * 0x100000;
            };
            ScCall("LoadGame", [&] { return load(false); }, [&](void *) { return load(true); });
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            break;
        }
        }
    }
}

// ---- part 39b: the update and the tick, the seat weapon's turning and firing, the firing for a time, AttackMoveTo's
// attack, the meleeTrackers' alerts, the neutral controller.
DA_ROB(ScTurnSeatWeapon, void (S_Sc::*)(), &S_Sc::TurnSeatWeapon)
DA_ROB(ScUpdateFireTime, void (S_Sc::*)(int), &S_Sc::UpdateFireTime)
DA_ROB(ScGetSeatAimDir, S_vector (S_Sc::*)(bool), &S_Sc::GetSeatAimDir)
DA_ROB(ScAimAndFireSeatWeapon, void (S_Sc::*)(), &S_Sc::AimAndFireSeatWeapon)
DA_ROB(ScProcessMeleeTrackerAlerts, void (S_Sc::*)(), &S_Sc::ProcessMeleeTrackerAlerts)
DA_ROB(ScDecideAttack, void (S_Sc::*)(), &S_Sc::DecideAttack)
DA_ROB(ScUpdateAttack, void (S_Sc::*)(int), &S_Sc::UpdateAttack)

// The car's seat weapon's rounds and origin (part 35's car: the port's slots and the original's 0x9C, 0xA8 with its
// hidden return pointer).
struct S_ScSeat {
    int rounds = 5;
    S_vector origin;
} g_scSeat;
struct S_ScSeatFake {
    virtual int Rounds(int seat);
    virtual S_vector Origin(int seat);
};
int S_ScSeatFake::Rounds(int seat)
{
    Log(this, "GetSeatWeaponRounds", {double(seat)});
    return g_scSeat.rounds;
}
S_vector S_ScSeatFake::Origin(int seat)
{
    Log(this, "GetSeatWeaponOrigin", {double(seat)});
    return g_scSeat.origin;
}
int AC SC_SeatRounds(void *self, int seat)
{
    Log(self, "GetSeatWeaponRounds", {double(seat)});
    return g_scSeat.rounds;
}
float *AC SC_SeatOrigin(void *self, float *out, int seat)
{
    Log(self, "GetSeatWeaponOrigin", {double(seat)});
    out[0] = g_scSeat.origin.x;
    out[1] = g_scSeat.origin.y;
    out[2] = g_scSeat.origin.z;
    return out;
}
void AC SC_SubmarineSetControls(void *self, float forward, float side, float up)
{
    Log(self, "SubmarineSetControls", {forward, side, up});
}

// The neutral controller's pair (vtable 0x008113e8, 0x140 bytes).
struct S_TestNeutral : C_NeutralController {
    bool ExecuteCommand(const S_HumanCommand &c) override
    {
        LogCommand(this, c);
        return true;
    }
};
S_TestNeutral *g_pne;
uint8_t *g_one;
void *g_oneVt[0x48];

void SetUpScriptedB()
{
    S_ScSeatFake fake;
    void **fakeVt = *reinterpret_cast<void ***>(&fake);
    g_asCarPortVt[VSlot(&C_Actor::GetSeatWeaponRounds)] = fakeVt[0];
    g_asCarPortVt[VSlot(&C_Actor::GetSeatWeaponOrigin)] = fakeVt[1];
    g_asCarOrigVt[0x9C / 4] = reinterpret_cast<void *>(SC_SeatRounds);
    g_asCarOrigVt[0xA8 / 4] = reinterpret_cast<void *>(SC_SeatOrigin);
    RedirectOriginal(0x005e0dd0, FAKE(SC_SubmarineSetControls));
    g_pne = new S_TestNeutral;
    g_one = DaOrigController(g_pne, 0x140, HUMAN_CONTROLLER_NEUTRAL, 0x008113e8, g_oneVt, "neutral");
    // (The AI pair the scripted controller again.)
    g_pa = reinterpret_cast<S_TestAi *>(static_cast<C_AIController *>(g_psc));
    g_oa = g_osc;
}

// Part 39a's state, then: the human in the car now and then (a car, a static weapon or a submarine), the seat weapon,
// the hands item's behaviour, targets near, the pending meleeTracker alerts and the nearby actors.
void RandomScriptedB(bool aiming)
{
    RandomScripted(aiming);
    S_Sc *s = Sc();
    C_Human *h = g_h.human;
    auto *car = reinterpret_cast<C_Actor *>(g_asCarPort);
    static const int types[] = {ACTOR_CAR, ACTOR_STATIC_WEAPON, ACTOR_SUBMARINE, ACTOR_CAR};
    const int type = types[I(0, 3)];
    car->m_type = static_cast<E_ActorType>(type);
    std::memcpy(g_asCarOrig + 0x18, &type, 4);
    // (Firing from a seat in the vehicle, as AttackActor / AttackFrame set it: both read it unchecked.)
    h->m_vehicle = P(0.4f) || s->*DaGet(ScFireFromSeat()) ? car : nullptr;
    // (A firing has its target or frame: the aim reads the frame unchecked, both sides.)
    if (!(s->*DaGet(ScFireTarget())) && !(s->*DaGet(ScFireFrame())))
        s->*DaGet(ScFireFrame()) = g_asThirdFrame;
    h->m_seat = I(0, 3);
    void *ov = h->m_vehicle ? static_cast<void *>(g_asCarOrig) : nullptr;
    std::memcpy(o_human + 0x160, &ov, 4);
    std::memcpy(o_human + 0x164, &h->m_seat, 4);
    // The seat weapon: its aim now and then toward the target (the turn's and the firing's tests).
    g_scSeat.rounds = P(0.2f) ? 0 : I(1, 30);
    g_scSeat.origin = h->GetFrame()->GetWorldPos() + S_vector(U(-1, 1), U(0, 2), U(-1, 1));
    if (P(0.4f)) {
        I3D_frame *to = P(0.5f) ? g_asThirdFrame : PortOther()->GetFrame();
        S_vector dir = to->GetWorldPos() - h->GetFrame()->GetWorldPos();
        dir.Normalize();
        g_asSeat.aim = (dir + (P(0.5f) ? S_vector(0, 0, 0) : S_vector(U(-0.1f, 0.1f), U(-0.1f, 0.1f), U(-0.1f, 0.1f)))).Normalized();
    }
    static const int behaviours[] = {ITEM_BEHAVIOUR_SIDEARM, ITEM_BEHAVIOUR_SMG, ITEM_BEHAVIOUR_RIFLE, ITEM_BEHAVIOUR_ASSAULT,
                                     ITEM_BEHAVIOUR_LMG, 0, 7};
    g_behaviour = behaviours[I(0, 6)];
    // The pending events (meleeTracker alerts and others) and the nearby actors (the humans' ids, gone ones, -1).
    C_HumanEvents &e = g_daEvents;
    // (Each a different event, as raised: the same one twice does not happen, and the original releases it twice.)
    {
        int order[DA_EVENTS];
        for (int k = 0; k < DA_EVENTS; ++k)
            order[k] = k;
        for (int k = DA_EVENTS; k > 1; --k)
            std::swap(order[k - 1], order[I(0, k - 1)]);
        for (int k = P(0.3f) ? 0 : I(1, 4); k > 0; --k)
            (e.*DaGet(EvPending())).push_back(&g_daEv[order[k]]);
    }
    for (int k = I(0, 3); k > 0; --k)
        (e.*DaGet(EvNearby())).push_back(P(0.2f) ? static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1)))->GetNetId()
                                                 : (P(0.5f) ? h : PortOther())->GetNetId());   // (known: the original reads one gone)
    g_scs.time = uint32_t(I(0, 1000000));
    // (The formation not led by the human: the original's FollowFormation returns a stale byte then.)
    if (s->*DaGet(ScFormation()) && ScFormationObj()->*DaGet(FmLeader()) == h) {
        ScFormationObj()->*DaGet(FmLeader()) = PortOther();
        void *oleader = ToOrig(PortOther());
        std::memcpy(g_scOrigFormation + 0x74, &oleader, 4);
    }
    g_script.activeIsOwner = P(0.6f);
    // (The movement idle or not: the network games' send mask.)
    g_h.movement->m_unknown1AC = P(0.5f) ? 0 : I(1, 3);
    g_h.movement->m_unknown9C0 = P(0.5f) ? 0 : I(1, 900);
    // (A tick that may fire: the weapon has its model (the shot dummy's frame), as part 36's firing tests.)
    if (aiming)
        g_cs.itemFrame = g_shdumParent;
    // (The active player is the human itself here, 0 m away: a stop distance below 0 now and then.)
    if (P(0.3f))
        s->*DaGet(ScStopDist2()) = U(-1, -0.1f);
    // A route that ends in the move now and then.
    g_da.routeEnds = P(0.3f);
    if (P(0.6f))
        s->m_move->flags |= S_AIMoveOrder::FLAG_ROUTE;
}

// The heard sounds (the port's vector, the original's at +0x2A8): emptied before, their contents compared after.
std::string HeardSoundsDiffer()
{
    const std::vector<S_RememberedSound *> &port = g_h.human->m_heardSounds;
    uint32_t v[4];
    std::memcpy(v, o_human + 0x2A8, 16);
    const size_t n = v[1] ? (v[2] - v[1]) / 4 : 0;
    if (n != port.size())
        return "heard sounds: " + std::to_string(port.size()) + " / " + std::to_string(n);
    for (size_t k = 0; k < n; ++k) {
        const auto *o = reinterpret_cast<const uint8_t *const *>(static_cast<uintptr_t>(v[1]))[k];
        const auto *p = reinterpret_cast<const uint8_t *>(port[k]);
        if (std::memcmp(o, p, 0x14) != 0 || std::memcmp(o + 0x18, p + 0x18, 4) != 0)
            return "heard sound " + std::to_string(k);
        void *source;
        std::memcpy(&source, p + 0x14, 4);
        if ((source ? ToOrig(source) : nullptr) != *reinterpret_cast<void *const *>(o + 0x14))
            return "heard sound " + std::to_string(k) + " source";
    }
    return "";
}

void TestScriptedB()
{
    std::printf("scripted: update, tick, seat weapon, firing, attack, alerts, neutral\n");
    SetUpScriptedB();
    C_Human *const humans[2] = {g_h.human, PortOther()};
    for (int i = 0; i < 12000 / QUICK; ++i) {
        const int which = I(0, 15);
        RandomScriptedB(which == 1 || which == 6 || which == 8);
        S_Sc *s = Sc();
        const int dt = I(0, 200);
        switch (which) {
        case 0:
            ScCall("Update", [&] { s->Update(dt); return 0; }, [&](void *) { OAi<void>(0x0058e1c0, dt); return 0; });
            break;
        case 1:
            ScCall("Tick", [&] { s->Tick(dt); return 0; }, [&](void *) { OAi<void>(0x0058e210, dt); return 0; });
            break;
        case 2:
            g_h.human->m_vehicle = reinterpret_cast<C_Actor *>(g_asCarPort);   // (the original reads the vehicle unchecked)
            {
                void *ov = g_asCarOrig;
                std::memcpy(o_human + 0x160, &ov, 4);
            }
            ScCall("ControlVehicle", [&] { s->ControlVehicle(); return 0; }, [&](void *) { OAi<void>(0x005911f0); return 0; });
            break;
        case 3: {
            I3D_frame *target = P(0.2f) ? nullptr : P(0.5f) ? g_asThirdFrame : PortOther()->GetFrame();
            ScCall("TurnAt", [&] { s->TurnAt(target); return 0; }, [&](void *) { OAi<void>(0x005904c0, static_cast<void *>(target)); return 0; });
            break;
        }
        case 4:
            ScCall("TurnSeatWeapon", [&] { (s->*DaGet(ScTurnSeatWeapon()))(); return 0; }, [&](void *) { OAi<void>(0x0058db20); return 0; });
            break;
        case 5: {
            const int time = I(-10, 3000), label = I(-1, 30);
            const bool fake = P(0.5f);
            if (P(0.5f)) {
                C_Actor *target = P(0.2f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 1)]);
                ScCall("AttackActor", [&] { s->AttackActor(target, time, fake, label); return 0; },
                       [&](void *) { OAi<void>(0x0058f1f0, target ? ToOrig(target) : nullptr, time, int(fake), label); return 0; });
            } else {
                I3D_frame *target = P(0.5f) ? g_asThirdFrame : g_h.frame;
                ScCall("AttackFrame", [&] { s->AttackFrame(target, time, fake, label); return 0; },
                       [&](void *) { OAi<void>(0x0058f260, static_cast<void *>(target), time, int(fake), label); return 0; });
            }
            break;
        }
        case 6:
            // (The fire target the tested human: its model has the bones the aim picks from.)
            s->*DaGet(ScFireTarget()) = P(0.3f) ? nullptr : g_h.human;
            if (!(s->*DaGet(ScFireTarget())) && !(s->*DaGet(ScFireFrame())))
                s->*DaGet(ScFireFrame()) = g_asThirdFrame;
            if (s->*DaGet(ScFireFromSeat())) {
                // (Firing from a seat: in the vehicle, as AttackActor / AttackFrame set it; both read it unchecked.)
                g_h.human->m_vehicle = reinterpret_cast<C_Actor *>(g_asCarPort);
                void *ov = g_asCarOrig;
                std::memcpy(o_human + 0x160, &ov, 4);
            }
            ScCall("UpdateFireTime", [&] { (s->*DaGet(ScUpdateFireTime()))(dt); return 0; }, [&](void *) { OAi<void>(0x0058e500, dt); return 0; });
            break;
        case 7: {
            // (In a seat: the original reads the vehicle unchecked; a fire target or frame.)
            g_h.human->m_vehicle = reinterpret_cast<C_Actor *>(g_asCarPort);
            void *ov = g_asCarOrig;
            std::memcpy(o_human + 0x160, &ov, 4);
            if (!(s->*DaGet(ScFireTarget())) && !(s->*DaGet(ScFireFrame())))
                s->*DaGet(ScFireFrame()) = g_asThirdFrame;
            if (P(0.5f)) {
                const bool scatter = P(0.5f);
                S_vector pd, od;
                ScCall("GetSeatAimDir",
                       [&] {
                           pd = (s->*DaGet(ScGetSeatAimDir()))(scatter);
                           return 0;
                       },
                       [&](void *) {
                           typedef float *(AC * T_Fn)(void *, float *, int);
                           float out[3];
                           OriginalAt<T_Fn>(0x0058e8b0)(g_osc, out, int(scatter));
                           od = S_vector(out[0], out[1], out[2]);
                           return 0;
                       });
                const bool same = std::memcmp(&pd, &od, 12) == 0;
                CHECK(same);
                static int shown;
                if (!same && shown++ < 3)
                    std::printf("  GetSeatAimDir: %g %g %g / %g %g %g\n", pd.x, pd.y, pd.z, od.x, od.y, od.z);
            } else {
                ScCall("AimAndFireSeatWeapon", [&] { (s->*DaGet(ScAimAndFireSeatWeapon()))(); return 0; },
                       [&](void *) { OAi<void>(0x0058ebd0); return 0; });
            }
            break;
        }
        case 8:
            // (The other human far now and then: the weapons' ranges, 30 to 300 m.)
            if (P(0.5f)) {
                const S_vector4 &own = g_h.human->m_position;
                const float yaw = U(0, 6.2831f), d = U(20, 330);
                PortOther()->m_position = {own.x + std::sin(yaw) * d, own.y, own.z + std::cos(yaw) * d, 1.0f};
                std::memcpy(g_origOther + 0x30, &PortOther()->m_position, 16);
            }
            ScCall("DecideAttack", [&] { (s->*DaGet(ScDecideAttack()))(); return 0; }, [&](void *) { OAi<void>(0x00591550); return 0; });
            break;
        case 9:
        case 10:
            ScCall("UpdateAttack", [&] { (s->*DaGet(ScUpdateAttack()))(dt); return 0; }, [&](void *) { OAi<void>(0x00591930, dt); return 0; });
            break;
        case 11: {
            g_h.human->m_heardSounds.clear();
            std::memset(o_human + 0x2A8, 0, 16);
            ScCall("ProcessMeleeTrackerAlerts", [&] { (s->*DaGet(ScProcessMeleeTrackerAlerts()))(); return 0; },
                   [&](void *) {
                       std::memset(o_human + 0x2A8, 0, 16);
                       OAi<void>(0x0058fd30);
                       return 0;
                   });
            const std::string why = HeardSoundsDiffer();
            CHECK(why.empty());
            static int shown;
            if (!why.empty() && shown++ < 3)
                std::printf("  ProcessMeleeTrackerAlerts: %s\n", why.c_str());
            g_h.human->m_heardSounds.clear();
            break;
        }
        default: {
            // The neutral controller (its own pair, the AI pair for the call).
            g_pa = reinterpret_cast<S_TestAi *>(static_cast<C_AIController *>(g_pne));
            g_oa = g_one;
            RandomAi();
            g_pa->m_human = g_h.human;
            // (The game's walking network: the original reads a missing one's ladder points.)
            GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_checkpointNet);
            {
                void *net = g_checkpointNet;
                std::memcpy(g_origGame + 0x54, &net, 4);
            }
            C_HumanController *previous = P(0.5f) ? nullptr : static_cast<C_HumanController *>(g_pc2);
            const int k = I(0, 2);
            const char *const names[] = {"NeutralAttach", "NeutralDetach", "NeutralUnknown114"};
            const S_vector dir(U(-1, 1), U(-1, 1), U(-1, 1));
            DaCall(
                names[k], [] {}, [] { return std::string(); }, [] {},
                [&] {
                    if (k == 0)
                        g_pne->Attach(g_h.human, previous);
                    else if (k == 1)
                        g_pne->Detach();
                    else
                        return int(g_pne->Unknown114(&dir));
                    return 0;
                },
                [&](void *) {
                    if (k == 0)
                        OAi<void>(0x005a42f0, ToOrig(g_h.human), previous ? ToOrig(previous) : nullptr);
                    else if (k == 1)
                        OAi<void>(0x005a4310);
                    else
                        return int(OAi<uint32_t>(0x005a42e0, &dir) & 0xFF);
                    return 0;
                });
            g_pa = reinterpret_cast<S_TestAi *>(static_cast<C_AIController *>(g_psc));
            g_oa = g_osc;
            break;
        }
        }
    }
}

void TestScripted()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    SetUpFireOrder();
    SetUpScripted();
    TestScriptedA();
    TestScriptedB();
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
}
}  // namespace
