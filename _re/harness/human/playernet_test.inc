// Part 42 of the human harness (HUMAN_PLAYERNET builds, build_playernet.bat; included by human_test.cpp after
// playerwpn_test.inc): the player controller's network side (game/player_net.cpp) against the original's, on part 40's
// pair, part 41's weapons and part 33's net object block; part 34's remote controller fakes copied (the client's item
// moves, the movement's OnRemoteAction, the visual's SetDirection, the human store's and the net game's stats, the net
// object's queue of net messages and its tracks).

// ---- the port's side
void C_Human::ClientMoveItem(int uid, int from, int fromContainer, int toContainer, int slot)
{
    Log(this, "ClientMoveItem", {double(uid), double(from), double(fromContainer), double(toContainer), double(slot)});
}
void C_Human::ClientPickUpItem(int temp, int container) { Log(this, "ClientPickUpItem", {double(temp), double(container)}); }
void C_Human::ClientPutOnUniform(int uid) { Log(this, "ClientPutOnUniform", {double(uid)}); }
void C_Human::ClientTakeFromSupplyBox(int box, int index, int container, int uid)
{
    Log(this, "ClientTakeFromSupplyBox", {double(box), double(index), double(container), double(uid)});
}
void C_Human::ClientTransferItem(int uid, int container, int other, int otherUid, int otherContainer)
{
    Log(this, "ClientTransferItem", {double(uid), double(container), double(other), double(otherUid), double(otherContainer)});
}
void C_HumanMovement::OnRemoteAction(int request) { Log(this, "OnRemoteAction", {double(request)}); }
void C_HumanVisual::SetDirection(const S_vector4 &dir, float roll) { Log(this, "VisualSetDirection", {dir.x, dir.y, dir.z, roll}); }
namespace {
int g_pnStats[8] = {0, 0, 0, 0, 0, 3, 0, 0};            // GetPlayerStats' answer ([5] the level)
uint32_t g_pnTime = 100000;                             // timeGetTime
}  // namespace
void C_HumanStore::GetStats(int *stats, int index, int level) const
{
    Log(this, "GetStats", {double(index), double(level)});
    stats[0] = 77;
    stats[3] = 12 + level;
}
int *C_NetGame::GetPlayerStats(int slot)
{
    Log(nullptr, "GetPlayerStats", {double(slot)});
    return g_pnStats;
}
void C_NetGame::SetPlayerRank(int rank, int slot) { Log(nullptr, "SetPlayerRank", {double(rank), double(slot)}); }
namespace {
void *PopNetMessage(void *object, bool orig, bool outgoing);
}  // namespace
C_NetObjectMessage *C_NetObject::PopMessage(bool outgoing) { return static_cast<C_NetObjectMessage *>(PopNetMessage(this, false, outgoing)); }
// (The net messages' base virtuals in net code not linked: their vtables, player_net.cpp's, name them.)
bool C_NetObjectMessage::MarkSent(int) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_NetQuantized::SetReadQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_NetQuantized::SetWriteQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) { AiUnexpected(__PRETTY_FUNCTION__); }
// timeGetTime (the port's import slot: this build links no winmm; the original's 0x0080f3f8).
extern "C" DWORD __stdcall PnTimeGetTime()
{
    Log(nullptr, "timeGetTime");
    return g_pnTime;
}
extern "C" {
void *g_pnImpTimeGetTime asm("__imp__timeGetTime@0") = reinterpret_cast<void *>(PnTimeGetTime);
}

