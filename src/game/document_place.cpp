// Document places (C_DocumentPlace; original 0x0060dab0..0x0060ed90).
#include "game/document_place.h"

#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage, g_actorsModule
#include "game/frame_box_volume.h"
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"                               // g_gameRole, g_texts
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// ReadProperties' / WriteProperties' chunks.
enum : uint16_t {
    PROPERTY_TEXT_ITEM = 100,                           // the name's text, the item type
    PROPERTY_SIGNAL_TEXT = 101,
};
constexpr uint16_t CHUNK_STATE = 3000;                  // the saved game's

// The messages: a delivery (the human's id, the item's uid) or a state.
enum : uint32_t {
    MSG_DELIVERED = 0,
    MSG_STATE = 1,
};
// The original writes and reads the kind with the bit count (int)(log(0) / log(2) + 1), which
// __ftol makes INT_MIN; its bit stream then masks with its table's first entry (0) and moves on
// by a word, so the writer ORs the kind into the word and the reader always gets 0 (quirk kept: a
// state message reaches the clients as a delivery, the state's bits read as the human's id).
// Fix: before that the original's growing stream reserves 2^31 bits more, a 256 MB reallocation
// per message; the port writes the kind in a whole word, which moves the stream on the same way.
constexpr int KIND_BITS = 32;
constexpr int HUMAN_ID_BITS = 10;
constexpr int UID_BITS = 16;
// The state's bits: (int)(log(2) / log(2) + 1), worked out at run time by the original.
constexpr int STATE_BITS = 2;

}  // namespace

C_DocumentPlace::C_DocumentPlace() : C_Actor(ACTOR_DOCUMENT_PLACE)   // 0x0060db70
{
}

C_DocumentPlace::~C_DocumentPlace()                     // 0x0060dbf0
{
    // Fix: the documents' model left by a missing StopMission is let go (the original leaks it).
    if (m_documents)
        m_documents->Release();
}

void C_DocumentPlace::CopyFrom(const C_Actor *source)   // 0x0060dc10
{
    C_Actor::CopyFrom(source);
    const auto *other = static_cast<const C_DocumentPlace *>(source);
    m_itemId = other->m_itemId;
    m_nameTextId = other->m_nameTextId;
    m_textId = other->m_textId;
}

void C_DocumentPlace::StartMission(bool server)         // 0x0060dc70 (DS 0x00636f60)
{
    Activate(server);
    if (m_started)
        return;
    MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, COLLISION_TYPE_LIGHT_OBJECT, this);
    ApplyState(DOCUMENTS_WAITING);
    m_started = true;
}

void C_DocumentPlace::StopMission(bool server)          // 0x0060df20
{
    if (m_started)
        ApplyState(DOCUMENTS_STOPPED);
    m_started = false;
    Deactivate(server);
}

bool C_DocumentPlace::WriteProperties(C_chunk &chunk)   // 0x0060df50 (DS 0x00637300)
{
    chunk.Begin(PROPERTY_TEXT_ITEM);
    chunk.Write(m_nameTextId);
    chunk.Write(m_itemId);
    chunk.Ascend();
    chunk.Begin(PROPERTY_SIGNAL_TEXT);
    chunk.Write(m_textId);
    chunk.Ascend();
    return true;
}

bool C_DocumentPlace::ReadProperties(C_chunk &chunk)    // 0x0060e160
{
    // Quirk kept: a chunk of another id is entered and not left (its contents are read as
    // chunks).
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == PROPERTY_TEXT_ITEM) {
            chunk.Read(m_nameTextId);
            chunk.Read(m_itemId);
            chunk.Ascend();
        } else if (id == PROPERTY_SIGNAL_TEXT) {
            chunk.Read(m_textId);
            chunk.Ascend();
        }
    }
    return true;
}

void C_DocumentPlace::OnMessage(C_BitStream *msg)       // 0x0060e420
{
    // The kind always reads as a delivery (see KIND_BITS; the original's branch for MSG_STATE, the
    // state applied, is never taken).
    msg->ReadBits(KIND_BITS);
    const int humanId = static_cast<int>(msg->ReadBits(HUMAN_ID_BITS));
    const int uid = static_cast<int16_t>(msg->ReadBits(UID_BITS));
    OnDelivered(humanId, uid);
}

void C_DocumentPlace::WriteState(C_BitStream *msg)      // 0x0060e4d0
{
    msg->WriteBits(static_cast<uint32_t>(m_state), STATE_BITS);
}

void C_DocumentPlace::ReadState(C_BitStream *msg)       // 0x0060e510
{
    if (msg->ReadBits(STATE_BITS) == DOCUMENTS_DELIVERED)
        ShowDocuments();
}

void C_DocumentPlace::RestartMission(bool)              // 0x0060e560 (DS 0x00637ac0)
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

