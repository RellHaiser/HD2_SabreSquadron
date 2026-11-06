// Harness stub of game/pending.h: only what the vehicle files use (game/vehicle_params.cpp,
// game/vehicle.cpp, game/vehicle_start.cpp, game/vehicle_solve.cpp, game/vehicle_collide.cpp) and
// the car's (game/car*.cpp).
#pragma once

#include <cstdint>
#include <vector>

#include "game/resource_cache.h"                        // g_modelCache (the real header)

class I3D_scene;

#include "game/car_tables.h"                            // (the real one)

extern I3D_scene *g_carScene;

#include "common/c_str.h"
#include "game/camera_module.h"                       // C_CameraFrameKeeper (the real header)

// The game's texts (C_Car's name; the real header since part 9g-iii, its Get faked by the builds that do
// not link game/game_texts.cpp).
#include "game/game_texts.h"
int ShowMessage(const char *text, const char *title, unsigned int flags);   // (the texts' loading, part 9g-iii)
int GetGameTypeTextId(int gameType);                    // (the loading screen, part 8f-i)
int GetVoiceCodecTextId(int codec);                     // (the main menu's create server screen, part 9h-xiii)

// --- the car's seats (game/car_seats.cpp; as the real header declares them) ---------------------
class C_Actor;
class C_BitStream;
class I3D_frame;
enum : uint32_t {
    GAME_ROLE_SERVER = 0x01,
    GAME_ROLE_CLIENT = 0x02,
    GAME_ROLE_LOCAL = 0x07,
};
extern uint32_t g_gameRole;
extern int g_localPlayerSlot;
void PeriodicLinks_SetRate(int id, int rate);
int PeriodicLinks_Add(C_Actor *actor, int arg);
void PeriodicLinks_Remove(int id);
#include "net/net_object.h"
// (C_CameraFrameKeeper: the real header, part 8e-i.)

// --- the car's updates (game/car_update.cpp; as the real header declares them) -------------------
enum : int {
    MP_GAME_DEATHMATCH = 0,
    MP_GAME_OCCUPATION = 1,
    MP_GAME_OBJECTIVES = 2,
    MP_GAME_COOPERATIVE = 3,
};
class C_MpMap;
struct S_MultiplayerSettings {
    int gameType;
    int allowVehicles;                                  // (the car's StartMission, RestartMission)
    int allowRespawn;                                   // (the spawn point, part 7b)
    int warmup;                                         // (the multiplayer zone, part 7d) s
    bool dedicated;                                     // (the loading screen, part 8f-i)
    int sessionType;                                    // (the in-game menu's messages, part 9e-vii)
    int unknown174;                                     // (the session listing kind: the create server screen, part 9h-xiv)
    // (The session menu, part 9g-ii.)
    char sessionName[0x168];
    uint8_t unknown17C[0x9C];
    C_str password;
    // (The main menu's hosted session, part 9h-xvii.)
    int unknown178, port, maxClients, pointLimit, roundLimit, roundCount, respawnTime, friendlyFire, autoTeamBalance,
        thirdPersonView, spawnProtection, inverseDamage, coopDifficulty, coopLives, allowCrosshair, teamLives;
    std::vector<C_MpMap *> maps;
    // (The session's settings, part 9i-ii.)
    C_str adminPassword;
    int fallingDamage, unknown25C, unknown260, autoRestart, spawnOnStart, currentMap;
    class C_MpGameSetup *setup;
    void Reset();
    void SetRules(const struct S_MpGameRules &rules);
    void GetRules(struct S_MpGameRules &rules);
    void ApplySetupRules();
    void MakeSetup();
    void DeleteSetup();
    bool RemoveMap(const char *name);
    bool AddMap(const char *name);
    void Clear();
    bool SelectMap(const char *name);
    C_MpMap *GetCurrentMap();
    // (The replication, part 9i-iii.)
    void WriteRules(class C_BitStream *stream);
    void ReadRules(class C_BitStream *stream);
    void WriteCurrentMap(class C_BitStream *stream);
    void ReadCurrentMap(class C_BitStream *stream);
    void WriteDescription(const char *fileName);
};
extern int g_netMaxFreq, g_netMaxPing, g_netMaxInactivity;
enum : int { SESSION_NONE = 0, SESSION_JOINING = 1, SESSION_HOSTING = 2 };

