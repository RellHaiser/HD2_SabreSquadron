// The SDL3 backend of the input device (input/input.h): the keyboard and mouse of the SDL3
// platform's window (platform/sdl3), in DirectInput's terms. The platform's message loop feeds SDL
// (IGraph pumps it every frame and while it waits to acquire a device).
//
// - Acquiring needs the window to have the keyboard focus, as DirectInput's foreground cooperative
//   level does: "another application has priority" otherwise. A device whose window lost the
//   focus reports "input lost" once and is unacquired.
// - The keyboard is SDL's key state with each scancode as its DirectInput key code (DIK_*; the
//   game's key bindings store those).
// - The mouse is SDL's relative motion since the last read (raw, as DirectInput's), its wheel in
//   DirectInput's units (120 a notch) and its buttons (left, right, middle, X1, X2). An exclusive
//   mouse is SDL's relative mode: the cursor hidden and kept in the window.
// - Key names are SDL's for the key on the current layout.
#include "input/input.h"

#include <cmath>
#include <cstring>

#include "input/backends.h"
#include "platform/sdl3/sdl3_api.h"

namespace ls3d::input {

namespace {

using sdl3::api;

struct S_peripheral {
    bool mouse;
    DWORD level = 0;                        // DISCL_*
    bool acquired = false;
    float restX = 0, restY = 0, restWheel = 0;  // motion under a unit, kept for the next read
};

S_peripheral *P(Peripheral *p) { return reinterpret_cast<S_peripheral *>(p); }

bool Focused() { return sdl3::g_window && api.SDL_GetKeyboardFocus() == sdl3::g_window; }

// An exclusive mouse is SDL's relative mode while acquired.
void Relative(const S_peripheral *p, bool on)
{
    if (p->mouse && (p->level & DISCL_EXCLUSIVE) && sdl3::g_window)
        api.SDL_SetWindowRelativeMouseMode(sdl3::g_window, on);
}

// Whole units of `value` (added to `rest`); the fraction stays in `rest`.
LONG Take(float &rest, float value)
{
    rest += value;
    const float whole = std::trunc(rest);
    rest -= whole;
    return static_cast<LONG>(whole);
}

int s_system;                               // (the input system has no state)

class DeviceSdl3 final : public Device {
public:
    // ---- the input system
    HRESULT Create(HINSTANCE, System **system) override
    {
        *system = reinterpret_cast<System *>(&s_system);
        return DI_OK;
    }
    ULONG Release(System *) override { return 0; }
    HRESULT CreateDevice(System *, REFGUID guid, Peripheral **peripheral) override
    {
        *peripheral = nullptr;
        if (IsEqualGUID(guid, GUID_SysKeyboard))
            *peripheral = reinterpret_cast<Peripheral *>(new S_peripheral{false});
        else if (IsEqualGUID(guid, GUID_SysMouse))
            *peripheral = reinterpret_cast<Peripheral *>(new S_peripheral{true});
        else
            return DIERR_DEVICENOTREG;
        return DI_OK;
    }

