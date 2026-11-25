"""Net part 14: the network's odds and ends (net/net_console.cpp and net/net_com.cpp) against the original's
(net/misc_diff.cpp, suite net.misc). net_console.cpp's mutants (T): run with HD2_MISC_QUICK=1 on build_misc.bat with
MUT_WRAPPER=misc_diff.cpp (the test includes the file), linked with -Wl,--image-base=0x20000000. net_com.cpp's ("com"
labels, C): the same without MUT_WRAPPER (in wrapper mode the runner rebuilds the wrapper only, so a linked file's
mutants all survived the first run); besides the generator's three, each GUID's last byte changed and two codec rows
swapped.

68 mutants, 12 not building (headers renamed, the globals and the array's size or last row removed); all 56 building
killed (net_console.cpp 30, net_com.cpp 26)."""
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..\\..\\..\\..'))
T = os.path.join(ROOT, 'src/net/net_console.cpp')
C = os.path.join(ROOT, 'src/net/net_com.cpp')

MUTATIONS = [
    (T, 'G 6 text', '#include "net/net_game.h"\r\n', '#include "net/net_game.hx"\r\n'),
    (T, 'G 8 text', '#include "game/console.h"\r\n', '#include "game/console.hx"\r\n'),
    (T, 'G 9 text', '#include "game/game_texts.h"\r\n', '#include "game/game_texts.hx"\r\n'),
    (T, 'G 10 text', '#include "game/pending.h"\r\n', '#include "game/pending.hx"\r\n'),
    (T, 'G 19 del', 'bool g_loadingMap;\r\n', ';\r\n'),
    (T, 'G 21 del', 'char g_cdKey[0x40];\r\n', ';\r\n'),
    (T, 'G 31 cmp', '    if (percent % 4 == 0 && g_loadingMap)\r\n', '    if (percent % 4 != 0 && g_loadingMap)\r\n'),
    (T, 'G 31 num', '    if (percent % 4 == 0 && g_loadingMap)\r\n', '    if (percent % 5 == 0 && g_loadingMap)\r\n'),
    (T, 'G 31 and', '    if (percent % 4 == 0 && g_loadingMap)\r\n', '    if (percent % 4 == 0 || g_loadingMap)\r\n'),
    (T, 'G 32 del', "        GetConsole()->OnChar('.');\r\n", '        ;\r\n'),
    (T, 'G 33 cmp', '    if (percent == 100) {\r\n', '    if (percent != 100) {\r\n'),
    (T, 'G 33 num', '    if (percent == 100) {\r\n', '    if (percent == 101) {\r\n'),
    (T, 'G 34 neg', '        if (!g_loadingMap)\r\n', '        if (g_loadingMap)\r\n'),
    (T, 'G 36 del', '        GetConsole()->MoveInputLineToHistory();\r\n', '        ;\r\n'),
    (T, 'G 38 del', '        GetConsole()->AddLine(C_fstr("%s", text.c_str()).c_str());\r\n', '        ;\r\n'),
    (T, 'G 38 text', '        GetConsole()->AddLine(C_fstr("%s", text.c_str()).c_str());\r\n', '        GetConsole()->AddLine(C_fstr("%sx", text.c_str()).c_str());\r\n'),
    (T, 'G 39 del', '        g_loadingMap = false;\r\n', '        ;\r\n'),
    (T, 'G 39 bool', '        g_loadingMap = false;\r\n', '        g_loadingMap = true;\r\n'),
    (T, 'G 40 cmp', '    } else if (percent == 0 && !g_loadingMap) {\r\n', '    } else if (percent != 0 && !g_loadingMap) {\r\n'),
    (T, 'G 40 num', '    } else if (percent == 0 && !g_loadingMap) {\r\n', '    } else if (percent == 1 && !g_loadingMap) {\r\n'),
    (T, 'G 40 and', '    } else if (percent == 0 && !g_loadingMap) {\r\n', '    } else if (percent == 0 || !g_loadingMap) {\r\n'),
    (T, 'G 42 text', '        C_str line = C_fstr("%s  ", text.c_str());\r\n', '        C_str line = C_fstr("%s  x", text.c_str());\r\n'),
    (T, 'G 43 del', '        line += g_gameProfile.m_mpSettings->GetCurrentMap()->GetName();\r\n', '        ;\r\n'),
    (T, 'G 44 del', '        GetConsole()->AddLine(line.c_str());\r\n', '        ;\r\n'),
    (T, 'G 45 del', '        g_loadingMap = true;\r\n', '        ;\r\n'),
    (T, 'G 45 bool', '        g_loadingMap = true;\r\n', '        g_loadingMap = false;\r\n'),
    (T, 'G 51 del', '    HKEY key;\r\n', '    ;\r\n'),
    (T, 'G 52 num', '    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\\\Illusion Softworks\\\\Hidden & Dangerous 2", 0, KEY_READ, &key) !=\r\n', '    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\\\Illusion Softworks\\\\Hidden & Dangerous 3", 0, KEY_READ, &key) !=\r\n'),
    (T, 'G 52 text', '    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\\\Illusion Softworks\\\\Hidden & Dangerous 2", 0, KEY_READ, &key) !=\r\n', '    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\\\Illusion Softworks\\\\Hidden & Dangerous 2x", 0, KEY_READ, &key) !=\r\n'),
    (T, 'G 54 del', '        key = reinterpret_cast<HKEY>(-1);\r\n', '        ;\r\n'),
    (T, 'G 54 num', '        key = reinterpret_cast<HKEY>(-1);\r\n', '        key = reinterpret_cast<HKEY>(-2);\r\n'),
    (T, 'G 56 del', '    DWORD type;\r\n', '    ;\r\n'),
    (T, 'G 58 del', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS && size;\r\n', '        ;\r\n'),
    (T, 'G 58 cmp', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS && size;\r\n', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) != ERROR_SUCCESS && size;\r\n'),
    (T, 'G 58 and', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS && size;\r\n', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS || size;\r\n'),
    (T, 'G 58 text', '        RegQueryValueExA(key, "key", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS && size;\r\n', '        RegQueryValueExA(key, "keyx", nullptr, &type, reinterpret_cast<BYTE *>(g_cdKey), &size) == ERROR_SUCCESS && size;\r\n'),
    (T, 'G 59 cmp', '    if (key != reinterpret_cast<HKEY>(-1))\r\n', '    if (key == reinterpret_cast<HKEY>(-1))\r\n'),
    (T, 'G 59 num', '    if (key != reinterpret_cast<HKEY>(-1))\r\n', '    if (key != reinterpret_cast<HKEY>(-2))\r\n'),
    (T, 'G 60 del', '        RegCloseKey(key);\r\n', '        ;\r\n'),
    (C, 'com G 3 text', '#include "net/dplay8.h"\r\n', '#include "net/dplay8.hx"\r\n'),
    (C, 'com G 21 num', 'const GUID g_netVoiceCodecs[8] = {\r\n', 'const GUID g_netVoiceCodecs[9] = {\r\n'),
    (C, 'com G 30 del', '    {0x8DE12FD4, 0x7CB3, 0x48CE, {0xA7, 0xE8, 0x9C, 0x47, 0xA2, 0x2E, 0x8A, 0xC5}},   // 7 (0x00819138)\r\n};\r\n', '    {0x8DE12FD4, 0x7CB3, 0x48CE, {0xA7, 0xE8, 0x9C, 0x47, 0xA2, 0x2E, 0x8A, 0xC5}},   // 7 (0x00819138)\r\n;\r\n'),
    (C, 'com G 5 num', 'const GUID CLSID_NetDirectPlay8Server = {0xDA825E1B, 0x6830, 0x43D7, {0x83, 0x5D, 0x0B, 0x5A, 0xD8, 0x29, 0x56, 0xA2}};\r\n', 'const GUID CLSID_NetDirectPlay8Server = {0xDA825E1B, 0x6830, 0x43D7, {0x83, 0x5D, 0x0B, 0x5A, 0xD8, 0x29, 0x56, 0xA3}};\r\n'),
    (C, 'com G 6 num', 'const GUID IID_NetDirectPlay8Server = {0x5102DACE, 0x241B, 0x11D3, {0xAE, 0xA7, 0x00, 0x60, 0x97, 0xB0, 0x14, 0x11}};\r\n', 'const GUID IID_NetDirectPlay8Server = {0x5102DACE, 0x241B, 0x11D3, {0xAE, 0xA7, 0x00, 0x60, 0x97, 0xB0, 0x14, 0x10}};\r\n'),
    (C, 'com G 7 num', 'const GUID CLSID_NetDirectPlay8Client = {0x743F1DC6, 0x5ABA, 0x429F, {0x8B, 0xDF, 0xC5, 0x4D, 0x03, 0x25, 0x3D, 0xC2}};\r\n', 'const GUID CLSID_NetDirectPlay8Client = {0x743F1DC6, 0x5ABA, 0x429F, {0x8B, 0xDF, 0xC5, 0x4D, 0x03, 0x25, 0x3D, 0xC3}};\r\n'),
    (C, 'com G 8 num', 'const GUID IID_NetDirectPlay8Client = {0x5102DACD, 0x241B, 0x11D3, {0xAE, 0xA7, 0x00, 0x60, 0x97, 0xB0, 0x14, 0x11}};\r\n', 'const GUID IID_NetDirectPlay8Client = {0x5102DACD, 0x241B, 0x11D3, {0xAE, 0xA7, 0x00, 0x60, 0x97, 0xB0, 0x14, 0x10}};\r\n'),
    (C, 'com G 9 num', 'const GUID CLSID_NetDirectPlay8Address = {0x934A9523, 0xA3CA, 0x4BC5, {0xAD, 0xA0, 0xD6, 0xD9, 0x5D, 0x97, 0x94, 0x21}};\r\n', 'const GUID CLSID_NetDirectPlay8Address = {0x934A9523, 0xA3CA, 0x4BC5, {0xAD, 0xA0, 0xD6, 0xD9, 0x5D, 0x97, 0x94, 0x20}};\r\n'),
    (C, 'com G 10 num', 'const GUID IID_NetDirectPlay8Address = {0x83783300, 0x4063, 0x4C8A, {0x9D, 0xB3, 0x82, 0x83, 0x0A, 0x7F, 0xEB, 0x31}};\r\n', 'const GUID IID_NetDirectPlay8Address = {0x83783300, 0x4063, 0x4C8A, {0x9D, 0xB3, 0x82, 0x83, 0x0A, 0x7F, 0xEB, 0x30}};\r\n'),
    (C, 'com G 11 num', 'const GUID CLSID_NetDP8SP_TCPIP = {0xEBFE7BA0, 0x628D, 0x11D2, {0xAE, 0x0F, 0x00, 0x60, 0x97, 0xB0, 0x14, 0x11}};\r\n', 'const GUID CLSID_NetDP8SP_TCPIP = {0xEBFE7BA0, 0x628D, 0x11D2, {0xAE, 0x0F, 0x00, 0x60, 0x97, 0xB0, 0x14, 0x10}};\r\n'),
    (C, 'com G 12 num', 'const GUID g_netApplicationGuid = {0x2AE5B082, 0xCFA0, 0x11D4, {0x9E, 0x1D, 0x00, 0x50, 0xFC, 0x21, 0x3D, 0xE6}};\r\n', 'const GUID g_netApplicationGuid = {0x2AE5B082, 0xCFA0, 0x11D4, {0x9E, 0x1D, 0x00, 0x50, 0xFC, 0x21, 0x3D, 0xE7}};\r\n'),
    (C, 'com G 13 num', 'const GUID CLSID_NetVoiceClient = {0xB9F3EB85, 0xB781, 0x4AC1, {0x8D, 0x90, 0x93, 0xA0, 0x5E, 0xE3, 0x7D, 0x7D}};\r\n', 'const GUID CLSID_NetVoiceClient = {0xB9F3EB85, 0xB781, 0x4AC1, {0x8D, 0x90, 0x93, 0xA0, 0x5E, 0xE3, 0x7D, 0x7C}};\r\n'),
    (C, 'com G 14 num', 'const GUID IID_NetVoiceClient = {0x1DFDC8EA, 0xBCF7, 0x41D6, {0xB2, 0x95, 0xAB, 0x64, 0xB3, 0xB2, 0x33, 0x06}};\r\n', 'const GUID IID_NetVoiceClient = {0x1DFDC8EA, 0xBCF7, 0x41D6, {0xB2, 0x95, 0xAB, 0x64, 0xB3, 0xB2, 0x33, 0x07}};\r\n'),
    (C, 'com G 15 num', 'const GUID CLSID_NetVoiceServer = {0xD3F5B8E6, 0x9B78, 0x4A4C, {0x94, 0xEA, 0xCA, 0x23, 0x97, 0xB6, 0x63, 0xD3}};\r\n', 'const GUID CLSID_NetVoiceServer = {0xD3F5B8E6, 0x9B78, 0x4A4C, {0x94, 0xEA, 0xCA, 0x23, 0x97, 0xB6, 0x63, 0xD2}};\r\n'),
    (C, 'com G 16 num', 'const GUID IID_NetVoiceServer = {0xFAA1C173, 0x0468, 0x43B6, {0x8A, 0x2A, 0xEA, 0x8A, 0x4F, 0x20, 0x76, 0xC9}};\r\n', 'const GUID IID_NetVoiceServer = {0xFAA1C173, 0x0468, 0x43B6, {0x8A, 0x2A, 0xEA, 0x8A, 0x4F, 0x20, 0x76, 0xC8}};\r\n'),
    (C, 'com G 17 num', 'const GUID CLSID_NetVoiceTest = {0x0F0F094B, 0xB01C, 0x4091, {0xA1, 0x4D, 0xDD, 0x0C, 0xD8, 0x07, 0x71, 0x1A}};\r\n', 'const GUID CLSID_NetVoiceTest = {0x0F0F094B, 0xB01C, 0x4091, {0xA1, 0x4D, 0xDD, 0x0C, 0xD8, 0x07, 0x71, 0x1B}};\r\n'),
    (C, 'com G 18 num', 'const GUID IID_NetVoiceTest = {0xD26AF734, 0x208B, 0x41DA, {0x82, 0x24, 0xE0, 0xCE, 0x79, 0x81, 0x0B, 0xE1}};\r\n', 'const GUID IID_NetVoiceTest = {0xD26AF734, 0x208B, 0x41DA, {0x82, 0x24, 0xE0, 0xCE, 0x79, 0x81, 0x0B, 0xE0}};\r\n'),
    (C, 'com G 19 num', 'const GUID g_netVoicePlayback = {0xDEF00002, 0x9C6D, 0x47ED, {0xAA, 0xF1, 0x4D, 0xDA, 0x8F, 0x2B, 0x5C, 0x03}};\r\n', 'const GUID g_netVoicePlayback = {0xDEF00002, 0x9C6D, 0x47ED, {0xAA, 0xF1, 0x4D, 0xDA, 0x8F, 0x2B, 0x5C, 0x02}};\r\n'),
    (C, 'com G 20 num', 'const GUID g_netVoiceCapture = {0xDEF00003, 0x9C6D, 0x47ED, {0xAA, 0xF1, 0x4D, 0xDA, 0x8F, 0x2B, 0x5C, 0x03}};\r\n', 'const GUID g_netVoiceCapture = {0xDEF00003, 0x9C6D, 0x47ED, {0xAA, 0xF1, 0x4D, 0xDA, 0x8F, 0x2B, 0x5C, 0x02}};\r\n'),
    (C, 'com G 22 num', "    {0xD7954361, 0x5A0B, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // (others: the default's)\r\n", "    {0xD7954361, 0x5A0B, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE6}},   // (others: the default's)\r\n"),
    (C, 'com G 23 num', '    {0xFE44A9FE, 0x8ED4, 0x48BF, {0x9D, 0x66, 0x1B, 0x1A, 0xDF, 0xF9, 0xFF, 0x6D}},   // 1 (0x008190f8)\r\n', '    {0xFE44A9FE, 0x8ED4, 0x48BF, {0x9D, 0x66, 0x1B, 0x1A, 0xDF, 0xF9, 0xFF, 0x6C}},   // 1 (0x008190f8)\r\n'),
    (C, 'com G 24 num', '    {0x7D82A29B, 0x2242, 0x4F82, {0x8F, 0x39, 0x5D, 0x11, 0x53, 0xDF, 0x3E, 0x41}},   // 2 (0x00819128)\r\n', '    {0x7D82A29B, 0x2242, 0x4F82, {0x8F, 0x39, 0x5D, 0x11, 0x53, 0xDF, 0x3E, 0x40}},   // 2 (0x00819128)\r\n'),
    (C, 'com G 25 num', '    {0x53DEF900, 0x7168, 0x4633, {0xB4, 0x7F, 0xD1, 0x43, 0x91, 0x6A, 0x13, 0xC7}},   // 3 (0x00819118)\r\n', '    {0x53DEF900, 0x7168, 0x4633, {0xB4, 0x7F, 0xD1, 0x43, 0x91, 0x6A, 0x13, 0xC6}},   // 3 (0x00819118)\r\n'),
    (C, 'com G 26 num', '    {0xD7954361, 0x5A0B, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // 4 (0x00819108, the default)\r\n', '    {0xD7954361, 0x5A0B, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE6}},   // 4 (0x00819108, the default)\r\n'),
    (C, 'com G 27 num', '    {0x24768C60, 0x5A0D, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // 5 (0x00819148)\r\n', '    {0x24768C60, 0x5A0D, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE6}},   // 5 (0x00819148)\r\n'),
    (C, 'com G 28 num', '    {0x699B52C1, 0xA885, 0x46A8, {0xA3, 0x08, 0x97, 0x17, 0x24, 0x19, 0xAD, 0xC7}},   // 6 (0x00819158)\r\n', '    {0x699B52C1, 0xA885, 0x46A8, {0xA3, 0x08, 0x97, 0x17, 0x24, 0x19, 0xAD, 0xC6}},   // 6 (0x00819158)\r\n'),
    (C, 'com G 29 num', '    {0x8DE12FD4, 0x7CB3, 0x48CE, {0xA7, 0xE8, 0x9C, 0x47, 0xA2, 0x2E, 0x8A, 0xC5}},   // 7 (0x00819138)\r\n', '    {0x8DE12FD4, 0x7CB3, 0x48CE, {0xA7, 0xE8, 0x9C, 0x47, 0xA2, 0x2E, 0x8A, 0xC4}},   // 7 (0x00819138)\r\n'),
    (C, 'com G 22 swap', "    {0xD7954361, 0x5A0B, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // (others: the default's)\r\n    {0xFE44A9FE, 0x8ED4, 0x48BF, {0x9D, 0x66, 0x1B, 0x1A, 0xDF, 0xF9, 0xFF, 0x6D}},   // 1 (0x008190f8)\r\n", "    {0xFE44A9FE, 0x8ED4, 0x48BF, {0x9D, 0x66, 0x1B, 0x1A, 0xDF, 0xF9, 0xFF, 0x6D}},   // 1 (0x008190f8)\r\n    {0xD7954361, 0x5A0B, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // (others: the default's)\r\n"),
    (C, 'com G 26 swap', '    {0xD7954361, 0x5A0B, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // 4 (0x00819108, the default)\r\n    {0x24768C60, 0x5A0D, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // 5 (0x00819148)\r\n', '    {0x24768C60, 0x5A0D, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // 5 (0x00819148)\r\n    {0xD7954361, 0x5A0B, 0x11D3, {0x9B, 0xE4, 0x52, 0x54, 0x00, 0xD9, 0x85, 0xE7}},   // 4 (0x00819108, the default)\r\n'),
]
