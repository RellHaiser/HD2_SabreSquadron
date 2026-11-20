// Human part 63: the compass and its radar (game/interface_compass.cpp) against the original's (0x00493680..0x00494950).
// MinGW; build_compass.bat; runs compass_diff.exe ("compass_diff: N checks, 0 failures"). interface_compass.cpp is
// compiled into this file (its namespace's statics reached). Scenarios of operations (Init, Show, Update, ShowSound, the
// radar's Add, Remove and Update, Done; the world changing between them: the active soldier, the humans' answers and
// health, the game's time, difficulty and role) run from the same start on each side, every call outside the compass
// logged by the same fakes, the compass's and the radar's fields and the statics snapped after each operation, the frames
// compared byte for byte at the end. Faked on both sides with the same loggers (stdcall, this on the stack): the frames
// (one arena: Release, SetWorldDir, Update, SetOn, LinkTo, FindChildFrame, Duplicate, SetAlpha; the original's
// UpdateWMatrixProc import 0x007e5296), the scene's FindFrame, the driver's CreateVisual (I3DGetDriver: the original's
// import 0x007e529c), the humans (IsSurrendered, IsUnconscious, GetFacingDir, GetViewDir, GetMoveMode, GetStateFlags,
// IsLeftFootForward: the original's redirected; their role, health and frame in raw blocks), the team's active player
// (0x0045d1d0), the profile's difficulty (0x006b6a20), the game (its time: the original's pointer 0x008ae8f4), RandFloat
// (a sequence; the original's import 0x007e5302), rand (the CRT's, seeded the same: the original's 0x007e6393 to it), the
// original's new and delete (to malloc and free). LS3DF's own S_vector::AngleTo, S_quat::Make and Normalize on both sides;
// the port's AngleBetween2D is the original's (0x00788a10: the human movement's, tested there).
// The cases: Statics (the three statics as loaded), Wobble (UpdateWobble straight), Scenarios (above; also the compass's
// angles and speeds set straight: the wraps at exactly -pi and pi, the speed at 0 and at its limits; directions NaN,
// infinite, 1e-8 long, a hair off an axis; positions 1e-8 apart; the radar's templates at squared lengths of exactly 1e-8
// and 1.5), SoundEdge (the port's fix of a sound's ninth direction, the port's alone; the original's write past the marks
// checked), Forget (the port's own C_Radar::Forget, alone: the human's blips released, the others kept).
// HD2_COMPASS_ROUNDS: scenarios (default 60,000; HD2_COMPASS_QUICK: 8,000; the other cases a twentieth);
// HD2_COMPASS_ULPS: float tolerance (default 0: none needed); HD2_COMPASS_SHOW: a round's two logs printed;
// HD2_COMPASS_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/interface_compass.cpp"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

namespace H {

int checks, failures;
const char *g_case = "";
int g_round;
void Fail(const char *format, ...)
{
    ++failures;
    if (failures > 40)
        return;
    std::printf("FAIL [%s round %d] ", g_case, g_round);
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
}
#define CHECK(cond, ...)               \
    do {                               \
        ++checks;                      \
        if (!(cond))                   \
            Fail(__VA_ARGS__);         \
    } while (0)

std::mt19937 g_rng(63);
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(g_rng) < p; }
float F(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_rng); }
uint32_t U32() { return static_cast<uint32_t>(g_rng()); }
template <class T, size_t N> T Pick(const T (&list)[N]) { return list[I(0, int(N) - 1)]; }
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char text[400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_log.push_back(text);
}

// ---- the engine's fakes: one arena of frames ----
constexpr int FRAMES = 48;
constexpr int FRAME_BYTES = 0x400;
constexpr int VISUAL_FIRST = 24;    // CreateVisual's answers (f24 on, in order)
constexpr int HUMAN_FRAME = 40;     // the humans' frames (f40..f43)
constexpr int HUMANS = 4;
struct alignas(16) S_Arena {
    uint8_t frames[FRAMES][FRAME_BYTES];
};
S_Arena g_arena, g_arenaStart;
void *g_frameVt[64], *g_sceneVt[64], *g_driverVt[32];
struct S_Fake {
    void **vt;
};
S_Fake g_scene{g_sceneVt}, g_driver{g_driverVt};
I3D_frame *FrameAt(int i) { return reinterpret_cast<I3D_frame *>(g_arena.frames[i]); }
int FrameIndex(const void *f)
{
    for (int i = 0; i < FRAMES; ++i)
        if (f == g_arena.frames[i])
            return i;
    return -1;
}

// The humans: raw blocks (role +0x19C, health +0xEC, frame +0x20) and their methods' answers.
alignas(16) uint8_t g_humans[HUMANS][0x800], g_humansStart[HUMANS][0x800];
struct S_HumanAnswers {
    bool surrendered, unconscious, leftFoot;
    S_vector facing, view;
    uint32_t moveMode, state;
};
S_HumanAnswers g_answers[HUMANS], g_answersStart[HUMANS];
int HumanIndex(const void *h)
{
    for (int i = 0; i < HUMANS; ++i)
        if (h == g_humans[i])
            return i;
    return -1;
}
C_Human *HumanAt(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Human *>(g_humans[i]); }

std::string Name(const void *p)
{
    char text[32];
    if (!p)
        return "null";
    if (FrameIndex(p) >= 0)
        std::snprintf(text, sizeof text, "f%d", FrameIndex(p));
    else if (HumanIndex(p) >= 0)
        std::snprintf(text, sizeof text, "h%d", HumanIndex(p));
    else if (p == &g_scene)
        return "scene";
    else if (p == &g_driver)
        return "driver";
    else
        return "?";
    return text;
}
std::string FloatText(float f)
{
    char text[16];
    std::snprintf(text, sizeof text, "f%08x", Bits(f));
    return text;
}
std::string VecText(const S_vector &v) { return FloatText(v.x) + " " + FloatText(v.y) + " " + FloatText(v.z); }

// The round's world.
int g_sceneAnswer[5];   // kul_us, kul_en, kompas, kmpscvr, strelka: a frame or -1
const char *const SCENE_NAMES[5] = {"kul_us", "kul_en", "kompas", "kmpscvr", "strelka"};
int g_nextVisual;
int g_active;
int g_difficulty;
alignas(16) uint8_t g_game[0x100];
uint32_t g_randFloat;

