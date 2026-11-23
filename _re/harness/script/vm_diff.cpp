// Script part 2: the mission script interpreter (script.cpp's Tick, its events and control flow; script_vm.cpp's
// script-level commands; script_expression.cpp's Evaluate / ToString; script_functions.cpp's script-level functions)
// against the original executable's. Both compile the same program (sc_shared.inc's fakes), then run it side by side:
// ticks of random lengths and the events (signals, OnDeath, OnUse, OnHit, OnAlarm, OnAlarmDone, OnGlobalAlarm,
// OnCutscene, OnCutsceneDone, OnActionDone, Stop, Restart, OnExit, the mission's start, the restart flag, the role),
// the scripts' whole state (and the save-game values, a sentinel on each side of them, and the results of the real
// commands each side ran) compared after each one. The game commands and functions are faked alike on both sides (a
// command: Advance and true, waiting (false) or Advance and false; a function: a value; each from a sequence both
// sides draw in the same order); the script-level ones run for real. Programs: a few crafted ones (ticks only), random
// programs from a grammar, every game script. The game the interpreter reaches is link stubs (vm_stubs.s). MinGW;
// build_vm.bat; runs vm_diff.exe ("vm_diff: N checks, 0 failures").
// Env: as sc_diff (HD2_SC_DATA / HD2_UNPACKED, HD2_SC_ONLY, HD2_SC_DUMP, HD2_SC_SEED, HD2_SC_FIRST_CHANCE), and
// HD2_VM_QUICK (fewer programs and steps), HD2_VM_NO_CORPUS (the random programs alone), HD2_VM_STEPS (steps per
// program), HD2_VM_PROGRAMS (random programs), HD2_VM_SHOW (a random program's number: its text printed and its steps),
// HD2_VM_SHOW_FAILS (the programs not compiling or skipped named), HD2_VM_FILE (one program from a file, run alone).
#define HARNESS_NAME "vm_diff"
#include "sc_shared.inc"

#include "game/world.h"
#include "script/script_module.h"

namespace H {

// ---- the original's members ----
using T_Tick = void(TC *)(void *, int);
using T_VoidInt = void(TC *)(void *, int);
using T_BoolInt = bool(TC *)(void *, int);
using T_Bool = bool(TC *)(void *);
using T_Exec = bool(TC *)(void *, const void *);
using T_Call = int(TC *)(void *, int, const int *, const int *);
constexpr uint32_t O_TICK = 0x004167b0, O_RESTART = 0x0041d8d0, O_STOP = 0x00416db0, O_SEND_SIGNAL = 0x00416610,
                   O_ON_DEATH = 0x0041d920, O_ON_USE = 0x0041d960, O_ON_HIT = 0x0041dbb0, O_ON_ALARM = 0x0041da20,
                   O_ON_ALARM_DONE = 0x0041da80, O_ON_GLOBAL_ALARM = 0x0041db50, O_ON_EXIT = 0x0041db80,
                   O_ON_CUTSCENE = 0x0041d9b0, O_ON_CUTSCENE_DONE = 0x0041dae0, O_ON_ACTION_DONE = 0x00416d50,
                   O_ADVANCE = 0x00416cf0, O_EXECUTE = 0x00416e10, O_CALL_FUNCTION = 0x0041e3f0,
                   O_RESTARTING = 0x008693e0, O_SAVE_VALUES = 0x00868b3c, O_RAND = 0x007e6393;

// ---- the fakes both sides share: game commands and functions ----
// Each action draws a base; each side's n-th fake within the action uses (base, n): the same answers in the same order.
uint32_t g_fakeBase;
int g_fakeCount;
int g_fakeCommands, g_fakeFunctions, g_realCommands, g_realFunctions;
uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a ^ (b * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}
uint32_t NextFake() { return Mix(g_fakeBase, uint32_t(g_fakeCount++)); }

// The commands run for real: the script's own (flow, variables, whenevers, signals, the save-game values, the debug
// commands stripped from the release build, the declarations and handlers (no case: false)). SendSignal stops a script
// on a frame variable that is null (always here: the frame commands are faked), so it is real only one time in four.
bool RealCommand(int opcode)
{
    switch (opcode) {
    case OP_SendSignal:
        return NextFake() % 4 == 0;
    case OP_EndScript: case OP_If: case OP_Else: case OP_BlockBegin: case OP_BlockEnd: case OP_Whenever:
    case OP_Evaluate: case OP_SetWhenever: case OP_SetWheneverTime: case OP_DisableSignals: case OP_DisableAlarms:
    case OP_DisableWhenevers: case OP_EnableSignal: case OP_SaveGameValue: case OP_FRM_TimerSetOn: case OP___Log:
    case OP___LogNumber: case OP___LogFrame: case OP___SetupAutoLogging: case OP_Integer: case OP_Real: case OP_Bool:
    case OP_String: case OP_Enum: case OP_Frame: case OP_Label: case OP_GoTo: case OP_Gosub: case OP_Return:
    case OP_Delay: case OP_OnSignal: case OP_ScriptStartPoint: case OP_OnDeath: case OP_OnUse: case OP_OnAlarm:
    case OP_OnHit: case OP_OnGlobalAlarm: case OP_OnExit: case OP_OnAlarmDone: case OP_OnCutsceneDone:
    case OP_OnCutscene: case OP_Block:
        return true;
    default:
        return opcode < 1 || opcode > OP_LAST_COMMAND;
    }
}
// A faked command: 0 Advance and true (done), 1 false (an action under way: OnActionDone ends it), 2 Advance and false.
int FakeCommand() { return int(NextFake() % 3); }
bool RealFunction(int function)
{
    return function == SFUNC_SIGNAL_RECEIVED || function == SFUNC_RANDOM_INT || function == SFUNC_LOAD_GAME_VALUE;
}
int FakeFunction()
{
    static const int values[] = {0, 0, 1, 1, 2, 3, 5, -1, 100};
    return values[NextFake() % 9];
}

// The original's side: a vtable whose ExecuteCommand slot is this, and its CallFunction redirected here (the real one
// through a trampoline: its first six bytes, then a jump past them).
// (The real commands' results, which Tick and ExecuteBlock drop and the actor scripts use, logged per action on each
// side and compared with the state.)
std::string g_origExec, g_portExec;
uint32_t g_origVtable[4];
bool TC O_ExecuteCommand(void *self, const int *cmd)
{
    if (!cmd || RealCommand(cmd[0])) {
        ++g_realCommands;
        const bool r = OriginalAt<T_Exec>(O_EXECUTE)(self, cmd);
        g_origExec += F(" %d:%d", cmd ? cmd[0] : -1, int(r));
        return r;
    }
    ++g_fakeCommands;
    switch (FakeCommand()) {
    case 0: OriginalAt<T_Bool>(O_ADVANCE)(self); return true;
    case 1: return false;
    default: OriginalAt<T_Bool>(O_ADVANCE)(self); return false;
    }
}
T_Call g_origCallFunction;
int TC O_CallFunction(void *self, int function, const int *types, const int *args)
{
    if (RealFunction(function)) {
        ++g_realFunctions;
        return g_origCallFunction(self, function, types, args);
    }
    ++g_fakeFunctions;
    return FakeFunction();
}
int __cdecl O_Rand() { return std::rand(); }

}  // namespace H

