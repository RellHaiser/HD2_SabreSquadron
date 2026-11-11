// Physics objects — see physics.h. Doors are in physics_door.cpp, boxes in
// physics_box.cpp.
#include "game/physics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/game.h"
#include "game/pending.h"
#include "game/physics_internal.h"
#include "game/world.h"
#include "net/bitstream.h"

S_PhysicsHandle g_noPhysicsHandle;                          // {-1, false} (0x0040fae0)
T_PhysicsObject const g_noPhysicsObject = &g_noPhysicsHandle;

C_PhysicsModule g_physicsModule(MODULE_PHYSICS, "Physics");

C_PhysicsModuleBase *GetPhysicsModule()
{
    return &g_physicsModule;
}

// ---------------------------------------------------------------------------
// Helpers shared by the physics files
// ---------------------------------------------------------------------------

namespace physics {

void SetFramePhysicsObject(I3D_frame *frame, T_PhysicsObject object)
{
    if (S_FrameData *data = frame->GetData()) {
        data->physicsObject = object;
    } else if (object) {
        // (The original leaves the other fields of the new data block uninitialized.)
        S_FrameData newData = {};
        newData.physicsObject = object;
        SetFrameData(frame, newData);
    }
}

void ComputeBoundSphere(I3D_bound_volume &bound)            // 0x00406c70
{
    if (bound.bbox.min.x < bound.bbox.max.x) {
        const S_vector half = (bound.bbox.max - bound.bbox.min) * 0.5f;
        bound.center = half + bound.bbox.min;
        bound.radius = std::sqrt(half.Square());
    } else {
        bound.radius = 0.0f;
    }
}

I3D_bound_volume HierarchyBound(I3D_frame *frame)
{
    I3D_bound_volume bound = {};
    I3D_bbox box;
    bound.bbox = *I3DGetDriver()->GetHierarchyBBox(box, frame);
    ComputeBoundSphere(bound);
    return bound;
}

I3D_bound_volume ModelBound(I3D_frame *frame)
{
    if (frame->GetType() == FRAME_VISUAL)
        return static_cast<I3D_visual *>(frame)->GetLocalBound();
    return HierarchyBound(frame);
}

} // namespace physics

using namespace physics;

// ---------------------------------------------------------------------------
// C_PhysObject
// ---------------------------------------------------------------------------

C_PhysObject::C_PhysObject(I3D_frame *frame, S_PhysicsHandle *handle)
{
    if (!frame)
        return;
    if (I3D_scene *scene = GetGame()->GetScene())
        frame->LinkTo(scene->GetPrimarySector(), I3DLINK_KEEP_WORLD);
    frame->Update();
    m_frame = frame;
    frame->AddRef();
    m_handle = handle;
    m_pos = m_frame->GetWorldPos();
    m_posW = m_frame->GetMatrix().m[3][3];
    m_rot.Make(m_frame->GetMatrix());
    m_volumeActive = true;      // (the subclasses add the volume or clear the flag)
}

C_PhysObject::~C_PhysObject()
{
    if (m_volumeActive)
        SetVolumeActive(false);
    CallCallback(PHYS_CALLBACK_DELETED);
    if (m_frame) {
        SetFramePhysicsObject(m_frame, g_noPhysicsObject);
        m_frame->Release();
    }
}

void C_PhysObject::SyncFrame()
{
    if (!m_frame)
        return;
    m_frame->SetPos(m_pos);
    m_frame->SetRot(m_rot);
    if (I3D_scene *scene = GetGame()->GetScene())
        scene->UpdateFrameSector(m_frame, m_frame->GetWorldPos());
    m_frame->Update();

    if (m_volumeActive && !m_freezeVolume) {
        m_matrix = m_frame->GetMatrix();
        m_invMatrix.Inverse(m_frame->GetMatrix());
        const I3D_bound_volume &bound = static_cast<I3D_visual *>(m_frame)->GetLocalBound();
        m_volume.center = bound.center * m_frame->GetMatrix();
        g_collisionWorld.MoveVolume(&m_volume);
    }
}

