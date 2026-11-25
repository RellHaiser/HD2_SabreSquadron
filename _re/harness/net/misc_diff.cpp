// Net part 14: the network's odds and ends (net/net_console.cpp: a dedicated server's loading progress on its console,
// the CD key from the registry; net/net_com.cpp: the GUIDs of DirectPlay 8 and DirectPlay Voice) against the original
// executable's. MinGW; build_misc.bat; runs misc_diff.exe ("misc_diff: N checks, 0 failures").
//
// net_console.cpp is included here (its loading flag and key buffer are in an unnamed namespace). Each round sets both
// sides' state alike (the loading flag, the key buffer's bytes, the current map's name) and calls PrintLoadProgress
// (0x00731af0) or GetCDKey (0x00732000) on both: the console (OnChar, MoveInputLineToHistory, AddLine), the texts, the
// current map and the registry (RegOpenKeyExA, RegQueryValueExA, RegCloseKey: scripted, never the real registry; the
// original's by its import slots, the port's renamed below) are the test's, logging. After each call the logs, the
// result, the loading flag, the key buffer and the original's heap balance are compared. The GUIDs are compared with
// the original's data at their addresses, and the voice codecs' order with the original's StartVoiceServer switch
// (0x00724520: its jump table, each case's GUID).
// HD2_MISC_ROUNDS: rounds (default 40,000; HD2_MISC_QUICK: 10,000); HD2_MISC_SEED.
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

// ---- the port's registry: the test's ----
LONG WINAPI FakeRegOpenKeyExA(HKEY root, LPCSTR path, DWORD options, REGSAM sam, PHKEY key);
LONG WINAPI FakeRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size);
LONG WINAPI FakeRegCloseKey(HKEY key);
#define RegOpenKeyExA FakeRegOpenKeyExA
#define RegQueryValueExA FakeRegQueryValueExA
#define RegCloseKey FakeRegCloseKey

#define protected public
#define private public
#include "game/console.h"
#include "game/game_texts.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/profile.h"
#include "net/dplay8.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "net/net_console.cpp"

#undef RegOpenKeyExA
#undef RegQueryValueExA
#undef RegCloseKey

#include "original_exe.h"

#define THISCALL __attribute__((thiscall))

namespace {

// ---- checks, logs ----
int checks, failures;
std::string g_round;
void Check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok) {
        ++failures;
        if (failures <= 40)
            std::printf("FAIL %s: %s\n", g_round.c_str(), what.c_str());
    }
}
int g_side;   // 0 the port, 1 the original
std::vector<std::string> g_log[2];
void Log(const std::string &s) { g_log[g_side].push_back(s); }
std::string N(long long v) { return std::to_string(v); }
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%x", v);
    return b;
}
std::string Str(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }

std::mt19937 g_rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(g_rng) < p; }

// ---- the registry (both sides) ----
struct S_RegScript {
    LONG openResult;        // RegOpenKeyExA's
    uint32_t key;           // the key it gives
    LONG queryResult;       // RegQueryValueExA's (ERROR_MORE_DATA when the value does not fit)
    DWORD type;
    std::vector<uint8_t> value;
} g_reg;
uint8_t *g_keyBuffer[2];   // each side's CD key buffer

