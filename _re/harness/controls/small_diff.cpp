// Human part 49: the small controls (game/controls.cpp: the text control, the title, the label, the picture, the group,
// the frame switch, the text button, the tooltip, the button) against the original's. MinGW; build_small.bat; runs
// small_diff.exe ("small_diff: N checks, 0 failures"). The kit (controls_kit.inc) fakes what the controls reach outside
// themselves on both sides and compares the trees, the message queue, the engine's fakes and the calls on them.
// HD2_SMALL_ROUNDS: rounds per kind of call (default 20,000; HD2_SMALL_QUICK: 4,000); HD2_SMALL_CASE: one only;
// HD2_SMALL_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
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

// Text controls (+0x6C the box).
void RandomText(C_Control *c) { RandomBox(static_cast<C_TextControl *>(c)->m_box); }
void TextToOrig(C_Control *c, uint8_t *o) { BoxToOrig(static_cast<C_TextControl *>(c)->m_box, o, O_BOX); }
std::string TextDiffers(C_Control *c, const uint8_t *o) { return BoxDiffers(static_cast<C_TextControl *>(c)->m_box, o, O_BOX); }

// The picture: the box, +0x70 its texture, +0x74 its face group.
void RandomPicture(C_Control *c)
{
    auto *p = static_cast<C_PictureControl *>(c);
    RandomText(c);
    p->m_texture = P(0.2) ? nullptr : reinterpret_cast<I3D_texture *>(g_arena.textures[I(0, 3)]);
    p->m_faceGroup = reinterpret_cast<I3D_face_group *>(g_arena.groups[I(0, 3)]);
}
void PictureToOrig(C_Control *c, uint8_t *o)
{
    auto *p = static_cast<C_PictureControl *>(c);
    TextToOrig(c, o);
    OF<void *>(o, 0x70) = p->m_texture;
    OF<void *>(o, 0x74) = p->m_faceGroup;
}
std::string PictureDiffers(C_Control *c, const uint8_t *o)
{
    auto *p = static_cast<C_PictureControl *>(c);
    if (OF<void *>(o, 0x70) != p->m_texture || OF<void *>(o, 0x74) != p->m_faceGroup)
        return "texture " + PtrName(OF<void *>(o, 0x70)) + " group " + PtrName(OF<void *>(o, 0x74)) + " / " + PtrName(p->m_texture) + " " +
               PtrName(p->m_faceGroup);
    return TextDiffers(c, o);
}

// The group: +0x6C its focus.
void RandomGroup(C_Control *c)
{
    auto *g = static_cast<C_ControlGroup *>(c);
    g->m_focus = I(-1, int(g->m_children.size()));
}
void GroupToOrig(C_Control *c, uint8_t *o) { OF<int>(o, 0x6C) = static_cast<C_ControlGroup *>(c)->m_focus; }
std::string GroupDiffers(C_Control *c, const uint8_t *o)
{
    return OF<int>(o, 0x6C) == static_cast<C_ControlGroup *>(c)->m_focus ? ""
                                                                          : "focus " + std::to_string(OF<int>(o, 0x6C)) + " / " +
                                                                                std::to_string(static_cast<C_ControlGroup *>(c)->m_focus);
}

// The frame switch: +0x6C its frames (an MSVC6 vector), +0x7C the selected.
void RandomSwitch(C_Control *c)
{
    auto *s = static_cast<C_FrameSwitch *>(c);
    s->m_frames.clear();
    for (int k = I(0, 4); k > 0; --k)
        s->m_frames.push_back(RandomFrame());
    s->m_selected = P(0.8) ? I(-1, int(s->m_frames.size())) : Pick({-100, 1000});
}
void SwitchToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_FrameSwitch *>(c);
    OVecPut(o, 0x6C, s->m_frames.data(), s->m_frames.size() * 4);
    OF<int>(o, 0x7C) = s->m_selected;
}
std::string SwitchDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_FrameSwitch *>(c);
    if (OVecBytes(o, 0x6C) != s->m_frames.size() * 4 ||
        (!s->m_frames.empty() && std::memcmp(OVecData(o, 0x6C), s->m_frames.data(), s->m_frames.size() * 4) != 0))
        return "frames " + std::to_string(OVecBytes(o, 0x6C) / 4) + " / " + std::to_string(s->m_frames.size());
    return OF<int>(o, 0x7C) == s->m_selected ? "" : "selected " + std::to_string(OF<int>(o, 0x7C)) + " / " + std::to_string(s->m_selected);
}

