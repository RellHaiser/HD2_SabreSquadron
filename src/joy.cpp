// Joy: one opened joystick.
#include "ijoy_internal.h"

#include <cmath>
#include <cstring>

namespace {

const float kPi = 3.14159265f;
const float kRadiansPerCentidegree = 1.0f / 18000.0f;  // times pi
const LONG kRangeMin = -10000;
const LONG kRangeMax = 10000;

// DIEnumEffectsCallback (0x10005ab0): records what the device supports of each standard effect.
BOOL CALLBACK AddEffectInfo(const DIEFFECTINFOA *info, void *context)
{
    static const GUID *const kTypes[IJOY_NUM_STANDARD_EFFECTS] = {
        &GUID_ConstantForce, &GUID_RampForce, &GUID_Square, &GUID_Sine, &GUID_Triangle,
        &GUID_SawtoothUp, &GUID_SawtoothDown, &GUID_Spring, &GUID_Damper, &GUID_Inertia,
        &GUID_Friction,
    };
    EffectSlot *slots = static_cast<EffectSlot *>(context);
    for (int i = 0; i < IJOY_NUM_STANDARD_EFFECTS; ++i) {
        if (info->guid == *kTypes[i]) {
            slots[i].info = *info;
            slots[i].supported = true;
            break;
        }
    }
    return DIENUM_CONTINUE;
}

}  // namespace

// 0x10001a80
Joy::Joy(JoyBase *base, DeviceEntry *entry, DeviceList *list, DWORD flags, bool *ok)
{
    refCount_ = 0;
    *ok = false;
    base_ = base;
    entry_ = entry;
    list_ = list;
    hwnd_ = list->hwnd;
    dinput_ = list->directInput;
    device_ = nullptr;
    std::memset(&raw_, 0, sizeof(raw_));
    for (DWORD &armed : buttonArmed_)
        armed = 0xFFFFFFFF;
    acquired_ = true;
    forceFeedback_ = false;
    std::memset(&state_, 0, sizeof(state_));
    std::memset(&previous_, 0, sizeof(previous_));
    std::memset(effects_, 0, sizeof(effects_));
    numEffectSlots_ = 0;
    for (bool &changed : povChanged_)
        changed = true;
    for (int i = 0; i < IJOY_NUM_AXES; ++i) {
        deadZone_[i] = 0.0f;
        axisMode_[i] = 0;
        saturation_[i] = 1.0f;
        unused_[i] = 0;
        axisScale_[i] = static_cast<float>(kRangeMax);
        axisOffset_[i] = 0.0f;
    }

    if (FAILED(dinput_->CreateDeviceEx(entry_->instance->guidInstance, IID_IDirectInputDevice7A,
                                       reinterpret_cast<void **>(&device_), nullptr)))
        return;
    DWORD level = DISCL_EXCLUSIVE | (flags & IJOY_BACKGROUND ? DISCL_BACKGROUND : DISCL_FOREGROUND);
    if (FAILED(device_->SetCooperativeLevel(hwnd_, level))) {
        device_->Release();
        device_ = nullptr;
        return;
    }
    if (FAILED(device_->SetDataFormat(&g_joystickDataFormat))) {
        device_->Release();
        device_ = nullptr;
        return;
    }

    // Ask for a -10000..10000 range on every axis; where the device refuses, scale by its own.
    for (int axis = 0; axis < IJOY_NUM_AXES; ++axis) {
        DIPROPRANGE range = MakePropRange(AxisOffset(axis), kRangeMin, kRangeMax);
        if (FAILED(device_->SetProperty(DIPROP_RANGE, &range.diph))) {
            // Copy-paste slip of the original: the fallback reads the Y axis' range for every
            // axis but X.
            ReadAxisRange(axis, axis == IJOY_AXIS_X ? AxisOffset(IJOY_AXIS_X) : AxisOffset(IJOY_AXIS_Y));
        }
    }

    DIPROPDWORD axisMode = MakePropDword(0, DIPH_DEVICE, DIPROPAXISMODE_ABS);
    device_->SetProperty(DIPROP_AXISMODE, &axisMode.diph);
    DIPROPDWORD deadZone = MakePropDword(0, DIPH_DEVICE, 0);
    device_->SetProperty(DIPROP_DEADZONE, &deadZone.diph);
    DIPROPDWORD saturation = MakePropDword(0, DIPH_DEVICE, 10000);
    device_->SetProperty(DIPROP_SATURATION, &saturation.diph);

    numEffectSlots_ = IJOY_NUM_STANDARD_EFFECTS;
    for (int i = 0; i < numEffectSlots_; ++i)
        effects_[i].supported = false;
    if (entry_->caps.dwFlags & DIDC_FORCEFEEDBACK) {
        SetAcquired(true);
        forceFeedback_ = true;
        DIPROPDWORD autoCenter = MakePropDword(0, DIPH_DEVICE, DIPROPAUTOCENTER_OFF);
        if (FAILED(device_->SetProperty(DIPROP_AUTOCENTER, &autoCenter.diph)))
            Sleep(0);
        SetAcquired(true);
        DIPROPDWORD gain = MakePropDword(0, DIPH_DEVICE, 10000);
        device_->SetProperty(DIPROP_FFGAIN, &gain.diph);
        device_->EnumEffects(AddEffectInfo, effects_, DIEFT_ALL);
    }

    base_->AddRef();
    ++g_leaks.joys;
    SetAcquired(true);
    AddRef();
    *ok = true;
}

