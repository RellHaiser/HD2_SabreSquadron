// The main menu (original 0x00611200 .. 0x0063c740; part 9h). Part 9h-iii: the class, starting, stopping, the
// update, the messages, the building of the screens (0x006264c0 .. 0x00627690, 0x00636400, 0x0062f740,
// 0x00622af0, 0x006c1970 .. 0x006c1a90).
#include "game/main_menu.h"

#include <cctype>
#include <cstring>

#include "common/os_files.h"
#include "game/campaign.h"
#include "game/control.h"
#include "game/controls.h"
#include "game/game_app.h"
#include "game/game_texts.h"
#include "game/mp_game_setup.h"
#include "game/options_handler.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/session_menu.h"
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

// ---- the profile screen (part 9h-v) -----------------------------------------------------------------------

namespace {

// The profile screen's controls (screen 2).
constexpr uint32_t PROFILE_NAME = 0x8100000, PROFILE_LIST = 0x8200000, PROFILE_FIRST_DIFFICULTY = 0x8300000,
                   PROFILE_LAST_DIFFICULTY = 0x8600000, PROFILE_CREATE = 0x8700000, PROFILE_RENAME = 0x8800000,
                   PROFILE_DELETE = 0x8900000, PROFILE_BACK = 0x8A00000;
constexpr uint32_t PROFILE_CONTROL_STEP = 0x100000;
// The list's selected line (its child 8) and the list's own id bits.
constexpr uint32_t PROFILE_LIST_SELECTED = 0x8200008;
constexpr uint32_t CONTROL_ID_CHILD_MASK = 0xFFFFFFF0;
// The profile screen's step while a new profile's difficulty is picked.
constexpr int STEP_PICK_DIFFICULTY = 3;
// The list's lines' style.
constexpr uint32_t STYLE_PROFILE_LINE = 0x13;
// The name's box edited (posted by the box).
constexpr uint32_t MAIN_MSG_EDIT_NAME = 0x20000C3;
// GET_TEXT's answer when no line is selected.
constexpr uint32_t NO_LINE = 0xFFFFFFFF;

// The name of the list's selected line (its answer to CONTROL_MSG_GET_TEXT).
C_str AskText(C_Control *control)
{
    S_ControlMessage ask;
    ask.id = CONTROL_MSG_GET_TEXT;
    control->HandleMessage(&ask);
    return ask.text;
}

} // namespace

bool C_MainMenu::DescribeProfileScreen(S_ScreenDesc *desc, I3D_frame *)
{
    ScanProfiles(&AddProfileLine, static_cast<int>(reinterpret_cast<uintptr_t>(desc->controls[1])));
    return true;
}

void C_MainMenu::AddProfileLine(const char *name, int context)
{
    // (The original formats the name, as C_fstr's format.)
    S_ControlMessage line;
    line.id = CONTROL_MSG_ADD_TEXT;
    line.value = STYLE_PROFILE_LINE;
    line.text = C_str(name);
    reinterpret_cast<S_ControlDesc *>(static_cast<uintptr_t>(context))->messages.push_back(line);
}

void C_MainMenu::PostProfileLine(const char *name, int)
{
    // (As AddProfileLine: the name formatted in the original.)
    PostText(PROFILE_LIST, CONTROL_MSG_ADD_TEXT, STYLE_PROFILE_LINE, C_str(name));
}

void C_MainMenu::RefreshProfile()
{
    if (m_profileName.IsEmpty()) {
        PostTo(PROFILE_NAME, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON, TEXT_EMPTY);
        for (uint32_t id = PROFILE_FIRST_DIFFICULTY; id <= PROFILE_LAST_DIFFICULTY; id += PROFILE_CONTROL_STEP)
            PostTo(id, CONTROL_MSG_WHITE_TEXT, 0);
        PostTo(PROFILE_RENAME, CONTROL_MSG_DEACTIVATE, 0);
        PostTo(PROFILE_DELETE, CONTROL_MSG_DEACTIVATE, 0);
        return;
    }
    PostText(PROFILE_NAME, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, m_profileName);
    PostText(PROFILE_LIST, CONTROL_MSG_SELECT_TEXT, 0, m_profileName);
    const int difficulty = g_gameProfile.GetDifficulty();
    if (difficulty >= 1 && difficulty <= 4) {
        for (int i = 1; i <= 4; ++i)
            PostTo(PROFILE_FIRST_DIFFICULTY + static_cast<uint32_t>(i - 1) * PROFILE_CONTROL_STEP, CONTROL_MSG_WHITE_TEXT,
                   i == difficulty ? 1 : 0);
    }
    PostTo(PROFILE_RENAME, CONTROL_MSG_ACTIVATE, 0);
    PostTo(PROFILE_DELETE, CONTROL_MSG_ACTIVATE, 0);
}

bool C_MainMenu::OnProfileShown(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    self->RefreshProfile();
    PostTo(PROFILE_NAME, CONTROL_MSG_DEACTIVATE, 0);
    PostTo(PROFILE_LIST, CONTROL_MSG_REFRESH, 0);
    for (uint32_t id = PROFILE_FIRST_DIFFICULTY; id <= PROFILE_LAST_DIFFICULTY; id += PROFILE_CONTROL_STEP)
        PostTo(id, CONTROL_MSG_DEACTIVATE, 0);
    PostTo(PROFILE_CREATE, CONTROL_MSG_ACTIVATE, 0);
    const uint32_t picked = self->m_profileName.IsEmpty() ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE;
    PostTo(PROFILE_RENAME, picked, 0);
    PostTo(PROFILE_DELETE, picked, 0);
    PostTo(PROFILE_BACK, CONTROL_MSG_ACTIVATE, 0);
    return true;
}

bool C_MainMenu::OnProfileList(void *menu, void *, C_Control *control, uint32_t notification, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    if (!control || (control->GetId() & CONTROL_ID_CHILD_MASK) != PROFILE_LIST)
        return false;
    if (notification == CONTROL_NOTIFY_CLICK) {
        const C_str name = AskText(control);
        if (!name.IsEmpty()) {
            self->m_profileName = name;
            SelectProfile(self, nullptr, nullptr, 0, 0);
            self->RefreshProfile();
        }
    } else if (notification == CONTROL_NOTIFY_13) {
        // The current profile's line again: back.
        const C_str name = AskText(control);
        if (!name.IsEmpty() && std::strcmp(self->m_profileName.c_str(), name.c_str()) == 0)
            Post(MENU_MSG_BACK, 0);
    }
    return true;
}

bool C_MainMenu::OnDifficulty(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    if (self->m_step != STEP_PICK_DIFFICULTY)
        return false;
    PostTo(PROFILE_LIST, CONTROL_MSG_ACTIVATE, 0);
    for (uint32_t id = PROFILE_FIRST_DIFFICULTY; id <= PROFILE_LAST_DIFFICULTY; id += PROFILE_CONTROL_STEP)
        PostTo(id, CONTROL_MSG_DEACTIVATE, 0);
    for (uint32_t id = PROFILE_CREATE; id <= PROFILE_BACK; id += PROFILE_CONTROL_STEP)
        PostTo(id, CONTROL_MSG_ACTIVATE, 0);
    if (!control)
        return false;
    const uint32_t id = control->GetId();
    if (id < PROFILE_FIRST_DIFFICULTY || id > PROFILE_LAST_DIFFICULTY || (id - PROFILE_FIRST_DIFFICULTY) % PROFILE_CONTROL_STEP)
        return false;
    g_gameProfile.SetDifficulty(static_cast<int>((id - PROFILE_FIRST_DIFFICULTY) / PROFILE_CONTROL_STEP) + 1);
    S_ControlMessage white;
    white.id = CONTROL_MSG_WHITE_TEXT;
    white.value = 1;
    control->HandleMessage(&white);
    self->m_step = 0;
    g_gameProfile.GetPlayerProfile()->Save();
    return true;
}

