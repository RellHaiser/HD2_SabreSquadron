// Mission loader part 1: the mission's loading (game/mission_loader.cpp: C_game::LoadMission and C_MissionLoader, the
// original's 0x006caad0 and 0x006cc280..0x006d4ea0) against the original executable's. cmd_diff.cpp's world, fakes and
// call logger are included (CMD_DIFF_NO_MAIN); the loader's own calls go through ml_calls.s / ml_calls.inc
// (gen_cmd_calls.py ml). The model, sound, shadow and version model caches run on both sides (game/resource_cache.cpp
// linked; the original inlines the caches' OpenInto and Clear in the loader): the original's built by its static
// initialisers' constructors.
//
// Each case picks a mission: one of the game's (H:\HD2_Unpacked\Missions: its scene2.bin, actors.bin, sounds.bin,
// check2.bin) or one made for the case (well-formed files of every entry the loader reads, in memory), and builds the
// same world on each side: cmd_diff's frames plus frames named as the mission's objects (their types inferred from the
// objects' entries, some missing), the scene's three sectors and its camera (or none). Then, on each side:
//  - "loader": a loader (the original's built by its maps' constructors, 0x006cb5d0 / 0x006cb430), Begin, LoadScene,
//    LoadScene2, LoadActors, LoadSounds, LoadCheckpoints; the results, the loader's names (its frame map, walked in the
//    original's MSVC 6 tree), its mission name;
//  - "mission": C_game::LoadMission (the original's copy protection's two blocks jumped over); its result.
// After each the logs of every call into the game and the engine, the game's fields the loading writes, the car scene,
// the particles' scene, the shifting textures' entries and the world's frames are compared.
// MinGW; build_ml.bat; runs ml_diff.exe ("ml_diff: N checks, 0 failures"). Env: HD2_ML_CASES (default 3,000;
// HD2_ML_QUICK: 600), HD2_ML_SEED, HD2_ML_MISSION (one game mission's name), HD2_ML_SYNTH (made missions only),
// HD2_ML_SHOW, HD2_ML_DUMP (a failing case's lines).
#define HARNESS_NAME "ml_diff"
#define CMD_CALLS_INC "ml_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "game/mission_loader.h"
#include "game/particles.h"
#include "game/resource_cache.h"
#include "game/tshift.h"
#undef protected
#undef private

// The port's side of the files' writing and seeking (sc_shared.inc's in memory).
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }

// The debug texts: each side's dbgPrintf logs its text (the original's import thunk 0x007e52d8 redirected).
namespace H {
void LogDebugText(const char *format, va_list args)
{
    char text[2048];
    std::vsnprintf(text, sizeof text, format, args);
    if (*g_case)
        g_log[g_side].push_back(std::string("dbgPrintf ") + text);
}
void __cdecl O_dbgPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogDebugText(format, args);
    va_end(args);
}
// (The original's sprintf: its CRT's locks never set up.)
int __cdecl O_Sprintf(char *out, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsprintf(out, format, args);
    va_end(args);
    return n;
}
}  // namespace H
extern "C" void dbgPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    H::LogDebugText(format, args);
    va_end(args);
}

namespace H {

// ---- the original's loader and its functions ----
constexpr uint32_t O_MAP1_CTOR = 0x006cb5d0, O_MAP2_CTOR = 0x006cb430, O_MAP1_DTOR = 0x006cb340,
                   O_MAP2_DTOR = 0x006cb4e0, O_STR_RELEASE = 0x004050e0;
constexpr uint32_t O_BEGIN = 0x006d4270, O_LOAD_SCENE = 0x006d44d0, O_LOAD_SCENE2 = 0x006cc280,
                   O_LOAD_ACTORS = 0x006d4910, O_LOAD_SOUNDS = 0x006d4bd0, O_LOAD_CHECKPOINTS = 0x006d45d0,
                   O_LOAD_MISSION = 0x006caad0;
// The globals the loading writes (the original's): the car scene, the particles' scene (the module's +0x100C), the
// shifting textures' vector (the module's +0x0C: first +4, last +8, end +0xC; entries 0x1C bytes), the caches.
constexpr uint32_t O_CAR_SCENE = 0x0089c1d0, O_PARTICLES_SCENE = 0x0089aa04, O_TSHIFT_VECTOR = 0x0089bc40;
constexpr uint32_t O_MODEL_CACHE = 0x008aeda0, O_SOUND_CACHE = 0x008aed64, O_SHADOW_CACHE = 0x008aecec,
                   O_VERSION_CACHE = 0x008aecb0;
constexpr uint32_t O_CACHE_CLEAR_MODEL = 0x006d77e0, O_CACHE_CLEAR_SOUND = 0x006d87a0, O_CACHE_CLEAR_SHADOW = 0x006d6dd0;
using T_MapCtor1 = void(TC *)(void *, const void *, bool, const void *);
using T_MapCtor2 = void(TC *)(void *, const void *, const void *);
using T_RepRelease = void(TC *)(void *);
using T_Begin = void(TC *)(void *, const char *, const void *);
using T_IntOp = int(TC *)(void *);
using T_Mission = int(TC *)(void *, const char *, const void *);

struct S_OrigLoader {
    alignas(16) uint8_t bytes[0x40];
    uint32_t nil1 = 0;                                  // (the frame map's nil node: the empty tree's root)
};
void OrigLoaderMake(S_OrigLoader &l)
{
    std::memset(l.bytes, 0, sizeof l.bytes);
    uint8_t less1 = 0, alloc1 = 0, less2 = 0, alloc2 = 0;
    OriginalAt<T_MapCtor1>(O_MAP1_CTOR)(l.bytes, &less1, false, &alloc1);
    OriginalAt<T_MapCtor2>(O_MAP2_CTOR)(l.bytes + 0x10, &less2, &alloc2);
    l.nil1 = At<uint32_t>(reinterpret_cast<void *>(At<uint32_t>(l.bytes, 4)), 4);
}
void OrigLoaderFree(S_OrigLoader &l)
{
    for (uint32_t off = 0x30; off >= 0x20; off -= 4)
        if (uint32_t rep = At<uint32_t>(l.bytes, off))
            OriginalAt<T_RepRelease>(O_STR_RELEASE)(reinterpret_cast<void *>(rep));
    OriginalAt<T_Void>(O_MAP2_DTOR)(l.bytes + 0x10);
    OriginalAt<T_Void>(O_MAP1_DTOR)(l.bytes);
}
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string(); }
// The original's frame map in order (an MSVC 6 tree: left +0, parent +4, right +8, the key's C_str +0xC, the frame
// +0x10), its size (+0xC) checked against the walk.
std::vector<std::string> OrigFrames(const S_OrigLoader &l)
{
    std::vector<std::string> out;
    const uint32_t head = At<uint32_t>(l.bytes, 4);
    std::function<void(uint32_t)> walk = [&](uint32_t n) {
        if (n == l.nil1 || !n)
            return;
        walk(At<uint32_t>(reinterpret_cast<void *>(n), 0));
        out.push_back("name " + RepText(At<uint32_t>(reinterpret_cast<void *>(n), 0xC)) + " -> " +
                      NameOf(At<uint32_t>(reinterpret_cast<void *>(n), 0x10)));
        walk(At<uint32_t>(reinterpret_cast<void *>(n), 8));
    };
    walk(At<uint32_t>(reinterpret_cast<void *>(head), 4));
    out.push_back(F("names %u (walked %u)", At<uint32_t>(l.bytes, 0xC), unsigned(out.size())));
    out.push_back(F("second map %u", At<uint32_t>(l.bytes, 0x10 + 0xC)));
    for (uint32_t off = 0x20; off <= 0x30; off += 4)
        out.push_back(F("string +%02X ", off) + RepText(At<uint32_t>(l.bytes, off)));
    return out;
}
std::vector<std::string> PortFrames(const C_MissionLoader &l)
{
    std::vector<std::string> out;
    for (const auto &kv : l.m_frames)
        out.push_back("name " + std::string(kv.first.c_str()) + " -> " + NameOf(reinterpret_cast<uint32_t>(kv.second)));
    out.push_back(F("names %u (walked %u)", unsigned(l.m_frames.size()), unsigned(out.size())));
    out.push_back(F("second map %u", unsigned(l.m_unknown10.size())));
    const C_str *strings[] = {&l.m_unknown20, &l.m_unknown24, &l.m_unknown28, &l.m_unknown2C, &l.m_name};
    for (int i = 0; i < 5; ++i)
        out.push_back(F("string +%02X ", 0x20 + 4 * i) + std::string(strings[i]->c_str()));
    return out;
}

