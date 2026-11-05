// Declarations of game functionality that ported code calls but whose own source
// files have not been hand-ported yet (their mechanical translation lives in
// src/tier1). Each entry moves to its proper header when its file is ported.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/campaign.h"
#include "game/game_app.h"
#include "game/game_state.h"
#include "game/module.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/car_tables.h"                            // (the tyre and surface tables, ported)

// ---- the application's states (C_GameApp, game/game_app.h) ---------------------
// C_GameState: see game/game_state.h. The menus share one implementation (0x0066b010 ..,
// the base of vtable 0x00815850) and differ in their GetTypeId (the screen they show);
// their constructors are inline in C_GameApp's code, which also holds their GetTypeId and
// destructors. The roles of the campaign screens are inferred from where the shipped
// campaigns put them.

// The video player C_VideoState / C_MovieState: game/video_state.h.
// The main menu (type 2): game/main_menu.h.
#include "game/main_menu.h"
// The equipment screen (kinds 3 and 4): game/equipment_screen.h. The debriefing (kind 5):
// game/debriefing.h.
// The squad screen (kinds 1 and 2): game/squad_menu.h.
// Campaign screen kind 0 (none in the shipped campaigns; type 9): game/hero_stats_menu.h.
#include "game/hero_stats_menu.h"
// The console (type 12, vtable 0x008165a8, 0x214 bytes): the command line's "-cmd" / "-c".
class C_ConsoleState : public C_GameState {
public:
    // `command` runs first (null: none; at most 0x204 chars).
    explicit C_ConsoleState(const char *command);   // 0x007442e0
    // "HD2 console", "Type 'help' for keywords list"; the console shows the messages.
    void ShowBanner();                              // 0x007443a0
    bool IsShown() const;                           // byte +0x210
};
// The next word of a console command line (quotes allowed); 0 when one was read, 3 at the
// end. `*cursor` moves past it.
int ReadConsoleToken(const char **cursor, char *out, int *unknown, char *unknown2);   // 0x007445d0

// ---- the objects of the GEffects module (part 8d) --------------------------
#include "game/decorators.h"

// The volumetrics (the GEffects module's +0xC4) and the clouds: game/volumetrics.h.
#include "game/volumetrics.h"

// The mesh player (the GEffects module's +0xE4): game/mesh_player.h; the birds: game/birds.h.
#include "game/mesh_player.h"

// The GEffects module: game/geffects.h.
#include "game/geffects.h"

// The highest y of the scene's frames' bounds (the top of the box C_NetGame::Command gathers at
// 0x0072d9c0 for the network game; the supply box's drop height stays under it).
extern float g_netSceneTop;                         // 0x0097ca44
// Adds a line to the debug line buffer (at most 15000; `color2` 0: one colour).
void DrawDebugLine(const float *from, const float *to, uint32_t color, uint32_t color2);   // 0x0071fad0

// ---- input/controls (0x008aeb58) ----------------------------------------------
class C_Controls {
public:
    void Update(bool processAll);                   // 0x006c7bc0
    // The frame's values per action: held (+0xCC) or pressed in this frame (+0xDC); their
    // count, -1 when there are none.
    int GetValues(float **values, bool pressed);    // 0x006c7600
    // One action's value (held, or pressed in this frame); 0 out of range.
    float GetValue(int action, bool pressed);       // 0x006c76a0
    // Binds `action` to a device's key (C_KeyBindings; `unknown` is always 0).
    void Bind(int action, int16_t device, uint16_t key, int unknown, int direction);   // 0x006c7790
    // The mouse's divisors (+0xA8 x, +0xAC y, +0xB0 the third; floats).
    bool SetMouseScale(int x, int y, int z);        // 0x006c6fe0
    // The byte at +0xB4 (0x008aec0c), which the player options set (inverted mouse, inferred).
    void SetInvertMouse(bool on);
    // Zeroes the frame's values of the bindings on key `key` (any device; null arrays: its own).
    void ClearKey(int key, float *held, float *pressed);         // 0x006c7510
    // Zeroes the frame's values of the bindings on the same device key as `action`'s.
    void ClearAction(int action, float *held, float *pressed);   // 0x006c73f0
    // The input pressed now, for a key binding (its record at +0x14, reset first; none: null; the
    // devices' state re-read when `reread`; `unknown` 0x9A from the hot key control).
    struct S_Press {
        int direction;                              // +0x00 axes: 1 + / 2 -
        uint16_t device;                            // +0x04 E_BindingDevice
        uint16_t key;                               // +0x06 (keyboard 1: Escape)
        int unknown08;
    };
    const S_Press *PollPress(int unknown, bool reread);   // 0x006c87f0
};
extern C_Controls g_controls;                       // 0x008aeb58

