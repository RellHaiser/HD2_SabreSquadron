// Spawn points (C_SpawnPoint; original 0x00611730..0x006131d0).
#include "game/spawn_point.h"

#include <windows.h>
#include <mmsystem.h>                                   // timeGetTime

#include <cstdio>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/item_defs.h"
#include "game/pending.h"                               // g_gameRole, g_netGame, g_localPlayerSlot
#include "game/profile.h"                               // S_SquadSoldier, g_mpSettings, g_gameProfile
#include "game/resource_cache.h"                        // g_modelCache
#include "game/team.h"                                  // g_teamManager
#include "game/world.h"                                 // C_MPZone, GetCameraModule
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// ReadProperties' chunks: 0 the version (must be 3) first, then the others.
enum : uint16_t {
    PROPERTY_VERSION = 0,
    PROPERTY_TEAM = 1,
    PROPERTY_KIND = 2,
    PROPERTY_UNIFORM = 3,
    PROPERTY_UNKNOWN98 = 4,
    PROPERTY_NPC = 5,
};
constexpr int PROPERTIES_VERSION = 3;

constexpr int SPAWN_TRIES = 32;                         // random spawn points tried (31 in fact)
const char DEFAULT_MODEL[] = "NA01";

// The spawn message.
constexpr uint32_t MSG_SPAWNED = 0;
constexpr int MSG_KIND_BITS = 2;
constexpr int SLOT_BITS = 8;
constexpr int CONNECTION_BITS = 32;
constexpr int PLAYER_NAME_BUFFER = 0xFF;
constexpr int HEALTH_BITS = 10;
constexpr int STAT_BITS = 7;
// The stats after the health, in the message's order.
constexpr int STAT_ORDER[] = {2, 1, 3, 4, 5, 6, 7, 8, 9, 11, 10};
// A client's stats until the message's (single player sends none).
constexpr int DEFAULT_STATS[STAT_COUNT] = {800, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};

}  // namespace

void C_SpawnPoint::RestartMission(bool)                 // 0x006117f0
{
    // (The original also works out the frame's normalised z axis and does nothing with it.)
    if (m_free || !m_spawned)
        return;
    if (const S_FrameData *data = m_frame ? m_frame->GetData() : nullptr) {
        if (data->script)
            data->script->OnUse(0);
    }
}

void C_SpawnPoint::StartMission(bool server)            // 0x00611970 (DS 0x0063bb90)
{
    Activate(server);
    m_free = true;
}

void C_SpawnPoint::StopMission(bool server)             // 0x00611990 (DS 0x0063bbc0)
{
    Deactivate(server);
    m_free = true;
}

bool C_SpawnPoint::ReadProperties(C_chunk &chunk)       // 0x006119b0
{
    uint16_t id;
    if (chunk.RemainingSize() <= 0 || !chunk.Descend(id) || id != PROPERTY_VERSION)
        return false;
    // (The team is read into the version's value: a short team chunk gives 3.)
    int first = 0;                                      // (the original goes on with stack values)
    chunk.Read(first);
    chunk.Ascend();
    if (first != PROPERTIES_VERSION)
        return false;
    int kind = 0, uniform = 0, unknown98 = 0, npc = 0;
    // Quirk kept: a chunk of another id is entered and not left.
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case PROPERTY_TEAM:
            chunk.Read(first);
            chunk.Ascend();
            m_team = first;
            break;
        case PROPERTY_KIND:
            chunk.Read(kind);
            chunk.Ascend();
            m_kind = kind;
            break;
        case PROPERTY_UNIFORM:
            chunk.Read(uniform);
            chunk.Ascend();
            m_uniform = uniform;
            break;
        case PROPERTY_UNKNOWN98:
            chunk.Read(unknown98);
            chunk.Ascend();
            m_unknown98 = unknown98;
            break;
        case PROPERTY_NPC:
            chunk.Read(npc);
            chunk.Ascend();
            m_npc = npc;
            break;
        default:
            break;
        }
    }
    return true;
}

