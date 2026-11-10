// JoyEffect: one force-feedback effect of a joystick.
#include "ijoy_internal.h"

#include <cstring>

namespace {

const float kCentidegreesPerRadian = 18000.0f;  // times 1/pi
const float kInvPi = 0.318309886f;
const float kDirectInputScale = 10000.0f;     // DI_FFNOMINALMAX
const DWORD kDefaultEnvelopeTime = 500;       // microseconds
const DWORD kDefaultPeriod = 500000;          // microseconds
const DWORD kQuarterPeriod = 9000;            // phase, hundredths of a degree
const DWORD kHalfPeriod = 18000;

// DIEnumEffectsInFileCallback (0x10005cf0): creates each effect of the file, pushing it in front.
struct FileEnumContext {
    JoyEffect::FileEffect *effects;
    IDirectInputDevice7A *device;
    int count;
};

BOOL CALLBACK AddFileEffect(const DIFILEEFFECT *fileEffect, void *context)
{
    FileEnumContext *ctx = static_cast<FileEnumContext *>(context);
    JoyEffect::FileEffect *node = new JoyEffect::FileEffect;
    if (FAILED(ctx->device->CreateEffect(fileEffect->GuidEffect, fileEffect->lpDiEffect, &node->effect,
                                         nullptr))) {
        delete node;
        return DIENUM_CONTINUE;
    }
    node->next = ctx->effects;
    ctx->effects = node;
    ++ctx->count;
    return DIENUM_CONTINUE;
}

}  // namespace

// 0x100042e0
JoyEffect::JoyEffect(Joy *joy, IDirectInputDevice7A *device, EffectSlot *slots, int numSlots, bool *ok)
{
    refCount_ = 0;
    *ok = false;
    joy_ = joy;
    device_ = device;
    slots_ = slots;
    effect_ = nullptr;
    fileEffects_ = nullptr;
    numEffects_ = 0;
    numSlots_ = numSlots;
    std::memset(&params_, 0, sizeof(params_));
    std::memset(&envelope_, 0, sizeof(envelope_));
    constant_ = {};
    ramp_ = {};
    periodic_ = {};
    std::memset(condition_, 0, sizeof(condition_));
    std::memset(axes_, 0, sizeof(axes_));
    numAxes_ = 0;
    ResetPendingAxes();
    for (LONG &d : direction_)
        d = 0;
    type_ = IJOY_EFFECT_NONE;
    ++g_leaks.effects;
    joy_->AddRef();
    AddRef();
    *ok = true;
}

// 0x100043f0
JoyEffect::~JoyEffect()
{
    while (FileEffect *node = fileEffects_) {
        FileEffect *next = node->next;
        if (node->effect)
            node->effect->Release();
        delete node;
        fileEffects_ = next;
    }
    if (effect_) {
        effect_->Release();
        effect_ = nullptr;
    }
    --g_leaks.effects;
    joy_->Release();
}

// 0x100044c0
int JoyEffect::AddRef()
{
    ++g_leaks.effectRefs;
    return ++refCount_;
}

// 0x10004490
int JoyEffect::Release()
{
    int count = --refCount_;
    --g_leaks.effectRefs;
    if (refCount_ == 0)
        delete this;
    return count;
}

void JoyEffect::ResetPendingAxes()
{
    pendingAxes_[0] = AxisOffset(IJOY_AXIS_X);
    pendingAxes_[1] = AxisOffset(IJOY_AXIS_Y);
    numPendingAxes_ = 2;
}

void JoyEffect::ReleaseEffects()
{
    if (effect_) {
        effect_->Release();
        effect_ = nullptr;
    }
    while (FileEffect *node = fileEffects_) {
        FileEffect *next = node->next;
        if (node->effect)
            node->effect->Release();
        delete node;
        fileEffects_ = next;
    }
}

// Sends `parameter` to the playing effect if the device can change it while playing.
bool JoyEffect::Apply(DWORD parameter)
{
    if (!(slots_[type_].info.dwDynamicParams & parameter))
        return false;
    return SUCCEEDED(effect_->SetParameters(&params_, DIEP_NORESTART | parameter));
}

// Unloads the effect and stores `parameter` without downloading, if the effect takes it at all.
bool JoyEffect::Commit(DWORD parameter)
{
    if (!Playable())
        return false;
    effect_->Unload();
    if (!(slots_[type_].info.dwStaticParams & parameter))
        return false;
    return SUCCEEDED(effect_->SetParameters(&params_, DIEP_NODOWNLOAD | parameter));
}

