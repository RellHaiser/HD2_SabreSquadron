// Human part 60: the hot key and the titled edit box (game/controls.cpp lines 4902-5159) against the original's. MinGW;
// build_hotkey.bat; runs hotkey_diff.exe ("hotkey_diff: N checks, 0 failures"). Planned trees of hot keys and titled edit
// boxes, each with its edit box (+0x78, id | 1) and title (+0x74, id | 2) owned (not children: the kit's two owned
// controls); the fields +0x6C (armed) and +0x70 (the binding). What the hot key reaches outside the controls faked on both
// sides with the same loggers: the input's PollPress (0x006c87f0 on 0x008aeb58: a round's press or none), the key
// bindings' Bind (0x006a8670) and GetBindingName (0x006ab840) on the player profile's (g_gameProfile +0x20, the
// bindings at +0xEC: one block both sides use), the cursor's SetDelay (0x0066ab50). The kit's cases; the own messages
// straight to them; the input (the slots, Unknown20, the tick, SetBinding +0x84); the constructors' fields.
// HD2_HOTKEY_ROUNDS: rounds per kind of call (default 20,000; HD2_HOTKEY_QUICK: 4,000); HD2_HOTKEY_CASE: one only;
// HD2_HOTKEY_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/control.h"
#include "game/controls.h"
#include "game/cursor.h"
#include "game/font.h"
#include "game/game_texts.h"
#include "game/item_defs.h"
#include "game/key_bindings.h"
#include "game/paged_text_box.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

#define CONTROLS_KIT_HAS_HOT_KEY  // PollPress, Bind, GetBindingName, SetDelay: this part's fakes
#include "controls_kit.inc"

namespace {

// ---- the fakes outside the controls (a round's answers, both sides) ----
// The press PollPress answers (the port's S_Press layout the original's: direction, device, key, the joystick).
C_Controls::S_Press g_press;
bool g_pollNone;
bool g_bindResult;
const char *g_bindingName;
// The player profile both sides use (the port's g_gameProfile.m_player, the original's 0x008aea2c): its bindings at +0xEC
// (the port's by its own member, g++'s place).
alignas(16) uint8_t g_playerBlock[0x2000];
const void *PortBindings() { return reinterpret_cast<C_PlayerProfile *>(g_playerBlock)->GetKeyBindings(); }
const char *BindingsName(const void *self, bool port)
{
    return self == (port ? PortBindings() : static_cast<const void *>(g_playerBlock + 0xEC)) ? "bindings" : "bindings?";
}
const C_Controls::S_Press *PollAnswer() { return g_pollNone ? nullptr : &g_press; }
C_str BindingName(int index)
{
    if (!g_bindingName)
        return C_str();
    char text[96];
    std::snprintf(text, sizeof text, "%s%d", g_bindingName, index & 0xFF);
    return C_str(text);
}

// The original's.
const void *__attribute__((thiscall)) O_PollPress(void *self, int filter, int reread)
{
    Log("%s PollPress %x %d", self == reinterpret_cast<void *>(0x008aeb58) ? "controls" : "controls?", filter, reread & 0xFF);
    return PollAnswer();
}
bool __attribute__((thiscall)) O_Bind(void *self, int index, int device, int key, int direction)
{
    Log("%s Bind %d %d %d %d", BindingsName(self, false), index, device, key, direction);
    return g_bindResult;
}
void *__attribute__((thiscall)) O_GetBindingName(void *self, void *out, int index)
{
    Log("%s GetBindingName %d", BindingsName(self, false), index);
    new (out) C_str(BindingName(index));
    return out;
}
void __attribute__((thiscall)) O_SetDelay(void *self, int time) { Log("%s SetDelay %d", self == g_arena.cursor ? "cursor" : "cursor?", time); }

}  // namespace

