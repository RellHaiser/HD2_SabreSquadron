// Mine part 1: the mines (game/mine.cpp, the original's 0x005fde00..0x005ff150) against the original executable's.
// cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the mines' calls go through mn_calls.s /
// mn_calls.inc (gen_cmd_calls.py mn). The game's texts are logged on both sides; the item definitions are the test's
// (the original's table at 0x0098b030, read inline; the port's GetItemDef answers from the test's own).
//
// Each case builds the same world on each side (cmd_diff's: humans, cars with drivers or none, other actors), one or two
// mine frames under the primary sector (placed, turned, now and then with a tiny, zero or vertical z axis), makes the
// mines on each side (the original's by its constructor 0x005fdec0 in a block of 0x120, the port's by its own), sets
// their frames and runs the same random sequence of operations in the case's role: Setup (an owner or none; the mines'
// two items, another mine item, an item without a component), ticks and updates (the arming countdown on its role's
// side), Defuse, OnSteppedOn (humans, the owner, cars with or without drivers, other actors), the clients' messages,
// saves and loads (the file just written, made ones), stops and starts, restarts, GetMaterialId. After each operation
// the answer, every mine's state (every field, its sphere in each side's layout), the frames and every call made into
// the game and the engine (the volumes, the explosions, the messages) are compared; a save's bytes too. The creator
// makes and destroys a mine at the end.
// MinGW; build_mn.bat; runs mn_diff.exe ("mn_diff: N checks, 0 failures"). Env: HD2_MN_CASES (default 1,500;
// HD2_MN_QUICK: 300), HD2_MN_SEED, HD2_MN_SHOW (1: the port's lines; 2: both), HD2_MN_ONLY (the case shown),
// HD2_MN_TRACE (a case's operations as they start).
#define HARNESS_NAME "mn_diff"
#define CMD_CALLS_INC "mn_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/car.h"
#include "game/collision.h"
#include "game/game_texts.h"
#include "game/item_defs.h"
#include "game/mine.h"
#undef protected
#undef private

// ---- the port's side of what the mines call and the test provides ----
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
// The actors' creators (the mines' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the mine's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
void C_Actor::CopyFrom(const C_Actor *) {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}
// The item components' streams (their vtables here: the mines' components are the test's).
void C_MineComp::Save(int *, void *) {}
void C_MineComp::Load(int, const void *) {}

// The item definitions: the test's (each side's own table).
namespace H {
constexpr int ITEM_TABLE = 500;
uint8_t *g_portDefs[ITEM_TABLE];
}  // namespace H
C_ItemDef *GetItemDef(int typeId)
{
    return typeId >= 0 && typeId < H::ITEM_TABLE ? reinterpret_cast<C_ItemDef *>(H::g_portDefs[typeId]) : nullptr;
}

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
// The texts (the original's C_GameTexts::Get: its C_str by a hidden pointer).
void *OT O_TextsGet(void *, void *ret, int id)
{
    g_log[g_side].push_back(F("texts %d", id));
    *reinterpret_cast<uint32_t *>(ret) = 0;
    OriginalAt<T_Fstr>(0x00405100)(ret, "%s", TextOf(id).c_str());
    return ret;
}
// A car's driver (the original's getter is out of line; the port reads the field: not logged).
uint32_t OT O_GetDriver(uint8_t *car) { return *reinterpret_cast<uint32_t *>(car + 0x24AC); }
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

