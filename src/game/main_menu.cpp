// The main menu (original 0x00611200 .. 0x0063c740; part 9h). Part 9h-iii: the class, starting, stopping, the
// update, the messages, the building of the screens (0x006264c0 .. 0x00627690, 0x00636400, 0x0062f740,
// 0x00622af0, 0x006c1970 .. 0x006c1a90).
#include "game/main_menu.h"

#include "common/os_files.h"
#include "game/control.h"
#include "game/controls.h"
#include "game/game_app.h"
#include "game/mp_game_setup.h"
#include "game/options_handler.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/text_style.h"
#include "ls3d/i3d.h"

int g_gameInfoStyles[GAME_INFO_STYLES] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

namespace {

constexpr int SCREEN_INDEX_SHIFT = 26;
constexpr uint32_t SCREEN_INDEX_MASK = 0x3F;
constexpr uint32_t SCREEN_KIND_MASK = 0xFC000000;

// The main screen's date (its style).
constexpr uint32_t DATE_TEXT = 0x4300000;
constexpr uint32_t DATE_STYLE = 0xC;
// The quit question's own ids for dropping the kept quit and closing it (its buttons' ids).
constexpr uint32_t QUIT_DROP = MENU_MSG_DEFERRED | 0x4000000 | 0x1000 | MENU_DEFERRED_DROP;
constexpr uint32_t QUIT_CLOSE = 0x4000000 | 0x1000 | MENU_MSG_CLOSE_POPUP;
// Where the quit question's model sits.
const S_vector QUIT_MODEL_POS(0.5f, -0.375f, -0.5f);

void Post(uint32_t id, uint32_t value)
{
    S_ControlMessage message;
    message.id = id;
    message.value = value;
    PostControlMessage(message);
}

// The date on the main screen (the original formats the date, as C_fstr's format).
void PostDate(const char *date)
{
    S_ControlMessage message;
    message.target = DATE_TEXT;
    message.id = CONTROL_MSG_SET_TEXT;
    message.value = DATE_STYLE;
    message.text = C_str(date ? date : "");
    PostControlMessage(message);
}

} // namespace

bool C_MainMenu::OnStart(void *)
{
    // (Dropped: the copy protection's check value written first.)
    InitializeCriticalSection(&m_foundLock);
    DrainFoundServers();
    m_mpSettings = new C_MpMenuSettings();
    m_options = new C_OptionsHandler(this);
    const char *date = nullptr;
    GetDateString(&date, false);
    PostDate(date);
    return true;
}

void C_MainMenu::OnStop()
{
    DrainFoundServers();
    delete m_mpSettings;
    m_mpSettings = nullptr;
    delete m_options;
    m_options = nullptr;
    // (0x0062f740.)
    for (int &style : g_gameInfoStyles) {
        ReleaseTextStyle(style);
        style = -1;
    }
    DeleteCriticalSection(&m_foundLock);
}

void C_MainMenu::OnUpdate(int)
{
    const char *date = nullptr;
    if (GetDateString(&date, false))
        PostDate(date);
}

void C_MainMenu::DrainFoundServers()
{
    EnterCriticalSection(&m_foundLock);
    while (!m_foundServers.empty()) {
        S_ServerEntry *server = m_foundServers.back();
        if (!m_mpSettings->IsInFirstList(server) && !m_mpSettings->IsInSecondList(server))
            delete server;
        m_foundServers.pop_back();
    }
    LeaveCriticalSection(&m_foundLock);
}

void C_MainMenu::OnMessage(S_ControlMessage *message)
{
    if (OnProfileMessage(message) || OnNetworkMessage(message) || OnMissionsMessage(message) ||
        m_options->HandleMessage(message) || OnConnectMessage(message))
        return;

    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE: {
        const uint32_t top = g_menuScreens.empty() ? 0 : g_menuScreens.back()->GetId() >> SCREEN_INDEX_SHIFT &
                                                         SCREEN_INDEX_MASK;
        if (top == MAIN_SCREEN_PROFILE) {
            SelectProfile(this, nullptr, nullptr, 0, 0);
            Post(MENU_MSG_BACK, 0);
        } else if (top == MAIN_SCREEN_ADD_SERVER || top == MAIN_SCREEN_PASSWORD) {
            Post(MENU_MSG_DEFERRED | MENU_DEFERRED_DROP, 1);
            Post(MENU_MSG_CLOSE_POPUP, 0);
        } else if (top == MAIN_SCREEN_QUIT) {
            Post(QUIT_DROP, 1);
            Post(QUIT_CLOSE, 0);
        } else {
            Post(MENU_MSG_BACK, 0);
        }
        return;
    }
    case MAIN_MSG_PLAY_MOVIE:
        m_state = GAME_STATE_PLAY_AVI;
        return;
    case MAIN_MSG_LOAD_GAME:
        if (m_saveName.IsEmpty()) {
            GetGameApp()->m_pendingCommand = APP_COMMAND_LOAD_FROM_MENU;
            return;
        }
    {
        C_str path = g_gameProfile.GetProfileDirectory();
        path += m_saveName;
        GetGameApp()->RequestLoad(path);
        return;
    }
    default:
        return;
    }
}

