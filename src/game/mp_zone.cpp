// Multiplayer zones (C_MPZone; original 0x005ebde0..0x005edcf0).
#include "game/mp_zone.h"

#include <cmath>
#include <cstring>
#include <string>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage, g_actorsModule
#include "game/campaign.h"                              // C_MissionDef
#include "game/game.h"                                  // IsSpawnBlocked
#include "game/human.h"
#include "game/interface.h"
#include "game/pending.h"                               // g_gameRole, g_netGame
#include "game/vertex_anim.h"
#include "game/profile.h"                               // g_gameProfile, g_mpSettings
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// The messages and their bits: (int)(log(2) / log(2) + 1), worked out at run time by the
// original.
enum : uint32_t {
    MSG_CAPTURE_START = 0,
    MSG_CAPTURE_STOP = 1,
    MSG_TEAM = 2,
};
constexpr int MSG_BITS = 2;
constexpr int TEAM_BITS = 2;                            // WriteState / ReadState

constexpr int PROPERTIES_VERSION = 0x8001;              // older properties start with the number
constexpr int CHECK_INTERVAL = 1000;                    // ms
constexpr float TEAM_POINTS_PER_MS = 1.25e-05f;         // a held plain zone's team points
constexpr int CAPTURE_POINTS = 5;
constexpr int NEUTRALIZE_POINTS = 2;
constexpr int MAX_SPAWN_TRIES = 32;
constexpr int PLAYER_SLOTS = 32;                        // the first actors: the players'
constexpr int MISSION_ORDERED_ZONES = 1;                // C_MissionDef +0x60

constexpr const char *FLAG_ANIMATION = "vlajka";        // the flag's vertex animation and visual
constexpr int FLAG_FRAME_TIME = 210;                    // ms (the player's default is 250)

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

// The zone's sphere: the box's centre and the distance from it to the box's minimum corner
// (summed z, y, x).
void ZoneSphere(const I3D_bbox &box, S_vector &center, float &radius)
{
    center = S_vector((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                      (box.min.z + box.max.z) * 0.5f);
    const S_vector d(box.min.x - center.x, box.min.y - center.y, box.min.z - center.z);
    radius = LengthOf((d.z * d.z + d.y * d.y) + d.x * d.x);
}

}  // namespace

C_MPZone::C_MPZone() : C_Actor(ACTOR_MP_ZONE)           // 0x005ebed0
{
}

bool C_MPZone::SetFrame(I3D_frame *frame)               // 0x005ebfd0
{
    if (!frame)
        return false;
    return C_Actor::SetFrame(frame);
}

void C_MPZone::CopyFrom(const C_Actor *source)          // 0x005ebff0
{
    C_Actor::CopyFrom(source);
    const auto *other = static_cast<const C_MPZone *>(source);
    m_number = other->m_number;
    m_shape = other->m_shape;
    m_team = other->m_team;
    m_textId = other->m_textId;
    m_objective = other->m_objective;
    m_captureTime = other->m_captureTime;
    m_bothWays = other->m_bothWays;
    std::memcpy(m_flagNames, other->m_flagNames, sizeof m_flagNames);
}

bool C_MPZone::WriteProperties(C_chunk &chunk)          // 0x005ed920
{
    chunk.Write(PROPERTIES_VERSION);
    chunk.Write(m_number);
    chunk.Write(m_shape);
    chunk.Write(m_team);
    chunk.Write(m_textId);
    chunk.Write(m_flagNames);
    chunk.Write(m_objective);
    chunk.Write(m_captureTime);
    chunk.Write(m_bothWays);
    return true;
}

bool C_MPZone::ReadProperties(C_chunk &chunk)           // 0x005eda60
{
    // Older properties start with the number. (Quirk kept: the original reads the first value
    // into its chunk pointer's slot, so a failed read leaves an address there, above the version:
    // read as versioned.)
    int first = PROPERTIES_VERSION + 1;
    chunk.Read(first);
    if (first <= 0x8000)
        m_number = first;
    else
        chunk.Read(m_number);
    chunk.Read(m_shape);
    chunk.Read(m_team);
    chunk.Read(m_textId);
    chunk.Read(m_flagNames);
    chunk.Read(m_objective);
    chunk.Read(m_captureTime);
    if (first > 0x8000)
        chunk.Read(m_bothWays);
    m_newTeam = m_team;
    m_startTeam = m_team;
    return true;
}

