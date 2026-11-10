// The squad's commands (the object 0x0089ad40, vtable 0x0080fb28; game/squad_commander.cpp,
// original source 0x004559e0..0x0045d040): single player's coordinator of the player's soldiers.
// It carries out the HUD's orders to the squad (follow, sneak, bound, hold, the combat modes,
// the formation and its spacing, take cover, come here ...), gives the commands on actors
// (doors, mines, vehicles, bodies, places ...) to the member best placed and equipped for them,
// runs the manoeuvres of several members (a grenade into a room, through a door, up a ladder),
// keeps the groups of members the HUD lists and voices the members' idle reports.
#pragma once

#include <cstdint>
#include <vector>

#include "ls3d/math.h"

class C_Actor;
class C_Human;
struct S_ActionCommand;

class C_SquadCommander {
public:
    // The groups of members (CollectGroup / HasGroupMembers / IsInGroup); Tick rebuilds the
    // stored ones from the player's team every tick (none without a selected soldier).
    enum : int {
        GROUP_NEAR = 0,                                 // within 40 m of the selected soldier, or busy with orders
        GROUP_IDLE_NEAR = 1,                            // no orders, the action finished, within 40 m
        GROUP_BUSY = 2,                                 // with orders
        GROUP_3 = 3,                                    // (never filled)
        GROUP_IDLE = 4,                                 // CollectGroup: the members of GROUP_NEAR without orders
        GROUP_ALL = 5,                                  // CollectGroup: every living member but the selected one
        STORED_GROUPS = 5,
    };
    // The manoeuvres of several members: the commands that start them (E_HumanCommandType
    // numbers), -1 none.
    enum : int {
        MANOEUVRE_NONE = -1,
        MANOEUVRE_LADDER = 0x03,                        // up a ladder (the use command on a ladder)
        MANOEUVRE_GRENADE_ROOM = 0x12,                  // a grenade through a door (opened first)
        MANOEUVRE_THROUGH_DOOR = 0x15,                  // two members through a door
    };
    // The formation's spacing (SetSpacing: the scale 0.75 * spacing + 1.5).
    static constexpr int MAX_SPACING = 7;

    C_SquadCommander();                                 // 0x00456780
    // (Not in the original's vtable: 0x00455a50 frees the vectors at exit.)
    virtual ~C_SquadCommander() = default;
    C_SquadCommander(const C_SquadCommander &) = delete;
    C_SquadCommander &operator=(const C_SquadCommander &) = delete;

