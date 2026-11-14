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
    bool groundOn = true;                               // S_HumanBody::GetGround answers the ground (else none)
    float cover = 1.0f;                                 // the graphic effects' GetGroundCover
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
#ifndef HUMAN_CONTROL   // (the control build links game/human_control.cpp's)
const char *C_Human::GetPlayerName() const { return "playerName"; }
#endif
#ifndef HUMAN_CONTROL   // (the control build links game/human_control.cpp's)
const S_vector &C_Human::GetViewDir() const { return g_combat.viewDir; }
#endif
bool C_Human::HasSeenEnemy(const C_Human *human) const
{
    Log(human, "HasSeenEnemy");
    return g_combat.seenEnemy;
}
#ifndef HUMAN_CONTROL   // (the control build links game/human_control.cpp's)
bool C_Human::IsSurrendered() const { return g_combat.surrendered; }
#endif
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
const uint32_t *S_HumanBody::GetGround() const { return g_combat.groundOn ? g_combat.ground : nullptr; }
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
    for (uint32_t at : {0x005a6600u, 0x005a0890u, 0x005aa790u, 0x005a60d0u, 0x005a0420u, 0x005a0480u, 0x005a0660u, 0x005a15d0u})
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
    KeepStr(h->m_name, "name");
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
    // (Each human its own strings, kept: a copied human shares the C_str reps without a reference, and each side's
    // GetName / SetTexture releases the one it replaces.)
    KeepStr(PortOther()->m_name, "otherName");
    KeepStr(PortOther()->m_faceTexture, "otherFace");
    std::memset(g_origOther + 0x80, 0, 4);
    std::memset(g_origOther + 0xBC, 0, 4);
    OriginalAt<T_StrAssign>(0x00405180)(g_origOther + 0x80, "otherName");
    OriginalAt<T_StrAssign>(0x00405180)(g_origOther + 0xBC, "otherFace");
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

// ---- part 21b: bleeding, motion, visibility, the bones shots aim at, the box, fading

float CT_ OB_VisibilityFactor(void *) { return g_combat.visibility; }
const uint32_t *CT_ OB_GetGround(void *) { return g_combat.groundOn ? g_combat.ground : nullptr; }
void CT_ OB_GetVisibleParts(void *, int observer, int target, uint32_t *parts)
{
    Log(nullptr, "GetVisibleParts", {double(observer), double(target)});
    *parts = g_combat.visibleParts;
}
void CT_ OB_SprayBloodRandom(void *, const S_vector *pos, const S_vector *dir)
{
    Log(nullptr, "SprayBloodRandom", {pos->x, pos->y, pos->z, dir->x, dir->y, dir->z});
}
void *OB_GetCoopTeam(void *) { return g_teamManager.GetCoopTeam(); }
float CT_ GE_GetGroundCover(void *self, int material)
{
    Log(self, "GetGroundCover", {double(material)});
    return g_combat.cover;
}

// The body's visual "a" under the model: its joints (+0x214), 25 dummies around it; its local box.
I3D_frame *g_bodyVisual, *g_joints[25], *g_otherFrame;

void SetUpMotion()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005ac350, reinterpret_cast<const void *>(OB_VisibilityFactor)},
        {0x005ac430, reinterpret_cast<const void *>(OB_GetGround)},
        {0x0046f2a0, reinterpret_cast<const void *>(OB_GetVisibleParts)},
        {0x0044f4f0, reinterpret_cast<const void *>(OB_SprayBloodRandom)},
        {0x00461420, reinterpret_cast<const void *>(OB_GetCoopTeam)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_origGeVt[0x94 / 4] = reinterpret_cast<void *>(GE_GetGroundCover);
    g_selfGeVt[VSlot(&C_GEffectsModule::GetGroundCover)] = reinterpret_cast<void *>(GE_GetGroundCover);
    S_quat none;
    none.Identity();
    // (The base harness's model has its body visual "a" already: its joints set here.)
    g_bodyVisual = FindIn(g_h.frame, "a");
    if (!g_bodyVisual) {
        I3D_bbox box;
        box.min = S_vector(-0.3f, -0.9f, -0.2f);
        box.max = S_vector(0.35f, 0.95f, 0.25f);
        g_bodyVisual = NewFrame(FRAME_VISUAL, "a", S_vector(0, 0.9f, 0), none, box, "", g_h.frame);
    }
    for (int k = 0; k < 25; ++k)
        g_joints[k] = NewFrame(FRAME_DUMMY, "joint" + std::to_string(k), S_vector(U(-0.4f, 0.4f), U(-0.9f, 0.9f), U(-0.2f, 0.2f)),
                               none, Box(S_vector(0.02f, 0.02f, 0.02f)), "", g_bodyVisual);
    g_otherFrame = NewFrame(FRAME_DUMMY, "otherModel", S_vector(20, 1, 20), none, Box(S_vector(0.3f, 0.9f, 0.3f)), "", g_sector);
    I3D_frame **joints = g_joints;
    std::memcpy(reinterpret_cast<uint8_t *>(g_bodyVisual) + 0x214, &joints, 4);
}

