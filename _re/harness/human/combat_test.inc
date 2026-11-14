// Part 21 of the human harness (HUMAN_COMBAT builds, build_combat.bat; included by human_test.cpp after self_test.inc):
// the human's combat (game/human_combat.cpp) against the original's, on part 20's harness (its fakes of the combat
// functions left out: this build links the real ones). The modules the combat code talks to are fakes, their calls
// logged on both sides and their answers scripted.
#include "game/shots.h"
#include "game/vehicle.h"
#include "net/net_game.h"
#include "script/script.h"

asm(".globl _g_shotManager\n\t.bss\n\t.balign 16\n_g_shotManager:\n\t.space 1024\n\t.text");

namespace {
// What the combat fakes answer.
struct S_CombatScript {
    bool seenEnemy = false;                             // HasSeenEnemy
    bool surrendered = false;                           // IsSurrendered
    bool speaking = false;                              // IsHumanSpeaking
    int cheat = 0;                                      // GetCheatValue
    int localTeam = 0;                                  // C_NetGame::GetLocalTeam
    int playerTeam[64] = {};                            // C_NetGame::GetPlayerTeam by slot
    uint32_t visibleParts = 0;                          // C_SightModule::GetVisibleParts
    float visibility = 1.0f;                            // S_HumanBody::GetVisibilityFactor
    uint32_t ground[4] = {};                            // S_HumanBody::GetGround
    S_vector viewDir = S_vector(0, 0, 1);               // C_Human::GetViewDir
    C_Actor *human[64] = {};                            // C_ActorsModule::GetHuman by id
} g_combat;
}  // namespace

