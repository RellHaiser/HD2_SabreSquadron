// Human part 80: the meleeTracker's animation mixer (game/melee_tracker_anim_mixer.cpp: C_MeleeTrackerAnimMixer, the
// body's field at +0x10, 0x150 bytes in the original's layout) against the original's (0x005c6d10 .. 0x005c7b9c,
// 0x005cfd20 .. 0x005d0977). MinGW; build_mtmix.bat; runs mtmix_diff.exe ("mtmix_diff: N checks, 0 failures").
// melee_tracker_anim_mixer.cpp is compiled into this file. Each side drives its own mixer (the same start bytes) over
// one fake model (the interface kit's frame f0: its eight stages' animation, weight, time, flags and activity kept here,
// restored for each side; FindChildFrame, SetCallback, Tick, SetAnimation .. AdvanceStages logged) with a note frame
// "a" (f1, its parent the model, the model's frame data naming each side's tracker: the original reaches the mixer as
// the actor + 0xA0, the port through its own layout). The animation sets are fakes (Release logged, GetEndTime, the
// file name read inline); the animation cache's Get answers them by name on both sides (the original's 0x0041fc10
// redirected). The chunks are real on both sides (common/chunk.cpp, the original's own) over data files in memory.
// MixerBlendProgress (0x00772900, human part 7's) is the original's on both sides.
// Each scenario sets the mixer, the model and the world, then runs up to 14 operations (Init, Reset, UpdateStages,
// UpdateMove, PlayStages, SetAddMove, SetSlide, the track getters and setters, TakeSoundEvents, GetBlendProgress, the
// note keys through NoteHook and OnNote, SaveGame compared byte for byte with the next chunk after it, LoadGame of the
// side's own save or of a file the scenario builds); the logs (every call out, the results, the mixer's and the
// model's state after each operation) compared.
// HD2_MTMIX_ROUNDS: scenarios (default 60,000; HD2_MTMIX_QUICK: 20,000); HD2_INTERFACE_ULPS, HD2_INTERFACE_SHOW,
// HD2_INTERFACE_ECHO, HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/resource_cache.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
// (The cache's Get, faked below: declared before the mixer's source uses it.)
template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *fileName, I3D_animation_set **out, T_CacheErrorProc, void *);

#include "game/melee_tracker_anim_mixer.cpp"
#include "ls3d/rw_data.h"
#undef private
#undef protected

#include "original_exe.h"

#include "../interface/interface_kit.inc"

// MixerBlendProgress: the original's on both sides (human part 7 tested the port's against it).
float __stdcall MixerBlendProgress(float elapsed, float step, float duration)
{
    using T = float(__stdcall *)(float, float, float);
    return OriginalAt<T>(0x00772900)(elapsed, step, duration);
}

namespace H {

int g_side;
template <class T> T At(const void *b, uint32_t o)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(b) + o, sizeof v);
    return v;
}

// ---- the mixers (each side's; the original's layout on both) ----
alignas(16) C_MeleeTrackerAnimMixer g_mix[2];
C_MeleeTrackerAnimMixer &Mix() { return g_mix[g_side]; }
// The tracker each side's NoteHook reaches: the original's actor + 0xA0, the port's through its C_MeleeTracker.
size_t PortMixerOffset()
{
    return offsetof(C_MeleeTracker, m_movement) + offsetof(C_MeleeTrackerMovement, m_mixer);
}
S_FrameData g_frameData[2];

// ---- the model's stages ----
constexpr int STAGES = 8;
struct S_Stages {
    int anim[STAGES];                                   // -1 none, else a set's index
    float weight[STAGES];
    int time[STAGES];
    int flags[STAGES];
    int active[STAGES];
};
S_Stages g_st;

// ---- the animation sets ----
constexpr int SETS = 6;
struct S_FakeSet {
    void **vt;
    int refCount;                                       // +0x04
    int playMode;                                       // +0x08
    const char *fileName;                               // +0x0C
    int endTime;
};
S_FakeSet g_sets[SETS];
void *g_setVt[16];
const char *const SET_NAMES[SETS] = {"anims\\dog\\walk.i3d", "anims\\dog\\run.i3d", "bite.i3d", "anims\\dog\\sit.i3d",
                                     "anims\\idle.i3d", nullptr};
const int SET_ENDS[SETS] = {900, 640, 1, 2400, 333, 1700};
int SetIndex(const void *p)
{
    for (int i = 0; i < SETS; ++i)
        if (p == &g_sets[i])
            return i;
    return p ? -2 : -1;
}
I3D_animation_set *SetAt(int i) { return i < 0 ? nullptr : reinterpret_cast<I3D_animation_set *>(&g_sets[i]); }
std::string PartName(const void *p)
{
    const int s = SetIndex(p);
    if (s >= 0)
        return "set" + std::to_string(s);
    if (p == &g_mix[0] || p == &g_mix[1])
        return "mixer";
    return "";
}
uint32_t __stdcall S_Release(void *self)
{
    Log("%s Release", Name(self).c_str());
    return uint32_t(--static_cast<S_FakeSet *>(self)->refCount);
}
int __stdcall S_EndTime(void *self) { return static_cast<S_FakeSet *>(self)->endTime; }
void __stdcall S_Other(void *self) { Log("%s set other slot", Name(self).c_str()); }