bool C_MainMenu::OnProfileMessage(S_ControlMessage *message)
{
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE:
        // (Picking a difficulty: Escape taken.)
        return !g_menuScreens.empty() &&
               (g_menuScreens.back()->GetId() >> SCREEN_INDEX_SHIFT & SCREEN_INDEX_MASK) == MAIN_SCREEN_PROFILE &&
               m_step == STEP_PICK_DIFFICULTY;
    case CONTROL_MSG_REMOVE_SELECTED: {
        if (m_profileName.IsEmpty() || !DeleteProfile(m_profileName.c_str()))
            return true;
        S_ControlMessage remove;
        remove.target = PROFILE_LIST_SELECTED;
        remove.id = CONTROL_MSG_REMOVE_SELECTED;
        HandleMessage(&remove);
        S_ControlMessage ask;
        ask.target = PROFILE_LIST_SELECTED;
        ask.id = CONTROL_MSG_GET_TEXT;
        HandleMessage(&ask);
        m_profileName = ask.value == NO_LINE ? C_str("") : ask.text;
        SelectProfile(this, nullptr, nullptr, 0, 0);
        RefreshProfile();
        return true;
    }
    case MAIN_MSG_EDIT_NAME:
        PostTo(PROFILE_NAME, CONTROL_MSG_EDIT, 0);
        return true;
    default:
        return false;
    }
}

// ---- the profile screen's buttons (part 9h-vi) ------------------------------------------------------------

namespace {

// The profile screen's steps: a new profile's name typed, a profile's new name typed.
constexpr int STEP_NEW_NAME = 1, STEP_RENAME = 2;
// The questions: delete the profile; the name taken / not valid, creating and renaming.
constexpr uint32_t TEXT_DELETE_PROFILE = 0x7D4, TEXT_CREATE_EXISTS = 0x251C, TEXT_CREATE_FAILED = 0x251D,
                   TEXT_RENAME_EXISTS = 0x2526, TEXT_RENAME_FAILED = 0x2527;
constexpr int MESSAGE_KIND_QUESTION = 1, MESSAGE_KIND_INFO = 2;
// The name typed (the original's buffer).
constexpr int TYPED_NAME_SIZE = 100;

// The name box's text without its trailing spaces (ASCII ones).
void TypedName(C_Control *box, char (&name)[TYPED_NAME_SIZE])
{
    const C_str text = AskText(box);
    // Fix: at most 99 characters (the original copies the text whole; the box's limit is 17, none in Japanese).
    std::memset(name, 0, sizeof name);
    std::strncpy(name, text.c_str(), sizeof name - 1);
    size_t length = std::strlen(name);
    while (length > 0 && static_cast<unsigned char>(name[length - 1]) < 0x80 && std::isspace(name[length - 1]))
        name[--length] = '\0';
}

} // namespace

bool C_MainMenu::OnProfileButton(void *menu, void *, C_Control *control, uint32_t notification, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    // (Fix: no control: nothing; the original reads it.)
    if (!control)
        return true;
    switch (control->GetId() & CONTROL_ID_CHILD_MASK) {
    case PROFILE_DELETE:
        self->ShowMessageId(MESSAGE_KIND_QUESTION, TEXT_DELETE_PROFILE, CONTROL_MSG_REMOVE_SELECTED, 0);
        return true;
    case PROFILE_RENAME:
        // The name box edited, the rest off.
        PostTo(PROFILE_NAME, CONTROL_MSG_ACTIVATE, 0);
        PostTo(PROFILE_NAME, CONTROL_MSG_EDIT, 0);
        PostTo(PROFILE_LIST, CONTROL_MSG_DEACTIVATE, 0);
        for (uint32_t id = PROFILE_CREATE; id <= PROFILE_BACK; id += PROFILE_CONTROL_STEP)
            PostTo(id, CONTROL_MSG_DEACTIVATE, 0);
        self->m_step = STEP_RENAME;
        return true;
    case PROFILE_CREATE:
        // The name box emptied and edited, the rest off, no difficulty white.
        PostTo(PROFILE_NAME, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON, TEXT_EMPTY);
        PostTo(PROFILE_NAME, CONTROL_MSG_ACTIVATE, 0);
        PostTo(PROFILE_NAME, CONTROL_MSG_EDIT, 0);
        PostTo(PROFILE_LIST, CONTROL_MSG_DEACTIVATE, 0);
        for (uint32_t id = PROFILE_CREATE; id <= PROFILE_BACK; id += PROFILE_CONTROL_STEP)
            PostTo(id, CONTROL_MSG_DEACTIVATE, 0);
        for (uint32_t id = PROFILE_FIRST_DIFFICULTY; id <= PROFILE_LAST_DIFFICULTY; id += PROFILE_CONTROL_STEP)
            PostTo(id, CONTROL_MSG_WHITE_TEXT, 0);
        self->m_step = STEP_NEW_NAME;
        return true;
    case PROFILE_NAME:
        break;
    default:
        return true;
    }

    if (notification == CONTROL_NOTIFY_ESCAPE) {
        // The typing given up: the screen as it was (the step kept, as the original).
        self->RefreshProfile();
        PostTo(PROFILE_NAME, CONTROL_MSG_DEACTIVATE, 0);
        PostTo(PROFILE_LIST, CONTROL_MSG_ACTIVATE, 0);
        PostTo(PROFILE_CREATE, CONTROL_MSG_ACTIVATE, 0);
        if (!self->m_profileName.IsEmpty()) {
            PostTo(PROFILE_RENAME, CONTROL_MSG_ACTIVATE, 0);
            PostTo(PROFILE_DELETE, CONTROL_MSG_ACTIVATE, 0);
        }
        PostTo(PROFILE_BACK, CONTROL_MSG_ACTIVATE, 0);
        return true;
    }
    if (notification != CONTROL_NOTIFY_RETURN)
        return true;

    char name[TYPED_NAME_SIZE];
    if (self->m_step == STEP_NEW_NAME) {
        TypedName(control, name);
        const int result = CreateProfile(name);
        if (result == -1) {
            self->ShowMessageId(MESSAGE_KIND_INFO, TEXT_CREATE_EXISTS, MAIN_MSG_EDIT_NAME, MAIN_MSG_EDIT_NAME);
        } else if (result == -2) {
            self->ShowMessageId(MESSAGE_KIND_INFO, TEXT_CREATE_FAILED, MAIN_MSG_EDIT_NAME, MAIN_MSG_EDIT_NAME);
        } else {
            // Made: its difficulty to pick, the list read again with it selected.
            PostTo(PROFILE_NAME, CONTROL_MSG_DEACTIVATE, 0);
            PostTo(PROFILE_LIST, CONTROL_MSG_CLEAR, 0);
            for (uint32_t id = PROFILE_FIRST_DIFFICULTY; id <= PROFILE_LAST_DIFFICULTY; id += PROFILE_CONTROL_STEP)
                PostTo(id, CONTROL_MSG_ACTIVATE, 0);
            self->m_profileName = C_str(name);
            SelectProfile(self, nullptr, nullptr, 0, 0);
            g_gameProfile.GetPlayerProfile()->Save();
            ScanProfiles(&PostProfileLine, 0);
            self->m_step = STEP_PICK_DIFFICULTY;
            PostText(PROFILE_NAME, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, self->m_profileName);
            PostText(PROFILE_LIST, CONTROL_MSG_SELECT_TEXT, 0, self->m_profileName);
        }
    }
    if (self->m_step == STEP_RENAME) {
        TypedName(control, name);
        if (name[0] == '\0') {
            // Nothing typed: the name as it was.
            self->RefreshProfile();
            PostTo(PROFILE_NAME, CONTROL_MSG_DEACTIVATE, 0);
            PostText(PROFILE_NAME, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, self->m_profileName);
            PostTo(PROFILE_LIST, CONTROL_MSG_ACTIVATE, 0);
            for (uint32_t id = PROFILE_CREATE; id <= PROFILE_BACK; id += PROFILE_CONTROL_STEP)
                PostTo(id, CONTROL_MSG_ACTIVATE, 0);
            return true;
        }
        const int result = g_gameProfile.GetPlayerProfile()->Rename(self->m_profileName.c_str(), name);
        if (result == -1) {
            self->ShowMessageId(MESSAGE_KIND_INFO, TEXT_RENAME_EXISTS, MAIN_MSG_EDIT_NAME, MAIN_MSG_EDIT_NAME);
        } else if (result == -2) {
            self->ShowMessageId(MESSAGE_KIND_INFO, TEXT_RENAME_FAILED, MAIN_MSG_EDIT_NAME, MAIN_MSG_EDIT_NAME);
        } else {
            PostTo(PROFILE_NAME, CONTROL_MSG_DEACTIVATE, 0);
            PostTo(PROFILE_LIST, CONTROL_MSG_CLEAR, 0);
            PostTo(PROFILE_LIST, CONTROL_MSG_ACTIVATE, 0);
            for (uint32_t id = PROFILE_CREATE; id <= PROFILE_BACK; id += PROFILE_CONTROL_STEP)
                PostTo(id, CONTROL_MSG_ACTIVATE, 0);
            self->m_profileName = C_str(name);
            ScanProfiles(&PostProfileLine, 0);
            g_gameProfile.m_config->SetLastProfile(self->m_profileName);
            self->RefreshProfile();
            self->m_step = 0;
        }
    }
    return true;
}