void C_PhysObject::SetVolumeActive(bool on)
{
    if (m_volumeActive == on)
        return;
    m_volumeActive = on;
    if (on) {
        g_collisionWorld.AddVolume(&m_volume);
        g_collisionWorld.MoveVolume(&m_volume);
    } else {
        g_collisionWorld.RemoveVolume(&m_volume);
    }
}

void C_PhysObject::ApplyGravity(float dt)
{
    m_velocity.y -= dt * 9.81f;
}

void C_PhysObject::Rotate(float dt)
{
    const float speed = m_angularVelocity.Magnitude();
    m_angularVelocity.Normalize();
    m_angularVelocity *= (1.0f - dt) * speed;
    const S_matrix spin = RotateXMatrix(m_angularVelocity.x * dt) * RotateYMatrix(m_angularVelocity.y * dt) *
                          RotateZMatrix(m_angularVelocity.z * dt);
    m_rot = m_rot.RotateByMatrix(spin);
}

void C_PhysObject::ApplyImpulse(const S_vector &impulse, const S_vector &point, float factor)
{
    if (!m_frame)
        return;
    const float strength = impulse.Magnitude();
    const float mass = m_mass == 0.0f ? 1.0f : m_mass;
    m_velocity += impulse.Normalized() * std::sqrt(strength * strength * factor / mass);

    // Spin from the push's lever arm (in the frame's space).
    S_matrix toLocal;
    toLocal.Inverse(m_frame->GetMatrix());
    const S_vector localPoint = point * toLocal;
    const S_vector localTip = (m_frame->GetWorldPos() + impulse) * toLocal;
    m_angularVelocity += localTip.Cross(localPoint) * factor;
    m_state = 0;
}

void C_PhysObject::Bounce(S_vector &velocity, const S_vector &normal)
{
    const float speed = velocity.Magnitude();
    velocity.Normalize();
    const S_vector side = normal.Cross(velocity) * 2.0f;
    velocity = (side.Cross(normal) - velocity) * speed;   // mirrored at the surface
    CallCallback(PHYS_CALLBACK_BOUNCED);
}

void C_PhysObject::AddBoxVolume(S_CollisionVolume &volume, S_matrix &matrix, S_matrix &invMatrix,
                                const I3D_bound_volume &bound, uint32_t flags, uint32_t type)
{
    InitBoxVolume(volume, matrix, invMatrix, bound, flags, type);
    g_collisionWorld.AddVolume(&volume);
    g_collisionWorld.MoveVolume(&volume);
}

void C_PhysObject::InitBoxVolume(S_CollisionVolume &volume, S_matrix &matrix, S_matrix &invMatrix,
                                 const I3D_bound_volume &bound, uint32_t flags, uint32_t type)
{
    volume = S_CollisionVolume();
    volume.radius = bound.radius;
    volume.flags = flags;
    volume.bbox = bound.bbox;
    matrix = m_frame->GetMatrix();
    volume.matrix = &matrix;
    invMatrix.Inverse(m_frame->GetMatrix());
    volume.invMatrix = &invMatrix;
    volume.frame = m_frame;
    volume.type = type;
    volume.center = bound.center * m_frame->GetMatrix();
}

// ---------------------------------------------------------------------------
// C_PhysSimpleObject
// ---------------------------------------------------------------------------

C_PhysSimpleObject::C_PhysSimpleObject(I3D_frame *frame, S_PhysicsHandle *handle) : C_PhysObject(frame, handle)
{
    m_type = PHYS_SIMPLE;
    m_volumeActive = false;
}

bool C_PhysSimpleObject::Tick(float dt)
{
    if (!m_frame)
        return false;
    Rotate(dt);
    const S_vector start = m_frame->GetWorldPos();
    const S_vector move = m_velocity * dt;
    S_vector hitPos, hitNormal;
    const S_CollisionVolume *hit = CollisionRayTest(start, move, &hitPos, &hitNormal, 0, 0);
    if (hit && (m_collideWithBoxes || !(hit->flags & COLLISION_FLAG_DYNAMIC))) {
        if (m_velocity.Magnitude() >= 1.0f) {
            Bounce(m_velocity, hitNormal);
            m_velocity *= 0.4f;
            return true;
        }
        m_pos = hitPos;
        return false;
    }
    m_pos += move;
    ApplyGravity(dt);
    return true;
}

