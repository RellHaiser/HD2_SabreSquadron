// Junkers part 1: the Junkers 88 bomber, its fighters, parts, debris, tracers, paths and seats (game/junkers88.cpp,
// the original's 0x005edd20..0x005f91b0) against the original executable's. cmd_diff.cpp's world, fakes and call logger
// are included (CMD_DIFF_NO_MAIN); the bomber's calls go through jk_calls.s / jk_calls.inc (gen_cmd_calls.py jk). The
// model, sound and animation caches run on both sides (game/resource_cache.cpp linked; the original's constructed by
// their static initialisers' constructors). The seat weapons are fakes written here on both sides (their GetAim /
// GetOrigin return vectors through a hidden pointer); the LS3DF math is LS3DF.dll's on both sides; RandomFloat and
// AngleBetween2D are the original's own on both sides (the original's rand redirected to the CRT's).
//
// Each case builds the same world on each side (cmd_diff's, plus the bomber's model: its parts "<name>", "<name>d",
// "<name>f" for the parts' names, some missing, children on some; the blur, propellers, BODY, the five seats' dummies
// with their user properties; the fighters' and the paths' models made by the engine's Open by their file names),
// makes a bomber on each side (the original's by its constructor 0x005ef180 in a block of 0x330, the port's by its
// own), gives both the same properties, starts the mission and runs the same random sequence of operations: updates
// and ticks (the game's time advancing), hits on the fighters, SetState, the seats' virtuals, FindTarget, the paths'
// notes, saves and loads, restarts, stops. After each operation the answer, the bomber's state (every field, its
// parts, fighters, debris, tracers, paths and chatter in each side's layout), the world's frames and every call made
// into the game and the engine are compared; a save's bytes too.
// MinGW; build_jk.bat; runs jk_diff.exe ("jk_diff: N checks, 0 failures"). Env: HD2_JK_CASES (default 1,500;
// HD2_JK_QUICK: 300), HD2_JK_SEED, HD2_JK_SHOW (1: the port's lines; 2: both), HD2_JK_DUMP.
#define HARNESS_NAME "jk_diff"
#define CMD_CALLS_INC "jk_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/collision.h"
#include "ls3d/igraph.h"
#include "game/game_texts.h"
#include "game/junkers88.h"
#include "game/resource_cache.h"
#include "game/seat_weapon.h"
#undef protected
#undef private

// ---- the port's side of what the bomber calls and the test provides ----
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
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

