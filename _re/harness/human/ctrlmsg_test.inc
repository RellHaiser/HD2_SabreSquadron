// Part 31 of the human harness (HUMAN_CTRLMSG builds, build_ctrlmsg.bat; included by human_test.cpp after
// locomotion3_test.inc): the controllers' network messages (game/controller_messages.cpp), every message's Read and
// Write and the control state's two helpers, against the original's (0x005ac5f0..0x005b30d0).
//
// The bits go through each side's own bit stream code (WriteBits, ReadBits, WriteString, ReadString) on port
// C_NetMessage objects over the test's buffers, as in part 27; the written words and bit positions are compared, and
// Read reads the same input on both sides. The quantised floats and the 3D directions stand in on both sides with
// raw floats of their own (their call logged with the sender or player index: C_NetMessage's own code is tested by
// the net harnesses). What the messages call besides is logged alike: the port's calls through ld --wrap
// (ctrlmsg_wrap.rsp), the original's through redirects. The message objects are the port's classes; the original
// runs on a copy of their bytes (the action command's first words moved: the original's command has a vtable), and
// the objects are compared after, word by word, but for the vectors' w the original fills from a stack temp.
#include <cstdarg>

#include "game/controller_messages.h"
#include "game/item_defs.h"
#include "game/items.h"

