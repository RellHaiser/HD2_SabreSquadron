// Submarines (C_Submarine; original 0x005de660..0x005e2070: the actor 0x005de660..0x005dfde0, the
// properties 0x005dfde0..0x005e0240, the seats 0x005e0240..0x005e0da0, the movement 0x005e0da0..).
#include "game/submarine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/collision.h"
#include "game/frame_volume.h"
#include "game/game.h"
#include "game/pending.h"                               // g_gameRole, g_texts, C_CameraFrameKeeper
#include "game/sound_module.h"
#include "ls3d/i3d.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

constexpr int TEXT_SUBMARINE = 0x1BF4;                  // the actor's name

// ReadProperties' chunks.
constexpr uint16_t PROPERTY_VERSION = 0;
constexpr uint16_t PROPERTY_MAX_WATER_LEVEL = 1;
constexpr int PROPERTIES_VERSION = 2;
// The saved game's chunks.
constexpr uint16_t CHUNK_SPEED = 0x457;
constexpr uint16_t CHUNK_TURN = 0x8AE;
constexpr uint16_t CHUNK_CLIMB = 0xD05;

constexpr float MAX_SPEED = 6.0f;                       // m/s
constexpr float MAX_CLIMB = 1.0f;                       // m/s
constexpr float MAX_TURN = 0.5f;
constexpr float TURN_RATE = 0.1f;                       // per s at full sideways control
constexpr float TURN_DECAY = 0.5f;                      // per s
constexpr float TURN_SCALE = 0.2f;                      // rad per s per (turn * speed)
constexpr float MOVING_SPEED = 0.01f;                   // |speed| above: the propeller and the engine
constexpr float FAST_SPEED = 2.4f;                      // |speed| above: the water sounds
constexpr float SURFACE_RANGE = 1.0f;                   // m below the maximum level: at the surface

// The pilot seat's camera keeper's dummy (the car's names).
const char *const CAMERA_3RD = "camera3RD*";
const char *const CAMERA_FPV = "cameraFPV*";

// The hull's boxes: the "BODYCOLB00" frame's children.
constexpr uint32_t HULL_VOLUME_FLAGS = 0x42;
constexpr uint32_t HULL_VOLUME_TYPE = 1;
// CheckCollision's sweep: what it hits (types) and leaves out (flags).
constexpr uint32_t SWEEP_TYPES = 2;
constexpr uint32_t SWEEP_FLAGS = 0x10000;

// The sounds: chapter 8, these groups (looping, at the boat's frame; the surface's at the
// propeller).
constexpr int SOUND_CHAPTER = 8;
constexpr uint32_t SOUND_FLAGS = 0xD;

// The editor's description of the property (0x008ac960, set by the static initialiser 0x005dfdf0,
// thunk 0x005dfde0; the game does not read it).
struct S_PropertyDesc {
    uint16_t type;
    uint16_t count;
    const char *name;
    const char *format;
    const char *label;
    float min;
    float max;
    int unknown18;
    uint8_t unknown1C[8];
    uint16_t unknown24;
};
const S_PropertyDesc g_maxWaterLevelDesc = {5, 1, "Max waterlevel", "%1(m)", "Max waterlevel", -1000.0f, 1000.0f, 0, {}, 0};

// The submarines of the mission (0x008ac9a8; made by the static initialiser 0x005de750 with
// 0x005de760, freed at exit by 0x005de790).
std::vector<C_Actor *> g_submarines;

// The sound in `slot` (re)started or paused; false without one.
bool SetSoundActive(int slot, bool on)                  // 0x005df600
{
    if (slot == -1)
        return false;
    I3D_sound *sound = GetSoundModule()->GetSound(slot);
    if (!sound)
        return false;
    if (sound->IsOn() != on)
        sound->SetActive(on, false);
    return true;
}

