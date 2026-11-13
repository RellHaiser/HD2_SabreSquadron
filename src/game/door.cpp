// C_Door: see door.h.
#include "game/door.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/frame_box_volume.h"
#include "game/game.h"
#include "game/pending.h"                               // g_gameRole, g_texts
#include "game/human.h"
#include "game/team.h"                                  // g_teamManager
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"

namespace {

bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }
bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }

I3D_scene *Scene() { return GetGame()->GetScene(); }

// Messages (3 bits: floor(log2 7) + 1, computed at run time in the original).
constexpr int MSG_BITS = 3;
enum : uint32_t {
    MSG_OPEN = 0,
    MSG_CLOSE = 1,
    MSG_LOCKED = 2,                                     // a locked door tried
    MSG_UNLOCKED = 3,
    MSG_LOCK = 4,
    MSG_WRONG_KEY = 5,
};

// Property chunks (inside the actor's properties, after the fields).
enum : uint16_t {
    PROP_SOUNDS = 0x65,
    PROP_PORTALS = 0x66,
    PROP_OTHER_HALF = 0x67,
    PROP_AUTO_CLOSE = 0x68,
    PROP_UNKNOWN_69 = 0x69,
    PROP_FACING = 0x6A,
    PROP_SHOOT_THROUGH = 0x6B,
    PROP_PICKABLE = 0x6C,
    PROP_TEXT = 0x6D,
};
constexpr uint16_t CHUNK_SAVE = 3000;

constexpr int AUTO_CLOSE_TIME = 15000;                  // ms after the opening delay
constexpr int AUTO_CLOSE_RETRY = 2000;                  // ms: the player near or the door seen
constexpr float AUTO_CLOSE_PLAYER_SQUARE = 100.0f;      // 10 m
constexpr uint32_t AUTO_CLOSE_SEEN_FRAMES = 10;
constexpr int BLOCKED_DELAY = 200;                      // ms before it moves again
constexpr float HANDLE_RATE = 0.005f;                   // rad a ms
constexpr float HANDLE_MAX = 0.785398185f;              // rad (45 degrees)
constexpr float PORTAL_MAX_DISTANCE = 10.0f;            // m
constexpr float SLIDE_TURN = 4.71238899f;               // rad: the slide's direction about the axis
constexpr float FACING_TURN = 1.57079637f;              // rad (WriteProperties)
constexpr float HINGED_BOX_SHRINK = 0.01f;              // m, each side
constexpr float SLIDING_BOX_SHRINK_XZ = 0.001f;         // m
constexpr float SLIDING_BOX_SHRINK_Y = 0.01f;           // m
constexpr uint32_t FIND_OTHER_HALF_MASK = 0xC0001;      // (FindFrame's type mask)

// The box's edges as corner pairs (0x0083ea0c; corners: bit 2 min x (else max), bit 1 min y,
// bit 0 max z).
const uint16_t BOX_EDGES[12][2] = {{2, 3}, {3, 7}, {7, 6}, {6, 2}, {0, 2}, {1, 3},
                                   {5, 7}, {4, 6}, {0, 1}, {1, 5}, {5, 4}, {4, 0}};

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

// DotVector (0x00454660): the products summed z, y, x.
float DotZYX(const S_vector &a, const S_vector &b) { return (a.z * b.z + a.y * b.y) + a.x * b.x; }

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

// The quaternion product a * b as the doors compute it inline (a turn by a after b).
S_quat QuatProduct(const S_quat &a, const S_quat &b)
{
    const S_vector c = a.v.Cross(b.v);
    const S_vector av = a.v * b.s;
    const S_vector bv = b.v * a.s;
    S_quat r;
    r.v = S_vector(bv.x + av.x, bv.y + av.y, bv.z + av.z);
    r.v = S_vector(r.v.x + c.x, r.v.y + c.y, r.v.z + c.z);
    r.s = a.s * b.s - DotZYX(a.v, b.v);
    return r;
}

