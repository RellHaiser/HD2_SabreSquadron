// The options screen's messages (original 0x0061bf00..0x00620e90; part 9e-viii).
#include "game/options_handler.h"

#include "game/control.h"
#include "game/controls.h"
#include "game/cursor.h"
#include "game/key_bindings.h"
#include "game/menu_state.h"
#include "game/profile.h"
#include "game/screen.h"
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
// What leaving posts: the save and the menu's back, with bit 0x04000000.
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
