// C_ActorsModule ("Actors", 0x004755d0..0x00476a50) and C_TemporaryActorsList
// ("Temp. Actors", 0x00476ec0..0x00479600).
#include "game/actors_module.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <windows.h>

#include "common/chunk.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/interface.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/physics.h"
#include "game/resource_cache.h"
#include "game/world.h"
#include "net/bitstream.h"

C_ActorsModule g_actorsModule(MODULE_ACTORS, "Actors");  // constructed by 0x00475150 (DS 0x00491290)
C_TemporaryActorsList g_temporaryActors(MODULE_TEMP_ACTORS, "Temp. Actors");

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// Unbinds the actor from its frame; frees the frame's data block once nothing else
// uses it.
void ReleaseFrameData(C_Actor *actor)
{
    I3D_frame *frame = actor->GetFrame();
    if (!frame || !frame->GetData())
        return;
    S_FrameData *data = frame->GetData();
    data->actor = nullptr;
    if (!data->actor && !data->script && !data->unknown08 && data->physicsObject == g_noPhysicsObject &&
        !data->versionMaster && !data->versionKeeper)
        FreeFrameData(frame);
}

// Makes `frame`'s data point to `actor`.
void BindFrameData(I3D_frame *frame, C_Actor *actor)
{
    if (S_FrameData *data = frame->GetData()) {
        data->actor = actor;
    } else {
        S_FrameData newData = {};
        newData.actor = actor;
        newData.physicsObject = g_noPhysicsObject;
        SetFrameData(frame, newData);
    }
}

// The squared distance from `pos` to the actor's frame.
float DistanceSquared(C_Actor *actor, const S_vector &pos)
{
    return (pos - actor->GetFrame()->GetWorldPos()).Square();
}

C_Item *GetItemOf(C_Actor *itemActor) { return static_cast<C_ItemActor *>(itemActor)->GetItem(); }
int GetMineItemId(C_Actor *mine) { return static_cast<C_Mine *>(mine)->GetItemId(); }
C_Human *GetMineOwner(C_Actor *mine) { return static_cast<C_Mine *>(mine)->GetOwner(); }

// Actor ids of player slots and temporary actors are sent with 10 bits.
constexpr int ACTOR_ID_BITS = 10;
// Width of an actor type in messages: log2(ACTOR_TYPE_COUNT) + 1.
const int ACTOR_TYPE_BITS = static_cast<int>(std::log2(static_cast<double>(ACTOR_TYPE_COUNT)) + 1.0);
// Width of a temporary-list command: log2(2) + 1.
constexpr int TEMP_COMMAND_BITS = 2;

// Temporary-list commands (first field of its messages).
enum : uint32_t {
    TEMP_ACTOR_MESSAGE = 0,         // id (10 bits), then the actor's message
    TEMP_COMMAND = 1,               // command (2 bits), then its data
};
enum : uint32_t {
    TEMP_COMMAND_CREATE_MINE = 0,
};

// Saved-game chunks of the temporary list.
enum : uint16_t {
    CHUNK_TEMP_NEXT_ID = 0x456,
    CHUNK_TEMP_ITEM = 0x457,
    CHUNK_TEMP_MINE = 0x458,
    CHUNK_TEMP_WIRE_CUT = 0x459,
    CHUNK_TEMP_DEBRIS = 0x45A,
};
// Saved-game chunk of one actor in the actors module.
constexpr uint16_t CHUNK_ACTORS_ACTOR = 0x29A;

// A dropped pick-up (ITEM_BEHAVIOUR_RESPAWNING) goes back to its place this long after the drop.
constexpr int ITEM_RESPAWN_TIME = 15000;

int GetItemBehaviour(C_Item *item) { return item->GetDef()->GetBehaviour(); }

// Model frame of item type `itemId`, or null if its model does not load.
I3D_frame *LoadItemModel(int itemId)
{
    I3D_frame *frame = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    // (The original reads the model name through a null definition for unknown ids.)
    const C_ItemDef *def = GetItemDef(itemId);
    const char *file = def ? def->GetModelName() : nullptr;
    if (!file || FAILED(g_modelCache.OpenInto(static_cast<I3D_model *>(frame), file))) {
        frame->Release();
        return nullptr;
    }
    return frame;
}

} // namespace

// =============================================================================
// C_ActorsModule
// =============================================================================

C_ActorsModule::C_ActorsModule(int id, const char *name) : C_Module(id, name)
{
    Clear();
}

C_ActorsModule::~C_ActorsModule()
{
    m_iterating = false;
    Clear();
    // (Actors still waiting in m_removed are not deleted.)
}

void C_ActorsModule::Clear()
{
    // (The original destroys front to back while DestroyActor() moves the last actor
    // into the freed slot; it ends up destroying all of them as well.)
    for (int i = static_cast<int>(m_actors.size()); i-- > 0;) {
        if (m_actors[i])
            DestroyActor(m_actors[i], true);
    }
    m_actors.clear();
    m_active.clear();
    m_pending.clear();
    for (int i = 0; i < PLAYER_SLOTS; ++i) {
        m_actors.push_back(nullptr);
        m_playerSlotUse[i] = 0xFF;
    }
}