// Its frequency moved to `frequency` when it is 0.05 off (the sound in `slot`).
void SetSoundFrequency(int slot, float frequency)
{
    I3D_sound *sound = GetSoundModule()->GetSound(slot);
    if (!(std::fabs(sound->GetFrequencyInline() - frequency) <= 0.05f))
        sound->SetFrequency(frequency);
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

// The frame's z axis, normalised summed (y² + z²) + x².
S_vector4 FrameHeading(I3D_frame *frame)
{
    const S_matrix &world = frame->GetMatrix();
    S_vector z(world.m[2][0], world.m[2][1], world.m[2][2]);
    NormalizeBy(z, (z.y * z.y + z.z * z.z) + z.x * z.x);
    return {z.x, z.y, z.z, world.m[2][3]};
}

}  // namespace

const std::vector<C_Actor *> &GetSubmarines()           // 0x005de7b0
{
    return g_submarines;
}

C_Submarine::C_Submarine() : C_Actor(ACTOR_SUBMARINE)   // 0x005df000
{
    // (The sound slots are unset in the original until StartMission.)
    Reset();
    m_name = g_texts.Get(TEXT_SUBMARINE);
    g_submarines.push_back(this);
}

C_Submarine::~C_Submarine()                             // 0x005df2e0
{
    for (size_t i = g_submarines.size(); i-- > 0;) {
        if (g_submarines[i] == this) {
            g_submarines[i] = g_submarines.back();
            g_submarines.pop_back();
            break;
        }
    }
    Reset();
}

void C_Submarine::SetControls(float forward, float side, float up)   // 0x005e0dd0
{
    m_forward = forward;
    m_side = side;
    m_up = up;
}

void C_Submarine::Reset()                               // 0x005df650
{
    m_speed = 0.0f;
    m_turn = 0.0f;
    m_forward = 0.0f;
    m_side = 0.0f;
    m_up = 0.0f;
    m_climb = 0.0f;
    m_volumesActive = false;
    for (size_t i = m_volumes.size(); i-- > 0;) {
        if (S_FrameVolume *volume = m_volumes[i]) {
            g_collisionWorld.RemoveVolume(&volume->volume);   // 0x005dfa70
            delete volume;
        }
    }
    m_volumes.clear();
    for (int seat = GetSeatCount(); seat-- > 0;) {
        LeaveSeat(seat);
        SetSeatBlocked(seat, false);
    }
}

bool C_Submarine::SetFrame(I3D_frame *frame)            // 0x005dee00
{
    if (!C_Actor::SetFrame(frame))
        return false;
    m_body = frame->FindChildFrame("BODY", FRAME_MASK_ANY);
    // (Without a body the w is a stale stack value in the original.)
    m_bodyPos = {};
    if (m_body) {
        const S_vector &pos = m_body->GetPos();
        m_bodyPos = {pos.x, pos.y, pos.z, m_body->GetPosW()};
    }
    for (int seat = GetSeatCount(); seat-- > 0;) {
        char name[16];
        std::snprintf(name, sizeof name, "SEAT%02d", seat);
        m_seats[seat].frame = frame->FindChildFrame(name, FRAME_MASK_ANY);
        if (!m_seats[seat].frame)
            break;
        // "SEAT<digits>": the flags' bits (added to what they were).
        const char *props = m_seats[seat].frame->GetUserProps();
        if (std::strncmp(props, "SEAT", 4) == 0) {
            for (const char *c = props + 4; *c && *c != ',' && *c != ' '; ++c)
                m_seats[seat].flags |= 1u << ((*c - '0') & 31);
        }
    }
    m_propeller = frame->FindChildFrame("sroub", FRAME_MASK_ANY);
    return true;
}

bool C_Submarine::ReadProperties(C_chunk &chunk)        // 0x005dfe50
{
    uint16_t id;
    if (chunk.RemainingSize() == 0 || !chunk.Descend(id) || id != PROPERTY_VERSION)
        return false;
    // (Quirk kept: a short level chunk leaves the version chunk's id and the low half of its size
    // there, the original's header buffer.)
    const uint32_t versionSize = static_cast<uint32_t>(chunk.RemainingSize() + 6);
    int version = 0;                                    // (the original goes on with a stack value)
    chunk.Read(version);
    chunk.Ascend();
    if (version != PROPERTIES_VERSION)
        return false;
    uint32_t level = (versionSize & 0xFFFF) << 16 | PROPERTY_VERSION;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != PROPERTY_MAX_WATER_LEVEL)
            continue;
        chunk.Read(level);
        chunk.Ascend();
        std::memcpy(&m_maxWaterLevel, &level, sizeof level);
    }
    return true;
}

