// The rigid body (C_RigidBody; original 0x004c7d90..0x004cff70): lifetime, setup, state, impulses,
// saved games and the per-frame bookkeeping. The integration steps (StepWheel, StepBox and their
// helpers) are in rigid_body_step.cpp.
#include "game/rigid_body.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/game.h"
#include "game/rigid_body_internal.h"

std::vector<C_RigidBody *> g_rigidBodies;              // 0x0089bcd4

namespace {

// (The hierarchy box GetHierarchyBox fills: 0x0089bcf0.)
I3D_bbox g_hierarchyBox;

constexpr float MIN_REST_SPEED = 0.3f;                  // m/s: faster bodies are not slow
constexpr float MIN_REST_SPIN = 0.1f;                   // rad/s
constexpr float MOVED_SQUARE = 0.04f;                   // a body moving 0.2 m is not still
constexpr uint32_t STILL_REST_TIME = 2000;              // ms still before it may rest
constexpr float ACTOR_MOVED_SQUARE = 1.6e-05f;          // an actor under the body moving 4 mm wakes it
constexpr float FELL_SPEED = -85.0f;                    // m/s
constexpr uint32_t MAX_FRAME = 83;                      // ms: longer frames are slowed down
constexpr float IMPULSE_SCALE = 1.1f;
constexpr float START_REST_SPEED_SQUARE = 0.01f;
constexpr float SETUP_REST_SPEED_SQUARE = 1e-05f;
constexpr float REST_DELAY = 0.6f;
constexpr float BOX_PADDING = 0.05f;                    // added to the box's sides for the inertia
constexpr float MAX_SKIN = 0.04f;
constexpr float TOPPLE_RATIO = 2.5f;                    // a box this many times taller than its base topples
constexpr float TOPPLE_IMPULSE = 0.12f;
constexpr int SAVE_VERSION = 5;

// The world box of `frame`'s visual and its visual descendants, through `parent` (the frame's
// parent's matrix in the hierarchy box's space). (0x004c7e50)
void AddHierarchyBox(I3D_frame *frame, const S_matrix &parent)
{
    if (frame->GetVisualType() != I3DVISUAL_OBJECT)
        return;
    const S_matrix matrix = frame->GetLocalMatrix() * parent;
    const I3D_bbox &box = static_cast<I3D_visual *>(frame)->GetLocalBound().bbox;
    if (box.min.x <= box.max.x) {
        for (int corner = 0; corner < 8; ++corner) {
            const S_vector p(corner & 1 ? box.max.x : box.min.x, corner & 2 ? box.max.y : box.min.y,
                             corner & 4 ? box.max.z : box.min.z);
            g_hierarchyBox.Expand(p * matrix);
        }
    }
    for (I3D_frame *child = frame->GetFirstChild(); child; child = child->GetNextSibling())
        AddHierarchyBox(child, matrix);
}

// The saved game's record (0xA4 bytes).
struct S_RigidBodySave {
    int version;                                        // +0x00 SAVE_VERSION
    int mode;                                           // +0x04
    S_vector pos;                                       // +0x08 the frame's world position
    S_vector centre;                                    // +0x14
    float rot[4];                                       // +0x20 the frame's rotation: s, x, y, z
    float maxStep;                                      // +0x30
    float mass;                                         // +0x34
    S_vector invInertia;                                // +0x38
    S_vector velocity;                                  // +0x44
    S_vector spin;                                      // +0x50
    float friction;                                     // +0x5C
    float bounce;                                       // +0x60
    float contactRange;                                 // +0x64 (loading: also the ground distance)
    float stiffness;                                    // +0x68
    float unknown584;                                   // +0x6C
    float width;                                        // +0x70
    float radius;                                       // +0x74
    float unknown594;                                   // +0x78
    float springForce;                                  // +0x7C
    S_vector contactPoint;                              // +0x80
    float contactRange2;                                // +0x8C (the contact range again; not read)
    float slowTime;                                     // +0x90
    int unknown5AC;                                     // +0x94
    int hitMaterial;                                    // +0x98
    bool unknown9C;                                     // +0x9C (true; not read)
    float g;                                            // +0xA0
};
static_assert(sizeof(S_RigidBodySave) == 0xA4, "S_RigidBodySave layout");

}  // namespace

