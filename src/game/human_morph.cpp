// The morphs of a human's model (C_HumanMorph, C_HumanMovement +0x610; original source
// 0x00780df0..0x007830e0): blinking, breathing by the exhaustion (with breath sounds),
// expressions, the grips of the hands and the dead face, blended on the channels of the model's
// skinned morph visual from the states of Tables\morph.sav.
#include "game/human_movement.h"

#include <cmath>
#include <cstdlib>

#include "common/chunk.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"

namespace {

// Tables\morph.sav: per animation group 19 states, each with the weights of 8 channels of up
// to 8 targets and the channels it sets.
constexpr int MORPH_GROUPS = 5;
constexpr int MORPH_STATES = 19;
constexpr int MORPH_CHANNELS = 8;
constexpr int MORPH_TARGETS = 8;

struct S_MorphWeights {                                 // 0x108 bytes
    float weights[MORPH_CHANNELS][MORPH_TARGETS];       // +0x000
    int valid;                                          // +0x100 1: the state exists
    int unknown104;
};
struct S_MorphFlags {                                   // 0x20 bytes
    uint8_t unknown00[0x0C];
    bool channels[MORPH_CHANNELS];                      // +0x0C the channels the state sets
    uint8_t unknown14[0x0C];
};
static_assert(sizeof(S_MorphWeights) == 0x108 && sizeof(S_MorphFlags) == 0x20, "morph table layout");

S_MorphWeights g_morphWeights[MORPH_GROUPS * MORPH_STATES];   // 0x00984130
S_MorphFlags g_morphFlags[MORPH_GROUPS * MORPH_STATES];       // 0x00983550
bool g_morphTablesLoaded = false;                             // 0x0098a345

// Morph channels.
enum : int {
    MORPH_CHANNEL_BREATH = 0,
    MORPH_CHANNEL_EYES = 1,
    MORPH_CHANNEL_GRIP2 = 3,
    MORPH_CHANNEL_GRIP = 4,
};
// The dead faces are states 15..18.
constexpr int MORPH_STATE_DEAD = 15;

// Times (ms of the movement clock; 0x0098a328..0x0098a340, set by static initialisers).
const int GRIP_BLEND_TIME = MoveTime(720.0f);          // 0x0098a328
const int EXPRESSION_BLEND_TIME = MoveTime(420.0f);    // 0x0098a32c
const int MAX_EXHAUSTION = MoveTime(30000.0f);          // 0x0098a330
const int BREATH_BLEND_TIME = MoveTime(250.0f);        // 0x0098a334
const int BLINK_DURATION = MoveTime(180.0f);           // 0x0098a338
const int BLINK_RANDOM = MoveTime(1200.0f);            // 0x0098a33c
const int BLINK_INTERVAL = MoveTime(4200.0f);          // 0x0098a340
// Exhaustion above this makes the body pant.
const float PANT_THRESHOLD = 8500.0f / MOVE_TIME_SCALE;

// The movement sound groups of panting.
constexpr int SOUND_PANT = MOVE_SOUND_UVS;
constexpr int SOUND_PANT_HEAVY = MOVE_SOUND_UV1;

// Morph state of an eye state (the blink channel).
int EyeState(int state, int fallback)
{
    switch (state) {
    case 0:
        return 8;
    case 1:
        return 7;
    case 3:
        return 4;
    case 4:
        return 5;
    case 5:
        return 6;
    default:
        return fallback;
    }
}

int GripState(int state, int base, int fallback)
{
    switch (state) {
    case 0:
        return base;
    case 1:
        return base + 2;
    case 2:
        return base + 4;
    default:
        return fallback;
    }
}

float BlendFactor(int time, int duration)
{
    const float t = static_cast<float>(time) / static_cast<float>(duration);
    return t > 1.0f ? 1.0f : t;
}

}  // namespace

// ------------------------------------------------------------------ lifetime

void C_HumanMorph::LoadTables()
{
    if (g_morphTablesLoaded)
        return;
    g_morphTablesLoaded = true;
    const int handle = dtaOpen("Tables\\morph.sav", 0);
    if (handle != -1 && dtaRead(handle, g_morphWeights, sizeof(g_morphWeights)) != -1)
        dtaRead(handle, g_morphFlags, sizeof(g_morphFlags));
    // (Also closes a handle that failed to open, as the original.)
    dtaClose(handle);
}

