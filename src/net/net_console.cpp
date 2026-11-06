// The network's odds and ends (part 10d-vii): a dedicated server's loading progress on its console, the CD key from
// the registry, the crash report sent to the developers (not called in this build). See net_game.h, pending.h.
#include <winsock2.h>
#include <windows.h>

#include "net/net_game.h"

#include <cstring>

#include "game/console.h"
#include "game/game_texts.h"
#include "game/pending.h"
#include "ls3d/igraph.h"
#include "net/gamespy.h"

namespace {

// The texts: loading the map, the map loaded; the crash report's question.
constexpr int TEXT_LOADING_MAP = 0x22C8;
constexpr int TEXT_MAP_LOADED = 0x22C9;
constexpr int TEXT_CRASH_REPORT = 0x578;

// A map is loading (the dots go on; 0x009820fc).
bool g_loadingMap;
// The CD key read (0x0097d008).
char g_cdKey[0x40];

// The crash report's dialog (resource 0x73) and its controls.
constexpr int CRASH_DIALOG = 0x73;
constexpr int CRASH_QUESTION = 0x3FC;
constexpr int CRASH_TEXT = 0x3FA;
// Where the report goes: 194.213.63.62, TCP port 15002.
constexpr uint8_t REPORT_ADDRESS[4] = {194, 213, 63, 62};
constexpr u_short REPORT_PORT = 15002;

INT_PTR __stdcall CrashReportProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)   // 0x00731e10
{
    switch (message) {
    case WM_CLOSE:
        EndDialog(dialog, 1);
        break;
    case WM_INITDIALOG:
        SetDlgItemTextA(dialog, CRASH_QUESTION, g_texts.Get(TEXT_CRASH_REPORT).c_str());
        SetDlgItemTextA(dialog, CRASH_TEXT, reinterpret_cast<const char *>(lParam));
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
            EndDialog(dialog, 0);
        else if (LOWORD(wParam) == IDCANCEL)
            EndDialog(dialog, 1);
        break;
    default:
        break;
    }
    return FALSE;
}

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

// The crash report: the game hidden, the player asked (the text shown); yes: the text sent to the developers.
void ReportCrash(const char *text)                       // 0x00731ee0
{
    if (GetIGraph()) {
        GetIGraph()->UnknownDC(0);
        ShowWindow(GetIGraph()->GetHWND(), SW_HIDE);
    }
    if (DialogBoxParamA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(CRASH_DIALOG), nullptr, CrashReportProc,
                        reinterpret_cast<LPARAM>(text)) != 0)
        return;
    SocketStartUp();
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    std::memcpy(&address.sin_addr, REPORT_ADDRESS, sizeof REPORT_ADDRESS);
    address.sin_port = htons(REPORT_PORT);
    if (connect(s, reinterpret_cast<const sockaddr *>(&address), sizeof address) == SOCKET_ERROR)
        return;
    send(s, text, static_cast<int>(std::strlen(text) + 1), 0);
    closesocket(s);
    WSACleanup();
}
