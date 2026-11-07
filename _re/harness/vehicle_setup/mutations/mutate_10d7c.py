"""Part 10d-vii mutations: the loading progress, the CD key (net/net_console.cpp).

Run with the net harness (_re/harness/net: build_firewall.bat, firewall_test.exe; the scratchpad's mutnetf.py).
Generated (deletions, comparison, number, bool and negation flips; the lines unique in the file).

Result (2026-11-05): 56 mutations: 16 killed, 11 not building, 29 survived: ReportCrash and its dialog procedure were
not exercised (never called; part 10e-i put them back in tier 1 as dead code, so those patterns no longer match);
GetCDKey's RegCloseKey and its -1 sentinel are not observable from the harness.
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
T = os.path.join(ROOT, r"src/net/net_console.cpp")

MUTATIONS = [
    (T, 'T 33 num', 'constexpr uint8_t REPORT_ADDRESS[4] = {194, 213, 63, 62};\n', 'constexpr uint8_t REPORT_ADDRESS[5] = {194, 213, 63, 62};\n'),
    (T, 'T 34 num', 'constexpr u_short REPORT_PORT = 15002;\n', 'constexpr u_short REPORT_PORT = 15003;\n'),
    (T, 'T 40 del', '        EndDialog(dialog, 1);\n        break;\n    case WM_INITDIALOG:\n', '\n        break;\n    case WM_INITDIALOG:\n'),
    (T, 'T 40 num', '        EndDialog(dialog, 1);\n        break;\n    case WM_INITDIALOG:\n', '        EndDialog(dialog, 2);\n        break;\n    case WM_INITDIALOG:\n'),
    (T, 'T 43 del', '        SetDlgItemTextA(dialog, CRASH_QUESTION, g_texts.Get(TEXT_CRASH_REPORT).c_str());\n', '\n'),
    (T, 'T 44 del', '        SetDlgItemTextA(dialog, CRASH_TEXT, reinterpret_cast<const char *>(lParam));\n', '\n'),
    (T, 'T 47 cmp', '        if (LOWORD(wParam) == IDOK)\n', '        if (LOWORD(wParam) != IDOK)\n'),
    (T, 'T 48 del', '            EndDialog(dialog, 0);\n', '\n'),
    (T, 'T 48 num', '            EndDialog(dialog, 0);\n', '            EndDialog(dialog, 1);\n'),
    (T, 'T 49 cmp', '        else if (LOWORD(wParam) == IDCANCEL)\n', '        else if (LOWORD(wParam) != IDCANCEL)\n'),
    (T, 'T 50 del', '            EndDialog(dialog, 1);\n', '\n'),
    (T, 'T 50 num', '            EndDialog(dialog, 1);\n', '            EndDialog(dialog, 2);\n'),
    (T, 'T 66 cmp', '    if (percent % 4 == 0 && g_loadingMap)\n', '    if (percent % 4 != 0 && g_loadingMap)\n'),
    (T, 'T 66 num', '    if (percent % 4 == 0 && g_loadingMap)\n', '    if (percent % 5 == 0 && g_loadingMap)\n'),
    (T, 'T 67 del', "        GetConsole()->OnChar('.');\n", '\n'),
    (T, 'T 68 cmp', '    if (percent == 100) {\n', '    if (percent != 100) {\n'),
    (T, 'T 68 num', '    if (percent == 100) {\n', '    if (percent == 101) {\n'),
    (T, 'T 69 not', '        if (!g_loadingMap)\n', '        if (g_loadingMap)\n'),
    (T, 'T 71 del', '        GetConsole()->MoveInputLineToHistory();\n', '\n'),
    (T, 'T 72 del', '        const C_str text = g_texts.Get(TEXT_MAP_LOADED);\n', '\n'),
    (T, 'T 73 del', '        GetConsole()->AddLine(C_fstr("%s", text.c_str()).c_str());\n', '\n'),
    (T, 'T 74 del', '        g_loadingMap = false;\n', '\n'),
    (T, 'T 75 cmp', '    } else if (percent == 0 && !g_loadingMap) {\n', '    } else if (percent != 0 && !g_loadingMap) {\n'),
    (T, 'T 75 num', '    } else if (percent == 0 && !g_loadingMap) {\n', '    } else if (percent == 1 && !g_loadingMap) {\n'),
    (T, 'T 76 del', '        const C_str text = g_texts.Get(TEXT_LOADING_MAP);\n', '\n'),
    (T, 'T 77 del', '        C_str line = C_fstr("%s  ", text.c_str());\n', '\n'),
    (T, 'T 78 del', '        line += g_gameProfile.m_mpSettings->GetCurrentMap()->GetName();\n', '\n'),
    (T, 'T 79 del', '        GetConsole()->AddLine(line.c_str());\n', '\n'),
    (T, 'T 80 del', '        g_loadingMap = true;\n', '\n'),
    (T, 'T 86 del', '    HKEY key;\n', '\n'),
    (T, 'T 87 num', '    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\\\Illusion Softworks\\\\Hidden & Dangerous 2", 0, KEY_READ, &key) !=\n', '    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\\\Illusion Softworks\\\\Hidden & Dangerous 3", 0, KEY_READ, &key) !=\n'),
    (T, 'T 89 del', '        key = reinterpret_cast<HKEY>(-1);\n', '\n'),
    (T, 'T 89 num', '        key = reinterpret_cast<HKEY>(-1);\n', '        key = reinterpret_cast<HKEY>(-2);\n'),
    (T, 'T 90 del', '    DWORD size = sizeof g_cdKey;\n', '\n'),
    (T, 'T 91 del', '    DWORD type;\n', '\n'),
    (T, 'T 93 del', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS && size;\n', '\n'),
    (T, 'T 93 cmp', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS && size;\n', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) != ERROR_SUCCESS && size;\n'),
    (T, 'T 94 cmp', '    if (key != reinterpret_cast<HKEY>(-1))\n', '    if (key == reinterpret_cast<HKEY>(-1))\n'),
    (T, 'T 94 num', '    if (key != reinterpret_cast<HKEY>(-1))\n', '    if (key != reinterpret_cast<HKEY>(-2))\n'),
    (T, 'T 95 del', '        RegCloseKey(key);\n', '\n'),
    (T, 'T 103 del', '        GetIGraph()->UnknownDC(0);\n', '\n'),
    (T, 'T 103 num', '        GetIGraph()->UnknownDC(0);\n', '        GetIGraph()->UnknownDC(1);\n'),
    (T, 'T 104 del', '        ShowWindow(GetIGraph()->GetHWND(), SW_HIDE);\n', '\n'),
    (T, 'T 107 cmp', '                        reinterpret_cast<LPARAM>(text)) != 0)\n', '                        reinterpret_cast<LPARAM>(text)) == 0)\n'),
    (T, 'T 107 num', '                        reinterpret_cast<LPARAM>(text)) != 0)\n', '                        reinterpret_cast<LPARAM>(text)) != 1)\n'),
    (T, 'T 109 del', '    SocketStartUp();\n', '\n'),
    (T, 'T 110 del', '    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);\n', '\n'),
    (T, 'T 111 del', '    sockaddr_in address = {};\n', '\n'),
    (T, 'T 112 del', '    address.sin_family = AF_INET;\n', '\n'),
    (T, 'T 113 del', '    std::memcpy(&address.sin_addr, REPORT_ADDRESS, sizeof REPORT_ADDRESS);\n', '\n'),
    (T, 'T 114 del', '    address.sin_port = htons(REPORT_PORT);\n', '\n'),
    (T, 'T 115 cmp', '    if (connect(s, reinterpret_cast<const sockaddr *>(&address), sizeof address) == SOCKET_ERROR)\n', '    if (connect(s, reinterpret_cast<const sockaddr *>(&address), sizeof address) != SOCKET_ERROR)\n'),
    (T, 'T 117 del', '    send(s, text, static_cast<int>(std::strlen(text) + 1), 0);\n', '\n'),
    (T, 'T 117 num', '    send(s, text, static_cast<int>(std::strlen(text) + 1), 0);\n', '    send(s, text, static_cast<int>(std::strlen(text) + 2), 0);\n'),
    (T, 'T 118 del', '    closesocket(s);\n', '\n'),
    (T, 'T 119 del', '    WSACleanup();\n', '\n'),
]