// ---- the missions: the game's, scanned for their names; or made for the case ----
// A chunk: its id and its data's range.
struct S_Chunk {
    uint16_t id;
    size_t begin, end;
};
std::vector<S_Chunk> Children(const std::string &f, size_t begin, size_t end)
{
    std::vector<S_Chunk> out;
    size_t at = begin;
    while (at + 6 <= end) {
        uint16_t id;
        uint32_t size;
        std::memcpy(&id, f.data() + at, 2);
        std::memcpy(&size, f.data() + at + 2, 4);
        if (size < 6 || at + size > end)
            break;
        out.push_back({id, at + 6, at + size});
        at += size;
    }
    return out;
}
std::string StringOf(const std::string &f, const S_Chunk &c)
{
    std::string s = f.substr(c.begin, std::min<size_t>(c.end - c.begin, 999));
    const size_t z = s.find('\0');
    return z == std::string::npos ? s : s.substr(0, z);
}
// A name and the frame type its object's entries say (0: none known), the visual type.
struct S_Named {
    std::string name;
    int type = 0;
    uint32_t visual = 0;
};
void ScanObjects(const std::string &f, size_t begin, size_t end, std::vector<S_Named> &out)
{
    for (const S_Chunk &top : Children(f, begin, end)) {
        if (top.id == 0x4C53) {                         // (actors.bin / sounds.bin / scene2.bin's object chunk)
            ScanObjects(f, top.begin, top.end, out);
            continue;
        }
        if (top.id == 0x4000) {
            for (const S_Chunk &obj : Children(f, top.begin, top.end)) {
                if (obj.id != 0x4010)
                    continue;
                S_Named n;
                for (const S_Chunk &e : Children(f, obj.begin, obj.end)) {
                    switch (e.id) {
                    case 0x0010: n.name = StringOf(f, e); break;
                    case 0x4011: if (e.end - e.begin >= 4) std::memcpy(&n.type, f.data() + e.begin, 4); break;
                    case 0x4012: if (e.end - e.begin >= 4) std::memcpy(&n.visual, f.data() + e.begin, 4); break;
                    case 0x4020:
                        for (const S_Chunk &p : Children(f, e.begin, e.end))
                            if (p.id == 0x0010)
                                out.push_back({StringOf(f, p), 0, 0});
                        break;
                    case 0x4040: if (!n.type) n.type = FRAME_LIGHT; break;
                    case 0x4060: if (!n.type) n.type = FRAME_SOUND; break;
                    case 0x4036: if (!n.type) n.type = FRAME_CAMERA; break;
                    case 0x4050: if (!n.type) n.type = FRAME_DUMMY; break;
                    case 0x2012: if (!n.type) n.type = FRAME_MODEL; break;
                    case 0x4080: if (!n.type) n.type = FRAME_OCCLUDER; break;
                    case 0xB150: if (!n.type) n.type = FRAME_AREA; break;
                    case 0xB401: if (!n.type) n.type = FRAME_SECTOR; break;
                    case 0x40F0: n.type = FRAME_VISUAL; n.visual = I3DVISUAL_PROJECTOR; break;
                    case 0xB110: n.type = FRAME_VISUAL; n.visual = I3DVISUAL_LENSFLARE; break;
                    case 0x40A0: if (!n.type) { n.type = FRAME_VISUAL; n.visual = I3DVISUAL_LMAP; } break;
                    default: break;
                    }
                }
                if (!n.name.empty())
                    out.push_back(n);
            }
        } else if (top.id == 0xAE20) {
            for (const S_Chunk &a : Children(f, top.begin, top.end))
                for (const S_Chunk &p : Children(f, a.begin, a.end))
                    if (p.id == 0xAE23)
                        out.push_back({StringOf(f, p), 0, 0});
        } else if (top.id == 0xB820) {
            for (const S_Chunk &p : Children(f, top.begin, top.end))
                out.push_back({StringOf(f, p), 0, 0});
        }
    }
}

// The chunk writer.
struct S_Writer {
    std::string out;
    std::vector<size_t> open;
    void Begin(uint16_t id)
    {
        open.push_back(out.size());
        out.append(reinterpret_cast<const char *>(&id), 2);
        out.append(4, '\0');
    }
    void End()
    {
        const size_t at = open.back();
        open.pop_back();
        const uint32_t size = uint32_t(out.size() - at);
        std::memcpy(&out[at + 2], &size, 4);
    }
    template <class T> void Put(const T &v) { out.append(reinterpret_cast<const char *>(&v), sizeof v); }
    void Text(const std::string &s, bool zero = true)
    {
        out += s;
        if (zero)
            out += '\0';
    }
    void Entry(uint16_t id, const void *data, size_t size)
    {
        Begin(id);
        out.append(static_cast<const char *>(data), size);
        End();
    }
    template <class T> void EntryOf(uint16_t id, const T &v) { Entry(id, &v, sizeof v); }
    void Name(uint16_t id, const std::string &s)
    {
        Begin(id);
        Text(s);
        End();
    }
};

// The names the made missions use (the world has some of them).
const char *const SYNTH_NAMES[] = {"obj0", "obj1", "obj2", "obj3", "obj4", "obj5", "obj6", "obj7", "obj8", "obj9",
                                   "lamp", "lamp2", "sndA", "sndB", "camX", "boxD", "mdl", "mdl2", "occl", "area1",
                                   "sec1", "sec2", "proj", "flare", "lmapv", "door", "model1", "light1", "sector1",
                                   "dummy1", "visual1", "cam1", "snd_a", "Primary sector", "Backdrop sector"};
constexpr int SYNTH_NAME_COUNT = int(sizeof SYNTH_NAMES / sizeof *SYNTH_NAMES);
std::string SynthName() { return P(0.05) ? F("fresh%d", I(0, 9999)) : SYNTH_NAMES[I(0, SYNTH_NAME_COUNT - 1)]; }
// The scene version the made file has set so far, when certain (-1: not, the entries then of version 7's size: a
// shorter read of them is harmless, a longer read past a short entry is not).
int g_synthVersion = -1;
float SynthFloat()
{
    static const float values[] = {0.0f, 1.0f, 0.5f, 2.0f, -1.0f, 10.0f, 0.05f, 0.1f, 0.2f, 100.0f, 1.3f, 0.25f};
    return P(0.6) ? values[I(0, 11)] : float(I(-4000, 4000)) * 0.125f;
}
S_vector SynthVector() { return S_vector(SynthFloat(), SynthFloat(), SynthFloat()); }

