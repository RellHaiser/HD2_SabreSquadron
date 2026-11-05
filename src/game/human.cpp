// C_Human: creation, mission start / stop / restart, saved games, mission properties, soldier
// setup and the per-frame / per-tick updates (original source 0x0059be30..0x0059ffd0 and
// 0x005a8790..0x005a9980). Damage is in human_combat.cpp, the inventory in human_items.cpp,
// sight and hearing in human_senses.cpp, the network state in human_net.cpp and the small
// controller wrappers in human_control.cpp.
#include "game/first_person_view.h"
#include "game/human.h"

#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/collision.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/interface.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/surface_marks.h"
#include "game/world.h"
#include "ls3d/igraph.h"
#include "script/script.h"

namespace {

// Saved-game chunks of a human.
enum : uint16_t {
    CHUNK_HUMAN = 1000,                 // C_Actor::SaveGame()
    CHUNK_HUMAN_STATE = 0x3E9,
    CHUNK_HUMAN_BODY = 0x3EA,
    CHUNK_HUMAN_CONTROLLER = 0x3EB,
    CHUNK_HUMAN_EVENTS = 0x3EC,
    CHUNK_HUMAN_STATUS = 0x3ED,         // inside CHUNK_HUMAN_STATE ...
    CHUNK_HUMAN_VEHICLE = 0x3EE,
    CHUNK_HUMAN_CARRIED = 0x3EF,
    CHUNK_HUMAN_PROGRESS = 0x3F0,
    CHUNK_HUMAN_ACTION = 0x3F1,         // ... up to here
    CHUNK_HUMAN_FORMATION = 0x3F2,
    CHUNK_HUMAN_FACE = 0x3FC,
    CHUNK_HUMAN_AI_MEMORY = 0x406,
};

// Version of the mission file properties (ReadProperties accepts 11 and later).
constexpr uint8_t PROPERTIES_VERSION = 15;
constexpr uint8_t PROPERTIES_MIN_VERSION = 11;

// Text ids of the default names (GetName()).
enum : int {
    TEXT_ENEMY = 0x1BEF,
    TEXT_CIVILIAN = 0x1BFD,
    TEXT_SPAWN_PROTECTION = 0x4F59,
};

C_SpeechSets *g_speechSets = nullptr;                   // 0x008ac448

}  // namespace

// ---------------------------------------------------------------------------------- creator

C_Actor *C_HumanCreator::Create()
{
    return new C_Human();
}

void C_HumanCreator::Destroy(C_Actor *actor)
{
    delete actor;
}

static C_HumanCreator g_humanCreator;                   // 0x008ac428

// Registers the creator (static initialiser 0x0059be40).
static const bool g_humanCreatorRegistered = [] {
    g_actorCreators[ACTOR_HUMAN] = &g_humanCreator;
    return true;
}();

// ------------------------------------------------------------------------------ lifetime

C_Human::C_Human() : C_Actor(ACTOR_HUMAN)
{
    m_events = new C_HumanEvents();
    // Network games: uniforms cannot be taken off other humans.
    m_unknown1a5 = !IsLocalGame();
    m_aiMemory = new C_HumanAIMemory(this);
}

C_Human::~C_Human()
{
    if (m_periodicLink != -1)
        PeriodicLinks_Remove(m_periodicLink);
    if (!IsLocalGame() && m_netId < C_ActorsModule::PLAYER_SLOTS) {
        S_NetPlayerState *players = g_netGame.LockPlayers();
        players[m_netId].human = nullptr;
        g_netGame.UnlockPlayers();
    }
    m_events->Reset();
    delete m_events;
    Done();
    delete m_aiMemory;
}

// Puts every heard sound back into the pool (inline in the original).
void C_Human::ClearHeardSounds()
{
    for (size_t i = m_heardSounds.size(); i-- > 0;) {
        if (m_heardSounds[i])
            FreeRememberedSound(m_heardSounds[i]);
    }
    m_heardSounds.clear();
}

void C_Human::ResetAIMemory()
{
    if (m_aiMemory)
        m_aiMemory->Reset(this);
}

C_SpeechSets *C_Human::GetSpeechSets()
{
    if (!g_speechSets)
        g_speechSets = new C_SpeechSets();
    return g_speechSets;
}

// ---------------------------------------------------------------------------- restarts

