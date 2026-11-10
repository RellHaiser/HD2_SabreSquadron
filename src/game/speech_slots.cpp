// The speech slots (part 11b; original source 0x0071e1e0..0x0071fb60): 16 slots of played dubbing, each with its sound frame and
// the speaker's mouth moved from the lip-sync data; the material tables and the debug lines beside them. See game/world.h.
#include <cstdio>
#include <cstring>

#include "game/car_effects.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"

namespace {

constexpr int SEEK_FROM_START = 0;
constexpr int SEEK_FROM_END = 2;
constexpr int SPEECH_SLOTS = 16;
constexpr int STATE_FREE = 0;
constexpr int STATE_ATTACHED = 1;
constexpr int STATE_PLAYING = 2;
constexpr int STATE_CLOSING = 3;                        // the mouth closes
constexpr int STATE_DELAY = 4;
constexpr int STATE_PLAYING_KEPT = 5;                   // as playing; its sound is not released here
constexpr int MOUTH_CLOSE_TIME = 200;                   // ms
constexpr int LIP_FRAME_TIME = 20;                      // ms
constexpr float BYTE_TO_WEIGHT = 0.00392156886f;        // 0x0080f6ec
constexpr float CLOSE_RATE = 0.005f;                    // per ms left: 1 / MOUTH_CLOSE_TIME
constexpr float FADE_IN_RATE = 0.008f;                  // per ms

// What drives the mouth: the six values of a lip-sync frame, as 0..1.
struct S_MouthPose {
    float a, b, c, d, e, f;
};

struct S_SpeechSlot {                                   // 0x44 bytes
    uint32_t *lipData;                                  // +0x00 the frame count, then 8 bytes for each frame from +8
    uint32_t time;                                      // +0x04 ms played
    uint32_t timer;                                     // +0x08
    int state;                                          // +0x0C
    uint32_t unknown10;
    I3D_sound *sound;                                   // +0x14
    I3D_frame *frame;                                   // +0x18
    I3D_frame *morph;                                   // +0x1C the speaker's 'SMMR' visual, "a"
    I3D_frame *extraFrameA;                             // +0x20 the human's +0x2BC
    I3D_frame *extraFrameB;                             // +0x24 the human's +0x2B8
    // The pose the speech starts from (the fade in) and the closing mouth works on.
    float poseB, poseA, poseC, poseE, poseD, poseF;     // +0x28
    float fadeIn;                                       // +0x40
};
static_assert(sizeof(S_SpeechSlot) == 0x44 || sizeof(void *) != 4, "S_SpeechSlot layout");

S_SpeechSlot g_slots[SPEECH_SLOTS];                     // 0x008cb8e0
int g_resyncCount;                                      // 0x008cbd20 thinks until the sounds are put back at their times

void ClearAllSlots()
{
    std::memset(g_slots, 0, sizeof g_slots);
}

// The channels at rest: 0, 1 and 2 on their first target.
void SetNeutralMouth(I3D_frame *visual)
{
    I3D_morph *morph = I3D_GetMorph(visual);
    for (int channel = 0; channel < 3; ++channel) {
        morph->SetWeight(channel, 0, 1.0f);
        for (int target = 1; target < 5; ++target)
            morph->SetWeight(channel, target, 0.0f);
    }
}

void ApplyMouth(I3D_frame *visual, const S_MouthPose &p)
{
    I3D_morph *morph = I3D_GetMorph(visual);
    morph->SetWeight(0, 0, 0.0f);
    morph->SetWeight(0, 1, p.a * p.b);
    morph->SetWeight(0, 2, (1.0f - p.b) * p.a);
    const float notA = 1.0f - p.a;
    morph->SetWeight(0, 3, (1.0f - p.c) * notA);
    morph->SetWeight(0, 4, notA * p.c);
    morph->SetWeight(1, 0, 0.0f);
    morph->SetWeight(1, 1, p.d * p.e);
    morph->SetWeight(1, 2, (1.0f - p.d) * p.e);
    const float notE = 1.0f - p.e;
    morph->SetWeight(1, 3, notE);
    morph->SetWeight(2, 0, 0.0f);
    morph->SetWeight(2, 1, p.f * p.e);
    morph->SetWeight(2, 2, (1.0f - p.f) * p.e);
    morph->SetWeight(2, 3, notE);
}

// The speaker's morph visual, null without one.
I3D_frame *FindMouth(I3D_frame *frame)
{
    I3D_frame *visual = frame->FindChildFrame("a", 0xFFFF);
    return visual && visual->GetVisualType() == I3DVISUAL_SINGLEMORPH ? visual : nullptr;
}

void ReleaseSlotSound(S_SpeechSlot &slot)
{
    if (slot.sound && slot.state != STATE_PLAYING_KEPT)
        slot.sound->Release();
    slot.sound = nullptr;
}

void StopSpeechSlot(int index)                          // 0x0071f0c0
{
    if (index < 0 || index >= SPEECH_SLOTS)
        return;
    S_SpeechSlot &slot = g_slots[index];
    if (slot.sound)
        slot.sound->SetActive(false, true);
    slot.timer = 1;
    slot.state = STATE_CLOSING;
}

bool OpenDubbingSample(I3D_sound *sound, int dubbing)   // 0x0071ebc0
{
    static const char *const DIRECTORIES[] = {"sounds"};   // 0x008441e8
    for (const char *directory : DIRECTORIES) {
        char name[0x100];
        std::sprintf(name, "%s\\%08d.wav", directory, dubbing);
        if (sound->Open(name, 0) == 0)
            return true;
    }
    return false;
}

void LoadLipData(S_SpeechSlot &slot, int dubbing)
{
    delete[] reinterpret_cast<char *>(slot.lipData);
    slot.lipData = nullptr;
    char name[0x100];
    std::sprintf(name, "tables\\dabing\\%08d.dat", dubbing);
    const int file = dtaOpen(name, false);
    if (file < 0) {
        dbgPrintf("Nenalezen Dabing %08d.dat", dubbing);
        return;
    }
    const int size = dtaSeek(file, 0, SEEK_FROM_END);
    if (size != 0) {
        dtaSeek(file, 0, SEEK_FROM_START);
        slot.lipData = reinterpret_cast<uint32_t *>(new char[static_cast<size_t>(size)]);
        *slot.lipData = 0;
        dtaRead(file, slot.lipData, size);
    }
    dtaClose(file);
}

// (Playing or kept) the mouth at lip-sync frame `lipFrame`.
void UpdateSpeakingMouth(S_SpeechSlot &slot, uint32_t lipFrame, int timeDelta)
{
    if (!slot.lipData || !slot.morph)
        return;
    const uint32_t count = slot.lipData[0];
    if (lipFrame >= count)
        lipFrame = count - 2;
    const auto *bytes = reinterpret_cast<const uint8_t *>(slot.lipData) + 8 + lipFrame * 8;
    S_MouthPose p;
    p.a = static_cast<float>(bytes[0]) * BYTE_TO_WEIGHT;
    p.b = static_cast<float>(bytes[1]) * BYTE_TO_WEIGHT;
    p.c = static_cast<float>(bytes[2]) * BYTE_TO_WEIGHT;
    p.d = static_cast<float>(bytes[3]) * BYTE_TO_WEIGHT;
    p.e = static_cast<float>(bytes[4]) * BYTE_TO_WEIGHT;
    p.f = static_cast<float>(bytes[5]) * BYTE_TO_WEIGHT;
    if (slot.fadeIn < 1.0f) {
        const float fade = static_cast<float>(static_cast<uint32_t>(timeDelta)) * FADE_IN_RATE + slot.fadeIn;
        slot.fadeIn = fade;
        if (fade < 1.0f) {
            const float from = 1.0f - fade;
            p.b = from * slot.poseB + fade * p.b;
            p.a = from * slot.poseA + fade * p.a;
            p.c = from * slot.poseC + fade * p.c;
            p.e = from * slot.poseE + fade * p.e;
            p.d = from * slot.poseD + fade * p.d;
            p.f = from * slot.poseF + fade * p.f;
        }
    }
    ApplyMouth(slot.morph, p);
}

// The mouth closes from the last lip-sync frame; it stays the pose the next speech fades from.
void UpdateClosingMouth(S_SpeechSlot &slot)
{
    if (!slot.lipData || !slot.morph)
        return;
    const auto *bytes = reinterpret_cast<const uint8_t *>(slot.lipData) + slot.lipData[0] * 8 - 8;
    const float left = static_cast<float>(slot.timer) * CLOSE_RATE;
    slot.poseB = static_cast<float>(bytes[1]) * BYTE_TO_WEIGHT * left;
    slot.poseA = static_cast<float>(bytes[0]) * BYTE_TO_WEIGHT * left;
    slot.poseC = static_cast<float>(bytes[2]) * BYTE_TO_WEIGHT;
    slot.poseE = static_cast<float>(bytes[4]) * BYTE_TO_WEIGHT;
    slot.poseD = static_cast<float>(bytes[3]) * BYTE_TO_WEIGHT;
    slot.poseF = static_cast<float>(bytes[5]) * BYTE_TO_WEIGHT;
    S_MouthPose p;
    p.a = slot.poseA;
    p.b = slot.poseB;
    p.c = slot.poseC;
    p.d = slot.poseD;
    p.e = slot.poseE;
    p.f = slot.poseF;
    ApplyMouth(slot.morph, p);
}

void CountDownTimer(S_SpeechSlot &slot, int timeDelta)
{
    if (static_cast<uint32_t>(timeDelta) < slot.timer) {
        slot.timer -= static_cast<uint32_t>(timeDelta);
    } else {
        slot.timer = 0;
        slot.state = STATE_FREE;
    }
}

}  // namespace

