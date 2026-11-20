// The interface's differential kit (parts 64 on: game/interface*.cpp against the original's), included by each part's test
// after the port's source it tests (compiled into the test: its namespace's statics reached) and original_exe.h. What the
// interface reaches outside itself is faked on both sides with the same loggers; a part runs scenarios of operations on
// each side from one start and compares the logs (the calls out, the fields snapped after each operation) and the arena.
// - checks and failures, the random helpers, the log (Log; float values as "f" + their bits, FloatText);
// - an arena of frames (the engine's layout, a logging vtable: Release, SetPos, SetDir, SetWorldDir, Update, SetOn,
//   LinkTo, NumChildren, FindChildFrame (a part's answer: g_findChild), Duplicate, UpdateBound (g_updateBound), SetAlpha
//   (stored at +0x1EC); LS3DF's UpdateWMatrixProc logged, the matrix taken as made; the original's import redirected);
// - the scene (FindFrame: g_findFrame), the driver (Set and GetState, CreateVisual: g_createVisual; the original's
//   I3DGetDriver import redirected), the graph (GetIGraph: the original's import redirected; slots a part sets);
// - the original's new and delete to malloc and free, its rand to the CRT's (seeded the same), the game's pointer;
// - Name(p): frames "fN", the fakes by name, a part's own (g_partName), else "?";
// - the crash handlers (HD2_INTERFACE_FIRST_CHANCE).

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

std::mt19937 g_rng(64);
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
float FromBits(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char text[600];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_log.push_back(text);
    // (HD2_INTERFACE_ECHO=round: that round's calls printed as they come, for a crash's or a hang's last.)
    static const char *echo = std::getenv("HD2_INTERFACE_ECHO");
    if (echo && std::atoi(echo) == g_round)
        std::printf("    > %s\n", text);
}
std::string FloatText(float f)
{
    char text[16];
    std::snprintf(text, sizeof text, "f%08x", Bits(f));
    return text;
}
std::string VecText(const S_vector &v) { return FloatText(v.x) + " " + FloatText(v.y) + " " + FloatText(v.z); }

// ---- the arena of frames ----
#ifndef INTERFACE_FRAMES
#define INTERFACE_FRAMES 32
#endif
constexpr int FRAMES = INTERFACE_FRAMES;   // (a part may ask for more: INTERFACE_FRAMES before the include)
constexpr int FRAME_BYTES = 0x400;
struct alignas(16) S_Arena {
    uint8_t frames[FRAMES][FRAME_BYTES];
};
S_Arena g_arena, g_arenaStart;
void *g_frameVt[64], *g_sceneVt[64], *g_driverVt[32], *g_graphVt[96];
struct S_Fake {
    void **vt;
};
S_Fake g_scene{g_sceneVt}, g_driver{g_driverVt}, g_graph{g_graphVt};
I3D_frame *FrameAt(int i) { return reinterpret_cast<I3D_frame *>(g_arena.frames[i]); }
int FrameIndex(const void *f)
{
    for (int i = 0; i < FRAMES; ++i)
        if (f == g_arena.frames[i])
            return i;
    return -1;
}
// A part's names for its own objects ("" when not one of them).
std::string (*g_partName)(const void *p) = nullptr;
std::string Name(const void *p)
{
    char text[32];
    if (!p)
        return "null";
    if (FrameIndex(p) >= 0) {
        std::snprintf(text, sizeof text, "f%d", FrameIndex(p));
        return text;
    }
    if (p == &g_scene)
        return "scene";
    if (p == &g_driver)
        return "driver";
    if (p == &g_graph)
        return "graph";
    if (g_partName) {
        const std::string n = g_partName(p);
        if (!n.empty())
            return n;
    }
    return "?";
}