uint32_t __stdcall F_Release(void *f)
{
    Log("%s Release", Name(f).c_str());
    return 0;
}
void __stdcall F_SetWorldDir(void *f, const S_vector &dir, float roll)
{
    Log("%s SetWorldDir %s %s", Name(f).c_str(), VecText(dir).c_str(), FloatText(roll).c_str());
}
void __stdcall F_Update(void *f) { Log("%s Update", Name(f).c_str()); }
void __stdcall F_SetOn(void *f, bool on)
{
    Log("%s SetOn %d", Name(f).c_str(), on);
    auto &flags = reinterpret_cast<I3D_frame *>(f)->m_frameFlags;
    flags = on ? flags | 1u : flags & ~1u;
}
void __stdcall F_LinkTo(void *f, void *parent, uint32_t flags) { Log("%s LinkTo %s %u", Name(f).c_str(), Name(parent).c_str(), flags); }
void *__stdcall F_FindChild(void *f, const char *name, uint32_t mask)
{
    Log("%s FindChildFrame %s %x", Name(f).c_str(), name, mask);
    // (The compass's sound marks: kmps_bl00..07 f5..f12, kmps_rd00..07 f13..f20.)
    int n;
    if (FrameIndex(f) == g_sceneAnswer[2] && std::sscanf(name, "kmps_bl%02d", &n) == 1 && std::strlen(name) == 9 && n >= 0 && n < 8)
        return g_arena.frames[5 + n];
    if (FrameIndex(f) == g_sceneAnswer[2] && std::sscanf(name, "kmps_rd%02d", &n) == 1 && std::strlen(name) == 9 && n >= 0 && n < 8)
        return g_arena.frames[13 + n];
    return nullptr;
}
HRESULT __stdcall F_Duplicate(void *f, void *source)
{
    Log("%s Duplicate %s", Name(f).c_str(), Name(source).c_str());
    return 0;
}
void __stdcall F_SetAlpha(void *f, float alpha)
{
    Log("%s SetAlpha %s", Name(f).c_str(), FloatText(alpha).c_str());
    std::memcpy(static_cast<uint8_t *>(f) + 0x1EC, &alpha, 4);
}
template <int SLOT> void __stdcall F_Other(void *f) { Log("%s slot %x", Name(f).c_str(), SLOT * 4); }
template <int SLOT> void __stdcall SC_Other(void *) { Log("scene slot %x", SLOT * 4); }
template <int SLOT> void __stdcall D_Other(void *) { Log("driver slot %x", SLOT * 4); }
void *__stdcall SC_FindFrame(void *, const char *name, uint32_t mask)
{
    Log("scene FindFrame %s %x", name, mask);
    for (int k = 0; k < 5; ++k)
        if (std::strcmp(name, SCENE_NAMES[k]) == 0)
            return g_sceneAnswer[k] < 0 ? nullptr : g_arena.frames[g_sceneAnswer[k]];
    return nullptr;
}
void *__stdcall D_CreateVisual(void *, uint32_t type)
{
    Log("driver CreateVisual %x -> f%d", type, g_nextVisual);
    return g_arena.frames[g_nextVisual++];
}
// LS3DF's (thiscall): logged, the matrix taken as made.
void __attribute__((thiscall)) O_UpdateWMatrix(void *f)
{
    Log("%s UpdateWMatrixProc", Name(f).c_str());
    reinterpret_cast<I3D_frame *>(f)->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
}
void *__cdecl O_GetDriver() { return &g_driver; }
float NextRandFloat()
{
    g_randFloat = g_randFloat * 1664525u + 1013904223u;
    const float r = static_cast<float>(g_randFloat >> 8) * (1.0f / 16777216.0f);
    Log("RandFloat %s", FloatText(r).c_str());
    return r;
}
float __cdecl O_RandFloat() { return NextRandFloat(); }
int __cdecl O_Rand() { return std::rand(); }
void *__cdecl O_New(size_t size) { return std::malloc(size); }
void __cdecl O_Delete(void *p) { std::free(p); }

// The humans' methods (both sides).
const S_HumanAnswers &A(const void *h) { return g_answers[HumanIndex(h)]; }
bool HIsSurrendered(const void *h)
{
    Log("h%d IsSurrendered", HumanIndex(h));
    return A(h).surrendered;
}
bool HIsUnconscious(const void *h)
{
    Log("h%d IsUnconscious", HumanIndex(h));
    return A(h).unconscious;
}
const S_vector &HGetFacingDir(const void *h)
{
    Log("h%d GetFacingDir", HumanIndex(h));
    return A(h).facing;
}
const S_vector &HGetViewDir(const void *h)
{
    Log("h%d GetViewDir", HumanIndex(h));
    return A(h).view;
}
uint32_t HGetMoveMode(const void *h)
{
    Log("h%d GetMoveMode", HumanIndex(h));
    return A(h).moveMode;
}
uint32_t HGetStateFlags(const void *h)
{
    Log("h%d GetStateFlags", HumanIndex(h));
    return A(h).state;
}
bool HIsLeftFootForward(const void *h)
{
    Log("h%d IsLeftFootForward", HumanIndex(h));
    return A(h).leftFoot;
}
bool __attribute__((thiscall)) O_IsSurrendered(void *h) { return HIsSurrendered(h); }
bool __attribute__((thiscall)) O_IsUnconscious(void *h) { return HIsUnconscious(h); }
const S_vector *__attribute__((thiscall)) O_GetFacingDir(void *h) { return &HGetFacingDir(h); }
const S_vector *__attribute__((thiscall)) O_GetViewDir(void *h) { return &HGetViewDir(h); }
uint32_t __attribute__((thiscall)) O_GetMoveMode(void *h) { return HGetMoveMode(h); }
uint32_t __attribute__((thiscall)) O_GetStateFlags(void *h) { return HGetStateFlags(h); }
bool __attribute__((thiscall)) O_IsLeftFootForward(void *h) { return HIsLeftFootForward(h); }
// (Getters: not logged.)
void *__attribute__((thiscall)) O_GetActivePlayer(void *) { return HumanAt(g_active); }
int __attribute__((thiscall)) O_GetDifficulty(void *) { return g_difficulty; }

// ---- the original's ----
uint32_t &OrigRole() { return *OriginalAt<uint32_t *>(0x00982100); }
float &OrigDecay() { return *OriginalAt<float *>(0x0089bc5c); }
float &OrigUnused() { return *OriginalAt<float *>(0x0089bc58); }
int &OrigSkip() { return *OriginalAt<int *>(0x0089bc64); }
using T_Init = bool(__attribute__((thiscall)) *)(void *, void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Ptr = void(__attribute__((thiscall)) *)(void *, void *);
using T_Sound = void(__attribute__((thiscall)) *)(void *, const S_vector *, int);
using T_AngleBetween2D = float(__attribute__((fastcall)) *)(const float *, const float *);
constexpr uint32_t COMPASS_BYTES = 0xA4;

}  // namespace H