// ---------------------------------------------------------------------------
// C_PhysFallingObject
// ---------------------------------------------------------------------------

C_PhysFallingObject::C_PhysFallingObject(I3D_frame *frame, S_PhysicsHandle *handle)
    : C_PhysObject(frame, handle)
{
    m_type = PHYS_FALLING;
    m_volumeActive = false;
    // Small pieces slide along the ground below them instead of tumbling.
    if (frame->GetBound().radius < 0.5f)
        m_sliding = true;
    m_groundChecked = false;
    if (C_GEffectsModule *effects = GetGEffects())
        effects->FadeOutModel(m_frame, 5000, false);
}

bool C_PhysFallingObject::Tick(float dt)
{
    if (!m_frame)
        return false;
    Rotate(dt);
    const S_vector start = m_frame->GetWorldPos();
    const S_vector move = m_velocity * dt;
    S_vector hitPos, hitNormal;
    bool hit = false;
    bool hitBox = false;

    if (!m_sliding) {
        const S_CollisionVolume *volume = CollisionRayTest(start, move, &hitPos, &hitNormal, 0, 0);
        hit = volume != nullptr;
        hitBox = hit && (volume->flags & COLLISION_FLAG_DYNAMIC);
    } else {
        if (move.y < 0.0f && !m_groundChecked) {
            m_groundChecked = true;
            S_vector groundPos;
            if (!CollisionRayTest(start, S_vector(0.0f, -4.0f, 0.0f), &groundPos, nullptr, 0, 0)) {
                // Nothing below: it is gone.
                m_velocity.Zero();
                m_pos += move;
                m_resting = true;
                return false;
            }
            m_groundY = groundPos.y;
        }
        if (move.y > 0.0f)
            m_groundChecked = false;
        if (move.y + m_pos.y <= m_groundY) {
            m_groundChecked = false;
            hitPos = S_vector(m_pos.x + move.x, m_groundY + 0.0001f, m_pos.z + move.z);
            hitNormal = S_vector(0.0f, 1.0f, 0.0f);
            hit = true;
        }
    }

    if (!hit || (!m_collideWithBoxes && hitBox)) {
        m_pos += move;
        ApplyGravity(dt);
        return true;
    }
    if (m_velocity.Magnitude() < 1.0f) {
        m_velocity.Zero();
        m_resting = true;
        m_pos = hitPos;
        return false;
    }
    Bounce(m_velocity, hitNormal);
    float moveLength = move.Magnitude();
    if (moveLength == 0.0f)
        moveLength = 1.0f;
    const float travelled = (hitPos - start).Magnitude();
    m_pos = hitPos;
    m_velocity *= travelled / moveLength * 0.4f;
    return true;
}

// ---------------------------------------------------------------------------
// C_PhysCan
// ---------------------------------------------------------------------------

C_PhysCan::C_PhysCan(I3D_frame *frame, float radius, S_PhysicsHandle *handle) : C_PhysObject(frame, handle)
{
    m_type = PHYS_CAN;
    if (!m_frame)
        return;
    const I3D_bound_volume bound = HierarchyBound(frame);
    m_radius = radius >= 0.0f ? radius : bound.radius;

    m_volume = S_CollisionVolume();
    m_volume.flags = COLLISION_SHAPE_SPHERE;
    m_volume.radius = m_radius;
    m_volume.bbox = bound.bbox;
    m_matrix = m_frame->GetMatrix();
    m_volume.matrix = &m_matrix;
    m_invMatrix.Inverse(m_frame->GetMatrix());
    m_volume.invMatrix = &m_invMatrix;
    m_volume.frame = m_frame;
    m_volume.type = 0;
    m_volume.center = m_frame->GetWorldPos();
    g_collisionWorld.AddVolume(&m_volume);
    g_collisionWorld.MoveVolume(&m_volume);
}

