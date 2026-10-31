// The texture shifting module (0x0048e130..0x0048eaa0): see game/tshift.h.
#include "game/tshift.h"

#include "ls3d/i3d.h"

C_TShiftModule g_tshiftModule(MODULE_TSHIFT, "TShift"); // 0x0089bc34

C_TShiftModule::C_TShiftModule(int id, const char *name) : C_Module(id, name)   // 0x0048e1f0
{
}

C_TShiftModule::~C_TShiftModule() = default;            // 0x0048e250

void C_TShiftModule::FreeMissionData(bool server)       // 0x0048e190
{
    if (!server)
        m_entries.clear();
}

bool C_TShiftModule::HasFrame(const I3D_frame *frame) const   // 0x0048e2d0
{
    for (size_t i = m_entries.size(); i-- > 0;) {
        if (m_entries[i].frame == frame)
            return true;
    }
    return false;
}

bool C_TShiftModule::HasMesh(const I3D_mesh *mesh) const   // 0x0048e280
{
    for (size_t i = m_entries.size(); i-- > 0;) {
        if (m_entries[i].mesh == mesh)
            return true;
    }
    return false;
}

void C_TShiftModule::AddVisual(I3D_frame *frame, I3D_mesh *mesh)   // 0x0048e330
{
    if (HasMesh(mesh))
        return;
    // Fix: reverse starts false; the original leaves a byte of its stack there (the loader's
    // entries too, 0x006cdc60).
    m_entries.push_back({mesh, frame});
}

void C_TShiftModule::RemoveMesh(const I3D_mesh *mesh)   // 0x0048e580
{
    if (!HasMesh(mesh))
        return;
    for (size_t i = m_entries.size(); i-- > 0;) {
        if (m_entries[i].mesh == mesh) {
            m_entries.erase(m_entries.begin() + static_cast<long>(i));
            return;
        }
    }
}

void C_TShiftModule::RemoveFrame(const I3D_frame *frame)   // 0x0048e620
{
    if (!HasFrame(frame))
        return;
    for (size_t i = m_entries.size(); i-- > 0;) {
        if (m_entries[i].frame == frame) {
            m_entries.erase(m_entries.begin() + static_cast<long>(i));
            return;
        }
    }
}

void C_TShiftModule::RemoveModel(const I3D_frame *model)   // 0x0048e6d0
{
    for (size_t i = m_entries.size(); i-- > 0;) {
        const I3D_frame *root = m_entries[i].frame;
        if (root) {
            while (const I3D_frame *parent = root->GetParent()) {
                root = parent;
                if (parent->GetType() == FRAME_MODEL)
                    break;
            }
        }
        if (root == model) {
            m_entries.erase(m_entries.begin() + static_cast<long>(i));
            return;
        }
    }
}

S_TShiftEntry *C_TShiftModule::GetEntry(const I3D_mesh *mesh)   // 0x0048e780
{
    for (size_t i = m_entries.size(); i-- > 0;) {
        if (m_entries[i].mesh == mesh)
            return &m_entries[i];
    }
    return nullptr;
}
