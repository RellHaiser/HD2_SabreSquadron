// Explosive places (C_ExplosivePlace; original 0x0060c170..0x0060da80).
#include "game/explosive_place.h"

#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage, g_actorsModule
#include "game/frame_box_volume.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"                               // g_gameRole, g_texts
#include "game/shots.h"                                 // g_shotManager
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// ReadProperties' / WriteProperties' chunks.
enum : uint16_t {
    PROPERTY_TEXT_TIMED = 100,                          // the name's text, the timer flag
    PROPERTY_ITEM = 101,
    PROPERTY_DELAY = 102,
    PROPERTY_EXPLOSION = 103,                           // sound, effect, damage, range
    PROPERTY_START_PLACED = 104,
    PROPERTY_HIDDEN = 105,
};
constexpr uint16_t CHUNK_STATE = 3000;                  // the saved game's

// The messages.
enum : uint32_t {
    MSG_EXPLODED = 0,
    MSG_DISARMED = 1,
    MSG_PLACED = 2,                                     // the human's id, the item's uid
    MSG_STATE = 3,
};
// The kind's and the state's bits: (int)(log(3) / log(2) + 1), worked out at run time by the
// original (quirk kept: state 4 does not fit and goes out as 0).
constexpr int KIND_BITS = 2;
constexpr int STATE_BITS = 2;
constexpr int HUMAN_ID_BITS = 10;
constexpr int UID_BITS = 16;

}  // namespace

C_ExplosivePlace::C_ExplosivePlace() : C_Actor(ACTOR_EXPLOSIVE_PLACE)   // 0x0060c230
{
}

void C_ExplosivePlace::Update(int frameTime)            // 0x0060c370
{
    if (IsServer() || !m_timed || m_timeLeft <= 0)
        return;
    m_timeLeft -= frameTime;
    if (m_timeLeft < 1)
        m_timeLeft = 0;
}

void C_ExplosivePlace::Tick(int timeDelta)              // 0x0060c330
{
    if (!m_timed || m_timeLeft <= 0)
        return;
    m_timeLeft -= timeDelta;
    if (m_timeLeft < 1)
        Explode(m_planterId);
}

void C_ExplosivePlace::OnMessage(C_BitStream *msg)      // 0x0060cda0
{
    switch (msg->ReadBits(KIND_BITS)) {
    case MSG_EXPLODED:
        OnExploded();
        return;
    case MSG_DISARMED:
        OnDisarmed();
        return;
    case MSG_PLACED: {
        const int humanId = static_cast<int>(msg->ReadBits(HUMAN_ID_BITS));
        const int uid = static_cast<int16_t>(msg->ReadBits(UID_BITS));
        OnPlaced(humanId, uid);
        return;
    }
    case MSG_STATE:
        ApplyState(static_cast<int>(msg->ReadBits(STATE_BITS)));
        return;
    default:
        return;
    }
}

bool C_ExplosivePlace::WriteProperties(C_chunk &chunk)  // 0x0060c6e0
{
    chunk.Begin(PROPERTY_TEXT_TIMED);
    chunk.Write(m_nameTextId);
    chunk.Write(m_timed);
    chunk.Ascend();
    chunk.Begin(PROPERTY_ITEM);
    chunk.Write(m_itemId);
    chunk.Ascend();
    chunk.Begin(PROPERTY_DELAY);
    chunk.Write(m_delay);
    chunk.Ascend();
    chunk.Begin(PROPERTY_EXPLOSION);
    chunk.Write(m_sound);
    chunk.Write(m_effect);
    chunk.Write(m_damage);
    chunk.Write(m_range);
    chunk.Ascend();
    chunk.Begin(PROPERTY_START_PLACED);
    chunk.Write(m_startPlaced);
    chunk.Ascend();
    chunk.Begin(PROPERTY_HIDDEN);
    chunk.Write(m_hidden);
    chunk.Ascend();
    return true;
}

bool C_ExplosivePlace::ReadProperties(C_chunk &chunk)   // 0x0060ca30
{
    // Quirk kept: a chunk of another id is entered and not left (its contents are read as
    // chunks).
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case PROPERTY_TEXT_TIMED:
            chunk.Read(m_nameTextId);
            chunk.Read(m_timed);
            chunk.Ascend();
            break;
        case PROPERTY_ITEM:
            chunk.Read(m_itemId);
            chunk.Ascend();
            break;
        case PROPERTY_DELAY:
            chunk.Read(m_delay);
            chunk.Ascend();
            break;
        case PROPERTY_EXPLOSION:
            chunk.Read(m_sound);
            chunk.Read(m_effect);
            chunk.Read(m_damage);
            chunk.Read(m_range);
            chunk.Ascend();
            break;
        case PROPERTY_START_PLACED:
            chunk.Read(m_startPlaced);
            chunk.Ascend();
            break;
        case PROPERTY_HIDDEN:
            chunk.Read(m_hidden);
            chunk.Ascend();
            break;
        default:
            break;
        }
    }
    return true;
}

