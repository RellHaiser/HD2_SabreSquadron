// Bicycles (C_Bike; original 0x005d8760..0x005db780).
#include "game/bike.h"

#include <cmath>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/checkpoints.h"
#include "game/frame_box_volume.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_movement.h"                        // AngleBetween2D
#include "game/pending.h"                               // GetGEffects
#include "game/resource_cache.h"                        // g_animationCache
#include "ls3d/i3d.h"
#include "script/script.h"

namespace {

// The saved game's chunks.
constexpr uint16_t CHUNK_STOPPED = 1000;
constexpr uint16_t CHUNK_RIDDEN = 2000;
constexpr uint16_t CHUNK_SPEED = 3000;
constexpr uint16_t CHUNK_PEDALS = 4000;
constexpr uint16_t CHUNK_RIDER = 5000;
constexpr uint16_t CHUNK_PATH = 6000;

constexpr uint32_t BIKE_VOLUME_TYPE = COLLISION_TYPE_PIKE;   // (the pike's type too)
constexpr int BIKE_SOUND_CHAPTER = 1;
constexpr float POINT_RADIUS_UNIT = 0.0625f;            // a checkpoint's radius byte, in m
constexpr float MIN_REACH = 3.0f;                       // m: a point is reached within at least this
constexpr float KMH_TO_MS = 0.28f;
constexpr float ACCELERATION = 0.001f;                  // m/s a ms (also the slowing without a path)
constexpr float START_DISTANCE = 5.0f;                  // m: the road point the ride starts from
constexpr float STEER_RATE = 0.002f;                    // of the frame's angle to the handlebars, a ms
constexpr float STEER_SMALL = 0.05235988f;              // rad (3 degrees): below, three times as fast
constexpr float STEER_LIMIT = 0.43633232f;              // rad (25 degrees) the rider's animation shows
constexpr float RAD_TO_DEG = 57.295776f;
constexpr float STEER_CENTRE = 25.0f;                   // the animation's frame for straight on
constexpr int ANIM_FRAME_TIME = 16;                     // ms
constexpr float WHEEL_HEIGHT = 0.35f;                   // m of the front wheel over the ground
constexpr float FALL_RATE = -2.0f;                      // m/s
constexpr float WHEEL_TURN = 3.3333333f;                // rad a m (a 0.3 m wheel)
constexpr float CRANK_RATIO = 0.5f;
constexpr float PEDAL_FRAMES = 127.32395f;              // the legs' animation frames a rad of the cranks
constexpr float PEDAL_RAD = 0.007853982f;               // (1 / PEDAL_FRAMES)
constexpr uint32_t GROUND_EXCLUDE_FLAGS = 0x800000;
constexpr int RIDER_NAME_SIZE = 1000;                       // the rider's name read back (999 characters)

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

float SquareYZX(const S_vector &v) { return (v.y * v.y + v.z * v.z) + v.x * v.x; }

// I3D_frame::GetLocalDir as the original sums it.
S_vector LocalDir(I3D_frame *frame)
{
    bool normalize = false;
    S_vector dir = frame->GetLocalDirUnnormalized(&normalize);
    if (normalize)
        NormalizeBy(dir, SquareYZX(dir));
    return dir;
}

// The normalized world z axis (I3D_frame::GetWorldDir inlined).
S_vector WorldDir(const I3D_frame *frame)
{
    S_vector dir = frame->GetMatrix()(2);
    NormalizeBy(dir, SquareYZX(dir));
    return dir;
}

// `frame`'s rotation turned by `angle` about x.
void TurnAboutX(I3D_frame *frame, float angle)
{
    S_quat turn;
    turn.Make(S_vector(1.0f, 0.0f, 0.0f), angle);
    frame->SetRot(frame->GetRot().RotateByMatrix(turn.RotationMatrix()));
}

C_Human *HumanOf(const I3D_frame *frame)
{
    const S_FrameData *data = frame ? frame->GetData() : nullptr;
    return data ? static_cast<C_Human *>(data->actor) : nullptr;
}

C_BikeCreator g_bikeCreator;                            // 0x008ac954

}  // namespace

C_Bike::C_Bike() : C_Actor(ACTOR_BIKE)                  // 0x005d9460
{
}