// The port's.
const C_Controls::S_Press *C_Controls::PollPress(int filter, bool reread)
{
    Log("%s PollPress %x %d", this == &g_controls ? "controls" : "controls?", filter, int(reread));
    return PollAnswer();
}
bool C_KeyBindings::Bind(int index, int device, int key, int direction)
{
    Log("%s Bind %d %d %d %d", BindingsName(this, true), index, device, key, direction);
    return g_bindResult;
}
C_str C_KeyBindings::GetBindingName(int index) const
{
    Log("%s GetBindingName %d", BindingsName(this, true), index);
    return BindingName(index);
}
void C_Cursor::SetDelay(int time) { Log("%s SetDelay %d", static_cast<void *>(this) == g_arena.cursor ? "cursor" : "cursor?", time); }

namespace {
#include "controls_kinds.inc"
#include "controls_tree.inc"

uint8_t *OrigOf(const C_Control *c) { return c ? ToOrigNode(c) : nullptr; }

// ---- the kinds ----
// The hot key (and the titled edit box): +0x6C armed (the first editing tick passed), +0x70 the binding, +0x74 the title,
// +0x78 the edit box (both owned).
void RandomHotKey(C_Control *c)
{
    auto *h = static_cast<C_HotKeyControl *>(c);
    h->m_armed = P(0.5);
    h->m_binding = P(0.8) ? I(-1, 70) : int(U32());
}
void HotKeyToOrig(C_Control *c, uint8_t *o)
{
    auto *h = static_cast<C_HotKeyControl *>(c);
    OF<bool>(o, 0x6C) = h->m_armed;
    OF<int>(o, 0x70) = h->m_binding;
    OF<uint8_t *>(o, 0x74) = OrigOf(h->m_title);
    OF<uint8_t *>(o, 0x78) = OrigOf(h->m_edit);
}
std::string HotKeyDiffers(C_Control *c, const uint8_t *o)
{
    auto *h = static_cast<C_HotKeyControl *>(c);
    char text[160];
    if (OF<bool>(o, 0x6C) != h->m_armed || OF<int>(o, 0x70) != h->m_binding || PtrName(OF<void *>(o, 0x74)) != PtrName(h->m_title) ||
        PtrName(OF<void *>(o, 0x78)) != PtrName(h->m_edit)) {
        std::snprintf(text, sizeof text, "hot key armed %d binding %d title %s edit %s / armed %d binding %d title %s edit %s", OF<bool>(o, 0x6C),
                      OF<int>(o, 0x70), PtrName(OF<void *>(o, 0x74)).c_str(), PtrName(OF<void *>(o, 0x78)).c_str(), h->m_armed, h->m_binding,
                      PtrName(h->m_title).c_str(), PtrName(h->m_edit).c_str());
        return text;
    }
    return "";
}
C_Control **HotKeyTitle(C_Control *c) { return reinterpret_cast<C_Control **>(&static_cast<C_HotKeyControl *>(c)->m_title); }
C_Control **HotKeyEdit(C_Control *c) { return reinterpret_cast<C_Control **>(&static_cast<C_HotKeyControl *>(c)->m_edit); }

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"title", 0x00813cc0, 0x70, Make<C_TitleControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"editbox", 0x00813d48, 0x9C, Make<C_EditBox>, RandomEditBox, EditBoxToOrig, EditBoxDiffers, O_BOX, nullptr, true},
    {"hotkey", 0x00814fe0, 0x7C, Make<C_HotKeyControl>, RandomHotKey, HotKeyToOrig, HotKeyDiffers, 0, nullptr, false, 0x74, HotKeyTitle, 0,
     nullptr, 0x78, HotKeyEdit},
    {"titled", 0x00814f58, 0x7C, Make<C_TitledEditBox>, RandomHotKey, HotKeyToOrig, HotKeyDiffers, 0, nullptr, false, 0x74, HotKeyTitle, 0,
     nullptr, 0x78, HotKeyEdit},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_TITLE = 1, K_EDITBOX = 2, K_HOTKEY = 3, K_TITLED = 4;

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
int PlanAdd(int parent, int kind, int bits, bool child = true)
{
    g_plan.push_back({parent, kind, bits, child});
    return int(g_plan.size()) - 1;
}
// A hot key or a titled edit box with its edit box (id | 1) and title (id | 2), owned: always there (the original's
// handlers use both unchecked).
void PlanHotKey(int parent)
{
    const int at = PlanAdd(parent, P(0.5) ? K_HOTKEY : K_TITLED, -1);
    PlanAdd(at, K_EDITBOX, 1, false);
    PlanAdd(at, K_TITLE, 2, false);
}
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    if (P(0.4)) {
        PlanHotKey(-1);
    } else {
        const int root = PlanAdd(-1, K_CONTROL, -1);
        for (int k = I(1, 3); k > 0; --k)
            PlanHotKey(root);
        if (P(0.3))
            PlanAdd(root, P(0.5) ? K_TITLE : K_EDITBOX, -1);
    }
    return g_plan;
}
std::vector<size_t> NodesOf(std::initializer_list<int> kinds)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        for (int k : kinds)
            if (g_nodeKinds[i] == &KINDS[k])
                out.push_back(i);
    return out;
}
// (The owned parts wired; the cursor made: the original's tick sets its delay unchecked, a flaw the port fixes.)
void AfterTree()
{
    g_cursorMade = true;
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        C_Control *c = g_portNodes[i];
        auto *h = c->m_parent ? dynamic_cast<C_HotKeyControl *>(c->m_parent) : nullptr;
        if (!h)
            continue;
        if (g_nodeKinds[i] == &KINDS[K_EDITBOX])
            h->m_edit = static_cast<C_EditBox *>(c);
        else if (g_nodeKinds[i] == &KINDS[K_TITLE])
            h->m_title = static_cast<C_TitleControl *>(c);
    }
}
// Create's frames: the title's ("hktext*" for the hot key, "text*" for the titled box: found mostly), the edit box's
// (on the same frame).
void CreateFrames(size_t node, int fi)
{
    if (fi < 0)
        return;
    const S_Kind *k = g_nodeKinds[node];
    if (k == &KINDS[K_HOTKEY])
        g_arena.child[fi][ChildName("hktext*")] = static_cast<int16_t>(P(0.8) ? I(0, 15) : -1);
    if (k == &KINDS[K_TITLED])
        g_arena.child[fi][ChildName("text*")] = static_cast<int16_t>(P(0.8) ? I(0, 15) : -1);
    if ((k == &KINDS[K_HOTKEY] || k == &KINDS[K_TITLED] || k == &KINDS[K_EDITBOX]) && P(0.85))
        for (const char *name : {"normal*", "actived*"})
            if (g_arena.child[fi][ChildName(name)] < 0)
                g_arena.child[fi][ChildName(name)] = static_cast<int16_t>(I(0, 15));
}

