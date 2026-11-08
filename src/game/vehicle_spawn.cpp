// The "vehicle" console commands (see the header). What the patch's assembly does, through the port's
// classes; the differences are marked "Port:".
#include "game/vehicle_spawn.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/car.h"
#include "game/collision.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/team.h"
#include "game/vehicle_params.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"

namespace {

constexpr int MACHINE_GUN_PACK_ITEM = 211;

struct S_VehicleKind {
    const char *command;
    const char *model;
    const char *nameFormat;                             // the frame's name; a saved game finds the kind by it
    const char *physics;                                // the car block as a mission keeps it, unpacked
    int shellItem;
    int shellCount;
    int machineGunPacks;                                // packs, not rounds
};

// Port: the patch builds the physics tables into the executable. They are copies of the car blocks in
// the missions' actors.bin, so files of the game: here they are read from the game's folder.
const S_VehicleKind VEHICLE_KINDS[] = {
    {"tiger", "la_tiger", "HD2PZR_%u", "tables\\vehicles\\tiger.tbl", 213, 90, 30},
    {"panther", "la_panther", "HD2V1R_%u", "tables\\vehicles\\panther.tbl", 213, 82, 21},
    {"sherman", "la_sherman", "HD2V2R_%u", "tables\\vehicles\\sherman.tbl", 214, 97, 19},
    {"panzer", "la_Panzer III", "HD2V3R_%u", "tables\\vehicles\\panzer3.tbl", 214, 121, 18},
    {"puma", "la_puma", "HD2V4R_%u", "tables\\vehicles\\puma.tbl", 214, 55, 0},
    {"isu", "la_Isu152", "HD2V5R_%u", "tables\\vehicles\\Isu152.tbl", 215, 20, 0},
};
constexpr int VEHICLE_KIND_COUNT = sizeof(VEHICLE_KINDS) / sizeof(VEHICLE_KINDS[0]);
constexpr int KIND_TIGER = 0;

constexpr float SPAWN_OFFSET_X = 8.0f;                  // from the player, along the world's x
constexpr float TRACE_START_HEIGHT = 15.0f;             // above the player
constexpr float TRACE_LENGTH = 50.0f;                   // down from there
constexpr float HEIGHT_ABOVE_GROUND = 2.0f;             // it drops and settles

constexpr int MAX_TOKEN_LENGTH = 63;

// The saved game's chunk: magic, the actors the game had, the records' count, then the records.
constexpr uint32_t SPAWN_SAVE_MAGIC = 0x32525A50;       // "PZR2": a record's top byte is its kind
constexpr uint32_t SPAWN_SAVE_MAGIC_TIGERS = 0x31525A50;   // "PZR1": all of them Tigers
constexpr int MAX_SAVED_ACTORS = 0x100000;
constexpr uint32_t MAX_SAVED_RECORDS = 1024;
constexpr uint32_t SAVED_ID_MASK = 0x00FFFFFF;
constexpr int SAVED_KIND_SHIFT = 24;

struct S_SavedVehicle {
    uint32_t idAndKind;
    S_vector pos;
};

static_assert(sizeof(S_SavedVehicle) == 16, "the patch's record");

bool IsSinglePlayer() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

void Report(const char *text)
{
    // Port: on the screen. The patch has only a file for this, vehicle_status.log, and only for the usage.
    if (C_game *game = GetGame())
        game->PrintMessage(MESSAGE_WHITE, "%s", text);
}

// The kind of a spawned vehicle, by its frame's name; -1: another actor. The frame's name can hold an
// id the actor no longer has (actors move down when another is removed): the id here does not matter.
int SpawnedKind(const C_Actor *actor)
{
    if (!actor || actor->GetType() != ACTOR_CAR || !actor->GetFrame())
        return -1;
    const char *name = actor->GetFrame()->GetName();
    if (!name)
        return -1;
    unsigned id;
    char end;
    if (std::sscanf(name, "HD2PZR_%u%c", &id, &end) == 1)
        return KIND_TIGER;
    int kind;
    if (std::sscanf(name, "HD2V%dR_%u%c", &kind, &id, &end) == 2 && kind >= 0 && kind < VEHICLE_KIND_COUNT)
        return kind;
    return -1;
}

// The seats' occupants are on: the vehicle must not go.
bool IsOccupied(C_Actor *actor)
{
    for (int seat = actor->GetSeatCount(); seat-- > 0;) {
        if (actor->GetSeatOccupant(seat))
            return true;
    }
    return false;
}

bool ReadPhysics(const char *fileName, std::vector<uint8_t> &blob)
{
    const int handle = dtaOpen(fileName, false);
    if (handle == -1)
        return false;
    blob.assign(VEHICLE_PARAMS_BLOB_SIZE, 0);
    const int got = dtaRead(handle, blob.data(), static_cast<int>(blob.size()));
    dtaClose(handle);
    return got == static_cast<int>(blob.size());
}

// Next to the player, on the ground: a ray down from above that place.
//
// Quirk kept: 8 along the world's x, wherever the player faces.
//
// Port: the ray is the level's collision: the patch asks the scene for what the ray meets of the
// player's own frame (I3D_scene::IntersectFrame at 0x104) so it comes back empty, and the vehicle stays
// at the height of the player's feet.
bool FindSpawnPosition(S_vector &pos)
{
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    if (!player || player->GetType() != ACTOR_HUMAN || !player->IsAlive() || !player->GetFrame())
        return false;
    const S_vector &feet = player->GetFrame()->GetWorldPos();
    pos = S_vector(feet.x + SPAWN_OFFSET_X, feet.y, feet.z);
    const S_vector from(pos.x, feet.y + TRACE_START_HEIGHT, pos.z);
    S_vector ground;
    if (CollisionRayTest(from, S_vector(0.0f, -TRACE_LENGTH, 0.0f), &ground, nullptr, 0, 0))
        pos.y = ground.y;
    pos.y += HEIGHT_ABOVE_GROUND;
    return true;
}

// The vehicle's store, past its limits: 90 shells and 30 packs are over its 1000 of weight, and
// the items module refuses each one past that. The patch's "ammo capacity fix" also goes around it.
void LoadAmmo(C_Actor *vehicle, int itemId, int count)
{
    if (!GetItemDef(itemId))
        return;
    C_ItemContainer *store = vehicle->GetContainer(0);
    for (int i = 0; i < count; ++i) {
        C_Item *item = g_itemsModule.CreateItem(itemId, 0, nullptr, -1);
        C_Item *replaced = nullptr;
        if (!item || store->AddItem(item, &replaced, -1) < 1)
            return;
        item->SetOwner(vehicle);
        vehicle->AddCarriedWeight(item->GetWeight());
    }
}

// The factory (0x00BBC049). `restorePos` null: from the command, next to the player, with ammo;
// else from a saved game, which has the rest. The new actor, null: failed, and all is undone.
C_Actor *SpawnVehicle(int kindIndex, const S_vector *restorePos)
{
    const S_VehicleKind &kind = VEHICLE_KINDS[kindIndex];
    if (!IsSinglePlayer())
        return nullptr;
    S_vector pos;
    if (restorePos)
        pos = *restorePos;
    else if (!FindSpawnPosition(pos))
        return nullptr;
    C_game *game = GetGame();
    I3D_scene *scene = game ? game->GetScene() : nullptr;
    if (!scene || !scene->GetPrimarySector())
        return nullptr;
    // Port: read first. The patch's are in the executable, so it has no such failure.
    std::vector<uint8_t> blob;
    if (!ReadPhysics(kind.physics, blob)) {
        Report(C_fstr("vehicle: can't read %s", kind.physics).c_str());
        return nullptr;
    }

    I3D_frame *model = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    if (!model)
        return nullptr;
    C_Actor *result = nullptr;
    if (SUCCEEDED(g_modelCache.OpenInto(static_cast<I3D_model *>(model), kind.model))) {
        model->LinkTo(scene->GetPrimarySector(), 0);
        model->SetPos(pos);
        model->Update();
        C_Actor *actor = g_actorsModule.CreateActor(ACTOR_CAR, model, true, -1);
        bool ready = false;
        if (actor) {
            // Named before SetFrame: the vehicle finds its parts behind the model's name.
            char name[64];
            std::snprintf(name, sizeof(name), kind.nameFormat, static_cast<unsigned>(actor->GetNetId()));
            model->SetName(name);
            if (actor->SetFrame(model)) {
                S_VehicleParams params{};
                ReadVehicleParams(blob.data(), params, false);
                ready = static_cast<C_Car *>(actor)->GetVehicle()->ApplyParams(params, 0);
            }
            if (ready) {
                actor->StartMission(false);
                if (!restorePos) {
                    LoadAmmo(actor, kind.shellItem, kind.shellCount);
                    LoadAmmo(actor, MACHINE_GUN_PACK_ITEM, kind.machineGunPacks);
                }
                model->SetOn(true);
                result = actor;
            } else {
                g_actorsModule.DestroyActor(actor, true);
            }
        }
        if (!result)
            model->LinkTo(nullptr, 0);
    }
    model->Release();                                   // the scene and the vehicle hold it now
    if (!result)
        Report(C_fstr("vehicle: can't make %s", kind.model).c_str());
    return result;
}

// Up to two words of up to 63 characters; spaces and tabs between. No quotes.
struct S_Words {
    char text[2][MAX_TOKEN_LENGTH + 1] = {};
    int count = 0;
    bool malformed = false;
};

S_Words SplitWords(const char *line)
{
    S_Words words;
    for (;;) {
        while (*line == ' ' || *line == '\t')
            ++line;
        if (!*line)
            return words;
        if (words.count == 2) {
            words.malformed = true;
            return words;
        }
        int length = 0;
        while (*line && *line != ' ' && *line != '\t') {
            if (length == MAX_TOKEN_LENGTH) {
                words.malformed = true;
                return words;
            }
            words.text[words.count][length++] = *line++;
        }
        ++words.count;
    }
}

void ReportUsage()
{
    Report("Usage: vehicle tiger|panther|sherman|panzer|puma|isu");
}

}  // namespace

