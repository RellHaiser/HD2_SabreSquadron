// Tests the ported profile layer on its own:
//   harness roster FILE   loads HumanStore.def, dumps it, writes it back and compares the bytes
//   harness names         the old full-name chunk's parser
//   harness squad         a squad soldier's items and load, save / load round trip
//   harness misc          item stock, diary, movies, key bindings, HUD option round trips
//   harness player        profile.sav and Options.bin round trips, game.cfg, last save text
// Writes its files into the working directory (PlayersProfiles\Tester, game.cfg, *.bin).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <string>
#include <vector>

#define private public
#define protected public
#include "game/profile.h"
#undef private
#undef protected
#include "common/chunk.h"
#include "common/config_file.h"
#include "game/campaign.h"
#include "game/game_app.h"
#include "game/item_defs.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"
#include "script/script_module.h"

// --- stubs ------------------------------------------------------------------------------------
bool g_harnessLocalGame = true;
C_GameTexts g_texts;
C_str C_GameTexts::Get(int id) const
{
    if (id == 0x802 || id == 0x803)
        return C_str("%s$%s$%s");
    return C_fstr("T%X", id);
}
void C_GameTexts::Load(const char *fileName) { std::printf("texts.Load(%s)\n", fileName); }
bool IsLegalUtf8(const uint8_t *begin, const uint8_t *end)
{
    for (const uint8_t *p = begin; p < end;) {
        int n = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0;
        if (n == 0 || p + n > end)
            return false;
        for (int i = 1; i < n; ++i)
            if ((p[i] & 0xC0) != 0x80)
                return false;
        p += n;
    }
    return true;
}
void WideToAnsi(char *out, const wchar_t *text, int maxLength)
{
    int i = 0;
    for (; text[i] && i < maxLength; ++i)
        out[i] = static_cast<char>(text[i]);
    out[i] = '\0';
}
C_Controls g_controls;
int s_bindCalls;
void C_Controls::Bind(int, int16_t, uint16_t, int, int) { ++s_bindCalls; }
bool C_Controls::SetMouseScale(int x, int y, int z) { std::printf("mouse scale %d %d %d\n", x, y, z); return true; }
void C_Controls::SetInvertMouse(bool on) { std::printf("invert mouse %d\n", on); }
C_NetGame g_netGame;
void C_NetGame::SetVoiceValue(int value) { std::printf("voice value %d\n", value); }
static IGraph s_graph;
IGraph *GetIGraph() { return &s_graph; }
long IGraph::LoadTexture(const char *, const char *, uint32_t, IImage **out) { *out = nullptr; return -1; }
const wchar_t *IGraph::GetKeyName(int key)
{
    static wchar_t name[32];
    swprintf(name, 32, L"KEY%02X", key);
    return name;
}
void IGraph::SetGammaRamp(const uint16_t *ramp, uint32_t) { std::printf("gamma ramp %u %u %u .. %u\n", ramp[0], ramp[1], ramp[2], ramp[255]); }
I3D_driver *I3DGetDriver() { return nullptr; }
static C_SoundModule s_sound;
C_SoundModule *GetSoundModule() { return &s_sound; }
float C_SoundModule::SetMusicVolume(float v) { std::printf("music volume %.2f\n", v); return v; }
float C_SoundModule::SetMasterVolume(float v) { std::printf("master volume %.2f\n", v); return v; }
C_ScriptModule g_scriptModule;
C_GameApp *GetGameApp() { return nullptr; }
static C_MissionDef s_mission;
C_MissionDef *C_Campaign::GetCurrentMission() { return &s_mission; }
int C_Campaign::GetMissionNumber(int) { return 3; }
int C_Campaign::CountMissions(int) { return 16; }
int C_Campaign::GetMissionId(int, int index) { return 100 + index; }
C_ItemsModule g_itemsModule;
static C_BackpackComp s_backpack(20.0f, 30.0f);
static C_ItemDef s_defs[] = {
    C_ItemDef(0.0f, 0),                             // 0 unused
    C_ItemDef(1.0f, 1),                             // 1 helmet
    C_ItemDef(2.0f, 0),                             // 2 uniform
    C_ItemDef(3.0f, 5, &s_backpack),                // 3 backpack
    C_ItemDef(4.5f, 3),                             // 4 rifle
    C_ItemDef(0.5f, 1),                             // 5 grenade
};
C_ItemDef *GetItemDef(int typeId) { return typeId >= 0 && typeId < 6 ? &s_defs[typeId] : nullptr; }

