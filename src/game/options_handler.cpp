// The options screen's messages (original 0x0061bf00..0x00620e90; part 9e-viii; 9e-x: its description).
#include "game/options_handler.h"

#include "game/control.h"
#include "common/c_str.h"
#include "game/controls.h"
#include "game/cursor.h"
#include "game/key_bindings.h"
#include "game/menu_state.h"
#include "game/profile.h"
#include "game/screen.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

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
constexpr uint32_t OPTION_A02 = 0xA02000;               // (C_PlayerOptions::unknown3A)
constexpr uint32_t CROSSHAIR = 0xA03000;
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
    Post(Id(OPTION_A02), CONTROL_MSG_SET_CHECKED_QUIET, options->unknown3A);
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