// 0x100023c0. The base is released even when the constructor failed before taking its reference.
Joy::~Joy()
{
    if (device_) {
        device_->Unacquire();
        device_->Release();
        device_ = nullptr;
    }
    --g_leaks.joys;
    base_->Release();
}

// 0x100040a0
int Joy::AddRef()
{
    ++g_leaks.joyRefs;
    return ++refCount_;
}

// 0x10004070
int Joy::Release()
{
    int count = --refCount_;
    --g_leaks.joyRefs;
    if (refCount_ == 0)
        delete this;
    return count;
}

// 0x10003840
bool Joy::SetAcquired(bool acquire)
{
    acquired_ = acquire;
    if (!acquire) {
        device_->Unacquire();
        return true;
    }
    return SUCCEEDED(device_->Acquire());
}

// Reads the device range of `object` (unchecked: zeros on failure) into axis `axis`' scaling.
void Joy::ReadAxisRange(int axis, DWORD object)
{
    DIPROPRANGE range = MakePropRange(object, 0, 0);
    device_->GetProperty(DIPROP_RANGE, &range.diph);
    StoreAxisRange(axis, range);
}

void Joy::StoreAxisRange(int axis, const DIPROPRANGE &range)
{
    if (range.lMin < 0) {
        axisScale_[axis] = static_cast<float>(range.lMax);
        return;
    }
    // For a 0-based range; the original centres on -(max + half) where max - half was meant.
    float half = static_cast<float>(range.lMax - range.lMin) * 0.5f;
    axisScale_[axis] = half;
    axisOffset_[axis] = -(static_cast<float>(range.lMax) + half);
}

// 0x100024b0
float Joy::GetDeadZone(int axis)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES)
        return 0.0f;
    return deadZone_[axis];
}

// 0x10002460
bool Joy::SetDeadZone(int axis, float deadZone)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES || !(deadZone >= 0.0f && deadZone <= 1.0f))
        return false;
    deadZone_[axis] = deadZone;
    return true;
}

// 0x100025d0
float Joy::GetDeviceDeadZone(int axis)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES)
        return 0.0f;
    DIPROPDWORD p = MakePropDword(AxisOffset(axis), DIPH_BYOFFSET, 0);
    if (FAILED(device_->GetProperty(DIPROP_DEADZONE, &p.diph)))
        return 0.0f;
    return static_cast<float>(p.dwData) * 0.0001f;
}