    // ---- keyboards and mice
    ULONG Release(Peripheral *peripheral) override
    {
        Unacquire(peripheral);
        delete P(peripheral);
        return 0;
    }
    HRESULT SetDataFormat(Peripheral *, const DIDATAFORMAT *) override { return DI_OK; }
    HRESULT SetCooperativeLevel(Peripheral *peripheral, HWND, DWORD flags) override
    {
        P(peripheral)->level = flags;
        return DI_OK;
    }
    HRESULT Acquire(Peripheral *peripheral) override
    {
        S_peripheral *p = P(peripheral);
        if (p->acquired)
            return S_FALSE;
        if (!Focused())
            return DIERR_OTHERAPPHASPRIO;
        p->acquired = true;
        Relative(p, true);
        if (p->mouse) {
            // The motion and wheel from before do not count.
            float x, y;
            api.SDL_GetRelativeMouseState(&x, &y);
            sdl3::g_wheel = 0;
            p->restX = p->restY = p->restWheel = 0;
        }
        return DI_OK;
    }
    HRESULT Unacquire(Peripheral *peripheral) override
    {
        S_peripheral *p = P(peripheral);
        if (!p->acquired)
            return DI_NOEFFECT;
        p->acquired = false;
        Relative(p, false);
        return DI_OK;
    }
    HRESULT GetDeviceState(Peripheral *peripheral, DWORD size, void *state) override
    {
        S_peripheral *p = P(peripheral);
        if (!p->acquired)
            return DIERR_NOTACQUIRED;
        if (!Focused()) {
            p->acquired = false;
            Relative(p, false);
            return DIERR_INPUTLOST;
        }
        if (!p->mouse) {
            if (size != 256)
                return DIERR_INVALIDPARAM;
            auto *keys = static_cast<BYTE *>(state);
            std::memset(keys, 0, 256);
            int count = 0;
            const bool *down = api.SDL_GetKeyboardState(&count);
            for (int sc = 0; sc < count; ++sc) {
                if (down[sc]) {
                    if (const uint8_t dik = sdl3::DikOfScancode(static_cast<SDL_Scancode>(sc)))
                        keys[dik] = 0x80;
                }
            }
            return DI_OK;
        }
        if (size < sizeof(DIMOUSESTATE))
            return DIERR_INVALIDPARAM;
        std::memset(state, 0, size);
        auto *m = static_cast<DIMOUSESTATE2 *>(state);
        float x = 0, y = 0;
        const SDL_MouseButtonFlags buttons = api.SDL_GetRelativeMouseState(&x, &y);
        m->lX = Take(p->restX, x);
        m->lY = Take(p->restY, y);
        m->lZ = Take(p->restWheel, sdl3::g_wheel * 120.0f);
        sdl3::g_wheel = 0;
        const SDL_MouseButtonFlags masks[] = {SDL_BUTTON_LMASK, SDL_BUTTON_RMASK, SDL_BUTTON_MMASK, SDL_BUTTON_X1MASK,
                                              SDL_BUTTON_X2MASK};
        const DWORD count = size >= sizeof(DIMOUSESTATE2) ? 5 : 4;
        for (DWORD i = 0; i < count; ++i)
            m->rgbButtons[i] = (buttons & masks[i]) ? 0x80 : 0;
        return DI_OK;
    }
    HRESULT GetCapabilities(Peripheral *peripheral, DIDEVCAPS *caps) override
    {
        const DWORD size = caps->dwSize;
        std::memset(caps, 0, size < sizeof *caps ? size : sizeof *caps);
        caps->dwSize = size;
        caps->dwFlags = DIDC_ATTACHED;
        if (P(peripheral)->mouse) {
            caps->dwDevType = DI8DEVTYPE_MOUSE | (DI8DEVTYPEMOUSE_UNKNOWN << 8);
            caps->dwAxes = 3;
            caps->dwButtons = 5;
        } else {
            caps->dwDevType = DI8DEVTYPE_KEYBOARD | (DI8DEVTYPEKEYBOARD_PCENH << 8);
            caps->dwButtons = 128;
        }
        return DI_OK;
    }
    HRESULT GetProperty(Peripheral *peripheral, REFGUID property, DIPROPHEADER *header) override
    {
        if (&property != &DIPROP_KEYNAME || P(peripheral)->mouse || header->dwHow != DIPH_BYOFFSET ||
            header->dwSize < sizeof(DIPROPSTRING))
            return DIERR_UNSUPPORTED;
        const SDL_Scancode sc = sdl3::ScancodeOfDik(static_cast<uint8_t>(header->dwObj));
        if (sc == SDL_SCANCODE_UNKNOWN || header->dwObj > 0xff)
            return DIERR_NOTFOUND;
        const char *name = api.SDL_GetKeyName(api.SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false));
        if (!name || !*name)
            return DIERR_NOTFOUND;
        auto *text = reinterpret_cast<DIPROPSTRING *>(header);
#ifdef _WIN32
        if (!MultiByteToWideChar(CP_UTF8, 0, name, -1, text->wsz, MAX_PATH))
            return DIERR_NOTFOUND;
#else
        size_t i = 0;
        for (; name[i] && i + 1 < MAX_PATH; ++i)
            text->wsz[i] = static_cast<unsigned char>(name[i]);
        text->wsz[i] = 0;
#endif
        return DI_OK;
    }
};

DeviceSdl3 s_device;

}  // namespace

Device *Sdl3InputDevice() { return &s_device; }

}  // namespace ls3d::input
