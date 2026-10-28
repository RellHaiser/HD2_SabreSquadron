// Items lying in the world (C_ItemActor; original 0x005b6710..0x005b7fd9).
#include "game/item_actor.h"

#include <cmath>
#include <cstring>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"                               // g_gameRole, GetGEffects, g_texts, FreeFrameData
#include "game/profile.h"                               // g_gameProfile
#include "game/sound_module.h"
#include "game/sound_table.h"                           // SOUND_CHAPTER_FALLING_HIT
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

// Which sides of the game this machine runs (g_gameRole); single player is both.
bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// Width of a message kind: log2(4) + 1 (computed at run time in the original).
constexpr int MSG_KIND_BITS = 3;

// Saved-game chunks (written straight into the actor's chunk; the base's are not).
constexpr uint16_t CHUNK_ITEM_STATE = 1;                // life, fading, world position and rotation
constexpr uint16_t CHUNK_ITEM_BODY = 3;                 // the rigid body

constexpr float LIFE_TIME = 30000.0f;                   // ms
constexpr float LIFE_TIME_SPREAD = 10000.0f;            // ms
constexpr float FADE_TIME = 500.0f;                     // ms: the fade-out starts this long before the end
constexpr float FRICTION = 0.8f;
constexpr int PUSH_DELAY = 300;                         // ms: hits push the item from then on
constexpr float BULLET_PUSH = 0.05f;                    // a bullet's impulse per point of damage
constexpr int SOUND_PAUSE = 2000;                       // ms after a hit sound
constexpr int SOUND_PAUSE_LEFT = 50;                    // ms: a hit sounds again with less left
// A human's collision volumes (0x100 a living one's, 0x10000 a dead one's): items pass them.
constexpr uint32_t HUMAN_VOLUME_TYPES = 0x10100;
constexpr uint32_t FIND_ANY_FRAME = 0xFFFF;
constexpr uint32_t LINK_TO_SECTOR = 5;                  // LinkTo flags (I3DLINK_KEEP_WORLD | 4)

// A float sent as its 32 bits.
uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

float BitsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

S_vector4 Point4(const S_vector &v) { return {v.x, v.y, v.z, 0.0f}; }

// The multiplayer pick-ups (and the items dropped like them): they neither go away nor move.
bool IsPickUp(const C_Item *item)
{
    const int behaviour = item->GetDef()->GetBehaviour();
    return behaviour == ITEM_BEHAVIOUR_RESPAWNING || behaviour == ITEM_BEHAVIOUR_MISSION_EXPLOSIVE;
}

// S_vector::Normalized with the original's sum ((y² + z²) + x²); a NaN square scales.
S_vector NormalizedYZX(S_vector v)
{
    const float sq = (v.y * v.y + v.z * v.z) + v.x * v.x;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return v;
    if (!(sq < 1e-8f))
        return v * (1.0f / std::sqrt(sq));
    if (v.x != 0.0f)
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    else if (v.z != 0.0f)
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    else
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
    return v;
}

}  // namespace

C_ItemActor::C_ItemActor() : C_Actor(ACTOR_ITEM) {}     // 0x005b67d0

C_ItemActor::~C_ItemActor() = default;                  // 0x005b6880

bool C_ItemActor::SetFrame(I3D_frame *frame)            // 0x005b68d0
{
    m_item = nullptr;
    // (The model is the item's own: picked up while fading out, it would come back faded.)
    frame->SetHierarchyAlpha(1.0f);
    return C_Actor::SetFrame(frame);
}

void C_ItemActor::StartMission(bool server)             // 0x005b6900
{
    Activate(server);
    m_fading = false;
}

void C_ItemActor::StopMission(bool server)              // 0x005b6920
{
    m_body.Stop();
    if (m_frame)
        m_frame->Release();
    m_frame = nullptr;
    if (m_item) {
        g_itemsModule.DestroyItem(m_item);
        m_item = nullptr;
    }
    Deactivate(server);
}

void C_ItemActor::ReleaseItem()                         // 0x005b6980
{
    FreeFrameData(m_frame);
    m_item = nullptr;
    m_frame->Release();
    m_frame = nullptr;
}

