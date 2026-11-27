// Explosive place part 1: the explosive places (game/explosive_place.cpp, the original's 0x0060c170..0x0060da80) against
// the original executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the places'
// calls go through ep_calls.s / ep_calls.inc (gen_cmd_calls.py ep). The LS3DF math (the inverse) is LS3DF.dll's on both
// sides; the game's texts are logged on both sides; the item definitions are the test's (the original's table at
// 0x0098b030, read inline; the port's GetItemDef answers from the test's own).
//
// Each case builds the same world on each side (cmd_diff's) and one or two places: frames under the primary sector
// (turned, tilted, scaled, now and then with a tiny, zero or vertical z axis) with their "explosive_on" and
// "explosive_off" children (now and then missing) and a visual with user properties, makes the places on each side (the
// original's by its constructor 0x0060c230 in a block of 0x180, the port's by its own) and runs the same random sequence
// of operations in the case's role: properties read (made ones: every entry, any order, foreign ones) and written,
// CopyFrom, starts, stops, restarts, SetState, Place (humans with the charge or without), Disarm, ticks and updates (the
// countdown at its edges), the clients' messages, the network state written and read, the saved game's calls (the file
// just written, made ones), GetMaterialId. After each operation the answer, every place's state (every field, its
// volume, its frames) and every call made into the game and the engine (the volumes, the explosions, the messages) are
// compared; the properties' and the saves' bytes too. The creator makes and destroys one.
// MinGW; build_ep.bat; runs ep_diff.exe ("ep_diff: N checks, 0 failures"). Env: HD2_EP_CASES (default 1,500;
// HD2_EP_QUICK: 300), HD2_EP_SEED, HD2_EP_SHOW (1: the port's lines; 2: both), HD2_EP_ONLY (the case shown),
// HD2_EP_TRACE (a case's operations as they start).
#define HARNESS_NAME "ep_diff"
#define CMD_CALLS_INC "ep_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/collision.h"
#include "game/explosive_place.h"
#include "game/game_texts.h"
#include "game/item_defs.h"
#include "game/items.h"
#undef protected
#undef private

// ---- the port's side of what the places call and the test provides ----
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