void C_MPZone::StartMission(bool server)                // 0x005ed110
{
    Activate(server);
    if (!m_started) {
        m_started = true;
        I3D_scene *scene = GetGame()->GetScene();
        for (int i = 0; i < 3; ++i) {
            m_flags[i] = scene->FindFrame(m_flagNames[i], FRAME_MASK_USER);
            if (m_flags[i])
                m_flags[i]->AddRef();
        }
        m_flagAnim = new S_VertexAnimPlayer();
        // Fix: the name is not copied into a 28-byte buffer.
        const std::string spawnName = std::string(m_frame->GetName()) + "spawn";
        m_spawnFrame = scene->FindFrame(spawnName.c_str(), FRAME_MASK_DUMMY);
        if (m_spawnFrame)
            m_spawnFrame->AddRef();
        m_progressShown = false;
        m_localInside = false;
    }
    if (!server) {
        m_team = -1;
        m_resetPending = false;
        ApplyTeam();
    }
}

void C_MPZone::StopMission(bool)                        // 0x005ed260
{
    if (!m_started)
        return;
    m_started = false;
    for (I3D_frame *&flag : m_flags) {
        if (flag) {
            flag->Release();
            flag = nullptr;
        }
    }
    // Fix: the player is let go even when it was never shown (the original deletes it only when
    // the module has it).
    g_vertexAnimModule.RemovePlayer(m_flagAnim, false);
    delete m_flagAnim;
    m_flagAnim = nullptr;
    if (m_spawnFrame) {
        m_spawnFrame->Release();
        m_spawnFrame = nullptr;
    }
}

void C_MPZone::RestartMission(bool)                     // 0x005ed8b0
{
    m_newTeam = m_startTeam;
    m_progressShown = false;
    m_resetPending = true;
    m_captureState = MPZONE_CAPTURE_STOPPED;
    m_progress = 0;
    m_spawnIndex = 0;
    m_team = -1;
    ApplyTeam();
}

void C_MPZone::WriteState(C_BitStream *msg)             // 0x005ed2d0
{
    msg->WriteBits(static_cast<uint32_t>(m_team), TEAM_BITS);
}

void C_MPZone::ReadState(C_BitStream *msg)              // 0x005ed2f0
{
    m_newTeam = static_cast<int>(msg->ReadBits(TEAM_BITS));
    m_team = -1;
    ApplyTeam();
}

void C_MPZone::SendState(uint32_t kind)                 // 0x005ed320
{
    C_BitStream *msg = CreateActorMessage(m_netId, true);
    msg->WriteBits(kind, MSG_BITS);
    if (kind == MSG_TEAM)
        msg->WriteBits(static_cast<uint32_t>(m_team), MSG_BITS);
    SendActorMessage(msg);
}

void C_MPZone::OnMessage(C_BitStream *msg)              // 0x005ed390
{
    switch (msg->ReadBits(MSG_BITS)) {
    case MSG_CAPTURE_START:
        m_captureState = MPZONE_CAPTURE_RUNNING;
        m_progress = 0;
        if (!IsServer())
            m_checkTime = 10000;                        // (a check at the next Update)
        return;
    case MSG_CAPTURE_STOP:
        m_captureState = MPZONE_CAPTURE_NONE;
        if (m_progressShown && m_captureTime != 0)
            m_progressShown = false;
        return;
    case MSG_TEAM: {
        m_captureState = MPZONE_CAPTURE_NONE;
        const int team = static_cast<int>(msg->ReadBits(MSG_BITS));
        if (m_team == team)
            return;
        m_newTeam = team;
        if (m_objective != -1 || (m_spawnFrame && g_mpSettings->gameType == MP_GAME_OBJECTIVES)) {
            m_team = team;
            return;
        }
        // (The original then prints "New zone owner: Axis" / "Alied" / "Neutral" through an empty
        // debug function.)
        ApplyTeam();
        return;
    }
    default:
        return;
    }
}

