// Part 29 of the human harness (HUMAN_ANIMTABLES builds, build_animtables.bat; included by human_test.cpp after
// locomotion3_test.inc): the humans' animation tables (game/human_anim_tables.cpp: Tables\anim.sav, the length
// queries) against the original's (0x00783420..0x00784430, 0x0075d000..0x0075d150).
//
// Each side makes, loads and frees its own tables (the original's in its globals 0x0098a474..0x0098a4a4: the port's
// tables keep the original's layout, so the two are compared byte for byte). The animation cache is a fake on both
// sides (the port's Get through ld --wrap, the original's AnimationCacheBase_Get redirected): one fake animation set
// per name, its GetEndTime from the name, its Release logged. The original's new and delete use the test's heap and
// the port's new[] is the test's too, both filling new blocks with 0xCD (the game's anim.sav is 84 bytes short of the
// tables: their last bytes are what the block held).
#include "game/human_anim_tables.h"
namespace {

#define AT __attribute__((thiscall))

bool g_atOn = false;
void ALog(const void *self, const std::string &what, std::initializer_list<double> nums = {})
{
    if (g_atOn)
        Log(self, what.c_str(), nums);
}

// ---- the fake animation sets (COM style: stdcall, `this` on the stack)
struct S_FakeAnim {
    void **vt;
    std::string name;
    int refs = 0;
};
std::map<std::string, S_FakeAnim *> g_fakeAnims;
void *g_fakeAnimVt[16];
uint32_t __stdcall FA_Release(S_FakeAnim *a)
{
    ALog(a, "Release");
    return uint32_t(--a->refs);
}
int __stdcall FA_GetEndTime(S_FakeAnim *a)
{
    uint32_t h = 2166136261u;
    for (char c : a->name)
        h = (h ^ uint8_t(c)) * 16777619u;
    return a->name.size() % 7 == 3 ? 0 : int(h % 9000);
}
void __stdcall FA_Unexpected(void *self)
{
    std::printf("unexpected animation set call on %s\n", NameOf(self).c_str());
    std::abort();
}
HRESULT AnimGet(const char *name, void **out)
{
    // (A name that cannot be a file's, such as the 0xCD bytes the game's short anim.sav leaves in the last records,
    // fails as the real cache's would: no such file. The original reads such a name on past its 20 bytes, the port
    // stops there; both fail.)
    for (const char *c = name; *c; ++c)
        if (uint8_t(*c) < 0x20 || uint8_t(*c) > 0x7E) {
            ALog(nullptr, "Get (not a file name)");
            return E_FAIL;
        }
    S_FakeAnim *&a = g_fakeAnims[name];
    if (!a) {
        a = new S_FakeAnim();
        a->vt = g_fakeAnimVt;
        a->name = name;
        g_names[a] = std::string("anim ") + name;
    }
    ++a->refs;
    ALog(nullptr, std::string("Get ") + name);
    *out = a;
    return 0;
}
HRESULT AT AT_CacheGet(void *, const char *name, void **out, void *, void *) { return AnimGet(name, out); }
void __fastcall AT_ShowMessage(const char *text, const char *caption, uint32_t flags)
{
    ALog(nullptr, std::string("ShowMessage ") + text + " / " + caption, {double(flags)});
}

// ---- the heaps: new blocks filled with 0xCD on both sides
bool g_atFill = false;
void *__cdecl AT_New(size_t size)
{
    void *p = std::malloc(size ? size : 1);
    std::memset(p, 0xCD, size);
    return p;
}
void __cdecl AT_Delete(void *p) { std::free(p); }
}  // namespace

// The port's new[] in this build (the tables' records and LoadHumanAnimTables' buffer).
void *operator new[](size_t size) { return AT_New(size); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }

extern "C" {
HRESULT __wrap__ZN15C_ResourceCacheI17I3D_animation_setE3GetEPKcPPS0_PU8fastcallFvS3_PvES6_(void *, const char *, void **, void *, void *)
    __attribute__((thiscall));
HRESULT __wrap__ZN15C_ResourceCacheI17I3D_animation_setE3GetEPKcPPS0_PU8fastcallFvS3_PvES6_(void *, const char *name, void **out, void *, void *)
{
    return AnimGet(name, out);
}
void __wrap__Z11ShowMessagePKcS0_j(const char *text, const char *caption, uint32_t flags) { AT_ShowMessage(text, caption, flags); }
}

namespace {
const uint32_t O_HUMAN_TABLE = 0x0098a474, O_HUMAN_TABLE2 = 0x0098a478, O_CARRY_TABLES = 0x0098a47c,
               O_ACTIVITY_TABLE = 0x0098a498, O_SEAT_TABLE = 0x0098a49c, O_TABLES_LOADED = 0x0098a4a4;
uint8_t *OrigTable(uint32_t global) { return *OriginalAt<uint8_t **>(global); }
uint32_t AtWord(const void *p) { return uint32_t(uintptr_t(p)); }
void AtWr(uint8_t *p, uint32_t w) { std::memcpy(p, &w, 4); }

// A table's bytes (its slots and records pointer) as text, the records pointer as null or not.
std::string TableBytes(const void *table, size_t size, size_t recordsAt)
{
    if (!table)
        return "null";
    if (IsBadReadPtr(table, size)) {
        std::printf("  bad table at %p\n", table);
        return "bad";
    }
    const auto *b = static_cast<const uint8_t *>(table);
    std::string t;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t w;
        std::memcpy(&w, b + i, 4);
        if (i == recordsAt)
            t += w ? "R " : "0 ";
        else if (g_names.count(reinterpret_cast<const void *>(uintptr_t(w))))
            t += NameOf(reinterpret_cast<const void *>(uintptr_t(w))) + " ";
        else
            t += std::to_string(w) + " ";
    }
    return t;
}
std::string PortTables()
{
    std::string t = TableBytes(g_humanAnimTable, sizeof(C_HumanAnimTable), 0x24D8) + "\n";
    t += TableBytes(g_humanAnimTable2, sizeof(C_HumanAnimTable), 0x24D8) + "\n";
    for (C_CarryAnimTable *c : g_carryAnimTables)
        t += TableBytes(c, sizeof(C_CarryAnimTable), 0x1110) + "\n";
    t += TableBytes(g_activityAnimTable, sizeof(C_ActivityAnimTable), 0xA8C) + "\n";
    t += TableBytes(g_seatWeaponAnimTable, sizeof(C_SeatWeaponAnimTable), 0x30) + "\n";
    return t;
}
std::string OrigTables()
{
    std::string t = TableBytes(OrigTable(O_HUMAN_TABLE), sizeof(C_HumanAnimTable), 0x24D8) + "\n";
    t += TableBytes(OrigTable(O_HUMAN_TABLE2), sizeof(C_HumanAnimTable), 0x24D8) + "\n";
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i)
        t += TableBytes(OrigTable(O_CARRY_TABLES + 4 * i), sizeof(C_CarryAnimTable), 0x1110) + "\n";
    t += TableBytes(OrigTable(O_ACTIVITY_TABLE), sizeof(C_ActivityAnimTable), 0xA8C) + "\n";
    t += TableBytes(OrigTable(O_SEAT_TABLE), sizeof(C_SeatWeaponAnimTable), 0x30) + "\n";
    return t;
}
bool TablesAgree(const char *what)
{
    const std::string p = PortTables(), o = OrigTables();
    if (p == o)
        return true;
    size_t at = 0;
    while (at < p.size() && at < o.size() && p[at] == o[at])
        ++at;
    const size_t from = at > 120 ? at - 120 : 0;
    std::printf("  %s: tables differ at %zu:\n    port ...%s\n    original ...%s\n", what, at, p.substr(from, 240).c_str(),
                o.substr(from, 240).c_str());
    return false;
}

