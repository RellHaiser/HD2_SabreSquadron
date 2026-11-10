// Implementation classes of IJoy.dll. Member layouts follow the original (offsets in comments,
// checked by static_asserts under MSVC); nothing outside the DLL reads them, but keeping them makes
// the port comparable with the original byte for byte.
#pragma once

#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <dinput.h>

#include <cstddef>

#include "ijoy.h"

// Objects alive and references held, per class; reported on unload with IJOY_REPORT_LEAKS (0x1000fe60).
struct LeakCounters {
    int joyBaseRefs;
    int joyRefs;
    int effectRefs;
    int joyBases;
    int joys;
    int effects;
    bool report;

    LeakCounters();
    ~LeakCounters();
};

extern LeakCounters g_leaks;

// c_dfDIJoystick2 as the original linked it from the DirectX 7 dinput.lib (data_format.cpp).
extern const DIDATAFORMAT g_joystickDataFormat;

// One object (axis, button, POV, ...) of an enumerated device.
struct DeviceObject {
    DIDEVICEOBJECTINSTANCEA *info;  // 0x00
    DWORD unused[2];                // 0x04, always 0
    DeviceObject *next;             // 0x0c
};

// One attached joystick, collected by IJoyBase at creation.
struct DeviceEntry {
    DIDEVICEINSTANCEA *instance;   // 0x00
    IDirectInputDevice7A *device;  // 0x04, only while the device is enumerated
    DIDEVCAPS caps;                // 0x08
    DeviceObject *objects;         // 0x34
    DeviceEntry *next;             // 0x38
    bool objectsOk;                // 0x3c, set by the object enumeration (never read)
};

// The DirectInput objects shared by a JoyBase and its joysticks; also the device enumeration context.
struct DeviceList {
    IDirectInput7A *directInput;  // 0x00
    HWND hwnd;                    // 0x04
    DeviceEntry *devices;         // 0x08
    bool ok;                      // 0x0c, a device was added
};

// What a joystick's device supports of one standard effect type.
struct EffectSlot {
    DIEFFECTINFOA info;  // 0x000
    bool supported;      // 0x124
};

class Joy;

class JoyBase : public IJoyBase {
public:
    JoyBase(HINSTANCE hinstance, HWND hwnd, IDirectInput7A *directInput, bool *ok);
    ~JoyBase() override;

    int AddRef() override;
    int Release() override;
    const char *GetAxisName(int joy, int axis) override;
    bool GetAxisInfo(int joy, int axis, IJoyAxisInfo *info) override;
    const char *GetButtonName(int joy, int button) override;
    int GetNumJoys() override;
    IDirectInput7A *GetDirectInput() override;
    bool GetJoyInfo(IJoyInfo *info, int joy) override;
    const char *GetPovName(int joy, int pov) override;
    bool RunControlPanel() override;
    bool CreateJoy(IJoy **joy, int index, DWORD flags) override;

private:
    // Opened joysticks by index. The list holds no reference: a joystick released to zero stays
    // here as a dangling pointer, like in the original.
    struct OpenJoy {
        int index;
        Joy *joy;
        OpenJoy *next;
    };

    bool EnumerateDevices();
    void FreeDevices();
    DeviceEntry *Device(int index);

    int refCount_;            // 0x04
    DeviceList list_;         // 0x08
    HINSTANCE hinstance_;     // 0x18
    IDirectInput7A *dinput_;  // 0x1c
    HWND hwnd_;               // 0x20
    int numJoys_;             // 0x24
    OpenJoy *openJoys_;       // 0x28

    friend void CheckLayouts();
};

class Joy : public IJoy {
public:
    Joy(JoyBase *base, DeviceEntry *entry, DeviceList *list, DWORD flags, bool *ok);
    ~Joy() override;