bool C_PhysCan::Tick(float dt)
{
    if (!m_frame)
        return false;
    Rotate(dt);
    const S_vector start = m_frame->GetWorldPos();
    const S_vector move = m_velocity * dt;
    // (Starts a millimetre back so a resting can still finds the ground.)
    const S_vector from = start - move.Normalized() * 0.001f;
    S_vector hitPos, hitNormal;
    const S_CollisionVolume *hit = CollisionSphereTest(from, move, m_radius, &hitPos, &hitNormal, 0, 0);
    if (hit && (m_collideWithBoxes || !(hit->flags & COLLISION_FLAG_DYNAMIC))) {
        if (m_velocity.Magnitude() >= 1.0f) {
            Bounce(m_velocity, hitNormal);
            const float travelled = (hitPos - start).Magnitude();
            const float moveLength = move.Magnitude();
            m_pos = hitPos;
            // (Not guarded in the original.)
            if (moveLength != 0.0f)
                m_velocity *= travelled / moveLength * 0.4f;
            return true;
        }
        m_velocity.Zero();
        m_pos = hitPos;
        return false;
    }
    m_pos += move;
    ApplyGravity(dt);
    return true;
}

void C_PhysCan::ApplyImpulse(const S_vector &impulse, const S_vector &point, float factor)
{
    if (!m_frame)
        return;
    m_velocity += impulse.Normalized() * 15.0f;

    S_matrix toLocal;
    toLocal.Inverse(m_frame->GetMatrix());
    const S_vector localPoint = point * toLocal;
    const S_vector localTip = (m_frame->GetWorldPos() + impulse) * toLocal;
    m_angularVelocity += localTip.Cross(localPoint);
    m_state = 0;
}

// ---------------------------------------------------------------------------
// C_PhysSphere (not created by the game)
// ---------------------------------------------------------------------------

C_PhysSphere::C_PhysSphere(I3D_frame *frame, float radius, S_PhysicsHandle *handle) : C_PhysObject(frame, handle)
{
    m_type = PHYS_SPHERE;
    if (!m_frame)
        return;
    const I3D_bound_volume bound = HierarchyBound(frame);
    m_radius = radius >= 0.0f ? radius : bound.radius;

    // (Unlike a can's, the volume is the model's bound, whatever the ball's radius: 0x0040f7ff..0x0040f9db.)
    m_volume = S_CollisionVolume();
    m_volume.flags = COLLISION_SHAPE_SPHERE;
    m_volume.bbox = bound.bbox;
    m_matrix = m_frame->GetMatrix();
    m_volume.matrix = &m_matrix;
    m_invMatrix.Inverse(m_frame->GetMatrix());
    m_volume.invMatrix = &m_invMatrix;
    m_volume.frame = m_frame;
    m_volume.type = 0;
    const S_vector scale = m_frame->GetWorldScale();
    m_volume.radius = std::max(scale.x, std::max(scale.y, scale.z)) * bound.radius;
    m_volume.center = bound.center * m_frame->GetMatrix();
    g_collisionWorld.AddVolume(&m_volume);
    g_collisionWorld.MoveVolume(&m_volume);
}

bool C_PhysSphere::Tick(float dt)
{
    if (!m_frame)
        return false;
    S_vector pos = m_frame->GetWorldPos();
    S_vector move = m_velocity * dt;

    // Ray from the center that reaches the surface of the ball.
    const S_vector dir = move.Normalized();
    const float reach = move.Magnitude() + m_radius;
    S_vector ray = dir * reach;
    const S_vector from = pos - ray.Normalized() * 0.001f;
    S_vector hitPos, hitNormal;
    const S_CollisionVolume *hit = CollisionRayTest(from, ray, &hitPos, &hitNormal, 0, 0);
    const S_vector surface = ray.Normalized() * m_radius;
    hitPos -= surface;      // ball center at the contact
    move = ray - surface;

    if (!hit || (!m_collideWithBoxes && (hit->flags & COLLISION_FLAG_DYNAMIC))) {
        pos += move;
    } else if (m_velocity.Magnitude() < 1.0f) {
        pos = hitPos;
    } else {
        // Bounce and use the rest of the time step after the contact.
        Bounce(m_velocity, hitNormal);
        const float travelled = (hitPos - pos).Magnitude();
        const float speed = m_velocity.Magnitude();
        m_velocity.y = travelled / speed * 9.81f + (m_velocity.y - dt * 9.81f);
        const float rest = dt - travelled / speed;
        m_velocity *= 0.4f;
        m_velocity.y -= rest * 9.81f;
        move = m_velocity * rest;
    }
    m_frame->SetPos(pos);
    m_frame->Update();
    m_pos = pos;
    const I3D_bound_volume bound = m_frame->GetBound();
    m_invMatrix.Inverse(m_frame->GetMatrix());
    // (As the original, 0x0040f650: the frame's local bound center and radius copied into the
    // volume's center and the word after it, not moved into world space. Balls are never made.)
    m_volume.center = bound.center;
    m_volume.centerW = bound.radius;
    g_collisionWorld.MoveVolume(&m_volume);
    return true;
}