// Both sides' calls, the logs compared.
template <class P, class O> void TablesCall(const char *what, P port, O original)
{
    g_calls.clear();
    g_atOn = true;
    const int pr = port();
    const std::vector<S_Call> portCalls = g_calls;
    g_calls.clear();
    const int orr = original();
    const std::vector<S_Call> origCalls = g_calls;
    g_atOn = false;
    std::string why;
    const bool calls = CallsAgree(portCalls, origCalls, why);
    if (pr != orr || !calls)
        std::printf("  %s: result %d / %d; %s\n", what, pr, orr, why.c_str());
    CHECK(pr == orr);
    CHECK(calls);
}

// ---- the files: the game's (84 bytes short), a full one (the M1919 mod's size) and the test's
const char *const AT_NAMES[] = {"", "a", "abcd", "walk1", "Stand_idle_01", "run_fast", "crouch_turn_l", "x123456789012345678",
                                "carry_box_walk", "prone_shoot", "\x01\x02\x03\x04\x05", " lead"};
void WriteAnimFile(const std::string &path, int extra)
{
    const size_t total = 2 * C_HumanAnimTable::RECORDS_SIZE + CARRY_ANIM_TABLES * C_CarryAnimTable::RECORDS_SIZE +
                         C_ActivityAnimTable::RECORDS_SIZE + C_SeatWeaponAnimTable::RECORDS_SIZE;
    std::vector<uint8_t> data(total + size_t(extra > 0 ? extra : 0), 0);
    const auto record = [&](size_t at, int nameAt, int size) {
        uint32_t flags = P(0.5f) ? 0u : uint32_t(std::rand()) * 65599u;
        if (nameAt != 0)
            std::memcpy(&data[at], &flags, 4);
        for (int k = 4; k < size; k += 4)
            if (k < nameAt || k >= nameAt + 0x14) {
                const float f = P(0.5f) ? U(0, 3000) : 0.0f;
                std::memcpy(&data[at + size_t(k)], &f, 4);
            }
        const char *name = P(0.4f) ? AT_NAMES[I(0, 11)] : "";
        std::strncpy(reinterpret_cast<char *>(&data[at + size_t(nameAt)]), name, 0x13);
    };
    size_t at = 0;
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < C_HumanAnimTable::SLOT_COUNT; ++i, at += C_HumanAnimTable::RECORD_SIZE)
            record(at, 8, C_HumanAnimTable::RECORD_SIZE);
    for (int t = 0; t < CARRY_ANIM_TABLES; ++t)
        for (int i = 0; i < C_CarryAnimTable::SLOT_COUNT; ++i, at += C_CarryAnimTable::RECORD_SIZE)
            record(at, 4, C_CarryAnimTable::RECORD_SIZE);
    for (int i = 0; i < C_ActivityAnimTable::SLOT_COUNT; ++i, at += C_ActivityAnimTable::RECORD_SIZE)
        record(at, 8, C_ActivityAnimTable::RECORD_SIZE);
    for (int i = 0; i < C_SeatWeaponAnimTable::SLOT_COUNT; ++i, at += C_SeatWeaponAnimTable::RECORD_SIZE)
        record(at, 0, C_SeatWeaponAnimTable::RECORD_SIZE);
    if (extra < 0)
        data.resize(data.size() + size_t(extra));
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!data.empty())
        std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

const char *const M1919_ANIM = "G:\\Hidden & Dangerous 2\\M1919_FrontSights\\Tables\\anim.sav";
typedef void(__cdecl *T_AtVoid)();
typedef bool(__cdecl *T_AtBool)();
typedef int(__stdcall *T_AtCarry)(int, int, int);
typedef int(__stdcall *T_AtActivity)(int, int);
typedef int(AT *T_AtMovementLength)(void *, int, int, int);

