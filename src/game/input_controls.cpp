// The controls (part 11c; original source 0x006c6d70..0x006c9580): each frame, the state of the keyboard, the mouse and the
// joysticks as a value for each action, held and pressed in this frame. The bindings come from C_KeyBindings; the render callback
// beside them is at the end. See game/pending.h.
#include <cmath>
#include <cstring>

#include "game/game.h"
#include "game/geffects.h"
#include "game/interface.h"
#include "game/pending.h"
#include "ls3d/igraph.h"
#include "script/script_module.h"

C_Controls g_controls;                                  // 0x008aeb58

namespace {

constexpr int KEY_DEVICE = 1, MOUSE_DEVICE = 2, JOY_DEVICE = 3;
constexpr int AXIS_DIRECTION_PLUS = 1, AXIS_DIRECTION_MINUS = 2;
constexpr int BIND_MOUSE_X = 0x100, BIND_MOUSE_Y = 0x200, BIND_MOUSE_WHEEL = 0x300;
constexpr int JOY_BUTTON_KEYS = 0x100, JOY_HAT_KEYS = 0x200;
constexpr int JOY_AXIS_MODE_ABSOLUTE = 4;               // (the third direction of an axis binding)
constexpr int JOY_AXES = 8, JOY_HATS = 4;
constexpr int MOUSE_MOVE_THRESHOLD = 10;
// PollPress's filter: what not to report.
enum : int {
    POLL_NO_MOUSE_AXES = 0x01,
    POLL_NO_JOY_AXES = 0x02,
    POLL_NO_MOUSE_BUTTONS = 0x04,
    POLL_NO_JOY_BUTTONS = 0x08,
    POLL_NO_JOY_HATS = 0x10,
    POLL_NO_KEYBOARD = 0x20,
};
constexpr uint32_t MODIFIER_LSHIFT = 0x20, MODIFIER_RSHIFT = 0x40, MODIFIER_LCTRL = 0x80, MODIFIER_RCTRL = 0x100,
                   MODIFIER_LALT = 0x200, MODIFIER_RALT = 0x400;

// The index of a mouse button's bit; -1 for other keys.
int MouseButtonIndex(int key)
{
    switch (key) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
        return 2;
    case 8:
        return 3;
    case 0x10:
        return 4;
    default:
        return -1;
    }
}

// A hat's angle (radians) as direction bits: 1 up, 2 right, 4 down, 8 left, with the corners between.
uint8_t HatDirections(float angle)
{
    if (angle <= 3.16f) {
        if (angle >= 3.12f)
            return 4;
        if (angle >= 1.5f)
            return angle <= 1.6f ? 2 : 6;
        return angle <= 0.1f ? 1 : 3;
    }
    if (angle <= 4.72f)
        return angle >= 4.69f ? 8 : 0xC;
    return 9;
}

float ClampedAxis(float value, float scale)
{
    if (scale < std::fabs(value))
        value = value <= 0.0f ? -scale : scale;
    return value / scale;
}

// `value` for this binding's direction: only its half; both for any other direction. Unchanged `held` outside it.
void ApplyAxis(float &held, int direction, float value)
{
    if (direction == AXIS_DIRECTION_PLUS) {
        if (value > 0.0f)
            held = value;
    } else if (direction == AXIS_DIRECTION_MINUS) {
        if (value < 0.0f)
            held = -value;
    } else {
        held = value;
    }
}

}  // namespace

C_Controls::C_Controls()
{
    m_scale[0] = 20.0f;
    m_scale[1] = 20.0f;
    m_scale[2] = 20.0f;
}

C_Controls::~C_Controls()
{
    Shutdown();
}

void C_Controls::Shutdown()                             // 0x006c6eb0
{
    m_bindings.clear();
    m_held.clear();
    m_pressed.clear();
    for (size_t i = m_joys.size(); i-- > 0;) {
        if (m_joys[i].joy)
            m_joys[i].joy->Release();
    }
    m_joys.clear();
}

bool C_Controls::SetMouseScale(int x, int y, int z)
{
    m_scale[0] = static_cast<float>(x);
    m_scale[1] = static_cast<float>(y);
    m_scale[2] = static_cast<float>(z);
    return true;
}

void C_Controls::SetInvertMouse(bool on)
{
    m_invertMouse = on;
}

