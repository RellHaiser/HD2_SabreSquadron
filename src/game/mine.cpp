// Mines (C_Mine; original 0x005fde00..0x005ff150).
#include "game/mine.h"

#include "common/chunk.h"
#include "game/actors_module.h"                         // g_temporaryActors, g_actorsModule, temp messages
#include "game/car.h"
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/pending.h"                               // g_gameRole, g_texts
#include "game/shots.h"                                 // g_shotManager
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

constexpr int ITEM_MINE_HUMANS = 0x46;                  // set off by humans only
constexpr int ITEM_MINE_CARS = 0x47;                    // set off by cars only
constexpr float SINK_HUMANS = 0.07f;                    // m into the ground along the frame's z axis
constexpr float SINK_CARS = 0.08f;
constexpr float RADIUS_HUMANS = 0.15f;                  // the sphere's
constexpr float RADIUS_CARS = 0.4f;
constexpr float SPHERE_HEIGHT = 0.2f;                   // m above the frame
constexpr float ARM_DELAY = 2000.0f;                    // ms
constexpr int ARMED = -1;                               // m_armTime once armed

// The messages to the clients.
enum : uint32_t {
    MSG_EXPLODED = 0,
    MSG_REMOVED = 1,
};
// (int)(log(2) / log(2) + 1), worked out at run time by the original.
constexpr int MSG_BITS = 2;

// The saved game's chunks.
enum : uint16_t {
    CHUNK_STATE = 1,                                    // place, direction, countdown, flags
    CHUNK_OWNER = 2,                                    // the layer's actor id
};

// The mine item's component (its effect, damage, sound and range); null without one.
C_MineComp *MineComponent(int itemId)
{
    const C_ItemDef *def = GetItemDef(itemId);
    return def ? static_cast<C_MineComp *>(def->GetComponent()) : nullptr;
}

}  // namespace

bool C_Mine::SetFrame(I3D_frame *frame)                 // 0x005fdf90
{
    frame->SetName("Actor_mine");
    return C_Actor::SetFrame(frame);
}

void C_Mine::StartMission(bool server)                  // 0x005fdfb0
{
    m_volumeAdded = false;
    Activate(server);
}

void C_Mine::StopMission(bool server)                   // 0x005fdfd0
{
    if (m_volumeAdded)
        g_collisionWorld.RemoveVolume(&m_volume);
    m_volumeAdded = false;
    Deactivate(server);
}

void C_Mine::Setup(C_Human *owner, int itemId, int materialId)   // 0x005fe010
{
    m_materialId = materialId;
    m_armTime = 0;
    m_armed = false;
    m_itemId = itemId;
    m_owner = owner;
    if (itemId == ITEM_MINE_HUMANS || itemId == ITEM_MINE_CARS) {
        const float depth = itemId == ITEM_MINE_HUMANS ? SINK_HUMANS : SINK_CARS;
        const S_vector axis = m_frame->GetMatrix()(2).NormalizedGame();
        const S_vector sink(axis.x * depth, axis.y * depth, axis.z * depth);
        const S_vector &pos = m_frame->GetWorldPos();
        m_frame->SetPos(S_vector(pos.x - sink.x, pos.y - sink.y, pos.z - sink.z));
    }
    m_frame->SetOn(true);
    m_frame->Update();
    GetGame()->GetScene()->UpdateFrameSector(m_frame, m_frame->GetWorldPos());
    m_frame->Update();
    // Fix: an item type without a definition names it by text -1 (the original reads through
    // null).
    const C_ItemDef *def = GetItemDef(m_itemId);
    m_name = g_texts.Get(def ? def->GetTextId() : -1);
}

bool C_Mine::SaveGame(C_chunk &chunk)                   // 0x005fe440
{
    chunk.Begin(CHUNK_STATE);
    const S_vector pos = m_frame->GetWorldPos();
    chunk.Write(pos);
    chunk.Write(m_frame->GetMatrix()(2).NormalizedGame());
    chunk.Write(m_armTime);
    chunk.Write(static_cast<int>(m_armed));
    chunk.Write(static_cast<int>(m_volumeAdded));
    chunk.Ascend();
    if (m_owner) {
        chunk.Begin(CHUNK_OWNER);
        chunk.Write(m_owner->GetNetId());
        chunk.Ascend();
    }
    return true;
}

bool C_Mine::LoadGame(C_chunk &chunk)                   // 0x005fe800
{
    // Quirk kept: a chunk of another id is entered and not left; the flags and the layer's id share
    // one value (a short chunk keeps the last one read).
    int value = 0;
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == CHUNK_STATE) {
            S_vector pos(0.0f, 0.0f, 0.0f);             // (the original goes on with stack values)
            S_vector dir(0.0f, 0.0f, 0.0f);
            chunk.Read(pos);
            chunk.Read(dir);
            m_frame->SetPos(pos);
            m_frame->SetDir(dir, 0.0f);
            chunk.Read(m_armTime);
            chunk.Read(value);
            m_armed = value != 0;
            chunk.Read(value);
            m_volumeAdded = value != 0;
            chunk.Ascend();
        } else if (id == CHUNK_OWNER) {
            chunk.Read(value);
            m_owner = static_cast<C_Human *>(g_actorsModule.GetHuman(value));
            chunk.Ascend();
        }
    }
    if (m_armed)
        Arm();
    return false;
}