// ---------------------------------------------------------------------------
// C_PhysSwingingObject
// ---------------------------------------------------------------------------

C_PhysSwingingObject::C_PhysSwingingObject(I3D_frame *frame, S_PhysicsHandle *handle, I3D_frame *parent)
    : C_PhysObject(frame, handle), m_parent(parent)
{
    m_type = PHYS_SWINGING;
    if (!m_frame)
        return;
    const I3D_bound_volume bound = ModelBound(m_frame);
    AddBoxVolume(m_volume, m_matrix, m_invMatrix, bound, COLLISION_SHAPE_BOX, COLLISION_TYPE_LIGHT_OBJECT);

    // Second volume, half a unit lower.
    m_volume2 = S_CollisionVolume();
    m_volume2.flags = COLLISION_SHAPE_SPHERE;
    m_volume2.radius = bound.radius;
    m_volume2.bbox = bound.bbox;
    m_matrix2 = m_frame->GetMatrix();
    m_volume2.matrix = &m_matrix2;
    m_invMatrix2.Inverse(m_frame->GetMatrix());
    m_volume2.invMatrix = &m_invMatrix2;
    m_volume2.frame = m_frame;
    m_volume2.type = 0;
    m_volume2.center = bound.center * m_frame->GetMatrix();
    m_volume2.center.y -= 0.5f;
    g_collisionWorld.AddVolume(&m_volume2);
    g_collisionWorld.MoveVolume(&m_volume2);
    m_swinging = false;
}

C_PhysSwingingObject::~C_PhysSwingingObject()
{
    if (m_volumeActive)
        g_collisionWorld.RemoveVolume(&m_volume2);
    if (m_frame) {
        m_frame->LinkTo(m_parent, I3DLINK_KEEP_WORLD);
        m_frame->Update();
    }
}

bool C_PhysSwingingObject::Tick(float dt)
{
    // Quarter swings: out to side 1 and back, out to side 2 and back; every return
    // keeps 40% of the amplitude.
    const float step = dt * 2.5f;
    const S_quat *target;
    bool shortestPath;
    bool settled = false;
    switch (m_swingState) {
    case 0:
        m_phase += step;
        if (m_phase > 1.0f) {
            m_phase = 1.0f;
            m_swingState = 1;
        }
        shortestPath = true;
        target = &m_swingRot1;
        break;
    case 1:
        m_phase -= step;
        if (m_phase < 0.0f) {
            m_amplitude *= 0.4f;
            m_phase = 0.0f;
            m_swingState = 2;
            settled = m_amplitude < 0.1f;
        }
        shortestPath = false;
        target = &m_swingRot1;
        break;
    case 2:
        m_phase += step;
        if (m_phase > 1.0f) {
            m_phase = 1.0f;
            m_swingState = 3;
        }
        shortestPath = true;
        target = &m_swingRot2;
        break;
    case 3:
        m_phase -= step;
        if (m_phase < 0.0f) {
            m_amplitude *= 0.4f;
            m_phase = 0.0f;
            m_swingState = 0;
            settled = m_amplitude < 0.1f;
        }
        shortestPath = false;
        target = &m_swingRot2;
        break;
    default:
        return true;
    }

    if (settled) {
        // Blocks again.
        SetVolumeActive(true);
        g_collisionWorld.AddVolume(&m_volume2);
        g_collisionWorld.MoveVolume(&m_volume2);
        m_swinging = false;
        return false;
    }
    m_rot = m_restRot.Slerp(*target, std::sin(m_phase) * m_amplitude, shortestPath);
    return true;
}