// The frame `name` (its short name lower-cased) contains, the children searched from the last,
// each before its own children, the frame itself last. (0x00600950; Fix: the original copies the
// names into 32 / 36 byte stack buffers.)
std::string LowerName(const char *name)
{
    std::string s(name);
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

I3D_frame *FindHandle(I3D_frame *frame, const std::string &needle)
{
    for (int i = frame->NumChildren(); i-- > 0;) {
        I3D_frame *child = frame->GetChild(i);
        if (LowerName(child->GetShortName()).find(needle) != std::string::npos)
            return child;
        if (I3D_frame *found = FindHandle(child, needle))
            return found;
    }
    return LowerName(frame->GetShortName()).find(needle) != std::string::npos ? frame : nullptr;
}

I3D_frame *FindHandle(I3D_frame *frame, const char *name) { return FindHandle(frame, LowerName(name)); }

// The handle turned about its local z by `angle` (its z axis as SetWorldDir's direction).
void TurnHandle(I3D_frame *handle, float angle)
{
    if (!handle)
        return;
    bool normalize;
    S_vector dir = handle->GetLocalDirUnnormalized(&normalize);
    if (normalize)
        NormalizeBy(dir, SquareYZX(dir));
    handle->SetWorldDir(dir, angle);
    handle->Update();
}

// The corners of `box` (see BOX_EDGES) through `world`.
void BoxCorners(const I3D_bbox &box, const S_matrix &world, S_vector corners[8])
{
    for (int c = 0; c < 8; ++c)
        corners[c] = S_vector(c & 4 ? box.min.x : box.max.x, c & 2 ? box.min.y : box.max.y,
                              c & 1 ? box.max.z : box.min.z);
    for (int c = 8; c-- > 0;)
        corners[c] = corners[c] * world;
}

// Whether a dynamic volume touches the box's edges or its diagonals 1-6 and 2-5.
bool BoxTouches(const S_vector corners[8])
{
    S_vector hitPos;
    S_vector hitNormal;
    for (const uint16_t *edge : BOX_EDGES) {
        const S_vector &a = corners[edge[0]];
        const S_vector &b = corners[edge[1]];
        if (g_collisionWorld.RayTestDynamic(a, S_vector(b.x - a.x, b.y - a.y, b.z - a.z), &hitPos, &hitNormal, 2, 0))
            return true;
    }
    if (g_collisionWorld.RayTestDynamic(corners[1], corners[6] - corners[1], &hitPos, &hitNormal, 2, 0))
        return true;
    return g_collisionWorld.RayTestDynamic(corners[2], corners[5] - corners[2], &hitPos, &hitNormal, 2, 0) != nullptr;
}

// I3D_frame::GetWorldDir as it compiles here (0x0041d200) (DS 0x0043da40): the square summed y, z, x.
S_vector WorldDir(const I3D_frame *frame)
{
    S_vector dir = frame->GetMatrix()(2);
    NormalizeBy(dir, SquareYZX(dir));
    return dir;
}

}  // namespace

C_DoorCreator g_doorCreator;                            // 0x008ac9f8

C_Door::C_Door() : C_Actor(ACTOR_DOOR)                  // 0x005ff210
{
}

void C_Door::Tick(int timeDelta)                        // 0x00600920
{
    if (!IsClient())
        Animate(timeDelta);
}

void C_Door::SetState(int action, bool autoClose, bool linked, int force)   // 0x005ff4b0
{
    if (m_otherHalf && !linked && m_started)
        m_otherHalf->SetState(action, autoClose, true, force);
    if (const S_FrameData *data = m_frame->GetData()) {
        if (data->script)
            data->script->OnUse(action);
    }
    if (action == DOOR_ACTION_OPEN) {
        if (!static_cast<char>(force) && m_locked) {
            SendMessage(MSG_LOCKED);
            return;
        }
        m_delay = m_openDelay;
        m_autoCloseTime = autoClose && m_autoClose ? m_openDelay + AUTO_CLOSE_TIME : 0;
        SendMessage(MSG_OPEN);
        m_motion = DOOR_OPENING;
        m_handleMoving = true;
        SetPortalsClosed(false);
        return;
    }
    if (action == DOOR_ACTION_CLOSE) {
        SendMessage(MSG_CLOSE);
        m_motion = DOOR_CLOSING;
    }
}

bool C_Door::Unlock(int key)                            // 0x005ff5f0
{
    if (!m_locked)
        return true;
    if (key == m_key) {
        m_locked = false;
        if (m_otherHalf)
            m_otherHalf->m_locked = false;
        SendMessage(MSG_UNLOCKED);
    } else {
        SendMessage(MSG_WRONG_KEY);
    }
    return !m_locked;
}

