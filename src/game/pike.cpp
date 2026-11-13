// Pikes (C_Pike; original 0x005b88c0..0x005ba100).
#include "game/pike.h"

#include <cmath>

#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage
#include "game/pending.h"                               // g_gameRole
#include "game/sound_table.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

constexpr uint16_t CHUNK_STATE = 1234;                  // the saved game's

// The messages and the state's bit: (int)(log(2) / log(2) + 1), worked out at run time by the
// original.
enum : uint32_t {
    MSG_RAISING = 0,
    MSG_LOWERING = 1,
};
constexpr int MSG_BITS = 2;

constexpr int BLOCKED_WAIT = 200;                       // ms
constexpr float TIME_SCALE = 0.001f;                    // ms to s
constexpr float CORNER_INSET = 0.01f;                   // m the rays keep inside the box

// The box's edges: corner indices (0x0083dff8; corner i has max x for i < 4, max y for i % 4 < 2,
// max z for odd i).
constexpr uint16_t EDGES[12][2] = {{2, 3}, {3, 7}, {7, 6}, {6, 2}, {0, 2}, {1, 3},
                                   {5, 7}, {4, 6}, {0, 1}, {1, 5}, {5, 4}, {4, 0}};

// The rays' exclusions: what cars drive through.
constexpr uint32_t RAY_EXCLUDE = COLLISION_TYPE_LIGHT_OBJECT;

void PlaySoundRequest(const S_SoundRequest &request)
{
    GetSoundModule()->Play(request);
}

}  // namespace

C_Pike::C_Pike() : C_Actor(ACTOR_PIKE)                  // 0x005b8d00
{
}

void C_Pike::RestartMission(bool)                       // 0x005b8990 (DS 0x005cb9c0)
{
    StopMission(true);
    StartMission(true);
}

bool C_Pike::SaveGame(C_chunk &chunk)                   // 0x005b89b0
{
    chunk.Begin(CHUNK_STATE);
    chunk.Write(m_state);
    chunk.Write(m_blockedTime);
    chunk.Write(m_angle);
    chunk.Ascend();
    return true;
}

bool C_Pike::LoadGame(C_chunk &chunk)                   // 0x005b8b00
{
    // (The chunk's id is not checked.) Quirks kept: a chunk too short for the state gives its own
    // id and the low half of its size as the state; the chunk is read and left even when it was
    // not entered.
    uint16_t id;
    int state = 0;                                      // (the original goes on with a stack value)
    if (chunk.Descend(id)) {
        const uint32_t size = static_cast<uint32_t>(chunk.RemainingSize() + 6);
        state = static_cast<int>(id | (size << 16));
    }
    m_state = state;
    chunk.Read(m_state);
    chunk.Read(m_blockedTime);
    chunk.Read(m_angle);
    chunk.Ascend();
    Update(0);
    return true;
}

bool C_Pike::SetFrame(I3D_frame *frame)                 // 0x005b8e00 (DS 0x005cbf80)
{
    if (!frame)
        return false;
    return C_Actor::SetFrame(frame);
}

void C_Pike::CopyFrom(const C_Actor *source)            // 0x005b8e20
{
    C_Actor::CopyFrom(source);
    const auto *other = static_cast<const C_Pike *>(source);
    m_state = other->m_state;
    m_maxAngle = other->m_maxAngle;
    m_speed = other->m_speed;
    m_axis = other->m_axis;
    m_lowSound = other->m_lowSound;
    m_highSound = other->m_highSound;
    m_moveSound = other->m_moveSound;
}

void C_Pike::SetState(int state)                        // 0x005b8ea0
{
    if (const S_FrameData *data = m_frame->GetData()) {
        if (data->script)
            data->script->OnUse(state);
    }
    if (state == PIKE_RAISING) {
        SendState(MSG_RAISING);
        m_state = PIKE_RAISING;
    } else if (state == PIKE_LOWERING) {
        SendState(MSG_LOWERING);
        m_state = PIKE_LOWERING;
    }
}

