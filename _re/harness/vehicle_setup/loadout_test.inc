// Part 9e-iv's loadout presets and missions' items (game/loadout_presets.cpp), in the factory build, run
// by ControlsTest after StoreListTest: the files car_stubs.cpp's memory files, the campaign zeroed
// memory with its operations' and missions' counts faked, the profile's directory "prof\". Prints
// "loadout:" with 1 per check met.
#define private public
#include "game/loadout_presets.h"
#undef private
#include "game/campaign.h"

// The campaign's operations and their missions (campaign.cpp not linked).
std::vector<int> g_lpMissions;
int C_Campaign::CountOperations(int campaign)
{
    GfLog("operations %d", campaign);
    return static_cast<int>(g_lpMissions.size());
}
int C_Campaign::CountOperationMissions(int campaign, int operation)
{
    GfLog("missions %d %d", campaign, operation);
    return g_lpMissions[static_cast<size_t>(operation)];
}
C_str C_GameProfile::GetProfileDirectory() { return C_str("prof\\"); }
size_t DtaOpenCount();                                  // (car_stubs.cpp)
extern bool g_dtaOpenWriteFails;

namespace {

struct S_LoadoutResults {
    bool lpAdd = true, lpCount = true, lpDefaults = true, lpGet = true, lpSave = true, lpLoad = true, lpLoadRows = true;
    bool lpLoadNone = true, miSetup = true, miLoad = true;
};

// A file's bytes: ints, then records.
struct S_LpFile {
    std::vector<uint8_t> bytes;
    void Int(int v)
    {
        const auto *p = reinterpret_cast<const uint8_t *>(&v);
        bytes.insert(bytes.end(), p, p + 4);
    }
    void Raw(const void *data, size_t size)
    {
        const auto *p = static_cast<const uint8_t *>(data);
        bytes.insert(bytes.end(), p, p + size);
    }
};
S_LoadoutPreset LpPreset(const char *name, int head, uint32_t flags)
{
    S_LoadoutPreset p;
    std::strncpy(p.name, name, sizeof p.name - 1);
    p.head = head;
    p.flags = flags;
    p.textId = 0x77;
    p.unknown1b4 = 9;
    return p;
}

} // namespace