C_HumanMorph::C_HumanMorph()
{
    LoadTables();
    m_morphVisual = nullptr;
    m_group = 1;
}

void C_HumanMorph::Init(C_Human *owner)
{
    I3D_frame *model = owner->GetFrame();
    m_morphVisual = nullptr;
    m_owner = owner;
    m_group = owner->GetGroup();
    if (!model)
        return;
    for (I3D_frame *child = model->GetFirstChild(); child; child = child->GetNextSibling()) {
        if (child->GetVisualType() == I3DVISUAL_SINGLEMORPH) {
            m_morphVisual = child;
            break;
        }
    }
    m_head = m_owner->GetFrame()->FindChildFrame("f", FRAME_MASK_ANY);
    m_expressionRequest = 100;
    m_gripRequest = 100;
    m_grip2Request = 100;
    m_blinkState = 1;
    m_blinkPrevious = 1;
    m_blinkTime = 0;
    m_blinkDuration = 0;
    m_blinkTimer = -1;
    m_breathState = 0;
    m_breathPrevious = 0;
    m_breathTime = 0;
    m_breathDuration = 0;
    m_exhaustTime = -1;
    m_exhaustion = 0;
    m_heavyBreath = false;
    m_expressionSpeed = 1;
    m_expressionTime = 0;
    m_expressionDuration = 0;
    m_gripState = 0;
    m_gripPrevious = 0;
    m_gripTime = 0;
    m_gripDuration = 0;
    m_grip2State = 0;
    m_grip2Previous = 0;
    m_grip2Time = 0;
    m_grip2Duration = 0;
    m_breathSound = nullptr;
    m_breathSoundTime = -1;
    m_breathVolume = -1.0f;
    if (m_morphVisual) {
        Apply(true);
        m_dirty = false;
    }
}

void C_HumanMorph::Done()
{
    if (m_breathSound) {
        GetMovementSounds()->Return(m_breathSound);
        m_breathSound = nullptr;
    }
}

void C_HumanMorph::Reset()
{
    Init(m_owner);
    SetBlinking(true);
}

void C_HumanMorph::Relink(I3D_frame * /*unused*/)
{
    Init(m_owner);
}

// ------------------------------------------------------------------ requests

void C_HumanMorph::SetBlinking(bool on)
{
    if (!on) {
        m_blinkTimer = -1;
    } else if (m_blinkTimer == -1) {
        m_blinkTimer = std::rand() % BLINK_RANDOM + BLINK_INTERVAL;
    }
}

void C_HumanMorph::SetUnknown1(int speed)
{
    m_expressionRequest = 2;
    m_expressionSpeed = speed;
}

void C_HumanMorph::SetUnknown2(int speed)
{
    m_expressionRequest = 3;
    m_expressionSpeed = speed;
}

void C_HumanMorph::SetUnknown3(int speed)
{
    m_expressionRequest = 4;
    m_expressionSpeed = speed;
}

void C_HumanMorph::SetExhausted(bool start, bool cap)
{
    const int running = m_exhaustTime;
    if (running == -1) {
        if (start) {
            m_exhaustTime = m_exhaustion;
            m_exhaustion = 0;
        }
        return;
    }
    if (start)
        return;
    // Stopping: at least the panting threshold, settled above it.
    m_exhaustion = static_cast<float>(running) >= PANT_THRESHOLD ? running : static_cast<int>(PANT_THRESHOLD);
    const float exhaustion = static_cast<float>(m_exhaustion);
    if (cap && exhaustion > PANT_THRESHOLD + 101.0f) {
        m_exhaustion = static_cast<int>(PANT_THRESHOLD + 101.0f);
        m_exhaustTime = -1;
        return;
    }
    if (exhaustion > PANT_THRESHOLD && exhaustion < PANT_THRESHOLD + 1000.0f) {
        m_exhaustion = static_cast<int>(PANT_THRESHOLD + 1000.0f);
        m_exhaustTime = -1;
        return;
    }
    m_exhaustTime = -1;
}