C_Actor *C_ActorsModule::CreateActor(E_ActorType type, I3D_frame *frame, bool active, int slot)
{
    if (type < 0 || type >= ACTOR_TYPE_COUNT || !g_actorCreators[type])
        return nullptr;
    C_Actor *actor = g_actorCreators[type]->Create();
    if (slot != -1 && m_actors[slot])
        DestroyActor(m_actors[slot], true);
    if (!actor)
        return nullptr;
    if (frame)
        BindFrameData(frame, actor);
    actor->SetNetId(slot == -1 ? static_cast<int>(m_actors.size()) : slot);
    if (slot == -1) {
        m_actors.push_back(actor);
    } else {
        m_actors[slot] = actor;
        ++m_playerSlotUse[slot];
    }
    if (active) {
        // (Directly, even while the active set is being iterated.)
        actor->SetActiveIndex(static_cast<int>(m_active.size()));
        m_active.push_back(actor);
    }
    return actor;
}

void C_ActorsModule::DestroyActor(C_Actor *actor, bool immediately)
{
    if (!actor)
        return;
    if (actor->GetActiveIndex() >= 0)
        SetActorActive(actor, false);
    const int id = actor->GetNetId();
    if (id < PLAYER_SLOTS) {
        m_actors[id] = nullptr;
    } else {
        m_actors[id] = m_actors.back();
        m_actors[id]->SetNetId(id);
        m_actors.pop_back();
    }
    if (!immediately) {
        ScheduleRemoval(actor, REMOVAL_DELAY);
        return;
    }
    ReleaseFrameData(actor);
    g_actorCreators[actor->GetType()]->Destroy(actor);
}

C_Actor *C_ActorsModule::GetActor(int index) const
{
    return index >= 0 && index < static_cast<int>(m_actors.size()) ? m_actors[index] : nullptr;
}

C_Actor *C_ActorsModule::GetActorById(int id) const
{
    if (id < 0 || id >= static_cast<int>(m_actors.size()))
        return nullptr;
    if (id < PLAYER_SLOTS && !m_actors[id]) {
        for (size_t i = m_removed.size(); i-- > 0;) {
            if (m_removed[i].actor->GetNetId() == id)
                return m_removed[i].actor;
        }
    }
    return m_actors[id];
}

C_Actor *C_ActorsModule::GetHuman(int id) const
{
    if (id < 0 || id >= static_cast<int>(m_actors.size()))
        return nullptr;
    C_Actor *actor = m_actors[id];
    if (actor && actor->GetType() == ACTOR_HUMAN)
        return actor;
    for (size_t i = m_removed.size(); i-- > 0;) {
        if (m_removed[i].actor->GetNetId() == id)
            return m_removed[i].actor;
    }
    return nullptr;
}

void C_ActorsModule::SetActorActive(C_Actor *actor, bool active)
{
    if (active == (actor->GetActiveIndex() >= 0))
        return;
    if (m_iterating) {
        m_pending.push_back(actor);
        return;
    }
    if (active) {
        actor->SetActiveIndex(static_cast<int>(m_active.size()));
        m_active.push_back(actor);
    } else {
        C_Actor *last = m_active.back();
        m_active[actor->GetActiveIndex()] = last;
        last->SetActiveIndex(actor->GetActiveIndex());
        m_active.pop_back();
        actor->SetActiveIndex(-1);
    }
}

void C_ActorsModule::ApplyPendingActivations()
{
    // Each queued actor toggles.
    for (size_t i = 0; i < m_pending.size(); ++i) {
        C_Actor *actor = m_pending[i];
        SetActorActive(actor, actor->GetActiveIndex() == -1);
    }
    m_pending.clear();
}

int C_ActorsModule::FindFreePlayerSlot() const
{
    for (int i = 0; i < PLAYER_SLOTS; ++i) {
        if (!m_actors[i])
            return i;
    }
    return -1;
}

C_Actor *C_ActorsModule::FindSpawnPoint(int team) const
{
    for (C_Actor *actor : m_actors) {
        if (actor && actor->GetType() == ACTOR_SPAWN_POINT) {
            const auto *spawn = static_cast<const C_SpawnPoint *>(actor);
            if (spawn->GetTeam() == team && spawn->IsEnabled())
                return actor;
        }
    }
    return nullptr;
}

C_Actor *C_ActorsModule::FindRandomSpawnPoint(int team, int kind) const
{
    auto matches = [&](const C_Actor *actor) {
        if (!actor || actor->GetType() != ACTOR_SPAWN_POINT)
            return false;
        const auto *spawn = static_cast<const C_SpawnPoint *>(actor);
        return spawn->GetTeam() == team && spawn->IsEnabled() && (spawn->GetKind() == kind || spawn->GetKind() == 2);
    };
    int count = 0;
    for (const C_Actor *actor : m_actors)
        count += matches(actor);
    // (rand() == RAND_MAX picks one past the last match: no spawn point.)
    const int pick = static_cast<int>(std::rand() * (1.0f / 32767.0f) * count);
    int n = 0;
    for (C_Actor *actor : m_actors) {
        if (matches(actor) && n++ == pick)
            return actor;
    }
    return nullptr;
}

void C_ActorsModule::Tick(int timeDelta)
{
    if (!IsClient())
        TickRemovals(timeDelta);
    PeriodicLinks_Tick();
    HumanPass_Tick(timeDelta, m_active);
    m_iterating = true;
    const size_t count = m_active.size();
    for (size_t i = 0; i < count; ++i)
        m_active[i]->Tick(timeDelta);
    m_iterating = false;
    ApplyPendingActivations();
}

