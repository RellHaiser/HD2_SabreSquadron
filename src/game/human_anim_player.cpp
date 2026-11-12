// The animation player of a human (C_HumanAnimPlayer, C_HumanMovement +0xC00; original source
// 0x00777d10..0x0077e1e0): each update it takes what the work asked for (the movement's next
// animation state), queues it or starts it on the mixer's tracks 0..2 (body animation, carry
// animation of the hands item, activity) from the animation tables, and handles what starting
// it means for the body: climbing over obstacles, doors, lying down and getting up, drawing and
// putting away the hands item, the props of the activities. The special animations
// (PlayAnimation, the scripts') go on the mixer's track 3.
//
// The original keeps the states of an update in statics shared by all humans (the requests,
// the states being played, the mixer animations to start); they are kept so here.
#include "game/first_person_view.h"
#include "game/human_movement.h"

#include <cstdlib>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_anim_tables.h"
#include "game/human_body.h"
#include "game/human_controller.h"
#include "game/human_equipment.h"
#include "game/human_internal.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/ladder.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

constexpr int ANIM_STAND = 0x2D;
constexpr int CARRY_ANIM_NONE = 0x5B;
// The hands item is put away (C_HumanMovement::m_handsCarryState 4 follows).
constexpr int CARRY_ANIM_PUT_AWAY = 0x5C;
constexpr int ACTIVITY_NONE = 0x4B;
// The activity track fades out.
constexpr int ACTIVITY_FADE_OUT = 0x4C;
// The idle activity (C_HumanAnimPlayer::m_unknown58).
constexpr int ACTIVITY_IDLE = 0x12;

// The work's blocked requests while a delay lasts (C_HumanWork::m_blocked; 0x20000: none of
// the requests, 0x40000 and 8 inferred from their users).
constexpr uint32_t BLOCK_NONE = 0x20000;
constexpr uint32_t BLOCK_MOVES = 0x40000;
constexpr uint32_t BLOCK_STANCE = 0x8;

// The head dummy of the human models.
const char HEAD_DUMMY_NAME[] = "HD";                    // 0x0084e938
// The door handle of the vehicles (body animations 0..3).
const char DOOR_HANDLE_NAME[] = "klika";

enum : uint16_t {
    CHUNK_ANIM_PLAYER = 0x1BC,
    CHUNK_AP_PENDING_ANIM = 0x00,
    CHUNK_AP_ANIM_DELAY = 0x01,
    CHUNK_AP_ANIM_DELAY_STATE = 0x02,
    CHUNK_AP_ANIM_BLOCKED = 0x03,
    CHUNK_AP_PENDING_CARRY = 0x04,
    CHUNK_AP_CARRY_DELAY = 0x05,
    CHUNK_AP_CARRY_DELAY_STATE = 0x06,
    CHUNK_AP_CARRY_BLOCKED = 0x07,
    CHUNK_AP_PENDING_ACTIVITY = 0x08,
    CHUNK_AP_ACTIVITY_DELAY = 0x09,
    CHUNK_AP_ACTIVITY_DELAY_STATE = 0x0A,
    CHUNK_AP_ACTIVITY_BLOCKED = 0x0B,
    CHUNK_AP_UNKNOWN5C = 0x0C,
    CHUNK_AP_UNKNOWN60 = 0x0D,
    CHUNK_AP_UNKNOWN70 = 0x0E,
    CHUNK_AP_UNKNOWN58 = 0x0F,
    CHUNK_AP_UNKNOWN90 = 0x10,
    CHUNK_AP_UNKNOWNA0 = 0x11,
};

// ------------------------------------------------------------------ the update's statics

S_HumanAnimRef g_curAnim;                               // 0x009833f0 what the body plays (as the tracks start)
int g_curCarry[3];                                      // 0x009833e4
int g_curActivity[2];                                   // 0x009833dc
S_HumanAnimRef g_nextAnim;                              // 0x009833cc what is asked for
int g_nextCarry[3];                                     // 0x009833c0
int g_nextActivity[2];                                  // 0x009833b8
S_HumanAnimRef g_oldAnim;                               // 0x00983478 what was played before the update
int g_oldCarry[3];                                      // 0x0098346c
int g_oldActivity[2];                                   // 0x00983464
I3D_frame *g_jointFrames[HUMAN_JOINT_COUNT];            // 0x00983400 (C_HumanAnimMixer::NotifyFrames)
int g_jointCount = -1;                                  // 0x0084e934
int g_activityMode;                                     // 0x00983488
const C_ActivityAnimTable *g_activityTable;             // 0x0098348c
int g_carryMode;                                        // 0x00983490
const C_CarryAnimTable *g_carryTable;                   // 0x00983494
int g_animMode;                                         // 0x00983498
const C_HumanAnimTable *g_animTable;                    // 0x0098349c (kept from human to human)
S_MixerAnim g_startAnims[MIXER_TRACKS];                 // 0x009834a0 to start the stages with
S_MixerAnim g_anims[MIXER_TRACKS];                      // 0x009834f0 for C_HumanAnimMixer::Play
bool g_changed = false;                                 // 0x00983540
I3D_animation_set *g_animSet;                           // 0x00983544
I3D_animation_set *g_carrySet;                          // 0x00983548
I3D_animation_set *g_activitySet;                       // 0x0098354c
float g_animSpeed = 1.0f;                               // 0x0084e924 (QueueAnim)
float g_animBlendTime = -1.0f;                          // 0x0084e928 (> 0: instead of the table's)
float g_carrySpeed = 1.0f;                              // 0x0084e92c
float g_activitySpeed = 1.0f;                           // 0x0084e930

bool InRange(int value, int low, int high) { return value >= low && value <= high; }
bool IsDeathAnim(int anim) { return InRange(anim, 0x71, 0x9A); }

void SetCarry(int *carry, const int *from)
{
    carry[0] = from[0];
    carry[1] = from[1];
}

// The slot of an animation of a movement table (the ids from HUMAN_ANIM_FIRST_6X with their
// sub-variant).
const S_HumanAnimSlot &AnimSlot(const C_HumanAnimTable *table, const S_HumanAnimRef &anim)
{
    if (anim.anim < HUMAN_ANIM_FIRST_6X)
        return table->GetSlot(anim.anim * 3 + anim.stance);
    return table->GetSlot(C_HumanAnimTable::BLOCK1_IDS * 3 +
                          ((anim.anim - HUMAN_ANIM_FIRST_6X) * 3 + anim.stance) * 6 + anim.sub);
}

// The movement table of an animation group (null: none).
const C_HumanAnimTable *GroupAnimTable(int group)
{
    return group == 1 ? g_humanAnimTable : group == 2 ? g_humanAnimTable2 : nullptr;
}

// The carry table of a carry mode (0 and 6 share the first; null: none).
const C_CarryAnimTable *CarryAnimTable(int carryMode)
{
    switch (carryMode) {
    case 0:
    case 6:
        return g_carryAnimTables[0];
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        return g_carryAnimTables[carryMode];
    default:
        return nullptr;
    }
}

// Two carry animations of the same standing group (or the same one).
bool IsSameStandGroup(const int *carry, int other)   // 0x007797f0
{
    const int id = carry[0];
    return id == other || (InRange(id, 0x2D, 0x36) && InRange(other, 0x2D, 0x36)) ||
           (InRange(id, 0x37, 0x3E) && InRange(other, 0x37, 0x3E)) ||
           (InRange(id, 0x3F, 0x41) && InRange(other, 0x3F, 0x41));
}

// The active player of a client.
bool IsActivePlayerOnClient(const C_Human *human)
{
    return IsClient() && g_teamManager.GetPlayerTeam()->GetActivePlayer() == human;
}

// The sound of putting the hands weapon away / taking the item of a belt slot, heard at the
// camera (when the HUD is off: in the first person view, inferred).
void PlayItemSound(const C_Item *item, C_Human *owner)
{
    if (GetGame()->IsFirstPersonView() || !item || !GetItemDef(item->GetTypeId()))
        return;
    // Fix: an item without a component makes no sound (the original calls through null).
    const C_ItemComponent *component = GetItemDef(item->GetTypeId())->GetComponent();
    if (!component)
        return;
    S_SoundRequest request;
    request.flags = SOUND_SILENT_TO_AI;
    request.chapter = SOUND_CHAPTER_WEAPON_SHOOTING;
    request.attachTo = nullptr;
    request.source = owner;
    request.group = const_cast<C_WeaponComp *>(static_cast<const C_WeaponComp *>(component))->GetShotSound();
    GetGame()->GetCameraModule()->GetCameraPos(0, reinterpret_cast<S_vector4 *>(&request.pos));   // (and posW)
    GetSoundModule()->Play(request);
}

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

}  // namespace

// ------------------------------------------------------------------ setting up

void C_HumanAnimPlayer::Reset()
{
    m_animDelayState = 0;
    m_carryDelayState = 0;
    m_activityDelayState = 0;
    m_animDelay = MOVE_NO_TIME;
    m_carryDelay = MOVE_NO_TIME;
    m_activityDelay = MOVE_NO_TIME;
    m_animBlocked = BLOCK_NONE;
    m_carryBlocked = BLOCK_NONE;
    m_activityBlocked = BLOCK_NONE;
    m_unknown58 = MOVE_NO_TIME;
    m_unknownA0 = MOVE_NO_VALUE;
}

void C_HumanAnimPlayer::Init(C_HumanMovement *movement, C_HumanSpecialMoves *specialMoves, C_HumanLocomotion *locomotion,
                             C_Human *owner)
{
    m_specialMoves = specialMoves;
    m_movement = movement;
    m_owner = owner;
    m_locomotion = locomotion;
    Reset();
    m_headDummy = owner->GetFrame()->FindChildFrame(HEAD_DUMMY_NAME, FRAME_MASK_DUMMY);
}

void C_HumanAnimPlayer::FindHeadDummy()
{
    m_headDummy = m_owner->GetFrame()->FindChildFrame(HEAD_DUMMY_NAME, FRAME_MASK_DUMMY);
}

// ------------------------------------------------------------------ the update