bool C_Door::Lock(int key)                              // 0x005ff650
{
    if (m_locked)
        return true;
    if (m_motion != DOOR_CLOSED)
        return false;
    if (key == m_key) {
        m_locked = true;
        if (m_otherHalf)
            m_otherHalf->m_locked = true;
        SendMessage(MSG_LOCK);
    } else {
        SendMessage(MSG_WRONG_KEY);
    }
    return m_locked;
}

void C_Door::PickLock()                                 // 0x005ff6c0
{
    if (m_motion != DOOR_CLOSED || !m_locked || !m_pickable)
        return;
    m_motion = DOOR_CLOSED;
    m_locked = false;
    if (m_otherHalf)
        m_otherHalf->m_locked = false;
    SendMessage(MSG_UNLOCKED);
}

int C_Door::GetSide(const S_vector4 &pos) const         // 0x00602320
{
    if (m_kind == DOOR_SLIDING)
        return 2;
    const S_vector &p = m_frame->GetWorldPos();
    if ((pos.x - p.x) * m_facing.x + (pos.y - p.y) * m_facing.y + (pos.z - p.z) * m_facing.z < 0.0f)
        return 0;
    return 1;
}

void C_Door::SendMessage(uint32_t kind)                 // 0x00601f40
{
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(kind, MSG_BITS);
    SendActorMessage(msg);
}

void C_Door::PlaySound(int id)
{
    if (id == -1)
        return;
    m_sound.group = id;
    GetSoundModule()->Play(m_sound);
}

void C_Door::StartMovingSound()
{
    if (m_sounds[SOUND_MOVING] == -1) {
        m_movingSound = -1;
    } else if (m_movingSound == -1) {
        m_sound.group = m_sounds[SOUND_MOVING];
        m_sound.flags |= SOUND_KEEP_SLOT;
        m_movingSound = GetSoundModule()->Play(m_sound);
        m_movingSoundPaused = false;
        m_sound.flags &= ~SOUND_KEEP_SLOT;
    } else {
        // Fix: a slot without a sound (the original calls through null).
        if (I3D_sound *sound = GetSoundModule()->GetSound(m_movingSound))
            sound->SetOn(true);
        m_movingSoundPaused = false;
    }
}

void C_Door::OnMessage(C_BitStream *msg)                // 0x00601f90
{
    switch (msg->ReadBits(MSG_BITS)) {
    case MSG_OPEN:
        PlaySound(m_sounds[SOUND_OPEN]);
        StartMovingSound();
        m_motion = DOOR_OPENING;
        m_handleMoving = true;
        m_handleRising = true;
        SetPortalsClosed(false);
        break;
    case MSG_CLOSE:
        StartMovingSound();
        m_motion = DOOR_CLOSING;
        break;
    case MSG_LOCKED:
        PlaySound(m_sounds[SOUND_LOCKED]);
        break;
    case MSG_UNLOCKED:
        m_locked = false;
        if (m_otherHalf)
            m_otherHalf->m_locked = false;
        PlaySound(m_sounds[SOUND_UNLOCK]);
        break;
    case MSG_LOCK:
        // (The other half is not locked here.)
        m_locked = true;
        PlaySound(m_sounds[SOUND_LOCK]);
        break;
    case MSG_WRONG_KEY:
        PlaySound(m_sounds[SOUND_WRONG_KEY]);
        break;
    default:
        break;
    }
}

