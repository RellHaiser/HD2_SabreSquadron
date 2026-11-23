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
// Script part 5: three more actions: FRM_SetAnimationDelayed's wait set up (a fake animated model; the ticks run it),
// SaveGame (each side's chunk file in memory, compared byte for byte) and LoadGame (the same bytes on both sides: the
// original's last save or a file made for the script, its chunks in any order, broken or short); each side's fake
// frames, scene and items (C_Item::GetFrame faked alike), every call on them compared; a crafted program with a command
// made null; the fakes compared again after the scripts are freed.
// Env: as sc_diff (HD2_SC_DATA / HD2_UNPACKED, HD2_SC_ONLY, HD2_SC_DUMP, HD2_SC_SEED, HD2_SC_FIRST_CHANCE), and
// HD2_VM_QUICK (fewer programs and steps), HD2_VM_NO_CORPUS (the random programs alone), HD2_VM_STEPS (steps per
// program), HD2_VM_PROGRAMS (random programs), HD2_VM_SHOW (a random program's number: its text printed and its steps),
// HD2_VM_SHOW_FAILS (the programs not compiling or skipped named), HD2_VM_FILE (one program from a file, run alone).
#define HARNESS_NAME "vm_diff"
#include "sc_shared.inc"

#include "common/chunk.h"
#define private public                                  // (part 5: the items module's lists set directly)
#include "game/items_module.h"
#undef private
#include "game/world.h"
#include "script/script_module.h"

#include <utility>

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
// Part 5: the script's kind on both sides (the original's vtable +4; the port's override).
int g_scriptKind = SCRIPT_KIND_GENERIC;
int TC O_GetKind(void *) { return g_scriptKind; }

}  // namespace H