// (The original's table starts with a list of morph fades that no code adds to: its loop in the update is left out.)

void ClearSpeechSlots()
{
    ClearAllSlots();
}

void FreeSpeechSlots()
{
    for (S_SpeechSlot &slot : g_slots) {
        ReleaseSlotSound(slot);
        delete[] reinterpret_cast<char *>(slot.lipData);
    }
    ClearAllSlots();
}

int FindFreeSpeechSlot()
{
    for (int i = SPEECH_SLOTS - 1; i >= 0; --i) {
        if (g_slots[i].state == STATE_FREE)
            return i;
    }
    return SPEECH_SLOTS - 1;
}

void AttachSpeechSlot(int index, C_Human *owner)
{
    if (index < 0 || index >= SPEECH_SLOTS || !owner)
        return;
    S_SpeechSlot &slot = g_slots[index];
    slot.frame = owner->GetFrame();
    slot.extraFrameB = owner->GetSpeechFrameB();
    slot.extraFrameA = owner->GetSpeechFrameA();
    slot.morph = FindMouth(slot.frame);
    slot.state = STATE_ATTACHED;
    if (!slot.morph)
        return;
    SetNeutralMouth(slot.morph);
    slot.poseA = 0.0f;
    slot.poseB = 0.46f;
    slot.poseC = 1.0f;
    slot.poseD = 0.11f;
    slot.poseE = 1.0f;
    slot.poseF = 0.6f;
    S_MouthPose p;
    p.a = slot.poseA;
    p.b = slot.poseB;
    p.c = slot.poseC;
    p.d = slot.poseD;
    p.e = slot.poseE;
    p.f = slot.poseF;
    ApplyMouth(slot.morph, p);
}