void C_HumanAnimPlayer::Update()
{
    g_changed = false;
    g_animSet = nullptr;
    g_carrySet = nullptr;
    g_activitySet = nullptr;
    g_animSpeed = 1.0f;
    g_animBlendTime = -1.0f;
    g_carrySpeed = 1.0f;
    g_activitySpeed = 1.0f;
    g_oldAnim.Set(HUMAN_ANIM_NONE, HUMAN_STANCE_STAND, 1);
    g_oldCarry[0] = CARRY_ANIM_NONE;
    g_oldCarry[1] = 0;
    g_oldActivity[0] = ACTIVITY_NONE;
    g_oldActivity[1] = 0;
    g_jointCount = -1;
    const S_HumanAnimState &state = m_movement->m_animState;
    S_HumanAnimState &next = m_movement->m_nextAnimState;
    g_curAnim.Set(state.anim.anim, state.anim.stance, state.anim.sub);
    g_curCarry[0] = state.carryAnim;
    g_curCarry[1] = state.carryVariant;
    g_curActivity[0] = state.activity;
    g_curActivity[1] = state.activityVariant;
    g_nextAnim.Set(next.anim.anim, next.anim.stance, next.anim.sub);
    g_nextCarry[0] = next.carryAnim;
    g_nextCarry[1] = next.carryVariant;
    g_nextActivity[0] = next.activity;
    g_nextActivity[1] = next.activityVariant;
    for (S_MixerAnim &anim : g_anims)
        anim.anim = nullptr;
    g_anims[0].crossfade = true;
    g_anims[1].crossfade = true;
    g_anims[2].crossfade = true;
    for (S_MixerAnim &anim : g_startAnims)
        anim.anim = nullptr;
    g_oldAnim.Set(state.anim.anim, state.anim.stance, state.anim.sub);
    g_oldCarry[0] = state.carryAnim;
    g_oldCarry[1] = state.carryVariant;
    g_oldActivity[0] = state.activity;
    g_oldActivity[1] = state.activityVariant;
    if (IsDeathAnim(g_curAnim.anim))
        return;
    UpdateAnimTrack();
    UpdateActivityTrack();
    UpdateCarryTrack();
    if (g_changed) {
        if (g_jointCount >= 0)
            m_movement->m_mixer.NotifyFrames(2, g_jointFrames, g_jointCount);
        m_movement->m_mixer.UpdateStages(0, 1, true, g_startAnims);
    }
    // The requests are taken.
    g_nextAnim.anim = HUMAN_ANIM_NONE;
    g_nextAnim.Normalize();
    g_nextCarry[0] = CARRY_ANIM_NONE;
    g_nextActivity[0] = ACTIVITY_NONE;
    next.anim.Set(g_nextAnim.anim, g_nextAnim.stance, g_nextAnim.sub);
    next.carryAnim = g_nextCarry[0];
    next.carryVariant = g_nextCarry[1];
    next.activity = g_nextActivity[0];
    next.activityVariant = g_nextActivity[1];
}

// ------------------------------------------------------------------ the body animation

void C_HumanAnimPlayer::UpdateAnimTrack()
{
    C_HumanAnimMixer &mixer = m_movement->m_mixer;
    C_HumanWork *work = m_owner->GetWork();
    if (m_movement->m_useState != MOVE_NO_TIME) {
        // Using a ladder.
        const auto holdAll = [&mixer]() {
            for (int track = 0; track < 3; ++track) {
                const int time = mixer.GetTrackTime(track);
                if (time != MOVE_NO_TIME)
                    mixer.HoldTrack(track, time);
            }
        };
        if (g_nextAnim.anim == HUMAN_ANIM_NONE && InRange(g_curAnim.anim, 0x6A, 0x6C)) {
            // Nothing asked for: the climb holds.
            holdAll();
            m_owner->SetMoveFlags(0);
            m_movement->StopUseSound();
        } else if (!InRange(g_curAnim.anim, 0x6A, 0x6C) && g_curAnim.anim != 8 && g_nextAnim.anim != HUMAN_ANIM_NONE &&
                   g_nextAnim.anim != 8) {
            m_owner->SetMoveFlags(0);
            work->Play(ANIM_STAND, true);
            holdAll();
            m_movement->StopUseSound();
        } else {
            if (InRange(g_curAnim.anim, 0x6A, 0x6C)) {
                // Up the ladder as forward, down as back.
                m_owner->SetMoveFlags(g_curAnim.anim == 0x6A ? MOVE_FORWARD : MOVE_BACK);
                mixer.ClearQueue(0);
                if (g_curAnim.anim == 0x6C)
                    m_movement->StartUseSound();
            }
            for (int track = 0; track < 3; ++track) {
                if (mixer.m_tracks[track].hold > 0)
                    mixer.ReleaseTrack(track);
            }
        }
    }

    S_HumanAnimRef last;                                // the animation the single-stage test was made for
    last.Set(g_nextAnim.anim, g_nextAnim.stance, g_nextAnim.sub);
    bool singleStage = IsSingleStageAnim(g_nextAnim.anim);
    if (m_animDelayState > 0 || singleStage)
        mixer.ClearQueue(0);
    bool queue = true;
    if (IsDeathAnim(g_nextAnim.anim)) {
        g_nextAnim.stance = m_movement->m_unknown9E0;
        g_nextAnim.Normalize();
        queue = false;
    } else if (m_animDelay != MOVE_NO_TIME) {
        // A started animation waits for its time (unless a standing one only changes).
        const int anim = m_movement->m_animState.anim.anim;
        const int asked = m_movement->m_nextAnimState.anim.anim;
        if (!InRange(anim, 0x29, 0x41) || asked == HUMAN_ANIM_NONE ||
            (InRange(asked, 0x29, 0x41) && asked != 0x2C && asked != 0x2B && asked != 0x36 && asked != 0x3E &&
             asked != 0x41)) {
            queue = false;
            if (!mixer.HasTrackPlayed(0, m_animDelay)) {
                if (!g_nextAnim.IsSame(g_curAnim) && (m_animDelayState != -1 || mixer.m_tracks[0].hold > 0) &&
                    InRange(g_curAnim.anim, 0xA2, 0xA5)) {
                    // Out of 0xA2..0xA5 early: the animation plays back.
                    mixer.ReleaseTrack(0);
                    m_animDelayState = -1;
                    mixer.MirrorTrackTime(0);
                    mixer.m_holdTime = MOVE_NO_TIME;
                    work->m_blocked &= ~m_animBlocked;
                }
                if ((InRange(g_curAnim.anim, 0x6A, 0x6C) || (g_curAnim.anim != 8 && g_curAnim.anim != 0x0E)) &&
                    InRange(g_nextAnim.anim, 0x6A, 0x6C) && !IsLocalGame() && !m_owner->IsLocalPlayer() &&
                    !m_owner->IsServerSimulated())
                    m_animDelay = MOVE_NO_TIME;
                g_nextAnim.anim = HUMAN_ANIM_NONE;
                g_nextAnim.Normalize();
            } else if (m_animDelayState == 1 && g_nextAnim.IsSame(m_pendingAnim)) {
                // Asked for the animation it waits to play: from its start.
                mixer.m_tracks[0].time = 0;
                mixer.m_tracks[0].hold = 0;
            } else {
                if (m_animDelayState == 0) {
                    const int current = g_curAnim.anim;
                    if ((!(m_animBlocked & BLOCK_MOVES) && current != 0x20 && current != 0x21 && current != 0x1F &&
                         current != 0x1E) ||
                        g_nextAnim.anim == HUMAN_ANIM_NONE) {
                        g_nextAnim.Set(m_pendingAnim.anim, m_pendingAnim.stance, m_pendingAnim.sub);
                        if (InRange(current, 8, 0x0E)) {
                            g_nextAnim.stance = work->m_request.stance;
                            g_nextAnim.Normalize();
                        }
                    }
                } else {
                    mixer.FadeOutTrack(0, MoveTime(350.0f));
                    g_animBlendTime = 350.0f / MOVE_TIME_SCALE;
                    g_curAnim.anim = HUMAN_ANIM_NONE;
                    g_curAnim.Normalize();
                    if (m_pendingAnim.anim == 0x21 || m_pendingAnim.anim == 0x1F)
                        g_nextAnim.Set(m_pendingAnim.anim, m_pendingAnim.stance, m_pendingAnim.sub);
                }
                m_animDelay = MOVE_NO_TIME;
                if (m_movement->m_useState == MOVE_NO_TIME && m_animBlocked != BLOCK_NONE)
                    work->m_blocked &= ~m_animBlocked;
            }
        }
    }
    if (queue) {
        g_animSpeed = mixer.QueueAnim(&g_nextAnim);
        if (singleStage && g_nextAnim.anim == HUMAN_ANIM_NONE)
            mixer.LockQueue(0);
        m_animDelay = MOVE_NO_TIME;
    }

    if (g_nextAnim.anim != HUMAN_ANIM_NONE)
        AdjustAnimRequest(g_curAnim, &g_nextAnim);
    if (!last.IsSame(g_nextAnim)) {
        last.Set(g_nextAnim.anim, g_nextAnim.stance, g_nextAnim.sub);
        singleStage = IsSingleStageAnim(g_nextAnim.anim);
    }
    if (singleStage)
        mixer.LockQueue(0);
    if (g_nextAnim.anim != HUMAN_ANIM_NONE && !g_nextAnim.IsSame(g_curAnim)) {
        if ((g_curAnim.anim == 0x2B || g_curAnim.anim == 0x2C) && g_nextAnim.anim != 0x2B && g_nextAnim.anim != 0x2C)
            m_locomotion->m_turnTarget.x = MOVE_NO_VALUE;
        if (InRange(g_curAnim.anim, 8, 0x0E)) {
            // Off a climb: a little further on.
            const S_vector4 &dir = m_owner->GetDirection4();
            const float scale = !m_movement->m_unknown96C && g_curAnim.anim != 0x0E ? 0.08f : 0.25f;
            const S_vector4 move = {dir.x * scale, 0.0f, dir.z * scale, 0.0f};
            mixer.AddMove(move, 200);
        }
        g_curAnim.Set(g_nextAnim.anim, g_nextAnim.stance, g_nextAnim.sub);
        if (InRange(g_curAnim.anim, 8, 0x0E)) {
            g_curAnim.stance = HUMAN_STANCE_STAND;
            g_curAnim.Normalize();
        }
        if (g_curAnim.anim < HUMAN_ANIM_NONE) {
            // (A group other than 1 / 2 keeps the table of the previous human, as in the original.)
            const int group = m_movement->m_owner->GetWork()->GetGroup();
            if (group == 1)
                g_animTable = g_humanAnimTable;
            else if (m_owner->GetWork()->GetGroup() == 2)
                g_animTable = g_humanAnimTable2;
            mixer.SetTrackRate(0, 1.0f);
            bool running = false;
            // Fix: no table, no animation (the original reads near null).
            const S_HumanAnimSlot *slot = g_animTable ? &AnimSlot(g_animTable, g_curAnim) : nullptr;
            g_animSet = slot ? slot->anim : nullptr;
            if (g_curAnim.anim < HUMAN_ANIM_FIRST_6X) {
                g_anims[0].blendTime = g_animBlendTime > 0.0f ? g_animBlendTime
                                       : slot ? slot->time / MOVE_TIME_SCALE * g_animSpeed : 0.0f;
            } else {
                // (The table's time of the moving animations is not scaled by the speed.)
                g_anims[0].blendTime = g_animBlendTime > 0.0f ? g_animBlendTime : slot ? slot->time / MOVE_TIME_SCALE : 0.0f;
                running = g_curAnim.sub == 2 || g_curAnim.sub == 5;
            }
            g_anims[0].mode = slot ? static_cast<int>(slot->flags & 0xE) : 0;
            g_anims[0].weight = 1.0f;
            g_anims[0].anim = g_animSet;
            m_movement->m_morph.SetExhausted(running, false);
            if (!g_animSet)
                g_curAnim.Set(g_oldAnim.anim, g_oldAnim.stance, g_oldAnim.sub);
            const int current = g_curAnim.anim;
            const int old = g_oldAnim.anim;
            if (m_animDelayState == -1) {
                m_animDelayState = 0;
                g_anims[0].blendTime = 200.0f;
            } else {
                // Blend times of particular changes (ms).
                float ms = -1.0f;
                if (InRange(current, 0x59, 0x5D) && !InRange(old, 0x59, 0x5D) && old != 0x68 && old != 0x69) {
                    ms = 1100.0f;
                } else if (InRange(old, 0x59, 0x5D) && (current == 0x2E || current == 0x38 || current == 0x40 ||
                                                         current == 0xBE || current == 0xC6)) {
                    ms = 600.0f;
                } else if (g_curAnim.stance != g_oldAnim.stance && g_curCarry[0] == 0x20 && !InRange(current, 0x29, 0x41) &&
                           current != 0x1A && current != 0x1B && g_curAnim.stance < 2 && g_oldAnim.stance < 2) {
                    g_anims[0].blendTime = 650.0f / MOVE_TIME_SCALE;
                    m_movement->m_unknown9AC = 2000;
                    m_movement->m_unknown9B0 = g_oldAnim.stance;
                } else if (old == 0x2C || old == 0x2B) {
                    ms = 300.0f;
                } else if (InRange(old, 8, 0x0E)) {
                    ms = 200.0f;
                } else if (InRange(current, 0xA2, 0xA5) && InRange(old, 0xA2, 0xA5)) {
                    g_anims[0].mode = MIXER_SYNC_SHARE;
                } else if (old == 6) {
                    ms = 800.0f;
                }
                if (ms >= 0.0f)
                    g_anims[0].blendTime = ms / MOVE_TIME_SCALE;
            }
            if (current == 0x9E) {
                m_movement->m_morph.SetUnknown78(1);
                m_movement->m_morph.SetUnknown64(1);
            } else if (old == 0x9E) {
                m_movement->m_morph.SetUnknown78(0);
                m_movement->m_morph.SetUnknown64(0);
            }
            m_locomotion->m_jumpBack = (current == 0x12 || current == 0x18 || current == 0x17) && (old == 0xBF || old == 0xC7);
        }
    }

    if (!g_animSet) {
        g_nextAnim.anim = HUMAN_ANIM_NONE;
        g_nextAnim.Normalize();
        return;
    }
    m_movement->m_animState.anim.Set(g_curAnim.anim, g_curAnim.stance, g_curAnim.sub);
    if (!last.IsSame(g_curAnim)) {
        last.Set(g_curAnim.anim, g_curAnim.stance, g_curAnim.sub);
        singleStage = IsSingleStageAnim(g_curAnim.anim);
    }
    g_animMode = singleStage ? I3DANIM_PLAY_ONCE : I3DANIM_LOOP;
    g_animSet->SetPlayMode(g_animMode);
    if (!IsDeathAnim(g_curAnim.anim) || m_movement->m_unknown1AC > 6)
        mixer.Play(g_anims, &g_nextAnim, g_nextCarry, g_nextActivity, g_animMode == I3DANIM_PLAY_ONCE);
    g_startAnims[0] = g_anims[0];
    g_anims[0].anim = nullptr;
    OnAnimStarted(g_curAnim, g_oldAnim, singleStage);
    m_locomotion->UpdateVelocity(false);
    m_owner->SetMoveFlags(0);
    // The direction of the animation for the controller (the others leave the flags cleared;
    // the standing animations clear them once more).
    const int anim = g_curAnim.anim;
    const bool held = mixer.m_tracks[0].hold > 0;
    uint32_t flags = 0;
    bool tell = true;
    switch (anim) {
    case 0xBE:
    case 0xC6:
        flags = MOVE_FORWARD;
        break;
    case 0xBF:
    case 0xC7:
        flags = MOVE_BACK;
        break;
    case 0xC0:
    case 0xC8:
        flags = MOVE_LEFT;
        break;
    case 0xC1:
    case 0xC9:
        flags = MOVE_RIGHT;
        break;
    case 0xC2:
    case 0xCA:
        flags = MOVE_FORWARD | MOVE_LEFT;
        break;
    case 0xC3:
    case 0xCB:
        flags = MOVE_FORWARD | MOVE_RIGHT;
        break;
    case 0xC4:
    case 0xCC:
        flags = MOVE_BACK | MOVE_LEFT;
        break;
    case 0xC5:
    case 0xCD:
        flags = MOVE_BACK | MOVE_RIGHT;
        break;
    case 0x6A:
        flags = held ? 0 : MOVE_FORWARD;
        break;
    case 0x6B:
    case 0x6C:
        flags = held ? 0 : MOVE_BACK;
        break;
    case 0xA2:
    case 0xA4:
        flags = MOVE_LEAN_1;
        break;
    case 0xA3:
    case 0xA5:
        flags = MOVE_LEAN_2;
        break;
    default:
        tell = InRange(anim, 0x29, 0x41);
        break;
    }
    if (tell)
        m_owner->SetMoveFlags(flags);
    g_changed = true;
}