void C_SpawnPoint::Spawn(int slot, S_SoldierState *state, int *stats)   // 0x00611c90 (DS 0x0063bf90)
{
    if (!IsServer())
        return;
    if (IsLocalGame())
        m_free = false;
    const int actorSlot = IsLocalGame() ? g_actorsModule.FindFreePlayerSlot() : slot;
    const int uniform = IsLocalGame() ? m_uniform : -1;
    const S_matrix &world = m_frame->GetMatrix();
    S_vector4 pos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    const S_vector axis = world(2).NormalizedGame();
    S_vector4 dir = {axis.x, axis.y, axis.z, world.m[2][3]};
    if (!IsLocalGame()) {
        const int type = g_mpSettings->gameType;
        if (type == MP_GAME_OCCUPATION || type == MP_GAME_OBJECTIVES || type == MP_GAME_COOPERATIVE) {
            auto *zone = static_cast<C_MPZone *>(g_actorsModule.GetActorById(state->spawnZone));
            if (!zone->PickSpawnPosition(&pos, &dir, g_mpSettings->allowRespawn != 0)) {
                GetGameApp()->SendGameMessage(GAME_MSG_SPAWN_FAILED, 0, slot, 1, false);
                return;
            }
            if (g_mpSettings->gameType == MP_GAME_OCCUPATION)
                FaceTargetZone(&dir, pos, state->team);
        } else {
            for (int tries = SPAWN_TRIES; --tries != 0;) {
                const int team = g_mpSettings->gameType == MP_GAME_DEATHMATCH ? 0 : state->team;
                const C_Actor *point = g_actorsModule.FindRandomSpawnPoint(1, team);
                pos = point->GetPosition4();
                dir = point->GetDirection4();
                if (!IsSpawnBlocked(pos))
                    break;
            }
        }
    }
    if (state) {
        auto *soldier = static_cast<S_SquadSoldier *>(state);
        if (state->uniform.typeId == -1) {
            S_SoldierItem item;
            item.uid = -1;
            item.typeId = static_cast<int16_t>(m_uniform);
            item.present = 1;
            S_SoldierItem displaced;
            soldier->PutItem(&displaced, SQUAD_SLOT_UNIFORM, item);
        }
        soldier->SplitStacks();
    }
    C_Human *human = CreateSoldier(slot, actorSlot, pos, dir, state, stats, uniform, m_npc != 0);
    m_spawned = human;
    if (!human)
        return;
#ifndef HD2_DEDICATED   // the server names no local player
    if (IsLocalGame())
        g_netGame.SetPlayer(slot, 0, "name");
#endif
    if (!IsLocalGame() && slot == g_localPlayerSlot && GetGame()->GetRoundState() != ROUND_WARMUP)
        GetGame()->SetCameraMode(GAME_CAMERA_FOLLOW);
    human->StartMission(true);
    if (IsClient())
        human->StartMission(false);
    if (!IsLocalGame()) {
        if (human->GetControllerType() == HUMAN_CONTROLLER_PLAYER) {
            human->SetPlayerTeam(state->team, false);
            auto *controller = static_cast<C_PlayerController *>(human->GetController());
            controller->SetSpectating(state->spectating);
            controller->SetVip(state->vip);
        }
        if (g_gameProfile.GetDetailSettings()->IsVoiceChatEnabled())
            g_netGame.UpdateVoiceTeam();
    }
    if (GetGame()->GetMode() == GAME_MODE_LOADING) {
        // Loading a saved game: set up at once, no message.
        S_NetPlayerState *players = g_netGame.LockPlayers();
        if (IsClient())
            g_netGame.SetPlayer(slot, players[slot].connection, players[slot].name);
        SetupSpawned(slot, actorSlot, pos, dir, state, nullptr);
        g_netGame.UnlockPlayers();
    } else if (C_BitStream *stream = CreateActorMessage(m_netId, true)) {
        auto *msg = static_cast<C_NetMessage *>(stream);
        msg->WriteBits(MSG_SPAWNED, MSG_KIND_BITS);
        msg->WriteBits(static_cast<uint32_t>(slot), SLOT_BITS);
        msg->WriteBits(static_cast<uint32_t>(actorSlot), SLOT_BITS);
        msg->WriteQuantized(&pos.x, msg->GetSender(-1), 7);
        msg->WriteDirection3D(S_vector(dir.x, dir.y, dir.z), -1);
        msg->WriteBits(static_cast<uint32_t>(g_actorsModule.GetPlayerSlotUse(actorSlot)), SLOT_BITS);
        S_NetPlayerState *players = g_netGame.LockPlayers();
        msg->WriteString(players[slot].name);
        msg->WriteBits(static_cast<uint32_t>(players[slot].connection), CONNECTION_BITS);
        g_netGame.UnlockPlayers();
        NetWriteSoldierState(msg, *state, false);
        if (!IsLocalGame()) {
            msg->WriteBits(static_cast<uint32_t>(stats[0]), HEALTH_BITS);
            for (int stat : STAT_ORDER)
                msg->WriteBits(static_cast<uint32_t>(stats[stat]), STAT_BITS);
        }
        SendActorMessage(msg);
    }
    if (const S_FrameData *data = m_frame ? m_frame->GetData() : nullptr) {
        if (data->script)
            data->script->OnUse(0);
    }
}

