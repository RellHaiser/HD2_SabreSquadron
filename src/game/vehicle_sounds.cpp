// C_Vehicle: the sounds (original 0x00517370 LoadSounds, 0x00517cf0 AddSoundsToSectors,
// 0x005181c0 RemoveSoundsFromSectors, 0x0051bd50 OpenSound, 0x0051bd80 LoadSound, 0x0051c180
// ReleaseSounds, 0x00518710 SwitchEngine, 0x00518ab0 UpdateEngineSound, 0x0051a480 PlayGearSound,
// 0x0051a620 PlayEngineFail, 0x0051b7c0 StopEngineLoops, 0x0051b8f0 StopSounds, 0x00519a60
// UpdateRollingSound, 0x00519dc0 UpdateRimSound, 0x0051a130 UpdateTyreSqueal, 0x0051a580
// PlayHandbrakeSound, 0x0051a6c0 / 0x0051a7a0 SwitchLoopSoundA / B, 0x0051a880 PlayWheelBurst,
// 0x0051a950 PlayImpactSound, 0x0051b560 PlayBumpSound, 0x0051b660 PlayDoorSound;
// 0x00517340 is the file's static initialiser). Two groups of sound frames: m_sounds1 (40 slots: mostly ambient,
// 4 / 50 m) and m_sounds2 (41: positional, the ranges from the block), named by the parameter
// block's two sound sets; m_unknown2298 picks the group that plays.
#include "game/vehicle.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "game/pending.h"                               // g_carScene, g_carSurfaces
#include "game/resource_cache.h"                        // g_soundCache
#include "ls3d/i3d.h"

std::vector<I3D_sector *> C_Vehicle::s_soundSectors;   // 0x0089c1c0
float C_Vehicle::s_soundVolume = 1.0f;                  // 0x0083bed4 g_vehicleSoundVolume