    // The HUD's command (S_ActionCommand::action, E_SquadCommand): to the member it names or
    // the idle ones.
    virtual void ExecuteActionCommand(const S_ActionCommand &command);   // 0x00 0x00456020
    virtual int GetUnknown38() { return m_unknown38; }  // 0x04 0x00456850 (not set anywhere) (DS 0x0046f8f0)
    // `out` gets the members of `group` (GROUP_*).
    virtual void CollectGroup(std::vector<C_Human *> &out, int group);   // 0x08 0x004568d0
    virtual bool HasGroupMembers(int group);            // 0x0C 0x00456b80
    virtual bool IsInGroup(const C_Human *human, int group);   // 0x10 0x00456890
    // The members the HUD offers command `command` on `actor`: the idle ones within 40 m of
    // the selected soldier to use a vehicle or a ladder, go through a door or search a body;
    // the soldiers in a vehicle to leave it; none for the others.
    virtual void CollectFor(std::vector<C_Human *> &out, int command, C_Actor *actor, int value);   // 0x14 0x00456c20
    // The actor the squad turned on (OnFriendlyFire; 4 s).
    virtual C_Actor *GetSquadTarget() { return m_squadTarget; }   // 0x18 0x00456860
    // The player hurt a member too often: the members that see `attacker` drop their orders
    // and actions.
    virtual void OnFriendlyFire(C_Actor *attacker);     // 0x1C 0x00459500
    // The other living members wait 2.2 s before they look for their formation slots again.
    virtual void DelayFormationChecks();                // 0x20 0x00459490
    // The player made a noise after a quiet time: the members in combat mode 0 switch to 1.
    virtual void OnPlayerNoise();                       // 0x24 0x00459360
    // A command on an actor (E_HumanCommandType numbers; the squad's order queue and the HUD's
    // SQUAD_COMMAND_ON_ACTOR): by `member` or the idle member best placed and equipped for it
    // (none: the first says so); 0x12, 0x15 and the use of a ladder start a manoeuvre.
    // `quiet`: sneaking; `value`: the pick-up's item.
    virtual void ExecuteCommand(int command, C_Actor *actor, C_Human *member, bool quiet, int value);   // 0x28 0x00458e50
    // The squad whispers (single player: the player's quiet commands).
    virtual void SetWhispering(bool on) { m_whispering = on; }   // 0x2C 0x00459470
    virtual bool IsWhispering() { return m_whispering; }         // 0x30 0x00459480 (DS 0x004730b0)
    // The squad members following `leader` stop following it.
    virtual void ReleaseFollowers(C_Human *leader);     // 0x34 0x004593e0
    // `member` has nothing to do: it says so after a while (staggered, 300..500 ms apart).
    virtual void OnMemberIdle(C_Human *member);         // 0x38 0x00455af0
    // The spacing at its ends (the HUD's spacing buttons).
    virtual bool IsSpacingMax() { return m_spacing == MAX_SPACING; }   // 0x3C 0x004595c0
    virtual bool IsSpacingMin() { return m_spacing == 0; }             // 0x40 0x004595d0
    // The interface's other slots (no-ops and constants).
    virtual void Unknown44(C_Human * /*human*/) {}      // 0x44 0x0045b750
    virtual int Unknown48(C_Human * /*human*/) { return -1; }     // 0x48 0x0045b760 (DS 0x00475cd0)
    virtual bool Unknown4C(C_Human * /*human*/) { return false; }   // 0x4C 0x0045b770 (DS 0x00475cf0)
    virtual int Unknown50(C_Human * /*human*/) { return -1; }     // 0x50 0x0045b780 (DS 0x00475d10)
    virtual bool Unknown54(C_Human * /*human*/) { return true; }  // 0x54 0x0045b790 (DS 0x00475d30)
    virtual void Unknown58(C_Human * /*human*/, void * /*out*/) {}   // 0x58 0x0045b7a0
    virtual void Unknown5C(C_Human * /*human*/, void * /*out*/) {}   // 0x5C 0x0045b7b0
    // Flags the HUD reads (only Reset and the timers of Tick clear them; nothing sets them).
    virtual bool GetFlag41() { return m_flag41; }       // 0x60 0x004567e0 (DS 0x0046f810)
    virtual bool GetFlag42() { return m_flag42; }       // 0x64 0x004567f0 (DS 0x0046f830)
    virtual bool GetFlag3D() { return m_flag3D; }       // 0x68 0x00456800 (DS 0x0046f850)
    virtual bool GetFlag3E() { return m_flag3E; }       // 0x6C 0x00456810 (DS 0x0046f870)
    virtual bool GetFlag3F() { return m_flag3F; }       // 0x70 0x00456820 (DS 0x0046f890)
    virtual bool GetFlag3C() { return m_flag3C; }       // 0x74 0x00456830 (DS 0x0046f8b0)
    virtual bool GetFlag40() { return m_flag40; }       // 0x78 0x00456840 (DS 0x0046f8d0)
    // Mission start / restart.
    virtual void Reset();                               // 0x7C 0x00456440
    // Every game tick (the team manager's): the timers, the groups, the idle reports.
    virtual void Tick(int timeDelta);                   // 0x80 0x004564a0