// LS3DF.dll's math (the original's calls go there too; a struct answer's address after `this`).
template <class T> T Ls3dfFn(const char *name) { return reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df(name))); }
S_matrix __stdcall RotateXMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateXMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix r;
    f(&r, angle);
    return r;
}
S_matrix __stdcall RotateYMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateYMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix r;
    f(&r, angle);
    return r;
}
S_matrix __stdcall RotateZMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateZMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix r;
    f(&r, angle);
    return r;
}
bool __stdcall S_matrix::Inverse(const S_matrix &src)
{
    static const auto f = Ls3dfFn<bool(__stdcall *)(S_matrix *, const S_matrix &)>("?Inverse@S_matrix@@QAG_NABU1@@Z");
    return f(this, src);
}
S_vector __stdcall S_matrix::GetScale() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, float *)>("?GetScale@S_matrix@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
S_matrix __stdcall S_matrix::operator*(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, S_matrix *, const S_matrix &)>("??DS_matrix@@QBG?AU0@ABU0@@Z");
    S_matrix r;
    f(this, &r, m);
    return r;
}
S_quat __stdcall S_quat::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_matrix &)>(
        "?RotateByMatrix@S_quat@@QBG?AU1@ABUS_matrix@@@Z");
    S_quat r;
    f(this, &r, m);
    return r;
}
// The original's own on both sides (its rand: the CRT's, cmd_diff's redirect).
float RandomFloat(float max) { return OriginalAt<float(__stdcall *)(float)>(0x00475190)(max); }
float AngleBetween2D(const float *a, const float *b)
{
    return OriginalAt<float(__fastcall *)(const float *, const float *)>(0x00788a10)(a, b);
}
// The port's allocations zeroed, as the original's (its _aligned_malloc redirected to a zeroing one): a bomber the
// creator makes has its base's fields as the (logged) base constructor leaves them on both sides.
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t n, std::align_val_t a)
{
    void *p = _aligned_malloc(n ? n : 1, size_t(a));
    std::memset(p, 0, n ? n : 1);
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
// The bomber's creator (junkers88.cpp's).
extern C_Junkers88Creator g_junkers88Creator;
// The actors' creators (the bomber's registers itself before main: a table of the test's, set up as the program loads).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the bomber's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
void C_Actor::CopyFrom(const C_Actor *) {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}

namespace H {
// The texts (the bomber's name): logged, "T<id>" on both sides.
std::string TextOf(int id) { return F("T%X", id); }
}  // namespace H
C_str C_GameTexts::Get(int id) const
{
    H::g_log[H::g_side].push_back(H::F("texts %d", id));
    return C_str(H::TextOf(id).c_str());
}

// ---- the seat weapons: the test's on both sides ----
namespace H {
struct S_SwState {
    int count = 0;
};
S_SwState g_sw[2];
std::string SwName(const void *w) { return NameOf(reinterpret_cast<uint32_t>(w)); }
void SwLog(const void *w, const std::string &what) { g_log[g_side].push_back(SwName(w) + " " + what); }
void SwMade(void *w)
{
    Name(w, F("seat weapon %d", g_sw[g_side].count++));
    SwLog(w, "made");
}
// (Its name forgotten: its block may come back as another object, a fighter's volume among them.)
void SwDestroyed(void *w)
{
    SwLog(w, "destroyed");
    g_names[g_side].erase(reinterpret_cast<uint32_t>(w));
}
bool SwInit(void *w, void *owner, void *frame, void *seatFrame, int seat)
{
    const bool ok = NextFake() % 4 != 0;
    SwLog(w, F("Init(%s, %s, %s, %d) -> %d", NameOf(reinterpret_cast<uint32_t>(owner)).c_str(),
               NameOf(reinterpret_cast<uint32_t>(frame)).c_str(), NameOf(reinterpret_cast<uint32_t>(seatFrame)).c_str(),
               seat, int(ok)));
    return ok;
}
int SwRounds(void *w)
{
    const int r = FakeInt();
    SwLog(w, F("GetRounds -> %d", r));
    return r;
}
void *SwItemDef(void *w)
{
    static uint8_t defs[2][0x40];
    Name(defs[g_side], "item def");
    SwLog(w, "GetItemDef");
    return defs[g_side];
}
S_vector SwVector(void *w, const char *what)
{
    const S_vector v(FakeFloat(), FakeFloat(), FakeFloat());
    SwLog(w, std::string(what) + " -> " + VectorText(reinterpret_cast<uint32_t>(&v.x), 3));
    return v;
}
bool SwC8(void *w, int *a, int *b, int *c)
{
    const bool ok = NextFake() % 2 != 0;
    const int x = FakeInt(), y = FakeInt(), z = FakeInt();
    if (a)
        *a = x;
    if (b)
        *b = y;
    if (c)
        *c = z;
    SwLog(w, F("UnknownC8 -> %d %d %d %d", int(ok), x, y, z));
    return ok;
}
}  // namespace H
C_SeatWeapon::C_SeatWeapon() { H::SwMade(this); }
C_SeatWeapon::~C_SeatWeapon() { H::SwDestroyed(this); }
bool C_SeatWeapon::Init(C_Actor *owner, I3D_frame *frame, I3D_frame *seatFrame, int seat)
{
    return H::SwInit(this, owner, frame, seatFrame, seat);
}
void C_SeatWeapon::SetGunner(C_Human *gunner) { H::SwLog(this, "SetGunner(" + H::NameOf(reinterpret_cast<uint32_t>(gunner)) + ")"); }
int C_SeatWeapon::GetRounds() { return H::SwRounds(this); }
C_ItemDef *C_SeatWeapon::GetItemDef() { return static_cast<C_ItemDef *>(H::SwItemDef(this)); }
S_vector C_SeatWeapon::GetAim() { return H::SwVector(this, "GetAim"); }
S_vector C_SeatWeapon::GetOrigin() { return H::SwVector(this, "GetOrigin"); }
void C_SeatWeapon::ApplyControls(float *axes, float *buttons)
{
    H::SwLog(this, "ApplyControls(" + H::VectorText(reinterpret_cast<uint32_t>(axes), 2) + ", " +
                       H::VectorText(reinterpret_cast<uint32_t>(buttons), 2) + ")");
}
void C_SeatWeapon::ShowView(bool on) { H::SwLog(this, H::F("ShowView(%d)", int(on))); }
bool C_SeatWeapon::UnknownC8(int *a, int *b, int *c) { return H::SwC8(this, a, b, c); }
void C_SeatWeapon::Update(int time) { H::SwLog(this, H::F("Update(%d)", time)); }
void C_SeatWeapon::Tick(int time) { H::SwLog(this, H::F("Tick(%d)", time)); }

// The sound driver (ISndGetDriver: LS3DF's; the original's import thunk redirected): each side's world's.
namespace H {
uint8_t *g_sndDriver[2];
void *__cdecl O_ISndGetDriver() { return g_sndDriver[1]; }
}  // namespace H
extern "C" ISND_driver *ISndGetDriver() { return reinterpret_cast<ISND_driver *>(H::g_sndDriver[H::g_side]); }

namespace H {

#define OT __attribute__((thiscall))
// The original's side of the seat weapons (thiscall; GetRounds and GetItemDef __fastcall with `this` alone, the
// same; GetAim / GetOrigin through a hidden pointer after `this`).
void *OT O_SwCtor(void *w)
{
    SwMade(w);
    return w;
}
void OT O_SwDtor(void *w) { SwDestroyed(w); }
bool OT O_SwInit(void *w, void *owner, void *frame, void *seatFrame, int seat) { return SwInit(w, owner, frame, seatFrame, seat); }
void OT O_SwSetGunner(void *w, void *gunner) { SwLog(w, "SetGunner(" + NameOf(reinterpret_cast<uint32_t>(gunner)) + ")"); }
int OT O_SwGetRounds(void *w) { return SwRounds(w); }
void *OT O_SwGetItemDef(void *w) { return SwItemDef(w); }
void *OT O_SwGetAim(void *w, S_vector *ret)
{
    *ret = SwVector(w, "GetAim");
    return ret;
}
void *OT O_SwGetOrigin(void *w, S_vector *ret)
{
    *ret = SwVector(w, "GetOrigin");
    return ret;
}
void OT O_SwApplyControls(void *w, float *axes, float *buttons)
{
    SwLog(w, "ApplyControls(" + VectorText(reinterpret_cast<uint32_t>(axes), 2) + ", " +
                 VectorText(reinterpret_cast<uint32_t>(buttons), 2) + ")");
}
void OT O_SwShowView(void *w, int on) { SwLog(w, F("ShowView(%d)", on & 0xFF)); }
bool OT O_SwC8(void *w, int *a, int *b, int *c) { return SwC8(w, a, b, c); }
void OT O_SwUpdate(void *w, int time) { SwLog(w, F("Update(%d)", time)); }
void OT O_SwTick(void *w, int time) { SwLog(w, F("Tick(%d)", time)); }
// The texts (the original's C_GameTexts::Get: its C_str by a hidden pointer).
void *OT O_TextsGet(void *, void *ret, int id)
{
    g_log[g_side].push_back(F("texts %d", id));
    *reinterpret_cast<uint32_t *>(ret) = 0;
    OriginalAt<T_Fstr>(0x00405100)(ret, "%s", TextOf(id).c_str());
    return ret;
}
int __cdecl O_Atexit(void *) { return 0; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size ? size : 1, align);
    std::memset(p, 0, size ? size : 1);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }

uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T &Mut(void *p, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }

// ---- the world's models: the bomber's parts, the fighters', the paths', the tracers' ----
// The parts' names (each "<name>d" damaged, "<name>" intact, "<name>f" the stump; the engines and the blades always
// whole: the original reads them unchecked).
const char *const PART_NAMES[] = {"engine_l00", "engine_r00", "left_wing00", "left_wing01", "left_wing02", "right_wing00",
                                  "right_wing01", "right_wing02", "flap_l00", "flap_l01", "flap_r00", "flap_r01",
                                  "flap_r00a", "flap_r01a", "listy_l00", "listy_r00", "elevator_left01",
                                  "elevator_right01", "tail00", "gun01", "door00"};
constexpr int PART_COUNT = int(sizeof PART_NAMES / sizeof *PART_NAMES);
std::deque<std::string> g_texts[2];                     // (names and user properties: stable storage)
const char *Text(const std::string &s)
{
    g_texts[g_side].push_back(s);
    return g_texts[g_side].back().c_str();
}
// A world frame under `parent` (its name, user properties, owner model).
uint8_t *Child(uint8_t *parent, int type, const std::string &name, uint8_t *owner, uint32_t visual = I3DVISUAL_OBJECT)
{
    uint8_t *b = NewFrame(type, name);
    S_FakeFrame &f = W().frames.back();
    if (type == FRAME_VISUAL)
        f.visualType = visual;
    I3D_frame *x = AsFrame(b);
    x->m_name = f.name.c_str();
    x->m_ownerModel = reinterpret_cast<I3D_frame *>(owner);
    x->m_frameFlags |= 1;                               // (shown)
    if (parent)
        LinkUnder(b, parent);
    return b;
}
// The bomber's hierarchy (by the world's random numbers: the same on both sides).
void BuildBomber(uint8_t *model)
{
    for (int p = 0; p < PART_COUNT; ++p) {
        const std::string name = PART_NAMES[p];
        const bool must = name.compare(0, 6, "engine") == 0 || name.compare(0, 5, "listy") == 0;
        if (!must && WI(0, 4) == 0)
            continue;
        uint8_t *damaged = Child(model, FRAME_VISUAL, name + "d", model);
        if (must || WI(0, 5))
            Child(model, FRAME_VISUAL, name, model);
        if (WI(0, 1))
            Child(model, FRAME_VISUAL, name + "f", model);
        for (int k = WI(0, 2); k > 0; --k)
            Child(damaged, FRAME_DUMMY, F("%s_piece%d", name.c_str(), k), model);
    }
    Child(model, FRAME_VISUAL, "bluur_l00", model);
    Child(model, FRAME_VISUAL, "bluur_r00", model);
    if (WI(0, 5))
        Child(model, FRAME_DUMMY, "vrtule_l00", model);
    if (WI(0, 5))
        Child(model, FRAME_DUMMY, "vrtule_r00", model);
    if (WI(0, 7))
        Child(model, FRAME_DUMMY, "BODY", model);
    static const char *const props[] = {"SEAT18,name12", "SEAT2", "SEAT8name7", "SEAT", "x", "SEAT0123 name3", ""};
    for (int s = 0; s < C_Junkers88::SEATS; ++s) {
        uint8_t *seat = Child(model, FRAME_DUMMY, F("SEAT%02d", s), model);
        AsFrame(seat)->m_userProps = Text(props[WI(0, 6)]);
        if (WI(0, 1))
            Child(seat, FRAME_DUMMY, F("cameraFPV%d", s), model);
    }
}
// The engine's Open of a model by its file name: the fighter's, the paths', the tracers' and the bomber's frames.
void FillModel(uint8_t *model, const std::string &file)
{
    if (file.find("#aircraftdum") != std::string::npos || file.find("#airfightdum") != std::string::npos) {
        Child(model, FRAME_DUMMY, "dum", model);
    } else if (file.find("WeaponOdds") != std::string::npos) {
        Child(model, FRAME_VISUAL, "trace", model, I3DVISUAL_BILLBOARD);
    } else if (file.find("la_Bf109Canyon") != std::string::npos) {
        // (At least one gun: the original divides by the count when a fighter fires, the port's fix fires none.)
        int guns = 0;
        for (const char *n : {"weapon_r00", "weapon_l00", "weapons_r01", "weapon_l01", "vrtule"})
            if (WI(0, 4)) {
                Child(model, FRAME_DUMMY, n, model);
                ++guns;
            }
        if (!guns)
            Child(model, FRAME_DUMMY, "weapon_r00", model);
        for (const char *n : {"carriage_l00", "carriage_r00", "left_wing", "right_wing", "BODY"})
            if (WI(0, 4))
                Child(model, FRAME_VISUAL, n, model);
    } else if (file.find("ju88") != std::string::npos) {
        BuildBomber(model);
    }
}

// The models' animations (each stage's set and time; Tick advances them), the sets' end times by their names.
struct S_ModelAnim {
    uint32_t anim[4] = {};
    int time[4] = {};
};
std::map<uint32_t, S_ModelAnim> g_anims[2];
int EndTimeOf(const char *file)
{
    uint32_t h = 2166136261u;
    for (const char *c = file ? file : ""; *c; ++c)
        h = (h ^ uint8_t(*c)) * 16777619u;
    return 300 + int(h % 4000);
}

// The paths' dummy models (the port's ~S_Path releases them, the original's leaves them: their references and the
// port's releases of them are left out of the comparison).
std::set<uint32_t> g_pathModels[2];
std::vector<std::string> JkFrameStates()
{
    std::vector<std::string> out;
    for (const S_FakeFrame &f : W().frames) {
        const I3D_frame *x = AsFrame(f.bytes);
        const bool path = g_pathModels[g_side].count(Ptr(f.bytes)) != 0;
        out.push_back(F("frame %s ref %s flags %08x pos ", NameOf(Ptr(f.bytes)).c_str(),
                        path ? "-" : F("%d", int(x->m_refCount)).c_str(), x->m_frameFlags) +
                      VectorText(Ptr(&x->m_localMatrix.m[3][0]), 3) + " rot " + VectorText(Ptr(&x->m_rot), 4) + " scale " +
                      VectorText(Ptr(&x->m_scale), 3) + " parent " +
                      NameOf(Ptr(x->m_parent)) + " name " + (x->m_name ? x->m_name : "") + " on " + F("%d", int(x->m_frameFlags & 1)));
    }
    return out;
}
// A sound request's position's w (+0x0C): the original's is a stack leftover at every call (the port's 0).
std::string MaskSoundW(const std::string &line)
{
    if (line.compare(0, 20, "C_SoundModule::Play(") != 0)
        return line;
    const size_t open = line.find('{'), close = line.find('}', open);
    if (open == std::string::npos || close == std::string::npos)
        return line;
    const size_t last = line.rfind(' ', close);
    return line.substr(0, last + 1) + "-" + line.substr(close);
}
bool IsPathModelRelease(const std::string &line)
{
    for (uint32_t m : g_pathModels[0])
        if (line.compare(0, 19 + NameOf(m).size() + 1, "I3D_frame::Release(" + NameOf(m) + ")") == 0)
            return true;
    return false;
}

// The volume a ray finds (FindTarget): its actor this side's bomber.
alignas(16) uint8_t g_rayVolume[2][0x80];
uint8_t *g_bomber[2];                                   // (the side's bomber: the original's block, the port's object)
C_Junkers88 *PortBomber() { return reinterpret_cast<C_Junkers88 *>(g_bomber[0]); }

void SetUpJunkersAnswers()
{
    // The model cache's Open: the file's frames.
    g_answers["I3D_model::Open"] = [](const std::vector<uint32_t> &a) {
        const std::string file = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        FillModel(reinterpret_cast<uint8_t *>(a[0]), file);
        Mut<const char *>(reinterpret_cast<void *>(a[0]), 0x134) = Text(file);
        if (file.find("#air") != std::string::npos)     // (a path's dummy model: see g_pathModels)
            g_pathModels[g_side].insert(a[0]);
        return file.find("missing") != std::string::npos ? uint32_t(I3DERR_NOFILE) : 0u;
    };
    // A model's copy (the cache's Duplicate): its frames replaced by copies of the source's (names, types, user
    // properties, visual types, places; owned by the copy), its file name.
    g_answers["I3D_model::DuplicateModel"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *dest = reinterpret_cast<I3D_frame *>(a[0]);
        while (dest->m_firstChild) {
            I3D_frame *c = dest->m_firstChild;
            dest->m_firstChild = c->m_nextSibling;
            c->m_parent = nullptr;
            c->m_nextSibling = nullptr;
        }
        std::function<void(I3D_frame *, I3D_frame *)> copy = [&](I3D_frame *from, I3D_frame *to) {
            std::vector<I3D_frame *> kids;
            for (I3D_frame *c = from->m_firstChild; c; c = c->m_nextSibling)
                kids.push_back(c);
            for (size_t i = kids.size(); i-- > 0;) {
                I3D_frame *c = kids[i];
                uint32_t visual = 0;
                for (const S_FakeFrame &f : W().frames)
                    if (f.bytes == reinterpret_cast<uint8_t *>(c))
                        visual = f.visualType;
                uint8_t *b = Child(reinterpret_cast<uint8_t *>(to), c->m_type, c->m_name ? c->m_name : "",
                                   reinterpret_cast<uint8_t *>(dest), visual);
                I3D_frame *n = AsFrame(b);
                n->m_userProps = c->m_userProps;
                n->m_localMatrix = c->m_localMatrix;
                n->m_rot = c->m_rot;
                Name(b, "frame " + std::string(c->m_name ? c->m_name : "") + F(" copy%d", W().created++));
                copy(c, n);
            }
        };
        copy(reinterpret_cast<I3D_frame *>(a[1]), dest);
        const char *file = Mut<const char *>(reinterpret_cast<void *>(a[1]), 0x134);
        Mut<const char *>(dest, 0x134) = file;
        // (A path's dummy model, copied from the cache's: the port releases it with its path, the original leaves it.
        // A copy of a path's model just opened is the cache's own, released alike.)
        if (file && std::strstr(file, "#air") && !g_pathModels[g_side].count(a[1]))
            g_pathModels[g_side].insert(a[0]);
        return 0u;
    };
    // The animation cache's sets: made by the driver, opened by name (its file name kept, +0x0C), released (+4).
    g_answers["I3D_driver::CreateAnimationSet"] = [](const std::vector<uint32_t> &) {
        uint8_t *anim = Block(0x40);
        *reinterpret_cast<void ***>(anim) = EngineVtable("I3D_animation_set");
        Mut<int>(anim, 4) = 1;
        Name(anim, F("anim set %d", W().created++));
        return Ptr(anim);
    };
    g_answers["I3D_animation_set::Open"] = [](const std::vector<uint32_t> &a) {
        Mut<const char *>(reinterpret_cast<void *>(a[0]), 0x0C) = Text(a[1] ? reinterpret_cast<const char *>(a[1]) : "");
        return 0u;
    };
    g_answers["I3D_animation_set::AddRef"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(++Mut<int>(reinterpret_cast<void *>(a[0]), 4));
    };
    g_answers["I3D_animation_set::Release"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(--Mut<int>(reinterpret_cast<void *>(a[0]), 4));
    };
    g_answers["I3D_animation_set::GetEndTime"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(EndTimeOf(Mut<const char *>(reinterpret_cast<void *>(a[0]), 0x0C)));
    };
    g_answers["I3D_model::SetAnimation"] = [](const std::vector<uint32_t> &a) {
        if (a[2] < 4) {
            S_ModelAnim &m = g_anims[g_side][a[0]];
            m.anim[a[2]] = a[1];
            m.time[a[2]] = 0;
        }
        return 0u;
    };
    g_answers["I3D_model::GetAnimation"] = [](const std::vector<uint32_t> &a) {
        return a[1] < 4 ? g_anims[g_side][a[0]].anim[a[1]] : 0u;
    };
    g_answers["I3D_model::GetAnimTime"] = [](const std::vector<uint32_t> &a) {
        return a[1] < 4 ? uint32_t(g_anims[g_side][a[0]].time[a[1]]) : 0u;
    };
    g_answers["I3D_model::SetAnimTime"] = [](const std::vector<uint32_t> &a) {
        if (a[1] < 4)
            g_anims[g_side][a[0]].time[a[1]] = int(a[2]);
        return 0u;
    };
    g_answers["I3D_frame::Tick"] = [](const std::vector<uint32_t> &a) {
        auto it = g_anims[g_side].find(a[0]);
        if (it != g_anims[g_side].end())
            for (int &t : it->second.time)
                t += int(a[1]);
        return 0u;
    };
    // Frames by name under a frame: a trailing '*' (ENUMF_WILDMASK) matches a prefix.
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        const bool wild = !want.empty() && want.back() == '*';
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                const std::string n = c->m_name ? c->m_name : "";
                if (wild ? n.compare(0, want.size() - 1, want, 0, want.size() - 1) == 0 : n == want)
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
    // EnumFrames: the callback on the frame's descendants in the type mask, depth first, listed first.
    g_extraQuiet.insert("I3D_frame::EnumFrames");
    g_answers["I3D_frame::EnumFrames"] = [](const std::vector<uint32_t> &a) {
        using T_Enum = uint32_t(__stdcall *)(I3D_frame *, void *);
        g_log[g_side].push_back(F("I3D_frame::EnumFrames(%s, types %08x)", NameOf(a[0]).c_str(), a[3]));
        std::vector<I3D_frame *> found;
        std::function<void(I3D_frame *)> walk = [&](I3D_frame *f) {
            for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling) {
                if (c->m_type && (a[3] & (1u << (c->m_type - 1))))
                    found.push_back(c);
                walk(c);
            }
        };
        walk(reinterpret_cast<I3D_frame *>(a[0]));
        for (I3D_frame *f : found)
            if (reinterpret_cast<T_Enum>(a[1])(f, reinterpret_cast<void *>(a[2])))
                break;
        return 0u;
    };
    g_answers["I3D_frame::LinkTo"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *c = reinterpret_cast<I3D_frame *>(a[0]);
        // (A link under the frame itself or under one of its children refused, as the engine's: a cycle would
        // send the engine's matrix updates round for ever.)
        for (I3D_frame *p = reinterpret_cast<I3D_frame *>(a[1]); p; p = p->m_parent)
            if (p == c) {
                if (std::getenv("HD2_JK_TRACE"))
                    std::printf("trace LinkTo cycle refused\n");
                return 0x80000001u;
            }
        if (c->m_parent) {
            I3D_frame **link = &c->m_parent->m_firstChild;
            while (*link && *link != c)
                link = &(*link)->m_nextSibling;
            if (*link)
                *link = c->m_nextSibling;
        }
        c->m_parent = nullptr;
        c->m_nextSibling = nullptr;
        if (I3D_frame *p = reinterpret_cast<I3D_frame *>(a[1])) {
            c->m_parent = p;
            c->m_nextSibling = p->m_firstChild;
            p->m_firstChild = c;
        }
        return 0u;
    };
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    g_answers["I3D_frame::AddRef"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(++reinterpret_cast<I3D_frame *>(a[0])->m_refCount);
    };
    // A frame's position (the bombers', fighters' and debris' moves): its local matrix's, its matrices made again.
    g_answers["I3D_frame::SetPos"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        f->m_localMatrix.m[3][0] = pos.x;
        f->m_localMatrix.m[3][1] = pos.y;
        f->m_localMatrix.m[3][2] = pos.z;
        f->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        return 0u;
    };
    // A frame's bound (the fighters' boxes' w): a radius of 2..6 m, made valid.
    g_answers["I3D_frame::UpdateBound"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_bound.radius = float(2 + NextFake() % 5);
        f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
        return 0u;
    };
    // A sound request's file (the chatter's: its name as text; the logger shows the pointer). The slot as the default.
    g_answers["C_SoundModule::Play"] = [](const std::vector<uint32_t> &a) {
        const char *file = *reinterpret_cast<const char *const *>(a[1] + 0x24);
        if (file)
            g_log[g_side].push_back(F("sound file %s", file));
        return uint32_t(FakeInt());
    };
    g_answers["I3D_frame::SetName"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_name = Text(a[1] ? reinterpret_cast<const char *>(a[1]) : "");
        return 0u;
    };
    g_answers["I3D_frame::SetCallback"] = [](const std::vector<uint32_t> &a) {
        reinterpret_cast<I3D_frame *>(a[0])->m_callback = reinterpret_cast<I3D_CALLBACK *>(a[1]);
        return 0u;
    };
    // The visuals the driver makes (the tracers' copies): a world frame of the type.
    g_answers["I3D_driver::CreateVisual"] = [](const std::vector<uint32_t> &a) {
        return Ptr(Child(nullptr, FRAME_VISUAL, F("visual%d", W().created++), nullptr, a[1]));
    };
    g_answers["I3D_driver::CreateFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(Child(nullptr, int(a[1]), F("new%d", W().created++), nullptr));
    };
    // The driver's frames by name (a loaded debris piece's): always found when there (a piece whose frame is not is
    // kept and updated through null, in both programs).
    g_answers["I3D_driver::FindFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(FindByName(reinterpret_cast<const char *>(a[2])));
    };
    // A hierarchy's box (the fighters' volumes): filled and returned (min +0, max +0x10).
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        float *box = reinterpret_cast<float *>(a[1]);
        const float s = float(1 + NextFake() % 4);
        const float b[8] = {-2.0f * s, -0.5f * s, -3.0f * s, 0.0f, 2.0f * s, 0.75f * s, 3.0f * s, 0.0f};
        std::memcpy(box, b, sizeof b);
        return a[1];
    };
    // The sound driver: mostly there.
    g_answers["ISND_driver::IsInitialized"] = [](const std::vector<uint32_t> &) { return uint32_t(NextFake() % 6 != 0); };
    // The rays and spheres: a hit now and then, mostly half way (the static ray's now and then at its start or end).
    g_answers["C_CollisionWorld::RayTestStatic"] = [](const std::vector<uint32_t> &a) {
        if (NextFake() % 3)
            return 0u;
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        const S_vector &dir = *reinterpret_cast<const S_vector *>(a[2]);
        const uint32_t where = NextFake() % 4;
        const float t = where == 0 ? 0.0f : where == 1 ? 1.0f : 0.5f;
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(pos.x + dir.x * t, pos.y + dir.y * t, pos.z + dir.z * t);
        static uint8_t volume[0x80];
        Name(volume, "static volume");
        return Ptr(volume);
    };
    g_answers["CollisionSphereTest"] = [](const std::vector<uint32_t> &a) {
        if (NextFake() % 4)
            return 0u;
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[0]);
        const S_vector &move = *reinterpret_cast<const S_vector *>(a[1]);
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(pos.x + move.x * 0.5f, pos.y + move.y * 0.5f, pos.z + move.z * 0.5f);
        static uint8_t volume[0x80];
        Name(volume, "sphere volume");
        return Ptr(volume);
    };
    g_answers["CollisionRayTest"] = [](const std::vector<uint32_t> &) {
        const uint32_t r = NextFake() % 4;
        if (r == 0)
            return 0u;
        uint8_t *v = g_rayVolume[g_side];
        reinterpret_cast<S_CollisionVolume *>(v)->actor = r == 3 ? nullptr : reinterpret_cast<C_Actor *>(g_bomber[g_side]);
        Name(v, "ray volume");
        return Ptr(v);
    };
    // A human's view direction (FindTarget).
    g_answers["C_Human::GetViewDir"] = [](const std::vector<uint32_t> &) {
        static S_vector dirs[2];
        // (Tiny and zero ones now and then: the normalisation's edges.)
        static const S_vector odd[] = {S_vector(1e-5f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, -1e-5f), S_vector(0.0f, 0.0f, 0.0f),
                                       S_vector(0.0f, 1e-5f, 0.0f), S_vector(1e-5f, 0.0f, -1e-5f), S_vector(0.0f, 1e-5f, -1e-5f),
                                       S_vector(0.0f, 0.0f, 1e-5f)};
        const uint32_t r = NextFake() % 12;
        dirs[g_side] = r < 7 ? odd[r] : S_vector(FakeFloat(), FakeFloat(), FakeFloat());
        Name(&dirs[g_side], "view dir");
        return Ptr(&dirs[g_side]);
    };
}

