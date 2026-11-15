// Part 34 of the human harness (HUMAN_REMOTE builds, build_remote.bat; included by human_test.cpp after
// aictl_test.inc): C_RemoteController, a network client's copy of another player's human (game/remote_controller.cpp),
// against the original's, on part 33's harness: part 23's controllers, human and fakes, part 33b's message and stream
// fakes.
//
// Each side has its remote controller: the port's class (its constructor inline, as the original's in
// C_Human::CreateController) and the original's made by the base's constructor (0x00582ff0) with its vtable (0x008112d8)
// and its own fields cleared. The port's is copied into the original's layout before each call (the base by part 23's
// table; the control axes and buttons +0xC0 / +0x1C0, the VIP and surrender flags +0x2C0 / +0x2C1 in the original) and
// the two compared after it, with the queued messages and the net object's block (part 33a's way). What the controller
// calls on the human (the client's item moves, Freeze, PlayAnimation), its movement (OnRemoteAction), the net game (the
// player's stats and rank), the human store's stats and the net object's PopMessage is logged alike on both sides.
// GetSquadCommandGesture (player_controller.cpp, not linked) reads the original's table: the port's is the player
// controller's test's.
//
// Part 34a: Attach, Detach, OnBodyFlags, the requests, FlushMessages, OnMessage.

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
void C_Human::Freeze(bool on) { Log(this, "Freeze", {double(on)}); }
void C_Human::PlayAnimation(const char *name, int a, int b, int c) { Log(this, (std::string("PlayAnimation ") + name).c_str(), {double(a), double(b), double(c)}); }
void C_HumanMovement::OnRemoteAction(int request) { Log(this, "OnRemoteAction", {double(request)}); }
// (The movement's OnFrozen, part 4's, logged: wrapped here.)
extern "C" void __attribute__((thiscall)) __wrap__ZN15C_HumanMovement8OnFrozenEb(C_HumanMovement *m, bool on) { Log(m, "MovementOnFrozen", {double(on)}); }
void C_HumanVisual::SetDirection(const S_vector4 &dir, float roll) { Log(this, "VisualSetDirection", {dir.x, dir.y, dir.z, roll}); }
namespace {
int g_rmStats[8] = {0, 0, 0, 0, 0, 3, 0, 0};            // GetPlayerStats' answer ([5] the level)
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
    return g_rmStats;
}
void C_NetGame::SetPlayerRank(int rank, int slot) { Log(nullptr, "SetPlayerRank", {double(rank), double(slot)}); }
namespace {
void *PopRemoteMessage(void *object, bool orig, bool outgoing);   // (part 34b's queues)
}  // namespace
C_NetObjectMessage *C_NetObject::PopMessage(bool outgoing) { return static_cast<C_NetObjectMessage *>(PopRemoteMessage(this, false, outgoing)); }
void C_WeaponItem::PlayEmptySound(C_Actor *owner) { Log(this, "PlayEmptySound", {double(owner != nullptr)}); }
// (The original's table, 0x0083c358: the gesture per squad command, 0x4C none; commands 0x35 and 0x38 have none.)
bool GetSquadCommandGesture(int command, int *gesture)
{
    if (command == 0x35 || command == 0x38)
        return false;
    *gesture = OriginalAt<int *>(0x0083c358)[command * 2];
    return *gesture != 0x4C;
}

