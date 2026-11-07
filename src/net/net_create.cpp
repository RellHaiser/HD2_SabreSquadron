// The network game's COM objects made and the system asked (part 10d-iv / 10d-vii; a file of its own: the net
// harness supplies its fakes).
#include "net/dplay8.h"

#include <objbase.h>

HRESULT NetCreateInstance(const GUID &clsid, DWORD context, const GUID &iid, void **object)
{
    return CoCreateInstance(clsid, nullptr, context, iid, object);
}

// (GetVersionExA: deprecated, kept: the original's test.)
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif
bool NetFirewallAvailable()                             // (inline in ConfigureFirewall / FirewallBlocks)
{
    OSVERSIONINFOEXA buffer = {};                       // (Fix of the port: it was the smaller struct, and the system wrote past it.)
    OSVERSIONINFOA &version = reinterpret_cast<OSVERSIONINFOA &>(buffer);
    version.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);        // (0x9C first)
    if (!GetVersionExA(&version)) {
        version.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
        if (!GetVersionExA(&version))
            return false;
    }
    return version.dwPlatformId == VER_PLATFORM_WIN32_NT && (version.dwMajorVersion == 5 || version.dwMinorVersion == 1);
}
