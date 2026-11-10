// Schools of fish (C_Fish; original 0x005db780..0x005de650).
#include "game/fish.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/pending.h"                               // RandomFloat
#include "game/resource_cache.h"                        // g_modelCache
#include "ls3d/i3d.h"

namespace {

// The school's states.
enum : int {
    FISH_SPEED_UP = 0,                                  // to the top speed, then FISH_SLOW_DOWN
    FISH_SLOW_DOWN = 1,                                 // to a tenth of it, then FISH_PAUSE
    FISH_PAUSE = 2,                                     // m_pause ms, then a new direction
};

// The saved game's chunks.
constexpr uint16_t CHUNK_FISH = 9999;
constexpr uint16_t CHUNK_DIR = 1000;
constexpr uint16_t CHUNK_STATE = 2000;
constexpr uint16_t CHUNK_PAUSE = 3000;
constexpr uint16_t CHUNK_TIME = 4000;
constexpr uint16_t CHUNK_SPEED = 5000;
constexpr uint16_t CHUNK_VELOCITY = 6000;

const char *const FISH_MODEL = "ryba";                  // the model and its frame copied
constexpr float VISIBLE_RANGE = 15.0f;                  // m beyond the radius
constexpr float ACCELERATION = 0.007f;                  // m/s a ms
constexpr float SLOW_SPEED = 0.1f;                      // of the top speed
constexpr float SLOW_DOWN = 0.1f;                       // m/s added to the speed's excess (a s)
constexpr float START_SPEED = 0.5f;
constexpr float TURN_RANGE = 4.712389f;                 // rad about y (+-135 degrees)
constexpr float TURN_HALF = 2.3561945f;
constexpr float PITCH_RANGE = 0.94247788f;              // rad about z (+-27 degrees)
constexpr float PITCH_HALF = 0.47123894f;
constexpr float LEVEL_RATE = 0.001f;                    // of the direction's y a ms back to 0
constexpr float SPREAD = 1.1111112f;                    // the places' x and y going fast (z: SQUEEZE)
constexpr float SQUEEZE = 0.9f;
constexpr float SWAPPED = 0.2f;                         // of the fish whose places change sides at a turn
constexpr float SLIDE_SCALE = 0.5f;                     // of the slide's normal added to the direction
constexpr float VELOCITY_SCALE = 10.0f;                 // the tick's move (m) to m/s
constexpr float TURN_RATE = 0.06f;                      // of the frame's move a s turning it
constexpr float FISH_SPEED = 0.00015f;                  // m a ms a fish's place moves
constexpr float SWAY_RATE = 0.0001f;
constexpr float FADE_SQUARE = 1.0f;                     // m^2 from the camera: fully drawn beyond

// The length from a square the caller summed, as the SDK's inlined helper: 0 when its bits are
// below 1e-8's, exactly 1 for a near-unit square, the square root otherwise.
float LengthOf(float square)
{
    int32_t bits;
    std::memcpy(&bits, &square, sizeof bits);
    if (bits < 0x322bcc77)
        return 0.0f;
    if (!(std::fabs(square - 1.0f) < 1e-08f))
        return std::sqrt(square);
    return 1.0f;
}

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

float SquareXYZ(const S_vector &v) { return (v.x * v.x + v.y * v.y) + v.z * v.z; }
float SquareXZY(const S_vector &v) { return (v.x * v.x + v.z * v.z) + v.y * v.y; }
float SquareYZX(const S_vector &v) { return (v.y * v.y + v.z * v.z) + v.x * v.x; }

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

// The sway's sin / cos (extended precision on the FPU) times a float `scale`, rounded once.
float Sway(double wave, float scale) { return static_cast<float>(wave * static_cast<double>(scale)); }

C_FishCreator g_fishCreator;                            // 0x008ac958

}  // namespace

C_Fish::C_Fish() : C_Actor(ACTOR_FISH)                  // 0x005dbd80
{
}

