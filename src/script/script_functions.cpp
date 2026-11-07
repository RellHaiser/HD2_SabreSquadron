// Built-in functions of script expressions (_PlayerInRange, _SignalReceived, ...).
// C_Script::CallFunction — 0x0041e3f0.
#include <cstdlib>

#include "game/actor.h"
#include "game/game.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "script/script.h"
#include "script/script_expression.h"
#include "script/script_module.h"

namespace {

// Distance reported when the reference frame is missing.
constexpr float FAR_AWAY = 1000.0f;

int ToInt(float value) { return static_cast<int>(value); }

float Distance(const I3D_frame *a, const I3D_frame *b)
{
    return (a->GetWorldPos() - b->GetWorldPos()).Magnitude();
}

float DistanceSquared(const S_vector &a, const S_vector &b) { return (a - b).Square(); }

// Living, controllable player (not unconscious, not in a vehicle).
bool IsActivePlayer(const C_Human *human)
{
    return human->IsAlive() && human->IsInPlay() && !human->IsSpectating();
}

} // namespace

int C_Script::CallFunction(int function, const int * /*argTypes*/, const int *args)
{
    if (!(g_gameRole & GAME_ROLE_SERVER))
        return 0;

    auto number = [&](int i) { return NumberVar(args[i]); };
    auto frame = [&](int i) { return FrameVar(args[i]); };

    switch (function) {
    case SFUNC_PLAYER_IN_RANGE: {
        // Number of players within the range.
        const int range = number(0);
        const float range2 = static_cast<float>(range * range);
        int count = 0;
        const bool coop = g_gameProfile.IsCooperativeGame();
        C_TeamBase *team = coop ? static_cast<C_TeamBase *>(g_teamManager.GetCoopTeam())
                                : static_cast<C_TeamBase *>(g_teamManager.GetPlayerTeam());
        for (int i = team->GetMemberCount(); i-- > 0;) {
            C_Human *player = team->GetMember(i);
            if (!player || !player->IsAlive())
                continue;
            if (coop && (!player->IsInPlay() || player->IsSpectating()))
                continue;
            if (!player->GetFrame() || !m_frame)
                continue;
            if (DistanceSquared(player->GetFrame()->GetWorldPos(), m_frame->GetWorldPos()) < range2)
                ++count;
        }
        return count;
    }

    case SFUNC_ENEMY_IN_RANGE: {
        I3D_frame *ref = frame(0);
        if (!ref)
            return 0;
        const int range = number(1);
        const S_vector pos = ref->GetWorldPos();
        for (int i = C_ActorsModule::PLAYER_SLOTS; i < g_actorsModule.GetActorCount(); ++i) {
            C_Actor *actor = g_actorsModule.GetActorById(i);
            if (!actor || actor->GetType() != ACTOR_HUMAN)
                continue;
            C_Human *human = static_cast<C_Human *>(actor);
            if (!human->IsAlive() || human->IsUnconscious() || human->GetRole() != HUMAN_ROLE_ENEMY)
                continue;
            if (DistanceSquared(human->GetFrame()->GetWorldPos(), pos) < static_cast<float>(range * range))
                return 1;
        }
        return 0;
    }

    case SFUNC_ALL_PLAYERS_IN_RANGE: {
        C_Team *team = g_teamManager.GetPlayerTeam();
        bool anyAlive = false;
        for (int i = team->GetMemberCount(); i-- > 0;) {
            C_Human *player = team->GetMember(i);
            if (!player->IsAlive())
                continue;
            anyAlive = true;
            if (!player->GetFrame() || !m_frame)
                continue;
            if (static_cast<float>(number(0)) < Distance(player->GetFrame(), m_frame))
                return 0;
        }
        return anyAlive ? 1 : 0;
    }

    case SFUNC_MINE_IN_RANGE: {
        // Iterates over a copy: the list may change while checking.
        const std::vector<C_Actor *> actors = g_temporaryActors.GetActors();
        for (size_t i = actors.size(); i-- > 0;) {
            if (actors[i]->GetType() != ACTOR_MINE || !m_frame)
                continue;
            if ((actors[i]->GetPosition() - m_frame->GetWorldPos()).Magnitude() < static_cast<float>(number(0)))
                return 1;
        }
        return 0;
    }

    case SFUNC_ITEM_IN_RANGE:
        return g_temporaryActors.IsItemInRange(number(0), m_frame->GetWorldPos(), number(1)) ? 1 : 0;

    case SFUNC_ALL_PLAYERS_DEAD:
        return g_teamManager.GetPlayerTeam()->GetAliveCount() == 0;

    case SFUNC_ALL_ENEMIES_DEAD:
        return g_actorsModule.GetLiveEnemyCount() == 0;

    case SFUNC_GET_NUM_LIVE_ENEMIES:
        return g_actorsModule.GetLiveEnemyCount();

    case SFUNC_GET_COUNT_OF_CARNAGE_ENEMIES:
        return g_actorsModule.GetCarnageEnemyCount();

    case SFUNC_SIGNAL_RECEIVED: {
        // Consumes the signal.
        const uint32_t bit = 1u << (number(0) & 31);
        if (!(m_pendingSignals & bit))
            return 0;
        m_pendingSignals &= ~bit;
        return 1;
    }

    case SFUNC_IS_LANGUAGE_VERSION:
        return g_gameProfile.IsLanguage(StringVar(args[0]));

    case SFUNC_RANDOM_INT:
        return number(0) != 0 ? std::rand() % number(0) : 0;

    case SFUNC_LOAD_GAME_VALUE: {
        const int index = number(0);
        return index >= 0 && index < SAVE_GAME_VALUES ? g_scriptModule.GetSaveGameValues()[index] : 0;
    }

    case SFUNC_SEE_ENEMY:
        if (C_Human *human = GetFrameActorAs<C_Human>(m_frame, ACTOR_HUMAN))
            return human->SeesEnemy();
        return 0;

    case SFUNC_SEE_FRIEND:
        if (C_Human *human = GetFrameActorAs<C_Human>(m_frame, ACTOR_HUMAN))
            return human->SeesFriend();
        return 0;

    case SFUNC_FRAME_IN_RANGE: {
        I3D_frame *target = frame(0);
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL && target) {
            // In multiplayer a dead or inactive player is "far away".
            C_Human *human = GetFrameActorAs<C_Human>(target, ACTOR_HUMAN);
            if (human && (human->IsDead() || !human->IsInPlay() || human->IsSpectating()))
                return ToInt(FAR_AWAY);
        }
        if (!target)
            return ToInt(FAR_AWAY);
        return ToInt(Distance(target, m_frame));
    }

    case SFUNC_ACTOR_GET_STATE: {
        C_Actor *actor = GetFrameActor(frame(0));
        if (!actor)
            return 0;
        switch (actor->GetType()) {
        case ACTOR_HUMAN: {
            C_Human *human = static_cast<C_Human *>(actor);
            return human->IsAlive() && !human->IsUnconscious();
        }
        case ACTOR_CAR: return !static_cast<C_Car *>(actor)->IsDestroyed();
        case ACTOR_DOOR: return static_cast<C_Door *>(actor)->GetState();
        case ACTOR_RADIO: return static_cast<C_Radio *>(actor)->GetState();
        case ACTOR_SWITCH: return static_cast<C_Switch *>(actor)->GetState();
        case ACTOR_PHOTO_PLACE: return static_cast<C_PhotoPlace *>(actor)->GetState();
        case ACTOR_STATIC_WEAPON: return static_cast<C_StaticWeapon *>(actor)->GetState();
        case ACTOR_EXPLOSIVE_PLACE: return static_cast<C_ExplosivePlace *>(actor)->GetState();
        case ACTOR_DOCUMENT_PLACE: return static_cast<C_DocumentPlace *>(actor)->GetState();
        case ACTOR_ANIMATION: return !static_cast<C_AnimationActor *>(actor)->IsStopped();
        case ACTOR_PIKE: return static_cast<C_Pike *>(actor)->GetState();
        case ACTOR_WIRE_CUT: return static_cast<C_WireCut *>(actor)->IsCut();
        case ACTOR_TS_EXPLOSION: return static_cast<C_TSExplosion *>(actor)->GetState();
        default: return 0;
        }
    }

    case SFUNC_GET_OBJECTIVE_STATUS:
        return g_gameProfile.GetObjectiveStatus(number(0), number(1), number(2), number(3));

    case SFUNC_GET_ALARM_TYPE: {
        C_Actor *actor = GetFrameActor(m_frame);
        if (!actor)
            return 0;
        if (actor->GetType() == ACTOR_HUMAN)
            return static_cast<C_Human *>(actor)->GetAlarmType();
        if (actor->GetType() == ACTOR_MELEE_TRACKER)
            return static_cast<C_MeleeTracker *>(actor)->GetAlarmType();
        return 0;
    }

    case SFUNC_IS_IN_INVENTORY:
        return g_teamManager.GetPlayerTeam()->HasItem(number(0));

    case SFUNC_GET_NUM_MEMBERS_OF_CLIENT_TEAM:
        return g_teamManager.GetPlayerTeam()->GetAliveCount();

    case SFUNC_GET_HUMAN_STORE_ID:
        if (C_Human *human = GetFrameActorAs<C_Human>(frame(0), ACTOR_HUMAN))
            return human->GetProfileIndex();
        return -1;

    case SFUNC_IS_TEAM_MEMBER_DEAD: {
        C_Team *team = g_teamManager.GetPlayerTeam();
        return team->GetAliveCount() != team->GetMemberCount();
    }

    case SFUNC_MP_ALL_TEAM_MEMBERS_ARE_IN_RANGE: {
        const bool allied = number(0) != 0;
        const int range = number(1);
        const S_vector pos = m_frame->GetWorldPos();
        bool none = true;
        for (int i = C_ActorsModule::PLAYER_SLOTS; i-- > 0;) {
            C_Human *player = static_cast<C_Human *>(g_actorsModule.GetActor(i));
            if (!player)
                continue;
            // A dead player that is still connected is not in range.
            if (player->IsDead() && CanPlayerRespawn(player->GetNetId()))
                return 0;
            if (!player->IsInPlay() || player->GetTeam(false) != static_cast<int>(allied) || player->IsDead() || player->IsSpectating())
                continue;
            none = false;
            if (static_cast<float>(range * range) < DistanceSquared(player->GetFrame()->GetWorldPos(), pos))
                return 0;
        }
        return none ? 0 : 1;
    }

    case SFUNC_MP_IS_TEAM_MEMBER_IN_RANGE: {
        const bool allied = number(0) != 0;
        const int range = number(1);
        const S_vector pos = m_frame->GetWorldPos();
        for (int i = C_ActorsModule::PLAYER_SLOTS; i-- > 0;) {
            C_Human *player = static_cast<C_Human *>(g_actorsModule.GetActor(i));
            if (!player)
                continue;
            if (player->IsDead()) {
                if (CanPlayerRespawn(player->GetNetId()))
                    return 0;
                continue;
            }
            if (player->IsSpectating() || player->GetTeam(false) != static_cast<int>(allied) || !player->IsInPlay())
                continue;
            if (DistanceSquared(player->GetFrame()->GetWorldPos(), pos) < static_cast<float>(range * range))
                return 1;
        }
        return 0;
    }

    case SFUNC_MP_IS_ITEM_IN_TEAM_INVENTORY: {
        // The third argument receives the frame of the player carrying the item.
        SetFrameVar(args[2], nullptr);
        const bool allied = number(0) != 0;
        const int item = number(1);
        for (int i = C_ActorsModule::PLAYER_SLOTS; i-- > 0;) {
            C_Human *player = static_cast<C_Human *>(g_actorsModule.GetActor(i));
            if (!player || !player->IsAlive() || player->IsSpectating() || !player->IsInPlay() ||
                player->GetTeam(false) != static_cast<int>(allied))
                continue;
            if (player->FindItem(item, nullptr)) {
                SetFrameVar(args[2], player->GetFrame());
                return 1;
            }
        }
        return 0;
    }

    case SFUNC_SP_GET_GAME_TYPE: {
        const int type = g_playMode->mode + g_playMode->singleMission * 4;
        return type < 0 || type > 10 ? -1 : type;
    }

    case SFUNC_SP_GET_GAME_DIFFICULTY:
        return g_gameProfile.GetDifficulty();

    case SFUNC_GET_CAMERA_STATE:
        return GetGame()->GetCameraModule()->GetState() ? 1 : 0;

    case SFUNC_GET_DIST_TO_ACTIVE_PLAYER: {
        C_Human *active = g_teamManager.GetPlayerTeam()->GetActivePlayer();
        if (!active)
            return INT32_MIN;                           // (int)2147483648.0f
        return ToInt(Distance(m_frame, active->GetFrame()));
    }

    case SFUNC_CAMERA_IN_RANGE: {
        I3D_frame *camera = GetGame()->GetScene()->GetActiveCamera();
        if (!camera)
            return ToInt(FAR_AWAY);
        return ToInt(Distance(camera, m_frame));
    }

    case SFUNC_PLAYER_IN_BOX: {
        I3D_frame *box = frame(0);
        if (box->GetType() != FRAME_DUMMY)
            return 0;
        const I3D_bbox &local = static_cast<I3D_dummy *>(box)->GetBBox();
        const S_matrix &m = box->GetMatrix();
        S_vector min, max;
        for (int c = 0; c < 8; ++c) {
            const S_vector corner((c & 1 ? local.max : local.min).x, (c & 2 ? local.max : local.min).y,
                                  (c & 4 ? local.max : local.min).z);
            const S_vector p = corner * m;
            if (c == 0) {
                min = max = p;
                continue;
            }
            min = S_vector(p.x < min.x ? p.x : min.x, p.y < min.y ? p.y : min.y, p.z < min.z ? p.z : min.z);
            max = S_vector(p.x > max.x ? p.x : max.x, p.y > max.y ? p.y : max.y, p.z > max.z ? p.z : max.z);
        }
        for (int i = 0; i < C_ActorsModule::PLAYER_SLOTS; ++i) {
            C_Human *player = static_cast<C_Human *>(g_actorsModule.GetActor(i));
            if (!player || !player->IsInPlay() || player->IsSpectating() || !player->IsAlive())
                continue;
            const S_vector &p = player->GetFrame()->GetWorldPos();
            if (p.x < max.x && p.y < max.y && p.z < max.z && min.x < p.x && min.y < p.y && min.z < p.z)
                return 1;
        }
        return 0;
    }

    case SFUNC_SIGNAL_PISTOL_FIRED:
        return GetGame()->WasSignalPistolFired();

    default:
        return 0;
    }
}
