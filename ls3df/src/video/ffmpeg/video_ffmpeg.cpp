// The FFmpeg backend of the video device (video/video.h): Bink movies and the files IShow plays
// through DirectShow graphs, decoded by FFmpeg (avformat, avcodec, swscale, swresample; the DLLs are
// loaded at run time). LS3DF.ini chooses it ([Video] FFmpeg=1, video/video.cpp).
//
// - Movies keep Bink's model. IShow asks whether the next frame is due (the wall clock from the frame
//   the movie started or was sought at, at the movie's rate, standing still while paused), decodes
//   it and copies rectangles of it in RGB32, RGB565 or RGB555. FFmpeg's Bink decoder gives YUV 4:2:0,
//   converted once per frame and format with swscale. Frames are decoded in order; a seek goes to the
//   keyframe before and decodes up to the frame.
// - A movie's sound plays through the sound device (audio/audio.h), not Bink's own DirectSound: a
//   looping buffer kept filled about a second ahead of its play cursor whenever IShow asks about the
//   movie (every frame it draws). The movie's clock and its sound start together, at the open or a
//   seek, as Bink's do.
// - Graphs keep DirectShow's model: stopped, paused or running, a position, and events (complete,
//   paused) for NextEvent. A thread decodes the file; while the graph runs it hands each frame to the
//   sink when its time comes (bottom-up rows, as the texture renderer got them) and keeps the
//   sound's buffer filled. A file rendered without a sink plays its sound only: there is no window
//   for its pictures. Only the last file rendered into a graph plays (DirectShow kept earlier ones
//   connected; IShow renders one file per graph unless one fails).
#include "video/video.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#pragma warning(push, 0)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#pragma warning(pop)

#include "audio/audio.h"
#include "platform/platform.h"
#include "video/backends.h"