// ---- the single-player screen (part 9h-vii) ---------------------------------------------------------------

namespace {

// The single-player screen's controls (screen 3): the side switch, the description text, the mode buttons.
constexpr uint32_t SINGLE_SIDE = 0xC200000, SINGLE_TEXT = 0xC300000, SINGLE_MISSION = 0xC700000,
                   SINGLE_MISSION_CARNAGE = 0xC800000, SINGLE_BACK = 0xCC00000;
// The description's lines' styles: the heading's and the mode's.
constexpr uint32_t STYLE_MODE_HEADING = 2, STYLE_MODE_TEXT = 3;
constexpr uint32_t TEXT_MODE_HEADING = 0x8A2;
// What a mode's button starts (the menu's messages with the main screen's bits): a campaign, the single
// missions.
constexpr uint32_t MAIN_MSG_START_CAMPAIGN = 0x2000033, MAIN_MSG_SINGLE_MISSIONS = 0x2000074;

// A mode's button: its side's frame and text; what it starts (the campaign; the single missions: -1 none) and
// in which play mode.
struct S_SingleMode {
    uint32_t button;
    int frame;
    uint32_t text;
    int campaign;                                       // (-1: a single mission)
    int playMode;
};
const S_SingleMode SINGLE_MODES[] = {
    {0xC400000, 0, 0x8A3, 0, PLAY_MODE_CAMPAIGN},       // "bcampaign"
    {0xC500000, 1, 0x8A4, 0, PLAY_MODE_LONE_WOLF},      // "blone wolf"
    {0xC600000, 2, 0x8A5, 0, PLAY_MODE_CARNAGE},        // "carnage"
    {0xC700000, 3, 0x8A6, -1, 0},                       // "bsingle mission"
    {0xC800000, 4, 0x8A7, -1, PLAY_MODE_CARNAGE},       // "bsingle mission- carnage"
    {0xC900000, 5, 0x8A3, 1, PLAY_MODE_CAMPAIGN},       // "bcampaign01" (the second campaign's)
    {0xCA00000, 6, 0x8A4, 1, PLAY_MODE_LONE_WOLF},      // "blone wolf01"
    {0xCB00000, 7, 0x8A5, 1, PLAY_MODE_CARNAGE},        // "carnage01"
};

} // namespace

bool C_MainMenu::OnSingleplayerShown(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    self->m_saveName = C_str("");
    PostTo(SINGLE_MISSION, CONTROL_MSG_ACTIVATE, 0);
    PostTo(SINGLE_MISSION_CARNAGE, CONTROL_MSG_ACTIVATE, 0);
    return true;
}

bool C_MainMenu::OnSingleplayerButton(void *, void *, C_Control *control, uint32_t notification, uint32_t)
{
    if (!control)
        return false;
    PostTo(SINGLE_TEXT, CONTROL_MSG_CLEAR, 0);
    const uint32_t id = control->GetId() & CONTROL_ID_CHILD_MASK;
    const S_SingleMode *mode = nullptr;
    for (const S_SingleMode &known : SINGLE_MODES)
        if (known.button == id)
            mode = &known;

    if (notification == CONTROL_NOTIFY_ENTER) {
        // The cursor over a mode: its side shown, its description; over back: no side.
        if (id == SINGLE_BACK) {
            PostTo(SINGLE_SIDE, CONTROL_MSG_HIDE_FRAME, 0);
            return true;
        }
        if (!mode)
            return false;
        PostTo(SINGLE_SIDE, CONTROL_MSG_SELECT_FRAME, static_cast<uint32_t>(mode->frame));
        PostTo(SINGLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, TEXT_MODE_HEADING);
        PostTo(SINGLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, TEXT_EMPTY);
        PostTo(SINGLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_TEXT, mode->text);
        return true;
    }
    if (notification != CONTROL_NOTIFY_CLICK)
        return false;

    // A click: the mode started (not a network game).
    PostTo(SINGLE_SIDE, CONTROL_MSG_HIDE_FRAME, 0);
    g_mpSettings->sessionType = SESSION_NONE;
    if (id == SINGLE_BACK) {
        Post(MENU_MSG_BACK, 0);
        return true;
    }
    if (!mode)
        return false;
    if (mode->campaign >= 0) {
        g_gameProfile.m_campaign->SetCurrentIndex(mode->campaign);
        g_gameProfile.m_playMode->singleMission = 0;
        g_gameProfile.m_playMode->mode = mode->playMode;
        Post(FROM_SCREEN | MAIN_MSG_START_CAMPAIGN, 0);
    } else {
        g_gameProfile.m_playMode->singleMission = 1;
        g_gameProfile.m_playMode->mode = mode->playMode;
        Post(FROM_SCREEN | MAIN_MSG_SINGLE_MISSIONS, 0);
    }
    return true;
}

// ---- the missions screen's description, lists and pick (part 9h-viii) -------------------------------------

namespace {

constexpr int CONTROL_PICTURE = 4, CONTROL_LABEL = 0x12, CONTROL_SCROLL_BAR = 8, CONTROL_HIDING_LIST = 0x23,
              CONTROL_LIST_ITEM = 6;
// The lists: their scroll (the hiding list's value), the lines shown, their step down; the lines' styles.
constexpr uint32_t MISSION_LIST_SCROLL = 0x14600000, MISSION_LIST_ROWS = 0x11;
constexpr float MISSION_LIST_STEP = 0.032f;
constexpr uint32_t STYLE_OPERATION = 0xD, STYLE_MISSION = 0xC;
// The missions screen's controls (by their order in its description: screen 5's).
constexpr uint32_t MISSIONS_PICTURE = 0x200000, MISSIONS_TEXT = 0x300000, MISSIONS_LOAD = 0x400000;
constexpr uint32_t MISSIONS_LOAD_LAST = 0x14B00000;
constexpr uint32_t MAIN_MSG_START_MISSION = 0x200001E;
// The description: its heading; a mission's texts from this id on have a title and lines; carnage's note.
constexpr uint32_t TEXT_MISSION_HEADING = 0x8D7, TEXT_CARNAGE_NOTE = 0x189C;
constexpr int FIRST_MISSION_TEXT = 0x1194, MISSION_EXTRA_LINES = 6;

uintptr_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// A mission list: a hiding list scrolled (by `scroll`), its rows, its step, the lines' origin the template's
// place. (The cinematics screen's list too.)
S_ControlDesc *AddMissionList(S_ScreenDesc *desc, const char *name, uint32_t scroll, I3D_frame *lineTemplate)
{
    S_ControlDesc *list = desc->AddControl(CONTROL_HIDING_LIST, name);
    AddDescMessage(list, 0, CONTROL_MSG_SET_VALUE, scroll);
    AddDescMessage(list, 0, CONTROL_MSG_SET_VISIBLE, MISSION_LIST_ROWS);
    AddDescMessage(list, 0, CONTROL_MSG_SET_DOWN_STEP, 0, FloatBits(MISSION_LIST_STEP));
    // (The list frees it.)
    auto *origin = static_cast<S_vector4 *>(_aligned_malloc(sizeof(S_vector4), 16));
    if (origin)
        std::memcpy(origin, &lineTemplate->GetPos(), sizeof(S_vector4));
    AddDescMessage(list, 0, CONTROL_MSG_SET_ORIGIN, 0, reinterpret_cast<uintptr_t>(origin));
    return list;
}

} // namespace

