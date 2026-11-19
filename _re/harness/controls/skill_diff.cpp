// Human part 54: the skill (game/controls.cpp lines 3363-3622) against the original's. MinGW; build_skill.bat; runs
// skill_diff.exe ("skill_diff: N checks, 0 failures"). The kit (controls_kit.inc, controls_tree.inc, controls_kinds.inc)
// fakes what the controls reach outside themselves on both sides and compares the trees, the message queue, the engine's
// fakes and the calls on them. The trees are planned: skills with their titles and buttons (as Create makes them, now and
// then a button missing, a child added or two out of order), each skill's pool (its value: the control it asks for a
// point) none, another skill or a control that is not one.
// HD2_SKILL_ROUNDS: rounds per kind of call (default 20,000; HD2_SKILL_QUICK: 4,000); HD2_SKILL_CASE: one only;
// HD2_SKILL_FIRST_CHANCE: faults under the original's exception frames shown.
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

#include "controls_kit.inc"

namespace {
#include "controls_kinds.inc"
#include "controls_tree.inc"

// ---- this part's kind ----
uint8_t *OrigOf(const C_Control *c) { return c ? ToOrigNode(c) : nullptr; }
// The node's n-th child of type T (null: none).
template <class T> T *ChildOf(C_Control *c, int n = 0)
{
    for (C_Control *k : c->m_children)
        if (auto *t = dynamic_cast<T *>(k))
            if (n-- == 0)
                return t;
    return nullptr;
}

// The skill: +0x6C the name's title, +0x70 the value's, +0x74 the plus button, +0x78 the minus, +0x7C the name, +0x80 the
// points, +0x84 the base, +0x88 the focus. (Its titles always: the original uses them unchecked; its buttons now and then
// none.)
const char *const NAMES[] = {"Shooting", "", "Stamina", "x y"};
int RandomCount() { return P(0.2) ? Pick({0, 1, 99, 100, 101, -1}) : I(-20, 120); }
void RandomSkill(C_Control *c)
{
    auto *s = static_cast<C_SkillControl *>(c);
    s->m_nameTitle = ChildOf<C_TitleControl>(c, 0);
    s->m_valueTitle = ChildOf<C_TitleControl>(c, 1);
    s->m_minusButton = P(0.05) ? nullptr : ChildOf<C_Button>(c, 0);
    s->m_plusButton = P(0.05) ? nullptr : ChildOf<C_Button>(c, 1);
    s->m_name = P(0.2) ? C_str() : C_str(Pick(NAMES));
    s->m_points = RandomCount();
    s->m_base = RandomCount();
    s->m_focus = I(-1, int(c->m_children.size()));
}
void SkillToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_SkillControl *>(c);
    OF<uint8_t *>(o, 0x6C) = OrigOf(s->m_nameTitle);
    OF<uint8_t *>(o, 0x70) = OrigOf(s->m_valueTitle);
    OF<uint8_t *>(o, 0x74) = OrigOf(s->m_plusButton);
    OF<uint8_t *>(o, 0x78) = OrigOf(s->m_minusButton);
    OF<uint32_t>(o, 0x7C) = OrigText(s->m_name);
    OF<int>(o, 0x80) = s->m_points;
    OF<int>(o, 0x84) = s->m_base;
    OF<int>(o, 0x88) = s->m_focus;
}
std::string SkillDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_SkillControl *>(c);
    const std::pair<uint32_t, const void *> parts[] = {
        {0x6C, s->m_nameTitle}, {0x70, s->m_valueTitle}, {0x74, s->m_plusButton}, {0x78, s->m_minusButton}};
    for (const auto &f : parts)
        if (PtrName(OF<void *>(o, f.first)) != PtrName(f.second))
            return "part +" + std::to_string(f.first) + " " + PtrName(OF<void *>(o, f.first)) + " / " + PtrName(f.second);
    if (TextOf(OF<uint32_t>(o, 0x7C)) != TextOf(s->m_name))
        return "name " + TextOf(OF<uint32_t>(o, 0x7C)) + " / " + TextOf(s->m_name);
    if (OF<int>(o, 0x80) != s->m_points || OF<int>(o, 0x84) != s->m_base || OF<int>(o, 0x88) != s->m_focus)
        return "points/base/focus " + std::to_string(OF<int>(o, 0x80)) + " " + std::to_string(OF<int>(o, 0x84)) + " " +
               std::to_string(OF<int>(o, 0x88)) + " / " + std::to_string(s->m_points) + " " + std::to_string(s->m_base) + " " +
               std::to_string(s->m_focus);
    return "";
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"title", 0x00813cc0, 0x70, Make<C_TitleControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"button", 0x00813f68, 0x84, Make<C_Button>, RandomButton, ButtonToOrig, ButtonDiffers},
    {"skill", 0x00814ba0, 0x8C, Make<C_SkillControl>, RandomSkill, SkillToOrig, SkillDiffers},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_TITLE = 1, K_BUTTON = 2, K_SKILL = 3;

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
int PlanAdd(int parent, int kind, int bits, bool child = true)
{
    g_plan.push_back({parent, kind, bits, child});
    return int(g_plan.size()) - 1;
}
// A skill: its titles (both: the original's uses them unchecked), its minus and plus buttons (now and then not there),
// now and then two swapped or an extra child.
void PlanSkill(int parent)
{
    const int at = PlanAdd(parent, K_SKILL, P(0.7) ? -1 : I(0, 15));
    std::vector<std::pair<int, int>> parts = {{K_TITLE, 1}, {K_TITLE, 1}};
    if (P(0.85))
        parts.push_back({K_BUTTON, 2});
    if (P(0.85))
        parts.push_back({K_BUTTON, 3});
    if (P(0.1)) {
        const size_t a = size_t(I(0, int(parts.size()) - 1)), b = size_t(I(0, int(parts.size()) - 1));
        std::swap(parts[a], parts[b]);
    }
    if (P(0.15))
        parts.push_back({Pick({K_CONTROL, K_TITLE, K_BUTTON}), P(0.5) ? -1 : I(0, 15)});
    for (const auto &p : parts)
        PlanAdd(at, p.first, p.second);
}
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    if (P(0.25)) {
        PlanSkill(-1);
    } else {
        const int root = PlanAdd(-1, K_CONTROL, -1);
        for (int k = I(1, 3); k > 0; --k)
            PlanSkill(root);
        if (P(0.3))
            PlanAdd(root, Pick({K_CONTROL, K_TITLE, K_BUTTON}), -1);
    }
    return g_plan;
}