// ---- the item definitions: per case, each side's layout ----
// (0x46 and 0x47 the mines' own; 0x30 another mine item; 0x31 an item without a component. Not one without a
// definition: the original reads its text and its component through null.)
const int ITEM_IDS[] = {0x46, 0x47, 0x46, 0x47, 0x30, 0x31};
constexpr int ITEM_NO_COMPONENT = 0x31;
std::vector<uint8_t *> g_defBlocks[2];
void MakeItemDefs(uint32_t seed)
{
    uint32_t s = seed;
    auto next = [&](int n) {
        s = s * 1664525u + 1013904223u;
        return int((s >> 8) % uint32_t(n));
    };
    for (int id : {0x46, 0x47, 0x30, 0x31}) {
        uint8_t *def = Block(std::max(sizeof(C_ItemDef), size_t(0x100)) + 0x40);
        Name(def, F("item def %x", id));
        uint8_t *comp = nullptr;
        if (id != ITEM_NO_COMPONENT) {
            comp = Block(std::max(sizeof(C_MineComp), size_t(0x40)) + 0x40);
            Name(comp, F("mine comp %x", id));
        }
        static const float damages[] = {100.0f, 250.5f, 0.0f, 1000.0f};
        static const float radii[] = {5.0f, 2.5f, 0.0f, 12.25f};
        const float damage = damages[next(4)], radius = radii[next(4)];
        const int sound = next(5) == 0 ? -1 : 20 + next(30), effect = next(6) == 0 ? -1 : 3 + next(20);
        const int text = 0x200 + id;
        if (g_side) {
            Mut<int>(def, 0x14) = text;
            Mut<uint8_t *>(def, 0x48) = comp;
            if (comp) {
                Mut<float>(comp, 0x10) = damage;
                Mut<int>(comp, 0x14) = sound;
                Mut<int>(comp, 0x1C) = effect;
                Mut<float>(comp, 0x28) = radius;
            }
            OriginalAt<uint8_t **>(0x0098b030)[id] = def;
        } else {
            C_ItemDef *d = reinterpret_cast<C_ItemDef *>(def);
            d->m_textId = text;
            if (comp) {
                C_MineComp *c = new (comp) C_MineComp();
                c->m_damage = damage;
                c->m_shotSound = sound;
                c->m_effect = effect;
                c->m_explosionRadius = radius;
                d->m_component = c;
            }
            g_portDefs[id] = def;
        }
        g_defBlocks[g_side].push_back(def);
    }
}
void ClearItemDefs()
{
    for (int id = 0; id < ITEM_TABLE; ++id) {
        OriginalAt<uint8_t **>(0x0098b030)[id] = nullptr;
        g_portDefs[id] = nullptr;
    }
    g_defBlocks[0].clear();
    g_defBlocks[1].clear();
}

// ---- the mines ----
constexpr int MAX_MINES = 2;
uint8_t *g_mine[2][MAX_MINES];
uint8_t *g_mineFrame[2][MAX_MINES];
int g_mineCount;
C_Mine *PortMine(int i) { return reinterpret_cast<C_Mine *>(g_mine[0][i]); }
constexpr uint32_t O_MN_CTOR = 0x005fdec0, O_MN_DTOR = 0x005fdf60, O_MN_SET_FRAME = 0x005fdf90, O_MN_START = 0x005fdfb0,
                   O_MN_STOP = 0x005fdfd0, O_MN_SETUP = 0x005fe010, O_MN_RESTART = 0x005fe430, O_MN_SAVE = 0x005fe440,
                   O_MN_LOAD = 0x005fe800, O_MN_MESSAGE = 0x005feb50, O_MN_TICK = 0x005fee70, O_MN_UPDATE = 0x005fee90,
                   O_MN_MATERIAL = 0x005feeb0, O_MN_DEFUSE = 0x005feee0, O_MN_STEPPED = 0x005fefb0;
constexpr uint32_t O_MN_CREATOR = 0x008ac9f4, O_MN_CREATE = 0x005fde30, O_MN_DESTROY = 0x005fdea0;

// The messages' words (ReadBits answers them in turn on each side).
std::deque<uint32_t> g_bits[2];

