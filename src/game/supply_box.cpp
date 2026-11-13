// C_SupplyBox: see supply_box.h.
#include "game/supply_box.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage, SendActorMessage
#include "game/collision.h"
#include "game/game.h"
#include "game/pending.h"                               // g_gameRole, g_texts, GetGEffects
#include "game/vertex_anim.h"
#include "game/resource_cache.h"                        // g_modelCache, g_animationCache
#include "game/sound_module.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }
bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

I3D_scene *Scene() { return GetGame()->GetScene(); }

constexpr uint16_t CHUNK_STATE = 0x202;
constexpr uint8_t PROPERTY_VERSION = 1;
constexpr int PROPERTY_LIST_SIZE = 10;                  // items of each list in the properties

constexpr int AMMO_PARTS = 14;
constexpr int HEALTH = 6000;
constexpr int DEFAULT_TEXT = 0x1BFA;
constexpr int DEFAULT_MAX_CARRIED = 5;
constexpr int DEFAULT_CAPACITY = 20;
constexpr int ENABLE_DROP_TIME = 3000;                  // ms from SetEnabled(true) to the drop
constexpr int PLANE_LEAD_TIME = 5001;                   // ms before the drop: the plane flies over
constexpr float PLANE_DISTANCE = 150.0f;                // m behind the drop point
constexpr float PLANE_HEIGHT = 20.0f;                   // m over it
constexpr float PLANE_SPEED = 0.03f;                    // m a ms
constexpr float DROP_HEIGHT = 85.0f;                    // m
constexpr float DROP_UNDER_TOP = 0.1f;                  // m under the scene's top
constexpr float DUMMY_OVER_CRATE = 3.0f;                // m: the cratedummy over the crate
constexpr float FALL_SPEED = 0.005f;                    // m a ms
constexpr float FALL_ANIMATION_RATE = 1.5f;             // "#cratefly" plays this much faster
constexpr float FALL_GAP = 0.01f;                       // m kept over the ground
constexpr double BOX_RADIUS_SCALE = 1.2;                // (a double in the original)
constexpr int BLEND_TIME = 350;                         // ms
constexpr int FADE_TIME = 1000;                         // ms
constexpr int DEFAULT_STATE_TIME = 1000;                // ms when an animation is missing
constexpr int LANDED_TIME = 2000;                       // ms before the parachute fades
constexpr int STATE_ANIM_TICK = 10000;                  // ms: a ready box's open animation done
constexpr uint32_t FIND_PART_MASK = 0x10001;            // (ENUMF_WILDMASK | visuals)
constexpr uint32_t FIND_CRATE_MASK = 0x10020;           // (ENUMF_WILDMASK | dummies)
constexpr uint32_t FALL_EXCLUDE_TYPES = 0x10002;
constexpr uint32_t FALL_EXCLUDE_FLAGS = 0x30000;
constexpr uint32_t CRUSHED_TYPES = 0x101;               // humans and vehicles under the box
constexpr uint32_t DEBUG_RED = 0xFF0000;
constexpr uint32_t DEBUG_GREEN = 0xFF00;

// The animations (+0x22C; SetAnimation's index).
enum : int {
    ANIM_FLY = 0,                                       // the cratedummy's
    ANIM_OPEN = 1,
    ANIM_CLOSED = 2,
    ANIM_DROP = 3,
    ANIM_DESTROYED = 4,                                 // (none: no file)
    ANIM_RESET = 5,
};
const char *const ANIM_FILES[6] = {"#cratefly", "#crateopen", "#crateclosed", "#cratedrop", nullptr, nullptr};

// The sounds (chapter 1).
enum : int {
    SOUND_GROUP_PLANE = 8,
    SOUND_GROUP_LANDED = 9,
    SOUND_GROUP_OPEN = 10,
    SOUND_GROUP_CLOSED = 11,
    SOUND_GROUP_DESTROYED = 12,
};

// Messages: 1 bit, 1 destroyed, 0 the state.
enum : uint32_t {
    MSG_STATE = 0,
    MSG_DESTROYED = 1,
};

// S_vector::Normalize as the inline copies compile here: the square the caller summed; a NaN
// component counts as not 0 and not negative.
void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-08f)
        return;
    if (!(square < 1e-08f)) {
        const float scale = 1.0f / std::sqrt(square);
        v = S_vector(v.x * scale, v.y * scale, v.z * scale);
    } else if (v.x != 0.0f) {
        v.x = v.x < 0.0f ? -1.0f : 1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z < 0.0f ? -1.0f : 1.0f;
    } else {
        v.y = v.y < 0.0f ? -1.0f : 1.0f;
    }
}

// S_vector::Magnitude inline: 0 for a square below 1e-8 (its bits), 1 near 1.
float LengthOf(float square)
{
    uint32_t bits;
    std::memcpy(&bits, &square, sizeof bits);
    if (static_cast<int32_t>(bits) < 0x322bcc77)
        return 0.0f;
    if (!(std::fabs(square - 1.0f) < 1e-08f))
        return std::sqrt(square);
    return 1.0f;
}

// The distance between two points, the squares summed z, y, x.
float DistanceZYX(const S_vector4 &a, const S_vector4 &b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return LengthOf((dz * dz + dy * dy) + dx * dx);
}