void C_ItemActor::StartBody(const S_vector &velocity)
{
    S_RigidBodyParams params;
    params.friction = FRICTION;
    params.SetVelocity(velocity);
    params.weight = m_item->GetWeight();
    params.unknown41 = m_item->GetSize() < 2;
    params.callback = OnContact;
    params.context = this;
    m_body.SetFrame(m_item->GetFrame(0));
    m_body.Setup(params);
    m_body.Start();
    m_body.SetOwner(this);
}

void C_ItemActor::SetupItem(C_Item *item, const S_vector &velocity)   // 0x005b69b0
{
    m_item = item;
    if (!item)
        return;
    StartBody(velocity);
    m_soundTimer = 0;
    m_respawnTimer = -1;
    // Only a multiplayer server's dropped items go away (not the pick-ups, nor in co-op).
    if (!IsServer() || IsLocalGame() || g_gameProfile.IsCooperativeGame() || IsPickUp(item))
        m_lifeTime = -1;
    else
        m_lifeTime = static_cast<int>(RandFloat() * LIFE_TIME_SPREAD + LIFE_TIME);
    m_age = 0;
    m_name = g_texts.Get(m_item->GetDef()->GetTextId());
}

bool C_ItemActor::SaveGame(C_chunk &chunk)              // 0x005b6b80
{
    chunk.Begin(CHUNK_ITEM_STATE);
    chunk.Write(m_lifeTime);
    chunk.Write(static_cast<int>(m_fading));            // (the byte stored zero-extended to 4)
    chunk.Write(&m_frame->GetWorldPos(), 0xc);
    S_quat rot;
    rot.Make(m_frame->GetMatrix());
    float file[4];
    rot.ToFile(file);
    chunk.Write(file, 0x10);
    chunk.Ascend();

    chunk.Begin(CHUNK_ITEM_BODY);
    m_body.SaveGame(chunk);
    chunk.Ascend();
    return true;
}

bool C_ItemActor::LoadGame(C_chunk &chunk)              // 0x005b6e20
{
    // Quirk kept: a chunk of another id is entered and not left (its contents are read as the
    // item's).
    // (A header that cannot be read ends the loop; the original goes on with the previous id.)
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == CHUNK_ITEM_STATE) {
            chunk.Read(&m_lifeTime, 4);
            int fading = 0;
            chunk.Read(&fading, 4);
            m_fading = fading != 0;
            S_vector pos(0.0f, 0.0f, 0.0f);
            chunk.Read(&pos, 0xc);
            float file[4] = {};
            chunk.Read(file, 0x10);
            // Placed in world space, then put into the sector there.
            m_frame->LinkTo(nullptr, 0);
            m_frame->SetPosInline(pos);
            m_frame->SetRot(S_quat::FromFile(file));
            m_frame->Update();
            m_frame->LinkTo(GetGame()->GetScene()->GetSector(pos), LINK_TO_SECTOR);
            chunk.Ascend();
        } else if (id == CHUNK_ITEM_BODY) {
            m_body.LoadGame(chunk);
            chunk.Ascend();
        }
    }
    m_respawnTimer = -1;
    return true;
}

void C_ItemActor::OnMessage(C_BitStream *stream)        // 0x005b7110
{
    auto *msg = static_cast<C_NetMessage *>(stream);
    switch (msg->ReadBits(MSG_KIND_BITS)) {
    case MSG_REMOVE:
        GetGEffects()->ClearModelHighlight(m_frame);
        m_frame->SetOn(false);
        m_frame->Update();
        g_temporaryActors.DestroyActor(this);
        break;
    case MSG_PLACEMENT: {
        S_vector pos;
        msg->ReadPosition(pos);
        S_vector axis;
        float angle;
        msg->ReadAxisAngle(axis, angle, -1);
        OnPlacement(pos, axis, angle);
        break;
    }
    case MSG_PUSH: {
        S_vector point;
        msg->ReadPosition(point);
        S_vector dir;
        msg->ReadDirection3D(dir, -1);
        const float length = BitsFloat(msg->ReadBits(32));
        OnPush(dir * length, point);
        break;
    }
    case MSG_TELEPORT: {
        char name[0x80];
        msg->ReadString(name, 0x7f);
        if (I3D_frame *target = GetGame()->GetScene()->FindFrame(name, FIND_ANY_FRAME))
            OnTeleport(target);
        break;
    }
    }
}