bool C_MainMenu::SelectProfile(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    if (self->m_profileName.IsEmpty()) {
        g_gameProfile.SelectPlayer(nullptr);
        return true;
    }
    g_gameProfile.SelectPlayer(self->m_profileName.c_str());
    self->m_options->RequestFill();
    g_gameProfile.ResetGame();
    return true;
}

bool C_MainMenu::BuildScreens()
{
    S_MenuDesc *menu = Describe();
    for (S_ScreenDesc *desc : menu->screens) {
        I3D_frame *model = LoadModel(desc->name.c_str(), desc->file.c_str());
        if (!model)
            continue;
        switch ((desc->id & SCREEN_KIND_MASK) >> SCREEN_INDEX_SHIFT) {
        case MAIN_SCREEN_PROFILE: DescribeProfileScreen(desc, model); break;
        case MAIN_SCREEN_QUIT: model->SetPos(QUIT_MODEL_POS); break;
        case MAIN_SCREEN_MISSIONS: DescribeMissionsScreen(desc, model); break;
        case MAIN_SCREEN_OPTIONS: m_options->BuildScreen(desc, model); break;
        case MAIN_SCREEN_CINEMATICS: DescribeCinematicsScreen(desc, model); break;
        case MAIN_SCREEN_BROWSER: DescribeBrowserScreen(desc, model); break;
        case MAIN_SCREEN_CREATE_SERVER: DescribeCreateServerScreen(desc, model); break;
        case MAIN_SCREEN_PASSWORD: DescribePasswordScreen(desc, model); break;
        case MAIN_SCREEN_FILTERS: DescribeFiltersScreen(desc, model); break;
        case MAIN_SCREEN_GAME_INFO: DescribeGameInfoScreen(desc, model); break;
        case MAIN_SCREEN_CONNECT: DescribeConnectScreen(desc, model); break;
        case MAIN_SCREEN_ADD_SERVER: DescribeAddServerScreen(desc, model); break;
        default: break;
        }
        auto *screen = new C_Screen(m_scene, model);
        // (Quirk kept: a screen not built is leaked.)
        if (screen->Build(desc)) {
            screen->HandleMessages(desc->messages);
            m_screens.push_back(screen);
        }
    }
    delete menu;
    S_ControlMessage open;
    open.id = MENU_MSG_OPEN_SCREEN;
    open.value = MAIN_SCREEN_MAIN;
    HandleMessage(&open);
    return true;
}

// ---- the screens' descriptions (part 9h-iv) ---------------------------------------------------------------

namespace {

constexpr int CONTROL_FRAME_SWITCH = 0xC, CONTROL_SCROLLED_TEXT = 0xF, CONTROL_TEXT = 0x13, CONTROL_BUTTON = 0x14,
              CONTROL_EDIT = 0x1B;
// The texts' styles.
constexpr uint32_t STYLE_TITLE = 10, STYLE_LABEL = 0xB, STYLE_VALUE = 0xC, STYLE_BUTTON = 0xD, STYLE_VERSION = 0x28;
constexpr uint32_t TEXT_VERSION = 0x7D6, TEXT_EMPTY = 0x7D2;
// What the menu's buttons post: a menu message with the bits 0x04001000 (the main screen's; the quit
// question's own drop and post).
constexpr uint32_t FROM_SCREEN = 0x4001000;
constexpr uint32_t MAIN_MSG_LAN_GAME = 0x2000099, MAIN_MSG_INTERNET_GAME = 0x200009A, MAIN_MSG_OPEN_OPTIONS = 0x2000071;
// The profile's name: the characters a file name may not hold, its longest (but in Japanese).
const char *const FORBIDDEN_NAME_CHARS = "./\\*?|<>:\"";
constexpr uint32_t NAME_MAX_LENGTH = 0x11;
// The single-player screen's side switch's frames (in the original's order).
const char *const SIDE_FRAMES[] = {"ss1", "ss2", "ss3", "ss4", "ss5", "ss6", "ss8", "ss7"};

S_ControlDesc *AddText(S_ScreenDesc *desc, const char *name, uint32_t style, uint32_t text)
{
    S_ControlDesc *control = desc->AddControl(CONTROL_TEXT, name);
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, style, text);
    return control;
}

