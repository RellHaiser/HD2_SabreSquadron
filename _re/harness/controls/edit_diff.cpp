// Human part 52: the edit control, the edit box and the selection list (game/controls.cpp lines 2200-2764) against the
// original's. MinGW; build_edit.bat; runs edit_diff.exe ("edit_diff: N checks, 0 failures"). The kit (controls_kit.inc,
// controls_tree.inc) fakes what the controls reach outside themselves on both sides and compares the trees, the message
// queue, the engine's fakes and the calls on them.
// HD2_EDIT_ROUNDS: rounds per kind of call (default 20,000; HD2_EDIT_QUICK: 4,000); HD2_EDIT_CASE: one only;
// HD2_EDIT_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
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

// ---- the kinds ----
const uint32_t O_BOX = 0x6C;
template <class T> C_Control *Make(I3D_scene *scene, uint32_t id, C_Control *parent) { return new C_Logged<T>(scene, id, parent); }
I3D_frame *RandomFrame() { return FrameAt(I(0, 15)); }
void NoFields(C_Control *) {}
void NoToOrig(C_Control *, uint8_t *) {}
std::string NoDiffers(C_Control *, const uint8_t *) { return ""; }

// The profile's unicode font (the port's member, the original's byte at g_gameProfile + 0x30): a round's.
bool g_unicode;
void SetUnicode(bool on)
{
    g_unicode = on;
    g_gameProfile.m_unicodeFont = on;
    *OriginalAt<bool *>(0x008aea0c + 0x30) = on;
}
const char *const FORBIDDEN[] = {"abc", "0123456789", "-", " x"};
// A text the buffer may hold (no '%': the original shows the buffer through C_fstr, a flaw the port fixes; valid UTF-8 when
// the font is unicode: the original's backspace reads before the buffer at a lone continuation byte, also fixed).
std::string RandomBufferText(int maxLength)
{
    std::string s;
    // (Nearly full now and then: the limits' cases.)
    const int n = P(0.3) ? std::max(0, maxLength - I(0, 2)) : I(0, maxLength);
    // (Starting with a lead and its sequence now and then: a lead at the buffer's start.)
    if (g_unicode && P(0.2) && n >= 3)
        s = P(0.5) ? "\xC3\xA9" : "\xE2\x82\xAC";
    while (int(s.size()) < n) {
        const int kind = I(0, 9);
        if (kind < 6) {
            char c = char(I(0x20, 0x7E));
            if (c == '%')
                c = 'x';
            s += c;
        } else if (kind < 8) {
            s += char('0' + I(0, 9));
        } else if (g_unicode && !s.empty() && int(s.size()) + 5 <= n) {
            // (2, 3 and 4 bytes; and odd tails after a lead: a continuation too many, five of them, a lead cut short. Never
            // a continuation first: there the original's backspace reads before the buffer, a flaw the port fixes.)
            static const char *const seqs[] = {"\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xC3\xA9\xA9", "\xE2\x82",
                                               "\xF8\x80\x80\x80\x80", "\xF0\x9F\x98", "\xC3"};
            s += Pick(seqs);
        } else if (!g_unicode) {
            s += char(I(0x80, 0xFF));
        }
    }
    // (Ending in one of them now and then: the backspace's case.)
    // (Valid ones, the leads at their ranges' edges, a lone lead, wrong leads for their lengths.)
    static const char *const tails[] = {"\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xC3\xA9\xA9", "\xE2\x82", "\xF8\x80\x80\x80\x80",
                                        "\xC0\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xEF\xBF\xBF", "\xF0\x90\x80\x80", "\xF7\xBF\xBF\xBF",
                                        "\xC3", "\xF0", "\xC3\x80\x80\x80", "\xE2\x80\x80\x80"};
    if (g_unicode && P(0.3)) {
        const std::string tail = Pick(tails);
        if (int(tail.size()) + 1 <= maxLength)
            s = (s.empty() ? std::string("a") : s.substr(0, size_t(std::max(1, maxLength - int(tail.size()))))) + tail;
        if (int(s.size()) > maxLength)
            s.resize(size_t(maxLength));
    }
    return s;
}