void SynthObject(S_Writer &w)
{
    w.Begin(0x4010);
    static const int types[] = {0, 0, 0, FRAME_VISUAL, FRAME_LIGHT, FRAME_CAMERA, FRAME_SOUND, FRAME_DUMMY, FRAME_USER,
                                FRAME_MODEL, FRAME_OCCLUDER, FRAME_AREA, FRAME_SECTOR, FRAME_TARGET};
    static const uint32_t visuals[] = {I3DVISUAL_OBJECT, I3DVISUAL_LMAP, I3DVISUAL_PROJECTOR, I3DVISUAL_LENSFLARE,
                                       I3DVISUAL_SINGLEMESH, I3DVISUAL_BILLBOARD};
    int type = types[I(0, 13)];
    uint32_t visual = visuals[I(0, 5)];
    // Mostly an object of a kind: its type and visual type, and its own entries more often than the others.
    std::vector<int> own;
    if (P(0.75)) {
        switch (I(0, 11)) {
        case 0: type = FRAME_LIGHT; own = {8}; break;
        case 1: type = FRAME_SOUND; own = {10}; break;
        case 2: type = FRAME_CAMERA; own = {7}; break;
        case 3: type = FRAME_DUMMY; own = {9, 22}; break;
        case 4: type = FRAME_VISUAL; visual = I3DVISUAL_PROJECTOR; own = {15}; break;
        case 5: type = FRAME_VISUAL; visual = I3DVISUAL_LENSFLARE; own = {16}; break;
        case 6: type = FRAME_OCCLUDER; own = {11}; break;
        case 7: type = FRAME_AREA; own = {12}; break;
        case 8: type = FRAME_SECTOR; own = {17}; break;
        case 9: type = FRAME_MODEL; own = {4}; break;
        case 10: type = FRAME_VISUAL; visual = P(0.5) ? I3DVISUAL_LMAP : I3DVISUAL_OBJECT; own = {13, 14}; break;
        default: type = 0; own = {5, 22}; break;
        }
    }
    // The type and visual type first, mostly (the name's entry makes the frame by them).
    if (P(0.9)) {
        w.EntryOf(0x4011, type);
        if (type == FRAME_VISUAL || P(0.1))
            w.EntryOf(0x4012, visual);
    }
    // (An object of a type takes a fresh name mostly: a name the scene has keeps the scene's frame, none made.)
    w.Name(0x0010, type && P(0.6) ? F("fresh%d", I(0, 9999)) : SynthName());
    const int entries = I(0, 9);
    for (int e = 0; e < entries; ++e) {
        const int pick = !own.empty() && P(0.6) ? own[size_t(I(0, int(own.size()) - 1))] : I(0, 26);
        switch (pick) {
        case 0: w.EntryOf(0x0020, SynthVector()); break;
        case 1: {
            const float q[4] = {SynthFloat(), SynthFloat(), SynthFloat(), SynthFloat()};
            w.Entry(0x0022, q, sizeof q);
            break;
        }
        case 2: w.EntryOf(0x002C, SynthVector()); break;
        case 3: w.EntryOf(0x002D, SynthVector()); break;
        case 4: {
            static const char *const files[] = {"", "tank.4ds", "house", "a.b.4ds", "missing.4ds"};
            w.Name(0x2012, files[I(0, 4)]);
            break;
        }
        case 5:
            w.Begin(0x4020);
            if (P(0.85))
                w.Name(0x0010, P(0.6) ? SynthName() : F("nope%d", I(0, 3)));   // (found, or not)
            else
                w.EntryOf(0x0022, 7);
            w.End();
            break;
        case 6: w.Entry(0x4033, nullptr, 0); break;
        case 7:
            // (The field of view and the far clip only: another entry is not left, and a header that does not read
            // then loops for ever, in both programs.)
            w.Begin(0x4036);
            for (int i = I(0, 3); i > 0; --i)
                w.EntryOf(P(0.5) ? 0x3010 : 0x3011, SynthFloat());
            w.End();
            break;
        case 8: {
            w.Begin(0x4040);
            for (int i = I(0, 8); i > 0; --i) {
                switch (I(0, 6)) {
                case 0: w.EntryOf(0x4041, I(0, 4)); break;
                // (The power, the colour and the range as the scene's version reads them: their specular and
                // ambient parts and the attenuation from version 7.)
                case 1: {
                    const float p[3] = {SynthFloat(), SynthFloat(), SynthFloat()};
                    w.Entry(0x4042, p, g_synthVersion < 0 || g_synthVersion >= 7 ? sizeof p : sizeof(float));
                    break;
                }
                case 2: {
                    const S_vector c[3] = {SynthVector(), SynthVector(), SynthVector()};
                    w.Entry(0x0026, c, g_synthVersion < 0 || g_synthVersion >= 7 ? sizeof c : sizeof(S_vector));
                    break;
                }
                case 3: {
                    const float c[2] = {SynthFloat(), SynthFloat()};
                    w.Entry(0x4043, c, sizeof c);
                    break;
                }
                case 4: {
                    const float r[6] = {SynthFloat(), SynthFloat(), SynthFloat(), SynthFloat(), SynthFloat(), SynthFloat()};
                    w.Entry(0x4044, r, g_synthVersion < 0 || g_synthVersion >= 7 ? sizeof r : sizeof(float) * 2);
                    break;
                }
                case 5: w.EntryOf(0x4045, I(0, 0x7F)); break;
                default: w.Name(0x4046, SynthName()); break;
                }
            }
            w.End();
            break;
        }
        case 9: {
            const float b[6] = {SynthFloat(), SynthFloat(), SynthFloat(), SynthFloat(), SynthFloat(), SynthFloat()};
            w.Entry(0x4050, b, sizeof b);
            break;
        }
        case 10: {
            w.Begin(0x4060);
            for (int i = I(0, 9); i > 0; --i) {
                switch (I(0, 10)) {
                case 0: w.EntryOf(0x4061, I(0, 3)); break;
                case 1: w.EntryOf(0x4062, SynthFloat()); break;
                case 2: w.EntryOf(0x4063, SynthFloat()); break;
                case 3: {
                    const float c[2] = {SynthFloat(), SynthFloat()};
                    w.Entry(0x4064, c, sizeof c);
                    break;
                }
                case 4: {
                    const float r[3] = {SynthFloat(), SynthFloat(), SynthFloat()};
                    w.Entry(0x4065, r, sizeof r);
                    break;
                }
                case 5: {
                    const float r[4] = {SynthFloat(), SynthFloat(), SynthFloat(), SynthFloat()};
                    w.Entry(0x4068, r, sizeof r);
                    break;
                }
                case 6: w.Entry(0x4066, nullptr, 0); break;
                case 7: w.Entry(0x4067, nullptr, 0); break;
                case 8: {
                    static const char *const files[] = {"wind", "fire.wav", "x.y.wav", "missing"};
                    w.Name(0x0010, files[I(0, 3)]);
                    break;
                }
                case 9: w.Name(P(0.5) ? 0xB200 : 0xB300, SynthName()); break;
                default: w.EntryOf(0xB800, SynthFloat()); break;
                }
            }
            w.End();
            break;
        }
        case 11:
        case 12: {
            const bool occluder = pick == 11;
            w.Begin(occluder ? 0x4080 : 0xB150);
            // (The vertices before the faces: the original's count is uninitialised before the first vertices.)
            for (int i = I(1, 2); i > 0; --i) {
                const int n = I(0, 5);
                w.Begin(occluder ? 0x4082 : 0xB170);
                w.Put(n);
                for (int k = 0; k < n; ++k)
                    w.Put(SynthVector());
                w.End();
                const int faces = I(0, 4);
                w.Begin(occluder ? 0x4083 : 0xB180);
                w.Put(faces);
                for (int k = 0; k < 3 * faces; ++k)
                    w.Put(uint16_t(I(0, 5)));
                w.End();
            }
            w.End();
            break;
        }
        case 13: {
            static const uint32_t vts[] = {I3DVISUAL_OBJECT, I3DVISUAL_LMAP, I3DVISUAL_PROJECTOR, I3DVISUAL_SINGLEMESH};
            w.EntryOf(0x4090, vts[I(0, 3)]);
            break;
        }
        case 14: w.Entry(0x40A0, "lightmaps", 9); break;
        case 15: {
            w.Begin(0x40F0);
            for (int i = I(0, 8); i > 0; --i) {
                switch (I(0, 6)) {
                case 0: w.EntryOf(0x40F1, SynthFloat()); break;
                case 1: w.EntryOf(0x40F2, SynthFloat()); break;
                case 2: w.EntryOf(0x40F3, SynthVector()); break;
                case 3: w.EntryOf(0x40F4, I(0, 3)); break;
                case 4: w.EntryOf(0x40F5, I(0, 3)); break;
                case 5: w.EntryOf(0x40F6, uint8_t(I(0, 1))); break;
                default: w.Name(0x40F7, P(0.8) ? "decal.bmp" : "missing.bmp"); break;
                }
            }
            w.End();
            break;
        }
        case 16:
            // (Only on a lens flare: on another frame the original calls past its vtable's known slots.)
            if (type == FRAME_VISUAL && visual == I3DVISUAL_LENSFLARE) {
                w.Begin(0xB110);
                for (int i = I(0, 7); i > 0; --i) {
                    if (P(0.15))
                        w.Entry(P(0.5) ? 0xB111 : 0xB118, nullptr, 0);   // (another id, empty: not left, nothing in it)
                    else
                        w.EntryOf(uint16_t(0xB112 + I(0, 5)), SynthFloat());
                }
                w.End();
            }
            break;
        case 17: {
            w.Begin(0xB401);
            for (int i = 0; i < 20; ++i)
                w.Put(I(-5, 100));
            for (int i = 0; i < 2; ++i)
                w.Put(SynthVector());
            w.End();
            break;
        }
        case 18: w.EntryOf(0xB400, 5); break;
        case 19: w.EntryOf(0x7777, 3); break;           // (an unknown entry: skipped)
        case 20: w.EntryOf(0x4011, types[I(0, 13)]); break;   // (the type after the name: a later name's frame)
        case 21: w.Name(0x0010, SynthName()); break;
        case 22:
            // Two parent entries: the first found, the second not (the parent then null, its flag kept).
            for (const std::string &n : {std::string(SYNTH_NAMES[I(0, SYNTH_NAME_COUNT - 1)]), F("nope%d", I(0, 3))}) {
                w.Begin(0x4020);
                w.Name(0x0010, n);
                w.End();
            }
            break;
        default: w.EntryOf(0x0020, SynthVector()); break;
        }
    }
    w.End();
}

