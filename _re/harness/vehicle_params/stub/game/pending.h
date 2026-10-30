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

// The game's texts (C_Car's name).
class C_GameTexts {
public:
    C_str Get(int id) const;
};
extern C_GameTexts g_texts;

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
class C_NetObjectMessage {
public:
    explicit C_NetObjectMessage(int type);
    virtual ~C_NetObjectMessage();
    virtual void Write(C_BitStream *stream) = 0;
    virtual void Read(C_BitStream *stream) = 0;
    int GetType() const { return type; }
    int type;
    int sender;
    int unknown24;
    bool fromClient = false;
};
struct S_vector4;
struct S_vector;
struct S_NetQuantization;
class C_NetTrack {
public:
    void SetValue(const S_vector4 &value);
    void SetRate(const S_vector &rate);
    const S_vector4 &GetCurrent();
    const S_vector4 &GetRate();
};
class C_NetDirTrack {
public:
    const S_vector4 &GetValue();
    const S_vector4 &GetTarget();
    void SetValue(const S_vector4 &dir, float angle);
    float GetAngle();
};
class C_NetProperty {
public:
    C_NetTrack &GetTrack();
    C_NetDirTrack &GetDirTrack();
    void SetField(int field, int value);
    uint32_t GetFlags() const;
    void SetFlags(uint32_t flags);
};
using NetStateHandler = void (__fastcall *)(void *context, C_BitStream *stream);
using NetMessageFactory = C_NetObjectMessage *(__fastcall *)(void *context, C_BitStream *stream, bool write);
class C_NetObject {
public:
    void SetQuantizations0(S_NetQuantization *a, S_NetQuantization *b, S_NetQuantization *c);
    void SetQuantizations1(S_NetQuantization *a, S_NetQuantization *b, S_NetQuantization *c);
    C_NetProperty *GetProperty(int index);
    void Update(float seconds);
    C_NetObjectMessage *PopMessage(int unknown);
    void QueueMessage(C_NetObjectMessage *msg, int sender, bool reliable);
    void Tick(float seconds);
    void SetSendMask(int mask);
    void SetUnknown3C(int value);
    int GetUnknown3C() const;
    void SetStateHandlers(NetStateHandler read, NetStateHandler write, void *context);
    void SetMessageFactory(NetMessageFactory factory, void *context);
    void SetUnknown40(bool value);
};
class C_NetReplication {
public:
    C_NetObject *FindObject(int id, bool create);
    void RemoveObject(C_NetObject *object);
    int GetLocalId() const;
    void SetUnknownC4(int value);
    void SetUnknownC8(int value);
};
extern C_NetReplication *g_netReplication;
class alignas(16) C_CameraFrameKeeper {
public:
    C_CameraFrameKeeper(C_Actor *owner, I3D_frame *frame);
    ~C_CameraFrameKeeper();
    bool Restore();
    void Update(int time);

private:
    uint8_t m_unknown[0x50];
};

// --- the car's updates (game/car_update.cpp; as the real header declares them) -------------------
enum : int {
    MP_GAME_DEATHMATCH = 0,
    MP_GAME_OCCUPATION = 1,
    MP_GAME_OBJECTIVES = 2,
    MP_GAME_COOPERATIVE = 3,
};
struct S_MultiplayerSettings {
    int gameType;
    int allowVehicles;                                  // (the car's StartMission, RestartMission)
    int allowRespawn;                                   // (the spawn point, part 7b)
    int warmup;                                         // (the multiplayer zone, part 7d) s
};

// --- the car's callbacks (game/car_callbacks.cpp; as the real header declares them) --------------
class C_GEffectsModule {
public:
    void ShatterGlass(I3D_frame *visual, float unknown);
    // (The debris and item actors, game/debris.cpp and game/item_actor.cpp.)
    bool ClearModelHighlight(I3D_frame *model);
    void FadeOutModel(I3D_frame *model, int time, bool unknown);
    // (The bike, part 7e.)
    I3D_frame *CreateShadow(I3D_frame *model);
    I3D_frame *GetShadow(I3D_frame *model);
    // (The supply box, part 7f.)
    void FadeInModel(I3D_frame *model, int time, bool turnOn);
    void FadeOutHierarchy(I3D_frame *model, int time, bool unknown);
};
C_GEffectsModule *GetGEffects();
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
    uint8_t unknown40[0x68];
    uint32_t activityTime;                              // +0xA8
    uint8_t unknownAC[0x18];
    // (The multiplayer zone, part 7d.)
    bool IsConnected() const { return name[0] != 0; }
};
class C_NetGame {
public:
    C_Human *GetLocalHuman();
    S_NetPlayerState *LockPlayers();
    void UnlockPlayers();
    void SetPlayer(int slot, int connection, const char *name);
    void UpdateVoiceTeam();
    // (The multiplayer zone, part 7d.)
    int GetLocalTeam();
    int GetPlayerCount() const;
    void AddScore(int slot, int points);
    void AddTeamScore(int team, float points);
};
extern C_NetGame g_netGame;
class C_NetMessage;
struct S_SoldierState;
void NetWriteSoldierState(C_NetMessage *msg, const S_SoldierState &state, bool unknown);
void NetReadSoldierState(C_NetMessage *msg, S_SoldierState &state, bool unknown);

// (The seat weapon, game/seat_weapon.cpp: its barrels' frame data.)
struct S_FrameData;
void SetFrameData(I3D_frame *frame, const S_FrameData &data);

// (The fowl generator, game/fowl_generator.cpp: the birds module's birds and RandomFloat.)
// The sight module (part 7g: only the air attacks' start wait; +0x34).
class C_SightModule {
public:
    int GetStartDelay() const;
};
extern C_SightModule g_sightModule;                 // 0x0089b288
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
float RandomFloat(float max);                           // 0x00475190

// ---- the vertex animation module (the multiplayer zone's flag, part 7d; as the real header declares it)
struct S_VertexAnimation {
    uint8_t unknown00[0x18];
    int frameCount;                                     // +0x18
};
struct S_VertexAnimPlayer {
    int frame = 0;                                      // +0x00
    class I3D_frame *visual = nullptr;                  // +0x04
    int unknown08 = 0;
    int unknown0C = 0;
    int unknown10 = 1;
    int unknown14 = 0;
    int frameTime = 250;                                // +0x18 ms
    bool unknown1C = false;
    bool unknown1D = false;
    bool unknown1E = false;
    bool unknown1F = true;
    int unknown20 = 0;
    S_VertexAnimation *animation = nullptr;             // +0x24
    int unknown28 = 1;

    void Reset();
    void Init(class I3D_frame *visual, S_VertexAnimation *animation);
};
class C_str;
class C_VertexAnimModule {
public:
    S_VertexAnimation *FindAnimation(const C_str &name);
    void RemovePlayer(S_VertexAnimPlayer *player, bool destroy);
    void AddPlayer(S_VertexAnimPlayer *player);
};
extern C_VertexAnimModule g_vertexAnimModule;
