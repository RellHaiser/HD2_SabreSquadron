// C_Human: damage and death, the multiplayer hit / kill messages, sides and hostility,
// disguise, visibility and the bones shots aim at (original source 0x005a0220..0x005a2740,
// 0x005a3ab0..0x005a3dd0 and smaller functions between 0x005a4600 and 0x005a8820).
#include "game/human.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/actors_module.h"
#include "game/game.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/interface.h"
#include "game/items.h"
#include "game/item_defs.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/surface_marks.h"
#include "game/world.h"
#include "script/script.h"

namespace {

// Game texts of the multiplayer messages (format arguments in the comments).
enum : int {
    TEXT_ZONE_HELMET = 0x2260,
    TEXT_ZONE_BODY = 0x2261,
    TEXT_ZONE_HEAD = 0x2262,
    TEXT_ZONE_LEFT_ARM = 0x2263,
    TEXT_ZONE_RIGHT_ARM = 0x2264,
    TEXT_ZONE_LEFT_LEG = 0x2265,
    TEXT_ZONE_RIGHT_LEG = 0x2266,
    TEXT_HIT_TEAMMATE = 0x2292,             // victim, zone
    TEXT_HIT_ENEMY = 0x2293,                // victim, zone
    TEXT_DIED_LOCAL = 0x2296,               // (the local player)
    TEXT_PLAYER_DIED = 0x2297,              // victim
    TEXT_KILLED_TEAMMATE = 0x2298,          // victim (the local player killed him)
    TEXT_KILLED_ENEMY_HEADSHOT = 0x2299,    // victim
    TEXT_KILLED_ENEMY = 0x229A,             // victim
    TEXT_TEAMKILL = 0x229B,                 // killer, victim
    TEXT_KILL_HEADSHOT = 0x229C,            // killer, victim
    TEXT_KILL = 0x229D,                     // killer, victim
    TEXT_DIED = 0x229E,                     // victim
    TEXT_SUICIDE = 0x22A3,                  // victim
    TEXT_SUICIDE_LOCAL = 0x22A4,            // (the local player)
};

// Speech sentences of the speech sets played on hits.
enum : int {
    SPEECH_DEATH = 0x73,
    SPEECH_DEATH_BACKSTAB = 0x74,
    SPEECH_PAIN = 0x75,
};

// Body flags of a hit (S_HumanBody::ApplyHit): the zone ...
enum : uint32_t {
    HIT_FLAG_HEAD = 0x01,
    HIT_FLAG_LEFT_ARM = 0x02,
    HIT_FLAG_RIGHT_ARM = 0x04,
    HIT_FLAG_BODY = 0x08,
    HIT_FLAG_LEFT_LEG = 0x10,
    HIT_FLAG_RIGHT_LEG = 0x20,
    HIT_FLAG_LEGS = HIT_FLAG_LEFT_LEG | HIT_FLAG_RIGHT_LEG,
};

// ... and the kind of damage.
uint32_t HitTypeFlags(int type)
{
    switch (type) {
    case 0: return 0x1000;
    case 9: return 0x800;
    case 10: return 0x400;
    case HIT_BACKSTAB: return 0x2000;
    case HIT_RUN_OVER: return 0x4000;
    case 15: return 0x8000;
    default: return 0;
    }
}

int ZoneTextId(int zone)
{
    switch (zone) {
    case HIT_ZONE_BODY: return TEXT_ZONE_BODY;
    case HIT_ZONE_HEAD: return TEXT_ZONE_HEAD;
    case HIT_ZONE_LEFT_ARM_UPPER:
    case HIT_ZONE_LEFT_ARM_LOWER: return TEXT_ZONE_LEFT_ARM;
    case HIT_ZONE_RIGHT_ARM_UPPER:
    case HIT_ZONE_RIGHT_ARM_LOWER: return TEXT_ZONE_RIGHT_ARM;
    case HIT_ZONE_LEFT_LEG_UPPER:
    case HIT_ZONE_LEFT_LEG_LOWER: return TEXT_ZONE_LEFT_LEG;
    case HIT_ZONE_RIGHT_LEG_UPPER:
    case HIT_ZONE_RIGHT_LEG_LOWER: return TEXT_ZONE_RIGHT_LEG;
    case HIT_ZONE_HELMET: return TEXT_ZONE_HELMET;
    default: return -1;
    }
}

// Message colours (0..255).
const S_vector COLOR_YELLOW(255.0f, 255.0f, 0.0f);
const S_vector COLOR_WHITE(255.0f, 255.0f, 255.0f);
const S_vector COLOR_RED(255.0f, 0.0f, 0.0f);
const S_vector COLOR_BLUE(0.0f, 0.0f, 255.0f);
const S_vector COLOR_DARK_RED(160.0f, 0.0f, 0.0f);
const S_vector COLOR_DARK_BLUE(0.0f, 0.0f, 160.0f);
const S_vector COLOR_GREEN(0.0f, 255.0f, 0.0f);
const S_vector COLOR_DARK_GREEN(0.0f, 160.0f, 0.0f);
const S_vector COLOR_CHAT(50.0f, 150.0f, 50.0f);

// Axis messages in red, allied ones in blue.
const S_vector &TeamColor(int team, bool dark)
{
    if (team == TEAM_AXIS)
        return dark ? COLOR_DARK_RED : COLOR_RED;
    return dark ? COLOR_DARK_BLUE : COLOR_BLUE;
}

// Bones of the "a" body visual by hit-zone index (GetRandomBonePos()).
const int RANDOM_BONES[17] = {0x16, 0x15, 0x14, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0B, 0x0A, 0x09, 0x0C, 0x08, 0x03, 0x01, 0x18};

}  // namespace

// ---------------------------------------------------------------------------- messages

