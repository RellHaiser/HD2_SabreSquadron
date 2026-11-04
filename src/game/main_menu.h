// The main menu (game/main_menu.cpp, part 9h; vtable 0x00815e30, 0x240 bytes; the menu state's type 2): its
// screens (described in code, built by kind), the profile and single-player screens, the options, the
// multiplayer browser, the server's set-up and the network screens. Part 9h-iii: the class, starting,
// stopping, the update, the messages and the building of the screens; the screens' descriptions and
// handlers follow in the later sub-parts.
#pragma once

#include <cstdint>
#include <vector>

#include <windows.h>

#include "common/c_str.h"
#include "game/menu_state.h"

class C_Control;
class C_MpMenuSettings;
class C_OptionsHandler;
class I3D_frame;
struct S_MenuDesc;
struct S_ScreenDesc;
struct S_ServerEntry;

// The main menu's screens (the ids' top 6 bits; index 0 is the message box).
enum : uint32_t {
    MAIN_SCREEN_MAIN = 1,                           // "MAIN MENU"
    MAIN_SCREEN_PROFILE = 2,                        // "CHANGE PROFIL"
    MAIN_SCREEN_SINGLEPLAYER = 3,                   // "SINGLEPLAYER"
    MAIN_SCREEN_QUIT = 4,                           // "CONFIRM" (the quit question, QUIT_SCREEN)
    MAIN_SCREEN_MISSIONS = 5,                       // "SINGLE MISSION 2" (the campaign's missions)
    MAIN_SCREEN_OPTIONS = 6,                        // "OPTIONS"
    MAIN_SCREEN_CINEMATICS = 7,                     // "SINGLE MISSION 2" (the cinematics)
    MAIN_SCREEN_BROWSER = 8,                        // "multiplayer menu"
    MAIN_SCREEN_CREATE_SERVER = 9,                  // "create server"
    MAIN_SCREEN_PASSWORD = 10,                      // "PASSWORD"
    MAIN_SCREEN_FILTERS = 11,                       // "Server Filters"
    MAIN_SCREEN_GAME_INFO = 12,                     // "Game info"
    MAIN_SCREEN_CONNECT = 13,                       // "MP Connect console" (on the "confirm" model)
    MAIN_SCREEN_ADD_SERVER = 14,                    // "ADD SERVER" (on the "password" model)
};
// The menu's messages of its own (target 0).
enum : uint32_t {
    MAIN_MSG_PLAY_MOVIE = 0x200003A,                // the menu's movie (m_movie; empty: the credits)
    MAIN_MSG_LOAD_GAME = 0x2000075,                 // the saved game picked (m_saveName) loaded
};

// The game info screen's text styles (made by its description, released when the menu stops).
constexpr int GAME_INFO_STYLES = 12;
extern int g_gameInfoStyles[GAME_INFO_STYLES];      // 0x008aca88

class C_MainMenu : public C_MenuState {
public:
    // (Inline in C_GameApp::StartStep and the state changes.)
    C_MainMenu() = default;
    // (The options handler and the settings are deleted by OnStop.)
    ~C_MainMenu() override = default;               // 0x006c19a0 (0x006c1980)
    C_MainMenu(const C_MainMenu &) = delete;
    C_MainMenu &operator=(const C_MainMenu &) = delete;

    int GetTypeId() const override { return GAME_STATE_TYPE_MAIN_MENU; }   // 0x1C 0x006c1970

    // In turn to the profile screens', the network screens', the missions', the options' and the
    // connection's handlers (one taking it: done). Then Escape: on the profile screen the profile picked
    // made current and back; on the password or the add-server popup the kept messages dropped and the
    // popup closed; on the quit question likewise (its own ids); else back. MAIN_MSG_PLAY_MOVIE: the
    // movie's state; MAIN_MSG_LOAD_GAME: the saved game picked loaded (none: the application's load
    // command alone).
    void OnMessage(S_ControlMessage *message) override; // 0x34 0x00626ca0
    // The screens described (Describe), their models loaded (none: skipped), each described further by
    // its kind (the quit question's model moved to (0.5, -0.375, -0.5)), made and built; the main screen
    // opened. True.
    bool BuildScreens() override;                       // 0x38 0x006271e0
    // The servers found and not listed deleted; the settings and the options handler deleted; the game
    // info's styles released; the lock deleted.
    void OnStop() override;                             // 0x3C 0x00626ad0
    // The lock made, the servers found dropped; the multiplayer settings made (the map list read), the
    // options handler; the date shown on the main screen. True.
    bool OnStart(void *arg) override;                   // 0x40 0x006264c0
    // The date shown again (when known).
    void OnUpdate(int timeDelta) override;              // 0x48 0x00626b30

    // The movie MAIN_MSG_PLAY_MOVIE plays (empty: the credits).
    const C_str &GetMovie() const { return m_movie; }

protected:
    // The servers the browser's thread found (under the lock), from the last: those in neither of the
    // settings' lists deleted, all taken off.
    void DrainFoundServers();                           // 0x00636400 (__fastcall)
    // The menu's screens' descriptions (owned by the caller): the main screen (its texts, the buttons: resume,
    // the profile, single player, the LAN / Internet games, the cinematics, the options, the credits, exit),
    // the profile screen (the name's box: no ./\*?|<>:" characters, 17 at most but in Japanese; the profiles'
    // list, the difficulties, create / rename / delete, back), the single-player screen (the side switch's
    // frames ss1 .. ss8, the modes' buttons), the quit question (yes, no, ok hidden); the other screens
    // empty (their kinds' functions describe them).
    S_MenuDesc *Describe();                             // 0x00623e30

