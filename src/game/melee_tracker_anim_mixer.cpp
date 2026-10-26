// The animation mixer of a meleeTracker (C_MeleeTrackerAnimMixer, C_MeleeTrackerMovement +0x10; original source
// 0x005c6d10..0x005c7b00 and 0x005cfd20..0x005d0950): two channels cross-fade their animations
// in and out over the model's first four stages and advance them at a rate the body sets; the
// root-motion velocity, add-moves and slides the body asks for give the move of the body; the
// note keys of the animations come back as events of the tracks.
//
// It is the slimmer cousin of C_HumanAnimMixer (game/human_anim_mixer.cpp) and follows the same
// shape (stages, blends, add-move/slide, chunked save/load).
#include "game/melee_tracker_movement.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/melee_tracker.h"
#include "game/resource_cache.h"
#include "ls3d/i3d.h"

// How far a blend of `duration` ms is after `elapsed` ms (shared with the human mixer; also
// called at 0x005c6ee0 / 0x005c7b00).
float __stdcall MixerBlendProgress(float elapsed, float step, float duration);   // 0x00772900

namespace {

// The note frame of the meleeTracker's model: its callback gets the note keys of the animations.
const char NOTE_FRAME_NAME[] = "a";                     // 0x00839100

// The mixer of the model's actor (the original reaches it as actor + 0xA0: the body at +0x90,
// its mixer at +0x10).
C_MeleeTrackerAnimMixer *MixerOfActor(C_Actor *actor)
{
    return &static_cast<C_MeleeTracker *>(actor)->GetMovement().GetMixer();
}

}  // namespace

// ------------------------------------------------------------------ setting up

void C_MeleeTrackerAnimMixer::ResetTracks()
{
    for (S_MeleeTrackerMixerTrack &track : m_tracks) {
        track.current = -1;
        track.time = -1;
        track.hold = 0;
        track.events[0] = track.events[1] = track.events[2] = 0;
        track.eventCount = 0;
        track.length = 0;
        track.reversed = 0;
    }
    for (S_MeleeTrackerMixerStage &stage : m_stages) {
        stage.weight = 0.0f;
        stage.blendTime = 0.0f;
        stage.blendElapsed = MELEE_TRACKER_MIXER_NO_BLEND;
        stage.crossfade = 1;
        stage.flags = 0;
        stage.rate = 1.0f;
        stage.finished = 0;
        stage.tag = -1;
    }
    if (I3D_frame *note = m_model->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY))
        note->SetCallback(NoteHook, I3DCALLBACK_ANIM_NOTE);
    m_move.x = m_move.y = m_move.z = 0.0f;
    // Quirk kept: m_move.w is left as it was (the original stores an uninitialised stack slot).
    m_blendScratch = 0.0f;
    m_speedPrevious = 0.0f;
    m_speedCurrent = 0.0f;
    m_stageFinished = 0;
    m_addMoveDuration = MELEE_TRACKER_MOVE_NO_TIME;
    m_slideDuration = MELEE_TRACKER_MOVE_NO_TIME;
}

void C_MeleeTrackerAnimMixer::Init(I3D_model *model)
{
    m_model = model;
    ResetTracks();
}

void C_MeleeTrackerAnimMixer::Reset()
{
    if (I3D_frame *note = m_model->FindChildFrame(NOTE_FRAME_NAME, FRAME_MASK_ANY))
        note->SetCallback(nullptr, 0);
    ResetTracks();
}

// ------------------------------------------------------------------ the stages