// The text button: the box, +0x70 normal, +0x74 active.
void RandomTextButton(C_Control *c)
{
    auto *b = static_cast<C_TextButton *>(c);
    RandomText(c);
    b->m_normalFrame = RandomFrame();
    b->m_activeFrame = RandomFrame();
}
void TextButtonToOrig(C_Control *c, uint8_t *o)
{
    auto *b = static_cast<C_TextButton *>(c);
    TextToOrig(c, o);
    OF<void *>(o, 0x70) = b->m_normalFrame;
    OF<void *>(o, 0x74) = b->m_activeFrame;
}
std::string TextButtonDiffers(C_Control *c, const uint8_t *o)
{
    auto *b = static_cast<C_TextButton *>(c);
    if (OF<void *>(o, 0x70) != b->m_normalFrame || OF<void *>(o, 0x74) != b->m_activeFrame)
        return "frames " + PtrName(OF<void *>(o, 0x70)) + " " + PtrName(OF<void *>(o, 0x74)) + " / " + PtrName(b->m_normalFrame) + " " +
               PtrName(b->m_activeFrame);
    return TextDiffers(c, o);
}

// The tooltip: the box, +0x70 armed, +0x74 the cursor.
void RandomTooltip(C_Control *c)
{
    auto *t = static_cast<C_Tooltip *>(c);
    RandomText(c);
    t->m_armed = P(0.6);
    t->m_cursor = P(0.9) || t->m_armed ? TheCursor() : nullptr;
}
void TooltipToOrig(C_Control *c, uint8_t *o)
{
    auto *t = static_cast<C_Tooltip *>(c);
    TextToOrig(c, o);
    OF<bool>(o, 0x70) = t->m_armed;
    OF<void *>(o, 0x74) = t->m_cursor;
}
std::string TooltipDiffers(C_Control *c, const uint8_t *o)
{
    auto *t = static_cast<C_Tooltip *>(c);
    if (OF<bool>(o, 0x70) != t->m_armed || PtrName(OF<void *>(o, 0x74)) != PtrName(t->m_cursor))
        return "armed/cursor";
    return TextDiffers(c, o);
}

// The button: +0x6C the cursor, +0x70 normal, +0x74 active, +0x78 inactive, +0x7C held, +0x80 the repeat time.
void RandomButton(C_Control *c)
{
    auto *b = static_cast<C_Button *>(c);
    b->m_cursor = TheCursor();
    b->m_normalFrame = RandomFrame();
    b->m_activeFrame = RandomFrame();
    b->m_inactiveFrame = RandomFrame();
    b->m_held = P(0.5);
    b->m_repeatTime = P(0.5) ? Pick({300, -1, 0, 299, 301, 500}) : I(-5, 2000);
}
void ButtonToOrig(C_Control *c, uint8_t *o)
{
    auto *b = static_cast<C_Button *>(c);
    OF<void *>(o, 0x6C) = b->m_cursor;
    OF<void *>(o, 0x70) = b->m_normalFrame;
    OF<void *>(o, 0x74) = b->m_activeFrame;
    OF<void *>(o, 0x78) = b->m_inactiveFrame;
    OF<bool>(o, 0x7C) = b->m_held;
    OF<int>(o, 0x80) = b->m_repeatTime;
}
std::string ButtonDiffers(C_Control *c, const uint8_t *o)
{
    auto *b = static_cast<C_Button *>(c);
    char text[160];
    if (PtrName(OF<void *>(o, 0x6C)) != PtrName(b->m_cursor) || OF<void *>(o, 0x70) != b->m_normalFrame || OF<void *>(o, 0x74) != b->m_activeFrame ||
        OF<void *>(o, 0x78) != b->m_inactiveFrame || OF<bool>(o, 0x7C) != b->m_held || OF<int>(o, 0x80) != b->m_repeatTime) {
        std::snprintf(text, sizeof text, "button %s %s %s %s %d %d / %s %s %s %s %d %d", PtrName(OF<void *>(o, 0x6C)).c_str(),
                      PtrName(OF<void *>(o, 0x70)).c_str(), PtrName(OF<void *>(o, 0x74)).c_str(), PtrName(OF<void *>(o, 0x78)).c_str(),
                      OF<bool>(o, 0x7C), OF<int>(o, 0x80), PtrName(b->m_cursor).c_str(), PtrName(b->m_normalFrame).c_str(),
                      PtrName(b->m_activeFrame).c_str(), PtrName(b->m_inactiveFrame).c_str(), b->m_held, b->m_repeatTime);
        return text;
    }
    return "";
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"text", 0x00813e58, 0x70, Make<C_TextControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"title", 0x00813cc0, 0x70, Make<C_TitleControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"label", 0x00814188, 0x70, Make<C_LabelControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"picture", 0x00813ff0, 0x78, Make<C_PictureControl>, RandomPicture, PictureToOrig, PictureDiffers, O_BOX},
    {"group", 0x00814430, 0x70, Make<C_ControlGroup>, RandomGroup, GroupToOrig, GroupDiffers},
    {"switch", 0x00815310, 0x80, Make<C_FrameSwitch>, RandomSwitch, SwitchToOrig, SwitchDiffers},
    {"textbutton", 0x00814cb0, 0x78, Make<C_TextButton>, RandomTextButton, TextButtonToOrig, TextButtonDiffers, O_BOX},
    {"tooltip", 0x008148f8, 0x78, Make<C_Tooltip>, RandomTooltip, TooltipToOrig, TooltipDiffers, O_BOX},
    {"button", 0x00813f68, 0x84, Make<C_Button>, RandomButton, ButtonToOrig, ButtonDiffers},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);

