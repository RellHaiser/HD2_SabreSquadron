// C_Actor, the base of all actors (original source 0x00610070..0x006115b0; the
// trivial virtual defaults were emitted with other files, see actor.h).
#include "game/actor.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/rigid_body.h"                         // ForgetActor

namespace {

// Saved-game chunk of an actor and its sub-chunks.
enum : uint16_t {
    CHUNK_ACTOR = 0x3039,
    CHUNK_ACTOR_PARENT = 0,         // name of the frame's parent ("" = none)
    CHUNK_ACTOR_POSITION = 1,
    CHUNK_ACTOR_DIRECTION = 2,
    CHUNK_ACTOR_FRAME_POS = 3,      // frame world position (restored as the local one)
    CHUNK_ACTOR_FRAME_ON = 4,
    CHUNK_ACTOR_FRAME_ROT = 5,      // file order s, x, y, z
};

constexpr uint32_t FIND_ANY_FRAME = 0xFFFF;
constexpr int MAX_PARENT_NAME = 999;

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

I3D_scene *Scene() { return GetGame()->GetScene(); }

// Visual types whose geometry has a local bound (mesh visuals).
bool HasMeshBound(I3D_frame *frame)
{
    switch (frame->GetVisualType()) {
    case I3DVISUAL_OBJECT:
    case I3DVISUAL_LMAP:
    case I3DVISUAL_SINGLEMESH:
    case I3DVISUAL_SINGLEMORPH:
        return true;
    default:
        return false;
    }
}

// Adds the geometry of `frame` and its children, placed by `parentMatrix`, to `box`.
void AddToHierarchyBox(I3D_bbox &box, I3D_frame *frame, const S_matrix &parentMatrix)   // 0x006112b0
{
    const S_matrix matrix = frame->GetLocalMatrix() * parentMatrix;
    if (HasMeshBound(frame)) {
        const I3D_bbox &local = static_cast<I3D_visual *>(frame)->GetLocalBound().bbox;
        if (local.IsValid()) {
            for (int i = 0; i < 8; ++i) {
                const S_vector corner((i & 1) ? local.max.x : local.min.x, (i & 2) ? local.max.y : local.min.y,
                                      (i & 4) ? local.max.z : local.min.z);
                box.Expand(corner * matrix);
            }
        }
    }
    for (I3D_frame *child = frame->GetFirstChild(); child; child = child->GetNextSibling())
        AddToHierarchyBox(box, child, matrix);
}

// Box around the geometry of `frame` and all its children, in `frame`'s space.
// (The original returns a pointer to a static box.)
I3D_bbox ComputeHierarchyBox(I3D_frame *frame)                                      // 0x006111f0
{
    I3D_bbox box = HasMeshBound(frame) ? static_cast<I3D_visual *>(frame)->GetLocalBound().bbox : I3D_bbox::Empty();
    for (I3D_frame *child = frame->GetFirstChild(); child; child = child->GetNextSibling())
        AddToHierarchyBox(box, child, IdentityMatrix());
    return box;
}

} // namespace