// The frame's normalized world z (the square summed y, z, x).
S_vector WorldDir(const I3D_frame *frame)
{
    S_vector dir = frame->GetMatrix()(2);
    NormalizeBy(dir, (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
    return dir;
}

S_vector4 WorldPos4(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

S_vector Vec(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

I3D_model *Model(I3D_frame *frame) { return static_cast<I3D_model *>(frame); }

// The world matrix without the frame's (local) scale and its inverse.
void BoxMatrices(I3D_frame *frame, S_matrix &matrix, S_matrix &inverse)
{
    const S_vector scale = frame->GetScale();
    matrix = frame->GetMatrix();
    const float sx = 1.0f / scale.x;
    const float sy = 1.0f / scale.y;
    const float sz = 1.0f / scale.z;
    for (int j = 0; j < 3; ++j) {
        matrix.m[0][j] = sx * matrix.m[0][j];
        matrix.m[1][j] = sy * matrix.m[1][j];
        matrix.m[2][j] = sz * matrix.m[2][j];
    }
    inverse.Inverse(matrix);
}

}  // namespace

C_SupplyBoxCreator g_supplyBoxCreator;                  // 0x008ac9ec

C_SupplyBox::C_SupplyBox() : C_Actor(ACTOR_SUPPLY_BOX)  // 0x005f97d0
{
}

void C_SupplyBox::UseCharge()
{
    if (m_state != SUPPLY_READY || !(m_flags & SUPPLY_SHOWN))
        return;
    if (--m_charges <= 0)
        SetState(SUPPLY_CLOSING);
    UpdateAmmoParts();
}

void C_SupplyBox::Reset()                               // 0x005f98e0
{
    m_health = HEALTH;
    m_charges = m_capacity;
    I3D_frame *walls = m_frame->FindChildFrame("Steny02", FIND_PART_MASK);
    m_walls = walls && walls->GetVisualType() == I3DVISUAL_OBJECT ? walls : nullptr;
    for (int i = 0; i < AMMO_PARTS; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, i < 10 ? "Ammo0%d" : "Ammo%d", i);
        m_ammo[i] = m_frame->FindChildFrame(name, FIND_PART_MASK);
    }
    m_blendTime = 0;
    m_animStage = -1;
    m_anim = ANIM_RESET;
    m_state = SUPPLY_READY;
    m_stateTime = 0;
    m_planeFlying = false;
    m_textId = DEFAULT_TEXT;
}

void C_SupplyBox::InitBox()                             // 0x005f9a60 (DS 0x0061d8e0)
{
    S_VertexAnimation *animation = g_vertexAnimModule.FindAnimation(C_str("ammobox"));
    I3D_frame *morph = m_parachute->FindChildFrame("morph", FIND_PART_MASK);
    // Fix: the original forgets the parachute (null) when it has no morph, and then calls through it.
    if (morph && morph->GetVisualType() != I3DVISUAL_OBJECT)
        morph = nullptr;
    if (m_morphPlayer) {
        g_vertexAnimModule.RemovePlayer(m_morphPlayer, false);
        delete m_morphPlayer;
        m_morphPlayer = nullptr;
    }
    m_morphPlayer = new S_VertexAnimPlayer();
    // (Fix: not without the morph.)
    if (animation && morph) {
        static_cast<I3D_object_visual *>(morph)->DuplicateMesh();
        m_morphPlayer->Init(morph, animation);
        m_morphPlayer->stopped = true;
        m_morphPlayer->Reset();
        g_vertexAnimModule.AddPlayer(m_morphPlayer);
        m_morphPlayer->pingPong = true;
        m_morphPlayer->maxLevel = animation->levelCount - 1;
        m_morphPlayer->frameTime = 130;
        m_morphPlayer->startFrame = 0;
        m_morphPlayer->loop = false;
    }
    // The box: the hierarchy's under the frame's own scale; the sphere from x and z.
    I3D_bbox hierarchy;
    I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, m_frame);
    const S_vector scale = m_frame->GetScale();
    box.max = S_vector(box.max.x * scale.x, box.max.y * scale.y, box.max.z * scale.z);
    box.min = S_vector(box.min.x * scale.x, box.min.y * scale.y, box.min.z * scale.z);
    BoxMatrices(m_frame, m_boxMatrix, m_boxInvMatrix);
    const float hx = (box.max.x - box.min.x) * 0.5f;
    const float hz = (box.max.z - box.min.z) * 0.5f;
    const float radius = static_cast<float>(static_cast<double>(std::sqrt(hz * hz + hx * hx)) * BOX_RADIUS_SCALE);
    m_volume = new S_CollisionVolume();                 // _aligned_malloc(0x60, 16), zeroed
    m_volume->flags = COLLISION_SHAPE_BOX;
    m_volume->actor = this;
    m_volume->type = COLLISION_TYPE_PIKE;
    m_volume->frame = m_frame;
    const S_vector4 pos = WorldPos4(m_frame);
    m_volume->center = Vec(pos);
    m_volume->centerW = pos.w;
    m_volume->matrix = &m_boxMatrix;
    m_volume->invMatrix = &m_boxInvMatrix;
    m_volume->radius = radius;
    m_volume->bbox = box;
    UpdateAmmoParts();
    m_flags = SUPPLY_DISABLED;
    SetVisibility(m_shownFlags | SUPPLY_MOVE_TO_DROP | SUPPLY_AT_ONCE);
    if (m_flags & SUPPLY_SHOWN)
        SetState(SUPPLY_OPENING);
}

void C_SupplyBox::StartDrop()                           // 0x005f9f00
{
    float height = DROP_HEIGHT;
    if (!IsLocalGame() && !(DROP_HEIGHT < g_scanMax[1]))
        height = g_scanMax[1] - DROP_UNDER_TOP;
    m_health = HEALTH;
    m_charges = m_capacity;
    m_frame->SetPos(S_vector(m_dropPos.x, height + m_dropPos.y, m_dropPos.z));
    m_frame->SetRot(m_dropRot);
    m_frame->Update();
    Scene()->UpdateFrameSector(m_frame, m_frame->GetWorldPos());
    m_parachute->SetOn(true);
    m_parachute->SetPos(S_vector(m_dropPos.x, height + m_dropPos.y, m_dropPos.z));
    m_parachute->SetDir(WorldDir(m_frame), 0.0f);
    m_parachute->Update();
    GetGEffects()->FadeInModel(m_parachute, 0, true);
    m_crateDummy->SetOn(true);
    m_crateDummy->SetPos(S_vector(m_dropPos.x, (height + DUMMY_OVER_CRATE) + m_dropPos.y, m_dropPos.z));
    m_crateDummy->Update();
    Scene()->UpdateFrameSector(m_crateDummy, m_crateDummy->GetWorldPos());
    m_parachute->LinkTo(m_flyingCrate, 1);
    m_frame->LinkTo(m_flyingCrate, 1);
    UpdateAmmoParts();
    SetVisibility(SUPPLY_SHOWN);
}

void C_SupplyBox::SetEnabled(bool on)                   // 0x005fa290
{
    if (!on) {
        SetVisibility(SUPPLY_DISABLED | SUPPLY_AT_ONCE);
        return;
    }
    m_stateTime = -1;
    m_state = SUPPLY_CLOSING;
    SetState(SUPPLY_HIDDEN);
    m_stateTime = ENABLE_DROP_TIME;
    if (!IsLocalGame() && IsServer()) {
        C_BitStream *msg = CreateActorMessage(m_netId, true);
        msg->WriteBits(MSG_STATE, 1);
        WriteStateBits(msg);
        SendActorMessage(msg);
    }
}

void C_SupplyBox::OnMessage(C_BitStream *msg)           // 0x005fa3f0
{
    if (msg->ReadBits(1) != MSG_STATE) {
        Damage(-1);
        return;
    }
    S_State state;
    ReadStateBits(msg, state);
    ApplyState(state);
}

void C_SupplyBox::UpdateBoxVolume()                     // 0x005fa530
{
    BoxMatrices(m_frame, m_boxMatrix, m_boxInvMatrix);
    const S_vector4 pos = WorldPos4(m_frame);
    m_volume->center = Vec(pos);
    m_volume->centerW = pos.w;
    m_volume->matrix = &m_boxMatrix;
    m_volume->invMatrix = &m_boxInvMatrix;
    g_collisionWorld.MoveVolume(m_volume);
}

bool C_SupplyBox::SetFrame(I3D_frame *frame)            // 0x005fa6a0 (DS 0x0061e850)
{
    m_dropPos = WorldPos4(frame);
    S_quat rot;
    rot.Make(frame->GetMatrix());
    m_dropRot = rot;
    m_axisItems.clear();
    m_alliedItems.clear();
    m_hasItemLists = 0;
    m_maxCarried = DEFAULT_MAX_CARRIED;
    m_capacity = DEFAULT_CAPACITY;
    m_respawnTime = -1;
    m_started = false;
    m_volume = nullptr;
    const bool set = C_Actor::SetFrame(frame);
    Reset();
    if (!m_walls)
        return false;
    // The feet: up to four of the walls' vertices below half the box's height (from the origin).
    I3D_bbox hierarchy;
    const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, m_frame);
    I3D_mesh_level *level = static_cast<I3D_visual *>(m_walls)->GetVisualMesh()->GetLevel(0);
    const int count = level->NumVertices();
    const float *vertices = static_cast<const float *>(level->GetVertices(0));
    const float limit = (box.max.y - box.min.y) * 0.5f;
    int feet = 0;
    for (int i = 0; i < count; ++i) {
        const float *v = vertices + i * 8;
        if (v[1] < limit && feet < 4)
            m_feet[feet++] = {v[0], v[1], v[2], 0.0f};  // (w: a stale local in the original)
    }
    level->UnlockVertices();
    // Ordered round the box: 0 and 1 the nearer pair, 2 across from 0... (each swap keeps w).
    if (DistanceZYX(m_feet[0], m_feet[2]) < DistanceZYX(m_feet[0], m_feet[1])) {
        std::swap(m_feet[1], m_feet[2]);
    } else if (DistanceZYX(m_feet[0], m_feet[3]) < DistanceZYX(m_feet[0], m_feet[1])) {
        std::swap(m_feet[1], m_feet[3]);
    }
    const float d03 = DistanceZYX(m_feet[0], m_feet[3]);
    const S_vector d02(m_feet[0].x - m_feet[2].x, m_feet[0].y - m_feet[2].y, m_feet[0].z - m_feet[2].z);
    // (S_vector::MagnitudeInline 0x0040e0b0: the squares summed x, y, z.)
    const float m02 = LengthOf((d02.x * d02.x + d02.y * d02.y) + d02.z * d02.z);
    if (m02 > d03)
        std::swap(m_feet[2], m_feet[3]);
    return set;
}