void C_Human::PrintText(const C_Human * /*human*/, const S_vector &color, int textId, ...)
{
    char format[512];
    std::strcpy(format, g_texts.Get(textId));
    char text[1024];
    va_list args;
    va_start(args, textId);
    std::vsprintf(text, format, args);
    va_end(args);
    GetGame()->PrintMessage(color, text);
}

void C_Human::PrintNamedText(const C_Human *human, const S_vector & /*color*/, int textId, ...)
{
    char format[512];
    std::strcpy(format, g_texts.Get(textId));
    char text[1024];
    va_list args;
    va_start(args, textId);
    std::vsprintf(text, format, args);
    va_end(args);
    char named[1024];
    std::sprintf(named, "%s: %s", human->GetShortName(), text);
    // (The original prints the text without the name, always in the chat colour.)
    GetGame()->PrintMessage(COLOR_CHAT, text);
}

void C_Human::SayQuickMessage(int index)
{
    if (m_role != HUMAN_ROLE_PLAYER)
        return;
    if (!IsLocalGame() && GetTeam(false) != g_netGame.GetLocalTeam())
        return;
    const char *text = GetGame()->GetQuickMessages()->GetText(index);
    if (!text)
        return;
    char line[1024];
    std::sprintf(line, "%s: %s", GetShortName(), text);
    GetGame()->PrintMessage(COLOR_CHAT, line);
    SetChatText(text);
}

void C_Human::SetChatText(const char *text)
{
    if (IsLocalGame() || !IsClient())
        return;
    if (text) {
        char line[2048];
        std::sprintf(line, "%s: %s", GetPlayerName(), text);
        if (std::strlen(line) <= sizeof(m_chatText) - 1) {
            std::strcpy(m_chatText, line);
        } else {
            std::strncpy(m_chatText, line, 60);
            m_chatText[60] = '\0';
            std::strcat(m_chatText, "...");
        }
    }
    m_chatTime = 10000;
}

void C_Human::ShowHitMessage(int type, int zone, int killerId)
{
    if (IsLocalGame() || type == HIT_SCRIPT_KILL || type == 0)
        return;
    if (type == HIT_THROWN_OBJECT && m_stats[STAT_HEALTH] > 0)
        return;

    C_str zoneText;
    const int zoneTextId = ZoneTextId(zone);
    if (zoneTextId != -1)
        zoneText = g_texts.Get(zoneTextId);
    else
        zoneText = "Unknow damage zone !!!!";

    C_Human *killer = static_cast<C_Human *>(g_actorsModule.GetHuman(killerId));
    const bool teamGame = g_mpSettings->gameType != MP_GAME_DEATHMATCH;
    if (m_stats[STAT_HEALTH] > 0) {
        // Wounded: only the shooter is told.
        if (!killer || !killer->IsLocalPlayer())
            return;
        if (g_netGame.GetLocalTeam() == GetTeam(false) && teamGame)
            PrintText(this, COLOR_YELLOW, TEXT_HIT_TEAMMATE, GetName()->c_str(), zoneText.c_str());
        else
            PrintText(this, COLOR_DARK_GREEN, TEXT_HIT_ENEMY, GetName()->c_str(), zoneText.c_str());
        return;
    }
    if (!killer) {
        PrintText(this, COLOR_WHITE, TEXT_DIED, GetName()->c_str());
        return;
    }
    const int killerTeam = killer->GetTeam(false);
    if (killer == this) {
        const int text = type == HIT_SUICIDE ? TEXT_SUICIDE_LOCAL : TEXT_DIED_LOCAL;
        const int otherText = type == HIT_SUICIDE ? TEXT_SUICIDE : TEXT_PLAYER_DIED;
        if (IsLocalPlayer())
            PrintText(this, COLOR_YELLOW, text);
        else
            PrintText(this, TeamColor(killerTeam, false), otherText, GetName()->c_str());
        return;
    }
    if (killer->IsLocalPlayer()) {
        if (g_netGame.GetLocalTeam() == GetTeam(false) && teamGame)
            PrintText(this, COLOR_YELLOW, TEXT_KILLED_TEAMMATE, GetName()->c_str());
        else
            PrintText(this, COLOR_GREEN, zone == HIT_ZONE_HEAD ? TEXT_KILLED_ENEMY_HEADSHOT : TEXT_KILLED_ENEMY,
                      GetName()->c_str());
        return;
    }
    const char *victimName = GetName()->c_str();
    const char *killerName = killer->GetName()->c_str();
    if (killerTeam == GetTeam(false) && teamGame)
        PrintText(this, COLOR_YELLOW, TEXT_TEAMKILL, killerName, victimName);
    else if (zone == HIT_ZONE_HEAD)
        PrintText(this, TeamColor(killerTeam, false), TEXT_KILL_HEADSHOT, killerName, victimName);
    else
        PrintText(this, TeamColor(killerTeam, true), TEXT_KILL, killerName, victimName);
}

// ------------------------------------------------------------------------------ damage

