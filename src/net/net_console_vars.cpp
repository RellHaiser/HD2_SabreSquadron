// The game's own console commands (part 10e-ii): the chat line's words before the command processor: sensitivity, say,
// kill, the cheats and their variables (their values limited by a small range language). See game/pending.h.
#include <winsock2.h>
#include <windows.h>

#include "net/net_game.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/team.h"
#include "game/vehicle_spawn.h"

namespace {

// ExecuteConsoleCommand's results.
enum {
    CONSOLE_OK = 0,
    CONSOLE_NOT_HANDLED = 2,
    CONSOLE_BAD_TYPE = 3,
    CONSOLE_BAD_NUMBER = 4,
    CONSOLE_OUT_OF_RANGE = 5,
};

constexpr int DIFFICULTY_NO_CHEATS = 4;
constexpr int CHAT_TEXT_SIZE = 100;
constexpr int LINE_LIMIT = 500;
constexpr int GIVEN_AMMO = 6;
constexpr int GIVEN_THROWN = 5;

enum E_ValueType { VALUE_INT = 1, VALUE_TEXT = 2 };

struct S_ConsoleEntry {                                 // 0x18 bytes (0x00844530)
    const char *name;
    int takesValue;                                     // +0x04 (all of them)
    int valueType;                                      // +0x08
    bool variable;                                      // +0x0C keeps a value
    const char *range;                                  // +0x10 what it may be
    int value;                                          // +0x14 (a text variable's std::string in the original: none
                                                        //        in the table, dropped with 0x00720ea0)
};
S_ConsoleEntry g_entries[] = {
    {"sensitivity", 1, VALUE_INT, false, nullptr, 0},   {"say", 1, VALUE_TEXT, false, nullptr, 0},
    {"sayteam", 1, VALUE_TEXT, false, nullptr, 0},      {"kill", 1, VALUE_TEXT, false, nullptr, 0},
    {"happyammo", 1, VALUE_INT, false, nullptr, 0},     {"hydroshock", 1, VALUE_INT, true, "0,1", 0},
    {"sniper", 1, VALUE_INT, true, "0,1", 0},           {"heal", 1, VALUE_INT, false, nullptr, 0},
    {"giveitem", 1, VALUE_TEXT, false, nullptr, 0},     {"anorexy", 1, VALUE_INT, false, nullptr, 0},
    {"no_gi", 1, VALUE_INT, true, "0,1", 0},
};

// The names' lookup (0x0097b9cc; made from the table).
const std::map<std::string, int> &EntryIndex()
{
    static const std::map<std::string, int> index = [] {
        std::map<std::string, int> made;
        int i = 0;
        for (const S_ConsoleEntry &entry : g_entries)
            made[entry.name] = i++;
        return made;
    }();
    return index;
}

const struct {
    int typeId;
    const char *name;
} GIVEN_ITEMS[] = {                                     // 0x00844378
    {0, "PARABELLUM"}, {1, "ENFIELD"}, {2, "COLT"}, {3, "TAISHO"}, {5, "TOKAREV"}, {4, "K98"}, {6, "LEE"},
    {7, "DELISLE"}, {8, "MOSIN"}, {10, "GARAND"}, {11, "CARBINE"}, {12, "ARISAKA"}, {14, "SPRINGFIELD"}, {13, "K98OPT"},
    {15, "SPRINGFIELDOPT"}, {16, "ARISAKAOPT"}, {17, "LEEOPT"}, {18, "MP40"}, {19, "ZB26"}, {20, "THOMPSON"},
    {21, "STENGUN"}, {22, "STENGUNSIL"}, {23, "SHOTGUN"}, {24, "SPAGIN"}, {25, "PPS"}, {26, "MP44"}, {28, "BAR"},
    {29, "BRENGUN"}, {31, "DEGTYAREV"}, {38, "PANZERFAUST"}, {39, "BAZOOKA"}, {53, "MEDIKITBIG"}, {54, "MEDIKIT"},
    {99, "BINOCULARS"}, {229, "CAMERA"}, {56, "KNIFEGER"}, {57, "KNIFESAS"}, {59, "WIRECUTTER"}, {60, "GRENADE36"},
    {62, "GRENADE69"}, {61, "GRENADEGER"}, {63, "GRENADEJAP"}, {66, "CLUMMYBOMB"}, {67, "EXPLOSIVESBAG"},
    {68, "DYNAMITE"}, {70, "MINE"}, {71, "TANKMINE"}, {251, "MAGNETICMINE"}, {95, "BACKPACKRUS"}, {97, "BACKPACKGER"},
    {101, "BACKPACKGB1"}, {222, "BACKPACKGB2"}, {228, "BACKPACKGB3"}, {231, "AQUALUNG"},
};

// ---- the range language: terms separated by ',': [!]... primary [& term]; a primary: *, n, n:m, <n, <=n, >n, >=n, %n ----

enum E_RangeToken { RANGE_END = 0, RANGE_NUMBER = 1, RANGE_MARK = 2 };
constexpr int MARK_AT_MOST = 1;                         // <=
constexpr int MARK_AT_LEAST = 2;                        // >=

struct S_RangeToken {
    int value;
    int kind;
};

const char *g_rangeCursor;                              // 0x0097b9f0
S_RangeToken g_rangeToken;                              // 0x0097b9e8

S_RangeToken ReadRangeToken()                           // 0x00722010
{
    while (*g_rangeCursor == ' ' || *g_rangeCursor == '\t')
        ++g_rangeCursor;
    char c = *g_rangeCursor++;
    if (c == '!')
        return {'!', RANGE_MARK};
    int sign = 1;
    if (c == '-') {
        c = *g_rangeCursor++;
        sign = -1;
        if (c < '0' || c > '9')
            return {0, RANGE_END};
    }
    if (c >= '0' && c <= '9') {
        int value = c - '0';
        while (*g_rangeCursor >= '0' && *g_rangeCursor <= '9')
            value = value * 10 + (*g_rangeCursor++ - '0');
        return {value * sign, RANGE_NUMBER};
    }
    switch (c) {
    case 0:
        --g_rangeCursor;
        return {0, RANGE_END};
    case '%':
    case '&':
    case '*':
    case ',':
    case ':':
        return {c, RANGE_MARK};
    case '<':
        if (*g_rangeCursor == '=') {
            ++g_rangeCursor;
            return {MARK_AT_MOST, RANGE_MARK};
        }
        return {'<', RANGE_MARK};
    case '>':
        if (*g_rangeCursor == '=') {
            ++g_rangeCursor;
            return {MARK_AT_LEAST, RANGE_MARK};
        }
        return {'>', RANGE_MARK};
    default:
        return {0, RANGE_END};
    }
}

void NextRangeToken() { g_rangeToken = ReadRangeToken(); }
bool IsRangeMark(int mark) { return g_rangeToken.kind == RANGE_MARK && g_rangeToken.value == mark; }

bool __fastcall ParseRangePrimary(int number, bool *in)   // 0x00722250
{
    if (g_rangeToken.kind == RANGE_NUMBER) {
        const int low = g_rangeToken.value;
        NextRangeToken();
        if (!IsRangeMark(':')) {
            if (number == low)
                *in = true;
            return true;
        }
        NextRangeToken();
        if (g_rangeToken.kind != RANGE_NUMBER)
            return false;
        if (low <= number && number <= g_rangeToken.value)
            *in = true;
        NextRangeToken();
        return true;
    }
    if (g_rangeToken.kind != RANGE_MARK)
        return false;
    const int mark = g_rangeToken.value;
    if (mark != MARK_AT_MOST && mark != MARK_AT_LEAST && mark != '%' && mark != '<' && mark != '>')
        return false;
    NextRangeToken();
    switch (mark) {
    case MARK_AT_MOST:
        *in = number <= g_rangeToken.value;
        break;
    case MARK_AT_LEAST:
        *in = g_rangeToken.value <= number;
        break;
    case '%':
        *in = number % g_rangeToken.value == 0;
        break;
    case '<':
        *in = number < g_rangeToken.value;
        break;
    default:
        *in = g_rangeToken.value < number;
        break;
    }
    NextRangeToken();
    return true;
}

bool __fastcall ParseRangeTerm(int number, bool *in)    // 0x00722190
{
    bool negate = false;
    while (IsRangeMark('!')) {
        negate = !negate;
        NextRangeToken();
    }
    if (!ParseRangePrimary(number, in))
        return false;
    const bool left = *in != negate;
    *in = left;
    if (!IsRangeMark('&'))
        return true;
    NextRangeToken();
    *in = false;
    if (!ParseRangeTerm(number, in))
        return false;
    *in = *in && left;
    return true;
}

}  // namespace