// Wide text to the ANSI code page, at most `maxLength` chars (and a terminator).
void WideToAnsi(char *out, const wchar_t *text, int maxLength);   // 0x006c90c0 (__fastcall)

// Objects registered for a per-frame refresh before the controls update.
class C_FrameListener {
public:
    virtual void OnFrame(bool active);              // vtable +0x58
};
extern C_FrameListener **g_frameListenersBegin;     // 0x008aeb4c (std::vector data)
extern C_FrameListener **g_frameListenersEnd;       // 0x008aeb50

// ---- networking (0x0097d070) ---------------------------------------------------
class C_BitStream;
enum E_NetChannel {
    NET_CHANNEL_SCRIPT = 4,
};
// Delivery modes of C_NetGame::CreateMessage (exact meaning of 3 not identified).
enum E_NetDelivery {
    NET_DELIVERY_RELIABLE = 0,
    NET_DELIVERY_UNRELIABLE = 1,
    NET_DELIVERY_3 = 3,
};
struct S_ServerEntry;
// A server's status (the net module's answer to QueryServerStatus; read by the main menu's game info screen,
// part 9h-xii): its players, its rules, the teams' scores.
struct S_StatusPlayer {                                 // 0x5C bytes
    int unknown00;
    int kills;                                          // +0x04 (the player's stats, NET_STAT_*)
    int unknown08;
    int points;                                         // +0x0C
    uint8_t unknown10[0x14];
    int team;                                           // +0x24 (0, 1, 2: its mark)
    char name[0x34];                                    // +0x28 (empty: no player)
};
struct S_ServerStatus {
    int unknown00;
    std::vector<S_StatusPlayer> players;                // +0x04 (C_NetGame::FillServerStatus)
    int maxClients;                                     // +0x10
    int fragLimit;                                      // +0x14
    int roundLimit;                                     // +0x18
    int roundCount;                                     // +0x1C
    int preparationTime;                                // +0x20
    int respawnTime;                                    // +0x24
    int allowRespawn;                                   // +0x28
    int friendlyFire;                                   // +0x2C
    int autoTeamBalance;                                // +0x30
    int thirdPersonView;                                // +0x34
    int spawnProtectionTime;                            // +0x38
    int fallingDamage;                                  // +0x3C
    int unknown40;                                      // +0x40 (S_MultiplayerSettings +0x25C)
    int allowVehicles;                                  // +0x44
    int difficulty;                                     // +0x48 (1 .. 4)
    int lives;                                          // +0x4C
    int allowCrosshair;                                 // +0x50
    int spawnOnStart;                                   // +0x54
    int teamLives;                                      // +0x58
    int inverseDamage;                                  // +0x5C
    int axisScore;                                      // +0x60
    int alliedScore;                                    // +0x64
};

// The network game (C_NetGame, g_netGame, S_NetPlayerState, NET_COMMAND_* / NET_JOIN_*): net/net_game.h.
#include "net/net_game.h"
// The session a server makes (C_NetGame::Host; S_MultiplayerSettings::sessionName's 0x168 bytes, the
// DirectPlay fields set by the caller).
struct S_NetSessionDesc {                               // 0x168 bytes
    char name[200];                                     // +0x00
    uint32_t application[4];                            // +0xC8 DirectPlay's application GUID
    uint8_t unknownD8[0x84];
    int16_t unknown15C;                                 // +0x15C (100)
    int16_t unknown15E;                                 // +0x15E (2000)
    uint8_t unknown160[8];
};
static_assert(sizeof(S_NetSessionDesc) == 0x168, "S_NetSessionDesc");
// Lowest corner of the level's frames (grown by the net game's level scan, 0x0072d9c0); bodies
// that fall 5 m below it die.
extern float g_levelMinY;                           // 0x0097ca34
// Network slot of the local player.
extern int g_localPlayerSlot;                       // 0x00982104
// The session's server name (inferred from the info screen's "<text 0x930> %s"; written through
// a structure not identified).
extern char g_serverName[];                         // 0x00981e70

