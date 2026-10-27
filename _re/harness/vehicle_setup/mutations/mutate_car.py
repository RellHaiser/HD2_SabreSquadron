"""Mutations of src/game/car.cpp (part 2a) for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\car.cpp")
P = os.path.join(ROOT, r"src\game\perf_timer.cpp")

MUTATIONS = [
    # the timer
    (P, "timer base", "    QueryPerformanceCounter(&g_perfBase);\n", ""),
    (P, "timer scale", "return static_cast<int64_t>(seconds * 1000.0f);", "return static_cast<int64_t>(seconds * 100.0f);"),
    # the creator, the lifetime
    (K, "creator registration", "    g_actorCreators[ACTOR_CAR] = &g_carCreator;", "    g_actorCreators[ACTOR_CAR + 1] = &g_carCreator;"),
    (K, "callback context", "    m_vehicle.m_callbackContext = this;\n", ""),
    (K, "name", "m_name = g_texts.Get(0x1bf0);", "m_name = g_texts.Get(0x1bf1);"),
    (K, "not listed", "    g_cars.push_back(this);\n", ""),
    (K, "list order kept", "            g_cars[i] = g_cars.back();\n            g_cars.pop_back();",
     "            g_cars.erase(g_cars.begin() + static_cast<std::ptrdiff_t>(i));"),
    (K, "not unlisted", "    for (size_t i = g_cars.size(); i-- > 0;) {\n        if (g_cars[i] == this) {", "    for (size_t i = 0; i < 0; ++i) {\n        if (g_cars[i] == this) {"),
    (K, "parts kept", "    m_parts.Release();\n    // Out of the list", "    // Out of the list"),
    # (The damage parts' Release moved to car_parts.cpp: mutate_parts.py.)
    (K, "seat count", "    return static_cast<int>(m_vehicle.m_seats.size());", "    return static_cast<int>(m_vehicle.m_seats.size()) + 1;"),
    # SetFrame
    (K, "started kept", "    m_started = false;\n    std::fill", "    std::fill"),
    (K, "seat weapons kept", "    std::fill(std::begin(m_seatWeapons), std::end(m_seatWeapons), nullptr);\n", ""),
    (K, "setup ignored", "    if (!C_Actor::SetFrame(frame) || !m_vehicle.Setup(frame))\n        return false;",
     "    if (!C_Actor::SetFrame(frame))\n        return false;\n    m_vehicle.Setup(frame);"),
    (K, "response slot", "SetCallback(VEHICLE_CALLBACK_ACTOR_RESPONSE, reinterpret_cast<void *>(&CarOnBodyActorResponse));",
     "SetCallback(VEHICLE_CALLBACK_ACTOR_RESPONSE, reinterpret_cast<void *>(&CarOnBodyActorImpulse));"),
    (K, "glass slot", "SetCallback(VEHICLE_CALLBACK_GLASS_BROKEN, reinterpret_cast<void *>(&CarOnGlassBroken));",
     "SetCallback(VEHICLE_CALLBACK_GLASS_BROKEN, nullptr);"),
    (K, "impulse slot", "m_vehicle.SetCallback(VEHICLE_CALLBACK_IMPULSE, nullptr);",
     "m_vehicle.SetCallback(VEHICLE_CALLBACK_IMPULSE, reinterpret_cast<void *>(&CarOnRepaired));"),
    (K, "ground slots", "SetCallback(VEHICLE_CALLBACK_WHEEL_GROUND, reinterpret_cast<void *>(&CarOnWheelGround));",
     "SetCallback(VEHICLE_CALLBACK_WHEEL_GROUND, reinterpret_cast<void *>(&CarOnBodyGround));"),
    (K, "emitter slot", "SetCallback(VEHICLE_CALLBACK_REUSE_EMITTER, reinterpret_cast<void *>(&ReuseCarEmitter));",
     "SetCallback(VEHICLE_CALLBACK_SETUP_EMITTER + 2, reinterpret_cast<void *>(&ReuseCarEmitter));"),
    (K, "parts not set up", "    m_parts.Setup(this);\n", ""),
    # the properties
    (K, "version written", "constexpr int CAR_PROPERTIES_VERSION = 3;", "constexpr int CAR_PROPERTIES_VERSION = 4;"),
    (K, "size not written", "    chunk.Write(size);\n", ""),
    (K, "write order", "    chunk.Write(version);\n    chunk.Write(size);", "    chunk.Write(size);\n    chunk.Write(version);"),
    (K, "version check", "    if (version != CAR_PROPERTIES_VERSION)\n        return false;", "    if (version < CAR_PROPERTIES_VERSION)\n        return false;"),
    (K, "not applied", "    m_vehicle.ApplyParams(params, 0);\n    return true;\n}\n\nvoid C_Car::CopyFrom", "    return true;\n}\n\nvoid C_Car::CopyFrom"),
    # CopyFrom
    (K, "copy frame", "    C_Actor::SetFrame(source->GetFrame());\n", ""),
    (K, "copy type", "    if (source->GetType() != ACTOR_CAR)\n        return;\n    C_Actor::CopyFrom(source);", "    C_Actor::CopyFrom(source);"),
    (K, "copy actor", "    C_Actor::CopyFrom(source);\n    S_VehicleParams params;", "    S_VehicleParams params;"),
    (K, "copy params", "    m_vehicle.ApplyParams(params, 0);\n}", "}"),
]


if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