void C_Human::RestartSinglePlayer(bool server)
{
    if (m_fadedOut) {
        m_fadedOut = false;
        GetGEffects()->FadeHuman(this, 0, true);
        m_body.SetModelVisible(true);
        SurfaceMarks_SetHumanVisible(this, true);
    }
    m_faceTexture = "";
    SetLeader(nullptr);
    DisbandFormation();
    m_chatTime = 0;
    m_chatText[0] = '\0';
    Reactivate(server);
    m_seenFriends.clear();
    m_seenEnemies.clear();
    m_seenBodies.clear();
    m_seenTraitors.clear();
    m_inPlay = true;
    ClearHeardSounds();
    std::memcpy(m_stats, m_initialStats, sizeof(m_stats));
    m_stealthTime = 0;
    m_enduranceTime = 0;
    m_stamina = static_cast<float>(m_stats[STAT_ENDURANCE]);
    m_progress = S_SoldierProgress();
    m_velocity.x = m_velocity.y = m_velocity.z = 0.0f;
    m_actionText = 0;
    m_actionTotal = 0;
    m_actionTime = 0;
    m_vehicle = nullptr;
    m_seat = -1;
    m_bleeding = 0.0f;
    m_bloodDropTime = 0;
    const S_matrix &world = m_frame->GetMatrix();
    m_lastPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_unknown1ac = 0;
    m_carried = nullptr;
    OnHandsChanged();
    m_legHits = 0;
    m_body.Reset();
    m_controller->Detach();
    m_events->Reset();
    delete m_controller;
    m_controller = nullptr;
    m_sightId = -1;
    m_sightTargetId = -1;
    GetSoundModule()->RemoveListener(m_soundListener);
    m_soundListener = -1;

    if (m_role == HUMAN_ROLE_PLAYER && m_profileIndex >= 0) {
        if (S_SoldierState *state = g_gameProfile.FindSoldierState(m_profileIndex))
            Setup(*state, m_role, m_group, nullptr, -1);
    }
    const S_FrameData *data = m_frame->GetData();
    if (data && data->script) {
        CreateController(HUMAN_CONTROLLER_AI);
    } else {
        switch (m_role) {
        case HUMAN_ROLE_PLAYER:
            AssignPlayerController(false);
            if (IsClient() && server)
                g_teamManager.GetPlayerTeam()->AddMember(this);
            break;
        case HUMAN_ROLE_ENEMY:
            CreateController(HUMAN_CONTROLLER_ENEMY);
            break;
        case HUMAN_ROLE_NEUTRAL:
        case HUMAN_ROLE_NPC_TEAM:
        case HUMAN_ROLE_NPC_TEAM_5:
            CreateController(HUMAN_CONTROLLER_NEUTRAL);
            break;
        default:
            break;
        }
    }
    if (IsLocalPlayer() && IsClient()) {
        GetGame()->ShowFirstPersonView(GetGame()->IsFirstPersonView());
        GetGame()->GetFirstPersonView()->Refresh();
    }
    m_soundListener = GetSoundModule()->AddListener(m_frame, OnSoundHeard);
    m_sightId = g_sightModule.AddObserver(m_frame, SIGHT_ALL);
    m_sightTargetId = g_sightModule.AddTarget(m_frame, SIGHT_ALL);
    if (server)
        m_periodicLink = PeriodicLinks_Add(this, 0);
    ResetAIMemory();
}

void C_Human::RestartCoop()
{
    m_faceTexture = "";
    SetLeader(nullptr);
    DisbandFormation();
    m_chatTime = 0;
    m_chatText[0] = '\0';
    m_seenFriends.clear();
    m_seenEnemies.clear();
    m_seenBodies.clear();
    m_seenTraitors.clear();
    ClearHeardSounds();
    std::memcpy(m_stats, m_initialStats, sizeof(m_stats));
    m_stamina = static_cast<float>(m_stats[STAT_ENDURANCE]);
    m_stealthTime = 0;
    m_enduranceTime = 0;
    m_progress = S_SoldierProgress();
    m_actionText = 0;
    m_actionTotal = 0;
    m_actionTime = 0;
    SetBodyDetached(false);
    // Player slots stay out of play until the players spawn.
    m_inPlay = m_netId >= C_ActorsModule::PLAYER_SLOTS;
    m_velocity.x = m_velocity.y = m_velocity.z = 0.0f;
    m_vehicle = nullptr;
    m_seat = -1;
    m_bleeding = 0.0f;
    m_bloodDropTime = 0;
    const S_matrix &world = m_frame->GetMatrix();
    m_lastPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_unknown1ac = 0;
    m_carried = nullptr;
    OnHandsChanged();
    m_legHits = 0;
    m_body.Reset();
    m_controller->Detach();
    m_events->Reset();
    delete m_controller;
    m_controller = nullptr;
    m_sightId = -1;
    m_sightTargetId = -1;
    if (m_soundListener != -1)
        GetSoundModule()->RemoveListener(m_soundListener);
    m_soundListener = -1;

    const S_FrameData *data = m_frame->GetData();
    if (data && data->script) {
        CreateController(HUMAN_CONTROLLER_AI);
    } else {
        switch (m_role) {
        case HUMAN_ROLE_PLAYER:
            AssignPlayerController(false);
            break;
        case HUMAN_ROLE_ENEMY:
            CreateController(IsServer() ? HUMAN_CONTROLLER_ENEMY : HUMAN_CONTROLLER_REMOTE);
            break;
        case HUMAN_ROLE_NEUTRAL:
        case HUMAN_ROLE_NPC_TEAM:
        case HUMAN_ROLE_NPC_TEAM_5:
            CreateController(IsServer() ? HUMAN_CONTROLLER_NEUTRAL : HUMAN_CONTROLLER_REMOTE);
            break;
        default:
            break;
        }
    }
    if (IsLocalPlayer() && IsClient()) {
        GetGame()->ShowFirstPersonView(GetGame()->IsFirstPersonView());
        GetGame()->GetFirstPersonView()->Refresh();
    }
    if (m_role != HUMAN_ROLE_PLAYER) {
        if (IsServer()) {
            m_sightId = g_sightModule.AddObserver(m_frame, SIGHT_ALL);
            m_sightTargetId = g_sightModule.AddTarget(m_frame, SIGHT_ALL);
        }
    }
    if (m_role == HUMAN_ROLE_PLAYER || IsServer()) {
        if (IsServer()) {
            m_soundListener = GetSoundModule()->AddListener(m_frame, OnSoundHeard);
            m_periodicLink = PeriodicLinks_Add(this, 0);
        }
    }
    if (m_role == HUMAN_ROLE_PLAYER) {
        const E_HumanController type = m_controller->GetType();
        if (type == HUMAN_CONTROLLER_PLAYER || type == HUMAN_CONTROLLER_REMOTE)
            m_controller->SetSpectating(false);
    }
    ResetAIMemory();
}