// --- the car's callbacks (game/car_callbacks.cpp; as the real header declares them) --------------
#ifdef WORLD_TEST
// The GEffects module's objects (part 8d; as the real header declares them) and the module itself
// (part 8c-i, the world build tests the real one).
#include "game/decorators.h"
#include "game/volumetrics.h"
#include "game/mesh_player.h"
#include "game/birds.h"
float RandomFloat(float max);
#include "game/geffects.h"
#else
class C_GEffectsModule {
public:
    void ShatterGlass(I3D_frame *visual, float unknown);
    // (The debris and item actors, game/debris.cpp and game/item_actor.cpp.)
    bool ClearModelTransparency(I3D_frame *model);
    void FadeOutModel(I3D_frame *model, int time, bool unknown);
    // (The bike, part 7e.)
    I3D_frame *CreateShadow(I3D_frame *model);
    I3D_frame *GetShadow(I3D_frame *model);
    // (The supply box, part 7f.)
    void FadeInModel(I3D_frame *model, int time, bool turnOn);
    void FadeOutHierarchy(I3D_frame *model, int time, bool unknown);
    // (The Junkers 88, part 7h.)
    void SetHierarchyOn(I3D_frame *frame, bool on, uint32_t typeMask);
};
C_GEffectsModule *GetGEffects();
#endif
// (The supply box, part 7f: the scene's top, the debug lines.)
extern float g_netSceneTop;
void DrawDebugLine(const float *from, const float *to, uint32_t color, uint32_t color2);
// (The item actor's ReleaseItem.)
void FreeFrameData(I3D_frame *frame);

// The local human of a network game (game/car_hits.cpp).
class C_Human;
// (The spawn point, part 7b: the players' states, as the real header declares them.)
struct S_NetPlayerState {
    int connection;                                     // +0x00
    int unknown04;
    char name[0x34];                                    // +0x08
    C_Human *human;                                     // +0x3C
    int stats[9];                                       // +0x40 (the players' list, part 9c-v-d)
    uint8_t unknown64[0x44];
    uint32_t activityTime;                              // +0xA8
    uint8_t unknownAC[0x18];
    // (The multiplayer zone, part 7d.)
    bool IsConnected() const { return name[0] != 0; }
};
// (The main menu's game info screen, part 9h-xii: the real ones' layout.)
struct S_ServerEntry;
struct S_StatusPlayer {
    int unknown00;
    int kills;
    int unknown08;
    int points;
    uint8_t unknown10[0x14];
    int team;
    char name[0x34];
};
struct S_ServerStatus {
    int unknown00;
    std::vector<S_StatusPlayer> players;                // (the real one's: +0x04)
    int unknown10;
    int fragLimit;
    int roundLimit;
    int roundCount;
    int preparationTime;
    int respawnTime;
    int allowRespawn;
    int friendlyFire;
    int autoTeamBalance;
    int thirdPersonView;
    int spawnProtectionTime;
    int unknown3C;
    int unknown40;
    int allowVehicles;
    int difficulty;
    int lives;
    int allowCrosshair;
    int unknown54;
    int teamLives;
    int inverseDamage;
    int axisScore;
    int alliedScore;
};