namespace {

constexpr int SOUNDS1 = 40, SOUNDS2 = 41;
constexpr int SOUNDS1_CRASH = 0x1D, SOUNDS2_CRASH = 0x1E;   // the two crash rows' first slots

// Every slot of a group in the original's order (LoadSounds, the sectors' loops and ReleaseSounds
// walk them alike): the slots before the crash sounds, the two crash rows pairwise from the last
// pair back, the rest.
template <class F>
void ForEachSlot(I3D_sound **sounds, int crash, int count, F f)
{
    for (int i = 0; i < crash; ++i)
        f(sounds[i]);
    for (int k = VEHICLE_CRASH_SOUNDS; k-- > 0;) {
        f(sounds[crash + k]);
        f(sounds[crash + VEHICLE_CRASH_SOUNDS + k]);
    }
    for (int i = crash + 2 * VEHICLE_CRASH_SOUNDS; i < count; ++i)
        f(sounds[i]);
}

template <class F>
void ForEachSound(I3D_sound **group1, I3D_sound **group2, F f)
{
    ForEachSlot(group1, SOUNDS1_CRASH, SOUNDS1, f);
    ForEachSlot(group2, SOUNDS2_CRASH, SOUNDS2, f);
}

// The engine loops (sounds1[3..17], sounds2[4..18]): set A on load, B off load, C in reverse.
constexpr int LAYERS1 = 3, LAYERS2 = 4;
constexpr int IDLE1 = 0x12, IDLE2 = 0x13;               // the idle loops
constexpr int ROLLING1 = 0x13, ROLLING2 = 0x14;         // the rolling sounds
constexpr int GEAR1 = 0x17, GEAR2 = 0x18;               // the two gear sounds' first slots
constexpr int SINGLE_LOOP2 = 3;                         // group 2's engine loop without the layers
constexpr int LOOP_A1 = 0x14, LOOP_A2 = 0x15;           // the switched loops (+0x2294, +0x2295)
constexpr int LOOP_B1 = 0x15, LOOP_B2 = 0x16;
constexpr int HANDBRAKE1 = 0x16, HANDBRAKE2 = 0x17;
constexpr int SQUEAL1 = 0x19, SQUEAL2 = 0x1A;
constexpr int BURST1 = 0x1C, BURST2 = 0x1D;             // a tyre's burst
constexpr int RIM1 = 0x27, RIM2 = 0x28;                 // a wheel rolling on its rim ("PNEU_PUNC.WAV")
constexpr int BUMP1 = 0x1B, BUMP2 = 0x1C;               // a wheel bottoming out
constexpr int DOOR_OPEN1 = 0x25, DOOR_OPEN2 = 0x26;
constexpr int DOOR_SHUT1 = 0x26, DOOR_SHUT2 = 0x27;

bool IsPlaying(I3D_sound *sound)
{
    return sound && sound->IsPlaying(0);
}

// A one-off sound (the starter, the stop, the failed start, a gear change) from its start, at the
// volume scale.
void StartSound(I3D_sound *sound, float volume)
{
    sound->SetOn(true);
    sound->SetVolume(volume);
}

// One engine layer `i` of a set's rows (pitch low / high, revs on / off, fade-in end / fade-out
// start, volume on load / off load): on between its revs, faded in and out over the ends, its
// pitch rising with the revs. `load` mixes the two volumes; `muted`: the layer's flag in neutral.
void UpdateLayer(I3D_sound *sound, const float (*rows)[VEHICLE_SOUND_LAYERS], int i, float revs, float load,
                 bool muted, float scale)
{
    if (!sound)
        return;
    const float low = rows[2][i], high = rows[3][i], fadeIn = rows[4][i], fadeOut = rows[5][i];
    if (revs < low || !(revs <= high) || muted) {
        if (sound->IsPlaying(0))
            sound->SetOn(false);
        return;
    }
    float volume = (rows[6][i] - rows[7][i]) * load + rows[7][i];
    if (volume > 0.01f) {
        if (revs < fadeIn && low != fadeIn)
            volume *= (revs - low) / (fadeIn - low);
        else if (revs > fadeOut && high != fadeOut)
            volume *= 1.0f - (revs - fadeOut) / (high - fadeOut);
    }
    const float pitch = (revs - low) / ((high - low) / (rows[1][i] - rows[0][i])) + rows[0][i];
    if (!sound->IsPlaying(0)) {
        if (!(volume > 0.01f))
            return;
        sound->SetOn(true);
    } else if (volume < 0.01f) {
        sound->SetOn(false);
        return;
    }
    sound->SetFrequency(pitch);
    sound->SetVolume(scale * volume);
}

// A sound by a speed (the rolling and rim sounds). `curve`: the pitch at the lowest and at the top
// speed, the lowest speed (silent under it), the top speed (the speed held to it), the speed of
// full volume (faded in up to it).
void UpdateSpeedSound(I3D_sound *sound, float speed, const float *curve, float scale)
{
    const float low = curve[2], top = curve[3], full = curve[4];
    if (speed > top)
        speed = top;
    if (!(speed >= low)) {
        if (sound->IsPlaying(0))
            sound->SetOn(false);
        return;
    }
    float volume = 1.0f;
    if (speed < full && full != low)
        volume = (speed - low) / (full - low);
    const float pitch = (speed - low) / ((top - low) / (curve[1] - curve[0])) + curve[0];
    if (!sound->IsPlaying(0)) {
        if (!(volume > 0.01f))
            return;
        sound->SetOn(true);
    } else if (volume < 0.01f) {
        sound->SetOn(false);
        return;
    }
    sound->SetFrequency(pitch);
    sound->SetVolume(scale * volume);
}

// A switched loop: on, the playing group's starts (unless it plays) and the other's stops; off,
// both stop.
void SwitchLoop(I3D_sound *sound1, I3D_sound *sound2, bool group1, bool on, float volume)
{
    if (!on) {
        if (sound1)
            sound1->SetOn(false);
        if (sound2)
            sound2->SetOn(false);
        return;
    }
    I3D_sound *sound = group1 ? sound1 : sound2, *other = group1 ? sound2 : sound1;
    if (sound && !sound->IsPlaying(0))
        StartSound(sound, volume);
    if (other)
        other->SetOn(false);
}

}  // namespace

