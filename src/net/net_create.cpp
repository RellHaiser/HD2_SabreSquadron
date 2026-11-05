// The network game's COM objects made (part 10d-iv; a file of its own: the net harness supplies its fakes).
#include "net/dplay8.h"

#include <objbase.h>

HRESULT NetCreateInstance(const GUID &clsid, DWORD context, const GUID &iid, void **object)
{
    return CoCreateInstance(clsid, nullptr, context, iid, object);
}
