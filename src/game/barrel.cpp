// C_Barrel: see barrel.h.
#include "game/barrel.h"

#include <cmath>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage, SendActorMessage
#include "game/frame_box_volume.h"
#include "game/game.h"
#include "game/particles.h"                             // g_particlesModule
#include "game/pending.h"                               // g_gameRole
#include "game/resource_cache.h"                        // g_modelCache
#include "game/shots.h"                                 // GetFrameMaterial
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

// Messages (3 bits: floor(log2 4) + 1, computed at run time in the original).
constexpr int MSG_BITS = 3;
enum : uint32_t {
    MSG_HIT_BODY = 0,
    MSG_BUMP = 1,
    MSG_DESTROYED = 2,
    MSG_HIT_SURFACE = 3,
};

enum : uint16_t {
    PROP_SURFACE = 2000,
    PROP_FULL_LEVEL = 3000,
    PROP_EMPTY_LEVEL = 4000,
    PROP_LEAK_RATE = 5000,
    PROP_LEAK_EFFECT = 6000,
    PROP_BODY = 7000,
    PROP_DESTROYED_MODEL = 8000,
    PROP_HIT_POINTS = 9000,
    PROP_VOLUME_OUT = 10000,
    PROP_EXPLOSION_EFFECT = 11000,
    PROP_EXPLOSION_LEVEL = 12000,
    PROP_EXIT_HOLES = 13000,
};
enum : uint16_t {
    CHUNK_SAVE = 1,
    CHUNK_EMPTY = 1000,
    CHUNK_LEVEL = 2000,
    CHUNK_HEALTH = 3000,
};

constexpr int MOVE_TIME = 400;                          // ms: the body's tip each way, the slosh's start
constexpr float MOVE_RATE = 0.0025f;                    // 1 / 400
constexpr float BODY_TIP = 0.0872664601f;               // rad (5 degrees)
constexpr float SLOSH_TILT = 0.0174532924f;             // rad (1 degree) either way
constexpr float SLOSH_DAMPING = 0.0001f;                // a ms
constexpr float SLOSH_RATE = 0.00392699009f;            // rad a ms (pi / 800)
constexpr float SLOSH_TURN = 0.00012f;                  // rad a ms about y
constexpr float SLOSH_MAX = 2.5f;
constexpr float HOLE_STOP_BELOW = 0.09f;                // a hole this near the level stops leaking
constexpr uint32_t FIND_PART_MASK = FRAME_MASK_ANY;

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

// Where the line through `origin` along `dir` meets the upright cylinder of `radius` about the
// space's y axis (in the xz plane; 0x006083f0): `a` the far side along `dir`, `b` the near.
void CylinderHits(const S_vector &origin, const S_vector &dir, float radius, S_vector &a, S_vector &b)
{
    const float ox = -origin.x;
    const float oz = -origin.z;
    const float inv = 1.0f / LengthOf(dir.z * dir.z + dir.x * dir.x);
    const float t = ((dir.z * inv) * oz + ox * (dir.x * inv)) + (inv * 0.0f) * 0.0f;
    const float h = std::sqrt(radius * radius - ((oz * oz + ox * ox) - t * t));
    const float ta = (t + h) * inv;
    const float tb = (t - h) * inv;
    a = S_vector(ta * dir.x + origin.x, ta * dir.y + origin.y, ta * dir.z + origin.z);
    b = S_vector(tb * dir.x + origin.x, tb * dir.y + origin.y, tb * dir.z + origin.z);
}

// The direction leveled (y 0) and normalized (the square summed z, x).
void Level(S_vector &dir)
{
    dir.y = 0.0f;
    NormalizeBy(dir, dir.z * dir.z + dir.x * dir.x);
}

float ToFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

uint32_t ToBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

void WriteVector(C_BitStream *msg, const S_vector &v)
{
    msg->WriteBits(ToBits(v.x), 32);
    msg->WriteBits(ToBits(v.y), 32);
    msg->WriteBits(ToBits(v.z), 32);
}