S_NetPlayer *g_netPlayerTable = nullptr;                // (net/net_game.cpp's, not linked: GetSender is wrapped)
int g_netPlayerCount = 0;
namespace {

#define CM __attribute__((thiscall))

struct S_CmScript {
    bool on = false;
    int controllerType[3] = {7, 7, 7};                  // the targets' GetControllerType
    int health = 0, team = 0;                           // GetEquipmentState's
} g_cm;

void CLog(const void *self, const std::string &what, std::initializer_list<double> nums = {})
{
    if (g_cm.on)
        Log(self, what.c_str(), nums);
}

// ---- the senders and players (GetSender's answers; the quantisation is not read: the floats go raw)
alignas(16) uint8_t g_cmPlayers[8][0x80];
void *CM CM_GetSender(const void *msg, int index)
{
    CLog(nullptr, "GetSender", {double(index)});
    return g_cmPlayers[(index + 2) & 7];
}
void PutFloat(C_BitStream *msg, float f)
{
    uint32_t w;
    std::memcpy(&w, &f, 4);
    msg->WriteBits(w, 32);
}
float TakeFloat(C_BitStream *msg)
{
    const uint32_t w = msg->ReadBits(32);
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}
bool CM CM_WriteQuantized(C_BitStream *msg, const float *v, const void *q, uint8_t mask)
{
    CLog(q, "WriteQuantized", {v[0], v[1], v[2], double(mask)});
    for (int i = 0; i < 3; ++i)
        if (mask & (1 << i))
            PutFloat(msg, v[i]);
    return true;
}
void CM CM_ReadQuantized(C_BitStream *msg, float *v, const void *q, uint32_t mask)
{
    for (int i = 0; i < 3; ++i)
        v[i] = (mask & (1u << i)) ? TakeFloat(msg) : 7.25f;
    CLog(q, "ReadQuantized", {double(mask)});
}
void CM CM_WriteDirection3D(C_BitStream *msg, const float *dir, int player)
{
    CLog(nullptr, "WriteDirection3D", {dir[0], dir[1], dir[2], double(player)});
    for (int i = 0; i < 3; ++i)
        PutFloat(msg, dir[i]);
}
void CM CM_ReadDirection3D(C_BitStream *msg, float *dir, int player)
{
    for (int i = 0; i < 3; ++i)
        dir[i] = TakeFloat(msg);
    CLog(nullptr, "ReadDirection3D", {double(player)});
}

// ---- the human of the inventory check: FindItemByUid (found or not by the shared rand), GetFullName; the game's Print
alignas(16) uint8_t g_cmFound[0x40], g_cmGame[0x40];
C_Item *CM CM_FindItemByUid(const void *human, int uid)
{
    const bool found = std::rand() % 3 != 0;
    CLog(human, "FindItemByUid", {double(uid), double(found)});
    return found ? reinterpret_cast<C_Item *>(g_cmFound) : nullptr;
}
const char *CM CM_GetFullName(const void *human)
{
    CLog(human, "GetFullName");
    return "Sgt. Test Soldier";
}
void *__cdecl CM_GetGame()
{
    CLog(nullptr, "GetGame");
    return g_cmGame;
}
void __cdecl CM_Print(void *game, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const char *name = va_arg(args, const char *);
    const char *type = va_arg(args, const char *);
    const int uid = va_arg(args, int);
    va_end(args);
    CLog(game, std::string("Print ") + format + "|" + (name ? name : "null") + "|" + (type ? type : "null"), {double(uid)});
}
// The item definitions (the original's table at 0x0098b030, 500 entries; the name at +0x10 on both sides).
enum { CM_DEFS = 10 };
alignas(16) uint8_t g_cmDefs[CM_DEFS][0x40];
const char *const CM_DEF_NAMES[CM_DEFS] = {"Thompson", "MP40", "Colt", "Knife", "Medkit", "Grenade", "Helmet",
                                           "Uniform", "Backpack", "Kar98"};

// ---- the equipment message's humans: by actor id, three of them
const int CM_TARGET_IDS[3] = {5, 77, 300};
alignas(16) uint8_t g_cmTargetPort[3][0x40], g_cmTargetOrig[3][0x40];
int TargetIndex(int id)
{
    for (int i = 0; i < 3; ++i)
        if (CM_TARGET_IDS[i] == id)
            return i;
    return -1;
}
void *CM CM_GetActorByIdOrig(const void *, int id)
{
    CLog(nullptr, "GetActorById", {double(id)});
    const int i = TargetIndex(id);
    return i < 0 ? nullptr : g_cmTargetOrig[i];
}
int TargetOf(const void *human)
{
    for (int i = 0; i < 3; ++i)
        if (human == g_cmTargetPort[i] || human == g_cmTargetOrig[i])
            return i;
    return -1;
}
int CM CM_GetControllerType(const void *human)
{
    const int i = TargetOf(human);
    const int type = i < 0 ? -1 : g_cm.controllerType[i];
    CLog(human, "GetControllerType", {double(type)});
    return type;
}
// The soldier state's words both sides set (the port's offsets are the original's up to the belt).
std::vector<double> StateDump(const uint8_t *s)
{
    std::vector<double> d;
    auto word = [&](int at) {
        int32_t w;
        std::memcpy(&w, s + at, 4);
        d.push_back(w);
    };
    word(0x04);
    d.push_back(s[0x08]);
    d.push_back(s[0x09]);
    d.push_back(s[0x0A]);
    // (Not the progress, +0x10..+0x34: the original zeroes it, the port's S_SoldierProgress starts with its defaults;
    // the items-only state neither sends nor reads it, and CreateEquipment reads the items alone.)
    word(0x0C);
    for (int at = 0x34; at < 0x44; at += 4)
        word(at);
    d.push_back(s[0x44]);
    word(0x48);
    for (int at = 0x4C; at < 0x6A; ++at)
        d.push_back(s[at]);
    return d;
}
void LogState(const void *self, const char *what, const uint8_t *state)
{
    if (!g_cm.on)
        return;
    S_Call c{NameOf(self) + "." + what, StateDump(state)};
    g_calls.push_back(c);
}
void CM CM_CreateEquipment(void *human, uint8_t *state) { LogState(human, "CreateEquipment", state); }
void CM CM_GetEquipmentState(const void *human, uint8_t *state)
{
    LogState(human, "GetEquipmentState", state);
    std::memcpy(state + 0x0C, &g_cm.health, 4);
    std::memcpy(state + 0x40, &g_cm.team, 4);
}
void __fastcall CM_NetWriteSoldierState(C_BitStream *msg, uint8_t *state, bool itemsOnly)
{
    LogState(nullptr, itemsOnly ? "NetWriteSoldierState items" : "NetWriteSoldierState", state);
    int health;
    std::memcpy(&health, state + 0x0C, 4);
    msg->WriteBits(uint32_t(health) & 0xFF, 8);
    msg->WriteBits(0x15, 5);
}
void __fastcall CM_NetReadSoldierState(C_BitStream *msg, uint8_t *state, bool itemsOnly)
{
    const int health = int(int8_t(msg->ReadBits(8)));
    const int team = int(msg->ReadBits(3));
    std::memcpy(state + 0x0C, &health, 4);
    std::memcpy(state + 0x40, &team, 4);
    LogState(nullptr, itemsOnly ? "NetReadSoldierState items" : "NetReadSoldierState", state);
}
void *__cdecl CM_New(size_t size) { return std::malloc(size); }
void __cdecl CM_Delete(void *p) { std::free(p); }

// ---- the inventory check's containers: the human's 7 (single items: GetItem; the belt and the backpack's contents:
// GetUsedSlotCount, GetSlots), each side's vtable and slots (the port's vectors, the original's pointers to MSVC6
// vectors: 4 bytes, then first, last, end)
struct S_CmContainer {
    void **vt;
    C_Item *item;
    int used;
    std::vector<C_Item *> *portSlots;
    uint32_t **origSlots;
};
C_Item *CM CC_GetItem(S_CmContainer *c) { return c->item; }
int CM CC_GetUsedSlotCount(S_CmContainer *c) { return c->used; }
std::vector<C_Item *> *CM CC_GetPortSlots(S_CmContainer *c) { return c->portSlots; }
uint32_t **CM CC_GetOrigSlots(S_CmContainer *c) { return c->origSlots; }
void CM CC_Unexpected(S_CmContainer *c)
{
    std::printf("unexpected container call on %s\n", NameOf(c).c_str());
    std::abort();
}
void *g_ccPortVt[40], *g_ccOrigVt[40];
S_CmContainer g_ccPort[ITEM_CONTAINER_COUNT], g_ccOrig[ITEM_CONTAINER_COUNT];
C_ItemContainer *g_ccPortArray[ITEM_CONTAINER_COUNT];
S_CmContainer *g_ccOrigArray[ITEM_CONTAINER_COUNT];
std::vector<C_Item *> g_ccSlots[2][32];
uint32_t g_ccMsvc[2][32][4];
uint32_t *g_ccMsvcPtrs[2][32];
enum { CM_ITEMS = 40 };
alignas(16) uint8_t g_cmItems[CM_ITEMS][0x40];

void RandomContainers()
{
    for (int c = 0; c < ITEM_CONTAINER_COUNT; ++c) {
        C_Item *item = P(0.6f) ? reinterpret_cast<C_Item *>(g_cmItems[I(0, CM_ITEMS - 1)]) : nullptr;
        g_ccPort[c].item = g_ccOrig[c].item = item;
    }
    for (int s = 0; s < 2; ++s) {
        const int c = s == 0 ? ITEM_CONTAINER_BELT : ITEM_CONTAINER_BACKPACK_CONTENTS;
        g_ccPort[c].item = g_ccOrig[c].item = nullptr;
        const int used = P(0.15f) ? 0 : I(1, 6);
        g_ccPort[c].used = g_ccOrig[c].used = used;
        for (int i = 0; i < 32; ++i) {
            std::vector<C_Item *> &v = g_ccSlots[s][i];
            v.clear();
            const int n = i < used + 1 ? (P(0.2f) ? 0 : I(1, 4)) : 0;
            for (int k = 0; k < n; ++k)
                v.push_back(reinterpret_cast<C_Item *>(g_cmItems[I(0, CM_ITEMS - 1)]));
            // (An empty slot's vector: never allocated, or emptied.)
            const bool nullFirst = v.empty() && P(0.5f);
            uint32_t *m = g_ccMsvc[s][i];
            m[0] = 0xCDCDCDCD;
            m[1] = nullFirst ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(v.empty() ? reinterpret_cast<C_Item **>(m) : v.data()));
            m[2] = m[1] + 4 * static_cast<uint32_t>(v.size());
            m[3] = m[2];
            g_ccMsvcPtrs[s][i] = m;
        }
    }
    // (No backpack: no backpack contents; the original checks that container alone.)
    const bool contents = P(0.7f);
    g_ccPortArray[ITEM_CONTAINER_BACKPACK_CONTENTS] = contents ? reinterpret_cast<C_ItemContainer *>(&g_ccPort[ITEM_CONTAINER_BACKPACK_CONTENTS]) : nullptr;
    g_ccOrigArray[ITEM_CONTAINER_BACKPACK_CONTENTS] = contents ? &g_ccOrig[ITEM_CONTAINER_BACKPACK_CONTENTS] : nullptr;
    for (int k = 0; k < CM_ITEMS; ++k) {
        const int type = I(-2, 600), uid = P(0.2f) ? I(-5, 5) : I(0, 0x7FFFFFFF);
        std::memcpy(g_cmItems[k] + 0x0C, &type, 4);
        std::memcpy(g_cmItems[k] + 0x10, &uid, 4);
    }
}