// The hub's parts the layer reads.
C_GameProfile g_gameProfile;
C_Campaign *&g_campaign = g_gameProfile.m_campaign;
S_HudOption *&g_hudOption = g_gameProfile.m_hudOption;
S_MissionSquad *&g_missionSquad = g_gameProfile.m_missionSquad;
S_MultiplayerSettings *&g_mpSettings = g_gameProfile.m_mpSettings;
S_PlayMode *&g_playMode = g_gameProfile.m_playMode;
C_GameProfile::C_GameProfile()
{
    m_campaign = new C_Campaign();
    m_humanStore = new C_HumanStore();
    m_itemStock18 = new C_ItemStock();
    m_itemStock1C = new C_ItemStock();
    m_player = new C_PlayerProfile();
    m_missionSquad = new S_MissionSquad();
    m_playMode = new S_PlayMode();
    m_hudOption = new S_HudOption();
}
C_GameProfile::~C_GameProfile() {}
C_str C_GameProfile::GetLanguage() { return C_str("english"); }
C_str C_GameProfile::GetProfileDirectory()
{
    return C_fstr("%s\\%s\\", "PlayersProfiles", m_player->GetName().c_str());
}

// --- rw_data over stdio -------------------------------------------------------------------------
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

// --- helpers --------------------------------------------------------------------------------------
static std::vector<unsigned char> ReadFile(const char *name)
{
    std::vector<unsigned char> data;
    FILE *f = std::fopen(name, "rb");
    if (!f)
        return data;
    int c;
    while ((c = std::fgetc(f)) != EOF)
        data.push_back(static_cast<unsigned char>(c));
    std::fclose(f);
    return data;
}

static bool SameFiles(const char *a, const char *b)
{
    const std::vector<unsigned char> x = ReadFile(a), y = ReadFile(b);
    if (x == y) {
        std::printf("  %s == %s (%u bytes)\n", a, b, static_cast<unsigned>(x.size()));
        return true;
    }
    size_t i = 0;
    while (i < x.size() && i < y.size() && x[i] == y[i])
        ++i;
    std::printf("  %s != %s (%u / %u bytes, first difference at %#x)\n", a, b, static_cast<unsigned>(x.size()),
                static_cast<unsigned>(y.size()), static_cast<unsigned>(i));
    return false;
}

template <typename F> static void WriteRoot(const char *name, uint16_t id, F write)
{
    C_chunk chunk;
    chunk.Open(name, true);
    chunk.Begin(id);
    write(chunk);
    chunk.Ascend();
    chunk.Close();
}

template <typename F> static void ReadRoot(const char *name, F read)
{
    C_chunk chunk;
    chunk.Open(name, false);
    uint16_t id;
    chunk.Descend(id);
    read(chunk);
    chunk.Close();
}

static int s_failures;
#define CHECK(x) do { if (!(x)) { std::printf("  FAILED: %s (line %d)\n", #x, __LINE__); ++s_failures; } } while (0)