// 0x10004980
bool JoyEffect::Start(DWORD iterations, bool solo)
{
    if (type_ == IJOY_EFFECT_NONE)
        return false;
    if (type_ == IJOY_EFFECT_FILE) {
        // Download all, then start all; on a failure undo the ones done so far.
        int done = 0;
        for (FileEffect *node = fileEffects_; node; node = node->next, ++done) {
            if (FAILED(node->effect->Download())) {
                for (FileEffect *undo = fileEffects_; done > 0; undo = undo->next, --done)
                    undo->effect->Unload();
                return false;
            }
        }
        done = 0;
        for (FileEffect *node = fileEffects_; node; node = node->next, ++done) {
            if (FAILED(node->effect->Start(1, 0))) {
                for (FileEffect *undo = fileEffects_; done > 0; undo = undo->next, --done)
                    undo->effect->Unload();
                return false;
            }
        }
        return true;
    }
    if (iterations == 0)
        iterations = 1;
    return SUCCEEDED(effect_->Start(iterations, solo ? DIES_SOLO : 0));
}

// 0x10004a60
bool JoyEffect::Stop(bool unload)
{
    if (type_ == IJOY_EFFECT_NONE)
        return false;
    if (type_ == IJOY_EFFECT_FILE) {
        for (FileEffect *node = fileEffects_; node; node = node->next) {
            if (FAILED(node->effect->Stop()))
                return false;
            if (unload)
                node->effect->Unload();
        }
        return true;
    }
    if (FAILED(effect_->Stop()))
        return false;
    if (unload)
        effect_->Unload();
    return true;
}

// 0x100045e0
bool JoyEffect::Create(int type, float direction, DWORD duration, float gain, const char *fileName,
                       int allowEmptyFile)
{
    type_ = IJOY_EFFECT_NONE;
    ReleaseEffects();
    numEffects_ = 0;
    if (type == IJOY_EFFECT_NONE)
        return true;

    if (type == IJOY_EFFECT_FILE) {
        FileEnumContext ctx = {nullptr, device_, 0};
        if (fileName) {
            if (FAILED(device_->EnumEffectsInFile(fileName, AddFileEffect, &ctx, DIFEF_MODIFYIFNEEDED))) {
                fileEffects_ = ctx.effects;
                ReleaseEffects();
                return false;
            }
        } else if (!allowEmptyFile) {
            return false;
        }
        fileEffects_ = ctx.effects;
        numEffects_ = ctx.count;
        type_ = IJOY_EFFECT_FILE;
        return true;
    }

    if (type >= numSlots_ || !slots_[type].supported) {
        ResetPendingAxes();
        return false;
    }
    for (DWORD i = 0; i < numPendingAxes_; ++i)
        axes_[i] = pendingAxes_[i];
    numAxes_ = numPendingAxes_;
    ResetPendingAxes();
    direction_[0] = FloatToLong(direction * kCentidegreesPerRadian * kInvPi);
    gain = Clamp(gain, 0.0f, 1.0f);

    condition_[0] = {0, 0, 0, DI_FFNOMINALMAX, DI_FFNOMINALMAX, 0};
    condition_[1] = {0, 0, 0, DI_FFNOMINALMAX, DI_FFNOMINALMAX, 0};
    envelope_.dwSize = sizeof(DIENVELOPE);
    envelope_.dwAttackLevel = 0;
    envelope_.dwAttackTime = kDefaultEnvelopeTime;
    envelope_.dwFadeLevel = 0;
    envelope_.dwFadeTime = kDefaultEnvelopeTime;
    constant_.lMagnitude = 0;
    ramp_.lStart = 0;
    ramp_.lEnd = 0;
    periodic_.dwMagnitude = 0;
    periodic_.lOffset = 0;
    periodic_.dwPhase = 0;
    periodic_.dwPeriod = kDefaultPeriod;

    params_.dwSize = sizeof(DIEFFECT);
    params_.dwFlags = DIEFF_OBJECTOFFSETS | DIEFF_POLAR;
    params_.dwDuration = duration != 0 ? duration : INFINITE;
    params_.dwSamplePeriod = 0;
    params_.dwGain = FloatToLong(gain * kDirectInputScale);
    params_.dwTriggerButton = DIEB_NOTRIGGER;
    params_.dwTriggerRepeatInterval = 0;
    params_.cAxes = numAxes_;
    params_.rgdwAxes = axes_;
    params_.rglDirection = direction_;
    params_.lpEnvelope = nullptr;
    switch (type) {
    case IJOY_EFFECT_CONSTANT:
        params_.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        params_.lpvTypeSpecificParams = &constant_;
        break;
    case IJOY_EFFECT_RAMP:
        if (duration == 0) {  // a ramp cannot be infinite
            ResetPendingAxes();
            return false;
        }
        params_.cbTypeSpecificParams = sizeof(DIRAMPFORCE);
        params_.lpvTypeSpecificParams = &ramp_;
        break;
    case IJOY_EFFECT_SQUARE:
    case IJOY_EFFECT_SINE:
    case IJOY_EFFECT_TRIANGLE:
    case IJOY_EFFECT_SAWTOOTH_UP:
    case IJOY_EFFECT_SAWTOOTH_DOWN:
        params_.cbTypeSpecificParams = sizeof(DIPERIODIC);
        params_.lpvTypeSpecificParams = &periodic_;
        break;
    case IJOY_EFFECT_SPRING:
    case IJOY_EFFECT_DAMPER:
    case IJOY_EFFECT_INERTIA:
    case IJOY_EFFECT_FRICTION:
        params_.cbTypeSpecificParams = sizeof(condition_);
        params_.lpvTypeSpecificParams = condition_;
        break;
    }
    if (FAILED(device_->CreateEffect(slots_[type].info.guid, &params_, &effect_, nullptr))) {
        type_ = IJOY_EFFECT_NONE;
        if (effect_) {
            effect_->Release();
            effect_ = nullptr;
        }
        ResetPendingAxes();
        return false;
    }
    type_ = type;
    numEffects_ = 1;
    return true;
}