// Multiplayer game types (S_MultiplayerSettings::gameType).
enum E_MpGameType : int {
    MP_GAME_DEATHMATCH = 0,                         // the only one without teams
    MP_GAME_OCCUPATION = 1,                         // taking the zones
    MP_GAME_OBJECTIVES = 2,                         // attackers and defenders
    MP_GAME_COOPERATIVE = 3,
};
// A map of the session's list (the settings' +0x280 vector), its objectives and items: game/mp_game_setup.h.
#include "game/mp_game_setup.h"
// S_MultiplayerSettings::sessionType (the session menu's console: 2 creates the server, 1 connects).
enum : int { SESSION_NONE = 0, SESSION_JOINING = 1, SESSION_HOSTING = 2 };
// Settings of the network game session (the object 0x008aea30 points to; the names are those
// of the server's config file, 0x006ad200).
struct S_MultiplayerSettings {
    S_MultiplayerSettings();                        // (inline in C_GameProfile's constructor)
    virtual ~S_MultiplayerSettings();               // 0x006b50a0 (0x006b50c0)

    int sessionType;                                // +0x004 SESSION_*: 0 none, 1 joining, 2 hosting
    char sessionName[0x168];                        // +0x008
    bool dedicated;                                 // +0x170 the host plays no part: the scene is not drawn
                                                    //        and the console takes commands
    uint8_t unknown171[3];
    int unknown174;                                 // +0x174
    int unknown178;                                 // +0x178
    uint8_t unknown17C[0x9C];
    C_str password;                                 // +0x218
    C_str unknown21C;                               // +0x21C ("\1\2\3" after Reset)
    int port;                                       // +0x220
    int gameType;                                   // +0x224 E_MpGameType
    int maxClients;                                 // +0x228
    int pointLimit;                                 // +0x22C (0: none)
    int roundLimit;                                 // +0x230 min (0: none)
    int roundCount;                                 // +0x234 rounds before the next map
    int warmup;                                     // +0x238 s
    int respawnTime;                                // +0x23C s between respawn waves
    int allowRespawn;                               // +0x240
    int friendlyFire;                               // +0x244 team hits also hurt the shooter
    int autoTeamBalance;                            // +0x248
    int thirdPersonView;                            // +0x24C
    int spawnProtection;                            // +0x250 s
    int inverseDamage;                              // +0x254 share of a team hit the shooter takes
    int fallingDamage;                              // +0x258 falls hurt (C_HumanLocomotion::ApplyFallDamage)
    int unknown25C;                                 // +0x25C (S_MpGameRules' +0x34)
    int unknown260;                                 // +0x260
    int allowVehicles;                              // +0x264
    int autoRestart;                                // +0x268
    int coopDifficulty;                             // +0x26C
    int coopLives;                                  // +0x270 per player (-1: unlimited)
    int allowCrosshair;                             // +0x274
    int spawnOnStart;                               // +0x278 co-op: everyone spawns at zone 0
    int teamLives;                                  // +0x27C the co-op lives are shared
    std::vector<C_MpMap *> maps;                    // +0x280 the session's maps (owned; the main menu's copies)
    int currentMap;                                 // +0x290
    C_MpGameSetup *setup;                           // +0x294 the maps and rules the session takes (MakeSetup)