void C_Fish::RestartMission(bool)                       // 0x005db870 (DS 0x005f7be0)
{
    StopMission(true);
    StartMission(true);
}

void C_Fish::CopyFrom(const C_Actor *source)            // 0x005dbe80
{
    if (!source)
        return;
    C_Actor::CopyFrom(source);
    const auto *fish = static_cast<const C_Fish *>(source);
    m_topSpeed = fish->m_topSpeed;
    m_count = fish->m_count;
    m_radius = fish->m_radius;
}

bool C_Fish::ReadProperties(C_chunk &chunk)             // 0x005de300
{
    chunk.Read(m_topSpeed);
    chunk.Read(m_radius);
    chunk.Read(m_count);
    return true;
}

bool C_Fish::SaveGame(C_chunk &chunk)                   // 0x005db890
{
    C_Actor::SaveGame(chunk);
    chunk.Begin(CHUNK_FISH);
    chunk.WriteChunk(CHUNK_DIR, &m_dir, 12);
    chunk.WriteChunk(CHUNK_STATE, m_state);
    chunk.WriteChunk(CHUNK_PAUSE, m_pause);
    chunk.WriteChunk(CHUNK_TIME, m_time);
    chunk.WriteChunk(CHUNK_SPEED, m_speed);
    chunk.WriteChunk(CHUNK_VELOCITY, &m_velocity, 12);
    chunk.Ascend();
    return true;
}

bool C_Fish::LoadGame(C_chunk &chunk)                   // 0x005dba50
{
    C_Actor::LoadGame(chunk);
    // (The chunk's id is not checked.)
    uint16_t id = 0;
    chunk.Descend(id);
    // Quirk kept: a speed chunk too short to read leaves the speed the first four bytes of this
    // chunk's header (its id and the low half of its size), the buffer it is read into.
    uint32_t speed = id | static_cast<uint32_t>(chunk.RemainingSize() + 6) << 16;
    // Quirk kept: a chunk of another id is entered and not left.
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_DIR: {
            S_vector dir(0.0f, 0.0f, 0.0f);             // (the original copies its w from the stack)
            chunk.Read(&dir, 12);
            chunk.Ascend();
            SetXYZ(m_dir, dir);
            break;
        }
        case CHUNK_STATE:
            chunk.Read(m_state);
            chunk.Ascend();
            break;
        case CHUNK_PAUSE:
            chunk.Read(m_pause);
            chunk.Ascend();
            break;
        case CHUNK_TIME:
            chunk.Read(m_time);
            chunk.Ascend();
            break;
        case CHUNK_SPEED:
            chunk.Read(speed);
            chunk.Ascend();
            std::memcpy(&m_speed, &speed, sizeof m_speed);
            break;
        case CHUNK_VELOCITY: {
            S_vector velocity(0.0f, 0.0f, 0.0f);
            chunk.Read(&velocity, 12);
            chunk.Ascend();
            SetXYZ(m_velocity, velocity);
            break;
        }
        default:
            break;
        }
    }
    chunk.Ascend();
    return true;
}