void C_Submarine::AddVolume(I3D_frame *frame, uint32_t flags, uint32_t type)   // 0x005dfa80
{
    if (!frame)
        return;
    auto *volume = new S_FrameVolume();                 // _aligned_malloc(0xa0, 16) in the original
    volume->Init(frame, flags, type, this);
    m_volumes.push_back(volume);
}

void C_Submarine::StartMission(bool server)             // 0x005df3d0
{
    Activate(server);
    if (!server)
        return;
    if (I3D_frame *hull = m_frame->FindChildFrame("BODYCOLB00", FRAME_MASK_ANY)) {
        for (int i = 0; i < hull->NumChildren(); ++i)
            AddVolume(hull->GetChild(i), HULL_VOLUME_FLAGS, HULL_VOLUME_TYPE);
    }
    m_volumesActive = true;
    m_seats[0].occupant = nullptr;
    m_seats[1].occupant = nullptr;
    m_bob = 0.0f;
    // The sounds, paused. (The request's posW: a stale stack value in the original.)
    S_SoundRequest request;
    request.flags = SOUND_FLAGS;
    request.attachTo = m_frame;
    request.chapter = SOUND_CHAPTER;
    request.group = 7;
    m_engineSound = GetSoundModule()->Play(request);
    SetSoundActive(m_engineSound, false);
    request.group = 0;
    m_diveSound = GetSoundModule()->Play(request);
    SetSoundActive(m_diveSound, false);
    request.group = 1;
    m_riseSound = GetSoundModule()->Play(request);
    SetSoundActive(m_riseSound, false);
    request.attachTo = m_propeller;
    request.group = 2;
    m_surfaceSound = GetSoundModule()->Play(request);
    SetSoundActive(m_surfaceSound, false);
    request.group = 3;
    m_underwaterSound = GetSoundModule()->Play(request);
    SetSoundActive(m_underwaterSound, false);
    // Fix: without a pilot seat there is no camera dummy to keep (the original reads through null).
    I3D_frame *camera = m_seats[0].frame ? m_seats[0].frame->FindChildFrame(CAMERA_3RD, ENUMF_WILDMASK | FRAME_MASK_DUMMY)
                                         : nullptr;
    m_camera = new C_CameraFrameKeeper(this, camera);   // _aligned_malloc(0x50, 16) in the original
}

void C_Submarine::StopMission(bool server)              // 0x005dfca0
{
    Deactivate(server);
    if (!server && IsServer())
        return;
    if (m_body) {
        m_body->SetPosInline(S_vector(m_bodyPos.x, m_bodyPos.y, m_bodyPos.z));
        m_body->Update();
    }
    Reset();
    for (int *slot : {&m_engineSound, &m_diveSound, &m_riseSound, &m_surfaceSound, &m_underwaterSound}) {
        if (*slot != -1)
            GetSoundModule()->Stop(slot);
    }
    if (m_camera) {
        delete m_camera;                                // (Restore, then _aligned_free)
        m_camera = nullptr;
    }
}

void C_Submarine::RestartMission(bool)                  // 0x005de7c0
{
    m_engineSound = m_diveSound = m_riseSound = m_surfaceSound = m_underwaterSound = -1;
    if (IsClient())
        StopMission(false);
    if (IsServer()) {
        StopMission(true);
        StartMission(true);
    }
    if (IsClient())
        StartMission(false);
}

bool C_Submarine::SaveGame(C_chunk &chunk)              // 0x005de830
{
    C_Actor::SaveGame(chunk);
    chunk.Begin(CHUNK_SPEED);
    chunk.Write(m_speed);
    chunk.Ascend();
    chunk.Begin(CHUNK_TURN);
    chunk.Write(m_turn);
    chunk.Ascend();
    chunk.Begin(CHUNK_CLIMB);
    chunk.Write(m_climb);
    chunk.Ascend();
    return true;
}