void C_Human::RestartMultiplayer()
{
    SetBodyDetached(false);
    std::memcpy(m_stats, m_initialStats, sizeof(m_stats));
    m_stamina = static_cast<float>(m_stats[STAT_ENDURANCE]);
    m_velocity.x = m_velocity.y = m_velocity.z = 0.0f;
    m_stealthTime = 0;
    m_enduranceTime = 0;
    m_vehicle = nullptr;
    m_seat = -1;
    m_bleeding = 0.0f;
    m_bloodDropTime = 0;
    const S_matrix &world = m_frame->GetMatrix();
    m_lastPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_unknown1ac = 0;
    m_carried = nullptr;
    OnHandsChanged();
    m_legHits = 0;
    m_inPlay = false;
    m_body.Reset();
    m_controller->Detach();
    m_events->Reset();
    delete m_controller;
    m_controller = nullptr;
    m_sightId = -1;
    m_sightTargetId = -1;
    m_soundListener = -1;
    AssignPlayerController(false);
    if (IsLocalPlayer() && IsClient()) {
        GetGame()->ShowFirstPersonView(GetGame()->IsFirstPersonView());
        GetGame()->GetFirstPersonView()->Refresh();
    }
    const E_HumanController type = m_controller->GetType();
    if (type == HUMAN_CONTROLLER_PLAYER || type == HUMAN_CONTROLLER_REMOTE)
        m_controller->SetSpectating(false);
    ResetAIMemory();
}

void C_Human::RestartMission(bool server)
{
    m_frame->SetOn(true);
    Reactivate(server);
    if (IsLocalGame())
        RestartSinglePlayer(server);
    else if (!g_gameProfile.IsCooperativeGame())
        RestartMultiplayer();
    else
        RestartCoop();
    if (server && IsClient()) {
        if (C_Item *uniform = GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem())
            PutOnUniform(uniform->GetUid());
        UpdateDisguise(100.0f);
    }
}

// The body leaves / rejoins the world: the movement stops and the model is hidden (inline
// in the original, movement byte +0xDC4).
void C_Human::SetBodyDetached(bool on)
{
    C_HumanMovement *movement = m_body.movement;
    if (movement->IsDetached() == on)
        return;
    movement->SetDetachedFlag(on);
    movement->OnFrozen(on);
    movement->GetOwner()->GetFrame()->SetOn(!on);
}

// ------------------------------------------------------------------------- saved games

void C_Human::SaveState(C_chunk &chunk)
{
    chunk.Begin(CHUNK_HUMAN_STATUS);
    chunk.Write(static_cast<int>(m_controller->GetType()));
    chunk.Write(m_stamina);
    chunk.Write(m_bleeding);
    chunk.Write(m_bloodDropTime);
    chunk.Write(m_legHits);
    // (A byte that is read back but not used.)
    chunk.Write(static_cast<uint8_t>(1));
    chunk.Write(m_disguise);
    chunk.Write(m_profileIndex);
    chunk.Write(m_periodicLink);
    chunk.Write(m_stats, sizeof(m_stats));
    chunk.Write(m_initialStats, sizeof(m_initialStats));
    chunk.Ascend();

    if (m_vehicle) {
        chunk.Begin(CHUNK_HUMAN_VEHICLE);
        chunk.Write(m_vehicle->GetNetId());
        chunk.Write(m_seat);
        chunk.Ascend();
    }
    if (m_role == HUMAN_ROLE_PLAYER) {
        chunk.Begin(CHUNK_HUMAN_PROGRESS);
        chunk.Write(m_progress);
        chunk.Write(m_stealthTime);
        chunk.Write(m_enduranceTime);
        chunk.Ascend();
    }
    chunk.Begin(CHUNK_HUMAN_ACTION);
    chunk.Write(m_actionTime);
    chunk.Write(m_actionTotal);
    chunk.Write(m_actionText);
    chunk.Ascend();
    if (m_carried) {
        chunk.Begin(CHUNK_HUMAN_CARRIED);
        chunk.Write(m_carried->GetNetId());
        chunk.Ascend();
    }
}

bool C_Human::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_HUMAN);
    C_Actor::SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_HUMAN_STATE);
    SaveState(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_HUMAN_BODY);
    m_body.SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_HUMAN_EVENTS);
    m_events->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_HUMAN_CONTROLLER);
    m_controller->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_HUMAN_FORMATION);
    if (m_formation)
        m_formation->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_HUMAN_AI_MEMORY);
    if (m_aiMemory)
        m_aiMemory->SaveGame(chunk);
    chunk.Ascend();
    if (!m_faceTexture.IsEmpty())
        chunk.WriteStringChunk(CHUNK_HUMAN_FACE, m_faceTexture);
    return true;
}

void C_Human::LoadState(C_chunk &chunk)
{
    int controllerType = HUMAN_CONTROLLER_NEUTRAL;
    int vehicleId = -1;
    int carriedId = -1;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_HUMAN_STATUS: {
            chunk.Read(controllerType);
            chunk.Read(m_stamina);
            chunk.Read(m_bleeding);
            chunk.Read(m_bloodDropTime);
            chunk.Read(m_legHits);
            uint8_t unused = 1;
            chunk.Read(unused);
            chunk.Read(m_disguise);
            chunk.Read(m_profileIndex);
            chunk.Read(m_periodicLink);
            chunk.Read(m_stats, sizeof(m_stats));
            chunk.Read(m_initialStats, sizeof(m_initialStats));
            chunk.Ascend();
            break;
        }
        case CHUNK_HUMAN_VEHICLE:
            chunk.Read(vehicleId);
            chunk.Read(m_seat);
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_CARRIED:
            chunk.Read(carriedId);
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_PROGRESS:
            chunk.Read(m_progress);
            chunk.Read(m_stealthTime);
            chunk.Read(m_enduranceTime);
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_ACTION:
            chunk.Read(m_actionTime);
            chunk.Read(m_actionTotal);
            chunk.Read(m_actionText);
            chunk.Ascend();
            break;
        default:
            // (The original leaves unknown chunks without skipping them.)
            break;
        }
    }
    if (m_controller) {
        m_controller->Detach();
        delete m_controller;
        m_controller = nullptr;
    }
    CreateController(static_cast<E_HumanController>(controllerType));
    if (vehicleId >= 0) {
        m_vehicle = g_actorsModule.GetActorById(vehicleId);
        m_vehicle->SetSeatOccupant(m_seat, this);
    }
    if (carriedId >= 0)
        m_carried = g_actorsModule.GetActorById(carriedId);
}

