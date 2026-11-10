// The first draw (or clear) at which two replays of a captured frame stop drawing the same picture
// (docs/render_device.md). It reads .ls3dframe captures (src/gfx/trace/trace_format.h) and replays
// them with tests/render_replay.h; the one thing it asks of the engine is fixed::SetFrameObserver,
// to report the pipeline SDL_GPU and OpenGL recorded for the draw.
//
// How: the capture is indexed and validated offline first (record sizes against the file, payload
// sizes against each operation's layout, resource ids defined before use and not redefined while
// live). Its "pixel operations" (CLEAR, DRAW_*) are numbered 1..N. Checkpoint k is a *prefix* of the
// capture: every record up to and including pixel operation k, then END_SCENE (when a scene is open)
// and PRESENT, so the picture is read back there. Each prefix is replayed from the start on a freshly
// created device — never a readback in the middle of one replay: the fixed renderer runs and drops
// the recorded frame on a back-buffer readback (fixed_device.cpp CopyRects -> RunFrame), so a
// mid-frame readback could change what follows. The reference side and the test side each replay
// the prefix; the first k whose pictures differ (more than --min-pixels pixels with a channel more
// than --tolerance apart) is the divergent operation. Checkpoints are visited every --stride
// operations (default sqrt(N)), then one by one inside the first window that differs. A divergence
// that a later operation completely paints over is only seen at --stride 1.
//
// Usage:
//   test_render_divergence.exe --index <capture>             validate and summarise, no device
//   test_render_divergence.exe <capture> [--reference d3d8|sdlgpu|opengl] [--backend d3d8|sdlgpu|opengl]
//        [--driver direct3d12|vulkan] [--d3d8 <dll>] [--tolerance N] [--min-pixels N] [--stride N]
//        [--inject OP:rs:STATE:VALUE | OP:ts:STAGE:TYPE:VALUE | OP:tex:STAGE:ID] ... [--out DIR]
//        [--minimize [--minimize-budget N]]
//   test_render_divergence.exe --selftest [--out DIR]          synthetic captures, injected faults
//   test_render_divergence.exe --zbias-probe --backend B       what ZBIAS does on a backend, measured
// --inject adds a record on the test side right before pixel operation OP (a synthetic fault: the
// report says so). Writes DIR (default <capture>.e01\): report.md, expected/actual/diff PNGs and
// repro.ls3dframe — the test side's prefix with the reference picture as its OP_FRAME_IMAGE, so
// `test_render_replay.exe repro.ls3dframe --backend <test>` shows the divergence on its own.
// --minimize adds minimal.ls3dframe: as few earlier draws as still show it (Minimize, below).
// Exit: 0 none found, 3 localized, 4 found but not localized, 2 bad input; --selftest 0 pass, 1 fail.
//
// test-sources: src/gfx/d3d8/gfx_d3d8.cpp src/gfx/trace/*.cpp src/gfx/sdlgpu/*.cpp src/gfx/fixed/*.cpp src/gfx/opengl/*.cpp
// test-sources: src/core/enhance.cpp src/core/debug.cpp
// test-sources: src/platform/*.cpp src/platform/win32/*.cpp src/platform/sdl3/*.cpp
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "render_replay.h"
#include "core/enhance.h"
#include "gfx/fixed/fixed_device.h"
#include "gfx/opengl/opengl_device.h"
#include "gfx/sdlgpu/sdlgpu_device.h"