// The parts' answers.
I3D_frame *(*g_findChild)(I3D_frame *f, const char *name) = nullptr;
I3D_frame *(*g_findFrame)(const char *name) = nullptr;
I3D_frame *(*g_createVisual)(uint32_t type) = nullptr;
void (*g_updateBound)(I3D_frame *f) = nullptr;
uint32_t g_driverState[16];

uint32_t __stdcall F_Release(void *f)
{
    Log("%s Release", Name(f).c_str());
    return 0;
}
void __stdcall F_SetPos(void *f, const S_vector &pos) { Log("%s SetPos %s", Name(f).c_str(), VecText(pos).c_str()); }
void __stdcall F_SetDir(void *f, const S_vector &dir, float roll)
{
    Log("%s SetDir %s %s", Name(f).c_str(), VecText(dir).c_str(), FloatText(roll).c_str());
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
int __stdcall F_NumChildren(void *f)
{
    Log("%s NumChildren", Name(f).c_str());
    return 0;
}
void *__stdcall F_FindChild(void *f, const char *name, uint32_t mask)
{
    Log("%s FindChildFrame %s %x", Name(f).c_str(), name, mask);
    return g_findChild ? g_findChild(static_cast<I3D_frame *>(f), name) : nullptr;
}
HRESULT __stdcall F_Duplicate(void *f, void *source)
{
    Log("%s Duplicate %s", Name(f).c_str(), Name(source).c_str());
    return 0;
}
void __stdcall F_UpdateBound(void *f)
{
    Log("%s UpdateBound", Name(f).c_str());
    if (g_updateBound)
        g_updateBound(static_cast<I3D_frame *>(f));
}
void __stdcall F_SetAlpha(void *f, float alpha)
{
    Log("%s SetAlpha %s", Name(f).c_str(), FloatText(alpha).c_str());
    std::memcpy(static_cast<uint8_t *>(f) + 0x1EC, &alpha, 4);
}
void __stdcall F_Other(void *f) { Log("%s other slot", Name(f).c_str()); }
void __stdcall SC_Other(void *) { Log("scene other slot"); }
void __stdcall D_Other(void *) { Log("driver other slot"); }
void __stdcall G_Other(void *) { Log("graph other slot"); }
void *__stdcall SC_FindFrame(void *, const char *name, uint32_t mask)
{
    Log("scene FindFrame %s %x", name, mask);
    return g_findFrame ? g_findFrame(name) : nullptr;
}
void __stdcall D_SetState(void *, int state, uint32_t value)
{
    Log("driver SetState %d %x", state, value);
    if (state >= 0 && state < 16)
        g_driverState[state] = value;
}
uint32_t __stdcall D_GetState(void *, int state)
{
    Log("driver GetState %d", state);
    return state >= 0 && state < 16 ? g_driverState[state] : 0;
}
void *__stdcall D_CreateVisual(void *, uint32_t type)
{
    I3D_frame *f = g_createVisual ? g_createVisual(type) : nullptr;
    Log("driver CreateVisual %x -> %s", type, Name(f).c_str());
    return f;
}
// LS3DF's (thiscall): logged, the matrix taken as made.
void __attribute__((thiscall)) O_UpdateWMatrix(void *f)
{
    Log("%s UpdateWMatrixProc", Name(f).c_str());
    reinterpret_cast<I3D_frame *>(f)->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
}
void *__cdecl O_GetDriver() { return &g_driver; }
void *__cdecl O_GetIGraph() { return &g_graph; }
int __cdecl O_Rand() { return std::rand(); }
void *__cdecl O_New(size_t size) { return std::malloc(size ? size : 1); }
void __cdecl O_Delete(void *p) { std::free(p); }

// A frame in a random state (its vtable the arena's).
void RandomFrame(int i)
{
    std::memset(g_arena.frames[i], 0, FRAME_BYTES);
    *reinterpret_cast<void ***>(g_arena.frames[i]) = g_frameVt;
    I3D_frame *f = FrameAt(i);
    f->m_frameFlags = (P(0.6) ? 1u : 0u) | (P(0.7) ? uint32_t(FRMFLAGS_WMATRIX_VALID) : 0u) |
                      (P(0.15) ? uint32_t(FRMFLAGS_POS_LOCKED) : 0u) | (P(0.5) ? uint32_t(FRMFLAGS_ROT_DIRTY) : 0u) |
                      (P(0.7) ? uint32_t(FRMFLAGS_BOUND_VALID) : 0u) | (U32() & 0x0F00);
    for (int r = 0; r < 4; ++r) {
        f->m_localMatrix(r) = S_vector(F(-2, 2), F(-2, 2), F(-2, 2));
        f->m_worldMatrix(r) = S_vector(F(-2, 2), F(-2, 2), F(-2, 2));
    }
    f->m_rot.v = S_vector(F(-1, 1), F(-1, 1), F(-1, 1));
    f->m_rot.s = F(-1, 1);
    const S_vector a(F(-0.2f, 1.2f), F(-0.9f, 0.1f), F(-1, 1)), b(F(-0.2f, 1.2f), F(-0.9f, 0.1f), F(-1, 1));
    f->m_bound.bbox.min = S_vector(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
    f->m_bound.bbox.max = S_vector(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
    const float alpha = P(0.3) ? Pick({0.0f, 1.0f, 0.5f}) : F(0, 1);
    std::memcpy(g_arena.frames[i] + 0x1EC, &alpha, 4);
}

void SetUpKit()
{
    for (void *&s : g_frameVt)
        s = reinterpret_cast<void *>(F_Other);
    g_frameVt[0x00 / 4] = reinterpret_cast<void *>(F_Release);
    g_frameVt[0x08 / 4] = reinterpret_cast<void *>(F_SetPos);
    g_frameVt[0x0C / 4] = reinterpret_cast<void *>(F_SetDir);
    g_frameVt[0x1C / 4] = reinterpret_cast<void *>(F_SetWorldDir);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_Update);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(F_SetOn);
    g_frameVt[0x38 / 4] = reinterpret_cast<void *>(F_LinkTo);
    g_frameVt[0x3C / 4] = reinterpret_cast<void *>(F_NumChildren);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    g_frameVt[0x4C / 4] = reinterpret_cast<void *>(F_Duplicate);
    g_frameVt[0x58 / 4] = reinterpret_cast<void *>(F_UpdateBound);
    g_frameVt[0xA8 / 4] = reinterpret_cast<void *>(F_SetAlpha);
    for (void *&s : g_sceneVt)
        s = reinterpret_cast<void *>(SC_Other);
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(SC_FindFrame);
    for (void *&s : g_driverVt)
        s = reinterpret_cast<void *>(D_Other);
    g_driverVt[0x18 / 4] = reinterpret_cast<void *>(D_SetState);
    g_driverVt[0x1C / 4] = reinterpret_cast<void *>(D_GetState);
    g_driverVt[0x50 / 4] = reinterpret_cast<void *>(D_CreateVisual);
    for (void *&s : g_graphVt)
        s = reinterpret_cast<void *>(G_Other);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e5296, reinterpret_cast<const void *>(O_UpdateWMatrix)},
        {0x007e529c, reinterpret_cast<const void *>(O_GetDriver)},
        {0x007e530e, reinterpret_cast<const void *>(O_GetIGraph)},
        {0x007e6393, reinterpret_cast<const void *>(O_Rand)},
        {0x007e566f, reinterpret_cast<const void *>(O_New)},
        {0x007e2120, reinterpret_cast<const void *>(O_Delete)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

// Lines equal but for the float tokens ("f" + 8 hex digits), within g_ulps (0: exact).
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
    for (;;) {
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
}
// The two sides' logs, line by line (the first difference reported with the line before it).
void CompareLogs(const std::vector<std::string> &o, const std::vector<std::string> &p)
{
    size_t k = 0;
    for (; k < o.size() && k < p.size(); ++k) {
        ++checks;
        if (!SameLine(o[k], p[k]))
            break;
    }
    CHECK(k == o.size() && k == p.size(), "log line %u of %u / %u:\n  orig %s\n  port %s%s", unsigned(k), unsigned(o.size()),
          unsigned(p.size()), k < o.size() ? o[k].c_str() : "(end)", k < p.size() ? p[k].c_str() : "(end)",
          k > 0 ? ("\n  after " + o[k - 1]).c_str() : "");
}
// The arenas after each side, frame by frame.
void CompareArenas(const S_Arena &o, const S_Arena &p)
{
    for (int i = 0; i < FRAMES; ++i) {
        ++checks;
        if (std::memcmp(o.frames[i], p.frames[i], FRAME_BYTES) != 0) {
            int at = 0;
            while (o.frames[i][at] == p.frames[i][at])
                ++at;
            uint32_t ow, pw;
            std::memcpy(&ow, o.frames[i] + (at & ~3), 4);
            std::memcpy(&pw, p.frames[i] + (at & ~3), 4);
            CHECK(false, "f%d +0x%x: %08x / %08x", i, at & ~3, ow, pw);
            return;
        }
    }
}
// HD2_INTERFACE_SHOW=round: that round's two logs printed.
void ShowLogs(const std::vector<std::string> &o, const std::vector<std::string> &p)
{
    static const char *show = std::getenv("HD2_INTERFACE_SHOW");
    if (show && std::atoi(show) == g_round)
        for (size_t k = 0; k < std::max(o.size(), p.size()); ++k)
            std::printf("%s\n  %s\n", k < o.size() ? o[k].c_str() : "(end)", k < p.size() ? p[k].c_str() : "(end)");
}

const char *g_testName = "interface";
LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    std::printf("%s: exception %08lx at %p (linked %08x) [%s round %d]\n", g_testName, e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, unsigned(at - base + 0x20000000), g_case, g_round);
    const size_t from = g_log.size() > 30 ? g_log.size() - 30 : 0;
    for (size_t k = from; k < g_log.size(); ++k)
        std::printf("    > %s\n", g_log[k].c_str());
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
// The start of a part's main: the original loaded at its base (a relaunched copy), PC_24; false: return `exitCode`.
bool StartKit(const char *name, int &exitCode)
{
    g_testName = name;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_INTERFACE_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    if (RunReservingOriginalBase(exitCode))
        return false;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("%s: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n", name);
        exitCode = 1;
        return false;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUpKit();
    if (const char *u = std::getenv("HD2_INTERFACE_ULPS"))
        g_ulps = std::atoi(u);
    return true;
}

}  // namespace H

// The port's side of the kit's fakes.
void I3D_frame::UpdateWMatrixProc() { H::O_UpdateWMatrix(this); }
// LS3DF's own math (the original's imports go to the same DLL).
namespace H {
void *Ls3df(const char *name)
{
    const FARPROC p = GetProcAddress(GetModuleHandleA("LS3DF.dll"), name);
    if (!p) {
        std::printf("%s: %s not in LS3DF.dll\n", g_testName, name);
        std::exit(2);
    }
    return reinterpret_cast<void *>(p);
}
}  // namespace H
void __stdcall S_quat::Make(const S_matrix &m)
{
    using T = void(__stdcall *)(S_quat *, const S_matrix &);
    static const T make = reinterpret_cast<T>(H::Ls3df("?Make@S_quat@@QAGXABUS_matrix@@@Z"));
    make(this, m);
}
void __stdcall S_quat::Normalize()
{
    using T = void(__stdcall *)(S_quat *);
    static const T normalize = reinterpret_cast<T>(H::Ls3df("?Normalize@S_quat@@QAGXXZ"));
    normalize(this);
}
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&H::g_driver); }
extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&H::g_graph); }
