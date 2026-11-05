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
struct S_ServerStatus;
struct S_MpMapGroup;

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
// The server status the game info screen shows next (its query's thread sets it under the menu's lock).
extern S_ServerStatus *g_gameInfoStatus;            // 0x008acab8
// The connect screen's console style (made by its description; never released).
extern int g_connectStyle;                          // 0x0083fdd0
// The browser's time since the network was last polled (ms; part 9h-xx).
extern int g_netPollTime;                           // 0x0083fe08

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
    // The missions screen: "video" off, "screen_shot" given its own mesh; the groups GB_Campaign and
    // GB_SingleMission made under the model (none: false); the texts, the picture, the description, load / exit,
    // the scroll bar; the templates blong and bshort under GB_Campaign (the campaigns' list), blong under
    // GB_SingleMission (the single missions'); load the last save (off), the version. True.
    bool DescribeMissionsScreen(S_ScreenDesc *desc, I3D_frame *model);      // 0x006392b0
    // The campaigns' list (a hiding list GB_Campaign): each campaign's operations (copies of the operation
    // template "Campaign%02d", numbered from the first campaign's missions' count for the second) and their
    // missions (copies of the mission template "Mission%02d", numbered through), picked: OnMissionPick.
    void DescribeCampaignList(S_ScreenDesc *desc, I3D_frame *group, I3D_frame *operationTemplate,
                              I3D_frame *missionTemplate);   // 0x00639b40
    // The single missions' list (GB_SingleMission): every campaign's missions but the first campaign's first,
    // copies of the template "SMMission%02d", picked: OnMissionPick.
    void DescribeSingleList(S_ScreenDesc *desc, I3D_frame *group, I3D_frame *missionTemplate);   // 0x0063a1a0
    // A mission's line picked (none: false): the mission (its index through the campaigns) kept, its save slot
    // shown; load on, its picture, the description: the heading, the mission's texts (its text id 0x1194 on:
    // the title, the lines after it up to six more found), carnage's note. True.
    static bool OnMissionPick(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x0063b310
    // The play mode's save slot: its file the saved game picked; "load the last save" on only when the slot's
    // mission is `mission` (another mode: nothing).
    void ShowSaveSlot(int mission);                     // 0x0063b0f0
    // The campaign's missions shown (the campaign started, played before): no save picked, the title by play
    // mode, the campaigns' list scrolled; the progress the player's in the campaign; its operations' lines
    // and their missions' up to the one after the progress.
    void ShowCampaignMissions();                        // 0x0063a670 (__fastcall)
    // The single missions shown: no save picked, the title (mode 0 / carnage), their list scrolled; every
    // campaign's missions (the first's first not) up to the player's best progress there (every one: the
    // secret key's 100).
    void ShowSingleMissions();                          // 0x0063ab60 (__fastcall)
    // An operation's line picked (none: false; no line posts it): its index kept, an operation picked; load
    // on, the description cleared, the operation's name as the picture. True.
    static bool OnOperationPick(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x0063af20
    // The cinematics screen: "video" and "text00" off, "screen_shot" its own mesh; the group GB_Cinematics made
    // under the model (none: false), the templates blong and bshort off (fix: missing: false); hidden:
    // OnCinematicsHidden, shown: OnCinematicsShown; the title, the picture, the movie, play (off; OnMoviePlay),
    // back, the scroll bar, the movies' list (a line per movie: copies of blong "Anim%02d", its text, picked:
    // OnMoviePick), the list's label, the version; load the last save and load hidden. True.
    bool DescribeCinematicsScreen(S_ScreenDesc *desc, I3D_frame *model);         // 0x00614140
    // The cinematics screen hidden: the movie closed, none picked. True.
    static bool OnCinematicsHidden(void *menu, void *argument, C_Control *control, uint32_t notification,
                                   uint32_t value);     // 0x00615660
    // Shown: the movies' lines hidden but those of the movies seen. True.
    static bool OnCinematicsShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                                  uint32_t value);      // 0x00615820
    // Play (none: false): the movie picked played over the menu (GAME_STATE_PLAY_AVI). True.
    static bool OnMoviePlay(void *menu, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x00615450
    // A movie's line picked (none: false): its file kept (m_movie), play on, the movie closed and the file
    // played in the screen's movie control. True.
    static bool OnMoviePick(void *menu, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x00615470
    // The browser (part 9h-xix): its tick (OnBrowserTick), shown (OnBrowserShown); the title; the sort arrows and
    // the list's headers (OnServerSort); the server list (DescribeServerList); restore / refresh the list
    // (OnRestoreList, OnRefreshList); the address popup (off), the filters, join (off), create a server, the game
    // info (off), back (the list emptied); the version, the logos, the online line; the favourites
    // (OnFavoriteToggle, OnFavorites). True.
    bool DescribeBrowserScreen(S_ScreenDesc *desc, I3D_frame *model);       // 0x00634050
    // The list's frames: the group GB_ServerList made under the model, the row "sever00" moved into it (none:
    // false), off, and copied ("Server1" .. "Server11", off); the scroll bar and the list (picked:
    // OnServerPicked, 13: OnServerJoin, changed: OnServerCountChanged). True.
    bool DescribeServerList(S_ScreenDesc *desc, I3D_frame *model);          // 0x00634580
    // Shown: no single mission. True.
    static bool OnBrowserShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                               uint32_t value);         // 0x006356d0
    // The screen's tick (part 9h-xx; other states: nothing): the list shown again (emptied, its lines from the
    // list shown, idle; no more); pinging the reachable ones: those answering listed once; else the servers
    // found listed (ListFoundServers). A search with no answer for 15 s stopped (its button's), the list
    // sorted every second, the network polled every 10 ms. True.
    static bool OnBrowserTick(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00635ba0
    // A server's line added (the servers found filtered: unreachable, the ping, the game type, empty, full,
    // dedicated, a password, the version).
    void AddServerLine(S_ServerEntry *server, int unused = 0);   // 0x00635f10
    // A server found kept (under the lock).
    void AddFoundServer(S_ServerEntry *server);         // 0x00635990
    // The searches' and pings' answers (the net thread's): kept while the main menu runs and no password was
    // asked.
    static void __fastcall OnServerFound(S_ServerEntry *server);   // 0x00635b70
    // The servers found taken (the last first; LAN games: one an address, the others dropped unless listed):
    // searching, the new ones listed; pinging, the listed ones answering shown again.
    void ListFoundServers();                            // 0x00635710 (__fastcall)
    // The arrows (the direction toggled) or a header (its key; already the key: the arrows flipped). No
    // control, another: false.
    static bool OnServerSort(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00636640
    // The pinged server's answer: nothing.
    static void __fastcall IgnoreServer(S_ServerEntry *server);   // 0x00635980
    // A server picked (m_server; none: nothing more): pinged, join and the game info on; listing the
    // Internet's: the favourite button on. True.
    static bool OnServerPicked(void *menu, void *argument, C_Control *control, uint32_t notification,
                               uint32_t value);         // 0x006360c0
    // A server picked: the join messages posted (as the join button's). True.
    static bool OnServerJoin(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00636280
    // The online line: the list's entries / the first list's servers, their players. True.
    static bool OnServerCountChanged(void *menu, void *argument, C_Control *control, uint32_t notification,
                                     uint32_t value);   // 0x006364b0
    // The browser's buttons (part 9h-xx). "REFRESH LIST": idle: the list shown pinged (its servers not answered
    // yet; with the unreachable ones hidden, emptied), the buttons off, "STOP"; pinging: stopped (sorted, its
    // text, the LAN's button on with the servers found). "UPDATE LIST": idle: the LAN searched (the servers
    // dropped, the list emptied); searching: stopped (likewise; the ping's button on with Internet games). A
    // favourite (none picked: false): the servers found: copied to the favourites; the favourites: removed,
    // the list again. The favourites: the other list shown (the searches stopped, the buttons, the texts), the
    // list again. True.
    static bool OnRestoreList(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00634f90
    static bool OnRefreshList(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00635360
    static bool OnFavoriteToggle(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);       // 0x00634950
    static bool OnFavorites(void *menu, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x00634b30
    // The create server screen: shown / hidden: OnCreateServerShown / OnCreateServerHidden; the host's name
    // (OnHostNameChanged), the game type (OnServerStyleChanged), the maps panel, the options panel, the voice
    // chat's codec, start (off; MAIN_MSG_START_SERVER), back, the texts, the map list presets
    // (OnMapPresetPicked). Not startable yet. True.
    bool DescribeCreateServerScreen(S_ScreenDesc *desc, I3D_frame *model);  // 0x00628610
    // The maps panel: its groups (one per game type, the picked maps'), the templates bmap_A / bmap_B and the
    // scroll bars moved there; each game type's maps (DescribeMapList), the picked ones. A frame missing: false.
    bool DescribeMapsPanel(S_ScreenDesc *desc, I3D_frame *model);   // 0x00628f80
    // A game type's maps: a hiding list (the group's name; hidden) of a line per map ("MPMap%02d": its name,
    // its index; dragged and double-clicked: the map line callbacks).
    void DescribeMapList(S_ControlDesc *panel, const S_MpMapGroup &maps, I3D_frame *group,
                         I3D_frame *lineTemplate);   // 0x00629bc0
    // The picked maps: a pool list (shown) of 50 lines, all hidden. (`maps` unused: DescribeMapList's signature; the
    // original passes the cooperative maps.)
    void DescribePickedMapList(S_ControlDesc *panel, const S_MpMapGroup &maps, I3D_frame *group,
                               I3D_frame *lineTemplate);   // 0x00629740
    // The picked list: the current group's maps (the one dragged, by the hint's index, current), wrapped in
    // leave / 0x05 notifications when nothing is dragged.
    void ShowPickedMaps();                              // 0x0062c780 (__fastcall)
    // Startable: the current group has maps and the server a name; start on or off.
    void RefreshStartButton();                          // 0x0062cbf0 (__fastcall)
    // The game type's map list shown (its scroll bar on it, refreshed), the others hidden.
    void ShowMapList(int type);                         // 0x0062ce10
    // The options from the current rules; inverse damage / the respawn time on by friendly fire / respawning;
    // the rows of the game type on (deathmatch, team play, hidden maps: 2 .. 32 players; cooperative: 2 .. 6,
    // respawning forced, its difficulty, lives, team lives).
    void ShowServerOptions(int type);                   // 0x0062d950
    // A titled edit box's text (none, another kind: empty).
    C_str GetEditText(uint32_t id);                     // 0x0062c6d0
    // The hosted session from the create server screen (OnNetworkMessage's start): the settings reset, hosting;
    // the server's and the player's names; the game type, internet, dedicated, the cooperative rules (another
    // type: difficulty 3, unlimited lives, no team lives), the rules, the password, the port, the voice codec;
    // copies of the rotation's maps.
    void SetUpHostedSession();                          // 0x0062d030
    // (The two lists' helper: a map line's callbacks: double click, drag started, moved, dropped.)
    static void AddMapLineCallbacks(S_ControlDesc *line, uintptr_t self);
    // The options panel: the internet / dedicated check boxes, its scroll bar, its list of rows (copies of the
    // templates yes_no_tmpl, bshort00, binput, combo_tmpl under gr_setings): the cooperative difficulty, the
    // numbers (players, lives, limits, times), the yes / no rules, the password, the port. A frame missing:
    // false.
    bool DescribeOptionsPanel(S_ScreenDesc *desc, I3D_frame *model);   // 0x0062a060
    // (The create server screen's callbacks: a later sub-part.)
    // Shown (part 9h-xiv): the settings loaded; the host's name, the game type, internet (the session's listing
    // kind 1), dedicated, the password, the port, the voice codec, the rotation shown. True.
    static bool OnCreateServerShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                                    uint32_t value);    // 0x0062c2a0
    // Hidden: the settings saved. True.
    static bool OnCreateServerHidden(void *menu, void *argument, C_Control *control, uint32_t notification,
                                     uint32_t value);   // 0x0062c290
    // The host's box edited (notification 0x0B): its text the server's name (none: the name shown again);
    // start refreshed. True.
    static bool OnHostNameChanged(void *menu, void *argument, C_Control *control, uint32_t notification,
                                  uint32_t value);      // 0x0062ca80
    // The game type picked (part 9h-xv; GET_SELECTED's value; none, -1: nothing): set up; the picked maps,
    // start, its map list, its options shown. True.
    static bool OnServerStyleChanged(void *menu, void *argument, C_Control *control, uint32_t notification,
                                     uint32_t value);   // 0x0062cd50
    // An option changed (none: false): internet / dedicated / the voice codec into the setup; a row into the
    // current rules (the difficulty + 1; the password: its text; the port: the setup's), inverse damage and
    // the respawn time then on by friendly fire / respawning. True.
    static bool OnServerOptionChanged(void *menu, void *argument, C_Control *control, uint32_t notification,
                                      uint32_t value);  // 0x0062d490
    // A preset picked (none: nothing): that rotation set up, the picked maps shown, start refreshed. True.
    static bool OnMapPresetPicked(void *menu, void *argument, C_Control *control, uint32_t notification,
                                  uint32_t value);      // 0x0062c970
    // A map line double-clicked (notification 0x13; part 9h-xvi): a picked one taken off the rotation, a list's
    // added to it; the picked maps, start. True.
    static bool OnMapLineDoubleClick(void *menu, void *argument, C_Control *control, uint32_t notification,
                                     uint32_t value);   // 0x0062ca10
    // A drag started (no highlight frame / no copy made: false): a copy of the line's frame in the scene; the
    // menu's hint the drag (its index, from a list or the picked one, the copy, the map's name). True.
    static bool OnMapLineDragStart(void *menu, void *argument, C_Control *control, uint32_t notification,
                                   uint32_t value);     // 0x0062e870
    // Moved (no copy: false): the copy with the cursor; a picked map over another picked line moved there.
    static bool OnMapLineDragMove(void *menu, void *argument, C_Control *control, uint32_t notification,
                                  uint32_t value);      // 0x0062e9f0
    // Dropped: the copy released; a picked map over the lists taken off, a list's over the picked ones added
    // (moved to the line under the cursor); the drag ended, the picked maps, start. True.
    static bool OnMapLineDrop(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x0062eb60
    // The password screen (over the browser: a server's password; over the options: the parental lock's code):
    // the model placed as the quit question's; the question, the box (no blanks, '>', '-', '"'; Return and
    // Escape: OnPasswordInput), yes and no (the same), the two other boxes hidden. True.
    bool DescribePasswordScreen(S_ScreenDesc *desc, I3D_frame *model);      // 0x00637890
    // The box's and the buttons' callback: a server's password while m_step is 7, else the parental lock's.
    static bool OnPasswordInput(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x00637a40
    // A server's password (none: false; another control: false): Return in the box / yes: a password typed
    // kept (g_mpSettings, g_sessionPasswordSet), the connection started (the popup replaced by the connect
    // screen); Escape / no: the kept messages dropped, the popup closed. The step ended (yes with no password:
    // not). True.
    static bool OnServerPassword(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);       // 0x00637c10
    // The parental lock's code (none / another control: false): Return in the box / yes: the lock cleared with
    // the code (when set) or set with it, the options' check box as the lock is, the kept messages posted,
    // the popup closed; Escape / no: the check box, the kept messages dropped, closed. The step ended. True.
    static bool OnParentalLockCode(void *menu, void *argument, C_Control *control, uint32_t notification,
                                   uint32_t value);     // 0x00637fd0
    // The filters screen (the model unused): shown: OnFiltersShown; the game types (any, 0 .. 3), the ping
    // box (digits, 0 .. 9999), six check boxes (responding, dedicated, not empty, not full, no password, the
    // current version), accept (back; OnFiltersAccept), back, the texts. True.
    bool DescribeFiltersScreen(S_ScreenDesc *desc, I3D_frame *model);       // 0x00638660
    // Shown: the controls set from m_mpSettings' filter (no ping filter: 9999). True.
    static bool OnFiltersShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                               uint32_t value);         // 0x00638aa0
    // Accept: the filter set from the controls (a change marks it changed), saved; the browser's server list
    // cleared, the browser's step 6 (the servers asked for again). True.
    static bool OnFiltersAccept(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x00638910
    // A copy of `message` handled by the control `id` (none: nothing).
    void SendToControl(uint32_t id, const S_ControlMessage &message);   // 0x0062c5b0
    // The control `id`'s value: check boxes, spin controls, check groups: their state (GET_CHECKED's value);
    // titled edit boxes: their number (GET_NUMBER's value2); titled combo boxes: GET_TEXT's value; another
    // kind or none: -1.
    int GetControlValue(uint32_t id);                   // 0x0062c5d0
    // The game info screen (the model unused): its twelve styles made (g_gameInfoStyles); shown and "brefresh
    // status": OnGameInfoRefresh, the tick: OnGameInfoTick; the players' box, the status text, join (the
    // browser's join messages), return, the texts, the players' header, the teams' scores. True.
    bool DescribeGameInfoScreen(S_ScreenDesc *desc, I3D_frame *model);      // 0x0062f4b0
    // No server picked: false. The boxes emptied, "Reading Data from server ...", the axis' score hidden in a
    // cooperative game; the server's status asked for (OnGameInfoStatus). True.
    static bool OnGameInfoRefresh(void *menu, void *argument, C_Control *control, uint32_t notification,
                                  uint32_t value);      // 0x006319a0
    // The query's answer (its thread): kept for the tick while the main menu runs and no password was asked.
    static void __fastcall OnGameInfoStatus(S_ServerStatus *status);   // 0x00631c60
    // The screen's tick: a status kept shown (ShowGameInfo), then dropped. True.
    static bool OnGameInfoTick(void *menu, void *argument, C_Control *control, uint32_t notification,
                               uint32_t value);         // 0x00631c20
    // On the game info screen with a server picked: the players (the last first: a team's mark, name,
    // kills, points), the teams' scores, the server's lines (name, address, ping, players, its flags) and
    // its rules (the cooperative ones too).
    static void ShowGameInfo(const S_ServerStatus *status);   // 0x0062fa30 (__stdcall)
    // The connect screen (part 9h-xviii): the model placed as the quit question's; its tick: OnConnectTick,
    // hidden: OnConnectHidden; the console text, yes / no hidden, OK (cancel: OnConnectMessage); its style. True.
    bool DescribeConnectScreen(S_ScreenDesc *desc, I3D_frame *model);       // 0x00632150
    // Hidden: the console emptied, no wait. True.
    static bool OnConnectHidden(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x00632270
    // The screen's tick (another screen's, hidden: false): waiting (the tick's time off); else the connection's
    // state: start (the squad and roster cleared; hosting: the dedicated server's or the server's host; joining:
    // connect; else an error), connect, joining (connected: done; refused: its lines, failed), host / host the
    // dedicated server (failed: its lines), hosted (the network handler's start / the menu quits). True.
    static bool OnConnectTick(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00632560
    // The add server popup (part 9h-xxiii): the model placed as the quit question's; "ENTER THE IP ADDRESS", the
    // address (255.255.255.255, 15 characters at most) and port (11001, digits) boxes, yes / no
    // (OnAddServerButton), the third box hidden. True.
    bool DescribeAddServerScreen(S_ScreenDesc *desc, I3D_frame *model);     // 0x006370c0
    // Yes: both boxes filled, a server at the address (its first number the lowest byte) and port added to the
    // list shown, its line; yes / no: closed, idle. True; another control (none): false.
    static bool OnAddServerButton(void *menu, void *argument, C_Control *control, uint32_t notification,
                                  uint32_t value);      // 0x00637510

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
    // The network's messages (part 9h-xxi): LAN / Internet games (the settings, the browser opened; LAN: searched;
    // Internet: the server list read, shown again); the server started (the next map); leaving the browser (the
    // Internet's servers saved); the create server screen's start (startable: the settings saved, the session set
    // up, the firewall asked about); its answers; the connection started; joining the server picked (a password
    // asked for); the LAN searched, the list pinged; the searches stopped (none: the network closed); Escape on the
    // browser. True; others false.
    bool OnNetworkMessage(S_ControlMessage *message);   // 0x00633490
    // The join's password asked for: the password popup, its box emptied and edited.
    void AskJoinPassword();                             // 0x00637a60 (__fastcall)
    // The missions' messages: a mission started (none picked: nothing): a single mission set up in its
    // campaign, else the save before it loaded (not the campaigns' first missions), the campaign advanced;
    // the state ended. The single-mission flag / the play mode set (value). The campaign started: played
    // before, its missions shown; else the roster loaded when empty (failing: false), the next step. The
    // single missions opened. The secret key on the single missions: all of them shown or not. A popup
    // opened: taken. True; others false.
    bool OnMissionsMessage(S_ControlMessage *message);  // 0x00638e00
    // Escape (its OK's too) on the connect screen (the top): closed, 0x2000028 posted, start refreshed.
    // True; others false.
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
    bool m_openFirewall = false;                        // +0x1F0 the host opens the firewall's ports
    int m_connectState = 5;                             // +0x1F4 the connect screen's state (0: starting)
    bool m_singleMissions = false;                      // +0x1F8 the missions screen shows the single missions
    int m_connectWait = 0;                              // +0x1FC the connect screen's wait (ms)
    int m_searchTime = 0;                               // +0x200 the browser's search: since the last answer (ms)
    int m_sortTime = 0;                                 // +0x204 the browser's list: since sorted (ms)
    int m_mission = -1;                                 // +0x208 the missions screen's pick (through the campaigns)
    int m_progress = -1;                                // +0x20C the missions shown up to (100: all)
    bool m_operationPicked = false;                     // +0x210 an operation's line picked (a mission's clears it)
    bool m_startable = false;                           // +0x212 the create server screen's start allowed
    C_str m_profileName;                                // +0x213 the profile picked (unaligned in the original)
    int unknown218 = 0;                                 // +0x218
    S_ServerEntry *m_server = nullptr;                  // +0x21C the browser's server picked
};