bool C_Pike::Turn(int)                                  // 0x005b8f00 (DS 0x005cc0d0)
{
    // The angle about the axis, from the rotation the frame had when activated.
    const S_quat previous = m_frame->GetRot();
    S_quat turn;
    turn.Make(S_vector(m_axis.x, m_axis.y, m_axis.z), m_angle);
    const S_quat &start = m_initialRot;
    const S_vector cross(turn.v.y * start.v.z - turn.v.z * start.v.y, turn.v.z * start.v.x - start.v.z * turn.v.x,
                         turn.v.x * start.v.y - turn.v.y * start.v.x);
    S_quat rot;
    rot.v.x = (start.v.x * turn.s + turn.v.x * start.s) + cross.x;
    rot.v.y = (start.v.y * turn.s + turn.v.y * start.s) + cross.y;
    rot.v.z = (start.v.z * turn.s + turn.v.z * start.s) + cross.z;
    rot.s = turn.s * start.s - ((turn.v.z * start.v.z + turn.v.y * start.v.y) + turn.v.x * start.v.x);
    m_frame->SetRot(rot);
    m_frame->Update();

    // Rays along the box's edges and two of its diagonals, a little inside it, its own volume
    // ignored.
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    I3D_bbox hierarchy;
    const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, m_frame);
    const S_vector high(box.max.x - CORNER_INSET, box.max.y - CORNER_INSET, box.max.z - CORNER_INSET);
    const S_vector low(box.min.x + CORNER_INSET, box.min.y + CORNER_INSET, box.min.z + CORNER_INSET);
    S_vector corners[8];
    int n = 0;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            corners[n++] = S_vector(i ? low.x : high.x, j ? low.y : high.y, low.z);
            corners[n++] = S_vector(i ? low.x : high.x, j ? low.y : high.y, high.z);
        }
    }
    const S_matrix world = m_frame->GetMatrix();
    for (int i = 8; i-- > 0;)
        corners[i] *= world;
    bool blocked = false;
    S_vector hitPos, hitNormal;
    for (const auto &edge : EDGES) {
        const S_vector &from = corners[edge[0]];
        const S_vector &to = corners[edge[1]];
        const S_vector dir(to.x - from.x, to.y - from.y, to.z - from.z);
        if (g_collisionWorld.RayTestDynamic(from, dir, &hitPos, &hitNormal, RAY_EXCLUDE, 0)) {
            blocked = true;
            break;
        }
    }
    for (int k = 0; k < 2 && !blocked; ++k) {
        const S_vector &from = corners[k == 0 ? 1 : 2];
        const S_vector &to = corners[k == 0 ? 6 : 5];
        const S_vector dir(to.x - from.x, to.y - from.y, to.z - from.z);
        blocked = g_collisionWorld.RayTestDynamic(from, dir, &hitPos, &hitNormal, RAY_EXCLUDE, 0) != nullptr;
    }
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    if (blocked) {
        m_frame->SetRot(previous);
        m_frame->Update();
    }
    return blocked;
}

void C_Pike::Tick(int timeDelta)                        // 0x005b9340
{
    if (m_state == PIKE_DOWN || m_state == PIKE_UP || IsClient())
        return;
    const float angle = m_angle;
    if (m_blockedTime != 0) {
        m_blockedTime -= timeDelta;
        if (m_blockedTime > 0)
            return;
        m_blockedTime = 0;
    }
    const float step = static_cast<float>(static_cast<uint32_t>(timeDelta)) * TIME_SCALE * m_speed;
    if (m_state == PIKE_RAISING) {
        m_angle = step + m_angle;
        if (m_maxAngle < m_angle) {
            m_state = PIKE_UP;
            m_angle = m_maxAngle;
        }
    } else if (m_state == PIKE_LOWERING) {
        m_angle = m_angle - step;
        if (m_angle < 0.0f) {
            m_state = PIKE_DOWN;
            m_angle = 0.0f;
        }
    }
    if (Turn(timeDelta)) {
        m_blockedTime = BLOCKED_WAIT;
        m_angle = angle;
    }
    UpdateVolume(false);
}

