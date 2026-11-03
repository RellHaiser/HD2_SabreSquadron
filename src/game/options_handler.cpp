// The options screen's messages (original 0x0061bf00..0x00620e90; part 9e-viii; 9e-x: its description;
// 9e-xi: the video, sound, game and multiplayer pages; 9e-xii: the controls and keys pages).
#include "game/options_handler.h"

#include "game/control.h"
#include "common/c_str.h"
#include "game/controls.h"
#include "game/cursor.h"
#include "game/game.h"
#include "game/items_module.h"
#include "game/key_bindings.h"
#include "game/menu_state.h"
#include "game/profile.h"
#include "game/screen.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

#include <cstring>

namespace {

// A screen's index: its id's top 6 bits.
constexpr int SCREEN_INDEX_SHIFT = 26;
constexpr uint32_t SCREEN_INDEX_MASK = 0x3F;

// The handler's messages (target 0).
enum : uint32_t {
    OPTIONS_MSG_SAVE = 0x200006F,
    OPTIONS_MSG_OPEN = 0x2000071,
    OPTIONS_MSG_LEAVE = 0x20000C4,
    OPTIONS_MSG_SHOW_UNBOUND = 0x20000C7,
};
// What leaving posts: the save and the menu's back, with bits 0x04001000.
constexpr uint32_t LEAVE_SAVE = 0x600106F, LEAVE_BACK = 0x6001003;
constexpr uint32_t TEXT_UNBOUND_KEY = 0x2530;

// The screen's pages and their controls (below the screen's index).
constexpr uint32_t PAGES[] = {0x600000, 0x700000, 0x800000, 0x900000, 0xA00000, 0xB00000};
constexpr uint32_t KEYS_PAGE = 0x900000;
constexpr uint32_t GAMMA = 0x604000;
constexpr uint32_t DETAIL_LEVEL = 0x605000;
constexpr uint32_t SOUND_VOLUME = 0x706000;
constexpr uint32_t MUSIC_VOLUME = 0x707000;
constexpr uint32_t INVERT_MOUSE = 0x804000;
constexpr uint32_t MOUSE_SENSITIVITY = 0x806000;
constexpr uint32_t KEY_GROUPS = 0x902000;               // the groups' choice
constexpr uint32_t KEY_GROUP_LIST = 0x903000;
constexpr uint32_t SUBTITLES = 0xA02000;
constexpr uint32_t CROSSHAIR = 0xA03000;                // (the "parental lock" control)
constexpr uint32_t VOICE_CHAT = 0xB04000;
constexpr uint32_t VOICE_VALUE = 0xB06000;
// The key bindings' groups' lists: KEYS_PAGE | group << 12, groups 4 .. 8.
constexpr int FIRST_KEY_GROUP = 4, KEY_GROUP_END = 9;
// IGraph's info flags: the gamma can be set (inferred).
constexpr uint32_t IGRAPH_GAMMA = 2;

// The screen's description: its controls' types, the texts and their styles.
constexpr int CONTROL_GROUP = 5, CONTROL_TEXT = 0x13, CONTROL_BUTTON = 0x14;
constexpr uint32_t BUTTON_STYLE = 0xD, VERSION_STYLE = 0x28;
constexpr uint32_t TEXT_VERSION = 0x7D6, TEXT_NO_NAME = 0x7E4;
// The third control (text_date01) gets the player's name, style 0xC.
constexpr uint32_t NAME_TEXT = 0x300000, NAME_STYLE = 0xC;
const char *const GROUP_FRAME = "GROptions";
// The pages' buttons: their frames (moved under the group's), texts and pages (back: leaving).
struct S_PageButton {
    const char *name;
    uint32_t textId;
    uint32_t page;
};
// The pages' controls' types and texts' styles.
constexpr int CONTROL_SLIDER = 8, CONTROL_LIST = 0x10, CONTROL_CHECK = 0x24;
constexpr uint32_t TITLE_STYLE = 0xB, CHECK_STYLE = 0xD;
constexpr uint32_t SLIDER_POSITIONS = 100;
// The detail switches' frames (renamed GD00..GD06) and texts; the detail levels' texts.
const char *const DETAIL_FRAMES[7] = {"nextdetail", "shadows", "mirrors", "morphing", "dynamic lightning",
                                      "light glow", "use high models"};
constexpr int DETAIL_MORPHING = 3, DETAIL_HIGH_MODELS = 6, DETAIL_CUSTOM = 3;
// The video page's controls by their index (after the texts and the gamma): the level list, GD00..
constexpr uint32_t DETAIL_LIST_INDEX = 5, DETAIL_SWITCHES = 7;
const uint32_t PAGES_ALL[] = {0x600000, 0x700000, 0x800000, 0x900000, 0xA00000, 0xB00000};
// The keys page: the layouts' buttons, the key lists' and the bindings' controls, their rows' spacing.
constexpr uint32_t DEFAULT_LAYOUT_BUTTON = 0x909000, LAYOUT_1_BUTTON = 0x90A000;
constexpr int CONTROL_KEY_LIST = 0xB, CONTROL_KEY = 0xD;
constexpr uint32_t KEY_LIST_ROWS = 9;
constexpr float KEY_ROW_STEP = -0.036f;
const char *const KEY_GROUPS_NAMES[] = {"GR_Movements", "GR_Weapons", "GR_Other", "GR_Selections", "GR_Tactical"};
// The "min / norm / max" texts' runs (styles 0x1B..0x1D, texts 0x7EE..0x7F0).
void AddLimits(S_ControlDesc *text)
{
    AddDescMessage(text, 0, CONTROL_MSG_ADD_TEXT_ID, 0x1B, 0x7EE);
    AddDescMessage(text, 0, CONTROL_MSG_ADD_TEXT_ID, 0x1C, 0x7EF);
    AddDescMessage(text, 0, CONTROL_MSG_ADD_TEXT_ID, 0x1D, 0x7F0);
}
void AddTitle(S_ControlDesc *group, const char *name, uint32_t textId)
{
    AddDescMessage(group->AddChild(CONTROL_TEXT, name), 0, CONTROL_MSG_SET_TEXT_ID, TITLE_STYLE, textId);
}
void AddCallback(S_ControlDesc *control, void *handler, ControlCallback callback)
{
    AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CHANGED, reinterpret_cast<uintptr_t>(handler),
                   reinterpret_cast<uintptr_t>(callback));
}
// A slider's position (the control's CONTROL_MSG_GET_SLIDER: a float in value2).
float SliderPosition(C_Control *control)
{
    S_ControlMessage message;
    message.id = CONTROL_MSG_GET_SLIDER;
    control->HandleMessage(&message);
    const uint32_t bits = static_cast<uint32_t>(message.value2);
    float position;
    std::memcpy(&position, &bits, sizeof position);
    return position;
}

