// C_Module — base class of the game's subsystem modules.
//
// Each module is a static global object that registers itself by id in a fixed
// table. The game core broadcasts lifecycle events to every registered module.
// Most events carry a `server` flag: the game runs a server and a client pass
// (both in single player), and modules usually act on only one side.
#pragma once

#include "common/c_str.h"

class C_chunk;

// Module ids (registration order; the id is also the chunk id in saved games).
enum E_ModuleId {
    MODULE_TRACK = 0,             // "Track module"  C_TrackModule
    MODULE_GEFFECTS = 1,          // "GEffects"      C_GEffectsModule
    MODULE_PHYSICS = 2,           // "Physics"       C_PhysicsModule
    MODULE_ITEMS = 3,             // "Items"
    MODULE_SCRIPT = 4,            // "Script"        C_ScriptModule
    MODULE_SIGHT = 5,             // "Sight"         C_SightModule
    MODULE_SOUND = 6,             // "Sound"         C_SoundModule
    MODULE_VERSIONS = 7,          // "Versions"      C_VersionsModule
    MODULE_LIGHTMAP_GROUPS = 8,   // "LMapsGrp"      C_LightMapGroupsModule
    MODULE_VERTEX_ANIM = 9,       // "Vertex anim"   C_VertexAnimModule
    MODULE_TSHIFT = 10,           // "TShift"        C_TShiftModule
    MODULE_TEAM = 11,             // "TeamMgr"       C_TeamManager
    MODULE_TEMP_ACTORS = 12,      // "Temp. Actors"  C_TemporaryActorsList
    MODULE_ACTORS = 13,           // "Actors"        C_ActorsModule
    MODULE_SHOTS = 14,            // "ShotMgr"       C_ShotManager
    MODULE_CAMERA = 15,           // "CameraModule"  C_CameraModule
    MODULE_HEAVY_FIRE_SUPPORT = 16,   // "HeavyFireSupportModule" C_HeavyFireSupport
    MODULE_PARTICLES = 17,        // "Particles"     C_ParticlesModule
    MODULE_18 = 18,
    MODULE_COUNT = 19,
};

class C_BitStream;

class C_Module {
public:
    C_Module(int id, const char *name);                     // 0x0048d950
    virtual ~C_Module();                                     // 0x0048da30

    virtual void Init() {}                                   // 0x04
    virtual void Done() {}                                   // 0x08
    // Fixed-rate game tick (the server runs these in 100 ms steps).
    virtual void Tick(int timeDelta) {}                      // 0x0C
    // Called every frame with the frame time while the game is not paused.
    virtual void Update(int timeDelta) {}                    // 0x10
    virtual void LoadMissionData(bool server) {}             // 0x14
    virtual void FreeMissionData(bool server) {}             // 0x18
    virtual void StartMission(bool server) {}                // 0x1C
    virtual void StopMission(bool server) {}                 // 0x20
    // Handles a network message addressed to the module (channel = module id).
    virtual bool OnMessage(C_BitStream *msg) { return false; }   // 0x24
    // Like OnMessage, for the messages the network layer delivers with its other
    // flag (see C_NetGame receive, 0x0072c480).
    virtual bool Unknown28(C_BitStream *msg) { return false; }   // 0x28
    virtual void RestartMission(bool server) {}              // 0x2C
    virtual bool SaveGame(C_chunk &chunk) { return true; }   // 0x30
    virtual bool LoadGame(C_chunk &chunk) { return true; }   // 0x34
    // Full state transfer to a joining client: the server writes, the client reads.
    virtual void WriteState(C_BitStream *msg) {}             // 0x38
    virtual void ReadState(C_BitStream *msg) {}              // 0x3C

    const char *GetName() const { return m_name; }
    int GetId() const { return m_id; }

private:
    C_str m_name;                                            // +0x04
    int m_id;                                                // +0x08
};

// Registered modules, indexed by E_ModuleId (unused slots are null).
extern C_Module *g_modules[MODULE_COUNT];

namespace ModuleManager {
void BroadcastInit();                                        // 0x0048da60
void BroadcastDone();                                        // 0x0048da80
void BroadcastTick(int timeDelta);                 // 0x0048daa0
void BroadcastUpdate(int timeDelta);                         // 0x0048dad0
void BroadcastLoadMissionData(bool server);                  // 0x0048dbf0
void BroadcastFreeMissionData(bool server);                  // 0x0048dc20
void BroadcastStartMission(bool server);                     // 0x0048db00
void BroadcastStopMission(bool server);                      // 0x0048db90 (reverse order)
void BroadcastRestartMission(bool server);                   // 0x0048dbc0
void BroadcastWriteState(C_BitStream *msg);                          // 0x0048db30
void BroadcastReadState(C_BitStream *msg);                           // 0x0048db60
#undef DispatchMessage                                       // (windows.h's macro would rename it where that is included)
bool DispatchMessage(int moduleId, C_BitStream *msg);                 // 0x0048dc70
bool CallUnknown28(int moduleId, C_BitStream *msg);          // 0x0048dc50
bool SaveGame(C_chunk &chunk);                               // 0x0048dc90
bool LoadGame(C_chunk &chunk);                               // 0x0048de60
} // namespace ModuleManager