// ---- the port's side of the fakes ----
void I3D_frame::UpdateWMatrixProc() { H::O_UpdateWMatrix(this); }
namespace {
FARPROC Ls3df(const char *name)
{
    const FARPROC p = GetProcAddress(GetModuleHandleA("LS3DF.dll"), name);
    if (!p) {
        std::printf("compass_diff: %s not in LS3DF.dll\n", name);
        std::exit(2);
    }
    return p;
}
}  // namespace
void __stdcall S_quat::Make(const S_matrix &m)
{
    using T = void(__stdcall *)(S_quat *, const S_matrix &);
    static const T make = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?Make@S_quat@@QAGXABUS_matrix@@@Z")));
    make(this, m);
}
void __stdcall S_quat::Normalize()
{
    using T = void(__stdcall *)(S_quat *);
    static const T normalize = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?Normalize@S_quat@@QAGXXZ")));
    normalize(this);
}
float __stdcall S_vector::AngleTo(const S_vector &v) const
{
    using T = float(__stdcall *)(const S_vector *, const S_vector &);
    static const T angleTo = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?AngleTo@S_vector@@QBGMABU1@@Z")));
    return angleTo(this, v);
}
extern "C" float RandFloat() { return H::NextRandFloat(); }
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&H::g_driver); }
float AngleBetween2D(const float *a, const float *b) { return OriginalAt<H::T_AngleBetween2D>(0x00788a10)(a, b); }
C_Human *C_Team::GetActivePlayer() const { return H::HumanAt(H::g_active); }
int C_GameProfile::GetDifficulty() const { return H::g_difficulty; }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_game); }
bool C_Human::IsSurrendered() const { return H::HIsSurrendered(this); }
bool C_Human::IsUnconscious() const { return H::HIsUnconscious(this); }
const S_vector &C_Human::GetFacingDir() const { return H::HGetFacingDir(this); }
const S_vector &C_Human::GetViewDir() const { return H::HGetViewDir(this); }
uint32_t C_Human::GetMoveMode() const { return H::HGetMoveMode(this); }
uint32_t C_Human::GetStateFlags() const { return H::HGetStateFlags(this); }
bool C_Human::IsLeftFootForward() const { return H::HIsLeftFootForward(this); }

// (g++ lays the human out its own way: its role, health and frame written at both sides' offsets, apart.)
constexpr uint32_t PORT_ROLE = offsetof(C_Human, m_role), PORT_HEALTH = offsetof(C_Human, m_stats), PORT_FRAME = offsetof(C_Actor, m_frame);
static_assert(sizeof(C_Human) <= 0x800, "human block");
static_assert(PORT_ROLE + 4 <= 0x20 || PORT_ROLE >= 0x1A0 || (PORT_ROLE >= 0x24 && PORT_ROLE + 4 <= 0xEC) || (PORT_ROLE >= 0xF0 && PORT_ROLE + 4 <= 0x19C), "role apart");
static_assert(PORT_HEALTH + 4 <= 0x20 || PORT_HEALTH >= 0x1A0 || (PORT_HEALTH >= 0x24 && PORT_HEALTH + 4 <= 0xEC) || (PORT_HEALTH >= 0xF0 && PORT_HEALTH + 4 <= 0x19C) || PORT_HEALTH == 0xEC, "health apart");
static_assert(PORT_FRAME == 0x20 || PORT_FRAME + 4 <= 0x20 || PORT_FRAME >= 0x1A0 || (PORT_FRAME >= 0x24 && PORT_FRAME + 4 <= 0xEC) || (PORT_FRAME >= 0xF0 && PORT_FRAME + 4 <= 0x19C), "frame apart");
static_assert(offsetof(C_game, m_time) == 0x2C, "time");
static_assert(offsetof(C_Compass, m_soundTimes) == 0x60 && offsetof(C_Compass, m_leftFoot) == 0xA0, "compass");
static_assert(offsetof(S_RadarBlip, alpha) == 0x0C, "blip");