const S_PageButton PAGE_BUTTONS[] = {
    {"bvideo setup", 0x965, 0x600000}, {"bsound setup", 0x966, 0x700000}, {"bcontrols", 0x967, 0x800000},
    {"bgame setup", 0x968, 0xA00000}, {"bmultiplayer setup", 0x969, 0xB00000}, {"bback", 0x96A, 0},
};

void Post(uint32_t target, uint32_t id, uint32_t value = 0)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    PostControlMessage(message);
}

} // namespace

C_OptionsHandler::~C_OptionsHandler()
{
    if (m_cursor) {
        m_cursor->Release();
        m_cursor = nullptr;
    }
}

bool C_OptionsHandler::HandleMessage(S_ControlMessage *message)
{
    C_PlayerProfile *player = g_gameProfile.m_player;
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE: {
        C_Screen *top = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
        if (!top || (top->GetId() >> SCREEN_INDEX_SHIFT & SCREEN_INDEX_MASK) != m_screen)
            return false;
        Post(0, OPTIONS_MSG_LEAVE);
        return true;
    }
    case OPTIONS_MSG_SAVE:
        player->SaveOptions();
        player->ApplyHardwareOption();
        player->GetKeyBindings()->ApplyChanged();
        return true;
    case OPTIONS_MSG_OPEN:
        if (m_fillOnOpen) {
            Fill();
            m_fillOnOpen = false;
        }
        Post(0, MENU_MSG_OPEN_SCREEN, m_screen);
        return true;
    case OPTIONS_MSG_LEAVE:
        if (player->GetKeyBindings()->FindUnbound() != -1) {
            m_menu->ShowMessageId(MESSAGE_BOX_OK, TEXT_UNBOUND_KEY, OPTIONS_MSG_SHOW_UNBOUND, OPTIONS_MSG_SHOW_UNBOUND);
            return true;
        }
        for (uint32_t page : PAGES)
            Post(Id(page), CONTROL_MSG_HIDE);
        Post(0, LEAVE_SAVE);
        Post(0, LEAVE_BACK);
        return true;
    case OPTIONS_MSG_SHOW_UNBOUND: {
        // (The original asks a copy of the bindings.)
        const C_KeyBindings &bindings = *player->GetKeyBindings();
        const int index = bindings.FindUnbound();
        int textId = 0, group = 0;
        bindings.GetTextAndGroup(index, &textId, &group);
        for (uint32_t page : PAGES)
            Post(Id(page), page == KEYS_PAGE ? CONTROL_MSG_SHOW : CONTROL_MSG_HIDE);
        Post(Id(KEY_GROUPS), CONTROL_MSG_SET_CHECKED, static_cast<uint32_t>(group));
        int position = 0;
        bindings.GetGroupPosition(index, &position);
        if (position > 0)
            Post(Id(KEYS_PAGE) | (static_cast<uint32_t>(group + FIRST_KEY_GROUP) & 0xFF) << 12, CONTROL_MSG_SELECT_INDEX,
                 static_cast<uint32_t>(position - 1));
        return true;
    }
    default:
        return false;
    }
}

