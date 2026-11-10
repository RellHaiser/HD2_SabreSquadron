// The diagnostic event stream (src/diag/events.h) and the engine's markers in it.
//
// 1. The ring alone: off by default (a mark records nothing), capacity rounding, wrap-around (the
//    oldest overwritten and counted), a mark from another thread counted and not recorded, the CSV's
//    header and rows, LS3DF.ini [Diagnostics] read next to the module.
// 2. What a mark costs, off and on (ns, this machine).
// 3. The engine's markers on SDL_GPU and OpenGL: a small capture replayed (its picture read back:
//    CopyRects) and then presented, and the events checked in order: read-back -> run frame (value 1)
//    -> texture upload -> renderer run; present -> run frame (value 0) -> renderer run -> acquire and
//    submit (SDL_GPU) or swap (OpenGL). The picture with the markers on must be the one with them off.
//    A backend that cannot make its device here is NOT RUN, not failed.
// 4. The hitch recorder (E02), on synthetic frames with a stall put in on purpose (the marks are the
//    real ones, the stall a spin): a CPU stall in the client update, a pipeline creation, an upload
//    wait, a swapchain acquire, the game's time between the engine's flips. Each must give exactly one
//    record whose cause is that stage. Also: no record for 300 frames that vary under the thresholds,
//    the record limit (the rest counted), a capture asked for once and its own slow frames not
//    recorded, and what a mark costs with the recorder on.
//
// Usage: test_diag_events.exe   (writes and deletes LS3DF.ini and a CSV next to itself)
//
// test-sources: src/gfx/d3d8/gfx_d3d8.cpp src/gfx/trace/*.cpp src/gfx/sdlgpu/*.cpp src/gfx/fixed/*.cpp src/gfx/opengl/*.cpp
// test-sources: src/core/enhance.cpp src/core/debug.cpp
// test-sources: src/platform/*.cpp src/platform/win32/*.cpp src/platform/sdl3/*.cpp
#include <windows.h>

#include <chrono>
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "render_replay.h"
#include "core/enhance.h"
#include "diag/events.h"
#include "gfx/opengl/opengl_device.h"
#include "gfx/sdlgpu/sdlgpu_device.h"

namespace {

using namespace ls3d;
using namespace ls3d::gfx;
using namespace ls3d::gfx::trace;

int g_failures = 0, g_notRun = 0;

void Check(bool ok, const char *what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAIL");
    g_failures += !ok;
}

std::vector<diag::S_event> Recorded()
{
    std::vector<diag::S_event> out;
    const diag::S_stream &s = diag::g_stream;
    const uint64_t capacity = static_cast<uint64_t>(s.mask) + 1;
    for (uint64_t i = s.written > capacity ? s.written - capacity : 0; i < s.written; ++i)
        out.push_back(s.events[static_cast<uint32_t>(i) & s.mask]);
    return out;
}

std::string NextTo(const char *name)
{
    char path[MAX_PATH];
    return diag::ModuleFilePath(name, path, sizeof path) ? path : name;
}

// ---------------------------------------------------------------- 1. the ring

void RingTests()
{
    diag::Mark(diag::EV_LOOP_BEGIN, 1);
    Check(!diag::Enabled() && diag::g_stream.written == 0, "off by default: a mark records nothing");
    Check(!diag::Start(0) && !diag::Enabled(), "capacity 0 stays off");

    Check(diag::Start(1000) && diag::g_stream.mask + 1 == 1024, "capacity 1000 rounds up to 1024");
    for (uint32_t i = 1; i <= 3000; ++i)
        diag::Mark(diag::EV_LOOP_BEGIN, i, i * 2ull);
    std::vector<diag::S_event> events = Recorded();
    bool ordered = events.size() == 1024 && events.front().id == 3000 - 1024 + 1 && events.back().id == 3000;
    for (size_t i = 1; i < events.size() && ordered; ++i)
        ordered = events[i].id == events[i - 1].id + 1 && events[i].qpc >= events[i - 1].qpc && events[i].value == events[i].id * 2ull;
    Check(ordered, "wrap-around: the last 1024 of 3000 kept, oldest first, times non-decreasing");

    std::thread other([] { diag::Mark(diag::EV_FLIP_BEGIN, 7); });
    other.join();
    Check(diag::g_stream.foreign == 1 && diag::g_stream.written == 3000, "a mark from another thread: counted, not recorded");

    const std::string csv = NextTo("test_diag_events.csv");
    Check(diag::WriteCsv(csv.c_str(), "test"), "CSV written");
    FILE *f = std::fopen(csv.c_str(), "r");
    int comments = 0, rows = 0;
    bool header = false, overwritten = false, names = true;
    char line[256];
    while (f && std::fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            ++comments;
            overwritten |= std::strstr(line, "# overwritten,1976") != nullptr;
        } else if (!header) {
            header = std::strcmp(line, "qpc,type,name,id,value\n") == 0;
        } else {
            ++rows;
            names = names && std::strstr(line, ",1,loop_begin,") != nullptr;
        }
    }
    if (f)
        std::fclose(f);
    DeleteFileA(csv.c_str());
    Check(comments == 7 && header && rows == 1024 && overwritten && names, "CSV: 7 header lines, the columns, 1024 rows, 1976 overwritten");
    diag::Stop();
    Check(!diag::Enabled() && !diag::g_stream.events, "stopped: off, ring freed");

