"""Mutations of src/game/vehicle_start.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\vehicle_start.cpp")

MUTATIONS = [
    (K, "every holder straightened",
     "if ((wheel->flags & WHEEL_STEERED) && !(wheel->flags & WHEEL_DUMMY) && holder->GetParent() == m_model) {",
     "if (!(wheel->flags & WHEEL_DUMMY) && holder->GetParent() == m_model) {"),
    (K, "straight turned", "straight.Make(holder->GetLocalMatrix()(1), 0.0f);",
     "straight.Make(holder->GetLocalMatrix()(1), 0.1f);"),
    (K, "holder not placed", "        holder->SetPosInline(XYZ(wheel->holderPos));\n", ""),
    (K, "no model place", "    ReadModelPlace();\n\n    // The motion:", "\n    // The motion:"),
    (K, "velocity along x", "speed * m_modelAxisZ.x, speed * m_modelAxisZ.y, speed * m_modelAxisZ.z",
     "speed * m_modelAxisX.x, speed * m_modelAxisX.y, speed * m_modelAxisX.z"),
    (K, "dir not normalized", "SetXYZ(m_velocityDir, velocity.Normalized());", "SetXYZ(m_velocityDir, velocity);"),
    (K, "speed signed", "m_speed = std::fabs(speed);", "m_speed = speed;"),
    (K, "engine always on", "SetEngine(speed != 0.0f, true);", "SetEngine(true, true);"),
    (K, "engine not instant", "SetEngine(speed != 0.0f, true);", "SetEngine(speed != 0.0f, false);"),
    (K, "angular velocity kept", "    m_unknown2293 = 0;\n    m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);\n",
     "    m_unknown2293 = 0;\n"),
    (K, "physics bits or'ed", "m_unknown600 = VEHICLE_PHYSICS_AWAKE;", "m_unknown600 |= VEHICLE_PHYSICS_AWAKE;"),
    (K, "gear 2", "    m_gear = 1;\n    m_requestedGear = 1;\n    m_unknown78C = 0.0f;",
     "    m_gear = 2;\n    m_requestedGear = 1;\n    m_unknown78C = 0.0f;"),
    (K, "clutch bit kept", "VEHICLE_CLUTCH_8 |\n                      VEHICLE_CLUTCH_10);", "VEHICLE_CLUTCH_8);"),
    (K, "spinning kept", "WHEEL_BUMPED | WHEEL_SPINNING);", "WHEEL_BUMPED);"),
    (K, "hit kept", "        wheel->flags &= ~WHEEL_HIT;\n", ""),
    (K, "rim flag kept", "wheel->flags &= ~(WHEEL_ON_RIM | WHEEL_DETACHED);", "wheel->flags &= ~WHEEL_ON_RIM;"),
    (K, "frames hidden", "\n            frame->SetOn(true);\n", "\n"),
    (K, "rim timer", "m_sinceRimScrape = 30000;                   // (in the loop",
     "m_sinceRimScrape = 3000;                    // (in the loop"),
    (K, "skid index kept", "        wheel->skidMark = 0;\n        wheel->flags &= ~WHEEL_HIT;",
     "        wheel->flags &= ~WHEEL_HIT;"),
    (K, "ground plane", "wheel->groundPlane = {S_vector(0.0f, 1.0f, 0.0f), 0.0f};\n        wheel->groundPlaneVolume = nullptr;\n        const float reach",
     "wheel->groundPlane = {S_vector(0.0f, 0.0f, 1.0f), 0.0f};\n        wheel->groundPlaneVolume = nullptr;\n        const float reach"),
    (K, "contact reach", "        wheel->groundPlaneVolume = nullptr;\n        const float reach = wheel->unknown1E4;",
     "        wheel->groundPlaneVolume = nullptr;\n        const float reach = wheel->radius;"),
    (K, "ray end", "- XYZ(m_modelAxisY));\n    }\n    m_unknown908", "+ XYZ(m_modelAxisY));\n    }\n    m_unknown908"),
    (K, "condition not reset", "        ResetCondition();\n        RepairParts(false);", "        RepairParts(false);"),
    (K, "parts not repaired", "        RepairParts(false);\n    }\n    m_unknown228E = 0;", "    }\n    m_unknown228E = 0;"),
    (K, "repair always", "    if (repair) {\n        ResetCondition();", "    if (true) {\n        ResetCondition();"),
    (K, "lamps into the model", "lamp.projector->LinkTo(m_unknown544, 0);", "lamp.projector->LinkTo(m_model, 0);"),
    (K, "shared lamps linked", "if (!lamp.projector || (lamp.flags & VEHICLE_LAMP_SHARED))\n            continue;\n        lamp.projector->LinkTo(m_unknown544",
     "if (!lamp.projector)\n            continue;\n        lamp.projector->LinkTo(m_unknown544"),
    (K, "projector light not linked", "            m_projectors[i].light->LinkTo(m_unknown544, 0);\n", "            ;\n"),
    (K, "no wheel spin", "    UpdateWheelSpin(1.0f);\n    SetVolumesInWorld(true, true);", "    SetVolumesInWorld(true, true);"),
    (K, "no volumes", "    UpdateWheelSpin(1.0f);\n    SetVolumesInWorld(true, true);\n", "    UpdateWheelSpin(1.0f);\n"),
    (K, "contact flag left", "    m_unknown22C0 = 1;\n    m_unknown22C1 = 0;\n    m_brake", "    m_unknown22C1 = 0;\n    m_brake"),
    (K, "throttle kept", "    m_throttle = 0.0f;\n    m_rearGroundVolume = nullptr;", "    m_rearGroundVolume = nullptr;"),
    (K, "ground volume kept", "    m_frontGroundVolume = nullptr;\n    m_unknown4F8[1] = 0;\n    m_unknown4F8[0] = 0;\n}\n\n// --- the loads",
     "    m_unknown4F8[1] = 0;\n    m_unknown4F8[0] = 0;\n}\n\n// --- the loads"),
]

if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