namespace ls3d::video {

namespace {

// ---------------------------------------------------------------- the libraries

// The functions the backend calls, by library (kLibraries).
#define LS3D_FF_FUNCTIONS(X)                                                                                 \
    X(0, av_frame_alloc)                                                                                 \
    X(0, av_frame_free)                                                                                  \
    X(0, av_frame_unref)                                                                                 \
    X(0, av_frame_move_ref)                                                                              \
    X(0, av_log_set_level)                                                                               \
    X(0, av_channel_layout_default)                                                                      \
    X(0, av_channel_layout_uninit)                                                                       \
    X(1, av_packet_alloc)                                                                                \
    X(1, av_packet_free)                                                                                 \
    X(1, avcodec_find_decoder)                                                                           \
    X(1, avcodec_alloc_context3)                                                                         \
    X(1, avcodec_free_context)                                                                           \
    X(1, avcodec_parameters_to_context)                                                                  \
    X(1, avcodec_open2)                                                                                  \
    X(1, avcodec_send_packet)                                                                            \
    X(1, avcodec_receive_frame)                                                                          \
    X(1, avcodec_flush_buffers)                                                                          \
    X(2, avformat_open_input)                                                                            \
    X(2, avformat_find_stream_info)                                                                      \
    X(2, avformat_close_input)                                                                           \
    X(2, av_read_frame)                                                                                  \
    X(2, av_seek_frame)                                                                                  \
    X(2, av_find_best_stream)                                                                            \
    X(3, sws_getContext)                                                                                 \
    X(3, sws_scale)                                                                                      \
    X(3, sws_freeContext)                                                                                \
    X(4, swr_alloc_set_opts2)                                                                            \
    X(4, swr_init)                                                                                       \
    X(4, swr_convert)                                                                                    \
    X(4, swr_free)

const char *const kLibraries[] = {"avutil-59.dll", "avcodec-61.dll", "avformat-61.dll", "swscale-8.dll",
                                  "swresample-5.dll"};

struct S_ff {
#define LS3D_FF_MEMBER(library, name) decltype(&::name) name;
    LS3D_FF_FUNCTIONS(LS3D_FF_MEMBER)
#undef LS3D_FF_MEMBER
};
S_ff ff;

bool LoadLibraries()
{
    static int state;               // 0: not tried, 1: loaded, -1: missing
    if (state)
        return state > 0;
    state = -1;
    platform::Library *libraries[5] = {};
    for (int i = 0; i < 5; ++i) {
        if (!(libraries[i] = platform::OpenLibrary(kLibraries[i]))) {
            while (i--)
                platform::CloseLibrary(libraries[i]);
            return false;
        }
    }
    bool found = true;
#define LS3D_FF_LOAD(library, name)                                                                         \
    found &= (ff.name = reinterpret_cast<decltype(ff.name)>(platform::LibrarySymbol(libraries[library], #name))) != \
             nullptr;
    LS3D_FF_FUNCTIONS(LS3D_FF_LOAD)
#undef LS3D_FF_LOAD
    if (!found) {
        for (platform::Library *l : libraries)
            platform::CloseLibrary(l);
        return false;
    }
    ff.av_log_set_level(AV_LOG_QUIET);
    state = 1;
    return true;
}

// ---------------------------------------------------------------- sound

// Hundredths of a decibel of a 0 - 32768 volume (Bink's).
LONG Millibels(int32_t volume)
{
    if (volume <= 0)
        return DSBVOLUME_MIN;
    const double mB = 2000.0 * std::log10(volume / 32768.0);
    return mB < DSBVOLUME_MIN ? DSBVOLUME_MIN : mB > 0 ? 0 : static_cast<LONG>(mB);
}

// A movie's or a graph's sound: 16-bit PCM in a looping buffer of the sound device, kept filled
// ahead of its play cursor. The rest of the ring is kept silent, so a late refill (or none: a movie
// IShow stopped asking about) plays silence rather than old sound.
class S_sound {
public:
    bool Open(audio::System *system, int channels, int rate)
    {
        Close();
        if (!system || channels < 1 || channels > 2 || rate < 100 || rate > 100000)
            return false;
        block = channels * 2;
        bytesPerSecond = static_cast<DWORD>(rate * block);
        size = bytesPerSecond * 2;
        WAVEFORMATEX f = {};
        f.wFormatTag = WAVE_FORMAT_PCM;
        f.nChannels = static_cast<WORD>(channels);
        f.nSamplesPerSec = static_cast<DWORD>(rate);
        f.wBitsPerSample = 16;
        f.nBlockAlign = static_cast<WORD>(block);
        f.nAvgBytesPerSec = bytesPerSecond;
        DSBUFFERDESC desc = {};
        desc.dwSize = sizeof desc;
        desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
        desc.dwBufferBytes = size;
        desc.lpwfxFormat = &f;
        if (audio::g_pAudio->CreateSoundBuffer(system, &desc, &buffer) < 0) {
            buffer = nullptr;
            return false;
        }
        audio::g_pAudio->SetVolume(buffer, volume);
        Clear();
        return true;
    }
    void Close()
    {
        if (buffer) {
            audio::g_pAudio->Stop(buffer);
            audio::g_pAudio->Release(buffer);
            buffer = nullptr;
        }
    }
    bool IsOpen() const { return buffer != nullptr; }
    // Stopped, silent, at the start.
    void Clear()
    {
        if (!buffer)
            return;
        audio::g_pAudio->Stop(buffer);
        audio::g_pAudio->SetCurrentPosition(buffer, 0);
        Zero(0, size);
        writePos = lastPlay = 0;
        written = played = 0;
        silentTo = size;
        wanted = running = false;
    }
    // Plays the sound written, at once or as soon as there is some.
    void Play()
    {
        wanted = true;
        Start();
    }
    void Stop()
    {
        wanted = false;
        Halt();
    }
    // Stops the buffer once everything written has played (the watchdog's call: the cursor would
    // come round to old sound). The next write starts it again.
    void StopIfDry()
    {
        if (running && Drained())
            Halt();
    }
    void SetVolume(LONG mB)
    {
        volume = mB;
        if (buffer)
            audio::g_pAudio->SetVolume(buffer, mB);
    }
    // Bytes that may be written now (whole frames): up to a second ahead of the play cursor.
    DWORD Free()
    {
        if (!buffer)
            return 0;
        DWORD play = 0;
        audio::g_pAudio->GetCurrentPosition(buffer, &play, nullptr);
        played += (play + size - lastPlay) % size;
        lastPlay = play;
        if (played > written) {
            // Ran dry: go on from the cursor.
            written = played;
            writePos = play;
        }
        const uint64_t ahead = written - played;
        return ahead >= bytesPerSecond ? 0 : static_cast<DWORD>((bytesPerSecond - ahead) / block * block);
    }
    // Everything written has been played (the end of a graph's sound).
    bool Drained()
    {
        Free();
        return !buffer || played >= written;
    }
    void Write(const uint8_t *data, DWORD bytes)
    {
        if (!buffer || !bytes)
            return;
        Copy(writePos, data, bytes);
        writePos = (writePos + bytes) % size;
        written += bytes;
        // Silence from the end of the sound round to the play cursor (what was played comes round
        // again): zeroed as the cursor moves on.
        if (silentTo < written)
            silentTo = written;
        if (played + size > silentTo) {
            Zero(static_cast<DWORD>(silentTo % size), static_cast<DWORD>(played + size - silentTo));
            silentTo = played + size;
        }
        Start();
    }

private:
    void Start()
    {
        if (buffer && wanted && !running && written > played) {
            audio::g_pAudio->Play(buffer, 0, 0, DSBPLAY_LOOPING);
            running = true;
        }
    }
    void Halt()
    {
        if (buffer && running) {
            audio::g_pAudio->Stop(buffer);
            running = false;
        }
    }
    void Copy(DWORD at, const uint8_t *data, DWORD bytes)
    {
        void *p1 = nullptr, *p2 = nullptr;
        DWORD s1 = 0, s2 = 0;
        if (audio::g_pAudio->Lock(buffer, at, bytes, &p1, &s1, &p2, &s2, 0) < 0)
            return;
        if (data) {
            std::memcpy(p1, data, s1);
            if (p2)
                std::memcpy(p2, data + s1, s2);
        } else {
            std::memset(p1, 0, s1);
            if (p2)
                std::memset(p2, 0, s2);
        }
        audio::g_pAudio->Unlock(buffer, p1, s1, p2, s2);
    }
    void Zero(DWORD at, DWORD bytes) { Copy(at, nullptr, std::min(bytes, size)); }

    audio::Buffer *buffer = nullptr;
    DWORD size = 0, block = 2, bytesPerSecond = 0;
    DWORD writePos = 0, lastPlay = 0;
    uint64_t written = 0, played = 0;   // bytes, since the last Clear
    uint64_t silentTo = 0;              // the ring is silent from `written` to here
    bool wanted = false;                // Play (not Stop) last
    bool running = false;               // the buffer plays
    LONG volume = 0;
};

// ---------------------------------------------------------------- decoding

// A file's demuxer and decoders: the first video and audio streams, their packets queued apart.
class S_decoder {
public:
    ~S_decoder() { Close(); }

    bool Open(const char *fileName)
    {
        if (ff.avformat_open_input(&format, fileName, nullptr, nullptr) < 0) {
            format = nullptr;
            return false;
        }
        if (ff.avformat_find_stream_info(format, nullptr) < 0) {
            Close();
            return false;
        }
        OpenStream(video, AVMEDIA_TYPE_VIDEO);
        OpenStream(audio, AVMEDIA_TYPE_AUDIO);
        frame = ff.av_frame_alloc();
        return true;
    }
    void Close()
    {
        for (S_stream *s : {&video, &audio}) {
            Clear(*s);
            if (s->codec)
                ff.avcodec_free_context(&s->codec);
            s->index = -1;
        }
        if (frame)
            ff.av_frame_free(&frame);
        if (format)
            ff.avformat_close_input(&format);
    }
    bool HasVideo() const { return video.codec != nullptr; }
    bool HasAudio() const { return audio.codec != nullptr; }
    AVStream *VideoStream() const { return format->streams[video.index]; }
    AVStream *AudioStream() const { return format->streams[audio.index]; }
    AVCodecContext *AudioCodec() const { return audio.codec; }

    // The next decoded video / audio frame (in `frame` until the next call); false at the end.
    AVFrame *NextVideo() { return Receive(video) ? frame : nullptr; }
    AVFrame *NextAudio() { return Receive(audio) ? frame : nullptr; }

    // To the keyframe at or before `timestamp` (of `stream`; -1: AV_TIME_BASE units), decoders flushed.
    void Seek(int stream, int64_t timestamp)
    {
        ff.av_seek_frame(format, stream, timestamp, AVSEEK_FLAG_BACKWARD);
        for (S_stream *s : {&video, &audio}) {
            Clear(*s);
            if (s->codec)
                ff.avcodec_flush_buffers(s->codec);
        }
        ended = false;
    }
    int VideoIndex() const { return video.index; }

private:
    struct S_stream {
        int index = -1;
        AVCodecContext *codec = nullptr;
        std::deque<AVPacket *> packets;
        bool flushed = false;               // the end sent to the decoder
    };

    void OpenStream(S_stream &s, AVMediaType type)
    {
        const int index = ff.av_find_best_stream(format, type, -1, -1, nullptr, 0);
        if (index < 0)
            return;
        const AVCodec *decoder = ff.avcodec_find_decoder(format->streams[index]->codecpar->codec_id);
        if (!decoder)
            return;
        AVCodecContext *c = ff.avcodec_alloc_context3(decoder);
        if (!c)
            return;
        if (ff.avcodec_parameters_to_context(c, format->streams[index]->codecpar) < 0 ||
            ff.avcodec_open2(c, decoder, nullptr) < 0) {
            ff.avcodec_free_context(&c);
            return;
        }
        s.index = index;
        s.codec = c;
    }
    void Clear(S_stream &s)
    {
        for (AVPacket *p : s.packets)
            ff.av_packet_free(&p);
        s.packets.clear();
        s.flushed = false;
    }
    // One packet from the file, into its stream's queue; false at the end.
    bool Read()
    {
        if (ended)
            return false;
        AVPacket *p = ff.av_packet_alloc();
        if (ff.av_read_frame(format, p) < 0) {
            ff.av_packet_free(&p);
            ended = true;
            return false;
        }
        if (p->stream_index == video.index && video.codec)
            video.packets.push_back(p);
        else if (p->stream_index == audio.index && audio.codec)
            audio.packets.push_back(p);
        else
            ff.av_packet_free(&p);
        return true;
    }
    bool Receive(S_stream &s)
    {
        if (!s.codec)
            return false;
        for (;;) {
            const int r = ff.avcodec_receive_frame(s.codec, frame);
            if (r == 0)
                return true;
            if (r != AVERROR(EAGAIN))
                return false;               // the end, or an error
            if (s.packets.empty() && Read())
                continue;
            if (!s.packets.empty()) {
                AVPacket *p = s.packets.front();
                s.packets.pop_front();
                ff.avcodec_send_packet(s.codec, p);
                ff.av_packet_free(&p);
            } else if (!s.flushed) {
                ff.avcodec_send_packet(s.codec, nullptr);
                s.flushed = true;
            } else {
                return false;
            }
        }
    }

    AVFormatContext *format = nullptr;
    S_stream video, audio;
    AVFrame *frame = nullptr;
    bool ended = false;
};

// Converts decoded audio to the sound's 16-bit PCM (mono or stereo, the stream's rate).
class S_resampler {
public:
    ~S_resampler() { Close(); }
    bool Open(const AVCodecContext *codec)
    {
        Close();
        channels = codec->ch_layout.nb_channels >= 2 ? 2 : 1;
        rate = codec->sample_rate;
        AVChannelLayout out = {};
        ff.av_channel_layout_default(&out, channels);
        const int r = ff.swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_S16, rate, &codec->ch_layout, codec->sample_fmt,
                                             codec->sample_rate, 0, nullptr);
        ff.av_channel_layout_uninit(&out);
        if (r < 0 || ff.swr_init(swr) < 0) {
            Close();
            return false;
        }
        return true;
    }
    void Close()
    {
        if (swr)
            ff.swr_free(&swr);
    }
    // The frame's samples, appended to `out` (interleaved 16-bit), `skip` frames dropped first.
    void Convert(const AVFrame *f, std::vector<uint8_t> &out, int64_t &skip)
    {
        if (!swr)
            return;
        std::vector<uint8_t> converted(static_cast<size_t>(f->nb_samples + 256) * channels * 2);
        uint8_t *dst = converted.data();
        const int n = ff.swr_convert(swr, &dst, f->nb_samples + 256, f->extended_data,
                                     f->nb_samples);
        if (n <= 0)
            return;
        int64_t first = std::min<int64_t>(skip, n);
        skip -= first;
        out.insert(out.end(), converted.begin() + static_cast<std::ptrdiff_t>(first * channels * 2),
                   converted.begin() + static_cast<std::ptrdiff_t>(static_cast<int64_t>(n) * channels * 2));
    }
    int channels = 0, rate = 0;

private:
    SwrContext *swr = nullptr;
};

// A picture in RGB32 (BGRA in memory), RGB565 or RGB555, top-down rows.
struct S_rgb {
    std::vector<uint8_t> bits;
    int pitch = 0;
    int64_t source = -1;                    // which picture it holds
    SwsContext *sws = nullptr;
    ~S_rgb()
    {
        if (sws)
            ff.sws_freeContext(sws);
    }
    void Convert(const AVFrame *f, AVPixelFormat format)
    {
        if (!sws)
            sws = ff.sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format), f->width, f->height,
                                    format, SWS_POINT | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT, nullptr, nullptr,
                                    nullptr);
        pitch = f->width * (format == AV_PIX_FMT_BGRA ? 4 : 2);
        bits.resize(static_cast<size_t>(pitch) * f->height);
        uint8_t *dst[4] = {bits.data()};
        int strides[4] = {pitch};
        if (sws)
            ff.sws_scale(sws, f->data, f->linesize, 0, f->height, dst, strides);
    }
};

// ---------------------------------------------------------------- movies

struct S_movie {
    std::mutex lock;                        // (the watchdog thread)
    S_decoder decoder;
    uint32_t width = 0, height = 0, frames = 0;
    uint32_t rate = 1, rateDivisor = 1;
    uint32_t frame = 1;                     // the current frame (from 1)
    AVFrame *picture = nullptr;             // the decoded current frame
    uint32_t pictureFrame = 0;              // which frame `picture` holds (0: none)
    int64_t pictures = 0;                   // pictures decoded (the RGB copies' source)
    uint32_t decoded = 0;                   // the last frame the decoder gave (0: none since the start / seek)
    S_rgb rgb[3];                           // RGB32, RGB565, RGB555
    // The clock: `startFrame` was due at `startMs`. It and the sound start at the first frame decoded
    // after the open or a seek (`started`).
    bool started = false;
    uint32_t startMs = 0, startFrame = 1;
    bool paused = false;
    uint32_t pausedMs = 0;
    // Sound.
    S_sound sound;
    S_resampler resampler;
    std::vector<uint8_t> pending;           // converted, not yet written
    int64_t skip = 0;                       // sample frames to drop (after a seek)
    double skipUntil = 0;                   // seconds: the sound before it is dropped
    bool soundEnded = false;
    double audioTimeBase = 0;
    platform::Thread *watchdog = nullptr;
    std::atomic<bool> quit{false};