namespace {

using namespace ls3d;
using namespace ls3d::gfx;
using namespace ls3d::gfx::trace;
using replay::S_image;

// ---------------------------------------------------------------- offline index

struct S_record {
    E_op op;
    uint64_t offset;                    // of the payload in the file
    uint32_t size;
};

struct S_index {
    std::vector<S_record> records;
    std::vector<uint32_t> pixelOps;     // record numbers of CLEAR / DRAW_*, in order
    std::vector<std::string> errors;    // the capture must not be replayed
    std::vector<std::string> warnings;
    uint32_t width = 0, height = 0;
    bool present = false, frameImage = false;
    std::map<uint32_t, uint32_t> opCounts;
    uint64_t fileBytes = 0;
};

const char *OpName(uint32_t op)
{
    static const char *names[] = {"?", "HEADER", "RENDER_STATE", "STAGE_STATE", "TEXTURE", "TRANSFORM", "VIEWPORT",
                                  "MATERIAL", "LIGHT", "LIGHT_ENABLE", "DEF_SHADER", "VERTEX_SHADER", "SHADER_CONSTANT",
                                  "STREAM", "INDICES", "DEF_TEXTURE", "TEXTURE_DATA", "DEF_VERTEX_BUFFER",
                                  "DEF_INDEX_BUFFER", "VERTEX_DATA", "INDEX_DATA", "DROP", "CLEAR", "BEGIN_SCENE",
                                  "END_SCENE", "DRAW_INDEXED", "DRAW_UP", "DRAW_INDEXED_UP", "PRESENT", "GAMMA",
                                  "FRAME_IMAGE"};
    return op < sizeof names / sizeof *names ? names[op] : "UNKNOWN";
}

bool IsPixelOp(E_op op)
{
    return op == OP_CLEAR || op == OP_DRAW_INDEXED || op == OP_DRAW_UP || op == OP_DRAW_INDEXED_UP;
}

uint32_t U32(const uint8_t *p, size_t at) { uint32_t v; std::memcpy(&v, p + at, 4); return v; }

std::string Fmt(const char *f, ...)
{
    char text[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(text, sizeof text, f, a);
    va_end(a);
    return text;
}

// Every record's size checked against the file before anything is allocated for it, then its payload
// against its operation's layout; resource ids followed. False when the file cannot be opened.
bool IndexCapture(const char *path, S_index *x)
{
    *x = S_index();
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    _fseeki64(f, 0, SEEK_END);
    x->fileBytes = static_cast<uint64_t>(_ftelli64(f));
    _fseeki64(f, 0, SEEK_SET);
    char magic[sizeof kMagic];
    if (x->fileBytes < sizeof magic || std::fread(magic, sizeof magic, 1, f) != 1 ||
        std::memcmp(magic, kMagic, sizeof kMagic) != 0) {
        x->errors.push_back("not a capture (no LS3DFRM1 magic)");
        std::fclose(f);
        return true;
    }
    enum E_kind { NONE, TEXTURE, VERTICES, INDICES };
    std::map<uint32_t, E_kind> live;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> textureFormat;   // id: format, levels (for TEXTURE_DATA)
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> textureSize;     // id: width, height
    std::vector<uint8_t> payload;
    uint64_t at = sizeof magic;
    auto use = [&](uint32_t n, uint32_t id, E_kind kind, const char *what) {
        if (!id)
            return;
        auto l = live.find(id);
        if (l == live.end())
            x->errors.push_back(Fmt("record %u (%s): %s id %u used before it is defined", n, OpName(x->records[n].op), what, id));
        else if (l->second != kind)
            x->errors.push_back(Fmt("record %u (%s): id %u is not a %s", n, OpName(x->records[n].op), id, what));
    };
    while (at < x->fileBytes) {
        const uint32_t n = static_cast<uint32_t>(x->records.size());
        if (x->fileBytes - at < 8) {
            x->errors.push_back(Fmt("record %u at byte %llu: cut short in its header", n, static_cast<unsigned long long>(at)));
            break;
        }
        uint32_t head[2];
        std::fread(head, sizeof head, 1, f);
        at += 8;
        if (head[1] > x->fileBytes - at) {
            x->errors.push_back(Fmt("record %u at byte %llu: claims %u payload bytes, %llu left in the file", n,
                                    static_cast<unsigned long long>(at - 8), head[1],
                                    static_cast<unsigned long long>(x->fileBytes - at)));
            break;
        }
        const E_op op = static_cast<E_op>(head[0]);
        const uint32_t size = head[1];
        x->records.push_back({op, at, size});
        ++x->opCounts[op];
        payload.resize(size ? size : 1);
        if (size && std::fread(payload.data(), size, 1, f) != 1) {
            x->errors.push_back(Fmt("record %u: read failed", n));
            break;
        }
        at += size;
        const uint8_t *p = payload.data();
        // The least the layout needs (and, where it says, exactly that).
        uint64_t need = 0;
        bool exact = false;
        switch (op) {
        case OP_HEADER: need = 28; break;
        case OP_RENDER_STATE: need = 8; break;
        case OP_STAGE_STATE: need = 12; break;
        case OP_TEXTURE: need = 8; break;
        case OP_TRANSFORM: need = 4 + sizeof(D3DMATRIX); break;
        case OP_VIEWPORT: need = sizeof(D3DVIEWPORT8); break;
        case OP_MATERIAL: need = sizeof(D3DMATERIAL8); break;
        case OP_LIGHT: need = 4 + sizeof(D3DLIGHT8); break;
        case OP_LIGHT_ENABLE: need = 8; break;
        case OP_DEF_SHADER:
            need = 16;
            if (size >= 16) {
                need += 4ull * U32(p, 8) + 4ull * U32(p, 12);
                exact = true;
            }
            break;
        case OP_VERTEX_SHADER: need = 4; break;
        case OP_SHADER_CONSTANT:
            need = 8;
            if (size >= 8)
                need += 16ull * U32(p, 4);
            break;
        case OP_STREAM: need = 12; break;
        case OP_INDICES: need = 8; break;
        case OP_DEF_TEXTURE: need = 28; break;
        case OP_TEXTURE_DATA: need = 24; break;
        case OP_DEF_VERTEX_BUFFER:
        case OP_DEF_INDEX_BUFFER:
            need = 20;
            if (size >= 20) {
                need += U32(p, 4);
                exact = true;
            }
            break;
        case OP_VERTEX_DATA:
        case OP_INDEX_DATA: need = 8; break;
        case OP_DROP: need = 4; break;
        case OP_CLEAR:
            need = 4;
            if (size >= 4)
                need += 16ull * U32(p, 0) + 16;
            exact = true;
            break;
        case OP_BEGIN_SCENE:
        case OP_END_SCENE:
        case OP_PRESENT: exact = true; break;
        case OP_DRAW_INDEXED: need = 20; break;
        case OP_DRAW_UP:
            need = 12;
            if (size >= 12)
                need += static_cast<uint64_t>(PrimitiveVertices(static_cast<D3DPRIMITIVETYPE>(U32(p, 0)), U32(p, 4))) * U32(p, 8);
            break;
        case OP_DRAW_INDEXED_UP:
            need = 28;
            if (size >= 28)
                need += static_cast<uint64_t>(U32(p, 24)) + (static_cast<uint64_t>(U32(p, 4)) + U32(p, 8)) * U32(p, 20);
            break;
        case OP_GAMMA: need = 4 + sizeof(D3DGAMMARAMP); break;
        case OP_FRAME_IMAGE: {
            need = 12;
            uint32_t rowBytes, rows;
            if (size >= 12 && FormatRows(static_cast<D3DFORMAT>(U32(p, 8)), U32(p, 0), U32(p, 4), &rowBytes, &rows))
                need += static_cast<uint64_t>(rowBytes) * rows;
            break;
        }
        default:
            x->errors.push_back(Fmt("record %u: unknown operation %u (a newer format?)", n, head[0]));
            continue;
        }
        if (size < need || (exact && size != need)) {
            x->errors.push_back(Fmt("record %u (%s): %u payload bytes, its layout needs %s%llu", n, OpName(op), size,
                                    exact ? "" : "at least ", static_cast<unsigned long long>(need)));
            continue;
        }
        if (n == 0 && op != OP_HEADER)
            x->errors.push_back("the first record is not HEADER");
        switch (op) {
        case OP_HEADER:
            if (n != 0)
                x->errors.push_back(Fmt("record %u: a second HEADER", n));
            x->width = U32(p, 0);
            x->height = U32(p, 4);
            break;
        case OP_TEXTURE: use(n, U32(p, 4), TEXTURE, "texture"); break;
        case OP_STREAM: use(n, U32(p, 4), VERTICES, "vertex buffer"); break;
        case OP_INDICES: use(n, U32(p, 0), INDICES, "index buffer"); break;
        case OP_VERTEX_DATA: use(n, U32(p, 0), VERTICES, "vertex buffer"); break;
        case OP_INDEX_DATA: use(n, U32(p, 0), INDICES, "index buffer"); break;
        case OP_TEXTURE_DATA: {
            const uint32_t id = U32(p, 0);
            use(n, id, TEXTURE, "texture");
            auto t = textureFormat.find(id);
            uint32_t rowBytes, rows;
            const uint32_t w = U32(p, 16) - U32(p, 8), h = U32(p, 20) - U32(p, 12);
            if (U32(p, 16) < U32(p, 8) || U32(p, 20) < U32(p, 12))
                x->errors.push_back(Fmt("record %u (TEXTURE_DATA): an inverted rectangle", n));
            else if (t != textureFormat.end() && FormatRows(static_cast<D3DFORMAT>(t->second.first), w, h, &rowBytes, &rows) &&
                     static_cast<uint64_t>(rowBytes) * rows > size - 24ull)
                x->errors.push_back(Fmt("record %u (TEXTURE_DATA): %u bytes of rows, the rectangle needs %llu", n, size - 24,
                                        static_cast<unsigned long long>(rowBytes) * rows));
            break;
        }
        case OP_DEF_TEXTURE:
        case OP_DEF_VERTEX_BUFFER:
        case OP_DEF_INDEX_BUFFER: {
            const uint32_t id = U32(p, 0);
            if (live.count(id))
                x->errors.push_back(Fmt("record %u (%s): id %u defined again while live (no DROP)", n, OpName(op), id));
            live[id] = op == OP_DEF_TEXTURE ? TEXTURE : op == OP_DEF_VERTEX_BUFFER ? VERTICES : INDICES;
            if (op == OP_DEF_TEXTURE) {
                textureFormat[id] = {U32(p, 20), U32(p, 12)};
                textureSize[id] = {U32(p, 4), U32(p, 8)};
            }
            break;
        }
        case OP_DROP:
            if (!live.erase(U32(p, 0)))
                x->warnings.push_back(Fmt("record %u (DROP): id %u was not defined", n, U32(p, 0)));
            break;
        case OP_PRESENT: x->present = true; break;
        case OP_FRAME_IMAGE: x->frameImage = true; break;
        default: break;
        }
        if (IsPixelOp(op))
            x->pixelOps.push_back(n);
    }
    std::fclose(f);
    if (x->records.empty() || x->records[0].op != OP_HEADER) {
        if (std::find(x->errors.begin(), x->errors.end(), "the first record is not HEADER") == x->errors.end())
            x->errors.push_back("no HEADER");
    }
    if (!x->present)
        x->warnings.push_back("no PRESENT (a capture cut short?)");
    if (x->pixelOps.empty())
        x->warnings.push_back("no CLEAR or DRAW: nothing to compare");
    return true;
}

void PrintIndex(const char *path, const S_index &x)
{
    std::printf("%s: %llu bytes, %zu records, %ux%u, %zu pixel operations (clears and draws)%s%s\n", path,
                static_cast<unsigned long long>(x.fileBytes), x.records.size(), x.width, x.height, x.pixelOps.size(),
                x.frameImage ? ", the game's picture" : "", x.present ? "" : ", NO PRESENT");
    for (const auto &c : x.opCounts)
        std::printf("    %-18s %u\n", OpName(c.first), c.second);
    for (const std::string &w : x.warnings)
        std::printf("  warning: %s\n", w.c_str());
    for (const std::string &e : x.errors)
        std::printf("  ERROR: %s\n", e.c_str());
}

// ---------------------------------------------------------------- prefixes

struct S_injection {
    uint32_t pixelOp;                   // 1-based: the record goes right before this operation
    E_op op;
    std::vector<uint32_t> values;
    std::string text;
};

bool ParseInjection(const char *s, S_injection *j)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    char kind[8] = {};
    j->text = s;
    if (std::sscanf(s, "%u:%7[a-z]:%u:%u:%u", &a, kind, &b, &c, &d) == 5 && !std::strcmp(kind, "ts")) {
        *j = {a, OP_STAGE_STATE, {b, c, d}, s};
        return a > 0;
    }
    if (std::sscanf(s, "%u:%7[a-z]:%u:%u", &a, kind, &b, &c) == 4) {
        if (!std::strcmp(kind, "rs"))
            *j = {a, OP_RENDER_STATE, {b, c}, s};
        else if (!std::strcmp(kind, "tex"))
            *j = {a, OP_TEXTURE, {b, c}, s};
        else
            return false;
        return a > 0;
    }
    return false;
}

void PutRecord(FILE *out, E_op op, const void *payload, uint32_t size)
{
    const uint32_t head[2] = {op, size};
    std::fwrite(head, sizeof head, 1, out);
    if (size)
        std::fwrite(payload, size, 1, out);
}

// ---------------------------------------------------------------- what each draw uses (the minimizer)

// A resource "instance" is one definition of an id (ids come back after a DROP): its DEF record's
// number + 1, so 0 is none.
struct S_usage {
    std::vector<uint32_t> recInstance;              // per record: the instance it defines, fills, drops or binds
    std::vector<std::vector<uint32_t>> opUses;      // per pixel operation (1-based): what is bound when it draws,
                                                    // on either side (the test side's with the injections)
    std::vector<uint32_t> injected;                 // bound by an injected record
};

S_usage ComputeUsage(const char *path, const S_index &x, uint32_t k, const std::vector<S_injection> &injections)
{
    S_usage u;
    const uint32_t last = x.pixelOps[k - 1];
    u.recInstance.assign(last + 1, 0);
    u.opUses.resize(k + 1);
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return u;
    std::map<uint32_t, uint32_t> instance;          // id: its live instance
    auto of = [&](uint32_t id) { auto i = instance.find(id); return id && i != instance.end() ? i->second : 0u; };
    struct S_side {
        std::map<uint32_t, uint32_t> textures, streams;
        uint32_t indices = 0;
    } sides[2];                                     // reference, test
    uint32_t pixelOp = 0;
    for (uint32_t r = 0; r <= last; ++r) {
        const S_record &rec = x.records[r];
        uint8_t p[16] = {};
        _fseeki64(f, static_cast<long long>(rec.offset), SEEK_SET);
        std::fread(p, std::min<uint32_t>(rec.size, 16), 1, f);
        switch (rec.op) {
        case OP_DEF_TEXTURE:
        case OP_DEF_VERTEX_BUFFER:
        case OP_DEF_INDEX_BUFFER: u.recInstance[r] = instance[U32(p, 0)] = r + 1; break;
        case OP_TEXTURE_DATA:
        case OP_VERTEX_DATA:
        case OP_INDEX_DATA: u.recInstance[r] = of(U32(p, 0)); break;
        case OP_DROP:
            u.recInstance[r] = of(U32(p, 0));
            instance.erase(U32(p, 0));
            break;
        case OP_TEXTURE:
            u.recInstance[r] = of(U32(p, 4));
            for (S_side &s : sides)
                s.textures[U32(p, 0)] = u.recInstance[r];
            break;
        case OP_STREAM:
            u.recInstance[r] = of(U32(p, 4));
            for (S_side &s : sides)
                s.streams[U32(p, 0)] = u.recInstance[r];
            break;
        case OP_INDICES:
            u.recInstance[r] = of(U32(p, 0));
            for (S_side &s : sides)
                s.indices = u.recInstance[r];
            break;
        default: break;
        }
        if (!IsPixelOp(rec.op))
            continue;
        ++pixelOp;
        for (const S_injection &j : injections)
            if (j.pixelOp == pixelOp && j.op == OP_TEXTURE) {
                sides[1].textures[j.values[0]] = of(j.values[1]);
                u.injected.push_back(of(j.values[1]));
            }
        std::vector<uint32_t> &uses = u.opUses[pixelOp];
        for (const S_side &s : sides) {
            for (const auto &t : s.textures)
                uses.push_back(t.second);
            if (rec.op == OP_DRAW_INDEXED) {
                for (const auto &v : s.streams)
                    uses.push_back(v.second);
                uses.push_back(s.indices);
            }
        }
    }
    std::fclose(f);
    return u;
}

// Which pixel operations a written prefix keeps, and so which resource instances it needs.
struct S_subset {
    const S_usage *usage = nullptr;
    std::vector<bool> keep;             // per pixel operation (1-based)
    std::vector<bool> needed;           // per instance
};

S_subset MakeSubset(const S_usage &u, uint32_t k, const std::vector<uint32_t> &earlier)
{
    S_subset s;
    s.usage = &u;
    s.keep.assign(k + 1, false);
    s.needed.assign(u.recInstance.size() + 2, false);
    s.keep[k] = true;
    for (uint32_t j : earlier)
        s.keep[j] = true;
    for (uint32_t j = 1; j <= k; ++j)
        if (s.keep[j])
            for (uint32_t i : u.opUses[j])
                s.needed[i] = true;
    for (uint32_t i : u.injected)
        s.needed[i] = true;
    s.needed[0] = false;
    return s;
}

// The capture's records up to and including pixel operation `k` (1-based), `injections` before theirs,
// then END_SCENE if a scene is open, `expected` as the FRAME_IMAGE (if any) and PRESENT. With a
// `subset`: only its pixel operations (the injections all the same), and only the resources they need;
// a binding of one it does not need becomes a binding of none (id 0).
bool WritePrefix(const char *source, const S_index &x, uint32_t k, const std::vector<S_injection> &injections,
                 const S_image *expected, const std::string &path, const S_subset *subset = nullptr)
{
    FILE *in = std::fopen(source, "rb");
    FILE *out = std::fopen(path.c_str(), "wb");
    if (!in || !out) {
        if (in)
            std::fclose(in);
        if (out)
            std::fclose(out);
        return false;
    }
    std::fwrite(kMagic, sizeof kMagic, 1, out);
    const uint32_t last = x.pixelOps[k - 1];
    bool scene = false;
    uint32_t pixelOp = 0;
    std::vector<uint8_t> chunk(1 << 20);
    for (uint32_t r = 0; r <= last; ++r) {
        const S_record &rec = x.records[r];
        if (rec.op == OP_PRESENT || rec.op == OP_FRAME_IMAGE)
            continue;
        if (IsPixelOp(rec.op)) {
            ++pixelOp;
            for (const S_injection &j : injections)
                if (j.pixelOp == pixelOp)
                    PutRecord(out, j.op, j.values.data(), static_cast<uint32_t>(j.values.size() * 4));
            if (subset && !subset->keep[pixelOp])
                continue;
        } else if (subset) {
            const uint32_t instance = subset->usage->recInstance[r];
            if (instance && !subset->needed[instance]) {
                if (rec.op == OP_TEXTURE || rec.op == OP_STREAM || rec.op == OP_INDICES) {
                    uint32_t v[3] = {};
                    _fseeki64(in, static_cast<long long>(rec.offset), SEEK_SET);
                    std::fread(v, std::min<uint32_t>(rec.size, sizeof v), 1, in);
                    v[rec.op == OP_INDICES ? 0 : 1] = 0;
                    PutRecord(out, rec.op, v, std::min<uint32_t>(rec.size, sizeof v));
                }
                continue;           // (a definition, its data or its DROP)
            }
        }
        scene = rec.op == OP_BEGIN_SCENE ? true : rec.op == OP_END_SCENE ? false : scene;
        const uint32_t head[2] = {rec.op, rec.size};
        std::fwrite(head, sizeof head, 1, out);
        _fseeki64(in, static_cast<long long>(rec.offset), SEEK_SET);
        for (uint32_t left = rec.size; left;) {
            const uint32_t n = left < chunk.size() ? left : static_cast<uint32_t>(chunk.size());
            if (std::fread(chunk.data(), n, 1, in) != 1)
                break;
            std::fwrite(chunk.data(), n, 1, out);
            left -= n;
        }
    }
    if (scene)
        PutRecord(out, OP_END_SCENE, nullptr, 0);
    if (expected && !expected->bgr.empty()) {
        std::vector<uint8_t> image(12 + static_cast<size_t>(expected->width) * expected->height * 4);
        const uint32_t h[3] = {expected->width, expected->height, D3DFMT_X8R8G8B8};
        std::memcpy(image.data(), h, 12);
        for (size_t p = 0, q = 12; p + 2 < expected->bgr.size(); p += 3, q += 4) {
            image[q] = expected->bgr[p];
            image[q + 1] = expected->bgr[p + 1];
            image[q + 2] = expected->bgr[p + 2];
            image[q + 3] = 0xff;
        }
        PutRecord(out, OP_FRAME_IMAGE, image.data(), static_cast<uint32_t>(image.size()));
    }
    PutRecord(out, OP_PRESENT, nullptr, 0);
    const bool ok = !std::ferror(out) && !std::ferror(in);
    std::fclose(in);
    return std::fclose(out) == 0 && ok;
}

// ---------------------------------------------------------------- replays

struct S_backend {
    std::string name;
    Device *device = nullptr;
};

// `path` replayed from its first record on a newly created device; the picture it read back.
bool ReplayFile(const S_backend &b, const std::string &path, S_image *picture, std::string *error, int *failedCalls,
                D3DFORMAT *depthFallback = nullptr)
{
    replay::Capture capture;
    if (!capture.Open(path.c_str())) {
        *error = "cannot open " + path;
        return false;
    }
    HWND window = CreateWindowA("LS3DFDivergence", "e01", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                GetModuleHandleA(nullptr), nullptr);
    bool ok;
    {
        replay::Replayer r(b.device, window);
        ok = r.Run(capture);
        *picture = r.picture;
        *error = r.error;
        *failedCalls = r.failedCalls;
        if (depthFallback)
            *depthFallback = r.depthFallback;
        if (ok && picture->bgr.empty()) {
            *error = "no picture read back";
            ok = false;
        }
    }
    DestroyWindow(window);
    return ok;
}

uint64_t Hash(const S_image &image)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t v : image.bgr)
        h = (h ^ v) * 0x100000001b3ull;
    return h;
}