    int AddRef() override;
    int Release() override;
    float GetDeadZone(int axis) override;
    bool SetDeadZone(int axis, float deadZone) override;
    float GetDeviceDeadZone(int axis) override;
    bool SetDeviceDeadZone(int axis, float deadZone) override;
    float GetSaturation(int axis) override;
    bool SetSaturation(int axis, float saturation) override;
    float GetDeviceSaturation(int axis) override;
    bool SetDeviceSaturation(int axis, float saturation) override;
    DWORD GetAxisMode(int axis) override;
    bool SetAxisMode(int axis, DWORD mode) override;
    float GetAxis(int axis) override;
    bool IsButtonDown(int button) override;
    bool IsButtonPressed(int button) override;
    DWORD GetButtons(int group) override;
    DWORD GetButtonsPressed(int group) override;
    bool GetPov(float *radians, int pov) override;
    bool GetPovChanged(float *radians, int pov) override;
    bool GetState(IJoyState *state) override;
    bool GetStatePressed(IJoyState *state) override;
    bool Update(DWORD flags) override;
    const char *GetAxisName(int axis) override;
    bool GetAxisInfo(int axis, IJoyAxisInfo *info) override;
    const char *GetButtonName(int button) override;
    bool GetInfo(IJoyInfo *info) override;
    const char *GetPovName(int pov) override;
    bool RunControlPanel() override;
    float GetForceFeedbackGain() override;
    bool SetForceFeedbackGain(float gain) override;
    bool GetForceFeedbackState(DWORD *state) override;
    bool SendForceFeedbackCommand(int command) override;
    bool CreateEffect(IJoyEffect **effect) override;

private:
    bool SetAcquired(bool acquire);
    void ReadAxisRange(int axis, DWORD object);
    void StoreAxisRange(int axis, const DIPROPRANGE &range);

    int refCount_;                      // 0x004
    JoyBase *base_;                     // 0x008
    DeviceEntry *entry_;                // 0x00c
    DeviceList *list_;                  // 0x010
    IDirectInput7A *dinput_;            // 0x014
    IDirectInputDevice7A *device_;      // 0x018
    HWND hwnd_;                         // 0x01c
    DIJOYSTATE2 raw_;                   // 0x020
    IJoyState state_;                   // 0x130
    IJoyState previous_;                // 0x174, the state before the relative-axis step
    bool acquired_;                     // 0x1b8
    bool forceFeedback_;                // 0x1b9
    EffectSlot effects_[IJOY_NUM_STANDARD_EFFECTS];  // 0x1bc
    int numEffectSlots_;                // 0xe74
    // Per button group: buttons that were up in the update before last, i.e. may count as pressed.
    DWORD buttonArmed_[IJOY_NUM_BUTTON_GROUPS];  // 0xe78
    bool povChanged_[IJOY_NUM_POVS];    // 0xe88
    float deadZone_[IJOY_NUM_AXES];     // 0xe8c
    float saturation_[IJOY_NUM_AXES];   // 0xeac
    DWORD axisMode_[IJOY_NUM_AXES];     // 0xecc
    DWORD unused_[IJOY_NUM_AXES];       // 0xeec, zeroed, never used
    float axisScale_[IJOY_NUM_AXES];    // 0xf0c, half the device range
    float axisOffset_[IJOY_NUM_AXES];   // 0xf2c, added to the device value before scaling

    friend void CheckLayouts();
};

class JoyEffect : public IJoyEffect {
public:
    JoyEffect(Joy *joy, IDirectInputDevice7A *device, EffectSlot *slots, int numSlots, bool *ok);
    ~JoyEffect() override;

    int AddRef() override;
    int Release() override;
    bool Start(DWORD iterations, bool solo) override;
    bool Stop(bool unload) override;
    bool Create(int type, float direction, DWORD duration, float gain, const char *fileName,
                int allowEmptyFile) override;
    bool SetAxes(int count, const BYTE *axes) override;
    bool SetGain(float gain, bool apply) override;
    bool CommitGain() override;
    bool SetDirection(float radians, bool apply) override;
    bool CommitDirection() override;
    bool SetDuration(DWORD microseconds, bool apply) override;
    bool CommitDuration() override;
    bool SetEnvelope(float attackLevel, DWORD attackTime, float fadeLevel, DWORD fadeTime, bool apply) override;
    bool CommitEnvelope() override;
    bool SetConstantForce(float magnitude, bool apply) override;
    bool SetRampForce(float start, float end, bool apply) override;
    bool SetPeriodic(float magnitude, float offset, float phaseShift, DWORD period, bool apply) override;
    bool SetCondition(const IJoyCondition *first, const IJoyCondition *second, bool apply) override;
    bool CommitTypeParams() override;
    bool GetEffectCaps(IJoyEffectCaps *caps, int type) override;
    int GetType() override;
    bool GetStatus(DWORD *status) override;