bool C_Submarine::LoadGame(C_chunk &chunk)              // 0x005dea30
{
    C_Actor::LoadGame(chunk);
    const S_matrix &world = m_frame->GetMatrix();
    m_position = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_direction = FrameHeading(m_frame);
    UpdateVolumes();
    // (Quirk kept: another id is entered and not left. A short chunk stores an unset local in the
    // original: 0 here.)
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        float value = 0.0f;
        if (id == CHUNK_SPEED) {
            chunk.Read(value);
            chunk.Ascend();
            m_speed = value;
        } else if (id == CHUNK_TURN) {
            chunk.Read(value);
            chunk.Ascend();
            m_turn = value;
        } else if (id == CHUNK_CLIMB) {
            chunk.Read(value);
            chunk.Ascend();
            m_climb = value;
        }
    }
    return true;
}

// --- the seats ------------------------------------------------------------------------------------

bool C_Submarine::IsSeatBlocked(int seat)               // 0x005e0270
{
    return seat >= 0 && seat < SEAT_COUNT && m_seats[seat].blocked;
}

bool C_Submarine::SetSeatBlocked(int seat, bool blocked)   // 0x005e0290
{
    if (seat < 0 || seat >= SEAT_COUNT)
        return false;
    m_seats[seat].blocked = blocked;
    return true;
}

void C_Submarine::SetSeatOccupant(int seat, C_Actor *occupant)   // 0x005e0320
{
    if (seat < 0 || seat >= SEAT_COUNT)
        return;
    m_seats[seat].occupant = occupant;
    if (!occupant)
        return;
    if (seat == 0 && m_camera)
        m_camera->Restore();
    I3D_frame *frame = occupant->GetFrame();
    if (!frame)
        return;
    frame->LinkTo(m_seats[seat].frame ? m_seats[seat].frame : m_frame, 0);
    frame->Update();
    frame->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    frame->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    frame->Update();
}

C_Actor *C_Submarine::GetSeatOccupant(int seat)         // 0x005e0410
{
    return seat >= 0 && seat < SEAT_COUNT ? m_seats[seat].occupant : nullptr;
}

int C_Submarine::FindSeat(const C_Actor *occupant)      // 0x005e0430
{
    for (int seat = SEAT_COUNT; seat-- > 0;) {
        if (m_seats[seat].occupant == occupant)
            return seat;
    }
    return -1;
}

C_Actor *C_Submarine::LeaveSeat(int seat)               // 0x005e0460
{
    if (seat < 0 || seat >= SEAT_COUNT)
        return nullptr;
    if (C_Actor *occupant = m_seats[seat].occupant) {
        if (I3D_frame *frame = occupant->GetFrame())
            frame->LinkTo(GetGame()->GetScene()->GetPrimarySector(), I3DLINK_KEEP_WORLD);
        m_seats[seat].occupant = nullptr;
    }
    if (seat == 0) {
        if (m_camera)
            m_camera->Restore();
        SetControls(0.0f, 0.0f, 0.0f);
    }
    SetSeatBlocked(seat, false);
    return m_seats[seat].occupant;
}

bool C_Submarine::ExitSeat(int seat)                    // 0x005e04f0
{
    if (seat < 0 || seat >= SEAT_COUNT)
        return false;
    C_Actor *occupant = GetSeatOccupant(seat);
    if (!occupant || !PutOutside(occupant))
        return false;
    LeaveSeat(seat);
    return true;
}

uint32_t C_Submarine::GetSeatFlags(int seat)            // 0x005e0ca0
{
    return seat >= 0 && seat < SEAT_COUNT ? m_seats[seat].flags : 0;
}

I3D_frame *C_Submarine::GetSeatCamera(int seat)         // 0x005e0cc0
{
    if (seat < 0 || seat >= SEAT_COUNT)
        return nullptr;
    C_Actor *occupant = m_seats[seat].occupant;
    if (occupant)
        occupant->GetFrame()->LinkTo(nullptr, 0);
    const uint32_t mask = ENUMF_WILDMASK | FRAME_MASK_DUMMY;
    I3D_frame *seatFrame = m_seats[seat].frame;
    I3D_frame *camera = seatFrame->FindChildFrame(GetGame()->IsFirstPersonView() ? CAMERA_FPV : CAMERA_3RD, mask);
    if (!camera)
        camera = seatFrame->GetParent()->FindChildFrame(GetGame()->IsFirstPersonView() ? CAMERA_FPV : CAMERA_3RD, mask);
    if (occupant) {
        occupant->GetFrame()->LinkTo(seatFrame, 0);
        occupant->GetFrame()->Update();
    }
    return camera;
}