void C_Human::Hit(E_HitType hitType, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attackerActor,
                  int zone)
{
    const int type = hitType;
    // (Attackers are humans or null.)
    C_Human *attacker = static_cast<C_Human *>(attackerActor);
    if (IsLocalGame()) {
        // "sniper" console command: enemies die from any hit.
        if (GetCheatValue(CHEAT_HYDROSHOCK) != 0 && g_gameProfile.GetDifficulty() != 4 && m_role == HUMAN_ROLE_ENEMY)
            amount = -1.0f;
        if (GetGame()->IsNoTeamDamage() && g_teamManager.GetPlayerTeam()->IsMember(this))
            return;
    }
    if (type == HIT_SCRIPT_KILL_EX) {
        m_stats[STAT_HEALTH] = 0;
        m_controller->OnHit(type, zone, 0, attacker, pos, S_vector(0.0f, 1.0f, 0.0f));
        m_body.Ragdoll();
        m_body.ResetMovement();
        return;
    }
    const S_vector hitDir = dir.Normalized();
    int reflectPercent = 0;
    if (!IsLocalGame()) {
        if (m_stats[STAT_HEALTH] > 0 && this != attacker && g_mpSettings->gameType != MP_GAME_DEATHMATCH && attacker &&
            attacker->GetTeam(false) == GetTeam(false)) {
            // Team damage: either the shooter takes a share of it or there is none (run
            // over and thrown objects excepted).
            if (attacker->m_role == HUMAN_ROLE_PLAYER && g_mpSettings->friendlyFire)
                reflectPercent = g_mpSettings->inverseDamage;
            else if (type != HIT_RUN_OVER && type != HIT_THROWN_OBJECT)
                return;
        }
        // Spawn protection.
        if (m_role == HUMAN_ROLE_PLAYER && m_aliveTime < g_mpSettings->spawnProtection * 1000 && type != HIT_SUICIDE)
            return;
    } else if (attacker && attacker->IsLocalPlayer() && !CopyProtectionIntact() &&
               static_cast<float>(std::rand()) < 327.67f) {
        // A failed integrity check (copy protection, inferred): now and then (1%) the
        // player's own shots hurt him.
        const S_vector back = -attacker->GetViewDir();
        attacker->Hit(hitType, amount, attacker->GetRandomBonePos(), back, attacker, zone);
    }
    if ((IsLocalGame() || g_gameProfile.IsCooperativeGame()) && amount != -1.0f && type == HIT_BULLET) {
        const int difficulty = g_gameProfile.GetDifficulty();
        if (m_role == HUMAN_ROLE_PLAYER || m_role == HUMAN_ROLE_NPC_TEAM) {
            if (difficulty == 1)
                amount *= 0.2f;
            else if (difficulty == 2)
                amount *= 0.4f;
            else if (difficulty == 4)
                amount *= 1.2f;
        } else if (difficulty == 1 && attacker && attacker->m_role == HUMAN_ROLE_PLAYER) {
            amount *= 1.2f;
        }
    }

    int material = GetMaterialId(nullptr);
    int damage;
    uint32_t flags;
    switch (zone) {
    case HIT_ZONE_BODY:
        damage = static_cast<int>(amount);
        flags = HIT_FLAG_BODY;
        break;
    case HIT_ZONE_HEAD:
        damage = static_cast<int>(amount * 5.0f);
        flags = HIT_FLAG_HEAD;
        break;
    case HIT_ZONE_LEFT_ARM_UPPER:
    case HIT_ZONE_LEFT_ARM_LOWER:
        damage = static_cast<int>(amount * 0.2);
        flags = HIT_FLAG_LEFT_ARM;
        break;
    case HIT_ZONE_RIGHT_ARM_UPPER:
    case HIT_ZONE_RIGHT_ARM_LOWER:
        damage = static_cast<int>(amount * 0.2);
        flags = HIT_FLAG_RIGHT_ARM;
        break;
    case HIT_ZONE_LEFT_LEG_UPPER:
    case HIT_ZONE_LEFT_LEG_LOWER:
        damage = static_cast<int>(amount * 0.33);
        flags = HIT_FLAG_LEFT_LEG;
        break;
    case HIT_ZONE_RIGHT_LEG_UPPER:
    case HIT_ZONE_RIGHT_LEG_LOWER:
        damage = static_cast<int>(amount * 0.33);
        flags = HIT_FLAG_RIGHT_LEG;
        break;
    case HIT_ZONE_HELMET: {
        // The helmet stops most shots; 30% go through as a head shot (not for the player's
        // soldiers on the easiest difficulty).
        float value;
        if (RandFloat() < 0.3f) {
            if ((IsLocalGame() || g_gameProfile.IsCooperativeGame()) && m_role == HUMAN_ROLE_PLAYER &&
                g_gameProfile.GetDifficulty() == 1)
                value = amount;
            else
                value = amount * 5.0f;
        } else {
            value = amount * 0.2f;
        }
        damage = static_cast<int>(value);
        material = HELMET_MATERIAL;
        flags = HIT_FLAG_HEAD;
        break;
    }
    default:
        damage = 0;
        flags = HIT_FLAG_BODY;
        break;
    }

    if (IsClient() && type != HIT_MELEE) {
        if (type == HIT_BULLET) {
            g_shotManager.PlayHitSound(this, material, pos, false, !IsLocalGame());
            g_shotManager.SprayBlood(pos, hitDir);
            g_shotManager.PlayHitEffect(material, pos, -dir.Normalized());
        } else if (type == HIT_EXPLOSION) {
            g_shotManager.PlayHitSound(this, material, pos, false, !IsLocalGame());
        }
    }
    if (m_stats[STAT_HEALTH] <= 0) {
        // Shots into a body only push it.
        m_body.ApplyHit(flags, pos, hitDir * amount);
        return;
    }
    if (type != HIT_THROWN_OBJECT) {
        flags |= HitTypeFlags(type);
        const S_vector force = amount == -1.0f ? dir * 1000.0f : dir * amount;
        m_body.ApplyHit(flags, pos, force);
    }
    if (amount == -1.0f)
        m_stats[STAT_HEALTH] = 0;
    else
        m_stats[STAT_HEALTH] -= damage;
    // Three leg hits cripple everybody but the players.
    if (m_role != HUMAN_ROLE_PLAYER && (flags & HIT_FLAG_LEGS) != 0 && m_legHits < 3) {
        if (++m_legHits == 3)
            m_body.Cripple();
    }
    if (m_stats[STAT_HEALTH] <= 0) {
        m_stats[STAT_HEALTH] = 0;
        m_body.Die(attacker);
        if (attacker)
            attacker->OnKilledHuman(this, type);
        const S_FrameData *data = m_frame->GetData();
        if (data && data->script)
            data->script->OnDeath();
        OnDeath();
        if (!IsLocalGame())
            g_netGame.OnPlayerKilled(m_netId, attacker ? attacker->m_netId : -1);
    }
    if (!IsClient() && attacker)
        ShowHitMessage(type, zone, attacker->m_netId);
    m_controller->OnHit(type, zone, m_stats[STAT_HEALTH], attacker, pos, hitDir);
    if (type != 10) {
        if (m_stats[STAT_HEALTH] > 0) {
            if (!IsHumanSpeaking(this))
                m_controller->Say(SPEECH_PAIN, 200, 0x20, false);
        } else {
            StopFrameSpeech(m_frame);
            if (type == HIT_BACKSTAB)
                m_controller->Say(SPEECH_DEATH_BACKSTAB, 200, 1, false);
            else if (zone != HIT_ZONE_HEAD)
                m_controller->Say(SPEECH_DEATH, 200, 0x20, false);
        }
    }
    if (reflectPercent > 0) {
        const S_vector back = -attacker->GetViewDir();
        attacker->Hit(hitType, static_cast<float>(reflectPercent) * 0.01f * amount, attacker->GetRandomBonePos(), back,
                      attacker, zone);
    }
}