void C_SupplyBox::StartMission(bool server)             // 0x005fac80
{
    if (m_started)
        return;
    m_started = true;
    Reset();
    I3D_frame *parent = m_frame->GetParent();
    Activate(server);
    m_frame->LinkTo(parent, 1);
    m_crateDummy = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    g_modelCache.OpenInto(m_crateDummy, "cratedummy");
    m_flyingCrate = m_crateDummy->FindChildFrame("letibedna", FIND_CRATE_MASK);
    m_plane = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    m_parachute = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    g_modelCache.OpenInto(m_parachute, "parachute");
    for (int i = 0; i < 6; ++i) {
        if (ANIM_FILES[i])
            g_animationCache.Get(ANIM_FILES[i], &m_anims[i]);
    }
    InitBox();
    m_name = g_texts.Get(m_textId);
}

void C_SupplyBox::StopMission(bool server)              // 0x005fb220
{
    if (!m_started)
        return;
    Deactivate(server);
    m_started = false;
    m_morphPlayer->stopped = true;
    m_morphPlayer->Reset();
    m_crateDummy->SetOn(false);
    m_parachute->LinkTo(nullptr, 1);
    m_frame->LinkTo(nullptr, 1);
    SetAnimation(ANIM_RESET);
    TickAnimation(1);
    g_vertexAnimModule.RemovePlayer(m_morphPlayer, false);
    delete m_morphPlayer;
    m_morphPlayer = nullptr;
    m_crateDummy->Release();
    m_crateDummy = nullptr;
    m_flyingCrate = nullptr;
    m_plane->Release();
    m_plane = nullptr;
    if (m_parachute) {
        m_parachute->Release();
        m_parachute = nullptr;
    }
    for (I3D_animation_set *&anim : m_anims) {
        if (anim) {
            anim->Release();
            anim = nullptr;
        }
    }
    SetVisibility(SUPPLY_SHOWN | SUPPLY_MOVE_TO_DROP | SUPPLY_AT_ONCE);
    if (m_volume) {
        g_collisionWorld.RemoveVolume(m_volume);
        delete m_volume;
        m_volume = nullptr;
    }
}

