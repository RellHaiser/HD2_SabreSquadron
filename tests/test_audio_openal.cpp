// Test of the sound device's OpenAL Soft backend (src/audio/openal): the original only ever used
// DirectSound, so there is nothing to compare it with. Instead the backend renders through OpenAL
// Soft's loopback device (stereo float, 44.1 kHz, no HRTF or limiter), and its output and state
// are checked against what DirectSound 8 does with the same calls:
// - volume and pan in hundredths of a decibel, 8- and 16-bit, mono and stereo;
// - the play cursor (bytes the mixer took), position notifications as IShow's Ogg stream sets
//   them (every event set once a lap, in order, when the cursor passes it), locks across the
//   buffer's end, a one-shot buffer's end (stopped, cursor 0, the stop notification), Stop keeping
//   the cursor, SetCurrentPosition while playing, the frequency, duplicates sharing the bytes;
// - 3D: distance (inverse, clamped by the minimum and maximum distances, the listener's rolloff),
//   left / right (DirectSound's left-handed axes), 3D processing turned off, the cone;
// - EAX 3 listener properties through OpenAL Soft's emulation, and its reverb tail.
//
// Usage: test_audio_openal.exe (soft_oal.dll next to it: build.py copies it)
//
// test-sources: src/audio/openal/audio_openal.cpp src/platform/win32/platform_win32.cpp
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/audio/backends.h"
#include "../src/audio/openal/openal_device.h"
#include "../src/isnd/eax.h"

using namespace ls3d::audio;