void SetUpMineAnswers()
{
    g_answers["C_Actor::SetFrame"] = [](const std::vector<uint32_t> &) { return 1u; };
    g_answers["I3D_frame::SetName"] = [](const std::vector<uint32_t> &a) {
        reinterpret_cast<I3D_frame *>(a[0])->m_name = Text(a[1] ? reinterpret_cast<const char *>(a[1]) : "");
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
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    // The temporary actors' messages: the world's bit stream; their words in turn.
    g_answers["CreateTempActorMessage"] = [](const std::vector<uint32_t> &) { return Ptr(W().bitstream); };
    g_answers["C_BitStream::ReadBits"] = [](const std::vector<uint32_t> &) {
        if (g_bits[g_side].empty())
            return 0u;
        const uint32_t w = g_bits[g_side].front();
        g_bits[g_side].pop_front();
        return w;
    };
    // The actors module's humans by id: the world's list (none past it).
    g_answers["C_ActorsModule::GetHuman"] = [](const std::vector<uint32_t> &a) {
        const int id = int(a[1]);
        return id >= 0 && size_t(id) < W().list.size() ? Ptr(W().list[size_t(id)]) : 0u;
    };
}

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string Fl(float f) { return FloatText(f); }
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string(); }
// The sphere (S_CollisionVolume: the same layout on both sides; its centre's w the position's, a stale word in the
// original's Arm: left out).
std::string VolumeText(const S_CollisionVolume &v)
{
    return F("volume flags %x type %x frame %s actor %s radius ", v.flags, v.type, NameOf(Ptr(v.frame)).c_str(),
             NameOf(Ptr(v.actor)).c_str()) +
           Fl(v.radius) + " centre " + V3(&v.center.x);
}
std::vector<std::string> MineState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_mine[g_side][i];
    if (g_side) {
        out.push_back(F("mine%d item %x arm %d armed %d added %d owner %s material %d name %s pos ", i, At<int>(b, 0x90),
                        At<int>(b, 0x94), int(b[0x98]), int(b[0x99]), NameOf(At<uint32_t>(b, 0x9C)).c_str(), At<int>(b, 0x100),
                        RepText(At<uint32_t>(b, 0x80)).c_str()) +
                      V3(FP(b, 0x30)) + " dir " + V4(FP(b, 0x40)));
        out.push_back("  " + VolumeText(*reinterpret_cast<const S_CollisionVolume *>(b + 0xA0)));
        return out;
    }
    const C_Mine *p = PortMine(i);
    out.push_back(F("mine%d item %x arm %d armed %d added %d owner %s material %d name %s pos ", i, p->m_itemId, p->m_armTime,
                    int(p->m_armed), int(p->m_volumeAdded), NameOf(Ptr(p->m_owner)).c_str(), p->m_materialId,
                    p->m_name.c_str() ? p->m_name.c_str() : "") +
                  V3(&p->m_position.x) + " dir " + V4(&p->m_direction.x));
    out.push_back("  " + VolumeText(p->m_volume));
    return out;
}
std::vector<std::string> MinesState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_mineCount; ++i)
        for (const std::string &s : MineState(i))
            out.push_back(s);
    for (int i = 0; i < g_mineCount; ++i) {
        const I3D_frame *f = AsFrame(g_mineFrame[g_side][i]);
        out.push_back(F("frame%d flags %08x at ", i, f->m_frameFlags) + VectorText(Ptr(&f->m_localMatrix.m[3][0]), 3) + " rot " +
                      VectorText(Ptr(&f->m_rot), 4) + " name " + (f->m_name ? f->m_name : ""));
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
    template <class T> void EntryOf(uint16_t id, const T &v)
    {
        Begin(id);
        Raw(&v, sizeof v);
        End();
    }
};
enum { OP_SETUP, OP_TICK, OP_UPDATE, OP_DEFUSE, OP_STEPPED, OP_MESSAGE, OP_SAVE_LOAD, OP_STOP_START, OP_RESTART, OP_MATERIAL,
       OP_KINDS };
const char *const OP_KIND_NAMES[] = {"Setup", "Tick", "Update", "Defuse", "SteppedOn", "Message", "SaveLoad", "StopStart",
                                     "Restart", "Material"};
