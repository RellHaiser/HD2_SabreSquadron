// Diagnostic events: the one schema of the low-latency Stage A markers (docs: niche_engine_features
// docs/latency). Two streams use it, one in each module: LS3DF.dll (the engine's markers) and
// HD2_Port.exe (the game loop's), each with its own ring. Both stamp QueryPerformanceCounter, the
// clock std::chrono::steady_clock and the game's MicrosecondsNow already use, so the two files merge
// by time exactly, with nothing passed across the DLL boundary.
//
// Off unless LS3DF.ini (next to the module) says
//   [Diagnostics]
//   Events=1                 ; both streams: markers into a ring, written out when the module closes
//   EventBuffer=65536        ; events a ring holds (rounded up to a power of two, 1024 - 4194304); 24 bytes each
//   Hitches=1                ; with Events: the hitch recorder (default 1); 0: the ring alone
//   HitchMs=50               ; a hitch: frames this far apart, or a stage of a frame this long
//   HitchWaitMs=20           ; ... or a wait or creation this long (upload wait, pipeline, acquire, swap, uploads)
//   HitchBeforeMs=2000       ; events kept before a hitch (and always the slow frame's start)
//   HitchAfterMs=500         ; and after it
//   HitchFiles=50            ; hitches written at most in a session (then only counted)
//   HitchCapture=0           ; the engine: that many hitches also save the next frame (needs [Render] CaptureFrames=1)
//
// The hitch recorder (E02) watches the marks as they are made: a frame gap or a stage over its threshold
// opens a window; when the window's "after" has passed, the main thread copies the window's events into
// one of a few slots made at the start (a memcpy), and a writer thread of its own formats it and appends
// one JSON line to LS3DF_hitches.jsonl / HD2_hitches.jsonl: the trigger, the slow frame's stages by
// their own (exclusive) time, the largest of them as the cause, and the events. A trigger while a window
// is open joins it; no free slot: the hitch is counted as dropped. Nothing is written on the main thread.
//
// A ring has one producer, the thread that started it (the main thread: the game loop, the window,
// the renderer). A mark is a QueryPerformanceCounter and a 24-byte store: no lock, no allocation, no
// formatting, no file. A mark from another thread is not recorded, only counted. When the ring is
// full the oldest events are overwritten (it is a flight recorder) and counted. Off, a mark is one
// test of a bool.
//
// Header only: each module that includes it has its own stream (an inline variable is one per module).
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace ls3d::diag {

// The event types. Values are the file format: never renumber, only add. "id" and "value" are what
// the comment says; 0 otherwise. Game events are 1 - 63, engine events 64 - 127.
enum E_event : uint16_t {
    // ---- HD2_Port.exe, the main loop (src/game/app.cpp), one iteration
    EV_LOOP_BEGIN = 1,              // id: the iteration (1, 2, ...)
    EV_INPUT_POLL_BEGIN = 2,        // window messages and the keyboard snapshot (may wait while inactive)
    EV_INPUT_POLL_END = 3,
    EV_FRAME_FLOOR_BEGIN = 4,       // LimitFrameTime: the minimum frame time's sleep
    EV_FRAME_FLOOR_END = 5,         // value: the frame time it returned, ms
    EV_CONTROLS_BEGIN = 6,          // BeginFrame (the mouse snapshot), frame listeners, controls
    EV_CONTROLS_END = 7,
    EV_ENGINE_TICK_BEGIN = 8,       // I3D driver Tick
    EV_ENGINE_TICK_END = 9,
    EV_RENDER_RECORD_BEGIN = 10,    // RenderState: the frame's drawing calls
    EV_RENDER_RECORD_END = 11,
    EV_NET_RECEIVE_BEGIN = 12,      // value: 1 before the server's update, 0 before the client's
    EV_NET_RECEIVE_END = 13,
    EV_UPDATE_SERVER_BEGIN = 14,
    EV_UPDATE_SERVER_END = 15,
    EV_UPDATE_CLIENT_BEGIN = 16,    // the simulation (and HandleInput in it)
    EV_UPDATE_CLIENT_END = 17,
    EV_INPUT_CONSUMED = 18,         // HandleInput: the controls handed to the game state
    EV_NET_SEND_BEGIN = 19,
    EV_NET_SEND_END = 20,
    EV_FLIP_CALL_BEGIN = 21,        // the loop's IGraph::Flip call
    EV_FLIP_CALL_END = 22,
    EV_LOOP_END = 23,               // id: the iteration

    // ---- LS3DF.dll
    EV_FLIP_BEGIN = 64,             // IGraph::Flip, any caller; id: the flip (1, 2, ...)
    EV_FLIP_END = 65,
    EV_PRESENT_BEGIN = 66,          // fixed renderer Present (SDL_GPU, OpenGL); id: the present
    EV_PRESENT_END = 67,
    EV_RUN_FRAME_BEGIN = 68,        // RunFrame; id: the render job; value: 0 for a present, 1 for a read-back
    EV_RUN_FRAME_END = 69,          // value: the frame's commands
    EV_TEXTURE_UPLOAD_BEGIN = 70,   // RunFrame's dirty textures to the renderer
    EV_TEXTURE_UPLOAD_END = 71,     // value: textures uploaded
    EV_RENDERER_RUN_BEGIN = 72,     // the renderer's Run (SDL_GPU: recording the command buffer; OpenGL: the GL calls)
    EV_RENDERER_RUN_END = 73,
    EV_ACQUIRE_BEGIN = 74,          // SDL_GPU: waiting for and acquiring the swapchain texture
    EV_ACQUIRE_END = 75,
    EV_GL_SWAP_BEGIN = 76,          // OpenGL: SDL_GL_SwapWindow (the existing stats call this "the swapchain")
    EV_GL_SWAP_END = 77,
    EV_SUBMIT_BEGIN = 78,           // SDL_GPU: the frame's command buffer submitted
    EV_SUBMIT_END = 79,
    EV_UPLOAD_WAIT_BEGIN = 80,      // SDL_GPU: waiting for the oldest staging batch (over the in-flight bound)
    EV_UPLOAD_WAIT_END = 81,
    EV_PIPELINE_CREATE_BEGIN = 82,  // SDL_GPU: a pipeline cache miss, SDL_CreateGPUGraphicsPipeline
    EV_PIPELINE_CREATE_END = 83,
    EV_READBACK_BEGIN = 84,         // a back-buffer read (CopyRects: screenshots, captures), no present
    EV_READBACK_END = 85,
};

inline const char *EventName(uint16_t type)
{
    switch (type) {
    case EV_LOOP_BEGIN: return "loop_begin";
    case EV_INPUT_POLL_BEGIN: return "input_poll_begin";
    case EV_INPUT_POLL_END: return "input_poll_end";
    case EV_FRAME_FLOOR_BEGIN: return "frame_floor_begin";
    case EV_FRAME_FLOOR_END: return "frame_floor_end";
    case EV_CONTROLS_BEGIN: return "controls_begin";
    case EV_CONTROLS_END: return "controls_end";
    case EV_ENGINE_TICK_BEGIN: return "engine_tick_begin";
    case EV_ENGINE_TICK_END: return "engine_tick_end";
    case EV_RENDER_RECORD_BEGIN: return "render_record_begin";
    case EV_RENDER_RECORD_END: return "render_record_end";
    case EV_NET_RECEIVE_BEGIN: return "net_receive_begin";
    case EV_NET_RECEIVE_END: return "net_receive_end";
    case EV_UPDATE_SERVER_BEGIN: return "update_server_begin";
    case EV_UPDATE_SERVER_END: return "update_server_end";
    case EV_UPDATE_CLIENT_BEGIN: return "update_client_begin";
    case EV_UPDATE_CLIENT_END: return "update_client_end";
    case EV_INPUT_CONSUMED: return "input_consumed";
    case EV_NET_SEND_BEGIN: return "net_send_begin";
    case EV_NET_SEND_END: return "net_send_end";
    case EV_FLIP_CALL_BEGIN: return "flip_call_begin";
    case EV_FLIP_CALL_END: return "flip_call_end";
    case EV_LOOP_END: return "loop_end";
    case EV_FLIP_BEGIN: return "flip_begin";
    case EV_FLIP_END: return "flip_end";
    case EV_PRESENT_BEGIN: return "present_begin";
    case EV_PRESENT_END: return "present_end";
    case EV_RUN_FRAME_BEGIN: return "run_frame_begin";
    case EV_RUN_FRAME_END: return "run_frame_end";
    case EV_TEXTURE_UPLOAD_BEGIN: return "texture_upload_begin";
    case EV_TEXTURE_UPLOAD_END: return "texture_upload_end";
    case EV_RENDERER_RUN_BEGIN: return "renderer_run_begin";
    case EV_RENDERER_RUN_END: return "renderer_run_end";
    case EV_ACQUIRE_BEGIN: return "acquire_begin";
    case EV_ACQUIRE_END: return "acquire_end";
    case EV_GL_SWAP_BEGIN: return "gl_swap_begin";
    case EV_GL_SWAP_END: return "gl_swap_end";
    case EV_SUBMIT_BEGIN: return "submit_begin";
    case EV_SUBMIT_END: return "submit_end";
    case EV_UPLOAD_WAIT_BEGIN: return "upload_wait_begin";
    case EV_UPLOAD_WAIT_END: return "upload_wait_end";
    case EV_PIPELINE_CREATE_BEGIN: return "pipeline_create_begin";
    case EV_PIPELINE_CREATE_END: return "pipeline_create_end";
    case EV_READBACK_BEGIN: return "readback_begin";
    case EV_READBACK_END: return "readback_end";
    default: return "unknown";
    }
}

struct S_event {
    int64_t qpc;                    // QueryPerformanceCounter ticks (the frequency is in the file's header)
    uint64_t value;
    uint32_t id;
    uint16_t type;                  // E_event
    uint16_t reserved;
};
static_assert(sizeof(S_event) == 24, "the event is 24 bytes");

struct S_hitches;

struct S_stream {
    S_hitches *hitches = nullptr;   // the hitch recorder, when on
    S_event *events = nullptr;
    uint32_t mask = 0;              // capacity - 1
    uint64_t written = 0;           // events recorded since Start (the ring holds the last capacity of them)
    volatile LONG foreign = 0;      // marks from another thread, not recorded
    DWORD thread = 0;               // the producer
    bool on = false;
};
inline S_stream g_stream;           // (one per module)

inline bool Enabled() { return g_stream.on; }
inline void HitchStep(uint16_t type, uint32_t id, int64_t qpc);

// A marker. Off: nothing. From a thread that is not the producer: counted, not recorded.
inline void Mark(E_event type, uint32_t id = 0, uint64_t value = 0)
{
    S_stream &s = g_stream;
    if (!s.on)
        return;
    if (GetCurrentThreadId() != s.thread) {
        InterlockedIncrement(&s.foreign);
        return;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    S_event &e = s.events[static_cast<uint32_t>(s.written) & s.mask];
    e.qpc = now.QuadPart;
    e.value = value;
    e.id = id;
    e.type = type;
    e.reserved = 0;
    ++s.written;
    if (s.hitches)
        HitchStep(type, id, now.QuadPart);
}

// The ring made, on the calling thread (its producer). `capacity` 0: stays off. Allocates once.
inline bool Start(uint32_t capacity)
{
    S_stream &s = g_stream;
    if (s.on || !capacity)
        return s.on;
    uint32_t n = 1024;
    while (n < capacity && n < (1u << 22))
        n <<= 1;
    s.events = static_cast<S_event *>(std::calloc(n, sizeof(S_event)));
    if (!s.events)
        return false;
    s.mask = n - 1;
    s.written = 0;
    s.foreign = 0;
    s.thread = GetCurrentThreadId();
    s.on = true;
    return true;
}

inline void StopHitches();

inline void Stop()
{
    S_stream &s = g_stream;
    StopHitches();
    s.on = false;
    std::free(s.events);
    s.events = nullptr;
    s.mask = 0;
}

// The events in the ring, oldest first, as CSV with a commented header (the format's version, the
// stream, the clock's frequency and what was lost). A cold path: call it at a safe point.
inline bool WriteCsv(const char *path, const char *stream)
{
    const S_stream &s = g_stream;
    if (!s.events)
        return false;
    FILE *f = std::fopen(path, "w");
    if (!f)
        return false;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const uint64_t capacity = static_cast<uint64_t>(s.mask) + 1;
    const uint64_t first = s.written > capacity ? s.written - capacity : 0;
    std::fprintf(f, "# ls3d diagnostic events, version 1\n# stream,%s\n# qpc_frequency,%lld\n", stream,
                 static_cast<long long>(frequency.QuadPart));
    std::fprintf(f, "# capacity,%llu\n# recorded,%llu\n# overwritten,%llu\n# other_thread_marks,%ld\n",
                 static_cast<unsigned long long>(capacity), static_cast<unsigned long long>(s.written),
                 static_cast<unsigned long long>(first), static_cast<long>(s.foreign));
    std::fprintf(f, "qpc,type,name,id,value\n");
    for (uint64_t i = first; i < s.written; ++i) {
        const S_event &e = s.events[static_cast<uint32_t>(i) & s.mask];
        std::fprintf(f, "%lld,%u,%s,%u,%llu\n", static_cast<long long>(e.qpc), e.type, EventName(e.type), e.id,
                     static_cast<unsigned long long>(e.value));
    }
    return std::fclose(f) == 0;
}

// `name` next to the module that includes this header (the DLL or the EXE); false if it does not fit.
inline bool ModuleFilePath(const char *name, char *path, size_t size)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&g_stream), &module))
        return false;
    const DWORD n = GetModuleFileNameA(module, path, static_cast<DWORD>(size));
    if (!n || n >= size)
        return false;
    char *slash = std::strrchr(path, '\\');
    if (!slash || static_cast<size_t>(slash + 1 - path) + std::strlen(name) + 1 > size)
        return false;
    std::strcpy(slash + 1, name);
    return true;
}