bool C_MPZone::GetProgress(int *time, int *total, int *textId) const   // 0x005ec840
{
    if (!m_progressShown)
        return false;
    *total = m_captureTime * 1000;
    // (A neutral zone is taken in half the time.)
    *time = (*total - m_progress) - (m_team == TEAM_NONE ? m_captureTime * 500 : 0);
    *textId = m_textId;
    return true;
}

void C_MPZone::EnableForSide(int team)                  // 0x005ed900
{
    m_newTeam = team;
    ApplyTeam();
    SendState(MSG_TEAM);
}

void C_MPZone::ApplyTeam()                              // 0x005ec8b0
{
    if (m_team == m_newTeam)
        return;
    if (IsClient() && m_objective == -1 && (!m_spawnFrame || g_mpSettings->gameType != MP_GAME_OBJECTIVES) &&
        !g_gameProfile.IsCooperativeGame())
        GetGame()->GetInterface()->SetObjectiveIcon(m_number, m_newTeam);
    for (int i = 0; i < 3; ++i) {
        I3D_frame *flag = m_flags[i];
        if (!flag)
            continue;
        flag->SetOn(m_newTeam == i);
        flag->Update();
        if (m_newTeam != i)
            continue;
        S_VertexAnimation *animation = g_vertexAnimModule.FindAnimation(C_str(FLAG_ANIMATION));
        I3D_frame *visual = flag->FindChildFrame(FLAG_ANIMATION, ENUMF_WILDMASK | FRAME_MASK_VISUAL);
        if (visual && visual->GetVisualType() == I3DVISUAL_OBJECT && animation) {
            m_flagAnim->stopped = true;
            m_flagAnim->Reset();
            g_vertexAnimModule.RemovePlayer(m_flagAnim, false);
            m_flagAnim->Init(visual, animation);
            g_vertexAnimModule.AddPlayer(m_flagAnim);
            m_flagAnim->pingPong = false;
            m_flagAnim->maxLevel = animation->levelCount - 1;
            m_flagAnim->frameTime = FLAG_FRAME_TIME;
            m_flagAnim->startFrame = 0;
            m_flagAnim->loop = true;
            m_flagAnim->stopped = false;
        }
    }
    m_team = m_newTeam;
}

bool C_MPZone::Contains(const S_vector &point, const I3D_bbox &box, const S_vector &center, float radius) const   // 0x005ecad0
{
    if (m_shape == 0) {
        // (Any NaN is outside.)
        return point.x > box.min.x && point.x < box.max.x && point.y > box.min.y && point.y < box.max.y &&
               point.z > box.min.z && point.z < box.max.z;
    }
    if (m_shape != 1)
        return false;
    const S_vector d(point.x - center.x, point.y - center.y, point.z - center.z);
    return LengthOf((d.z * d.z + d.y * d.y) + d.x * d.x) < radius;
}

void C_MPZone::CountPresence(int present[3], const I3D_bbox &box)   // 0x005ecbe0
{
    S_matrix inverse;
    inverse.Inverse(m_frame->GetMatrix());
    S_vector center;
    float radius;
    ZoneSphere(box, center, radius);
    present[0] = present[1] = present[2] = 0;
    S_NetPlayerState *players = g_netGame.LockPlayers();
    const int count = g_netGame.GetPlayerCount();
    for (int slot = 0; slot < count; ++slot) {
        C_Human *human = players[slot].human;
        // (Each team's count stops at 1.)
        if (!players[slot].IsConnected() || !human || present[human->GetTeam(false)] != 0 || !human->IsAlive() ||
            !human->IsInPlay())
            continue;
        if (Contains(human->GetFrame()->GetWorldPos() * inverse, box, center, radius))
            ++present[players[slot].human->GetTeam(false)];
    }
    C_Human *local = nullptr;
    if (IsClient() && players[g_localPlayerSlot].IsConnected())
        local = players[g_localPlayerSlot].human;
    if (local) {
        m_localInside = Contains(local->GetPosition() * inverse, box, center, radius);
        if (!m_bothWays && g_netGame.GetLocalTeam() == m_startTeam)
            m_localInside = false;
    } else {
        m_localInside = false;
    }
    g_netGame.UnlockPlayers();
}

