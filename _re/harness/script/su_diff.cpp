// Supply box part 1: the supply boxes (game/supply_box.cpp, the original's 0x005f9700..0x005fd9f0) against the original
// executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the boxes' calls go
// through su_calls.s / su_calls.inc (gen_cmd_calls.py su). The model cache is real on both sides (game/resource_cache.cpp;
// the original's inline OpenInto on its own cache, built here): "cratedummy" with its "letibedna", "parachute" with its
// "morph"; the animation cache, the vertex animations, the sweeps and rays, the effects and the sounds are logged and
// answered; the LS3DF math (the inverse, the scale, the rotation from a matrix, the products) is LS3DF.dll's.
//
// Each case builds the same world on each side (cmd_diff's) and one or two boxes: model frames under the primary sector
// (turned) with their walls ("Steny02", an object visual whose mesh's vertices give the feet; now and then missing or
// another visual), the "Ammo00".."Ammo13" parts (now and then some missing), makes the boxes on each side (the
// original's by its constructor 0x005f97d0 in a block of 0x260, the port's by its own) and runs the same random sequence
// of operations in the case's role: SetFrame, properties read (lists of up to ten, cut short) and written, CopyFrom,
// starts, stops, restarts, SetEnabled, charges taken, updates and ticks through every state (the plane, the fall swept
// and rayed onto static and dynamic volumes, the landing, the parachute, the opening), the clients' messages, the
// network state, the saved game's calls. After each operation the answer, every box's state (every field, its volume)
// and every call made into the game and the engine are compared; the properties' and the saves' bytes too (the state's
// stack leftovers masked). The creator makes and destroys one.
// MinGW; build_su.bat; runs su_diff.exe ("su_diff: N checks, 0 failures"). Env: HD2_SU_CASES (default 1,500;
// HD2_SU_QUICK: 300), HD2_SU_SEED, HD2_SU_SHOW (1: the port's lines; 2: both), HD2_SU_ONLY (the case shown),
// HD2_SU_TRACE (a case's operations as they start).
#define HARNESS_NAME "su_diff"
#define CMD_CALLS_INC "su_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/collision.h"
#include "game/game_texts.h"
#include "game/resource_cache.h"
#include "game/supply_box.h"
#include "game/vertex_anim.h"
#undef protected
#undef private

extern float g_scanMax[3];