namespace H {

// ---- the scenarios ----
enum E_Op { OP_INIT, OP_DONE, OP_SHOW, OP_UPDATE, OP_SOUND, OP_ADD, OP_REMOVE, OP_RADAR_UPDATE, OP_ACTIVE, OP_HUMAN, OP_TIME, OP_GAME, OP_STATE };
const char *const OP_NAMES[] = {"Init", "Done", "Show", "Update", "ShowSound", "Add", "Remove", "RadarUpdate", "Active", "Human", "Time", "Game", "State"};
struct S_Op {
    E_Op kind;
    int a = 0, b = 0;
    S_vector v;
    S_HumanAnswers answers{};
    int health = 0, role = 0;
    S_vector pos;
    bool posValid = false;
    float state[4] = {};   // OP_STATE: the heading, needle, wobble, needle speed
};
struct S_Scenario {
    std::vector<S_Op> ops;
    uint32_t seed;
    bool leftFoot;
    float decay, unused;
    int skip;
    int active, difficulty;
    uint32_t role, time;
};

// The direction ShowSound's port computes (its float steps; the original's x87 rounds the same): 8 is the original's
// flaw (past the marks), < 0 a NaN's (both write far away).
int SoundDirection(const S_vector &dir)
{
    float flat[2] = {dir.x, dir.z};
    const float length = std::sqrt(flat[1] * flat[1] + flat[0] * flat[0]);
    if (length > 1e-8f) {
        const float k = 1.0f / length;
        flat[0] *= k;
        flat[1] = k * flat[1];
    }
    const float north[2] = {0.0f, 1.0f};
    float angle = OriginalAt<T_AngleBetween2D>(0x00788a10)(flat, north);
    if (angle < 0.0f)
        angle += 6.28318548f;
    // (The original's __ftol: through 64 bits, the low word; a NaN's "indefinite" value gives 0.)
    const float d = angle * 1.27323949f;
    if (d != d)
        return 0;
    if (!(d > -1.0f && d < 2147483520.0f))
        return -1;
    return static_cast<int>(d);
}
S_vector RandomSoundDir()
{
    for (;;) {
        S_vector v;
        switch (I(0, 5)) {
        case 0:   // near north, either side
            v = S_vector(P(0.5) ? F(-1e-4f, 1e-4f) : Pick({-1e-7f, 1e-7f, -1e-30f, 0.0f}), F(-1, 1), P(0.8) ? 1.0f : F(0.1f, 3));
            break;
        case 1:   // the eight borders
        {
            const float a = I(0, 7) * 0.785398163f + (P(0.5) ? 0.0f : P(0.5) ? F(-1e-6f, 1e-6f) : F(-1e-5f, 1e-5f));
            v = S_vector(std::sin(a) * F(0.5f, 2), F(-1, 1), std::cos(a) * F(0.5f, 2));
            break;
        }
        case 2:
            v = S_vector(F(-50, 50), F(-50, 50), F(-50, 50));
            break;
        case 3:   // tiny (not normalized below 1e-8), zero, NaN
            v = P(0.6) ? S_vector(F(-1e-9f, 1e-9f), 0.0f, F(-1e-9f, 1e-9f))
                       : Pick({S_vector(0, 0, 0), S_vector(NAN, 0, 1), S_vector(0, 1, -NAN), S_vector(INFINITY, 0, 1)});
            break;
        default:
            v = S_vector(F(-1, 1), F(-1, 1), F(-1, 1));
            break;
        }
        const int d = SoundDirection(v);
        if (d >= 0 && d <= 7)
            return v;
    }
}
S_vector RandomDir()
{
    if (P(0.05))
        return S_vector(0, 0, 0);
    if (P(0.03))
        return S_vector(Pick({NAN, 0.0f, 1.0f}), F(-1, 1), Pick({NAN, INFINITY, -INFINITY, 0.5f}));
    if (P(0.05))
        return S_vector(Pick({1e-8f, -1e-8f}), F(-1, 1), 0.0f);
    if (P(0.1))
        return S_vector(Pick({1e-4f, -3e-4f, 1e-6f, 2e-3f}) * F(0.5f, 2), F(-1, 1), Pick({1.0f, -1.0f, 0.9999999f, -0.99999994f}));
    if (P(0.1))
        return S_vector(Pick({-1.0f, 0.0f, 1.0f}), F(-1, 1), Pick({-1.0f, 0.0f, 1.0f}));
    return S_vector(F(-1, 1), F(-1, 1), F(-1, 1));
}
S_HumanAnswers RandomAnswers()
{
    S_HumanAnswers a;
    a.surrendered = P(0.15);
    a.unconscious = P(0.15);
    a.leftFoot = P(0.5);
    a.facing = RandomDir();
    a.view = RandomDir();
    a.moveMode = P(0.3) ? 0 : Pick({1u, 2u, 3u, 0x10u, U32()});
    a.state = Pick({0u, 0x2000u, 0x4000u, 0x10000u, 0x6000u, 0x12000u, 0x14000u, U32()});
    return a;
}
int RandomDelta() { return P(0.6) ? Pick({0, 1, 10, 16, 33, 50, 100, 250, 499, 500, 501, 1000}) : P(0.9) ? I(0, 700) : I(-40, -1); }

void SetRole(int h, uint32_t role)
{
    *reinterpret_cast<uint32_t *>(g_humans[h] + 0x19C) = role;
    *reinterpret_cast<uint32_t *>(g_humans[h] + PORT_ROLE) = role;
}
void SetHealth(int h, int health)
{
    *reinterpret_cast<int *>(g_humans[h] + 0xEC) = health;
    *reinterpret_cast<int *>(g_humans[h] + PORT_HEALTH) = health;
}

// Two floats whose squares sum to exactly 1e-8f (0x322bcc77; no single float's square does).
float FromBits(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
const float EDGE_X = FromBits(0x38944a82u), EDGE_Y = FromBits(0x38944a81u);

void RandomFrame(int i)
{
    std::memset(g_arena.frames[i], 0, FRAME_BYTES);
    *reinterpret_cast<void ***>(g_arena.frames[i]) = g_frameVt;
    I3D_frame *f = FrameAt(i);
    f->m_frameFlags = (P(0.6) ? 1u : 0u) | (P(0.7) ? uint32_t(FRMFLAGS_WMATRIX_VALID) : 0u) | (P(0.15) ? uint32_t(FRMFLAGS_POS_LOCKED) : 0u) |
                      (P(0.5) ? uint32_t(FRMFLAGS_ROT_DIRTY) : 0u) | (U32() & 0x0F00);
    for (int r = 0; r < 4; ++r) {
        f->m_localMatrix(r) = S_vector(F(-2, 2), F(-2, 2), F(-2, 2));
        f->m_worldMatrix(r) = S_vector(F(-20, 20), F(-2, 2), F(-20, 20));
    }
    f->m_rot.v = S_vector(F(-1, 1), F(-1, 1), F(-1, 1));
    f->m_rot.s = F(-1, 1);
    f->m_parent = P(0.8) ? FrameAt(I(21, 23)) : nullptr;
    if (i < 2 && P(0.3))
        f->m_localMatrix(3) = Pick({S_vector(EDGE_X, EDGE_Y, 0), S_vector(EDGE_X, EDGE_Y, 0), S_vector(1.0f, 0.5f, 0.5f), S_vector(0, 0, 0),
                                    S_vector(1, 0, 0), S_vector(-NAN, 0, 0), S_vector(-NAN, 0, 0)});
    const float alpha = P(0.3) ? Pick({0.0f, 1.0f, 0.5f, 1.5f, -0.1f, 0.0005f, 0.9995f}) : F(0, 1);
    std::memcpy(g_arena.frames[i] + 0x1EC, &alpha, 4);
}

S_Scenario RandomScenario()
{
    S_Scenario s;
    s.seed = U32();
    s.leftFoot = P(0.5);
    s.decay = P(0.5) ? 1.0f : F(0, 0.001f);
    s.unused = F(-1, 1);
    s.skip = P(0.6) ? 0 : I(0, 6);
    s.active = P(0.85) ? I(0, HUMANS - 1) : -1;
    s.difficulty = I(1, 4);
    s.role = P(0.8) ? 7u : Pick({0u, 1u, 3u, 5u, 6u, 0xFu, 0x17u});
    s.time = P(0.5) ? I(0, 100000) : U32();
    for (int k = 0; k < 5; ++k)
        g_sceneAnswer[k] = k < 2 && P(0.1) ? -1 : k;
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    for (int h = 0; h < HUMANS; ++h) {
        std::memset(g_humans[h], 0, sizeof g_humans[h]);
        SetRole(h, P(0.4) ? 2u : Pick({0u, 1u, 3u, 4u}));
        SetHealth(h, P(0.7) ? I(1, 100) : Pick({0, -5, 1}));
        *reinterpret_cast<void **>(g_humans[h] + 0x20) = FrameAt(HUMAN_FRAME + h);
        *reinterpret_cast<void **>(g_humans[h] + PORT_FRAME) = FrameAt(HUMAN_FRAME + h);
        g_answers[h] = RandomAnswers();
    }
    // The ops: Init first; up to 20 adds (CreateVisual's frames).
    bool alive = false;
    int adds = 0;
    const int count = I(1, 16);
    for (int n = 0; n < count; ++n) {
        S_Op op;
        if (!alive) {
            op.kind = n == 0 || P(0.5) ? OP_INIT : Pick({OP_SHOW, OP_ACTIVE, OP_TIME});
        } else {
            op.kind = static_cast<E_Op>(Pick({1, 2, 3, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12}));
            if (op.kind == OP_DONE && P(0.7))
                op.kind = OP_UPDATE;
            if (op.kind == OP_ADD && adds >= 20)
                op.kind = OP_UPDATE;
        }
        switch (op.kind) {
        case OP_INIT:
            alive = true;
            break;
        case OP_DONE:
            alive = false;
            break;
        case OP_SHOW:
            op.a = P(0.5);
            break;
        case OP_UPDATE:
        case OP_RADAR_UPDATE:
            op.a = RandomDelta();
            break;
        case OP_SOUND:
            op.v = RandomSoundDir();
            op.a = P(0.5) ? 0 : Pick({1, 2, 5, -1, 100});
            break;
        case OP_ADD:
            ++adds;
            op.a = P(0.9) ? I(0, HUMANS - 1) : -1;
            break;
        case OP_REMOVE:
            op.a = P(0.9) ? I(0, HUMANS - 1) : -1;
            break;
        case OP_ACTIVE:
            op.a = P(0.85) ? I(0, HUMANS - 1) : -1;
            break;
        case OP_HUMAN:
            op.a = I(0, HUMANS - 1);
            op.answers = g_answers[op.a];
            // (Mostly the foot changing: the wobble's kick.)
            if (P(0.6))
                op.answers.leftFoot = !op.answers.leftFoot;
            if (P(0.3))
                op.answers = RandomAnswers();
            op.health = P(0.7) ? I(1, 100) : Pick({0, -5, 1});
            op.role = P(0.4) ? 2 : Pick({0, 1, 3, 4});
            op.pos = P(0.25) ? S_vector(Pick({0.0f, 1e-8f, -1e-8f}), F(-2, 2), Pick({0.0f, 1e-8f}))
                             : S_vector(F(-20, 20), F(-2, 2), F(-20, 20));
            op.posValid = P(0.7);
            break;
        case OP_TIME:
            op.a = static_cast<int>(P(0.5) ? I(0, 100000) : U32());
            break;
        case OP_STATE:
        {
            // (The wraps at exactly -pi and pi, the heading on the needle, the speed at its limits.)
            const float pi = 3.14159274f;
            const float angles[] = {0.0f, pi, -pi, 1.0f, -2.5f, 3.1415925f, -3.1415925f};
            op.state[0] = P(0.7) ? Pick(angles) : F(-3.2f, 3.2f);
            op.state[1] = P(0.3) ? op.state[0] : P(0.7) ? Pick(angles) : F(-3.2f, 3.2f);
            op.state[2] = P(0.5) ? 0.0f : F(0, 0.2f);
            op.state[3] = P(0.4) ? 0.0f : P(0.6) ? Pick({0.35f, -0.35f, 0.3499999f, -0.3500001f, 1e-30f}) : F(-0.4f, 0.4f);
            op.b = P(0.5) ? 0 : P(0.5) ? I(-40, -1) : RandomDelta();   // an Update straight after (0: none)
            op.a = P(0.5) ? -1 : I(0, HUMANS - 1);   // the active soldier (none: the heading stays)
            break;
        }
        case OP_GAME:
            op.a = I(1, 4);
            op.b = static_cast<int>(P(0.8) ? 7u : Pick({0u, 1u, 3u, 5u, 6u, 0xFu}));
            break;
        }
        s.ops.push_back(op);
    }
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(g_humansStart, g_humans, sizeof g_humans);
    std::memcpy(g_answersStart, g_answers, sizeof g_answers);
    return s;
}

// A side's snap of the compass, its radar and the statics.
std::string SnapPort(const C_Compass &c)
{
    std::string s = "compass " + FloatText(c.m_heading) + " " + FloatText(c.m_needle) + " " + FloatText(c.m_wobble) + " " +
                    FloatText(c.m_needleSpeed) + " " + Name(c.m_frame) + " " + Name(c.m_card) + " " + Name(c.m_arrow) + " radar " +
                    (c.m_radar ? "1" : "0") + " left " + std::to_string(int(c.m_leftFoot));
    for (int d = 0; d < 8; ++d)
        for (int k = 0; k < 2; ++k)
            s += " " + Name(c.m_soundMarks[d][k]) + ":" + std::to_string(c.m_soundTimes[d][k]);
    if (c.m_radar) {
        s += " | " + Name(c.m_radar->m_ally) + " " + Name(c.m_radar->m_enemy) + " n " + std::to_string(c.m_radar->m_blips.size());
        for (const S_RadarBlip *b : c.m_radar->m_blips)
            s += " [" + Name(b->visual) + " " + Name(b->human) + " " + FloatText(b->radius) + " " + FloatText(b->alpha) + "]";
    }
    s += " | statics " + FloatText(s_wobbleDecay) + " " + FloatText(s_unused89bc58) + " " + std::to_string(s_wobbleSkip);
    return s;
}
std::string SnapOrig(const uint8_t *c)
{
    const auto f = [&](uint32_t o) { return *reinterpret_cast<const float *>(c + o); };
    const auto p = [&](const uint8_t *b, uint32_t o) { return *reinterpret_cast<void *const *>(b + o); };
    const uint8_t *radar = static_cast<const uint8_t *>(p(c, 0x1C));
    std::string s = "compass " + FloatText(f(0)) + " " + FloatText(f(4)) + " " + FloatText(f(8)) + " " + FloatText(f(0xC)) + " " +
                    Name(p(c, 0x10)) + " " + Name(p(c, 0x14)) + " " + Name(p(c, 0x18)) + " radar " + (radar ? "1" : "0") + " left " +
                    std::to_string(int(c[0xA0]));
    for (int d = 0; d < 8; ++d)
        for (int k = 0; k < 2; ++k)
            s += " " + Name(p(c, 0x20 + (d * 2 + k) * 4)) + ":" + std::to_string(*reinterpret_cast<const int *>(c + 0x60 + (d * 2 + k) * 4));
    if (radar) {
        void *const *first = static_cast<void *const *>(p(radar, 0xC));
        void *const *last = static_cast<void *const *>(p(radar, 0x10));
        s += " | " + Name(p(radar, 0)) + " " + Name(p(radar, 4)) + " n " + std::to_string(first ? last - first : 0);
        for (void *const *b = first; b && b < last; ++b) {
            const uint8_t *blip = static_cast<const uint8_t *>(*b);
            s += " [" + Name(p(blip, 0)) + " " + Name(p(blip, 4)) + " " + FloatText(*reinterpret_cast<const float *>(blip + 8)) + " " +
                 FloatText(*reinterpret_cast<const float *>(blip + 0xC)) + "]";
        }
    }
    s += " | statics " + FloatText(OrigDecay()) + " " + FloatText(OrigUnused()) + " " + std::to_string(OrigSkip());
    return s;
}

// A side's run of a scenario: its log (the calls and the snaps) and the arena after it.
struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_portRun, g_origRun;

void ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case OP_ACTIVE:
        g_active = op.a;
        break;
    case OP_HUMAN:
        g_answers[op.a] = op.answers;
        SetHealth(op.a, op.health);
        SetRole(op.a, uint32_t(op.role));
        FrameAt(HUMAN_FRAME + op.a)->m_worldMatrix(3) = op.pos;
        if (op.posValid)
            FrameAt(HUMAN_FRAME + op.a)->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
        else
            FrameAt(HUMAN_FRAME + op.a)->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        break;
    case OP_TIME:
        *reinterpret_cast<uint32_t *>(g_game + 0x2C) = uint32_t(op.a);
        break;
    case OP_GAME:
        g_difficulty = op.a;
        g_gameRole = uint32_t(op.b);
        OrigRole() = uint32_t(op.b);
        break;
    default:
        break;
    }
}
void StartSide(const S_Scenario &s)
{
    std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
    std::memcpy(g_humans, g_humansStart, sizeof g_humans);
    std::memcpy(g_answers, g_answersStart, sizeof g_answers);
    g_log.clear();
    g_nextVisual = VISUAL_FIRST;
    g_active = s.active;
    g_difficulty = s.difficulty;
    g_gameRole = s.role;
    OrigRole() = s.role;
    std::memset(g_game, 0, sizeof g_game);
    *reinterpret_cast<uint32_t *>(g_game + 0x2C) = s.time;
    g_randFloat = s.seed;
    std::srand(s.seed);
}