bool C_Vehicle::LoadSounds(const S_VehicleParams &params)                      // 0x00517370
{
    // Group 1: 4 / 50 m (0.3, 0.2) for all.
    const auto one = [&](const char *name, int slot, uint32_t flags, bool loop, bool atEngine) {
        LoadSound(name, m_sounds1[slot], flags, 4.0f, 50.0f, 0.3f, 0.2f, loop, atEngine);
    };
    for (int i = 0; i < 3; ++i)
        one(params.sounds1[i], i, 3, false, true);
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i)
        one(params.layers1A[i], 3 + i, 0xB, true, true);
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i)
        one(params.layers1B[i], 8 + i, 0xB, true, true);
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i)
        one(params.layers1C[i], 13 + i, 0xB, true, true);
    one(params.sound1_9B4, 0x12, 3, true, true);
    if (m_maxWheelCircumference > 0.01f)
        one(params.sound1_9C4, 0x13, 0xB, true, false);
    one(params.sound1_9D4, 0x14, 3, true, false);
    one(params.sounds1_9E4[0], 0x15, 3, true, false);
    one(params.sounds1_9E4[1], 0x16, 3, false, false);
    one(params.sounds1_9E4[2], 0x17, 3, false, false);
    one(params.sounds1_9E4[3], 0x18, 3, false, false);
    one(params.sounds1_9E4[4], 0x19, 0xB, true, false);
    one(params.sounds1_9E4[5], 0x1A, 3, true, false);
    one(params.sounds1_9E4[6], 0x1B, 3, false, false);
    one(params.sounds1_9E4[7], 0x1C, 3, false, false);
    for (int k = VEHICLE_CRASH_SOUNDS; k-- > 0;) {
        one(params.crash1A[k], SOUNDS1_CRASH + k, 3, false, false);
        one(params.crash1B[k], SOUNDS1_CRASH + VEHICLE_CRASH_SOUNDS + k, 3, false, false);
    }
    one(params.sound1_AF4, 0x25, 0, false, false);
    one(params.sound1_B04, 0x26, 0, false, false);
    LoadSound("PNEU_PUNC.WAV", m_sounds1[0x27], 0xB, 6.0f, 30.0f, 0.3f, 0.2f, true, false);

    // Group 2: the engine's at +0xEE8's range, the rolling sound at +0xF10's, the rest at +0x95C's.
    const auto two = [&](const char *name, int slot, uint32_t flags, const float *range, bool loop,
                         bool atEngine) {
        LoadSound(name, m_sounds2[slot], flags, range[0], range[1], range[2], range[3], loop, atEngine);
    };
    const float other[4] = {m_unknown95C, m_unknown960, m_unknown964, m_unknown968};
    for (int i = 0; i < 3; ++i)
        two(params.sounds2[i], i, 0, m_unknownEE8, false, true);
    two(params.sounds2[3], 3, 8, m_unknownEE8, true, true);
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i)
        two(params.layers2A[i], 4 + i, 8, m_unknownEE8, true, true);
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i)
        two(params.layers2B[i], 9 + i, 8, m_unknownEE8, true, true);
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i)
        two(params.layers2C[i], 14 + i, 8, m_unknownEE8, true, true);
    two(params.sound2_E50, 0x13, 0, m_unknownEE8, true, true);
    if (m_maxWheelCircumference > 0.01f)
        two(params.sound2_E60, 0x14, 8, m_unknownF10, true, false);
    two(params.sound2_E70, 0x15, 0, other, true, false);
    LoadSound(params.sounds2_E80[0], m_sounds2[0x16], 0, 4.0f, 110.0f, 0.45f, 0.37f, true, false);
    two(params.sounds2_E80[1], 0x17, 0, other, false, false);
    two(params.sounds2_E80[2], 0x18, 0, other, false, false);
    two(params.sounds2_E80[3], 0x19, 0, other, false, false);
    two(params.sounds2_E80[4], 0x1A, 8, other, true, false);
    two(params.sounds2_E80[5], 0x1B, 0, other, true, false);
    two(params.sounds2_E80[6], 0x1C, 0, other, false, false);
    two(params.sounds2_E80[7], 0x1D, 0, other, false, false);
    for (int k = VEHICLE_CRASH_SOUNDS; k-- > 0;) {
        two(params.crash2A[k], SOUNDS2_CRASH + k, 0, other, false, false);
        two(params.crash2B[k], SOUNDS2_CRASH + VEHICLE_CRASH_SOUNDS + k, 0, other, false, false);
    }
    two(params.sound2_F90, 0x26, 0, other, false, false);
    two(params.sound2_FA0, 0x27, 0, other, false, false);
    two("PNEU_PUNC.WAV", 0x28, 8, other, true, false);
    return true;
}