    // The defaults (no session, no password, +0x21C "\1\2\3", no listing, not dedicated, the server entry reset,
    // no name, game type 0, port 11001, the rules' defaults, the first map current), the maps deleted (part 9i-ii).
    void Reset();                                   // 0x006ad600 (__fastcall)
    // The rules (+0x228 .. +0x27F) from / to S_MpGameRules (its 22 values in order; the original copies them).
    void SetRules(const S_MpGameRules &rules);
    void GetRules(S_MpGameRules &rules);
    // The set-up's rules of the session's game type taken.
    void ApplySetupRules();                         // 0x006acc10 (__fastcall)
    // A set-up made (its map list read) / deleted.
    void MakeSetup();                               // 0x006acc50 (__fastcall)
    void DeleteSetup();                             // 0x006acd30 (__fastcall)
    // Map `name` (any case) removed and deleted; not the current one; none: false.
    bool RemoveMap(const char *name);               // 0x006acd60
    // The set-up's map `name` (any case; the last of it) of the session's game type copied to the maps; the set-up
    // not read, another game type, none of the name: false.
    bool AddMap(const char *name);                  // 0x006ace20
    // The maps deleted (the last first), none current, the set-up deleted.
    void Clear();                                   // 0x006ad560 (__fastcall)
    // Sent to the clients (part 9i-iii): the listing and the game type (8 bits), the rules (16 each), the net
    // limits (16; the inactivity in seconds); read back so (the rules and limits signed).
    void WriteRules(class C_BitStream *stream);     // 0x006ad990
    void ReadRules(class C_BitStream *stream);      // 0x006ada30
    // The current map sent; read: the maps replaced by the one read, current.
    void WriteCurrentMap(class C_BitStream *stream);   // 0x006ad840
    void ReadCurrentMap(class C_BitStream *stream); // 0x006ad860
    // The session described for the dedicated server (part 9i-iv; null: mpdesc.txt): its name, the game type, the
    // maps, the domain, dedicated, the port (not a dedicated LAN server's), the password, the rules (the first 13
    // by their config keywords, the others), the net limits, the game's voice codec (none: no line), "server".
    void WriteDescription(const char *fileName);    // 0x006ad200

    // The selected map (+0x290 into the vector at +0x280).
    C_MpMap *GetCurrentMap();                       // 0x006ad830
    // Selects map `name` (null: the next one); false when there is none of that name.
    bool SelectMap(const char *name);               // 0x006ad780
};

// The net module's limits the session sends: the update rate, the ping, the inactivity (ms; mpdesc.txt's
// maxfreq / maxping / maxinactivity).
extern int g_netMaxFreq;                            // 0x009820d8
extern int g_netMaxPing;                            // 0x009820d4
extern int g_netMaxInactivity;                      // 0x009820dc
// The replication of actors to the clients (C_NetProperty, C_NetObject, C_NetObjectMessage, C_NetReplication,
// g_netReplication): net/net_object.h.
#include "net/net_object.h"

// Soldier states in network messages (S_SoldierState, game/human.h).
class C_NetMessage;
struct S_SoldierState;
void NetWriteSoldierState(C_NetMessage *msg, const S_SoldierState &state, bool unknown);   // 0x00727850
void NetReadSoldierState(C_NetMessage *msg, S_SoldierState &state, bool unknown);          // 0x00727a30

// ---- text / localisation -----------------------------------------------------------
// The game profile (g_gameProfile): see game/profile.h.

// The player's options (C_PlayerOptions, the detail switches): game/profile.h.

// The game's texts (all_txt, g_texts): game/game_texts.h.
#include "game/game_texts.h"

// The console's commands (the table at 0x00844530, 0x18 bytes each: name, kind, value kind,
// enabled, ..., value).
enum : int {
    CONSOLE_SENSITIVITY = 0,
    CONSOLE_SAY = 1,
    CONSOLE_SAY_TEAM = 2,
    CONSOLE_KILL = 3,
    CONSOLE_HAPPYAMMO = 4,
    CHEAT_HYDROSHOCK = 5,                           // "hydroshock": enemies die from any hit
    CHEAT_SNIPER = 6,                               // "sniper": the player's weapons lose their recoil
    CONSOLE_HEAL = 7,
    CONSOLE_GIVEITEM = 8,
    CONSOLE_ANOREXY = 9,
    CONSOLE_NO_GI = 10,                             // "no_gi": the game interface is not drawn
};
// Value of console command `index`, -1 if it takes none.
int GetCheatValue(int index);                       // 0x00720f50
// A console line ("command arguments"; the chat line without "Say"), and command `command` of the
// table with its argument text (the chat: CONSOLE_SAY / CONSOLE_SAY_TEAM); results not
// identified.
int ExecuteConsoleLine(const char *line);           // 0x00720500 (__fastcall)
int ExecuteConsoleCommand(int command, const char *args);   // 0x00720710 (__fastcall)