// 0x100044e0
bool JoyEffect::SetAxes(int count, const BYTE *axes)
{
    if (type_ == IJOY_EFFECT_FILE || count > IJOY_NUM_AXES || count < 0)
        return false;
    if (count == 0) {
        ResetPendingAxes();
        return true;
    }
    for (int i = 0; i < count; ++i) {
        if (axes[i] >= IJOY_NUM_AXES) {
            // Leaves numPendingAxes_ as it was.
            pendingAxes_[0] = AxisOffset(IJOY_AXIS_X);
            pendingAxes_[1] = AxisOffset(IJOY_AXIS_Y);
            return false;
        }
        pendingAxes_[i] = AxisOffset(axes[i]);
    }
    numPendingAxes_ = count;
    return true;
}

// 0x10004bd0
bool JoyEffect::SetGain(float gain, bool apply)
{
    if (!Playable())
        return false;
    params_.dwSize = sizeof(DIEFFECT);
    params_.dwGain = FloatToLong(Clamp(gain, 0.0f, 1.0f) * kDirectInputScale);
    return !apply || Apply(DIEP_GAIN);
}

// 0x10004c80
bool JoyEffect::CommitGain()
{
    return Commit(DIEP_GAIN);
}

// 0x10004ae0
bool JoyEffect::SetDirection(float radians, bool apply)
{
    if (!Playable())
        return false;
    direction_[0] = FloatToLong(radians * kCentidegreesPerRadian * kInvPi);
    params_.cAxes = numAxes_;
    params_.dwSize = sizeof(DIEFFECT);
    params_.dwFlags = DIEFF_OBJECTOFFSETS | DIEFF_POLAR;
    params_.rgdwAxes = axes_;
    params_.rglDirection = direction_;
    return !apply || Apply(DIEP_DIRECTION);
}

// 0x10004b80
bool JoyEffect::CommitDirection()
{
    return Commit(DIEP_DIRECTION);
}

// 0x10004cd0
bool JoyEffect::SetDuration(DWORD microseconds, bool apply)
{
    if (!Playable())
        return false;
    params_.dwSize = sizeof(DIEFFECT);
    if (microseconds == 0) {
        if (type_ == IJOY_EFFECT_RAMP)
            return false;
        params_.dwDuration = INFINITE;
    } else {
        params_.dwDuration = microseconds;
    }
    return !apply || Apply(DIEP_DURATION);
}

// 0x10004d50
bool JoyEffect::CommitDuration()
{
    return Commit(DIEP_DURATION);
}