// ---- the messages' values ----
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_CALLBACK:
        // (A function always: the original calls a callback without checking it.)
        m.value = RandomNotification();
        m.value2 = RandomObject();
        m.value3 = g_portCbs[I(0, 2)];
        break;
    case CONTROL_MSG_SET_FORBIDDEN:
        m.value2 = P(0.3) ? 0 : reinterpret_cast<uintptr_t>(Pick(FORBIDDEN));
        break;
    case CONTROL_MSG_SET_MAX_LENGTH:
        m.value = uint32_t(P(0.8) ? I(0, 40) : 30);
        break;
    case CONTROL_MSG_SET_RANGE:
        m.value = uint32_t(I(-50, 50));
        m.value2 = P(0.2) ? m.value : uint32_t(int(m.value) + I(0, 100));
        break;
    case CONTROL_MSG_SET_NUMBER:
        m.value = uint32_t(I(-1, 8));
        m.value2 = uint32_t(I(-100, 1000));
        break;
    case CONTROL_MSG_SET_BINDING:
        m.value = uint32_t(P(0.8) ? I(-1, 70) : int(U32()));
        m.value2 = uint32_t(P(0.7) ? I(0x7D0, 0x7FF) : int(U32()));
        break;
    case CONTROL_MSG_SET_TEXT:
    case CONTROL_MSG_ADD_TEXT:
        m.text = Pick({"one", "", "Two words"});
        break;
    case CONTROL_MSG_SET_VALUE:
        m.value = P(0.3) ? 0 : RandomId();
        break;
    }
}