const I3D_bbox &GetHierarchyBox(I3D_frame *frame)       // 0x004c7db0
{
    if (frame->GetVisualType() == I3DVISUAL_OBJECT) {
        g_hierarchyBox = static_cast<I3D_visual *>(frame)->GetLocalBound().bbox;
    } else {
        // (The w components are left as they were.)
        g_hierarchyBox.min = S_vector(1e16f, 1e16f, 1e16f);
        g_hierarchyBox.max = S_vector(-1e16f, -1e16f, -1e16f);
    }
    for (I3D_frame *child = frame->GetFirstChild(); child; child = child->GetNextSibling())
        AddHierarchyBox(child, IdentityMatrix());
    return g_hierarchyBox;
}

void ForgetActor(C_Actor *actor)                        // 0x004cff70
{
    for (size_t i = g_rigidBodies.size(); i-- > 0;)
        g_rigidBodies[i]->ForgetActor(actor);
}

C_RigidBody::C_RigidBody()                              // 0x004c84a0
{
    // (The members SetFrame / Setup / Start set are left unset by the original.)
    g_rigidBodies.push_back(this);
}

C_RigidBody::~C_RigidBody()                             // 0x004c8770
{
    for (size_t i = g_rigidBodies.size(); i-- > 0;) {
        if (g_rigidBodies[i] == this) {
            g_rigidBodies[i] = g_rigidBodies.back();
            g_rigidBodies.pop_back();
        }
    }
    delete[] m_actorContacts;
    m_actorContacts = nullptr;
    m_actorContactCount = 0;
    m_actorContactsCapacity = 0;
}

void C_RigidBody::RefreshMatrices()                     // 0x004ceaa0
{
    m_matrix = m_frame->GetMatrix();
    const S_matrix source = m_matrix;
    m_invMatrix = m_matrix;
    m_invMatrix.Inverse(source);
    m_rotation = m_matrix;
    m_rotation.m[3][0] = m_rotation.m[3][1] = m_rotation.m[3][2] = 0.0f;
    m_invRotation = m_invMatrix;
    m_invRotation.m[3][0] = m_invRotation.m[3][1] = m_invRotation.m[3][2] = 0.0f;
    if (m_hasCentre)
        SetXYZ(m_centreWorld, m_centre * m_matrix);     // (the original's w is a temporary's, unset)
    else
        m_centreWorld = Row(m_frame->GetMatrix(), 3);
    m_pos = Row(m_frame->GetMatrix(), 3);
    m_right = Row(m_matrix, 0);
    m_up = Row(m_matrix, 1);
    m_dir = Row(m_matrix, 2);
}

bool C_RigidBody::SetFrame(I3D_frame *frame)            // 0x004c8820
{
    m_frame = frame;
    m_pos = Row(frame->GetMatrix(), 3);
    m_matrix = m_frame->GetMatrix();
    const S_matrix source = m_matrix;
    m_invMatrix = m_matrix;
    m_invMatrix.Inverse(source);
    m_rotation = m_matrix;
    m_rotation.m[3][0] = m_rotation.m[3][1] = m_rotation.m[3][2] = 0.0f;
    m_invRotation = m_invMatrix;
    m_invRotation.m[3][0] = m_invRotation.m[3][1] = m_invRotation.m[3][2] = 0.0f;
    m_right = Row(m_matrix, 0);
    m_up = Row(m_matrix, 1);
    m_state = RIGID_BODY_AT_REST;                       // (set directly: no callback)
    m_dir = Row(m_matrix, 2);
    return true;
}

void C_RigidBody::SetOwner(C_Actor *owner)              // 0x004c8250
{
    m_owner = owner;
    m_volume.actor = owner;
}

void C_RigidBody::SetState(int state)                   // 0x004c8270
{
    if (state == m_state)
        return;
    if (m_state == RIGID_BODY_MOVING && state == RIGID_BODY_AT_REST && m_callback)
        m_callback(RIGID_BODY_EVENT_REST, 0, m_context, nullptr);   // (the original passes the body in EDX)
    m_state = state;
    if (state == RIGID_BODY_MOVING) {
        m_slowTime = 0.0f;
        return;
    }
    if (!m_restVolume)
        return;
    // At rest (or fallen) the volume is the doubled box.
    const I3D_bbox &box = GetHierarchyBox(m_frame);
    float square = (box.min.x * box.min.x + box.min.y * box.min.y) + box.min.z * box.min.z;
    const float maxSquare = (box.max.x * box.max.x + box.max.y * box.max.y) + box.max.z * box.max.z;
    if (maxSquare > square)
        square = maxSquare;
    const float radius = LengthOf(square);
    m_volume.flags = COLLISION_SHAPE_BOX;
    m_volume.center = XYZ(m_pos);
    m_volume.centerW = m_pos.w;
    m_volume.radius = radius + radius;
    // (The box's w components come from the original's stack, unset.)
    m_volume.bbox.min = S_vector(box.min.x + box.min.x, box.min.y + box.min.y, box.min.z + box.min.z);
    m_volume.bbox.max = S_vector(box.max.x + box.max.x, box.max.y + box.max.y, box.max.z + box.max.z);
    m_volume.invMatrix = &m_invMatrix;
    m_volume.matrix = &m_matrix;
    m_volume.actor = m_owner;
    m_volume.frame = m_frame;
    g_collisionWorld.MoveVolume(&m_volume);
}