long C_Vehicle::OpenSound(I3D_sound *sound, const char *fileName, uint32_t flags)   // 0x0051bd50
{
    return g_soundCache.OpenInto(sound, fileName, flags);
}

void C_Vehicle::LoadSound(const char *name, I3D_sound *&slot, uint32_t flags, float nearRange, float farRange,
                          float range2, float range3, bool loop, bool atEngine)   // 0x0051bd80
{
    if (!*name) {
        if (slot) {
            slot->LinkTo(nullptr, 0);
            slot->SetOn(false);
            slot->Release();
            slot = nullptr;
        }
        return;
    }
    if (slot) {
        // Fix: compared as it is (the original first copies the current name into an 80-byte stack
        // buffer).
        if (std::strcmp(name, slot->GetFileName()) == 0)
            return;
        slot->SetOn(false);
    } else {
        slot = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
        if (!slot)
            return;
    }
    if (OpenSound(slot, name, flags & VEHICLE_SOUND_OPEN_8) != 0) {
        slot->Release();
        slot = nullptr;
        return;
    }
    slot->SetOn(false);
    slot->SetVolume(1.0f);
    slot->Unknown88(0);
    if (loop)
        slot->SetLoop(true);
    if (flags & VEHICLE_SOUND_AMBIENT) {
        slot->SetSoundType(I3DSOUND_AMBIENT);
        slot->SetCone(0.0f, 0.0f);
        slot->LinkTo(g_carScene->GetPrimarySector(), 0);
    } else {
        slot->SetSoundType(I3DSOUND_POINT);
        slot->SetCone(6.283185f, 6.283185f);
        slot->SetRange(nearRange, farRange, range2, range3);
        slot->LinkTo(m_model, 0);
    }
    if (atEngine)
        slot->SetPosInline(S_vector(m_smokePos.x, m_smokePos.y, m_smokePos.z));
    slot->Update();
}

void C_Vehicle::AddSoundsToSectors()                                            // 0x00517cf0
{
    for (size_t s = s_soundSectors.size(); s-- > 0;)
        ForEachSound(m_sounds1, m_sounds2, [&](I3D_sound *sound) {
            if (sound)
                s_soundSectors[s]->AddSound(sound);
        });
}

void C_Vehicle::RemoveSoundsFromSectors()                                       // 0x005181c0
{
    for (size_t s = s_soundSectors.size(); s-- > 0;)
        ForEachSound(m_sounds1, m_sounds2, [&](I3D_sound *sound) {
            if (sound)
                s_soundSectors[s]->RemoveSound(sound);
        });
}

void C_Vehicle::ReleaseSounds()                                                 // 0x0051c180
{
    ForEachSound(m_sounds1, m_sounds2, [](I3D_sound *&sound) {
        if (sound) {
            sound->LinkTo(nullptr, 0);
            sound->Release();
            sound = nullptr;
        }
    });
}

void C_Vehicle::SwitchEngine(bool on)                                           // 0x00518710
{
    // The starter ([0]) and stop ([1]) sounds of both groups.
    I3D_sound *const starts[4] = {m_sounds1[0], m_sounds1[1], m_sounds2[0], m_sounds2[1]};
    bool quiet = true;
    if (m_soundsOn)
        for (I3D_sound *sound : starts)
            if (IsPlaying(sound))
                quiet = false;
    if (!on) {
        if (!m_unknown22A2)
            return;
        StopSounds();
        I3D_sound *stop = m_unknown2297 ? m_sounds1[1] : m_sounds2[1];
        if (stop && m_soundsOn)
            StartSound(stop, s_soundVolume);
        for (I3D_sound *sound : starts)
            if (sound)
                sound->Play(0);
        m_engineRevs = 0.0f;
        m_engineOn = false;
        m_unknown22A2 = 0;
        m_unknown22A0 = 0;
        return;
    }
    if (!m_unknown22A2 && !m_engineOn && quiet) {
        I3D_sound *starter = m_unknown2297 ? m_sounds1[0] : m_sounds2[0];
        if (starter && m_soundsOn) {
            starter->Play(0);
            StartSound(starter, s_soundVolume);
        }
        m_unknown22A2 = 1;
    }
    if (!m_unknown22A2)
        return;
    // Running once none of them has more than 300 ms left (Update calls again until then).
    if (m_soundsOn) {
        bool done = true;
        for (I3D_sound *sound : starts) {
            if (!IsPlaying(sound))
                continue;
            const uint32_t length = static_cast<uint32_t>(sound->GetLength());
            if (length > 300 && length - static_cast<uint32_t>(sound->GetPosition()) > 300) {
                m_unknownF48 = 0.0f;
                done = false;
            }
        }
        if (!done)
            return;
    }
    m_engineOn = true;
    m_unknown2298 = m_unknown2297;
}