S_ControlDesc *AddButton(S_ScreenDesc *desc, const char *name, uint32_t text)
{
    S_ControlDesc *control = desc->AddControl(CONTROL_BUTTON, name);
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON, text);
    return control;
}

// A button posting a menu message (value).
void AddMenuButton(S_ScreenDesc *desc, const char *name, uint32_t text, uint32_t message, uint32_t value)
{
    AddDescMessage(AddButton(desc, name, text), 0, FROM_SCREEN | message, value);
}

uintptr_t Callback(ControlCallback function) { return reinterpret_cast<uintptr_t>(function); }

} // namespace

S_MenuDesc *C_MainMenu::Describe()
{
    const auto self = reinterpret_cast<uintptr_t>(this);
    auto *menu = new S_MenuDesc();

    // The main screen.
    S_ScreenDesc *desc = menu->AddScreen("MAIN MENU", "main_menu");
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, self, Callback(&OnMainShown));
    AddText(desc, "text_screen", STYLE_TITLE, 0x835);
    AddText(desc, "text_date00", STYLE_LABEL, 0x836);
    AddText(desc, "text_date01", STYLE_VALUE, TEXT_EMPTY);
    AddText(desc, "text_profile00", STYLE_LABEL, 0x837);
    AddText(desc, "text_profile01", STYLE_VALUE, TEXT_EMPTY);
    AddText(desc, "text_last save00", STYLE_LABEL, 0x838);
    AddText(desc, "text_last save01", STYLE_VALUE, TEXT_EMPTY);
    AddMenuButton(desc, "bresume game", 0x839, MAIN_MSG_LOAD_GAME, 0);
    AddMenuButton(desc, "bchange profile", 0x83A, MENU_MSG_OPEN_SCREEN, MAIN_SCREEN_PROFILE);
    AddMenuButton(desc, "bsingleplayer", 0x83B, MENU_MSG_OPEN_SCREEN, MAIN_SCREEN_SINGLEPLAYER);
    AddMenuButton(desc, "bmultiplayerlan", 0x843, MAIN_MSG_LAN_GAME, 0);
    AddMenuButton(desc, "bmultiplayernet", 0x844, MAIN_MSG_INTERNET_GAME, 0);
    AddMenuButton(desc, "bcinematics", 0x83D, MENU_MSG_OPEN_SCREEN, MAIN_SCREEN_CINEMATICS);
    AddMenuButton(desc, "boptions", 0x83E, MAIN_MSG_OPEN_OPTIONS, 0);
    AddMenuButton(desc, "bcredits", 0x83F, MAIN_MSG_PLAY_MOVIE, 0);
    AddMenuButton(desc, "bexit", 0x840, MENU_MSG_BACK, MAIN_SCREEN_MAIN << SCREEN_INDEX_SHIFT);
    AddText(desc, "text_h&dversion", STYLE_VERSION, TEXT_VERSION);

    // The profile screen.
    desc = menu->AddScreen("CHANGE PROFIL", "change_profile");
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, self, Callback(&OnProfileShown));
    S_ControlDesc *control = desc->AddControl(CONTROL_EDIT, "bcurrent profile");
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON, TEXT_EMPTY);
    AddDescMessage(control, 0, CONTROL_MSG_SET_FORBIDDEN, 0, reinterpret_cast<uintptr_t>(FORBIDDEN_NAME_CHARS));
    if (!g_gameProfile.IsLanguage("japan"))
        AddDescMessage(control, 0, CONTROL_MSG_SET_MAX_LENGTH, NAME_MAX_LENGTH);
    AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_RETURN, self, Callback(&OnProfileButton));
    AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_ESCAPE, self, Callback(&OnProfileButton));
    control = desc->AddControl(CONTROL_SCROLLED_TEXT, "LB_Profile*");
    AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&OnProfileList));
    AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_13, self, Callback(&OnProfileList));
    const struct {
        const char *name;
        uint32_t text;
    } difficulties[] = {{"beasy-*", 0x86D}, {"bnormal-*", 0x86E}, {"bhard-*", 0x86F}, {"bvery hard-*", 0x870}};
    for (const auto &difficulty : difficulties)
        AddDescMessage(AddButton(desc, difficulty.name, difficulty.text), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK,
                       self, Callback(&OnDifficulty));
    AddDescMessage(AddButton(desc, "bcreate profile", 0x868), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,
                   Callback(&OnProfileButton));
    AddDescMessage(AddButton(desc, "brename profile", 0x86A), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,
                   Callback(&OnProfileButton));
    AddDescMessage(AddButton(desc, "Bdelete profile", 0x86B), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,
                   Callback(&OnProfileButton));
    AddMenuButton(desc, "bback", 0x86C, MENU_MSG_BACK, 0);
    AddText(desc, "text_h&dversion", STYLE_VERSION, TEXT_VERSION);
    AddText(desc, "text_current profile and save", STYLE_LABEL, 0x88F);
    desc->AddControl(CONTROL_TEXT, "text_date and time");
    AddText(desc, "text_list of profiles", STYLE_LABEL, 0x890);
    desc->AddControl(CONTROL_TEXT, "text01");
    AddText(desc, "text_screen", STYLE_TITLE, 0x867);
    AddText(desc, "text_difficulty", STYLE_LABEL, 0x891);

    // The single-player screen.
    desc = menu->AddScreen("SINGLEPLAYER", "singleplayer");
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, self, Callback(&OnSingleplayerShown));
    AddText(desc, "text_screen", STYLE_TITLE, 0x899);
    control = desc->AddControl(CONTROL_FRAME_SWITCH, "single side");
    for (const char *frame : SIDE_FRAMES)
        AddDescMessage(control, 0, CONTROL_MSG_ADD_FRAME, 0, reinterpret_cast<uintptr_t>(frame));
    desc->AddControl(CONTROL_TEXT, "text00");
    const struct {
        const char *name;
        uint32_t text;
    } modes[] = {{"bcampaign", 0x89A}, {"blone wolf", 0x89B}, {"carnage", 0x89C}, {"bsingle mission", 0x89D},
                 {"bsingle mission- carnage", 0x89E}, {"bcampaign01", 0x89A}, {"blone wolf01", 0x89B},
                 {"carnage01", 0x89C}, {"bback", 0x8A0}};
    for (const auto &mode : modes) {
        control = AddButton(desc, mode.name, mode.text);
        AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_ENTER, self, Callback(&OnSingleplayerButton));
        AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&OnSingleplayerButton));
    }
    AddText(desc, "text_h&dversion", STYLE_VERSION, TEXT_VERSION);
    AddText(desc, "text_HD2original", STYLE_BUTTON, 0x4FB0);
    AddText(desc, "text_HD2datadisk", STYLE_BUTTON, 0x4FB1);

    // The quit question.
    desc = menu->AddScreen("CONFIRM", "confirm");
    AddText(desc, "text", STYLE_VALUE, 0x7D4);
    control = AddButton(desc, "byes*", 0x7D1);
    AddDescMessage(control, 0, MENU_MSG_DEFERRED | FROM_SCREEN | MENU_DEFERRED_POST, 0);
    AddDescMessage(control, 0, FROM_SCREEN | MENU_MSG_CLOSE_POPUP, 0);
    control = AddButton(desc, "bno*", 0x7D0);
    AddDescMessage(control, 0, MENU_MSG_DEFERRED | FROM_SCREEN | MENU_DEFERRED_DROP, 1);
    AddDescMessage(control, 0, FROM_SCREEN | MENU_MSG_CLOSE_POPUP, 0);
    AddDescMessage(desc->AddControl(CONTROL_BUTTON, "bok"), 0, CONTROL_MSG_HIDE, 0);

    // The others (described by their kinds' functions).
    menu->AddScreen("SINGLE MISSION 2", "single mission 2");
    menu->AddScreen("OPTIONS", "options");
    menu->AddScreen("SINGLE MISSION 2", "single mission 2");
    menu->AddScreen("multiplayer menu", "multiplayer menu");
    menu->AddScreen("create server", "create server");
    menu->AddScreen("PASSWORD", "password");
    menu->AddScreen("Server Filters", "server filters");
    menu->AddScreen("Game info", "gameinfo");
    menu->AddScreen("MP Connect console", "confirm");
    menu->AddScreen("ADD SERVER", "password");
    return menu;
}