// ------------------------------------------------------------------ the carry animation

void C_HumanAnimPlayer::UpdateCarryTrack()
{
    C_HumanAnimMixer &mixer = m_movement->m_mixer;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanWeaponPose *pose = m_movement->m_weaponPose2;
    AdjustCarryRequest(g_curAnim, g_curActivity);
    g_nextCarry[0] = m_movement->m_nextAnimState.carryAnim;
    g_nextCarry[1] = m_movement->m_nextAnimState.carryVariant;
    if (m_carryDelay == MOVE_NO_TIME) {
        if (!InRange(g_nextCarry[0], 0x50, 0x52))
            g_carrySpeed = mixer.QueueCarry(g_nextCarry);
    } else if (!mixer.HasTrackPlayed(1, m_carryDelay)) {
        // Waiting: the request is queued (0x23 / 0x24 also follow the wait).
        if (g_nextCarry[0] != CARRY_ANIM_NONE)
            mixer.PushCarry(g_nextCarry);
        if (g_nextCarry[0] == 0x23 || g_nextCarry[0] == 0x24)
            SetCarry(m_pendingCarry, g_nextCarry);
        g_nextCarry[0] = CARRY_ANIM_NONE;
        return;
    } else if (m_pendingCarry[0] == CARRY_ANIM_PUT_AWAY) {
        g_nextCarry[0] = CARRY_ANIM_PUT_AWAY;
        m_carryDelay = MOVE_NO_TIME;
        work->m_blocked &= ~m_carryBlocked;
    } else if (m_carryDelayState == 1 && g_nextCarry[0] == m_pendingCarry[0] && g_nextCarry[1] == m_pendingCarry[1]) {
        mixer.m_tracks[1].time = 0;
        mixer.m_tracks[1].hold = 0;
    } else {
        if (m_carryDelayState == 0) {
            const int carryMode = work->m_request.carryMode;
            if (InRange(m_pendingCarry[0], 0x1E, 0x20) && (carryMode == 0 || carryMode == 6))
                g_nextCarry[0] = CARRY_ANIM_PUT_AWAY;
            else
                SetCarry(g_nextCarry, m_pendingCarry);
        } else {
            mixer.SetTrackRate(1, 0.0f);
        }
        m_carryDelayState = 0;
        m_carryDelay = MOVE_NO_TIME;
        work->m_blocked &= ~m_carryBlocked;
    }

    if (g_nextCarry[0] != CARRY_ANIM_NONE && (!IsSameStandGroup(g_nextCarry, g_curCarry[0]) || g_nextCarry[1] != g_curCarry[1])) {
        bool putAway = false;
        if (g_nextCarry[0] == 0x20) {
            pose->RaiseWeapon(InRange(g_curCarry[0], 0x50, 0x52) ? 0 : 300);
        } else if (g_nextCarry[0] != 0x27) {
            pose->LowerWeapon(InRange(g_nextCarry[0], 0x50, 0x52) ? 0 : 300);
            putAway = g_nextCarry[0] == CARRY_ANIM_PUT_AWAY;
        }
        if (putAway) {
            SetCarry(g_curCarry, g_nextCarry);
            const int hands = m_movement->m_handsCarryState;
            if (hands == 3 || hands == 4) {
                if (g_nextCarry[1] == 2) {
                    mixer.FadeOutTrack(1, MoveTime(200.0f));
                    pose->SetBlendLength(MoveTime(100.0f));
                    g_changed = true;
                } else if (InRange(g_curAnim.anim, 8, 0x0E)) {
                    mixer.FadeOutTrack(1, MoveTime(500.0f));
                    pose->SetBlendLength(MoveTime(250.0f));
                } else if (hands == 4) {
                    mixer.FadeOutTrack(1, MoveTime(250.0f));
                    pose->SetBlendLength(MoveTime(125.0f));
                } else {
                    mixer.FadeOutTrack(1, MoveTime(400.0f));
                    pose->SetBlendLength(MoveTime(200.0f));
                }
                const int now = m_movement->m_handsCarryState;
                const int carryMode = work->m_request.carryMode;
                if ((now != 4 && now != 3) || carryMode == 0 || carryMode == 100)
                    m_movement->m_handsCarryState = 0;
            } else {
                mixer.FadeOutTrack(1, MoveTime(350.0f));
                const int carryMode = work->m_request.carryMode;
                if (carryMode != 6 && (carryMode == 0 || carryMode == 100))
                    m_movement->m_handsCarryState = 0;
            }
        } else {
            SetCarry(g_curCarry, g_nextCarry);
            if (g_nextCarry[0] < CARRY_ANIM_NONE) {
                if (g_nextCarry[0] == 0x20 && g_oldCarry[0] != 0x20)
                    m_movement->m_unknownA28 = MoveTime(150.0f);
                const int carryMode = work->m_request.carryMode;
                if (g_curCarry[0] == 0x22) {
                    work->m_forced = true;
                    work->SetCarryMode(m_movement->m_unknown220, false);
                    work->m_forced = false;
                }
                g_carryTable = CarryAnimTable(work->m_request.carryMode);
                work->m_forced = true;
                work->SetCarryMode(carryMode, false);
                work->m_forced = false;
                if (g_curCarry[1] < 0 || g_curCarry[1] > 4)
                    g_curCarry[1] = work->m_request.stance;
                if (g_carryTable) {
                    mixer.SetTrackRate(1, 1.0f);
                    // (Variants 3 / 4 read the next id's slots, as in the original.)
                    // Fix: not past the table's end.
                    const int index = g_curCarry[0] * 3 + g_curCarry[1];
                    if (index < C_CarryAnimTable::SLOT_COUNT) {
                        const S_CarryAnimSlot &slot = g_carryTable->GetSlot(g_curCarry[0], g_curCarry[1]);
                        g_carrySet = slot.anim;
                        g_anims[1].blendTime = slot.time / MOVE_TIME_SCALE * g_carrySpeed;
                        pose->SetBlendLength(static_cast<int>(g_anims[1].blendTime * 0.5f));
                        g_anims[1].anim = g_carrySet;
                        g_anims[1].weight = slot.weight;
                        g_anims[1].mode = static_cast<int>(slot.flags & 0xE);
                    }
                }
                if (!g_carrySet) {
                    SetCarry(g_curCarry, g_oldCarry);
                } else if ((g_curCarry[0] == 0x20 && g_oldCarry[0] == 0x20) ||
                           ((g_curCarry[0] == 0x1F || g_curCarry[0] == 0x1E) && g_oldCarry[0] == g_curCarry[0])) {
                    if (g_curCarry[1] != g_oldCarry[1])
                        g_anims[1].blendTime = 550.0f / MOVE_TIME_SCALE * g_carrySpeed;
                }
            }
        }
    }

    if (g_carrySet) {
        if (g_curCarry[0] == 0x27) {
#ifndef HD2_DEDICATED   // the server's has no recoil
            pose->StartRecoil(g_carrySet);
#endif
            return;
        }
        m_movement->m_animState.carryVariant = g_curCarry[1];
        m_movement->m_animState.carryAnim = g_curCarry[0];
        g_carryMode = IsSingleStageCarryAnim(g_curCarry[0]) ? I3DANIM_PLAY_ONCE : I3DANIM_LOOP;
        g_carrySet->SetPlayMode(g_carryMode);
        mixer.Play(g_anims, &g_nextAnim, g_nextCarry, g_nextActivity, g_carryMode == I3DANIM_PLAY_ONCE);
        g_startAnims[1] = g_anims[1];
        g_anims[1].anim = nullptr;
        OnCarryStarted(g_curCarry, g_oldCarry);
        g_changed = true;
        return;
    }
    if (g_curCarry[0] == CARRY_ANIM_PUT_AWAY)
        m_movement->m_animState.carryAnim = CARRY_ANIM_NONE;
    g_nextCarry[0] = CARRY_ANIM_NONE;
}

