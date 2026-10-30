// IJoy.dll: the joystick / force-feedback wrapper of Hidden & Dangerous 2 over DirectInput 7.
//
// The DLL exports one function, CreateJoyBase (decorated _CreateJoyBase@20, ordinal 1). Everything
// else is reached through three interfaces with MSVC-compatible vtables (__thiscall, a virtual
// destructor in slot 0, bool results in AL, float results in ST0):
//
//   IJoyBase    the DirectInput object plus the list of attached joysticks
//   IJoy        one opened joystick: polled state, axis setup, force-feedback device control
//   IJoyEffect  one force-feedback effect of a joystick
//
// Axes are numbered 0..7 (X, Y, Z, Rx, Ry, Rz, slider 0, slider 1), POV hats 0..3 and buttons
// 0..127 in four groups of 32. Objects are reference counted: CreateJoyBase, IJoyBase::CreateJoy
// and IJoy::CreateEffect hand out one reference; Release deletes the object at zero.
#pragma once

#include <windows.h>

struct IDirectInput7A;

// CreateJoyBase flags.
enum : DWORD {
    IJOY_REPORT_LEAKS = 0x1,  // on DLL unload, show a message box listing unreleased objects
};

// IJoyBase::CreateJoy flags.
enum : DWORD {
    IJOY_BACKGROUND = 0x1,  // DISCL_BACKGROUND instead of DISCL_FOREGROUND (always exclusive)
};

// IJoy::Update flags.
enum : DWORD {
    IJOY_UPDATE_KEEP_LOST = 0x1,  // a lost device still updates: axes 0, buttons up, POVs centred
};

enum IJoyAxis {
    IJOY_AXIS_X, IJOY_AXIS_Y, IJOY_AXIS_Z, IJOY_AXIS_RX, IJOY_AXIS_RY, IJOY_AXIS_RZ,
    IJOY_AXIS_SLIDER0, IJOY_AXIS_SLIDER1,
    IJOY_NUM_AXES
};

enum { IJOY_NUM_POVS = 4, IJOY_NUM_BUTTON_GROUPS = 4, IJOY_NUM_BUTTONS = 128 };

// Axis mode bits (IJoy::SetAxisMode).
enum : DWORD {
    IJOY_AXIS_RELATIVE = 0x1,  // report the change since the previous update
    IJOY_AXIS_INVERT = 0x2,    // negate
    IJOY_AXIS_UNIPOLAR = 0x4,  // 0..1 instead of -1..1
    IJOY_AXIS_RAW = 0x8,       // uncalibrated device value; no scaling, dead zone or saturation
};

// IJoyInfo::subType: the DIDEVTYPEJOYSTICK_* subtype (1 = unknown, 2 = traditional, 3 = flight
// stick, 4 = gamepad, 5 = rudder, 6 = wheel, 7 = head tracker), 0 for anything else.
struct IJoyInfo {
    const char *instanceName;
    const char *productName;
    DWORD numAxes;
    DWORD numButtons;
    DWORD numPovs;
    bool forceFeedback;
    int subType;
};

// IJoyAxisInfo::aspect.
enum { IJOY_ASPECT_NONE, IJOY_ASPECT_ACCELERATION, IJOY_ASPECT_FORCE, IJOY_ASPECT_POSITION, IJOY_ASPECT_VELOCITY };

struct IJoyAxisInfo {
    const char *name;
    bool ffActuator;
    DWORD ffForceResolution;
    DWORD ffMaxForce;
    int aspect;  // IJOY_ASPECT_*
};

// The polled state. Axes are -1..1 (0..1 unipolar) unless raw, POVs in radians clockwise from
// north, buttons one bit each (bit b of buttons[g] is button g * 32 + b).
struct IJoyState {
    float axis[IJOY_NUM_AXES];
    float pov[IJOY_NUM_POVS];
    bool povCentered[IJOY_NUM_POVS];
    DWORD buttons[IJOY_NUM_BUTTON_GROUPS];
};

// IJoy::GetForceFeedbackState bits.
enum : DWORD {
    IJOY_FF_ACTUATORS_OFF = 0x1,
    IJOY_FF_ACTUATORS_ON = 0x2,
    IJOY_FF_DEVICE_LOST = 0x4,
    IJOY_FF_EMPTY = 0x8,
    IJOY_FF_PAUSED = 0x10,
    IJOY_FF_POWER_OFF = 0x20,
    IJOY_FF_POWER_ON = 0x40,
    IJOY_FF_SAFETY_SWITCH_OFF = 0x80,
    IJOY_FF_SAFETY_SWITCH_ON = 0x100,
    IJOY_FF_STOPPED = 0x200,
    IJOY_FF_USER_SWITCH_OFF = 0x400,
    IJOY_FF_USER_SWITCH_ON = 0x800,
};