void C_ActorsModule::Update(int frameTime)
{
    if (!IsLocalGame())
        TickRemovals(frameTime);
    m_iterating = true;
    const size_t count = m_active.size();
    for (size_t i = 0; i < count; ++i)
        m_active[i]->Update(frameTime);
    m_iterating = false;
    ApplyPendingActivations();
    CountEnemies();
}

void C_ActorsModule::StartMission(bool server)
{
    m_liveEnemies = 0;
    m_carnageEnemies = 0;
    for (size_t i = 0; i < m_actors.size(); ++i) {
        if (i < PLAYER_SLOTS)
            m_playerSlotUse[i] = 0xFF;
        if (m_actors[i])
            m_actors[i]->StartMission(server);
    }
}

void C_ActorsModule::StopMission(bool server)
{
    if (!IsLocalGame())
        TickRemovals(100000);         // delete everything waiting
    for (size_t i = 0; i < m_actors.size(); ++i) {
        C_Actor *actor = m_actors[i];
        if (!actor)
            continue;
        actor->StopMission(server);
        // players' actors go with the mission
        if (i < PLAYER_SLOTS && (server || !IsServer()))
            DestroyActor(actor, true);
    }
}

void C_ActorsModule::RestartMission(bool server)
{
    m_liveEnemies = 0;
    m_carnageEnemies = 0;
    PeriodicLinks_Clear();
    for (size_t i = 0; i < m_actors.size(); ++i) {
        C_Actor *actor = m_actors[i];
        if (i < PLAYER_SLOTS && !actor)
            continue;
        actor->RestartMission(server);
        SetActorActive(actor, true);
    }
    if (server)
        CountEnemies();
}

bool C_ActorsModule::SaveGame(C_chunk &chunk)
{
    PeriodicLinks_Save(chunk);
    for (size_t i = 0; i < m_actors.size(); ++i) {
        C_Actor *actor = m_actors[i];
        if (i < PLAYER_SLOTS && !actor)
            continue;
        chunk.Begin(CHUNK_ACTORS_ACTOR);
        chunk.Write(static_cast<uint8_t>(actor->IsActive()));
        actor->SaveGame(chunk);
        chunk.Ascend();
    }
    return true;
}

bool C_ActorsModule::LoadGame(C_chunk &chunk)
{
    PeriodicLinks_Load(chunk);
    for (size_t i = 0; i < m_actors.size(); ++i) {
        C_Actor *actor = m_actors[i];
        if (i < PLAYER_SLOTS && !actor)
            continue;
        uint16_t id;
        chunk.Descend(id);              // (the id is not checked)
        uint8_t active = 1;
        if (chunk.RemainingSize() > 0)
            chunk.Read(active);
        actor->LoadGame(chunk);
        SetActorActive(actor, active != 0);
        chunk.Ascend();
    }
    CountEnemies();
    return true;
}

void C_ActorsModule::WriteState(C_BitStream *msg)
{
    for (C_Actor *actor : m_actors) {
        if (actor)
            actor->WriteState(msg);
    }
}

void C_ActorsModule::ReadState(C_BitStream *msg)
{
    for (C_Actor *actor : m_actors) {
        if (actor)
            actor->ReadState(msg);
    }
}

bool C_ActorsModule::OnMessage(C_BitStream *msg)
{
    const int index = static_cast<int>(msg->ReadBits(ACTOR_ID_BITS));
    if (index >= PLAYER_SLOTS)
        m_actors[index]->OnMessage(msg);
    else
        C_Human::HandleMessage(static_cast<C_Human *>(m_actors[index]), msg);
    return true;
}

bool C_ActorsModule::Unknown28(C_BitStream *msg)
{
    const int index = static_cast<int>(msg->ReadBits(ACTOR_ID_BITS));
    if (index >= PLAYER_SLOTS)
        m_actors[index]->Unknown20(msg);
    else
        C_Human::HandleMessage2(static_cast<C_Human *>(m_actors[index]), msg);
    return true;
}

void C_ActorsModule::ScheduleRemoval(C_Actor *actor, int delay)
{
    m_removed.push_back({actor, delay});
    // (Only humans are removed with a delay.)
    static_cast<C_Human *>(actor)->DetachFromWorld();
}

void C_ActorsModule::TickRemovals(int timeDelta)
{
    for (size_t i = 0; i < m_removed.size(); ++i) {
        m_removed[i].timeLeft -= timeDelta;
        if (m_removed[i].timeLeft >= 0)
            continue;
        C_Actor *actor = m_removed[i].actor;
        if (IsServer()) {
            S_NetPlayerState *players = g_netGame.LockPlayers();
            players[actor->GetNetId()].left = false;
            g_netGame.UnlockPlayers();
        }
        if (IsClient())
            actor->StopMission(false);
        if (IsServer())
            actor->StopMission(true);
        if (IsClient())
            GetGame()->GetInterface()->OnActorEvent(INTERFACE_ACTOR_DELETED, actor);
        g_actorCreators[actor->GetType()]->Destroy(actor);
        // The last entry takes this place and is checked next (its time is
        // decremented a second time this tick, as in the original).
        if (m_removed.size() > 1) {
            m_removed[i] = m_removed.back();
            --i;
        }
        m_removed.pop_back();
    }
}