// --- tests ----------------------------------------------------------------------------------------
static void TestRoster(const char *file)
{
    C_HumanStore &store = *g_gameProfile.m_humanStore;
    CHECK(store.Load(C_str(file)));
    std::printf("roster: %d soldiers\n", store.GetCount());
    for (int i = 0; i < store.GetCount(); ++i) {
        const S_SoldierProfile *p = store.Get(i);
        if (i < 4 || i == store.GetCount() - 1)
            std::printf("  %2d '%s' / '%s' face %s -> %s  u24 %d u44 %d stats %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n", i,
                        p->fullName.c_str(), p->shortName.c_str(), p->faceFile.c_str(), p->faceTexture.c_str(),
                        p->unknown24, p->unknown44, p->stats[0], p->stats[1], p->stats[2], p->stats[3], p->stats[4],
                        p->stats[5], p->stats[6], p->stats[7], p->stats[8]);
    }
    WriteRoot("roster_out.bin", 0x29A, [&](C_chunk &chunk) { store.SaveGame(chunk, false); });
    CHECK(SameFiles(file, "roster_out.bin"));
    int stats[STAT_COUNT] = {800, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};
    store.GetStats(stats, 0, 10);
    std::printf("  stats of 0 at level 10:");
    for (int s : stats)
        std::printf(" %d", s);
    std::printf("\n");
}

static void TestNames()
{
    const char *names[] = {"First (Nick) Last", "First Last", "First", "First (Nick)", "(Nick) Last", "First Last)", ""};
    for (const char *name : names) {
        WriteRoot("name.bin", 9000, [&](C_chunk &chunk) { chunk.WriteStringChunk(0x238C, name); });
        S_SoldierProfile profile;
        ReadRoot("name.bin", [&](C_chunk &chunk) { profile.LoadGame(chunk); });
        std::printf("  '%s' -> first '%s' nick '%s' last '%s' full '%s' short '%s'\n", name, profile.firstName.c_str(),
                    profile.nickname.c_str(), profile.lastName.c_str(), profile.fullName.c_str(), profile.shortName.c_str());
    }
}

static void PrintLoad(const char *what, const S_SquadSoldier &s, const S_SoldierItem &displaced)
{
    std::printf("  %-22s displaced {%d,%d,%d} load cap %d w %.2f bp %.1f/%d %.2f/%d belt %.2f/%d  belt %u bp %u\n", what,
                displaced.uid, displaced.typeId, displaced.present, s.load.capacity, s.load.weight,
                s.load.backpackMaxWeight, s.load.backpackMaxVolume, s.load.backpackWeight, s.load.backpackVolume,
                s.load.beltWeight, s.load.beltVolume, static_cast<unsigned>(s.belt.size()),
                static_cast<unsigned>(s.backpackContents.size()));
}

static S_SoldierItem Item(int type, int count)
{
    S_SoldierItem item;
    item.uid = -1;
    item.typeId = static_cast<int16_t>(type);
    item.present = static_cast<int16_t>(count);
    return item;
}