void C_Human::ApplyRemoteHit(int type, int zone, int health, int killerId, const S_vector &pos, const S_vector &dir)
{
    if (type == HIT_SCRIPT_KILL_EX) {
        m_stats[STAT_HEALTH] = health;
        if (IsLocalGame() || health > 0)
            return;
        // The body falls in a random direction.
        const float a = (RandFloat() - 0.5f) * 2.0f;
        const float b = (RandFloat() - 0.5f) * 2.0f;
        const float c = (RandFloat() - 0.5f) * 2.0f;
        const float strength = RandFloat() * 10.0f + 1.0f;
        m_body.ApplyHit(HIT_FLAG_BODY, m_bodyFrame->GetWorldPos(), S_vector(b, a, c) * strength);
        ShowDeath();
        return;
    }
    const float damage = static_cast<float>(m_stats[STAT_HEALTH] - health);
    if (IsServer()) {
        if (m_stats[STAT_HEALTH] <= 0)
            ShowDeath();
        ShowHitMessage(type, zone, killerId);
        return;
    }
    m_stats[STAT_HEALTH] = health;
    int material = GetMaterialId(nullptr);
    const S_vector hitPos = m_frame->GetWorldPos() + pos;
    uint32_t flags = HIT_FLAG_BODY;
    switch (zone) {
    case HIT_ZONE_HEAD:
        flags = HIT_FLAG_HEAD;
        break;
    case HIT_ZONE_LEFT_ARM_UPPER:
    case HIT_ZONE_LEFT_ARM_LOWER:
    case HIT_ZONE_RIGHT_ARM_UPPER:
    case HIT_ZONE_RIGHT_ARM_LOWER:
        // (Both arms report the left one here.)
        flags = HIT_FLAG_LEFT_ARM;
        break;
    case HIT_ZONE_LEFT_LEG_UPPER:
    case HIT_ZONE_LEFT_LEG_LOWER:
        flags = HIT_FLAG_LEFT_LEG;
        break;
    case HIT_ZONE_RIGHT_LEG_UPPER:
    case HIT_ZONE_RIGHT_LEG_LOWER:
        flags = HIT_FLAG_RIGHT_LEG;
        break;
    case HIT_ZONE_HELMET:
        flags = HIT_FLAG_HEAD;
        material = HELMET_MATERIAL;
        break;
    default:
        break;
    }
    if (type != HIT_THROWN_OBJECT) {
        flags |= HitTypeFlags(type);
        // (The body gets the position relative to the human, as it was sent.)
        m_body.ApplyHit(flags, pos, dir.Normalized() * damage);
    }
    if (m_stats[STAT_HEALTH] <= 0) {
        if (m_role == HUMAN_ROLE_PLAYER && !IsLocalGame())
            g_netGame.OnPlayerKilled(m_netId, killerId);
        ShowDeath();
    }
    if (IsLocalPlayer()) {
        if (GetGame()->IsFirstPersonView())
            GetGame()->GetInterface()->FlashDamage();
        static_cast<C_PlayerController *>(m_controller)->OnDamaged(type, zone, dir);
    }
    if (type == HIT_BULLET) {
        g_shotManager.PlayHitSound(this, material, hitPos, false, true);
        g_shotManager.SprayBlood(hitPos, dir);
        g_shotManager.PlayHitEffect(material, hitPos, -dir.Normalized());
    } else if (type == HIT_MELEE) {
        g_shotManager.PlayHitSound(this, material, hitPos, false, true);
        g_shotManager.PlayHitEffect(material, hitPos, -dir.Normalized());
    } else if (type == HIT_EXPLOSION) {
        g_shotManager.PlayHitSound(this, material, hitPos, false, true);
    }
    ShowHitMessage(type, zone, killerId);
}