S_vector ReadVector(C_BitStream *msg)
{
    const float x = ToFloat(msg->ReadBits(32));
    const float y = ToFloat(msg->ReadBits(32));
    const float z = ToFloat(msg->ReadBits(32));
    return S_vector(x, y, z);
}

S_matrix InverseOf(const I3D_frame *frame)
{
    S_matrix inverse;
    inverse.Inverse(frame->GetMatrix());
    return inverse;
}

// An int / float leaf: read, then left (0x0053b1d0 / 0x00482170). (DS 0x0052eda0)
int ReadIntLeaf(C_chunk &chunk, int value)
{
    chunk.Read(value);
    chunk.Ascend();
    return value;
}

float ReadFloatLeaf(C_chunk &chunk, float value)
{
    chunk.Read(value);
    chunk.Ascend();
    return value;
}

// The rest of the chunk as a string, then left (C_chunk::ReadRemainingString 0x00415d00: 999
// characters at most).
C_str ReadStringLeaf(C_chunk &chunk)
{
    char text[1000];
    chunk.ReadString(text, sizeof text);
    chunk.Ascend();
    return C_str(text);
}

}  // namespace

C_BarrelCreator g_barrelCreator;                        // 0x008aca24

C_Barrel::C_Barrel() : C_Actor(ACTOR_BARREL)            // 0x00606380
{
}

C_Barrel::~C_Barrel() = default;                        // 0x00606490 (the holes are StopMission's)

void C_Barrel::Tick(int timeDelta)                      // 0x006071c0
{
    if (!IsClient())
        Animate(timeDelta);
}

void C_Barrel::CopyFrom(const C_Actor *source)          // 0x00606520
{
    C_Actor::CopyFrom(source);
    const auto *barrel = static_cast<const C_Barrel *>(source);
    m_level = barrel->m_level;
    m_fullLevel = barrel->m_fullLevel;
    m_emptyLevel = barrel->m_emptyLevel;
    m_surfaceName = barrel->m_surfaceName;
    m_leakRate = barrel->m_leakRate;
    m_leakEffect = barrel->m_leakEffect;
    m_bodyName = barrel->m_bodyName;
    m_destroyedModel = barrel->m_destroyedModel;
    m_hitPoints = barrel->m_hitPoints;
    m_explosionLevel = barrel->m_explosionLevel;
    m_volumeOutWhenDestroyed = barrel->m_volumeOutWhenDestroyed;
    m_explosionEffect = barrel->m_explosionEffect;
    m_exitHoles = barrel->m_exitHoles;
}

void C_Barrel::StartMission(bool server)                // 0x00606650 (DS 0x0062dc30)
{
    Activate(server);
    if (server) {
        MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, COLLISION_TYPE_BARREL, this);
        g_collisionWorld.AddVolume(&m_volume);
        g_collisionWorld.MoveVolume(&m_volume);
    }
    m_bodyTipTime = 0;
    m_sloshing = false;
    m_level = m_fullLevel;
    m_empty = false;
    m_surface = m_frame->FindChildFrame(m_surfaceName.c_str(), FIND_PART_MASK);
    m_body = m_frame->FindChildFrame(m_bodyName.c_str(), FIND_PART_MASK);
    m_bodyTipping = false;
    if (m_body) {
        m_bodyRot = m_body->GetRot();
        m_bodyPos = m_body->GetPos();
    }
    if (m_surface)
        m_surfaceRot = m_surface->GetRot();
    m_destroyed = nullptr;
    m_health = static_cast<float>(m_hitPoints);
    SetSurfaceLevel();
    // The radius: half the diagonal of the box's xz square.
    I3D_bbox hierarchy;
    const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, m_frame);
    const float dx = box.min.x - box.max.x;
    const float dz = box.min.z - box.max.z;
    const float diagonal = LengthOf(dz * dz + dx * dx);
    m_radius = static_cast<float>(static_cast<double>(std::sqrt(diagonal * diagonal * 0.5f)) * 0.5);
}