namespace {
// ---- the original's side
void AC OR_ClientMoveItem(void *self, int uid, int from, int fromContainer, int toContainer, int slot)
{
    Log(self, "ClientMoveItem", {double(uid), double(from), double(fromContainer), double(toContainer), double(slot)});
}
void AC OR_ClientPickUpItem(void *self, int temp, int container) { Log(self, "ClientPickUpItem", {double(temp), double(container)}); }
void AC OR_ClientPutOnUniform(void *self, int uid) { Log(self, "ClientPutOnUniform", {double(uid)}); }
void AC OR_ClientTakeFromSupplyBox(void *self, int box, int index, int container, int uid)
{
    Log(self, "ClientTakeFromSupplyBox", {double(box), double(index), double(container), double(uid)});
}
void AC OR_ClientTransferItem(void *self, int uid, int container, int other, int otherUid, int otherContainer)
{
    Log(self, "ClientTransferItem", {double(uid), double(container), double(other), double(otherUid), double(otherContainer)});
}
void AC OR_Freeze(void *self, int on) { Log(self, "Freeze", {double(on & 0xFF)}); }
// (The loop flag: the original pushes the word that holds the message's bool, its upper bytes as the stack had them;
// its low byte compared.)
void AC OR_PlayAnimation(void *self, const char *name, int a, int b, int c)
{
    Log(self, (std::string("PlayAnimation ") + name).c_str(), {double(a), double(b), double(c & 0xFF)});
}
int AC OR_UseObject(void *self, void *object)
{
    Log(self, ("UseObject " + NameOf(object)).c_str());
    return 0;
}
void AC OR_OnRemoteAction(void *self, int request) { Log(self, "OnRemoteAction", {double(request)}); }
void AC OR_GetStats(void *self, int *stats, int index, int level)
{
    Log(self, "GetStats", {double(index), double(level)});
    stats[0] = 77;
    stats[3] = 12 + level;
}
int *AC OR_GetPlayerStats(void *, int slot)
{
    Log(nullptr, "GetPlayerStats", {double(slot)});
    return g_rmStats;
}
void AC OR_SetPlayerRank(void *, int rank, int slot) { Log(nullptr, "SetPlayerRank", {double(rank), double(slot)}); }
alignas(16) uint8_t g_rmHumanStore[0x40];

// ---- the controllers
// (ExecuteCommand, part 23's, logged on both sides: the original's through a copy of its vtable.)
struct S_TestRemote : C_RemoteController {
    bool ExecuteCommand(const S_HumanCommand &c) override
    {
        LogCommand(this, c);
        return true;
    }
};
void *g_orVt[0x48];
C_RemoteController *g_pr;
uint8_t *g_or;                                          // (0x2D0 bytes)
const size_t kRmAxes = 0xC0, kRmButtons = 0x1C0, kRmVip = 0x2C0, kRmSurrendered = 0x2C1;
void RemoteToOrig(const C_RemoteController *r, uint8_t *o)
{
    ControllerToOrig(r, o);
    std::memcpy(o + kRmAxes, r->m_controlAxes, sizeof r->m_controlAxes);
    std::memcpy(o + kRmButtons, r->m_controlButtons, sizeof r->m_controlButtons);
    o[kRmVip] = r->m_vip;
    o[kRmSurrendered] = r->m_surrendered;
}
std::string RemoteDiffers(const C_RemoteController *r, const uint8_t *o)
{
    std::string why = ControllerDiffers(r, o);
    if (!why.empty())
        return why;
    if (std::memcmp(o + kRmAxes, r->m_controlAxes, sizeof r->m_controlAxes) != 0)
        return "control axes";
    if (std::memcmp(o + kRmButtons, r->m_controlButtons, sizeof r->m_controlButtons) != 0)
        return "control buttons";
    if (o[kRmVip] != uint8_t(r->m_vip) || o[kRmSurrendered] != uint8_t(r->m_surrendered))
        return "vip / surrendered";
    return "";
}
// (The human's net object, part 20's block: each side's writes kept apart, as part 33a's AiCall.)
template <class P, class O> void RemoteCall(const char *what, P port, O original)
{
    RemoteToOrig(g_pr, g_or);
    ControllerToOrig(g_pc2, g_oc2);
    std::memset(g_netObjectBytes, 0x5A, sizeof g_netObjectBytes);
    std::memcpy(g_aiNetPort, g_netObjectBytes, sizeof g_aiNetPort);
    DiffCall(what,
             [&] {
                 const int r = port();
                 std::memcpy(g_aiNetOrig, g_netObjectBytes, sizeof g_aiNetOrig);
                 std::memcpy(g_netObjectBytes, g_aiNetPort, sizeof g_aiNetPort);
                 std::memcpy(g_aiNetPort, g_aiNetOrig, sizeof g_aiNetPort);
                 return r;
             },
             [&](void *o) {
                 const int r = original(o);
                 std::memcpy(g_aiNetOrig, g_netObjectBytes, sizeof g_aiNetOrig);
                 return r;
             });
    std::string why = RemoteDiffers(g_pr, g_or);
    if (why.empty()) {
        static const struct {
            size_t at;
            const void *port;
            uint32_t orig;
        } callbacks[] = {{0x54, reinterpret_cast<const void *>(NetReadControlBits), 0x0053b970},
                         {0x58, reinterpret_cast<const void *>(NetWriteControlBits), 0x0053b990},
                         {0x64, reinterpret_cast<const void *>(NetCreateMessage), 0x0053b760}};
        for (size_t at = 0x38; at < 0x68 && why.empty(); at += 4) {
            uint32_t a, b;
            std::memcpy(&a, g_aiNetPort + at, 4);
            std::memcpy(&b, g_aiNetOrig + at, 4);
            a = OrigWord(a);
            for (const auto &c : callbacks)
                if (c.at == at && a == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.port)))
                    a = c.orig;
            if (at == 0x40)
                a &= 0xFF, b &= 0xFF;
            if (a != b)
                why = "netObject+" + std::to_string(at);
        }
    }
    CHECK(why.empty());
    static std::map<std::string, int> shown;
    if (!why.empty() && shown[what]++ < 3)
        std::printf("  %s: %s\n", what, why.c_str());
}
template <class R, class... A> R ORm(uint32_t at, A... a)
{
    using F = R(AC *)(void *, A...);
    return OriginalAt<F>(at)(static_cast<void *>(g_or), a...);
}