// The edit control: the box, +0x70 the saved text, +0x74 the buffer (the original's a char *), +0x78 the forbidden
// characters, +0x7C editing, +0x7D digits only, +0x80 the run, +0x84 the longest.
void RandomEdit(C_Control *c)
{
    auto *e = static_cast<C_EditControl *>(c);
    RandomBox(e->m_box);
    e->m_savedText = P(0.3) ? C_str() : C_str(P(0.5) ? "saved" : "");
    e->m_maxLength = P(0.6) ? 30 : I(1, 40);
    e->m_buffer.assign(size_t(e->m_maxLength) + 1, '\0');
    const std::string text = RandomBufferText(e->m_maxLength);
    std::memcpy(e->m_buffer.data(), text.data(), text.size());
    // (Stale bytes after the text now and then, as a backspace leaves them: the terminators' writes show.)
    if (P(0.5))
        for (size_t k = text.size() + 1; k < e->m_buffer.size(); ++k)
            e->m_buffer[k] = char(P(0.5) ? I(0x21, 0x7E) : I(0x80, 0xBF));
    e->m_forbidden = P(0.5) ? nullptr : Pick(FORBIDDEN);
    e->m_editing = P(0.5);
    e->m_digitsOnly = P(0.3);
    e->m_run = I(-1, 7);
}
void EditToOrig(C_Control *c, uint8_t *o)
{
    auto *e = static_cast<C_EditControl *>(c);
    BoxToOrig(e->m_box, o, O_BOX);
    OF<uint32_t>(o, 0x70) = OrigText(e->m_savedText);
    // (The buffer in a block of the original's allocator: it frees it when it makes another.)
    auto *buffer = static_cast<char *>(std::calloc(1, e->m_buffer.size() + 1));
    std::memcpy(buffer, e->m_buffer.data(), e->m_buffer.size());
    OF<char *>(o, 0x74) = buffer;
    OF<const char *>(o, 0x78) = e->m_forbidden;
    OF<bool>(o, 0x7C) = e->m_editing;
    OF<bool>(o, 0x7D) = e->m_digitsOnly;
    OF<int>(o, 0x80) = e->m_run;
    OF<int>(o, 0x84) = e->m_maxLength;
}
std::string EditDiffers(C_Control *c, const uint8_t *o)
{
    auto *e = static_cast<C_EditControl *>(c);
    if (TextOf(OF<uint32_t>(o, 0x70)) != TextOf(e->m_savedText))
        return "saved text " + TextOf(OF<uint32_t>(o, 0x70)) + " / " + TextOf(e->m_savedText);
    if (OF<int>(o, 0x84) != e->m_maxLength)
        return "max length";
    const char *ob = OF<const char *>(o, 0x74);
    // (The port's buffer the original's size, the longest and its terminator.)
    if (e->m_buffer.size() != size_t(e->m_maxLength) + 1)
        return "the port's buffer of " + std::to_string(e->m_buffer.size()) + " for " + std::to_string(e->m_maxLength);
    if (!ob || std::memcmp(ob, e->m_buffer.data(), e->m_buffer.size()) != 0)
        return "buffer '" + std::string(ob ? ob : "(null)") + "' / '" + e->m_buffer.data() + "'";
    if (OF<const char *>(o, 0x78) != e->m_forbidden || OF<bool>(o, 0x7C) != e->m_editing || OF<bool>(o, 0x7D) != e->m_digitsOnly ||
        OF<int>(o, 0x80) != e->m_run)
        return "edit fields " + std::to_string(OF<bool>(o, 0x7C)) + std::to_string(OF<bool>(o, 0x7D)) + " " + std::to_string(OF<int>(o, 0x80)) +
               " / " + std::to_string(e->m_editing) + std::to_string(e->m_digitsOnly) + " " + std::to_string(e->m_run);
    return BoxDiffers(e->m_box, o, O_BOX);
}