    // The effects of an .ffe file (IJOY_EFFECT_FILE).
    struct FileEffect {
        IDirectInputEffect *effect;
        FileEffect *next;
    };

private:
    bool Playable() const { return type_ != IJOY_EFFECT_FILE && type_ != IJOY_EFFECT_NONE; }
    void ReleaseEffects();
    void ResetPendingAxes();
    bool Apply(DWORD parameter);
    bool Commit(DWORD parameter);

    int refCount_;                   // 0x004
    Joy *joy_;                       // 0x008
    int type_;                       // 0x00c, IJoyEffectType
    IDirectInputDevice7A *device_;   // 0x010
    IDirectInputEffect *effect_;     // 0x014
    FileEffect *fileEffects_;        // 0x018
    int numEffects_;                 // 0x01c
    EffectSlot *slots_;              // 0x020
    int numSlots_;                   // 0x024
    DIEFFECT params_;                // 0x028
    DIENVELOPE envelope_;            // 0x060
    DICONSTANTFORCE constant_;       // 0x074
    DIRAMPFORCE ramp_;               // 0x078
    DIPERIODIC periodic_;            // 0x080
    DICONDITION condition_[2];       // 0x090
    DWORD pendingAxes_[IJOY_NUM_AXES];  // 0x0c0, SetAxes; moved to axes_ by Create
    DWORD axes_[IJOY_NUM_AXES];      // 0x0e0, data-format offsets
    DWORD numPendingAxes_;           // 0x100
    DWORD numAxes_;                  // 0x104
    LONG direction_[IJOY_NUM_AXES];  // 0x108

    friend void CheckLayouts();
};

// Object lookups shared by JoyBase (any joystick) and Joy (its own), joy_base.cpp. The lookups
// return nullptr for an index out of range or a missing object.
const char *AxisName(const DeviceEntry *entry, int axis);
const char *ButtonName(const DeviceEntry *entry, int button);
const char *PovName(const DeviceEntry *entry, int pov);
bool AxisInfo(const DeviceEntry *entry, int axis, IJoyAxisInfo *info);
void JoyInfo(const DeviceEntry *entry, bool forceFeedback, IJoyInfo *info);

// DIJOYSTATE2 offset of axis 0..7 (lX ... rglSlider[1]).
inline DWORD AxisOffset(int axis) { return static_cast<DWORD>(axis) * sizeof(LONG); }

inline DIPROPDWORD MakePropDword(DWORD object, DWORD how, DWORD value)
{
    DIPROPDWORD p = {};
    p.diph.dwSize = sizeof(DIPROPDWORD);
    p.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    p.diph.dwObj = object;
    p.diph.dwHow = how;
    p.dwData = value;
    return p;
}

inline DIPROPRANGE MakePropRange(DWORD object, LONG minimum, LONG maximum)
{
    DIPROPRANGE p = {};
    p.diph.dwSize = sizeof(DIPROPRANGE);
    p.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    p.diph.dwObj = object;
    p.diph.dwHow = DIPH_BYOFFSET;
    p.lMin = minimum;
    p.lMax = maximum;
    return p;
}

// The x87 __ftol of the original: truncation toward zero.
inline LONG FloatToLong(float value) { return static_cast<LONG>(value); }

// The original's clamp: as its x87 compares test, NaN becomes `low`.
inline float Clamp(float value, float low, float high)
{
    if (value > high)
        return high;
    if (!(value >= low))
        return low;
    return value;
}