void LoadoutTest(I3D_scene *)
{
    S_LoadoutResults r;
    g_gfLog.clear();
    const std::map<std::string, std::vector<uint8_t>> oldFiles = g_files;
    // (Every file opened closed: checked after each group.)
    const size_t open = DtaOpenCount();

    // --- presets added (their defaults), cleared; a preset item reset ---
    {
        bool ok = true;
        C_LoadoutPresets l;
        S_LoadoutPreset *a = l.AddPreset();
        S_LoadoutPreset *b = l.AddUserPreset();
        ok &= l.GetPresetCount() == 1 && l.GetUserPresetCount() == 1 && a->name[0] == 0 && a->head == -1 && a->hands == -1 &&
              a->beltCount == 0 && a->belt[13].typeId == -1 && a->belt[13].count == 0 && a->backpackItems[31].typeId == -1 &&
              a->flags == 0 && a->textId == -1 && a->unknown1b4 == 0 && b->uniform == -1;
        l.Clear();
        ok &= l.GetPresetCount() == 0 && l.GetUserPresetCount() == 0;
        S_PresetItem item;
        item.typeId = 5;
        item.count = 3;
        item.Reset();
        ok &= item.typeId == -1 && item.count == 0;
        WM(r.lpAdd, ok);
    }

    // --- a type's count: its places, the first of its belt and backpack entries; the places cleared ---
    {
        bool ok = true;
        S_LoadoutPreset p;
        p.head = 7;
        p.longGun = 7;
        p.hands = 8;
        p.beltCount = 3;
        p.belt[0] = {9, 4};
        p.belt[1] = {7, 2};
        p.belt[2] = {7, 5};
        p.belt[5] = {7, 50};
        // (Entries past the counts not looked at.)
        p.belt[3] = {6, 50};
        p.backpackCount = 2;
        p.backpackItems[1] = {7, 3};
        p.backpackItems[2] = {6, 30};
        ok &= p.CountItems(7) == 2 + 2 + 3 && p.CountItems(8) == 1 && p.CountItems(9) == 4 && p.CountItems(1) == 0 &&
              p.CountItems(6) == 0;
        p.ClearPlaces();
        ok &= p.head == -1 && p.longGun == -1 && p.hands == -1 && p.beltCount == 0 && p.backpackCount == 0 &&
              p.belt[0].typeId == 9 && p.CountItems(7) == 0;
        WM(r.lpCount, ok);
    }

    // --- the default user presets: six each of the axis', the allied, the deathmatch and the user's
    // (their names, games and texts) ---
    {
        bool ok = true;
        C_LoadoutPresets l;
        l.CreateUserPresets();
        ok &= l.GetUserPresetCount() == 24;
        const char *games[] = {"Axis", "Allied", "Deadmatch", "User"};
        const uint32_t flags[] = {LOADOUT_AXIS, LOADOUT_ALLIED, LOADOUT_DEATHMATCH, LOADOUT_USER};
        for (int i = 0; i < 24 && ok; ++i) {
            const S_LoadoutPreset *p = l.m_userPresets[static_cast<size_t>(i)];
            const std::string name = i < 18 ? CmText("%s_user_preset%d", games[i / 6], i % 6 + 1) : CmText("User_preset%d", i % 6 + 1);
            ok &= name == p->name && p->flags == flags[i / 6] && p->textId == (i < 18 ? 0x1982 + i : -1);
        }
        // A long name: 29 characters kept.
        S_LoadoutPreset *p = l.m_userPresets[0];
        std::memset(p->name, 'x', sizeof p->name);
        l.m_userPresets.erase(l.m_userPresets.begin() + 1, l.m_userPresets.end());
        // (Again on a list of one: the 24 named are that one and 23 new.)
        l.CreateUserPresets();
        ok &= l.GetUserPresetCount() == 25 && std::string(l.m_userPresets[0]->name) == "Axis_user_preset1" &&
              l.m_userPresets[0]->name[0x1D] == 0 && l.m_userPresets[0]->name[0x1E] == 'x' &&
              l.m_userPresets[24]->name[0] == 0 && l.m_userPresets[24]->flags == 0;
        WM(r.lpDefaults, ok);
    }

    // --- the nth preset with all of the flags (none: null), in either list ---
    {
        bool ok = true;
        C_LoadoutPresets l;
        *l.AddPreset() = LpPreset("a", 1, 0x12);
        *l.AddPreset() = LpPreset("b", 2, 0x22);
        *l.AddPreset() = LpPreset("c", 3, 0x32);
        l.CreateUserPresets();
        ok &= l.GetPreset(0, 0x12)->head == 1 && l.GetPreset(1, 0x12)->head == 3 && !l.GetPreset(2, 0x12) &&
              l.GetPreset(0, 0x22)->head == 2 && l.GetPreset(1, 0x22)->head == 3 && l.GetPreset(2, 0)->head == 3 &&
              !l.GetPreset(3, 0) && !l.GetPreset(0, 0x80);
        ok &= std::string(l.GetUserPreset(0, LOADOUT_ALLIED)->name) == "Allied_user_preset1" &&
              std::string(l.GetUserPreset(5, LOADOUT_ALLIED)->name) == "Allied_user_preset6" && !l.GetUserPreset(6, LOADOUT_ALLIED) &&
              std::string(l.GetUserPreset(0, LOADOUT_USER)->name) == "User_preset1" &&
              std::string(l.GetUserPreset(18, 0)->name) == "User_preset1";
        WM(r.lpGet, ok);
    }

    // --- the user presets saved (version 3, their count, whole records; none: the defaults first) ---
    {
        bool ok = true;
        g_files.erase("prof\\user_presets.def");
        C_LoadoutPresets l;
        ok &= l.SaveUser() && l.GetUserPresetCount() == 24;
        const std::vector<uint8_t> &f = g_files["prof\\user_presets.def"];
        ok &= f.size() == 8 + 24 * sizeof(S_LoadoutPreset) && reinterpret_cast<const int *>(f.data())[0] == 3 &&
              reinterpret_cast<const int *>(f.data())[1] == 24 &&
              std::memcmp(f.data() + 8 + 5 * sizeof(S_LoadoutPreset), l.m_userPresets[5], sizeof(S_LoadoutPreset)) == 0;
        // Two of them: those two.
        C_LoadoutPresets m;
        *m.AddUserPreset() = LpPreset("u1", 4, 4);
        *m.AddUserPreset() = LpPreset("u2", 5, 4);
        ok &= m.SaveUser() && g_files["prof\\user_presets.def"].size() == 8 + 2 * sizeof(S_LoadoutPreset) &&
              m.GetUserPresetCount() == 2;
        // Not opened: false, the defaults not made.
        g_dtaOpenWriteFails = true;
        C_LoadoutPresets n;
        ok &= !n.SaveUser() && n.GetUserPresetCount() == 0;
        g_dtaOpenWriteFails = false;
        WM(r.lpSave, ok && DtaOpenCount() == open);
    }

    // --- loaded: presets.def's records (versions 0 to 2: 0x1B0, 0x1B4, 0x1B8 bytes), the old ones gone;
    // the user's into the defaults' rows of six ---
    {
        bool ok = true;
        for (int version = 0; version < 3; ++version) {
            S_LpFile presets;
            presets.Int(version);
            presets.Int(2);
            const S_LoadoutPreset a = LpPreset("pa", 11, 0x12), b = LpPreset("pb", 12, 0x22);
            const int size = version == 0 ? 0x1B0 : version == 1 ? 0x1B4 : 0x1B8;
            presets.Raw(&a, static_cast<size_t>(size));
            presets.Raw(&b, static_cast<size_t>(size));
            g_files["GameData\\presets.def"] = presets.bytes;
            S_LpFile user;
            user.Int(3);
            user.Int(24);
            for (int i = 0; i < 24; ++i) {
                const S_LoadoutPreset u = LpPreset(CmText("user%d", i).c_str(), 100 + i, 4);
                user.Raw(&u, sizeof u);
            }
            g_files["prof\\user_presets.def"] = user.bytes;
            C_LoadoutPresets l;
            *l.AddPreset() = LpPreset("old", 1, 0);
            ok &= l.Load() && l.GetPresetCount() == 2 && l.m_presets[0]->head == 11 && std::string(l.m_presets[1]->name) == "pb" &&
                  l.m_presets[1]->textId == (version == 0 ? -1 : 0x77) && l.m_presets[1]->unknown1b4 == (version == 2 ? 9 : 0) &&
                  l.m_presets[1]->flags == 0x22u;
            ok &= l.GetUserPresetCount() == 24;
            for (int i = 0; i < 24 && ok; ++i)
                ok &= l.m_userPresets[static_cast<size_t>(i)]->head == 100 + i;
        }
        WM(r.lpLoad, ok && DtaOpenCount() == open);
    }

    // --- the user's of 8: two a row (the rest of the rows the defaults); over 24: 24; under 4: one a row ---
    {
        bool ok = true;
        const auto userFile = [](int count, int version) {
            S_LpFile user;
            user.Int(version);
            user.Int(count);
            for (int i = 0; i < count; ++i) {
                const S_LoadoutPreset u = LpPreset(CmText("user%d", i).c_str(), 100 + i, 4);
                user.Raw(&u, version == 0 ? 0x1B0u : sizeof u);
            }
            g_files["prof\\user_presets.def"] = user.bytes;
        };
        userFile(8, 3);
        C_LoadoutPresets l;
        ok &= l.Load();
        const int want8[] = {0, 1, 6, 7, 12, 13, 18, 19};
        for (int i = 0; i < 8; ++i)
            ok &= l.m_userPresets[static_cast<size_t>(want8[i])]->head == 100 + i;
        ok &= l.m_userPresets[2]->head == -1 && std::string(l.m_userPresets[2]->name) == "Axis_user_preset3";
        userFile(30, 0);
        C_LoadoutPresets m;
        ok &= m.Load() && m.GetUserPresetCount() == 24 && m.m_userPresets[23]->head == 123 && m.m_userPresets[23]->textId == -1 &&
              m.m_userPresets[23]->flags == 4;
        userFile(2, 3);
        C_LoadoutPresets n;
        ok &= n.Load() && n.m_userPresets[0]->head == 100 && n.m_userPresets[6]->head == 101 && n.m_userPresets[1]->head == -1;
        WM(r.lpLoadRows, ok);
    }

    // --- no presets.def: false, nothing (the old kept); no user file: the defaults saved, false ---
    {
        bool ok = true;
        g_files.erase("GameData\\presets.def");
        C_LoadoutPresets l;
        *l.AddPreset() = LpPreset("old", 1, 0);
        ok &= !l.Load() && l.GetPresetCount() == 1 && l.GetUserPresetCount() == 0;
        S_LpFile presets;
        presets.Int(2);
        presets.Int(0);
        g_files["GameData\\presets.def"] = presets.bytes;
        g_files.erase("prof\\user_presets.def");
        C_LoadoutPresets m;
        ok &= !m.Load() && m.GetPresetCount() == 0 && m.GetUserPresetCount() == 24 &&
              g_files["prof\\user_presets.def"].size() == 8 + 24 * sizeof(S_LoadoutPreset);
        WM(r.lpLoadNone, ok && DtaOpenCount() == open);
    }

    // --- the missions' items set up for the current campaign: a record per mission, each operation's
    // first index (from the campaign's 30), the end's; then the file ---
    alignas(16) static uint8_t campaignMemory[sizeof(C_Campaign)];
    std::memset(campaignMemory, 0, sizeof campaignMemory);
    C_Campaign *campaign = reinterpret_cast<C_Campaign *>(campaignMemory);
    C_Campaign *const oldCampaign = g_gameProfile.m_campaign;
    g_gameProfile.m_campaign = campaign;
    {
        bool ok = true;
        g_files.erase("GameData\\miss_itm.def");
        C_MissionItems m;
        ok &= m.m_first[0] == -1 && m.m_first[49] == -1 && !m.m_records[0] && !m.m_records[119];
        campaign->SetCurrentIndex(1);
        g_lpMissions = {2, 0, 3};
        g_gfLog.clear();
        m.Setup();
        ok &= MsLogIs({"operations 1", "missions 1 0", "missions 1 1", "missions 1 2"}, g_gfLog) && m.m_campaign == 1 &&
              m.GetFirstRecord(1, 0) == 30 && m.GetFirstRecord(1, 1) == 32 && m.GetFirstRecord(1, 2) == 32 &&
              m.GetFirstRecord(1, 3) == 35 && m.m_first[34] == -1 && m.m_first[29] == -1 && !m.m_records[29] &&
              m.m_records[30] && m.m_records[34] && !m.m_records[35] && m.m_records[30]->kind == 4 &&
              m.m_records[30]->slots[9].c == -1 && m.m_records[30]->pairs.empty();
        m.Release();
        ok &= !m.m_records[30] && !m.m_records[34];
        // Then miss_itm.def into them: 30 absent, the 31st kind 7.
        S_LpFile f;
        f.Int(1);
        f.Int(0);
        const std::vector<uint8_t> header(0x3C, 0);
        f.Raw(header.data(), header.size());
        f.Int(31);
        for (int i = 0; i < 30; ++i)
            f.Int(0);
        f.Int(1);
        f.Int(7);
        f.Int(0);
        f.Int(0);
        g_files["GameData\\miss_itm.def"] = f.bytes;
        C_MissionItems n;
        n.Setup();
        ok &= n.m_records[30] && n.m_records[30]->kind == 7 && n.m_records[31]->kind == 4;
        WM(r.miSetup, ok && DtaOpenCount() == open);
    }

    // --- miss_itm.def: its header skipped (version 1: 0x3C bytes, else 200); per record: absent (an
    // existing one cleared), else its kind, slots and pairs (a missing record made) ---
    {
        bool ok = true;
        for (int version = 1; version < 3; ++version) {
            S_LpFile f;
            f.Int(version);
            f.Int(0);
            std::vector<uint8_t> header(version == 1 ? 0x3C : 200, 0xEE);
            f.Raw(header.data(), header.size());
            f.Int(4);
            // 0: kind 7, two slots, one pair.
            f.Int(1);
            f.Int(7);
            f.Int(2);
            const int slots[] = {1, 2, 3, 4, 5, 6};
            f.Raw(slots, sizeof slots);
            f.Int(1);
            f.Int(40);
            f.Int(41);
            // 1: absent.
            f.Int(0);
            // 2: kind 9, no slots, two pairs.
            f.Int(1);
            f.Int(9);
            f.Int(0);
            f.Int(2);
            f.Int(50);
            f.Int(51);
            f.Int(52);
            f.Int(53);
            // 3: kind 6, eleven slots (the ten kept, the 11th skipped), one pair.
            f.Int(1);
            f.Int(6);
            f.Int(11);
            for (int i = 0; i < 33; ++i)
                f.Int(100 + i);
            f.Int(1);
            f.Int(60);
            f.Int(61);
            g_files["GameData\\miss_itm.def"] = f.bytes;
            C_MissionItems m;
            m.m_records[0] = new S_MissionItems();
            m.m_records[0]->pairs.push_back({1, 1});
            m.m_records[1] = new S_MissionItems();
            m.m_records[1]->kind = 5;
            m.m_records[1]->slots[3].b = 8;
            m.m_records[1]->pairs.push_back({2, 2});
            // (Past the file's records: kept.)
            m.m_records[4] = new S_MissionItems();
            m.m_records[4]->pairs.push_back({3, 3});
            ok &= m.Load();
            const S_MissionItems *a = m.m_records[0], *b = m.m_records[1], *c = m.m_records[2];
            ok &= a->kind == 7 && a->slots[0].a == 1 && a->slots[1].c == 6 && a->slots[2].a == -1 &&
                  a->pairs == std::vector<std::pair<int, int>>({{1, 1}, {40, 41}});
            ok &= b->kind == 5 && b->slots[3].b == -1 && b->pairs.empty();
            ok &= c && c->kind == 9 && c->slots[0].a == -1 && c->pairs == std::vector<std::pair<int, int>>({{50, 51}, {52, 53}});
            const S_MissionItems *d = m.m_records[3];
            ok &= d && d->kind == 6 && d->slots[0].a == 100 && d->slots[9].c == 129 &&
                  d->pairs == std::vector<std::pair<int, int>>({{60, 61}});
            ok &= m.m_records[4]->pairs.size() == 1 && !m.m_records[5];
        }
        // Over 120 records: 120 (the rest not read; past the array nothing written).
        {
            S_LpFile f;
            f.Int(1);
            f.Int(0);
            const std::vector<uint8_t> header(0x3C, 0);
            f.Raw(header.data(), header.size());
            f.Int(121);
            for (int i = 0; i < 120; ++i)
                f.Int(0);
            f.Int(1);
            f.Int(0x123);
            f.Int(0);
            f.Int(0);
            g_files["GameData\\miss_itm.def"] = f.bytes;
            C_MissionItems m;
            ok &= m.Load() && m.m_campaign == 0 && !m.m_records[119];
        }
        // No file: false.
        g_files.erase("GameData\\miss_itm.def");
        C_MissionItems m;
        ok &= !m.Load();
        WM(r.miLoad, ok && DtaOpenCount() == open);
    }
    g_gameProfile.m_campaign = oldCampaign;
    g_lpMissions.clear();
    g_files = oldFiles;
    g_gfLog.clear();
    std::printf("loadout: lpAdd %d lpCount %d lpDefaults %d lpGet %d lpSave %d lpLoad %d lpLoadRows %d lpLoadNone %d "
                "miSetup %d miLoad %d\n",
                r.lpAdd, r.lpCount, r.lpDefaults, r.lpGet, r.lpSave, r.lpLoad, r.lpLoadRows, r.lpLoadNone, r.miSetup,
                r.miLoad);
}
