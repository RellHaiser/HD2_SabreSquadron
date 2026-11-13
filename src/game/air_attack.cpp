// C_AirAttack: see air_attack.h.
#include "game/air_attack.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/frame_box_volume.h"
#include "game/game.h"
#include "game/human.h"
#include "game/particles.h"                             // g_particlesModule
#include "game/pending.h"                               // RandomFloat, g_sightModule
#include "game/resource_cache.h"                        // g_soundCache
#include "game/shots.h"                                 // MATERIAL_STOP / MATERIAL_PASS
#include "game/world.h"                                 // GetCameraModule
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"                                // ISndGetDriver
#include "script/script.h"
#include "script/script_module.h"                       // g_scriptModule

namespace {

I3D_scene *Scene() { return GetGame()->GetScene(); }

constexpr uint16_t CHUNK_STATE = 1000;
constexpr uint16_t CHUNK_IDLE = 1500;
constexpr uint16_t CHUNK_IN_SIGHT = 2000;
constexpr uint16_t CHUNK_ROLLING = 2500;
constexpr uint16_t CHUNK_GONE = 3000;
constexpr uint16_t CHUNK_ROLL = 3500;
constexpr uint16_t CHUNK_SPEED = 4000;
constexpr uint16_t CHUNK_CLEARANCE = 4500;
constexpr uint16_t CHUNK_CEILING = 5000;
constexpr uint16_t CHUNK_HEALTH = 5500;
constexpr uint16_t CHUNK_FIRE_TIME = 6000;
constexpr uint16_t CHUNK_BULLETS = 6500;
constexpr uint16_t CHUNK_TARGET = 7000;
constexpr uint16_t CHUNK_DUST_TIME = 8000;
constexpr uint16_t PROP_EMPTY = 1;
constexpr uint16_t PROP_HIT_POINTS = 2;
constexpr uint16_t PROP_DAMAGE = 4;

constexpr int MATERIAL_DEEP_SAND = 42;                  // "Písek - moc" (shots and dust only show there; dust on MATERIAL_MINEFIELD too)
constexpr int EFFECT_SMOKE = 0x10;                      // shot down
constexpr int EFFECT_BULLET = 4;                        // a shot landing
constexpr int EFFECT_DUST = 0x3B;
constexpr int SHOT_INTERVAL = 25;                       // ms
constexpr int FLASH_TIME = 100;                         // ms
constexpr int DUST_INTERVAL = 3000;                     // ms
constexpr int DUST_TIME = 3000;                         // ms
constexpr float DUST_SPEED = 37.0f;                     // m/s
constexpr float MAX_SHOT_RANGE = 200.0f;                // m
constexpr float FIRE_RAY_SCALE = 1.2f;
constexpr uint32_t FIRE_EXCLUDE_FLAGS = 0x820000;
constexpr float LEAVE_SQUARE = 160000.0f;               // 400 m (x and z)
constexpr float GONE_SQUARE = 250000.0f;                // 500 m
constexpr float PASS_SQUARE = 2500.0f;                  // 50 m: over the target
constexpr float PASSED_SQUARE = 6400.0f;                // 80 m past it
constexpr float FIND_SQUARE = 22500.0f;                 // 150 m (FindAirAttackFor)
constexpr float GUN_SOUND_SQUARE = 160000.0f;           // 400 m
constexpr float WHIZZ_SQUARE = 100.0f;                  // 10 m from the camera
constexpr float DUST_CAMERA_SQUARE = 3600.0f;           // 60 m (x and z)
constexpr float PROPELLER_RATE = 0.002f;                // rad a ms
constexpr float ROLL_RATE = 0.003f;                     // rad a ms (the victory roll)
constexpr float BANK_RATE = 0.0005f;                    // rad a ms
constexpr float IDLE_TURN = 0.75f;                      // rad a second
constexpr float APPROACH_TURN = 0.4f;                   // rad a second
constexpr float PI = 3.14159274f;
constexpr float TWO_PI = 6.28318548f;

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

void NormalizeBy(S_vector4 &v, float square)
{
    S_vector u(v.x, v.y, v.z);
    NormalizeBy(u, square);
    v = {u.x, u.y, u.z, v.w};
}

float SquareXYZ(const S_vector4 &v) { return (v.x * v.x + v.y * v.y) + v.z * v.z; }

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

S_vector Vec(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }
S_vector4 Vec4(const S_vector &v, float w) { return {v.x, v.y, v.z, w}; }
float Time(int ms) { return static_cast<float>(static_cast<uint32_t>(ms)); }

// The frame's world position as a 4-vector (its matrix row).
S_vector4 WorldPos4(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

// A sound frame of the plane: `file` loaded, under the frame at its origin, positional, looping,
// off, from a random place (`positions` ms); null when it does not load.
I3D_sound *LoadPlaneSound(I3D_frame *frame, const char *file, float farRange, int positions)
{
    auto *sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    if (g_soundCache.OpenInto(sound, file) != 0) {
        sound->Release();
        return nullptr;
    }
    sound->LinkTo(frame, 0);
    sound->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    sound->SetSoundType(I3DSOUND_POINT);
    sound->SetLoop(true);
    sound->SetRange(20.0f, farRange, 0.3f, 0.3f);
    sound->SetOn(false);
    sound->Play(std::rand() % positions);
    return sound;
}

// A short sound at `pos` (under the primary sector): one of `pool` that has stopped, else a new
// one of `file` kept in it.
I3D_sound *PlayPooledSound(std::vector<I3D_sound *> &pool, const S_vector &pos, const char *pattern,
                           bool varyPitch)
{
    for (size_t i = pool.size(); i-- > 0;) {
        I3D_sound *sound = pool[i];
        if (!sound->IsPlaying(0)) {
            sound->SetPos(pos);
            sound->Play(0);
            sound->SetOn(true);
            sound->Update();
            return sound;
        }
    }
    auto *sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    const C_fstr file(pattern, std::rand() % 5 + 1);
    // (Not loaded: kept all the same.)
    g_soundCache.OpenInto(sound, file.c_str());
    sound->LinkTo(Scene()->GetPrimarySector(), 0);
    sound->SetPosInline(pos);
    sound->SetSoundType(I3DSOUND_POINT);
    sound->SetLoop(false);
    sound->SetRange(10.0f, 30.0f, 0.5f, 0.5f);
    sound->SetOn(true);
    sound->Play(0);
    if (varyPitch)
        sound->SetFrequency(1.5f - RandomFloat(1.0f));
    sound->Update();
    pool.push_back(sound);
    return sound;
}

// A motor sound started at a random pitch.
void StartMotor(I3D_sound *sound)
{
    sound->SetOn(true);
    sound->SetFrequency(1.25f - RandomFloat(0.5f));
    sound->Update();
}

}  // namespace

S_CollisionVolume *RayTestPastBorders(const S_vector4 &from, const S_vector4 &dir, S_vector4 *hitPos,
                                      S_vector4 *hitNormal, uint32_t excludeTypes,
                                      uint32_t excludeFlags)   // 0x005e2cc0
{
    S_vector start = Vec(from);
    S_vector ray = Vec(dir);
    const S_vector end(dir.x + from.x, from.y + dir.y, from.z + dir.z);
    S_vector n = ray;
    NormalizeBy(n, (n.y * n.y + n.z * n.z) + n.x * n.x);
    const S_vector step(n.x * 0.05f, n.y * 0.05f, n.z * 0.05f);
    // (The outputs start as the caller's and are written back whatever the result.)
    S_vector4 pos = hitPos ? *hitPos : S_vector4{};
    S_vector4 normal = hitNormal ? *hitNormal : S_vector4{};
    S_vector at, facing;
    S_CollisionVolume *hit = nullptr;
    for (;;) {
        at = Vec(pos);
        facing = Vec(normal);
        hit = CollisionRayTest(start, ray, &at, &facing, excludeTypes, excludeFlags);
        pos = {at.x, at.y, at.z, pos.w};
        normal = {facing.x, facing.y, facing.z, normal.w};
        if (!hit)
            break;
        const int material = GetVolumeMaterial(*hit);
        if (material != MATERIAL_STOP && material != MATERIAL_PASS)
            break;
        if (std::fabs((facing.y * ray.y + facing.z * ray.z) + facing.x * ray.x) < 0.001)
            break;
        start = S_vector(at.x + step.x, at.y + step.y, at.z + step.z);
        ray = S_vector(end.x - start.x, end.y - start.y, end.z - start.z);
        if ((ray.z * ray.z + ray.y * ray.y) + ray.x * ray.x < 0.04f) {
            hit = nullptr;
            break;
        }
    }
    if (hitPos)
        *hitPos = pos;
    if (hitNormal)
        *hitNormal = normal;
    return hit;
}

C_AirAttackCreator g_airAttackCreator;                  // 0x008ac9d4
std::vector<C_AirAttack *> g_airAttacks;                // 0x008ac9c4 (static initialiser 0x005e3a20)

// --- the dust ---------------------------------------------------------------------------------------

C_AirAttack::S_Dust::S_Dust(const S_vector4 &pos, const S_vector4 &d)   // 0x005e92b0
{
    frame = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    frame->LinkTo(Scene()->GetPrimarySector(), 0);
    frame->SetPosInline(Vec(pos));
    frame->SetOn(true);
    frame->Update();
    particle = g_particlesModule.CreateAt(EFFECT_DUST, Vec(pos), S_vector(0.0f, 0.0f, -1.0f), frame, true, true);
    time = DUST_TIME;
    dir = {d.x, 0.0f, d.z, d.w};
    NormalizeBy(dir, SquareXYZ(dir));
}

C_AirAttack::S_Dust::~S_Dust()                          // 0x005e94d0 (DS 0x00609260)
{
#ifndef HD2_DEDICATED   // no particles on the server
    if (particle != -1)
        g_particlesModule.Finish(particle);
#endif
    frame->Release();
}

bool C_AirAttack::S_Dust::Update(int frameTime)         // 0x005e94f0
{
    time -= frameTime;
    if (time <= 0)
        return false;
    const float t = static_cast<float>(frameTime);
    const S_vector move(dir.x * 0.001f * DUST_SPEED, dir.y * 0.001f * DUST_SPEED, dir.z * 0.001f * DUST_SPEED);
    const S_vector &at = frame->GetPos();
    const S_vector from(move.x * t + at.x, (move.y * t + at.y) + 10.0f, at.z + move.z * t);
    S_vector ground;
    S_CollisionVolume *hit = CollisionRayTest(from, S_vector(0.0f, -50.0f, 0.0f), &ground, nullptr, 0xFFFFFFFF, 0);
    if (!hit)
        return false;
    const int material = GetVolumeMaterial(*hit);
    if (material != MATERIAL_DEEP_SAND && material != MATERIAL_MINEFIELD)
        return false;
    frame->SetPosInline(S_vector(ground.x, ground.y + 2.0f, ground.z));
    frame->Update();
    return true;
}

// --- the actor --------------------------------------------------------------------------------------

C_AirAttack::C_AirAttack() : C_Actor(ACTOR_AIR_ATTACK)  // 0x005e3c90
{
    g_airAttacks.push_back(this);
}

C_AirAttack::~C_AirAttack()                             // 0x005e3f10
{
    for (size_t i = g_airAttacks.size(); i-- > 0;) {
        if (g_airAttacks[i] == this) {
            g_airAttacks[i] = g_airAttacks.back();
            g_airAttacks.pop_back();
            break;
        }
    }
    // (The lists' items are StopMission's.)
}

bool C_AirAttack::SetFrame(I3D_frame *frame)            // 0x005e4070
{
    m_hitPoints = 100.0f;
    m_damage = 70.0f;
    return C_Actor::SetFrame(frame);
}

void C_AirAttack::RestartMission(bool)                  // 0x005e3090 (DS 0x00601630)
{
    StopMission(true);
    StartMission(true);
}

void C_AirAttack::SetTarget(bool stop, I3D_frame *target)   // 0x005e7570
{
    m_speed = 100.0f;
    m_clearance = 30.0f;
    m_ceiling = 100.0f;
    if (!stop && (m_state == AIR_ATTACK_SHOT_DOWN || target)) {
        m_idle = false;
        m_state = AIR_ATTACK_PASSING;
        m_clearance = 35.0f;
        m_ceiling = 100.0f;
        m_target = target;
        return;
    }
    m_idle = true;
}

void C_AirAttack::StartMission(bool server)             // 0x005e4090 (DS 0x00602aa0)
{
    Activate(server);
    // The direction: the frame's z, levelled.
    const S_matrix &world = m_frame->GetMatrix();
    S_vector dir(world.m[2][0], world.m[2][1], world.m[2][2]);
    NormalizeBy(dir, (dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z);
    m_direction = {dir.x, 0.0f, dir.z, world.m[2][3]};
    NormalizeBy(m_direction, SquareXYZ(m_direction));
    if (m_started)
        return;
    m_started = true;
    m_targetInSight = false;
    m_rolling = false;
    m_gone = false;
    m_dustTime = 0;
    m_position = WorldPos4(m_frame);
    m_turnRate = 0.0f;
    m_health = m_hitPoints;
    m_climbRate = 0.0f;
    m_roll = 0.0f;
    m_fireTime = 0;
    m_smoke = -1;
    SetTarget(true, nullptr);
    // The blades and the wheels hidden; the flashes off.
    for (const char *name : {"listy", "carriage_l00", "carriage_r00"}) {
        if (I3D_frame *part = m_frame->FindChildFrame(name, FRAME_MASK_ANY))
            part->SetOn(false);
    }
    const auto addFlash = [this](I3D_frame *frame) {
        auto *flash = new S_Flash;
        flash->frame = frame;
        flash->time = -1;
        frame->SetOn(false);
        m_flashes.push_back(flash);
    };
    if (I3D_frame *flash = m_frame->FindChildFrame("bilboard", FRAME_MASK_ANY))
        addFlash(flash);
    // "bilboard_l0N" / "bilboard_r0N" (five times as large) from 0 while either is there.
    for (int i = 0;; ++i) {
        bool found = false;
        for (const char *pattern : {"bilboard_l0%d", "bilboard_r0%d"}) {
            I3D_frame *frame = m_frame->FindChildFrame(C_fstr(pattern, i).c_str(), FRAME_MASK_ANY);
            if (!frame)
                continue;
            frame->SetScale(S_vector(5.0f, 5.0f, 5.0f));
            addFlash(frame);
            found = true;
        }
        if (!found)
            break;
    }
    m_propeller = m_frame->FindChildFrame("bluur", FRAME_MASK_ANY);
    m_motorSound = nullptr;
    for (int i = 0; i < 4; ++i) {
        m_motorSounds[i] = LoadPlaneSound(m_frame, C_fstr("motor%d.wav", i + 1).c_str(), 500.0f, 5000);
        if (m_motorSounds[i]) {
            m_motorSounds[i]->SetFrequency(1.25f - RandomFloat(0.5f));
            m_motorSounds[i]->Update();
        }
    }
    m_gunSound = nullptr;
    for (int i = 0; i < 5; ++i) {
        m_gunSounds[i] = LoadPlaneSound(m_frame, C_fstr("kul_d%d.wav", i + 1).c_str(), 250.0f, 500);
        if (m_gunSounds[i])
            m_gunSounds[i]->Update();
    }
    // The box (its centre's w: the local centre's x, a stale local in the original).
    S_vector center;
    MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, COLLISION_TYPE_PIKE, this, nullptr, &center);
    m_volume.centerW = center.x;
    g_collisionWorld.AddVolume(&m_volume);
    m_target = nullptr;
#ifndef HD2_DEDICATED   // no sounds on the server
    // The shots' sounds into the cache.
    if (!ISndGetDriver()->IsInitialized())
        return;
    auto *sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    for (int i = 1; i < 6; ++i) {
        g_soundCache.OpenInto(sound, C_fstr("whizz%d.wav", i).c_str());
        g_soundCache.OpenInto(sound, C_fstr("sand%02d.wav", i).c_str());
    }
    sound->Release();
#endif
}

void C_AirAttack::StopMission(bool server)              // 0x005e56d0 (DS 0x00604510)
{
    Deactivate(server);
    if (!m_started)
        return;
    m_started = false;
    g_collisionWorld.RemoveVolume(&m_volume);
    if (m_smoke != -1) {
#ifndef HD2_DEDICATED   // no particles on the server (the id is still cleared)
        g_particlesModule.Destroy(m_smoke);
#endif
        m_smoke = -1;
    }
    for (I3D_sound *&sound : m_motorSounds) {
        if (sound) {
            sound->Release();
            sound = nullptr;
        }
    }
    for (I3D_sound *&sound : m_gunSounds) {
        if (sound) {
            sound->Release();
            sound = nullptr;
        }
    }
    // Fix: the original leaves the two pointers to the released sounds (the pause would call
    // through them).
    m_motorSound = nullptr;
    m_gunSound = nullptr;
    for (size_t i = m_bullets.size(); i-- > 0;)
        delete m_bullets[i];
    m_bullets.clear();
    for (size_t i = m_impactSounds.size(); i-- > 0;) {
        if (m_impactSounds[i])
            m_impactSounds[i]->Release();
    }
    m_impactSounds.clear();
    for (size_t i = m_whizzSounds.size(); i-- > 0;) {
        if (m_whizzSounds[i])
            m_whizzSounds[i]->Release();
    }
    m_whizzSounds.clear();
    for (size_t i = m_flashes.size(); i-- > 0;)
        delete m_flashes[i];
    m_flashes.clear();
    for (size_t i = m_dusts.size(); i-- > 0;)
        delete m_dusts[i];
    m_dusts.clear();
}

void C_AirAttack::Hit(E_HitType, float amount, const S_vector &, const S_vector &, C_Actor *, int)   // 0x005e3b80 (DS 0x00602440)
{
    if (m_health == 0.0f)
        return;
    m_health = m_health - amount;
    if (!(m_health < 0.0f))
        return;
    m_health = 0.0f;
    m_state = AIR_ATTACK_SHOT_DOWN;
    SetTarget(false, nullptr);
    // Smoke from a metre under it, pointing down.
    // (The original's matrix is otherwise uninitialised.)
    S_matrix smoke;
    smoke.Identity();
    smoke.SetDir3(S_vector(0.0f, -1.0f, 0.0f));
    const S_matrix &world = m_frame->GetMatrix();
    smoke.m[3][0] = world.m[3][0];
    smoke.m[3][1] = world.m[3][1] - 1.0f;
    smoke.m[3][2] = world.m[3][2];
#ifdef HD2_DEDICATED   // the server creates no particles (the id is -1)
    m_smoke = -1;
#else
    m_smoke = g_particlesModule.Create(EFFECT_SMOKE, smoke, m_frame, true, false);
#endif
    if (const S_FrameData *data = m_frame->GetData()) {
        if (data->script)
            data->script->OnDeath();
    }
}

void C_AirAttack::Update(int frameTime)                 // 0x005e5960 (DS 0x00604850)
{
    if (g_scriptModule.IsCutsceneRunning())
        return;
    if (!m_frame->IsOn())
        return;
    for (size_t i = m_dusts.size(); i-- > 0;) {
        if (!m_dusts[i]->Update(frameTime)) {
            delete m_dusts[i];
            m_dusts[i] = m_dusts.back();
            m_dusts.pop_back();
        }
    }
    if (m_gone)
        return;
    if (g_sightModule.GetStartDelay() > 2000)
        return;
    const float t = Time(frameTime);
    // The propeller turns about its z.
    if (m_propeller) {
        const S_quat rot = m_propeller->GetRot().RotateByMatrix(RotateZMatrix(t * PROPELLER_RATE));
        m_propeller->SetRot(rot);
        m_propeller->Update();
    }
    if (!m_motorSound) {
        m_motorSound = m_motorSounds[std::rand() % 4];
        if (m_motorSound)
            StartMotor(m_motorSound);
    }
    // The run.
    if (m_idle) {
        SetTurn(IDLE_TURN, 0.0f);
        m_targetInSight = false;
    } else {
        SetTurn(0.0f, 0.0f);
        S_vector toTarget(0.0f, 0.0f, 0.0f);
        if (m_target) {
            const S_matrix &t3 = m_target->GetMatrix();
            toTarget = S_vector(t3.m[3][0] - m_position.x, t3.m[3][1] - m_position.y, t3.m[3][2] - m_position.z);
        }
        const float flat = toTarget.x * toTarget.x + toTarget.z * toTarget.z;
        switch (m_state) {
        case AIR_ATTACK_LEAVING:
            m_targetInSight = false;
            if (m_rolling)
                m_roll = t * ROLL_RATE + m_roll;
            if (flat > LEAVE_SQUARE) {
                m_state = AIR_ATTACK_APPROACHING;
                m_clearance = 35.0f;
                m_ceiling = 100.0f;
                m_rolling = false;
                if (m_motorSound) {
                    m_motorSound->SetOn(false);
                    m_motorSound->Update();
                }
                m_motorSound = m_motorSounds[std::rand() & 3];
                if (m_motorSound)
                    StartMotor(m_motorSound);
            }
            break;
        case AIR_ATTACK_APPROACHING: {
            if (flat < PASS_SQUARE) {
                m_clearance = 25.0f;
                m_ceiling = 40.0f;
                m_state = AIR_ATTACK_PASSING;
            }
            const S_vector &at = m_target->GetWorldPos();
            const float heading[2] = {m_direction.x, m_direction.z};
            const float target2[2] = {at.x - m_position.x, at.z - m_position.z};
            const float angle = AngleBetween2D(heading, target2);
            NormalizeBy(m_direction, SquareXYZ(m_direction));
            // In sight: the first volume on the way past the borders is the target's frame.
            const S_vector &at2 = m_target->GetWorldPos();
            const S_vector4 delta = {at2.x - m_position.x, at2.y - m_position.y, at2.z - m_position.z, 0.0f};
            const S_CollisionVolume *seen = RayTestPastBorders(m_position, delta, nullptr, nullptr, 0, 0);
            m_targetInSight = seen && (seen->flags & 0xC0) && seen->frame == m_target;
            // (The original sets a turn rate from the angle and the distance first; this one
            // replaces it at once.)
            SetTurn(angle > 0.0f ? APPROACH_TURN : -APPROACH_TURN, 0.0f);
            break;
        }
        case AIR_ATTACK_PASSING:
            m_targetInSight = false;
            if (flat > PASSED_SQUARE) {
                m_state = AIR_ATTACK_LEAVING;
                if ((std::rand() & 3) == 0)
                    m_rolling = true;
            }
            break;
        case AIR_ATTACK_SHOT_DOWN:
            m_targetInSight = false;
            if (flat > GONE_SQUARE) {
                m_gone = true;
                if (m_smoke != -1) {
#ifndef HD2_DEDICATED   // no particles on the server (the id is still cleared)
                    g_particlesModule.Finish(m_smoke);
#endif
                    m_smoke = -1;
                }
                for (size_t i = m_dusts.size(); i-- > 0;)
                    delete m_dusts[i];
                m_dusts.clear();
            }
            break;
        default:
            break;
        }
    }
    // The ground under it (none: level flight); too low (under the ceiling or with the target in
    // sight): climbing by the slope.
    S_vector4 ground = {}, normal = {};
    const S_vector4 above = {m_position.x, m_position.y + 1000.0f, m_position.z, m_position.w};
    if (!RayTestPastBorders(above, {0.0f, -10000.0f, 0.0f, 0.0f}, &ground, &normal, 0xFFFFFFFF, 0)) {
        m_direction.y = 0.0f;
    } else {
        const float height = m_position.y - ground.y;
        if ((height <= m_ceiling || m_targetInSight) && height < m_clearance)
            m_climbRate = normal.y == 0.0f ? m_climbRate + 1.0f : 2.0f / normal.y + m_climbRate;
    }
    // What is ahead within the clearance: climbing too; nothing near ahead: back to level.
    const S_vector4 ahead = {m_direction.x * m_clearance, m_direction.y * m_clearance, m_direction.z * m_clearance,
                             0.0f};
    const auto level = [&](float upRate) {
        if (m_direction.y > 0.0f) {
            m_direction.y = m_direction.y - (t * upRate) / (1.0f - m_direction.y);
            if (m_direction.y < 0.0f)
                m_direction.y = 0.0f;
        } else if (m_direction.y < 0.0f) {
            m_direction.y = (t * 0.0015f) / (m_direction.y - 1.0f) + m_direction.y;
            if (m_direction.y > 0.0f)
                m_direction.y = 0.0f;
        }
    };
    if (!RayTestPastBorders(m_position, ahead, &ground, &normal, 0xFFFFFFFF, 0)) {
        level(0.0003f);
    } else {
        const float dx = m_position.x - ground.x, dy = m_position.y - ground.y, dz = m_position.z - ground.z;
        if (LengthOf((dx * dx + dz * dz) + dy * dy) >= m_clearance)
            level(0.001f);
        else
            m_climbRate = normal.y == 0.0f ? m_climbRate + 1.0f : 2.0f / normal.y + m_climbRate;
    }
    NormalizeBy(m_direction, SquareXYZ(m_direction));
    // The turn and the bank (to the turn rate; level again without one, unless rolling).
    const auto bankTo = [&](float target) {
        if (m_roll > target) {
            m_roll = m_roll - t * BANK_RATE;
            if (m_roll < target)
                m_roll = target;
        } else if (m_roll < target) {
            m_roll = t * BANK_RATE + m_roll;
            if (m_roll > target)
                m_roll = target;
        }
    };
    if (m_turnRate != 0.0f) {
        // (The original copies a stale stack word into w; w is kept here.)
        const S_vector turned = Vec(m_direction).RotateByMatrix(RotateYMatrix(t * m_turnRate * 0.001f));
        m_direction = {turned.x, turned.y, turned.z, m_direction.w};
        if (!m_rolling)
            bankTo(m_turnRate);
    } else if (!m_rolling) {
        bankTo(0.0f);
    }
    if (m_climbRate != 0.0f)
        m_direction.y = t * m_climbRate * 0.001f + m_direction.y;
    // With the target in sight: its height approached (0.001 a ms), the guns.
    if (m_targetInSight && m_target) {
        NormalizeBy(m_direction, SquareXYZ(m_direction));
        const S_vector &at = m_target->GetWorldPos();
        S_vector toTarget(at.x - m_position.x, at.y - m_position.y, at.z - m_position.z);
        NormalizeBy(toTarget, (toTarget.y * toTarget.y + toTarget.x * toTarget.x) + toTarget.z * toTarget.z);
        const float wanted = toTarget.y;
        if (wanted < m_direction.y) {
            const float y = m_direction.y - t * 0.001f;
            m_direction.y = y < wanted ? wanted : y;
        } else if (wanted > m_direction.y) {
            const float y = t * 0.001f + m_direction.y;
            m_direction.y = y > wanted ? wanted : y;
        }
        Fire(frameTime);
    }
    NormalizeBy(m_direction, SquareXYZ(m_direction));
    // The move.
    const float step = t * m_speed * 0.001f;
    m_position.x = step * m_direction.x + m_position.x;
    m_position.y = step * m_direction.y + m_position.y;
    m_position.z = step * m_direction.z + m_position.z;
    if (m_roll > PI)
        m_roll = m_roll - TWO_PI;
    m_frame->SetPos(Vec(m_position));
    m_frame->SetDir(Vec(m_direction), m_roll);
    m_frame->Update();
    const S_vector velocity((m_speed * m_direction.x) * 0.5f, (m_direction.y * m_speed) * 0.5f,
                            (m_direction.z * m_speed) * 0.5f);
    if (m_motorSound) {
        m_motorSound->SetVelocity(velocity);
        m_motorSound->Update();
    }
    // The guns' sound on the approach (a human target outdoors within 400 m); fading out else.
    if (!m_idle && m_state == AIR_ATTACK_APPROACHING) {
        if (!m_gunSound && m_target) {
            const S_vector &plane = m_frame->GetWorldPos();
            const S_vector &at = m_target->GetWorldPos();
            const float dx = plane.x - at.x, dy = plane.y - at.y, dz = plane.z - at.z;
            if ((dx * dx + dz * dz) + dy * dy < GUN_SOUND_SQUARE) {
                const S_FrameData *data = m_target->GetData();
                C_Actor *actor = data ? data->actor : nullptr;
                if (actor && actor->GetType() == ACTOR_HUMAN && actor->GetSector() == Scene()->GetPrimarySector()) {
                    m_gunSound = m_gunSounds[std::rand() % 5];
                    if (m_gunSound) {
                        m_gunSound->SetOn(true);
                        m_gunSound->SetVolume(1.0f);
                    }
                }
            }
        }
        if (m_gunSound)
            m_gunSound->SetVelocity(velocity);
    } else if (m_gunSound) {
        const float volume = m_gunSound->GetVolumeInline() - t * 0.004f;
        m_gunSound->SetVolume(volume);
        if (volume <= 0.0f) {
            m_gunSound->SetOn(false);
            m_gunSound->Update();
            m_gunSound = nullptr;
        }
    }
    if (m_gunSound)
        m_gunSound->Update();
    // The shots landing: a particle, a human on the line hit, sometimes an impact sound and a whizz
    // near the camera.
    for (size_t i = m_bullets.size(); i-- > 0;) {
        m_bullets[i]->time -= frameTime;
        if (m_bullets[i]->time >= 1)
            continue;
        const S_vector pos = Vec(m_bullets[i]->pos);
#ifndef HD2_DEDICATED   // no particles on the server
        g_particlesModule.CreateAt(EFFECT_BULLET, pos, S_vector(0.0f, 1.0f, 0.0f), nullptr, true, false);
#endif
        BulletHit(pos, Vec(m_bullets[i]->shot));
        if ((std::rand() & 3) == 0)
            PlayImpactSound(pos);
        delete m_bullets[i];
        m_bullets[i] = m_bullets.back();
        m_bullets.pop_back();
        if ((std::rand() & 3) == 0) {
            S_vector4 camera;
            GetGame()->GetCameraModule()->GetCameraPos(2, &camera);
            const float dx = pos.x - camera.x, dy = pos.y - camera.y, dz = pos.z - camera.z;
            if ((dx * dx + dz * dz) + dy * dy < WHIZZ_SQUARE) {
                const float a = RandomFloat(4.0f) - 2.0f;
                const float b = RandomFloat(4.0f) - 2.0f;
                const float c = RandomFloat(4.0f) - 2.0f;
                PlayWhizzSound(S_vector(c + camera.x, b + camera.y, a + camera.z));
            }
        }
    }
    // The box: quirk kept, its centre the frame's bound's (in the frame's space, the radius as w).
    const I3D_bound_volume &bound = m_frame->GetBound();
    m_invMatrix.Inverse(m_frame->GetMatrix());
    m_volume.center = bound.center;
    m_volume.centerW = bound.radius;
    g_collisionWorld.MoveVolume(&m_volume);
    for (size_t i = m_flashes.size(); i-- > 0;) {
        S_Flash *flash = m_flashes[i];
        if (flash->time < 0)
            continue;
        flash->time -= frameTime;
        if (flash->time < 0) {
            flash->frame->SetOn(false);
            flash->frame->Update();
        }
    }
}

void C_AirAttack::Tick(int timeDelta)                   // 0x005e71e0
{
    if (g_scriptModule.IsCutsceneRunning() || !m_frame->IsOn())
        return;
    if (m_dustTime != 0) {
        m_dustTime -= timeDelta;
        if (m_dustTime < 0)
            m_dustTime = 0;
    }
    if (m_gone || m_dustTime != 0)
        return;
    // Within 60 m of the camera (x and z) and 40 m over sand: dust 2 m over it.
    const S_vector &camera = Scene()->GetActiveCamera()->GetWorldPos();
    const float dx = camera.x - m_position.x, dz = camera.z - m_position.z;
    if (!(dx * dx + dz * dz < DUST_CAMERA_SQUARE))
        return;
    S_vector ground;
    S_CollisionVolume *hit =
        CollisionRayTest(Vec(m_position), S_vector(0.0f, -40.0f, 0.0f), &ground, nullptr, 0xFFFFFFFF, 0);
    if (!hit)
        return;
    const int material = GetVolumeMaterial(*hit);
    if (material != MATERIAL_DEEP_SAND && material != MATERIAL_MINEFIELD)
        return;
    AddDust({ground.x, ground.y + 2.0f, ground.z, 0.0f}, m_direction);
    m_dustTime = DUST_INTERVAL;
}

void C_AirAttack::AddDust(const S_vector4 &pos, const S_vector4 &dir)   // 0x005e7340
{
    m_dusts.push_back(new S_Dust(pos, dir));
}

void C_AirAttack::BulletHit(const S_vector &pos, const S_vector &shot)   // 0x005e3a80
{
    const S_vector from(pos.x - shot.x, pos.y - shot.y, pos.z - shot.z);
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    S_vector hitPos, hitNormal;
    S_CollisionVolume *hit = g_collisionWorld.RayTestDynamic(from, shot, &hitPos, &hitNormal, 0, 0);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    if (!hit)
        return;
    if (hit->actor && hit->actor->GetType() == ACTOR_HUMAN)
        hit->actor->Hit(HIT_BULLET, m_damage, hitPos, hitNormal, nullptr, 0);
    // (Then a gun sound starts if none plays.)
    if (!m_gunSound) {
        m_gunSound = m_gunSounds[std::rand() % 5];
        if (m_gunSound) {
            m_gunSound->SetOn(true);
            m_gunSound->SetVolume(1.0f);
        }
    }
}

void C_AirAttack::PlayImpactSound(const S_vector &pos)  // 0x005e75f0
{
    PlayPooledSound(m_impactSounds, pos, "sand%02d.wav", true);
}

void C_AirAttack::PlayWhizzSound(const S_vector &pos)   // 0x005e7bf0
{
    PlayPooledSound(m_whizzSounds, pos, "whizz%d.wav", false);
}

void C_AirAttack::Fire(int frameTime)                   // 0x005e81c0
{
    m_fireTime -= frameTime;
    while (m_fireTime < 0) {
        m_fireTime += SHOT_INTERVAL;
        // A flash (Fix: none without flashes; the original divides by their count).
        if (!m_flashes.empty()) {
            S_Flash *flash = m_flashes[static_cast<uint32_t>(std::rand()) % m_flashes.size()];
            flash->time = FLASH_TIME;
            flash->frame->SetOn(true);
            flash->frame->Update();
        }
        const S_vector &at = m_target->GetWorldPos();
        S_vector d(at.x - m_position.x, at.y - m_position.y, at.z - m_position.z);
        const float distance = LengthOf((d.x * d.x + d.z * d.z) + d.y * d.y);
        if (!(distance < MAX_SHOT_RANGE))
            continue;
        NormalizeBy(d, (d.x * d.x + d.z * d.z) + d.y * d.y);
        NormalizeBy(m_direction, SquareXYZ(m_direction));
        // Sideways: the direction crossed with one nearly straight up.
        S_vector up(d.x, 1000.0f, d.z);
        NormalizeBy(up, (up.z * up.z + up.x * up.x) + 1e+06f);
        S_vector side(up.z * d.y - d.z * up.y, d.z * up.x - up.z * d.x, d.x * up.y - up.x * d.y);
        NormalizeBy(side, (side.x * side.x + side.y * side.y) + side.z * side.z);
        S_vector flat(d.x, 0.0f, d.z);
        NormalizeBy(flat, flat.x * flat.x + flat.z * flat.z);
        // The aim: short of the target by 0.15 of (the distance - 125 + up to 50) along the level
        // direction, up to 3 m to either side; the ray 1.2 times as long, past the borders, at the
        // scene only.
        const S_vector4 target = WorldPos4(m_target);
        const float k = RandomFloat(50.0f) + (distance - 125.0f);
        const S_vector shortOf((flat.x * k) * 0.15f, (k * flat.y) * 0.15f, (flat.z * k) * 0.15f);
        S_vector4 aim = {target.x - shortOf.x, target.y - shortOf.y, target.z - shortOf.z, target.w};
        const float j = RandomFloat(6.0f) - 3.0f;
        const S_vector spread(side.x * j, side.y * j, side.z * j);
        S_vector4 shot = {(aim.x + spread.x) - m_position.x, (aim.y + spread.y) - m_position.y,
                          (aim.z + spread.z) - m_position.z, 0.0f};
        shot = {shot.x * FIRE_RAY_SCALE, shot.y * FIRE_RAY_SCALE, shot.z * FIRE_RAY_SCALE, shot.w};
        S_vector4 hitPos = {}, hitNormal = {};
        const S_CollisionVolume *hit =
            RayTestPastBorders(m_position, shot, &hitPos, &hitNormal, 0xFFFFFFFF, FIRE_EXCLUDE_FLAGS);
        const S_vector &now = m_target->GetWorldPos();
        const float mx = hitPos.x - now.x, my = hitPos.y - now.y, mz = hitPos.z - now.z;
        const float miss = LengthOf((mx * mx + mz * mz) + my * my);
        // Struck sand: it lands in (twice the miss distance) ms.
        if (!hit || GetVolumeMaterial(*hit) != MATERIAL_DEEP_SAND)
            continue;
        auto *bullet = new S_Bullet;
        bullet->pos = hitPos;
        bullet->time = static_cast<int>(miss + miss);
        bullet->shot = shot;
        m_bullets.push_back(bullet);
    }
}

bool C_AirAttack::WriteProperties(C_chunk &chunk)       // 0x005e8e10
{
    chunk.WriteChunk(PROP_HIT_POINTS, m_hitPoints);
    chunk.WriteChunk(PROP_DAMAGE, m_damage);
    return false;
}

bool C_AirAttack::ReadProperties(C_chunk &chunk)        // 0x005e9010
{
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case PROP_HIT_POINTS:
            // Fix: a short chunk keeps the value (the original takes an uninitialised local).
            chunk.Read(m_hitPoints);
            break;
        case PROP_DAMAGE:
            chunk.Read(m_damage);
            break;
        case PROP_EMPTY:
        default:
            break;
        }
        chunk.Ascend();
    }
    return true;
}