// The motion state: bleeding, its drops' timer, the model's place (moving or not), a seat; the controller's move
// flags and quiet time; the body's visibility, the ground's material and cover; the parts a shooter sees.
void RandomMotion()
{
    RandomSide();
    C_Human *h = g_h.human;
    h->m_bleeding = P(0.3f) ? 0.0f : U(0, 3);
    h->m_bloodDropTime = P(0.3f) ? I(1400, 2600) : I(0, 3000);
    h->m_vehicle = P(0.2f) ? reinterpret_cast<C_Actor *>(g_vehicleBytes) : nullptr;
    const S_vector at(U(4, 30), U(0.5f, 2), U(4, 30));
    g_h.frame->SetPosInline(at);
    UpdateTree(g_h.frame);
    const S_vector last = P(0.3f) ? at : at + S_vector(U(-0.3f, 0.3f), U(-0.1f, 0.1f), U(-0.3f, 0.3f));
    h->m_lastPos = {last.x, last.y, last.z, 1.0f};
    h->m_velocity = P(0.3f) ? S_vector4{0, 0, 0, 0} : S_vector4{U(-3, 3), U(-1, 1), U(-3, 3), 0.0f};
    C_HumanController *c = reinterpret_cast<C_HumanController *>(g_portController);
    static const uint32_t stances[] = {MOVE_LYING, MOVE_CROUCHING, MOVE_STANDING, 0};
    static const uint32_t paces[] = {0, MOVE_PACE_1, MOVE_PACE_2, MOVE_PACE_3, MOVE_PACE_4};
    c->m_moveFlags = stances[I(0, 3)] | (P(0.6f) ? uint32_t(0x8 << I(0, 4)) : 0u) | paces[I(0, 4)];
    c->m_quietTime = P(0.2f) ? (P(0.5f) ? 7000 : 8000) + I(0, 1) : P(0.3f) ? I(6900, 8100) : I(0, 20000);
    std::memcpy(g_origController + 0x50, &c->m_moveFlags, 4);
    std::memcpy(g_origController + 0xB0, &c->m_quietTime, 4);
    g_combat.visibility = P(0.2f) ? 1.0f : U(0, 1.5f);
    g_combat.groundOn = P(0.7f);
    for (uint32_t &g : g_combat.ground)
        g = uint32_t(I(0, 0x7FFFFFFF));
    g_combat.cover = P(0.3f) ? 1.0f : U(0, 1);
    g_combat.visibleParts = P(0.2f) ? 0u : uint32_t(I(1, 63));
    for (I3D_frame *j : g_joints)
        j->SetPosInline(S_vector(U(-0.4f, 0.4f), U(-0.9f, 0.9f), U(-0.2f, 0.2f)));
    // The body visual's own box (each corner apart).
    I3D_bound_volume &bound = reinterpret_cast<I3D_visual *>(g_bodyVisual)->Field<I3D_bound_volume>(0x1A0);
    bound.bbox.min = S_vector(-U(0.1f, 0.5f), -U(0.5f, 1.0f), -U(0.1f, 0.4f));
    bound.bbox.max = S_vector(U(0.1f, 0.5f), U(0.5f, 1.0f), U(0.1f, 0.4f));
    reinterpret_cast<I3D_visual *>(g_bodyVisual)->VisualFlags() |= VISFLAGS_BOUND_VALID;
    UpdateTree(g_h.frame);
}

typedef float(CT_ *T_BFloat)(void *);
typedef const S_vector *(CT_ *T_BBone)(void *);
typedef const S_vector *(CT_ *T_BAim)(void *, const void *);
typedef void(CT_ *T_BCorners)(void *, S_vector4 *);