// A pair of tables the game checks against each other (copy protection, inferred).
extern const uint32_t g_protectionTableA[];         // 0x008ca840
extern const uint32_t g_protectionTableB[];         // 0x008caf40
extern uint16_t g_protectionIndexA;                 // 0x008cac84
extern uint16_t g_protectionIndexB;                 // 0x008cb384
inline bool CopyProtectionIntact()
{
    return g_protectionTableA[g_protectionIndexA] == g_protectionTableB[g_protectionIndexB];
}
// The pair the aqualung checks (C_HumanMovement::UpdateBreath).
extern uint16_t g_protectionIndexBreathA;           // 0x008cac8e
extern uint16_t g_protectionIndexBreathB;           // 0x008cb38e
inline bool CopyProtectionIntactBreath()
{
    return g_protectionTableA[g_protectionIndexBreathA] == g_protectionTableB[g_protectionIndexBreathB];
}

// (C_Ladder and the accessors the human movement code uses on it: game/ladder.h.)

// The birds (C_Bird, g_birds, g_spareBirdModels): game/birds.h.
#include "game/birds.h"

// The vertex animation module: see game/vertex_anim.h.

// ---- the game's collaborators (C_game, game/game.h) -------------------------------
// The campaign tree: game/campaign.h; the profile's objects (g_campaign, g_missionSquad,
// g_hudOption, g_mpSettings, g_gameMode): game/profile.h.
class I3D_scene;

// The text id of a network game type (-1 .. 3; others: "unknown"; the menus' code).
int GetGameTypeTextId(int gameType);                // 0x006acb80
// The voice chat codec's text (0 .. 7: 0xB40 + n; another: 0xB48 "UNKNOWN CODEC").
int GetVoiceCodecTextId(int codec);                 // 0x006acb60

// The loading screen (C_LoaderScreen, g_loaderScreen): game/loader_screen.h.
#include "game/loader_screen.h"

// Effects that run for a while (the list at 0x008aee58; saved with the game).
void TimedEvents_Update(int timeDelta, bool clear); // 0x006df760 (`clear`: all go)
void TimedEvents_Clear();                           // 0x006df880
bool TimedEvents_Save(C_chunk &chunk);              // 0x006df890
bool TimedEvents_Load(C_chunk &chunk);              // 0x006dfa60

// Frees the frame data blocks of all the scene's frames.
void FreeAllFrameData(I3D_scene *scene);            // 0x006e9f20
// (PauseAirAttackSounds, FindAirAttackFor: see game/air_attack.h.)
// The scene's render callback (the fade, the interface, the debug joints; `phase` 1 / 2).
int __stdcall GameRenderCallback(int phase, int unknown1, int unknown2);   // 0x006c95b0

// (The in-game menu, C_GameMenu: game/game_menu.h.)
// The console commands of a dedicated server (the singleton at 0x0098c0fc).
class C_CommandProcessor {
public:
    bool Execute(const char *command);              // 0x00742850
};
C_CommandProcessor *GetCommandProcessor();          // 0x0069f520

// The game's scene as the vehicles use it (0x0089c1d0; copied by the mission loader 0x006d4270).
extern I3D_scene *g_carScene;
// tables\hdmaterials.def and tables\effects_car.def (0xF0-byte records; freed by C_game::Stop).
bool LoadMaterialTables();                          // 0x0071fd80