// ---- the original's bomber ----
constexpr uint32_t O_JK_CTOR = 0x005ef180, O_JK_DTOR = 0x005ef470, O_JK_UPDATE = 0x005f7710, O_JK_TICK = 0x005f76a0,
                   O_JK_READ = 0x005f29f0, O_JK_START = 0x005ef6d0, O_JK_STOP = 0x005efc10, O_JK_RESTART = 0x005edde0,
                   O_JK_SAVE = 0x005ee1e0, O_JK_LOAD = 0x005ee830, O_JK_SET_BLOCKED = 0x005f2e70,
                   O_JK_SET_OCCUPANT = 0x005f2ec0, O_JK_GET_OCCUPANT = 0x005f2fa0, O_JK_FIND_SEAT = 0x005f2fd0,
                   O_JK_LEAVE = 0x005f3000, O_JK_EXIT = 0x005f30c0, O_JK_SEAT_FLAGS = 0x005f2e30,
                   O_JK_SEAT_CAMERA = 0x005f3110, O_JK_SEAT_TEXT = 0x005f3060, O_JK_HAS_WEAPON = 0x005f8370,
                   O_JK_ROUNDS = 0x005f83b0, O_JK_AIM = 0x005f84b0, O_JK_ORIGIN = 0x005f8440,
                   O_JK_WEAPON_DEF = 0x005f8180, O_JK_CONTROLS = 0x005f83e0, O_JK_SHOW_VIEW = 0x005f8490,
                   O_JK_C8 = 0x005f8570, O_JK_FPV_ONLY = 0x005f8540, O_JK_HIT = 0x005f0d70, O_JK_SET_STATE = 0x005f0a90,
                   O_JK_FIND_TARGET = 0x005f0e60, O_JK_PATH_NOTE = 0x005f54c0, O_JK_FIND_FOR = 0x005f0e40;