bool C_PhysSwingingObject::Hit(int kind, const S_vector *dir, void *extra)
{
    if (m_swinging)
        return false;
    m_swinging = true;
    if (!m_frame)
        return false;

    m_restRot.Make(m_frame->GetMatrix());
    // Swings around the horizontal axis across the hit direction, 36 degrees.
    S_vector axis(-dir->z, 0.0f, dir->x);
    axis.Normalize();
    m_swingRot2 = m_restRot;
    m_swingRot1 = m_swingRot2;
    S_quat turn;
    turn.Make(axis, 0.62831855f);
    m_swingRot1 = m_swingRot1.RotateByMatrix(turn.RotationMatrix());
    turn.Make(axis, -0.62831855f);
    m_swingRot2 = m_swingRot2.RotateByMatrix(turn.RotationMatrix());

    SetVolumeActive(false);
    g_collisionWorld.RemoveVolume(&m_volume2);
    m_amplitude = 1.0f;
    m_swingState = 0;
    m_phase = 0.0f;
    return true;
}

// ---------------------------------------------------------------------------
// C_PhysicsModule
// ---------------------------------------------------------------------------

C_PhysicsModule::C_PhysicsModule(int id, const char *name) : C_PhysicsModuleBase(id, name) {}

C_PhysicsModule::~C_PhysicsModule()
{
    DeleteAll();
}

T_PhysicsObject C_PhysicsModule::CreateObject(const S_PhysicsObjectDesc &desc)
{
    if (!desc.frame)
        return g_noPhysicsObject;

    // Reuse a free handle.
    std::size_t handleIndex = 0;
    S_PhysicsHandle *handle = nullptr;
    for (; handleIndex < m_handles.size(); ++handleIndex) {
        if (m_handles[handleIndex] && m_handles[handleIndex]->index == -1) {
            handle = m_handles[handleIndex];
            break;
        }
    }
    const bool newHandle = handleIndex == m_handles.size();
    if (newHandle)
        handle = new S_PhysicsHandle{0, false};

    C_PhysObject *object;
    switch (desc.type) {
    case PHYS_SIMPLE: object = new C_PhysSimpleObject(desc.frame, handle); break;
    case PHYS_SPHERE: object = new C_PhysSphere(desc.frame, desc.radius, handle); break;
    case PHYS_CAN: object = new C_PhysCan(desc.frame, desc.radius, handle); break;
    case PHYS_BOX: object = new C_PhysBox(desc.frame, handle); break;
    case PHYS_FALLING: object = new C_PhysFallingObject(desc.frame, handle); break;
    case PHYS_DOOR:
        object = new C_PhysDoor(desc, handle);
        m_doors.push_back(object);
        break;
    case PHYS_SWINGING: object = new C_PhysSwingingObject(desc.frame, handle, desc.frame->GetParent()); break;
    default:
        // (The original keeps a newly allocated handle here.)
        if (newHandle)
            delete handle;
        return g_noPhysicsObject;
    }

    object->SetCallback(desc.callback);
    object->SetMaterialId(desc.materialId);
    object->SetFlags(desc.unknown14, desc.collideWithBoxes);
    object->SetCallbackContext(desc.callbackContext);

    // First free slot of the list.
    std::vector<C_PhysObject *> &list = desc.startActive ? m_active : m_resting;
    handle->active = desc.startActive;
    auto slot = std::find(list.begin(), list.end(), nullptr);
    handle->index = static_cast<int>(slot - list.begin());
    if (slot == list.end())
        list.push_back(object);
    else
        *slot = object;

    if (newHandle)
        m_handles.push_back(handle);
    SetFramePhysicsObject(desc.frame, handle);
    if (desc.callback)
        desc.callback(PHYS_CALLBACK_CREATED, handle, desc.callbackContext);
    return handle;
}

