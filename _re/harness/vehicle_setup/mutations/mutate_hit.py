"""Mutations of src/game/vehicle_drive.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\vehicle_drive.cpp")

MUTATIONS = [
    # AddForce
    (K, "force flag", "    if (!(flags & 0x1))\n        m_unknown270 += force;", "    if (!(flags & 0x2))\n        m_unknown270 += force;"),
    (K, "force sign", "        m_unknown270 += force;", "        m_unknown270 -= force;"),
    (K, "torque flag", "    if (!(flags & 0x2)) {\n        // The torque in the body's space",
     "    if (!(flags & 0x1)) {\n        // The torque in the body's space"),
    (K, "torque cross order", "S_vector torque = force.Cross(point - XYZ(m_unknown1B0));",
     "S_vector torque = (point - XYZ(m_unknown1B0)).Cross(force);"),
    (K, "torque arm", "S_vector torque = force.Cross(point - XYZ(m_unknown1B0));", "S_vector torque = force.Cross(point);"),
    (K, "torque into world", "        torque *= m_unknown350;\n        torque = S_vector(torque.x * factors.x",
     "        torque *= m_unknown310;\n        torque = S_vector(torque.x * factors.x"),
    (K, "torque factors", "torque = S_vector(torque.x * factors.x, torque.y * factors.y, torque.z * factors.z);",
     "torque = S_vector(torque.x * factors.x, torque.y * factors.z, torque.z * factors.y);"),
    (K, "torque not back", "        torque *= m_unknown310;\n        m_unknown280 += torque;", "        m_unknown280 += torque;"),
    (K, "torque sign", "        m_unknown280 += torque;", "        m_unknown280 -= torque;"),
    (K, "force woken", "        m_unknown280 += torque;\n    }\n    m_unknown22C0 = 1;\n", "        m_unknown280 += torque;\n    }\n"),
    (K, "force callback",
     "    m_unknown22C0 = 1;\n    if (auto callback = reinterpret_cast<T_VehicleCallback>(m_callbacks[VEHICLE_CALLBACK_IMPULSE]))\n"
     "        callback(m_callbackContext);\n}\n\nvoid C_Vehicle::ApplyCarHit",
     "    m_unknown22C0 = 1;\n}\n\nvoid C_Vehicle::ApplyCarHit"),
    # ApplyCarHit: the motion
    (K, "hit arm", "const S_vector arm = XYZ(point) - XYZ(m_unknown1B0);", "const S_vector arm = XYZ(point);"),
    (K, "hit scale", "const S_vector push = impulse * scale;", "const S_vector push = impulse;"),
    (K, "hit velocity unscaled", "push * (1.0f / m_currentMass)", "impulse * (1.0f / m_currentMass)"),
    (K, "hit cross order", "S_vector turn = arm.Cross(push);", "S_vector turn = push.Cross(arm);"),
    (K, "hit inertia", "turn = S_vector(turn.x * m_unknown220.x, turn.y * m_unknown220.y, turn.z * m_unknown220.z);",
     "turn = S_vector(turn.x * m_unknown220.x, turn.y * m_unknown220.x, turn.z * m_unknown220.x);"),
    (K, "hit turn into world", "    turn *= m_unknown350;\n    turn = S_vector(turn.x * m_unknown220.x",
     "    turn *= m_unknown310;\n    turn = S_vector(turn.x * m_unknown220.x"),
    (K, "hit turn sign", "    m_unknown210 -= turn;\n\n    // The push's size", "    m_unknown210 += turn;\n\n    // The push's size"),
    # the size
    (K, "size bits", "    if (squareBits < 0x322bcc77)\n        size = 0.0f;", "    if (square < 0.0f)\n        size = 0.0f;"),
    (K, "size sqrt", "        size = std::sqrt(square);", "        size = square;"),
    # the dent
    (K, "dent flag", "    if (m_unknownF6C & VEHICLE_SIM_DEFORM) {", "    if (m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE) {"),
    (K, "dent direction", "const S_vector against = -normal;", "const S_vector against = normal;"),
    (K, "dent radius", "0.0f}, 0.45f, size, 0, nullptr);", "0.0f}, 0.5f, size, 0, nullptr);"),
    (K, "dent strength", "0.0f}, 0.45f, size, 0, nullptr);", "0.0f}, 0.45f, size * 0.9f, 0, nullptr);"),
    (K, "dent point", "Deform(point, S_vector4{", "Deform(m_unknown1B0, S_vector4{"),
    # the wear
    (K, "wear flag", "    if (m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE) {\n        ImpactDamage(size);",
     "    if (m_unknownF6C & VEHICLE_SIM_DEFORM) {\n        ImpactDamage(size);"),
    (K, "wear size", "        ImpactDamage(size);", "        ImpactDamage(square);"),
    (K, "wear after callback",
     "        ImpactDamage(size);\n        if (auto callback = reinterpret_cast<T_VehicleImpactCallback>(m_callbacks[VEHICLE_CALLBACK_IMPACT]))\n"
     "            callback(m_callbackContext, &push, &push, &point, 0.45f, reinterpret_cast<const S_vector *>(&m_velocity),\n"
     "                     other, nullptr, 1);\n",
     "        if (auto callback = reinterpret_cast<T_VehicleImpactCallback>(m_callbacks[VEHICLE_CALLBACK_IMPACT]))\n"
     "            callback(m_callbackContext, &push, &push, &point, 0.45f, reinterpret_cast<const S_vector *>(&m_velocity),\n"
     "                     other, nullptr, 1);\n        ImpactDamage(size);\n"),
    (K, "impact radius", "&push, &push, &point, 0.45f,", "&push, &push, &point, 0.5f,"),
    (K, "impact largest", "callback(m_callbackContext, &push, &push, &point", "callback(m_callbackContext, &push, &impulse, &point"),
    (K, "impact velocity", "reinterpret_cast<const S_vector *>(&m_velocity),", "reinterpret_cast<const S_vector *>(&m_velocityDir),"),
    (K, "impact actor", "                     other, nullptr, 1);", "                     nullptr, nullptr, 1);"),
    (K, "impact last", "                     other, nullptr, 1);", "                     other, nullptr, 0);"),
    # the direction, the flag, the callback
    (K, "dir early", "    // The push's size (the SDK's", "    m_velocityDir = m_velocity;\n    // The push's size (the SDK's"),
    (K, "dir copy", "\n    m_velocityDir = m_velocity;\n    S_vector dir", "\n    S_vector dir"),
    (K, "dir copy xyz", "\n    m_velocityDir = m_velocity;\n    S_vector dir",
     "\n    SetXYZ(m_velocityDir, XYZ(m_velocity));\n    S_vector dir"),
    (K, "dir not normalized", "    dir.Normalize();\n    SetXYZ(m_velocityDir, dir);\n    m_unknown22C0 = 1;",
     "    SetXYZ(m_velocityDir, dir);\n    m_unknown22C0 = 1;"),
    (K, "hit woken", "    SetXYZ(m_velocityDir, dir);\n    m_unknown22C0 = 1;", "    SetXYZ(m_velocityDir, dir);"),
    (K, "hit callback",
     "    m_unknown22C0 = 1;\n    if (auto callback = reinterpret_cast<T_VehicleCallback>(m_callbacks[VEHICLE_CALLBACK_IMPULSE]))\n"
     "        callback(m_callbackContext);\n}\n\n// --- Reset",
     "    m_unknown22C0 = 1;\n}\n\n// --- Reset"),
]

if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