constexpr uint32_t O_G_JUNKERS = 0x008ac9e4, O_G_ATTACKING = 0x008ac9e8, O_G_SEAT_FLAGS = 0x0083e868;
constexpr uint32_t O_MODEL_CACHE = 0x008aeda0, O_SOUND_CACHE = 0x008aed64, O_ANIM_CACHE = 0x008aed28;

// ---- the compared state: the bomber's fields in each side's layout ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string(); }
std::string Fl(float f) { return FloatText(f); }
// An MSVC 6 vector of pointers at `off` (first +4, last +8).
std::vector<uint32_t> OVec(const uint8_t *p, uint32_t off)
{
    std::vector<uint32_t> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += 4)
        out.push_back(*reinterpret_cast<const uint32_t *>(a));
    return out;
}
std::vector<int> OVecInt(const uint8_t *p, uint32_t off)
{
    std::vector<int> out;
    for (uint32_t w : OVec(p, off))
        out.push_back(int(w));
    return out;
}
std::string PartIndex(uint32_t part, bool orig)
{
    if (!part)
        return "none";
    return F("part%d", orig ? At<int>(reinterpret_cast<void *>(part), 0x38)
                            : reinterpret_cast<const C_Junkers88::S_Part *>(part)->index);
}
std::string PartList(const std::vector<uint32_t> &parts, bool orig)
{
    std::string s = "[";
    for (uint32_t p : parts)
        s += " " + PartIndex(p, orig);
    return s + " ]";
}
template <class T> std::vector<uint32_t> PVec(const std::vector<T *> &v)
{
    std::vector<uint32_t> out;
    for (T *p : v)
        out.push_back(Ptr(p));
    return out;
}
std::string PathText(uint32_t path, bool orig)
{
    if (!path)
        return "path none";
    const uint8_t *p = reinterpret_cast<const uint8_t *>(path);
    if (orig)
        return F("path owner %s model %s index %d name %s follow %d", NameOf(At<uint32_t>(p, 0)).c_str(),
                 NameOf(At<uint32_t>(p, 4)).c_str(), At<int>(p, 8), RepText(At<uint32_t>(p, 0xC)).c_str(), int(p[0x10]));
    const auto *x = reinterpret_cast<const C_Junkers88::S_Path *>(path);
    return F("path owner %s model %s index %d name %s follow %d", NameOf(Ptr(x->owner)).c_str(), NameOf(Ptr(x->model)).c_str(),
             x->index, x->name.c_str(), int(x->follow));
}
std::string FallText(const uint8_t *f)
{
    return "fall " + V3(reinterpret_cast<const float *>(f)) + F(" %s falling %d radius ", NameOf(At<uint32_t>(f, 0x10)).c_str(),
                                                                  int(f[0x14])) +
           Fl(At<float>(f, 0x18));
}
std::vector<std::string> BomberState()
{
    std::vector<std::string> out;
    const bool orig = g_side != 0;
    uint8_t *b = g_bomber[g_side];
    if (!b) {
        out.push_back("no bomber");
        return out;
    }
    if (orig) {
        out.push_back("velocity " + V3(FP(b, 0x90)) + F(" hits %d", At<int>(b, 0xA0)));
        for (int i = 0; i < 6; ++i) {
            const uint8_t *c = b + 0xA4 + 0x20 * i;
            std::string s = F("chatter %d: min %d delay %d range %d next %d sounds", i, At<int>(c, 0), At<int>(c, 4),
                              At<int>(c, 8), At<int>(c, 0xC));
            for (int id : OVecInt(c, 0x10))
                s += F(" %x", id);
            out.push_back(s);
        }
        for (int s = 0; s < C_Junkers88::SEATS; ++s) {
            const uint8_t *x = b + 0x164 + 0x10 * s;
            out.push_back(F("seat %d blocked %d occupant %s frame %s flags %x", s, int(x[0]), NameOf(At<uint32_t>(x, 4)).c_str(),
                            NameOf(At<uint32_t>(x, 8)).c_str(), At<uint32_t>(x, 0xC)));
        }
        std::string sw = "seat weapons";
        for (uint32_t w : OVec(b, 0x1B4))
            sw += " " + NameOf(w);
        out.push_back(sw);
        out.push_back("camera " + PathText(At<uint32_t>(b, 0x1C4), true));
        for (uint32_t f : OVec(b, 0x1C8)) {
            const uint8_t *x = reinterpret_cast<const uint8_t *>(f);
            std::string s = F("fighter model %s attacking %d down %d inWorld %d gun %s motor %s smoke %d %d fire %d hits %d %d "
                              "path %d health ",
                              NameOf(At<uint32_t>(x, 0xA0)).c_str(), int(x[0xA8]), int(x[0xA9]), int(x[0xAA]),
                              NameOf(At<uint32_t>(x, 0xC0)).c_str(), NameOf(At<uint32_t>(x, 0xC4)).c_str(), At<int>(x, 0xC8),
                              At<int>(x, 0xCC), At<int>(x, 0xF0), At<int>(x, 0xF4), At<int>(x, 0xF8), At<int>(x, 0xFC)) +
                            Fl(At<float>(x, 0x100)) + " guns";
            for (int g = 0; g < 5; ++g)
                s += " " + NameOf(At<uint32_t>(x, 0xAC + 4 * g));
            s += " gun frames";
            for (uint32_t g : OVec(x, 0x104))
                s += " " + NameOf(g);
            out.push_back(s);
            out.push_back("  " + PathText(At<uint32_t>(x, 0xA4), true) + " " + FallText(x + 0xD0) + " inv " +
                          VectorText(Ptr(x), 16));
            const S_CollisionVolume &v = *reinterpret_cast<const S_CollisionVolume *>(x + 0x40);
            out.push_back("  volume centre " + V3(&v.center.x) + " " + Fl(v.centerW) +
                          F(" flags %x type %x radius ", v.flags, v.type) + Fl(v.radius));
        }
        for (uint32_t d : OVec(b, 0x1D8)) {
            const uint8_t *x = reinterpret_cast<const uint8_t *>(d);
            out.push_back("debris " + V3(FP(x, 0)) + " spin " + V3(FP(x, 0x10)) + " floor " +
                          Fl(At<float>(x, 0x20)) + " " + NameOf(At<uint32_t>(x, 0x24)));
        }
        for (uint32_t g : {0x1E8u, 0x210u, 0x238u})
            out.push_back(F("group left %d initial %d parts ", At<int>(b, g), At<int>(b, g + 4)) +
                          PartList(OVec(b, g + 8), true) + " broken " + PartList(OVec(b, g + 0x18), true));
        out.push_back("dependent " + PartList(OVec(b, 0x260), true));
        for (uint32_t p : OVec(b, 0x270)) {
            const uint8_t *x = reinterpret_cast<const uint8_t *>(p);
            out.push_back(F("part%d %s %s %s stages %d engine %d outer %d hide %d blades %d stage %d hanging ", At<int>(x, 0x38),
                            NameOf(At<uint32_t>(x, 0)).c_str(), NameOf(At<uint32_t>(x, 4)).c_str(),
                            NameOf(At<uint32_t>(x, 8)).c_str(), At<int>(x, 0xC), int(x[0x10]), int(x[0x11]), int(x[0x12]),
                            At<int>(x, 0x14), At<int>(x, 0x3C)) +
                          PartList(OVec(x, 0x18), true) + " thrown " + PartList(OVec(x, 0x28), true));
        }
        out.push_back("health " + Fl(At<float>(b, 0x280)) + " firepower " + Fl(At<float>(b, 0x284)) + " interceptors " +
                      Fl(At<float>(b, 0x288)) + " rate " + Fl(At<float>(b, 0x28C)));
        out.push_back(F("blur %s %s blades %s %s engines %s %s", NameOf(At<uint32_t>(b, 0x290)).c_str(),
                        NameOf(At<uint32_t>(b, 0x294)).c_str(), PartIndex(At<uint32_t>(b, 0x298), true).c_str(),
                        PartIndex(At<uint32_t>(b, 0x29C), true).c_str(), PartIndex(At<uint32_t>(b, 0x2A0), true).c_str(),
                        PartIndex(At<uint32_t>(b, 0x2A4), true).c_str()));
        std::string snd = "sounds";
        for (int i = 0; i < 6; ++i)
            snd += " " + NameOf(At<uint32_t>(b, 0x2A8 + 4 * i));
        out.push_back(snd + F(" propellers %s %s body %s down %d %d", NameOf(At<uint32_t>(b, 0x2C0)).c_str(),
                              NameOf(At<uint32_t>(b, 0x2C4)).c_str(), NameOf(At<uint32_t>(b, 0x2C8)).c_str(), int(b[0x2CC]),
                              int(b[0x2CD])));
        out.push_back(FallText(b + 0x2D0) + " slow " + Fl(At<float>(b, 0x2F0)) + " " + Fl(At<float>(b, 0x2F4)) + " trace " +
                      NameOf(At<uint32_t>(b, 0x2F8)));
        for (uint32_t off : {0x2FCu, 0x30Cu}) {
            std::string s = off == 0x2FC ? "tracers" : "pool";
            for (uint32_t t : OVec(b, off))
                s += " " + NameOf(At<uint32_t>(reinterpret_cast<void *>(t), 0)) + " " + Fl(At<float>(reinterpret_cast<void *>(t), 4));
            out.push_back(s);
        }
        out.push_back("properties " + Fl(At<float>(b, 0x31C)) + " " + Fl(At<float>(b, 0x320)) + " " + Fl(At<float>(b, 0x324)) +
                      " " + Fl(At<float>(b, 0x328)) + F(" %x", At<int>(b, 0x32C)));
        out.push_back(F("globals bomber %d attacking %d input %d hud %d", int(*OriginalAt<uint32_t *>(O_G_JUNKERS) == Ptr(b)),
                        *OriginalAt<int *>(O_G_ATTACKING), int(At<uint8_t>(g_origGame, 0x6E)),
                        int((*OriginalAt<uint8_t **>(O_GAME_PROFILE + 0x0C))[0])));
        return out;
    }
    const C_Junkers88 *j = PortBomber();
    out.push_back("velocity " + V3(&j->m_velocity.x) + F(" hits %d", j->m_hitsByOthers));
    for (int i = 0; i < 6; ++i) {
        const auto &c = j->m_chatter[i];
        std::string s = F("chatter %d: min %d delay %d range %d next %d sounds", i, c.minDelay, c.delay, c.range, c.next);
        for (int id : c.sounds)
            s += F(" %x", id);
        out.push_back(s);
    }
    for (int s = 0; s < C_Junkers88::SEATS; ++s) {
        const auto &x = j->m_seats[s];
        out.push_back(F("seat %d blocked %d occupant %s frame %s flags %x", s, int(x.blocked), NameOf(Ptr(x.occupant)).c_str(),
                        NameOf(Ptr(x.frame)).c_str(), x.flags));
    }
    std::string sw = "seat weapons";
    for (C_SeatWeapon *w : j->m_seatWeapons)
        sw += " " + NameOf(Ptr(w));
    out.push_back(sw);
    out.push_back("camera " + PathText(Ptr(j->m_camera), false));
    for (const C_Junkers88::S_Fighter *x : j->m_fighters) {
        std::string s = F("fighter model %s attacking %d down %d inWorld %d gun %s motor %s smoke %d %d fire %d hits %d %d "
                          "path %d health ",
                          NameOf(Ptr(x->model)).c_str(), int(x->attacking), int(x->down), int(x->inWorld), NameOf(Ptr(x->gun)).c_str(),
                          NameOf(Ptr(x->motor)).c_str(), x->smoke[0], x->smoke[1], x->fireTime, x->hitTime[0], x->hitTime[1],
                          x->pathIndex) +
                        Fl(x->health) + " guns";
        for (I3D_sound *g : x->guns)
            s += " " + NameOf(Ptr(g));
        s += " gun frames";
        for (I3D_frame *g : x->gunFrames)
            s += " " + NameOf(Ptr(g));
        out.push_back(s);
        out.push_back("  " + PathText(Ptr(x->path), false) + " " + FallText(reinterpret_cast<const uint8_t *>(&x->fall)) +
                      " inv " + VectorText(Ptr(&x->invMatrix), 16));
        out.push_back("  volume centre " + V3(&x->volume.center.x) + " " + Fl(x->volume.centerW) +
                      F(" flags %x type %x radius ", x->volume.flags, x->volume.type) + Fl(x->volume.radius));
    }
    for (const C_Junkers88::S_Debris *x : j->m_debris)
        out.push_back("debris " + V3(&x->velocity.x) + " spin " + V3(&x->spin.x) + " floor " + Fl(x->floor) + " " +
                      NameOf(Ptr(x->frame)));
    for (const auto *g : {&j->m_damage, &j->m_engines, &j->m_outer})
        out.push_back(F("group left %d initial %d parts ", g->left, g->initial) + PartList(PVec(g->parts), false) + " broken " +
                      PartList(PVec(g->broken), false));
    out.push_back("dependent " + PartList(PVec(j->m_dependent), false));
    for (const C_Junkers88::S_Part *x : j->m_parts)
        out.push_back(F("part%d %s %s %s stages %d engine %d outer %d hide %d blades %d stage %d hanging ", x->index,
                        NameOf(Ptr(x->frames[0])).c_str(), NameOf(Ptr(x->frames[1])).c_str(), NameOf(Ptr(x->frames[2])).c_str(),
                        x->stages, int(x->engine), int(x->outer), int(x->hideIntact), x->blades, x->stage) +
                      PartList(PVec(x->hanging), false) + " thrown " + PartList(PVec(x->thrown), false));
    out.push_back("health " + Fl(j->m_health) + " firepower " + Fl(j->m_firepower) + " interceptors " + Fl(j->m_interceptorHealth) +
                  " rate " + Fl(j->m_damageRate));
    out.push_back(F("blur %s %s blades %s %s engines %s %s", NameOf(Ptr(j->m_blur[0])).c_str(), NameOf(Ptr(j->m_blur[1])).c_str(),
                    PartIndex(Ptr(j->m_bladeParts[0]), false).c_str(), PartIndex(Ptr(j->m_bladeParts[1]), false).c_str(),
                    PartIndex(Ptr(j->m_engineParts[0]), false).c_str(), PartIndex(Ptr(j->m_engineParts[1]), false).c_str()));
    std::string snd = "sounds";
    for (I3D_sound *s : j->m_sounds)
        snd += " " + NameOf(Ptr(s));
    out.push_back(snd + F(" propellers %s %s body %s down %d %d", NameOf(Ptr(j->m_propeller[0])).c_str(),
                          NameOf(Ptr(j->m_propeller[1])).c_str(), NameOf(Ptr(j->m_body)).c_str(), int(j->m_engineDown[0]),
                          int(j->m_engineDown[1])));
    out.push_back(FallText(reinterpret_cast<const uint8_t *>(&j->m_fall)) + " slow " + Fl(j->m_slowTime) + " " +
                  Fl(j->m_slowFraction) + " trace " + NameOf(Ptr(j->m_trace)));
    for (const auto *list : {&j->m_tracers, &j->m_tracerPool}) {
        std::string s = list == &j->m_tracers ? "tracers" : "pool";
        for (const C_Junkers88::S_Tracer *t : *list)
            s += " " + NameOf(Ptr(t->frame)) + " " + Fl(t->distance);
        out.push_back(s);
    }
    out.push_back("properties " + Fl(j->m_maxHealth) + " " + Fl(j->m_fighterHealth) + " " + Fl(j->m_fighterDamage) + " " +
                  Fl(j->m_gunFov) + F(" %x", j->m_unknown32C));
    out.push_back(F("globals bomber %d attacking %d input %d hud %d", int(g_junkers88 == j), g_attackingFighters,
                    int(reinterpret_cast<C_game *>(g_portGameBuf)->m_playerInputEnabled),
                    int(*reinterpret_cast<const uint8_t *>(g_gameProfile.m_hudOption))));
    return out;
}

