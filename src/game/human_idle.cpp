// The idle behaviour of a human's body and its interactions with the world (C_HumanIdle,
// C_HumanMovement +0xCB0; original source 0x00750940..0x00758050).
//
// Update: the standing fidgets of the player's soldiers, the swimming bob, the breath vapour of
// cold missions. Interact: what the controllers and the scripts ask the body to do with
// something (E_Interaction): doors, vehicle seats, chairs, healing, manipulating objects, tying
// up, cutting, backstabs. A human simulated here plays them at once; the other side replays
// them from the steps queued in C_HumanMovement::m_remoteActions (the stance to take, the
// animation, putting the hands item away). UpdateInteraction then puts the body into a seat and
// takes it out again.
//
// (0x00750940 / 0x00750a90 and 0x00757240..0x00758020 are std::deque<S_RemoteAction> code;
// 0x00755800..0x00755930 inline accessors of the work, the movement and the actors.)
#include "game/human_movement.h"

#include <windows.h>

#include <cstddef>
#include <cstdlib>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_anim_tables.h"
#include "game/human_body.h"
#include "game/human_equipment.h"
#include "game/human_internal.h"
#include "game/items.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"

static_assert(sizeof(C_HumanIdle) == 0x20, "C_HumanIdle layout");
static_assert(offsetof(C_HumanLocomotion, m_waterTestTime) == 0x94, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_velocity) == 0xB0, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_fallStartY) == 0xFC, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_steadyTurnTime) == 0x130, "C_HumanLocomotion layout");

namespace {

constexpr int ANIM_STAND = 0x2D;
// The crouching / lying idle (the stance picks the variant).
constexpr int ANIM_LOW = 0x37;
constexpr int ANIM_SIT_DOWN = 0x1C;                     // "sednuti"
constexpr int ANIM_STAND_UP = 0x1D;
constexpr int ANIM_STAND_UP_FAST = 0x26;
constexpr int ANIM_TIED_UP = 0x62;                      // "svazanej"
constexpr int ANIM_IN_SUBMARINE = 0x6F;                 // "naponorce"
constexpr int ANIM_ON_MOTORBIKE = 0x9B;                 // "namotorce"
constexpr int ANIM_DRIVER = 0x9E;                       // "ridic"
constexpr int ANIM_PASSENGER = 0x9F;                    // "spol"
constexpr int ANIM_SITTING = 0x22;                      // "sedim1"
constexpr int ACTIVITY_CONTROLS = 0x46;                 // hands on the controls
constexpr int ACTIVITY_SEAT_WEAPON = 0x47;              // at a seat weapon without a pose
constexpr int ACTIVITY_FADE_OUT = 0x4C;
constexpr int CARRY_ANIM_PUT_AWAY = 0x5C;

// The work requests an interaction blocks (C_HumanWork::m_blocked2).
constexpr uint32_t BLOCK_BUSY = WORK_REQUEST_ANIM | WORK_REQUEST_ACTIVITY | WORK_REQUEST_STANCE | WORK_REQUEST_LOOK_AT |
                                WORK_REQUEST_CARRY_MODE | WORK_REQUEST_ACTION | WORK_REQUEST_FACING;
constexpr uint32_t BLOCK_SEAT = WORK_REQUEST_ANIM | WORK_REQUEST_ACTIVITY | WORK_REQUEST_STANCE | WORK_REQUEST_LOOK_AT;

// Remote actions an interaction queues (E_RemoteAction; C_HumanMovement::ProcessRemoteActions).
enum : int {
    REMOTE_HEAL = 5,                                    // animation 0x19 "medik01"
    REMOTE_MANIPULATE = 6,                              // animation 4 "manipul"
    REMOTE_MANIPULATE_LONG = 8,                         // animation 5 "manipulm" / 0x59 "potapManipul"
    REMOTE_TIE_UP = 9,                                  // animation 6 "svazuje1"
    REMOTE_BE_TIED_UP = 10,                             // animation 7 "svazovanej"
    REMOTE_CUT = 0x0B,                                  // animation 0x27 "strihaLOOP"
    REMOTE_PUT_AWAY = 0x0C,                             // the hands item
    REMOTE_BACKSTAB = 0x10,                             // carry animation 0x47 "podrezSTa"
    REMOTE_BE_BACKSTABBED = 0x11,
    REMOTE_ANIM_28 = 0x12,                              // animation 0x28
};
// Phases of the remote actions (S_RemoteAction::phase).
enum : int {
    REMOTE_PHASE_DROP = 0,                              // dropped when it took place
    REMOTE_PHASE_WAIT = 1,                              // waits for its time
    REMOTE_PHASE_KEEP = 2,                              // kept as done (3) when it took place
    REMOTE_PHASE_DONE = 3,
};

// Particle effects of the breath vapour (standing; moving: attached to the head).
constexpr int PARTICLE_BREATH = 0x11;
constexpr int PARTICLE_BREATH_MOVING = 0x4C;

// The dummies of a human model the scene needs for its special handling.
const char HUMAN_MODEL_DUMMY_Q[] = "q";                 // 0x00839398
const char HUMAN_MODEL_DUMMY_Z[] = "z";                 // 0x0083939c
// The dummy of a chair where the body ends up when it gets up.
const char CHAIR_EXIT_DUMMY[] = "__*";                  // 0x0084e878
constexpr uint32_t CHAIR_EXIT_DUMMY_MASK = 0x10020;

const float PI = 3.14159274f;                           // 0x40490fdb
const float TWO_PI = 6.28318548f;                       // 0x0080f94c

enum : uint16_t {
    CHUNK_IDLE = 0x6F,
    CHUNK_IDLE_STAND_TIME = 0x07,
    CHUNK_IDLE_FIDGET_TIME = 0x08,
};

bool InRange(int value, int low, int high) { return value >= low && value <= high; }

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

// The world position of a frame with its w (the original copies the matrix row).
S_vector4 WorldPos4(I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

// The body of `human` back in the scene's human models (out of a seat).
void AddHumanModel(I3D_frame *model)
{
    I3D_scene *scene = GetGame()->GetScene();
    scene->AddHumanModel(model, model->FindChildFrame(HUMAN_MODEL_DUMMY_Q, 0xFFFF),
                         model->FindChildFrame(HUMAN_MODEL_DUMMY_Z, 0xFFFF));
}

// The camera of the controlled soldier follows it again.
void UpdateActivePlayerCamera(const C_Human *human)
{
    if (IsClient() && g_teamManager.GetPlayerTeam()->GetActivePlayer() == human)
        g_teamManager.GetPlayerTeam()->UpdateCameraTarget();
}

// The equipment models of places 1..3 shown / hidden (the hands item model by `hands`).
void ShowEquipmentModels(C_HumanEquipment *equipment, bool hands, bool others)
{
    if (I3D_frame *model = equipment->GetModel(3))
        model->SetOn(hands);
    if (I3D_frame *model = equipment->GetModel(1))
        model->SetOn(others);
    if (I3D_frame *model = equipment->GetModel(2))
        model->SetOn(others);
}

}  // namespace

// ------------------------------------------------------------------ setting up

void C_HumanIdle::Reset()
{
    m_standTime = 0;
    m_fidgetTime = MOVE_NO_TIME;
    m_vapourTime = std::rand() % 8000 + 5000;
}

bool C_HumanIdle::SaveStatics(C_chunk & /*chunk*/)
{
    return true;
}

bool C_HumanIdle::LoadStatics(C_chunk & /*chunk*/)
{
    return true;
}

// ------------------------------------------------------------------ the update

void C_HumanIdle::Update(int time)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    const int anim = movement->m_animState.anim.anim;
    const int next = movement->m_nextAnimState.anim.anim;
    if (!InRange(anim, 0x29, 0x41) || m_owner->GetRole() != HUMAN_ROLE_PLAYER ||
        (!InRange(next, 0x29, 0x41) && next != HUMAN_ANIM_NONE)) {
        // Not standing still.
        m_fidgetTime = MOVE_NO_TIME;
        m_standTime = 0;
        // Swimming in place: the body bobs (the locomotion adds the sine of the phase).
        const C_HumanWork *ownerWork = movement->m_owner->GetWork();
        if (ownerWork->m_mode == WORK_MODE_SWIMMING && (anim == 0x5B || anim == 0x5D) && (next == 0x5B || next == 0x5D)) {
            float &phase = movement->m_swimBobPhase;
            if (m_locomotion->m_waterHeight >= 0.2f) {
                phase = static_cast<float>(time) * 0.000833333354f + phase;
            } else {
                if (phase == 0.0f)
                    phase = PI;
                phase = static_cast<float>(time) * 0.00125f + phase;
            }
            if (phase > TWO_PI)
                phase -= TWO_PI;
        } else {
            movement->m_swimBobPhase = 0.0f;
        }
    } else {
        // Standing still: a fidget every 15.5..25.5 s (the first after 15.5..40.5 s).
        if (m_standTime == 0)
            m_fidgetTime = std::rand() % 25000 + 15500;
        m_standTime += time;
        if (m_standTime > 100000000)
            m_standTime = 1;
        m_fidgetTime -= time;
        if (m_fidgetTime < 1 && (work->m_request.unknown5C == 0 || work->m_request.carryMode == 0)) {
            m_fidgetTime = std::rand() % 10000 + 15500;
            S_HumanAnimRef &asked = movement->m_nextAnimState.anim;
            switch (movement->m_animState.anim.stance) {
            case HUMAN_STANCE_STAND:
                asked.anim = std::rand() % 7 + 0x2F;
                asked.Normalize();
                break;
            case HUMAN_STANCE_CROUCH:
                asked.anim = std::rand() % 5 + 0x39;
                asked.Normalize();
                break;
            case HUMAN_STANCE_PRONE:
                asked.anim = 0x41;
                asked.Normalize();
                break;
            default:
                break;
            }
        }
    }