void C_MPZone::AwardPlayers(int points)                 // 0x005ec310
{
    const I3D_bbox &box = static_cast<I3D_dummy *>(m_frame)->GetBBox();
    S_matrix inverse;
    inverse.Inverse(m_frame->GetMatrix());
    S_vector center;
    float radius;
    ZoneSphere(box, center, radius);
    S_NetPlayerState *players = g_netGame.LockPlayers();
    const int count = g_netGame.GetPlayerCount();
    for (int slot = 0; slot < count; ++slot) {
        C_Human *human = players[slot].human;
        if (!players[slot].IsConnected() || !human || !human->IsAlive() || !human->IsInPlay())
            continue;
        if (Contains(human->GetFrame()->GetWorldPos() * inverse, box, center, radius))
            g_netGame.AddScore(slot, points);
    }
    g_netGame.UnlockPlayers();
}

bool C_MPZone::CanTake(int team) const                  // 0x005ed7e0
{
    // (The mission is the multiplayer map's: the original reads its +0x60 unchecked.)
    if (static_cast<const C_MpMissionDef *>(GetGame()->GetMission())->GetUnknown60() != MISSION_ORDERED_ZONES)
        return true;
    int held = 0;
    const std::vector<C_Actor *> &actors = g_actorsModule.GetActors();
    for (size_t i = PLAYER_SLOTS; i < actors.size(); ++i) {
        // Fix: an empty slot is passed over (the original reads through it).
        if (!actors[i] || actors[i]->GetType() != ACTOR_MP_ZONE)
            continue;
        const auto *zone = static_cast<const C_MPZone *>(actors[i]);
        if (zone->m_team == team)
            ++held;
        if (team == TEAM_AXIS) {
            if (zone->m_number == m_number - 1 && zone->m_team == team)
                return true;
        } else if (team == TEAM_ALLIES) {
            if (zone->m_number == m_number + 1 && zone->m_team == team)
                return true;
        }
    }
    return held == 0 && m_team == TEAM_NONE;
}

void C_MPZone::CheckCapture()                           // 0x005ecf20
{
    if (m_team != m_newTeam || g_gameProfile.IsCooperativeGame())
        return;
    const I3D_bbox box = static_cast<I3D_dummy *>(m_frame)->GetBBox();
    CountPresence(m_present, box);
    if (m_captureState < 0)
        return;
    if (m_captureState <= MPZONE_CAPTURE_STOPPED) {
        if (GetGame()->GetMissionTime() < static_cast<uint32_t>((g_mpSettings->warmup + 5) * 1000))
            return;
        // (The holders defend it; one held by the starting team needs its enemy there.)
        if (m_present[m_team] != 0 && m_bothWays)
            return;
        if (!m_bothWays && m_present[m_startTeam == TEAM_AXIS ? TEAM_ALLIES : TEAM_AXIS] == 0)
            return;
        if (m_objective != -1 && m_team != m_startTeam)
            return;
        if (g_mpSettings->gameType == MP_GAME_OBJECTIVES && m_objective == -1)
            return;
        const int axis = m_present[TEAM_AXIS], allies = m_present[TEAM_ALLIES];
        if (allies == axis) {
            if (m_bothWays)
                return;
            if (axis == 0 && allies == 0)
                return;
        }
        if (!CanTake(axis == 0 ? TEAM_ALLIES : TEAM_AXIS))
            return;
        m_progress = 0;
        m_captureState = MPZONE_CAPTURE_RUNNING;
        SendState(MSG_CAPTURE_START);
        return;
    }
    if (m_captureState != MPZONE_CAPTURE_RUNNING)
        return;
    if (m_present[m_team] != 0 && m_bothWays) {
        m_captureState = MPZONE_CAPTURE_NONE;
        SendState(MSG_CAPTURE_STOP);
        return;
    }
    const int axis = m_present[TEAM_AXIS], allies = m_present[TEAM_ALLIES];
    bool stop;
    if (axis == 0)
        stop = allies == 0;
    else
        stop = allies != 0 && m_bothWays;
    if (!stop && CanTake(axis == 0 ? TEAM_ALLIES : TEAM_AXIS))
        return;
    m_captureState = MPZONE_CAPTURE_STOPPED;
    SendState(MSG_CAPTURE_STOP);
}