void C_SupplyBox::Update(int frameTime)                 // 0x005fb380
{
    if (m_flags & SUPPLY_DISABLED)
        return;
    TickState(frameTime);
    if (m_state == SUPPLY_FALLING)
        m_crateDummy->Tick(static_cast<int>(static_cast<float>(static_cast<uint32_t>(frameTime)) * FALL_ANIMATION_RATE));
    TickAnimation(frameTime);
    // The plane flies on along the crate's z.
    if (m_planeFlying) {
        const float t = static_cast<float>(static_cast<uint32_t>(frameTime));
        S_vector dir = m_frame->GetMatrix()(2);
        NormalizeBy(dir, (dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z);
        const S_vector move(dir.x * t * PLANE_SPEED, dir.y * t * PLANE_SPEED, dir.z * t * PLANE_SPEED);
        const S_vector &at = m_plane->GetWorldPos();
        m_plane->SetPos(S_vector(move.x + at.x, move.y + at.y, move.z + at.z));
        m_plane->Update();
    }
    if (m_state != SUPPLY_FALLING)
        return;
    // Fix: without the walls (their placement) the original reads through null; nothing falls.
    if (!m_walls)
        return;
    // The fall: the box swept down; on a touch, rays down from the feet, their pairs' middles and
    // a sphere from the middle; it stops short of what they meet (static ground lands it; humans
    // and vehicles under it are hit for -1 on the server).
    const float fall = static_cast<float>(static_cast<uint32_t>(frameTime)) * FALL_SPEED;
    float allowed = fall;
    const S_matrix from = m_walls->GetMatrix();
    S_matrix to = from;
    to.m[3][1] = from.m[3][1] - fall;
    g_collisionWorld.SetIgnoredVolume(m_volume, 0);
    const S_vector4 move = {0.0f, -fall, 0.0f, 0.0f};
    const S_vector up(0.0f, 1.0f, 0.0f);
    bool touching = false;
    bool landed = false;
    if (g_collisionWorld.TestBoxSweep(m_volume->bbox, from, to, move, up, up, FALL_EXCLUDE_TYPES, FALL_EXCLUDE_FLAGS)) {
        S_vector points[7];
        for (int i = 0; i < 4; ++i)
            points[i] = Vec(m_feet[i]) * from;
        const S_vector m01((points[1].x + points[0].x) * 0.5f, (points[1].y + points[0].y) * 0.5f,
                           (points[1].z + points[0].z) * 0.5f);
        const S_vector m23((points[3].x + points[2].x) * 0.5f, (points[3].y + points[2].y) * 0.5f,
                           (points[3].z + points[2].z) * 0.5f);
        points[4] = m01;
        points[5] = m23;
        points[6] = S_vector((m23.x + m01.x) * 0.5f, (m23.y + m01.y) * 0.5f, (m23.z + m01.z) * 0.5f);
        S_vector hits[7];
        S_vector normals[7];
        for (int i = 0; i < 7; ++i) {
            const S_vector &p = points[i];
            S_CollisionVolume *hit;
            if (i < 6) {
                const float end[4] = {p.x, -fall + p.y, p.z, 0.0f};
                DrawDebugLine(&p.x, end, DEBUG_RED, 0);
                hit = CollisionRayTest(p, S_vector(0.0f, -fall, 0.0f), &hits[i], &normals[i], FALL_EXCLUDE_TYPES,
                                       FALL_EXCLUDE_FLAGS);
            } else {
                const float end[4] = {p.x, -fall + p.y, p.z, 0.0f};
                DrawDebugLine(&p.x, end, DEBUG_GREEN, 0);
                const float dx = points[0].x - points[1].x;
                const float dy = points[0].y - points[1].y;
                const float dz = points[0].z - points[1].z;
                const float r = LengthOf((dz * dz + dy * dy) + dx * dx) * 0.5f;
                hit = CollisionSphereTest(S_vector(p.x, r + p.y, p.z), S_vector(0.0f, -fall - r, 0.0f), r, &hits[i],
                                          &normals[i], FALL_EXCLUDE_TYPES, FALL_EXCLUDE_FLAGS);
                if (hit)
                    hits[i].y = hits[i].y - r;
            }
            if (!hit)
                continue;
            const float d = p.y - hits[i].y;
            if (d <= allowed + FALL_GAP)
                allowed = d - FALL_GAP;
            else
                touching = true;
            if (!(hit->flags & COLLISION_FLAG_DYNAMIC)) {
                if (!touching)
                    landed = true;
            } else {
                if ((hit->type & CRUSHED_TYPES) && hit->actor && IsServer()) {
                    const S_vector zero(0.0f, 0.0f, 0.0f);
                    hit->actor->Hit(HIT_FALL, -1.0f, zero, zero, nullptr, 0);
                }
                touching = true;
            }
        }
    }
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    if (allowed > 0.0f) {
        const S_vector &at = m_crateDummy->GetWorldPos();
        const S_vector pos(at.x, at.y - allowed, at.z);
        m_crateDummy->SetPos(pos);
        m_crateDummy->Update();
        Scene()->UpdateFrameSector(m_crateDummy, pos);
        UpdateBoxVolume();
    }
    if (landed)
        SetState(SUPPLY_LANDED);
}

void C_SupplyBox::Tick(int timeDelta)                   // 0x005fbd20 (DS 0x00620490)
{
#ifndef HD2_DEDICATED   // the server has only the server's side
    if (!IsClient())
#endif
        Update(timeDelta);
}

void C_SupplyBox::Damage(int amount)                    // 0x005fbd40
{
    if (m_flags & SUPPLY_DISABLED)
        return;
    if (amount == -1)
        m_health = 0;
    else
        m_health -= amount;
    if (m_health <= 0)
        SetState(SUPPLY_DESTROYED);
}

void C_SupplyBox::SetVisibility(uint32_t flags)         // 0x005fbd90
{
    const int fade = flags & SUPPLY_AT_ONCE ? 0 : FADE_TIME;
    if (flags & SUPPLY_SHOWN) {
        if (m_flags & (SUPPLY_DISABLED | SUPPLY_FLAG_4))
            g_collisionWorld.AddVolume(m_volume);
        GetGEffects()->FadeInModel(m_frame, fade, true);
        if (flags & SUPPLY_MOVE_TO_DROP) {
            m_frame->SetPos(Vec(m_dropPos));
            m_frame->Update();
            Scene()->UpdateFrameSector(m_frame, m_frame->GetWorldPos());
        }
        UpdateBoxVolume();
        m_flags = flags & 7;
        return;
    }
    m_parachute->SetOn(false);
    GetGEffects()->FadeOutHierarchy(m_frame, fade, true);
    if (m_flags & SUPPLY_SHOWN)
        g_collisionWorld.RemoveVolume(m_volume);
    m_flags = flags & 7;
}

void C_SupplyBox::CopyFrom(const C_Actor *source)       // 0x005fbec0
{
    C_Actor::CopyFrom(source);
    const auto *box = static_cast<const C_SupplyBox *>(source);
    m_hasItemLists = box->m_hasItemLists;
    m_maxCarried = box->m_maxCarried;
    m_capacity = box->m_capacity;
    m_respawnTime = box->m_respawnTime;
    m_axisItems = box->m_axisItems;
    m_alliedItems = box->m_alliedItems;
}

bool C_SupplyBox::WriteProperties(C_chunk &chunk)       // 0x005fc050
{
    chunk.Write(PROPERTY_VERSION);
    chunk.Write(m_hasItemLists);
    chunk.Write(m_shownFlags);
    chunk.Write(m_maxCarried);
    chunk.Write(m_capacity);
    chunk.Write(m_respawnTime);
    const int none = -1;
    for (int i = 0; i < PROPERTY_LIST_SIZE; ++i)
        chunk.Write(i < static_cast<int>(m_axisItems.size()) ? m_axisItems[i] : none);
    for (int i = 0; i < PROPERTY_LIST_SIZE; ++i)
        chunk.Write(i < static_cast<int>(m_alliedItems.size()) ? m_alliedItems[i] : none);
    chunk.Write(m_textId);
    return true;
}

bool C_SupplyBox::ReadProperties(C_chunk &chunk)        // 0x005fc200
{
    uint8_t version;
    chunk.Read(version);
    chunk.Read(m_hasItemLists);
    chunk.Read(m_shownFlags);
    chunk.Read(m_maxCarried);
    chunk.Read(m_capacity);
    chunk.Read(m_respawnTime);
    for (int i = 0; i < PROPERTY_LIST_SIZE; ++i) {
        int item = -1;                                  // (a short chunk: the last one read again)
        chunk.Read(item);
        if (item != -1)
            m_axisItems.push_back(item);
    }
    for (int i = 0; i < PROPERTY_LIST_SIZE; ++i) {
        int item = -1;
        chunk.Read(item);
        if (item != -1)
            m_alliedItems.push_back(item);
    }
    chunk.Read(m_textId);
    return true;
}

void C_SupplyBox::RestartMission(bool server)           // 0x005fc480
{
    m_crateDummy->SetOn(false);
    m_parachute->LinkTo(nullptr, 1);
    m_frame->LinkTo(nullptr, 1);
    if (m_volume) {
        g_collisionWorld.RemoveVolume(m_volume);
        delete m_volume;
        m_volume = nullptr;
    }
    Reactivate(server);
    SetAnimation(ANIM_RESET);
    TickAnimation(1);
    m_started = false;
    Reset();
    InitBox();
    SetVisibility(m_shownFlags | SUPPLY_MOVE_TO_DROP | SUPPLY_AT_ONCE);
    m_started = true;
}

bool C_SupplyBox::SaveGame(C_chunk &chunk)              // 0x005fc530
{
    C_Actor::SaveGame(chunk);
    S_State state;
    GetState(state);
    chunk.Begin(CHUNK_STATE);
    chunk.Write(&state, sizeof state);
    chunk.Ascend();
    return true;
}

bool C_SupplyBox::LoadGame(C_chunk &chunk)              // 0x005fc640
{
    C_Actor::LoadGame(chunk);
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == CHUNK_STATE) {
            // (A short chunk: the original applies stack leftovers; zeros here.)
            S_State state;
            chunk.Read(&state, sizeof state);
            ApplyState(state);
        }
        chunk.Ascend();
    }
    UpdateAmmoParts();
    return true;
}