void C_Human::OnDeath()
{
    m_bleeding = 0.0f;
    m_deadTime = 0;
    m_controller->OnDeath();
    m_body.PlayDeath();
    if (IsLocalGame() || g_gameProfile.IsCooperativeGame()) {
        PeriodicLinks_SetRate(m_periodicLink, 1);
        if (m_role == HUMAN_ROLE_ENEMY) {
            if (IsLocalGame())
                g_teamManager.GetPlayerTeam()->GetEnemyRecords().Add(this);
            else
                g_teamManager.GetCoopTeam()->GetEnemyRecords().Add(this);
        }
        if (m_sightId != -1) {
            g_sightModule.SetObserverState(m_sightId, SIGHT_OBSERVER_OFF);
            g_sightModule.SetTargetState(m_sightTargetId, SIGHT_TARGET_DEAD);
        }
        if (m_soundListener != -1) {
            GetSoundModule()->RemoveListener(m_soundListener);
            m_soundListener = -1;
        }
    }
    if (m_role == HUMAN_ROLE_PLAYER || m_role == HUMAN_ROLE_NPC_TEAM) {
        if (IsLocalGame()) {
            if (g_teamManager.GetPlayerTeam()->IsMember(this)) {
                g_teamManager.GetPlayerTeam()->OnMemberDied(this);
                GetSoundModule()->PlayMusic(MUSIC_SOLDIER_DIED);
            }
        } else if (g_gameProfile.IsCooperativeGame()) {
            g_teamManager.GetCoopTeam()->OnMemberDied(this);
        }
    }
    // A dead driver lets go of the controls.
    if (m_vehicle && m_vehicle->GetType() == ACTOR_CAR) {
        C_Vehicle *vehicle = static_cast<C_Car *>(m_vehicle)->GetVehicle();
        vehicle->SetThrottle(0.0f, true);
        vehicle->SetSteering(0.0f);
        vehicle->SetBrake(1.0f);
        vehicle->SetHandbrake(true, true);
        vehicle->SetUnknown4f23b0(0.0f);
    }
    if (m_formation)
        m_formation->OnLeaderDied();
    DisbandFormation();
}

void C_Human::ShowDeath()
{
    m_controller->OnKilled();
    if (IsServer())
        return;
    StopFrameSpeech(m_frame);
    m_bleeding = 0.0f;
    m_deadTime = 0;
    m_body.PlayDeath();
}

void C_Human::OnKilledHuman(C_Human *victim, int type)
{
    if (!IsLocalGame())
        return;
    if (m_role == HUMAN_ROLE_PLAYER) {
        int experience;
        if (!IsHostileTo(victim) || victim->IsUnconscious()) {
            experience = -1000;
        } else if (victim->IsSurrendered()) {
            experience = -500;
        } else {
            if (type == HIT_BULLET)
                AddShooting(0.04f);
            if (victim->m_controller->GetType() == HUMAN_CONTROLLER_AI) {
                AddStealth(0.03f);
                experience = 300;
            } else if (victim->HasSeenEnemy(this) ||
                       static_cast<float>(victim->m_controller->GetQuietTime()) <= 60000.0f) {
                experience = 100;
            } else {
                // Killed without being noticed.
                AddStealth(0.02f);
                experience = 300;
            }
        }
        AddExperience(experience);
    }
    // The victim's sight target is evaluated again (it is a body now).
    int targetId = -1;
    if (IsHostileTo(victim)) {
        for (size_t i = m_seenEnemies.size(); i-- > 0;) {
            if (m_seenEnemies[i] == victim) {
                targetId = m_seenEnemies[i]->m_sightTargetId;
                break;
            }
        }
        m_controller->OnKilledEnemy(victim);
    } else {
        const std::vector<C_Human *> &list = IsDisguisedEnemy(victim) ? m_seenTraitors : m_seenFriends;
        if (list.empty())
            return;
        // (No break: the first entry in the list wins.)
        for (size_t i = list.size(); i-- > 0;) {
            if (list[i] == victim)
                targetId = list[i]->m_sightTargetId;
        }
    }
    if (targetId >= 0) {
        OnSightEvent(1, targetId);
        OnSightEvent(0, targetId);
    }
}

void C_Human::KnockOut(C_Human *by)
{
    m_controller->OnKnockedOut();
    by->AddExperience(500);
    if (m_sightId != -1)
        g_sightModule.SetObserverState(m_sightId, SIGHT_OBSERVER_OFF);
    if (m_sightTargetId != -1)
        g_sightModule.SetTargetState(m_sightTargetId, SIGHT_TARGET_DEAD);
    if (m_soundListener != -1) {
        GetSoundModule()->RemoveListener(m_soundListener);
        m_soundListener = -1;
    }
}

bool C_Human::OnCollisionEx(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6, int a7)
{
    if (!m_controller)
        return false;
    // Run over by a car faster than 2 m/s: killed by its driver.
    if (IsServer() && other->GetType() == ACTOR_CAR && !(static_cast<C_Car *>(other)->GetSpeed() <= 2.0f)) {
        if (IsLocalGame()) {
            // The car's crew loses its disguise.
            for (int seat = other->GetSeatCount(); seat-- > 0;) {
                if (C_Actor *occupant = other->GetSeatOccupant(seat))
                    static_cast<C_Human *>(occupant)->SetDisguise(-30.0f);
            }
        }
        Hit(static_cast<E_HitType>(HIT_RUN_OVER), -1.0f, *pos, reinterpret_cast<const S_vector &>(m_direction),
            static_cast<C_Car *>(other)->GetDriver(), 0);
    }
    return m_controller->OnCollisionEx(other, pos, a3, a4, a5, a6, a7);
}

// --------------------------------------------------------------------- bleeding / motion

void C_Human::TickBleeding(int timeDelta)
{
    if (m_bleeding == 0.0f)
        return;
    m_bleeding -= static_cast<float>(timeDelta) * 0.001f;
    if (m_bleeding < 0.0f)
        m_bleeding = 0.0f;
}

void C_Human::UpdateBleeding(int frameTime)
{
    if (m_bleeding == 0.0f)
        return;
    if (!IsClient()) {
        m_bleeding -= static_cast<float>(frameTime) * 0.001f;
        if (m_bleeding < 0.0f)
            m_bleeding = 0.0f;
    }
    // Drops of blood: every 1.5 s while moving, every 2.5 s standing.
    m_bloodDropTime += frameTime;
    const bool moving = GetVelocity().Square() >= 1e-16f;
    if (moving ? m_bloodDropTime <= 1500 : m_bloodDropTime <= 2500)
        return;
    m_bloodDropTime = 0;
    const S_vector pos = m_frame->GetWorldPos() + S_vector(0.0f, 1.0f, 0.0f);
    const float a = RandFloat();
    const float b = RandFloat();
    const float spread = moving ? 0.1f : 0.25f;
    const S_vector dir(((a - 0.5f) + (a - 0.5f)) * spread, -0.99f, (b - 0.5f) * spread * 2.0f);
    g_shotManager.SprayBloodRandom(pos, dir);
}

