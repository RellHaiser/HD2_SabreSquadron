// Differential test: loads the original IJoy.dll and the port side by side and drives both through
// identical random call sequences against a scripted fake DirectInput 7 (CreateJoyBase accepts an
// existing IDirectInput7A, so no joystick is needed). Every call into the fake and every result
// coming back is logged; the two logs must match line for line.
//
// Usage: compare.exe ORIGINAL_DLL PORT_DLL [seeds] [steps]
//
// The FPU runs in 24-bit precision like the game's (Direct3D sets it), where the original's x87
// float code rounds like the port's SSE floats. Known crash paths of the original (null object
// pointers, GetStatus of a file effect) are not exercised.
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <dinput.h>

#include <float.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ijoy.h"

namespace {

// ---------------------------------------------------------------------------------------- log

std::vector<std::string> *g_log;

void Log(const char *format, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    g_log->push_back(buf);
}

unsigned FloatBits(float f)
{
    unsigned u;
    std::memcpy(&u, &f, 4);
    return u;
}

struct Rng {
    uint32_t s;
    uint32_t Next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    int Int(int n) { return n <= 0 ? 0 : static_cast<int>(Next() % static_cast<uint32_t>(n)); }
    bool Chance(int percent) { return Int(100) < percent; }
};

// The scripted joysticks of a run, built from the seed before either DLL is driven. The fakes
// draw from their own generator (`script`), the driver from another, so both DLLs see the same.
struct DeviceConfig {
    DIDEVICEINSTANCEA instance;
    DIDEVCAPS caps;
    std::vector<DIDEVICEOBJECTINSTANCEA> objects;
    std::vector<DIEFFECTINFOA> effects;
    bool failCreate, failCoop, failFormat, rangeFails[8];
    LONG rangeMin[8], rangeMax[8];
};

std::vector<DeviceConfig> g_devices;
Rng g_script;

const GUID *const kEffectGuids[11] = {
    &GUID_ConstantForce, &GUID_RampForce, &GUID_Square, &GUID_Sine, &GUID_Triangle, &GUID_SawtoothUp,
    &GUID_SawtoothDown, &GUID_Spring, &GUID_Damper, &GUID_Inertia, &GUID_Friction,
};

int EffectIndex(REFGUID g)
{
    for (int i = 0; i < 11; ++i)
        if (g == *kEffectGuids[i])
            return i;
    return -1;
}

void BuildDevices(uint32_t seed)
{
    Rng r{seed * 2654435761u + 1};
    g_devices.clear();
    int n = r.Int(4);  // 0 = no joystick
    for (int d = 0; d < n; ++d) {
        DeviceConfig c = {};
        c.instance.dwSize = sizeof(c.instance);
        c.instance.guidInstance = {0x1000u + d, 1, 2, {3, 4, 5, 6, 7, 8, 9, static_cast<unsigned char>(d)}};
        c.instance.dwDevType = DIDEVTYPE_JOYSTICK | (r.Int(9) << 8);
        sprintf(c.instance.tszInstanceName, "Joystick %d", d);
        sprintf(c.instance.tszProductName, "Product %u", r.Next() % 1000);
        c.caps.dwSize = sizeof(c.caps);
        c.caps.dwFlags = DIDC_ATTACHED | (r.Chance(60) ? DIDC_FORCEFEEDBACK : 0);
        c.caps.dwAxes = 1 + r.Int(8);
        c.caps.dwButtons = r.Int(40);
        c.caps.dwPOVs = r.Int(5);
        static const GUID *const axisGuids[8] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis,
                                                 &GUID_RyAxis, &GUID_RzAxis, &GUID_Slider, &GUID_Slider};
        for (int a = 0; a < 8; ++a) {
            if (a > 0 && !r.Chance(60))
                continue;
            DIDEVICEOBJECTINSTANCEA o = {};
            o.dwSize = sizeof(o);
            o.guidType = *axisGuids[a];
            o.dwOfs = a * 4;
            o.dwFlags = (r.Chance(50) ? DIDOI_FFACTUATOR : 0) | (r.Int(5) << 8);
            o.dwFFMaxForce = r.Next() % 100000;
            o.dwFFForceResolution = r.Next() % 1000;
            sprintf(o.tszName, "Axis %d of %d", a, d);
            c.objects.push_back(o);
        }
        for (DWORD b = 0; b < c.caps.dwButtons + 2; ++b) {
            DIDEVICEOBJECTINSTANCEA o = {};
            o.dwSize = sizeof(o);
            o.guidType = GUID_Button;
            o.dwOfs = 0x30 + b;
            sprintf(o.tszName, "Button %u", b);
            if (r.Chance(90))
                c.objects.push_back(o);
        }
        for (int p = 0; p < 5; ++p) {
            DIDEVICEOBJECTINSTANCEA o = {};
            o.dwSize = sizeof(o);
            o.guidType = r.Chance(90) ? GUID_POV : GUID_Button;
            o.dwOfs = 0x20 + p * 4;
            sprintf(o.tszName, "Hat %d", p);
            if (r.Chance(70))
                c.objects.push_back(o);
        }
        if (c.caps.dwFlags & DIDC_FORCEFEEDBACK) {
            for (int e = 0; e < 11; ++e) {
                if (!r.Chance(75))
                    continue;
                DIEFFECTINFOA info = {};
                info.dwSize = sizeof(info);
                info.guid = *kEffectGuids[e];
                info.dwEffType = r.Next() & 0xFE00 | (e + 1);
                info.dwStaticParams = r.Next() & 0x1FF;
                info.dwDynamicParams = r.Next() & 0x1FF;
                sprintf(info.tszName, "Effect %d", e);
                c.effects.push_back(info);
            }
            DIEFFECTINFOA odd = {};
            odd.dwSize = sizeof(odd);
            odd.guid = GUID_CustomForce;
            c.effects.push_back(odd);
        }
        c.failCreate = r.Chance(5);
        c.failCoop = r.Chance(5);
        c.failFormat = r.Chance(5);
        for (int a = 0; a < 8; ++a) {
            c.rangeFails[a] = r.Chance(30);
            c.rangeMin[a] = r.Chance(50) ? 0 : -static_cast<LONG>(r.Next() % 40000);
            c.rangeMax[a] = static_cast<LONG>(r.Next() % 70000);
        }
        g_devices.push_back(c);
    }
}