void C_Controls::ClearDeviceKeys()
{
    std::memset(m_keys, 0, sizeof m_keys);
    std::memset(m_keyCounts, 0, sizeof m_keyCounts);
    std::memset(m_mouseCounts, 0, 4);
}

bool C_Controls::Attach(IGraph *graph, C_FrameListener **joys, unsigned count)
{
    if (!graph)
        return false;
    for (unsigned i = count; i-- > 0;) {
        if (!joys[i])
            return false;
    }
    ClearDeviceKeys();
    m_graph = graph;
    for (unsigned i = 0; i < count; ++i) {
        S_ControlsJoy joy = {};
        joy.joy = joys[i];
        joy.joy->AddRef();
        m_joys.push_back(joy);
    }
    m_keyArmed = ~static_cast<uint64_t>(0);
    SetMouseScale(400, 400, 1);
    m_invertMouse = false;
    return true;
}

void BindControls(C_Controls &controls, IGraph *graph, C_FrameListener **listeners, unsigned count)
{
    controls.Attach(graph, listeners, count);
}

void InitControls(C_Controls &controls)
{
    controls.m_bindings.clear();
    controls.m_held.clear();
    controls.m_pressed.clear();
    controls.ClearDeviceKeys();
}

void C_Controls::ClearAction(int action, float *held, float *pressed)
{
    float *const heldValues = held && pressed ? held : m_held.data();
    float *const pressedValues = held && pressed ? pressed : m_pressed.data();
    const S_Press &binding = m_bindings[static_cast<size_t>(action)];
    for (size_t i = m_bindings.size(); i-- > 0;) {
        if (m_bindings[i].device == binding.device && m_bindings[i].key == binding.key) {
            heldValues[i] = 0.0f;
            pressedValues[i] = 0.0f;
        }
    }
}

void C_Controls::ClearKey(int key, float *held, float *pressed)
{
    float *const heldValues = held && pressed ? held : m_held.data();
    float *const pressedValues = held && pressed ? pressed : m_pressed.data();
    for (size_t i = m_bindings.size(); i-- > 0;) {
        if (m_bindings[i].key == static_cast<uint32_t>(key)) {
            heldValues[i] = 0.0f;
            pressedValues[i] = 0.0f;
        }
    }
}

int C_Controls::GetValues(float **values, bool pressed)
{
    std::vector<float> &list = pressed ? m_pressed : m_held;
    if (list.empty())
        return -1;
    *values = list.data();
    return static_cast<int>(list.size());
}

float C_Controls::GetValue(int action, bool pressed)
{
    const std::vector<float> &list = pressed ? m_pressed : m_held;
    if (static_cast<uint32_t>(action) < list.size() && action >= 0)
        return list[static_cast<size_t>(action)];
    return 0.0f;
}

void C_Controls::Bind(int action, int16_t device, uint16_t key, int unknown, int direction)
{
    S_Press binding;
    binding.direction = direction;
    binding.device = static_cast<uint16_t>(device);
    binding.key = key;
    binding.unknown08 = unknown;
    SetBinding(&binding, action);
}