int C_Submarine::GetSeatText(int seat)                  // 0x005e02c0
{
    if (seat < 0 || seat >= GetSeatCount())
        return -1;
    const char *name = std::strstr(m_seats[seat].frame->GetUserProps(), "name");
    return name ? std::atoi(name + 4) : -1;
}

bool C_Submarine::FindExit(int seat, S_vector4 *pos, S_vector4 *dir, bool otherSide)   // 0x005e0530
{
    if (seat < 0 || seat >= SEAT_COUNT)
        return false;
    I3D_frame *seatFrame = m_seats[seat].frame;
    const S_matrix &seatWorld = seatFrame->GetMatrix();
    S_vector local(seatWorld.m[3][0], seatWorld.m[3][1], seatWorld.m[3][2]);
    const float localW = seatWorld.m[3][3];
    S_matrix inverse;
    inverse.Inverse(m_frame->GetMatrix());
    local *= inverse;
    // The boat's x axis made level and a unit vector (twice, as the inline code does).
    const S_matrix &world = m_frame->GetMatrix();
    S_vector side(world.m[0][0], 0.0f, world.m[0][2]);
    const float levelSquare = side.z * side.z + side.x * side.x;
    if (!(std::fabs(levelSquare - 1.0f) < 1e-08f)) {
        if (!(levelSquare < 1e-08f)) {
            const float scale = 1.0f / std::sqrt(levelSquare);
            side = S_vector(side.x * scale, 0.0f * scale, side.z * scale);
        } else if (side.x != 0.0f) {
            side.x = side.x < 0.0f ? -1.0f : 1.0f;
        } else if (side.z != 0.0f) {
            side.z = side.z < 0.0f ? -1.0f : 1.0f;
        } else {
            side.y = 1.0f;
        }
    }
    NormalizeBy(side, (side.z * side.z + side.x * side.x) + side.y * side.y);
    const float reach = otherSide ? -1.35f : 1.35f;
    const S_vector offset(side.x * reach, reach * side.y, side.z * reach);
    // From the seat's height and depth on the boat's middle, a 0.25 m sphere moved sideways.
    S_vector start(0.0f, local.y, local.z);
    start *= world;
    const S_vector from(start.x, start.y + 1.0f, start.z);
    S_vector hit;
    if (CollisionSphereTest(from, offset, 0.25f, &hit, nullptr, 0xFFFFFFFF, 0))
        return false;
    S_vector end(offset.x + start.x, start.y + offset.y, start.z + offset.z);
    // On the ground below (0.45 m below where the sphere stops).
    if (CollisionSphereTest(S_vector(end.x, end.y + 1.5f, end.z), S_vector(0.0f, -3.5f, 0.0f), 0.25f, &hit, nullptr,
                            0xFFFFFFFF, 0))
        end = S_vector(hit.x, hit.y - 0.45f, hit.z);
    // (The w: the seat's, or after a ground hit a stale stack value in the original.)
    *pos = {end.x, end.y, end.z, localW};
    // Facing the seat, level.
    const S_vector &seatPos = seatFrame->GetWorldPos();
    S_vector facing(seatPos.x - end.x, 0.0f, seatPos.z - end.z);
    const float square = facing.x * facing.x + facing.z * facing.z;
    if (!(std::fabs(square - 1.0f) < 1e-08f)) {
        if (!(square < 1e-08f)) {
            const float scale = 1.0f / std::sqrt(square);
            facing = S_vector(scale * facing.x, 0.0f * scale, scale * facing.z);
        } else if (facing.x != 0.0f) {
            facing.x = facing.x < 0.0f ? -1.0f : 1.0f;
        } else if (facing.z != 0.0f) {
            facing.z = facing.z < 0.0f ? -1.0f : 1.0f;
        } else {
            facing.y = 1.0f;
        }
    }
    // (The w: a stale stack value in the original.)
    *dir = {facing.x, facing.y, facing.z, 0.0f};
    return true;
}