bool ExecuteVehicleSpawnCommand(const char *line)
{
    const S_Words words = SplitWords(line);
    if (_stricmp(words.text[0], "vehicle") == 0) {
        // Quirk kept: in multiplayer too this much is taken from the commands behind; only the factory refuses.
        if (words.malformed || words.count != 2) {
            ReportUsage();
            return true;
        }
        for (int kind = 0; kind < VEHICLE_KIND_COUNT; ++kind) {
            if (_stricmp(words.text[1], VEHICLE_KINDS[kind].command) == 0) {
                SpawnVehicle(kind, nullptr);
                return true;
            }
        }
        ReportUsage();
        return true;
    }
    // Quirk kept: the patch's older command, "panzer" alone, makes a Tiger ("vehicle panzer": a Panzer III).
    if (!words.malformed && words.count == 1 && _stricmp(words.text[0], "panzer") == 0) {
        SpawnVehicle(KIND_TIGER, nullptr);
        return true;
    }
    return false;
}

void SaveSpawnedVehicles(C_chunk &chunk)
{
    if (!IsSinglePlayer())
        return;
    const int total = g_actorsModule.GetActorCount();
    std::vector<S_SavedVehicle> records;
    for (int id = C_ActorsModule::PLAYER_SLOTS; id < total; ++id) {
        C_Actor *actor = g_actorsModule.GetActor(id);
        const int kind = SpawnedKind(actor);
        if (kind >= 0)
            records.push_back({static_cast<uint32_t>(id) | static_cast<uint32_t>(kind) << SAVED_KIND_SHIFT,
                               actor->GetFrame()->GetWorldPos()});
    }
    if (records.empty() || records.size() > MAX_SAVED_RECORDS)
        return;
    chunk.Begin(CHUNK_GAME_SPAWNED_VEHICLES);
    chunk.Write(SPAWN_SAVE_MAGIC);
    chunk.Write(static_cast<uint32_t>(total));
    chunk.Write(static_cast<uint32_t>(records.size()));
    for (const S_SavedVehicle &record : records) {
        chunk.Write(record.idAndKind);
        chunk.Write(record.pos);
    }
    chunk.Ascend();
}