    // Breath vapour in the cold (above water, not faded out; not for the soldier seen in the
    // first person).
    if (!g_coldMission || m_locomotion->m_waterSurface != MOVE_NO_VALUE || !(movement->m_flags & MOVE_FLAG_FULL_DETAIL) ||
        m_owner->IsFadedOut())
        return;
    if (GetGame()->IsFirstPersonView() && g_teamManager.GetPlayerTeam()->GetActivePlayer() == m_owner)
        return;
    m_vapourTime -= time;
    if (m_vapourTime >= 1)
        return;
    bool moving = false;
    if (movement->m_animState.anim.anim < HUMAN_ANIM_FIRST_6X) {
        m_vapourTime = std::rand() % 1500 + 3500;
    } else {
        // Moving: running and marching pant without vapour (only the timer).
        switch (work->m_request.gait) {
        case 0:
            m_vapourTime = std::rand() % 750 + 3500;
            break;
        case 1:
        case 2:
            m_vapourTime = std::rand() % 250 + 1600;
            return;
        case 5:
            m_vapourTime = std::rand() % 250 + 1300;
            return;
        default:
            m_vapourTime = std::rand() % 750 + 3200;
            break;
        }
        moving = true;
    }
    // Only outdoors (the model in the primary sector).
    I3D_frame *model = m_owner->GetFrame();
    if (model->GetParent() != reinterpret_cast<I3D_frame *>(GetGame()->GetScene()->GetPrimarySector()))
        return;
    I3D_frame *head = movement->m_joints[HUMAN_JOINT_F1].frame;
#ifdef HD2_DEDICATED   // no particles on the server; it still reads the head's matrix
    (void)moving;
    (void)head->GetWorldDir();
    (void)head->GetWorldPos();
#else
    const S_vector dir = head->GetWorldDir();
    const S_vector pos = head->GetWorldPos() + dir * 0.15f;
    g_particlesModule.CreateAt(moving ? PARTICLE_BREATH_MOVING : PARTICLE_BREATH, pos, dir, moving ? head : nullptr, true,
                               true);
#endif
}

// ------------------------------------------------------------------ the interactions