struct S_options {
    S_backend reference, test;
    uint32_t tolerance = 0;
    uint64_t minPixels = 0;
    uint32_t stride = 0;                // 0: sqrt(N)
    std::vector<S_injection> injections;
    std::string out;
    bool quiet = false;
    bool minimize = false;
    uint32_t minimizeBudget = 150;      // candidate prefixes tried (two replays each)
};

struct S_checkpoint {
    bool done = false, ok = false;
    S_image expected, actual;
    replay::S_difference d;
    bool diverges = false;
    std::string error;
    int failedCalls = 0;
    D3DFORMAT refDepth = D3DFMT_UNKNOWN, testDepth = D3DFMT_UNKNOWN;    // a depth format used instead of the capture's
};

enum E_verdict { NO_DIVERGENCE, LOCALIZED, NOT_LOCALIZED, BAD_INPUT };

struct S_result {
    E_verdict verdict = BAD_INPUT;
    uint32_t op = 0;                    // the first divergent pixel operation (1-based), 0: none
    uint32_t record = 0;
    uint32_t replays = 0;
    std::string why;                    // NOT_LOCALIZED / BAD_INPUT: the reason
    bool reproVerified = false;
    bool minimized = false;             // --minimize: minimal.ls3dframe made and verified
    std::vector<uint32_t> minimalKept;
};

class Search {
public:
    Search(const char *capture, const S_index &index, const S_options &options)
        : path(capture), x(index), o(options), points(index.pixelOps.size() + 1) {}

    S_checkpoint &At(uint32_t k)
    {
        S_checkpoint &c = points[k];
        if (c.done)
            return c;
        c.done = true;
        const std::string refPath = o.out + "\\work_ref.ls3dframe", testPath = o.out + "\\work_test.ls3dframe";
        if (!WritePrefix(path, x, k, {}, nullptr, refPath) ||
            (!o.injections.empty() && !WritePrefix(path, x, k, o.injections, nullptr, testPath))) {
            c.error = "cannot write a prefix into " + o.out;
            return c;
        }
        int failed = 0;
        replays += 2;
        if (!ReplayFile(o.reference, refPath, &c.expected, &c.error, &failed, &c.refDepth)) {
            c.error = o.reference.name + ": " + c.error;
            return c;
        }
        c.failedCalls = failed;
        if (!ReplayFile(o.test, o.injections.empty() ? refPath : testPath, &c.actual, &c.error, &failed, &c.testDepth)) {
            c.error = o.test.name + ": " + c.error;
            return c;
        }
        c.failedCalls += failed;
        c.ok = true;
        if (c.expected.width != c.actual.width || c.expected.height != c.actual.height) {
            c.diverges = true;
            c.d.pixelsOver = static_cast<uint64_t>(c.expected.width) * c.expected.height;
        } else {
            c.d = replay::Compare(c.expected, c.actual, o.tolerance);
            c.diverges = c.d.pixelsOver > o.minPixels;
        }
        if (!o.quiet)
            std::printf("    op %u/%zu (%s): %llu pixels over %u apart, largest %u%s\n", k, x.pixelOps.size(),
                        OpName(x.records[x.pixelOps[k - 1]].op), static_cast<unsigned long long>(c.d.pixelsOver), o.tolerance,
                        c.d.maxChannel, c.diverges ? "  DIVERGES" : "");
        return c;
    }

    S_result Run()
    {
        S_result r;
        const uint32_t n = static_cast<uint32_t>(x.pixelOps.size());
        if (!n) {
            r.why = "no pixel operations";
            return r;
        }
        stride = o.stride ? o.stride : static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(n))));
        uint32_t before = 0, found = 0;
        for (uint32_t k = stride < n ? stride : n;; k = k + stride < n ? k + stride : n) {
            S_checkpoint &c = At(k);
            if (!c.ok)
                return Failed(r, k, c.error);
            if (c.diverges) {
                Keep(before, k);
                for (uint32_t f = before + 1; f <= k && !found; ++f) {
                    S_checkpoint &e = At(f);
                    if (!e.ok)
                        return Failed(r, f, e.error);
                    Keep(f - 1, f, k);  // (k: the fine scan ends on it, already replayed)
                    if (e.diverges)
                        found = f;
                }
                break;
            }
            Keep(k, k);
            before = k;
            if (k == n)
                break;
        }
        r.replays = replays;
        if (!found) {
            r.verdict = NO_DIVERGENCE;
            return r;
        }
        r.op = found;
        r.record = x.pixelOps[found - 1];
        r.verdict = LOCALIZED;
        // Consistency: a replay of the same prefix must draw the same picture again, and every pixel
        // that newly differs must be one that operation `found` drew on one side or the other (a pixel
        // neither side touched keeps whatever difference it had).
        S_checkpoint &c = points[found];
        S_image again;
        std::string error;
        int failed = 0;
        const std::string refPath = o.out + "\\work_ref.ls3dframe";
        WritePrefix(path, x, found, {}, nullptr, refPath);
        ++replays;
        if (!ReplayFile(o.reference, refPath, &again, &error, &failed) || Hash(again) != Hash(c.expected)) {
            r.verdict = NOT_LOCALIZED;
            r.why = "the reference side does not draw the same picture twice for the same prefix (nondeterministic replay)";
        } else if (found > 1) {
            const S_checkpoint &p = points[found - 1];
            uint64_t newly = 0, untouched = 0;
            for (size_t i = 0; i + 2 < c.expected.bgr.size(); i += 3) {
                const bool now = Over(c.expected, c.actual, i), was = Over(p.expected, p.actual, i);
                if (!now || was)
                    continue;
                ++newly;
                stats.mask.push_back(static_cast<uint32_t>(i));
                if (!Over(p.expected, c.expected, i, 0) && !Over(p.actual, c.actual, i, 0))
                    ++untouched;
            }
            stats.newly = newly;
            stats.untouched = untouched;
            if (untouched) {
                r.verdict = NOT_LOCALIZED;
                r.why = Fmt("%llu of the %llu newly differing pixels were not drawn by this operation on either side "
                            "(replays are not consistent)", static_cast<unsigned long long>(untouched),
                            static_cast<unsigned long long>(newly));
            }
        } else {
            for (size_t i = 0; i + 2 < c.expected.bgr.size(); i += 3)
                if (Over(c.expected, c.actual, i))
                    stats.mask.push_back(static_cast<uint32_t>(i));
        }
        r.replays = replays;
        return r;
    }

    // Only the pictures of checkpoints `a`, `b` and `c` stay in memory: two 1600x1200 pictures a
    // checkpoint, kept for every one visited, are more than a 32-bit process has room for beside the
    // replay. (A freed checkpoint stays `done`: it is never visited again.)
    void Keep(uint32_t a, uint32_t b, uint32_t c = UINT32_MAX)
    {
        for (uint32_t j = 0; j < points.size(); ++j)
            if (j != a && j != b && j != c) {
                S_image().bgr.swap(points[j].expected.bgr);
                S_image().bgr.swap(points[j].actual.bgr);
            }
    }

    bool Over(const S_image &a, const S_image &b, size_t i, int tolerance = -1) const
    {
        const int t = tolerance < 0 ? static_cast<int>(o.tolerance) : tolerance;
        for (int ch = 0; ch < 3; ++ch)
            if (std::abs(static_cast<int>(a.bgr[i + ch]) - static_cast<int>(b.bgr[i + ch])) > t)
                return true;
        return false;
    }

    S_result Failed(S_result &r, uint32_t k, const std::string &why)
    {
        r.verdict = NOT_LOCALIZED;
        r.op = 0;
        r.why = Fmt("the replay of the prefix up to op %u failed: ", k) + why;
        r.replays = replays;
        return r;
    }

    const char *path;
    const S_index &x;
    const S_options &o;
    std::vector<S_checkpoint> points;
    uint32_t replays = 0, stride = 0;
    struct {
        uint64_t newly = 0, untouched = 0;
        std::vector<uint32_t> mask;     // the newly differing pixels (byte offsets in the BGR rows)
    } stats;
};

// ---------------------------------------------------------------- the state at an operation

