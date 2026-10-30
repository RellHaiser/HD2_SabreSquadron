// C_Script::ExecuteCommand — the interpreter's command set (0x00416e10).
//
// Conventions of the original that the port keeps:
//  * most commands silently do nothing when an argument is unusable, then move on;
//  * a few commands stall the script (return without advancing) on bad input;
//  * some debug commands advance but report "not handled" (derived script classes
//    then run their own handler, which may advance a second time).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/actor.h"
#include "game/game.h"
#include "game/interface.h"
#include "game/lightmap_groups.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/track.h"
#include "game/tshift.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"
#include "script/script_expression.h"
#include "script/script_module.h"
#include "script/script_tables.h"

namespace {

constexpr float DEG_TO_RAD = 0.017453292f;

int Clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Corners of `box` transformed by `m`, as an axis-aligned box.
void TransformBox(const I3D_bbox &box, const S_matrix &m, S_vector &outMin, S_vector &outMax)
{
    for (int i = 0; i < 8; ++i) {
        const S_vector corner((i & 1 ? box.max : box.min).x, (i & 2 ? box.max : box.min).y,
                              (i & 4 ? box.max : box.min).z);
        const S_vector p = corner * m;
        if (i == 0) {
            outMin = outMax = p;
            continue;
        }
        outMin.x = p.x < outMin.x ? p.x : outMin.x;
        outMin.y = p.y < outMin.y ? p.y : outMin.y;
        outMin.z = p.z < outMin.z ? p.z : outMin.z;
        outMax.x = p.x > outMax.x ? p.x : outMax.x;
        outMax.y = p.y > outMax.y ? p.y : outMax.y;
        outMax.z = p.z > outMax.z ? p.z : outMax.z;
    }
}

bool StrictlyInside(const S_vector &p, const S_vector &min, const S_vector &max)
{
    return p.x < max.x && p.y < max.y && p.z < max.z && min.x < p.x && min.y < p.y && min.z < p.z;
}

// Team whose members count as "players" for the current game mode.
C_TeamBase *GetPlayerTeamForMode()
{
    if (g_gameProfile.IsCooperativeGame())
        return g_teamManager.GetCoopTeam();
    return g_teamManager.GetPlayerTeam();
}

// Sends a one-value script message to the clients.
void SendScriptValue(int msg, uint32_t value, int bits)
{
    if (C_BitStream *out = BeginScriptNetMessage(msg, true)) {
        out->WriteBits(value, bits);
        SendScriptNetMessage(out);
    }
}

uint32_t FloatBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

} // namespace