// The port's side: a script whose ExecuteCommand fakes the game commands, and CallFunction wrapped.
class C_TestScript : public C_Script {
public:
    C_TestScript() : C_Script(nullptr, true) {}
    int GetKind() const override { return H::g_scriptKind; }
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
// Part 5: the port's side of the files written (sc_shared.inc's, as the original's).
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
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

// ---- part 5: the fake frames (the animated model FRM_SetAnimationDelayed waits on, the frames a load finds) ----
// One set per side, alike: a frame's block (its vtable at 0, its reference count +0xE4, its name +0xE8; its data
// null) and the harness's own words past it: the animation's time +0x100 and end +0x104, its animation set +0x108 (a
// block whose +4 points back to the frame). Every call is logged on its side and compared.
// (The first SCENE_FRAMES are the scene's (FindFrame's); the rest are items' frames, found only through the items.)
constexpr int FAKE_FRAMES = 10, SCENE_FRAMES = 6;
const char *const FAKE_FRAME_NAMES[FAKE_FRAMES] = {"door_1", "Box.lid", "anim_a", "anim_b", "lift", "LIFT",
                                                   "item_a", "item_b", "Item_A", "lift"};
uint8_t *g_fakeFrames[2][FAKE_FRAMES];                  // [0] the original's, [1] the port's
uint8_t *g_fakeScene[2];
std::string g_frameLog[2];
int FakeSide() { return std::strcmp(g_case, "original") == 0 ? 0 : 1; }
int &AnimTime(uint8_t *f) { return *reinterpret_cast<int *>(f + 0x100); }
int &AnimEnd(uint8_t *f) { return *reinterpret_cast<int *>(f + 0x104); }
int16_t &RefCount(uint8_t *f) { return *reinterpret_cast<int16_t *>(f + 0xE4); }
const char *FrameName(const void *f) { return *reinterpret_cast<const char *const *>(static_cast<const uint8_t *>(f) + 0xE8); }
std::string FakeFrameName(uint32_t p)
{
    if (!p)
        return "null";
    for (int s = 0; s < 2; ++s)
        for (uint8_t *f : g_fakeFrames[s])
            if (reinterpret_cast<uint32_t>(f) == p)
                return FrameName(f);
    return "?";
}
uint32_t __stdcall FF_Release(uint8_t *f)
{
    g_frameLog[FakeSide()] += F(" release %s", FrameName(f));
    return uint32_t(--RefCount(f));
}
void __stdcall FF_Tick(uint8_t *f, int dt)
{
    g_frameLog[FakeSide()] += F(" tick %s %d", FrameName(f), dt);
    AnimTime(f) += dt;
}
void *__stdcall FF_GetAnimation(uint8_t *f, int stage)
{
    g_frameLog[FakeSide()] += F(" animation %s %d", FrameName(f), stage);
    return *reinterpret_cast<void **>(f + 0x108);
}
int __stdcall FF_GetAnimTime(uint8_t *f, int stage)
{
    g_frameLog[FakeSide()] += F(" time %s %d", FrameName(f), stage);
    return AnimTime(f);
}
int __stdcall FA_GetEndTime(uint8_t *a)
{
    uint8_t *f = *reinterpret_cast<uint8_t **>(a + 4);
    g_frameLog[FakeSide()] += F(" end %s", FrameName(f));
    return AnimEnd(f);
}
void *__stdcall FS_FindFrame(uint8_t *, const char *name, uint32_t mask)
{
    const int side = FakeSide();
    g_frameLog[side] += F(" find \"%s\" %x", name, mask);
    for (int k = 0; k < SCENE_FRAMES; ++k)
        if (std::strcmp(FrameName(g_fakeFrames[side][k]), name) == 0)
            return g_fakeFrames[side][k];
    return nullptr;
}
// The items (part 5: LoadGame's frames not in the scene): a fake item is a block whose +4 holds its frame (one of the
// items' fake frames, or none); C_Item::GetFrame on both sides answers it, logged. Each side's items module: the
// original's placements (0x0098b808, count 0x0098b810; 0x30 bytes each, the item first) and its respawning items (the
// MSVC 6 vector at 0x0098b818); the port's g_itemsModule (sc_globals.cpp's raw storage, zeros: its members set).
static_assert(sizeof(C_ItemsModule) <= 0x1000, "sc_globals.cpp's g_itemsModule storage");
uint8_t *g_fakeItems[2][4];
std::string ItemName(const uint8_t *item)
{
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 4; ++k)
            if (g_fakeItems[s][k] == item)
                return F("item %d", k);
    return "?";
}
I3D_frame *FakeItemFrame(const uint8_t *item, int index)
{
    g_frameLog[FakeSide()] += F(" item frame %s %d", ItemName(item).c_str(), index);
    return *reinterpret_cast<I3D_frame *const *>(item + 4);
}
I3D_frame *TC O_ItemGetFrame(uint8_t *item, int index) { return FakeItemFrame(item, index); }
void SetUpFakeItems()
{
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 4; ++k) {
            g_fakeItems[s][k] = static_cast<uint8_t *>(_aligned_malloc(0x20, 16));
            std::memset(g_fakeItems[s][k], 0, 0x20);
        }
    RedirectOriginal(0x007d3f00, reinterpret_cast<const void *>(&O_ItemGetFrame));
}
// Before a load, sometimes: placements (some without an item, some items without a frame) and respawning items, alike.
std::vector<uint8_t *> g_itemBlocks;
int g_itemLoads;
void SetItems(bool on)
{
    for (uint8_t *b : g_itemBlocks)
        _aligned_free(b);
    g_itemBlocks.clear();
    int placements = 0, respawning = 0;
    int placementItems[4] = {}, respawningItems[4] = {};
    if (on) {
        ++g_itemLoads;
        // Each item its frame (an item's frame, a scene frame's name twin, or none), the same on both sides.
        for (int k = 0; k < 4; ++k) {
            const int f = P(0.15) ? -1 : I(SCENE_FRAMES, FAKE_FRAMES - 1);
            for (int s = 0; s < 2; ++s) {
                uint8_t *frame = f < 0 ? nullptr : g_fakeFrames[s][f];
                std::memcpy(g_fakeItems[s][k] + 4, &frame, 4);
            }
        }
        placements = I(0, 4);
        for (int i = 0; i < placements; ++i)
            placementItems[i] = P(0.2) ? -1 : I(0, 3);
        respawning = I(0, 4);
        for (int i = 0; i < respawning; ++i)
            respawningItems[i] = I(0, 3);
    }
    for (int s = 0; s < 2; ++s) {
        uint8_t *list = nullptr, *items = nullptr;
        if (placements) {
            list = static_cast<uint8_t *>(_aligned_malloc(0x30 * placements, 16));
            std::memset(list, 0, size_t(0x30 * placements));
            g_itemBlocks.push_back(list);
            for (int i = 0; i < placements; ++i) {
                uint8_t *item = placementItems[i] < 0 ? nullptr : g_fakeItems[s][placementItems[i]];
                std::memcpy(list + 0x30 * i, &item, 4);
            }
        }
        if (respawning) {
            items = static_cast<uint8_t *>(_aligned_malloc(4 * respawning, 16));
            g_itemBlocks.push_back(items);
            for (int i = 0; i < respawning; ++i) {
                uint8_t *item = g_fakeItems[s][respawningItems[i]];
                std::memcpy(items + 4 * i, &item, 4);
            }
        }
        uint8_t *const end = items ? items + 4 * respawning : nullptr;
        if (s == 0) {
            *OriginalAt<uint8_t **>(0x0098b808) = list;
            *OriginalAt<int *>(0x0098b810) = placements;
            *OriginalAt<uint8_t **>(0x0098b81c) = items;
            *OriginalAt<uint8_t **>(0x0098b820) = end;
            *OriginalAt<uint8_t **>(0x0098b824) = end;
        } else {
            g_itemsModule.m_placements = reinterpret_cast<S_ItemPlacement *>(list);
            g_itemsModule.m_placementCount = placements;
            g_itemsModule.m_respawningItems.assign(reinterpret_cast<C_Item **>(items),
                                                   reinterpret_cast<C_Item **>(end));
        }
    }
}
// Any other slot ends the test (its arguments unknown).
template <int N> void __stdcall FakeTrap()
{
    std::printf("vm_diff: a fake frame's slot 0x%02X called [%s]\n", N * 4, g_current.c_str());
    std::exit(3);
}
template <int... N> void FillTraps(void **vt, std::integer_sequence<int, N...>)
{
    ((vt[N] = reinterpret_cast<void *>(&FakeTrap<N>)), ...);
}
void **FakeVtable()
{
    void **vt = new void *[0x40];
    FillTraps(vt, std::make_integer_sequence<int, 0x40>());
    return vt;
}
void SetUpFakeFrames()
{
    void **frameVt = FakeVtable(), **animVt = FakeVtable(), **sceneVt = FakeVtable();
    frameVt[0x00 / 4] = reinterpret_cast<void *>(&FF_Release);
    frameVt[0x20 / 4] = reinterpret_cast<void *>(&FF_Tick);
    frameVt[0x88 / 4] = reinterpret_cast<void *>(&FF_GetAnimation);
    frameVt[0xA0 / 4] = reinterpret_cast<void *>(&FF_GetAnimTime);
    animVt[0x08 / 4] = reinterpret_cast<void *>(&FA_GetEndTime);
    sceneVt[0x7C / 4] = reinterpret_cast<void *>(&FS_FindFrame);
    for (int s = 0; s < 2; ++s) {
        for (int k = 0; k < FAKE_FRAMES; ++k) {
            uint8_t *f = static_cast<uint8_t *>(_aligned_malloc(0x200, 16));
            std::memset(f, 0, 0x200);
            *reinterpret_cast<void ***>(f) = frameVt;
            *reinterpret_cast<const char **>(f + 0xE8) = FAKE_FRAME_NAMES[k];
            uint8_t *a = static_cast<uint8_t *>(_aligned_malloc(0x20, 16));
            std::memset(a, 0, 0x20);
            *reinterpret_cast<void ***>(a) = animVt;
            *reinterpret_cast<uint8_t **>(a + 4) = f;
            *reinterpret_cast<uint8_t **>(f + 0x108) = a;
            g_fakeFrames[s][k] = f;
        }
        g_fakeScene[s] = static_cast<uint8_t *>(_aligned_malloc(0x40, 16));
        std::memset(g_fakeScene[s], 0, 0x40);
        *reinterpret_cast<void ***>(g_fakeScene[s]) = sceneVt;
    }
    std::memcpy(g_origGame + 0x18, &g_fakeScene[0], 4);
    reinterpret_cast<C_game *>(g_portGameBuf)->m_scene = reinterpret_cast<I3D_scene *>(g_fakeScene[1]);
}
void ResetFakeFrames()
{
    for (int s = 0; s < 2; ++s)
        for (uint8_t *f : g_fakeFrames[s]) {
            RefCount(f) = 1;
            AnimTime(f) = 0;
            AnimEnd(f) = 0;
        }
}
// The fakes' state on one side, for the comparison.
std::string FakeState(int side)
{
    std::string s = "fakes";
    for (uint8_t *f : g_fakeFrames[side])
        s += F(" %s %d %d %d", FrameName(f), int(RefCount(f)), AnimTime(f), AnimEnd(f));
    return s;
}