void C_SpawnPoint::SetupSpawned(int slot, int actorSlot, const S_vector4 &pos, const S_vector4 &dir,
                                S_SoldierState *state, const int *stats)   // 0x006123a0
{
    if (!m_frame)
        return;
    C_Human *human;
    if (!IsServer())
        human = CreateSoldier(slot, actorSlot, pos, dir, state, stats, -1, m_npc != 0);
    else if (IsLocalGame())
        human = static_cast<C_Human *>(m_spawned);
    else
        human = static_cast<C_Human *>(g_actorsModule.GetActorById(actorSlot));
    if (!human)
        return;
    if (!IsLocalGame() && slot == g_localPlayerSlot && GetGame()->GetRoundState() != ROUND_WARMUP)
        GetGame()->SetCameraMode(GAME_CAMERA_FOLLOW);
    if (!IsServer())
        human->StartMission(false);
    if (slot == g_localPlayerSlot && !m_npc) {
        GetCameraModule()->SetSpecialMode(0, true, nullptr);
        GetCameraModule()->SetSpecialMode(2, true, nullptr);
        if (!g_teamManager.GetPlayerTeam()->IsMember(human))
            g_teamManager.GetPlayerTeam()->AddMember(human);
    }
    if (!IsLocalGame()) {
        const int type = human->GetControllerType();
        if (type == HUMAN_CONTROLLER_PLAYER) {
            human->SetPlayerTeam(state->team, false);
            if (human->IsLocalPlayer())
                g_teamManager.GetPlayerTeam()->SelectMember(human);
            auto *controller = static_cast<C_PlayerController *>(human->GetController());
            controller->SetSpectating(state->spectating);
            controller->SetVip(state->vip);
            GetGame()->OnPlayerSpawned(slot, human);
        } else if (type == HUMAN_CONTROLLER_REMOTE) {
            auto *controller = static_cast<C_RemoteController *>(human->GetController());
            controller->SetVip(state->vip);
            controller->SetSpectating(state->spectating);
        }
    }
    if (!IsLocalGame() && !IsServer() && g_gameProfile.GetDetailSettings()->IsVoiceChatEnabled())
        g_netGame.UpdateVoiceTeam();
}

