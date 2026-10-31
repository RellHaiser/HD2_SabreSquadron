// Harness stub of game/human.h (the real one is too wide): what the car's seats call on their
// occupants (game/car_seats.cpp), under the real names; the harness sets and reads the fields.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "game/items.h"                                 // C_Item (part 7b)
#include "game/soldier.h"                               // S_SoldierState, STAT_COUNT (part 7b)

// C_Human::GetEnvironmentFlags() bits, roles.
enum : uint32_t {
    HUMAN_ENV_DEADLY = 0x2000,
    HUMAN_ENV_RESTRICTED = 0x4000,
};
enum E_HumanRole : int {
    HUMAN_ROLE_PLAYER = 1,
    HUMAN_ROLE_ENEMY = 2,
    HUMAN_ROLE_NEUTRAL = 3,
    HUMAN_ROLE_NPC_TEAM = 4,
    HUMAN_ROLE_NPC_TEAM_5 = 5,
};

// Teams as C_Human::GetTeam() returns them (the multiplayer zone, part 7d).
enum E_HumanTeam {
    TEAM_AXIS = 0,
    TEAM_ALLIES = 1,
    TEAM_NONE = 2,
};

enum : int {
    HUMAN_CONTROLLER_NONE = 0,
    HUMAN_CONTROLLER_PLAYER = 1,
    HUMAN_CONTROLLER_SQUAD = 3,
    HUMAN_CONTROLLER_ENEMY = 4,
    HUMAN_CONTROLLER_REMOTE = 7,
};

// The order the humans were hit in (the car's callbacks).
inline std::vector<const C_Actor *> g_hitOrder;

class C_HumanMovement;                                  // (game/human_movement.h)
class C_HumanController;                                // (game/human_controller.h)

class C_Human : public C_Actor {
public:
    C_Human() : C_Actor(ACTOR_HUMAN) {}
    void CopyFrom(const C_Actor *) override {}
    void StartMission(bool server) override { startMissions.push_back(server); }
    void StopMission(bool) override {}
    void RestartMission(bool) override {}