void C_Door::Animate(int time)                          // 0x005fffe0
{
    // The handles turn up to 45 degrees and back as it starts to open.
    if ((m_handle || m_handle2) && m_handleMoving) {
        m_handleAngle = (m_handleRising ? 1.0f : -1.0f) * static_cast<float>(time) * HANDLE_RATE + m_handleAngle;
        if (m_handleRising) {
            if (m_handleAngle > HANDLE_MAX) {
                m_handleAngle = HANDLE_MAX;
                m_handleRising = false;
            }
        } else if (m_handleAngle < 0.0f) {
            m_handleAngle = 0.0f;
            m_handleMoving = false;
        }
        TurnHandle(m_handle, m_handleAngle);
        TurnHandle(m_handle2, m_handleAngle);
    }
    // Closing by itself: not while the player is within 10 m or the door was seen lately (single
    // player; again 2 s later).
    if (m_autoCloseTime != 0) {
        m_autoCloseTime -= time;
        if (m_autoCloseTime <= 0) {
            m_autoCloseTime = 0;
            if (m_motion == DOOR_OPEN) {
                bool wait = false;
                if (IsLocalGame()) {
                    if (C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer()) {
                        const S_vector &p = player->GetFrame()->GetWorldPos();
                        const float dx = m_position.x - p.x;
                        const float dy = m_position.y - p.y;
                        const float dz = m_position.z - p.z;
                        wait = (dz * dz + dy * dy) + dx * dx < AUTO_CLOSE_PLAYER_SQUARE ||
                               static_cast<uint32_t>(Scene()->GetRenderStamp() - m_frame->GetRenderStamp()) <
                                   AUTO_CLOSE_SEEN_FRAMES;
                    }
                }
                if (wait)
                    m_autoCloseTime = AUTO_CLOSE_RETRY;
                else
                    SetState(DOOR_ACTION_CLOSE, true, false, 0);
            }
        }
    }
    const int motion = m_motion;
    if (motion == DOOR_CLOSED || motion == DOOR_OPEN)
        return;
    const float before = m_opened;
    if (m_delay != 0) {
        m_delay -= time;
        if (m_delay > 0)
            return;
        m_delay = 0;
    }
    const float step = static_cast<float>(static_cast<uint32_t>(time)) * 0.001f * m_speed;
    if (motion == DOOR_OPENING) {
        m_opened = step + m_opened;
        if (m_opened > m_openAngle) {
            m_motion = DOOR_OPEN;
            m_opened = m_openAngle;
            if (m_movingSound != -1) {
                if (I3D_sound *sound = GetSoundModule()->GetSound(m_movingSound))
                    sound->SetOn(false);
            }
        }
    } else if (motion == DOOR_CLOSING) {
        m_opened = m_opened - step;
        if (m_opened < 0.0f) {
            if (m_movingSound != -1) {
                if (I3D_sound *sound = GetSoundModule()->GetSound(m_movingSound))
                    sound->SetOn(false);
            }
            m_motion = DOOR_CLOSED;
            m_opened = 0.0f;
            PlaySound(m_sounds[SOUND_CLOSED]);
            if (!m_otherHalf || m_otherHalf->m_motion == DOOR_CLOSED)
                SetPortalsClosed(true);
        }
    }
    // Blocked: back where it was, the moving sound paused, again in 0.2 s.
    if (m_kind == DOOR_HINGED ? TurnTo() : SlideTo()) {
        if (m_movingSound != -1) {
            if (I3D_sound *sound = GetSoundModule()->GetSound(m_movingSound)) {
                m_movingSoundPos = sound->GetPosition();
                sound->SetOn(false);
            }
        }
        m_movingSoundPaused = true;
        m_opened = before;
        m_delay = BLOCKED_DELAY;
        UpdateVolume();
        return;
    }
    if (m_movingSound != -1 && m_movingSoundPaused) {
        if (I3D_sound *sound = GetSoundModule()->GetSound(m_movingSound)) {
            sound->Play(m_movingSoundPos);
            sound->SetOn(true);
        }
    }
    m_movingSoundPaused = false;
    UpdateVolume();
}

bool C_Door::TurnTo()                                   // 0x005ff700 (DS 0x00624f80)
{
    const S_quat previous = m_frame->GetRot();
    S_quat turn;
    turn.Make(S_vector(m_axis.x, m_axis.y, m_axis.z), m_opened);
    m_frame->SetRot(QuatProduct(turn, m_startRot));
    m_frame->Update();
    if (m_motion == DOOR_OPENING)
        return false;
    // (Nor about the x axis.)
    if ((m_axis.x == -1.0f && m_axis.y == 0.0f && m_axis.z == 0.0f) ||
        (m_axis.x == 1.0f && m_axis.y == 0.0f && m_axis.z == 0.0f))
        return false;
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    I3D_bbox hierarchy;
    I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, m_frame);
    box.max = S_vector(box.max.x - HINGED_BOX_SHRINK, box.max.y - HINGED_BOX_SHRINK, box.max.z - HINGED_BOX_SHRINK);
    box.min = S_vector(box.min.x + HINGED_BOX_SHRINK, box.min.y + HINGED_BOX_SHRINK, box.min.z + HINGED_BOX_SHRINK);
    S_vector corners[8];
    BoxCorners(box, m_frame->GetMatrix(), corners);
    const bool blocked = BoxTouches(corners);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    if (blocked) {
        m_frame->SetRot(previous);
        m_frame->Update();
    }
    return blocked;
}

