// Part 27 of the human harness (HUMAN_NETSTATE builds, build_netstate.bat; included by human_test.cpp after
// locomotion3_test.inc): C_Human::WriteState and ReadState (game/human_net.cpp), the human's state sent to a joining
// client, against the original's (0x005a9c40, 0x005aa1b0) on the base harness's human.
//
// The bits go through each side's own bit stream code on port C_NetMessage objects over the test's buffers (the
// original's C_BitStream layout); the written words and bit positions are compared, and ReadState reads the same input
// on both sides. What the two functions call besides is logged alike: the port's calls through ld --wrap
// (netstate_wrap.rsp), the original's through redirects. The soldier state's serialisers stand in with a fixed format
// of their own (the health, 8 bits, and a 5-bit marker); the position is written and read as three raw floats. The
// vehicles are fakes of each side's layout and vtable (FindSeat, SetSeatBlocked logged).
S_NetPlayer *g_netPlayerTable = nullptr;                // (net/net_game.cpp's, not linked: GetSender is not reached)
int g_netPlayerCount = 0;
namespace {

#define NS __attribute__((thiscall))

struct S_NetScript {
    bool on = false;
    int health = 100;                                   // GetSoldierState's
    bool suspended = false, surrendered = false, unconscious = false;
    int seat = 0;                                       // FindSeat's answer
} g_ns;

void NLog(const void *self, const std::string &what, std::initializer_list<double> nums = {})
{
    if (g_ns.on)
        Log(self, what.c_str(), nums);
}

// ---- the vehicles: each side's layout (the port's C_Actor; the original's net id +0x10, type +0x18) and vtable
enum { NS_VEHICLES = 6 };
uint8_t *g_nsVehPort[NS_VEHICLES], *g_nsVehOrig[NS_VEHICLES];
void *g_nsPortVt[128], *g_nsOrigVt[96];
int NS NS_FindSeat(void *self, const void *occupant)
{
    NLog(self, "FindSeat " + NameOf(occupant));
    return g_ns.seat;
}
bool NS NS_SetSeatBlocked(void *self, int seat, bool blocked)
{
    NLog(self, "SetSeatBlocked", {double(seat), double(blocked)});
    return true;
}
void NS NS_UnexpectedVirtual(void *self)
{
    std::printf("unexpected virtual call on %s\n", NameOf(self).c_str());
    std::abort();
}
C_Actor *PortVehicle(int i) { return reinterpret_cast<C_Actor *>(g_nsVehPort[i]); }
// The vehicle with net id `id` (ids 3, 10, 17, ...), the other side's by index.
int VehicleIndex(int id)
{
    for (int i = 0; i < NS_VEHICLES; ++i)
        if (PortVehicle(i)->m_netId == id)
            return i;
    return -1;
}

// ---- the original's side
uint8_t g_nsSender[0x100];                              // GetSender's answer (nothing reads it: WriteQuantized is faked)
void *NS ON_GetSender(void *, int) { return g_nsSender; }
bool NS ON_WriteQuantized(C_BitStream *msg, const float *v, const void *, uint8_t mask)
{
    NLog(nullptr, "WritePosition", {v[0], v[1], v[2], double(mask)});
    for (int i = 0; i < 3; ++i) {
        uint32_t w;
        std::memcpy(&w, v + i, 4);
        msg->WriteBits(w, 32);
    }
    return true;
}
void NS ON_ReadPosition(C_BitStream *msg, float *v, int player, uint32_t mask)   // 0x005aa740
{
    for (int i = 0; i < 3; ++i) {
        const uint32_t w = msg->ReadBits(32);
        std::memcpy(v + i, &w, 4);
    }
    NLog(nullptr, "ReadPosition", {double(player), double(mask)});
}
void ReadPositionLogged(C_BitStream *msg, float *v) { ON_ReadPosition(msg, v, -1, 7); }
void NS ON_GetSoldierState(void *self, uint8_t *state)
{
    NLog(self, "GetSoldierState");
    std::memcpy(state + 0x0C, &g_ns.health, 4);
}
void __fastcall ON_NetWriteSoldierState(C_BitStream *msg, uint8_t *state, bool itemsOnly)
{
    int health;
    std::memcpy(&health, state + 0x0C, 4);
    NLog(nullptr, "NetWriteSoldierState", {double(health), double(itemsOnly)});
    msg->WriteBits(uint32_t(health) & 0xFF, 8);
    msg->WriteBits(0x15, 5);
}
void __fastcall ON_NetReadSoldierState(C_BitStream *msg, uint8_t *state, bool itemsOnly)
{
    const int health = int(int8_t(msg->ReadBits(8)));
    const uint32_t marker = msg->ReadBits(5);
    std::memcpy(state + 0x0C, &health, 4);
    NLog(nullptr, "NetReadSoldierState", {double(health), double(marker), double(itemsOnly)});
}
bool NS ON_IsAISuspended(void *self)
{
    NLog(self, "IsAISuspended");
    return g_ns.suspended;
}
bool NS ON_IsSurrendered(void *self)
{
    NLog(self, "IsSurrendered");
    return g_ns.surrendered;
}
bool NS ON_IsUnconscious(void *self)
{
    NLog(self, "IsUnconscious");
    return g_ns.unconscious;
}
void *NS ON_GetActorById(void *, int id)
{
    const int i = VehicleIndex(id);
    NLog(nullptr, "GetActorById", {double(id)});
    return i < 0 ? nullptr : g_nsVehOrig[i];
}
void NS ON_ExecuteBodyCommand(void *self, const uint8_t *cmd)
{
    int f[4];
    std::memcpy(f, cmd + 4, 16);
    void *arg2;
    std::memcpy(&arg2, cmd + 0x14, 4);
    NLog(self, "ExecuteBodyCommand " + NameOf(arg2), {double(f[0]), double(f[1]), double(f[2]), double(f[3])});
}
void NS ON_ApplyRemoteHit(void *self, int a, int b, int c, int d, const float *v1, const float *v2)
{
    NLog(self, "ApplyRemoteHit", {double(a), double(b), double(c), double(d), v1[0], v1[1], v1[2], v2[0], v2[1], v2[2]});
}
void NS ON_ClearActorContainers(void *, void *actor) { NLog(actor, "ClearActorContainers"); }
void NS ON_Setup(void *self, const uint8_t *state, int role, int group, const int *stats, int netId)
{
    int health;
    std::memcpy(&health, state + 0x0C, 4);
    NLog(self, "Setup", {double(health), double(role), double(group), double(stats != nullptr), double(netId)});
}
void NS ON_Freeze(void *self, bool on) { NLog(self, "Freeze", {double(on)}); }
void NS ON_SetTexture(void *self, const char *name) { NLog(self, std::string("SetTexture ") + (name ? name : "null")); }
void NS ON_PlayAnimation(void *self, const char *name, int a, int b, int c)
{
    NLog(self, std::string("PlayAnimation ") + (name ? name : "null"), {double(a), double(b), double(c)});
}
// The original's operator new and delete (its C_str copies): the test's heap (the original's CRT heap is not set up).
void *__cdecl ON_New(size_t size) { return std::malloc(size); }
void __cdecl ON_Delete(void *p) { std::free(p); }
void NS ON_RaiseWeapon(void *self) { NLog(self, "RaiseWeapon"); }
void NS ON_LowerWeapon(void *self) { NLog(self, "LowerWeapon"); }
}  // namespace