void C_Vehicle::UpdateEngineSound(float timeDelta)                              // 0x00518ab0
{
    if (!m_engineOn)
        return;
    const float volume = s_soundVolume;
    // The engine fades in over its first 0.3 s (the scale is put back at the end).
    m_unknownF48 += timeDelta;
    if (m_unknownF48 > 0.3f)
        m_unknownF48 = 0.3f;
    s_soundVolume = s_soundVolume * m_unknownF48 * 3.3333333f;
    float revs = m_engineRevs;
    if (revs < m_idleRevs)
        revs = m_idleRevs;
    else if (revs > m_revLimit - 0.001f)
        revs = m_revLimit - 0.001f;

    const auto layer = [&](int set, int i) {
        return m_unknown2298 ? m_sounds1[LAYERS1 + set * VEHICLE_SOUND_LAYERS + i]
                             : m_sounds2[LAYERS2 + set * VEHICLE_SOUND_LAYERS + i];
    };
    const auto switchOff = [](I3D_sound *sound) {
        if (sound)
            sound->SetOn(false);
    };
    const auto stopSet = [&](int set) {
        for (int i = VEHICLE_SOUND_LAYERS; i-- > 0;)
            switchOff(layer(set, i));
    };
    if (m_unknown2298 != m_unknown2297) {
        // The other group: the old one's loops stop.
        for (int set = 0; set < 3; ++set)
            stopSet(set);
        if (!m_unknown2298) {
            switchOff(m_sounds2[IDLE2]);
            switchOff(m_sounds2[ROLLING2]);
            switchOff(m_sounds2[SINGLE_LOOP2]);
        } else {
            switchOff(m_sounds1[IDLE1]);
            switchOff(m_sounds1[ROLLING1]);
        }
        m_unknown2298 = m_unknown2297;
    }

    if (!(m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS) && !m_unknown2298) {
        // Group 2 without the details: one loop, its volume rising to +0xCF8 at +0xCF4's revs,
        // its pitch from +0xCEC at idle to +0xCF0 at the limit.
        if (I3D_sound *sound = m_sounds2[SINGLE_LOOP2]) {
            float loudness = m_unknownCF8;
            if (revs < m_unknownCF4)
                loudness = (revs - m_idleRevs) / (m_unknownCF4 - m_idleRevs) * loudness;
            const float pitch =
                (revs - m_idleRevs) / ((m_revLimit - m_idleRevs) / (m_unknownCF0 - m_unknownCEC)) + m_unknownCEC;
            if (!sound->IsPlaying(0))
                sound->SetOn(true);
            sound->SetFrequency(pitch);
            sound->SetVolume(s_soundVolume * loudness);
        }
    } else {
        const bool group1 = m_unknown2298 != 0;
        const auto &rows = group1 ? m_layers1 : m_layers2;
        if (m_gear < 0) {
            stopSet(0);
            stopSet(1);
        } else {
            stopSet(2);
        }
        if (m_gear >= 0) {
            // In gear: set A by the load, set B by its rest; a flagged layer is silent in neutral.
            const uint8_t *flagsA = group1 ? m_layers1AFlags : m_layers2AFlags;
            const uint8_t *flagsB = group1 ? m_layers1BFlags : m_layers2BFlags;
            for (int i = VEHICLE_SOUND_LAYERS; i-- > 0;)
                UpdateLayer(layer(0, i), rows[0], i, revs, m_unknown78C, flagsA[i] && m_gear == 0, s_soundVolume);
            for (int i = VEHICLE_SOUND_LAYERS; i-- > 0;)
                UpdateLayer(layer(1, i), rows[1], i, revs, 1.0f - m_unknown78C, flagsB[i] && m_gear == 0,
                            s_soundVolume);
        } else {
            for (int i = VEHICLE_SOUND_LAYERS; i-- > 0;)
                UpdateLayer(layer(2, i), rows[2], i, revs, m_unknown78C, false, s_soundVolume);
        }
    }

    // The idle loop: loudest (+0xC04 / +0xEE4) at +0xC00 / +0xEE0's revs, silent at +0xBFC /
    // +0xEDC's.
    const bool group1 = m_unknown2298 != 0;
    if (I3D_sound *idle = group1 ? m_sounds1[IDLE1] : m_sounds2[IDLE2]) {
        const float loud = group1 ? m_unknownC00 : m_unknownEE0, silent = group1 ? m_unknownBFC : m_unknownEDC;
        if (revs < loud || !(revs <= silent)) {
            if (idle->IsPlaying(0))
                idle->SetOn(false);
        } else {
            const float loudness = (silent - revs) / (silent - loud) * (group1 ? m_unknownC04 : m_unknownEE4);
            if (!idle->IsPlaying(0)) {
                if (loudness > 0.01f)
                    StartSound(idle, s_soundVolume * loudness);
            } else if (loudness < 0.01f) {
                idle->SetOn(false);
            } else {
                idle->SetVolume(s_soundVolume * loudness);
            }
        }
    }
    s_soundVolume = volume;
}