void C_Mine::OnMessage(C_BitStream *msg)                // 0x005feb50
{
    const uint32_t kind = msg->ReadBits(MSG_BITS);
    if (kind == MSG_EXPLODED)
        Detonate();
    else if (kind == MSG_REMOVED)
        Remove();
}

void C_Mine::Arm()                                      // 0x005febb0
{
    const S_matrix &world = m_frame->GetMatrix();
    // (The position's w is a stale local in the original; the actor's is kept.)
    m_position.x = world.m[3][0];
    m_position.y = world.m[3][1] + SPHERE_HEIGHT;
    m_position.z = world.m[3][2];
    const S_vector dir = world(2).NormalizedGame();
    m_direction = {dir.x, dir.y, dir.z, world.m[2][3]};
    m_volume = S_CollisionVolume();
    m_volume.flags = COLLISION_SHAPE_SPHERE;
    m_volume.center = S_vector(m_position.x, m_position.y, m_position.z);
    m_volume.centerW = m_position.w;
    m_volume.radius = m_itemId == ITEM_MINE_CARS ? RADIUS_CARS : RADIUS_HUMANS;
    m_volume.frame = m_frame;
    m_volume.type = COLLISION_TYPE_MINE;
    m_volume.actor = this;
    g_collisionWorld.AddVolume(&m_volume);
    m_armTime = ARMED;
    m_armed = true;
    m_volumeAdded = true;
}

void C_Mine::Advance(int time)                          // 0x005fee10
{
    m_armTime += time;
    if (static_cast<float>(m_armTime) >= ARM_DELAY) {
        m_armTime = ARMED;
        Arm();
    }
}

void C_Mine::CountDown(int time)                        // 0x005fee50
{
    if (m_armTime >= 0)
        Advance(time);
}

void C_Mine::Tick(int timeDelta)                        // 0x005fee70
{
    if (!IsClient())
        CountDown(timeDelta);
}

void C_Mine::Update(int frameTime)                      // 0x005fee90
{
    if (IsClient())
        CountDown(frameTime);
}

void C_Mine::Remove()                                   // 0x005feec0
{
    m_armed = false;
    g_temporaryActors.DestroyActor(this);
}

void C_Mine::Defuse()                                   // 0x005feee0
{
    m_armed = false;
    C_BitStream *msg = CreateTempActorMessage(m_netId, true);
    msg->WriteBits(MSG_REMOVED, MSG_BITS);
    SendTempActorMessage(msg);
    // (The clients, a listen server's included, remove it on the message.)
    if (!IsClient())
        g_temporaryActors.DestroyActor(this);
}

void C_Mine::Detonate()                                 // 0x005fef50
{
    m_armed = false;
    // Fix: without the item's mine component there is nothing to play (the original reads
    // through null).
    if (C_MineComp *mine = MineComponent(m_itemId)) {
        const S_vector pos(m_position.x, m_position.y, m_position.z);
        const S_vector dir(m_direction.x, m_direction.y, m_direction.z);
        g_shotManager.PlayExplosion(pos, dir, mine->GetEffect(), -1, mine->GetShotSound());
    }
    g_temporaryActors.DestroyActor(this);
}

void C_Mine::OnSteppedOn(C_Actor *actor, int)           // 0x005fefb0
{
    if (!m_armed || actor == m_owner)
        return;
    if (actor->GetType() == ACTOR_HUMAN && m_itemId == ITEM_MINE_CARS)
        return;
    if (actor->GetType() == ACTOR_CAR && m_itemId == ITEM_MINE_HUMANS)
        return;
    m_armed = false;
    if (m_volumeAdded)
        g_collisionWorld.RemoveVolume(&m_volume);
    m_volumeAdded = false;
    C_Actor *culprit = m_owner;
    if (!culprit)
        culprit = actor->GetType() == ACTOR_HUMAN ? actor : static_cast<C_Car *>(actor)->GetDriver();
    C_MineComp *mine = MineComponent(m_itemId);
    C_BitStream *msg = CreateTempActorMessage(m_netId, true);
    msg->WriteBits(MSG_EXPLODED, MSG_BITS);
    SendTempActorMessage(msg);
    // Quirk kept: 0.2 m above the sphere, itself 0.2 m above the frame.
    const S_vector pos(m_position.x, m_position.y + SPHERE_HEIGHT, m_position.z);
    // Fix: without the item's mine component there is no explosion (the original reads through
    // null).
    if (mine)
        g_shotManager.Explode(culprit, pos, mine->GetExplosionRadius(), mine->GetDamage(), mine->GetShotSound());
    if (!IsClient())
        g_temporaryActors.DestroyActor(this);
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_MineCreator::Create()                        // 0x005fde30
{
    return new C_Mine();                                // _aligned_malloc(0x120, 16) in the original
}

void C_MineCreator::Destroy(C_Actor *actor)             // 0x005fdea0
{
    delete actor;
}

static C_MineCreator g_mineCreator;                     // 0x008ac9f4

// Registers the creator (static initialiser 0x005fde10, thunk 0x005fde00).
static const bool g_mineCreatorRegistered = [] {
    g_actorCreators[ACTOR_MINE] = &g_mineCreator;
    return true;
}();