void InstallCtrlMsgRedirects()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0073d770, reinterpret_cast<const void *>(CM_GetSender)},
        {0x0073d810, reinterpret_cast<const void *>(CM_WriteQuantized)},
        {0x0073d900, reinterpret_cast<const void *>(CM_ReadQuantized)},
        {0x0073da90, reinterpret_cast<const void *>(CM_WriteDirection3D)},
        {0x0073dae0, reinterpret_cast<const void *>(CM_ReadDirection3D)},
        {0x005a6ff0, reinterpret_cast<const void *>(CM_FindItemByUid)},
        {0x005a8320, reinterpret_cast<const void *>(CM_GetFullName)},
        {0x006987d0, reinterpret_cast<const void *>(CM_GetGame)},
        {0x0069c2b0, reinterpret_cast<const void *>(CM_Print)},
        {0x00475d50, reinterpret_cast<const void *>(CM_GetActorByIdOrig)},
        {0x00727a30, reinterpret_cast<const void *>(CM_NetReadSoldierState)},
        {0x00727850, reinterpret_cast<const void *>(CM_NetWriteSoldierState)},
        {0x005a5fe0, reinterpret_cast<const void *>(CM_GetControllerType)},
        {0x0059eb60, reinterpret_cast<const void *>(CM_CreateEquipment)},
        {0x005a8b50, reinterpret_cast<const void *>(CM_GetEquipmentState)},
        {0x007e566f, reinterpret_cast<const void *>(CM_New)},
        {0x007e2120, reinterpret_cast<const void *>(CM_Delete)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    for (int i = 0; i < 8; ++i)
        g_names[g_cmPlayers[i]] = "player" + std::to_string(i - 2);
    g_names[g_cmFound] = "foundItem";
    g_names[g_cmGame] = "game";
    for (int i = 0; i < 3; ++i)
        g_names[g_cmTargetPort[i]] = g_names[g_cmTargetOrig[i]] = "target" + std::to_string(i);
    void **origDefs = OriginalAt<void **>(0x0098b030);
    for (int i = 0; i < CM_DEFS; ++i) {
        const char *name = CM_DEF_NAMES[i];
        std::memcpy(g_cmDefs[i] + 0x10, &name, 4);
        origDefs[i] = g_cmDefs[i];
        CHECK(reinterpret_cast<const C_ItemDef *>(g_cmDefs[i])->GetDisplayName() == name);   // (+0x10 in both)
    }
    for (void *&slot : g_ccPortVt)
        slot = reinterpret_cast<void *>(CC_Unexpected);
    for (void *&slot : g_ccOrigVt)
        slot = reinterpret_cast<void *>(CC_Unexpected);
    g_ccPortVt[VSlot(&C_ItemContainer::GetItem)] = reinterpret_cast<void *>(CC_GetItem);
    g_ccPortVt[VSlot(&C_ItemContainer::GetUsedSlotCount)] = reinterpret_cast<void *>(CC_GetUsedSlotCount);
    g_ccPortVt[VSlot(&C_ItemContainer::GetSlots)] = reinterpret_cast<void *>(CC_GetPortSlots);
    g_ccOrigVt[0x40 / 4] = reinterpret_cast<void *>(CC_GetItem);
    g_ccOrigVt[0x28 / 4] = reinterpret_cast<void *>(CC_GetUsedSlotCount);
    g_ccOrigVt[0x3C / 4] = reinterpret_cast<void *>(CC_GetOrigSlots);
    for (int c = 0; c < ITEM_CONTAINER_COUNT; ++c) {
        g_ccPort[c].vt = g_ccPortVt;
        g_ccOrig[c].vt = g_ccOrigVt;
        g_ccPortArray[c] = reinterpret_cast<C_ItemContainer *>(&g_ccPort[c]);
        g_ccOrigArray[c] = &g_ccOrig[c];
        g_names[&g_ccPort[c]] = g_names[&g_ccOrig[c]] = "container" + std::to_string(c);
    }
    for (int s = 0; s < 2; ++s) {
        const int c = s == 0 ? ITEM_CONTAINER_BELT : ITEM_CONTAINER_BACKPACK_CONTENTS;
        g_ccPort[c].portSlots = g_ccSlots[s];
        g_ccOrig[c].origSlots = g_ccMsvcPtrs[s];
    }
    g_names[g_ccPortArray] = g_names[g_ccOrigArray] = "containers";
    g_extraPointers.push_back({g_ccPortArray, g_ccOrigArray});
}
}  // namespace