void C_ActorsModule::CountEnemies()
{
    if (!IsLocalGame() && !g_gameProfile.IsCooperativeGame())
        return;
    m_liveEnemies = 0;
    m_carnageEnemies = 0;
    m_carnageEnemy = nullptr;
    for (C_Actor *actor : m_actors) {
        if (!actor || actor->GetType() != ACTOR_HUMAN)
            continue;
        auto *human = static_cast<C_Human *>(actor);
        if (human->GetRole() != HUMAN_ROLE_ENEMY || human->GetHealth() <= 0 || !human->GetController() ||
            human->IsUnconscious())
            continue;
        if (!human->IsAISuspended())
            ++m_liveEnemies;
        if (human->IsCarnageEnemy()) {
            m_carnageEnemy = human;
            ++m_carnageEnemies;
        }
    }
    if (m_carnageEnemies != 1)
        m_carnageEnemy = nullptr;
}

// =============================================================================
// C_TemporaryActorsList
// =============================================================================

C_TemporaryActorsList::C_TemporaryActorsList(int id, const char *name) : C_Module(id, name)
{
    Clear();
}

C_TemporaryActorsList::~C_TemporaryActorsList()
{
    Clear();
}

C_Actor *C_TemporaryActorsList::CreateActor(E_ActorType type, I3D_frame *frame, int id)
{
    C_Actor *actor = g_actorCreators[type]->Create();
    if (!actor)
        return nullptr;
    actor->SetFrame(frame);
    actor->SetTemporary(true);
    BindFrameData(actor->GetFrame(), actor);
    if (id < 0)
        id = m_nextId++;
    actor->SetNetId(id);
    m_actors.push_back(actor);
    if (IsServer())
        actor->StartMission(true);
    if (IsClient())
        actor->StartMission(false);
    return actor;
}

void C_TemporaryActorsList::DestroyActor(C_Actor *actor)
{
    if (m_iterating) {
        m_toDestroy.push_back(actor);
        return;
    }
    if (actor->GetActiveIndex() >= 0)
        SetActorActive(actor, false);
    ReleaseFrameData(actor);
    if (IsClient())
        actor->StopMission(false);
    if (IsServer())
        actor->StopMission(true);
    for (size_t i = 0; i < m_actors.size(); ++i) {
        if (m_actors[i] == actor) {
            m_actors[i] = m_actors.back();
            m_actors.pop_back();
        }
    }
    if (IsClient())
        GetGame()->GetInterface()->OnActorEvent(INTERFACE_ACTOR_DELETED, actor);
    g_actorCreators[actor->GetType()]->Destroy(actor);
}

void C_TemporaryActorsList::DestroyMinesOf(C_Human *owner)   // 0x00479700
{
    // (DestroyActor moves the last actor into the freed place, which the loop then skips.)
    for (size_t i = 0; i < m_actors.size(); ++i) {
        C_Actor *actor = m_actors[i];
        if (actor->GetType() == ACTOR_MINE && static_cast<C_Mine *>(actor)->GetOwner() == owner)
            DestroyActor(actor);
    }
}

C_Actor *C_TemporaryActorsList::FindActor(int id) const
{
    for (size_t i = m_actors.size(); i-- > 0;) {
        if (m_actors[i]->GetNetId() == id)
            return m_actors[i];
    }
    return nullptr;
}

void C_TemporaryActorsList::SetActorActive(C_Actor *actor, bool active)
{
    if (active == (actor->GetActiveIndex() >= 0))
        return;
    if (m_iterating) {
        m_pending.push_back(actor);
        return;
    }
    if (active) {
        actor->SetActiveIndex(static_cast<int>(m_active.size()));
        m_active.push_back(actor);
    } else {
        if (m_active.size() > 1) {
            C_Actor *last = m_active.back();
            m_active[actor->GetActiveIndex()] = last;
            last->SetActiveIndex(actor->GetActiveIndex());
        }
        m_active.pop_back();
        actor->SetActiveIndex(-1);
    }
}

void C_TemporaryActorsList::Clear()
{
    for (size_t i = m_actors.size(); i-- > 0;)
        DestroyActor(m_actors[i]);
    m_actors.clear();
    m_active.clear();
    m_pending.clear();
}

void C_TemporaryActorsList::ApplyPendingChanges()
{
    for (size_t i = m_pending.size(); i-- > 0;)
        SetActorActive(m_pending[i], m_pending[i]->GetActiveIndex() == -1);
    m_pending.clear();
    for (size_t i = m_toDestroy.size(); i-- > 0;)
        DestroyActor(m_toDestroy[i]);
    m_toDestroy.clear();
}

void C_TemporaryActorsList::Tick(int timeDelta)
{
    m_iterating = true;
    for (size_t i = m_active.size(); i-- > 0;)
        m_active[i]->Tick(timeDelta);
    m_iterating = false;
    ApplyPendingChanges();
}

void C_TemporaryActorsList::Update(int frameTime)
{
    m_iterating = true;
    for (size_t i = m_active.size(); i-- > 0;)
        m_active[i]->Update(frameTime);
    m_iterating = false;
    ApplyPendingChanges();
}