const char *RenderStateName(uint32_t s)
{
    switch (s) {
    case D3DRS_ZENABLE: return "ZENABLE";
    case D3DRS_FILLMODE: return "FILLMODE";
    case D3DRS_SHADEMODE: return "SHADEMODE";
    case D3DRS_ZWRITEENABLE: return "ZWRITEENABLE";
    case D3DRS_ALPHATESTENABLE: return "ALPHATESTENABLE";
    case D3DRS_SRCBLEND: return "SRCBLEND";
    case D3DRS_DESTBLEND: return "DESTBLEND";
    case D3DRS_CULLMODE: return "CULLMODE";
    case D3DRS_ZFUNC: return "ZFUNC";
    case D3DRS_ALPHAREF: return "ALPHAREF";
    case D3DRS_ALPHAFUNC: return "ALPHAFUNC";
    case D3DRS_DITHERENABLE: return "DITHERENABLE";
    case D3DRS_ALPHABLENDENABLE: return "ALPHABLENDENABLE";
    case D3DRS_FOGENABLE: return "FOGENABLE";
    case D3DRS_SPECULARENABLE: return "SPECULARENABLE";
    case D3DRS_FOGCOLOR: return "FOGCOLOR";
    case D3DRS_FOGTABLEMODE: return "FOGTABLEMODE";
    case D3DRS_FOGSTART: return "FOGSTART";
    case D3DRS_FOGEND: return "FOGEND";
    case D3DRS_FOGDENSITY: return "FOGDENSITY";
    case D3DRS_ZBIAS: return "ZBIAS";
    case D3DRS_STENCILENABLE: return "STENCILENABLE";
    case D3DRS_TEXTUREFACTOR: return "TEXTUREFACTOR";
    case D3DRS_LIGHTING: return "LIGHTING";
    case D3DRS_AMBIENT: return "AMBIENT";
    case D3DRS_FOGVERTEXMODE: return "FOGVERTEXMODE";
    case D3DRS_COLORVERTEX: return "COLORVERTEX";
    default: return nullptr;
    }
}

const char *StageStateName(uint32_t t)
{
    static const char *names[] = {nullptr, "COLOROP", "COLORARG1", "COLORARG2", "ALPHAOP", "ALPHAARG1", "ALPHAARG2",
                                  "BUMPENVMAT00", "BUMPENVMAT01", "BUMPENVMAT10", "BUMPENVMAT11", "TEXCOORDINDEX",
                                  nullptr, "ADDRESSU", "ADDRESSV", "BORDERCOLOR", "MAGFILTER", "MINFILTER", "MIPFILTER",
                                  "MIPMAPLODBIAS", "MAXMIPLEVEL", "MAXANISOTROPY", "BUMPENVLSCALE", "BUMPENVLOFFSET",
                                  "TEXTURETRANSFORMFLAGS", "ADDRESSW", "COLORARG0", "ALPHAARG0", "RESULTARG"};
    return t < sizeof names / sizeof *names ? names[t] : nullptr;
}

// The state a backend had when it drew operation `k`: every record before it (the test side's with
// the injections).
std::string DescribeState(const char *path, const S_index &x, uint32_t k, const std::vector<S_injection> &injections)
{
    std::map<uint32_t, uint32_t> rs;
    std::map<uint32_t, std::map<uint32_t, uint32_t>> ts;
    std::map<uint32_t, uint32_t> tex;
    std::map<uint32_t, std::vector<uint32_t>> texDefs;     // id: w, h, levels, usage, format, pool
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> streams;
    std::map<uint32_t, std::vector<uint32_t>> shaderDefs;  // handle: usage, declaration DWORDs, function DWORDs
    uint32_t shader = 0, indices = 0, lights = 0;
    D3DVIEWPORT8 viewport = {};
    bool viewportSet = false;
    std::string op;
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return "(cannot reopen the capture)\n";
    std::vector<uint8_t> p;
    uint32_t pixelOp = 0;
    std::string injected;
    auto apply = [&](E_op o, const uint8_t *q, uint32_t size) {
        switch (o) {
        case OP_RENDER_STATE: rs[U32(q, 0)] = U32(q, 4); break;
        case OP_STAGE_STATE: ts[U32(q, 0)][U32(q, 4)] = U32(q, 8); break;
        case OP_TEXTURE: tex[U32(q, 0)] = U32(q, 4); break;
        case OP_VERTEX_SHADER: shader = U32(q, 0); break;
        case OP_STREAM: streams[U32(q, 0)] = {U32(q, 4), U32(q, 8)}; break;
        case OP_INDICES: indices = U32(q, 0); break;
        case OP_VIEWPORT:
            std::memcpy(&viewport, q, sizeof viewport);
            viewportSet = true;
            break;
        case OP_LIGHT_ENABLE: lights = U32(q, 4) ? lights | (1u << (U32(q, 0) & 31)) : lights & ~(1u << (U32(q, 0) & 31)); break;
        case OP_DEF_TEXTURE: texDefs[U32(q, 0)] = {U32(q, 4), U32(q, 8), U32(q, 12), U32(q, 16), U32(q, 20), U32(q, 24)}; break;
        case OP_DROP: texDefs.erase(U32(q, 0)); break;
        case OP_DEF_SHADER: shaderDefs[U32(q, 0)] = {U32(q, 4), U32(q, 8), U32(q, 12)}; break;
        default: (void)size; break;
        }
    };
    for (uint32_t r = 0; r <= x.pixelOps[k - 1]; ++r) {
        const S_record &rec = x.records[r];
        p.resize(rec.size ? rec.size : 1);
        _fseeki64(f, static_cast<long long>(rec.offset), SEEK_SET);
        if (rec.size && std::fread(p.data(), rec.size, 1, f) != 1)
            break;
        if (IsPixelOp(rec.op) && ++pixelOp == k) {
            for (const S_injection &j : injections)
                if (j.pixelOp == k) {
                    apply(j.op, reinterpret_cast<const uint8_t *>(j.values.data()), static_cast<uint32_t>(j.values.size() * 4));
                    injected += "  - `" + j.text + "` (" + OpName(j.op) + ")\n";
                }
            const uint8_t *q = p.data();
            switch (rec.op) {
            case OP_CLEAR: {
                const uint32_t count = U32(q, 0);
                op = Fmt("CLEAR: %u rects, flags 0x%x, colour 0x%08x, z %g", count, U32(q, 4 + 16 * count),
                         U32(q, 8 + 16 * count), static_cast<double>(*reinterpret_cast<const float *>(q + 12 + 16 * count)));
                break;
            }
            case OP_DRAW_INDEXED:
                op = Fmt("DRAW_INDEXED: type %u, min index %u, %u vertices, start index %u, %u primitives", U32(q, 0), U32(q, 4),
                         U32(q, 8), U32(q, 12), U32(q, 16));
                break;
            case OP_DRAW_UP:
                op = Fmt("DRAW_UP: type %u, %u primitives, stride %u", U32(q, 0), U32(q, 4), U32(q, 8));
                break;
            default:
                op = Fmt("DRAW_INDEXED_UP: type %u, min index %u, %u vertices, %u primitives, index format %u, stride %u",
                         U32(q, 0), U32(q, 4), U32(q, 8), U32(q, 12), U32(q, 16), U32(q, 20));
                break;
            }
        } else if (IsPixelOp(rec.op)) {
            for (const S_injection &j : injections)
                if (j.pixelOp == pixelOp)
                    apply(j.op, reinterpret_cast<const uint8_t *>(j.values.data()), static_cast<uint32_t>(j.values.size() * 4));
        } else {
            apply(rec.op, p.data(), rec.size);
        }
    }
    std::fclose(f);
    std::string s = "- operation: " + op + "\n";
    if (!injected.empty())
        s += "- **synthetic fault injected on the test side right before it:**\n" + injected;
    if (auto d = shaderDefs.find(shader); d != shaderDefs.end())
        s += Fmt("- vertex shader: declared handle %u (DEF_SHADER: usage 0x%x, %u declaration DWORDs, %u function DWORDs: %s)\n",
                 shader, d->second[0], d->second[1], d->second[2],
                 d->second[2] ? "a programmable vs.1.x shader" : "fixed-function with a stream declaration");
    else
        s += Fmt("- vertex shader: FVF 0x%x\n", shader);
    s += Fmt("- index buffer id %u; lights enabled mask 0x%x\n", indices, lights);
    if (viewportSet)
        s += Fmt("- viewport: %u,%u %ux%u z %g..%g\n", viewport.X, viewport.Y, viewport.Width, viewport.Height,
                 static_cast<double>(viewport.MinZ), static_cast<double>(viewport.MaxZ));
    else
        s += "- viewport: not set in the capture (the device's default: the whole back buffer)\n";
    for (const auto &st : streams)
        s += Fmt("- stream %u: vertex buffer id %u, stride %u\n", st.first, st.second.first, st.second.second);
    for (const auto &t : tex) {
        if (!t.second)
            continue;
        auto d = texDefs.find(t.second);
        if (d != texDefs.end())
            s += Fmt("- stage %u texture id %u: %ux%u, %u levels, usage 0x%x, format %u, pool %u\n", t.first, t.second,
                     d->second[0], d->second[1], d->second[2], d->second[3], d->second[4], d->second[5]);
        else
            s += Fmt("- stage %u texture id %u (definition not found)\n", t.first, t.second);
    }
    s += "- render states (as set in the capture up to here):";
    for (const auto &r : rs) {
        const char *name = RenderStateName(r.first);
        if (r.first == D3DRS_FOGSTART || r.first == D3DRS_FOGEND || r.first == D3DRS_FOGDENSITY) {
            float v;
            std::memcpy(&v, &r.second, 4);
            s += Fmt(" %s=%g", name, static_cast<double>(v));
        } else {
            s += name ? Fmt(" %s=%u", name, r.second) : Fmt(" %u=%u", r.first, r.second);
        }
    }
    s += "\n";
    for (const auto &stage : ts) {
        const auto colorOp = stage.second.find(D3DTSS_COLOROP);
        if (stage.first > 0 && colorOp != stage.second.end() && colorOp->second == D3DTOP_DISABLE)
            continue;
        s += Fmt("- stage %u:", stage.first);
        for (const auto &t : stage.second) {
            const char *name = StageStateName(t.first);
            s += name ? Fmt(" %s=%u", name, t.second) : Fmt(" %u=%u", t.first, t.second);
        }
        s += "\n";
    }
    return s;
}

// ---------------------------------------------------------------- the pipeline a backend gave the draw

// What the fixed renderer (SDL_GPU, OpenGL) recorded for a frame's last command, through
// fixed::SetFrameObserver. A prefix ends with the operation looked at, so its frame's last command is
// that operation's when the operation made one.
struct S_observed {
    bool seen = false;
    size_t commands = 0;
    fixed::S_command last = {};
    fixed::S_vertexUniforms vu = {};
    fixed::S_pixelUniforms pu = {};
    bool pretransformed = false, programmable = false;
    int32_t layoutFlags = 0;
    bool textured[fixed::kStages] = {};
};

void ObserveFrame(const fixed::S_frame &frame, void *context)
{
    S_observed &o = *static_cast<S_observed *>(context);
    if (frame.commands.empty())
        return;
    o = S_observed();
    o.seen = true;
    o.commands = frame.commands.size();
    o.last = frame.commands.back();
    if (o.last.vertexUniforms < frame.vertexBlocks.size())
        o.vu = frame.vertexBlocks[o.last.vertexUniforms];
    if (o.last.pixelUniforms < frame.pixelBlocks.size())
        o.pu = frame.pixelBlocks[o.last.pixelUniforms];
    if (o.last.layout) {
        o.pretransformed = o.last.layout->pretransformed;
        o.programmable = o.last.layout->programmable;
        o.layoutFlags = o.last.layout->flags;
    }
    for (int s = 0; s < fixed::kStages; ++s)
        o.textured[s] = o.last.textures[s] != nullptr;
    o.last.layout = nullptr;            // (the frame's: not kept)
    for (auto &t : o.last.textures)
        t = nullptr;
}

bool UsesFixedRenderer(const S_backend &b) { return b.name.rfind("d3d8", 0) != 0; }

// `path` replayed on `b` with the observer on: the pipeline of its frame's last command.
S_observed ObservePipeline(const S_backend &b, const std::string &path)
{
    S_observed o;
    if (!UsesFixedRenderer(b))
        return o;
    S_image picture;
    std::string error;
    int failed = 0;
    fixed::SetFrameObserver(ObserveFrame, &o);
    ReplayFile(b, path, &picture, &error, &failed);
    fixed::SetFrameObserver(nullptr, nullptr);
    return o;
}