// [Diagnostics] Events / EventBuffer of the LS3DF.ini next to the module: the ring's capacity, 0 when off.
inline uint32_t ConfiguredCapacity()
{
    char ini[MAX_PATH];
    if (!ModuleFilePath("LS3DF.ini", ini, sizeof ini) || GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (!GetPrivateProfileIntA("Diagnostics", "Events", 0, ini))
        return 0;
    const int n = static_cast<int>(GetPrivateProfileIntA("Diagnostics", "EventBuffer", 65536, ini));
    return n < 1024 ? 1024u : n > (1 << 22) ? (1u << 22) : static_cast<uint32_t>(n);
}

// ---------------------------------------------------------------- the hitch recorder (E02)

struct S_hitchConfig {
    uint32_t gapMs = 50;            // HitchMs: frames apart (and a stage of a frame); 0: no recorder
    uint32_t waitMs = 20;           // HitchWaitMs: waits and creations
    uint32_t beforeMs = 2000, afterMs = 500;
    uint32_t maxRecords = 50;       // HitchFiles
    uint32_t captures = 0;          // HitchCapture (the engine's)
};

// [Diagnostics] of the LS3DF.ini next to the module; false: no recorder (Events or Hitches off).
inline bool ConfiguredHitches(S_hitchConfig *c)
{
    *c = S_hitchConfig();
    char ini[MAX_PATH];
    if (!ModuleFilePath("LS3DF.ini", ini, sizeof ini) || GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES ||
        !GetPrivateProfileIntA("Diagnostics", "Events", 0, ini) || !GetPrivateProfileIntA("Diagnostics", "Hitches", 1, ini))
        return false;
    auto get = [&](const char *key, uint32_t fallback, uint32_t low, uint32_t high) {
        const int v = static_cast<int>(GetPrivateProfileIntA("Diagnostics", key, static_cast<int>(fallback), ini));
        return v < static_cast<int>(low) ? low : v > static_cast<int>(high) ? high : static_cast<uint32_t>(v);
    };
    c->gapMs = get("HitchMs", 50, 0, 60000);
    c->waitMs = get("HitchWaitMs", 20, 1, 60000);
    c->beforeMs = get("HitchBeforeMs", 2000, 0, 60000);
    c->afterMs = get("HitchAfterMs", 500, 20, 60000);     // (at least long enough for a stall's frame gap to join it)
    c->maxRecords = get("HitchFiles", 50, 0, 100000);
    c->captures = get("HitchCapture", 0, 0, 100);
    return c->gapMs > 0;
}

struct S_hitchTrigger {
    uint16_t type = 0;              // the event that tripped it (a frame start or a stage's end)
    uint8_t gap = 0;                // 1: frames apart; 0: a stage
    uint8_t capture = 0;            // the next frame's capture was asked for
    uint32_t id = 0, merged = 0;    // merged: triggers that joined this window
    int64_t qpc = 0, ticks = 0;     // when, and how long (the gap or the stage)
    int64_t frameStart = 0;         // the slow frame's start (its frame boundary)
};

struct S_hitchSlot {
    volatile LONG state = 0;        // 0 free, 1 ready for the writer, 2 being written
    S_hitchTrigger trigger;
    uint32_t record = 0, count = 0, truncated = 0;
    uint8_t ringShort = 0;          // the window began before the oldest event the ring still had
    S_event *events = nullptr;
};

struct S_hitches {
    static constexpr int kSlots = 4;
    static constexpr uint32_t kSlotEvents = 16384;
    S_hitchConfig config;
    uint8_t kind[128] = {};         // 1 frame boundary, 2 a stage's end, 3 a wait's end
    int64_t lastBegin[128] = {};
    int64_t lastBoundary = 0, gapTicks = 0, waitTicks = 0, beforeTicks = 0, afterTicks = 0, frequency = 1;
    bool pending = false;
    S_hitchTrigger trigger;
    int64_t pendingUntil = 0, skipUntil = 0;
    uint32_t records = 0, skipped = 0, dropped = 0, capturesLeft = 0, suppress = 0;
    void (*requestCapture)() = nullptr;
    S_hitchSlot slots[kSlots];
    HANDLE wake = nullptr, thread = nullptr;
    volatile LONG quit = 0;
    char path[MAX_PATH] = {};
    char stream[16] = {};
    char header[2048] = {};         // the file's first line
    bool headerWritten = false;
};

inline const char *SpanName(uint16_t endType, char *buffer, size_t size)
{
    const char *name = EventName(endType);
    const size_t n = std::strlen(name);
    const size_t keep = n > 4 && std::strcmp(name + n - 4, "_end") == 0 ? n - 4 : n;
    std::snprintf(buffer, size, "%.*s", static_cast<int>(keep), name);
    return buffer;
}

// What a frame's time was spent on, from the events of [start, end): each stage's own time (minus the
// stages nested in it) and its whole time, largest own time first; the time in no stage is "unmarked"
// (the game) or "outside_engine_flip" (the engine: between flips is the game's).
struct S_attribution {
    struct S_row {
        double own, whole;
        uint16_t type;              // 0: the time in no stage
        char name[40];
    } rows[130];
    int count = 0;
};

inline void Attribute(const S_hitchSlot &slot, int64_t start, int64_t end, bool engine, double toMs, S_attribution *a)
{
    struct S_open {
        uint16_t type;
        int64_t begin, children;
    } stack[32];
    int depth = 0;
    double own[128] = {}, whole[128] = {};
    int64_t topLevel = 0;
    for (uint32_t i = 0; i < slot.count; ++i) {
        const S_event &e = slot.events[i];
        if (e.qpc < start || e.qpc > end || e.type >= 128)
            continue;
        const char *name = EventName(e.type);
        const size_t n = std::strlen(name);
        if (n > 6 && std::strcmp(name + n - 6, "_begin") == 0 && e.type != EV_LOOP_BEGIN) {
            if (depth < 32)
                stack[depth++] = {e.type, e.qpc, 0};
        } else if (n > 4 && std::strcmp(name + n - 4, "_end") == 0 && e.type != EV_LOOP_END) {
            if (depth > 0 && stack[depth - 1].type == e.type - 1) {
                const S_open o = stack[--depth];
                const int64_t d = e.qpc - o.begin;
                whole[e.type] += static_cast<double>(d) * toMs;
                own[e.type] += static_cast<double>(d - o.children) * toMs;
                if (depth > 0)
                    stack[depth - 1].children += d;
                else
                    topLevel += d;
            }
        }
    }
    // (The engine's flip is a stage too: its own time is the flip's outside its stages, and what is
    // outside every flip is the game's, between flips.)
    a->count = 0;
    for (int t = 0; t < 128; ++t)
        if (whole[t] > 0.0) {
            S_attribution::S_row &r = a->rows[a->count++];
            r.own = own[t];
            r.whole = whole[t];
            r.type = static_cast<uint16_t>(t);
            SpanName(static_cast<uint16_t>(t), r.name, sizeof r.name);
        }
    S_attribution::S_row &rest = a->rows[a->count++];
    rest.own = rest.whole = static_cast<double>(end - start - topLevel) * toMs;
    rest.type = 0;
    std::snprintf(rest.name, sizeof rest.name, "%s", engine ? "outside_engine_flip" : "unmarked");
    for (int i = 1; i < a->count; ++i)
        for (int j = i; j > 0 && a->rows[j].own > a->rows[j - 1].own; --j) {
            const S_attribution::S_row r = a->rows[j];
            a->rows[j] = a->rows[j - 1];
            a->rows[j - 1] = r;
        }
}

inline void WriteAttribution(FILE *f, const S_hitchSlot &slot, int64_t start, int64_t end, bool engine, double toMs)
{
    S_attribution a;
    Attribute(slot, start, end, engine, toMs, &a);
    std::fprintf(f, "\"cause\":\"%s\",\"attribution\":[", a.rows[0].name);
    for (int i = 0; i < a.count; ++i)
        std::fprintf(f, "%s{\"stage\":\"%s\",\"own_ms\":%.3f,\"total_ms\":%.3f}", i ? "," : "", a.rows[i].name, a.rows[i].own, a.rows[i].whole);
    std::fprintf(f, "]");
}

// The slow frames from the trigger's on, its own and those whose triggers joined it (the frames
// before it were an earlier record's): frames over the hitch threshold, or holding a stage or a wait
// over its own. Each with its cause and largest stages.
inline void WriteSlowFrames(FILE *f, const S_hitches &h, const S_hitchSlot &slot, bool engine, double toMs)
{
    const int64_t from = slot.trigger.frameStart;
    const double gapMs = static_cast<double>(h.config.gapMs), waitMs = static_cast<double>(h.config.waitMs);
    std::fprintf(f, "\"slow_frames\":[");
    int written = 0;
    int64_t previous = 0;
    for (uint32_t i = 0; i < slot.count; ++i) {
        const S_event &e = slot.events[i];
        if (e.type >= 128 || h.kind[e.type] != 1)
            continue;
        if (previous && previous >= from) {
            S_attribution a;
            Attribute(slot, previous, e.qpc, engine, toMs, &a);
            const double ms = static_cast<double>(e.qpc - previous) * toMs;
            bool slow = ms > gapMs;
            for (int r = 0; r < a.count && !slow; ++r)
                if (a.rows[r].type && h.kind[a.rows[r].type] >= 2)
                    slow = a.rows[r].whole > (h.kind[a.rows[r].type] == 2 ? gapMs : waitMs);
            if (slow) {
                std::fprintf(f, "%s{\"start_qpc\":%lld,\"ms\":%.3f,\"cause\":\"%s\",\"top\":[", written ? "," : "",
                             static_cast<long long>(previous), ms, a.rows[0].name);
                for (int r = 0; r < a.count && r < 3; ++r)
                    std::fprintf(f, "%s{\"stage\":\"%s\",\"own_ms\":%.3f}", r ? "," : "", a.rows[r].name, a.rows[r].own);
                std::fprintf(f, "]}");
                ++written;
            }
        }
        previous = e.qpc;
    }
    std::fprintf(f, "]");
}

inline void WriteRecord(S_hitches &h, const S_hitchSlot &slot)
{
    FILE *f = std::fopen(h.path, "a");
    if (!f)
        return;
    if (!h.headerWritten) {
        std::fprintf(f, "%s\n", h.header);
        h.headerWritten = true;
    }
    const double toMs = 1000.0 / static_cast<double>(h.frequency);
    const S_hitchTrigger &t = slot.trigger;
    // The slow frame: from its boundary to the next one in the window (else the window's end).
    int64_t end = slot.count ? slot.events[slot.count - 1].qpc : t.qpc;
    for (uint32_t i = 0; i < slot.count; ++i)
        if (slot.events[i].qpc > t.frameStart && h.kind[slot.events[i].type & 127] == 1) {
            end = slot.events[i].qpc;
            break;
        }
    if (t.gap)
        end = t.qpc;
    char stage[40];
    std::fprintf(f, "{\"record\":%u,\"trigger\":{\"kind\":\"%s\",\"event\":\"%s\",\"id\":%u,\"ms\":%.3f,\"qpc\":%lld,\"merged\":%u},",
                 slot.record, t.gap ? "frame_gap" : "stage", t.gap ? EventName(t.type) : SpanName(t.type, stage, sizeof stage), t.id,
                 static_cast<double>(t.ticks) * toMs, static_cast<long long>(t.qpc), t.merged);
    std::fprintf(f, "\"frame\":{\"start_qpc\":%lld,\"end_qpc\":%lld,\"ms\":%.3f},", static_cast<long long>(t.frameStart),
                 static_cast<long long>(end), static_cast<double>(end - t.frameStart) * toMs);
    WriteAttribution(f, slot, t.frameStart, end, std::strcmp(h.stream, "engine") == 0, toMs);
    std::fprintf(f, ",");
    WriteSlowFrames(f, h, slot, std::strcmp(h.stream, "engine") == 0, toMs);
    std::fprintf(f, ",\"capture_requested\":%s,\"window\":{\"events\":%u,\"truncated\":%u,\"ring_short\":%s},\"events\":[",
                 t.capture ? "true" : "false", slot.count, slot.truncated, slot.ringShort ? "true" : "false");
    for (uint32_t i = 0; i < slot.count; ++i) {
        const S_event &e = slot.events[i];
        std::fprintf(f, "%s[%lld,%u,%u,%llu]", i ? "," : "", static_cast<long long>(e.qpc), e.type, e.id,
                     static_cast<unsigned long long>(e.value));
    }
    std::fprintf(f, "]}\n");
    std::fclose(f);
}

inline DWORD WINAPI HitchWriter(void *param)
{
    S_hitches &h = *static_cast<S_hitches *>(param);
    for (;;) {
        WaitForSingleObject(h.wake, INFINITE);
        bool any = true;
        while (any) {
            any = false;
            for (S_hitchSlot &slot : h.slots)
                if (InterlockedCompareExchange(&slot.state, 2, 1) == 1) {
                    WriteRecord(h, slot);
                    InterlockedExchange(&slot.state, 0);
                    any = true;
                }
        }
        if (h.quit)
            return 0;
    }
}

// The window's events into a free slot, for the writer. On the producer thread.
inline void HitchSnapshot(S_hitches &h)
{
    h.pending = false;
    S_hitchSlot *slot = nullptr;
    for (S_hitchSlot &s : h.slots)
        if (s.state == 0) {
            slot = &s;
            break;
        }
    if (!slot) {
        ++h.dropped;
        return;
    }
    const S_stream &s = g_stream;
    const uint64_t capacity = static_cast<uint64_t>(s.mask) + 1;
    const uint64_t oldest = s.written > capacity ? s.written - capacity : 0;
    const int64_t from = h.trigger.qpc - h.beforeTicks < h.trigger.frameStart ? h.trigger.qpc - h.beforeTicks : h.trigger.frameStart;
    uint64_t first = s.written;
    while (first > oldest && s.events[static_cast<uint32_t>(first - 1) & s.mask].qpc >= from)
        --first;
    slot->ringShort = first == oldest && oldest > 0 ? 1 : 0;
    slot->truncated = 0;
    if (s.written - first > S_hitches::kSlotEvents) {
        slot->truncated = static_cast<uint32_t>(s.written - first - S_hitches::kSlotEvents);
        first = s.written - S_hitches::kSlotEvents;
    }
    slot->count = static_cast<uint32_t>(s.written - first);
    for (uint32_t i = 0; i < slot->count; ++i)
        slot->events[i] = s.events[static_cast<uint32_t>(first + i) & s.mask];
    slot->trigger = h.trigger;
    slot->record = ++h.records;
    InterlockedExchange(&slot->state, 1);
    SetEvent(h.wake);
}

inline void HitchTrigger(S_hitches &h, uint16_t type, uint32_t id, int64_t ticks, int64_t qpc, bool gap)
{
    if (h.pending) {
        ++h.trigger.merged;
        return;
    }
    if (h.suppress)
        return;
    if (h.records >= h.config.maxRecords) {
        // Over the limit: counted once a hitch, its other triggers joining it as in a window.
        if (qpc >= h.skipUntil) {
            ++h.skipped;
            h.skipUntil = qpc + h.afterTicks;
        }
        return;
    }
    h.pending = true;
    h.trigger = S_hitchTrigger();
    h.trigger.type = type;
    h.trigger.gap = gap ? 1 : 0;
    h.trigger.id = id;
    h.trigger.qpc = qpc;
    h.trigger.ticks = ticks;
    h.trigger.frameStart = gap ? qpc - ticks : (h.lastBoundary ? h.lastBoundary : qpc - ticks);
    h.pendingUntil = qpc + h.afterTicks;
    if (h.capturesLeft && h.requestCapture) {
        --h.capturesLeft;
        h.requestCapture();
        h.trigger.capture = 1;
        h.suppress = 3;             // (the capture's own slow frames are not hitches)
    }
}

inline void HitchStep(uint16_t type, uint32_t id, int64_t qpc)
{
    S_hitches &h = *g_stream.hitches;
    if (h.pending && qpc >= h.pendingUntil)
        HitchSnapshot(h);
    if (type >= 128)
        return;
    switch (h.kind[type]) {
    case 1: {
        if (h.lastBoundary && qpc - h.lastBoundary > h.gapTicks)
            HitchTrigger(h, type, id, qpc - h.lastBoundary, qpc, true);
        h.lastBoundary = qpc;
        if (h.suppress)
            --h.suppress;
        break;
    }
    case 2:
    case 3: {
        const int64_t begin = h.lastBegin[type - 1];
        if (begin && qpc - begin > (h.kind[type] == 2 ? h.gapTicks : h.waitTicks))
            HitchTrigger(h, type, id, qpc - begin, qpc, false);
        break;
    }
    default: break;
    }
    h.lastBegin[type] = qpc;
}

// The recorder on the running stream (Start first), on the producer thread. `stream`: "game" or
// "engine"; `boundary`: the event that starts a frame (EV_LOOP_BEGIN, EV_FLIP_BEGIN); `file`: next to the
// module, emptied now; `metadata`: a JSON object's members (no braces) for the file's first line.
inline bool StartHitches(const S_hitchConfig &config, const char *stream, E_event boundary, const char *file,
                         const char *metadata, void (*requestCapture)() = nullptr)
{
    S_stream &s = g_stream;
    if (!s.on || s.hitches || !config.gapMs)
        return false;
    S_hitches *h = new (std::nothrow) S_hitches();
    if (!h)
        return false;
    for (S_hitchSlot &slot : h->slots) {
        slot.events = static_cast<S_event *>(std::calloc(S_hitches::kSlotEvents, sizeof(S_event)));
        if (!slot.events) {
            for (S_hitchSlot &made : h->slots)
                std::free(made.events);
            delete h;
            return false;
        }
    }
    h->config = config;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    h->frequency = frequency.QuadPart;
    h->gapTicks = frequency.QuadPart * config.gapMs / 1000;
    h->waitTicks = frequency.QuadPart * config.waitMs / 1000;
    h->beforeTicks = frequency.QuadPart * config.beforeMs / 1000;
    h->afterTicks = frequency.QuadPart * config.afterMs / 1000;
    h->capturesLeft = config.captures;
    h->requestCapture = requestCapture;
    h->kind[boundary] = 1;
    static const uint16_t stages[] = {EV_INPUT_POLL_END, EV_CONTROLS_END, EV_ENGINE_TICK_END, EV_RENDER_RECORD_END, EV_NET_RECEIVE_END,
                                      EV_UPDATE_SERVER_END, EV_UPDATE_CLIENT_END, EV_NET_SEND_END, EV_FLIP_CALL_END, EV_PRESENT_END,
                                      EV_RUN_FRAME_END, EV_RENDERER_RUN_END};
    static const uint16_t waits[] = {EV_TEXTURE_UPLOAD_END, EV_ACQUIRE_END, EV_GL_SWAP_END, EV_SUBMIT_END, EV_UPLOAD_WAIT_END,
                                     EV_PIPELINE_CREATE_END};
    for (uint16_t t : stages)
        h->kind[t] = 2;
    for (uint16_t t : waits)
        h->kind[t] = 3;
    std::snprintf(h->stream, sizeof h->stream, "%s", stream);
    if (!ModuleFilePath(file, h->path, sizeof h->path)) {
        for (S_hitchSlot &slot : h->slots)
            std::free(slot.events);
        delete h;
        return false;
    }
    if (FILE *f = std::fopen(h->path, "w"))
        std::fclose(f);
    // The module (its file name and link time: which build) and the settings.
    char module[MAX_PATH] = "?";
    unsigned long linked = 0;
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&g_stream), &self)) {
        char full[MAX_PATH];
        if (GetModuleFileNameA(self, full, MAX_PATH)) {
            const char *slash = std::strrchr(full, '\\');
            std::snprintf(module, sizeof module, "%s", slash ? slash + 1 : full);
        }
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(self);
        linked = reinterpret_cast<const IMAGE_NT_HEADERS *>(reinterpret_cast<const uint8_t *>(self) + dos->e_lfanew)->FileHeader.TimeDateStamp;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    std::snprintf(h->header, sizeof h->header,
                  "{\"format\":\"ls3d.hitches\",\"version\":1,\"stream\":\"%s\",\"module\":\"%s\",\"module_link_time\":%lu,"
                  "\"qpc_frequency\":%lld,\"session_start_qpc\":%lld,\"config\":{\"hitch_ms\":%u,\"wait_ms\":%u,\"before_ms\":%u,"
                  "\"after_ms\":%u,\"max_records\":%u,\"captures\":%u},\"metadata\":{%s},\"event_format\":[\"qpc\",\"type\",\"id\",\"value\"],"
                  "\"types\":{",
                  stream, module, linked, static_cast<long long>(frequency.QuadPart), static_cast<long long>(now.QuadPart), config.gapMs,
                  config.waitMs, config.beforeMs, config.afterMs, config.maxRecords, config.captures, metadata ? metadata : "");
    bool firstType = true;
    for (uint16_t t = 1; t < 128; ++t)
        if (std::strcmp(EventName(t), "unknown") != 0) {
            const size_t used = std::strlen(h->header);
            std::snprintf(h->header + used, sizeof h->header - used, "%s\"%u\":\"%s\"", firstType ? "" : ",", t, EventName(t));
            firstType = false;
        }
    const size_t used = std::strlen(h->header);
    std::snprintf(h->header + used, sizeof h->header - used, "}}");
    h->wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    h->thread = h->wake ? CreateThread(nullptr, 0, HitchWriter, h, 0, nullptr) : nullptr;
    if (!h->thread) {
        if (h->wake)
            CloseHandle(h->wake);
        for (S_hitchSlot &slot : h->slots)
            std::free(slot.events);
        delete h;
        return false;
    }
    s.hitches = h;
    return true;
}