void C_Human::UpdateVelocity(int timeDelta)
{
    if (m_vehicle) {
        m_velocity.x = m_velocity.y = m_velocity.z = 0.0f;
        m_prevVelocity = m_velocity;
        return;
    }
    m_prevVelocity = m_velocity;
    m_velocity = m_lastPos;
    const S_matrix &world = m_frame->GetMatrix();
    m_lastPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_velocity.x -= m_lastPos.x;
    m_velocity.y -= m_lastPos.y;
    m_velocity.z -= m_lastPos.z;
    if (GetVelocity().Square() >= 1e-16f) {
        const float scale = -1000.0f / static_cast<float>(timeDelta);
        m_velocity.x *= scale;
        m_velocity.y *= scale;
        m_velocity.z *= scale;
    }
}

void C_Human::NotifyShotAt(C_Actor *shooter, const S_vector &velocity)
{
    if ((m_controller->GetMoveFlags() & MOVE_DEAF) == 0 && !IsLocalPlayer())
        m_controller->OnShotAt(shooter, velocity);
}

// ------------------------------------------------------------------------ sides / teams

bool C_Human::IsLocalPlayer() const
{
    if (!m_controller)
        return false;
    if (IsLocalGame())
        return m_controller->GetType() == HUMAN_CONTROLLER_PLAYER;
    return m_netId == g_localPlayerSlot;
}

bool C_Human::IsServerSimulated() const
{
    return IsServer() && m_role != HUMAN_ROLE_PLAYER;
}

bool C_Human::IsFriendOfInNetGame(const C_Human *other) const
{
    if (g_mpSettings->gameType == MP_GAME_DEATHMATCH)
        return false;
    if (!g_gameProfile.IsCooperativeGame())
        return other->GetTeam(false) == GetTeam(false);
    if (m_role == HUMAN_ROLE_PLAYER && other->m_role == HUMAN_ROLE_ENEMY)
        return false;
    if (m_role == HUMAN_ROLE_ENEMY && other->m_role == HUMAN_ROLE_PLAYER)
        return false;
    const int side = GetSide();
    const int otherSide = other->GetSide();
    if (side != SIDE_NEUTRAL && otherSide != SIDE_NEUTRAL)
        return side == otherSide;
    return true;
}

bool C_Human::IsFriendOf(const C_Actor *otherActor) const
{
    if (!IsLocalGame()) {
        // (Network games only meet humans here.)
        return IsFriendOfInNetGame(static_cast<const C_Human *>(otherActor));
    }
    if (otherActor->GetType() != ACTOR_HUMAN) {
        // MeleeTrackers are friends of everybody but the player's soldiers.
        return otherActor->GetType() == ACTOR_MELEE_TRACKER && m_role != HUMAN_ROLE_PLAYER;
    }
    const C_Human *other = static_cast<const C_Human *>(otherActor);
    C_Team *team = g_teamManager.GetPlayerTeam();
    if (team->IsMember(this)) {
        if (team->IsMember(other))
            return true;
        if (other->m_role == HUMAN_ROLE_NPC_TEAM_5)
            return true;
        if (other->m_role == HUMAN_ROLE_ENEMY)
            return false;
    }
    if (m_hostileToPlayers && team->IsMember(other))
        return false;
    const auto alliedRole = [](E_HumanRole role) {
        return role == HUMAN_ROLE_NEUTRAL || role == HUMAN_ROLE_NPC_TEAM || role == HUMAN_ROLE_PLAYER;
    };
    const auto axisRole = [](E_HumanRole role) { return role == HUMAN_ROLE_ENEMY || role == HUMAN_ROLE_NPC_TEAM_5; };
    if ((alliedRole(m_role) && alliedRole(other->m_role)) || (axisRole(m_role) && axisRole(other->m_role)))
        return true;
    const int side = GetSide();
    const int otherSide = other->GetSide();
    if (side != SIDE_NEUTRAL && otherSide != SIDE_NEUTRAL)
        return side == otherSide;
    return true;
}

bool C_Human::IsDisguisedEnemy(const C_Human *other) const
{
    if (m_role == HUMAN_ROLE_PLAYER && other->m_role == HUMAN_ROLE_ENEMY && IsFriendOf(other))
        return true;
    if (m_role == HUMAN_ROLE_ENEMY && other->m_role == HUMAN_ROLE_PLAYER && IsFriendOf(other))
        return true;
    return false;
}

int C_Human::GetSide() const
{
    C_Team *team = g_teamManager.GetPlayerTeam();
    if (!GetGame()->IsUniformDisguise()) {
        if (team->IsMember(this))
            return SIDE_ALLIES;
    } else {
        if (!g_gameProfile.IsCooperativeGame()) {
            // Uniforms disguise the player's soldiers until the disguise is gone.
            if (team->IsMember(this) && m_disguise <= 0.0f)
                return SIDE_ALLIES;
        } else if (m_role == HUMAN_ROLE_PLAYER) {
            return SIDE_ALLIES;
        }
        if (C_Item *uniform = GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem())
            return uniform->GetDef()->GetTeam();
    }
    if (m_role == HUMAN_ROLE_ENEMY || m_role == HUMAN_ROLE_NPC_TEAM_5)
        return SIDE_AXIS;
    return m_role == HUMAN_ROLE_NPC_TEAM ? SIDE_ALLIES : SIDE_NEUTRAL;
}