// ---- random trees (part 48's rules: self routes that raise notifications only on these, notifying only those) ----
const uint32_t NOTIFICATIONS[] = {CONTROL_NOTIFY_CLICK, CONTROL_NOTIFY_02, CONTROL_NOTIFY_ENTER, CONTROL_NOTIFY_LEAVE, CONTROL_NOTIFY_05,
                                  CONTROL_NOTIFY_PRESS, CONTROL_NOTIFY_RELEASE, CONTROL_NOTIFY_08, CONTROL_NOTIFY_09, CONTROL_NOTIFY_0A,
                                  CONTROL_NOTIFY_0B, CONTROL_NOTIFY_HIDDEN, CONTROL_NOTIFY_SHOWN, CONTROL_NOTIFY_WHEEL,
                                  CONTROL_NOTIFY_KEY_CLICK, CONTROL_NOTIFY_CHANGED, CONTROL_NOTIFY_13};
const uint32_t NOTIFY_RAISING[] = {CONTROL_NOTIFY_02, CONTROL_NOTIFY_05, CONTROL_NOTIFY_0B};
const uint32_t NOTIFY_RAISED[] = {CONTROL_NOTIFY_08, CONTROL_NOTIFY_09, CONTROL_NOTIFY_0A, CONTROL_NOTIFY_13, CONTROL_NOTIFY_CHANGED};
// The codes: the base's and the controls'.
const uint32_t CODES[] = {
    CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE, CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_INDEX,
    CONTROL_MSG_GET_INDEX, CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE, CONTROL_MSG_REFRESH, CONTROL_MSG_SET_TEXT, CONTROL_MSG_SET_TEXT_ID,
    CONTROL_MSG_ADD_TEXT, CONTROL_MSG_ADD_TEXT_ID, CONTROL_MSG_SET_RUN_TEXT, CONTROL_MSG_REMOVE_RUN, CONTROL_MSG_CLEAR_TEXT,
    CONTROL_MSG_FACE_FRONT, CONTROL_MSG_SET_COLOUR, CONTROL_MSG_CLEAR, CONTROL_MSG_GET_RUNS, CONTROL_MSG_SET_MARGIN, CONTROL_MSG_WHITE_TEXT,
    CONTROL_MSG_RESTORE_TEXTURE, CONTROL_MSG_LOAD_TEXTURE, CONTROL_MSG_FIND_FACE_GROUP, CONTROL_MSG_ADD_FRAME,
    CONTROL_MSG_SELECT_FRAME, CONTROL_MSG_HIDE_FRAME, CONTROL_MSG_IGNORED_25, 0x1000000, 0x2000001, 0,
};
uint32_t RandomNotification() { return P(0.05) ? 0 : P(0.03) ? CONTROL_NOTIFY_ANY : Pick(NOTIFICATIONS); }
uintptr_t RandomObject() { return P(0.2) ? 0 : reinterpret_cast<uintptr_t>(g_objects[I(0, 1)]); }
uintptr_t RandomFunction() { return P(0.15) ? 0 : g_portCbs[I(0, 2)]; }
const char *const FRAME_NAMES[] = {"normal*", "page1", "x"};
const char *const TEXTURE_NAMES[] = {"menu.bmp", "face.tga", "", "nope.bmp"};
S_ControlMessage RandomMessage(bool quiet)
{
    S_ControlMessage m;
    m.target = P(0.5) ? 0xFFFFFFFF : P(0.7) && !g_portNodes.empty() ? g_portNodes[size_t(I(0, int(g_portNodes.size()) - 1))]->m_id : U32();
    if (quiet || P(0.7)) {
        m.id = Pick(CODES);
    } else {
        // (The picture's SET_TEXTURE shows or hides it: loud too.)
        const uint32_t loud[] = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_SET_TEXTURE};
        m.id = Pick(loud);
    }
    m.value = P(0.5) ? uint32_t(I(-2, 8)) : U32();
    m.value2 = P(0.5) ? uint32_t(I(-2, 8)) : U32();
    m.value3 = U32();
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_NOTIFY:
        m.value = Pick(NOTIFY_RAISED);
        break;
    case CONTROL_MSG_SET_TEXTURE:
        m.value2 = P(0.3) ? 0 : reinterpret_cast<uintptr_t>(g_arena.textures[I(0, 3)]);
        break;
    case CONTROL_MSG_ADD_FRAME:
        m.value2 = reinterpret_cast<uintptr_t>(Pick(FRAME_NAMES));
        break;
    case CONTROL_MSG_LOAD_TEXTURE:
    case CONTROL_MSG_FIND_FACE_GROUP:
        m.text = Pick(TEXTURE_NAMES);
        if (P(0.2))
            m.text = C_str();
        break;
    }
    if (P(0.3) && !m.text.m_rep)
        m.text = P(0.5) ? "hello" : "";
    return m;
}