    // LS3DF.ini next to the module.
    const std::string ini = NextTo("LS3DF.ini");
    const bool existed = GetFileAttributesA(ini.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (existed) {
        std::printf("LS3DF.ini next to the test exists already: [Diagnostics] checks NOT RUN\n");
        ++g_notRun;
        return;
    }
    Check(diag::ConfiguredCapacity() == 0, "no LS3DF.ini: off");
    WritePrivateProfileStringA("Diagnostics", "EventBuffer", "5000", ini.c_str());
    Check(diag::ConfiguredCapacity() == 0, "EventBuffer without Events=1: off");
    WritePrivateProfileStringA("Diagnostics", "Events", "1", ini.c_str());
    Check(diag::ConfiguredCapacity() == 5000, "Events=1, EventBuffer=5000: 5000");
    WritePrivateProfileStringA("Diagnostics", "EventBuffer", nullptr, ini.c_str());
    Check(diag::ConfiguredCapacity() == 65536, "Events=1 alone: 65536");
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, ini.c_str());     // (flush the profile cache)
    DeleteFileA(ini.c_str());
}

// ---------------------------------------------------------------- 2. the cost

void CostTests()
{
    using Clock = std::chrono::steady_clock;
    const int n = 10000000;
    auto per = [&](auto body) {
        const auto start = Clock::now();
        for (int i = 0; i < n; ++i)
            body(i);
        return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / n;
    };
    const double off = per([](int i) { diag::Mark(diag::EV_PRESENT_BEGIN, static_cast<uint32_t>(i)); });
    diag::Start(65536);
    const double on = per([](int i) { diag::Mark(diag::EV_PRESENT_BEGIN, static_cast<uint32_t>(i)); });
    diag::Stop();
    LARGE_INTEGER sink = {};
    const double qpc = per([&](int) { LARGE_INTEGER t; QueryPerformanceCounter(&t); sink.QuadPart += t.QuadPart; });
    std::printf("cost per mark (10,000,000 marks): off %.2f ns, on %.2f ns (QueryPerformanceCounter alone %.2f ns)%s\n", off, on,
                qpc, sink.QuadPart == 1 ? " " : "");
    // About 45 marks a game frame (the loop's and the engine's): on, under a microsecond a frame here.
    Check(off < 5.0 && on < 200.0, "cost: off under 5 ns, on under 200 ns a mark");
}

// ---------------------------------------------------------------- 3. the engine's markers

class Writer {
public:
    std::vector<uint8_t> bytes;
    Writer() { bytes.assign(kMagic, kMagic + sizeof kMagic); }
    void Rec(E_op op, std::initializer_list<uint32_t> values, const void *tail = nullptr, size_t tailBytes = 0)
    {
        Put(op);
        Put(static_cast<uint32_t>(values.size() * 4 + tailBytes));
        for (uint32_t v : values)
            Put(v);
        if (tailBytes)
            bytes.insert(bytes.end(), static_cast<const uint8_t *>(tail), static_cast<const uint8_t *>(tail) + tailBytes);
    }
    void Put(uint32_t v) { bytes.insert(bytes.end(), reinterpret_cast<uint8_t *>(&v), reinterpret_cast<uint8_t *>(&v) + 4); }
};