// ---- the main screen shown (part 9h-iv) -------------------------------------------------------------------

namespace {

// The main screen's controls (screen 1: the control's index in bits 20-25).
constexpr uint32_t MAIN_PROFILE_TEXT = 0x4500000, MAIN_SAVE_TEXT = 0x4700000, MAIN_RESUME = 0x4800000,
                   MAIN_CHANGE_PROFILE = 0x4900000, MAIN_FIRST_MODE = 0x4A00000, MAIN_LAST_MODE = 0x4F00000;
constexpr uint32_t MAIN_CONTROL_STEP = 0x100000;
constexpr uint32_t TEXT_NO_PROFILE = 0x7E4, TEXT_NO_SAVE = 0x7E5, TEXT_CREATE_PROFILE = 0x845,
                   TEXT_CHANGE_PROFILE = 0x83A;
constexpr uint32_t STYLE_SAVE_LINE = 7;

void PostTo(uint32_t target, uint32_t id, uint32_t value, uintptr_t value2 = 0)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    PostControlMessage(message);
}

void PostText(uint32_t target, uint32_t id, uint32_t style, const C_str &text)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = style;
    message.text = text;
    PostControlMessage(message);
}

} // namespace

bool C_MainMenu::OnMainShown(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    self->m_saveName = C_str("");
    g_netGame.Close(true);
    C_PlayerProfile *player = g_gameProfile.GetPlayerProfile();
    self->m_profileName = player->GetProfileName();
    if (self->m_profileName.IsEmpty()) {
        // No profile: "none", no save; resume off, "create a profile", the modes off.
        PostTo(MAIN_PROFILE_TEXT, CONTROL_MSG_SET_TEXT_ID, STYLE_VALUE, TEXT_NO_PROFILE);
        PostTo(MAIN_SAVE_TEXT, CONTROL_MSG_SET_TEXT_ID, STYLE_VALUE, TEXT_NO_SAVE);
        PostTo(MAIN_RESUME, CONTROL_MSG_DEACTIVATE, 0);
        PostTo(MAIN_CHANGE_PROFILE, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON, TEXT_CREATE_PROFILE);
        for (uint32_t id = MAIN_FIRST_MODE; id <= MAIN_LAST_MODE; id += MAIN_CONTROL_STEP)
            PostTo(id, CONTROL_MSG_DEACTIVATE, 0);
        return true;
    }
    // (The original posts the name twice.)
    PostText(MAIN_PROFILE_TEXT, CONTROL_MSG_SET_TEXT, STYLE_VALUE, self->m_profileName);
    PostText(MAIN_PROFILE_TEXT, CONTROL_MSG_SET_TEXT, STYLE_VALUE, self->m_profileName);
    C_str mode, mission, date;
    if (player->GetLastSaveText(&mode, &mission, &date)) {
        PostTo(MAIN_SAVE_TEXT, CONTROL_MSG_CLEAR, 0);
        PostText(MAIN_SAVE_TEXT, CONTROL_MSG_ADD_TEXT, STYLE_SAVE_LINE, mode);
        PostText(MAIN_SAVE_TEXT, CONTROL_MSG_ADD_TEXT, STYLE_SAVE_LINE, mission);
        PostText(MAIN_SAVE_TEXT, CONTROL_MSG_ADD_TEXT, STYLE_SAVE_LINE, date);
        PostTo(MAIN_RESUME, CONTROL_MSG_ACTIVATE, 0);
    } else {
        PostTo(MAIN_SAVE_TEXT, CONTROL_MSG_CLEAR, 0);
        PostTo(MAIN_SAVE_TEXT, CONTROL_MSG_SET_TEXT_ID, STYLE_VALUE, TEXT_NO_SAVE);
        PostTo(MAIN_RESUME, CONTROL_MSG_DEACTIVATE, 0);
    }
    PostTo(MAIN_CHANGE_PROFILE, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON, TEXT_CHANGE_PROFILE);
    for (uint32_t id = MAIN_FIRST_MODE; id <= MAIN_LAST_MODE; id += MAIN_CONTROL_STEP)
        PostTo(id, CONTROL_MSG_ACTIVATE, 0);
    return true;
}