bool C_Human::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_HUMAN:
            C_Actor::LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_STATE:
            LoadState(chunk);
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_BODY:
            m_body.LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_CONTROLLER:
            m_controller->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_EVENTS:
            m_events->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_FORMATION:
            if (!chunk.AtEnd()) {
                if (!m_formation)
                    m_formation = new C_FormationShape();
                m_formation->LoadGame(chunk);
            }
            chunk.Ascend();
            break;
        case CHUNK_HUMAN_FACE: {
            // (C_chunk::ReadRemainingString 0x00415d00: at most 999 characters, then ascends.)
            char face[1000];
            chunk.ReadString(face, sizeof(face));
            chunk.Ascend();
            SetTexture(face);
            break;
        }
        case CHUNK_HUMAN_AI_MEMORY:
            if (!m_aiMemory)
                m_aiMemory = new C_HumanAIMemory(this);
            m_aiMemory->LoadGame(chunk);
            chunk.Ascend();
            break;
        default:
            // (The original does not leave unknown chunks: their contents are read next.)
            break;
        }
    }
    if (C_Item *uniform = GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem())
        PutOnUniform(uniform->GetUid());
    m_inventoryChanged = true;
    if (IsDead() || IsUnconscious()) {
        if (m_role == HUMAN_ROLE_ENEMY && IsDead())
            g_teamManager.GetPlayerTeam()->GetEnemyRecords().Add(this);
        if (m_sightId != -1) {
            g_sightModule.SetObserverState(m_sightId, SIGHT_OBSERVER_OFF);
            g_sightModule.SetTargetState(m_sightTargetId, SIGHT_TARGET_DEAD);
        }
        if (m_soundListener != -1) {
            GetSoundModule()->RemoveListener(m_soundListener);
            m_soundListener = -1;
        }
    }
    return true;
}

// --------------------------------------------------------------- frame and properties

bool C_Human::SetFrame(I3D_frame *frame)
{
    if (!C_Actor::SetFrame(frame))
        return false;
    if (frame->GetType() != FRAME_MODEL)
        return false;
    m_profileIndex = -1;
    m_initialized = false;
    m_vehicle = nullptr;
    m_controller = nullptr;
    m_unknown2b8 = 0;
    m_unknown2bc = 0;
    m_aliveTime = 0;
    m_deadTime = 0;
    m_unknown94 = 0.0f;
    return true;
}

void C_Human::CopyFrom(const C_Actor *source)
{
    C_Actor::CopyFrom(source);
    const C_Human *human = static_cast<const C_Human *>(source);
    m_role = human->m_role;
    m_group = human->m_group;
    m_profileIndex = human->m_profileIndex;
    m_carnage = human->m_carnage;
    m_unknown1a5 = human->m_unknown1a5;
    m_hostileToPlayers = human->m_hostileToPlayers;
    std::memcpy(m_stats, human->m_stats, sizeof(m_stats));
}

bool C_Human::WriteProperties(C_chunk &chunk)
{
    chunk.Write(PROPERTIES_VERSION);
    chunk.Write(m_role);
    chunk.Write(m_group);
    chunk.Write(m_stats, sizeof(m_stats));
    chunk.Write(m_profileIndex);
    chunk.Write(m_carnage);
    chunk.Write(m_unknown1a5);
    chunk.Write(m_hostileToPlayers);
    chunk.Write(m_nameTextId);
    return true;
}

bool C_Human::ReadProperties(C_chunk &chunk)
{
    // Every field is optional: it is read only if the chunk still holds enough bytes.
    const auto readIfThere = [&chunk](void *value, int size) {
        if (chunk.RemainingSize() >= size)
            chunk.Read(value, size);
    };
    uint8_t version = 0;
    readIfThere(&version, 1);
    if (version < PROPERTIES_MIN_VERSION)
        return false;
    readIfThere(&m_role, sizeof(m_role));
    readIfThere(&m_group, sizeof(m_group));
    readIfThere(m_stats, sizeof(m_stats));
    readIfThere(&m_profileIndex, sizeof(m_profileIndex));
    if (version > 11)
        readIfThere(&m_carnage, sizeof(m_carnage));
    if (version > 12)
        chunk.Read(m_unknown1a5);
    if (version > 13)
        chunk.Read(m_hostileToPlayers);
    if (version > 14)
        chunk.Read(m_nameTextId);
    if (m_nameTextId == 0)
        m_nameTextId = -1;
    std::memcpy(m_initialStats, m_stats, sizeof(m_stats));
    return true;
}

C_str *C_Human::GetName()
{
    if (m_role == HUMAN_ROLE_PLAYER || m_role == HUMAN_ROLE_NPC_TEAM || m_role == HUMAN_ROLE_NPC_TEAM_5) {
        m_name = GetFullName();
        return &m_name;
    }
    if (m_nameTextId != -1)
        m_name = g_texts.Get(m_nameTextId);
    else if (m_role == HUMAN_ROLE_ENEMY)
        m_name = g_texts.Get(TEXT_ENEMY);
    else if (m_role == HUMAN_ROLE_NEUTRAL)
        m_name = g_texts.Get(TEXT_CIVILIAN);
    return &m_name;
}