// ---- one case ----
// The chunk writer (the properties).
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
    template <class T> void EntryOf(uint16_t id, const T &v)
    {
        Begin(id);
        out.append(reinterpret_cast<const char *>(&v), sizeof v);
        End();
    }
};
enum {
    OP_UPDATE, OP_TICK, OP_HIT, OP_SET_STATE, OP_SEAT, OP_FIND_TARGET, OP_NOTE, OP_SAVE_LOAD, OP_RESTART, OP_STOP_START,
    OP_KINDS
};
const char *const OP_KIND_NAMES[] = {"Update", "Tick", "Hit", "SetState", "Seat", "FindTarget", "PathNote", "SaveLoad",
                                     "Restart", "StopStart"};
struct S_JkOp {
    int kind = 0;
    int a = 0, b = 0, c = 0, d = 0;
    float f = 0.0f;
};
struct S_JkCase {
    std::vector<S_JkOp> ops;
    std::string props;                                  // (a properties chunk's entries, or none)
    bool readProps = false;
    bool serverStart = false;                           // StartMission(true) first
    int chatterRange = 1;                               // (group 4's pause range: the original divides by it)
    uint32_t role = GAME_ROLE_LOCAL;
    int time = 0;
    bool cutscene = false;
    int cutsceneId = 0;
    int playMode = 0;
    int oddHeading = 0;                                 // (1..7: the bomber's z axis tiny, zero or odd)
    bool cameraAtOrigin = false;                        // (the tracers' sounds near the camera)
};
int g_jkCases, g_opCounts[OP_KINDS];
// (Odd ones too: the timers land on 1.)
const int FRAME_TIMES[] = {0, 10, 16, 33, 100, 250, 1000, 3000, 7000, 1, 19, 999};
S_JkCase RandomJkCase()
{
    S_JkCase c;
    c.readProps = P(0.7);
    if (c.readProps) {
        S_Writer w;
        w.Begin(0xAE24);
        for (int i = I(0, 6); i > 0; --i) {
            static const uint16_t ids[] = {100, 200, 300, 400, 500};
            const uint16_t id = ids[I(0, 4)];
            if (id == 500)
                w.EntryOf(id, I(0, 0x2000));
            else {
                static const float values[] = {1000.0f, 50.0f, 10.0f, 30.0f, 1.0471976f, 200.0f, 0.0f, 5.0f};
                w.EntryOf(id, values[I(0, 7)]);
            }
        }
        // (Now and then a chunk after the properties. Not an unknown entry of one byte at their end: both programs enter
        // it and read a header across its end, the original then hangs.)
        w.End();
        if (P(0.15))
            w.EntryOf(uint16_t(100), 7.0f);
        c.props = w.out;
    }
    c.serverStart = P(0.2);
    c.chatterRange = I(1, 500);
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    // (The fade's and the attack's thresholds exactly now and then.)
    static const int edges[] = {9000, 9001, 10000, 10001, 11000, 11001};
    c.time = P(0.1) ? edges[I(0, 5)] : P(0.3) ? I(0, 12000) : I(9000, 60000);
    c.cutscene = P(0.15);
    c.cutsceneId = P(0.5) ? 31 : I(-1, 40);
    c.playMode = I(0, 3);
    c.oddHeading = P(0.2) ? I(1, 7) : 0;
    c.cameraAtOrigin = P(0.4);
    const int count = P(0.1) ? I(80, 160) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_JkOp op;
        const int r = I(0, 99);
        op.kind = r < 35 ? OP_UPDATE : r < 42 ? OP_TICK : r < 55 ? OP_HIT : r < 63 ? OP_SET_STATE : r < 80 ? OP_SEAT
                : r < 84 ? OP_FIND_TARGET : r < 91 ? OP_NOTE : r < 95 ? OP_SAVE_LOAD : r < 97 ? OP_RESTART : OP_STOP_START;
        op.a = I(0, 1000);
        op.b = I(-1, 5);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        static const float amounts[] = {0.0f, 1.0f, 5.0f, 50.0f, 1000.0f, 3.0f, 10.0f};
        op.f = amounts[I(0, 6)];
        c.ops.push_back(op);
    }
    return c;
}

