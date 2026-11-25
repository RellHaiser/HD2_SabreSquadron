// Net part 6: the game's own console commands and their variables (net/net_console_vars.cpp: ExecuteConsoleLine,
// ExecuteConsoleCommand, GetCheatValue, the variables' range language ConsoleValueInRange) against the original
// executable's. MinGW; build_cvar.bat; runs cvar_diff.exe ("cvar_diff: N checks, 0 failures").
//
// What the commands reach is the test's on both sides, each in its program's layout: the game's Print, the controls'
// SetMouseScale, the net game's SendChat, the app's PostCommand, the team manager's player team (GetActivePlayer,
// HealMembers), the active player (its virtual Hit, its net id), the profile's difficulty, the items module (GiveItem,
// the infinite ammo flag: the original's byte 0x0098b02c), the item definitions (their virtual GetBehaviour and
// GetAmmoItemId; the original's table 0x0098b030) and the command processor's Execute (the lines no entry takes). The
// original's name lookup (a std::map, 0x0097b9cc) is made by its own static initialisers before the rounds. Every fake
// logs its call alike; after each line the result, the logs, the variables' values (the original's table 0x00844530)
// and the infinite ammo flag are compared. ConsoleValueInRange is also run alone on random ranges and numbers.
// HD2_CVAR_ROUNDS: rounds (default 40,000; HD2_CVAR_QUICK: 10,000); HD2_CVAR_SEED; HD2_CVAR_FIRST_CHANCE.
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/actor.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/team.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

// ---- the logs (both sides) ----
int g_side;
bool g_faking;
std::vector<std::string> g_log[2];
void Log(const std::string &s)
{
    if (g_faking)
        g_log[g_side].push_back(s);
}
std::string N(long long v) { return std::to_string(v); }
std::string Str(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }
// A chat's text: its first 100 characters. (The original copies at most 100 without ending a longer copy: it reads on
// past it; the port's Fix ends it. See the notes.)
std::string ChatText(const char *text) { return Str(std::string(text, strnlen(text, 100)).c_str()); }
std::string F(float f)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", f);
    return b;
}

// ---- the round's script ----
struct S_Script {
    int difficulty = 0;
    bool player = true;
    bool mouseAnswer = true;
    bool chatAnswer = true;
} g_script;

// ---- the item definitions: per type id, both programs' objects with the same answers ----
struct S_DefAnswers {
    bool exists;
    int behaviour;
    int ammo;
};
S_DefAnswers g_defs[500];

// ================================================================================================================
// The port's world.
// ================================================================================================================
uint32_t g_gameRole;
int g_localPlayerSlot;

alignas(16) unsigned char g_gameStore[0x200];
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameStore); }
void C_game::Print(const char *format, ...) { Log("Print " + Str(format)); }

bool C_Controls::SetMouseScale(int x, int y, int z)
{
    Log("SetMouseScale " + N(x) + " " + N(y) + " " + N(z));
    return g_script.mouseAnswer;
}
bool C_NetGame::SendChat(const char *text, bool team)
{
    Log("SendChat " + ChatText(text) + " " + N(team));
    return g_script.chatAnswer;
}
alignas(16) unsigned char g_appStore[0x200];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_appStore); }
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    Log("PostCommand " + N(command) + " " + N(a) + " " + N(b) + " " + N(flush));
}
int C_GameProfile::GetDifficulty() const
{
    Log("GetDifficulty");
    return g_script.difficulty;
}

// The active player: a raw object, its vtable's Hit slot (found from the member pointer) a logger, its net id set.
alignas(16) unsigned char g_portPlayer[0x400];
void *g_portPlayerVtable[0x100];
void __attribute__((thiscall)) PortHit(void *, E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *by, int part)
{
    Log("Hit " + N(type) + " " + F(amount) + " " + F(pos.x) + " " + F(pos.y) + " " + F(pos.z) + " " + F(dir.x) + " " + F(dir.y) + " " +
        F(dir.z) + " " + N(by != nullptr) + " " + N(part));
}
C_Human *C_Team::GetActivePlayer() const
{
    Log("GetActivePlayer");
    return g_script.player ? reinterpret_cast<C_Human *>(g_portPlayer) : nullptr;
}
void C_Team::HealMembers() { Log("HealMembers"); }