bool C_AirAttack::SaveGame(C_chunk &chunk)              // 0x005e30b0
{
    C_Actor::SaveGame(chunk);
    chunk.WriteChunk(CHUNK_STATE, m_state);
    const auto byte = [&](uint16_t id, bool value) {
        chunk.Begin(id);
        chunk.Write(value);
        chunk.Ascend();
    };
    byte(CHUNK_IDLE, m_idle);
    byte(CHUNK_IN_SIGHT, m_targetInSight);
    byte(CHUNK_ROLLING, m_rolling);
    byte(CHUNK_GONE, m_gone);
    chunk.WriteChunk(CHUNK_ROLL, m_roll);
    chunk.WriteChunk(CHUNK_SPEED, m_speed);
    chunk.WriteChunk(CHUNK_CLEARANCE, m_clearance);
    chunk.WriteChunk(CHUNK_CEILING, m_ceiling);
    chunk.WriteChunk(CHUNK_HEALTH, m_health);
    chunk.Begin(CHUNK_FIRE_TIME);
    chunk.Write(m_fireTime);
    chunk.Ascend();
    chunk.Begin(CHUNK_DUST_TIME);
    chunk.Write(m_dustTime);
    chunk.Ascend();
    if (m_target) {
        const char *name = m_target->GetName();
        chunk.Begin(CHUNK_TARGET);
        chunk.Write(name, static_cast<int>(std::strlen(name)) + 1);
        chunk.Ascend();
    }
    chunk.Begin(CHUNK_BULLETS);
    chunk.Write(static_cast<int>(m_bullets.size()));
    for (const S_Bullet *bullet : m_bullets) {
        chunk.Write(bullet->time);
        chunk.Write(&bullet->shot, 12);
        chunk.Write(&bullet->pos, 12);
    }
    chunk.Ascend();
    return true;
}

