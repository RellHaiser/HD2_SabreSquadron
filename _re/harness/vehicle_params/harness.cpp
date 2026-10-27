// Tests src/game/vehicle_params.cpp:
//  1. the shipped cars' properties (chunk 0xAE24 payloads: version, size, COMP blob): unpack,
//     pack again (must be byte-identical), read the block and write it back (must give the same
//     blob but for the unused gaps and what follows each name's terminator);
//  2. the field map: a marked byte at each blob position must land where the original reader
//     (0x004d7d80) puts it (params_map.txt: blob offset, struct offset, size or "str");
//  3. the tyre index option against a made-up tyre table.
//  4. (with ORIG_DIR) the block read from each car against the original reader's, simulated from
//     its operations in order (lstrcpyA unbounded) by origread.py.
// The inputs come from the shipped missions (H:\HD2_Unpacked\MISSIONS\*\actors.bin) via
// carparams.py; params_map.txt from running the reader / writer symbolically (symx86.py).
// Build: see build.bat. Run: harness.exe PROPS_DIR params_map.txt [ORIG_DIR]
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "game/pending.h"                               // the stub: S_TyreType, g_tyreTypes
#include "game/vehicle_params.h"

std::vector<S_TyreType> g_tyreTypes;

namespace {

std::vector<uint8_t> Load(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct S_MapEntry {
    int blob, offset, size;                             // size 0: a name slot
};

std::vector<S_MapEntry> LoadMap(const char *path)
{
    std::vector<S_MapEntry> map;
    FILE *f = std::fopen(path, "r");
    char kind[16];
    S_MapEntry e;
    while (f && std::fscanf(f, "%x %x %15s", &e.blob, &e.offset, kind) == 3) {
        e.size = std::strcmp(kind, "str") == 0 ? 0 : std::atoi(kind);
        map.push_back(e);
    }
    if (f)
        std::fclose(f);
    return map;
}

void TestTyres()
{
    g_tyreTypes.assign(4, S_TyreType{});
    const int ids[4] = {10, 20, 20, 30};
    for (int i = 0; i < 4; ++i)
        g_tyreTypes[i].id = ids[i];
    S_VehicleParams params{};
    const int types[VEHICLE_AXLES] = {20, 99, 10, 30};
    std::memcpy(params.axleTyreType, types, sizeof types);
    std::vector<uint8_t> blob(VEHICLE_PARAMS_BLOB_SIZE);
    WriteVehicleParams(blob.data(), params, true);
    int stored[4];
    for (int a = 0; a < 4; ++a)
        std::memcpy(&stored[a], &blob[0x15C + 0x70 * a + 0x20], 4);
    // An index out of range reads as 0.
    const int bad[2] = {5, -1};
    std::memcpy(&blob[0x15C + 0x70 * 4 + 0x20], &bad[0], 4);
    std::memcpy(&blob[0x15C + 0x70 * 5 + 0x20], &bad[1], 4);
    S_VehicleParams back{};
    ReadVehicleParams(blob.data(), back, true);
    std::printf("tyres: written indices %d %d %d %d (want 2 0 0 3), read ids %d %d %d %d %d %d (want 20 10 10 30 0 0)\n",
                stored[0], stored[1], stored[2], stored[3], back.axleTyreType[0], back.axleTyreType[1],
                back.axleTyreType[2], back.axleTyreType[3], back.axleTyreType[4], back.axleTyreType[5]);
    g_tyreTypes.clear();
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::printf("usage: harness PROPS_DIR params_map.txt\n");
        return 1;
    }
    const std::vector<S_MapEntry> map = LoadMap(argv[2]);
    std::printf("field map: %zu entries\n", map.size());

    // --- 2. the field map -------------------------------------------------------------
    std::map<int, int> expected;                        // blob byte -> struct byte
    std::vector<bool> nameByte(VEHICLE_PARAMS_BLOB_SIZE, false);
    for (const S_MapEntry &e : map) {
        if (e.size == 0) {
            expected[e.blob] = e.offset;                // a name: only its first byte maps on its own
            for (int i = 0; i < VEHICLE_SOUND_NAME; ++i)
                nameByte[e.blob + i] = true;
        } else {
            for (int i = 0; i < e.size; ++i)
                expected[e.blob + i] = e.offset + i;
        }
    }
    int mapErrors = 0;
    std::vector<uint8_t> blob(VEHICLE_PARAMS_BLOB_SIZE);
    for (int k = 0; k < static_cast<int>(VEHICLE_PARAMS_BLOB_SIZE); ++k) {
        std::fill(blob.begin(), blob.end(), 0);
        blob[k] = 0xA5;
        S_VehicleParams params;
        std::memset(&params, 0, sizeof params);
        ReadVehicleParams(blob.data(), params, false);
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&params);
        std::vector<int> hits;
        for (int s = 0; s < static_cast<int>(sizeof params); ++s)
            if (bytes[s] == 0xA5)
                hits.push_back(s);
        auto it = expected.find(k);
        const bool ok = it == expected.end() ? hits.empty() : (hits.size() == 1 && hits[0] == it->second);
        if (!ok && mapErrors++ < 20)
            std::printf("  map: blob 0x%04x -> %zu hits (first 0x%04x), want 0x%04x\n", k, hits.size(),
                        hits.empty() ? -1 : hits[0], it == expected.end() ? -1 : it->second);
    }
    // ... and back: a marked struct byte must land at its blob byte.
    for (const auto &pair : expected) {
        S_VehicleParams params;
        std::memset(&params, 0, sizeof params);
        reinterpret_cast<uint8_t *>(&params)[pair.second] = 0xA5;
        WriteVehicleParams(blob.data(), params, false);
        int hits = 0, at = -1;
        for (int b = 0; b < static_cast<int>(VEHICLE_PARAMS_BLOB_SIZE); ++b)
            if (blob[b] == 0xA5) {
                ++hits;
                at = b;
            }
        if ((hits != 1 || at != pair.first) && mapErrors++ < 40)
            std::printf("  map back: struct 0x%04x -> %d hits (0x%04x), want 0x%04x\n", pair.second, hits, at,
                        pair.first);
    }
    std::printf("field map errors: %d\n", mapErrors);