// ---- the port's side through ld --wrap
extern "C" {
void NS __wrap__ZN12C_NetMessage13WritePositionERK8S_vector(C_NetMessage *msg, const S_vector &p) { ON_WriteQuantized(msg, &p.x, nullptr, 7); }
void NS __wrap__ZN12C_NetMessage12ReadPositionER8S_vector(C_NetMessage *msg, S_vector &p) { ReadPositionLogged(msg, &p.x); }
void NS __wrap__ZNK7C_Human15GetSoldierStateER14S_SoldierState(const C_Human *h, S_SoldierState &s)
{
    ON_GetSoldierState(const_cast<C_Human *>(h), reinterpret_cast<uint8_t *>(&s));
}
void __wrap__Z20NetWriteSoldierStateP12C_NetMessageR14S_SoldierStateb(C_NetMessage *m, S_SoldierState &s, bool items)
{
    ON_NetWriteSoldierState(m, reinterpret_cast<uint8_t *>(&s), items);
}
void __wrap__Z19NetReadSoldierStateP12C_NetMessageR14S_SoldierStateb(C_NetMessage *m, S_SoldierState &s, bool items)
{
    ON_NetReadSoldierState(m, reinterpret_cast<uint8_t *>(&s), items);
}
bool NS __wrap__ZNK7C_Human13IsAISuspendedEv(const C_Human *h) { return ON_IsAISuspended(const_cast<C_Human *>(h)); }
bool NS __wrap__ZNK7C_Human13IsSurrenderedEv(const C_Human *h) { return ON_IsSurrendered(const_cast<C_Human *>(h)); }
bool NS __wrap__ZNK7C_Human13IsUnconsciousEv(const C_Human *h) { return ON_IsUnconscious(const_cast<C_Human *>(h)); }
C_Actor *NS __wrap__ZNK14C_ActorsModule12GetActorByIdEi(const void *, int id)
{
    const int i = VehicleIndex(id);
    NLog(nullptr, "GetActorById", {double(id)});
    return i < 0 ? nullptr : PortVehicle(i);
}
void NS __wrap__ZN7C_Human18ExecuteBodyCommandERK13S_BodyCommand(C_Human *h, const S_BodyCommand &c)
{
    ON_ExecuteBodyCommand(h, reinterpret_cast<const uint8_t *>(&c));
}
void NS __wrap__ZN7C_Human14ApplyRemoteHitEiiiiRK8S_vectorS2_(C_Human *h, int a, int b, int c, int d, const S_vector &v1, const S_vector &v2)
{
    ON_ApplyRemoteHit(h, a, b, c, d, &v1.x, &v2.x);
}
void __wrap__Z20ClearActorContainersP7C_Actor(C_Actor *a) { ON_ClearActorContainers(nullptr, a); }
void NS __wrap__ZN7C_Human5SetupER14S_SoldierStateiiPKii(C_Human *h, S_SoldierState &s, int role, int group, const int *stats, int netId)
{
    ON_Setup(h, reinterpret_cast<const uint8_t *>(&s), role, group, stats, netId);
}
void NS __wrap__ZN7C_Human6FreezeEb(C_Human *h, bool on) { ON_Freeze(h, on); }
void NS __wrap__ZN7C_Human10SetTextureEPKc(C_Human *h, const char *name) { ON_SetTexture(h, name); }
void NS __wrap__ZN7C_Human13PlayAnimationEPKciii(C_Human *h, const char *name, int a, int b, int c) { ON_PlayAnimation(h, name, a, b, c); }
void NS __wrap__ZN17C_HumanController11RaiseWeaponEv(C_HumanController *c) { ON_RaiseWeapon(c); }
void NS __wrap__ZN17C_HumanController11LowerWeaponEv(C_HumanController *c) { ON_LowerWeapon(c); }
}  // extern "C"