void SetUpRemote()
{
    SetUpAi();
    SetUpAiNetwork();
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005a2e10, FAKE(OR_ClientMoveItem)},
        {0x005a3a40, FAKE(OR_ClientPickUpItem)},
        {0x005a6a00, FAKE(OR_ClientPutOnUniform)},
        {0x005a38f0, FAKE(OR_ClientTakeFromSupplyBox)},
        {0x005a2f50, FAKE(OR_ClientTransferItem)},
        {0x005a5d50, FAKE(OR_Freeze)},
        {0x005a9c20, FAKE(OR_PlayAnimation)},
        {0x00786b50, FAKE(OR_OnRemoteAction)},
        {0x006a1f80, FAKE(OR_GetStats)},
        {0x0072f890, FAKE(OR_GetPlayerStats)},
        {0x0072fe90, FAKE(OR_SetPlayerRank)},
        {0x00786600, FAKE(OR_UseObject)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // The human store: the profile's (the port's member, the original's 0x008aea1c).
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_humanStore = reinterpret_cast<C_HumanStore *>(g_rmHumanStore);
    *OriginalAt<void **>(0x008aea1c) = g_rmHumanStore;
    g_names[g_rmHumanStore] = "humanStore";
    g_pr = new S_TestRemote;
    g_or = static_cast<uint8_t *>(OC_AlignedNew(0x2D0, 16));
    typedef void(AC * T_Ctor)(void *, int);
    OriginalAt<T_Ctor>(0x00582ff0)(g_or, HUMAN_CONTROLLER_REMOTE);
    std::memcpy(g_orVt, OriginalAt<void *>(0x008112d8), sizeof g_orVt);
    g_orVt[0xDC / 4] = reinterpret_cast<void *>(OA_ExecuteCommand);
    void *vt = g_orVt;
    std::memcpy(g_or, &vt, 4);
    std::memset(g_or + 0xC0, 0, 0x204);
    g_names[g_pr] = g_names[g_or] = "remote";
    g_extraPointers.push_back({g_pr, g_or});
    // (Update points m_axes and m_buttons at the controller's own arrays.)
    g_extraPointers.push_back({g_pr->m_controlAxes, g_or + kRmAxes});
    g_extraPointers.push_back({g_pr->m_controlButtons, g_or + kRmButtons});
}