// ---- the model's slots (the kit's frame vtable) ----
I3D_frame *Model() { return FrameAt(0); }
I3D_frame *NoteFrame() { return FrameAt(1); }
bool StageOk(int stage)
{
    if (stage >= 0 && stage < STAGES)
        return true;
    Log("bad stage %d", stage);
    return false;
}
void __stdcall M_Tick(void *f, int time) { Log("%s Tick %d", Name(f).c_str(), time); }
void __stdcall M_SetCallback(void *f, void *cb, uint32_t messages)
{
    // (Each side's NoteHook logged as "hook".)
    const bool hook = cb == reinterpret_cast<void *>(&C_MeleeTrackerAnimMixer::NoteHook) || cb == OriginalAt<void *>(0x005c7960);
    Log("%s SetCallback %s %x", Name(f).c_str(), cb ? hook ? "hook" : "other" : "null", messages);
    static_cast<I3D_frame *>(f)->m_callback = hook ? &C_MeleeTrackerAnimMixer::NoteHook : reinterpret_cast<I3D_CALLBACK *>(cb);
}
void __stdcall M_SetAnimation(void *f, void *anim, int stage, float weight, int flags)
{
    Log("%s SetAnimation %s %d %s %d", Name(f).c_str(), Name(anim).c_str(), stage, FloatText(weight).c_str(), flags);
    if (!StageOk(stage))
        return;
    g_st.anim[stage] = SetIndex(anim);
    g_st.weight[stage] = weight;
    g_st.time[stage] = 0;
    g_st.flags[stage] = flags;
    g_st.active[stage] = anim != nullptr;
}
void *__stdcall M_GetAnimation(void *, int stage) { return StageOk(stage) ? SetAt(g_st.anim[stage]) : nullptr; }
void __stdcall M_StopAnimation(void *f, int stage)
{
    Log("%s StopAnimation %d", Name(f).c_str(), stage);
    if (StageOk(stage))
        g_st.active[stage] = 0;
}
void __stdcall M_SetAnimWeight(void *f, int stage, float weight)
{
    Log("%s SetAnimWeight %d %s", Name(f).c_str(), stage, FloatText(weight).c_str());
    if (StageOk(stage))
        g_st.weight[stage] = weight;
}
float __stdcall M_GetAnimWeight(void *, int stage) { return StageOk(stage) ? g_st.weight[stage] : 0.0f; }
void __stdcall M_SetAnimTime(void *f, int stage, int time)
{
    Log("%s SetAnimTime %d %d", Name(f).c_str(), stage, time);
    if (StageOk(stage))
        g_st.time[stage] = time;
}
int __stdcall M_GetAnimTime(void *, int stage) { return StageOk(stage) ? g_st.time[stage] : 0; }
bool __stdcall M_IsAnimActive(void *, int stage) { return StageOk(stage) && g_st.active[stage] != 0; }
void __stdcall M_AdvanceStages(void *f, const int *times)
{
    std::string t;
    for (int i = 0; i < STAGES; ++i) {
        t += " " + std::to_string(times[i]);
        if (g_st.anim[i] >= 0)
            g_st.time[i] += times[i];
    }
    Log("%s AdvanceStages%s", Name(f).c_str(), t.c_str());
}

// ---- the animation cache (both sides) ----
std::string g_cacheMissing;                             // a name the cache does not find
HRESULT CacheGet(const char *name, I3D_animation_set **out)
{
    Log("cache Get %s", name);
    *out = nullptr;
    if (name == g_cacheMissing)
        return static_cast<HRESULT>(0x80004005);
    for (int i = 0; i < SETS; ++i) {
        const char *file = SET_NAMES[i];
        if (!file)
            continue;
        const char *slash = std::strrchr(file, '\\');
        if (std::strcmp(slash ? slash + 1 : file, name) == 0 || std::strcmp(file, name) == 0) {
            ++g_sets[i].refCount;
            *out = SetAt(i);
            return I3D_OK;
        }
    }
    return static_cast<HRESULT>(0x80004005);
}
HRESULT __attribute__((thiscall)) O_CacheGet(void *, const char *name, I3D_animation_set **out, void *, void *)
{
    return CacheGet(name, out);
}

}  // namespace H

template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *fileName, I3D_animation_set **out, T_CacheErrorProc, void *)
{
    return H::CacheGet(fileName, out);
}

