// The speech slots, the material tables and the debug lines (part 11b: game/speech_slots.cpp), the engine faked: a logging sound
// frame, a mouth's weights kept, files from memory. MinGW; build_speech.bat; run speech_test.exe ("speech: N checks, 0 failures").
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/car_effects.h"
#include "game/checkpoints.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char text[300];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_log.push_back(text);
}
std::string TakeLog()
{
    std::string all;
    for (const std::string &line : g_log)
        all += line + "\n";
    g_log.clear();
    return all;
}

// ---- files ----
std::map<std::string, std::string> g_files;
std::map<int, std::pair<std::string, size_t>> g_open;
int g_nextHandle = 1;
}  // namespace

int RW_DATA_CALL dtaOpen(const char *name, bool)
{
    if (!g_files.count(name))
        return -1;
    g_open[g_nextHandle] = {name, 0};
    return g_nextHandle++;
}
int RW_DATA_CALL dtaOpenWrite(const char *, int) { return -1; }
bool RW_DATA_CALL dtaClose(int handle) { Log("close"); g_open.erase(handle); return true; }
int RW_DATA_CALL dtaSeek(int handle, int offset, int origin)
{
    auto &f = g_open[handle];
    const size_t size = g_files[f.first].size();
    f.second = static_cast<size_t>((origin == 0 ? 0 : origin == 1 ? static_cast<long>(f.second) : static_cast<long>(size)) + offset);
    return static_cast<int>(f.second);
}
int RW_DATA_CALL dtaRead(int handle, void *data, int size)
{
    auto &f = g_open[handle];
    std::memcpy(data, g_files[f.first].data() + f.second, static_cast<size_t>(size));
    f.second += static_cast<size_t>(size);
    return size;
}
int RW_DATA_CALL dtaWrite(int, const void *, int) { return 0; }
extern "C" void dbgPrintf(const char *format, ...)
{
    char text[200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    Log("dbg %s", text);
}

namespace {

const char *NameOf(const I3D_frame *f) { return f ? f->GetName() : "(null)"; }

// A frame with a mouth (its morph interface at +0x120).
struct S_Weights {
    float w[3][5];
    bool set[3][5];
};
std::map<const void *, S_Weights> g_weights;
void __stdcall F_SetWeight(I3D_morph *m, int channel, int target, float weight)
{
    S_Weights &w = g_weights[m];
    w.w[channel][target] = weight;
    w.set[channel][target] = true;
}
void *g_morphVt[4] = {nullptr, nullptr, reinterpret_cast<void *>(F_SetWeight), nullptr};

uint32_t __stdcall F_Release(I3D_frame *f) { Log("release %s", NameOf(f)); return 0; }
uint32_t __stdcall F_VisualType(I3D_frame *f) { return f->Field<uint32_t>(0x200); }
void __stdcall F_Update(I3D_frame *f) { Log("update %s", NameOf(f)); }
void __stdcall F_SetOn(I3D_frame *f, bool on) { Log("on %s %d", NameOf(f), on); }
void __stdcall F_LinkTo(I3D_frame *f, I3D_frame *p, uint32_t flags) { Log("link %s %s %u", NameOf(f), NameOf(p), flags); }
void __stdcall F_SetWorldDir(I3D_frame *f, const S_vector &d, float roll) { Log("dir %s %g %g %g %g", NameOf(f), d.x, d.y, d.z, roll); }
std::map<std::string, I3D_frame *> g_children;
I3D_frame *__stdcall F_FindChild(I3D_frame *f, const char *name, uint32_t mask)
{
    Log("find %s %s %x", NameOf(f), name, mask);
    const auto it = g_children.find(std::string(NameOf(f)) + "/" + name);
    return it == g_children.end() ? nullptr : it->second;
}
// Sounds.
int g_openResult;
bool g_playing = true;
int g_position, g_length = 5000;
HRESULT __stdcall F_SoundOpen(I3D_sound *, const char *name, uint32_t flags) { Log("open %s %u", name, flags); return g_openResult; }
bool __stdcall F_IsPlaying(I3D_sound *, int unknown) { Log("playing? %d", unknown); return g_playing; }
void __stdcall F_SoundType(I3D_sound *, int type) { Log("type %d", type); }
void __stdcall F_Range(I3D_sound *, float a, float b, float c, float d) { Log("range %g %g %.2f %.2f", a, b, c, d); }
void __stdcall F_OutVolume(I3D_sound *, float v) { Log("out %g", v); }
void __stdcall F_Volume(I3D_sound *, float v) { Log("volume %g", v); }
void __stdcall F_Play(I3D_sound *, int position) { Log("play %d", position); }
int __stdcall F_Position(I3D_sound *) { return g_position; }
int __stdcall F_Length(I3D_sound *) { return g_length; }
void __stdcall F_Active(I3D_sound *, bool on, bool immediate) { Log("active %d %d", on, immediate); }

void *g_frameVt[64];
void *g_soundVt[64];
void BuildTables()
{
    const auto at = [](void **t, int offset, void *f) { t[offset / 4] = f; };
    for (void **t : {g_frameVt, g_soundVt}) {
        at(t, 0x00, reinterpret_cast<void *>(F_Release));
        at(t, 0x04, reinterpret_cast<void *>(F_VisualType));
        at(t, 0x1C, reinterpret_cast<void *>(F_SetWorldDir));
        at(t, 0x24, reinterpret_cast<void *>(F_Update));
        at(t, 0x30, reinterpret_cast<void *>(F_SetOn));
        at(t, 0x38, reinterpret_cast<void *>(F_LinkTo));
        at(t, 0x48, reinterpret_cast<void *>(F_FindChild));
    }
    at(g_soundVt, 0x74, reinterpret_cast<void *>(F_SoundOpen));
    at(g_soundVt, 0x78, reinterpret_cast<void *>(F_IsPlaying));
    at(g_soundVt, 0x7C, reinterpret_cast<void *>(F_SoundType));
    at(g_soundVt, 0x80, reinterpret_cast<void *>(F_Range));
    at(g_soundVt, 0x88, reinterpret_cast<void *>(F_OutVolume));
    at(g_soundVt, 0x8C, reinterpret_cast<void *>(F_Volume));
    at(g_soundVt, 0x94, reinterpret_cast<void *>(F_Play));
    at(g_soundVt, 0xA4, reinterpret_cast<void *>(F_Position));
    at(g_soundVt, 0xA8, reinterpret_cast<void *>(F_Length));
    at(g_soundVt, 0xAC, reinterpret_cast<void *>(F_Active));
}
I3D_frame *NewFrame(const char *name, void **vt = g_frameVt)
{
    auto *buf = static_cast<char *>(_aligned_malloc(0x400, 16));
    std::memset(buf, 0, 0x400);
    auto *f = reinterpret_cast<I3D_frame *>(buf);
    *reinterpret_cast<void ***>(buf) = vt;
    *reinterpret_cast<void ***>(buf + 0x120) = g_morphVt;
    f->m_name = _strdup(name);
    f->m_frameFlags = 0x28;
    return f;
}
// A frame with its mouth "a".
I3D_frame *NewSpeaker(const char *name, bool withHead = false)
{
    I3D_frame *f = NewFrame(name);
    const std::string mouthName = std::string(name) + ".a";
    I3D_frame *mouth = NewFrame(mouthName.c_str());
    mouth->Field<uint32_t>(0x200) = I3DVISUAL_SINGLEMORPH;
    g_children[std::string(name) + "/a"] = mouth;
    if (withHead)
        g_children[mouthName + "/f"] = NewFrame("head");
    return f;
}
I3D_frame *MouthOf(const char *name) { return g_children[std::string(name) + "/a"]; }
const S_Weights &WeightsOf(I3D_frame *mouth) { return g_weights[reinterpret_cast<char *>(mouth) + 0x120]; }
bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

bool IsNeutral(I3D_frame *mouth)
{
    const S_Weights &w = WeightsOf(mouth);
    for (int c = 0; c < 3; ++c) {
        if (w.w[c][0] != 1.0f)
            return false;
        for (int t = 1; t < 5; ++t) {
            if (w.w[c][t] != 0.0f)
                return false;
        }
    }
    return true;
}
// The weights ApplyMouth gives for a pose (written apart: the original's order of factors does not matter here).
void CheckPose(I3D_frame *mouth, float a, float b, float c, float d, float e, float f, int line)
{
    const S_Weights &w = WeightsOf(mouth);
    const bool ok = w.w[0][0] == 0.0f && Near(w.w[0][1], a * b) && Near(w.w[0][2], (1 - b) * a) && Near(w.w[0][3], (1 - c) * (1 - a)) &&
                    Near(w.w[0][4], c * (1 - a)) && w.w[1][0] == 0.0f && Near(w.w[1][1], d * e) && Near(w.w[1][2], (1 - d) * e) &&
                    Near(w.w[1][3], 1 - e) && w.w[2][0] == 0.0f && Near(w.w[2][1], f * e) && Near(w.w[2][2], (1 - f) * e) && Near(w.w[2][3], 1 - e);
    if (!ok)
        std::printf("pose at line %d\n", line);
    CHECK(ok);
}
#define CHECK_POSE(mouth, a, b, c, d, e, f) CheckPose(mouth, a, b, c, d, e, f, __LINE__)

// Lip data: the count, then a byte pattern at +8 for each frame.
std::string LipFile(const std::vector<std::vector<int>> &frames, uint32_t count)
{
    std::string s(reinterpret_cast<const char *>(&count), 4);
    s.append(4, '\0');
    for (const auto &frame : frames) {
        for (int i = 0; i < 8; ++i)
            s += static_cast<char>(i < static_cast<int>(frame.size()) ? frame[static_cast<size_t>(i)] : 0);
    }
    return s;
}

// ---- the world ----
I3D_frame *g_created;
void *g_driverVt[32];
struct S_FakeDriver { void **vtable; } g_driver;
I3D_frame *__stdcall F_CreateFrame(S_FakeDriver *, int type)
{
    Log("create %d", type);
    return g_created = NewFrame("sound", g_soundVt);
}
alignas(16) unsigned char g_gameStorage[0x4000];
alignas(16) unsigned char g_humanStorage[0x800];
}  // namespace

C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameStorage); }
namespace { std::vector<S_Checkpoint *> g_checkpoints; }
S_Checkpoint *C_CheckpointNetwork::Get(int index) { return g_checkpoints[static_cast<size_t>(index)]; }
I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }
void C_QuickMessages::ShowDubbing(int dubbing, int length) { Log("subtitle %d %d", dubbing, length); }
void I3D_frame::UpdateWMatrixProc() {}
S_MaterialDef *g_materials;
uint32_t g_materialCount;
S_CarEffectDef *g_carEffects;
uint32_t g_carEffectCount;

