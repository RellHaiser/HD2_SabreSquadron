// Seat weapon part 1: the weapons mounted at vehicles' seats (game/seat_weapon.cpp, the original's 0x004c1230..0x004c7c70)
// against the original executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the
// weapon's calls go through sw_calls.s / sw_calls.inc (gen_cmd_calls.py sw). The model, sound and animation caches run
// on both sides (game/resource_cache.cpp linked; the original's constructed by their static initialisers'
// constructors); the LS3DF math is LS3DF.dll's on both sides; BuildBasis is the original's own on both sides.
//
// Each case builds the same world on each side (cmd_diff's, plus a weapon model under the primary sector: per seat its
// HORT / VERT / AIM dummies, BARREL visuals with their SHDUM and CARDUM dummies and user properties (the item, the
// turning rates and limits, some missing), a VIEW visual, a projectile, a SEAT dummy; some parts missing), makes one or
// two weapons on each side (the original's by its constructor 0x004c19d0 in a block of 0x180, the port's by its own)
// and runs the same random sequence of operations in the case's role: Init (on a car, a static weapon, another actor;
// now and then without the model or the seat's frame), Stop, Restart, SetGunner, ShowView, updates and ticks, the
// gunner's controls, FireShot, PlayShot, Reload, ShowReload, SetRounds, the getters, CanAim, saves and loads, the
// barrels' animation notes (a shell thrown), the messages (SeatWeaponSendMessage, MakeSeatWeaponMessage, their Write and
// Read). The frames' answers turn them for real: SetWorldDir sets the rotation (a parent's turn taken out), Update makes
// the children's world matrices stale. After each operation the answer, every weapon's fields, its item's rounds and the
// frames are compared; at the end every call made into the game and the engine (the sounds' and shells' set-up, the
// shots, the flashes, the gunner's view, the messages' contents).
// MinGW; build_sw.bat; runs sw_diff.exe ("sw_diff: N checks, 0 failures"). Env: HD2_SW_CASES (default 1,500;
// HD2_SW_QUICK: 300), HD2_SW_SEED, HD2_SW_SHOW (1: the port's lines; 2: both), HD2_SW_ONLY (the case shown),
// HD2_SW_TRACE (a case's operations printed as they run).
// (Included by st_diff.cpp, the static weapon's test, with SW_DIFF_NO_MAIN and its own name and calls.)
#ifndef HARNESS_NAME
#define HARNESS_NAME "sw_diff"
#define CMD_CALLS_INC "sw_calls.inc"
#endif
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/collision.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#define g_noPhysicsObject g_noPhysicsObjectOfPhysicsH   // (cmd_diff.cpp declares the port's its own way)
#include "game/physics.h"
#undef g_noPhysicsObject
#include "game/resource_cache.h"
#include "game/seat_weapon.h"
#include "game/sound_table.h"
#include "game/vehicle.h"
#include "ls3d/igraph.h"
#include "net/bitstream.h"
#include "net/net_object.h"
#undef protected
#undef private

// ---- the port's side of what the weapon calls and the test provides ----
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
void __stdcall S_quat::Make(const S_vector &axis, float angle)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_quat *, const S_vector &, float)>("?Make@S_quat@@QAGXABUS_vector@@M@Z");
    f(this, axis, angle);
}
void __stdcall S_quat::Inverse(S_vector &axis, float &angle) const
{
    static const auto f = Ls3dfFn<void(__stdcall *)(const S_quat *, S_vector &, float &)>("?Inverse@S_quat@@QBGXAAUS_vector@@AAM@Z");
    f(this, axis, angle);
}
float __stdcall S_vector::AngleTo(const S_vector &v) const
{
    static const auto f = Ls3dfFn<float(__stdcall *)(const S_vector *, const S_vector &)>("?AngleTo@S_vector@@QBGMABU1@@Z");
    return f(this, v);
}
// The original's own on both sides.
void BuildBasis(const S_vector &dir, S_vector &side, S_vector &up)
{
    OriginalAt<void(__fastcall *)(const S_vector *, S_vector *, S_vector *)>(0x00544d20)(&dir, &side, &up);
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
// The actors' creators (none made here).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;

// The messages' bases (net/net_object.cpp, net/net_track.cpp: not linked): as theirs.
C_NetObjectMessage::C_NetObjectMessage(int t) : type(t) {}
bool C_NetObjectMessage::MarkSent(int player)
{
    const int bit = 1 << player;
    if (sentTo & bit)
        return false;
    sentTo |= bit;
    return true;
}
void C_NetQuantized::SetReadQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) {}
void C_NetQuantized::SetWriteQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) {}
// The local player's id (the original reads +0xDC of the replication).
namespace H {
int g_localId = 3;
}
int C_NetReplication::GetLocalId() const { return H::g_localId; }

// The sound driver (ISndGetDriver: LS3DF's; the original's import thunk redirected): each side's world's.
namespace H {
uint8_t *g_sndDriver[2];
void *__cdecl O_ISndGetDriver() { return g_sndDriver[1]; }
}  // namespace H
extern "C" ISND_driver *ISndGetDriver() { return reinterpret_cast<ISND_driver *>(H::g_sndDriver[H::g_side]); }