void C_ItemActor::Tick(int timeDelta)                   // 0x005b7320
{
    if (!m_item)
        return;
    m_age += timeDelta;
    // Quirk kept: a dedicated server steps the body here and in Update.
    if (!IsClient())
        m_body.Tick(static_cast<uint32_t>(timeDelta));
    if (m_respawnTimer != -1) {
        m_respawnTimer -= timeDelta;
        if (m_respawnTimer < 1) {
            Respawn();
            m_respawnTimer = -1;
        }
    }
    if (m_lifeTime == -1)
        return;
    m_lifeTime -= timeDelta;
    if (m_lifeTime < 1) {
        GetGEffects()->ClearModelHighlight(m_frame);
        m_frame->SetOn(false);
        m_frame->Update();
        C_BitStream *msg = CreateTempActorMessage(m_netId, true);
        msg->WriteBits(MSG_REMOVE, MSG_KIND_BITS);
        SendTempActorMessage(msg);
        m_lifeTime = -1;
        if (!IsClient())
            g_temporaryActors.DestroyActor(this);
        return;
    }
    if (static_cast<float>(m_lifeTime) <= FADE_TIME && !m_fading) {
        m_fading = true;
        GetGEffects()->FadeOutModel(m_frame, m_lifeTime, false);
    }
}

void C_ItemActor::Update(int frameTime)                 // 0x005b7480
{
    m_body.Tick(static_cast<uint32_t>(frameTime));
    if (m_soundTimer != 0) {
        m_soundTimer -= frameTime;
        if (m_soundTimer < 0)
            m_soundTimer = 0;
    }
}

void C_ItemActor::Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *,
                      int)                              // 0x005b74d0
{
    if (m_age < PUSH_DELAY || IsPickUp(m_item))
        return;
    // A bullet's direction is scaled by its damage; other hits' are the impulse.
    S_vector impulse = dir;
    if (type == HIT_BULLET)
        impulse *= amount * BULLET_PUSH;
    Push(impulse, pos);
}

void C_ItemActor::PlayHitSound()                        // 0x005b7570
{
    if (!m_item || m_soundTimer >= SOUND_PAUSE_LEFT)
        return;
    const int group = m_item->GetDef()->GetCategory();
    if (group <= 0)
        return;
    S_SoundRequest sound;
    sound.group = group;
    sound.chapter = SOUND_CHAPTER_FALLING_HIT;
    sound.source = nullptr;
    const S_matrix &world = m_frame->GetMatrix();
    sound.pos = world(3);
    sound.posW = world.m[3][3];
    sound.attachTo = nullptr;
    sound.flags = SOUND_SILENT_TO_AI;
    GetSoundModule()->Play(sound);
    m_soundTimer = SOUND_PAUSE;
}

int __fastcall C_ItemActor::OnContact(int event, int, void *context, const S_RigidBodyContact *contact)   // 0x005b7630
{
    auto *actor = static_cast<C_ItemActor *>(context);
    if (event == RIGID_BODY_EVENT_CONTACT) {
        const S_CollisionVolume *volume = contact->volume;
        if ((volume->flags & COLLISION_FLAG_DYNAMIC) && (volume->type & HUMAN_VOLUME_TYPES))
            return RIGID_BODY_REFUSE;
        if (actor && IsClient())
            actor->PlayHitSound();
        return 2;                                       // (counts: any value but 1)
    }
    if (event == RIGID_BODY_EVENT_REST && !IsLocalGame() && IsServer())
        actor->SendPlacement();
    return 0;
}

void C_ItemActor::AttachTo(I3D_frame *target)           // 0x005b76a0
{
    if (!m_frame)
        return;
    m_frame->LinkTo(target, I3DLINK_KEEP_WORLD);
    m_frame->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    m_frame->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    m_frame->Update();
    m_frame->LinkTo(nullptr, I3DLINK_KEEP_WORLD);
    GetGame()->GetScene()->UpdateFrameSector(m_frame, m_frame->GetWorldPos());
    m_frame->SetOn(true);
    m_frame->Update();
    const S_matrix &world = m_frame->GetMatrix();
    m_position = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    const S_vector dir = NormalizedYZX(world(2));
    m_direction = {dir.x, dir.y, dir.z, world.m[2][3]};
    ResetBody();
}