bool C_Door::SlideTo()                                  // 0x005ffbd0 (DS 0x00625590)
{
    S_quat turn;
    turn.Make(S_vector(m_axis.x, m_axis.y, m_axis.z), SLIDE_TURN);
    const S_vector dir = QuatProduct(m_frame->GetRot(), turn).GetDir();
    const S_vector offset(dir.x * m_opened, dir.y * m_opened, dir.z * m_opened);
    m_frame->SetPos(S_vector(offset.x + m_startPos.x, offset.y + m_startPos.y, offset.z + m_startPos.z));
    m_frame->Update();
    if (m_motion == DOOR_OPENING)
        return false;
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    I3D_bbox hierarchy;
    I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, m_frame);
    box.max = S_vector(box.max.x - SLIDING_BOX_SHRINK_XZ, box.max.y - SLIDING_BOX_SHRINK_Y,
                       box.max.z - SLIDING_BOX_SHRINK_XZ);
    box.min = S_vector(box.min.x + SLIDING_BOX_SHRINK_XZ, box.min.y + SLIDING_BOX_SHRINK_Y,
                       box.min.z + SLIDING_BOX_SHRINK_XZ);
    S_vector corners[8];
    BoxCorners(box, m_frame->GetMatrix(), corners);
    // (Quirk kept: a blocked door stays where it slid to; the caller only puts m_opened back.)
    const bool blocked = BoxTouches(corners);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    return blocked;
}

void C_Door::SetPortalsClosed(bool closed)              // 0x006007b0
{
    if (m_portal)
        m_portal->SetOn(!closed);
    if (m_otherPortal)
        m_otherPortal->SetOn(!closed);
}

void C_Door::UpdateVolume()                             // 0x006007f0
{
    const I3D_bound_volume &bound = m_frame->GetBound();
    m_invMatrix.Inverse(m_frame->GetMatrix());
    // A turning door opening lets cars through (but one turning about the x axis).
    const bool passable = m_kind == DOOR_HINGED && m_motion == DOOR_OPENING &&
                          !(m_axis.x == 1.0f && m_axis.y == 0.0f && m_axis.z == 0.0f) &&
                          !(m_axis.x == -1.0f && m_axis.y == 0.0f && m_axis.z == 0.0f);
    m_volume.type = passable ? COLLISION_TYPE_ACTOR_DOOR | COLLISION_TYPE_LIGHT_OBJECT : COLLISION_TYPE_ACTOR_DOOR;
    // Quirk kept: the centre is the frame's bound's, in the frame's space (the bound's radius as w).
    m_volume.center = bound.center;
    m_volume.centerW = bound.radius;
    g_collisionWorld.MoveVolume(&m_volume);
}

void C_Door::FindPortals()                              // 0x00600a50
{
    if (!m_hasPortals) {
        m_otherPortal = nullptr;
        m_portal = nullptr;
        return;
    }
    // Quirk kept: the frame's bound's centre in the frame's space against the portals' centres.
    const S_vector center = m_frame->GetBound().center;
    I3D_sector *sector = nullptr;
    for (I3D_frame *parent = m_frame->GetParent(); parent; parent = parent->GetParent()) {
        if (parent->GetType() == FRAME_SECTOR) {
            sector = static_cast<I3D_sector *>(parent);
            break;
        }
    }
    if (!sector)
        sector = Scene()->GetSector(center);
    const bool own = sector->NumPortals() != 0;
    const int count = own ? sector->NumPortals() : sector->NumIncomingPortals();
    I3D_portal *nearest = nullptr;
    float best = PORTAL_MAX_DISTANCE;
    for (int i = 0; i < count; ++i) {
        I3D_portal *portal = own ? sector->GetPortal(i) : sector->GetIncomingPortal(i);
        const S_vector &c = portal->GetCenter();
        const float dx = c.x - center.x;
        const float dy = c.y - center.y;
        const float dz = c.z - center.z;
        const float distance = LengthOf((dz * dz + dy * dy) + dx * dx);
        if (!(distance > best) && !(distance > portal->GetRadius())) {
            best = distance;
            nearest = portal;
        }
    }
    if (nearest) {
        m_portal = nearest;
        m_otherPortal = nearest->GetOpposite();
    }
}

