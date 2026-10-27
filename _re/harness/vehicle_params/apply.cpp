// Tests C_Vehicle::ApplyParams (src/game/vehicle.cpp) on the shipped cars: the game accepts them,
// so applying each without VEHICLE_PARAMS_FIX must succeed, and applying with it must change no
// value of the block (a changed value = a check the port treats as invalid). The vehicle is a test
// one: two wheels per used axle (the leading axles with a value at +0x18C), all with radius 0.4.
// Build: see build.bat. Run: apply.exe PROPS_DIR
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#include "game/pending.h"
#define private public
#include "game/vehicle.h"
#undef private

std::vector<S_TyreType> g_tyreTypes;
bool C_Vehicle::LoadSounds(const S_VehicleParams &) { return true; }
// The real constructor and destructor are engine code (game/vehicle_setup.cpp); the test vehicle
// is built in zeroed storage and these leave its plain members as they are.
C_Vehicle::C_Vehicle() {}
C_Vehicle::~C_Vehicle() {}

namespace {

std::vector<uint8_t> Load(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The model decides the axle count; the editor leaves the unused axles' values at 0 (flags aside).
int UsedAxles(const S_VehicleParams &p)
{
    int used = 0;
    while (used < VEHICLE_AXLES && p.axleUnknown18C[used] > 0.009f)
        ++used;
    return used;
}

struct S_TestVehicle {
    alignas(C_Vehicle) unsigned char storage[sizeof(C_Vehicle)] = {};
    C_Vehicle &vehicle = *new (storage) C_Vehicle();
    std::vector<S_VehicleWheel> wheels;
    std::vector<S_VehicleWheel *> pointers;

    explicit S_TestVehicle(int axles) : wheels(axles * 2), pointers(axles * 2)
    {
        vehicle.m_axleCount = axles;
        vehicle.m_wheelCount = axles * 2;
        for (int i = 0; i < axles * 2; ++i) {
            wheels[i] = S_VehicleWheel{};
            wheels[i].radius = 0.4f;
            pointers[i] = &wheels[i];
        }
        vehicle.m_wheels = pointers.data();
        vehicle.m_bodyMin = S_vector4{-1.0f, 0.0f, -2.0f, 0.0f};
        vehicle.m_bodyMax = S_vector4{1.0f, 1.5f, 2.0f, 0.0f};
        vehicle.m_unknownF74 = 0;
    }
    ~S_TestVehicle() { vehicle.~C_Vehicle(); }
    S_TestVehicle(const S_TestVehicle &) = delete;
    S_TestVehicle &operator=(const S_TestVehicle &) = delete;
};

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: apply PROPS_DIR\n");
        return 1;
    }
    int cars = 0, applied = 0, unchanged = 0;
    for (const auto &entry : std::filesystem::directory_iterator(argv[1])) {
        const std::vector<uint8_t> prop = Load(entry.path().string());
        uint32_t size = 0;
        std::memcpy(&size, &prop[4], 4);
        std::vector<uint8_t> raw;
        if (!UnpackVehicleBlob(&prop[8], size, raw) || raw.size() < VEHICLE_PARAMS_BLOB_SIZE)
            continue;
        ++cars;
        S_VehicleParams params{};
        ReadVehicleParams(raw.data(), params, false);
        const int axles = UsedAxles(params);

        S_TestVehicle a(axles);
        S_VehicleParams first = params;
        const bool ok = a.vehicle.ApplyParams(first, 0);
        if (ok)
            ++applied;

        S_TestVehicle b(axles);
        b.vehicle.m_params = params;                    // what a fix restores from
        S_VehicleParams fixed = params;
        b.vehicle.ApplyParams(fixed, VEHICLE_PARAMS_FIX);
        const uint8_t *x = reinterpret_cast<const uint8_t *>(&params);
        const uint8_t *y = reinterpret_cast<const uint8_t *>(&fixed);
        std::string changed;
        for (size_t i = 0; i < sizeof params; i += 4)
            if (std::memcmp(x + i, y + i, 4) != 0) {
                char text[16];
                std::snprintf(text, sizeof text, " +0x%03zx", i);
                changed += text;
            }
        if (changed.empty())
            ++unchanged;
        if (!ok || !changed.empty())
            std::printf("%s: axles %d, applied %d, fixed:%s\n", entry.path().filename().string().c_str(), axles, ok,
                        changed.c_str());
        if (cars == 1)
            std::printf("%s: axles %d top speed %.2f m/s gears %d driven %.0f revs idle %.1f limit %.1f /s\n",
                        entry.path().filename().string().c_str(), axles, a.vehicle.m_topSpeed,
                        a.vehicle.m_gearCount, a.vehicle.m_drivenWheels, a.vehicle.m_idleRevs,
                        a.vehicle.m_revLimit);
    }
    std::printf("cars %d: applied %d, nothing to fix %d\n", cars, applied, unchanged);
    return 0;
}