bool C_MainMenu::DescribeMissionsScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    if (I3D_frame *video = model->FindChildFrame("video", FRAME_MASK_VISUAL))
        video->SetOn(false);
    if (auto *shot = static_cast<I3D_object_visual *>(model->FindChildFrame("screen_shot", FRAME_MASK_VISUAL))) {
        shot->DuplicateMesh();
        shot->GetVisualMesh()->Unknown18();
    }
    I3D_frame *operationTemplate = model->FindChildFrame("blong", FRAME_MASK_DUMMY);
    I3D_frame *missionTemplate = model->FindChildFrame("bshort", FRAME_MASK_DUMMY);
    I3D_frame *campaigns = AddFrame(C_str("GB_Campaign"), FRAME_DUMMY, model, S_vector(0.0f, 0.0f, 0.0f));
    I3D_frame *singles = AddFrame(C_str("GB_SingleMission"), FRAME_DUMMY, model, S_vector(0.0f, 0.0f, 0.0f));
    if (!campaigns || !singles)
        return false;
    // Fix: no templates: false (the original uses them unchecked).
    if (!operationTemplate || !missionTemplate)
        return false;
    operationTemplate->Update();
    operationTemplate->SetOn(false);
    missionTemplate->Update();
    missionTemplate->SetOn(false);
    campaigns->Update();
    singles->Update();

    AddText(desc, "text_screen", STYLE_TITLE, 0x8CB);
    desc->AddControl(CONTROL_PICTURE, "screen_shot");
    AddDescMessage(desc->AddControl(CONTROL_LABEL, "text00"), 0, CONTROL_MSG_SET_MARGIN, 0);
    S_ControlDesc *load = AddButton(desc, "bload mission", 0x935);
    AddDescMessage(load, 0, CONTROL_MSG_DEACTIVATE, 0);
    AddDescMessage(load, 0, FROM_SCREEN | MAIN_MSG_START_MISSION, 0);
    AddMenuButton(desc, "bexit", 0x8D0, MENU_MSG_BACK, 0);
    desc->AddControl(CONTROL_SCROLL_BAR, "scroll00");
    operationTemplate->LinkTo(campaigns, 1);
    missionTemplate->LinkTo(campaigns, 1);
    DescribeCampaignList(desc, campaigns, operationTemplate, missionTemplate);
    operationTemplate->LinkTo(singles, 1);
    DescribeSingleList(desc, singles, operationTemplate);
    AddText(desc, "text_list of profiles", STYLE_LABEL, 0x8D2);
    AddText(desc, "text_h&dversion", STYLE_VERSION, TEXT_VERSION);
    S_ControlDesc *last = AddButton(desc, "bload lastsave", 0x839);
    AddDescMessage(last, 0, CONTROL_MSG_DEACTIVATE, 0);
    AddDescMessage(last, 0, FROM_SCREEN | MAIN_MSG_LOAD_GAME, 0);
    AddDescMessage(AddButton(desc, "bexit01", 0x8D0), 0, CONTROL_MSG_HIDE, 0);
    return true;
}

void C_MainMenu::DescribeCampaignList(S_ScreenDesc *desc, I3D_frame *group, I3D_frame *operationTemplate,
                                      I3D_frame *missionTemplate)
{
    S_ControlDesc *list = AddMissionList(desc, "GB_Campaign", MISSION_LIST_SCROLL, operationTemplate);
    const auto self = reinterpret_cast<uintptr_t>(this);
    C_Campaign *campaign = g_gameProfile.m_campaign;
    int index = 0;
    for (int c = 0; c < campaign->GetCount(); ++c) {
        const int operations = campaign->CountOperations(c);
        const int first = c == 0 ? 0 : campaign->CountMissions(0);
        for (int op = 0; op < operations; ++op) {
            const C_fstr name("Campaign%02d", op + first);
            CloneFrame(name, group, operationTemplate);
            const int operationText = campaign->GetOperationId(c, op);
            S_ControlDesc *line = list->AddChild(CONTROL_LIST_ITEM, name.c_str());
            AddDescMessage(line, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_OPERATION, static_cast<uintptr_t>(operationText));
            AddDescMessage(line, 0, CONTROL_MSG_IGNORE_INPUT, 0);
            const int missions = campaign->CountOperationMissions(c, op);
            for (int m = 0; m < missions; ++m, ++index) {
                const C_fstr missionName("Mission%02d", index);
                CloneFrame(missionName, group, missionTemplate);
                const int text = campaign->GetMissionId(c, index - first);
                S_ControlDesc *missionLine = list->AddChild(CONTROL_LIST_ITEM, missionName.c_str());
                AddDescMessage(missionLine, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_MISSION, static_cast<uintptr_t>(text));
                AddDescMessage(missionLine, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(index));
                AddDescMessage(missionLine, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&OnMissionPick));
            }
        }
    }
    AddDescMessage(list, 0, CONTROL_MSG_REFRESH, 0);
}

void C_MainMenu::DescribeSingleList(S_ScreenDesc *desc, I3D_frame *group, I3D_frame *missionTemplate)
{
    S_ControlDesc *list = AddMissionList(desc, "GB_SingleMission", MISSION_LIST_SCROLL, missionTemplate);
    const auto self = reinterpret_cast<uintptr_t>(this);
    C_Campaign *campaign = g_gameProfile.m_campaign;
    for (int c = 0; c < campaign->GetCount(); ++c) {
        const int missions = campaign->CountMissions(c);
        const int first = c == 0 ? 0 : campaign->CountMissions(0);
        // (The first campaign's first mission is not among them.)
        for (int m = c == 0 ? 1 : 0; m < missions; ++m) {
            const int index = m + first;
            const C_fstr name("SMMission%02d", index);
            CloneFrame(name, group, missionTemplate);
            const int text = campaign->GetMissionId(c, m);
            S_ControlDesc *line = list->AddChild(CONTROL_LIST_ITEM, name.c_str());
            AddDescMessage(line, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_MISSION, static_cast<uintptr_t>(text));
            AddDescMessage(line, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(index));
            AddDescMessage(line, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&OnMissionPick));
        }
    }
    AddDescMessage(list, 0, CONTROL_MSG_REFRESH, 0);
}

void C_MainMenu::ShowSaveSlot(int mission)
{
    m_saveName = C_str("");
    const S_PlayMode *mode = g_gameProfile.m_playMode;
    int slot = -1;
    if (mode->singleMission == 0) {
        if (mode->mode == PLAY_MODE_CAMPAIGN)
            slot = SAVE_SLOT_CAMPAIGN;
        else if (mode->mode == PLAY_MODE_LONE_WOLF)
            slot = SAVE_SLOT_LONE_WOLF;
        else if (mode->mode == PLAY_MODE_CARNAGE)
            slot = SAVE_SLOT_CARNAGE;
        else
            return;
    } else if (mode->singleMission == 1) {
        if (mode->mode == 0)
            slot = SAVE_SLOT_SINGLE_MISSION;
        else if (mode->mode == PLAY_MODE_CARNAGE)
            slot = SAVE_SLOT_SINGLE_CARNAGE;
        else
            return;
    }
    bool resumable = false;
    if (slot >= 0) {
        const C_PlayerProfile *player = g_gameProfile.GetPlayerProfile();
        m_saveName = player->GetSaveSlotFile(slot);
        const int saved = player->GetSaveSlotMission(slot);
        resumable = saved != -1 && mission == saved;
    }
    PostTo(MISSIONS_LOAD_LAST, resumable ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE, 0);
}