// 64x64: a clear and a textured quad.
std::vector<uint8_t> Scene()
{
    Writer w;
    w.Rec(OP_HEADER, {64, 64, D3DFMT_X8R8G8B8, D3DFMT_D16, 0, 1, 0});
    const uint32_t states[][2] = {{D3DRS_LIGHTING, 0}, {D3DRS_CULLMODE, 1}, {D3DRS_ZENABLE, 0}, {D3DRS_ALPHABLENDENABLE, 0},
                                  {D3DRS_FOGENABLE, 0}, {D3DRS_ALPHATESTENABLE, 0}};
    for (const auto &s : states)
        w.Rec(OP_RENDER_STATE, {s[0], s[1]});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLOROP, D3DTOP_MODULATE});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLORARG1, D3DTA_TEXTURE});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLORARG2, D3DTA_DIFFUSE});
    w.Rec(OP_STAGE_STATE, {1, D3DTSS_COLOROP, D3DTOP_DISABLE});
    const D3DVIEWPORT8 viewport = {0, 0, 64, 64, 0.0f, 1.0f};
    w.Rec(OP_VIEWPORT, {}, &viewport, sizeof viewport);
    w.Rec(OP_VERTEX_SHADER, {D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1});
    w.Rec(OP_DEF_TEXTURE, {1, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED});
    uint32_t texels[16];
    for (int i = 0; i < 16; ++i)
        texels[i] = ((i % 4 + i / 4) & 1) ? 0xffffffffu : 0xffff0000u;
    w.Rec(OP_TEXTURE_DATA, {1, 0, 0, 0, 4, 4}, texels, sizeof texels);
    w.Rec(OP_TEXTURE, {0, 1});
    w.Rec(OP_BEGIN_SCENE, {});
    const float one = 1.0f;
    uint32_t z1;
    std::memcpy(&z1, &one, 4);
    w.Rec(OP_CLEAR, {0, D3DCLEAR_TARGET, 0xff202020u, z1, 0});
    struct S_vertex {
        float x, y, z, rhw;
        DWORD diffuse;
        float u, v;
    };
    const S_vertex v[6] = {{8, 8, 0.5f, 1, 0xffffffffu, 0, 0},  {56, 8, 0.5f, 1, 0xffffffffu, 1, 0},  {8, 56, 0.5f, 1, 0xffffffffu, 0, 1},
                           {56, 8, 0.5f, 1, 0xffffffffu, 1, 0}, {56, 56, 0.5f, 1, 0xffffffffu, 1, 1}, {8, 56, 0.5f, 1, 0xffffffffu, 0, 1}};
    w.Rec(OP_DRAW_UP, {D3DPT_TRIANGLELIST, 2, sizeof(S_vertex)}, v, sizeof v);
    w.Rec(OP_END_SCENE, {});
    w.Rec(OP_PRESENT, {});
    return w.bytes;
}

uint64_t Hash(const replay::S_image &image)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : image.bgr)
        h = (h ^ b) * 0x100000001b3ull;
    return h;
}

