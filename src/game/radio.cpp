// Radios (C_Radio; original 0x0060a6f0..0x0060b610).
#include "game/radio.h"

#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage
#include "game/frame_box_volume.h"
#include "game/human.h"
#include "game/pending.h"                               // g_gameRole, g_texts
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// ReadProperties' / WriteProperties' chunks.
enum : uint16_t {
    PROPERTY_SOUND_ITEM = 100,                          // the sound, the item type
    PROPERTY_SIDE = 101,
};
constexpr uint16_t CHUNK_STATE = 3000;                  // the saved game's

constexpr int TEXT_RADIO = 0x1BF8;                      // the actor's name
constexpr int SIDE_ANY = 2;
constexpr int STATE_ON = 1;
constexpr int STATE_USED = 2;
// The state's bits: (int)(log(3) / log(2) + 1), worked out at run time by the original.
constexpr int STATE_BITS = 2;

}  // namespace

C_Radio::C_Radio() : C_Actor(ACTOR_RADIO)               // 0x0060a7b0
{
    m_name = g_texts.Get(TEXT_RADIO);
}

void C_Radio::CopyFrom(const C_Actor *source)           // 0x0060a8d0
{
    C_Actor::CopyFrom(source);
    const auto *other = static_cast<const C_Radio *>(source);
    m_sound = other->m_sound;
    m_itemId = other->m_itemId;
    m_side = other->m_side;
}

void C_Radio::StartMission(bool server)                 // 0x0060a930 (DS 0x00632d00)
{
    Activate(server);
    if (m_started)
        return;
    m_started = true;
    MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, COLLISION_TYPE_PLACE, this);
    g_collisionWorld.AddVolume(&m_volume);
    m_state = STATE_ON;
}

void C_Radio::StopMission(bool server)                  // 0x0060ac00
{
    m_started = false;
    g_collisionWorld.RemoveVolume(&m_volume);
    Deactivate(server);
}

bool C_Radio::WriteProperties(C_chunk &chunk)           // 0x0060ac30
{
    chunk.Begin(PROPERTY_SOUND_ITEM);
    chunk.Write(m_sound);
    chunk.Write(m_itemId);
    chunk.Ascend();
    chunk.Begin(PROPERTY_SIDE);
    chunk.Write(m_side);
    chunk.Ascend();
    return true;
}

bool C_Radio::ReadProperties(C_chunk &chunk)            // 0x0060ae40
{
    // Quirk kept: chunk 101 is not left, nor a chunk of another id (their contents are read as
    // chunks).
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == PROPERTY_SOUND_ITEM) {
            chunk.Read(m_sound);
            chunk.Read(m_itemId);
            chunk.Ascend();
        } else if (id == PROPERTY_SIDE) {
            chunk.Read(m_side);
        }
    }
    return true;
}

void C_Radio::OnMessage(C_BitStream *msg)               // 0x0060b090
{
    m_state = static_cast<int>(msg->ReadBits(STATE_BITS));
    if (m_state != STATE_USED || m_sound < 0)
        return;
    S_SoundRequest request;
    const S_matrix &world = m_frame->GetMatrix();
    request.pos = world(3);
    request.posW = world.m[3][3];
    request.flags = SOUND_SILENT_TO_AI;
    request.chapter = SOUND_CHAPTER_OBJECT_MANIPULATION;
    request.group = m_sound;
    GetSoundModule()->Play(request);
}

int C_Radio::GetMaterialId(I3D_frame *part)             // 0x0060b170
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

bool C_Radio::SaveGame(C_chunk &chunk)                  // 0x0060b1c0
{
    chunk.Begin(CHUNK_STATE);
    chunk.Write(m_state);
    chunk.Ascend();
    return true;
}

bool C_Radio::LoadGame(C_chunk &chunk)                  // 0x0060b2c0
{
    // Quirk kept: a chunk of another id is entered and not left.
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != CHUNK_STATE)
            continue;
        chunk.Read(m_state);
        chunk.Ascend();
    }
    return true;
}

bool C_Radio::Accepts(C_Human *human) const
{
    if (m_itemId >= 0 && !human->FindItem(m_itemId))
        return false;
    return m_side == SIDE_ANY || m_side == human->GetTeam(false);
}

bool C_Radio::CanUse(C_Human *human) const              // 0x0060b460
{
    return m_state == STATE_ON && Accepts(human);
}

bool C_Radio::Use(C_Human *human)                       // 0x0060b4c0
{
    if (!Accepts(human))
        return false;
    m_state = STATE_USED;
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(static_cast<uint32_t>(m_state), STATE_BITS);
    SendActorMessage(msg);
    return true;
}

void C_Radio::SetState(int state)                       // 0x0060b570
{
    m_state = state;
    if (IsLocalGame())
        return;
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(static_cast<uint32_t>(m_state), STATE_BITS);
    SendActorMessage(msg);
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_RadioCreator::Create()                       // 0x0060a720
{
    return new C_Radio();                               // _aligned_malloc(0x150, 16) in the original
}

void C_RadioCreator::Destroy(C_Actor *actor)            // 0x0060a790
{
    delete actor;
}

static C_RadioCreator g_radioCreator;                   // 0x008aca28

// Registers the creator (static initialiser 0x0060a700, thunk 0x0060a6f0).
static const bool g_radioCreatorRegistered = [] {
    g_actorCreators[ACTOR_RADIO] = &g_radioCreator;
    return true;
}();