void C_Vehicle::PlayGearSound()                                                 // 0x0051a480
{
    if (!(m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS))
        return;
    // Not over the last one (of either group).
    const bool quiet = !IsPlaying(m_sounds1[GEAR1 + m_unknown650]);
    if (IsPlaying(m_sounds2[GEAR2 + m_unknown650]) || !quiet)
        return;
    // The second sound one time in five.
    m_unknown650 = std::rand() % 100 > 79;
    if (I3D_sound *sound = m_unknown2298 ? m_sounds1[GEAR1 + m_unknown650] : m_sounds2[GEAR2 + m_unknown650])
        StartSound(sound, s_soundVolume);
}

void C_Vehicle::PlayEngineFail()                                                // 0x0051a620
{
    const bool quiet = !IsPlaying(m_sounds1[2]);
    if (IsPlaying(m_sounds2[2]) || !quiet)
        return;
    if (I3D_sound *sound = m_unknown2298 ? m_sounds1[2] : m_sounds2[2])
        StartSound(sound, s_soundVolume);
}

void C_Vehicle::StopEngineLoops()                                               // 0x0051b7c0
{
    const auto stop = [](I3D_sound *sound) {
        if (sound)
            sound->SetActive(false, true);
    };
    for (int i = 0; i < 3 * VEHICLE_SOUND_LAYERS; ++i)
        stop(m_sounds1[LAYERS1 + i]);
    stop(m_sounds1[IDLE1]);
    stop(m_sounds2[SINGLE_LOOP2]);
    for (int i = 0; i < 3 * VEHICLE_SOUND_LAYERS; ++i)
        stop(m_sounds2[LAYERS2 + i]);
    stop(m_sounds2[IDLE2]);
}

void C_Vehicle::StopSounds()                                                    // 0x0051b8f0
{
    ForEachSound(m_sounds1, m_sounds2, [](I3D_sound *sound) {
        if (sound)
            sound->SetActive(false, true);
    });
}

void C_Vehicle::UpdateRollingSound()                                            // 0x00519a60
{
    const float speed = S_vector(m_velocity.x, m_velocity.y, m_velocity.z).Magnitude();
    if (m_unknown2298) {
        const float curve[5] = {m_unknownC08, m_unknownC0C, m_unknownC10, m_unknownC14, m_unknownC18};
        if (I3D_sound *sound = m_sounds1[ROLLING1])
            UpdateSpeedSound(sound, speed, curve, s_soundVolume);
    } else if ((m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS) && m_sounds2[ROLLING2]) {
        const float curve[5] = {m_unknownEF8, m_unknownEFC, m_unknownF00, m_unknownF04, m_unknownF08};
        UpdateSpeedSound(m_sounds2[ROLLING2], speed, curve, s_soundVolume);
    }
}