// A made object chunk (scene2.bin's, actors.bin's or sounds.bin's): its entries in a random order.
std::string SynthObjects(int kind)
{
    S_Writer w;
    w.Begin(0x4C53);
    g_synthVersion = -1;
    // (The scene's version first, mostly: lights read by it, 5 to 7 at the bounds.)
    if (kind == 0 && P(0.7)) {
        const int16_t v[2] = {int16_t(P(0.5) ? I(4, 7) : I(1, 9)), int16_t(I(0, 3))};
        w.Entry(0x0001, v, sizeof v);
        g_synthVersion = v[0];
    }
    // (Mostly a list of objects first: the lights, sounds and the rest read under that version.)
    if (kind == 0 && P(0.8)) {
        w.Begin(0x4000);
        for (int k = I(2, 10); k > 0; --k)
            SynthObject(w);
        w.End();
    }
    const int count = kind == 0 ? I(0, 14) : I(0, 4);
    for (int i = 0; i < count; ++i) {
        switch (kind == 0 ? I(0, 22) : I(0, 3) == 0 ? 0 : 7) {
        case 0: {
            w.Begin(0x4000);
            for (int k = I(0, 6); k > 0; --k) {
                if (P(0.9))
                    SynthObject(w);
                else
                    w.EntryOf(0x4011, 1);               // (not an object: skipped)
            }
            w.End();
            break;
        }
        case 1: {
            const int16_t v[2] = {int16_t(I(1, 9)), int16_t(I(0, 3))};
            w.Entry(0x0001, v, sizeof v);
            g_synthVersion = v[0];
            break;
        }
        case 2: w.EntryOf(0x3010, SynthFloat()); break;
        case 3: w.EntryOf(0x3011, SynthFloat()); break;
        case 4: w.Entry(0x3012, nullptr, 0); break;
        case 5: w.EntryOf(0x3013, SynthFloat()); break;
        case 6: w.EntryOf(0x3200, SynthVector()); break;
        case 7: {
            w.Begin(0xAE20);
            for (int k = I(1, 5); k > 0; --k) {
                w.Begin(P(0.9) ? 0xAE21 : 0xAE2F);
                // (The frame's name, the type, the properties; a part of another kind is not left: last.)
                if (P(0.9))
                    w.Name(0xAE23, SynthName());
                if (P(0.9))
                    w.EntryOf(0xAE22, I(0, 40));
                if (P(0.7)) {
                    w.Begin(0xAE24);
                    if (P(0.7))
                        w.EntryOf(0x0001, I(0, 9));
                    w.End();
                }
                // (No part of another kind: it is not left, the next parts are read inside it, and a file of them
                // overflows the original's chunk stack.)
                w.End();
            }
            w.End();
            break;
        }
        case 8: {
            const float v[2] = {SynthFloat(), SynthFloat()};
            w.Entry(0x3211, v, sizeof v);
            break;
        }
        case 9: {
            w.Begin(0xAE00);
            for (int k = I(0, 4); k > 0; --k) {
                w.Begin(P(0.9) ? 0xAE01 : 0xAE09);
                const std::string name = SynthName();
                w.Put(I(0, 9));
                w.Put(I(0, 9));
                w.Put(int(name.size()));
                w.Text(name, false);
                w.End();
            }
            w.End();
            break;
        }
        case 10: w.Entry(0xAE40, "checkpoints", 11); break;
        case 11: w.EntryOf(0xB004, uint8_t(I(0, 1))); break;
        case 12: w.EntryOf(0xB600, I(0, 3)); break;
        case 13: w.EntryOf(0xB650, I(-1, 5)); break;
        case 14: {
            uint32_t raw[11];
            for (uint32_t &r : raw) {
                const float f = SynthFloat();
                std::memcpy(&r, &f, 4);
            }
            w.Entry(0xB700, raw, sizeof raw);
            break;
        }
        case 15: w.Entry(0xB810, "groups", 6); break;
        case 16: {
            w.Begin(0xB820);
            for (int k = I(0, 4); k > 0; --k)
                w.Name(P(0.9) ? 0xB820 : 0xB821, SynthName());
            w.End();
            break;
        }
        case 17: w.EntryOf(0xB840, I(-1, 20)); break;
        case 18: w.EntryOf(0xB880, I(-1, 20)); break;
        case 19: w.EntryOf(0x5555, 2); break;
        default: {
            w.Begin(0x4000);
            SynthObject(w);
            w.End();
            break;
        }
        }
    }
    // Now and then a last entry cut short: a header of fewer than 6 bytes left at the file's end, inside a list
    // (the tail's chunks all end with the file).
    if (kind == 0 && P(0.12)) {
        const std::string stray(size_t(I(1, 5)), '\x07');
        switch (I(0, 9)) {
        case 6: w.Begin(0x4000); break;                 // the objects' list
        case 7:                                         // a light's entries
        case 8:                                         // a sound's entries
        case 9: {                                       // an area's entries
            const int which = I(0, 2);
            w.Begin(0x4000);
            w.Begin(0x4010);
            w.EntryOf(0x4011, int(which == 0 ? FRAME_LIGHT : which == 1 ? FRAME_SOUND : FRAME_AREA));
            w.Name(0x0010, F("tail%d", I(0, 99)));
            w.Begin(uint16_t(which == 0 ? 0x4040 : which == 1 ? 0x4060 : 0xB150));
            break;
        }
        case 0:                                         // an object's parent entry
            w.Begin(0x4000);
            w.Begin(0x4010);
            w.EntryOf(0x4011, int(FRAME_DUMMY));
            w.Name(0x0010, F("tail%d", I(0, 99)));
            w.Begin(0x4020);
            break;
        case 1: w.Begin(0xAE00); break;                 // the shifting textures' list
        case 2: w.Begin(0xAE20); break;                 // the actors' list
        case 3:                                         // an actor's parts
            w.Begin(0xAE20);
            w.Begin(0xAE21);
            w.Name(0xAE23, SynthName());
            break;
        case 4:                                         // an occluder's entries
            w.Begin(0x4000);
            w.Begin(0x4010);
            w.EntryOf(0x4011, int(FRAME_OCCLUDER));
            w.Name(0x0010, F("tail%d", I(0, 99)));
            w.Begin(0x4080);
            break;
        default:                                        // a projector's entries
            w.Begin(0x4000);
            w.Begin(0x4010);
            w.EntryOf(0x4011, int(FRAME_VISUAL));
            w.EntryOf(0x4012, I3DVISUAL_PROJECTOR);
            w.Name(0x0010, F("tail%d", I(0, 99)));
            w.Begin(0x40F0);
            break;
        }
        w.out += stray;
        while (!w.open.empty())
            w.End();
        return w.out;
    }
    w.End();
    // (Another chunk after the object chunk now and then: not read.)
    if (P(0.1))
        w.EntryOf(0x1234, 1);
    return w.out;
}

// ---- the case's mission ----
struct S_Mission {
    std::string name;
    bool game = false;                                  // one of the game's
    std::vector<S_Named> names;                         // (the world's frames by these)
    int objects = 0;                                    // (frames to reserve)
};
std::vector<std::string> g_gameMissions;
std::vector<std::string> g_percentMissions;             // (left out: a name with a '%')
std::map<std::string, S_Mission> g_scanned;
S_Mission GameMission(const std::string &name)
{
    auto it = g_scanned.find(name);
    if (it != g_scanned.end())
        return it->second;
    S_Mission m;
    m.name = name;
    m.game = true;
    for (const char *file : {"scene2.bin", "actors.bin", "sounds.bin"}) {
        std::string text;
        if (FileText("missions\\" + name + "\\" + file, text)) {
            ScanObjects(text, 0, text.size(), m.names);
            m.objects += int(text.size() / 40);
        }
    }
    g_scanned[name] = m;
    return m;
}
int g_synthCount;
S_Mission SynthMission()
{
    S_Mission m;
    m.name = F("SYNTH%d", g_synthCount++ % 4);
    const std::string dir = "missions\\" + m.name + "\\";
    for (const char *f : {"scene2.bin", "actors.bin", "sounds.bin", "check2.bin", "check.bin"}) {
        g_overrides.erase(Lower(dir + f));
        g_cache[Lower(dir + f)] = std::string("\x01missing", 8);
    }
    if (P(0.9))
        g_overrides[Lower(dir + "scene2.bin")] = P(0.03) ? std::string() : SynthObjects(0);   // (empty: no chunk)
    if (P(0.7))
        g_overrides[Lower(dir + "actors.bin")] = P(0.9) ? SynthObjects(1) : std::string("\x10\x00\x06\x00\x00\x00", 6);
    if (P(0.7))
        g_overrides[Lower(dir + "sounds.bin")] = P(0.9) ? SynthObjects(2) : std::string("\x10\x00\x06\x00\x00\x00", 6);
    if (P(0.6))
        g_overrides[Lower(dir + "check2.bin")] = std::string("\x40\xAE\x06\x00\x00\x00", 6);
    else if (P(0.5))
        g_overrides[Lower(dir + "check.bin")] = std::string("old", 3);
    for (const char *f : {"scene2.bin", "actors.bin", "sounds.bin"}) {
        std::string text;
        if (FileText(dir + f, text))
            ScanObjects(text, 0, text.size(), m.names);
    }
    for (int i = 0; i < SYNTH_NAME_COUNT; ++i)
        m.names.push_back({SYNTH_NAMES[i], 0, 0});
    m.objects = 600;
    return m;
}

// ---- the world: cmd_diff's, the mission's names, the scene ----
// The frames' classes (for their vtables): by type, a visual's by its visual type.
const char *ClassOf(int type, uint32_t visual)
{
    switch (type) {
    case FRAME_VISUAL:
        switch (visual) {
        case I3DVISUAL_LMAP: return "I3D_lmap_visual";
        case I3DVISUAL_PROJECTOR: return "I3D_projector_visual";
        case I3DVISUAL_LENSFLARE: return "I3D_lensflare_visual";
        default: return "I3D_object_visual";
        }
    case FRAME_OCCLUDER: return "I3D_occluder";
    case FRAME_AREA: return "I3D_area";
    case FRAME_SHADOW: return "I3D_shadow";
    default: return FrameClass(type);
    }
}
// The world's frames by address (indexed as they come: the game's big scenes make thousands).
std::map<uint32_t, size_t> g_frameIndex[2];
size_t g_indexed[2];
S_FakeFrame *FakeOf(uint32_t p)
{
    std::vector<S_FakeFrame> &frames = W().frames;
    for (size_t &i = g_indexed[g_side]; i < frames.size(); ++i)
        g_frameIndex[g_side][reinterpret_cast<uint32_t>(frames[i].bytes)] = i;
    auto it = g_frameIndex[g_side].find(p);
    return it != g_frameIndex[g_side].end() ? &frames[it->second] : nullptr;
}
// A new frame of the world (its class's vtable; a light's sectors' array, +0x120 / +0x128).
uint8_t *MakeFrame(int type, uint32_t visual, const std::string &name)
{
    uint8_t *b = NewFrame(type, name);
    S_FakeFrame &f = W().frames.back();
    if (type == FRAME_VISUAL)
        f.visualType = visual;
    *reinterpret_cast<void ***>(b) = EngineVtable(ClassOf(type, f.visualType));
    AsFrame(b)->m_name = f.name.c_str();
    // (A lens flare's and a projector's +0x1E0 is a field of theirs, not the mesh cmd_diff gives a visual.)
    if (type == FRAME_VISUAL && (f.visualType == I3DVISUAL_LENSFLARE || f.visualType == I3DVISUAL_PROJECTOR))
        *reinterpret_cast<uint32_t *>(b + 0x1E0) = 0;
    if (type == FRAME_LIGHT) {
        *reinterpret_cast<uint8_t **>(b + 0x120) = Block(64 * 4);
        // (Its attenuation, as SetAttenuation left it: GetOwnAttenuation reads it inline.)
        static const float att[] = {0.0f, 0.0f, 0.5f, 1.0f, 2.0f};
        for (int i = 0; i < 3; ++i)
            *reinterpret_cast<float *>(b + 0x17C + 4 * i) = att[WI(0, 4)];
    }
    return b;
}
void Unlink(I3D_frame *c)
{
    if (!c->m_parent)
        return;
    I3D_frame **link = &c->m_parent->m_firstChild;
    while (*link && *link != c)
        link = &(*link)->m_nextSibling;
    if (*link)
        *link = c->m_nextSibling;
    c->m_parent = nullptr;
    c->m_nextSibling = nullptr;
}
std::set<uint32_t> g_inScene[2];
uint8_t *g_igraphOf[2];