// 0x10004da0
bool JoyEffect::SetEnvelope(float attackLevel, DWORD attackTime, float fadeLevel, DWORD fadeTime, bool apply)
{
    if (!Playable())
        return false;
    attackLevel = Clamp(attackLevel, 0.0f, 1.0f);
    fadeLevel = Clamp(fadeLevel, 0.0f, 1.0f);
    params_.lpEnvelope = &envelope_;
    params_.dwSize = sizeof(DIEFFECT);
    envelope_.dwSize = sizeof(DIENVELOPE);
    envelope_.dwAttackLevel = FloatToLong(attackLevel * kDirectInputScale);
    envelope_.dwAttackTime = attackTime;
    envelope_.dwFadeLevel = FloatToLong(fadeLevel * kDirectInputScale);
    envelope_.dwFadeTime = fadeTime;
    return !apply || Apply(DIEP_ENVELOPE);
}

// 0x10004eb0
bool JoyEffect::CommitEnvelope()
{
    return Commit(DIEP_ENVELOPE);
}

// 0x10004f00
bool JoyEffect::SetConstantForce(float magnitude, bool apply)
{
    if (!Playable() || type_ != IJOY_EFFECT_CONSTANT)
        return false;
    constant_.lMagnitude = FloatToLong(Clamp(magnitude, -1.0f, 1.0f) * kDirectInputScale);
    params_.lpvTypeSpecificParams = &constant_;
    params_.dwSize = sizeof(DIEFFECT);
    params_.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
    return !apply || Apply(DIEP_TYPESPECIFICPARAMS);
}

// 0x10005000
bool JoyEffect::SetRampForce(float start, float end, bool apply)
{
    if (!Playable() || type_ != IJOY_EFFECT_RAMP)
        return false;
    start = Clamp(start, -1.0f, 1.0f);
    end = Clamp(end, -1.0f, 1.0f);
    ramp_.lStart = FloatToLong(start * kDirectInputScale);
    ramp_.lEnd = FloatToLong(end * kDirectInputScale);
    params_.lpvTypeSpecificParams = &ramp_;
    params_.dwSize = sizeof(DIEFFECT);
    params_.cbTypeSpecificParams = sizeof(DIRAMPFORCE);
    return !apply || Apply(DIEP_TYPESPECIFICPARAMS);
}

// 0x10005100
bool JoyEffect::SetPeriodic(float magnitude, float offset, float phaseShift, DWORD period, bool apply)
{
    if (!Playable())
        return false;
    if (type_ < IJOY_EFFECT_SQUARE || type_ > IJOY_EFFECT_SAWTOOTH_DOWN)
        return false;
    DWORD phase = 0;
    if (FloatToLong(phaseShift) != 0)
        phase = type_ == IJOY_EFFECT_SINE ? kQuarterPeriod : kHalfPeriod;
    magnitude = Clamp(magnitude, 0.0f, 1.0f);
    offset = Clamp(offset, -1.0f, 1.0f);
    periodic_.dwMagnitude = FloatToLong(magnitude * kDirectInputScale);
    periodic_.lOffset = FloatToLong(offset * kDirectInputScale);
    params_.lpvTypeSpecificParams = &periodic_;
    periodic_.dwPeriod = period;
    periodic_.dwPhase = phase;
    params_.dwSize = sizeof(DIEFFECT);
    params_.cbTypeSpecificParams = sizeof(DIPERIODIC);
    return !apply || Apply(DIEP_TYPESPECIFICPARAMS);
}