struct S_MnOp {
    int kind = 0;
    int mine = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_MnMine {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f, tilt = 0.0f;
    int odd = 0;                                        // (1..4: its z axis tiny, zero or vertical)
    bool started = true;
};
struct S_MnCase {
    std::vector<S_MnMine> mines;
    std::vector<S_MnOp> ops;
    uint32_t role = GAME_ROLE_LOCAL;
    uint32_t defSeed = 0;
};
int g_mnCases, g_opCounts[OP_KINDS];
// (The countdown's edges: 2,000 ms in one step, one under, one over.)
const int FRAME_TIMES[] = {0, 16, 100, 500, 1000, 1999, 2000, 2001, 3000, 1};
S_MnCase RandomMnCase()
{
    S_MnCase c;
    const int mines = P(0.75) ? 1 : 2;
    for (int i = 0; i < mines; ++i) {
        S_MnMine m;
        m.x = float(I(-50, 50)) * 0.5f;
        m.y = float(I(-4, 10)) * 0.25f;
        m.z = float(I(-50, 50)) * 0.5f;
        m.heading = float(I(0, 628)) * 0.01f;
        m.tilt = P(0.3) ? float(I(-30, 30)) * 0.01f : 0.0f;
        m.odd = P(0.1) ? I(1, 4) : 0;
        m.started = P(0.95);
        c.mines.push_back(m);
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    c.defSeed = U32();
    const int count = P(0.1) ? I(40, 100) : I(4, 30);
    for (int i = 0; i < count; ++i) {
        S_MnOp op;
        const int r = I(0, 99);
        op.kind = r < 15 ? OP_SETUP : r < 35 ? OP_TICK : r < 55 ? OP_UPDATE : r < 60 ? OP_DEFUSE : r < 75 ? OP_STEPPED
                : r < 82 ? OP_MESSAGE : r < 90 ? OP_SAVE_LOAD : r < 94 ? OP_STOP_START : r < 97 ? OP_RESTART : OP_MATERIAL;
        op.mine = I(0, mines - 1);
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // Set up first, mostly.
    if (P(0.9))
        for (int i = mines; i-- > 0;) {
            S_MnOp op;
            op.kind = OP_SETUP;
            op.mine = i;
            op.a = I(0, 1000);
            op.b = I(0, 1000);
            op.c = I(0, 1000);
            c.ops.insert(c.ops.begin(), op);
        }
    return c;
}

uint8_t *ActorOf(int type, int n)
{
    std::vector<uint8_t *> found;
    for (const auto &a : W().actors)
        if (a.type == type)
            found.push_back(a.bytes);
    return found.empty() ? nullptr : found[size_t(n) % found.size()];
}
// Whether the mine's item has a component (without one the original reads the effect and the damage through null).
bool HasComponent(int i)
{
    const int item = g_side ? At<int>(g_mine[1][i], 0x90) : PortMine(i)->m_itemId;
    return item != ITEM_NO_COMPONENT && (item == 0x46 || item == 0x47 || item == 0x30);
}

using T_MnInt = void(TC *)(void *, int);
using T_MnChunk = bool(TC *)(void *, void *);
std::string RunMnOp(const S_MnOp &op, int index)
{
    const bool o = g_side != 0;
    const int mi = op.mine < g_mineCount ? op.mine : 0;
    uint8_t *b = g_mine[g_side][mi];
    C_Mine *p = PortMine(mi);
    const int t = FRAME_TIMES[op.a % 10];
    switch (op.kind) {
    case OP_SETUP: {
        uint8_t *owner = op.a % 4 ? ActorOf(ACTOR_HUMAN, op.b) : nullptr;
        const int item = ITEM_IDS[op.c % 6];
        const int material = op.c % 7 == 0 ? 3 : -1;
        if (o)
            OriginalAt<void(TC *)(void *, void *, int, int)>(O_MN_SETUP)(b, owner, item, material);
        else
            p->Setup(reinterpret_cast<C_Human *>(owner), item, material);
        return F("Setup %d %s %x %d", mi, NameOf(Ptr(owner)).c_str(), item, material);
    }
    case OP_TICK:
        if (o)
            OriginalAt<T_MnInt>(O_MN_TICK)(b, t);
        else
            p->Tick(t);
        return F("Tick %d %d", mi, t);
    case OP_UPDATE:
        if (o)
            OriginalAt<T_MnInt>(O_MN_UPDATE)(b, t);
        else
            p->Update(t);
        return F("Update %d %d", mi, t);
    case OP_DEFUSE:
        if (o)
            OriginalAt<void(__attribute__((fastcall)) *)(void *)>(O_MN_DEFUSE)(b);
        else
            p->Defuse();
        return F("Defuse %d", mi);
    case OP_STEPPED: {
        // A human (now and then the owner), a car (its driver or none). (Not another actor: only humans' feet and cars'
        // wheels reach it, and an ownerless mine takes any other for a car, its driver read past its block.)
        const int which = op.a % 6;
        uint8_t *owner = reinterpret_cast<uint8_t *>(o ? At<uint32_t>(b, 0x9C) : Ptr(p->m_owner));
        uint8_t *actor = which < 2 ? ActorOf(ACTOR_HUMAN, op.b) : which == 2 && owner ? owner : ActorOf(ACTOR_CAR, op.b);
        if (!actor)
            return "OnSteppedOn (no actor)";
        // (Not without the item's component, armed: the original reads its range through null.)
        const bool armed = o ? b[0x98] != 0 : p->m_armed;
        if (armed && !HasComponent(mi))
            return "OnSteppedOn (no component)";
        const int wheel = which < 2 ? -1 : op.c % 4;
        if (o)
            OriginalAt<void(TC *)(void *, void *, int)>(O_MN_STEPPED)(b, actor, wheel);
        else
            p->OnSteppedOn(reinterpret_cast<C_Actor *>(actor), wheel);
        return F("OnSteppedOn %d %s %d", mi, NameOf(Ptr(actor)).c_str(), wheel);
    }
    case OP_MESSAGE: {
        const uint32_t kind = uint32_t(op.a % 4);
        // (Not an explosion without the item's component: the original reads its effect through null.)
        if (kind == 0 && !HasComponent(mi))
            return "OnMessage (no component)";
        g_bits[g_side].clear();
        g_bits[g_side].push_back(kind);
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_MN_MESSAGE)(b, W().bitstream);
        else
            p->OnMessage(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("OnMessage %d %u", mi, kind);
    }
    case OP_SAVE_LOAD: {
        const std::string file = F("mn_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_MnChunk>(O_MN_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        std::string hex;
        for (unsigned char ch : bytes)
            hex += F("%02x", ch);
        // (Loaded back now and then (into this one or the other): its file, or a made one: the state with other flags,
        // an owner's id (a human's, another actor's, none, past the list), an entry of another id (entered, not left:
        // what is inside it is read). Not a short state: the original goes on with stack words for what is missing, the
        // port's fix with 0.)
        bool rl = false;
        const int load = op.c % 4;
        uint8_t *into = g_mine[g_side][(mi + (op.d & 1)) % g_mineCount];
        unsigned next = 0;
        if (load) {
            std::string name = file;
            if (load >= 2) {
                S_Writer w;
                w.Begin(0x2000);
                if (load == 3 && op.b % 3 == 0) {
                    w.Begin(0x0009);
                    w.EntryOf(2, int(op.d % 12));
                    w.End();
                }
                // (The owner's chunk before the state now and then.)
                const bool ownerFirst = op.a % 4 == 3;
                if (ownerFirst)
                    w.EntryOf(2, int(op.c % 12));
                w.Begin(1);
                const float pos[3] = {float(op.a % 20) - 10.0f, float(op.b % 5) * 0.5f, float(op.d % 20) - 10.0f};
                static const float dirs[][3] = {{0.0f, 0.0f, 1.0f}, {0.6f, 0.0f, 0.8f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
                w.Raw(pos, 12);
                w.Raw(dirs[op.b % 4], 12);
                static const int times[] = {0, -1, 1500, 2000, 1999};
                const int arm = times[op.a % 5];
                const int armed = op.b & 1, added = (op.b >> 1) & 1;
                w.Raw(&arm, 4);
                w.Raw(&armed, 4);
                w.Raw(&added, 4);
                w.End();
                if (op.d % 3 && !ownerFirst)
                    w.EntryOf(2, int(op.d % 3 == 1 ? op.c % 12 : 999));
                w.End();
                name = F("mn_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            // (Not an armed load into a mine whose item lacks the component: harmless until it explodes, guarded there.)
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_MnChunk>(O_MN_LOAD)(into, &chunk) : reinterpret_cast<C_Mine *>(into)->LoadGame(chunk);
            // (Where the load left the reader: the next chunk's id.)
            chunk.Ascend();
            uint16_t after = 0;
            chunk.Descend(after);
            next = after;
            chunk.Close();
        }
        return F("SaveGame %d -> %d LoadGame %d into %s -> %d next %04x bytes ", mi, int(rs), load, NameOf(Ptr(into)).c_str(),
                 int(rl), next) +
               hex;
    }
    case OP_STOP_START: {
        const bool server = op.c & 1;
        const int what = op.d % 3;                      // (0: stop and start; 1: stop; 2: start)
        if (what != 2) {
            if (o)
                OriginalAt<T_MnInt>(O_MN_STOP)(b, server);
            else
                p->StopMission(server);
        }
        if (what != 1) {
            if (o)
                OriginalAt<T_MnInt>(O_MN_START)(b, server);
            else
                p->StartMission(server);
        }
        return F("StopStart %d %d %d", mi, what, int(server));
    }
    case OP_RESTART: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_MnInt>(O_MN_RESTART)(b, server);
        else
            p->RestartMission(server);
        return F("RestartMission %d %d", mi, int(server));
    }
    default: {
        uint8_t *frame = op.b & 1 ? g_mineFrame[g_side][mi] : nullptr;
        const int r = o ? OriginalAt<int(TC *)(void *, void *)>(O_MN_MATERIAL)(b, frame)
                        : p->GetMaterialId(AsFrame(frame));
        return F("GetMaterialId %d -> %d", mi, r);
    }
    }
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("mn_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_mnShow;
void RunMnCase(const S_MnCase &c)
{
    g_caseStart = GetTickCount();
    ++g_mnCases;
    g_holdFrees = true;
    g_current = F("case %d", g_mnCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32(), driverSeed = U32();
    g_mineCount = int(c.mines.size());
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        g_bits[side].clear();
        BuildWorld(side, worldSeed);
        MakeItemDefs(c.defSeed);
        // The cars' drivers: a human or none (the original's at +0x24AC, the port's member).
        uint32_t ds = driverSeed;
        for (const auto &a : W().actors)
            if (a.type == ACTOR_CAR) {
                ds = ds * 1664525u + 1013904223u;
                uint8_t *driver = (ds >> 8) % 3 ? ActorOf(ACTOR_HUMAN, int(ds >> 12)) : nullptr;
                if (side)
                    Mut<uint8_t *>(a.bytes, 0x24AC) = driver;
                else
                    reinterpret_cast<C_Car *>(a.bytes)->m_driver = reinterpret_cast<C_Human *>(driver);
            }
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        for (int i = 0; i < g_mineCount; ++i) {
            const S_MnMine &mc = c.mines[size_t(i)];
            uint8_t *frame = Child(sector, FRAME_DUMMY, F("mine%d", i), nullptr);
            S_matrix &m = AsFrame(frame)->m_localMatrix;
            m.Identity();
            const float ch = std::cos(mc.heading), sh = std::sin(mc.heading), ct = std::cos(mc.tilt), st = std::sin(mc.tilt);
            m.m[0][0] = ch;
            m.m[0][2] = -sh;
            m.m[1][1] = ct;
            m.m[1][2] = st;
            m.m[2][0] = sh * ct;
            m.m[2][1] = -st;
            m.m[2][2] = ch * ct;
            if (mc.odd) {
                static const float rows[4][3] = {{1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1e-5f}};
                for (int k = 0; k < 3; ++k)
                    m.m[2][k] = rows[mc.odd - 1][k];
            }
            m.m[3][0] = mc.x;
            m.m[3][1] = mc.y;
            m.m[3][2] = mc.z;
            g_mineFrame[side][i] = frame;
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        // The mines: made, their frames set, started.
        for (int i = 0; i < g_mineCount; ++i) {
            uint8_t *b;
            if (side) {
                b = Block(0x120 + 16);
                OriginalAt<T_Void>(O_MN_CTOR)(b);
            } else {
                b = Block(sizeof(C_Mine) + 16);
                new (b) C_Mine();
            }
            g_mine[side][i] = b;
            Name(b, F("mine %d", i));
            if (side)
                Name(b + 0xA0, F("mine %d sphere", i));
            else
                Name(&PortMine(i)->m_volume, F("mine %d sphere", i));
            if (side) {
                Mut<uint8_t *>(b, 0x20) = g_mineFrame[1][i];
                out.push_back(F("SetFrame %d -> %d", i, int(OriginalAt<bool(TC *)(void *, void *)>(O_MN_SET_FRAME)(b, g_mineFrame[1][i]))));
            } else {
                PortMine(i)->m_frame = AsFrame(g_mineFrame[0][i]);
                out.push_back(F("SetFrame %d -> %d", i, int(PortMine(i)->SetFrame(AsFrame(g_mineFrame[0][i])))));
            }
            if (c.mines[size_t(i)].started) {
                const bool server = (c.role & GAME_ROLE_SERVER) != 0;
                if (side)
                    OriginalAt<T_MnInt>(O_MN_START)(b, server);
                else
                    PortMine(i)->StartMission(server);
            }
        }
        for (const std::string &s : MinesState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_MN_TRACE") ? std::atoi(std::getenv("HD2_MN_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_mnCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunMnOp(c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : MinesState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_mineCount; ++i) {
            uint8_t *b = g_mine[side][i];
            if (side) {
                OriginalAt<T_MnInt>(O_MN_STOP)(b, 1);
                OriginalAt<T_Void>(O_MN_DTOR)(b);
            } else {
                PortMine(i)->StopMission(true);
                PortMine(i)->~C_Mine();
            }
        }
        // The creator: a mine made and destroyed.
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(O_MN_CREATE)(OriginalAt<void *>(O_MN_CREATOR));
            Name(made, "created mine");
            out.push_back(F("created type %d", At<int>(made, 0x18)));
            OriginalAt<void(TC *)(void *, void *)>(O_MN_DESTROY)(OriginalAt<void *>(O_MN_CREATOR), made);
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_MINE];
            C_Actor *made = creator->Create();
            Name(made, "created mine");
            out.push_back(F("created type %d", int(made->GetType())));
            creator->Destroy(made);
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        ClearItemDefs();
        for (int i = 0; i < MAX_MINES; ++i)
            g_mine[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_MN_ONLY") ? std::atoi(std::getenv("HD2_MN_ONLY")) : 0;
    if (g_mnShow && (!only || only == g_mnCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_mnShow == 2)
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

void SetUpMine()
{
    SetUpCmd();
    SetUpMineAnswers();
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
        {0x004d11f0, reinterpret_cast<const void *>(&O_GetDriver)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("mn_diff: main");
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
        std::printf("mn_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_MN_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_MN_SHOW"))
        g_mnShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_MN_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_MN_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpMine();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunMnCase(RandomMnCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("mn_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_mnCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