void C_RigidBody::Setup(const S_RigidBodyParams &params)   // 0x004cd6e0
{
    m_context = params.context;
    m_callback = params.callback;
    m_restVolume = params.unknown41;
    m_moveCollision = params.moveCollision;
    m_mass = params.weight;
    m_friction = params.friction;
    m_velocity = XYZ(params.velocity);
    m_velocityW = params.velocity.w;
    if ((m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y) + m_velocity.z * m_velocity.z <
        SETUP_REST_SPEED_SQUARE)
        SetState(RIGID_BODY_AT_REST);
    // The centre of mass, the inertia of the box (5 cm larger each way) and the points: from the
    // frame's hierarchy box.
    const I3D_bbox &box = GetHierarchyBox(m_frame);
    const float dx = box.max.x - box.min.x;
    const float dy = box.max.y - box.min.y;
    const float dz = box.max.z - box.min.z;
    m_centre = S_vector(dx * 0.5f + box.min.x, 0.5f * dy + box.min.y, dz * 0.5f + box.min.z);
    m_hasCentre = true;
    const float sx = dx + BOX_PADDING;
    const float sy = dy + BOX_PADDING;
    const float sz = dz + BOX_PADDING;
    const float inverseY = 1.0f / sy;
    const auto within = [](float ratio) { return !(ratio > 2.0f) && !(ratio < 0.5f); };
    if (within(inverseY * sx) && within(sx / sz) && within(inverseY * sz)) {
        // About a cube: a cube's (side the average).
        const float side = ((sy + sz) + sx) * 0.33333334f;
        const float inverse = 1.0f / (((side * side) * m_mass) * 0.33333334f);
        m_invInertia = S_vector(inverse, inverse, inverse);
    } else {
        // The longest side's square sets the two others' axes; the third is scaled by the longest
        // and the middle sides' squares.
        const auto setLong = [&](float longest, float middle, float &axis, float &other1, float &other2) {
            const float longSquare = longest * longest;
            const float inverse = 1.0f / ((longSquare * m_mass) * 0.33333334f);
            other1 = inverse;
            other2 = inverse;
            axis = (longSquare / (middle * middle)) * inverse;
        };
        if (!(sx > sy)) {
            if (!(sy > sz))
                setLong(sz, !(sy > sx) ? sx : sy, m_invInertia.z, m_invInertia.y, m_invInertia.x);
            else
                setLong(sy, !(sx > sz) ? sz : sx, m_invInertia.y, m_invInertia.x, m_invInertia.z);
        } else if (!(sx > sz)) {
            setLong(sz, !(sy > sx) ? sx : sy, m_invInertia.z, m_invInertia.y, m_invInertia.x);
        } else {
            setLong(sx, !(sy > sz) ? sz : sy, m_invInertia.x, m_invInertia.y, m_invInertia.z);
        }
    }
    const float smallest = !(sx > sy) ? (!(sx > sz) ? sx : sz) : (!(sy > sz) ? sy : sz);
    m_skin = smallest * 0.5f;
    if (m_skin > MAX_SKIN)
        m_skin = MAX_SKIN;
    // The corners and the faces' middles (the original's w are unset but for the first and fifth).
    const float cx = (box.max.x - box.min.x) * 0.5f + box.min.x;
    const float cy = (box.max.y - box.min.y) * 0.5f + box.min.y;
    const float cz = (box.max.z - box.min.z) * 0.5f + box.min.z;
    m_points[0] = {box.min.x, box.min.y, box.min.z, box.minW};
    m_points[1] = {box.min.x, box.min.y, box.max.z, 0.0f};
    m_points[2] = {box.max.x, box.min.y, box.max.z, 0.0f};
    m_points[3] = {box.max.x, box.min.y, box.min.z, 0.0f};
    m_points[4] = {box.max.x, box.max.y, box.max.z, box.maxW};
    m_points[5] = {box.min.x, box.max.y, box.min.z, 0.0f};
    m_points[6] = {box.min.x, box.max.y, box.max.z, 0.0f};
    m_points[7] = {box.max.x, box.max.y, box.min.z, 0.0f};
    m_points[8] = {cx, cy, box.min.z, 0.0f};
    m_points[9] = {cx, cy, box.max.z, 0.0f};
    m_points[10] = {cx, box.min.y, cz, 0.0f};
    m_points[11] = {cx, box.max.y, cz, 0.0f};
    m_points[12] = {box.min.x, cy, cz, 0.0f};
    m_points[13] = {box.max.x, cy, cz, 0.0f};
    const float massSide = m_mass * 0.0f;
    m_gravity = {massSide * m_g, (m_mass * -1.0f) * m_g, massSide * m_g, 0.0f};
    m_bounce = 1.01f;
    m_contactRange = 0.04f;
    m_mode = RIGID_BODY_BOX;
    m_stiffness = (m_g * m_mass) * 12.5f;
    m_unknown584 = m_mass * 4.0f;
}