void C_Pike::Update(int frameTime)                      // 0x005b9470
{
    if (m_blockedTime != 0) {
        m_blockedTime -= frameTime;
        if (m_blockedTime > 0)
            return;
        m_blockedTime = 0;
    }
    const float angle = m_angle;
    const float step = static_cast<float>(static_cast<uint32_t>(frameTime)) * TIME_SCALE * m_speed;
    bool arrived = false;
    float end = 0.0f;
    int sound = -1;
    if (m_state == PIKE_RAISING) {
        m_angle = step + m_angle;
        if (!(m_angle <= m_maxAngle)) {
            end = m_maxAngle;
            m_state = PIKE_UP;
            m_sound.flags = SOUND_SILENT_TO_AI;
            sound = m_highSound;
            arrived = true;
        }
    } else {
        if (m_state != PIKE_LOWERING)
            return;
        m_angle = m_angle - step;
        if (!(0.0f <= m_angle)) {
            sound = m_lowSound;
            m_state = PIKE_DOWN;
            m_sound.flags = SOUND_SILENT_TO_AI;
            arrived = true;
        }
    }
    if (arrived) {
        m_angle = end;
        m_sound.group = sound;
        if (sound != -1)
            PlaySoundRequest(m_sound);
        if (m_loopSound != -1) {
            GetSoundModule()->Stop(&m_loopSound);
            m_loopSound = -1;
        }
    }
    if (Turn(frameTime)) {
        m_blockedTime = BLOCKED_WAIT;
        m_angle = angle;
    }
    // (Both replaced by UpdateVolume: the frame's bound as the volume's centre, its inverse world
    // matrix.)
    const I3D_bound_volume &bound = m_frame->GetBound();
    m_invMatrix.Inverse(m_frame->GetMatrix());
    m_volume.center = bound.center;
    m_volume.centerW = bound.radius;
    UpdateVolume(false);
}

void C_Pike::StartMission(bool server)                  // 0x005b9670
{
    Activate(server);
    if (m_started)
        return;
    m_started = true;
    m_angle = 0.0f;
    m_startRot = m_frame->GetRot();
    if (IsServer() && m_state == PIKE_UP)
        SetState(PIKE_RAISING);
    m_volume = S_CollisionVolume();
    m_volume.frame = m_frame;
    m_volume.actor = this;
    m_volume.flags = COLLISION_SHAPE_BOX;
    m_volume.type = COLLISION_TYPE_PIKE;
    UpdateVolume(true);
    m_sound.chapter = SOUND_CHAPTER_OBJECT_MANIPULATION;
    const S_matrix &world = m_frame->GetMatrix();
    m_sound.pos = world(3);
    m_sound.posW = world.m[3][3];
    m_sound.source = nullptr;
    m_sound.attachTo = nullptr;
    m_sound.flags = 0;
    m_loopSound = -1;
}

void C_Pike::UpdateVolume(bool add)                     // 0x005b97b0 (DS 0x005ccba0)
{
    I3D_frame *frame = m_volume.frame;
    if (!frame)
        return;
    I3D_bbox box;
    // Fix: the centre is 0 for an empty box (the original leaves it unset).
    S_vector center(0.0f, 0.0f, 0.0f);
    float radius = 0.0f;
    if (frame->GetType() == FRAME_DUMMY) {
        // A dummy's own box: the centre halfway, the radius half its height (quirk kept).
        box = static_cast<I3D_dummy *>(frame)->GetBBox();
        center = S_vector((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                          (box.min.z + box.max.z) * 0.5f);
        radius = (box.max.y - box.min.y) * 0.5f;
    } else {
        I3D_bbox hierarchy;
        box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, frame);
        if (box.min.x <= box.max.x) {
            const S_vector half = (box.max - box.min) * 0.5f;
            center = box.min + half;
            radius = std::sqrt((half.z * half.z + half.y * half.y) + half.x * half.x);
        }
    }
    // The box at the frame's own scale; the world matrix without it.
    const S_vector scale = frame->GetScale();
    box.max.x = box.max.x * scale.x;
    box.max.y = scale.y * box.max.y;
    box.max.z = box.max.z * scale.z;
    box.min.x = box.min.x * scale.x;
    box.min.y = scale.y * box.min.y;
    box.min.z = box.min.z * scale.z;
    // (The original stores the centre once more before the matrices, then replaces it.)
    m_volume.radius = radius;
    m_volume.bbox = box;
    m_matrix = frame->GetMatrix();
    const float sx = 1.0f / scale.x, sy = 1.0f / scale.y, sz = 1.0f / scale.z;
    for (int c = 0; c < 3; ++c) {
        m_matrix.m[0][c] = sx * m_matrix.m[0][c];
        m_matrix.m[1][c] = sy * m_matrix.m[1][c];
        m_matrix.m[2][c] = sz * m_matrix.m[2][c];
    }
    m_volume.matrix = &m_matrix;
    m_invMatrix.Inverse(m_matrix);
    m_volume.invMatrix = &m_invMatrix;
    const S_vector worldScale = frame->GetWorldScale();
    const float maxYZ = worldScale.y > worldScale.z ? worldScale.y : worldScale.z;
    const float maxScale = worldScale.x > maxYZ ? worldScale.x : maxYZ;
    m_volume.radius = maxScale * radius;
    m_volume.center = center * frame->GetMatrix();
    m_volume.centerW = m_matrix.m[0][3];                // (a stale local in the original: this)
    if (add)
        g_collisionWorld.AddVolume(&m_volume);
    g_collisionWorld.MoveVolume(&m_volume);
}