bool C_PhysicsModule::DestroyObject(T_PhysicsObject *handle)
{
    if (m_handles.empty())
        return false;
    S_PhysicsHandle *h = *handle;
    if (h == g_noPhysicsObject || !h || h->index == -1)
        return false;

    std::vector<C_PhysObject *> &list = h->active ? m_active : m_resting;
    C_PhysObject *object = list[h->index];
    if (!object)
        return false;
    if (object->GetFrame())
        SetFramePhysicsObject(object->GetFrame(), g_noPhysicsObject);
    list[h->index] = nullptr;
    h->index = -1;
    *handle = g_noPhysicsObject;

    // Bug fix: the original clears the last door entry that is NOT this object
    // (inverted comparison), leaving a dangling pointer in the door list.
    auto door = std::find(m_doors.begin(), m_doors.end(), object);
    if (door != m_doors.end())
        *door = nullptr;
    delete object;
    return true;
}

C_PhysObject *C_PhysicsModule::Wake(S_PhysicsHandle *handle)
{
    int index = handle->index;
    if (!handle->active) {
        C_PhysObject *object = m_resting[index];
        if (!object)
            return nullptr;
        m_resting[index] = m_resting.back();
        if (C_PhysObject *moved = m_resting[index])
            moved->GetHandle()->index = index;
        m_resting.pop_back();
        m_active.push_back(object);
        index = static_cast<int>(m_active.size()) - 1;
        object->GetHandle()->index = index;
        object->GetHandle()->active = true;
    }
    return m_active[index];
}

void C_PhysicsModule::ApplyImpulse(T_PhysicsObject handle, const S_vector &impulse, const S_vector &point,
                                   float factor)
{
    if (!handle || handle->index == -1)
        return;
    if (C_PhysObject *object = Wake(handle)) {
        object->ApplyImpulse(impulse, point, factor);
        object->CallCallback(PHYS_CALLBACK_PUSHED);
    }
}

bool C_PhysicsModule::Hit(T_PhysicsObject handle, int kind, const S_vector *dir, void *extra)
{
    if (!handle || handle->index == -1)
        return false;
    C_PhysObject *object = Wake(handle);
    return object ? object->Hit(kind, dir, extra) : false;
}

template <class F> void C_PhysicsModule::StepObjects(int timeDelta, F selected)
{
    for (std::size_t i = m_active.size(); i-- > 0;) {
        C_PhysObject *object = m_active[i];
        if (!object || !selected(*object))
            continue;
        const bool moving = object->Tick(static_cast<float>(timeDelta) * 0.001f);
        object->SyncFrame();
        if (!moving)
            Deactivate(static_cast<int>(i), object->IsResting());
    }
}

void C_PhysicsModule::Tick(int timeDelta)
{
    if (!(g_gameRole & GAME_ROLE_SERVER))
        return;
    StepObjects(timeDelta, [](const C_PhysObject &object) {
        const int type = object.GetType();
        return type != PHYS_FALLING && type != PHYS_SWINGING &&
               (type != PHYS_DOOR || !(g_gameRole & GAME_ROLE_CLIENT));
    });
}

void C_PhysicsModule::Update(int timeDelta)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT))
        return;
    StepObjects(timeDelta, [](const C_PhysObject &object) {
        const int type = object.GetType();
        return type == PHYS_DOOR || type == PHYS_FALLING || type == PHYS_SWINGING;
    });
}

void C_PhysicsModule::Deactivate(int index, bool destroy)
{
    C_PhysObject *object = m_active[index];
    if (object->CallCallback(PHYS_CALLBACK_RESTING) == 1)
        destroy = true;

    m_resting.push_back(object);
    m_active[index] = m_active.back();
    if (C_PhysObject *moved = m_active[index]; moved && moved->GetHandle()->index != -1)
        moved->GetHandle()->index = index;
    m_active.pop_back();

    S_PhysicsHandle *handle = object->GetHandle();
    handle->index = static_cast<int>(m_resting.size()) - 1;
    handle->active = false;
    if (destroy)
        DestroyObject(&handle);
}

void C_PhysicsModule::DeleteAll()
{
    for (std::size_t i = m_active.size(); i-- > 0;) {
        delete m_active[i];
        m_active[i] = nullptr;
    }
    m_active.clear();
    for (std::size_t i = m_resting.size(); i-- > 0;) {
        delete m_resting[i];
        m_resting[i] = nullptr;
    }
    m_resting.clear();
    for (S_PhysicsHandle *&handle : m_handles) {
        delete handle;
        handle = nullptr;
    }
    m_handles.clear();
    // (Added: the original keeps the deleted doors until the next mission start.)
    m_doors.clear();
}