void C_Door::CopyFrom(const C_Actor *source)            // 0x005ff3e0
{
    C_Actor::CopyFrom(source);
    const auto *door = static_cast<const C_Door *>(source);
    m_kind = door->m_kind;
    m_motion = door->m_motion;
    m_openAngle = door->m_openAngle;
    m_speed = door->m_speed;
    m_key = door->m_key;
    m_locked = door->m_locked;
    m_pickable = door->m_pickable;
    m_axis = door->m_axis;
    m_openDelay = door->m_openDelay;
    m_unknown23C = door->m_unknown23C;
    m_hasPortals = door->m_hasPortals;
    FindPortals();
    for (int i = 0; i < SOUND_COUNT; ++i)
        m_sounds[i] = door->m_sounds[i];
}

void C_Door::StartMission(bool server)                  // 0x00600cc0 (DS 0x00626ae0)
{
    Activate(server);
    if (!m_hasPortals)
        m_frame->LinkTo(m_initialParent, 1);
    if (!m_started) {
        m_otherPortal = nullptr;
        m_portal = nullptr;
    }
    if (IsClient() && !server && m_hasPortals) {
        FindPortals();
        m_frame->LinkTo(Scene()->GetDynamicRoot(), 1);
        m_frame->Update();
        const S_matrix &world = m_frame->GetMatrix();
        m_startPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
        m_startRot = m_frame->GetRot();
        if (m_motion == DOOR_CLOSED)
            SetPortalsClosed(true);
    }
    if (m_started)
        return;
    if (!m_otherHalfName.IsEmpty()) {
        if (I3D_frame *frame = Scene()->FindFrame(m_otherHalfName.c_str(), FIND_OTHER_HALF_MASK)) {
            const S_FrameData *data = frame->GetData();
            m_otherHalf = static_cast<C_Door *>(data ? data->actor : nullptr);
        }
    }
    m_name = g_texts.Get(m_textId);
    m_movingSound = -1;
    m_movingSoundPos = 0;
    m_handle = FindHandle(m_frame, "dhandle");
    m_handle2 = FindHandle(m_frame, "d2handle");
    m_handleRising = true;
    m_handleMoving = false;
    m_handleAngle = 0.0f;
    m_opened = 0.0f;
    const S_matrix &world = m_frame->GetMatrix();
    m_startPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_startRot = m_frame->GetRot();
    m_initialMotion = m_motion;
    m_initialLocked = m_locked;
    if (IsServer() && m_motion == DOOR_OPEN)
        SetState(DOOR_ACTION_OPEN, true, false, 0);
    m_started = true;
    m_autoCloseTime = 0;
    MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, COLLISION_TYPE_ACTOR_DOOR, this);
    if (m_shootThrough)
        m_volume.flags |= COLLISION_VOLUME_SHOOT_THROUGH;
    g_collisionWorld.AddVolume(&m_volume);
    const S_matrix &at = m_frame->GetMatrix();
    m_sound.pos = at(3);
    m_sound.posW = at.m[3][3];
    m_sound.flags = SOUND_SILENT_TO_AI;
    m_sound.attachTo = nullptr;
    m_sound.source = nullptr;
    m_sound.chapter = 1;
}

void C_Door::StopMission(bool server)                   // 0x00601350
{
    SetPortalsClosed(false);
    m_otherHalf = nullptr;
    m_started = false;
    g_collisionWorld.RemoveVolume(&m_volume);
    m_motion = m_initialMotion;
    m_unknown94 = 0;
    Deactivate(server);
}

void C_Door::RestartMission(bool)                       // 0x00602400
{
    m_locked = m_initialLocked;
    m_frame->SetPos(S_vector(m_startPos.x, m_startPos.y, m_startPos.z));
    m_frame->SetRot(m_startRot);
    m_frame->Update();
    m_motion = m_initialMotion;
    m_handleMoving = false;
    m_handleAngle = 0.0f;
    m_handleRising = true;
    m_opened = 0.0f;
    m_delay = 0;
    // (Quirk kept: a moving sound playing is forgotten, not stopped.)
    m_movingSound = -1;
    m_movingSoundPos = 0;
    m_movingSoundPaused = false;
    UpdateVolume();
    SetPortalsClosed(m_initialMotion == DOOR_CLOSED);
    if (IsServer() && m_motion == DOOR_OPEN)
        SetState(DOOR_ACTION_OPEN, true, false, 0);
}