// IJoy::SendForceFeedbackCommand.
enum IJoyFFCommand {
    IJOY_FF_CONTINUE = 1,
    IJOY_FF_PAUSE = 2,
    IJOY_FF_RESET = 3,
    IJOY_FF_SET_ACTUATORS_OFF = 4,
    IJOY_FF_SET_ACTUATORS_ON = 5,
    IJOY_FF_STOP_ALL = 6,
};

// Effect types: the eleven standard DirectInput effects, then a set loaded from an .ffe file.
enum IJoyEffectType {
    IJOY_EFFECT_CONSTANT,
    IJOY_EFFECT_RAMP,
    IJOY_EFFECT_SQUARE,
    IJOY_EFFECT_SINE,
    IJOY_EFFECT_TRIANGLE,
    IJOY_EFFECT_SAWTOOTH_UP,
    IJOY_EFFECT_SAWTOOTH_DOWN,
    IJOY_EFFECT_SPRING,
    IJOY_EFFECT_DAMPER,
    IJOY_EFFECT_INERTIA,
    IJOY_EFFECT_FRICTION,
    IJOY_NUM_STANDARD_EFFECTS,
    IJOY_EFFECT_FILE = IJOY_NUM_STANDARD_EFFECTS,
    IJOY_EFFECT_NONE,
};

// What a device supports for one effect type (IJoyEffect::GetEffectCaps).
struct IJoyEffectCaps {
    bool supported;
    bool deadBand;
    bool saturation;
    bool posNegSaturation;
    bool posNegCoefficients;
    // Parameters the effect accepts, and those it can change while playing.
    bool staticDirection, staticDuration, staticEnvelope, staticGain, staticTypeParams;
    bool dynamicDirection, dynamicDuration, dynamicEnvelope, dynamicGain, dynamicTypeParams;
};

// One axis of a condition effect (IJoyEffect::SetCondition), each value clamped to the range shown.
struct IJoyCondition {
    float offset;               // -1..1
    float positiveCoefficient;  // -1..1
    float negativeCoefficient;  // -1..1
    float positiveSaturation;   // 0..1
    float negativeSaturation;   // 0..1
    float deadBand;             // 0..1
};

// IJoyEffect::GetStatus bits.
enum : DWORD {
    IJOY_EFFECT_PLAYING = 0x1,
    IJOY_EFFECT_EMULATED = 0x2,
};

// The parameter setters take `apply`: false only stores the value for the next Create (or a
// later Commit*), true also sends it to the playing effect when the device can change it live.
// The Commit* methods unload the effect and store the parameter without downloading it.
class IJoyEffect {
public:
    virtual ~IJoyEffect() {}
    virtual int AddRef() = 0;
    virtual int Release() = 0;
    // iterations 0 = once; solo stops the device's other effects.
    virtual bool Start(DWORD iterations, bool solo) = 0;
    virtual bool Stop(bool unload) = 0;
    // (Re)creates the effect; IJOY_EFFECT_NONE only releases the current one. direction is in
    // radians, duration in microseconds (0 = infinite, not allowed for a ramp), gain 0..1. For
    // IJOY_EFFECT_FILE, fileName names an .ffe file; without one the call succeeds (with no
    // effects) only when allowEmptyFile is nonzero.
    virtual bool Create(int type, float direction, DWORD duration, float gain, const char *fileName,
                        int allowEmptyFile) = 0;
    // axes: count (0..8) IJoyAxis numbers; 0 restores the default X and Y. Used by the next Create.
    virtual bool SetAxes(int count, const BYTE *axes) = 0;
    virtual bool SetGain(float gain, bool apply) = 0;
    virtual bool CommitGain() = 0;
    virtual bool SetDirection(float radians, bool apply) = 0;
    virtual bool CommitDirection() = 0;
    virtual bool SetDuration(DWORD microseconds, bool apply) = 0;
    virtual bool CommitDuration() = 0;
    virtual bool SetEnvelope(float attackLevel, DWORD attackTime, float fadeLevel, DWORD fadeTime, bool apply) = 0;
    virtual bool CommitEnvelope() = 0;
    virtual bool SetConstantForce(float magnitude, bool apply) = 0;
    virtual bool SetRampForce(float start, float end, bool apply) = 0;
    // phaseShift nonzero: start a quarter period in for a sine, half a period for the others.
    virtual bool SetPeriodic(float magnitude, float offset, float phaseShift, DWORD period, bool apply) = 0;
    virtual bool SetCondition(const IJoyCondition *first, const IJoyCondition *second, bool apply) = 0;
    virtual bool CommitTypeParams() = 0;
    virtual bool GetEffectCaps(IJoyEffectCaps *caps, int type) = 0;
    virtual int GetType() = 0;
    virtual bool GetStatus(DWORD *status) = 0;
};