void C_PhysicsModule::StartMission(bool server)
{
    if (server || !(g_gameRole & GAME_ROLE_SERVER)) {
        DeleteAll();
        CreateSceneObjects();
    }
}

void C_PhysicsModule::StopMission(bool server)
{
    if (server || !(g_gameRole & GAME_ROLE_SERVER))
        DeleteAll();
}

void C_PhysicsModule::RestartMission(bool server)
{
    if (g_gameRole & GAME_ROLE_CLIENT)
        StopMission(false);
    if (g_gameRole & GAME_ROLE_SERVER)
        StopMission(true);
    g_temporaryActors.ClearBreakables();
    if (g_gameRole & GAME_ROLE_SERVER)
        StartMission(true);
    if (g_gameRole & GAME_ROLE_CLIENT)
        StartMission(false);
}

uint32_t __stdcall CreatePhysicsObjectProc(I3D_frame *frame, void *context)   // 0x00410b40
{
    const char *props = frame->GetUserProps();
    if (!*props)
        return 0;
    // (Bounded copy; the original copies the whole text into a 1 KB buffer.)
    char text[0x400];
    std::strncpy(text, props, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    _strlwr(text);
    if (!std::strstr(text, "phobj") || frame->GetType() == FRAME_MODEL)
        return 0;

    S_PhysicsObjectDesc desc;
    desc.frame = frame;
    desc.type = PHYS_DOOR;
    desc.radius = -1.0f;
    desc.shootThrough = !std::strstr(text, "sightstop");
    if (std::strstr(text, "can")) {
        desc.type = PHYS_CAN;
        desc.startActive = true;
    } else {
        if (std::strstr(text, "cross"))
            desc.type = PHYS_SWINGING;
        desc.door.freeSwing = std::strstr(text, "free") || std::strstr(text, "axis");
        desc.door.tree = std::strstr(text, "tree") != nullptr;
        const char *breakWord = std::strstr(text, "break");
        desc.door.breakable = breakWord != nullptr;
        if (breakWord) {
            char name[256];
            const int read = std::sscanf(breakWord + std::strlen("break"), "%255s", name);
            if (read == 0 || read == EOF) {
                desc.door.breakModel = "plotdira";
            } else {
                if (char *dot = std::strchr(name, '.'))
                    *dot = '\0';
                desc.door.breakModel = name;
            }
        }
        if (const char *angle = std::strstr(text, "angle"))
            desc.door.angle = static_cast<float>(std::atof(angle + std::strlen("angle"))) * 0.008726646f;
    }
    g_physicsModule.CreateObject(desc);
    return 0;
}

void C_PhysicsModule::CreateSceneObjects()
{
    m_doors.clear();
    GetGame()->GetScene()->EnumFrames(CreatePhysicsObjectProc, &m_doors, 0xFFFF, 0);
}

void C_PhysicsModule::WriteState(C_BitStream *msg)
{
    if (m_doors.empty())
        return;
    auto *net = static_cast<C_NetMessage *>(msg);
    for (std::size_t i = m_doors.size(); i-- > 0;) {
        const auto *door = static_cast<const C_PhysDoor *>(m_doors[i]);
        if (door && door->IsOpening()) {
            msg->WriteBits(static_cast<uint32_t>(i), 16);
            net->WriteDirection(door->GetHitDir(), -1);
        }
    }
    msg->WriteBits(0xFFFFFFFF, 16);
}

void C_PhysicsModule::ReadState(C_BitStream *msg)
{
    if (m_doors.empty())
        return;
    auto *net = static_cast<C_NetMessage *>(msg);
    for (int index = static_cast<int16_t>(msg->ReadBits(16)); index != -1;
         index = static_cast<int16_t>(msg->ReadBits(16))) {
        S_vector dir;
        net->ReadDirection(dir, -1);
        // (Null check added: destroyed doors leave empty entries.)
        if (index >= 0 && static_cast<std::size_t>(index) < m_doors.size() && m_doors[index]) {
            int extra = 0;
            Hit(m_doors[index]->GetHandle(), PHYS_HIT_KICK, &dir, &extra);
        }
    }
}
