// Human part 47: the controls (game/input_controls.cpp: C_Controls, WideToAnsi, GameRenderCallback) against the original's
// functions in the original executable mapped into this process (../collision/original_exe.cpp). MinGW; build_diff.bat; runs
// controls_diff.exe ("controls_diff: N checks, 0 failures").
//
// The port's C_Controls (g++ layout: its vectors 12 bytes) and the original's object (a raw block, MSVC6 vectors of 16 bytes:
// allocator, first, last, end) are given the same random state before each call (the original's vectors rebuilt in fresh
// buffers: its operator new / delete are the test's malloc / free) and compared field by field after it, with the calls each
// side made on the fakes: the graphics interface (one object: its methods are stdcall, the same slots on both sides) and the
// joysticks (one set per side: g++ puts two destructors first, so the original's vtable is the port's moved down by a slot).
// HD2_CONTROLS_ROUNDS: rounds per call kind (default 20,000; HD2_CONTROLS_QUICK: 4,000); HD2_CONTROLS_CASE: one kind only.
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/game.h"
#include "game/geffects.h"
#include "game/interface.h"
#include "game/pending.h"
#include "ls3d/igraph.h"
#include "script/script_module.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

namespace {

int checks, failures;
const char *g_case = "";
int g_round;
void Fail(const char *format, ...)
{
    ++failures;
    if (failures > 40)
        return;
    std::printf("FAIL [%s round %d] ", g_case, g_round);
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
}
#define CHECK(cond, ...)               \
    do {                               \
        ++checks;                      \
        if (!(cond))                   \
            Fail(__VA_ARGS__);         \
    } while (0)

// ---- random values ----
std::mt19937 g_rng(47);
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(g_rng) < p; }
float F(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_rng); }
uint32_t U32() { return static_cast<uint32_t>(g_rng()); }
template <class T, size_t N> T Pick(const T (&list)[N]) { return list[I(0, int(N) - 1)]; }
float Ulp(float f, int steps)
{
    for (; steps > 0; --steps)
        f = std::nextafter(f, 1e30f);
    for (; steps < 0; ++steps)
        f = std::nextafter(f, -1e30f);
    return f;
}
// A value near one of `marks` (exactly, a step or two off) or anywhere in [lo, hi].
float Near(std::initializer_list<float> marks, float lo, float hi)
{
    if (P(0.6)) {
        const float m = *(marks.begin() + I(0, int(marks.size()) - 1));
        return P(0.5) ? m : Ulp(m, I(-2, 2));
    }
    if (P(0.03))
        return std::nanf("");
    return F(lo, hi);
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// ---- the calls on the fakes, per side ----
std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char text[400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_log.push_back(text);
}
std::string Joined(const std::vector<std::string> &log)
{
    std::string all;
    for (const std::string &line : log)
        all += line + "; ";
    return all;
}

// ---- the graphics interface (one object for both sides) ----
struct S_Input {
    uint64_t keyBits = 0;
    uint32_t buttons = 0;
    int dx = 0, dy = 0, wheel = 0;
    uint32_t modifiers = 0;
    int typed = 0;
    S_IGraphViewport viewport = {1, 2, 3, 4, 0.5f, 0.75f};
    int scrnX = 640, scrnY = 480;
} g_in;
void __stdcall G_Process(void *) { Log("process"); }
void __stdcall G_Begin(void *) { Log("begin"); }
void __stdcall G_Read(void *) { Log("read"); }
void __stdcall G_Watch(void *, const char *keys, int count)
{
    std::string hex;
    char b[4];
    for (int i = 0; i < 0x40; ++i) {
        std::snprintf(b, sizeof b, "%02x", static_cast<uint8_t>(keys[i]));
        hex += b;
    }
    Log("watch %d %s", count, hex.c_str());
}
uint64_t __stdcall G_Bits(void *) { Log("bits"); return g_in.keyBits; }
uint32_t __stdcall G_Buttons(void *) { Log("buttons"); return g_in.buttons; }
int __stdcall G_Dx(void *) { Log("dx"); return g_in.dx; }
int __stdcall G_Dy(void *) { Log("dy"); return g_in.dy; }
int __stdcall G_Wheel(void *) { Log("wheel"); return g_in.wheel; }
uint32_t __stdcall G_Modifiers(void *, int arg) { Log("modifiers %d", arg); return g_in.modifiers; }
int __stdcall G_Typed(void *) { Log("typed"); return g_in.typed; }
const S_IGraphViewport *__stdcall G_GetViewport(void *) { Log("get viewport"); return &g_in.viewport; }
void __stdcall G_SetViewport(void *, const S_IGraphViewport &v)
{
    Log("viewport %d %d %d %d %08x %08x", v.x, v.y, v.width, v.height, Bits(v.minZ), Bits(v.maxZ));
}
int __stdcall G_ScrnX(void *) { Log("scrnx"); return g_in.scrnX; }
int __stdcall G_ScrnY(void *) { Log("scrny"); return g_in.scrnY; }
void __stdcall G_Other(void *) { Log("other graph slot"); }
void *g_graphVt[128];
struct S_FakeGraph { void **vtable; } g_graph;

// ---- the joysticks (one set per side) ----
struct S_JoyInput {
    uint32_t modes[8] = {};
    float axes[8] = {};
    S_JoyState state = {}, pressed = {};
};
S_JoyInput g_joyIn[4];
class C_FakeJoy : public C_FrameListener {
public:
    int index = 0;
    int refs = 0;
    uint32_t modeSet[8] = {};
    int AddRef() override { Log("joy%d addref", index); return ++refs; }
    int Release() override { Log("joy%d release", index); return --refs; }
    float GetDeadZone(int) override { Log("joy%d other", index); return 0; }
    bool SetDeadZone(int, float) override { Log("joy%d other", index); return true; }
    float GetDeviceDeadZone(int) override { Log("joy%d other", index); return 0; }
    bool SetDeviceDeadZone(int, float) override { Log("joy%d other", index); return true; }
    float GetSaturation(int) override { Log("joy%d other", index); return 0; }
    bool SetSaturation(int, float) override { Log("joy%d other", index); return true; }
    float GetDeviceSaturation(int) override { Log("joy%d other", index); return 0; }
    bool SetDeviceSaturation(int, float) override { Log("joy%d other", index); return true; }
    uint32_t GetAxisMode(int axis) override
    {
        Log("joy%d axis mode? %d", index, axis);
        return axis >= 0 && axis < 8 ? g_joyIn[index].modes[axis] : 0xDEAD;
    }
    bool SetAxisMode(int axis, uint32_t mode) override
    {
        Log("joy%d axis mode %d %u", index, axis, mode);
        if (axis >= 0 && axis < 8)
            modeSet[axis] = mode;
        return P(0.5);
    }
    float GetAxis(int axis) override
    {
        Log("joy%d axis? %d", index, axis);
        return axis >= 0 && axis < 8 ? g_joyIn[index].axes[axis] : -7.0f;
    }
    bool IsButtonDown(int) override { Log("joy%d other", index); return false; }
    bool IsButtonPressed(int) override { Log("joy%d other", index); return false; }
    uint32_t GetButtons(int) override { Log("joy%d other", index); return 0; }
    uint32_t GetButtonsPressed(int) override { Log("joy%d other", index); return 0; }
    bool GetPov(float *, int) override { Log("joy%d other", index); return false; }
    bool GetPovChanged(float *, int) override { Log("joy%d other", index); return false; }
    bool GetState(S_JoyState *out) override
    {
        Log("joy%d state", index);
        *out = g_joyIn[index].state;
        return true;
    }
    bool GetStatePressed(S_JoyState *out) override
    {
        Log("joy%d pressed", index);
        *out = g_joyIn[index].pressed;
        return true;
    }
    bool OnFrame(uint32_t flags) override { Log("joy%d update %u", index, flags); return true; }
};
C_FakeJoy g_joyP[4], g_joyO[4];
void *g_joyOrigVt[32];
// (The original's slot 0, its deleting destructor: never called by the controls.)
void *__attribute__((thiscall)) J_Destroy(void *self, unsigned) { Log("joy destroyed"); return self; }

// ---- the world around the render callback ----
alignas(16) unsigned char g_gameStorage[0x4000];
alignas(16) unsigned char g_interfaceStorage[0x2000];
void *g_gameVt[32];
int __thiscall F_GetState(C_GameState *state) { return state->m_state; }
void *g_effectsVt[64];
struct S_FakeEffects { void **vtable; } g_effects;
void __thiscall E_Volumetrics(void *) { Log("volumetrics"); }
void __thiscall E_Fade(void *) { Log("fade"); }

// The original's: the game (+0x04 state, +0x24 end delay, +0x58 interface, +0x60 quick messages; GetState at 0x20), the
// interface (+0x0C flags), the effects (RenderFade 0xA0, RenderVolumetrics 0xA4).
alignas(16) unsigned char g_gameO[0x100];
alignas(16) unsigned char g_interfaceO[0x40];
alignas(16) unsigned char g_quickO[0x10];
void *g_gameOVt[32];
int __attribute__((thiscall)) FO_GetState(void *game) { return *reinterpret_cast<int *>(static_cast<char *>(game) + 4); }
void *g_effectsOVt[64];
S_FakeEffects g_effectsO;
void *__cdecl O_GetGame() { return g_gameO; }
void *__cdecl O_GetGEffects() { return &g_effectsO; }
void *__stdcall O_GetIGraph() { return &g_graph; }
void __attribute__((thiscall)) O_Render(void *self) { Log(self == g_interfaceO ? "interface render" : "interface render (other)"); }
void __attribute__((thiscall)) O_RenderOverlay(void *self) { Log(self == g_interfaceO ? "interface overlay" : "interface overlay (other)"); }
void __attribute__((thiscall)) O_QuickRender(void *self) { Log(self == g_quickO ? "quick messages" : "quick messages (other)"); }
void __attribute__((thiscall)) O_ApplyFade(void *self) { Log(self == g_gameO ? "apply fade" : "apply fade (other)"); }
void __cdecl O_Stats() { Log("stats"); }
void __cdecl O_DebugDraw() { Log("debug drawing"); }
void *__cdecl O_New(size_t size) { return std::malloc(size ? size : 1); }
void __cdecl O_Delete(void *p) { std::free(p); }

}  // namespace