// ------------------------------------------------------------------ the activity

void C_HumanAnimPlayer::UpdateActivityTrack()
{
    C_HumanAnimMixer &mixer = m_movement->m_mixer;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    AdjustActivityRequest(g_curAnim, g_curCarry, g_nextActivity);
    const int hands = m_movement->m_handsCarryState;
    if (mixer.GetTrackTag(2) >= 0 || hands == 2 || hands == 1)
        work->OrFlagsCC(0x200);
    // The ends of the activities' prop animations (tags of OnActivityStarted).
    mixer.CheckTrackFinished(2, 0x51);
    if (mixer.CheckTrackFinished(2, 0x52)) {
        equipment->OnAnimationEvent(6);
    } else if (mixer.CheckTrackFinished(2, 0x53)) {
        if (g_curActivity[0] == 0x41 && g_nextActivity[0] == ACTIVITY_NONE) {
            mixer.SetTrackTag(2, 0x53);
        } else {
            equipment->SetHolster4Visible(true);
            equipment->OnAnimationEvent(8);
            work->m_blocked &= ~1u;
            work->m_blocked &= ~8u;
        }
    } else if (mixer.CheckTrackFinished(2, 0x55)) {
        if (g_curActivity[0] == 0x41 && g_nextActivity[0] == ACTIVITY_NONE) {
            mixer.SetTrackTag(2, 0x55);
        } else {
            equipment->SetHolster4Visible(true);
            // Fix: the event goes through the mixer's bounded AddTrackEvent (the original writes
            // past the track's 5 events when they are full).
            mixer.AddTrackEvent(1, ANIM_EVENT_HIDE_HANDS_ITEM_C);
            equipment->OnAnimationEvent(8);
            work->m_blocked &= ~1u;
            work->m_blocked &= ~8u;
        }
    } else if (mixer.CheckTrackFinished(2, 0x50) && g_curActivity[0] != 0x1A) {
        equipment->SetFlashlight(false);
        equipment->OnAnimationEvent(10);
    } else if (mixer.CheckTrackFinished(2, 0x54)) {
        // The idle activity again in 10..20 s.
        m_unknown58 = std::rand() % 10000 + 10000;
    }

    bool play = true;
    if (m_activityDelay == MOVE_NO_TIME) {
        S_HumanAnimState &next = m_movement->m_nextAnimState;
        if (next.activity == ACTIVITY_NONE && m_unknown58 != MOVE_NO_TIME && m_unknown58 < 1) {
            m_unknown58 = MOVE_NO_TIME;
            next.activity = ACTIVITY_IDLE;
            g_nextActivity[0] = ACTIVITY_IDLE;
        }
        g_activitySpeed = mixer.QueueActivity(&next.activity);
    } else if (mixer.HasTrackPlayed(2, m_activityDelay) || (m_pendingActivity[0] == 0x41 && mixer.m_tracks[2].hold > 0)) {
        const int requested = work->m_request.activity;
        if (requested != ACTIVITY_NONE) {
            m_pendingActivity[0] = requested;
            work->PlayWorkAnimation(ACTIVITY_NONE, true);
        }
        bool unblock = true;
        if (m_pendingActivity[0] == ACTIVITY_FADE_OUT) {
            g_nextActivity[0] = ACTIVITY_FADE_OUT;
            m_activityDelay = MOVE_NO_TIME;
        } else if (m_activityDelayState == 1 && g_nextActivity[0] == m_pendingActivity[0] &&
                   g_nextActivity[1] == m_pendingActivity[1]) {
            mixer.m_tracks[2].time = 0;
            mixer.m_tracks[2].hold = 0;
            unblock = false;
        } else {
            if (m_activityDelayState == 0)
                SetCarry(g_nextActivity, m_pendingActivity);
            else
                mixer.SetTrackRate(2, 0.0f);
            m_activityDelayState = 0;
            m_activityDelay = MOVE_NO_TIME;
        }
        if (unblock)
            work->m_blocked &= ~m_activityBlocked;
    } else {
        if (g_nextActivity[0] != g_curActivity[0] && mixer.m_tracks[2].hold > 0 && g_nextActivity[0] != ACTIVITY_NONE) {
            mixer.ReleaseTrack(2);
            m_pendingActivity[0] = ACTIVITY_FADE_OUT;
            m_activityDelay = 200;
            work->m_forced = true;
            work->PlayWorkAnimation(ACTIVITY_NONE, true);
            work->m_forced = false;
        }
        g_nextActivity[0] = ACTIVITY_NONE;
        play = false;
    }

    if (play && g_nextActivity[0] != ACTIVITY_NONE &&
        (g_nextActivity[0] != g_curActivity[0] || g_nextActivity[1] != g_curActivity[1])) {
        if (g_nextActivity[0] == ACTIVITY_FADE_OUT) {
            mixer.FadeOutTrack(2, g_curActivity[0] == ACTIVITY_IDLE ? MoveTime(1000.0f) : MoveTime(300.0f));
            SetCarry(g_curActivity, g_nextActivity);
            work->m_forced = true;
            work->PlayWorkAnimation(ACTIVITY_NONE, false);
            work->m_forced = false;
        } else {
            SetCarry(g_curActivity, g_nextActivity);
            g_activityTable = g_activityAnimTable;
            if (g_activityTable) {
                // (The rate of track 1, as in the original.)
                mixer.SetTrackRate(1, 1.0f);
                const S_HumanAnimSlot &slot = g_activityTable->GetSlot(g_curActivity[0], g_curActivity[1]);
                g_anims[2].anim = slot.anim;
                g_anims[2].weight = 1.0f;
                g_anims[2].blendTime = slot.time / MOVE_TIME_SCALE * g_activitySpeed;
                g_anims[2].mode = static_cast<int>(slot.flags & 0xE);
                g_activitySet = g_anims[2].anim;
            }
            if (!g_activitySet) {
                SetCarry(g_curActivity, g_oldActivity);
            } else {
                // Lighter activities over the moving animations; the arms' activities 0x1E..0x33
                // (hit reactions) tell the skeleton's arm joints.
                const int anim = m_movement->m_animState.anim.anim;
                const bool moving = InRange(anim, 0xBE, 0xCE);
                const int activity = g_curActivity[0];
                if (InRange(activity, 0x1E, 0x33)) {
                    g_anims[2].weight = moving ? 0.5f : 0.6f;
                    for (int i = 0; i < 8; ++i)
                        g_jointFrames[i] = m_movement->m_joints[8 + i].frame;
                    g_jointCount = 7;
                } else if (InRange(activity, 0, 2)) {
                    if (moving)
                        g_anims[2].weight = 0.75f;
                } else if (activity == 0x42) {
                    g_anims[2].weight = 0.65f;
                }
            }
        }
    }

    if (g_activitySet) {
        if (g_curActivity[0] < 8) {
#ifndef HD2_DEDICATED   // the server's plays no weapon pose activity
            m_movement->m_weaponPose2->PlayActivity(g_activitySet);
#endif
        } else {
            m_movement->m_animState.activity = g_curActivity[0];
            m_movement->m_animState.activityVariant = g_curActivity[1];
            g_activityMode = IsSingleStageActivity(g_curActivity[0]) ? I3DANIM_PLAY_ONCE : I3DANIM_LOOP;
            g_activitySet->SetPlayMode(g_activityMode);
            mixer.Play(g_anims, &g_nextAnim, g_nextCarry, g_nextActivity, g_activityMode == I3DANIM_PLAY_ONCE);
            g_startAnims[2] = g_anims[2];
            g_anims[2].anim = nullptr;
            if (g_curActivity[0] == 0x41)
                g_anims[2].crossfade = false;
        }
        OnActivityStarted(g_curActivity, g_oldActivity);
        g_changed = true;
        return;
    }
    if (g_curActivity[0] == ACTIVITY_FADE_OUT)
        m_movement->m_animState.activity = ACTIVITY_NONE;
    g_nextActivity[0] = ACTIVITY_NONE;
}

// ------------------------------------------------------------------ starting the animations