bool C_MainMenu::OnMissionPick(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    if (!control)
        return false;
    // The line's index (through the campaigns), its screen.
    const int index = control->GetIndex();
    const uint32_t screen = control->GetId() >> SCREEN_INDEX_SHIFT & SCREEN_INDEX_MASK;
    C_Campaign *campaign = g_gameProfile.m_campaign;
    const int firstCount = campaign->CountMissions(0);
    const int campaignIndex = index >= firstCount ? 1 : 0;
    const int missionNumber = index >= firstCount ? index - firstCount : index;
    const C_str image = campaign->GetMissionImage(campaignIndex, missionNumber);
    self->m_operationPicked = false;
    self->m_mission = index;
    self->ShowSaveSlot(index);
    const uint32_t base = screen << SCREEN_INDEX_SHIFT;
    PostTo(base | MISSIONS_LOAD, CONTROL_MSG_ACTIVATE, 0);
    PostTo(base | MISSIONS_TEXT, CONTROL_MSG_CLEAR, 0);
    PostText(base | MISSIONS_PICTURE, CONTROL_MSG_LOAD_TEXTURE, 0, image);
    const int text = campaign->GetMissionText(campaignIndex, missionNumber, 1);
    if (text < FIRST_MISSION_TEXT)
        return true;
    const uint32_t description = base | MISSIONS_TEXT;
    PostTo(description, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, TEXT_MISSION_HEADING);
    PostTo(description, CONTROL_MSG_ADD_TEXT_ID, STYLE_BUTTON, static_cast<uintptr_t>(text + 1));
    PostTo(description, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, static_cast<uintptr_t>(text + 2));
    PostTo(description, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, TEXT_EMPTY);
    PostTo(description, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_TEXT, static_cast<uintptr_t>(text + 3));
    PostTo(description, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, TEXT_EMPTY);
    // The lines after it, while there are texts (six at most).
    for (int i = 0, id = text + 3; i < MISSION_EXTRA_LINES; ++i) {
        ++id;
        if (g_texts.Get(id).IsEmpty())
            break;
        PostTo(description, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, static_cast<uintptr_t>(id));
    }
    if (g_gameProfile.m_playMode->mode == PLAY_MODE_CARNAGE)
        PostTo(description, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, TEXT_CARNAGE_NOTE);
    return true;
}

// ---- the missions screen's messages and lists shown (part 9h-ix) ----------------------------------------

namespace {

// The menu's own messages about the missions: a mission started, the single-mission flag / play mode set
// (value), the campaign started / the single missions opened (the single-player screen's).
constexpr uint32_t MAIN_MSG_SET_SINGLE_MISSION = 0x2000031, MAIN_MSG_SET_PLAY_MODE = 0x2000070;
// The missions screen's controls (absolute): its title, the campaigns' and the single missions' lists.
constexpr uint32_t MISSIONS_SCREEN = static_cast<uint32_t>(MAIN_SCREEN_MISSIONS) << SCREEN_INDEX_SHIFT;
constexpr uint32_t MISSIONS_TITLE = MISSIONS_SCREEN | 0x100000, CAMPAIGN_LIST = MISSIONS_SCREEN | 0x700000,
                   SINGLE_LIST = MISSIONS_SCREEN | 0x800000;
// A list's line by its place (from 1).
constexpr uint32_t LINE_INDEX_SHIFT = 12, LINE_INDEX_MASK = 0xFF;
// Every mission shown (the secret key's; else the player's progress).
constexpr int ALL_MISSIONS = 100;
// The titles: the campaign by play mode (1 .. 3), the single missions (mode 0 / carnage).
constexpr uint32_t TEXT_CAMPAIGN_TITLES[] = {0x8D3, 0x8D4, 0x8D5};
constexpr uint32_t TEXT_SINGLE_TITLE = 0x8CB, TEXT_SINGLE_CARNAGE_TITLE = 0x8D6;

uint32_t LineId(uint32_t list, int place)
{
    return (static_cast<uint32_t>(place) & LINE_INDEX_MASK) << LINE_INDEX_SHIFT | list;
}

// The missions screen made ready for a list: no save picked, the title, both lists hidden, the scroll bar on
// `list`, the picture restored, the description cleared, load and load the last save off.
void ResetMissionsScreen(uint32_t title, uint32_t list)
{
    PostTo(MISSIONS_TITLE, CONTROL_MSG_SET_TEXT_ID, STYLE_TITLE, title);
    PostTo(CAMPAIGN_LIST, CONTROL_MSG_HIDE_ALL, 0);
    PostTo(SINGLE_LIST, CONTROL_MSG_HIDE_ALL, 0);
    PostTo(MISSION_LIST_SCROLL, CONTROL_MSG_SET_VALUE, list);
    PostTo(MISSIONS_SCREEN | MISSIONS_PICTURE, CONTROL_MSG_RESTORE_TEXTURE, 0);
    PostTo(MISSIONS_SCREEN | MISSIONS_TEXT, CONTROL_MSG_CLEAR, 0);
    PostTo(MISSIONS_SCREEN | MISSIONS_LOAD, CONTROL_MSG_DEACTIVATE, 0);
    PostTo(MISSIONS_LOAD_LAST, CONTROL_MSG_DEACTIVATE, 0);
}

} // namespace

bool C_MainMenu::OnMissionsMessage(S_ControlMessage *message)
{
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_OPEN_POPUP:
        // (Taken: OnMessage leaves popups be.)
        return true;
    case MAIN_MSG_START_MISSION: {
        if (m_mission == -1)
            return true;
        g_mpSettings->sessionType = SESSION_NONE;
        g_gameProfile.ResetGame();
        C_Campaign *campaign = g_gameProfile.m_campaign;
        const int firstCount = campaign->CountMissions(0);
        // A single mission: its campaign and its number there set up.
        if (g_gameProfile.m_playMode->singleMission == 1) {
            int missionNumber = m_mission;
            if (m_mission < firstCount) {
                campaign->SetCurrentIndex(0);
            } else {
                campaign->SetCurrentIndex(1);
                missionNumber -= firstCount;
            }
            campaign->SetupSingleMission(campaign->GetCurrentIndex(), missionNumber);
            m_state = GAME_STATE_ENDED;
            return true;
        }
        // The campaign: a campaign's first mission begins it; another loads the save made before it.
        if (m_mission != 0 && m_mission != firstCount) {
            const int before = m_mission < firstCount ? m_mission - 1 : m_mission - firstCount - 1;
            GetGameApp()->LoadGame(g_gameProfile.GetSaveFileName(campaign->GetCurrentIndex(), before), false);
        }
        campaign->AdvanceToMission();
        m_state = GAME_STATE_ENDED;
        return true;
    }
    case MAIN_MSG_SET_SINGLE_MISSION:
        g_mpSettings->sessionType = SESSION_NONE;
        g_gameProfile.m_playMode->singleMission = static_cast<int>(message->value);
        return true;
    case MAIN_MSG_START_CAMPAIGN: {
        m_singleMissions = false;
        g_mpSettings->sessionType = SESSION_NONE;
        g_gameProfile.ResetGame();
        // Played before: its missions to pick from; else the roster (loaded when empty), the first mission.
        if (g_gameProfile.GetPlayerProfile()->HasProgress(g_gameProfile.m_campaign->GetCurrentIndex())) {
            ShowCampaignMissions();
            Post(MENU_MSG_OPEN_SCREEN, MAIN_SCREEN_MISSIONS);
            return true;
        }
        C_HumanStore *store = g_gameProfile.m_humanStore;
        if (store->GetCount() == 0 && !store->Load(C_str("tables\\HumanStore.def")))
            return false;
        g_gameProfile.m_campaign->AdvanceToMission();
        Post(MENU_MSG_NEXT, 0);
        return true;
    }
    case MAIN_MSG_SET_PLAY_MODE:
        g_mpSettings->sessionType = SESSION_NONE;
        g_gameProfile.m_playMode->mode = static_cast<int>(message->value);
        return true;
    case MAIN_MSG_SINGLE_MISSIONS:
        m_singleMissions = true;
        ShowSingleMissions();
        Post(MENU_MSG_OPEN_SCREEN, MAIN_SCREEN_MISSIONS);
        return true;
    case MENU_MSG_SECRET_KEY:
        // The single missions shown: every one of them shown / only those reached.
        if (m_singleMissions && !g_menuScreens.empty() &&
            (g_menuScreens.back()->GetId() >> SCREEN_INDEX_SHIFT & SCREEN_INDEX_MASK) == MAIN_SCREEN_MISSIONS) {
            m_progress = m_progress != ALL_MISSIONS ? ALL_MISSIONS : -1;
            ShowSingleMissions();
        }
        return true;
    default:
        return false;
    }
}