void C_TemporaryActorsList::StartMission(bool server)
{
    if (server || !IsServer())
        m_nextId = 0;
    m_debrisCount = 0;
    if (server)
        CreateBreakables();
    else
        CreateBreakablesOnClient();
    if (!IsServer() || !server)
        return;

    // The server turns the mission's items into item actors; armed mines are laid
    // on the ground below their placement.
    for (int i = 0; i < g_itemsModule.GetPlacementCount(); ++i) {
        const S_ItemPlacement &placement = g_itemsModule.GetPlacement(i);
        C_Item *item = placement.item;
        const S_vector pos(placement.pos.x, placement.pos.y, placement.pos.z);
        if (item->IsArmed() && GetItemBehaviour(item) == ITEM_BEHAVIOUR_MINE) {
            const S_vector from = pos + S_vector(0.0f, 0.5f, 0.0f);
            S_vector hitPos, hitNormal;
            if (g_collisionWorld.RayTest(from, S_vector(0.0f, -2.0f, 0.0f), &hitPos, &hitNormal, 0, 0))
                CreateMine(nullptr, item->GetTypeId(), hitPos, hitNormal, false);
            else
                CreateMine(nullptr, item->GetTypeId(), pos, S_vector(0.0f, 1.0f, 0.0f), false);
            g_itemsModule.DestroyItem(item);
            continue;
        }
        I3D_frame *frame = item->GetFrame(0);
        frame->SetPosInline(pos);
        frame->SetRot(placement.rot);
        auto *actor = static_cast<C_ItemActor *>(g_temporaryActors.CreateActor(ACTOR_ITEM, frame, -1));
        actor->SetupItem(item, S_vector(0.0f, 0.0f, 0.0f));
        if (!IsLocalGame() && GetItemBehaviour(item) == ITEM_BEHAVIOUR_RESPAWNING)
            m_respawnItems.push_back(placement);
    }
    g_itemsModule.FreePlacements();
}

void C_TemporaryActorsList::StopMission(bool server)
{
    if (!server && IsServer())
        return;
    for (size_t i = m_actors.size(); i-- > 0;)
        DestroyActor(m_actors[i]);
    m_breakables.clear();
    m_respawnItems.clear();
}

void C_TemporaryActorsList::RestartMission(bool server)
{
    for (size_t i = m_actors.size(); i-- > 0;)
        DestroyActor(m_actors[i]);
    m_nextId = 0;
    m_debrisCount = 0;
    if (!server) {
        if (IsServer())
            CreateBreakables();
        if (IsClient())
            CreateBreakablesOnClient();
    } else {
        if (IsServer())
            StartMission(true);
        if (IsClient())
            StartMission(false);
    }
}

bool C_TemporaryActorsList::SaveGame(C_chunk &chunk)
{
    chunk.WriteChunk(CHUNK_TEMP_NEXT_ID, m_nextId);
    for (C_Actor *actor : m_actors) {
        const uint8_t active = actor->IsActive();
        switch (actor->GetType()) {
        case ACTOR_MINE:
            chunk.Begin(CHUNK_TEMP_MINE);
            chunk.Write(active);
            chunk.Write(actor->GetNetId());
            chunk.Write(GetMineItemId(actor));
            actor->SaveGame(chunk);
            chunk.Ascend();
            break;
        case ACTOR_ITEM:
            chunk.Begin(CHUNK_TEMP_ITEM);
            chunk.Write(active);
            // (The actor id goes into the item record's container number.)
            g_itemsModule.SaveItem(GetItemOf(actor), actor->GetNetId(), 0, 0, chunk);
            actor->SaveGame(chunk);
            chunk.Ascend();
            break;
        case ACTOR_DEBRIS:
            chunk.Begin(CHUNK_TEMP_DEBRIS);         // (debris is not restored)
            chunk.Ascend();
            break;
        case ACTOR_WIRE_CUT:
            chunk.Begin(CHUNK_TEMP_WIRE_CUT);
            chunk.Write(active);
            chunk.Write(actor->GetNetId());
            actor->SaveGame(chunk);
            chunk.Ascend();
            break;
        default:
            break;
        }
    }
    return true;
}

bool C_TemporaryActorsList::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        // (The original keeps the actor of the previous chunk here, so a following
        // next-id or debris chunk activates it again.)
        C_Actor *actor = nullptr;
        uint8_t active = 1;
        switch (id) {
        case CHUNK_TEMP_NEXT_ID:
            chunk.Read(m_nextId);
            chunk.Ascend();
            break;
        case CHUNK_TEMP_ITEM: {
            if (chunk.RemainingSize() > 0)
                chunk.Read(active);
            int actorId = -1, slot = 0, index = 0;
            C_Item *item = g_itemsModule.LoadItem(&actorId, &slot, &index, chunk, nullptr);
            I3D_frame *frame = item->GetFrame(0);
            actor = CreateActor(ACTOR_ITEM, frame, actorId);          // 0x004793f0
            static_cast<C_ItemActor *>(actor)->SetupItem(item, S_vector(0.0f, 0.0f, 0.0f));
            actor->LoadGame(chunk);
            chunk.Ascend();
            break;
        }
        case CHUNK_TEMP_MINE: {
            if (chunk.RemainingSize() > 0)
                chunk.Read(active);
            int actorId = 0, itemId = 0;
            chunk.Read(actorId);
            chunk.Read(itemId);
            actor = LoadMine(actorId, itemId);
            actor->LoadGame(chunk);
            chunk.Ascend();
            break;
        }
        case CHUNK_TEMP_WIRE_CUT: {
            if (chunk.RemainingSize() > 0)
                chunk.Read(active);
            int actorId = 0;
            chunk.Read(actorId);
            actor = FindActor(actorId);
            if (actor)
                actor->LoadGame(chunk);
            chunk.Ascend();
            break;
        }
        default:
            chunk.Ascend();
            break;
        }
        if (actor)
            SetActorActive(actor, active != 0);
    }
    return true;
}