// 0x100024d0. Succeeds whether or not the device takes the value.
bool Joy::SetDeviceDeadZone(int axis, float deadZone)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES || !(deadZone >= 0.0f && deadZone <= 1.0f))
        return false;
    DIPROPDWORD p = MakePropDword(AxisOffset(axis), DIPH_BYOFFSET, FloatToLong(deadZone * 10000.0f));
    device_->SetProperty(DIPROP_DEADZONE, &p.diph);
    return true;
}

// 0x10002700
float Joy::GetSaturation(int axis)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES)
        return 0.0f;
    return saturation_[axis];
}

// 0x100026b0
bool Joy::SetSaturation(int axis, float saturation)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES || !(saturation >= 0.0f && saturation <= 1.0f))
        return false;
    saturation_[axis] = saturation;
    return true;
}

// 0x10002820
float Joy::GetDeviceSaturation(int axis)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES)
        return 0.0f;
    DIPROPDWORD p = MakePropDword(AxisOffset(axis), DIPH_BYOFFSET, 0);
    if (FAILED(device_->GetProperty(DIPROP_SATURATION, &p.diph)))
        return 0.0f;
    return static_cast<float>(p.dwData) * 0.0001f;
}

// 0x10002720. Succeeds whether or not the device takes the value.
bool Joy::SetDeviceSaturation(int axis, float saturation)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES || !(saturation >= 0.0f && saturation <= 1.0f))
        return false;
    DIPROPDWORD p = MakePropDword(AxisOffset(axis), DIPH_BYOFFSET, FloatToLong(saturation * 10000.0f));
    device_->SetProperty(DIPROP_SATURATION, &p.diph);
    return true;
}

// 0x10002be0
DWORD Joy::GetAxisMode(int axis)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES)
        return 0;
    return axisMode_[axis] & 0xF;
}

// 0x10002a00. Switching IJOY_AXIS_RAW sets the device's calibration mode; back to calibrated it
// also restores the -10000..10000 range.
bool Joy::SetAxisMode(int axis, DWORD mode)
{
    if (mode > 0xF || axis < 0 || axis >= IJOY_NUM_AXES)
        return false;
    DWORD raw = mode & IJOY_AXIS_RAW;
    if (raw != (axisMode_[axis] & IJOY_AXIS_RAW)) {
        DWORD offset = AxisOffset(axis);
        DIPROPDWORD calibration = MakePropDword(offset, DIPH_BYOFFSET,
                                                raw ? DIPROPCALIBRATIONMODE_RAW : DIPROPCALIBRATIONMODE_COOKED);
        if (FAILED(device_->SetProperty(DIPROP_CALIBRATIONMODE, &calibration.diph)))
            return false;
        if (!raw) {
            DIPROPRANGE range = MakePropRange(offset, kRangeMin, kRangeMax);
            device_->SetProperty(DIPROP_RANGE, &range.diph);
            DIPROPRANGE got = MakePropRange(offset, 0, 0);
            device_->GetProperty(DIPROP_RANGE, &got.diph);
            // The original stores the result into axis X's scaling whatever the axis.
            StoreAxisRange(IJOY_AXIS_X, got);
        }
    }
    axisMode_[axis] = (axisMode_[axis] & 0xF0) | mode;
    return true;
}

// 0x10003a90
float Joy::GetAxis(int axis)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES)
        return 0.0f;
    return state_.axis[axis];
}

// 0x10003b20. The original's switch over the button group lacks its breaks: a button that is up
// also tests the same bit of every later group.
bool Joy::IsButtonDown(int button)
{
    if (button < 0 || button >= IJOY_NUM_BUTTONS)
        return false;
    DWORD bit = 1u << (button & 31);
    for (int group = button / 32; group < IJOY_NUM_BUTTON_GROUPS; ++group) {
        if (state_.buttons[group] & bit)
            return true;
    }
    return false;
}

