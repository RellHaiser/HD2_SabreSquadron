// The texture shifting module ("TShift", module 10; the object 0x0089bc34; game/tshift.cpp, original
// source 0x0048e130..0x0048eaa0): the lightmapped and object visuals of the mission's scene whose
// textures move (the loader of the scene file adds them, C_game 0x006cd3fd), switched on and off
// by the scripts (frm_setshifting). The module itself only keeps the list.
#pragma once

#include <cstdint>
#include <vector>

#include "game/module.h"

class I3D_frame;
class I3D_mesh;

// A shifting visual (0x1C bytes; made by 0x006cdc60).
struct S_TShiftEntry {
    I3D_mesh *mesh;                                     // +0x00 the visual's (the scripts find it by it)
    I3D_frame *frame;                                   // +0x04
    bool on = true;                                     // +0x08
    bool reverse = false;                               // +0x09
    int unknown0C = 0;                                  // +0x0C (from the scene file)
    int unknown10 = 0;                                  // +0x10 (from the scene file)
    int unknown14 = 0;
    int unknown18 = 0;
};

class C_TShiftModule : public C_Module {               // vtable 0x00810418
public:
    C_TShiftModule(int id, const char *name);           // 0x0048e1f0
    ~C_TShiftModule() override;                         // 0x0048e250

    // The client's list dropped.
    void FreeMissionData(bool server) override;         // 0x0048e190

    // Whether a visual of that mesh shifts.
    bool HasMesh(const I3D_mesh *mesh) const;           // 0x0048e280
    // Whether the frame shifts.
    bool HasFrame(const I3D_frame *frame) const;        // 0x0048e2d0
    // The entry of that mesh (the last of equal ones), or null.
    S_TShiftEntry *GetEntry(const I3D_mesh *mesh);      // 0x0048e780
    // (The scene loader's vector::insert at the end, 0x006d39f0.)
    void AddEntry(const S_TShiftEntry &entry) { m_entries.push_back(entry); }
    // The rest has no callers in the game (Ghidra had not found them as functions).
    // A visual with that mesh added, on, unless the mesh has one already.
    void AddVisual(I3D_frame *frame, I3D_mesh *mesh);   // 0x0048e330
    // The last entry of that mesh / that frame / under that model (the model frame nearest above
    // the entry's frame, else its topmost one) removed.
    void RemoveMesh(const I3D_mesh *mesh);              // 0x0048e580
    void RemoveFrame(const I3D_frame *frame);           // 0x0048e620
    void RemoveModel(const I3D_frame *model);           // 0x0048e6d0

private:
    // (+0x04 the C_Module part.)
    std::vector<S_TShiftEntry> m_entries;               // +0x0C
};
extern C_TShiftModule g_tshiftModule;                   // 0x0089bc34 (static init 0x0048e130)