namespace {

C_Human *HumanFor(I3D_frame *frame)
{
    std::memset(g_humanStorage, 0, sizeof g_humanStorage);
    auto *h = reinterpret_cast<C_Human *>(g_humanStorage);
    h->m_frame = frame;
    return h;
}

void Reset()
{
    FreeSpeechSlots();
    g_log.clear();
    g_weights.clear();
    g_files.clear();
    g_openResult = 0;
    g_playing = true;
    g_position = 0;
    g_length = 5000;
}

void TestAttach()
{
    Reset();
    // From the top slot down; all in use: the last.
    CHECK(FindFreeSpeechSlot() == 15);
    I3D_frame *f = NewSpeaker("human");
    C_Human *human = HumanFor(f);
    human->m_unknown2b8 = 0;
    AttachSpeechSlot(15, human);
    CHECK(TakeLog() == "find human a ffff\n");
    CHECK(FindFreeSpeechSlot() == 14);
    // A human's neutral face, then the speech's starting pose set.
    CHECK_POSE(MouthOf("human"), 0.0f, 0.46f, 1.0f, 0.11f, 1.0f, 0.6f);
    const S_Weights &w = WeightsOf(MouthOf("human"));
    CHECK(w.w[1][4] == 0.0f && w.w[2][4] == 0.0f && w.set[1][4] && w.set[2][4]);
    // A frame's: neutral only.
    I3D_frame *other = NewSpeaker("other");
    AttachSpeechSlot(14, other);
    CHECK(IsNeutral(MouthOf("other")) && FindFreeSpeechSlot() == 13);
    // No mouth / the wrong kind: still taken.
    I3D_frame *mute = NewFrame("mute");
    AttachSpeechSlot(13, mute);
    CHECK(FindFreeSpeechSlot() == 12);
    I3D_frame *wrong = NewSpeaker("wrong");
    MouthOf("wrong")->Field<uint32_t>(0x200) = 0x12345678;
    AttachSpeechSlot(12, wrong);
    CHECK(FindFreeSpeechSlot() == 11 && !WeightsOf(MouthOf("wrong")).set[0][0]);
    // Out of range or null: nothing.
    AttachSpeechSlot(-1, mute);
    AttachSpeechSlot(16, mute);
    AttachSpeechSlot(11, static_cast<I3D_frame *>(nullptr));
    AttachSpeechSlot(11, static_cast<C_Human *>(nullptr));
    AttachSpeechSlot(-1, human);
    AttachSpeechSlot(16, human);
    CHECK(FindFreeSpeechSlot() == 11);
    for (int i = 0; i < 16; ++i)
        AttachSpeechSlot(i, mute);
    CHECK(FindFreeSpeechSlot() == 15);
    g_log.clear();
    // Emptied without freeing.
    ClearSpeechSlots();
    CHECK(g_log.empty() && FindFreeSpeechSlot() == 15);
}

void TestPlay()
{
    Reset();
    I3D_frame *f = NewSpeaker("h", true);
    g_files["tables\\dabing\\00000007.dat"] = LipFile({{255, 0, 0, 0, 0, 0}}, 3);
    AttachSpeechSlot(15, HumanFor(f));
    g_log.clear();
    // The whole set-up, with the head and subtitle, the active player's kind.
    PlaySpeechSlot(15, 7, 2.0f, 30.0f, true, true);
    const char *want =
        "close\ncreate 4\nopen sounds\\00000007.wav 0\nsubtitle 7 5000\n"
        "find h.a f ffff\nlink sound head 0\ndir sound 0 0 1 0\ntype 3\nrange 2 30 0.30 0.25\nout 1\nvolume 1\non sound 0\nplay 0\nupdate sound\nactive 1 1\n";
    const std::string got = TakeLog();
    CHECK(got == want);
    if (got != want)
        std::printf("%s", got.c_str());
    CHECK(g_created->m_localMatrix(3).x == 0.0f && g_created->m_localMatrix(3).y == 0.0f && Near(g_created->m_localMatrix(3).z, 0.1f));
    CHECK(g_created->Field<uint32_t>(0x128) == 0x80);
    CHECK(IsHumanSpeaking(HumanFor(f)));
    // Again in the slot: its sound kept, no subtitle, another's kind, no head: at the frame, 1.6 m up.
    g_children.erase("h.a/f");
    g_log.clear();
    PlaySpeechSlot(15, 7, 1.0f, 2.0f, false, false);
    const std::string again = TakeLog();
    CHECK(again.find("create") == std::string::npos && again.find("subtitle") == std::string::npos && again.find("type 1\n") != std::string::npos);
    CHECK(again.find("link sound h 0\n") != std::string::npos);
    CHECK(Near(g_created->m_localMatrix(3).y, 1.6f) && g_created->m_localMatrix(3).z == 0.0f);
    // A locked position stays.
    g_created->m_frameFlags |= FRMFLAGS_POS_LOCKED;
    g_created->m_localMatrix(3).y = 9.0f;
    PlaySpeechSlot(15, 7, 1.0f, 2.0f, false, false);
    CHECK(g_created->m_localMatrix(3).y == 9.0f);
    // No sample: released, the subtitle's length -1, no sound calls; no lip data: told.
    Reset();
    I3D_frame *q = NewFrame("quiet");
    AttachSpeechSlot(3, q);
    g_openResult = 1;
    g_log.clear();
    PlaySpeechSlot(3, 8, 1.0f, 2.0f, true, false);
    CHECK(TakeLog() == "dbg Nenalezen Dabing 00000008.dat\ncreate 4\nopen sounds\\00000008.wav 0\nrelease sound\nsubtitle 8 -1\n");
    g_log.clear();
    PlaySpeechSlot(3, 8, 1.0f, 2.0f, false, false);
    CHECK(TakeLog().find("subtitle") == std::string::npos);
    PlaySpeechSlot(-1, 8, 1.0f, 2.0f, true, false);
    PlaySpeechSlot(16, 8, 1.0f, 2.0f, true, false);
    CHECK(g_log.empty());
    // An empty lip file: closed, no data.
    Reset();
    g_files["tables\\dabing\\00000009.dat"] = "";
    AttachSpeechSlot(4, q);
    g_openResult = 1;
    g_log.clear();
    PlaySpeechSlot(4, 9, 1.0f, 2.0f, false, false);
    CHECK(TakeLog().find("close\n") == 0);
    UpdateSpeechSlots(10);                              // (no data: the mouthless end)
    CHECK(!IsHumanSpeaking(HumanFor(q)));
}

void TestUpdate()
{
    Reset();
    I3D_frame *f = NewSpeaker("s");
    const std::string lip = LipFile({{51, 102, 153, 204, 255, 0}, {255, 255, 255, 255, 255, 255}, {0, 51, 0, 51, 0, 51}, {102, 102, 102, 102, 102, 102}}, 4);
    g_files["tables\\dabing\\00000001.dat"] = lip;
    C_Human *human = HumanFor(f);
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    I3D_frame *mouth = MouthOf("s");
    g_log.clear();
    // The first update: position 0, frame 0, the fade in from the starting pose (0.008 per ms).
    g_position = 0;
    UpdateSpeechSlots(25);
    float fade = 25 * 0.008f;
    CHECK(TakeLog() == "playing? 0\n");
    const auto blend = [&](float from, float to) { return (1 - fade) * from + fade * to; };
    CHECK_POSE(mouth, blend(0.0f, 0.2f), blend(0.46f, 0.4f), blend(1.0f, 0.6f), blend(0.11f, 0.8f), blend(1.0f, 1.0f), blend(0.6f, 0.0f));
    // The fade done (over 1): the frame itself. Frame 1 at 20 ms; 39 ms is still frame 1.
    g_position = 39;
    UpdateSpeechSlots(100);
    CHECK_POSE(mouth, 1, 1, 1, 1, 1, 1);
    g_position = 40;
    UpdateSpeechSlots(1);
    CHECK_POSE(mouth, 0, 0.2f, 0, 0.2f, 0, 0.2f);
    // Past the data: the count's last but one (index count - 2).
    g_position = 4000;
    UpdateSpeechSlots(1);
    CHECK_POSE(mouth, 0, 0.2f, 0, 0.2f, 0, 0.2f);
    // The position back from before the end: goes on. Near the end (time + 300 > length): over.
    g_length = 4300;
    g_position = 3000;
    UpdateSpeechSlots(1);
    CHECK(IsHumanSpeaking(human));
    g_length = 3299;
    g_position = 2999;
    UpdateSpeechSlots(1);
    CHECK(!IsHumanSpeaking(human));                     // (closing: 200 ms)
    // Closing: the last frame's a and b scaled by what's left (a timer of 199 after 1 ms).
    CHECK_POSE(mouth, 0.0f, 0.2f * (199 * 0.005f), 0.0f, 0.2f, 0.0f, 0.2f);
    UpdateSpeechSlots(99);
    CHECK_POSE(mouth, 0.0f, 0.2f * (100 * 0.005f), 0.0f, 0.2f, 0.0f, 0.2f);
    CHECK(FindFreeSpeechSlot() == 14);
    UpdateSpeechSlots(100);                             // (exactly the rest: free)
    CHECK(FindFreeSpeechSlot() == 15);
    // Free: not touched.
    g_weights.clear();
    UpdateSpeechSlots(10);
    CHECK(g_weights.empty());
    // A sound that ended.
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    g_playing = false;
    g_log.clear();
    UpdateSpeechSlots(5);
    CHECK(!IsHumanSpeaking(human));
    g_playing = true;
    // A stop: the sound off at once, the mouth closes in a tick.
    FreeSpeechSlots();
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    AttachSpeechSlot(14, NewFrame("elsewhere"));
    PlaySpeechSlot(14, 1, 1.0f, 2.0f, false, false);
    g_log.clear();
    StopFrameSpeech(NewFrame("nobody"));
    CHECK(g_log.empty());
    StopFrameSpeech(f);
    CHECK(TakeLog() == "active 0 1\n");
    CHECK(!IsHumanSpeaking(human) && FindFreeSpeechSlot() == 13);
    UpdateSpeechSlots(1);
    CHECK(FindFreeSpeechSlot() == 15 || FindFreeSpeechSlot() == 13);
    // No sound: the time counts itself; the end by the data.
    Reset();
    g_files["tables\\dabing\\00000001.dat"] = lip;
    g_openResult = 1;
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    UpdateSpeechSlots(200);
    CHECK_POSE(mouth, 0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.2f);       // (200 ms is past the data: closing, the whole timer gone at once)
    Reset();
    g_files["tables\\dabing\\00000001.dat"] = LipFile({{10, 20, 30, 40, 50, 60}, {255, 255, 255, 255, 255, 255}, {0, 51, 0, 51, 0, 51}, {102, 102, 102, 102, 102, 102}, {1, 1, 1, 1, 1, 1}, {2, 2, 2, 2, 2, 2}}, 6);
    g_openResult = 1;
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    UpdateSpeechSlots(200);                             // (frame 10 of 6: over)
    CHECK(!IsHumanSpeaking(human));
    Reset();
    g_files["tables\\dabing\\00000001.dat"] = LipFile({{0, 0, 0, 0, 0, 0}, {51, 51, 51, 51, 51, 51}, {102, 102, 102, 102, 102, 102}}, 3);
    g_openResult = 1;
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    UpdateSpeechSlots(25);                              // (frame 1 of 3: still on)
    CHECK(IsHumanSpeaking(human));
    UpdateSpeechSlots(14);                              // (39 ms: frame 1)
    CHECK(IsHumanSpeaking(human));
    UpdateSpeechSlots(1);                               // (40 ms: frame 2 = count - 1: over)
    CHECK(!IsHumanSpeaking(human));
}

void TestSlotZeroAndRestart()
{
    Reset();
    I3D_frame *f = NewSpeaker("z");
    C_Human *human = HumanFor(f);
    // The last free one is the first.
    for (int i = 1; i < 16; ++i)
        AttachSpeechSlot(i, NewFrame("x"));
    CHECK(FindFreeSpeechSlot() == 0);
    AttachSpeechSlot(0, human);
    CHECK(FindFreeSpeechSlot() == 15);                  // (all in use)
    FreeSpeechSlots();
    AttachSpeechSlot(0, f);
    CHECK(WeightsOf(MouthOf("z")).set[0][0] && IsNeutral(MouthOf("z")));
    // A frame's slot holds the frame: it speaks, it is stopped, it is released.
    g_files["tables\\dabing\\00000002.dat"] = LipFile({{10, 20, 30, 40, 50, 60}, {70, 80, 90, 100, 110, 120}, {51, 102, 153, 204, 255, 26}, {0}}, 4);
    PlaySpeechSlot(0, 2, 1.0f, 2.0f, false, false);
    CHECK(IsHumanSpeaking(human));
    g_log.clear();
    StopFrameSpeech(f);
    CHECK(TakeLog() == "active 0 1\n" && !IsHumanSpeaking(human));
    // Stopped: the closing timer is 1 ms: gone at the next tick of any length, the last frame's a and b at 0.
    I3D_frame *mouth = MouthOf("z");
    UpdateSpeechSlots(1);
    CHECK_POSE(mouth, 0.0f, 0.0f, 0.6f, 0.8f, 1.0f, 0.102f);
    CHECK(FindFreeSpeechSlot() == 15);
    for (int i = 1; i < 16; ++i)
        AttachSpeechSlot(i, NewFrame("x"));
    CHECK(FindFreeSpeechSlot() == 0);                   // (it is free again)
    FreeSpeechSlots();

    // Closing partly (each value), then a new speech in the slot: from the time 0, timer full, fading from that pose.
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 2, 1.0f, 2.0f, false, false);
    g_playing = false;
    UpdateSpeechSlots(100);                             // (closing: 100 ms left)
    const float k = 100 * 0.005f;
    const float a = 0.2f * k, b = 0.4f * k, c = 0.6f, d = 0.8f, e = 1.0f, g = 0.102f;
    CHECK_POSE(mouth, a, b, c, d, e, g);
    g_playing = true;
    g_position = 0;
    AttachSpeechSlot(15, f);                            // (a frame's: the pose fields stay)
    PlaySpeechSlot(15, 2, 1.0f, 2.0f, false, false);
    UpdateSpeechSlots(25);
    const float t = 25 * 0.008f, r = 1 - t;
    const float fa = 10 * 0.00392157f, fb = 20 * 0.00392157f, fc = 30 * 0.00392157f, fd = 40 * 0.00392157f, fe = 50 * 0.00392157f, ff = 60 * 0.00392157f;
    CHECK_POSE(mouth, r * a + t * fa, r * b + t * fb, r * c + t * fc, r * d + t * fd, r * e + t * fe, r * g + t * ff);
    // The timer full again: ended now, closing takes all its 200 ms (150 + 49 + 1).
    g_playing = false;
    UpdateSpeechSlots(150);
    CHECK(FindFreeSpeechSlot() == 14);
    UpdateSpeechSlots(49);
    CHECK(FindFreeSpeechSlot() == 14);
    UpdateSpeechSlots(1);
    CHECK(FindFreeSpeechSlot() == 15);
    g_playing = true;
    // The time from the sound, not its own (a position equal to the time goes on, near the end or not).
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 2, 1.0f, 2.0f, false, false);
    g_length = 100;
    g_position = 0;
    UpdateSpeechSlots(1);
    CHECK(IsHumanSpeaking(human));
    g_length = 5000;
    FreeSpeechSlots();
}