class IJoy {
public:
    virtual ~IJoy() {}
    virtual int AddRef() = 0;
    virtual int Release() = 0;
    // Dead zone and saturation 0..1: the "device" variants are DirectInput properties applied by
    // the driver, the others are applied by Update to the scaled value.
    virtual float GetDeadZone(int axis) = 0;
    virtual bool SetDeadZone(int axis, float deadZone) = 0;
    virtual float GetDeviceDeadZone(int axis) = 0;
    virtual bool SetDeviceDeadZone(int axis, float deadZone) = 0;
    virtual float GetSaturation(int axis) = 0;
    virtual bool SetSaturation(int axis, float saturation) = 0;
    virtual float GetDeviceSaturation(int axis) = 0;
    virtual bool SetDeviceSaturation(int axis, float saturation) = 0;
    virtual DWORD GetAxisMode(int axis) = 0;
    virtual bool SetAxisMode(int axis, DWORD mode) = 0;
    virtual float GetAxis(int axis) = 0;
    virtual bool IsButtonDown(int button) = 0;
    virtual bool IsButtonPressed(int button) = 0;  // went down in the last Update
    virtual DWORD GetButtons(int group) = 0;
    virtual DWORD GetButtonsPressed(int group) = 0;
    virtual bool GetPov(float *radians, int pov) = 0;         // false when centred
    virtual bool GetPovChanged(float *radians, int pov) = 0;  // moved in the last Update, not centred
    virtual bool GetState(IJoyState *state) = 0;
    // The state with buttons that went down in the last Update, and povCentered true for POVs
    // that did not change.
    virtual bool GetStatePressed(IJoyState *state) = 0;
    virtual bool Update(DWORD flags) = 0;  // IJOY_UPDATE_*
    virtual const char *GetAxisName(int axis) = 0;
    virtual bool GetAxisInfo(int axis, IJoyAxisInfo *info) = 0;
    virtual const char *GetButtonName(int button) = 0;
    virtual bool GetInfo(IJoyInfo *info) = 0;
    virtual const char *GetPovName(int pov) = 0;
    virtual bool RunControlPanel() = 0;
    virtual float GetForceFeedbackGain() = 0;
    virtual bool SetForceFeedbackGain(float gain) = 0;
    virtual bool GetForceFeedbackState(DWORD *state) = 0;  // IJOY_FF_* bits
    virtual bool SendForceFeedbackCommand(int command) = 0;  // IJoyFFCommand
    virtual bool CreateEffect(IJoyEffect **effect) = 0;
};

class IJoyBase {
public:
    virtual ~IJoyBase() {}
    virtual int AddRef() = 0;
    virtual int Release() = 0;
    virtual const char *GetAxisName(int joy, int axis) = 0;
    virtual bool GetAxisInfo(int joy, int axis, IJoyAxisInfo *info) = 0;
    virtual const char *GetButtonName(int joy, int button) = 0;
    virtual int GetNumJoys() = 0;
    virtual IDirectInput7A *GetDirectInput() = 0;
    virtual bool GetJoyInfo(IJoyInfo *info, int joy) = 0;
    virtual const char *GetPovName(int joy, int pov) = 0;
    virtual bool RunControlPanel() = 0;
    // Opens joystick `joy` (0..GetNumJoys() - 1); asking again for the same one returns the
    // same object with another reference. flags: IJOY_BACKGROUND.
    virtual bool CreateJoy(IJoy **joy, int index, DWORD flags) = 0;
};

#ifdef IJOY_EXPORTS
#define IJOY_API extern "C" __declspec(dllexport)
#else
#define IJOY_API extern "C" __declspec(dllimport)
#endif

// Creates the joystick system. hinstance and hwnd (the window DirectInput cooperates with) must be
// set; directInput, if given, is used (and AddRef'd) instead of creating one. flags: IJOY_REPORT_LEAKS.
// Fails when no joystick is attached.
IJOY_API bool __stdcall CreateJoyBase(IJoyBase **joyBase, HINSTANCE hinstance, HWND hwnd,
                                      IDirectInput7A *directInput, DWORD flags);