    ~S_movie()
    {
        if (picture)
            ff.av_frame_free(&picture);
    }

    // The frame number of a decoded picture (from 1).
    uint32_t FrameOf(const AVFrame *f) const
    {
        const AVStream *s = decoder.VideoStream();
        const int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        if (pts == AV_NOPTS_VALUE)
            return decoded + 1;
        const double seconds = (pts - (s->start_time != AV_NOPTS_VALUE ? s->start_time : 0)) * av_q2d(s->time_base);
        return static_cast<uint32_t>(std::floor(seconds * rate / rateDivisor + 0.5)) + 1;
    }

    // Decodes up to `frame` (into `picture`); the clock and the sound start with the first one.
    void Decode()
    {
        if (pictureFrame != frame) {
            while (decoded < frame) {
                AVFrame *f = decoder.NextVideo();
                if (!f)
                    break;
                decoded = FrameOf(f);
                if (decoded >= frame || decoded == frames) {
                    ff.av_frame_unref(picture);
                    ff.av_frame_move_ref(picture, f);
                    ++pictures;
                }
            }
            pictureFrame = frame;
        }
        if (!started) {
            started = true;
            startMs = pausedMs = platform::Milliseconds();
            startFrame = frame;
            FillSound();
            if (!paused)
                sound.Play();
        } else {
            FillSound();
        }
    }