static void TestSquad()
{
    S_MissionSquad &squad = *g_gameProfile.m_missionSquad;
    S_SquadSoldier *s = squad.AddSoldier();
    CHECK(s->SetProfile(0, 0));
    std::printf("squad soldier '%s' capacity %d\n", s->name.c_str(), s->load.capacity);
    S_SoldierItem displaced;
    s->PutItem(&displaced, SQUAD_SLOT_HEAD, Item(1, 1)); PrintLoad("head helmet", *s, displaced);
    s->PutItem(&displaced, SQUAD_SLOT_UNIFORM, Item(2, 1)); PrintLoad("uniform", *s, displaced);
    s->PutItem(&displaced, SQUAD_SLOT_BACKPACK_CONTENTS, Item(5, 3)); PrintLoad("bp before backpack", *s, displaced);
    s->PutItem(&displaced, SQUAD_SLOT_BACKPACK, Item(3, 1)); PrintLoad("backpack", *s, displaced);
    s->PutItem(&displaced, SQUAD_SLOT_LONG_GUN, Item(4, 1)); PrintLoad("rifle", *s, displaced);
    s->PutItem(&displaced, SQUAD_SLOT_BELT, Item(5, 4)); PrintLoad("belt 4 grenades", *s, displaced);
    s->PutItem(&displaced, SQUAD_SLOT_BELT, Item(5, 4)); PrintLoad("belt 4 more", *s, displaced);
    s->PutItem(&displaced, SQUAD_SLOT_BACKPACK_CONTENTS, Item(5, 30)); PrintLoad("bp 30 grenades", *s, displaced);
    s->PutItem(&displaced, SQUAD_SLOT_HEAD, Item(4, 1)); PrintLoad("rifle on head", *s, displaced);
    const S_SoldierLoad incremental = s->load;
    S_SoldierItem take = Item(5, 2);
    std::printf("  remove 2 from belt: %d (asked %d)\n", s->RemoveItem(SQUAD_SLOT_BELT, take), take.present);
    PrintLoad("after remove", *s, displaced);
    std::printf("  count belt %d bp %d\n", s->CountItems(SQUAD_SLOT_BELT, Item(5, 0)), s->CountItems(SQUAD_SLOT_BACKPACK_CONTENTS, Item(5, 0)));
    S_SoldierItem backpackTake = Item(3, 1);
    std::printf("  remove full backpack: %d\n", s->RemoveItem(SQUAD_SLOT_BACKPACK, backpackTake));
    const S_SoldierLoad before = s->load;
    s->UpdateLoad();
    PrintLoad("UpdateLoad", *s, displaced);
    CHECK(before.beltVolume == s->load.beltVolume && before.backpackVolume == s->load.backpackVolume);
    (void)incremental;
    s->SplitStacks();
    std::printf("  split: belt %u bp %u\n", static_cast<unsigned>(s->belt.size()), static_cast<unsigned>(s->backpackContents.size()));
    s->MergeItems();
    std::printf("  merged: belt %u (%d) bp %u (%d)\n", static_cast<unsigned>(s->belt.size()), s->belt.empty() ? 0 : s->belt[0].present,
                static_cast<unsigned>(s->backpackContents.size()), s->backpackContents.empty() ? 0 : s->backpackContents[0].present);
    WriteRoot("squad1.bin", 0x3EB, [&](C_chunk &chunk) { squad.SaveGame(chunk); });
    ReadRoot("squad1.bin", [&](C_chunk &chunk) { squad.LoadGame(chunk); });
    WriteRoot("squad2.bin", 0x3EB, [&](C_chunk &chunk) { squad.SaveGame(chunk); });
    CHECK(SameFiles("squad1.bin", "squad2.bin"));
    s = squad.states[0];
    s->ReturnItems();
    std::printf("  returned: stock grenades %d rifles %d helmets %d\n", g_gameProfile.m_itemStock1C->Get(5),
                g_gameProfile.m_itemStock1C->Get(4), g_gameProfile.m_itemStock1C->Get(1));
}