// The port's side: a script whose ExecuteCommand fakes the game commands, and CallFunction wrapped.
class C_TestScript : public C_Script {
public:
    C_TestScript() : C_Script(nullptr, true) {}
    bool ExecuteCommand(const S_ScriptCommand &cmd) override
    {
        if (H::RealCommand(cmd.opcode)) {
            ++H::g_realCommands;
            const bool r = C_Script::ExecuteCommand(cmd);
            H::g_portExec += H::F(" %d:%d", cmd.opcode, int(r));
            return r;
        }
        ++H::g_fakeCommands;
        switch (H::FakeCommand()) {
        case 0: Advance(); return true;
        case 1: return false;
        default: Advance(); return false;
        }
    }
};
extern "C" {
int __attribute__((thiscall)) __real__ZN8C_Script12CallFunctionEiPKiS1_(C_Script *, int, const int *, const int *);
int __attribute__((thiscall)) __wrap__ZN8C_Script12CallFunctionEiPKiS1_(C_Script *self, int function, const int *types,
                                                                         const int *args)
{
    if (H::RealFunction(function)) {
        ++H::g_realFunctions;
        return __real__ZN8C_Script12CallFunctionEiPKiS1_(self, function, types, args);
    }
    ++H::g_fakeFunctions;
    return H::FakeFunction();
}
// The link stubs (vm_stubs.s) report a call to anything else and end the test.
void VmUnexpected(const char *what)
{
    std::printf("vm_diff: unexpected call: %s [%s]\n", what, H::g_current.c_str());
    std::exit(3);
}
}

uint32_t g_gameRole = GAME_ROLE_LOCAL;
bool g_scriptModuleRestarting;