// ---- the port's side
C_Actor *C_ActorsModule::GetHuman(int id) const
{
    Log(nullptr, "GetHuman", {double(id)});
    return id >= 0 && id < 64 ? g_combat.human[id] : nullptr;
}
void C_Team::OnMemberDied(C_Human *human) { Log(human, "TeamOnMemberDied"); }
void C_CoopTeam::OnMemberDied(C_Human *human) { Log(human, "CoopTeamOnMemberDied"); }
void C_FormationShape::OnLeaderDied() { Log(this, "FormationOnLeaderDied"); }
const char *C_Human::GetPlayerName() const { return "playerName"; }
const S_vector &C_Human::GetViewDir() const { return g_combat.viewDir; }
bool C_Human::HasSeenEnemy(const C_Human *human) const
{
    Log(human, "HasSeenEnemy");
    return g_combat.seenEnemy;
}
bool C_Human::IsSurrendered() const { return g_combat.surrendered; }
void C_Human::OnSightEvent(int lost, int targetId) { Log(this, "OnSightEvent", {double(lost), double(targetId)}); }
int C_HumanController::Say(int sentence, int delay, uint32_t flags, bool force)
{
    Log(this, "ControllerSay", {double(sentence), double(delay), double(flags), double(force)});
    return 0;
}
void C_Interface::FlashDamage() { Log(nullptr, "FlashDamage"); }
int C_NetGame::GetLocalTeam() { return g_combat.localTeam; }
int C_NetGame::GetPlayerTeam(int slot, bool pending)
{
    Log(nullptr, "GetPlayerTeam", {double(slot), double(pending)});
    return slot >= 0 && slot < 64 ? g_combat.playerTeam[slot] : 0;
}
void C_NetGame::OnPlayerKilled(int victim, int killer) { Log(nullptr, "OnPlayerKilled", {double(victim), double(killer)}); }
void C_PlayerController::OnDamaged(int type, int zone, const S_vector &dir)
{
    Log(this, "PlayerOnDamaged", {double(type), double(zone), dir.x, dir.y, dir.z});
}
const char *C_QuickMessages::GetText(int id) const { return id == 0 ? "quick0" : "quick"; }
void C_Script::OnDeath() { Log(this, "ScriptOnDeath"); }
void C_ShotManager::PlayHitEffect(int material, const S_vector &pos, const S_vector &normal)
{
    Log(nullptr, "PlayHitEffect", {double(material), pos.x, pos.y, pos.z, normal.x, normal.y, normal.z});
}
void C_ShotManager::PlayHitSound(C_Actor *source, int material, const S_vector &pos, bool alternate, bool silentToAI)
{
    Log(source, "PlayHitSound", {double(material), pos.x, pos.y, pos.z, double(alternate), double(silentToAI)});
}
void C_ShotManager::SprayBlood(const S_vector &pos, const S_vector &dir)
{
    Log(nullptr, "SprayBlood", {pos.x, pos.y, pos.z, dir.x, dir.y, dir.z});
}
void C_ShotManager::SprayBloodRandom(const S_vector &pos, const S_vector &dir)
{
    Log(nullptr, "SprayBloodRandom", {pos.x, pos.y, pos.z, dir.x, dir.y, dir.z});
}
void C_SightModule::GetVisibleParts(int observer, int target, uint32_t *parts)
{
    Log(nullptr, "GetVisibleParts", {double(observer), double(target)});
    *parts = g_combat.visibleParts;
}
void C_SoundModule::PlayMusic(int track) { Log(this, "PlayMusic", {double(track)}); }
bool C_Vehicle::SetBrake(float value)
{
    Log(this, "SetBrake", {value});
    return true;
}
bool C_Vehicle::SetHandbrake(bool on, bool unknown)
{
    Log(this, "SetHandbrake", {double(on), double(unknown)});
    return true;
}
bool C_Vehicle::SetSteering(float value)
{
    Log(this, "SetSteering", {value});
    return true;
}
bool C_Vehicle::SetThrottle(float value, bool first)
{
    Log(this, "SetThrottle", {value, double(first)});
    return true;
}
bool C_Vehicle::SetUnknown4f23b0(float value)
{
    Log(this, "SetUnknown4f23b0", {value});
    return true;
}
void C_game::PrintMessage(const S_vector &color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char text[256];
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    Log(nullptr, (std::string("PrintMessage ") + text).c_str(), {color.x, color.y, color.z});
}
int GetCheatValue(int index)
{
    Log(nullptr, "GetCheatValue", {double(index)});
    return g_combat.cheat;
}
bool IsHumanSpeaking(const C_Human *human)
{
    Log(human, "IsHumanSpeaking");
    return g_combat.speaking;
}
void PeriodicLinks_SetRate(int id, int rate) { Log(nullptr, "PeriodicLinks_SetRate", {double(id), double(rate)}); }
void StopFrameSpeech(I3D_frame *owner) { Log(nullptr, ("StopFrameSpeech " + NameOf(owner)).c_str()); }
void S_HumanBody::ApplyHit(uint32_t flags, const S_vector &pos, const S_vector &force)
{
    Log(nullptr, "BodyApplyHit", {double(flags), pos.x, pos.y, pos.z, force.x, force.y, force.z});
}
void S_HumanBody::Cripple() { Log(nullptr, "BodyCripple"); }
void S_HumanBody::Die(C_Actor *killer) { Log(killer, "BodyDie"); }
void S_HumanBody::PlayDeath() { Log(nullptr, "BodyPlayDeath"); }
void S_HumanBody::Ragdoll() { Log(nullptr, "BodyRagdoll"); }
void S_HumanBody::ResetMovement() { Log(nullptr, "BodyResetMovement"); }
const uint32_t *S_HumanBody::GetGround() const { return g_combat.ground; }
float S_HumanBody::GetVisibilityFactor() const { return g_combat.visibility; }