// The capture replayed (read back), then presented once more. False: the device could not be made.
bool Replay(Device *device, const std::string &path, bool markers, replay::S_image *picture, std::vector<diag::S_event> *events)
{
    replay::Capture capture;
    if (!capture.Open(path.c_str()))
        return false;
    HWND window = CreateWindowA("LS3DFDiagEvents", "diag", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (markers)
        diag::Start(65536);
    bool ok;
    {
        replay::Replayer r(device, window);
        ok = r.Run(capture) && !r.picture.bgr.empty();
        *picture = r.picture;
        if (ok) {
            device->BeginScene();
            device->EndScene();
            ok = SUCCEEDED(device->Present());
        }
    }
    if (markers) {
        *events = Recorded();
        diag::Stop();
    }
    DestroyWindow(window);
    return ok;
}

// The first event of `type` at or after `from` (and its index in `at`); false if none.
bool Find(const std::vector<diag::S_event> &e, size_t from, uint16_t type, size_t *at, uint64_t value = UINT64_MAX)
{
    for (size_t i = from; i < e.size(); ++i)
        if (e[i].type == type && (value == UINT64_MAX || e[i].value == value)) {
            *at = i;
            return true;
        }
    return false;
}

void BackendTest(const char *name, bool sdlGpu)
{
    S_enhancements e = Enhancements();
    std::snprintf(e.sdlGpuDriver, sizeof e.sdlGpuDriver, "%s", sdlGpu ? "direct3d12" : "");
    SetEnhancements(e);
    Device *device = sdlGpu ? SdlGpuDevice() : OpenGlDevice();
    const std::string path = NextTo("test_diag_events.ls3dframe");
    {
        const std::vector<uint8_t> scene = Scene();
        FILE *f = std::fopen(path.c_str(), "wb");
        std::fwrite(scene.data(), scene.size(), 1, f);
        std::fclose(f);
    }
    replay::S_image off, on;
    std::vector<diag::S_event> none, events;
    if (!device || !Replay(device, path, false, &off, &none)) {
        std::printf("%s: the device cannot be made here: NOT RUN\n", name);
        ++g_notRun;
        DeleteFileA(path.c_str());
        return;
    }
    const bool replayed = Replay(device, path, true, &on, &events);
    DeleteFileA(path.c_str());
    Check(replayed && Hash(on) == Hash(off) && !off.bgr.empty(), (std::string(name) + ": the same picture with the markers on").c_str());

    // The read-back: RunFrame with value 1 inside READBACK, its uploads and its renderer run.
    size_t rb = 0, rf = 0, tu = 0, tue = 0, rr = 0, rre = 0, rfe = 0, rbe = 0;
    const bool readBack = Find(events, 0, diag::EV_READBACK_BEGIN, &rb) && Find(events, rb, diag::EV_RUN_FRAME_BEGIN, &rf, 1) &&
                          Find(events, rf, diag::EV_TEXTURE_UPLOAD_BEGIN, &tu) && Find(events, tu, diag::EV_TEXTURE_UPLOAD_END, &tue) &&
                          Find(events, tue, diag::EV_RENDERER_RUN_BEGIN, &rr) && Find(events, rr, diag::EV_RENDERER_RUN_END, &rre) &&
                          Find(events, rre, diag::EV_RUN_FRAME_END, &rfe) && Find(events, rfe, diag::EV_READBACK_END, &rbe) &&
                          events[tue].value >= 1 && events[rfe].value >= 2 && events[rf].id == events[rfe].id;
    Check(readBack, (std::string(name) + ": read-back -> run frame (1) -> texture upload (>= 1) -> renderer run -> end (>= 2 commands)").c_str());

    // The present: RunFrame with value 0 inside PRESENT, then the backend's acquire and submit, or swap.
    size_t pb = 0, pf = 0, pfe = 0, pe = 0, x = 0, y = 0;
    bool present = Find(events, rbe, diag::EV_PRESENT_BEGIN, &pb) && Find(events, pb, diag::EV_RUN_FRAME_BEGIN, &pf, 0) &&
                   Find(events, pf, diag::EV_RUN_FRAME_END, &pfe) && Find(events, pfe, diag::EV_PRESENT_END, &pe) &&
                   events[pf].id == events[rf].id + 1;
    if (sdlGpu)
        present = present && Find(events, pfe, diag::EV_ACQUIRE_BEGIN, &x) && x < pe && Find(events, x, diag::EV_ACQUIRE_END, &y) &&
                  Find(events, y, diag::EV_SUBMIT_BEGIN, &x) && Find(events, x, diag::EV_SUBMIT_END, &y) && y < pe;
    else
        present = present && Find(events, pfe, diag::EV_GL_SWAP_BEGIN, &x) && Find(events, x, diag::EV_GL_SWAP_END, &y) && y < pe;
    Check(present, (std::string(name) + (sdlGpu ? ": present -> run frame (0) -> acquire -> submit -> end" : ": present -> run frame (0) -> swap -> end")).c_str());

    if (sdlGpu) {
        size_t c = 0, ce = 0;
        Check(Find(events, 0, diag::EV_PIPELINE_CREATE_BEGIN, &c) && Find(events, c, diag::EV_PIPELINE_CREATE_END, &ce),
              "SDL_GPU: a pipeline cache miss marked (a new device's first draws)");
    }
    bool paired = true;
    for (size_t i = 1; i < events.size(); ++i)
        paired = paired && events[i].qpc >= events[i - 1].qpc;
    Check(paired && diag::g_stream.foreign == 0, (std::string(name) + ": times non-decreasing, no marks from other threads").c_str());
    std::printf("%s: %zu events\n", name, events.size());
}

// ---------------------------------------------------------------- 4. the hitch recorder

void SpinMs(double ms)
{
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    do
        QueryPerformanceCounter(&b);
    while (static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart) < ms);
}