namespace {
// ---- the original's side
void AC PN_ClientMoveItem(void *self, int uid, int from, int fromContainer, int toContainer, int slot)
{
    Log(self, "ClientMoveItem", {double(uid), double(from), double(fromContainer), double(toContainer), double(slot)});
}
void AC PN_ClientPickUpItem(void *self, int temp, int container) { Log(self, "ClientPickUpItem", {double(temp), double(container)}); }
void AC PN_ClientPutOnUniform(void *self, int uid) { Log(self, "ClientPutOnUniform", {double(uid)}); }
void AC PN_ClientTakeFromSupplyBox(void *self, int box, int index, int container, int uid)
{
    Log(self, "ClientTakeFromSupplyBox", {double(box), double(index), double(container), double(uid)});
}
void AC PN_ClientTransferItem(void *self, int uid, int container, int other, int otherUid, int otherContainer)
{
    Log(self, "ClientTransferItem", {double(uid), double(container), double(other), double(otherUid), double(otherContainer)});
}
void AC PN_OnRemoteAction(void *self, int request) { Log(self, "OnRemoteAction", {double(request)}); }
void AC PN_GetStats(void *self, int *stats, int index, int level)
{
    Log(self, "GetStats", {double(index), double(level)});
    stats[0] = 77;
    stats[3] = 12 + level;
}
int *AC PN_GetPlayerStats(void *, int slot)
{
    Log(nullptr, "GetPlayerStats", {double(slot)});
    return g_pnStats;
}
void AC PN_SetPlayerRank(void *, int rank, int slot) { Log(nullptr, "SetPlayerRank", {double(rank), double(slot)}); }
int AC PN_UseObject(void *self, void *object)
{
    Log(self, ("UseObject " + NameOf(object)).c_str());
    return 0;
}
void AC PN_SetDirection(void *self, const S_vector4 *dir, float roll) { Log(self, "VisualSetDirection", {dir->x, dir->y, dir->z, roll}); }
alignas(16) uint8_t g_pnHumanStore[0x40];

// The net object: its Update (slot 2) logged; the property's position track answers GetCurrent and GetRate, the
// direction track GetCurrent, with the test's vectors; PopMessage hands out each side's queue of net messages (the
// same layout but the binoculars' flag, as part 34b's), each holding a reference so that neither side deletes it.
S_vector4 g_pnPos, g_pnDir, g_pnRate;
const S_vector4 &AC PN_GetCurrent(void *) { return g_pnPos; }
const S_vector4 &AC PN_GetRate(void *) { return g_pnRate; }
const S_vector4 &AC PN_GetDirection(void *) { return g_pnDir; }
void AC PN_Update(void *self, float seconds) { Log(self, "NetObjectUpdate", {seconds}); }
void *g_pnNetVt[16];
void AC PN_SetRounds(void *self, void *owner, int ammo, int rounds) { Log(self, ("WeaponSetRounds " + NameOf(owner)).c_str(), {double(ammo), double(rounds)}); }
std::vector<uint8_t *> g_pnPortQueue, g_pnOrigQueue, g_pnAll;
void *PopNetMessage(void *object, bool orig, bool outgoing)
{
    Log(object, "PopMessage", {double(outgoing)});
    std::vector<uint8_t *> &q = orig ? g_pnOrigQueue : g_pnPortQueue;
    if (q.empty())
        return nullptr;
    uint8_t *m = q.front();
    q.erase(q.begin());
    return m;
}
void *AC PN_PopMessage(void *self, int outgoing) { return PopNetMessage(self, true, (outgoing & 0xFF) != 0); }
// (The port's messages carry its vtables: ReleaseNetMessage reads the reference count only.)
void RandomNetMessages()
{
    g_pnPortQueue.clear();
    g_pnOrigQueue.clear();
    for (int k = P(0.3f) ? 0 : I(1, 3); k > 0; --k) {
        uint8_t *pm = static_cast<uint8_t *>(_aligned_malloc(0x80, 16)), *om = static_cast<uint8_t *>(_aligned_malloc(0x80, 16));
        std::memset(pm, 0, 0x80);
        const int type = P(0.6f) ? NET_MSG_ACTION : P(0.5f) ? NET_MSG_DIVE : NET_MSG_BINOCULARS;
        std::memcpy(pm + 0x1C, &type, 4);
        const int refs = 1;
        std::memcpy(pm + 0x24, &refs, 4);
        pm[0x2C] = uint8_t(P(0.5f));                    // fromClient (ProcessNetMessages sets it)
        if (type == NET_MSG_ACTION) {
            const float f[8] = {U(-1, 1), U(-1, 1), U(-1, 1), 0, U(-50, 50), U(-5, 5), U(-50, 50), 1};
            std::memcpy(pm + 0x30, f, 32);
            const int interval = I(0, 30), ammo = I(-1, 500), action = I(0, 18);
            const float strength = U(0, 1);
            std::memcpy(pm + 0x50, &interval, 4);
            std::memcpy(pm + 0x54, &strength, 4);
            std::memcpy(pm + 0x58, &ammo, 4);
            pm[0x5C] = uint8_t(I(0, 255));
            std::memcpy(pm + 0x60, &action, 4);
        }
        std::memcpy(om, pm, 0x80);
        if (type == NET_MSG_BINOCULARS)
            pm[offsetof(C_NetBinocularsMessage, on)] = om[0x30] = uint8_t(P(0.5f));
        g_pnAll.push_back(pm);
        g_pnAll.push_back(om);
        g_pnPortQueue.push_back(pm);
        g_pnOrigQueue.push_back(om);
    }
}
bool IsTestMessage(const void *m) { return std::find(g_pnAll.begin(), g_pnAll.end(), m) != g_pnAll.end(); }
void FreeNetMessages()
{
    for (uint8_t *m : g_pnAll)
        _aligned_free(m);
    g_pnAll.clear();
}
DWORD __stdcall PN_TimeGetTime()
{
    Log(nullptr, "timeGetTime");
    return g_pnTime;
}

LONG CALLBACK PnFirstChance(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == 0xC0000005) {
        const CONTEXT *c = e->ContextRecord;
        std::printf("{first chance AV at %p: eax %08lx ecx %08lx edx %08lx esi %08lx edi %08lx esp %08lx, reading %p}\n",
                    e->ExceptionRecord->ExceptionAddress, c->Eax, c->Ecx, c->Edx, c->Esi, c->Edi, c->Esp,
                    reinterpret_cast<void *>(e->ExceptionRecord->ExceptionInformation[1]));
        const uint32_t *sp = reinterpret_cast<const uint32_t *>(c->Esp);
        for (int k = 0; k < 24; ++k)
            std::printf("%08x ", sp[k]);
        std::printf("\n");
        std::fflush(stdout);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void SetUpPlayerNet()
{
    if (std::getenv("PN_DEBUG"))
        AddVectoredExceptionHandler(1, PnFirstChance);
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005a2e10, FAKE(PN_ClientMoveItem)},
        {0x005a3a40, FAKE(PN_ClientPickUpItem)},
        {0x005a6a00, FAKE(PN_ClientPutOnUniform)},
        {0x005a38f0, FAKE(PN_ClientTakeFromSupplyBox)},
        {0x005a2f50, FAKE(PN_ClientTransferItem)},
        {0x00786b50, FAKE(PN_OnRemoteAction)},
        {0x006a1f80, FAKE(PN_GetStats)},
        {0x0072f890, FAKE(PN_GetPlayerStats)},
        {0x0072fe90, FAKE(PN_SetPlayerRank)},
        {0x0073e9d0, FAKE(PN_PopMessage)},
        {0x0075a520, FAKE(PN_SetDirection)},
        {0x004d9050, FAKE(OV_CarApplyControls)},   // (part 23's, set only in its own test)
        {0x005e0dd0, FAKE(OV_SubmarineSetControls)},
        {0x00786600, FAKE(PN_UseObject)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    *OriginalAt<void **>(0x0080f3f8) = reinterpret_cast<void *>(PN_TimeGetTime);
    // The human store: the profile's (the port's member, the original's 0x008aea1c).
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_humanStore = reinterpret_cast<C_HumanStore *>(g_pnHumanStore);
    *OriginalAt<void **>(0x008aea1c) = g_pnHumanStore;
    g_names[g_pnHumanStore] = "humanStore";
    g_portTrackVt[VSlot(&C_NetTrack::GetCurrent)] = reinterpret_cast<void *>(PN_GetCurrent);
    g_portTrackVt[VSlot(&C_NetTrack::GetRate)] = reinterpret_cast<void *>(PN_GetRate);
    g_origTrackVt[0x28 / 4] = reinterpret_cast<void *>(PN_GetCurrent);
    g_origTrackVt[0x34 / 4] = reinterpret_cast<void *>(PN_GetRate);
    g_aiDirTrackVtPort[VSlot(&C_NetDirTrack::GetCurrent)] = reinterpret_cast<void *>(PN_GetDirection);
    g_aiDirTrackVtOrig[0x28 / 4] = reinterpret_cast<void *>(PN_GetDirection);
    for (void *&slot : g_pnNetVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_pnNetVt[2] = reinterpret_cast<void *>(PN_Update);
    g_weaponPortVt[VSlot(&C_Item::SetRounds)] = reinterpret_cast<void *>(PN_SetRounds);
    g_weaponOrigVt[0x14 / 4] = reinterpret_cast<void *>(PN_SetRounds);
    g_aiTestMessage = IsTestMessage;
    // The replication's local object (+0x30; TickLocal updates it): the same logging vtable in the port's (part 22's
    // block) and the original's (part 33's).
    void *vt = g_pnNetVt;
    for (uint8_t *replication : {reinterpret_cast<uint8_t *>(g_netReplication), g_aiReplication}) {
        std::memcpy(replication + 0x30, &vt, 4);
        g_names[replication + 0x30] = "localObject";
    }
}

// The state for part 42: part 41's; a network role more often; the replicated values; the net messages; the time.
void RandomPlayerNet()
{
    RandomPlayerWeapons();
    C_PlayerController *p = g_pp;
    if (P(0.6f))
        g_gameRole = P(0.5f) ? GAME_ROLE_CLIENT : GAME_ROLE_SERVER;
    p->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
    g_pnPos = P(0.1f) ? S_vector4{0, 0, 0, 0} : S_vector4{U(-200, 200), U(-5, 5), U(-200, 200), 1};
    g_pnDir = AiDir();
    g_pnRate = AiDir();
    g_pnTime = uint32_t(I(0, 1000000));
    g_pnStats[5] = I(0, 6);
    // The weapons' shot intervals (the action message's: the port's offset found by GetShotInterval, the original's
    // +0x68).
    static size_t intervalAt = 0;
    for (size_t at = 4; at + 4 <= 0x100 && !intervalAt; at += 4) {
        int keep, marker = 0x13572468;
        std::memcpy(&keep, g_weaponPort[0] + at, 4);
        std::memcpy(g_weaponPort[0] + at, &marker, 4);
        if (reinterpret_cast<const C_WeaponItem *>(g_weaponPort[0])->GetShotInterval() == marker)
            intervalAt = at;
        std::memcpy(g_weaponPort[0] + at, &keep, 4);
    }
    for (int k = 0; k < 2; ++k) {
        const int interval = I(0, 3000);
        std::memcpy(g_weaponPort[k] + intervalAt, &interval, 4);
        std::memcpy(g_weaponOrig[k] + 0x68, &interval, 4);
    }
    RandomNetMessages();
}

void TestPlayerNetA()
{
    std::printf("player net: replication, net messages, server actions\n");
    for (int i = 0; i < 12000; ++i) {
        RandomPlayerNet();
        C_PlayerController *p = g_pp;
        const int dt = I(0, 200);
        // (Messages queued: both replications flush them.)
        if (P(0.3f))
            RandomAiQueue(p->m_toClients, g_op + 0x90);
        if (P(0.3f))
            RandomAiQueue(p->m_toServer, g_op + 0x80);
        switch (I(0, 3)) {
        case 0:
            PwCall("UpdateRemote",
                   [&] {
                       *reinterpret_cast<void ***>(g_netObjectBytes) = g_pnNetVt;
                       p->UpdateRemote(dt);
                       return 0;
                   },
                   [&](void *) {
                       *reinterpret_cast<void ***>(g_netObjectBytes) = g_pnNetVt;
                       OPl<void>(0x0053f520, dt);
                       return 0;
                   });
            break;
        case 1:
            // (The scope's view back with a hands weapon: the original reads its scope unchecked; the port checks.)
            // (The movement's velocity with a height now and then: the replicated rate's direction.)
            if (P(0.5f))
                g_h.movement->m_locomotion.m_velocity = {U(-3, 3), U(-2, 2), U(-3, 3), 0.0f};
            // (The breath at its edges: run out exactly, back to full exactly.)
            if (P(0.3f))
                p->m_breath = p->m_holdingBreath ? dt + I(-1, 2) : 3500 - dt + I(-2, 1);
            // (UpdateWeaponReady takes the hands item from the container first.)
            if (p->m_scopeDelay > 0 && p->m_scopeDelay - dt < 1) {
                g_handsWeapon = g_weaponPort[I(0, 1)];
                g_wornContainers[ITEM_CONTAINER_HANDS].item = static_cast<C_Item *>(g_handsWeapon);
                p->m_handsItem = static_cast<C_Item *>(g_handsWeapon);
            }
            PwCall("TickLocal",
                   [&] {
                       *reinterpret_cast<void ***>(g_netObjectBytes) = g_pnNetVt;
                       p->TickLocal(dt);
                       return 0;
                   },
                   [&](void *) {
                       *reinterpret_cast<void ***>(g_netObjectBytes) = g_pnNetVt;
                       OPl<void>(0x0053f9b0, dt);
                       return 0;
                   });
            break;
        case 2:
            p->m_buttons = g_buttons;
            p->m_axes = g_axes;
            PwCall("ProcessNetMessages",
                   [&] {
                       *reinterpret_cast<void ***>(g_netObjectBytes) = g_pnNetVt;
                       p->ProcessNetMessages();
                       return 0;
                   },
                   [&](void *) {
                       *reinterpret_cast<void ***>(g_netObjectBytes) = g_pnNetVt;
                       OPl<void>(0x005404f0);
                       return 0;
                   });
            break;
        default: {
            // A client's action (the port's message object and the original's bytes, the same fields).
            C_NetActionMessage *pm = new C_NetActionMessage;
            pm->dir = AiDir();
            pm->pos = {U(-50, 50), U(-5, 5), U(-50, 50), 1};
            pm->shotInterval = I(0, 30);
            // (The weapon's attack delay now and then the interval's exactly.)
            if (P(0.3f)) {
                const float delay = float(pm->shotInterval * 100);
                std::memcpy(g_weaponCompBytes + g_attackDelayAt, &delay, 4);
                std::memcpy(g_weaponCompBytes + 0x18, &delay, 4);
            }
            pm->strength = P(0.2f) ? 0.0f : U(0, 1);
            pm->ammoUid = I(-1, 500);
            pm->rounds = uint8_t(I(0, 255));
            pm->action = I(0, 18);
            alignas(16) uint8_t om[0x80];
            std::memcpy(om, static_cast<const void *>(pm), sizeof om);
            PwCall("ServerAction", [&] { return int(p->ServerAction(*pm)); },
                   [&](void *) { return int(OPl<uint32_t>(0x005441e0, static_cast<void *>(om)) & 0xFF); });
            delete pm;
            break;
        }
        }
        FreeNetMessages();
    }
}

// ---- the net object's messages and callbacks: the action's, the dive's and the binoculars' Write and Read, the factory,
// the control bits. Each side writes into its own stream (the same layout, part 33b's) and reads from the same bits; the
// sender's quantization (the stream's player, +0x2C) is the test's.
alignas(16) uint8_t g_pnQuant[0x80];
void SetUpPlayerNetMessages()
{
    auto *q = reinterpret_cast<S_NetQuantization *>(g_pnQuant);
    for (int i = 0; i < 3; ++i) {
        q->bits[i] = 16;
        q->offset[i] = -1000.0f;
        q->scale[i] = 65535.0f / 2000.0f;
    }
    g_names[g_pnQuant] = "quantization";
}
void StreamFor(S_AiStream &s, const uint32_t *words)
{
    std::memcpy(s.words, words, sizeof s.words);
    s.msg.Attach(s.words, sizeof s.words * 8);
    // (The sender: +0x2C in the original and an MSVC build, +0x28 in this g++ build, whose m_unreliable sits in the base's
    // tail padding; the other word is each side's unused recipient.)
    void *sender = g_pnQuant;
    std::memcpy(reinterpret_cast<uint8_t *>(&s.msg) + 0x28, &sender, 4);
    std::memcpy(reinterpret_cast<uint8_t *>(&s.msg) + 0x2C, &sender, 4);
}
// The streams after a call: their words and positions alike.
bool StreamsAgree(const S_AiStream &a, const S_AiStream &b)
{
    return std::memcmp(a.words, b.words, sizeof a.words) == 0 && a.msg.BitPosition() == b.msg.BitPosition();
}
// The action's fields (+0x30 .. +0x64).
void RandomAction(C_NetActionMessage &m)
{
    m.dir = AiDir();
    m.pos = {U(-900, 900), U(-50, 50), U(-900, 900), 1};
    m.shotInterval = P(0.2f) ? I(250, 400) : I(0, 30);
    m.strength = P(0.2f) ? 0.0f : P(0.1f) ? 1.0f : U(0, 1);
    m.ammoUid = I(-1, 500);
    m.rounds = uint8_t(I(0, 255));
    m.action = P(0.2f) ? CONTROLLER_ACTION_RELOAD : I(0, 18);
}
typedef void(AC *T_MsgCall)(void *, void *);
typedef void *(__attribute__((fastcall)) * T_Create)(void *, void *, int);
typedef void(__attribute__((fastcall)) * T_Bits)(void *, void *);

void TestPlayerNetB()
{
    std::printf("player net: the net messages, the factory, the control bits\n");
    for (int i = 0; i < 12000; ++i) {
        RandomPlayerNet();
        C_PlayerController *p = g_pp;
        uint32_t input[64];
        for (uint32_t &w : input)
            w = uint32_t(I(0, 0x7FFFFFFF)) * 2u + uint32_t(I(0, 1));
        S_AiStream ps, os;
        StreamFor(ps, input);
        StreamFor(os, input);
        switch (I(0, 8)) {
        case 0: {
            C_NetActionMessage pm;
            RandomAction(pm);
            alignas(16) uint8_t om[0x80];
            std::memcpy(om, static_cast<const void *>(&pm), sizeof om);
            PwCall("ActionWrite", [&] { pm.Write(&ps.msg); return int(ps.msg.BitPosition()); },
                   [&](void *) {
                       OriginalAt<T_MsgCall>(0x0053b370)(om, &os.msg);
                       return int(os.msg.BitPosition());
                   });
            CHECK(StreamsAgree(ps, os));
            break;
        }
        case 1: {
            C_NetActionMessage pm;
            RandomAction(pm);
            alignas(16) uint8_t om[0x80];
            std::memcpy(om, static_cast<const void *>(&pm), sizeof om);
            PwCall("ActionRead", [&] { pm.Read(&ps.msg); return int(ps.msg.BitPosition()); },
                   [&](void *) {
                       OriginalAt<T_MsgCall>(0x0053b510)(om, &os.msg);
                       return int(os.msg.BitPosition());
                   });
            const bool same = std::memcmp(reinterpret_cast<const uint8_t *>(&pm) + 0x30, om + 0x30, 0x34) == 0;
            CHECK(same);
            static int shown;
            if (!same && shown++ < 3)
                std::printf("  ActionRead: the fields differ\n");
            break;
        }
        case 2: {
            C_NetDiveMessage pm;
            alignas(16) uint8_t om[0x40];
            std::memcpy(om, static_cast<const void *>(&pm), sizeof om);
            PwCall("DiveWrite", [&] { pm.Write(&ps.msg); return int(ps.msg.BitPosition()); },
                   [&](void *) {
                       OriginalAt<T_MsgCall>(0x0053b6a0)(om, &os.msg);
                       return int(os.msg.BitPosition());
                   });
            CHECK(StreamsAgree(ps, os));
            break;
        }
        case 3:
        case 4: {
            C_NetBinocularsMessage pm;
            pm.on = P(0.5f);
            alignas(16) uint8_t om[0x40] = {};
            std::memcpy(om, static_cast<const void *>(&pm), 0x2C);
            om[0x30] = uint8_t(pm.on);
            if (P(0.5f)) {
                PwCall("BinocularsWrite", [&] { pm.Write(&ps.msg); return int(ps.msg.BitPosition()); },
                       [&](void *) {
                           OriginalAt<T_MsgCall>(0x0053b6f0)(om, &os.msg);
                           return int(os.msg.BitPosition());
                       });
                CHECK(StreamsAgree(ps, os));
            } else {
                PwCall("BinocularsRead", [&] { pm.Read(&ps.msg); return int(ps.msg.BitPosition()) * 2 + int(pm.on); },
                       [&](void *) {
                           OriginalAt<T_MsgCall>(0x0053b740)(om, &os.msg);
                           return int(os.msg.BitPosition()) * 2 + int(om[0x30]);
                       });
            }
            break;
        }
        case 5:
        case 6: {
            // The factory: writing its kinds' count, or reading a kind and making its message (the type compared; the
            // port's deleted, the original's left: its allocator is its CRT's).
            const bool write = P(0.3f);
            PwCall("NetCreateMessage",
                   [&] {
                       C_NetObjectMessage *m = NetCreateMessage(p, &ps.msg, write);
                       const int type = m ? m->type : -1;
                       delete m;
                       return type * 0x10000 + int(ps.msg.BitPosition());
                   },
                   [&](void *) {
                       const uint8_t *m = static_cast<const uint8_t *>(OriginalAt<T_Create>(0x0053b760)(g_op, &os.msg, write));
                       int type = -1;
                       if (m)
                           std::memcpy(&type, m + 0x1C, 4);
                       return type * 0x10000 + int(os.msg.BitPosition());
                   });
            CHECK(StreamsAgree(ps, os));
            break;
        }
        case 7:
            p->m_controlBits = uint32_t(I(0, 0x1FF));
            PwCall("NetWriteControlBits", [&] { NetWriteControlBits(p, &ps.msg); return int(ps.msg.BitPosition()); },
                   [&](void *) {
                       OriginalAt<T_Bits>(0x0053b990)(g_op, &os.msg);
                       return int(os.msg.BitPosition());
                   });
            CHECK(StreamsAgree(ps, os));
            break;
        default: {
            const bool none = P(0.2f);
            PwCall("NetReadControlBits", [&] { NetReadControlBits(none ? nullptr : p, &ps.msg); return int(ps.msg.BitPosition()); },
                   [&](void *) {
                       OriginalAt<T_Bits>(0x0053b970)(none ? nullptr : static_cast<void *>(g_op), &os.msg);
                       return int(os.msg.BitPosition());
                   });
            break;
        }
        }
        FreeNetMessages();
    }
}

// ---- part 42b: the requests (the controller messages to the server, the action message to the net object) and their
// sending (FlushMessages). The squad commander's OnPlayerNoise (the port's slot, the original's 0x24) is logged.
void AC PN_OnPlayerNoise(void *self) { Log(self, "OnPlayerNoise"); }
void SetUpPlayerNetRequests()
{
    g_plCommanderPortVt[VSlot(&C_SquadCommander::OnPlayerNoise)] = reinterpret_cast<void *>(PN_OnPlayerNoise);
    g_plCommanderOrigVt[0x24 / 4] = reinterpret_cast<void *>(PN_OnPlayerNoise);
}
template <class R, class... A> R OPn(uint32_t at, A... a) { return OPl<R>(at, a...); }
// The last message each side queued for the server (the original's vector +0x80), as raw bytes.
const uint8_t *LastToServer(bool original)
{
    if (!original)
        return g_pp->m_toServer.empty() ? nullptr : reinterpret_cast<const uint8_t *>(g_pp->m_toServer.back());
    void *const *first = *reinterpret_cast<void *const *const *>(g_op + 0x80 + 4);
    void *const *last = *reinterpret_cast<void *const *const *>(g_op + 0x80 + 8);
    return first && last != first ? static_cast<const uint8_t *>(last[-1]) : nullptr;
}
// Words of the two messages alike (port offset, original offset, count; pointers through ToOrig).
bool WordsAgree(const uint8_t *pm, const uint8_t *om, size_t portAt, size_t origAt, size_t words, bool pointers = false)
{
    for (size_t k = 0; k < words; ++k) {
        uint32_t a, b;
        std::memcpy(&a, pm + portAt + 4 * k, 4);
        std::memcpy(&b, om + origAt + 4 * k, 4);
        if (pointers)
            a = OrigWord(a);
        if (a != b)
            return false;
    }
    return true;
}
void CheckLastToServer(const char *what, bool same)
{
    CHECK(same);
    static std::map<std::string, int> shown;
    if (!same && shown[what]++ < 3)
        std::printf("  %s: the message's fields differ\n", what);
}

void TestPlayerNetRequests()
{
    std::printf("player net: requests, flushing\n");
    for (int i = 0; i < 12000; ++i) {
        RandomPlayerNet();
        C_PlayerController *p = g_pp;
        p->m_quietTime = P(0.3f) ? 500 + I(-1, 1) : I(0, 3000);
        switch (I(0, 14)) {
        case 0:
        case 1: {
            const int action = P(0.3f) ? CONTROLLER_ACTION_RELOAD : I(0, 18);
            const S_vector4 pos = {U(-50, 50), U(-5, 5), U(-50, 50), 1}, dir = AiDir();
            const float strength = P(0.3f) ? 0.0f : U(0, 1);
            const int ammo = I(-1, 500), rounds = I(0, 300);
            // (A reload in the sights or the scope reads the hands weapon's component: both unchecked.)
            if (action == CONTROLLER_ACTION_RELOAD && p->m_viewMode != C_PlayerController::VIEW_NORMAL && !p->m_handsItem)
                p->m_handsItem = reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
            PwCall("RequestAction", [&] { p->RequestAction(action, &pos, &dir, strength, ammo, rounds); return 0; },
                   [&](void *) { OPn<void>(0x00542920, action, &pos, &dir, strength, ammo, rounds); return 0; });
            break;
        }
        case 2: {
            S_HumanCommand c;
            c.category = I(0, 3);
            c.type = I(0, 0x26);
            c.param1 = I(-1, 120);
            c.param2 = I(0, 5);
            PwCall("IssueCommand", [&] { p->IssueCommand(c); return 0; }, [&](void *) { OPn<void>(0x0053d340, &c); return 0; });
            break;
        }
        case 3: {
            S_ActionCommand c;
            c.category = I(0, 5);
            c.dir = AiDir();
            c.action = I(0, 0x3D);
            c.command = I(-1, 0x26);
            c.actor = AiActor();
            c.value = I(0, 100);
            c.member = P(0.5f) ? nullptr : PortOther();
            alignas(16) uint8_t oc[0x40] = {};
            CopyMapped(reinterpret_cast<const uint8_t *>(&c) + 4, oc + 4, 0x30);
            PwCall("SendActionCommand", [&] { p->SendActionCommand(c); return 0; },
                   [&](void *) { OPn<void>(0x0053d910, static_cast<void *>(oc)); return 0; });
            // (Its fields: the position, the command's from its category to its member, the original's +0x10 and
            // +0x24; part 33's comparison knows the action only.)
            {
                const uint8_t *pm = LastToServer(false), *om = LastToServer(true);
                const size_t at = offsetof(S_MsgAction, command) + 4;
                CheckLastToServer("SendActionCommand", pm && om && WordsAgree(pm, om, offsetof(S_MsgAction, pos), 0x10, 4) &&
                                                           WordsAgree(pm, om, at, 0x24, 9) && WordsAgree(pm, om, at + 0x24, 0x48, 1, true) &&
                                                           WordsAgree(pm, om, at + 0x28, 0x4C, 1) && WordsAgree(pm, om, at + 0x2C, 0x50, 1, true));
            }
            break;
        }
        case 4: {
            const int uid = I(-1, 500);
            PwCall("OnUniformChanged", [&] { p->OnUniformChanged(uid); return 0; }, [&](void *) { OPn<void>(0x0053e0b0, uid); return 0; });
            break;
        }
        case 5: {
            const int box = I(-1, 120), index = I(-1, 20);
            PwCall("RequestTakeFromBox", [&] { p->RequestTakeFromBox(box, index); return 0; },
                   [&](void *) { OPn<void>(0x0053e270, box, index); return 0; });
            break;
        }
        case 6: {
            const int actor = I(-1, 120), container = I(0, 7);
            PwCall("RequestPickUpItem", [&] { p->RequestPickUpItem(actor, container); return 0; },
                   [&](void *) { OPn<void>(0x0053e440, actor, container); return 0; });
            break;
        }
        case 7: {
            const int a = I(-1, 500), b = I(-1, 120), c = I(0, 7), d = I(0, 7), e = I(-1, 15);
            PwCall("RequestMoveItem", [&] { return int(p->RequestMoveItem(a, b, c, d, e)); },
                   [&](void *) { return int(OPn<uint32_t>(0x0053e610, a, b, c, d, e) & 0xFF); });
            break;
        }
        case 8: {
            const int a = I(-1, 500), b = I(0, 7), c = I(-1, 120), d = I(-1, 500), e = I(0, 7);
            PwCall("RequestTransferItem", [&] { return int(p->RequestTransferItem(a, b, c, d, e)); },
                   [&](void *) { return int(OPn<uint32_t>(0x0053e7f0, a, b, c, d, e) & 0xFF); });
            break;
        }
        case 9: {
            const int uid = I(-1, 500);
            const S_vector4 v4 = AiDir();
            const S_vector v(v4.x * (P(0.5f) ? U(0, 20) : 1.0f), v4.y, v4.z);
            PwCall("RequestThrowItem", [&] { p->RequestThrowItem(uid, v); return 0; },
                   [&](void *) { OPn<void>(0x0053e9d0, uid, &v); return 0; });
            break;
        }
        case 10: {
            const int id = I(-1, 120);
            PwCall("RequestEquipment", [&] { p->RequestEquipment(id); return 0; }, [&](void *) { OPn<void>(0x0053f1b0, id); return 0; });
            break;
        }
        case 11: {
            const int type = I(0, 7), damage = I(0, 200), attacker = I(-1, 120), zone = I(0, 12);
            PwCall("RequestDamage", [&] { p->RequestDamage(type, damage, attacker, zone); return 0; },
                   [&](void *) { OPn<void>(0x00547270, type, damage, attacker, zone); return 0; });
            // (Its four values, +0x08 .. +0x14 on both sides.)
            {
                const uint8_t *pm = LastToServer(false), *om = LastToServer(true);
                CheckLastToServer("RequestDamage", pm && om && WordsAgree(pm, om, offsetof(S_Msg17, value08), 0x08, 4));
            }
            break;
        }
        case 12: {
            const int cls = I(0, 6);
            PwCall("SendSoldierClass", [&] { p->SendSoldierClass(cls); return 0; }, [&](void *) { OPn<void>(0x005470b0, cls); return 0; });
            break;
        }
        case 13: {
            // (A ladder now and then: part 23's switch typed as one for the call, the port's type at its offset, found
            // by GetType, and the original's +0x18.)
            C_Actor *other = P(0.4f) ? static_cast<C_Actor *>(PortOther()) : static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1)));
            const bool ladder = P(0.4f);
            static size_t typeAt = 0;
            for (size_t at = 4; at < 0x100 && !typeAt; at += 4) {
                int keep, marker = 0x77;
                std::memcpy(&keep, g_actPort[8] + at, 4);
                std::memcpy(g_actPort[8] + at, &marker, 4);
                if (int(reinterpret_cast<C_Actor *>(g_actPort[8])->GetType()) == marker)
                    typeAt = at;
                std::memcpy(g_actPort[8] + at, &keep, 4);
            }
            int keepPort, keepOrig;
            std::memcpy(&keepPort, g_actPort[8] + typeAt, 4);
            std::memcpy(&keepOrig, g_actOrig[8] + 0x18, 4);
            if (ladder) {
                other = static_cast<C_Actor *>(PortActor(8));
                const int type = ACTOR_LADDER;
                std::memcpy(g_actPort[8] + typeAt, &type, 4);
                std::memcpy(g_actOrig[8] + 0x18, &type, 4);
            }
            const S_vector pos(U(-5, 5), U(0, 2), U(-5, 5));
            PwCall("OnCollisionEx", [&] { return int(p->OnCollisionEx(other, &pos, 1, 2, 3, 4, 5)); },
                   [&](void *) { return int(OPn<uint32_t>(0x00544b40, ToOrig(other), &pos, 1, 2, 3, 4, 5) & 0xFF); });
            std::memcpy(g_actPort[8] + typeAt, &keepPort, 4);
            std::memcpy(g_actOrig[8] + 0x18, &keepOrig, 4);
            break;
        }
        default: {
            const bool toClients = P(0.5f);
            RandomAiQueue(toClients ? p->m_toClients : p->m_toServer, g_op + (toClients ? 0x90 : 0x80));
            PwCall("FlushMessages", [&] { p->FlushMessages(toClients); return 0; },
                   [&](void *) { OPn<void>(0x0053f370, toClients); return 0; });
            break;
        }
        }
        FreeNetMessages();
    }
}