void C_HumanIdle::Interact(C_Actor *target, uint32_t interaction, int arg)
{
    OutputDebugStringA("Akce!!!!!!!!!!!!!!!!!!!!!\n");
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    std::deque<S_RemoteAction> &remote = movement->m_remoteActions;
    const uint32_t code = interaction & INTERACT_CODE_MASK;

    const auto push = [&remote](int time, int action, int phase) {
        S_RemoteAction step;
        step.time = time;
        step.action = action;
        step.phase = phase;
        remote.push_back(step);
    };
    // A new interaction on a client drops what is left of the remote steps (when the
    // movement's clock has not run yet; as in the original).
    const auto restart = [&]() {
        if (!IsServer() && !m_owner->IsLocalPlayer() && movement->m_clock == 0)
            remote.clear();
    };
    const auto frontIs = [&remote](int action) { return !remote.empty() && remote.front().action == action; };
    // The steps of an interaction where the body takes the stance `need` for the animation of
    // the remote action `action` and back (and puts the hands item away after it).
    const auto queueSteps = [&](int action, int need) {
        const int stance = work->m_request.stance;
        if (stance == need) {
            push(10, action, REMOTE_PHASE_KEEP);
            push(MoveTime(500.0f), REMOTE_PUT_AWAY, REMOTE_PHASE_WAIT);
            return;
        }
        push(MoveTime(2500.0f), need, REMOTE_PHASE_DROP);
        push(MoveTime(150.0f), action, REMOTE_PHASE_KEEP);
        if (stance >= HUMAN_STANCE_STAND && stance <= HUMAN_STANCE_PRONE)
            push(MoveTime(2500.0f), stance, REMOTE_PHASE_DROP);
        push(MoveTime(500.0f), REMOTE_PUT_AWAY, REMOTE_PHASE_DROP);
    };
    // The body stops where it is.
    const auto stop = [&]() {
        const S_vector4 still = {0.0f, 0.0f, 0.0f, 0.0f};
        movement->m_mixer.SetVelocity(still, 0.0f, 0.0f, &m_locomotion->m_velocity);
        movement->m_mixer.m_move.x = 0.0f;
        movement->m_mixer.m_move.y = 0.0f;
        movement->m_mixer.m_move.z = 0.0f;
    };
    // The direction from the body to `to` (normalized).
    const auto towards = [&](const C_Actor *to) { return (XYZ(to->GetPosition4()) - XYZ(m_owner->GetPosition4())).Normalized(); };
    // The end of an interaction: the work takes requests again (the steps of the start are
    // dropped by the caller), the body stands, the locomotion waits a moment.
    const auto standUp = [&](int animation) {
        work->m_forced = true;
        work->Play(animation, true);
        work->m_forced = false;
    };
    const auto finish = [&]() { m_locomotion->m_steadyTurnTime = MoveTime(500.0f); };

    switch (code) {
    case INTERACT_DOOR: {
        // Opening (arg 1) or closing a door: from the side the body stands on.
        movement->m_seatMode = 0;
        work->m_blocked2 |= WORK_REQUEST_INTERACT;
        movement->m_seat = arg;
        movement->m_interactionTime = MOVE_NO_TIME;
        movement->m_interaction = interaction;
        movement->m_vehicle = target;
        movement->m_interactionTime = 0;
        C_Door *door = static_cast<C_Door *>(target);
        const int side = door->GetSide(m_owner->GetPosition4());
        if (side == 2 || movement->m_useState != MOVE_NO_TIME) {
            // A door without sides, or on a ladder: the door itself (on the server).
            if (IsServer())
                door->SetState(movement->m_seat != 0 ? 2 : 3, true, false, m_owner->GetRole() != HUMAN_ROLE_PLAYER);
            return;
        }
        if (side != 0 && side != 1)
            return;
        // Animations 0 / 1 open the door, 2 / 3 close it ("dvere").
        const int animation = (arg != 0 ? 0 : 2) + side;
        movement->m_nextAnimState.anim.anim = animation;
        movement->m_nextAnimState.anim.Normalize();
        movement->m_interactionTime = movement->GetAnimLength(animation, 0, 1);
        return;
    }

    case INTERACT_ENTER_CAR:
    case INTERACT_ENTER_SUBMARINE:
    case INTERACT_ENTER_STATIC_WEAPON:
    case INTERACT_ENTER_JUNKERS88:
    case INTERACT_ENTER_SEAT_D: {
        if (movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING)
            m_locomotion->StopSwimming(false);
        // (The direction bits of the move mode are always 0 here: the controller's move flags
        // are cleared.)
        m_owner->SetMoveFlags(m_owner->GetMoveMode() & MOVE_STANCE_MASK);
        // Fix: no seat to swap with unless the human changes seats (the original reads the
        // previous seat from an uninitialised stack slot, sending a living occupant to a
        // random seat).
        int previousSeat = -1;
        const uint32_t previous = movement->m_interaction & INTERACT_CODE_MASK;
        if (previous == INTERACT_ENTER_CAR || previous == INTERACT_ENTER_SUBMARINE ||
            previous == INTERACT_ENTER_JUNKERS88 || previous == INTERACT_ENTER_SEAT_D) {
            // From one seat to another: out of the old one at once.
            target->LeaveSeat(movement->m_seat);
            AddHumanModel(movement->m_owner->GetFrame());
            movement->m_interactionTime = -1;
            previousSeat = movement->m_seat;
        } else {
            movement->m_interactionTime = m_owner->IsLocalPlayer() && IsLocalGame() ? 500 : 5;
        }
        work->m_blocked2 |= WORK_REQUEST_INTERACT;
        movement->m_seat = arg;
        movement->m_seatMode = static_cast<int>(target->GetSeatFlags(movement->m_seat));
        movement->m_interaction = interaction;
        movement->m_vehicle = target;
        movement->m_seatStance = work->m_request.stance;
        movement->m_mixer.ClearQueue(0);
        work->m_blocked2 |= BLOCK_SEAT;
        m_animPlayer->m_animDelay = MOVE_NO_TIME;
        m_animPlayer->m_animDelayState = 0;
        m_animPlayer->m_carryDelay = MOVE_NO_TIME;
        m_animPlayer->m_carryDelayState = 0;
        m_animPlayer->m_activityDelay = MOVE_NO_TIME;
        m_animPlayer->m_activityDelayState = 0;
        // Someone in the seat: a body is taken out, the living move to the old seat (or get
        // out).
        C_Actor *occupant = movement->m_vehicle->GetSeatOccupant(movement->m_seat);
        if (occupant && occupant->GetType() == ACTOR_HUMAN) {
            C_Human *other = static_cast<C_Human *>(occupant);
            const S_vector4 pos = WorldPos4(other->GetFrame());
            if (other->GetHealth() <= 0) {
                movement->m_vehicle->LeaveSeat(movement->m_seat);
                // (The original looks the "q" dummy up in this human's model instead of the
                // body's; kept.)
                I3D_frame *body = other->GetFrame();
                GetGame()->GetScene()->AddHumanModel(body, m_owner->GetFrame()->FindChildFrame(HUMAN_MODEL_DUMMY_Q, 0xFFFF),
                                                     body->FindChildFrame(HUMAN_MODEL_DUMMY_Z, 0xFFFF));
                other->ClearVehicle();
                other->GetMovement()->SetPosition(pos);
                // (This human's model, as in the original.)
                GetGame()->GetScene()->MarkTrackedModel(m_owner->GetFrame());
            } else if (previousSeat >= 0) {
                other->GetMovement()->m_idle.Interact(target, interaction, previousSeat);
            } else {
                other->GetMovement()->m_idle.Interact(
                    target, (interaction & ~(INTERACT_CODE_MASK & ~3u)) | INTERACT_LEAVE_CAR, movement->m_seat);
            }
        }
        if (!m_owner->IsLocalPlayer() || !IsLocalGame()) {
            movement->m_interactionTime = 0;
            UpdateInteraction(100);
        }
        return;
    }

    case INTERACT_LEAVE_CAR:
    case INTERACT_LEAVE_SUBMARINE:
    case INTERACT_LEAVE_STATIC_WEAPON:
    case INTERACT_LEAVE_JUNKERS88:
        work->m_blocked2 |= WORK_REQUEST_INTERACT;
        {
            const S_vector4 pos = WorldPos4(m_owner->GetFrame());
            m_owner->SetPosition(XYZ(pos), pos.w);
        }
        movement->m_seat = arg;
        movement->m_interaction = interaction;
        m_locomotion->m_fallStartY = 100000.0f;
        if (m_owner->IsLocalPlayer() && IsLocalGame()) {
            movement->m_interactionTime = 500;
            return;
        }
        // (Past 10000: out at once.)
        movement->m_interactionTime = 10001;
        UpdateInteraction(100);
        return;

    case INTERACT_SIT: {
        movement->m_seatMode = 5;
        const int mode = movement->m_owner->GetWork()->m_mode;
        const int anim = movement->m_animState.anim.anim;
        if (mode == 2 || mode == 3 || anim == ANIM_PASSENGER || anim == ANIM_DRIVER || anim == ANIM_ON_MOTORBIKE ||
            anim == ANIM_IN_SUBMARINE || anim == ANIM_SITTING || anim == ANIM_STAND_UP || anim == ANIM_STAND_UP_FAST)
            return;
        work->m_blocked2 |= WORK_REQUEST_INTERACT;
        movement->m_seatFrame = reinterpret_cast<I3D_frame *>(static_cast<intptr_t>(arg));
        if (interaction & INTERACT_FLAG_BODY) {
            movement->m_nextAnimState.anim.anim = ANIM_SIT_DOWN;
            movement->m_nextAnimState.anim.Normalize();
            work->Play(ANIM_SIT_DOWN, false);
            movement->m_nextAnimState.anim.stance = HUMAN_STANCE_STAND;
            movement->m_nextAnimState.anim.Normalize();
            work->SetStance(HUMAN_STANCE_STAND, false);
        }
        movement->m_mixer.ClearQueue(0);
        movement->m_interaction = interaction;
        movement->m_interactionTime = movement->GetAnimLength(ANIM_SIT_DOWN, 0, 1);
        movement->m_vehicle = target;
        return;
    }

    case INTERACT_STAND_UP:
    case INTERACT_STAND_UP_FAST: {
        const int mode = movement->m_owner->GetWork()->m_mode;
        int anim = movement->m_animState.anim.anim;
        if (mode != 2 && mode != 3 && anim != ANIM_PASSENGER && anim != ANIM_DRIVER && anim != ANIM_ON_MOTORBIKE &&
            anim != ANIM_IN_SUBMARINE && anim != ANIM_SITTING)
            return;
        anim = movement->m_animState.anim.anim;
        if (anim == ANIM_STAND_UP || anim == ANIM_STAND_UP_FAST)
            return;
        work->m_blocked2 |= WORK_REQUEST_INTERACT;
        if (interaction & INTERACT_FLAG_BODY) {
            const int animation = code == INTERACT_STAND_UP_FAST ? ANIM_STAND_UP_FAST : ANIM_STAND_UP;
            movement->m_nextAnimState.anim.anim = animation;
            movement->m_nextAnimState.anim.Normalize();
            work->Play(animation, false);
        }
        movement->m_mixer.ClearQueue(0);
        movement->m_interaction = interaction;
        movement->m_interactionTime = MoveTime(900.0f);
        finish();
        return;
    }

    case INTERACT_HEAL:
    case INTERACT_HEAL_HERE: {
        if (remote.size() >= 12)
            return;
        restart();
        equipment->PutAwayHandsItem(true);
        stop();
        const S_vector dir = towards(target);
        work->SetFacingDir(dir, true);
        if (code == INTERACT_HEAL_HERE) {
            push(10, REMOTE_ACTION_CROUCH, REMOTE_PHASE_KEEP);
            const int stance = work->m_request.stance;
            if (stance == HUMAN_STANCE_STAND || stance == HUMAN_STANCE_PRONE)
                push(MoveTime(2500.0f), stance, REMOTE_PHASE_DROP);
            const C_HumanMovement *ownerMovement = m_owner->GetMovement();
            if (ownerMovement->m_unknownA30 <= 0 && ownerMovement->m_animState.anim.anim != 0x5F)
                push(MoveTime(500.0f), REMOTE_PUT_AWAY, REMOTE_PHASE_DROP);
        } else {
            // Up to the patient.
            const S_vector to = XYZ(target->GetPosition4()) - dir * 0.6f;
            movement->m_mixer.MoveTo({to.x, to.y, to.z, 0.0f}, 2.0f, 2000);
            queueSteps(REMOTE_HEAL, HUMAN_STANCE_CROUCH);
        }
        work->Play(ANIM_STAND, true);
        work->SetLookAt({0.0f, 0.0f, 0.0f, 0.0f}, true);
        work->m_blocked2 |= BLOCK_BUSY;
        return;
    }

    case INTERACT_HEAL_END:
        work->m_blocked2 &= ~BLOCK_BUSY;
        if (frontIs(REMOTE_ACTION_CROUCH)) {
            remote.pop_front();
            if (frontIs(REMOTE_HEAL))
                remote.pop_front();
            if (frontIs(REMOTE_ACTION_PRONE) || frontIs(REMOTE_ACTION_STAND))
                remote.pop_front();
        } else if (frontIs(REMOTE_HEAL)) {
            remote.pop_front();
        }
        standUp(ANIM_STAND);
        finish();
        return;

    case INTERACT_HEAL_HERE_END:
        work->m_blocked2 &= ~BLOCK_BUSY;
        if (frontIs(REMOTE_ACTION_CROUCH) && remote.front().phase == REMOTE_PHASE_KEEP) {
            remote.pop_front();
            if (frontIs(REMOTE_ACTION_PRONE) || frontIs(REMOTE_ACTION_STAND))
                remote.pop_front();
        } else if (frontIs(REMOTE_ACTION_CROUCH) && remote.front().phase == REMOTE_PHASE_DONE) {
            remote.pop_front();
        }
        finish();
        return;

    case INTERACT_24:
        if (remote.size() >= 12)
            return;
        restart();
        equipment->PutAwayHandsItem(true);
        stop();
        // (Not while in a seat or at a door.)
        if (movement->m_interaction == 0 || movement->m_interaction == INTERACT_DOOR)
            queueSteps(REMOTE_ANIM_28, HUMAN_STANCE_CROUCH);
        work->Play(ANIM_STAND, true);
        work->SetLookAt({0.0f, 0.0f, 0.0f, 0.0f}, true);
        work->m_blocked2 |= BLOCK_BUSY;
        return;

    case INTERACT_24_END:
        work->m_blocked2 &= ~BLOCK_BUSY;
        if (frontIs(REMOTE_ACTION_CROUCH)) {
            remote.pop_front();
            if (frontIs(REMOTE_ANIM_28))
                remote.pop_front();
            if (frontIs(REMOTE_ACTION_PRONE) || frontIs(REMOTE_ACTION_STAND))
                remote.pop_front();
        } else if (frontIs(REMOTE_ANIM_28)) {
            remote.pop_front();
        }
        if (movement->m_interaction == 0 || movement->m_interaction == INTERACT_DOOR)
            standUp(ANIM_STAND);
        finish();
        return;

    case INTERACT_MANIPULATE:
        if (remote.size() > 25)
            return;
        restart();
        equipment->PutAwayHandsItem(true);
        work->m_blocked2 |= BLOCK_BUSY;
        queueSteps(REMOTE_MANIPULATE, HUMAN_STANCE_CROUCH);
        return;

    case INTERACT_MANIPULATE_END:
        work->m_blocked2 &= ~BLOCK_BUSY;
        if (frontIs(REMOTE_MANIPULATE))
            remote.pop_front();
        standUp(ANIM_STAND);
        finish();
        return;

    case INTERACT_MANIPULATE_LONG:
    case INTERACT_MANIPULATE_LONG_2: {
        if (remote.size() > 15)
            return;
        restart();
        const bool swimming = movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING;
        if (!swimming)
            equipment->PutAwayHandsItem(true);
        work->m_blocked2 |= BLOCK_BUSY;
        if (movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING)
            push(10, REMOTE_MANIPULATE_LONG, REMOTE_PHASE_KEEP);
        else
            queueSteps(REMOTE_MANIPULATE_LONG, HUMAN_STANCE_CROUCH);
        return;
    }

    case INTERACT_MANIPULATE_LONG_END:
    case INTERACT_MANIPULATE_LONG_2_END:
        if (movement->m_owner->GetWork()->m_mode != WORK_MODE_SWIMMING)
            equipment->PutAwayHandsItem(false);
        work->m_blocked2 &= ~BLOCK_BUSY;
        if (frontIs(REMOTE_MANIPULATE_LONG))
            remote.pop_front();
        standUp(ANIM_STAND);
        finish();
        return;

    case INTERACT_TIE_UP:
    case INTERACT_BACKSTAB: {
        if (remote.size() > (code == INTERACT_TIE_UP ? 15u : 25u))
            return;
        restart();
        // Up behind / to the other one (0.9 m, the knife 0.5 m).
        const S_vector dir = towards(target);
        const S_vector to = XYZ(target->GetPosition4()) - dir * (code == INTERACT_TIE_UP ? 0.9f : 0.5f);
        movement->m_mixer.MoveTo({to.x, to.y, to.z, 0.0f}, 2.0f, 2000);
        work->SetFacingDir(dir, true);
        equipment->PutAwayHandsItem(true);
        work->m_blocked2 |= BLOCK_BUSY;
        queueSteps(code == INTERACT_TIE_UP ? REMOTE_TIE_UP : REMOTE_BACKSTAB, HUMAN_STANCE_STAND);
        return;
    }

    case INTERACT_TIE_UP_END:
    case INTERACT_BACKSTAB_END:
        work->m_blocked2 &= ~BLOCK_BUSY;
        if (frontIs(code == INTERACT_TIE_UP_END ? REMOTE_TIE_UP : REMOTE_BACKSTAB))
            remote.pop_front();
        standUp(ANIM_STAND);
        finish();
        return;

    case INTERACT_BE_TIED_UP:
    case INTERACT_BE_BACKSTABBED: {
        if (remote.size() > (code == INTERACT_BE_TIED_UP ? 25u : 15u))
            return;
        restart();
        if (code == INTERACT_BE_TIED_UP) {
            // Facing away from the captor (it ties the hands behind the back); the hands item goes
            // away.
            const S_vector dir = (XYZ(m_owner->GetPosition4()) - XYZ(target->GetPosition4())).Normalized();
            work->SetFacingDir(dir, true);
            movement->m_stanceVolumeScale = 0.45f;
            if (!IsServer()) {
                work->m_forced = true;
                work->SetFacingDir(dir, true);
                work->m_forced = false;
                movement->m_interaction = interaction;
                movement->m_unknown18C = true;
            }
            equipment->PutAwayHandsItem(true);
            movement->m_nextAnimState.carryAnim = CARRY_ANIM_PUT_AWAY;
            movement->m_nextAnimState.activity = ACTIVITY_FADE_OUT;
            work->PlayWorkAnimation(ACTIVITY_FADE_OUT, true);
            work->m_blocked2 |= BLOCK_BUSY;
        } else {
            // The attacker behind: facing away from it; the server kills.
            const S_vector dir = (XYZ(m_owner->GetPosition4()) - XYZ(target->GetPosition4())).Normalized();
            work->SetFacingDir(dir, true);
            movement->m_stanceVolumeScale = 0.45f;
            if (IsServer())
                m_owner->Hit(HIT_BACKSTAB, 1000.0f, movement->m_joints[HUMAN_JOINT_F1].frame->GetWorldPos(),
                             m_owner->GetDirection(), target, 0);
        }
        const int action = code == INTERACT_BE_TIED_UP ? REMOTE_BE_TIED_UP : REMOTE_BE_BACKSTABBED;
        if (work->m_request.stance == HUMAN_STANCE_STAND) {
            push(10, action, REMOTE_PHASE_KEEP);
        } else {
            push(MoveTime(2500.0f), REMOTE_ACTION_STAND, REMOTE_PHASE_DROP);
            push(MoveTime(150.0f), action, REMOTE_PHASE_KEEP);
        }
        return;
    }

    case INTERACT_BE_TIED_UP_END:
        // (The work stays blocked: the human is tied up.)
        if (frontIs(REMOTE_BE_TIED_UP))
            remote.pop_front();
        standUp(ANIM_TIED_UP);
        finish();
        return;

    case INTERACT_BE_BACKSTABBED_END:
        if (frontIs(REMOTE_BE_BACKSTABBED))
            remote.pop_front();
        return;

    case INTERACT_CUT: {
        if (remote.size() > 25)
            return;
        restart();
        equipment->PutAwayHandsItem(true);
        // The wire cutters (from the belt) in the hand.
        if (C_Item *cutters = m_owner->FindBeltItem(ITEM_BEHAVIOUR_WIRE_CUTTER))
            equipment->SetModel(cutters->GetFrame(0), 6, -1, -5, -1);
        work->m_blocked2 |= BLOCK_BUSY;
        queueSteps(REMOTE_CUT, HUMAN_STANCE_CROUCH);
        return;
    }

    case INTERACT_CUT_END:
        work->m_blocked2 &= ~BLOCK_BUSY;
        equipment->OnAnimationEvent(8);
        if (frontIs(REMOTE_CUT))
            remote.pop_front();
        standUp(ANIM_STAND);
        finish();
        return;

    default:
        return;
    }
}