void C_RigidBody::SetupWheel(const S_WheelDebrisParams &params)   // 0x004cd550
{
    m_mass = params.weight;
    m_radius = params.radius;
    m_width = params.width;
    const float inverse = 1.0f / (((m_radius * m_radius) * m_mass) * 0.5f);
    m_invInertia = S_vector(inverse, inverse, inverse);
    m_velocity = XYZ(params.velocity);
    m_velocityW = params.velocity.w;
    if ((m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y) + m_velocity.z * m_velocity.z <
        SETUP_REST_SPEED_SQUARE)
        SetState(RIGID_BODY_AT_REST);
    m_friction = params.unknown1C;
    const float massSide = m_mass * 0.0f;
    m_gravity = {massSide * m_g, (m_mass * -1.0f) * m_g, massSide * m_g, 0.0f};
    m_unknown594 = params.unknown20;
    m_contactRange = 0.03f;
    m_bounce = 1.05f;
    m_mode = RIGID_BODY_DISC;
    m_stiffness = (m_g * m_mass) * 37.03704f;
    m_context = nullptr;
    m_callback = params.callback;
    m_restVolume = params.moveCollision;
    m_moveCollision = true;
}

bool C_RigidBody::ApplyImpulse(int /*unused*/, const S_vector4 &point, const S_vector &impulse, int /*unused4*/,
                               int /*unused5*/, int /*unused6*/)   // 0x004cdfe0
{
    SetState(RIGID_BODY_MOVING);
    m_stillTime = 0;
    m_slowTime = 0.0f;
    const float rx = point.x - m_centreWorld.x;
    const float ry = point.y - m_centreWorld.y;
    const float rz = point.z - m_centreWorld.z;
    const float jx = impulse.x * IMPULSE_SCALE;
    const float jy = impulse.y * IMPULSE_SCALE;
    const float jz = impulse.z * IMPULSE_SCALE;
    const float inverseMass = 1.0f / m_mass;
    m_velocity.x = jx * inverseMass + m_velocity.x;
    m_velocity.y = jy * inverseMass + m_velocity.y;
    m_velocity.z = jz * inverseMass + m_velocity.z;
    S_vector turn(jz * ry - jy * rz, rz * jx - jz * rx, jy * rx - ry * jx);
    turn *= m_invRotation;
    turn.x = turn.x * m_invInertia.x;
    turn.y = turn.y * m_invInertia.y;
    turn.z = turn.z * m_invInertia.z;
    turn *= m_rotation;
    m_spin.x = m_spin.x - turn.x;
    m_spin.y = m_spin.y - turn.y;
    m_spin.z = m_spin.z - turn.z;
    const float size = LengthOf((impulse.z * impulse.z + impulse.y * impulse.y) + impulse.x * impulse.x);
    if (m_hitMaterial >= 0 && size > m_hardestHit) {
        m_hardestHit = size;
        m_hitPointIndex = 0;
        m_pointSurface[0] = static_cast<uint16_t>(m_hitMaterial);
        m_hitPoint = point;
    }
    return true;
}