void C_OptionsHandler::Fill()
{
    const auto *options = g_gameProfile.GetDetailSettings();
    const bool gamma = (GetIGraph()->GetInfo()->flags & IGRAPH_GAMMA) != 0;
    Post(Id(GAMMA), gamma ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE);
    Post(Id(GAMMA), CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(static_cast<int>(options->gamma)));
    Post(Id(SOUND_VOLUME), CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(static_cast<int>(options->soundVolume)));
    Post(Id(MUSIC_VOLUME), CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(static_cast<int>(options->musicVolume)));
    Post(Id(MOUSE_SENSITIVITY), CONTROL_MSG_SET_POSITION,
         static_cast<uint32_t>(static_cast<int>(options->mouseSensitivity)));
    Post(Id(DETAIL_LEVEL), CONTROL_MSG_SET_CHECKED, static_cast<uint32_t>(options->level));
    Post(Id(INVERT_MOUSE), CONTROL_MSG_SET_CHECKED_QUIET, options->invertMouse);
    Post(Id(SUBTITLES), CONTROL_MSG_SET_CHECKED_QUIET, options->subtitles);
    Post(Id(VOICE_CHAT), CONTROL_MSG_SET_CHECKED_QUIET, options->voiceChat);
    Post(Id(VOICE_VALUE), CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(static_cast<int>(options->voiceValue)));
    Post(Id(CROSSHAIR), CONTROL_MSG_SET_CHECKED_QUIET, options->crosshair);
    Post(Id(VOICE_VALUE), options->voiceChat ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE);
    RefreshKeyGroups();
}

bool C_OptionsHandler::BuildScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    I3D_frame *group = m_menu->AddFrame(C_str(GROUP_FRAME), FRAME_DUMMY, model, S_vector(0.0f, 0.0f, 0.0f));
    if (!group)
        return false;
    I3D_frame *buttons[6];
    for (int i = 0; i < 6; ++i)
        buttons[i] = model->FindChildFrame(PAGE_BUTTONS[i].name, FRAME_MASK_DUMMY);
    // The second "min / norm / max" text where the first is.
    I3D_frame *limits = model->FindChildFrame("text_min norm max", FRAME_MASK_VISUAL);
    I3D_frame *limits2 = model->FindChildFrame("text_min norm max02", FRAME_MASK_VISUAL);
    if (limits && limits2)
        limits2->SetPos(limits->GetWorldPos());
    for (I3D_frame *button : buttons) {
        if (!button)
            return false;
    }
    for (I3D_frame *button : buttons)
        button->LinkTo(group, 1);
    m_screen = static_cast<uint8_t>(desc->id >> 26 & 0x3F);
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, reinterpret_cast<uintptr_t>(this),
                     reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnShown));
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_screen"), 0, CONTROL_MSG_SET_TEXT_ID, 0xA, 0x961);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_date00"), 0, CONTROL_MSG_SET_TEXT_ID, 0xB, 0x962);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_date01"), 0, CONTROL_MSG_SET_TEXT_ID, 0xC, 0x963);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_profile00"), 0, CONTROL_MSG_SET_TEXT_ID, 0xB, 0x964);
    S_ControlDesc *options = desc->AddControl(CONTROL_GROUP, GROUP_FRAME);
    for (const S_PageButton &page : PAGE_BUTTONS) {
        S_ControlDesc *button = options->AddChild(CONTROL_BUTTON, page.name);
        AddDescMessage(button, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, page.textId);
        if (page.page)
            AddDescMessage(button, Id(page.page), CONTROL_NOTIFY_CLICK | CONTROL_MSG_SHOW, 0);
        else
            AddDescMessage(button, 0, CONTROL_NOTIFY_CLICK | OPTIONS_MSG_LEAVE, 0);
    }
    BuildVideoPage(desc, model);
    BuildSoundPage(desc, model);
    BuildControlsPage(desc, model);
    BuildGamePage(desc, model);
    BuildMultiplayerPage(desc, model);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_h&dversion"), 0, CONTROL_MSG_SET_TEXT_ID, VERSION_STYLE,
                   TEXT_VERSION);
    return true;
}