void C_Bike::RestartMission(bool)                       // 0x005d8880
{
    if (m_rider)
        Drive(CHECKPOINT_NONE, 0.0f, m_rider);
    StopMission(true);
    StartMission(true);
}

void C_Bike::CopyFrom(const C_Actor *source)            // 0x005d9590
{
    C_Actor::CopyFrom(source);
    const auto *bike = static_cast<const C_Bike *>(source);
    m_topSpeed = bike->m_topSpeed;
    m_unknown124 = bike->m_unknown124;
}

bool C_Bike::ReadProperties(C_chunk &chunk)             // 0x005db640
{
    const float topSpeed = m_topSpeed;
    chunk.Read(m_topSpeed);
    m_topSpeed = topSpeed;
    chunk.Read(m_unknown124);
    return true;
}

bool C_Bike::SaveGame(C_chunk &chunk)                   // 0x005d88b0
{
    C_Actor::SaveGame(chunk);
    if (!m_driving) {
        chunk.Begin(CHUNK_STOPPED);
        chunk.Ascend();
        return true;
    }
    chunk.Begin(CHUNK_RIDDEN);
    // Fix: ridden without a rider, the name is empty (the original reads through null).
    const char *name = m_rider ? m_rider->GetName() : "";
    chunk.WriteChunk(CHUNK_RIDER, name, static_cast<int>(std::strlen(name) + 1));
    chunk.Begin(CHUNK_PATH);
    chunk.Write(static_cast<int>(m_path.size()));
    for (uint16_t point : m_path)
        chunk.Write(point);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_SPEED, m_speed);
    chunk.Begin(CHUNK_PEDALS);
    chunk.Write(m_pedalPhase);
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

bool C_Bike::LoadGame(C_chunk &chunk)                   // 0x005d8b90
{
    C_Actor::LoadGame(chunk);
    uint16_t id = 0;
    if (chunk.Descend(id) && id == CHUNK_RIDDEN) {
        // Quirk kept: a chunk of another id is entered and not left.
        while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
            switch (id) {
            case CHUNK_RIDER: {
                char name[RIDER_NAME_SIZE];
                chunk.ReadString(name, RIDER_NAME_SIZE);
                chunk.Ascend();
                I3D_frame *rider = GetGame()->GetScene()->FindFrame(name, FRAME_MASK_ANY);
                if (rider) {
                    MountRider(rider);
                    if (C_Human *human = HumanOf(rider)) {
                        m_speed = 0.0f;
                        human->Suspend(true);
                    }
                    m_driving = true;
                }
                break;
            }
            case CHUNK_SPEED:
                chunk.Read(m_speed);
                chunk.Ascend();
                break;
            case CHUNK_PEDALS:
                chunk.Read(m_pedalPhase);
                chunk.Ascend();
                break;
            case CHUNK_PATH: {
                int count = 0;
                chunk.Read(count);
                uint16_t point = 0;                     // (a short chunk repeats the last one read)
                for (int i = count; i != 0; --i) {
                    chunk.Read(point);
                    m_path.push_back(point);
                }
                chunk.Ascend();
                break;
            }
            default:
                break;
            }
        }
    }
    chunk.Ascend();
    return true;
}

void C_Bike::StartMission(bool server)                  // 0x005db0e0 (DS 0x005f7200)
{
    Activate(server);
    if (m_started)
        return;
    m_started = true;
    m_driving = false;
    m_speed = 0.0f;
    m_path.clear();
    MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, BIKE_VOLUME_TYPE, this);
    g_collisionWorld.AddVolume(&m_volume);
    const S_matrix &world = m_frame->GetMatrix();
    m_sound.chapter = BIKE_SOUND_CHAPTER;
    m_sound.pos = world(3);
    m_sound.posW = world.m[3][3];
    m_sound.attachTo = nullptr;
    m_sound.flags = 0;
    m_sound.source = this;
    m_frontWheel = m_frame->FindChildFrame("kolo1", FRAME_MASK_ANY);
    m_rearWheel = m_frame->FindChildFrame("kolo2", FRAME_MASK_ANY);
    m_handlebars = m_frame->FindChildFrame("riditka", FRAME_MASK_ANY);
    m_cranks = m_frame->FindChildFrame("kliky", FRAME_MASK_ANY);
    m_body = m_frame->FindChildFrame("ram", FRAME_MASK_ANY);
    m_riderModel = nullptr;
    m_pedalPhase = 0.0f;
    GetGEffects()->CreateShadow(m_frame);
    m_rider = nullptr;
}