// The length queries on loaded tables: the carry and activity tables' (stdcall in the original) and the movement
// tables' (the original's 0x0075d000 on a movement whose human's work names the group, 1 or 2).
void TestTableQueries()
{
    alignas(16) uint8_t movement[0x300] = {}, human[0x100] = {}, work[0x100] = {};
    AtWr(movement + 0x258, AtWord(human));
    AtWr(human + 0xA4, AtWord(work));
    for (int n = 0; n < 3000 / QUICK; ++n) {
        const int id = P(0.9f) ? I(0, 100) : I(-3, 300);
        const int variant = I(0, 2), mode = I(-1, 8);
        if (id >= 0)
            TablesCall("GetCarryAnimLength", [&] { return GetCarryAnimLength(id, variant, mode); },
                       [&] { return OriginalAt<T_AtCarry>(0x0075d090)(id, variant, mode); });
        if (id >= 0 && id < 120)
            TablesCall("GetActivityAnimLength", [&] { return GetActivityAnimLength(id, variant); },
                       [&] { return OriginalAt<T_AtActivity>(0x0075d120)(id, variant); });
        const int group = I(1, 2), anim = P(0.8f) ? I(0, 205) : I(206, 260), sub = I(0, 5);
        AtWr(work + 0xA0, uint32_t(group));
        TablesCall("GetAnimLength", [&] { return GetHumanAnimTable(group)->GetAnimLength(anim, variant, sub); },
                   [&] { return OriginalAt<T_AtMovementLength>(0x0075d000)(movement, anim, variant, sub); });
    }
}