bool C_OptionsHandler::OnShown(void *handler, void *, C_Control *, uint32_t, uint32_t)
{
    const auto *self = static_cast<C_OptionsHandler *>(handler);
    const C_str name = g_gameProfile.m_player->GetName();
    S_ControlMessage message;
    message.target = self->Id(NAME_TEXT);
    message.value = NAME_STYLE;
    if (name.IsEmpty()) {
        message.id = CONTROL_MSG_SET_TEXT_ID;
        message.value2 = TEXT_NO_NAME;
    } else {
        message.id = CONTROL_MSG_SET_TEXT;
        message.text = name;
    }
    PostControlMessage(message);
    return true;
}

S_ControlDesc *C_OptionsHandler::AddPage(S_ScreenDesc *desc, const char *name, uint32_t page)
{
    S_ControlDesc *group = desc->AddControl(CONTROL_GROUP, name);
    AddDescMessage(group, 0, CONTROL_MSG_HIDE, 0);
    for (uint32_t other : PAGES_ALL) {
        if (other != page)
            AddDescMessage(group, Id(other), CONTROL_NOTIFY_SHOWN | CONTROL_MSG_HIDE, 0);
    }
    return group;
}

bool C_OptionsHandler::BuildVideoPage(S_ScreenDesc *desc, I3D_frame *model)
{
    for (int i = 0; i < 7; ++i) {
        if (I3D_frame *frame = model->FindChildFrame(DETAIL_FRAMES[i], FRAME_MASK_VISUAL))
            frame->SetName(C_fstr("GD%02d", i).c_str());
    }
    S_ControlDesc *page = AddPage(desc, "graphics setup", 0x600000);
    AddTitle(page, "text_graphics setup", 0x96B);
    AddTitle(page, "text_gama correction", 0x96C);
    AddLimits(page->AddChild(CONTROL_TEXT, "text_min norm max02"));
    S_ControlDesc *gamma = page->AddChild(CONTROL_SLIDER, "scrollbar03");
    AddDescMessage(gamma, 0, CONTROL_MSG_SET_COUNT, SLIDER_POSITIONS);
    AddCallback(gamma, this, &C_OptionsHandler::OnSlider);
    S_ControlDesc *levels = page->AddChild(CONTROL_LIST, "geometry detail");
    for (uint32_t textId = 0x96E; textId <= 0x971; ++textId)
        AddDescMessage(levels, 0, CONTROL_MSG_ADD_ENTRY, textId);
    AddCallback(levels, this, &C_OptionsHandler::OnDetail);
    S_ControlDesc *detail = nullptr;
    for (int i = 0; i < 7; ++i) {
        detail = page->AddChild(CONTROL_CHECK, C_fstr("GD%02d", i).c_str());
        AddDescMessage(detail, 0, CONTROL_MSG_SET_TEXT_ID, CHECK_STYLE, 0x972 + static_cast<uint32_t>(i));
        AddCallback(detail, this, &C_OptionsHandler::OnDetail);
        // The morphing not switched in a game.
        if (i == DETAIL_MORPHING && GetGame())
            AddDescMessage(detail, 0, CONTROL_MSG_DEACTIVATE, 0);
    }
    // (The high models' switch hidden.)
    AddDescMessage(detail, 0, CONTROL_MSG_HIDE, 0);
    AddDescMessage(page->AddChild(CONTROL_TEXT, "text_modifications"), 0, CONTROL_MSG_ADD_TEXT_ID, TITLE_STYLE, 0x97E);
    return true;
}