bool C_AirAttack::LoadGame(C_chunk &chunk)              // 0x005e3420 (DS 0x00601ab0)
{
    C_Actor::LoadGame(chunk);
    const auto floatLeaf = [&](float &value) {
        chunk.Read(value);
        chunk.Ascend();
    };
    const auto intLeaf = [&](int &value) {
        chunk.Read(value);
        chunk.Ascend();
    };
    const auto byteLeaf = [&](bool &value) {
        chunk.Read(value);
        chunk.Ascend();
    };
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_STATE:
            intLeaf(m_state);
            break;
        case CHUNK_IDLE:
            byteLeaf(m_idle);
            break;
        case CHUNK_IN_SIGHT:
            byteLeaf(m_targetInSight);
            break;
        case CHUNK_ROLLING:
            byteLeaf(m_rolling);
            break;
        case CHUNK_GONE:
            byteLeaf(m_gone);
            break;
        case CHUNK_ROLL:
            floatLeaf(m_roll);
            break;
        case CHUNK_SPEED:
            floatLeaf(m_speed);
            break;
        case CHUNK_CLEARANCE:
            floatLeaf(m_clearance);
            break;
        case CHUNK_CEILING:
            floatLeaf(m_ceiling);
            break;
        case CHUNK_HEALTH:
            floatLeaf(m_health);
            break;
        case CHUNK_FIRE_TIME:
            intLeaf(m_fireTime);
            break;
        case CHUNK_DUST_TIME:
            intLeaf(m_dustTime);
            break;
        case CHUNK_BULLETS: {
            int count = 0;
            chunk.Read(count);
            for (int i = 0; i < count; ++i) {
                auto *bullet = new S_Bullet;
                chunk.Read(bullet->time);
                chunk.Read(&bullet->shot, 12);
                chunk.Read(&bullet->pos, 12);
                m_bullets.push_back(bullet);
            }
            chunk.Ascend();
            break;
        }
        case CHUNK_TARGET: {
            // (C_chunk::ReadRemainingString: at most 999 characters, then ascends.) The frame by its
            // full name, else by its short one.
            char name[1000];
            chunk.ReadString(name, sizeof name);
            chunk.Ascend();
            m_target = I3DGetDriver()->FindFrame(Scene(), name);
            if (!m_target)
                m_target = Scene()->FindFrame(name, FRAME_MASK_ANY);
            break;
        }
        default:
            // Quirk kept: entered, not left.
            break;
        }
    }
    return true;
}