    // To `target`: the keyframe before it (the file, so the sound too); the clock and the sound start
    // again at the next frame decoded.
    void Seek(uint32_t target)
    {
        frame = target;
        const AVStream *s = decoder.VideoStream();
        const double seconds = (target - 1) * static_cast<double>(rateDivisor) / rate;
        int64_t ts = static_cast<int64_t>(seconds / av_q2d(s->time_base) + 0.5);
        if (s->start_time != AV_NOPTS_VALUE)
            ts += s->start_time;
        decoder.Seek(decoder.VideoIndex(), ts);
        decoded = 0;
        pictureFrame = 0;
        Restart();
    }

    // The clock and the sound wait for the current frame to be decoded.
    void Restart()
    {
        started = false;
        if (!sound.IsOpen())
            return;
        sound.Clear();
        pending.clear();
        soundEnded = false;
        // The sound from the frame's time on: what the decoder gives before it is dropped.
        skip = 0;
        skipUntil = (frame - 1) * static_cast<double>(rateDivisor) / rate;
    }

    void FillSound()
    {
        if (!sound.IsOpen())
            return;
        DWORD free = sound.Free();
        while (free) {
            if (pending.empty()) {
                if (soundEnded)
                    break;
                AVFrame *f = decoder.NextAudio();
                if (!f) {
                    soundEnded = true;
                    break;
                }
                // Drop what lies before the frame the sound started at.
                if (skipUntil > 0 && f->pts != AV_NOPTS_VALUE) {
                    const double start = f->pts * audioTimeBase;
                    const double end = start + static_cast<double>(f->nb_samples) / resampler.rate;
                    if (end <= skipUntil)
                        continue;
                    if (start < skipUntil)
                        skip = static_cast<int64_t>((skipUntil - start) * resampler.rate);
                    skipUntil = 0;
                }
                resampler.Convert(f, pending, skip);
                continue;
            }
            const DWORD n = std::min<DWORD>(free, static_cast<DWORD>(pending.size()));
            sound.Write(pending.data(), n);
            pending.erase(pending.begin(), pending.begin() + n);
            free -= n;
        }
    }
};

S_movie *M(Movie *movie) { return reinterpret_cast<S_movie *>(movie); }

// A movie's sound stops when IShow stops asking for frames (Bink's mixer ran dry the same way): the
// looping buffer would come round to old sound.
DWORD WINAPI MovieWatchdog(void *param)
{
    S_movie *m = static_cast<S_movie *>(param);
    while (!m->quit) {
        {
            std::lock_guard<std::mutex> guard(m->lock);
            m->sound.StopIfDry();
        }
        platform::Pause(10);
    }
    return 0;
}

// ---------------------------------------------------------------- graphs

struct S_graph {
    std::mutex lock;
    S_decoder *decoder = nullptr;           // the last file rendered
    FrameSink *sink = nullptr;              // its sink (null: a file played with its own renderers)
    std::vector<FrameSink *> sinks;         // every sink given (owned)
    int width = 0, height = 0;              // the picture (with a sink)
    D3DFORMAT format = D3DFMT_UNKNOWN;      // the sink's tiles
    AVPixelFormat pixels = AV_PIX_FMT_NONE; // the frames' layout for the sink
    int pitch = 0;
    std::vector<uint8_t> bottomUp;
    S_rgb rgb;
    audio::System *soundSystem = nullptr;
    S_sound sound;
    S_resampler resampler;
    std::vector<uint8_t> pending;
    LONG volume = 0;