void C_RigidBody::AddImpulse(const S_vector &impulse, const S_vector4 &point)   // 0x004ce7a0
{
    SetState(RIGID_BODY_MOVING);
    m_stillTime = 0;
    m_slowTime = 0.0f;
    const float rx = point.x - m_centreWorld.x;
    const float ry = point.y - m_centreWorld.y;
    const float rz = point.z - m_centreWorld.z;
    const float inverseMass = 1.0f / m_mass;
    m_velocity.x = inverseMass * impulse.x + m_velocity.x;
    m_velocity.y = inverseMass * impulse.y + m_velocity.y;
    m_velocity.z = inverseMass * impulse.z + m_velocity.z;
    S_vector turn(ry * impulse.z - rz * impulse.y, rz * impulse.x - rx * impulse.z, rx * impulse.y - ry * impulse.x);
    turn *= m_invRotation;
    turn.x = turn.x * m_invInertia.x;
    turn.y = turn.y * m_invInertia.y;
    turn.z = turn.z * m_invInertia.z;
    turn *= m_rotation;
    m_spin.x = m_spin.x - turn.x;
    m_spin.y = m_spin.y - turn.y;
    m_spin.z = m_spin.z - turn.z;
}

bool C_RigidBody::Wake(int /*unused1*/, int /*unused2*/, int /*unused3*/, int /*unused4*/, int /*unused5*/,
                       int /*unused6*/, int /*unused7*/, int /*unused8*/)   // 0x004ce5f0
{
    SetState(RIGID_BODY_MOVING);
    m_stillTime = 0;
    m_slowTime = 0.0f;
    return true;
}

void C_RigidBody::Nudge()                               // 0x004c9480
{
    if (m_state != RIGID_BODY_MOVING)
        return;
    if (m_nudged) {
        m_slowTime = 0.0f;
        m_stillTime = 0;
    }
    m_nudged = true;
}

void C_RigidBody::Start()                               // 0x004c8980
{
    SetOwner(nullptr);
    for (int i = RIGID_BODY_POINTS; i-- > 0;) {
        m_pointHit[i] = false;
        m_pointActors[i] = nullptr;
        m_pointOnActor[i] = false;
    }
    m_matrix = m_frame->GetMatrix();
    const S_matrix source = m_matrix;
    m_invMatrix = m_matrix;
    m_invMatrix.Inverse(source);
    m_rotation = m_matrix;
    m_rotation.m[3][0] = m_rotation.m[3][1] = m_rotation.m[3][2] = 0.0f;
    m_invRotation = m_invMatrix;
    m_invRotation.m[3][0] = m_invRotation.m[3][1] = m_invRotation.m[3][2] = 0.0f;
    m_right = Row(m_matrix, 0);
    m_up = Row(m_matrix, 1);
    m_dir = Row(m_matrix, 2);
    m_pos = Row(m_frame->GetMatrix(), 3);
    // Its volume: the hierarchy box, in a sphere as far as its farther corner.
    const I3D_bbox &box = GetHierarchyBox(m_frame);
    float square = (box.min.x * box.min.x + box.min.y * box.min.y) + box.min.z * box.min.z;
    const float maxSquare = (box.max.x * box.max.x + box.max.y * box.max.y) + box.max.z * box.max.z;
    if (maxSquare > square)
        square = maxSquare;
    const float radius = LengthOf(square);
    m_volume = S_CollisionVolume();
    m_volume.radius = radius;
    m_volume.flags = COLLISION_SHAPE_BOX;
    m_volume.center = XYZ(m_pos);
    m_volume.centerW = m_pos.w;
    m_volume.bbox = box;
    m_volume.matrix = &m_matrix;
    m_volume.invMatrix = &m_invMatrix;
    m_volume.frame = m_frame;
    m_volume.type = m_moveCollision ? RIGID_BODY_VOLUME_TYPE_MOVING : RIGID_BODY_VOLUME_TYPE;
    g_collisionWorld.AddVolume(&m_volume);
    g_collisionWorld.MoveVolume(&m_volume);
    if (m_hasCentre)
        SetXYZ(m_centreWorld, m_centre * m_matrix);     // (the original's w is a temporary's, unset)
    else
        m_centreWorld = m_pos;
    m_springForce = 0.0f;
    m_spin = S_vector(0.0f, 0.0f, 0.0f);
    m_slowTime = 0.0f;
    m_contactPoint.x = m_contactPoint.y = m_contactPoint.z = 0.0f;
    m_restDelay = REST_DELAY;
    m_stillTime = 0;
    m_restPos = m_pos;
    for (uint16_t &surface : m_pointSurface)
        surface = 0;
    m_hitPointIndex = 0;
    m_hardestHit = 0.0f;
    m_unknown5B8 = 0;
    m_timeScale = 1.0f;
    m_nudged = true;
    const float speedSquare = (m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y) + m_velocity.z * m_velocity.z;
    SetState(speedSquare < START_REST_SPEED_SQUARE ? RIGID_BODY_AT_REST : RIGID_BODY_MOVING);
    for (int i = RIGID_BODY_POINTS; i-- > 0;)
        m_lastPoints[i].x = m_lastPoints[i].y = m_lastPoints[i].z = 0.0f;
    m_contactsChanged = true;
}