// ------------------------------------------------------------------ the interaction under way

void C_HumanIdle::UpdateInteraction(int time)
{
    C_HumanMovement *movement = m_movement;
    if (movement->m_interaction == 0 || movement->m_interactionTime == MOVE_NO_TIME)
        return;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    const uint32_t interaction = movement->m_interaction;
    const uint32_t code = interaction & INTERACT_CODE_MASK;
    switch (code) {
    case INTERACT_DOOR:
        if (movement->m_interactionTime > 0)
            break;
        movement->m_interactionTime = MOVE_NO_TIME;
        movement->m_interaction = 0;
        work->m_blocked2 &= ~WORK_REQUEST_INTERACT;
        break;

    case INTERACT_ENTER_CAR:
    case INTERACT_ENTER_SUBMARINE:
    case INTERACT_ENTER_STATIC_WEAPON:
    case INTERACT_ENTER_JUNKERS88:
    case INTERACT_ENTER_SEAT_D: {
        // Into the seat once the timer ran out (within a second).
        if (movement->m_interactionTime <= -1000 || movement->m_interactionTime > 0 || !(interaction & INTERACT_FLAG_BODY))
            break;
        C_Actor *vehicle = movement->m_vehicle;
        GetGame()->GetScene()->RemoveHumanModel(movement->m_owner->GetFrame());
        C_Actor *occupant = vehicle->GetSeatOccupant(movement->m_seat);
        if (occupant && vehicle->GetSeatOccupant(movement->m_seat) != m_owner) {
            // Taken by someone else meanwhile: given up (the original leaves the model out of
            // the scene's human models).
            work->m_blocked2 &= ~(BLOCK_SEAT | WORK_REQUEST_INTERACT);
            movement->m_seatMode = 0;
            movement->m_interactionTime = MOVE_NO_TIME;
            movement->m_interaction = 0;
            movement->m_vehicle = nullptr;
            movement->m_stillTime = 0;
            return;
        }
        vehicle->SetSeatOccupant(movement->m_seat, m_owner);
        m_owner->SetVehicle(movement->m_vehicle, movement->m_seat);
        // The pose of the seat.
        const uint32_t seat = static_cast<uint32_t>(movement->m_seatMode);
        S_HumanAnimRef &asked = movement->m_nextAnimState.anim;
        int stance = HUMAN_STANCE_STAND;
        if (seat & SEAT_FLAG_STAND) {
            asked.anim = ANIM_STAND;
        } else if (seat & SEAT_FLAG_CROUCH) {
            stance = HUMAN_STANCE_CROUCH;
            asked.anim = ANIM_LOW;
        } else if (seat & SEAT_FLAG_LIE) {
            stance = HUMAN_STANCE_PRONE;
            asked.anim = ANIM_LOW;
        } else if (seat & SEAT_FLAG_ASTRIDE) {
            asked.anim = code == INTERACT_ENTER_SUBMARINE ? ANIM_IN_SUBMARINE : ANIM_ON_MOTORBIKE;
        } else {
            asked.anim = (seat & SEAT_FLAG_CONTROLS) ? ANIM_DRIVER : ANIM_PASSENGER;
        }
        asked.Normalize();
        asked.stance = stance;
        asked.Normalize();
        work->m_forced = true;
        work->Play(asked.anim, false);
        movement->m_request.stance = asked.stance;
        work->SetStance(asked.stance, false);
        movement->m_weaponPose2->SetSeatStance(asked.stance);
        work->m_forced = false;
#ifndef HD2_DEDICATED   // no weapon pose seats on the server
        movement->m_weaponPose2->SetSeat(static_cast<uint32_t>(movement->m_seatMode), movement->m_vehicle, movement->m_seat);
#endif
        // (The same either way in the original.)
        movement->m_hitVolumeScale[0] = 0.0f;
        movement->m_hitVolumeScale[1] = 0.0f;
        // What the hands do in the seat.
        bool removeShadow = true;
        int &mode = movement->m_seatMode;
        S_HumanAnimState &next = movement->m_nextAnimState;
        const auto playActivity = [&]() {
            work->m_forced = true;
            work->PlayWorkAnimation(next.activity, false);
            work->m_forced = false;
        };
        if ((mode & SEAT_FLAG_CONTROLS) && !movement->m_vehicle->HasSeatWeapon(movement->m_seat)) {
            next.activity = ACTIVITY_CONTROLS;
            next.activityVariant = 0;
            playActivity();
            equipment->PutAwayHandsItem(true);
            mode = 1;
        } else if (mode & SEAT_FLAG_WEAPON) {
            // The gunner's pose kind: the weapon component's projectile type (0..2, the column of
            // g_seatWeaponAnimTable: 0 "mg42", 1 "w97").
            C_ItemDef *weapon = movement->m_vehicle->GetSeatWeaponDef(movement->m_seat);
            mode = weapon ? weapon->GetComponent()->GetProjectileType() + 6 : 9;
            if (mode >= 9) {
                next.activity = ACTIVITY_SEAT_WEAPON;
                next.activityVariant = 0;
                mode = 3;
            } else {
                next.activity = ACTIVITY_FADE_OUT;
                // The gunner's pose (none for a passenger's seat weapon).
                I3D_animation_set *pose = nullptr;
                if (next.anim.anim != ANIM_PASSENGER) {
                    pose = g_seatWeaponAnimTable->GetAnim(mode - 6 + stance * 3);
                    if (pose)
                        removeShadow = false;
                }
#ifndef HD2_DEDICATED   // no weapon pose seats on the server
                movement->m_weaponPose2->SetSeatWeaponPose(pose);
#endif
            }
            playActivity();
            equipment->PutAwayHandsItem(true);
        } else if (!(mode & (SEAT_FLAG_CONTROLS | SEAT_FLAG_STAND | SEAT_FLAG_CROUCH | SEAT_FLAG_HIDDEN)) &&
                   (mode & SEAT_FLAG_ARMED)) {
            equipment->PutAwayHandsItem(false);
            next.activity = ACTIVITY_FADE_OUT;
            playActivity();
            mode = 2;
        } else if ((mode & (SEAT_FLAG_STAND | SEAT_FLAG_CROUCH)) && (mode & SEAT_FLAG_ARMED)) {
            next.activity = ACTIVITY_FADE_OUT;
            equipment->PutAwayHandsItem(false);
            mode = 4;
        } else {
            next.activity = ACTIVITY_FADE_OUT;
            mode = 5;
            equipment->PutAwayHandsItem(true);
        }
        movement->m_interactionTime = -2000;
        movement->m_stanceVolumeScale = 0.0f;
        m_animPlayer->Update();
        movement->m_mixer.UpdateStages(2000, 2000, false, nullptr);
        const bool keepsItem = mode == 2 || mode == 4;
        if (keepsItem)
            work->m_blocked2 &= ~WORK_REQUEST_UNKNOWN5C;
        else
            work->m_blocked2 |= WORK_REQUEST_UNKNOWN5C;
        work->m_blocked2 |= WORK_REQUEST_FACING;
        work->m_blocked2 |= WORK_REQUEST_STANCE;
        if (I3D_frame *model = equipment->GetModel(3))
            model->SetOn(keepsItem);
        if (I3D_frame *model = equipment->GetModel(1))
            model->SetOn(false);
        if (I3D_frame *model = equipment->GetModel(2))
            model->SetOn(false);
        if (movement->m_shadow && removeShadow)
            m_owner->GetMovement()->m_visual.RemoveShadow();
        UpdateActivePlayerCamera(m_owner);
        movement->m_interactionTime = MOVE_NO_TIME;
        work->m_blocked2 &= ~WORK_REQUEST_INTERACT;
        if (mode >= 3 && mode <= 4)
            work->m_blocked2 &= ~WORK_REQUEST_FACING;
        if (movement->m_vehicle->GetSeatFlags(movement->m_seat) & SEAT_FLAG_HIDDEN)
            m_owner->GetFrame()->SetOn(false);
        break;
    }

    case INTERACT_LEAVE_CAR:
    case INTERACT_LEAVE_SUBMARINE:
    case INTERACT_LEAVE_STATIC_WEAPON:
    case INTERACT_LEAVE_JUNKERS88: {
        if (!movement->m_vehicle) {
            movement->m_interactionTime = MOVE_NO_TIME;
            break;
        }
        if (movement->m_interactionTime < 1) {
            // (Times out after 10 s without getting out.)
            movement->m_interactionTime = 20000;
            break;
        }
        if (movement->m_interactionTime <= 10000)
            break;
        if (interaction & INTERACT_FLAG_BODY) {
            C_Actor *vehicle = movement->m_vehicle;
            if (!vehicle->ExitSeat(movement->m_seat)) {
                // Cannot get out: stays in the seat.
                work->m_blocked2 &= ~WORK_REQUEST_INTERACT;
                movement->m_interactionTime = MOVE_NO_TIME;
                movement->m_interaction = INTERACT_FLAG_BODY | INTERACT_ENTER_CAR;
                movement->m_stillTime = 0;
                return;
            }
            AddHumanModel(movement->m_owner->GetFrame());
#ifndef HD2_DEDICATED   // no weapon pose seats on the server
            movement->m_weaponPose2->SetSeat(static_cast<uint32_t>(movement->m_seatMode), nullptr, movement->m_seat);
#endif
            movement->m_owner->ClearVehicle();
            m_locomotion->m_volumesTime = 0;
            if (vehicle->GetSeatFlags(movement->m_seat) & SEAT_FLAG_HIDDEN)
                m_owner->GetFrame()->SetOn(true);
            S_HumanAnimRef &asked = movement->m_nextAnimState.anim;
            asked.anim = ANIM_STAND;
            asked.Normalize();
            asked.stance = work->m_request.stance;
            asked.Normalize();
            movement->m_nextAnimState.activity = ACTIVITY_FADE_OUT;
            work->m_forced = true;
            work->Play(ANIM_STAND, false);
            work->PlayWorkAnimation(ACTIVITY_FADE_OUT, false);
            work->m_forced = false;
            m_animPlayer->Update();
            ShowEquipmentModels(equipment, true, true);
            if (code != INTERACT_LEAVE_SUBMARINE)
                equipment->PutAwayHandsItem(false);
            work->m_blocked2 &= ~(WORK_REQUEST_ANIM | WORK_REQUEST_ACTIVITY | WORK_REQUEST_LOOK_AT | WORK_REQUEST_FACING);
            movement->m_mixer.ClearQueue(0);
            m_animPlayer->m_animDelay = MOVE_NO_TIME;
            m_animPlayer->m_animDelayState = 0;
            m_animPlayer->m_carryDelay = MOVE_NO_TIME;
            m_animPlayer->m_carryDelayState = 0;
            m_animPlayer->m_activityDelay = MOVE_NO_TIME;
            m_animPlayer->m_activityDelayState = 0;
            movement->m_stanceVolumeScale = 0.8f;
            movement->m_hitVolumeScale[0] = 1.0f;
            movement->m_hitVolumeScale[1] = 1.0f;
            if (movement->m_seatMode != 2 && movement->m_seatMode != 4)
                work->m_blocked2 &= ~WORK_REQUEST_UNKNOWN5C;
            work->ApplyHit(0, S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, 0.0f));
            movement->m_mixer.UpdateStages(2000, 2000, false, nullptr);
            const S_vector4 pos = WorldPos4(m_owner->GetFrame());
            m_owner->SetPosition(XYZ(pos), pos.w);
            C_HumanMovement *ownerMovement = m_owner->GetMovement();
            ownerMovement->m_stillTime = 0;
            ownerMovement->m_visual.SetNetworkPosition(ownerMovement->m_owner->GetPosition4());
            ownerMovement->m_visual.SetNetworkPosition(ownerMovement->m_owner->GetPosition4());
            ownerMovement->m_visual.m_sectorTime = 10000;
            ownerMovement->m_visual.m_lighting.m_sampleDue = true;
            GetGame()->GetScene()->MarkTrackedModel(ownerMovement->m_owner->GetFrame());
            movement->m_seatMode = 0;
            m_locomotion->Land(false);
            m_locomotion->m_waterTransition = 0;
            m_locomotion->m_waterTestTime = 1000;
            m_locomotion->m_inWater = true;
            m_owner->GetMovement()->UpdateModelNames();
            movement->UpdateDummies();
            movement->UpdateHitSpheres();
            m_locomotion->m_fallStartY = 100000.0f;
            if (movement->m_shadow)
                movement->m_shadow->Update();
        }
        UpdateActivePlayerCamera(m_owner);
        movement->m_interactionTime = MOVE_NO_TIME;
        movement->m_interaction = 0;
        movement->m_vehicle = nullptr;
        movement->m_stillTime = 0;
        work->m_blocked2 &= ~WORK_REQUEST_INTERACT;
        work->m_blocked2 &= ~WORK_REQUEST_STANCE;
        break;
    }

    case INTERACT_SIT: {
        if (movement->m_interactionTime < 1) {
            movement->m_interactionTime = MOVE_NO_TIME;
            work->m_blocked2 &= ~WORK_REQUEST_INTERACT;
            break;
        }
        // At the start of the sitting down: the body onto the chair (half a metre below its
        // frame, facing along it).
        I3D_frame *chair = movement->m_seatFrame;
        if (movement->m_interactionTime != movement->GetAnimLength(ANIM_SIT_DOWN, 0, 1) || !chair ||
            !(movement->m_interaction & INTERACT_FLAG_BODY))
            break;
        GetGame()->GetScene()->RemoveHumanModel(movement->m_owner->GetFrame());
        const S_matrix &m = chair->GetMatrix();
        S_vector dir(m.m[2][0], m.m[2][1], m.m[2][2]);
        dir.Normalize();
        const float dirW = m.m[2][3];
        const S_matrix &w = chair->GetMatrix();
        const S_vector4 pos = {w.m[3][0], w.m[3][1] - 0.5f, w.m[3][2], w.m[3][3]};
        work->m_blocked2 |= WORK_REQUEST_FACING;
        m_owner->SetPosition(XYZ(pos), pos.w);
        m_owner->SetDirection(dir, dirW);
        I3D_frame *model = m_owner->GetFrame();
        model->SetPos(XYZ(pos));
        model->SetDir(dir, 0.0f);
        model->LinkTo(chair, 1);
        model->Update();
        equipment->PutAwayHandsItem(true);
        break;
    }

    case INTERACT_STAND_UP:
    case INTERACT_STAND_UP_FAST: {
        if (movement->m_interactionTime >= 1)
            break;
        work->m_blocked2 &= ~WORK_REQUEST_INTERACT;
        I3D_frame *chair = movement->m_seatFrame;
        AddHumanModel(movement->m_owner->GetFrame());
        if (chair && (movement->m_interaction & INTERACT_FLAG_BODY)) {
            I3D_frame *model = m_owner->GetFrame();
            if (I3D_frame *exit = chair->FindChildFrame(CHAIR_EXIT_DUMMY, CHAIR_EXIT_DUMMY_MASK)) {
                // Over to the chair's exit dummy.
                const S_vector4 to = WorldPos4(exit);
                const S_vector4 from = WorldPos4(model);
                movement->m_mixer.AddMove2({to.x - from.x, to.y - from.y, to.z - from.z, 0.0f}, MoveTime(700.0f));
                GetGame()->GetScene()->MarkTrackedModel(model);
                m_owner->SetPosition(XYZ(from), from.w);
                movement->m_stillTime = -2000;
            } else {
                // 0.4 m ahead.
                const S_vector ahead = model->GetWorldDir();
                S_vector flat(ahead.x, 0.0f, ahead.z);
                flat.Normalize();
                const S_vector4 from = WorldPos4(model);
                movement->m_mixer.AddMove2({flat.x * 0.4f, flat.y * 0.4f, flat.z * 0.4f, 0.0f}, MoveTime(700.0f));
                GetGame()->GetScene()->MarkTrackedModel(model);
                m_owner->SetPosition(XYZ(from), from.w);
            }
            equipment->PutAwayHandsItem(false);
            movement->m_seatMode = 0;
            m_owner->GetMovement()->UpdateModelNames();
            m_locomotion->m_fallStartY = 100000.0f;
            movement->UpdateDummies();
            movement->UpdateHitSpheres();
            if (movement->m_shadow)
                movement->m_shadow->Update();
        }
        work->m_blocked2 &= ~WORK_REQUEST_FACING;
        movement->m_interactionTime = MOVE_NO_TIME;
        movement->m_interaction = 0;
        movement->m_vehicle = nullptr;
        movement->m_stillTime = 0;
        break;
    }

    default:
        break;
    }
    if (movement->m_interactionTime != MOVE_NO_TIME)
        movement->m_interactionTime -= time;
}

// ------------------------------------------------------------------ save games

bool C_HumanIdle::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_IDLE))
        return false;
    chunk.WriteChunk(CHUNK_IDLE_STAND_TIME, m_standTime);
    chunk.WriteChunk(CHUNK_IDLE_FIDGET_TIME, m_fidgetTime);
    chunk.Ascend();
    return true;
}

bool C_HumanIdle::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_IDLE) {
        chunk.Ascend();
        return false;
    }
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_IDLE_STAND_TIME:
            chunk.Read(m_standTime);
            break;
        case CHUNK_IDLE_FIDGET_TIME:
            chunk.Read(m_fidgetTime);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}