void C_HumanAnimPlayer::OnAnimStarted(const S_HumanAnimRef &anim, const S_HumanAnimRef &old, bool singleStage)
{
    C_HumanAnimMixer &mixer = m_movement->m_mixer;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    if (!singleStage) {
        // On a ladder (where it is simulated): the body is put on the ladder and stands still.
        if (InRange(anim.anim, 0x6A, 0x6C) && (IsLocalGame() || m_owner->IsLocalPlayer() || m_owner->IsServerSimulated())) {
            S_vector4 pos;
            m_locomotion->GetLadderPos(&pos);
            m_owner->SetPosition(XYZ(pos), pos.w);
            const S_vector4 still = {0.0f, 0.0f, 0.0f, 0.0f};
            mixer.SetVelocity(still, 0.0f, 0.0f, &pos);
            mixer.SetVelocity(still, 0.0f, 0.0f, &pos);
        }
    } else if (m_animDelay == MOVE_NO_TIME) {
        // A single-stage animation: what follows it, after how long, and the requests the work
        // blocks meanwhile.
        bool climb = false;                             // the stance follows the climb's end
        bool holdShare = false;
        bool shortDelay = false;
        m_animDelayState = 0;
        m_animBlocked = BLOCK_NONE;
        S_HumanAnimRef &pending = m_pendingAnim;
        pending.Set(old.anim, old.stance, old.sub);
        pending.stance = work->m_request.stance;
        pending.Normalize();
        pending.sub = work->m_request.gait;
        m_movement->m_unknown968 = 0.0f;
        m_locomotion->m_unknown0E4 = false;
        m_unknown5C = 0;
        m_unknown60 = MOVE_NO_TIME;
        const auto setPending = [&pending](int id) {
            pending.anim = id;
            pending.Normalize();
        };
        const auto setPendingStance = [&pending](int id, int stance) {
            pending.anim = id;
            pending.Normalize();
            pending.stance = stance;
            pending.Normalize();
        };
        // The head dummy back at the head (inline SetPos of the original).
        const auto resetHead = [this]() {
            m_unknown70 = {0.0f, 0.0f, 0.0f, m_unknown70.w};
            if (m_headDummy) {
                m_headDummy->SetPosInline(XYZ(m_unknown70));
                m_headDummy->Update();
            }
        };
        // Over an obstacle: its height, the times of the climb, the standing animation after it.
        const auto climbOver = [&](float height, float time1, float time2, bool standUp) {
            m_movement->m_unknown95C = height;
            // (Floats, not MoveTime's truncated ints: the original stores ms / 1.4 with FSTP.)
            m_movement->m_unknown960 = time1 / MOVE_TIME_SCALE;
            m_movement->m_unknown964 = time2 / MOVE_TIME_SCALE;
            if (m_movement->m_unknown954 == 1)
                setPendingStance(0x38, HUMAN_STANCE_CROUCH);
            else if (standUp || pending.anim == 0x2B || pending.anim == 0x2C)
                setPending(ANIM_STAND);
            C_HumanWork *ownerWork = m_movement->m_owner->GetWork();
            ownerWork->m_forced = true;
            ownerWork->Play(pending.anim, true);
            ownerWork->m_forced = false;
            m_locomotion->Land(false);
        };
        // Lying down / getting up (inferred): the climb's stance follows it.
        const auto changeStance = [&](int pendingStance, int unknown9B0) {
            pending.Normalize();
            m_animBlocked = BLOCK_STANCE;
            climb = true;
            m_locomotion->m_unknown0DC = 0;
            m_locomotion->m_unknown0E0 = 0;
            m_movement->m_unknown9AC = 0;
            m_movement->m_unknown9B0 = unknown9B0;
            (void)pendingStance;
        };
        switch (anim.anim) {
        case 0:
        case 1:
        case 2:
        case 3: {
            // Opening a vehicle's door: facing its handle.
            // Fix: a human without a vehicle has no handle (the original calls through null).
            I3D_frame *handle = m_movement->m_vehicle
                                    ? m_movement->m_vehicle->GetFrame()->FindChildFrame(DOOR_HANDLE_NAME, FRAME_MASK_DUMMY)
                                    : nullptr;
            if (handle) {
                const S_vector to = handle->GetWorldPos() - m_owner->GetPosition();
                m_movement->m_unknown998 = m_locomotion->GetTurnTime(to);
                work->SetFacingDir(to, true);
            }
            m_animBlocked = BLOCK_MOVES;
            break;
        }
        case 7:
            setPending(0x62);
            m_animBlocked = BLOCK_MOVES;
            break;
        case 8: {
            m_movement->PlaySound(MOVE_SOUND_OVER3);
            m_locomotion->Land(false);
            m_movement->m_unknown95C = -1.629f;
            m_movement->m_unknown960 = 0.0f / MOVE_TIME_SCALE;
            m_movement->m_unknown964 = 0.0f / MOVE_TIME_SCALE;
            setPendingStance(work->m_request.gait == 2 ? 0x6C : 0x6B, HUMAN_STANCE_STAND);
            m_animBlocked = BLOCK_NONE;
            C_HumanWork *ownerWork = m_movement->m_owner->GetWork();
            ownerWork->m_forced = true;
            ownerWork->Play(pending.anim, true);
            ownerWork->m_forced = false;
            m_unknown70 = {0.0f, 0.0f, 0.0f, m_unknown70.w};
            work->m_blocked &= ~WORK_REQUEST_ANIM;
            work->m_forced = true;
            work->Play(pending.anim, true);
            work->m_forced = false;
            if (m_headDummy) {
                m_headDummy->SetPosInline(XYZ(m_unknown70));
                m_headDummy->Update();
            }
            break;
        }
        case 9:
            climbOver(0.244f, 0.0f, 554.0f, false);
            resetHead();
            break;
        case 10:
            climbOver(0.561f, 0.0f, 754.0f, false);
            resetHead();
            break;
        case 0x0B:
            m_movement->PlaySound(MOVE_SOUND_OVER);
            climbOver(0.695f, 0.0f, 400.0f, false);
            resetHead();
            break;
        case 0x0C:
            m_movement->PlaySound(MOVE_SOUND_OVER2);
            climbOver(0.97f, 550.0f, 750.0f, false);
            resetHead();
            break;
        case 0x0D: {
            m_movement->PlaySound(MOVE_SOUND_OVER3);
            const S_vector4 &dir = m_owner->GetDirection4();
            const S_vector4 back = {-dir.x * 0.1f, 0.0f, -dir.z * 0.1f, dir.w};
            climbOver(1.642f, 0.0f, 426.0f, false);
            mixer.AddMove(back, 250);
            m_unknown70 = {0.0f, 0.0f, 0.0f, dir.w};
            if (m_headDummy) {
                m_headDummy->SetPosInline(XYZ(m_unknown70));
                m_headDummy->Update();
            }
            break;
        }
        case 0x0E:
            m_movement->PlaySound(MOVE_SOUND_OVER3);
            climbOver(1.356f, 0.0f, 400.0f, true);
            resetHead();
            if (C_Actor *object = m_movement->m_useObject) {
                // Off the top of the ladder in use: on along its facing by half the ladder's
                // horizontal extent.
                const S_vector4 &top = GetLadderTop(object);
                const S_vector4 &bottom = GetLadderBottom(object);
                const S_vector4 &facing = GetLadderDir(object);
                const float extent = S_vector(bottom.x - top.x, 0.0f, bottom.z - top.z).Magnitude();
                const S_vector along = S_vector(facing.x, facing.y, facing.z) * extent * 0.5f;
                const S_vector flat(along.x, 0.0f, along.z);
                if (flat.Magnitude() >= 0.05f) {
                    m_unknown60 = 50;
                    m_unknown90 = {flat.x, 0.0f, flat.z, m_unknown90.w};
                }
            }
            break;
        case 0x12:
            m_locomotion->m_jumpTime = 0;
            work->m_bodyFlags |= 0x80;
            mixer.Slide({0.0f, 8.0f, 0.0f, 0.0f}, 2400);
            m_animBlocked = BLOCK_MOVES;
            break;
        case 0x14:
            setPendingStance(ANIM_STAND, HUMAN_STANCE_STAND);
            m_animBlocked = BLOCK_STANCE;
            // (Falls through to 0x16, which does the same.)
            setPendingStance(ANIM_STAND, HUMAN_STANCE_STAND);
            m_animBlocked = BLOCK_STANCE;
            break;
        case 0x16:
            setPendingStance(ANIM_STAND, HUMAN_STANCE_STAND);
            m_animBlocked = BLOCK_STANCE;
            break;
        case 0x15:
            setPendingStance(0x37, HUMAN_STANCE_CROUCH);
            m_animBlocked = BLOCK_STANCE;
            work->Play(ANIM_STAND, false);
            break;
        case 0x17:
            m_locomotion->m_jumpTime = 0;
            work->m_bodyFlags |= 0x80;
            if (work->m_request.gait == 2)
                mixer.Slide({0.0f, 9.5f, 0.0f, 0.0f}, 3000);
            else
                mixer.Slide({0.0f, 9.0f, 0.0f, 0.0f}, 2500);
            m_animBlocked = BLOCK_MOVES;
            break;
        case 0x18:
            m_locomotion->m_jumpTime = 0;
            work->m_bodyFlags |= 0x80;
            mixer.Slide({0.0f, 9.85f, 0.0f, 0.0f}, work->m_request.gait == 2 ? 2500 : 2100);
            m_animBlocked = BLOCK_MOVES;
            break;
        case 0x1A:
            m_movement->PlaySound(MOVE_SOUND_KNEEL);
            setPendingStance(0x37, HUMAN_STANCE_CROUCH);
            m_animBlocked = BLOCK_STANCE;
            climb = true;
            m_locomotion->m_unknown0DC = 0;
            m_locomotion->m_unknown0E0 = 0;
            m_movement->m_unknown9AC = 0;
            m_movement->m_unknown9B0 = 0;
            break;
        case 0x1B:
            m_movement->PlaySound(MOVE_SOUND_STAND_UP);
            setPending(0x2E);
            pending.stance = HUMAN_STANCE_STAND;
            changeStance(HUMAN_STANCE_STAND, 1);
            break;
        case 0x1C:
            setPending(0x22);
            m_movement->SetSeatMode(1, true);
            break;
        case 0x1D:
        case 0x25:
        case 0x26:
            setPending(ANIM_STAND);
            m_movement->SetSeatMode(1, false);
            m_movement->SetSeatMode(2, false);
            break;
        case 0x1E:
            m_movement->PlaySound(MOVE_SOUND_LIE_DOWN);
            setPending(0x40);
            pending.stance = HUMAN_STANCE_PRONE;
            changeStance(HUMAN_STANCE_PRONE, 1);
            break;
        case 0x1F:
            m_movement->PlaySound(MOVE_SOUND_KNEEL);
            setPending(0x38);
            pending.stance = HUMAN_STANCE_CROUCH;
            changeStance(HUMAN_STANCE_CROUCH, 2);
            break;
        case 0x20:
            m_movement->PlaySound(MOVE_SOUND_LIE_DOWN);
            pending.stance = HUMAN_STANCE_PRONE;
            pending.Normalize();
            m_animBlocked = BLOCK_STANCE;
            climb = true;
            m_locomotion->m_unknown0DC = 0;
            m_locomotion->m_unknown0E0 = 0;
            m_movement->m_unknown9AC = 0;
            m_movement->m_unknown9B0 = 0;
            if (old.anim < HUMAN_ANIM_FIRST_6X) {
                setPending(0x40);
            } else {
                // Lying down while moving: moves on lying (sooner).
                pending.Set(old.anim, old.stance, old.sub);
                pending.stance = HUMAN_STANCE_PRONE;
                pending.Normalize();
                shortDelay = true;
            }
            break;
        case 0x21:
            m_movement->PlaySound(MOVE_SOUND_STAND_UP);
            setPending(0x2E);
            pending.stance = HUMAN_STANCE_STAND;
            changeStance(HUMAN_STANCE_STAND, 2);
            break;
        case 0x24:
            setPending(0x23);
            m_movement->SetSeatMode(2, true);
            break;
        case 0x2B:
        case 0x2C:
            if (IsSingleStageAnim(pending.anim))
                setPending(ANIM_STAND);
            if (work->m_request.anim == anim.anim)
                work->Play(pending.anim, true);
            break;
        case 0x2F:
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
            setPending(ANIM_STAND);
            break;
        case 0x39:
        case 0x3A:
        case 0x3B:
        case 0x3C:
        case 0x3D:
        case 0x3E:
            setPending(0x37);
            break;
        case 0x41:
            setPending(0x3F);
            break;
        case 0x5E:
            // Picking up a body: it goes on the shoulder (place 9).
            if (I3D_frame *body = work->m_request.workingFrame) {
                m_movement->m_unknownA30 = 0;
                C_Actor *actor = body->GetData() ? body->GetData()->actor : nullptr;
                // Fix: a frame without an actor (the original reads through null).
                equipment->SetModel(body, EQUIPMENT_PLACE_CARRIED, -1, actor ? actor->GetNetId() : -1, -1);
            }
            break;
        case 0x5F:
            m_movement->m_unknownA30 = 2;
            work->m_forced = true;
            work->SetCarryMode(100, false);
            work->m_forced = false;
            break;
        case 0x67:
            work->Play(pending.anim, true);
            break;
        case 0x9C:
            m_movement->m_unknownA2C = 0;
            equipment->SetModel(work->m_request.workingFrame, 4, -1, -4, -1);
            break;
        case 0x9D:
            m_movement->m_unknownA2C = 2;
            work->m_forced = true;
            work->SetCarryMode(100, false);
            work->m_forced = false;
            break;
        case 0xA0:
            m_movement->m_unknownA2C = 8;
            equipment->SetModel(work->m_request.workingFrame, 4, -1, -4, -1);
            break;
        case 0xA1:
            m_movement->m_unknownA2C = 10;
            break;
        case 0xA2:
        case 0xA3:
        case 0xA4:
        case 0xA5:
            holdShare = true;
            m_animDelayState = 0;
            break;
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC4:
        case 0xC5:
        case 0xC8:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCC:
        case 0xCD:
            pending.Set(anim.anim, anim.stance, anim.sub);
            m_animDelayState = 0;
            m_locomotion->m_unknown0DC = 0;
            m_locomotion->m_unknown0E0 = 0;
            m_animBlocked = BLOCK_MOVES;
            break;
        default:
            break;
        }
        if (IsDeathAnim(anim.anim)) {
            setPending(0x71);
            m_unknown70 = {0.0f, 0.0f, 0.0f, m_unknown70.w};
        }

        if (anim.anim < HUMAN_ANIM_NONE) {
            // How long the animation plays before the pending one (ms of the movement clock):
            // its length less the blend time of the pending one.
            const C_HumanAnimTable *table = GroupAnimTable(work->GetGroup());
            // Fix: no table, no wait (the original reads near null).
            I3D_animation_set *set = table ? AnimSlot(table, anim).anim : nullptr;
            const int pendingBlend = table ? static_cast<int>(AnimSlot(table, pending).time) : 0;
            if (!set) {
                m_animDelay = 0;
            } else {
                if (anim.anim < HUMAN_ANIM_FIRST_6X && holdShare) {
                    if (m_unknownA0 != MOVE_NO_VALUE)
                        mixer.SetHoldTime(set->GetEndTime(), m_unknownA0);
                    m_animDelay = set->GetEndTime() - 200;
                } else {
                    m_animDelay = set->GetEndTime() - pendingBlend;
                }
                m_animDelay = static_cast<int>(static_cast<double>(m_animDelay) / MOVE_TIME_SCALE);
                if (anim.anim < HUMAN_ANIM_FIRST_6X && shortDelay)
                    m_animDelay = static_cast<int>(static_cast<float>(m_animDelay) * 0.75f);
                if (m_movement->m_unknown9AC == 0)
                    m_movement->m_unknown9AC = m_animDelay;
                if (!IsDeathAnim(anim.anim)) {
                    if (!InRange(anim.anim, 0x0B, 0x0E)) {
                        if (climb)
                            work->SetStanceTimer(static_cast<int>(static_cast<float>(m_animDelay) * 1.1f),
                                                 work->m_request.stance, 8);
                    } else if (m_movement->m_unknown954 == 1) {
                        work->SetStance(HUMAN_STANCE_CROUCH, false);
                        work->SetStanceTimer(static_cast<int>(static_cast<float>(m_animDelay) * 1.1f),
                                             m_movement->m_unknown954, 9);
                    }
                    if (InRange(anim.anim, 0x0B, 0x0E) || anim.anim == 8) {
                        // The trail spheres stay for the climb.
                        m_movement->m_trailTime = m_animDelay;
                        for (int i = 0; i < m_movement->m_trailCount; ++i)
                            g_collisionWorld.AddVolume(m_movement->m_trailVolumes[i]);
                    }
                } else {
                    if (m_movement->m_unknown1AC == 4)
                        work->SetStanceTimer(MoveTime(100.0f), anim.anim == 0x92 || anim.anim == 0x94 ? 0 : 2, 9);
                    m_movement->m_unknown9BC = MoveTime(300.0f);
                    m_movement->m_unknown9B8 = static_cast<int>(static_cast<float>(m_animDelay) * 1.1f);
                    m_movement->m_unknown9C0 = static_cast<int>(static_cast<float>(m_animDelay) * 1.7f);
                    m_movement->m_unknown9C4 = m_movement->m_unknown9B8 / 4;
                }
            }
            work->m_blocked |= m_animBlocked;
        } else {
            m_animDelay = MOVE_NO_TIME;
        }
        if (m_movement->m_unknown9AC == 0)
            m_movement->m_unknown9AC = MOVE_NO_TIME;
    }
    m_unknownA0 = MOVE_NO_VALUE;
    if (anim.anim == 0x6C)
        m_movement->StartUseSound();
    else
        m_movement->StopUseSound();
}