// ---- misc ---------------------------------------------------------------------
class IGraph;
#include <windows.h>
extern uint32_t g_graphFlags;                       // 0x00981e6c IGraph info flags after init
extern void (*g_fatalErrorHandler)();               // 0x008bf0e4
void OnFatalError();                                // 0x006e1ad0
LRESULT CALLBACK GameWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitControls(C_Controls &controls);            // 0x006c7340
void BindControls(C_Controls &controls, IGraph *graph, C_FrameListener **listeners, unsigned count);  // 0x006c7010
void RunExitSequence();                             // object 0x007426a0 at 0x0098c0fc
// Game role flags (0x00982100): bit 0 = server side, bit 1 = client side.
enum : uint32_t {
    GAME_ROLE_SERVER = 0x01,
    GAME_ROLE_CLIENT = 0x02,
    GAME_ROLE_LOCAL = 0x07,                         // single player: everything local
};
extern uint32_t g_gameRole;                         // 0x00982100

// Global object at 0x008ca4c8 and its item list (purpose not identified yet).
void InitUnknown8CA4C8();                           // 0x006e8960
void FreeUnknown8CA4C8();                           // 0x006e8970
void FreeResourceCaches();                          // tail of 0x006dc770 (model/sound caches)
// Shows a message box; returns the pressed button.
int ShowMessage(const char *text, const char *title, unsigned int flags);   // 0x006ed2a0
// Reads the CD key stored by the installer; null if missing.
const char *GetCDKey();                             // 0x00732000
bool ValidateCDKey(const char *key);                // 0x007350a0

// ---- frames / models -------------------------------------------------------------
class C_Actor;
class I3D_frame;
struct S_FrameData;
// Attaches a copy of `data` to `frame` (allocating the frame's data block if needed).
void SetFrameData(I3D_frame *frame, const S_FrameData &data);   // 0x006e9ea0

// Frees the frame's data block (S_FrameData).
void FreeFrameData(I3D_frame *frame);               // 0x006e9ef0

// The camera module's keeper of a camera dummy's placement (C_CameraFrameKeeper):
// game/camera_module.h.

// ---- items (game/items_module.h) ----------------------------------------------------
class C_Actor;
class C_chunk;
class C_Item;
// C_HumanEquipment: see game/human_body.h.
// The first-person view (C_game +0x5C): see game/first_person_view.h.

// (The seat weapons: game/seat_weapon.h; C_Actor::GetSeatWeaponDef gives their item's definition.)
// (Item actor setup: C_ItemActor::SetupItem, game/item_actor.h.)

// Not identified yet: a list of periodic entries at 0x008aee64 advanced in 100 ms
// steps (saved with the actors), and a per-tick human pass at 0x008aaee0 run in
// single player / co-op.
// Returns the link id (-1 on clients).
int PeriodicLinks_Add(C_Actor *actor, int arg);     // 0x006e0080
void PeriodicLinks_Remove(int id);                  // 0x006e0220
void PeriodicLinks_SetRate(int id, int rate);       // 0x006e03a0 (rate table 0x008435c8)
void PeriodicLinks_Tick();                          // 0x006e0b30
void PeriodicLinks_Clear();                         // 0x006e0b90
bool PeriodicLinks_Save(C_chunk &chunk);            // 0x006e0c60
bool PeriodicLinks_Load(C_chunk &chunk);            // 0x006e0f40
void HumanPass_Tick(int timeDelta, const std::vector<C_Actor *> &active);   // 0x005647c0

// ---- scripts ------------------------------------------------------------------------
// Set while the script module restarts a mission (StopMission + StartMission).
extern bool g_scriptModuleRestarting;               // 0x008693e0

// The sight module: see game/sight.h.
#include "game/sight.h"

// Physics: see game/physics.h.

// Collision world: see game/collision.h.

// Uniform random number in [0, max).
float RandomFloat(float max);                       // 0x00475190

// The points of walking-network path `path` from `from` to `to` (the command map's path of an
// order; replaces `points`).
void BuildPathPoints(const struct S_vector4 &from, const struct S_vector4 &to, const std::vector<uint16_t> &path,
                     struct S_Vector4Array &points);   // 0x007ce930 (__fastcall)

class C_Actor;
class C_Human;
class I3D_frame;
// FindJunkersTargetFor (0x005f0e40): see game/junkers88.h.