// The items.
C_Item *C_ItemsModule::GiveItem(int typeId, int unused, C_Actor *actor, int unused2)
{
    Log("GiveItem " + N(typeId) + " " + N(unused) + " " + N(actor == reinterpret_cast<C_Actor *>(g_portPlayer) ? 1 : actor ? 2 : 0) + " " +
        N(unused2));
    return nullptr;
}
alignas(16) unsigned char g_portDefs[500][16];
void *g_portDefVtable[0x40];
int __attribute__((thiscall)) PortBehaviour(void *self)
{
    const int type = static_cast<int>((static_cast<unsigned char(*)[16]>(self) - g_portDefs));
    Log("GetBehaviour " + N(type));
    return g_defs[type].behaviour;
}
int __attribute__((thiscall)) PortAmmo(void *self)
{
    const int type = static_cast<int>((static_cast<unsigned char(*)[16]>(self) - g_portDefs));
    Log("GetAmmoItemId " + N(type));
    return g_defs[type].ammo;
}
C_ItemDef *GetItemDef(int typeId)
{
    return typeId >= 0 && typeId < 500 && g_defs[typeId].exists ? reinterpret_cast<C_ItemDef *>(g_portDefs[typeId]) : nullptr;
}

// The command processor (the lines no entry takes).
alignas(16) unsigned char g_processorStore[0x10];
C_CommandProcessor *GetCommandProcessor() { return reinterpret_cast<C_CommandProcessor *>(g_processorStore); }
bool C_CommandProcessor::Execute(const char *command)
{
    Log("Execute " + Str(command));
    return true;
}

namespace {

// ================================================================================================================
// The original's world.
// ================================================================================================================
template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))

const uint32_t O_LINE = 0x00720500, O_COMMAND = 0x00720710, O_CHEAT = 0x00720f50, O_IN_RANGE = 0x00721f60;
const uint32_t O_ENTRIES = 0x00844530, O_INFINITE_AMMO = 0x0098b02c, O_DEFS = 0x0098b030;
const uint32_t O_ROLE = 0x00982100, O_LOCAL_SLOT = 0x00982104, O_PROCESSOR = 0x0098c0fc;
const uint32_t O_RANGE_CURSOR = 0x0097b9f0;

alignas(16) uint8_t g_oGame[0x100], g_oApp[0x100], g_oPlayer[0x100], g_oProcessor[0x10];
void *g_oPlayerVtable[0x80];
alignas(16) uint8_t g_oDefs[500][16];
void *g_oDefVtable[0x10];

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }
void *ORealloc(void *p, size_t n) { return std::realloc(p, n); }
// std::_Lockit (the original's MSVC6 containers lock a critical section its CRT's start-up made): one thread here.
void THISCALL ONoLock(void *) {}

void *OGetGame() { return g_oGame; }
void __cdecl OPrint(void *, const char *format, ...) { Log("Print " + Str(format)); }
bool THISCALL OSetMouseScale(void *, int x, int y, int z)
{
    Log("SetMouseScale " + N(x) + " " + N(y) + " " + N(z));
    return g_script.mouseAnswer;
}
bool THISCALL OSendChat(void *, const char *text, bool team)
{
    Log("SendChat " + ChatText(text) + " " + N(team));
    return g_script.chatAnswer;
}
void *OGetGameApp() { return g_oApp; }
void THISCALL OPostCommand(void *, int command, intptr_t a, intptr_t b, bool flush)
{
    Log("PostCommand " + N(command) + " " + N(a) + " " + N(b) + " " + N(flush));
}
int THISCALL OGetDifficulty(void *)
{
    Log("GetDifficulty");
    return g_script.difficulty;
}
void *THISCALL OGetActivePlayer(void *)
{
    Log("GetActivePlayer");
    return g_script.player ? g_oPlayer : nullptr;
}
void THISCALL OHealMembers(void *) { Log("HealMembers"); }
void THISCALL OHit(void *, int type, float amount, const float *pos, const float *dir, void *by, int part)
{
    Log("Hit " + N(type) + " " + F(amount) + " " + F(pos[0]) + " " + F(pos[1]) + " " + F(pos[2]) + " " + F(dir[0]) + " " + F(dir[1]) + " " +
        F(dir[2]) + " " + N(by != nullptr) + " " + N(part));
}
void *THISCALL OGiveItem(void *, int typeId, int unused, void *actor, int unused2)
{
    Log("GiveItem " + N(typeId) + " " + N(unused) + " " + N(actor == g_oPlayer ? 1 : actor ? 2 : 0) + " " + N(unused2));
    return nullptr;
}
int THISCALL OBehaviour(void *self)
{
    const int type = static_cast<int>(static_cast<uint8_t(*)[16]>(self) - g_oDefs);
    Log("GetBehaviour " + N(type));
    return g_defs[type].behaviour;
}
int THISCALL OAmmo(void *self)
{
    const int type = static_cast<int>(static_cast<uint8_t(*)[16]>(self) - g_oDefs);
    Log("GetAmmoItemId " + N(type));
    return g_defs[type].ammo;
}
bool THISCALL OExecute(void *, const char *command)
{
    Log("Execute " + Str(command));
    return true;
}