void C_MPZone::Tick(int timeDelta)                      // 0x005ec060
{
    if (IsLocalGame())
        return;
    m_checkTime += timeDelta;
    if (m_checkTime > CHECK_INTERVAL) {
        CheckCapture();
        m_checkTime = 0;
    }
    // A held plain zone earns its team points (the flags of an occupation).
    if (m_objective == -1 && m_flags[0])
        g_netGame.AddTeamScore(m_team, static_cast<float>(static_cast<uint32_t>(timeDelta)) * TEAM_POINTS_PER_MS);
    if (m_captureState != MPZONE_CAPTURE_RUNNING)
        return;
    m_progress += timeDelta;
    // A held plain zone turns neutral at half the time.
    if (m_objective == -1 && m_progress > m_captureTime * 500 && m_team != TEAM_NONE) {
        if (!CanTake(m_present[TEAM_AXIS] == 0 ? TEAM_ALLIES : TEAM_AXIS) ||
            (m_present[TEAM_AXIS] == 0 && m_present[TEAM_ALLIES] == 0)) {
            m_progress = 0;
            m_captureState = MPZONE_CAPTURE_STOPPED;
            SendState(MSG_CAPTURE_STOP);
            return;
        }
        m_progress = 0;
        m_newTeam = TEAM_NONE;
        ApplyTeam();
        SendState(MSG_TEAM);
        SendState(MSG_CAPTURE_START);
        AwardPlayers(NEUTRALIZE_POINTS);
    }
    const int scale = m_bothWays && m_objective == -1 ? 500 : 1000;
    if (m_progress <= m_captureTime * scale)
        return;
    if (m_team != TEAM_NONE && m_bothWays && m_objective == -1)
        return;
    const I3D_bbox box = static_cast<I3D_dummy *>(m_frame)->GetBBox();
    CountPresence(m_present, box);
    const int axis = m_present[TEAM_AXIS], allies = m_present[TEAM_ALLIES];
    if (!CanTake(axis == 0 ? TEAM_ALLIES : TEAM_AXIS) || (axis == 0 && allies == 0)) {
        m_progress = 0;
        m_captureState = MPZONE_CAPTURE_STOPPED;
        SendState(MSG_CAPTURE_STOP);
        return;
    }
    // Taken: by the team in it, or (an objective) from its holders.
    m_newTeam = m_objective == -1 ? (axis == 0 ? TEAM_ALLIES : TEAM_AXIS) : (m_team != TEAM_ALLIES ? 1 : 0);
    m_captureState = MPZONE_CAPTURE_NONE;
    m_progress = 0;
    if (m_objective == -1) {
        ApplyTeam();
        AwardPlayers(CAPTURE_POINTS);
        SendState(MSG_TEAM);
        return;
    }
    if (m_newTeam != m_startTeam) {
        m_team = m_newTeam;
        GetGame()->SetObjectiveStatus(m_objective, 1);
    }
    SendState(MSG_TEAM);
}