std::string DescribePipeline(const S_backend &b, const S_observed &o, uint32_t pixelOps)
{
    if (!UsesFixedRenderer(b))
        return Fmt("- %s: drawn by the Direct3D 8 driver itself (no fixed-renderer pipeline)\n", b.name.c_str());
    if (!o.seen)
        return Fmt("- %s: no frame was recorded (nothing observed)\n", b.name.c_str());
    const fixed::S_pipelineKey &k = o.last.key;
    std::string s = Fmt("- %s: the frame's last command (%zu commands recorded for this prefix's %u pixel operations; "
                        "it is this operation's when the operation made one)\n", b.name.c_str(), o.commands, pixelOps);
    s += Fmt("  - `S_pipelineKey`: layout 0x%08x stride %u primitive %u; blend %u src %u dst %u; write mask 0x%x; depth "
             "test %u write %u func %u; cull %u fill %u; **zbias %u**; alpha to coverage %u\n",
             k.layout, k.stride, k.primitive, k.blend, k.srcBlend, k.dstBlend, k.writeMask, k.depthTest, k.depthWrite,
             k.depthFunc, k.cull, k.fill, k.zbias, k.alphaToCoverage);
    s += Fmt("  - vertex layout: %s, flags 0x%x; %u %s\n",
             o.programmable ? "programmable (vs.1.x)" : o.pretransformed ? "pre-transformed" : "transformed", o.layoutFlags,
             o.last.count, o.last.indexed ? "indices" : "vertices");
    s += Fmt("  - shader mode (`fixed_function.hlsl` VertexUniforms.mode): position %d, lighting %d, fog %d, flags 0x%x; "
             "fog start %g end %g density %g\n", o.vu.mode.x, o.vu.mode.y, o.vu.mode.z, o.vu.mode.w,
             static_cast<double>(o.vu.fog.x), static_cast<double>(o.vu.fog.y), static_cast<double>(o.vu.fog.z));
    s += Fmt("  - viewport %g,%g %gx%g z %g..%g; scissor %d,%d %dx%d\n", static_cast<double>(o.last.viewport.x),
             static_cast<double>(o.last.viewport.y), static_cast<double>(o.last.viewport.w), static_cast<double>(o.last.viewport.h),
             static_cast<double>(o.last.viewport.minZ), static_cast<double>(o.last.viewport.maxZ), o.last.scissor.x,
             o.last.scissor.y, o.last.scissor.w, o.last.scissor.h);
    for (int st = 0; st < fixed::kStages; ++st) {
        const fixed::S_samplerKey &sm = o.last.samplers[st];
        s += Fmt("  - stage %d: colour op %d (%d, %d), alpha op %d (%d, %d); texture %s; sampler min %u mag %u mip %u, "
                 "address %u/%u, anisotropy %u, lod bias %d; texgen %d/%d/0x%x\n", st, o.pu.colorStage[st].x,
                 o.pu.colorStage[st].y, o.pu.colorStage[st].z, o.pu.alphaStage[st].x, o.pu.alphaStage[st].y,
                 o.pu.alphaStage[st].z, o.textured[st] ? "bound" : "none", sm.minFilter, sm.magFilter, sm.mipFilter,
                 sm.addressU, sm.addressV, sm.anisotropy, sm.lodBias, o.vu.texGen[st].x, o.vu.texGen[st].y, o.vu.texGen[st].z);
        if (o.pu.colorStage[st].x == D3DTOP_DISABLE)
            break;
    }
    s += Fmt("  - pixel flags (fog, specular, alpha test, ref): %d %d %d %d; coverage %d\n", o.pu.pixelFlags.x,
             o.pu.pixelFlags.y, o.pu.pixelFlags.z, o.pu.pixelFlags.w, o.pu.coverage.x);
    return s;
}

// ---------------------------------------------------------------- the minimizer

struct S_minimized {
    bool ran = false, verified = false, budgetHit = false;
    std::vector<uint32_t> kept;         // the earlier pixel operations the repro keeps
    uint32_t candidates = 0;            // prefixes tried
    uint64_t need = 0, still = 0;       // of the mask's pixels: required, and differing in the result
    uint64_t bytes = 0, fullBytes = 0;
    std::string error;
};

// Delta debugging over the draws before `k` (every clear stays): the fewest of them (1-minimal, within the
// budget) whose prefix still shows the divergence, with only the resources those operations and `k`
// use. "Still shows" means: on the two backends, at least 90% of `mask` (the pixels that newly differed
// at `k` in the full capture) still differ by more than the tolerance. Every candidate is replayed, so
// what it returns reproduces by construction. Writes minimal.ls3dframe (the reference picture of the
// minimal prefix as its FRAME_IMAGE) and replays it once more to verify.
S_minimized Minimize(const char *path, const S_index &x, uint32_t k, const std::vector<uint32_t> &mask, const S_options &o)
{
    S_minimized m;
    m.ran = true;
    if (mask.empty()) {
        m.error = "no newly differing pixels to keep";
        return m;
    }
    m.need = std::max<uint64_t>(1, (mask.size() * 9 + 9) / 10);
    const S_usage usage = ComputeUsage(path, x, k, o.injections);
    const std::string refPath = o.out + "\\min_ref.ls3dframe", testPath = o.out + "\\min_test.ls3dframe";
    S_image bestExpected, bestActual;
    // Clears stay: they set the buffers' first contents, which without them are whatever the driver
    // left (a repro resting on that would not be one).
    std::vector<uint32_t> clears, list;
    for (uint32_t j = 1; j < k; ++j)
        (x.records[x.pixelOps[j - 1]].op == OP_CLEAR ? clears : list).push_back(j);
    auto with = [&](std::vector<uint32_t> earlier) {
        earlier.insert(earlier.end(), clears.begin(), clears.end());
        std::sort(earlier.begin(), earlier.end());
        return earlier;
    };
    auto holds = [&](const std::vector<uint32_t> &draws) {
        const std::vector<uint32_t> earlier = with(draws);
        if (m.candidates >= o.minimizeBudget) {
            m.budgetHit = true;
            return false;
        }
        ++m.candidates;
        const S_subset subset = MakeSubset(usage, k, earlier);
        S_image expected, actual;
        std::string error;
        int failed = 0;
        if (!WritePrefix(path, x, k, {}, nullptr, refPath, &subset) ||
            !WritePrefix(path, x, k, o.injections, nullptr, testPath, &subset) ||
            !ReplayFile(o.reference, refPath, &expected, &error, &failed) ||
            !ReplayFile(o.test, testPath, &actual, &error, &failed) || expected.width != actual.width ||
            expected.height != actual.height || expected.bgr.size() <= mask.back() + 2)
            return false;
        uint64_t still = 0;
        for (uint32_t i : mask)
            for (int ch = 0; ch < 3; ++ch)
                if (std::abs(static_cast<int>(expected.bgr[i + ch]) - static_cast<int>(actual.bgr[i + ch])) >
                    static_cast<int>(o.tolerance)) {
                    ++still;
                    break;
                }
        if (!o.quiet)
            std::printf("    minimize: %zu earlier operations kept: %llu of %zu pixels still differ%s\n", earlier.size(),
                        static_cast<unsigned long long>(still), mask.size(), still >= m.need ? "  (holds)" : "");
        if (still < m.need)
            return false;
        m.still = still;
        bestExpected.bgr.swap(expected.bgr);
        bestExpected.width = expected.width;
        bestExpected.height = expected.height;
        bestActual.bgr.swap(actual.bgr);
        bestActual.width = actual.width;
        bestActual.height = actual.height;
        return true;
    };
    // The full prefix holds (it is the search's own); it is tried first only for its pictures.
    if (!holds(list)) {
        m.error = "the full prefix does not show the divergence again";
        DeleteFileA(refPath.c_str());
        DeleteFileA(testPath.c_str());
        return m;
    }
    if (holds({})) {
        list.clear();
    } else {
        size_t n = 2;
        while (list.size() >= 2 && !m.budgetHit) {
            n = std::min(n, list.size());
            std::vector<std::vector<uint32_t>> chunks(n);
            for (size_t i = 0; i < list.size(); ++i)
                chunks[i * n / list.size()].push_back(list[i]);
            bool reduced = false;
            for (size_t c = 0; c < n && !reduced; ++c)
                if (holds(chunks[c])) {
                    list = chunks[c];
                    n = 2;
                    reduced = true;
                }
            for (size_t c = 0; c < n && !reduced && n > 2; ++c) {
                std::vector<uint32_t> rest;
                for (size_t d = 0; d < n; ++d)
                    if (d != c)
                        rest.insert(rest.end(), chunks[d].begin(), chunks[d].end());
                if (holds(rest)) {
                    list = rest;
                    n = std::max<size_t>(n - 1, 2);
                    reduced = true;
                }
            }
            if (!reduced) {
                if (n >= list.size())
                    break;
                n = std::min(n * 2, list.size());
            }
        }
    }
    DeleteFileA(refPath.c_str());
    DeleteFileA(testPath.c_str());
    m.kept = with(list);
    const std::string minimal = o.out + "\\minimal.ls3dframe";
    const S_subset subset = MakeSubset(usage, k, m.kept);
    if (!WritePrefix(path, x, k, o.injections, &bestExpected, minimal, &subset)) {
        m.error = "cannot write " + minimal;
        return m;
    }
    S_index mini;
    IndexCapture(minimal.c_str(), &mini);
    m.fullBytes = x.records[x.pixelOps[k - 1]].offset + x.records[x.pixelOps[k - 1]].size;
    m.bytes = mini.fileBytes;
    if (!mini.errors.empty()) {
        m.error = "minimal.ls3dframe does not validate: " + mini.errors[0];
        return m;
    }
    S_image again;
    std::string error;
    int failed = 0;
    if (ReplayFile(o.test, minimal, &again, &error, &failed))
        m.verified = Hash(again) == Hash(bestActual) && replay::Compare(bestExpected, again, o.tolerance).pixelsOver > 0;
    return m;
}

// ---------------------------------------------------------------- one analysis

struct S_box {
    uint32_t x0 = UINT32_MAX, y0 = UINT32_MAX, x1 = 0, y1 = 0;
    uint64_t pixels = 0;
};

S_box DivergentBox(const S_image &a, const S_image &b, uint32_t tolerance, S_image *heat)
{
    S_box box;
    if (a.width != b.width || a.height != b.height)
        return box;
    replay::Compare(a, b, tolerance, heat);
    for (uint32_t y = 0; y < a.height; ++y)
        for (uint32_t x = 0; x < a.width; ++x) {
            const size_t i = (static_cast<size_t>(y) * a.width + x) * 3;
            int worst = 0;
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(static_cast<int>(a.bgr[i + c]) - static_cast<int>(b.bgr[i + c])));
            if (worst > static_cast<int>(tolerance)) {
                ++box.pixels;
                box.x0 = std::min(box.x0, x);
                box.y0 = std::min(box.y0, y);
                box.x1 = std::max(box.x1, x);
                box.y1 = std::max(box.y1, y);
            }
        }
    return box;
}