void C_ItemActor::TeleportTo(I3D_frame *target)         // 0x005b7920
{
    AttachTo(target);
    if (IsLocalGame())
        return;
    C_BitStream *msg = CreateTempActorMessage(m_netId, true);
    msg->WriteBits(MSG_TELEPORT, MSG_KIND_BITS);
    msg->WriteString(target->GetName());
    SendTempActorMessage(msg);
}

void C_ItemActor::OnTeleport(I3D_frame *target)         // 0x005b79a0
{
    if (!IsServer())
        AttachTo(target);
}

void C_ItemActor::Push(const S_vector &impulse, const S_vector &point)   // 0x005b79c0
{
    if (!m_item)
        return;
    m_body.AddImpulse(impulse, Point4(point));
    if (IsLocalGame())
        return;
    // The impulse as its direction and length.
    const S_vector dir = NormalizedYZX(impulse);
    auto *msg = static_cast<C_NetMessage *>(CreateTempActorMessage(m_netId, true));
    msg->WriteBits(MSG_PUSH, MSG_KIND_BITS);
    msg->WritePosition(point);
    msg->WriteDirection3D(dir, -1);
    msg->WriteBits(FloatBits(impulse.Magnitude()), 32);
    SendTempActorMessage(msg);
}

void C_ItemActor::OnPush(const S_vector &impulse, const S_vector &point)   // 0x005b7c30
{
    if (!IsServer())
        m_body.AddImpulse(impulse, Point4(point));
}

void C_ItemActor::SendPlacement()                       // 0x005b7c50
{
    if (!m_item)
        return;
    auto *msg = static_cast<C_NetMessage *>(CreateTempActorMessage(m_netId, true));
    msg->WriteBits(MSG_PLACEMENT, MSG_KIND_BITS);
    S_quat rot;
    rot.Make(m_frame->GetMatrix());
    // (LS3D's Inverse writes four floats: the axis, then the rotation's s.)
    S_vector4 axis = {};
    float angle;
    rot.Inverse(reinterpret_cast<S_vector &>(axis), angle);
    msg->WritePosition(m_frame->GetWorldPos());
    msg->WriteAxisAngle(reinterpret_cast<const S_vector &>(axis), angle, -1);
    SendTempActorMessage(msg);
}

void C_ItemActor::OnPlacement(const S_vector &pos, const S_vector &axis, float angle)   // 0x005b7d50
{
    if (IsServer())
        return;
    S_quat rot;
    rot.Make(axis, angle);
    m_frame->SetPosRot(pos, rot);
    m_frame->Update();
    ResetBody();
}

void C_ItemActor::ResetBody()                           // 0x005b7da0
{
    if (!m_item)
        return;
    m_body.Stop();
    StartBody(S_vector(0.0f, 0.0f, 0.0f));
}

void C_ItemActor::Respawn()                             // 0x005b7ec0
{
    if (IsLocalGame())
        return;
    const S_ItemPlacement *placement = nullptr;
    for (const S_ItemPlacement &entry : g_temporaryActors.GetRespawnItems()) {
        if (entry.item == m_item) {
            placement = &entry;
            break;
        }
    }
    if (!placement)
        return;
    auto *msg = static_cast<C_NetMessage *>(CreateTempActorMessage(m_netId, true));
    msg->WriteBits(MSG_PLACEMENT, MSG_KIND_BITS);
    const S_vector pos(placement->pos.x, placement->pos.y, placement->pos.z);
    m_frame->SetPosRot(pos, placement->rot);
    m_frame->Update();
    ResetBody();
    S_vector4 axis = {};                                // (see SendPlacement)
    float angle;
    placement->rot.Inverse(reinterpret_cast<S_vector &>(axis), angle);
    msg->WritePosition(pos);
    msg->WriteAxisAngle(reinterpret_cast<const S_vector &>(axis), angle, -1);
    SendTempActorMessage(msg);
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_ItemActorCreator::Create()                   // 0x005b6740
{
    return new C_ItemActor();                           // _aligned_malloc(0x6c0, 16) in the original
}

void C_ItemActorCreator::Destroy(C_Actor *actor)        // 0x005b67b0
{
    delete actor;
}

static C_ItemActorCreator g_itemActorCreator;           // 0x008ac59c

// Registers the creator (static initialiser 0x005b6720, thunk 0x005b6710).
static const bool g_itemActorCreatorRegistered = [] {
    g_actorCreators[ACTOR_ITEM] = &g_itemActorCreator;
    return true;
}();
