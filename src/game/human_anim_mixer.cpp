// The animation mixer of a human (C_HumanAnimMixer, C_HumanMovement +0x260; original source
// 0x00772520..0x00777d10): the tracks blend their animations in and out on the 8 stages of the
// model and advance them at a rate from the body's speed; the root motion velocity the
// animation player reports and the moves it asks for give the move of the body; the note keys
// of the animations come back as events of the tracks, sounds and the muzzle flash.
#include "game/human_movement.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_anim_tables.h"
#include "game/human_internal.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

constexpr int CARRY_ANIM_NONE = 0x5B;
constexpr int ACTIVITY_NONE = 0x4B;

// The note frame of the models: its callback gets the note keys of the animations.
const char NOTE_FRAME_NAME[] = "a";                     // 0x00839100

// Steps closer than this (game ms) make no new step event.
constexpr float STEP_EVENT_INTERVAL = 180.0f;
// The slides move in steps of 80 ms.
constexpr int SLIDE_STEP_TIME = 80;
// The lowest share SetHoldTime takes.
constexpr float MIN_HOLD_SHARE = 0.22f;
// In the first person view the note sounds are moved this far along the note frame (m).
constexpr float FIRST_PERSON_SOUND_OFFSET = 0.666f;

enum : uint16_t {
    CHUNK_MIXER = 0x378,
    CHUNK_MIXER_TRACKS = 0x00,
    CHUNK_MIXER_STAGES = 0x01,
    CHUNK_MIXER_PREVIOUS_ANIM = 0x02,
    CHUNK_MIXER_PREVIOUS_CARRY = 0x03,
    CHUNK_MIXER_PREVIOUS_ACTIVITY = 0x04,
    CHUNK_MIXER_CURRENT_ANIM = 0x05,
    CHUNK_MIXER_CURRENT_CARRY = 0x06,
    CHUNK_MIXER_CURRENT_ACTIVITY = 0x07,
    CHUNK_MIXER_BLEND_PROGRESS = 0x08,
    CHUNK_MIXER_VELOCITY_A = 0x09,
    CHUNK_MIXER_WEIGHT_A = 0x0A,
    CHUNK_MIXER_VELOCITY_B = 0x0B,
    CHUNK_MIXER_WEIGHT_B = 0x0C,
    CHUNK_MIXER_MAX_SPEED = 0x0D,
    CHUNK_MIXER_MOVE = 0x10,
    CHUNK_MIXER_ADD_MOVE = 0x11,
    CHUNK_MIXER_ADD_MOVE_TIME = 0x12,
    CHUNK_MIXER_ADD_MOVE_DURATION = 0x13,
    CHUNK_MIXER_EXTRA_MOVE = 0x14,
    CHUNK_MIXER_ADD_MOVE2 = 0x17,
    CHUNK_MIXER_ADD_MOVE2_TIME = 0x18,
    CHUNK_MIXER_ADD_MOVE2_DURATION = 0x19,
    CHUNK_MIXER_SLIDE = 0x1A,
    CHUNK_MIXER_SLIDE_VELOCITY = 0x1B,
    CHUNK_MIXER_SLIDE_STEP_TIME = 0x1C,
    CHUNK_MIXER_SLIDE_TIME = 0x1D,
    CHUNK_MIXER_SLIDE_DURATION = 0x1E,
    CHUNK_MIXER_ANIM_QUEUE = 0x1F,
    CHUNK_MIXER_CARRY_QUEUE = 0x20,
    CHUNK_MIXER_ACTIVITY_QUEUE = 0x21,
    CHUNK_MIXER_MOVE_TO_SPEED = 0x22,
    CHUNK_MIXER_MOVE_TO_TARGET = 0x23,
    CHUNK_MIXER_MOVE_TO_TIME = 0x24,
    CHUNK_MIXER_MOVE_TO_DURATION = 0x25,
    CHUNK_MIXER_SPEED_SCALE = 0x26,
    CHUNK_MIXER_TRACK3_FADE_OUT = 0x27,
    CHUNK_MIXER_HOLD_TIME = 0x28,
    // Per stage s at 0x50 + 6 * s: the animation file (without the path), its play mode, the
    // weight and the time of the stage.
    CHUNK_MIXER_STAGE_ANIM = 0x50,
    CHUNK_MIXER_STAGE_MODE = 0x51,
    CHUNK_MIXER_STAGE_WEIGHT = 0x52,
    CHUNK_MIXER_STAGE_TIME = 0x53,
    CHUNK_MIXER_STAGE_STRIDE = 6,
    CHUNK_MIXER_STAGE_LAST = 0x80,
};

// The standing animations (0x2D..0x41; inferred from the ids around ANIM_STAND 0x2D).
bool IsStandAnim(int anim) { return anim > 0x2C && anim < 0x42; }
// The moving animations (the ones with 3 x 6 variants).
bool IsMovingAnim(int anim) { return anim >= HUMAN_ANIM_FIRST_6X && anim < HUMAN_ANIM_NONE; }

// A blend of the stage is under way.
bool IsBlending(const S_MixerStage &stage)
{
    return stage.blendTime > 0.0f && stage.blendElapsed >= 0.0f && stage.blendElapsed < stage.blendTime;
}

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &p)
{
    v.x = p.x;
    v.y = p.y;
    v.z = p.z;
}

void AddXYZ(S_vector4 &v, const S_vector &p)
{
    v.x += p.x;
    v.y += p.y;
    v.z += p.z;
}

// Adds an event of a track at `index` (normally its count).
// Fix: at most MIXER_TRACK_EVENTS events an update (the original writes on into the fields
// after the array when more notes come).
void AddEventAt(S_MixerTrack &track, int index, int event)
{
    if (index < MIXER_TRACK_EVENTS)
        track.events[index] = event;
    if (track.eventCount < MIXER_TRACK_EVENTS)
        ++track.eventCount;
}

void AddEvent(S_MixerTrack &track, int event) { AddEventAt(track, track.eventCount, event); }

// Takes the next entry of a queue (it must not be empty); a locked queue opens when empty.
template <class T> const T &Pop(S_MixerQueue<T> &queue)
{
    const T &entry = queue.entries[queue.read];
    if (++queue.read > 1)
        queue.read = 0;
    if (queue.locked && queue.read == queue.write)
        queue.locked = false;
    return entry;
}

// Whether a queue takes a new entry (a locked queue opens when empty).
template <class T> bool IsOpen(S_MixerQueue<T> &queue)
{
    if (queue.locked && queue.read == queue.write)
        queue.locked = false;
    return !queue.locked;
}

// Moves the write position on (a full ring drops its oldest entry).
template <class T> void Advance(S_MixerQueue<T> &queue)
{
    if (++queue.write > 1)
        queue.write = 0;
    if (queue.write == queue.read)
        ++queue.read;
    if (queue.read > 1)
        queue.read = 0;
}

// Queues an animation unless the queue is locked or its last entry is the same.
void PushAnim(S_MixerQueue<S_HumanAnimRef> &queue, const S_HumanAnimRef &anim)
{
    if (!IsOpen(queue))
        return;
    const int write = queue.write;
    if (queue.read != write && queue.entries[write == 0 ? 1 : write - 1].IsSame(anim))
        return;
    queue.entries[write].Set(anim.anim, anim.stance, anim.sub);
    Advance(queue);
}

// The same for a carry animation (its id and variant).
void PushCarryEntry(S_MixerQueue<S_MixerCarry> &queue, int anim, int variant)
{
    if (!IsOpen(queue))
        return;
    const int write = queue.write;
    const S_MixerCarry &last = queue.entries[write == 0 ? 1 : write - 1];
    if (queue.read != write && last.anim == anim && last.variant == variant)
        return;
    queue.entries[write].anim = anim;
    queue.entries[write].variant = variant;
    Advance(queue);
}