bool C_Submarine::PutOutside(C_Actor *actor)            // 0x005e0b30
{
    if (!actor)
        return false;
    const int seat = FindSeat(actor);
    if (seat == -1)
        return false;
    S_vector4 pos, dir;
    if (!FindExit(seat, &pos, &dir, false) && !FindExit(seat, &pos, &dir, true)) {
        int other = 0;
        for (;; ++other) {
            if (other >= SEAT_COUNT)
                return false;
            if (other != seat && (FindExit(other, &pos, &dir, true) || FindExit(other, &pos, &dir, false)))
                break;
        }
    }
    I3D_frame *frame = actor->GetFrame();
    frame->LinkTo(GetGame()->GetScene()->GetPrimarySector(), 0);
    actor->SetDirection4(dir);
    actor->SetPosition4(pos);
    frame->SetPos(S_vector(pos.x, pos.y, pos.z));
    frame->SetWorldDir(S_vector(dir.x, dir.y, dir.z), 0.0f);
    frame->Update();
    return true;
}

// --- the movement ---------------------------------------------------------------------------------

void C_Submarine::Tick(int timeDelta)                   // 0x005e0e00
{
    if (!IsClient() && IsServer())
        Update(timeDelta);
}

void C_Submarine::Update(int frameTime)                 // 0x005e0e20
{
    Bob(GetGame()->GetTime());
    ApplyControls(frameTime);
    Move(frameTime);
    if (m_camera)
        m_camera->Update(frameTime);
}

void C_Submarine::Bob(uint32_t time)                    // 0x005e14d0
{
    if (!IsServer() || !m_body)
        return;
    const float bobAngle = static_cast<float>(static_cast<double>(time) * 0.002f);
    m_bob = static_cast<float>(std::cos(static_cast<double>(bobAngle)) * 0.05f);
    const float rollAngle = static_cast<float>(static_cast<double>(time) * 0.001f);
    const float roll = static_cast<float>(std::sin(static_cast<double>(rollAngle)) * 0.01884955660992227);
    m_body->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), roll);
    m_body->SetPosInline(S_vector(m_bodyPos.x, m_bob + m_bodyPos.y, m_bodyPos.z));
    m_body->Update();
}

void C_Submarine::ApplyControls(int frameTime)          // 0x005e1d90
{
    const float time = static_cast<float>(static_cast<uint32_t>(frameTime)) * 0.001f;
    if (m_forward != 0.0f) {
        float speed = time * m_forward + m_speed;
        if (speed > MAX_SPEED)
            speed = MAX_SPEED;
        if (speed < -MAX_SPEED)
            speed = -MAX_SPEED;
        m_speed = speed;
    } else if (m_speed > 0.0f) {
        m_speed = m_speed - time;
        if (m_speed < 0.0f)
            m_speed = 0.0f;
    } else if (m_speed < 0.0f) {
        m_speed = time + m_speed;
        if (!(m_speed <= 0.0f))
            m_speed = 0.0f;
    }
    if (m_up != 0.0f) {
        float climb = time * m_up + m_climb;
        if (climb > MAX_CLIMB)
            climb = MAX_CLIMB;
        if (climb < -MAX_CLIMB)
            climb = -MAX_CLIMB;
        m_climb = climb;
    } else if (m_climb > 0.0f) {
        m_climb = m_climb - time;
        if (m_climb < 0.0f)
            m_climb = 0.0f;
    } else if (m_climb < 0.0f) {
        m_climb = time + m_climb;
        if (!(m_climb <= 0.0f))
            m_climb = 0.0f;
    }
    if (m_side != 0.0f) {
        m_turn = time * m_side * TURN_RATE + m_turn;
    } else if (m_turn > 0.0f) {
        m_turn = m_turn - time * TURN_DECAY;
        if (m_turn < 0.0f)
            m_turn = 0.0f;
    } else if (m_turn < 0.0f) {
        m_turn = time * TURN_DECAY + m_turn;
        if (!(m_turn <= 0.0f))
            m_turn = 0.0f;
    }
    float turn = m_turn > MAX_TURN ? MAX_TURN : m_turn;
    if (turn < -MAX_TURN)
        turn = -MAX_TURN;
    m_turn = turn;
}