void RunPort(const S_Scenario &s)
{
    StartSide(s);
    s_wobbleDecay = s.decay;
    s_unused89bc58 = s.unused;
    s_wobbleSkip = s.skip;
    C_Compass *c = new C_Compass;
    c->m_leftFoot = s.leftFoot;
    bool alive = false;
    for (const S_Op &op : s.ops) {
        Log("-- %s %d %d", OP_NAMES[op.kind], op.a, op.b);
        switch (op.kind) {
        case OP_INIT:
            Log("Init -> %d", c->Init(reinterpret_cast<I3D_scene *>(&g_scene)));
            alive = true;
            break;
        case OP_DONE:
            c->Done();
            alive = false;
            break;
        case OP_SHOW:
            c->Show(op.a != 0);
            break;
        case OP_UPDATE:
            c->Update(op.a);
            break;
        case OP_STATE:
            c->m_heading = op.state[0];
            c->m_needle = op.state[1];
            c->m_wobble = op.state[2];
            c->m_needleSpeed = op.state[3];
            g_active = op.a;
            if (op.b != 0)
                c->Update(op.b);
            break;
        case OP_SOUND:
            c->ShowSound(op.v, op.a);
            break;
        case OP_ADD:
            c->m_radar->Add(HumanAt(op.a));
            break;
        case OP_REMOVE:
            c->m_radar->Remove(HumanAt(op.a));
            break;
        case OP_RADAR_UPDATE:
            c->m_radar->Update(op.a);
            break;
        default:
            ApplyWorld(op);
            break;
        }
        g_log.push_back(SnapPort(*c));
    }
    if (alive) {
        Log("-- end");
        c->Done();
    }
    delete c;
    g_portRun.log.swap(g_log);
    std::memcpy(&g_portRun.arena, &g_arena, sizeof g_arena);
}
void RunOrig(const S_Scenario &s)
{
    StartSide(s);
    OrigDecay() = s.decay;
    OrigUnused() = s.unused;
    OrigSkip() = s.skip;
    // (Room past the compass: the original's flaw writes the ninth direction's red mark there.)
    uint8_t *c = static_cast<uint8_t *>(std::calloc(1, 0x100));
    c[0xA0] = s.leftFoot;
    bool alive = false;
    const auto radar = [&] { return *reinterpret_cast<void **>(c + 0x1C); };
    for (const S_Op &op : s.ops) {
        Log("-- %s %d %d", OP_NAMES[op.kind], op.a, op.b);
        switch (op.kind) {
        case OP_INIT:
            Log("Init -> %d", OriginalAt<T_Init>(0x00493f80)(c, &g_scene));
            alive = true;
            break;
        case OP_DONE:
            OriginalAt<T_Void>(0x00494140)(c);
            alive = false;
            break;
        case OP_SHOW:
            OriginalAt<T_Bool>(0x00494170)(c, op.a != 0);
            break;
        case OP_UPDATE:
            OriginalAt<T_Int>(0x00494190)(c, op.a);
            break;
        case OP_STATE:
            std::memcpy(c, &op.state[0], 4);
            std::memcpy(c + 4, &op.state[1], 4);
            std::memcpy(c + 8, &op.state[2], 4);
            std::memcpy(c + 0xC, &op.state[3], 4);
            g_active = op.a;
            if (op.b != 0)
                OriginalAt<T_Int>(0x00494190)(c, op.b);
            break;
        case OP_SOUND:
            OriginalAt<T_Sound>(0x004947e0)(c, &op.v, op.a);
            break;
        case OP_ADD:
            OriginalAt<T_Ptr>(0x00493d10)(radar(), HumanAt(op.a));
            break;
        case OP_REMOVE:
            OriginalAt<T_Ptr>(0x00493f40)(radar(), HumanAt(op.a));
            break;
        case OP_RADAR_UPDATE:
            OriginalAt<T_Int>(0x00493c60)(radar(), op.a);
            break;
        default:
            ApplyWorld(op);
            break;
        }
        g_log.push_back(SnapOrig(c));
    }
    if (alive) {
        Log("-- end");
        OriginalAt<T_Void>(0x00494140)(c);
    }
    std::free(c);
    g_origRun.log.swap(g_log);
    std::memcpy(&g_origRun.arena, &g_arena, sizeof g_arena);
}