namespace {
#define CT_ __attribute__((thiscall))
// ---- part 21a: sides, teams, disguise and stats

// Who is in the player's team: by object (each human in both layouts).
const void *g_memberSelf[2], *g_memberOther[2];
bool g_isMemberSelf = false, g_isMemberOther = false;
bool MemberOf(const void *human)
{
    if (human == g_memberSelf[0] || human == g_memberSelf[1])
        return g_isMemberSelf;
    if (human == g_memberOther[0] || human == g_memberOther[1])
        return g_isMemberOther;
    return false;
}

// The worn items (head, uniform, hands, long gun, backpack): one container object each, its item scripted; the items'
// definitions with their side (the original's item +0x04 -> definition +0x44; the port's at its own offsets).
struct S_FakeContainer {
    void **vt;
    void *item;
    uint8_t rest[0x38];
};
S_FakeContainer g_wornContainers[16];
C_ItemContainer *g_wornTable[16];
void *g_wornVt[64];
alignas(16) uint8_t g_wornItems[16][0x80], g_wornDefs[3][0x200];
// (Not logged: a getter. The original's GetSide asks for the uniform twice, and its UpdateDisguise asks the worn
// containers in another order: neither changes the result.)
void *CT_ WC_GetItem(void *self) { return static_cast<S_FakeContainer *>(self)->item; }

// The original's: the player team's members by object; the network game's player teams; the controller's OnShotAt.
int CT_ OC_IsMember(void *, const void *human)
{
    Log(human, "IsTeamMember");
    return MemberOf(human);
}
int CT_ OC_GetPlayerTeam(void *, int slot, int pending)
{
    Log(nullptr, "GetPlayerTeam", {double(slot), double(B(pending))});
    return slot >= 0 && slot < 64 ? g_combat.playerTeam[slot] : 0;
}
void CT_ CT_OnShotAt(void *self, void *shooter, const S_vector *v)
{
    Log(self, "ControllerOnShotAt", {double(shooter != nullptr), v->x, v->y, v->z});
}

// (Their protected fields through derived accessors.)
struct S_DefAccess : C_ItemDef {
    static void SetTeam(void *def, int team) { static_cast<S_DefAccess *>(static_cast<C_ItemDef *>(def))->m_team = team; }
};
struct S_ItemAccess : C_Item {
    static void SetDef(void *item, void *def)
    {
        static_cast<S_ItemAccess *>(static_cast<C_Item *>(item))->m_def = static_cast<C_ItemDef *>(def);
    }
};

// Another human (the port's layout and the original's: the same values), and a MeleeTracker.
alignas(16) uint8_t g_portOther[sizeof(C_Human) + 0x100], g_origOther[0x1000], g_meleeBytes[0x200];
C_Human *PortOther() { return reinterpret_cast<C_Human *>(g_portOther); }

typedef int(CT_ *T_KPtr)(void *, const void *);
typedef int(CT_ *T_KVoid)(void *);
typedef int(CT_ *T_KInt)(void *, int);
typedef void(CT_ *T_KSetTeam)(void *, int, int);
typedef void(CT_ *T_KFloat)(void *, float);
typedef void(CT_ *T_KInts)(void *, const int *);
typedef float(CT_ *T_KFactor)(void *, int);
typedef void(CT_ *T_KShotAt)(void *, const void *, const S_vector *);

void SetUpCombat()
{
    SetUpSelf();
    SetUpUpdate();
    SetUpMission();
    SetUpRestart();
    SetUpEquipment();
    SetUpRespawn();
    SetUpSaves();
    // The real ones on both sides (part 20's tests scripted or logged them).
    for (uint32_t at : {0x005a6600u, 0x005a0890u, 0x005aa790u, 0x005a60d0u})
        RestoreOriginal(at);
    RedirectOriginal(0x00461a20, reinterpret_cast<const void *>(OC_IsMember));
    RedirectOriginal(0x00730900, reinterpret_cast<const void *>(OC_GetPlayerTeam));
    g_self.memberOf = MemberOf;
    for (void *&slot : g_wornVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_wornVt[0x40 / 4] = reinterpret_cast<void *>(WC_GetItem);
    g_wornVt[VSlot(&C_ItemContainer::GetItem)] = reinterpret_cast<void *>(WC_GetItem);
    for (int k = 0; k < 16; ++k) {
        g_wornContainers[k].vt = g_wornVt;
        g_wornTable[k] = reinterpret_cast<C_ItemContainer *>(&g_wornContainers[k]);
        g_names[&g_wornContainers[k]] = "container" + std::to_string(k);
    }
    // The definitions of the three sides (0 allied, 1 axis, 2 all).
    for (int t = 0; t < 3; ++t) {
        S_DefAccess::SetTeam(g_wornDefs[t], t);
        std::memcpy(g_wornDefs[t] + 0x44, &t, 4);
    }
    g_portControllerVt[VSlot(&C_HumanController::OnShotAt)] = reinterpret_cast<void *>(CT_OnShotAt);
    g_origControllerVt[0xB8 / 4] = reinterpret_cast<void *>(CT_OnShotAt);
    g_names[g_portOther] = g_names[g_origOther] = "other";
    g_names[g_meleeBytes] = "meleeTracker";
    reinterpret_cast<C_Actor *>(g_meleeBytes)->m_type = ACTOR_MELEE_TRACKER;
    const int melee = ACTOR_MELEE_TRACKER;
    std::memcpy(g_meleeBytes + 0x18, &melee, 4);
}

// A human's side: its role, the game (local or network, co-op, the uniforms disguising), the team, what it wears.
void RandomSide()
{
    RandomRestart();
    C_Human *h = g_h.human;
    h->m_containers = g_wornTable;
    for (int k = 0; k < 16; ++k) {
        S_FakeContainer &c = g_wornContainers[k];
        c.item = P(0.4f) ? nullptr : g_wornItems[k];
        const int t = I(0, 2);
        S_ItemAccess::SetDef(g_wornItems[k], g_wornDefs[t]);
        void *def = g_wornDefs[t];
        std::memcpy(g_wornItems[k] + 0x04, &def, 4);
    }
    h->m_role = static_cast<E_HumanRole>(I(0, 5));
    h->m_hostileToPlayers = P(0.3f);
    h->m_disguise = P(0.3f) ? 0.0f : P(0.2f) ? -U(0, 5) : P(0.2f) ? U(0, 1.2f) : U(0, 120);
    h->m_carried = P(0.2f) ? reinterpret_cast<C_Actor *>(g_carriedBytes) : nullptr;
    const bool disguise = P(0.5f);
    GetGame()->SetUniformDisguise(disguise);
    g_origGame[0x34] = disguise;
    g_mpSettings->gameType = P(0.4f) ? MP_GAME_DEATHMATCH : I(1, 4);
    for (int &t : g_combat.playerTeam)
        t = I(0, 2);
    g_isMemberSelf = P(0.5f);
    g_isMemberOther = P(0.5f);
    for (int i = 0; i < 12; ++i) {
        h->m_stats[i] = P(0.1f) ? 0 : I(-10, 900);
        h->m_initialStats[i] = P(0.1f) ? 0 : I(1, 900);
    }
    h->m_stamina = P(0.2f) ? 0.0f : U(0, 150);
    g_memberSelf[0] = h;
    g_memberSelf[1] = o_human;
}

// The other human: a state like the tested one's, copied in both layouts (ToOriginal), then the tested one made again.
void RandomOther()
{
    RandomSide();
    ToOriginal();
    std::memcpy(g_portOther, static_cast<const void *>(g_h.human), sizeof(C_Human));
    std::memcpy(g_origOther, o_human, sizeof g_origOther);
    g_memberOther[0] = g_portOther;
    g_memberOther[1] = g_origOther;
    const bool memberOther = g_isMemberOther;
    RandomSide();
    g_isMemberOther = memberOther;
}

void TestCombatSides()
{
    C_Human *h = g_h.human;
    std::printf("combat sides\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomOther();
        const int which = i % 12;
        // (MeleeTrackers only in local games: network games meet humans only, as IsFriendOf says.)
        const bool melee = which == 1 && (g_gameRole & 7) == 7 && P(0.3f);
        const void *portOther = melee ? static_cast<const void *>(g_meleeBytes) : g_portOther;
        const void *origOther = melee ? static_cast<const void *>(g_meleeBytes) : g_origOther;
        switch (which) {
        case 0:
            DiffCall("IsFriendOfInNetGame", [&] { return int(h->IsFriendOfInNetGame(PortOther())); },
                     [&](void *) { return OriginalAt<T_KPtr>(0x005a09a0)(o_human, g_origOther) & 0xFF; });
            break;
        case 1:
            DiffCall("IsFriendOf", [&] { return int(h->IsFriendOf(static_cast<const C_Actor *>(portOther))); },
                     [&](void *) { return OriginalAt<T_KPtr>(0x005a0ab0)(o_human, origOther) & 0xFF; });
            break;
        case 2:
            DiffCall("IsDisguisedEnemy", [&] { return int(h->IsDisguisedEnemy(PortOther())); },
                     [&](void *) { return OriginalAt<T_KPtr>(0x005a0a50)(o_human, g_origOther) & 0xFF; });
            break;
        case 3:
            DiffCall("GetSide", [&] { return h->GetSide(); },
                     [&](void *) { return OriginalAt<T_KVoid>(0x005a84f0)(o_human); });
            break;
        case 4: {
            const bool pending = P(0.5f);
            DiffCall("GetTeam", [&] { return h->GetTeam(pending); },
                     [&](void *) { return OriginalAt<T_KInt>(0x005a8820)(o_human, pending); });
            break;
        }
        case 5: {
            const int team = I(0, 3);
            const bool pending = P(0.5f);
            DiffCall("SetPlayerTeam", [&] { h->SetPlayerTeam(team, pending); return 0; },
                     [&](void *) { OriginalAt<T_KSetTeam>(0x005a8800)(o_human, team, pending); return 0; });
            break;
        }
        case 6:
            DiffCall("IsLocalPlayer", [&] { return int(h->IsLocalPlayer()); },
                     [&](void *) { return OriginalAt<T_KVoid>(0x005a0890)(o_human) & 0xFF; });
            break;
        case 7:
            DiffCall("IsServerSimulated", [&] { return int(h->IsServerSimulated()); },
                     [&](void *) { return OriginalAt<T_KVoid>(0x005aa790)(o_human) & 0xFF; });
            break;
        case 8: {
            const float amount = P(0.2f) ? 0.0f : U(-20, 60);
            DiffCall("UpdateDisguise", [&] { h->UpdateDisguise(amount); return 0; },
                     [&](void *) { OriginalAt<T_KFloat>(0x005a60d0)(o_human, amount); return 0; });
            break;
        }
        case 9: {
            const float value = U(-10, 150);
            if (P(0.5f))
                DiffCall("SetDisguise", [&] { h->SetDisguise(value); return 0; },
                         [&](void *) { OriginalAt<T_KFloat>(0x005a6250)(o_human, value); return 0; });
            else
                DiffCall("GetDisguisePercent", [&] { return h->GetDisguisePercent(); },
                         [&](void *) { return OriginalAt<T_KVoid>(0x005a6280)(o_human); });
            break;
        }
        case 10: {
            const S_vector v = S_vector(U(-200, 200), U(-50, 50), U(-200, 200));
            reinterpret_cast<C_HumanController *>(g_portController)->m_moveFlags = P(0.3f) ? uint32_t(MOVE_DEAF) : 0u;
            std::memcpy(g_origController + 0x50, &reinterpret_cast<C_HumanController *>(g_portController)->m_moveFlags, 4);
            DiffCall("NotifyShotAt", [&] { h->NotifyShotAt(reinterpret_cast<C_Actor *>(g_portOther), v); return 0; },
                     [&](void *) { OriginalAt<T_KShotAt>(0x005a5960)(o_human, g_origOther, &v); return 0; });
            break;
        }
        default: {
            // Back to the scripted answer for the other tests' human (none here).
            const int stat = I(-2, 14);
            DiffCall("GetStat", [&] { return h->GetStat(stat); },
                     [&](void *) { return OriginalAt<T_KInt>(0x005a6600)(o_human, stat); });
            break;
        }
        }
    }
}

// The stats: their factors, the skills set, morale changed, health and stamina as percents.
void TestCombatStats()
{
    C_Human *h = g_h.human;
    std::printf("combat stats\n");
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomSide();
        switch (i % 6) {
        case 0: {
            const int stat = I(-2, 14);
            DiffCall("GetStatFactor",
                     [&] {
                         const float f = h->GetStatFactor(stat);
                         int bits;
                         std::memcpy(&bits, &f, 4);
                         return bits;
                     },
                     [&](void *) {
                         const float f = OriginalAt<T_KFactor>(0x005a6530)(o_human, stat);
                         int bits;
                         std::memcpy(&bits, &f, 4);
                         return bits;
                     });
            break;
        }
        case 1: {
            int skills[12];
            for (int &v : skills)
                v = I(0, 120);
            DiffCall("SetSkills", [&] { h->SetSkills(skills); return 0; },
                     [&](void *) { OriginalAt<T_KInts>(0x005a96a0)(o_human, skills); return 0; });
            break;
        }
        case 2: {
            const int delta = I(-200, 200);
            DiffCall("ChangeMorale", [&] { h->ChangeMorale(delta); return 0; },
                     [&](void *) { OriginalAt<T_KInt>(0x005a64f0)(o_human, delta); return 0; });
            break;
        }
        case 3:
            DiffCall("GetHealthPercent", [&] { return h->GetHealthPercent(); },
                     [&](void *) { return OriginalAt<T_KVoid>(0x005a5ff0)(o_human); });
            break;
        case 4: {
            const int percent = I(-20, 130);
            DiffCall("SetHealthPercent", [&] { h->SetHealthPercent(percent); return 0; },
                     [&](void *) { OriginalAt<T_KInt>(0x005a6060)(o_human, percent); return 0; });
            break;
        }
        default:
            DiffCall("GetStaminaPercent", [&] { return h->GetStaminaPercent(); },
                     [&](void *) { return OriginalAt<T_KVoid>(0x005a62a0)(o_human); });
            break;
        }
    }
}

void TestCombat()
{
    SetUpCombat();
    TestCombatSides();
    TestCombatStats();
}

}  // namespace