// ---- part 42c: the controller messages read: OnMessage (a client: the server's about the human), OnMessage2 (the
// server: a client's requests), HandleMessage and HandleMessage2 (the same for a human not there: read and dropped).
// The input is part 33b's format (the messages' fake Reads), each handler's known types, now and then a type it does not
// read (both sides then read on out of step alike), the end.
enum { PN_ON_MESSAGE, PN_ON_MESSAGE2, PN_HANDLE, PN_HANDLE2 };
bool PnKnownType(int handler, int type)
{
    if (type > CTRL_MSG_23)
        return false;
    switch (handler) {
    // (Not the inventory check on a client: the original leaves its message's human unset, a stale value.)
    case PN_ON_MESSAGE:
        return type != 5 && type != CTRL_MSG_17 && type != CTRL_MSG_ANIMATION && type != CTRL_MSG_21 && type != CTRL_MSG_INVENTORY_CHECK;
    case PN_ON_MESSAGE2:
        return type != CTRL_MSG_HIT && type != 5 && type != CTRL_MSG_INVENTORY_CHECK && type != CTRL_MSG_HEAL &&
               type != CTRL_MSG_ANIMATION && type != CTRL_MSG_SAY && type != CTRL_MSG_EQUIPMENT;
    case PN_HANDLE: return type != 5 && type != CTRL_MSG_17 && type != CTRL_MSG_21 && type != CTRL_MSG_INVENTORY_CHECK;
    default: return type != 5 && type != CTRL_MSG_ANIMATION && type != CTRL_MSG_SAY && type != CTRL_MSG_EQUIPMENT;
    }
}
void RandomPlayerInput(C_BitStream &w, int handler)
{
    for (int k = I(0, 5); k > 0; --k) {
        int type = I(0, CTRL_MSG_23);
        // (Types the handler does not read only for the server's: a client's would read on out of step into the
        // inventory check, whose stale human differs.)
        const bool client = handler == PN_ON_MESSAGE || handler == PN_HANDLE;
        if (!PnKnownType(handler, type) && (client || P(0.8f)))
            continue;
        if (!client && P(0.02f))
            type = 25;
        w.WriteBits(uint32_t(type), 5);
        if (!PnKnownType(handler, type))
            continue;
        for (const S_AiMsgField &f : AiMsgFields(type)) {
            if (f.kind == 'f') {
                const float x = U(-50, 50);
                uint32_t b;
                std::memcpy(&b, &x, 4);
                w.WriteBits(b, 32);
            } else if (f.kind == 'b') {
                w.WriteBits(P(0.5f), 1);
            } else if (type == CTRL_MSG_17) {
                // (The damage request's real ranges: the type and the zone 4 bits, the amount and the attacker 10.)
                const size_t at = f.port;
                const uint32_t v = at == 8 || at == 0xC ? uint32_t(I(0, 15))
                                   : at == 0x10           ? uint32_t(I(0, 1023))
                                                          : uint32_t(P(0.3f) ? 100 + I(0, KIND_HUMAN) : I(0, 1023));
                w.WriteBits(v, 16);
            } else if (type == CTRL_MSG_ACTION) {
                // (A squad command within the speech tables: the original reads past them, the port has none.)
                w.WriteBits(uint32_t(I(0, 0x3D)), 16);
            } else {
                // (Ids of part 23's actors now and then: 100 + a kind; the hands' container now and then.)
                w.WriteBits(uint32_t(P(0.3f) ? 100 + I(0, KIND_HUMAN) : P(0.2f) ? ITEM_CONTAINER_HANDS : I(0, 0xFFFF)) & 0xFFFF, 16);
            }
        }
    }
    w.WriteBits(CTRL_MSG_END, 5);
}