int g_missionFrames;                                    // (the world's frames past cmd_diff's)
void BuildMissionWorld(int side, uint32_t seed, const S_Mission &m, int sceneMode)
{
    g_side = side;
    FreeWorld(side);
    g_world[side].frames.reserve(size_t(m.objects) * 3 + m.names.size() + 4096);
    BuildWorld(side, seed);
    S_World &w = W();
    // (cmd_diff's visual frames draw their visual types: each its class's vtable, a lightmapped one's own slots.)
    for (S_FakeFrame &f : w.frames)
        if (AsFrame(f.bytes)->m_type == FRAME_VISUAL)
            *reinterpret_cast<void ***>(f.bytes) = EngineVtable(ClassOf(FRAME_VISUAL, f.visualType));
    // (The world's own draws: the same on both sides.)
    for (const S_Named &n : m.names) {
        // (Not the cut-short tails' objects: each must make its own frame for its entries.)
        if (n.name.empty() || n.name.find('%') != std::string::npos || n.name.compare(0, 4, "tail") == 0 || WI(0, 9) < 3)
            continue;
        int type = n.type;
        uint32_t visual = n.visual;
        if (!type) {
            static const int types[] = {FRAME_VISUAL, FRAME_DUMMY, FRAME_MODEL, FRAME_SECTOR, FRAME_LIGHT, FRAME_SOUND,
                                        FRAME_CAMERA, FRAME_JOINT, FRAME_TARGET, FRAME_OCCLUDER, FRAME_AREA};
            type = types[WI(0, 10)];
        } else if (n.visual != I3DVISUAL_LENSFLARE && WI(0, 9) == 0) {
            type = WI(0, 1) ? FRAME_DUMMY : FRAME_VISUAL;   // (another type: the entries for it not applied)
        }
        if (type == FRAME_VISUAL && !visual) {
            static const uint32_t visuals[] = {I3DVISUAL_OBJECT, I3DVISUAL_LMAP, I3DVISUAL_SINGLEMESH};
            visual = visuals[WI(0, 2)];
        }
        MakeFrame(type, visual, n.name);
    }
    // The scene's sectors (named as the game's) and its camera (or none: LoadScene2 makes one).
    uint8_t *backdrop = MakeFrame(FRAME_SECTOR, 0, "Backdrop sector");
    uint8_t *omni = MakeFrame(FRAME_SECTOR, 0, "Omni sector");
    // The mission's frames: some under others (a visual's children, frames under models), the lights in sectors
    // already (a light's object takes it out of them first).
    std::vector<uint8_t *> sectors = {backdrop, omni};
    for (S_FakeFrame &f : w.frames)
        if (AsFrame(f.bytes)->m_type == FRAME_SECTOR)
            sectors.push_back(f.bytes);
    const size_t first = size_t(WORLD_FRAMES);
    for (size_t i = first; i + 2 < w.frames.size(); ++i) {
        I3D_frame *f = AsFrame(w.frames[i].bytes);
        if (f->m_type == FRAME_LIGHT) {
            uint8_t *l = w.frames[i].bytes;
            *reinterpret_cast<uint8_t **>(l + 0x120) = Block(64 * 4);
            for (int k = WI(0, 2); k > 0; --k) {
                uint32_t *arr = *reinterpret_cast<uint32_t **>(l + 0x120);
                arr[(*reinterpret_cast<int *>(l + 0x128))++] = reinterpret_cast<uint32_t>(sectors[size_t(WI(0, int(sectors.size()) - 1))]);
            }
        }
        if (WI(0, 4) == 0) {
            const size_t p = first + size_t(WI(0, int(w.frames.size() - first) - 1));
            I3D_frame *parent = AsFrame(w.frames[p].bytes);
            bool cycle = false;
            for (I3D_frame *a = parent; a; a = a->m_parent)
                cycle = cycle || a == f;
            if (!cycle && !f->m_parent) {
                f->m_parent = parent;
                f->m_nextSibling = parent->m_firstChild;
                parent->m_firstChild = f;
            }
        }
    }
    *reinterpret_cast<uint8_t **>(w.scene + 0x1FC) = backdrop;
    *reinterpret_cast<uint8_t **>(w.scene + 0x200) = omni;
    if (sceneMode == 1)
        *reinterpret_cast<uint8_t **>(w.scene + 0x15C) = nullptr;
    g_missionFrames = int(w.frames.size());
    g_frameIndex[side].clear();
    g_indexed[side] = 0;
    g_inScene[side].clear();
    for (const S_FakeFrame &f : w.frames)
        g_inScene[side].insert(reinterpret_cast<uint32_t>(f.bytes));
    // The IGraph (LoadTexture's).
    static uint8_t *igraph[2];
    igraph[side] = Block(0x100);
    *reinterpret_cast<void ***>(igraph[side]) = EngineVtable("IGraph");
    Name(igraph[side], "igraph");
    g_igraphOf[side] = igraph[side];
}

