// Photo places (C_PhotoPlace; original 0x0060b610..0x0060c170).
#include "game/photo_place.h"

#include "common/chunk.h"
#include "game/frame_box_volume.h"
#include "game/pending.h"                               // g_texts

namespace {

constexpr uint16_t PROPERTY_TEXT = 100;                 // ReadProperties' chunk: the name's text
constexpr uint16_t CHUNK_PHOTOGRAPHED = 3000;           // the saved game's

}  // namespace

C_PhotoPlace::C_PhotoPlace() : C_Actor(ACTOR_PHOTO_PLACE)   // 0x0060b6d0
{
    m_name = g_texts.Get(-1);
}

void C_PhotoPlace::CopyFrom(const C_Actor *source)      // 0x0060b7e0
{
    C_Actor::CopyFrom(source);
    m_textId = static_cast<const C_PhotoPlace *>(source)->m_textId;
}

void C_PhotoPlace::StartMission(bool server)            // 0x0060b820 (DS 0x006340a0)
{
    Activate(server);
    if (m_started)
        return;
    m_photographed = false;
    m_started = true;
    m_name = g_texts.Get(m_textId);
    MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, COLLISION_TYPE_PLACE, this);
    g_collisionWorld.AddVolume(&m_volume);
}

void C_PhotoPlace::StopMission(bool server)             // 0x0060bb50 (DS 0x006344a0)
{
    m_started = false;
    g_collisionWorld.RemoveVolume(&m_volume);
    Deactivate(server);
}

bool C_PhotoPlace::WriteProperties(C_chunk &chunk)      // 0x0060bb80
{
    chunk.Begin(PROPERTY_TEXT);
    chunk.Write(m_textId);
    chunk.Ascend();
    return true;
}

bool C_PhotoPlace::ReadProperties(C_chunk &chunk)       // 0x0060bc80
{
    // Quirk kept: a chunk of another id is entered and not left (its contents are read as
    // chunks).
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != PROPERTY_TEXT)
            continue;
        chunk.Read(m_textId);
        chunk.Ascend();
    }
    return true;
}

int C_PhotoPlace::GetMaterialId(I3D_frame *part)        // 0x0060be40
{
    int id = GetFrameMaterialId(part);
    if (id != -1)
        return id;
    if (part != m_frame) {
        id = GetFrameMaterialId(m_frame);
        if (id != -1)
            return id;
    }
    return 11;
}

bool C_PhotoPlace::SaveGame(C_chunk &chunk)             // 0x0060be90
{
    chunk.Begin(CHUNK_PHOTOGRAPHED);
    chunk.Write(static_cast<uint8_t>(m_photographed));
    chunk.Ascend();
    return true;
}

bool C_PhotoPlace::LoadGame(C_chunk &chunk)             // 0x0060bf90
{
    // Quirk kept: as ReadProperties.
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != CHUNK_PHOTOGRAPHED)
            continue;
        uint8_t photographed;
        if (chunk.Read(photographed))
            m_photographed = photographed != 0;
        chunk.Ascend();
    }
    return true;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_PhotoPlaceCreator::Create()                  // 0x0060b640
{
    return new C_PhotoPlace();                          // _aligned_malloc(0x140, 16) in the original
}

void C_PhotoPlaceCreator::Destroy(C_Actor *actor)       // 0x0060b6b0
{
    delete actor;
}

static C_PhotoPlaceCreator g_photoPlaceCreator;         // 0x008aca2c

// Registers the creator (static initialiser 0x0060b620, thunk 0x0060b610).
static const bool g_photoPlaceCreatorRegistered = [] {
    g_actorCreators[ACTOR_PHOTO_PLACE] = &g_photoPlaceCreator;
    return true;
}();