void C_RigidBody::Stop()                                // 0x004c8ea0
{
    g_collisionWorld.RemoveVolume(&m_volume);
    delete[] m_actorContacts;
    m_actorContacts = nullptr;
    m_actorContactCount = 0;
    m_actorContactsCapacity = 0;
}

void C_RigidBody::Step(uint32_t time)                   // 0x004c8ee0
{
    if (!(m_slowTime <= m_restDelay) || m_stillTime >= STILL_REST_TIME + 1)
        return;
    m_timeScale = 1.0f;
    if (time > MAX_FRAME) {
        m_timeScale = static_cast<float>(MAX_FRAME) / static_cast<float>(time);
        time = MAX_FRAME;
    } else if (time == 0) {
        return;
    }
    const float dt = static_cast<float>(time) * 0.001f;
    if (dt <= m_maxStep) {
        if (m_mode == RIGID_BODY_DISC)
            StepWheel(dt);
        else
            StepBox(dt);
        return;
    }
    const int steps = static_cast<int>(dt / m_maxStep) + 1;
    const float step = dt / static_cast<float>(steps);
    for (int i = steps; i > 0; --i) {
        if (m_mode == RIGID_BODY_DISC)
            StepWheel(step);
        else
            StepBox(step);
    }
}

void C_RigidBody::Tick(uint32_t time)                   // 0x004c9000
{
    if (m_state == RIGID_BODY_AT_REST)
        return;
    Step(time);
    if (time > MAX_FRAME)
        time = MAX_FRAME;
    else if (time == 0)
        return;
    const float dt = static_cast<float>(time) * 0.001f;
    // The slow time (under 0.3 m/s and 0.1 rad/s) and the still time (within 0.2 m).
    const float speed = LengthOf((m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y) + m_velocity.z * m_velocity.z);
    const float spin = LengthOf((m_spin.x * m_spin.x + m_spin.y * m_spin.y) + m_spin.z * m_spin.z);
    if (speed >= MIN_REST_SPEED || spin >= MIN_REST_SPIN)
        m_slowTime = 0.0f;
    else
        m_slowTime = dt + m_slowTime;
    const float mz = m_pos.z - m_restPos.z;
    const float my = m_pos.y - m_restPos.y;
    const float mx = m_pos.x - m_restPos.x;
    if ((mz * mz + my * my) + mx * mx >= MOVED_SQUARE) {
        m_stillTime = 0;
        m_restPos = m_pos;
    } else {
        m_stillTime += time;
    }
    if (m_restDelay < m_slowTime || m_stillTime > STILL_REST_TIME) {
        if (m_actorContactCount == 0) {
            if (Topple())
                SetState(RIGID_BODY_AT_REST);
        } else {
            // Lying on actors: one moving wakes it.
            for (int i = m_actorContactCount; i-- > 0;) {
                S_RigidBodyActorContact &contact = m_actorContacts[i];
                const S_vector4 &at = contact.actor->GetPosition4();
                const float dz = contact.pos.z - at.z;
                const float dy = contact.pos.y - at.y;
                const float dx = contact.pos.x - at.x;
                if ((dz * dz + dy * dy) + dx * dx > ACTOR_MOVED_SQUARE) {
                    contact.pos = at;
                    m_slowTime = 0.0f;
                    m_stillTime = 0;
                    m_contactsChanged = true;
                    if (m_hasCentre)
                        SetXYZ(m_centreWorld, m_centre * m_matrix);
                    else
                        m_centreWorld = m_pos;
                    break;
                }
            }
        }
    } else {
        // The actors not touched for a second are forgotten.
        for (int i = m_actorContactCount; i-- > 0;) {
            m_actorContacts[i].time -= static_cast<int>(time);
            if (m_actorContacts[i].time < 0) {
                for (int k = i; k < m_actorContactCount - 1; ++k)
                    m_actorContacts[k] = m_actorContacts[k + 1];
                if (m_actorContactCount != 0)
                    --m_actorContactCount;
                m_contactsChanged = true;
            }
        }
    }
    if (m_velocity.y < FELL_SPEED)
        SetState(RIGID_BODY_FELL);
    m_frame->Update();
    const I3D_bound_volume &bound = m_frame->GetBound();
    GetGame()->GetScene()->UpdateFrameSector(m_frame, bound.center);
    RefreshMatrices();
    m_volume.center = bound.center;
    m_volume.centerW = bound.radius;
    g_collisionWorld.MoveVolume(&m_volume);
    m_hardestHit = 0.0f;
    m_hitPointIndex = 0;
}