void C_Barrel::SetSurfaceLevel()                        // 0x00606b10
{
    if (!m_surface)
        return;
    const S_vector &pos = m_surface->GetPos();
    m_surface->SetPosInline(S_vector(pos.x, m_level, pos.z));
    m_surface->Update();
}

void C_Barrel::StopMission(bool server)                 // 0x00606bb0
{
    Deactivate(server);
    if (server && m_frame->IsOn())
        g_collisionWorld.RemoveVolume(&m_volume);
    m_frame->SetOn(true);
    m_frame->Update();
    if (m_destroyed) {
        m_destroyed->LinkTo(nullptr, 0);
        m_destroyed->Update();
        m_destroyed->Release();
        m_destroyed = nullptr;
    }
    if (m_body) {
        m_body->SetRot(m_bodyRot);
        m_body->SetPosInline(m_bodyPos);
        m_body->Update();
        m_body = nullptr;
    }
    if (m_surface) {
        // (Not updated.)
        m_surface->SetRot(m_surfaceRot);
        m_surface = nullptr;
    }
    for (size_t i = m_holes.size(); i-- > 0;)
        delete m_holes[i];
    m_holes.clear();
}

void C_Barrel::RestartMission(bool)                     // 0x00606340
{
    StopMission(false);
    StartMission(false);
}

