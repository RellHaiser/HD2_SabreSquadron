# The video device (video)

IShow plays its videos through one interface, `video::Device` (`video::g_pVideo`), declared in
[`src/video/video.h`](../src/video/video.h). It has two backends:

- **Windows** ([`src/video/win32`](../src/video/win32)), the default: IShow's Bink and DirectShow
  code, moved there unchanged, with the texture renderer filter. The DLL therefore makes exactly
  the Bink and DirectShow calls the original made.
- **FFmpeg** ([`src/video/ffmpeg`](../src/video/ffmpeg)), chosen in LS3DF.ini with
  `[Video] FFmpeg=1` ([below](#the-ffmpeg-backend)): part of phase 2 of the
  [cross-platform plan](cross_platform.md).

This is the last of the device seams (after [render](render_device.md), [sound](sound_device.md),
[input](input_device.md) and the [platform](platform.md)).

## Design

**Two kinds of player.** IShow drives its videos in two different ways, and the device keeps
both:

- **Movies** (`.bik`, the game's videos) are pulled. IShow steps through the frames itself: it
  asks whether the next frame is due, decodes it, copies it into its tiles and moves on. The
  vocabulary is Bink's: frame numbers from 1, the frame rate as rate / divisor, a frame copy's
  destination formats and its "copy everything" flag, volumes from 0 to 32,768. A decoder that
  can be stepped (FFmpeg) fits this model.
- **Graphs** (`.avi` and anything else) run by themselves, DirectShow's way. The device creates,
  runs, pauses, stops and seeks a graph, sets its volume (hundredths of a decibel), reports its
  events (complete, user abort, paused) and the picture size. An `.avi`'s frames are pushed into
  IShow's tiles; other files are shown by the player's own renderers.

Ogg Vorbis music is not part of the device: vorbisfile is portable, and the stream already plays
through the sound device.

**Frames come in through a sink.** The texture renderer used to call into IShow. IShow cannot
implement a backend's interface (its virtual table is the game's ABI), so the device takes a
`video::FrameSink` instead. IShow's adapter (in `ishow.cpp`) answers what the original renderer
asked IShow:

| `FrameSink` | IShow's answer (the original renderer's code) |
|---|---|
| `FrameFormat` | 32-bit with `ISHOW_TRUECOLOR` and a true-colour texture format, else the 16-bit format's layout (565 / 555) |
| `SetSize` | creates the tiles and returns their texture format |
| `Frame` | copies the bottom-up frame into the tiles and counts it (`GetFrameCount`) |

A graph owns the sinks it was given and deletes them with itself.

**Objects are opaque handles** (`video::Movie`, `video::Graph`), operated on through the device.
They sit where IShow kept the API pointers:

| IShow member | Before | Now |
|---|---|---|
| `graph` (+0x04) | `IGraphBuilder` | `video::Graph` (the backend's record of the graph and its interfaces) |
| +0x08, +0x0C, +0x14, +0x18 | `IMediaControl`, `IMediaSeeking`, `IBasicAudio`, `IMediaEvent` | unused, in the graph record |
| `movie` (+0x1C) | `BINK` | `video::Movie` (the `BINK` with the Windows backend) |
| +0x50 | the texture renderer | unused, in the graph record |
| `thread` (+0x64), `event` (+0x68) | thread and event handles | `platform::Thread`, `platform::Event` |

The game reads IShow only through its virtual table and the tiles, so the unused members keep
their places and the object its size (0x8C).

**One escape hatch remains.** Bink plays a movie's sound itself, through DirectSound, so
`UseSound` hands it the sound device's own object (`audio::Device::Native`). The two Windows
backends meet there. The FFmpeg backend plays the sound through the sound device instead. Graphs
get the sound device through `UseGraphSound`, which DirectShow, with a sound renderer of its own,
does not need.

**The platform got events.** The Ogg stream thread waits for the sound buffer's position
notifications. `platform::Event` (`NewEvent`, `SetEvent`, `WaitEvent`, `DeleteEvent`) replaces
IShow's Win32 event, and `NativeEvent` gives DirectSound the handle it sets. IShow's threads,
sleeps and events all go through the platform now.

**The boundary is checked.** `python build.py` (and `check`) fails when code outside
`src/video/win32`, `src/dshow`, `src/dx` and `src/external` includes `dshow.h`, Bink or the backend,
calls a Bink function or names the DirectShow interfaces. The platform check no longer exempts
IShow; only the DirectShow base classes keep their Win32 calls, as part of the Windows backend.

## What a new backend has to provide

Everything in `video::Device`:

- **Movies:** open and close, the size, frame count, current frame and rate; pause, seek to a
  frame, "is the next frame due", decode, advance; copy a rectangle of the decoded frame in RGB32,
  RGB565 or RGB555 (all of it, or what changed); the volume; the sound device to play the sound on.
- **Graphs:** create and release; render a file with the player's own output, or with its video
  into a `FrameSink` (in the sink's pixel layout, bottom-up rows) and its sound to the sound card;
  run, pause, stop; seek and report the position (100 ns units); the volume; the next event; the
  size of the picture rendered into the sink.

## The FFmpeg backend

**Choosing it.** IShow calls `video::ChooseDevice()` ([`src/video/video.cpp`](../src/video/video.cpp))
on its first `Open`. With `[Video] FFmpeg=1` in LS3DF.ini ([ls3df_ini.md](ls3df_ini.md#video)),
the backend loads FFmpeg's libraries at run time (`avutil-59.dll`, `avcodec-61.dll`,
`avformat-61.dll`, `swscale-8.dll`, `swresample-5.dll`, through `platform::OpenLibrary`). If they are
missing, Bink and DirectShow stay; `log.txt` says which. The choice is made once, so every movie
and graph has one backend.

**Movies keep Bink's model:**

- **The clock.** IShow asks whether the next frame is due. The answer comes from the wall clock,
  counting from the frame the movie was started or sought at, at the movie's rate (rate /
  divisor, from the file), and standing still while the movie is paused. The clock and the sound
  start at the first frame decoded after the open or a seek: a movie opened long before `Play`
  starts in step, and one IShow stopped (it seeks back to frame 1) stays silent.
- **Pictures.** Frames are decoded in order, and a seek goes to the keyframe before the frame and
  decodes up to it. FFmpeg's Bink decoder gives YUV 4:2:0. Each picture is converted once per
  format asked for (RGB32 as BGRA, RGB565, RGB555) with swscale, and rectangles are copied out of
  it, as IShow copies its tiles.
- **Sound** plays through the sound device (DirectSound or OpenAL Soft), not Bink's own
  DirectSound.
  - It is a looping 16-bit buffer. The backend keeps it about a second ahead of its play cursor
    whenever IShow asks about the movie, which it does every frame it draws.
  - The rest of the ring is kept silent. A watchdog thread stops the buffer once everything written
    has played, so a movie IShow stops asking about falls silent, as Bink's did, instead of
    replaying old sound.
  - Bink's 0–32,768 volume becomes 2000 · log10(v / 32768) hundredths of a decibel.

**Graphs keep DirectShow's model:**

- **States and events.** A graph is stopped, paused or running, has a position (100 ns), and queues
  events for `NextEvent`: paused on `Pause`, complete at the end.
- **Frames.** A thread decodes the file. While the graph runs, it hands each frame to the sink when
  its time comes, in bottom-up rows of DWORD-aligned pitch as the texture renderer got them, and
  keeps the sound's buffer filled. A seek goes to the keyframe before and drops what lies before
  the position.
- **What it does not do:**
  - A file rendered without a sink (`RenderFile`) plays its sound only: there is no window for its
    pictures.
  - Only the last file rendered into a graph plays. DirectShow kept earlier ones connected; IShow
    renders one file per graph unless one fails.

**The libraries.** `build.py` needs FFmpeg's headers in `%FFMPEG_DIR%`, by default `deps\ffmpeg-x86`
next to the checkout. It copies the five DLLs next to the DLL and the tests. They are a 32-bit
shared build of FFmpeg 7.1.1, licensed LGPL 2.1, with only what the game's files need:

- the Bink, AVI and WAV demuxers;
- the Bink video and audio decoders, raw video, PCM, MS and IMA ADPCM, MS Video 1 and Cinepak;
- the file protocol, swscale and swresample.

They depend only on Windows' own DLLs. To build them, first clone FFmpeg:

```sh
git clone --depth 1 --branch n7.1.1 https://github.com/FFmpeg/FFmpeg.git ffmpeg
```

Configure and build it with a 32-bit MinGW (gcc and make), from a POSIX shell such as Git's bash.
Do this in the source folder, on a path without spaces:

```sh
./configure --prefix=<install> --arch=x86 --target-os=mingw32 --cc=i686-w64-mingw32-gcc \
  --enable-shared --disable-static --disable-programs --disable-doc --disable-network \
  --disable-autodetect --disable-x86asm --disable-avdevice --disable-avfilter --disable-postproc \
  --disable-everything --enable-w32threads --enable-protocol=file --enable-demuxer=bink,avi,wav \
  --enable-decoder=bink,binkaudio_rdft,binkaudio_dct,rawvideo,pcm_s16le,pcm_u8,adpcm_ms,adpcm_ima_wav,msvideo1,cinepak \
  --enable-swscale --enable-swresample --extra-ldflags=-static-libgcc \
  --extra-libs="-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic"
mingw32-make -j16 SHELL=sh && mingw32-make install SHELL=sh
```

Then copy the install's `bin` and `include` to `deps\ffmpeg-x86`.

Two catches with MinGW's own `make`:

- It cannot read MSYS-style paths, hence building in the source folder.
- It picks the wrong rule for the version resources. Compile them by hand first with
  `windres -I. -o libX/Xres.o libX/Xres.rc`, and give each an empty, older `libX/Xres.s`.

## Tests

`compare_show` is new. It is the first test of IShow, so it covers the players themselves as well
as the seam. The original's players and the port's run the same operations:

- **Ogg:** the game's three shortest music files (from the unpacked `Sounds` folder), decoded
  into fake DirectSound buffers. The test wakes each side's stream thread as DirectSound's
  notification would and waits for its refill. The DirectSound calls, the buffers' bytes and the
  players' fields must match.
- **Bink:** the game's three smallest movies (`Gamemovie`), through the real `binkw32.dll`. Both
  sides' imports of Bink are pointed at logging wrappers. `BinkWait` gives both sides the same
  scripted answers, so the result does not depend on the clock, and `BinkSetSoundSystem` is only
  logged. The Bink calls with their arguments, the tiles (layout, quads, and the pixels on the
  fake Direct3D device) and the fields must match.
- **AVI:** uncompressed AVI files written by the test (24-, 32- and 16-bit, one with a sound
  track), through Windows' DirectShow and each side's texture renderer, on four texture formats
  and sizes. The graphs run without a reference clock, so every frame between a random start and
  stop reaches the tiles. A silent WAV goes through a plain graph. The frame counts, the last
  frame's pixels, the events, positions and sizes must match.

Result: 20,000 Ogg rounds, 20,000 Bink rounds and the AVI and WAV cases, identical on each of
two seeds (1 and 2). A run with seed 1 takes two minutes. It opens 1,445 music files and wakes
the stream threads 8,066 times (170 of the wakes stop a stream at its end). It opens 1,615 movies,
answers 4,796 `BinkWait` calls and compares 8.1 GB of tile pixels. All 12 AVI cases and the WAV
open (the AVI with sound needs a sound card).

18 deliberate bugs are caught:

- 7 in the backend's Bink half: the frame sought, the copied rectangle, the rate, the wait, the
  volume, the sound system and pause;
- 4 in its graph half: seeking, the picture size, events and rendering a file;
- 1 in the texture renderer's pixel format;
- 2 in IShow's frame sink: the copy width and true colour;
- 4 in IShow itself: the Ogg notification offsets, the copy-everything flag, the paused event,
  and the end-of-stream quirk below.

The rest of the suite gives the same results as before the change.

What the test found in the port (not in the seam), now in the
[porting notes](porting_notes.md):

- The original marks the end of a non-looping stream only in a refill that wraps past the
  buffer's end; the refill up to the play cursor writes silence without marking it. The stream
  thread stops a marked stream at its next count of eleven notifications, so the original's music
  stops later than the port's did, which marked the end in both refills. The quirk is kept now.
- `Close` returns whatever freeing the Ogg decoder left in a register (1) when there was one; the
  port returns 0.

Two things the test has to take care of, both outside the players' logic:

- `Close` ends the stream thread with `TerminateThread`, as the original does. A thread ended
  while it is still starting up (its DLL attach calls, its first message wait) leaves a lock
  taken, and every later thread hangs. The test waits for each new thread to block in its wait.
  The game never closes a stream that soon.
- A seek while paused pauses the graph again, and its "paused" event arrives some time later.
  The test waits for it before running the graph, so that both sides see their events in the
  same order.

### The FFmpeg backend: `test_video_ffmpeg`

The FFmpeg backend has no original to compare with, so the test puts it beside the Windows
backend, which makes exactly the original's calls (above):

- **Movies.** The game's Bink movies are decoded by both. Every frame is copied out in RGB32, and
  every seventh also in RGB565 and RGB555. The sizes, frame counts, rates and frame numbers must
  be the same, and the pictures close: FFmpeg's YUV-to-RGB is not Bink's. Then come seeks (the
  first frame, the last, random ones) and a tile-sized rectangle copy.
- **Graphs.** Uncompressed AVI files, written by the test as `compare_show` writes them, are
  rendered into a sink by both. DirectShow runs without a reference clock. Both must deliver the
  same frames, in the same order, with the same pixels. The 16-bit conversions may differ by one
  5-bit step: DirectShow's colour converter truncates where swscale rounds. Then FFmpeg's graph
  alone: a seek while stopped, the paused event, the end event (once), a sound-only file, a
  missing file.
- **Sound.** A movie's sound goes through the sound device: the OpenAL Soft backend on its
  loopback device. It must play without gaps, at half the level at Bink volume 16,384, stay
  silent while paused and play on after, and fall silent when IShow stops asking about the movie.

Result: all 68 of the game's movies and 101,158 frames, in 12 minutes.

- RGB32 averages 49.7 dB, and no movie is under 47.7 dB. Most movies' pixels are all within 2
  of 255. A few movies have single blocks further off: up to 150 in `D_OutCistFrance1` and
  `D_OutGenFrance1`, where FFmpeg's decoder does not match binkw32 exactly.
- The 16-bit copies average 35 dB: both quantize to 5 or 6 bits, differently.
- 78 AVI frames are identical in 32 bits.
- The suite runs 60 frames each of the 8 smallest movies.

Of 20 deliberately introduced bugs, 15 are caught: the pixel formats, the copy rectangle, the
frame numbers, the next frame, the rate, the graph's row order and dropped frames, the paused
event, a repeated end event, the volume, the watchdog, pause, the sound not starting, and a stale
conversion. The other 5 change nothing these files show:

- a frame count the Bink demuxer never gives;
- a seek one frame late, when the only keyframe is the first;
- unaligned graph rows, read with the pitch the sink is given;
- a coarser seek in a graph, whose dropped frames hide it;
- the silencing of the ring, which the watchdog makes redundant.

## Not covered

PlayModal (it reads the keyboard), Draw (IGraph's render states), the external stream API
(nothing in LS3DF installs one), Bink's own sound, and graphs that open a video window. The FFmpeg
backend has been tried in the game (November 2026): the intro, a briefing and the credits play
with their sound, and a movie can be skipped.