// Plays a note sound at the note frame; in the first person view of the player it is moved
// along the frame's z axis.
void PlayNoteRequest(S_SoundRequest &request, C_Human *human)
{
    // (`human` is any actor of the model; the original takes it for a human.)
    if (human && human->IsLocalPlayer() && GetGame()->IsFirstPersonView() && request.attachTo) {
        const S_vector dir = request.attachTo->GetLocalDir();
        request.pos += dir * FIRST_PERSON_SOUND_OFFSET;
    }
    request.flags = SOUND_SILENT_TO_AI;
    GetSoundModule()->Play(request);
}

// The sounds of the note keys (0x00775e30): "!a b" plays dubbing number b (0 stops it),
// "$file near far" a sound file, "@group" a sound of the action chapter.
void PlayNoteSound(I3D_frame *frame, const char *note)
{
    I3D_frame *model = frame->GetParent();
    C_Actor *actor = model && model->GetData() ? model->GetData()->actor : nullptr;
    C_Human *human = static_cast<C_Human *>(actor);
    if (!note)
        return;
    switch (note[0]) {
    case '!': {
        // (The first number is read and not used.)
        std::atoi(note + 1);
        // Fix: a note shorter than "!a b" is off (the original reads past its end).
        const int dubbing = std::strlen(note) >= 3 ? std::atoi(note + 3) : 0;
        C_Human *owner = actor && actor->GetType() == ACTOR_HUMAN ? human : nullptr;
        GetSoundModule()->PlayDubbing(owner, model, dubbing, 0.0f, 20.0f, true);
        break;
    }
    case '$': {
        S_SoundRequest request;
        // Fix: the copy is bounded (the original copies the whole note into 256 bytes).
        char text[256];
        std::strncpy(text, note, sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
        char *nearText = std::strchr(text + 1, ' ');
        if (!nearText)
            return;
        *nearText++ = '\0';
        request.fileName = text + 1;
        char *farText = std::strchr(nearText, ' ');
        if (!farText)
            return;
        *farText++ = '\0';
        std::sscanf(nearText, "%f", &request.nearRange);
        std::sscanf(farText, "%f", &request.farRange);
        request.source = actor;
        // (Looked for under the note frame itself, unlike '@', which looks under the model.)
        request.attachTo = frame->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY);
        PlayNoteRequest(request, human);
        break;
    }
    case '@': {
        S_SoundRequest request;
        request.group = std::atoi(note + 1);
        request.chapter = SOUND_CHAPTER_ACTION;
        request.source = actor;
        request.attachTo = model->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY);
        PlayNoteRequest(request, human);
        break;
    }
    default:
        break;
    }
}

}  // namespace

float __stdcall MixerBlendProgress(float elapsed, float step, float duration)
{
    const float percent = duration * 0.01f;
    const float before = (elapsed - step) / percent;
    return ((elapsed / percent - before) * 0.5f + before) * 0.01f;
}

// ------------------------------------------------------------------ setting up

void C_HumanAnimMixer::Init(C_Human *owner)
{
    m_animQueue.read = m_animQueue.write = 0;
    m_animQueue.locked = false;
    m_carryQueue.read = m_carryQueue.write = 0;
    m_carryQueue.locked = false;
    m_activityQueue.read = m_activityQueue.write = 0;
    m_activityQueue.locked = false;
    m_owner = owner;
    m_model = static_cast<I3D_model *>(owner->GetFrame());
    // (Looked for from the model's parent.)
    // Fix: a model without a parent has no note frame (the original calls through null).
    I3D_frame *parent = m_model->GetParent();
    m_noteFrame = parent ? parent->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY) : nullptr;
    for (S_MixerTrack &track : m_tracks) {
        track.current = -1;
        track.time = -1;
        track.hold = 0;
        track.eventCount = 0;
        track.length = 0;
        track.reversed = false;
    }
    // (Stage 7 is not reset: the original's loop runs over the first 7.)
    for (int i = 0; i < MIXER_STAGES - 1; ++i) {
        S_MixerStage &stage = m_stages[i];
        stage.blendElapsed = MIXER_NO_BLEND;
        stage.blendTime = 0.0f;
        stage.weight = 0.0f;
        stage.crossfade = true;
        stage.flags = 0;
        stage.rate = 1.0f;
        stage.speed = 1.0f;
        stage.tag = -1;
        stage.finished = false;
    }
    for (int stage = 0; stage < MIXER_STAGES; ++stage)
        m_model->SetAnimation(nullptr, stage, 1.0f, 0);
    m_model->SetPoseWeight(0);
    if (I3D_frame *frame = owner->GetFrame()->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY))
        frame->SetCallback(NoteHook, I3DCALLBACK_ANIM_NOTE);
    SetXYZ(m_velocityA, S_vector(0.0f, 0.0f, 0.0f));
    m_track3FadeOut = -1;
    m_blendProgress = 0.0f;
    m_weightA = 0.0f;
    m_weightB = 0.0f;
    m_maxSpeed = 0.0f;
    SetXYZ(m_velocityB, S_vector(0.0f, 0.0f, 0.0f));
    SetXYZ(m_move, S_vector(0.0f, 0.0f, 0.0f));
    m_addMoveDuration = MOVE_NO_TIME;
    m_moveToDuration = MOVE_NO_TIME;
    m_addMove2Duration = MOVE_NO_TIME;
    m_slideDuration = MOVE_NO_TIME;
    SetXYZ(m_extraMove, S_vector(0.0f, 0.0f, 0.0f));
    m_holdTime = MOVE_NO_TIME;
    m_stepSoundTime = 0;
    m_speedScale = 1.0f;
    m_previous.anim.Set(HUMAN_ANIM_NONE, HUMAN_STANCE_STAND, 1);
    m_previous.carryAnim = CARRY_ANIM_NONE;
    m_previous.carryVariant = 0;
    m_previous.activityVariant = 0;
    m_previous.activity = ACTIVITY_NONE;
    m_current.anim.Set(HUMAN_ANIM_NONE, HUMAN_STANCE_STAND, 1);
    m_current.carryAnim = CARRY_ANIM_NONE;
    m_current.carryVariant = 0;
    m_current.activity = ACTIVITY_NONE;
    m_current.activityVariant = 0;
}

void C_HumanAnimMixer::Done()
{
    if (I3D_frame *frame = m_model->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY))
        frame->SetCallback(nullptr, 0);
    for (int stage = 0; stage < MIXER_STAGES; ++stage)
        m_model->SetAnimation(nullptr, stage, 1.0f, 0);
}

void C_HumanAnimMixer::Relink(I3D_model *model, I3D_model *previous)
{
    m_model = model;
    I3D_frame *frame = model->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY);
    if (frame)
        frame->SetCallback(NoteHook, I3DCALLBACK_ANIM_NOTE);
    m_noteFrame = frame;
    if (!previous || previous == model)
        return;
    for (int stage = 0; stage < MIXER_STAGES; ++stage) {
        if (!previous->GetAnimation(stage))
            continue;
        const float weight = previous->GetAnimWeight(stage);
        model->SetAnimation(previous->GetAnimation(stage), stage, weight, 0);
        model->SetAnimTime(stage, previous->GetAnimTime(stage));
    }
}

// ------------------------------------------------------------------ the stages

