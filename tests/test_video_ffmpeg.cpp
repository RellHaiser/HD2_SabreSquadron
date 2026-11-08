// Test of the video device's FFmpeg backend (src/video/ffmpeg) against its Windows backend (Bink and
// DirectShow, src/video/win32), which makes exactly the original's calls (compare_show):
// - Movies: the game's Bink movies (the game folder's Gamemovie) decoded by both, every frame
//   copied out in RGB32 (and every seventh in RGB565 and RGB555) and compared: the sizes, frame
//   counts, rates and frame numbers must be the same, the pixels close (FFmpeg's YUV to RGB is not
//   Bink's); then random seeks.
// - Graphs: uncompressed AVI files written by the test (test_media.h) rendered into a sink by both,
//   DirectShow without a reference clock (every frame at once): the same frames, in the same
//   order, the same pixels (32-bit; the 16-bit conversions are allowed one step).
// - A movie's sound through the sound device (the OpenAL Soft backend on its loopback device):
//   played without gaps, at the volume set, silent while paused.
//
// Usage: test_video_ffmpeg.exe [frames per movie (0: all)] [movies (0: all)]
//
// test-sources: src/video/win32/*.cpp src/video/ffmpeg/video_ffmpeg.cpp src/dshow/*.cpp src/core/debug.cpp
// test-sources: src/audio/dsound/audio_dsound.cpp src/audio/openal/audio_openal.cpp
// test-sources: src/platform/win32/platform_win32.cpp
#include <windows.h>

#include <dshow.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "../src/audio/backends.h"
#include "../src/audio/openal/openal_device.h"
#include "../src/core/debug.h"
#include "../src/video/backends.h"
#include "test_media.h"
#include "test_paths.h"

using namespace ls3d;
using namespace ls3d::video;

// Engine parts not linked.
namespace ls3d::i3d {
void RefreshSoundVolumes() {}
}  // namespace ls3d::i3d