int FloatBits(float f)
{
    int bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
int VectorHash(const S_vector &v) { return FloatBits(v.x) ^ (FloatBits(v.y) * 3) ^ (FloatBits(v.z) * 7); }

void TestCombatMotion()
{
    C_Human *h = g_h.human;
    std::printf("combat motion\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomMotion();
        const int time = P(0.1f) ? 0 : I(1, 150);
        switch (i % 9) {
        case 0:
            DiffCall("TickBleeding", [&] { h->TickBleeding(time); return 0; },
                     [&](void *) { OriginalAt<T_KInt>(0x005a0420)(o_human, time); return 0; });
            break;
        case 1:
            DiffCall("UpdateBleeding", [&] { h->UpdateBleeding(time); return 0; },
                     [&](void *) { OriginalAt<T_KInt>(0x005a0480)(o_human, time); return 0; });
            break;
        case 2: {
            const int t = I(1, 150);
            DiffCall("UpdateVelocity", [&] { h->UpdateVelocity(t); return 0; },
                     [&](void *) { OriginalAt<T_KInt>(0x005a0660)(o_human, t); return 0; });
            break;
        }
        case 3:
            if (P(0.1f))
                h->m_controller = nullptr;
            DiffCall("GetVisibility", [&] { return FloatBits(h->GetVisibility()); },
                     [&](void *) { return FloatBits(OriginalAt<T_BFloat>(0x005a59e0)(o_human)); });
            break;
        case 4:
            DiffCall("GetRandomBonePos", [&] { return VectorHash(h->GetRandomBonePos()); },
                     [&](void *) { return VectorHash(*OriginalAt<T_BBone>(0x005a3cc0)(o_human)); });
            break;
        case 5: {
            const bool none = P(0.1f);
            RandomOther();
            RandomMotion();
            // (A shooter whose sight sees some parts: never only other bits, where the original divides by zero.)
            if (g_combat.visibleParts != 0 && !(g_combat.visibleParts & 0x1F))
                g_combat.visibleParts |= 1u << I(0, 4);
            // The shooter on its own model, 0..25 m away (the leg bones depend on the distance).
            g_otherFrame->SetPosInline(g_h.frame->GetWorldPos() + S_vector(U(-1, 1), 0, U(-1, 1)).Normalized() * U(0, 25));
            UpdateTree(g_otherFrame);
            PortOther()->m_frame = g_otherFrame;
            std::memcpy(g_origOther + 0x20, &g_otherFrame, 4);
            DiffCall("GetAimPoint", [&] { return VectorHash(h->GetAimPoint(none ? nullptr : PortOther())); },
                     [&](void *) { return VectorHash(*OriginalAt<T_BAim>(0x005a3ab0)(o_human, none ? nullptr : g_origOther)); });
            break;
        }
        case 6: {
            S_vector4 portCorners[8] = {}, origCorners[8] = {};
            DiffCall("GetBoxCorners", [&] { h->C_Human::GetBoxCorners(portCorners); return 0; },
                     [&](void *) { OriginalAt<T_BCorners>(0x005a85f0)(o_human, origCorners); return 0; });
            for (int k = 0; k < 8; ++k)
                for (int c = 0; c < 3; ++c)
                    CHECK(std::fabs((&portCorners[k].x)[c] - (&origCorners[k].x)[c]) <= 1e-5f);
            break;
        }
        case 7:
            DiffCall("FadeOut", [&] { h->FadeOut(time); return 0; },
                     [&](void *) { OriginalAt<T_KInt>(0x005a86f0)(o_human, time); return 0; });
            break;
        default:
            DiffCall("FadeIn", [&] { h->FadeIn(time); return 0; },
                     [&](void *) { OriginalAt<T_KInt>(0x005a8740)(o_human, time); return 0; });
            break;
        }
    }
}

// ---- part 21c: the messages and the deaths

// The human's own virtual GetName and GetMaterialId on both sides (the port's slots call the real members).
C_str *CT_ PH_GetName(C_Human *self) { return self->C_Human::GetName(); }
int CT_ PH_GetMaterialId(C_Human *self, I3D_frame *part) { return self->C_Human::GetMaterialId(part); }
// The humans by id (the tested one and the other: each side its own).
void *CT_ OD_GetHuman(void *, int id)
{
    Log(nullptr, "GetHuman", {double(id)});
    C_Actor *port = id >= 0 && id < 64 ? g_combat.human[id] : nullptr;
    if (port == reinterpret_cast<C_Actor *>(g_h.human))
        return o_human;
    if (port == reinterpret_cast<C_Actor *>(g_portOther))
        return g_origOther;
    return port;
}
int CT_ OD_GetLocalTeam(void *) { return g_combat.localTeam; }
void OD_PrintMessage(void *, const S_vector *color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char text[256];
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    Log(nullptr, (std::string("PrintMessage ") + text).c_str(), {color->x, color->y, color->z});
}
const char *CT_ OD_QuickText(void *, int id) { return id == 0 ? "quick0" : "quick"; }
const char *CT_ OD_GetPlayerName(void *) { return "playerName"; }
void CT_ OD_BodyPlayDeath(void *) { Log(nullptr, "BodyPlayDeath"); }
void CT_ OD_BodyApplyHit(void *, uint32_t flags, const S_vector *pos, const S_vector *force)
{
    Log(nullptr, "BodyApplyHit", {double(flags), pos->x, pos->y, pos->z, force->x, force->y, force->z});
}
void __stdcall OD_SetRate(int id, int rate) { Log(nullptr, "PeriodicLinks_SetRate", {double(id), double(rate)}); }
void CT_ OD_TeamOnMemberDied(void *, void *human) { Log(human, "TeamOnMemberDied"); }
void CT_ OD_CoopOnMemberDied(void *, void *human) { Log(human, "CoopTeamOnMemberDied"); }
void CT_ OD_PlayMusic(void *self, int track) { Log(self, "PlayMusic", {double(track)}); }
void CT_ OD_OnLeaderDied(void *self) { Log(self, "FormationOnLeaderDied"); }
int CT_ OD_SetThrottle(void *self, float v, int first)
{
    Log(self, "SetThrottle", {v, double(B(first))});
    return 1;
}
int CT_ OD_SetSteering(void *self, float v)
{
    Log(self, "SetSteering", {v});
    return 1;
}
int CT_ OD_SetBrake(void *self, float v)
{
    Log(self, "SetBrake", {v});
    return 1;
}
int CT_ OD_SetHandbrake(void *self, int on, int unknown)
{
    Log(self, "SetHandbrake", {double(B(on)), double(B(unknown))});
    return 1;
}
int CT_ OD_SetUnknown4f23b0(void *self, float v)
{
    Log(self, "SetUnknown4f23b0", {v});
    return 1;
}
void CT_ OD_StopFrameSpeech(I3D_frame *owner) { Log(nullptr, ("StopFrameSpeech " + NameOf(owner)).c_str()); }
int CT_ OD_IsSurrendered(void *) { return g_combat.surrendered; }
int CT_ OD_HasSeenEnemy(void *, const void *human)
{
    Log(human, "HasSeenEnemy");
    return g_combat.seenEnemy;
}
void CT_ OD_OnSightEvent(void *self, int lost, int target) { Log(self, "OnSightEvent", {double(lost), double(target)}); }
void CT_ OD_OnPlayerKilled(void *, int victim, int killer) { Log(nullptr, "OnPlayerKilled", {double(victim), double(killer)}); }
void CT_ OD_FlashDamage(void *) { Log(nullptr, "FlashDamage"); }
void CT_ OD_OnDamaged(void *self, int type, int zone, const S_vector *dir)
{
    Log(self, "PlayerOnDamaged", {double(type), double(zone), dir->x, dir->y, dir->z});
}
void CT_ OD_PlayHitSound(void *, void *source, int material, const S_vector *pos, int alternate, int silent)
{
    Log(source, "PlayHitSound", {double(material), pos->x, pos->y, pos->z, double(B(alternate)), double(B(silent))});
}
void CT_ OD_SprayBlood(void *, const S_vector *pos, const S_vector *dir)
{
    Log(nullptr, "SprayBlood", {pos->x, pos->y, pos->z, dir->x, dir->y, dir->z});
}
void CT_ OD_PlayHitEffect(void *, int material, const S_vector *pos, const S_vector *normal)
{
    Log(nullptr, "PlayHitEffect", {double(material), pos->x, pos->y, pos->z, normal->x, normal->y, normal->z});
}
// The controller's death calls (the port's slots by member pointers, the original's 0x84 / 0x88 / 0x8C / 0x94).
void CT_ CT_OnDeath(void *self) { Log(self, "ControllerOnDeath"); }
void CT_ CT_OnKilled(void *self) { Log(self, "ControllerOnKilled"); }
void CT_ CT_OnKnockedOut(void *self) { Log(self, "ControllerOnKnockedOut"); }
void CT_ CT_OnKilledEnemy(void *self, void *victim) { Log(victim, "ControllerOnKilledEnemy"); }

// The multiplayer messages' texts show their arguments: as many "%s" as each takes.
const char *MessageArguments(int id)
{
    switch (id) {
    case 0x2292: case 0x2293: case 0x229B: case 0x229C: case 0x229D:
        return " [%s|%s]";
    case 0x2297: case 0x2298: case 0x2299: case 0x229A: case 0x229E: case 0x22A3:
        return " [%s]";
    default:
        return "";
    }
}

void (*g_previousToOriginal)();
void SlotToOriginal()
{
    if (g_previousToOriginal)
        g_previousToOriginal();
    *OriginalAt<int *>(0x00982104) = g_localPlayerSlot;
}

void SetUpDeaths()
{
    g_previousToOriginal = g_toOriginalExtra;
    g_toOriginalExtra = SlotToOriginal;
    g_self.textSuffix = MessageArguments;
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00475e00, reinterpret_cast<const void *>(OD_GetHuman)},
        {0x00730880, reinterpret_cast<const void *>(OD_GetLocalTeam)},
        {0x0069c240, reinterpret_cast<const void *>(OD_PrintMessage)},
        {0x004900e0, reinterpret_cast<const void *>(OD_QuickText)},
        {0x005a81b0, reinterpret_cast<const void *>(OD_GetPlayerName)},
        {0x005abbf0, reinterpret_cast<const void *>(OD_BodyPlayDeath)},
        {0x005abbd0, reinterpret_cast<const void *>(OD_BodyApplyHit)},
        {0x006e03a0, reinterpret_cast<const void *>(OD_SetRate)},
        {0x0045e790, reinterpret_cast<const void *>(OD_TeamOnMemberDied)},
        {0x0045fe10, reinterpret_cast<const void *>(OD_CoopOnMemberDied)},
        {0x00595710, reinterpret_cast<const void *>(OD_OnLeaderDied)},
        {0x004f2100, reinterpret_cast<const void *>(OD_SetThrottle)},
        {0x004f2420, reinterpret_cast<const void *>(OD_SetSteering)},
        {0x004f2270, reinterpret_cast<const void *>(OD_SetBrake)},
        {0x004f26a0, reinterpret_cast<const void *>(OD_SetHandbrake)},
        {0x004f23b0, reinterpret_cast<const void *>(OD_SetUnknown4f23b0)},
        {0x0071e830, reinterpret_cast<const void *>(OD_StopFrameSpeech)},
        {0x005a6450, reinterpret_cast<const void *>(OD_IsSurrendered)},
        {0x005a5e50, reinterpret_cast<const void *>(OD_HasSeenEnemy)},
        {0x005a5250, reinterpret_cast<const void *>(OD_OnSightEvent)},
        {0x0072f230, reinterpret_cast<const void *>(OD_OnPlayerKilled)},
        {0x0049f0b0, reinterpret_cast<const void *>(OD_FlashDamage)},
        {0x00545dd0, reinterpret_cast<const void *>(OD_OnDamaged)},
        {0x00451b00, reinterpret_cast<const void *>(OD_PlayHitSound)},
        {0x0044f4d0, reinterpret_cast<const void *>(OD_SprayBlood)},
        {0x00451c00, reinterpret_cast<const void *>(OD_PlayHitEffect)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    p_humanVt[VSlot(&C_Actor::GetName)] = reinterpret_cast<void *>(PH_GetName);
    p_humanVt[VSlot(&C_Actor::GetMaterialId)] = reinterpret_cast<void *>(PH_GetMaterialId);
    o_humanVt[0x04 / 4] = OriginalAt<void *>(0x005a9980);
    o_humanVt[0xD8 / 4] = OriginalAt<void *>(0x0059c2a0);
    g_origSoundVt[0x68 / 4] = reinterpret_cast<void *>(OD_PlayMusic);
    g_portControllerVt[VSlot(&C_HumanController::OnDeath)] = reinterpret_cast<void *>(CT_OnDeath);
    g_portControllerVt[VSlot(&C_HumanController::OnKilled)] = reinterpret_cast<void *>(CT_OnKilled);
    g_portControllerVt[VSlot(&C_HumanController::OnKnockedOut)] = reinterpret_cast<void *>(CT_OnKnockedOut);
    g_portControllerVt[VSlot(&C_HumanController::OnKilledEnemy)] = reinterpret_cast<void *>(CT_OnKilledEnemy);
    g_origControllerVt[0x84 / 4] = reinterpret_cast<void *>(CT_OnDeath);
    g_origControllerVt[0x88 / 4] = reinterpret_cast<void *>(CT_OnKilled);
    g_origControllerVt[0x8C / 4] = reinterpret_cast<void *>(CT_OnKnockedOut);
    g_origControllerVt[0x94 / 4] = reinterpret_cast<void *>(CT_OnKilledEnemy);
    // The car's vehicle: the port's member at its own offset, the original's at +0x90.
    g_names[reinterpret_cast<C_Car *>(g_vehicleBytes)->GetVehicle()] = g_names[g_vehicleBytes + 0x90] = "carVehicle";
}

// A death's state: the other human (a killer or a victim; its controller the original's on its side), the human's
// vehicle a car or not, its ids, a formation, the network game's local team, the scripted answers.
void RandomDeath()
{
    RandomOther();
    RandomMotion();
    C_Human *h = g_h.human;
    void *origController = g_origController;
    std::memcpy(g_origOther + 0xB0, &origController, 4);
    PortOther()->m_controller = reinterpret_cast<C_HumanController *>(g_portController);
    h->m_controller = reinterpret_cast<C_HumanController *>(g_portController);
    const bool car = P(0.6f);
    reinterpret_cast<C_Actor *>(g_vehicleBytes)->m_type = car ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
    const int type = car ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
    std::memcpy(g_vehicleBytes + 0x18, &type, 4);
    h->m_formation = P(0.3f) ? reinterpret_cast<C_FormationShape *>(g_formationBytes) : nullptr;
    h->m_bodyFrame = g_zFrame;
    h->m_sightId = P(0.3f) ? -1 : I(0, 20);
    h->m_sightTargetId = P(0.2f) ? -1 : I(0, 20);
    h->m_soundListener = P(0.3f) ? -1 : I(0, 20);
    h->m_periodicLink = I(-1, 20);
    g_combat.localTeam = I(0, 2);
    g_combat.surrendered = P(0.2f);
    g_combat.seenEnemy = P(0.5f);
    g_combat.speaking = P(0.5f);
    g_self.unconscious = P(0.15f);
    // The ids GetHuman knows: the tested human, the other one.
    for (C_Actor *&a : g_combat.human)
        a = nullptr;
    h->m_netId = I(0, 30);
    PortOther()->m_netId = (h->m_netId + I(1, 30)) % 32;
    std::memcpy(g_origOther + 0x10, &PortOther()->m_netId, 4);
    g_combat.human[h->m_netId] = reinterpret_cast<C_Actor *>(h);
    g_combat.human[PortOther()->m_netId] = reinterpret_cast<C_Actor *>(g_portOther);
    // The local player: the tested human, the other one or nobody.
    g_localPlayerSlot = P(0.4f) ? h->m_netId : P(0.6f) ? PortOther()->m_netId : 99;
    g_script.localPlayer = g_localPlayerSlot == h->m_netId;
    // A health of 1 sometimes; the victim's controller AI or another, its quiet time short or long.
    if (P(0.1f))
        h->m_stats[STAT_HEALTH] = 1;
    static const E_HumanController types[] = {HUMAN_CONTROLLER_PLAYER, HUMAN_CONTROLLER_REMOTE, HUMAN_CONTROLLER_ENEMY,
                                              HUMAN_CONTROLLER_AI};
    C_HumanController *c = reinterpret_cast<C_HumanController *>(g_portController);
    c->m_type = types[I(0, 3)];
    std::memcpy(g_origController + 0x48, &c->m_type, 4);
    c->m_quietTime = P(0.3f) ? 60000 + I(0, 1) : P(0.5f) ? I(0, 70000) : I(60000, 200000);
    std::memcpy(g_origController + 0xB0, &c->m_quietTime, 4);
}

typedef void(CT_ *T_DMessage)(void *, int, int, int);
typedef void (*T_DPrint)(const void *, const S_vector *, int, ...);
typedef void(CT_ *T_DText)(void *, const char *);
typedef void(CT_ *T_DKilled)(void *, void *, int);
typedef void(CT_ *T_DRemote)(void *, int, int, int, int, const S_vector *, const S_vector *);

void TestCombatDeaths()
{
    C_Human *h = g_h.human;
    // (The name's rep each side's own: its text is compared in the messages printed.)
    g_skipFields.insert("m_name");
    std::printf("combat deaths\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomDeath();
        static const int types[] = {HIT_FALL, HIT_BULLET, HIT_EXPLOSION, HIT_MELEE, HIT_SCRIPT_KILL, HIT_THROWN_OBJECT,
                                    HIT_BACKSTAB, HIT_SUICIDE, HIT_RUN_OVER, HIT_SCRIPT_KILL_EX, HIT_DROWNING, 9};
        const int type = types[I(0, 11)];
        const int zone = P(0.1f) ? I(10, 13) : I(0, 11);
        // The killer: the tested human itself, the other, an unknown id.
        const int killerId = P(0.2f) ? h->m_netId : P(0.8f) ? PortOther()->m_netId : 50;
        switch (i % 9) {
        case 0:
            DiffCall("ShowHitMessage", [&] { h->ShowHitMessage(type, zone, killerId); return 0; },
                     [&](void *) { OriginalAt<T_DMessage>(0x005a0da0)(o_human, type, zone, killerId); return 0; });
            break;
        case 1: {
            // (A text with two arguments, one with one, or another.)
            const int textId = P(0.4f) ? 0x229D : P(0.5f) ? 0x229E : I(0, 9000);
            const S_vector color(U(0, 255), U(0, 255), U(0, 255));
            const bool named = P(0.5f);
            DiffCall(named ? "PrintNamedText" : "PrintText",
                     [&] {
                         if (named)
                             C_Human::PrintNamedText(h, color, textId, "arg1", "arg2");
                         else
                             C_Human::PrintText(h, color, textId, "arg1", "arg2");
                         return 0;
                     },
                     [&](void *) {
                         OriginalAt<T_DPrint>(named ? 0x005a4770 : 0x005a4600)(o_human, &color, textId, "arg1", "arg2");
                         return 0;
                     });
            break;
        }
        case 2: {
            const int index = I(0, 3);
            DiffCall("SayQuickMessage", [&] { h->SayQuickMessage(index); return 0; },
                     [&](void *) { OriginalAt<T_KInt>(0x005a46a0)(o_human, index); return 0; });
            break;
        }
        case 3: {
            const std::string text = P(0.1f) ? std::string() : std::string(size_t(I(1, 80)), char('a' + I(0, 25)));
            const char *arg = P(0.1f) ? nullptr : text.c_str();
            DiffCall("SetChatText", [&] { h->SetChatText(arg); return 0; },
                     [&](void *) { OriginalAt<T_DText>(0x005a9b30)(o_human, arg); return 0; });
            break;
        }
        case 4:
            DiffCall("OnDeath", [&] { h->OnDeath(); return 0; },
                     [&](void *) { OriginalAt<T_SVoid>(0x005a0220)(o_human); return 0; });
            break;
        case 5:
            DiffCall("ShowDeath", [&] { h->ShowDeath(); return 0; },
                     [&](void *) { OriginalAt<T_SVoid>(0x005a03e0)(o_human); return 0; });
            break;
        case 6: {
            // The killer a player often; the victim's sight target id at its edges.
            if (P(0.5f))
                h->m_role = HUMAN_ROLE_PLAYER;
            PortOther()->m_sightTargetId = P(0.3f) ? I(-1, 1) : I(-1, 20);
            std::memcpy(g_origOther + 0x25C, &PortOther()->m_sightTargetId, 4);
            // The seen lists (enemies, friends, traitors): the victim in none, one or several places, among
            // others (addresses only compared). The original's are MSVC6 vectors: an allocator word, then first,
            // last, end.
            static const uint32_t kListOffsets[3] = {0x264, 0x274, 0x294};
            std::vector<C_Human *> *portLists[3] = {&h->m_seenEnemies, &h->m_seenFriends, &h->m_seenTraitors};
            static void *origLists[3][6];
            size_t counts[3];
            for (int l = 0; l < 3; ++l) {
                portLists[l]->clear();
                counts[l] = size_t(P(0.15f) ? 0 : I(1, 6));
                for (size_t k = 0; k < counts[l]; ++k) {
                    const int pick = I(0, 3);
                    C_Human *port = pick == 0 ? PortOther()
                                  : pick == 1 ? h
                                              : reinterpret_cast<C_Human *>(uintptr_t(0x1000 * pick));
                    portLists[l]->push_back(port);
                    origLists[l][k] = pick == 0 ? static_cast<void *>(g_origOther)
                                    : pick == 1 ? static_cast<void *>(o_human)
                                                : static_cast<void *>(port);
                }
            }
            DiffCall("OnKilledHuman", [&] { h->OnKilledHuman(PortOther(), type); return 0; },
                     [&](void *) {
                         for (int l = 0; l < 3; ++l) {
                             void **words = reinterpret_cast<void **>(o_human + kListOffsets[l]);
                             words[0] = nullptr;
                             words[1] = origLists[l];
                             words[2] = origLists[l] + counts[l];
                             words[3] = origLists[l] + counts[l];
                         }
                         OriginalAt<T_DKilled>(0x005a0be0)(o_human, g_origOther, type);
                         for (int l = 0; l < 3; ++l)
                             std::memset(o_human + kListOffsets[l], 0, 16);
                         return 0;
                     });
            for (std::vector<C_Human *> *list : portLists)
                std::vector<C_Human *>().swap(*list);
            break;
        }
        case 7:
            DiffCall("KnockOut", [&] { h->KnockOut(PortOther()); return 0; },
                     [&](void *) { OriginalAt<T_KPtr>(0x005a62e0)(o_human, g_origOther); return 0; });
            break;
        default: {
            const int health = P(0.2f) ? I(-1, 1) : P(0.4f) ? I(-20, 0) : I(1, 900);
            const S_vector pos(U(-1, 1), U(0, 2), U(-1, 1)), dir(U(-1, 1), U(-1, 1), U(-1, 1));
            DiffCall("ApplyRemoteHit", [&] { h->ApplyRemoteHit(type, zone, health, killerId, pos, dir); return 0; },
                     [&](void *) {
                         OriginalAt<T_DRemote>(0x005a15d0)(o_human, type, zone, health, killerId, &pos, &dir);
                         return 0;
                     });
            break;
        }
        }
    }
}

// ---- part 21d: hits and collisions

// The original's calls Hit makes (the port's fakes are above): logged as the port's are.
int CT_ OH_GetCheatValue(int index)
{
    Log(nullptr, "GetCheatValue", {double(index)});
    return g_combat.cheat;
}
int CT_ OH_IsHumanSpeaking(const void *human)
{
    Log(human, "IsHumanSpeaking");
    return g_combat.speaking;
}
int CT_ OH_Say(void *self, int sentence, int delay, uint32_t flags, int force)
{
    Log(self, "ControllerSay", {double(sentence), double(delay), double(flags), double(B(force))});
    return 0;
}
void CT_ OH_BodyDie(void *, void *killer) { Log(killer, "BodyDie"); }
void CT_ OH_BodyCripple(void *) { Log(nullptr, "BodyCripple"); }
void CT_ OH_BodyRagdoll(void *) { Log(nullptr, "BodyRagdoll"); }
void CT_ OH_BodyResetMovement(void *) { Log(nullptr, "BodyResetMovement"); }
const S_vector *CT_ OH_GetViewDir(void *) { return &g_combat.viewDir; }
void CT_ OH_ScriptOnDeath(void *self) { Log(self, "ScriptOnDeath"); }
// The controller's OnHit and OnCollisionEx (both sides: the port's references are pointers).
void CT_ CT_OnHit(void *self, int type, int zone, int health, void *attacker, const S_vector *pos, const S_vector *dir)
{
    Log(self, ("ControllerOnHit " + NameOf(attacker)).c_str(),
        {double(type), double(zone), double(health), pos->x, pos->y, pos->z, dir->x, dir->y, dir->z});
}
bool g_collisionAnswer = false;
int CT_ CT_OnCollisionEx(void *self, void *other, const S_vector *pos, int a3, int a4, int a5, int a6, int a7)
{
    Log(self, ("ControllerOnCollisionEx " + NameOf(other)).c_str(),
        {pos->x, pos->y, pos->z, double(a3), double(a4), double(a5), double(a6), double(a7)});
    return g_collisionAnswer;
}
// The human's Hit: the real one on both sides (the port's slot calls the member).
void CT_ PH_Hit(C_Human *self, E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
                int zone)
{
    self->C_Human::Hit(type, amount, pos, dir, attacker, zone);
}

// The car that runs a human over: each side its own (the driver and the seats' occupants are each side's humans).
alignas(16) uint8_t g_portCar[sizeof(C_Car) + 0x100], g_origCar[0x2600];
void *g_portCarVt[128], *g_origCarVt[64];
size_t g_portSpeedOffset = 0;
int g_seatCount = 0;
int g_seats[4] = {};    // 0 none, 1 the tested human, 2 the other
int CT_ CAR_GetSeatCount(void *self)
{
    Log(self, "GetSeatCount");
    return g_seatCount;
}
void *CT_ CAR_GetSeatOccupant(void *self, int seat)
{
    Log(self, "GetSeatOccupant", {double(seat)});
    const bool port = self == g_portCar;
    const int who = seat >= 0 && seat < 4 ? g_seats[seat] : 0;
    if (who == 1)
        return port ? static_cast<void *>(g_h.human) : static_cast<void *>(o_human);
    if (who == 2)
        return port ? static_cast<void *>(g_portOther) : static_cast<void *>(g_origOther);
    return nullptr;
}

// The frame's data (its script's OnDeath), shared: both sides read the same frame.
alignas(16) uint8_t g_scriptBytes[0x40];
S_FrameData g_hitFrameData;

void SetUpHits()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00720f50, reinterpret_cast<const void *>(OH_GetCheatValue)},
        {0x0071f090, reinterpret_cast<const void *>(OH_IsHumanSpeaking)},
        {0x005882f0, reinterpret_cast<const void *>(OH_Say)},
        {0x005abbe0, reinterpret_cast<const void *>(OH_BodyDie)},
        {0x005ac370, reinterpret_cast<const void *>(OH_BodyCripple)},
        {0x005ac420, reinterpret_cast<const void *>(OH_BodyRagdoll)},
        {0x005ac440, reinterpret_cast<const void *>(OH_BodyResetMovement)},
        {0x005a80e0, reinterpret_cast<const void *>(OH_GetViewDir)},
        {0x0041d920, reinterpret_cast<const void *>(OH_ScriptOnDeath)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    p_humanVt[VSlot(&C_Actor::Hit)] = reinterpret_cast<void *>(PH_Hit);
    o_humanVt[0xD4 / 4] = OriginalAt<void *>(0x005a1c90);
    g_portControllerVt[VSlot(&C_HumanController::OnHit)] = reinterpret_cast<void *>(CT_OnHit);
    g_portControllerVt[VSlot(&C_HumanController::OnCollisionEx)] = reinterpret_cast<void *>(CT_OnCollisionEx);
    g_origControllerVt[0xBC / 4] = reinterpret_cast<void *>(CT_OnHit);
    g_origControllerVt[0xC8 / 4] = reinterpret_cast<void *>(CT_OnCollisionEx);
    // The original's integrity check passes (the port has none): its two table entries equal.
    *OriginalAt<uint16_t *>(0x008cac84) = 0;
    *OriginalAt<uint16_t *>(0x008cb384) = 0;
    *OriginalAt<uint32_t *>(0x008caf40) = *OriginalAt<uint32_t *>(0x008ca840);
    // The cars: one combined vtable each side would do, but the slots are per side anyway.
    for (void *&slot : g_portCarVt)
        slot = reinterpret_cast<void *>(P_Unexpected);
    for (void *&slot : g_origCarVt)
        slot = reinterpret_cast<void *>(P_Unexpected);
    g_portCarVt[VSlot(&C_Actor::GetSeatCount)] = reinterpret_cast<void *>(CAR_GetSeatCount);
    g_portCarVt[VSlot(&C_Actor::GetSeatOccupant)] = reinterpret_cast<void *>(CAR_GetSeatOccupant);
    g_origCarVt[0x88 / 4] = reinterpret_cast<void *>(CAR_GetSeatCount);
    g_origCarVt[0x78 / 4] = reinterpret_cast<void *>(CAR_GetSeatOccupant);
    // (C_Vehicle's m_speed is private, and the port's vehicle is not laid out as the original's: its offset found
    // through GetSpeed.)
    for (size_t off = 4; off + 4 <= sizeof(C_Car); off += 4) {
        std::memset(g_portCar, 0, sizeof g_portCar);
        const float marker = 1234.5f;
        std::memcpy(g_portCar + off, &marker, 4);
        if (reinterpret_cast<C_Car *>(g_portCar)->GetSpeed() == marker) {
            g_portSpeedOffset = off;
            break;
        }
    }
    std::memset(g_portCar, 0, sizeof g_portCar);
    CHECK(g_portSpeedOffset != 0);
    *reinterpret_cast<void ***>(g_portCar) = g_portCarVt;
    *reinterpret_cast<void ***>(g_origCar) = g_origCarVt;
    g_names[g_portCar] = g_names[g_origCar] = "car";
    g_names[g_scriptBytes] = "script";
}