bool ConsoleValueInRange(int number, const char *range, bool *in)
{
    g_rangeCursor = range;                              // (0x00721f50)
    NextRangeToken();
    if (IsRangeMark('*')) {
        *in = true;
        return true;
    }
    *in = false;
    for (;;) {
        if (!ParseRangeTerm(number, in))
            return false;
        if (g_rangeToken.kind == RANGE_END)
            return true;
        if (!IsRangeMark(','))
            return false;
        NextRangeToken();
        if (*in)
            return true;
    }
}

namespace {

int ShowVariable(int index)                             // 0x00720d10
{
    const S_ConsoleEntry &entry = g_entries[index];
    if (entry.valueType != VALUE_INT) {
        GetGame()->Print("Error: Invalid variable type");
        return CONSOLE_OUT_OF_RANGE;
    }
    GetGame()->Print(C_fstr("%s = %i", entry.name, entry.value).c_str());
    return CONSOLE_OK;
}

int SetVariable(int index, int value)                   // 0x00720e50
{
    S_ConsoleEntry &entry = g_entries[index];
    bool in;
    if (!ConsoleValueInRange(value, entry.range, &in) || !in)
        return CONSOLE_OUT_OF_RANGE;
    entry.value = value;
    return CONSOLE_OK;
}

bool CheatsAllowed()
{
    return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL && g_gameProfile.GetDifficulty() != DIFFICULTY_NO_CHEATS;
}

C_Human *ActivePlayer()
{
    return g_teamManager.GetPlayerTeam()->GetActivePlayer();
}

void GiveNamedItem(const char *name)
{
    int typeId = -1;
    for (const auto &item : GIVEN_ITEMS) {
        if (_stricmp(item.name, name) == 0) {
            typeId = item.typeId;
            break;
        }
    }
    C_ItemDef *def = typeId >= 0 ? GetItemDef(typeId) : nullptr;
    if (!def)
        return;
    g_itemsModule.GiveItem(typeId, 0, ActivePlayer(), -1);
    if (def->GetAmmoItemId() >= 0) {
        for (int i = 0; i < GIVEN_AMMO; ++i)
            g_itemsModule.GiveItem(def->GetAmmoItemId(), 0, ActivePlayer(), -1);
        return;
    }
    if (def->GetBehaviour() == ITEM_BEHAVIOUR_THROWN || def->GetBehaviour() == ITEM_BEHAVIOUR_STICKY_CHARGE) {
        for (int i = 0; i < GIVEN_THROWN; ++i)
            g_itemsModule.GiveItem(typeId, 0, ActivePlayer(), -1);
    }
}

}  // namespace