// ---- checks ----
int checks, failures;
int g_round;
std::string g_line;
void Fail(const std::string &what)
{
    ++failures;
    if (failures <= 40)
        std::printf("round %d, %s: %s\n", g_round, Str(g_line.c_str()).c_str(), what.c_str());
}
void Check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok)
        Fail(what);
}

std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }

void CompareLogs()
{
    if (g_log[0] == g_log[1]) {
        ++checks;
        return;
    }
    std::string s = "the calls differ:";
    const size_t n = std::max(g_log[0].size(), g_log[1].size());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < g_log[1].size() ? g_log[1][i] : "-", y = i < g_log[0].size() ? g_log[0][i] : "-";
        if (x != y) {
            s += " (call " + N(i) + ")\n     orig: " + x.substr(0, 200) + "\n     port: " + y.substr(0, 200);
            break;
        }
    }
    Check(false, s);
}

// The variables (the port's table, the original's 0x00844530: its value at +0x14, 0x18 each).
constexpr int ENTRY_COUNT = 11;
// (The port's table is its file's own: its variables' values read by GetCheatValue; the others never change.)
void CompareVariables()
{
    std::string s;
    for (int i = 0; i < ENTRY_COUNT; ++i) {
        const int a = GetCheatValue(i);
        const int b = reinterpret_cast<int(FAST *)(int)>(O_CHEAT)(i);
        if (a != b)
            s += "entry " + N(i) + " " + N(b) + " / " + N(a) + "; ";
    }
    if (reinterpret_cast<C_ItemsModule *>(&g_itemsModule)->m_infiniteAmmo != (G<uint8_t>(O_INFINITE_AMMO) != 0))
        s += "the infinite ammo " + N(G<uint8_t>(O_INFINITE_AMMO)) + "; ";
    Check(s.empty(), s);
}

// ---- the world ----
const int GIVEN_TYPES[] = {0,   1,   2,   3,   5,   4,  6,  7,  8,  10, 11, 12, 14, 13, 15, 16, 17, 18, 19,  20,  21,  22, 23, 24, 25, 26, 28,
                           29,  31,  38,  39,  53,  54, 99, 229, 56, 57, 59, 60, 62, 61, 63, 66, 67, 68, 70, 71, 251, 95, 97, 101, 222, 228, 231};

void SetWorld()
{
    static const uint32_t roles[] = {7, 1, 2, 3, 0, 5};
    g_gameRole = roles[I(0, 5)];
    G<uint32_t>(O_ROLE) = g_gameRole;
    g_localPlayerSlot = I(-1, 4);
    G<int>(O_LOCAL_SLOT) = g_localPlayerSlot;
    g_script.difficulty = I(0, 4);
    if (P(0.4)) {
        // (A local game: the cheats allowed below difficulty 4.)
        g_gameRole = 7;
        G<uint32_t>(O_ROLE) = 7;
    }
    g_script.player = P(0.85);
    g_script.mouseAnswer = P(0.5);
    g_script.chatAnswer = P(0.5);
    // The player's net id (none, the local slot, another).
    const int netId = P(0.3) ? -1 : (P(0.6) ? g_localPlayerSlot : I(0, 6));
    reinterpret_cast<C_Actor *>(g_portPlayer)->m_netId = netId;
    OF<int>(g_oPlayer, 0x10) = netId;
    // The item definitions of the given names' types (and a few others).
    for (int t : GIVEN_TYPES) {
        S_DefAnswers &d = g_defs[t];
        d.exists = P(0.85);
        const int kind = I(0, 3);
        d.behaviour = kind == 0 ? ITEM_BEHAVIOUR_THROWN : kind == 1 ? ITEM_BEHAVIOUR_STICKY_CHARGE : I(0, 12);
        d.ammo = P(0.5) ? -1 : (P(0.2) ? 0 : I(0, 300));
        G<void *>(O_DEFS + 4 * static_cast<uint32_t>(t)) = d.exists ? g_oDefs[t] : nullptr;
    }
    // The variables' values: the original's table takes the port's (they change only by the commands compared).
    for (int i = 0; i < ENTRY_COUNT; ++i)
        if (GetCheatValue(i) != -1)
            G<int>(O_ENTRIES + 0x18 * static_cast<uint32_t>(i) + 0x14) = GetCheatValue(i);
    const bool ammo = P(0.5);
    reinterpret_cast<C_ItemsModule *>(&g_itemsModule)->m_infiniteAmmo = ammo;
    G<uint8_t>(O_INFINITE_AMMO) = ammo;
}