// ---- the port's side of what the boxes call and the test provides ----
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
bool __stdcall S_matrix::Inverse(const S_matrix &src)
{
    static const auto f = Ls3dfFn<bool(__stdcall *)(S_matrix *, const S_matrix &)>("?Inverse@S_matrix@@QAG_NABU1@@Z");
    return f(this, src);
}
S_vector &__stdcall S_vector::operator*=(const S_matrix &m)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_vector *, const S_matrix &)>("??XS_vector@@QAGAAU0@ABUS_matrix@@@Z");
    f(this, m);
    return *this;
}
S_vector __stdcall S_matrix::GetScale() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, float *)>("?GetScale@S_matrix@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
// The port's allocations zeroed, as the original's (its _aligned_malloc redirected to a zeroing one).
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
// The actors' creators (the boxes' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the box's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}

namespace H {
std::string TextOf(int id) { return F("T%X", id); }
}  // namespace H
C_str C_GameTexts::Get(int id) const
{
    H::g_log[H::g_side].push_back(H::F("texts %d", id));
    return C_str(H::TextOf(id).c_str());
}

namespace H {

#define OT __attribute__((thiscall))
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
std::deque<std::string> g_texts[2];
const char *Text(const std::string &s)
{
    g_texts[g_side].push_back(s);
    return g_texts[g_side].back().c_str();
}
uint8_t *Child(uint8_t *parent, int type, const std::string &name, uint8_t *owner)
{
    uint8_t *b = NewFrame(type, name);
    S_FakeFrame &f = W().frames.back();
    I3D_frame *x = AsFrame(b);
    x->m_name = f.name.c_str();
    x->m_ownerModel = reinterpret_cast<I3D_frame *>(owner);
    x->m_frameFlags |= 1;
    if (parent)
        LinkUnder(b, parent);
    return b;
}

// ---- the boxes ----
constexpr int MAX_BOXES = 2;
uint8_t *g_box[2][MAX_BOXES];
uint8_t *g_boxFrame[2][MAX_BOXES];
int g_boxCount;
C_SupplyBox *PortBox(int i) { return reinterpret_cast<C_SupplyBox *>(g_box[0][i]); }
constexpr uint32_t O_SU_CTOR = 0x005f97d0, O_SU_DTOR = 0x005f9880, O_SU_ENABLE = 0x005fa290, O_SU_UPDATE = 0x005fb380,
                   O_SU_TICK = 0x005fbd20, O_SU_MESSAGE = 0x005fa3f0, O_SU_SET_FRAME = 0x005fa6a0, O_SU_WRITE = 0x005fc050,
                   O_SU_READ = 0x005fc200, O_SU_COPY = 0x005fbec0, O_SU_START = 0x005fac80, O_SU_STOP = 0x005fb220,
                   O_SU_RESTART = 0x005fc480, O_SU_SAVE = 0x005fc530, O_SU_LOAD = 0x005fc640, O_SU_WRITE_STATE = 0x005fd560,
                   O_SU_READ_STATE = 0x005fd640;
constexpr uint32_t O_SU_CREATOR = 0x008ac9ec, O_SU_CREATE = 0x005f9730, O_SU_DESTROY = 0x005f97b0;
constexpr uint32_t O_MODEL_CACHE = 0x008aeda0;

// The messages' words (ReadBits answers them in turn on each side); the quantized vectors read (in turn).
std::deque<uint32_t> g_bits[2];
std::deque<S_vector4> g_quantized[2];
// The fall: whether the sweep touches, and the rays' and the sphere's hits (by turns); their volumes.
int g_sweepEvery = 2;
uint8_t *g_staticVolume[2];
uint8_t *g_dynamicVolume[2];
uint8_t *g_animation[2];                                // (the vertex animation "ammobox", or none)
bool g_hasAnimation = true;

void SetUpBoxAnswers()
{
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    // A frame's position: its local matrix's, its matrices made again (its children's too).
    g_answers["I3D_frame::SetPos"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        f->m_localMatrix.m[3][0] = pos.x;
        f->m_localMatrix.m[3][1] = pos.y;
        f->m_localMatrix.m[3][2] = pos.z;
        std::function<void(I3D_frame *)> stale = [&](I3D_frame *x) {
            x->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
            for (I3D_frame *c = x->m_firstChild; c; c = c->m_nextSibling)
                stale(c);
        };
        stale(f);
        return 0u;
    };
    g_answers["I3D_frame::LinkTo"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *c = reinterpret_cast<I3D_frame *>(a[0]);
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
        c->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        return 0u;
    };
    // Frames by name under a frame (wildcards as the original's masks ask: none here), any type.
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                if ((c->m_name ? c->m_name : "") == want)
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
    // A hierarchy's box: filled and returned (min +0, max +0x10), by turns (none empty).
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        float *box = reinterpret_cast<float *>(a[1]);
        static const float boxes[][6] = {{-0.4f, 0.0f, -0.3f, 0.4f, 0.8f, 0.3f}, {-0.5f, 0.0f, -0.25f, 0.5f, 0.6f, 0.25f},
                                         {-0.4f, -0.1f, -0.3f, 0.4f, 0.8f, 0.3f}};
        const float *b = boxes[NextFake() % 3];
        const float v[8] = {b[0], b[1], b[2], 0.0f, b[3], b[4], b[5], 0.0f};
        std::memcpy(box, v, sizeof v);
        return a[1];
    };
    // The models: "cratedummy" with its "letibedna", "parachute" with its "morph" (an object visual); others missing.
    g_answers["I3D_model::Open"] = [](const std::vector<uint32_t> &a) {
        const std::string file = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        uint8_t *model = reinterpret_cast<uint8_t *>(a[0]);
        if (file.find("cratedummy") != std::string::npos) {
            Child(model, FRAME_DUMMY, "letibedna", model);
            return 0u;
        }
        if (file.find("parachute") != std::string::npos) {
            Child(model, FRAME_VISUAL, "morph", model);
            W().frames.back().visualType = I3DVISUAL_OBJECT;
            return 0u;
        }
        return uint32_t(I3DERR_NOFILE);
    };
    g_answers["I3D_model::DuplicateModel"] = [](const std::vector<uint32_t> &a) {
        std::function<void(I3D_frame *, I3D_frame *)> copy = [&](I3D_frame *from, I3D_frame *to) {
            for (I3D_frame *c = from->m_firstChild; c; c = c->m_nextSibling) {
                uint32_t type = 0;
                for (S_FakeFrame &f : W().frames)
                    if (f.bytes == reinterpret_cast<uint8_t *>(c))
                        type = f.visualType;
                uint8_t *b = NewFrame(c->m_type, c->m_name ? c->m_name : "");
                AsFrame(b)->m_name = W().frames.back().name.c_str();
                W().frames.back().visualType = type;
                LinkUnder(b, reinterpret_cast<uint8_t *>(to));
                copy(c, AsFrame(b));
            }
        };
        copy(reinterpret_cast<I3D_frame *>(a[1]), reinterpret_cast<I3D_frame *>(a[0]));
        return 0u;
    };
    // The animations' ends: by turns.
    g_answers["I3D_animation_set::GetEndTime"] = [](const std::vector<uint32_t> &) {
        static const uint32_t ends[] = {500, 1500, 900, 2000};
        return ends[NextFake() % 4];
    };
    g_answers["I3D_model::IsAnimActive"] = [](const std::vector<uint32_t> &) { return NextFake() % 3 ? 1u : 0u; };
    g_answers["I3D_model::GetAnimTime"] = [](const std::vector<uint32_t> &) { return uint32_t(NextFake() % 1200); };
    // The vertex animation "ammobox" (its levels) or none.
    g_answers["C_VertexAnimModule::FindAnimation"] = [](const std::vector<uint32_t> &) { return g_hasAnimation ? Ptr(g_animation[g_side]) : 0u; };
    // The walls' mesh: its level, its vertices.
    g_answers["I3D_mesh::GetLevel"] = [](const std::vector<uint32_t> &a) { return At<uint32_t>(reinterpret_cast<void *>(a[0]), 0x100); };
    g_answers["I3D_mesh_level::GetVertices"] = [](const std::vector<uint32_t> &a) { return At<uint32_t>(reinterpret_cast<void *>(a[0]), 0x100); };
    // The fall's sweep touches by the case's rate; rays and the sphere hit static ground, a dynamic volume or nothing.
    g_answers["C_CollisionWorld::TestBoxSweep"] = [](const std::vector<uint32_t> &) {
        return NextFake() % uint32_t(g_sweepEvery) == 0 ? 1u : 0u;
    };
    auto hitTest = [](uint32_t posArg, uint32_t dirArg, uint32_t hitArg) {
        return [posArg, dirArg, hitArg](const std::vector<uint32_t> &a) -> uint32_t {
            const uint32_t r = NextFake() % 6;
            if (r < 2)
                return 0u;
            const S_vector &from = *reinterpret_cast<const S_vector *>(a[posArg]);
            const S_vector &dir = *reinterpret_cast<const S_vector *>(a[dirArg]);
            static const float parts[] = {0.0f, 0.3f, 0.9f, 1.0f};
            if (S_vector *hit = reinterpret_cast<S_vector *>(a[hitArg]))
                *hit = S_vector(from.x + dir.x * parts[r - 2], from.y + dir.y * parts[r - 2], from.z + dir.z * parts[r - 2]);
            return Ptr(r == 5 ? g_dynamicVolume[g_side] : g_staticVolume[g_side]);
        };
    };
    g_answers["CollisionRayTest"] = hitTest(0, 1, 2);
    g_answers["CollisionSphereTest"] = hitTest(0, 1, 3);
    // The messages: the world's bit stream; their words in turn; the quantized vectors in turn.
    g_answers["CreateActorMessage"] = [](const std::vector<uint32_t> &) { return Ptr(W().bitstream); };
    g_answers["C_BitStream::ReadBits"] = [](const std::vector<uint32_t> &) {
        if (g_bits[g_side].empty())
            return 0u;
        const uint32_t w = g_bits[g_side].front();
        g_bits[g_side].pop_front();
        return w;
    };
    g_answers["C_NetMessage::WriteQuantized"] = [](const std::vector<uint32_t> &a) {
        // (Three: the crate's w is a stale word in the original's state.)
        g_log[g_side].push_back("  quantized " + VectorText(a[1], 3));
        return 1u;
    };
    g_answers["C_NetMessage::ReadQuantized"] = [](const std::vector<uint32_t> &a) {
        S_vector4 v = {0.0f, 0.0f, 0.0f, 0.0f};
        if (!g_quantized[g_side].empty()) {
            v = g_quantized[g_side].front();
            g_quantized[g_side].pop_front();
        }
        std::memcpy(reinterpret_cast<void *>(a[1]), &v, 16);
        return 0u;
    };
}

// The crushed actor's Hit (the original's vtable slot 0xD4, the port's 0xD8: both here), logged.
void OT B_ActorHit(void *actor, int type, float amount, const S_vector *pos, const S_vector *dir, void *attacker, int part)
{
    g_log[g_side].push_back(F("%s Hit(%d, ", NameOf(Ptr(actor)).c_str(), type) + FloatText(amount) + " " + VectorText(Ptr(pos), 3) + " " +
                            VectorText(Ptr(dir), 3) +
                            F(", %s, %d)", NameOf(Ptr(attacker)).c_str(), part));
}
void *g_crushedVtable[0x100];
void SetUpCrushedVtable()
{
    g_crushedVtable[0xD4 / 4] = reinterpret_cast<void *>(&B_ActorHit);
    g_crushedVtable[0xD8 / 4] = reinterpret_cast<void *>(&B_ActorHit);
}

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
std::string Fl(float f) { return FloatText(f); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string(); }
std::string FrameText(const I3D_frame *f)
{
    if (!f)
        return "none";
    return F("%s flags %08x at ", f->m_name ? f->m_name : "", f->m_frameFlags) + VectorText(Ptr(&f->m_localMatrix.m[3][0]), 3) +
           " rot " + VectorText(Ptr(&f->m_rot), 4) + " parent " + NameOf(Ptr(f->m_parent));
}
std::string VolumeText(const void *p, const void *matrix, const void *inv)
{
    if (!p)
        return "volume none";
    const S_CollisionVolume &v = *static_cast<const S_CollisionVolume *>(p);
    return F("volume flags %x type %x frame %s actor %s matrix %s inv %s radius ", v.flags, v.type, NameOf(Ptr(v.frame)).c_str(),
             NameOf(Ptr(v.actor)).c_str(), v.matrix == matrix ? "own" : NameOf(Ptr(v.matrix)).c_str(),
             v.invMatrix == inv ? "own" : NameOf(Ptr(v.invMatrix)).c_str()) +
           Fl(v.radius) + " centre " + V4(&v.center.x) + " box " + V3(&v.bbox.min.x) + " " + V3(&v.bbox.max.x);
}
std::vector<int> OIntVec(const uint8_t *p, uint32_t off)
{
    std::vector<int> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += 4)
        out.push_back(*reinterpret_cast<const int *>(a));
    return out;
}
std::vector<std::string> BoxState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_box[g_side][i];
    const C_SupplyBox *p = PortBox(i);
    const bool o = g_side != 0;