void C_HumanMorph::ShowDeathFace()
{
    // Fix: a model without a morph visual is read at 0x120 in the original.
    if (!m_morphVisual)
        return;
    const int state = std::rand() % 4 + MORPH_STATE_DEAD + m_group * MORPH_STATES;
    const S_MorphWeights &weights = g_morphWeights[state];
    const S_MorphFlags &flags = g_morphFlags[state];
    if (weights.valid != 1)
        return;
    I3D_morph *morph = I3D_GetMorph(m_morphVisual);
    const int targets = morph->NumTargets();
    m_blinkTime = 0;
    m_blinkDuration = 0;
    m_blinkTimer = -1;
    m_breathTime = 0;
    m_breathDuration = 0;
    m_exhaustTime = -1;
    m_exhaustion = 0;
    m_heavyBreath = false;
    m_expressionRequest = 100;
    m_expressionSpeed = 1;
    m_expressionTime = 0;
    m_expressionDuration = 0;
    // (Only when the camera is not near the body, as in the original.)
    I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera();
    const S_vector delta = m_owner->GetFrame()->GetWorldPos() - camera->GetWorldPos();
    const float square = delta.Square();
    const float distance = square < 1e-8f ? 0.0f : std::fabs(square - 1.0f) < 1e-8f ? 1.0f : std::sqrt(square);
    if (distance <= (2.4f - camera->GetFOV()) * 3.0f)
        return;
    for (int channel = 0; channel < MORPH_CHANNELS; ++channel) {
        if (!flags.channels[channel])
            continue;
        for (int i = 0; i < targets; ++i)
            morph->SetWeight(channel, i, weights.weights[channel][i]);
    }
    // The eyes turned a little about their x axes.
    I3D_frame *leftEye = m_owner->GetFrame()->FindChildFrame("Leye", FRAME_MASK_ANY);
    I3D_frame *rightEye = m_owner->GetFrame()->FindChildFrame("Reye", FRAME_MASK_ANY);
    const float angle = RandFloat() * 0.05f + 0.025f;
    for (I3D_frame *eye : {leftEye, rightEye}) {
        if (!eye)
            continue;
        const S_matrix local = eye->GetLocalMatrix();
        S_quat rot;
        rot.Make(local(0), angle);
        eye->SetLocalMatrix(local * rot.RotationMatrix());
        eye->Update();
    }
}

// ------------------------------------------------------------------ per frame

void C_HumanMorph::Apply(bool force)
{
    I3D_morph *morph = I3D_GetMorph(m_morphVisual);
    // (The state indices carry over from one channel to the next when a state is unknown.)
    int to = MORPH_STATES;
    int from = MORPH_STATES;
    const int base = m_group * MORPH_STATES;
    // Blends a channel between the states `to` and `from`; false: a state does not exist
    // (the original then stops).
    const auto blend = [&](int channel, float t, bool needDifferent) {
        const S_MorphWeights &a = g_morphWeights[base + to];
        const S_MorphWeights &b = g_morphWeights[base + from];
        if (a.valid != 1 || b.valid != 1)
            return false;
        if (needDifferent && to == from)
            return true;
        const int targets = morph->NumTargets();
        if (!g_morphFlags[base + to].channels[channel] || !g_morphFlags[base + from].channels[channel])
            return true;
        for (int i = 0; i < targets; ++i)
            morph->SetWeight(channel, i, t * a.weights[channel][i] + (1.0f - t) * b.weights[channel][i]);
        return true;
    };

    // The eyes.
    if (m_blinkDuration != 0 || force) {
        const float t = m_blinkDuration != 0 ? BlendFactor(m_blinkTime, m_blinkDuration) : 1.0f;
        to = EyeState(m_blinkState, to);
        from = EyeState(m_blinkPrevious, from);
        if (!blend(MORPH_CHANNEL_EYES, t, false))
            return;
    }
    // Breathing.
    if (m_breathDuration != 0 || force) {
        const float t = m_breathDuration != 0 ? BlendFactor(m_breathTime, m_breathDuration) : 1.0f;
        const bool resting = m_exhaustTime < 1 && m_exhaustion < 1;
        switch (m_breathState) {
        case 0:
            to = 1;
            break;
        case 1:
            to = resting ? 0 : m_heavyBreath ? 2 : 3;
            break;
        case 2:
            to = 4;
            break;
        case 3:
            to = 5;
            break;
        case 4:
            to = 6;
            break;
        default:
            break;
        }
        switch (m_breathPrevious) {
        case 0:
            from = 1;
            break;
        case 1:
            from = resting ? 0 : 2;
            break;
        case 2:
            from = 4;
            break;
        case 3:
            from = 5;
            break;
        case 4:
            from = 6;
            break;
        default:
            break;
        }
        if (!blend(MORPH_CHANNEL_BREATH, t, true))
            return;
    }
    // The grip of the hand.
    if (m_gripDuration != 0 || force) {
        const float t = m_gripDuration != 0 ? BlendFactor(m_gripTime, m_gripDuration) : 1.0f;
        to = GripState(m_gripState, 9, to);
        from = GripState(m_gripPrevious, 9, from);
        if (!blend(MORPH_CHANNEL_GRIP, t, false))
            return;
    }
    // The grip of the other hand.
    if (m_grip2Duration != 0 || force) {
        const float t = m_grip2Duration != 0 ? BlendFactor(m_grip2Time, m_grip2Duration) : 1.0f;
        to = GripState(m_grip2State, 10, to);
        from = GripState(m_grip2Previous, 10, from);
        blend(MORPH_CHANNEL_GRIP2, t, false);
    }
}