LONG OpenKey(HKEY root, LPCSTR path, DWORD options, REGSAM sam, PHKEY key)
{
    Log("open " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))) + " " + Str(path) + " " + N(options) + " " +
        Hex(sam) + " " + N(key != nullptr));
    if (g_reg.openResult == ERROR_SUCCESS)
        *key = reinterpret_cast<HKEY>(static_cast<uintptr_t>(g_reg.key));
    else if (P(0.5))
        *key = reinterpret_cast<HKEY>(static_cast<uintptr_t>(0x5A5A5A5A));   // (left as the call made it)
    return g_reg.openResult;
}
LONG QueryValue(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    const std::string where = data == g_keyBuffer[g_side] ? "key buffer" : "elsewhere";
    Log("query " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key))) + " " + Str(name) + " " +
        N(reserved != nullptr) + " " + N(type != nullptr) + " " + where + " " + (size ? N(*size) : std::string("no size")));
    if (!size)
        return ERROR_INVALID_PARAMETER;
    if (g_reg.queryResult != ERROR_SUCCESS) {
        if (g_reg.queryResult == ERROR_MORE_DATA)
            *size = static_cast<DWORD>(g_reg.value.size());
        return g_reg.queryResult;
    }
    if (g_reg.value.size() > *size) {
        *size = static_cast<DWORD>(g_reg.value.size());
        return ERROR_MORE_DATA;
    }
    if (type)
        *type = g_reg.type;
    if (data)
        std::memcpy(data, g_reg.value.data(), g_reg.value.size());
    *size = static_cast<DWORD>(g_reg.value.size());
    return ERROR_SUCCESS;
}
LONG CloseKey(HKEY key)
{
    Log("close " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key))));
    return ERROR_SUCCESS;
}

}   // namespace

LONG WINAPI FakeRegOpenKeyExA(HKEY root, LPCSTR path, DWORD options, REGSAM sam, PHKEY key)
{
    return OpenKey(root, path, options, sam, key);
}
LONG WINAPI FakeRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    return QueryValue(key, name, reserved, type, data, size);
}
LONG WINAPI FakeRegCloseKey(HKEY key) { return CloseKey(key); }

// ---- the port's game around ----
C_GameTexts g_texts;
C_str C_GameTexts::Get(int id) const
{
    Log("texts " + N(id));
    char text[16];
    std::snprintf(text, sizeof text, "T%X", id);
    return C_str(text);
}
alignas(16) static unsigned char g_consoleStorage[sizeof(C_Console)];
C_Console *GetConsole() { return reinterpret_cast<C_Console *>(g_consoleStorage); }
void C_Console::OnChar(char ch) { Log("char " + N(static_cast<unsigned char>(ch))); }
void C_Console::AddLine(const char *text) { Log("line " + Str(text)); }
void C_Console::MoveInputLineToHistory() { Log("history"); }
C_MpMap *g_pMap;
C_MpMap *S_MultiplayerSettings::GetCurrentMap()
{
    Log("current map");
    return g_pMap;
}
alignas(16) static unsigned char g_pSettings[0x1000];

namespace {

// ---- the original's ----
const uint32_t O_PRINT_LOAD_PROGRESS = 0x00731af0, O_GET_CD_KEY = 0x00732000, O_FSTR = 0x00405100;
const uint32_t O_LOADING = 0x009820fc, O_KEY = 0x0097d008, O_CONSOLE = 0x0098c100;

template <class T> T &G(uint32_t at) { return *OriginalAt<T *>(at); }
template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }

long g_oLive;   // the original's heap blocks alive
void *ONew(size_t n)
{
    ++g_oLive;
    return std::calloc(n ? n : 1, 1);
}
void ODelete(void *p)
{
    if (p)
        --g_oLive;
    std::free(p);
}
void *ORealloc(void *p, size_t n)
{
    if (!p)
        ++g_oLive;
    return std::realloc(p, n);
}
void THISCALL ONoLock(void *) {}
int OAtexit(void *) { return 0; }

alignas(16) uint8_t g_oConsole[0x100];
void THISCALL OOnChar(void *, char ch) { Log("char " + N(static_cast<unsigned char>(ch))); }
void THISCALL OAddLine(void *, const char *text) { Log("line " + Str(text)); }
void THISCALL OMoveInputLineToHistory(void *) { Log("history"); }
// The original's C_str: one pointer to { refCount, length, chars } (made by its C_fstr, on its heap).
void OMakeStr(void *at, const char *text)
{
    OF<void *>(at, 0) = nullptr;
    reinterpret_cast<void *(__cdecl *)(void *, const char *, ...)>(O_FSTR)(at, "%s", text);
}
void *THISCALL OTextsGet(void *, void *ret, int id)
{
    Log("texts " + N(id));
    char text[16];
    std::snprintf(text, sizeof text, "T%X", id);
    OMakeStr(ret, text);
    return ret;
}
// The original's map (0x48 bytes, packed: the name's C_str at +1).
alignas(16) uint8_t g_oMap[0x48];
void *THISCALL OGetCurrentMap(void *)
{
    Log("current map");
    return g_oMap;
}