int C_Human::GetTeam(bool pending) const
{
    E_HumanRole role;
    if (IsLocalGame()) {
        role = m_role;
        if (role == HUMAN_ROLE_ENEMY)
            return TEAM_AXIS;
    } else {
        if (!g_gameProfile.IsCooperativeGame())
            return g_netGame.GetPlayerTeam(m_netId, pending);
        role = m_role;
        if (role == HUMAN_ROLE_ENEMY || role == HUMAN_ROLE_NPC_TEAM_5)
            return TEAM_AXIS;
    }
    if (role == HUMAN_ROLE_PLAYER || role == HUMAN_ROLE_NPC_TEAM)
        return TEAM_ALLIES;
    return TEAM_NONE;
}

void C_Human::SetPlayerTeam(int team, bool pending)
{
    g_netGame.SetPlayerTeam(m_netId, team, pending);
}

// --------------------------------------------------------------------------- disguise

void C_Human::UpdateDisguise(float amount)
{
    if (!IsLocalGame() || !GetGame()->IsUniformDisguise()) {
        m_disguise = 0.0f;
        return;
    }
    if (m_carried) {
        m_disguise = 0.0f;
        return;
    }
    // Anything of the allies on the body gives the soldier away.
    static const int WORN[] = {ITEM_CONTAINER_HEAD, ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_HANDS,
                               ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_BACKPACK};
    for (int container : WORN) {
        C_Item *item = GetContainer(container)->GetItem();
        if (item && item->GetDef()->GetTeam() == SIDE_ALLIES) {
            m_disguise = 0.0f;
            return;
        }
    }
    m_disguise += amount;
    if (m_disguise > 100.0f)
        m_disguise = 100.0f;
}

void C_Human::SetDisguise(float value)
{
    m_disguise = IsLocalGame() ? value : 0.0f;
}

int C_Human::GetDisguisePercent() const
{
    const int percent = static_cast<int>(m_disguise);
    if (percent > 100)
        return 100;
    return percent < 0 ? 0 : percent;
}

// ------------------------------------------------------------------------------ stats

int C_Human::GetStat(int stat) const
{
    if (stat >= STAT_HEALTH && stat <= STAT_SIGHT_RANGE)
        return m_stats[stat];
    if (stat == STAT_SPEECH_SET)
        return m_stats[STAT_HEARING_RANGE];
    return 0;
}

float C_Human::GetStatFactor(int stat) const
{
    if (stat == STAT_HEALTH)
        return static_cast<float>(m_stats[STAT_HEALTH]) * 0.00125f;
    if (stat > STAT_HEALTH && stat <= STAT_LOCKPICK)
        return static_cast<float>(m_stats[stat]) * 0.01f;
    return 0.0f;
}

void C_Human::SetSkills(const int *skills)
{
    static const int SKILLS[] = {STAT_STRENGTH, STAT_ENDURANCE, STAT_SHOOTING, STAT_STEALTH, STAT_FIRST_AID, STAT_LOCKPICK};
    for (int stat : SKILLS) {
        m_stats[stat] = skills[stat];
        m_initialStats[stat] = skills[stat];
    }
}

void C_Human::ChangeMorale(int delta)
{
    m_stats[STAT_MORALE] += delta;
    if (m_stats[STAT_MORALE] > m_initialStats[STAT_MORALE])
        m_stats[STAT_MORALE] = m_initialStats[STAT_MORALE];
    else if (m_stats[STAT_MORALE] < 0)
        m_stats[STAT_MORALE] = 0;
}

int C_Human::GetHealthPercent() const
{
    if (m_stats[STAT_HEALTH] == 0)
        return 0;
    const float percent = static_cast<float>(m_stats[STAT_HEALTH]) * 100.0f / static_cast<float>(m_initialStats[STAT_HEALTH]);
    if (percent < 1.0f && percent > 0.0f)
        return 1;
    if (percent > 100.0f)
        return 100;
    return static_cast<int>(percent);
}

void C_Human::SetHealthPercent(int percent)
{
    if (m_stats[STAT_HEALTH] <= 0)
        return;
    if (percent > 100)
        percent = 100;
    if (percent < 1)
        percent = 1;
    m_stats[STAT_HEALTH] = static_cast<int>(static_cast<float>(m_initialStats[STAT_HEALTH] * percent) * 0.01f);
}

int C_Human::GetStaminaPercent() const
{
    if (m_stats[STAT_HEALTH] == 0 || m_stamina == 0.0f)
        return 0;
    return static_cast<int>(m_stamina / (static_cast<float>(m_stats[STAT_ENDURANCE]) * 0.01f));
}

// -------------------------------------------------------------------------- visibility