void TestPlayerNetMessages()
{
    std::printf("player net: the controller messages read\n");
    for (int i = 0; i < 12000; ++i) {
        RandomPlayerNet();
        C_PlayerController *p = g_pp;
        g_h.human->m_speechSet = reinterpret_cast<C_SpeechSet *>(g_speechSetBytes2);
        g_cs.moved = P(0.7f);
        const int handler = I(0, 3);
        uint32_t input[128] = {};
        {
            C_BitStream w(input, sizeof input * 8);
            RandomPlayerInput(w, handler);
        }
        S_AiStream ps, os;
        std::memcpy(ps.words, input, sizeof ps.words);
        std::memcpy(os.words, ps.words, sizeof ps.words);
        ps.msg.Attach(ps.words, sizeof ps.words * 8);
        os.msg.Attach(os.words, sizeof os.words * 8);
        static const char *const names[] = {"OnMessage", "OnMessage2", "HandleMessage", "HandleMessage2"};
        typedef void(__attribute__((fastcall)) * T_Handle)(void *);
        PwCall(names[handler],
               [&] {
                   switch (handler) {
                   case PN_ON_MESSAGE: p->OnMessage(&ps.msg); break;
                   case PN_ON_MESSAGE2: p->OnMessage2(&ps.msg); break;
                   case PN_HANDLE: C_PlayerController::HandleMessage(&ps.msg); break;
                   default: C_PlayerController::HandleMessage2(&ps.msg); break;
                   }
                   return int(ps.msg.BitPosition());
               },
               [&](void *) {
                   switch (handler) {
                   case PN_ON_MESSAGE: OPn<void>(0x005406c0, &os.msg); break;
                   case PN_ON_MESSAGE2: OPn<void>(0x00540db0, &os.msg); break;
                   case PN_HANDLE: OriginalAt<T_Handle>(0x005418e0)(&os.msg); break;
                   default: OriginalAt<T_Handle>(0x00541cb0)(&os.msg); break;
                   }
                   return int(os.msg.BitPosition());
               });
        FreeNetMessages();
    }
}

void TestPlayerNet()
{
    SetUpPlayerController();
    SetUpPlayerSquad();
    SetUpPlayerMission();
    SetUpPlayerSlots();
    SetUpPlayerB();
    SetUpPlayerC();
    SetUpPlayerWeapons();
    SetUpPlayerWeaponsB();
    SetUpPlayerNet();
    TestPlayerNetA();
    SetUpPlayerNetMessages();
    TestPlayerNetB();
    SetUpPlayerNetRequests();
    TestPlayerNetRequests();
    TestPlayerNetMessages();
}
}  // namespace