#define FIELD(T, member, off) (o ? At<T>(b, off) : T(p->member))
    const std::vector<int> axis = o ? OIntVec(b, 0xFC) : p->m_axisItems, allies = o ? OIntVec(b, 0x10C) : p->m_alliedItems;
    std::string lists = " axis";
    for (int x : axis)
        lists += F(" %x", x);
    lists += " allies";
    for (int x : allies)
        lists += F(" %x", x);
    std::string ammo;
    for (int k = 0; k < 14; ++k)
        ammo += " " + NameOf(o ? At<uint32_t>(b, 0xC4 + 4 * k) : Ptr(p->m_ammo[k]));
    out.push_back(F("box%d started %d walls %s lists %d shown %x flags %x max %d capacity %d charges %d respawn %d health %d text %x "
                    "dummy %s crate %s plane %s flying %d parachute %s blend %d stage %d anim %d state %d time %d",
                    i, int(FIELD(uint8_t, m_started, 0x90)), NameOf(o ? At<uint32_t>(b, 0xC0) : Ptr(p->m_walls)).c_str(),
                    FIELD(int, m_hasItemLists, 0x11C), FIELD(uint32_t, m_shownFlags, 0x120), FIELD(uint32_t, m_flags, 0x124),
                    FIELD(int, m_maxCarried, 0x128), FIELD(int, m_capacity, 0x12C), FIELD(int, m_charges, 0x130),
                    FIELD(int, m_respawnTime, 0x134), FIELD(int, m_health, 0x138), FIELD(int, m_textId, 0x210),
                    NameOf(o ? At<uint32_t>(b, 0x218) : Ptr(p->m_crateDummy)).c_str(), NameOf(o ? At<uint32_t>(b, 0x21C) : Ptr(p->m_flyingCrate)).c_str(),
                    NameOf(o ? At<uint32_t>(b, 0x220) : Ptr(p->m_plane)).c_str(), int(FIELD(uint8_t, m_planeFlying, 0x224)),
                    NameOf(o ? At<uint32_t>(b, 0x228) : Ptr(p->m_parachute)).c_str(), FIELD(int, m_blendTime, 0x244),
                    FIELD(int, m_animStage, 0x248), FIELD(int, m_anim, 0x24C), FIELD(int, m_state, 0x250), FIELD(int, m_stateTime, 0x254)) +
                  lists + " ammo" + ammo);
    std::string anims;
    for (int k = 0; k < 6; ++k)
        anims += " " + NameOf(o ? At<uint32_t>(b, 0x22C + 4 * k) : Ptr(p->m_anims[k]));
    // (The feet's w: a stale local in the original, left out.)
    std::string feet;
    for (int k = 0; k < 4; ++k)
        feet += " " + V3(o ? FP(b, 0x1D0 + 0x10 * k) : &p->m_feet[k].x);
    out.push_back("  drop " + V4(o ? FP(b, 0xA0) : &p->m_dropPos.x) + " rot " + V4(o ? FP(b, 0xB0) : reinterpret_cast<const float *>(&p->m_dropRot)) +
                  " feet" + feet + " anims" + anims + " name " +
                  (o ? RepText(At<uint32_t>(b, 0x80)) : std::string(p->m_name.c_str() ? p->m_name.c_str() : "")));
    // The morph player (its layout the same on both sides): max level, start frame, frame time, its flags.
    if (const uint8_t *m = reinterpret_cast<const uint8_t *>(o ? At<uint32_t>(b, 0x214) : Ptr(p->m_morphPlayer)))
        out.push_back(F("  morph max %d start %d frameTime %d pingPong %d stopped %d loop %d", At<int>(m, 0), At<int>(m, 8), At<int>(m, 0x18),
                        int(m[0x1C]), int(m[0x1E]), int(m[0x1F])));
    out.push_back("  matrices " + VectorText(o ? Ptr(b + 0x140) : Ptr(&p->m_boxMatrix), 16) + " " +
                  VectorText(o ? Ptr(b + 0x180) : Ptr(&p->m_boxInvMatrix), 16));
    out.push_back("  " + VolumeText(reinterpret_cast<const void *>(o ? At<uint32_t>(b, 0x1C0) : Ptr(p->m_volume)),
                                    o ? static_cast<const void *>(b + 0x140) : static_cast<const void *>(&p->m_boxMatrix),
                                    o ? static_cast<const void *>(b + 0x180) : static_cast<const void *>(&p->m_boxInvMatrix)));