void TestPauseAndFree()
{
    Reset();
    I3D_frame *f = NewSpeaker("p");
    C_Human *human = HumanFor(f);
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    g_log.clear();
    // Paused: the sound off, its position kept.
    g_position = 777;
    PauseSpeechSlots(true);
    CHECK(TakeLog() == "playing? 0\nactive 0 1\nupdate sound\n");
    g_playing = false;
    PauseSpeechSlots(true);
    CHECK(TakeLog() == "playing? 0\n");
    g_playing = true;
    // Back on: then the sound goes back to that place at the next update, once.
    PauseSpeechSlots(false);
    CHECK(TakeLog() == "on sound 1\nactive 1 1\nupdate sound\n");
    g_position = 1;
    UpdateSpeechSlots(5);
    CHECK(TakeLog() == "playing? 0\nplay 777\n");
    UpdateSpeechSlots(5);
    CHECK(TakeLog() == "playing? 0\n");
    // Stopped ones do not come back.
    StopFrameSpeech(f);
    g_log.clear();
    PauseSpeechSlots(false);
    CHECK(g_log.empty());
    // Freed: the sound released; the slot free. Reset: neutral too. The human's own only.
    FreeSpeechSlots();
    CHECK(TakeLog() == "release sound\n" && FindFreeSpeechSlot() == 15);
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    g_log.clear();
    g_weights.clear();
    ResetSpeechSlots();
    CHECK(TakeLog() == "release sound\n" && IsNeutral(MouthOf("p")) && FindFreeSpeechSlot() == 15);
    AttachSpeechSlot(15, human);
    PlaySpeechSlot(15, 1, 1.0f, 2.0f, false, false);
    I3D_frame *other = NewFrame("other2");
    AttachSpeechSlot(14, other);
    g_log.clear();
    g_weights.clear();
    ReleaseHumanSpeech(human);
    CHECK(TakeLog() == "release sound\n" && IsNeutral(MouthOf("p")) && FindFreeSpeechSlot() == 15);
    // Another's frame stays attached.
    AttachSpeechSlot(15, other);
    CHECK(FindFreeSpeechSlot() == 13);
    FreeSpeechSlots();
    // No frame on the human: nothing done.
    AttachSpeechSlot(15, other);
    ReleaseHumanSpeech(HumanFor(nullptr));
    CHECK(FindFreeSpeechSlot() == 14);
    FreeSpeechSlots();
    // The extra frames kept up to date, when on.
    I3D_frame *a = NewFrame("extraA"), *b = NewFrame("extraB"), *off = NewFrame("extraOff");
    a->m_frameFlags |= 1;
    b->m_frameFlags |= 1;
    C_Human *withExtras = HumanFor(f);
    withExtras->m_unknown2bc = reinterpret_cast<int>(a);
    withExtras->m_unknown2b8 = reinterpret_cast<int>(b);
    AttachSpeechSlot(15, withExtras);
    withExtras->m_unknown2bc = reinterpret_cast<int>(off);
    withExtras->m_unknown2b8 = 0;
    AttachSpeechSlot(14, withExtras);
    a->m_localMatrix(3) = S_vector(4, 5, 6);
    g_log.clear();
    RefreshSpeechSlotFrames();
    CHECK(TakeLog() == "update extraA\nupdate extraB\n");
    CHECK(a->m_localMatrix(3).y == 5.0f && (a->m_frameFlags & FRMFLAGS_CHANGED));
    // A frame's slot clears them, with a mouth.
    AttachSpeechSlot(15, NewSpeaker("newer"));
    g_log.clear();
    RefreshSpeechSlotFrames();
    CHECK(g_log.empty());
    FreeSpeechSlots();
}

