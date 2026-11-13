// Breakable fences (C_WireCut; original 0x005e2070..0x005e2a10).
#include "game/wire_cut.h"

#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateTempActorMessage
#include "game/game.h"
#include "game/pending.h"                               // g_gameRole, g_texts
#include "game/physics.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

constexpr uint16_t CHUNK_CUT = 1000;                    // the cut's direction
constexpr int TEXT_WIRE_CUT = 0x1BF6;                   // the actor's name
constexpr float POINT_SEARCH_RADIUS = 5.0f;             // m: the points on either side of the fence
constexpr float CUT_MIN_SQUARE = 1e-16f;                // a shorter direction: not cut

}  // namespace

C_WireCut::C_WireCut() : C_Actor(ACTOR_WIRE_CUT)        // 0x005e2130
{
    m_name = g_texts.Get(TEXT_WIRE_CUT);
}

bool C_WireCut::IsCut() const                           // 0x005e24c0
{
    return !(m_cutDir.x * m_cutDir.x + m_cutDir.y * m_cutDir.y + m_cutDir.z * m_cutDir.z < CUT_MIN_SQUARE);
}

void C_WireCut::SetPointsType(uint8_t type)             // 0x005e2420
{
    if (m_points[0] != CHECKPOINT_NONE)
        GetGame()->GetCheckpoints()->Get(m_points[0])->type = type;
    if (m_points[1] != CHECKPOINT_NONE)
        GetGame()->GetCheckpoints()->Get(m_points[1])->type = type;
}

void C_WireCut::OpenWay()                               // 0x005e2480
{
    if (!IsLocalGame())
        return;
    GetGame()->GetCheckpoints()->JoinPoints(m_points[0], m_points[1]);
    SetPointsType(CHECKPOINT_TYPE_FENCE_GAP);
}

bool C_WireCut::Cut(const S_vector &dir, bool hideBrokenPart)   // 0x005e2510
{
    if (!m_door->Hit(PHYS_HIT_BREAK, &dir, nullptr))
        return false;
    if (hideBrokenPart)
        m_door->HideBrokenPart();
    OpenWay();
    // (Only the heading goes: WriteDirection drops y.)
    auto *msg = static_cast<C_NetMessage *>(CreateTempActorMessage(m_netId, true));
    msg->WriteDirection(dir, -1);
    SendTempActorMessage(msg);
    if (const S_FrameData *data = m_frame->GetData()) {
        if (data->script)
            data->script->OnUse(1);
    }
    m_cutDir = dir;
    return true;
}

void C_WireCut::OnCut(const S_vector &dir)              // 0x005e25c0
{
    if (IsServer())
        return;
    // (OpenWay does nothing on a client.)
    if (m_door->Hit(PHYS_HIT_BREAK, &dir, nullptr))
        OpenWay();
}

void C_WireCut::OnMessage(C_BitStream *msg)             // 0x005e25f0
{
    S_vector dir;
    static_cast<C_NetMessage *>(msg)->ReadDirection(dir, -1);
    OnCut(dir);
}

void C_WireCut::StartMission(bool server)               // 0x005e2260
{
    Activate(server);
    m_cutDir = S_vector(0.0f, 0.0f, 0.0f);
    if (!server)
        return;
    m_points[0] = CHECKPOINT_NONE;
    m_points[1] = CHECKPOINT_NONE;
    if (!IsLocalGame())
        return;
    // The point nearest to the fence, then the one nearest to that point's mirror image through
    // the fence.
    const S_vector pos = m_frame->GetWorldPos();
    m_points[0] = static_cast<uint16_t>(GetGame()->GetCheckpoints()->FindNearestPoint(pos, POINT_SEARCH_RADIUS, -1.0f));
    if (m_points[0] == CHECKPOINT_NONE)
        return;
    const S_vector first = GetGame()->GetCheckpoints()->Get(m_points[0])->pos;
    const S_vector mirror = (pos - first) + pos;
    m_points[1] = static_cast<uint16_t>(GetGame()->GetCheckpoints()->FindNearestPoint(mirror, POINT_SEARCH_RADIUS, -1.0f));
}

void C_WireCut::StopMission(bool server)                // 0x005e23e0
{
    Deactivate(server);
    if (!server)
        return;
    SetPointsType(CHECKPOINT_TYPE_NORMAL);
    m_points[1] = CHECKPOINT_NONE;
    m_points[0] = CHECKPOINT_NONE;
}

bool C_WireCut::SaveGame(C_chunk &chunk)                // 0x005e2630
{
    chunk.Begin(CHUNK_CUT);
    chunk.Write(m_cutDir);
    chunk.Ascend();
    return true;
}

bool C_WireCut::LoadGame(C_chunk &chunk)                // 0x005e2730
{
    // Quirk kept: a chunk of another id is entered and not left (its contents are read as
    // chunks).
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != CHUNK_CUT)
            continue;
        chunk.Read(m_cutDir);
        chunk.Ascend();
    }
    // (IsCut with the squares summed in another order.)
    if (!(m_cutDir.z * m_cutDir.z + m_cutDir.x * m_cutDir.x + m_cutDir.y * m_cutDir.y < CUT_MIN_SQUARE))
        Cut(m_cutDir, false);
    return true;
}

void C_WireCut::WriteState(C_BitStream *msg)            // 0x005e2930
{
    if (!IsCut()) {
        msg->WriteBits(0, 1);
        return;
    }
    msg->WriteBits(1, 1);
    static_cast<C_NetMessage *>(msg)->WriteDirection(m_cutDir, -1);
}

void C_WireCut::ReadState(C_BitStream *msg)             // 0x005e29a0
{
    if (!msg->ReadBits(1))
        return;
    S_vector dir;
    static_cast<C_NetMessage *>(msg)->ReadDirection(dir, -1);
    OnCut(dir);
}

int C_WireCut::GetMaterialId(I3D_frame *part)           // 0x005e2910 (DS 0x00600bb0)
{
    const int id = GetFrameMaterialId(part);
    return id < 0 ? 11 : id;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_WireCutCreator::Create()                     // 0x005e20a0
{
    return new C_WireCut();                             // _aligned_malloc(0xb0, 16) in the original
}

void C_WireCutCreator::Destroy(C_Actor *actor)          // 0x005e2110
{
    delete actor;
}

static C_WireCutCreator g_wireCutCreator;               // 0x008ac9bc

// Registers the creator (static initialiser 0x005e2080, thunk 0x005e2070).
static const bool g_wireCutCreatorRegistered = [] {
    g_actorCreators[ACTOR_WIRE_CUT] = &g_wireCutCreator;
    return true;
}();