// 0x10003bd0. Same fall-through as IsButtonDown.
bool Joy::IsButtonPressed(int button)
{
    if (button < 0 || button >= IJOY_NUM_BUTTONS)
        return false;
    DWORD bit = 1u << (button & 31);
    for (int group = button / 32; group < IJOY_NUM_BUTTON_GROUPS; ++group) {
        if ((state_.buttons[group] & bit) && (buttonArmed_[group] & bit))
            return true;
    }
    return false;
}

// 0x100039d0
DWORD Joy::GetButtons(int group)
{
    if (group < 0 || group >= IJOY_NUM_BUTTON_GROUPS)
        return 0;
    return state_.buttons[group];
}

// 0x10003a20
DWORD Joy::GetButtonsPressed(int group)
{
    if (group < 0 || group >= IJOY_NUM_BUTTON_GROUPS)
        return 0;
    return buttonArmed_[group] & state_.buttons[group];
}

// 0x10003950
bool Joy::GetPov(float *radians, int pov)
{
    if (pov < 0 || pov >= IJOY_NUM_POVS || !radians)
        return false;
    *radians = state_.pov[pov];
    return !state_.povCentered[pov];
}

// 0x10003990
bool Joy::GetPovChanged(float *radians, int pov)
{
    if (pov < 0 || pov >= IJOY_NUM_POVS || !radians)
        return false;
    *radians = state_.pov[pov];
    return povChanged_[pov] && !state_.povCentered[pov];
}

// 0x10003870
bool Joy::GetState(IJoyState *state)
{
    if (!state)
        return false;
    *state = state_;
    return true;
}

// 0x100038a0
bool Joy::GetStatePressed(IJoyState *state)
{
    if (!state)
        return false;
    *state = state_;
    for (int group = 0; group < IJOY_NUM_BUTTON_GROUPS; ++group)
        state->buttons[group] = state_.buttons[group] & buttonArmed_[group];
    for (int pov = 0; pov < IJOY_NUM_POVS; ++pov)
        state->povCentered[pov] = !povChanged_[pov] || state_.povCentered[pov];
    return true;
}