// ---- the round ----
const char *const kNames[] = {"Arnhem", "", "Tobruk 2", "a", "Long name of a map with spaces and %s %d in it", "Monte Cassino"};

void SetMap()
{
    const int k = I(0, static_cast<int>(sizeof kNames / sizeof kNames[0]));   // the last: a null text
    static C_MpMap *map;
    delete map;
    map = k < static_cast<int>(sizeof kNames / sizeof kNames[0]) ? new C_MpMap(C_str(kNames[k]), C_str("dir"))
                                                               : new C_MpMap(C_str(), C_str("dir"));
    g_pMap = map;
    void *&rep = *reinterpret_cast<void **>(g_oMap + 1);
    ODelete(rep);
    rep = nullptr;
    if (k < static_cast<int>(sizeof kNames / sizeof kNames[0])) {
        void *str = nullptr;
        OMakeStr(&str, kNames[k]);
        rep = str;
    }
}

void SetKeyBuffers()
{
    for (int i = 0; i < 0x40; ++i) {
        const auto b = static_cast<uint8_t>(P(0.5) ? 0 : I(0, 255));
        g_keyBuffer[0][i] = b;
        g_keyBuffer[1][i] = b;
    }
}

void SetRegistry()
{
    static const LONG opens[] = {ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED};
    g_reg.openResult = opens[I(0, 4)];
    g_reg.key = P(0.1) ? 0xFFFFFFFFu : P(0.1) ? 0u : static_cast<uint32_t>(I(1, 0x7FFFFFFF));
    static const LONG queries[] = {ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, ERROR_MORE_DATA,
                                   ERROR_INVALID_HANDLE};
    g_reg.queryResult = queries[I(0, 5)];
    g_reg.type = P(0.8) ? REG_SZ : static_cast<DWORD>(I(0, 11));
    const int length = P(0.15) ? 0 : P(0.15) ? I(0x3E, 0x42) : I(1, 0x40);
    g_reg.value.resize(static_cast<size_t>(length));
    for (uint8_t &b : g_reg.value)
        b = static_cast<uint8_t>(P(0.8) ? I('0', 'Z') : I(0, 255));
}

int PercentArg()
{
    switch (I(0, 6)) {
    case 0:
        return 0;
    case 1:
        return 100;
    case 2:
        return 4 * I(-3, 26);
    case 3:
        return I(-10, 110);
    case 4:
        return P(0.5) ? INT_MIN + 4 * I(0, 2) : INT_MAX - I(0, 4);
    default:
        return I(0, 100);
    }
}

std::string Join(const std::vector<std::string> &v)
{
    std::string s;
    for (const std::string &l : v)
        s += "      " + l + "\n";
    return s;
}

void CompareLogs(const std::string &what)
{
    if (g_log[0] != g_log[1])
        Check(false, what + ": the calls differ:\n   port:\n" + Join(g_log[0]) + "   orig:\n" + Join(g_log[1]));
    else
        Check(true, what);
}

void CompareState(const std::string &what)
{
    Check(g_loadingMap == (G<int>(O_LOADING) != 0) && (G<int>(O_LOADING) == 0 || G<int>(O_LOADING) == 1),
          what + ": the loading flag " + N(g_loadingMap) + " / " + N(G<int>(O_LOADING)));
    Check(std::memcmp(g_keyBuffer[0], g_keyBuffer[1], 0x40) == 0, what + ": the key buffer");
    Check(g_oLive == 0, what + ": the original's heap blocks alive " + N(g_oLive));
}