void ClearPortTree()
{
    for (C_Control *c : g_portNodes) {
        c->m_children.clear();
        delete c;
    }
    g_portNodes.clear();
    g_nodeKinds.clear();
}

// `kinds`: the kinds the tree draws from (empty: all).
void RandomTree(const std::vector<int> &kinds = {})
{
    ClearPortTree();
    RandomArena();
    g_cursorMade = P(0.9);
    const int count = P(0.1) ? 1 : I(2, 8);
    std::vector<int> depth;
    const uint32_t top = P(0.8) ? (uint32_t(I(1, 63)) << 26) : 0;
    for (int i = 0; i < count; ++i) {
        // (Parents: groups or plain controls mostly.)
        C_Control *parent = i == 0 ? nullptr : g_portNodes[size_t(I(0, i - 1))];
        int d = 0;
        uint32_t id = top;
        if (parent) {
            const size_t pi = size_t(std::find(g_portNodes.begin(), g_portNodes.end(), parent) - g_portNodes.begin());
            d = depth[pi] + 1;
            const uint32_t index = uint32_t(parent->m_children.size() + 1);
            const int shift = d == 1 ? 20 : d == 2 ? 12 : 4;
            id = d <= 3 ? parent->m_id | (index << shift) : parent->m_id;
        }
        if (P(0.05))
            id = P(0.5) ? 0xFFFFFFFF : U32();
        const int k = kinds.empty() ? I(0, KIND_COUNT - 1) : kinds[size_t(I(0, int(kinds.size()) - 1))];
        C_Control *c = KINDS[k].make(reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000 + I(0, 3))), id, parent);
        if (parent && P(0.95))
            parent->m_children.push_back(c);
        g_portNodes.push_back(c);
        g_nodeKinds.push_back(&KINDS[k]);
        depth.push_back(d);
        c->m_frame = RandomFrame();
        c->m_type = P(0.5) ? 0 : I(-1, 0x30);
        c->m_value = P(0.5) ? 0 : U32();
        c->m_index = P(0.5) ? -1 : I(-3, 30);
        c->m_inactive = P(0.3);
        c->m_shown = P(0.7);
        c->m_takesInput = P(0.7);
        for (int r = I(0, 3); r > 0; --r) {
            S_ControlMessage m = RandomMessage(false);
            const uint32_t code = m.id & CONTROL_MSG_MASK;
            const bool loud = code == CONTROL_MSG_SHOW || code == CONTROL_MSG_HIDE || code == CONTROL_MSG_NOTIFY || code == CONTROL_MSG_SET_TEXTURE;
            const uint32_t on = loud ? Pick(NOTIFY_RAISING) : RandomNotification();
            m.id = (m.id & ~CONTROL_NOTIFY_MASK) | (on & CONTROL_NOTIFY_MASK);
            // (Self routes only with the base's codes and the text's: a self route's message of another kind would need
            // its own state, a texture's message its face group.)
            c->m_routes.push_back(m);
        }
        for (int r = I(0, 2); r > 0; --r) {
            S_ControlCallback cb;
            cb.notification = RandomNotification();
            cb.object = reinterpret_cast<void *>(RandomObject());
            cb.function = reinterpret_cast<ControlCallback>(RandomFunction());
            c->m_callbacks.push_back(cb);
        }
    }
        // (The catch-all callback now and then: every notification shows.)
    for (C_Control *c : g_portNodes)
        if (P(0.3)) {
            c->m_anyCallback.notification = CONTROL_NOTIFY_ANY;
            c->m_anyCallback.object = reinterpret_cast<void *>(RandomObject());
            c->m_anyCallback.function = reinterpret_cast<ControlCallback>(g_portCbs[I(0, 2)]);
        }
    // The kinds' own fields (the group's focus after its children).
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        g_nodeKinds[i]->randomize(g_portNodes[i]);
    g_cbResults.clear();
    for (int k = 0; k < 7; ++k)
        g_cbResults.push_back(P(0.7));
}