void C_HumanAnimMixer::UpdateStages(int /*time*/, int step, bool start, const S_MixerAnim *anims)
{
    m_blendProgress = 0.0f;
    if (!start) {
        // The events of the last update are gone (track 3 has none).
        for (int track = 0; track < 3; ++track)
            m_tracks[track].eventCount = 0;
    }
    if (step == -1 && !start)
        return;
    m_stepSoundTime += step;
    const float stepTime = static_cast<float>(step);
    if (!start) {
        // The stages run at the movement's clock, the body tracks also at their rate and speed.
        int times[MIXER_STAGES];
        for (int stage = 0; stage < MIXER_STAGES; ++stage) {
            m_owner->IsLocalPlayer();                   // (asked and not used)
            const float factor = m_stages[stage].speed * m_stages[stage].rate;
            times[stage] = stage < 6 ? static_cast<int>(stepTime * MOVE_TIME_SCALE * factor)
                                     : static_cast<int>(stepTime * MOVE_TIME_SCALE);
        }
        m_model->AdvanceStages(times);
    }
    for (int track = 0; track < MIXER_TRACKS; ++track) {
        if (start && anims && !anims[track].anim)
            continue;
        S_MixerTrack &trk = m_tracks[track];
        if (trk.current == -1)
            continue;
        const int current = 2 * track + trk.current;
        const int other = 2 * track + (trk.current == 0 ? 1 : 0);
        S_MixerStage &stage = m_stages[current];
        if (trk.hold < 1) {
            trk.time = static_cast<int>(stepTime * MOVE_TIME_SCALE + static_cast<float>(trk.time));
            if (m_holdTime >= 0 && track == 0) {
                const int holdTime = m_holdTime;
                if (m_model->GetAnimTime(current) >= holdTime) {
                    HoldTrack(0, holdTime);
                    m_tracks[0].time = m_holdTime;
                    m_model->SetAnimTime(current, m_holdTime);
                    m_holdTime = MOVE_NO_TIME;
                }
            }
            if (trk.time > 0x7FFFFC17)
                trk.time = 100000;
            if (m_track3FadeOut >= 0 && track == 3 && m_tracks[3].time + m_track3FadeOut >= m_tracks[3].length)
                FadeOutTrack3(m_track3FadeOut);
            stage.blendElapsed += stepTime;
        } else {
            // Held: only a blend under way goes on.
            if (stage.blendElapsed < 0.0f)
                continue;
            if (stage.blendElapsed < stage.blendTime)
                stage.blendElapsed += stepTime;
        }

        if (stage.blendElapsed < stage.blendTime) {
            const float progress = MixerBlendProgress(stage.blendElapsed, stepTime, stage.blendTime);
            if (track == 0)
                m_blendProgress = progress;
            if (stage.flags & MIXER_STAGE_FADE_OUT) {
                // Both stages of the track go out.
                const float weight = (1.0f - progress) * stage.weight;
                m_model->SetAnimWeight(current, weight);
                if (m_model->GetAnimWeight(other) >= weight)
                    m_model->SetAnimWeight(other, weight);
            } else if (stage.blendTime > 0.0f) {
                if (stage.crossfade) {
                    m_model->SetAnimWeight(other, (1.0f - progress) * m_stages[other].weight);
                    m_model->SetAnimWeight(current, progress * stage.weight);
                } else if (other > current) {
                    // The old animation is on the stage above: it fades out.
                    m_model->SetAnimWeight(other, (1.0f - progress) * m_stages[other].weight);
                } else {
                    // The new one is on the stage above: it fades in.
                    m_model->SetAnimWeight(current, progress * stage.weight);
                }
            }
            continue;
        }

        // The blend is done: the old animation goes off the other stage.
        if (stage.flags & MIXER_STAGE_FADE_OUT) {
            trk.current = -1;
            if (track == 0) {
                m_current.anim.anim = HUMAN_ANIM_NONE;
                m_current.anim.Normalize();
            } else if (track == 1) {
                m_current.carryAnim = CARRY_ANIM_NONE;
            } else if (track == 2) {
                m_current.activity = ACTIVITY_NONE;
            }
        } else {
            m_model->SetAnimWeight(current, stage.weight);
        }
        S_MixerStage &out = m_stages[other];
        if (out.blendElapsed != MIXER_NO_BLEND) {
            m_model->SetAnimWeight(other, 0.0f);
            m_model->StopAnimation(other);
            m_model->SetAnimation(nullptr, other, 1.0f, 0);
            out.rate = 1.0f;
            out.speed = 1.0f;
            out.blendElapsed = MIXER_NO_BLEND;
            out.flags = 0;
            out.finished = true;
        }
        if (stage.flags & MIXER_STAGE_FADE_OUT) {
            m_model->StopAnimation(current);
            m_model->SetAnimWeight(current, 0.0f);
            stage.rate = 1.0f;
            stage.speed = 1.0f;
            stage.flags = 0;
            stage.blendElapsed = MIXER_NO_BLEND;
            stage.finished = true;
        }
        if (track == 0) {
            m_previous.anim.anim = HUMAN_ANIM_NONE;
            m_previous.anim.Normalize();
        } else if (track == 1) {
            m_previous.carryAnim = CARRY_ANIM_NONE;
        } else if (track == 2) {
            m_previous.activity = ACTIVITY_NONE;
        }
        m_blendProgress = 1.0f;
    }
}

void C_HumanAnimMixer::Play(S_MixerAnim *anims, const S_HumanAnimRef *anim, const int *carry, const int *activity,
                            bool restart)
{
    for (int track = 0; track < 3; ++track) {
        S_MixerAnim &next = anims[track];
        if (!next.anim)
            continue;
        if (track == 0)
            m_holdTime = MOVE_NO_TIME;
        S_MixerTrack &trk = m_tracks[track];
        const int was = trk.current;
        trk.hold = 0;
        trk.time = 0;
        trk.current = was == 1 ? 0 : 1;
        trk.length = next.anim->GetEndTime();
        trk.reversed = false;
        const int index = 2 * track + trk.current;
        S_MixerStage &stage = m_stages[index];
        if (was == -1) {
            m_model->SetAnimation(next.anim, index, next.crossfade ? 0.0f : next.weight, 0);
            stage.weight = next.weight;
            stage.blendTime = next.blendTime;
            stage.crossfade = next.crossfade;
            stage.blendElapsed = 0.0f;
            stage.flags = 0;
            stage.rate = 1.0f;
            stage.speed = 1.0f;
            continue;
        }
        // Asked back for the animation that blends out on the other stage: the blend turns
        // round from where it is (and the time is kept: the mode no longer syncs it).
        const S_MixerStage &out = m_stages[2 * track + was];
        bool back = false;
        if (track == 0) {
            back = m_previous.anim.anim != HUMAN_ANIM_NONE && m_previous.anim.IsSame(*anim) &&
                   out.blendElapsed > 0.0f && !restart;
        } else if (track == 2) {
            back = m_previous.activity != ACTIVITY_NONE && m_previous.activity == activity[0] &&
                   m_previous.activityVariant == activity[1] && out.blendElapsed > 0.0f;
        }
        if (back) {
            stage.blendElapsed = (1.0f - out.blendElapsed / out.blendTime) * next.blendTime;
            next.mode = I3DANIM_LOOP;
        } else {
            // The stage above starts at nothing; the one below at its weight unless it
            // cross-fades.
            const float weight = trk.current == 1 || next.crossfade ? 0.0f : next.weight;
            m_model->SetAnimation(next.anim, index, weight, 0);
            stage.blendElapsed = 0.0f;
        }
        stage.weight = next.weight;
        stage.blendTime = next.blendTime;
        stage.crossfade = next.crossfade;
        stage.flags = 0;
        stage.rate = 1.0f;
        stage.speed = 1.0f;
        SyncTrackTime(anims, track);
    }
    if (anim->anim != HUMAN_ANIM_NONE && anims[0].anim) {
        m_previous.anim.Set(m_current.anim.anim, m_current.anim.stance, m_current.anim.sub);
        m_current.anim.Set(anim->anim, anim->stance, anim->sub);
    }
    if (carry[0] != CARRY_ANIM_NONE && anims[1].anim) {
        m_previous.carryAnim = m_current.carryAnim;
        m_previous.carryVariant = m_current.carryVariant;
        m_current.carryAnim = carry[0];
        m_current.carryVariant = carry[1];
    }
    if (activity[0] != ACTIVITY_NONE && anims[2].anim) {
        m_previous.activity = m_current.activity;
        m_previous.activityVariant = m_current.activityVariant;
        m_current.activity = activity[0];
        m_current.activityVariant = activity[1];
    }
}