void RandomRemote()
{
    RandomAi();
    C_RemoteController *r = g_pr;
    RandomControllerFields(r);
    r->m_human = g_h.human;
    r->m_handsItem = g_pc->m_handsItem;
    r->m_toServer.clear();
    r->m_toClients.clear();
    std::memset(g_or + 0x80, 0, 0x20);
    for (float &a : r->m_controlAxes)
        a = P(0.7f) ? 0.0f : U(-1, 1);
    for (float &b : r->m_controlButtons)
        b = P(0.8f) ? 0.0f : 1.0f;
    r->m_vip = P(0.5f);
    r->m_surrendered = P(0.5f);
    g_h.human->m_controller = r;
    // (A network game: remote controllers are a client's or the server's copies.)
    g_gameRole = P(0.5f) ? GAME_ROLE_CLIENT : GAME_ROLE_SERVER;
    r->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
    g_h.human->m_speechSet = reinterpret_cast<C_SpeechSet *>(g_speechSetBytes2);
    g_rmStats[5] = I(0, 6);
}

void TestRemoteA()
{
    std::printf("remote controller: life cycle, requests, messages\n");
    C_Human *h = g_h.human;
    for (int i = 0; i < 16000 / QUICK; ++i) {
        RandomRemote();
        const int which = I(0, 8);
        switch (which) {
        case 0: {
            const bool previous = P(0.5f);
            C_HumanController *pp = previous ? g_pc2 : nullptr;
            void *op = previous ? static_cast<void *>(g_oc2) : nullptr;
            g_pr->m_events = nullptr;
            RemoteCall("Attach", [&] { g_pr->Attach(h, pp); return 0; },
                       [&](void *) { ORm<void>(0x00592140, static_cast<void *>(o_human), op); return 0; });
            break;
        }
        case 1:
            RemoteCall("Detach", [&] { g_pr->Detach(); return 0; }, [&](void *) { ORm<void>(0x00592200); return 0; });
            break;
        case 2: {
            const uint32_t flags = uint32_t(I(0, 0x3FFFF));
            RemoteCall("OnBodyFlags", [&] { g_pr->OnBodyFlags(flags); return 0; },
                       [&](void *) { ORm<void>(0x005941b0, flags); return 0; });
            break;
        }
        case 3: {
            const int a = I(-1, 500), b = I(-1, 120), c = I(0, 7), d = I(0, 7), e = I(-1, 15);
            RemoteCall("RequestMoveItem", [&] { return int(g_pr->RequestMoveItem(a, b, c, d, e)); },
                       [&](void *) { return int(ORm<uint32_t>(0x00592320, a, b, c, d, e) & 0xFF); });
            break;
        }
        case 4: {
            const int a = I(-1, 500), b = I(0, 7), c = I(-1, 120), d = I(-1, 500), e = I(0, 7);
            RemoteCall("RequestTransferItem", [&] { return int(g_pr->RequestTransferItem(a, b, c, d, e)); },
                       [&](void *) { return int(ORm<uint32_t>(0x00592500, a, b, c, d, e) & 0xFF); });
            break;
        }
        case 5: {
            const int uid = I(-1, 500);
            const S_vector4 v4 = AiDir();
            const S_vector v(v4.x * (P(0.5f) ? U(0, 20) : 1.0f), v4.y, v4.z);
            RemoteCall("RequestThrowItem", [&] { g_pr->RequestThrowItem(uid, v); return 0; },
                       [&](void *) { ORm<void>(0x005926e0, uid, &v); return 0; });
            break;
        }
        case 6: {
            const bool toClients = P(0.5f);
            RandomAiQueue(toClients ? g_pr->m_toClients : g_pr->m_toServer, g_or + (toClients ? 0x90 : 0x80));
            RemoteCall("FlushMessages", [&] { g_pr->FlushMessages(toClients); return 0; },
                       [&](void *) { ORm<void>(0x00592220, toClients); return 0; });
            break;
        }
        default: {
            // (Not the inventory check: the original leaves its message's human unset, a stale value.)
            uint32_t input[128] = {};
            {
                C_BitStream w(input, sizeof input * 8);
                static const int TYPES[] = {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 14, 15, 16, 18, 19, 20, 22, 23};
                for (int k = I(0, 5); k > 0; --k) {
                    const int type = TYPES[I(0, 19)];
                    w.WriteBits(uint32_t(type), 5);
                    for (const S_AiMsgField &f : AiMsgFields(type)) {
                        if (f.kind == 'f') {
                            const float x = U(-50, 50);
                            uint32_t b;
                            std::memcpy(&b, &x, 4);
                            w.WriteBits(b, 32);
                        } else if (f.kind == 'b') {
                            w.WriteBits(P(0.5f), 1);
                        } else {
                            w.WriteBits(uint32_t(P(0.3f) ? 100 + I(0, KIND_HUMAN) : I(0, 0x3F)) & 0xFFFF, 16);
                        }
                    }
                }
                w.WriteBits(CTRL_MSG_END, 5);
            }
            S_AiStream ps, os;
            std::memcpy(ps.words, input, sizeof ps.words);
            std::memcpy(os.words, input, sizeof os.words);
            ps.msg.Attach(ps.words, sizeof ps.words * 8);
            os.msg.Attach(os.words, sizeof os.words * 8);
            RemoteCall("OnMessage", [&] { g_pr->OnMessage(&ps.msg); return int(ps.msg.BitPosition()); },
                       [&](void *) { ORm<void>(0x00592fe0, &os.msg); return int(os.msg.BitPosition()); });
            break;
        }
        }
    }
}