C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameStorage); }
IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graph); }
C_GEffectsModule *GetGEffects() { return reinterpret_cast<C_GEffectsModule *>(&g_effects); }
void C_Interface::RenderOverlay() { Log(reinterpret_cast<unsigned char *>(this) == g_interfaceStorage ? "interface overlay" : "interface overlay (other)"); }
void C_Interface::Render() { Log(reinterpret_cast<unsigned char *>(this) == g_interfaceStorage ? "interface render" : "interface render (other)"); }
void C_QuickMessages::Render() { Log(reinterpret_cast<unsigned char *>(this) == g_interfaceStorage + 0x1000 ? "quick messages" : "quick messages (other)"); }
void C_game::ApplyFade() { Log(reinterpret_cast<unsigned char *>(this) == g_gameStorage ? "apply fade" : "apply fade (other)"); }
void RenderDebugStats() { Log("stats"); }

namespace {

// ---- the original's functions ----
using ThisFn = void(__attribute__((thiscall)) *)(void *);
using SetMouseScaleFn = bool(__attribute__((thiscall)) *)(void *, int, int, int);
using AttachFn = bool(__attribute__((thiscall)) *)(void *, void *, void **, unsigned);
using InitFn = void(__attribute__((fastcall)) *)(void *);
using ClearFn = void(__attribute__((thiscall)) *)(void *, int, float *, float *);
using GetValuesFn = int(__attribute__((thiscall)) *)(void *, float **, int);
using GetValueFn = float(__attribute__((thiscall)) *)(void *, int, int);
using BindFn = void(__attribute__((thiscall)) *)(void *, int, int, int, int, int);
using SetBindingFn = bool(__attribute__((thiscall)) *)(void *, const void *, int);
using UpdateFn = void(__attribute__((thiscall)) *)(void *, int);
using PollFn = const void *(__attribute__((thiscall)) *)(void *, int, int);
using WideFn = void(__attribute__((fastcall)) *)(char *, const wchar_t *, int);
using RenderFn = int(__stdcall *)(int, int, int);

// ---- the two objects ----
C_Controls *g_port;
alignas(16) unsigned char g_origBlock[0x100];
unsigned char *const g_orig = g_origBlock;
template <class T> T &OF(uint32_t offset) { return *reinterpret_cast<T *>(g_orig + offset); }
// The original's vectors: first, last, end at +4, +8, +0xC from the vector.
const uint32_t O_JOYS = 0x04, O_PRESS = 0x14, O_KEYS = 0x20, O_KEYCOUNTS = 0x60, O_MOUSECOUNTS = 0xA0, O_SCALE = 0xA8,
               O_INVERT = 0xB4, O_BINDINGS = 0xB8, O_HELD = 0xC8, O_PRESSED = 0xD8, O_KEYARMED = 0xE8, O_MOUSEARMED = 0xF0;
const size_t JOY_SIZE = 0x8C, PRESS_SIZE = 0x0C;
size_t OVecBytes(uint32_t at) { return size_t(OF<char *>(at + 8) - OF<char *>(at + 4)); }
template <class T> T *OVecData(uint32_t at) { return OF<T *>(at + 4); }
void OVecPut(uint32_t at, const void *data, size_t bytes)
{
    std::free(OF<void *>(at + 4));
    if (bytes == 0 && P(0.5)) {
        OF<void *>(at + 4) = OF<void *>(at + 8) = OF<void *>(at + 12) = nullptr;
        return;
    }
    // (Spare room now and then: the original's growth then takes another way.)
    const size_t spare = P(0.5) ? 0 : size_t(I(1, 4)) * (at == O_JOYS ? JOY_SIZE : at == O_BINDINGS ? PRESS_SIZE : 4);
    auto *buffer = static_cast<char *>(std::malloc(bytes + spare + 1));
    std::memcpy(buffer, data, bytes);
    OF<char *>(at + 4) = buffer;
    OF<char *>(at + 8) = buffer + bytes;
    OF<char *>(at + 12) = buffer + bytes + spare;
}
C_FakeJoy *ToOrigJoy(C_FrameListener *joy)
{
    for (int i = 0; i < 4; ++i) {
        if (joy == &g_joyP[i])
            return &g_joyO[i];
    }
    return reinterpret_cast<C_FakeJoy *>(joy);
}

// The port's state into the original's object.
void ToOrig()
{
    C_Controls &c = *g_port;
    OF<void *>(0) = c.m_graph;
    std::vector<S_ControlsJoy> joys = c.m_joys;
    for (S_ControlsJoy &j : joys)
        j.joy = ToOrigJoy(j.joy);
    OVecPut(O_JOYS, joys.data(), joys.size() * JOY_SIZE);
    std::memcpy(g_orig + O_PRESS, &c.m_press, PRESS_SIZE);
    std::memcpy(g_orig + O_KEYS, c.m_keys, 0x40);
    std::memcpy(g_orig + O_KEYCOUNTS, c.m_keyCounts, 0x40);
    std::memcpy(g_orig + O_MOUSECOUNTS, c.m_mouseCounts, 8);
    std::memcpy(g_orig + O_SCALE, c.m_scale, 12);
    OF<bool>(O_INVERT) = c.m_invertMouse;
    OVecPut(O_BINDINGS, c.m_bindings.data(), c.m_bindings.size() * PRESS_SIZE);
    OVecPut(O_HELD, c.m_held.data(), c.m_held.size() * 4);
    OVecPut(O_PRESSED, c.m_pressed.data(), c.m_pressed.size() * 4);
    OF<uint64_t>(O_KEYARMED) = c.m_keyArmed;
    OF<uint32_t>(O_MOUSEARMED) = c.m_mouseArmed;
}

std::string Hex(const void *data, size_t bytes)
{
    std::string s;
    char b[4];
    for (size_t i = 0; i < bytes; ++i) {
        std::snprintf(b, sizeof b, "%02x", static_cast<const uint8_t *>(data)[i]);
        s += b;
    }
    return s;
}

// The two objects alike (`bytesOnly`: the fields the original's constructor sets).
void CompareObjects(bool constructed = false)
{
    C_Controls &c = *g_port;
    CHECK(OF<void *>(0) == c.m_graph, "graph %p / %p", OF<void *>(0), static_cast<void *>(c.m_graph));
    CHECK(OVecBytes(O_JOYS) == c.m_joys.size() * JOY_SIZE, "joys %u / %u", unsigned(OVecBytes(O_JOYS) / JOY_SIZE), unsigned(c.m_joys.size()));
    if (OVecBytes(O_JOYS) == c.m_joys.size() * JOY_SIZE) {
        for (size_t i = 0; i < c.m_joys.size(); ++i) {
            const unsigned char *o = OVecData<unsigned char>(O_JOYS) + i * JOY_SIZE;
            S_ControlsJoy j = c.m_joys[i];
            j.joy = ToOrigJoy(j.joy);
            CHECK(std::memcmp(o, &j, JOY_SIZE) == 0, "joy %u: %s / %s", unsigned(i), Hex(o, JOY_SIZE).c_str(), Hex(&j, JOY_SIZE).c_str());
        }
    }
    CHECK(OVecBytes(O_BINDINGS) == c.m_bindings.size() * PRESS_SIZE, "bindings %u / %u", unsigned(OVecBytes(O_BINDINGS) / PRESS_SIZE),
          unsigned(c.m_bindings.size()));
    if (OVecBytes(O_BINDINGS) == c.m_bindings.size() * PRESS_SIZE && !c.m_bindings.empty())
        CHECK(std::memcmp(OVecData<char>(O_BINDINGS), c.m_bindings.data(), OVecBytes(O_BINDINGS)) == 0, "bindings: %s / %s",
              Hex(OVecData<char>(O_BINDINGS), OVecBytes(O_BINDINGS)).c_str(), Hex(c.m_bindings.data(), OVecBytes(O_BINDINGS)).c_str());
    CHECK(OVecBytes(O_HELD) == c.m_held.size() * 4, "held %u / %u", unsigned(OVecBytes(O_HELD) / 4), unsigned(c.m_held.size()));
    if (OVecBytes(O_HELD) == c.m_held.size() * 4 && !c.m_held.empty())
        CHECK(std::memcmp(OVecData<char>(O_HELD), c.m_held.data(), OVecBytes(O_HELD)) == 0, "held: %s / %s",
              Hex(OVecData<char>(O_HELD), OVecBytes(O_HELD)).c_str(), Hex(c.m_held.data(), OVecBytes(O_HELD)).c_str());
    CHECK(OVecBytes(O_PRESSED) == c.m_pressed.size() * 4, "pressed %u / %u", unsigned(OVecBytes(O_PRESSED) / 4), unsigned(c.m_pressed.size()));
    if (OVecBytes(O_PRESSED) == c.m_pressed.size() * 4 && !c.m_pressed.empty())
        CHECK(std::memcmp(OVecData<char>(O_PRESSED), c.m_pressed.data(), OVecBytes(O_PRESSED)) == 0, "pressed: %s / %s",
              Hex(OVecData<char>(O_PRESSED), OVecBytes(O_PRESSED)).c_str(), Hex(c.m_pressed.data(), OVecBytes(O_PRESSED)).c_str());
    CHECK(std::memcmp(g_orig + O_SCALE, c.m_scale, 12) == 0, "scale %s / %s", Hex(g_orig + O_SCALE, 12).c_str(), Hex(c.m_scale, 12).c_str());
    if (constructed)
        return;
    CHECK(std::memcmp(g_orig + O_PRESS, &c.m_press, PRESS_SIZE) == 0, "press %s / %s", Hex(g_orig + O_PRESS, PRESS_SIZE).c_str(),
          Hex(&c.m_press, PRESS_SIZE).c_str());
    CHECK(std::memcmp(g_orig + O_KEYS, c.m_keys, 0x40) == 0, "keys %s / %s", Hex(g_orig + O_KEYS, 0x40).c_str(), Hex(c.m_keys, 0x40).c_str());
    CHECK(std::memcmp(g_orig + O_KEYCOUNTS, c.m_keyCounts, 0x40) == 0, "key counts %s / %s", Hex(g_orig + O_KEYCOUNTS, 0x40).c_str(),
          Hex(c.m_keyCounts, 0x40).c_str());
    CHECK(std::memcmp(g_orig + O_MOUSECOUNTS, c.m_mouseCounts, 8) == 0, "mouse counts %s / %s", Hex(g_orig + O_MOUSECOUNTS, 8).c_str(),
          Hex(c.m_mouseCounts, 8).c_str());
    CHECK(OF<bool>(O_INVERT) == c.m_invertMouse, "invert");
    CHECK(OF<uint64_t>(O_KEYARMED) == c.m_keyArmed, "key armed %016llx / %016llx", (unsigned long long)OF<uint64_t>(O_KEYARMED),
          (unsigned long long)c.m_keyArmed);
    CHECK(OF<uint32_t>(O_MOUSEARMED) == c.m_mouseArmed, "mouse armed %08x / %08x", OF<uint32_t>(O_MOUSEARMED), c.m_mouseArmed);
}

// ---- random states ----
const int KEY = 1, MOUSE = 2, JOY = 3;
uint16_t RandomKey(int device)
{
    static const uint16_t keys[] = {0x01, 0x10, 0x11, 0x1D, 0x2A, 0x39, 0x80, 0xC8, 0xFF, 0x110, 0x0, 0x9D};
    static const uint16_t mouse[] = {0, 1, 2, 3, 4, 8, 0x10, 0x20, 0x100, 0x200, 0x300, 0x400, 0x101};
    switch (device) {
    case KEY:
        return P(0.7) ? Pick(keys) : static_cast<uint16_t>(I(0, 0x1FF));
    case MOUSE:
        return Pick(mouse);
    case JOY: {
        const int kind = I(0, 2);
        if (kind == 0)
            return static_cast<uint16_t>(I(0, 9) + (P(0.05) ? 0xF0 : 0));
        if (kind == 1)
            return static_cast<uint16_t>(0x100 + I(0, 31) + (P(0.05) ? 0x40 : 0));
        return static_cast<uint16_t>(0x200 + I(0, 3) * 0x10 + Pick({1, 2, 3, 4, 6, 8, 9, 12, 15, 0}));
    }
    default:
        return static_cast<uint16_t>(I(0, 0x300));
    }
}
C_Controls::S_Press RandomBinding()
{
    C_Controls::S_Press b{};
    const int device = P(0.05) ? Pick({0, 4, 0x103, -1}) : I(1, 3);
    b.device = static_cast<uint16_t>(device);
    b.key = RandomKey(device);
    b.direction = P(0.9) ? I(0, 3) : Pick({-1, 4, 0x103});
    const int joys = int(g_port->m_joys.size());
    b.unknown08 = P(0.9) ? I(0, joys > 0 ? joys - 1 : 0) : Pick({-1, joys, joys + 1, 0x10000});
    return b;
}
float RandomPov()
{
    return Near({0.0f, 0.1f, 1.5f, 1.6f, 3.12f, 3.16f, 4.69f, 4.72f, 6.28f, 0.05f, 2.0f, 3.14f, 4.7f, 5.5f, -0.5f, 9.0f}, -1.0f, 7.0f);
}
float RandomAxis() { return Near({0.0f, 0.3f, -0.3f, 0.5f, -0.5f, 0.7f, -0.7f, 1.0f, -1.0f, 1.2f, 0.2f}, -1.5f, 1.5f); }
void RandomJoyState(S_JoyState &s)
{
    for (float &a : s.axis)
        a = RandomAxis();
    for (float &p : s.pov)
        p = RandomPov();
    for (bool &c : s.povCentered)
        c = P(0.5);
    s.buttons[0] = P(0.4) ? 0 : P(0.5) ? 1u << I(0, 31) : U32();
    for (int i = 1; i < 4; ++i)
        s.buttons[i] = U32();
}
void RandomInput()
{
    C_Controls &c = *g_port;
    // The keys held: often those the controls watch.
    g_in.keyBits = 0;
    if (P(0.8)) {
        for (int i = 0; i < 64; ++i) {
            if (c.m_keys[i] != 0 ? P(0.4) : P(0.05))
                g_in.keyBits |= uint64_t(1) << i;
        }
    } else {
        g_in.keyBits = (uint64_t(U32()) << 32) | U32();
    }
    g_in.buttons = P(0.3) ? 0 : P(0.6) ? U32() & 0x1F : U32();
    const auto delta = []() { return P(0.3) ? 0 : P(0.5) ? Pick({10, 11, -10, -11, 9, -9, 400, -400, 401, -401, 1, -1}) : I(-1000, 1000); };
    g_in.dx = delta();
    g_in.dy = delta();
    g_in.wheel = delta();
    g_in.modifiers = P(0.4) ? 0 : P(0.7) ? (1u << I(5, 10)) | (P(0.3) ? 1u << I(5, 10) : 0) : U32();
    g_in.typed = P(0.4) ? 0 : P(0.8) ? I(1, 255) : Pick({0x100, 0x141, -1, -200});
    g_in.viewport = {I(-5, 5), I(-5, 5), I(0, 2000), I(0, 2000), F(0, 1), F(0, 1)};
    g_in.scrnX = I(0, 4000);
    g_in.scrnY = I(0, 4000);
    for (S_JoyInput &j : g_joyIn) {
        RandomJoyState(j.state);
        if (P(0.5))
            j.pressed = j.state;
        RandomJoyState(j.pressed);
        if (P(0.3)) {
            for (int a = 0; a < 8; ++a)
                if (P(0.5))
                    j.pressed.axis[a] = j.state.axis[a];
        }
        for (int a = 0; a < 8; ++a) {
            j.modes[a] = P(0.8) ? static_cast<uint32_t>(Pick({0, 1, 4, 5, 8, 9, 12, 13, 2})) : U32();
            j.axes[a] = P(0.3) ? j.state.axis[a] : P(0.3) ? j.pressed.axis[a] : RandomAxis();
            if (P(0.2))
                j.axes[a] = j.pressed.axis[a] + Pick({0.5f, -0.5f, 0.7f, -0.7f, 0.3f, -0.3f}) * (P(0.5) ? 1.0f : Ulp(1.0f, I(-1, 1)));
        }
    }
}

void ResetFakes()
{
    for (int i = 0; i < 4; ++i) {
        for (C_FakeJoy *j : {&g_joyP[i], &g_joyO[i]}) {
            j->index = i;
            j->refs = 0;
            std::memset(j->modeSet, 0, sizeof j->modeSet);
        }
    }
}

// The port's object in a random state (bindings made through SetBinding, so the watched keys and counts agree with them, then
// now and then disturbed).
void RandomState()
{
    C_Controls &c = *g_port;
    c.m_bindings.clear();
    c.m_held.clear();
    c.m_pressed.clear();
    c.m_joys.clear();
    std::memset(c.m_keys, 0, sizeof c.m_keys);
    std::memset(c.m_keyCounts, 0, sizeof c.m_keyCounts);
    std::memset(c.m_mouseCounts, 0, sizeof c.m_mouseCounts);
    c.m_graph = reinterpret_cast<IGraph *>(&g_graph);
    const int joys = P(0.2) ? 0 : I(1, 4);
    for (int i = 0; i < joys; ++i) {
        S_ControlsJoy j{};
        j.joy = &g_joyP[i];
        if (P(0.7)) {
            RandomJoyState(j.state);
            RandomJoyState(j.pressed);
        }
        c.m_joys.push_back(j);
    }
    const int bindings = P(0.1) ? 0 : I(1, P(0.1) ? 70 : 24);
    for (int i = 0; i < bindings; ++i) {
        C_Controls::S_Press b = RandomBinding();
        c.SetBinding(&b, P(0.8) ? i : I(0, bindings));
    }
    if (P(0.1) && !c.m_bindings.empty())
        c.SetBinding(nullptr, I(0, int(c.m_bindings.size()) - 1));
    // (A joystick binding whose joystick is not there: SetBinding refuses to make one.)
    if (P(0.1) && !c.m_bindings.empty()) {
        C_Controls::S_Press &b = c.m_bindings[size_t(I(0, int(c.m_bindings.size()) - 1))];
        b.device = JOY;
        b.key = RandomKey(JOY);
        b.unknown08 = P(0.5) ? int(c.m_joys.size()) : -1;
    }
    if (P(0.1)) {
        for (int k = I(1, 4); k > 0; --k)
            c.m_keys[I(0, 63)] = static_cast<char>(I(0, 255));
    }
    if (P(0.1)) {
        for (int k = I(1, 4); k > 0; --k)
            c.m_keyCounts[I(0, 63)] = static_cast<char>(I(-2, 3));
    }
    if (P(0.1)) {
        for (int k = I(1, 3); k > 0; --k)
            c.m_mouseCounts[I(0, 7)] = static_cast<char>(I(-2, 3));
    }
    for (size_t i = 0; i < c.m_held.size(); ++i) {
        c.m_held[i] = P(0.5) ? 0.0f : F(-2, 2);
        c.m_pressed[i] = P(0.5) ? 0.0f : F(-2, 2);
    }
    c.m_press.direction = int(U32());
    c.m_press.device = static_cast<uint16_t>(U32());
    c.m_press.key = static_cast<uint16_t>(U32());
    c.m_press.unknown08 = int(U32());
    const float scales[] = {1.0f, 20.0f, 400.0f, 0.5f, 3.0f, 0.0f};
    for (float &s : c.m_scale)
        s = P(0.9) ? Pick(scales) : F(0.1f, 1000.0f);
    c.m_invertMouse = P(0.5);
    c.m_keyArmed = P(0.3) ? ~uint64_t(0) : P(0.5) ? 0 : (uint64_t(U32()) << 32) | U32();
    c.m_mouseArmed = P(0.3) ? ~0u : P(0.5) ? 0 : U32();
    RandomInput();
}

// ---- the calls on both sides ----
template <class PortCall, class OrigCall> void Diff(PortCall port, OrigCall orig)
{
    ToOrig();
    ResetFakes();
    g_log.clear();
    port();
    const std::vector<std::string> portLog = g_log;
    g_log.clear();
    orig();
    CHECK(g_log == portLog, "calls: %s / %s", Joined(g_log).c_str(), Joined(portLog).c_str());
    for (int i = 0; i < 4; ++i) {
        CHECK(g_joyO[i].refs == g_joyP[i].refs, "joy%d refs %d / %d", i, g_joyO[i].refs, g_joyP[i].refs);
        CHECK(std::memcmp(g_joyO[i].modeSet, g_joyP[i].modeSet, sizeof g_joyP[i].modeSet) == 0, "joy%d modes", i);
    }
    CompareObjects();
}

void TestUpdate()
{
    const bool all = P(0.5);
    const int before = failures;
    Diff([&] { g_port->Update(all); }, [&] { OriginalAt<UpdateFn>(0x006c7bc0)(g_orig, all); });
    if (failures != before && std::getenv("HD2_CONTROLS_DUMP")) {
        const C_Controls &c = *g_port;
        for (size_t i = 0; i < c.m_bindings.size() && i * 4 < OVecBytes(O_HELD); ++i) {
            const float ho = OVecData<float>(O_HELD)[i], po = OVecData<float>(O_PRESSED)[i];
            if (Bits(ho) == Bits(c.m_held[i]) && Bits(po) == Bits(c.m_pressed[i]))
                continue;
            const C_Controls::S_Press &b = c.m_bindings[i];
            std::printf("  binding %u: device %u key %x direction %d joy %d: held %g / %g pressed %g / %g\n", unsigned(i), b.device, b.key,
                        b.direction, b.unknown08, double(ho), double(c.m_held[i]), double(po), double(c.m_pressed[i]));
            if (b.device == 3 && size_t(b.unknown08) < c.m_joys.size()) {
                const S_ControlsJoy &j = c.m_joys[size_t(b.unknown08)];
                const int hat = (b.key >> 4) & 0xF;
                if (b.key >= 0x200 && hat < 4)
                    std::printf("    pov %.9g centered %d / pressed pov %.9g centered %d\n", double(j.state.pov[hat]), j.state.povCentered[hat],
                                double(j.pressed.pov[hat]), j.pressed.povCentered[hat]);
                else if (b.key < 8)
                    std::printf("    axis %.9g\n", double(j.state.axis[b.key]));
            }
            if (b.device == 2)
                std::printf("    mouse %d %d %d scale %g %g %g\n", g_in.dx, g_in.dy, g_in.wheel, double(c.m_scale[0]), double(c.m_scale[1]),
                            double(c.m_scale[2]));
        }
    }
}

void TestPoll()
{
    const int filter = P(0.5) ? I(0, 0x3F) : P(0.5) ? Pick({0x9A, 0x80, 0x9B, 0x3F, 0}) : I(0, 0xFF);
    const bool reread = P(0.5);
    const void *portResult = nullptr, *origResult = nullptr;
    Diff([&] { portResult = g_port->PollPress(filter, reread); },
         [&] { origResult = OriginalAt<PollFn>(0x006c87f0)(g_orig, filter, reread); });
    CHECK((portResult == nullptr) == (origResult == nullptr), "poll result %p / %p", origResult, portResult);
    if (std::getenv("HD2_CONTROLS_DUMP") && std::memcmp(g_orig + O_PRESS, &g_port->m_press, PRESS_SIZE) != 0) {
        std::printf("  filter %x\n", filter);
        for (size_t i = 0; i < g_port->m_joys.size(); ++i) {
            for (int a = 0; a < 8; ++a)
                std::printf("  joy %u axis %d: value %.9g (%08x) rest %.9g (%08x) mode %x\n", unsigned(i), a, double(g_joyIn[i].axes[a]),
                            Bits(g_joyIn[i].axes[a]), double(g_port->m_joys[i].pressed.axis[a]), Bits(g_port->m_joys[i].pressed.axis[a]),
                            g_joyIn[i].modes[a]);
        }
    }
    if (portResult)
        CHECK(portResult == &g_port->m_press && origResult == g_orig + O_PRESS, "poll result's place");
}

void TestBind()
{
    const int action = P(0.9) ? I(0, int(g_port->m_bindings.size()) + 3) : Pick({-1, -100, 200});
    const C_Controls::S_Press b = RandomBinding();
    Diff([&] { g_port->Bind(action, static_cast<int16_t>(b.device), b.key, b.unknown08, b.direction); },
         [&] { OriginalAt<BindFn>(0x006c7790)(g_orig, action, static_cast<int16_t>(b.device), b.key, b.unknown08, b.direction); });
}

void TestSetBinding()
{
    const int action = P(0.9) ? I(0, int(g_port->m_bindings.size()) + 3) : Pick({-1, -100, 70});
    // (Many keys watched now and then: the 64 slots full, or all but the last.)
    if (P(0.1)) {
        C_Controls &c = *g_port;
        const int full = P(0.5) ? 64 : 63;
        for (int i = 0; i < 64; ++i) {
            c.m_keys[i] = i < full ? static_cast<char>(i + 0x40) : 0;
            c.m_keyCounts[i] = i < full ? 1 : 0;
        }
    }
    const C_Controls::S_Press b = RandomBinding();
    const bool none = P(0.1);
    bool portResult = false, origResult = false;
    Diff([&] { portResult = g_port->SetBinding(none ? nullptr : &b, action); },
         [&] { origResult = OriginalAt<SetBindingFn>(0x006c77d0)(g_orig, none ? nullptr : &b, action); });
    CHECK(portResult == origResult, "SetBinding %d / %d", origResult, portResult);
}

void TestClear(bool byAction)
{
    C_Controls &c = *g_port;
    if (byAction && c.m_bindings.empty())
        return;
    const int what = byAction ? I(0, int(c.m_bindings.size()) - 1)
                              : (P(0.7) && !c.m_bindings.empty() ? int(c.m_bindings[size_t(I(0, int(c.m_bindings.size()) - 1))].key)
                                                                 : int(RandomKey(I(1, 3))));
    // Own arrays (one null: the controls' own), or the caller's.
    const int arrays = I(0, 3);
    const size_t n = c.m_bindings.size() + 1;
    std::vector<float> heldP(n), pressedP(n);
    for (size_t i = 0; i < n; ++i) {
        heldP[i] = F(-1, 1);
        pressedP[i] = F(-1, 1);
    }
    std::vector<float> heldO = heldP, pressedO = pressedP;
    float *hp = arrays & 1 ? heldP.data() : nullptr, *pp = arrays & 2 ? pressedP.data() : nullptr;
    float *ho = arrays & 1 ? heldO.data() : nullptr, *po = arrays & 2 ? pressedO.data() : nullptr;
    const uint32_t at = byAction ? 0x006c73f0 : 0x006c7510;
    Diff([&] { byAction ? c.ClearAction(what, hp, pp) : c.ClearKey(what, hp, pp); }, [&] { OriginalAt<ClearFn>(at)(g_orig, what, ho, po); });
    CHECK(heldO == heldP && pressedO == pressedP, "the caller's arrays");
}

void TestValues()
{
    const bool pressed = P(0.5);
    if (P(0.5)) {
        float *vp = nullptr, *vo = nullptr;
        int np = 0, no = 0;
        Diff([&] { np = g_port->GetValues(&vp, pressed); }, [&] { no = OriginalAt<GetValuesFn>(0x006c7600)(g_orig, &vo, pressed); });
        CHECK(np == no, "GetValues %d / %d", no, np);
        if (np == no && np > 0)
            CHECK(std::memcmp(vp, vo, size_t(np) * 4) == 0 && vo == OVecData<float>(pressed ? O_PRESSED : O_HELD), "GetValues' values");
        if (np < 0)
            CHECK(vp == nullptr && vo == nullptr, "GetValues: values set when none");
    } else {
        const int action = P(0.8) ? I(-1, int(g_port->m_held.size())) : Pick({-100, 1000, int(0x80000000)});
        float vp = 0, vo = 0;
        Diff([&] { vp = g_port->GetValue(action, pressed); }, [&] { vo = OriginalAt<GetValueFn>(0x006c76a0)(g_orig, action, pressed); });
        CHECK(Bits(vp) == Bits(vo), "GetValue %g / %g", double(vo), double(vp));
    }
}

void TestScale()
{
    const auto v = []() { return P(0.7) ? Pick({0, 1, 20, 400, -3, 1000}) : I(-100000, 100000); };
    const int x = v(), y = v(), z = v();
    bool rp = false, ro = false;
    Diff([&] { rp = g_port->SetMouseScale(x, y, z); }, [&] { ro = OriginalAt<SetMouseScaleFn>(0x006c6fe0)(g_orig, x, y, z); });
    CHECK(rp == ro, "SetMouseScale %d / %d", ro, rp);
}

void TestAttach()
{
    const int count = I(0, 4);
    C_FrameListener *listP[4] = {};
    void *listO[4] = {};
    for (int i = 0; i < count; ++i) {
        const int which = I(0, 3);
        const bool null = P(0.05);
        listP[i] = null ? nullptr : &g_joyP[which];
        listO[i] = null ? nullptr : static_cast<void *>(&g_joyO[which]);
    }
    IGraph *graph = P(0.05) ? nullptr : reinterpret_cast<IGraph *>(&g_graph);
    // (The graph before: none, or another, so that its store shows.)
    if (P(0.5))
        g_port->m_graph = P(0.5) ? nullptr : reinterpret_cast<IGraph *>(&g_graphVt);
    bool rp = false, ro = false;
    if (P(0.3)) {
        // (BindControls: the same, its result dropped.)
        Diff([&] { BindControls(*g_port, graph, listP, unsigned(count)); },
             [&] { ro = OriginalAt<AttachFn>(0x006c7010)(g_orig, graph, listO, unsigned(count)); });
        g_port->m_graph = reinterpret_cast<IGraph *>(&g_graph);   // (for the calls after, when refused)
        return;
    }
    Diff([&] { rp = g_port->Attach(graph, listP, unsigned(count)); },
         [&] { ro = OriginalAt<AttachFn>(0x006c7010)(g_orig, graph, listO, unsigned(count)); });
    CHECK(rp == ro, "Attach %d / %d", ro, rp);
    g_port->m_graph = reinterpret_cast<IGraph *>(&g_graph);   // (for the calls after, when refused)
}

// SetInvertMouse: the original has no function (its callers store the byte at +0xB4).
void TestInvert()
{
    const bool on = P(0.5);
    Diff([&] { g_port->SetInvertMouse(on); }, [&] { OF<bool>(O_INVERT) = on; });
}

void TestInit()
{
    Diff([&] { InitControls(*g_port); }, [&] { OriginalAt<InitFn>(0x006c7340)(g_orig); });
}

void TestShutdown()
{
    Diff([&] { g_port->Shutdown(); }, [&] { OriginalAt<ThisFn>(0x006c6eb0)(g_orig); });
}

// The constructor (the fields it sets) and the destructor (the joysticks released).
void TestLifetime()
{
    ToOrig();
    ResetFakes();
    g_log.clear();
    g_port->~C_Controls();
    const std::vector<std::string> portLog = g_log;
    g_log.clear();
    OriginalAt<ThisFn>(0x006c6e00)(g_orig);
    CHECK(g_log == portLog, "destructor's calls: %s / %s", Joined(g_log).c_str(), Joined(portLog).c_str());
    for (int i = 0; i < 4; ++i)
        CHECK(g_joyO[i].refs == g_joyP[i].refs, "joy%d refs %d / %d", i, g_joyO[i].refs, g_joyP[i].refs);
    std::memset(g_orig, 0xCD, 0x100);
    new (g_port) C_Controls();
    OriginalAt<ThisFn>(0x006c6d70)(g_orig);
    CompareObjects(true);
}

void TestWide()
{
    wchar_t text[40];
    const int length = I(0, 30);
    for (int i = 0; i < length; ++i) {
        const int kind = I(0, 9);
        text[i] = kind < 6 ? wchar_t(I(0x20, 0x7E)) : kind < 8 ? wchar_t(Pick({0xE9, 0xFC, 0xDF, 0x20AC, 0x0161, 0x10D})) : wchar_t(I(0x80, 0xFFFF));
        if (text[i] == 0 || (text[i] >= 0xD800 && text[i] < 0xE000))
            text[i] = L'x';
    }
    text[length] = 0;
    const int max = P(0.8) ? I(0, length + 3) : Pick({-1, 0, 39});
    char outP[80], outO[80];
    const int fill = I(0, 255);
    std::memset(outP, fill, sizeof outP);
    std::memset(outO, fill, sizeof outO);
    const int nulls = P(0.1) ? I(1, 3) : 0;
    char *op = nulls & 1 ? nullptr : outP, *oo = nulls & 1 ? nullptr : outO;
    const wchar_t *t = nulls & 2 ? nullptr : text;
    if (max < 0 && op)
        return;   // (out[-1] written: both sides, but outside the buffer)
    SetLastError(0);
    WideToAnsi(op, t, max);
    const DWORD errorP = GetLastError();
    SetLastError(0);
    OriginalAt<WideFn>(0x006c90c0)(oo, t, max);
    const DWORD errorO = GetLastError();
    CHECK(std::memcmp(outP, outO, sizeof outP) == 0, "WideToAnsi(%d of %d): %s / %s", max, length, Hex(outO, 40).c_str(), Hex(outP, 40).c_str());
    CHECK(errorP == errorO, "WideToAnsi's error %lu / %lu", errorO, errorP);
}

void TestRender()
{
    C_game *game = GetGame();
    auto *interfaces = reinterpret_cast<C_Interface *>(g_interfaceStorage);
    C_ScriptModule *module = &g_scriptModule;
    const int state = P(0.3) ? GAME_STATE_GAME_MENU : P(0.5) ? Pick({5, 6, 4, 1}) : I(0, 16);
    const int endDelay = P(0.5) ? 0 : P(0.5) ? 1 : int(U32());
    const bool active = P(0.6), cutscene = P(0.3), fake = P(0.3);
    const int phase = P(0.9) ? I(1, 2) : Pick({0, 3, -1, 6});
    RandomInput();
    game->m_state = state;
    game->m_endDelay = endDelay;
    interfaces->m_flags = active ? INTERFACE_ACTIVE | (U32() & ~1u) : U32() & ~1u;
    module->m_cutsceneRunning = cutscene;
    module->m_fakeCutscene = fake;
    *reinterpret_cast<int *>(g_gameO + 4) = state;
    *reinterpret_cast<int *>(g_gameO + 0x24) = endDelay;
    *reinterpret_cast<uint32_t *>(g_interfaceO + 0x0C) = interfaces->m_flags;
    *OriginalAt<bool *>(0x0086933c) = cutscene;
    *OriginalAt<bool *>(0x0086933d) = fake;
    g_log.clear();
    const int rp = GameRenderCallback(phase, I(0, 9), I(0, 9));
    const std::vector<std::string> portLog = g_log;
    g_log.clear();
    const int ro = OriginalAt<RenderFn>(0x006c95b0)(phase, I(0, 9), I(0, 9));
    CHECK(g_log == portLog, "render calls (phase %d state %d): %s / %s", phase, state, Joined(g_log).c_str(), Joined(portLog).c_str());
    CHECK(rp == ro, "render result %d / %d", ro, rp);
    ++checks;
}

void SetUp()
{
    g_graph.vtable = g_graphVt;
    for (void *&slot : g_graphVt)
        slot = reinterpret_cast<void *>(G_Other);
    const auto at = [](int offset, void *f) { g_graphVt[offset / 4] = f; };
    at(0x14, reinterpret_cast<void *>(G_Process));
    at(0xE8, reinterpret_cast<void *>(G_Begin));
    at(0x9C, reinterpret_cast<void *>(G_Read));
    at(0xB4, reinterpret_cast<void *>(G_Watch));
    at(0xB8, reinterpret_cast<void *>(G_Bits));
    at(0x104, reinterpret_cast<void *>(G_Buttons));
    at(0xEC, reinterpret_cast<void *>(G_Dx));
    at(0xF0, reinterpret_cast<void *>(G_Dy));
    at(0xF4, reinterpret_cast<void *>(G_Wheel));
    at(0xC8, reinterpret_cast<void *>(G_Modifiers));
    at(0xA0, reinterpret_cast<void *>(G_Typed));
    at(0x18, reinterpret_cast<void *>(G_ScrnX));
    at(0x1C, reinterpret_cast<void *>(G_ScrnY));
    at(0x7C, reinterpret_cast<void *>(G_SetViewport));
    at(0x80, reinterpret_cast<void *>(G_GetViewport));
    // The original's joysticks: the port's vtable a slot down (g++ has two destructors at its start).
    void **portVt = *reinterpret_cast<void ***>(&g_joyP[0]);
    for (int i = 0; i + 1 < 32; ++i)
        g_joyOrigVt[i] = portVt[i + 1];
    g_joyOrigVt[0] = reinterpret_cast<void *>(J_Destroy);
    for (C_FakeJoy &j : g_joyO)
        *reinterpret_cast<void ***>(&j) = g_joyOrigVt;
    // The game around the render callback, the port's (its vtable a slot on: two destructors) and the original's.
    C_game *game = GetGame();
    *reinterpret_cast<void ***>(game) = g_gameVt;
    g_gameVt[0x20 / 4 + 1] = reinterpret_cast<void *>(F_GetState);
    game->m_interface = reinterpret_cast<C_Interface *>(g_interfaceStorage);
    game->m_quickMessages = reinterpret_cast<C_QuickMessages *>(g_interfaceStorage + 0x1000);
    g_effects.vtable = g_effectsVt;
    g_effectsVt[0xA0 / 4 + 1] = reinterpret_cast<void *>(E_Fade);
    g_effectsVt[0xA4 / 4 + 1] = reinterpret_cast<void *>(E_Volumetrics);
    *reinterpret_cast<void ***>(g_gameO) = g_gameOVt;
    g_gameOVt[0x20 / 4] = reinterpret_cast<void *>(FO_GetState);
    *reinterpret_cast<void **>(g_gameO + 0x58) = g_interfaceO;
    *reinterpret_cast<void **>(g_gameO + 0x60) = g_quickO;
    g_effectsO.vtable = g_effectsOVt;
    g_effectsOVt[0xA0 / 4] = reinterpret_cast<void *>(E_Fade);
    g_effectsOVt[0xA4 / 4] = reinterpret_cast<void *>(E_Volumetrics);
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(O_New));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(O_Delete));
    RedirectOriginal(0x006987d0, reinterpret_cast<const void *>(O_GetGame));
    RedirectOriginal(0x00486510, reinterpret_cast<const void *>(O_GetGEffects));
    RedirectOriginal(0x007e530e, reinterpret_cast<const void *>(O_GetIGraph));
    RedirectOriginal(0x0049b9f0, reinterpret_cast<const void *>(O_Render));
    RedirectOriginal(0x0049b9b0, reinterpret_cast<const void *>(O_RenderOverlay));
    RedirectOriginal(0x0048f390, reinterpret_cast<const void *>(O_QuickRender));
    RedirectOriginal(0x0069b0e0, reinterpret_cast<const void *>(O_ApplyFade));
    RedirectOriginal(0x006e8860, reinterpret_cast<const void *>(O_Stats));
    RedirectOriginal(0x0079bbc0, reinterpret_cast<const void *>(O_DebugDraw));
    // The port's object, made by its constructor; the original's by its own.
    alignas(16) static unsigned char storage[sizeof(C_Controls)];
    g_port = new (storage) C_Controls();
    OriginalAt<ThisFn>(0x006c6d70)(g_orig);
}

}  // namespace

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("controls_diff: exception %08lx at %p\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);   // _PC_24, _MCW_PC: the game's precision
    if (!LoadOriginalExe()) {
        std::printf("controls_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_CONTROLS_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_CONTROLS_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_CONTROLS_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Update", TestUpdate},
        {"PollPress", TestPoll},
        {"Bind", TestBind},
        {"SetBinding", TestSetBinding},
        {"ClearAction", [] { TestClear(true); }},
        {"ClearKey", [] { TestClear(false); }},
        {"GetValues", TestValues},
        {"SetMouseScale", TestScale},
        {"SetInvertMouse", TestInvert},
        {"Attach", TestAttach},
        {"InitControls", TestInit},
        {"Shutdown", TestShutdown},
        {"Lifetime", TestLifetime},
        {"WideToAnsi", TestWide},
        {"GameRenderCallback", TestRender},
    };
    for (const S_Case &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round) {
            RandomState();
            c.run();
        }
    }
    // (Each call's results again on states the calls themselves left: a run of calls on one object.)
    if (!only) {
        g_case = "sequence";
        RandomState();
        for (g_round = 0; g_round < rounds; ++g_round) {
            if (P(0.1))
                RandomInput();
            switch (I(0, 9)) {
            case 0:
            case 1:
                TestUpdate();
                break;
            case 2:
                TestPoll();
                break;
            case 3:
            case 4:
                TestBind();
                break;
            case 5:
                TestSetBinding();
                break;
            case 6:
                TestClear(P(0.5));
                break;
            case 7:
                TestValues();
                break;
            case 8:
                if (P(0.1))
                    TestInit();
                else if (P(0.1))
                    TestAttach();
                else
                    TestScale();
                break;
            default:
                if (P(0.02))
                    RandomState();
                break;
            }
        }
    }
    std::printf("controls_diff: %d checks, %d failures\n", checks, failures);
    // (The original's joysticks back to the port's vtable for their destructors.)
    for (C_FakeJoy &j : g_joyO)
        *reinterpret_cast<void ***>(&j) = *reinterpret_cast<void ***>(&g_joyP[0]);
    return failures != 0;
}