void C_TemporaryActorsList::WriteState(C_BitStream *stream)
{
    auto *msg = static_cast<C_NetMessage *>(stream);
    msg->WriteBits(static_cast<uint32_t>(m_nextId), 16);
    for (C_Actor *actor : m_actors) {
        msg->WriteBits(actor->GetType(), ACTOR_TYPE_BITS);
        msg->WriteBits(static_cast<uint32_t>(actor->GetNetId()), 16);
        switch (actor->GetType()) {
        case ACTOR_MINE: {
            // (The original reads the owner's id without checking for an owner.)
            C_Human *owner = GetMineOwner(actor);
            msg->WriteBits(static_cast<uint32_t>(owner ? owner->GetNetId() : -1), 16);
            msg->WriteBits(static_cast<uint32_t>(GetMineItemId(actor)), 16);
            msg->WritePosition(actor->GetPosition());
            msg->WriteDirection3D(actor->GetDirection(), -1);
            actor->WriteState(msg);
            break;
        }
        case ACTOR_ITEM: {
            C_Item *item = GetItemOf(actor);
            msg->WriteBits(static_cast<uint32_t>(item->GetTypeId()), 16);
            msg->WriteBits(static_cast<uint32_t>(item->GetUid()), 16);
            msg->WritePosition(actor->GetPosition());
            msg->WriteDirection3D(actor->GetDirection(), -1);
            actor->WriteState(msg);
            break;
        }
        case ACTOR_WIRE_CUT:
            actor->WriteState(msg);
            break;
        default:
            break;
        }
    }
    msg->WriteBits(0, ACTOR_TYPE_BITS);
}

void C_TemporaryActorsList::ReadState(C_BitStream *stream)
{
    auto *msg = static_cast<C_NetMessage *>(stream);
    m_nextId = static_cast<int16_t>(msg->ReadBits(16));
    for (uint32_t type = msg->ReadBits(ACTOR_TYPE_BITS); type != 0; type = msg->ReadBits(ACTOR_TYPE_BITS)) {
        const int id = static_cast<int16_t>(msg->ReadBits(16));
        C_Actor *actor = nullptr;
        if (type == ACTOR_MINE || type == ACTOR_ITEM) {
            const int a = static_cast<int16_t>(msg->ReadBits(16));
            const int b = static_cast<int16_t>(msg->ReadBits(16));
            S_vector pos, dir;
            msg->ReadPosition(pos);
            msg->ReadDirection3D(dir, -1);
            if (type == ACTOR_MINE)
                actor = ClientCreateMine(id, a, b, pos, dir);
            else
                actor = ClientCreateItem(id, -1, a, b, pos, S_vector(0.0f, 0.0f, 0.0f), &dir);
        } else if (type == ACTOR_WIRE_CUT) {
            actor = FindActor(id);
        } else {
            continue;
        }
        // (The original does not check for a failed creation.)
        if (actor)
            actor->ReadState(msg);
    }
}

bool C_TemporaryActorsList::OnMessage(C_BitStream *stream)
{
    auto *msg = static_cast<C_NetMessage *>(stream);
    const uint32_t command = msg->ReadBits(TEMP_COMMAND_BITS);
    if (command == TEMP_ACTOR_MESSAGE) {
        if (C_Actor *actor = FindActor(static_cast<int>(msg->ReadBits(ACTOR_ID_BITS))))
            actor->OnMessage(msg);
    } else if (command == TEMP_COMMAND && msg->ReadBits(TEMP_COMMAND_BITS) == TEMP_COMMAND_CREATE_MINE) {
        const int id = static_cast<int16_t>(msg->ReadBits(16));
        const int ownerId = static_cast<int16_t>(msg->ReadBits(16));
        const int itemId = static_cast<int16_t>(msg->ReadBits(16));
        S_vector pos, dir;
        msg->ReadPosition(pos);
        msg->ReadDirection3D(dir, -1);
        ClientCreateMine(id, ownerId, itemId, pos, dir);
    }
    return true;
}

bool C_TemporaryActorsList::Unknown28(C_BitStream *msg)
{
    C_Actor *actor = FindActor(static_cast<int>(msg->ReadBits(ACTOR_ID_BITS)));
    if (!actor)
        return false;
    actor->Unknown20(msg);
    return true;
}

