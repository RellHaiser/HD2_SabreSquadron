// The controls (part 11c: game/input_controls.cpp), the graphics interface and the joysticks faked. MinGW; build_controls.bat; run
// controls_test.exe ("controls: N checks, 0 failures").
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char text[200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_log.push_back(text);
}
std::string TakeLog()
{
    std::string all;
    for (const std::string &line : g_log)
        all += line + "\n";
    g_log.clear();
    return all;
}

// ---- the graphics interface ----
struct S_Input {
    uint64_t keyBits = 0;
    uint32_t buttons = 0;
    int dx = 0, dy = 0, wheel = 0;
    uint32_t modifiers = 0;
    int typed = 0;
    S_IGraphViewport viewport = {1, 2, 3, 4, 0.5f, 0.75f};
    char watched[0x40] = {};
} g_in;
void __stdcall G_Process(void *) { Log("process"); }
void __stdcall G_Begin(void *) { Log("begin"); }
void __stdcall G_Read(void *) { Log("read"); }
void __stdcall G_Watch(void *, const char *keys, int count)
{
    Log("watch %d", count);
    std::memcpy(g_in.watched, keys, 0x40);
}
uint64_t __stdcall G_Bits(void *) { return g_in.keyBits; }
uint32_t __stdcall G_Buttons(void *) { return g_in.buttons; }
int __stdcall G_Dx(void *) { return g_in.dx; }
int __stdcall G_Dy(void *) { return g_in.dy; }
int __stdcall G_Wheel(void *) { return g_in.wheel; }
uint32_t __stdcall G_Modifiers(void *, int arg) { CHECK(arg == 0); return g_in.modifiers; }
int __stdcall G_Typed(void *) { return g_in.typed; }
const S_IGraphViewport *__stdcall G_GetViewport(void *) { return &g_in.viewport; }
void __stdcall G_SetViewport(void *, const S_IGraphViewport &v) { Log("viewport %d %d %d %d %g %g", v.x, v.y, v.width, v.height, v.minZ, v.maxZ); }
int __stdcall G_ScrnX(void *) { return 640; }
int __stdcall G_ScrnY(void *) { return 480; }
void *g_graphVt[128];
struct S_FakeGraph { void **vtable; } g_graph;

// ---- joysticks ----
class C_FakeJoy : public C_FrameListener {
public:
    int refs = 0;
    uint32_t modes[8] = {};
    uint32_t modeSet[8] = {};
    float axes[8] = {};
    S_JoyState state = {}, pressed = {};
    C_FakeJoy()
    {
        for (bool &c : state.povCentered)
            c = true;
        for (bool &c : pressed.povCentered)
            c = true;
    }
    int AddRef() override { return ++refs; }
    int Release() override { Log("joy release"); return --refs; }
    float GetDeadZone(int) override { return 0; }
    bool SetDeadZone(int, float) override { return true; }
    float GetDeviceDeadZone(int) override { return 0; }
    bool SetDeviceDeadZone(int, float) override { return true; }
    float GetSaturation(int) override { return 0; }
    bool SetSaturation(int, float) override { return true; }
    float GetDeviceSaturation(int) override { return 0; }
    bool SetDeviceSaturation(int, float) override { return true; }
    uint32_t GetAxisMode(int axis) override { return modes[axis]; }
    bool SetAxisMode(int axis, uint32_t mode) override { Log("axis mode %d %u", axis, mode); if (axis >= 0 && axis < 8) modeSet[axis] = mode; return true; }
    float GetAxis(int axis) override { return axes[axis]; }
    bool IsButtonDown(int) override { return false; }
    bool IsButtonPressed(int) override { return false; }
    uint32_t GetButtons(int) override { return 0; }
    uint32_t GetButtonsPressed(int) override { return 0; }
    bool GetPov(float *, int) override { return false; }
    bool GetPovChanged(float *, int) override { return false; }
    bool GetState(S_JoyState *out) override { *out = state; return true; }
    bool GetStatePressed(S_JoyState *out) override { *out = pressed; return true; }
    bool OnFrame(uint32_t flags) override { Log("joy update %u", flags); return true; }
};
}  // namespace

// ---- the world around the render callback ----
namespace {
alignas(16) unsigned char g_gameStorage[0x4000];
alignas(16) unsigned char g_interfaceStorage[0x2000];
void *g_gameVt[32];
int __thiscall F_GetState(C_GameState *state) { return state->m_state; }
void *g_effectsVt[64];
struct S_FakeEffects { void **vtable; } g_effects;
void __thiscall E_Volumetrics(void *) { Log("volumetrics"); }
void __thiscall E_Fade(void *) { Log("fade"); }
}  // namespace
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameStorage); }
IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graph); }
C_GEffectsModule *GetGEffects() { return reinterpret_cast<C_GEffectsModule *>(&g_effects); }
void C_Interface::RenderOverlay() { Log("interface overlay"); }
void C_Interface::Render() { Log("interface render"); }
void C_QuickMessages::Render() { Log("quick messages"); }
void C_game::ApplyFade() { Log("apply fade"); }
void RenderDebugStats() { Log("stats"); }