std::vector<std::string> ReadLines(const std::string &path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
        lines.push_back(line);
    return lines;
}

struct S_hitchRun {
    std::vector<std::string> records;   // the record lines
    std::string header, end;
    diag::S_hitchTotals totals;
};

// A recorder on a new ring for `frames` frames of `frame(i)`, then stopped; its file read back.
template <typename F>
S_hitchRun RunHitches(const char *stream, diag::E_event boundary, diag::S_hitchConfig config, int frames, F frame,
                      void (*capture)() = nullptr)
{
    S_hitchRun run;
    const std::string path = NextTo("test_hitches.jsonl");
    diag::Start(65536);
    if (!diag::StartHitches(config, stream, boundary, "test_hitches.jsonl", "\"test\":true", capture)) {
        diag::Stop();
        return run;
    }
    for (int i = 0; i < frames; ++i)
        frame(i);
    run.totals = diag::StopHitchesWithTotals();
    diag::Stop();
    for (const std::string &line : ReadLines(path)) {
        if (line.find("\"format\":\"ls3d.hitches\"") != std::string::npos)
            run.header = line;
        else if (line.find("\"end\":true") != std::string::npos)
            run.end = line;
        else if (line.find("\"record\":") != std::string::npos)
            run.records.push_back(line);
    }
    DeleteFileA(path.c_str());
    return run;
}

bool Has(const std::string &line, const char *text) { return line.find(text) != std::string::npos; }

// A game-loop frame: about 4 ms of marked stages, `stallMs` more in the client update.
void GameFrame(int i, double stallMs)
{
    using namespace diag;
    Mark(EV_LOOP_BEGIN, static_cast<uint32_t>(i + 1));
    Mark(EV_INPUT_POLL_BEGIN);
    Mark(EV_INPUT_POLL_END);
    Mark(EV_FRAME_FLOOR_BEGIN);
    SpinMs(1.0);
    Mark(EV_FRAME_FLOOR_END, 0, 10);
    Mark(EV_RENDER_RECORD_BEGIN);
    SpinMs(1.5);
    Mark(EV_RENDER_RECORD_END);
    Mark(EV_UPDATE_CLIENT_BEGIN);
    Mark(EV_INPUT_CONSUMED, 0, 1);
    SpinMs(0.5 + stallMs);
    Mark(EV_UPDATE_CLIENT_END);
    Mark(EV_FLIP_CALL_BEGIN, static_cast<uint32_t>(i + 1));
    SpinMs(1.0);
    Mark(EV_FLIP_CALL_END, static_cast<uint32_t>(i + 1));
    Mark(EV_LOOP_END, static_cast<uint32_t>(i + 1));
}