void C_SupplyBox::SetAnimation(int anim)                // 0x005fc860
{
    I3D_model *model = Model(m_frame);
    if (anim == 0)
        return;
    if (anim == ANIM_RESET) {
        if (m_animStage == -1)
            m_animStage = 0;
        model->SetAnimation(m_anims[ANIM_CLOSED], m_animStage, 1.0f, 1);
        model->Tick(0);
        model->SetAnimation(m_anims[ANIM_OPEN], m_animStage, 1.0f, 1);
        model->Tick(0);
        model->StopAnimation(m_animStage);
        m_anim = ANIM_RESET;
        return;
    }
    if (m_anim != ANIM_RESET && m_animStage != -1) {
        // On the other stage, blended in.
        m_animStage = m_animStage == 0 ? 1 : 0;
        model->SetAnimation(m_anims[anim], m_animStage, 0.0f, 1);
        m_anim = anim;
        m_blendTime = BLEND_TIME;
        return;
    }
    if (m_animStage == -1)
        m_animStage = 0;
    model->SetAnimation(m_anims[anim], m_animStage, 1.0f, 1);
    m_blendTime = 0;
    m_anim = anim;
}

void C_SupplyBox::TickAnimation(int time)               // 0x005fc9a0
{
    I3D_model *model = Model(m_frame);
    if (m_blendTime > 0) {
        const int stage = m_animStage;
        m_blendTime -= time;
        // Quirk kept: whole steps (integer division), so the weights jump.
        float left = static_cast<float>(m_blendTime / BLEND_TIME);
        if (left < 0.0f)
            left = 0.0f;
        model->SetAnimWeight(stage, 1.0f - left);
        model->SetAnimWeight(stage == 0, left);
    }
    if (m_animStage != -1 && !model->IsAnimActive(m_animStage))
        m_anim = ANIM_RESET;
    model->Tick(time);
}