    enum { STOPPED, PAUSED, RUNNING } state = STOPPED;
    int64_t position = 0;                   // 100 ns (stopped / paused; running: from the clock)
    uint32_t clockMs = 0;                   // running: `position` was at `clockMs`
    bool videoEnded = false, soundEnded = false, complete = false;
    AVFrame *next = nullptr;                // decoded, waiting for its time
    int64_t nextTime = 0;                   // 100 ns
    int64_t skipUntil = -1;                 // 100 ns: frames and sound before it are dropped (after a seek)
    std::deque<long> events;

    platform::Thread *thread = nullptr;
    std::atomic<bool> quit{false};

    int64_t Now() const
    {
        return state == RUNNING ? position + static_cast<int64_t>(platform::Milliseconds() - clockMs) * 10000
                                : position;
    }
    void Drop()
    {
        if (next)
            ff.av_frame_free(&next);
    }
    void CloseFile()
    {
        Drop();
        delete decoder;
        decoder = nullptr;
        sink = nullptr;
        sound.Close();
        resampler.Close();
        pending.clear();
    }
    // 100 ns of a decoded frame of `stream`.
    static int64_t TimeOf(const AVFrame *f, const AVStream *s)
    {
        int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        if (pts == AV_NOPTS_VALUE)
            return 0;
        if (s->start_time != AV_NOPTS_VALUE)
            pts -= s->start_time;
        return static_cast<int64_t>(pts * av_q2d(s->time_base) * 1e7 + 0.5);
    }
    // To `time` (100 ns): the keyframe before, then what lies before `time` is dropped.
    void Seek(int64_t time)
    {
        position = time;
        clockMs = platform::Milliseconds();
        videoEnded = soundEnded = complete = false;
        Drop();
        pending.clear();
        if (!decoder)
            return;
        decoder->Seek(-1, time / 10);       // (AV_TIME_BASE: microseconds)
        skipUntil = time;
        if (sound.IsOpen()) {
            sound.Clear();
            if (state == RUNNING) {
                FillSound();
                sound.Play();
            }
        }
    }
    void FillSound()
    {
        if (!sound.IsOpen())
            return;
        DWORD free = sound.Free();
        while (free) {
            if (pending.empty()) {
                if (soundEnded)
                    break;
                AVFrame *f = decoder->NextAudio();
                if (!f) {
                    soundEnded = true;
                    break;
                }
                int64_t skip = 0;
                const int64_t start = TimeOf(f, decoder->AudioStream());
                if (skipUntil >= 0 && start < skipUntil) {
                    const int64_t end = start + static_cast<int64_t>(f->nb_samples) * 10000000 / resampler.rate;
                    if (end <= skipUntil)
                        continue;
                    skip = (skipUntil - start) * resampler.rate / 10000000;
                }
                resampler.Convert(f, pending, skip);
                continue;
            }
            const DWORD n = std::min<DWORD>(free, static_cast<DWORD>(pending.size()));
            sound.Write(pending.data(), n);
            pending.erase(pending.begin(), pending.begin() + n);
            free -= n;
        }
    }
    // The next frame for the sink, when its time has come (lock held).
    void Present()
    {
        if (!sink || videoEnded)
            return;
        while (!next) {
            AVFrame *f = decoder->NextVideo();
            if (!f) {
                videoEnded = true;
                return;
            }
            const int64_t t = TimeOf(f, decoder->VideoStream());
            if (skipUntil >= 0 && t + 400000 < skipUntil)
                continue;                   // (before the position sought: kept within 40 ms)
            next = ff.av_frame_alloc();
            ff.av_frame_move_ref(next, f);
            nextTime = t;
        }
        if (nextTime > Now())
            return;
        rgb.Convert(next, pixels);
        // Bottom-up rows of DWORD-aligned pitch, as DirectShow's renderer got them.
        const int bytes = width * (pixels == AV_PIX_FMT_BGRA ? 4 : 2);
        bottomUp.assign(static_cast<size_t>(pitch) * height, 0);
        for (int y = 0; y < height && y < next->height; ++y)
            std::memcpy(bottomUp.data() + static_cast<size_t>(height - 1 - y) * pitch,
                        rgb.bits.data() + static_cast<size_t>(y) * rgb.pitch, std::min(bytes, rgb.pitch));
        sink->Frame(bottomUp.data(), pitch, height, format);
        Drop();
    }
    // The thread's step (lock held): frames, sound, the end.
    void Step()
    {
        if (state != RUNNING || complete || !decoder)
            return;
        Present();
        FillSound();
        sound.StopIfDry();
        if (skipUntil >= 0 && (videoEnded || next) && (soundEnded || !pending.empty() || !sound.IsOpen()))
            skipUntil = -1;
        if ((videoEnded || !sink) && (soundEnded || !sound.IsOpen()) && (!sound.IsOpen() || sound.Drained())) {
            complete = true;
            events.push_back(GRAPH_EVENT_COMPLETE);
        }
    }
};

S_graph *G(Graph *graph) { return reinterpret_cast<S_graph *>(graph); }

DWORD WINAPI GraphThread(void *param)
{
    S_graph *g = static_cast<S_graph *>(param);
    while (!g->quit) {
        {
            std::lock_guard<std::mutex> guard(g->lock);
            g->Step();
        }
        platform::Pause(2);
    }
    return 0;
}

class DeviceFFmpeg final : public Device {
public:
    // ---- movies
    void UseSound(audio::System *system) override { movieSound = system; }
    Movie *OpenMovie(const char *fileName) override
    {
        S_movie *m = new S_movie;
        if (!m->decoder.Open(fileName) || !m->decoder.HasVideo()) {
            delete m;
            return nullptr;
        }
        const AVStream *s = m->decoder.VideoStream();
        m->width = static_cast<uint32_t>(s->codecpar->width);
        m->height = static_cast<uint32_t>(s->codecpar->height);
        const AVRational fps = s->avg_frame_rate.num > 0 ? s->avg_frame_rate : s->r_frame_rate;
        m->rate = fps.num > 0 ? static_cast<uint32_t>(fps.num) : 25;
        m->rateDivisor = fps.den > 0 ? static_cast<uint32_t>(fps.den) : 1;
        m->frames = s->nb_frames > 0 ? static_cast<uint32_t>(s->nb_frames)
                                     : static_cast<uint32_t>(s->duration * av_q2d(s->time_base) * m->rate /
                                                             m->rateDivisor + 0.5);
        m->picture = ff.av_frame_alloc();
        if (m->decoder.HasAudio() && movieSound && m->resampler.Open(m->decoder.AudioCodec())) {
            m->audioTimeBase = av_q2d(m->decoder.AudioStream()->time_base);
            m->sound.Open(movieSound, m->resampler.channels, m->resampler.rate);
        }
        m->Restart();
        if (m->sound.IsOpen())
            m->watchdog = platform::StartThread(MovieWatchdog, m);
        return reinterpret_cast<Movie *>(m);
    }
    void CloseMovie(Movie *movie) override
    {
        S_movie *m = M(movie);
        if (m->watchdog) {
            m->quit = true;
            platform::Join(m->watchdog);
            platform::Release(m->watchdog);
        }
        m->sound.Close();
        delete m;
    }
    S_movieInfo Info(Movie *movie) override
    {
        S_movie *m = M(movie);
        std::lock_guard<std::mutex> guard(m->lock);
        return {m->width, m->height, m->frames, m->frame, m->rate, m->rateDivisor};
    }
    void Pause(Movie *movie, bool pause) override
    {
        S_movie *m = M(movie);
        std::lock_guard<std::mutex> guard(m->lock);
        if (pause == m->paused)
            return;
        m->paused = pause;
        if (!m->started)
            return;                         // (the clock and the sound have not started)
        if (pause) {
            m->pausedMs = platform::Milliseconds();
            m->sound.Stop();
        } else {
            m->startMs += platform::Milliseconds() - m->pausedMs;
            m->FillSound();
            m->sound.Play();
        }
    }
    void Goto(Movie *movie, uint32_t frame) override
    {
        S_movie *m = M(movie);
        std::lock_guard<std::mutex> guard(m->lock);
        m->Seek(frame < 1 ? 1 : frame > m->frames ? m->frames : frame);
    }
    bool Wait(Movie *movie) override
    {
        S_movie *m = M(movie);
        std::lock_guard<std::mutex> guard(m->lock);
        if (m->paused)
            return true;
        if (!m->started)
            return false;                   // (the first frame after the open or a seek: due now)
        m->FillSound();
        const double due = m->startMs + (m->frame - m->startFrame) * 1000.0 * m->rateDivisor / m->rate;
        return platform::Milliseconds() < due;
    }
    void DecodeFrame(Movie *movie) override
    {
        S_movie *m = M(movie);
        std::lock_guard<std::mutex> guard(m->lock);
        m->Decode();
    }
    void NextFrame(Movie *movie) override
    {
        S_movie *m = M(movie);
        std::lock_guard<std::mutex> guard(m->lock);
        if (m->frame < m->frames)
            ++m->frame;
        else
            m->Seek(1);                     // (Bink goes round to the first frame)
    }
    void CopyFrame(Movie *movie, void *dest, int pitch, uint32_t destHeight, uint32_t x, uint32_t y, uint32_t width,
                   uint32_t height, uint32_t format) override
    {
        S_movie *m = M(movie);
        std::lock_guard<std::mutex> guard(m->lock);
        if (!m->pictureFrame || !m->picture->data[0])
            return;
        int slot;
        AVPixelFormat pixels;
        switch (format & ~MOVIE_COPY_ALL) {
        case MOVIE_RGB32: slot = 0, pixels = AV_PIX_FMT_BGRA; break;
        case MOVIE_RGB565: slot = 1, pixels = AV_PIX_FMT_RGB565LE; break;
        case MOVIE_RGB555: slot = 2, pixels = AV_PIX_FMT_RGB555LE; break;
        default: return;
        }
        S_rgb &rgb = m->rgb[slot];
        if (rgb.source != m->pictures) {
            rgb.Convert(m->picture, pixels);
            rgb.source = m->pictures;
        }
        const int bpp = slot ? 2 : 4;
        if (x >= m->width || y >= m->height)
            return;
        width = std::min(width, m->width - x);
        height = std::min(std::min(height, m->height - y), destHeight);
        for (uint32_t row = 0; row < height; ++row)
            std::memcpy(static_cast<uint8_t *>(dest) + static_cast<size_t>(row) * pitch,
                        rgb.bits.data() + static_cast<size_t>(y + row) * rgb.pitch + static_cast<size_t>(x) * bpp,
                        static_cast<size_t>(width) * bpp);
    }
    void SetVolume(Movie *movie, int32_t volume) override
    {
        S_movie *m = M(movie);
        std::lock_guard<std::mutex> guard(m->lock);
        m->sound.SetVolume(Millibels(volume));
    }