void Round()
{
    if (P(0.3)) {
        const bool loading = P(0.5);
        g_loadingMap = loading;
        G<int>(O_LOADING) = loading;
    }
    if (P(0.2))
        SetKeyBuffers();
    if (P(0.3))
        SetMap();
    const long mapBlocks = g_oLive;
    g_oLive = 0;
    g_log[0].clear();
    g_log[1].clear();
    if (P(0.7)) {
        const int percent = PercentArg();
        g_round = "PrintLoadProgress " + N(percent);
        g_side = 0;
        reinterpret_cast<C_NetGame *>(g_pSettings)->PrintLoadProgress(percent);
        g_side = 1;
        static uint8_t oNet[0x1000];
        reinterpret_cast<void(THISCALL *)(void *, int)>(O_PRINT_LOAD_PROGRESS)(oNet, percent);
        CompareLogs(g_round);
    } else {
        SetRegistry();
        g_round = "GetCDKey open " + N(g_reg.openResult) + " query " + N(g_reg.queryResult) + " length " +
                  N(static_cast<long long>(g_reg.value.size()));
        g_side = 0;
        const char *p = GetCDKey();
        g_side = 1;
        const char *o = reinterpret_cast<const char *(*)()>(O_GET_CD_KEY)();
        CompareLogs(g_round);
        const std::string pr = p == nullptr ? "null" : p == g_cdKey ? "key" : "other";
        const std::string orr = o == nullptr ? "null" : o == OriginalAt<const char *>(O_KEY) ? "key" : "other";
        Check(pr == orr, g_round + ": the result " + pr + " / " + orr);
    }
    CompareState(g_round);
    g_oLive = mapBlocks;
}