// 0x10005260. The original clamps in two global scratch copies (0x1000fe80); locals here.
bool JoyEffect::SetCondition(const IJoyCondition *first, const IJoyCondition *second, bool apply)
{
    if (!Playable())
        return false;
    if (type_ != IJOY_EFFECT_SPRING && type_ != IJOY_EFFECT_DAMPER && type_ != IJOY_EFFECT_FRICTION &&
        type_ != IJOY_EFFECT_INERTIA)
        return false;
    const IJoyCondition *in[2] = {first, second};
    for (int i = 0; i < 2; ++i) {
        IJoyCondition c = *in[i];
        c.offset = Clamp(c.offset, -1.0f, 1.0f);
        c.positiveCoefficient = Clamp(c.positiveCoefficient, -1.0f, 1.0f);
        c.negativeCoefficient = Clamp(c.negativeCoefficient, -1.0f, 1.0f);
        c.positiveSaturation = Clamp(c.positiveSaturation, 0.0f, 1.0f);
        c.negativeSaturation = Clamp(c.negativeSaturation, 0.0f, 1.0f);
        c.deadBand = Clamp(c.deadBand, 0.0f, 1.0f);
        condition_[i].lOffset = FloatToLong(c.offset * kDirectInputScale);
        condition_[i].lPositiveCoefficient = FloatToLong(c.positiveCoefficient * kDirectInputScale);
        condition_[i].lNegativeCoefficient = FloatToLong(c.negativeCoefficient * kDirectInputScale);
        condition_[i].dwPositiveSaturation = FloatToLong(c.positiveSaturation * kDirectInputScale);
        condition_[i].dwNegativeSaturation = FloatToLong(c.negativeSaturation * kDirectInputScale);
        condition_[i].lDeadBand = FloatToLong(c.deadBand * kDirectInputScale);
    }
    params_.cbTypeSpecificParams = sizeof(condition_);
    params_.dwSize = sizeof(DIEFFECT);
    params_.lpvTypeSpecificParams = condition_;
    return !apply || Apply(DIEP_TYPESPECIFICPARAMS);
}

// 0x10004fb0
bool JoyEffect::CommitTypeParams()
{
    return Commit(DIEP_TYPESPECIFICPARAMS);
}

// 0x10005700. `type` is any standard effect type, not this effect's.
bool JoyEffect::GetEffectCaps(IJoyEffectCaps *caps, int type)
{
    if (!caps)
        return false;
    caps->supported = caps->deadBand = caps->saturation = caps->posNegSaturation = false;
    if (type_ == IJOY_EFFECT_FILE || type >= numSlots_ || type < 0)
        return false;
    const EffectSlot &slot = slots_[type];
    caps->supported = slot.supported;
    if (!slot.supported)
        return false;
    DWORD effType = slot.info.dwEffType;
    caps->deadBand = (effType & DIEFT_DEADBAND) != 0;
    caps->saturation = (effType & DIEFT_SATURATION) != 0;
    caps->posNegSaturation = (effType & DIEFT_POSNEGSATURATION) != 0;
    caps->posNegCoefficients = (effType & DIEFT_POSNEGCOEFFICIENTS) != 0;
    DWORD s = slot.info.dwStaticParams;
    caps->staticDirection = (s & DIEP_DIRECTION) != 0;
    caps->staticDuration = (s & DIEP_DURATION) != 0;
    caps->staticEnvelope = (s & DIEP_ENVELOPE) != 0;
    caps->staticGain = (s & DIEP_GAIN) != 0;
    caps->staticTypeParams = (s & DIEP_TYPESPECIFICPARAMS) != 0;
    DWORD d = slot.info.dwDynamicParams;
    caps->dynamicDirection = (d & DIEP_DIRECTION) != 0;
    caps->dynamicDuration = (d & DIEP_DURATION) != 0;
    caps->dynamicEnvelope = (d & DIEP_ENVELOPE) != 0;
    caps->dynamicGain = (d & DIEP_GAIN) != 0;
    caps->dynamicTypeParams = (d & DIEP_TYPESPECIFICPARAMS) != 0;
    return true;
}

// 0x10005920
int JoyEffect::GetType()
{
    return type_;
}

// 0x10005840. For a file the original asks effect_ (null then) once per file effect instead of
// each file effect; kept.
bool JoyEffect::GetStatus(DWORD *status)
{
    if (!status)
        return false;
    *status = 0;
    DWORD flags = 0;
    if (type_ == IJOY_EFFECT_FILE) {
        bool playing = false;
        bool emulated = false;
        for (FileEffect *node = fileEffects_; node; node = node->next) {
            if (FAILED(effect_->GetEffectStatus(&flags)))
                return false;
            if (flags & DIEGES_PLAYING)
                playing = true;
            if (flags & DIEGES_EMULATED)
                emulated = true;
        }
        if (playing)
            *status |= IJOY_EFFECT_PLAYING;
        if (emulated)
            *status |= IJOY_EFFECT_EMULATED;
        return true;
    }
    if (FAILED(effect_->GetEffectStatus(&flags)))
        return false;
    if (flags & DIEGES_PLAYING)
        *status |= IJOY_EFFECT_PLAYING;
    if (flags & DIEGES_EMULATED)
        *status |= IJOY_EFFECT_EMULATED;
    return true;
}