namespace {
// The human's controller: the port's (its class's layout) and the original's copy (the original's offsets).
alignas(16) uint8_t g_nsController[0x400], g_nsControllerOrig[0x400];

void InstallNetStateRedirects()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0073d770, reinterpret_cast<const void *>(ON_GetSender)},
        {0x0073d810, reinterpret_cast<const void *>(ON_WriteQuantized)},
        {0x005aa740, reinterpret_cast<const void *>(ON_ReadPosition)},
        {0x005a9040, reinterpret_cast<const void *>(ON_GetSoldierState)},
        {0x00727850, reinterpret_cast<const void *>(ON_NetWriteSoldierState)},
        {0x00727a30, reinterpret_cast<const void *>(ON_NetReadSoldierState)},
        {0x005a5d30, reinterpret_cast<const void *>(ON_IsAISuspended)},
        {0x005a6450, reinterpret_cast<const void *>(ON_IsSurrendered)},
        {0x005a6380, reinterpret_cast<const void *>(ON_IsUnconscious)},
        {0x00475d50, reinterpret_cast<const void *>(ON_GetActorById)},
        {0x005a45f0, reinterpret_cast<const void *>(ON_ExecuteBodyCommand)},
        {0x005a15d0, reinterpret_cast<const void *>(ON_ApplyRemoteHit)},
        {0x007de350, reinterpret_cast<const void *>(ON_ClearActorContainers)},
        {0x0059edc0, reinterpret_cast<const void *>(ON_Setup)},
        {0x005a5d50, reinterpret_cast<const void *>(ON_Freeze)},
        {0x005a87c0, reinterpret_cast<const void *>(ON_SetTexture)},
        {0x005a9c20, reinterpret_cast<const void *>(ON_PlayAnimation)},
        {0x0058c330, reinterpret_cast<const void *>(ON_RaiseWeapon)},
        {0x0058c400, reinterpret_cast<const void *>(ON_LowerWeapon)},
        {0x007e566f, reinterpret_cast<const void *>(ON_New)},
        {0x007e2120, reinterpret_cast<const void *>(ON_Delete)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    for (void *&slot : g_nsPortVt)
        slot = reinterpret_cast<void *>(NS_UnexpectedVirtual);
    for (void *&slot : g_nsOrigVt)
        slot = reinterpret_cast<void *>(NS_UnexpectedVirtual);
    g_nsPortVt[VSlot(&C_Actor::FindSeat)] = reinterpret_cast<void *>(NS_FindSeat);
    g_nsPortVt[VSlot(&C_Actor::SetSeatBlocked)] = reinterpret_cast<void *>(NS_SetSeatBlocked);
    g_nsOrigVt[0x7C / 4] = reinterpret_cast<void *>(NS_FindSeat);
    g_nsOrigVt[0x70 / 4] = reinterpret_cast<void *>(NS_SetSeatBlocked);
    // Vehicles of every kind the two functions tell apart, and others.
    static const E_ActorType TYPES[NS_VEHICLES] = {ACTOR_CAR, ACTOR_SUBMARINE, ACTOR_STATIC_WEAPON, ACTOR_JUNKERS88, ACTOR_DOOR, ACTOR_HUMAN};
    for (int i = 0; i < NS_VEHICLES; ++i) {
        g_nsVehPort[i] = new uint8_t[0x800]();
        g_nsVehOrig[i] = new uint8_t[0x800]();
        C_Actor *p = PortVehicle(i);
        *reinterpret_cast<void ***>(p) = g_nsPortVt;
        p->m_type = TYPES[i];
        p->m_netId = 3 + 7 * i;
        *reinterpret_cast<void ***>(g_nsVehOrig[i]) = g_nsOrigVt;
        const int type = TYPES[i], id = p->m_netId;
        std::memcpy(g_nsVehOrig[i] + 0x18, &type, 4);
        std::memcpy(g_nsVehOrig[i] + 0x10, &id, 4);
        g_names[g_nsVehPort[i]] = g_names[g_nsVehOrig[i]] = "vehicle" + std::to_string(i);
        g_extraPointers.push_back({g_nsVehPort[i], g_nsVehOrig[i]});
    }
    g_names[g_nsController] = g_names[g_nsControllerOrig] = "controller";
    g_extraPointers.push_back({g_nsController, g_nsControllerOrig});
}