void C_MPZone::Update(int frameTime)                    // 0x005ec540
{
    if (IsLocalGame())
        return;
    if (m_resetPending) {
        m_resetPending = false;
        m_team = -1;
        ApplyTeam();
    }
    if (m_captureState != MPZONE_CAPTURE_RUNNING) {
        if (m_progressShown && m_captureTime != 0 && m_team != TEAM_NONE)
            m_progressShown = false;
        return;
    }
    if (!IsServer()) {
        // A pure client checks the local player once a second.
        m_progress += frameTime;
        m_checkTime += frameTime;
        if (m_checkTime <= CHECK_INTERVAL)
            return;
        m_checkTime = 0;
        S_NetPlayerState *players = g_netGame.LockPlayers();
        C_Human *local = players[g_localPlayerSlot].human;
        g_netGame.UnlockPlayers();
        if (!local || local->GetHealth() < 1) {
            m_localInside = false;
        } else {
            const I3D_bbox &box = static_cast<I3D_dummy *>(m_frame)->GetBBox();
            S_matrix inverse;
            inverse.Inverse(m_frame->GetMatrix());
            S_vector center;
            float radius;
            ZoneSphere(box, center, radius);
            m_localInside = Contains(local->GetPosition() * inverse, box, center, radius);
            if (!m_bothWays && g_netGame.GetLocalTeam() == m_startTeam)
                m_localInside = false;
        }
    }
    if (m_localInside && !m_progressShown && m_captureTime != 0) {
        if (C_Human *local = g_netGame.GetLocalHuman())
            local->SetMpZone(m_netId);
        m_progressShown = true;
    }
}

void C_MPZone::GetSpawnPoint(int index, S_vector4 *pos, S_vector4 *dir) const   // 0x005ed550
{
    I3D_frame *point = m_spawnFrame->GetChild(index);
    const S_matrix &world = point->GetMatrix();
    *pos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    // The z axis normalised as the inline code does: summed (y² + x²) + z²; a NaN component
    // counts as not 0 and not negative.
    S_vector z(world.m[2][0], world.m[2][1], world.m[2][2]);
    const float square = (z.y * z.y + z.x * z.x) + z.z * z.z;
    if (!(std::fabs(square - 1.0f) < 1e-08f)) {
        if (!(square < 1e-08f)) {
            const float scale = 1.0f / std::sqrt(square);
            z = S_vector(z.x * scale, z.y * scale, z.z * scale);
        } else if (z.x != 0.0f) {
            z.x = z.x < 0.0f ? -1.0f : 1.0f;
        } else if (z.z != 0.0f) {
            z.z = z.z < 0.0f ? -1.0f : 1.0f;
        } else {
            z.y = z.y < 0.0f ? -1.0f : 1.0f;
        }
    }
    *dir = {z.x, z.y, z.z, world.m[2][3]};
}

bool C_MPZone::PickSpawnPosition(S_vector4 *pos, S_vector4 *dir, bool cycle)   // 0x005ed700
{
    int tries = 0;
    do {
        if (!cycle && m_spawnIndex == m_spawnFrame->NumChildren())
            return false;
        GetSpawnPoint(m_spawnIndex, pos, dir);
        ++m_spawnIndex;
        if (cycle)
            m_spawnIndex = static_cast<int>(static_cast<uint32_t>(m_spawnIndex) %
                                            static_cast<uint32_t>(m_spawnFrame->NumChildren()));
        // (Its spawn points used up: neutral.)
        if (m_team != TEAM_NONE && m_spawnIndex == m_spawnFrame->NumChildren()) {
            m_team = TEAM_NONE;
            SendState(MSG_TEAM);
        }
        ++tries;
    } while (IsSpawnBlocked(*pos) && tries < MAX_SPAWN_TRIES && g_mpSettings->gameType != MP_GAME_OBJECTIVES);
    return tries < MAX_SPAWN_TRIES;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_MPZoneCreator::Create()                      // 0x005ebe40
{
    return new C_MPZone();                              // _aligned_malloc(0x120, 16) in the original
}

void C_MPZoneCreator::Destroy(C_Actor *actor)           // 0x005ebeb0
{
    delete actor;
}

static C_MPZoneCreator g_mpZoneCreator;                 // 0x008ac9dc

// Registers the creator (static initialiser 0x005ebe20, thunk 0x005ebe10).
static const bool g_mpZoneCreatorRegistered = [] {
    g_actorCreators[ACTOR_MP_ZONE] = &g_mpZoneCreator;
    return true;
}();