// ---- the lines ----
const char *const ENTRY_NAMES[] = {"sensitivity", "say",     "sayteam", "kill",  "happyammo", "hydroshock",
                                   "sniper",      "heal",    "giveitem", "anorexy", "no_gi"};
const char *const ITEM_NAMES[] = {
    "PARABELLUM", "ENFIELD", "COLT", "TAISHO", "TOKAREV", "K98", "LEE", "DELISLE", "MOSIN", "GARAND", "CARBINE", "ARISAKA",
    "SPRINGFIELD", "K98OPT", "SPRINGFIELDOPT", "ARISAKAOPT", "LEEOPT", "MP40", "ZB26", "THOMPSON", "STENGUN", "STENGUNSIL",
    "SHOTGUN", "SPAGIN", "PPS", "MP44", "BAR", "BRENGUN", "DEGTYAREV", "PANZERFAUST", "BAZOOKA", "MEDIKITBIG", "MEDIKIT",
    "BINOCULARS", "CAMERA", "KNIFEGER", "KNIFESAS", "WIRECUTTER", "GRENADE36", "GRENADE69", "GRENADEGER", "GRENADEJAP",
    "CLUMMYBOMB", "EXPLOSIVESBAG", "DYNAMITE", "MINE", "TANKMINE", "MAGNETICMINE", "BACKPACKRUS", "BACKPACKGER",
    "BACKPACKGB1", "BACKPACKGB2", "BACKPACKGB3", "AQUALUNG", "mp40", "Garand", "medikit", "NOSUCHITEM", "", "dynamitex"};
constexpr int ITEM_NAME_COUNT = static_cast<int>(sizeof ITEM_NAMES / sizeof ITEM_NAMES[0]);
std::string RandomArgument()
{
    switch (I(0, 12)) {
    case 0: return "";
    case 1: return N(I(-5, 5));
    case 2: return N(I(0, 2));
    case 3: return "0x" + N(I(1, 19));
    case 4: return "0" + N(I(1, 7));
    case 5: return "\"" + std::string(ITEM_NAMES[I(0, ITEM_NAME_COUNT - 1)]) + "\"";
    case 6: return "\"";
    case 7: return ITEM_NAMES[I(0, ITEM_NAME_COUNT - 1)];
    case 8: return "hello world";
    case 9: return "12abc";
    case 10: return std::string(static_cast<size_t>(I(95, 110)), 'x');
    default: return N(I(-100, 100));
    }
}
std::string RandomLine()
{
    std::string line;
    switch (I(0, 9)) {
    case 0: line = "." + RandomArgument(); break;
    case 1: line = std::string(1, static_cast<char>(I('a', 'z'))) + "," + RandomArgument(); break;
    case 2: line = std::string(1, static_cast<char>(I('a', 'z'))); break;
    case 3: line = "maxclients " + N(I(0, 40)); break;
    case 4: line = "unknownword " + RandomArgument(); break;
    case 5: line = std::string(static_cast<size_t>(I(495, 505)), 'y'); break;
    default: {
        const int entry = I(0, ENTRY_COUNT - 1);
        line = ENTRY_NAMES[entry];
        if (entry == 8 && P(0.8))
            line += std::string(" ") + ITEM_NAMES[I(0, ITEM_NAME_COUNT - 1)];
        else if (P(0.9))
            line += std::string(static_cast<size_t>(I(1, 3)), ' ') + RandomArgument();
        if (P(0.3))
            line += std::string(static_cast<size_t>(I(1, 3)), ' ');
        break;
    }
    }
    if (P(0.05))
        line = "";
    return line;
}