size_t RandomNode() { return size_t(I(0, int(g_portNodes.size()) - 1)); }
uint32_t RandomId()
{
    if (P(0.7))
        return g_portNodes[RandomNode()]->m_id ^ (P(0.2) ? (1u << I(0, 31)) : 0);
    return P(0.3) ? 0xF : U32();
}

// The original's slot of a node (its logged vtable: Refresh and HandleMessageCopy logged).
template <class Fn> Fn Slot(size_t i, uint32_t slot) { return reinterpret_cast<Fn>(static_cast<uintptr_t>(OF<uint32_t *>(g_origNodes[i], 0)[slot / 4])); }
using VoidFn = void(__attribute__((thiscall)) *)(void *);
using VoidIntFn = void(__attribute__((thiscall)) *)(void *, int);
using IntFn = int(__attribute__((thiscall)) *)(void *, int);
using Int2Fn = int(__attribute__((thiscall)) *)(void *, int, int);
using BoolFn = bool(__attribute__((thiscall)) *)(void *);
using BoolIntFn = bool(__attribute__((thiscall)) *)(void *, int);
using MsgFn = bool(__attribute__((thiscall)) *)(void *, void *);
using CreateFn = bool(__attribute__((thiscall)) *)(void *, void *, void *, void *);

// A text control's box taken away now and then (Refresh and SetInactive check for one).
void DropBox(size_t i)
{
    if (g_nodeKinds[i]->boxAt && P(0.2)) {
        auto *t = static_cast<C_TextControl *>(g_portNodes[i]);
        delete t->m_box;
        t->m_box = nullptr;
    }
}

