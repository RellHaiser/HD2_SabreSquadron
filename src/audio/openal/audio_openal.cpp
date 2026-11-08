// The OpenAL Soft backend of the sound device (audio/audio.h): DirectSound 8's model on OpenAL Soft,
// whose library (soft_oal.dll, else OpenAL32.dll) is loaded at run time. LS3DF.ini chooses it
// ([Sound] OpenAL, audio/audio.cpp).
//
// - A DirectSound buffer is an OpenAL source playing a callback buffer (AL_SOFT_callback_buffer).
//   The backend keeps the buffer's bytes as DirectSound does (the engine locks and writes them,
//   also while they play), and the mixer takes them through the callback, which moves the play
//   cursor, wraps or ends at the buffer's end and sets the position notifications the cursor
//   passes. The cursor is what the mixer has taken, a little ahead of what is heard.
// - 2D buffers become stereo in the callback, with DirectSound's pan (the far channel turned
//   down), and go to the output's front channels as they are (AL_DIRECT_CHANNELS_SOFT).
// - 3D buffers are mono sources that OpenAL places with DirectSound's distance model (inverse
//   distance, clamped between the minimum and maximum distances, the listener's rolloff factor).
//   DirectSound's axes are left-handed, OpenAL's right-handed: z is negated.
// - Buffers take the formats DirectSound takes: PCM of 8, 16, 24 or 32 bits and 32-bit float, as
//   WAVEFORMATEX or WAVEFORMATEXTENSIBLE, mono or stereo (mods ship 24-bit sounds).
// - Volumes in hundredths of a decibel become gains (10^(mB / 2000)); -10000 is silence.
// - EAX 3 goes to OpenAL Soft's EAX emulation (EAXSet / EAXGet take EAX 3's property sets).
// - The output follows the system and OpenAL Soft's own settings: SetSpeakerConfig is accepted and
//   ignored. No hardware buffers are reported, as DirectSound reports on Windows Vista and later,
//   so the voice manager stays off as it does there.
// - Bink plays a movie's sound itself through DirectSound: Native has no DirectSound object to
//   give, and Bink then opens its own.
#include "audio/openal/openal_device.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define AL_NO_PROTOTYPES
#define ALC_NO_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include "audio/backends.h"
#include "platform/platform.h"

