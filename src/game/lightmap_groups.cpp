// The lightmap groups module (0x0048c740..0x0048d8f0): see game/lightmap_groups.h.
#include "game/lightmap_groups.h"

#include <cstring>

#include "common/chunk.h"
#include "game/game.h"
#include "ls3d/i3d.h"

C_LightMapGroupsModule g_lightMapGroupsModule(MODULE_LIGHTMAP_GROUPS, "LMapsGrp");   // 0x0089bbbc

namespace {

enum : uint16_t {
    CHUNK_GROUP = 0x100,                                // a group's name
    CHUNK_GROUP_FRAME = 0x110,                          // a frame of the last group
};

// A frame of the mission's scene, else of the driver's (the scene's models).
I3D_frame *FindSceneFrame(const char *name, uint32_t mask)
{
    I3D_scene *scene = GetGame()->GetScene();
    if (I3D_frame *frame = scene->FindFrame(name, mask))
        return frame;
    return I3DGetDriver()->FindFrame(scene, name);
}

I3D_lmap_visual *AsLmap(I3D_frame *frame)
{
    return static_cast<I3D_lmap_visual *>(frame);
}

}  // namespace

C_LightMapGroupsModule *GetLightMapGroups()             // 0x0048cb00 (DS 0x004a2660)
{
    return &g_lightMapGroupsModule;
}

bool S_LightmapGroup::HasFrame(const char *frame) const   // 0x0048d1d0
{
    for (size_t i = frames.size(); i-- > 0;) {
        if (_stricmp(frame, frames[i]) == 0)
            return true;
    }
    return false;
}

S_LightmapChange::S_LightmapChange(I3D_frame *frame_) : frame(frame_)
{
    frame->AddRef();
    original = AsLmap(frame)->GetLightmapIndex();
}

S_LightmapChange::~S_LightmapChange()                   // 0x0048d250
{
    frame->Release();
}

C_LightMapGroupsModule::C_LightMapGroupsModule(int id, const char *name) : C_Module(id, name)   // 0x0048cb10
{
}

C_LightMapGroupsModule::~C_LightMapGroupsModule() = default;   // 0x0048cd30 (the vectors only)

void C_LightMapGroupsModule::DeleteGroups()
{
    for (size_t i = m_groups.size(); i-- > 0;) {
        delete m_groups[i];
        m_groups[i] = nullptr;
    }
    m_groups.clear();
}

void C_LightMapGroupsModule::DeleteChanges()
{
    for (size_t i = m_changes.size(); i-- > 0;)
        delete m_changes[i];
    m_changes.clear();
}

void C_LightMapGroupsModule::RestartMission(bool)       // 0x0048c790
{
    for (size_t i = m_changes.size(); i-- > 0;) {
        S_LightmapChange *change = m_changes[i];
        AsLmap(change->frame)->SelectLightmap(change->original);
        change->frame->Update();
    }
    DeleteChanges();
}

bool C_LightMapGroupsModule::SaveGame(C_chunk &chunk)   // 0x0048c820
{
    const int count = static_cast<int>(m_changes.size());
    chunk.Write(count);
    for (int i = count; i-- > 0;) {
        I3D_frame *frame = m_changes[i]->frame;
        const int lightmap = AsLmap(frame)->GetLightmapIndex();
        chunk.Write(lightmap);
        chunk.WriteSizedString(frame->GetName());
    }
    return true;
}

bool C_LightMapGroupsModule::LoadGame(C_chunk &chunk)   // 0x0048c920
{
    int count = 0;
    chunk.Read(count);
    for (; count != 0; --count) {
        int lightmap = 0;
        chunk.Read(lightmap);
        char name[0x400];
        // Fix: a longer name is skipped; the original read its rest into one byte on the stack.
        chunk.ReadSizedString(name, 0x3FF);
        if (I3D_frame *frame = FindSceneFrame(name, FRAME_MASK_ALL))
            SetFrameLightmap(frame, lightmap);
    }
    return true;
}

void C_LightMapGroupsModule::StopMission(bool)          // 0x0048cb60 (DS 0x004a26f0)
{
    RestartMission(false);
}

void C_LightMapGroupsModule::FreeMissionData(bool server)   // 0x0048cb70
{
    if (server)
        return;
    DeleteGroups();
    DeleteChanges();
}

bool C_LightMapGroupsModule::LoadGroups(C_chunk *chunk)   // 0x0048cd70
{
    if (!chunk)
        return false;
    DeleteGroups();
    DeleteChanges();
    // (The id of a header that failed to read is the last one's.)
    uint16_t id = 0xFFFF;
    for (;;) {
        // Until the chunk's end (a position past it goes on; -1 stops, as a failed Tell does).
        const int left = chunk->RemainingSize();
        if (left == 0 || left == -1)
            break;
        chunk->Descend(id);
        // (The original reads the strings without a terminator; the files' carry theirs.)
        if (id == CHUNK_GROUP) {
            char text[1000];
            chunk->ReadString(text, sizeof text);
            const C_str name(text);
            chunk->Ascend();
            if (!FindGroup(name) && std::strlen(name) != 0)
                m_groups.push_back(new S_LightmapGroup(name));
        } else if (id == CHUNK_GROUP_FRAME) {
            char text[1000];
            chunk->ReadString(text, sizeof text);
            const C_str name(text);
            chunk->Ascend();
            // Fix: a frame before the first group is dropped; the original read before the vector.
            S_LightmapGroup *group = m_groups.empty() ? nullptr : m_groups.back();
            if (group && !group->HasFrame(name))
                group->frames.push_back(name);
        }
        // (Any other chunk is entered and not left: its children are read as the groups'.)
    }
    return true;
}

S_LightmapGroup *C_LightMapGroupsModule::FindGroup(const char *name) const   // 0x0041d5e0
{
    if (!name)
        return nullptr;
    for (size_t i = m_groups.size(); i-- > 0;) {
        if (_stricmp(m_groups[i]->name, name) == 0)
            return m_groups[i];
    }
    return nullptr;
}

void C_LightMapGroupsModule::SetFrameLightmap(I3D_frame *frame, int lightmap)   // 0x0048d270
{
    if (!frame)
        return;
    for (size_t i = m_changes.size(); i-- > 0;) {
        S_LightmapChange *change = m_changes[i];
        if (change->frame != frame)
            continue;
        AsLmap(frame)->SelectLightmap(lightmap);
        frame->Update();
        if (lightmap == change->original) {
            delete change;
            m_changes[i] = m_changes.back();
            m_changes.pop_back();
        }
        return;
    }
    m_changes.push_back(new S_LightmapChange(frame));
    AsLmap(frame)->SelectLightmap(lightmap);
    frame->Update();
}

void C_LightMapGroupsModule::SetGroupLightmap(const S_LightmapGroup *group, int lightmap)   // 0x0048d4e0 (DS 0x004a3330)
{
    if (!group || !GetGame()->GetScene())
        return;
    for (size_t i = group->frames.size(); i-- > 0;) {
        I3D_frame *frame = FindSceneFrame(group->frames[i], FRAME_MASK_ANY);
        if (frame && frame->GetVisualType() == I3DVISUAL_LMAP)
            SetFrameLightmap(frame, lightmap);
    }
}