// --- the list -----------------------------------------------------------------------------------------

C_Actor *FindAirAttackFor(C_Human *human)               // 0x005e8b70
{
    const S_vector4 from = WorldPos4(human->GetBodyFrame());
    std::vector<C_AirAttack *> seen;
    for (size_t i = g_airAttacks.size(); i-- > 0;) {
        C_AirAttack *plane = g_airAttacks[i];
        if (plane->m_gone || (plane->m_state != C_AirAttack::AIR_ATTACK_APPROACHING &&
                              plane->m_state != C_AirAttack::AIR_ATTACK_PASSING))
            continue;
        const S_vector &at = plane->GetFrame()->GetWorldPos();
        const S_vector4 to = {at.x - from.x, at.y - from.y, at.z - from.z, 0.0f};
        S_vector4 hitPos = {}, hitNormal = {};
        const S_CollisionVolume *hit = RayTestPastBorders(from, to, &hitPos, &hitNormal, 0, 0);
        if (hit && hit->actor && hit->actor == plane)
            seen.push_back(plane);
    }
    C_Actor *nearest = nullptr;
    float best = FIND_SQUARE;
    for (size_t i = seen.size(); i-- > 0;) {
        const S_vector &at = seen[i]->GetFrame()->GetWorldPos();
        const float dx = at.x - from.x, dy = at.y - from.y, dz = at.z - from.z;
        const float square = (dz * dz + dy * dy) + dx * dx;
        if (square < best) {
            nearest = seen[i];
            best = square;
        }
    }
    return nearest;
}