void C_MainMenu::ShowCampaignMissions()
{
    m_saveName = C_str("");
    C_Campaign *campaign = g_gameProfile.m_campaign;
    const int mode = g_gameProfile.m_playMode->mode;
    const uint32_t title = mode >= PLAY_MODE_CAMPAIGN && mode <= PLAY_MODE_CARNAGE ? TEXT_CAMPAIGN_TITLES[mode - 1] : 0;
    ResetMissionsScreen(title, CAMPAIGN_LIST);
    const int current = campaign->GetCurrentIndex();
    m_progress = g_gameProfile.GetPlayerProfile()->GetProgress(current);
    const int operations = campaign->CountOperations(current);
    // The lines before the campaign's: the first campaign's operations and missions.
    const int base = current == 0 ? 0 : campaign->CountMissions(0) + campaign->CountOperations(0);
    // Each operation's line, then its missions' up to the one after the progress.
    for (int op = 0, place = 1, shown = 0; op < operations; ++op) {
        PostTo(CAMPAIGN_LIST, CONTROL_MSG_ITEM_COUNTED, LineId(CAMPAIGN_LIST, base + place++));
        const int missions = campaign->CountOperationMissions(current, op);
        for (int m = 0; m < missions; ++m) {
            PostTo(CAMPAIGN_LIST, CONTROL_MSG_ITEM_COUNTED, LineId(CAMPAIGN_LIST, base + place++));
            if (m_progress < shown++)
                return;
        }
    }
}

void C_MainMenu::ShowSingleMissions()
{
    m_saveName = C_str("");
    const int mode = g_gameProfile.m_playMode->mode;
    const uint32_t title = mode == 0 ? TEXT_SINGLE_TITLE : mode == PLAY_MODE_CARNAGE ? TEXT_SINGLE_CARNAGE_TITLE : 0;
    ResetMissionsScreen(title, SINGLE_LIST);
    C_Campaign *campaign = g_gameProfile.m_campaign;
    for (int c = 0; c < campaign->GetCount(); ++c) {
        if (m_progress != ALL_MISSIONS)
            m_progress = g_gameProfile.GetPlayerProfile()->GetBestProgress(c);
        const int missions = campaign->CountMissions(c);
        const int first = c == 0 ? 0 : campaign->CountMissions(0);
        // (The first campaign's first mission is not among them.)
        for (int m = c == 0 ? 1 : 0; m < missions && m <= m_progress; ++m)
            PostTo(SINGLE_LIST, CONTROL_MSG_ITEM_COUNTED, LineId(SINGLE_LIST, m + first));
    }
}

bool C_MainMenu::OnOperationPick(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    if (!control)
        return false;
    const int index = control->GetIndex();
    const uint32_t base = control->GetId() >> SCREEN_INDEX_SHIFT << SCREEN_INDEX_SHIFT;
    C_Campaign *campaign = g_gameProfile.m_campaign;
    const C_str name = campaign->GetOperationName(campaign->GetCurrentIndex(), index);
    self->m_operationPicked = true;
    self->m_mission = index;
    PostTo(base | MISSIONS_LOAD, CONTROL_MSG_ACTIVATE, 0);
    PostTo(base | MISSIONS_TEXT, CONTROL_MSG_CLEAR, 0);
    PostText(base | MISSIONS_PICTURE, CONTROL_MSG_LOAD_TEXTURE, 0, name);
    return true;
}

// ---- the cinematics screen (part 9h-ix) ---------------------------------------------------------------------

namespace {

constexpr int CONTROL_MOVIE = 0x22;
// The cinematics screen's controls (absolute; by their order in its description: screen 7's).
constexpr uint32_t CINEMATICS_SCREEN = static_cast<uint32_t>(MAIN_SCREEN_CINEMATICS) << SCREEN_INDEX_SHIFT;
constexpr uint32_t CINEMATICS_MOVIE = CINEMATICS_SCREEN | 0x300000, CINEMATICS_PLAY = CINEMATICS_SCREEN | 0x400000,
                   CINEMATICS_SCROLL = CINEMATICS_SCREEN | 0x600000, CINEMATICS_LIST = CINEMATICS_SCREEN | 0x700000;
// The title, play, the list's label; the lines' style.
constexpr uint32_t TEXT_CINEMATICS_TITLE = 0x83D, TEXT_PLAY = 0x8D8, TEXT_MOVIES_LABEL = 0x9F;
constexpr uint32_t STYLE_MOVIE = 0xC;

} // namespace

bool C_MainMenu::DescribeCinematicsScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    if (I3D_frame *video = model->FindChildFrame("video", FRAME_MASK_VISUAL))
        video->SetOn(false);
    if (I3D_frame *text = model->FindChildFrame("text00", FRAME_MASK_VISUAL))
        text->SetOn(false);
    if (auto *shot = static_cast<I3D_object_visual *>(model->FindChildFrame("screen_shot", FRAME_MASK_VISUAL))) {
        shot->DuplicateMesh();
        shot->GetVisualMesh()->Unknown18();
    }
    I3D_frame *lineTemplate = model->FindChildFrame("blong", FRAME_MASK_DUMMY);
    I3D_frame *shortTemplate = model->FindChildFrame("bshort", FRAME_MASK_DUMMY);
    I3D_frame *group = AddFrame(C_str("GB_Cinematics"), FRAME_DUMMY, model, S_vector(0.0f, 0.0f, 0.0f));
    if (!group)
        return false;
    // Fix: no templates: false (the original uses them unchecked).
    if (!lineTemplate || !shortTemplate)
        return false;
    lineTemplate->Update();
    lineTemplate->SetOn(false);
    shortTemplate->Update();
    shortTemplate->SetOn(false);
    group->Update();
    lineTemplate->LinkTo(group, 1);
    const auto self = reinterpret_cast<uintptr_t>(this);
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_HIDDEN, self, Callback(&OnCinematicsHidden));
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, self, Callback(&OnCinematicsShown));

    AddText(desc, "text_screen", STYLE_TITLE, TEXT_CINEMATICS_TITLE);
    desc->AddControl(CONTROL_PICTURE, "screen_shot");
    desc->AddControl(CONTROL_MOVIE, "video");
    S_ControlDesc *play = AddButton(desc, "bexit01", TEXT_PLAY);
    AddDescMessage(play, 0, CONTROL_MSG_DEACTIVATE, 0);
    AddDescMessage(play, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&OnMoviePlay));
    AddMenuButton(desc, "bexit", 0x8D0, MENU_MSG_BACK, 0);
    AddDescMessage(desc->AddControl(CONTROL_SCROLL_BAR, "scroll00"), 0, CONTROL_MSG_SET_VALUE, CINEMATICS_LIST);
    // The movies' list: a line per movie (copies of the template "Anim%02d"), its text, picked: OnMoviePick.
    S_ControlDesc *list = AddMissionList(desc, "GB_Cinematics", CINEMATICS_SCROLL, lineTemplate);
    C_MovieList &movies = g_gameProfile.GetPlayerProfile()->GetMovies();
    for (int i = 0; i < movies.GetCount(); ++i) {
        const C_fstr name("Anim%02d", i);
        int text = 0;
        C_str file;
        movies.Get(i, &text, &file);
        CloneFrame(name, group, lineTemplate);
        S_ControlDesc *line = list->AddChild(CONTROL_LIST_ITEM, name.c_str());
        AddDescMessage(line, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_MOVIE, static_cast<uintptr_t>(text));
        AddDescMessage(line, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(i));
        AddDescMessage(line, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&OnMoviePick));
    }
    AddDescMessage(list, 0, CONTROL_MSG_REFRESH, 0);
    AddText(desc, "text_list of profiles", STYLE_LABEL, TEXT_MOVIES_LABEL);
    AddText(desc, "text_h&dversion", STYLE_VERSION, TEXT_VERSION);
    S_ControlDesc *last = desc->AddControl(CONTROL_BUTTON, "bload lastsave");
    AddDescMessage(last, 0, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, C_str("Load Last save"));
    AddDescMessage(last, 0, CONTROL_MSG_HIDE, 0);
    AddDescMessage(AddButton(desc, "bload mission", 0x8D0), 0, CONTROL_MSG_HIDE, 0);
    return true;
}