void TestAnimTables()
{
    std::printf("animation tables\n");
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0041fc10, reinterpret_cast<const void *>(AT_CacheGet)},
        {0x006ed2a0, reinterpret_cast<const void *>(AT_ShowMessage)},
        {0x007e566f, reinterpret_cast<const void *>(AT_New)},
        {0x007e2120, reinterpret_cast<const void *>(AT_Delete)},
        {0x007e53a4, reinterpret_cast<const void *>(dtaOpen)},
        {0x007e53b0, reinterpret_cast<const void *>(dtaClose)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    for (void *&slot : g_fakeAnimVt)
        slot = reinterpret_cast<void *>(FA_Unexpected);
    g_fakeAnimVt[0] = reinterpret_cast<void *>(FA_Release);
    g_fakeAnimVt[2] = reinterpret_cast<void *>(FA_GetEndTime);

    char cwd[MAX_PATH];
    GetCurrentDirectoryA(sizeof cwd, cwd);
    const std::string dir = TempFile("anim_tables");
    CreateDirectoryA(dir.c_str(), nullptr);
    CreateDirectoryA((dir + "\\Tables").c_str(), nullptr);
    SetCurrentDirectoryA(dir.c_str());
    const std::string file = "Tables\\anim.sav";
    for (int i = 0; i < 160 / QUICK; ++i) {
        std::remove(file.c_str());
        const int kind = i % 8;
        if (kind == 0)
            CopyFileA("H:\\HD2_Unpacked\\Tables\\anim.sav", file.c_str(), FALSE);
        else if (kind == 1)
            WriteAnimFile(file, 0);
        else if (kind == 2)
            WriteAnimFile(file, I(1, 300));             // (longer: the rest not read)
        else if (kind == 3)
            WriteAnimFile(file, -I(1, 2000));           // (shorter: the rest what the block held)
        else if (kind == 4 && GetFileAttributesA(M1919_ANIM) != INVALID_FILE_ATTRIBUTES)
            CopyFileA(M1919_ANIM, file.c_str(), FALSE); // (the full size: the M1919 front sights mod's, a copy)
        else if (kind == 7)
            ;                                           // (no file)
        else
            WriteAnimFile(file, 0);
        // Made (the records zeroed), their records handed out before a load now and then.
        const auto init = [&] {
            TablesCall("InitHumanAnimTables", [&] { InitHumanAnimTables(); return 0; },
                       [&] { OriginalAt<T_AtVoid>(0x00783420)(); return 0; });
            // (The original's carry tables leave their slots' weights (+0x08), its activity table its slots' times
            // (+0x04), as the block held them until loaded; the port's start at 0. Zeroed in the original's.)
            for (int t = 0; t < CARRY_ANIM_TABLES; ++t)
                for (int s = 0; s < C_CarryAnimTable::SLOT_COUNT; ++s)
                    AtWr(OrigTable(O_CARRY_TABLES + 4 * t) + 16 * s + 8, 0);
            for (int s = 0; s < C_ActivityAnimTable::SLOT_COUNT; ++s)
                AtWr(OrigTable(O_ACTIVITY_TABLE) + 12 * s + 4, 0);
            CHECK(TablesAgree("Init"));
        };
        init();
        if (P(0.2f)) {
            // SaveRecords: the size asked (0), then the records copied out (and dropped).
            std::vector<uint8_t> pout(C_HumanAnimTable::RECORDS_SIZE, 0x11), oout(C_HumanAnimTable::RECORDS_SIZE, 0x11);
            int ps = 0, os = 0;
            g_humanAnimTable->SaveRecords(&ps, nullptr);
            typedef void(AT * T)(void *, int *, void *);
            OriginalAt<T>(0x00783b70)(OrigTable(O_HUMAN_TABLE), &os, nullptr);
            CHECK(ps == os);
            ps = os = 1;
            g_humanAnimTable->SaveRecords(&ps, pout.data());
            OriginalAt<T>(0x00783b70)(OrigTable(O_HUMAN_TABLE), &os, oout.data());
            CHECK(ps == os);
            CHECK(pout == oout);
            CHECK(TablesAgree("SaveRecords"));
            // (The table's records are gone now: its load copies into null in the original. Made again.)
            TablesCall("DoneHumanAnimTables", [&] { DoneHumanAnimTables(); return 0; },
                       [&] { OriginalAt<T_AtVoid>(0x00783680)(); return 0; });
            init();
        }
        bool loaded = false;
        if (i == 1) {
            // Ensure (its flag, the port's file-local and the original's 0x0098a4a4, set once for good): the first
            // time it loads; again it answers true without loading.
            TablesCall("EnsureHumanAnimTablesLoaded", [&] { return int(loaded = EnsureHumanAnimTablesLoaded()); },
                       [&] { return int(OriginalAt<T_AtBool>(0x007837b0)()); });
            CHECK(TablesAgree("Ensure"));
            TablesCall("EnsureHumanAnimTablesLoaded again", [&] { return int(EnsureHumanAnimTablesLoaded()); },
                       [&] { return int(OriginalAt<T_AtBool>(0x007837b0)()); });
            CHECK(*OriginalAt<bool *>(O_TABLES_LOADED));
        } else {
            // (Now and then the read failing, dtaRead's -1, on both sides.)
            g_dtaReadFails = kind != 7 && P(0.1f);
            int portFlag = -1;
            TablesCall("LoadHumanAnimTables",
                       [&] {
                           loaded = LoadHumanAnimTables();
                           portFlag = g_lastDtaOpenFlag;
                           return int(loaded);
                       },
                       [&] { return int(OriginalAt<T_AtBool>(0x00784020)()); });
            CHECK(portFlag == g_lastDtaOpenFlag);         // (dtaOpen's second argument)
            g_dtaReadFails = false;
        }
        CHECK(TablesAgree("Load"));
        if (loaded)
            TestTableQueries();
        TablesCall("DoneHumanAnimTables", [&] { DoneHumanAnimTables(); return 0; },
                   [&] { OriginalAt<T_AtVoid>(0x00783680)(); return 0; });
        // (Freed: compared by its Release calls. The original leaves its globals pointing at the freed tables, the port
        // clears them; the original's Init sets them again.)
        CHECK(g_humanAnimTable == nullptr && g_activityAnimTable == nullptr && g_seatWeaponAnimTable == nullptr);
    }
    std::remove(file.c_str());
    SetCurrentDirectoryA(cwd);
    RemoveDirectoryA((dir + "\\Tables").c_str());
    RemoveDirectoryA(dir.c_str());
}
}  // namespace