void PauseAirAttackSounds(bool paused)                  // 0x005e8d50
{
    for (size_t i = g_airAttacks.size(); i-- > 0;) {
        C_AirAttack *plane = g_airAttacks[i];
        if (plane->m_motorSound)
            plane->m_motorSound->SetOn(!paused);
        if (plane->m_gunSound)
            plane->m_gunSound->SetOn(!paused);
        for (int k = 5; k-- > 0;) {
            if (plane->m_gunSounds[k])
                plane->m_gunSounds[k]->SetOn(!paused);
        }
        for (int k = 4; k-- > 0;) {
            if (plane->m_motorSounds[k])
                plane->m_motorSounds[k]->SetOn(!paused);
        }
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_AirAttackCreator::Create()                   // 0x005e2c30
{
    return new C_AirAttack();                           // _aligned_malloc(0x1F0, 16) in the original
}

void C_AirAttackCreator::Destroy(C_Actor *actor)        // 0x005e2ca0
{
    delete actor;
}

// Registers the creator (static initialiser 0x005e2c10, thunk 0x005e2c00). (0x005ea2d0: another
// static guard of this file, nothing to port.)
static const bool g_airAttackCreatorRegistered = [] {
    g_actorCreators[ACTOR_AIR_ATTACK] = &g_airAttackCreator;
    return true;
}();