bool C_Door::WriteProperties(C_chunk &chunk)            // 0x006013b0
{
    chunk.Write(m_kind);
    chunk.Write(m_motion);
    chunk.Write(m_openAngle);
    chunk.Write(m_speed);
    chunk.Write(&m_axis, 12);
    chunk.Write(m_key);
    chunk.Write(m_locked);
    // (Two fields read back as nothing: a 0 byte, then 4 bytes whose last three are stack
    // leftovers in the original; zeros here.)
    const uint8_t zeros[4] = {0, 0, 0, 0};
    chunk.Write(zeros, 1);
    chunk.Write(zeros, 4);
    chunk.Write(m_unknown23C);
    chunk.Begin(PROP_SOUNDS);
    for (int sound : m_sounds)
        chunk.Write(sound);
    chunk.Ascend();
    chunk.Begin(PROP_PORTALS);
    chunk.Write(m_hasPortals);
    chunk.Ascend();
    if (!m_otherHalfName.IsEmpty())
        chunk.WriteChunk(PROP_OTHER_HALF, m_otherHalfName.c_str(), static_cast<int>(m_otherHalfName.Size()) + 1);
    chunk.Begin(PROP_AUTO_CLOSE);
    chunk.Write(m_autoClose);
    chunk.Ascend();
    chunk.Begin(PROP_UNKNOWN_69);
    chunk.Write(m_unknown99);
    chunk.Ascend();
    // The facing: the world z, turned to where the frame's bound centre moves when the frame
    // turns a quarter about the axis. (Quirk kept: the start rotation becomes the frame's.)
    const S_vector dir = WorldDir(m_frame);
    m_facing = {dir.x, dir.y, dir.z, 0.0f};
    const S_vector before = m_frame->GetBound().center;
    m_startRot = m_frame->GetRot();
    S_quat quarter;
    quarter.Make(S_vector(m_axis.x, m_axis.y, m_axis.z), FACING_TURN);
    m_frame->SetRot(QuatProduct(quarter, m_startRot));
    m_frame->Update();
    const S_vector after = m_frame->GetBound().center;
    S_vector moved(after.x - before.x, after.y - before.y, after.z - before.z);
    NormalizeBy(moved, SquareYZX(moved));
    S_vector facing(m_facing.x, m_facing.y, m_facing.z);
    if (DotZYX(facing, moved) < 0.0f)
        m_facing = {-m_facing.x, -m_facing.y, -m_facing.z, m_facing.w};
    m_frame->SetRot(m_startRot);
    m_frame->Update();
    chunk.WriteChunk(PROP_FACING, &m_facing, 12);
    chunk.Begin(PROP_SHOOT_THROUGH);
    chunk.Write(m_shootThrough);
    chunk.Ascend();
    chunk.Begin(PROP_PICKABLE);
    chunk.Write(m_pickable);
    chunk.Ascend();
    chunk.Begin(PROP_TEXT);
    chunk.Write(m_textId);
    chunk.Write(m_unknown118);
    chunk.Ascend();
    return true;
}