// Lines equal but for the float tokens ("f" + 8 hex digits), within HD2_COMPASS_ULPS.
int g_ulps;
bool FloatToken(const std::string &t, uint32_t &bits)
{
    if (t.size() != 9 || t[0] != 'f')
        return false;
    for (size_t k = 1; k < 9; ++k)
        if (!std::isxdigit(static_cast<unsigned char>(t[k])))
            return false;
    bits = uint32_t(std::strtoul(t.c_str() + 1, nullptr, 16));
    return true;
}
bool SameLine(const std::string &a, const std::string &b)
{
    if (a == b)
        return true;
    if (g_ulps == 0)
        return false;
    size_t i = 0, j = 0;
    while (i <= a.size() && j <= b.size()) {
        const size_t ie = std::min(a.find(' ', i), a.size()), je = std::min(b.find(' ', j), b.size());
        const std::string ta = a.substr(i, ie - i), tb = b.substr(j, je - j);
        uint32_t x, y;
        if (ta != tb) {
            if (!FloatToken(ta, x) || !FloatToken(tb, y))
                return false;
            const int64_t sx = (x & 0x80000000u) ? -int64_t(x & 0x7FFFFFFFu) : int64_t(x);
            const int64_t sy = (y & 0x80000000u) ? -int64_t(y & 0x7FFFFFFFu) : int64_t(y);
            if (std::llabs(sx - sy) > g_ulps)
                return false;
        }
        if (ie == a.size() || je == b.size())
            return ie == a.size() && je == b.size();
        i = ie + 1;
        j = je + 1;
    }
    return false;
}