void C_Fish::StartMission(bool server)                  // 0x005dd900
{
    Activate(server);
    if (m_started)
        return;
    I3D_model *model = nullptr;
    if (g_modelCache.Get(FISH_MODEL, &model) != I3D_OK)
        return;
    I3D_frame *proto = model->FindChildFrame(FISH_MODEL, FRAME_MASK_ANY);
    // Fix: a model without the frame makes no fish (the original calls through null).
    for (int left = proto ? m_count : 0; left != 0;) {
        --left;
        I3D_frame *frame = I3DGetDriver()->CreateVisual(proto->GetVisualType());
        if (!frame) {
            // (The fish made so far are kept; the school is not started.)
            model->Release();
            return;
        }
        frame->Duplicate(proto);
        auto *fish = new S_FishMember;                  // _aligned_malloc(0x30, 16), zeroed
        m_fish.push_back(fish);
        fish->frame = frame;
        const float z = RandomFloat(m_radius + m_radius) - m_radius;
        const float y = RandomFloat(m_radius) - m_radius * 0.5f;
        const float x = RandomFloat(m_radius) - m_radius * 0.5f;
        fish->pos = {x, y, z, 0.0f};                    // (w: a stale local in the original)
        fish->phase = RandomFloat(6.2831855f);
        fish->rate = RandomFloat(0.5f) + 0.5f;
        fish->target = fish->pos;
        frame->LinkTo(m_frame, 0);
        frame->SetPosInline(S_vector(x, y, z));
        const float scaleZ = RandomFloat(0.5f) + 1.0f;
        const float scaleY = RandomFloat(0.25f) + 1.0f;
        const float scaleX = RandomFloat(0.25f) + 1.0f;
        frame->SetScale(S_vector(scaleX, scaleY, scaleZ));
        frame->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        frame->Update();
    }
    m_speed = START_SPEED;
    m_started = true;
    // The frame's direction, level (I3D_frame::GetWorldDir 0x0041d200, then without y).
    S_vector dir = m_frame->GetMatrix()(2);
    NormalizeBy(dir, SquareYZX(dir));
    dir.y = 0.0f;
    NormalizeBy(dir, dir.z * dir.z + dir.x * dir.x);
    m_state = FISH_PAUSE;
    SetXYZ(m_dir, dir);
    m_time = 0;
    m_pause = std::rand() % 1000 + 1000;
    SetXYZ(m_velocity, S_vector(0.0f, 0.0f, 0.0f));
    model->Release();
}

void C_Fish::StopMission(bool server)                   // 0x005de260
{
    Deactivate(server);
    if (!m_started)
        return;
    m_frame->SetOn(true);
    for (size_t i = m_fish.size(); i-- > 0;) {
        if (S_FishMember *fish = m_fish[i]) {
            if (fish->frame)
                fish->frame->Release();
            delete fish;
        }
    }
    m_fish.clear();
    m_started = false;
}

bool C_Fish::UpdateVisibility()                         // 0x005dca60
{
    I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera();
    if (!camera)
        return false;
    const S_vector &at = m_frame->GetWorldPos();
    const S_vector &eye = camera->GetWorldPos();
    const S_vector d(eye.x - at.x, eye.y - at.y, eye.z - at.z);
    const float range = m_radius + VISIBLE_RANGE;
    if (range * range < (d.z * d.z + d.x * d.x) + d.y * d.y) {
        if (m_frame->IsOn())
            m_frame->SetOn(false);
        return false;
    }
    if (!m_frame->IsOn())
        m_frame->SetOn(true);
    return true;
}