void C_HumanAnimMixer::SyncTrackTime(const S_MixerAnim *anims, int track)
{
    const int current = m_tracks[track].current;
    if (current == -1)
        return;
    const int stage = 2 * track + current;
    const int other = 2 * track + (current == 0 ? 1 : 0);
    const uint8_t mode = static_cast<uint8_t>(anims[track].mode);
    if ((mode & MIXER_SYNC_TIME_LEFT) == MIXER_SYNC_TIME_LEFT) {
        // As much left to play as of the old animation (all of it when that is longer).
        if (!(m_stages[other].blendTime > 0.0f))
            return;
        const int otherTime = m_model->GetAnimTime(other);
        I3D_animation_set *old = m_model->GetAnimation(other);
        if (!old)
            return;
        const int left = old->GetEndTime() - otherTime;
        I3D_animation_set *now = m_model->GetAnimation(stage);
        if (!now)
            return;
        int time = now->GetEndTime();
        if (left > 0) {
            if (left < time)
                time -= left;
            m_model->SetAnimTime(stage, time);
        }
    } else if ((mode & MIXER_SYNC_SHARE) == MIXER_SYNC_SHARE && m_stages[other].blendTime > 0.0f) {
        // At the same share of the length as the old animation.
        const int otherTime = m_model->GetAnimTime(other);
        I3D_animation_set *old = m_model->GetAnimation(other);
        if (!old)
            return;
        const int oldLength = old->GetEndTime();
        I3D_animation_set *now = m_model->GetAnimation(stage);
        if (!now)
            return;
        const int length = now->GetEndTime();
        if (oldLength > 0)
            m_model->SetAnimTime(stage, static_cast<int>(static_cast<double>(otherTime) / oldLength * length));
    }
}

// ------------------------------------------------------------------ the move of the body

void C_HumanAnimMixer::Update(int time)
{
    const float seconds = static_cast<float>(time) * 0.001f;
    // The weight of the velocity, or the speed it was limited to.
    float limited = m_weightB;
    const int current = m_tracks[0].current;
    S_vector velocity = XYZ(m_velocityB) * m_weightB;
    if (!m_owner->IsLocalPlayer() || m_blendProgress <= 0.0f || m_blendProgress >= 1.0f) {
        // (The player's own blends of the body animation are not limited.)
        m_weightA = 0.0f;
        if (m_maxSpeed > 0.0f) {
            const float length = velocity.Magnitude();
            if (length > m_maxSpeed) {
                // Down to the limit, or to half when that is more than twice over.
                limited = length <= m_maxSpeed + m_maxSpeed ? m_maxSpeed : length * 0.5f;
                velocity = velocity * (1.0f / length) * limited;
            }
        }
    }
    SetXYZ(m_move, velocity * seconds);
    if (current == -1)
        return;

    // The speed of the moving animations follows that of the body.
    float factor;
    if (m_owner->IsLocalPlayer()) {
        factor = m_speedScale;
    } else {
        if (m_weightB == 0.0f || limited == 0.0f) {
            factor = XYZ(m_velocityB).Square() >= 1e-16f ? 1.0f : 0.3f;
        } else {
            factor = limited / m_weightB;
            if (factor > 1.0f)
                factor = 1.0f;
            else if (factor < 0.4f)
                factor = 0.4f;
        }
        factor *= m_speedScale;
    }
    m_stages[current == 0 ? 1 : 0].speed = m_previous.anim.anim < HUMAN_ANIM_FIRST_6X ? 1.0f : factor;
    m_stages[current].speed = m_current.anim.anim < HUMAN_ANIM_FIRST_6X ? 1.0f : factor;

    if (m_addMoveDuration != MOVE_NO_TIME) {
        const int duration = m_addMoveDuration;
        m_addMoveTime += time;
        int part = time;
        if (duration < m_addMoveTime) {
            m_addMoveDuration = MOVE_NO_TIME;
            part = duration - m_addMoveTime + time;
        }
        if (duration > 0)
            AddXYZ(m_move, XYZ(m_addMove) * (static_cast<float>(part) / static_cast<float>(duration)));
    }
    if (m_moveToDuration != MOVE_NO_TIME) {
        const S_vector to = XYZ(m_moveToTarget) - m_model->GetWorldPos();
        const float distance = to.Magnitude();
        m_moveToTime += time;
        if (m_moveToDuration < m_moveToTime)
            m_moveToDuration = MOVE_NO_TIME;
        if ((m_moveToSpeed < distance && distance != 0.0f) || seconds * m_moveToSpeed < distance)
            AddXYZ(m_move, to * (1.0f / distance) * m_moveToSpeed * seconds);
        else
            AddXYZ(m_move, to * seconds);
    }
    if (m_addMove2Duration != MOVE_NO_TIME) {
        const int duration = m_addMove2Duration;
        m_addMove2Time += time;
        int part = time;
        if (duration < m_addMove2Time) {
            m_addMove2Duration = MOVE_NO_TIME;
            part = duration - m_addMove2Time + time;
        }
        if (duration > 0)
            AddXYZ(m_move, XYZ(m_addMove2) * (static_cast<float>(part) / static_cast<float>(duration)));
    }
    if (m_slideDuration != MOVE_NO_TIME) {
        int part = time;
        if (m_slideDuration <= m_slideTime + time) {
            part = m_slideDuration - m_slideTime;
            m_slideDuration = MOVE_NO_TIME;
        }
        m_slideTime += part;
        // Every 80 ms step takes its share off what is left.
        int left = part;
        while (left > 0) {
            if (static_cast<float>(m_slideStepTime + left) <= static_cast<float>(SLIDE_STEP_TIME)) {
                m_slideStepTime += left;
                break;
            }
            SetXYZ(m_slide, XYZ(m_slide) - XYZ(m_slideVelocity));
            left = static_cast<int>(static_cast<float>(left) - (static_cast<float>(SLIDE_STEP_TIME) -
                                                                  static_cast<float>(m_slideStepTime)));
            m_slideStepTime = 0;
        }
        AddXYZ(m_move, XYZ(m_slide) * (static_cast<float>(part) * 0.001f));
    }
    if (XYZ(m_extraMove).Square() >= 1e-16f) {
        m_extraMoveTime += time;
        int part = time;
        if (m_extraMoveDuration < m_extraMoveTime) {
            part = m_extraMoveDuration - m_extraMoveTime + time;
            // (Cleared before the last part is added: that part is lost.)
            SetXYZ(m_extraMove, S_vector(0.0f, 0.0f, 0.0f));
        }
        AddXYZ(m_move, XYZ(m_extraMove) * (static_cast<float>(part) / static_cast<float>(m_extraMoveDuration)));
    }
}

void C_HumanAnimMixer::SetVelocity(const S_vector4 &velocity, float weight, float maxSpeed, S_vector4 *result)
{
    m_velocityA = m_velocityB;
    m_weightA = m_weightB;
    m_velocityB = velocity;
    m_weightB = weight;
    if (m_weightA < weight)
        SetXYZ(*result, XYZ(m_velocityB) * m_weightB);
    else
        SetXYZ(*result, XYZ(m_velocityA) * m_weightA);
    m_maxSpeed = maxSpeed;
}

void C_HumanAnimMixer::ReplaceVelocity(const S_vector4 &velocity, float weight, float maxSpeed, S_vector4 *result)
{
    m_velocityB = velocity;
    m_weightB = weight;
    m_maxSpeed = maxSpeed;
    if (m_tracks[0].current == -1)
        return;
    if (weight <= m_weightA)
        SetXYZ(*result, XYZ(m_velocityA) * m_weightA);
    else
        SetXYZ(*result, XYZ(m_velocityB) * m_weightB);
}

void C_HumanAnimMixer::AddMove(const S_vector4 &move, int duration)
{
    m_addMove = move;
    m_addMoveTime = 0;
    m_addMoveDuration = duration;
}

void C_HumanAnimMixer::MoveTo(const S_vector4 &target, float speed, int duration)
{
    m_moveToSpeed = speed;
    m_moveToTarget = target;
    m_moveToTime = 0;
    m_moveToDuration = duration;
}