// (The record's end: the joystick's index is `unknown08`.)
bool C_Controls::SetBinding(const S_Press *binding, int action)   // 0x006c77d0
{
    if (action < 0)
        return false;
    const auto index = static_cast<size_t>(action);
    if (m_bindings.size() <= index)
        m_bindings.resize(index + 1, S_Press{});
    if (m_held.size() <= index)
        m_held.resize(index + 1, 0.0f);
    if (m_pressed.size() <= index)
        m_pressed.resize(index + 1, 0.0f);
    // What it was.
    const S_Press &old = m_bindings[index];
    if (old.device == KEY_DEVICE) {
        for (int i = 0; i < 0x40; ++i) {
            if (m_keys[i] == static_cast<char>(old.key)) {
                if (--m_keyCounts[i] == 0) {
                    m_keys[i] = 0;
                    m_graph->SetWatchedKeys(m_keys, 0x40);
                }
                break;
            }
        }
    }
    if (m_bindings[index].device == MOUSE_DEVICE) {
        const int button = MouseButtonIndex(m_bindings[index].key);
        if (button >= 0)
            --m_mouseCounts[button];
    }
    if (!binding) {
        m_bindings[index] = S_Press{};
        return true;
    }
    // What it is now.
    if (binding->device == KEY_DEVICE) {
        int found = -1;
        for (int i = 0; i < 0x40; ++i) {
            if (m_keys[i] == static_cast<char>(binding->key)) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            ++m_keyCounts[found];
        } else {
            int empty = 0;
            while (m_keys[empty] != 0) {
                if (++empty > 0x3F)
                    return false;
            }
            m_keys[empty] = static_cast<char>(binding->key);
            m_graph->SetWatchedKeys(m_keys, 0x40);
            ++m_keyCounts[empty];
        }
    } else if (binding->device == MOUSE_DEVICE) {
        const int button = MouseButtonIndex(binding->key);
        if (button >= 0)
            ++m_mouseCounts[button];
    } else if (binding->device == JOY_DEVICE) {
        if (m_joys.size() <= static_cast<uint32_t>(binding->unknown08))
            return false;
        const bool absolute = binding->direction == 3 && binding->key < 0xFF;
        m_joys[static_cast<size_t>(binding->unknown08)].joy->SetAxisMode(binding->key, absolute ? JOY_AXIS_MODE_ABSOLUTE : 0);
    }
    m_bindings[index] = *binding;
    return true;
}

void C_Controls::Update(bool processAll)
{
    for (size_t i = m_joys.size(); i-- > 0;) {
        S_ControlsJoy &joy = m_joys[i];
        if (processAll)
            joy.joy->OnFrame(1);
        joy.joy->GetState(&joy.state);
        joy.joy->GetStatePressed(&joy.pressed);
    }
    if (processAll) {
        m_graph->ProcessWinMessages();
        m_graph->BeginFrame();
        m_graph->ReadKeyboard();
    }
    const uint64_t keys = m_graph->GetWatchedKeyBits();
    const uint32_t buttons = m_graph->GetMouseButtons();
    float moveX = static_cast<float>(m_graph->GetMouseDeltaX());
    float moveY = static_cast<float>(m_graph->GetMouseDeltaY());
    float wheel = static_cast<float>(m_graph->GetMouseWheel());
    // A key's "pressed" goes to each of its bindings once, until it has been released.
    char mouseCounts[6];
    std::memcpy(mouseCounts, m_mouseCounts, sizeof mouseCounts);
    char keyCounts[0x40];
    std::memcpy(keyCounts, m_keyCounts, sizeof keyCounts);
    m_mouseArmed = ~(m_mouseArmed & buttons) ^ buttons;
    m_keyArmed = ~(m_keyArmed & keys) ^ keys;
    moveX = ClampedAxis(moveX, m_scale[0]);
    moveY = ClampedAxis(moveY, m_scale[1]);
    wheel = ClampedAxis(wheel, m_scale[2]);

    for (size_t i = m_bindings.size(); i-- > 0;) {
        const S_Press &binding = m_bindings[i];
        float &held = m_held[i];
        float &pressed = m_pressed[i];
        if (binding.device == KEY_DEVICE) {
            int slot = 0;
            while (slot < 0x40 && m_keys[slot] != static_cast<char>(binding.key))
                ++slot;
            const uint64_t bit = slot < 64 ? static_cast<uint64_t>(1) << slot : 0;
            if (!(bit & keys)) {
                held = 0.0f;
                pressed = 0.0f;
            } else {
                held = 1.0f;
                if (m_keyArmed & bit) {
                    if (--keyCounts[slot] == 0)
                        m_keyArmed &= ~bit;
                    pressed = 1.0f;
                } else {
                    pressed = 0.0f;
                }
            }
        } else if (binding.device == MOUSE_DEVICE) {
            held = 0.0f;
            const int button = MouseButtonIndex(binding.key);
            if (button >= 0) {
                const uint32_t bit = 1u << button;
                if (!(bit & buttons)) {
                    pressed = 0.0f;
                } else {
                    held = 1.0f;
                    if (m_mouseArmed & bit) {
                        if (--mouseCounts[button] == 0)
                            m_mouseArmed &= ~bit;
                        pressed = 1.0f;
                    } else {
                        pressed = 0.0f;
                    }
                }
            } else if (binding.key == BIND_MOUSE_X || binding.key == BIND_MOUSE_Y || binding.key == BIND_MOUSE_WHEEL) {
                ApplyAxis(held, binding.direction, binding.key == BIND_MOUSE_X ? moveX : binding.key == BIND_MOUSE_Y ? moveY : wheel);
                pressed = held;
            }
        } else if (binding.device == JOY_DEVICE) {
            held = 0.0f;
            pressed = 0.0f;
            if (m_joys.size() <= static_cast<uint32_t>(binding.unknown08))
                continue;
            const S_ControlsJoy &joy = m_joys[static_cast<size_t>(binding.unknown08)];
            const int key = binding.key;
            if (key >= JOY_HAT_KEYS) {
                const int hat = (key >> 4) & 0xF;
                if (!joy.state.povCentered[hat] && (static_cast<uint8_t>(key) & HatDirections(joy.state.pov[hat])))
                    held = 1.0f;
                if (!joy.pressed.povCentered[hat] && (HatDirections(joy.pressed.pov[hat]) & key & 0xF))
                    pressed = 1.0f;
            } else if (key >= JOY_BUTTON_KEYS) {
                const uint32_t bit = 1u << (key & 0x1F);
                if (joy.state.buttons[0] & bit)
                    held = 1.0f;
                if (joy.pressed.buttons[0] & bit)
                    pressed = 1.0f;
            } else {
                if (key < JOY_AXES)
                    ApplyAxis(held, binding.direction, joy.state.axis[key]);
                pressed = held;
            }
        }
    }
}