void RunLine(const std::string &text)
{
    g_line = text;
    // (Each side a copy: the original writes into the line to trim it.)
    // (Zeros after it, as the game's chat buffer has: the original reads the second character of an empty line.)
    std::vector<char> a(text.begin(), text.end()), b(text.begin(), text.end());
    a.resize(a.size() + 8, 0);
    b.resize(b.size() + 8, 0);
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    const int x = ExecuteConsoleLine(a.data());
    g_side = 1;
    const int y = reinterpret_cast<int(FAST *)(const char *)>(O_LINE)(b.data());
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareVariables();
}

// ExecuteConsoleCommand alone on any entry (the line's own split gives only the table's names).
void RunCommand()
{
    const int command = I(0, ENTRY_COUNT - 1);
    const std::string args = (P(0.3) ? std::string(static_cast<size_t>(I(1, 3)), ' ') : std::string()) + RandomArgument();
    g_line = std::string("(command ") + ENTRY_NAMES[command] + ") " + args;
    std::vector<char> a(args.begin(), args.end()), b(args.begin(), args.end());
    a.resize(a.size() + 8, 0);
    b.resize(b.size() + 8, 0);
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    const int x = ExecuteConsoleCommand(command, a.data());
    g_side = 1;
    const int y = reinterpret_cast<int(FAST *)(int, const char *)>(O_COMMAND)(command, b.data());
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareVariables();
}

// The range language alone: random ranges (its grammar, or anything) and numbers.
std::string RandomRange()
{
    if (P(0.15)) {
        std::string s;
        const char chars[] = "0123456789-!&*,:<>=% \tx";
        for (int k = I(0, 10); k > 0; --k)
            s += chars[I(0, static_cast<int>(sizeof chars) - 2)];
        return s;
    }
    if (P(0.1))
        return P(0.5) ? "*" : " * ";
    std::string s;
    for (int t = I(1, 3); t > 0; --t) {
        for (int k = I(0, 2) == 0 ? I(1, 3) : 0; k > 0; --k)
            s += "!";
        for (int a = I(0, 2) == 0 ? 2 : 1; a > 0; --a) {
            switch (I(0, 6)) {
            case 0: s += P(0.5) ? N(I(-5, 5)) : (P(0.5) ? N(I(-120, 120)) : "0" + N(I(0, 9))); break;
            case 1: s += N(I(-5, 5)) + ":" + (P(0.5) ? N(I(-5, 9)) : N(I(10, 66))); break;
            case 2: s += "<" + N(I(-5, 5)); break;
            case 3: s += "<=" + N(I(-5, 5)); break;
            case 4: s += ">" + N(I(-5, 5)); break;
            case 5: s += ">=" + N(I(-5, 5)); break;
            default: s += "%" + N(I(1, 4)); break;
            }
            if (a > 1)
                s += P(0.7) ? "&" : " & ";
        }
        if (t > 1)
            s += P(0.7) ? "," : " , ";
    }
    if (P(0.1))
        s += P(0.5) ? "," : "x";
    return s;
}
void RunRange()
{
    const std::string range = RandomRange();
    const int number = P(0.6) ? I(-7, 9) : I(-130, 130);
    g_line = "(range " + std::to_string(number) + ") " + range;
    // ('%' with no number after it, or 0, divides by zero on both sides (the token's value 0): none given.)
    for (size_t at = range.find('%'); at != std::string::npos; at = range.find('%', at + 1)) {
        size_t k = at + 1;
        while (k < range.size() && (range[k] == ' ' || range[k] == '\t'))
            ++k;
        if (k < range.size() && range[k] == '-')
            ++k;
        if (k >= range.size() || range[k] < '1' || range[k] > '9')
            return;
    }
    // (A comparison mark with no number after it compares against an undefined value in the original (its stack
    // temporary's): none given.)
    for (size_t at = 0; at < range.size(); ++at) {
        if (range[at] != '<' && range[at] != '>')
            continue;
        size_t k = at + 1;
        if (k < range.size() && range[k] == '=')
            ++k;
        while (k < range.size() && (range[k] == ' ' || range[k] == '\t'))
            ++k;
        if (k < range.size() && range[k] == '-')
            ++k;
        if (k >= range.size() || range[k] < '0' || range[k] > '9')
            return;
    }
    bool a = false, b = false;
    const bool x = ConsoleValueInRange(number, range.c_str(), &a);
    const bool y = reinterpret_cast<bool(FAST *)(int, const char *, bool *)>(O_IN_RANGE)(number, range.c_str(), &b);
    Check(x == y && a == b, "ConsoleValueInRange " + N(y) + " " + N(b) + " / " + N(x) + " " + N(a));
}

