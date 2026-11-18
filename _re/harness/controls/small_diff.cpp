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

#include "controls_tree.inc"

// The button's frame with three children has them (a missing one is never set up by the original's data).
void ButtonFrames(size_t node, int fi)
{
    if (std::string(g_nodeKinds[node]->name) == "button" && g_arena.numChildren[fi] == 3)
        for (int k = 0; k < 3; ++k)
            if (g_arena.child[fi][k] < 0)
                g_arena.child[fi][k] = static_cast<int16_t>(I(0, 15));
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_codes = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE, CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_INDEX,
               CONTROL_MSG_GET_INDEX, CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE, CONTROL_MSG_REFRESH, CONTROL_MSG_SET_TEXT,
               CONTROL_MSG_SET_TEXT_ID, CONTROL_MSG_ADD_TEXT, CONTROL_MSG_ADD_TEXT_ID, CONTROL_MSG_SET_RUN_TEXT, CONTROL_MSG_REMOVE_RUN,
               CONTROL_MSG_CLEAR_TEXT, CONTROL_MSG_FACE_FRONT, CONTROL_MSG_SET_COLOUR, CONTROL_MSG_CLEAR, CONTROL_MSG_GET_RUNS,
               CONTROL_MSG_SET_MARGIN, CONTROL_MSG_WHITE_TEXT, CONTROL_MSG_RESTORE_TEXTURE, CONTROL_MSG_LOAD_TEXTURE,
               CONTROL_MSG_FIND_FACE_GROUP, CONTROL_MSG_ADD_FRAME, CONTROL_MSG_SELECT_FRAME, CONTROL_MSG_HIDE_FRAME,
               CONTROL_MSG_IGNORED_25, 0x1000000, 0x2000001, 0};
    // (The picture's SET_TEXTURE shows or hides it: loud too.)
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_SET_TEXTURE};
    g_tickKinds = {0, 5, 8, 9};
    g_beforeCreate = ButtonFrames;
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