void Compare()
{
    const auto &o = g_origRun.log, &p = g_portRun.log;
    size_t k = 0;
    for (; k < o.size() && k < p.size(); ++k) {
        ++checks;
        if (!SameLine(o[k], p[k]))
            break;
    }
    CHECK(k == o.size() && k == p.size(), "log line %u of %u / %u:\n  orig %s\n  port %s%s", unsigned(k), unsigned(o.size()),
          unsigned(p.size()), k < o.size() ? o[k].c_str() : "(end)", k < p.size() ? p[k].c_str() : "(end)",
          k > 0 ? ("\n  after " + o[k - 1]).c_str() : "");
    for (int i = 0; i < FRAMES; ++i) {
        const int diff = std::memcmp(g_origRun.arena.frames[i], g_portRun.arena.frames[i], FRAME_BYTES);
        if (diff != 0) {
            int at = 0;
            while (g_origRun.arena.frames[i][at] == g_portRun.arena.frames[i][at])
                ++at;
            uint32_t ow, pw;
            std::memcpy(&ow, g_origRun.arena.frames[i] + (at & ~3), 4);
            std::memcpy(&pw, g_portRun.arena.frames[i] + (at & ~3), 4);
            CHECK(false, "f%d +0x%x: %08x / %08x", i, at & ~3, ow, pw);
            break;
        }
        ++checks;
    }
}

void TestScenarios()
{
    const S_Scenario s = RandomScenario();
    RunOrig(s);
    RunPort(s);
    Compare();
    // (HD2_COMPASS_SHOW=round: that round's two logs.)
    static const char *show = std::getenv("HD2_COMPASS_SHOW");
    if (show && std::atoi(show) == g_round)
        for (size_t k = 0; k < std::max(g_origRun.log.size(), g_portRun.log.size()); ++k)
            std::printf("%s\n  %s\n", k < g_origRun.log.size() ? g_origRun.log[k].c_str() : "(end)",
                        k < g_portRun.log.size() ? g_portRun.log[k].c_str() : "(end)");
}

// UpdateWobble straight (no active soldier: the decay and the swing).
void TestWobble()
{
    g_active = -1;
    const uint32_t time = P(0.5) ? uint32_t(I(0, 100000)) : U32();
    *reinterpret_cast<uint32_t *>(g_game + 0x2C) = time;
    C_Compass c{};
    c.m_wobble = P(0.3) ? 0.0f : F(0, 0.2f);
    c.m_needle = F(-3.2f, 3.2f);
    uint8_t o[0x100] = {};
    std::memcpy(o + 8, &c.m_wobble, 4);
    std::memcpy(o + 4, &c.m_needle, 4);
    s_wobbleDecay = OrigDecay() = F(0, 0.001f);
    const int dt = RandomDelta();
    using T_Wobble = float(__attribute__((thiscall)) *)(void *, int);
    g_log.clear();
    const float of = OriginalAt<T_Wobble>(0x00494360)(o, dt);
    const float pf = c.UpdateWobble(dt);
    CHECK(Bits(of) == Bits(pf), "wobble %08x / %08x (time %u, wobble %a, needle %a, dt %d)", Bits(of), Bits(pf), time, double(c.m_wobble),
          double(c.m_needle), dt);
}

// The port's own Forget (not in the original, which kept a deleted human's blips): the port's alone, after random Adds;
// the human's blips gone (their visuals released), the others kept, the frames otherwise untouched.
void TestForget()
{
    RandomScenario();
    StartSide(S_Scenario{});
    g_gameRole = 7;
    g_difficulty = I(1, 3);
    C_Radar radar(reinterpret_cast<I3D_scene *>(&g_scene));
    const int adds = I(0, 12);
    for (int k = 0; k < adds; ++k)
        radar.Add(HumanAt(I(0, HUMANS - 1)));
    const int h = P(0.9) ? I(0, HUMANS - 1) : -1;
    std::vector<const S_RadarBlip *> kept;
    std::vector<const void *> gone;
    for (const S_RadarBlip *b : radar.m_blips)
        (b->human == HumanAt(h) ? gone.push_back(b->visual) : kept.push_back(b));
    g_log.clear();
    radar.Forget(HumanAt(h));
    std::vector<const S_RadarBlip *> now(radar.m_blips.begin(), radar.m_blips.end());
    std::sort(kept.begin(), kept.end());
    std::sort(now.begin(), now.end());
    CHECK(now == kept, "Forget h%d: %u blips kept of %u", h, unsigned(now.size()), unsigned(kept.size()));
    std::vector<std::string> released;
    for (const void *v : gone)
        released.push_back(Name(v) + " Release");
    std::vector<std::string> log = g_log;
    std::sort(released.begin(), released.end());
    std::sort(log.begin(), log.end());
    CHECK(log == released, "Forget h%d: calls %u, %u visuals", h, unsigned(log.size()), unsigned(released.size()));
}