// ---- the state compared ----
std::vector<std::string> OrigState(const uint8_t *b)
{
    std::vector<std::string> out;
    std::string flags = "flags";
    for (uint32_t off = 4; off <= 0xA; ++off)
        flags += F(" %d", int(b[off]));
    out.push_back(flags);
    out.push_back(F("signals %08x disabled %08x delay %d start %d global %d wait %d %s", At<uint32_t>(b, 0x10),
                    At<uint32_t>(b, 0x14), At<int>(b, 0x18), At<int>(b, 0x1C), int(b[0x20]), At<int>(b, 0x24),
                    FakeFrameName(At<uint32_t>(b, 0x28)).c_str()));
    std::string frames = "frame variables";
    for (uint32_t q : OrigVec(b, 0x8C, 4))
        frames += " " + FakeFrameName(At<uint32_t>(reinterpret_cast<void *>(q), 0));
    out.push_back(frames);
    out.push_back(FakeState(0));
    out.push_back("frame calls" + g_frameLog[0]);
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
    out.push_back(F("signals %08x disabled %08x delay %d start %d global %d wait %d %s", t.m_pendingSignals,
                    t.m_disabledSignals, t.m_delay, t.m_startPoint, int(t.m_global), t.m_waitAnimLabel,
                    FakeFrameName(reinterpret_cast<uint32_t>(t.m_waitAnimModel)).c_str()));
    std::string frames = "frame variables";
    for (const I3D_frame *f : t.m_frames)
        frames += " " + FakeFrameName(reinterpret_cast<uint32_t>(f));
    out.push_back(frames);
    out.push_back(FakeState(1));
    out.push_back("frame calls" + g_frameLog[1]);
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
    A_ACTION_DONE, A_STOP, A_RESTART, A_EXIT, A_MISSION, A_RESTARTING, A_ROLE, A_WAIT, A_SAVE, A_LOAD, A_COUNT
};
const char *const ACTION_NAMES[A_COUNT] = {"Tick", "SendSignal", "OnDeath", "OnUse", "OnHit", "OnAlarm", "OnAlarmDone",
                                           "OnGlobalAlarm", "OnCutscene", "OnCutsceneDone", "OnActionDone", "Stop",
                                           "Restart", "OnExit", "mission", "restarting", "role", "wait", "SaveGame",
                                           "LoadGame"};