void C_HumanMorph::Update(int time)
{
    if (m_blinkDuration > 0) {
        m_dirty = true;
        m_blinkTime += time;
    }
    // A blink every 4.2 .. 5.4 s (unless one or an expression is under way).
    if (m_blinkTimer != -1 && (m_blinkTimer -= time) < 1) {
        if (m_blinkDuration == 0 && m_expressionDuration == 0) {
            m_blinkTime = 0;
            m_blinkTimer = std::rand() % BLINK_RANDOM + BLINK_INTERVAL;
            m_blinkDuration = BLINK_DURATION;
            m_blinkPrevious = m_blinkState;
            m_blinkState = 0;
            m_dirty = true;
        } else {
            m_blinkTimer = 500;
        }
    }

    // Exhaustion and panting.
    const int running = m_exhaustTime;
    if (running < 0) {
        if (m_exhaustion > 0) {
            // Settling: down toward what the stamina says, then by 1.3 per ms.
            const int stamina = m_owner->GetStaminaPercent();
            bool decay = true;
            if (stamina < 100 && static_cast<float>(m_exhaustion) >= PANT_THRESHOLD) {
                const float percent = (static_cast<float>(m_exhaustion) - PANT_THRESHOLD) * 100.0f /
                                      (static_cast<float>(MAX_EXHAUSTION) - PANT_THRESHOLD);
                decay = false;
                if (static_cast<float>(100 - m_owner->GetStaminaPercent()) <= percent) {
                    const float level = 1.0f - static_cast<float>(m_owner->GetStaminaPercent()) * 0.01f;
                    m_exhaustion = static_cast<int>(level * (static_cast<float>(MAX_EXHAUSTION) - PANT_THRESHOLD) +
                                                    PANT_THRESHOLD);
                }
            }
            if (decay)
                m_exhaustion = static_cast<int>(static_cast<float>(m_exhaustion) - static_cast<float>(time) * 1.3f);
            if (m_breathState == 1 && static_cast<float>(m_exhaustion) <= PANT_THRESHOLD) {
                m_heavyBreath = false;
                m_breathTime = 0;
                m_breathPrevious = 1;
                m_breathState = 0;
                m_breathDuration = BREATH_BLEND_TIME;
            }
        }
    } else {
        // Running: panting starts when the threshold is crossed.
        int crossed = static_cast<float>(running) < PANT_THRESHOLD ? 0 : -1;
        m_exhaustTime = running + time;
        if (static_cast<float>(m_exhaustTime) >= PANT_THRESHOLD)
            ++crossed;
        if (crossed == 1 && m_breathDuration == 0) {
            m_breathTime = 0;
            m_breathPrevious = m_breathState;
            m_breathState = 1;
            m_breathDuration = BREATH_BLEND_TIME;
        }
    }
    if (m_breathDuration > 0) {
        m_dirty = true;
        m_breathTime += time;
    }
    if (m_expressionDuration > 0)
        m_expressionTime += time;

    // An expression requested: the breath and eye channels take it for a while.
    if (m_breathDuration == 0 && m_expressionDuration == 0 && m_expressionRequest != 100) {
        const int expression = m_expressionRequest;
        m_breathTime = 0;
        m_breathDuration = EXPRESSION_BLEND_TIME;
        m_expressionTime = 0;
        switch (m_expressionSpeed) {
        case 0:
            m_expressionDuration = m_breathDuration + 400;
            break;
        case 1:
            m_expressionDuration = m_breathDuration + 700;
            break;
        case 2:
            m_expressionDuration = m_breathDuration + 1200;
            break;
        case 3:
            m_expressionDuration = m_breathDuration + 2200;
            break;
        default:
            break;
        }
        m_breathPrevious = m_breathState;
        m_breathState = expression;
        if (m_blinkDuration == 0) {
            m_blinkTime = 0;
            m_blinkDuration = m_breathDuration;
            m_blinkPrevious = m_blinkState;
        }
        if (expression == 2)
            m_blinkState = 3;
        else if (expression == 3)
            m_blinkState = 4;
        else if (expression == 4)
            m_blinkState = 5;
        m_expressionRequest = 100;
    }

    // The grips.
    if (m_gripDuration > 0) {
        m_dirty = true;
        m_gripTime += time;
    }
    const int grip2Duration = m_grip2Duration;
    if (grip2Duration > 0) {
        m_dirty = true;
        m_grip2Time += time;
    }
    if (m_gripRequest != 100 && m_gripDuration == 0) {
        m_gripTime = 0;
        m_gripDuration = GRIP_BLEND_TIME;
        m_gripPrevious = m_gripState;
        m_gripState = m_gripRequest;
        m_gripRequest = 100;
    }
    if (m_grip2Request != 100 && grip2Duration == 0) {
        m_grip2Time = 0;
        m_grip2Previous = m_grip2State;
        m_grip2Duration = GRIP_BLEND_TIME;
        m_grip2State = m_grip2Request;
        m_grip2Request = 100;
    }

    if (m_dirty && m_morphVisual && (m_owner->GetMovement()->m_flags & MOVE_FLAG_LARGE)) {
        m_dirty = false;
        Apply(false);
    }
    UpdateBreathSound(time);

    // Ends of the blends.
    if (m_blinkDuration <= m_blinkTime && m_blinkDuration > 0) {
        m_blinkTime = 0;
        m_blinkDuration = 0;
        if (m_blinkState == 0) {
            // The eyes open again.
            m_blinkPrevious = 0;
            m_blinkState = 1;
            m_blinkTime = 0;
            m_blinkDuration = BLINK_DURATION;
        }
    }
    if (m_breathDuration <= m_breathTime && m_breathDuration > 0) {
        m_breathTime = 0;
        m_breathDuration = 0;
    }
    if (m_expressionDuration <= m_expressionTime && m_expressionDuration != 0) {
        if (m_breathDuration == 0) {
            m_expressionDuration = 0;
            m_expressionTime = 0;
            m_breathTime = 0;
            m_breathPrevious = m_breathState;
            m_breathDuration = EXPRESSION_BLEND_TIME;
            m_breathState = 0;
        }
        if (m_blinkDuration == 0) {
            m_blinkTime = 0;
            m_blinkDuration = m_breathDuration;
            m_blinkPrevious = m_blinkState;
            m_blinkState = 1;
        }
    }
    if (m_gripDuration <= m_gripTime && m_gripDuration > 0) {
        m_gripTime = 0;
        m_gripDuration = 0;
    }
    if (m_grip2Duration <= m_grip2Time && m_grip2Duration > 0) {
        m_grip2Time = 0;
        m_grip2Duration = 0;
    }
}

