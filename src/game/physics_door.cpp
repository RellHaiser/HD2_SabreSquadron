// Physics doors, gates, fences and trees — see physics.h.
#include <algorithm>
#include <cstring>

#include "common/c_str.h"
#include "game/game.h"
#include "game/pending.h"
#include "game/physics.h"
#include "game/physics_internal.h"
#include "game/resource_cache.h"
#include "game/world.h"

using namespace physics;

namespace {

// Opening beyond this no longer blocks the way (the collision box is removed).
constexpr float WIDE_OPEN_ANGLE = 1.2566371f;   // 72 degrees
// Rattle of a door that is walked into: about one degree, quickly.
constexpr float RATTLE_ANGLE = 0.0172787607f;
constexpr float RATTLE_SPEED = 12.0f;
constexpr float RATTLE_DAMPING = 0.6f;

} // namespace

C_PhysDoor::C_PhysDoor(const S_PhysicsObjectDesc &desc, S_PhysicsHandle *handle)
    : C_PhysObject(desc.frame, handle)
{
    m_type = PHYS_DOOR;
    if (!m_frame)
        return;

    const I3D_bound_volume bound = ModelBound(m_frame);
    InitBoxVolume(m_volume, m_matrix, m_invMatrix, bound,
                  COLLISION_SHAPE_BOX | (desc.shootThrough ? COLLISION_VOLUME_SHOOT_THROUGH : 0),
                  COLLISION_TYPE_DOOR);

    // Bottom center and rotation axis in world space; the top corners (local) are
    // the points checked against the world while the leaf turns.
    const S_vector top = bound.bbox.max * m_frame->GetMatrix();
    const S_vector bottom = bound.bbox.min * m_frame->GetMatrix();
    m_axis = S_vector(top.x - bottom.x, 0.0f, top.z - bottom.z);
    m_center = S_vector((top.x + bottom.x) * 0.5f, bottom.y, (top.z + bottom.z) * 0.5f);
    m_edge1 = top;
    m_edge2 = bottom;
    m_edge2.y = m_edge1.y;
    m_edge1 *= m_invMatrix;
    m_edge2 *= m_invMatrix;

    m_moving = false;
    m_freeSwing = desc.door.freeSwing;
    m_tree = desc.door.tree;
    m_closedRot = m_frame->GetRot();
    m_phase = 0.0f;
    m_initialRot = m_frame->GetRot();
    SetFreezeVolume(true);
    m_speed = 1.0f;
    m_openAngle = 1.0f;
    m_damping = 0.4f;
    m_breakable = desc.door.breakable;
    if (m_breakable) {
        // Vehicles drive through breakable fences.
        m_volume.flags |= COLLISION_VOLUME_SHOOT_THROUGH;
        g_temporaryActors.AddBreakable({m_frame, this, &m_volume});
    }
    g_collisionWorld.AddVolume(&m_volume);
    g_collisionWorld.MoveVolume(&m_volume);

    m_breakModel = desc.door.breakModel;
    m_angle = desc.door.angle;
    m_originalFrame = nullptr;
    m_brokenPart = nullptr;
    m_swingState = 0;
}

C_PhysDoor::~C_PhysDoor()
{
    for (std::size_t i = m_breakVolumes.size(); i-- > 0;) {
        if (S_BreakVolume *volume = m_breakVolumes[i]) {
            volume->volume.frame->Release();
            g_collisionWorld.RemoveVolume(&volume->volume);
            delete volume;
        }
    }
    m_breakVolumes.clear();

    if (m_frame) {
        m_frame->SetRot(m_initialRot);
        m_frame->Update();
    }
    if (m_originalFrame) {
        // Broken fence: the original frame comes back.
        m_originalFrame->SetRot(m_initialRot);
        m_originalFrame->SetOn(true);
        m_originalFrame->Update();
        m_originalFrame = nullptr;
        // (The fence frame took an extra reference in Break().)
        m_frame->Release();
    }
}

void C_PhysDoor::HideBrokenPart()
{
    if (m_brokenPart) {
        m_brokenPart->SetOn(false);
        m_brokenPart->Update();
    }
}