// The port's fix (the original's flaw: a direction a hair west of north, + 2 pi, made the ninth direction, past the
// marks): the port's alone, the last direction's mark set and nothing else.
int g_edgeFound;
void TestSoundEdge()
{
    S_vector v;
    int tries = 0;
    for (;; ++tries) {
        v = S_vector(-std::ldexp(1.0f, -I(20, 126)), F(-1, 1), F(0.01f, 5));
        if (P(0.5))
            v.x = -v.x;
        if (SoundDirection(v) == 8) {
            ++g_edgeFound;
            break;
        }
        if (tries > 200)
            return;
    }
    C_Compass c{};
    g_active = 0;
    c.m_soundTimes[7][0] = c.m_soundTimes[7][1] = 0;
    const int chapter = P(0.5) ? 0 : 1;
    C_Compass before = c;
    g_log.clear();
    c.ShowSound(v, chapter);
    before.m_soundTimes[7][chapter == 0 ? 0 : 1] = 500;
    CHECK(std::memcmp(&before, &c, sizeof c) == 0, "ShowSound (%s): not only the last mark", VecText(v).c_str());
    // (The original's flaw shown: its time goes past the marks' times, over m_leftFoot or past the compass.)
    uint8_t o[0x100] = {};
    OriginalAt<T_Sound>(0x004947e0)(o, &v, chapter);
    int past;
    std::memcpy(&past, o + 0x60 + (8 * 2 + (chapter == 0 ? 0 : 1)) * 4, 4);
    CHECK(past == 500, "the original's ShowSound (%s): %d past the marks", VecText(v).c_str(), past);
}

// The statics as loaded (the original's from its image's data).
void TestStatics()
{
    CHECK(Bits(s_wobbleDecay) == Bits(OrigDecay()), "s_wobbleDecay at load %08x / %08x", Bits(OrigDecay()), Bits(s_wobbleDecay));
    CHECK(Bits(s_unused89bc58) == Bits(OrigUnused()), "s_unused89bc58 at load %08x / %08x", Bits(OrigUnused()), Bits(s_unused89bc58));
    CHECK(s_wobbleSkip == OrigSkip(), "s_wobbleSkip at load %d / %d", OrigSkip(), s_wobbleSkip);
}

void SetUp()
{
    for (int k = 0; k < 64; ++k)
        g_frameVt[k] = reinterpret_cast<void *>(F_Other<0>);
#define OTHER(VT, FN, N) VT[N] = reinterpret_cast<void *>(FN<N>)
    OTHER(g_frameVt, F_Other, 1);
    OTHER(g_frameVt, F_Other, 2);
    OTHER(g_frameVt, F_Other, 3);
    OTHER(g_frameVt, F_Other, 4);
    OTHER(g_frameVt, F_Other, 5);
    OTHER(g_frameVt, F_Other, 6);
    OTHER(g_frameVt, F_Other, 8);
    OTHER(g_frameVt, F_Other, 10);
    OTHER(g_frameVt, F_Other, 11);
    OTHER(g_frameVt, F_Other, 13);
    OTHER(g_frameVt, F_Other, 15);
    OTHER(g_frameVt, F_Other, 16);
    OTHER(g_frameVt, F_Other, 17);
    OTHER(g_frameVt, F_Other, 20);
    OTHER(g_frameVt, F_Other, 21);
    g_frameVt[0x00 / 4] = reinterpret_cast<void *>(F_Release);
    g_frameVt[0x1C / 4] = reinterpret_cast<void *>(F_SetWorldDir);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_Update);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(F_SetOn);
    g_frameVt[0x38 / 4] = reinterpret_cast<void *>(F_LinkTo);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    g_frameVt[0x4C / 4] = reinterpret_cast<void *>(F_Duplicate);
    g_frameVt[0xA8 / 4] = reinterpret_cast<void *>(F_SetAlpha);
    for (int k = 0; k < 64; ++k)
        g_sceneVt[k] = reinterpret_cast<void *>(SC_Other<0>);
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(SC_FindFrame);
    for (int k = 0; k < 32; ++k)
        g_driverVt[k] = reinterpret_cast<void *>(D_Other<0>);
    g_driverVt[0x50 / 4] = reinterpret_cast<void *>(D_CreateVisual);
#undef OTHER
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e5296, reinterpret_cast<const void *>(O_UpdateWMatrix)},
        {0x007e529c, reinterpret_cast<const void *>(O_GetDriver)},
        {0x007e5302, reinterpret_cast<const void *>(O_RandFloat)},
        {0x007e6393, reinterpret_cast<const void *>(O_Rand)},
        {0x007e566f, reinterpret_cast<const void *>(O_New)},
        {0x007e2120, reinterpret_cast<const void *>(O_Delete)},
        {0x0045d1d0, reinterpret_cast<const void *>(O_GetActivePlayer)},
        {0x006b6a20, reinterpret_cast<const void *>(O_GetDifficulty)},
        {0x005a6450, reinterpret_cast<const void *>(O_IsSurrendered)},
        {0x005a6380, reinterpret_cast<const void *>(O_IsUnconscious)},
        {0x005a80d0, reinterpret_cast<const void *>(O_GetFacingDir)},
        {0x005a80e0, reinterpret_cast<const void *>(O_GetViewDir)},
        {0x005a5c70, reinterpret_cast<const void *>(O_GetMoveMode)},
        {0x005a5c80, reinterpret_cast<const void *>(O_GetStateFlags)},
        {0x005a8160, reinterpret_cast<const void *>(O_IsLeftFootForward)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // (The original's game: GetGame reads this pointer.)
    *OriginalAt<void **>(0x008ae8f4) = g_game;
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    std::printf("compass_diff: exception %08lx at %p (linked %08x) [%s round %d]\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, unsigned(at - base + 0x20000000), g_case, g_round);
    for (const std::string &line : g_log)
        std::printf("    > %s\n", line.c_str());
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW) {
        const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        char text[160];
        const int n = std::snprintf(text, sizeof text, "first chance %08lx at %08x (linked %08x) [%s round %d]\n", code, unsigned(at),
                                    unsigned(at - base + 0x20000000), g_case, g_round);
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, DWORD(n), &written, nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace H

int main()
{
    using namespace H;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_COMPASS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("compass_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    g_case = "Statics";
    TestStatics();
    CHECK(Bits(EDGE_X * EDGE_X + EDGE_Y * EDGE_Y) == 0x322bcc77u, "the edge pair's square %08x", Bits(EDGE_X * EDGE_X + EDGE_Y * EDGE_Y));
    SetUp();
    int rounds = std::getenv("HD2_COMPASS_QUICK") ? 8000 : 60000;
    if (const char *r = std::getenv("HD2_COMPASS_ROUNDS"))
        rounds = std::atoi(r);
    if (const char *u = std::getenv("HD2_COMPASS_ULPS"))
        g_ulps = std::atoi(u);
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {{"Wobble", TestWobble}, {"Scenarios", TestScenarios}, {"SoundEdge", TestSoundEdge}, {"Forget", TestForget}};
    for (const S_Case &c : cases) {
        g_case = c.name;
        const int n = c.run == TestScenarios ? rounds : rounds / 20;
        for (g_round = 0; g_round < n; ++g_round)
            c.run();
    }
    std::printf("compass_diff: the ninth direction found %d times\n", g_edgeFound);
    std::printf("compass_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