    // --- 3. tyre indices ----------------------------------------------------------------
    TestTyres();

    // --- 1. the shipped cars --------------------------------------------------------------
    int files = 0, repacked = 0, sameBlob = 0, sameParams = 0, unterminated = 0, sameAsOriginal = 0;
    bool shown = false;
    for (const auto &entry : std::filesystem::directory_iterator(argv[1])) {
        const std::vector<uint8_t> prop = Load(entry.path().string());
        ++files;
        uint32_t version = 0, size = 0;
        std::memcpy(&version, &prop[0], 4);
        std::memcpy(&size, &prop[4], 4);
        if (version != VEHICLE_PROPERTIES_VERSION || 8 + size != prop.size()) {
            std::printf("%s: version %u size %u (%zu bytes)\n", entry.path().filename().string().c_str(), version,
                        size, prop.size());
            continue;
        }
        std::vector<uint8_t> raw, packed;
        if (!UnpackVehicleBlob(&prop[8], size, raw)) {
            std::printf("%s: not COMP\n", entry.path().filename().string().c_str());
            continue;
        }
        PackVehicleBlob(raw.data(), static_cast<uint32_t>(raw.size()), packed);
        if (packed == std::vector<uint8_t>(prop.begin() + 8, prop.end()))
            ++repacked;
        else
            std::printf("%s: repacked differently (%zu vs %u bytes)\n", entry.path().filename().string().c_str(),
                        packed.size(), size);
        if (raw.size() < VEHICLE_PARAMS_BLOB_SIZE) {
            std::printf("%s: blob too short (%zu)\n", entry.path().filename().string().c_str(), raw.size());
            continue;
        }

        S_VehicleParams params{};
        ReadVehicleParams(raw.data(), params, false);
        WriteVehicleParams(blob.data(), params, false);
        int diffs = 0;
        for (uint32_t b = 0; b < VEHICLE_PARAMS_BLOB_SIZE; ++b) {
            if ((b >= 0x75E && b < 0x76E) || b == 0x8D6)
                continue;                               // the unused gaps
            if (nameByte[b])
                continue;                               // names: compared below
            if (blob[b] != raw[b])
                ++diffs;
        }
        for (const S_MapEntry &e : map) {
            if (e.size)
                continue;
            int len = 0;
            while (len < VEHICLE_SOUND_NAME && raw[e.blob + len])
                ++len;
            if (len == VEHICLE_SOUND_NAME) {
                ++unterminated;
                continue;
            }
            if (std::memcmp(&raw[e.blob], &blob[e.blob], len + 1) != 0)
                ++diffs;
        }
        if (diffs == 0)
            ++sameBlob;
        else
            std::printf("%s: %d blob bytes differ after read + write\n", entry.path().filename().string().c_str(),
                        diffs);
        S_VehicleParams again{};
        ReadVehicleParams(blob.data(), again, false);
        if (std::memcmp(&params, &again, sizeof params) == 0)
            ++sameParams;
        // Against the original reader's struct (simulated in Python: origread.py): the same
        // bytes but for what follows each name's end, and the names as the game reads them.
        if (argc > 3) {
            std::string stem = entry.path().stem().string();
            const std::vector<uint8_t> original = Load(std::string(argv[3]) + "/" + stem + ".struct");
            const uint8_t *mine = reinterpret_cast<const uint8_t *>(&params);
            if (original.size() != sizeof params) {
                std::printf("%s: no original struct\n", stem.c_str());
            } else {
                std::vector<bool> inName(sizeof params, false);
                for (const S_MapEntry &e : map)
                    if (e.size == 0)
                        for (int i = 0; i < VEHICLE_SOUND_NAME; ++i)
                            inName[e.offset + i] = true;
                int differ = 0;
                for (size_t i = 0; i < sizeof params; ++i)
                    if (!inName[i] && mine[i] != original[i])
                        ++differ;
                for (const S_MapEntry &e : map) {
                    if (e.size)
                        continue;
                    size_t i = e.offset;
                    while (i < sizeof params && original[i] && mine[i] == original[i])
                        ++i;
                    if (i < sizeof params && mine[i] != original[i])
                        ++differ;
                }
                if (differ == 0)
                    ++sameAsOriginal;
                else
                    std::printf("%s: %d bytes differ from the original reader's\n", stem.c_str(), differ);
            }
        }
        if (!shown) {
            shown = true;
            std::printf("%s: mass %g gravity %g centre %g %g %g inertia %g %g %g km/h %g glass '%s' sounds2 '%s' "
                        "'%s' layers2A '%s' '%s' crash2A '%s' tyre %d\n",
                        entry.path().filename().string().c_str(), params.mass, params.gravity,
                        params.centreOfMass[0], params.centreOfMass[1], params.centreOfMass[2], params.inertia[0],
                        params.inertia[1], params.inertia[2], params.unknown6A8, params.glassTexture,
                        params.sounds2[0], params.sounds2[1], params.layers2A[0], params.layers2A[1],
                        params.crash2A[0], params.axleTyreType[0]);
        }
    }
    std::printf("cars %d: repacked identical %d, read+write identical %d, read(write(read)) same %d, "
                "as the original reader %d, unterminated names %d\n",
                files, repacked, sameBlob, sameParams, sameAsOriginal, unterminated);
    return 0;
}