bool C_OptionsHandler::BuildControlsPage(S_ScreenDesc *desc, I3D_frame *model)
{
    S_ControlDesc *page = AddPage(desc, "keysscreen", 0x800000);
    AddTitle(page, "text_controls setup", 0x989);
    S_ControlDesc *keys = page->AddChild(CONTROL_BUTTON, "bcontrols01");
    AddDescMessage(keys, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, 0x98A);
    AddDescMessage(keys, Id(KEYS_PAGE), CONTROL_NOTIFY_CLICK | CONTROL_MSG_SHOW, 0);
    AddTitle(page, "text_mouse setup", 0x98B);
    S_ControlDesc *invert = page->AddChild(CONTROL_CHECK, "invert mouse");
    AddDescMessage(invert, 0, CONTROL_MSG_SET_TEXT_ID, CHECK_STYLE, 0x98C);
    AddCallback(invert, this, &C_OptionsHandler::OnCheck);
    AddTitle(page, "text_mouse sensitivity", 0x98D);
    S_ControlDesc *slider = page->AddChild(CONTROL_SLIDER, "scrollbar");
    AddDescMessage(slider, 0, CONTROL_MSG_SET_COUNT, SLIDER_POSITIONS);
    AddCallback(slider, this, &C_OptionsHandler::OnSlider);
    AddLimits(page->AddChild(CONTROL_TEXT, "text_minimal normal maximal"));
    // The joystick's, hidden.
    S_ControlDesc *joystick = page->AddChild(CONTROL_TEXT, "text_joystick setup");
    AddDescMessage(joystick, 0, CONTROL_MSG_SET_TEXT_ID, TITLE_STYLE, 0x98F);
    AddDescMessage(joystick, 0, CONTROL_MSG_HIDE, 0);
    const struct {
        int type;
        const char *name;
        uint32_t textId;
    } hidden[] = {{CONTROL_BUTTON, "bcalibrate", 0x990}, {CONTROL_CHECK, "force feedback", 0x991}, {CONTROL_CHECK, "lock fire", 0x992}};
    for (const auto &control : hidden) {
        S_ControlDesc *child = page->AddChild(control.type, control.name);
        AddDescMessage(child, 0, CONTROL_MSG_SET_TEXT_ID, CHECK_STYLE, control.textId);
        AddDescMessage(child, 0, CONTROL_MSG_HIDE, 0);
    }

    // The keys page (hidden; shown: the controls page hidden).
    S_ControlDesc *bindings = desc->AddControl(CONTROL_GROUP, "keysscreen01");
    AddDescMessage(bindings, 0, CONTROL_MSG_HIDE, 0);
    AddDescMessage(bindings, Id(0x800000), CONTROL_NOTIFY_SHOWN | CONTROL_MSG_HIDE, 0);
    AddTitle(bindings, "text_last save01", 0x993);
    S_ControlDesc *groups = bindings->AddChild(CONTROL_LIST, "controls");
    for (uint32_t textId = 0x994; textId <= 0x998; ++textId)
        AddDescMessage(groups, 0, CONTROL_MSG_ADD_ENTRY, textId);
    AddCallback(groups, this, &C_OptionsHandler::OnKeyGroup);
    AddDescMessage(bindings->AddChild(CONTROL_SLIDER, "scroll00"), 0, CONTROL_MSG_SET_VALUE, Id(KEYS_PAGE | FIRST_KEY_GROUP << 12));
    I3D_frame *input = model->FindChildFrame("hkinput", FRAME_MASK_DUMMY);
    I3D_frame *text = model->FindChildFrame("hktext", FRAME_MASK_VISUAL);
    if (!input || !text)
        return false;
    text->LinkTo(input, 1);
    for (int group = 0; group < 5; ++group)
        BuildKeyGroup(bindings, model, C_str(KEY_GROUPS_NAMES[group]), group);
    for (const auto &button : {std::make_pair("bdefault*", 0x999u), std::make_pair("bback01", 0x99Bu)}) {
        S_ControlDesc *layout = bindings->AddChild(CONTROL_BUTTON, button.first);
        AddDescMessage(layout, 0, CONTROL_MSG_SET_TEXT_ID, CHECK_STYLE, button.second);
        AddDescMessage(layout, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, reinterpret_cast<uintptr_t>(this),
                       reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnKeyLayout));
    }
    return true;
}