// ---- part 34b: Update (the replicated position, view and controls; the net object's messages: the actions, the dive,
// the binoculars)
//
// The net object's Update (its vtable's slot 2 on both sides) is logged; the property's position track answers its
// GetCurrent and GetRate (slots 10 and 13), the direction track its GetCurrent, with the test's vectors; PopMessage hands
// out each side's queue of net messages (the same layout: type +0x1C, from a client +0x2C, the action's fields from
// +0x30; the binoculars' flag +0x30 in the original, after the base's bool in the port), each holding a reference so that neither side deletes it. The weapons' SetRounds
// (slot 0x14) and PlayEmptySound, the visual's SetDirection are logged alike.
static_assert(offsetof(C_NetActionMessage, dir) == 0x30 && offsetof(C_NetActionMessage, action) == 0x60 &&
                  offsetof(C_NetObjectMessage, refCount) == 0x24,
              "the net messages: the original's layout");
S_vector4 g_rmPos, g_rmDir, g_rmRate;
const S_vector4 &AC TR_GetCurrent(void *) { return g_rmPos; }
const S_vector4 &AC TR_GetRate(void *) { return g_rmRate; }
const S_vector4 &AC TR_GetDirection(void *) { return g_rmDir; }
void AC NO_Update(void *self, float seconds) { Log(self, "NetObjectUpdate", {seconds}); }
void *g_rmNetVt[16];
void AC WP_SetRounds(void *self, void *owner, int ammo, int rounds) { Log(self, ("WeaponSetRounds " + NameOf(owner)).c_str(), {double(ammo), double(rounds)}); }
void AC OR_OnFrozen(void *self, int on) { Log(self, "MovementOnFrozen", {double(on & 0xFF)}); }
void AC OR_SetDirection(void *self, const S_vector4 *dir, float roll) { Log(self, "VisualSetDirection", {dir->x, dir->y, dir->z, roll}); }
void AC OR_PlayEmptySound(void *self, void *owner) { Log(self, "PlayEmptySound", {double(owner != nullptr)}); }
// The net messages each side's PopMessage hands out.
std::vector<uint8_t *> g_rmPortQueue, g_rmOrigQueue, g_rmAll;
void *PopRemoteMessage(void *object, bool orig, bool outgoing)
{
    Log(object, "PopMessage", {double(outgoing)});
    std::vector<uint8_t *> &q = orig ? g_rmOrigQueue : g_rmPortQueue;
    if (q.empty())
        return nullptr;
    uint8_t *m = q.front();
    q.erase(q.begin());
    return m;
}
void *AC OR_PopMessage2(void *self, int outgoing) { return PopRemoteMessage(self, true, (outgoing & 0xFF) != 0); }
void RandomNetMessages()
{
    g_rmPortQueue.clear();
    g_rmOrigQueue.clear();
    for (int k = P(0.3f) ? 0 : I(1, 3); k > 0; --k) {
        uint8_t *pm = static_cast<uint8_t *>(_aligned_malloc(0x80, 16)), *om = static_cast<uint8_t *>(_aligned_malloc(0x80, 16));
        std::memset(pm, 0, 0x80);
        const int type = P(0.7f) ? NET_MSG_ACTION : P(0.5f) ? NET_MSG_DIVE : NET_MSG_BINOCULARS;
        std::memcpy(pm + 0x1C, &type, 4);
        const int refs = 1;
        std::memcpy(pm + 0x24, &refs, 4);
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
        // (The binoculars' flag: +0x30 in the original, after the base's bool in the port.)
        if (type == NET_MSG_BINOCULARS)
            pm[offsetof(C_NetBinocularsMessage, on)] = om[0x30] = uint8_t(P(0.5f));
        g_rmAll.push_back(pm);
        g_rmAll.push_back(om);
        g_rmPortQueue.push_back(pm);
        g_rmOrigQueue.push_back(om);
    }
}