void C_Submarine::UpdateVolumes()                       // 0x005e0e60
{
    // (Quirk kept: a box whose frame is not a dummy takes the centre found before it, unset in the
    // original for the first.)
    S_vector center(0.0f, 0.0f, 0.0f);
    for (size_t i = m_volumes.size(); i-- > 0;) {
        S_FrameVolume *volume = m_volumes[i];
        if (!volume || !volume->volume.frame)
            continue;
        I3D_frame *frame = volume->volume.frame;
        if (frame->GetType() == FRAME_DUMMY) {
            const I3D_bbox &box = static_cast<I3D_dummy *>(frame)->GetBBox();
            center = S_vector((box.min.x + box.max.x) * 0.5f, (box.max.y + box.min.y) * 0.5f, (box.max.z + box.min.z) * 0.5f);
            center *= frame->GetMatrix();
        }
        volume->invMatrix.Inverse(frame->GetMatrix());
        volume->volume.center = center;
        g_collisionWorld.MoveVolume(&volume->volume);
    }
}

void C_Submarine::SetVolumesActive(bool active)         // 0x005e1010
{
    if (m_volumesActive == active)
        return;
    for (S_FrameVolume *volume : m_volumes) {
        if (!volume)
            continue;
        if (active) {
            g_collisionWorld.AddVolume(&volume->volume);
            g_collisionWorld.MoveVolume(&volume->volume);
        } else {
            g_collisionWorld.RemoveVolume(&volume->volume);
        }
    }
    m_volumesActive = active;
}

bool C_Submarine::CheckCollision(float time)            // 0x005e1090
{
    if (!m_frame)
        return false;
    const S_matrix &world = m_frame->GetMatrix();
    const S_vector4 oldPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    const S_vector4 oldDir = FrameHeading(m_frame);
    m_frame->SetPos(GetPosition());
    m_frame->SetDir(GetDirection(), 0.0f);
    m_frame->Update();
    SetVolumesActive(false);
    // (Quirk kept: a box whose frame is not a dummy takes the centre found before it.)
    S_vector center(0.0f, 0.0f, 0.0f);
    for (size_t i = 1; i < m_volumes.size(); ++i) {
        S_FrameVolume *volume = m_volumes[i];
        if (!volume || !volume->volume.frame)
            continue;
        I3D_frame *frame = volume->volume.frame;
        if (frame->GetType() == FRAME_DUMMY) {
            const I3D_bbox &box = static_cast<I3D_dummy *>(frame)->GetBBox();
            center = S_vector((box.min.x + box.max.x) * 0.5f, (box.max.y + box.min.y) * 0.5f, (box.max.z + box.min.z) * 0.5f);
            center *= frame->GetMatrix();
        }
        // The box swept from where it was to its new place.
        const S_vector &from = volume->volume.center;
        S_vector move(center.x - from.x, center.y - from.y, center.z - from.z);
        S_vector hitPos, normal;
        if (!CollisionSphereTest(from, move, volume->volume.radius, &hitPos, &normal, SWEEP_TYPES, SWEEP_FLAGS))
            continue;
        // It turns away from the wall (by the speed), and goes back.
        normal.y = 0.0f;
        move.y = 0.0f;
        const float away = time * m_speed * TURN_RATE;
        if (!(move.z * normal.x - normal.z * move.x > 0.0f))
            m_turn = away + m_turn;
        else
            m_turn = m_turn - away;
        m_position = oldPos;
        m_direction = oldDir;
        m_frame->SetPos(GetPosition());
        m_frame->SetDir(GetDirection(), 0.0f);
        m_frame->Update();
        SetVolumesActive(true);
        return false;
    }
    SetVolumesActive(true);
    return true;
}