void RandomHit()
{
    RandomDeath();
    C_Human *h = g_h.human;
    g_combat.cheat = P(0.3f) ? I(1, 2) : 0;
    g_self.difficulty = I(1, 4);
    const bool noTeamDamage = P(0.3f);
    GetGame()->SetNoTeamDamage(noTeamDamage);
    g_origGame[0x35] = noTeamDamage;
    g_mpSettings->friendlyFire = P(0.5f) ? 1 : 0;
    g_mpSettings->inverseDamage = P(0.3f) ? 0 : I(1, 100);
    g_mpSettings->spawnProtection = P(0.5f) ? 0 : I(1, 10);
    // The time since the spawn at the protection's edge, or anywhere.
    h->m_aliveTime = P(0.3f) ? g_mpSettings->spawnProtection * 1000 + I(-1, 1) : I(0, 20000);
    h->m_legHits = I(0, 3);
    if (P(0.15f))
        h->m_stats[STAT_HEALTH] = I(-5, 0);
    g_combat.viewDir = S_vector(U(-1, 1), U(-1, 1), U(-1, 1));
    // The frame's data: none, without a script, with one.
    const int data = I(0, 2);
    g_hitFrameData = S_FrameData();
    g_hitFrameData.script = data == 2 ? reinterpret_cast<C_Script *>(g_scriptBytes) : nullptr;
    g_h.frame->SetData(data ? &g_hitFrameData : nullptr);
}

