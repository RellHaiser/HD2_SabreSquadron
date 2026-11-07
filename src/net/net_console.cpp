// The network's odds and ends (part 10d-vii): a dedicated server's loading progress on its console, the CD key from
// the registry. See net_game.h, pending.h.
#include <winsock2.h>
#include <windows.h>

#include "net/net_game.h"

#include "game/console.h"
#include "game/game_texts.h"
#include "game/pending.h"

namespace {

// The texts: loading the map, the map loaded.
constexpr int TEXT_LOADING_MAP = 0x22C8;
constexpr int TEXT_MAP_LOADED = 0x22C9;

// A map is loading (the dots go on; 0x009820fc).
bool g_loadingMap;
// The CD key read (0x0097d008).
char g_cdKey[0x40];

}  // namespace

// The game's role (GAME_ROLE_*): local until a session starts (the static initialiser 0x00732da0).
uint32_t g_gameRole = GAME_ROLE_LOCAL;                  // 0x00982100

void C_NetGame::PrintLoadProgress(int percent)
{
    // A dot each 4 % while a map loads; at 0 the map's name, at 100 the end.
    if (percent % 4 == 0 && g_loadingMap)
        GetConsole()->OnChar('.');
    if (percent == 100) {
        if (!g_loadingMap)
            return;
        GetConsole()->MoveInputLineToHistory();
        const C_str text = g_texts.Get(TEXT_MAP_LOADED);
        GetConsole()->AddLine(C_fstr("%s", text.c_str()).c_str());
        g_loadingMap = false;
    } else if (percent == 0 && !g_loadingMap) {
        const C_str text = g_texts.Get(TEXT_LOADING_MAP);
        C_str line = C_fstr("%s  ", text.c_str());
        line += g_gameProfile.m_mpSettings->GetCurrentMap()->GetName();
        GetConsole()->AddLine(line.c_str());
        g_loadingMap = true;
    }
}

const char *GetCDKey()
{
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Illusion Softworks\\Hidden & Dangerous 2", 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
        key = reinterpret_cast<HKEY>(-1);
    DWORD size = sizeof g_cdKey;
    DWORD type;
    const bool read =
        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS && size;
    if (key != reinterpret_cast<HKEY>(-1))
        RegCloseKey(key);
    return read ? g_cdKey : nullptr;
}