void C_Submarine::Move(int frameTime)                   // 0x005e15e0
{
    const float time = static_cast<float>(static_cast<uint32_t>(frameTime)) * 0.001f;
    if (!IsServer())
        return;
    if (m_speed != 0.0f || m_climb != 0.0f) {
        if (!m_frame)
            return;
        const S_matrix &world = m_frame->GetMatrix();
        m_position = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
        m_direction = FrameHeading(m_frame);
        // Level.
        m_direction.y = 0.0f;
        S_vector heading(m_direction.x, m_direction.y, m_direction.z);
        NormalizeBy(heading, (heading.x * heading.x + heading.y * heading.y) + heading.z * heading.z);
        m_direction = {heading.x, heading.y, heading.z, m_direction.w};
        const float step = time * m_speed;
        m_position.x = step * m_direction.x + m_position.x;
        m_position.y = step * m_direction.y + m_position.y;
        m_position.z = step * m_direction.z + m_position.z;
        if (m_turn != 0.0f) {
            const S_vector turned = GetDirection().RotateByMatrix(RotateYMatrix(time * m_turn * m_speed * TURN_SCALE));
            m_direction = {turned.x, turned.y, turned.z, m_direction.w};
        }
        if (m_climb != 0.0f) {
            m_position.y = time * m_climb + m_position.y;
            if (m_position.y > m_maxWaterLevel)
                m_position.y = m_maxWaterLevel;
        }
        // Blocked while turning: it turns by the speed's size instead and tries again.
        if (!CheckCollision(time) && m_turn != 0.0f) {
            const float angle = static_cast<float>(static_cast<double>(time * m_turn * std::fabs(m_speed)) * 0.20000000298023224);
            const S_vector turned = GetDirection().RotateByMatrix(RotateYMatrix(angle));
            m_direction = {turned.x, turned.y, turned.z, m_direction.w};
            CheckCollision(time);
        }
        m_frame->SetPos(GetPosition());
        m_frame->SetDir(GetDirection(), 0.0f);
        m_frame->Update();
        UpdateVolumes();
        if (std::fabs(m_speed) > MOVING_SPEED && m_propeller) {
            const S_quat rot = m_propeller->GetRot().RotateByMatrix(RotateZMatrix(m_speed * -0.04f));
            m_propeller->SetRot(rot);
            m_propeller->Update();
        }
    }
    // The engine's pitch by the speed; the water sounds when fast: at the surface (within 1 m of
    // the highest level) or below; climbing and diving.
    if (std::fabs(m_speed) > MOVING_SPEED) {
        if (SetSoundActive(m_engineSound, true))
            SetSoundFrequency(m_engineSound, (std::fabs(m_speed * 0.16666667f) + 1.0f) * 0.5f);
        if (std::fabs(m_speed) > FAST_SPEED) {
            if (std::fabs(m_position.y - m_maxWaterLevel) > SURFACE_RANGE) {
                SetSoundActive(m_surfaceSound, false);
                if (SetSoundActive(m_underwaterSound, true))
                    SetSoundFrequency(m_underwaterSound, std::fabs(m_speed * 0.16666667f) * 0.3f + 0.7f);
            } else {
                SetSoundActive(m_surfaceSound, true);
                SetSoundActive(m_underwaterSound, false);
            }
        } else {
            SetSoundActive(m_surfaceSound, false);
            SetSoundActive(m_underwaterSound, false);
        }
    } else {
        SetSoundActive(m_engineSound, false);
        SetSoundActive(m_surfaceSound, false);
        SetSoundActive(m_underwaterSound, false);
    }
    if (m_climb == 0.0f) {
        SetSoundActive(m_riseSound, false);
        SetSoundActive(m_diveSound, false);
    } else if (m_climb > 0.0f) {
        SetSoundActive(m_riseSound, true);
        SetSoundActive(m_diveSound, false);
    } else {
        SetSoundActive(m_riseSound, false);
        SetSoundActive(m_diveSound, true);
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_SubmarineCreator::Create()                   // 0x005de6c0
{
    return new C_Submarine();                           // _aligned_malloc(0x120, 16) in the original
}

void C_SubmarineCreator::Destroy(C_Actor *actor)        // 0x005de730
{
    delete actor;
}

static C_SubmarineCreator g_submarineCreator;           // 0x008ac9b8

// Registers the creator (static initialiser 0x005de6a0, thunk 0x005de690).
static const bool g_submarineCreatorRegistered = [] {
    g_actorCreators[ACTOR_SUBMARINE] = &g_submarineCreator;
    return true;
}();