typedef void(CT_ *T_HHit)(void *, int, float, const S_vector *, const S_vector *, void *, int);
typedef int(CT_ *T_HCollision)(void *, void *, const S_vector *, int, int, int, int, int);

void TestCombatHits()
{
    C_Human *h = g_h.human;
    I3D_frame *const frame = g_h.frame;
    S_FrameData *const frameData = frame->GetData();
    // (The name's rep each side's own, as in part 21c.)
    g_skipFields.insert("m_name");
    std::printf("combat hits\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomHit();
        static const int types[] = {HIT_FALL, HIT_BULLET, HIT_BULLET, HIT_BULLET, HIT_EXPLOSION, HIT_MELEE,
                                    HIT_SCRIPT_KILL, HIT_THROWN_OBJECT, HIT_BACKSTAB, HIT_SUICIDE, HIT_RUN_OVER,
                                    HIT_SCRIPT_KILL_EX, HIT_DROWNING, 9, 10, 15};
        const int type = types[I(0, 15)];
        const S_vector pos(U(-1, 1), U(0, 2), U(-1, 1));
        const S_vector dir = P(0.05f) ? S_vector(0, 0, 0) : S_vector(U(-1, 1), U(-1, 1), U(-1, 1));
        // The attacker: none, the tested human itself, the other one.
        const int who = P(0.2f) ? 0 : P(0.15f) ? 1 : 2;
        C_Actor *portAttacker = who == 0 ? nullptr
                              : who == 1 ? reinterpret_cast<C_Actor *>(h)
                                         : reinterpret_cast<C_Actor *>(g_portOther);
        void *origAttacker = who == 0 ? nullptr : who == 1 ? static_cast<void *>(o_human) : g_origOther;
        if (i % 3 != 2) {
            const float amount = P(0.15f) ? -1.0f : P(0.05f) ? 0.0f : P(0.5f) ? float(I(0, 300)) : U(0, 300);
            const int zone = I(-1, 14);
            DiffCall("Hit", [&] { h->C_Human::Hit(E_HitType(type), amount, pos, dir, portAttacker, zone); return 0; },
                     [&](void *) {
                         OriginalAt<T_HHit>(0x005a1c90)(o_human, type, amount, &pos, &dir, origAttacker, zone);
                         return 0;
                     });
        } else {
            // A car (or a static weapon) running into the human: its speed at 2 m/s, a NaN, any; its driver and
            // crew; the controller's answer; no controller.
            const bool car = P(0.8f);
            reinterpret_cast<C_Actor *>(g_portCar)->m_type = car ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
            const int carType = car ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
            std::memcpy(g_origCar + 0x18, &carType, 4);
            const float speed = P(0.2f) ? 2.0f : P(0.05f) ? std::numeric_limits<float>::quiet_NaN() : U(0, 5);
            std::memcpy(g_portCar + g_portSpeedOffset, &speed, 4);
            std::memcpy(g_origCar + 0x958, &speed, 4);
            reinterpret_cast<C_Car *>(g_portCar)->m_driver = reinterpret_cast<C_Human *>(portAttacker);
            std::memcpy(g_origCar + 0x24AC, &origAttacker, 4);
            g_seatCount = I(0, 4);
            for (int &seat : g_seats)
                seat = I(0, 2);
            g_collisionAnswer = P(0.5f);
            if (P(0.1f))
                h->m_controller = nullptr;
            const int a3 = I(0, 1000), a4 = I(0, 1000), a5 = I(-5, 5), a6 = I(-5, 5), a7 = I(-5, 5);
            DiffCall("OnCollisionEx",
                     [&] {
                         return int(h->C_Human::OnCollisionEx(reinterpret_cast<C_Actor *>(g_portCar), &pos, a3, a4, a5,
                                                              a6, a7));
                     },
                     [&](void *) {
                         return OriginalAt<T_HCollision>(0x005a07c0)(o_human, g_origCar, &pos, a3, a4, a5, a6, a7) & 0xFF;
                     });
        }
        // The other human (hit back, a crew member): its health, leg hits and disguise alike.
        CHECK(PortOther()->m_stats[STAT_HEALTH] == *reinterpret_cast<const int *>(g_origOther + 0xEC));
        CHECK(PortOther()->m_legHits == *reinterpret_cast<const int *>(g_origOther + 0x15C));
        CHECK(std::memcmp(&PortOther()->m_disguise, g_origOther + 0x16C, 4) == 0);
    }
    frame->SetData(frameData);
}

void TestCombat()
{
    SetUpCombat();
    SetUpMotion();
    SetUpDeaths();
    // COMBAT_ONLY: one part's tests alone (21a, 21b ...), for its mutation list.
    const char *only = std::getenv("COMBAT_ONLY");
    const auto run = [&](const char *part) { return !only || std::strcmp(only, part) == 0; };
    if (run("21a")) {
        TestCombatSides();
        TestCombatStats();
    }
    if (run("21b"))
        TestCombatMotion();
    if (run("21c"))
        TestCombatDeaths();
    if (run("21d")) {
        SetUpHits();
        TestCombatHits();
    }
}

}  // namespace