bool C_OptionsHandler::BuildKeyGroup(S_ControlDesc *page, I3D_frame *model, C_str name, int group)
{
    I3D_frame *dummy = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    I3D_frame *input = model->FindChildFrame("hkinput", FRAME_MASK_DUMMY);
    I3D_frame *screen = model->FindChildFrame("keysscreen01", FRAME_MASK_DUMMY);
    // (The dummy leaked, as the original.)
    if (!input || !dummy || !screen)
        return false;
    dummy->LinkTo(screen, 0);
    dummy->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    dummy->SetName(name.c_str());
    m_menu->GetModels().push_back(dummy);
    dummy->Update();
    input->LinkTo(dummy, 1);
    input->SetOn(true);
    input->Update();
    S_ControlDesc *list = page->AddChild(CONTROL_KEY_LIST, name.c_str());
    AddDescMessage(list, 0, CONTROL_MSG_SET_VISIBLE, KEY_LIST_ROWS);
    AddDescMessage(list, 0, CONTROL_MSG_SET_VALUE, Id(KEY_GROUP_LIST));
    const float step = -KEY_ROW_STEP;
    uint32_t stepBits;
    std::memcpy(&stepBits, &step, sizeof stepBits);
    AddDescMessage(list, 0, CONTROL_MSG_SET_DOWN_STEP, 0, stepBits);
    if (group != 0)
        AddDescMessage(list, 0, CONTROL_MSG_HIDE, 0);
    // (The original asks a copy of the bindings.)
    const C_KeyBindings &bindings = *g_gameProfile.m_player->GetKeyBindings();
    int row = 0;
    for (int i = 0; i < bindings.GetCount(); ++i) {
        int textId = 0, bindingGroup = 0;
        bindings.GetTextAndGroup(i, &textId, &bindingGroup);
        if (bindingGroup != group)
            continue;
        const C_str rowName = C_fstr("Control%2d", i);
        I3D_frame *copy = nullptr;
        CloneFrameTree(copy, input, nullptr);
        m_menu->GetFrames().push_back(copy);
        copy->SetName(rowName.c_str());
        const S_vector &pos = input->GetPos();
        copy->SetPosInline(S_vector(pos.x, pos.y, static_cast<float>(row) * KEY_ROW_STEP + pos.z));
        ++row;
        copy->LinkTo(dummy, 0);
        copy->SetOn(true);
        copy->Update();
        S_ControlDesc *binding = list->AddChild(CONTROL_KEY, rowName.c_str());
        AddDescMessage(binding, 0, CONTROL_MSG_SET_BINDING, static_cast<uint32_t>(i), static_cast<uintptr_t>(textId));
        AddCallback(binding, this, &C_OptionsHandler::OnKeyChanged);
    }
    if (group == 0)
        AddDescMessage(list, 0, CONTROL_MSG_REFRESH, 0);
    input->SetOn(false);
    input->Update();
    return true;
}

bool C_OptionsHandler::OnKeyGroup(void *handler, void *, C_Control *control, uint32_t, uint32_t)
{
    const auto *self = static_cast<C_OptionsHandler *>(handler);
    S_ControlMessage selected;
    selected.id = CONTROL_MSG_GET_SELECTED;
    control->HandleMessage(&selected);
    const int index = static_cast<int>(selected.value);
    if (index > -1) {
        for (uint32_t group = FIRST_KEY_GROUP; group < KEY_GROUP_END; ++group) {
            const uint32_t list = self->Id(KEYS_PAGE | group << 12);
            if (group == static_cast<uint32_t>(index) + FIRST_KEY_GROUP) {
                Post(list, CONTROL_MSG_SHOW);
                Post(self->Id(KEY_GROUP_LIST), CONTROL_MSG_SET_VALUE, list);
                Post(list, CONTROL_MSG_REFRESH);
            } else {
                Post(list, CONTROL_MSG_HIDE);
            }
        }
    }
    return true;
}

bool C_OptionsHandler::OnKeyChanged(void *handler, void *, C_Control *, uint32_t, uint32_t)
{
    return static_cast<C_OptionsHandler *>(handler)->RefreshKeyGroups();
}