// ---- the answers the loading needs (on top of cmd_diff's) ----
uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
void SetUpMissionAnswers()
{
    // The actors the loader makes: their SetFrame and ReadProperties through each side's vtable.
    g_virtuals.push_back({"C_Actor::SetFrame", 0x2C, PortSlot(&C_Actor::SetFrame), "p", 'b'});
    g_virtuals.push_back({"C_Actor::ReadProperties", 0x34, PortSlot(&C_Actor::ReadProperties), "p", 'b'});
    // The shadows' parameters (scene2.bin's 0xB700): their 13 words logged, but the colour's w (+0x0C) and the fourth
    // unread word (+0x1C), which the file does not set: the original's stack there, the port's 0.
    g_virtuals.push_back({"C_GEffectsModule::SetShadowParams", 0x80, PortSlot(&C_GEffectsModule::SetShadowParams), "p", 'v'});
    g_extraQuiet.insert("C_GEffectsModule::SetShadowParams");
    g_answers["C_GEffectsModule::SetShadowParams"] = [](const std::vector<uint32_t> &a) {
        std::string s = "C_GEffectsModule::SetShadowParams {";
        for (int i = 0; i < 13; ++i)
            s += i == 3 || i == 7 ? std::string(" -")
                                  : F(i ? " %08x" : "%08x", At<uint32_t>(reinterpret_cast<void *>(a[1]), uint32_t(4 * i)));
        g_log[g_side].push_back(s + "}");
        return 0u;
    };
    g_answers["C_ActorsModule::CreateActor"] = [](const std::vector<uint32_t> &) {
        if (NextFake() % 5 == 0)
            return 0u;
        static void **vt[2];
        if (!vt[g_side])
            vt[g_side] = GameVtable("C_Actor", VirtualSlots("C_Actor::", g_side != 0));
        uint8_t *actor = Block(0x400);
        *reinterpret_cast<void ***>(actor) = vt[g_side];
        Name(actor, F("actor%d", W().created++));
        return Ptr(actor);
    };
    // The frames the driver makes (each type's class; a visual's by its visual type, now and then none).
    g_answers["I3D_driver::CreateFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(MakeFrame(int(a[1]), I3DVISUAL_OBJECT, F("new%d", W().created++)));
    };
    g_answers["I3D_driver::CreateVisual"] = [](const std::vector<uint32_t> &a) {
        if (NextFake() % 8 == 0)
            return 0u;
        return Ptr(MakeFrame(FRAME_VISUAL, a[1], F("new%d", W().created++)));
    };
    // The scene's file: its progress reported through the loader's callback (messages 0x10 and 0x20 among them).
    g_answers["I3D_scene::Open"] = [](const std::vector<uint32_t> &a) {
        using T_Progress = void(__stdcall *)(int, float, uint32_t, void *);
        if (const T_Progress progress = reinterpret_cast<T_Progress>(a[3])) {
            static const int messages[] = {0x10, 0x01, 0x20, 0x02, 0x00};
            static const float fractions[] = {0.0f, 0.25f, 0.5f, 1.0f, 0.75f};
            for (int i = 0, n = int(NextFake() % 6); i < n; ++i)
                progress(messages[i % 5], fractions[(i + n) % 5], 7u, reinterpret_cast<void *>(a[4]));
        }
        return uint32_t(FakeInt());
    };
    g_answers["I3D_frame::GetVisualType"] = [](const std::vector<uint32_t> &a) {
        const S_FakeFrame *f = FakeOf(a[0]);
        return f ? f->visualType : 0u;
    };
    g_answers["I3D_frame::SetName"] = [](const std::vector<uint32_t> &a) {
        if (S_FakeFrame *f = FakeOf(a[0])) {
            f->name = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
            AsFrame(f->bytes)->m_name = f->name.c_str();
        }
        return 0u;
    };
    g_answers["I3D_frame::LinkTo"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *c = reinterpret_cast<I3D_frame *>(a[0]);
        Unlink(c);
        if (I3D_frame *p = reinterpret_cast<I3D_frame *>(a[1])) {
            c->m_parent = p;
            c->m_nextSibling = p->m_firstChild;
            p->m_firstChild = c;
        }
        return 0u;
    };
    g_answers["I3D_frame::AddRef"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(++reinterpret_cast<I3D_frame *>(a[0])->m_refCount);
    };
    g_answers["I3D_scene::AddFrame"] = [](const std::vector<uint32_t> &a) {
        g_inScene[g_side].insert(a[1]);
        return 0u;
    };
    g_answers["I3D_scene::RemoveFrame"] = [](const std::vector<uint32_t> &a) {
        if (a[1])
            Unlink(reinterpret_cast<I3D_frame *>(a[1]));
        return uint32_t(g_inScene[g_side].erase(a[1]));
    };
    g_answers["I3D_scene::SetActiveCamera"] = [](const std::vector<uint32_t> &a) {
        *reinterpret_cast<uint32_t *>(a[0] + 0x15C) = a[1];
        return 0u;
    };
    // EnumFrames: the callback on each frame of the scene (or a frame's descendants, depth first) in the type mask,
    // listed first. (Logged here without the callback: each side's own function.)
    using T_Enum = uint32_t(__stdcall *)(I3D_frame *, void *);
    auto enumerate = [](const std::vector<uint32_t> &a, const char *what, const std::vector<I3D_frame *> &found) {
        g_log[g_side].push_back(F("%s(%s, types %08x, mask %s)", what, NameOf(a[0]).c_str(), a[3], ArgText('s', a[4]).c_str()));
        const T_Enum callback = reinterpret_cast<T_Enum>(a[1]);
        for (I3D_frame *f : found)
            if (callback(f, reinterpret_cast<void *>(a[2])))
                break;
        return 0u;
    };
    g_extraQuiet.insert("I3D_scene::EnumFrames");
    g_answers["I3D_scene::EnumFrames"] = [enumerate](const std::vector<uint32_t> &a) {
        std::vector<I3D_frame *> found;
        for (const S_FakeFrame &f : W().frames)
            if (g_inScene[g_side].count(Ptr(f.bytes)) && AsFrame(f.bytes)->m_type && (a[3] & (1u << (AsFrame(f.bytes)->m_type - 1))))
                found.push_back(AsFrame(f.bytes));
        return enumerate(a, "I3D_scene::EnumFrames", found);
    };
    g_extraQuiet.insert("I3D_frame::EnumFrames");
    g_answers["I3D_frame::EnumFrames"] = [enumerate](const std::vector<uint32_t> &a) {
        std::vector<I3D_frame *> found;
        std::function<void(I3D_frame *)> walk = [&](I3D_frame *f) {
            for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling) {
                if (c->m_type && (a[3] & (1u << (c->m_type - 1))))
                    found.push_back(c);
                walk(c);
            }
        };
        walk(reinterpret_cast<I3D_frame *>(a[0]));
        return enumerate(a, "I3D_frame::EnumFrames", found);
    };
    // A light's sectors (its +0x120 array, +0x128 count: NumSectors / GetSector are inline).
    auto lightSectors = [](uint32_t light) {
        uint8_t *l = reinterpret_cast<uint8_t *>(light);
        if (!*reinterpret_cast<uint8_t **>(l + 0x120))
            *reinterpret_cast<uint8_t **>(l + 0x120) = Block(64 * 4);
        return std::make_pair(*reinterpret_cast<uint32_t **>(l + 0x120), reinterpret_cast<int *>(l + 0x128));
    };
    g_answers["I3D_sector::AddLight"] = [lightSectors](const std::vector<uint32_t> &a) {
        auto s = lightSectors(a[1]);
        if (*s.second < 64)
            s.first[(*s.second)++] = a[0];
        return 0u;
    };
    g_answers["I3D_sector::RemoveLight"] = [lightSectors](const std::vector<uint32_t> &a) {
        auto s = lightSectors(a[1]);
        for (int i = 0; i < *s.second; ++i)
            if (s.first[i] == a[0]) {
                std::memmove(s.first + i, s.first + i + 1, size_t(*s.second - i - 1) * 4);
                --*s.second;
                break;
            }
        return 0u;
    };
    // A light's attenuation kept (+0x17C: GetOwnAttenuation reads it).
    g_answers["I3D_light::SetAttenuation"] = [](const std::vector<uint32_t> &a) {
        for (int i = 0; i < 3; ++i)
            *reinterpret_cast<uint32_t *>(a[0] + 0x17C + 4 * uint32_t(i)) = a[1 + size_t(i)];
        return 0u;
    };
    // A model's animation (AddAnimatedModel), now and then.
    g_answers["I3D_model::GetAnimation"] = [](const std::vector<uint32_t> &) {
        if (NextFake() % 3)
            return 0u;
        uint8_t *anim = Block(0x40);
        *reinterpret_cast<void ***>(anim) = EngineVtable("I3D_animation_set");
        Name(anim, "anim");
        return Ptr(anim);
    };
    // The screen and its textures (the projectors'): an image, or none.
    g_answers["GetIGraph"] = [](const std::vector<uint32_t> &) { return Ptr(g_igraphOf[g_side]); };
    g_extraQuiet.insert("GetIGraph");
    g_answers["IGraph::LoadTexture"] = [](const std::vector<uint32_t> &a) {
        const uint32_t r = NextFake() % 4;
        if (r == 0)
            return 1u;
        uint8_t *image = nullptr;
        if (r != 3) {
            image = Block(0x40);
            *reinterpret_cast<void ***>(image) = EngineVtable("IImage");
            Name(image, F("image%d", W().created++));
        }
        if (a[4])
            *reinterpret_cast<uint8_t **>(a[4]) = image;
        return 0u;
    };
    // The caches' files: the prefilled ones always open (else the sequence's answer).
    for (const char *key : {"I3D_model::Open", "I3D_shadow::Open"})
        g_answers[key] = [](const std::vector<uint32_t> &a) {
            const char *file = reinterpret_cast<const char *>(a[1]);
            return file && std::strstr(file, "prefill") ? 0u : uint32_t(FakeInt());
        };
    // A sector's sound environment: parameters 20 and 21 pass a vector by its address (its three floats logged).
    g_extraQuiet.insert("I3D_sector::SetEnvironmentParam");
    g_answers["I3D_sector::SetEnvironmentParam"] = [](const std::vector<uint32_t> &a) {
        const int param = int(a[1]);
        const std::string value = param == 20 || param == 21 ? VectorText(a[2], 3) : F("%d", int(a[2]));
        const uint32_t answer = uint32_t(FakeInt());
        g_log[g_side].push_back(F("I3D_sector::SetEnvironmentParam(%s, %d, %s) -> %d", NameOf(a[0]).c_str(), param,
                                  value.c_str(), int(answer)));
        return answer;
    };
    // An occluder's or an area's mesh: the vertices (their x, y, z at the stride) and the triangles, by their
    // contents (no faces: the pointers are not read).
    for (const char *key : {"I3D_occluder::Build", "I3D_area::Build"}) {
        g_extraQuiet.insert(key);
        const std::string name = key;
        g_answers[key] = [name](const std::vector<uint32_t> &a) {
            std::string s = name + "(" + NameOf(a[0]) + F(", %d vertices", int(a[2]));
            for (int i = 0; i < int(a[2]) && a[1]; ++i)
                s += " " + VectorText(a[1] + uint32_t(i) * a[5], 3);
            s += F(", %d triangles", int(a[4]));
            for (int i = 0; i < 3 * int(a[4]) && a[3]; ++i)
                s += F(" %d", int(reinterpret_cast<const uint16_t *>(a[3])[i]));
            const uint32_t answer = uint32_t(FakeInt());
            g_log[g_side].push_back(s + F(", stride %d) -> %d", int(a[5]), int(answer)));
            return answer;
        };
    }
    // The projector's orthogonal flag: the original passes the byte with its stack's three bytes above it (the low
    // byte logged).
    g_extraQuiet.insert("I3D_projector_visual::SetOrthogonal");
    g_answers["I3D_projector_visual::SetOrthogonal"] = [](const std::vector<uint32_t> &a) {
        g_log[g_side].push_back(F("I3D_projector_visual::SetOrthogonal(%s, %d)", NameOf(a[0]).c_str(), int(a[1] & 0xFF)));
        return 0u;
    };
}