void C_Vehicle::UpdateRimSound(bool on)                                         // 0x00519dc0
{
    if (!on) {
        if (m_sounds1[RIM1])
            m_sounds1[RIM1]->SetOn(false);
        if (m_sounds2[RIM2])
            m_sounds2[RIM2]->SetOn(false);
        return;
    }
    // By the speed along the car.
    const float speed = std::fabs((m_modelAxisZ.z * m_velocity.z + m_modelAxisZ.y * m_velocity.y) +
                                  m_modelAxisZ.x * m_velocity.x);
    if (m_unknown2298) {
        if (I3D_sound *sound = m_sounds1[RIM1])
            UpdateSpeedSound(sound, speed, m_unknownC20, s_soundVolume);
    } else if ((m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS) && m_sounds2[RIM2]) {
        UpdateSpeedSound(m_sounds2[RIM2], speed, m_unknownF20, s_soundVolume);
    }
}

void C_Vehicle::UpdateTyreSqueal(float timeDelta)                               // 0x0051a130
{
    // The loudest wheel on a squealing surface while the car moves (over 0.6 m/s): locked or
    // slipping past its upper angle full, between its two angles by the slip, quieter under 2 m/s
    // of contact speed; a spinning wheel at least by its overspeed (from +0x5E4 over the car's
    // speed, full +0x5E8 over it).
    float level = 0.0f;
    for (int i = m_wheelCount; i-- > 0;) {
        const S_VehicleWheel *w = m_wheels[i];
        if (!(w->flags & WHEEL_ON_GROUND) || (w->flags & WHEEL_DUMMY) ||
            !g_carSurfaces[g_carSurfaceOfMaterial[w->surface]].tyreSqueal || !(m_speed > 0.6f))
            continue;
        float wheel = 0.0f;
        if (w->flags & WHEEL_LOCKED) {
            wheel = 1.0f;
        } else if (w->unknown1D4 > w->unknown224) {
            wheel = 1.0f;
        } else if (w->unknown1D4 > w->unknown220) {
            const float slip = (w->unknown1D4 - w->unknown220) / (w->unknown224 - w->unknown220);
            if (slip > 0.0f)
                wheel = slip;
        }
        const S_vector &v = w->unknown0A0;
        const float square = (v.x * v.x + v.y * v.y) + v.z * v.z;
        if (square < 4.0f)
            wheel = wheel * square * 0.25f;
        if (w->flags & WHEEL_SPINNING) {
            const float spin = m_engineRevs / m_gearRatios[m_gear + 1] * m_maxWheelCircumference;
            const float over = m_unknown5E4 + m_speed;
            if (spin > over) {
                const float spinning = m_unknown5E8 + m_speed < spin ? 1.0f : (spin - over) / m_unknown5E8;
                if (spinning > wheel)
                    wheel = spinning;
            }
        }
        if (wheel > level)
            level = wheel;
        if (level > 0.99999f) {
            level = 1.0f;
            break;
        }
    }
    // Rising by +0x5EC per second, falling at once.
    const float step = timeDelta * m_unknown5EC;
    if (step > level - m_unknown5F0)
        m_unknown5F0 = level;
    else
        m_unknown5F0 = step + m_unknown5F0;
    if (m_unknown5F0 >= 0.99999f)
        m_unknown5F0 = 1.0f;
    if (m_unknown5F0 < 0.01f) {
        if (m_sounds1[SQUEAL1])
            m_sounds1[SQUEAL1]->SetOn(false);
        if (m_sounds2[SQUEAL2])
            m_sounds2[SQUEAL2]->SetOn(false);
        return;
    }
    I3D_sound *sound = m_unknown2298 ? m_sounds1[SQUEAL1] : m_sounds2[SQUEAL2];
    I3D_sound *other = m_unknown2298 ? m_sounds2[SQUEAL2] : m_sounds1[SQUEAL1];
    if (sound) {
        if (!sound->IsPlaying(0))
            sound->SetOn(true);
        sound->SetVolume(s_soundVolume * m_unknown5F0);
    }
    if (other)
        other->SetOn(false);
}