// The side's bomber made (the original's by its constructor), its frame the world's model, group 4's pause range.
uint8_t *g_bomberModel[2];
void MakeBomber(const S_JkCase &c)
{
    uint8_t *b = Block(0x340);
    if (g_side) {
        OriginalAt<T_Void>(O_JK_CTOR)(b);
        Mut<uint8_t *>(b, 0x20) = g_bomberModel[1];
        Mut<int>(b, 0xA4 + 4 * 0x20 + 8) = c.chatterRange;
    } else {
        b = Block(sizeof(C_Junkers88) + 16);
        C_Junkers88 *j = new (b) C_Junkers88();
        j->m_frame = AsFrame(g_bomberModel[0]);
        j->m_chatter[4].range = c.chatterRange;
    }
    g_bomber[g_side] = b;
    Name(b, "bomber");
}
// The side's seat weapons by index (the original's vector, the port's).
int WeaponCount()
{
    return g_side ? int(OVec(g_bomber[1], 0x1B4).size()) : int(PortBomber()->m_seatWeapons.size());
}
bool HasWeapon(int seat)
{
    if (seat < 0)
        return false;
    if (g_side) {
        const std::vector<uint32_t> w = OVec(g_bomber[1], 0x1B4);
        return size_t(seat) < w.size() && w[size_t(seat)];
    }
    return size_t(seat) < PortBomber()->m_seatWeapons.size() && PortBomber()->m_seatWeapons[size_t(seat)];
}
bool HasCamera() { return g_side ? At<uint32_t>(g_bomber[1], 0x1C4) != 0 : PortBomber()->m_camera != nullptr; }
// The side's fighters' models and their paths' models.
std::vector<uint32_t> FighterModels(bool paths)
{
    std::vector<uint32_t> out;
    if (g_side) {
        for (uint32_t f : OVec(g_bomber[1], 0x1C8)) {
            if (!paths)
                out.push_back(At<uint32_t>(reinterpret_cast<void *>(f), 0xA0));
            else if (uint32_t p = At<uint32_t>(reinterpret_cast<void *>(f), 0xA4))
                out.push_back(At<uint32_t>(reinterpret_cast<void *>(p), 4));
        }
    } else {
        for (const C_Junkers88::S_Fighter *f : PortBomber()->m_fighters) {
            if (!paths)
                out.push_back(Ptr(f->model));
            else if (f->path)
                out.push_back(Ptr(f->path->model));
        }
    }
    return out;
}
I3D_frame *NthChild(uint32_t frame, int n)
{
    if (!frame)
        return nullptr;
    std::vector<I3D_frame *> kids;
    for (I3D_frame *c = reinterpret_cast<I3D_frame *>(frame)->m_firstChild; c; c = c->m_nextSibling)
        kids.push_back(c);
    return kids.empty() ? nullptr : kids[size_t(n) % kids.size()];
}
uint8_t *HumanActor(int n)
{
    const auto &humans = W().humans;
    return humans.empty() ? nullptr : W().actors[size_t(humans[size_t(n) % humans.size()])].bytes;
}
void AdvanceTime(int t)
{
    if (g_side)
        Mut<uint32_t>(g_origGame, 0x2C) += uint32_t(t);
    else
        reinterpret_cast<C_game *>(g_portGameBuf)->m_time += uint32_t(t);
}

