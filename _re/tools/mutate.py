"""Mutation check of tests/compare.cpp: builds the port with one deliberate deviation at a time
(each a quirk or detail the port reproduces) and expects the differential test to fail.

Usage: python mutate.py ORIGINAL_DLL [seeds]   (needs build/compare.exe: python build.py compare)
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MUTATIONS = [
    ("joy.cpp", "state_.axis[axis == IJOY_AXIS_SLIDER1 ? IJOY_AXIS_SLIDER0 : axis] = saturation_[axis];",
     "state_.axis[axis] = saturation_[axis];"),
    ("joy.cpp", "pov == 0 ? (moved && !centered) : (moved || !centered)", "moved || !centered"),
    ("joy.cpp", "axis == IJOY_AXIS_X ? AxisOffset(IJOY_AXIS_X) : AxisOffset(IJOY_AXIS_Y)", "AxisOffset(axis)"),
    ("joy.cpp", "for (int group = button / 32; group < IJOY_NUM_BUTTON_GROUPS; ++group) {\n        if (state_.buttons[group] & bit)",
     "for (int group = button / 32; group <= button / 32; ++group) {\n        if (state_.buttons[group] & bit)"),
    ("joy.cpp", "deadZone_[IJOY_AXIS_X] = 0.0f;", ""),
    ("joy.cpp", "StoreAxisRange(IJOY_AXIS_X, got);", "StoreAxisRange(axis, got);"),
    ("joy.cpp", "!(saturation_[axis] >= std::fabs(state_.axis[axis]))", "saturation_[axis] < std::fabs(state_.axis[axis])"),
    ("joy.cpp", "{DIGFFS_PAUSED, IJOY_FF_PAUSED},", "{DIGFFS_PAUSED, IJOY_FF_EMPTY},"),
    ("joy.cpp", "buttonArmed_[group] = held ^ ~(held & buttonArmed_[group]);", "buttonArmed_[group] = ~held;"),
    ("joy.cpp", "-(static_cast<float>(range.lMax) + half)", "-(static_cast<float>(range.lMax) - half)"),
    ("joy_effect.cpp", "phase = type_ == IJOY_EFFECT_SINE ? kQuarterPeriod : kHalfPeriod;",
     "phase = type_ == IJOY_EFFECT_SINE ? kHalfPeriod : kQuarterPeriod;"),
    ("joy_effect.cpp", "if (!(slots_[type_].info.dwStaticParams & parameter))",
     "if (!(slots_[type_].info.dwDynamicParams & parameter))"),
    ("joy_effect.cpp", "c.offset = Clamp(c.offset, -1.0f, 1.0f);", "c.offset = Clamp(c.offset, 0.0f, 1.0f);"),
    ("joy_effect.cpp", "if (duration == 0) {  // a ramp cannot be infinite", "if (false) {"),
    ("joy_effect.cpp", "            // Leaves numPendingAxes_ as it was.\n", "            numPendingAxes_ = 2;\n"),
    ("joy_base.cpp", "info->subType = subType >= 1 && subType <= 7 ? subType : 0;", "info->subType = subType;"),
    ("joy_base.cpp", "        if (!list_.ok)\n            return false;\n        std::memset", "        std::memset"),
    ("data_format.cpp", "{&GUID_Slider, 0x18, AXIS, DIDOI_ASPECTVELOCITY},", "{&GUID_Slider, 0xc8, AXIS, DIDOI_ASPECTVELOCITY},"),
]


def main():
    orig = sys.argv[1]
    seeds = sys.argv[2] if len(sys.argv) > 2 else "300"
    compare = os.path.join(ROOT, "build", "compare.exe")
    caught = 0
    for i, (name, old, new) in enumerate(MUTATIONS):
        work = os.path.join(tempfile.gettempdir(), "ijoy_mutation")
        shutil.rmtree(work, ignore_errors=True)
        for sub in ("src", "include", "lib"):
            shutil.copytree(os.path.join(ROOT, sub), os.path.join(work, sub))
        for f in ("build.py", "ijoy.def"):
            shutil.copyfile(os.path.join(ROOT, f), os.path.join(work, f))
        path = os.path.join(work, "src", name)
        text = open(path, encoding="utf-8").read()
        if text.count(old) != 1:
            print(f"{i}: pattern not found once in {name}")
            continue
        open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new))
        r = subprocess.run([sys.executable, os.path.join(work, "build.py"), "dll"], capture_output=True, text=True)
        if r.returncode != 0:
            print(f"{i}: build failed\n{r.stdout}")
            continue
        dll = os.path.join(work, "IJoy_mutant.dll")
        shutil.copyfile(os.path.join(work, "build", "IJoy.dll"), dll)
        t = subprocess.run([compare, orig, dll, seeds, "300"], capture_output=True, text=True, timeout=900)
        last = t.stdout.strip().splitlines()[-1] if t.stdout.strip() else f"exit {t.returncode}"
        ok = t.returncode != 0
        caught += ok
        print(f"{i}: {'caught' if ok else 'MISSED'}  {name}: {old[:60]!r}  ({last})", flush=True)
    print(f"{caught} of {len(MUTATIONS)} mutations caught")


if __name__ == "__main__":
    main()