float C_Human::GetVisibility() const
{
    if (!m_controller)
        return -1.0f;
    if (m_stats[STAT_HEALTH] <= 0)
        return m_body.GetVisibilityFactor() * 50.0f;
    const uint32_t flags = m_controller->GetMoveFlags();
    // Recently alarmed humans stand out (the player's soldiers for a shorter time).
    C_TeamBase *team = (!IsLocalGame() && g_gameProfile.IsCooperativeGame() && IsServer())
                           ? static_cast<C_TeamBase *>(g_teamManager.GetCoopTeam())
                           : static_cast<C_TeamBase *>(g_teamManager.GetPlayerTeam());
    const float alertWindow = team->IsMember(this) ? 7000.0f : 8000.0f;
    float alerted = 0.0f;
    if (static_cast<float>(m_controller->GetQuietTime()) <= alertWindow)
        alerted = (flags & MOVE_STANDING) ? 450.0f : (flags & MOVE_CROUCHING) ? 380.0f : 300.0f;
    float visibility;
    if (m_vehicle || (flags & MOVE_STANDING))
        visibility = 100.0f;
    else
        visibility = (flags & MOVE_CROUCHING) ? 70.0f : 50.0f;
    if (m_role == HUMAN_ROLE_ENEMY && visibility < 30.0f)
        visibility = 30.0f;
    if (flags & MOVE_MOVING_MASK) {
        // Faster paces are more visible.
        if (flags & MOVE_PACE_1) {
            visibility *= 1.05f;
            if (visibility < 2.0f)
                visibility = 2.0f;
        } else if (flags & MOVE_PACE_2) {
            visibility *= 1.2f;
            if (visibility < 4.0f)
                visibility = 4.0f;
        } else if (flags & MOVE_PACE_3) {
            visibility *= 1.3f;
            if (visibility < 5.0f)
                visibility = 5.0f;
        } else if (flags & MOVE_PACE_4) {
            visibility *= 1.6f;
            if (visibility < 10.0f)
                visibility = 10.0f;
        } else {
            visibility *= 1.8f;
            if (visibility < 15.0f)
                visibility = 15.0f;
        }
    }
    if (alerted > visibility)
        visibility = alerted;
    visibility *= m_body.GetVisibilityFactor();
    // Lying or crouching on cover-giving ground (grass ...) hides more.
    const uint32_t *ground = m_body.GetGround();
    if ((flags & (MOVE_LYING | MOVE_CROUCHING)) && ground) {
        float cover = GetGEffects()->GetGroundCover(reinterpret_cast<const uint8_t *>(ground)[3]);
        if (cover != 1.0f && (flags & MOVE_CROUCHING))
            cover = (1.0f - cover) * 0.5f + cover;
        return cover * visibility;
    }
    return visibility;
}

// --------------------------------------------------------------------- bones / aiming

const S_vector &C_Human::GetRandomBonePos() const
{
    const auto *body = static_cast<I3D_singlemesh_visual *>(m_frame->FindChildFrame("a", FRAME_MASK_VISUAL));
    I3D_frame *bone = body->GetJoints()[RANDOM_BONES[std::rand() % 17]];
    return bone->GetWorldPos();
}

const S_vector &C_Human::GetAimPoint(const C_Human *shooter) const
{
    if (!shooter)
        return GetRandomBonePos();
    uint32_t parts = 0;
    g_sightModule.GetVisibleParts(shooter->m_sightId, m_sightTargetId, &parts);
    if (parts == 0)
        return GetRandomBonePos();
    const auto *body = static_cast<I3D_singlemesh_visual *>(m_frame->FindChildFrame("a", FRAME_MASK_VISUAL));
    I3D_frame *const *joints = body->GetJoints();
    const S_vector delta = shooter->m_frame->GetWorldPos() - m_frame->GetWorldPos();
    const float distance2 = delta.Square();

    // Bones of the parts the shooter sees (SIGHT_PART_*); players aim at fewer leg bones.
    char bones[16];
    int count = 0;
    const auto addUpperBody = [&] {
        bones[count++] = 0x0C;
        bones[count++] = 0x08;
        bones[count++] = 0x03;
        bones[count++] = 0x01;
    };
    if (shooter->m_role == HUMAN_ROLE_PLAYER) {
        if (parts & SIGHT_PART_BODY) {
            addUpperBody();
        } else if (parts & SIGHT_PART_LEGS) {
            if (distance2 > 225.0f)
                bones[count++] = 0x15;
            bones[count++] = 0x14;
            if (distance2 > 225.0f)
                bones[count++] = 0x11;
            bones[count++] = 0x10;
        }
    } else {
        if (parts & SIGHT_PART_BODY)
            addUpperBody();
        if (parts & SIGHT_PART_LEGS) {
            if (distance2 > 225.0f)
                bones[count++] = 0x16;
            if (distance2 > 100.0f)
                bones[count++] = 0x15;
            bones[count++] = 0x14;
            if (distance2 > 225.0f)
                bones[count++] = 0x12;
            if (distance2 > 100.0f)
                bones[count++] = 0x11;
            bones[count++] = 0x10;
        }
    }
    if (parts & SIGHT_PART_HEAD)
        bones[count++] = 0x18;
    if (parts & SIGHT_PART_LEFT_ARM)
        bones[count++] = 0x0E;
    if (parts & SIGHT_PART_RIGHT_ARM)
        bones[count++] = 0x0A;
    // Fix: the original divides by zero when only other part bits are set.
    if (count == 0)
        return GetRandomBonePos();
    return joints[static_cast<int>(bones[std::rand() % count])]->GetWorldPos();
}

void C_Human::GetBoxCorners(S_vector4 corners[8])
{
    auto *body = static_cast<I3D_visual *>(m_frame->FindChildFrame("a", FRAME_MASK_VISUAL));
    const I3D_bbox box = body->GetLocalBound().bbox;
    int n = 0;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            const float x = i ? box.min.x : box.max.x;
            const float y = j ? box.min.y : box.max.y;
            corners[n].x = x;
            corners[n].y = y;
            corners[n].z = box.min.z;
            ++n;
            corners[n].x = x;
            corners[n].y = y;
            corners[n].z = box.max.z;
            ++n;
        }
    }
    const S_matrix &world = body->GetMatrix();
    for (int k = 0; k < 8; ++k)
        reinterpret_cast<S_vector &>(corners[k]) *= world;
}

// ------------------------------------------------------------------------------ fading

void C_Human::FadeOut(int time)
{
    if (m_fadedOut)
        return;
    GetGEffects()->FadeHuman(this, time, false);
    m_fadedOut = true;
    m_body.SetModelVisible(false);
    SurfaceMarks_SetHumanVisible(this, false);
}

void C_Human::FadeIn(int time)
{
    if (!m_fadedOut)
        return;
    GetGEffects()->FadeHuman(this, time, true);
    m_fadedOut = false;
    m_body.SetModelVisible(true);
    SurfaceMarks_SetHumanVisible(this, true);
}