void C_MeleeTrackerAnimMixer::UpdateStages(int /*unused*/, int timeDelta, bool start, const int *trackActive)
{
    m_blendScratch = 0.0f;
    if (timeDelta == -1) {
        if (!start)
            return;
    } else if (!start) {
        // Advance the model's stages: the four the mixer blends at their own rate, the rest at
        // the plain movement time.
        const int raw = static_cast<int>(static_cast<float>(timeDelta));
        int times[8];
        for (int i = 0; i < 8; ++i)
            times[i] = i < MELEE_TRACKER_MIXER_STAGES
                           ? static_cast<int>(static_cast<float>(timeDelta) * m_stages[i].rate)
                           : raw;
        m_model->AdvanceStages(times);
    }

    // `trackActive` (the requested animations, when `start`) gates each track on a non-null anim.
    const int *active = trackActive;
    for (int track = 0; track < MELEE_TRACKER_MIXER_TRACKS; ++track) {
        S_MeleeTrackerMixerTrack &trk = m_tracks[track];
        const bool gated = start && trackActive && active && *active == 0;
        if (!gated && trk.current != -1) {
            const int cur = trk.current + 2 * track;
            const int other = (trk.current == 0 ? 1 : 0) + 2 * track;
            if (trk.hold < 1) {
                trk.time += timeDelta;
                if (trk.time > 0x7FFFD8EF)
                    trk.time = 100000;
            }
            S_MeleeTrackerMixerStage &stage = m_stages[cur];
            S_MeleeTrackerMixerStage &out = m_stages[other];
            if (stage.blendElapsed >= 0.0f) {
                const float elapsed = static_cast<float>(timeDelta) + stage.blendElapsed;
                stage.blendElapsed = elapsed;
                if (elapsed < stage.blendTime) {
                    float progress = MixerBlendProgress(elapsed, static_cast<float>(timeDelta), stage.blendTime);
                    if (!(stage.flags & MELEE_TRACKER_MIXER_STAGE_FADE_OUT)) {
                        if (stage.blendTime > 0.0f) {
                            if (stage.crossfade != 0) {
                                m_model->SetAnimWeight(other, (1.0f - progress) * out.weight);
                                m_model->SetAnimWeight(cur, progress * stage.weight);
                            } else if (cur < other) {
                                // The old animation is on the stage above: it fades out.
                                m_model->SetAnimWeight(other, (1.0f - progress) * out.weight);
                            } else {
                                // The new one is on the stage above: it fades in.
                                m_model->SetAnimWeight(cur, progress * stage.weight);
                            }
                        }
                    } else {
                        // Both stages of the track fade out.
                        const float weight = (1.0f - progress) * stage.weight;
                        m_model->SetAnimWeight(cur, weight);
                        if (weight <= m_model->GetAnimWeight(other))
                            m_model->SetAnimWeight(other, weight);
                    }
                } else {
                    // The blend is done.
                    if (!(stage.flags & MELEE_TRACKER_MIXER_STAGE_FADE_OUT))
                        m_model->SetAnimWeight(cur, stage.weight);
                    else
                        trk.current = -1;
                    m_model->StopAnimation(other);
                    m_model->SetAnimation(nullptr, other, 1.0f, 0);
                    m_stageFinished = 1;
                    out.blendElapsed = MELEE_TRACKER_MIXER_NO_BLEND;
                    out.flags = 0;
                    out.rate = 1.0f;
                    out.finished = 1;
                    stage.blendElapsed = MELEE_TRACKER_MIXER_NO_BLEND;
                    if (stage.flags & MELEE_TRACKER_MIXER_STAGE_FADE_OUT) {
                        m_model->StopAnimation(cur);
                        stage.flags = 0;
                        stage.rate = 1.0f;
                        stage.finished = 1;
                    }
                }
            }
        }
        if (active)
            active += sizeof(S_MeleeTrackerMixerAnim) / sizeof(int);
    }
}

void C_MeleeTrackerAnimMixer::PlayStages(const S_MeleeTrackerMixerAnim *anims)
{
    for (int track = 0; track < MELEE_TRACKER_MIXER_TRACKS; ++track) {
        const S_MeleeTrackerMixerAnim &next = anims[track];
        if (!next.anim)
            continue;
        S_MeleeTrackerMixerTrack &trk = m_tracks[track];
        const int was = trk.current;
        trk.hold = 0;
        trk.time = 0;
        if (was == -1 || was == 0) {
            // Free, or the lower stage is playing: start on the upper stage of the pair.
            trk.current = 1;
            trk.length = next.anim->GetEndTime();
            trk.reversed = 0;
            const int index = 2 * track + 1;
            m_model->SetAnimation(next.anim, index, 0.0f, 0);
            S_MeleeTrackerMixerStage &stage = m_stages[index];
            stage.weight = next.weight;
            stage.blendTime = next.blendTime;
            stage.crossfade = next.crossfade;
            stage.blendElapsed = 0.0f;
            stage.flags = 0;
            stage.rate = 1.0f;
        } else {
            // The upper stage is playing: start on the lower stage.
            trk.current = 0;
            trk.length = next.anim->GetEndTime();
            trk.reversed = 0;
            const int index = 2 * track;
            const float weight = next.crossfade != 0 ? 0.0f : next.weight;
            m_model->SetAnimation(next.anim, index, weight, 0);
            S_MeleeTrackerMixerStage &stage = m_stages[index];
            stage.weight = next.weight;
            stage.blendTime = next.blendTime;
            stage.crossfade = next.crossfade;
            stage.blendElapsed = 0.0f;
            stage.flags = 0;
            stage.rate = 1.0f;
        }
        m_speedPrevious = m_speedCurrent;
        m_speedCurrent = next.speed;
    }
}