// LS3DF.dll's math (the original's calls go there too).
template <class T> T Ls3dfFn(const char *name) { return reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df(name))); }
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
// A frame's "matid" (the original's own function on both sides: not logged).
int GetFrameMaterialId(const I3D_frame *frame) { return OriginalAt<int(__stdcall *)(const I3D_frame *)>(0x00611090)(frame); }
// A hierarchy's box (the original's own function on both sides, its engine calls logged: ECX the frame, a static box).
I3D_bbox ComputeHierarchyBox(I3D_frame *frame)
{
    return *OriginalAt<const I3D_bbox *(__attribute__((thiscall)) *)(I3D_frame *)>(0x006111f0)(frame);
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
// The actors' creators (the places' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the place's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}

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
// A frame's local matrix set whole (rows scaled), as I3D_frame::SetLocalMatrix: the rotation taken from it.
void SetLocal(uint8_t *frame, const S_matrix &m, const S_vector &scale)
{
    I3D_frame *f = AsFrame(frame);
    f->m_localMatrix = m;
    f->m_scale = scale;
    f->m_frameFlags = (f->m_frameFlags & ~0x28u) | FRMFLAGS_CHANGED | FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
}

// ---- the item definitions: per case, each side's layout ----
// (0x44 the places' own; 0x45 and 0x30 others. Not one without a definition: the original reads its text through null.)
const int ITEM_IDS[] = {0x44, 0x44, 0x45, 0x30};
std::vector<uint8_t *> g_defBlocks[2];
void MakeItemDefs()
{
    for (int id : {0x44, 0x45, 0x30}) {
        uint8_t *def = Block(std::max(sizeof(C_ItemDef), size_t(0x100)) + 0x40);
        Name(def, F("item def %x", id));
        const int text = 0x200 + id;
        if (g_side) {
            Mut<int>(def, 0x14) = text;
            OriginalAt<uint8_t **>(0x0098b030)[id] = def;
        } else {
            reinterpret_cast<C_ItemDef *>(def)->m_textId = text;
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

// ---- the places ----
constexpr int MAX_PLACES = 2;
uint8_t *g_place[2][MAX_PLACES];
uint8_t *g_placeFrame[2][MAX_PLACES];
uint8_t *g_placeVisual[2][MAX_PLACES];
int g_placeCount;
C_ExplosivePlace *PortPlace(int i) { return reinterpret_cast<C_ExplosivePlace *>(g_place[0][i]); }
constexpr uint32_t O_EP_CTOR = 0x0060c230, O_EP_DTOR = 0x0060c2e0, O_EP_SET_STATE = 0x0060d300, O_EP_PLACE = 0x0060d6f0,
                   O_EP_DISARM = 0x0060d9f0, O_EP_UPDATE = 0x0060c370, O_EP_TICK = 0x0060c330, O_EP_MESSAGE = 0x0060cda0,
                   O_EP_WRITE = 0x0060c6e0, O_EP_READ = 0x0060ca30, O_EP_COPY = 0x0060c300, O_EP_START = 0x0060c3b0,
                   O_EP_STOP = 0x0060c6b0, O_EP_RESTART = 0x0060cf20, O_EP_SAVE = 0x0060cf70, O_EP_LOAD = 0x0060d0c0,
                   O_EP_WRITE_STATE = 0x0060ce60, O_EP_READ_STATE = 0x0060cea0, O_EP_MATERIAL = 0x0060cee0;
constexpr uint32_t O_EP_CREATOR = 0x008aca30, O_EP_CREATE = 0x0060c1a0, O_EP_DESTROY = 0x0060c210;

// The messages' words (ReadBits answers them in turn on each side); the item a human's FindItem / FindItemByUid finds.
std::deque<uint32_t> g_bits[2];
uint8_t *g_item[2];
// The mesh visuals' types (frame -> fourCC; both sides' frames).
std::map<uint32_t, uint32_t> g_visualType;
// A visual made a mesh with a local bound (+0x1A0, its valid flag 0x20000 at +0x1D0 unless stale). Kind 0..4.
void MeshBound(uint8_t *frame, int kind, bool stale)
{
    static const uint32_t types[] = {0x5f4a424f, 0x50414d4c, 0x4d474e53, 0x524d4d53};
    static const float boxes[][6] = {{-0.3f, 0.0f, -0.1f, 0.3f, 0.5f, 0.1f}, {-0.25f, -0.5f, -0.2f, 0.25f, 2.5f, 0.05f},
                                     {-1.4f, 0.0f, 0.0f, 0.4f, 1.0f, 0.3f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                                     {1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f}};
    const float *b = boxes[kind % 5];
    const float v[8] = {b[0], b[1], b[2], 0.0f, b[3], b[4], b[5], 0.0f};
    std::memcpy(frame + 0x1A0, v, sizeof v);
    Mut<uint32_t>(frame, 0x1D0) = stale ? 0u : 0x20000u;
    g_visualType[Ptr(frame)] = types[kind % 4];
}

void SetUpPlaceAnswers()
{
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    // Frames by name under a frame, of the mask's types.
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        const uint32_t types = a[2] & 0xFFFF;
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                const std::string n = c->m_name ? c->m_name : "";
                if (n == want && c->m_type && (types & (1u << (c->m_type - 1))))
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
    // The visuals' types: the meshes' (the test's marks), else none.
    g_answers["I3D_frame::GetVisualType"] = [](const std::vector<uint32_t> &a) {
        const auto it = g_visualType.find(a[0]);
        return it == g_visualType.end() ? 0u : it->second;
    };
    // The actors' messages: the world's bit stream; their words in turn.
    g_answers["CreateActorMessage"] = [](const std::vector<uint32_t> &) { return Ptr(W().bitstream); };
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
    g_answers["C_Human::FindItem"] = [](const std::vector<uint32_t> &) { return Ptr(g_item[g_side]); };
    g_answers["C_Human::FindItemByUid"] = [](const std::vector<uint32_t> &) { return Ptr(g_item[g_side]); };
}

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
std::string Fl(float f) { return FloatText(f); }
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string(); }
// The volume (S_CollisionVolume: the same layout on both sides; its inverse matrix the place's own; its centre's w a stack word
// nothing writes in the original: left out, as the centre of an empty box, which the original leaves unset).
std::string VolumeText(const S_CollisionVolume &v, const void *inv)
{
    return F("volume flags %x type %x frame %s actor %s matrix %s inv %s radius ", v.flags, v.type, NameOf(Ptr(v.frame)).c_str(),
             NameOf(Ptr(v.actor)).c_str(), NameOf(Ptr(v.matrix)).c_str(), v.invMatrix == inv ? "own" : NameOf(Ptr(v.invMatrix)).c_str()) +
           Fl(v.radius) + " centre " + (v.bbox.min.x > v.bbox.max.x ? std::string("(unset)") : V3(&v.center.x)) + " box " + V3(&v.bbox.min.x) + " " + V3(&v.bbox.max.x);
}
std::vector<std::string> PlaceState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_place[g_side][i];
    if (g_side) {
        out.push_back(F("place%d started %d state %d timed %d text %x item %x delay %d sound %d effect %d damage %d range %d "
                        "startPlaced %d hidden %d on %s off %s planter %d left %d added %d name %s",
                        i, int(b[0x90]), At<int>(b, 0x94), int(b[0x98]), At<int>(b, 0x9C), At<int>(b, 0xA0), At<int>(b, 0xA4),
                        At<int>(b, 0xA8), At<int>(b, 0xAC), At<int>(b, 0xB0), At<int>(b, 0xB4), int(b[0xB8]), int(b[0xB9]),
                        NameOf(At<uint32_t>(b, 0xBC)).c_str(), NameOf(At<uint32_t>(b, 0xC0)).c_str(), At<int>(b, 0xC4),
                        At<int>(b, 0xC8), int(b[0x170]), RepText(At<uint32_t>(b, 0x80)).c_str()));
        out.push_back("  inv " + VectorText(Ptr(b + 0x130), 16));
        out.push_back("  " + VolumeText(*reinterpret_cast<const S_CollisionVolume *>(b + 0xD0), b + 0x130));
        return out;
    }
    const C_ExplosivePlace *p = PortPlace(i);
    out.push_back(F("place%d started %d state %d timed %d text %x item %x delay %d sound %d effect %d damage %d range %d "
                    "startPlaced %d hidden %d on %s off %s planter %d left %d added %d name %s",
                    i, int(p->m_started), p->m_state, int(p->m_timed), p->m_nameTextId, p->m_itemId, p->m_delay, p->m_sound,
                    p->m_effect, p->m_damage, p->m_range, int(p->m_startPlaced), int(p->m_hidden), NameOf(Ptr(p->m_onFrame)).c_str(),
                    NameOf(Ptr(p->m_offFrame)).c_str(), p->m_planterId, p->m_timeLeft, int(p->m_volumeAdded),
                    p->m_name.c_str() ? p->m_name.c_str() : ""));
    out.push_back("  inv " + VectorText(Ptr(&p->m_invMatrix), 16));
    out.push_back("  " + VolumeText(p->m_volume, &p->m_invMatrix));
    return out;
}
std::vector<std::string> PlacesState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_placeCount; ++i)
        for (const std::string &s : PlaceState(i))
            out.push_back(s);
    for (int i = 0; i < g_placeCount; ++i) {
        const I3D_frame *f = AsFrame(g_placeFrame[g_side][i]);
        std::string kids;
        for (const I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
            kids += F(" %s:%08x", c->m_name ? c->m_name : "", c->m_frameFlags);
        out.push_back(F("frame%d flags %08x", i, f->m_frameFlags) + kids);
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
enum { OP_READ, OP_WRITE, OP_COPY, OP_START, OP_STOP, OP_RESTART, OP_SET_STATE, OP_PLACE, OP_DISARM, OP_TICK, OP_UPDATE,
       OP_MESSAGE, OP_NET_STATE, OP_SAVE_LOAD, OP_MATERIAL, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"ReadProperties", "WriteProperties", "CopyFrom", "Start", "Stop", "Restart", "SetState",
                                     "Place", "Disarm", "Tick", "Update", "Message", "NetState", "SaveLoad", "Material"};
struct S_EpOp {
    int kind = 0;
    int place = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_EpPlace {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f, tilt = 0.0f;
    S_vector scale = S_vector(1.0f, 1.0f, 1.0f);
    int odd = 0;                                        // (1..4: its z axis tiny, zero or vertical)
    bool on = true, off = true, visual = true;
    int box = 0, stale = 0;                             // (the meshes' bounds, which are out of date)
    std::string props;
};
struct S_EpCase {
    std::vector<S_EpPlace> places;
    std::vector<S_EpOp> ops;
    uint32_t role = GAME_ROLE_LOCAL;
};
int g_epCases, g_opCounts[OP_KINDS];
// (The countdown's edges.)
const int FRAME_TIMES[] = {0, 16, 100, 500, 1000, 1999, 2000, 2001, 3000, 1, 5000, -5};
const int DELAYS[] = {0, 2000, 1, 5000, 100, -10};
S_EpCase RandomEpCase()
{
    S_EpCase c;
    const int places = P(0.75) ? 1 : 2;
    for (int i = 0; i < places; ++i) {
        S_EpPlace l;
        l.x = float(I(-40, 40)) * 0.5f;
        l.y = float(I(-4, 10)) * 0.5f;
        l.z = float(I(-40, 40)) * 0.5f;
        l.heading = float(I(0, 628)) * 0.01f;
        l.tilt = P(0.3) ? float(I(-40, 40)) * 0.01f : 0.0f;
        if (P(0.3)) {
            static const float scales[] = {1.0f, 2.0f, 0.5f, 1.25f};
            l.scale = S_vector(scales[I(0, 3)], scales[I(0, 3)], scales[I(0, 3)]);
        }
        l.odd = P(0.1) ? I(1, 4) : 0;
        l.on = P(0.93);
        l.off = P(0.93);
        l.visual = P(0.8);
        l.box = I(0, 4);
        l.stale = P(0.2) ? I(1, 3) : 0;
        static const char *const props[] = {"", "matid 7", "MATID12 x", "matid", "nothing here"};
        l.props = props[I(0, 4)];
        c.places.push_back(l);
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    const int count = P(0.1) ? I(40, 120) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_EpOp op;
        const int r = I(0, 99);
        op.kind = r < 8 ? OP_READ : r < 11 ? OP_WRITE : r < 13 ? OP_COPY : r < 19 ? OP_START : r < 23 ? OP_STOP : r < 26 ? OP_RESTART
                : r < 36 ? OP_SET_STATE : r < 48 ? OP_PLACE : r < 53 ? OP_DISARM : r < 65 ? OP_TICK : r < 75 ? OP_UPDATE
                : r < 84 ? OP_MESSAGE : r < 88 ? OP_NET_STATE : r < 96 ? OP_SAVE_LOAD : OP_MATERIAL;
        op.place = I(0, places - 1);
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // Properties read first, mostly; then started.
    for (int i = places; i-- > 0;) {
        if (P(0.9)) {
            S_EpOp start;
            start.kind = OP_START;
            start.place = i;
            start.c = I(0, 1);
            c.ops.insert(c.ops.begin(), start);
        }
        if (P(0.8)) {
            S_EpOp read;
            read.kind = OP_READ;
            read.place = i;
            read.a = I(0, 1000);
            read.b = I(0, 1000);
            read.c = I(0, 1000);
            read.d = I(0, 1000);
            c.ops.insert(c.ops.begin(), read);
        }
    }
    return c;
}

uint8_t *HumanActor(int n)
{
    const auto &humans = W().humans;
    return humans.empty() ? nullptr : W().actors[size_t(humans[size_t(n) % humans.size()])].bytes;
}
// An item for the humans' finds: none, or one with a uid (+0x10 on both sides).
void SetItem(int n)
{
    static const int uids[] = {0, 7, 0x7FFF, 0xFFFF, 0x8001, 1234};
    if (n % 4 == 0) {
        g_item[g_side] = nullptr;
        return;
    }
    uint8_t *item = Block(0x40);
    Name(item, "item");
    Mut<int>(item, 0x10) = uids[n % 6];
    g_item[g_side] = item;
}

using T_EpInt = void(TC *)(void *, int);
using T_EpChunk = bool(TC *)(void *, void *);
using T_EpPtr = bool(TC *)(void *, void *);
using T_EpVoid = void(TC *)(void *, void *);
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}
std::string RunEpOp(const S_EpOp &op, int index)
{
    const bool o = g_side != 0;
    const int li = op.place < g_placeCount ? op.place : 0;
    uint8_t *b = g_place[g_side][li];
    C_ExplosivePlace *p = PortPlace(li);
    switch (op.kind) {
    case OP_READ: {
        // The entries (each now and then), in any order, a foreign one now and then (entered, not left: what is inside
        // it read as entries).
        S_Writer w;
        w.Begin(0xAE20);
        std::vector<int> ids;
        for (int id = 100; id <= 105; ++id)
            if ((op.a >> (id - 100)) & 1 || op.b % 5 == 0)
                ids.push_back(id);
        for (size_t k = ids.size(); k > 1; --k)
            std::swap(ids[k - 1], ids[size_t(op.c + int(k)) % k]);
        static const int texts[] = {0x1BF9, 0x300, 0, -1};
        static const int sounds[] = {-1, 20, 33}, effects[] = {-1, 4, 17}, damages[] = {0, 100, 2500, -3}, ranges[] = {0, 5, 12, 40};
        const int foreignAt = op.d % 3 == 0 ? op.d % int(ids.size() + 1) : -1;
        for (size_t k = 0; k <= ids.size(); ++k) {
            if (int(k) == foreignAt) {
                w.Begin(uint16_t(op.d % 2 ? 0x0099 : 0x1234));
                if (op.d % 4 == 0)
                    w.EntryOf(102, int(DELAYS[op.b % 6]));
                w.End();
            }
            if (k == ids.size())
                break;
            const int id = ids[k];
            w.Begin(uint16_t(id));
            switch (id) {
            case 100: {
                const int text = texts[op.b % 4];
                const uint8_t timed = uint8_t(op.c % 4 != 0);
                w.Raw(&text, 4);
                w.Raw(&timed, 1);
                break;
            }
            case 101: {
                const int item = ITEM_IDS[op.d % 4];
                w.Raw(&item, 4);
                break;
            }
            case 102: {
                const int delay = DELAYS[op.c % 6];
                w.Raw(&delay, 4);
                break;
            }
            case 103: {
                const int v[4] = {sounds[op.a % 3], effects[op.b % 3], damages[op.c % 4], ranges[op.d % 4]};
                w.Raw(v, 16);
                break;
            }
            case 104: {
                const uint8_t f = uint8_t((op.a / 7) % 3 == 0);
                w.Raw(&f, 1);
                break;
            }
            default: {
                const uint8_t f = uint8_t((op.b / 7) % 4 == 0);
                w.Raw(&f, 1);
                break;
            }
            }
            w.End();
        }
        w.End();
        const std::string name = F("ep_props%d.bin", index);
        g_overrides[Lower(name)] = w.out;
        C_chunk chunk;
        chunk.Open(name.c_str(), false);
        uint16_t id;
        chunk.Descend(id);
        const bool r = o ? OriginalAt<T_EpChunk>(O_EP_READ)(b, &chunk) : p->ReadProperties(chunk);
        chunk.Close();
        return F("ReadProperties %d -> %d ", li, int(r)) + Hex(w.out);
    }
    case OP_WRITE: {
        const std::string file = F("ep_write%d.bin", index);
        C_chunk chunk;
        chunk.Open(file.c_str(), true);
        chunk.Begin(0xAE20);
        const bool r = o ? OriginalAt<T_EpChunk>(O_EP_WRITE)(b, &chunk) : p->WriteProperties(chunk);
        chunk.Ascend();
        // (An entry after: where the properties left the writer.)
        chunk.Begin(0x5555);
        chunk.Write(int(1));
        chunk.Ascend();
        chunk.Close();
        std::string bytes;
        FileText(file, bytes);
        return F("WriteProperties %d -> %d ", li, int(r)) + Hex(bytes);
    }
    case OP_COPY: {
        const int from = (li + (op.a & 1)) % g_placeCount;
        uint8_t *src = g_place[g_side][from];
        if (o)
            OriginalAt<T_EpVoid>(O_EP_COPY)(b, src);
        else
            p->CopyFrom(reinterpret_cast<C_Actor *>(src));
        return F("CopyFrom %d %d", li, from);
    }
    case OP_START: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_EpInt>(O_EP_START)(b, server);
        else
            p->StartMission(server);
        return F("StartMission %d %d", li, int(server));
    }
    case OP_STOP: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_EpInt>(O_EP_STOP)(b, server);
        else
            p->StopMission(server);
        return F("StopMission %d %d", li, int(server));
    }
    case OP_RESTART:
        if (o)
            OriginalAt<T_EpInt>(O_EP_RESTART)(b, op.c & 1);
        else
            p->RestartMission(op.c & 1);
        return F("RestartMission %d", li);
    case OP_SET_STATE: {
        const int state = op.a % 7 == 6 ? 5 : op.a % 6 == 5 ? -1 : op.a % 5;
        if (o)
            OriginalAt<T_EpInt>(O_EP_SET_STATE)(b, state);
        else
            p->SetState(state);
        return F("SetState %d %d", li, state);
    }
    case OP_PLACE: {
        uint8_t *human = HumanActor(op.b);
        if (!human)
            return "Place (no human)";
        SetItem(op.c);
        const bool r = o ? OriginalAt<T_EpPtr>(O_EP_PLACE)(b, human) : p->Place(reinterpret_cast<C_Human *>(human));
        return F("Place %d %s -> %d", li, NameOf(Ptr(human)).c_str(), int(r));
    }
    case OP_DISARM: {
        uint8_t *human = op.b & 1 ? HumanActor(op.b) : nullptr;
        const bool r = o ? OriginalAt<T_EpPtr>(O_EP_DISARM)(b, human) : p->Disarm(reinterpret_cast<C_Human *>(human));
        return F("Disarm %d -> %d", li, int(r));
    }
    case OP_TICK: {
        const int t = FRAME_TIMES[op.a % 12];
        if (o)
            OriginalAt<T_EpInt>(O_EP_TICK)(b, t);
        else
            p->Tick(t);
        return F("Tick %d %d", li, t);
    }
    case OP_UPDATE: {
        const int t = FRAME_TIMES[op.a % 12];
        if (o)
            OriginalAt<T_EpInt>(O_EP_UPDATE)(b, t);
        else
            p->Update(t);
        return F("Update %d %d", li, t);
    }
    case OP_MESSAGE: {
        // The kind, then its words: a human's id (the world's, none, past the list) and an item's uid; a state.
        const uint32_t kind = uint32_t(op.a % 4);
        g_bits[g_side].clear();
        g_bits[g_side].push_back(kind);
        if (kind == 2) {
            const int n = int(W().list.size());
            const uint32_t id = op.b % 5 == 0 ? 1023u : uint32_t(op.b % (n + 2));
            g_bits[g_side].push_back(id);
            static const uint32_t uids[] = {7, 0xFFFF, 0x8001, 0};
            g_bits[g_side].push_back(uids[op.c % 4]);
            SetItem(op.d);
        } else if (kind == 3) {
            g_bits[g_side].push_back(uint32_t(op.b % 4));
        }
        if (o)
            OriginalAt<T_EpVoid>(O_EP_MESSAGE)(b, W().bitstream);
        else
            p->OnMessage(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("OnMessage %d %u", li, kind);
    }
    case OP_NET_STATE: {
        if (op.a & 1) {
            if (o)
                OriginalAt<T_EpVoid>(O_EP_WRITE_STATE)(b, W().bitstream);
            else
                p->WriteState(reinterpret_cast<C_BitStream *>(W().bitstream));
            return F("WriteState %d", li);
        }
        g_bits[g_side].clear();
        g_bits[g_side].push_back(uint32_t(op.b % 4));
        if (o)
            OriginalAt<T_EpVoid>(O_EP_READ_STATE)(b, W().bitstream);
        else
            p->ReadState(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("ReadState %d %d", li, op.b % 4);
    }
    case OP_SAVE_LOAD: {
        const std::string file = F("ep_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_EpChunk>(O_EP_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        // (Loaded back now and then (into this one or the other): its file, or a made one: other states (5 and -1 among
        // them), times and planters, a foreign entry (entered, not left), two states. Not a
        // short state: the original keeps a stack word.)
        bool rl = false;
        const int load = op.c % 4;
        uint8_t *into = g_place[g_side][(li + (op.d & 1)) % g_placeCount];
        unsigned next = 0;
        if (load) {
            std::string name = file;
            if (load >= 2) {
                S_Writer w;
                w.Begin(0x2000);
                // (A foreign entry: empty, or holding a state entry. Not one holding anything else: the original's
                // reader runs past its end and loops for ever, the port's stops.)
                if (load == 3 && op.b % 3 == 0) {
                    w.Begin(0x0009);
                    if (op.d & 2) {
                        w.Begin(3000);
                        const int v[3] = {op.d % 6 - 1, op.b % 3000, op.c % 3 - 1};
                        w.Raw(v, 12);
                        w.End();
                    }
                    w.End();
                }
                static const int states[] = {0, 1, 2, 3, 4, 5, -1, 1};
                static const int lefts[] = {0, 1, 1500, 2000, -4, 30000};
                const int n = int(W().list.size());
                const int planters[] = {-1, 0, op.d % (n + 1), n + 3, 1};
                const int rounds = op.a % 7 == 0 ? 2 : 1;
                for (int k = 0; k < rounds; ++k) {
                    w.Begin(3000);
                    const int v[3] = {states[(op.a + k) % 8], lefts[(op.b + k) % 6], planters[(op.d + k) % 5]};
                    w.Raw(v, 12);
                    w.End();
                }
                w.End();
                name = F("ep_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_EpChunk>(O_EP_LOAD)(into, &chunk)
                   : reinterpret_cast<C_ExplosivePlace *>(into)->LoadGame(chunk);
            chunk.Ascend();
            uint16_t after = 0;
            chunk.Descend(after);
            next = after;
            chunk.Close();
        }
        return F("SaveGame %d -> %d LoadGame %d into %s -> %d next %04x bytes ", li, int(rs), load, NameOf(Ptr(into)).c_str(),
                 int(rl), next) +
               Hex(bytes);
    }
    default: {
        // Its frame, its visual, one of the state frames. (Not none: GetFrameMaterialId reads through null.)
        uint8_t *frame = g_placeFrame[g_side][li];
        if (op.b % 3 == 1 && g_placeVisual[g_side][li])
            frame = g_placeVisual[g_side][li];
        else if (op.b % 3 == 2 && AsFrame(frame)->m_firstChild)
            frame = reinterpret_cast<uint8_t *>(AsFrame(frame)->m_firstChild);
        const int r = o ? OriginalAt<int(TC *)(void *, void *)>(O_EP_MATERIAL)(b, frame) : p->GetMaterialId(AsFrame(frame));
        return F("GetMaterialId %d %s -> %d", li, NameOf(Ptr(frame)).c_str(), r);
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
            std::printf("ep_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_epShow;
void RunEpCase(const S_EpCase &c)
{
    g_caseStart = GetTickCount();
    ++g_epCases;
    g_holdFrees = true;
    g_current = F("case %d", g_epCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_placeCount = int(c.places.size());
    g_visualType.clear();
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        g_bits[side].clear();
        g_item[side] = nullptr;
        BuildWorld(side, worldSeed);
        MakeItemDefs();
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        for (int i = 0; i < g_placeCount; ++i) {
            const S_EpPlace &lc = c.places[size_t(i)];
            uint8_t *frame = Child(sector, FRAME_MODEL, F("place%d", i), nullptr);
            S_matrix m;
            m.Identity();
            const float ch = std::cos(lc.heading), sh = std::sin(lc.heading), ct = std::cos(lc.tilt), st = std::sin(lc.tilt);
            m.m[0][0] = ch;
            m.m[0][2] = -sh;
            m.m[1][1] = ct;
            m.m[1][2] = st;
            m.m[2][0] = sh * ct;
            m.m[2][1] = -st;
            m.m[2][2] = ch * ct;
            if (lc.odd) {
                static const float rows[4][3] = {{1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1e-5f}};
                for (int k = 0; k < 3; ++k)
                    m.m[2][k] = rows[lc.odd - 1][k];
            }
            for (int k = 0; k < 3; ++k) {
                m.m[0][k] *= lc.scale.x;
                m.m[1][k] *= lc.scale.y;
                m.m[2][k] *= lc.scale.z;
            }
            m.m[3][0] = lc.x;
            m.m[3][1] = lc.y;
            m.m[3][2] = lc.z;
            SetLocal(frame, m, lc.scale);
            AsFrame(frame)->m_userProps = Text(lc.props);
            g_placeVisual[side][i] = nullptr;
            // (The visuals: "explosive_on" and the mesh with geometry, now and then with their bounds out of date.)
            if (lc.on) {
                uint8_t *on = Child(frame, FRAME_VISUAL, "explosive_on", frame);
                MeshBound(on, lc.box + 1, lc.stale & 1);
                AsFrame(on)->m_localMatrix.m[3][1] = 0.25f;
            }
            if (lc.off)
                Child(frame, FRAME_VISUAL, "explosive_off", frame);
            if (lc.visual) {
                uint8_t *visual = Child(frame, FRAME_VISUAL, "place mesh", frame);
                AsFrame(visual)->m_userProps = Text(lc.props.empty() ? "matid 3" : "");
                MeshBound(visual, lc.box, lc.stale & 2);
                AsFrame(visual)->m_localMatrix.m[3][0] = -0.5f;
                AsFrame(visual)->m_localMatrix.m[3][2] = 0.75f;
                g_placeVisual[side][i] = visual;
            }
            g_placeFrame[side][i] = frame;
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        for (int i = 0; i < g_placeCount; ++i) {
            uint8_t *b;
            if (side) {
                b = Block(0x180 + 16);
                OriginalAt<T_Void>(O_EP_CTOR)(b);
                // (The original's constructor leaves who placed it and the time left unset, the port's -1 and 0:
                // StartMission sets both before they are read.)
                Mut<int>(b, 0xC4) = -1;
                Mut<int>(b, 0xC8) = 0;
                Mut<uint8_t *>(b, 0x20) = g_placeFrame[1][i];
            } else {
                b = Block(sizeof(C_ExplosivePlace) + 16);
                new (b) C_ExplosivePlace();
                reinterpret_cast<C_ExplosivePlace *>(b)->m_frame = AsFrame(g_placeFrame[0][i]);
            }
            g_place[side][i] = b;
            Name(b, F("place %d", i));
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : PlacesState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_EP_TRACE") ? std::atoi(std::getenv("HD2_EP_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_epCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunEpOp(c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : PlacesState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_placeCount; ++i) {
            uint8_t *b = g_place[side][i];
            if (side) {
                OriginalAt<T_EpInt>(O_EP_STOP)(b, 1);
                OriginalAt<T_Void>(O_EP_DTOR)(b);
            } else {
                PortPlace(i)->StopMission(true);
                PortPlace(i)->~C_ExplosivePlace();
            }
        }
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(O_EP_CREATE)(OriginalAt<void *>(O_EP_CREATOR));
            Name(made, "created place");
            out.push_back(F("created type %d", At<int>(made, 0x18)));
            OriginalAt<void(TC *)(void *, void *)>(O_EP_DESTROY)(OriginalAt<void *>(O_EP_CREATOR), made);
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_EXPLOSIVE_PLACE];
            C_Actor *made = creator->Create();
            Name(made, "created place");
            out.push_back(F("created type %d", int(made->GetType())));
            creator->Destroy(made);
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        ClearItemDefs();
        for (int i = 0; i < MAX_PLACES; ++i)
            g_place[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_EP_ONLY") ? std::atoi(std::getenv("HD2_EP_ONLY")) : 0;
    if (g_epShow && (!only || only == g_epCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_epShow == 2)
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

void SetUpPlace()
{
    SetUpCmd();
    SetUpPlaceAnswers();
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
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("ep_diff: main");
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
        std::printf("ep_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_EP_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_EP_SHOW"))
        g_epShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_EP_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_EP_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpPlace();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunEpCase(RandomEpCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("ep_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_epCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