int g_actionCounts[A_COUNT];
struct S_Action {
    E_Action kind;
    int value;
};
bool g_ticksOnly;                                       // (the crafted programs: nothing interrupts them)
S_Action RandomAction(int ip)
{
    // Part 5: FRM_SetAnimationDelayed's wait set up (then ticks), the saved games.
    if (!g_ticksOnly && P(0.12)) {
        const int k = I(0, 9);
        return {k < 5 ? A_WAIT : k < 7 ? A_SAVE : A_LOAD, 0};
    }
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

// (The results of OnAlarmDone, OnCutscene and OnCutsceneDone are compared too, and part 5's saves and loads: recorded
// here.)
std::string g_origResult, g_portResult;

// ---- part 5: FRM_SetAnimationDelayed's wait, set up alike on both sides ----
// The label: mostly the instruction pointer's (the command waiting), sometimes another (the wait dropped) or none; the
// model: an animated one mostly; its time and end on both sides' fakes.
int g_waits, g_waitsAtIp;
void SetWait(uint8_t *b, C_TestScript &s)
{
    const int ip = s.m_ip;
    const int label = P(0.75) ? ip : P(0.3) ? -1 : I(0, ip + 2);
    const int m = P(0.85) ? I(2, 3) : I(0, FAKE_FRAMES - 1);
    static const int ends[] = {0, 1, 100, 500, 1000, 3000};
    const int end = ends[I(0, 5)];
    const int time = P(0.3) ? end + I(-2, 1) : I(0, std::max(end, 1));
    for (int side = 0; side < 2; ++side) {
        AnimTime(g_fakeFrames[side][m]) = time;
        AnimEnd(g_fakeFrames[side][m]) = end;
    }
    const uint32_t model = label == -1 ? 0 : reinterpret_cast<uint32_t>(g_fakeFrames[0][m]);
    std::memcpy(b + 0x24, &label, 4);
    std::memcpy(b + 0x28, &model, 4);
    s.m_waitAnimLabel = label;
    s.m_waitAnimModel = label == -1 ? nullptr : reinterpret_cast<I3D_model *>(g_fakeFrames[1][m]);
    ++g_waits;
    if (label == ip)
        ++g_waitsAtIp;
}

// ---- part 5: the saved games (C_Script::SaveGame / LoadGame) ----
// A save: each side's chunk file in memory ("o_save.sav", "p_save.sav"), the script's chunk inside one of the test's
// (as C_ScriptModule's own chunk holds it); compared: the result, the chunk's place after, the file's bytes. A load: the
// same bytes on both sides (the original's last save, or a file made here: the script's chunks in any order, some
// repeated, missing, unknown, short or long, broken), its chunk descended into first; compared: the result, the place
// after, then the script's whole state (and the fakes' calls: FindFrame, Release).
using T_ChunkOpen = bool(TC *)(void *, const char *, bool);
using T_ChunkVoid = bool(TC *)(void *);
using T_ChunkBegin = bool(TC *)(void *, uint16_t);
using T_ChunkAscend = bool(TC *)(void *, uint32_t *);
using T_ChunkDescend = bool(TC *)(void *, uint16_t *);
using T_ScriptChunk = bool(TC *)(void *, void *);
constexpr uint32_t O_SAVE_GAME = 0x004147d0, O_LOAD_GAME = 0x00414e30, O_CHUNK_OPEN = 0x00405620,
                   O_CHUNK_CLOSE = 0x00405750, O_CHUNK_BEGIN = 0x00405820, O_CHUNK_ASCEND = 0x00405890,
                   O_CHUNK_DESCEND = 0x00415c90;
constexpr uint16_t SCRIPT_TEST_CHUNK = 0x4444;
static_assert(sizeof(C_chunk) == 0x414, "C_chunk layout (the handle +4, the depth +0x410)");
std::string g_lastSave;                                 // the original's last save (a later load reads it)
int g_saves, g_loads, g_loadsOfSaves, g_madeFiles;
std::string Hex(const std::string &s)
{
    std::string h;
    for (unsigned char c : s)
        h += F("%02x", c);
    return h;
}
// (The same layout on both sides: open +0, the handle +4, the depth +0x410.)
std::string ChunkPlace(const void *c)
{
    const bool open = At<uint8_t>(c, 0) != 0;
    return F("open %d at %d depth %d", int(open), open ? FileSeek(At<int>(c, 4), 0, 1) : -1, At<int>(c, 0x410));
}
std::string SaveOrig(uint8_t *b)
{
    alignas(16) uint8_t c[0x500] = {};
    OriginalAt<T_ChunkOpen>(O_CHUNK_OPEN)(c, "o_save.sav", true);
    OriginalAt<T_ChunkBegin>(O_CHUNK_BEGIN)(c, SCRIPT_TEST_CHUNK);
    const bool r = OriginalAt<T_ScriptChunk>(O_SAVE_GAME)(b, c);
    const std::string place = ChunkPlace(c);
    OriginalAt<T_ChunkAscend>(O_CHUNK_ASCEND)(c, nullptr);
    OriginalAt<T_ChunkVoid>(O_CHUNK_CLOSE)(c);
    g_lastSave = g_overrides["o_save.sav"];
    return F("save %d %s file ", int(r), place.c_str()) + Hex(g_lastSave);   // (not through F: its 1,200 characters cut the file)
}
std::string SavePort(C_TestScript &s)
{
    C_chunk c;
    c.Open("p_save.sav", true);
    c.Begin(SCRIPT_TEST_CHUNK);
    const bool r = s.SaveGame(c);
    const std::string place = ChunkPlace(&c);
    c.Ascend();
    c.Close();
    return F("save %d %s file ", int(r), place.c_str()) + Hex(g_overrides["p_save.sav"]);   // (not through F: its 1,200 characters cut the file)
}
std::string LoadOrig(uint8_t *b)
{
    alignas(16) uint8_t c[0x500] = {};
    const bool open = OriginalAt<T_ChunkOpen>(O_CHUNK_OPEN)(c, "load.sav", false);
    uint16_t id = 0;
    const bool in = OriginalAt<T_ChunkDescend>(O_CHUNK_DESCEND)(c, &id);
    const bool r = OriginalAt<T_ScriptChunk>(O_LOAD_GAME)(b, c);
    const std::string place = ChunkPlace(c);
    OriginalAt<T_ChunkAscend>(O_CHUNK_ASCEND)(c, nullptr);
    OriginalAt<T_ChunkVoid>(O_CHUNK_CLOSE)(c);
    return F("load open %d in %d %04x result %d %s", int(open), int(in), id, int(r), place.c_str());
}
std::string LoadPort(C_TestScript &s)
{
    C_chunk c;
    const bool open = c.Open("load.sav", false);
    uint16_t id = 0;
    const bool in = c.Descend(id);
    const bool r = s.LoadGame(c);
    const std::string place = ChunkPlace(&c);
    c.Ascend();
    c.Close();
    return F("load open %d in %d %04x result %d %s", int(open), int(in), id, int(r), place.c_str());
}
// Before a save, sometimes: frames put in empty frame variables on both sides (each side its own fakes, the same ones),
// with a reference each (as FRM_* commands leave them).
void AssignFrames(uint8_t *b, C_TestScript &s)
{
    const std::vector<uint32_t> slots = OrigVec(b, 0x8C, 4);
    for (size_t i = 0; i < slots.size() && i < s.m_frames.size(); ++i) {
        if (At<uint32_t>(reinterpret_cast<void *>(slots[i]), 0) || s.m_frames[i] || P(0.4))
            continue;
        const int k = I(0, FAKE_FRAMES - 1);
        const uint32_t f = reinterpret_cast<uint32_t>(g_fakeFrames[0][k]);
        std::memcpy(reinterpret_cast<void *>(slots[i]), &f, 4);
        ++RefCount(g_fakeFrames[0][k]);
        s.m_frames[i] = reinterpret_cast<I3D_frame *>(g_fakeFrames[1][k]);
        ++RefCount(g_fakeFrames[1][k]);
    }
}
// A load file made for the script (its numbers, frames and whenevers counted from the port's, alike).
const char *RandomFrameName()
{
    static const char *const names[] = {"door_1", "Box.lid", "anim_a", "anim_b", "lift", "LIFT", "", "nosuch", "door_",
                                        "item_a", "item_b", "Item_A", "item_", "x", "d"};
    return names[I(0, 14)];
}
void WriteInt(C_chunk &c, int v) { c.Write(v); }
void WriteRawHeader(C_chunk &c, uint16_t id, uint32_t size)
{
    c.Write(id);
    c.Write(size);
}
std::string MakeLoadFile(const C_TestScript &s)
{
    ++g_madeFiles;
    C_chunk c;
    c.Open("make.sav", true);
    c.Begin(SCRIPT_TEST_CHUNK);
    static const uint16_t known[] = {1000, 0x4B0, 0x514, 0x578, 0x5DC, 0x640, 0x6A4, 0x708, 0x76C, 0x7D0,
                                     0x834, 0x898, 0x8FC, 0x960, 0x9C4, 0xA28, 0xA8C, 0xAF0, 0xB54};
    std::vector<uint16_t> ids;
    for (uint16_t id : known)
        if (P(0.8))
            ids.push_back(id);
    for (int n = P(0.3) ? I(1, 2) : 0; n > 0 && !ids.empty(); --n)
        ids.push_back(ids[size_t(I(0, int(ids.size()) - 1))]);
    if (P(0.05)) {
        static const uint16_t unknown[] = {0, 999, 0x992, 0x4444, 0xFFFF};
        ids.push_back(unknown[I(0, 4)]);
    }
    for (size_t i = ids.size(); i > 1; --i)
        std::swap(ids[i - 1], ids[size_t(I(0, int(i) - 1))]);
    const int count = s.CommandCount();
    int variables = 0;
    for (const S_ScriptNumber &n : s.m_numbers)
        variables += !n.isConstant;
    const auto jitter = [](int n) { return std::max(0, n + (P(0.1) ? I(-2, 1) : 0)); };
    bool broken = false;
    for (const uint16_t id : ids) {
        c.Begin(id);
        switch (id) {
        case 1000:
            WriteInt(c, P(0.9) ? I(0, std::max(count - 1, 0)) : P(0.5) ? -1 : count + I(0, 3));
            break;
        case 0x4B0:
            WriteInt(c, I(0, 1500));
            break;
        case 0x514:
            WriteInt(c, P(0.5) ? 0 : I(-50, 3000));
            break;
        case 0x578: case 0x5DC:
            WriteInt(c, P(0.5) ? 0 : int(U32() & (P(0.5) ? 0x7F : 0xFFFFFFFF)));
            break;
        case 0x640: case 0x6A4: case 0x708: case 0x76C: case 0x7D0: case 0x834: case 0x898: case 0xAF0: case 0xB54: {
            const uint8_t v = uint8_t(I(0, 1));
            c.Write(v);
            if (P(0.05))
                c.Write(v);                             // (a byte too many)
            break;
        }
        case 0x8FC:
            for (int n = jitter(variables); n > 0; --n)
                WriteInt(c, P(0.7) ? I(-5, 100) : int(U32()));
            break;
        case 0x960:
            // (Never fewer names than variables: the original would read the next chunk's payload as a name, without
            // a terminator: its stale stack again.)
            for (int n = int(s.m_frames.size()) + (P(0.1) ? I(0, 2) : 0); n > 0; --n) {
                if (P(0.04)) {
                    WriteRawHeader(c, 0x992, 0x7FFF);  // (broken: past its parent; the file's next bytes its name)
                    broken = true;
                    continue;
                }
                // (Always with its terminator: the original copies the bytes into its stack without one, see
                // COVERAGE.md; a header shorter than a header reads a negative size into it.)
                const char *name = RandomFrameName();
                c.Begin(P(0.97) ? uint16_t(0x992) : uint16_t(0x993));
                c.Write(name, int(std::strlen(name)) + 1);
                c.Ascend();
            }
            break;
        case 0x9C4: {
            if (P(0.05))
                break;                                  // (empty: the count's read fails, its first value stays)
            const int depth = P(0.03) ? I(100, 300) : I(0, 4);
            WriteInt(c, depth + (P(0.1) ? I(-1, 2) : 0));
            for (int k = 0; k < depth; ++k)
                WriteInt(c, I(0, std::max(count - 1, 0)));
            break;
        }
        case 0xA28: {
            WriteInt(c, P(0.8) ? I(0, std::max(count - 1, 0)) : -1);
            const char *name = RandomFrameName();
            const int length = int(std::strlen(name)) + 1;
            // (Its length the text's or 0: a shorter one leaves the original's buffer unterminated, a longer one past
            // the chunk leaves it unread (its stale stack either way), and one past the buffer is skipped onto its
            // stack.)
            const uint16_t stored = uint16_t(P(0.95) ? length : 0);
            c.Write(stored);
            c.Write(name, length);
            break;
        }
        case 0xA8C:
            for (int n = jitter(int(s.m_whenevers.size())); n > 0; --n) {
                WriteInt(c, I(0, 3000));
                WriteInt(c, I(0, 3000));
                c.Write(uint8_t(I(0, 1)));
            }
            break;
        default:
            for (int n = I(0, 6); n > 0; --n)
                c.Write(uint8_t(U32()));
            break;
        }
        c.Ascend();
    }
    // Stray bytes at the script's chunk's end, sometimes (fewer than a header: the loop's "bytes left" test).
    for (int n = P(0.04) ? I(1, 5) : 0; n > 0; --n)
        c.Write(uint8_t(U32()));
    c.Ascend();
    // (After a broken name, always: with no bytes left the original's name would be its stale stack.)
    if (broken || P(0.5)) {
        c.Begin(0x7777);
        WriteInt(c, 0x12345678);
        c.Ascend();
    }
    c.Close();
    std::string f = g_overrides["make.sav"];
    // A file cut short (not one with names: the cut string would be read from the stale stack, as above; nor with the
    // instruction pointer, the timer, the delay or the signals: the original reads each into a stack word it then
    // stores, read or not).
    bool cuttable = true;
    for (const uint16_t id : {uint16_t(0x960), uint16_t(0xA28), uint16_t(1000), uint16_t(0x4B0), uint16_t(0x514),
                              uint16_t(0x578), uint16_t(0x5DC)})
        cuttable = cuttable && std::find(ids.begin(), ids.end(), id) == ids.end();
    if (P(cuttable ? 0.3 : 0.0))
        f.resize(size_t(I(0, int(f.size()))));
    return f;
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
    case A_SAVE: g_origResult = SaveOrig(b); break;
    case A_LOAD: g_origResult = LoadOrig(b); break;
    default: break;
    }
}
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
    case A_SAVE: g_portResult = SavePort(s); break;
    case A_LOAD: g_portResult = LoadPort(s); break;
    default: break;
    }
}