void C_HumanMorph::UpdateBreathSound(int time)
{
#ifdef HD2_DEDICATED   // no sounds on the server (its copy still makes an unused sound request)
    (void)time;
#else
    if (!IsClient())
        return;
    const int exhaustion = m_exhaustTime == -1 ? m_exhaustion : m_exhaustTime;
    const int over = static_cast<int>(static_cast<float>(exhaustion) - PANT_THRESHOLD);
    const float level = static_cast<float>(over) / (static_cast<float>(MAX_EXHAUSTION) - PANT_THRESHOLD);
    if (m_breathSoundTime >= 0)
        m_breathSoundTime -= time;
    // Only the active player pants audibly.
    if (m_breathSound &&
        !(over > 0 && g_teamManager.GetPlayerTeam()->GetActivePlayer() == m_owner && !m_owner->IsPlayerHoldingBreath())) {
        if (m_breathSoundTime > 0)
            return;
        GetMovementSounds()->Return(m_breathSound);
        m_breathSound = nullptr;
        return;
    }
    if (over <= 0)
        return;
    const auto start = [this, level]() {
        m_heavyBreath = level >= 0.8f;
        if (m_owner->IsLocalPlayer()) {
            const S_vector offset(0.0f, 0.0f, 0.3f);
            m_breathSound = GetMovementSounds()->Play(m_heavyBreath ? SOUND_PANT_HEAVY : SOUND_PANT, m_head, &offset);
        }
    };
    if (!m_breathSound) {
        start();
        if (m_breathSound) {
            m_breathSoundTime = m_breathSound->GetLength();
            m_breathVolume = m_breathSound->GetVolumeInline();
            m_breathSound->SetVolume(0.0f);
        }
        return;
    }
    if (m_breathSoundTime < 1) {
        // The next breath.
        GetMovementSounds()->Return(m_breathSound);
        m_breathSound = nullptr;
        start();
        if (!m_breathSound)
            return;
        m_breathSoundTime = m_breathSound->GetLength();
        m_breathVolume = m_breathSound->GetVolumeInline();
    }
    // Quieter while only a little exhausted.
    m_breathSound->SetVolume(level < 0.2f ? level * m_breathVolume * 5.0f : m_breathVolume);
#endif
}