// Create's frames: the titles' always (the original uses them unchecked), the buttons' now and then, made complete for
// the button's Create.
void Complete(int f, std::initializer_list<const char *> names, int count)
{
    if (f < 0)
        return;
    if (count)
        g_arena.numChildren[f] = count;
    for (const char *name : names)
        if (g_arena.child[f][ChildName(name)] < 0)
            g_arena.child[f][ChildName(name)] = static_cast<int16_t>(I(0, 15));
}
int PartFrame(int f, const char *name, bool always = false)
{
    int16_t &child = g_arena.child[f][ChildName(name)];
    if (child < 0 && (always || P(0.8)))
        child = static_cast<int16_t>(I(0, 15));
    return child;
}
void CompleteFor(int kind, int f)
{
    if (f < 0)
        return;
    switch (kind) {
    case K_BUTTON:
        Complete(f, {"normal*", "actived*", "inactived*"}, 3);
        break;
    case K_SKILL:
        PartFrame(f, "skill_text*", true);
        PartFrame(f, "skill_value*", true);
        CompleteFor(K_BUTTON, PartFrame(f, "barrow up*"));
        CompleteFor(K_BUTTON, PartFrame(f, "barrow down*"));
        break;
    }
}
void CreateFrames(size_t node, int fi) { CompleteFor(int(g_nodeKinds[node] - KINDS), fi); }