// 0x10002c00: polls the device and rebuilds the state.
bool Joy::Update(DWORD flags)
{
    bool deviceOk = true;
    bool read = SUCCEEDED(device_->Poll()) || (acquired_ && SetAcquired(true));
    if (!read) {
        if (!(flags & IJOY_UPDATE_KEEP_LOST))
            return false;
        deviceOk = false;
        for (int axis = 0; axis < IJOY_NUM_AXES; ++axis) {
            if (!(axisMode_[axis] & IJOY_AXIS_RAW))
                state_.axis[axis] = 0.0f;
        }
    } else {
        // A failed read that reacquires goes on with the zeroed state.
        std::memset(&raw_, 0, sizeof(raw_));
        if (FAILED(device_->GetDeviceState(sizeof(raw_), &raw_)) && !(acquired_ && SetAcquired(true)))
            return false;
        const LONG *rawAxes = &raw_.lX;  // lX, lY, lZ, lRx, lRy, lRz, rglSlider[0], rglSlider[1]
        for (int axis = 0; axis < IJOY_NUM_AXES; ++axis) {
            DWORD mode = axisMode_[axis];
            float v;
            if (mode & IJOY_AXIS_RAW) {
                v = static_cast<float>(rawAxes[axis]);
            } else {
                v = static_cast<float>(rawAxes[axis]) + axisOffset_[axis];
                if (mode & IJOY_AXIS_UNIPOLAR)
                    v = (v + axisScale_[axis]) / (axisScale_[axis] + axisScale_[axis]);
                else
                    v = v / axisScale_[axis];
                if (mode & IJOY_AXIS_INVERT)
                    v = -v;
            }
            state_.axis[axis] = v;
        }
    }

    for (int axis = 0; axis < IJOY_NUM_AXES; ++axis) {
        if (!(axisMode_[axis] & IJOY_AXIS_RAW) && std::fabs(state_.axis[axis]) < deadZone_[axis])
            state_.axis[axis] = 0.0f;
    }
    for (int axis = 0; axis < IJOY_NUM_AXES; ++axis) {
        // Written as the x87 compare tests it: a NaN axis (zero device range) is clamped too.
        if (!(axisMode_[axis] & IJOY_AXIS_RAW) && !(saturation_[axis] >= std::fabs(state_.axis[axis]))) {
            if (state_.axis[axis] > 0.0f) {
                // The original writes the positive clamp of the last axis into the one before.
                state_.axis[axis == IJOY_AXIS_SLIDER1 ? IJOY_AXIS_SLIDER0 : axis] = saturation_[axis];
            } else {
                state_.axis[axis] = -saturation_[axis];
            }
        }
    }

    IJoyState current = state_;
    for (int axis = 0; axis < IJOY_NUM_AXES; ++axis) {
        DWORD mode = axisMode_[axis];
        if (!(mode & IJOY_AXIS_RAW) && (mode & IJOY_AXIS_RELATIVE))
            state_.axis[axis] -= previous_.axis[axis];
    }
    previous_ = current;

    if (deviceOk) {
        for (int pov = 0; pov < IJOY_NUM_POVS; ++pov) {
            if (povChanged_[pov] && !state_.povCentered[pov])
                povChanged_[pov] = false;
            float old = state_.pov[pov];
            DWORD value = raw_.rgdwPOV[pov];
            bool centered = LOWORD(value) == 0xFFFF;
            state_.povCentered[pov] = centered;
            if (centered)
                povChanged_[pov] = true;
            else
                state_.pov[pov] = static_cast<float>(value) * kPi * kRadiansPerCentidegree;
            // POV 0 counts a move only when not centred; the others count any move or any
            // uncentred update.
            bool moved = old < state_.pov[pov] || old > state_.pov[pov];
            if (pov == 0 ? (moved && !centered) : (moved || !centered))
                povChanged_[pov] = true;
        }
    } else {
        for (int pov = 0; pov < IJOY_NUM_POVS; ++pov) {
            state_.povCentered[pov] = true;
            povChanged_[pov] = false;
        }
        // The original runs this loop to 8, past both 4-byte arrays: "povCentered[4..7]" are
        // the bytes of buttons[0] (set to 1 each) and "povChanged[4..7]" those of deadZone_[0].
        state_.buttons[0] = 0x01010101;
        deadZone_[IJOY_AXIS_X] = 0.0f;
    }

    for (int group = 0; group < IJOY_NUM_BUTTON_GROUPS; ++group) {
        // A button held at the previous update cannot count as pressed again until released.
        buttonArmed_[group] &= ~state_.buttons[group];
        if (group == 0 && !deviceOk)
            std::memset(raw_.rgbButtons, 0, sizeof(raw_.rgbButtons));
        DWORD held = 0;
        for (int b = 0; b < 32; ++b) {
            if (raw_.rgbButtons[group * 32 + b])
                held |= 1u << b;
        }
        state_.buttons[group] = held;
        // Released buttons are armed again; held ones keep their arm bit.
        buttonArmed_[group] = held ^ ~(held & buttonArmed_[group]);
    }
    return true;
}

// 0x10003de0
const char *Joy::GetAxisName(int axis)
{
    return AxisName(entry_, axis);
}

// 0x10003e80. Like the original, an invalid request zeroes *info even when info is null.
bool Joy::GetAxisInfo(int axis, IJoyAxisInfo *info)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES || !info) {
        std::memset(info, 0, sizeof(*info));
        return false;
    }
    return AxisInfo(entry_, axis, info);
}

// 0x10003d80
const char *Joy::GetButtonName(int button)
{
    return ButtonName(entry_, button);
}

// 0x10003ca0
bool Joy::GetInfo(IJoyInfo *info)
{
    if (!info)
        return false;
    JoyInfo(entry_, forceFeedback_, info);
    return true;
}