void C_Fish::Tick(int timeDelta)                        // 0x005dbec0
{
    if (!UpdateVisibility())
        return;
    const uint32_t time = static_cast<uint32_t>(timeDelta);
    m_time += timeDelta;
    S_vector dir = XYZ(m_dir);
    switch (m_state) {
    case FISH_SPEED_UP:
        m_speed = static_cast<float>(time) * ACCELERATION + m_speed;
        if (m_speed > m_topSpeed) {
            m_state = FISH_SLOW_DOWN;
            m_speed = m_topSpeed;
            for (size_t i = m_fish.size(); i-- > 0;) {
                S_vector4 &target = m_fish[i]->target;
                target.x *= SPREAD;
                target.y *= SPREAD;
                target.z *= SQUEEZE;
            }
        }
        break;
    case FISH_SLOW_DOWN: {
        const float slow = m_topSpeed * SLOW_SPEED;
        m_speed = m_speed - ((m_speed - slow) + SLOW_DOWN) * static_cast<float>(timeDelta) * 0.001f;
        if (m_speed < slow) {
            m_speed = slow;
            m_state = FISH_PAUSE;
            m_pause = std::rand() % 2000 + 3000;
        }
        break;
    }
    case FISH_PAUSE: {
        m_pause -= static_cast<int>(time);
        if (m_pause >= 0)
            break;
        m_state = FISH_SPEED_UP;
        const S_matrix turn = RotateYMatrix(RandomFloat(TURN_RANGE) - TURN_HALF);
        const S_matrix pitch = RotateZMatrix(RandomFloat(PITCH_RANGE) - PITCH_HALF);
        dir = dir.RotateByMatrix(turn).RotateByMatrix(pitch);
        NormalizeBy(dir, SquareXYZ(dir));
        // The school draws in; a fifth of the fish (from the first) swap places with their
        // mirror ones (from the last).
        const int swapped = static_cast<int>(static_cast<float>(m_fish.size()) * SWAPPED);
        for (size_t i = m_fish.size(); i-- > 0;) {
            S_vector4 &target = m_fish[i]->target;
            target.x *= SQUEEZE;
            target.y *= SQUEEZE;
            target.z *= SPREAD;
            if (static_cast<int>(i) < swapped) {
                S_vector4 &mirror = m_fish[m_fish.size() - i - 1]->target;
                const S_vector4 kept = target;
                target = mirror;
                mirror = kept;
            }
        }
        break;
    }
    default:
        break;
    }

    const float frameTime = static_cast<float>(time);
    const float distance = frameTime * m_speed * 0.001f;
    S_vector pos = m_frame->GetWorldPos();
    const S_vector move(distance * dir.x, distance * dir.y, distance * dir.z);
    // The direction back to level.
    const float level = frameTime * LEVEL_RATE;
    if (dir.y > 0.0f) {
        dir.y = dir.y - level;
        if (dir.y < 0.0f)
            dir.y = 0.0f;
    } else if (dir.y < 0.0f) {
        dir.y = level + dir.y;
        if (dir.y > 0.0f)
            dir.y = 0.0f;
    }
    NormalizeBy(dir, SquareXYZ(dir));
    S_vector end(0.0f, 0.0f, 0.0f);
    S_vector normal(0.0f, 0.0f, 0.0f);
    if (CollisionSlideSphere(pos, move, m_radius, end, &normal, 0xFFFFFFFF, 0)) {
        // Turned away along the slide.
        const S_vector slid(end.x - pos.x, end.y - pos.y, end.z - pos.z);
        dir.x = (normal.x * SLIDE_SCALE + slid.x) + dir.x;
        dir.y = (normal.y * SLIDE_SCALE + slid.y) + dir.y;
        dir.z = (normal.z * SLIDE_SCALE + slid.z) + dir.z;
        NormalizeBy(dir, SquareXYZ(dir));
        pos = end;
    } else {
        pos = S_vector(pos.x + move.x, pos.y + move.y, pos.z + move.z);
    }
    SetXYZ(m_dir, dir);
    // No farther than the distance.
    const S_vector &from = m_frame->GetWorldPos();
    S_vector delta(pos.x - from.x, pos.y - from.y, pos.z - from.z);
    const float square = SquareXZY(delta);
    if (LengthOf(square) > distance) {
        NormalizeBy(delta, square);
        delta = S_vector(delta.x * distance, delta.y * distance, delta.z * distance);
        pos = S_vector(delta.x + from.x, delta.y + from.y, delta.z + from.z);
    }
    const S_vector &now = m_frame->GetWorldPos();
    SetXYZ(m_velocity, S_vector((pos.x - now.x) * VELOCITY_SCALE, (pos.y - now.y) * VELOCITY_SCALE,
                                (pos.z - now.z) * VELOCITY_SCALE));
}

