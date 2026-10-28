// C_Script — one running instance of a mission script (*.scr).
//
// A script is compiled (script_compiler.cpp) into a flat list of commands plus
// variable tables; this class interprets it. Every command refers to its
// arguments by index into the variable tables (frames, numbers, strings,
// expressions). Control flow (If/Else/Whenever/event handlers) works on "blocks":
// a block is a OP_Block command whose single argument is the number of commands
// it spans, followed by '{', the body and '}'.
//
// Scripts only run on the server side. Each script normally belongs to a frame
// (the frame's S_FrameData points back at it); "global" scripts have no frame.
// Human and meleeTracker actors get the derived C_HumanScript / C_MeleeTrackerScript, which add the
// actor-control commands (HUMAN_*, MELEE_TRACKER_*).
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "script/script_opcodes.h"

class C_chunk;
class I3D_frame;
class I3D_model;
class C_ScriptExpression;

// A compiled command: opcode + argument variable indices (GetScriptArgCount() of them).
struct S_ScriptCommand {                               // 8 bytes
    int opcode = 0;
    int *args = nullptr;

    int Arg(int i) const { return args[i]; }
    void SetArgs(const int *values);                   // 0x00414610
    void FreeArgs();                                   // 0x00414660
};

// Integer variable or literal. Literals are not written to saved games.
struct S_ScriptNumber {                                // 8 bytes
    int value = 0;
    bool isConstant = false;
};

// Entry point of a handler that is selected by a number (signal / cutscene id).
struct S_ScriptHandler {                               // 8 bytes
    int id = -1;
    int label = -1;                                    // command index of the handler
};

// A Whenever block: its condition is tested every `period` ms while enabled.
struct S_ScriptWhenever {                              // 0x14 bytes
    int expression = 0;                                // index into the expression table
    int label = 0;                                     // command index of the Whenever's block
    bool enabled = true;
    int period = 500;                                  // ms
    int elapsed = 0;                                   // ms since the last test
};

enum E_ScriptKind {
    SCRIPT_KIND_GENERIC = 0,
    SCRIPT_KIND_HUMAN = 1,
    SCRIPT_KIND_MELEE_TRACKER = 2,
};

class C_Script {
public:
    // `global`: the script is not attached to (and does not keep a reference on) `frame`.
    C_Script(I3D_frame *frame, bool global);           // 0x0041dcb0
    virtual ~C_Script();                               // 0x0041df20

    // Executes one command; returns false for opcodes this class does not implement.
    virtual bool ExecuteCommand(const S_ScriptCommand &cmd);   // 0x00416e10 (vtable +0x00)
    virtual int GetKind() const { return SCRIPT_KIND_GENERIC; } // 0x0041df10 (vtable +0x04)

    // Runs the script for one game tick.
    void Tick(int timeDelta);                          // 0x004167b0
    // Rewinds to the ScriptStartPoint and clears the per-run flags.
    void Restart();                                    // 0x0041d8d0
    // Ends the script (EndScript, Return with an empty call stack, death without OnDeath).
    void Stop();                                       // 0x00416db0
    // Frees the compiled program and all variables.
    void Clear();                                      // 0x0041e140

    bool SaveGame(C_chunk &chunk) const;               // 0x004147d0
    bool LoadGame(C_chunk &chunk);                     // 0x00414e30

    // --- events --------------------------------------------------------------
    void SendSignal(int signal);                       // 0x00416610
    void OnDeath();                                    // 0x0041d920
    void OnUse(int unused);                            // 0x0041d960 (callers pass 1 / 0)
    void OnHit();                                      // 0x0041dbb0
    void OnAlarm();                                    // 0x0041da20
    bool OnAlarmDone();                                // 0x0041da80
    // The script has an OnAlarmDone block (the enemy AI goes back to it after the alarm).
    bool HasOnAlarmDone() const { return m_onAlarmDone != -1; }
    void OnGlobalAlarm();                              // 0x0041db50
    // Runs the OnExit block synchronously (the script is about to be destroyed).
    void OnExit();                                     // 0x0041db80
    bool OnCutscene(int cutscene);                     // 0x0041d9b0
    bool OnCutsceneDone(int cutscene);                 // 0x0041dae0
    // Completion callbacks of actor actions started by the command at `label`.
    void OnActionDone(int label);                      // 0x00416d50 (also 0x00416d70, 0x00416d90)

    // --- building (used by the compiler) ----------------------------------------
    int AddCommand(S_ScriptCommand *cmd);              // 0x00416700, returns its index
    void AddExpression(C_ScriptExpression *expr);      // 0x00416750
    bool AddSignalHandler(int signal, int label);      // 0x00415fe0
    bool AddCutsceneHandler(int cutscene, int label);  // 0x004161f0
    bool AddCutsceneDoneHandler(int cutscene, int label);   // 0x00416400

    // --- state -------------------------------------------------------------------
    I3D_frame *GetFrame() const { return m_frame; }
    bool IsFinished() const { return m_finished; }
    bool IsInCutsceneHandler() const { return m_inCutsceneHandler; }
    // Clears and returns the "HUD hidden by this script's camera" flags (EndCutscene).
    bool TakeHudHidden() { const bool v = m_hudHiddenByScript; m_hudHiddenByScript = false; return v; }
    bool TakeHudPanelHidden() { const bool v = m_hudPanelHiddenByScript; m_hudPanelHiddenByScript = false; return v; }
    int GetInstructionPointer() const { return m_ip; }
    // Microseconds the last Tick() took (profiling display).
    int GetLastTickTime() const { return m_lastTickTime; }