void C_SpawnPoint::OnMessage(C_BitStream *stream)       // 0x006125a0
{
    if (!IsClient() || !stream)
        return;
    auto *msg = static_cast<C_NetMessage *>(stream);
    if (msg->ReadBits(MSG_KIND_BITS) != MSG_SPAWNED)
        return;
    const int slot = static_cast<int>(msg->ReadBits(SLOT_BITS));
    const int actorSlot = static_cast<int>(msg->ReadBits(SLOT_BITS));
    float place[3] = {};
    msg->ReadQuantized(place, msg->GetSender(-1), 7);
    S_vector heading(0.0f, 0.0f, 0.0f);
    msg->ReadDirection3D(heading, -1);
    // (The w components are a stale local in the original.)
    const S_vector4 pos = {place[0], place[1], place[2], 0.0f};
    const S_vector4 dir = {heading.x, heading.y, heading.z, 0.0f};
    const int slotUse = static_cast<int>(msg->ReadBits(SLOT_BITS));
    char name[PLAYER_NAME_BUFFER];
    msg->ReadString(name, PLAYER_NAME_BUFFER);
    const int connection = static_cast<int>(msg->ReadBits(CONNECTION_BITS));
    if (!IsServer())
        g_actorsModule.SetPlayerSlotUse(actorSlot, static_cast<int8_t>(slotUse) - 1);
    // The state as the original builds it before the message's.
    S_SquadSoldier state;
    state.progress = S_SoldierProgress{0, 0, 0, 0, 0, 0, 0, 0, 0};
    state.speechSet = -1;
    state.unknown34 = -1;
    state.unknown38 = -1;
    state.team = -1;
    state.spawnZone = 0x3FF;
    state.spectating = false;
    state.vip = false;
    state.deployed = true;
    state.profileIndex = -1;
    state.health = 0;
    NetReadSoldierState(msg, state, false);
    int stats[STAT_COUNT];
    std::memcpy(stats, DEFAULT_STATS, sizeof stats);
    if (!IsLocalGame()) {
        stats[0] = static_cast<int>(msg->ReadBits(HEALTH_BITS));
        for (int stat : STAT_ORDER)
            stats[stat] = static_cast<int>(msg->ReadBits(STAT_BITS));
    }
    if (IsClient())
        g_netGame.SetPlayer(slot, connection, name);
    SetupSpawned(slot, actorSlot, pos, dir, &state, IsLocalGame() ? nullptr : stats);
}

C_Human *C_SpawnPoint::CreateSoldier(int slot, int actorSlot, const S_vector4 &pos, const S_vector4 &dir,
                                     S_SoldierState *state, const int *stats, int uniform, bool npc)   // 0x00612a60 (DS 0x0063ca70)
{
    I3D_frame *frame = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    if (!frame)
        return nullptr;
    std::string model = DEFAULT_MODEL;
    int uniformId = state ? state->uniform.typeId : -1;
    if (IsLocalGame())
        uniformId = uniform;
    if (uniformId != -1) {
        C_ItemDef *def = GetItemDef(uniformId);
        if (def && def->GetBehaviour() == ITEM_BEHAVIOUR_UNIFORM) {
            // The uniform's model name up to its last dot (the dot kept).
            model = def->GetModelName();
            const size_t dot = model.rfind('.');
            if (dot != std::string::npos)
                model.erase(dot + 1);
        }
    }
    if (g_modelCache.OpenInto(static_cast<I3D_model *>(frame), model.c_str()) != I3D_OK) {
        frame->Release();
        return nullptr;
    }
    if (I3D_scene *scene = GetGame()->GetScene())
        frame->LinkTo(scene->GetPrimarySector(), 0);
    frame->SetPosInline(S_vector(pos.x, pos.y, pos.z));
    frame->SetWorldDir(S_vector(dir.x, dir.y, dir.z), 0.0f);
    char name[32];
    std::snprintf(name, sizeof name, "_PLAYER_%2d", actorSlot);
    frame->SetName(name);
    frame->Update();
    auto *human = static_cast<C_Human *>(g_actorsModule.CreateActor(ACTOR_HUMAN, frame, true, actorSlot));
    if (!human) {
        frame->Release();
        return nullptr;
    }
    human->SetFrame(frame);
    frame->Release();
    human->Setup(*state, npc ? HUMAN_ROLE_NPC_TEAM : HUMAN_ROLE_PLAYER, 1, stats, slot);
    if (!IsLocalGame()) {
        S_NetPlayerState *players = g_netGame.LockPlayers();
        players[slot].human = human;
        players[slot].activityTime = timeGetTime();
        if (IsServer() && g_gameProfile.IsCooperativeGame())
            g_teamManager.GetCoopTeam()->AddMember(players[slot].human);
        g_netGame.UnlockPlayers();
    }
    return human;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_SpawnPointCreator::Create()                  // 0x00611760
{
    return new C_SpawnPoint();                          // _aligned_malloc(0xb0, 16) in the original
}

void C_SpawnPointCreator::Destroy(C_Actor *actor)       // 0x006117d0
{
    delete actor;
}

static C_SpawnPointCreator g_spawnPointCreator;         // 0x008aca60

// Registers the creator (static initialiser 0x00611740, thunk 0x00611730).
static const bool g_spawnPointCreatorRegistered = [] {
    g_actorCreators[ACTOR_SPAWN_POINT] = &g_spawnPointCreator;
    return true;
}();
