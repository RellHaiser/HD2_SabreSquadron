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
        case MAIN_SCREEN_SAVES: DescribeSavesScreen(desc, model); break;
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