bool C_OptionsHandler::OnKeyLayout(void *handler, void *, C_Control *control, uint32_t, uint32_t)
{
    if (!control)
        return false;
    auto *self = static_cast<C_OptionsHandler *>(handler);
    const uint32_t id = control->GetId();
    const int layout = id != self->Id(DEFAULT_LAYOUT_BUTTON) && id == self->Id(LAYOUT_1_BUTTON) ? 1 : 0;
    g_gameProfile.m_player->SetKeyLayout(layout);
    self->RefreshKeyGroups();
    return true;
}

bool C_OptionsHandler::BuildSoundPage(S_ScreenDesc *desc, I3D_frame *)
{
    S_ControlDesc *page = AddPage(desc, "sound setup", 0x700000);
    AddTitle(page, "text_sound setup", 0x979);
    AddTitle(page, "text_sound volume", 0x97A);
    AddLimits(page->AddChild(CONTROL_TEXT, "text_min norm max"));
    AddTitle(page, "text_music volume", 0x97C);
    S_ControlDesc *limits = page->AddChild(CONTROL_TEXT, "text_min norm max01");
    AddDescMessage(limits, 0, CONTROL_MSG_SET_MARGIN, 1);
    AddLimits(limits);
    for (const char *name : {"scrollbar01", "scrollbar02"}) {
        S_ControlDesc *slider = page->AddChild(CONTROL_SLIDER, name);
        AddDescMessage(slider, 0, CONTROL_MSG_SET_COUNT, SLIDER_POSITIONS);
        AddCallback(slider, this, &C_OptionsHandler::OnSlider);
    }
    return true;
}

bool C_OptionsHandler::BuildGamePage(S_ScreenDesc *desc, I3D_frame *)
{
    S_ControlDesc *page = AddPage(desc, "gameoptions", 0xA00000);
    AddTitle(page, "text_game options", 0x99D);
    S_ControlDesc *subtitles = page->AddChild(CONTROL_CHECK, "subtitles");
    AddDescMessage(subtitles, 0, CONTROL_MSG_SET_TEXT_ID, CHECK_STYLE, 0x99E);
    AddCallback(subtitles, this, &C_OptionsHandler::OnCheck);
    // (The crosshair's: the "parental lock" control with a text of its own.)
    S_ControlDesc *crosshair = page->AddChild(CONTROL_CHECK, "parental lock");
    AddDescMessage(crosshair, 0, CONTROL_MSG_SET_TEXT_ID, CHECK_STYLE, 0x4EF4);
    AddCallback(crosshair, this, &C_OptionsHandler::OnCheck);
    if (GetGame())
        AddDescMessage(crosshair, 0, CONTROL_MSG_DEACTIVATE, 0);
    return true;
}

bool C_OptionsHandler::BuildMultiplayerPage(S_ScreenDesc *desc, I3D_frame *)
{
    S_ControlDesc *page = AddPage(desc, "network settings", 0xB00000);
    // (Quirk kept: the connection's controls added, the group hidden again for each.)
    page->AddChild(CONTROL_TEXT, "text_connection rate");
    AddDescMessage(page, 0, CONTROL_MSG_HIDE, 0);
    page->AddChild(CONTROL_LIST, "connection");
    AddDescMessage(page, 0, CONTROL_MSG_HIDE, 0);
    AddTitle(page, "text_voice-chat settings", 0x981);
    S_ControlDesc *voice = page->AddChild(CONTROL_CHECK, "enable voice-chat");
    AddDescMessage(voice, 0, CONTROL_MSG_SET_TEXT_ID, CHECK_STYLE, 0x97F);
    AddCallback(voice, this, &C_OptionsHandler::OnVoiceChat);
    AddTitle(page, "text_voice-chat volume", 0x980);
    S_ControlDesc *slider = page->AddChild(CONTROL_SLIDER, "scrollbar04");
    AddDescMessage(slider, 0, CONTROL_MSG_SET_COUNT, SLIDER_POSITIONS);
    AddCallback(slider, this, &C_OptionsHandler::OnSlider);
    S_ControlDesc *limits = page->AddChild(CONTROL_TEXT, "text_min norm max020");
    AddDescMessage(limits, 0, CONTROL_MSG_SET_MARGIN, 1);
    AddLimits(limits);
    AddTitle(page, "text_network setup", 0x9A9);
    return true;
}