void C_HumanAnimMixer::AddMove2(const S_vector4 &move, int duration)
{
    m_addMove2 = move;
    m_addMove2Time = 0;
    m_addMove2Duration = duration;
}

void C_HumanAnimMixer::Slide(const S_vector4 &offset, int duration)
{
    m_slideStepTime = 0;
    m_slide = offset;
    m_slideTime = 0;
    m_slideDuration = duration;
    // The share of an 80 ms step: along the offset, its length over the steps.
    m_slideVelocity = offset;
    SetXYZ(m_slideVelocity, XYZ(offset).Normalized() *
                                (XYZ(offset).Magnitude() / (static_cast<float>(m_slideDuration) / SLIDE_STEP_TIME)));
}

void C_HumanAnimMixer::SetHoldTime(int length, float share)
{
    if (share < MIN_HOLD_SHARE)
        share = MIN_HOLD_SHARE;
    m_holdTime = static_cast<int>(static_cast<float>(length) * share);
}

// ------------------------------------------------------------------ the queues

float C_HumanAnimMixer::QueueAnim(S_HumanAnimRef *anim)
{
    const int current = m_tracks[0].current;
    // What to play now (nothing unless the queue or the request says so).
    S_HumanAnimRef play = *anim;
    play.anim = HUMAN_ANIM_NONE;
    play.Normalize();
    S_MixerQueue<S_HumanAnimRef> &queue = m_animQueue;
    if (queue.read != queue.write) {
        if (current != -1 && IsBlending(m_stages[current])) {
            // The blend goes on. Back to the animation blending out, or from one standing
            // animation to another, with a moving one involved: played at once.
            if (m_previous.anim.anim != HUMAN_ANIM_NONE &&
                (m_previous.anim.IsSame(*anim) || (IsStandAnim(m_previous.anim.anim) && IsStandAnim(anim->anim))) &&
                (IsMovingAnim(anim->anim) || IsMovingAnim(m_current.anim.anim)))
                play.Set(anim->anim, anim->stance, anim->sub);
        } else {
            const S_HumanAnimRef &next = Pop(queue);
            play.Set(next.anim, next.stance, next.sub);
            if (play.IsSame(*anim)) {
                anim->anim = HUMAN_ANIM_NONE;
                anim->Normalize();
            }
        }
    }
    if (anim->anim != HUMAN_ANIM_NONE && !anim->IsSame(m_current.anim)) {
        if (queue.read != queue.write || play.anim != HUMAN_ANIM_NONE) {
            PushAnim(queue, *anim);
        } else if (current != -1 && IsBlending(m_stages[current]) &&
                   (m_owner->IsLocalPlayer() || m_owner->IsServerSimulated() || IsLocalGame()) &&
                   !(m_previous.anim.anim != HUMAN_ANIM_NONE && m_previous.anim.IsSame(*anim) &&
                     (IsMovingAnim(anim->anim) || IsMovingAnim(m_current.anim.anim))) &&
                   anim->anim != 0x0E && (anim->anim < 0x1A || anim->anim > 0x21)) {
            // The blend goes on: the request waits (ids 0x0E and 0x1A..0x21 never do).
            PushAnim(queue, *anim);
        } else {
            play.Set(anim->anim, anim->stance, anim->sub);
        }
    }
    anim->Set(play.anim, play.stance, play.sub);
    return 1.0f;
}

float C_HumanAnimMixer::QueueCarry(int *carry)
{
    const int current = m_tracks[1].current;
    S_MixerCarry play = {CARRY_ANIM_NONE, carry[1], carry[2]};
    S_MixerQueue<S_MixerCarry> &queue = m_carryQueue;
    const bool blending = current != -1 && IsBlending(m_stages[2 + current]);
    if (queue.read != queue.write) {
        if (!blending) {
            const S_MixerCarry &next = Pop(queue);
            play.anim = next.anim;
            play.variant = next.variant;
            if (play.anim == carry[0] && play.variant == carry[1]) {
                carry[0] = play.anim;
                carry[1] = play.variant;
                return 1.0f;
            }
        } else if (m_previous.carryAnim != CARRY_ANIM_NONE && m_previous.carryAnim == carry[0] &&
                   m_previous.carryVariant == carry[1]) {
            // Back to the one blending out.
            play.anim = carry[0];
            play.variant = carry[1];
        }
    }
    if (carry[0] != CARRY_ANIM_NONE && !(carry[0] == m_current.carryAnim && carry[1] == m_current.carryVariant)) {
        if (queue.read != queue.write || play.anim != CARRY_ANIM_NONE) {
            PushCarryEntry(queue, carry[0], carry[1]);
        } else if (!blending ||
                   (m_previous.carryAnim != CARRY_ANIM_NONE && m_previous.carryAnim == carry[0] &&
                    m_previous.carryVariant == carry[1]) ||
                   (m_stages[2 + current].flags & MIXER_STAGE_FADE_OUT)) {
            play.anim = carry[0];
            play.variant = carry[1];
        } else {
            // The blend goes on: the request waits, and the old stage's blend is stretched by
            // what has passed of it.
            PushCarryEntry(queue, carry[0], carry[1]);
            S_MixerStage &out = m_stages[2 + (current == 0 ? 1 : 0)];
            out.blendTime += out.blendElapsed;
        }
    }
    carry[0] = play.anim;
    carry[1] = play.variant;
    return 1.0f;
}

void C_HumanAnimMixer::PushCarry(const int *carry)
{
    PushCarryEntry(m_carryQueue, carry[0], carry[1]);
}

float C_HumanAnimMixer::QueueActivity(int * /*activity*/)
{
    return 1.0f;
}

void C_HumanAnimMixer::ClearQueue(int track)
{
    auto clear = [](auto &queue) {
        while (queue.read != queue.write)
            Pop(queue);
    };
    if (track == 0)
        clear(m_animQueue);
    else if (track == 1)
        clear(m_carryQueue);
    else if (track == 2)
        clear(m_activityQueue);
}

void C_HumanAnimMixer::LockQueue(int track)
{
    if (track == 0)
        m_animQueue.locked = true;
    else if (track == 1)
        m_carryQueue.locked = true;
    else if (track == 2)
        m_activityQueue.locked = true;
}

// ------------------------------------------------------------------ the tracks

int C_HumanAnimMixer::GetTrackTime(int track)
{
    const int current = m_tracks[track].current;
    if (current == -1)
        return MOVE_NO_TIME;
    return m_model->GetAnimTime(2 * track + current);
}

bool C_HumanAnimMixer::HasTrackPlayed(int track, int ms)
{
    if (m_tracks[track].current == -1)
        return true;
    return static_cast<float>(GetTrackTime(track)) >= static_cast<float>(ms) * MOVE_TIME_SCALE;
}

float C_HumanAnimMixer::GetTrackProgress(int track)
{
    const S_MixerTrack &trk = m_tracks[track];
    if (trk.current == -1)
        return -1.0f;
    return static_cast<float>(m_model->GetAnimTime(2 * track + trk.current)) / static_cast<float>(trk.length);
}

float C_HumanAnimMixer::GetBlendProgress(int track)
{
    // (Clears the progress of track 0 as a side effect.)
    m_blendProgress = 0.0f;
    const int current = m_tracks[track].current;
    if (current == -1)
        return 0.0f;
    const S_MixerStage &stage = m_stages[2 * track + current];
    if (stage.blendElapsed < 0.0f || !(stage.blendElapsed < stage.blendTime))
        return 1.0f;
    return MixerBlendProgress(stage.blendElapsed, 0.0f, stage.blendTime);
}