    // ---- graphs
    void UseGraphSound(audio::System *system) override { graphSound = system; }
    Graph *CreateGraph() override
    {
        S_graph *g = new S_graph;
        g->soundSystem = graphSound;
        g->thread = platform::StartThread(GraphThread, g);
        return reinterpret_cast<Graph *>(g);
    }
    void ReleaseGraph(Graph *graph) override
    {
        S_graph *g = G(graph);
        g->quit = true;
        platform::Join(g->thread);
        platform::Release(g->thread);
        g->CloseFile();
        for (FrameSink *sink : g->sinks)
            delete sink;
        delete g;
    }
    HRESULT RenderFile(Graph *graph, const char *fileName) override { return Render(G(graph), fileName, nullptr); }
    HRESULT RenderToSink(Graph *graph, const char *fileName, FrameSink *sink) override
    {
        S_graph *g = G(graph);
        {
            std::lock_guard<std::mutex> guard(g->lock);
            g->sinks.push_back(sink);
        }
        return Render(g, fileName, sink);
    }
    void Run(Graph *graph) override
    {
        if (!graph)
            return;
        S_graph *g = G(graph);
        std::lock_guard<std::mutex> guard(g->lock);
        if (g->state == S_graph::RUNNING)
            return;
        g->clockMs = platform::Milliseconds();
        g->state = S_graph::RUNNING;
        g->FillSound();
        g->sound.Play();
    }
    void Stop(Graph *graph) override
    {
        S_graph *g = G(graph);
        std::lock_guard<std::mutex> guard(g->lock);
        g->position = g->Now();
        g->state = S_graph::STOPPED;
        g->sound.Stop();
    }
    void Pause(Graph *graph) override
    {
        S_graph *g = G(graph);
        std::lock_guard<std::mutex> guard(g->lock);
        g->position = g->Now();
        g->state = S_graph::PAUSED;
        g->sound.Stop();
        g->events.push_back(GRAPH_EVENT_PAUSED);
    }
    void SetPosition(Graph *graph, int64_t position) override
    {
        S_graph *g = G(graph);
        std::lock_guard<std::mutex> guard(g->lock);
        g->Seek(position < 0 ? 0 : position);
    }
    int64_t GetPosition(Graph *graph) override
    {
        S_graph *g = G(graph);
        std::lock_guard<std::mutex> guard(g->lock);
        return g->Now();
    }
    void SetVolume(Graph *graph, long volume) override
    {
        S_graph *g = G(graph);
        std::lock_guard<std::mutex> guard(g->lock);
        g->volume = volume;
        g->sound.SetVolume(volume);
    }
    bool NextEvent(Graph *graph, long *code) override
    {
        S_graph *g = G(graph);
        std::lock_guard<std::mutex> guard(g->lock);
        if (g->events.empty())
            return false;
        *code = g->events.front();
        g->events.pop_front();
        return true;
    }
    bool VideoSize(Graph *graph, int *width, int *height) override
    {
        if (!graph)
            return false;
        S_graph *g = G(graph);
        std::lock_guard<std::mutex> guard(g->lock);
        if (!g->sink)
            return false;
        *width = g->width;
        *height = g->height;
        return true;
    }

private:
    HRESULT Render(S_graph *g, const char *fileName, FrameSink *sink)
    {
        std::lock_guard<std::mutex> guard(g->lock);
        g->CloseFile();
        S_decoder *d = new S_decoder;
        if (!d->Open(fileName) || (sink && !d->HasVideo()) || (!sink && !d->HasAudio() && !d->HasVideo())) {
            delete d;
            return E_FAIL;
        }
        if (sink) {
            const AVStream *s = d->VideoStream();
            g->width = s->codecpar->width;
            g->height = s->codecpar->height;
            const int layout = sink->FrameFormat();
            g->pixels = layout == 32 ? AV_PIX_FMT_BGRA : layout == 565 ? AV_PIX_FMT_RGB565LE : AV_PIX_FMT_RGB555LE;
            g->pitch = (g->width * (layout == 32 ? 4 : 2) + 3) & ~3;
            g->format = sink->SetSize(g->width, g->height);
            if (!g->format) {
                delete d;
                return E_FAIL;
            }
            if (g->rgb.sws) {
                ff.sws_freeContext(g->rgb.sws);
                g->rgb.sws = nullptr;
            }
        }
        g->decoder = d;
        g->sink = sink;
        if (d->HasAudio() && g->soundSystem && g->resampler.Open(d->AudioCodec()) &&
            g->sound.Open(g->soundSystem, g->resampler.channels, g->resampler.rate))
            g->sound.SetVolume(g->volume);
        g->Seek(g->position);
        return S_OK;
    }

    audio::System *movieSound = nullptr;
    audio::System *graphSound = nullptr;
};

DeviceFFmpeg s_device;

}  // namespace

Device *FFmpegVideoDevice() { return LoadLibraries() ? &s_device : nullptr; }

}  // namespace ls3d::video
