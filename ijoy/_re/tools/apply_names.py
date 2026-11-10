"""Names IJoy.dll's functions in Ghidra after the port (class::method), and removes the bogus
function auto-analysis made of the joystick data-format table at 0x10005ee0.

Usage: python apply_names.py [--dry-run]
"""
import sys

import gh

NAMES = {
    0x10001000: "_dynamic_initializer_g_leaks",
    0x10001010: "_construct_g_leaks",
    0x10001020: "_register_g_leaks_destructor",
    0x10001030: "_destroy_g_leaks",
    0x100010d0: "JoyBase::~JoyBase",
    0x10001130: "JoyBase::scalar_deleting_destructor",
    0x10001150: "JoyBase::FreeDevices",
    0x100011f0: "JoyBase::JoyBase",
    0x100012c0: "JoyBase::Release",
    0x100012f0: "JoyBase::AddRef",
    0x10001310: "JoyBase::EnumerateDevices",
    0x100013c0: "JoyBase::GetJoyInfo",
    0x100014c0: "JoyBase::GetNumJoys",
    0x100014d0: "JoyBase::GetButtonName",
    0x10001550: "JoyBase::GetAxisName",
    0x10001620: "JoyBase::GetAxisInfo",
    0x100017a0: "IJoyBase::~IJoyBase",
    0x100017b0: "IJoyBase::scalar_deleting_destructor",
    0x100017d0: "JoyBase::GetPovName",
    0x10001880: "JoyBase::GetDirectInput",
    0x10001890: "JoyBase::RunControlPanel",
    0x100018b0: "JoyBase::CreateJoy",
    0x10001a80: "Joy::Joy",
    0x10002370: "IJoy::~IJoy",
    0x10002380: "IJoy::scalar_deleting_destructor",
    0x100023a0: "Joy::scalar_deleting_destructor",
    0x100023c0: "Joy::~Joy",
    0x10002440: "Joy::RunControlPanel",
    0x10002460: "Joy::SetDeadZone",
    0x100024b0: "Joy::GetDeadZone",
    0x100024d0: "Joy::SetDeviceDeadZone",
    0x100025d0: "Joy::GetDeviceDeadZone",
    0x100026b0: "Joy::SetSaturation",
    0x10002700: "Joy::GetSaturation",
    0x10002720: "Joy::SetDeviceSaturation",
    0x10002820: "Joy::GetDeviceSaturation",
    0x10002900: "Joy::SetForceFeedbackGain",
    0x10002990: "Joy::GetForceFeedbackGain",
    0x10002a00: "Joy::SetAxisMode",
    0x10002be0: "Joy::GetAxisMode",
    0x10002c00: "Joy::Update",
    0x10003840: "Joy::SetAcquired",
    0x10003870: "Joy::GetState",
    0x100038a0: "Joy::GetStatePressed",
    0x10003950: "Joy::GetPov",
    0x10003990: "Joy::GetPovChanged",
    0x100039d0: "Joy::GetButtons",
    0x10003a20: "Joy::GetButtonsPressed",
    0x10003a90: "Joy::GetAxis",
    0x10003b20: "Joy::IsButtonDown",
    0x10003bd0: "Joy::IsButtonPressed",
    0x10003ca0: "Joy::GetInfo",
    0x10003d80: "Joy::GetButtonName",
    0x10003de0: "Joy::GetAxisName",
    0x10003e80: "Joy::GetAxisInfo",
    0x10003fe0: "Joy::GetPovName",
    0x10004070: "Joy::Release",
    0x100040a0: "Joy::AddRef",
    0x100040c0: "Joy::GetForceFeedbackState",
    0x10004170: "Joy::SendForceFeedbackCommand",
    0x10004230: "Joy::CreateEffect",
    0x100042e0: "JoyEffect::JoyEffect",
    0x100043a0: "IJoyEffect::~IJoyEffect",
    0x100043b0: "IJoyEffect::scalar_deleting_destructor",
    0x100043d0: "JoyEffect::scalar_deleting_destructor",
    0x100043f0: "JoyEffect::~JoyEffect",
    0x10004490: "JoyEffect::Release",
    0x100044c0: "JoyEffect::AddRef",
    0x100044e0: "JoyEffect::SetAxes",
    0x100045e0: "JoyEffect::Create",
    0x10004980: "JoyEffect::Start",
    0x10004a60: "JoyEffect::Stop",
    0x10004ae0: "JoyEffect::SetDirection",
    0x10004b80: "JoyEffect::CommitDirection",
    0x10004bd0: "JoyEffect::SetGain",
    0x10004c80: "JoyEffect::CommitGain",
    0x10004cd0: "JoyEffect::SetDuration",
    0x10004d50: "JoyEffect::CommitDuration",
    0x10004da0: "JoyEffect::SetEnvelope",
    0x10004eb0: "JoyEffect::CommitEnvelope",
    0x10004f00: "JoyEffect::SetConstantForce",
    0x10004fb0: "JoyEffect::CommitTypeParams",
    0x10005000: "JoyEffect::SetRampForce",
    0x10005100: "JoyEffect::SetPeriodic",
    0x10005260: "JoyEffect::SetCondition",
    0x10005700: "JoyEffect::GetEffectCaps",
    0x10005840: "JoyEffect::GetStatus",
    0x10005920: "JoyEffect::GetType",
    0x10005930: "CollectJoystick",
    0x10005a00: "CollectDeviceObject",
    0x10005ab0: "AddEffectInfo",
    0x10005cf0: "AddFileEffect",
    0x10005d60: "LeakCounters::LeakCounters",
    0x10005d80: "LeakCounters::~LeakCounters",
}


def main():
    dry = "--dry-run" in sys.argv
    bad = 0
    for addr, name in sorted(NAMES.items()):
        body = {"old_name": "0x%x" % addr, "new_name": name}
        if dry:
            body["dry_run"] = True
        r = gh.post("rename_function", body)
        if not (isinstance(r, dict) and r.get("success", True) and "error" not in r):
            bad += 1
            print("%x %s: %s" % (addr, name, r))
    if not dry:
        r = gh.post("delete_function", {"address": "0x10005ee0"})
        print("delete 0x10005ee0:", r)
    print(len(NAMES) - bad, "of", len(NAMES), "renamed")


if __name__ == "__main__":
    main()