void ResetSpeechSlots()
{
#ifndef HD2_DEDICATED   // the server's copy is empty
    for (S_SpeechSlot &slot : g_slots) {
        if (slot.morph)
            SetNeutralMouth(slot.morph);
        delete[] reinterpret_cast<char *>(slot.lipData);
        ReleaseSlotSound(slot);
    }
    ClearAllSlots();
#endif
}

void StopFrameSpeech(I3D_frame *owner)
{
#ifdef HD2_DEDICATED   // the server's copy is empty
    (void)owner;
#else
    for (int i = 0; i < SPEECH_SLOTS; ++i) {
        if (g_slots[i].state == STATE_PLAYING && g_slots[i].frame == owner)
            StopSpeechSlot(i);
    }
#endif
}

void AttachSpeechSlot(int index, I3D_frame *frame)
{
    if (index < 0 || index >= SPEECH_SLOTS || !frame)
        return;
    S_SpeechSlot &slot = g_slots[index];
    slot.frame = frame;
    slot.morph = FindMouth(frame);
    slot.state = STATE_ATTACHED;
    if (!slot.morph)
        return;
    slot.extraFrameB = nullptr;
    slot.extraFrameA = nullptr;
    SetNeutralMouth(slot.morph);
}

void ReleaseHumanSpeech(const C_Human *human)
{
    I3D_frame *frame = human->GetFrame();
    if (!frame)
        return;
    for (S_SpeechSlot &slot : g_slots) {
        if (slot.frame != frame)
            continue;
        if (slot.morph)
            SetNeutralMouth(slot.morph);
        delete[] reinterpret_cast<char *>(slot.lipData);
        ReleaseSlotSound(slot);
        std::memset(&slot, 0, sizeof slot);
    }
}

