// Static weapon part 1: the guns on mounts (game/static_weapon.cpp, the original's 0x00602a10..0x00606250) against the
// original executable's. sw_diff.cpp is included (SW_DIFF_NO_MAIN: its world, frames' answers, items, sound groups and
// weapon dumps, over cmd_diff.cpp's); the weapon's calls go through st_calls.s / st_calls.inc (gen_cmd_calls.py st). The
// seat weapon inside is each side's own (game/seat_weapon.cpp linked; the original's in its block), the caches real on
// both sides; a script's OnUse is logged on both sides (the port's patched at run time, the original's redirected); so
// are the texts.
//
// Each case builds the same world on each side (cmd_diff's, plus the weapon's model under the primary sector: its seats
// (user properties, cameras under them or their parents), seat 0's HORT / VERT / BARREL parts, visuals with boxes, some
// flagged or empty; the destroyed model "C<file>" opened by the cache, or missing), makes a static weapon on each side
// (the original's by its constructor 0x00602ec0 in a block of 0x300, the port's by its own), sets its frame, its store
// (items in slots) and its net object, and runs the same random sequence of operations in the case's role:
// StartMission, StopMission, RestartMission, SetState, updates and ticks (the net object's messages popped: shots,
// reloads, rounds, ammunition), OnMessage, WriteState / ReadState, saves and loads, the seats' virtuals, the seat weapon's
// virtuals, Hit, GetBoxCorners, GetExitPosUnused, the creator. After each operation the answer, its calls, the weapon's
// fields (its box, seats, state, the seat weapon's every field), the net object's settings are compared; at the end the
// frames.
// MinGW; build_st.bat; runs st_diff.exe ("st_diff: N checks, 0 failures"). Env: HD2_ST_CASES (default 1,500;
// HD2_ST_QUICK: 300), HD2_ST_SEED, HD2_ST_SHOW (1: the port's lines; 2: both), HD2_ST_ONLY, HD2_ST_TRACE.
#define HARNESS_NAME "st_diff"
#define CMD_CALLS_INC "st_calls.inc"
#define SW_DIFF_NO_MAIN
#include "sw_diff.cpp"

#define private public
#define protected public
#include "game/static_weapon.h"
#include "game/game_texts.h"
#include "script/script.h"
#undef protected
#undef private

// ---- the port's side of what the weapon calls and the test provides ----
// The net object's and the replication's setters (net/net_object.cpp: not linked): the original's fields.
void C_NetObject::SetMessageFactory(NetMessageFactory factory, void *context)
{
    *reinterpret_cast<NetMessageFactory *>(reinterpret_cast<uint8_t *>(this) + 0x64) = factory;
    *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(this) + 0x60) = context;
}
void C_NetObject::SetSendMask(int mask) { *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(this) + 0x38) = mask; }
void C_NetObject::SetLive(int value) { *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(this) + 0x3C) = value; }
void C_NetObject::SetForwarding(bool value) { *(reinterpret_cast<uint8_t *>(this) + 0x40) = value; }
void C_NetReplication::SetWriteEpoch(int value) { *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(this) + 0xC4) = value; }
void C_NetReplication::SetReadEpoch(int value) { *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(this) + 0xC8) = value; }
// The base's virtuals the weapon's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::CopyFrom(const C_Actor *) {}
// LS3DF.dll's matrix math (the original's calls go there too).
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
S_vector &__stdcall S_vector::operator*=(const S_matrix &m)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_vector *, const S_matrix &)>("??XS_vector@@QAGAAU0@ABUS_matrix@@@Z");
    f(this, m);
    return *this;
}
S_matrix __stdcall S_matrix::operator*(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, S_matrix *, const S_matrix &)>("??DS_matrix@@QBG?AU0@ABU0@@Z");
    S_matrix r;
    f(this, &r, m);
    return r;
}
namespace H {
// The texts (the weapon's name): logged, "T<id>" on both sides.
std::string TextOf(int id) { return F("T%X", id); }
void *__attribute__((thiscall)) O_TextsGet(void *, void *ret, int id)
{
    g_log[g_side].push_back(F("texts %d", id));
    *reinterpret_cast<uint32_t *>(ret) = 0;
    OriginalAt<T_Fstr>(0x00405100)(ret, "%s", TextOf(id).c_str());
    return ret;
}
// A script's OnUse (the port's own patched to this at the start; the original's redirected): logged.
void __attribute__((thiscall)) A_OnUse(void *script, int how)
{
    g_log[g_side].push_back(F("C_Script::OnUse(%s, %d)", NameOf(reinterpret_cast<uint32_t>(script)).c_str(), how));
}
// A port function's entry patched to jump to `to` (its code made writable).
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
}  // namespace H
C_str C_GameTexts::Get(int id) const
{
    H::g_log[H::g_side].push_back(H::F("texts %d", id));
    return C_str(H::TextOf(id).c_str());
}

