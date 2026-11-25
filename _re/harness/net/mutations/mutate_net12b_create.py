"""Net part 12b: net/net_create.cpp (NetCreateInstance, NetFirewallAvailable) through the firewall's test (run as
mutate_net12b's without MUT_WRAPPER: the file is compiled on its own). 10 mutants, 2 not building, all 8 building
killed."""
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..\\..\\..\\..'))
T = os.path.join(ROOT, 'src/net/net_create.cpp')

MUTATIONS = [
    (T, 'G 3 text', '#include "net/dplay8.h"\r\n', '#include "net/dplay8.hx"\r\n'),
    (T, 'G 19 del', '    OSVERSIONINFOA &version = reinterpret_cast<OSVERSIONINFOA &>(buffer);\r\n', '    ;\r\n'),
    (T, 'G 20 del', '    version.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);        // (0x9C first)\r\n', '    ;\r\n'),
    (T, 'G 21 neg', '    if (!GetVersionExA(&version)) {\r\n', '    if (GetVersionExA(&version)) {\r\n'),
    (T, 'G 22 del', '        version.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);\r\n', '        ;\r\n'),
    (T, 'G 23 neg', '        if (!GetVersionExA(&version))\r\n', '        if (GetVersionExA(&version))\r\n'),
    (T, 'G 24 bool', '            return false;\r\n', '            return true;\r\n'),
    (T, 'G 26 cmp', '    return version.dwPlatformId == VER_PLATFORM_WIN32_NT && (version.dwMajorVersion == 5 || version.dwMinorVersion == 1);\r\n', '    return version.dwPlatformId != VER_PLATFORM_WIN32_NT && (version.dwMajorVersion == 5 || version.dwMinorVersion == 1);\r\n'),
    (T, 'G 26 num', '    return version.dwPlatformId == VER_PLATFORM_WIN32_NT && (version.dwMajorVersion == 5 || version.dwMinorVersion == 1);\r\n', '    return version.dwPlatformId == VER_PLATFORM_WIN32_NT && (version.dwMajorVersion == 6 || version.dwMinorVersion == 1);\r\n'),
    (T, 'G 26 and', '    return version.dwPlatformId == VER_PLATFORM_WIN32_NT && (version.dwMajorVersion == 5 || version.dwMinorVersion == 1);\r\n', '    return version.dwPlatformId == VER_PLATFORM_WIN32_NT || (version.dwMajorVersion == 5 || version.dwMinorVersion == 1);\r\n'),
]
