// The server's sound driver: LS3DRF.dll has no ISND, so ISndGetDriver() is not an import. The client's code calls it to ask
// whether there is a sound device (IsInitialized), and the answer here is always no; the server's code does not call it.
// The symbol is defined as the import's pointer (__imp__ISndGetDriver), since igraph.h declares the function dllimport.
#include "ls3d/igraph.h"

namespace {

class C_NullSndDriver : public ISND_driver {
public:
    void __stdcall Slot00() override {}
    HRESULT __stdcall Init(HWND, int, int, int, uint32_t) override { return E_FAIL; }
    void __stdcall Close() override {}
    bool __stdcall IsInitialized() override { return false; }
    void __stdcall Slot10() override {}
    void __stdcall SetVolume(float) override {}
    void __stdcall Slot18() override {}
    void __stdcall Slot1C() override {}
    ISND_sound *__stdcall CreateSound() override { return nullptr; }
    void __stdcall Slot24() override {}
    ISND_source *__stdcall CreateSource() override { return nullptr; }
};

C_NullSndDriver g_nullSndDriver;

ISND_driver *NullSndGetDriver()
{
    return &g_nullSndDriver;
}

}  // namespace

extern "C" ISND_driver *(*_imp__ISndGetDriver)() = NullSndGetDriver;
