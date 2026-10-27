// Harness for src/game/car_tables.cpp (the vehicles' tyre and surface tables). The data files
// come from the folder given as the first argument (tables\tyres.bin, tables\hdmaterials.def);
// synthetic files are written to the second (a scratch folder). Every value is printed as its
// bits; check.py works the same lines out from the files and compares.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "game/car_tables.h"
#include "game/shots.h"
#include "ls3d/rw_data.h"

S_MaterialDef *g_materials;
uint32_t g_materialCount;

// --- the data files: dtaOpen maps "tables\tyres.bin" to g_tyresPath --------------------------------

std::string g_tyresPath;
std::map<int, FILE *> g_open;
int g_nextHandle = 1, g_opens;

int __stdcall dtaOpen(const char *fileName, bool)
{
    ++g_opens;
    if (std::strcmp(fileName, "tables\\tyres.bin") != 0)
        return -1;
    FILE *f = std::fopen(g_tyresPath.c_str(), "rb");
    if (!f)
        return -1;
    g_open[g_nextHandle] = f;
    return g_nextHandle++;
}
int __stdcall dtaRead(int handle, void *buffer, int size)
{
    return static_cast<int>(std::fread(buffer, 1, static_cast<size_t>(size), g_open.at(handle)));
}
bool __stdcall dtaClose(int handle)
{
    std::fclose(g_open.at(handle));
    g_open.erase(handle);
    return true;
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

void PrintTyres(const char *label)
{
    std::printf("%s: types %zu\n", label, g_tyreTypes.size());
    for (size_t t = 0; t < g_tyreTypes.size(); ++t) {
        const S_TyreType &type = g_tyreTypes[t];
        char name[0x21] = {};
        std::memcpy(name, type.name, 0x20);
        std::printf("type %zu id %d name '%s' entries %d\n", t, type.id, name,
                    type.surfaces ? static_cast<int>(type.surfaces->size()) : -1);
        if (!type.surfaces)
            continue;
        for (size_t e = 0; e < type.surfaces->size(); ++e) {
            const S_TyreSurface &s = (*type.surfaces)[e];
            std::printf("entry %zu %zu surface %d", t, e, s.surface);
            for (const float *v = &s.unknown04; v <= &s.unknown30; ++v)
                std::printf(" %08X", Bits(*v));
            std::printf("\n");
        }
    }
}

void PrintSurfaces(const char *label)
{
    std::printf("%s: surfaces %zu\n", label, g_carSurfaces.size());
    for (const S_CarSurface &s : g_carSurfaces)
        std::printf("surface %d %08X %08X %08X %08X %08X %08X %08X %d %d %d %d %d\n", s.material, Bits(s.unknown04),
                    Bits(s.unknown08), Bits(s.unknown0C), Bits(s.unknown10), Bits(s.unknown14), Bits(s.friction),
                    Bits(s.bounce), s.dustEffect, s.skidEffect, s.contactEffect, s.tyreSqueal, s.skidMarks);
    std::printf("%s map:", label);
    for (int i = 0; i < 256; ++i)
        std::printf(" %d", g_carSurfaceOfMaterial[i]);
    std::printf("\n");
}

// A synthetic tyres.bin: a type without entries, one with a short entry (two floats), one of 13.
void WriteSynthetic(const std::string &path, int version)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    const auto put = [&](const void *p, size_t n) { std::fwrite(p, 1, n, f); };
    const auto putInt = [&](int v) { put(&v, 4); };
    putInt(version);
    putInt(3);
    char name[0x20] = {};
    // Without entries.
    putInt(0);
    std::strcpy(name, "empty");
    put(name, 0x20);
    putInt(7);
    putInt(0);
    // A short entry and a full one.
    for (int t = 0; t < 2; ++t) {
        putInt(0);
        std::memset(name, 0, sizeof name);
        std::strcpy(name, t ? "full" : "short");
        put(name, 0x20);
        putInt(100 + t);
        putInt(t ? 2 : 1);
        for (int e = 0; e <= t; ++e) {
            putInt(30 + e);
            const int floats = t ? 13 : 2;
            putInt(floats * 4);
            for (int k = 0; k < floats; ++k) {
                const float v = static_cast<float>(k + 1) * 0.5f + static_cast<float>(e);
                put(&v, 4);
            }
        }
    }
    std::fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "harness <game folder> <scratch folder>\n");
        return 2;
    }
    const std::string game = argv[1], scratch = argv[2];

    // --- the tyres ---
    g_tyresPath = game + "\\tables\\tyres.bin";
    std::printf("load real: %d open %zu\n", LoadTyres(), g_open.size());
    PrintTyres("real");
    std::vector<const void *> tables;
    for (const S_TyreType &t : g_tyreTypes)
        tables.push_back(t.surfaces);
    std::printf("load again: %d\n", LoadTyres());
    bool fresh = g_tyreTypes.size() == tables.size();
    for (size_t i = 1; fresh && i < tables.size(); ++i)
        fresh = g_tyreTypes[i].surfaces && g_tyreTypes[i].surfaces != tables[i];
    std::printf("again types %zu fresh tables %d\n", g_tyreTypes.size(), fresh);
    g_tyresPath = scratch + "\\missing.bin";
    std::printf("load missing: %d open %zu\n", LoadTyres(), g_open.size());
    PrintTyres("missing");
    WriteSynthetic(scratch + "\\version2.bin", 2);
    g_tyresPath = scratch + "\\version2.bin";
    std::printf("load version 2: %d open %zu\n", LoadTyres(), g_open.size());
    PrintTyres("version 2");
    WriteSynthetic(scratch + "\\synthetic.bin", 1);
    g_tyresPath = scratch + "\\synthetic.bin";
    std::printf("load synthetic: %d open %zu\n", LoadTyres(), g_open.size());
    PrintTyres("synthetic");
    FreeTyres();
    std::printf("freed: types %zu\n", g_tyreTypes.size());

    // --- the surfaces ---
    for (int &m : g_carSurfaceOfMaterial)
        m = 77;
    g_materialCount = 0;
    std::printf("init none: %d\n", InitCarSurfaces());
    PrintSurfaces("none");
    std::vector<S_MaterialDef> materials;
    if (FILE *f = std::fopen((game + "\\tables\\hdmaterials.def").c_str(), "rb")) {
        S_MaterialDef m;
        while (std::fread(&m, sizeof m, 1, f) == 1)
            materials.push_back(m);
        std::fclose(f);
    }
    g_materials = materials.data();
    g_materialCount = static_cast<uint32_t>(materials.size());
    std::printf("init real: %d\n", InitCarSurfaces());
    PrintSurfaces("real");
    // Synthetic: every clamp both ways, the minimums both ways.
    std::vector<S_MaterialDef> made(100);
    for (int i = 0; i < 100; ++i) {
        S_MaterialDef &m = made[i];
        std::memset(&m, 0, sizeof m);
        m.carFriction = static_cast<float>(i % 7 - 3) * 0.5f;
        m.carBounce = static_cast<float>(i % 5) * 0.75f;
        m.carUnknownE0 = static_cast<float>(i % 4);
        m.carUnknownE4 = static_cast<float>(i * 3 % 5);
        m.carUnknownE8 = static_cast<float>(i % 3);
        m.carUnknownEC = static_cast<float>(i * 2 % 4);
        m.carUnknownF0 = static_cast<float>(i % 6 - 2) * 0.125f;
        m.carDustEffect = i;
        m.carSkidEffect = i + 1;
        m.carContactEffect = -i;
    }
    g_materials = made.data();
    g_materialCount = 100;
    std::printf("init synthetic: %d\n", InitCarSurfaces());
    PrintSurfaces("synthetic");
    ClearCarSurfaces();
    std::printf("cleared: surfaces %zu\n", g_carSurfaces.size());
    return 0;
}