void C_HumanAnimPlayer::OnCarryStarted(const int *carry, const int *old)
{
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    if (!IsSingleStageCarryAnim(carry[0]) || m_carryDelay != MOVE_NO_TIME)
        return;
    bool weaponSpecial = false;                         // 0x50..0x52
    bool fromProne = false;
    bool drawing = false;
    bool specialObject = false;
    bool putAway = false;
    SetCarry(m_pendingCarry, old);
    m_pendingCarry[1] = work->m_request.stance;
    m_pendingCarry[2] = work->m_request.gait;
    const int carryMode = work->m_request.carryMode;
    switch (carry[0]) {
    case 0x22:
        // Putting the hands item away.
        if (IsActivePlayerOnClient(m_owner))
            m_movement->PlaySound(MOVE_SOUND_WEAPON_IN);
        m_pendingCarry[0] = CARRY_ANIM_PUT_AWAY;
        m_carryBlocked = WORK_REQUEST_CARRY_MODE;
        work->m_forced = true;
        work->SetCarryMode(m_movement->m_unknown220, false);
        work->m_forced = false;
        m_movement->m_handsCarryState = 1;
        break;
    case 0x23:
        m_movement->m_unknown190 &= ~1;
        // (Falls through.)
    case 0x24:
        // Drawing it: what to hold it with after.
        if (IsActivePlayerOnClient(m_owner)) {
            const C_Item *item = m_owner->GetHandsItem();
            m_movement->PlaySound(item && m_owner->GetHandsItem()->GetDef()->GetBehaviour() == 9 ? MOVE_SOUND_GRENADE_TAKE
                                                                                                    : MOVE_SOUND_WEAPON_OUT);
        }
        m_movement->m_handsCarryState = 2;
        drawing = true;
        m_movement->m_unknownA24 = 0;
        m_carryBlocked = WORK_REQUEST_CARRY_MODE;
        if (work->m_request.unknown5C == 0) {
            const int anim = m_movement->m_animState.anim.anim;
            if (work->m_request.carryMode == 6 || (work->m_request.stance == HUMAN_STANCE_PRONE && !InRange(anim, 0x29, 0x41)))
                m_pendingCarry[0] = CARRY_ANIM_PUT_AWAY;
            else
                m_pendingCarry[0] = m_owner->GetRole() == HUMAN_ROLE_PLAYER ? 0x1E : 0x1F;
        } else if (work->m_request.unknown5C == 1) {
            const int mode = work->m_request.carryMode;
            const int anim = m_movement->m_animState.anim.anim;
            if (mode == 6 || (equipment->GetFlags() & EQUIPMENT_HANDS_PUT_AWAY) || mode == 100 ||
                (work->m_request.stance == HUMAN_STANCE_PRONE && !InRange(anim, 0x29, 0x41)))
                m_pendingCarry[0] = CARRY_ANIM_PUT_AWAY;
            else
                m_pendingCarry[0] = 0x20;
        }
        break;
    case 0x25:
        m_pendingCarry[0] = CARRY_ANIM_PUT_AWAY;
        break;
    case 0x46:
    case 0x47:
        if (IsActivePlayerOnClient(m_owner))
            GetGame()->GetFirstPersonView()->OnInventoryEvent(FPV_EVENT_FIRE, FPV_EVENT_IDLE);
        m_pendingCarry[0] = CARRY_ANIM_PUT_AWAY;
        break;
    case 0x4C:
    case 0x4D:
        fromProne = true;
        // (The original asks for the hands weapon only out of the first person view.)
        if (!GetGame()->IsFirstPersonView())
            PlayItemSound(m_movement->m_owner->GetHandsWeapon(), m_movement->m_owner);
        if (IsActivePlayerOnClient(m_owner))
            GetGame()->GetFirstPersonView()->OnInventoryEvent(FPV_EVENT_FIRE, FPV_EVENT_IDLE);
        m_pendingCarry[0] = CARRY_ANIM_PUT_AWAY;
        break;
    case 0x4F:
    case 0x5A: {
        fromProne = true;
        const C_Item *item = m_movement->m_owner->FindBeltItem(9);
        PlayItemSound(item, m_movement->m_owner);
        equipment->SetModel(work->m_request.workingFrame, 7, -1, -6, -1);
        if (m_pendingCarry[0] == CARRY_ANIM_NONE)
            m_pendingCarry[0] = CARRY_ANIM_PUT_AWAY;
        specialObject = true;
        break;
    }
    case 0x50:
    case 0x51:
    case 0x52:
        if (IsActivePlayerOnClient(m_owner))
            GetGame()->GetFirstPersonView()->OnInventoryEvent(FPV_EVENT_RELOAD, FPV_EVENT_IDLE);
        putAway = true;
        weaponSpecial = true;
        break;
    default:
        break;
    }

    const C_CarryAnimTable *table = CarryAnimTable(work->m_request.carryMode);
    I3D_animation_set *set = nullptr;
    if ((m_pendingCarry[0] <= 0x5A || m_pendingCarry[0] == CARRY_ANIM_PUT_AWAY) && table)
        set = table->GetSlot(carry[0], carry[1]).anim;
    if (!set) {
        m_carryDelay = MOVE_NO_TIME;
    } else {
        const int length = set->GetEndTime();
        m_carryDelay = length;
#ifndef HD2_DEDICATED   // no weapon pose on the server
        if (weaponSpecial)
            m_movement->m_owner->GetMovement()->m_weaponPose->PauseWeapon(length, specialObject);
#endif
        if (specialObject)
            m_movement->m_unknownA24 = static_cast<int>(static_cast<double>(m_carryDelay) / MOVE_TIME_SCALE);
        // The remote action that replays it (prone).
        const auto remote = [this](int action) {
            S_RemoteAction entry;
            entry.time = static_cast<int>(static_cast<double>(m_carryDelay) / MOVE_TIME_SCALE);
            entry.action = action;
            entry.phase = 1;
            m_movement->m_remoteActions.push_back(entry);
        };
        if (fromProne) {
#ifndef HD2_DEDICATED   // no weapon pose on the server
            m_movement->m_weaponPose2->PauseHands(static_cast<int>(static_cast<double>(m_carryDelay) / MOVE_TIME_SCALE));
#endif
            if (work->m_request.stance == HUMAN_STANCE_PRONE)
                remote(0x0E);
        } else if (putAway && (work->m_request.stance == HUMAN_STANCE_PRONE || work->m_request.carryMode == 5)) {
            remote(0x0D);
        } else if (drawing && work->m_request.stance == HUMAN_STANCE_PRONE) {
            remote(0x0F);
        }
        int unknownA1C;
        if (m_pendingCarry[0] == CARRY_ANIM_PUT_AWAY) {
            const double scale = 1.0 / MOVE_TIME_SCALE;
            m_carryDelay = static_cast<int>(static_cast<double>(m_carryDelay) * scale - 420.0 * scale);
            unknownA1C = m_carryDelay;
        } else {
            // Less the blend time of the carry animation that follows.
            const float delay = static_cast<float>(m_carryDelay);
            const int reduced = static_cast<int>(delay - table->GetSlot(m_pendingCarry[0], m_pendingCarry[1]).time);
            m_carryDelay = static_cast<int>(static_cast<double>(reduced) / MOVE_TIME_SCALE);
            unknownA1C = static_cast<int>(static_cast<double>(delay) / MOVE_TIME_SCALE);
        }
        if (carry[0] == 0x22 || carry[0] == 0x23 || carry[0] == 0x24)
            m_movement->m_unknownA1C = unknownA1C;
    }
    work->m_forced = true;
    work->SetCarryMode(carryMode, false);
    work->m_forced = false;
    if (m_carryDelay != MOVE_NO_TIME)
        work->m_blocked |= m_carryBlocked;
}