C_Debris *C_TemporaryActorsList::CreateDebris(I3D_frame *frame, const S_vector &pos, const S_vector &dir)
{
    const uint32_t visualType = frame->GetVisualType();
    if (!visualType)
        return nullptr;
    I3D_frame *copy = I3DGetDriver()->CreateVisual(visualType);
    copy->Duplicate(frame);
    copy->SetPos(pos);
    copy->SetDir(dir, 0.0f);
    copy->Update();
    C_Actor *actor = CreateActor(ACTOR_DEBRIS, copy, 1000000 + m_debrisCount++);
    copy->Release();
    return static_cast<C_Debris *>(actor);
}

void C_TemporaryActorsList::CreateMine(C_Human *owner, int itemId, const S_vector &pos, const S_vector &dir, bool send)
{
    I3D_frame *frame = LoadItemModel(itemId);
    if (!frame)
        return;
    frame->SetPosInline(pos);
    frame->SetWorldDir(dir, 0.0f);
    frame->Update();
    C_Actor *mine = CreateActor(ACTOR_MINE, frame, -1);
    static_cast<C_Mine *>(mine)->Setup(owner, itemId, -1);
    frame->Release();
    if (owner) {
        C_Item *item = owner->FindItem(itemId, nullptr);
        if (!item)
            return;
        owner->RemoveItem(item->GetUid(), true);
    }
    if (!IsLocalGame() && send)
        SendCreateMine(mine->GetNetId(), owner ? owner->GetNetId() : -1, itemId, pos, dir);
}

void C_TemporaryActorsList::SendCreateMine(int id, int ownerId, int itemId, const S_vector &pos, const S_vector &dir)
{
    auto *msg = static_cast<C_NetMessage *>(CreateTempActorCommand(TEMP_COMMAND_CREATE_MINE, true));
    msg->WriteBits(static_cast<uint32_t>(id), 16);
    msg->WriteBits(static_cast<uint32_t>(ownerId), 16);
    msg->WriteBits(static_cast<uint32_t>(itemId), 16);
    msg->WritePosition(pos);
    msg->WriteDirection3D(dir, -1);
    SendTempActorMessage(msg);
}

C_Actor *C_TemporaryActorsList::ClientCreateMine(int id, int ownerId, int itemId, const S_vector &pos,
                                                 const S_vector &dir)
{
    if (IsServer())
        return FindActor(id);
    I3D_frame *frame = LoadItemModel(itemId);
    if (!frame)
        return nullptr;
    frame->SetPosInline(pos);
    frame->SetWorldDir(dir, 0.0f);
    frame->Update();
    auto *owner = ownerId >= 0 ? static_cast<C_Human *>(g_actorsModule.GetHuman(ownerId)) : nullptr;
    C_Actor *mine = CreateActor(ACTOR_MINE, frame, id);
    static_cast<C_Mine *>(mine)->Setup(owner, itemId, -1);
    frame->Release();
    if (owner) {
        if (C_Item *item = owner->FindItem(itemId, nullptr))
            owner->RemoveItem(item->GetUid(), true);
    }
    return mine;
}

C_Actor *C_TemporaryActorsList::LoadMine(int id, int itemId)
{
    I3D_frame *frame = LoadItemModel(itemId);
    if (!frame)
        return nullptr;
    C_Actor *mine = CreateActor(ACTOR_MINE, frame, id);
    static_cast<C_Mine *>(mine)->Setup(nullptr, itemId, -1);
    frame->Release();
    return mine;
}

int C_TemporaryActorsList::DropItem(C_Human *human, int itemUid, const S_vector &pos, const S_vector &dir,
                                    bool /*unknown*/)
{
    C_Item *item = human->FindItemByUid(itemUid);
    if (!item)
        return -1;
    I3D_frame *frame = item->GetFrame(0);
    human->RemoveItem(itemUid, false);
    frame->SetPosDir(pos, dir.Normalized(), 0.0f);
    frame->Update();
    auto *actor = static_cast<C_ItemActor *>(g_temporaryActors.CreateActor(ACTOR_ITEM, frame, -1));
    actor->SetupItem(item, dir);
    if (!IsLocalGame() && GetItemBehaviour(item) == ITEM_BEHAVIOUR_RESPAWNING)
        actor->SetRespawnTime(ITEM_RESPAWN_TIME);
    return actor->GetNetId();
}

C_Actor *C_TemporaryActorsList::ClientCreateItem(int id, int ownerId, int itemType, int itemUid, const S_vector &pos,
                                                 const S_vector &dir, const S_vector *exactDir)
{
    if (IsServer())
        return FindActor(id);
    char text[256];
    std::snprintf(text, sizeof(text),
                  "C_TemporaryActorsList::ClCreateItem: _TempActorIndex %d ActorIndex %d _ItemUID %d \n", id,
                  ownerId, itemUid);
    OutputDebugStringA(text);

    C_Item *item;
    if (ownerId == -1) {
        // an item nobody had: a new one
        item = g_itemsModule.CreateItem(itemType, 0, nullptr, itemUid);
        item->SetInWorld(true);
    } else {
        auto *owner = static_cast<C_Human *>(g_actorsModule.GetHuman(ownerId));
        item = owner ? owner->FindItemByUid(itemUid) : nullptr;
        if (!item) {
            std::snprintf(text, sizeof(text),
                          owner ? "C_TemporaryActorsList::ClCreateItem - wrong params (ITEM) for clcreateitem %d, %d"
                                : "C_TemporaryActorsList::ClCreateItem - wrong params (HUMAN) for clcreateitem %d, %d",
                          ownerId, itemUid);
            OutputDebugStringA(text);
            return nullptr;
        }
        owner->RemoveItem(itemUid, false);
    }
    I3D_frame *frame = item->GetFrame(0);
    if (!frame)
        return nullptr;
    frame->SetPosDir(pos, exactDir ? *exactDir : dir.Normalized(), 0.0f);
    frame->Update();
    auto *actor = static_cast<C_ItemActor *>(g_temporaryActors.CreateActor(ACTOR_ITEM, frame, id));
    actor->SetupItem(item, dir);
    std::snprintf(text, sizeof(text),
                  "C_TemporaryActorsList::ClCreateItem: CREATED _TempActorIndex %d ActorIndex %d _ItemUID %d \n",
                  id, ownerId, itemUid);
    OutputDebugStringA(text);
    return actor;
}