// ---- the GUIDs ----
std::string GuidText(const GUID &g)
{
    char b[48];
    std::snprintf(b, sizeof b, "%08lX-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X", static_cast<unsigned long>(g.Data1),
                  g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
                  g.Data4[7]);
    return b;
}
void CheckGuid(const char *name, const GUID &port, uint32_t at)
{
    const GUID &orig = G<GUID>(at);
    Check(std::memcmp(&port, &orig, sizeof(GUID)) == 0,
          std::string(name) + " " + GuidText(port) + " / the original's " + Hex(at) + " " + GuidText(orig));
}
// The data address an instruction reads: MOV EAX,[imm] (A1) or MOV r32,[imm] (8B 05/0D/15/1D/25/2D/35/3D).
uint32_t ReadAddress(uint32_t code)
{
    const uint8_t *c = OriginalAt<const uint8_t *>(code);
    if (c[0] == 0xA1)
        return *reinterpret_cast<const uint32_t *>(c + 1);
    if (c[0] == 0x8B && (c[1] & 0xC7) == 0x05)
        return *reinterpret_cast<const uint32_t *>(c + 2);
    return 0;
}
void TestGuids()
{
    g_round = "GUIDs";
    CheckGuid("CLSID_NetDirectPlay8Server", CLSID_NetDirectPlay8Server, 0x008190a8);
    CheckGuid("IID_NetDirectPlay8Server", IID_NetDirectPlay8Server, 0x00819058);
    CheckGuid("CLSID_NetDirectPlay8Client", CLSID_NetDirectPlay8Client, 0x008190b8);
    CheckGuid("IID_NetDirectPlay8Client", IID_NetDirectPlay8Client, 0x00819068);
    CheckGuid("CLSID_NetDirectPlay8Address", CLSID_NetDirectPlay8Address, 0x008190e8);
    CheckGuid("IID_NetDirectPlay8Address", IID_NetDirectPlay8Address, 0x008190d8);
    CheckGuid("CLSID_NetDP8SP_TCPIP", CLSID_NetDP8SP_TCPIP, 0x00818fe8);
    CheckGuid("g_netApplicationGuid", g_netApplicationGuid, 0x00816350);
    CheckGuid("CLSID_NetVoiceClient", CLSID_NetVoiceClient, 0x008191b8);
    CheckGuid("IID_NetVoiceClient", IID_NetVoiceClient, 0x00819188);
    CheckGuid("CLSID_NetVoiceServer", CLSID_NetVoiceServer, 0x008191a8);
    CheckGuid("IID_NetVoiceServer", IID_NetVoiceServer, 0x00819178);
    CheckGuid("CLSID_NetVoiceTest", CLSID_NetVoiceTest, 0x00819198);
    CheckGuid("IID_NetVoiceTest", IID_NetVoiceTest, 0x00819168);
    CheckGuid("g_netVoicePlayback", g_netVoicePlayback, 0x00819488);
    CheckGuid("g_netVoiceCapture", g_netVoiceCapture, 0x00819478);

    // The codecs: StartVoiceServer loads the default (0x0072457f: MOV ECX,[0x00819108]) and switches on the setting
    // less 1 (0..6: the jump table at 0x00724704); each case reads its GUID's first word first, but the default's case
    // (it stores the registers loaded before: 0x0072463f). Anything else keeps the default.
    const uint32_t defaultAt = ReadAddress(0x0072457f);
    Check(defaultAt == 0x00819108, "the codecs' default read at " + Hex(defaultAt));
    CheckGuid("g_netVoiceCodecs[0] (the default)", g_netVoiceCodecs[0], defaultAt);
    const uint32_t *table = OriginalAt<const uint32_t *>(0x00724704);
    for (int codec = 1; codec <= 7; ++codec) {
        const uint32_t target = table[codec - 1];
        uint32_t at = target == 0x0072463f ? defaultAt : ReadAddress(target);
        // (The cases' first instruction may read a later word first; the GUID starts at the lowest word read.)
        if (at && target != 0x0072463f) {
            uint32_t lowest = at;
            for (uint32_t c = target; c < target + 0x30; ++c) {
                const uint32_t a = ReadAddress(c);
                if (a >= at - 12 && a <= at + 12 && a < lowest)
                    lowest = a;
            }
            at = lowest;
        }
        Check(at != 0, "codec " + N(codec) + ": no GUID read at " + Hex(target));
        if (at)
            CheckGuid(("g_netVoiceCodecs[" + N(codec) + "]").c_str(), g_netVoiceCodecs[codec], at);
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %s: code %lx at %p\n", g_round.c_str(), e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress);
    std::fflush(stdout);
    ExitProcess(3);
}

}   // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("misc_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);

    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e42d9, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e4375, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e562e, reinterpret_cast<const void *>(OAtexit));
    RedirectOriginal(0x00744c60, reinterpret_cast<const void *>(OOnChar));
    RedirectOriginal(0x00744cd0, reinterpret_cast<const void *>(OAddLine));
    RedirectOriginal(0x00744da0, reinterpret_cast<const void *>(OMoveInputLineToHistory));
    RedirectOriginal(0x006e5380, reinterpret_cast<const void *>(OTextsGet));
    RedirectOriginal(0x006ad830, reinterpret_cast<const void *>(OGetCurrentMap));
    // The registry: its import slots (the original calls through them).
    G<const void *>(0x0080f018) = reinterpret_cast<const void *>(&FakeRegOpenKeyExA);
    G<const void *>(0x0080f004) = reinterpret_cast<const void *>(&FakeRegQueryValueExA);
    G<const void *>(0x0080f000) = reinterpret_cast<const void *>(&FakeRegCloseKey);
    // The console made: its getter does not construct one.
    G<void *>(O_CONSOLE) = g_oConsole;
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_mpSettings = reinterpret_cast<S_MultiplayerSettings *>(g_pSettings);
    g_keyBuffer[0] = reinterpret_cast<uint8_t *>(g_cdKey);
    g_keyBuffer[1] = OriginalAt<uint8_t *>(O_KEY);

    const char *seedText = std::getenv("HD2_MISC_SEED");
    g_rng.seed(seedText ? static_cast<unsigned>(std::atoi(seedText)) : 1u);
    const char *roundsText = std::getenv("HD2_MISC_ROUNDS");
    const int rounds = roundsText ? std::atoi(roundsText) : std::getenv("HD2_MISC_QUICK") ? 10000 : 40000;

    TestGuids();
    g_loadingMap = false;
    G<int>(O_LOADING) = 0;
    SetKeyBuffers();
    SetMap();
    g_oLive = 0;
    for (int r = 0; r < rounds && failures < 40; ++r)
        Round();
    std::printf("misc_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