void C_SupplyBox::TickState(int time)                   // 0x005fca60
{
    if (m_stateTime <= 0)
        return;
    m_stateTime -= time;
    if (m_stateTime <= 0) {
        switch (m_state) {
        case SUPPLY_CLOSING:
        case SUPPLY_DESTROYED:
            SetState(SUPPLY_HIDDEN);
            break;
        case SUPPLY_HIDDEN:
            SetState(SUPPLY_FALLING);
            break;
        case SUPPLY_LANDED:
            SetState(SUPPLY_PARACHUTE_FADING);
            break;
        case SUPPLY_PARACHUTE_FADING:
            SetState(SUPPLY_OPENING);
            break;
        case SUPPLY_OPENING:
            SetState(SUPPLY_READY);
            break;
        default:
            break;
        }
        return;
    }
    if (m_stateTime < PLANE_LEAD_TIME && m_state == SUPPLY_HIDDEN && !m_planeFlying) {
        // The plane, heard 150 m behind and 20 m over the drop point (along the crate's z).
        m_planeFlying = true;
        const S_vector dir = WorldDir(m_frame);
        m_plane->SetPos(S_vector(m_dropPos.x - dir.x * PLANE_DISTANCE,
                                 (m_dropPos.y + PLANE_HEIGHT) - dir.y * PLANE_DISTANCE,
                                 m_dropPos.z - dir.z * PLANE_DISTANCE));
        m_plane->Update();
        S_SoundRequest request;
        request.pos = S_vector(0.0f, 0.0f, 0.0f);
        request.flags = SOUND_SILENT_TO_AI | SOUND_ANY_RANGE;
        request.attachTo = m_plane;
        request.source = nullptr;
        request.chapter = 1;
        request.group = SOUND_GROUP_PLANE;
        GetSoundModule()->Play(request);
    }
}