// ---- the pools ----
// A skill asks its pool (FindControl of its value, up first) for a point: the original's without one found crashes, a
// pool that is a skill asks its own; so the pools: none, a control found from every skill that is not a skill (the
// messages' SET_VALUE draw from them too), or another skill found as itself whose own pool is not a skill.
std::vector<uint32_t> g_safePools;
uint32_t Pick(const std::vector<uint32_t> &v) { return v[size_t(I(0, int(v.size()) - 1))]; }
std::vector<size_t> NodesOf(int kind)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[kind])
            out.push_back(i);
    return out;
}
bool IsSkill(const C_Control *c)
{
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_portNodes[i] == c)
            return g_nodeKinds[i] == &KINDS[K_SKILL];
    return false;
}
void AfterTree()
{
    g_cursorMade = true;
    const std::vector<size_t> skills = NodesOf(K_SKILL);
    g_safePools.clear();
    std::vector<uint32_t> ids;
    for (C_Control *c : g_portNodes)
        ids.push_back(c->m_id);
    ids.push_back(0xF);
    ids.push_back(U32());
    for (uint32_t id : ids) {
        if (id == 0 || std::find(g_safePools.begin(), g_safePools.end(), id) != g_safePools.end())
            continue;
        bool safe = true;
        for (size_t s : skills) {
            C_Control *found = g_portNodes[s]->FindControl(id, true);
            if (!found || IsSkill(found))
                safe = false;
        }
        if (safe)
            g_safePools.push_back(id);
    }
    // (First the pools that are no skills, then the skills that ask one of those.)
    std::vector<bool> terminal(skills.size());
    for (size_t k = 0; k < skills.size(); ++k) {
        terminal[k] = P(0.6);
        if (terminal[k])
            g_portNodes[skills[k]]->m_value = P(0.5) || g_safePools.empty() ? 0 : Pick(g_safePools);
    }
    for (size_t k = 0; k < skills.size(); ++k) {
        if (terminal[k])
            continue;
        C_Control *s = g_portNodes[skills[k]];
        std::vector<uint32_t> pools;
        for (size_t t = 0; t < skills.size(); ++t)
            if (terminal[t] && s->FindControl(g_portNodes[skills[t]]->m_id, true) == g_portNodes[skills[t]])
                pools.push_back(g_portNodes[skills[t]]->m_id);
        s->m_value = !pools.empty() && P(0.8) ? Pick(pools) : !g_safePools.empty() && P(0.5) ? Pick(g_safePools) : 0;
    }
}

// ---- the round's state and the messages' values ----
void PickRound()
{
    g_box.editRun = P(0.8);
    g_box.hoverAt = P(0.7);
    g_box.hovered = int16_t(I(-1, 6));
    g_box.selection = int16_t(I(-1, 6));
    g_box.scrollRange = int16_t(P(0.4) ? 0 : I(1, 20));
}

const char *const TEXTS[] = {"Shooting", "", "12", "Stamina"};
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_CALLBACK:
        // (A function always: the original calls a callback without checking it.)
        m.value = RandomNotification();
        m.value2 = RandomObject();
        m.value3 = g_portCbs[I(0, 2)];
        break;
    case CONTROL_MSG_SET_BASE:
    case CONTROL_MSG_SET_TOTAL:
        m.value = uint32_t(RandomCount() + (P(0.3) ? RandomCount() : 0));
        break;
    case CONTROL_MSG_ADD_TEXT_ID:
    case CONTROL_MSG_SET_TEXT_ID:
        m.value2 = uint32_t(Pick({2002, 1, 77, 0}));
        break;
    case CONTROL_MSG_ADD_TEXT:
    case CONTROL_MSG_SET_TEXT:
        m.text = Pick(TEXTS);
        break;
    case CONTROL_MSG_SET_VALUE:
        // (A pool that is safe: see AfterTree.)
        m.value = P(0.4) || g_safePools.empty() ? 0 : Pick(g_safePools);
        break;
    }
}

// ---- the tests ----
const uint32_t OWN_CODES[] = {CONTROL_MSG_SET_VALUE, CONTROL_MSG_SET_TEXT,  CONTROL_MSG_SKILL_UP,  CONTROL_MSG_SKILL_DOWN,
                              CONTROL_MSG_SET_BASE,  CONTROL_MSG_SET_TOTAL, CONTROL_MSG_GET_NUMBER, CONTROL_MSG_REFRESH,
                              CONTROL_MSG_ADD_TEXT,  CONTROL_MSG_SET_TEXT_ID, CONTROL_MSG_CLEAR,    CONTROL_MSG_GET_VALUE,
                              CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE, 0x1000000,              0x2000001};