static void TestMisc()
{
    C_ItemStock stock;
    stock.Set(3, 5);
    stock.Add(7, 2);
    stock.Add(-1, 9);
    std::printf("stock: take 3 of 7 -> %d, get 3 = %d, get 9 = %d\n", stock.Take(7, 3), stock.Get(3), stock.Get(9));
    WriteRoot("stock1.bin", 0x3EC, [&](C_chunk &chunk) { stock.SaveGame(chunk); });
    C_ItemStock stock2;
    ReadRoot("stock1.bin", [&](C_chunk &chunk) { CHECK(stock2.LoadGame(chunk)); });
    WriteRoot("stock2.bin", 0x3EC, [&](C_chunk &chunk) { stock2.SaveGame(chunk); });
    CHECK(SameFiles("stock1.bin", "stock2.bin"));

    C_Diary diary;
    diary.AddText(10);
    diary.AddText(11);
    diary.CommitCurrent();
    diary.AddText(12);
    diary.locked = true;
    diary.AddText(13);
    diary.locked = false;
    WriteRoot("diary1.bin", 0x15B3, [&](C_chunk &chunk) { diary.SaveGame(chunk); });
    C_Diary diary2;
    ReadRoot("diary1.bin", [&](C_chunk &chunk) { diary2.LoadGame(chunk); });
    std::printf("diary: %u texts, %u current\n", static_cast<unsigned>(diary2.GetTexts().size()),
                static_cast<unsigned>(diary2.GetCurrentTexts().size()));
    WriteRoot("diary2.bin", 0x15B3, [&](C_chunk &chunk) { diary2.SaveGame(chunk); });
    CHECK(SameFiles("diary1.bin", "diary2.bin"));

    C_MovieList movies;
    movies.SetSeen("D_Outro.bik", true);
    movies.SetSeen("IntroAlps.bik", true);
    WriteRoot("movies1.bin", 0x1EF0, [&](C_chunk &chunk) { movies.Save(chunk); });
    C_MovieList movies2;
    ReadRoot("movies1.bin", [&](C_chunk &chunk) { movies2.Load(chunk); });
    std::printf("movies: %d, seen:", movies2.GetCount());
    for (int i = 0; i < movies2.GetCount(); ++i)
        if (movies2.IsSeen(i))
            std::printf(" %d", i);
    std::printf("\n");
    WriteRoot("movies2.bin", 0x1EF0, [&](C_chunk &chunk) { movies2.Save(chunk); });
    CHECK(SameFiles("movies1.bin", "movies2.bin"));

    C_KeyBindings bindings;
    bindings.SetDefaults(1);
    std::printf("bindings: %d, bind calls %d (mode %d)\n", bindings.GetCount(), s_bindCalls, bindings.m_mode);
    std::printf("  forward = '%s', fire = '%s', wheel up = '%s'\n", bindings.GetBindingName(bindings.Find(0, 2)).c_str(),
                bindings.GetBindingName(bindings.Find(6, 2)).c_str(), bindings.GetBindingName(4).c_str());
    C_str text;
    bindings.ReplaceTags(&text, "Press <Move_forward> to walk");
    std::printf("  '%s'\n", text.c_str());
    bindings.ReplaceTags(&text, "<Reload>");
    std::printf("  '%s'\n", text.c_str());
    // F1 is reserved; binding back's key (S) to forward unbinds back.
    const bool boundF1 = bindings.Bind(bindings.Find(0, 2), 1, 0x3B, 0);
    const bool boundS = bindings.Bind(bindings.Find(0, 2), 1, 0x1F, 0);
    const C_str back = bindings.GetBindingName(bindings.Find(1, 2));
    std::printf("  bind F1: %d, bind S to forward: %d, back now '%s', unbound %d (back is %d)\n", boundF1, boundS,
                back.c_str(), bindings.FindUnbound(), bindings.Find(1, 2));
    int position;
    bindings.GetGroupPosition(5, &position);
    std::printf("  group position of 5: %d\n", position);
    // (Loading applies the changed bindings and clears their flags.)
    bindings.ApplyChanged();
    WriteRoot("keys1.bin", 0x25A6, [&](C_chunk &chunk) { bindings.Save(chunk); });
    C_KeyBindings bindings2;
    ReadRoot("keys1.bin", [&](C_chunk &chunk) { CHECK(bindings2.Load(chunk)); });
    WriteRoot("keys2.bin", 0x25A6, [&](C_chunk &chunk) { bindings2.Save(chunk); });
    CHECK(SameFiles("keys1.bin", "keys2.bin"));

    S_HudOption hud;
    hud.shown = true;
    WriteRoot("hud.bin", 0x3F2, [&](C_chunk &chunk) { hud.SaveGame(chunk); });
    S_HudOption hud2;
    ReadRoot("hud.bin", [&](C_chunk &chunk) { CHECK(hud2.LoadGame(chunk)); });
    CHECK(hud2.shown);
}