void C_HumanAnimPlayer::OnActivityStarted(const int *activity, const int *old)
{
    const C_ActivityAnimTable *table = g_activityAnimTable;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    C_HumanAnimMixer &mixer = m_movement->m_mixer;
    if (m_activityDelay != MOVE_NO_TIME)
        return;
    if (old[0] == ACTIVITY_NONE || IsSingleStageActivity(old[0])) {
        m_pendingActivity[0] = ACTIVITY_FADE_OUT;
        work->PlayWorkAnimation(ACTIVITY_NONE, true);
    } else {
        SetCarry(m_pendingActivity, old);
    }
    m_pendingActivity[1] = work->m_request.stance;
    m_unknown58 = MOVE_NO_TIME;
    // A prop of the activity (a copy of the activity prop `index`) in the special object place.
    const auto takeProp = [equipment](int index, int time) {
        if (!GetActivityProp(index))
            return false;
        I3D_frame *frame = I3DGetDriver()->CreateFrame(FRAME_MODEL);
        frame->Duplicate(GetActivityProp(index));
        equipment->SetModel(frame, 7, -1, time, -1);
        return true;
    };
    int tag = -1;
    switch (activity[0]) {
    case 0x11:
        if (!equipment->GetModel(7) && GetActivityProp(2)) {
            I3D_frame *frame = I3DGetDriver()->CreateFrame(FRAME_MODEL);
            frame->Duplicate(GetActivityProp(2));
            equipment->SetSpecialHandDummy1(false);
            equipment->SetModel(frame, 7, -1, -3, -1);
            equipment->OnAnimationEvent(0x0B);
            equipment->SetSpecialHandDummy1(true);
        }
        m_pendingActivity[0] = ACTIVITY_IDLE;
        break;
    case ACTIVITY_IDLE:
        mixer.SetTrackTag(2, 0x54);
        if (work->m_request.activity == 0x11) {
            work->m_forced = true;
            work->PlayWorkAnimation(ACTIVITY_NONE, true);
            work->m_forced = false;
        }
        break;
    case 0x16:
        work->m_blocked |= 1;
        work->m_blocked |= 8;
        // (Falls through.)
    case 0x17:
        equipment->SetModel(work->m_request.workingFrame, 6, -1, -7, -1);
        equipment->OnAnimationEvent(9);
        equipment->SetHolster4Visible(false);
        work->OrFlagsCC(0x200);
        if (work->m_request.activity != 0x41) {
            tag = 0x55;
            break;
        }
        m_pendingActivity[0] = 0x41;
        mixer.SetTrackTag(2, 0x53);
        work->m_forced = true;
        work->PlayWorkAnimation(ACTIVITY_NONE, true);
        work->m_forced = false;
        break;
    case 0x19:
        if (!equipment->GetModel(7) && takeProp(0, -2))
            equipment->OnAnimationEvent(0x0B);
        mixer.SetTrackTag(2, 0x50);
        work->PlayWorkAnimation(ACTIVITY_NONE, true);
        return;
    case 0x1A:
        m_pendingActivity[0] = 0x19;
        tag = 0x51;
        break;
    case 0x1C:
        takeProp(1, -1);
        tag = 0x50;
        break;
    case 0x1D:
        equipment->SetModel(work->m_request.workingFrame, 7, -1, -4, -1);
        tag = 0x52;
        break;
    case 0x34:
    case 0x41:
    case 0x42:
    case 0x47:
    case 0x48:
    case 0x4A:
        return;
    case 0x46:
        mixer.HoldTrackAt(2, 0.5f);
        return;
    case 0x49: {
        work->m_request.workingFrame->AddRef();
        equipment->SetModel(work->m_request.workingFrame, 7, -1, -8, -1);
        equipment->OnAnimationEvent(0x0B);
        work->PlayWorkAnimation(ACTIVITY_NONE, true);
        mixer.SetTrackTag(2, 0x53);
        const int length = GetActivityAnimLength(0x49, work->m_request.stance);
#ifndef HD2_DEDICATED   // no weapon pose on the server
        m_owner->GetMovement()->m_weaponPose->PauseWeapon(static_cast<int>(static_cast<float>(length) * 0.625f), false);
#endif
        break;
    }
    default:
        if (!InRange(activity[0], 0x1E, 0x33))
            tag = 0x51;
        break;
    }
    if (tag >= 0)
        mixer.SetTrackTag(2, tag);

    // How long the activity plays before the pending one.
    I3D_animation_set *set = (m_pendingActivity[0] < ACTIVITY_NONE || m_pendingActivity[0] == ACTIVITY_FADE_OUT) && table
                                 ? table->GetSlot(activity[0], activity[1]).anim
                                 : nullptr;
    if (!set) {
        m_activityDelay = MOVE_NO_TIME;
        return;
    }
    const int length = set->GetEndTime();
    m_activityDelay = length;
    const double scale = 1.0 / MOVE_TIME_SCALE;
    if (m_pendingActivity[0] != 0x41 && activity[0] != 0x49)
        m_activityDelay = static_cast<int>(static_cast<double>(length) * scale - 560.0 * scale);
    else
        m_activityDelay = static_cast<int>(static_cast<double>(length) / MOVE_TIME_SCALE);
}

// ------------------------------------------------------------------ adjusting the requests

void C_HumanAnimPlayer::AdjustAnimRequest(const S_HumanAnimRef &current, S_HumanAnimRef *next)
{
    C_HumanMovement *movement = m_movement;
    const C_HumanWork *work = m_owner->GetWork();
    const bool aiming = movement->m_handsCarryState == 2 || movement->m_handsCarryState == 3;
    const int carry = movement->m_animState.carryAnim;
    const bool heavy = InRange(carry, 0x46, 0x4F) || InRange(carry, 0x50, 0x52);
    const int anim = next->anim;
    if (InRange(anim, 0x6A, 0x6C) || anim == 8 || anim == 0x0E) {
        // Climbing a ladder fast.
        if (anim == 0x6B && (next->sub == 2 || work->m_request.gait == 5)) {
            next->anim = 0x6C;
            next->Normalize();
        }
        return;
    }
    if (movement->m_useState != MOVE_NO_TIME)
        return;
    bool moving = heavy;
    if (InRange(anim, 0x29, 0x41)) {
        if ((work->m_request.unknown5C != 0 && work->m_request.carryMode != 6 && (aiming || current.anim == 0x21)) || heavy) {
            // Standing with the weapon up: the aiming stances.
            if (next->stance == HUMAN_STANCE_STAND) {
                if (anim != 0x36 && anim != 0x2C && anim != 0x2B) {
                    next->anim = 0x36;
                    next->Normalize();
                }
            } else if (next->stance == HUMAN_STANCE_CROUCH) {
                if (anim != 0x3E) {
                    next->anim = 0x3E;
                    next->Normalize();
                }
            } else if (next->stance == HUMAN_STANCE_PRONE && g_nextAnim.anim != 0x41) {
                // (The original sets the shared request, which `next` is.)
                g_nextAnim.anim = 0x41;
                g_nextAnim.Normalize();
            }
        } else {
            // The aiming stances without the weapon up: back.
            if (current.stance == HUMAN_STANCE_STAND) {
                if (anim == 0x36) {
                    next->anim = ANIM_STAND;
                    next->Normalize();
                }
            } else if (current.stance == HUMAN_STANCE_CROUCH) {
                if (anim == 0x3E) {
                    next->anim = 0x37;
                    next->Normalize();
                }
            } else if (current.stance == HUMAN_STANCE_PRONE && g_nextAnim.anim == 0x41) {
                g_nextAnim.anim = 0x3F;
                g_nextAnim.Normalize();
            }
            moving = false;
        }
    }
    if (!moving && (work->m_request.unknown5C == 0 || work->m_request.carryMode == 6 || movement->m_unknown1AC != 1))
        return;
    // The moving animations with a heavy weapon are the 8 after the others.
    const int asked = next->anim;
    if (!InRange(asked, 0xBE, 0xCD)) {
        const int playing = current.anim;
        if (InRange(playing, 0xBE, 0xCD) && asked == HUMAN_ANIM_NONE) {
            if (playing < 0xC6) {
                if (heavy) {
                    next->anim = playing + 8;
                    next->Normalize();
                }
            } else if (!heavy) {
                next->anim = playing - 8;
                next->Normalize();
            }
        }
    } else if (heavy) {
        next->anim = asked + 8;
        next->Normalize();
    }
}

