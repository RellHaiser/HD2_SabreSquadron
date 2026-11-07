# The sound device (audio)

Everything the engine plays goes through one interface, `audio::Device` (`audio::g_pAudio`),
declared in [`src/audio/audio.h`](../src/audio/audio.h). This covers ISND (sources, sounds, 3D
sounds, the listener, EAX, the voice manager), IShow's Ogg Vorbis streaming and the I3D sound
frames. It has two backends:

- **DirectSound** ([`src/audio/dsound`](../src/audio/dsound)), the default: it passes every call to
  DirectSound unchanged, so the DLL makes exactly the DirectSound calls the original made.
- **OpenAL Soft** ([`src/audio/openal`](../src/audio/openal)), chosen in LS3DF.ini with
  `[Sound] OpenAL=1` ([below](#the-openal-soft-backend)): the first step of phase 2 of the
  [cross-platform plan](cross_platform.md).

This is the sound counterpart of the [render device](render_device.md), built the same way and for
the same reason: to make the API replaceable without changing behaviour.

## Design

**The vocabulary stays DirectSound 8's.** The engine describes sound in DirectSound's terms:
buffer descriptions and wave formats, volumes and pans in hundredths of a decibel, play flags and
priorities, 3D modes, distances and cones, the listener's parameters, and EAX 3 reverb as property
sets. A backend has to reproduce that model. The types come from
[`src/dx/dsound_types.h`](../src/dx/dsound_types.h), which on Windows takes them from the SDK's
`dsound.h`. Creative's EAX declarations stay in `src/isnd/eax.h`.

**Objects are opaque handles.** The DirectSound object, buffers, 3D buffers, the listener, EAX
property sets and position notifications are `audio::System`, `Buffer`, `Buffer3D`, `Listener`,
`PropertySet` and `Notify`. These are incomplete types that only a backend looks inside, and
operations on them go through the device (`g_pAudio->Play(buffer, ...)`). DirectSound's
`QueryInterface` becomes one `Query` overload per interface the engine asks for, and like
`QueryInterface` each gives a new reference. The handles sit where the DirectSound pointers were and
have the same size:

| Structure | Members |
|---|---|
| `ISND_driver` | `dsound` (+0x0C), `eax` (+0x10), `eaxBuffer` (+0x14), `primary` (+0x28), `listener` (+0x2C) |
| `ISND_source` | `buffer` (+0x30) |
| `ISND_sound` | `buffer` (+0x08) |
| `ISND_3Dsound` | `buffer3D` (+0x20) |
| `IShow` | `notify` (+0x6C), `buffer` (+0x70) |
| the voice manager's `S_voice`, `S_suspendedVoice` | `buffer` (+0x04) |

The DirectSound backend's handles are the DirectSound interfaces themselves
(`audio::dsound::DS()` / `Handle()` convert them), so these members hold the same values as before.
A buffer handle holds whichever buffer interface the engine had (DirectSound 8's extends the
original); the backend calls only the original's methods on it.

**One escape hatch.** Bink plays its own sound through DirectSound, so the video device's Windows
backend hands it the sound device's own object (`Native()`, through `video::Device::UseSound`; see
[the video device](video_device.md)). A new video backend will play its sound through the sound
device instead.

**The boundary is checked.** `python build.py` (and `check`) fails when code outside `src/dx` and
`src/audio/dsound` includes `dsound.h` or the backend, or names a DirectSound interface or
function, and when code outside `src/audio/openal` includes OpenAL's headers or calls its
functions. Comments may still name them.

## What a new backend has to provide

Everything in `audio::Device`:

- **The device:** enumeration (for the log), creation (the EAX Unified class or the default device),
  capabilities (secondary buffer rate limits and the free hardware 3D buffers the voice manager
  counts), speaker configuration.
- **Buffers:** the primary buffer's format; secondary buffers from PCM data, static (whole files),
  duplicated (several sounds share one source's data) or streamed (IShow's Ogg player refills a
  ring buffer on position notifications). Volume, pan, frequency, play / stop / position and
  status, play priorities and the voice-placement flags.
- **3D:** per-buffer position, velocity, cone, distances and mode (normal, disabled); one
  listener with position, orientation, velocity and distance, Doppler and rolloff factors.
- **EAX 3:** the listener property set (all parameters at once, or single properties) and support
  queries for the listener and buffer sets.

## The OpenAL Soft backend

**Choosing it.** The sound driver calls `audio::ChooseDevice()` ([`src/audio/audio.cpp`](../src/audio/audio.cpp))
before it opens the device. With `[Sound] OpenAL=1` in LS3DF.ini ([ls3df_ini.md](ls3df_ini.md#sound)),
the backend loads OpenAL Soft at run time: `soft_oal.dll`, else `OpenAL32.dll`, through
`platform::OpenLibrary`. If neither is there, DirectSound is used; `log.txt` says which. Nothing is
linked against OpenAL, so the DLL runs without it.

**DirectSound's model on OpenAL:**

- **Buffers.** A buffer is an OpenAL source playing a *callback buffer* (`AL_SOFT_callback_buffer`).
  The backend keeps the buffer's bytes itself, as DirectSound does: the engine locks and writes
  them, also while they play (IShow's ring). The mixer takes them through the callback. That
  callback converts them (8- or 16-bit, mono or stereo), moves the play cursor, wraps at the end
  of a looping buffer or ends a one-shot one, and sets the position notifications the cursor
  passes.
- **The play cursor** is the next byte the mixer takes. It is a few frames ahead of what is heard,
  like DirectSound's.
  - `Stop` keeps the cursor and `Play` goes on from it.
  - A one-shot buffer's end stops it with the cursor back at 0 (so `Play` replays it), and sets the
    stop notification (`DSBPN_OFFSETSTOP`).
  - The buffer reports playing until the mixer has played the last bytes it took.
- **Volume and pan** are DirectSound's hundredths of a decibel: gain = 10^(mB / 2000), and -10000 is
  silence. A 2D buffer is mixed to stereo in the callback with DirectSound's pan (the channel on the
  other side turned down) and goes to the output's front channels unchanged
  (`AL_DIRECT_CHANNELS_SOFT`).
- **The frequency** sets the source's pitch, relative to the buffer's own rate (100–100,000 Hz).
- **3D buffers** are mono sources placed by OpenAL with DirectSound's distance model: inverse
  distance, clamped between the minimum and maximum distances, with the listener's rolloff factor.
  - DirectSound's axes are left-handed, OpenAL's right-handed, so z is negated for positions,
    velocities, cone directions and the listener's orientation.
  - Head-relative mode makes a source relative. With 3D processing off, the source sits on the
    listener at full volume.
  - The cone (angles and outside volume), velocities with the Doppler factor, and the distance factor
    (OpenAL's speed of sound becomes 343.3 / factor) map directly.
  - Deferred 3D settings are applied at once.
- **EAX 3** property sets go to OpenAL Soft's own EAX emulation (`EAXSet` / `EAXGet`, which take EAX
  3's property set GUIDs), so the game's reverb works. With DirectSound on today's Windows it is off,
  because no EAX driver exists. The EAX Unified class (`CreateInstance`) is accepted for this.
- **The device:** `Enumerate` lists OpenAL's outputs, under GUIDs made from their place in the list.
  `Create` / `Initialize` open the default output, or the enumerated one. Each buffer holds a
  source, so the context asks for 4,096 mono and 4,096 stereo sources.
- **Not emulated:**
  - `GetCaps` reports no hardware buffers, as DirectSound does on Windows Vista and later, so the
    voice manager stays off there as well.
  - `SetSpeakerConfig` is accepted and ignored: the output follows the system and OpenAL Soft's own
    settings (`alsoft.ini`).
  - Priorities and voice-placement flags mean nothing to OpenAL.
  - `Native` has no DirectSound object to give, so Bink opens its own DirectSound for a movie's
    sound. The video seam's next backend (FFmpeg) plays it through the sound device instead.

**Building.** `build.py` needs OpenAL Soft's headers (1.23 or later, 32-bit) in `%OPENAL_DIR%`, by
default `deps\openal-soft-x86` next to the checkout. It copies the library next to the DLL and
the tests as `soft_oal.dll`. To build it from source with Visual Studio's CMake, run in an x86
developer prompt:

```bat
git clone --depth 1 --branch 1.24.3 https://github.com/kcat/openal-soft.git deps\openal-soft
cmake -S deps\openal-soft -B deps\openal-soft\build-x86 -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
      -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF ^
      -DCMAKE_INSTALL_PREFIX=deps\openal-soft-x86
cmake --build deps\openal-soft\build-x86
cmake --install deps\openal-soft\build-x86
```

(The static C runtime spares the game folder Visual C++'s runtime DLLs.) A binary release from
openal-soft.org works too: point `OPENAL_DIR` at it, and `build.py` takes its `bin\Win32\soft_oal.dll`.

## Tests

- `compare_isnd` compiles the backend (`// test-sources: src/audio/dsound/audio_dsound.cpp`) and
  puts its fake DirectSound objects into the drivers' members as handles. It still compares every
  DirectSound call, its arguments, the return values, the objects' fields and the voice manager's
  lists against the original: 100,000 rounds identical on each of four seeds (1 to 4). A deliberately
  broken backend (`SetPan` calling `SetVolume`) shows 21 differences in 20,000 rounds.
- `compare_texture` compiles IShow, so it compiles the backend too.
- The rest of the suite gives the same results as before the change.
- Since the video seam, `compare_show` compares IShow's Ogg streaming on fake DirectSound buffers:
  the calls, the buffers' bytes and the stream thread's refills.
- `test_audio_openal` covers the OpenAL Soft backend. The original only ever used DirectSound, so
  there is nothing to compare with. Instead, the backend renders through OpenAL Soft's loopback
  device (stereo float, 44.1 kHz, no HRTF or limiter), and its output and state are checked against
  what DirectSound does with the same calls:
  - volume and pan, 8- and 16-bit, mono and stereo, and silence at -10000;
  - the cursor following the mix;
  - IShow's notifications, each set exactly when the cursor passes it, over two laps;
  - locks across the end;
  - a one-shot buffer's end and replay, Stop keeping the cursor, seeking while playing, half the
    frequency, a duplicate sharing the bytes;
  - distance (1 / d), the maximum distance, rolloff, left / right, 3D off, the cone's -20 dB;
  - the EAX listener properties kept, and the reverb tail present only with the room on.

  Result: 66 checks pass, and 20 deliberately introduced bugs in the backend are all caught:
  - 6 in volume, pan and conversion: the gain formula, the pan side, 8-bit's offset, mono in one
    channel, a duplicate's volume, the looping flag;
  - 6 in the cursor and notifications: one at the cursor itself, the end not resetting it or not
    notifying, Stop resetting it, seeking ignored, locks not wrapping;
  - 1 in the frequency (the pitch inverted);
  - 6 in 3D: z not negated, x mirrored, the distance model, rolloff, 3D off not relative, the cone's
    outside volume;
  - 1 in EAX (its calls dropped).

## Not covered

Like before, device creation, EAX detection and enumeration need real hardware and are not
compared. The OpenAL backend has been tried in the game (November 2026) and works; beyond that
session, only the loopback test checks it.