void C_Fish::Update(int frameTime)                      // 0x005dcb10
{
    if (!UpdateVisibility())
        return;
    const float time = static_cast<float>(static_cast<uint32_t>(frameTime));
    const float seconds = time * 0.001f;
    S_vector step(seconds * m_velocity.x, seconds * m_velocity.y, seconds * m_velocity.z);
    const S_vector &from = m_frame->GetWorldPos();
    const S_vector to(step.x + from.x, step.y + from.y, step.z + from.z);
    // The frame turned towards the move.
    bool turned = false;
    const float square = SquareXZY(step);
    if (square != 0.0f) {
        S_vector move = step;
        NormalizeBy(move, square);
        move *= seconds * TURN_RATE;
        S_vector dir = m_frame->GetMatrix()(2);
        NormalizeBy(dir, SquareYZX(dir));
        dir = S_vector(dir.x + move.x, dir.y + move.y, dir.z + move.z);
        NormalizeBy(dir, SquareXYZ(dir));
        m_frame->SetDir(dir, 0.0f);
        turned = true;
    }
    m_frame->SetPos(to);
    m_frame->Update();
    // The fish turn towards a point ahead along the move (the frame's space); unmoved, the move
    // itself.
    if (turned) {
        S_matrix inverse;
        inverse.Inverse(m_frame->GetMatrix());
        NormalizeBy(step, SquareXZY(step));
        const float ahead = seconds + seconds;
        step = S_vector(step.x * ahead, step.y * ahead, step.z * ahead);
        const S_vector &at = m_frame->GetWorldPos();
        step = S_vector(step.x + at.x, step.y + at.y, step.z + at.z);
        step *= inverse;
    }
    const S_vector camera = GetGame()->GetScene()->GetActiveCamera()->GetWorldPos();
    for (size_t i = m_fish.size(); i-- > 0;) {
        S_FishMember &fish = *m_fish[i];
        // The place towards its target, at most 0.15 m a second.
        S_vector delta(fish.target.x - fish.pos.x, fish.target.y - fish.pos.y, fish.target.z - fish.pos.z);
        const float distance = SquareXZY(delta);
        const float length = LengthOf(distance);
        if (length != 0.0f) {
            const float most = time * FISH_SPEED;
            const float by = most < length ? most : length;
            NormalizeBy(delta, distance);
            fish.pos.x = delta.x * by + fish.pos.x;
            fish.pos.y = delta.y * by + fish.pos.y;
            fish.pos.z = by * delta.z + fish.pos.z;
        }
        // (I3D_frame::GetLocalDir, as the original sums it.)
        bool normalize = false;
        S_vector dir = fish.frame->GetLocalDirUnnormalized(&normalize);
        if (normalize)
            NormalizeBy(dir, SquareYZX(dir));
        dir = S_vector(dir.x + step.x, dir.y + step.y, dir.z + step.z);
        NormalizeBy(dir, SquareYZX(dir));
        fish.frame->SetWorldDir(dir, 0.0f);
        // The sway about the place.
        const float t = static_cast<float>(m_time) * fish.rate * SWAY_RATE;
        const float x = Sway(std::sin(static_cast<double>(SWAY_RATE * t + fish.phase)), 0.25f) + fish.pos.x;
        const float z = Sway(std::cos(static_cast<double>(7e-05f * t + fish.phase)), 0.35f) + fish.pos.z;
        const float y = Sway(std::cos(static_cast<double>(t * 0.00012f + fish.phase)), 0.45f) + fish.pos.y;
        fish.frame->SetPosInline(S_vector(x, y, z));
        // Faded out within a metre of the camera.
        const S_vector &at = fish.frame->GetWorldPos();
        const S_vector d(at.x - camera.x, at.y - camera.y, at.z - camera.z);
        const float closeness = (d.z * d.z + d.y * d.y) + d.x * d.x;
        fish.frame->SetHierarchyAlpha(closeness < FADE_SQUARE ? closeness : 1.0f);
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_FishCreator::Create()                        // 0x005db7e0
{
    return new C_Fish();                                // _aligned_malloc(0xe0, 16) in the original
}

void C_FishCreator::Destroy(C_Actor *actor)             // 0x005db850
{
    delete actor;
}

// Registers the creator (static initialiser 0x005db7c0, thunk 0x005db7b0).
static const bool g_fishCreatorRegistered = [] {
    g_actorCreators[ACTOR_FISH] = &g_fishCreator;
    return true;
}();