void PlaySpeechSlot(int index, int dubbing, float nearRange, float farRange, bool showSubtitle, bool isActivePlayer)
{
    if (index < 0 || index >= SPEECH_SLOTS)
        return;
    S_SpeechSlot &slot = g_slots[index];
    LoadLipData(slot, dubbing);
    if (!slot.sound)
        slot.sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    if (!OpenDubbingSample(slot.sound, dubbing)) {
        slot.sound->Release();
        slot.sound = nullptr;
    }
    slot.state = STATE_PLAYING;
    if (!slot.sound) {
        if (showSubtitle)
            GetGame()->GetQuickMessages()->ShowDubbing(dubbing, -1);
    } else {
        I3D_sound *sound = slot.sound;
        if (showSubtitle)
            GetGame()->GetQuickMessages()->ShowDubbing(dubbing, sound->GetLength());
        sound->SetUnknown128(0x80);
        I3D_frame *head = slot.morph ? slot.morph->FindChildFrame("f", 0xFFFF) : nullptr;
        if (head) {
            sound->LinkTo(head, 0);
            sound->SetPosInline(S_vector(0.0f, 0.0f, 0.1f));
        } else {
            sound->LinkTo(slot.frame, 0);
            sound->SetPosInline(S_vector(0.0f, 1.6f, 0.0f));
        }
        sound->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        sound->SetSoundType(isActivePlayer ? I3DSOUND_AMBIENT : I3DSOUND_POINT);
        sound->SetRange(nearRange, farRange, 0.3f, 0.25f);
        sound->SetOutVolume(1.0f);
        sound->SetVolume(1.0f);
        sound->SetOn(false);
        sound->Play(0);
        sound->Update();
        sound->SetActive(true, true);
    }
    slot.timer = MOUTH_CLOSE_TIME;
    slot.time = 0;
    slot.fadeIn = 0.0f;
}

bool IsHumanSpeaking(const C_Human *human)
{
#ifdef HD2_DEDICATED   // the server's copy: nobody speaks
    (void)human;
#else
    for (const S_SpeechSlot &slot : g_slots) {
        if (slot.state == STATE_PLAYING && slot.frame == human->GetFrame())
            return true;
    }
#endif
    return false;
}

void RefreshSpeechSlotFrames()
{
    const auto refresh = [](I3D_frame *frame) {
        if (frame && frame->IsOn()) {
            frame->SetPosInline(frame->GetPos());
            frame->Update();
        }
    };
    for (const S_SpeechSlot &slot : g_slots) {
        refresh(slot.extraFrameA);
        refresh(slot.extraFrameB);
    }
}