// ---- the port's side through ld --wrap
extern "C" {
S_NetPlayer *CM __wrap__ZNK12C_NetMessage9GetSenderEi(const C_NetMessage *m, int i) { return static_cast<S_NetPlayer *>(CM_GetSender(m, i)); }
bool CM __wrap__ZN12C_NetMessage14WriteQuantizedEPKfPK17S_NetQuantizationh(C_NetMessage *m, const float *v, const S_NetQuantization *q, uint8_t mask)
{
    return CM_WriteQuantized(m, v, q, mask);
}
void CM __wrap__ZN12C_NetMessage13ReadQuantizedEPfPK17S_NetQuantizationj(C_NetMessage *m, float *v, const S_NetQuantization *q, uint32_t mask)
{
    CM_ReadQuantized(m, v, q, mask);
}
void CM __wrap__ZN12C_NetMessage16WriteDirection3DERK8S_vectori(C_NetMessage *m, const S_vector &d, int p) { CM_WriteDirection3D(m, &d.x, p); }
void CM __wrap__ZN12C_NetMessage15ReadDirection3DER8S_vectori(C_NetMessage *m, S_vector &d, int p) { CM_ReadDirection3D(m, &d.x, p); }
C_Item *CM __wrap__ZNK7C_Human13FindItemByUidEi(const C_Human *h, int uid) { return CM_FindItemByUid(h, uid); }
const char *CM __wrap__ZNK7C_Human11GetFullNameEv(const C_Human *h) { return CM_GetFullName(h); }
C_game *__wrap__Z7GetGamev() { return static_cast<C_game *>(CM_GetGame()); }
void __cdecl __wrap__ZN6C_game5PrintEPKcz(C_game *game, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const char *name = va_arg(args, const char *);
    const char *type = va_arg(args, const char *);
    const int uid = va_arg(args, int);
    va_end(args);
    CM_Print(game, format, name, type, uid);
}
C_ItemDef *__wrap__Z10GetItemDefi(int typeId)
{
    return typeId >= 0 && typeId < CM_DEFS ? reinterpret_cast<C_ItemDef *>(g_cmDefs[typeId]) : nullptr;
}
C_Actor *CM __wrap__ZNK14C_ActorsModule12GetActorByIdEi(const void *, int id)
{
    CLog(nullptr, "GetActorById", {double(id)});
    const int i = TargetIndex(id);
    return i < 0 ? nullptr : reinterpret_cast<C_Actor *>(g_cmTargetPort[i]);
}
int CM __wrap__ZNK7C_Human17GetControllerTypeEv(const C_Human *h) { return CM_GetControllerType(h); }
void CM __wrap__ZN7C_Human15CreateEquipmentEP14S_SoldierState(C_Human *h, S_SoldierState *s)
{
    CM_CreateEquipment(h, reinterpret_cast<uint8_t *>(s));
}
void CM __wrap__ZNK7C_Human17GetEquipmentStateER14S_SoldierState(const C_Human *h, S_SoldierState &s)
{
    CM_GetEquipmentState(h, reinterpret_cast<uint8_t *>(&s));
}
void __wrap__Z20NetWriteSoldierStateP12C_NetMessageR14S_SoldierStateb(C_NetMessage *m, S_SoldierState &s, bool items)
{
    CM_NetWriteSoldierState(m, reinterpret_cast<uint8_t *>(&s), items);
}
void __wrap__Z19NetReadSoldierStateP12C_NetMessageR14S_SoldierStateb(C_NetMessage *m, S_SoldierState &s, bool items)
{
    CM_NetReadSoldierState(m, reinterpret_cast<uint8_t *>(&s), items);
}
}  // extern "C"