// An engine frame (SDL_GPU's marks): `stall` ms in the stage `where`, or between flips (0).
void EngineFrame(int i, diag::E_event where, double stallMs)
{
    using namespace diag;
    const uint32_t id = static_cast<uint32_t>(i + 1);
    auto stage = [&](E_event begin, double ms) {
        Mark(begin);
        SpinMs(ms + (begin == where ? stallMs : 0.0));
        Mark(static_cast<E_event>(begin + 1));
    };
    Mark(EV_FLIP_BEGIN, id);
    Mark(EV_PRESENT_BEGIN, id);
    Mark(EV_RUN_FRAME_BEGIN, id, 0);
    Mark(EV_TEXTURE_UPLOAD_BEGIN, id);
    stage(EV_UPLOAD_WAIT_BEGIN, 0.0);
    Mark(EV_TEXTURE_UPLOAD_END, id, 1);
    Mark(EV_RENDERER_RUN_BEGIN, id);
    SpinMs(0.5);
    stage(EV_PIPELINE_CREATE_BEGIN, 0.0);
    Mark(EV_RENDERER_RUN_END, id);
    Mark(EV_RUN_FRAME_END, id, 100);
    stage(EV_ACQUIRE_BEGIN, 0.2);
    stage(EV_SUBMIT_BEGIN, 0.1);
    Mark(EV_PRESENT_END, id);
    Mark(EV_FLIP_END, id);
    SpinMs(2.0 + (where == 0 ? stallMs : 0.0));        // the game's part of the frame
}

int g_captures = 0;
void CountCapture() { ++g_captures; }