// ---- the caches (both sides' real; the original's constructed here, their static initialisers never ran) ----
using T_AddDir = void(TC *)(void *, const char *);
int __cdecl O_Atexit(void *) { return 0; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align) { return _aligned_malloc(size, align); }
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
void SetUpCaches()
{
    // (Their maps' std::_Lockit: its critical section (0x0098bca8) set up here, its state (0x0098bcc0) 2 = ready.)
    if (*OriginalAt<uint32_t *>(0x0098bcc0) != 2) {
        InitializeCriticalSection(OriginalAt<CRITICAL_SECTION *>(0x0098bca8));
        *OriginalAt<uint32_t *>(0x0098bcc0) = 2;
    }
    for (uint32_t ctor : {0x006d5030u, 0x006d5370u, 0x006d59c0u, 0x006d5d60u})
        OriginalAt<void (*)()>(ctor)();
    // (The entries' limit (+0x34) as the port's, 256; the entries' vector (+0x14: first +0x18, last +0x1C, end +0x20)
    // with room for 256: grown, it copies its entries with an inline AddRef, a Release the port's does not make.)
    for (uint32_t cache : {O_MODEL_CACHE, O_SOUND_CACHE}) {
        *OriginalAt<uint32_t *>(cache + 0x34) = 256;
        uint8_t *buf = static_cast<uint8_t *>(O_New(8 * 256));
        *OriginalAt<uint8_t **>(cache + 0x18) = buf;
        *OriginalAt<uint8_t **>(cache + 0x1C) = buf;
        *OriginalAt<uint8_t **>(cache + 0x20) = buf + 8 * 256;
    }
    reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(O_MODEL_CACHE))[0x18 / 4])(OriginalAt<void *>(O_MODEL_CACHE), "models\\");
    reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(O_SOUND_CACHE))[0x18 / 4])(OriginalAt<void *>(O_SOUND_CACHE), "sounds\\");
    g_modelCache.AddDir("models\\");
    g_soundCache.AddDir("sounds\\");
    // (The shadows' and the versions' caches: a directory each, their entries the prefill's.)
    reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(O_SHADOW_CACHE))[0x18 / 4])(OriginalAt<void *>(O_SHADOW_CACHE), "shadows\\");
    reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(O_VERSION_CACHE))[0x18 / 4])(OriginalAt<void *>(O_VERSION_CACHE), "versions\\");
    g_shadowCache.AddDir("shadows\\");
    g_versionModelCache.AddDir("versions\\");
}
void ClearCaches()
{
    if (g_side) {
        OriginalAt<T_Void>(O_CACHE_CLEAR_MODEL)(OriginalAt<void *>(O_MODEL_CACHE));
        OriginalAt<T_Void>(O_CACHE_CLEAR_SOUND)(OriginalAt<void *>(O_SOUND_CACHE));
    } else {
        g_modelCache.Clear();
        g_soundCache.Clear();
    }
}

// ---- the compared state past the log ----
std::vector<std::string> MissionState()
{
    std::vector<std::string> out;
    if (g_side) {
        const uint8_t *g = g_origGame;
        out.push_back(F("game version %d %d d8 %d fov %s clip %s %s music %d %d loaded %d name ", int(At<int16_t>(g, 0x66)),
                        int(At<int16_t>(g, 0x68)), At<int>(g, 0xD8), FloatText(At<float>(g, 0xBC)).c_str(),
                        FloatText(At<float>(g, 0xC0)).c_str(), FloatText(At<float>(g, 0xC4)).c_str(), At<int>(g, 0xC8),
                        At<int>(g, 0xCC), int(g[0x14])) +
                      RepText(At<uint32_t>(g, 0x6A)));
        out.push_back("car scene " + NameOf(*OriginalAt<uint32_t *>(O_CAR_SCENE)) + " particles scene " +
                      NameOf(*OriginalAt<uint32_t *>(O_PARTICLES_SCENE)));
        const uint8_t *first = *OriginalAt<uint8_t **>(O_TSHIFT_VECTOR + 4);
        const uint8_t *last = *OriginalAt<uint8_t **>(O_TSHIFT_VECTOR + 8);
        for (const uint8_t *e = first; e && e < last; e += 0x1C)
            out.push_back(F("tshift %s %s %d %d %d %d", NameOf(At<uint32_t>(e, 0)).c_str(), NameOf(At<uint32_t>(e, 4)).c_str(),
                            int(e[8]), int(e[9]), At<int>(e, 0xC), At<int>(e, 0x10)));
    } else {
        const C_game *g = reinterpret_cast<const C_game *>(g_portGameBuf);
        out.push_back(F("game version %d %d d8 %d fov %s clip %s %s music %d %d loaded %d name ", int(g->m_unknown66),
                        int(g->m_unknown68), g->m_unknownD8, FloatText(g->m_fov).c_str(), FloatText(g->m_nearClip).c_str(),
                        FloatText(g->m_farClip).c_str(), g->m_music, g->m_alarmMusic, int(g->m_missionLoaded)) +
                      std::string(g->m_missionName.c_str()));
        out.push_back("car scene " + NameOf(Ptr(g_carScene)) + " particles scene " + NameOf(Ptr(g_particlesModule.m_scene)));
        for (const S_TShiftEntry &e : g_tshiftModule.m_entries)
            out.push_back(F("tshift %s %s %d %d %d %d", NameOf(Ptr(e.mesh)).c_str(), NameOf(Ptr(e.frame)).c_str(), int(e.on),
                            int(e.reverse), e.unknown0C, e.unknown10));
    }
    out.push_back(F("active camera %s", NameOf(At<uint32_t>(W().scene, 0x15C)).c_str()));
    // The scene's +0x210 (scene2.bin's 0x3200, inline).
    out.push_back("scene 210 " + VectorText(Ptr(W().scene + 0x210), 3));
    // The world's frames: their names, types, links and the scene's; a dummy's box (SetBBox: inline; its min and max,
    // the w parts not: the original's stack there).
    for (const S_FakeFrame &f : W().frames) {
        const I3D_frame *x = AsFrame(f.bytes);
        out.push_back(F("frame %s named %s type %d in scene %d first child %s next %s", NameOf(Ptr(f.bytes)).c_str(),
                        f.name.c_str(), int(x->m_type), int(g_inScene[g_side].count(Ptr(f.bytes))),
                        NameOf(Ptr(x->m_firstChild)).c_str(), NameOf(Ptr(x->m_nextSibling)).c_str()));
        if (x->m_type == FRAME_DUMMY)
            out.push_back("  box " + VectorText(Ptr(f.bytes + 0x120), 3) + " " + VectorText(Ptr(f.bytes + 0x130), 3));
        // (A lens flare's and a projector's inline setters' fields: scene2.bin's 0xB110, the projector's range.)
        if (x->m_type == FRAME_VISUAL && (f.visualType == I3DVISUAL_LENSFLARE || f.visualType == I3DVISUAL_PROJECTOR))
            out.push_back("  fields " + VectorText(Ptr(f.bytes + 0x1E0), 2) + F(" %d ", At<int>(f.bytes, 0x1EC)) +
                          VectorText(Ptr(f.bytes + 0x220), 2) + " " + VectorText(Ptr(f.bytes + 0x240), 1));
    }
    for (const std::string &s : FrameStates())
        out.push_back(s);
    return out;
}
// The globals the loading writes, the same on both sides before each.
void ResetMissionState(const std::string &oldName)
{
    SetMission(oldName);
    if (g_side) {
        uint8_t *g = g_origGame;
        // (The original's name a string of its heap, as its own: Begin releases it. SetMission's is a static buffer,
        // which the release would hand to its operator delete.)
        uint32_t rep = 0;
        OriginalAt<T_Fstr>(0x00405100)(&rep, "%s", oldName.c_str());
        std::memcpy(g + 0x6A, &rep, 4);
        *reinterpret_cast<int16_t *>(g + 0x66) = 77;
        *reinterpret_cast<int16_t *>(g + 0x68) = 88;
        *reinterpret_cast<int *>(g + 0xD8) = 99;
        *reinterpret_cast<float *>(g + 0xBC) = 1.5f;
        *reinterpret_cast<float *>(g + 0xC0) = 2.5f;
        *reinterpret_cast<float *>(g + 0xC4) = 3.5f;
        *reinterpret_cast<int *>(g + 0xC8) = -1;
        *reinterpret_cast<int *>(g + 0xCC) = -1;
        g[0x14] = 0;
        *OriginalAt<uint32_t *>(O_CAR_SCENE) = 0;
        *OriginalAt<uint32_t *>(O_PARTICLES_SCENE) = 0;
        *OriginalAt<uint32_t *>(O_TSHIFT_VECTOR + 8) = *OriginalAt<uint32_t *>(O_TSHIFT_VECTOR + 4);
    } else {
        C_game *g = reinterpret_cast<C_game *>(g_portGameBuf);
        g->m_unknown66 = 77;
        g->m_unknown68 = 88;
        g->m_unknownD8 = 99;
        g->m_fov = 1.5f;
        g->m_nearClip = 2.5f;
        g->m_farClip = 3.5f;
        g->m_music = -1;
        g->m_alarmMusic = -1;
        g->m_missionLoaded = false;
        g_carScene = nullptr;
        g_particlesModule.m_scene = nullptr;
        g_tshiftModule.m_entries.clear();
    }
}