const C_Controls::S_Press *C_Controls::PollPress(int filter, bool reread)
{
    m_press = S_Press{};
    for (size_t i = m_joys.size(); i-- > 0;) {
        if (reread)
            m_joys[i].joy->OnFrame(1);
        m_joys[i].joy->GetState(&m_joys[i].state);
    }
    if (reread) {
        m_graph->ProcessWinMessages();
        m_graph->BeginFrame();
        m_graph->ReadKeyboard();
    }
    const uint32_t modifiers = m_graph->GetModifiers(0);
    const auto typed = static_cast<uint8_t>(m_graph->GetAsciiKey());
    const uint32_t buttons = m_graph->GetMouseButtons();
    const int moveX = m_graph->GetMouseDeltaX();
    const int moveY = m_graph->GetMouseDeltaY();
    const int wheel = m_graph->GetMouseWheel();

    const auto result = [this](int device, int key, int direction = 0, int joy = 0) {
        m_press.device = static_cast<uint16_t>(device);
        m_press.key = static_cast<uint16_t>(key);
        m_press.direction = direction;
        m_press.unknown08 = joy;
        return &m_press;
    };
    if (!(filter & POLL_NO_KEYBOARD) && (modifiers != 0 || typed != 0)) {
        if (modifiers == 0)
            return result(KEY_DEVICE, typed);
        if (modifiers & MODIFIER_LSHIFT)
            return result(KEY_DEVICE, 0x2A);
        if (modifiers & MODIFIER_RSHIFT)
            return result(KEY_DEVICE, 0x36);
        if (modifiers & MODIFIER_LCTRL)
            return result(KEY_DEVICE, 0x1D);
        if (modifiers & MODIFIER_RCTRL)
            return result(KEY_DEVICE, 0x9D);
        if (modifiers & MODIFIER_LALT)
            return result(KEY_DEVICE, 0x38);
        if (modifiers & MODIFIER_RALT)
            return result(KEY_DEVICE, 0xB8);
        return result(KEY_DEVICE, 0);
    }
    if (!(filter & POLL_NO_MOUSE_BUTTONS)) {
        for (int bit = 1; bit <= 0x10; bit <<= 1) {
            if (buttons & static_cast<uint32_t>(bit))
                return result(MOUSE_DEVICE, bit);
        }
    }
    if (!(filter & POLL_NO_MOUSE_AXES)) {
        if (moveX > MOUSE_MOVE_THRESHOLD)
            return result(MOUSE_DEVICE, BIND_MOUSE_X, AXIS_DIRECTION_PLUS);
        if (moveX < -MOUSE_MOVE_THRESHOLD)
            return result(MOUSE_DEVICE, BIND_MOUSE_X, AXIS_DIRECTION_MINUS);
        if (moveY > MOUSE_MOVE_THRESHOLD)
            return result(MOUSE_DEVICE, BIND_MOUSE_Y, AXIS_DIRECTION_PLUS);
        if (moveY < -MOUSE_MOVE_THRESHOLD)
            return result(MOUSE_DEVICE, BIND_MOUSE_Y, AXIS_DIRECTION_MINUS);
        if (wheel > MOUSE_MOVE_THRESHOLD)
            return result(MOUSE_DEVICE, BIND_MOUSE_WHEEL, AXIS_DIRECTION_PLUS);
        if (wheel < -MOUSE_MOVE_THRESHOLD)
            return result(MOUSE_DEVICE, BIND_MOUSE_WHEEL, AXIS_DIRECTION_MINUS);
    }
    for (int index = static_cast<int>(m_joys.size()) - 1; index >= 0; --index) {
        const S_ControlsJoy &joy = m_joys[static_cast<size_t>(index)];
        if (!(filter & POLL_NO_JOY_BUTTONS)) {
            for (int bit = 0; bit < 32; ++bit) {
                if (joy.state.buttons[0] & (1u << bit))
                    return result(JOY_DEVICE, JOY_BUTTON_KEYS + bit, 0, index);
            }
        }
        if (!(filter & POLL_NO_JOY_HATS)) {
            for (int hat = 0; hat < JOY_HATS; ++hat) {
                if (joy.state.povCentered[hat])
                    continue;
                const float angle = joy.state.pov[hat];
                const int direction = angle > 4.69f ? 8 : angle > 3.12f ? 4 : angle > 1.5f ? 2 : 1;
                return result(JOY_DEVICE, (hat + 0x20) * 0x10 + direction, 0, index);
            }
        }
        if (!(filter & POLL_NO_JOY_AXES)) {
            for (int axis = 0; axis < JOY_AXES; ++axis) {
                const float value = joy.joy->GetAxis(axis);
                const float rest = joy.pressed.axis[axis];
                const uint32_t mode = joy.joy->GetAxisMode(axis);
                bool moved;
                bool negative;
                if (mode & 8) {
                    moved = !(filter & 0x80) && value != rest;
                    negative = value <= 0.0f;
                } else if (mode & 1) {
                    moved = std::fabs(value) > ((mode & 4) ? 0.3 : 0.5);
                    negative = value <= 0.0f;
                } else {
                    moved = std::fabs(value - rest) > ((mode & 4) ? 0.5 : 0.7);
                    negative = value <= rest;
                }
                if (moved)
                    return result(JOY_DEVICE, axis, negative ? AXIS_DIRECTION_MINUS : AXIS_DIRECTION_PLUS, index);
            }
        }
    }
    return nullptr;
}