S_result Analyse(const char *capture, const S_options &o)
{
    S_result result;
    S_index x;
    if (!IndexCapture(capture, &x)) {
        result.why = "cannot open the capture";
        std::printf("%s: cannot open\n", capture);
        return result;
    }
    if (!o.quiet)
        PrintIndex(capture, x);
    if (!x.errors.empty()) {
        result.why = "the capture is malformed: " + x.errors[0];
        std::printf("%s: not replayed: %zu validation errors\n", capture, x.errors.size());
        return result;
    }
    for (const S_injection &j : o.injections)
        if (j.pixelOp > x.pixelOps.size()) {
            result.why = Fmt("--inject %s: the capture has %zu pixel operations", j.text.c_str(), x.pixelOps.size());
            std::printf("%s\n", result.why.c_str());
            return result;
        }
    CreateDirectoryA(o.out.c_str(), nullptr);
    if (!o.quiet)
        std::printf("  reference %s, test %s, tolerance %u, min pixels %llu\n", o.reference.name.c_str(), o.test.name.c_str(),
                    o.tolerance, static_cast<unsigned long long>(o.minPixels));
    Search search(capture, x, o);
    result = search.Run();
    DeleteFileA((o.out + "\\work_ref.ls3dframe").c_str());
    DeleteFileA((o.out + "\\work_test.ls3dframe").c_str());

    // The report.
    std::string md = "# E01 render divergence report\n\n";
    md += Fmt("- capture: `%s` (%llu bytes, %zu records, %ux%u, %zu pixel operations)\n", capture,
              static_cast<unsigned long long>(x.fileBytes), x.records.size(), x.width, x.height, x.pixelOps.size());
    md += Fmt("- reference: %s; test: %s; tolerance %u per channel; divergent when more than %llu pixels exceed it\n",
              o.reference.name.c_str(), o.test.name.c_str(), o.tolerance, static_cast<unsigned long long>(o.minPixels));
    md += Fmt("- checkpoints: every %u operations, then one by one inside the first differing window; %u replays, each "
              "a prefix from the capture's first record on a newly created device\n", search.stride, result.replays);
    if (!o.injections.empty()) {
        md += "- **synthetic faults injected on the test side:**";
        for (const S_injection &j : o.injections)
            md += " `" + j.text + "`";
        md += "\n";
    }
    md += "\n## Result\n\n";
    if (result.verdict == NO_DIVERGENCE) {
        md += Fmt("No divergence at the checkpoints visited (stride %u). A difference that a later operation paints over "
                  "completely is only visible with `--stride 1`.\n", search.stride);
    } else if (result.op == 0) {
        md += "**Not localized.** " + result.why + "\n";
    } else {
        const S_checkpoint &c = search.points[result.op];
        S_image heat;
        const S_box box = DivergentBox(c.expected, c.actual, o.tolerance, &heat);
        md += result.verdict == LOCALIZED ? Fmt("**First divergent operation: %u** (record %u, %s).\n\n", result.op, result.record,
                                                OpName(x.records[result.record].op))
                                          : Fmt("**Not localized** (first differing checkpoint: operation %u): %s\n\n",
                                                result.op, result.why.c_str());
        md += Fmt("- %llu pixels over tolerance (largest channel difference %u, %.1f dB) in x %u..%u, y %u..%u\n",
                  static_cast<unsigned long long>(box.pixels), c.d.maxChannel, c.d.psnr, box.x0, box.x1, box.y0, box.y1);
        if (result.op > 1)
            md += Fmt("- operation %u (the one before) matched; %llu pixels newly differ, all drawn by operation %u on one "
                      "side or the other: %s\n", result.op - 1, static_cast<unsigned long long>(search.stats.newly), result.op,
                      search.stats.untouched ? "NO" : "yes");
        else
            md += "- it is the first operation of the frame: nothing before it to compare\n";
        md += Fmt("- picture hashes (FNV-1a over BGR): expected %016llx, actual %016llx\n",
                  static_cast<unsigned long long>(Hash(c.expected)), static_cast<unsigned long long>(Hash(c.actual)));
        md += Fmt("- device calls that failed during the two replays: %d\n", c.failedCalls);
        if (c.refDepth || c.testDepth)
            md += Fmt("- **depth buffer not the capture's**: the driver lacked its format; %s%s%s. Depth-test results may "
                      "differ from the game's for that reason alone\n",
                      c.refDepth ? Fmt("%s used format %u", o.reference.name.c_str(), c.refDepth).c_str() : "",
                      c.refDepth && c.testDepth ? ", " : "",
                      c.testDepth ? Fmt("%s used format %u", o.test.name.c_str(), c.testDepth).c_str() : "");
        md += "\n## State at the operation\n\n" + DescribeState(capture, x, result.op, o.injections);
        replay::WritePng((o.out + "\\expected.png").c_str(), c.expected);
        replay::WritePng((o.out + "\\actual.png").c_str(), c.actual);
        replay::WritePng((o.out + "\\diff.png").c_str(), heat);
        if (result.op > 1 && search.points[result.op - 1].ok) {
            replay::WritePng((o.out + "\\before_expected.png").c_str(), search.points[result.op - 1].expected);
            replay::WritePng((o.out + "\\before_actual.png").c_str(), search.points[result.op - 1].actual);
        }
        // The repro: the test side's prefix, with the reference picture to compare against.
        const std::string repro = o.out + "\\repro.ls3dframe";
        if (WritePrefix(capture, x, result.op, o.injections, &c.expected, repro)) {
            S_image again;
            std::string error;
            int failed = 0;
            if (ReplayFile(o.test, repro, &again, &error, &failed)) {
                const replay::S_difference d = replay::Compare(c.expected, again, o.tolerance);
                result.reproVerified = d.pixelsOver > o.minPixels && Hash(again) == Hash(c.actual);
            }
            md += Fmt("\n## Reproduction\n\n`repro.ls3dframe`: the capture's first %u records up to this operation%s, then "
                      "the expected picture as its FRAME_IMAGE. Replayed again on %s: %s.\n\n",
                      result.record + 1, o.injections.empty() ? "" : " with the injected records",
                      o.test.name.c_str(), result.reproVerified ? "the same divergence (verified)" : "NOT reproduced");
            md += Fmt("```\ntest_render_replay.exe repro.ls3dframe --backend %s --tolerance %u\n```\n\n"
                      "(It fails there, as it should: the picture differs from the expected one. Expected/actual/diff "
                      "PNGs are next to this report; diff.png shows channel differences x4.)\n",
                      o.test.name.c_str(), o.tolerance);
        }
        // The pipeline each side's fixed renderer gave the operation (SDL_GPU, OpenGL).
        md += "\n## Pipeline at the operation (backend side)\n\n";
        const S_observed testSide = ObservePipeline(o.test, repro);
        S_observed refSide;
        if (UsesFixedRenderer(o.reference)) {
            const std::string refPath = o.out + "\\work_ref.ls3dframe";
            WritePrefix(capture, x, result.op, {}, nullptr, refPath);
            refSide = ObservePipeline(o.reference, refPath);
            DeleteFileA(refPath.c_str());
        }
        md += DescribePipeline(o.reference, refSide, result.op);
        md += DescribePipeline(o.test, testSide, result.op);
        // The smallest prefix that still shows it.
        if (o.minimize && result.verdict == LOCALIZED) {
            if (!o.quiet)
                std::printf("  minimizing (budget %u candidates)\n", o.minimizeBudget);
            const S_minimized m = Minimize(capture, x, result.op, search.stats.mask, o);
            result.minimized = m.verified;
            result.minimalKept = m.kept;
            md += "\n## Minimal reproduction\n\n";
            if (!m.error.empty()) {
                md += "Not made: " + m.error + "\n";
            } else {
                md += Fmt("`minimal.ls3dframe`: operation %u and %zu of the %u operations before it, with only the resources "
                          "they use: %llu bytes (the prefix: %llu). %u candidate prefixes replayed%s. %llu of the %zu newly "
                          "differing pixels still differ (at least %llu required). Replayed again on %s: %s.\n\n",
                          result.op, m.kept.size(), result.op - 1, static_cast<unsigned long long>(m.bytes),
                          static_cast<unsigned long long>(m.fullBytes), m.candidates,
                          m.budgetHit ? " (budget reached: smaller than the prefix, not necessarily 1-minimal)" : " (1-minimal)",
                          static_cast<unsigned long long>(m.still), search.stats.mask.size(),
                          static_cast<unsigned long long>(m.need), o.test.name.c_str(),
                          m.verified ? "the same picture, still differing (verified)" : "NOT reproduced");
                md += "Earlier operations kept:";
                for (size_t i = 0; i < m.kept.size() && i < 64; ++i)
                    md += Fmt(" %u", m.kept[i]);
                md += m.kept.size() > 64 ? " ...\n" : m.kept.empty() ? " none\n" : "\n";
                md += Fmt("\n```\ntest_render_replay.exe minimal.ls3dframe --backend %s --tolerance %u\n```\n",
                          o.test.name.c_str(), o.tolerance);
            }
            if (!o.quiet || !m.verified)
                std::printf("  minimal: %zu earlier operations kept, %llu bytes, %u candidates%s%s\n", m.kept.size(),
                            static_cast<unsigned long long>(m.bytes), m.candidates, m.verified ? ", verified" : ", NOT verified",
                            m.error.empty() ? "" : (" (" + m.error + ")").c_str());
        }
    }
    md += "\n## Limits\n\n- Replays are compared with each other, not with the game's own picture: the capture holds "
          "only the final one (OP_FRAME_IMAGE).\n- An operation's effect that later operations paint over is found only "
          "at `--stride 1`.\n- The report names where the pictures first differ, not why the backend draws it "
          "differently. The pipeline listed for SDL_GPU and OpenGL is the frame's last command: the operation's own "
          "when it made one.\n";
    if (FILE *f = std::fopen((o.out + "\\report.md").c_str(), "wb")) {
        std::fwrite(md.data(), md.size(), 1, f);
        std::fclose(f);
    }
    if (!o.quiet || result.verdict != NO_DIVERGENCE)
        std::printf("  %s%s; report %s\\report.md\n",
                    result.verdict == NO_DIVERGENCE ? "no divergence"
                    : result.verdict == LOCALIZED   ? Fmt("first divergent operation %u (%s)", result.op,
                                                          OpName(x.records[result.record].op)).c_str()
                                                    : ("not localized: " + result.why).c_str(),
                    result.reproVerified ? ", repro verified" : "", o.out.c_str());
    return result;
}

// ---------------------------------------------------------------- the self-test

struct S_screenVertex {
    float x, y, z, rhw;
    DWORD diffuse;
    float u, v;
};

class Writer {
public:
    std::vector<uint8_t> bytes;
    Writer() { bytes.assign(kMagic, kMagic + sizeof kMagic); }
    void Rec(E_op op, std::initializer_list<uint32_t> values, const void *tail = nullptr, size_t tailBytes = 0)
    {
        const uint32_t size = static_cast<uint32_t>(values.size() * 4 + tailBytes);
        Put(op);
        Put(size);
        for (uint32_t v : values)
            Put(v);
        if (tailBytes)
            bytes.insert(bytes.end(), static_cast<const uint8_t *>(tail), static_cast<const uint8_t *>(tail) + tailBytes);
    }
    void Put(uint32_t v) { bytes.insert(bytes.end(), reinterpret_cast<uint8_t *>(&v), reinterpret_cast<uint8_t *>(&v) + 4); }
    void Quad(float x0, float y0, float x1, float y1, float z, DWORD colour)
    {
        const S_screenVertex v[6] = {{x0, y0, z, 1, colour, 0, 0}, {x1, y0, z, 1, colour, 1, 0}, {x0, y1, z, 1, colour, 0, 1},
                                     {x1, y0, z, 1, colour, 1, 0}, {x1, y1, z, 1, colour, 1, 1}, {x0, y1, z, 1, colour, 0, 1}};
        Rec(OP_DRAW_UP, {D3DPT_TRIANGLELIST, 2, sizeof(S_screenVertex)}, v, sizeof v);
    }
    bool Save(const std::string &path) const
    {
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;
        std::fwrite(bytes.data(), bytes.size(), 1, f);
        return std::fclose(f) == 0;
    }
};

