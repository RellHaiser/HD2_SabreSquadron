"""Mutations of src/game/vehicle_lamps.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
SRC = os.path.join(ROOT, r"src\game\vehicle_lamps.cpp")

MUTATIONS = [
    ("blink times swapped", "if (m_unknown5C0 >= m_unknown5B8) {", "if (m_unknown5C0 >= m_unknown5BC) {"),
    ("flash sides swapped", "m_unknown5B0 = (m_unknown5B0 & ~VEHICLE_LIGHT_FLASH_RIGHT) | VEHICLE_LIGHT_FLASH_LEFT;",
     "m_unknown5B0 = (m_unknown5B0 & ~VEHICLE_LIGHT_FLASH_LEFT) | VEHICLE_LIGHT_FLASH_RIGHT;"),
    ("auto brake sign", "if (m_brake > 0.0f)", "if (m_brake < 0.0f)"),
    ("auto indicator side", "else if (m_unknown94C > 0.0f)", "else if (m_unknown94C < 0.0f)"),
    ("rear lamps not brake", "lit = (lights & VEHICLE_LIGHT_INDICATOR_LEFT) ? blink : (lights & VEHICLE_LIGHT_BRAKE) && working;",
     "lit = (lights & VEHICLE_LIGHT_INDICATOR_LEFT) ? blink : (lights & VEHICLE_LIGHT_REVERSE) && working;"),
    ("headlight beams bit", "beams = VEHICLE_LIGHT_HEADLIGHT_BEAMS;", "beams = VEHICLE_LIGHT_BRAKE_BEAMS;"),
    ("shared not placed at projector", "        at = projector.frame;\n", ""),
    ("shared headlight spot dir", "lamp.kind == VEHICLE_LAMP_RE ? &ahead : nullptr", "nullptr"),
    ("off keeps projector", "                lamp.projector->SetOn(false);\n                DarkenLampObject(lamp.light, lamp);",
     "                DarkenLampObject(lamp.light, lamp);"),
    ("off ignores shared lit", "            if (lamp.projector &&\n                !((lamp.flags & VEHICLE_LAMP_SHARED) && (m_projectors[lamp.projectorIndex].state & VEHICLE_PROJECTOR_LIT))) {",
     "            if (lamp.projector) {"),
    ("lit flag not cleared", "        m_projectors[i].state &= ~VEHICLE_PROJECTOR_LIT;", "        (void)i;"),
    ("indicator blink ignored", "const bool blink = (lights & VEHICLE_LIGHT_BLINK) && working;", "const bool blink = working;"),
]

# (name, pattern, replacement): in SRC.
MUTATIONS = [m if len(m) == 4 else (SRC,) + m for m in MUTATIONS]

if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