void C_Pike::StopMission(bool server)                   // 0x005b9bc0
{
    Deactivate(server);
    if (!m_started)
        return;
    m_frame->SetRot(m_startRot);
    m_frame->Update();
    g_collisionWorld.RemoveVolume(&m_volume);
    m_started = false;
    if (m_loopSound != -1) {
        GetSoundModule()->Stop(&m_loopSound);
        m_loopSound = -1;
    }
}

bool C_Pike::ReadProperties(C_chunk &chunk)             // 0x005b9c80
{
    chunk.Read(m_state);
    chunk.Read(m_maxAngle);
    chunk.Read(m_speed);
    float axis[3] = {};                                 // (the original goes on with stack values)
    chunk.Read(axis);
    m_axis.x = axis[0];
    m_axis.y = axis[1];
    m_axis.z = axis[2];
    chunk.Read(m_lowSound);
    chunk.Read(m_highSound);
    chunk.Read(m_moveSound);
    return true;
}

void C_Pike::SendState(uint32_t kind)                   // 0x005b9e70
{
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(kind, MSG_BITS);
    SendActorMessage(msg);
}

void C_Pike::OnMessage(C_BitStream *msg)                // 0x005b9ec0
{
    const uint32_t kind = msg->ReadBits(MSG_BITS);
    if (kind != MSG_RAISING && kind != MSG_LOWERING)
        return;
    const int start = kind == MSG_RAISING ? m_lowSound : m_highSound;
    // (Without the start sound there is no loop either.)
    if (start != -1) {
        m_sound.flags = SOUND_SILENT_TO_AI;
        m_sound.group = start;
        PlaySoundRequest(m_sound);
        m_sound.flags = SOUND_SILENT_TO_AI | SOUND_LOOP;
        m_sound.group = m_moveSound;
        if (m_moveSound != -1 && m_loopSound == -1)
            m_loopSound = GetSoundModule()->Play(m_sound);
    }
    m_state = kind == MSG_RAISING ? PIKE_RAISING : PIKE_LOWERING;
}

void C_Pike::WriteState(C_BitStream *msg)               // 0x005ba000
{
    msg->WriteBits(m_state != PIKE_LOWERING && m_state != PIKE_DOWN ? MSG_RAISING : MSG_LOWERING, MSG_BITS);
}

void C_Pike::ReadState(C_BitStream *msg)                // 0x005ba070
{
    const uint32_t kind = msg->ReadBits(MSG_BITS);
    if (kind == MSG_RAISING) {
        if (m_state != PIKE_UP)
            m_state = PIKE_RAISING;
    } else if (kind == MSG_LOWERING && m_state != PIKE_DOWN) {
        m_state = PIKE_LOWERING;
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_PikeCreator::Create()                        // 0x005b88f0
{
    return new C_Pike();                                // _aligned_malloc(0x210, 16) in the original
}

void C_PikeCreator::Destroy(C_Actor *actor)             // 0x005b8960
{
    delete actor;
}

static C_PikeCreator g_pikeCreator;                     // 0x008ac5a4

// Registers the creator (static initialiser 0x005b88d0, thunk 0x005b88c0).
static const bool g_pikeCreatorRegistered = [] {
    g_actorCreators[ACTOR_PIKE] = &g_pikeCreator;
    return true;
}();