// 0x10003fe0
const char *Joy::GetPovName(int pov)
{
    return PovName(entry_, pov);
}

// 0x10002440
bool Joy::RunControlPanel()
{
    return SUCCEEDED(device_->RunControlPanel(hwnd_, 0));
}

// 0x10002990
float Joy::GetForceFeedbackGain()
{
    if (!(entry_->caps.dwFlags & DIDC_FORCEFEEDBACK))
        return 0.0f;
    DIPROPDWORD p = MakePropDword(0, DIPH_DEVICE, 0);
    if (FAILED(device_->GetProperty(DIPROP_FFGAIN, &p.diph)))
        return 0.0f;
    return static_cast<float>(p.dwData) * 0.0001f;
}

// 0x10002900
bool Joy::SetForceFeedbackGain(float gain)
{
    if (!(gain >= 0.0f && gain <= 1.0f) || !(entry_->caps.dwFlags & DIDC_FORCEFEEDBACK))
        return false;
    DIPROPDWORD p = MakePropDword(0, DIPH_DEVICE, FloatToLong(gain * 10000.0f));
    return SUCCEEDED(device_->SetProperty(DIPROP_FFGAIN, &p.diph));
}

// 0x100040c0
bool Joy::GetForceFeedbackState(DWORD *state)
{
    if (!state)
        return false;
    *state = 0;
    DWORD ff = 0;
    if (FAILED(device_->GetForceFeedbackState(&ff)))
        return false;
    static const struct {
        DWORD directInput;
        DWORD ijoy;
    } kBits[] = {
        {DIGFFS_ACTUATORSOFF, IJOY_FF_ACTUATORS_OFF},
        {DIGFFS_ACTUATORSON, IJOY_FF_ACTUATORS_ON},
        {DIGFFS_DEVICELOST, IJOY_FF_DEVICE_LOST},
        {DIGFFS_EMPTY, IJOY_FF_EMPTY},
        {DIGFFS_PAUSED, IJOY_FF_PAUSED},
        {DIGFFS_POWEROFF, IJOY_FF_POWER_OFF},
        {DIGFFS_POWERON, IJOY_FF_POWER_ON},
        {DIGFFS_SAFETYSWITCHOFF, IJOY_FF_SAFETY_SWITCH_OFF},
        {DIGFFS_SAFETYSWITCHON, IJOY_FF_SAFETY_SWITCH_ON},
        {DIGFFS_STOPPED, IJOY_FF_STOPPED},
        {DIGFFS_USERFFSWITCHOFF, IJOY_FF_USER_SWITCH_OFF},
        {DIGFFS_USERFFSWITCHON, IJOY_FF_USER_SWITCH_ON},
    };
    for (const auto &b : kBits) {
        if (ff & b.directInput)
            *state |= b.ijoy;
    }
    return true;
}

// 0x10004170
bool Joy::SendForceFeedbackCommand(int command)
{
    DWORD directInput;
    switch (command) {
    case IJOY_FF_CONTINUE: directInput = DISFFC_CONTINUE; break;
    case IJOY_FF_PAUSE: directInput = DISFFC_PAUSE; break;
    case IJOY_FF_RESET: directInput = DISFFC_RESET; break;
    case IJOY_FF_SET_ACTUATORS_OFF: directInput = DISFFC_SETACTUATORSOFF; break;
    case IJOY_FF_SET_ACTUATORS_ON: directInput = DISFFC_SETACTUATORSON; break;
    case IJOY_FF_STOP_ALL: directInput = DISFFC_STOPALL; break;
    default: return false;
    }
    return SUCCEEDED(device_->SendForceFeedbackCommand(directInput));
}

// 0x10004230
bool Joy::CreateEffect(IJoyEffect **effect)
{
    bool ok = false;
    JoyEffect *created = new JoyEffect(this, device_, effects_, numEffectSlots_, &ok);
    *effect = created;
    if (ok)
        return true;
    delete created;
    *effect = nullptr;
    return false;
}