bool C_RigidBody::Topple()                              // 0x004cf440
{
    if (m_mode != RIGID_BODY_BOX)
        return true;
    if (m_actorContactCount != 0)
        return false;
    // The body's axes in the world, each turned upward.
    const auto axis = [&](const S_vector &local) {
        S_vector v = local * m_matrix;
        v = S_vector(v.x - m_centreWorld.x, v.y - m_centreWorld.y, v.z - m_centreWorld.z);
        NormalizeBy(v, (v.x * v.x + v.z * v.z) + v.y * v.y);
        return v;
    };
    S_vector x = axis(S_vector(1.0f, 0.0f, 0.0f));
    S_vector y = axis(S_vector(0.0f, 1.0f, 0.0f));
    S_vector z = axis(S_vector(0.0f, 0.0f, 1.0f));
    const auto upward = [](S_vector &v) {
        if (v.y < 0.0f) {
            v.x = v.x * -1.0f;
            v.y = v.y * -1.0f;
            v.z = v.z * -1.0f;
        }
    };
    upward(x);
    upward(y);
    upward(z);
    const float upX = (x.z + x.x) * 0.0f + x.y;
    const float upY = (y.z + y.x) * 0.0f + y.y;
    const float upZ = (z.z + z.x) * 0.0f + z.y;
    const I3D_bbox &box = GetHierarchyBox(m_frame);
    const float sx = box.max.x - box.min.x;
    const float sy = box.max.y - box.min.y;
    const float sz = box.max.z - box.min.z;
    // Standing on the side across the most upright axis: stable unless that axis's extent is more
    // than 2.5 times one of the others; then pushed over toward the narrower side.
    float ratio;
    S_vector dir;
    if (upX > upY && upX > upZ) {
        if (!(sx > sy * TOPPLE_RATIO) && !(sz * TOPPLE_RATIO < sx))
            return true;
        ratio = 1.0f / ((!(sx > sy * TOPPLE_RATIO) ? sz : sy) / sx);
        dir = S_vector(z.x + y.x, z.y + y.y, z.z + y.z);
    } else if (upY > upX && upY > upZ) {
        if (!(sy > sx * TOPPLE_RATIO) && !(sz * TOPPLE_RATIO < sy))
            return true;
        ratio = 1.0f / ((!(sy > sx * TOPPLE_RATIO) ? sz : sx) / sy);
        dir = S_vector(z.x + x.x, z.y + x.y, z.z + x.z);
    } else if (upZ > upX && upZ > upY) {
        if (!(sz > sx * TOPPLE_RATIO) && !(sy * TOPPLE_RATIO < sz))
            return true;
        ratio = 1.0f / ((!(sz > sx * TOPPLE_RATIO) ? sy : sx) / sz);
        dir = S_vector(y.x + x.x, y.y + x.y, y.z + x.z);
    } else {
        return true;
    }
    NormalizeBy(dir, (dir.x * dir.x + dir.z * dir.z) + dir.y * dir.y);
    dir = S_vector(dir.x * TOPPLE_IMPULSE, dir.y * TOPPLE_IMPULSE, dir.z * TOPPLE_IMPULSE);
    const S_vector4 &corner = m_points[0];
    const float reach = LengthOf((corner.x * corner.x + corner.y * corner.y) + corner.z * corner.z);
    const S_vector4 at = {m_centreWorld.x, reach * ratio + m_centreWorld.y, m_centreWorld.z, 0.0f};
    AddImpulse(S_vector(m_mass * dir.x, dir.y * m_mass, dir.z * m_mass), at);
    return false;
}

void C_RigidBody::ForgetActor(C_Actor *actor)           // 0x004ce8f0
{
    for (int n = m_actorContactCount, i = n; i-- > 0;) {
        if (m_actorContacts[i].actor == actor) {
            m_actorContacts[i] = m_actorContacts[m_actorContactCount - 1];
            if (m_actorContactCount != 0)
                --m_actorContactCount;
            m_slowTime = 0.0f;
            m_stillTime = 0;
            m_contactsChanged = true;
        }
    }
    for (int i = RIGID_BODY_POINTS; i-- > 0;) {
        if (m_pointActors[i] == actor)
            m_pointActors[i] = nullptr;
    }
}