// The caches Begin empties (models, shadows, the versions' models) given an entry each before a case (both sides'
// own: the original's through their vtables, OpenInto +0x04, Get +0x08).
void PrefillCaches()
{
    uint8_t *model = MakeFrame(FRAME_MODEL, 0, "prefill model");
    uint8_t *version = MakeFrame(FRAME_MODEL, 0, "prefill version");
    if (g_side) {
        using T_OpenInto = int(TC *)(void *, void *, const char *, uint32_t, void *, void *, int);
        using T_Get = int(TC *)(void *, const char *, void **, void *, void *);
        reinterpret_cast<T_OpenInto>((*OriginalAt<void ***>(O_MODEL_CACHE))[1])(OriginalAt<void *>(O_MODEL_CACHE), model,
                                                                                  "prefill_model", 0, nullptr, nullptr, 0);
        reinterpret_cast<T_OpenInto>((*OriginalAt<void ***>(O_VERSION_CACHE))[1])(OriginalAt<void *>(O_VERSION_CACHE), version,
                                                                                    "prefill_version", 0, nullptr, nullptr, 0);
        void *shadow = nullptr;
        reinterpret_cast<T_Get>((*OriginalAt<void ***>(O_SHADOW_CACHE))[2])(OriginalAt<void *>(O_SHADOW_CACHE), "prefill_shadow",
                                                                              &shadow, nullptr, nullptr);
    } else {
        g_modelCache.OpenInto(reinterpret_cast<I3D_model *>(model), "prefill_model");
        g_versionModelCache.OpenInto(reinterpret_cast<I3D_model *>(version), "prefill_version");
        I3D_shadow *shadow = nullptr;
        g_shadowCache.Get("prefill_shadow", &shadow);
    }
}

// ---- one case ----
struct S_MlCase {
    S_Mission mission;
    int op = 0;                                         // 0 the loader's functions, 1 LoadMission
    int sceneMode = 0;                                  // 1: the scene without a camera
    uint32_t role = GAME_ROLE_LOCAL;
    bool def = false;                                   // a mission definition passed
};
const char *const OP_NAMES[] = {"loader", "mission"};
int g_mlCases, g_opCases[2], g_gameCases;
bool g_mlShow;
S_MlCase RandomMlCase()
{
    S_MlCase c;
    static const char *only = std::getenv("HD2_ML_MISSION");
    static const bool synthOnly = std::getenv("HD2_ML_SYNTH") != nullptr;
    if (only)
        c.mission = GameMission(only);
    else if (!synthOnly && !g_gameMissions.empty() && P(0.05))
        c.mission = GameMission(g_gameMissions[size_t(I(0, int(g_gameMissions.size()) - 1))]);
    else
        c.mission = SynthMission();
    c.op = P(0.3) ? 1 : 0;
    c.sceneMode = P(0.3) ? 1 : 0;
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT,
                                     GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 4)];
    c.def = P(0.5);
    return c;
}
// The watchdog: a case that runs past a minute (a loop in one side's reading) ends the run, named.
volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("ml_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
void RunMlCase(const S_MlCase &c)
{
    g_caseStart = GetTickCount();
    ++g_mlCases;
    ++g_opCases[c.op];
    g_gameCases += c.mission.game;
    g_holdFrees = true;
    g_current = F("case %d: %s %s", g_mlCases, OP_NAMES[c.op], c.mission.name.c_str());
    const uint32_t worldSeed = U32(), fakeBase = U32();
    S_Result r;
    static uint8_t defs[2][0x40];
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        BuildMissionWorld(side, worldSeed, c.mission, c.sceneMode);
        Name(defs[side], "mission def");
        ResetMissionState("OLDMISSION");
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        // The caches Begin empties hold an entry each (its Clear releases it).
        PrefillCaches();
        g_fakeCount = 0;
        g_log[side].clear();
        g_opened.clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        const void *def = c.def ? defs[side] : nullptr;
        const char *name = c.mission.name.c_str();
        g_case = SIDE_NAMES[side];
        if (c.op == 0) {
            S_OrigLoader ol;
            C_MissionLoader *pl = nullptr;
            if (side) {
                OrigLoaderMake(ol);
                OriginalAt<T_Begin>(O_BEGIN)(ol.bytes, name, def);
                out.push_back(F("LoadScene %d", OriginalAt<T_IntOp>(O_LOAD_SCENE)(ol.bytes)));
                out.push_back(F("LoadScene2 %d", OriginalAt<T_IntOp>(O_LOAD_SCENE2)(ol.bytes)));
                OriginalAt<T_Void>(O_LOAD_ACTORS)(ol.bytes);
                OriginalAt<T_Void>(O_LOAD_SOUNDS)(ol.bytes);
                OriginalAt<T_Void>(O_LOAD_CHECKPOINTS)(ol.bytes);
            } else {
                pl = new C_MissionLoader;
                pl->Begin(name, static_cast<const C_MissionDef *>(def));
                out.push_back(F("LoadScene %d", int(pl->LoadScene())));
                out.push_back(F("LoadScene2 %d", pl->LoadScene2()));
                pl->LoadActors();
                pl->LoadSounds();
                pl->LoadCheckpoints();
            }
            g_case = "";
            for (const std::string &s : side ? OrigFrames(ol) : PortFrames(*pl))
                out.push_back(s);
            if (side)
                OrigLoaderFree(ol);
            else
                delete pl;
        } else {
            const int result =
                side ? OriginalAt<T_Mission>(O_LOAD_MISSION)(g_origGame, name, def)
                     : reinterpret_cast<C_game *>(g_portGameBuf)->LoadMission(name, static_cast<C_MissionDef *>(const_cast<void *>(def)));
            g_case = "";
            out.push_back(F("LoadMission %d", result));
        }
        for (const std::string &s : MissionState())
            out.push_back(s);
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        // (The files each side opened and closed, in order: Compare adds them.)
        (side ? r.openedOrig : r.openedPort) = g_opened;
        ClearCaches();
    }
    if (g_mlShow) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        // (HD2_ML_SHOW=2: the original's lines too.)
        if (std::strcmp(std::getenv("HD2_ML_SHOW"), "2") == 0)
            for (const std::string &s : r.orig)
                std::printf("  orig: %s\n", s.c_str());
    }
    Compare(g_current, r);
    ReleaseHeldFrees();
    // (A write past one of the original's blocks found while freeing this case's: the next Compare would skip
    // silently. A failure here instead: no case goes uncompared.)
    if (!g_overrun.empty()) {
        ++failures;
        std::printf("FAIL %s: the original wrote past a block\n", g_overrun.c_str());
        g_overrun.clear();
    }
    FreeWorld(0);
    FreeWorld(1);
}

void SetUpMission()
{
    SetUpCmd();
    SetUpMissionAnswers();
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e52d8, reinterpret_cast<const void *>(&O_dbgPrintf)},
        {0x007e6334, reinterpret_cast<const void *>(&O_Sprintf)},
        {0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite)},
        {0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek)},
        {0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite)},
        {0x007e562e, reinterpret_cast<const void *>(&O_Atexit)},
        {0x007e53c2, reinterpret_cast<const void *>(&O_AlignedMalloc)},
        {0x007e559c, reinterpret_cast<const void *>(&O_AlignedFree)},
        // LoadMission's copy protection: its two blocks jumped over (the port leaves them out).
        {0x006cac09, OriginalAt<const void *>(0x006cac72)},
        {0x006cadb1, OriginalAt<const void *>(0x006cae88)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    SetUpCaches();
    // The game's missions (each with a scene2.bin).
    WIN32_FIND_DATAA d;
    HANDLE h = FindFirstFileA((g_data + "\\Missions\\*").c_str(), &d);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::string name = d.cFileName;
            if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != "." && name != "..") {
                std::string text;
                if (!FileText("missions\\" + name + "\\scene2.bin", text))
                    continue;
                // (A name with a '%': the original takes the names as formats, the port as text (its fix). Such a
                // mission is left out: Libye1_mp's "AMB%BACK".)
                const S_Mission m = GameMission(name);
                if (std::any_of(m.names.begin(), m.names.end(),
                                [](const S_Named &n) { return n.name.find('%') != std::string::npos; })) {
                    g_percentMissions.push_back(name);
                    continue;
                }
                g_gameMissions.push_back(name);
            }
        } while (FindNextFileA(h, &d));
        FindClose(h);
    }
}

}  // namespace H

int main()
{
    using namespace H;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("ml_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *d = std::getenv("HD2_UNPACKED"))
        g_data = d;
    if (const char *s = std::getenv("HD2_ML_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    g_mlShow = std::getenv("HD2_ML_SHOW") != nullptr;
    g_dump = std::getenv("HD2_ML_DUMP") != nullptr;
    int cases = std::getenv("HD2_ML_QUICK") ? 600 : 3000;
    if (const char *s = std::getenv("HD2_ML_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpMission();
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    // Each of the game's missions once (the loader's functions; HD2_ML_QUICK: every fourth), then the random cases.
    if (!std::getenv("HD2_ML_SYNTH") && !std::getenv("HD2_ML_MISSION"))
        for (size_t m = 0; m < g_gameMissions.size(); m += std::getenv("HD2_ML_QUICK") ? 4 : 1) {
            S_MlCase c = RandomMlCase();
            c.mission = GameMission(g_gameMissions[m]);
            c.op = 0;
            RunMlCase(c);
        }
    for (int n = 0; n < cases; ++n)
        RunMlCase(RandomMlCase());
    std::string percent;
    for (const std::string &m : g_percentMissions)
        percent += (percent.empty() ? "" : ", ") + m;
    std::printf("ml_diff: %d cases (loader %d, mission %d; the game's missions %d of %d, left out: %s), %d calls logged, "
                "%d skipped (overruns), %d checks, %d failures\n",
                g_mlCases, g_opCases[0], g_opCases[1], g_gameCases, int(g_gameMissions.size()),
                percent.empty() ? "none" : percent.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