// ---- one program on both sides ----
int g_programs, g_notCompiling, g_compileFailures, g_skippedFreed, g_skippedOverrun, g_steps, g_dumpAll;
int g_nullCommand = -1;                                 // (part 5: the command made null after the compile, -1 none)
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
        // Part 5: the script's kind (GetKind, vtable +4: the saved games' actor chunks), mostly a plain script's.
        g_scriptKind = P(0.6) ? SCRIPT_KIND_GENERIC : P(0.5) ? SCRIPT_KIND_HUMAN : SCRIPT_KIND_MELEE_TRACKER;
        g_origVtable[1] = reinterpret_cast<uint32_t>(&O_GetKind);
        ResetFakeFrames();
        g_lastSave.clear();
        // Part 5: a null command (Tick's check; the compiler never leaves one): the crafted program's, on both sides.
        const std::vector<uint32_t> origCommands = OrigVec(block, 0x5C, 4);
        S_ScriptCommand *nulledPort = nullptr;
        uint32_t nulledOrig = 0;
        if (g_nullCommand >= 0 && g_nullCommand < int(origCommands.size()) && g_nullCommand < script->CommandCount()) {
            nulledPort = script->m_commands[size_t(g_nullCommand)];
            script->m_commands[size_t(g_nullCommand)] = nullptr;
            std::memcpy(&nulledOrig, reinterpret_cast<void *>(origCommands[size_t(g_nullCommand)]), 4);
            std::memset(reinterpret_cast<void *>(origCommands[size_t(g_nullCommand)]), 0, 4);
        }
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
            else if (a.kind == A_WAIT)
                SetWait(block, *script);
            else if (a.kind == A_SAVE) {
                ++g_saves;
                if (P(0.5))
                    AssignFrames(block, *script);
            } else if (a.kind == A_LOAD) {
                ++g_loads;
                const bool own = !g_lastSave.empty() && P(0.4);
                g_loadsOfSaves += own;
                g_overrides["load.sav"] = own ? g_lastSave : MakeLoadFile(*script);
                SetItems(P(0.5));
            }
            g_frameLog[0].clear();
            g_frameLog[1].clear();
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
            if (!Compare(path + F(" step %d (%s %d)", step, ACTION_NAMES[a.kind], a.value), st)) {
                if (g_dump)
                    std::printf("  [%s]%s\n", g_current.c_str(),
                                a.kind == A_LOAD ? (" file " + Hex(g_overrides["load.sav"])).c_str() : "");
                break;                                  // (the rest would follow from the first difference)
            }
        }
        g_armDivide = false;
        SetRole(GAME_ROLE_LOCAL);
        SetRestarting(false);
        SetMissionStarted(true);
        if (nulledPort) {                               // (the nulled command back, for the frees)
            script->m_commands[size_t(g_nullCommand)] = nulledPort;
            std::memcpy(reinterpret_cast<void *>(origCommands[size_t(g_nullCommand)]), &nulledOrig, 4);
        }
    }
    // Freed on both sides (the original's vtable back first: its destructor is not called, Clear frees the program).
    // (Part 5: the frames the variables held released by each side's free, compared after.)
    g_frameLog[0].clear();
    g_frameLog[1].clear();
    g_case = "port";
    delete script;
    g_case = "original";
    OriginalAt<T_Void>(0x0041e140)(block);
    ReleaseHeldFrees();
    std::free(block);
    g_case = "";
    S_Result freed;
    freed.orig = {FakeState(0), "frame calls" + g_frameLog[0]};
    freed.port = {FakeState(1), "frame calls" + g_frameLog[1]};
    Compare(path + " (freed)", freed);
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
    // Part 5: the saved games' files (written in memory), the fake frames and scene.
    RedirectOriginal(0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite));
    RedirectOriginal(0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek));
    RedirectOriginal(0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite));
    SetUpFakeFrames();
    SetUpFakeItems();
}

}  // namespace H