bool C_Door::ReadProperties(C_chunk &chunk)             // 0x00601ac0
{
    chunk.Read(m_kind);
    chunk.Read(m_motion);
    chunk.Read(m_openAngle);
    chunk.Read(m_speed);
    chunk.Read(&m_axis, 12);
    chunk.Read(m_key);
    chunk.Read(m_locked);
    uint8_t skipped1;
    uint32_t skipped4;
    chunk.Read(skipped1);
    chunk.Read(skipped4);
    chunk.Read(m_unknown23C);
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case PROP_SOUNDS:
            for (int &sound : m_sounds)
                chunk.Read(sound);
            chunk.Ascend();
            break;
        case PROP_PORTALS:
            chunk.Read(m_hasPortals);
            chunk.Ascend();
            break;
        case PROP_OTHER_HALF: {
            // (C_chunk::ReadRemainingString 0x00415d00: at most 999 characters, then ascends.)
            char name[1000];
            chunk.ReadString(name, sizeof name);
            chunk.Ascend();
            m_otherHalfName = name;
            break;
        }
        case PROP_AUTO_CLOSE:
            chunk.Read(m_autoClose);
            chunk.Ascend();
            break;
        case PROP_UNKNOWN_69:
            chunk.Read(m_unknown99);
            chunk.Ascend();
            break;
        case PROP_FACING: {
            // (The w the original keeps is a stack leftover: 0 here.)
            S_vector facing(0.0f, 0.0f, 0.0f);
            chunk.Read(&facing, 12);
            chunk.Ascend();
            m_facing = {facing.x, facing.y, facing.z, 0.0f};
            break;
        }
        case PROP_SHOOT_THROUGH:
            chunk.Read(m_shootThrough);
            chunk.Ascend();
            break;
        case PROP_PICKABLE:
            chunk.Read(m_pickable);
            chunk.Ascend();
            break;
        case PROP_TEXT:
            chunk.Read(m_textId);
            chunk.Read(m_unknown118);
            chunk.Ascend();
            break;
        default:
            break;
        }
    }
    if (m_hasPortals)
        FindPortals();
    if (m_key == 0)
        m_key = -1;
    return true;
}

bool C_Door::SaveGame(C_chunk &chunk)                   // 0x00602500
{
    chunk.Begin(CHUNK_SAVE);
    chunk.Write(m_opened);
    chunk.Write(m_locked);
    chunk.Write(m_motion);
    chunk.Write(m_delay);
    chunk.Write(&m_frame->GetMatrix().m[3][0], 12);
    float rot[4];
    m_frame->GetRot().ToFile(rot);
    chunk.Write(rot, 16);
    chunk.Ascend();
    return false;
}

bool C_Door::LoadGame(C_chunk &chunk)                   // 0x00602700
{
    // Fix: without the chunk the original places the frame at stack leftovers (here it stays).
    S_vector pos = m_frame->GetPos();
    S_quat rot = m_frame->GetRot();
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != CHUNK_SAVE)
            continue;
        chunk.Read(m_opened);
        chunk.Read(m_locked);
        chunk.Read(m_motion);
        chunk.Read(m_delay);
        chunk.Read(&pos, 12);
        float file[4] = {rot.s, rot.v.x, rot.v.y, rot.v.z};
        chunk.Read(file, 16);
        rot = S_quat::FromFile(file);
        chunk.Ascend();
    }
    m_frame->SetPos(pos);
    m_frame->SetRot(rot);
    m_frame->Update();
    UpdateVolume();
    SetPortalsClosed(m_motion == DOOR_CLOSED);
    return true;
}

void C_Door::WriteState(C_BitStream *msg)               // 0x006021e0
{
    msg->WriteBits(m_locked ? 1 : 0, 1);
    switch (m_motion) {
    case DOOR_CLOSED:
    case DOOR_CLOSING:
        msg->WriteBits(MSG_CLOSE, MSG_BITS);
        break;
    case DOOR_OPEN:
    case DOOR_OPENING:
        msg->WriteBits(MSG_OPEN, MSG_BITS);
        break;
    default:
        break;
    }
}

void C_Door::ReadState(C_BitStream *msg)                // 0x00602290
{
    m_locked = msg->ReadBits(1) != 0;
    const uint32_t kind = msg->ReadBits(MSG_BITS);
    if (kind == MSG_OPEN) {
        m_motion = DOOR_OPENING;
        m_handleMoving = true;
        m_handleRising = true;
        SetPortalsClosed(false);
    } else if (kind == MSG_CLOSE) {
        m_motion = DOOR_CLOSING;
    }
}

int C_Door::GetMaterialId(I3D_frame *part)              // 0x006023b0
{
    int id = GetFrameMaterialId(part);
    if (id != -1)
        return id;
    if (part != m_frame) {
        id = GetFrameMaterialId(m_frame);
        if (id != -1)
            return id;
    }
    return 11;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_DoorCreator::Create()                        // 0x005ff180
{
    return new C_Door();                                // _aligned_malloc(0x270, 16) in the original
}

void C_DoorCreator::Destroy(C_Actor *actor)             // 0x005ff1f0
{
    delete actor;
}

// Registers the creator (static initialiser 0x005ff160, thunk 0x005ff150).
static const bool g_doorCreatorRegistered = [] {
    g_actorCreators[ACTOR_DOOR] = &g_doorCreator;
    return true;
}();