// The input slots, the ticks and the others without a message.
void TestSlots()
{
    const size_t i = RandomNode();
    C_Control *p = g_portNodes[i];
    const int id = int(RandomId()), amount = I(-3, 3), time = I(0, 100);
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter},   {0x34, &C_Control::OnLeave},   {0x38, &C_Control::OnClick},   {0x3C, &C_Control::OnInput02},
        {0x40, &C_Control::OnInput05}, {0x44, &C_Control::OnRelease}, {0x48, &C_Control::OnPress},   {0x50, &C_Control::OnInput08},
        {0x54, &C_Control::OnInput09}, {0x58, &C_Control::OnInput0A}, {0x5C, &C_Control::OnKeyClick}, {0x60, &C_Control::OnInput13},
        {0x64, &C_Control::OnInput0B},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    const int pick = I(0, n + 9);
    int rp = 0, ro = 0;
    if (pick < n) {
        Diff([&] { rp = (p->*slots[pick].port)(id); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], id); });
    } else {
        switch (pick - n) {
        case 0:
            Diff([&] { rp = p->OnWheel(id, amount); }, [&] { ro = Slot<Int2Fn>(i, 0x4C)(g_origNodes[i], id, amount); });
            break;
        case 1:
            Diff([&] { p->Tick(time); }, [&] { Slot<VoidIntFn>(i, 0x1C)(g_origNodes[i], time); });
            break;
        case 2:
            Diff([&] { rp = p->Unknown20(id); }, [&] { ro = Slot<BoolIntFn>(i, 0x20)(g_origNodes[i], id); });
            break;
        case 3:
            DropBox(i);
            Diff([&] { p->Refresh(); }, [&] { Slot<VoidFn>(i, 0x18)(g_origNodes[i]); });
            break;
        case 4: {
            DropBox(i);
            const bool on = P(0.5);
            Diff([&] { p->SetInactive(on); }, [&] { Slot<VoidIntFn>(i, 0x7C)(g_origNodes[i], on); });
            break;
        }
        case 5:
            Diff([&] { p->Show(); }, [&] { Slot<VoidFn>(i, 0x10)(g_origNodes[i]); });
            break;
        case 6:
            Diff([&] { p->Hide(); }, [&] { Slot<VoidFn>(i, 0x14)(g_origNodes[i]); });
            break;
        case 7: {
            const int index = I(-2, 6);
            Diff([&] { rp = p->IsChild(index); }, [&] { ro = Slot<BoolIntFn>(i, 0x04)(g_origNodes[i], index); });
            break;
        }
        case 8:
            Diff([&] { rp = p->TakesInput(); }, [&] { ro = Slot<BoolFn>(i, 0x08)(g_origNodes[i]); });
            break;
        default:
            Diff([&] { rp = p->FindChild(uint32_t(id)); }, [&] { ro = Slot<IntFn>(i, 0x00)(g_origNodes[i], id); });
            break;
        }
    }
    CHECK(rp == ro, "slot %d (%s): %d / %d", pick, g_nodeKinds[i]->name, ro, rp);
}