// The table itself (0x0089b530; the original's pointer at 0x00839460 is pre-set to it). Fix: the
// port left the pointer null, so the creators' static registrations wrote through null at startup;
// constant-initialised now, so it is set before any registration runs.
static C_ActorCreator *s_actorCreators[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = s_actorCreators;

C_Actor::C_Actor(E_ActorType type) : m_type(type), m_name("Missing actor name!!")
{
}

C_Actor::~C_Actor()
{
    Done();
    m_carriedWeight = 0.0f;
    if (m_frame)
        m_frame->Release();
    ForgetActor(this);
}

void C_Actor::CopyFrom(const C_Actor * /*source*/)
{
}

bool C_Actor::SetFrame(I3D_frame *frame)
{
    if (!frame)
        return false;
    m_frame = frame;
    frame->AddRef();
    Init();
    return true;
}

void C_Actor::Init()
{
    m_containers = nullptr;
    m_singleContainer = true;
    if (m_type == ACTOR_CAR || m_type == ACTOR_STATIC_WEAPON || m_type == ACTOR_SUBMARINE ||
        m_type == ACTOR_JUNKERS88) {
        m_containers = new C_ItemContainer *[1];
        m_containers[0] = new C_ItemStore(2);
        m_containers[0]->SetMaxWeight(1000.0f);
    }
}

void C_Actor::Done()
{
    if (!m_containers)
        return;
    // (Only the first store is freed: vehicles have one, humans free theirs themselves.)
    delete m_containers[0];
    delete[] m_containers;
    m_containers = nullptr;
}

void C_Actor::Activate(bool server)
{
    const S_matrix &world = m_frame->GetMatrix();
    const S_vector dir = world(2).Normalized();
    m_direction = {dir.x, dir.y, dir.z, world.m[2][3]};
    m_position = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};

    if (!m_temporary) {
        if (!m_initialParent) {
            m_initialParent = m_frame->GetParent();
            if (m_initialParent)
                m_initialParent->AddRef();
            const S_matrix &local = m_frame->GetLocalMatrix();
            m_initialPos = {local.m[3][0], local.m[3][1], local.m[3][2], local.m[3][3]};
            m_initialRot = m_frame->GetRot();
        }
        if (server || !IsServer())
            g_actorsModule.SetActorActive(this, true);
    } else if (server || !IsServer()) {
        g_temporaryActors.SetActorActive(this, true);
    }
    Scene()->UpdateFrameSector(m_frame, m_frame->GetWorldPos());
}

void C_Actor::Deactivate(bool server)
{
    m_carriedWeight = 0.0f;
    if (m_temporary) {
        if (server || !IsServer())
            g_temporaryActors.SetActorActive(this, false);
        return;
    }
    if (server || !IsServer())
        g_actorsModule.SetActorActive(this, false);
    m_frame->SetOn(true);
    if (!m_initialParent)
        return;

    // Back under the parent it had; if that frame left the scene meanwhile, under
    // the scene frame of the same name.
    const C_str parentName(m_initialParent->GetName());
    m_frame->LinkTo(m_initialParent, 0);
    m_frame->Update();
    m_initialParent->Release();
    if (!m_frame->GetParent()) {
        if (I3D_frame *parent = Scene()->FindFrame(parentName, FIND_ANY_FRAME)) {
            m_frame->LinkTo(parent, 0);
            m_frame->Update();
        }
    }
    m_initialParent = nullptr;
    m_frame->SetPosInline(S_vector(m_initialPos.x, m_initialPos.y, m_initialPos.z));
    m_frame->SetRot(m_initialRot);
    m_frame->Update();
}

void C_Actor::Reactivate(bool /*server*/)
{
    if (m_temporary)
        return;
    const float saved = m_carriedWeight;
    if (IsClient())
        Deactivate(false);
    if (IsServer()) {
        Deactivate(true);
        Activate(true);
    }
    if (IsClient())
        Activate(false);
    m_carriedWeight = saved;
}

bool C_Actor::SaveGame(C_chunk &chunk)
{
    if (!chunk.Begin(CHUNK_ACTOR))
        return false;
    SaveActorItems(this, chunk);

    const I3D_frame *parent = m_frame->GetParent();
    const char *parentName = parent ? parent->GetName() : "";
    chunk.WriteChunk(CHUNK_ACTOR_PARENT, parentName, static_cast<int>(std::strlen(parentName)) + 1);
    chunk.WriteChunk(CHUNK_ACTOR_POSITION, &m_position, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_ACTOR_DIRECTION, &m_direction, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_ACTOR_FRAME_POS, &m_frame->GetWorldPos(), sizeof(S_vector));
    chunk.WriteChunk(CHUNK_ACTOR_FRAME_ON, static_cast<int>(m_frame->IsOn()));
    float rot[4];
    m_frame->GetRot().ToFile(rot);
    chunk.WriteChunk(CHUNK_ACTOR_FRAME_ROT, rot, sizeof(rot));
    chunk.Ascend();
    return true;
}