void C_SupplyBox::PlaySound(int group)
{
    S_SoundRequest request;
    const S_vector4 pos = WorldPos4(m_frame);
    request.pos = Vec(pos);
    request.posW = pos.w;
    request.flags = SOUND_SILENT_TO_AI;
    request.attachTo = nullptr;
    request.source = nullptr;
    request.chapter = 1;
    request.group = group;
    GetSoundModule()->Play(request);
}

void C_SupplyBox::PutAwayDrop()
{
    m_crateDummy->SetOn(false);
    m_parachute->LinkTo(nullptr, 1);
    m_frame->LinkTo(nullptr, 1);
}

void C_SupplyBox::SetState(int state)                   // 0x005fcd80 (DS 0x00621940)
{
    switch (state) {
    case SUPPLY_READY:
        m_stateTime = -1;
        break;
    case SUPPLY_CLOSING: {
        SetAnimation(ANIM_CLOSED);
        PlaySound(SOUND_GROUP_CLOSED);
        I3D_animation_set *anim = m_anims[ANIM_CLOSED];
        m_stateTime = anim ? anim->GetEndTime() : DEFAULT_STATE_TIME;
        break;
    }
    case SUPPLY_HIDDEN:
        SetVisibility(SUPPLY_FLAG_4);
        m_morphPlayer->stopped = true;
        m_morphPlayer->Reset();
        m_stateTime = m_respawnTime > 0 ? m_respawnTime * 1000 : -1;
        break;
    case SUPPLY_FALLING:
        // (Only from hidden; otherwise just the state.)
        if (m_state != SUPPLY_HIDDEN)
            break;
        SetAnimation(ANIM_RESET);
        StartDrop();
        m_crateDummy->SetAnimation(m_anims[ANIM_FLY], 0, 1.0f, 2);
        m_stateTime = -1;
        break;
    case SUPPLY_LANDED:
        SetAnimation(ANIM_DROP);
        m_stateTime = LANDED_TIME;
        PutAwayDrop();
        Scene()->UpdateFrameSector(m_parachute, m_parachute->GetWorldPos());
        Scene()->UpdateFrameSector(m_frame, m_frame->GetWorldPos());
        m_frame->SetRot(m_dropRot);
        m_morphPlayer->stopped = false;
        m_planeFlying = false;
        PlaySound(SOUND_GROUP_LANDED);
        break;
    case SUPPLY_PARACHUTE_FADING:
        m_stateTime = FADE_TIME;
        GetGEffects()->FadeOutHierarchy(m_parachute, FADE_TIME, true);
        break;
    case SUPPLY_OPENING: {
        SetAnimation(ANIM_OPEN);
        m_morphPlayer->stopped = true;
        I3D_animation_set *anim = m_anims[ANIM_OPEN];
        m_stateTime = anim ? anim->GetEndTime() : DEFAULT_STATE_TIME;
        if (m_stateTime < DEFAULT_STATE_TIME)
            m_stateTime = DEFAULT_STATE_TIME;
        PlaySound(SOUND_GROUP_OPEN);
        break;
    }
    case SUPPLY_DESTROYED: {
        PutAwayDrop();
        SetAnimation(ANIM_DESTROYED);
        SetVisibility(SUPPLY_FLAG_4 | SUPPLY_AT_ONCE);
        I3D_animation_set *anim = m_anims[ANIM_DESTROYED];
        m_stateTime = anim ? anim->GetEndTime() : DEFAULT_STATE_TIME;
        PlaySound(SOUND_GROUP_DESTROYED);
        break;
    }
    default:
        break;
    }
    m_state = state;
}

void C_SupplyBox::GetState(S_State &state)              // 0x005fd180
{
    state = S_State();
    state.flags = m_flags;
    state.charges = m_charges;
    state.health = m_health;
    state.planeFlying = m_planeFlying;
    state.blendTime = m_blendTime;
    state.animStage = m_animStage;
    state.anim = m_anim;
    state.state = m_state;
    state.stateTime = m_stateTime;
    if (m_state == SUPPLY_FALLING) {
        state.pos = WorldPos4(m_crateDummy);
        state.crate = WorldPos4(m_frame);
    } else {
        state.crate = {0.0f, 0.0f, 0.0f, 0.0f};         // (w: a stale local in the original)
        state.pos = WorldPos4(m_frame);
    }
    state.animTime = Model(m_frame)->GetAnimTime(m_animStage);
}