size_t PickSkill()
{
    const std::vector<size_t> skills = NodesOf(K_SKILL);
    return skills[size_t(I(0, int(skills.size()) - 1))];
}
// A target for the skill: its id with any last 4 bits, everyone's, another.
uint32_t SkillTarget(size_t i)
{
    if (P(0.2))
        return 0xFFFFFFFF;
    if (P(0.6))
        return (g_portNodes[i]->m_id & ~0xFu) | uint32_t(I(0, 15));
    return RandomId();
}
// The skill's own messages sent straight to it.
void TestOwn()
{
    const size_t i = PickSkill();
    S_ControlMessage m;
    m.target = SkillTarget(i);
    m.id = Pick(OWN_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    if (P(0.3) && !m.text.m_rep)
        m.text = P(0.5) ? "hello" : "";
    g_messageBefore = m;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    const bool copy = P(0.2);
    Diff([&] { rp = copy ? g_portNodes[i]->HandleMessageCopy(m) : g_portNodes[i]->HandleMessage(&m); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, copy ? 0x74 : 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "message %x: %d / %d", g_messageBefore.id, ro, rp);
    if (!copy) {
        const std::string why = MessageDiffers(m, mo);
        CHECK(why.empty(), "the message after: %s", why.c_str());
    }
}

// The skill's input slots with ids naming its parts (any last 4 bits), its keys (the minus and plus ones, others, with
// bits above the byte).
void TestInput()
{
    const size_t i = PickSkill();
    C_Control *p = g_portNodes[i];
    const int id = int(SkillTarget(i));
    if (P(0.5))
        static_cast<C_SkillControl *>(p)->m_focus = I(-1, int(p->m_children.size()));
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter}, {0x34, &C_Control::OnLeave},   {0x40, &C_Control::OnInput05},
        {0x48, &C_Control::OnPress}, {0x50, &C_Control::OnInput08}, {0x38, &C_Control::OnClick},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    const int pick = I(0, n + 1);
    int rp = 0, ro = 0;
    if (pick < n) {
        Diff([&] { rp = (p->*slots[pick].port)(id); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], id); });
    } else if (pick == n) {
        const int key = Pick({0x0C, 0x4A, 0x0D, 0x4E, 0x0B, 0x0E, 0x4B, 0x4D, 0x4C, 0}) | (P(0.2) ? int(U32() & 0xFFFFFF00u) : 0);
        Diff([&] { rp = p->Unknown20(key); }, [&] { ro = Slot<BoolIntFn>(i, 0x20)(g_origNodes[i], key); });
    } else {
        Diff([&] { rp = p->FindChild(uint32_t(id)); }, [&] { ro = Slot<IntFn>(i, 0x00)(g_origNodes[i], id); });
    }
    CHECK(rp == ro, "input slot %d %x: %d / %d", pick, unsigned(id), ro, rp);
}

// SetStrings (the skill's does nothing).
using StringsFn = void(__attribute__((thiscall)) *)(void *, void *);
void TestStrings()
{
    const size_t i = PickSkill();
    std::vector<C_str> lines;
    for (int k = I(0, 3); k > 0; --k)
        lines.push_back(C_str(Pick(TEXTS)));
    Diff([&] { g_portNodes[i]->SetStrings(lines); },
         [&] {
             auto *v = static_cast<uint8_t *>(OrigAlloc(16));
             std::vector<uint32_t> reps;
             for (const C_str &s : lines)
                 reps.push_back(OrigText(s));
             OVecPut(v, 0, reps.data(), reps.size() * 4);
             Slot<StringsFn>(i, 0x78)(g_origNodes[i], v);
         });
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_arenaNames = 44;   // (its names: the item information's after them)
    g_treePlan = TreePlan;
    g_codes = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE,  CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_INDEX,
               CONTROL_MSG_GET_INDEX,  CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE,    CONTROL_MSG_REFRESH,    CONTROL_MSG_SET_TEXT,
               CONTROL_MSG_SET_TEXT_ID, CONTROL_MSG_ADD_TEXT, CONTROL_MSG_ADD_TEXT_ID,  CONTROL_MSG_CLEAR,      CONTROL_MSG_SET_MARGIN,
               CONTROL_MSG_SKILL_UP,   CONTROL_MSG_SKILL_DOWN, CONTROL_MSG_SET_BASE,    CONTROL_MSG_SET_TOTAL,  CONTROL_MSG_GET_NUMBER,
               CONTROL_MSG_ADD_CALLBACK, 0x1000000,           0x2000001,                0};
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY};
    // (A route's SET_VALUE would give a pool from an earlier tree.)
    g_noRouteCodes = {CONTROL_MSG_SET_VALUE};
    g_tickKinds = {K_SKILL, K_TITLE, K_BUTTON, K_CONTROL};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    g_afterTree = AfterTree;
    // The buttons' routes Create adds: their value2 and value3 the original's stack's.
    g_stackValue2Codes = {CONTROL_MSG_SKILL_UP, CONTROL_MSG_SKILL_DOWN};
    g_stackValue3Codes = g_stackValue2Codes;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SKILL_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("skill_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_SKILL_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_SKILL_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_SKILL_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Input", TestInput},   {"Strings", TestStrings},
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
    std::printf("skill_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