namespace {
static_assert(offsetof(S_SoldierState, spawnZone) == 0x04 && offsetof(S_SoldierState, health) == 0x0C &&
                      offsetof(S_SoldierState, unknown34) == 0x34 && offsetof(S_SoldierState, team) == 0x40 &&
                      offsetof(S_SoldierState, deployed) == 0x44 && offsetof(S_SoldierState, speechSet) == 0x48 &&
                      offsetof(S_SoldierState, head) == 0x4C && offsetof(S_SoldierState, hands) == 0x64,
              "S_SoldierState: the original's offsets");
// (The port's offsets: its vectors are 4-aligned, so the messages with vectors are not in the original's layout.)
static_assert(offsetof(S_MsgHit, pos) == 0x18 && offsetof(S_MsgHit, velocity) == 0x28, "S_MsgHit");
static_assert(offsetof(S_MsgControlState, pos) == 0x08 && offsetof(S_MsgControlState, controlBits) == 0x48, "S_MsgControlState");
static_assert(offsetof(S_MsgMoveItem, slot) == 0x18 && offsetof(S_MsgTransferItem, container) == 0x18, "item messages");
static_assert(offsetof(S_MsgDropItem, dir) == 0x0C && offsetof(S_MsgDropItem, actor) == 0x20, "S_MsgDropItem");
static_assert(offsetof(S_MsgTakeFromBox, container) == 0x14, "S_MsgTakeFromBox");
static_assert(offsetof(S_MsgAction, pos) == 0x08 && offsetof(S_MsgAction, command) == 0x18 &&
                      offsetof(S_ActionCommand, dir) == 0x0C && offsetof(S_ActionCommand, member) == 0x2C,
              "S_MsgAction");
static_assert(offsetof(S_MsgHeal, removeItem) == 0x14 && offsetof(S_Msg17, value14) == 0x14, "S_MsgHeal, S_Msg17");
static_assert(offsetof(S_MsgAnimation, blendTime) == 0x88 && offsetof(S_MsgAnimation, loop) == 0x90, "S_MsgAnimation");
static_assert(offsetof(S_MsgSay, force) == 0x0C && offsetof(S_MsgSuspend, on) == 0x08, "S_MsgSay, S_MsgSuspend");

// A run of bytes of a message: its port offset, the original's, its size.
struct S_CmField {
    size_t port, orig, size;
};
// The messages: the port's object, the original's Read and Write.
struct S_CmMessage {
    const char *name;
    S_ControllerMessage *msg;
    size_t size;
    uint32_t read, write;
    std::vector<size_t> skipAfterRead;                  // words the original fills from a stack temp (its offsets)
    std::vector<S_CmField> fields;                      // (none: the same offsets, +4 to the end)
};
std::vector<S_CmMessage> g_cmMessages;
template <class T> void AddMessage(const char *name, uint32_t read, uint32_t write, std::vector<size_t> skip = {},
                                   std::vector<S_CmField> fields = {})
{
    if (fields.empty())
        fields.push_back({4, 4, sizeof(T) - 4});
    g_cmMessages.push_back({name, new T, sizeof(T), read, write, std::move(skip), std::move(fields)});
}
void MakeMessages()
{
    AddMessage<S_MsgControls>("Controls", 0x005b10c0, 0x005b1100);
    AddMessage<S_MsgHit>("Hit", 0x005b1510, 0x005b1540, {}, {{4, 4, 0x14}, {0x18, 0x20, 0x20}});
    AddMessage<S_MsgPutOnUniform>("PutOnUniform", 0x005b1980, 0x005b19a0);
    AddMessage<S_MsgCommand>("Command", 0x005b1180, 0x005b11a0);
    AddMessage<S_MsgControlState>("ControlState", 0x005b1000, 0x005b1030, {0x1C, 0x2C, 0x3C}, {{4, 4, 4}, {0x08, 0x10, 0x44}});
    AddMessage<S_MsgUseObject>("UseObject", 0x005b0f70, 0x005b0f90);
    AddMessage<S_MsgMoveItem>("MoveItem", 0x005b1a10, 0x005b1ad0);
    AddMessage<S_MsgTransferItem>("TransferItem", 0x005b1bb0, 0x005b1c40);
    AddMessage<S_MsgPickUpItem>("PickUpItem", 0x005b1e40, 0x005b1e90);
    AddMessage<S_MsgDropItem>("DropItem", 0x005b1f10, 0x005b2020, {0x1C}, {{4, 4, 8}, {0x0C, 0x10, 0x18}});
    AddMessage<S_MsgTakeFromBox>("TakeFromBox", 0x005b1d10, 0x005b1d80);
    // (The original's command: its vtable at +0x20, category +0x24, ..., dir +0x30.)
    AddMessage<S_MsgAction>("Action", 0x005b1290, 0x005b13f0, {0x1C, 0x3C}, {{4, 4, 4}, {0x08, 0x10, 0x10}, {0x18, 0x24, 0x30}});
    AddMessage<S_MsgInventoryCheck>("InventoryCheck", 0x005b2340, 0x005b2700);
    AddMessage<S_MsgHeal>("Heal", 0x005b2100, 0x005b2150);
    AddMessage<S_MsgRemoteRequest>("RemoteRequest", 0x005b0ee0, 0x005b0f00);
    AddMessage<S_MsgSoldierClass>("SoldierClass", 0x005b0e50, 0x005b0e70);
    AddMessage<S_Msg17>("Msg17", 0x005b1800, 0x005b1820);
    AddMessage<S_MsgAnimation>("Animation", 0x005b21d0, 0x005b2230);
    AddMessage<S_MsgSay>("Say", 0x005b22b0, 0x005b22e0);
    AddMessage<S_MsgEquipment>("Equipment", 0x005b2a90, 0x005b2d20);
    AddMessage<S_Msg21>("Msg21", 0x005b2f80, 0x005b2fa0);
    AddMessage<S_MsgSuspend>("Suspend", 0x005b2ff0, 0x005b3010);
    AddMessage<S_Msg23>("Msg23", 0x005b3060, 0x005b3080);
}

// The original's copy of a message: the port's fields at the original's offsets (the rest a fill), the inventory
// check's human the original's.
alignas(16) uint8_t g_cmOrig[0x400];
void ToOrigBytes(const S_CmMessage &m, const uint8_t *port, uint8_t *orig)
{
    std::memset(orig, 0xA5, 0x100);
    for (const S_CmField &f : m.fields)
        std::memcpy(orig + f.orig, port + f.port, f.size);
    if (m.msg->type == CTRL_MSG_INVENTORY_CHECK) {
        void *h;
        std::memcpy(&h, port + 8, 4);
        if (h == g_h.human)
            h = o_human;
        std::memcpy(orig + 8, &h, 4);
    }
}

struct S_CmStream {
    uint32_t words[256] = {};
    C_NetMessage msg;
    S_CmStream() { msg.Attach(words, sizeof words * 8); }
};
std::string CmStreamText(const S_CmStream &s)
{
    std::string t = std::to_string(s.msg.BitPosition()) + ":";
    for (uint32_t i = 0; i < (s.msg.BitPosition() + 31) / 32; ++i)
        t += " " + std::to_string(s.words[i]);
    return t;
}

uint32_t FloatWord(float f)
{
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}
// A float near the edges the messages test (0.01 of the velocities, 1..5 of the speed, the 1e-8 of Magnitude).
float EdgeFloat()
{
    static const float EDGES[] = {0.0f, -0.0f, 0.01f, -0.01f, 0.0099999998f, 0.010000001f, 1.0f, 5.0f, 0.99999994f,
                                  1.0000001f, 1e-4f, 1e-5f, 0.5f, 255.0f, 255.9f, 256.0f, -1.0f, 4.9999995f, 5.0000005f};
    static const float CENTRES[] = {0.01f, 1.0f, 5.0f};
    if (P(0.5f))
        return EDGES[I(0, int(sizeof EDGES / sizeof EDGES[0]) - 1)];
    float f = CENTRES[I(0, 2)] * (P(0.5f) ? 1.0f : -1.0f);
    for (int k = I(0, 4); k > 0; --k)
        f = std::nextafter(f, P(0.5f) ? 10.0f : -10.0f);
    return f;
}
float RandomFloat()
{
    const int kind = I(0, 9);
    if (kind < 3)
        return EdgeFloat();
    if (kind < 8)
        return U(-60, 60);
    uint32_t w = (uint32_t(std::rand()) << 17) ^ (uint32_t(std::rand()) << 2) ^ uint32_t(std::rand());
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}
uint32_t RandomWord()
{
    const int kind = I(0, 20);
    if (kind == 20)                                     // (any bits: NaNs, signalling ones too)
        return (uint32_t(std::rand()) << 17) ^ (uint32_t(std::rand()) << 2) ^ uint32_t(std::rand());
    if (kind < 8)
        return uint32_t(I(-3, 12));
    if (kind < 12)
        return uint32_t(I(0, 1100));
    if (kind < 15)
        return uint32_t(I(-70000, 70000));
    return FloatWord(P(0.3f) ? EdgeFloat() : U(-60, 60));
}

const char *const CM_ANIMS[] = {"", "w", "salute", "anim\\sit_down_chair01", "a_very_long_animation_name_for_the_scripted_controller_to_play_on_the_clients_of_the_server_x",
                                "an_animation_name_of_exactly_one_hundred_and_twenty_seven_characters_which_fills_the_messages_buffer_to_its_last_byte_xxxxxxxx",
                                "an_animation_name_longer_than_the_messages_buffer_of_one_hundred_and_twenty_eight_bytes_which_the_reading_side_cuts_off_somewhere_x"};

// The fields of a message to write: words at random, then what each message's fields mean.
void RandomFields(const S_CmMessage &m)
{
    auto *b = reinterpret_cast<uint8_t *>(m.msg);
    for (size_t at = 8; at + 4 <= m.size; at += 4) {
        const uint32_t w = RandomWord();
        std::memcpy(b + at, &w, 4);
    }
    switch (m.msg->type) {
    case CTRL_MSG_HIT: {
        auto *h = static_cast<S_MsgHit *>(m.msg);
        h->hitType = P(0.6f) ? HIT_BULLET : I(-1, 15);
        const int kind = I(0, 5);
        // (Speeds under 2^31: above, the original's __ftol keeps the low word of its 64-bit result, the port's
        // conversion gives 0x80000000; no bullet flies so fast.)
        S_vector v(RandomFloat(), RandomFloat(), RandomFloat());
        if (!(v.Magnitude() < 1e6f))
            v = S_vector(U(-60, 60), P(0.5f) ? EdgeFloat() : U(-60, 60), U(-60, 60));
        if (kind == 0)
            v = S_vector(0.0f, 0.0f, 0.0f);
        else if (kind == 1)
            v = S_vector(EdgeFloat() * 1e-4f, 0.0f, EdgeFloat() * 1e-4f);
        else if (kind == 2) {
            const float a = U(-3.2f, 3.2f), c = U(-1.0f, 1.0f);
            v = S_vector(std::cos(a) * std::sqrt(1 - c * c), c, std::sin(a) * std::sqrt(1 - c * c));
        } else if (kind == 3)
            v = S_vector(U(-200, 200), U(-200, 200), U(-200, 200));
        else if (kind == 4) {
            // (A speed just below or above a whole number.)
            const float a = U(-3.2f, 3.2f), speed = float(I(1, 255)) + (P(0.5f) ? -1e-5f : 1e-5f);
            v = S_vector(std::cos(a) * speed, 0.0f, std::sin(a) * speed);
        }
        h->velocity.x = v.x;
        h->velocity.y = v.y;
        h->velocity.z = v.z;
        break;
    }
    case CTRL_MSG_DROP_ITEM: {
        auto *d = static_cast<S_MsgDropItem *>(m.msg);
        d->speed = P(0.5f) ? U(1.0f, 5.0f) : P(0.5f) ? EdgeFloat() : U(-20.0f, 20.0f);
        if (P(0.3f))
            d->speed = 1.0f + float(I(0, 15)) / 15.0f * 4.0f;
        break;
    }
    case CTRL_MSG_TAKE_FROM_BOX:
        if (P(0.4f))
            static_cast<S_MsgTakeFromBox *>(m.msg)->container = ITEM_CONTAINER_COUNT;
        break;
    case CTRL_MSG_INVENTORY_CHECK:
        static_cast<S_MsgInventoryCheck *>(m.msg)->human = g_h.human;
        RandomContainers();
        break;
    case CTRL_MSG_HEAL:
        static_cast<S_MsgHeal *>(m.msg)->removeItem = P(0.5f);
        break;
    case CTRL_MSG_ANIMATION: {
        auto *a = static_cast<S_MsgAnimation *>(m.msg);
        std::memset(a->name, 0, sizeof a->name);
        std::strncpy(a->name, CM_ANIMS[I(0, 5)], sizeof a->name - 1);
        a->loop = P(0.5f);
        break;
    }
    case CTRL_MSG_SAY:
        static_cast<S_MsgSay *>(m.msg)->force = P(0.5f);
        break;
    case CTRL_MSG_EQUIPMENT:
        static_cast<S_MsgEquipment *>(m.msg)->human = CM_TARGET_IDS[I(0, 2)];
        break;
    case CTRL_MSG_SUSPEND:
        static_cast<S_MsgSuspend *>(m.msg)->on = P(0.5f);
        break;
    case CTRL_MSG_23:
        static_cast<S_Msg23 *>(m.msg)->on = P(0.5f);
        break;
    default:
        break;
    }
}

void RandomScript()
{
    static const uint32_t ROLES[] = {GAME_ROLE_LOCAL, GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, 0, 3};
    g_gameRole = ROLES[I(0, 5)];
    for (int &t : g_cm.controllerType)
        t = P(0.6f) ? int(HUMAN_CONTROLLER_REMOTE) : I(0, 8);
    g_cm.health = I(-20, 120);
    g_cm.team = I(-1, 5);
}

// The words of the two objects that differ (the vtable is the port's alone), or "".
std::string ObjectsDiffer(const S_CmMessage &m, const uint8_t *port, const uint8_t *orig, const std::vector<size_t> &skip)
{
    alignas(16) uint8_t asOrig[0x400];
    ToOrigBytes(m, port, asOrig);
    for (const S_CmField &f : m.fields)
        for (size_t at = f.orig; at < f.orig + f.size; at += 4) {
            if (std::find(skip.begin(), skip.end(), at) != skip.end())
                continue;
            const size_t n = std::min<size_t>(4, f.orig + f.size - at);
            uint32_t a = 0, b = 0;
            std::memcpy(&a, asOrig + at, n);
            std::memcpy(&b, orig + at, n);
            if (a != b) {
                char text[96];
                std::snprintf(text, sizeof text, "original +0x%02zx: port %08x / original %08x", at, a, b);
                return text;
            }
        }
    return "";
}

std::map<std::string, long> g_cmFailures;
void Report(const std::string &what, const std::string &why)
{
    if (why.empty())
        return;
    if (++g_cmFailures[what] <= 3)
        std::printf("  %s: %s\n", what.c_str(), why.c_str());
}

void TestWrites()
{
    std::printf("ctrlmsg: Write\n");
    for (int i = 0; i < 60000 / QUICK; ++i) {
        const S_CmMessage &m = g_cmMessages[I(0, int(g_cmMessages.size()) - 1)];
        RandomScript();
        RandomFields(m);
        auto *port = reinterpret_cast<uint8_t *>(m.msg);
        std::vector<uint8_t> before(port, port + m.size);
        S_CmStream ps, os;
        g_h.human->m_containers = g_ccPortArray;
        g_cm.on = true;
        DiffCall((std::string("Write ") + m.name).c_str(), [&] { m.msg->Write(ps.msg); return 0; },
                 [&](void *) {
                     ToOrigBytes(m, before.data(), g_cmOrig);
                     S_CmContainer **containers = g_ccOrigArray;
                     std::memcpy(o_human + 0x74, &containers, 4);
                     typedef void(CM * T)(void *, void *);
                     OriginalAt<T>(m.write)(g_cmOrig, &os.msg);
                     return 0;
                 });
        g_cm.on = false;
        const std::string p = CmStreamText(ps), o = CmStreamText(os);
        std::string why = p == o ? "" : "bits: port " + p + " / original " + o;
        if (why.empty())
            why = ObjectsDiffer(m, port, g_cmOrig, {});
        if (!why.empty() && m.msg->type == CTRL_MSG_HIT && g_cmFailures[std::string("Write ") + m.name] < 3) {
            const auto *h = static_cast<const S_MsgHit *>(m.msg);
            std::printf("    velocity %.9g %.9g %.9g (%08x %08x %08x)\n", h->velocity.x, h->velocity.y, h->velocity.z,
                        FloatWord(h->velocity.x), FloatWord(h->velocity.y), FloatWord(h->velocity.z));
        }
        Report(std::string("Write ") + m.name, why);
        CHECK(why.empty());
    }
}

// A message's input: words at random, or what its Write would send (with edge values).
void RandomInput(const S_CmMessage &m, C_BitStream &w)
{
    // (Not the inventory check: a type out of the definitions' table crashes the original, which reads its name through
    // a null definition; the port reads none.)
    if (P(0.35f) && m.msg->type != CTRL_MSG_INVENTORY_CHECK) {
        for (int k = 0; k < 40; ++k)
            w.WriteBits(P(0.5f) ? (uint32_t(std::rand()) << 17) ^ (uint32_t(std::rand()) << 2) ^ uint32_t(std::rand())
                                : RandomWord(), 32);
        return;
    }
    switch (m.msg->type) {
    case CTRL_MSG_HIT:
        w.WriteBits(uint32_t(I(0, 1023)), 10);
        w.WriteBits(uint32_t(I(0, 15)), 4);
        w.WriteBits(uint32_t(I(0, 15)), 4);
        w.WriteBits(uint32_t(I(0, 0xFFFF)), 16);
        w.WriteBits(P(0.6f), 1);
        for (int k = 0; k < 6; ++k)
            PutFloat(&w, RandomFloat());
        w.WriteBits(uint32_t(I(0, 255)), 8);
        break;
    case CTRL_MSG_CONTROL_STATE:
        for (int k = 0; k < 9; ++k)
            PutFloat(&w, P(0.5f) ? EdgeFloat() : RandomFloat());
        w.WriteBits(uint32_t(I(0, 511)), 9);
        break;
    case CTRL_MSG_DROP_ITEM:
        w.WriteBits(uint32_t(I(0, 0xFFFF)), 16);
        for (int k = 0; k < 3; ++k)
            PutFloat(&w, RandomFloat());
        w.WriteBits(uint32_t(I(0, 15)), 4);
        w.WriteBits(P(0.5f), 1);
        w.WriteBits(uint32_t(I(0, 0xFFFF)), 16);
        break;
    case CTRL_MSG_INVENTORY_CHECK:
        for (int k = I(0, 8); k > 0; --k) {
            w.WriteBits(uint32_t(I(0, 6)), 3);
            w.WriteBits(uint32_t(I(0, CM_DEFS - 1)), 32);
            w.WriteBits(uint32_t(P(0.3f) ? I(-3, 3) : I(0, 0x7FFFFFFF)), 32);
        }
        w.WriteBits(7, 3);
        break;
    case CTRL_MSG_ANIMATION:
        w.WriteString(CM_ANIMS[I(0, 6)]);
        w.WriteBits(uint32_t(I(0, 0xFFFF)), 16);
        w.WriteBits(uint32_t(I(0, 0xFFFF)), 16);
        w.WriteBits(P(0.5f), 1);
        break;
    case CTRL_MSG_ACTION:
        for (int k = 0; k < 3; ++k)
            PutFloat(&w, RandomFloat());
        w.WriteBits(uint32_t(I(0, 63)), 6);
        for (int k = 0; k < 3; ++k)
            PutFloat(&w, RandomFloat());
        for (int k = 0; k < 4; ++k)
            w.WriteBits(RandomWord(), 32);
        break;
    case CTRL_MSG_EQUIPMENT:
        w.WriteBits(uint32_t(P(0.7f) ? CM_TARGET_IDS[I(0, 2)] : I(0, 0xFFFF)), 16);
        w.WriteBits(uint32_t(I(0, 255)), 8);
        w.WriteBits(uint32_t(I(0, 7)), 3);
        break;
    default:
        for (int k = 0; k < 40; ++k)
            w.WriteBits(RandomWord(), 32);
        break;
    }
    for (int k = 0; k < 8; ++k)
        w.WriteBits(uint32_t(std::rand()), 32);
}

void TestReads()
{
    std::printf("ctrlmsg: Read\n");
    for (int i = 0; i < 60000 / QUICK; ++i) {
        const S_CmMessage &m = g_cmMessages[I(0, int(g_cmMessages.size()) - 1)];
        RandomScript();
        // (The object's words before: at random, as left by an earlier message; the inventory check's human.)
        auto *port = reinterpret_cast<uint8_t *>(m.msg);
        for (size_t at = 8; at + 4 <= m.size; at += 4) {
            const uint32_t w = RandomWord();
            std::memcpy(port + at, &w, 4);
        }
        if (m.msg->type == CTRL_MSG_INVENTORY_CHECK)
            static_cast<S_MsgInventoryCheck *>(m.msg)->human = P(0.15f) ? nullptr : g_h.human;
        uint32_t input[256] = {};
        {
            C_BitStream w(input, sizeof input * 8);
            RandomInput(m, w);
        }
        std::vector<uint8_t> before(port, port + m.size);
        S_CmStream ps, os;
        std::memcpy(ps.words, input, sizeof input);
        std::memcpy(os.words, input, sizeof input);
        g_cm.on = true;
        DiffCall((std::string("Read ") + m.name).c_str(), [&] { m.msg->Read(ps.msg); return int(ps.msg.BitPosition()); },
                 [&](void *) {
                     ToOrigBytes(m, before.data(), g_cmOrig);
                     typedef void(CM * T)(void *, void *);
                     OriginalAt<T>(m.read)(g_cmOrig, &os.msg);
                     return int(os.msg.BitPosition());
                 });
        g_cm.on = false;
        const std::string why = ObjectsDiffer(m, port, g_cmOrig, m.skipAfterRead);
        Report(std::string("Read ") + m.name, why);
        CHECK(why.empty());
    }
}

// The control state's helpers on their own (the meleeTracker calls them): their results.
void TestControlStateHelpers()
{
    std::printf("ctrlmsg: WriteControlState, ReadControlState\n");
    for (int i = 0; i < 6000 / QUICK; ++i) {
        float v[3][4];
        for (auto &row : v)
            for (float &f : row)
                f = P(0.5f) ? EdgeFloat() : RandomFloat();
        const uint32_t bits = uint32_t(I(0, 1023));
        S_CmStream ps, os;
        g_cm.on = true;
        DiffCall("WriteControlState", [&] { return int(WriteControlState(&ps.msg, v[0], v[1], v[2], v[3], bits)); },
                 [&](void *) {
                     typedef bool(__fastcall * T)(void *, const float *, const float *, const float *, const void *, uint32_t);
                     return int(OriginalAt<T>(0x005ac5f0)(&os.msg, v[0], v[1], v[2], v[3], bits));
                 });
        g_cm.on = false;
        CHECK(CmStreamText(ps) == CmStreamText(os));
        // (Read back from what was written.)
        float pv[4][4], ov[4][4];
        std::memset(pv, 0x11, sizeof pv);
        std::memset(ov, 0x11, sizeof ov);
        uint32_t pbits = 5, obits = 5;
        ps.msg.Attach(ps.words, sizeof ps.words * 8);
        os.msg.Attach(os.words, sizeof os.words * 8);
        g_cm.on = true;
        DiffCall("ReadControlState", [&] { return int(ReadControlState(&ps.msg, pv[0], pv[1], pv[2], pv[3], &pbits)); },
                 [&](void *) {
                     typedef bool(__fastcall * T)(void *, float *, float *, float *, void *, uint32_t *);
                     return int(OriginalAt<T>(0x005ac660)(&os.msg, ov[0], ov[1], ov[2], ov[3], &obits));
                 });
        g_cm.on = false;
        bool same = pbits == obits;
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 3; ++k)
                same = same && std::memcmp(&pv[r][k], &ov[r][k], 4) == 0;
        if (!same)
            Report("ReadControlState", "values");
        CHECK(same);
    }
}

void TestCtrlMsg()
{
    g_skipFields.insert("m_containers");                // (each side's fake containers: compared through the bits)
    InstallCtrlMsgRedirects();
    MakeMessages();
    TestWrites();
    TestReads();
    TestControlStateHelpers();
    for (const auto &f : g_cmFailures)
        std::printf("  %s: %ld failures\n", f.first.c_str(), f.second);
}
}  // namespace
