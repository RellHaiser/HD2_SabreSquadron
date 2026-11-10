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

struct S_stream {
    S_event *events = nullptr;
    uint32_t mask = 0;              // capacity - 1
    uint64_t written = 0;           // events recorded since Start (the ring holds the last capacity of them)
    volatile LONG foreign = 0;      // marks from another thread, not recorded
    DWORD thread = 0;               // the producer
    bool on = false;
};
inline S_stream g_stream;           // (one per module)

inline bool Enabled() { return g_stream.on; }

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

inline void Stop()
{
    S_stream &s = g_stream;
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

}  // namespace ls3d::diag