// The data files in memory (both sides).
namespace H {
std::map<std::string, std::string> g_files;
struct S_OpenFile {
    std::string name;
    size_t at = 0;
};
std::map<int, S_OpenFile> g_openFiles;
int g_nextFile = 1;
bool g_fileLog = true;
int FileOpen(const char *name, bool write)
{
    if (!write && !g_files.count(name)) {
        if (g_fileLog)
            Log("dta open %s -> -1", name);
        return -1;
    }
    if (write)
        g_files[name].clear();
    g_openFiles[g_nextFile] = S_OpenFile{name, 0};
    if (g_fileLog)
        Log("dta open %s %d -> %d", name, write, g_nextFile);
    return g_nextFile++;
}
int FileSeek(int handle, int offset, int origin)
{
    if (!g_openFiles.count(handle))
        return -1;
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    o.at = origin == 0 ? size_t(offset) : origin == 1 ? o.at + offset : f.size() + offset;
    return int(o.at);
}
int FileRead(int handle, void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    const int got = o.at >= f.size() ? 0 : std::max(0, std::min(int(f.size() - o.at), size));
    if (got)
        std::memcpy(data, f.data() + o.at, size_t(got));
    o.at += size_t(got);
    return got;
}
int FileWrite(int handle, const void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    std::string &f = g_files[o.name];
    if (o.at + size_t(size) > f.size())
        f.resize(o.at + size_t(size));
    std::memcpy(&f[o.at], data, size_t(size));
    o.at += size_t(size);
    return size;
}
bool FileClose(int handle)
{
    if (g_fileLog)
        Log("dta close %d", handle);
    return g_openFiles.erase(handle) != 0;
}
int __stdcall O_dtaOpen(const char *name, bool) { return FileOpen(name, false); }
int __stdcall O_dtaOpenWrite(const char *name, int) { return FileOpen(name, true); }
int __stdcall O_dtaSeek(int h, int offset, int origin) { return FileSeek(h, offset, origin); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
int __stdcall O_dtaWrite(int h, const void *data, int size) { return FileWrite(h, data, size); }
bool __stdcall O_dtaClose(int h) { return FileClose(h); }
}  // namespace H
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name, false); }
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpen(name, true); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }

namespace H {

// ---- the chunks (each side's own functions; the layouts are the same) ----
using T_ChunkOpen = bool(__attribute__((thiscall)) *)(void *, const char *, bool);
using T_ChunkClose = bool(__attribute__((thiscall)) *)(void *);
using T_ChunkBegin = bool(__attribute__((thiscall)) *)(void *, uint16_t);
using T_ChunkWrite = bool(__attribute__((thiscall)) *)(void *, const void *, int);
using T_ChunkAscend = bool(__attribute__((thiscall)) *)(void *, uint32_t *);
bool CBegin(void *c, uint16_t id) { return g_side ? static_cast<C_chunk *>(c)->Begin(id) : OriginalAt<T_ChunkBegin>(0x00405820)(c, id); }
bool CWrite(void *c, const void *d, int n) { return g_side ? static_cast<C_chunk *>(c)->Write(d, n) : OriginalAt<T_ChunkWrite>(0x004059b0)(c, d, n); }
bool CAscend(void *c) { return g_side ? static_cast<C_chunk *>(c)->Ascend() : OriginalAt<T_ChunkAscend>(0x00405890)(c, nullptr); }
int CPos(const void *c) { return At<bool>(c, 0) ? FileSeek(At<int>(c, 4), 0, 1) : -1; }
int CDepth(const void *c) { return At<int>(c, 0x410); }
struct S_SideChunk {
    alignas(16) uint8_t orig[0x500] = {};
    C_chunk port;
    void *Ptr(int side) { return side ? static_cast<void *>(&port) : static_cast<void *>(orig); }
};
bool ChunkOpen(int side, S_SideChunk &c, const char *name, bool write)
{
    return side ? c.port.Open(name, write) : OriginalAt<T_ChunkOpen>(0x00405620)(c.orig, name, write);
}
void ChunkClose(int side, S_SideChunk &c) { side ? c.port.Close() : OriginalAt<T_ChunkClose>(0x00405750)(c.orig); }

// ---- the original's entry points ----
using T_V = void(__attribute__((thiscall)) *)(void *);
using T_VP = void(__attribute__((thiscall)) *)(void *, void *);
using T_Stages = void(__attribute__((thiscall)) *)(void *, int, int, int, const void *);
using T_Move = void(__attribute__((thiscall)) *)(void *, int, const float *);
using T_Play = void(__attribute__((thiscall)) *)(void *, const void *);
using T_SetMove = void(__attribute__((thiscall)) *)(void *, const float *, int);
using T_GetInt = int(__attribute__((thiscall)) *)(void *, int);
using T_Rate = void(__attribute__((thiscall)) *)(void *, int, float);
using T_Events = int *(__attribute__((thiscall)) *)(void *, int, int *);
using T_Blend = float(__attribute__((thiscall)) *)(void *, int);
using T_Note = void(__attribute__((thiscall)) *)(void *, uint32_t, const char *, uintptr_t);
using T_Hook = int(__stdcall *)(I3D_frame *, uint32_t, uintptr_t, uintptr_t);
using T_RetBP = bool(__attribute__((thiscall)) *)(void *, void *);

// ---- the world ----
struct S_World {
    alignas(16) uint8_t mixer[sizeof(C_MeleeTrackerAnimMixer)];
    S_Stages stages;
    int playMode[SETS];
    bool noteFrame;                                     // the model has its note frame "a"
    uint32_t noteCallback;                              // 0 none, 1 the hook, 2 another
};
S_World g_w;
int MaybeTime(double none, int lo, int hi) { return P(none) ? MELEE_TRACKER_MOVE_NO_TIME : I(lo, hi); }
float Pf(std::initializer_list<float> l) { return l.begin()[I(0, int(l.size()) - 1)]; }
S_vector4 RandomVector(float scale)
{
    S_vector4 v;
    v.x = P(0.1) ? 0.0f : F(-scale, scale);
    v.y = P(0.3) ? 0.0f : F(-scale, scale);
    v.z = P(0.1) ? 0.0f : F(-scale, scale);
    v.w = F(-1, 1);
    return v;
}
void RandomWorld()
{
    C_MeleeTrackerAnimMixer x;
    std::memset(static_cast<void *>(&x), 0, sizeof x);
    x.m_vtable = reinterpret_cast<void *>(0x008121d0);
    for (uint8_t &b : x.m_unknown04)
        b = uint8_t(U32());
    // (Now and then another model: Init's own.)
    x.m_model = reinterpret_cast<I3D_model *>(P(0.85) ? Model() : FrameAt(2));
    for (S_MeleeTrackerMixerTrack &t : x.m_tracks) {
        t.current = P(0.3) ? -1 : I(0, 1);
        t.time = P(0.05) ? 0x7FFFD8EF - I(-60, 60) : P(0.05) ? 0x7FFFD8EF - Pick({0, 1, 79, 80, 81, 120}) : P(0.1) ? -1 : I(0, 2500);
        t.hold = P(0.6) ? 0 : P(0.4) ? Pf({-1, 1}) : I(-5, 900);
        t.eventCount = I(0, 2);
        for (int &e : t.events)
            e = P(0.4) ? 0 : I(1, 3);
        t.length = I(0, 2500);
        t.reversed = uint8_t(P(0.2));
        t.pad21[0] = uint8_t(U32());
    }
    for (S_MeleeTrackerMixerStage &s : x.m_stages) {
        s.weight = P(0.3) ? 1.0f : F(0, 1);
        s.blendTime = P(0.2) ? 0.0f : P(0.05) ? F(-50, 0) : F(1, 500);
        s.blendElapsed = P(0.3) ? MELEE_TRACKER_MIXER_NO_BLEND : P(0.05) ? F(-30, 0) : P(0.1) ? 0.0f : F(0, 600);
        if (P(0.05))
            s.blendElapsed = s.blendTime;
        s.crossfade = uint8_t(P(0.5) ? 0 : P(0.9) ? 1 : I(2, 255));
        s.flags = P(0.6) ? 0 : P(0.6) ? MELEE_TRACKER_MIXER_STAGE_FADE_OUT : U32() & 0xF;
        s.rate = P(0.6) ? 1.0f : P(0.3) ? 0.0f : F(0.2f, 2.0f);
        s.finished = uint8_t(P(0.3));
        s.tag = I(-1, 3);
    }
    x.m_blendScratch = P(0.3) ? 0.0f : P(0.3) ? 1.0f : F(0, 1);
    x.m_speedPrevious = P(0.3) ? 0.0f : F(0, 6);
    x.m_speedCurrent = P(0.3) ? 0.0f : F(0, 6);
    for (uint8_t &b : x.m_unknownE8)
        b = uint8_t(U32());
    x.m_move = RandomVector(0.5f);
    x.m_addMove = RandomVector(2.0f);
    x.m_addMoveTime = I(0, 700);
    x.m_addMoveDuration = MaybeTime(0.4, 1, 600);
    for (uint8_t &b : x.m_unknown118)
        b = uint8_t(U32());
    x.m_slide = RandomVector(2.0f);
    x.m_slideVelocity = RandomVector(0.2f);
    x.m_slideStepTime = I(0, 79);
    x.m_slideTime = I(0, 500);
    x.m_slideDuration = MaybeTime(0.4, 1, 800);
    if (P(0.1) && x.m_slideDuration != MELEE_TRACKER_MOVE_NO_TIME)
        x.m_slideDuration = x.m_slideTime + I(0, 40);
    x.m_stageFinished = uint8_t(P(0.3));
    x.pad14D[0] = uint8_t(U32());
    std::memcpy(g_w.mixer, static_cast<void *>(&x), sizeof x);
    for (int i = 0; i < STAGES; ++i) {
        g_w.stages.anim[i] = P(0.3) ? -1 : I(0, SETS - 1);
        g_w.stages.weight[i] = P(0.2) ? 0.0f : F(0, 1);
        g_w.stages.time[i] = I(0, 2500);
        g_w.stages.flags[i] = I(0, 3);
        g_w.stages.active[i] = P(0.8);
    }
    for (int &m : g_w.playMode)
        m = I(1, 2);
    g_w.noteFrame = P(0.85);
    g_w.noteCallback = uint32_t(I(0, 2));
}
int __stdcall OtherCallback(I3D_frame *, uint32_t, uintptr_t, uintptr_t) { return 0; }
I3D_frame *FindNote(I3D_frame *f, const char *name)
{
    return (f == Model() || f == FrameAt(2)) && g_w.noteFrame && std::strcmp(name, "a") == 0 ? NoteFrame() : nullptr;
}
void SetWorld(int side)
{
    std::memcpy(static_cast<void *>(&g_mix[side]), g_w.mixer, sizeof g_w.mixer);
    g_st = g_w.stages;
    for (int i = 0; i < SETS; ++i) {
        g_sets[i].vt = g_setVt;
        g_sets[i].refCount = 1;
        g_sets[i].playMode = g_w.playMode[i];
        g_sets[i].fileName = SET_NAMES[i];
        g_sets[i].endTime = SET_ENDS[i];
    }
    NoteFrame()->m_callback = g_w.noteCallback == 0 ? nullptr
                              : g_w.noteCallback == 1 ? &C_MeleeTrackerAnimMixer::NoteHook
                                                      : &OtherCallback;
    // The model's frame data: the side's tracker.
    uint8_t *mixer = reinterpret_cast<uint8_t *>(&g_mix[side]);
    g_frameData[side].actor = reinterpret_cast<C_Actor *>(mixer - (side ? PortMixerOffset() : 0xA0));
    Model()->m_data = &g_frameData[side];
}

// The mixer's and the model's state (the original's stack words left out: the move's w always (ResetTracks and
// UpdateMove store one), the vectors' w once loaded (LoadGame keeps its stack copy's)).
bool g_loaded;
std::string Snap(int side)
{
    uint8_t b[sizeof(C_MeleeTrackerAnimMixer)];
    std::memcpy(b, static_cast<void *>(&g_mix[side]), sizeof b);
    std::memset(b + 0xFC, 0, 4);
    if (g_loaded)
        for (uint32_t at : {0x10Cu, 0x12Cu, 0x13Cu})
            std::memset(b + at, 0, 4);
    std::string s = "mixer";
    char t[12];
    for (size_t i = 0; i < sizeof b; i += 4) {
        uint32_t w;
        std::memcpy(&w, b + i, 4);
        std::snprintf(t, sizeof t, " %x", w);
        s += t;
    }
    s += " | stages";
    for (int i = 0; i < STAGES; ++i) {
        std::snprintf(t, sizeof t, " %d", g_st.anim[i]);
        s += t + (" " + FloatText(g_st.weight[i]));
        std::snprintf(t, sizeof t, " %d %d %d", g_st.time[i], g_st.flags[i], g_st.active[i]);
        s += t;
    }
    s += " | refs";
    for (const S_FakeSet &set : g_sets)
        s += " " + std::to_string(set.refCount);
    s += NoteFrame()->m_callback == &C_MeleeTrackerAnimMixer::NoteHook ? " | hook" : NoteFrame()->m_callback ? " | other" : " | none";
    return s;
}

// ---- the operations ----
enum {
    OP_INIT,
    OP_RESET,
    OP_UPDATE_STAGES,
    OP_UPDATE_MOVE,
    OP_PLAY,
    OP_ADD_MOVE,
    OP_SLIDE,
    OP_ACTIVE_TIME,
    OP_LENGTH,
    OP_RATE,
    OP_EVENTS,
    OP_BLEND,
    OP_NOTE_HOOK,
    OP_NOTE,
    OP_SAVE,
    OP_LOAD,
    OP_LOAD_OWN,
    W_STAGES,
    OPS
};
struct S_Op {
    int kind;
    int a, b, c;
    float v[4];
    float f;
    S_MeleeTrackerMixerAnim anims[2];
    int active[10];
    bool hasActive;
    std::string note;
    uint32_t seed;
};
const char *const NOTES[] = {"a1", "a2", "a3", "a", "m", "mx", "s", "step", "x", "", "A1", "S"};
S_Op RandomOp()
{
    S_Op op{};
    op.kind = I(0, OPS - 1);
    op.a = I(0, 1);
    op.b = I(-1, 120);
    if (P(0.1))
        op.b = Pick({0, 1, -1, 80, 79, 81, 160});
    op.c = I(0, 9);
    const S_vector4 v = RandomVector(P(0.2) ? 1e-4f : P(0.3) ? 1.0f : 4.0f);
    op.v[0] = v.x;
    op.v[1] = v.y;
    op.v[2] = v.z;
    op.v[3] = v.w;
    if (P(0.05)) {                                      // a unit vector
        const float l = std::sqrt(op.v[0] * op.v[0] + op.v[1] * op.v[1] + op.v[2] * op.v[2]);
        if (l > 0)
            for (int k = 0; k < 3; ++k)
                op.v[k] /= l;
    }
    if (P(0.05))
        op.v[0] = op.v[1] = op.v[2] = 0.0f;
    if (P(0.1)) {                                       // (the slide's short and unit cases: one axis or tiny)
        const float a = Pick({0.0f, 1.0f, -1.0f, 2e-5f, -2e-5f, 1e-6f, -3e-6f, 0.5f});
        const int k = I(0, 2);
        op.v[0] = op.v[1] = op.v[2] = 0.0f;
        op.v[k] = a;
        if (P(0.3))
            op.v[I(0, 2)] = Pick({0.0f, 1e-6f, -1e-6f});
    }
    op.f = P(0.3) ? 1.0f : P(0.2) ? 0.0f : F(0.1f, 2.5f);
    for (S_MeleeTrackerMixerAnim &a : op.anims) {
        a.anim = P(0.3) ? nullptr : SetAt(I(0, SETS - 1));
        a.speed = P(0.3) ? 0.0f : F(0, 6);
        a.weight = P(0.4) ? 1.0f : F(0, 1);
        a.blendTime = P(0.2) ? 0.0f : F(1, 400);
        a.crossfade = uint8_t(P(0.5) ? 0 : P(0.9) ? 1 : I(2, 255));
    }
    op.hasActive = P(0.6);
    for (int &x : op.active)
        x = P(0.5) ? 0 : int(U32() | 1);
    op.note = Pick(NOTES);
    op.seed = U32();
    return op;
}
void LogFloat(const char *what, float f) { Log("%s %s", what, FloatText(f).c_str()); }

// A file for LoadGame: the mixer's chunk (0x49C1, now and then another) with its sub-chunks in any order, some left
// out or doubled, unknown ones among them; the stage chunks (0x50 + 6 * stage + kind: the name, the weight, the time,
// kinds the loader skips).
std::string MakeLoadFile()
{
    const bool keep = g_fileLog;
    g_fileLog = false;
    const std::map<std::string, std::string> kept = g_files;
    C_chunk c;
    c.Open("make.sav", true);
    c.Begin(P(0.06) ? uint16_t(0x49C2) : uint16_t(0x49C1));
    std::vector<int> ids;
    for (int id = 0; id <= 0x0E; ++id)
        if (P(0.8))
            ids.push_back(id);
    for (int stage = 0; stage < 6; ++stage)
        for (int kind = 0; kind < 6; ++kind)
            if (P(kind < 3 ? 0.6 : 0.1))
                ids.push_back(0x50 + stage * 6 + kind);
    if (P(0.1))
        ids.push_back(0x74);                            // (stage 6's name: the last id read)
    for (int n = I(0, 2); n > 0; --n)
        ids.push_back(Pick({0x0F, 0x10, 0x4F, 0x75, 0x76, 0x100, 0x49C1}));
    for (int n = I(0, 2); n > 0 && !ids.empty(); --n)
        ids.push_back(ids[size_t(I(0, int(ids.size()) - 1))]);
    std::shuffle(ids.begin(), ids.end(), g_rng);
    // (Each field its type's values: times in milliseconds, floats as floats. Junk words (an int of a billion as a
    // time) are no state of the game's: the original's x87 code converts them exactly where the port's float
    // conversion rounds.)
    const auto fl = [&](float lo, float hi) { return Bits(F(lo, hi)); };
    const auto in = [&](int lo, int hi) { return uint32_t(I(lo, hi)); };
    for (const int id : ids) {
        std::vector<uint32_t> w;
        std::string text;
        bool isText = false;
        if (id == 0) {
            for (int t = 0; t < 2; ++t)
                for (uint32_t x : {in(-1, 1), in(-1, 2500), P(0.6) ? 0u : in(-2, 900), in(0, 3), in(0, 3), in(0, 3), in(0, 2),
                                   in(0, 2500), in(0, 1)})
                    w.push_back(x);
        } else if (id == 1) {
            for (int st = 0; st < 4; ++st)
                for (uint32_t x : {fl(0, 1), P(0.2) ? 0u : fl(1, 500), P(0.3) ? Bits(MELEE_TRACKER_MIXER_NO_BLEND) : fl(0, 600),
                                   in(0, 1), in(0, 1), P(0.6) ? Bits(1.0f) : fl(0, 2), in(0, 1), in(-1, 3)})
                    w.push_back(x);
        } else if (id == 2 || id == 3 || id == 4) {
            w = {fl(0, 5)};
        } else if (id == 5 || id == 6 || id == 9 || id == 10) {
            w = {fl(-3, 3), fl(-3, 3), fl(-3, 3)};
        } else if (id == 8 || id == 0x0D) {
            w = {P(0.4) ? uint32_t(MELEE_TRACKER_MOVE_NO_TIME) : in(1, 800)};
        } else if (id == 7 || id == 0x0B || id == 0x0C) {
            w = {in(0, id == 0x0B ? 79 : 700)};
        } else if (id >= 0x50 && id <= 0x74 && (id - 0x50) % 6 == 0) {
            isText = true;
            // (Names the cache has, a missing one, an empty one.)
            const int s = I(-2, SETS - 2);
            if (s >= 0) {
                const char *file = SET_NAMES[s];
                const char *slash = std::strrchr(file, '\\');
                text = P(0.8) && slash ? slash + 1 : file;
            } else if (s == -2)
                text = "missing.i3d";
        } else if (id >= 0x50 && id <= 0x74 && (id - 0x50) % 6 == 1) {
            w = {Bits(P(0.3) ? 1.0f : F(0, 1))};
        } else if (id == 0x0E) {
            w = {uint32_t(P(0.5) ? I(0, 1) : I(-3, 300))};
        } else {
            w = {in(-5, 3000)};
        }
        if (P(0.04) && !isText)
            w.push_back(U32());                         // (a longer chunk: its rest skipped)
        c.Begin(uint16_t(id));
        if (isText)
            c.Write(text.c_str(), int(text.size() + 1));
        else
            c.Write(w.data(), int(w.size() * 4));
        c.Ascend();
    }
    c.Ascend();
    c.Begin(0x7777);
    const uint32_t next = 0x12345678;
    c.Write(&next, 4);
    c.Ascend();
    c.Close();
    std::string f = g_files["make.sav"];
    if (P(0.02))
        f.clear();
    g_files = kept;
    g_fileLog = keep;
    return f;
}

std::string g_ownSave[2];
void Run(int side, const S_Op &op)
{
    C_MeleeTrackerAnimMixer &m = Mix();
    void *x = &g_mix[side];
    switch (op.kind) {
    case OP_INIT:
        side ? m.Init(reinterpret_cast<I3D_model *>(Model())) : OriginalAt<T_VP>(0x005c6e10)(x, Model());
        break;
    case OP_RESET:
        side ? m.Reset() : OriginalAt<T_V>(0x005d0950)(x);
        break;
    case OP_UPDATE_STAGES: {
        const int delta = op.c == 0 ? -1 : op.b;
        const bool start = op.c >= 7;
        const int *active = op.hasActive ? op.active : nullptr;
        side ? m.UpdateStages(op.a, delta, start, active) : OriginalAt<T_Stages>(0x005c6ee0)(x, op.a, delta, start, active);
        break;
    }
    case OP_UPDATE_MOVE:
        side ? m.UpdateMove(op.b, op.v) : OriginalAt<T_Move>(0x005c71e0)(x, op.b, op.v);
        break;
    case OP_PLAY:
        side ? m.PlayStages(op.anims) : OriginalAt<T_Play>(0x005c7430)(x, op.anims);
        break;
    case OP_ADD_MOVE:
        side ? m.SetAddMove(op.v, op.b) : OriginalAt<T_SetMove>(0x005c75c0)(x, op.v, op.b);
        break;
    case OP_SLIDE: {
        const int duration = op.b == 0 ? 1 : op.b;     // (0: a division by zero in both)
        side ? m.SetSlide(op.v, duration) : OriginalAt<T_SetMove>(0x005c7600)(x, op.v, duration);
        break;
    }
    case OP_ACTIVE_TIME:
        Log("result %d", side ? m.GetActiveTrackTime(op.a) : OriginalAt<T_GetInt>(0x005c7850)(x, op.a));
        break;
    case OP_LENGTH:
        Log("result %d", side ? m.GetTrackLength(op.a) : OriginalAt<T_GetInt>(0x005c7890)(x, op.a));
        break;
    case OP_RATE:
        side ? m.SetTrackRate(op.a, op.f) : OriginalAt<T_Rate>(0x005c7930)(x, op.a, op.f);
        break;
    case OP_EVENTS: {
        int count = -7;
        int *events = side ? m.TakeSoundEvents(op.a, &count) : OriginalAt<T_Events>(0x005c7ae0)(x, op.a, &count);
        Log("events +%d count %d", int(reinterpret_cast<uint8_t *>(events) - static_cast<uint8_t *>(x)), count);
        break;
    }
    case OP_BLEND:
        LogFloat("result", side ? m.GetBlendProgress(op.a) : OriginalAt<T_Blend>(0x005c7b00)(x, op.a));
        break;
    case OP_NOTE_HOOK:
    case OP_NOTE: {
        // (A track with three events: a fourth is the port's fix (the original writes past the array), not compared.)
        if (m.m_tracks[0].eventCount >= MELEE_TRACKER_MIXER_TRACK_EVENTS ||
            m.m_tracks[1].eventCount >= MELEE_TRACKER_MIXER_TRACK_EVENTS) {
            Log("note skipped");
            break;
        }
        const char *note = op.c == 0 && op.kind == OP_NOTE ? nullptr : op.note.c_str();
        const uintptr_t p = reinterpret_cast<uintptr_t>(note);
        if (op.kind == OP_NOTE_HOOK)
            Log("result %d", side ? C_MeleeTrackerAnimMixer::NoteHook(NoteFrame(), I3DCALLBACK_ANIM_NOTE, p, op.seed)
                                  : OriginalAt<T_Hook>(0x005c7960)(NoteFrame(), I3DCALLBACK_ANIM_NOTE, p, op.seed));
        else
            side ? m.OnNote(op.seed, note, op.seed) : OriginalAt<T_Note>(0x005c79b0)(x, op.seed, note, op.seed);
        break;
    }
    case OP_SAVE:
    case OP_LOAD:
    case OP_LOAD_OWN: {
        const bool save = op.kind == OP_SAVE;
        const char *name = save ? "save.sav" : "load.sav";
        if (op.kind == OP_LOAD_OWN) {
            if (g_ownSave[side].empty())
                break;
            g_files[name] = g_ownSave[side];
        }
        S_SideChunk c;
        const bool open = op.c != 9;                    // (now and then the chunk not open)
        if (open)
            ChunkOpen(side, c, name, save);
        if (save && open && op.c == 8)                  // (inside another chunk)
            CBegin(c.Ptr(side), 0x1111);
        bool result;
        if (save)
            result = side ? m.SaveGame(c.port) : OriginalAt<T_RetBP>(0x005cfd20)(x, c.orig);
        else
            result = side ? m.LoadGame(c.port) : OriginalAt<T_RetBP>(0x005d0180)(x, c.orig);
        Log("result %d", int(result));
        if (!save)
            g_loaded = true;
        if (open) {
            if (save) {
                CBegin(c.Ptr(side), 0x7777);
                const uint32_t next = 0x12345678;
                CWrite(c.Ptr(side), &next, 4);
                CAscend(c.Ptr(side));
                if (op.c == 8)
                    CAscend(c.Ptr(side));
            } else
                Log("left at %d depth %d", CPos(c.Ptr(side)), CDepth(c.Ptr(side)));
            ChunkClose(side, c);
        }
        if (save && open) {
            const std::string &f = g_files[name];
            std::string hex;
            char t[4];
            for (unsigned char ch : f) {
                std::snprintf(t, sizeof t, "%02x", ch);
                hex += t;
            }
            Log("file %d %s", int(f.size()), hex.c_str());
            g_ownSave[side] = f;
        }
        break;
    }
    default:
        break;
    }
}

struct S_Run {
    std::vector<std::string> log;
};
S_Run g_runs[2];
void TestScenarios()
{
    RandomWorld();
    const S_World start = g_w;
    std::vector<S_Op> ops;
    for (int n = I(1, 14); n > 0; --n)
        ops.push_back(RandomOp());
    std::vector<std::string> files;
    for (const S_Op &op : ops)
        files.push_back(op.kind == OP_LOAD ? MakeLoadFile() : std::string());
    // (The stage changes between operations: the same on both sides.)
    std::vector<S_Stages> stages;
    for (size_t i = 0; i < ops.size(); ++i) {
        RandomWorld();
        stages.push_back(g_w.stages);
    }
    g_w = start;
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_log.clear();
        g_files.clear();
        g_openFiles.clear();
        g_nextFile = 1;
        g_ownSave[side].clear();
        g_loaded = false;
        SetWorld(side);
        for (size_t i = 0; i < ops.size(); ++i) {
            const S_Op &op = ops[i];
            Log("-- %d %d %d %d %s %s %s %s %s", op.kind, op.a, op.b, op.c, FloatText(op.v[0]).c_str(), FloatText(op.v[1]).c_str(),
                FloatText(op.v[2]).c_str(), FloatText(op.v[3]).c_str(), FloatText(op.f).c_str());
            if (op.kind == W_STAGES) {
                g_st = stages[i];
            } else {
                if (op.kind == OP_LOAD)
                    g_files["load.sav"] = files[i];
                Run(side, op);
            }
            g_log.push_back(Snap(side));
        }
        g_runs[side].log.swap(g_log);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

void SetUp()
{
    g_partName = PartName;
    g_findChild = FindNote;
    for (void *&v : g_setVt)
        v = reinterpret_cast<void *>(S_Other);
    g_setVt[0x00 / 4] = reinterpret_cast<void *>(S_Release);
    g_setVt[0x08 / 4] = reinterpret_cast<void *>(S_EndTime);
    g_frameVt[0x20 / 4] = reinterpret_cast<void *>(M_Tick);
    g_frameVt[0x28 / 4] = reinterpret_cast<void *>(M_SetCallback);
    g_frameVt[0x84 / 4] = reinterpret_cast<void *>(M_SetAnimation);
    g_frameVt[0x88 / 4] = reinterpret_cast<void *>(M_GetAnimation);
    g_frameVt[0x8C / 4] = reinterpret_cast<void *>(M_StopAnimation);
    g_frameVt[0x94 / 4] = reinterpret_cast<void *>(M_SetAnimWeight);
    g_frameVt[0x98 / 4] = reinterpret_cast<void *>(M_GetAnimWeight);
    g_frameVt[0x9C / 4] = reinterpret_cast<void *>(M_SetAnimTime);
    g_frameVt[0xA0 / 4] = reinterpret_cast<void *>(M_GetAnimTime);
    g_frameVt[0xAC / 4] = reinterpret_cast<void *>(M_IsAnimActive);
    g_frameVt[0xB0 / 4] = reinterpret_cast<void *>(M_AdvanceStages);
    for (int i = 0; i < 3; ++i) {
        std::memset(g_arena.frames[i], 0, FRAME_BYTES);
        *reinterpret_cast<void ***>(g_arena.frames[i]) = g_frameVt;
    }
    NoteFrame()->m_parent = Model();
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x0041fc10, reinterpret_cast<const void *>(O_CacheGet)},
        {0x007e53a4, reinterpret_cast<const void *>(O_dtaOpen)},
        {0x007e53aa, reinterpret_cast<const void *>(O_dtaOpenWrite)},
        {0x007e53b0, reinterpret_cast<const void *>(O_dtaClose)},
        {0x007e5392, reinterpret_cast<const void *>(O_dtaWrite)},
        {0x007e5398, reinterpret_cast<const void *>(O_dtaSeek)},
        {0x007e539e, reinterpret_cast<const void *>(O_dtaRead)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    g_log.clear();
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("mtmix_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_MTMIX_QUICK") ? 20000 : 60000;
    if (const char *r = std::getenv("HD2_MTMIX_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "MeleeTrackerMixer";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    std::printf("mtmix_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