    // ---- the screens' callbacks (later sub-parts but OnMainShown) ----
    static bool OnMainShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x006276a0
    // The profile screen shown: refreshed; the name's box, the list's refresh, the difficulties off; create on,
    // rename / delete only with a profile picked; back on. True.
    static bool OnProfileShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                               uint32_t value);         // 0x006237e0
    static bool OnProfileButton(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x006214d0
    // The profiles' list (another control: false): a click: its profile (a name) made current, refreshed; a
    // double click (13) on the current one: back. True.
    static bool OnProfileList(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00623b40
    // A difficulty picked while creating a profile (else false): the list on, the difficulties off, the
    // buttons on; the difficulty set (easy 1 .. very hard 4; another control: false), its button white, the
    // step ended, the player's profile saved. True.
    static bool OnDifficulty(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00622b50
    static bool OnSingleplayerShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                                    uint32_t value);    // 0x0063b7f0
    static bool OnSingleplayerButton(void *menu, void *argument, C_Control *control, uint32_t notification,
                                     uint32_t value);   // 0x0063b9a0

    // ---- the screens' further descriptions (by kind; later sub-parts) ----
    // The profile screen: the profiles found (ScanProfiles) as the list's lines (its second control). True.
    bool DescribeProfileScreen(S_ScreenDesc *desc, I3D_frame *model);       // 0x00621210
    bool DescribeMissionsScreen(S_ScreenDesc *desc, I3D_frame *model);      // 0x006392b0
    bool DescribeCinematicsScreen(S_ScreenDesc *desc, I3D_frame *model);         // 0x00614140
    bool DescribeBrowserScreen(S_ScreenDesc *desc, I3D_frame *model);       // 0x00634050
    bool DescribeCreateServerScreen(S_ScreenDesc *desc, I3D_frame *model);  // 0x00628610
    bool DescribePasswordScreen(S_ScreenDesc *desc, I3D_frame *model);      // 0x00637890
    bool DescribeFiltersScreen(S_ScreenDesc *desc, I3D_frame *model);       // 0x00638660
    bool DescribeGameInfoScreen(S_ScreenDesc *desc, I3D_frame *model);      // 0x0062f4b0
    bool DescribeConnectScreen(S_ScreenDesc *desc, I3D_frame *model);       // 0x00632150
    bool DescribeAddServerScreen(S_ScreenDesc *desc, I3D_frame *model);     // 0x006370c0

    // ---- the screens' message handlers (true: taken; later sub-parts) ----
    // The profile screen's messages: Escape while a difficulty is picked: taken. CONTROL_MSG_REMOVE_SELECTED (the
    // deletion confirmed): the profile picked deleted (failing: nothing), its line removed, the line now selected
    // picked (none: no name), made current, refreshed. 0x20000C3: the name's box edited. True; others false.
    bool OnProfileMessage(S_ControlMessage *message);   // 0x00621240
    // The profile screen's controls by the profile picked: none: no name, no difficulty white, rename and delete
    // off; else its name, its line selected, its difficulty's button white, rename and delete on.
    void RefreshProfile();                              // 0x00622ed0 (__fastcall)
    // ScanProfiles' callbacks: a profile's line added to the list's description (`context`) / posted to the
    // list (part 9h-vi's).
    static void AddProfileLine(const char *name, int context);    // 0x00621110 (__fastcall)
    static void PostProfileLine(const char *name, int context);   // 0x00620fb0 (__fastcall)
    bool OnNetworkMessage(S_ControlMessage *message);   // 0x00633490
    bool OnMissionsMessage(S_ControlMessage *message);  // 0x00638e00
    bool OnConnectMessage(S_ControlMessage *message);   // 0x00632390
    // The profile screen's pick: the profile named m_profileName (none: no player) made current, the
    // options to be filled again, the game reset. True. (A button's callback; OnMessage calls it too.)
    static bool SelectProfile(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00622af0

    CRITICAL_SECTION m_foundLock;                       // +0x1B0 (OnStart .. OnStop)
    // The step of a screen's dialog (the profile screen's: 3 picking a difficulty; the browser's too).
    int m_step = 0;                                     // +0x1C8
    C_OptionsHandler *m_options = nullptr;              // +0x1D0
    std::vector<S_ServerEntry *> m_foundServers;        // +0x1D4 (the browser's thread's)
    C_MpMenuSettings *m_mpSettings = nullptr;           // +0x1E4
    C_str m_movie;                                      // +0x1E8
    C_str m_saveName;                                   // +0x1EC the saved game picked
    bool unknown1F0 = false;                            // +0x1F0
    int unknown1F4 = 5;                                 // +0x1F4
    bool unknown1F8 = false;                            // +0x1F8 (the missions screen's)
    int unknown1FC = 0;                                 // +0x1FC
    int unknown200 = 0;                                 // +0x200
    int unknown204 = 0;                                 // +0x204
    int unknown208 = -1;                                // +0x208 (the missions screen's pick)
    int unknown20C = -1;                                // +0x20C
    bool unknown210 = false;                            // +0x210
    bool unknown212 = false;                            // +0x212
    C_str m_profileName;                                // +0x213 the profile picked (unaligned in the original)
    int unknown218 = 0;                                 // +0x218
    int unknown21C = 0;                                 // +0x21C (the browser's)
};