void C_Bike::StopMission(bool server)                   // 0x005db4c0
{
    Deactivate(server);
    if (!m_started)
        return;
    if (m_riderModel) {
        m_riderModel->LinkTo(nullptr, 0);
        m_riderModel->Release();
        m_riderModel = nullptr;
    }
    m_rider = nullptr;
    g_collisionWorld.RemoveVolume(&m_volume);
    m_started = false;
    m_path.clear();
    if (m_body)
        m_body->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    if (m_cranks) {
        S_quat straight;
        straight.Make(IdentityMatrix());
        m_cranks->SetRot(straight);
    }
    // (The shadow is asked for twice, a reference each, and released once.)
    if (GetGEffects()->GetShadow(m_frame))
        GetGEffects()->GetShadow(m_frame)->Release();
}

void C_Bike::MountRider(I3D_frame *rider)               // 0x005d9230
{
    if (!rider)
        return;
    m_riderModel = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (!m_riderModel)
        return;
    m_riderModel->Duplicate(rider);
    m_riderModel->SetName(C_fstr("%s_ridedupl", rider->GetName()));
    m_riderModel->LinkTo(m_frame, 0);
    m_riderModel->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    S_quat straight;
    straight.Make(IdentityMatrix());
    m_riderModel->SetRot(straight);
    m_riderModel->Update();
    m_riderModel->LinkTo(m_body, I3DLINK_KEEP_WORLD);
    m_riderModel->Update();
    I3D_animation_set *anim = nullptr;
    if (g_animationCache.Get("bike_legs", &anim) < 0)
        return;
    m_riderModel->SetAnimation(anim, 0, 1.0f, 2);
    if (anim) {
        anim->Release();
        anim = nullptr;
    }
    if (g_animationCache.Get("bike_ride", &anim) < 0)
        return;
    m_riderModel->SetAnimation(anim, 1, 1.0f, 0);
    if (anim)
        anim->Release();
    m_rider = rider;
}

void C_Bike::Drive(int roadCheckpoint, float, I3D_frame *rider)   // 0x005d8f60
{
    if (roadCheckpoint != CHECKPOINT_NONE) {
        C_CheckpointNetwork *roads = GetGame()->GetCheckpoints()->GetRoadNetwork();
        const int start = roads->FindNearestPoint(m_frame->GetWorldPos(), START_DISTANCE, -1.0f);
        if (start == CHECKPOINT_NONE)
            return;
        std::vector<uint16_t> path;
        if (!roads->FindPath(start, roadCheckpoint, &path, 0))
            return;
        m_path.clear();
        bool mounted = false;
        if (rider && !m_riderModel) {
            MountRider(rider);
            mounted = true;
        }
        for (uint16_t point : path)
            m_path.push_back(point);
        if (roadCheckpoint != start)
            m_path.push_back(static_cast<uint16_t>(roadCheckpoint));
        if (!m_path.empty())
            m_driving = true;
        if (mounted) {
            if (C_Human *human = HumanOf(rider)) {
                m_speed = 0.0f;
                human->Suspend(true);
            }
        }
        return;
    }
    // Off: the human back where the bike is.
    if (C_Human *human = HumanOf(rider)) {
        human->Suspend(false);
        const S_matrix &world = m_frame->GetMatrix();
        human->SetPosition4({world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]});
        I3D_scene *scene = GetGame()->GetScene();
        scene->UpdateFrameSector(rider, rider->GetWorldPos());
    }
    if (m_riderModel) {
        m_riderModel->LinkTo(nullptr, 0);
        m_riderModel->Release();
        m_riderModel = nullptr;
    }
    m_driving = false;
}