bool C_MainMenu::OnCinematicsHidden(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    PostTo(CINEMATICS_MOVIE, CONTROL_MSG_CLOSE_MOVIE, 0);
    static_cast<C_MainMenu *>(menu)->m_movie = C_str("");
    return true;
}

bool C_MainMenu::OnCinematicsShown(void *, void *, C_Control *, uint32_t, uint32_t)
{
    PostTo(CINEMATICS_LIST, CONTROL_MSG_HIDE_ALL, 0);
    const C_MovieList &movies = g_gameProfile.GetPlayerProfile()->GetMovies();
    for (int i = 0; i < movies.GetCount(); ++i) {
        if (movies.IsSeen(i))
            PostTo(CINEMATICS_LIST, CONTROL_MSG_ITEM_COUNTED, LineId(CINEMATICS_LIST, i + 1));
    }
    return true;
}

bool C_MainMenu::OnMoviePlay(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    if (!control)
        return false;
    static_cast<C_MainMenu *>(menu)->m_state = GAME_STATE_PLAY_AVI;
    return true;
}

bool C_MainMenu::OnMoviePick(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    if (!control)
        return false;
    int text = 0;
    g_gameProfile.GetPlayerProfile()->GetMovies().Get(control->GetIndex(), &text, &self->m_movie);
    PostTo(CINEMATICS_PLAY, CONTROL_MSG_ACTIVATE, 0);
    PostTo(CINEMATICS_MOVIE, CONTROL_MSG_CLOSE_MOVIE, 0);
    PostText(CINEMATICS_MOVIE, CONTROL_MSG_PLAY_MOVIE, 0, self->m_movie);
    return true;
}

// ---- the password screen (part 9h-x) ----------------------------------------------------------------------

namespace {

constexpr int CONTROL_EDIT_BOX = 3;
// The password screen's controls (screen 10's): the box, yes, no.
constexpr uint32_t PASSWORD_SCREEN = static_cast<uint32_t>(MAIN_SCREEN_PASSWORD) << SCREEN_INDEX_SHIFT;
constexpr uint32_t PASSWORD_INPUT = PASSWORD_SCREEN | 0x200000, PASSWORD_YES = PASSWORD_SCREEN | 0x300000,
                   PASSWORD_NO = PASSWORD_SCREEN | 0x400000;
// The step the browser sets while a server's password is asked (else: the parental lock's code).
constexpr int STEP_SERVER_PASSWORD = 7;
// The characters a password may not have.
const char *const PASSWORD_FORBIDDEN_CHARS = " \t>-\"";
// The options screen's parental lock check box.
constexpr uint32_t OPTIONS_PARENTAL_LOCK = 0x18A03000;
// The texts: the question, the box's, yes, no; the box's style.
constexpr uint32_t TEXT_PASSWORD_QUESTION = 0x7D3, TEXT_PASSWORD_BOX = 0x7D2, TEXT_YES = 0x7D1, TEXT_NO = 0x7D0;
constexpr uint32_t STYLE_PASSWORD_QUESTION = 0xC, STYLE_PASSWORD_BOX = 0xE;
// The connect screen opened over the browser; the connection's first state.
constexpr uint32_t CONNECT_POPUP = MAIN_SCREEN_CONNECT;
constexpr int CONNECT_START = 0;

// A control's text (GET_TEXT asked of it).
C_str ControlText(C_Control *control)
{
    S_ControlMessage ask;
    ask.id = CONTROL_MSG_GET_TEXT;
    control->HandleMessage(&ask);
    return ask.text;
}

} // namespace

bool C_MainMenu::DescribePasswordScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    model->SetPos(QUIT_MODEL_POS);
    AddText(desc, "text", STYLE_PASSWORD_QUESTION, TEXT_PASSWORD_QUESTION);
    const auto self = reinterpret_cast<uintptr_t>(this);
    S_ControlDesc *input = desc->AddControl(CONTROL_EDIT_BOX, "binput");
    AddDescMessage(input, 0, CONTROL_MSG_SET_FORBIDDEN, 0, reinterpret_cast<uintptr_t>(PASSWORD_FORBIDDEN_CHARS));
    AddDescMessage(input, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_PASSWORD_BOX, TEXT_PASSWORD_BOX);
    AddDescMessage(input, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_RETURN, self, Callback(&OnPasswordInput));
    AddDescMessage(input, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_ESCAPE, self, Callback(&OnPasswordInput));
    AddDescMessage(AddButton(desc, "byes*", TEXT_YES), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,
                   Callback(&OnPasswordInput));
    AddDescMessage(AddButton(desc, "bno*", TEXT_NO), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,
                   Callback(&OnPasswordInput));
    AddDescMessage(desc->AddControl(CONTROL_EDIT_BOX, "binput01"), 0, CONTROL_MSG_HIDE, 0);
    AddDescMessage(desc->AddControl(CONTROL_EDIT_BOX, "binput02"), 0, CONTROL_MSG_HIDE, 0);
    return true;
}

bool C_MainMenu::OnPasswordInput(void *menu, void *argument, C_Control *control, uint32_t notification, uint32_t value)
{
    if (static_cast<C_MainMenu *>(menu)->m_step == STEP_SERVER_PASSWORD)
        return OnServerPassword(menu, argument, control, notification, value);
    return OnParentalLockCode(menu, argument, control, notification, value);
}

bool C_MainMenu::OnServerPassword(void *menu, void *, C_Control *control, uint32_t notification, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    if (!control)
        return false;
    // The password typed (none: nothing): kept, the connection started over the browser.
    const auto connect = [self](const C_str &password) {
        if (password.IsEmpty())
            return false;
        g_mpSettings->password = password;
        g_sessionPasswordSet = true;
        self->m_connectState = CONNECT_START;
        Post(MENU_MSG_CLOSE_POPUP, 0);
        Post(MENU_MSG_OPEN_POPUP, CONNECT_POPUP);
        return true;
    };
    switch (control->GetId()) {
    case PASSWORD_INPUT:
        if (notification == CONTROL_NOTIFY_RETURN) {
            connect(ControlText(control));
        } else if (notification == CONTROL_NOTIFY_ESCAPE) {
            Post(MENU_MSG_DEFERRED | MENU_DEFERRED_DROP, 1);
            Post(MENU_MSG_CLOSE_POPUP, 0);
        }
        self->m_step = 0;
        return true;
    case PASSWORD_YES: {
        // (Fix: no box: no password; the original uses it unchecked.)
        C_Control *input = self->FindControl(PASSWORD_INPUT);
        if (input && connect(ControlText(input)))
            self->m_step = 0;
        return true;
    }
    case PASSWORD_NO:
        Post(MENU_MSG_DEFERRED | MENU_DEFERRED_DROP, 1);
        Post(MENU_MSG_CLOSE_POPUP, 0);
        self->m_step = 0;
        return true;
    default:
        return false;
    }
}

bool C_MainMenu::OnParentalLockCode(void *menu, void *, C_Control *control, uint32_t notification, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    if (!control)
        return false;
    C_GameConfig *config = g_gameProfile.m_config;
    // The code typed: the lock cleared with it (set: the code must match) or set; the options' check box,
    // the kept messages posted, the popup closed.
    const auto apply = [config](const C_str &code) {
        if (config->IsParentLockSet())
            config->ClearParentLock(code);
        else
            config->SetParentLock(code);
        PostTo(OPTIONS_PARENTAL_LOCK, CONTROL_MSG_SET_CHECKED_QUIET, config->IsParentLockSet());
        Post(MENU_MSG_DEFERRED | MENU_DEFERRED_POST, 0);
        Post(MENU_MSG_CLOSE_POPUP, 0);
    };
    // Cancelled: the check box as the lock is, the kept messages dropped (count: 0 by Escape, 1 by no).
    const auto cancel = [config](uint32_t count) {
        PostTo(OPTIONS_PARENTAL_LOCK, CONTROL_MSG_SET_CHECKED_QUIET, config->IsParentLockSet());
        Post(MENU_MSG_DEFERRED | MENU_DEFERRED_DROP, count);
        Post(MENU_MSG_CLOSE_POPUP, 0);
    };
    switch (control->GetId()) {
    case PASSWORD_INPUT:
        if (notification == CONTROL_NOTIFY_RETURN)
            apply(ControlText(control));
        else if (notification == CONTROL_NOTIFY_ESCAPE)
            cancel(0);
        break;
    case PASSWORD_YES: {
        // (Fix: no box: an empty code; the original uses it unchecked.)
        C_Control *input = self->FindControl(PASSWORD_INPUT);
        apply(input ? ControlText(input) : C_str(""));
        break;
    }
    case PASSWORD_NO:
        cancel(1);
        break;
    default:
        return false;
    }
    self->m_step = 0;
    return true;
}