HRESULT Result(int failPercent) { return g_script.Chance(failPercent) ? DIERR_INPUTLOST : DI_OK; }

// ---------------------------------------------------------------------------------- fake effect

void LogEffectParams(const char *what, const DIEFFECT *e, DWORD flags)
{
    Log("%s flags=%08x size=%u dwFlags=%x dur=%u period=%u gain=%u trig=%x rep=%u axes=%u env=%d cb=%u", what,
        flags, e->dwSize, e->dwFlags, e->dwDuration, e->dwSamplePeriod, e->dwGain, e->dwTriggerButton,
        e->dwTriggerRepeatInterval, e->cAxes, e->lpEnvelope != nullptr, e->cbTypeSpecificParams);
    for (DWORD i = 0; i < e->cAxes && i < 8; ++i)
        Log("  axis %u ofs=%u dir=%d", i, e->rgdwAxes[i], e->rglDirection ? e->rglDirection[i] : 0);
    if (e->lpEnvelope)
        Log("  env %u %u %u %u %u", e->lpEnvelope->dwSize, e->lpEnvelope->dwAttackLevel, e->lpEnvelope->dwAttackTime,
            e->lpEnvelope->dwFadeLevel, e->lpEnvelope->dwFadeTime);
    if (e->lpvTypeSpecificParams) {
        const LONG *p = static_cast<const LONG *>(e->lpvTypeSpecificParams);
        std::string s = "  type";
        for (DWORD i = 0; i < e->cbTypeSpecificParams / 4 && i < 12; ++i)
            s += " " + std::to_string(p[i]);
        Log("%s", s.c_str());
    }
}