#undef FIELD
    return out;
}
std::vector<std::string> BoxesState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_boxCount; ++i)
        for (const std::string &s : BoxState(i))
            out.push_back(s);
    for (int i = 0; i < g_boxCount; ++i) {
        out.push_back(F("frame%d ", i) + FrameText(AsFrame(g_boxFrame[g_side][i])));
        const uint8_t *b = g_box[g_side][i];
        const uint32_t dummy = g_side ? At<uint32_t>(b, 0x218) : Ptr(PortBox(i)->m_crateDummy);
        const uint32_t chute = g_side ? At<uint32_t>(b, 0x228) : Ptr(PortBox(i)->m_parachute);
        const uint32_t plane = g_side ? At<uint32_t>(b, 0x220) : Ptr(PortBox(i)->m_plane);
        if (dummy)
            out.push_back("  dummy " + FrameText(reinterpret_cast<const I3D_frame *>(dummy)));
        if (chute)
            out.push_back("  parachute " + FrameText(reinterpret_cast<const I3D_frame *>(chute)));
        if (plane)
            out.push_back("  plane " + FrameText(reinterpret_cast<const I3D_frame *>(plane)));
    }
    return out;
}

// ---- one case ----
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
    void Raw(const void *p, size_t n) { out.append(static_cast<const char *>(p), n); }
};
enum { OP_SET_FRAME, OP_READ, OP_WRITE, OP_COPY, OP_START, OP_STOP, OP_RESTART, OP_ENABLE, OP_CHARGE, OP_UPDATE, OP_TICK, OP_MESSAGE,
       OP_NET_STATE, OP_SAVE_LOAD, OP_DAMAGE, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"SetFrame", "ReadProperties", "WriteProperties", "CopyFrom", "Start", "Stop", "Restart", "SetEnabled",
                                     "UseCharge", "Update", "Tick", "Message", "NetState", "SaveLoad", "Damage"};