bool C_Actor::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id != CHUNK_ACTOR) {
        chunk.Ascend();
        return false;
    }
    LoadActorItems(this, chunk);

    bool on = true;
    uint16_t sub;
    while (chunk.RemainingSize() > 0 && chunk.Descend(sub)) {
        switch (sub) {
        case CHUNK_ACTOR_PARENT: {
            char name[MAX_PARENT_NAME + 1] = {};
            chunk.Read(name, std::min(chunk.RemainingSize(), MAX_PARENT_NAME));
            chunk.Ascend();
            I3D_frame *parent = name[0] ? Scene()->FindFrame(name, FIND_ANY_FRAME) : nullptr;
            m_frame->LinkTo(parent, 0);
            break;
        }
        case CHUNK_ACTOR_POSITION:
        case CHUNK_ACTOR_DIRECTION: {
            // (The original also copies a w component from uninitialised stack memory.)
            S_vector v(0.0f, 0.0f, 0.0f);
            chunk.Read(v);
            chunk.Ascend();
            S_vector4 &dst = sub == CHUNK_ACTOR_POSITION ? m_position : m_direction;
            dst.x = v.x;
            dst.y = v.y;
            dst.z = v.z;
            break;
        }
        case CHUNK_ACTOR_FRAME_POS: {
            S_vector pos(0.0f, 0.0f, 0.0f);
            chunk.Read(pos);
            chunk.Ascend();
            m_frame->SetPos(pos);
            break;
        }
        case CHUNK_ACTOR_FRAME_ON: {
            int value = 0;
            chunk.Read(value);
            chunk.Ascend();
            on = value != 0;
            break;
        }
        case CHUNK_ACTOR_FRAME_ROT: {
            float rot[4];
            chunk.Read(rot, sizeof(rot));
            chunk.Ascend();
            m_frame->SetRot(S_quat::FromFile(rot));
            break;
        }
        default:
            chunk.Ascend();
            break;
        }
    }
    m_frame->SetOn(on);
    m_frame->Update();
    chunk.Ascend();
    return true;
}

int C_Actor::GetSeatAmmoCount(int seat)
{
    if (!HasSeatWeapon(seat))
        return 0;
    const int ammoId = GetSeatWeaponDef(seat)->GetAmmoItemId();
    C_ItemContainer *store = m_containers[0];
    const std::vector<C_Item *> *slots = store->GetSlots();
    for (int i = store->GetSlotCount(); i-- > 0;) {
        if (!slots[i].empty() && slots[i].front()->GetTypeId() == ammoId)
            return static_cast<int>(slots[i].size());
    }
    return 0;
}

void C_Actor::GetBoxCorners(S_vector4 corners[8])
{
    m_frame->Update();
    const I3D_bbox box = ComputeHierarchyBox(m_frame);
    int n = 0;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            const float x = i == 0 ? box.max.x : box.min.x;
            const float y = j == 0 ? box.max.y : box.min.y;
            corners[n++] = {x, y, box.min.z, 0.0f};
            corners[n++] = {x, y, box.max.z, 0.0f};
        }
    }
    const S_matrix world = m_frame->GetMatrix();
    for (int k = 0; k < 8; ++k) {
        const S_vector v = S_vector(corners[k].x, corners[k].y, corners[k].z) * world;
        corners[k].x = v.x;
        corners[k].y = v.y;
        corners[k].z = v.z;
    }
}

int GetFrameMaterialId(const I3D_frame *frame)
{
    if (!frame)
        return -1;
    const char *found = std::strstr(frame->GetUserProps(), "matid");
    return found ? std::atoi(found + std::strlen("matid")) : -1;
}