std::string g_previousSave[2];                          // (each side's last save of the case)
std::map<std::string, int> g_savedHighestPart[2];      // (each save's highest part index in the groups)
using T_JkInt = void(TC *)(void *, int);
using T_JkBool = void(TC *)(void *, int);
using T_JkChunk = bool(TC *)(void *, void *);
std::string RunJkOp(const S_JkOp &op, int index)
{
    const bool o = g_side != 0;
    uint8_t *b = g_bomber[g_side];
    C_Junkers88 *j = PortBomber();
    const int t = FRAME_TIMES[op.a % 12];
    switch (op.kind) {
    case OP_UPDATE:
        AdvanceTime(t);
        if (o)
            OriginalAt<T_JkInt>(O_JK_UPDATE)(b, t);
        else
            j->Update(t);
        return F("Update %d", t);
    case OP_TICK:
        if (o)
            OriginalAt<T_JkInt>(O_JK_TICK)(b, t);
        else
            j->Tick(t);
        return F("Tick %d", t);
    case OP_HIT: {
        // A part of a fighter's model (or of the bomber's), by a human.
        const std::vector<uint32_t> models = FighterModels(false);
        I3D_frame *part = models.empty() || op.c % 5 == 0 ? NthChild(Ptr(g_bomberModel[g_side]), op.d)
                                                          : NthChild(models[size_t(op.c) % models.size()], op.d);
        uint8_t *attacker = HumanActor(op.a);
        if (!part || !attacker)
            return "Hit (none)";
        const S_vector pos(1.0f, 2.0f, 3.0f), dir(0.0f, 0.0f, 1.0f);
        if (o)
            OriginalAt<void(TC *)(void *, int, float, const S_vector *, const S_vector *, void *, int)>(O_JK_HIT)(
                b, op.b & 3, op.f, &pos, &dir, attacker, int(Ptr(part)));
        else
            j->Hit(E_HitType(op.b & 3), op.f, pos, dir, reinterpret_cast<C_Actor *>(attacker), int(Ptr(part)));
        return F("Hit %s by %s ", NameOf(Ptr(part)).c_str(), NameOf(Ptr(attacker)).c_str()) + Fl(op.f);
    }
    case OP_SET_STATE: {
        const int count = op.a % 10;
        if (o)
            OriginalAt<T_JkInt>(O_JK_SET_STATE)(b, count);
        else
            j->SetState(count);
        return F("SetState %d", count);
    }
    case OP_SEAT: {
        const int seat = op.b;
        const int which = op.c % 19;
        uint8_t *human = HumanActor(op.a);
        switch (which) {
        case 0: {
            const bool blocked = op.d & 1;
            const bool r = o ? OriginalAt<bool(TC *)(void *, int, int)>(O_JK_SET_BLOCKED)(b, seat, blocked)
                             : j->SetSeatBlocked(seat, blocked);
            return F("SetSeatBlocked %d %d -> %d", seat, int(blocked), int(r));
        }
        case 1:
            if (!human)
                return "SetSeatOccupant (none)";
            if (o)
                OriginalAt<void(TC *)(void *, int, void *)>(O_JK_SET_OCCUPANT)(b, seat, human);
            else
                j->SetSeatOccupant(seat, reinterpret_cast<C_Actor *>(human));
            return F("SetSeatOccupant %d %s", seat, NameOf(Ptr(human)).c_str());
        case 2: {
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_JK_GET_OCCUPANT)(b, seat)) : Ptr(j->GetSeatOccupant(seat));
            return F("GetSeatOccupant %d -> %s", seat, NameOf(r).c_str());
        }
        case 3: {
            const int r = o ? OriginalAt<int(TC *)(void *, const void *)>(O_JK_FIND_SEAT)(b, human)
                            : j->FindSeat(reinterpret_cast<C_Actor *>(human));
            return F("FindSeat %s -> %d", NameOf(Ptr(human)).c_str(), r);
        }
        case 4: {
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_JK_LEAVE)(b, seat)) : Ptr(j->LeaveSeat(seat));
            return F("LeaveSeat %d -> %s", seat, NameOf(r).c_str());
        }
        case 5: {
            const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_JK_EXIT)(b, seat) : j->ExitSeat(seat);
            return F("ExitSeat %d -> %d", seat, int(r));
        }
        case 6: {
            const uint32_t r = o ? OriginalAt<uint32_t(TC *)(void *, int)>(O_JK_SEAT_FLAGS)(b, seat) : j->GetSeatFlags(seat);
            return F("GetSeatFlags %d -> %x", seat, r);
        }
        case 7: {
            if (!HasCamera())
                return "GetSeatCamera (no camera)";
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_JK_SEAT_CAMERA)(b, seat)) : Ptr(j->GetSeatCamera(seat));
            return F("GetSeatCamera %d -> %s", seat, NameOf(r).c_str());
        }
        case 8: {
            const int r = o ? OriginalAt<int(TC *)(void *, int)>(O_JK_SEAT_TEXT)(b, seat) : j->GetSeatText(seat);
            return F("GetSeatText %d -> %d", seat, r);
        }
        case 9: {
            const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_JK_HAS_WEAPON)(b, seat) : j->HasSeatWeapon(seat);
            return F("HasSeatWeapon %d -> %d", seat, int(r));
        }
        case 10: {
            const int r = o ? OriginalAt<int(TC *)(void *, int)>(O_JK_ROUNDS)(b, seat) : j->GetSeatWeaponRounds(seat);
            return F("GetSeatWeaponRounds %d -> %d", seat, r);
        }
        case 11:
        case 12: {
            S_vector v(9.0f, 9.0f, 9.0f);
            const bool aim = which == 11;
            if (o)
                OriginalAt<void *(TC *)(void *, S_vector *, int)>(aim ? O_JK_AIM : O_JK_ORIGIN)(b, &v, seat);
            else
                v = aim ? j->GetSeatWeaponAim(seat) : j->GetSeatWeaponOrigin(seat);
            return F("GetSeatWeapon%s %d -> ", aim ? "Aim" : "Origin", seat) + V3(&v.x);
        }
        case 13: {
            // (Only with a weapon there: the original calls through the seat's entry, whatever HasSeatWeapon says.)
            if (!HasWeapon(seat))
                return "GetSeatWeaponDef (none)";
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_JK_WEAPON_DEF)(b, seat)) : Ptr(j->GetSeatWeaponDef(seat));
            return F("GetSeatWeaponDef %d -> %s", seat, NameOf(r).c_str());
        }
        case 14: {
            float axes[2] = {0.5f, -0.25f}, buttons[2] = {1.0f, 0.0f};
            if (o)
                OriginalAt<void(TC *)(void *, int, float *, float *)>(O_JK_CONTROLS)(b, seat, axes, buttons);
            else
                j->ApplySeatControls(seat, axes, buttons);
            return F("ApplySeatControls %d", seat);
        }
        case 15: {
            const bool on = op.d & 1;
            if (o)
                OriginalAt<void(TC *)(void *, int, int)>(O_JK_SHOW_VIEW)(b, seat, on);
            else
                j->ShowSeatView(seat, on);
            return F("ShowSeatView %d %d", seat, int(on));
        }
        case 16: {
            int x = -7, y = -7, z = -7;
            const bool r = o ? OriginalAt<bool(TC *)(void *, int, int *, int *, int *)>(O_JK_C8)(b, seat, &x, &y, &z)
                             : j->UnknownC8(seat, &x, &y, &z);
            return F("UnknownC8 %d -> %d %d %d %d", seat, int(r), x, y, z);
        }
        case 17: {
            // (Only with a weapon there: without one the original leaves its flag in the FPU's place.)
            if (!HasWeapon(seat))
                return "UnknownAC (none)";
            const float r = o ? OriginalAt<float(TC *)(void *, int)>(0x005f8500)(b, seat) : j->UnknownAC(seat);
            return F("UnknownAC %d -> ", seat) + Fl(r);
        }
        default: {
            const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_JK_FPV_ONLY)(b, seat) : j->IsSeatFirstPersonOnly(seat);
            return F("IsSeatFirstPersonOnly %d -> %d", seat, int(r));
        }
        }
    }
    case OP_FIND_TARGET: {
        uint8_t *human = HumanActor(op.a);
        if (!human)
            return "FindTarget (none)";
        I3D_frame *frame = reinterpret_cast<I3D_frame *>(0x1234);
        uint32_t r;
        if (op.c & 1)                                   // (FindJunkersTargetFor: __fastcall in the original)
            r = o ? Ptr(OriginalAt<void *(__fastcall *)(void *, I3D_frame **)>(O_JK_FIND_FOR)(human, &frame))
                  : Ptr(FindJunkersTargetFor(reinterpret_cast<C_Human *>(human), &frame));
        else
            r = o ? Ptr(OriginalAt<void *(TC *)(void *, void *, I3D_frame **)>(O_JK_FIND_TARGET)(b, human, &frame))
                  : Ptr(j->FindTarget(reinterpret_cast<C_Human *>(human), &frame));
        return F("FindTarget %s -> %s %s", NameOf(Ptr(human)).c_str(), NameOf(r).c_str(),
                 Ptr(frame) == 0x1234 ? "unset" : NameOf(Ptr(frame)).c_str());
    }
    case OP_NOTE: {
        // A note of a fighter's path (its model's first child), or of the bomber's camera's.
        const std::vector<uint32_t> paths = FighterModels(true);
        I3D_frame *frame = paths.empty() ? NthChild(Ptr(g_bomberModel[g_side]), 0) : NthChild(paths[size_t(op.a) % paths.size()], 0);
        if (!frame)
            return "PathNote (none)";
        static const char *const notes[] = {"fire_start", "xx fire_end", "other", nullptr, "fire_start_now"};
        // (A fire note only on a fighter's path: on another frame the original calls its fighter through null.)
        const char *note = notes[paths.empty() ? 2 + op.c % 2 : op.c % 5];
        const uint32_t message = op.d % 4 ? 0x1000u : 0x10u;
        const int r = o ? OriginalAt<int(__stdcall *)(void *, uint32_t, uintptr_t, uintptr_t)>(O_JK_PATH_NOTE)(
                              frame, message, reinterpret_cast<uintptr_t>(note), 0)
                        : C_Junkers88::PathNote(frame, message, reinterpret_cast<uintptr_t>(note), 0);
        return F("PathNote %s %x %s -> %d", NameOf(Ptr(frame)).c_str(), message, note ? note : "null", r);
    }
    case OP_SAVE_LOAD: {
        const std::string file = F("jk_save%d.sav", index);
        // (The highest part index the groups hold, and the parts there are: a load of indices past them reads past the
        // original's parts, the port's fix skips them.)
        int highest = -1;
        size_t partCount;
        if (o) {
            for (uint32_t g : {0x1E8u, 0x210u, 0x238u})
                for (uint32_t list : {g + 8, g + 0x18})
                    for (uint32_t x : OVec(b, list))
                        highest = std::max(highest, At<int>(reinterpret_cast<void *>(x), 0x38));
            partCount = OVec(b, 0x270).size();
        } else {
            for (const auto *g : {&j->m_damage, &j->m_engines, &j->m_outer})
                for (const auto *list : {&g->parts, &g->broken})
                    for (const auto *x : *list)
                        highest = std::max(highest, x->index);
            partCount = j->m_parts.size();
        }
        g_savedHighestPart[g_side][file] = highest;
        C_chunk save;
        save.Open(file.c_str(), true);
        const bool rs = o ? OriginalAt<T_JkChunk>(O_JK_SAVE)(b, &save) : j->SaveGame(save);
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        std::string hex;
        for (unsigned char ch : bytes)
            hex += F("%02x", ch);
        // (Loaded back now and then: the bomber's lists grow by the saved fighters, debris and tracers. Or a made
        // file: another chunk first; the bomber's chunk with an entry of another kind among its own.)
        // (Its own file: the case's save before this one, when there is one (this one just made restores what the bomber
        // has already).)
        bool rl = false;
        const int load = op.c % 4;
        const std::string previous = g_previousSave[g_side];
        g_previousSave[g_side] = file;
        if (load) {
            std::string name = previous.empty() ? file : previous;
            if (load >= 2) {
                S_Writer w;
                if (load == 2) {
                    w.EntryOf(0x1234, 5);
                } else {
                    w.Begin(1000);
                    w.EntryOf(0x7777, 3);
                    // (The engines' list: only a load fills it, the engines being outer parts too. With more engines
                    // left than there are and the health low, the next update breaks them.)
                    w.EntryOf(7000, (op.d & 1) ? 1.0f : 250.0f);
                    w.EntryOf(20000, 1);
                    w.EntryOf(0x4E84, 0.5f);
                    if (op.d & 1) {
                        const int engines[5] = {3 + op.d % 3, 2, (op.d / 2) % 3, (op.d / 6) % 3, 0};
                        w.EntryOf(15000, engines);
                    }
                    w.End();
                }
                name = F("jk_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            if (g_savedHighestPart[g_side].count(name) && g_savedHighestPart[g_side][name] >= int(partCount))
                return F("SaveGame -> %d (a load of parts past the bomber's)", int(rs));
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            rl = o ? OriginalAt<T_JkChunk>(O_JK_LOAD)(b, &chunk) : j->LoadGame(chunk);
            chunk.Close();
        }
        return F("SaveGame -> %d LoadGame %d -> %d bytes ", int(rs), load, int(rl)) + hex;
    }
    case OP_RESTART: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_JkBool>(O_JK_RESTART)(b, server);
        else
            j->RestartMission(server);
        return F("RestartMission %d", int(server));
    }
    default:
        if (o) {
            OriginalAt<T_JkBool>(O_JK_STOP)(b, 0);
            OriginalAt<T_JkBool>(O_JK_START)(b, 0);
        } else {
            j->StopMission(false);
            j->StartMission(false);
        }
        return "StopMission StartMission";
    }
}