    bool IsLocalPlayer() const { return localPlayer; }
    int GetControllerType() const { return controllerType; }
    void SetViewDir(const S_vector4 &dir)
    {
        viewDir = dir;
        ++viewDirs;
    }
    void SetFacingDir(const S_vector4 &dir)
    {
        facingDir = dir;
        ++facingDirs;
    }
    int GetPeriodicLink() const { return periodicLink; }
    bool IsDead() const { return health < 1; }
    bool IsAlive() const { return health > 0; }
    // (The multiplayer zone, part 7d.)
    int GetHealth() const { return health; }
    void SetMpZone(int zoneId) { mpZone = zoneId; }
    // The sight module's calls (part 8b; the world test defines them).
    bool IsFriendOf(const C_Actor *other) const;
    void OnSightEvent(int lost, int targetId);
    void TestSightOfHuman(C_Human *target, int *status, float *visibility);
    void TestSightOfMeleeTracker(C_Actor *meleeTracker, int *status, float *visibility);
    void RemoveCollision(bool unknown);
    void RestoreCollision(bool unknown);
    // The car's hits and crew queries (game/car_hits.cpp).
    bool IsHostileTo(const C_Actor *other) const
    {
        for (const C_Actor *a : hostileTo)
            if (a == other)
                return true;
        return false;
    }
    void AddExperience(int amount) { experience += amount; }
    I3D_frame *GetBodyFrame() const { return bodyFrame; }
    // (The effects module's rain, part 8c.)
    I3D_sector *GetBodySector() const { return bodySector; }
    I3D_sector *bodySector = nullptr;
    // (The decorators' bends, part 8d-iii.)
    const S_vector &GetVelocity() const { return velocity; }
    int GetStance() const { return stance; }
    S_vector velocity = {0.0f, 0.0f, 0.0f};
    int stance = 0;
    // The vehicle it sits in (the animation actor's wings, game/animation_actor.cpp).
    C_Actor *GetVehicle() const { return vehicle; }
    int GetVehicleSeat() const { return vehicleSeat; }  // (the camera module, part 8e-iii)
    // The camera module's effect value (part 8e-i; the camera test defines it).
    void SetUnknown94(float value);
    const S_vector &GetRandomBonePos() const { return bonePos; }
    void RemoveItem(int uid, bool destroy) { removedItems.push_back({uid, destroy}); }
    bool IsInPlay() const { return inPlay; }
    bool IsSpectating() const { return spectating; }
    int GetTeam(bool pending) const { return pending ? -7 : team; }
    E_HumanRole GetRole() const { return role; }
    void RequestLeaveVehicle() { ++leaveRequests; }
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int flags) override
    {
        hits.push_back({type, amount, pos, dir, attacker, flags});
        g_hitOrder.push_back(this);
    }
    // The contacts (the car's callbacks): a3 / a4 are vectors there, read at the call.
    void OnCollision(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6) override
    {
        Collide(false, other, pos, a3, a4, a5, a6, 0);
    }
    bool OnCollisionEx(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6, int a7) override
    {
        Collide(true, other, pos, a3, a4, a5, a6, a7);
        return false;
    }
    void Collide(bool ex, C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6, int a7)
    {
        S_Contact c = {ex, other, pos, *pos, a3, a4, {}, {}, a5, a6, a7};
        if (a3)
            c.v3 = *reinterpret_cast<const S_vector *>(static_cast<intptr_t>(a3));
        if (a4)
            c.v4 = *reinterpret_cast<const S_vector *>(static_cast<intptr_t>(a4));
        contacts.push_back(c);
    }

    // The movement (a client's wreck darkens its lighting; null: none).
    C_HumanMovement *GetMovement() const { return movement; }
    C_HumanMovement *movement = nullptr;
    // The seat weapon (game/seat_weapon.cpp): the view a gunner's turret turns to, the disguise firing
    // costs, the noise.
    const S_vector &GetViewDir() const { return lookDir; }
    void SetDisguise(float value)
    {
        disguise = value;
        ++disguises;
    }
    void MakeNoise() { ++noises; }
    // (Part 7e: the bike suspends its rider; the static weapon alarms a gunner a shot missed.)
    void Suspend(bool suspend) { suspends.push_back(suspend); }
    void NotifyShotAt(C_Actor *shooter, const S_vector &velocity) { shotsAt.push_back({shooter, velocity}); }
    std::vector<bool> suspends;
    std::vector<std::pair<C_Actor *, S_vector>> shotsAt;
    S_vector lookDir = S_vector(0.0f, 0.0f, 1.0f);
    float disguise = 0.0f;
    int disguises = 0, noises = 0;

    bool localPlayer = false;
    int controllerType = HUMAN_CONTROLLER_ENEMY;
    int periodicLink = -1;
    int health = 800;
    S_vector4 viewDir = {}, facingDir = {};
    int viewDirs = 0, facingDirs = 0;
    E_HumanRole role = HUMAN_ROLE_ENEMY;
    int leaveRequests = 0;
    struct S_Hit {
        E_HitType type;
        float amount;
        S_vector pos, dir;
        C_Actor *attacker;
        int flags;
    };
    std::vector<S_Hit> hits;
    struct S_Contact {
        bool ex;
        C_Actor *other;
        const S_vector *pos;
        S_vector posValue;
        int a3, a4;
        S_vector v3, v4;
        int a5, a6, a7;
    };
    std::vector<S_Contact> contacts;
    std::vector<const C_Actor *> hostileTo;
    int experience = 0;
    I3D_frame *bodyFrame = nullptr;
    C_Actor *vehicle = nullptr;
    int vehicleSeat = 0;
    S_vector bonePos = S_vector(0.0f, 0.0f, 0.0f);
    std::vector<std::pair<int, bool>> removedItems;
    bool inPlay = true, spectating = false;
    int team = 0;
    int mpZone = -1;

    // The objective actors and the spawn point (part 7b): the items carried (type, item), the
    // team set, the controller, the setups, the StartMission calls.
    C_Item *FindItem(int typeId, int * = nullptr) const
    {
        for (const auto &it : items)
            if (it.first == typeId)
                return it.second;
        return nullptr;
    }
    C_Item *FindItemByUid(int uid) const
    {
        for (const auto &it : items)
            if (it.second->GetUid() == uid)
                return it.second;
        return nullptr;
    }
    void SetPlayerTeam(int newTeam, bool pending) { playerTeams.emplace_back(newTeam, pending); }
    C_HumanController *GetController() const { return controller; }
    struct S_Setup {
        S_SoldierState *state;
        int role, group;
        const int *stats;
        int playerSlot;
        S_SoldierState copy;                            // (the state and the stats as they were: the
        std::vector<int> statValues;                    // spawn message's are locals)
    };
    void Setup(S_SoldierState &state, int setupRole, int group, const int *stats, int playerSlot)
    {
        setups.push_back({&state, setupRole, group, stats, playerSlot, state,
                          stats ? std::vector<int>(stats, stats + STAT_COUNT) : std::vector<int>{}});
    }
    std::vector<std::pair<int, C_Item *>> items;
    std::vector<std::pair<int, bool>> playerTeams;
    C_HumanController *controller = nullptr;
    std::vector<S_Setup> setups;
    std::vector<bool> startMissions;
};
