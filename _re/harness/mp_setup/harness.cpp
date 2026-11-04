// Test harness of the multiplayer game's set-up (src/game/mp_game_setup.cpp, part 9h-i), 32-bit MinGW
// (build.bat). Commands (run in a directory holding the mpmaplist.txt to read):
//   harness dump                   the map list read: every map's fields, objectives, items, presets and
//                                  the defaults (check_dump.py compares them with its own parser)
//   harness roundtrip IN OUT       the map list read, IN's chunk 0x44D loaded, saved alone into OUT (a
//                                  root 0x4E6 holding the 0x44D; check_roundtrip.py compares the chunks), then
//                                  the list read again (both dumped)
#include <cstdio>
#include <cstring>
#include <vector>

#include "common/chunk.h"
#include "game/game_texts.h"
#include "game/mp_game_setup.h"
#include "ls3d/rw_data.h"

// --- the texts: only the server's default name ------------------------------------------------------
C_GameTexts g_texts;
C_str C_GameTexts::Get(int id) const { return C_str(id == 0x7E9 ? "H&D 2 SERVER" : ""); }

// --- rw_data over stdio -----------------------------------------------------------------------------
static std::vector<FILE *> s_files;
static int AddFile(FILE *f)
{
    if (!f)
        return DTA_INVALID_HANDLE;
    s_files.push_back(f);
    return static_cast<int>(s_files.size()) - 1;
}
int __stdcall dtaOpen(const char *fileName, bool) { return AddFile(std::fopen(fileName, "rb")); }
int __stdcall dtaOpenWrite(const char *fileName, int) { return AddFile(std::fopen(fileName, "w+b")); }
int __stdcall dtaRead(int h, void *buffer, int size) { return static_cast<int>(std::fread(buffer, 1, size, s_files[h])); }
int __stdcall dtaWrite(int h, const void *buffer, int size) { return static_cast<int>(std::fwrite(buffer, 1, size, s_files[h])); }
int __stdcall dtaSeek(int h, int position, int origin)
{
    if (std::fseek(s_files[h], position, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END) != 0)
        return -1;
    return static_cast<int>(std::ftell(s_files[h]));
}
bool __stdcall dtaClose(int h)
{
    std::fclose(s_files[h]);
    s_files[h] = nullptr;
    return true;
}

static void Dump(const C_MpGameSetup &setup)
{
    std::printf("loaded %d name '%s' password '%s' port %d options %d %d %d %d %d\n", setup.loaded,
                setup.serverName.c_str(), setup.password.c_str(), setup.port, setup.unknown10, setup.unknown14,
                setup.unknown15, setup.unknown18, setup.unknown6C);
    for (int type = 0; type < MP_GAME_TYPES; ++type) {
        std::printf("type %d: %d maps\n", type, static_cast<int>(setup.maps[type].maps.size()));
        for (const C_MpMap *map : setup.maps[type].maps) {
            std::printf("map '%s' '%s' %d %d %d %d %d %d %d\n", map->GetName(), map->GetDirectory(), map->unknown00,
                        map->aircraftAxis, map->aircraftAllied, map->balancedAxis, map->balancedAllied,
                        map->frontline, map->description);
            for (const S_MpMapObjective &o : map->objectives)
                std::printf("  objective %d %d %d %d %d %d\n", o.axisText, o.alliedText, o.points, o.type, o.team,
                            o.vipUniform);
            for (const S_MpMapItem &i : map->items)
                std::printf("  item %d %d %d\n", i.itemId, i.count, i.ammo);
            std::printf("  presets '%s'\n", map->GetPresets());
        }
    }
    for (int type = 0; type < MP_GAME_TYPES; ++type) {
        const int *rule = &setup.rules[type].maxClients;
        std::printf("rules %d:", type);
        for (int i = 0; i < 22; ++i)
            std::printf(" %d", rule[i]);
        std::printf("\n");
    }
    for (int r = 0; r < MP_ROTATIONS; ++r)
        for (int type = 0; type < MP_GAME_TYPES; ++type) {
            std::printf("rotation %d type %d:", r, type);
            for (const C_MpMap *map : setup.rotations[r].types[type].maps)
                std::printf(" '%s' (%s)", map->GetName(), map->GetDirectory());
            std::printf("\n");
        }
}

int main(int argc, char **argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "dump") == 0) {
        C_MpGameSetup setup;
        setup.LoadMapList();
        Dump(setup);
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "roundtrip") == 0) {
        C_MpGameSetup setup;
        setup.LoadMapList();
        C_chunk in;
        if (!in.Open(argv[2], false)) {
            std::printf("cannot open %s\n", argv[2]);
            return 1;
        }
        uint16_t id = 0;
        in.Descend(id);
        while (!in.AtEnd()) {
            in.Descend(id);
            if (id == 0x44D)
                setup.Load(in);
            in.Ascend();
        }
        in.Close();
        C_chunk out;
        if (!out.Open(argv[3], true)) {
            std::printf("cannot write %s\n", argv[3]);
            return 1;
        }
        out.Begin(0x4E6);
        out.Begin(0x44D);
        setup.Save(out);
        out.Ascend();
        out.Ascend();
        out.Close();
        Dump(setup);
        // Read again (the main menu's reset does): the rotations and the options emptied, no map twice.
        setup.LoadMapList();
        std::printf("reset\n");
        Dump(setup);
        return 0;
    }
    std::printf("usage: harness dump | roundtrip IN OUT\n");
    return 2;
}