// Part 5: the port's C_Item::GetFrame: the fake item's frame (vm_diff's fake items alone reach it).
I3D_frame *C_Item::GetFrame(int index) { return H::FakeItemFrame(reinterpret_cast<const uint8_t *>(this), index); }

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
            // Part 5: its third command (a = a + 1) made null after the compile (see RunProgram).
            "Integer a;\r\nlabel L0:\r\na = a + 1;\r\nDelay(10);\r\ngoto L0;\r\n",
        };
        const std::string path = "Scripts\\AFRICA1\\vm_c.scr";
        for (const char *text : crafted) {
            g_overrides[Lower(path)] = text;
            g_current = path + " (crafted)";
            g_ticksOnly = true;
            g_nullCommand = text == crafted[4] ? 2 : -1;
            RunProgram(path, 600);
            g_nullCommand = -1;
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
    std::printf("vm_diff: %d waits set (%d at the instruction pointer), %d saves, %d loads (%d of a save, %d made files, %d "
                "with items)\n", g_waits, g_waitsAtIp, g_saves, g_loads, g_loadsOfSaves, g_madeFiles, g_itemLoads);
    std::printf("vm_diff: %d programs (%d not compiling in either, %d differing compiles, %d skipped: the original freed an expression node, %d: it "
                "wrote past a block), %d steps, %d checks, %d failures\n",
                g_programs, g_notCompiling, g_compileFailures, g_skippedFreed, g_skippedOverrun, g_steps, checks, failures);
    return failures != 0;
}