bool C_OptionsHandler::OnSlider(void *handler, void *, C_Control *control, uint32_t, uint32_t)
{
    if (!control)
        return false;
    const auto *self = static_cast<C_OptionsHandler *>(handler);
    float position = SliderPosition(control);
    C_PlayerProfile *player = g_gameProfile.m_player;
    auto *options = player->GetOptions();
    const uint32_t id = control->GetId();
    if (id == self->Id(GAMMA)) {
        options->gamma = position;
        player->ApplyGamma();
    } else if (id == self->Id(SOUND_VOLUME)) {
        options->soundVolume = position;
        player->ApplyVolume();
        self->m_menu->PlayClick();
    } else if (id == self->Id(MUSIC_VOLUME)) {
        // (Not below 1: off; NaN kept.)
        if (position < 1.0f)
            position = 0.0f;
        options->musicVolume = position;
        player->ApplyMusicVolume();
    } else if (id == self->Id(MOUSE_SENSITIVITY)) {
        options->mouseSensitivity = position;
        player->ApplyMouse();
    } else if (id == self->Id(VOICE_VALUE)) {
        options->voiceValue = position;
        player->ApplyVoiceValue();
    } else {
        return false;
    }
    return true;
}

bool C_OptionsHandler::OnDetail(void *handler, void *, C_Control *control, uint32_t, uint32_t value)
{
    if (!control)
        return false;
    const auto *self = static_cast<C_OptionsHandler *>(handler);
    auto *options = g_gameProfile.GetDetailSettings();
    const uint32_t index = control->GetId() >> 12 & 0xFF;
    if (index == DETAIL_LIST_INDEX) {
        if (value < 4)
            options->level = static_cast<int>(value);
        for (uint32_t i = 0; i < 7; ++i)
            Post(self->Id(0x600000 | (DETAIL_SWITCHES - 1 + i) << 12), CONTROL_MSG_SET_CHECKED_QUIET,
                 options->IsEnabled(static_cast<int>(i)));
        // The switches settable at the custom level only.
        const uint32_t activity = options->level == DETAIL_CUSTOM ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE;
        for (uint32_t i = 0; i < 7; ++i)
            Post(self->Id(0x600000 | (DETAIL_SWITCHES - 1 + i) << 12), activity);
        g_gameProfile.m_player->ApplyHardwareOption();
    } else if (index > DETAIL_LIST_INDEX && index < DETAIL_LIST_INDEX + 8) {
        options->SetSwitch(static_cast<int>(index - DETAIL_LIST_INDEX - 1), static_cast<uint8_t>(value) != 0);
    }
    return true;
}

bool C_OptionsHandler::OnCheck(void *handler, void *, C_Control *control, uint32_t, uint32_t value)
{
    if (!control)
        return false;
    const auto *self = static_cast<C_OptionsHandler *>(handler);
    C_PlayerProfile *player = g_gameProfile.m_player;
    const uint32_t id = control->GetId();
    if (id == self->Id(INVERT_MOUSE)) {
        player->GetOptions()->invertMouse = static_cast<uint8_t>(value) != 0;
        player->ApplyMouse();
    } else if (id == self->Id(SUBTITLES)) {
        player->GetOptions()->subtitles = static_cast<uint8_t>(value) != 0;
    } else if (id == self->Id(CROSSHAIR)) {
        player->GetOptions()->crosshair = static_cast<uint8_t>(value) != 0;
    } else {
        return false;
    }
    return true;
}

bool C_OptionsHandler::OnVoiceChat(void *handler, void *, C_Control *, uint32_t, uint32_t value)
{
    const auto *self = static_cast<C_OptionsHandler *>(handler);
    // (The switch the value's low byte; its volume by the whole value.)
    g_gameProfile.GetDetailSettings()->voiceChat = static_cast<uint8_t>(value) != 0;
    Post(self->Id(VOICE_VALUE), value ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE);
    return true;
}

bool C_OptionsHandler::RefreshKeyGroups()
{
    C_Control *list = m_menu->FindControl(Id(KEY_GROUP_LIST));
    if (!list)
        return false;
    S_ControlMessage value;
    value.id = CONTROL_MSG_GET_VALUE;
    list->HandleMessage(&value);
    const uint32_t chosen = value.value >> 12 & 0xFF;
    for (uint32_t group = FIRST_KEY_GROUP; group < KEY_GROUP_END; ++group) {
        if (group != chosen)
            Post(Id(KEYS_PAGE | group << 12), CONTROL_MSG_REFRESH);
    }
    Post(Id(KEYS_PAGE | chosen << 12), CONTROL_MSG_REFRESH);
    return true;
}