// (Names of one character too; a face name longer than ReadState's 100-byte buffer: cut to 99.)
const char *const NS_NAMES[] = {"", "walk", "idle_smoke", "a_very_long_animation_name_for_the_script_x", "w"};
const char *const NS_FACES[] = {"", "face01.bmp", "FACE_ger_07.tga",
                                "faces/a_face_texture_name_much_longer_than_the_hundred_bytes_ReadState_reads_it_into_on_its_stack_x.bmp"};

// The human's state both functions read: its vehicle, role, health, position, face, controller (type, raised weapon,
// the scripted one's animation and frame) and what the faked functions answer.
void RandomNetState()
{
    C_Human *h = g_h.human;
    h->m_vehicle = P(0.4f) ? nullptr : PortVehicle(I(0, NS_VEHICLES - 1));
    h->m_role = static_cast<E_HumanRole>(P(0.3f) ? int(HUMAN_ROLE_PLAYER) : I(0, 4));
    h->m_group = I(0, 6);
    h->m_stats[STAT_HEALTH] = P(0.4f) ? I(-20, 0) : I(1, 100);
    h->m_position = {U(-500, 500), U(-50, 50), U(-500, 500), 0};
    h->m_faceTexture = NS_FACES[I(0, 3)];
    h->m_controller = reinterpret_cast<C_HumanController *>(g_nsController);
    auto *sc = reinterpret_cast<C_ScriptedController *>(g_nsController);
    static const int TYPES[] = {0, 1, 2, 3, 4, 5, 5, 5};
    sc->m_type = static_cast<E_HumanController>(TYPES[I(0, 7)]);
    sc->m_weaponRaised = P(0.5f);
    std::memset(sc->m_animation, 0, sizeof sc->m_animation);
    const char *anim = NS_NAMES[I(0, 4)];
    std::strncpy(sc->m_animation, anim, sizeof sc->m_animation - 1);
    sc->m_animationFrame = P(0.2f) ? I(-40000, 80000) : I(0, 300);
    // The original's copy (its layout: type +0x48, raised weapon +0xAD, animation +0x150, frame +0x188).
    std::memset(g_nsControllerOrig, 0, sizeof g_nsControllerOrig);
    const int type = sc->m_type;
    std::memcpy(g_nsControllerOrig + 0x48, &type, 4);
    g_nsControllerOrig[0xAD] = sc->m_weaponRaised;
    std::memcpy(g_nsControllerOrig + 0x150, sc->m_animation, sizeof sc->m_animation);
    std::memcpy(g_nsControllerOrig + 0x188, &sc->m_animationFrame, 4);
    g_ns.health = h->m_stats[STAT_HEALTH];
    g_ns.suspended = P(0.3f);
    g_ns.surrendered = P(0.3f);
    g_ns.unconscious = P(0.3f);
    g_ns.seat = I(-1, 40);
}