// The edit box: +0x88 has a range, +0x8C min, +0x90 max, +0x94 normal, +0x98 active.
void RandomEditBox(C_Control *c)
{
    auto *e = static_cast<C_EditBox *>(c);
    RandomEdit(c);
    e->m_min = P(0.5) ? I(0, 10) : I(-100, 100);
    e->m_max = P(0.2) ? e->m_min : P(0.1) ? e->m_min - I(1, 20) : e->m_min + I(0, 100);
    e->m_hasRange = P(0.9) ? e->m_min != e->m_max : P(0.5);
    e->m_normalFrame = RandomFrame();
    e->m_activeFrame = RandomFrame();
}
void EditBoxToOrig(C_Control *c, uint8_t *o)
{
    auto *e = static_cast<C_EditBox *>(c);
    EditToOrig(c, o);
    OF<bool>(o, 0x88) = e->m_hasRange;
    OF<int>(o, 0x8C) = e->m_min;
    OF<int>(o, 0x90) = e->m_max;
    OF<I3D_frame *>(o, 0x94) = e->m_normalFrame;
    OF<I3D_frame *>(o, 0x98) = e->m_activeFrame;
}
std::string EditBoxDiffers(C_Control *c, const uint8_t *o)
{
    auto *e = static_cast<C_EditBox *>(c);
    if (OF<bool>(o, 0x88) != e->m_hasRange || OF<int>(o, 0x8C) != e->m_min || OF<int>(o, 0x90) != e->m_max)
        return "range";
    if (OF<I3D_frame *>(o, 0x94) != e->m_normalFrame || OF<I3D_frame *>(o, 0x98) != e->m_activeFrame)
        return "edit box frames";
    return EditDiffers(c, o);
}

// The selection list: +0x88 the select frame, +0x8C the cursor, +0x94 the selected line.
void RandomSelect(C_Control *c)
{
    auto *s = static_cast<C_SelectList *>(c);
    RandomEdit(c);
    s->m_selectFrame = P(0.2) ? nullptr : RandomFrame();
    s->m_cursor = TheCursor();
    s->m_selected = I(-1, 7);
}
void SelectToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_SelectList *>(c);
    EditToOrig(c, o);
    OF<I3D_frame *>(o, 0x88) = s->m_selectFrame;
    OF<void *>(o, 0x8C) = s->m_cursor;
    OF<int>(o, 0x94) = s->m_selected;
}
std::string SelectDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_SelectList *>(c);
    if (OF<I3D_frame *>(o, 0x88) != s->m_selectFrame || PtrName(OF<void *>(o, 0x8C)) != PtrName(s->m_cursor))
        return "select frame / cursor";
    if (OF<int>(o, 0x94) != s->m_selected)
        return "selected " + std::to_string(OF<int>(o, 0x94)) + " / " + std::to_string(s->m_selected);
    return EditDiffers(c, o);
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"edit", 0x00813ee0, 0x88, Make<C_EditControl>, RandomEdit, EditToOrig, EditDiffers, O_BOX, nullptr, true},
    {"editbox", 0x00813d48, 0x9C, Make<C_EditBox>, RandomEditBox, EditBoxToOrig, EditBoxDiffers, O_BOX, nullptr, true},
    {"select", 0x00814298, 0x98, Make<C_SelectList>, RandomSelect, SelectToOrig, SelectDiffers, O_BOX, nullptr, true},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_EDIT = 1, K_EDITBOX = 2, K_SELECT = 3;

}  // namespace

namespace {
#include "controls_tree.inc"

// A round's profile and box answers.
void RoundState()
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
    static const char *const selected[] = {"sel", "", "12", "-3", "abc", "99999", "7x"};
    g_box.selected = Pick(selected);
}
// (Before the tree: its buffers follow the round's font.)
void PickRound()
{
    RoundState();
}

void CreateFrames(size_t node, int fi)
{
    const S_Kind *k = g_nodeKinds[node];
    if (k == &KINDS[K_EDITBOX] && P(0.85))
        for (const char *name : {"normal*", "actived*"})
            if (g_arena.child[fi][ChildName(name)] < 0)
                g_arena.child[fi][ChildName(name)] = static_cast<int16_t>(I(0, 15));
}