// ---- the round ----
void PickRound()
{
    SetUnicode(P(0.4));
    g_box.editRun = P(0.8);
    g_box.hoverAt = P(0.7);
    g_box.insert = P(0.8);
    g_box.hovered = int16_t(I(-1, 6));
    g_box.selection = int16_t(I(-1, 6));
    g_box.selectRun = int16_t(I(-1, 6));
    g_box.scrollRange = int16_t(P(0.4) ? 0 : I(1, 20));
    g_box.centreX = F(-1, 1);
    g_box.centreY = P(0.3) ? 0.0f : F(-1, 1);
    g_box.selected = Pick({"sel", "", "12", "abc"});
    // The press: Escape on the keyboard now and then (and its neighbours: another key, another device), else any.
    g_pollNone = P(0.3);
    g_press.direction = P(0.7) ? I(0, 2) : int(U32());
    if (P(0.4)) {
        g_press.device = uint16_t(P(0.7) ? 1 : Pick({0, 2, 3, 0x101}));
        g_press.key = uint16_t(P(0.7) ? 1 : Pick({0, 2, 0x101, 0x1C}));
    } else {
        g_press.device = uint16_t(P(0.8) ? I(0, 4) : int(U32()));
        g_press.key = uint16_t(P(0.8) ? I(0, 0x100) : int(U32()));
    }
    g_press.unknown08 = int(U32());
    g_bindResult = P(0.6);
    // (No '%': the edit box shows its buffer through C_fstr in the original, part 52's flaw.)
    g_bindingName = P(0.05) ? nullptr : Pick({"Key ", "", "Left Shift ", "Joystick button with a long long name ", "\xC3\xA9 "});
    std::memset(g_playerBlock, 0, sizeof g_playerBlock);
    g_gameProfile.m_player = reinterpret_cast<C_PlayerProfile *>(g_playerBlock);
    *OriginalAt<void **>(0x008aea2c) = g_playerBlock;
}

// ---- the tests ----
const uint32_t HOT_KEY_CODES[] = {CONTROL_MSG_GET_BINDING, CONTROL_MSG_SET_BINDING, CONTROL_MSG_GET_BINDING2, CONTROL_MSG_REFRESH,
                                  CONTROL_MSG_SET_BINDING, CONTROL_MSG_SET_BINDING, CONTROL_MSG_REFRESH,      CONTROL_MSG_GET_BINDING,
                                  CONTROL_MSG_SET_TEXT,    CONTROL_MSG_GET_VALUE,   CONTROL_MSG_SET_VALUE,    0x1000000,
                                  CONTROL_MSG_GET_NUMBER,  0x2000001};
const uint32_t TITLED_CODES[] = {CONTROL_MSG_SET_TEXT,      CONTROL_MSG_SET_TEXT_ID,     CONTROL_MSG_ADD_CALLBACK, CONTROL_MSG_SET_MAX_LENGTH,
                                 CONTROL_MSG_SET_DIGITS_ONLY, CONTROL_MSG_SET_NUMBER,    CONTROL_MSG_GET_NUMBER,   CONTROL_MSG_SET_FORBIDDEN,
                                 CONTROL_MSG_SET_RANGE,     CONTROL_MSG_GET_TEXT,        CONTROL_MSG_REFRESH,      CONTROL_MSG_GET_BINDING,
                                 CONTROL_MSG_SET_BINDING,   CONTROL_MSG_SET_VALUE,       CONTROL_MSG_ADD_TEXT,     0x1000000,
                                 CONTROL_MSG_EDIT,          CONTROL_MSG_END_EDIT,        0x2000001};