// The frame bytes ReadState writes inline (its position +0xB0.., flags +0xE0).
std::vector<uint8_t> FrameBytes()
{
    const uint8_t *f = reinterpret_cast<const uint8_t *>(g_h.frame);
    return std::vector<uint8_t>(f + 0xB0, f + 0xE4);
}
void PutFrameBytes(const std::vector<uint8_t> &b) { std::memcpy(reinterpret_cast<uint8_t *>(g_h.frame) + 0xB0, b.data(), b.size()); }

struct S_Stream {
    uint32_t words[256] = {};
    C_NetMessage msg;
    S_Stream() { msg.Attach(words, sizeof words * 8); }
};
std::string StreamText(const S_Stream &s)
{
    std::string t = std::to_string(s.msg.BitPosition()) + ":";
    for (uint32_t i = 0; i < (s.msg.BitPosition() + 31) / 32; ++i)
        t += " " + std::to_string(s.words[i]);
    return t;
}

void TestWriteState()
{
    std::printf("netstate: WriteState\n");
    for (int i = 0; i < 20000 / QUICK; ++i) {
        RandomNetState();
        S_Stream ps, os;
        g_ns.on = true;
        DiffCall("WriteState", [&] { g_h.human->C_Human::WriteState(&ps.msg); return 0; },
                 [&](void *) {
                     typedef void(NS * T)(void *, void *);
                     OriginalAt<T>(0x005a9c40)(o_human, &os.msg);
                     return 0;
                 });
        g_ns.on = false;
        const std::string p = StreamText(ps), o = StreamText(os);
        if (p != o)
            std::printf("  WriteState bits: port %s / original %s\n", p.c_str(), o.c_str());
        CHECK(p == o);
    }
}