void C_Bike::Update(int frameTime)                      // 0x005d95c0
{
    const S_vector before = m_frame->GetPos();
    const uint32_t time = static_cast<uint32_t>(frameTime);
    if (m_driving) {
        if (m_path.empty())
            return;
        C_CheckpointNetwork *roads = GetGame()->GetCheckpoints()->GetRoadNetwork();
        const S_Checkpoint *next = roads->Get(m_path[0]);
        const float targetX = next->pos.x;
        const float targetZ = next->pos.z;
        const float atX = m_frame->GetPos().x;
        const float atZ = m_frame->GetPos().z;
        const float dx = atX - targetX;
        const float dz = atZ - targetZ;
        const float distance = std::sqrt(dz * dz + dx * dx);
        const float radius = static_cast<float>(roads->Get(m_path[0])->radius) * POINT_RADIUS_UNIT;
        const float reach = radius < MIN_REACH ? MIN_REACH : radius;
        if (distance > reach) {
            m_speed = static_cast<float>(time) * ACCELERATION + m_speed;
            const float top = m_topSpeed * KMH_TO_MS;
            if (m_speed > top)
                m_speed = top;
        } else {
            // (Still turned towards the point reached.)
            m_path.erase(m_path.begin());
            if (m_path.empty()) {
                m_driving = false;
                const S_FrameData *data = m_rider ? m_rider->GetData() : nullptr;
                if (data && data->script)
                    data->script->Advance();
            }
        }
        // Turned towards the point (not while it is within the angle its radius makes).
        S_vector dir = LocalDir(m_frame);
        const float facing[2] = {dir.x, dir.z};
        const float toPoint[2] = {targetX - atX, targetZ - atZ};
        float turn = AngleBetween2D(facing, toPoint);
        const double cone = std::asin(static_cast<double>(radius / distance));
        if (cone > std::fabs(static_cast<double>(turn)))
            turn = 0.0f;
        const float t = static_cast<float>(time);
        S_quat yaw;
        yaw.Make(S_vector(0.0f, 1.0f, 0.0f), t * turn * 0.001f);
        NormalizeBy(dir, (dir.y * dir.y + dir.x * dir.x) + dir.z * dir.z);
        dir = dir.RotateByMatrix(yaw.RotationMatrix());
        // The frame "ram" keeps facing where it did, then follows the handlebars.
        const S_vector bodyDir = WorldDir(m_body);
        m_frame->SetWorldDir(dir, 0.0f);
        m_frame->Update();
        m_body->SetDir(bodyDir, 0.0f);
        m_body->Update();
        S_vector body = LocalDir(m_body);
        body.y = 0.0f;
        const S_vector bars = LocalDir(m_handlebars);
        const float angle = body.AngleTo(bars);
        S_vector axis(bars.z * body.y - body.z * bars.y, body.z * bars.x - bars.z * body.x,
                      bars.y * body.x - bars.x * body.y);
        NormalizeBy(axis, (axis.z * axis.z + axis.y * axis.y) + axis.x * axis.x);
        float follow = t * STEER_RATE * angle;
        if (angle < STEER_SMALL)
            follow *= 3.0f;
        S_quat toBars;
        toBars.Make(axis, -follow);
        const S_vector turned = body.RotateByMatrix(toBars.RotationMatrix());
        m_body->SetWorldDir(turned, 0.0f);
        m_body->Update();
        // The rider steers (frame 0..50 of "bike_ride": 25 degrees each way).
        float steer = angle;
        if (turned.x > 0.0f)
            steer = steer * -1.0f;
        if (steer > STEER_LIMIT)
            steer = STEER_LIMIT;
        else if (steer < -STEER_LIMIT)
            steer = -STEER_LIMIT;
        if (m_riderModel) {
            const int frame = static_cast<int>(steer * RAD_TO_DEG + STEER_CENTRE);
            m_riderModel->SetAnimTime(1, frame * ANIM_FRAME_TIME);
            m_riderModel->Tick(0);
            m_riderModel->Update();
        }
    }
    if (m_speed == 0.0f)
        return;
    if (m_path.empty()) {
        m_speed = m_speed - static_cast<float>(time) * ACCELERATION;
        if (m_speed < 0.0f)
            m_speed = 0.0f;
    }
    const float t = static_cast<float>(time);
    const float distance = t * m_speed * 0.001f;
    // Along the handlebars, level.
    S_vector ahead = WorldDir(m_handlebars);
    ahead.y = 0.0f;
    NormalizeBy(ahead, ahead.z * ahead.z + ahead.x * ahead.x);
    const S_vector move(ahead.x * distance, ahead.y * distance, ahead.z * distance);
    const S_vector &at = m_frame->GetWorldPos();
    m_frame->SetPos(S_vector(move.x + at.x, move.y + at.y, move.z + at.z));
    m_frame->Update();
    // The front wheel on the ground (or falling).
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    const S_vector wheel = m_frontWheel->GetWorldPos();
    S_vector down(0.0f, -WHEEL_HEIGHT, 0.0f);
    S_vector ground(0.0f, 0.0f, 0.0f);
    if (!CollisionRayTest(wheel, down, &ground, nullptr, 0, GROUND_EXCLUDE_FLAGS)) {
        down = S_vector(t * 0.0f * 0.001f + down.x, down.y + t * FALL_RATE * 0.001f, down.z + t * 0.0f * 0.001f);
        if (!CollisionRayTest(wheel, down, &ground, nullptr, 0, GROUND_EXCLUDE_FLAGS))
            ground = S_vector(down.x + wheel.x, down.y + wheel.y, down.z + wheel.z);
    }
    const S_vector &local = m_frame->GetPos();
    const S_vector offset(local.x - wheel.x, local.y - wheel.y, local.z - wheel.z);
    m_frame->SetPosInline(S_vector(offset.x + ground.x, (ground.y + offset.y) + WHEEL_HEIGHT, ground.z + offset.z));
    // Facing along the move (twice it, added to the direction; not normalized here).
    const S_vector &now = m_frame->GetPos();
    const S_vector moved(now.x - before.x, now.y - before.y, now.z - before.z);
    const S_vector dir = LocalDir(m_frame);
    m_frame->SetWorldDir(S_vector(dir.x + (moved.x + moved.x), dir.y + (moved.y + moved.y), dir.z + (moved.z + moved.z)),
                         0.0f);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    // The wheels, the cranks and the rider's legs.
    if (distance != 0.0f && m_frontWheel) {
        const float roll = distance * WHEEL_TURN;
        S_quat spin;
        spin.Make(S_vector(1.0f, 0.0f, 0.0f), -roll);
        const S_quat wheelRot = m_frontWheel->GetRot().RotateByMatrix(spin.RotationMatrix());
        m_frontWheel->SetRot(wheelRot);
        m_frontWheel->Update();
        if (m_rearWheel) {
            m_rearWheel->SetRot(wheelRot);
            m_rearWheel->Update();
        }
        if (m_cranks) {
            const float crank = roll * CRANK_RATIO;
            TurnAboutX(m_cranks, -crank);
            m_cranks->Update();
            if (m_riderModel) {
                const float phase = crank + m_pedalPhase;
                if (phase != 0.0f) {
                    const float frames = phase * PEDAL_FRAMES;
                    const int whole = static_cast<int>(frames);
                    int times[8] = {whole, 0, 0, 0, 0, 0, 0, 0};
                    m_riderModel->AdvanceStages(times);
                    m_riderModel->Update();
                    m_pedalPhase = (frames - static_cast<float>(whole)) * PEDAL_RAD;
                }
            }
        }
    }
    // The box follows. Quirk kept: its sphere's centre is the frame's bound's, in the frame's
    // space (the bound's radius as w).
    const I3D_bound_volume &bound = m_frame->GetBound();
    m_invMatrix.Inverse(m_frame->GetMatrix());
    m_volume.center = bound.center;
    m_volume.centerW = bound.radius;
    g_collisionWorld.MoveVolume(&m_volume);
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_BikeCreator::Create()                        // 0x005d87f0
{
    return new C_Bike();                                // _aligned_malloc(0x1d0, 16) in the original
}

void C_BikeCreator::Destroy(C_Actor *actor)             // 0x005d8860
{
    delete actor;
}

// Registers the creator (static initialiser 0x005d87d0, thunk 0x005d87c0).
static const bool g_bikeCreatorRegistered = [] {
    g_actorCreators[ACTOR_BIKE] = &g_bikeCreator;
    return true;
}();