namespace {

C_Controls &Fresh(C_FakeJoy *joy = nullptr, C_FakeJoy *joy2 = nullptr)
{
    static alignas(C_Controls) unsigned char storage[sizeof(C_Controls)];
    auto *c = new (storage) C_Controls();
    InitControls(*c);
    C_FrameListener *list[2] = {joy, joy2};
    BindControls(*c, reinterpret_cast<IGraph *>(&g_graph), list, joy2 ? 2u : joy ? 1u : 0u);
    g_log.clear();
    g_in = S_Input{};
    return *c;
}
const int KEY = 1, MOUSE = 2, JOY = 3;

void TestConstruct()
{
    C_Controls c;
    CHECK(c.m_scale[0] == 20.0f && c.m_scale[1] == 20.0f && c.m_scale[2] == 20.0f);
    CHECK(c.m_graph == nullptr);
    // No graph or a null joystick: refused, nothing changed.
    C_FakeJoy joy;
    C_FrameListener *list[2] = {&joy, nullptr};
    {
        C_FrameListener *firstNull[2] = {nullptr, &joy};
        BindControls(c, reinterpret_cast<IGraph *>(&g_graph), firstNull, 2);
        CHECK(c.m_graph == nullptr && joy.refs == 0);
    }
    BindControls(c, nullptr, list, 1);
    CHECK(c.m_graph == nullptr && joy.refs == 0);
    BindControls(c, reinterpret_cast<IGraph *>(&g_graph), list, 2);
    CHECK(c.m_graph == nullptr && joy.refs == 0 && c.m_scale[0] == 20.0f);
    c.m_keys[3] = 9;
    c.m_keyCounts[3] = 9;
    c.m_mouseCounts[1] = 9;
    c.m_invertMouse = true;
    BindControls(c, reinterpret_cast<IGraph *>(&g_graph), list, 1);
    CHECK(c.m_graph == reinterpret_cast<IGraph *>(&g_graph) && joy.refs == 1 && c.m_joys.size() == 1);
    CHECK(c.m_keys[3] == 0 && c.m_keyCounts[3] == 0 && c.m_mouseCounts[1] == 0 && !c.m_invertMouse);
    CHECK(c.m_scale[0] == 400.0f && c.m_scale[1] == 400.0f && c.m_scale[2] == 1.0f && c.m_keyArmed == ~0ull);
    // InitControls: the bindings and values gone, the watched keys too.
    c.Bind(2, KEY, 0x10, 0, 0);
    CHECK(c.m_bindings.size() == 3 && c.m_held.size() == 3 && c.m_pressed.size() == 3);
    InitControls(c);
    CHECK(c.m_bindings.empty() && c.m_held.empty() && c.m_pressed.empty() && c.m_keys[0] == 0 && c.m_keyCounts[0] == 0);
    float *values = nullptr;
    CHECK(c.GetValues(&values, false) == -1 && c.GetValues(&values, true) == -1 && values == nullptr);
    // The end: each joystick released, last first.
    g_log.clear();
    c.~C_Controls();
    CHECK(TakeLog() == "joy release\n" && joy.refs == 0);
    new (&c) C_Controls();
}

void TestKeys()
{
    C_Controls &c = Fresh();
    // Slots by key: shared; freed with the last binding.
    c.Bind(0, KEY, 0x11, 0, 0);
    CHECK(TakeLog() == "watch 64\n" && g_in.watched[0] == 0x11 && c.m_keyCounts[0] == 1);
    c.Bind(1, KEY, 0x11, 0, 0);
    CHECK(g_log.empty() && c.m_keyCounts[0] == 2);
    c.Bind(2, KEY, 0x1E, 0, 0);
    CHECK(TakeLog() == "watch 64\n" && g_in.watched[1] == 0x1E);
    c.Bind(-1, KEY, 0x1F, 0, 0);
    CHECK(g_log.empty() && c.m_bindings.size() == 3);
    c.Update(false);
    CHECK(g_log.empty());                               // (no joysticks; nothing re-read)
    // Pressed: held, and pressed this frame once for each binding.
    g_in.keyBits = 1;
    c.Update(false);
    CHECK(c.GetValue(0, false) == 1.0f && c.GetValue(1, false) == 1.0f && c.GetValue(2, false) == 0.0f);
    CHECK(c.GetValue(0, true) == 1.0f && c.GetValue(1, true) == 1.0f && c.GetValue(2, true) == 0.0f);
    c.Update(false);
    CHECK(c.GetValue(0, false) == 1.0f && c.GetValue(0, true) == 0.0f && c.GetValue(1, true) == 0.0f);
    // Released, pressed again; the other slot (a high bit too).
    g_in.keyBits = 0;
    c.Update(false);
    CHECK(c.GetValue(0, false) == 0.0f && c.GetValue(0, true) == 0.0f);
    g_in.keyBits = 3;
    c.Update(false);
    CHECK(c.GetValue(0, true) == 1.0f && c.GetValue(2, true) == 1.0f && c.GetValue(2, false) == 1.0f);
    // Slot 40: past the first word.
    C_Controls &high = Fresh();
    for (int i = 0; i < 40; ++i)
        high.Bind(i, KEY, static_cast<uint16_t>(0x20 + i), 0, 0);
    high.Bind(40, KEY, 0x70, 0, 0);
    g_log.clear();
    g_in.keyBits = 1ull << 40;
    high.Update(false);
    CHECK(high.GetValue(40, false) == 1.0f && high.GetValue(40, true) == 1.0f && high.GetValue(0, false) == 0.0f && high.GetValue(8, false) == 0.0f);
    high.Update(false);
    CHECK(high.GetValue(40, true) == 0.0f);
    // A rebinding frees the old key's slot only with its last binding.
    C_Controls &re = Fresh();
    re.Bind(0, KEY, 0x11, 0, 0);
    re.Bind(1, KEY, 0x11, 0, 0);
    g_log.clear();
    re.Bind(0, KEY, 0x12, 0, 0);
    CHECK(TakeLog() == "watch 64\n" && re.m_keys[0] == 0x11 && re.m_keyCounts[0] == 1 && re.m_keys[1] == 0x12);
    re.Bind(1, KEY, 0x12, 0, 0);
    CHECK(TakeLog() == "watch 64\n" && re.m_keys[0] == 0 && re.m_keyCounts[0] == 0 && re.m_keyCounts[1] == 2);
    // The freed slot used again.
    re.Bind(2, KEY, 0x13, 0, 0);
    CHECK(re.m_keys[0] == 0x13);
    // 64 keys at most: the next refused, its action stays as it was.
    C_Controls &full = Fresh();
    for (int i = 0; i < 64; ++i)
        full.Bind(i, KEY, static_cast<uint16_t>(0x80 + i), 0, 0);
    CHECK(full.m_keys[63] == static_cast<char>(0x80 + 63));
    full.Bind(64, KEY, 0x40, 0, 0);
    CHECK(full.m_bindings.size() == 65 && full.m_bindings[64].device == 0);
    // Out of range values: 0.
    CHECK(c.GetValue(-1, false) == 0.0f && c.GetValue(99, false) == 0.0f && c.GetValue(-1, true) == 0.0f && c.GetValue(99, true) == 0.0f);
    float *values = nullptr;
    CHECK(full.GetValues(&values, false) == 65 && values == full.m_held.data());
    CHECK(full.GetValues(&values, true) == 65 && values == full.m_pressed.data());
}

void TestMouse()
{
    C_Controls &c = Fresh();
    c.Bind(0, MOUSE, 1, 0, 0);
    c.Bind(1, MOUSE, 1, 0, 0);
    c.Bind(2, MOUSE, 0x10, 0, 0);
    c.Bind(3, MOUSE, 0x100, 0, 1);
    c.Bind(4, MOUSE, 0x100, 0, 2);
    c.Bind(5, MOUSE, 0x200, 0, 0);
    c.Bind(6, MOUSE, 0x300, 0, 1);
    c.Bind(7, MOUSE, 0x77, 0, 0);
    CHECK(c.m_mouseCounts[0] == 2 && c.m_mouseCounts[4] == 1);
    // (The original never sets the buttons' armed bits: a first press right at the start is not told; a frame without arms them.)
    g_in.buttons = 0x11;
    c.Update(false);
    CHECK(c.GetValue(0, false) == 1.0f && c.GetValue(0, true) == 0.0f);
    g_in.buttons = 0;
    c.Update(false);
    g_in.buttons = 0x11;
    c.Update(false);
    CHECK(c.GetValue(0, false) == 1.0f && c.GetValue(0, true) == 1.0f && c.GetValue(1, true) == 1.0f && c.GetValue(2, true) == 1.0f);
    c.Update(false);
    CHECK(c.GetValue(0, false) == 1.0f && c.GetValue(0, true) == 0.0f && c.GetValue(2, true) == 0.0f);
    g_in.buttons = 0;
    c.Update(false);
    CHECK(c.GetValue(0, false) == 0.0f && c.GetValue(2, false) == 0.0f);
    // Axes, by the scale, cut at it: half directions.
    g_in.dx = 200;
    g_in.dy = -100;
    g_in.wheel = 5;                                     // (scale 1: cut to 1)
    c.Update(false);
    CHECK(c.GetValue(3, false) == 0.5f && c.GetValue(3, true) == 0.5f && c.GetValue(4, false) == 0.0f && c.GetValue(4, true) == 0.0f);
    CHECK(c.GetValue(5, false) == -0.25f && c.GetValue(6, false) == 1.0f && c.GetValue(6, true) == 1.0f);
    g_in.dx = -1000;
    g_in.dy = 1000;
    g_in.wheel = -5;
    c.Update(false);
    CHECK(c.GetValue(3, false) == 0.0f && c.GetValue(4, false) == 1.0f && c.GetValue(4, true) == 1.0f && c.GetValue(5, false) == 1.0f);
    CHECK(c.GetValue(6, false) == 0.0f);
    CHECK(c.GetValue(7, false) == 0.0f);                // (no such key)
    c.m_pressed[7] = 5.0f;
    c.Update(false);
    CHECK(c.GetValue(7, true) == 5.0f);                 // (left alone)
    // Rebinding a button: the count back.
    c.Bind(0, KEY, 0x11, 0, 0);
    CHECK(c.m_mouseCounts[0] == 1);
    c.SetMouseScale(10, 20, 30);
    CHECK(c.m_scale[0] == 10.0f && c.m_scale[1] == 20.0f && c.m_scale[2] == 30.0f);
    c.SetInvertMouse(true);
    CHECK(c.m_invertMouse);
    // Each button its own bit and count.
    {
        C_Controls &all = Fresh();
        const int KEYS[] = {1, 2, 4, 8, 0x10};
        for (int i = 0; i < 5; ++i)
            all.Bind(i, MOUSE, static_cast<uint16_t>(KEYS[i]), 0, 0);
        for (int i = 0; i < 5; ++i)
            CHECK(all.m_mouseCounts[i] == 1);
        all.Update(false);
        for (int i = 0; i < 5; ++i) {
            g_in.buttons = static_cast<uint32_t>(KEYS[i]);
            all.Update(false);
            for (int j = 0; j < 5; ++j)
                CHECK(all.GetValue(j, false) == (i == j ? 1.0f : 0.0f) && all.GetValue(j, true) == (i == j ? 1.0f : 0.0f));
        }
        // The scales apart; small moves count on their side only; a cut negative.
        all.SetMouseScale(100, 200, 400);
        all.Bind(0, MOUSE, 0x100, 0, 1);
        all.Bind(1, MOUSE, 0x200, 0, 2);
        all.Bind(2, MOUSE, 0x300, 0, 0);
        all.Bind(3, MOUSE, 0x100, 0, 2);
        g_in.buttons = 0;
        g_in.dx = 10;
        g_in.dy = -20;
        g_in.wheel = 40;
        all.Update(false);
        CHECK(all.GetValue(0, false) == 0.1f && all.GetValue(1, false) == 0.1f && all.GetValue(2, false) == 0.1f && all.GetValue(3, false) == 0.0f);
        g_in.dx = -10;
        g_in.dy = 20;
        g_in.wheel = -1000;
        all.Update(false);
        CHECK(all.GetValue(0, false) == 0.0f && all.GetValue(3, false) == 0.1f && all.GetValue(1, false) == 0.0f && all.GetValue(2, false) == -1.0f);
    }
}

void TestJoystick()
{
    C_FakeJoy a, b;
    C_Controls &c = Fresh(&a, &b);
    // Modes: the absolute direction on an axis key.
    c.Bind(0, JOY, 2, 1, 1);
    CHECK(TakeLog() == "axis mode 2 0\n");
    c.Bind(1, JOY, 2, 1, 3);
    CHECK(TakeLog() == "axis mode 2 4\n");
    c.Bind(2, JOY, 0x105, 0, 3);
    CHECK(TakeLog() == "axis mode 261 0\n");
    // A joystick not there: refused.
    c.Bind(3, JOY, 2, 2, 0);
    CHECK(g_log.empty() && c.m_bindings[3].device == 0);
    c.Bind(3, JOY, 2, 1, 2);
    c.Bind(4, JOY, 7, 0, 0);
    c.Bind(5, JOY, 8, 0, 0);                            // (past the axes)
    c.Bind(6, JOY, 0x21F, 0, 0);                        // (hat 1: every direction)
    c.Bind(7, JOY, 0x212, 0, 0);                        // (hat 1: right)
    g_log.clear();
    b.state.axis[2] = 0.5f;
    a.state.axis[7] = -0.25f;
    a.state.buttons[0] = 1u << 5;
    a.pressed.buttons[0] = 0;
    c.Update(true);
    CHECK(TakeLog() == "joy update 1\njoy update 1\nprocess\nbegin\nread\n");
    CHECK(c.GetValue(0, false) == 0.5f && c.GetValue(0, true) == 0.5f);
    CHECK(c.GetValue(1, false) == 0.5f);
    CHECK(c.GetValue(3, false) == 0.0f);
    CHECK(c.GetValue(4, false) == -0.25f && c.GetValue(4, true) == -0.25f);
    CHECK(c.GetValue(2, false) == 1.0f && c.GetValue(2, true) == 0.0f);
    CHECK(c.GetValue(5, false) == 0.0f);
    b.state.axis[2] = -0.5f;
    a.pressed.buttons[0] = 1u << 5;
    c.Update(false);
    CHECK(g_log.empty());
    CHECK(c.GetValue(0, false) == 0.0f && c.GetValue(3, false) == 0.5f && c.GetValue(3, true) == 0.5f && c.GetValue(1, false) == -0.5f);
    CHECK(c.GetValue(2, true) == 1.0f);
    // Small values on each side.
    b.state.axis[2] = 0.1f;
    c.Update(false);
    CHECK(c.GetValue(0, false) == 0.1f && c.GetValue(3, false) == 0.0f);
    b.state.axis[2] = -0.1f;
    c.Update(false);
    CHECK(c.GetValue(0, false) == 0.0f && c.GetValue(3, false) == 0.1f);
    // Key 8 is past the axes; the first button and the first axis.
    c.Bind(8, JOY, 0, 0, 0);
    a.state.axis[0] = 0.75f;
    a.state.buttons[0] |= 1;
    c.Bind(12, JOY, 0x100, 0, 0);
    g_log.clear();
    c.Update(false);
    CHECK(c.GetValue(8, false) == 0.75f && c.GetValue(12, false) == 1.0f);
    a.state.axis[0] = 0.0f;
    a.state.buttons[0] &= ~1u;
    // Hats by angle: each band.
    static const struct { float angle; int bits; } HATS[] = {
        {0.0f, 1}, {0.1f, 1}, {0.12f, 3}, {0.2f, 3}, {1.49f, 3}, {1.5f, 2}, {1.6f, 2}, {1.62f, 6}, {1.7f, 6}, {3.11f, 6}, {3.12f, 4}, {3.16f, 4}, {3.2f, 0xC},
        {4.68f, 0xC}, {4.69f, 8}, {4.72f, 8}, {4.75f, 9}, {4.8f, 9}, {6.2f, 9}};
    // Each direction apart: exactly the band's bits, held and pressed.
    for (int bit = 0; bit < 4; ++bit)
        c.Bind(20 + bit, JOY, static_cast<uint16_t>(0x210 + (1 << bit)), 0, 0);
    for (const auto &h : HATS) {
        a.state.pov[1] = h.angle;
        a.state.povCentered[1] = false;
        a.pressed.pov[1] = h.angle;
        a.pressed.povCentered[1] = false;
        c.Update(false);
        int held = 0, pressedBits = 0;
        for (int bit = 0; bit < 4; ++bit) {
            held |= c.GetValue(20 + bit, false) == 1.0f ? 1 << bit : 0;
            pressedBits |= c.GetValue(20 + bit, true) == 1.0f ? 1 << bit : 0;
        }
        const bool ok = held == h.bits && pressedBits == h.bits;
        if (!ok)
            std::printf("hat %g: %x %x\n", h.angle, held, pressedBits);
        CHECK(ok);
    }
    c.Bind(10, JOY, 0x211, 0, 0);
    a.state.pov[1] = a.pressed.pov[1] = 0.0f;
    // Centred: nothing. Only the pressed side centred: held only.
    a.state.povCentered[1] = true;
    a.pressed.povCentered[1] = true;
    c.Update(false);
    CHECK(c.GetValue(10, false) == 0.0f && c.GetValue(10, true) == 0.0f);
    a.state.povCentered[1] = false;
    c.Update(false);
    CHECK(c.GetValue(10, false) == 1.0f && c.GetValue(10, true) == 0.0f);
    a.state.povCentered[1] = true;
    a.pressed.povCentered[1] = false;
    c.Update(false);
    CHECK(c.GetValue(10, false) == 0.0f && c.GetValue(10, true) == 1.0f);
    // The bindings' list by key and action; with arrays given.
    float held[12], pressed[12];
    for (int i = 0; i < 12; ++i) {
        held[i] = 1.0f;
        pressed[i] = 1.0f;
    }
    c.Bind(9, KEY, 2, 0, 0);
    g_log.clear();
    c.ClearKey(2, held, pressed);                       // (any device: actions 0, 1, 3, 9)
    CHECK(held[0] == 0.0f && pressed[0] == 0.0f && held[1] == 0.0f && held[3] == 0.0f && held[9] == 0.0f && pressed[9] == 0.0f && held[4] == 1.0f && held[2] == 1.0f);
    for (int i = 0; i < 12; ++i) {
        held[i] = 1.0f;
        pressed[i] = 1.0f;
    }
    c.ClearAction(0, held, pressed);                    // (same device and key: not the keyboard's 9)
    CHECK(held[1] == 0.0f && pressed[1] == 0.0f && held[3] == 0.0f && held[9] == 1.0f && held[4] == 1.0f);
    // Its own arrays.
    for (size_t i = 0; i < c.m_held.size(); ++i) {
        c.m_held[i] = 1.0f;
        c.m_pressed[i] = 1.0f;
    }
    float other[12] = {};
    c.ClearKey(2, nullptr, other);
    CHECK(c.m_held[0] == 0.0f && c.m_pressed[0] == 0.0f && c.m_held[4] == 1.0f);
    c.m_held[0] = 1.0f;
    c.ClearKey(2, other, nullptr);
    CHECK(c.m_held[0] == 0.0f);
    c.m_held[1] = 1.0f;
    c.m_pressed[1] = 1.0f;
    c.ClearAction(0, nullptr, nullptr);
    CHECK(c.m_held[1] == 0.0f && c.m_pressed[1] == 0.0f && c.m_held[4] == 1.0f);
    c.m_held[1] = 1.0f;
    c.ClearAction(0, other, nullptr);
    CHECK(c.m_held[1] == 0.0f);
}

void TestPoll()
{
    C_FakeJoy a, b;
    C_Controls &c = Fresh(&a, &b);
    const auto poll = [&](int filter = 0) { return c.PollPress(filter, false); };
    CHECK(poll() == nullptr);
    CHECK(g_log.empty());
    c.PollPress(0, true);
    CHECK(TakeLog() == "joy update 1\njoy update 1\nprocess\nbegin\nread\n");
    // The keyboard: a typed key, the modifiers first by order; none of the known ones.
    g_in.typed = 0x1E;
    const C_Controls::S_Press *p = poll();
    CHECK(p == &c.m_press && p->device == 1 && p->key == 0x1E && p->direction == 0 && p->unknown08 == 0);
    static const struct { uint32_t bit; int key; } MODS[] = {{0x20, 0x2A}, {0x40, 0x36}, {0x80, 0x1D}, {0x100, 0x9D}, {0x200, 0x38}, {0x400, 0xB8}};
    for (const auto &m : MODS) {
        g_in.modifiers = m.bit | 0x800;
        p = poll();
        CHECK(p && p->device == 1 && p->key == m.key);
    }
    g_in.modifiers = 0x60;
    CHECK(poll()->key == 0x2A);
    g_in.modifiers = 0x800;
    p = poll();
    CHECK(p && p->device == 1 && p->key == 0);
    g_in.modifiers = 0;
    g_in.typed = 0x101;                                 // (a byte)
    CHECK(poll()->key == 1);
    CHECK(poll(0x20) == nullptr);
    g_in.typed = 0;
    // The mouse's buttons.
    for (int bit = 1; bit <= 0x10; bit <<= 1) {
        g_in.buttons = static_cast<uint32_t>(bit) | 0x20;
        p = poll();
        CHECK(p && p->device == 2 && p->key == bit);
    }
    g_in.buttons = 0x18;
    CHECK(poll()->key == 8);
    CHECK(poll(4) == nullptr);
    g_in.buttons = 0x20;
    CHECK(poll() == nullptr);
    g_in.buttons = 0;
    // Its moves, past 10.
    const auto move = [&](int dx, int dy, int wheel, int key, int direction) {
        g_in.dx = dx;
        g_in.dy = dy;
        g_in.wheel = wheel;
        const C_Controls::S_Press *r = poll();
        return r && r->device == 2 && r->key == key && r->direction == direction;
    };
    CHECK(move(11, 50, 50, 0x100, 1) && move(-11, 50, 50, 0x100, 2));
    CHECK(move(10, 11, 50, 0x200, 1) && move(-10, -11, 50, 0x200, 2));
    CHECK(move(10, 10, 11, 0x300, 1) && move(-10, -10, -11, 0x300, 2));
    g_in.dx = g_in.dy = g_in.wheel = 10;
    CHECK(poll() == nullptr);
    g_in.dx = 50;
    CHECK(poll(1) == nullptr);
    g_in.dx = 0;
    g_in.dy = 0;
    g_in.wheel = 0;
    // The last joystick first: its buttons.
    b.state.buttons[0] = 1u << 3;
    a.state.buttons[0] = 1;
    c.Update(false);
    g_log.clear();
    p = poll();
    CHECK(p && p->device == 3 && p->key == 0x103 && p->unknown08 == 1 && p->direction == 0);
    b.state.buttons[0] = 0;
    p = poll();
    CHECK(p && p->key == 0x100 && p->unknown08 == 0);
    a.state.buttons[0] = 1u << 31;
    CHECK(poll()->key == 0x11F);
    CHECK(poll(8) == nullptr);
    a.state.buttons[0] = 0;
    // Hats.
    static const struct { float angle; int bits; } HATS[] = {{0.0f, 1}, {1.5f, 1}, {1.51f, 2}, {3.12f, 2}, {3.13f, 4}, {4.69f, 4}, {4.7f, 8}};
    for (const auto &h : HATS) {
        a.state.pov[2] = h.angle;
        a.state.povCentered[2] = false;
        p = poll();
        const bool ok = p && p->device == 3 && p->key == 0x220 + h.bits && p->unknown08 == 0 && p->direction == 0;
        if (!ok)
            std::printf("poll hat %g\n", h.angle);
        CHECK(ok);
    }
    a.state.povCentered[0] = false;                     // (the first hat first)
    a.state.pov[0] = 0.0f;
    CHECK(poll()->key == 0x201);
    CHECK(poll(0x10) == nullptr);
    // Each hat its number; the joystick's index.
    a.state.povCentered[0] = true;
    p = poll();
    CHECK(p && p->key == 0x220 + 8);
    a.state.povCentered[0] = a.state.povCentered[2] = true;
    b.state.povCentered[3] = false;
    b.state.pov[3] = 0.0f;
    c.Update(false);
    g_log.clear();
    p = poll();
    CHECK(p && p->key == 0x231 && p->unknown08 == 1);
    b.state.povCentered[3] = true;
    c.Update(false);
    g_log.clear();
    // Axes: mode 1 (from the centre: 0.5, with 4: 0.3), others (from the rest: 0.7, with 4: 0.5), 8 (any change, unless told).
    const auto axis = [&](uint32_t mode, float value, float rest) {
        a.modes[3] = mode;
        a.axes[3] = value;
        a.pressed.axis[3] = rest;
        c.Update(false);
        g_log.clear();
        return poll();
    };
    // The first axis.
    a.modes[0] = 1;
    a.axes[0] = 0.6f;
    p = poll();
    CHECK(p && p->key == 0 && p->device == 3);
    a.axes[0] = 0.0f;
    a.modes[0] = 0;
    p = axis(1, 0.6f, 0.0f);
    CHECK(p && p->device == 3 && p->key == 3 && p->direction == 1 && p->unknown08 == 0);
    p = axis(1, -0.6f, 0.0f);
    CHECK(p && p->direction == 2);
    CHECK(axis(1, 0.5f, 0.0f) == nullptr && axis(5, 0.29f, 0.0f) == nullptr);
    p = axis(5, 0.31f, 0.0f);
    CHECK(p && p->direction == 1);
    p = axis(0, 0.9f, 0.1f);
    CHECK(p && p->direction == 1);
    p = axis(0, 0.1f, 0.9f);
    CHECK(p && p->direction == 2);
    p = axis(1, 0.51f, 0.0f);
    CHECK(p && p->direction == 1);
    p = axis(8, 0.1f, 0.0f);
    CHECK(p && p->direction == 1);
    // Mode bit 4 alone sets the smaller limit, not bit 1.
    CHECK(axis(1, 0.45f, 0.0f) == nullptr);
    p = axis(4, 0.6f, 0.0f);
    CHECK(p && p->direction == 1);
    CHECK(axis(0, 0.6f, 0.0f) == nullptr);
    CHECK(axis(0, 0.7f, 0.0f) == nullptr && axis(4, 0.5f, 0.0f) == nullptr);
    p = axis(4, 0.51f, 0.0f);
    CHECK(p && p->direction == 1);
    p = axis(8, 0.01f, 0.0f);
    CHECK(p && p->direction == 1);
    p = axis(8, -0.01f, 0.0f);
    CHECK(p && p->direction == 2);
    p = axis(8, 0.0f, 0.5f);
    CHECK(p && p->direction == 2);                      // (no change from rest needed in sign: against 0)
    CHECK(axis(8, 0.5f, 0.5f) == nullptr);
    a.modes[3] = 8;
    a.axes[3] = 0.1f;
    a.pressed.axis[3] = 0.5f;
    CHECK(poll(0x80) == nullptr);
    CHECK(poll(2) == nullptr);
    a.axes[3] = 0.0f;
    a.modes[3] = 0;
    a.pressed.axis[3] = 0.0f;
    // The record reset first: after a joystick's, nothing left of it.
    c.m_press.direction = 7;
    c.m_press.unknown08 = 7;
    CHECK(poll() == nullptr && c.m_press.direction == 0 && c.m_press.device == 0 && c.m_press.key == 0 && c.m_press.unknown08 == 0);
    c.m_joys.clear();
}

void TestWide()
{
    char out[16];
    std::memset(out, 'x', sizeof out);
    WideToAnsi(out, L"Space", 3);
    CHECK(std::memcmp(out, "Spa\0", 4) == 0 && out[4] == 'x');
    WideToAnsi(out, L"Ok", 10);
    CHECK(std::strcmp(out, "Ok") == 0);
    WideToAnsi(nullptr, L"Ok", 10);
    std::memset(out, 'x', sizeof out);
    WideToAnsi(out, nullptr, 10);
    CHECK(out[0] == 'x');
}

void TestRenderCallback()
{
    g_in = S_Input{};
    auto *interfaces = reinterpret_cast<C_Interface *>(g_interfaceStorage);
    C_ScriptModule *module = &g_scriptModule;
    C_game *game = GetGame();
    game->m_endDelay = 1;
    *reinterpret_cast<void ***>(game) = g_gameVt;
    g_gameVt[0x20 / 4 + 1] = reinterpret_cast<void *>(F_GetState);
    game->m_interface = interfaces;
    game->m_quickMessages = reinterpret_cast<C_QuickMessages *>(g_interfaceStorage);
    g_log.clear();
    // Exiting: nothing.
    game->m_state = 0xE;
    CHECK(GameRenderCallback(1, 0, 0) == 0 && g_log.empty());
    game->m_state = 1;
    // Phase 1.
    interfaces->m_flags = INTERFACE_ACTIVE;
    CHECK(GameRenderCallback(1, 0, 0) == 0 && TakeLog() == "interface overlay\nvolumetrics\n");
    interfaces->m_flags = 0;
    GameRenderCallback(1, 0, 0);
    CHECK(TakeLog() == "volumetrics\n");
    interfaces->m_flags = INTERFACE_ACTIVE;
    module->m_fakeCutscene = true;
    GameRenderCallback(1, 0, 0);
    CHECK(TakeLog() == "volumetrics\n");
    // Phase 2.
    GameRenderCallback(2, 0, 0);
    CHECK(TakeLog() == "quick messages\nfade\nstats\n");
    module->m_fakeCutscene = false;
    CHECK(GameRenderCallback(2, 0, 0) == 0 && TakeLog() == "interface render\nquick messages\nfade\nstats\n");
    interfaces->m_flags = 0;
    GameRenderCallback(2, 0, 0);
    CHECK(TakeLog() == "quick messages\nfade\nstats\n");
    // A cutscene: the whole screen, its depth range kept.
    module->m_cutsceneRunning = true;
    interfaces->m_flags = INTERFACE_ACTIVE;
    GameRenderCallback(2, 0, 0);
    CHECK(TakeLog() == "viewport 0 0 640 480 0.5 0.75\nquick messages\nfade\nstats\n");
    module->m_cutsceneRunning = false;
    interfaces->m_flags = 0;
    // The fade: states 5 and 6, no end delay.
    game->m_endDelay = 0;
    game->m_state = 5;
    GameRenderCallback(2, 0, 0);
    CHECK(TakeLog() == "quick messages\napply fade\nfade\nstats\n");
    game->m_state = 6;
    GameRenderCallback(2, 0, 0);
    CHECK(TakeLog() == "quick messages\napply fade\nfade\nstats\n");
    game->m_state = 4;
    GameRenderCallback(2, 0, 0);
    CHECK(TakeLog() == "quick messages\nfade\nstats\n");
    game->m_state = 5;
    game->m_endDelay = 1;
    GameRenderCallback(2, 0, 0);
    CHECK(TakeLog() == "quick messages\nfade\nstats\n");
    // Another phase: nothing.
    CHECK(GameRenderCallback(3, 0, 0) == 0 && g_log.empty());
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_graph.vtable = g_graphVt;
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
    g_effects.vtable = g_effectsVt;
    g_effectsVt[0xA0 / 4 + 1] = reinterpret_cast<void *>(E_Fade);
    g_effectsVt[0xA4 / 4 + 1] = reinterpret_cast<void *>(E_Volumetrics);
    TestConstruct();
    TestKeys();
    TestMouse();
    TestJoystick();
    TestPoll();
    TestWide();
    TestRenderCallback();
    std::printf("controls: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