void C_HumanAnimMixer::FadeOutTrack(int track, int time)
{
    const int current = m_tracks[track].current;
    if (current == -1 || (m_stages[2 * track + current].flags & MIXER_STAGE_FADE_OUT))
        return;
    for (int i = 0; i < 2; ++i) {
        S_MixerStage &stage = m_stages[2 * track + i];
        stage.blendElapsed = 0.0f;
        stage.blendTime = static_cast<float>(time);
        stage.flags |= MIXER_STAGE_FADE_OUT;
        stage.rate = 1.0f;
        stage.speed = 1.0f;
    }
}

void C_HumanAnimMixer::SetTrackRate(int track, float rate)
{
    const int current = m_tracks[track].current;
    if (current != -1)
        m_stages[2 * track + current].rate = rate;
}

void C_HumanAnimMixer::HoldTrack(int track, int time)
{
    S_MixerTrack &trk = m_tracks[track];
    if (trk.reversed)
        return;
    // Fix: a track without an animation has no stage to hold (the original writes the rate
    // of the stage before the pair).
    if (trk.current != -1)
        m_stages[2 * track + trk.current].rate = 0.0f;
    trk.hold = time;
}

void C_HumanAnimMixer::ReleaseTrack(int track)
{
    S_MixerTrack &trk = m_tracks[track];
    if (trk.hold == 0)
        return;
    trk.hold = -1;
    // Fix: as in HoldTrack.
    if (trk.current != -1)
        m_stages[2 * track + trk.current].rate = 1.0f;
}

void C_HumanAnimMixer::HoldTrackAt(int track, float share)
{
    S_MixerTrack &trk = m_tracks[track];
    if (trk.current == -1)
        return;
    const int stage = 2 * track + trk.current;
    const int time = static_cast<int>(static_cast<float>(trk.length) * share);
    m_model->SetAnimTime(stage, time);
    m_stages[stage].rate = 0.0f;
    trk.time = time;
    trk.hold = time;
    trk.reversed = false;
}

void C_HumanAnimMixer::MirrorTrackTime(int track)
{
    S_MixerTrack &trk = m_tracks[track];
    if (trk.current == -1)
        return;
    const int stage = 2 * track + trk.current;
    const int time = m_model->GetAnimTime(stage);
    if (time < trk.length / 2) {
        m_model->SetAnimTime(stage, trk.length - time);
        trk.reversed = true;
        trk.time = trk.length - time;
    }
}

const int *C_HumanAnimMixer::GetEvents(int track, int *count)
{
    *count = m_tracks[track].eventCount;
    return m_tracks[track].events;
}

void C_HumanAnimMixer::SetTrackTag(int track, int tag)
{
    const int current = m_tracks[track].current;
    if (current == -1)
        return;
    S_MixerStage &stage = m_stages[2 * track + current];
    stage.tag = tag;
    stage.finished = false;
}

bool C_HumanAnimMixer::CheckTrackFinished(int track, int tag)
{
    for (int i = 0; i < 2; ++i) {
        S_MixerStage &stage = m_stages[2 * track + i];
        if (stage.finished && stage.tag == tag && stage.tag >= 0) {
            stage.finished = false;
            stage.tag = -1;
            return true;
        }
    }
    return false;
}

int C_HumanAnimMixer::GetTrackTag(int track)
{
    const int current = m_tracks[track].current;
    return current != -1 ? m_stages[2 * track + current].tag : -1;
}

void C_HumanAnimMixer::PlayTrack3(I3D_animation_set *anim, int blendTime, int fadeOut, bool loop)
{
    S_MixerTrack &trk = m_tracks[3];
    trk.hold = 0;
    trk.time = 0;
    const int mode = loop ? I3DANIM_LOOP : I3DANIM_PLAY_ONCE;
    if (trk.current != 1) {
        // On stage 7, blending in over stage 6 (cross-fading when the track was free).
        const bool wasFree = trk.current == -1;
        trk.current = 1;
        trk.length = anim->GetEndTime();
        trk.reversed = false;
        m_model->SetAnimation(anim, 7, 0.0f, mode);
        anim->SetPlayMode(mode);
        S_MixerStage &stage = m_stages[7];
        stage.crossfade = wasFree;
        stage.blendElapsed = 0.0f;
        stage.weight = 1.0f;
        stage.flags = 0;
        stage.blendTime = static_cast<float>(blendTime);
        stage.rate = 1.0f;
        stage.speed = 1.0f;
    } else {
        // On stage 6, at once; stage 7 fades out above it.
        trk.current = 0;
        trk.length = anim->GetEndTime();
        trk.reversed = false;
        m_model->SetAnimation(anim, 6, 1.0f, mode);
        anim->SetPlayMode(mode);
        S_MixerStage &stage = m_stages[6];
        stage.blendElapsed = 0.0f;
        stage.blendTime = static_cast<float>(blendTime);
        stage.weight = 1.0f;
        stage.crossfade = false;
        stage.flags = 0;
        stage.rate = 1.0f;
        stage.speed = 1.0f;
    }
    m_track3FadeOut = loop ? -1 : fadeOut;
}

void C_HumanAnimMixer::FadeOutTrack3(int time)
{
    const int current = m_tracks[3].current;
    if (current == -1)
        return;
    S_MixerStage &stage = m_stages[6 + current];
    if (stage.flags & MIXER_STAGE_FADE_OUT)
        return;
    stage.blendElapsed = 0.0f;
    stage.flags |= MIXER_STAGE_FADE_OUT;
    stage.blendTime = static_cast<float>(time);
    stage.rate = 1.0f;
    stage.speed = 1.0f;
    m_track3FadeOut = -1;
}

void C_HumanAnimMixer::NotifyFrames(int track, I3D_frame **frames, int count)
{
    const int parts = m_model->NumTracks();
    I3D_model_part **list = m_model->GetTracks();
    for (int p = 0; p < parts; ++p) {
        for (int i = 0; i < count; ++i) {
            if (list[p]->GetFrame() != frames[i])
                continue;
            // (frames[count], one past the last, takes the place of the one found.)
            frames[i] = frames[count];
            --count;
            list[p]->OnStage(m_tracks[track].current + 2 * track);
        }
    }
}

void C_HumanAnimMixer::AddTrackEvent(int track, int event)
{
    AddEvent(m_tracks[track], event);
}

// ------------------------------------------------------------------ the note keys

int __stdcall C_HumanAnimMixer::NoteHook(I3D_frame *frame, uint32_t message, uintptr_t param1, uintptr_t param2)
{
    const char *note = reinterpret_cast<const char *>(param1);
    const int time = static_cast<int>(param2);
    I3D_frame *model = frame->GetParent();
    if (model && model->GetData() && model->GetData()->actor) {
        // (Any actor of the model is taken for a human.)
        C_HumanMovement *movement = static_cast<C_Human *>(model->GetData()->actor)->GetMovement();
        // Fix: a human without its movement gets no events (the original calls through null).
        if (movement)
            movement->m_mixer.OnNote(message, note, time);
    }
    PlayNoteSound(frame, note);
    return 0;
}

