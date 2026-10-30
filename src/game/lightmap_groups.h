// The lightmap groups module ("LMapsGrp", module 8; the object 0x0089bbbc; game/lightmap_groups.cpp,
// original source 0x0048c740..0x0048d8f0): named groups of lightmapped visuals whose lighting the
// mission scripts switch together (frm_setgrouplightmap; the rooms whose lamps go out), and the
// lightmaps switched, kept so that a restart or a saved game can bring them back.
//
// The groups come from the mission's scene file (C_game's loader passes the chunk that holds
// them): chunk 0x100 starts a group by its name, the chunks 0x110 after it name its frames.
#pragma once

#include <vector>

#include "common/c_str.h"
#include "game/module.h"

class C_chunk;
class I3D_frame;

// A group (0x14 bytes; ctor 0x0048d100, dtor 0x0048cc40, deleting dtor 0x0048d230).
struct S_LightmapGroup {
    explicit S_LightmapGroup(const char *name) : name(name) {}

    // Whether the group names that frame (case insensitive).
    bool HasFrame(const char *frame) const;             // 0x0048d1d0

    C_str name;                                         // +0x00
    std::vector<C_str> frames;                          // +0x04
};

// A visual whose lightmap was switched (8 bytes): it holds a reference to the frame.
struct S_LightmapChange {
    S_LightmapChange(I3D_frame *frame);
    ~S_LightmapChange();                                // 0x0048d250 (deleting)
    S_LightmapChange(const S_LightmapChange &) = delete;
    S_LightmapChange &operator=(const S_LightmapChange &) = delete;

    I3D_frame *frame;                                   // +0x00
    int original;                                       // +0x04 its lightmap before the first switch
};

class C_LightMapGroupsModule : public C_Module {       // vtable 0x00810398
public:
    C_LightMapGroupsModule(int id, const char *name);   // 0x0048cb10
    ~C_LightMapGroupsModule() override;                 // 0x0048cd30

    // The client's groups and switches dropped (no lightmap set back).
    void FreeMissionData(bool server) override;         // 0x0048cb70
    // RestartMission(false).
    void StopMission(bool server) override;             // 0x0048cb60
    // Every switched lightmap set back.
    void RestartMission(bool server) override;          // 0x0048c790
    // The switches, last first: the lightmap now and the frame's name.
    bool SaveGame(C_chunk &chunk) override;             // 0x0048c820
    bool LoadGame(C_chunk &chunk) override;             // 0x0048c920

    // The groups of the chunk the scene loader is in (false without one); the old groups and
    // switches dropped first.
    bool LoadGroups(C_chunk *chunk);                    // 0x0048cd70
    // The group of that name (case insensitive; the last of equal names), or null.
    S_LightmapGroup *FindGroup(const char *name) const;   // 0x0041d5e0
    // Switches the visual's lightmap (a switch back to its original one forgets it).
    void SetFrameLightmap(I3D_frame *frame, int lightmap);   // 0x0048d270
    // Switches the lightmap of every lightmapped visual of the group.
    void SetGroupLightmap(const S_LightmapGroup *group, int lightmap);   // 0x0048d4e0

private:
    void DeleteGroups();
    void DeleteChanges();

    // (+0x04 the C_Module part.)
    std::vector<S_LightmapGroup *> m_groups;            // +0x0C
    std::vector<S_LightmapChange *> m_changes;          // +0x1C
};
extern C_LightMapGroupsModule g_lightMapGroupsModule;   // 0x0089bbbc (static init 0x0048c740)
C_LightMapGroupsModule *GetLightMapGroups();            // 0x0048cb00