static void TestPlayer()
{
    _mkdir("PlayersProfiles");
    _mkdir("PlayersProfiles\\Tester");
    C_PlayerProfile &player = *g_gameProfile.m_player;
    player.m_name = "Tester";
    player.SetDifficulty(3);
    g_playMode->mode = PLAY_MODE_LONE_WOLF;
    player.SetProgress(1, 5);
    player.SetLastSave(C_str("PlayersProfiles\\Tester\\LOW_quick.sav"));
    std::printf("last save '%s' text '%s' slot1 %d '%s'\n", player.GetLastSave()->c_str(), player.m_lastSaveText.c_str(),
                player.m_saveSlots[1].mission, player.m_saveSlots[1].file.c_str());
    C_str mode, mission, date;
    CHECK(player.GetLastSaveText(&mode, &mission, &date));
    std::printf("  mode '%s' mission '%s' date '%s'\n", mode.c_str(), mission.c_str(), date.c_str());
    player.GetMovies().SetSeen("Outro1.bik", true);
    CHECK(player.Save());
    std::rename("PlayersProfiles\\Tester\\profile.sav", "profile1.sav");
    std::rename("profile1.sav", "PlayersProfiles\\Tester\\profile.sav");
    C_PlayerProfile other;
    other.m_name = "Tester";
    CHECK(other.Load());
    std::printf("loaded: difficulty %d progress %d %d best %d, last '%s'\n", other.GetDifficulty(), other.m_progress[1][2],
                other.GetProgress(1), other.GetBestProgress(1), other.GetLastSave()->c_str());
    std::rename("PlayersProfiles\\Tester\\profile.sav", "profile1.sav");
    CHECK(other.Save());
    CHECK(SameFiles("profile1.sav", "PlayersProfiles\\Tester\\profile.sav"));

    player.m_options.soundVolume = 80.0f;
    player.m_options.level = 1;
    CHECK(player.SaveOptions());
    std::rename("PlayersProfiles\\Tester\\Options.bin", "options1.bin");
    std::rename("options1.bin", "PlayersProfiles\\Tester\\Options.bin");
    CHECK(other.LoadOptions());
    std::printf("options: volume %.0f level %d decals %d\n", other.m_options.soundVolume, other.m_options.level,
                other.m_options.IsEnabled(DETAIL_DECALS));
    std::rename("PlayersProfiles\\Tester\\Options.bin", "options1.bin");
    CHECK(other.SaveOptions());
    CHECK(SameFiles("options1.bin", "PlayersProfiles\\Tester\\Options.bin"));

    // game.cfg: the constructor reads <exe dir>\game.cfg, Save writes the working directory's.
    C_GameConfig config;
    config.SetLastProfile(C_str("Tester"));
    config.SetParentLock(C_str("1234"));
    C_ConfigFile file;
    CHECK(file.Load(C_str("game.cfg")));
    char value[100];
    CHECK(file.GetString("LASTPROFIL", value, sizeof(value)));
    std::printf("game.cfg LASTPROFIL '%s'", value);
    CHECK(file.GetString("PARENTLOC", value, sizeof(value)));
    std::printf(" PARENTLOC '%s'\n", value);
    CHECK(!config.ClearParentLock(C_str("999")) && config.ClearParentLock(C_str("1234")) && !config.IsParentLockSet());
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    if (!std::strcmp(mode, "roster"))
        TestRoster(argc > 2 ? argv[2] : "HumanStore.def");
    else if (!std::strcmp(mode, "names"))
        TestNames();
    else if (!std::strcmp(mode, "squad")) {
        g_gameProfile.m_humanStore->Load(C_str(argc > 2 ? argv[2] : "HumanStore.def"));
        TestSquad();
    }
    else if (!std::strcmp(mode, "misc"))
        TestMisc();
    else if (!std::strcmp(mode, "player"))
        TestPlayer();
    else
        std::printf("modes: roster FILE, names, squad, misc, player\n");
    std::printf("%d failures\n", s_failures);
    return s_failures != 0;
}

// (Further stubs the link needs.)
bool C_GameApp::GetQuickSaveMission(const C_str &, int *) { return false; }
bool C_GameProfile::IsLanguage(const char *language) { return _stricmp(language, "English") == 0; }
void I3D_driver::SetState(int, uint32_t) {}