void C_HumanAnimMixer::OnNote(uint32_t /*message*/, const char *note, int time)
{
    if (!note)
        return;
    S_MixerTrack &body = m_tracks[0];
    S_MixerTrack &carry = m_tracks[1];
    S_MixerTrack &activity = m_tracks[2];
    switch (note[0]) {
    case '0':
        AddEvent(carry, ANIM_EVENT_EQUIPMENT_1);
        break;
    case '1':
        AddEvent(carry, ANIM_EVENT_WEAPON_READY);
        break;
    case '^':
        AddEvent(carry, ANIM_EVENT_UNKNOWN3);
        break;
    case 'c':
        AddEvent(carry, ANIM_EVENT_PICK_UP);
        break;
    case 'd':
        AddEvent(carry, ANIM_EVENT_UNKNOWN21);
        break;
    case 'e':
        AddEvent(carry, note[1] == '1' ? ANIM_EVENT_PROJECTILE_HAND1 : ANIM_EVENT_PROJECTILE_HAND2);
        break;
    case 'f':
        AddEvent(carry, ANIM_EVENT_PROJECTILE_WEAPON);
        break;
    case 'h':
        if (note[1] == '2')
            AddEvent(activity, ANIM_EVENT_SPECIAL_ATTACH);
        else
            AddEvent(carry, ANIM_EVENT_DOOR);
        break;
    case 'k':
        // (Into the body track at the count of the carry track, as in the original.)
        AddEventAt(body, carry.eventCount, ANIM_EVENT_SOUND);
        break;
    case 'l':
        if (note[1] == 'l')
            AddEvent(carry, ANIM_EVENT_HIDE_HANDS_ITEM_B);
        else if (note[1] == 'r')
            AddEvent(carry, ANIM_EVENT_HIDE_HANDS_ITEM_C);
        else
            AddEvent(carry, ANIM_EVENT_HIDE_HANDS_ITEM);
        break;
    case 'm':
        AddEvent(activity, ANIM_EVENT_BREATH);
        break;
    case 'n':
        AddEvent(activity, ANIM_EVENT_LIGHT);
        break;
    case 'p': {
        // "p" / "p2": the body / activity animation holds here.
        const int track = note[1] == '2' ? 2 : 0;
        if (m_tracks[track].hold < 1) {
            HoldTrack(track, time);
            if (track == 0 && m_holdTime != MOVE_NO_TIME)
                m_holdTime = MOVE_NO_TIME;
        }
        break;
    }
    case 's': {
        // A step ("s1" / "s2" the left / other foot), unless one came too short a time ago or
        // this update already has one.
        if (!(static_cast<float>(m_stepSoundTime) >= STEP_EVENT_INTERVAL / MOVE_TIME_SCALE))
            break;
        m_stepSoundTime = 0;
        for (int i = 0; i < body.eventCount; ++i) {
            if (body.events[i] == ANIM_EVENT_STEP)
                return;
        }
        if (note[1] == '1')
            AddEvent(body, ANIM_EVENT_STEP_LEFT);
        else if (note[1] == '2')
            AddEvent(body, ANIM_EVENT_STEP_6);
        else
            AddEvent(body, ANIM_EVENT_STEP);
        break;
    }
    case 't':
        AddEvent(carry, ANIM_EVENT_ACTIVITY_END);
        break;
    case 'u':
        // (As 'k'.)
        AddEventAt(body, carry.eventCount, ANIM_EVENT_UNKNOWN23);
        break;
    case 'w':
        if (note[1] == '2')
            AddEvent(activity, ANIM_EVENT_SPECIAL_DETACH);
        else
            AddEvent(carry, ANIM_EVENT_WEAPON_DRAWN);
        break;
    case 'y':
        AddEvent(carry, ANIM_EVENT_UNKNOWN22);
        break;
    case 'z':
        AddEvent(body, ANIM_EVENT_LADDER_STEP);
        break;
    default:
        break;
    }
}

void AttachModelUpdateHook(I3D_frame *model)
{
    if (!model || model->GetType() != FRAME_MODEL)
        return;
    I3D_frame *frame = model->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY);
    if (frame && (!model->GetData() || !model->GetData()->actor) && !frame->GetCallback())
        frame->SetCallback(C_HumanAnimMixer::NoteHook, I3DCALLBACK_ANIM_NOTE);
}

void DetachModelUpdateHook(I3D_frame *model)
{
    if (!model || model->GetType() != FRAME_MODEL)
        return;
    I3D_frame *frame = model->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY);
    if (frame && (!model->GetData() || !model->GetData()->actor) && frame->GetCallback())
        frame->SetCallback(nullptr, 0);
}

// ------------------------------------------------------------------ save games

bool C_HumanAnimMixer::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_MIXER))
        return false;
    chunk.WriteChunk(CHUNK_MIXER_TRACKS, m_tracks, sizeof(m_tracks));
    chunk.WriteChunk(CHUNK_MIXER_STAGES, m_stages, sizeof(m_stages));
    chunk.WriteChunk(CHUNK_MIXER_PREVIOUS_ANIM, &m_previous.anim, sizeof(m_previous.anim));
    chunk.WriteChunk(CHUNK_MIXER_PREVIOUS_CARRY, &m_previous.carryAnim, 3 * sizeof(int));
    chunk.WriteChunk(CHUNK_MIXER_PREVIOUS_ACTIVITY, &m_previous.activity, 2 * sizeof(int));
    chunk.WriteChunk(CHUNK_MIXER_CURRENT_ANIM, &m_current.anim, sizeof(m_current.anim));
    chunk.WriteChunk(CHUNK_MIXER_CURRENT_CARRY, &m_current.carryAnim, 3 * sizeof(int));
    chunk.WriteChunk(CHUNK_MIXER_CURRENT_ACTIVITY, &m_current.activity, 2 * sizeof(int));
    chunk.WriteChunk(CHUNK_MIXER_BLEND_PROGRESS, m_blendProgress);
    chunk.WriteChunk(CHUNK_MIXER_VELOCITY_A, &m_velocityA, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_WEIGHT_A, m_weightA);
    chunk.WriteChunk(CHUNK_MIXER_VELOCITY_B, &m_velocityB, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_WEIGHT_B, m_weightB);
    chunk.WriteChunk(CHUNK_MIXER_MAX_SPEED, m_maxSpeed);
    chunk.WriteChunk(CHUNK_MIXER_MOVE, &m_move, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_ADD_MOVE, &m_addMove, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_ADD_MOVE_TIME, m_addMoveTime);
    chunk.WriteChunk(CHUNK_MIXER_ADD_MOVE_DURATION, m_addMoveDuration);
    // (Not its times.)
    chunk.WriteChunk(CHUNK_MIXER_EXTRA_MOVE, &m_extraMove, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_ADD_MOVE2, &m_addMove2, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_ADD_MOVE2_TIME, m_addMove2Time);
    chunk.WriteChunk(CHUNK_MIXER_ADD_MOVE2_DURATION, m_addMove2Duration);
    chunk.WriteChunk(CHUNK_MIXER_SLIDE, &m_slide, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_SLIDE_VELOCITY, &m_slideVelocity, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_SLIDE_STEP_TIME, m_slideStepTime);
    chunk.WriteChunk(CHUNK_MIXER_SLIDE_TIME, m_slideTime);
    chunk.WriteChunk(CHUNK_MIXER_SLIDE_DURATION, m_slideDuration);
    chunk.WriteChunk(CHUNK_MIXER_ANIM_QUEUE, &m_animQueue, sizeof(m_animQueue));
    chunk.WriteChunk(CHUNK_MIXER_CARRY_QUEUE, &m_carryQueue, sizeof(m_carryQueue));
    chunk.WriteChunk(CHUNK_MIXER_ACTIVITY_QUEUE, &m_activityQueue, sizeof(m_activityQueue));
    chunk.WriteChunk(CHUNK_MIXER_MOVE_TO_SPEED, m_moveToSpeed);
    chunk.WriteChunk(CHUNK_MIXER_MOVE_TO_TARGET, &m_moveToTarget, sizeof(S_vector));
    chunk.WriteChunk(CHUNK_MIXER_MOVE_TO_TIME, m_moveToTime);
    chunk.WriteChunk(CHUNK_MIXER_MOVE_TO_DURATION, m_moveToDuration);
    chunk.WriteChunk(CHUNK_MIXER_SPEED_SCALE, m_speedScale);
    chunk.WriteChunk(CHUNK_MIXER_TRACK3_FADE_OUT, m_track3FadeOut);
    chunk.WriteChunk(CHUNK_MIXER_HOLD_TIME, m_holdTime);
    for (int stage = 0; stage < MIXER_STAGES; ++stage) {
        I3D_animation_set *anim = m_model->GetAnimation(stage);
        const char *name = "";
        int mode = 0;
        if (anim && m_model->IsAnimActive(stage)) {
            name = anim->GetFileName() ? anim->GetFileName() : "";
            if (const char *slash = std::strrchr(name, '\\'))
                name = slash + 1;
            mode = anim->GetPlayMode();
        }
        const uint16_t id = static_cast<uint16_t>(stage * CHUNK_MIXER_STAGE_STRIDE);
        chunk.WriteStringChunk(CHUNK_MIXER_STAGE_ANIM + id, name);
        chunk.WriteChunk(CHUNK_MIXER_STAGE_MODE + id, mode);
        chunk.WriteChunk(CHUNK_MIXER_STAGE_WEIGHT + id, m_model->GetAnimWeight(stage));
        chunk.WriteChunk(CHUNK_MIXER_STAGE_TIME + id, m_model->GetAnimTime(stage));
    }
    chunk.Ascend();
    return true;
}