bool C_PhysDoor::MoveLeaf()
{
    if (!m_frame)
        return false;
    m_rot = m_phase == 1.0f ? m_openRot1 : m_closedRot.Slerp(m_openRot1, m_amplitude * m_phase, true);

    const S_vector from1 = m_edge1 * m_frame->GetMatrix();
    const S_vector from2 = m_edge2 * m_frame->GetMatrix();
    m_frame->SetRot(m_rot);
    m_frame->Update();
    const S_vector to1 = m_edge1 * m_frame->GetMatrix();
    const S_vector to2 = m_edge2 * m_frame->GetMatrix();

    // Did the top corners sweep through anything (other doors excluded)?
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    const bool hit1 = CollisionRayTest(from1, to1 - from1, nullptr, nullptr, 0xFFFFFFFF, COLLISION_TYPE_DOOR) != nullptr;
    const bool hit2 = CollisionRayTest(from2, to2 - from2, nullptr, nullptr, 0xFFFFFFFF, COLLISION_TYPE_DOOR) != nullptr;
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    if (!hit1 && !hit2)
        return true;

    // Blocked: back off a little (the frame takes it with the next SyncFrame()).
    m_rot = m_closedRot.Slerp(m_openRot1, (m_phase - 0.01f) * m_amplitude, true);
    return false;
}

bool C_PhysDoor::Tick(float dt)
{
    const float step = dt * m_speed;
    if (!m_rattling) {
        // Opening (kicked): once, up to the opening angle.
        const float phase = m_phase + step;
        m_phase = std::min(phase, 1.0f);
        return MoveLeaf() && phase <= 1.0f;
    }

    // Rattling (pushed): quarter swings around the closed rotation, each return
    // keeping m_damping of the amplitude.
    const S_quat *target;
    bool shortestPath;
    switch (m_swingState) {
    case 0:
        return false;
    case 1:
    case 3: {
        const S_quat &open = m_swingState == 1 ? m_openRot1 : m_openRot2;
        m_phase += step;
        if (m_phase > 1.0f) {
            m_phase = 1.0f;
            m_swingState += 1;
        }
        if (m_phase == 1.0f) {
            m_rot = open;
            return true;
        }
        target = &open;
        shortestPath = true;
        break;
    }
    case 2:
    case 4:
        target = m_swingState == 2 ? &m_openRot1 : &m_openRot2;
        m_phase -= step;
        if (m_phase < 0.0f) {
            m_amplitude *= m_damping;
            m_phase = 0.0f;
            m_swingState = m_swingState == 2 ? 3 : 1;
            if (m_amplitude < 0.1f) {
                m_moving = false;
                return false;
            }
        }
        shortestPath = false;
        break;
    default:
        return true;
    }
    m_rot = m_closedRot.Slerp(*target, m_amplitude * m_phase, shortestPath);
    return true;
}

bool C_PhysDoor::Hit(int kind, const S_vector *dir, void *extra)
{
    if (kind == PHYS_HIT_BREAK) {
        if (m_breakable) {
            m_breakable = false;
            Break(dir);
        }
        return true;
    }
    if (m_moving || m_phase != 0.0f)
        return false;

    m_hitDir = *dir;
    if (!m_frame)
        return false;
    if (!m_freeSwing) {
        // Turn around the frame's x axis, away from the hit.
        m_axis = m_frame->GetMatrix()(0);
        if (dir->z * m_axis.x - m_axis.z * dir->x > 0.0f)
            m_axis = -m_axis;
    } else {
        // Tip over around the horizontal axis across the hit.
        m_axis = S_vector(-dir->z, 0.0f, dir->x);
    }
    m_axis.Normalize();
    m_closedRot = m_frame->GetRot();
    m_phase = 0.0f;
    m_moving = true;
    m_amplitude = 1.0f;

    if (kind == PHYS_HIT_PUSH) {
        if (!m_tree) {
            SetFreezeVolume(true);
            m_openAngle = RATTLE_ANGLE;
            m_rattling = true;
            m_speed = RATTLE_SPEED;
            m_damping = RATTLE_DAMPING;
            m_swingState = 1;
        } else {
            m_moving = false;
        }
    } else {
        m_rattling = false;
        SetFreezeVolume(false);
        m_openAngle = m_angle;
        if (m_angle > WIDE_OPEN_ANGLE)
            SetVolumeActive(false);
        m_speed = 1.0f;
        m_damping = 0.9f;
        if (m_tree) {
            switch (KnockOverTree(dir, extra)) {
            case TREE_SWAPPED:
                return true;            // (this object has been deleted)
            case TREE_FAILED:
                return false;
            case TREE_NOT_SWAPPED:
                break;
            }
        }
    }

    // Rotations opened to either side.
    m_openRot1 = m_closedRot;
    m_openRot2 = m_closedRot;
    S_quat turn;
    turn.Make(m_axis, m_openAngle);
    m_openRot1 = m_openRot1.RotateByMatrix(turn.RotationMatrix());
    turn.Make(m_axis, -m_openAngle);
    m_openRot2 = m_openRot2.RotateByMatrix(turn.RotationMatrix());
    return true;
}