namespace {

// Those past `total` that are spawned and empty, from the end (the mission's actors stay: as soon
// as the last isn't one, it stops).
void TrimSpawnedVehicles(int total)
{
    for (int count = g_actorsModule.GetActorCount(); count > total && count > C_ActorsModule::PLAYER_SLOTS;
         count = g_actorsModule.GetActorCount()) {
        C_Actor *actor = g_actorsModule.GetActor(count - 1);
        if (SpawnedKind(actor) < 0 || IsOccupied(actor))
            return;
        I3D_frame *frame = actor->GetFrame();
        // (StopMission(true) takes its periodic link, a restart having given it one.)
        actor->StopMission(false);
        actor->StopMission(true);
        frame->LinkTo(nullptr, 0);
        g_actorsModule.DestroyActor(actor, true);
    }
}

bool RestoreSpawnedVehicles(C_chunk &chunk, int &total)
{
    uint32_t magic = 0, savedTotal = 0, count = 0;
    chunk.Read(magic);
    chunk.Read(savedTotal);
    if (!chunk.Read(count))
        return false;
    if ((magic != SPAWN_SAVE_MAGIC && magic != SPAWN_SAVE_MAGIC_TIGERS) || savedTotal > MAX_SAVED_ACTORS ||
        count > MAX_SAVED_RECORDS || static_cast<int>(count * sizeof(S_SavedVehicle)) != chunk.RemainingSize())
        return false;
    total = static_cast<int>(savedTotal);
    int previous = C_ActorsModule::PLAYER_SLOTS - 1;
    for (uint32_t i = 0; i < count; ++i) {
        S_SavedVehicle record;
        if (!chunk.Read(record.idAndKind) || !chunk.Read(record.pos))
            return false;
        const int id = static_cast<int>(record.idAndKind & SAVED_ID_MASK);
        const int kind = static_cast<int>(record.idAndKind >> SAVED_KIND_SHIFT);
        // Rising ids, a known kind, a real place. Never over one of the mission's actors.
        if (kind >= VEHICLE_KIND_COUNT || (magic == SPAWN_SAVE_MAGIC_TIGERS && kind != KIND_TIGER) || id <= previous ||
            id >= total || !std::isfinite(record.pos.x) || !std::isfinite(record.pos.y) || !std::isfinite(record.pos.z))
            return false;
        previous = id;
        const int existing = g_actorsModule.GetActorCount();
        if (id > existing)
            return false;
        if (id < existing) {
            // The same scene again, this one still there: what it is must agree.
            if (SpawnedKind(g_actorsModule.GetActor(id)) != kind)
                return false;
            continue;
        }
        C_Actor *actor = SpawnVehicle(kind, &record.pos);
        if (!actor || actor->GetNetId() != id)
            return false;
    }
    return true;
}

}  // namespace

void LoadSpawnedVehicles(C_chunk &chunk)
{
    if (!IsSinglePlayer())
        return;
    int total = g_actorsModule.GetActorCount();
    if (RestoreSpawnedVehicles(chunk, total))
        TrimSpawnedVehicles(total);
}

void RemoveSpawnedVehicles()
{
    if (IsSinglePlayer())
        TrimSpawnedVehicles(C_ActorsModule::PLAYER_SLOTS);
}