// ------------------------------------------------------------------ saved games

namespace {

constexpr uint16_t CHUNK_MORPH = 0xB0;

}  // namespace

bool C_HumanMorph::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_MORPH))
        return false;
    chunk.WriteChunk(0x00, static_cast<int>(m_dirty));
    chunk.WriteChunk(0x01, m_blinkState);
    chunk.WriteChunk(0x02, m_blinkPrevious);
    chunk.WriteChunk(0x03, m_blinkTime);
    chunk.WriteChunk(0x04, m_blinkDuration);
    chunk.WriteChunk(0x05, m_blinkTimer);
    chunk.WriteChunk(0x06, m_breathState);
    chunk.WriteChunk(0x07, m_breathPrevious);
    chunk.WriteChunk(0x08, m_breathTime);
    chunk.WriteChunk(0x09, m_breathDuration);
    chunk.WriteChunk(0x0A, m_exhaustTime);
    chunk.WriteChunk(0x0B, m_exhaustion);
    chunk.WriteChunk(0x0C, static_cast<int>(m_heavyBreath));
    chunk.WriteChunk(0x0D, m_expressionTime);
    chunk.WriteChunk(0x0E, m_expressionDuration);
    chunk.WriteChunk(0x0F, m_expressionRequest);
    chunk.WriteChunk(0x10, m_expressionSpeed);
    chunk.WriteChunk(0x11, m_gripState);
    chunk.WriteChunk(0x12, m_gripPrevious);
    chunk.WriteChunk(0x13, m_gripTime);
    chunk.WriteChunk(0x14, m_gripDuration);
    chunk.WriteChunk(0x15, m_gripRequest);
    chunk.WriteChunk(0x16, m_grip2State);
    chunk.WriteChunk(0x17, m_grip2Previous);
    chunk.WriteChunk(0x18, m_grip2Time);
    chunk.WriteChunk(0x19, m_grip2Duration);
    chunk.WriteChunk(0x1A, m_grip2Request);
    chunk.WriteChunk(0x1B, m_breathSoundTime);
    chunk.WriteChunk(0x1C, m_breathVolume);
    chunk.Ascend();
    return true;
}

bool C_HumanMorph::LoadGame(C_chunk &chunk)
{
    if (m_breathSound) {
        GetMovementSounds()->Return(m_breathSound);
        m_breathSound = nullptr;
    }
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_MORPH) {
        chunk.Ascend();
        return false;
    }
    int *const fields[] = {
        nullptr, &m_blinkState, &m_blinkPrevious, &m_blinkTime, &m_blinkDuration, &m_blinkTimer, &m_breathState,
        &m_breathPrevious, &m_breathTime, &m_breathDuration, &m_exhaustTime, &m_exhaustion, nullptr,
        &m_expressionTime, &m_expressionDuration, &m_expressionRequest, &m_expressionSpeed, &m_gripState,
        &m_gripPrevious, &m_gripTime, &m_gripDuration, &m_gripRequest, &m_grip2State, &m_grip2Previous,
        &m_grip2Time, &m_grip2Duration, &m_grip2Request, &m_breathSoundTime,
    };
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        int value = 0;
        if (id == 0x00) {
            chunk.Read(value);
            m_dirty = value != 0;
        } else if (id == 0x0C) {
            chunk.Read(value);
            m_heavyBreath = value != 0;
        } else if (id == 0x1C) {
            chunk.Read(m_breathVolume);
        } else if (id < sizeof(fields) / sizeof(fields[0])) {
            chunk.Read(*fields[id]);
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}