namespace H {

// ---- the store: a container of slots (each a list of items), in each side's layout ----
struct S_Store {
    uint8_t *container = nullptr;
    uint8_t *slots = nullptr;                           // (the port's std::vector array, the original's 16-byte vectors)
    int used = 0;
};
S_Store g_store[2];
void **g_containerVtable[2];
// The case's store (by its numbers: the same on both sides).
void BuildStore(int slots, uint32_t seed)
{
    S_Store &s = g_store[g_side];
    s.container = Block(0x40);
    *reinterpret_cast<void ***>(s.container) = g_containerVtable[g_side];
    Name(s.container, "store");
    s.used = slots;
    std::vector<std::vector<uint8_t *>> lists(static_cast<size_t>(slots));
    for (int i = 0; i < slots; ++i)
        for (int k = int((seed >> (i * 2)) % 3); k > 0; --k) {
            uint8_t *item = MakeItem(4 + (i + k) % 5);
            const int uid = 200 + i * 10 + k;
            if (g_side)
                Mut<int>(item, 0x10) = uid;
            else
                reinterpret_cast<C_Item *>(item)->m_uid = uid;
            lists[size_t(i)].push_back(item);
        }
    if (g_side) {
        // (The original's: an array of pointers to the slots' vectors, each its allocator word, first, last, end.)
        s.slots = Block(4 * size_t(slots + 1));
        for (int i = 0; i < slots; ++i) {
            const auto &l = lists[size_t(i)];
            uint8_t *vec = Block(16);
            Mut<uint8_t *>(s.slots, uint32_t(4 * i)) = vec;
            if (l.empty())
                continue;
            uint8_t **items = reinterpret_cast<uint8_t **>(Block(4 * (l.size() + 1)));
            std::copy(l.begin(), l.end(), items);
            Mut<uint8_t **>(vec, 4) = items;
            Mut<uint8_t **>(vec, 8) = items + l.size();
            Mut<uint8_t **>(vec, 12) = items + l.size();
        }
    } else {
        s.slots = Block(sizeof(std::vector<C_Item *>) * size_t(slots + 1));
        auto *v = reinterpret_cast<std::vector<C_Item *> *>(s.slots);
        for (int i = 0; i < slots; ++i) {
            new (&v[i]) std::vector<C_Item *>();
            for (uint8_t *x : lists[size_t(i)])
                v[i].push_back(reinterpret_cast<C_Item *>(x));
        }
    }
}

// ---- the messages the net object hands out (PopMessage): each side's, made by its own factory ----
std::deque<uint8_t *> g_popQueue[2];
using T_SwMakeF = void *(__fastcall *)(void *, void *, bool);
uint8_t *MakeMessage(int type, int a, int b, int c)
{
    g_bits[g_side].clear();
    g_bits[g_side].push_back(uint32_t(type));
    uint8_t *m = g_side ? static_cast<uint8_t *>(OriginalAt<T_SwMakeF>(0x004c14b0)(nullptr, W().bitstream, false))
                        : reinterpret_cast<uint8_t *>(MakeSeatWeaponMessage(nullptr, reinterpret_cast<C_BitStream *>(W().bitstream), false));
    g_bits[g_side].clear();
    if (!m)
        return nullptr;
    // (Unnamed: a freed message's address comes back as a new one; the contents are logged when queued.) From a client.
    g_names[g_side].erase(Ptr(m));
    if (g_side)
        m[0x2C] = 1;
    else
        reinterpret_cast<C_NetObjectMessage *>(m)->fromClient = true;
    if (g_side) {
        switch (type) {
        case SEAT_WEAPON_MSG_SHOT:
            Mut<S_vector4>(m, 0x30) = {0.0f, 0.6f, 0.8f, 0.0f};
            Mut<S_vector4>(m, 0x40) = {float(a % 9), 1.0f, -2.0f, 1.0f};
            Mut<int>(m, 0x50) = b % 3;
            Mut<int>(m, 0x54) = c % 4;
            break;
        case SEAT_WEAPON_MSG_RELOAD: Mut<int>(m, 0x30) = b % 3; break;
        case SEAT_WEAPON_MSG_ROUNDS: Mut<int>(m, 0x30) = a % 50; break;
        default:
            Mut<int>(m, 0x30) = 4 + a % 5;
            Mut<int>(m, 0x34) = 300 + c;
            break;
        }
    } else {
        C_NetObjectMessage *x = reinterpret_cast<C_NetObjectMessage *>(m);
        switch (type) {
        case SEAT_WEAPON_MSG_SHOT: {
            auto *s = static_cast<C_SeatWeaponShotMessage *>(x);
            s->dir = {0.0f, 0.6f, 0.8f, 0.0f};
            s->pos = {float(a % 9), 1.0f, -2.0f, 1.0f};
            s->seat = b % 3;
            s->barrel = c % 4;
            break;
        }
        case SEAT_WEAPON_MSG_RELOAD: static_cast<C_SeatWeaponReloadMessage *>(x)->seat = b % 3; break;
        case SEAT_WEAPON_MSG_ROUNDS: static_cast<C_SeatWeaponRoundsMessage *>(x)->rounds = a % 50; break;
        default:
            static_cast<C_SeatWeaponAmmoMessage *>(x)->typeId = 4 + a % 5;
            static_cast<C_SeatWeaponAmmoMessage *>(x)->uid = 300 + c;
            break;
        }
    }
    return m;
}

void SetUpStaticAnswers()
{
    // The base's SetFrame: it took the frame (the test sets it).
    g_answers["C_Actor::SetFrame"] = [](const std::vector<uint32_t> &) { return 1u; };
    // The store.
    g_answers["C_ItemContainer::CountItems"] = [](const std::vector<uint32_t> &) { return NextFake() % 4; };
    g_answers["C_ItemContainer::GetUsedSlotCount"] = [](const std::vector<uint32_t> &) { return uint32_t(g_store[g_side].used); };
    g_answers["C_ItemContainer::GetSlots"] = [](const std::vector<uint32_t> &) { return Ptr(g_store[g_side].slots); };
    g_answers["C_ItemContainer::AddItem"] = [](const std::vector<uint32_t> &) { return 0u; };
    g_answers["C_ItemsModule::CreateItem"] = [](const std::vector<uint32_t> &a) {
        uint8_t *item = MakeItem(int(a[1]));
        if (g_side)
            Mut<int>(item, 0x10) = int(a[4]);
        else
            reinterpret_cast<C_Item *>(item)->m_uid = int(a[4]);
        return Ptr(item);
    };
    g_answers["C_ItemDef::GetAmmoItemId"] = [](const std::vector<uint32_t> &) { return 40u + NextFake() % 3; };
    g_answers["SaveActorItems"] = [](const std::vector<uint32_t> &) { return 1u; };
    g_answers["LoadActorItems"] = [](const std::vector<uint32_t> &) { return 1u; };
    // The actor messages: the world's bitstream.
    g_answers["CreateActorMessage"] = [](const std::vector<uint32_t> &) { return Ptr(W().bitstream); };
    // The net object's messages, popped in turn; queued again ones keep a reference (not deleted).
    g_answers["C_NetObject::PopMessage"] = [](const std::vector<uint32_t> &) {
        auto &q = g_popQueue[g_side];
        if (q.empty())
            return 0u;
        uint8_t *m = q.front();
        q.pop_front();
        return Ptr(m);
    };
    auto queue = g_answers["C_NetObject::QueueMessage"];
    g_answers["C_NetObject::QueueMessage"] = [queue](const std::vector<uint32_t> &a) {
        if (uint8_t *m = reinterpret_cast<uint8_t *>(a[1]))
            ++Mut<int>(m, 0x24);
        return queue(a);
    };
    g_answers["C_NetReplication::FindObject"] = [](const std::vector<uint32_t> &) { return Ptr(g_netObject[g_side]); };
    // The walking point at the weapon (none now and then).
    g_answers["C_CheckpointNetwork::FindActorPoint"] = [](const std::vector<uint32_t> &) {
        return NextFake() % 3 ? NextFake() % CHECKPOINT_NAME_COUNT : uint32_t(CHECKPOINT_NONE);
    };
    // A walking point (FindExit): the world's, point 0 at the origin (no way out: the fallback behind the weapon).
    g_answers["C_CheckpointNetwork::Get"] = [](const std::vector<uint32_t> &a) {
        static uint8_t origin[2][0x40];
        if (a[1] == 0)
            return Ptr(origin[g_side]);
        uint8_t *points = W().points[a[0] == reinterpret_cast<uint32_t>(W().roads)];
        return a[1] < uint32_t(CHECKPOINT_NAME_COUNT) ? Ptr(points + 0x24 * a[1]) : 0u;
    };
    // A hit's zone: a miss (0xFF) now and then.
    g_answers["C_HitZones::GetHitZone"] = [](const std::vector<uint32_t> &) { return NextFake() % 3 ? NextFake() % 6 : 0xFFu; };
    // The destroyed model ("C<file>"): copies of the weapon's visuals (other turns and meshes), none for "Cbad".
    auto open = g_answers["I3D_model::Open"];
    g_answers["I3D_model::Open"] = [open](const std::vector<uint32_t> &a) {
        const std::string file = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        uint8_t *m = reinterpret_cast<uint8_t *>(a[0]);
        // ("bad..." does not open again (its "Cbad..." does); "none..." has no destroyed model.)
        const size_t slash = file.find_last_of("\\/");
        const std::string base = slash == std::string::npos ? file : file.substr(slash + 1);
        if (base.compare(0, 3, "bad") == 0 || base.compare(0, 5, "Cnone") == 0)
            return uint32_t(I3DERR_NOFILE);
        if (file.find("GunShells") != std::string::npos)
            return open(a);
        static const char *const parts[] = {"BARREL00_00", "BARREL00_01", "base", "shield", "VIEW00"};
        int n = 0;
        for (const char *part : parts) {
            uint8_t *c = Child(m, FRAME_VISUAL, part, m);
            AsFrame(c)->m_rot = S_quat(0.5f, 0.5f, 0.5f, float(n++) * 0.25f - 0.5f);
            AsFrame(c)->m_rot.Normalize();
            uint8_t *mesh = Block(0x40);
            Name(mesh, F("mesh %s of %s", part, file.c_str()));
            Mut<uint8_t *>(c, 0x1E0) = mesh;
        }
        Mut<const char *>(m, 0x134) = Text(file);
        return 0u;
    };
    // The visuals' walk (EnumFrames: the callback for each of the mask's types; non-zero stops).
    g_answers["I3D_frame::EnumFrames"] = [](const std::vector<uint32_t> &a) {
        using T_Enum = uint32_t(__stdcall *)(I3D_frame *, void *);
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
}

// ---- the weapons ----
constexpr uint32_t O_ST_CTOR = 0x00602ec0, O_ST_DTOR = 0x00602fc0, O_ST_SET_STATE = 0x006059b0, O_ST_EXIT_POS = 0x006056d0,
                   O_ST_UPDATE = 0x006034d0, O_ST_TICK = 0x00603070, O_ST_ON_MESSAGE = 0x00605030,
                   O_ST_SET_FRAME = 0x00603040, O_ST_START = 0x006036d0, O_ST_STOP = 0x00604120,
                   O_ST_RESTART = 0x006050b0, O_ST_SAVE = 0x00605120, O_ST_LOAD = 0x006053c0,
                   O_ST_WRITE_STATE = 0x00603520, O_ST_READ_STATE = 0x00603560, O_ST_IS_BLOCKED = 0x00604a90,
                   O_ST_SET_BLOCKED = 0x00604ad0, O_ST_SET_OCCUPANT = 0x006041a0, O_ST_GET_OCCUPANT = 0x00604590,
                   O_ST_FIND_SEAT = 0x006045e0, O_ST_LEAVE = 0x00604630, O_ST_EXIT = 0x006046d0,
                   O_ST_SEAT_FLAGS = 0x00604a10, O_ST_SEAT_CAMERA = 0x00604b70, O_ST_SEAT_TEXT = 0x00604b10,
                   O_ST_ROUNDS = 0x00604d30, O_ST_AIM = 0x00604d70, O_ST_ORIGIN = 0x00604d40, O_ST_AC = 0x00604d60,
                   O_ST_WEAPON_TEXT = 0x00604d90, O_ST_WEAPON_DEF = 0x00604da0, O_ST_LIMITS = 0x00604dd0,
                   O_ST_CAN_AIM = 0x006055a0, O_ST_CONTROLS = 0x00604cf0, O_ST_SHOW_VIEW = 0x00604db0, O_ST_C8 = 0x00604e10,
                   O_ST_FPV_ONLY = 0x00604e30, O_ST_HIT = 0x006057b0, O_ST_CORNERS = 0x00605e50,
                   O_ST_HIT_POINTS = 0x00602f60, O_ST_HAS_WEAPON = 0x00604d20, O_ST_SEAT_COUNT = 0x00602f70;
constexpr uint32_t O_ST_CREATOR = 0x008aca20, O_ST_CREATE = 0x00602a70, O_ST_DESTROY = 0x00602ae0;
uint8_t *g_static[2];
C_StaticWeapon *PortStatic() { return reinterpret_cast<C_StaticWeapon *>(g_static[0]); }
// The seat weapon inside (sw_diff's dumps read it as weapon 0).
uint8_t *InnerWeapon() { return g_side ? g_static[1] + 0x160 : reinterpret_cast<uint8_t *>(&PortStatic()->m_weapon); }
struct S_StView {
    int seats = 0, state = 0;
    bool started = false;
    std::vector<uint32_t> occupants;
};
S_StView StView()
{
    S_StView v;
    const uint8_t *b = g_static[g_side];
    if (g_side) {
        const uint32_t first = At<uint32_t>(b, 0x2E4), last = At<uint32_t>(b, 0x2E8);
        for (uint32_t q = first; q && q < last; q += 0x0C)
            v.occupants.push_back(At<uint32_t>(reinterpret_cast<void *>(q), 0));
        v.state = At<int>(b, 0x2F0);
        v.started = b[0x159] != 0;
    } else {
        const C_StaticWeapon *w = PortStatic();
        for (const auto &s : w->m_seats)
            v.occupants.push_back(Ptr(s.occupant));
        v.state = w->m_state;
        v.started = w->m_started;
    }
    v.seats = int(v.occupants.size());
    return v;
}
std::string VolumeText(const uint8_t *v)
{
    const S_CollisionVolume &x = *reinterpret_cast<const S_CollisionVolume *>(v);
    return F("volume flags %x type %x frame %s actor %s radius ", x.flags, x.type, NameOf(Ptr(x.frame)).c_str(),
             NameOf(Ptr(x.actor)).c_str()) +
           Fl(x.radius) + " centre " + VectorText(Ptr(&x.center.x), 3) + " box " + VectorText(Ptr(&x.bbox.min.x), 3) + " " +
           VectorText(Ptr(&x.bbox.max.x), 3);
}
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string(); }
std::vector<std::string> StaticState()
{
    std::vector<std::string> out;
    const uint8_t *b = g_static[g_side];
    if (g_side) {
        out.push_back(F("static net %s pos ", NameOf(At<uint32_t>(b, 0x90)).c_str()) + V4(FP(b, 0x30)) + " dir " + V4(FP(b, 0x40)) +
                      " inv " + VectorText(Ptr(b + 0xA0), 16) + " exit " + VectorText(Ptr(b + 0xE0), 3));
        out.push_back("  " + VolumeText(b + 0xF0) +
                      F(" added %d destroyed %s shown %d started %d state %d store changed %d", int(b[0x150]),
                        NameOf(At<uint32_t>(b, 0x154)).c_str(), int(b[0x158]), int(b[0x159]), At<int>(b, 0x2F0), int(b[0x2F4])));
        std::string s = "  seats";
        const uint32_t first = At<uint32_t>(b, 0x2E4), last = At<uint32_t>(b, 0x2E8);
        for (uint32_t q = first; q && q < last; q += 0x0C)
            s += F(" {%s %d %s}", NameOf(At<uint32_t>(reinterpret_cast<void *>(q), 0)).c_str(), int(*reinterpret_cast<uint8_t *>(q + 4)),
                   NameOf(At<uint32_t>(reinterpret_cast<void *>(q), 8)).c_str());
        for (uint32_t q = first; q && q < last; q += 0x0C)
            if (const uint8_t *occ = reinterpret_cast<const uint8_t *>(At<uint32_t>(reinterpret_cast<void *>(q), 0)))
                s += " at " + V4(FP(occ, 0x30)) + " facing " + V4(FP(occ, 0x40));
        out.push_back(s + " name " + RepText(At<uint32_t>(b, 0x80)));
    } else {
        const C_StaticWeapon *w = PortStatic();
        out.push_back(F("static net %s pos ", NameOf(Ptr(w->m_netObject)).c_str()) + V4(&w->m_position.x) + " dir " +
                      V4(&w->m_direction.x) + " inv " + VectorText(Ptr(&w->m_invMatrix), 16) + " exit " + VectorText(Ptr(&w->m_exit.x), 3));
        out.push_back("  " + VolumeText(reinterpret_cast<const uint8_t *>(&w->m_volume)) +
                      F(" added %d destroyed %s shown %d started %d state %d store changed %d", int(w->m_volumeAdded),
                        NameOf(Ptr(w->m_destroyed)).c_str(), int(w->m_destroyedShown), int(w->m_started), w->m_state,
                        int(w->m_storeChanged)));
        std::string s = "  seats";
        for (const auto &x : w->m_seats)
            s += F(" {%s %d %s}", NameOf(Ptr(x.occupant)).c_str(), int(x.blocked), NameOf(Ptr(x.frame)).c_str());
        for (const auto &x : w->m_seats)
            if (x.occupant)
                s += " at " + V4(&x.occupant->m_position.x) + " facing " + V4(&x.occupant->m_direction.x);
        out.push_back(s + " name " + std::string(w->m_name.c_str()));
    }
    // The net object's settings (the original's offsets on both sides), the replication's epochs.
    if (const uint8_t *n = g_netObject[g_side])
        out.push_back(F("  net mask %d live %d forward %d factory %s context %s epochs %d %d", At<int>(n, 0x38), At<int>(n, 0x3C),
                        int(n[0x40]), NameOf(At<uint32_t>(n, 0x64)).c_str(), NameOf(At<uint32_t>(n, 0x60)).c_str(),
                        At<int>(g_replication[g_side], 0xC4), At<int>(g_replication[g_side], 0xC8)));
    g_weapon[g_side][0] = InnerWeapon();
    for (const std::string &s : WeaponState(0))
        out.push_back("  " + s);
    return out;
}

// ---- one case ----
enum {
    OP_ST_START, OP_ST_STOP, OP_ST_RESTART, OP_ST_STATE, OP_ST_UPDATE, OP_ST_TICK, OP_ST_MESSAGES, OP_ST_ON_MESSAGE,
    OP_ST_NET_STATE, OP_ST_SAVE_LOAD, OP_ST_SEAT, OP_ST_WEAPON, OP_ST_CONTROLS, OP_ST_HIT, OP_ST_CORNERS, OP_ST_KINDS
};
const char *const OP_ST_NAMES[] = {"Start", "Stop", "Restart", "SetState", "Update", "Tick", "Messages", "OnMessage",
                                   "NetState", "SaveLoad", "Seat", "Weapon", "Controls", "Hit", "Corners"};
struct S_StOp {
    int kind = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_StCase {
    int seats = 2;                                      // (SEAT00.. dummies)
    std::vector<std::string> seatProps;
    uint32_t cameraMask = 0;                            // (per seat: 1 FPV under it, 2 3RD under it, 4 under its parent)
    bool seatsUnderModel = true;
    S_SwSeat weapon;                                    // (seat 0's parts)
    std::string file;
    int storeSlots = 2;
    uint32_t storeSeed = 0;
    uint32_t role = GAME_ROLE_LOCAL;
    int script = 0;                                     // (0: no frame data; 1: data; 2: a script)
    std::vector<S_StOp> ops;
};
int g_stCases, g_stOpCounts[OP_ST_KINDS];
S_StCase RandomStCase()
{
    S_StCase c;
    c.seats = P(0.1) ? 0 : I(1, 3);
    static const char *const props[] = {"", "SEAT12", "SEAT3 name55", "name7 NO3RD", "SEAT1,2 name12", "x SEAT9 y"};
    for (int i = 0; i < c.seats; ++i)
        c.seatProps.push_back(props[I(0, 5)]);
    c.cameraMask = U32() & 0x1FF;
    c.seatsUnderModel = P(0.7);
    c.weapon.seat = 0;
    c.weapon.hort = P(0.95);
    c.weapon.vert = P(0.95);
    c.weapon.aim = P(0.7);
    c.weapon.view = P(0.7);
    c.weapon.seatUnderHort = false;
    c.weapon.vertUnderHort = P(0.9);
    c.weapon.barrels = P(0.05) ? 0 : I(1, 2);
    c.weapon.noEjector = P(0.3) ? uint32_t(I(0, 3)) : 0u;
    c.weapon.barrelProps = BarrelProps();
    static const char *const hortProps[] = {"", "soundm3", "soundm3 sounds4", "sounds7"};
    c.weapon.hortProps = hortProps[I(0, 3)];
    static const char *const files[] = {"MG42.i3d", "flak", "bad.i3d", "pak40", "none.i3d"};
    c.file = files[I(0, 4)];
    c.storeSlots = I(0, 3);
    c.storeSeed = U32();
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    // (Network games: the item and the turret there, for the refills: the original reads the item through null.)
    if (c.role != GAME_ROLE_LOCAL) {
        c.weapon.hort = c.weapon.vert = true;
        c.weapon.barrels = std::max(c.weapon.barrels, 1);
        c.weapon.barrelProps = F("itemid%d ", 3 + I(0, 4) * 3 + I(0, 1)) + c.weapon.barrelProps;
    }
    c.script = I(0, 2);
    const int count = P(0.1) ? I(80, 200) : I(5, 60);
    for (int i = 0; i < count; ++i) {
        S_StOp op;
        const int r = I(0, 99);
        op.kind = r < 5 ? OP_ST_START : r < 7 ? OP_ST_STOP : r < 9 ? OP_ST_RESTART : r < 13 ? OP_ST_STATE : r < 30 ? OP_ST_UPDATE
                : r < 40 ? OP_ST_TICK : r < 46 ? OP_ST_MESSAGES : r < 49 ? OP_ST_ON_MESSAGE : r < 52 ? OP_ST_NET_STATE
                : r < 57 ? OP_ST_SAVE_LOAD : r < 75 ? OP_ST_SEAT : r < 85 ? OP_ST_WEAPON : r < 92 ? OP_ST_CONTROLS
                : r < 97 ? OP_ST_HIT : OP_ST_CORNERS;
        op.a = I(0, 1000);
        op.b = I(-1, 3);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    if (P(0.9)) {
        S_StOp op;
        op.kind = OP_ST_START;
        op.c = I(0, 1000);
        c.ops.insert(c.ops.begin(), op);
    }
    return c;
}

// The weapon's model (by the case: the same on both sides).
uint8_t *g_stModel[2];
void BuildStaticModel(const S_StCase &c)
{
    uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
    uint8_t *model = Child(sector, FRAME_MODEL, "mount", nullptr);
    Mut<const char *>(model, 0x134) = Text(c.file);
    g_stModel[g_side] = model;
    g_noAnims.clear();
    // Seat 0's weapon parts.
    const S_SwSeat &s = c.weapon;
    uint8_t *hort = s.hort ? Child(model, FRAME_DUMMY, "HORT00", model) : nullptr;
    if (hort)
        AsFrame(hort)->m_userProps = Text(s.hortProps);
    uint8_t *vert = s.vert ? Child(hort && s.vertUnderHort ? hort : model, FRAME_DUMMY, "VERT00", model) : nullptr;
    if (s.aim)
        Child(vert ? vert : model, FRAME_DUMMY, "AIM00", model);
    for (int n = 0; n < s.barrels; ++n) {
        uint8_t *barrel = Child(vert ? vert : model, FRAME_VISUAL, F("BARREL00_%02d", n), model);
        if (n == 0)
            AsFrame(barrel)->m_userProps = Text(s.barrelProps);
        Child(barrel, FRAME_DUMMY, "SHDUM", model);
        if (!(s.noEjector & (1u << n)))
            Child(barrel, FRAME_DUMMY, "CARDUM01", model);
    }
    if (s.view)
        Child(model, FRAME_VISUAL, "VIEW00", model);
    // Visuals with boxes (some flagged, one empty) for the weapon's box.
    const float boxes[][6] = {{-1, -0.5f, -2, 1, 0.5f, 2}, {0, 0, 0, 0.25f, 3, 0.5f}, {2, 2, 2, -2, -2, -2}, {-3, 0, -1, -2, 1, 1}};
    int k = 0;
    for (const char *name : {"base", "shield", "wheel", "ammo"}) {
        uint8_t *v = Child(k == 3 && vert ? vert : model, FRAME_VISUAL, name, model, k == 1 ? I3DVISUAL_SINGLEMESH : I3DVISUAL_OBJECT);
        S_FakeFrame &f = W().frames.back();
        if (k == 1)
            f.visualType = I3DVISUAL_SINGLEMESH;
        float *box = reinterpret_cast<float *>(v + 0x1A0);
        std::memcpy(box, boxes[k], 12);
        std::memcpy(box + 4, boxes[k] + 3, 12);
        Mut<uint32_t>(v, 0x1D0) |= VISFLAGS_BOUND_VALID | (k == 3 && (c.storeSeed & 1) ? VISFLAGS_1000 : 0u);
        ++k;
    }
    // The seats (under the model or HORT), their cameras.
    for (int i = 0; i < c.seats; ++i) {
        uint8_t *seat = Child(c.seatsUnderModel || !hort ? model : hort, FRAME_DUMMY, F("SEAT%02d", i), model);
        AsFrame(seat)->m_userProps = Text(c.seatProps[size_t(i)]);
        const uint32_t m = (c.cameraMask >> (3 * i)) & 7;
        if (m & 1)
            Child(seat, FRAME_DUMMY, F("cameraFPV%d", i), model);
        if (m & 2)
            Child(seat, FRAME_DUMMY, F("camera3RD%d", i), model);
        if (m & 4)
            Child(c.seatsUnderModel || !hort ? model : hort, FRAME_DUMMY, i % 2 ? "cameraFPVx" : "camera3RDx", model);
    }
}

using T_StInt = void(TC *)(void *, int);
using T_StChunk = bool(TC *)(void *, void *);
std::string g_previousStSave[2];
std::string RunStOp(const S_StOp &op, int index)
{
    const bool o = g_side != 0;
    uint8_t *b = g_static[g_side];
    C_StaticWeapon *p = PortStatic();
    const S_StView v = StView();
    const S_SwView wv = ViewOf(InnerWeapon());
    const int t = FRAME_TIMES[op.a % 12];
    uint8_t *human = HumanActor(op.a);
    switch (op.kind) {
    case OP_ST_START:
    case OP_ST_STOP:
    case OP_ST_RESTART: {
        const bool server = op.c & 1;
        const uint32_t at = op.kind == OP_ST_START ? O_ST_START : op.kind == OP_ST_STOP ? O_ST_STOP : O_ST_RESTART;
        // (A restart of a weapon never started: the original reads its frames' seats unchecked; fine in both.)
        if (o)
            OriginalAt<T_StInt>(at)(b, server);
        else if (op.kind == OP_ST_START)
            p->StartMission(server);
        else if (op.kind == OP_ST_STOP)
            p->StopMission(server);
        else
            p->RestartMission(server);
        return F("%s %d", OP_ST_NAMES[op.kind], int(server));
    }
    case OP_ST_STATE: {
        const int state = op.c % 5 == 4 ? 7 : op.c % 4;
        if (o)
            OriginalAt<T_StInt>(O_ST_SET_STATE)(b, state);
        else
            p->SetState(state);
        return F("SetState %d", state);
    }
    case OP_ST_UPDATE:
    case OP_ST_TICK:
    case OP_ST_MESSAGES: {
        AdvanceTime(t);
        if (op.kind == OP_ST_MESSAGES) {
            // Messages for the net object to hand out (then an update or a tick).
            for (int k = 0; k < 1 + op.c % 3; ++k) {
                // (No reload without the weapon's gunner: the original reads his role through null, the port's fix not.)
                if ((op.d + k) % 4 == SEAT_WEAPON_MSG_RELOAD && !wv.gunner)
                    continue;
                // (A shot's barrel among the weapon's: the original reads past them, the port's fix refuses it.)
                if (uint8_t *m = MakeMessage((op.d + k) % 4, op.a + k, op.b + 1 + k, wv.barrels ? (op.c + k) % wv.barrels : 0))
                    g_popQueue[g_side].push_back(m);
            }
            if (o)
                Mut<uint32_t>(g_origGame, 0x28) += 16000;
            else
                reinterpret_cast<C_game *>(g_portGameBuf)->m_missionTime += 16000;
        }
        // (The seat weapon's turret with a gunner and no turret frames: read unchecked in both programs.)
        if (wv.gunner && !wv.failed && (!wv.hort || !wv.vert))
            return F("%s (no turret)", OP_ST_NAMES[op.kind]);
        // (The messages read with a net object only: the original reads it unchecked.)
        const bool tick = op.kind == OP_ST_TICK || (op.kind == OP_ST_MESSAGES && (op.d & 1));
        const bool update = !tick;
        // (An update without seats: the original reads the first seat's gunner through null.)
        if (update && !v.seats)
            return F("%s (no seats)", OP_ST_NAMES[op.kind]);
        // (A network game's tick after a stop: the refill reads the released item, in both programs.)
        if (tick && (g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL && !wv.failed && !wv.item)
            return F("%s (no item)", OP_ST_NAMES[op.kind]);
        if (!(g_gameRole & GAME_ROLE_LOCAL) || (g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL) {
            const bool net = o ? At<uint32_t>(b, 0x90) != 0 : p->m_netObject != nullptr;
            if (!net)
                return F("%s (no net object)", OP_ST_NAMES[op.kind]);
        }
        // (A popped shot or reload with the item gone or no barrels: read unchecked in both programs.)
        if (!g_popQueue[g_side].empty() && (!wv.item || !wv.barrels))
            g_popQueue[g_side].clear();
        if (o)
            OriginalAt<T_StInt>(update ? O_ST_UPDATE : O_ST_TICK)(b, t);
        else if (update)
            p->Update(t);
        else
            p->Tick(t);
        g_popQueue[g_side].clear();
        return F("%s %d", update ? "Update" : "Tick", t);
    }
    case OP_ST_ON_MESSAGE: {
        // The kind, then the state or the store (uid and type pairs, 0xFFFF last).
        g_bits[g_side].clear();
        g_bits[g_side].push_back(uint32_t(op.c % 4));
        if (op.c % 4 == 1)
            g_bits[g_side].push_back(uint32_t(op.d % 4));
        else if (op.c % 4 == 0) {
            for (int k = 0; k < op.a % 3; ++k) {
                g_bits[g_side].push_back(uint32_t(400 + k));
                g_bits[g_side].push_back(uint32_t(4 + k));
            }
            g_bits[g_side].push_back(0xFFFF);
        }
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_ST_ON_MESSAGE)(b, W().bitstream);
        else
            p->OnMessage(reinterpret_cast<C_BitStream *>(W().bitstream));
        g_bits[g_side].clear();
        return F("OnMessage %d %d", op.c % 4, op.d % 4);
    }
    case OP_ST_NET_STATE: {
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_ST_WRITE_STATE)(b, W().bitstream);
        else
            p->WriteState(reinterpret_cast<C_BitStream *>(W().bitstream));
        g_bits[g_side].clear();
        g_bits[g_side].push_back(uint32_t(op.d % 4));
        for (int k = 0; k < op.a % 3; ++k) {
            g_bits[g_side].push_back(uint32_t(500 + k));
            g_bits[g_side].push_back(uint32_t(5 + k));
        }
        g_bits[g_side].push_back(0xFFFF);
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_ST_READ_STATE)(b, W().bitstream);
        else
            p->ReadState(reinterpret_cast<C_BitStream *>(W().bitstream));
        g_bits[g_side].clear();
        return F("WriteState ReadState %d", op.d % 4);
    }
    case OP_ST_SAVE_LOAD: {
        // (The seat weapon's save reads its item; its load its turret.)
        if (!wv.item)
            return "SaveLoad (no item)";
        const std::string file = F("st_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_StChunk>(O_ST_SAVE)(b, &save) : p->SaveGame(save);
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
        const int load = op.c % 4;
        bool rl = false;
        const std::string previous = g_previousStSave[g_side];
        g_previousStSave[g_side] = file;
        if (load && wv.hort && wv.vert) {
            std::string name = load == 2 && !previous.empty() ? previous : file;
            if (load == 3) {
                // A made file: the state, an unknown entry, the store (logged), the seat weapon's state (its barrel in
                // range), in either order.
                S_Writer w;
                w.Begin(0x2000);
                const bool weaponFirst = op.a & 1;
                auto weapon = [&] {
                    w.Begin(0x3E9);
                    w.Begin(2000);
                    const float tilt = 0.25f, turn = -0.5f;
                    const uint32_t flags = op.d % 2 ? 0x20 : 0;
                    const int barrel = wv.barrels ? op.d % wv.barrels : 0, delay = op.c % 300, left = 0, total = 0;
                    for (const void *x : {static_cast<const void *>(&tilt), static_cast<const void *>(&turn),
                                          static_cast<const void *>(&flags), static_cast<const void *>(&barrel),
                                          static_cast<const void *>(&delay), static_cast<const void *>(&left),
                                          static_cast<const void *>(&total)})
                        w.Raw(x, 4);
                    w.End();
                    w.End();
                };
                if (weaponFirst)
                    weapon();
                w.EntryOf(1000, op.d % 5 == 4 ? 9 : op.d % 4);
                if (op.d % 3 == 0)
                    w.EntryOf(0x1234, 5);
                w.EntryOf(0x3EA, 0);
                if (!weaponFirst)
                    weapon();
                w.End();
                name = F("st_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_StChunk>(O_ST_LOAD)(b, &chunk) : p->LoadGame(chunk);
            chunk.Close();
        }
        return F("SaveGame -> %d LoadGame %d -> %d bytes ", int(rs), load, int(rl)) + hex;
    }
    case OP_ST_SEAT: {
        const int seat = op.b;
        const int which = op.c % 12;
        switch (which) {
        case 0: {
            const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_ST_IS_BLOCKED)(b, seat) : p->IsSeatBlocked(seat);
            return F("IsSeatBlocked %d -> %d", seat, int(r));
        }
        case 1: {
            const bool blocked = op.d & 1;
            const bool r = o ? OriginalAt<bool(TC *)(void *, int, int)>(O_ST_SET_BLOCKED)(b, seat, blocked)
                             : p->SetSeatBlocked(seat, blocked);
            return F("SetSeatBlocked %d %d -> %d", seat, int(blocked), int(r));
        }
        case 2:
        case 3: {
            // (Into a seat with a turret, or none: the gunner's controls and his view read it.)
            uint8_t *who = which == 2 || op.d % 4 ? human : nullptr;
            if (who && wv.started && !wv.failed && (!wv.hort || !wv.vert))
                return "SetSeatOccupant (no turret)";
            if (o)
                OriginalAt<void(TC *)(void *, int, void *)>(O_ST_SET_OCCUPANT)(b, seat, who);
            else
                p->SetSeatOccupant(seat, reinterpret_cast<C_Actor *>(who));
            return F("SetSeatOccupant %d %s", seat, NameOf(Ptr(who)).c_str());
        }
        case 4: {
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_ST_GET_OCCUPANT)(b, seat)) : Ptr(p->GetSeatOccupant(seat));
            return F("GetSeatOccupant %d -> %s", seat, NameOf(r).c_str());
        }
        case 5: {
            uint8_t *who = op.d % 3 ? human : nullptr;
            const int r = o ? OriginalAt<int(TC *)(void *, const void *)>(O_ST_FIND_SEAT)(b, who) : p->FindSeat(reinterpret_cast<C_Actor *>(who));
            return F("FindSeat %s -> %d", NameOf(Ptr(who)).c_str(), r);
        }
        case 6: {
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_ST_LEAVE)(b, seat)) : Ptr(p->LeaveSeat(seat));
            return F("LeaveSeat %d -> %s", seat, NameOf(r).c_str());
        }
        case 7: {
            const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_ST_EXIT)(b, seat) : p->ExitSeat(seat);
            return F("ExitSeat %d -> %d", seat, int(r));
        }
        case 8: {
            const uint32_t r = o ? OriginalAt<uint32_t(TC *)(void *, int)>(O_ST_SEAT_FLAGS)(b, seat) : p->GetSeatFlags(seat);
            const int n = o ? OriginalAt<int(TC *)(void *)>(O_ST_SEAT_COUNT)(b) : p->GetSeatCount();
            const bool fpv = o ? OriginalAt<bool(TC *)(void *, int)>(O_ST_FPV_ONLY)(b, seat) : p->IsSeatFirstPersonOnly(seat);
            return F("GetSeatFlags %d -> %x count %d fpv only %d", seat, r, n, int(fpv));
        }
        case 9: {
            // (Not a seat dummy without a parent: the original calls through null. The model's are linked.)
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_ST_SEAT_CAMERA)(b, seat)) : Ptr(p->GetSeatCamera(seat));
            return F("GetSeatCamera %d -> %s", seat, NameOf(r).c_str());
        }
        case 10: {
            const int r = o ? OriginalAt<int(TC *)(void *, int)>(O_ST_SEAT_TEXT)(b, seat) : p->GetSeatText(seat);
            return F("GetSeatText %d -> %d", seat, r);
        }
        default: {
            const bool has = o ? OriginalAt<bool(TC *)(void *, int)>(O_ST_HAS_WEAPON)(b, seat) : p->HasSeatWeapon(seat);
            const int hp = o ? OriginalAt<int(TC *)(void *)>(O_ST_HIT_POINTS)(b) : p->GetHitPoints();
            alignas(16) float exit[4] = {9.0f, 9.0f, 9.0f, 9.0f};
            if (o)
                OriginalAt<void(TC *)(void *, float *)>(O_ST_EXIT_POS)(b, exit);
            else
                p->GetExitPosUnused(reinterpret_cast<S_vector4 *>(exit));
            // (The exit's w: a stack word in the original, from FindExit.)
            return F("HasSeatWeapon %d -> %d hit points %d exit ", seat, int(has), hp) + VectorText(Ptr(exit), 3);
        }
        }
    }
    case OP_ST_WEAPON: {
        // The seat weapon's virtuals (whatever the seat): those reading the item or the turret with them only.
        const int seat = op.b;
        std::string s = F("weapon %d", seat);
        if (wv.item) {
            const int r = o ? OriginalAt<int(TC *)(void *, int)>(O_ST_ROUNDS)(b, seat) : p->GetSeatWeaponRounds(seat);
            s += F(" rounds %d", r);
        }
        if (wv.failed || wv.item) {
            const int text = o ? OriginalAt<int(TC *)(void *, int)>(O_ST_WEAPON_TEXT)(b, seat) : p->GetSeatWeaponText(seat);
            const uint32_t def = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_ST_WEAPON_DEF)(b, seat)) : Ptr(p->GetSeatWeaponDef(seat));
            s += F(" text %d def %s", text, NameOf(def).c_str());
        }
        if (wv.failed || wv.vert) {
            alignas(16) float aim[4] = {}, origin[4] = {};
            if (o) {
                OriginalAt<void *(TC *)(void *, float *, int)>(O_ST_AIM)(b, aim, seat);
                OriginalAt<void *(TC *)(void *, float *, int)>(O_ST_ORIGIN)(b, origin, seat);
            } else {
                const S_vector x = p->GetSeatWeaponAim(seat), y = p->GetSeatWeaponOrigin(seat);
                std::memcpy(aim, &x, 12);
                std::memcpy(origin, &y, 12);
            }
            alignas(16) float target[4] = {origin[0] + float(op.a % 20) - 10.0f, origin[1] + float(op.c % 10) - 5.0f,
                                           origin[2] + float(op.d % 20) - 10.0f, 1.0f};
            const bool can = o ? OriginalAt<bool(TC *)(void *, int, float *)>(O_ST_CAN_AIM)(b, seat, target) : p->CanSeatWeaponAim(seat, target);
            s += " aim " + VectorText(Ptr(aim), 3) + " origin " + VectorText(Ptr(origin), 3) + F(" can %d", int(can));
        }
        const float ac = o ? OriginalAt<float(TC *)(void *, int)>(O_ST_AC)(b, seat) : p->UnknownAC(seat);
        float lim[4] = {};
        if (o)
            OriginalAt<void(TC *)(void *, int, float *, float *, float *, float *)>(O_ST_LIMITS)(b, seat, lim, lim + 1, lim + 2, lim + 3);
        else
            p->GetSeatWeaponLimits(seat, lim, lim + 1, lim + 2, lim + 3);
        int c8[3] = {7, 8, 9};
        const bool r8 = o ? OriginalAt<bool(TC *)(void *, int, int *, int *, int *)>(O_ST_C8)(b, seat, c8, c8 + 1, c8 + 2)
                          : p->UnknownC8(seat, c8, c8 + 1, c8 + 2);
        const bool on = op.d & 1;
        if (o)
            OriginalAt<void(TC *)(void *, int, int)>(O_ST_SHOW_VIEW)(b, seat, on);
        else
            p->ShowSeatView(seat, on);
        return s + " ac " + Fl(ac) + " limits " + VectorText(Ptr(lim), 4) + F(" c8 %d %d %d %d view %d", int(r8), c8[0], c8[1], c8[2], int(on));
    }
    case OP_ST_CONTROLS: {
        const int seat = op.b;
        // (A seat out of range: the original reads past the seats, the port's fix refuses it.)
        if (seat < 0 || seat >= v.seats)
            return "ApplySeatControls (no seat)";
        // (A manned seat's controls: the seat weapon reads its item, barrels and owner unchecked.)
        if (seat >= 0 && seat < v.seats && v.occupants[size_t(seat)] && wv.gunner &&
            (!wv.item || !wv.barrels || wv.barrel >= wv.barrels))
            return "ApplySeatControls (no item, barrels)";
        float axes[64] = {}, buttons[64] = {};
        static const float values[] = {0.0f, 1.0f, -1.0f, 0.5f, 0.0f, 2.0f};
        axes[0x34] = values[op.a % 6];
        axes[0x35] = values[op.c % 6];
        axes[6] = op.d % 3 == 0 ? 1.0f : 0.0f;
        axes[13] = op.d % 7 == 0 ? 1.0f : 0.0f;
        buttons[6] = op.d % 4 == 1 ? 1.0f : 0.0f;
        if (o)
            OriginalAt<void(TC *)(void *, int, float *, float *)>(O_ST_CONTROLS)(b, seat, axes, buttons);
        else
            p->ApplySeatControls(seat, axes, buttons);
        return F("ApplySeatControls %d", seat);
    }
    case OP_ST_HIT: {
        uint8_t *attacker = op.d % 4 ? HumanActor(op.d) : nullptr;
        static const float amounts[] = {0.0f, 10.0f, 55.5f, 100.0f};
        const S_vector pos(1.0f, 2.0f, 3.0f);
        // (Tiny and zero now and then: the normalisation's edges.)
        static const S_vector tiny[] = {S_vector(1e-5f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, -1e-5f), S_vector(0.0f, 0.0f, 0.0f),
                                        S_vector(-1e-5f, 0.0f, 0.0f), S_vector(0.0f, 1e-5f, 0.0f), S_vector(0.0f, 0.0f, 1e-5f)};
        const S_vector dir = op.d % 4 == 3 ? tiny[op.a % 6] : S_vector(float(op.a % 3) - 1.0f, 0.5f, float(op.c % 5) - 2.0f);
        const int type = op.c % 4;
        if (o)
            OriginalAt<void(TC *)(void *, int, float, const S_vector *, const S_vector *, void *, int)>(O_ST_HIT)(
                b, type, amounts[op.a % 4], &pos, &dir, attacker, 0);
        else
            p->Hit(E_HitType(type), amounts[op.a % 4], pos, dir, reinterpret_cast<C_Actor *>(attacker), 0);
        return F("Hit %d %s", type, NameOf(Ptr(attacker)).c_str());
    }
    default: {
        alignas(16) S_vector4 corners[8] = {};
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_ST_CORNERS)(b, corners);
        else
            p->GetBoxCorners(corners);
        std::string s = "GetBoxCorners";
        for (const S_vector4 &x : corners)
            s += " " + VectorText(Ptr(&x.x), 3);
        return s;
    }
    }
}