namespace H {

// ---- division faults ----
// The original's Evaluate divides unchecked (a zero divisor, INT_MIN / -1); the port's checks the zero. A fault in an
// IDIV is counted and stepped over with a quotient of 0 (the port's answer for a zero divisor; INT_MIN / -1 faults in
// both), so the comparison goes on.
// And an expression whose operators the compiler dropped altogether ("a <= b": <= and >= are not binary operators to
// its parser, see script_expression.cpp) has children but no operator list: Evaluate and ToString read the null list
// and fault. Counted and stepped over to their "no such operator" path (the port's: the child skipped).
bool g_armDivide;
int g_divideFaults, g_nullOperatorFaults;
LONG WINAPI OnDivide(EXCEPTION_POINTERS *e)
{
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (g_armDivide && code == EXCEPTION_ACCESS_VIOLATION) {
        CONTEXT &c = *e->ContextRecord;
        if (c.Eip == 0x004116bd && c.Edx == 0) {         // Evaluate: MOV ECX,[EDX + EDI*4 - 4]
            c.Eip = 0x004117d6;
            ++g_nullOperatorFaults;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (c.Eip == 0x00411987 && c.Ecx == 0) {         // ToString: CMP [ECX + EBX*4 - 4],4
            c.Eip = 0x00411a4f;
            ++g_nullOperatorFaults;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (!g_armDivide || (code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW))
        return EXCEPTION_CONTINUE_SEARCH;
    const uint8_t *ip = reinterpret_cast<const uint8_t *>(e->ContextRecord->Eip);
    if (ip[0] != 0xF7 || ((ip[1] >> 3) & 7) != 7)
        return EXCEPTION_CONTINUE_SEARCH;
    const int mod = ip[1] >> 6, rm = ip[1] & 7;
    int length = 2;
    if (mod != 3 && rm == 4) {
        ++length;
        if (mod == 0 && (ip[2] & 7) == 5)
            length += 4;
    }
    if (mod == 0 && rm == 5)
        length += 4;
    else if (mod == 1)
        length += 1;
    else if (mod == 2)
        length += 4;
    e->ContextRecord->Eip += length;
    e->ContextRecord->Eax = 0;
    e->ContextRecord->Edx = 0;
    ++g_divideFaults;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// ---- the state compared ----
std::vector<std::string> OrigState(const uint8_t *b)
{
    std::vector<std::string> out;
    std::string flags = "flags";
    for (uint32_t off = 4; off <= 0xA; ++off)
        flags += F(" %d", int(b[off]));
    out.push_back(flags);
    out.push_back(F("signals %08x disabled %08x delay %d start %d global %d wait %d %d", At<uint32_t>(b, 0x10),
                    At<uint32_t>(b, 0x14), At<int>(b, 0x18), At<int>(b, 0x1C), int(b[0x20]), At<int>(b, 0x24),
                    int(At<uint32_t>(b, 0x28) != 0)));
    std::string calls = "calls";
    for (uint32_t q : OrigVec(b, 0xBC, 4))
        calls += F(" %d", At<int>(reinterpret_cast<void *>(q), 0));
    out.push_back(calls);
    std::string s = F("finished %d ip %d running %d actor %d timer %d handlers", int(b[0xD4]), At<int>(b, 0xD8),
                      int(b[0x104]), int(b[0x108]), At<int>(b, 0xFC));
    for (uint32_t off = 0xDC; off <= 0xF8; off += 4)
        s += F(" %d", At<int>(b, off));
    out.push_back(s);
    for (uint32_t q : OrigVec(b, 0x9C, 8))
        out.push_back(F("number %d", At<int>(reinterpret_cast<void *>(q), 0)));
    for (uint32_t q : OrigVec(b, 0xAC, 4))
        out.push_back("string " + OrigStr(At<uint32_t>(reinterpret_cast<void *>(q), 0)));
    for (uint32_t q : OrigVec(b, 0x7C, 4)) {
        const void *w = reinterpret_cast<void *>(At<uint32_t>(reinterpret_cast<void *>(q), 0));
        out.push_back(w ? F("whenever %d %d %d %d %d", At<int>(w, 0), At<int>(w, 4), int(At<uint8_t>(w, 8)), At<int>(w, 12),
                            At<int>(w, 16))
                        : std::string("whenever null"));
    }
    const int *values = OriginalAt<const int *>(O_SAVE_VALUES);
    std::string v = "values";
    for (int i = -1; i <= SAVE_GAME_VALUES; ++i)             // (the sentinels on each side included)
        if (values[i])
            v += F(" %d=%d", i, values[i]);
    out.push_back(v);
    return out;
}
std::vector<std::string> PortState(const C_Script &t)
{
    std::vector<std::string> out;
    out.push_back(F("flags %d %d %d %d %d %d %d", int(t.m_signalsDisabled), int(t.m_alarmsDisabled),
                    int(t.m_wheneversDisabled), int(t.m_hudHiddenByScript), int(t.m_hudPanelHiddenByScript),
                    int(t.m_inCutsceneHandler), int(t.m_unknown00A)));
    out.push_back(F("signals %08x disabled %08x delay %d start %d global %d wait %d %d", t.m_pendingSignals,
                    t.m_disabledSignals, t.m_delay, t.m_startPoint, int(t.m_global), t.m_waitAnimLabel,
                    int(t.m_waitAnimModel != nullptr)));
    std::string calls = "calls";
    for (int c : t.m_callStack)
        calls += F(" %d", c);
    out.push_back(calls);
    std::string s = F("finished %d ip %d running %d actor %d timer %d handlers", int(t.m_finished), t.m_ip,
                      int(t.m_running), int(t.m_actorFlag), t.m_wheneverTimer);
    for (int h : {t.m_onDeath, t.m_onUse, t.m_onHit, t.m_onAlarm, t.m_onAlarmDone, t.m_unusedHandler, t.m_onGlobalAlarm,
                  t.m_onExit})
        s += F(" %d", h);
    out.push_back(s);
    for (const S_ScriptNumber &n : t.m_numbers)
        out.push_back(F("number %d", n.value));
    for (const C_str &x : t.m_strings)
        out.push_back("string " + PortStr(x));
    for (const S_ScriptWhenever *w : t.m_whenevers)
        out.push_back(w ? F("whenever %d %d %d %d %d", w->expression, w->label, int(w->enabled), w->period, w->elapsed)
                        : std::string("whenever null"));
    std::string v = "values";
    for (int i = -1; i <= SAVE_GAME_VALUES; ++i)
        if (g_scriptModule.GetSaveGameValues()[i])
            v += F(" %d=%d", i, g_scriptModule.GetSaveGameValues()[i]);
    out.push_back(v);
    return out;
}

// ---- the actions ----
enum E_Action {
    A_TICK, A_SIGNAL, A_DEATH, A_USE, A_HIT, A_ALARM, A_ALARM_DONE, A_GLOBAL_ALARM, A_CUTSCENE, A_CUTSCENE_DONE,
    A_ACTION_DONE, A_STOP, A_RESTART, A_EXIT, A_MISSION, A_RESTARTING, A_ROLE, A_COUNT
};
const char *const ACTION_NAMES[A_COUNT] = {"Tick", "SendSignal", "OnDeath", "OnUse", "OnHit", "OnAlarm", "OnAlarmDone",
                                           "OnGlobalAlarm", "OnCutscene", "OnCutsceneDone", "OnActionDone", "Stop",
                                           "Restart", "OnExit", "mission", "restarting", "role"};
int g_actionCounts[A_COUNT];
struct S_Action {
    E_Action kind;
    int value;
};
bool g_ticksOnly;                                       // (the crafted programs: nothing interrupts them)
S_Action RandomAction(int ip)
{
    const int r = g_ticksOnly ? 0 : I(0, 99);
    if (r < 55) {
        static const int lengths[] = {0, 1, 16, 33, 100, 250, 499, 500, 501, 999, 1000, 1001, 1500, 3000};
        return {A_TICK, P(0.2) ? I(0, 2500) : lengths[I(0, int(sizeof lengths / sizeof *lengths) - 1)]};
    }
    if (r < 67)
        return {A_SIGNAL, P(0.1) ? I(-3, 70) : I(0, 6)};
    if (r < 69) return {A_DEATH, 0};
    if (r < 71) return {A_USE, I(0, 1)};
    if (r < 73) return {A_HIT, 0};
    if (r < 75) return {A_ALARM, 0};
    if (r < 77) return {A_ALARM_DONE, 0};
    if (r < 78) return {A_GLOBAL_ALARM, 0};
    if (r < 81) return {A_CUTSCENE, I(-1, 6)};
    if (r < 84) return {A_CUTSCENE_DONE, I(-1, 6)};
    if (r < 92) return {A_ACTION_DONE, P(0.7) ? ip : ip + I(-2, 2)};
    if (r < 93) return {A_STOP, 0};
    if (r < 95) return {A_RESTART, 0};
    if (r < 96) return {A_EXIT, 0};
    if (r < 97) return {A_MISSION, I(0, 1)};
    if (r < 98) return {A_RESTARTING, I(0, 1)};
    return {A_ROLE, P(0.5) ? int(GAME_ROLE_LOCAL) : int(GAME_ROLE_CLIENT)};
}
void SetMissionStarted(bool started)
{
    *reinterpret_cast<uint32_t *>(g_origGame + 0x28) = started ? 1000 : 0;
    reinterpret_cast<C_game *>(g_portGameBuf)->m_missionTime = started ? 1000 : 0;
}
void SetRestarting(bool on)
{
    *OriginalAt<uint8_t *>(O_RESTARTING) = on;
    g_scriptModuleRestarting = on;
}
void ApplyOrig(uint8_t *b, const S_Action &a)
{
    switch (a.kind) {
    case A_TICK: OriginalAt<T_Tick>(O_TICK)(b, a.value); break;
    case A_SIGNAL: OriginalAt<T_VoidInt>(O_SEND_SIGNAL)(b, a.value); break;
    case A_DEATH: OriginalAt<T_Bool>(O_ON_DEATH)(b); break;
    case A_USE: OriginalAt<T_VoidInt>(O_ON_USE)(b, a.value); break;
    case A_HIT: OriginalAt<T_Bool>(O_ON_HIT)(b); break;
    case A_ALARM: OriginalAt<T_Bool>(O_ON_ALARM)(b); break;
    case A_ALARM_DONE: OriginalAt<T_Bool>(O_ON_ALARM_DONE)(b); break;
    case A_GLOBAL_ALARM: OriginalAt<T_Bool>(O_ON_GLOBAL_ALARM)(b); break;
    case A_CUTSCENE: OriginalAt<T_BoolInt>(O_ON_CUTSCENE)(b, a.value); break;
    case A_CUTSCENE_DONE: OriginalAt<T_BoolInt>(O_ON_CUTSCENE_DONE)(b, a.value); break;
    case A_ACTION_DONE: OriginalAt<T_VoidInt>(O_ON_ACTION_DONE)(b, a.value); break;
    case A_STOP: OriginalAt<T_Bool>(O_STOP)(b); break;
    case A_RESTART: OriginalAt<T_Bool>(O_RESTART)(b); break;
    case A_EXIT: OriginalAt<T_Bool>(O_ON_EXIT)(b); break;
    default: break;
    }
}
// (The results of OnAlarmDone, OnCutscene and OnCutsceneDone are compared too: recorded here.)
std::string g_origResult, g_portResult;
void ApplyOrigWithResult(uint8_t *b, const S_Action &a)
{
    g_origResult.clear();
    if (a.kind == A_ALARM_DONE)
        g_origResult = F("result %d", int(OriginalAt<T_Bool>(O_ON_ALARM_DONE)(b)));
    else if (a.kind == A_CUTSCENE)
        g_origResult = F("result %d", int(OriginalAt<T_BoolInt>(O_ON_CUTSCENE)(b, a.value)));
    else if (a.kind == A_CUTSCENE_DONE)
        g_origResult = F("result %d", int(OriginalAt<T_BoolInt>(O_ON_CUTSCENE_DONE)(b, a.value)));
    else
        ApplyOrig(b, a);
}
void ApplyPort(C_TestScript &s, const S_Action &a)
{
    g_portResult.clear();
    switch (a.kind) {
    case A_TICK: s.Tick(a.value); break;
    case A_SIGNAL: s.SendSignal(a.value); break;
    case A_DEATH: s.OnDeath(); break;
    case A_USE: s.OnUse(a.value); break;
    case A_HIT: s.OnHit(); break;
    case A_ALARM: s.OnAlarm(); break;
    case A_ALARM_DONE: g_portResult = F("result %d", int(s.OnAlarmDone())); break;
    case A_GLOBAL_ALARM: s.OnGlobalAlarm(); break;
    case A_CUTSCENE: g_portResult = F("result %d", int(s.OnCutscene(a.value))); break;
    case A_CUTSCENE_DONE: g_portResult = F("result %d", int(s.OnCutsceneDone(a.value))); break;
    case A_ACTION_DONE: s.OnActionDone(a.value); break;
    case A_STOP: s.Stop(); break;
    case A_RESTART: s.Restart(); break;
    case A_EXIT: s.OnExit(); break;
    default: break;
    }
}

// ---- one program on both sides ----
int g_programs, g_notCompiling, g_compileFailures, g_skippedFreed, g_skippedOverrun, g_steps, g_dumpAll;
bool g_showSteps;
// Compiles `path` on both sides (compared as sc_diff does), then runs `steps` random actions, comparing after each.
void RunProgram(const std::string &path, int steps)
{
    ++g_programs;
    SetRole(GAME_ROLE_LOCAL);                           // (a client's compile crashes the original: AddCommand's -1)
    // The original's script: compiled, then its vtable's ExecuteCommand slot made the fake's.
    g_case = "original";
    g_opened.clear();
    uint8_t *block = static_cast<uint8_t *>(std::calloc(1, 0x200));
    OriginalAt<T_Ctor>(0x0041dcb0)(block, nullptr, 1);
    g_holdFrees = true;
    const bool ro = OriginalAt<T_Compile>(0x0042c850)(OriginalAt<void *>(0x008993f0), path.c_str(), block);
    S_Result r;
    r.orig = OrigDump(block, ro);
    r.openedOrig = g_opened;
    g_case = "port";
    g_opened.clear();
    C_TestScript *script = new C_TestScript;
    const bool rp = g_scriptCompiler.Compile(path.c_str(), script);
    r.port = PortDump(*script, rp);
    r.openedPort = g_opened;
    r.port.push_back("expressions");
    r.orig.push_back("expressions");
    const int freedBefore = g_freedNodes, overrunsBefore = g_overruns;
    PairExprs(r.orig, r.port, block, *script);
    const bool compiled = Compare(path + " (compiled)", r);
    if (!compiled)
        ++g_compileFailures;
    else if (g_freedNodes != freedBefore) {
        ++g_skippedFreed;                               // (GroupRange's use after free: the trees differ by design)
        if (std::getenv("HD2_VM_SHOW_FAILS"))
            std::printf("freed node: %s\n", g_current.c_str());
    }
    else if (g_overruns != overrunsBefore)
        ++g_skippedOverrun;
    else if (!ro || !rp) {
        ++g_notCompiling;                               // (both failed alike: nothing to run)
        if (std::getenv("HD2_VM_SHOW_FAILS"))
            std::printf("not compiling: %s (token \"%s\", line %d)\n", g_current.c_str(), g_scriptCompiler.m_token,
                        g_scriptCompiler.GetLine());
    } else {
        std::memcpy(g_origVtable, *reinterpret_cast<uint32_t **>(block), sizeof g_origVtable);
        g_origVtable[0] = reinterpret_cast<uint32_t>(&O_ExecuteCommand);
        *reinterpret_cast<uint32_t **>(block) = g_origVtable;
        // A fresh start: the save-game values cleared, the mission started, a server, then Restart.
        std::memset(OriginalAt<int *>(O_SAVE_VALUES), 0, SAVE_GAME_VALUES * sizeof(int));
        std::memset(g_scriptModule.GetSaveGameValues(), 0, SAVE_GAME_VALUES * sizeof(int));
        // (And a sentinel in the word on each side of the values: a write or read outside them shows.)
        for (int *v : {OriginalAt<int *>(O_SAVE_VALUES), g_scriptModule.GetSaveGameValues()}) {
            v[-1] = 0x5A5A;
            v[SAVE_GAME_VALUES] = 0x6B6B;
        }
        SetMissionStarted(true);
        SetRestarting(false);
        SetRole(GAME_ROLE_LOCAL);
        OriginalAt<T_Bool>(O_RESTART)(block);
        script->Restart();
        g_armDivide = true;
        for (int step = 0; step < steps; ++step) {
            const S_Action a = RandomAction(script->m_ip);
            ++g_actionCounts[a.kind];
            ++g_steps;
            if (a.kind == A_MISSION)
                SetMissionStarted(a.value != 0);
            else if (a.kind == A_RESTARTING)
                SetRestarting(a.value != 0);
            else if (a.kind == A_ROLE)
                SetRole(uint32_t(a.value));
            g_fakeBase = U32();
            const unsigned randSeed = U32();
            g_origExec = "exec";
            g_portExec = "exec";
            g_case = "original";
            g_fakeCount = 0;
            std::srand(randSeed);
            ApplyOrigWithResult(block, a);
            g_case = "port";
            g_fakeCount = 0;
            std::srand(randSeed);
            ApplyPort(*script, a);
            g_case = "";
            if (g_showSteps)
                std::printf("  step %d: %s %d -> ip %d\n", step, ACTION_NAMES[a.kind], a.value, script->m_ip);
            S_Result st;
            st.orig = OrigState(block);
            st.port = PortState(*script);
            st.orig.insert(st.orig.begin(), g_origResult);
            st.port.insert(st.port.begin(), g_portResult);
            st.orig.push_back(g_origExec);
            st.port.push_back(g_portExec);
            if (!Compare(path + F(" step %d (%s %d)", step, ACTION_NAMES[a.kind], a.value), st))
                break;                                  // (the rest would follow from the first difference)
        }
        g_armDivide = false;
        SetRole(GAME_ROLE_LOCAL);
        SetRestarting(false);
        SetMissionStarted(true);
    }
    // Freed on both sides (the original's vtable back first: its destructor is not called, Clear frees the program).
    delete script;
    g_case = "original";
    OriginalAt<T_Void>(0x0041e140)(block);
    ReleaseHeldFrees();
    std::free(block);
    g_case = "";
}

// ---- random programs from a grammar ----
struct S_Gen {
    std::string text;
    std::vector<std::string> labelsToDefine;            // defined once each, somewhere
    int labelCount = 0;
    int statements = 0;
    int nextWhenever = 3;                               // (w0..w2 at the top; more inside blocks)
};
const char *const INT_VARS[] = {"a", "b", "c", "d"};
std::string Literal()
{
    static const char *const literals[] = {"0", "1", "2", "3", "5", "7", "10", "100", "500", "-1"};
    return literals[I(0, 9)];
}
std::string Expression(int depth)
{
    static const char *const terms[] = {
        "a", "b", "c", "d", "0", "1", "2", "5", "_RandomInt(5)", "_RandomInt(a)", "_RandomInt(1)",
        "_SignalReceived(1)", "_SignalReceived(2)", "_SignalReceived(a)", "_LoadGameValue(3)", "_LoadGameValue(a)", "_LoadGameValue(255)", "_LoadGameValue(256)", "_LoadGameValue(-1)", "s", "f",
        "_GetNumLiveEnemies()", "_PlayerInRange(10)", "_EnemyInRange(f, 10)", "_GetAlarmType()", "_SeeEnemy()",
        "_AllEnemiesDead()",
    };
    // One precedence class per level: mixed ones only with parentheses (every grouping GroupRange makes deletes a node
    // the group keeps, the use after free sc_diff counts; those programs are not run). (A binary '-' is rare: the
    // compiler reads it as a sign and fails the expression, "c - c" included.)
    static const char *const classes[4][4] = {
        {"+", "+", "+", "+"}, {"*", "/", "*", "*"}, {"==", "!=", "<", ">"}, {"AND", "OR", "and", "or"}};
    const int cls = I(0, 3);
    std::string e;
    for (int n = I(1, 3), t = 0; t < n; ++t) {
        if (t) {
            const char *op = P(0.04) ? (P(0.5) ? "<=" : ">=") : P(0.003) ? "-" : classes[cls][I(0, 3)];
            e += std::string(" ") + op + " ";
        }
        if (P(0.1))
            e += P(0.5) ? "NOT " : "! ";
        if (depth < 3 && P(0.25))
            e += "(" + Expression(depth + 1) + ")";
        else
            e += terms[I(0, int(sizeof terms / sizeof *terms) - 1)];
    }
    return e;
}
std::string StringExpression()
{
    // (No "%d": the original's literal formatting reads a missing argument.)
    static const char *const parts[] = {"\"x\"", "s", "t", "a", "b", "\"y z\"", "\"\""};
    std::string e = parts[I(0, 6)];
    for (int n = I(0, 3); n > 0; --n)
        e += std::string(P(0.94) ? " + " : P(0.5) ? " - " : " <= ") + parts[I(0, 6)];
    return e;
}
void Statements(S_Gen &g, int depth, int count, const std::string &indent);
std::string Label(S_Gen &g) { return F("L%d", I(0, g.labelCount - 1)); }
void Statement(S_Gen &g, int depth, const std::string &indent)
{
    ++g.statements;
    std::string &t = g.text;
    if (!g.labelsToDefine.empty() && P(0.12)) {
        t += indent + "label " + g.labelsToDefine.back() + ":\r\n";
        g.labelsToDefine.pop_back();
        return;
    }
    const int r = I(0, 99);
    if (r < 18)
        t += indent + INT_VARS[I(0, 3)] + " = " + Expression(0) + ";\r\n";
    else if (r < 22)
        t += indent + (P(0.5) ? "s" : "t") + " = " + StringExpression() + ";\r\n";
    else if (r < 32 && depth < 3) {
        t += indent + "If (" + Expression(0) + ")\r\n" + indent + "{\r\n";
        Statements(g, depth + 1, I(0, 4), indent + "\t");
        t += indent + "}\r\n";
        if (P(0.35)) {
            t += indent + "Else\r\n" + indent + "{\r\n";
            Statements(g, depth + 1, I(0, 3), indent + "\t");
            t += indent + "}\r\n";
        }
    } else if (r < 36 && depth < 3) {
        t += indent + "block\r\n" + indent + "{\r\n";
        Statements(g, depth + 1, I(0, 5), indent + "\t");
        // (A Whenever inside a block: the interpreter's own Whenever case, reached through ExecuteBlock.)
        if (P(0.3)) {
            t += indent + F("\tWhenever w%d (", g.nextWhenever++) + Expression(0) + ")\r\n" + indent + "\t{\r\n";
            Statements(g, depth + 2, I(0, 2), indent + "\t\t");
            t += indent + "\t}\r\n";
            Statements(g, depth + 1, I(0, 2), indent + "\t");
        }
        t += indent + "}\r\n";
    } else if (r < 42)
        t += indent + "goto " + Label(g) + ";\r\n";
    else if (r < 47)
        t += indent + "Gosub " + Label(g) + ";\r\n";
    else if (r < 50)
        t += indent + "Return;\r\n";
    else if (r < 57)
        t += indent + "Delay(" + (P(0.2) ? std::string(INT_VARS[I(0, 3)]) : Literal()) + ");\r\n";
    else if (r < 61)
        t += indent + F("SetWhenever(w%d, %d);\r\n", I(0, 2), I(0, 1));
    else if (r < 63)
        // (The name as written: a quoted one names a Whenever declared with the quotes.)
        t += indent + F("SetWheneverTime(w%d, %d);\r\n", I(0, 2), P(0.5) ? I(0, 2000) : 500);
    else if (r < 66)
        t += indent + F("%s(%d);\r\n", P(0.5) ? "DisableSignals" : P(0.5) ? "DisableAlarms" : "DisableWhenevers", I(0, 1));
    else if (r < 68)
        t += indent + F("EnableSignal(%d, %d);\r\n", I(0, 5), I(0, 1));
    else if (r < 70)
        t += indent + F("SendSignal(f, %d);\r\n", I(0, 5));
    else if (r < 73)
    {
        // (A position out of the 256 values now and then: both check it.)
        static const int outside[] = {-1, 255, 256, 300};
        t += indent + F("SaveGameValue(%d, ", P(0.15) ? outside[I(0, 3)] : I(0, 5)) +
             (P(0.5) ? Literal() : std::string(INT_VARS[I(0, 3)])) + ");\r\n";
    }
    else if (r < 75)
        t += indent + "EndScript();\r\n";
    else if (r < 78) {
        // The debug commands (stripped from the release build).
        static const char *const debug[] = {"__Log(\"x\");", "__LogNumber(\"x\", a);", "__LogFrame(\"x\", f);",
                                            "FRM_TimerSetOn(1, 500);", "__SetupAutoLogging(1, \"x.txt\");"};
        t += indent + debug[I(0, 4)] + "\r\n";
    } else {
        static const char *const game[] = {"HUMAN_Suspend(1);", "HUMAN_SetAlarm(0);", "FRM_SetOn(f, 1);",
                                           "SetAlarmType(4, 0);", "Printf(\"x\");", "PrintfNumber(\"n\", a);",
                                           "FRM_FindFrame(f, \"x\");", "HUMAN_SetOptimized(1);"};
        t += indent + game[I(0, 7)] + "\r\n";
    }
}
void Statements(S_Gen &g, int depth, int count, const std::string &indent)
{
    for (int i = 0; i < count; ++i)
        Statement(g, depth, indent);
}
std::string RandomProgram()
{
    S_Gen g;
    g.labelCount = I(1, 5);
    for (int i = 0; i < g.labelCount; ++i)
        g.labelsToDefine.push_back(F("L%d", i));
    std::string &t = g.text;
    t += F("Integer a = %d;\r\nInteger b;\r\nInteger c = %d;\r\nInteger d;\r\nString s = \"q\";\r\nString t;\r\nFrame f;\r\n",
           I(-2, 5), I(0, 3));
    if (P(0.15))
        t += "ScriptStartPoint:\r\n";
    // The whenevers first (SetWhenever names one the compiler has seen), then the code.
    for (int w = 0; w < 3; ++w) {
        t += F("Whenever w%d (", w) + Expression(0) + ")\r\n{\r\n";
        Statements(g, 1, I(0, 3), "\t");
        t += "}\r\n";
    }
    Statements(g, 0, I(2, 12), "");
    // The handlers, in a random order, each followed by more code (a label may land in one).
    const int pieces = I(1, 7);
    for (int p = 0; p < pieces; ++p) {
        const int k = I(0, 10);
        static const char *const handlers[] = {"OnDeath()", "OnUse()", "OnHit()", "OnAlarm()", "OnAlarmDone()",
                                               "OnGlobalAlarm()", "OnExit()"};
        if (k < 7)
            t += handlers[k];
        else if (k == 7 || k == 8)
            t += F("OnSignal(%d)", I(0, 5));
        else if (k == 9)
            t += F("OnCutscene(%d)", I(0, 5));
        else
            t += F("OnCutsceneDone(%d)", I(0, 5));
        t += "\r\n{\r\n";
        Statements(g, 1, I(0, 5), "\t");
        t += "}\r\n";
        if (P(0.5))
            Statements(g, 0, I(0, 3), "");
    }
    while (!g.labelsToDefine.empty()) {
        t += "label " + g.labelsToDefine.back() + ":\r\n";
        g.labelsToDefine.pop_back();
        Statements(g, 0, I(0, 2), "");
    }
    return t;
}

void SetUpVm()
{
    // CallFunction: the first six bytes (PUSH EBP; MOV EBP,ESP; AND ESP,-16) copied into a trampoline that jumps past them.
    const uint8_t *f = OriginalAt<const uint8_t *>(O_CALL_FUNCTION);
    static const uint8_t prologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};
    if (std::memcmp(f, prologue, 6) != 0) {
        std::printf("vm_diff: CallFunction's prologue is not the expected one\n");
        std::exit(2);
    }
    uint8_t *tramp = static_cast<uint8_t *>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    std::memcpy(tramp, prologue, 6);
    tramp[6] = 0xE9;
    const int32_t rel = int32_t(O_CALL_FUNCTION + 6) - int32_t(reinterpret_cast<uintptr_t>(tramp) + 11);
    std::memcpy(tramp + 7, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), tramp, 64);
    g_origCallFunction = reinterpret_cast<T_Call>(tramp);
    RedirectOriginal(O_CALL_FUNCTION, reinterpret_cast<const void *>(&O_CallFunction));
    RedirectOriginal(O_RAND, reinterpret_cast<const void *>(&O_Rand));
}

}  // namespace H

int main()
{
    using namespace H;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("vm_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    AddVectoredExceptionHandler(1, OnDivide);
    if (const char *d = std::getenv("HD2_UNPACKED"))
        g_data = d;
    if (const char *d = std::getenv("HD2_SC_DATA"))
        g_data = d;
    g_dump = std::getenv("HD2_SC_DUMP") != nullptr;
    if (const char *s = std::getenv("HD2_SC_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    const bool quick = std::getenv("HD2_VM_QUICK") != nullptr;
    int steps = quick ? 60 : 200;
    if (const char *s = std::getenv("HD2_VM_STEPS"))
        steps = std::atoi(s);
    int programs = quick ? 3000 : 20000;
    if (const char *s = std::getenv("HD2_VM_PROGRAMS"))
        programs = std::atoi(s);
    const int show = std::getenv("HD2_VM_SHOW") ? std::atoi(std::getenv("HD2_VM_SHOW")) : -1;
    g_showSteps = show >= 0;
    const char *only = std::getenv("HD2_SC_ONLY");
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpVm();
    SetMission("AFRICA1");
    // HD2_VM_FILE: one program from a file on disk, run alone (its steps shown).
    if (const char *file = std::getenv("HD2_VM_FILE")) {
        std::string text;
        if (FILE *in = std::fopen(file, "rb")) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
                text.append(buf, n);
            std::fclose(in);
        }
        const std::string path = "Scripts\\AFRICA1\\vm_f.scr";
        g_overrides[Lower(path)] = text;
        g_current = path;
        g_showSteps = true;
        _putenv("HD2_VM_SHOW_FAILS=1");
        RunProgram(path, steps);
        std::printf("vm_diff: %d checks, %d failures\n", checks, failures);
        return failures != 0;
    }
    // Crafted programs, run longer: Gosub past its depth limit (100), the save-game values' bounds.
    {
        static const char *const crafted[] = {
            "Integer a;\r\nlabel L0:\r\na = a + 1;\r\nGosub L0;\r\n",
            "Integer a;\r\nlabel L0:\r\nGosub L1;\r\ngoto L0;\r\nlabel L1:\r\nGosub L2;\r\nReturn;\r\nlabel L2:\r\nReturn;\r\n",
            "Integer a = 7;\r\nInteger b;\r\nSaveGameValue(-1, a);\r\nSaveGameValue(256, a);\r\nSaveGameValue(255, a);\r\n"
            "SaveGameValue(0, a);\r\nb = _LoadGameValue(256) + _LoadGameValue(-1) + _LoadGameValue(255) + _LoadGameValue(0);\r\n",
            // A string or frame operand in a number's expression (0), and a string joined by a dropped operator.
            "Integer a = 3;\r\nInteger b = 4;\r\nString s = \"q\";\r\nFrame f;\r\nlabel L0:\r\na = s;\r\nb = f + 1;\r\n"
            "If (s)\r\n{\r\n\ta = 9;\r\n}\r\ns = a <= b;\r\ns = \"x\" + a <= \"y\";\r\nDelay(100);\r\ngoto L0;\r\n",
        };
        const std::string path = "Scripts\\AFRICA1\\vm_c.scr";
        for (const char *text : crafted) {
            g_overrides[Lower(path)] = text;
            g_current = path + " (crafted)";
            g_ticksOnly = true;
            RunProgram(path, 600);
            g_ticksOnly = false;
        }
        g_overrides.clear();
    }
    // Random programs.
    {
        const std::string path = "Scripts\\AFRICA1\\vm_x.scr";
        const uint32_t base = g_seed;
        for (int n = 0; n < programs; ++n) {
            if (show >= 0 && n != show)
                continue;
            g_seed = Mix(base, uint32_t(n)) | 1;            // (each program's own sequence: HD2_VM_SHOW repeats one)
            const std::string text = RandomProgram();
            if (show >= 0)
                std::printf("%s\n", text.c_str());
            g_overrides[Lower(path)] = text;
            g_current = path + F(" (program %d)", n);
            const int before = failures;
            RunProgram(path, steps);
            if (failures != before && g_dump)
                std::printf("%s\n", text.c_str());
        }
        g_overrides.clear();
    }
    // The game scripts (HD2_VM_NO_CORPUS: none).
    if (show < 0 && !std::getenv("HD2_VM_NO_CORPUS")) {
        for (const S_File &f : Corpus()) {
            if (only && Lower(f.path).find(Lower(only)) == std::string::npos)
                continue;
            SetMission(f.mission);
            g_current = f.path;
            RunProgram(f.path, steps);
        }
    }
    std::printf("vm_diff: actions");
    for (int i = 0; i < A_COUNT; ++i)
        std::printf(" %s %d", ACTION_NAMES[i], g_actionCounts[i]);
    std::printf("\nvm_diff: commands %d real, %d faked; functions %d real, %d faked; %d divisions the original faulted in, %d null operator "
                "lists\n", g_realCommands, g_fakeCommands, g_realFunctions, g_fakeFunctions, g_divideFaults,
                g_nullOperatorFaults);
    std::printf("vm_diff: %d programs (%d not compiling in either, %d differing compiles, %d skipped: the original freed an expression node, %d: it "
                "wrote past a block), %d steps, %d checks, %d failures\n",
                g_programs, g_notCompiling, g_compileFailures, g_skippedFreed, g_skippedOverrun, g_steps, checks, failures);
    return failures != 0;
}