void HitchTests()
{
    diag::S_hitchConfig c;
    c.gapMs = 50;
    c.waitMs = 20;
    c.beforeMs = 200;
    c.afterMs = 60;
    c.maxRecords = 50;

    // A CPU stall in the client update (the game's stream).
    S_hitchRun r = RunHitches("game", diag::EV_LOOP_BEGIN, c, 40, [](int i) { GameFrame(i, i == 20 ? 80.0 : 0.0); });
    Check(r.records.size() == 1 && Has(r.records[0], "\"cause\":\"update_client\"") && Has(r.records[0], "\"kind\":\"stage\",\"event\":\"update_client\"") &&
              Has(r.header, "\"stream\":\"game\"") && Has(r.end, "\"records\":1"),
          "hitch: an 80 ms client update -> one record, cause update_client");
    if (!r.records.empty()) {
        // The window: the slow frame's own loop_begin (iteration 21) and frames after it.
        Check(Has(r.records[0], ",1,21,0]") && Has(r.records[0], ",1,23,0]") && Has(r.records[0], "\"merged\":1"),
              "hitch: the window holds the slow frame and the frames after it; the frame-gap trigger joined it");
        std::printf("    %s\n", r.records[0].substr(0, std::min<size_t>(r.records[0].find("\"events\""), 400)).c_str());
    }

    // The engine's waits and creations, each stalled 30 ms in one frame.
    struct S_case {
        diag::E_event where;
        const char *cause;
    } cases[] = {{diag::EV_PIPELINE_CREATE_BEGIN, "\"cause\":\"pipeline_create\""},
                 {diag::EV_UPLOAD_WAIT_BEGIN, "\"cause\":\"upload_wait\""},
                 {diag::EV_ACQUIRE_BEGIN, "\"cause\":\"acquire\""},
                 {static_cast<diag::E_event>(0), "\"cause\":\"outside_engine_flip\""}};
    for (const S_case &k : cases) {
        const double stall = k.where ? 30.0 : 80.0;
        r = RunHitches("engine", diag::EV_FLIP_BEGIN, c, 40, [&](int i) { EngineFrame(i, k.where, i == 20 ? stall : 0.0); });
        Check(r.records.size() == 1 && Has(r.records[0], k.cause),
              (std::string("hitch: ") + (k.where ? diag::EventName(k.where) : "game time between flips") + " stalled -> one record, " +
               k.cause).c_str());
        if (r.records.size() != 1)
            for (const std::string &line : r.records)
                std::printf("    %s\n", line.substr(0, 300).c_str());
    }

    // Two different slow frames in one window (seen in the game: a frame gap, then 232 ms of texture
    // uploads in the next flip, merged): the record lists both, each with its own cause.
    r = RunHitches("engine", diag::EV_FLIP_BEGIN, c, 40, [](int i) {
        EngineFrame(i, i == 22 ? diag::EV_PIPELINE_CREATE_BEGIN : static_cast<diag::E_event>(0), i == 20 ? 80.0 : i == 22 ? 30.0 : 0.0);
    });
    {
        const std::string &line = r.records.empty() ? std::string() : r.records[0];
        const size_t slow = line.find("\"slow_frames\":[");
        const std::string frames = slow == std::string::npos ? std::string() : line.substr(slow, line.find("]}]", slow) - slow);
        Check(r.records.size() == 1 && Has(line, "\"cause\":\"outside_engine_flip\"") && Has(frames, "\"cause\":\"outside_engine_flip\"") &&
                  Has(frames, "\"cause\":\"pipeline_create\""),
              "hitch: a frame gap and a slow pipeline two frames later -> one record, slow_frames names both causes");
    }

    // No hitch: 300 frames from 2 to 15 ms apart, waits up to 10 ms.
    r = RunHitches("engine", diag::EV_FLIP_BEGIN, c, 300, [](int i) {
        EngineFrame(i, (i % 7) ? diag::EV_ACQUIRE_BEGIN : static_cast<diag::E_event>(0), static_cast<double>((i * 37) % 11));
    });
    Check(r.records.empty() && Has(r.end, "\"records\":0,\"skipped_over_limit\":0,\"dropped_no_slot\":0"),
          "hitch: 300 frames under the thresholds -> no record");

    // The limit: 2 records, 3 more hitches counted.
    diag::S_hitchConfig two = c;
    two.maxRecords = 2;
    r = RunHitches("game", diag::EV_LOOP_BEGIN, two, 50, [](int i) { GameFrame(i, i % 10 == 5 ? 60.0 : 0.0); });
    Check(r.records.size() == 2 && r.totals.records == 2 && r.totals.skipped == 3 && Has(r.end, "\"skipped_over_limit\":3"),
          "hitch: HitchFiles=2 -> 2 records, 3 hitches counted over the limit");
    if (r.records.size() != 2 || r.totals.skipped != 3)
        std::printf("    records %zu (totals %u), skipped %u, dropped %u; end: %s\n", r.records.size(), r.totals.records, r.totals.skipped,
                    r.totals.dropped, r.end.c_str());

    // A capture asked for once; the hitch right after it (the capture's own) is not recorded.
    diag::S_hitchConfig capture = c;
    capture.captures = 1;
    g_captures = 0;
    r = RunHitches("engine", diag::EV_FLIP_BEGIN, capture, 60, [](int i) {
        EngineFrame(i, static_cast<diag::E_event>(0), i == 10 || i == 30 ? 80.0 : (i == 11 ? 120.0 : 0.0));
    }, &CountCapture);
    Check(g_captures == 1 && r.records.size() == 2 && Has(r.records[0], "\"capture_requested\":true") &&
              Has(r.records[1], "\"capture_requested\":false"),
          "hitch: HitchCapture=1 -> one capture asked for, the next slow frame suppressed, a later hitch recorded");

    // The cost of a mark with the recorder on (no hitch).
    {
        using Clock = std::chrono::steady_clock;
        diag::Start(65536);
        diag::S_hitchConfig quiet = c;
        diag::StartHitches(quiet, "engine", diag::EV_FLIP_BEGIN, "test_hitches.jsonl", "");
        const int n = 10000000;
        const auto start = Clock::now();
        for (int i = 0; i < n; ++i)
            diag::Mark((i & 1) ? diag::EV_RUN_FRAME_END : diag::EV_RUN_FRAME_BEGIN, static_cast<uint32_t>(i));
        const double ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / n;
        const diag::S_hitchTotals totals = diag::StopHitchesWithTotals();
        diag::Stop();
        DeleteFileA(NextTo("test_hitches.jsonl").c_str());
        std::printf("cost per mark with the hitch recorder on: %.2f ns\n", ns);
        Check(ns < 200.0 && totals.records == 0, "hitch: a mark with the recorder on under 200 ns, no record");
    }
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    RingTests();
    CostTests();
    HitchTests();
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "LS3DFDiagEvents";
    RegisterClassA(&wc);
    BackendTest("SDL_GPU (Direct3D 12)", true);
    BackendTest("OpenGL", false);
    std::printf("test_diag_events: %d failed, %d not run\n", g_failures, g_notRun);
    return g_failures ? 1 : 0;
}