// ------------------------------------------------------------------ the move of the body

void C_MeleeTrackerAnimMixer::UpdateMove(int timeDelta, const float *velocity)
{
    const float seconds = static_cast<float>(timeDelta) * 0.001f;
    // The root-motion speed blends from the old animation's to the new one's, scaled by the
    // current stage's rate.
    float speed = m_speedCurrent;
    if (m_tracks[0].current != -1)
        speed *= m_stages[m_tracks[0].current].rate;
    const float progress = GetBlendProgress(0);
    speed = speed * progress + (1.0f - progress) * m_speedPrevious;
    m_move.x = speed * velocity[0] * seconds;
    m_move.y = speed * velocity[1] * seconds;
    m_move.z = speed * velocity[2] * seconds;
    // Quirk kept: m_move.w is left as scratch (the original stores an uninitialised stack slot).

    if (m_addMoveDuration != MELEE_TRACKER_MOVE_NO_TIME) {
        const int duration = m_addMoveDuration;
        int part = timeDelta;
        m_addMoveTime += timeDelta;
        if (duration < m_addMoveTime) {
            m_addMoveDuration = MELEE_TRACKER_MOVE_NO_TIME;
            part = duration - m_addMoveTime + timeDelta;
        }
        const float share = static_cast<float>(part) / static_cast<float>(duration);
        m_move.x += share * m_addMove.x;
        m_move.y += share * m_addMove.y;
        m_move.z += share * m_addMove.z;
    }

    if (m_slideDuration != MELEE_TRACKER_MOVE_NO_TIME) {
        int part = timeDelta;
        if (m_slideDuration <= m_slideTime + timeDelta) {
            part = m_slideDuration - m_slideTime;
            m_slideDuration = MELEE_TRACKER_MOVE_NO_TIME;
        }
        m_slideTime += part;
        // Each 80 ms step takes its share of the slide off what is left.
        int left = part;
        while (left > 0) {
            if (static_cast<float>(m_slideStepTime + left) <= 80.0f) {
                m_slideStepTime += left;
                break;
            }
            m_slide.x -= m_slideVelocity.x;
            m_slide.y -= m_slideVelocity.y;
            m_slide.z -= m_slideVelocity.z;
            left = static_cast<int>(static_cast<float>(left) - (80.0f - static_cast<float>(m_slideStepTime)));
            m_slideStepTime = 0;
        }
        const float share = static_cast<float>(part) * 0.001f;
        m_move.x += share * m_slide.x;
        m_move.y += share * m_slide.y;
        m_move.z += share * m_slide.z;
    }
}

void C_MeleeTrackerAnimMixer::SetAddMove(const float *move, int duration)
{
    m_addMove.x = move[0];
    m_addMove.y = move[1];
    m_addMove.z = move[2];
    m_addMove.w = move[3];
    m_addMoveTime = 0;
    m_addMoveDuration = duration;
}

void C_MeleeTrackerAnimMixer::SetSlide(const float *offset, int duration)
{
    m_slideStepTime = 0;
    m_slide.x = offset[0];
    m_slide.y = offset[1];
    m_slide.z = offset[2];
    m_slide.w = offset[3];
    m_slideTime = 0;
    m_slideDuration = duration;
    // The per-80 ms step: the offset's direction times its length over the number of steps.
    m_slideVelocity = m_slide;
    float lengthSq = m_slideVelocity.x * m_slideVelocity.x + m_slideVelocity.y * m_slideVelocity.y +
                     m_slideVelocity.z * m_slideVelocity.z;
    if (std::fabs(lengthSq - 1.0f) < 1e-08f) {
        // Already unit length.
    } else if (lengthSq >= 1e-08f) {
        const float inv = 1.0f / std::sqrt(lengthSq);
        m_slideVelocity.x *= inv;
        m_slideVelocity.y *= inv;
        m_slideVelocity.z *= inv;
    } else if (m_slideVelocity.x != 0.0f) {
        m_slideVelocity.x = m_slideVelocity.x >= 0.0f ? 1.0f : -1.0f;
    } else if (m_slideVelocity.z == 0.0f) {
        m_slideVelocity.y = m_slideVelocity.y >= 0.0f ? 1.0f : -1.0f;
    } else {
        m_slideVelocity.z = m_slideVelocity.z >= 0.0f ? 1.0f : -1.0f;
    }
    // Scale to the length moved per step.
    float magSq = offset[2] * offset[2] + offset[1] * offset[1] + offset[0] * offset[0];
    float mag;
    int32_t magSqBits;  // the SDK's test on the float's bits (below 1e-8, or negative)
    std::memcpy(&magSqBits, &magSq, sizeof magSqBits);
    if (magSqBits < 0x322bcc77)
        mag = 0.0f;
    else if (std::fabs(magSq - 1.0f) >= 1e-08f)
        mag = std::sqrt(magSq);
    else
        mag = 1.0f;
    const float perStep = mag / (static_cast<float>(duration) / 80.0f);
    m_slideVelocity.x *= perStep;
    m_slideVelocity.y *= perStep;
    m_slideVelocity.z *= perStep;
}