bool C_DocumentPlace::SaveGame(C_chunk &chunk)          // 0x0060e5b0
{
    chunk.Begin(CHUNK_STATE);
    chunk.Write(m_state);
    chunk.Ascend();
    return true;
}

bool C_DocumentPlace::LoadGame(C_chunk &chunk)          // 0x0060e6b0
{
    // Quirk kept: a chunk of another id is entered and not left.
    int state = DOCUMENTS_STOPPED;
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != CHUNK_STATE)
            continue;
        chunk.Read(state);
        chunk.Ascend();
    }
    ApplyState(state);
    return true;
}

void C_DocumentPlace::ShowDocuments()                   // 0x0060e860
{
    if (!m_documents) {
        C_ItemDef *def = GetItemDef(m_itemId);
        // Fix: without the item's definition nothing is shown (the original calls through null).
        if (!def)
            return;
        I3D_frame *model = def->GetModel();
        m_documents = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
        m_documents->Duplicate(model);
    }
    m_documents->LinkTo(m_frame, 0);
    m_documents->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    m_documents->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    m_documents->SetOn(true);
    m_documents->Update();
    m_frame->Update();
    // Left lying where the place is.
    m_documents->LinkTo(nullptr, I3DLINK_KEEP_WORLD);
    GetGame()->GetScene()->UpdateFrameSector(m_documents, m_documents->GetWorldPos());
    m_documents->Update();
    m_frame->Update();
    m_frame->SetOn(false);
}

bool C_DocumentPlace::Deliver(C_Human *human)           // 0x0060e9b0
{
    if (m_state != DOCUMENTS_WAITING)
        return false;
    C_Item *item = human->FindItem(m_itemId);
    if (!item)
        return false;
    const int uid = item->GetUid();
    human->RemoveItem(uid, true);
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(MSG_DELIVERED, KIND_BITS);
    msg->WriteBits(static_cast<uint32_t>(human->GetNetId()), HUMAN_ID_BITS);
    msg->WriteBits(static_cast<uint32_t>(uid), UID_BITS);
    SendActorMessage(msg);
    ApplyState(DOCUMENTS_DELIVERED);
    return true;
}

void C_DocumentPlace::OnDelivered(int humanId, int uid) // 0x0060ea70
{
    if (IsServer())
        return;
    auto *human = static_cast<C_Human *>(g_actorsModule.GetHuman(humanId));
    if (!human)
        return;
    C_Item *item = human->FindItemByUid(uid);
    if (!item)
        return;
    human->RemoveItem(item->GetUid(), true);
    ApplyState(DOCUMENTS_DELIVERED);
}

void C_DocumentPlace::SetState(int state)               // 0x0060eac0
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

void C_DocumentPlace::ApplyState(int state)             // 0x0060eb60
{
    if (state == m_state)
        return;
    m_state = state;
    switch (state) {
    case DOCUMENTS_WAITING:
        m_name = g_texts.Get(m_nameTextId);
        m_frame->SetOn(true);
        if (!m_volumeAdded)
            g_collisionWorld.AddVolume(&m_volume);
        m_volumeAdded = true;
        return;
    case DOCUMENTS_DELIVERED: {
        // Fix: an item type without a definition names it by text -1 (the original reads through
        // null).
        const C_ItemDef *def = GetItemDef(m_itemId);
        m_name = g_texts.Get(def ? def->GetTextId() : -1);
        ShowDocuments();
        if (m_volumeAdded)
            g_collisionWorld.RemoveVolume(&m_volume);
        break;
    }
    case DOCUMENTS_HIDDEN:
        if (m_documents)
            m_documents->SetOn(false);
        m_frame->SetOn(false);
        if (m_volumeAdded)
            g_collisionWorld.RemoveVolume(&m_volume);
        break;
    case DOCUMENTS_STOPPED:
        if (m_volumeAdded)
            g_collisionWorld.RemoveVolume(&m_volume);
        m_volumeAdded = false;
        if (m_documents)
            m_documents->Release();
        m_documents = nullptr;
        return;
    default:
        return;
    }
    m_volumeAdded = false;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_DocumentPlaceCreator::Create()               // 0x0060dae0
{
    return new C_DocumentPlace();                       // _aligned_malloc(0x150, 16) in the original
}

void C_DocumentPlaceCreator::Destroy(C_Actor *actor)    // 0x0060db50
{
    delete actor;
}

static C_DocumentPlaceCreator g_documentPlaceCreator;   // 0x008aca34

// Registers the creator (static initialiser 0x0060dac0, thunk 0x0060dab0).
static const bool g_documentPlaceCreatorRegistered = [] {
    g_actorCreators[ACTOR_DOCUMENT_PLACE] = &g_documentPlaceCreator;
    return true;
}();