bool C_HumanAnimMixer::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_MIXER) {
        chunk.Ascend();
        return false;
    }
    for (int stage = 0; stage < MIXER_STAGES; ++stage) {
        m_model->SetAnimation(nullptr, stage, 1.0f, 0);
        m_model->SetAnimWeight(stage, 0.0f);
    }
    auto readVector = [&chunk](S_vector4 &v) {
        S_vector value(0.0f, 0.0f, 0.0f);
        chunk.Read(value);
        SetXYZ(v, value);
    };
    int *const ints[] = {
        &m_addMoveTime, &m_addMoveDuration, &m_addMove2Time, &m_addMove2Duration, &m_slideStepTime,
        &m_slideTime, &m_slideDuration, &m_moveToTime, &m_moveToDuration, &m_track3FadeOut, &m_holdTime,
    };
    static const uint16_t INT_CHUNKS[] = {
        CHUNK_MIXER_ADD_MOVE_TIME, CHUNK_MIXER_ADD_MOVE_DURATION, CHUNK_MIXER_ADD_MOVE2_TIME,
        CHUNK_MIXER_ADD_MOVE2_DURATION, CHUNK_MIXER_SLIDE_STEP_TIME, CHUNK_MIXER_SLIDE_TIME,
        CHUNK_MIXER_SLIDE_DURATION, CHUNK_MIXER_MOVE_TO_TIME, CHUNK_MIXER_MOVE_TO_DURATION,
        CHUNK_MIXER_TRACK3_FADE_OUT, CHUNK_MIXER_HOLD_TIME,
    };
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_MIXER_TRACKS:
            chunk.Read(m_tracks, sizeof(m_tracks));
            break;
        case CHUNK_MIXER_STAGES:
            chunk.Read(m_stages, sizeof(m_stages));
            break;
        case CHUNK_MIXER_PREVIOUS_ANIM:
            chunk.Read(&m_previous.anim, sizeof(m_previous.anim));
            break;
        case CHUNK_MIXER_PREVIOUS_CARRY:
            chunk.Read(&m_previous.carryAnim, 3 * sizeof(int));
            break;
        case CHUNK_MIXER_PREVIOUS_ACTIVITY:
            chunk.Read(&m_previous.activity, 2 * sizeof(int));
            break;
        case CHUNK_MIXER_CURRENT_ANIM:
            chunk.Read(&m_current.anim, sizeof(m_current.anim));
            break;
        case CHUNK_MIXER_CURRENT_CARRY:
            chunk.Read(&m_current.carryAnim, 3 * sizeof(int));
            break;
        case CHUNK_MIXER_CURRENT_ACTIVITY:
            chunk.Read(&m_current.activity, 2 * sizeof(int));
            break;
        case CHUNK_MIXER_BLEND_PROGRESS:
            chunk.Read(m_blendProgress);
            break;
        case CHUNK_MIXER_VELOCITY_A:
            readVector(m_velocityA);
            break;
        case CHUNK_MIXER_WEIGHT_A:
            chunk.Read(m_weightA);
            break;
        case CHUNK_MIXER_VELOCITY_B:
            readVector(m_velocityB);
            break;
        case CHUNK_MIXER_WEIGHT_B:
            chunk.Read(m_weightB);
            break;
        case CHUNK_MIXER_MAX_SPEED:
            chunk.Read(m_maxSpeed);
            break;
        case CHUNK_MIXER_MOVE:
            readVector(m_move);
            break;
        case CHUNK_MIXER_ADD_MOVE:
            readVector(m_addMove);
            break;
        case CHUNK_MIXER_EXTRA_MOVE:
            readVector(m_extraMove);
            break;
        case CHUNK_MIXER_ADD_MOVE2:
            readVector(m_addMove2);
            break;
        case CHUNK_MIXER_SLIDE:
            readVector(m_slide);
            break;
        case CHUNK_MIXER_SLIDE_VELOCITY:
            readVector(m_slideVelocity);
            break;
        case CHUNK_MIXER_ANIM_QUEUE:
            chunk.Read(&m_animQueue, sizeof(m_animQueue));
            break;
        case CHUNK_MIXER_CARRY_QUEUE:
            chunk.Read(&m_carryQueue, sizeof(m_carryQueue));
            break;
        case CHUNK_MIXER_ACTIVITY_QUEUE:
            chunk.Read(&m_activityQueue, sizeof(m_activityQueue));
            break;
        case CHUNK_MIXER_MOVE_TO_SPEED:
            chunk.Read(m_moveToSpeed);
            break;
        case CHUNK_MIXER_MOVE_TO_TARGET:
            readVector(m_moveToTarget);
            break;
        case CHUNK_MIXER_SPEED_SCALE:
            chunk.Read(m_speedScale);
            break;
        default: {
            bool known = false;
            for (int i = 0; i < static_cast<int>(sizeof(INT_CHUNKS) / sizeof(INT_CHUNKS[0])); ++i) {
                if (id == INT_CHUNKS[i]) {
                    chunk.Read(*ints[i]);
                    known = true;
                }
            }
            if (known || id < CHUNK_MIXER_STAGE_ANIM || id > CHUNK_MIXER_STAGE_LAST)
                break;
            const int stage = (id - CHUNK_MIXER_STAGE_ANIM) / CHUNK_MIXER_STAGE_STRIDE;
            const int kind = (id - CHUNK_MIXER_STAGE_ANIM) % CHUNK_MIXER_STAGE_STRIDE;
            // Fix: there is no stage 8 (chunk 0x80; the original passes it to the model).
            if (stage >= MIXER_STAGES)
                break;
            if (kind == CHUNK_MIXER_STAGE_ANIM - CHUNK_MIXER_STAGE_ANIM) {
                // The file name, then its play mode from the chunk after it.
                char name[1000];
                chunk.ReadString(name, sizeof(name));
                chunk.Ascend();
                int mode = 0;
                const bool inMode = chunk.Descend(id);
                if (inMode)
                    chunk.Read(mode);
                if (!name[0]) {
                    m_model->SetAnimation(nullptr, stage, 1.0f, 0);
                } else {
                    I3D_animation_set *anim = nullptr;
                    g_animationCache.Get(name, &anim);
                    m_model->SetAnimation(anim, stage, 1.0f, mode);
                    // Fix: a file no longer found leaves the stage empty (the original calls
                    // through null).
                    if (anim) {
                        anim->SetPlayMode(mode);
                        anim->Release();
                    }
                }
                if (!inMode)
                    continue;               // (no chunk to leave)
            } else if (kind == CHUNK_MIXER_STAGE_WEIGHT - CHUNK_MIXER_STAGE_ANIM) {
                float weight = 0.0f;
                chunk.Read(weight);
                m_model->SetAnimWeight(stage, weight);
            } else if (kind == CHUNK_MIXER_STAGE_TIME - CHUNK_MIXER_STAGE_ANIM) {
                int time = 0;
                chunk.Read(time);
                if (m_model->GetAnimation(stage)) {
                    int times[MIXER_STAGES] = {};
                    times[stage] = time;
                    m_model->AdvanceStages(times);
                }
            }
            // (The original leaves the chunks of the other kinds entered.)
            break;
        }
        }
        chunk.Ascend();
    }
    for (int track = 0; track < 3; ++track)
        m_tracks[track].eventCount = 0;
    chunk.Ascend();
    return true;
}