namespace H {

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
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::deque<std::string> g_texts[2];                     // (names: stable storage)
const char *Text(const std::string &s)
{
    g_texts[g_side].push_back(s);
    return g_texts[g_side].back().c_str();
}
// A world frame under `parent` (its name, owner model; shown).
uint8_t *Child(uint8_t *parent, int type, const std::string &name, uint8_t *owner, uint32_t visual = I3DVISUAL_OBJECT)
{
    uint8_t *b = NewFrame(type, name);
    S_FakeFrame &f = W().frames.back();
    if (type == FRAME_VISUAL)
        f.visualType = visual;
    I3D_frame *x = AsFrame(b);
    x->m_name = f.name.c_str();
    x->m_ownerModel = reinterpret_cast<I3D_frame *>(owner);
    x->m_frameFlags |= 1;
    if (parent)
        LinkUnder(b, parent);
    return b;
}
// A frame's and its children's world matrices made stale (the engine's Update and SetPos do so).
void Stale(I3D_frame *f)
{
    f->m_frameFlags &= ~uint32_t(FRMFLAGS_WMATRIX_VALID);
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
        Stale(c);
}
S_vector V(float x, float y, float z) { return S_vector(x, y, z); }
float Dot3(const S_vector &a, const S_vector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
S_vector Cross3(const S_vector &a, const S_vector &b)
{
    return S_vector(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
S_vector Unit(const S_vector &v)
{
    const float l = std::sqrt(Dot3(v, v));
    return l > 1e-20f ? S_vector(v.x / l, v.y / l, v.z / l) : S_vector(0.0f, 0.0f, 1.0f);
}

// ---- the humans, the owners ----
uint8_t *HumanActor(int n)
{
    const auto &humans = W().humans;
    return humans.empty() ? nullptr : W().actors[size_t(humans[size_t(n) % humans.size()])].bytes;
}
uint8_t *ActorOfType(int type, int n)
{
    std::vector<uint8_t *> found;
    for (const W_Actor &a : W().actors)
        if (a.type == type)
            found.push_back(a.bytes);
    return found.empty() ? nullptr : found[size_t(n) % found.size()];
}
uint8_t *AnyActor(int n)
{
    const auto &actors = W().actors;
    return actors.empty() ? nullptr : actors[size_t(n) % actors.size()].bytes;
}

// ---- the items: a weapon item, its definition and its component on each side, in each side's layout ----
struct S_FakeItem {
    uint8_t *item, *def, *comp;
};
std::vector<S_FakeItem> g_items[2];
void **g_itemVtable[2], **g_compVtable[2], **g_physicsVtable[2], **g_itemDefVtable[2];
constexpr int FAIL_ITEM_ID = 99;                        // (CreateFreeItem gives none)
float ItemRounds(const uint8_t *item)
{
    return g_side ? At<float>(item, 0x50) : reinterpret_cast<const C_WeaponItem *>(item)->m_rounds;
}
void SetItemRounds(uint8_t *item, float rounds)
{
    if (g_side)
        Mut<float>(item, 0x50) = rounds;
    else
        reinterpret_cast<C_WeaponItem *>(item)->m_rounds = rounds;
}
uint8_t *MakeItem(int typeId)
{
    const bool o = g_side != 0;
    S_FakeItem x;
    x.item = Block(std::max(sizeof(C_WeaponItem), size_t(0xB0)) + 0x40);
    x.def = Block(std::max(sizeof(C_ItemDef), size_t(0x100)) + 0x40);
    x.comp = Block(std::max(sizeof(C_WeaponComp), size_t(0x80)) + 0x40);
    const int n = int(g_items[g_side].size());
    Name(x.item, F("item %d", n));
    Name(x.def, F("item def %d", n));
    Name(x.comp, F("item comp %d", n));
    *reinterpret_cast<void ***>(x.item) = g_itemVtable[g_side];
    *reinterpret_cast<void ***>(x.comp) = g_compVtable[g_side];
    if (g_itemDefVtable[g_side])                        // (st_diff's: the definitions' GetAmmoItemId)
        *reinterpret_cast<void ***>(x.def) = g_itemDefVtable[g_side];
    static const float delays[] = {0.0f, 50.0f, 100.0f, 333.5f, 1000.0f};
    static const float rounds[] = {30.0f, 1.0f, 0.0f, 7.5f, 2.0f, 100.0f};
    const int kind = typeId % 3 == 0 ? ICK_PROJECTILE_WEAPON : ICK_FIREARM;
    const float delay = delays[typeId % 5], start = rounds[(typeId / 3) % 6];
    const int effect = 10 + typeId, text = 0x100 + typeId, shellSound = typeId % 2 ? 5 + typeId : -1;
    if (o) {
        Mut<uint8_t *>(x.item, 4) = x.def;
        Mut<int>(x.item, 0x0C) = typeId;
        Mut<float>(x.item, 0x50) = start;
        Mut<int>(x.def, 0x14) = text;
        Mut<int>(x.def, 0x3C) = shellSound;
        Mut<uint8_t *>(x.def, 0x48) = x.comp;
        Mut<int>(x.comp, 0x08) = kind;
        Mut<float>(x.comp, 0x18) = delay;
        Mut<int>(x.comp, 0x1C) = effect;
    } else {
        C_WeaponItem *i = reinterpret_cast<C_WeaponItem *>(x.item);
        C_ItemDef *d = reinterpret_cast<C_ItemDef *>(x.def);
        C_WeaponComp *c = reinterpret_cast<C_WeaponComp *>(x.comp);
        i->m_def = d;
        i->m_typeId = typeId;
        i->m_rounds = start;
        d->m_textId = text;
        d->m_shellSound = shellSound;
        d->m_component = c;
        c->m_kind = E_ItemComponentKind(kind);
        c->m_attackDelay = delay;
        c->m_effect = effect;
    }
    g_items[g_side].push_back(x);
    return x.item;
}

// ---- the sound groups (the sound module's GetGroup): each side's layout ----
std::map<int, uint8_t *> g_groups[2];
uint8_t *SoundGroup(int id)
{
    auto it = g_groups[g_side].find(id);
    if (it != g_groups[g_side].end())
        return it->second;
    // (Group 5 of a chapter: none; 6: no samples; the samples' files "w<id>.wav", "bad" ones that do not open.)
    uint8_t *g = nullptr;
    if (id % 8 != 5) {
        const int samples = id % 8 == 6 ? 0 : 1 + id % 2;
        const float volumes[] = {1.0f, 0.5f, 0.25f}, nears[] = {0.0f, 2.0f, 5.5f}, fars[] = {20.0f, 50.0f, 100.0f};
        std::vector<uint8_t *> list;
        for (int s = 0; s < samples; ++s) {
            const std::string file = id % 8 == 7 && s == 0 ? F("bad%d.wav", id) : F("w%d_%d.wav", id, s);
            uint8_t *x = Block(std::max(sizeof(S_SoundSample), size_t(0x30)) + 0x10);
            Name(x, F("sample %d.%d", id, s));
            if (g_side) {
                OriginalAt<T_Fstr>(0x00405100)(x, "%s", file.c_str());
                Mut<float>(x, 0x08) = nears[(id + s) % 3];
                Mut<float>(x, 0x0C) = fars[(id + s) % 3];
                Mut<float>(x, 0x14) = volumes[(id + s) % 3];
            } else {
                S_SoundSample *p = new (x) S_SoundSample();
                p->fileName = file.c_str();
                p->nearRange = nears[(id + s) % 3];
                p->farRange = fars[(id + s) % 3];
                p->volume = volumes[(id + s) % 3];
            }
            list.push_back(x);
        }
        g = Block(std::max(sizeof(S_SoundGroup), size_t(0x18)) + 0x10);
        Name(g, F("sound group %d", id));
        if (g_side) {
            // (The original reads the samples' first at +4 and tests +0xC for none: an empty group's all null.)
            if (!list.empty()) {
                uint8_t **items = reinterpret_cast<uint8_t **>(Block(4 * (list.size() + 1)));
                std::copy(list.begin(), list.end(), items);
                Mut<uint8_t **>(g, 0x04) = items;
                Mut<uint8_t **>(g, 0x08) = items + list.size();
                Mut<uint8_t **>(g, 0x0C) = items + list.size();
            }
        } else {
            S_SoundGroup *p = new (g) S_SoundGroup();
            for (uint8_t *x : list)
                p->samples.push_back(reinterpret_cast<S_SoundSample *>(x));
        }
    }
    g_groups[g_side][id] = g;
    return g;
}

// ---- the weapons ----
constexpr int MAX_WEAPONS = 2;
uint8_t *g_weapon[2][MAX_WEAPONS];                      // (the side's weapons: the original's blocks, the port's objects)
int g_weaponCount;
uint8_t *g_model[2];                                    // (the case's weapon model)
uint8_t *g_seatFrame[2][MAX_WEAPONS];
int g_seatOf[MAX_WEAPONS];                              // (each weapon's seat in the case)
uint8_t *g_shellsModel[2][8];                           // (the shells models the cache opened)
C_SeatWeapon *PortWeapon(int i) { return reinterpret_cast<C_SeatWeapon *>(g_weapon[0][i]); }
// What the operations' guards read, in each side's layout.
struct S_SwView {
    uint32_t item = 0, hort = 0, vert = 0, aim = 0, shells = 0, gunner = 0, model = 0, owner = 0;
    int barrels = 0, barrel = 0;
    bool failed = false, started = false;
};
S_SwView ViewOf(const uint8_t *b)
{
    S_SwView v;
    if (g_side) {
        v.item = At<uint32_t>(b, 0x158);
        v.hort = At<uint32_t>(b, 0x13C);
        v.vert = At<uint32_t>(b, 0x140);
        v.aim = At<uint32_t>(b, 0x144);
        v.shells = At<uint32_t>(b, 0x148);
        v.gunner = At<uint32_t>(b, 0xFC);
        v.model = At<uint32_t>(b, 0xEC);
        v.owner = At<uint32_t>(b, 0x100);
        const uint32_t first = At<uint32_t>(b, 0x10C), last = At<uint32_t>(b, 0x110);
        v.barrels = first ? int((last - first) / 0x10) : 0;
        v.barrel = At<int>(b, 0x150);
        v.failed = b[0x138] != 0;
        v.started = b[0x104] != 0;
    } else {
        const C_SeatWeapon *w = reinterpret_cast<const C_SeatWeapon *>(b);
        v.item = Ptr(w->m_item);
        v.hort = Ptr(w->m_hort);
        v.vert = Ptr(w->m_vert);
        v.aim = Ptr(w->m_aim);
        v.shells = Ptr(w->m_shells);
        v.gunner = Ptr(w->m_gunner);
        v.model = Ptr(w->m_model);
        v.owner = Ptr(w->m_owner);
        v.barrels = int(w->m_barrels.size());
        v.barrel = w->m_barrel;
        v.failed = w->m_failed;
        v.started = w->m_started;
    }
    return v;
}

std::map<uint32_t, bool> g_localPlayers[2];
// The barrels' animations that never open (those without a muzzle: "_f<seat>_<n>").
std::vector<std::string> g_noAnims;

// ---- the answers ----
// The ReadBits answers, in turn on each side (the case's values first).
std::deque<uint32_t> g_bits[2];
bool g_bitsEmptyOnes;                                   // (st_diff's: all ones once the case's are read, ending the stores)
std::map<uint32_t, int> g_soundOpens[2];
uint8_t *g_netObject[2], *g_replication[2], *g_physicsModule[2], *g_sender[2];
void SetUpSwAnswers()
{
    // Frames by name under a frame: a trailing '*' (ENUMF_WILDMASK) matches a prefix; the types by the mask's bits
    // (1 << (type - 1)).
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        const bool wild = (a[2] & ENUMF_WILDMASK) && !want.empty() && want.back() == '*';
        const uint32_t types = a[2] & 0xFFFF;
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                const std::string n = c->m_name ? c->m_name : "";
                const bool named = wild ? n.compare(0, want.size() - 1, want, 0, want.size() - 1) == 0 : n == want;
                if (named && c->m_type >= 1 && (types & (1u << (c->m_type - 1))))
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
    g_answers["I3D_frame::LinkTo"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *c = reinterpret_cast<I3D_frame *>(a[0]);
        for (I3D_frame *p = reinterpret_cast<I3D_frame *>(a[1]); p; p = p->m_parent)
            if (p == c)
                return 0x80000001u;
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
        Stale(c);
        return 0u;
    };
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    // Update: the frame's and its children's world matrices made again (when next read).
    g_answers["I3D_frame::Update"] = [](const std::vector<uint32_t> &a) {
        Stale(reinterpret_cast<I3D_frame *>(a[0]));
        return 0u;
    };
    // SetWorldDir: the rotation that turns the local z axis to `dir` in the world (y up; the parent's turn taken out;
    // the roll about it).
    g_answers["I3D_frame::SetWorldDir"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector &dir = *reinterpret_cast<const S_vector *>(a[1]);
        float roll;
        std::memcpy(&roll, &a[2], 4);
        S_vector d = dir;
        if (f->m_parent) {
            const S_matrix &p = f->m_parent->GetMatrix();
            const S_vector rx = Unit(p(0)), ry = Unit(p(1)), rz = Unit(p(2));
            d = S_vector(Dot3(dir, rx), Dot3(dir, ry), Dot3(dir, rz));
        }
        const S_vector z = Unit(d);
        S_vector x = Cross3(S_vector(0.0f, 1.0f, 0.0f), z);
        x = Dot3(x, x) > 1e-12f ? Unit(x) : S_vector(1.0f, 0.0f, 0.0f);
        S_vector y = Cross3(z, x);
        if (roll != 0.0f) {
            const float c = std::cos(roll), s = std::sin(roll);
            const S_vector rx(x.x * c + y.x * s, x.y * c + y.y * s, x.z * c + y.z * s);
            const S_vector ry(y.x * c - x.x * s, y.y * c - x.y * s, y.z * c - x.z * s);
            x = rx;
            y = ry;
        }
        S_matrix m;
        m.Identity();
        m(0) = x;
        m(1) = y;
        m(2) = z;
        S_quat q;
        q.Make(m);
        f->SetRot(q);
        Stale(f);
        return 0u;
    };
    // The barrels' callbacks.
    g_answers["I3D_frame::SetCallback"] = [](const std::vector<uint32_t> &a) {
        reinterpret_cast<I3D_frame *>(a[0])->m_callback = reinterpret_cast<I3D_CALLBACK *>(a[1]);
        return 0u;
    };
    // A frame's data (the barrels' unknown1C: the weapon): set.
    g_answers["SetFrameData"] = [](const std::vector<uint32_t> &a) {
        S_FrameData *d = reinterpret_cast<S_FrameData *>(Block(0x20));
        std::memcpy(d, reinterpret_cast<const void *>(a[1]), 0x20);
        reinterpret_cast<I3D_frame *>(a[0])->m_data = d;
        return 0u;
    };
    g_answers["I3D_frame::AddRef"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(++reinterpret_cast<I3D_frame *>(a[0])->m_refCount);
    };
    g_answers["I3D_frame::Duplicate"] = [](const std::vector<uint32_t> &) { return 0u; };
    g_answers["I3D_driver::CreateVisual"] = [](const std::vector<uint32_t> &a) {
        return Ptr(Child(nullptr, FRAME_VISUAL, F("visual%d", W().created++), nullptr, a[1]));
    };
    g_answers["I3D_driver::CreateFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(Child(nullptr, int(a[1]), F("new%d", W().created++), nullptr));
    };
    // The shells model (the model cache's Open): its SHELL visual.
    g_answers["I3D_model::Open"] = [](const std::vector<uint32_t> &a) {
        const std::string file = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        uint8_t *m = reinterpret_cast<uint8_t *>(a[0]);
        Child(m, FRAME_VISUAL, "SHELL", m);
        Mut<const char *>(m, 0x134) = Text(file);
        return 0u;
    };
    // A model's copy (the cache's Duplicate): its frames replaced by copies of the source's.
    g_answers["I3D_model::DuplicateModel"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *dest = reinterpret_cast<I3D_frame *>(a[0]);
        while (dest->m_firstChild) {
            I3D_frame *c = dest->m_firstChild;
            dest->m_firstChild = c->m_nextSibling;
            c->m_parent = nullptr;
            c->m_nextSibling = nullptr;
        }
        for (I3D_frame *c = reinterpret_cast<I3D_frame *>(a[1])->m_firstChild; c; c = c->m_nextSibling) {
            uint8_t *b = Child(reinterpret_cast<uint8_t *>(dest), c->m_type, c->m_name ? c->m_name : "",
                               reinterpret_cast<uint8_t *>(dest));
            Name(b, "frame " + std::string(c->m_name ? c->m_name : "") + F(" copy%d", W().created++));
        }
        Mut<const char *>(dest, 0x134) = Mut<const char *>(reinterpret_cast<void *>(a[1]), 0x134);
        return 0u;
    };
    // The animation cache's sets: made by the driver, opened by name (those of a barrel 1 missing now and then).
    g_answers["I3D_driver::CreateAnimationSet"] = [](const std::vector<uint32_t> &) {
        uint8_t *anim = Block(0x40);
        *reinterpret_cast<void ***>(anim) = EngineVtable("I3D_animation_set");
        Mut<int>(anim, 4) = 1;
        Name(anim, F("anim set %d", W().created++));
        return Ptr(anim);
    };
    // (Never a barrel's without a muzzle: the port drops its reference, the original keeps it.)
    g_answers["I3D_animation_set::Open"] = [](const std::vector<uint32_t> &a) {
        const std::string name = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        Mut<const char *>(reinterpret_cast<void *>(a[0]), 0x0C) = Text(name);
        for (const std::string &n : g_noAnims)
            if (name.find(n) != std::string::npos)
                return uint32_t(I3DERR_NOFILE);
        return name.find("_01") != std::string::npos && NextFake() % 2 ? uint32_t(I3DERR_NOFILE) : 0u;
    };
    g_answers["I3D_animation_set::AddRef"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(++Mut<int>(reinterpret_cast<void *>(a[0]), 4));
    };
    g_answers["I3D_animation_set::Release"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(--Mut<int>(reinterpret_cast<void *>(a[0]), 4));
    };
    // The sounds: the "bad" files do not open.
    g_answers["I3D_sound::Open"] = [](const std::vector<uint32_t> &a) {
        const std::string file = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        return file.find("bad") != std::string::npos ? uint32_t(I3DERR_NOFILE) : 0u;
    };
    g_answers["ISND_driver::IsInitialized"] = [](const std::vector<uint32_t> &) { return 1u; };
    g_answers["C_SoundModule::GetGroup"] = [](const std::vector<uint32_t> &a) {
        uint8_t *g = SoundGroup(int(a[2]));
        if (std::getenv("HD2_SW_DEBUG") && g)
            std::printf("group side %d id %d %p: %08x %08x %08x %08x\n", g_side, int(a[2]), static_cast<void *>(g),
                        At<uint32_t>(g, 0), At<uint32_t>(g, 4), At<uint32_t>(g, 8), At<uint32_t>(g, 12));
        return Ptr(g);
    };
    // The items.
    g_answers["C_ItemsModule::CreateFreeItem"] = [](const std::vector<uint32_t> &a) {
        return int(a[1]) == FAIL_ITEM_ID ? 0u : Ptr(MakeItem(int(a[1])));
    };
    g_answers["C_WeaponItem::GetAmount"] = [](const std::vector<uint32_t> &a) {
        const float r = ItemRounds(reinterpret_cast<uint8_t *>(a[0]));
        uint32_t w;
        std::memcpy(&w, &r, 4);
        return w;
    };
    g_answers["C_WeaponItem::Use"] = [](const std::vector<uint32_t> &a) {
        float amount;
        std::memcpy(&amount, &a[1], 4);
        uint8_t *item = reinterpret_cast<uint8_t *>(a[0]);
        SetItemRounds(item, ItemRounds(item) - amount);
        return 0u;
    };
    // A reload: mostly done (the rounds full again), the magazine's uid given.
    g_answers["C_WeaponItem::Reload"] = [](const std::vector<uint32_t> &a) {
        if (int *uid = reinterpret_cast<int *>(a[3]))
            *uid = 77;
        if (NextFake() % 4 == 0)
            return 0u;
        SetItemRounds(reinterpret_cast<uint8_t *>(a[0]), 30.0f);
        return 1u;
    };
    g_answers["C_WeaponComp::GetAimedDispersion"] = [](const std::vector<uint32_t> &) {
        static const float values[] = {0.0f, 0.5f, 2.0f, 10.0f};
        uint32_t w;
        std::memcpy(&w, &values[NextFake() % 4], 4);
        return w;
    };
    g_answers["C_WeaponComp::GetReloadTimeMs"] = [](const std::vector<uint32_t> &) {
        static const int values[] = {0, 1, 500, 2000, 99};
        return uint32_t(values[NextFake() % 5]);
    };
    g_answers["C_WeaponComp::GetReloadSound"] = [](const std::vector<uint32_t> &) { return NextFake() % 3 ? 4u : uint32_t(-1); };
    // The owner's seats.
    g_answers["C_Actor::GetSeatCount"] = [](const std::vector<uint32_t> &) { return NextFake() % 4; };
    g_answers["C_Actor::GetSeatOccupant"] = [](const std::vector<uint32_t> &) {
        return NextFake() % 3 ? Ptr(HumanActor(int(NextFake() % 7))) : 0u;
    };
    // The gunner's view (tiny and zero now and then: the normalisations' edges); the view handed back logged with its w.
    g_answers["C_Human::GetViewDir"] = [](const std::vector<uint32_t> &) {
        static S_vector dirs[2];
        static const S_vector odd[] = {S_vector(1e-5f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, -1e-5f), S_vector(0.0f, 0.0f, 0.0f),
                                       S_vector(0.0f, 1.0f, 0.0f), S_vector(0.0f, -1.0f, 0.0f)};
        // (Mostly the same as before: the turret settles, its motor stops.)
        if (NextFake() % 4 == 0) {
            const uint32_t r = NextFake() % 12;
            static const float values[] = {0.0f, 1.0f, 0.5f, -0.75f, -1.0f, 0.25f, 3.0f};
            dirs[g_side] = r < 5 ? odd[r] : S_vector(values[NextFake() % 7], values[NextFake() % 7], values[NextFake() % 7]);
        }
        Name(&dirs[g_side], "view dir");
        return Ptr(&dirs[g_side]);
    };
    // Whether a gunner is the local player: decided at his first ask in the case (a remote gunner's turret settles).
    g_answers["C_Human::IsLocalPlayer"] = [](const std::vector<uint32_t> &a) {
        auto it = g_localPlayers[g_side].find(a[0]);
        if (it == g_localPlayers[g_side].end())
            it = g_localPlayers[g_side].emplace(a[0], NextFake() % 2 == 0).first;
        return uint32_t(it->second);
    };
    g_answers["C_Human::SetViewDir"] = [](const std::vector<uint32_t> &a) {
        g_log[g_side].push_back("  (view w " + FloatText(reinterpret_cast<const float *>(a[1])[3]) + ")");
        return 0u;
    };
    // The camera (2: the module's own): its position near the model now and then; its direction.
    g_answers["C_CameraModule::GetCameraPos"] = [](const std::vector<uint32_t> &a) {
        float *pos = reinterpret_cast<float *>(a[2]);
        static const float values[] = {0.0f, 2.0f, -3.0f, 10.0f, 100.0f};
        pos[0] = values[NextFake() % 5];
        pos[1] = values[NextFake() % 5];
        pos[2] = values[NextFake() % 5];
        pos[3] = 1.0f;
        return 1u;
    };
    g_answers["C_CameraModule::GetCameraDir"] = [](const std::vector<uint32_t> &a) {
        float *dir = reinterpret_cast<float *>(a[2]);
        static const float dirs[][4] = {{0.0f, 0.0f, 1.0f, 0.0f}, {0.6f, 0.0f, 0.8f, 0.0f}, {0.0f, -0.6f, 0.8f, 0.5f},
                                        {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
        std::memcpy(dir, dirs[NextFake() % 5], 16);
        return 1u;
    };
    // The aim's ray: nothing, or a hit part way (near the start now and then: within 3 m of a muzzle at the origin).
    auto ray = [](const std::vector<uint32_t> &a, int at) {
        const uint32_t r = NextFake() % 5;
        if (r == 0)
            return 0u;
        const S_vector &from = *reinterpret_cast<const S_vector *>(a[at]);
        const S_vector &dir = *reinterpret_cast<const S_vector *>(a[at + 1]);
        static const float parts[] = {0.5f, 0.001f, 0.1f, 1.0f};
        const float k = parts[r - 1];
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[at + 2]))
            *hit = S_vector(from.x + dir.x * k, from.y + dir.y * k, from.z + dir.z * k);
        static uint8_t volume[2][0x80];
        Name(volume[g_side], "ray volume");
        return Ptr(volume[g_side]);
    };
    g_answers["C_CollisionWorld::RayTest"] = [ray](const std::vector<uint32_t> &a) { return ray(a, 1); };
    g_answers["CollisionRayTest"] = [ray](const std::vector<uint32_t> &a) { return ray(a, 0); };
    // The physics module: the shells' objects (their description logged).
    g_answers["GetPhysicsModule"] = [](const std::vector<uint32_t> &) { return Ptr(g_physicsModule[g_side]); };
    g_answers["C_PhysicsModuleBase::CreateObject"] = [](const std::vector<uint32_t> &a) {
        const uint8_t *d = reinterpret_cast<const uint8_t *>(a[1]);
        g_log[g_side].push_back(F("  (desc frame %s type %d context %s material %d flags %d %d %d %d radius ",
                                  NameOf(At<uint32_t>(d, 0)).c_str(), At<int>(d, 4), NameOf(At<uint32_t>(d, 0x0C)).c_str(),
                                  At<int>(d, 0x10), int(d[0x14]), int(d[0x15]), int(d[0x16]), int(d[0x17])) +
                                FloatText(At<float>(d, 0x28)) + ")");
        static uint8_t object[2][0x20];
        Name(object[g_side], "shell object");
        return Ptr(object[g_side]);
    };
    // The network: the car's net object (always there), the sender's settings; the messages queued logged by contents.
    g_answers["C_NetReplication::FindObject"] = [](const std::vector<uint32_t> &) { return Ptr(g_netObject[g_side]); };
    g_answers["C_NetObject::QueueMessage"] = [](const std::vector<uint32_t> &a) {
        const uint8_t *m = reinterpret_cast<const uint8_t *>(a[1]);
        if (!m)
            return 0u;
        int type, seat = 0, barrel = 0, second = 0;
        bool fromClient;
        const float *pos = nullptr, *dir = nullptr;
        if (g_side) {
            type = At<int>(m, 0x1C);
            fromClient = m[0x2C] != 0;
            if (type == SEAT_WEAPON_MSG_SHOT) {
                dir = FP(m, 0x30);
                pos = FP(m, 0x40);
                seat = At<int>(m, 0x50);
                barrel = At<int>(m, 0x54);
            } else {
                seat = At<int>(m, 0x30);
                second = type == SEAT_WEAPON_MSG_RELOAD ? 0 : At<int>(m, 0x34);
            }
        } else {
            const C_NetObjectMessage *x = reinterpret_cast<const C_NetObjectMessage *>(m);
            type = x->type;
            fromClient = x->fromClient;
            if (type == SEAT_WEAPON_MSG_SHOT) {
                const auto *s = static_cast<const C_SeatWeaponShotMessage *>(x);
                dir = &s->dir.x;
                pos = &s->pos.x;
                seat = s->seat;
                barrel = s->barrel;
            } else if (type == SEAT_WEAPON_MSG_RELOAD) {
                seat = static_cast<const C_SeatWeaponReloadMessage *>(x)->seat;
            } else if (type == SEAT_WEAPON_MSG_ROUNDS) {
                seat = static_cast<const C_SeatWeaponRoundsMessage *>(x)->rounds;
                second = static_cast<const C_SeatWeaponRoundsMessage *>(x)->seat;
            } else {
                seat = static_cast<const C_SeatWeaponAmmoMessage *>(x)->typeId;
                second = static_cast<const C_SeatWeaponAmmoMessage *>(x)->uid;
            }
        }
        // (The words after the type: a shot's seat and barrel, a reload's seat, the rounds and their seat, an ammo item's
        // type and uid.)
        std::string s = F("  (message type %d client %d words %d %d %d", type, int(fromClient), seat, barrel, second);
        // (The direction's w: not sent; the original's is a stack word when it aims at what the camera sees.)
        if (pos)
            s += " pos " + VectorText(Ptr(pos), 4) + " dir " + VectorText(Ptr(dir), 3);
        g_log[g_side].push_back(s + ")");
        return 0u;
    };
    g_answers["C_NetMessage::GetSender"] = [](const std::vector<uint32_t> &) { return Ptr(g_sender[g_side]); };
    g_answers["C_NetMessage::WriteQuantized"] = [](const std::vector<uint32_t> &a) {
        g_log[g_side].push_back("  (quantized " + VectorText(a[1], 3) + ")");
        return 1u;
    };
    g_answers["C_NetMessage::ReadQuantized"] = [](const std::vector<uint32_t> &a) {
        float *v = reinterpret_cast<float *>(a[1]);
        v[0] = 1.5f;
        v[1] = -2.0f;
        v[2] = 30.25f;
        return 0u;
    };
    g_answers["C_BitStream::ReadBits"] = [](const std::vector<uint32_t> &a) {
        std::deque<uint32_t> &q = g_bits[g_side];
        uint32_t v = 0;
        if (!q.empty()) {
            v = q.front();
            q.pop_front();
        } else
            v = g_bitsEmptyOnes ? 0xFFFFFFFFu : NextFake();
        const int n = int(a[1]);
        return n >= 32 ? v : v & ((1u << n) - 1);
    };
    // Random numbers in [0, 1).
    g_answers["RandFloat"] = [](const std::vector<uint32_t> &) {
        static const float values[] = {0.0f, 0.25f, 0.5f, 0.75f, 0.9999f, 0.1f};
        uint32_t w;
        std::memcpy(&w, &values[NextFake() % 6], 4);
        return w;
    };
}

// ---- the original's weapons ----
constexpr uint32_t O_SW_CTOR = 0x004c19d0, O_SW_DTOR = 0x004c1aa0, O_SW_INIT = 0x004c1ad0, O_SW_STOP = 0x004c2380,
                   O_SW_RESTART = 0x004c6620, O_SW_FIRE_SHOT = 0x004c3dc0, O_SW_PLAY_SHOT = 0x004c4010,
                   O_SW_RELOAD = 0x004c6270, O_SW_SHOW_RELOAD = 0x004c62e0, O_SW_SET_ROUNDS = 0x004c6560,
                   O_SW_SAVE = 0x004c6660, O_SW_LOAD = 0x004c68b0, O_SW_SET_GUNNER = 0x004c6dd0,
                   O_SW_GET_ROUNDS = 0x004c6550, O_SW_ITEM_DEF = 0x004c57b0, O_SW_ITEM_TEXT = 0x004c57d0,
                   O_SW_AIM = 0x004c5870, O_SW_ORIGIN = 0x004c5800, O_SW_CAN_AIM = 0x004c6230,
                   O_SW_CONTROLS = 0x004c3b60, O_SW_SHOW_VIEW = 0x004c6d90, O_SW_C8 = 0x004c7670,
                   O_SW_UPDATE = 0x004c56a0, O_SW_TICK = 0x004c55e0, O_SW_NOTE = 0x004c1990, O_SW_SEND = 0x004c1230,
                   O_SW_MAKE = 0x004c14b0;
constexpr uint32_t O_MODEL_CACHE = 0x008aeda0, O_SOUND_CACHE = 0x008aed64, O_ANIM_CACHE = 0x008aed28;
constexpr uint32_t O_NET_REPLICATION = 0x0097d084;

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

// ---- the compared state ----
std::string Fl(float f) { return FloatText(f); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
std::vector<std::string> WeaponState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_weapon[g_side][i];
    struct S_Barrel {
        uint32_t frame, muzzle, ejector, animation;
    };
    std::vector<S_Barrel> barrels;
    if (g_side) {
        const uint32_t first = At<uint32_t>(b, 0x10C), last = At<uint32_t>(b, 0x110);
        for (uint32_t q = first; q && q < last; q += 0x10)
            barrels.push_back({At<uint32_t>(reinterpret_cast<void *>(q), 0), At<uint32_t>(reinterpret_cast<void *>(q), 4),
                               At<uint32_t>(reinterpret_cast<void *>(q), 8), At<uint32_t>(reinterpret_cast<void *>(q), 12)});
        out.push_back(F("weapon%d rest ", i) + V4(FP(b, 0x00)) + " " + V4(FP(b, 0x10)) + " " + Fl(At<float>(b, 0xC0)) + " " +
                      Fl(At<float>(b, 0xC4)) + F(" seat %d turn speed ", At<int>(b, 0xC8)) + Fl(At<float>(b, 0xCC)) +
                      F(" idle %d released %d tilt speed ", At<int>(b, 0xD0), int(b[0xD4])) + Fl(At<float>(b, 0xD8)) +
                      F(" released %d reload %d %d e8 %d", int(b[0xDC]), At<int>(b, 0xE0), At<int>(b, 0xE4), At<int>(b, 0xE8)));
        out.push_back(F("  model %s seat %s view %s projectile %s gunner %s owner %s started %d tilt ",
                        NameOf(At<uint32_t>(b, 0xEC)).c_str(), NameOf(At<uint32_t>(b, 0xF0)).c_str(),
                        NameOf(At<uint32_t>(b, 0xF4)).c_str(), NameOf(At<uint32_t>(b, 0xF8)).c_str(),
                        NameOf(At<uint32_t>(b, 0xFC)).c_str(), NameOf(At<uint32_t>(b, 0x100)).c_str(), int(b[0x104])) +
                      Fl(At<float>(b, 0x118)) + " turn " + Fl(At<float>(b, 0x11C)) +
                      F(" sounds %s %s limits ", NameOf(At<uint32_t>(b, 0x120)).c_str(), NameOf(At<uint32_t>(b, 0x124)).c_str()) +
                      VectorText(Ptr(b + 0x128), 4) + F(" failed %d", int(b[0x138])));
        out.push_back(F("  hort %s vert %s aim %s shells %s flags %x barrel %d delay %d item %s flash %d rates ",
                        NameOf(At<uint32_t>(b, 0x13C)).c_str(), NameOf(At<uint32_t>(b, 0x140)).c_str(),
                        NameOf(At<uint32_t>(b, 0x144)).c_str(), NameOf(At<uint32_t>(b, 0x148)).c_str(), At<uint32_t>(b, 0x14C),
                        At<int>(b, 0x150), At<int>(b, 0x154), NameOf(At<uint32_t>(b, 0x158)).c_str(), At<int>(b, 0x15C)) +
                      Fl(At<float>(b, 0x160)) + " " + Fl(At<float>(b, 0x164)) +
                      F(" launcher %d sender %s %s refill %u %d", int(b[0x168]), NameOf(At<uint32_t>(b, 0x16C)).c_str(),
                        NameOf(At<uint32_t>(b, 0x170)).c_str(), At<uint32_t>(b, 0x174), At<int>(b, 0x178)));
    } else {
        const C_SeatWeapon *w = reinterpret_cast<const C_SeatWeapon *>(b);
        for (const S_SeatWeaponBarrel &x : w->m_barrels)
            barrels.push_back({Ptr(x.frame), Ptr(x.muzzle), Ptr(x.ejector), Ptr(x.animation)});
        out.push_back(F("weapon%d rest ", i) + V4(&w->m_vertRestAxis.x) + " " + V4(&w->m_hortRestAxis.x) + " " +
                      Fl(w->m_vertRestAngle) + " " + Fl(w->m_hortRestAngle) + F(" seat %d turn speed ", w->m_seat) +
                      Fl(w->m_turnSpeed) + F(" idle %d released %d tilt speed ", w->m_idleTime, int(w->m_turnReleased)) +
                      Fl(w->m_tiltSpeed) +
                      F(" released %d reload %d %d e8 %d", int(w->m_tiltReleased), w->m_reloadLeft, w->m_reloadTime, w->m_unknownE8));
        float limits[4] = {w->m_upLimit, w->m_downLimit, w->m_leftLimit, w->m_rightLimit};
        out.push_back(F("  model %s seat %s view %s projectile %s gunner %s owner %s started %d tilt ",
                        NameOf(Ptr(w->m_model)).c_str(), NameOf(Ptr(w->m_seatFrame)).c_str(), NameOf(Ptr(w->m_view)).c_str(),
                        NameOf(Ptr(w->m_projectileFrame)).c_str(), NameOf(Ptr(w->m_gunner)).c_str(),
                        NameOf(Ptr(w->m_owner)).c_str(), int(w->m_started)) +
                      Fl(w->m_tilt) + " turn " + Fl(w->m_turn) +
                      F(" sounds %s %s limits ", NameOf(Ptr(w->m_motorSound)).c_str(), NameOf(Ptr(w->m_stopSound)).c_str()) +
                      VectorText(Ptr(limits), 4) + F(" failed %d", int(w->m_failed)));
        out.push_back(F("  hort %s vert %s aim %s shells %s flags %x barrel %d delay %d item %s flash %d rates ",
                        NameOf(Ptr(w->m_hort)).c_str(), NameOf(Ptr(w->m_vert)).c_str(), NameOf(Ptr(w->m_aim)).c_str(),
                        NameOf(Ptr(w->m_shells)).c_str(), w->m_flags, w->m_barrel, w->m_delay,
                        NameOf(Ptr(w->m_item)).c_str(), w->m_flashEffect) +
                      Fl(w->m_turnRate) + " " + Fl(w->m_tiltRate) +
                      F(" launcher %d sender %s %s refill %u %d", int(w->m_launcher),
                        NameOf(reinterpret_cast<uint32_t>(w->m_sender)).c_str(), NameOf(Ptr(w->m_senderContext)).c_str(),
                        w->m_refillTime, w->m_refillBelow));
    }
    std::string s = "  barrels";
    for (const S_Barrel &x : barrels)
        s += " {" + NameOf(x.frame) + " " + NameOf(x.muzzle) + " " + NameOf(x.ejector) + " " + NameOf(x.animation) + "}";
    out.push_back(s);
    const S_SwView v = ViewOf(b);
    if (v.item)
        out.push_back("  rounds " + Fl(ItemRounds(reinterpret_cast<uint8_t *>(v.item))));
    return out;
}
std::vector<std::string> SwFrameStates()
{
    std::vector<std::string> out;
    for (const S_FakeFrame &f : W().frames) {
        const I3D_frame *x = AsFrame(f.bytes);
        std::string data;
        if (x->m_data)
            data = F(" data %s %s", NameOf(Ptr(x->m_data->actor)).c_str(), NameOf(x->m_data->unknown1C).c_str());
        out.push_back(F("frame %s ref %d flags %08x pos ", NameOf(Ptr(f.bytes)).c_str(), int(x->m_refCount), x->m_frameFlags) +
                      VectorText(Ptr(&x->m_localMatrix.m[3][0]), 3) + " rot " + VectorText(Ptr(&x->m_rot), 4) + " parent " +
                      NameOf(Ptr(x->m_parent)) + " callback " + NameOf(reinterpret_cast<uint32_t>(x->m_callback)) + data);
    }
    return out;
}

// ---- one case ----
// The chunk writer (the made saves).
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
enum {
    OP_INIT, OP_STOP, OP_RESTART, OP_SET_GUNNER, OP_SHOW_VIEW, OP_UPDATE, OP_TICK, OP_CONTROLS, OP_FIRE_SHOT, OP_PLAY_SHOT,
    OP_RELOAD, OP_SHOW_RELOAD, OP_SET_ROUNDS, OP_GET, OP_SAVE_LOAD, OP_NOTE, OP_SEND, OP_MESSAGE, OP_KINDS
};
const char *const OP_KIND_NAMES[] = {"Init", "Stop", "Restart", "SetGunner", "ShowView", "Update", "Tick", "Controls",
                                     "FireShot", "PlayShot", "Reload", "ShowReload", "SetRounds", "Get", "SaveLoad", "Note",
                                     "Send", "Message"};
struct S_SwOp {
    int kind = 0;
    int weapon = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
// A seat's parts in the model.
struct S_SwSeat {
    int seat = 0;
    bool hort = true, vert = true, aim = true, view = true, seatUnderHort = true, vertUnderHort = true;
    int barrels = 1;                                    // (0..3)
    uint32_t noMuzzle = 0, noEjector = 0;               // (bits per barrel; barrel 0 always has its muzzle)
    std::string barrelProps, hortProps;
};
struct S_SwCase {
    std::vector<S_SwSeat> seats;                        // (one per weapon)
    bool projectile = true;
    std::string file;
    uint32_t role = GAME_ROLE_LOCAL;
    int ownerKind = 0;                                  // (0 a car, 1 a static weapon, 2 another actor)
    bool sender = true;
    std::vector<S_SwOp> ops;
};
int g_swCases, g_opCounts[OP_KINDS];
const int FRAME_TIMES[] = {0, 10, 16, 33, 100, 250, 1000, 3000, 1, 19, 299, 301};
std::string BarrelProps()
{
    // The item (its id; a failing one; "=" before it reads 0), the rates and the limits, some missing, in any case.
    static const int ids[] = {3, 4, 5, 7, 12, 30, FAIL_ITEM_ID};
    std::string s;
    const int idForm = I(0, 9);
    if (idForm < 8)
        s += F(idForm < 6 ? "itemid%d " : "ItemID%d ", ids[I(0, 6)]);
    else if (idForm == 8)
        s += "itemid=6 ";
    static const char *const keys[] = {"hs", "vs", "up", "down", "left", "right"};
    static const int values[] = {0, 10, 30, 45, 90, 180, 360};
    for (const char *k : keys)
        if (P(0.8))
            s += F("%s%d ", P(0.9) ? k : (std::string(k) == "up" ? "UP" : k), values[I(0, 6)]);
    return s;
}
S_SwCase RandomSwCase()
{
    S_SwCase c;
    const int weapons = P(0.7) ? 1 : 2;
    const int firstSeat = I(0, 2);
    for (int i = 0; i < weapons; ++i) {
        S_SwSeat s;
        s.seat = firstSeat + i;
        s.hort = P(0.95);
        s.vert = P(0.95);
        s.aim = P(0.7);
        s.view = P(0.7);
        s.seatUnderHort = P(0.6);
        s.vertUnderHort = P(0.9);
        s.barrels = P(0.05) ? 0 : I(1, 3);
        s.noMuzzle = P(0.2) ? uint32_t(I(0, 7)) & ~1u : 0u;
        s.noEjector = P(0.3) ? uint32_t(I(0, 7)) : 0u;
        s.barrelProps = BarrelProps();
        // (Group 0 too; a stop group empty (6) or missing (13, 5) after a motor group there.)
        static const char *const hortProps[] = {"", "soundm3", "soundm3 sounds4", "SOUNDS9 soundm2", "soundm5 sounds6",
                                                "sounds7", "soundm7 sounds1", "soundm13 sounds5", "soundm0 sounds0",
                                                "soundm3 sounds6", "soundm2 sounds13"};
        s.hortProps = hortProps[I(0, 10)];
        c.seats.push_back(s);
    }
    c.projectile = P(0.6);
    static const char *const files[] = {"models\\turret.i3d", "Kubel.i3d", "tank", "dir.x\\gun", "Cars\\SdKfz.4ds"};
    c.file = files[I(0, 4)];
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    c.ownerKind = I(0, 2);
    c.sender = P(0.5);
    const int count = P(0.1) ? I(80, 200) : I(5, 60);
    for (int i = 0; i < count; ++i) {
        S_SwOp op;
        const int r = I(0, 99);
        op.kind = r < 6 ? OP_INIT : r < 8 ? OP_STOP : r < 10 ? OP_RESTART : r < 16 ? OP_SET_GUNNER : r < 18 ? OP_SHOW_VIEW
                : r < 38 ? OP_UPDATE : r < 46 ? OP_TICK : r < 62 ? OP_CONTROLS : r < 66 ? OP_FIRE_SHOT : r < 69 ? OP_PLAY_SHOT
                : r < 72 ? OP_RELOAD : r < 75 ? OP_SHOW_RELOAD : r < 77 ? OP_SET_ROUNDS : r < 84 ? OP_GET
                : r < 89 ? OP_SAVE_LOAD : r < 93 ? OP_NOTE : r < 96 ? OP_SEND : OP_MESSAGE;
        op.weapon = I(0, weapons - 1);
        op.a = I(0, 1000);
        op.b = I(-1, 5);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // Init first, mostly.
    if (P(0.9)) {
        for (int i = weapons; i-- > 0;) {
            S_SwOp op;
            op.kind = OP_INIT;
            op.weapon = i;
            op.a = I(0, 1000);
            op.c = 10 + I(0, 990);                      // (with the model and the seat's frame)
            c.ops.insert(c.ops.begin(), op);
        }
    }
    return c;
}

// The weapon model (by the case: the same on both sides).
void BuildModel(const S_SwCase &c)
{
    g_noAnims.clear();
    uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
    uint8_t *model = Child(sector, FRAME_MODEL, "gun", nullptr);
    Mut<const char *>(model, 0x134) = Text(c.file);
    g_model[g_side] = model;
    for (size_t i = 0; i < c.seats.size(); ++i) {
        const S_SwSeat &s = c.seats[i];
        uint8_t *hort = s.hort ? Child(model, FRAME_DUMMY, F("HORT%02d", s.seat), model) : nullptr;
        if (hort)
            AsFrame(hort)->m_userProps = Text(s.hortProps);
        uint8_t *vert = s.vert ? Child(hort && s.vertUnderHort ? hort : model, FRAME_DUMMY, F("VERT%02dx", s.seat), model) : nullptr;
        if (s.aim)
            Child(vert ? vert : model, FRAME_DUMMY, F("AIM%02d", s.seat), model);
        for (int n = 0; n < s.barrels; ++n) {
            uint8_t *barrel = Child(vert ? vert : model, FRAME_VISUAL, F("BARREL%02d_%02d", s.seat, n), model);
            if (n == 0)
                AsFrame(barrel)->m_userProps = Text(s.barrelProps);
            if (!(s.noMuzzle & (1u << n)))
                Child(barrel, FRAME_DUMMY, "SHDUM", model);
            else
                g_noAnims.push_back(F("_f%02d_%02d", s.seat, n));
            if (!(s.noEjector & (1u << n)))
                Child(barrel, FRAME_DUMMY, "CARDUM01", model);
        }
        if (s.view)
            Child(model, FRAME_VISUAL, F("VIEW%02d", s.seat), model);
        g_seatFrame[g_side][i] = Child(hort && s.seatUnderHort ? hort : model, FRAME_DUMMY, F("SEAT%02d", s.seat), model);
    }
    if (c.projectile)
        Child(model, FRAME_VISUAL, "projectile", model);
}

uint8_t *g_owner[2];
void MakeWeapons(const S_SwCase &c)
{
    for (int i = 0; i < g_weaponCount; ++i) {
        uint8_t *b;
        if (g_side) {
            b = Block(0x180 + 16);
            OriginalAt<T_Void>(O_SW_CTOR)(b);
        } else {
            b = Block(sizeof(C_SeatWeapon) + 16);
            ::new (b) C_SeatWeapon();
        }
        g_weapon[g_side][i] = b;
        Name(b, F("weapon %d", i));
        // The messages' sender (inline setters in the original).
        if (c.sender) {
            if (g_side) {
                Mut<uint32_t>(b, 0x16C) = OriginalAt<uint32_t>(O_SW_SEND);
                Mut<uint8_t *>(b, 0x170) = g_owner[1];
            } else
                PortWeapon(i)->SetMessageSender(&SeatWeaponSendMessage, reinterpret_cast<C_Actor *>(g_owner[0]));
        }
    }
}
void AdvanceTime(int t)
{
    if (g_side)
        Mut<uint32_t>(g_origGame, 0x2C) += uint32_t(t);
    else
        reinterpret_cast<C_game *>(g_portGameBuf)->m_time += uint32_t(t);
}

using T_SwInt = void(TC *)(void *, int);
using T_SwChunk = bool(TC *)(void *, void *);
using T_SwSend = bool(__fastcall *)(int, const S_vector4 *, const S_vector4 *, int, int, void *);
using T_SwMake = void *(__fastcall *)(void *, void *, bool);
std::string g_previousSave[2];
std::string RunSwOp(const S_SwOp &op, int index)
{
    const bool o = g_side != 0;
    const int wi = op.weapon < g_weaponCount ? op.weapon : 0;
    uint8_t *b = g_weapon[g_side][wi];
    C_SeatWeapon *p = PortWeapon(wi);
    const S_SwView v = ViewOf(b);
    const int t = FRAME_TIMES[op.a % 12];
    switch (op.kind) {
    case OP_INIT: {
        // (The model and the seat's frame mostly; the case's owner; the weapon's own seat, now and then another.)
        uint8_t *model = op.c % 20 ? g_model[g_side] : nullptr;
        uint8_t *seatFrame = op.c % 19 ? g_seatFrame[g_side][wi] : nullptr;
        const int useSeat = op.d % 13 == 0 ? (op.d / 13) % 4 : g_seatOf[wi];
        bool r;
        if (o)
            r = OriginalAt<bool(TC *)(void *, void *, void *, void *, int)>(O_SW_INIT)(b, g_owner[1], model, seatFrame, useSeat);
        else
            r = p->Init(reinterpret_cast<C_Actor *>(g_owner[0]), AsFrame(model), AsFrame(seatFrame), useSeat);
        return F("Init %d seat %d model %d seat frame %d -> %d", wi, useSeat, int(model != nullptr), int(seatFrame != nullptr), int(r));
    }
    case OP_STOP:
        if (o)
            OriginalAt<T_Void>(O_SW_STOP)(b);
        else
            p->Stop();
        return F("Stop %d", wi);
    case OP_RESTART:
        if (o)
            OriginalAt<T_Void>(O_SW_RESTART)(b);
        else
            p->Restart();
        return F("Restart %d", wi);
    case OP_SET_GUNNER: {
        uint8_t *gunner = op.c % 5 ? HumanActor(op.a) : nullptr;
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_SW_SET_GUNNER)(b, gunner);
        else
            p->SetGunner(reinterpret_cast<C_Human *>(gunner));
        return F("SetGunner %d %s", wi, NameOf(Ptr(gunner)).c_str());
    }
    case OP_SHOW_VIEW: {
        const bool on = op.c & 1;
        if (o)
            OriginalAt<T_SwInt>(O_SW_SHOW_VIEW)(b, on);
        else
            p->ShowView(on);
        return F("ShowView %d %d", wi, int(on));
    }
    case OP_UPDATE:
    case OP_TICK: {
        AdvanceTime(t);
        // (Not with a gunner where the turret's frames are gone: both programs read them unchecked.)
        if (v.gunner && !v.failed && (!v.hort || !v.vert))
            return F("%s %d (no turret)", op.kind == OP_UPDATE ? "Update" : "Tick", wi);
        if (o)
            OriginalAt<T_SwInt>(op.kind == OP_UPDATE ? O_SW_UPDATE : O_SW_TICK)(b, t);
        else if (op.kind == OP_UPDATE)
            p->Update(t);
        else
            p->Tick(t);
        return F("%s %d %d", op.kind == OP_UPDATE ? "Update" : "Tick", wi, t);
    }
    case OP_CONTROLS: {
        // (With a gunner: the item, the barrels and the owner are read unchecked in both programs.)
        if (v.gunner && (!v.item || !v.barrels || !v.owner || v.barrel >= v.barrels))
            return F("ApplyControls %d (no item, barrels)", wi);
        float axes[64] = {}, buttons[64] = {};
        static const float values[] = {0.0f, 1.0f, -1.0f, 0.5f, -0.25f, 0.0f, 0.0f, 2.0f};
        axes[0x34] = values[op.a % 8];
        axes[0x35] = values[op.c % 8];
        axes[6] = op.d % 3 == 0 ? 1.0f : 0.0f;
        axes[13] = op.d % 11 == 0 ? 1.0f : 0.0f;
        buttons[6] = op.d % 4 == 1 ? 1.0f : 0.0f;
        if (o)
            OriginalAt<void(TC *)(void *, float *, float *)>(O_SW_CONTROLS)(b, axes, buttons);
        else
            p->ApplyControls(axes, buttons);
        return F("ApplyControls %d ", wi) + Fl(axes[0x34]) + " " + Fl(axes[0x35]) + " " + Fl(axes[6]) + " " + Fl(axes[13]) +
               " " + Fl(buttons[6]);
    }
    case OP_FIRE_SHOT: {
        if (!v.item || !v.barrels)
            return F("FireShot %d (no item, barrels)", wi);
        alignas(16) S_vector4 pos = {float(op.a % 20) - 10.0f, 2.0f, float(op.c % 30), 1.0f};
        alignas(16) S_vector4 dir = {0.0f, 0.0f, 1.0f, 0.0f};
        if (op.d % 3 == 1)
            dir = {0.6f, 0.0f, -0.8f, 0.0f};
        const int barrel = op.d % v.barrels;
        const bool r = o ? OriginalAt<bool(TC *)(void *, const S_vector4 *, const S_vector4 *, int, int)>(O_SW_FIRE_SHOT)(
                               b, &pos, &dir, barrel, 1000)
                         : p->FireShot(&pos, &dir, barrel, 1000);
        return F("FireShot %d %d -> %d", wi, barrel, int(r));
    }
    case OP_PLAY_SHOT: {
        if (v.gunner && (!v.item || !v.barrels || v.barrel >= v.barrels))
            return F("PlayShot %d (no item, barrels)", wi);
        if (o)
            OriginalAt<T_SwInt>(O_SW_PLAY_SHOT)(b, op.b);
        else
            p->PlayShot(op.b);
        return F("PlayShot %d %d", wi, op.b);
    }
    case OP_RELOAD:
    case OP_SHOW_RELOAD: {
        // (With a gunner: without one the original reads his role through null.)
        if (!v.item || !v.gunner || !v.barrels)
            return F("Reload %d (no item, gunner)", wi);
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(op.kind == OP_RELOAD ? O_SW_RELOAD : O_SW_SHOW_RELOAD)(b, nullptr);
        else if (op.kind == OP_RELOAD)
            p->Reload(nullptr);
        else
            p->ShowReload(nullptr);
        return F("%s %d", op.kind == OP_RELOAD ? "Reload" : "ShowReload", wi);
    }
    case OP_SET_ROUNDS: {
        if (!v.item)
            return F("SetRounds %d (no item)", wi);
        const int rounds = op.a % 40;
        if (o)
            OriginalAt<T_SwInt>(O_SW_SET_ROUNDS)(b, rounds);
        else
            p->SetRounds(rounds);
        return F("SetRounds %d %d", wi, rounds);
    }
    case OP_GET: {
        std::string s = F("Get %d", wi);
        if (v.item) {
            const int r = o ? OriginalAt<int(TC *)(void *)>(O_SW_GET_ROUNDS)(b) : p->GetRounds();
            s += F(" rounds %d", r);
        }
        if (v.failed || v.item) {
            const uint32_t def = o ? Ptr(OriginalAt<void *(TC *)(void *)>(O_SW_ITEM_DEF)(b)) : Ptr(p->GetItemDef());
            const int text = o ? OriginalAt<int(TC *)(void *)>(O_SW_ITEM_TEXT)(b) : p->GetItemText();
            s += F(" def %s text %d", NameOf(def).c_str(), text);
        }
        if (v.failed || v.vert) {
            alignas(16) float aim[4] = {}, origin[4] = {};
            if (o) {
                OriginalAt<void *(TC *)(void *, float *)>(O_SW_AIM)(b, aim);
                OriginalAt<void *(TC *)(void *, float *)>(O_SW_ORIGIN)(b, origin);
            } else {
                const S_vector x = p->GetAim(), y = p->GetOrigin();
                std::memcpy(aim, &x, 12);
                std::memcpy(origin, &y, 12);
            }
            alignas(16) float target[4] = {float(op.a % 40) - 20.0f, float(op.c % 20) - 10.0f, float(op.d % 40) - 20.0f, 1.0f};
            if (op.b == 5) {
                // (Straight along the barrel now and then; or straight above the turret, a little off.)
                for (int k = 0; k < 3; ++k)
                    target[k] = origin[k] + aim[k] * 10.0f;
            } else if (op.b == 4) {
                static const float offs[][2] = {{0.0f, 0.0f}, {1e-5f, 0.0f}, {0.0f, -1e-5f}, {-1e-5f, 0.0f}, {0.0f, 1e-5f}};
                const float *off = offs[op.d % 5];
                target[0] = origin[0] + off[0];
                target[1] = origin[1] + 10.0f;
                target[2] = origin[2] + off[1];
            }
            const bool can = o ? OriginalAt<bool(TC *)(void *, float *)>(O_SW_CAN_AIM)(b, target) : p->CanAim(target);
            s += " aim " + VectorText(Ptr(aim), 3) + " origin " + VectorText(Ptr(origin), 3) + F(" can aim %d", int(can));
        }
        int left = 7, total = 8, text = 9;
        const bool c8 = o ? OriginalAt<bool(TC *)(void *, int *, int *, int *)>(O_SW_C8)(b, &left, &total, &text)
                          : p->UnknownC8(&left, &total, &text);
        s += F(" c8 %d %d %d %d", int(c8), left, total, text);
        if (!o) {
            float up, down, l, r;
            p->GetLimits(&up, &down, &l, &r);
            const float lim[4] = {up, down, l, r};
            s += " limits " + VectorText(Ptr(lim), 4) + F(" refill %u %d", p->GetRefillTime(), p->GetRefillBelow());
        } else
            s += " limits " + VectorText(Ptr(b + 0x128), 4) + F(" refill %u %d", At<uint32_t>(b, 0x174), At<int>(b, 0x178));
        // (The refill's setters: inline in the original.)
        if (op.c % 7 == 0) {
            if (o) {
                Mut<uint32_t>(b, 0x174) = uint32_t(op.a);
                Mut<int>(b, 0x178) = op.d;
            } else {
                p->SetRefillTime(uint32_t(op.a));
                p->SetRefillBelow(op.d);
            }
        }
        return s;
    }
    case OP_SAVE_LOAD: {
        if (!v.item)
            return F("SaveLoad %d (no item)", wi);
        const std::string file = F("sw_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_SwChunk>(O_SW_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        // (An entry after it: one the save left open would take it in.)
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        std::string hex;
        for (unsigned char ch : bytes)
            hex += F("%02x", ch);
        // (Loaded now and then into this weapon or another, with its turret: this file, the case's previous one, or a
        // made one (the entries in either order, one of another id, the barrel within the barrels).)
        const int load = op.c % 4;
        bool rl = false;
        uint8_t *into = g_weapon[g_side][(wi + (op.d & 1)) % g_weaponCount];
        const S_SwView iv = ViewOf(into);
        const std::string previous = g_previousSave[g_side];
        g_previousSave[g_side] = file;
        if (load && iv.hort && iv.vert && iv.item) {
            std::string name = load == 2 && !previous.empty() ? previous : file;
            if (load == 3) {
                S_Writer w;
                w.Begin(0x2000);
                const bool roundsFirst = op.a & 1;
                static const float tilts[] = {0.0f, 0.5f, -0.25f, 1.5f};
                static const float turns[] = {0.0f, 1.0f, -2.0f, 3.1f};
                auto state = [&] {
                    w.Begin(2000);
                    const float tilt = tilts[op.a % 4], turn = turns[(op.a / 4) % 4];
                    const uint32_t flags = uint32_t(op.d % 3 == 0 ? 0x420 : op.d % 3 == 1 ? 0x5 : 0);
                    const int barrel = iv.barrels ? op.d % iv.barrels : 0, delay = op.c % 700, left = op.a % 500;
                    const int total = 900;
                    w.Raw(&tilt, 4);
                    w.Raw(&turn, 4);
                    w.Raw(&flags, 4);
                    w.Raw(&barrel, 4);
                    w.Raw(&delay, 4);
                    w.Raw(&left, 4);
                    w.Raw(&total, 4);
                    w.End();
                };
                if (!roundsFirst)
                    state();
                if (op.d % 5 == 0)
                    w.EntryOf(0x1234, 5);
                w.EntryOf(0x7D1, float(op.c % 25) + 0.5f);
                if (roundsFirst)
                    state();
                w.End();
                name = F("sw_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_SwChunk>(O_SW_LOAD)(into, &chunk) : reinterpret_cast<C_SeatWeapon *>(into)->LoadGame(chunk);
            chunk.Close();
        }
        return F("SaveGame %d -> %d LoadGame %d into %s -> %d bytes ", wi, int(rs), load, NameOf(Ptr(into)).c_str(), int(rl)) + hex;
    }
    case OP_NOTE: {
        // A barrel's note (its callback, as the engine calls it): "S..." throws a shell.
        I3D_frame *frame = nullptr;
        int n = 0;
        for (const S_FakeFrame &f : W().frames)
            if (AsFrame(f.bytes)->m_callback && n++ == op.a % 4)
                frame = AsFrame(f.bytes);
        if (!frame)
            return "Note (no barrel)";
        // (Not when the weapon it names has no shells model: the original reads it through null.)
        const S_FrameData *data = frame->m_data;
        const uint8_t *named = data ? reinterpret_cast<const uint8_t *>(data->unknown1C) : nullptr;
        if (named && !ViewOf(named).shells)
            return "Note (no shells)";
        static const char *const notes[] = {"S", "Shell", "X", "", "s"};
        const char *note = op.c % 9 == 0 ? nullptr : notes[op.c % 5];
        const int r = frame->m_callback(frame, I3DCALLBACK_ANIM_NOTE, reinterpret_cast<uintptr_t>(note), uintptr_t(op.d));
        return F("Note %s %s -> %d", NameOf(Ptr(frame)).c_str(), note ? note : "(null)", r);
    }
    case OP_SEND: {
        alignas(16) S_vector4 pos = {1.0f, 2.0f, float(op.a % 9), 1.0f}, dir = {0.0f, 0.6f, 0.8f, 0.25f};
        const int kind = op.c % 5;
        uint8_t *car = AnyActor(op.d);
        const bool r = o ? OriginalAt<T_SwSend>(O_SW_SEND)(kind, &pos, &dir, op.b & 3, op.a % 6, car)
                         : SeatWeaponSendMessage(kind, &pos, &dir, op.b & 3, op.a % 6, reinterpret_cast<C_Actor *>(car));
        return F("SeatWeaponSendMessage %d %s -> %d", kind, NameOf(Ptr(car)).c_str(), int(r));
    }
    default: {
        // A message made by the factory (or its count written), its Write, then its Read.
        uint8_t *stream = W().bitstream;
        const bool write = op.c % 6 == 0;
        g_bits[g_side].clear();
        g_bits[g_side].push_back(uint32_t(op.a % 8));
        uint8_t *car = g_owner[g_side];
        void *made = o ? OriginalAt<T_SwMake>(O_SW_MAKE)(car, stream, write)
                       : MakeSeatWeaponMessage(car, reinterpret_cast<C_BitStream *>(stream), write);
        if (!made)
            return F("MakeSeatWeaponMessage %d -> none", int(write));
        uint8_t *m = static_cast<uint8_t *>(made);
        Name(m, "made message");
        int type;
        if (o)
            type = At<int>(m, 0x1C);
        else
            type = reinterpret_cast<C_NetObjectMessage *>(m)->type;
        // Its fields as the sender's, then written; then read back (the bits answered in turn).
        static const uint32_t words[] = {5, 3, 0x3F800000u, 0xBF000000u, 0x41200000u, 2, 7, 1023, 0xFFFF, 0x80};
        if (o) {
            switch (type) {
            case SEAT_WEAPON_MSG_SHOT:
                Mut<S_vector4>(m, 0x30) = {0.6f, 0.0f, 0.8f, 0.0f};
                Mut<S_vector4>(m, 0x40) = {10.0f, -2.0f, 3.5f, 1.0f};
                Mut<int>(m, 0x50) = op.d % 8;
                Mut<int>(m, 0x54) = op.a % 4;
                break;
            case SEAT_WEAPON_MSG_RELOAD: Mut<int>(m, 0x30) = op.d % 8; break;
            case SEAT_WEAPON_MSG_ROUNDS:
                Mut<int>(m, 0x30) = op.a;
                Mut<int>(m, 0x34) = op.d % 8;
                break;
            default:
                Mut<int>(m, 0x30) = op.a % 256;
                Mut<int>(m, 0x34) = op.d * 70 - 30000;
                break;
            }
            reinterpret_cast<void(TC *)(void *, void *)>((*reinterpret_cast<void ***>(m))[8])(m, stream);
            for (int k = 0; k < 10; ++k)
                g_bits[1].push_back(words[(k + op.d) % 10]);
            reinterpret_cast<void(TC *)(void *, void *)>((*reinterpret_cast<void ***>(m))[9])(m, stream);
        } else {
            C_NetObjectMessage *x = reinterpret_cast<C_NetObjectMessage *>(m);
            switch (type) {
            case SEAT_WEAPON_MSG_SHOT: {
                auto *s = static_cast<C_SeatWeaponShotMessage *>(x);
                s->dir = {0.6f, 0.0f, 0.8f, 0.0f};
                s->pos = {10.0f, -2.0f, 3.5f, 1.0f};
                s->seat = op.d % 8;
                s->barrel = op.a % 4;
                break;
            }
            case SEAT_WEAPON_MSG_RELOAD: static_cast<C_SeatWeaponReloadMessage *>(x)->seat = op.d % 8; break;
            case SEAT_WEAPON_MSG_ROUNDS:
                static_cast<C_SeatWeaponRoundsMessage *>(x)->rounds = op.a;
                static_cast<C_SeatWeaponRoundsMessage *>(x)->seat = op.d % 8;
                break;
            default:
                static_cast<C_SeatWeaponAmmoMessage *>(x)->typeId = op.a % 256;
                static_cast<C_SeatWeaponAmmoMessage *>(x)->uid = op.d * 70 - 30000;
                break;
            }
            x->Write(reinterpret_cast<C_BitStream *>(stream));
            for (int k = 0; k < 10; ++k)
                g_bits[0].push_back(words[(k + op.d) % 10]);
            x->Read(reinterpret_cast<C_BitStream *>(stream));
        }
        // The fields read.
        std::string s = F("message type %d", type);
        if (o) {
            if (type == SEAT_WEAPON_MSG_SHOT)
                s += " dir " + V4(FP(m, 0x30)) + " pos " + V4(FP(m, 0x40)) + F(" seat %d barrel %d", At<int>(m, 0x50), At<int>(m, 0x54));
            else
                s += F(" %d %d", At<int>(m, 0x30), type == SEAT_WEAPON_MSG_RELOAD ? 0 : At<int>(m, 0x34));
        } else {
            C_NetObjectMessage *x = reinterpret_cast<C_NetObjectMessage *>(m);
            if (type == SEAT_WEAPON_MSG_SHOT) {
                auto *q = static_cast<C_SeatWeaponShotMessage *>(x);
                s += " dir " + V4(&q->dir.x) + " pos " + V4(&q->pos.x) + F(" seat %d barrel %d", q->seat, q->barrel);
            } else if (type == SEAT_WEAPON_MSG_RELOAD)
                s += F(" %d %d", static_cast<C_SeatWeaponReloadMessage *>(x)->seat, 0);
            else if (type == SEAT_WEAPON_MSG_ROUNDS)
                s += F(" %d %d", static_cast<C_SeatWeaponRoundsMessage *>(x)->rounds, static_cast<C_SeatWeaponRoundsMessage *>(x)->seat);
            else
                s += F(" %d %d", static_cast<C_SeatWeaponAmmoMessage *>(x)->typeId, static_cast<C_SeatWeaponAmmoMessage *>(x)->uid);
        }
        return s;
    }
    }
}

void ClearSwCaches()
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
            std::printf("sw_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_swShow;
void RunSwCase(const S_SwCase &c)
{
    g_caseStart = GetTickCount();
    ++g_swCases;
    g_holdFrees = true;
    g_current = F("case %d", g_swCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_weaponCount = int(c.seats.size());
    for (int i = 0; i < g_weaponCount; ++i)
        g_seatOf[i] = c.seats[size_t(i)].seat;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_world[side].frames.reserve(65536);
        g_texts[side].clear();
        g_items[side].clear();
        g_groups[side].clear();
        g_bits[side].clear();
        g_previousSave[side].clear();
        g_localPlayers[side].clear();
        BuildWorld(side, worldSeed);
        g_sndDriver[side] = Block(0x40);
        *reinterpret_cast<void ***>(g_sndDriver[side]) = EngineVtable("ISND_driver");
        Name(g_sndDriver[side], "sound driver");
        g_physicsModule[side] = Block(0x80);
        *reinterpret_cast<void ***>(g_physicsModule[side]) = g_physicsVtable[side];
        Name(g_physicsModule[side], "physics module");
        g_netObject[side] = Block(0x100);
        Name(g_netObject[side], "net object");
        g_replication[side] = Block(std::max(sizeof(C_NetReplication), size_t(0x100)) + 0x40);
        Name(g_replication[side], "net replication");
        g_sender[side] = Block(0x40);
        Name(g_sender[side], "sender");
        if (side) {
            *OriginalAt<uint8_t **>(O_NET_REPLICATION) = g_replication[1];
            Mut<int>(g_replication[1], 0xDC) = g_localId;
        } else
            g_netReplication = reinterpret_cast<C_NetReplication *>(g_replication[0]);
        // The owner: a car (its mass and lever origin), a static weapon or another actor.
        uint8_t *owner = c.ownerKind == 0 ? ActorOfType(ACTOR_CAR, 0) : c.ownerKind == 1 ? ActorOfType(ACTOR_STATIC_WEAPON, 0) : nullptr;
        if (!owner)
            owner = AnyActor(0);
        g_owner[side] = owner;
        // (Only a car's: another actor's block is smaller.)
        const bool isCar = owner && (side ? At<int>(owner, 0x18) : int(reinterpret_cast<C_Actor *>(owner)->GetType())) == ACTOR_CAR;
        if (isCar) {
            const float mass = 1500.0f;
            const S_vector4 lever = {0.5f, 1.0f, -2.0f, 1.0f};
            if (side) {
                Mut<float>(owner, 0x90 + 0x918) = mass;
                Mut<S_vector4>(owner, 0x90 + 0x1B0) = lever;
            } else {
                C_Vehicle *vehicle = reinterpret_cast<C_Car *>(owner)->GetVehicle();
                vehicle->m_currentMass = mass;
                vehicle->m_unknown1B0 = lever;
            }
        }
        BuildModel(c);
        SetRole(c.role);
        if (side) {
            Mut<uint32_t>(g_origGame, 0x28) = 5000;
            Mut<uint32_t>(g_origGame, 0x2C) = 5000;
        } else {
            reinterpret_cast<C_game *>(g_portGameBuf)->m_missionTime = 5000;
            reinterpret_cast<C_game *>(g_portGameBuf)->m_time = 5000;
        }
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        MakeWeapons(c);
        for (int i = 0; i < g_weaponCount; ++i)
            for (const std::string &s : WeaponState(i))
                out.push_back(s);
        static const int trace = std::getenv("HD2_SW_TRACE") ? std::atoi(std::getenv("HD2_SW_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_swCases)
                std::printf("trace %s op %d kind %s weapon %d a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i),
                            OP_KIND_NAMES[c.ops[i].kind], c.ops[i].weapon, c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunSwOp(c.ops[i], int(i)));
            if (trace == g_swCases) {
                std::printf("trace   -> %s\n", out.back().substr(0, 160).c_str());
                std::fflush(stdout);
            }
            for (const std::string &s : g_log[side])
                out.push_back("  call " + MaskSoundW(s));
            g_log[side].clear();
            for (int w = 0; w < g_weaponCount; ++w)
                for (const std::string &s : WeaponState(w))
                    out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_weaponCount; ++i) {
            uint8_t *b = g_weapon[side][i];
            if (side) {
                OriginalAt<T_Void>(O_SW_STOP)(b);
                OriginalAt<T_Void>(O_SW_DTOR)(b);
            } else {
                PortWeapon(i)->Stop();
                PortWeapon(i)->~C_SeatWeapon();
            }
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + MaskSoundW(s));
        for (const std::string &s : SwFrameStates())
            out.push_back(s);
        ClearSwCaches();
        for (int i = 0; i < MAX_WEAPONS; ++i)
            g_weapon[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_SW_ONLY") ? std::atoi(std::getenv("HD2_SW_ONLY")) : 0;
    if (g_swShow && (!only || only == g_swCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_swShow == 2)
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
void SetUpSw()
{
    SetUpCmd();
    SetUpSwAnswers();
    // The virtual calls the weapon makes on the test's objects (before the world's vtables are made).
    g_virtuals.push_back({"C_Actor::GetSeatOccupant", 0x78, PortSlot(&C_Actor::GetSeatOccupant), "i", 'p'});
    g_virtuals.push_back({"C_Actor::GetSeatCount", 0x88, PortSlot(&C_Actor::GetSeatCount), "", 'i'});
    g_virtuals.push_back({"C_WeaponItem::Use", 0x08, PortSlot(&C_WeaponItem::Use), "f", 'v'});
    g_virtuals.push_back({"C_WeaponItem::GetAmount", 0x0C, PortSlot(&C_WeaponItem::GetAmount), "", 'f'});
    g_virtuals.push_back({"C_WeaponItem::Reload", 0x18, PortSlot(&C_WeaponItem::Reload), "pipb", 'b'});
    g_virtuals.push_back({"C_WeaponComp::GetAimedDispersion", 0x10, PortSlot(&C_WeaponComp::GetAimedDispersion), "", 'f'});
    g_virtuals.push_back({"C_WeaponComp::GetReloadSound", 0x1C, PortSlot(&C_WeaponComp::GetReloadSound), "", 'i'});
    g_virtuals.push_back({"C_WeaponComp::GetReloadTimeMs", 0x20, PortSlot(&C_WeaponComp::GetReloadTimeMs), "", 'i'});
    g_virtuals.push_back({"C_PhysicsModuleBase::CreateObject", 0x40, PortSlot(&C_PhysicsModuleBase::CreateObject), "p", 'p'});
    g_virtuals.push_back({"C_PhysicsModuleBase::ApplyImpulse", 0x48, PortSlot(&C_PhysicsModuleBase::ApplyImpulse), "pvvf", 'v'});
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_itemVtable[side] = GameVtable("C_WeaponItem", VirtualSlots("C_WeaponItem::", side != 0));
        g_compVtable[side] = GameVtable("C_WeaponComp", VirtualSlots("C_WeaponComp::", side != 0));
        g_physicsVtable[side] = GameVtable("C_PhysicsModuleBase", VirtualSlots("C_PhysicsModuleBase::", side != 0));
    }
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
    // The names both sides' functions go by.
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        Name(side ? OriginalAt<void *>(O_SW_SEND) : reinterpret_cast<void *>(&SeatWeaponSendMessage), "send message");
        Name(side ? OriginalAt<void *>(O_SW_NOTE) : reinterpret_cast<void *>(&C_SeatWeapon::OnBarrelNote), "barrel note");
    }
}

}  // namespace H

#ifndef SW_DIFF_NO_MAIN
int main()
{
    using namespace H;
    std::puts("sw_diff: main");
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
        std::printf("sw_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_SW_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_SW_SHOW"))
        g_swShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_SW_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_SW_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpSw();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunSwCase(RandomSwCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("sw_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_swCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
#endif