namespace {

const int kRate = 44100;
Device *dev;
int g_checks, g_failures;

void Check(bool ok, const char *fmt, ...)
{
    ++g_checks;
    if (ok)
        return;
    ++g_failures;
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    std::printf("FAIL: %s\n", text);
}

bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

std::vector<float> Render(int frames)
{
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    openal::RenderLoopback(out.data(), frames);
    return out;
}

// Mean of the left and right channels and of their power over the last `frames` frames.
struct S_level {
    double left, right, power;
};
S_level Level(const std::vector<float> &v, int frames)
{
    S_level l = {};
    const size_t n = v.size() / 2;
    for (size_t i = n - static_cast<size_t>(frames); i < n; ++i) {
        l.left += v[2 * i];
        l.right += v[2 * i + 1];
        l.power += double(v[2 * i]) * v[2 * i] + double(v[2 * i + 1]) * v[2 * i + 1];
    }
    l.left /= frames;
    l.right /= frames;
    l.power /= frames;
    return l;
}

WAVEFORMATEX Format(int channels, int bits, int rate = kRate)
{
    WAVEFORMATEX f = {};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = static_cast<WORD>(channels);
    f.wBitsPerSample = static_cast<WORD>(bits);
    f.nSamplesPerSec = static_cast<DWORD>(rate);
    f.nBlockAlign = static_cast<WORD>(channels * bits / 8);
    f.nAvgBytesPerSec = f.nBlockAlign * f.nSamplesPerSec;
    return f;
}

Buffer *NewBuffer(System *system, DWORD flags, int channels, int bits, DWORD bytes)
{
    WAVEFORMATEX f = Format(channels, bits);
    DSBUFFERDESC desc = {};
    desc.dwSize = sizeof desc;
    desc.dwFlags = flags;
    desc.dwBufferBytes = bytes;
    desc.lpwfxFormat = &f;
    Buffer *b = nullptr;
    const HRESULT hr = dev->CreateSoundBuffer(system, &desc, &b);
    Check(hr == DS_OK && b, "CreateSoundBuffer(%08lx, %d ch, %d bits, %lu bytes): %08lx", flags, channels, bits, bytes,
          hr);
    return b;
}

// 16-bit samples written through Lock.
void Write16(Buffer *b, const std::vector<int16_t> &samples)
{
    void *p1 = nullptr;
    DWORD s1 = 0;
    Check(dev->Lock(b, 0, 0, &p1, &s1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER) == DS_OK, "Lock(entire)");
    std::memcpy(p1, samples.data(), s1 < samples.size() * 2 ? s1 : samples.size() * 2);
    dev->Unlock(b, p1, s1, nullptr, 0);
}

DWORD Cursor(Buffer *b)
{
    DWORD play = ~0u, write = ~0u;
    dev->GetCurrentPosition(b, &play, &write);
    return play;
}

void TestVolumeAndPan(System *system)
{
    // 16-bit mono at 0.5: volume -600 (0.501), pan +1000 (left at 0.316).
    Buffer *b = NewBuffer(system, DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN, 1, 16, kRate * 2);
    Write16(b, std::vector<int16_t>(kRate, 16384));
    dev->SetVolume(b, -600);
    dev->SetPan(b, 1000);
    dev->Play(b, 0, 0, DSBPLAY_LOOPING);
    const S_level l = Level(Render(8192), 2048);
    const double g = std::pow(10.0, -600 / 2000.0), pan = std::pow(10.0, -1000 / 2000.0);
    Check(Near(l.left, 0.5 * g * pan, 1e-4) && Near(l.right, 0.5 * g, 1e-4),
          "mono volume -600 pan +1000: %.5f %.5f (expected %.5f %.5f)", l.left, l.right, 0.5 * g * pan, 0.5 * g);
    DWORD status = 0;
    dev->GetStatus(b, &status);
    Check(status == (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING), "status while looping: %lx", status);
    dev->Stop(b);
    dev->Release(b);

    // 8-bit stereo, left 0.5 and right -0.25; pan -2000 (right at 0.1).
    Buffer *s = NewBuffer(system, DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN, 2, 8, 8820);
    void *p1;
    DWORD s1;
    dev->Lock(s, 0, 0, &p1, &s1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER);
    for (DWORD i = 0; i < s1; i += 2) {
        static_cast<uint8_t *>(p1)[i] = 0x80 + 64;
        static_cast<uint8_t *>(p1)[i + 1] = 0x80 - 32;
    }
    dev->Unlock(s, p1, s1, nullptr, 0);
    dev->SetPan(s, -2000);
    dev->Play(s, 0, 0, DSBPLAY_LOOPING);
    const S_level m = Level(Render(8192), 2048);
    Check(Near(m.left, 0.5, 1e-4) && Near(m.right, -0.025, 1e-4), "8-bit stereo pan -2000: %.5f %.5f", m.left,
          m.right);
    // Silence: the minimum volume.
    dev->SetVolume(s, DSBVOLUME_MIN);
    const S_level z = Level(Render(8192), 2048);
    Check(z.power < 1e-12, "volume -10000 is silent: power %g", z.power);
    dev->Stop(s);
    dev->Release(s);
}

// IShow's Ogg stream: a looping 0x84000-byte ring with a notification every 0xC000 bytes.
void TestNotifications(System *system)
{
    const DWORD size = 0x84000, block = 0xC000;
    Buffer *b = NewBuffer(system, DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_CTRLVOLUME, 2, 16,
                          size);
    Notify *notify = nullptr;
    Check(dev->Query(b, &notify) == DS_OK && notify, "Query(notify)");
    // IShow's 11 offsets, and one where a mix of 1024 frames ends: not passed until the next byte is
    // taken. (The mixer takes a few frames more than it mixes, the same number every time: measured
    // first.)
    dev->Play(b, 0, 0, DSBPLAY_LOOPING);
    Render(1024);
    const DWORD lead = Cursor(b) - 1024 * 4;
    dev->Stop(b);
    dev->SetCurrentPosition(b, 0);
    const int count = 12;
    DSBPOSITIONNOTIFY positions[count];
    HANDLE events[count];
    for (int i = 0; i < count; ++i) {
        events[i] = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        positions[i].dwOffset = i < 11 ? block * static_cast<DWORD>(i + 1) - 1 : 64 * 1024 * 4 + lead;
        positions[i].hEventNotify = events[i];
    }
    Check(dev->SetNotificationPositions(notify, count, positions) == DS_OK, "SetNotificationPositions");
    dev->Play(b, 0, 0, DSBPLAY_LOOPING);
    int fired = 0, wrong = 0;
    uint64_t advanced = 0;
    DWORD before = Cursor(b);
    const int steps = 300, chunk = 1024;
    for (int step = 0; step < steps; ++step) {
        Render(chunk);
        const DWORD after = Cursor(b);
        advanced += (after + size - before) % size;
        for (int i = 0; i < count; ++i) {
            const DWORD o = positions[i].dwOffset;
            const bool expected = before <= after ? (o >= before && o < after) : (o >= before || o < after);
            const bool set = WaitForSingleObject(events[i], 0) == WAIT_OBJECT_0;
            fired += set;
            wrong += set != expected;
        }
        before = after;
    }
    Check(!wrong, "notifications: %d set when the cursor did not pass them (or not set when it did)", wrong);
    Check(fired >= 22, "notifications over two laps: %d", fired);
    // The cursor moves with the mix (it may be ahead by what the mixer took in advance).
    const double expected = double(steps) * chunk * 4;
    Check(Near(double(advanced), expected, 4.0 * 4096), "cursor advanced %llu bytes for %.0f mixed",
          static_cast<unsigned long long>(advanced), expected);

    // Locks across the end.
    void *p1 = nullptr, *p2 = nullptr;
    DWORD s1 = 0, s2 = 0;
    Check(dev->Lock(b, size - 100, 300, &p1, &s1, &p2, &s2, 0) == DS_OK && s1 == 100 && s2 == 200 && p2, "Lock wraps: %lu + %lu",
          s1, s2);
    dev->Unlock(b, p1, s1, p2, s2);
    Check(dev->Lock(b, size, 4, &p1, &s1, &p2, &s2, 0) == DSERR_INVALIDPARAM, "Lock past the end refused");
    dev->Stop(b);
    dev->Release(notify);
    Check(dev->Release(b) == 0, "the last reference frees the buffer");
    for (HANDLE e : events)
        CloseHandle(e);
}

// A one-shot buffer, Stop and Play, SetCurrentPosition, the frequency, a duplicate.
void TestPlayback(System *system)
{
    // One-shot: 0.1 s at 0.25, then silence; stopped at the end with the cursor at 0.
    Buffer *b = NewBuffer(system, DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_CTRLVOLUME, 1, 16, 8820);
    Write16(b, std::vector<int16_t>(4410, 8192));
    Notify *notify;
    dev->Query(b, &notify);
    HANDLE stopped = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    DSBPOSITIONNOTIFY stop = {DSBPN_OFFSETSTOP, stopped};
    dev->SetNotificationPositions(notify, 1, &stop);
    dev->Play(b, 0, 0, 0);
    const std::vector<float> out = Render(13230);
    const S_level during = Level(std::vector<float>(out.begin(), out.begin() + 2 * 3000), 1000);
    const S_level after = Level(out, 4410);
    Check(Near(during.left, 0.25, 1e-4) && Near(during.right, 0.25, 1e-4), "one-shot plays: %.5f", during.left);
    Check(after.power < 1e-12, "one-shot ends: power %g", after.power);
    DWORD status = 1;
    dev->GetStatus(b, &status);
    Check(status == 0, "one-shot status at the end: %lx", status);
    Check(Cursor(b) == 0, "one-shot cursor at the end: %lu", Cursor(b));
    Check(WaitForSingleObject(stopped, 0) == WAIT_OBJECT_0, "the stop notification at the end");
    // Played again from the start (DirectSound's way to replay a sound).
    dev->Play(b, 0, 0, 0);
    Check(Near(Level(Render(2000), 1000).left, 0.25, 1e-4), "one-shot replays");
    dev->Stop(b);
    Check(WaitForSingleObject(stopped, 0) == WAIT_OBJECT_0, "the stop notification on Stop");
    dev->Release(notify);
    dev->Release(b);
    CloseHandle(stopped);

    // A ramp: sample i is i / frames (16-bit), so the output tells where the cursor is.
    const int frames = kRate;
    Buffer *r = NewBuffer(system, DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY, 1, 16, frames * 2);
    std::vector<int16_t> ramp(frames);
    for (int i = 0; i < frames; ++i)
        ramp[static_cast<size_t>(i)] = static_cast<int16_t>(i * 32767 / frames);
    Write16(r, ramp);
    dev->Play(r, 0, 0, DSBPLAY_LOOPING);
    Render(4096);
    const DWORD kept = Cursor(r);
    dev->Stop(r);
    const S_level silent = Level(Render(4096), 2048);
    Check(kept > 4096 * 2 && silent.power < 1e-12 && Cursor(r) == kept, "Stop: silent, cursor kept at %lu (%lu)", kept,
          Cursor(r));
    dev->Play(r, 0, 0, DSBPLAY_LOOPING);
    const std::vector<float> resumed = Render(1024);
    const double expectedResume = double(kept / 2 + 512) / frames;
    Check(Near(resumed[2 * 512], expectedResume, 2e-3), "Play resumes at the cursor: %.4f (expected %.4f)", resumed[2 * 512],
          expectedResume);
    // SetCurrentPosition while playing takes effect at once.
    dev->SetCurrentPosition(r, 20000 * 2);
    const std::vector<float> moved = Render(1024);
    const double expectedMoved = double(20000 + 512) / frames;
    Check(Near(moved[2 * 512], expectedMoved, 2e-3), "SetCurrentPosition while playing: %.4f (expected %.4f)", moved[2 * 512],
          expectedMoved);
    // Half the frequency: the cursor moves half as fast.
    dev->SetCurrentPosition(r, 0);
    dev->SetFrequency(r, kRate / 2);
    const DWORD from = Cursor(r);
    Render(20000);
    const double half = double(Cursor(r) - from) / 2;
    Check(Near(half, 10000, 1500), "frequency 22050: %.0f frames taken for 20000 mixed", half);
    Check(dev->SetFrequency(r, 50) == DSERR_INVALIDPARAM, "a frequency under the minimum is refused");
    dev->Stop(r);

    // A duplicate shares the bytes and starts with the original's volume.
    dev->SetVolume(r, -2000);
    Buffer *d = nullptr;
    Check(dev->DuplicateSoundBuffer(system, r, &d) == DS_OK && d, "DuplicateSoundBuffer");
    Write16(r, std::vector<int16_t>(frames, 16384));
    dev->Play(d, 0, 0, DSBPLAY_LOOPING);
    const S_level dup = Level(Render(8192), 2048);
    Check(Near(dup.left, 0.05, 1e-4), "duplicate: the original's bytes and volume: %.5f", dup.left);
    dev->Stop(d);
    dev->Release(d);
    dev->Release(r);
}

// A 3D buffer at 0.5 (mono); the listener at the origin facing +z (DirectSound's axes).
void Test3D(System *system, Listener *listener)
{
    Buffer *b = NewBuffer(system, DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME, 1, 16, kRate * 2);
    Write16(b, std::vector<int16_t>(kRate, 16384));
    Buffer3D *b3 = nullptr;
    Check(dev->Query(b, &b3) == DS_OK && b3, "Query(3D)");
    dev->Play(b, 0, 0, DSBPLAY_LOOPING);
    auto power = [&](float x, float y, float z) {
        dev->SetPosition(b3, x, y, z, DS3D_IMMEDIATE);
        return Level(Render(8192), 2048);
    };
    const double p1 = power(0, 0, 1).power, p2 = power(0, 0, 2).power, p4 = power(0, 0, 4).power;
    Check(Near(std::sqrt(p2 / p4), 2.0, 0.02) && Near(std::sqrt(p1 / p2), 2.0, 0.02),
          "distance: amplitude ratios %.4f (1 / 2 m) %.4f (2 / 4 m), expected 2", std::sqrt(p1 / p2), std::sqrt(p2 / p4));
    const S_level right = power(3, 0, 0), left = power(-3, 0, 0);
    Check(right.right > 2 * std::fabs(right.left) && left.left > 2 * std::fabs(left.right),
          "+x is right (%.4f %.4f), -x left (%.4f %.4f)", right.left, right.right, left.left, left.right);
    // The maximum distance: no quieter beyond it.
    dev->SetMaxDistance(b3, 3.0f, DS3D_IMMEDIATE);
    const double p3 = power(0, 0, 3).power, p10 = power(0, 0, 10).power;
    Check(Near(p10 / p3, 1.0, 0.01), "max distance 3: %.4f", p10 / p3);
    dev->SetMaxDistance(b3, DS3D_DEFAULTMAXDISTANCE, DS3D_IMMEDIATE);
    // The listener's rolloff factor 2: 1 / (1 + 2 (d - 1)).
    DS3DLISTENER params = {};
    params.dwSize = sizeof params;
    dev->GetAllParameters(listener, &params);
    params.flRolloffFactor = 2.0f;
    dev->SetAllParameters(listener, &params, DS3D_IMMEDIATE);
    const double r2 = power(0, 0, 2).power;
    Check(Near(std::sqrt(p2 / r2), 1.5, 0.02), "rolloff 2 at 2 m: %.4f quieter, expected 1.5", std::sqrt(p2 / r2));
    params.flRolloffFactor = 1.0f;
    dev->SetAllParameters(listener, &params, DS3D_IMMEDIATE);
    // 3D processing off: as close as the minimum distance, wherever the buffer and the listener are.
    dev->SetMode(b3, DS3DMODE_DISABLE, DS3D_IMMEDIATE);
    params.vPosition = {30.0f, 0.0f, -20.0f};
    dev->SetAllParameters(listener, &params, DS3D_IMMEDIATE);
    const double off = power(0, 0, 50).power;
    Check(Near(off / p1, 1.0, 0.02), "3D off at 50 m, the listener elsewhere: %.4f of the level at 1 m", off / p1);
    params.vPosition = {0.0f, 0.0f, 0.0f};
    dev->SetAllParameters(listener, &params, DS3D_IMMEDIATE);
    dev->SetMode(b3, DS3DMODE_NORMAL, DS3D_IMMEDIATE);
    // The cone, pointing away from the listener: outside it (180 degrees off the axis), -20 dB.
    dev->SetConeOrientation(b3, 0, 0, 1, DS3D_IMMEDIATE);
    dev->SetConeAngles(b3, 90, 180, DS3D_IMMEDIATE);
    dev->SetConeOutsideVolume(b3, -2000, DS3D_IMMEDIATE);
    const double cone = power(0, 0, 2).power;
    Check(Near(std::sqrt(p2 / cone), 10.0, 0.2), "outside the cone: %.3f quieter, expected 10", std::sqrt(p2 / cone));
    dev->Stop(b);
    dev->Release(b3);
    dev->Release(b);

    WAVEFORMATEX f = Format(2, 16);
    DSBUFFERDESC desc = {sizeof desc, DSBCAPS_CTRL3D, 4000, 0, &f, {}};
    Buffer *stereo3D = nullptr;
    Check(dev->CreateSoundBuffer(system, &desc, &stereo3D) == DSERR_INVALIDPARAM, "a stereo 3D buffer is refused");
}

// EAX 3: the listener's properties through a 3D buffer's property set, and the reverb.
void TestEax(System *system)
{
    Buffer *b = NewBuffer(system, DSBCAPS_CTRL3D | DSBCAPS_STATIC | DSBCAPS_LOCHARDWARE, 1, 16, 0x40);
    Buffer3D *b3 = nullptr;
    PropertySet *eax = nullptr;
    dev->Query(b, &b3);
    Check(dev->Query(b3, &eax) == DS_OK && eax, "Query(property set)");
    ULONG support = 0;
    const HRESULT hr = dev->QuerySupport(eax, ls3d::snd::kEaxListenerProperties, ls3d::snd::kEaxAllParameters, &support);
    Check(SUCCEEDED(hr) && support == (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET), "EAX 3 supported: %08lx %lx", hr,
          support);
    ls3d::snd::S_eaxListenerProperties props = {};
    ULONG returned = 0;
    Check(dev->Get(eax, ls3d::snd::kEaxListenerProperties, ls3d::snd::kEaxAllParameters, nullptr, 0, &props, sizeof props,
                   &returned) == S_OK && returned == sizeof props,
          "EAX listener properties read");

    // A burst, then what follows it: a reverb tail with the room at 0 dB, none at -100 dB.
    auto tail = [&](long room) {
        props.room = room;
        props.decayTime = 1.5f;
        dev->Set(eax, ls3d::snd::kEaxListenerProperties, ls3d::snd::kEaxAllParameters, nullptr, 0, &props, sizeof props);
        Buffer *s = NewBuffer(system, DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME, 1, 16, 4410 * 2);
        std::vector<int16_t> burst(4410);
        uint32_t seed = 1;
        for (int16_t &v : burst)
            v = static_cast<int16_t>((seed = seed * 1664525u + 1013904223u) >> 17) - 16384;
        Write16(s, burst);
        Buffer3D *s3;
        dev->Query(s, &s3);
        dev->SetPosition(s3, 0, 0, 1, DS3D_IMMEDIATE);
        dev->Play(s, 0, 0, 0);
        const S_level after = Level(Render(kRate / 2), kRate / 4);  // 0.25 - 0.5 s
        dev->Release(s3);
        dev->Release(s);
        return after.power;
    };
    const double dry = tail(-10000), wet = tail(0);
    ls3d::snd::S_eaxListenerProperties back = {};
    dev->Get(eax, ls3d::snd::kEaxListenerProperties, ls3d::snd::kEaxAllParameters, nullptr, 0, &back, sizeof back, &returned);
    Check(back.room == 0 && Near(back.decayTime, 1.5, 1e-6), "EAX listener properties kept: room %ld decay %.3f", back.room,
          back.decayTime);
    Check(dry < 1e-10 && wet > 1e-6, "reverb tail: %g without, %g with the room", dry, wet);
    dev->Release(eax);
    dev->Release(b3);
    dev->Release(b);
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!openal::UseLoopback(kRate)) {
        std::printf("no OpenAL Soft with a loopback device (soft_oal.dll next to the test)\n");
        return 2;
    }
    dev = OpenALDevice();