// ---- the path's points ----
S_vector4 V4(float x, float y, float z, float w = 0.0f)
{
    S_vector4 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    return v;
}
void AddCheckpoint(float x, float y, float z, int radiusUnits)
{
    auto *c = new S_Checkpoint();
    c->pos = S_vector(x, y, z);
    c->radius = static_cast<uint8_t>(radiusUnits);
    g_checkpoints.push_back(c);
}
bool NearV(const S_vector4 &v, float x, float y, float z) { return std::fabs(v.x - x) < 2e-3f && std::fabs(v.y - y) < 2e-3f && std::fabs(v.z - z) < 2e-3f; }

// The curve the points must follow (written apart from the forward differences): at t of the stretch.
S_vector Hermite(const S_vector &p0, const S_vector &p1, const S_vector &m0, const S_vector &m1, float t)
{
    const float t2 = t * t, t3 = t2 * t;
    const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    return S_vector(h00 * p0.x + h10 * m0.x + h01 * p1.x + h11 * m1.x, h00 * p0.y + h10 * m0.y + h01 * p1.y + h11 * m1.y,
                    h00 * p0.z + h10 * m0.z + h01 * p1.z + h11 * m1.z);
}

void TestPathPoints()
{
    S_Vector4Array out;
    out.data = static_cast<S_vector4 *>(_aligned_malloc(16, 16));
    out.capacity = 1;
    out.count = 1;
    // No network points: one stretch in 10 steps, the old points gone. w: of the ends.
    g_checkpoints.clear();
    BuildPathPoints(V4(1, 2, 3, 7), V4(11, -2, 9, 9), {}, out);
    CHECK(out.count == 11 && out.capacity >= 11);
    {
        const S_vector p0(1, 2, 3), p1(11, -2, 9), m = (p1 - p0) * 0.5f;
        for (int i = 0; i < 10; ++i) {
            const S_vector a = Hermite(p0, p1, m, m, static_cast<float>(i) * 0.1f);
            CHECK(NearV(out.data[i], a.x, a.y, a.z));
        }
    }
    CHECK(out.data[0].w == 7.0f && out.data[5].w == 7.0f && out.data[10].w == 9.0f);
    CHECK(out.data[10].x == 11.0f && out.data[10].y == -2.0f && out.data[10].z == 9.0f);
    // More points than one block of the array: grown, kept.
    CHECK(out.capacity % out.growBy == 0);

    // A point the line runs past outside its radius: passed through, on the line's side, at its radius.
    // from (-10,0,5) to (10,0,5), the point at the origin, radius 2: the path dips to (0,0,2).
    AddCheckpoint(0, 1, 0, 32);
    BuildPathPoints(V4(-10, 0, 5), V4(10, 0, 5), {0}, out);
    CHECK(out.count == 21);
    CHECK(NearV(out.data[0], -10, 0, 5) && NearV(out.data[10], 0, 1, 2) && NearV(out.data[20], 10, 0, 5));
    {
        const S_vector p0(-10, 0, 5), p1(0, 1, 2), p2(10, 0, 5);
        const S_vector m0 = (p1 - p0) * 0.5f, m1 = ((p1 - p0) + (p2 - p1)) * 0.5f, m2 = (p2 - p1) * 0.5f;
        for (int i = 0; i < 10; ++i) {
            const S_vector a = Hermite(p0, p1, m0, m1, static_cast<float>(i) * 0.1f);
            CHECK(NearV(out.data[i], a.x, a.y, a.z));
            const S_vector b = Hermite(p1, p2, m1, m2, static_cast<float>(i) * 0.1f);
            CHECK(NearV(out.data[10 + i], b.x, b.y, b.z));
        }
    }
    // The other side.
    BuildPathPoints(V4(-10, 0, -5), V4(10, 0, -5), {0}, out);
    CHECK(out.count == 21 && NearV(out.data[10], 0, 1, -2));
    // Inside the radius of the line: left out. Exactly at it: passed.
    BuildPathPoints(V4(-10, 0, 1.5f), V4(10, 0, 1.5f), {0}, out);
    CHECK(out.count == 11);
    BuildPathPoints(V4(-10, 0, 2), V4(10, 0, 2), {0}, out);
    CHECK(out.count == 21);
    // An end within its radius: left out.
    BuildPathPoints(V4(-1, 0, 1), V4(10, 0, 5), {0}, out);
    CHECK(out.count == 11);
    BuildPathPoints(V4(-10, 0, 5), V4(1, 0, 1), {0}, out);
    CHECK(out.count == 11);
    // Not between the ends (beyond either): left out.
    BuildPathPoints(V4(5, 0, 5), V4(10, 0, 5), {0}, out);
    CHECK(out.count == 11);
    BuildPathPoints(V4(-10, 0, 5), V4(-5, 0, 5), {0}, out);
    CHECK(out.count == 11);
    // A second point: the line to it, from the point just added. The radius: 1/16 m.
    g_checkpoints.clear();
    AddCheckpoint(0, 0, 0, 16);                         // (1 m)
    AddCheckpoint(10, 0, 0, 16);
    BuildPathPoints(V4(-10, 0, 3), V4(20, 0, 3), {0, 1}, out);
    CHECK(out.count == 31);
    // (The first from the line to the second point; the second from the point just added: worked out by hand.)
    CHECK(NearV(out.data[10], 0.1484f, 0, 0.9889f) && NearV(out.data[20], 9.8992f, 0, 0.9949f) && NearV(out.data[30], 20, 0, 3));
    // Only the second passed: the line from the start, not from the skipped point.
    BuildPathPoints(V4(1, 0, 3), V4(20, 0, 3), {0, 1}, out);
    CHECK(out.count == 21 && NearV(out.data[10], 10, 0, 1));
    // Heights do not matter to the test, only to the point.
    g_checkpoints[0]->pos.y = 50.0f;
    BuildPathPoints(V4(-10, 0, 3), V4(10, 0, 3), {0}, out);
    CHECK(out.count == 21 && NearV(out.data[10], 0, 50, 1));
    // A line of no length past the point (the ends the same): the normal's fallback is not reached by a real path; no crash.
    BuildPathPoints(V4(5, 0, 5), V4(5, 0, 5), {0}, out);
    CHECK(out.count == 11);
    _aligned_free(out.data);
}