void WideToAnsi(char *out, const wchar_t *text, int maxLength)
{
    if (!out || !text)
        return;
    int size = static_cast<int>(wcslen(text));
    if (maxLength < size)
        size = maxLength;
    WideCharToMultiByte(CP_ACP, 0, text, -1, out, size, nullptr, nullptr);
    out[size] = '\0';
}

// ---- the scene's render callback ---------------------------------------------------------------------------------------

int __stdcall GameRenderCallback(int phase, int, int)
{
    C_game *game = GetGame();
    if (game && game->GetState() == GAME_STATE_GAME_MENU)
        return 0;
    C_Interface *const interfaces = game->GetInterface();
    if (phase == 1) {
        if (!g_scriptModule.IsFakeCutscene() && interfaces->IsActive())
            interfaces->RenderOverlay();
        GetGEffects()->RenderVolumetrics();
        // (Then a debug drawing, 0x0079bbc0, behind the flag 0x0098af98, which nothing sets.)
    } else if (phase == 2) {
        if (g_scriptModule.IsCutsceneRunning()) {
            IGraph *graph = GetIGraph();
            S_IGraphViewport viewport = *graph->GetViewport();
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = graph->ScrnX();
            viewport.height = graph->ScrnY();
            graph->SetViewport(viewport);
        } else if (!g_scriptModule.IsFakeCutscene() && interfaces->IsActive()) {
            interfaces->Render();
        }
        game->GetQuickMessages()->Render();
        if (game->GetEndDelay() == 0 && (game->GetState() == 5 || game->GetState() == 6))
            game->ApplyFade();
        GetGEffects()->RenderFade();
        RenderDebugStats();
    }
    return 0;
}