// ------------------------------------------------------------------ the tracks

int C_MeleeTrackerAnimMixer::GetActiveTrackTime(int track)
{
    const int current = m_tracks[track].current;
    if (current != -1 && m_model->IsAnimActive(current + track * 2))
        return m_tracks[track].time;
    return -1;
}

int C_MeleeTrackerAnimMixer::GetTrackLength(int track)
{
    if (m_tracks[track].current == -1)
        return 0;
    return m_tracks[track].length;
}

void C_MeleeTrackerAnimMixer::SetTrackRate(int track, float rate)
{
    const int current = m_tracks[track].current;
    if (current != -1)
        m_stages[current + track * 2].rate = rate;
}

int *C_MeleeTrackerAnimMixer::TakeSoundEvents(int track, int *count)
{
    *count = m_tracks[track].eventCount;
    m_tracks[track].eventCount = 0;
    return m_tracks[track].events;
}

float C_MeleeTrackerAnimMixer::GetBlendProgress(int track)
{
    m_blendScratch = 0.0f;
    const int current = m_tracks[track].current;
    if (current != -1) {
        const int stage = current + track * 2;
        if (m_stages[stage].blendElapsed < 0.0f)
            return 1.0f;
        if (m_stages[stage].blendTime <= m_stages[stage].blendElapsed) {
            m_blendScratch = 1.0f;
            return m_blendScratch;
        }
        const float progress = MixerBlendProgress(m_stages[stage].blendElapsed, 0.0f, m_stages[stage].blendTime);
        if (track == 0) {
            m_blendScratch = progress;
            return m_blendScratch;
        }
    }
    return m_blendScratch;
}

// ------------------------------------------------------------------ the note keys

int __stdcall C_MeleeTrackerAnimMixer::NoteHook(I3D_frame *frame, uint32_t message, uintptr_t param1,
                                       uintptr_t param2)
{
    const char *note = reinterpret_cast<const char *>(param1);
    I3D_frame *model = frame->GetParent();
    C_Actor *actor = model && model->GetData() ? model->GetData()->actor : nullptr;
    // Fix: a note frame whose model has no actor is skipped (the original calls through a near-null
    // pointer: it reaches the mixer as actor + 0xA0 even when the actor is null).
    if (actor)
        MixerOfActor(actor)->OnNote(message, note, param2);
    return 0;
}

void C_MeleeTrackerAnimMixer::OnNote(uint32_t /*message*/, const char *note, uintptr_t /*unused*/)
{
    if (!note)
        return;
    S_MeleeTrackerMixerTrack &body = m_tracks[0];
    S_MeleeTrackerMixerTrack &overlay = m_tracks[1];
    // Adds an event to a track at its count.
    // Fix: at most MELEE_TRACKER_MIXER_TRACK_EVENTS events an update (the original writes on into the
    // fields after the array when more notes come).
    auto addEvent = [](S_MeleeTrackerMixerTrack &track, int event) {
        if (track.eventCount < MELEE_TRACKER_MIXER_TRACK_EVENTS)
            track.events[track.eventCount] = event;
        if (track.eventCount < MELEE_TRACKER_MIXER_TRACK_EVENTS)
            ++track.eventCount;
    };
    switch (note[0]) {
    case 'a':
        if (note[1] == '1')
            addEvent(overlay, 1);
        else if (note[1] == '2')
            addEvent(overlay, 2);
        break;
    case 'm':
        addEvent(overlay, 3);
        break;
    case 's': {
        // A footstep sound (event 0): skipped if the body track already has one this update.
        for (int i = 0; i < body.eventCount; ++i)
            if (body.events[i] == 0)
                return;
        addEvent(body, 0);
        break;
    }
    default:
        break;
    }
}

// ------------------------------------------------------------------ save games