// ReadState's input: what WriteState would send, with every field random (the seat always there, ReadState's way).
void RandomInput(C_BitStream &m)
{
    const bool inVehicle = P(0.6f);
    m.WriteBits(inVehicle, 1);
    if (inVehicle) {
        const int id = P(0.8f) ? 3 + 7 * I(0, NS_VEHICLES - 1) : I(0, 1023);
        m.WriteBits(uint32_t(id), 10);
        m.WriteBits(uint32_t(I(0, 31)), 5);
    }
    m.WriteBits(uint32_t(P(0.4f) ? I(-20, 0) : I(1, 100)) & 0xFF, 8);   // (the soldier state's stand-in)
    m.WriteBits(0x15, 5);
    // (The position: read only when the health is below 1, so these bits shift the rest otherwise.)
    if (P(0.5f))
        for (int k = 0; k < 3; ++k) {
            const float f = U(-500, 500);
            uint32_t w;
            std::memcpy(&w, &f, 4);
            m.WriteBits(w, 32);
        }
    m.WriteBits(P(0.5f), 1);                            // frozen
    const bool face = P(0.5f);
    m.WriteBits(face, 1);
    if (face)
        m.WriteString(NS_FACES[I(1, 3)]);
    const bool anim = P(0.5f);
    m.WriteBits(anim, 1);
    if (anim) {
        m.WriteString(NS_NAMES[I(0, 4)]);
        m.WriteBits(uint32_t(I(0, 0xFFFF)), 16);
    }
    for (int k = 0; k < 3; ++k)
        m.WriteBits(P(0.5f), 1);                        // raised, surrendered, unconscious
    for (int k = 0; k < 8; ++k)
        m.WriteBits(uint32_t(std::rand()), 32);         // (garbage after, as a longer message)
}

void TestReadState()
{
    std::printf("netstate: ReadState\n");
    for (int i = 0; i < 20000 / QUICK; ++i) {
        RandomNetState();
        uint32_t input[256] = {};
        {
            C_BitStream w(input, sizeof input * 8);
            RandomInput(w);
        }
        S_Stream ps, os;
        std::memcpy(ps.words, input, sizeof input);
        std::memcpy(os.words, input, sizeof input);
        const std::vector<uint8_t> frame0 = FrameBytes();
        std::vector<uint8_t> portFrame, origFrame;
        g_ns.on = true;
        DiffCall("ReadState",
                 [&] {
                     g_h.human->C_Human::ReadState(&ps.msg);
                     portFrame = FrameBytes();
                     PutFrameBytes(frame0);
                     return int(ps.msg.BitPosition());
                 },
                 [&](void *) {
                     typedef void(NS * T)(void *, void *);
                     OriginalAt<T>(0x005aa1b0)(o_human, &os.msg);
                     origFrame = FrameBytes();
                     PutFrameBytes(frame0);
                     return int(os.msg.BitPosition());
                 });
        g_ns.on = false;
        CHECK(portFrame == origFrame);
    }
}

void TestNetState()
{
    InstallNetStateRedirects();
    TestWriteState();
    TestReadState();
}
}  // namespace