void C_Vehicle::PlayHandbrakeSound()                                            // 0x0051a580
{
    if (!(m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS))
        return;
    // Once per pull (Update clears +0x228E while the handbrake is off).
    I3D_sound *sound = m_unknown2298 ? m_sounds1[HANDBRAKE1] : m_sounds2[HANDBRAKE2];
    if (sound && !sound->IsPlaying(0) && !m_unknown228E)
        StartSound(sound, s_soundVolume);
    m_unknown228E = 1;
}

void C_Vehicle::SwitchLoopSoundA(bool on)                                       // 0x0051a6c0
{
    SwitchLoop(m_sounds1[LOOP_A1], m_sounds2[LOOP_A2], m_unknown2298 != 0, on, s_soundVolume);
}

void C_Vehicle::SwitchLoopSoundB(bool on)                                       // 0x0051a7a0
{
    SwitchLoop(m_sounds1[LOOP_B1], m_sounds2[LOOP_B2], m_unknown2298 != 0, on, s_soundVolume);
}

void C_Vehicle::PlayWheelBurst(int wheel)                                       // 0x0051a880
{
    if (!m_soundsOn)
        return;
    if (m_unknown2298) {
        if (I3D_sound *sound = m_sounds1[BURST1])
            StartSound(sound, s_soundVolume);
        return;
    }
    if (I3D_sound *sound = m_sounds2[BURST2]) {
        StartSound(sound, s_soundVolume);
        // At the wheel.
        const S_vector4 &pos = m_wheels[wheel]->modelPos;
        sound->SetPosInline(S_vector(pos.x, pos.y, pos.z));
    }
}

void C_Vehicle::PlayImpactSound(float impulse, bool unknown)                    // 0x0051a950
{
    // The crash level by the playing group's thresholds (none up to the first).
    const bool group1 = m_unknown2298 != 0;
    const float *thresholds = group1 ? m_crash1Thresholds : m_crash2Thresholds;
    if (!(impulse > thresholds[0]))
        return;
    const int level = impulse < thresholds[1] ? 0 : impulse < thresholds[2] ? 1 : impulse < thresholds[3] ? 2 : 3;
    if ((group1 ? m_crash1Flags : m_crash2Flags)[level] && unknown)
        return;
    // One of the level's two sounds: the one there is, or by rand half and half; not over
    // itself, nor within 800 ms of the other's start.
    I3D_sound *const *crash = group1 ? m_sounds1 + SOUNDS1_CRASH : m_sounds2 + SOUNDS2_CRASH;
    I3D_sound *a = crash[level], *b = crash[VEHICLE_CRASH_SOUNDS + level];
    if (!a) {
        if (b && !b->IsPlaying(0))
            StartSound(b, s_soundVolume);
        return;
    }
    if (!b) {
        if (!a->IsPlaying(0))
            StartSound(a, s_soundVolume);
        return;
    }
    I3D_sound *sound = a, *other = b;
    if (std::rand() > 0x3FFE)
        sound = b, other = a;
    if (sound->IsPlaying(0))
        return;
    if (other->IsPlaying(0) && static_cast<uint32_t>(other->GetPosition()) < 800)
        return;
    StartSound(sound, s_soundVolume);
}

void C_Vehicle::PlayBumpSound(float volume)                                     // 0x0051b560
{
    if (!m_soundsOn)
        return;
    I3D_sound *sound = m_unknown2298 ? m_sounds1[BUMP1] : m_sounds2[BUMP2];
    if (!sound)
        return;
    // While it plays, a bump quieter by 0.15 or more leaves it (Quirk kept: asks twice).
    if (sound->IsPlaying(0) && (!sound->IsPlaying(0) || !(volume + 0.15f > sound->GetVolumeInline())))
        return;
    StartSound(sound, s_soundVolume * volume);
}

void C_Vehicle::PlayDoorSound(int seat, bool opening)                           // 0x0051b660
{
    if (!m_soundsOn)
        return;
    I3D_sound *sound = opening ? (m_unknown2298 ? m_sounds1[DOOR_OPEN1] : m_sounds2[DOOR_OPEN2])
                               : (m_unknown2298 ? m_sounds1[DOOR_SHUT1] : m_sounds2[DOOR_SHUT2]);
    if (!sound)
        return;
    StartSound(sound, s_soundVolume);
    // At the door.
    const S_vector4 &pos = m_seats[seat].door->pos;
    sound->SetPosInline(S_vector(pos.x, pos.y, pos.z));
}
