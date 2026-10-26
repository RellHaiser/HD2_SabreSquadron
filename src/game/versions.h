// Destructible objects ("versions") — source file 0x0043ad80-0x0043f1e0.
//
// A scene frame whose user properties (set in the modelling tool) contain
// "version" is a damageable part (C_VersionKeeper) of the model it belongs to
// (C_VersionMaster). The properties give the part's hit points ("version"), a
// particle effect played when it breaks ("particle"), its material ("matid") and
// whether bullets pass through ("trans").
//
// When a part's hit points run out it is replaced by its next damage stage. The
// stages come from a separate model, "d<model file name>" in the versions
// directories, which contains a frame named like each part: that frame is the
// first stage, the visual child of a stage is the next stage, and so on. A stage
// carries its own hit points ("version") and break sound ("sound", a group of
// sound chapter 7) and may have a dummy child whose visuals fly off as physics
// debris when the stage is reached.
//
// The server decides about damage and tells the clients which stage a part is in
// (module message channel MODULE_VERSIONS); saved games store the stages too.
#pragma once

#include <cstdint>
#include <vector>

#include "game/collision.h"
#include "game/module.h"
#include "ls3d/i3d.h"

class C_BitStream;
class C_VersionMaster;

// How Damage() shows the effects (particles, debris, sound) of a stage change.
enum E_VersionEffects {
    VERSION_EFFECTS_ALL = 0,
    VERSION_EFFECTS_SOME = 1,   // only for every third model (by index)
    VERSION_EFFECTS_NONE = 2,
};

// Collision box of a damageable model.
struct alignas(16) S_VersionCollision {
    S_VersionCollision(I3D_frame *frame, bool transparent);  // 0x0043ddb0
    ~S_VersionCollision();                                  // 0x0043e1f0

    S_matrix invMatrix;                                     // +0x00
    S_matrix matrix;                                        // +0x40
    S_CollisionVolume volume;                               // +0x80
};

// One damageable part.
class C_VersionKeeper {                                     // 0x68 bytes
public:
    C_VersionKeeper(I3D_frame *frame, C_VersionMaster *master, int index, int hitPoints,
                    int particleEffect, int materialId, bool transparent);   // 0x0043adb0
    ~C_VersionKeeper();                                     // 0x0043aef0
    C_VersionKeeper(const C_VersionKeeper &) = delete;
    C_VersionKeeper &operator=(const C_VersionKeeper &) = delete;

    // Applies `damage` hit points; `outFrame` receives the part's (possibly new)
    // frame. Returns false when the part was used up — it may then have been
    // deleted, and its master too (check C_VersionsModule::GetMaster()).
    bool Damage(int damage, I3D_frame **outFrame, E_VersionEffects effects);   // 0x0043b000
    // Jumps to damage stage `version` (-1: the last one) unless the part is already
    // further; `effects` plays particles, sound and physics debris.
    void SetVersion(int version, bool effects);             // 0x0043c040

    I3D_frame *GetFrame() const { return m_frame; }
    int GetVersion() const { return m_version; }
    int GetHitPoints() const { return m_hitPoints; }
    void SetHitPoints(int hitPoints) { m_hitPoints = hitPoints; }
    // Surface material of the part (S_MaterialDef id).
    int GetMaterialId() const { return m_materialId; }

private:
    friend class C_VersionMaster;

    // Copies the visuals of a stage's debris dummy into the scene as physics objects.
    void SpawnDebris(I3D_frame *debris, bool physics);     // 0x0043b140
    void PlaySound(int group);                              // 0x0043b9d0
    // Replaces the part's visual by a copy of `source`.
    void SwapVisual(I3D_frame *source);                     // 0x0043ba60
    void SpawnParticles(int effect);                        // 0x0043c000

    I3D_frame *m_frame;                                     // +0x00 holds a reference
    C_VersionMaster *m_master;                              // +0x04
    int m_hitPoints;                                        // +0x08 left in the current stage
    int m_particleEffect;                                   // +0x0C -1: none
    int m_materialId;                                       // +0x10
    int m_version = 0;                                      // +0x14 current stage (0 = intact)
    bool m_versionsLoaded = false;                          // +0x18
    std::vector<int> m_versionHitPoints;                    // +0x1C per stage
    std::vector<I3D_frame *> m_versionFrames;               // +0x2C stage visuals (versions model)
    std::vector<I3D_frame *> m_debrisFrames;                // +0x3C debris dummies (may be null)
    std::vector<int> m_versionSounds;                       // +0x4C sound groups (-1: none)
    int m_numVersions = 0;                                  // +0x5C
    int m_index;                                            // +0x60 in the master's list
    bool m_transparent;                                     // +0x64
};