void C_Barrel::Animate(int time)                        // 0x00606da0
{
    // The body tips over 400 ms and back over 400 ms.
    if (m_bodyTipping) {
        m_bodyTipTime -= time;
        float t;
        if (!m_bodyTippingBack) {
            if (m_bodyTipTime < 0) {
                m_bodyTipTime = MOVE_TIME;
                m_bodyTippingBack = true;
                t = 1.0f;
            } else {
                t = 1.0f - static_cast<float>(m_bodyTipTime) * MOVE_RATE;
            }
        } else {
            if (m_bodyTipTime < 0) {
                m_bodyTipTime = 0;
                m_bodyTipping = false;
            }
            t = static_cast<float>(m_bodyTipTime) * MOVE_RATE;
        }
        m_body->SetRot(m_bodyRot.Slerp(m_bodyTipRot, t, false));
        m_body->Update();
    }
    // The surface sways between its two extremes, dying down; it stops as the sway crosses half
    // way once damped.
    if (m_sloshing) {
        m_sloshTime -= time;
        if (!m_sloshDamped)
            m_sloshAmount = m_sloshAmount - static_cast<float>(time) * SLOSH_DAMPING;
        if (m_sloshAmount <= 0.0f) {
            m_sloshDamped = true;
            m_sloshAmount = SLOSH_DAMPING;
        }
        float c = std::cos(static_cast<float>(m_sloshTime) * SLOSH_RATE);
        if (m_sloshDamped && ((m_sloshPhase <= 0.5f && c >= 0.5f) || (m_sloshPhase >= 0.5f && c <= 0.5f))) {
            c = 0.0f;
            m_sloshing = false;
        }
        m_sloshPhase = c;
        const S_quat sway = m_sloshFrom.Slerp(m_sloshTo, c * m_sloshAmount, false);
        // (Barrels of odd and even ids turn opposite ways.)
        const int side = m_netId % 2 != 0 ? 1 : -1;
        const S_matrix turn = RotateYMatrix(static_cast<float>(side) * static_cast<float>(m_sloshTime - MOVE_TIME) * SLOSH_TURN);
        m_surface->SetRot(sway.RotateByMatrix(turn));
        m_surface->Update();
    }
    // The leak: holes at or over the level stop; each other one lowers it at the leak rate.
    if (!m_empty || !m_holes.empty()) {
        for (size_t i = m_holes.size(); i-- > 0;) {
            if (m_level - HOLE_STOP_BELOW < m_holes[i]->height) {
                g_particlesModule.Finish(m_holes[i]->particle);
                delete m_holes[i];
                m_holes.erase(m_holes.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        const float holes = static_cast<float>(static_cast<uint32_t>(m_holes.size()));
        m_level = m_level - holes * m_leakRate * static_cast<float>(time) * 0.001f;
        if (m_level <= m_emptyLevel) {
            m_empty = true;
            m_level = m_emptyLevel;
        }
        SetSurfaceLevel();
    }
}

void C_Barrel::Slosh(const S_vector &pos, const S_vector &dir, bool tipBody)   // 0x00607410
{
    if (tipBody) {
        m_bodyTipping = true;
        m_bodyTippingBack = false;
        m_bodyTipRot = m_bodyRot;
        // (The hit's direction crossed with y, the zero terms kept.)
        const S_vector axis(pos.y * 0.0f - pos.z, pos.z * 0.0f - pos.x * 0.0f, pos.x - pos.y * 0.0f);
        m_bodyTipTime = MOVE_TIME;
        S_quat tip;
        tip.Make(axis, BODY_TIP);
        m_bodyTipRot = m_bodyTipRot.RotateByMatrix(tip.RotationMatrix());
    }
    if (!m_surface)
        return;
    const S_vector axis(dir.y * 0.0f - dir.z, dir.z * 0.0f - dir.x * 0.0f, dir.x - dir.y * 0.0f);
    m_sloshTime = MOVE_TIME;
    m_sloshFrom = m_surface->GetRot();
    S_vector level = m_sloshFrom.GetDir();
    Level(level);
    m_sloshFrom.SetDir(level, 0.0f);
    m_sloshTo = m_surface->GetRot();
    level = m_sloshTo.GetDir();
    Level(level);
    m_sloshTo.SetDir(level, 0.0f);
    S_quat tilt;
    tilt.Make(axis, SLOSH_TILT);
    m_sloshFrom = m_sloshFrom.RotateByMatrix(tilt.RotationMatrix());
    tilt.Make(axis, -SLOSH_TILT);
    m_sloshTo = m_sloshTo.RotateByMatrix(tilt.RotationMatrix());
    if (!(m_sloshAmount < 0.1f))
        m_sloshAmount = RandFloat() * 0.2f + m_sloshAmount;
    else
        m_sloshAmount = 1.0f - RandFloat() * 0.2f;
    if (!(m_sloshAmount < SLOSH_MAX))
        m_sloshAmount = SLOSH_MAX;
    m_sloshing = true;
    m_sloshPhase = 0.0f;
    m_sloshDamped = false;
}

void C_Barrel::Destroy(bool loading)                    // 0x00607930
{
    m_health = 0.0f;
    m_empty = true;
    for (size_t i = m_holes.size(); i-- > 0;)
        g_particlesModule.Destroy(m_holes[i]->particle);
    for (size_t i = m_holes.size(); i-- > 0;)
        delete m_holes[i];
    m_holes.clear();
    m_bodyTipping = false;
    // The destroyed model (its file without the extension; Fix: not copied into a 48-byte buffer).
    std::string file = m_destroyedModel.c_str();
    if (!file.empty()) {
        const size_t dot = file.rfind('.');
        if (dot != std::string::npos)
            file.resize(dot);
        m_destroyed = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
        if (g_modelCache.OpenInto(m_destroyed, file.c_str()) < 0) {
            m_destroyed->Release();
            m_destroyed = nullptr;
        } else {
            m_destroyed->LinkTo(m_frame->GetParent(), 0);
            m_destroyed->SetPosInline(m_frame->GetPos());
            m_destroyed->SetRot(m_frame->GetRot());
            m_destroyed->SetOn(true);
            const std::string name = std::string(m_frame->GetName()) + "_destroyed";
            m_destroyed->SetName(name.c_str());
            m_destroyed->Update();
        }
    }
    m_frame->SetOn(false);
    m_frame->Update();
    if (m_volumeOutWhenDestroyed && IsServer())
        g_collisionWorld.RemoveVolume(&m_volume);
    if (loading)
        return;
    if (IsServer()) {
        if (const S_FrameData *data = m_frame->GetData()) {
            if (data->script)
                data->script->OnDeath();
        }
    }
    if (IsClient()) {
        const S_vector up(0.0f, 1.0f, 0.0f);
        g_particlesModule.CreateAt(m_explosionEffect, m_frame->GetWorldPos(), up, nullptr, true, false);
    }
}

void C_Barrel::AddHole(const S_vector &pos, float height)   // 0x00608150
{
    const S_vector &at = m_frame->GetWorldPos();
    const S_vector out(pos.x - at.x, 0.0f, pos.z - at.z);
    auto *hole = new S_Hole;
    hole->height = height;
    hole->particle = g_particlesModule.CreateAt(m_leakEffect, pos, out, m_frame, true, false);
    for (size_t i = 0; i < m_holes.size(); ++i) {
        if (height < m_holes[i]->height) {
            m_holes.insert(m_holes.begin() + static_cast<std::ptrdiff_t>(i), hole);
            return;
        }
    }
    m_holes.push_back(hole);
}

bool C_Barrel::IsLeakHeight(float height) const         // 0x006083c0
{
    return height < m_level && height > m_emptyLevel;
}

void C_Barrel::HitBody(const S_vector &pos, const S_vector &dir)   // 0x00608590
{
    if (m_empty)
        return;
    // (Heights in the surface's parent's space, else the frame's.)
    const I3D_frame *space = m_surface ? m_surface->GetParent() : m_frame;
    const S_matrix inverse = InverseOf(space);
    const S_vector local = pos * inverse;
    if (IsLeakHeight(local.y))
        AddHole(pos, local.y);
    S_vector d = dir;
    NormalizeBy(d, SquareYZX(d));
    if (m_exitHoles) {
        // The exit hole: the far side of the barrel along the shot.
        const S_vector &at = m_frame->GetWorldPos();
        S_vector through = S_vector(at.x + d.x, d.y + at.y, d.z + at.z) * inverse;
        NormalizeBy(through, SquareYZX(through));
        S_vector a, b;
        CylinderHits(local, through, m_radius, a, b);
        const float da = ((local.x - a.x) * (local.x - a.x) + (local.y - a.y) * (local.y - a.y)) +
                         (local.z - a.z) * (local.z - a.z);
        const float db = ((local.x - b.x) * (local.x - b.x) + (local.y - b.y) * (local.y - b.y)) +
                         (local.z - b.z) * (local.z - b.z);
        const S_vector &exit = da <= db ? b : a;
        if (IsLeakHeight(exit.y))
            AddHole(exit * m_frame->GetMatrix(), exit.y);
    }
    Slosh(d, d, false);
}

void C_Barrel::HitSurface(const S_vector &pos, const S_vector &dir)   // 0x00608a30
{
    // (The original passes a stack leftover as the position; unused without tipping.)
    Slosh(dir, dir, false);
    if (!m_exitHoles)
        return;
    const S_matrix inverse = InverseOf(m_frame);
    const S_vector local = pos * inverse;
    S_vector d = dir;
    NormalizeBy(d, SquareYZX(d));
    const S_vector &at = m_frame->GetWorldPos();
    S_vector through = S_vector(at.x + d.x, d.y + at.y, d.z + at.z) * inverse;
    NormalizeBy(through, SquareYZX(through));
    S_vector a, b;
    CylinderHits(local, through, m_radius, a, b);
    if (IsLeakHeight(a.y)) {
        AddHole(a * m_frame->GetMatrix(), a.y);
        return;
    }
    if (IsLeakHeight(b.y))
        AddHole(b * m_frame->GetMatrix(), b.y);
}

C_BitStream *C_Barrel::CreateMessage(uint32_t kind)
{
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(kind, MSG_BITS);
    return msg;
}

void C_Barrel::Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *, int part)   // 0x00608e40
{
    if (m_health > 0.0f) {
        m_health = m_health - amount;
        if (m_health <= 0.0f) {
            m_health = 0.0f;
            // Destroyed only while the level is over the explosion level.
            if (m_explosionLevel < m_level) {
                if (!IsClient()) {
                    Destroy(false);
                    return;
                }
                SendActorMessage(CreateMessage(MSG_DESTROYED));
                return;
            }
        }
    }
    if (type != HIT_BULLET && type != HIT_EXPLOSION)
        return;
    if (m_empty)
        return;
    if (!m_surface || reinterpret_cast<intptr_t>(m_surface) != part) {
        if (type != HIT_BULLET)
            return;
        if (!IsClient()) {
            HitBody(pos, dir);
            return;
        }
        C_BitStream *msg = CreateMessage(MSG_HIT_BODY);
        WriteVector(msg, pos);
        WriteVector(msg, dir);
        SendActorMessage(msg);
        return;
    }
    S_vector d = dir;
    NormalizeBy(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
    if (!IsClient()) {
        HitSurface(pos, d);
        return;
    }
    C_BitStream *msg = CreateMessage(MSG_HIT_SURFACE);
    WriteVector(msg, pos);
    WriteVector(msg, d);
    SendActorMessage(msg);
}

void C_Barrel::OnMessage(C_BitStream *msg)              // 0x00609220
{
    switch (msg->ReadBits(MSG_BITS)) {
    case MSG_HIT_BODY: {
        const S_vector pos = ReadVector(msg);
        const S_vector dir = ReadVector(msg);
        HitBody(pos, dir);
        break;
    }
    case MSG_BUMP: {
        const bool tip = msg->ReadBits(1) != 0;
        S_vector pos(0.0f, 0.0f, 0.0f);                 // (a stack leftover without the tip; unused)
        if (tip)
            pos = ReadVector(msg);
        const S_vector dir = ReadVector(msg);
        Slosh(pos, dir, tip);
        break;
    }
    case MSG_DESTROYED:
        Destroy(false);
        break;
    case MSG_HIT_SURFACE: {
        const S_vector pos = ReadVector(msg);
        const S_vector dir = ReadVector(msg);
        HitSurface(pos, dir);
        break;
    }
    default:
        break;
    }
}

int C_Barrel::GetMaterialId(I3D_frame *part)            // 0x006094f0
{
    const int id = GetFrameMaterial(part);
    return id == -1 ? 11 : id;
}

bool C_Barrel::OnCollisionEx(C_Actor *other, const S_vector *, int, int, int, int, int)   // 0x00609510
{
    if (!m_body || m_bodyTipping)
        return false;
    // The body tips away from the other actor; the surface sloshes away from it too.
    const S_vector from = other->GetFrame()->GetWorldPos();
    const S_vector &body = m_body->GetWorldPos();
    S_vector away(body.x - from.x, body.y - from.y, body.z - from.z);
    away.y = 0.0f;
    NormalizeBy(away, away.x * away.x + away.z * away.z);
    const S_vector pos = S_vector(away.x + body.x, away.y + body.y, away.z + body.z) * InverseOf(m_body);
    S_vector dir = from;                                // (without a surface: the other actor's position)
    if (m_surface) {
        const S_vector &surface = m_surface->GetWorldPos();
        S_vector toward(surface.x - from.x, 0.0f, surface.z - from.z);
        NormalizeBy(toward, toward.z * toward.z + toward.x * toward.x);
        dir = S_vector(toward.x + surface.x, toward.y + surface.y, toward.z + surface.z) * InverseOf(m_surface);
    }
    if (!IsClient()) {
        Slosh(pos, dir, true);
        return true;
    }
    C_BitStream *msg = CreateMessage(MSG_BUMP);
    msg->WriteBits(1, 1);
    WriteVector(msg, pos);
    WriteVector(msg, dir);
    SendActorMessage(msg);
    return true;
}

bool C_Barrel::SaveGame(C_chunk &chunk)                 // 0x00609a30
{
    chunk.Begin(CHUNK_SAVE);
    C_Actor::SaveGame(chunk);
    chunk.Begin(CHUNK_EMPTY);
    chunk.Write(m_empty);
    chunk.Ascend();
    // (While it leaks: the lowest hole's height, what the level sinks to.)
    const float level = m_holes.empty() ? m_level : m_holes[0]->height;
    chunk.Begin(CHUNK_LEVEL);
    chunk.Write(level);
    chunk.Ascend();
    if (m_hitPoints > 0) {
        const int health = static_cast<int>(m_health);
        chunk.Begin(CHUNK_HEALTH);
        chunk.Write(health);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

bool C_Barrel::LoadGame(C_chunk &chunk)                 // 0x00609c70
{
    uint16_t id = 0;
    if (chunk.Descend(id) && id == CHUNK_SAVE) {
        C_Actor::LoadGame(chunk);
        while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
            switch (id) {
            case CHUNK_EMPTY:
                chunk.Read(m_empty);
                chunk.Ascend();
                break;
            case CHUNK_LEVEL: {
                // (Short: the original keeps a stack leftover; the level stays here.)
                float level = m_level;
                chunk.Read(level);
                chunk.Ascend();
                m_level = level;
                SetSurfaceLevel();
                break;
            }
            case CHUNK_HEALTH: {
                int health = static_cast<int>(m_health);
                chunk.Read(health);
                chunk.Ascend();
                m_health = static_cast<float>(health);
                if (m_health == 0.0f)
                    Destroy(true);
                break;
            }
            default:
                chunk.Ascend();
                break;
            }
        }
    }
    chunk.Ascend();
    return true;
}

bool C_Barrel::ReadProperties(C_chunk &chunk)           // 0x0060a280
{
    // (The first chunk is entered whatever its id; the properties are inside it.)
    uint16_t id;
    chunk.Descend(id);
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case PROP_SURFACE:
            m_surfaceName = ReadStringLeaf(chunk);
            break;
        case PROP_FULL_LEVEL:
            m_fullLevel = ReadFloatLeaf(chunk, m_fullLevel);
            break;
        case PROP_EMPTY_LEVEL:
            m_emptyLevel = ReadFloatLeaf(chunk, m_emptyLevel);
            break;
        case PROP_LEAK_RATE:
            m_leakRate = ReadFloatLeaf(chunk, m_leakRate);
            break;
        case PROP_LEAK_EFFECT:
            m_leakEffect = ReadIntLeaf(chunk, m_leakEffect);
            break;
        case PROP_BODY:
            m_bodyName = ReadStringLeaf(chunk);
            break;
        case PROP_DESTROYED_MODEL:
            m_destroyedModel = ReadStringLeaf(chunk);
            break;
        case PROP_HIT_POINTS:
            m_hitPoints = ReadIntLeaf(chunk, m_hitPoints);
            break;
        case PROP_VOLUME_OUT:
            chunk.Read(m_volumeOutWhenDestroyed);
            chunk.Ascend();
            break;
        case PROP_EXPLOSION_EFFECT:
            m_explosionEffect = ReadIntLeaf(chunk, m_explosionEffect);
            break;
        case PROP_EXPLOSION_LEVEL:
            m_explosionLevel = ReadFloatLeaf(chunk, m_explosionLevel);
            break;
        case PROP_EXIT_HOLES:
            chunk.Read(m_exitHoles);
            chunk.Ascend();
            break;
        default:
            // Quirk kept: entered, not left.
            break;
        }
    }
    chunk.Ascend();
    return true;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_BarrelCreator::Create()                      // 0x006062b0
{
    return new C_Barrel();                              // _aligned_malloc(0x210, 16) in the original
}

void C_BarrelCreator::Destroy(C_Actor *actor)           // 0x00606320
{
    delete actor;
}

// Registers the creator (static initialiser 0x00606290, thunk 0x00606280).
static const bool g_barrelCreatorRegistered = [] {
    g_actorCreators[ACTOR_BARREL] = &g_barrelCreator;
    return true;
}();