C_PhysDoor::E_TreeResult C_PhysDoor::KnockOverTree(const S_vector *dir, void *extra)
{
    I3D_frame *owner = m_frame->GetOwnerModel();
    if (!owner || owner->GetType() != FRAME_MODEL)
        return TREE_NOT_SWAPPED;
    auto *damaged = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (!damaged)
        return TREE_FAILED;

    // "d<model file name without directory and extension>"
    char base[0x100];
    const char *fileName = static_cast<I3D_model *>(owner)->GetFileName();
    if (const char *slash = std::strrchr(fileName, '\\'))
        fileName = slash + 1;
    std::strncpy(base, fileName, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    if (char *dot = std::strrchr(base, '.'))
        *dot = '\0';
    const C_fstr damagedName("d%s", base);
    if (g_modelCache.OpenInto(damaged, damagedName) != I3D_OK) {
        damaged->Release();     // (leaked by the original)
        return TREE_NOT_SWAPPED;
    }

    // The damaged model takes the place of the owner model.
    damaged->LinkTo(owner->GetParent(), 0);
    damaged->SetPosInline(owner->GetPos());
    damaged->SetRot(owner->GetRot());
    damaged->SetScale(owner->GetScale());
    damaged->SetName(owner->GetName());
    damaged->SetOn(true);
    damaged->Update();
    owner->LinkTo(nullptr, 0);
    I3D_scene *scene = GetGame()->GetScene();
    if (!scene)
        return TREE_FAILED;     // (the original keeps the new model)
    if (scene->RemoveFrame(owner))
        owner->Release();
    scene->AddFrame(damaged);

    // Its version of this part falls over in the hit direction.
    if (I3D_frame *part = damaged->FindChildFrame(m_frame->GetShortName(), 0xFFFF)) {
        S_PhysicsObjectDesc desc;
        desc.frame = part->GetChild(0);
        desc.type = PHYS_DOOR;
        desc.shootThrough = true;
        desc.door.freeSwing = true;
        desc.radius = -1.0f;
        const T_PhysicsObject fallen = GetPhysicsModule()->CreateObject(desc);
        GetPhysicsModule()->Hit(fallen, PHYS_HIT_KICK, dir, extra);
    }
    damaged->Release();
    GetPhysicsModule()->DestroyObject(&m_handle);   // deletes this object
    return TREE_SWAPPED;
}

void C_PhysDoor::Break(const S_vector *dir)
{
    SetVolumeActive(false);
    auto *model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (!model)
        return;
    // (The original inlines the model cache with its own "(3ds filter)" messages.)
    if (g_modelCache.OpenInto(model, m_breakModel) != I3D_OK) {
        model->Release();
        return;
    }

    I3D_frame *fence = model->FindChildFrame("plot", 0xFFFF);  // fence with the hole
    I3D_frame *piece = model->FindChildFrame("dira", 0xFFFF);  // the piece that falls out
    if (piece && fence) {
        I3D_frame *child = fence->NumChildren() > 0 ? fence->GetChild(0) : nullptr;
        model->SetPos(m_frame->GetWorldPos());
        model->SetDir(m_frame->GetMatrix()(2).Normalized(), 0.0f);
        model->Update();

        // Move the parts out of the model into the scene, beside this fence.
        piece->AddRef();
        fence->AddRef();
        model->RemoveModelFrame(piece);
        model->RemoveModelFrame(fence);
        if (child) {
            child->AddRef();
            model->RemoveModelFrame(child);
        }
        piece->LinkTo(m_frame->GetParent(), I3DLINK_KEEP_WORLD);
        fence->LinkTo(m_frame->GetParent(), I3DLINK_KEEP_WORLD);
        if (I3D_scene *scene = GetGame()->GetScene()) {
            scene->AddFrame(piece);
            scene->AddFrame(fence);
            if (child) {
                scene->AddFrame(child);
                child->Release();
            }
            piece->Release();
        } else {
            fence->AddRef();
        }

        // This object continues as the fence with the hole.
        if (m_frame) {
            SetFramePhysicsObject(m_frame, g_noPhysicsObject);
            m_frame->SetOn(false);
            m_frame->Update();
            m_originalFrame = m_frame;
            m_frame->Release();
            m_frame = fence;
            m_closedRot = m_frame->GetRot();
            SetFramePhysicsObject(m_frame, m_handle);
            AddBreakVolumes(model);
        }

        // The piece flies off.
        S_PhysicsObjectDesc desc;
        desc.frame = piece;
        desc.type = PHYS_DOOR;
        desc.shootThrough = true;
        desc.radius = -1.0f;
        const T_PhysicsObject flying = GetPhysicsModule()->CreateObject(desc);
        GetPhysicsModule()->Hit(flying, PHYS_HIT_KICK, dir, nullptr);
        m_brokenPart = piece;
        piece->Release();
    }
    model->Release();
}

void C_PhysDoor::AddBreakVolumes(I3D_frame *model)
{
    for (int i = 0;; ++i) {
        I3D_frame *frame = model->FindChildFrame(C_fstr("VOLB0%d", i), 0xFFFF);
        if (!frame)
            break;
        frame->AddRef();
        static_cast<I3D_model *>(model)->RemoveModelFrame(frame);
        frame->LinkTo(m_frame->GetParent(), I3DLINK_KEEP_WORLD);

        auto *box = new S_BreakVolume;
        frame->AddRef();
        I3D_bound_volume bound = {};
        if (frame->GetType() != FRAME_DUMMY) {
            bound = HierarchyBound(frame);
        } else {
            bound.bbox = static_cast<I3D_dummy *>(frame)->GetBBox();
            bound.center = (bound.bbox.max + bound.bbox.min) * 0.5f;
            bound.radius = (bound.bbox.max.y - bound.bbox.min.y) * 0.5f;
        }

        box->volume = S_CollisionVolume();
        box->volume.flags = COLLISION_SHAPE_BOX;
        box->volume.frame = frame;
        box->volume.type = COLLISION_TYPE_DOOR;
        // The box is in the frame's scaled space; the matrices carry no scale.
        const S_vector scale = frame->GetScale();
        bound.bbox.min = S_vector(bound.bbox.min.x * scale.x, bound.bbox.min.y * scale.y, bound.bbox.min.z * scale.z);
        bound.bbox.max = S_vector(bound.bbox.max.x * scale.x, bound.bbox.max.y * scale.y, bound.bbox.max.z * scale.z);
        box->volume.center = bound.center * frame->GetMatrix();
        box->volume.bbox = bound.bbox;
        box->matrix = frame->GetMatrix();
        box->matrix(0) *= 1.0f / scale.x;
        box->matrix(1) *= 1.0f / scale.y;
        box->matrix(2) *= 1.0f / scale.z;
        box->volume.matrix = &box->matrix;
        box->invMatrix.Inverse(box->matrix);
        box->volume.invMatrix = &box->invMatrix;
        const S_vector worldScale = frame->GetWorldScale();
        box->volume.radius = std::max(worldScale.x, std::max(worldScale.y, worldScale.z)) * bound.radius;
        box->volume.center = bound.center * frame->GetMatrix();
        g_collisionWorld.AddVolume(&box->volume);
        g_collisionWorld.MoveVolume(&box->volume);

        m_breakVolumes.push_back(box);
        frame->Release();
        SetFramePhysicsObject(frame, m_handle);
    }
}