void TestTables()
{
    Reset();
    g_files["tables\\hdmaterials.def"] = std::string(0x300, 'm');
    g_files["tables\\effects_car.def"] = std::string(0xF0 * 2, 'c');
    CHECK(LoadMaterialTables());
    CHECK(g_materialCount == 3 && g_carEffectCount == 2 && *reinterpret_cast<char *>(g_materials) == 'm' && *reinterpret_cast<char *>(g_carEffects) == 'c');
    g_files.erase("tables\\effects_car.def");
    CHECK(!LoadMaterialTables());
    g_files.clear();
    CHECK(!LoadMaterialTables());
    // The lines: 15000 at most.
    const float from[3] = {1, 2, 3}, to[3] = {4, 5, 6};
    for (int i = 0; i < 15000; ++i)
        DrawDebugLine(from, to, 0x112233, i == 0 ? 0 : 0x445566);
    CHECK(g_debugLineCount == 15000 && g_debugNext == g_debugVertices + 30000);
    CHECK(g_debugVertices[0].x == 1 && g_debugVertices[0].y == 2 && g_debugVertices[0].z == 3 && g_debugVertices[0].color == 0xFF112233u);
    CHECK(g_debugVertices[1].x == 4 && g_debugVertices[1].y == 5 && g_debugVertices[1].z == 6);
    CHECK(g_debugVertices[1].color == 0xFF112233u && g_debugVertices[3].color == 0xFF445566u);
    DrawDebugLine(from, to, 1, 2);
    CHECK(g_debugLineCount == 15000);
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BuildTables();
    g_driver.vtable = g_driverVt;
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_CreateFrame);
    TestAttach();
    TestPlay();
    TestUpdate();
    TestSlotZeroAndRestart();
    TestPauseAndFree();
    TestTables();
    TestPathPoints();
    std::printf("speech: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