bool C_MeleeTrackerAnimMixer::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(0x49C1))
        return false;
    chunk.WriteChunk(0, &m_tracks, sizeof(m_tracks));
    chunk.WriteChunk(1, &m_stages, sizeof(m_stages));
    chunk.WriteChunk(2, m_blendScratch);
    chunk.WriteChunk(3, m_speedPrevious);
    chunk.WriteChunk(4, m_speedCurrent);
    chunk.WriteChunk(5, &m_move, 3 * sizeof(float));
    chunk.WriteChunk(6, &m_addMove, 3 * sizeof(float));
    chunk.WriteChunk(7, m_addMoveTime);
    chunk.WriteChunk(8, m_addMoveDuration);
    chunk.WriteChunk(9, &m_slide, 3 * sizeof(float));
    chunk.WriteChunk(10, &m_slideVelocity, 3 * sizeof(float));
    chunk.WriteChunk(0x0B, m_slideStepTime);
    chunk.WriteChunk(0x0C, m_slideTime);
    chunk.WriteChunk(0x0D, m_slideDuration);
    chunk.WriteChunk(0x0E, static_cast<int>(m_stageFinished));
    // The model's first six stages: the animation file, its weight and time.
    for (int stage = 0; stage < 6; ++stage) {
        I3D_animation_set *anim = m_model->GetAnimation(stage);
        const char *name = "";
        if (anim) {
            name = anim->GetFileName() ? anim->GetFileName() : "";
            if (const char *slash = std::strrchr(name, '\\'))
                name = slash + 1;
        }
        const uint16_t id = static_cast<uint16_t>(stage * 6);
        chunk.WriteStringChunk(id + 0x50, name);
        const float weight = m_model->GetAnimWeight(stage);
        chunk.WriteChunk(id + 0x51, weight);
        const int time = m_model->GetAnimTime(stage);
        chunk.WriteChunk(id + 0x52, time);
    }
    chunk.Ascend();
    return true;
}

bool C_MeleeTrackerAnimMixer::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != 0x49C1) {
        chunk.Ascend();
        return false;
    }
    for (int stage = 0; stage < 6; ++stage) {
        m_model->SetAnimation(nullptr, stage, 1.0f, 0);
        m_model->SetAnimWeight(stage, 0.0f);
    }
    auto readVector = [&chunk](S_vector4 &v) {
        S_vector value(0.0f, 0.0f, 0.0f);
        chunk.Read(value);
        v.x = value.x;
        v.y = value.y;
        v.z = value.z;
        // Quirk kept: w is left unchanged (the original reads 12 bytes and keeps a stack value).
    };
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case 0:
            chunk.Read(&m_tracks, sizeof(m_tracks));
            break;
        case 1:
            chunk.Read(&m_stages, sizeof(m_stages));
            break;
        case 2:
            chunk.Read(m_blendScratch);
            break;
        case 3:
            chunk.Read(m_speedPrevious);
            break;
        case 4:
            chunk.Read(m_speedCurrent);
            break;
        case 5:
            readVector(m_move);
            break;
        case 6:
            readVector(m_addMove);
            break;
        case 7:
            chunk.Read(m_addMoveTime);
            break;
        case 8:
            chunk.Read(m_addMoveDuration);
            break;
        case 9:
            readVector(m_slide);
            break;
        case 10:
            readVector(m_slideVelocity);
            break;
        case 0x0B:
            chunk.Read(m_slideStepTime);
            break;
        case 0x0C:
            chunk.Read(m_slideTime);
            break;
        case 0x0D:
            chunk.Read(m_slideDuration);
            break;
        case 0x0E: {
            int value = 0;
            chunk.Read(value);
            m_stageFinished = value != 0 ? 1 : 0;
            break;
        }
        default:
            if (id >= 0x50 && id <= 0x74) {
                const int stage = (id - 0x50) / 6;
                const int kind = (id - 0x50) % 6;
                if (kind == 0) {
                    char name[1000];
                    chunk.ReadString(name, sizeof(name));
                    if (!name[0]) {
                        m_model->SetAnimation(nullptr, stage, 1.0f, 0);
                    } else {
                        I3D_animation_set *anim = nullptr;
                        g_animationCache.Get(name, &anim);
                        m_model->SetAnimation(anim, stage, 1.0f, 0);
                        // Fix: a file no longer found leaves the stage empty (the original calls
                        // through null).
                        if (anim)
                            anim->Release();
                    }
                } else if (kind == 1) {
                    float weight = 0.0f;
                    chunk.Read(weight);
                    m_model->SetAnimWeight(stage, weight);
                } else if (kind == 2) {
                    int time = 0;
                    chunk.Read(time);
                    m_model->SetAnimTime(stage, time);
                }
            }
            break;
        }
        chunk.Ascend();
    }
    m_model->Tick(0);
    m_tracks[0].eventCount = 0;
    m_tracks[1].eventCount = 0;
    chunk.Ascend();
    return true;
}