struct S_SuOp {
    int kind = 0;
    int box = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_SuBox {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    int walls = 0;                                      // (0 an object visual, 1 another visual, 2 none)
    int odd = 0;                                        // (1..3: its z axis tiny along x, along z, zero)
    int scale = 0;                                      // (0 none; 1..3 scaled along an axis or all)
    float upperY = 0.8f;                                // the walls' upper vertices (about the feet's limit)
    int vertexOrder = 0;
    uint32_t ammoMissing = 0;
};
struct S_SuCase {
    std::vector<S_SuBox> boxes;
    std::vector<S_SuOp> ops;
    uint32_t role = GAME_ROLE_LOCAL;
    int sweepEvery = 2;
    bool animation = true;
    float scanMax = 100.0f;
};
int g_suCases, g_opCounts[OP_KINDS];
const int FRAME_TIMES[] = {0, 16, 100, 350, 1000, 2000, 3000, 5001, 5000, 1, 250, 10000};
S_SuCase RandomSuCase()
{
    S_SuCase c;
    const int boxes = P(0.75) ? 1 : 2;
    for (int i = 0; i < boxes; ++i) {
        S_SuBox l;
        l.x = float(I(-20, 20)) * 0.5f;
        l.y = float(I(-4, 4)) * 0.5f;
        l.z = float(I(-20, 20)) * 0.5f;
        l.heading = float(I(0, 628)) * 0.01f;
        l.walls = P(0.9) ? 0 : I(1, 2);
        l.odd = P(0.1) ? I(1, 3) : 0;
        l.scale = P(0.25) ? I(1, 3) : 0;
        static const float uppers[] = {0.8f, 0.5f, 0.35f, 0.4f, 0.3f, 0.25f};
        l.upperY = P(0.5) ? 0.8f : uppers[I(0, 5)];
        l.vertexOrder = I(0, 5);
        l.ammoMissing = P(0.2) ? uint32_t(I(0, 0x3FFF)) : 0;
        c.boxes.push_back(l);
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    c.sweepEvery = I(1, 4);
    c.animation = P(0.7);
    c.scanMax = P(0.5) ? 100.0f : 50.0f;
    const int count = P(0.1) ? I(40, 120) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_SuOp op;
        const int r = I(0, 99);
        op.kind = r < 2 ? OP_SET_FRAME : r < 6 ? OP_READ : r < 8 ? OP_WRITE : r < 10 ? OP_COPY : r < 14 ? OP_START : r < 16 ? OP_STOP
                : r < 18 ? OP_RESTART : r < 24 ? OP_ENABLE : r < 34 ? OP_CHARGE : r < 64 ? OP_UPDATE : r < 76 ? OP_TICK
                : r < 84 ? OP_MESSAGE : r < 90 ? OP_NET_STATE : r < 97 ? OP_SAVE_LOAD : OP_DAMAGE;
        op.box = I(0, boxes - 1);
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // SetFrame, the properties, a start: first, mostly.
    for (int i = boxes; i-- > 0;) {
        S_SuOp start;
        start.kind = OP_START;
        start.box = i;
        start.c = I(0, 3);
        if (P(0.9))
            c.ops.insert(c.ops.begin(), start);
        S_SuOp read;
        read.kind = OP_READ;
        read.box = i;
        read.a = I(0, 1000);
        read.b = I(0, 1000);
        read.c = I(0, 1000);
        read.d = I(0, 1000) | 1;
        if (P(0.85))
            c.ops.insert(c.ops.begin(), read);
        S_SuOp set;
        set.kind = OP_SET_FRAME;
        set.box = i;
        c.ops.insert(c.ops.begin(), set);
    }
    return c;
}

bool Started(int i) { return g_side ? g_box[1][i][0x90] != 0 : PortBox(i)->m_started; }
// (Not before SetFrame and a start: the box's models and its volume are made by StartMission; the game starts every
// actor before it is updated, saved or sent.)
bool Ready(int i)
{
    const bool started = g_side ? g_box[1][i][0x90] != 0 : PortBox(i)->m_started;
    const bool volume = g_side ? At<uint32_t>(g_box[1][i], 0x1C0) != 0 : PortBox(i)->m_volume != nullptr;
    return started && volume;
}

using T_SuInt = void(TC *)(void *, int);
using T_SuChunk = bool(TC *)(void *, void *);
using T_SuVoid = void(TC *)(void *, void *);
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}
void PushStateBits(const S_SuOp &op)
{
    static const uint32_t flags[] = {1, 0, 4, 9, 2, 0x11};
    g_bits[g_side].push_back(flags[op.a % 6]);
    g_bits[g_side].push_back(uint32_t(op.b % 25));
    g_bits[g_side].push_back(uint32_t(op.c % 3 ? 6000 : 0));
    g_bits[g_side].push_back(uint32_t(op.d & 1));
    g_bits[g_side].push_back(uint32_t(op.a % 400));
    g_bits[g_side].push_back(uint32_t(op.b % 3) - 1u);
    g_bits[g_side].push_back(uint32_t(op.c % 6));
    g_bits[g_side].push_back(uint32_t(op.d % 8));
    g_bits[g_side].push_back(uint32_t(op.a % 5 ? 1000 : -1));
    g_quantized[g_side].push_back({1.0f + float(op.b % 5), 2.0f, -3.0f, 1.0f});
    g_quantized[g_side].push_back({0.5f, float(op.c % 7), 0.25f, 0.0f});
    g_bits[g_side].push_back(uint32_t(op.d % 900));
}
std::string RunSuOp(const S_SuOp &op, int index)
{
    const bool o = g_side != 0;
    const int li = op.box < g_boxCount ? op.box : 0;
    uint8_t *b = g_box[g_side][li];
    C_SupplyBox *p = PortBox(li);
    switch (op.kind) {
    case OP_SET_FRAME: {
        // (Not again once started: the original forgets the volume it made, the port's too.)
        if (Started(li))
            return "SetFrame (started)";
        uint8_t *frame = g_boxFrame[g_side][li];
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *)>(O_SU_SET_FRAME)(b, frame) : p->SetFrame(AsFrame(frame));
        return F("SetFrame %d -> %d", li, int(r));
    }
    case OP_READ: {
        // The version, the fields, ten items of each list (-1 ends nothing: skipped), the text; now and then cut short.
        S_Writer w;
        w.Begin(0xAE50);
        const uint8_t version = 1;
        w.Raw(&version, 1);
        const int fields[5] = {op.a % 3 ? 1 : 0, int(op.b % 4 == 0 ? 0 : op.b % 4 == 1 ? 1 : 3), 2 + op.c % 6, op.d % 5 == 0 ? 0 : 1 + op.d % 30,
                               op.a % 4 == 0 ? -1 : op.a % 4 == 1 ? 0 : 1 + op.a % 20};
        w.Raw(fields, 20);
        for (int k = 0; k < 20; ++k) {
            const int item = (op.b + k * 3) % 4 == 0 ? -1 : 0x10 + (op.c + k) % 40;
            w.Raw(&item, 4);
        }
        const int text = 0x1C00 + op.d % 5;
        w.Raw(&text, 4);
        w.End();
        std::string bytes = w.out;
        // (Cut short: not before the lists end: the original reads the missing items as the last one read again,
        // the port's -1... checked: the same; not the text's place.)
        const std::string name = F("su_props%d.bin", index);
        g_overrides[Lower(name)] = bytes;
        C_chunk chunk;
        chunk.Open(name.c_str(), false);
        uint16_t id;
        chunk.Descend(id);
        const bool r = o ? OriginalAt<T_SuChunk>(O_SU_READ)(b, &chunk) : p->ReadProperties(chunk);
        chunk.Close();
        return F("ReadProperties %d -> %d ", li, int(r)) + Hex(bytes);
    }
    case OP_WRITE: {
        const std::string file = F("su_write%d.bin", index);
        C_chunk chunk;
        chunk.Open(file.c_str(), true);
        chunk.Begin(0xAE50);
        const bool r = o ? OriginalAt<T_SuChunk>(O_SU_WRITE)(b, &chunk) : p->WriteProperties(chunk);
        chunk.Ascend();
        chunk.Close();
        std::string bytes;
        FileText(file, bytes);
        return F("WriteProperties %d -> %d ", li, int(r)) + Hex(bytes);
    }
    case OP_COPY: {
        const int from = (li + (op.a & 1)) % g_boxCount;
        uint8_t *src = g_box[g_side][from];
        if (o)
            OriginalAt<T_SuVoid>(O_SU_COPY)(b, src);
        else
            p->CopyFrom(reinterpret_cast<C_Actor *>(src));
        return F("CopyFrom %d %d", li, from);
    }
    case OP_START: {
        // (Not without the walls: the box is made only with them, SetFrame fails.)
        const bool walls = o ? At<uint32_t>(b, 0xC0) != 0 : p->m_walls != nullptr;
        if (!walls)
            return "StartMission (no walls)";
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_SuInt>(O_SU_START)(b, server);
        else
            p->StartMission(server);
        return F("StartMission %d %d", li, int(server));
    }
    case OP_STOP: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_SuInt>(O_SU_STOP)(b, server);
        else
            p->StopMission(server);
        return F("StopMission %d %d", li, int(server));
    }
    case OP_RESTART:
        if (!Ready(li))
            return "RestartMission (not started)";
        if (o)
            OriginalAt<T_SuInt>(O_SU_RESTART)(b, op.c & 1);
        else
            p->RestartMission(op.c & 1);
        return F("RestartMission %d", li);
    case OP_ENABLE: {
        if (!Ready(li))
            return "SetEnabled (not started)";
        const bool on = op.a % 3 != 0;
        if (o)
            OriginalAt<T_SuInt>(O_SU_ENABLE)(b, on);
        else
            p->SetEnabled(on);
        return F("SetEnabled %d %d", li, int(on));
    }
    case OP_CHARGE: {
        if (!Ready(li))
            return "UseCharge (not started)";
        // (The original inlines it in its takers: the same steps on its fields, its SetState and UpdateAmmoParts.)
        const int times = op.a % 4 == 0 ? 25 : 1;
        for (int k = 0; k < times; ++k) {
            if (o) {
                if (At<int>(b, 0x250) != C_SupplyBox::SUPPLY_READY || !(At<uint32_t>(b, 0x124) & C_SupplyBox::SUPPLY_SHOWN))
                    continue;
                if (--Mut<int>(b, 0x130) <= 0)
                    OriginalAt<T_SuInt>(0x005fcd80)(b, C_SupplyBox::SUPPLY_CLOSING);
                OriginalAt<T_Void>(0x005fd760)(b);
            } else {
                p->UseCharge();
            }
        }
        return F("UseCharge %d x%d", li, times);
    }
    case OP_UPDATE:
    case OP_TICK: {
        if (!Ready(li))
            return "Update (not started)";
        const int t = FRAME_TIMES[op.a % 12];
        const bool tick = op.kind == OP_TICK;
        if (o)
            OriginalAt<T_SuInt>(tick ? O_SU_TICK : O_SU_UPDATE)(b, t);
        else if (tick)
            p->Tick(t);
        else
            p->Update(t);
        return F("%s %d %d", tick ? "Tick" : "Update", li, t);
    }
    case OP_MESSAGE: {
        if (!Ready(li))
            return "OnMessage (not started)";
        g_bits[g_side].clear();
        g_quantized[g_side].clear();
        const uint32_t kind = uint32_t(op.a % 4 == 0);
        g_bits[g_side].push_back(kind);
        if (kind == 0)
            PushStateBits(op);
        if (o)
            OriginalAt<T_SuVoid>(O_SU_MESSAGE)(b, W().bitstream);
        else
            p->OnMessage(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("OnMessage %d %u", li, kind);
    }
    case OP_NET_STATE: {
        if (!Ready(li))
            return "NetState (not started)";
        if (op.a & 1) {
            if (o)
                OriginalAt<T_SuVoid>(O_SU_WRITE_STATE)(b, W().bitstream);
            else
                p->WriteState(reinterpret_cast<C_BitStream *>(W().bitstream));
            return F("WriteState %d", li);
        }
        g_bits[g_side].clear();
        g_quantized[g_side].clear();
        PushStateBits(op);
        if (o)
            OriginalAt<T_SuVoid>(O_SU_READ_STATE)(b, W().bitstream);
        else
            p->ReadState(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("ReadState %d", li);
    }
    case OP_DAMAGE: {
        // (Only the destroyed message calls it, with -1: the original's function itself with other amounts too.)
        if (!Ready(li))
            return "Damage (not started)";
        static const int amounts[] = {-1, 100, 5999, 6000, 7000, 0};
        const int amount = amounts[op.a % 6];
        if (o)
            OriginalAt<T_SuInt>(0x005fbd40)(b, amount);
        else
            p->Damage(amount);
        return F("Damage %d %d", li, amount);
    }
    default: {
        if (!Ready(li))
            return "SaveLoad (not started)";
        const std::string file = F("su_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_SuChunk>(O_SU_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        // (The state's stack leftovers in the original: +0x0D..0x0F, +0x24..0x2F, the crate's w +0x4C, +0x54..0x5F.)
        const size_t state = bytes.find(std::string("\x02\x02", 2));
        if (state != std::string::npos && bytes.size() >= state + 6 + 0x60) {
            const size_t at = state + 6;
            for (size_t k : {0x0Du, 0x0Eu, 0x0Fu})
                bytes[at + k] = '\0';
            for (size_t k = 0x24; k < 0x30; ++k)
                bytes[at + k] = '\0';
            for (size_t k = 0x4C; k < 0x50; ++k)
                bytes[at + k] = '\0';
            for (size_t k = 0x54; k < 0x60; ++k)
                bytes[at + k] = '\0';
        }
        // (Loaded back now and then, into this one or the other started one.)
        bool rl = false;
        const int load = op.c % 3;
        const int ii = (li + (op.d & 1)) % g_boxCount;
        uint8_t *into = g_box[g_side][ii];
        int depth = 0;
        if (load && Ready(ii)) {
            C_chunk chunk;
            chunk.Open(file.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_SuChunk>(O_SU_LOAD)(into, &chunk) : reinterpret_cast<C_SupplyBox *>(into)->LoadGame(chunk);
            depth = chunk.m_depth;
            chunk.Close();
        }
        return F("SaveGame %d -> %d LoadGame %d into %d -> %d depth %d bytes ", li, int(rs), load, ii, int(rl), depth) + Hex(bytes);
    }
    }
}

void ClearModelCache()
{
    if (g_side)
        OriginalAt<T_Void>(0x006d77e0)(OriginalAt<void *>(O_MODEL_CACHE));
    else
        g_modelCache.Clear();
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("su_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_suShow;
void RunSuCase(const S_SuCase &c)
{
    g_caseStart = GetTickCount();
    ++g_suCases;
    g_holdFrees = true;
    g_current = F("case %d", g_suCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_boxCount = int(c.boxes.size());
    g_sweepEvery = c.sweepEvery;
    g_hasAnimation = c.animation;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        g_bits[side].clear();
        g_quantized[side].clear();
        BuildWorld(side, worldSeed);
        if (side)
            *OriginalAt<float *>(0x0097ca44) = c.scanMax;
        else
            g_scanMax[1] = c.scanMax;
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        // The volumes the fall meets: static ground and a dynamic one (no actor).
        g_staticVolume[side] = Block(0x60);
        Name(g_staticVolume[side], "ground");
        g_dynamicVolume[side] = Block(0x60);
        Name(g_dynamicVolume[side], "dynamic");
        Mut<uint32_t>(g_dynamicVolume[side], offsetof(S_CollisionVolume, flags)) = COLLISION_FLAG_DYNAMIC;
        Mut<uint32_t>(g_dynamicVolume[side], offsetof(S_CollisionVolume, type)) = 0x100;
        uint8_t *crushed = Block(0x400);
        Name(crushed, "crushed actor");
        *reinterpret_cast<void **>(crushed) = g_crushedVtable;
        Mut<uint8_t *>(g_dynamicVolume[side], offsetof(S_CollisionVolume, actor)) = (worldSeed & 1) ? crushed : nullptr;
        g_animation[side] = Block(0x40);
        Name(g_animation[side], "ammobox");
        Mut<int>(g_animation[side], 0x18) = 3;
        for (int i = 0; i < g_boxCount; ++i) {
            const S_SuBox &lc = c.boxes[size_t(i)];
            uint8_t *frame = Child(sector, FRAME_MODEL, F("crate%d", i), nullptr);
            S_matrix &m = AsFrame(frame)->m_localMatrix;
            m.Identity();
            const float ch = std::cos(lc.heading), sh = std::sin(lc.heading);
            m.m[0][0] = ch;
            m.m[0][2] = -sh;
            m.m[2][0] = sh;
            m.m[2][2] = ch;
            if (lc.odd) {
                static const float rows[3][3] = {{1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 1e-5f}, {0.0f, 0.0f, 0.0f}};
                for (int k = 0; k < 3; ++k)
                    m.m[2][k] = rows[lc.odd - 1][k];
            }
            m.m[3][0] = lc.x;
            m.m[3][1] = lc.y;
            m.m[3][2] = lc.z;
            // (Not a z scale of 1.5: the box's sphere radius then differs by one ulp from the original's, not explained
            // yet; see COVERAGE.md.)
            static const float scales[4][3] = {{1.0f, 1.0f, 1.0f}, {1.25f, 1.0f, 1.0f}, {1.0f, 0.8f, 1.0f}, {2.0f, 2.0f, 2.0f}};
            const float *sc = scales[lc.scale];
            for (int k = 0; k < 3; ++k) {
                m.m[0][k] *= sc[0];
                m.m[1][k] *= sc[1];
                m.m[2][k] *= sc[2];
            }
            AsFrame(frame)->m_scale = S_vector(sc[0], sc[1], sc[2]);
            if (lc.walls < 2) {
                uint8_t *walls = Child(frame, FRAME_VISUAL, "Steny02", frame);
                W().frames.back().visualType = lc.walls == 0 ? I3DVISUAL_OBJECT : 0x4d474e53;
                // The mesh: a level of eight vertices (0x20 bytes each), the box's corners in an order of the case's.
                uint8_t *mesh = Block(0x200);
                *reinterpret_cast<void ***>(mesh) = EngineVtable("I3D_mesh");
                uint8_t *level = Block(0x200);
                *reinterpret_cast<void ***>(level) = EngineVtable("I3D_mesh_level");
                float *vertices = reinterpret_cast<float *>(Block(8 * 0x20));
                static const float corners[8][3] = {{-0.4f, 0.0f, -0.3f}, {0.4f, 0.0f, -0.3f}, {0.4f, 0.0f, 0.3f}, {-0.4f, 0.0f, 0.3f},
                                                    {-0.4f, 0.8f, -0.3f}, {0.4f, 0.8f, -0.3f}, {0.4f, 0.8f, 0.3f}, {-0.4f, 0.8f, 0.3f}};
                static const int orders[6][8] = {{0, 1, 2, 3, 4, 5, 6, 7}, {0, 2, 1, 3, 4, 5, 6, 7}, {0, 3, 2, 1, 7, 6, 5, 4},
                                                 {4, 0, 5, 1, 6, 2, 7, 3}, {1, 3, 0, 2, 4, 5, 6, 7}, {2, 0, 3, 1, 4, 6, 5, 7}};
                for (int k = 0; k < 8; ++k) {
                    std::memcpy(vertices + 8 * k, corners[orders[lc.vertexOrder][k]], 12);
                    if (vertices[8 * k + 1] > 0.0f)
                        vertices[8 * k + 1] = lc.upperY;
                }
                Mut<uint8_t *>(mesh, 0x100) = level;
                Mut<float *>(level, 0x100) = vertices;
                Mut<int>(level, 0x54) = 8;
                Mut<uint8_t *>(walls, 0x1E0) = mesh;
            }
            for (int k = 0; k < 14; ++k)
                if (!(lc.ammoMissing >> k & 1))
                    Child(frame, FRAME_VISUAL, F(k < 10 ? "Ammo0%d" : "Ammo%d", k), frame);
            g_boxFrame[side][i] = frame;
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        for (int i = 0; i < g_boxCount; ++i) {
            uint8_t *b;
            if (side) {
                b = Block(0x260 + 16);
                OriginalAt<T_Void>(O_SU_CTOR)(b);
                // (The original's constructor leaves these unset, the port's its defaults: SetFrame and Reset set them
                // before they are read.)
                Mut<int>(b, 0x128) = 5;
                Mut<int>(b, 0x12C) = 20;
                Mut<int>(b, 0x134) = -1;
                Mut<int>(b, 0x138) = 6000;
                Mut<int>(b, 0x210) = 0x1BFA;
                Mut<int>(b, 0x248) = -1;
                Mut<int>(b, 0x24C) = 5;
                Mut<uint8_t *>(b, 0x20) = g_boxFrame[1][i];
            } else {
                b = Block(sizeof(C_SupplyBox) + 16);
                new (b) C_SupplyBox();
                reinterpret_cast<C_SupplyBox *>(b)->m_frame = AsFrame(g_boxFrame[0][i]);
            }
            g_box[side][i] = b;
            Name(b, F("box %d", i));
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : BoxesState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_SU_TRACE") ? std::atoi(std::getenv("HD2_SU_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_suCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunSuOp(c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : BoxesState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_boxCount; ++i) {
            uint8_t *b = g_box[side][i];
            if (side) {
                OriginalAt<T_SuInt>(O_SU_STOP)(b, 1);
                OriginalAt<T_Void>(O_SU_DTOR)(b);
            } else {
                PortBox(i)->StopMission(true);
                PortBox(i)->~C_SupplyBox();
            }
        }
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(O_SU_CREATE)(OriginalAt<void *>(O_SU_CREATOR));
            Name(made, "created box");
            out.push_back(F("created type %d", At<int>(made, 0x18)));
            OriginalAt<void(TC *)(void *, void *)>(O_SU_DESTROY)(OriginalAt<void *>(O_SU_CREATOR), made);
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_SUPPLY_BOX];
            C_Actor *made = creator->Create();
            Name(made, "created box");
            out.push_back(F("created type %d", int(made->GetType())));
            creator->Destroy(made);
        }
        ClearModelCache();
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        for (int i = 0; i < MAX_BOXES; ++i)
            g_box[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_SU_ONLY") ? std::atoi(std::getenv("HD2_SU_ONLY")) : 0;
    if (g_suShow && (!only || only == g_suCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_suShow == 2)
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

// The animation cache's Get on both sides (the original's out of line on its cache, never built here; the port's
// patched to this): logged; an animation set for most names, not found now and then (its pointer left alone).
HRESULT OT B_AnimGet(void *, const char *name, uint8_t **out, void *, void *)
{
    g_log[g_side].push_back(F("C_ResourceCache<I3D_animation_set>::Get(%s)", name ? name : "(null)"));
    if (NextFake() % 4 == 0)
        return I3DERR_NOFILE;
    uint8_t *anim = Block(0x100);
    *reinterpret_cast<void ***>(anim) = EngineVtable("I3D_animation_set");
    Name(anim, F("anim %s", name ? name : ""));
    *out = anim;
    return I3D_OK;
}
void PatchPort(void *at, const void *to)
{
    DWORD old;
    VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
    uint8_t *p = static_cast<uint8_t *>(at);
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(p) + 5));
    p[0] = 0xE9;
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
}

using T_AddDir = void(TC *)(void *, const char *);
void SetUpBox()
{
    SetUpCmd();
    SetUpBoxAnswers();
    SetUpCrushedVtable();
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
        {0x006e5380, reinterpret_cast<const void *>(&O_TextsGet)},
        {0x0041fc10, reinterpret_cast<const void *>(&B_AnimGet)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    {
        auto get = &C_ResourceCache<I3D_animation_set>::Get;
        void *at;
        std::memcpy(&at, &get, sizeof at);
        PatchPort(at, reinterpret_cast<const void *>(&B_AnimGet));
    }
    // The model cache (both sides' real; the original's built here).
    if (*OriginalAt<uint32_t *>(0x0098bcc0) != 2) {
        InitializeCriticalSection(OriginalAt<CRITICAL_SECTION *>(0x0098bca8));
        *OriginalAt<uint32_t *>(0x0098bcc0) = 2;
    }
    OriginalAt<void (*)()>(0x006d5030)();
    *OriginalAt<uint32_t *>(O_MODEL_CACHE + 0x34) = 256;
    uint8_t *buf = static_cast<uint8_t *>(O_New(8 * 256));
    *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x18) = buf;
    *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x1C) = buf;
    *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x20) = buf + 8 * 256;
    reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(O_MODEL_CACHE))[0x18 / 4])(OriginalAt<void *>(O_MODEL_CACHE), "models\\");
    g_modelCache.AddDir("models\\");
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("su_diff: main");
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
        std::printf("su_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_SU_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_SU_SHOW"))
        g_suShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_SU_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_SU_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpBox();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunSuCase(RandomSuCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("su_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_suCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