void UpdateSpeechSlots(int timeDelta)
{
    bool resync = false;
    if (g_resyncCount > 0 && --g_resyncCount <= 0)
        resync = true;
    for (S_SpeechSlot &slot : g_slots) {
        if (slot.state == STATE_FREE)
            continue;
        if (!slot.sound)
            slot.time += static_cast<uint32_t>(timeDelta);
        uint32_t lipFrame = 0;
        if (slot.state == STATE_PLAYING) {
            bool ended = false;
            if (slot.sound) {
                if (!slot.sound->IsPlaying(0)) {
                    ended = true;
                } else if (resync) {
                    slot.sound->Play(static_cast<int>(slot.time));
                } else {
                    const auto position = static_cast<uint32_t>(slot.sound->GetPosition());
                    // Back at the start while nearly at the end: it has looped round.
                    if (slot.time > position && slot.time + 300u > static_cast<uint32_t>(slot.sound->GetLength())) {
                        ended = true;
                    } else {
                        slot.time = position;
                        lipFrame = position / LIP_FRAME_TIME;
                    }
                }
            } else {
                lipFrame = slot.time / LIP_FRAME_TIME;
                if (!slot.lipData || lipFrame >= slot.lipData[0] - 1)
                    ended = true;
            }
            if (ended) {
                slot.timer = MOUTH_CLOSE_TIME;
                slot.state = STATE_CLOSING;
            }
        }
        switch (slot.state) {
        case STATE_PLAYING:
            UpdateSpeakingMouth(slot, lipFrame, timeDelta);
            break;
        case STATE_CLOSING:
            CountDownTimer(slot, timeDelta);
            UpdateClosingMouth(slot);
            break;
        case STATE_DELAY:
            CountDownTimer(slot, timeDelta);
            break;
        case STATE_PLAYING_KEPT:
            if (slot.sound) {
                slot.time = static_cast<uint32_t>(slot.sound->GetPosition());
                UpdateSpeakingMouth(slot, slot.time / LIP_FRAME_TIME, timeDelta);
            }
            break;
        default:
            break;
        }
    }
}

void PauseSpeechSlots(bool paused)
{
    for (S_SpeechSlot &slot : g_slots) {
        if (!slot.sound)
            continue;
        if (paused) {
            if (slot.sound->IsPlaying(0)) {
                slot.sound->SetActive(false, true);
                slot.time = static_cast<uint32_t>(slot.sound->GetPosition());
                slot.sound->Update();
            }
        } else if (slot.state == STATE_PLAYING) {
            slot.sound->SetOn(true);
            slot.sound->SetActive(true, true);
            g_resyncCount = 1;
            slot.sound->Update();
        }
    }
}

// ---- beside them -------------------------------------------------------------------------------------------------------

constexpr int MAX_DEBUG_LINES = 15000;
S_DebugVertex g_debugVertices[MAX_DEBUG_LINES * 2];     // (nothing in this build draws them)
S_DebugVertex *g_debugNext = g_debugVertices;           // 0x00844230
int g_debugLineCount;                                   // 0x0097b9a8

void DrawDebugLine(const float *from, const float *to, uint32_t color, uint32_t color2)
{
    if (g_debugLineCount >= MAX_DEBUG_LINES)
        return;
    ++g_debugLineCount;
    g_debugNext->x = from[0];
    g_debugNext->y = from[1];
    g_debugNext->z = from[2];
    g_debugNext->color = color | 0xFF000000u;
    ++g_debugNext;
    g_debugNext->x = to[0];
    g_debugNext->y = to[1];
    g_debugNext->z = to[2];
    g_debugNext->color = (color2 != 0 ? color2 : color) | 0xFF000000u;
    ++g_debugNext;
}

bool LoadMaterialTables()
{
    int file = dtaOpen("tables\\hdmaterials.def", false);
    if (file < 0)
        return false;
    int size = dtaSeek(file, 0, SEEK_FROM_END);
    dtaSeek(file, 0, SEEK_FROM_START);
    g_materials = reinterpret_cast<S_MaterialDef *>(new char[static_cast<size_t>(size)]);
    dtaRead(file, g_materials, size);
    dtaClose(file);
    g_materialCount = static_cast<uint32_t>(size) >> 8;
    file = dtaOpen("tables\\effects_car.def", false);
    if (file < 0)
        return false;
    delete[] reinterpret_cast<char *>(g_carEffects);
    size = dtaSeek(file, 0, SEEK_FROM_END);
    dtaSeek(file, 0, SEEK_FROM_START);
    g_carEffects = reinterpret_cast<S_CarEffectDef *>(new char[static_cast<size_t>(size)]);
    dtaRead(file, g_carEffects, size);
    dtaClose(file);
    g_carEffectCount = static_cast<uint32_t>(size) / 0xF0;
    return true;
}