void C_HumanAnimPlayer::AdjustCarryRequest(const S_HumanAnimRef &current, const int *activity)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    S_HumanAnimState &next = movement->m_nextAnimState;
    const int nextCarry = next.carryAnim;
    if (nextCarry != CARRY_ANIM_PUT_AWAY && movement->m_unknown1AC == 1) {
        const int anim = current.anim;
        const bool free = ((anim < 0 || anim > 0x28 || anim == 9 || anim == 10) && anim != 0x38 && anim != 0x67) ||
                          movement->m_handsCarryState < 2 || nextCarry == 0x22 || anim == 0x1A || anim == 0x1B;
        if (!free) {
            // Climbing and the like: the hands item is put away.
            next.carryAnim = CARRY_ANIM_PUT_AWAY;
            movement->m_handsCarryState = 4;
            equipment->OnAnimationEvent(EQUIPMENT_EVENT_1);
        } else if (!InRange(anim, 0x6A, 0x6C) && (!InRange(anim, 0x59, 0x5D) || nextCarry == 0x22 || nextCarry == 0x23)) {
            const int hands = movement->m_handsCarryState;
            // The weapon held up (3 / 4) or a held item.
            const auto readyFlags = [&]() {
                const uint32_t flags = work->m_bodyFlags;
                work->m_bodyFlags = flags | 8;
                if ((movement->m_animState.carryAnim == 0x20 || work->m_request.carryMode == 6) && movement->m_unknownA28 == 0)
                    work->m_bodyFlags = flags | 0x108;
            };
            if (hands == 3 || hands == 4) {
                if (work->m_request.stance == HUMAN_STANCE_PRONE && anim != 0x21 && anim != 0x1F && nextCarry != 0x22 &&
                    nextCarry != 0x23 && nextCarry != 0x24 && !InRange(movement->m_animState.anim.anim, 0x29, 0x41) &&
                    !InRange(anim, 0xA2, 0xA5)) {
                    // Prone: put away.
                    next.carryAnim = CARRY_ANIM_PUT_AWAY;
                    equipment->OnAnimationEvent(EQUIPMENT_EVENT_1);
                    movement->m_handsCarryState = 4;
                } else if (anim != HUMAN_ANIM_NONE && nextCarry != 0x22) {
                    if (work->m_request.action == 0 || work->m_request.carryMode == 6) {
                        const int act = *activity;
                        if ((!InRange(act, 8, 0x1D) && !InRange(act, 0x34, 0x4A)) || act == 0x4A) {
                            if (movement->m_unknownA24 == 1) {
                                movement->m_unknownA24 = 0;
                            } else if (movement->m_unknownA24 == 0) {
                                readyFlags();
                                if (next.carryAnim < 0x21 && work->m_request.carryMode != 6 && work->m_request.carryMode != 2 &&
                                    !InRange(movement->m_animState.carryAnim, 0x50, 0x52))
                                    equipment->OnAnimationEvent(EQUIPMENT_EVENT_0);
                            }
                        } else if (movement->m_unknownA24 == 0) {
                            if (work->m_request.carryMode != 6)
                                equipment->OnAnimationEvent(EQUIPMENT_EVENT_1);
                            movement->m_unknownA24 = MoveTime(550.0f);
                        }
                        const int mode = work->m_request.carryMode;
                        if (mode == 6 || (equipment->GetFlags() & EQUIPMENT_HANDS_PUT_AWAY) || mode == 100) {
                            if (InRange(movement->m_animState.anim.anim, 0x29, 0x41))
                                ReturnToStand();
                        } else if (mode != 0) {
                            if (work->m_request.unknown5C == 0) {
                                next.carryAnim = m_owner->GetRole() == HUMAN_ROLE_PLAYER ? 0x1E : 0x1F;
                                movement->m_handsCarryState = 3;
                                if (InRange(movement->m_animState.anim.anim, 0x29, 0x41))
                                    ReturnToStand();
                            } else if (work->m_request.unknown5C == 1) {
                                if (mode == 5 && work->m_request.stance == HUMAN_STANCE_PRONE) {
                                    next.carryAnim = m_owner->GetRole() == HUMAN_ROLE_PLAYER ? 0x1E : 0x1F;
                                    movement->m_handsCarryState = 3;
                                } else {
                                    // The weapon up: the aiming stance of the stance.
                                    next.carryAnim = 0x20;
                                    movement->m_handsCarryState = 3;
                                    const S_HumanAnimRef &playing = movement->m_animState.anim;
                                    if (InRange(playing.anim, 0x29, 0x41) && playing.anim != 0x2C && playing.anim != 0x2B) {
                                        if (playing.stance == HUMAN_STANCE_STAND) {
                                            if (playing.anim != 0x36)
                                                work->Play(0x36, true);
                                        } else if (playing.stance == HUMAN_STANCE_CROUCH) {
                                            if (playing.anim != 0x3E)
                                                work->Play(0x3E, true);
                                        } else if (playing.stance == HUMAN_STANCE_PRONE && playing.anim != 0x41) {
                                            work->Play(0x41, true);
                                        }
                                    }
                                }
                            }
                        }
                    } else if (nextCarry != 0x22 && work->m_request.action < 3) {
                        readyFlags();
                    }
                }
            } else if (hands == 0 && movement->m_animState.carryAnim == CARRY_ANIM_NONE) {
                ReturnToStand();
            }
        }
    }
    if (next.carryAnim == 0x23 && !(movement->m_unknown190 & 1))
        next.carryAnim = 0x24;
}

void C_HumanAnimPlayer::AdjustActivityRequest(const S_HumanAnimRef & /*current*/, const int * /*carry*/, int *next)
{
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    switch (next[0]) {
    case 0x13:
        // The flashlight off.
        next[0] = ACTIVITY_FADE_OUT;
        equipment->SetFlashlight(false);
        equipment->OnAnimationEvent(10);
        m_movement->m_mixer.SetTrackTag(2, -1);
        m_unknown58 = MOVE_NO_TIME;
        m_activityDelay = MOVE_NO_TIME;
        break;
    case 0x3D:
        next[0] = ACTIVITY_FADE_OUT;
        work->m_forced = true;
        work->PlayWorkAnimation(ACTIVITY_NONE, true);
        work->m_forced = false;
        break;
    case 0x41: {
        // With the item of belt behaviour 0xE in the hands.
        if (C_Item *item = m_owner->FindBeltItem(0x0E))
            work->SetWorkingFrame(item->GetFrame(0), true);
        next[0] = 0x17;
        break;
    }
    default:
        break;
    }
}

void C_HumanAnimPlayer::ReturnToStand()
{
    const S_HumanAnimRef &anim = m_movement->m_animState.anim;
    C_HumanWork *work = m_owner->GetWork();
    if (anim.stance == HUMAN_STANCE_STAND) {
        if (anim.anim == 0x36)
            work->Play(ANIM_STAND, true);
    } else if (anim.stance == HUMAN_STANCE_CROUCH) {
        if (anim.anim == 0x3E)
            work->Play(0x37, true);
    } else if (anim.stance == HUMAN_STANCE_PRONE && anim.anim == 0x41) {
        work->Play(0x3F, true);
    }
}

// ------------------------------------------------------------------ the special animations

void C_HumanAnimPlayer::PlayAnimation(const char *name, int blendTime, int fadeOut, int loop)
{
    C_HumanAnimMixer &mixer = m_movement->m_mixer;
    if (m_movement->m_unknown1AC != 1 || !name || !*name) {
        mixer.FadeOutTrack3(blendTime);
        return;
    }
    // (The original also builds "Neuspesny pokus nastavit specialni animaci: <name> - <error>"
    // from the result and never shows it.)
    I3D_animation_set *anim = nullptr;
    g_animationCache.Get(name, &anim);
    if (!anim) {
        mixer.FadeOutTrack3(blendTime);
        return;
    }
    mixer.PlayTrack3(anim, blendTime, fadeOut, (loop & 0xFF) != 0);
    g_startAnims[0].anim = nullptr;
    g_startAnims[1].anim = nullptr;
    g_startAnims[2].anim = nullptr;
    g_startAnims[3].anim = anim;
    mixer.UpdateStages(0, 1, true, g_startAnims);
    anim->Release();
}

// ------------------------------------------------------------------ save games

bool C_HumanAnimPlayer::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_ANIM_PLAYER))
        return false;
    chunk.WriteChunk(CHUNK_AP_PENDING_ANIM, &m_pendingAnim, sizeof(m_pendingAnim));
    chunk.WriteChunk(CHUNK_AP_ANIM_DELAY, m_animDelay);
    chunk.WriteChunk(CHUNK_AP_ANIM_DELAY_STATE, m_animDelayState);
    chunk.WriteChunk(CHUNK_AP_ANIM_BLOCKED, m_animBlocked);
    chunk.WriteChunk(CHUNK_AP_PENDING_CARRY, m_pendingCarry, sizeof(m_pendingCarry));
    chunk.WriteChunk(CHUNK_AP_CARRY_DELAY, m_carryDelay);
    chunk.WriteChunk(CHUNK_AP_CARRY_DELAY_STATE, m_carryDelayState);
    chunk.WriteChunk(CHUNK_AP_CARRY_BLOCKED, m_carryBlocked);
    chunk.WriteChunk(CHUNK_AP_PENDING_ACTIVITY, m_pendingActivity, sizeof(m_pendingActivity));
    chunk.WriteChunk(CHUNK_AP_ACTIVITY_DELAY, m_activityDelay);
    chunk.WriteChunk(CHUNK_AP_ACTIVITY_DELAY_STATE, m_activityDelayState);
    chunk.WriteChunk(CHUNK_AP_ACTIVITY_BLOCKED, m_activityBlocked);
    chunk.WriteChunk(CHUNK_AP_UNKNOWN5C, m_unknown5C);
    chunk.WriteChunk(CHUNK_AP_UNKNOWN60, m_unknown60);
    chunk.WriteChunk(CHUNK_AP_UNKNOWN70, &m_unknown70, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_AP_UNKNOWN58, m_unknown58);
    chunk.WriteChunk(CHUNK_AP_UNKNOWN90, &m_unknown90, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_AP_UNKNOWNA0, m_unknownA0);
    chunk.Ascend();
    return true;
}

bool C_HumanAnimPlayer::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_ANIM_PLAYER) {
        chunk.Ascend();
        return false;
    }
    const auto readVector = [&chunk](S_vector4 &v) {
        S_vector value(0.0f, 0.0f, 0.0f);
        chunk.Read(value);
        v.x = value.x;
        v.y = value.y;
        v.z = value.z;
    };
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_AP_PENDING_ANIM:
            chunk.Read(&m_pendingAnim, sizeof(m_pendingAnim));
            break;
        case CHUNK_AP_ANIM_DELAY:
            chunk.Read(m_animDelay);
            break;
        case CHUNK_AP_ANIM_DELAY_STATE:
            chunk.Read(m_animDelayState);
            break;
        case CHUNK_AP_ANIM_BLOCKED:
            chunk.Read(m_animBlocked);
            break;
        case CHUNK_AP_PENDING_CARRY:
            chunk.Read(m_pendingCarry, sizeof(m_pendingCarry));
            break;
        case CHUNK_AP_CARRY_DELAY:
            chunk.Read(m_carryDelay);
            break;
        case CHUNK_AP_CARRY_DELAY_STATE:
            chunk.Read(m_carryDelayState);
            break;
        case CHUNK_AP_CARRY_BLOCKED:
            chunk.Read(m_carryBlocked);
            break;
        case CHUNK_AP_PENDING_ACTIVITY:
            chunk.Read(m_pendingActivity, sizeof(m_pendingActivity));
            break;
        case CHUNK_AP_ACTIVITY_DELAY:
            chunk.Read(m_activityDelay);
            break;
        case CHUNK_AP_ACTIVITY_DELAY_STATE:
            chunk.Read(m_activityDelayState);
            break;
        case CHUNK_AP_ACTIVITY_BLOCKED:
            chunk.Read(m_activityBlocked);
            break;
        case CHUNK_AP_UNKNOWN5C:
            chunk.Read(m_unknown5C);
            break;
        case CHUNK_AP_UNKNOWN60:
            chunk.Read(m_unknown60);
            break;
        case CHUNK_AP_UNKNOWN70:
            readVector(m_unknown70);
            break;
        case CHUNK_AP_UNKNOWN58:
            chunk.Read(m_unknown58);
            break;
        case CHUNK_AP_UNKNOWN90:
            readVector(m_unknown90);
            break;
        case CHUNK_AP_UNKNOWNA0:
            chunk.Read(m_unknownA0);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}