volatile DWORD g_stCaseStart;
DWORD WINAPI StWatchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_stCaseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("st_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_stShow;
void RunStCase(const S_StCase &c)
{
    g_stCaseStart = GetTickCount();
    ++g_stCases;
    g_holdFrees = true;
    g_current = F("case %d", g_stCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_weaponCount = 1;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_world[side].frames.reserve(65536);
        g_texts[side].clear();
        g_items[side].clear();
        g_groups[side].clear();
        g_bits[side].clear();
        g_popQueue[side].clear();
        g_localPlayers[side].clear();
        g_previousStSave[side].clear();
        BuildWorld(side, worldSeed);
        g_sndDriver[side] = Block(0x40);
        *reinterpret_cast<void ***>(g_sndDriver[side]) = EngineVtable("ISND_driver");
        Name(g_sndDriver[side], "sound driver");
        g_physicsModule[side] = Block(0x80);
        *reinterpret_cast<void ***>(g_physicsModule[side]) = g_physicsVtable[side];
        Name(g_physicsModule[side], "physics module");
        g_netObject[side] = Block(0x100);
        Name(g_netObject[side], "net object");
        // (Its defaults: live 7, forwarding; the replication's epochs other than StartMission's.)
        Mut<int>(g_netObject[side], 0x3C) = 7;
        g_netObject[side][0x40] = 1;
        g_replication[side] = Block(std::max(sizeof(C_NetReplication), size_t(0x100)) + 0x40);
        Name(g_replication[side], "net replication");
        g_sender[side] = Block(0x40);
        Name(g_sender[side], "sender");
        Mut<int>(g_replication[side], 0xC4) = 5;
        Mut<int>(g_replication[side], 0xC8) = 6;
        if (side) {
            *OriginalAt<uint8_t **>(O_NET_REPLICATION) = g_replication[1];
            Mut<int>(g_replication[1], 0xDC) = g_localId;
        } else
            g_netReplication = reinterpret_cast<C_NetReplication *>(g_replication[0]);
        BuildStaticModel(c);
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
        BuildStore(c.storeSlots, c.storeSeed);
        // The weapon (its frame set, its store, its frame's data).
        uint8_t *b;
        if (side) {
            b = Block(0x300 + 16);
            OriginalAt<T_Void>(O_ST_CTOR)(b);
        } else {
            b = Block(sizeof(C_StaticWeapon) + 16);
            ::new (b) C_StaticWeapon();
        }
        g_static[side] = b;
        g_owner[side] = b;
        Name(b, "static weapon");
        uint8_t **containers = reinterpret_cast<uint8_t **>(Block(16));
        containers[0] = g_store[side].container;
        if (side)
            Mut<uint8_t **>(b, 0x74) = containers;
        else
            PortStatic()->m_containers = reinterpret_cast<C_ItemContainer **>(containers);
        if (c.script) {
            S_FrameData *data = reinterpret_cast<S_FrameData *>(Block(0x20));
            data->actor = reinterpret_cast<C_Actor *>(b);
            if (c.script == 2) {
                uint8_t *script = Block(0x40);
                Name(script, "script");
                data->script = reinterpret_cast<C_Script *>(script);
            }
            AsFrame(g_stModel[side])->m_data = data;
        }
        // (The base's SetFrame is logged: the frame set here, as it would.)
        if (side)
            Mut<uint8_t *>(b, 0x20) = g_stModel[1];
        else
            PortStatic()->m_frame = AsFrame(g_stModel[0]);
        const bool framed = side ? OriginalAt<bool(TC *)(void *, void *)>(O_ST_SET_FRAME)(b, g_stModel[1])
                                 : PortStatic()->SetFrame(AsFrame(g_stModel[0]));
        out.push_back(F("SetFrame -> %d", int(framed)));
        for (const std::string &s : StaticState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_ST_TRACE") ? std::atoi(std::getenv("HD2_ST_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_stOpCounts[c.ops[i].kind];
            if (trace == g_stCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_ST_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunStOp(c.ops[i], int(i)));
            if (trace == g_stCases) {
                std::printf("trace   -> %s\n", out.back().substr(0, 160).c_str());
                std::fflush(stdout);
            }
            for (const std::string &s : g_log[side])
                out.push_back("  call " + MaskSoundW(s));
            g_log[side].clear();
            for (const std::string &s : StaticState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed; the creator's make and destroy.
        if (side) {
            OriginalAt<T_StInt>(O_ST_STOP)(b, 0);
            OriginalAt<T_Void>(O_ST_DTOR)(b);
            void *made = OriginalAt<void *(TC *)(void *)>(O_ST_CREATE)(OriginalAt<void *>(O_ST_CREATOR));
            Name(made, "created weapon");
            out.push_back(F("created type %d state %d", At<int>(made, 0x18), At<int>(made, 0x2F0)));
            OriginalAt<void(TC *)(void *, void *)>(O_ST_DESTROY)(OriginalAt<void *>(O_ST_CREATOR), made);
        } else {
            PortStatic()->StopMission(false);
            PortStatic()->~C_StaticWeapon();
            C_Actor *made = g_actorCreators[ACTOR_STATIC_WEAPON]->Create();
            Name(made, "created weapon");
            out.push_back(F("created type %d state %d", int(made->GetType()), static_cast<C_StaticWeapon *>(made)->m_state));
            g_actorCreators[ACTOR_STATIC_WEAPON]->Destroy(made);
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + MaskSoundW(s));
        for (const std::string &s : SwFrameStates())
            out.push_back(s);
        ClearSwCaches();
        g_static[side] = nullptr;
    }
    static const int only = std::getenv("HD2_ST_ONLY") ? std::atoi(std::getenv("HD2_ST_ONLY")) : 0;
    if (g_stShow && (!only || only == g_stCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_stShow == 2)
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

void SetUpStatic()
{
    SetUpSw();
    SetUpStaticAnswers();
    g_bitsEmptyOnes = true;
    g_virtuals.push_back({"C_ItemDef::GetAmmoItemId", 0x18, PortSlot(&C_ItemDef::GetAmmoItemId), "", 'i'});
    g_virtuals.push_back({"C_ItemContainer::CountItems", 0x24, PortSlot(&C_ItemContainer::CountItems), "i", 'i'});
    g_virtuals.push_back({"C_ItemContainer::GetUsedSlotCount", 0x28, PortSlot(&C_ItemContainer::GetUsedSlotCount), "", 'i'});
    g_virtuals.push_back({"C_ItemContainer::AddItem", 0x2C, PortSlot(&C_ItemContainer::AddItem), "ppi", 'i'});
    g_virtuals.push_back({"C_ItemContainer::GetSlots", 0x3C, PortSlot(&C_ItemContainer::GetSlots), "", 'p'});
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_containerVtable[side] = GameVtable("C_ItemContainer", VirtualSlots("C_ItemContainer::", side != 0));
        g_itemDefVtable[side] = GameVtable("C_ItemDef", VirtualSlots("C_ItemDef::", side != 0));
        Name(side ? OriginalAt<void *>(0x004c14b0) : reinterpret_cast<void *>(&MakeSeatWeaponMessage), "seat weapon factory");
    }
    RedirectOriginal(0x006e5380, reinterpret_cast<const void *>(&O_TextsGet));
    RedirectOriginal(0x0041d960, reinterpret_cast<const void *>(&A_OnUse));
    {
        auto onUse = &C_Script::OnUse;
        void *at;
        std::memcpy(&at, &onUse, sizeof at);
        PatchPort(at, reinterpret_cast<const void *>(&A_OnUse));
    }
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("st_diff: main");
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
        std::printf("st_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_ST_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_ST_SHOW"))
        g_stShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_ST_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_ST_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpStatic();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, StWatchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunStCase(RandomStCase());
    std::string ops;
    for (int i = 0; i < OP_ST_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_ST_NAMES[i], g_stOpCounts[i]);
    std::printf("st_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_stCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
