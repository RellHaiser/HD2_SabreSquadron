"""Mutations of the car's helpers (part 1e) for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
SRC = os.path.join(ROOT, "src", "game")
C = os.path.join(SRC, "vehicle_collide.cpp")
L = os.path.join(SRC, "vehicle_lamps.cpp")
S = os.path.join(SRC, "vehicle_start.cpp")
D = os.path.join(SRC, "vehicle_drive.cpp")
H = os.path.join(SRC, "vehicle.h")

MUTATIONS = [
    # GetCameraOffset, SetCallback
    (D, "camera axis", "    offset->y = m_unknown67C;\n    offset->z = 0.0f;", "    offset->y = 0.0f;\n    offset->z = m_unknown67C;"),
    (H, "callback slot", "void SetCallback(int slot, void *callback) { m_callbacks[slot] = callback; }",
     "void SetCallback(int slot, void *callback) { m_callbacks[slot + 1] = callback; }"),
    # GetContactResponse
    (C, "contact arm", "const S_vector lever = XYZ(point) - XYZ(m_unknown1B0);", "const S_vector lever = XYZ(point) - XYZ(m_unknown1E0);"),
    (C, "contact spin sign", "SetXYZ(*velocity, XYZ(m_velocity) - m_unknown210.Cross(lever));", "SetXYZ(*velocity, XYZ(m_velocity) + m_unknown210.Cross(lever));"),
    (C, "contact spin order", "SetXYZ(*velocity, XYZ(m_velocity) - m_unknown210.Cross(lever));", "SetXYZ(*velocity, XYZ(m_velocity) - lever.Cross(m_unknown210));"),
    (C, "contact velocity w", "    *velocity = m_velocity;\n    SetXYZ(*velocity", "    *velocity = {};\n    SetXYZ(*velocity"),
    (C, "contact mass", "    *mass = m_currentMass;\n    // The turn per impulse", "    *mass = m_mass;\n    // The turn per impulse"),
    (C, "contact cross order", "S_vector t = lever.Cross(normal);", "S_vector t = normal.Cross(lever);"),
    (C, "contact body space", "    t *= m_unknown350;\n    t = S_vector(m_unknown220.x", "    t *= m_unknown310;\n    t = S_vector(m_unknown220.x"),
    (C, "contact inertia", "t = S_vector(m_unknown220.x * t.x, m_unknown220.y * t.y, m_unknown220.z * t.z);",
     "t = S_vector(m_unknown220.x * t.x, m_unknown220.z * t.y, m_unknown220.y * t.z);"),
    (C, "contact back", "    t *= m_unknown310;\n    *turn = {t.x, t.y, t.z, 0.0f};", "    *turn = {t.x, t.y, t.z, 0.0f};"),
    # SetBeamScale
    (L, "beam bits", "{VEHICLE_LIGHT_FLASH_BEAMS, {VEHICLE_LAMP_LI_LEFT, VEHICLE_LAMP_LI_RIGHT}},", "{VEHICLE_LIGHT_REAR_BEAMS, {VEHICLE_LAMP_LI_LEFT, VEHICLE_LAMP_LI_RIGHT}},"),
    (L, "beam right kind", "{VEHICLE_LIGHT_INDICATOR_BEAMS, {VEHICLE_LAMP_IN_LEFT, VEHICLE_LAMP_IN_RIGHT}},", "{VEHICLE_LIGHT_INDICATOR_BEAMS, {VEHICLE_LAMP_IN_LEFT, VEHICLE_LAMP_IN_LEFT}},"),
    (L, "beam reverse kind", "{VEHICLE_LIGHT_REVERSE_BEAMS, {VEHICLE_LAMP_BA, VEHICLE_LAMP_BA}},", "{VEHICLE_LIGHT_REVERSE_BEAMS, {VEHICLE_LAMP_BR, VEHICLE_LAMP_BR}},"),
    (L, "beam all", "        if (!(beams & group.beam))\n            continue;\n        for (size_t i = m_lamps.size()", "        for (size_t i = m_lamps.size()"),
    (L, "beam scale", "                lamp.projector->SetScale(scale);", "                lamp.projector->SetScale(S_vector(scale.x, scale.x, scale.z));"),
    # ReadModelState
    (S, "state inverse", "    InvertInto(m_invModelMatrix, m_model->GetMatrix());\n    m_unknown310 = m_modelMatrix;\n    m_unknown310.m[3][0] = m_unknown310.m[3][1] = m_unknown310.m[3][2] = 0.0f;\n    m_unknown350 = m_invModelMatrix;\n    m_unknown350.m[3][0] = m_unknown350.m[3][1] = m_unknown350.m[3][2] = 0.0f;\n    m_modelPos = Row(m_model->GetMatrix(), 3);\n    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_modelMatrix);\n    m_modelAxisX",
     "    m_unknown310 = m_modelMatrix;\n    m_unknown310.m[3][0] = m_unknown310.m[3][1] = m_unknown310.m[3][2] = 0.0f;\n    m_unknown350 = m_invModelMatrix;\n    m_unknown350.m[3][0] = m_unknown350.m[3][1] = m_unknown350.m[3][2] = 0.0f;\n    m_modelPos = Row(m_model->GetMatrix(), 3);\n    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_modelMatrix);\n    m_modelAxisX"),
    (S, "state rotation kept", "    m_unknown350.m[3][0] = m_unknown350.m[3][1] = m_unknown350.m[3][2] = 0.0f;\n    m_modelPos = Row(m_model->GetMatrix(), 3);",
     "    m_modelPos = Row(m_model->GetMatrix(), 3);"),
    (S, "state position", "    m_modelPos = Row(m_model->GetMatrix(), 3);\n    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_modelMatrix);\n    m_modelAxisX = Row(m_modelMatrix, 0);\n    m_modelAxisY = Row(m_modelMatrix, 1);\n    m_modelAxisZ = Row(m_modelMatrix, 2);\n    m_velocityDir",
     "    m_modelPos = Row(m_model->GetMatrix(), 2);\n    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_modelMatrix);\n    m_modelAxisX = Row(m_modelMatrix, 0);\n    m_modelAxisY = Row(m_modelMatrix, 1);\n    m_modelAxisZ = Row(m_modelMatrix, 2);\n    m_velocityDir"),
    (S, "state centre", "    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_modelMatrix);\n    m_modelAxisX = Row(m_modelMatrix, 0);\n    m_modelAxisY = Row(m_modelMatrix, 1);\n    m_modelAxisZ = Row(m_modelMatrix, 2);\n    m_velocityDir",
     "    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_invModelMatrix);\n    m_modelAxisX = Row(m_modelMatrix, 0);\n    m_modelAxisY = Row(m_modelMatrix, 1);\n    m_modelAxisZ = Row(m_modelMatrix, 2);\n    m_velocityDir"),
    (S, "state axes", "    m_modelAxisY = Row(m_modelMatrix, 1);\n    m_modelAxisZ = Row(m_modelMatrix, 2);\n    m_velocityDir", "    m_modelAxisY = Row(m_modelMatrix, 2);\n    m_modelAxisZ = Row(m_modelMatrix, 1);\n    m_velocityDir"),
    (S, "state direction", "    S_vector direction = XYZ(m_velocityDir);\n    direction.Normalize();\n    SetXYZ(m_velocityDir, direction);\n    m_unknown229F = 0;",
     "    m_unknown229F = 0;"),
    (S, "state flag", "    m_unknown229F = 0;\n    m_speed = XYZ(m_velocity).Magnitude();", "    m_speed = XYZ(m_velocity).Magnitude();"),
    (S, "state speed", "    m_speed = XYZ(m_velocity).Magnitude();\n}\n", "    m_speed = XYZ(m_velocity).Square();\n}\n"),
]


if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