struct S_hitchTotals {
    uint32_t records = 0, skipped = 0, dropped = 0;
};

// A window still open is written with what it has; the writer finishes and stops. Returns the totals.
inline S_hitchTotals StopHitchesWithTotals()
{
    S_hitchTotals totals;
    S_stream &s = g_stream;
    S_hitches *h = s.hitches;
    if (!h)
        return totals;
    if (h->pending)
        HitchSnapshot(*h);
    s.hitches = nullptr;
    InterlockedExchange(&h->quit, 1);
    SetEvent(h->wake);
    WaitForSingleObject(h->thread, INFINITE);
    CloseHandle(h->thread);
    CloseHandle(h->wake);
    totals = {h->records, h->skipped, h->dropped};
    if (FILE *f = std::fopen(h->path, "a")) {
        if (!h->headerWritten)
            std::fprintf(f, "%s\n", h->header);
        std::fprintf(f, "{\"end\":true,\"records\":%u,\"skipped_over_limit\":%u,\"dropped_no_slot\":%u}\n", h->records, h->skipped, h->dropped);
        std::fclose(f);
    }
    for (S_hitchSlot &slot : h->slots)
        std::free(slot.events);
    delete h;
    return totals;
}

inline void StopHitches() { StopHitchesWithTotals(); }

}  // namespace ls3d::diag