// ---- the filters screen (part 9h-xi) ----------------------------------------------------------------------

namespace {

// The controls' kinds GetControlValue asks: check boxes, spin controls, highlight check boxes, check groups
// (their state / pick), titled edit boxes (their number), titled combo boxes (their text's value).
constexpr int CONTROL_CHECK_BOX = 9, CONTROL_TITLED_EDIT = 0xE, CONTROL_SPIN = 0x10, CONTROL_HIGHLIGHT_CHECK = 0x24,
              CONTROL_CHECK_GROUP = 0x27, CONTROL_TITLED_COMBO = 0x28;
// The filters screen's controls (screen 11's): the game type, the ping, the six check boxes.
constexpr uint32_t FILTERS_SCREEN = static_cast<uint32_t>(MAIN_SCREEN_FILTERS) << SCREEN_INDEX_SHIFT;
constexpr uint32_t FILTERS_GAME_TYPE = FILTERS_SCREEN | 0x100000, FILTERS_PING = FILTERS_SCREEN | 0x200000,
                   FILTERS_FIRST_CHECK = FILTERS_SCREEN | 0x300000;
constexpr uint32_t CONTROL_INDEX_STEP = 0x100000;
// The game types listed (-1: any .. 3).
constexpr int FIRST_GAME_TYPE = -1, GAME_TYPE_END = 4;
// The ping box's range, its style; no ping filter shown as the most.
constexpr uint32_t PING_MAX = 9999, STYLE_PING = 0xD;
// The browser's server list (cleared on accept); the browser's step then (refresh).
constexpr uint32_t BROWSER_SERVER_LIST = 0x20900000;
constexpr int STEP_REFRESH_SERVERS = 6;

// The six check boxes (in order) and their filter flags.
bool S_ServerFilter::*const FILTER_CHECKS[] = {&S_ServerFilter::hideUnreachable, &S_ServerFilter::dedicatedOnly,
                                               &S_ServerFilter::hideEmpty,       &S_ServerFilter::hideFull,
                                               &S_ServerFilter::noPassword,      &S_ServerFilter::sameVersion};
const struct {
    const char *name;
    uint32_t text;
} FILTER_CHECK_NAMES[] = {{"responding only", 0xA00}, {"dedicated only", 0xA01},   {"that are not empty", 0xA02},
                          {"that are not full", 0xA03}, {"without password", 0xA04}, {"current version", 0xA13}};

S_ControlMessage FilterMessage(uint32_t id, uint32_t value, uintptr_t value2 = 0)
{
    S_ControlMessage message;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    return message;
}

} // namespace

void C_MainMenu::SendToControl(uint32_t id, const S_ControlMessage &message)
{
    if (C_Control *control = FindControl(id))
        control->HandleMessageCopy(message);
}

int C_MainMenu::GetControlValue(uint32_t id)
{
    C_Control *control = FindControl(id);
    if (!control)
        return -1;
    S_ControlMessage ask;
    switch (control->GetType()) {
    case CONTROL_CHECK_BOX:
    case CONTROL_HIGHLIGHT_CHECK:
    case CONTROL_CHECK_GROUP:
    case CONTROL_SPIN:
        ask.id = CONTROL_MSG_GET_CHECKED;
        break;
    case CONTROL_TITLED_EDIT:
        ask.id = CONTROL_MSG_GET_NUMBER;
        control->HandleMessage(&ask);
        return static_cast<int>(ask.value2);
    case CONTROL_TITLED_COMBO:
        ask.id = CONTROL_MSG_GET_TEXT;
        break;
    default:
        return -1;
    }
    control->HandleMessage(&ask);
    return static_cast<int>(ask.value);
}

bool C_MainMenu::DescribeFiltersScreen(S_ScreenDesc *desc, I3D_frame *)
{
    const auto self = reinterpret_cast<uintptr_t>(this);
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, self, Callback(&OnFiltersShown));
    S_ControlDesc *types = desc->AddControl(CONTROL_SPIN, "scan style");
    for (int type = FIRST_GAME_TYPE; type < GAME_TYPE_END; ++type)
        AddDescMessage(types, 0, CONTROL_MSG_ADD_ITEM, static_cast<uint32_t>(GetGameTypeTextId(type)));
    S_ControlDesc *ping = desc->AddControl(CONTROL_TITLED_EDIT, "bshort00");
    AddDescMessage(ping, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON, 0x9FB);
    AddDescMessage(ping, 0, CONTROL_MSG_SET_RANGE, 0, PING_MAX);
    AddDescMessage(ping, 0, CONTROL_MSG_SET_DIGITS_ONLY, 1);
    for (const auto &check : FILTER_CHECK_NAMES)
        AddDescMessage(desc->AddControl(CONTROL_HIGHLIGHT_CHECK, check.name), 0, CONTROL_MSG_SET_TEXT_ID, STYLE_BUTTON,
                       check.text);
    S_ControlDesc *accept = AddButton(desc, "baccept", 0x9FC);
    AddDescMessage(accept, 0, FROM_SCREEN | MENU_MSG_BACK, 0);
    AddDescMessage(accept, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&OnFiltersAccept));
    AddMenuButton(desc, "bback", 0x9FD, MENU_MSG_BACK, 0);
    AddText(desc, "text_screen", STYLE_TITLE, 0x9F7);
    AddText(desc, "text_h&dversion", STYLE_VERSION, TEXT_VERSION);
    AddText(desc, "text_scan style", STYLE_LABEL, 0x9FA);
    AddText(desc, "text_show servers", STYLE_LABEL, 0x9F8);
    return true;
}

bool C_MainMenu::OnFiltersShown(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    const S_ServerFilter &filter = self->m_mpSettings->m_filter;
    self->SendToControl(FILTERS_GAME_TYPE, FilterMessage(CONTROL_MSG_SELECT_ITEM, static_cast<uint32_t>(filter.gameType + 1)));
    self->SendToControl(FILTERS_PING, FilterMessage(CONTROL_MSG_SET_NUMBER, STYLE_PING,
                                                   static_cast<uintptr_t>(filter.maxPing < 1 ? PING_MAX : filter.maxPing)));
    uint32_t id = FILTERS_FIRST_CHECK;
    for (bool S_ServerFilter::*check : FILTER_CHECKS) {
        self->SendToControl(id, FilterMessage(CONTROL_MSG_SET_CHECKED, filter.*check));
        id += CONTROL_INDEX_STEP;
    }
    return true;
}

bool C_MainMenu::OnFiltersAccept(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_MainMenu *>(menu);
    S_ServerFilter &filter = self->m_mpSettings->m_filter;
    // Each value changed: kept, the filter marked changed.
    const int type = self->GetControlValue(FILTERS_GAME_TYPE);
    if (type != filter.gameType + 1) {
        filter.changed = true;
        filter.gameType = type - 1;
    }
    const int ping = self->GetControlValue(FILTERS_PING);
    if (ping != filter.maxPing) {
        filter.maxPing = ping;
        filter.changed = true;
    }
    uint32_t id = FILTERS_FIRST_CHECK;
    for (bool S_ServerFilter::*check : FILTER_CHECKS) {
        const bool on = self->GetControlValue(id) != 0;
        if (on != filter.*check) {
            filter.*check = on;
            filter.changed = true;
        }
        id += CONTROL_INDEX_STEP;
    }
    self->m_mpSettings->Save();
    // The browser's list emptied, its servers asked for again.
    S_ControlMessage clear;
    clear.target = BROWSER_SERVER_LIST;
    clear.id = CONTROL_MSG_CLEAR;
    self->HandleMessage(&clear);
    self->m_step = STEP_REFRESH_SERVERS;
    return true;
}