S_ControlMessage g_messageBefore;
void TestMessage()
{
    const size_t i = RandomNode();
    C_Control *p = g_portNodes[i];
    S_ControlMessage mp = RandomMessage(P(0.6));
    if (P(0.1))
        mp.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    if ((mp.id & CONTROL_NOTIFY_MASK) == 0 && (mp.id & CONTROL_MSG_MASK) == CONTROL_MSG_NOTIFY)
        mp.value = Pick(NOTIFY_RAISED);
    g_messageBefore = mp;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    const bool copy = P(0.3);
    Diff([&] { rp = copy ? p->HandleMessageCopy(mp) : p->HandleMessage(&mp); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, copy ? 0x74 : 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "HandleMessage (%s) %x: %d / %d", g_nodeKinds[i]->name, g_messageBefore.id, ro, rp);
    if (!copy) {
        const std::string why = MessageDiffers(mp, mo);
        CHECK(why.empty(), "the message after (%s): %s", g_nodeKinds[i]->name, why.c_str());
    }
}

// Create on the node's frame (its children's frames there: the button's three when it has three children).
void TestCreate()
{
    const size_t i = RandomNode();
    C_Control *p = g_portNodes[i];
    I3D_frame *frame = RandomFrame(), *root = RandomFrame();
    const int fi = FrameIndex(frame);
    if (std::string(g_nodeKinds[i]->name) == "button" && g_arena.numChildren[fi] == 3) {
        // (A frame with three children has them: a missing one is never set up by the original's data.)
        for (int k = 0; k < 3; ++k)
            if (g_arena.child[fi][k] < 0)
                g_arena.child[fi][k] = static_cast<int16_t>(I(0, 15));
    }
    // A description: none, or one without children (the factory's controls are a later part's).
    static S_ControlDesc *desc;
    delete desc;
    desc = new S_ControlDesc(I(0, 0x28), "d", U32());
    static uint8_t origDesc[0x40];
    std::memset(origDesc, 0, sizeof origDesc);
    OF<int>(origDesc, 0x0C) = desc->type;
    const bool none = P(0.2);
    bool rp = false, ro = false;
    Diff([&] { rp = p->Create(frame, root, none ? nullptr : desc); },
         [&] { ro = Slot<CreateFn>(i, 0x2C)(g_origNodes[i], frame, root, none ? nullptr : origDesc); });
    CHECK(rp == ro, "Create (%s): %d / %d", g_nodeKinds[i]->name, ro, rp);
}

// The destructor: a subtree deleted on both sides (the original's blocks copies its operator delete may free).
void TestDestructor()
{
    ToOrig();
    const size_t i = RandomNode();
    std::map<uint8_t *, uint8_t *> moved;
    std::vector<uint8_t *> stack{g_origNodes[i]};
    while (!stack.empty()) {
        uint8_t *n = stack.back();
        stack.pop_back();
        const size_t size = 0x100;
        auto *copy = static_cast<uint8_t *>(std::malloc(size));
        std::memcpy(copy, n, size);
        moved[n] = copy;
        const size_t kids = OVecBytes(n, O_CHILDREN) / 4;
        for (size_t k = 0; k < kids; ++k)
            stack.push_back(reinterpret_cast<uint8_t **>(OVecData(n, O_CHILDREN))[k]);
    }
    for (auto &m : moved) {
        uint8_t **kids = reinterpret_cast<uint8_t **>(OVecData(m.second, O_CHILDREN));
        const size_t n = OVecBytes(m.second, O_CHILDREN) / 4;
        for (size_t k = 0; k < n; ++k)
            if (moved.count(kids[k]))
                kids[k] = moved[kids[k]];
    }
    // (The copies named as their originals in the logs.)
    std::vector<uint8_t *> saved = g_origNodes;
    for (auto &m : moved)
        for (uint8_t *&o : g_origNodes)
            if (o == m.first)
                o = m.second;
    g_log.clear();
    Slot<IntFn>(i, 0x80)(g_origNodes[i], 1);
    const std::vector<std::string> origLog = g_log;
    g_origNodes = saved;
    // The port's.
    std::vector<C_Control *> gone, todo{g_portNodes[i]};
    while (!todo.empty()) {
        C_Control *n = todo.back();
        todo.pop_back();
        gone.push_back(n);
        for (C_Control *k : n->m_children)
            todo.push_back(k);
    }
    C_Control *root = g_portNodes[i];
    if (root->m_parent) {
        std::vector<C_Control *> &siblings = root->m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), root), siblings.end());
    }
    g_log.clear();
    delete root;
    CHECK(g_log == origLog, "destructor (%s): %s / %s", g_nodeKinds[i]->name, Joined(origLog).c_str(), Joined(g_log).c_str());
    for (C_Control *n : gone) {
        const size_t at = size_t(std::find(g_portNodes.begin(), g_portNodes.end(), n) - g_portNodes.begin());
        g_portNodes.erase(g_portNodes.begin() + std::ptrdiff_t(at));
        g_nodeKinds.erase(g_nodeKinds.begin() + std::ptrdiff_t(at));
    }
    g_origNodes.clear();
}

// Ticks: trees of the ticking kinds (the tooltip, the button, the group passing it to its focus).
void TestTick()
{
    RandomTree({0, 5, 8, 9});
    const size_t i = RandomNode();
    const int time = I(0, 100);
    if (P(0.5))
        *reinterpret_cast<int *>(g_arena.cursor + 0x40) = Pick({299, 300, 301, 499, 500, 501, -1, 0});
    Diff([&] { g_portNodes[i]->Tick(time); }, [&] { Slot<VoidIntFn>(i, 0x1C)(g_origNodes[i], time); });
}

void SetUp()
{
    SetUpKit();
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SMALL_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("small_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_SMALL_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_SMALL_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_SMALL_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots},
        {"Message", TestMessage},
        {"Create", TestCreate},
        {"Destructor", TestDestructor},
        {"Tick", TestTick},
    };
    for (const S_Case &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round) {
            RandomTree();
            c.run();
        }
    }
    std::printf("small_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