namespace {

Device *g_bink, *g_ff;
int g_checks, g_failures;

void Check(bool ok, const char *fmt, ...)
{
    ++g_checks;
    if (ok)
        return;
    ++g_failures;
    if (g_failures > 40)
        return;
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    std::printf("FAIL: %s\n", text);
}

// The difference of two pictures (8-bit channels): squared error, largest difference.
struct S_diff {
    double squared = 0;
    long long samples = 0;
    int largest = 0;
    void Add(int a, int b)
    {
        const int d = a > b ? a - b : b - a;
        squared += double(d) * d;
        ++samples;
        largest = std::max(largest, d);
    }
    double Psnr() const { return samples && squared ? 10.0 * std::log10(255.0 * 255.0 * samples / squared) : 99.0; }
};

// The red, green and blue of a pixel in a frame format (RGB32 / 565 / 555), 8 bits each.
void Channels(const uint8_t *p, uint32_t format, int rgb[3])
{
    if (format == MOVIE_RGB32) {
        rgb[0] = p[2], rgb[1] = p[1], rgb[2] = p[0];
        return;
    }
    const int v = p[0] | p[1] << 8;
    if (format == MOVIE_RGB565) {
        rgb[0] = (v >> 11 & 31) * 255 / 31, rgb[1] = (v >> 5 & 63) * 255 / 63, rgb[2] = (v & 31) * 255 / 31;
    } else {
        rgb[0] = (v >> 10 & 31) * 255 / 31, rgb[1] = (v >> 5 & 31) * 255 / 31, rgb[2] = (v & 31) * 255 / 31;
    }
}

std::vector<uint8_t> Grab(Device *d, Movie *m, uint32_t format)
{
    const S_movieInfo info = d->Info(m);
    const int bpp = format == MOVIE_RGB32 ? 4 : 2;
    std::vector<uint8_t> bits(size_t(info.width) * info.height * bpp, 0xcd);
    d->CopyFrame(m, bits.data(), static_cast<int>(info.width) * bpp, info.height, 0, 0, info.width, info.height,
                 format | MOVIE_COPY_ALL);
    return bits;
}

void Compare(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, uint32_t format, S_diff &diff)
{
    const int bpp = format == MOVIE_RGB32 ? 4 : 2;
    for (size_t i = 0; i + bpp <= a.size() && i + bpp <= b.size(); i += bpp) {
        int ca[3], cb[3];
        Channels(&a[i], format, ca);
        Channels(&b[i], format, cb);
        for (int c = 0; c < 3; ++c)
            diff.Add(ca[c], cb[c]);
    }
}

// ---------------------------------------------------------------- movies

struct S_movieResult {
    S_diff rgb32, rgb16;
    int frames = 0, seeks = 0;
};

void TestMovie(const std::string &path, int maxFrames, std::mt19937 &rng, S_movieResult &total)
{
    Movie *a = g_bink->OpenMovie(path.c_str());
    Movie *b = g_ff->OpenMovie(path.c_str());
    Check(a && b, "%s: opened (Bink %d, FFmpeg %d)", path.c_str(), a != nullptr, b != nullptr);
    if (!a || !b) {
        if (a)
            g_bink->CloseMovie(a);
        if (b)
            g_ff->CloseMovie(b);
        return;
    }
    const S_movieInfo ia = g_bink->Info(a), ib = g_ff->Info(b);
    Check(ia.width == ib.width && ia.height == ib.height && ia.frames == ib.frames && ia.frame == ib.frame &&
              ia.rate * ib.rateDivisor == ib.rate * ia.rateDivisor,
          "%s: info %ux%u %u frames at %u/%u (frame %u) / %ux%u %u at %u/%u (frame %u)", path.c_str(), ia.width,
          ia.height, ia.frames, ia.rate, ia.rateDivisor, ia.frame, ib.width, ib.height, ib.frames, ib.rate,
          ib.rateDivisor, ib.frame);
    S_movieResult r;
    const uint32_t frames = maxFrames ? std::min<uint32_t>(ia.frames, static_cast<uint32_t>(maxFrames)) : ia.frames;
    for (uint32_t f = 1; f <= frames; ++f) {
        g_bink->DecodeFrame(a);
        g_ff->DecodeFrame(b);
        Check(g_bink->Info(a).frame == g_ff->Info(b).frame, "%s: frame %u / %u", path.c_str(), g_bink->Info(a).frame,
              g_ff->Info(b).frame);
        Compare(Grab(g_bink, a, MOVIE_RGB32), Grab(g_ff, b, MOVIE_RGB32), MOVIE_RGB32, r.rgb32);
        if (f % 7 == 1) {
            const uint32_t format = f % 14 == 1 ? MOVIE_RGB565 : MOVIE_RGB555;
            Compare(Grab(g_bink, a, format), Grab(g_ff, b, format), format, r.rgb16);
        }
        ++r.frames;
        if (f < ia.frames) {
            g_bink->NextFrame(a);
            g_ff->NextFrame(b);
        }
    }
    // Seeks: the first frame (a keyframe), the last, and random ones.
    for (int i = 0; i < 6; ++i) {
        const uint32_t target = i == 0 ? 1 : i == 1 ? ia.frames : 1 + rng() % ia.frames;
        g_bink->Goto(a, target);
        g_ff->Goto(b, target);
        g_bink->DecodeFrame(a);
        g_ff->DecodeFrame(b);
        Check(g_bink->Info(a).frame == target && g_ff->Info(b).frame == target, "%s: Goto(%u): frames %u / %u",
              path.c_str(), target, g_bink->Info(a).frame, g_ff->Info(b).frame);
        S_diff d;
        Compare(Grab(g_bink, a, MOVIE_RGB32), Grab(g_ff, b, MOVIE_RGB32), MOVIE_RGB32, d);
        Check(d.Psnr() >= 30.0, "%s: Goto(%u): %.1f dB", path.c_str(), target, d.Psnr());
        r.rgb32.squared += d.squared, r.rgb32.samples += d.samples, r.rgb32.largest = std::max(r.rgb32.largest, d.largest);
        ++r.seeks;
        // A tile's rectangle (IShow copies its tiles so) is that part of the whole picture.
        const uint32_t x = ia.width / 3, y = ia.height / 4, w = std::min(64u, ia.width - x), hgt = std::min(32u, ia.height - y);
        std::vector<uint8_t> tile(size_t(w) * hgt * 4, 0xcd);
        g_ff->CopyFrame(b, tile.data(), static_cast<int>(w) * 4, hgt, x, y, w, hgt, MOVIE_RGB32);
        const std::vector<uint8_t> whole = Grab(g_ff, b, MOVIE_RGB32);
        bool same = true;
        for (uint32_t row = 0; row < hgt; ++row)
            same &= !std::memcmp(&tile[size_t(row) * w * 4], &whole[(size_t(y + row) * ia.width + x) * 4], size_t(w) * 4);
        Check(same, "%s: a rectangle copy is that part of the picture", path.c_str());
    }
    g_bink->CloseMovie(a);
    g_ff->CloseMovie(b);
    std::printf("  %-40s %4d frames  RGB32 %.1f dB (largest %d)  16-bit %.1f dB\n", path.substr(path.rfind('\\') + 1).c_str(),
                r.frames, r.rgb32.Psnr(), r.rgb32.largest, r.rgb16.Psnr());
    Check(r.rgb32.Psnr() >= 35.0 && r.rgb16.Psnr() >= 30.0, "%s: pictures %.1f / %.1f dB", path.c_str(), r.rgb32.Psnr(),
          r.rgb16.Psnr());
    total.rgb32.squared += r.rgb32.squared, total.rgb32.samples += r.rgb32.samples;
    total.rgb32.largest = std::max(total.rgb32.largest, r.rgb32.largest);
    total.rgb16.squared += r.rgb16.squared, total.rgb16.samples += r.rgb16.samples;
    total.frames += r.frames, total.seeks += r.seeks;
}

// ---------------------------------------------------------------- graphs

// A sink that keeps every frame (top-down rows, packed).
class S_capture final : public FrameSink {
public:
    explicit S_capture(int layout) : layout(layout) {}
    int FrameFormat() override { return layout; }
    D3DFORMAT SetSize(int w, int h) override
    {
        width = w, height = h;
        return layout == 32 ? D3DFMT_X8R8G8B8 : layout == 565 ? D3DFMT_R5G6B5 : D3DFMT_X1R5G5B5;
    }
    void Frame(const uint8_t *bits, int pitch, int rows, D3DFORMAT) override
    {
        const int bytes = width * (layout == 32 ? 4 : 2);
        std::vector<uint8_t> f(size_t(bytes) * rows);
        for (int y = 0; y < rows; ++y)
            std::memcpy(&f[size_t(y) * bytes], bits + size_t(rows - 1 - y) * pitch, size_t(bytes));
        std::lock_guard<std::mutex> guard(lock);
        frames.push_back(std::move(f));
    }
    size_t Count()
    {
        std::lock_guard<std::mutex> guard(lock);
        return frames.size();
    }
    int layout, width = 0, height = 0;
    std::mutex lock;
    std::vector<std::vector<uint8_t>> frames;
};

// Runs a graph until it reports the end; false when it does not within `ms`.
bool RunToEnd(Device *d, Graph *g, DWORD ms)
{
    d->Run(g);
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < ms) {
        long code;
        while (d->NextEvent(g, &code)) {
            if (code == GRAPH_EVENT_COMPLETE)
                return true;
        }
        Sleep(10);
    }
    return false;
}