// A damageable model and its parts.
class C_VersionMaster {                                     // 0x2C bytes
public:
    C_VersionMaster(I3D_frame *model, int index, bool transparent);   // 0x0043c220
    ~C_VersionMaster();                                     // 0x0043c320
    C_VersionMaster(const C_VersionMaster &) = delete;
    C_VersionMaster &operator=(const C_VersionMaster &) = delete;

    // False if the frame is a part already.
    bool AddKeeper(I3D_frame *frame, int hitPoints, int particleEffect, int materialId,
                   bool transparent);                       // 0x0043c440
    // Damages the part `frame` belongs to; false if it is none of this model's parts.
    bool DamageFrame(I3D_frame *frame, int damage);        // 0x0043c660
    // Damages every part; false if one of them (or the model) was used up.
    bool DamageAll(int damage, E_VersionEffects effects);   // 0x0043c6a0
    // After a stage change: notifies the model's script, deletes a used-up part,
    // refits the collision box and removes the model when no part is left. Returns
    // false if `keeper` was deleted.
    bool OnKeeperDamaged(C_VersionKeeper *keeper);          // 0x0043c710
    // Reads the stages of `keeper` from the versions model.
    bool LoadVersions(C_VersionKeeper *keeper);             // 0x0043cca0

    void SaveGame(C_chunk &chunk);                          // 0x0043d550
    void LoadGame(C_chunk &chunk);                          // 0x0043d7f0
    void WriteState(C_BitStream *msg);                      // 0x0043dab0
    void ReadState(C_BitStream *msg);                       // 0x0043db40

    I3D_frame *GetFrame() const { return m_frame; }
    int GetIndex() const { return m_index; }
    bool IsDamaged() const { return m_damaged; }
    void SetDamaged() { m_damaged = true; }

private:
    friend class C_VersionsModule;

    // Deletes the parts and releases the models.
    void Clear();                                           // 0x0043c1a0
    void LoadVersionsModel();                               // 0x0043ce40

    int m_refCount = 1;                                     // +0x00
    bool m_removed = false;                                 // +0x04
    I3D_frame *m_frame;                                     // +0x08 the model, holds a reference
    I3D_model *m_versionsModel = nullptr;                   // +0x0C damage stages
    std::vector<C_VersionKeeper *> m_keepers;               // +0x10 null once used up
    S_VersionCollision *m_collision;                        // +0x20
    int m_index;                                            // +0x24 in the module's list
    bool m_damaged = false;                                 // +0x28
};

class C_VersionsModule : public C_Module {                  // "Versions", vtable 0x0080f908, 0x3C bytes
public:
    C_VersionsModule(int id, const char *name);             // 0x0043dc10
    ~C_VersionsModule() override;                           // 0x0043dcd0

    void StartMission(bool server) override;                // 0x0043dd40
    void StopMission(bool server) override;                 // 0x0043dd90
    // Client: one part changed its stage.
    bool OnMessage(C_BitStream *msg) override;              // 0x0043f1e0
    void RestartMission(bool server) override;              // 0x0043eb50
    bool SaveGame(C_chunk &chunk) override;                 // 0x0043eba0
    bool LoadGame(C_chunk &chunk) override;                 // 0x0043ee10
    void WriteState(C_BitStream *msg) override;             // 0x0043f090
    void ReadState(C_BitStream *msg) override;              // 0x0043f120

    // Makes `frame` a damageable part of its model.
    void AddKeeper(I3D_frame *frame, int hitPoints, int particleEffect, int materialId,
                   bool transparent);                       // 0x0043e560
    // Drops a model whose parts are all used up.
    void RemoveMaster(C_VersionMaster *master);             // 0x0043e7c0
    // Keeps the collision box of a removed model until the mission ends.
    void KeepCollision(S_VersionCollision *collision);      // 0x0043e830
    // Remembers a damaged model so it is reloaded (repaired) when the mission ends.
    void AddDamagedModel(I3D_frame *model) { m_damagedModels.push_back(model); }

    // Null once the model was removed.
    C_VersionMaster *GetMaster(int index) const { return m_masters[index]; }

private:
    // Deletes everything and reloads the damaged models.
    void Clear();                                           // 0x0043e2c0

    std::vector<C_VersionMaster *> m_masters;               // +0x0C
    std::vector<S_VersionCollision *> m_keptCollisions;     // +0x1C
    std::vector<I3D_frame *> m_damagedModels;               // +0x2C
};

extern C_VersionsModule g_versionsModule;                   // 0x00899978
// Set while a saved game is loaded (the model scripts get no OnDeath then).
extern bool g_versionsLoading;                              // 0x00899974

// Number in the user properties after `key` ("version 50"), `defaultValue` if missing.
int ParseUserProp(const char *props, const char *key, int defaultValue);   // 0x0043e240
// Creates the parts of every scene frame with "version" in its user properties.
void BuildVersionKeepers(I3D_scene *scene);                 // 0x0043e3e0