void C_ExplosivePlace::CopyFrom(const C_Actor *source)  // 0x0060c300
{
    C_Actor::CopyFrom(source);
    const auto *other = static_cast<const C_ExplosivePlace *>(source);
    m_timed = other->m_timed;
    m_nameTextId = other->m_nameTextId;
}

void C_ExplosivePlace::StartMission(bool server)        // 0x0060c3b0
{
    Activate(server);
    if (m_started)
        return;
    m_planterId = -1;
    m_started = true;
    m_timeLeft = 0;
    float boxRadius;
    MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, COLLISION_TYPE_LIGHT_OBJECT, this, &boxRadius);
    // (A stale local in the original: the box's own radius.)
    m_volume.centerW = boxRadius;
    m_onFrame = m_frame->FindChildFrame("explosive_on", FRAME_MASK_ANY);
    m_offFrame = m_frame->FindChildFrame("explosive_off", FRAME_MASK_ANY);
    ApplyState(m_startPlaced ? EXPLOSIVE_PLACED : EXPLOSIVE_EMPTY);
}

void C_ExplosivePlace::StopMission(bool server)         // 0x0060c6b0
{
    if (m_started) {
        m_started = false;
        ApplyState(EXPLOSIVE_STOPPED);
    }
    Deactivate(server);
}

void C_ExplosivePlace::RestartMission(bool)             // 0x0060cf20 (DS 0x00635e40)
{
#ifndef HD2_DEDICATED   // the server has only the server's side
    if (IsClient())
        StopMission(false);
#endif
    if (IsServer()) {
        StopMission(true);
        StartMission(true);
    }
#ifndef HD2_DEDICATED   // the server has only the server's side
    if (IsClient())
        StartMission(false);
#endif
}

bool C_ExplosivePlace::SaveGame(C_chunk &chunk)         // 0x0060cf70
{
    chunk.Begin(CHUNK_STATE);
    chunk.Write(m_state);
    chunk.Write(m_timeLeft);
    chunk.Write(m_planterId);
    chunk.Ascend();
    return true;
}

bool C_ExplosivePlace::LoadGame(C_chunk &chunk)         // 0x0060d0c0
{
    // Quirk kept: a chunk of another id is entered and not left; a short chunk keeps the state
    // read last.
    int state = 0;
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != CHUNK_STATE)
            continue;
        chunk.Read(state);
        ApplyState(state);
        chunk.Read(m_timeLeft);
        chunk.Read(m_planterId);
        chunk.Ascend();
    }
    return true;
}

void C_ExplosivePlace::WriteState(C_BitStream *msg)     // 0x0060ce60
{
    msg->WriteBits(static_cast<uint32_t>(m_state), STATE_BITS);
}

void C_ExplosivePlace::ReadState(C_BitStream *msg)      // 0x0060cea0
{
    ApplyState(static_cast<int>(msg->ReadBits(STATE_BITS)));
}

int C_ExplosivePlace::GetMaterialId(I3D_frame *part)    // 0x0060cee0
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

void C_ExplosivePlace::SetState(int state)              // 0x0060d300
{
    if (state == m_state)
        return;
    ApplyState(state);
    if (IsLocalGame())
        return;
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(MSG_STATE, KIND_BITS);
    msg->WriteBits(static_cast<uint32_t>(state), STATE_BITS);
    SendActorMessage(msg);
}