bool C_RigidBody::SaveGame(C_chunk &chunk)              // 0x004cec60
{
    S_RigidBodySave save = {};
    save.version = SAVE_VERSION;
    save.pos = m_frame->GetWorldPos();
    const S_quat &rot = m_frame->GetRot();
    save.rot[0] = rot.s;
    save.rot[1] = rot.v.x;
    save.rot[2] = rot.v.y;
    save.rot[3] = rot.v.z;
    save.centre = m_centre;
    save.mode = m_mode;
    save.maxStep = m_maxStep;
    save.mass = m_mass;
    save.invInertia = m_invInertia;
    save.velocity = m_velocity;
    save.spin = m_spin;
    save.friction = m_friction;
    save.bounce = m_bounce;
    save.contactRange = m_contactRange;
    save.stiffness = m_stiffness;
    save.unknown584 = m_unknown584;
    save.width = m_width;
    save.radius = m_radius;
    save.unknown594 = m_unknown594;
    save.springForce = m_springForce;
    save.contactPoint = XYZ(m_contactPoint);
    save.contactRange2 = m_contactRange;
    save.slowTime = m_slowTime;
    save.unknown5AC = m_unknown5AC;
    save.hitMaterial = m_hitMaterial;
    save.unknown9C = true;
    save.g = m_g;
    chunk.Write(&save, sizeof save);
    return true;
}

bool C_RigidBody::LoadGame(C_chunk &chunk)              // 0x004cef00
{
    // Fix: a record that cannot be read is not loaded (the original tests the version of its
    // stack's garbage then).
    S_RigidBodySave save = {};
    chunk.Read(&save, sizeof save);
    if (save.version != SAVE_VERSION)
        return false;
    m_frame->SetPos(save.pos);
    S_quat rot;
    rot.v = S_vector(save.rot[1], save.rot[2], save.rot[3]);
    rot.s = save.rot[0];
    m_frame->SetRot(rot);
    m_frame->Update();
    m_pos = Row(m_frame->GetMatrix(), 3);
    m_centre = save.centre;
    m_mode = save.mode;
    m_maxStep = save.maxStep;
    m_mass = save.mass;
    m_invInertia = save.invInertia;
    m_velocity = save.velocity;
    m_spin = save.spin;
    m_friction = save.friction;
    m_bounce = save.bounce;
    m_contactRange = save.contactRange;
    m_stiffness = save.stiffness;
    m_unknown584 = save.unknown584;
    m_width = save.width;
    m_radius = save.radius;
    m_unknown594 = save.unknown594;
    m_springForce = save.springForce;
    SetXYZ(m_contactPoint, save.contactPoint);
    m_groundDistance = save.contactRange;               // (Quirk kept: the contact range, as Step resets it)
    m_slowTime = save.slowTime;
    m_unknown5AC = save.unknown5AC;
    m_hitMaterial = save.hitMaterial;
    m_g = save.g;
    m_matrix = m_frame->GetMatrix();
    const S_matrix source = m_matrix;
    m_invMatrix = m_matrix;
    m_invMatrix.Inverse(source);
    m_rotation = m_matrix;
    m_rotation.m[3][0] = m_rotation.m[3][1] = m_rotation.m[3][2] = 0.0f;
    m_invRotation = m_invMatrix;
    m_invRotation.m[3][0] = m_invRotation.m[3][1] = m_invRotation.m[3][2] = 0.0f;
    m_right = Row(m_matrix, 0);
    m_up = Row(m_matrix, 1);
    m_dir = Row(m_matrix, 2);
    const float massSide = m_mass * 0.0f;
    m_gravity = {massSide * m_g, (m_mass * -1.0f) * m_g, massSide * m_g, 0.0f};
    if (m_hasCentre)
        SetXYZ(m_centreWorld, m_centre * m_matrix);
    else
        m_centreWorld = m_pos;
    m_stillTime = 0;
    m_restPos = m_pos;
    m_volume.center = XYZ(m_pos);
    m_volume.centerW = m_pos.w;
    g_collisionWorld.MoveVolume(&m_volume);
    for (int i = RIGID_BODY_POINTS; i-- > 0;)
        m_pointHit[i] = false;
    return true;
}
