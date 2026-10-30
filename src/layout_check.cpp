// Compile-time checks that the classes keep the original's sizes and member offsets (MSVC x86).
#include "ijoy_internal.h"

#if defined(_MSC_VER) && defined(_M_IX86)

static_assert(sizeof(DIDEVICEINSTANCEA) == 0x244, "DIDEVICEINSTANCEA");
static_assert(sizeof(DIDEVICEOBJECTINSTANCEA) == 0x13c, "DIDEVICEOBJECTINSTANCEA");
static_assert(sizeof(DIEFFECTINFOA) == 0x124, "DIEFFECTINFOA");
static_assert(sizeof(DIEFFECT) == 0x38, "DIEFFECT (DirectInput 6+)");
static_assert(sizeof(DIJOYSTATE2) == 0x110, "DIJOYSTATE2");
static_assert(sizeof(IJoyState) == 0x44, "IJoyState");
static_assert(sizeof(IJoyInfo) == 0x1c, "IJoyInfo");
static_assert(sizeof(IJoyAxisInfo) == 0x14, "IJoyAxisInfo");
static_assert(sizeof(IJoyEffectCaps) == 15, "IJoyEffectCaps");
static_assert(sizeof(IJoyCondition) == 0x18, "IJoyCondition");
static_assert(sizeof(DeviceObject) == 0x10, "DeviceObject");
static_assert(sizeof(DeviceEntry) == 0x40, "DeviceEntry");
static_assert(offsetof(DeviceEntry, objects) == 0x34, "DeviceEntry::objects");
static_assert(offsetof(DeviceEntry, objectsOk) == 0x3c, "DeviceEntry::objectsOk");
static_assert(sizeof(EffectSlot) == 0x128, "EffectSlot");

void CheckLayouts()
{
    static_assert(sizeof(JoyBase) == 0x2c, "JoyBase");
    static_assert(offsetof(JoyBase, list_) == 0x08, "JoyBase::list_");
    static_assert(offsetof(JoyBase, dinput_) == 0x1c, "JoyBase::dinput_");
    static_assert(offsetof(JoyBase, openJoys_) == 0x28, "JoyBase::openJoys_");

    static_assert(sizeof(Joy) == 0xf4c, "Joy");
    static_assert(offsetof(Joy, device_) == 0x18, "Joy::device_");
    static_assert(offsetof(Joy, raw_) == 0x20, "Joy::raw_");
    static_assert(offsetof(Joy, state_) == 0x130, "Joy::state_");
    static_assert(offsetof(Joy, previous_) == 0x174, "Joy::previous_");
    static_assert(offsetof(Joy, acquired_) == 0x1b8, "Joy::acquired_");
    static_assert(offsetof(Joy, effects_) == 0x1bc, "Joy::effects_");
    static_assert(offsetof(Joy, numEffectSlots_) == 0xe74, "Joy::numEffectSlots_");
    static_assert(offsetof(Joy, buttonArmed_) == 0xe78, "Joy::buttonArmed_");
    static_assert(offsetof(Joy, povChanged_) == 0xe88, "Joy::povChanged_");
    static_assert(offsetof(Joy, deadZone_) == 0xe8c, "Joy::deadZone_");
    static_assert(offsetof(Joy, saturation_) == 0xeac, "Joy::saturation_");
    static_assert(offsetof(Joy, axisMode_) == 0xecc, "Joy::axisMode_");
    static_assert(offsetof(Joy, axisScale_) == 0xf0c, "Joy::axisScale_");
    static_assert(offsetof(Joy, axisOffset_) == 0xf2c, "Joy::axisOffset_");

    static_assert(sizeof(JoyEffect) == 0x128, "JoyEffect");
    static_assert(offsetof(JoyEffect, type_) == 0x0c, "JoyEffect::type_");
    static_assert(offsetof(JoyEffect, params_) == 0x28, "JoyEffect::params_");
    static_assert(offsetof(JoyEffect, envelope_) == 0x60, "JoyEffect::envelope_");
    static_assert(offsetof(JoyEffect, constant_) == 0x74, "JoyEffect::constant_");
    static_assert(offsetof(JoyEffect, periodic_) == 0x80, "JoyEffect::periodic_");
    static_assert(offsetof(JoyEffect, condition_) == 0x90, "JoyEffect::condition_");
    static_assert(offsetof(JoyEffect, pendingAxes_) == 0xc0, "JoyEffect::pendingAxes_");
    static_assert(offsetof(JoyEffect, axes_) == 0xe0, "JoyEffect::axes_");
    static_assert(offsetof(JoyEffect, numPendingAxes_) == 0x100, "JoyEffect::numPendingAxes_");
    static_assert(offsetof(JoyEffect, direction_) == 0x108, "JoyEffect::direction_");
}

#endif