void C_ExplosivePlace::ApplyState(int state)            // 0x0060d380
{
    if (state == m_state)
        return;
    m_state = state;
    if (!m_onFrame || !m_offFrame)
        return;
    bool volume = false;
    switch (state) {
    case EXPLOSIVE_EMPTY:
        m_onFrame->SetOn(false);
        m_timeLeft = 0;
        if (!m_hidden) {
            m_offFrame->SetOn(true);
            m_name = g_texts.Get(m_nameTextId);
            volume = true;
        } else {
            m_offFrame->SetOn(false);
            m_name = "Hidden, you should not see this text!!";
        }
        break;
    case EXPLOSIVE_PLACED: {
        m_onFrame->SetOn(true);
        m_offFrame->SetOn(false);
        m_timeLeft = m_delay;
        // Fix: an item type without a definition names it by text -1 (the original reads through
        // null).
        const C_ItemDef *def = GetItemDef(m_itemId);
        m_name = g_texts.Get(def ? def->GetTextId() : -1);
        volume = true;
        break;
    }
    case EXPLOSIVE_EXPLODED:
        m_onFrame->SetOn(false);
        m_offFrame->SetOn(false);
        m_timeLeft = 0;
        m_name = "Exploded, you should not see this text!!";
        break;
    case EXPLOSIVE_HIDDEN:
        m_onFrame->SetOn(false);
        m_offFrame->SetOn(false);
        m_timeLeft = 0;
        m_name = "Hidden, you should not see this text!!";
        break;
    case EXPLOSIVE_STOPPED:
        m_timeLeft = 0;
        m_onFrame->SetOn(true);
        m_offFrame->SetOn(true);
        break;
    default:
        m_frame->Update();
        return;
    }
    if (volume) {
        if (!m_volumeAdded)
            g_collisionWorld.AddVolume(&m_volume);
        m_volumeAdded = true;
    } else {
        if (m_volumeAdded)
            g_collisionWorld.RemoveVolume(&m_volume);
        m_volumeAdded = false;
    }
    m_frame->Update();
}

bool C_ExplosivePlace::Place(C_Human *human)            // 0x0060d6f0
{
    if (m_state != EXPLOSIVE_EMPTY)
        return false;
    // (Who placed it is noted even when the item is missing.)
    m_planterId = human->GetNetId();
    C_Item *item = human->FindItem(m_itemId);
    if (!item)
        return false;
    const int uid = item->GetUid();
    human->RemoveItem(uid, true);
    ApplyState(EXPLOSIVE_PLACED);
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(MSG_PLACED, KIND_BITS);
    msg->WriteBits(static_cast<uint32_t>(m_planterId), HUMAN_ID_BITS);
    msg->WriteBits(static_cast<uint32_t>(uid), UID_BITS);
    SendActorMessage(msg);
    return true;
}

void C_ExplosivePlace::OnPlaced(int humanId, int uid)   // 0x0060d7b0
{
    if (IsServer())
        return;
    m_planterId = humanId;
    auto *human = static_cast<C_Human *>(g_actorsModule.GetHuman(humanId));
    if (!human || !human->FindItemByUid(uid))
        return;
    human->RemoveItem(uid, true);
    ApplyState(EXPLOSIVE_PLACED);
}

void C_ExplosivePlace::ExplodeHarmless()                // 0x0060d810
{
    ApplyState(EXPLOSIVE_EXPLODED);
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(MSG_EXPLODED, KIND_BITS);
    SendActorMessage(msg);
}

void C_ExplosivePlace::Explode(int planterId)           // 0x0060d870 (DS 0x006369e0)
{
    // (Copy protection dropped: in single player missions after the 11th the original returned
    // here when its table check failed. The server's copy has no table check either.)
    if (planterId < 0) {
        ExplodeHarmless();
        return;
    }
    C_Actor *planter = g_actorsModule.GetHuman(planterId);
    if (!planter) {
        ExplodeHarmless();
        return;
    }
    ApplyState(EXPLOSIVE_EXPLODED);
    g_shotManager.Explode(planter, m_frame->GetWorldPos(), static_cast<float>(m_range), static_cast<float>(m_damage),
                          m_sound);
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(MSG_EXPLODED, KIND_BITS);
    SendActorMessage(msg);
}

void C_ExplosivePlace::OnExploded()                     // 0x0060d980
{
    ApplyState(EXPLOSIVE_EXPLODED);
    g_shotManager.PlayExplosion(m_frame->GetWorldPos(), S_vector(0.0f, 1.0f, 0.0f), m_effect, -1, m_sound);
}

bool C_ExplosivePlace::Disarm(C_Human *)                // 0x0060d9f0
{
    ApplyState(EXPLOSIVE_EMPTY);
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(MSG_DISARMED, KIND_BITS);
    SendActorMessage(msg);
    return true;
}

void C_ExplosivePlace::OnDisarmed()                     // 0x0060da50
{
    if (!IsServer())
        ApplyState(EXPLOSIVE_EMPTY);
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_ExplosivePlaceCreator::Create()              // 0x0060c1a0
{
    return new C_ExplosivePlace();                      // _aligned_malloc(0x180, 16) in the original
}

void C_ExplosivePlaceCreator::Destroy(C_Actor *actor)   // 0x0060c210
{
    delete actor;
}

static C_ExplosivePlaceCreator g_explosivePlaceCreator; // 0x008aca30

// Registers the creator (static initialiser 0x0060c180, thunk 0x0060c170).
static const bool g_explosivePlaceCreatorRegistered = [] {
    g_actorCreators[ACTOR_EXPLOSIVE_PLACE] = &g_explosivePlaceCreator;
    return true;
}();