int GetCheatValue(int index)
{
    const S_ConsoleEntry &entry = g_entries[index];
    return entry.variable && entry.valueType == VALUE_INT ? entry.value : -1;
}

int ExecuteConsoleCommand(int command, const char *args)
{
    // (The argument's spaces trimmed on a copy: the original writes into the line.)
    while (*args == ' ')
        ++args;
    std::string text(args);
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    const S_ConsoleEntry &entry = g_entries[command];
    if (entry.variable && text.empty())
        return ShowVariable(command);
    int number = 0;
    if (entry.valueType == VALUE_INT) {
        if (std::sscanf(text.c_str(), "%i", &number) != 1)
            return CONSOLE_BAD_NUMBER;
    } else if (text.size() > 1 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    } else if (text.size() == 1 && text[0] == '"') {
        // (One quote is both ends in the original: the text empty.)
        text.clear();
    }
    if (entry.variable)
        return SetVariable(command, number);
    switch (command) {
    case CONSOLE_SENSITIVITY:
        g_controls.SetMouseScale(number, number, 1);
        return CONSOLE_OK;
    case CONSOLE_SAY:
    case CONSOLE_SAY_TEAM: {
        // (Fix: terminated; the original's strncpy may not.)
        char line[CHAT_TEXT_SIZE + 1];
        std::strncpy(line, text.c_str(), CHAT_TEXT_SIZE);
        line[CHAT_TEXT_SIZE] = 0;
        g_netGame.SendChat(line, command == CONSOLE_SAY_TEAM);
        return CONSOLE_OK;
    }
    case CONSOLE_KILL: {
        C_Human *player = ActivePlayer();
        if ((g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL) {
            if (player) {
                const S_vector zero(0.0f, 0.0f, 0.0f);
                player->Hit(HIT_ENVIRONMENT, -1.0f, zero, zero, nullptr, 0);
            }
        } else if (player && player->GetNetId() != -1 && player->GetNetId() == g_localPlayerSlot) {
            GetGameApp()->PostCommand(4, player->GetNetId(), 0, false);
        }
        return CONSOLE_OK;
    }
    case CONSOLE_HAPPYAMMO:
        if (CheatsAllowed())
            g_itemsModule.SetInfiniteAmmo(number > 0);
        return CONSOLE_OK;
    case CONSOLE_HEAL:
        if (CheatsAllowed())
            g_teamManager.GetPlayerTeam()->HealMembers();
        return CONSOLE_OK;
    case CONSOLE_GIVEITEM:
        if (CheatsAllowed())
            GiveNamedItem(text.c_str());
        return CONSOLE_OK;
    case CONSOLE_ANOREXY:
        return CONSOLE_OK;
    default:
        return CONSOLE_NOT_HANDLED;
    }
}

int ExecuteConsoleLine(const char *line)
{
    if (ExecuteVehicleSpawnCommand(line))
        return CONSOLE_OK;
    // ".text": say; "x,text": say to the team.
    if (line[0] == '.')
        return ExecuteConsoleCommand(CONSOLE_SAY, line + 1);
    if (line[0] != 0 && line[1] == ',')
        return ExecuteConsoleCommand(CONSOLE_SAY_TEAM, line + 1);
    const char *rest = line;
    while (*rest != 0 && *rest != ' ')
        ++rest;
    std::string word(line, rest);
    while (*rest == ' ')
        ++rest;
    const auto found = EntryIndex().find(word);
    if (found != EntryIndex().end())
        return ExecuteConsoleCommand(found->second, rest);
    // Else the command processor's.
    word += ' ';
    word += rest;
    if (word.size() < LINE_LIMIT)
        GetCommandProcessor()->Execute(word.c_str());
    return CONSOLE_OK;
}