    // A member arrived where a manoeuvre sent it (S_SquadAction::notifyModule): when all did,
    // the next step.
    void OnOrderMoveDone();                             // 0x00456f00 (__fastcall)

private:
    // The formation's scale from the spacing.
    void UpdateScale();                                 // 0x004563f0
    // Applies the HUD's command to `member` (one without orders; with them for 0x1B..0x20).
    static void ApplyOrder(C_Human *member, const S_ActionCommand &command);   // 0x00455d40
    // SQUAD_COMMAND_TAKE_COVER: the members take cover against the direction given (the cover
    // points nearest to 5 slots of a wedge 2 m ahead of the selected soldier).
    void TakeCover(const S_ActionCommand &command, bool quiet);   // 0x00459740
    // SQUAD_COMMAND_MOVE_NEAR: the members go to their places around the point (out of the
    // static guns, the drivers drive).
    void MoveNear(const S_ActionCommand &command, bool quiet);    // 0x0045b890
    // Keeps the members of `members` that should do `command` on `actor` (one, for most;
    // `value` goes on to PickMember).
    static void FilterMembers(std::vector<C_Human *> &members, int command, C_Actor *actor, int value);   // 0x0045bb20
    // The member nearest to `actor` able to do `command` (null: none; `value` is not used).
    static C_Human *PickMember(std::vector<C_Human *> &members, int command, C_Actor *actor, int value);   // 0x0045bd10
    // Sorts `members` by the distance to `pos` (nearest first; but see the .cpp).
    static void SortByDistance(std::vector<C_Human *> &members, const S_vector &pos);   // 0x00459180 __stdcall
    // Drops the members whose action is going on.
    static void RemoveBusy(std::vector<C_Human *> &members);   // 0x004592e0
    // Sends `members` on manoeuvre `kind` at `actor` (a door, a ladder: its two walking points).
    void StartManoeuvre(int kind, C_Actor *actor, bool quiet, const std::vector<C_Human *> &members);   // 0x00457820

    // (+0x04..+0x2F not used.)
    int m_timer3C = 0;                                  // +0x30 ms until m_flag3C clears
    int m_timer40 = 0;                                  // +0x34 ms until m_flag40 clears
    int m_unknown38 = 0;                                // +0x38
    bool m_flag3C = false;                              // +0x3C
    bool m_flag3D = false;                              // +0x3D
    bool m_flag3E = false;                              // +0x3E
    bool m_flag3F = false;                              // +0x3F
    bool m_flag40 = false;                              // +0x40
    bool m_flag41 = false;                              // +0x41
    bool m_flag42 = false;                              // +0x42
    bool m_whispering = false;                          // +0x43
    int m_unknown44 = 5000;                             // +0x44 (only set)
    std::vector<C_Human *> m_groups[STORED_GROUPS];     // +0x48 (0x10 bytes each)
    C_Actor *m_squadTarget = nullptr;                   // +0x98 (OnFriendlyFire)
    int m_squadTargetTime = 0;                          // +0x9C ms
    // The manoeuvre going on: its stage (0 the members going to their places, 1 the next
    // step), the members still to arrive, the places of its two kinds of steps (MANOEUVRE_
    // THROUGH_DOOR and MANOEUVRE_GRENADE_ROOM: +0xB0 / +0xC0; MANOEUVRE_LADDER: +0xD0 / +0xE0;
    // the far side of the door / the ladder and the way through), the actor, the members.
    int m_manoeuvre = MANOEUVRE_NONE;                   // +0xA0
    int m_stage = 0;                                    // +0xA4
    int m_waiting = 0;                                  // +0xA8
    S_vector4 m_doorPos = {};                           // +0xB0
    S_vector4 m_doorDir = {};                           // +0xC0
    S_vector4 m_ladderPos = {};                         // +0xD0
    S_vector4 m_ladderDir = {};                         // +0xE0
    C_Actor *m_manoeuvreActor = nullptr;                // +0xF0
    std::vector<C_Human *> m_manoeuvreMembers;          // +0xF4
    // (+0x104..+0x10F not used.)
    S_vector4 m_formationScale = {};                    // +0x110 the selected soldier's formation's scale
    int m_spacing = 3;                                  // +0x120 0..MAX_SPACING
};
C_SquadCommander *GetSquadCommander();                  // 0x004563e0 (&g_squadCommander 0x0089ad40)