void ClearJkCaches()
{
    if (g_side) {
        OriginalAt<T_Void>(0x006d77e0)(OriginalAt<void *>(O_MODEL_CACHE));
        OriginalAt<T_Void>(0x006d87a0)(OriginalAt<void *>(O_SOUND_CACHE));
        OriginalAt<T_Void>(0x006d9890)(OriginalAt<void *>(O_ANIM_CACHE));
    } else {
        g_modelCache.Clear();
        g_soundCache.Clear();
        g_animationCache.Clear();
    }
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("jk_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_jkShow;
void RunJkCase(const S_JkCase &c)
{
    g_caseStart = GetTickCount();
    ++g_jkCases;
    g_holdFrees = true;
    g_current = F("case %d", g_jkCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_overrides[Lower("jk_props.bin")] = c.props;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_world[side].frames.reserve(65536);   // (long cases: many tracers and model copies)
        g_texts[side].clear();
        g_anims[side].clear();
        g_pathModels[side].clear();
        g_previousSave[side].clear();
        g_savedHighestPart[side].clear();
        g_sw[side] = S_SwState();
        BuildWorld(side, worldSeed);
        g_sndDriver[side] = Block(0x40);
        *reinterpret_cast<void ***>(g_sndDriver[side]) = EngineVtable("ISND_driver");
        Name(g_sndDriver[side], "sound driver");
        // The bomber's model.
        uint8_t *model = Child(nullptr, FRAME_MODEL, "ju88", nullptr);
        Mut<const char *>(model, 0x134) = Text("ju88");
        BuildBomber(model);
        g_bomberModel[side] = model;
        if (c.oddHeading) {
            static const float rows[7][3] = {{1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1e-5f}, {0.0f, 0.0f, 0.0f}, {1e-5f, 0.0f, 1e-5f},
                                             {0.0f, 0.0f, 1e-5f}, {1e-5f, 1.0f, 1e-5f}, {5.0f, 0.0f, 5.0f}};
            for (int k = 0; k < 3; ++k)
                AsFrame(model)->m_localMatrix.m[2][k] = rows[c.oddHeading - 1][k];
        }
        if (c.cameraAtOrigin)
            if (uint8_t *camera = *reinterpret_cast<uint8_t **>(W().scene + 0x15C))
                for (int k = 0; k < 3; ++k)
                    AsFrame(camera)->m_localMatrix.m[3][k] = 0.0f;
        // The game: its time, the cutscene, the HUD option, the play mode's difficulty, the role.
        static uint8_t hud[2][0x10];
        hud[side][0] = 0;
        Name(hud[side], "hud option");
        if (side) {
            Mut<uint32_t>(g_origGame, 0x2C) = uint32_t(c.time);
            *OriginalAt<uint8_t *>(O_SCRIPT_MODULE + 0x824) = c.cutscene;
            *OriginalAt<int *>(O_SCRIPT_MODULE + 0x834) = c.cutsceneId;
            *OriginalAt<uint8_t **>(O_GAME_PROFILE + 0x0C) = hud[1];
            *reinterpret_cast<int *>(*OriginalAt<uint8_t **>(O_GAME_PROFILE + 0x28) + 0x0C) = c.playMode;
            *OriginalAt<int *>(O_G_ATTACKING) = 0;
        } else {
            reinterpret_cast<C_game *>(g_portGameBuf)->m_time = uint32_t(c.time);
            g_scriptModule.m_cutsceneRunning = c.cutscene;
            g_scriptModule.m_cutscene = c.cutsceneId;
            g_gameProfile.m_hudOption = reinterpret_cast<S_HudOption *>(hud[0]);
            g_gameProfile.m_playMode = *reinterpret_cast<S_PlayMode **>(g_playModeStorage);
            g_gameProfile.m_playMode->unknown0C = c.playMode;
            g_attackingFighters = 0;
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        MakeBomber(c);
        uint8_t *b = g_bomber[side];
        if (c.readProps) {
            C_chunk props;
            props.Open("jk_props.bin", false);
            uint16_t id;
            props.Descend(id);
            const bool ok = side ? OriginalAt<T_JkChunk>(O_JK_READ)(b, &props) : PortBomber()->ReadProperties(props);
            props.Close();
            out.push_back(F("ReadProperties -> %d", int(ok)));
        }
        if (c.serverStart) {
            if (side)
                OriginalAt<T_JkBool>(O_JK_START)(b, 1);
            else
                PortBomber()->StartMission(true);
        }
        if (side)
            OriginalAt<T_JkBool>(O_JK_START)(b, 0);
        else
            PortBomber()->StartMission(false);
        for (const std::string &s : BomberState())
            out.push_back(s);
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            // (HD2_JK_TRACE=<case>: that case's operations printed as they start.)
            static const int trace = std::getenv("HD2_JK_TRACE") ? std::atoi(std::getenv("HD2_JK_TRACE")) : -1;
            if (trace == g_jkCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i),
                            OP_KIND_NAMES[c.ops[i].kind], c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunJkOp(c.ops[i], int(i)));
            if (trace == g_jkCases) {
                std::printf("trace   -> %s\n", out.back().substr(0, 160).c_str());
                std::fflush(stdout);
            }
            for (const std::string &s : BomberState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        if (side) {
            OriginalAt<T_JkBool>(O_JK_STOP)(b, 0);
            OriginalAt<T_Void>(O_JK_DTOR)(b);
        } else {
            PortBomber()->StopMission(false);
            PortBomber()->~C_Junkers88();
        }
        out.push_back(F("after: bomber global %s", side ? NameOf(*OriginalAt<uint32_t *>(O_G_JUNKERS)).c_str()
                                                     : NameOf(Ptr(g_junkers88)).c_str()));
        // The creator: a bomber made and destroyed (the original's creator at 0x008ac9e0, its Create and Destroy).
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(0x005edd20)(OriginalAt<void *>(0x008ac9e0));
            Name(made, "created bomber");
            out.push_back(F("created, global %s", NameOf(*OriginalAt<uint32_t *>(O_G_JUNKERS)).c_str()));
            OriginalAt<void(TC *)(void *, void *)>(0x005edda0)(OriginalAt<void *>(0x008ac9e0), made);
            out.push_back(F("destroyed, global %s", NameOf(*OriginalAt<uint32_t *>(O_G_JUNKERS)).c_str()));
        } else {
            C_Actor *made = g_junkers88Creator.Create();
            Name(made, "created bomber");
            out.push_back(F("created, global %s", NameOf(Ptr(g_junkers88)).c_str()));
            g_junkers88Creator.Destroy(made);
            out.push_back(F("destroyed, global %s", NameOf(Ptr(g_junkers88)).c_str()));
        }
        g_case = "";
        for (const std::string &s : JkFrameStates())
            out.push_back(s);
        // (The animation sets' releases left out on both sides: the port's S_Path::Next releases the set it plays,
        // the original keeps the cache's extra reference.)
        for (const std::string &s : g_log[side])
            if ((side || !IsPathModelRelease(s)) && s.compare(0, 27, "I3D_animation_set::Release(") != 0)
                out.push_back("call " + MaskSoundW(s));
        ClearJkCaches();
        g_bomber[side] = nullptr;
    }
    static const int only = std::getenv("HD2_JK_ONLY") ? std::atoi(std::getenv("HD2_JK_ONLY")) : 0;
    if (g_jkShow && (!only || only == g_jkCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_jkShow == 2)
            for (const std::string &s : r.orig)
                std::printf("  orig: %s\n", s.c_str());
    }
    Compare(g_current, r);
    ReleaseHeldFrees();
    if (!g_overrun.empty()) {
        ++failures;
        std::printf("FAIL %s: the original wrote past a block\n", g_overrun.c_str());
        g_overrun.clear();
    }
    FreeWorld(0);
    FreeWorld(1);
}

using T_AddDir = void(TC *)(void *, const char *);
void SetUpJunkers()
{
    SetUpCmd();
    SetUpJunkersAnswers();
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
        {0x007e531a, reinterpret_cast<const void *>(&O_ISndGetDriver)},
        {0x006e5380, reinterpret_cast<const void *>(&O_TextsGet)},
        // The seat weapons.
        {0x004c19d0, reinterpret_cast<const void *>(&O_SwCtor)},
        {0x004c1aa0, reinterpret_cast<const void *>(&O_SwDtor)},
        {0x004c1ad0, reinterpret_cast<const void *>(&O_SwInit)},
        {0x004c6dd0, reinterpret_cast<const void *>(&O_SwSetGunner)},
        {0x004c6550, reinterpret_cast<const void *>(&O_SwGetRounds)},
        {0x004c57b0, reinterpret_cast<const void *>(&O_SwGetItemDef)},
        {0x004c5870, reinterpret_cast<const void *>(&O_SwGetAim)},
        {0x004c5800, reinterpret_cast<const void *>(&O_SwGetOrigin)},
        {0x004c3b60, reinterpret_cast<const void *>(&O_SwApplyControls)},
        {0x004c6d90, reinterpret_cast<const void *>(&O_SwShowView)},
        {0x004c7670, reinterpret_cast<const void *>(&O_SwC8)},
        {0x004c56a0, reinterpret_cast<const void *>(&O_SwUpdate)},
        {0x004c55e0, reinterpret_cast<const void *>(&O_SwTick)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    // The caches (both sides' real; the original's built here).
    if (*OriginalAt<uint32_t *>(0x0098bcc0) != 2) {
        InitializeCriticalSection(OriginalAt<CRITICAL_SECTION *>(0x0098bca8));
        *OriginalAt<uint32_t *>(0x0098bcc0) = 2;
    }
    OriginalAt<void (*)()>(0x006d5030)();
    OriginalAt<void (*)()>(0x006d5370)();
    OriginalAt<T_Void>(0x006d5850)(OriginalAt<void *>(O_ANIM_CACHE));
    for (uint32_t cache : {O_MODEL_CACHE, O_SOUND_CACHE, O_ANIM_CACHE}) {
        *OriginalAt<uint32_t *>(cache + 0x34) = 256;
        uint8_t *buf = static_cast<uint8_t *>(O_New(8 * 256));
        *OriginalAt<uint8_t **>(cache + 0x18) = buf;
        *OriginalAt<uint8_t **>(cache + 0x1C) = buf;
        *OriginalAt<uint8_t **>(cache + 0x20) = buf + 8 * 256;
    }
    const struct {
        uint32_t cache;
        const char *dir;
    } dirs[] = {{O_MODEL_CACHE, "models\\"}, {O_SOUND_CACHE, "sounds\\"}, {O_ANIM_CACHE, "anims\\"}};
    for (const auto &d : dirs)
        reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(d.cache))[0x18 / 4])(OriginalAt<void *>(d.cache), d.dir);
    g_modelCache.AddDir("models\\");
    g_soundCache.AddDir("sounds\\");
    g_animationCache.AddDir("anims\\");
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("jk_diff: main");
    std::fflush(stdout);
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("jk_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_JK_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_JK_SHOW"))
        g_jkShow = std::atoi(s) ? std::atoi(s) : 1;
    g_dump = std::getenv("HD2_JK_DUMP") != nullptr;
    int cases = std::getenv("HD2_JK_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_JK_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpJunkers();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunJkCase(RandomJkCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("jk_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_jkCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