// ------------------------------------------------------------------------ soldier setup

void C_Human::SetProfile(int index)
{
    m_profileIndex = index;
    if (index >= 0)
        m_profile = GetSoldierProfile(index);
    if (m_profile) {
        for (int i = 0; i <= STAT_LOCKPICK; ++i)
            m_stats[i] = static_cast<int>(m_profile->stats[i]);
    }
    std::memcpy(m_initialStats, m_stats, sizeof(m_stats));
    ApplyProfileFace();
}

void C_Human::CreateEquipment(S_SoldierState *state)
{
    ClearActorContainers(this);
    if (!state)
        return;
    // On network games the state keeps the uids the server gave the new items.
    const auto create = [this](S_SoldierItem &entry, int container) -> C_Item * {
        if (entry.typeId < 0)
            return nullptr;
        C_Item *item = g_itemsModule.CreateItem(entry.typeId, container, this, entry.uid);
        if (!IsLocalGame() && entry.uid == -1 && item)
            entry.uid = static_cast<int16_t>(item->GetUid());
        return item;
    };
    create(state->head, ITEM_CONTAINER_HEAD);
    if (C_Item *uniform = create(state->uniform, ITEM_CONTAINER_UNIFORM)) {
        if (IsClient())
            PutOnUniform(uniform->GetUid());
    }
    create(state->backpack, ITEM_CONTAINER_BACKPACK);
    create(state->longGun, ITEM_CONTAINER_LONG_GUN);
    create(state->hands, ITEM_CONTAINER_HANDS);
    for (S_SoldierItem &entry : state->belt) {
        C_Item *item = g_itemsModule.CreateItem(entry.typeId, ITEM_CONTAINER_BELT, this, entry.uid);
        if (!IsLocalGame() && entry.uid == -1 && item)
            entry.uid = static_cast<int16_t>(item->GetUid());
    }
    for (S_SoldierItem &entry : state->backpackContents) {
        C_Item *item = g_itemsModule.CreateItem(entry.typeId, ITEM_CONTAINER_BACKPACK_CONTENTS, this, entry.uid);
        if (!IsLocalGame() && entry.uid == -1 && item)
            entry.uid = static_cast<int16_t>(item->GetUid());
    }
}

void C_Human::Setup(S_SoldierState &state, int role, int group, const int *stats, int playerSlot)
{
    m_role = static_cast<E_HumanRole>(role);
    m_group = group;
    m_profileIndex = state.profileIndex;
    if (m_profileIndex >= 0)
        m_profile = GetSoldierProfile(m_profileIndex);
    if (!IsLocalGame()) {
        if (m_controller)
            m_controller->SetSpectating(state.spectating);
        g_netGame.SetPlayerTeam(playerSlot, state.team, false);
        if (state.vip)
            GetGame()->SetVipSlot(playerSlot);
    }
    if (m_role == HUMAN_ROLE_PLAYER) {
        if (m_speechSet) {
            GetSpeechSets()->Release(m_speechSet);
            m_speechSet = nullptr;
        }
        int speechSet = state.speechSet;
        if (speechSet < 1 || speechSet > 4)
            speechSet = std::rand() % 4 + 1;
        m_speechSet = GetSpeechSets()->Acquire(speechSet);
    }
    if (!stats) {
        if (m_profile) {
            for (int i = 0; i <= STAT_LOCKPICK; ++i)
                m_stats[i] = static_cast<int>(m_profile->stats[i]);
        }
    } else {
        std::memcpy(m_stats, stats, sizeof(m_stats));
    }
    std::memcpy(m_initialStats, m_stats, sizeof(m_stats));
    CreateEquipment(&state);
    ApplyProfileFace();
    m_inventoryChanged = true;
}

void C_Human::ApplyProfileFace()
{
    if (m_profile)
        SetTexture(m_profile->faceTexture);
}

void C_Human::SetTexture(const char *name)
{
    m_faceTexture = name;
    GetGEffects()->SwitchFaceTexture(m_frame, m_faceTexture);
}