    int devices = 0;
    dev->Enumerate([](LPGUID, LPCSTR, LPCSTR, LPVOID context) -> BOOL {
        ++*static_cast<int *>(context);
        return TRUE;
    }, &devices);
    Check(devices >= 1, "Enumerate: %d", devices);

    // The sound driver's start-up: EAX Unified's class, then the primary buffer and its listener.
    static const GUID kEaxDirectSound8 = {0xca503b60, 0xb176, 0x11d4, {0xa0, 0x94, 0xd0, 0xc0, 0xbf, 0x3a, 0x56, 0x0c}};
    System *system = nullptr;
    Check(dev->CreateInstance(kEaxDirectSound8, &system) == DS_OK && system, "CreateInstance(EAX DirectSound 8)");
    Check(dev->Initialize(system, nullptr) == DS_OK, "Initialize");
    DSCAPS caps = {};
    caps.dwSize = sizeof caps;
    dev->GetCaps(system, &caps);
    Check(caps.dwMinSecondarySampleRate == 100 && caps.dwMaxSecondarySampleRate == 100000 && caps.dwFreeHw3DAllBuffers == 0,
          "GetCaps: %lu - %lu Hz, %lu 3D buffers", caps.dwMinSecondarySampleRate, caps.dwMaxSecondarySampleRate,
          caps.dwFreeHw3DAllBuffers);
    Check(dev->SetCooperativeLevel(system, nullptr, DSSCL_PRIORITY) == DS_OK, "SetCooperativeLevel");
    DSBUFFERDESC desc = {};
    desc.dwSize = sizeof desc;
    desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    Buffer *primary = nullptr;
    Check(dev->CreateSoundBuffer(system, &desc, &primary) == DS_OK && primary, "primary buffer");
    WAVEFORMATEX format = Format(2, 16, 22050);
    Check(dev->SetFormat(primary, &format) == DS_OK, "SetFormat(primary)");
    Listener *listener = nullptr;
    Check(dev->Query(primary, &listener) == DS_OK && listener, "Query(listener)");
    DS3DLISTENER params = {};
    params.dwSize = sizeof params;
    dev->GetAllParameters(listener, &params);
    Check(params.vOrientFront.z == 1.0f && params.vOrientTop.y == 1.0f && params.flRolloffFactor == 1.0f,
          "listener defaults");
    Check(dev->SetSpeakerConfig(system, DSSPEAKER_STEREO) == DS_OK, "SetSpeakerConfig");

    TestVolumeAndPan(system);
    TestNotifications(system);
    TestPlayback(system);
    Test3D(system, listener);
    TestEax(system);

    dev->Release(listener);
    dev->Release(primary);
    Check(dev->Release(system) == 0, "the device goes with its last reference");
    std::printf("test_audio_openal: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