namespace ls3d::audio {

namespace {

// ---------------------------------------------------------------- the library

using EaxFunction = ALenum(AL_APIENTRY *)(const GUID *set, ALuint property, ALuint source, ALvoid *value,
                                          ALuint size);

// The library's exports the backend calls.
#define LS3D_AL_EXPORTS(X)                                                                                  \
    X(LPALGENSOURCES, alGenSources)                                                                     \
    X(LPALDELETESOURCES, alDeleteSources)                                                               \
    X(LPALSOURCEI, alSourcei)                                                                           \
    X(LPALSOURCEF, alSourcef)                                                                           \
    X(LPALSOURCE3F, alSource3f)                                                                         \
    X(LPALSOURCEPLAY, alSourcePlay)                                                                     \
    X(LPALSOURCESTOP, alSourceStop)                                                                     \
    X(LPALGETSOURCEI, alGetSourcei)                                                                     \
    X(LPALGENBUFFERS, alGenBuffers)                                                                     \
    X(LPALDELETEBUFFERS, alDeleteBuffers)                                                               \
    X(LPALLISTENERF, alListenerf)                                                                       \
    X(LPALLISTENER3F, alListener3f)                                                                     \
    X(LPALLISTENERFV, alListenerfv)                                                                     \
    X(LPALDISTANCEMODEL, alDistanceModel)                                                               \
    X(LPALDOPPLERFACTOR, alDopplerFactor)                                                               \
    X(LPALSPEEDOFSOUND, alSpeedOfSound)                                                                 \
    X(LPALGETERROR, alGetError)                                                                         \
    X(LPALISEXTENSIONPRESENT, alIsExtensionPresent)                                                     \
    X(LPALGETPROCADDRESS, alGetProcAddress)                                                             \
    X(LPALCOPENDEVICE, alcOpenDevice)                                                                   \
    X(LPALCCLOSEDEVICE, alcCloseDevice)                                                                 \
    X(LPALCCREATECONTEXT, alcCreateContext)                                                             \
    X(LPALCDESTROYCONTEXT, alcDestroyContext)                                                           \
    X(LPALCMAKECONTEXTCURRENT, alcMakeContextCurrent)                                                   \
    X(LPALCGETSTRING, alcGetString)                                                                     \
    X(LPALCISEXTENSIONPRESENT, alcIsExtensionPresent)                                                   \
    X(LPALCGETPROCADDRESS, alcGetProcAddress)

struct S_al {
#define LS3D_AL_MEMBER(type, name) type name;
    LS3D_AL_EXPORTS(LS3D_AL_MEMBER)
#undef LS3D_AL_MEMBER
    // Extensions (alGetProcAddress / alcGetProcAddress).
    LPALBUFFERCALLBACKSOFT alBufferCallbackSOFT;
    LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT;
    LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT;
    EaxFunction EAXSet;
    EaxFunction EAXGet;
};
S_al al;

bool LoadLibraryOnce()
{
    static int state;               // 0: not tried, 1: loaded, -1: missing
    if (state)
        return state > 0;
    state = -1;
    platform::Library *library = nullptr;
    for (const char *name : {"soft_oal.dll", "OpenAL32.dll"}) {
        if ((library = platform::OpenLibrary(name)) != nullptr)
            break;
    }
    if (!library)
        return false;
    bool found = true;
#define LS3D_AL_LOAD(type, name) \
    found &= (al.name = reinterpret_cast<type>(platform::LibrarySymbol(library, #name))) != nullptr;
    LS3D_AL_EXPORTS(LS3D_AL_LOAD)
#undef LS3D_AL_LOAD
    if (!found) {
        platform::CloseLibrary(library);
        return false;
    }
    al.alcLoopbackOpenDeviceSOFT =
        reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(al.alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
    al.alcRenderSamplesSOFT =
        reinterpret_cast<LPALCRENDERSAMPLESSOFT>(al.alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));
    state = 1;
    return true;
}

// The loopback device of the tests (openal_device.h).
int g_loopbackRate;
ALCdevice *g_loopback;

// ---------------------------------------------------------------- conversions

// Hundredths of a decibel to a gain.
float Gain(LONG mB) { return mB <= DSBVOLUME_MIN ? 0.0f : std::pow(10.0f, static_cast<float>(mB) / 2000.0f); }

// DirectSound's frequency limits (DirectX 8's DSBFREQUENCY_MIN / MAX).
const DWORD kMinFrequency = 100;
const DWORD kMaxFrequency = 100000;

// The output devices: OpenAL's names, under GUIDs made from their place in the list.
std::vector<std::string> DeviceNames()
{
    const bool all = al.alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") != ALC_FALSE;
    const ALCchar *list = al.alcGetString(nullptr, all ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER);
    std::vector<std::string> names;
    for (const ALCchar *p = list; p && *p; p += std::strlen(p) + 1)
        names.push_back(p);
    return names;
}
GUID DeviceGuid(size_t index)
{
    // {4c533344-4f41-4c00-0000-0000000000nn} ("LS3D", "OAL")
    GUID g = {0x4c533344, 0x4f41, 0x4c00, {0, 0, 0, 0, 0, 0, 0, 0}};
    g.Data4[6] = static_cast<BYTE>(index >> 8);
    g.Data4[7] = static_cast<BYTE>(index);
    return g;
}

// ---------------------------------------------------------------- objects

struct S_buffer;

// The device and its OpenAL context.
struct S_system {
    std::atomic<ULONG> refs{1};
    ALCdevice *device = nullptr;
    ALCcontext *context = nullptr;
    bool eax = false;                       // OpenAL Soft's EAX 3 emulation
    DS3DLISTENER listener = {};
    S_buffer *primary = nullptr;
    std::mutex lock;
    std::vector<S_buffer *> buffers3D;      // (the listener's rolloff factor is theirs)

    S_system()
    {
        listener.dwSize = sizeof listener;
        listener.vOrientFront.z = 1.0f;
        listener.vOrientTop.y = 1.0f;
        listener.flDistanceFactor = 1.0f;
        listener.flRolloffFactor = 1.0f;
        listener.flDopplerFactor = 1.0f;
    }
    void AddRef() { ++refs; }
    ULONG Release();
};

// One of a buffer's other interfaces (3D, notifications, property set, the primary buffer's
// listener): a handle that shares the buffer's reference count.
struct S_part {
    S_buffer *owner;
};

struct S_buffer {
    std::atomic<ULONG> refs{1};
    S_system *system;
    DWORD flags;                            // DSBCAPS_*
    bool primary;
    bool is3D;
    WAVEFORMATEX format = {};
    std::shared_ptr<std::vector<uint8_t>> data;     // shared with duplicates
    DWORD size = 0;                         // whole frames
    ALuint source = 0;
    ALuint alBuffer = 0;

    // The mixer's callback reads and writes these (under `lock`).
    std::mutex lock;
    DWORD cursor = 0;                       // the play cursor: the next byte the mixer takes
    bool playing = false;                   // from Play to Stop or the end
    bool looping = false;
    float gainLeft = 1.0f, gainRight = 1.0f;    // 2D: DirectSound's pan
    std::vector<DSBPOSITIONNOTIFY> notifications;

    // Settings (a duplicate starts with the original's).
    LONG volume = 0;
    LONG pan = 0;
    DWORD frequency = 0;                    // 0: the format's rate
    DWORD mode = DS3DMODE_NORMAL;
    D3DVECTOR position = {}, velocity = {};

    S_part part3D{this}, partNotify{this}, partProperties{this}, partListener{this};

    S_buffer(S_system *s, DWORD f, bool primaryBuffer)
        : system(s), flags(f), primary(primaryBuffer), is3D(!primaryBuffer && (f & DSBCAPS_CTRL3D))
    {
        system->AddRef();
    }
    void AddRef() { ++refs; }
    ULONG Release();
};

S_system *Sys(System *s) { return reinterpret_cast<S_system *>(s); }
S_buffer *Buf(Buffer *b) { return reinterpret_cast<S_buffer *>(b); }
template <class H> S_buffer *Owner(H *h) { return reinterpret_cast<S_part *>(h)->owner; }
template <class H> H *PartHandle(S_part &part)
{
    part.owner->AddRef();
    return reinterpret_cast<H *>(&part);
}

ULONG S_system::Release()
{
    const ULONG left = --refs;
    if (left)
        return left;
    if (context) {
        al.alcMakeContextCurrent(nullptr);
        al.alcDestroyContext(context);
    }
    if (device)
        al.alcCloseDevice(device);
    if (device == g_loopback)
        g_loopback = nullptr;
    delete this;
    return 0;
}

ULONG S_buffer::Release()
{
    const ULONG left = --refs;
    if (left)
        return left;
    if (source) {
        {
            std::lock_guard<std::mutex> guard(lock);
            playing = false;
        }
        al.alSourceStop(source);            // (waits for the mixer: the callback is done with us)
        al.alSourcei(source, AL_BUFFER, 0);
        al.alDeleteSources(1, &source);
        al.alDeleteBuffers(1, &alBuffer);
    }
    if (is3D) {
        std::lock_guard<std::mutex> guard(system->lock);
        auto &list = system->buffers3D;
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i] == this) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    if (system->primary == this)
        system->primary = nullptr;
    S_system *s = system;
    delete this;
    s->Release();
    return 0;
}

// ---------------------------------------------------------------- the mixer's callback

// Sets the notifications whose offsets lie in [from, to) (lock held).
void NotifyRange(S_buffer *b, DWORD from, DWORD to)
{
    for (const DSBPOSITIONNOTIFY &n : b->notifications) {
        if (n.dwOffset != DSBPN_OFFSETSTOP && n.dwOffset >= from && n.dwOffset < to)
            platform::SetEvent(platform::EventFromNative(n.hEventNotify));
    }
}

void NotifyStop(S_buffer *b)
{
    for (const DSBPOSITIONNOTIFY &n : b->notifications) {
        if (n.dwOffset == DSBPN_OFFSETSTOP)
            platform::SetEvent(platform::EventFromNative(n.hEventNotify));
    }
}

// One sample of the buffer's format as a float (-1 - 1): PCM of 8 (unsigned), 16, 24 or 32 bits,
// or 32-bit float (`isFloat`).
inline float Sample(const uint8_t *p, int bits, bool isFloat)
{
    switch (bits) {
    case 8:
        return (static_cast<float>(*p) - 128.0f) * (1.0f / 128.0f);
    case 16: {
        int16_t v;
        std::memcpy(&v, p, 2);
        return static_cast<float>(v) * (1.0f / 32768.0f);
    }
    case 24: {
        const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(p[0]) << 8 | static_cast<uint32_t>(p[1]) << 16 |
                                               static_cast<uint32_t>(p[2]) << 24) >> 8;
        return static_cast<float>(v) * (1.0f / 8388608.0f);
    }
    default: {
        if (isFloat) {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        int32_t v;
        std::memcpy(&v, p, 4);
        return static_cast<float>(v) * (1.0f / 2147483648.0f);
    }
    }
}

// `frames` frames of the buffer from `src` into `dst`: stereo with the pan (2D) or mono (3D).
void Convert(const S_buffer *b, const uint8_t *src, int frames, float *dst)
{
    const int bits = b->format.wBitsPerSample;
    const bool isFloat = b->format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    const int bytes = bits / 8;
    const bool stereo = b->format.nChannels == 2;
    for (int i = 0; i < frames; ++i, src += b->format.nBlockAlign) {
        const float left = Sample(src, bits, isFloat);
        const float right = stereo ? Sample(src + bytes, bits, isFloat) : left;
        if (b->is3D) {
            *dst++ = stereo ? (left + right) * 0.5f : left;
        } else {
            *dst++ = left * b->gainLeft;
            *dst++ = right * b->gainRight;
        }
    }
}

ALsizei AL_APIENTRY Fill(ALvoid *user, ALvoid *out, ALsizei bytes) noexcept
{
    S_buffer *b = static_cast<S_buffer *>(user);
    const int channels = b->is3D ? 1 : 2;
    const int frames = bytes / (channels * static_cast<int>(sizeof(float)));
    float *dst = static_cast<float *>(out);
    const DWORD block = b->format.nBlockAlign;
    std::lock_guard<std::mutex> guard(b->lock);
    int done = 0;
    while (done < frames && b->playing && b->size) {
        if (b->cursor >= b->size)
            b->cursor = 0;
        const DWORD from = b->cursor;
        const int n = static_cast<int>(std::min<DWORD>(static_cast<DWORD>(frames - done), (b->size - from) / block));
        Convert(b, b->data->data() + from, n, dst + done * channels);
        done += n;
        b->cursor = from + static_cast<DWORD>(n) * block;
        NotifyRange(b, from, b->cursor);
        if (b->cursor >= b->size) {
            b->cursor = 0;
            if (!b->looping) {
                // The end: the source stops once the mixer has played what it took.
                b->playing = false;
                NotifyStop(b);
            }
        }
    }
    return done * channels * static_cast<ALsizei>(sizeof(float));
}

// ---------------------------------------------------------------- 3D

// DirectSound's vector in OpenAL's axes.
void SetVector(ALuint source, ALenum param, const D3DVECTOR &v) { al.alSource3f(source, param, v.x, v.y, -v.z); }

void Apply3D(S_buffer *b)
{
    if (b->mode == DS3DMODE_DISABLE) {
        // No 3D processing: in the listener's head, full volume.
        al.alSourcei(b->source, AL_SOURCE_RELATIVE, AL_TRUE);
        al.alSource3f(b->source, AL_POSITION, 0.0f, 0.0f, 0.0f);
        al.alSource3f(b->source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        return;
    }
    al.alSourcei(b->source, AL_SOURCE_RELATIVE, b->mode == DS3DMODE_HEADRELATIVE ? AL_TRUE : AL_FALSE);
    SetVector(b->source, AL_POSITION, b->position);
    SetVector(b->source, AL_VELOCITY, b->velocity);
}

void ApplyListener(S_system *s)
{
    const DS3DLISTENER &l = s->listener;
    al.alListener3f(AL_POSITION, l.vPosition.x, l.vPosition.y, -l.vPosition.z);
    al.alListener3f(AL_VELOCITY, l.vVelocity.x, l.vVelocity.y, -l.vVelocity.z);
    const ALfloat orientation[6] = {l.vOrientFront.x, l.vOrientFront.y, -l.vOrientFront.z,
                                    l.vOrientTop.x,   l.vOrientTop.y,   -l.vOrientTop.z};
    al.alListenerfv(AL_ORIENTATION, orientation);
    al.alDopplerFactor(l.flDopplerFactor);
    // The distance factor is metres per unit; velocities are units a second.
    if (l.flDistanceFactor > 0.0f)
        al.alSpeedOfSound(343.3f / l.flDistanceFactor);
    std::lock_guard<std::mutex> guard(s->lock);
    for (S_buffer *b : s->buffers3D)
        al.alSourcef(b->source, AL_ROLLOFF_FACTOR, l.flRolloffFactor);
}

// ---------------------------------------------------------------- the device

// The sample format (WAVE_FORMAT_PCM, _IEEE_FLOAT, ...) of a format, the extensible one's from its
// sub-format GUID (whose first two bytes are the format tag).
WORD SampleFormat(const WAVEFORMATEX &f)
{
    const WORD kExtensible = 0xfffe;        // WAVE_FORMAT_EXTENSIBLE
    if (f.wFormatTag != kExtensible)
        return f.wFormatTag;
    if (f.cbSize < 22)
        return 0;
    // WAVEFORMATEXTENSIBLE: the WAVEFORMATEX, wValidBitsPerSample, dwChannelMask, SubFormat.
    WORD tag;
    std::memcpy(&tag, reinterpret_cast<const uint8_t *>(&f) + 24, sizeof tag);
    return tag;
}

// A secondary buffer's OpenAL objects and first settings; false when OpenAL has none left.
bool CreateSource(S_buffer *b)
{
    al.alGetError();
    al.alGenBuffers(1, &b->alBuffer);
    if (al.alGetError() != AL_NO_ERROR)
        return false;
    al.alBufferCallbackSOFT(b->alBuffer, b->is3D ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_STEREO_FLOAT32,
                            static_cast<ALsizei>(b->format.nSamplesPerSec), Fill, b);
    al.alGenSources(1, &b->source);
    if (al.alGetError() != AL_NO_ERROR) {
        b->source = 0;
        al.alDeleteBuffers(1, &b->alBuffer);
        return false;
    }
    al.alSourcei(b->source, AL_BUFFER, static_cast<ALint>(b->alBuffer));
    al.alSourcef(b->source, AL_GAIN, Gain(b->volume));
    if (b->frequency)
        al.alSourcef(b->source, AL_PITCH,
                     static_cast<float>(b->frequency) / static_cast<float>(b->format.nSamplesPerSec));
    if (b->is3D) {
        al.alSourcef(b->source, AL_REFERENCE_DISTANCE, DS3D_DEFAULTMINDISTANCE);
        al.alSourcef(b->source, AL_MAX_DISTANCE, DS3D_DEFAULTMAXDISTANCE);
        al.alSourcef(b->source, AL_ROLLOFF_FACTOR, b->system->listener.flRolloffFactor);
        al.alSourcef(b->source, AL_CONE_INNER_ANGLE, 360.0f);
        al.alSourcef(b->source, AL_CONE_OUTER_ANGLE, 360.0f);
        al.alSource3f(b->source, AL_DIRECTION, 0.0f, 0.0f, -1.0f);
        Apply3D(b);
        std::lock_guard<std::mutex> guard(b->system->lock);
        b->system->buffers3D.push_back(b);
    } else {
        al.alSourcei(b->source, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);
        al.alSourcei(b->source, AL_SOURCE_RELATIVE, AL_TRUE);
    }
    return true;
}

class DeviceOpenAL final : public Device {
public:
    HRESULT Enumerate(LPDSENUMCALLBACKA callback, void *context) override
    {
        static char primary[] = "Primary Sound Driver", none[] = "", module[] = "soft_oal.dll";
        if (!callback(nullptr, primary, none, context))
            return DS_OK;
        const std::vector<std::string> names = DeviceNames();
        for (size_t i = 0; i < names.size(); ++i) {
            GUID guid = DeviceGuid(i);
            std::string name = names[i];
            if (!callback(&guid, &name[0], module, context))
                break;
        }
        return DS_OK;
    }
    // EAX Unified's DirectSound: OpenAL Soft has EAX of its own.
    HRESULT CreateInstance(REFCLSID clsid, System **system) override
    {
        static const GUID kEaxDirectSound8 = {0xca503b60, 0xb176, 0x11d4, {0xa0, 0x94, 0xd0, 0xc0, 0xbf, 0x3a, 0x56, 0x0c}};
        *system = nullptr;
        if (std::memcmp(&clsid, &kEaxDirectSound8, sizeof(GUID)) != 0)
            return REGDB_E_CLASSNOTREG;
        *system = reinterpret_cast<System *>(new S_system);
        return DS_OK;
    }
    HRESULT Initialize(System *system, const GUID *device) override
    {
        S_system *s = Sys(system);
        if (s->context)
            return DSERR_ALREADYINITIALIZED;
        if (g_loopbackRate) {
            s->device = al.alcLoopbackOpenDeviceSOFT ? al.alcLoopbackOpenDeviceSOFT(nullptr) : nullptr;
            g_loopback = s->device;
        } else {
            std::string name;
            if (device) {
                const std::vector<std::string> names = DeviceNames();
                for (size_t i = 0; i < names.size(); ++i) {
                    const GUID guid = DeviceGuid(i);
                    if (!std::memcmp(&guid, device, sizeof guid))
                        name = names[i];
                }
            }
            s->device = al.alcOpenDevice(name.empty() ? nullptr : name.c_str());
        }
        if (!s->device)
            return DSERR_NODRIVER;
        // Sources for every buffer the engine may make (each holds one, playing or not).
        std::vector<ALCint> attributes = {ALC_MONO_SOURCES, 4096, ALC_STEREO_SOURCES, 4096};
        if (g_loopbackRate) {
            const ALCint loopback[] = {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT,
                                       ALC_FREQUENCY, g_loopbackRate, ALC_HRTF_SOFT, ALC_FALSE,
                                       ALC_OUTPUT_LIMITER_SOFT, ALC_FALSE};
            attributes.insert(attributes.end(), std::begin(loopback), std::end(loopback));
        }
        attributes.push_back(0);
        s->context = al.alcCreateContext(s->device, attributes.data());
        if (!s->context || !al.alcMakeContextCurrent(s->context)) {
            if (s->context)
                al.alcDestroyContext(s->context);
            s->context = nullptr;
            al.alcCloseDevice(s->device);
            s->device = nullptr;
            return DSERR_NODRIVER;
        }
        if (!al.alIsExtensionPresent("AL_SOFT_callback_buffer") ||
            !(al.alBufferCallbackSOFT =
                  reinterpret_cast<LPALBUFFERCALLBACKSOFT>(al.alGetProcAddress("alBufferCallbackSOFT")))) {
            al.alcMakeContextCurrent(nullptr);
            al.alcDestroyContext(s->context);
            s->context = nullptr;
            al.alcCloseDevice(s->device);
            s->device = nullptr;
            return DSERR_NODRIVER;
        }
        al.EAXSet = reinterpret_cast<EaxFunction>(al.alGetProcAddress("EAXSet"));
        al.EAXGet = reinterpret_cast<EaxFunction>(al.alGetProcAddress("EAXGet"));
        s->eax = al.alIsExtensionPresent("EAX3.0") && al.EAXSet && al.EAXGet;
        al.alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
        ApplyListener(s);
        return DS_OK;
    }
    HRESULT Create(const GUID *device, System **system) override
    {
        *system = reinterpret_cast<System *>(new S_system);
        const HRESULT hr = Initialize(*system, device);
        if (FAILED(hr)) {
            Sys(*system)->Release();
            *system = nullptr;
        }
        return hr;
    }
    ULONG Release(System *system) override { return Sys(system)->Release(); }
    HRESULT GetCaps(System *system, DSCAPS *caps) override
    {
        if (!Sys(system)->context)
            return DSERR_UNINITIALIZED;
        const DWORD size = caps->dwSize;
        std::memset(caps, 0, sizeof *caps);
        caps->dwSize = size;
        caps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT |
                        DSCAPS_CONTINUOUSRATE | DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY8BIT |
                        DSCAPS_SECONDARY16BIT;
        caps->dwMinSecondarySampleRate = kMinFrequency;
        caps->dwMaxSecondarySampleRate = kMaxFrequency;
        caps->dwPrimaryBuffers = 1;
        return DS_OK;
    }
    HRESULT SetCooperativeLevel(System *system, HWND, DWORD) override
    {
        return Sys(system)->context ? DS_OK : DSERR_UNINITIALIZED;
    }
    HRESULT SetSpeakerConfig(System *system, DWORD) override
    {
        return Sys(system)->context ? DS_OK : DSERR_UNINITIALIZED;
    }
    HRESULT CreateSoundBuffer(System *system, const DSBUFFERDESC *desc, Buffer **buffer) override
    {
        S_system *s = Sys(system);
        *buffer = nullptr;
        if (!s->context)
            return DSERR_UNINITIALIZED;
        if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) {
            if (s->primary) {
                s->primary->AddRef();
            } else {
                s->primary = new S_buffer(s, desc->dwFlags, true);
                s->primary->format.wFormatTag = WAVE_FORMAT_PCM;
                s->primary->format.nChannels = 2;
                s->primary->format.nSamplesPerSec = 22050;
                s->primary->format.wBitsPerSample = 8;
                s->primary->format.nBlockAlign = 2;
                s->primary->format.nAvgBytesPerSec = 44100;
            }
            *buffer = reinterpret_cast<Buffer *>(s->primary);
            return DS_OK;
        }
        const WAVEFORMATEX *f = desc->lpwfxFormat;
        const WORD tag = f ? SampleFormat(*f) : 0;
        const bool pcm = tag == WAVE_FORMAT_PCM &&
            (f->wBitsPerSample == 8 || f->wBitsPerSample == 16 || f->wBitsPerSample == 24 || f->wBitsPerSample == 32);
        const bool ieeeFloat = tag == WAVE_FORMAT_IEEE_FLOAT && f->wBitsPerSample == 32;
        if (!(pcm || ieeeFloat) || (f->nChannels != 1 && f->nChannels != 2) ||
            f->nBlockAlign != f->nChannels * f->wBitsPerSample / 8 || f->nSamplesPerSec < kMinFrequency ||
            f->nSamplesPerSec > kMaxFrequency)
            return DSERR_BADFORMAT;
        if (desc->dwBufferBytes < DSBSIZE_MIN || desc->dwBufferBytes > DSBSIZE_MAX)
            return DSERR_INVALIDPARAM;
        if ((desc->dwFlags & DSBCAPS_CTRL3D) && f->nChannels != 1)
            return DSERR_INVALIDPARAM;      // (DirectSound's 3D buffers are mono)
        S_buffer *b = new S_buffer(s, desc->dwFlags, false);
        b->format = *f;
        b->format.wFormatTag = tag;         // (an extensible format as its sample format)
        b->format.cbSize = 0;
        const uint8_t silence = f->wBitsPerSample == 8 ? 0x80 : 0;
        b->data = std::make_shared<std::vector<uint8_t>>(desc->dwBufferBytes, silence);
        b->size = desc->dwBufferBytes / f->nBlockAlign * f->nBlockAlign;
        if (!CreateSource(b)) {
            b->Release();
            return DSERR_OUTOFMEMORY;
        }
        *buffer = reinterpret_cast<Buffer *>(b);
        return DS_OK;
    }
    HRESULT DuplicateSoundBuffer(System *system, Buffer *original, Buffer **duplicate) override
    {
        S_buffer *o = Buf(original);
        *duplicate = nullptr;
        if (o->primary)
            return DSERR_INVALIDCALL;
        S_buffer *b = new S_buffer(Sys(system), o->flags, false);
        b->format = o->format;
        b->data = o->data;
        b->size = o->size;
        {
            std::lock_guard<std::mutex> guard(o->lock);
            b->gainLeft = o->gainLeft;
            b->gainRight = o->gainRight;
        }
        b->volume = o->volume;
        b->pan = o->pan;
        b->frequency = o->frequency;
        if (!CreateSource(b)) {
            b->Release();
            return DSERR_OUTOFMEMORY;
        }
        *duplicate = reinterpret_cast<Buffer *>(b);
        return DS_OK;
    }
    void *Native(System *) override { return nullptr; }

    // ---- buffers
    HRESULT Query(Buffer *buffer, Buffer **buffer8) override
    {
        Buf(buffer)->AddRef();
        *buffer8 = buffer;
        return DS_OK;
    }
    HRESULT Query(Buffer *buffer, Buffer3D **buffer3D) override
    {
        S_buffer *b = Buf(buffer);
        *buffer3D = nullptr;
        if (!b->is3D)
            return E_NOINTERFACE;
        *buffer3D = PartHandle<Buffer3D>(b->part3D);
        return DS_OK;
    }
    HRESULT Query(Buffer *buffer, Listener **listener) override
    {
        S_buffer *b = Buf(buffer);
        *listener = nullptr;
        if (!b->primary || !(b->flags & DSBCAPS_CTRL3D))
            return E_NOINTERFACE;
        *listener = PartHandle<Listener>(b->partListener);
        return DS_OK;
    }
    HRESULT Query(Buffer *buffer, Notify **notify) override
    {
        S_buffer *b = Buf(buffer);
        *notify = nullptr;
        if (b->primary || !(b->flags & DSBCAPS_CTRLPOSITIONNOTIFY))
            return E_NOINTERFACE;
        *notify = PartHandle<Notify>(b->partNotify);
        return DS_OK;
    }
    ULONG Release(Buffer *buffer) override { return Buf(buffer)->Release(); }
    HRESULT SetFormat(Buffer *buffer, const WAVEFORMATEX *format) override
    {
        S_buffer *b = Buf(buffer);
        if (!b->primary)
            return DSERR_INVALIDCALL;
        b->format = *format;                // (the mix's format is OpenAL's)
        b->format.cbSize = 0;
        return DS_OK;
    }
    HRESULT Lock(Buffer *buffer, DWORD offset, DWORD bytes, void **data1, DWORD *bytes1, void **data2,
                 DWORD *bytes2, DWORD flags) override
    {
        S_buffer *b = Buf(buffer);
        if (b->primary)
            return DSERR_PRIOLEVELNEEDED;
        const DWORD size = static_cast<DWORD>(b->data->size());
        if (flags & DSBLOCK_FROMWRITECURSOR) {
            std::lock_guard<std::mutex> guard(b->lock);
            offset = b->cursor;
        }
        if (flags & DSBLOCK_ENTIREBUFFER) {
            offset = 0;
            bytes = size;
        }
        if (!bytes || offset >= size || bytes > size)
            return DSERR_INVALIDPARAM;
        const DWORD first = bytes < size - offset ? bytes : size - offset;
        *data1 = b->data->data() + offset;
        *bytes1 = first;
        if (data2)
            *data2 = bytes > first ? b->data->data() : nullptr;
        if (bytes2)
            *bytes2 = bytes - first;
        return DS_OK;
    }
    HRESULT Unlock(Buffer *, void *, DWORD, void *, DWORD) override { return DS_OK; }
    HRESULT Play(Buffer *buffer, DWORD, DWORD, DWORD flags) override
    {
        S_buffer *b = Buf(buffer);
        if (b->primary)
            return DS_OK;
        bool start;
        {
            std::lock_guard<std::mutex> guard(b->lock);
            b->looping = (flags & DSBPLAY_LOOPING) != 0;
            start = !b->playing;
            b->playing = true;
        }
        if (start)
            al.alSourcePlay(b->source);
        return DS_OK;
    }
    HRESULT Stop(Buffer *buffer) override
    {
        S_buffer *b = Buf(buffer);
        if (b->primary)
            return DS_OK;
        bool wasPlaying;
        {
            std::lock_guard<std::mutex> guard(b->lock);
            wasPlaying = b->playing;
            b->playing = false;
        }
        al.alSourceStop(b->source);
        if (wasPlaying) {
            std::lock_guard<std::mutex> guard(b->lock);
            NotifyStop(b);
        }
        return DS_OK;
    }
    HRESULT GetStatus(Buffer *buffer, DWORD *status) override
    {
        S_buffer *b = Buf(buffer);
        *status = 0;
        if (b->primary)
            return DS_OK;
        // Playing until the mixer has played the last bytes it took.
        ALint state = AL_STOPPED;
        al.alGetSourcei(b->source, AL_SOURCE_STATE, &state);
        if (state == AL_PLAYING) {
            std::lock_guard<std::mutex> guard(b->lock);
            *status = DSBSTATUS_PLAYING | (b->looping ? DSBSTATUS_LOOPING : 0);
        }
        return DS_OK;
    }
    HRESULT GetCurrentPosition(Buffer *buffer, DWORD *play, DWORD *write) override
    {
        S_buffer *b = Buf(buffer);
        if (b->primary)
            return DSERR_INVALIDCALL;
        std::lock_guard<std::mutex> guard(b->lock);
        if (play)
            *play = b->cursor;
        if (write)
            *write = b->cursor;
        return DS_OK;
    }
    HRESULT SetCurrentPosition(Buffer *buffer, DWORD position) override
    {
        S_buffer *b = Buf(buffer);
        if (b->primary)
            return DSERR_INVALIDCALL;
        // (The mixer takes only a few frames ahead: a playing buffer goes on from here in the next mix.)
        std::lock_guard<std::mutex> guard(b->lock);
        const DWORD block = b->format.nBlockAlign;
        b->cursor = position >= b->size ? 0 : position / block * block;
        return DS_OK;
    }
    HRESULT SetVolume(Buffer *buffer, LONG volume) override
    {
        S_buffer *b = Buf(buffer);
        if (volume > DSBVOLUME_MAX || volume < DSBVOLUME_MIN)
            return DSERR_INVALIDPARAM;
        b->volume = volume;
        if (b->primary)
            al.alListenerf(AL_GAIN, Gain(volume));
        else
            al.alSourcef(b->source, AL_GAIN, Gain(volume));
        return DS_OK;
    }
    HRESULT SetPan(Buffer *buffer, LONG pan) override
    {
        S_buffer *b = Buf(buffer);
        if (pan > DSBPAN_RIGHT || pan < DSBPAN_LEFT)
            return DSERR_INVALIDPARAM;
        b->pan = pan;
        std::lock_guard<std::mutex> guard(b->lock);
        b->gainLeft = pan > 0 ? Gain(-pan) : 1.0f;
        b->gainRight = pan < 0 ? Gain(pan) : 1.0f;
        return DS_OK;
    }
    HRESULT SetFrequency(Buffer *buffer, DWORD frequency) override
    {
        S_buffer *b = Buf(buffer);
        if (b->primary)
            return DSERR_INVALIDCALL;
        if (frequency && (frequency < kMinFrequency || frequency > kMaxFrequency))
            return DSERR_INVALIDPARAM;
        b->frequency = frequency;
        const DWORD hz = frequency ? frequency : b->format.nSamplesPerSec;
        al.alSourcef(b->source, AL_PITCH, static_cast<float>(hz) / static_cast<float>(b->format.nSamplesPerSec));
        return DS_OK;
    }

    // ---- 3D buffers (applied at once: DS3D_DEFERRED is treated as DS3D_IMMEDIATE)
    HRESULT Query(Buffer3D *buffer3D, PropertySet **properties) override
    {
        *properties = PartHandle<PropertySet>(Owner(buffer3D)->partProperties);
        return DS_OK;
    }
    ULONG Release(Buffer3D *buffer3D) override { return Owner(buffer3D)->Release(); }
    HRESULT SetMode(Buffer3D *buffer3D, DWORD mode, DWORD) override
    {
        if (mode != DS3DMODE_NORMAL && mode != DS3DMODE_HEADRELATIVE && mode != DS3DMODE_DISABLE)
            return DSERR_INVALIDPARAM;
        S_buffer *b = Owner(buffer3D);
        b->mode = mode;
        Apply3D(b);
        return DS_OK;
    }
    HRESULT SetPosition(Buffer3D *buffer3D, float x, float y, float z, DWORD) override
    {
        S_buffer *b = Owner(buffer3D);
        b->position = {x, y, z};
        Apply3D(b);
        return DS_OK;
    }
    HRESULT SetVelocity(Buffer3D *buffer3D, float x, float y, float z, DWORD) override
    {
        S_buffer *b = Owner(buffer3D);
        b->velocity = {x, y, z};
        Apply3D(b);
        return DS_OK;
    }
    HRESULT SetConeOrientation(Buffer3D *buffer3D, float x, float y, float z, DWORD) override
    {
        SetVector(Owner(buffer3D)->source, AL_DIRECTION, {x, y, z});
        return DS_OK;
    }
    HRESULT SetConeAngles(Buffer3D *buffer3D, DWORD inside, DWORD outside, DWORD) override
    {
        if (inside > 360 || outside > 360 || inside > outside)
            return DSERR_INVALIDPARAM;
        const ALuint source = Owner(buffer3D)->source;
        al.alSourcef(source, AL_CONE_INNER_ANGLE, static_cast<float>(inside));
        al.alSourcef(source, AL_CONE_OUTER_ANGLE, static_cast<float>(outside));
        return DS_OK;
    }
    HRESULT SetConeOutsideVolume(Buffer3D *buffer3D, LONG volume, DWORD) override
    {
        if (volume > DSBVOLUME_MAX || volume < DSBVOLUME_MIN)
            return DSERR_INVALIDPARAM;
        al.alSourcef(Owner(buffer3D)->source, AL_CONE_OUTER_GAIN, Gain(volume));
        return DS_OK;
    }
    HRESULT SetMinDistance(Buffer3D *buffer3D, float distance, DWORD) override
    {
        if (!(distance > 0.0f))
            return DSERR_INVALIDPARAM;
        al.alSourcef(Owner(buffer3D)->source, AL_REFERENCE_DISTANCE, distance);
        return DS_OK;
    }
    HRESULT SetMaxDistance(Buffer3D *buffer3D, float distance, DWORD) override
    {
        if (!(distance > 0.0f))
            return DSERR_INVALIDPARAM;
        al.alSourcef(Owner(buffer3D)->source, AL_MAX_DISTANCE, distance);
        return DS_OK;
    }

    // ---- the listener
    ULONG Release(Listener *listener) override { return Owner(listener)->Release(); }
    HRESULT GetAllParameters(Listener *listener, DS3DLISTENER *params) override
    {
        if (params->dwSize < sizeof(DS3DLISTENER))
            return DSERR_INVALIDPARAM;
        *params = Owner(listener)->system->listener;
        return DS_OK;
    }
    HRESULT SetAllParameters(Listener *listener, const DS3DLISTENER *params, DWORD) override
    {
        if (params->dwSize < sizeof(DS3DLISTENER))
            return DSERR_INVALIDPARAM;
        S_system *s = Owner(listener)->system;
        s->listener = *params;
        s->listener.dwSize = sizeof(DS3DLISTENER);
        ApplyListener(s);
        return DS_OK;
    }
    HRESULT CommitDeferredSettings(Listener *) override { return DS_OK; }

    // ---- property sets (EAX 3, OpenAL Soft's emulation)
    ULONG Release(PropertySet *properties) override { return Owner(properties)->Release(); }
    HRESULT QuerySupport(PropertySet *properties, REFGUID, ULONG, ULONG *support) override
    {
        *support = 0;
        if (!Owner(properties)->system->eax)
            return E_NOTIMPL;
        *support = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return S_OK;
    }
    HRESULT Set(PropertySet *properties, REFGUID set, ULONG id, void *, ULONG, void *data, ULONG size) override
    {
        S_buffer *b = Owner(properties);
        if (!b->system->eax)
            return E_NOTIMPL;
        return al.EAXSet(&set, id, b->source, data, size) == AL_NO_ERROR ? S_OK : E_FAIL;
    }
    HRESULT Get(PropertySet *properties, REFGUID set, ULONG id, void *, ULONG, void *data, ULONG size,
                ULONG *returned) override
    {
        S_buffer *b = Owner(properties);
        if (returned)
            *returned = 0;
        if (!b->system->eax)
            return E_NOTIMPL;
        if (al.EAXGet(&set, id, b->source, data, size) != AL_NO_ERROR)
            return E_FAIL;
        if (returned)
            *returned = size;
        return S_OK;
    }

    // ---- position notifications
    ULONG Release(Notify *notify) override { return Owner(notify)->Release(); }
    HRESULT SetNotificationPositions(Notify *notify, DWORD count, const DSBPOSITIONNOTIFY *positions) override
    {
        S_buffer *b = Owner(notify);
        for (DWORD i = 0; i < count; ++i) {
            if (positions[i].dwOffset != DSBPN_OFFSETSTOP && positions[i].dwOffset >= b->data->size())
                return DSERR_INVALIDPARAM;
        }
        std::lock_guard<std::mutex> guard(b->lock);
        if (b->playing)
            return DSERR_INVALIDCALL;
        b->notifications.assign(positions, positions + count);
        return DS_OK;
    }
};

DeviceOpenAL s_device;

}  // namespace

Device *OpenALDevice() { return LoadLibraryOnce() ? &s_device : nullptr; }

namespace openal {

bool UseLoopback(int rate)
{
    if (!LoadLibraryOnce() || !al.alcLoopbackOpenDeviceSOFT || !al.alcRenderSamplesSOFT)
        return false;
    g_loopbackRate = rate;
    return true;
}

void RenderLoopback(float *out, int frames)
{
    if (g_loopback)
        al.alcRenderSamplesSOFT(g_loopback, out, frames);
}

}  // namespace openal

}  // namespace ls3d::audio