void TestGraphs(const std::string &dir)
{
    struct S_avi {
        const char *name;
        int width, height, bits, frames;
    };
    static const S_avi kAvis[] = {{"ff24.avi", 75, 41, 24, 12}, {"ff32.avi", 130, 70, 32, 8}, {"ff16.avi", 33, 17, 16, 6}};
    int compared = 0;
    for (const S_avi &v : kAvis) {
        const std::string path = dir + v.name;
        WriteAvi(path, v.width, v.height, v.bits, v.frames, 0);
        for (int layout : {32, 565, 555}) {
            S_capture *ca = new S_capture(layout), *cb = new S_capture(layout);
            Graph *ga = g_bink->CreateGraph(), *gb = g_ff->CreateGraph();
            const HRESULT ha = g_bink->RenderToSink(ga, path.c_str(), ca);
            const HRESULT hb = g_ff->RenderToSink(gb, path.c_str(), cb);
            Check(SUCCEEDED(ha) && SUCCEEDED(hb), "%s %d: rendered %08lx / %08lx", v.name, layout, ha, hb);
            int wa = 0, ha2 = 0, wb = 0, hb2 = 0;
            Check(g_bink->VideoSize(ga, &wa, &ha2) && g_ff->VideoSize(gb, &wb, &hb2) && wa == wb && ha2 == hb2 &&
                      wb == v.width && hb2 == v.height,
                  "%s: video size %dx%d / %dx%d", v.name, wa, ha2, wb, hb2);
            // DirectShow without a clock: every frame at once (the backend's graph record starts with the graph).
            IMediaFilter *filter = nullptr;
            IGraphBuilder *builder = *reinterpret_cast<IGraphBuilder **>(ga);
            if (SUCCEEDED(builder->QueryInterface(IID_IMediaFilter, reinterpret_cast<void **>(&filter)))) {
                filter->SetSyncSource(nullptr);
                filter->Release();
            }
            Check(RunToEnd(g_bink, ga, 10000), "%s: DirectShow's graph ends", v.name);
            Check(RunToEnd(g_ff, gb, 10000), "%s: FFmpeg's graph ends", v.name);
            Check(ca->Count() == size_t(v.frames) && cb->Count() == size_t(v.frames), "%s %d: frames %zu / %zu (of %d)",
                  v.name, layout, ca->Count(), cb->Count(), v.frames);
            const uint32_t format = layout == 32 ? MOVIE_RGB32 : layout == 565 ? MOVIE_RGB565 : MOVIE_RGB555;
            for (size_t f = 0; f < ca->frames.size() && f < cb->frames.size(); ++f) {
                S_diff d;
                Compare(ca->frames[f], cb->frames[f], format, d);
                // (DirectShow's colour converter truncates to 5 or 6 bits and widens 5 bits by a shift;
                // swscale rounds and repeats the top bits: one 5-bit step apart.)
                const int allowed = layout == 32 && v.bits != 16 ? 0 : 9;
                Check(d.largest <= allowed, "%s %d frame %zu: largest difference %d", v.name, layout, f, d.largest);
                ++compared;
            }
            g_bink->Stop(ga);
            g_ff->Stop(gb);
            g_bink->ReleaseGraph(ga);       // (the sinks go with their graphs)
            g_ff->ReleaseGraph(gb);
        }
    }
    std::printf("graphs: %d frames compared\n", compared);

    // FFmpeg's graph on its own: states, positions and events.
    S_capture *c = new S_capture(32);
    Graph *g = g_ff->CreateGraph();
    g_ff->RenderToSink(g, (dir + "ff24.avi").c_str(), c);
    g_ff->SetPosition(g, 5000000);      // 0.5 s: frame 5 of 12
    Check(g_ff->GetPosition(g) == 5000000, "position while stopped: %lld", g_ff->GetPosition(g));
    g_ff->Pause(g);
    long code = 0;
    Check(g_ff->NextEvent(g, &code) && code == GRAPH_EVENT_PAUSED, "Pause: the paused event (%ld)", code);
    Check(RunToEnd(g_ff, g, 5000) && c->Count() == 7, "from 0.5 s: %zu frames, expected 7", c->Count());
    if (c->Count() == 7) {
        bool same = true;
        for (int y = 0; y < 41; ++y)
            for (int x = 0; x < 75; ++x)
                for (int ch = 0; ch < 3; ++ch)
                    same &= c->frames[0][size_t(y * 75 + x) * 4 + ch] == AviPixel(x, 40 - y, 5, ch, 24);
        Check(same, "the first frame shown is the file's frame 5");
    }
    Sleep(100);                         // (the graph's thread goes on)
    Check(!g_ff->NextEvent(g, &code), "one end event only");
    g_ff->ReleaseGraph(g);
    // A file with no video and nothing to play its sound: done at once.
    WriteWav(dir + "ff.wav", 300);
    g = g_ff->CreateGraph();
    Check(SUCCEEDED(g_ff->RenderFile(g, (dir + "ff.wav").c_str())), "RenderFile(wav)");
    Check(!g_ff->VideoSize(g, &compared, &compared), "no video size without a sink");
    Check(RunToEnd(g_ff, g, 2000), "a sound-only graph ends");
    g_ff->ReleaseGraph(g);
    g = g_ff->CreateGraph();
    Check(FAILED(g_ff->RenderFile(g, (dir + "missing.avi").c_str())), "a missing file fails");
    g_ff->ReleaseGraph(g);
}