size_t PickOf(std::initializer_list<int> kinds)
{
    const std::vector<size_t> nodes = NodesOf(kinds);
    return nodes.empty() ? SIZE_MAX : nodes[size_t(I(0, int(nodes.size()) - 1))];
}
uint32_t OwnTarget(size_t i)
{
    if (P(0.4))
        return 0xFFFFFFFF;
    if (P(0.4))
        return g_portNodes[i]->m_id;
    return RandomId();
}
// The hot keys' and titled boxes' own messages sent straight to them.
void TestOwn()
{
    const size_t i = PickOf({K_HOTKEY, K_TITLED});
    if (i == SIZE_MAX)
        return;
    const bool titled = g_nodeKinds[i] == &KINDS[K_TITLED];
    S_ControlMessage m;
    m.target = OwnTarget(i);
    m.id = titled ? Pick(TITLED_CODES) : Pick(HOT_KEY_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    if ((m.id & CONTROL_NOTIFY_MASK) == 0 && (m.id & CONTROL_MSG_MASK) == CONTROL_MSG_NOTIFY)
        m.value = Pick(NOTIFY_RAISED);
    g_messageBefore = m;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    const bool copy = P(0.2);
    Diff([&] { rp = copy ? g_portNodes[i]->HandleMessageCopy(m) : g_portNodes[i]->HandleMessage(&m); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, copy ? 0x74 : 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "message %x (%s): %d / %d", g_messageBefore.id, g_nodeKinds[i]->name, ro, rp);
    if (!copy) {
        const std::string why = MessageDiffers(m, mo);
        CHECK(why.empty(), "the message after (%s): %s", g_nodeKinds[i]->name, why.c_str());
    }
}

// Their input: the slots (the edit box editing or not, the hot key armed or not), Unknown20's keys, the tick, the
// refresh, SetInactive, SetBinding (+0x84).
void TestInput()
{
    const size_t i = PickOf({K_HOTKEY, K_TITLED});
    if (i == SIZE_MAX)
        return;
    auto *h = static_cast<C_HotKeyControl *>(g_portNodes[i]);
    const int pick = I(0, 12);
    if (P(0.6))
        h->m_edit->m_editing = pick == 8 ? P(0.85) : P(0.5);
    const int id = int(P(0.6) ? h->m_id | uint32_t(I(0, 2)) : RandomId());
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter}, {0x34, &C_Control::OnLeave}, {0x38, &C_Control::OnClick},    {0x38, &C_Control::OnClick},
        {0x3C, &C_Control::OnInput02}, {0x48, &C_Control::OnPress}, {0x5C, &C_Control::OnKeyClick},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    int rp = 0, ro = 0;
    if (pick < n) {
        Diff([&] { rp = (h->*slots[pick].port)(id); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], id); });
    } else if (pick == 7) {
        const int key = Pick({1, 1, 0x101, 0x1C, 0, 0x81, 2}) | (P(0.1) ? int(U32() & 0xFFFFFF00u) : 0);
        Diff([&] { rp = h->Unknown20(key); }, [&] { ro = Slot<BoolIntFn>(i, 0x20)(g_origNodes[i], key); });
    } else if (pick == 8 || pick == 9) {
        const int time = I(0, 100);
        Diff([&] { h->Tick(time); }, [&] { Slot<VoidIntFn>(i, 0x1C)(g_origNodes[i], time); });
    } else if (pick == 10) {
        Diff([&] { h->Refresh(); }, [&] { Slot<VoidFn>(i, 0x18)(g_origNodes[i]); });
    } else if (pick == 11) {
        const bool on = P(0.5);
        Diff([&] { h->SetInactive(on); }, [&] { Slot<VoidIntFn>(i, 0x7C)(g_origNodes[i], on); });
    } else {
        const int binding = P(0.8) ? I(-1, 70) : int(U32());
        Diff([&] { h->SetBinding(binding); }, [&] { Slot<VoidIntFn>(i, 0x84)(g_origNodes[i], binding); });
    }
    CHECK(rp == ro, "input %d (%s) %x: %d / %d", pick, g_nodeKinds[i]->name, unsigned(id), ro, rp);
}