// 64x64: a clear and seven quads (pixel operations 1..8). 1 clear; 2 opaque red; 3 textured (id 1);
// 4 near green; 5 alpha-blended blue over 2..4; 6 far yellow, partly behind 4 and 5; 7 magenta,
// 8 white over all of 7 (ZFUNC set again before it, as games do).
Writer SyntheticScene(bool undefinedTexture = false)
{
    Writer w;
    w.Rec(OP_HEADER, {64, 64, D3DFMT_X8R8G8B8, D3DFMT_D16, 0, 1, 0});
    const uint32_t states[][2] = {{D3DRS_LIGHTING, 0}, {D3DRS_CULLMODE, 1}, {D3DRS_ZENABLE, 1},
                                  {D3DRS_ZWRITEENABLE, 1}, {D3DRS_ZFUNC, D3DCMP_LESSEQUAL}, {D3DRS_ALPHABLENDENABLE, 0},
                                  {D3DRS_SRCBLEND, D3DBLEND_SRCALPHA}, {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA},
                                  {D3DRS_FOGENABLE, 0}, {D3DRS_ALPHATESTENABLE, 0}, {D3DRS_SPECULARENABLE, 0},
                                  {D3DRS_DITHERENABLE, 0}};
    for (const auto &s : states)
        w.Rec(OP_RENDER_STATE, {s[0], s[1]});
    const uint32_t stage0[][2] = {{D3DTSS_COLOROP, D3DTOP_SELECTARG1}, {D3DTSS_COLORARG1, D3DTA_DIFFUSE},
                                  {D3DTSS_COLORARG2, D3DTA_DIFFUSE}, {D3DTSS_ALPHAOP, D3DTOP_SELECTARG1},
                                  {D3DTSS_ALPHAARG1, D3DTA_DIFFUSE}, {D3DTSS_MAGFILTER, D3DTEXF_POINT},
                                  {D3DTSS_MINFILTER, D3DTEXF_POINT}, {D3DTSS_MIPFILTER, D3DTEXF_NONE}};
    for (const auto &s : stage0)
        w.Rec(OP_STAGE_STATE, {0, s[0], s[1]});
    w.Rec(OP_STAGE_STATE, {1, D3DTSS_COLOROP, D3DTOP_DISABLE});
    w.Rec(OP_STAGE_STATE, {1, D3DTSS_ALPHAOP, D3DTOP_DISABLE});
    const D3DVIEWPORT8 viewport = {0, 0, 64, 64, 0.0f, 1.0f};
    w.Rec(OP_VIEWPORT, {}, &viewport, sizeof viewport);
    w.Rec(OP_VERTEX_SHADER, {D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1});
    // Two 4x4 checkers: red/white (1) and blue/yellow (2).
    for (uint32_t id = 1; id <= 2; ++id) {
        w.Rec(OP_DEF_TEXTURE, {id, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED});
        uint32_t texels[16];
        for (int i = 0; i < 16; ++i)
            texels[i] = ((i % 4 + i / 4) & 1) ? 0xffffffffu : id == 1 ? 0xffff0000u : 0xff0000ffu;
        if (id == 2)
            for (uint32_t &t : texels)
                t = t == 0xffffffffu ? 0xffffff00u : t;
        w.Rec(OP_TEXTURE_DATA, {id, 0, 0, 0, 4, 4}, texels, sizeof texels);
    }
    w.Rec(OP_TEXTURE, {0, undefinedTexture ? 99u : 0u});
    w.Rec(OP_BEGIN_SCENE, {});
    const float one = 1.0f;
    uint32_t z1;
    std::memcpy(&z1, &one, 4);
    w.Rec(OP_CLEAR, {0, D3DCLEAR_TARGET | 2 /* ZBUFFER */, 0xff202020u, z1, 0});         // 1
    w.Quad(4, 4, 28, 28, 0.5f, 0xffff0000u);                                              // 2
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLOROP, D3DTOP_MODULATE});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLORARG1, D3DTA_TEXTURE});
    w.Rec(OP_TEXTURE, {0, 1});
    w.Quad(36, 4, 60, 28, 0.5f, 0xffffffffu);                                             // 3
    w.Rec(OP_TEXTURE, {0, 0});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLOROP, D3DTOP_SELECTARG1});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLORARG1, D3DTA_DIFFUSE});
    w.Quad(4, 36, 40, 60, 0.3f, 0xff00ff00u);                                             // 4
    w.Rec(OP_RENDER_STATE, {D3DRS_ALPHABLENDENABLE, 1});
    w.Quad(20, 20, 44, 44, 0.2f, 0x800000ffu);                                            // 5
    w.Rec(OP_RENDER_STATE, {D3DRS_ALPHABLENDENABLE, 0});
    w.Quad(24, 40, 60, 60, 0.6f, 0xffffff00u);                                            // 6
    w.Quad(46, 46, 58, 58, 0.1f, 0xffff00ffu);                                            // 7
    w.Rec(OP_RENDER_STATE, {D3DRS_ZFUNC, D3DCMP_LESSEQUAL});
    w.Quad(40, 40, 64, 64, 0.05f, 0xffffffffu);                                           // 8
    w.Rec(OP_END_SCENE, {});
    w.Rec(OP_PRESENT, {});
    return w;
}

int SelfTest(const std::string &outRoot, S_backend d3d8)
{
    int failures = 0, notRun = 0;
    CreateDirectoryA(outRoot.c_str(), nullptr);
    const std::string scene = outRoot + "\\synthetic.ls3dframe";
    const Writer good = SyntheticScene();
    good.Save(scene);

    // 1. The validator, offline.
    struct S_bad {
        const char *what;
        std::vector<uint8_t> bytes;
        const char *expect;             // a substring of the first error
    };
    std::vector<S_bad> bad;
    bad.push_back({"cut short", std::vector<uint8_t>(good.bytes.begin(), good.bytes.end() - 30), "claims"});
    {
        std::vector<uint8_t> b = good.bytes;
        const uint32_t huge = 0x7fffffff;
        std::memcpy(b.data() + 8 + 4, &huge, 4);                 // the HEADER's size
        bad.push_back({"oversized record", b, "claims"});
    }
    bad.push_back({"texture used before definition", SyntheticScene(true).bytes, "used before it is defined"});
    {
        Writer w = SyntheticScene();
        // A second definition of texture 1 appended before PRESENT is enough: strip the last record.
        w.bytes.resize(w.bytes.size() - 8);
        w.Rec(OP_DEF_TEXTURE, {1, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED});
        w.Rec(OP_PRESENT, {});
        bad.push_back({"id redefined while live", w.bytes, "defined again while live"});
    }
    {
        Writer w = SyntheticScene();
        w.bytes.resize(w.bytes.size() - 8);
        w.Rec(OP_DRAW_UP, {D3DPT_TRIANGLELIST, 2, 28});          // no vertices
        w.Rec(OP_PRESENT, {});
        bad.push_back({"draw without its vertices", w.bytes, "its layout needs"});
    }
    {
        S_index x;
        IndexCapture(scene.c_str(), &x);
        const bool ok = x.errors.empty() && x.pixelOps.size() == 8;
        std::printf("selftest validator: synthetic capture valid, %zu pixel operations: %s\n", x.pixelOps.size(), ok ? "ok" : "FAIL");
        failures += !ok;
    }
    for (const S_bad &b : bad) {
        const std::string path = outRoot + "\\bad.ls3dframe";
        FILE *f = std::fopen(path.c_str(), "wb");
        std::fwrite(b.bytes.data(), b.bytes.size(), 1, f);
        std::fclose(f);
        S_index x;
        IndexCapture(path.c_str(), &x);
        const bool ok = !x.errors.empty() && x.errors[0].find(b.expect) != std::string::npos;
        std::printf("selftest validator: %s -> %s: %s\n", b.what, x.errors.empty() ? "(no error)" : x.errors[0].c_str(),
                    ok ? "ok" : "FAIL");
        failures += !ok;
        // A malformed capture is never replayed.
        if (ok) {
            S_options o;
            o.reference = o.test = d3d8;
            o.out = outRoot + "\\bad.e01";
            o.quiet = true;
            const S_result r = Analyse(path.c_str(), o);
            if (r.verdict != BAD_INPUT) {
                std::printf("selftest validator: %s was replayed  FAIL\n", b.what);
                ++failures;
            }
        }
        DeleteFileA(path.c_str());
    }

    // 2. Localization, on Direct3D 8 against itself.
    struct S_case {
        const char *name;
        std::vector<const char *> inject;
        uint32_t stride;
        E_verdict verdict;
        uint32_t op;
        bool minimize = false;
        std::vector<uint32_t> mustKeep = {}, mustDrop = {};     // earlier operations the minimal repro keeps / drops
    };
    const S_case cases[] = {
        {"no fault", {}, 1, NO_DIVERGENCE, 0},
        {"no fault (coarse)", {}, 0, NO_DIVERGENCE, 0},
        {"texture", {"3:tex:0:2"}, 0, LOCALIZED, 3},
        {"blend", {"5:rs:20:1"}, 0, LOCALIZED, 5},
        {"depth", {"6:rs:23:8"}, 0, LOCALIZED, 6},
        {"fault before the clear", {"1:rs:23:1"}, 0, LOCALIZED, 2},  // ZFUNC NEVER: the clear ignores it, op 2 vanishes
        {"no-op state (fog colour, fog off)", {"2:rs:34:16711935"}, 1, NO_DIVERGENCE, 0},
        {"overdrawn (stride 1)", {"7:rs:23:1"}, 1, LOCALIZED, 7},
        {"overdrawn (coarse: not visible at its checkpoints)", {"7:rs:23:1"}, 0, NO_DIVERGENCE, 0},
        // The minimizer: op 6 drawn with ZFUNC ALWAYS shows where 4 (and, 16 pixels of them, 5) hid it; 2 and 3 are elsewhere
        // (and texture 1, op 3's alone, goes with it).
        {"depth, minimized", {"6:rs:23:8"}, 0, LOCALIZED, 6, true, {1, 4}, {2, 3}},
        {"texture, minimized", {"3:tex:0:2"}, 0, LOCALIZED, 3, true, {1}, {2}},
    };
    for (const S_case &c : cases) {
        S_options o;
        o.reference = o.test = d3d8;
        o.stride = c.stride;
        o.quiet = true;
        o.out = outRoot + "\\" + std::to_string(&c - cases);
        o.minimize = c.minimize;
        for (const char *s : c.inject) {
            S_injection j;
            ParseInjection(s, &j);
            o.injections.push_back(j);
        }
        const S_result r = Analyse(scene.c_str(), o);
        if (r.verdict == NOT_LOCALIZED && r.op == 0 && r.why.find("failed") != std::string::npos) {
            std::printf("selftest %s: NOT RUN (%s)\n", c.name, r.why.c_str());
            ++notRun;
            continue;
        }
        bool ok = r.verdict == c.verdict && r.op == c.op && (c.verdict != LOCALIZED || r.reproVerified);
        if (c.minimize) {
            auto has = [&](uint32_t j) { return std::find(r.minimalKept.begin(), r.minimalKept.end(), j) != r.minimalKept.end(); };
            bool kept = r.minimized;
            for (uint32_t j : c.mustKeep)
                kept = kept && has(j);
            for (uint32_t j : c.mustDrop)
                kept = kept && !has(j);
            std::string list;
            for (uint32_t j : r.minimalKept)
                list += Fmt(" %u", j);
            std::printf("selftest %s: minimal repro %s, earlier operations kept:%s\n", c.name,
                        r.minimized ? "verified" : "NOT made or not verified", list.empty() ? " none" : list.c_str());
            ok = ok && kept;
        }
        std::printf("selftest %s: expected %s, got %s%s (%u replays): %s\n", c.name,
                    c.verdict == NO_DIVERGENCE ? "none" : Fmt("op %u", c.op).c_str(),
                    r.verdict == NO_DIVERGENCE ? "none" : r.verdict == LOCALIZED ? Fmt("op %u", r.op).c_str() : ("not localized: " + r.why).c_str(),
                    r.reproVerified ? ", repro verified" : "", r.replays, ok ? "ok" : "FAIL");
        failures += !ok;
    }
    std::printf("test_render_divergence selftest: %d failed, %d not run\n", failures, notRun);
    return failures ? 1 : 0;
}

}  // namespace