// ---------------------------------------------------------------- a movie's sound

void TestSound(const std::string &movie)
{
    if (!audio::openal::UseLoopback(22050)) {
        Check(false, "OpenAL Soft's loopback device (soft_oal.dll)");
        return;
    }
    audio::Device *dev = audio::OpenALDevice();
    audio::System *system = nullptr;
    dev->Create(nullptr, &system);
    audio::g_pAudio = dev;
    g_ff->UseSound(system);
    // The movie's first 3 s, rendered as the mix asks for them; IShow's calls in between (a frame
    // decoded when one is due; the sound refills then and on each Wait). `ask` false: no calls at all.
    auto play = [&](Movie *m, int chunks, double &rms, int &silent, bool ask = true) {
        double sum = 0;
        long n = 0;
        silent = 0;
        for (int i = 0; i < chunks; ++i) {
            if (ask && !g_ff->Wait(m)) {
                g_ff->DecodeFrame(m);
                g_ff->NextFrame(m);
            }
            if (!ask)
                Sleep(12);              // (the watchdog looks every 10 ms)
            std::vector<float> out(512 * 2);
            audio::openal::RenderLoopback(out.data(), 512);
            double s = 0;
            for (float x : out)
                s += double(x) * x;
            silent += s < 1e-9;
            sum += s;
            n += 1024;
        }
        rms = std::sqrt(sum / n);
    };
    const int chunks = 130;             // (512 frames each at 22.05 kHz)
    double full = 0, half = 0, paused = 0;
    int silentFull = 0, silentHalf = 0, silentPaused = 0;
    Movie *m = g_ff->OpenMovie(movie.c_str());
    Check(m != nullptr, "%s opened with sound", movie.c_str());
    if (!m)
        return;
    play(m, chunks, full, silentFull);
    // Paused while it plays: silent; then on again.
    g_ff->Pause(m, true);
    play(m, 8, paused, silentPaused);   // (the stop fades out)
    play(m, 20, paused, silentPaused);
    Check(paused < 1e-6 && silentPaused == 20, "silent while paused: %g", paused);
    g_ff->Pause(m, false);
    double resumed = 0;
    int silentResumed = 0;
    play(m, 40, resumed, silentResumed);
    Check(resumed > 1e-4, "the sound goes on after the pause: rms %.5f", resumed);
    // IShow stops asking: the second of sound written ahead plays, then silence (not the ring's old
    // sound coming round again).
    double after = 0, late = 0;
    int silentAfter = 0, silentLate = 0;
    play(m, 60, after, silentAfter, false);
    play(m, 100, late, silentLate, false);
    Check(late < 1e-6 && silentLate == 100, "no calls: silent after the sound written (%d of 100 chunks silent)",
          silentLate);
    g_ff->CloseMovie(m);
    // The same 3 s at half the volume (Bink's 16384 of 32768: -6 dB).
    m = g_ff->OpenMovie(movie.c_str());
    g_ff->SetVolume(m, 16384);
    play(m, chunks, half, silentHalf);
    g_ff->CloseMovie(m);
    std::printf("movie sound: rms %.5f, at half volume %.5f (ratio %.4f); %d / %d silent chunks of %d\n", full, half,
                full ? half / full : 0.0, silentFull, silentHalf, chunks);
    Check(full > 1e-4 && silentFull < chunks / 2, "the movie's sound plays: rms %.5f, %d silent chunks", full,
          silentFull);
    Check(full > 0 && std::fabs(half / full - 0.5) < 0.01, "half volume: %.4f of the full", full ? half / full : 0.0);
    dev->Release(system);
}

}  // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const int maxFrames = argc > 1 ? std::atoi(argv[1]) : 150;
    const int maxMovies = argc > 2 ? std::atoi(argv[2]) : 8;
    ls3d::g_logDisabled = true;
    CoInitialize(nullptr);
    g_bink = WindowsVideoDevice();
    g_ff = FFmpegVideoDevice();
    if (!g_ff) {
        std::printf("no FFmpeg (avcodec-61.dll ... next to the test)\n");
        return 2;
    }
    const char *dll = OriginalDllPath(nullptr);
    std::string gameDir = dll;
    gameDir = gameDir.substr(0, gameDir.find_last_of('\\'));
    SetDllDirectoryA(gameDir.c_str());          // (binkw32.dll)

    // The movies, smallest first.
    std::vector<std::pair<DWORD, std::string>> movies;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((gameDir + "\\Gamemovie\\*.bik").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            movies.push_back({fd.nFileSizeLow, gameDir + "\\Gamemovie\\" + fd.cFileName});
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(movies.begin(), movies.end());
    if (movies.empty()) {
        std::printf("no movies in %s\\Gamemovie\n", gameDir.c_str());
        return 2;
    }
    std::mt19937 rng(3);
    S_movieResult total;
    int tested = 0;
    for (const auto &m : movies) {
        if (maxMovies && tested >= maxMovies)
            break;
        TestMovie(m.second, maxFrames, rng, total);
        ++tested;
    }
    std::printf("movies: %d, %d frames, %d seeks: RGB32 %.1f dB (largest difference %d), 16-bit %.1f dB\n", tested,
                total.frames, total.seeks, total.rgb32.Psnr(), total.rgb32.largest, total.rgb16.Psnr());

    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    const std::string work = std::string(tmp) + "ls3df_video\\";
    CreateDirectoryA(work.c_str(), nullptr);
    TestGraphs(work);
    TestSound(movies.back().second);

    std::printf("test_video_ffmpeg: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