const char *const TEXTS[] = {"run3", "RUN0", "Run3", "nothing", "", "12"};
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
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
    case CONTROL_MSG_GET_ITEM:
    case CONTROL_MSG_SET_POSITION:
        m.value = uint32_t(I(-1, 8));
        m.value2 = uint32_t(I(-100, 1000));
        break;
    case CONTROL_MSG_SELECT_TEXT:   // (WHITE_TEXT's code too)
    case CONTROL_MSG_INSERT_FIRST:
    case CONTROL_MSG_ADD_TEXT:
        m.text = Pick(TEXTS);
        break;
    }
}

// Typing: OnChar while editing (the keys: control ones, printable, digits, the forbidden, full-width digits, unicode), and
// the editing's own slots (BeginEdit, IsEditing, OnInput02, OnInput0B, OnClick, the tick).
void TestType()
{
    RandomTree({0, K_EDIT, K_EDITBOX, K_SELECT});
    std::vector<size_t> nodes;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] != &KINDS[0])
            nodes.push_back(i);
    if (nodes.empty())
        return;
    const size_t i = nodes[size_t(I(0, int(nodes.size()) - 1))];
    auto *e = static_cast<C_EditControl *>(g_portNodes[i]);
    const int pick = I(0, 9);
    if (pick < 5) {
        // (A digits-only control typed past 0x7F only with a unicode font: the original's isdigit takes the byte
        // signed, a flaw the port fixes.)
        int key;
        const int kind = I(0, 9);
        if (kind == 0 || P(0.25))
            key = P(0.6) ? 8 : Pick({0, 8, 9, 13, 27, 0x7F});
        else if (kind < 5)
            key = I(0x20, 0x7E);
        else if (kind < 7)
            key = Pick({'0', '5', '9', '-', 'a', 'b', ' '});
        else if (kind == 7)
            key = 0xFF10 + I(0, 9);
        else
            key = Pick({0xE9, 0x20AC, 0x3042, 0x7FF, 0x800, 0xFFFF, 0x80});
        if (key == '%')
            key = '&';
        if (key > 0x7F && !g_unicode && e->m_digitsOnly && !(key >= 0xFF10 && key <= 0xFF19))
            key = 'q';
        // (Byte mode: a key past 0xFF taken as its low byte by the original's char: 0x100 and 0x200 too, a 0 byte.)
        if (!g_unicode && key > 0xFF && !(key >= 0xFF10 && key <= 0xFF19))
            key = P(0.3) ? Pick({0x100, 0x200}) : (key & 0x7F);
        if (key == '%')
            key = '&';
        if (P(0.8))
            e->m_editing = true;
        bool rp = false, ro = false;
        Diff([&] { rp = e->OnChar(key); }, [&] { ro = Slot<BoolIntFn>(i, 0x24)(g_origNodes[i], key); });
        CHECK(rp == ro, "OnChar %x (%s): %d / %d", key, g_nodeKinds[i]->name, ro, rp);
        return;
    }
    int rp = 0, ro = 0;
    const int id = P(0.7) ? int(e->m_id) : I(-1, 5);
    switch (pick) {
    case 5:
        Diff([&] { rp = e->BeginEdit(id); }, [&] { ro = Slot<IntFn>(i, 0x68)(g_origNodes[i], id); });
        break;
    case 6:
        Diff([&] { rp = e->IsEditing(); }, [&] { ro = Slot<BoolFn>(i, 0x28)(g_origNodes[i]); });
        break;
    case 7:
        Diff([&] { rp = e->OnInput02(id); }, [&] { ro = Slot<IntFn>(i, 0x3C)(g_origNodes[i], id); });
        break;
    case 8:
        Diff([&] { rp = e->OnInput0B(id); }, [&] { ro = Slot<IntFn>(i, 0x64)(g_origNodes[i], id); });
        break;
    default:
        Diff([&] { rp = e->OnKeyClick(id); }, [&] { ro = Slot<IntFn>(i, 0x5C)(g_origNodes[i], id); });
        break;
    }
    CHECK(rp == ro, "edit slot %d (%s): %d / %d", pick, g_nodeKinds[i]->name, ro, rp);
}