// --zbias-probe: how far ZBIAS n moves a draw nearer on a backend, measured. A full-screen green quad at
// depth z0 (depth written), then a red one d farther (ZBIAS n, no depth write, LESSEQUAL): the largest d at
// which the red one still shows is the bias. "slope": both quads' depth rises 0.25 across the 64 pixels.
bool ProbeVisible(const S_backend &b, const std::string &path, uint32_t zbias, float z0, float d, float slope, bool *visible)
{
    Writer w;
    w.Rec(OP_HEADER, {64, 64, D3DFMT_X8R8G8B8, D3DFMT_D24X8, 0, 1, 0});
    const uint32_t states[][2] = {{D3DRS_LIGHTING, 0}, {D3DRS_CULLMODE, 1}, {D3DRS_ZENABLE, 1}, {D3DRS_ZWRITEENABLE, 1},
                                  {D3DRS_ZFUNC, D3DCMP_LESSEQUAL}, {D3DRS_ALPHABLENDENABLE, 0}, {D3DRS_FOGENABLE, 0},
                                  {D3DRS_ALPHATESTENABLE, 0}, {D3DRS_SPECULARENABLE, 0}, {D3DRS_DITHERENABLE, 0},
                                  {D3DRS_ZBIAS, 0}};
    for (const auto &s : states)
        w.Rec(OP_RENDER_STATE, {s[0], s[1]});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLOROP, D3DTOP_SELECTARG1});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_COLORARG1, D3DTA_DIFFUSE});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1});
    w.Rec(OP_STAGE_STATE, {0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE});
    w.Rec(OP_STAGE_STATE, {1, D3DTSS_COLOROP, D3DTOP_DISABLE});
    w.Rec(OP_STAGE_STATE, {1, D3DTSS_ALPHAOP, D3DTOP_DISABLE});
    const D3DVIEWPORT8 viewport = {0, 0, 64, 64, 0.0f, 1.0f};
    w.Rec(OP_VIEWPORT, {}, &viewport, sizeof viewport);
    w.Rec(OP_VERTEX_SHADER, {D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1});
    w.Rec(OP_BEGIN_SCENE, {});
    const float one = 1.0f;
    uint32_t z1;
    std::memcpy(&z1, &one, 4);
    w.Rec(OP_CLEAR, {0, D3DCLEAR_TARGET | 2 /* ZBUFFER */, 0xff000000u, z1, 0});
    auto quad = [&](float z, DWORD colour) {
        // Depth z at the centre, rising `slope` from left to right.
        const float l = z - slope * 0.5f, r = z + slope * 0.5f;
        const S_screenVertex v[6] = {{0, 0, l, 1, colour, 0, 0},  {64, 0, r, 1, colour, 1, 0},  {0, 64, l, 1, colour, 0, 1},
                                     {64, 0, r, 1, colour, 1, 0}, {64, 64, r, 1, colour, 1, 1}, {0, 64, l, 1, colour, 0, 1}};
        w.Rec(OP_DRAW_UP, {D3DPT_TRIANGLELIST, 2, sizeof(S_screenVertex)}, v, sizeof v);
    };
    quad(z0, 0xff00ff00u);
    w.Rec(OP_RENDER_STATE, {D3DRS_ZWRITEENABLE, 0});
    w.Rec(OP_RENDER_STATE, {D3DRS_ZBIAS, zbias});
    quad(z0 + d, 0xffff0000u);
    w.Rec(OP_END_SCENE, {});
    w.Rec(OP_PRESENT, {});
    if (!w.Save(path))
        return false;
    S_image picture;
    std::string error;
    int failed = 0;
    if (!ReplayFile(b, path, &picture, &error, &failed) || picture.width != 64) {
        std::printf("probe replay on %s: %s\n", b.name.c_str(), error.c_str());
        return false;
    }
    const uint8_t *p = &picture.bgr[(32 * 64 + 32) * 3];        // the centre pixel, B G R
    *visible = p[2] > 128 && p[1] < 128;
    return true;
}

int ZBiasProbe(const S_backend &b, const std::string &outRoot)
{
    CreateDirectoryA(outRoot.c_str(), nullptr);
    const std::string path = outRoot + "\\zbias_probe.ls3dframe";
    std::printf("zbias probe on %s: the largest depth gap (0..1 depth range) a ZBIAS draw still wins\n", b.name.c_str());
    std::printf("%-6s %-6s %-6s %12s %14s\n", "zbias", "z0", "slope", "gap", "gap/zbias");
    for (float slope : {0.0f, 0.25f})
        for (float z0 : {0.1f, 0.5f, 0.9f, 0.99f})
            for (uint32_t n : {0u, 1u, 2u, 8u, 16u}) {
                // visible at lo, not at hi
                float lo = 0.0f, hi = 0.01f;
                bool v = false;
                if (!ProbeVisible(b, path, n, z0, lo, slope, &v))
                    return 1;
                if (!v) {
                    std::printf("%-6u %-6.2f %-6.2f %12s\n", n, z0, slope, "not even at 0");
                    continue;
                }
                if (!ProbeVisible(b, path, n, z0, hi, slope, &v))
                    return 1;
                if (v) {
                    std::printf("%-6u %-6.2f %-6.2f %12s\n", n, z0, slope, "> 0.01");
                    continue;
                }
                for (int i = 0; i < 22; ++i) {
                    const float mid = (lo + hi) * 0.5f;
                    if (!ProbeVisible(b, path, n, z0, mid, slope, &v))
                        return 1;
                    (v ? lo : hi) = mid;
                }
                std::printf("%-6u %-6.2f %-6.2f %12.3e %14.3e\n", n, z0, slope, lo, n ? lo / n : 0.0f);
            }
    return 0;
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::vector<std::string> captures;
    S_options o;
    std::string reference = "d3d8", backend = "d3d8", driver, d3d8Path;
    bool index = false, selfTest = false, probe = false;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const bool more = i + 1 < argc;
        if (!std::strcmp(a, "--index"))
            index = true;
        else if (!std::strcmp(a, "--selftest"))
            selfTest = true;
        else if (!std::strcmp(a, "--minimize"))
            o.minimize = true;
        else if (!std::strcmp(a, "--minimize-budget") && more)
            o.minimizeBudget = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (!std::strcmp(a, "--zbias-probe"))
            probe = true;
        else if (!std::strcmp(a, "--reference") && more)
            reference = argv[++i];
        else if (!std::strcmp(a, "--backend") && more)
            backend = argv[++i];
        else if (!std::strcmp(a, "--driver") && more)
            driver = argv[++i];
        else if (!std::strcmp(a, "--d3d8") && more)
            d3d8Path = argv[++i];
        else if (!std::strcmp(a, "--tolerance") && more)
            o.tolerance = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (!std::strcmp(a, "--min-pixels") && more)
            o.minPixels = static_cast<uint64_t>(std::atoll(argv[++i]));
        else if (!std::strcmp(a, "--stride") && more)
            o.stride = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (!std::strcmp(a, "--out") && more)
            o.out = argv[++i];
        else if (!std::strcmp(a, "--inject") && more) {
            S_injection j;
            if (!ParseInjection(argv[++i], &j)) {
                std::printf("--inject %s: expected OP:rs:STATE:VALUE, OP:ts:STAGE:TYPE:VALUE or OP:tex:STAGE:ID\n", argv[i]);
                return 2;
            }
            o.injections.push_back(j);
        } else if (a[0] == '-' && a[1] == '-') {
            std::printf("unknown option %s\n", a);
            return 2;
        } else
            captures.push_back(a);
    }
    if (index) {
        int bad = 0;
        for (const std::string &c : captures) {
            S_index x;
            if (!IndexCapture(c.c_str(), &x)) {
                std::printf("%s: cannot open\n", c.c_str());
                ++bad;
                continue;
            }
            PrintIndex(c.c_str(), x);
            bad += !x.errors.empty();
        }
        return bad ? 2 : 0;
    }
    if (!selfTest && !probe && captures.size() != 1) {
        std::printf("usage: test_render_divergence <capture> [--reference B] [--backend B] [--inject ...] | --index <capture>... | --selftest\n");
        return 2;
    }
    // (Absolute paths first: the current directory changes below.)
    for (std::string *p : {&o.out, &d3d8Path}) {
        char full[MAX_PATH];
        if (!p->empty() && GetFullPathNameA(p->c_str(), MAX_PATH, full, nullptr))
            *p = full;
    }
    for (std::string &c : captures) {
        char full[MAX_PATH];
        if (GetFullPathNameA(c.c_str(), MAX_PATH, full, nullptr))
            c = full;
    }
    if (!d3d8Path.empty()) {
        // The game's d3d8.dll (a wrapper such as dgVoodoo's reads its settings next to it), as
        // test_render_replay loads it: the backend's "d3d8.dll" is then this one.
        const size_t slash = d3d8Path.find_last_of("\\/");
        if (slash != std::string::npos)
            SetCurrentDirectoryA(d3d8Path.substr(0, slash).c_str());
        if (!LoadLibraryA(d3d8Path.c_str())) {
            std::printf("cannot load %s\n", d3d8Path.c_str());
            return 2;
        }
        std::printf("drawing Direct3D 8 with %s\n", d3d8Path.c_str());
    }
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "LS3DFDivergence";
    RegisterClassA(&wc);
    Device *direct = g_pGfx;            // the Direct3D 8 backend (no ChooseDevice: no LS3DF.ini)
    auto make = [&](const std::string &name, S_backend *b) {
        b->name = name;
        if (name == "d3d8") {
            b->device = direct;
        } else if (name == "sdlgpu" || name == "opengl") {
            S_enhancements e = Enhancements();
            std::snprintf(e.sdlGpuDriver, sizeof e.sdlGpuDriver, "%s", driver.c_str());
            SetEnhancements(e);
            b->device = name == "sdlgpu" ? SdlGpuDevice() : OpenGlDevice();
            if (name == "sdlgpu" && !driver.empty())
                b->name += "/" + driver;
        } else {
            return false;
        }
        return b->device != nullptr;
    };
    if (selfTest) {
        S_backend d3d8;
        make("d3d8", &d3d8);
        return SelfTest(o.out.empty() ? "e01_selftest" : o.out, d3d8);
    }
    if (probe) {
        S_backend b;
        if (!make(backend, &b))
            return 2;
        return ZBiasProbe(b, o.out.empty() ? "e01_zbias_probe" : o.out);
    }
    if (!make(reference, &o.reference) || !make(backend, &o.test)) {
        std::printf("backends: d3d8, sdlgpu, opengl\n");
        return 2;
    }
    const std::string capture = captures[0];
    if (o.out.empty())
        o.out = capture + ".e01";
    const S_result r = Analyse(capture.c_str(), o);
    switch (r.verdict) {
    case NO_DIVERGENCE: return 0;
    case LOCALIZED: return 3;
    case NOT_LOCALIZED: return 4;
    default: return 2;
    }
}