bool C_TemporaryActorsList::IsItemInRange(int itemId, const S_vector &pos, int range) const
{
    const float range2 = static_cast<float>(range * range);
    for (size_t i = m_actors.size(); i-- > 0;) {
        C_Actor *actor = m_actors[i];
        if (actor->GetType() == ACTOR_ITEM && GetItemOf(actor) && GetItemOf(actor)->GetTypeId() == itemId &&
            DistanceSquared(actor, pos) < range2)
            return true;
    }
    return false;
}

bool C_TemporaryActorsList::IsMineInRange(const S_vector &pos, int range) const
{
    const float range2 = static_cast<float>(range * range);
    for (size_t i = m_actors.size(); i-- > 0;) {
        C_Actor *actor = m_actors[i];
        if (actor->GetType() == ACTOR_MINE && DistanceSquared(actor, pos) < range2)
            return true;
    }
    return false;
}

C_Actor *C_TemporaryActorsList::FindItemNear(const S_vector &pos, float range) const
{
    for (C_Actor *actor : m_actors) {
        if (actor->GetType() != ACTOR_ITEM)
            continue;
        const int kind = GetItemBehaviour(GetItemOf(actor));
        if ((kind == 3 || kind == 4 || kind == 5 || kind == 6 || kind == 12) &&
            DistanceSquared(actor, pos) <= range * range)
            return actor;
    }
    return nullptr;
}

void C_TemporaryActorsList::AddBreakable(const S_BreakableObject &object)
{
    m_breakables.push_back(object);
}

void C_TemporaryActorsList::ClearBreakables()
{
    m_breakables.clear();
}

void C_TemporaryActorsList::CreateBreakables()
{
    for (const S_BreakableObject &b : m_breakables) {
        auto *fence = static_cast<C_WireCut *>(CreateActor(ACTOR_WIRE_CUT, b.frame, -1));
        fence->SetDoor(b.door);
        b.volume->actor = fence;
        g_collisionWorld.MoveVolume(b.volume);
    }
}

void C_TemporaryActorsList::CreateBreakablesOnClient()
{
    if (!IsServer())
        CreateBreakables();
}

// ---- messages ---------------------------------------------------------------------

C_BitStream *CreateActorMessage(int id, bool reliable)
{
    C_BitStream *msg = g_netGame.CreateMessage(MODULE_ACTORS, reliable ? NET_DELIVERY_RELIABLE : NET_DELIVERY_3);
    msg->WriteBits(static_cast<uint32_t>(id), ACTOR_ID_BITS);
    return msg;
}

C_BitStream *CreateActorMessageEx(int id, bool reliable)
{
    C_BitStream *msg = g_netGame.CreateMessageEx(MODULE_ACTORS, reliable ? NET_DELIVERY_RELIABLE : NET_DELIVERY_3);
    msg->WriteBits(static_cast<uint32_t>(id), ACTOR_ID_BITS);
    return msg;
}

void SendActorMessage(C_BitStream *msg)
{
    g_netGame.SendNetMessage(msg);
}

C_BitStream *CreateTempActorMessage(int id, bool reliable)
{
    C_BitStream *msg = g_netGame.CreateMessage(MODULE_TEMP_ACTORS, reliable ? NET_DELIVERY_RELIABLE : NET_DELIVERY_3);
    msg->WriteBits(TEMP_ACTOR_MESSAGE, TEMP_COMMAND_BITS);
    msg->WriteBits(static_cast<uint32_t>(id), ACTOR_ID_BITS);
    return msg;
}

C_BitStream *CreateTempActorCommand(int command, bool reliable)
{
    C_BitStream *msg = g_netGame.CreateMessage(MODULE_TEMP_ACTORS, reliable ? NET_DELIVERY_RELIABLE : NET_DELIVERY_3);
    msg->WriteBits(TEMP_COMMAND, TEMP_COMMAND_BITS);
    msg->WriteBits(static_cast<uint32_t>(command), TEMP_COMMAND_BITS);
    return msg;
}

C_BitStream *CreateTempActorMessage2(int id, bool reliable)
{
    C_BitStream *msg = g_netGame.CreateMessageEx(MODULE_TEMP_ACTORS, reliable ? NET_DELIVERY_RELIABLE : NET_DELIVERY_3);
    msg->WriteBits(static_cast<uint32_t>(id), ACTOR_ID_BITS);
    return msg;
}

void SendTempActorMessage(C_BitStream *msg)
{
    g_netGame.SendNetMessage(msg);
}