    // Built-in condition function `function` with the given operands (0x0041e3f0).
    int CallFunction(int function, const int *argTypes, const int *args);

    // Literal tables filled by the compiler; return the new index.
    int AddNumber(int value, bool isConstant = false)
    {
        m_numbers.push_back(S_ScriptNumber{value, isConstant});
        return static_cast<int>(m_numbers.size()) - 1;
    }
    int AddString(const C_str &text)
    {
        m_strings.push_back(text);
        return static_cast<int>(m_strings.size()) - 1;
    }

    // Variable access (indices come from command arguments).
    I3D_frame *&FrameVar(int i) { return m_frames[i]; }
    int &NumberVar(int i) { return m_numbers[i].value; }
    bool BoolVar(int i) const { return m_numbers[i].value != 0; }
    C_str &StringVar(int i) { return m_strings[i]; }
    C_ScriptExpression *Expression(int i) const { return m_expressions[i]; }

protected:
    // Moves to the next command and keeps running (0x00416cf0).
    void Advance();
    // Executes the block following the instruction pointer immediately (0x0041dc00).
    void ExecuteBlock();
    // Jumps to `label` and keeps running.
    void JumpTo(int label) { m_ip = label; m_running = true; }
    // Skips the block that follows the current command (If false, Else, handlers).
    // Returns false if that leaves the program.
    bool SkipBlock();
    // Stores `frame` in a frame variable, releasing the previous value.
    void SetFrameVar(int index, I3D_frame *frame, bool addRef = true);
    int CommandCount() const { return static_cast<int>(m_commands.size()); }

    bool m_signalsDisabled = false;                    // +0x004 DisableSignals
    bool m_alarmsDisabled = false;                     // +0x005 DisableAlarms
    bool m_wheneversDisabled = false;                  // +0x006 DisableWhenevers
    bool m_hudHiddenByScript = false;                  // +0x007 FRM_SetActiveCamera hid the HUD
    bool m_hudPanelHiddenByScript = false;             // +0x008 ... and the HUD panel
    bool m_inCutsceneHandler = false;                  // +0x009
    bool m_unknown00A = false;                         // +0x00A
    C_str m_name;                                      // +0x00B
    uint32_t m_pendingSignals = 0;                     // +0x010 bit per received signal
    uint32_t m_disabledSignals = 0;                    // +0x014 bit per disabled signal
    int m_delay = 0;                                   // +0x018 ms left of a Delay
    int m_startPoint = -1;                             // +0x01C ScriptStartPoint
    bool m_global;                                     // +0x020
    int m_waitAnimLabel = -1;                          // +0x024 FRM_SetAnimationDelayed in progress
    I3D_model *m_waitAnimModel = nullptr;              // +0x028
    std::vector<S_ScriptHandler *> m_signalHandlers;   // +0x02C OnSignal
    std::vector<S_ScriptHandler *> m_cutsceneHandlers; // +0x03C OnCutscene
    std::vector<S_ScriptHandler *> m_cutsceneDoneHandlers;   // +0x04C OnCutsceneDone
    std::vector<S_ScriptCommand *> m_commands;         // +0x05C
    std::vector<C_ScriptExpression *> m_expressions;   // +0x06C
    std::vector<S_ScriptWhenever *> m_whenevers;       // +0x07C
    std::vector<I3D_frame *> m_frames;                 // +0x08C (each holds a reference)
    std::vector<S_ScriptNumber> m_numbers;             // +0x09C
    std::vector<C_str> m_strings;                      // +0x0AC
    std::vector<int> m_callStack;                      // +0x0BC Gosub return addresses
    C_str m_fileName;                                  // +0x0CC
    int m_lastTickTime = 0;                            // +0x0D0 us
    bool m_finished = false;                           // +0x0D4
    int m_ip = -1;                                     // +0x0D8 instruction pointer
    // Event handler entry points (command index, -1 = none).
    int m_onDeath = -1;                                // +0x0DC
    int m_onUse = -1;                                  // +0x0E0
    int m_onHit = -1;                                  // +0x0E4
    int m_onAlarm = -1;                                // +0x0E8
    int m_onAlarmDone = -1;                            // +0x0EC
    int m_unusedHandler = -1;                          // +0x0F0
    int m_onGlobalAlarm = -1;                          // +0x0F4
    int m_onExit = -1;                                 // +0x0F8
    int m_wheneverTimer = 0;                           // +0x0FC ms not yet spent on Whenever tests
    I3D_frame *m_frame;                                // +0x100
    bool m_running = false;                            // +0x104 execute the command at m_ip
    // State byte of the actor scripts (C_HumanScript, C_MeleeTrackerScript); saved here.
    bool m_actorFlag = false;                          // +0x108

    friend class C_ScriptCompiler;
    friend class C_ScriptedController;                 // AdvanceScript
    friend class C_MeleeTrackerAI;                              // MoveTo advances the script when no path
    friend class C_Bike;                                // Update advances the rider's at the ride's end
};

// Runs the OnHit block of the script on `frame`, if it has one (shots hitting scene frames).
void NotifyFrameHit(I3D_frame *frame);                  // 0x00427700