// Enemies: the difficulty scales their health and skills (0x005a8990).
void C_Human::ApplyDifficulty()
{
    switch (g_gameProfile.GetDifficulty()) {
    case 1:
        m_initialStats[STAT_HEALTH] = static_cast<int>(m_initialStats[STAT_HEALTH] * 0.7f);
        m_initialStats[STAT_MORALE] = static_cast<int>(m_initialStats[STAT_MORALE] * 0.9f);
        for (int i = STAT_SHOOTING; i <= STAT_LOCKPICK; ++i)
            m_initialStats[i] = static_cast<int>(m_initialStats[i] * 0.7f);
        break;
    case 2:
        m_initialStats[STAT_HEALTH] = static_cast<int>(m_initialStats[STAT_HEALTH] * 0.85f);
        m_initialStats[STAT_MORALE] = static_cast<int>(m_initialStats[STAT_MORALE] * DIFFICULTY_NORMAL_MORALE);
        for (int i = STAT_SHOOTING; i <= STAT_LOCKPICK; ++i)
            m_initialStats[i] = static_cast<int>(m_initialStats[i] * 0.85f);
        break;
    case 4:
        for (int i = STAT_ENDURANCE; i <= STAT_LOCKPICK; ++i)
            m_initialStats[i] = 100;
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------------------ mission start

void C_Human::StartMission(bool server)
{
    Activate(server);
    if (!IsLocalGame() && !server && IsClient() && g_localPlayerSlot == m_netId &&
        g_mpSettings->spawnProtection > 0)
        GetGame()->GetInterface()->ShowTimer(TEXT_SPAWN_PROTECTION, g_mpSettings->spawnProtection * 1000);

    if (!m_initialized) {
        m_faceTexture = "";
        // Humans placed outside a sector are moved into the one they stand in.
        I3D_frame *parent = m_frame->GetParent();
        if (parent && parent->GetType() != FRAME_SECTOR) {
            const S_vector pos = m_frame->GetWorldPos() + S_vector(0.0f, 0.5f, 0.0f);
            if (I3D_sector *sector = GetGame()->GetScene()->GetSector(pos)) {
                m_frame->LinkTo(reinterpret_cast<I3D_frame *>(sector), I3DLINK_KEEP_WORLD);
                m_frame->Update();
            }
        }
        m_actionText = 0;
        m_actionTotal = 0;
        m_actionTime = 0;
        m_unknown94 = 0.0f;
        if (m_role == HUMAN_ROLE_ENEMY)
            ApplyDifficulty();
        std::memcpy(m_stats, m_initialStats, sizeof(m_stats));
        if (m_role == HUMAN_ROLE_NPC_TEAM || m_role == HUMAN_ROLE_NPC_TEAM_5)
            SetProfile(m_profileIndex);
        m_velocity.x = m_velocity.y = m_velocity.z = 0.0f;
        m_stealthTime = 0;
        m_stamina = static_cast<float>(m_stats[STAT_ENDURANCE]);
        m_enduranceTime = 0;
        m_vehicle = nullptr;
        m_seat = -1;
        m_bleeding = 0.0f;
        m_bloodDropTime = 0;
        const S_matrix &world = m_frame->GetMatrix();
        m_lastPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
        m_unknown1ac = 0;
        m_fadedOut = false;
        m_deadTime = 0;
        m_inPlay = true;
        m_mpZone = 0;
        m_bodyFrame = m_frame->FindChildFrame("z", FRAME_MASK_MODEL_CHILD);
        if (!m_bodyFrame)
            m_bodyFrame = m_frame->FindChildFrame("f", FRAME_MASK_MODEL_CHILD);
        m_carried = nullptr;
        m_body.Create(this, m_group);

        // On co-op clients the server simulates the AI humans.
        const bool simulated = IsLocalGame() || IsServer() || !g_gameProfile.IsCooperativeGame();
        const S_FrameData *data = m_frame->GetData();
        if (data && data->script) {
            CreateController(HUMAN_CONTROLLER_AI);
        } else {
            switch (m_role) {
            case HUMAN_ROLE_PLAYER:
                AssignPlayerController(false);
                break;
            case HUMAN_ROLE_ENEMY:
                CreateController(simulated ? HUMAN_CONTROLLER_ENEMY : HUMAN_CONTROLLER_REMOTE);
                break;
            case HUMAN_ROLE_NEUTRAL:
            case HUMAN_ROLE_NPC_TEAM:
            case HUMAN_ROLE_NPC_TEAM_5:
                CreateController(simulated ? HUMAN_CONTROLLER_NEUTRAL : HUMAN_CONTROLLER_REMOTE);
                break;
            default:
                break;
            }
        }
        OnHandsChanged();
        m_legHits = 0;
        m_chatTime = 0;
        m_chatText[0] = '\0';
        if (m_role != HUMAN_ROLE_PLAYER) {
            if (m_stats[STAT_SPEECH_SET] == 0) {
                const C_fstr text("%s have empty speechset.. Please set it urgently..\n", m_frame->GetName());
                dbgPrintf2(text);
            }
            m_speechSet = GetSpeechSets()->Acquire(m_stats[STAT_SPEECH_SET]);
            if (!m_speechSet) {
                m_speechSet = GetSpeechSets()->Acquire(0);
                const C_fstr text("%s doesnt have speechset.. using player speech set!!!\n", m_frame->GetName());
                dbgPrintf2(text);
            }
        }
        m_initialized = true;
        m_inventoryChanged = false;
    }

    if (!server) {
        m_scale = {1.0f, 1.0f, 1.0f, m_scale.w};
        m_unknown200 = 0;
        S_HumanBody::StartMission(false);
        // A client that joins sees dead humans lying.
        if (!IsLocalGame() && m_stats[STAT_HEALTH] < 1)
            ApplyRemoteHit(HIT_SCRIPT_KILL_EX, HIT_ZONE_BODY, 0, -1, S_vector(0, 0, 0), S_vector(0, 0, 0));
    } else {
        S_HumanBody::StartMission(true);
        m_disguise = 0.0f;
        if (IsLocalGame() || g_gameProfile.IsCooperativeGame()) {
            m_sightId = g_sightModule.AddObserver(m_frame, SIGHT_ALL);
            m_sightTargetId = g_sightModule.AddTarget(m_frame, SIGHT_ALL);
            g_sightModule.SetTargetState(m_sightTargetId, SIGHT_TARGET_ALIVE);
            m_soundListener = GetSoundModule()->AddListener(m_frame, OnSoundHeard);
            m_periodicLink = PeriodicLinks_Add(this, 0);
        }
    }
    m_prevVelocity = m_velocity;
    ResetAIMemory();
}

void C_Human::StopMission(bool server)
{
    if (m_initialized) {
        GetGame()->GetCameraModule()->GetHumanEffects()->Remove(this);
        if (IsLocalGame() && (m_role == HUMAN_ROLE_PLAYER || m_role == HUMAN_ROLE_NPC_TEAM) && m_profile)
            m_profile->dead = m_stats[STAT_HEALTH] < 1;
        if (IsLocalGame() ? m_role == HUMAN_ROLE_PLAYER : IsLocalPlayer())
            g_teamManager.GetPlayerTeam()->RemoveMember(this);
        m_profile = nullptr;
        m_initialized = false;
        for (size_t i = m_pendingOrders.size(); i-- > 0;)
            ReleaseHumanOrder(m_pendingOrders[i]);
        m_pendingOrders.clear();
        m_controller->Detach();
        delete m_controller;
        m_controller = nullptr;
        m_body.Destroy();
        m_events->Reset();
        m_seenFriends.clear();
        m_seenEnemies.clear();
        m_seenBodies.clear();
        m_seenTraitors.clear();
        ClearHeardSounds();
        std::memcpy(m_stats, m_initialStats, sizeof(m_stats));
        ClearContainers();
        if (m_vehicle)
            m_vehicle->LeaveSeat(m_seat);
        m_vehicle = nullptr;
        m_seat = -1;
        SetLeader(nullptr);
        DisbandFormation();
    }
    if (!server) {
        S_HumanBody::StopMission(false);
    } else {
        S_HumanBody::StopMission(true);
        if (IsLocalGame() || g_gameProfile.IsCooperativeGame()) {
            if (m_sightId != -1) {
                g_sightModule.RemoveObserver(m_sightId);
                m_sightId = -1;
            }
            if (m_sightTargetId != -1) {
                g_sightModule.RemoveTarget(m_sightTargetId);
                m_sightTargetId = -1;
            }
            if (m_soundListener != -1) {
                GetSoundModule()->RemoveListener(m_soundListener);
                m_soundListener = -1;
            }
            if (m_periodicLink != -1) {
                PeriodicLinks_Remove(m_periodicLink);
                m_periodicLink = -1;
            }
        }
        if (m_role != HUMAN_ROLE_NPC_TEAM && m_role != HUMAN_ROLE_NPC_TEAM_5)
            m_profileIndex = -1;
    }
    Deactivate(server);
    if (m_speechSet) {
        GetSpeechSets()->Release(m_speechSet);
        m_speechSet = nullptr;
    }
}

// Multiplayer spawn (0x0059f050).
void C_Human::Respawn(const S_vector4 &pos, const S_vector4 &dir, S_SoldierState *state, const int *stats)
{
    ClearActorContainers(this);
    if (!IsLocalGame())
        SetBodyDetached(false);
    if (IsClient())
        StopMission(false);
    if (IsServer())
        StopMission(true);
    m_chatTime = 0;
    m_chatText[0] = '\0';
    m_position = pos;
    m_frame->SetPos(reinterpret_cast<const S_vector &>(m_position));
    m_direction = dir;
    m_frame->SetDir(reinterpret_cast<const S_vector &>(m_direction), 0.0f);
    // The replicated position jumps to the spawn point.
    C_NetTrack &track = g_netReplication->FindObject(m_netId, true)->GetProperty(1)->GetTrack();
    track.SetValue(pos);
    track.SetNetValue(pos);
    track.SetRate(S_vector(0.0f, 0.0f, 0.0f));
    if (state)
        Setup(*state, m_role, m_group, stats, m_netId);
    if (IsServer())
        StartMission(true);
    if (IsClient())
        StartMission(false);
    if (IsServer() && m_sightTargetId != -1) {
        g_sightModule.SetTargetState(m_sightTargetId, SIGHT_TARGET_HIDDEN);
        g_sightModule.SetTargetState(m_sightTargetId, SIGHT_TARGET_SHOWN);
    }
    if (IsLocalPlayer()) {
        g_teamManager.GetPlayerTeam()->AddMember(this);
        g_teamManager.GetPlayerTeam()->SelectMember(this);
        GetGame()->GetInterface()->SetScreen(0);
        GetGame()->GetInterface()->ShowSquadMember(nullptr);
        g_teamManager.GetPlayerTeam()->SetWatchedHuman(nullptr);
        GetCameraModule()->SetSpecialMode(2, true, nullptr);
        m_controller->SetSpectating(false);
    }
    if (IsClient() && g_localPlayerSlot == m_netId)
        g_teamManager.GetPlayerTeam()->UpdateCameraTarget();
    m_aliveTime = 0;
    m_inPlay = true;
}

void C_Human::RequestRespawn()
{
    if (m_deadTime > 3000 && !m_body.movement->IsRespawnBlocked())
        GetGameApp()->PostCommand(APP_NET_COMMAND_RESPAWN, 0, 0, false);
}

// ------------------------------------------------------------------------ update / tick

void C_Human::Update(int frameTime)
{
    if (m_actionTime != 0) {
        m_actionTime -= frameTime;
        if (m_actionTime < 0)
            m_actionTime = 0;
    }
    if (m_chatTime != 0) {
        m_chatTime -= frameTime;
        if (m_chatTime < 1) {
            m_chatTime = 0;
            m_chatText[0] = '\0';
        }
    }
    if (!IsLocalGame()) {
        m_aliveTime += frameTime;
        if (m_stats[STAT_HEALTH] < 1 && IsClient()) {
            // Deathmatch with automatic respawn: the local player asks for it after the
            // respawn delay.
            m_deadTime += frameTime;
            if (g_mpSettings->gameType == MP_GAME_DEATHMATCH && (g_mpSettings->respawnTime + 3) * 1000 < m_deadTime &&
                g_mpSettings->allowRespawn != 0 && g_localPlayerSlot == m_netId) {
                GetGameApp()->PostCommand(APP_NET_COMMAND_RESPAWN, 0, 0, false);
                m_deadTime = -4000;
            }
        } else {
            m_deadTime = 0;
        }
    }
    if (!C_Actor::IsActive())
        return;
    m_inventoryChanged = false;
    OnHandsChanged();
    UpdateBleeding(frameTime);
    m_controller->Update(frameTime);
    m_body.SetFacingDir(m_controller->GetFacingDir());
    m_body.SetLookAt(m_controller->GetLookAt());
    m_body.SetViewDir(m_controller->GetViewDir());
    m_body.Update(frameTime);
    if (!IsServer())
        UpdateVelocity(frameTime);
    m_controller->OnBodyFlags(m_body.TakeFlags());
}

void C_Human::Tick(int timeDelta)
{
    if (!C_Actor::IsActive())
        return;
    OnHandsChanged();
    TickBleeding(timeDelta);
    UpdateSeenHumans();
    ForgetOldSounds();
    if (IsLocalGame() && g_teamManager.GetPlayerTeam()->IsMember(this))
        UpdateDisguise(0.5f);
    m_events->Tick(timeDelta);
    m_controller->Tick(timeDelta);
    if (IsServer() || IsLocalGame()) {
        m_body.SetFacingDir(m_controller->GetFacingDir());
        m_body.SetLookAt(m_controller->GetLookAt());
        m_body.SetViewDir(m_controller->GetViewDir());
    }
    m_body.Tick(timeDelta);
    if (!IsClient())
        m_controller->OnBodyFlags(m_body.TakeFlags());
    if (m_stats[STAT_HEALTH] <= 0)
        return;
    UpdateVelocity(timeDelta);
    // Stepping on a mine (its trigger volume) while walking or crawling sets it off.
    if (GetVelocity().Square() >= 1e-16f && (GetStateFlags() & STATE_TREADING) != 0) {
        std::vector<S_CollisionVolume *> volumes;
        if (g_collisionWorld.GetVolumesInRadius(m_frame->GetWorldPos(), 0.5f, ~COLLISION_TYPE_MINE, volumes) &&
            (volumes.front()->type & COLLISION_TYPE_MINE) != 0 && volumes.front()->actor)
            static_cast<C_Mine *>(volumes.front()->actor)->OnSteppedOn(this, -1);
    }
    if (IsLocalGame() && m_role == HUMAN_ROLE_PLAYER)
        TrainSkills(timeDelta);
    if (m_formation)
        m_formation->Tick(timeDelta);
}

// Single player: walking trains endurance, sneaking stealth (0x005a88f0).
void C_Human::TrainSkills(int timeDelta)
{
    if (GetMoveMode() == 0)
        return;
    if ((GetStateFlags() & STATE_MARCHING) != 0 && (GetMoveMode() & MOVE_8) != 0) {
        m_enduranceTime += timeDelta;
        if (m_enduranceTime < 25001)
            return;
        m_enduranceTime -= 25000;
        AddEndurance(0.1f);
        return;
    }
    if ((GetStateFlags() & STATE_SNEAKING) != 0) {
        m_stealthTime += timeDelta;
        if (m_stealthTime > 20000) {
            m_stealthTime -= 20000;
            AddStealth(0.05f);
        }
    }
}

// Single player experience: the gains are capped and printed on the debug console.
void C_Human::AddEndurance(float amount)
{
    if (!IsLocalGame())
        return;
    m_progress.endurance += amount;
    if (m_progress.endurance > 5.0f)
        m_progress.endurance = 5.0f;
    GetGame()->Print("%s Update Endurance by %.3f to %.3f", GetShortName(), amount, m_progress.endurance);
}

void C_Human::AddUnknownProgress(float amount)
{
    if (!IsLocalGame())
        return;
    m_progress.unknown00 += amount;
    if (m_progress.unknown00 > 30.0f)
        m_progress.unknown00 = 30.0f;
}

void C_Human::AddMorale(float amount)
{
    if (!IsLocalGame())
        return;
    m_progress.morale += amount;
    if (m_progress.morale > 1.0f)
        m_progress.morale = 1.0f;
    GetGame()->Print("%s Update Morale by %.3f to %.3f", GetShortName(), amount, m_progress.morale);
}

void C_Human::AddShooting(float amount)
{
    if (!IsLocalGame())
        return;
    m_progress.shooting += amount;
    if (m_progress.shooting > 2.0f)
        m_progress.shooting = 2.0f;
    GetGame()->Print("%s Update Shooting by %.3f to %.3f", GetShortName(), amount, m_progress.shooting);
}

void C_Human::AddStealth(float amount)
{
    if (!IsLocalGame())
        return;
    m_progress.stealth += amount;
    if (m_progress.stealth > 2.0f)
        m_progress.stealth = 2.0f;
    GetGame()->Print("%s Update Stealth by %.3f to %.3f", GetShortName(), amount, m_progress.stealth);
}

void C_Human::AddFirstAid(float amount)
{
    if (!IsLocalGame())
        return;
    m_progress.firstAid += amount;
    if (m_progress.firstAid > 5.0f)
        m_progress.firstAid = 5.0f;
    GetGame()->Print("%s Update FirstAid by %.3f to %.3f", GetShortName(), amount, m_progress.firstAid);
}

void C_Human::AddLockpick(float amount)
{
    if (!IsLocalGame())
        return;
    m_progress.lockpick += amount;
    if (m_progress.lockpick > 5.0f)
        m_progress.lockpick = 5.0f;
    GetGame()->Print("%s Update Lockpick by %.3f to %.3f", GetShortName(), amount, m_progress.lockpick);
}

void C_Human::AddExperience(int amount)
{
    if (!IsLocalGame())
        return;
    m_progress.experience += amount;
    GetGame()->Print("%s Update Experience by %d to %d", GetShortName(), amount, m_progress.experience);
}