void C_SupplyBox::ApplyState(const S_State &state)      // 0x005fd2a0
{
    m_morphPlayer->stopped = true;
    m_morphPlayer->Reset();
    m_crateDummy->SetOn(false);
    m_parachute->LinkTo(nullptr, 1);
    UpdateAmmoParts();
    m_charges = state.charges;
    m_health = state.health;
    m_animStage = state.animStage;
    m_anim = state.anim;
    m_state = state.state;
    m_stateTime = state.stateTime;
    m_planeFlying = state.planeFlying;
    SetAnimation(ANIM_RESET);
    switch (m_state) {
    case SUPPLY_READY:
        SetVisibility(state.flags | SUPPLY_AT_ONCE);
        SetAnimation(ANIM_OPEN);
        TickAnimation(STATE_ANIM_TICK);
        break;
    case SUPPLY_CLOSING:
        SetVisibility(state.flags | SUPPLY_AT_ONCE);
        SetAnimation(ANIM_CLOSED);
        TickAnimation(state.animTime);
        break;
    case SUPPLY_HIDDEN:
    case SUPPLY_PARACHUTE_FADING:
    case SUPPLY_DESTROYED:
        SetVisibility(state.flags | SUPPLY_AT_ONCE);
        break;
    case SUPPLY_FALLING:
        StartDrop();
        m_crateDummy->SetAnimation(m_anims[ANIM_FLY], 0, 1.0f, 2);
        break;
    case SUPPLY_LANDED:
        SetVisibility(state.flags | SUPPLY_AT_ONCE);
        SetAnimation(ANIM_DROP);
        TickAnimation(state.animTime);
        break;
    case SUPPLY_OPENING:
        SetVisibility(state.flags | SUPPLY_AT_ONCE);
        SetAnimation(ANIM_OPEN);
        TickAnimation(state.animTime);
        break;
    default:
        break;
    }
    if (m_state != SUPPLY_FALLING) {
        m_frame->SetPos(Vec(state.pos));
        m_frame->Update();
        Scene()->UpdateFrameSector(m_frame, m_frame->GetWorldPos());
        if (!(m_flags & (SUPPLY_DISABLED | SUPPLY_FLAG_4)))
            UpdateBoxVolume();
        return;
    }
    m_crateDummy->SetPos(Vec(state.pos));
    m_crateDummy->Update();
    Scene()->UpdateFrameSector(m_crateDummy, m_crateDummy->GetWorldPos());
    // (Quirk kept: the crate's world position as its position under the swinging crate frame.)
    m_frame->SetPos(Vec(state.crate));
    m_frame->Update();
    UpdateBoxVolume();
}

void C_SupplyBox::WriteStateBits(C_BitStream *msg)
{
    S_State state;
    GetState(state);
    msg->WriteBits(state.flags, 6);
    msg->WriteBits(static_cast<uint32_t>(state.charges), 32);
    msg->WriteBits(static_cast<uint32_t>(state.health), 32);
    msg->WriteBits(state.planeFlying ? 1 : 0, 1);
    msg->WriteBits(static_cast<uint32_t>(state.blendTime), 32);
    msg->WriteBits(static_cast<uint32_t>(state.animStage), 32);
    msg->WriteBits(static_cast<uint32_t>(state.anim), 4);
    msg->WriteBits(static_cast<uint32_t>(state.state), 4);
    msg->WriteBits(static_cast<uint32_t>(state.stateTime), 32);
    auto *net = static_cast<C_NetMessage *>(msg);
    net->WriteQuantized(&state.pos.x, net->GetSender(-1), 7);
    net->WriteQuantized(&state.crate.x, net->GetSender(-1), 7);
    msg->WriteBits(static_cast<uint32_t>(state.animTime), 32);
}

void C_SupplyBox::ReadStateBits(C_BitStream *msg, S_State &state)
{
    state.flags = msg->ReadBits(6);
    state.charges = static_cast<int>(msg->ReadBits(32));
    state.health = static_cast<int>(msg->ReadBits(32));
    state.planeFlying = msg->ReadBits(1) != 0;
    state.blendTime = static_cast<int>(msg->ReadBits(32));
    state.animStage = static_cast<int>(msg->ReadBits(32));
    state.anim = static_cast<int>(msg->ReadBits(4));
    state.state = static_cast<int>(msg->ReadBits(4));
    state.stateTime = static_cast<int>(msg->ReadBits(32));
    auto *net = static_cast<C_NetMessage *>(msg);
    float values[4] = {};
    net->ReadQuantized(values, net->GetSender(-1), 7);
    state.pos = {values[0], values[1], values[2], values[3]};
    net->ReadQuantized(values, net->GetSender(-1), 7);
    state.crate = {values[0], values[1], values[2], values[3]};
    state.animTime = static_cast<int>(msg->ReadBits(32));
}

void C_SupplyBox::WriteState(C_BitStream *msg)          // 0x005fd560
{
    WriteStateBits(msg);
}

void C_SupplyBox::ReadState(C_BitStream *msg)           // 0x005fd640
{
    S_State state;
    ReadStateBits(msg, state);
    ApplyState(state);
}

void C_SupplyBox::UpdateAmmoParts()                     // 0x005fd760
{
    // Empty parts from the top; the last one stays while anything is left.
    const float empty = 14.0f - (static_cast<float>(m_charges) / static_cast<float>(m_capacity)) * 14.0f;
    for (int i = 0; i < AMMO_PARTS; ++i) {
        if (!m_ammo[i])
            continue;
        const bool on = !(static_cast<float>(i) < empty) || (i >= 13 && !(empty >= 14.0f) && !(empty <= 13.0f));
        m_ammo[i]->SetOn(on);
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_SupplyBoxCreator::Create()                   // 0x005f9730
{
    return new C_SupplyBox();                           // _aligned_malloc(0x260, 16) in the original
}

void C_SupplyBoxCreator::Destroy(C_Actor *actor)        // 0x005f97b0
{
    delete actor;
}

// Registers the creator (static initialiser 0x005f9710, thunk 0x005f9700).
static const bool g_supplyBoxCreatorRegistered = [] {
    g_actorCreators[ACTOR_SUPPLY_BOX] = &g_supplyBoxCreator;
    return true;
}();