void SetUpRemoteUpdate()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0073e9d0, FAKE(OR_PopMessage2)},
        {0x0075a520, FAKE(OR_SetDirection)},
        {0x0075c730, FAKE(OR_OnFrozen)},
        {0x007d52f0, FAKE(OR_PlayEmptySound)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_portTrackVt[VSlot(&C_NetTrack::GetCurrent)] = reinterpret_cast<void *>(TR_GetCurrent);
    g_portTrackVt[VSlot(&C_NetTrack::GetRate)] = reinterpret_cast<void *>(TR_GetRate);
    g_origTrackVt[0x28 / 4] = reinterpret_cast<void *>(TR_GetCurrent);
    g_origTrackVt[0x34 / 4] = reinterpret_cast<void *>(TR_GetRate);
    g_aiDirTrackVtPort[VSlot(&C_NetDirTrack::GetCurrent)] = reinterpret_cast<void *>(TR_GetDirection);
    g_aiDirTrackVtOrig[0x28 / 4] = reinterpret_cast<void *>(TR_GetDirection);
    for (void *&slot : g_rmNetVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_rmNetVt[2] = reinterpret_cast<void *>(NO_Update);
    g_weaponPortVt[VSlot(&C_Item::SetRounds)] = reinterpret_cast<void *>(WP_SetRounds);
    g_weaponOrigVt[0x14 / 4] = reinterpret_cast<void *>(WP_SetRounds);
}

void TestRemoteUpdate()
{
    std::printf("remote controller: update\n");
    C_Human *h = g_h.human;
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomRemote();
        RandomNetMessages();
        // (Dead or not; a spectator, in play or not; in a seat or not; the replicated values.)
        if (P(0.1f))
            g_pr->m_moveFlags |= MOVE_DEAD;
        else
            g_pr->m_moveFlags &= ~MOVE_DEAD;
        g_pr->m_spectating = P(0.2f);
        h->m_inPlay = P(0.85f);
        g_rmPos = P(0.1f) ? S_vector4{0, 0, 0, 0} : S_vector4{U(-200, 200), U(-5, 5), U(-200, 200), 1};
        g_rmDir = AiDir();
        g_rmRate = AiDir();
        g_pr->m_handsItem = g_pc->m_handsItem;
        // (Messages to the server: Update flushes them first.)
        if (P(0.5f))
            RandomAiQueue(g_pr->m_toServer, g_or + 0x80);
        const int frameTime = I(0, 200);
        RemoteCall("Update",
                   [&] {
                       *reinterpret_cast<void ***>(g_netObjectBytes) = g_rmNetVt;
                       g_pr->Update(frameTime);
                       return 0;
                   },
                   [&](void *) {
                       *reinterpret_cast<void ***>(g_netObjectBytes) = g_rmNetVt;
                       ORm<void>(0x00592a60, frameTime);
                       return 0;
                   });
        // (The weapons' throw strengths: the original's +0x98.)
        for (int k = 0; k < 2; ++k) {
            const float ps = reinterpret_cast<C_WeaponItem *>(g_weaponPort[k])->GetThrowStrength();
            float os;
            std::memcpy(&os, g_weaponOrig[k] + 0x98, 4);
            CHECK(std::memcmp(&ps, &os, 4) == 0);
        }
        for (uint8_t *m : g_rmAll)
            _aligned_free(m);
        g_rmAll.clear();
    }
}

void TestRemoteController()
{
    SetUpRemote();
    SetUpRemoteUpdate();
    TestRemoteA();
    TestRemoteUpdate();
}
}  // namespace
