// Scripts of actors that understand movement / behaviour commands:
// C_HumanScript (HUMAN_* commands) and C_MeleeTrackerScript (MELEE_TRACKER_* commands).
//
// Movement commands pass the current instruction pointer to the actor as a
// completion label; the actor calls C_Script::OnActionDone(label) when it
// arrives, which advances the script. Activities return their duration, which
// the script waits through its Delay timer.
#pragma once

#include "script/script.h"

class C_Human;
class C_MeleeTracker;

class C_HumanScript : public C_Script {                     // vtable 0x0080f714, 0x110 bytes
public:
    C_HumanScript(I3D_frame *frame, C_Human *human);        // 0x00421bc0 (DS 0x00443af0)
    bool ExecuteCommand(const S_ScriptCommand &cmd) override;   // 0x00421c00
    int GetKind() const override { return SCRIPT_KIND_HUMAN; }  // 0x00421bf0

private:
    C_Human *m_human;                                        // +0x10C
};

class C_MeleeTrackerScript : public C_Script {                       // vtable 0x0080f70c, 0x110 bytes
public:
    C_MeleeTrackerScript(I3D_frame *frame, C_MeleeTracker *meleeTracker);              // 0x00421930 (DS 0x00443790)
    bool ExecuteCommand(const S_ScriptCommand &cmd) override;   // 0x00421970
    int GetKind() const override { return SCRIPT_KIND_MELEE_TRACKER; }    // 0x00421960

private:
    C_MeleeTracker *m_meleeTracker;                                            // +0x10C
};