class C_NetGame {
public:
    int QueryServerStatus(void(__fastcall *callback)(S_ServerStatus *status), S_ServerEntry *server);
    void PingServer(void(__fastcall *callback)(S_ServerEntry *server), S_ServerEntry *server);
    void PollSearch();
    void SearchServers(void(__fastcall *callback)(S_ServerEntry *server));
    void PingServers(void(__fastcall *callback)(S_ServerEntry *server), std::vector<S_ServerEntry *> &servers);
    bool FirewallBlocks(uint16_t port);
    C_Human *GetLocalHuman();
    void Close(bool now);                           // (the main menu, part 9h-iv)
    S_NetPlayerState *LockPlayers();
    void UnlockPlayers();
    void SetPlayer(int slot, int connection, const char *name);
    void UpdateVoiceTeam();
    bool SendChat(const char *text, bool team);
    int *GetLocalPlayerStats();                     // (the in-game menu's stats, part 9e-xix)
    // (The multiplayer zone, part 7d.)
    int GetLocalTeam();
    int GetPlayerCount() const;
    void AddScore(int slot, int points);
    void AddTeamScore(int team, float points);
    void PrintLoadProgress(int percent);                // (the loading screen, part 8f-i)
    void CountTeams(int *axis, int *allies, bool pending, bool inGame);   // (the in-game menu, part 9e-vi)
    // (The session menu, part 9g-ii: campaign_screens_test.inc.)
    int Host(const struct S_NetSessionDesc &desc, bool unknown);
    int Connect(void *address);
    bool HostDedicated(bool unknown);                // (the main menu's connect screen, part 9h-xviii)
    int GetJoinStatus() const;
};
extern C_NetGame g_netGame;
enum : int {
    NET_JOIN_CONNECTED = 1, NET_JOIN_FULL = 3, NET_JOIN_BAD_PASSWORD = 4, NET_JOIN_BAD_CD_KEY = 5,
    NET_JOIN_BAD_VERSION = 7,
};
struct S_NetSessionDesc {                               // (as the real header)
    char name[200];
    uint32_t serviceProvider[4];                        // +0xC8
    uint8_t unknownD8[0x52];
    char password[0x32];                                // +0x12A
    int16_t unknown15C;
    int16_t unknown15E;
    uint8_t unknown160[8];
};
class C_NetMessage;
struct S_SoldierState;
void NetWriteSoldierState(C_NetMessage *msg, S_SoldierState &state, bool itemsOnly);
void NetReadSoldierState(C_NetMessage *msg, S_SoldierState &state, bool itemsOnly);

// (The seat weapon, game/seat_weapon.cpp: its barrels' frame data.)
struct S_FrameData;
void SetFrameData(I3D_frame *frame, const S_FrameData &data);

// (The fowl generator, game/fowl_generator.cpp: the birds module's birds and RandomFloat.)
// The sight module (part 7g: only the air attacks' start wait; +0x34; the world build, part 8b,
// tests the real one).
#ifdef WORLD_TEST
#include "game/sight.h"
#else
class C_SightModule {
public:
    int GetStartDelay() const;
};
extern C_SightModule g_sightModule;                 // 0x0089b288
#endif
#ifdef WORLD_TEST
#include "game/birds.h"
#else
class C_Bird {                                          // 0x70 bytes
public:
    // On `model`, whose frame data then points at the bird; `ownModel`: the model is the bird's,
    // else it goes back to g_spareBirdModels (hidden) when the bird is deleted.
    C_Bird(class I3D_frame *model, bool ownModel);     // 0x004797c0
    ~C_Bird();                                          // 0x004799e0
    // Flying on; false once it has flown off.
    bool Update(int time);                              // 0x0047a2e0
    void FlyAway();                                     // 0x0047a2f0
    class I3D_frame *GetModel() const { return m_model; }   // +0x50
    bool OwnsModel() const { return m_ownModel; }       // +0x60

private:
    uint8_t m_unknown00[0x50];
    class I3D_frame *m_model;                           // +0x50
    uint8_t m_unknown54[0x0C];
    bool m_ownModel;                                    // +0x60
    uint8_t m_unknown61[0x0F];
};
// The birds sitting in the mission (the birds module's) and the models of the birds deleted.
extern std::vector<C_Bird *> g_birds;                  // 0x0089bb90
extern std::vector<class I3D_frame *> g_spareBirdModels;   // 0x0089bba0
#endif
float RandomFloat(float max);                           // 0x00475190

// The controls (the camera module's look actions, part 8e-iii; the camera test defines GetValue).
class C_Controls {
public:
    float GetValue(int action, bool pressed);          // 0x006c76a0
    // (The hot key control's poll, part 9c-v-d: as the real header has it.)
    struct S_Press {
        int direction;
        uint16_t device;
        uint16_t key;
        int unknown08;
    };
    const S_Press *PollPress(int unknown, bool reread); // 0x006c87f0
};
extern C_Controls g_controls;                           // 0x008aeb58

// The vertex animation module: stub/game/vertex_anim.h.