// The constructors: inline in the original's factory (0x0065d470: type 0xd and 0xe, +0x6C 0, +0x74 0, +0x78 0; +0x70 not
// set: the original's binding whatever its block held, the port's 0).
void TestConstruct()
{
    auto *scene = reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000));
    const uint32_t id = U32();
    C_HotKeyControl hot(scene, id, nullptr);
    CHECK(!hot.m_armed && hot.m_binding == 0 && !hot.m_title && !hot.m_edit && hot.m_id == id && hot.m_scene == scene,
          "the hot key's fields at construction");
    C_TitledEditBox titled(scene, id, nullptr);
    CHECK(!titled.m_armed && titled.m_binding == 0 && !titled.m_title && !titled.m_edit && titled.m_id == id, "the titled box's fields");
    // (Neither owns anything yet: their destructors delete nothing.)
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_arenaNames = CHILD_NAME_COUNT;
    g_treePlan = TreePlan;
    g_codes = {CONTROL_MSG_DEACTIVATE,   CONTROL_MSG_ACTIVATE,      CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT,
               CONTROL_MSG_SET_INDEX,    CONTROL_MSG_GET_INDEX,     CONTROL_MSG_SET_VALUE,    CONTROL_MSG_GET_VALUE,
               CONTROL_MSG_REFRESH,      CONTROL_MSG_SET_TEXT,      CONTROL_MSG_SET_TEXT_ID,  CONTROL_MSG_ADD_TEXT,
               CONTROL_MSG_SET_FORBIDDEN, CONTROL_MSG_SET_MAX_LENGTH, CONTROL_MSG_SET_DIGITS_ONLY, CONTROL_MSG_GET_TEXT,
               CONTROL_MSG_SET_RANGE,    CONTROL_MSG_SET_NUMBER,    CONTROL_MSG_GET_NUMBER,   CONTROL_MSG_ADD_CALLBACK,
               CONTROL_MSG_GET_BINDING,  CONTROL_MSG_SET_BINDING,   CONTROL_MSG_GET_BINDING2, CONTROL_MSG_EDIT,
               0x1000000,                0x2000001,                 0};
    // (Ending the editing notifies 02: loud, and no loud route on 02.)
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_END_EDIT};
    NOTIFY_RAISING = {CONTROL_NOTIFY_05, CONTROL_NOTIFY_0B, CONTROL_NOTIFY_CLICK};
    g_tickKinds = {};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    g_afterTree = AfterTree;
    // (HOT_KEY_DONE built on the original's stack: its value2 and value3 unset.)
    g_stackValue2Codes = {CONTROL_MSG_HOT_KEY_DONE};
    g_stackValue3Codes = {CONTROL_MSG_HOT_KEY_DONE};
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
    RedirectOriginal(0x006c87f0, reinterpret_cast<const void *>(O_PollPress));
    RedirectOriginal(0x006a8670, reinterpret_cast<const void *>(O_Bind));
    RedirectOriginal(0x006ab840, reinterpret_cast<const void *>(O_GetBindingName));
    RedirectOriginal(0x0066ab50, reinterpret_cast<const void *>(O_SetDelay));
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_HOTKEY_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("hotkey_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_HOTKEY_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_HOTKEY_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_HOTKEY_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Input", TestInput},   {"Construct", TestConstruct},
    };
    for (const S_Case &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round) {
            PickRound();
            RandomTree();
            c.run();
        }
    }
    std::printf("hotkey_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