// The kinds' own messages sent straight to them.
void TestOwn()
{
    RandomTree({0, K_EDIT, K_EDITBOX, K_SELECT});
    std::vector<size_t> nodes;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] != &KINDS[0])
            nodes.push_back(i);
    if (nodes.empty())
        return;
    const size_t i = nodes[size_t(I(0, int(nodes.size()) - 1))];
    S_ControlMessage m;
    m.target = 0xFFFFFFFF;
    const uint32_t codes[] = {CONTROL_MSG_SET_FORBIDDEN, CONTROL_MSG_SET_MAX_LENGTH, CONTROL_MSG_SET_DIGITS_ONLY, CONTROL_MSG_IGNORED_74,
                              CONTROL_MSG_EDIT,          CONTROL_MSG_GET_TEXT,       CONTROL_MSG_END_EDIT,        CONTROL_MSG_SET_RANGE,
                              CONTROL_MSG_SET_NUMBER,    CONTROL_MSG_GET_NUMBER,     CONTROL_MSG_SET_MARGIN,      CONTROL_MSG_REFRESH,
                              CONTROL_MSG_CLEAR,         CONTROL_MSG_GET_ITEM,       CONTROL_MSG_SET_VALUE,       CONTROL_MSG_ADD_TEXT,
                              CONTROL_MSG_ADD_TEXT_ID,   CONTROL_MSG_INSERT_FIRST,   CONTROL_MSG_REMOVE_SELECTED, CONTROL_MSG_PREVIOUS_ITEM,
                              CONTROL_MSG_SET_POSITION,  CONTROL_MSG_GET_RUNS,       CONTROL_MSG_SELECT_TEXT};
    m.id = Pick(codes);
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    MessageValues(m);
    if (m.id == CONTROL_MSG_SET_VALUE && P(0.5) && !g_portNodes.empty())
        m.value = g_portNodes[RandomNode()]->m_id;
    g_messageBefore = m;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    Diff([&] { rp = g_portNodes[i]->HandleMessage(&m); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "message %x to %s: %d / %d", g_messageBefore.id, g_nodeKinds[i]->name, ro, rp);
    const std::string why = MessageDiffers(m, mo);
    CHECK(why.empty(), "the message after (%s): %s", g_nodeKinds[i]->name, why.c_str());
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_codes = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE, CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_INDEX,
               CONTROL_MSG_GET_INDEX, CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE, CONTROL_MSG_REFRESH, CONTROL_MSG_SET_TEXT,
               CONTROL_MSG_ADD_TEXT, CONTROL_MSG_CLEAR, CONTROL_MSG_GET_RUNS, CONTROL_MSG_SET_MARGIN, CONTROL_MSG_SET_FORBIDDEN,
               CONTROL_MSG_SET_MAX_LENGTH, CONTROL_MSG_SET_DIGITS_ONLY, CONTROL_MSG_IGNORED_74, CONTROL_MSG_EDIT, CONTROL_MSG_GET_TEXT,
               CONTROL_MSG_SET_RANGE, CONTROL_MSG_SET_NUMBER, CONTROL_MSG_GET_NUMBER, CONTROL_MSG_GET_ITEM, CONTROL_MSG_INSERT_FIRST,
               CONTROL_MSG_REMOVE_SELECTED, CONTROL_MSG_SET_POSITION, CONTROL_MSG_SELECT_TEXT, 0x1000000, 0x2000001, 0};
    // (Ending the editing notifies 02: loud, and no loud route on 02.)
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_END_EDIT};
    NOTIFY_RAISING = {CONTROL_NOTIFY_05, CONTROL_NOTIFY_0B, CONTROL_NOTIFY_CLICK};
    g_tickKinds = {0, K_EDIT, K_SELECT};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_EDIT_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("edit_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_EDIT_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_EDIT_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_EDIT_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Type", TestType},       {"Own", TestOwn},
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
    std::printf("edit_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