void Round()
{
    SetWorld();
    for (int k = I(1, 6); k > 0; --k) {
        const int op = I(0, 9);
        if (op < 6)
            RunLine(RandomLine());
        else if (op < 8)
            RunCommand();
        else
            RunRange();
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress,
                g_round, Str(g_line.c_str()).c_str(), g_side ? "the original" : "the port");
    const auto *sp = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(e->ContextRecord->Esp));
    std::printf("  the original's code on the stack:");
    for (int i = 0, n = 0; i < 256 && n < 12; ++i)
        if (sp[i] >= 0x401000 && sp[i] < 0x80d000) {
            std::printf(" %08x", sp[i]);
            ++n;
        }
    std::printf("\n");
    std::fflush(stdout);
    ExitProcess(3);
}
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        OnCrash(e);
    return EXCEPTION_CONTINUE_SEARCH;
}

// A virtual function's vtable slot in the port (g++: a member pointer to a virtual holds its offset + 1).
template <class M> int Slot(M member)
{
    uint32_t raw;
    std::memcpy(&raw, &member, 4);
    return (raw & 1) ? static_cast<int>((raw - 1) / 4) : -1;
}

}   // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_CVAR_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("cvar_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    // The port's fakes: the player's Hit, the definitions' GetBehaviour / GetAmmoItemId (their vtables' slots).
    const int hit = Slot(&C_Actor::Hit), behaviour = Slot(&C_ItemDef::GetBehaviour), ammo = Slot(&C_ItemDef::GetAmmoItemId);
    if (hit < 0 || behaviour < 0 || ammo < 0) {
        std::printf("cvar_diff: a virtual's slot not found\n");
        return 1;
    }
    g_portPlayerVtable[hit] = reinterpret_cast<void *>(&PortHit);
    OF<void *>(g_portPlayer, 0) = g_portPlayerVtable;
    g_portDefVtable[behaviour] = reinterpret_cast<void *>(&PortBehaviour);
    g_portDefVtable[ammo] = reinterpret_cast<void *>(&PortAmmo);
    for (auto &d : g_portDefs)
        OF<void *>(d, 0) = g_portDefVtable;
    // The original's.
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e42d9, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e4375, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x006987d0, reinterpret_cast<const void *>(OGetGame));
    RedirectOriginal(0x0069c2b0, reinterpret_cast<const void *>(OPrint));
    RedirectOriginal(0x006c6fe0, reinterpret_cast<const void *>(OSetMouseScale));
    RedirectOriginal(0x0072ee60, reinterpret_cast<const void *>(OSendChat));
    RedirectOriginal(0x006dcd10, reinterpret_cast<const void *>(OGetGameApp));
    RedirectOriginal(0x0072b590, reinterpret_cast<const void *>(OPostCommand));
    RedirectOriginal(0x006b6a20, reinterpret_cast<const void *>(OGetDifficulty));
    RedirectOriginal(0x0045d1d0, reinterpret_cast<const void *>(OGetActivePlayer));
    RedirectOriginal(0x0045f020, reinterpret_cast<const void *>(OHealMembers));
    RedirectOriginal(0x007d7d90, reinterpret_cast<const void *>(OGiveItem));
    RedirectOriginal(0x00742850, reinterpret_cast<const void *>(OExecute));
    for (void *&slot : g_oPlayerVtable)
        slot = nullptr;
    g_oPlayerVtable[0xD4 / 4] = reinterpret_cast<void *>(&OHit);
    OF<void *>(g_oPlayer, 0) = g_oPlayerVtable;
    g_oDefVtable[0x10 / 4] = reinterpret_cast<void *>(&OBehaviour);
    g_oDefVtable[0x18 / 4] = reinterpret_cast<void *>(&OAmmo);
    for (auto &d : g_oDefs)
        OF<void *>(d, 0) = g_oDefVtable;
    G<void *>(O_PROCESSOR) = g_oProcessor;
    // The original's name lookup: its static initialisers (the map made, then filled from the table).
    reinterpret_cast<void(__cdecl *)()>(0x007200c0)();
    reinterpret_cast<void(__cdecl *)()>(0x00720240)();
    int rounds = std::getenv("HD2_CVAR_QUICK") ? 10000 : 40000;
    if (const char *r = std::getenv("HD2_CVAR_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_CVAR_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("cvar_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