class FakeEffect : public IDirectInputEffect {
public:
    explicit FakeEffect(int id) : id_(id) {}
    STDMETHOD(QueryInterface)(REFIID, LPVOID *) override { return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
    STDMETHOD_(ULONG, Release)() override
    {
        Log("effect %d Release", id_);
        ULONG n = --refs_;
        if (n == 0)
            delete this;
        return n;
    }
    STDMETHOD(Initialize)(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
    STDMETHOD(GetEffectGuid)(LPGUID) override { return DI_OK; }
    STDMETHOD(GetParameters)(LPDIEFFECT, DWORD) override { return DI_OK; }
    STDMETHOD(SetParameters)(LPCDIEFFECT e, DWORD flags) override
    {
        char what[32];
        sprintf(what, "effect %d SetParameters", id_);
        LogEffectParams(what, e, flags);
        return Result(10);
    }
    STDMETHOD(Start)(DWORD iterations, DWORD flags) override
    {
        Log("effect %d Start %u %u", id_, iterations, flags);
        return Result(10);
    }
    STDMETHOD(Stop)() override
    {
        Log("effect %d Stop", id_);
        return Result(10);
    }
    STDMETHOD(GetEffectStatus)(LPDWORD status) override
    {
        *status = g_script.Next() & 3;
        Log("effect %d GetEffectStatus -> %u", id_, *status);
        return Result(10);
    }
    STDMETHOD(Download)() override
    {
        Log("effect %d Download", id_);
        return Result(10);
    }
    STDMETHOD(Unload)() override
    {
        Log("effect %d Unload", id_);
        return Result(10);
    }
    STDMETHOD(Escape)(LPDIEFFESCAPE) override { return DI_OK; }

private:
    int id_;
    ULONG refs_ = 1;
};

int g_nextEffect;

// ---------------------------------------------------------------------------------- fake device

class FakeDevice : public IDirectInputDevice7A {
public:
    explicit FakeDevice(int index) : d_(index), c_(g_devices[index]) {}
    STDMETHOD(QueryInterface)(REFIID, LPVOID *) override { return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
    STDMETHOD_(ULONG, Release)() override
    {
        Log("dev %d Release", d_);
        ULONG n = --refs_;
        if (n == 0)
            delete this;
        return n;
    }
    STDMETHOD(GetCapabilities)(LPDIDEVCAPS caps) override
    {
        Log("dev %d GetCapabilities size=%u", d_, caps->dwSize);
        *caps = c_.caps;
        return DI_OK;
    }
    STDMETHOD(EnumObjects)(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ctx, DWORD flags) override
    {
        Log("dev %d EnumObjects %x", d_, flags);
        for (const auto &o : c_.objects) {
            BOOL r = cb(&o, ctx);
            Log("  -> %d", r);
            if (!r)
                break;
        }
        return DI_OK;
    }
    STDMETHOD(GetProperty)(REFGUID prop, LPDIPROPHEADER h) override
    {
        DWORD id = static_cast<DWORD>(reinterpret_cast<uintptr_t>(&prop));
        Log("dev %d GetProperty %u size=%u hdr=%u obj=%u how=%u", d_, id, h->dwSize, h->dwHeaderSize, h->dwObj,
            h->dwHow);
        HRESULT hr = Result(15);
        if (id == 4) {
            int axis = static_cast<int>(h->dwObj / 4) & 7;
            if (SUCCEEDED(hr)) {
                reinterpret_cast<DIPROPRANGE *>(h)->lMin = c_.rangeMin[axis];
                reinterpret_cast<DIPROPRANGE *>(h)->lMax = c_.rangeMax[axis];
            }
        } else if (SUCCEEDED(hr)) {
            reinterpret_cast<DIPROPDWORD *>(h)->dwData = props_[id * 1000 + h->dwObj];
        }
        return hr;
    }
    STDMETHOD(SetProperty)(REFGUID prop, LPCDIPROPHEADER h) override
    {
        DWORD id = static_cast<DWORD>(reinterpret_cast<uintptr_t>(&prop));
        if (id == 4) {
            const DIPROPRANGE *r = reinterpret_cast<const DIPROPRANGE *>(h);
            Log("dev %d SetProperty RANGE size=%u obj=%u how=%u %d..%d", d_, h->dwSize, h->dwObj, h->dwHow, r->lMin,
                r->lMax);
            return c_.rangeFails[(h->dwObj / 4) & 7] ? DIERR_UNSUPPORTED : DI_OK;
        }
        DWORD v = reinterpret_cast<const DIPROPDWORD *>(h)->dwData;
        Log("dev %d SetProperty %u size=%u obj=%u how=%u data=%u", d_, id, h->dwSize, h->dwObj, h->dwHow, v);
        props_[id * 1000 + h->dwObj] = v;
        return Result(10);
    }
    STDMETHOD(Acquire)() override
    {
        Log("dev %d Acquire", d_);
        return Result(30);
    }
    STDMETHOD(Unacquire)() override
    {
        Log("dev %d Unacquire", d_);
        return DI_OK;
    }
    STDMETHOD(GetDeviceState)(DWORD size, LPVOID data) override
    {
        Log("dev %d GetDeviceState %u", d_, size);
        HRESULT hr = Result(15);
        DIJOYSTATE2 *s = static_cast<DIJOYSTATE2 *>(data);
        if (SUCCEEDED(hr)) {
            LONG *axes = &s->lX;
            for (int a = 0; a < 8; ++a) {
                int k = g_script.Int(4);
                axes[a] = k == 0 ? 0 : k == 1 ? static_cast<LONG>(g_script.Next() % 20001) - 10000
                                              : static_cast<LONG>(g_script.Next() % 140000) - 70000;
            }
            for (int p = 0; p < 4; ++p) {
                int k = g_script.Int(4);
                s->rgdwPOV[p] = k == 0 ? 0xFFFFFFFF : k == 1 ? 0x1234FFFF : k == 2 ? last_[p] : g_script.Next() % 36000;
                last_[p] = s->rgdwPOV[p];
            }
            for (int b = 0; b < 128; ++b)
                s->rgbButtons[b] = g_script.Chance(30) ? 0x80 : 0;
            s->lVX = static_cast<LONG>(g_script.Next());
        }
        return hr;
    }
    STDMETHOD(GetDeviceData)(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return E_NOTIMPL; }
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT f) override
    {
        Log("dev %d SetDataFormat %u %u %x %u %u", d_, f->dwSize, f->dwObjSize, f->dwFlags, f->dwDataSize, f->dwNumObjs);
        for (DWORD i = 0; i < f->dwNumObjs; ++i) {
            const DIOBJECTDATAFORMAT &o = f->rgodf[i];
            Log("  odf %u %d %u %x %x", i, o.pguid ? static_cast<int>(o.pguid->Data1 & 0xFFFF) : -1, o.dwOfs, o.dwType,
                o.dwFlags);
        }
        return c_.failFormat ? DIERR_INVALIDPARAM : DI_OK;
    }
    STDMETHOD(SetEventNotification)(HANDLE) override { return E_NOTIMPL; }
    STDMETHOD(SetCooperativeLevel)(HWND hwnd, DWORD level) override
    {
        Log("dev %d SetCooperativeLevel %p %x", d_, hwnd, level);
        return c_.failCoop ? DIERR_INVALIDPARAM : DI_OK;
    }
    STDMETHOD(GetObjectInfo)(LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD) override { return E_NOTIMPL; }
    STDMETHOD(GetDeviceInfo)(LPDIDEVICEINSTANCEA) override { return E_NOTIMPL; }
    STDMETHOD(RunControlPanel)(HWND hwnd, DWORD flags) override
    {
        Log("dev %d RunControlPanel %p %u", d_, hwnd, flags);
        return Result(30);
    }
    STDMETHOD(Initialize)(HINSTANCE, DWORD, REFGUID) override { return E_NOTIMPL; }
    STDMETHOD(CreateEffect)(REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN) override
    {
        char what[64];
        sprintf(what, "dev %d CreateEffect %d", d_, EffectIndex(g));
        LogEffectParams(what, e, 0);
        if (g_script.Chance(10)) {
            *out = nullptr;
            return DIERR_DEVICEFULL;
        }
        *out = new FakeEffect(g_nextEffect++);
        return DI_OK;
    }
    STDMETHOD(EnumEffects)(LPDIENUMEFFECTSCALLBACKA cb, LPVOID ctx, DWORD type) override
    {
        Log("dev %d EnumEffects %u", d_, type);
        for (const auto &e : c_.effects)
            Log("  -> %d", cb(&e, ctx));
        return DI_OK;
    }
    STDMETHOD(GetEffectInfo)(LPDIEFFECTINFOA, REFGUID) override { return E_NOTIMPL; }
    STDMETHOD(GetForceFeedbackState)(LPDWORD state) override
    {
        *state = g_script.Next() & 0x80000FFF;
        Log("dev %d GetForceFeedbackState -> %x", d_, *state);
        return Result(20);
    }
    STDMETHOD(SendForceFeedbackCommand)(DWORD command) override
    {
        Log("dev %d SendForceFeedbackCommand %x", d_, command);
        return Result(20);
    }
    STDMETHOD(EnumCreatedEffectObjects)(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override { return E_NOTIMPL; }
    STDMETHOD(Escape)(LPDIEFFESCAPE) override { return E_NOTIMPL; }
    STDMETHOD(Poll)() override
    {
        Log("dev %d Poll", d_);
        return Result(20);
    }
    STDMETHOD(SendDeviceData)(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return E_NOTIMPL; }
    STDMETHOD(EnumEffectsInFile)(LPCSTR file, LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID ctx, DWORD flags) override
    {
        Log("dev %d EnumEffectsInFile %s %x", d_, file, flags);
        int n = g_script.Int(4);
        for (int i = 0; i < n; ++i) {
            DIEFFECT e = {};
            e.dwSize = sizeof(e);
            e.dwGain = 1000 * i;
            DIFILEEFFECT fe = {};
            fe.dwSize = sizeof(fe);
            fe.GuidEffect = *kEffectGuids[i];
            fe.lpDiEffect = &e;
            Log("  -> %d", cb(&fe, ctx));
        }
        return Result(25);
    }
    STDMETHOD(WriteEffectToFile)(LPCSTR, DWORD, LPDIFILEEFFECT, DWORD) override { return E_NOTIMPL; }

private:
    int d_;
    DeviceConfig &c_;
    ULONG refs_ = 1;
    std::map<DWORD, DWORD> props_;
    DWORD last_[4] = {};
};

std::vector<FakeDevice *> g_opened;

// ---------------------------------------------------------------------------- fake DirectInput

class FakeDirectInput : public IDirectInput7A {
public:
    STDMETHOD(QueryInterface)(REFIID, LPVOID *) override { return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)() override
    {
        Log("di AddRef");
        return ++refs_;
    }
    STDMETHOD_(ULONG, Release)() override
    {
        Log("di Release");
        return --refs_;
    }
    STDMETHOD(CreateDevice)(REFGUID, LPDIRECTINPUTDEVICEA *, LPUNKNOWN) override { return E_NOTIMPL; }
    STDMETHOD(EnumDevices)(DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ctx, DWORD flags) override
    {
        Log("di EnumDevices %u %u", type, flags);
        for (const auto &d : g_devices) {
            BOOL r = cb(&d.instance, ctx);
            Log("  -> %d", r);
            if (!r)
                break;
        }
        return DI_OK;
    }
    STDMETHOD(GetDeviceStatus)(REFGUID) override { return E_NOTIMPL; }
    STDMETHOD(RunControlPanel)(HWND hwnd, DWORD flags) override
    {
        Log("di RunControlPanel %p %u", hwnd, flags);
        return Result(30);
    }
    STDMETHOD(Initialize)(HINSTANCE, DWORD) override { return E_NOTIMPL; }
    STDMETHOD(FindDevice)(REFGUID, LPCSTR, LPGUID) override { return E_NOTIMPL; }
    STDMETHOD(CreateDeviceEx)(REFGUID g, REFIID iid, LPVOID *out, LPUNKNOWN) override
    {
        int d = static_cast<int>(g.Data1 - 0x1000);
        Log("di CreateDeviceEx %d iid=%d", d, iid == IID_IDirectInputDevice7A);
        if (d < 0 || d >= static_cast<int>(g_devices.size()) || g_devices[d].failCreate) {
            *out = nullptr;
            return DIERR_DEVICENOTREG;
        }
        FakeDevice *dev = new FakeDevice(d);
        g_opened.push_back(dev);
        *out = dev;
        return DI_OK;
    }

private:
    ULONG refs_ = 1;
};

// ------------------------------------------------------------------------------------- driver

typedef bool(__stdcall *CreateJoyBaseFn)(IJoyBase **, HINSTANCE, HWND, IDirectInput7A *, DWORD);

float RandomFloat(Rng &r)
{
    static const float kValues[] = {-2.0f, -1.0f, -0.5f, 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 3.14159f, 1e-3f};
    if (r.Chance(60))
        return kValues[r.Int(sizeof(kValues) / sizeof(kValues[0]))];
    return static_cast<float>(static_cast<int>(r.Next() % 4001) - 2000) / 1000.0f;
}

int RandomIndex(Rng &r, int n) { return r.Chance(85) ? r.Int(n) : (r.Chance(50) ? -1 : n + r.Int(3)); }

void LogString(const char *what, const char *s) { Log("%s %s", what, s ? s : "(null)"); }

void LogInfo(const IJoyInfo &i)
{
    Log("info %s|%s %u %u %u %d %d", i.instanceName ? i.instanceName : "-", i.productName ? i.productName : "-",
        i.numAxes, i.numButtons, i.numPovs, i.forceFeedback, i.subType);
}

void LogAxisInfo(bool ok, const IJoyAxisInfo &a)
{
    Log("axisinfo %d %s %d %u %u %d", ok, a.name ? a.name : "-", a.ffActuator, a.ffForceResolution, a.ffMaxForce,
        a.aspect);
}

void LogState(const char *what, const IJoyState &s)
{
    std::string line = what;
    char buf[64];
    for (float a : s.axis) {
        sprintf(buf, " %08x", FloatBits(a));
        line += buf;
    }
    for (int p = 0; p < 4; ++p) {
        sprintf(buf, " p%08x/%d", FloatBits(s.pov[p]), s.povCentered[p]);
        line += buf;
    }
    for (DWORD b : s.buttons) {
        sprintf(buf, " b%08x", b);
        line += buf;
    }
    Log("%s", line.c_str());
}

void DriveEffect(Rng &r, IJoyEffect *e, int numAxes)
{
    int type = e->GetType();
    bool playable = type >= 0 && type < 11;
    int op = r.Int(26);
    if (op >= 23 || (!playable && r.Chance(50)))
        op = 0;  // (re)create often: most calls need a created effect
    switch (op) {
    case 0: {
        int t = r.Chance(80) ? r.Int(13) : RandomIndex(r, 11);
        if (t < 0)
            t = 12;
        bool file = r.Chance(70);
        bool ok = e->Create(t, RandomFloat(r), r.Chance(30) ? 0 : r.Next() % 3000000, RandomFloat(r),
                            file ? "test.ffe" : nullptr, r.Int(2));
        Log("Create %d -> %d type %d", t, ok, e->GetType());
        break;
    }
    case 1: {
        BYTE axes[9];
        int n = r.Int(10);
        for (int i = 0; i < 9; ++i)
            axes[i] = static_cast<BYTE>(r.Chance(90) ? r.Int(numAxes) : r.Int(12));
        Log("SetAxes %d -> %d", n, e->SetAxes(n, axes));
        break;
    }
    case 2: Log("Start -> %d", e->Start(r.Int(3), r.Chance(50))); break;
    case 3: Log("Stop -> %d", e->Stop(r.Chance(50))); break;
    case 4: Log("SetGain -> %d", e->SetGain(RandomFloat(r), r.Chance(50))); break;
    case 5: Log("CommitGain -> %d", e->CommitGain()); break;
    case 6: Log("SetDirection -> %d", e->SetDirection(RandomFloat(r) * 3.0f, r.Chance(50))); break;
    case 7: Log("CommitDirection -> %d", e->CommitDirection()); break;
    case 8: Log("SetDuration -> %d", e->SetDuration(r.Chance(30) ? 0 : r.Next() % 1000000, r.Chance(50))); break;
    case 9: Log("CommitDuration -> %d", e->CommitDuration()); break;
    case 10:
        Log("SetEnvelope -> %d", e->SetEnvelope(RandomFloat(r), r.Next() % 9999, RandomFloat(r), r.Next() % 9999,
                                                r.Chance(50)));
        break;
    case 11: Log("CommitEnvelope -> %d", e->CommitEnvelope()); break;
    case 12: Log("SetConstantForce -> %d", e->SetConstantForce(RandomFloat(r), r.Chance(50))); break;
    case 13: Log("SetRampForce -> %d", e->SetRampForce(RandomFloat(r), RandomFloat(r), r.Chance(50))); break;
    case 14:
        Log("SetPeriodic -> %d", e->SetPeriodic(RandomFloat(r), RandomFloat(r), r.Chance(50) ? 0.0f : RandomFloat(r),
                                                r.Next() % 100000, r.Chance(50)));
        break;
    case 15: {
        IJoyCondition c[2];
        for (auto &x : c)
            x = {RandomFloat(r), RandomFloat(r), RandomFloat(r), RandomFloat(r), RandomFloat(r), RandomFloat(r)};
        Log("SetCondition -> %d", e->SetCondition(&c[0], &c[1], r.Chance(50)));
        break;
    }
    case 16: Log("CommitTypeParams -> %d", e->CommitTypeParams()); break;
    case 17: {
        IJoyEffectCaps caps;
        std::memset(&caps, 0x55, sizeof(caps));
        int t = RandomIndex(r, 11);
        bool ok = e->GetEffectCaps(&caps, t);
        std::string s;
        for (int i = 0; i < 15; ++i)
            s += std::to_string(reinterpret_cast<BYTE *>(&caps)[i]) + ",";
        Log("GetEffectCaps %d -> %d %s", t, ok, ok ? s.c_str() : s.substr(0, 8).c_str());
        break;
    }
    case 18:
        if (playable) {
            DWORD st = 99;
            bool ok = e->GetStatus(&st);
            Log("GetStatus -> %d %u", ok, st);
        }
        break;
    default: Log("GetType -> %d", type); break;
    }
}

void DriveJoy(Rng &r, IJoy *j, std::vector<IJoyEffect *> &effects)
{
    int axis = RandomIndex(r, 8);
    switch (r.Int(40)) {
    case 0: Log("GetDeadZone %d -> %08x", axis, FloatBits(j->GetDeadZone(axis))); break;
    case 1: Log("SetDeadZone %d -> %d", axis, j->SetDeadZone(axis, RandomFloat(r) * 0.5f)); break;
    case 2: Log("GetDeviceDeadZone %d -> %08x", axis, FloatBits(j->GetDeviceDeadZone(axis))); break;
    case 3: Log("SetDeviceDeadZone %d -> %d", axis, j->SetDeviceDeadZone(axis, RandomFloat(r))); break;
    case 4: Log("GetSaturation %d -> %08x", axis, FloatBits(j->GetSaturation(axis))); break;
    case 5: Log("SetSaturation %d -> %d", axis, j->SetSaturation(axis, RandomFloat(r))); break;
    case 6: Log("GetDeviceSaturation %d -> %08x", axis, FloatBits(j->GetDeviceSaturation(axis))); break;
    case 7: Log("SetDeviceSaturation %d -> %d", axis, j->SetDeviceSaturation(axis, RandomFloat(r))); break;
    case 8: Log("GetAxisMode %d -> %u", axis, j->GetAxisMode(axis)); break;
    case 9:
    case 10: {
        DWORD mode = r.Chance(90) ? r.Int(16) : r.Next() % 40;
        Log("SetAxisMode %d %u -> %d", axis, mode, j->SetAxisMode(axis, mode));
        break;
    }
    case 11: Log("GetAxis %d -> %08x", axis, FloatBits(j->GetAxis(axis))); break;
    case 12: {
        int b = RandomIndex(r, 128);
        Log("IsButtonDown %d -> %d", b, j->IsButtonDown(b));
        break;
    }
    case 13: {
        int b = RandomIndex(r, 128);
        Log("IsButtonPressed %d -> %d", b, j->IsButtonPressed(b));
        break;
    }
    case 14: {
        int g = RandomIndex(r, 4);
        Log("GetButtons %d -> %08x %08x", g, j->GetButtons(g), j->GetButtonsPressed(g));
        break;
    }
    case 15: {
        int p = RandomIndex(r, 4);
        float a = 7.0f, b = 7.0f;
        bool x = j->GetPov(&a, p);
        bool y = j->GetPovChanged(&b, p);
        Log("GetPov %d -> %d %08x %d %08x", p, x, FloatBits(a), y, FloatBits(b));
        break;
    }
    case 16: {
        IJoyState s;
        std::memset(&s, 0, sizeof(s));
        Log("GetState -> %d", j->GetState(&s));
        LogState("state", s);
        std::memset(&s, 0, sizeof(s));
        Log("GetStatePressed -> %d", j->GetStatePressed(&s));
        LogState("pressed", s);
        break;
    }
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22: {
        DWORD flags = r.Int(2);
        Log("Update %u -> %d", flags, j->Update(flags));
        IJoyState s;
        j->GetState(&s);
        LogState("state", s);
        break;
    }
    case 23: LogString("GetAxisName", j->GetAxisName(axis)); break;
    case 24: {
        IJoyAxisInfo a;
        std::memset(&a, 0x33, sizeof(a));
        bool ok = j->GetAxisInfo(axis, &a);
        LogAxisInfo(ok, a);
        break;
    }
    case 25: LogString("GetButtonName", j->GetButtonName(RandomIndex(r, 40))); break;
    case 26: {
        IJoyInfo i = {};
        Log("GetInfo -> %d", j->GetInfo(&i));
        LogInfo(i);
        break;
    }
    case 27: LogString("GetPovName", j->GetPovName(RandomIndex(r, 4))); break;
    case 28: Log("RunControlPanel -> %d", j->RunControlPanel()); break;
    case 29: Log("GetForceFeedbackGain -> %08x", FloatBits(j->GetForceFeedbackGain())); break;
    case 30: Log("SetForceFeedbackGain -> %d", j->SetForceFeedbackGain(RandomFloat(r))); break;
    case 31: {
        DWORD st = 0x77;
        bool ok = j->GetForceFeedbackState(&st);
        Log("GetForceFeedbackState -> %d %x", ok, st);
        break;
    }
    case 32: {
        int c = r.Int(8);
        Log("SendForceFeedbackCommand %d -> %d", c, j->SendForceFeedbackCommand(c));
        break;
    }
    case 33: {
        IJoyEffect *e = nullptr;
        bool ok = j->CreateEffect(&e);
        Log("CreateEffect -> %d %d", ok, e != nullptr);
        if (e)
            effects.push_back(e);
        break;
    }
    default:
        if (!effects.empty()) {
            int k = r.Int(static_cast<int>(effects.size()));
            if (r.Chance(5)) {
                Log("effect Release -> %d", effects[k]->Release());
                effects.erase(effects.begin() + k);
            } else {
                for (int n = 1 + r.Int(4); n > 0; --n)
                    DriveEffect(r, effects[k], 8);
            }
        }
        break;
    }
}

std::vector<std::string> Run(CreateJoyBaseFn create, uint32_t seed, int steps)
{
    std::vector<std::string> log;
    g_log = &log;
    g_script = Rng{seed ^ 0x9E3779B9u};
    g_nextEffect = 0;
    g_opened.clear();
    Rng r{seed * 7919u + 13};
    FakeDirectInput di;
    IJoyBase *base = nullptr;
    HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234));
    bool ok = create(&base, GetModuleHandleA(nullptr), r.Chance(3) ? nullptr : hwnd, &di, 0);
    Log("CreateJoyBase -> %d %d", ok, base != nullptr);
    if (!ok)
        return log;
    std::vector<IJoy *> joys;
    std::vector<IJoyEffect *> effects;
    int numJoys = base->GetNumJoys();
    Log("GetNumJoys -> %d", numJoys);
    for (int step = 0; step < steps; ++step) {
        Log("-- step %d", step);
        if (joys.empty() || r.Chance(8)) {
            int joy = RandomIndex(r, numJoys);
            switch (r.Int(8)) {
            case 0: LogString("base GetAxisName", base->GetAxisName(joy, RandomIndex(r, 8))); break;
            case 1: {
                IJoyAxisInfo a;
                std::memset(&a, 0x33, sizeof(a));
                bool ok2 = base->GetAxisInfo(joy, RandomIndex(r, 8), &a);
                LogAxisInfo(ok2, a);
                break;
            }
            case 2: LogString("base GetButtonName", base->GetButtonName(joy, RandomIndex(r, 40))); break;
            case 3: {
                IJoyInfo i = {};
                Log("base GetJoyInfo -> %d", base->GetJoyInfo(&i, joy));
                LogInfo(i);
                break;
            }
            case 4: LogString("base GetPovName", base->GetPovName(joy, RandomIndex(r, 4))); break;
            case 5: Log("base RunControlPanel -> %d", base->RunControlPanel()); break;
            case 6: Log("base GetDirectInput -> %d", base->GetDirectInput() == &di); break;
            default: {
                IJoy *j = nullptr;
                DWORD flags = r.Int(2);
                bool ok2 = base->CreateJoy(&j, joy, flags);
                Log("base CreateJoy %d %u -> %d %d", joy, flags, ok2, j != nullptr);
                if (j)
                    joys.push_back(j);
                // A failed Joy constructor releases the base without having AddRef'd it (both
                // DLLs): the base may be gone, so the run ends here, leaking the rest.
                if (!ok2 && joy >= 0 && joy < numJoys)
                    return log;
                break;
            }
            }
            continue;
        }
        DriveJoy(r, joys[r.Int(static_cast<int>(joys.size()))], effects);
    }
    // Tear down: effects, then joysticks (each CreateJoy reference), then the base.
    for (IJoyEffect *e : effects)
        Log("final effect Release -> %d", e->Release());
    for (IJoy *j : joys)
        Log("final joy Release -> %d", j->Release());
    Log("final base Release -> %d", base->Release());
    return log;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::printf("usage: compare ORIGINAL_DLL PORT_DLL [seeds] [steps]\n");
        return 2;
    }
    HMODULE orig = LoadLibraryA(argv[1]);
    HMODULE port = LoadLibraryA(argv[2]);
    // After loading: the original's CRT start-up resets the precision control (in the game,
    // Direct3D sets 24 bits later too).
    unsigned cw;
    _controlfp_s(&cw, _PC_24, _MCW_PC);
    if (!orig || !port || orig == port) {
        std::printf("cannot load the DLLs (%lu)\n", GetLastError());
        return 2;
    }
    auto createOrig = reinterpret_cast<CreateJoyBaseFn>(GetProcAddress(orig, "_CreateJoyBase@20"));
    auto createPort = reinterpret_cast<CreateJoyBaseFn>(GetProcAddress(port, "_CreateJoyBase@20"));
    if (!createOrig || !createPort) {
        std::printf("export _CreateJoyBase@20 missing\n");
        return 2;
    }
    int seeds = argc > 3 ? std::atoi(argv[3]) : 2000;
    int steps = argc > 4 ? std::atoi(argv[4]) : 400;
    bool stats = argc > 5 && std::strcmp(argv[5], "stats") == 0;
    std::map<std::string, int> histogram;  // "stats": how often each kind of event happened
    long long lines = 0;
    int failures = 0, created = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        BuildDevices(seed);
        std::vector<std::string> a = Run(createOrig, seed, steps);
        std::vector<std::string> b = Run(createPort, seed, steps);
        lines += a.size();
        if (stats) {
            for (const std::string &line : a) {
                // The event name plus its result: words up to the first number after "->".
                size_t arrow = line.find("->");
                std::string key = line.substr(0, arrow == std::string::npos ? line.find_first_of("0123456789") : arrow + 4);
                ++histogram[key];
            }
        }
        for (const std::string &line : a) {
            if (line.compare(0, 10, "GetNumJoys") == 0) {
                ++created;
                break;
            }
        }
        size_t n = a.size() < b.size() ? a.size() : b.size();
        size_t i = 0;
        while (i < n && a[i] == b[i])
            ++i;
        if (i == n && a.size() == b.size())
            continue;
        if (++failures <= 5) {
            std::printf("seed %d: logs differ at line %zu\n", seed, i);
            for (size_t k = i >= 6 ? i - 6 : 0; k < i; ++k)
                std::printf("    %s\n", a[k].c_str());
            std::printf("  original: %s\n", i < a.size() ? a[i].c_str() : "(end)");
            std::printf("  port:     %s\n", i < b.size() ? b[i].c_str() : "(end)");
        }
    }
    for (const auto &h : histogram)
        std::printf("%8d  %s\n", h.second, h.first.c_str());
    std::printf("%d seeds (%d with a JoyBase), %lld log lines each side, %d failures\n", seeds, created, lines,
                failures);
    return failures ? 1 : 0;
}