bool C_Script::ExecuteCommand(const S_ScriptCommand &cmd)
{
    if (!(g_gameRole & GAME_ROLE_SERVER) || static_cast<unsigned>(cmd.opcode - 1) >= OP_LAST_COMMAND)
        return false;

    auto frameArg = [&](int i) -> I3D_frame *& { return FrameVar(cmd.Arg(i)); };
    auto numberArg = [&](int i) -> int & { return NumberVar(cmd.Arg(i)); };
    auto boolArg = [&](int i) { return NumberVar(cmd.Arg(i)) != 0; };
    auto stringArg = [&](int i) -> C_str & { return StringVar(cmd.Arg(i)); };
    auto humanArg = [&](int i) { return GetFrameActorAs<C_Human>(frameArg(i), ACTOR_HUMAN); };
    auto validFrameIndex = [&](int i) { return cmd.Arg(i) >= 0 && cmd.Arg(i) < static_cast<int>(m_frames.size()); };
    auto validNumberIndex = [&](int i) { return cmd.Arg(i) >= 0 && cmd.Arg(i) < static_cast<int>(m_numbers.size()); };
    auto validStringIndex = [&](int i) { return cmd.Arg(i) >= 0 && cmd.Arg(i) < static_cast<int>(m_strings.size()); };

    switch (cmd.opcode) {
    // ------------------------------------------------------------------ scripts
    case OP_ScriptAssign: {
        I3D_frame *target = frameArg(0);
        const C_str name = stringArg(1);
        if (!target) {
            // A spawn point passes the script on to the actor it spawned.
            C_SpawnPoint *spawn = GetFrameActorAs<C_SpawnPoint>(m_frame, ACTOR_SPAWN_POINT);
            if (spawn && spawn->GetSpawnedActor())
                target = spawn->GetSpawnedActor()->GetFrame();
        }
        if (target)
            g_scriptModule.AssignScript(target, name);
        break;
    }

    case OP_SendSignal: {
        I3D_frame *frame = frameArg(0);
        if (!frame)
            return false;
        if (C_Script *script = frame->GetData() ? frame->GetData()->script : nullptr)
            script->SendSignal(numberArg(1));
        break;
    }

    case OP_EndScript: {
        m_finished = true;
        C_Actor *actor = GetFrameActor(m_frame);
        if (actor && actor->GetType() == ACTOR_HUMAN) {
            C_Human *human = static_cast<C_Human *>(actor);
            if (human->IsAlive()) {
                human->SetAIOptimized(0);
                human->Suspend(false);
                human->ResetAI();
                // A Junkers88 gunner that is the active player hands control back.
                C_Actor *vehicle = human->GetVehicle();
                if (vehicle && vehicle->GetType() == ACTOR_JUNKERS88 &&
                    g_teamManager.GetPlayerTeam()->GetActivePlayer() == human) {
                    g_teamManager.GetPlayerTeam()->ReturnControl();
                    ++m_ip;
                    return true;
                }
            }
        } else if (actor && actor->GetType() == ACTOR_MELEE_TRACKER) {
            C_MeleeTracker *meleeTracker = static_cast<C_MeleeTracker *>(actor);
            if (meleeTracker->IsAlive()) {
                meleeTracker->SetOptimizedController(false);
                meleeTracker->SetOptimized(false);
                meleeTracker->ResetAI();
            }
        }
        ++m_ip;
        return true;
    }

    // ------------------------------------------------------------- control flow
    case OP_If:
        if (!m_expressions[cmd.Arg(0)]->Evaluate(this)) {
            if (!SkipBlock())
                return false;
            // A following Else runs instead (its '{' is the next command).
            if (m_commands[m_ip]->opcode != OP_Else) {
                m_running = true;
                return true;
            }
        }
        ++m_ip;
        m_running = true;
        return true;

    case OP_BlockBegin:
    case OP_BlockEnd:
        ++m_ip;
        m_running = true;
        return true;

    case OP_Else:
        // Reached after the If block ran: skip the Else block.
        m_running = true;
        return SkipBlock();

    case OP_Whenever: {
        // Only reached through ExecuteBlock(); the declaration skips its body.
        const int open = ++m_ip;
        if (m_commands[open]) {
            m_ip = open + m_commands[open]->Arg(0) + 2;
            Advance();
            return true;
        }
        break;
    }

    case OP_Evaluate: {
        // <variable> = <expression>, for integers (type 2) or strings (type 4).
        const int expression = cmd.Arg(0);
        const int type = cmd.Arg(1);
        const int target = cmd.Arg(2);
        if (type == SCRIPT_TYPE_INTEGER)
            NumberVar(target) = m_expressions[expression]->Evaluate(this);
        else if (type == SCRIPT_TYPE_STRING)
            StringVar(target) = m_expressions[expression]->ToString(this);
        ++m_ip;
        m_running = true;
        return true;
    }

    case OP_SetWhenever:
        // The argument is the Whenever's index, resolved by the compiler.
        if (S_ScriptWhenever *w = m_whenevers[cmd.Arg(0)])
            w->enabled = boolArg(1);
        break;

    case OP_SetWheneverTime:
        if (S_ScriptWhenever *w = m_whenevers[cmd.Arg(0)])
            w->period = numberArg(1);
        break;

    case OP_DisableSignals:
        m_signalsDisabled = boolArg(0);
        if (m_signalsDisabled)
            m_pendingSignals = 0;
        break;

    case OP_DisableAlarms:
        m_alarmsDisabled = boolArg(0);
        break;

    case OP_DisableWhenevers:
        m_wheneversDisabled = boolArg(0);
        break;

    case OP_EnableSignal: {
        const uint32_t bit = 1u << (numberArg(0) & 31);
        if (boolArg(1))
            m_disabledSignals &= ~bit;
        else
            m_disabledSignals |= bit;
        break;
    }

    // ---------------------------------------------------------------- formations
    case OP_FORMATION_Create:
        if (C_Human *leader = humanArg(0))
            g_teamManager.GetFormation(leader, true);
        break;

    case OP_FORMATION_Destroy:
        if (C_Human *leader = humanArg(0)) {
            if (C_Formation *formation = g_teamManager.GetFormation(leader, false))
                g_teamManager.DestroyFormation(formation);
        }
        break;

    case OP_FORMATION_AddMember: {
        C_Human *member = humanArg(0);
        C_Human *leader = humanArg(1);
        C_Formation *formation = leader ? g_teamManager.GetFormation(leader, false) : nullptr;
        if (member && formation) {
            g_teamManager.RemoveFromFormation(member);
            formation->AddMember(member);
        }
        break;
    }

    case OP_FORMATION_DelMember:
        if (C_Human *member = humanArg(0))
            g_teamManager.RemoveFromFormation(member);
        break;

    case OP_FORMATION_SetTypeColumn:
    case OP_FORMATION_SetTypeLine:
    case OP_FORMATION_SetTypeQuad2X3:
    case OP_FORMATION_SetTypeQuad3X2:
    case OP_FORMATION_SetTypeArrowSharp:
    case OP_FORMATION_SetTypeArrowInvert:
    case OP_FORMATION_SetScale: {
        C_Human *leader = humanArg(0);
        C_Formation *formation = leader ? g_teamManager.GetFormation(leader, false) : nullptr;
        if (!formation)
            break;
        C_FormationShape *shape = formation->GetShape();
        switch (cmd.opcode) {
        case OP_FORMATION_SetTypeColumn: shape->SetType(FORMATION_COLUMN); break;
        case OP_FORMATION_SetTypeLine: shape->SetType(FORMATION_LINE); break;
        case OP_FORMATION_SetTypeQuad2X3: shape->SetType(FORMATION_QUAD_2X3); break;
        case OP_FORMATION_SetTypeQuad3X2: shape->SetType(FORMATION_QUAD_3X2); break;
        case OP_FORMATION_SetTypeArrowSharp: shape->SetType(FORMATION_ARROW_SHARP); break;
        case OP_FORMATION_SetTypeArrowInvert: shape->SetType(FORMATION_ARROW_INVERT); break;
        default: {
            const float scale = numberArg(1) * 0.01f;             // cm -> m
            shape->SetScale(S_vector(scale, scale, scale));
            break;
        }
        }
        break;
    }

    // ------------------------------------------------------------------- actors
    case OP_HUMAN_Kill: {
        C_Actor *actor = GetFrameActor(frameArg(0));
        if (!actor)
            break;
        if (actor->GetType() == ACTOR_HUMAN)
            actor->Hit(HIT_SCRIPT_KILL, -1.0f, S_vector(0, 0, 0), S_vector(0, 0, 0), nullptr, 0);
        else if (actor->GetType() == ACTOR_ANIMATION)
            static_cast<C_AnimationActor *>(actor)->Stop(false);
        break;
    }

    case OP_HUMAN_KillEx:
        // The second argument (0, 1, 2) is ignored.
        if (C_Human *human = humanArg(0))
            human->Hit(HIT_SCRIPT_KILL_EX, -1.0f, S_vector(0, 0, 0), S_vector(0, 0, 0), nullptr, 0);
        break;

    case OP_HUMAN_KillAllInBox: {
        I3D_frame *frame = frameArg(0);
        if (!frame || frame->GetType() != FRAME_DUMMY) {
            Advance();
            return false;
        }
        S_vector min, max;
        TransformBox(static_cast<I3D_dummy *>(frame)->GetBBox(), frame->GetMatrix(), min, max);
        for (int i = g_actorsModule.GetActorCount(); i-- > 0;) {
            C_Actor *actor = g_actorsModule.GetActor(i);
            if (!actor || actor->GetType() != ACTOR_HUMAN)
                continue;
            C_Human *human = static_cast<C_Human *>(actor);
            if (!human->IsInPlay() || human->IsSpectating())
                continue;
            I3D_frame *body = human->GetBodyFrame();
            if (!StrictlyInside(body->GetWorldPos(), min, max))
                continue;
            const S_vector dir((RandFloat() - 0.5f) * 2.0f, RandFloat() * 2.0f + 3.0f, (RandFloat() - 0.5f) * 2.0f);
            human->Hit(HIT_EXPLOSION, -1.0f, body->GetWorldPos(), dir, nullptr, 0);
        }
        break;
    }

    case OP_SetNPCTeamStatus: {
        C_Human *npc = humanArg(0);
        const bool status = boolArg(1);
        if (npc && (npc->GetRole() == HUMAN_ROLE_NPC_TEAM || npc->GetRole() == HUMAN_ROLE_NPC_TEAM_5)) {
            if (!status)
                break;
            if (npc->IsAlive()) {
                g_teamManager.GetPlayerTeam()->SetNPCMember(npc, status);
                npc->SetAIOptimized(0);
                npc->Suspend(false);
                npc->ResetAI();
            }
        }
        if (!status)
            break;
        // The NPC joins the player's team: its script ends here.
        m_finished = true;
        ++m_ip;
        return true;
    }

    case OP_SetAlarmType:
        if (C_Human *human = GetFrameActorAs<C_Human>(m_frame, ACTOR_HUMAN))
            human->SetAlarmType(numberArg(0), boolArg(1));
        break;

    case OP_SetActorState: {
        C_Actor *actor = GetFrameActor(frameArg(0));
        if (!actor)
            break;
        const int state = numberArg(1);
        switch (actor->GetType()) {
        case ACTOR_DOOR:
            static_cast<C_Door *>(actor)->SetState(state ? 2 : 3, true, false, 0);
            break;
        case ACTOR_RADIO:
            static_cast<C_Radio *>(actor)->SetState(state);
            break;
        case ACTOR_SUPPLY_BOX:
            static_cast<C_SupplyBox *>(actor)->SetEnabled(state != 0);
            break;
        case ACTOR_SWITCH:
            static_cast<C_Switch *>(actor)->SetState(state);
            break;
        case ACTOR_TRAIN:
            static_cast<C_Train *>(actor)->Start();
            break;
        case ACTOR_STATIC_WEAPON:
            static_cast<C_StaticWeapon *>(actor)->SetState(state);
            break;
        case ACTOR_EXPLOSIVE_PLACE:
            static_cast<C_ExplosivePlace *>(actor)->SetState(state);
            break;
        case ACTOR_DOCUMENT_PLACE:
            static_cast<C_DocumentPlace *>(actor)->SetState(state);
            break;
        case ACTOR_ANIMATION: {
            C_AnimationActor *anim = static_cast<C_AnimationActor *>(actor);
            if (state == 666)
                anim->HoldAnimation();
            else if (!anim->IsStopped())
                anim->Stop(false);
            break;
        }
        case ACTOR_PIKE:
            static_cast<C_Pike *>(actor)->SetState(state ? 2 : 3);
            break;
        case ACTOR_FOWL_GENERATOR:
            static_cast<C_FowlGenerator *>(actor)->Start(true, 0);
            break;
        case ACTOR_JUNKERS88:
            static_cast<C_Junkers88 *>(actor)->SetState(state);
            break;
        case ACTOR_WIRE_CUT:
            if (state) {
                // Cut along the frame's facing direction.
                const S_vector dir = actor->GetFrame()->GetWorldDir();
                static_cast<C_WireCut *>(actor)->Cut(dir, true);
            }
            break;
        case ACTOR_TS_EXPLOSION:
            static_cast<C_TSExplosion *>(actor)->Trigger(1);
            break;
        default:
            break;
        }
        break;
    }

    case OP_DOOR_Lock: {
        C_Door *door = GetFrameActorAs<C_Door>(frameArg(0), ACTOR_DOOR);
        if (!door)
            break;
        if (boolArg(1))
            door->Lock(door->GetLockKey());
        else
            door->Unlock(door->GetLockKey());
        break;
    }

    case OP_DECORATOR_SetOn:
        if (g_decorators)
            g_decorators->SetOn(boolArg(0));
        break;

    case OP_AirAttack_SetTarget: {
        I3D_frame *target = frameArg(0);
        const bool attack = boolArg(1);
        if (C_AirAttack *air = GetFrameActorAs<C_AirAttack>(m_frame, ACTOR_AIR_ATTACK))
            air->SetTarget(!attack, target);
        break;
    }

    case OP_Car_SetFuel: {
        C_Car *car = GetFrameActorAs<C_Car>(frameArg(0), ACTOR_CAR);
        if (!car)
            break;
        car->GetVehicle()->SetFuel(numberArg(1) * 0.1f);         // 1/10 l
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
            car->SendFuel();
        break;
    }

    case OP_ITEM_Teleport: {
        C_ItemActor *item = GetFrameActorAs<C_ItemActor>(frameArg(0), ACTOR_ITEM);
        I3D_frame *target = frameArg(1);
        if (item && target)
            item->TeleportTo(target);
        break;
    }

    case OP_HUMAN_RemoveItemsFromStore:
        if (C_Human *human = humanArg(0))
            human->RemoveItemsFromStore(numberArg(1));
        break;

    case OP_AddHumanHealth:
        if (C_Human *human = humanArg(0)) {
            const int add = numberArg(1);
            human->SetHealthPercent(human->GetHealthPercent() + add);
        }
        break;

    case OP_SetupJunkers88:
        if (g_junkers88)
            g_junkers88->SetInterceptors(numberArg(0) * 0.01f, numberArg(1) * 0.01f);
        break;

    case OP_FRM_SwitchFaceTexture: {
        I3D_frame *frame = frameArg(0);
        const char *texture = stringArg(1);
        if (C_Human *human = GetFrameActorAs<C_Human>(frame, ACTOR_HUMAN))
            human->SetTexture(texture);
        else
            GetGEffects()->SwitchFaceTexture(frame, texture);
        break;
    }

    // ------------------------------------------------------------------- frames
    case OP_FRM_GetNearestPlayer: {
        I3D_frame *ref = frameArg(0);
        SetFrameVar(cmd.Arg(1), nullptr);
        if (!ref)
            break;
        C_TeamBase *team = GetPlayerTeamForMode();
        float best = 3.402823466e38f;
        I3D_frame *nearest = nullptr;
        for (int i = team->GetMemberCount(); i-- > 0;) {
            C_Human *player = team->GetMember(i);
            if (player->IsDead() || !player->IsInPlay() || player->IsSpectating() || !player->GetFrame())
                continue;
            const float dist = (player->GetFrame()->GetWorldPos() - ref->GetWorldPos()).Magnitude();
            if (dist < best) {
                best = dist;
                nearest = player->GetFrame();
            }
        }
        if (nearest)
            SetFrameVar(cmd.Arg(1), nearest);
        break;
    }

    case OP_FRM_GetMyFrame:
        if (!validFrameIndex(0))
            break;
        SetFrameVar(cmd.Arg(0), m_frame);
        break;

    case OP_FRM_FindFrame: {
        if (!validFrameIndex(0))
            break;
        SetFrameVar(cmd.Arg(0), nullptr);
        if (!validStringIndex(1))
            break;
        const C_str name = stringArg(1);
        I3D_scene *scene = GetGame()->GetScene();
        I3D_frame *found = I3DGetDriver()->FindFrame(scene, name);
        if (!found && scene)
            found = scene->FindFrame(name, FRAME_MASK_ALL);
        if (found)
            SetFrameVar(cmd.Arg(0), found);
        break;
    }

    case OP_FRM_FindChildFrame: {
        if (!validFrameIndex(0))
            break;
        SetFrameVar(cmd.Arg(0), nullptr);
        if (!validStringIndex(1))
            break;
        const C_str name = stringArg(1);
        if (I3D_frame *parent = frameArg(2)) {
            if (I3D_frame *found = parent->FindChildFrame(name, 0xFFFF))
                SetFrameVar(cmd.Arg(0), found);
        }
        break;
    }

    case OP_FRM_SetOn: {
        if (!validFrameIndex(0) || !validNumberIndex(1))
            return true;                                // stalls on bad arguments
        const bool on = boolArg(1);
        I3D_frame *frame = frameArg(0);
        if (!frame)
            break;
        if (frame->GetType() == FRAME_SOUND)
            g_scriptModule.RegisterSoundFrame(frame);
        frame->Update();
        // The clients (including the local one) switch the frame.
        if (C_BitStream *out = BeginScriptNetMessage(SCRIPT_MSG_FRAME_SET_ON, true)) {
            out->WriteString(frame->GetName());
            out->WriteBits(on, 1);
            SendScriptNetMessage(out);
        }
        break;
    }

    case OP_FRM_SetAnimation:
    case OP_FRM_SetAnimationDelayed: {
        I3D_model *model = static_cast<I3D_model *>(frameArg(0));
        if (!model || model->GetType() != FRAME_MODEL)
            break;
        const C_str animName = stringArg(1);
        const int stage = numberArg(2);
        bool started = false;
        I3D_animation_set *anim = nullptr;
        if (animName.Size() < 2 || g_animationCache.Get(animName, &anim) != I3D_OK) {
            // No (loadable) animation: clear the stage.
            model->SetAnimTime(stage, 0);
            model->StopAnimation(stage);
            model->SetAnimation(nullptr, stage, 1.0f, 0);
            g_scriptModule.UnregisterAnimatedModel(model);
        } else {
            anim->SetPlayMode(cmd.opcode == OP_FRM_SetAnimation ? I3DANIM_LOOP : I3DANIM_PLAY_ONCE);
            model->SetAnimation(anim, stage, 1.0f, 0);
            anim->Release();
            model->SetAnimTime(stage, 0);
            g_scriptModule.RememberFrame(model, SCRIPT_FRAME_ANIMATION);
            g_scriptModule.RegisterAnimatedModel(model);
            model->Update();
            started = true;
        }
        if (started && cmd.opcode != OP_FRM_SetAnimation) {
            // Delayed: Tick() advances when the animation ends.
            m_waitAnimModel = model;
            m_waitAnimLabel = m_ip;
            return true;
        }
        break;
    }

    case OP_FRM_ResetAnimation: {
        I3D_model *model = static_cast<I3D_model *>(frameArg(0));
        if (!model || model->GetType() != FRAME_MODEL)
            break;
        const int stage = numberArg(1);
        model->SetAnimTime(stage, 0);
        model->Tick(1);
        model->StopAnimation(stage);
        model->SetAnimation(nullptr, stage, 1.0f, 0);
        g_scriptModule.UnregisterAnimatedModel(model);
        break;
    }

    case OP_FRM_StopAnimation: {
        I3D_model *model = static_cast<I3D_model *>(frameArg(0));
        if (!model)
            return false;
        if (model->GetType() != FRAME_MODEL)
            break;
        model->StopAnimation(numberArg(1));
        g_scriptModule.UnregisterAnimatedModel(model);
        break;
    }

    case OP_FRM_TeleportNearCheckpoint: {
        I3D_frame *frame = frameArg(0);
        const C_str pointName = stringArg(1);
        const C_str dirName = stringArg(2);
        if (!frame)
            break;
        C_Actor *actor = GetFrameActor(frame);
        C_Checkpoints *checkpoints = GetGame()->GetCheckpoints();

        if (!actor || actor->GetType() != ACTOR_CAR) {
            const int point = checkpoints->Find(pointName);
            if (point == CHECKPOINT_NONE)
                break;
            const S_vector pos = checkpoints->Get(point)->pos;
            I3D_frame *moved = actor ? actor->GetFrame() : frame;
            if (actor) {
                actor->SetPosition(pos);
                moved->SetPos(pos);
                if (actor->GetType() == ACTOR_HUMAN)
                    static_cast<C_Human *>(actor)->GetMovement()->ResetToFrame();
            } else {
                frame->SetPos(pos);
            }
            if (!dirName.IsEmpty()) {
                const int dirPoint = checkpoints->Find(dirName);
                if (dirPoint != CHECKPOINT_NONE) {
                    const S_vector dir = (checkpoints->Get(dirPoint)->pos - pos).Normalized();
                    if (actor) {
                        moved->SetWorldDir(dir, 0.0f);
                        actor->SetDirection(dir);
                    } else {
                        frame->SetDir(dir, 0.0f);
                    }
                }
            }
            moved->Update();
        } else {
            // Cars teleport onto the road network.
            C_CheckpointNetwork *roads = checkpoints->GetRoadNetwork();
            const int point = roads->Find(pointName);
            if (point == CHECKPOINT_NONE)
                break;
            C_Vehicle *vehicle = static_cast<C_Car *>(actor)->GetVehicle();
            vehicle->Reset(0);
            const S_vector pos = roads->Get(point)->pos;
            actor->SetPosition(pos);
            actor->GetFrame()->SetPosInline(pos);
            if (!dirName.IsEmpty()) {
                const int dirPoint = roads->Find(dirName);
                if (dirPoint != CHECKPOINT_NONE) {
                    const S_vector dir = (roads->Get(dirPoint)->pos - pos).Normalized();
                    actor->GetFrame()->SetWorldDir(dir, 0.0f);
                    actor->SetDirection(dir);
                }
            }
            actor->GetFrame()->Update();
            const bool engineOn = vehicle->IsEngineOn();
            vehicle->Restart(0.0f, true);
            vehicle->SetEngine(engineOn, false);
        }
        break;
    }

    case OP_FRM_TeleportNearFrame: {
        I3D_frame *moving = frameArg(0);
        I3D_frame *target = frameArg(1);
        const bool link = boolArg(2);
        if (!moving || !target)
            break;
        // Put `moving` at the target's origin, keeping its parent...
        g_scriptModule.RememberFrame(moving, SCRIPT_FRAME_LINK);
        I3D_frame *parent = moving->GetParent();
        moving->LinkTo(target, I3DLINK_KEEP_WORLD);
        moving->SetPosInline(S_vector(0, 0, 0));
        moving->SetWorldDir(S_vector(0, 0, 1), 0.0f);
        moving->Update();
        moving->LinkTo(parent, I3DLINK_KEEP_WORLD);
        moving->Update();
        // ...then optionally link it to the target, otherwise to the target's sector.
        if (link) {
            g_scriptModule.RememberFrame(moving, SCRIPT_FRAME_LINK | SCRIPT_FRAME_LINKED);
            moving->LinkTo(target, I3DLINK_KEEP_WORLD);
        } else {
            I3D_scene *scene = GetGame()->GetScene();
            I3D_frame *sector = scene->GetSector(target->GetWorldPos() + S_vector(0, 0.5f, 0));
            if (!sector)
                sector = scene->GetPrimarySector();
            g_scriptModule.RememberFrame(moving, SCRIPT_FRAME_LINK);
            moving->LinkTo(sector, I3DLINK_KEEP_WORLD);
        }
        if (C_Actor *actor = GetFrameActor(moving)) {
            actor->SetPosition(target->GetWorldPos());
            actor->SetDirection(target->GetWorldDir());
            if (actor->GetType() == ACTOR_HUMAN)
                static_cast<C_Human *>(actor)->GetMovement()->ResetToFrame();
        }
        moving->Update();
        break;
    }

    case OP_FRM_TeleportNearFrameBack:
        if (I3D_frame *frame = frameArg(0))
            g_scriptModule.RestoreFrame(frame);
        break;

    case OP_FRM_SetTarget: {
        I3D_frame *frame = frameArg(0);
        I3D_frame *target = frameArg(1);
        if (frame && target && target->GetType() == FRAME_TARGET) {
            if (boolArg(2))
                static_cast<I3D_target *>(target)->AddFrame(frame);
            else
                static_cast<I3D_target *>(target)->RemoveFrame(frame);
        }
        break;
    }

    case OP_FRM_CreatePhysicalObject: {
        I3D_frame *frame = frameArg(0);
        if (!frame)
            break;
        const float weight = static_cast<float>(numberArg(1));
        C_Debris *debris = g_temporaryActors.CreateDebris(frame, frame->GetWorldPos(), frame->GetWorldDir());
        if (debris->GetFrame()) {
            debris->GetFrame()->SetOn(true);
            debris->GetFrame()->Update();
        }
        S_DebrisParams params;
        params.SetVelocity(S_vector(0, 1.0f, 0));
        params.weight = weight;
        params.friction = 0.75f;
        params.moveCollision = boolArg(3);
        debris->Setup(params, boolArg(2));
        break;
    }

    case OP_FRM_RotateX:
    case OP_FRM_RotateY:
    case OP_FRM_RotateZ: {
        const int axis = cmd.opcode == OP_FRM_RotateX ? 0 : cmd.opcode == OP_FRM_RotateY ? 1 : 2;
        g_scriptModule.RotateFrame(frameArg(0), axis, numberArg(1), numberArg(2));
        break;
    }

    case OP_FRM_WatchTrack:
    case OP_FRM_WatchTrackWithParent: {
        if (!validFrameIndex(0) || !validStringIndex(1))
            return true;                                // stalls on bad arguments
        I3D_frame *frame = frameArg(0);
        const char *track = stringArg(1);
        if (cmd.opcode == OP_FRM_WatchTrackWithParent) {
            if (frame)
                g_trackModule.CreatePlayer(track, frame, frameArg(2));
            break;
        }
        C_Actor *actor = GetFrameActor(frame);
        if (actor && actor->GetType() == ACTOR_TRAIN) {
            // Trains follow tracks themselves; an empty name stops them.
            static_cast<C_Train *>(actor)->SetTrack(track[0] != '\0', track);
            break;
        }
        if (frame)
            g_trackModule.CreatePlayer(track, frame, nullptr);
        break;
    }

    case OP_FRM_SetShifting: {
        I3D_frame *frame = frameArg(0);
        if (!frame)
            break;
        const bool on = boolArg(1);
        const bool reverse = boolArg(2);
        if (!g_tshiftModule.HasFrame(frame))
            break;
        const uint32_t visual = frame->GetVisualType();
        if (visual != I3DVISUAL_LMAP && visual != I3DVISUAL_OBJECT)
            break;
        if (I3D_mesh *mesh = static_cast<I3D_visual *>(frame)->GetVisualMesh()) {
            // (Not checked for null: the frame has an entry, and the mesh is its.)
            S_TShiftEntry *entry = g_tshiftModule.GetEntry(mesh);
            entry->on = on;
            entry->reverse = reverse;
        }
        break;
    }

    // ----------------------------------------------------------- frame effects
    case OP_FRM_SoundAdjustVolume:
    case OP_FRM_SoundAdjustDoppler: {
        I3D_sound *sound = static_cast<I3D_sound *>(frameArg(0));
        if (!sound || sound->GetType() != FRAME_SOUND)
            break;
        g_scriptModule.RememberFrame(sound, SCRIPT_FRAME_SOUND);
        if (cmd.opcode == OP_FRM_SoundAdjustVolume) {
            sound->SetVolume(numberArg(1) * 0.01f);
        } else {
            sound->SetVelocity(S_vector(static_cast<float>(numberArg(1)), static_cast<float>(numberArg(2)),
                                        static_cast<float>(numberArg(3))));
        }
        sound->Update();
        break;
    }

    case OP_FRM_MorphSpeech:
    case OP_FRM_MorphSpeechDelayed: {
        I3D_frame *frame = frameArg(0);
        if (frame) {
            if (C_BitStream *out = BeginScriptNetMessage(SCRIPT_MSG_MORPH_SPEECH, true)) {
                out->WriteString(frame->GetName());
                out->WriteBits(numberArg(1), 32);              // phrase
                out->WriteBits(numberArg(2), 8);               // near [m]
                out->WriteBits(numberArg(3), 8);               // far [m]
                SendScriptNetMessage(out);
            }
        }
        if (cmd.opcode == OP_FRM_MorphSpeechDelayed) {
            // Wait for the phrase to finish (Tick() advances after the delay).
            m_delay = GetSoundModule()->GetDubbingSampleSize(numberArg(1));
            return true;
        }
        break;
    }

    case OP_FRM_SetLightMap: {
        if (!validFrameIndex(0) || !validNumberIndex(1))
            return true;                                // stalls on bad arguments
        const int level = numberArg(1);
        I3D_frame *frame = frameArg(0);
        if (!frame)
            break;
        if (frame->GetType() != FRAME_VISUAL)
            return true;                                // stalls (original behaviour)
        if (C_BitStream *out = BeginScriptNetMessage(SCRIPT_MSG_SET_LIGHTMAP, true)) {
            out->WriteString(frame->GetName());
            out->WriteBits(level, 8);
            SendScriptNetMessage(out);
        }
        break;
    }

    case OP_FRM_SetGroupLightMap: {
        const C_str group = stringArg(0);
        const int level = numberArg(1);
        if (GetLightMapGroups()->FindGroup(group)) {
            if (C_BitStream *out = BeginScriptNetMessage(SCRIPT_MSG_SET_GROUP_LIGHTMAP, true)) {
                out->WriteString(group);
                out->WriteBits(level, 8);
                SendScriptNetMessage(out);
            }
        }
        break;
    }

    case OP_FRM_CreateParticle:
        // The clients create the effect (the original does not check the frame).
        if (C_BitStream *out = BeginScriptNetMessage(SCRIPT_MSG_CREATE_PARTICLE, true)) {
            out->WriteBits(numberArg(0), 8);
            out->WriteString(frameArg(1) ? frameArg(1)->GetName() : "");
            SendScriptNetMessage(out);
        }
        break;

    case OP_FRM_CreateIndexedParticle: {
        I3D_frame *frame = frameArg(1);
        if (frame)
            numberArg(2) = g_particlesModule.Create(numberArg(0), frame->GetMatrix(), frame, true, false);
        break;
    }

    case OP_FRM_DestroyIndexedParticle:
        if (numberArg(0) != -1)
            g_particlesModule.Destroy(numberArg(0));
        break;

    case OP_FRM_FinishIndexedParticle:
        if (numberArg(0) != -1)
            g_particlesModule.Finish(numberArg(0));
        break;

    case OP_MakeExplosion: {
        I3D_frame *frame = frameArg(0);
        if (!frame)
            break;
        S_Explosion explosion;
        explosion.pos = frame->GetMatrix()(3);
        explosion.posW = frame->GetMatrix().m[3][3];
        explosion.damage = numberArg(1) * 0.001f;          // 1000 = 1.0
        explosion.range = numberArg(2) * 0.001f;           // mm -> m
        explosion.Execute();
        break;
    }

    // ------------------------------------------------------------------ cameras
    case OP_FRM_SetActiveCamera: {
        I3D_frame *camera = frameArg(0);
        if (!camera)
            break;
        C_CameraModule *cameras = GetGame()->GetCameraModule();
        if (!cameras->IsGameCamera(camera)) {
            // A script camera hides the HUD for as long as it is active.
            if (GetGame()->IsFirstPersonView() && !m_hudHiddenByScript) {
                m_hudHiddenByScript = true;
                GetGame()->ShowFirstPersonView(false);
            }
            if (GetGame()->IsBinocularsView() && !m_hudPanelHiddenByScript) {
                m_hudPanelHiddenByScript = true;
                GetGame()->ShowBinocularsView(false);
            }
        }
        if (camera->GetType() != FRAME_CAMERA)
            break;
        if (cameras->IsGameCamera(camera)) {
            if (m_hudHiddenByScript) {
                m_hudHiddenByScript = false;
                GetGame()->ShowFirstPersonView(true);
            }
            if (m_hudPanelHiddenByScript) {
                m_hudPanelHiddenByScript = false;
                GetGame()->ShowBinocularsView(true);
            }
        }
        I3D_scene *scene = GetGame()->GetScene();
        if (!scene)
            break;
        I3D_camera *newCamera = static_cast<I3D_camera *>(camera);
        if (I3D_camera *current = scene->GetActiveCamera()) {
            float nearRange, farRange;
            current->GetRange(nearRange, farRange);
            newCamera->SetRange(nearRange, farRange);
            newCamera->SetAspectRatio(current->GetAspectRatio());
            newCamera->SetFOV(current->GetFOV());
            newCamera->Update();
        }
        scene->SetActiveCamera(newCamera);
        break;
    }

    case OP_FRM_GetActiveCamera: {
        I3D_frame *&var = frameArg(0);
        if (var)
            var->Release();
        I3D_scene *scene = GetGame()->GetScene();
        var = scene ? scene->GetActiveCamera() : nullptr;
        if (var)
            var->AddRef();
        break;
    }

    case OP_FRM_SetCameraFrame:
        GetGame()->GetCameraModule()->SetTarget(frameArg(0), -1);
        break;

    case OP_FRM_SetCameraFrameSlide:
        GetGame()->GetCameraModule()->SetTarget(frameArg(0), numberArg(1));
        break;

    case OP_CSC_SetCameraFOV:
        GetGame()->GetScene()->GetActiveCamera()->SetFOV(numberArg(0) * DEG_TO_RAD);
        break;

    case OP_CSC_SetCameraAnimation:
        GetGEffects()->SetCameraAnimation(frameArg(0), stringArg(1));
        break;

    // ------------------------------------------------------------ sound, music
    case OP_PlaySound: {
        if (!m_frame)
            break;
        S_SoundRequest request;
        const S_matrix &m = m_frame->GetMatrix();
        request.pos = m(3);
        request.posW = m.m[3][3];
        request.flags = SOUND_SILENT_TO_AI;
        request.chapter = numberArg(0);
        request.group = numberArg(1);
        GetSoundModule()->Play(request);
        break;
    }

    case OP_PlayMusic:
        GetSoundModule()->PlayMusic(numberArg(0));
        break;

    case OP_PlayCutsceneMusic: {
        const int index = numberArg(0);
        GetSoundModule()->CrossfadeMusic(index, 4000, 0);
        // -1 goes back to the mission's music.
        if (index == -1)
            GetGame()->RestoreMusic(GetGame()->IsAlarmMusic());
        break;
    }

    case OP_AddPauseSound:
        g_scriptModule.AddPauseSound(stringArg(0));
        break;

    // ------------------------------------------------------- mission & interface
    case OP_SetObjectiveStatus:
        SetObjectiveStatus(numberArg(0), numberArg(1));
        break;

    case OP_AddDiaryText:
        if (C_PlayerProfile *player = g_gameProfile.GetPlayerProfile())
            player->GetDiary()->AddText(numberArg(0));
        break;

    case OP_AddDebriefingText:
        if (C_PlayerProfile *player = g_gameProfile.GetPlayerProfile())
            player->GetDebriefing().AddText(numberArg(0));
        break;

    case OP_Printf:
        GetGame()->Print(stringArg(0));                     // empty in the release build
        break;

    case OP_PrintfNumber: {
        char text[1024];
        std::snprintf(text, sizeof(text), "%s %d", static_cast<const char *>(stringArg(0)), numberArg(1));
        GetGame()->Print(text);
        break;
    }

    case OP_SaveGameValue: {
        const int pos = numberArg(0);
        if (pos >= 0 && pos < SAVE_GAME_VALUES)
            g_scriptModule.GetSaveGameValues()[pos] = numberArg(1);
        break;
    }

    case OP_SetPlayerInput:
        GetGame()->SetPlayerInputEnabled(boolArg(0));
        break;

    case OP_PlayAvi:
        GetGame()->PlayAvi(stringArg(0));
        break;

    case OP_SetInterfaceTimer:
        GetGame()->GetInterface()->SetTimer(numberArg(0));
        break;

    case OP_SUBTITLES_SetOn:
        SendScriptValue(SCRIPT_MSG_SUBTITLES_ON, boolArg(0), 1);
        break;

    case OP_SUBTITLES_SetText:
        SendScriptValue(SCRIPT_MSG_SUBTITLES_TEXT, numberArg(0), 32);
        break;

    case OP_Mission_SetUniformDisguise:
        GetGame()->SetUniformDisguise(boolArg(0));
        break;

    case OP_SetupTacticalMode:
        GetGame()->SetTacticalMode(boolArg(0));
        break;

    case OP_HINT_ClearAll:
        GetGame()->GetInterface()->ClearHints();
        break;

    case OP_HINT_Add:
        GetGame()->GetInterface()->AddHint(numberArg(0));
        break;

    case OP_MP_EnableSpawnZone:
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
            EnableSpawnZone(stringArg(0), numberArg(1));
        break;

    case OP_CheckpointSeparate:
    case OP_CheckpointJoin: {
        C_Checkpoints *checkpoints = GetGame()->GetCheckpoints();
        if (!checkpoints)
            break;
        const char *a = stringArg(0);
        const char *b = stringArg(1);
        // Walking network first, then the road network.
        const bool join = cmd.opcode == OP_CheckpointJoin;
        if (join ? checkpoints->Join(a, b) : checkpoints->Separate(a, b))
            break;
        if (C_CheckpointNetwork *roads = checkpoints->GetRoadNetwork())
            join ? roads->Join(a, b) : roads->Separate(a, b);
        break;
    }

    // ---------------------------------------------------------------- cutscenes
    case OP_CSC_SetTimer:
        GetGEffects()->SetTimeScale(numberArg(0) * 0.001f, numberArg(1) * 0.001f);
        break;

    case OP_CSC_FadeIn:
        GetGEffects()->Fade(numberArg(0), FADE_IN, nullptr);
        break;

    case OP_CSC_FadeOut:
        GetGEffects()->Fade(numberArg(0), FADE_OUT, nullptr);
        break;

    case OP_CSC_FadeInFromColor:
    case OP_CSC_FadeOutToColor: {
        const S_vector color(Clamp255(numberArg(1)) / 255.0f, Clamp255(numberArg(2)) / 255.0f,
                             Clamp255(numberArg(3)) / 255.0f);
        GetGEffects()->Fade(numberArg(0), cmd.opcode == OP_CSC_FadeInFromColor ? FADE_IN : FADE_OUT, &color);
        break;
    }

    case OP_CSC_RunCutscene:
        g_scriptModule.RunCutscene(numberArg(0));
        break;

    case OP_CSC_EndCutscene:
        g_scriptModule.EndCutscene();
        break;

    case OP_CSC_SetupFakeCutscene:
        g_scriptModule.SetFakeCutscene(boolArg(0));
        break;

    case OP_CSC_NoTeamDamage:
        GetGame()->SetNoTeamDamage(boolArg(0));
        break;

    case OP_CSC_RunCLouder:
        GetGEffects()->SetCloudsOn(boolArg(0));
        break;

    case OP_CSC_GetPlayerNum:
    case OP_CSC_GetPlayerNumNoNPC: {
        // Index-th living team member (NoNPC skips the NPC team members).
        int index = numberArg(0);
        C_Human *found = nullptr;
        C_Team *team = g_teamManager.GetPlayerTeam();
        if (index >= 0 && index < team->GetAliveCount()) {
            for (int i = 0; i < team->GetMemberCount(); ++i) {
                C_Human *member = team->GetMember(i);
                if (member->IsDead())
                    continue;
                if (cmd.opcode == OP_CSC_GetPlayerNumNoNPC &&
                    (member->GetRole() == HUMAN_ROLE_NPC_TEAM || member->GetRole() == HUMAN_ROLE_NPC_TEAM_5))
                    continue;
                if (index-- == 0) {
                    found = member;
                    break;
                }
            }
        }
        SetFrameVar(cmd.Arg(1), nullptr);
        if (!found)
            break;
        SetFrameVar(cmd.Arg(1), found->GetFrame());
        break;
    }

    case OP_CSC_DuplicateHuman: {
        if (!g_scriptModule.IsCutsceneRunning())
            break;
        I3D_frame *source = frameArg(0);
        SetFrameVar(cmd.Arg(1), nullptr);
        if (!source)
            break;
        // Model copy of the human, placed where the original stands.
        I3D_model *copy = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
        if (source->GetType() == FRAME_MODEL) {
            copy->DuplicateModel(static_cast<I3D_model *>(source));
            GetGEffects()->SetModelHighlight(copy, 0.0f);
        }
        copy->LinkTo(source->GetParent(), 0);
        copy->SetPosInline(source->GetPos());
        copy->SetRot(source->GetRot());
        // Named after the third argument unless that name is already taken.
        const char *name = stringArg(2);
        I3D_scene *scene = GetGame()->GetScene();
        if (!I3DGetDriver()->FindFrame(scene, name) && !(scene && scene->FindFrame(name, FRAME_MASK_ALL)))
            copy->SetName(name);
        copy->Update();
        FrameVar(cmd.Arg(1)) = copy;                       // keeps the creation reference

        // Move the weapon from the holster joint into the right hand.
        I3D_frame *holster = copy->FindChildFrame("hw1", 0xFFFF);
        I3D_frame *hand = copy->FindChildFrame("rw", 0xFFFF);
        if (holster) {
            if (holster->NumChildren() > 0) {
                if (I3D_frame *weapon = holster->GetChild(0)) {
                    weapon->LinkTo(hand, 4);
                    weapon->SetPosInline(hand->GetPos() * hand->GetParent()->GetLocalMatrix());
                    weapon->SetWorldDir(S_vector(1.0f, 0, 0), 0.0f);
                    weapon->Update();
                }
            }
            holster->SetPosInline(S_vector(0.075f, -0.023f, -0.004f));
            holster->Update();
        }
        GetGEffects()->SetModelLightLevel(copy, 1.0f);
        g_scriptModule.RegisterDuplicate(copy);
        break;
    }

    case OP_CSC_ReleaseHuman: {
        if (g_scriptModule.IsCutsceneRunning()) {
            I3D_frame *&var = frameArg(0);
            if (I3D_frame *copy = var) {
                copy->Release();
                var = nullptr;
                g_scriptModule.UnregisterAnimatedModel(copy);
                g_scriptModule.RestoreFrame(copy);
                g_scriptModule.UnregisterDuplicate(copy);
            }
        }
        break;
    }

    case OP_CSC_FreeHands: {
        I3D_frame *model = frameArg(0);
        if (!model)
            break;
        if (I3D_frame *hw1 = model->FindChildFrame("hw1", 0xFFFF))
            GetGEffects()->SetHierarchyOn(hw1, false, 1);
        I3D_frame *hw2 = model->FindChildFrame("hw2", 0xFFFF);
        if (hw2)
            GetGEffects()->SetHierarchyOn(hw2, false, 1);
        break;
    }

    case OP_CSC_FreeBackPack: {
        I3D_frame *model = frameArg(0);
        I3D_frame *pack = model ? model->FindChildFrame("d", 0xFFFF) : nullptr;
        if (!pack)
            break;
        for (int i = pack->NumChildren(); i-- > 0;) {
            I3D_frame *child = pack->GetChild(i);
            if (child->GetType() == FRAME_VISUAL)
                GetGEffects()->HideVisual(child, false);
        }
        break;
    }

    case OP_CSC_FrameSetShadow: {
        I3D_frame *frame = frameArg(0);
        if (!frame || frame->GetType() != FRAME_MODEL)
            break;
        if (boolArg(1)) {
            GetGEffects()->CreateShadow(frame);
        } else if (I3D_frame *shadow = GetGEffects()->GetShadow(frame)) {
            shadow->SetOn(false);
            shadow->Update();
            shadow->Release();
        }
        break;
    }

    // ------------------------------------------------------------------ weather
    case OP_WEATHER_SetOn: {
        const bool on = boolArg(0);
        SendScriptValue(SCRIPT_MSG_WEATHER_ON, on, 1);
        if (!(g_gameRole & GAME_ROLE_CLIENT))
            GetGame()->GetScene()->SetWeather(WEATHER_ON, on);
        break;
    }

    case OP_WEATHER_SetCount: {
        const int count = numberArg(0);
        SendScriptValue(SCRIPT_MSG_WEATHER_COUNT, count, 32);
        if (!(g_gameRole & GAME_ROLE_CLIENT))
            GetGame()->GetScene()->SetWeather(WEATHER_COUNT, count);
        break;
    }

    case OP_WEATHER_SetSpeed: {
        float speed = numberArg(0) * 0.001f;                  // mm/s -> m/s
        if (speed < 0.0f)
            speed = 0.0f;
        else if (speed > 200.0f)
            speed = 200.0f;
        SendScriptValue(SCRIPT_MSG_WEATHER_SPEED, FloatBits(speed), 32);
        if (!(g_gameRole & GAME_ROLE_CLIENT))
            GetGame()->GetScene()->SetWeather(WEATHER_SPEED, FloatBits(speed));
        break;
    }

    case OP_THUNDERSTORM_SetOn: {
        const bool on = boolArg(0);
        I3D_frame *light = frameArg(1);
        if (light && light->GetType() != FRAME_LIGHT)
            light = nullptr;
        GetGEffects()->SetThunderstorm(on, light, nullptr);
        break;
    }

    case OP_THUNDERSTORM_MP:
        SendScriptValue(SCRIPT_MSG_THUNDERSTORM, numberArg(0), 16);
        Advance();
        return false;

    case OP_WEATHER_SetSize: {
        float length = numberArg(0) * 0.001f;
        float width = numberArg(1) * 0.001f;
        length = length < 0.0001f ? 0.0001f : (length > 20.0f ? 20.0f : length);
        width = width < 0.0f ? 0.0f : (width > 500.0f ? 500.0f : width);
        if (C_BitStream *out = BeginScriptNetMessage(SCRIPT_MSG_WEATHER_SIZE, true)) {
            out->WriteBits(FloatBits(length), 32);
            out->WriteBits(FloatBits(width), 32);
            SendScriptNetMessage(out);
        }
        if (!(g_gameRole & GAME_ROLE_CLIENT)) {
            GetGame()->GetScene()->SetWeather(WEATHER_LENGTH, FloatBits(length));
            GetGame()->GetScene()->SetWeather(WEATHER_WIDTH, FloatBits(width));
        }
        break;
    }

    case OP_WEATHER_SetColorIn:
    case OP_WEATHER_SetColorOut: {
        // Arguments are A, B, G, R; packed as 0xAABBGGRR... in argument order.
        const uint32_t color = (static_cast<uint32_t>(Clamp255(numberArg(3))) << 24) |
                               (static_cast<uint32_t>(Clamp255(numberArg(2))) << 16) |
                               (static_cast<uint32_t>(Clamp255(numberArg(1))) << 8) |
                               static_cast<uint32_t>(Clamp255(numberArg(0)));
        const bool in = cmd.opcode == OP_WEATHER_SetColorIn;
        SendScriptValue(in ? SCRIPT_MSG_WEATHER_COLOR_IN : SCRIPT_MSG_WEATHER_COLOR_OUT, color, 32);
        if (!(g_gameRole & GAME_ROLE_CLIENT))
            GetGame()->GetScene()->SetWeather(in ? WEATHER_COLOR_IN : WEATHER_COLOR_OUT, color);
        break;
    }

    case OP_WEATHER_Debug: {
        const int value = numberArg(0);
        if (GetGame()->GetScene())
            GetGame()->GetScene()->SetWeather(WEATHER_DEBUG, value);
        break;
    }

    case OP_WEATHER_SetMode: {
        const int mode = numberArg(0);
        SendScriptValue(SCRIPT_MSG_WEATHER_MODE, mode == 1, 1);
        if (!(g_gameRole & GAME_ROLE_CLIENT))
            GetGame()->GetScene()->SetWeather(WEATHER_MODE, mode);
        break;
    }

    case OP_WEATHER_SetSpace: {
        const int distance = numberArg(0);
        const int height = numberArg(1);
        if (C_BitStream *out = BeginScriptNetMessage(SCRIPT_MSG_WEATHER_SPACE, true)) {
            out->WriteBits(distance, 32);
            out->WriteBits(height, 32);
            SendScriptNetMessage(out);
        }
        if (!(g_gameRole & GAME_ROLE_CLIENT)) {
            GetGame()->GetScene()->SetWeather(WEATHER_DISTANCE, FloatBits(static_cast<float>(distance)));
            GetGame()->GetScene()->SetWeather(WEATHER_HEIGHT, FloatBits(static_cast<float>(height)));
        }
        break;
    }

    case OP_WEATHER_SetDir: {
        const S_vector dir = S_vector(static_cast<float>(numberArg(0)), static_cast<float>(numberArg(1)),
                                      static_cast<float>(numberArg(2))).Normalized();
        if (C_BitStream *out = BeginScriptNetMessage(SCRIPT_MSG_WEATHER_DIR, true)) {
            out->WriteBits(FloatBits(dir.x), 32);
            out->WriteBits(FloatBits(dir.y), 32);
            out->WriteBits(FloatBits(dir.z), 32);
            SendScriptNetMessage(out);
        }
        if (!(g_gameRole & GAME_ROLE_CLIENT)) {
            GetGame()->GetScene()->SetWeather(WEATHER_DIR_X, FloatBits(dir.x));
            GetGame()->GetScene()->SetWeather(WEATHER_DIR_Y, FloatBits(dir.y));
            GetGame()->GetScene()->SetWeather(WEATHER_DIR_Z, FloatBits(dir.z));
        }
        break;
    }

    // ---------------------------------------------------------- debug commands
    case OP_FRM_TimerSetOn:
    case OP___Log:
    case OP___LogNumber:
    case OP___LogFrame:
        // Stripped from the release build.
        Advance();
        return false;

    case OP___SetupAutoLogging:
        break;

    default:
        // Actor commands (HUMAN_*, MELEE_TRACKER_*) and the handler / declaration opcodes.
        return false;
    }

    // Commands that break out of the switch are done: continue with the next one.
    Advance();
    return true;
}
