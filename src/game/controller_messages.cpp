// The controllers' network messages (see controller_messages.h).
#include "game/controller_messages.h"

#include <cmath>
#include <cstring>

#include "game/actors_module.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/soldier.h"
#include "net/bitstream.h"

namespace {

// Field widths; the original computed each as floor(log2(n)) + 1 at run time.
constexpr int BITS_0_7 = 3;                             // log2(7)
constexpr int BITS_0_8 = 4;                             // log2(8)
constexpr int BITS_0_12 = 4;                            // log2(12)
constexpr int BITS_0_15 = 4;                            // log2(15)
constexpr int BITS_0_39 = 6;                            // log2(39)
constexpr int BITS_0_43 = 6;                            // log2(43)

// The largest value of n bits (0x00816388).
constexpr int MAX_OF_BITS[] = {0, 1, 3, 7, 15, 31, 63, 127};

// The inventory check's container codes.
constexpr int INVENTORY_END = 7;

int ReadShort(C_NetMessage &msg)
{
    return static_cast<int16_t>(msg.ReadBits(16));
}

uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

float BitsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void WriteType(C_NetMessage &msg, int type)
{
    msg.WriteBits(static_cast<uint32_t>(type), CTRL_MSG_TYPE_BITS);
}

// A soldier state as the equipment message starts it.
void NewSoldierState(S_SoldierState &state)
{
    state.spawnZone = 0x3FF;
    state.unknown34 = -1;
    state.unknown38 = -1;
    state.profileIndex = -1;
    state.team = -1;
    state.deployed = true;
    state.speechSet = -1;
}

S_vector XYZ(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

} // namespace

// --- the control state ----------------------------------------------------------------------

bool __fastcall WriteControlState(C_NetMessage *msg, const float *pos, const float *velocity,
                                  const float *dir, const void *unused, uint32_t bits)   // 0x005ac5f0
{
    (void)unused;
    msg->WriteQuantized(pos, msg->GetSender(-1), 7);
    msg->WriteQuantized(velocity, msg->GetSender(-1), 7);
    msg->WriteDirection3D(*reinterpret_cast<const S_vector *>(dir), -1);
    msg->WriteBits(bits, 9);
    return false;
}

bool __fastcall ReadControlState(C_NetMessage *msg, float *pos, float *velocity, float *dir,
                                 void *unused, uint32_t *bits)                          // 0x005ac660
{
    (void)unused;
    float value[3];
    msg->ReadQuantized(value, msg->GetSender(-1), 7);
    std::memcpy(pos, value, sizeof(value));
    msg->ReadQuantized(value, msg->GetSender(-1), 7);
    std::memcpy(velocity, value, sizeof(value));
    if (std::fabs(velocity[0]) < 0.01f && std::fabs(velocity[1]) < 0.01f && std::fabs(velocity[2]) < 0.01f) {
        velocity[0] = 0.0f;
        velocity[1] = 0.0f;
        velocity[2] = 0.0f;
    }
    S_vector d;
    msg->ReadDirection3D(d, -1);
    dir[0] = d.x;
    dir[1] = d.y;
    dir[2] = d.z;
    *bits = msg->ReadBits(9);
    return true;
}

// --- the messages -----------------------------------------------------------------------------

void S_MsgControls::Read(C_NetMessage &msg)             // 0x005b10c0
{
    (void)msg;
    std::memset(axes, 0, sizeof(axes));
    std::memset(buttons, 0, sizeof(buttons));
}

void S_MsgControls::Write(C_NetMessage &msg)            // 0x005b1100
{
    WriteType(msg, type);
}

void S_MsgHit::Read(C_NetMessage &msg)                  // 0x005b1510 (0x005b16e0)
{
    health = static_cast<int>(msg.ReadBits(10));
    hitType = static_cast<int>(msg.ReadBits(BITS_0_15));
    zone = static_cast<int>(msg.ReadBits(BITS_0_12));
    attacker = ReadShort(msg);
    if (msg.ReadBits(1)) {
        msg.ReadQuantized(&pos.x, msg.GetSender(2), 7);
        S_vector dir;
        msg.ReadDirection3D(dir, 3);
        const float speed = static_cast<float>(msg.ReadBits(8));
        SetXYZ(velocity, dir * speed);
    } else {
        SetXYZ(pos, S_vector(0.0f, 0.0f, 0.0f));
        SetXYZ(velocity, S_vector(1.0f, 0.0f, 0.0f));
    }
}

void S_MsgHit::Write(C_NetMessage &msg)                 // 0x005b1540
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(health), 10);
    msg.WriteBits(static_cast<uint32_t>(hitType), BITS_0_15);
    msg.WriteBits(static_cast<uint32_t>(zone), BITS_0_12);
    msg.WriteBits(static_cast<uint32_t>(attacker), 16);
    // Only bullets send where they hit and how fast they came.
    if (hitType != HIT_BULLET) {
        msg.WriteBits(0, 1);
        return;
    }
    msg.WriteBits(1, 1);
    msg.WriteQuantized(&pos.x, msg.GetSender(2), 7);
    const S_vector v = XYZ(velocity);
    msg.WriteDirection3D(v, 3);
    msg.WriteBits(static_cast<uint32_t>(static_cast<int>(v.Magnitude())), 8);
}

void S_MsgPutOnUniform::Read(C_NetMessage &msg)         // 0x005b1980 (0x005b19f0)
{
    uid = ReadShort(msg);
}

void S_MsgPutOnUniform::Write(C_NetMessage &msg)        // 0x005b19a0
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(uid), 16);
}

void S_MsgCommand::Read(C_NetMessage &msg)              // 0x005b1180 (0x005b1230)
{
    command = static_cast<int>(msg.ReadBits(BITS_0_39));
    actor = ReadShort(msg);
    arg = static_cast<int>(msg.ReadBits(8));
}

void S_MsgCommand::Write(C_NetMessage &msg)             // 0x005b11a0
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(command), BITS_0_39);
    msg.WriteBits(static_cast<uint32_t>(actor), 16);
    msg.WriteBits(static_cast<uint8_t>(arg), 8);
}

void S_MsgControlState::Read(C_NetMessage &msg)         // 0x005b1000 (0x005b1090)
{
    ReadControlState(&msg, &pos.x, &velocity.x, &dir.x, &unused40, &controlBits);
}

void S_MsgControlState::Write(C_NetMessage &msg)        // 0x005b1030
{
    WriteType(msg, type);
    WriteControlState(&msg, &pos.x, &velocity.x, &dir.x, &unused40, controlBits);
}

void S_MsgUseObject::Read(C_NetMessage &msg)            // 0x005b0f70 (0x005b0fe0)
{
    actor = static_cast<int>(msg.ReadBits(10));
}

void S_MsgUseObject::Write(C_NetMessage &msg)           // 0x005b0f90
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(actor), 10);
}

void S_MsgMoveItem::Read(C_NetMessage &msg)             // 0x005b1a10
{
    uid = ReadShort(msg);
    fromContainer = static_cast<int>(msg.ReadBits(BITS_0_7));
    toContainer = static_cast<int>(msg.ReadBits(BITS_0_7));
    slot = static_cast<int>(msg.ReadBits(BITS_0_15));
    other = msg.ReadBits(1) == 1 ? static_cast<int>(msg.ReadBits(10)) : -1;
}

void S_MsgMoveItem::Write(C_NetMessage &msg)            // 0x005b1ad0
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(uid), 16);
    msg.WriteBits(static_cast<uint32_t>(fromContainer), BITS_0_7);
    msg.WriteBits(static_cast<uint32_t>(toContainer), BITS_0_7);
    msg.WriteBits(static_cast<uint32_t>(slot), BITS_0_15);
    if (other >= 0) {
        msg.WriteBits(1, 1);
        msg.WriteBits(static_cast<uint32_t>(other), 10);
    } else {
        msg.WriteBits(0, 1);
    }
}

void S_MsgTransferItem::Read(C_NetMessage &msg)         // 0x005b1bb0
{
    uid = ReadShort(msg);
    otherUid = ReadShort(msg);
    arg = static_cast<int>(msg.ReadBits(BITS_0_7));
    container = static_cast<int>(msg.ReadBits(BITS_0_7));
    other = msg.ReadBits(1) == 1 ? static_cast<int>(msg.ReadBits(10)) : -1;
}

void S_MsgTransferItem::Write(C_NetMessage &msg)        // 0x005b1c40
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(uid), 16);
    msg.WriteBits(static_cast<uint32_t>(otherUid), 16);
    msg.WriteBits(static_cast<uint32_t>(arg), BITS_0_7);
    msg.WriteBits(static_cast<uint32_t>(container), BITS_0_7);
    if (other >= 0) {
        msg.WriteBits(1, 1);
        msg.WriteBits(static_cast<uint32_t>(other), 10);
    } else {
        msg.WriteBits(0, 1);
    }
}

void S_MsgPickUpItem::Read(C_NetMessage &msg)           // 0x005b1e40
{
    uid = ReadShort(msg);
    container = static_cast<int>(msg.ReadBits(BITS_0_7));
}

void S_MsgPickUpItem::Write(C_NetMessage &msg)          // 0x005b1e90
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(uid), 16);
    msg.WriteBits(static_cast<uint32_t>(container), BITS_0_7);
}

void S_MsgDropItem::Read(C_NetMessage &msg)             // 0x005b1f10
{
    uid = ReadShort(msg);
    S_vector d;
    msg.ReadDirection3D(d, -1);
    SetXYZ(dir, d);
    // The speed in 1..5, in 4 bits.
    const uint32_t value = msg.ReadBits(BITS_0_8);
    speed = (static_cast<float>(value) / MAX_OF_BITS[BITS_0_8]) * 4.0f + 1.0f;
    if (speed < 1.0f)
        speed = 1.0f;
    else if (speed > 5.0f)
        speed = 5.0f;
    actor = msg.ReadBits(1) ? ReadShort(msg) : -1;
}

void S_MsgDropItem::Write(C_NetMessage &msg)            // 0x005b2020
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(uid), 16);
    msg.WriteDirection3D(XYZ(dir), -1);
    msg.WriteBits(static_cast<uint32_t>(static_cast<int>((speed - 1.0f) * 0.25f * MAX_OF_BITS[BITS_0_8])),
                  BITS_0_8);
    if (actor >= 0) {
        msg.WriteBits(1, 1);
        msg.WriteBits(static_cast<uint32_t>(actor), 16);
    } else {
        msg.WriteBits(0, 1);
    }
}

void S_MsgTakeFromBox::Read(C_NetMessage &msg)          // 0x005b1d10
{
    box = static_cast<int>(msg.ReadBits(10));
    item = static_cast<int>(msg.ReadBits(4));
    if (msg.ReadBits(1)) {
        uid = ReadShort(msg);
        container = static_cast<int>(msg.ReadBits(BITS_0_7));
    }
}

void S_MsgTakeFromBox::Write(C_NetMessage &msg)         // 0x005b1d80
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(box), 10);
    msg.WriteBits(static_cast<uint32_t>(item), 4);
    if (container == ITEM_CONTAINER_COUNT) {
        msg.WriteBits(0, 1);
        return;
    }
    msg.WriteBits(1, 1);
    msg.WriteBits(static_cast<uint32_t>(uid), 16);
    msg.WriteBits(static_cast<uint32_t>(container), BITS_0_7);
}

void S_MsgAction::Read(C_NetMessage &msg)               // 0x005b1290
{
    pos.x = BitsFloat(msg.ReadBits(32));
    pos.y = BitsFloat(msg.ReadBits(32));
    pos.z = BitsFloat(msg.ReadBits(32));
    command.action = static_cast<int>(msg.ReadBits(BITS_0_43));
    command.dir.x = BitsFloat(msg.ReadBits(32));
    command.dir.y = BitsFloat(msg.ReadBits(32));
    command.dir.z = BitsFloat(msg.ReadBits(32));
    // Single player sends four more words (two of them pointers).
    command.command = -1;
    command.actor = nullptr;
    command.value = -1;
    command.member = nullptr;
    if (IsLocalGame()) {
        command.member = reinterpret_cast<C_Human *>(static_cast<uintptr_t>(msg.ReadBits(32)));
        command.actor = reinterpret_cast<C_Actor *>(static_cast<uintptr_t>(msg.ReadBits(32)));
        command.command = static_cast<int>(msg.ReadBits(32));
        command.value = static_cast<int>(msg.ReadBits(32));
    }
    command.category = 3;
}

void S_MsgAction::Write(C_NetMessage &msg)              // 0x005b13f0
{
    WriteType(msg, type);
    msg.WriteBits(FloatBits(pos.x), 32);
    msg.WriteBits(FloatBits(pos.y), 32);
    msg.WriteBits(FloatBits(pos.z), 32);
    msg.WriteBits(static_cast<uint32_t>(command.action), BITS_0_43);
    msg.WriteBits(FloatBits(command.dir.x), 32);
    msg.WriteBits(FloatBits(command.dir.y), 32);
    msg.WriteBits(FloatBits(command.dir.z), 32);
    if (IsLocalGame()) {
        msg.WriteBits(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command.member)), 32);
        msg.WriteBits(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command.actor)), 32);
        msg.WriteBits(static_cast<uint32_t>(command.command), 32);
        msg.WriteBits(static_cast<uint32_t>(command.value), 32);
    }
}

void S_MsgInventoryCheck::Read(C_NetMessage &msg)       // 0x005b2340
{
    static const char *const MISSING[INVENTORY_END] = {
        "%s DONT HAVE ITEM %s (%d) on his head !!!!!",
        "%s DONT HAVE ITEM %s (%d) on his holster !!!!!",
        "%s DONT HAVE ITEM %s (%d) in his shoulder !!!!!",
        "%s DONT HAVE ITEM %s (%d) in his hands !!!!!",
        "%s DONT HAVE ITEM %s (%d) as uniform  !!!!!",
        "%s DONT HAVE ITEM %s (%d) in his back !!!!!",
        "%s DONT HAVE ITEM %s (%d) in his backpack !!!!!",
    };
    for (;;) {
        const uint32_t code = msg.ReadBits(BITS_0_7);
        if (code == INVENTORY_END)
            return;
        const int typeId = static_cast<int>(msg.ReadBits(32));
        const int uid = static_cast<int>(msg.ReadBits(32));
        if (IsServer() || !human || human->FindItemByUid(uid))
            continue;
        // Fix: the original read the name of a type out of range through a null definition.
        const C_ItemDef *def = GetItemDef(typeId);
        GetGame()->Print(MISSING[code], human->GetFullName(), def ? def->GetDisplayName() : nullptr, uid);
    }
}

void S_MsgInventoryCheck::Write(C_NetMessage &msg)      // 0x005b2700
{
    WriteType(msg, type);
    auto writeItem = [&](int code, const C_Item *item) {
        msg.WriteBits(static_cast<uint32_t>(code), BITS_0_7);
        msg.WriteBits(static_cast<uint32_t>(item->GetTypeId()), 32);
        msg.WriteBits(static_cast<uint32_t>(item->GetUid()), 32);
    };
    // The single-item containers, then the belt's and the backpack's items.
    for (int container : {ITEM_CONTAINER_HEAD, ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_BACKPACK,
                          ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS}) {
        if (const C_Item *item = human->GetContainer(container)->GetItem())
            writeItem(container, item);
    }
    for (int container : {ITEM_CONTAINER_BELT, ITEM_CONTAINER_BACKPACK_CONTENTS}) {
        C_ItemContainer *store = human->GetContainer(container);
        if (!store)
            continue;                                   // (no backpack contents without a backpack)
        const std::vector<C_Item *> *slots = store->GetSlots();
        for (int i = 0; i < store->GetUsedSlotCount(); ++i) {
            for (const C_Item *item : slots[i])
                writeItem(container, item);
        }
    }
    msg.WriteBits(INVENTORY_END, BITS_0_7);
}

void S_MsgHeal::Read(C_NetMessage &msg)                 // 0x005b2100
{
    human = static_cast<int>(msg.ReadBits(10));
    itemUid = ReadShort(msg);
    health = static_cast<int>(msg.ReadBits(10));
    removeItem = msg.ReadBits(1) == 1;
}

void S_MsgHeal::Write(C_NetMessage &msg)                // 0x005b2150
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(human), 10);
    msg.WriteBits(static_cast<uint32_t>(itemUid), 16);
    msg.WriteBits(static_cast<uint32_t>(health), 10);
    msg.WriteBits(removeItem ? 1 : 0, 1);
}

void S_MsgRemoteRequest::Read(C_NetMessage &msg)        // 0x005b0ee0 (0x005b0f50) (DS 0x005c1c50 / DS 0x005c1cf0)
{
    request = static_cast<int>(msg.ReadBits(4));
}

void S_MsgRemoteRequest::Write(C_NetMessage &msg)       // 0x005b0f00
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(request), 4);
}

void S_MsgSoldierClass::Read(C_NetMessage &msg)         // 0x005b0e50 (0x005b0ec0) (DS 0x005c1b80 / DS 0x005c1c20)
{
    soldierClass = static_cast<int>(msg.ReadBits(4));
}

void S_MsgSoldierClass::Write(C_NetMessage &msg)        // 0x005b0e70
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(soldierClass), 4);
}

void S_Msg17::Read(C_NetMessage &msg)                   // 0x005b1800 (0x005b18f0)
{
    value10 = static_cast<int>(msg.ReadBits(10));
    value08 = static_cast<int>(msg.ReadBits(BITS_0_15));
    value0C = static_cast<int>(msg.ReadBits(BITS_0_12));
    value14 = static_cast<int>(msg.ReadBits(10));
}

void S_Msg17::Write(C_NetMessage &msg)                  // 0x005b1820
{
    WriteType(msg, type);
    // (Clamps the member itself.)
    if (value10 < 1)
        value10 = 0;
    else if (value10 > 0x3FF)
        value10 = 0x3FF;
    msg.WriteBits(static_cast<uint32_t>(value10), 10);
    msg.WriteBits(static_cast<uint32_t>(value08), BITS_0_15);
    msg.WriteBits(static_cast<uint32_t>(value0C), BITS_0_12);
    msg.WriteBits(static_cast<uint32_t>(value14), 10);
}

void S_MsgAnimation::Read(C_NetMessage &msg)            // 0x005b21d0
{
    msg.ReadString(name, sizeof(name));
    blendTime = ReadShort(msg);
    cancelTime = ReadShort(msg);
    loop = msg.ReadBits(1) == 1;
}

void S_MsgAnimation::Write(C_NetMessage &msg)           // 0x005b2230
{
    WriteType(msg, type);
    msg.WriteString(name);
    msg.WriteBits(static_cast<uint32_t>(blendTime), 16);
    msg.WriteBits(static_cast<uint32_t>(cancelTime), 16);
    msg.WriteBits(loop ? 1 : 0, 1);
}

void S_MsgSay::Read(C_NetMessage &msg)                  // 0x005b22b0
{
    sentence = static_cast<int>(msg.ReadBits(8));
    force = msg.ReadBits(1) != 0;
}

void S_MsgSay::Write(C_NetMessage &msg)                 // 0x005b22e0
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(sentence), 8);
    msg.WriteBits(force ? 1 : 0, 1);
}

void S_MsgEquipment::Read(C_NetMessage &msg)            // 0x005b2a90
{
    human = ReadShort(msg);
    auto *target = static_cast<C_Human *>(g_actorsModule.GetActorById(human));
    S_SoldierState state;
    NewSoldierState(state);
    NetReadSoldierState(&msg, state, true);
    if (target && target->GetControllerType() == HUMAN_CONTROLLER_REMOTE)
        target->CreateEquipment(&state);
}

void S_MsgEquipment::Write(C_NetMessage &msg)           // 0x005b2d20
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(human), 16);
    S_SoldierState state;
    NewSoldierState(state);
    // Fix: the original asked a missing human for its equipment.
    if (const auto *source = static_cast<const C_Human *>(g_actorsModule.GetActorById(human)))
        source->GetEquipmentState(state);
    NetWriteSoldierState(&msg, state, true);
}

void S_Msg21::Read(C_NetMessage &msg)                   // 0x005b2f80
{
    value = ReadShort(msg);
}

void S_Msg21::Write(C_NetMessage &msg)                  // 0x005b2fa0
{
    WriteType(msg, type);
    msg.WriteBits(static_cast<uint32_t>(value), 16);
}

void S_MsgSuspend::Read(C_NetMessage &msg)              // 0x005b2ff0 (DS 0x005c4710)
{
    on = msg.ReadBits(1) == 1;
}

void S_MsgSuspend::Write(C_NetMessage &msg)             // 0x005b3010
{
    WriteType(msg, type);
    msg.WriteBits(on ? 1 : 0, 1);
}

void S_Msg23::Read(C_NetMessage &msg)                   // 0x005b3060 (DS 0x005c47b0)
{
    on = msg.ReadBits(1) == 1;
}

void S_Msg23::Write(C_NetMessage &msg)                  // 0x005b3080
{
    WriteType(msg, type);
    msg.WriteBits(on ? 1 : 0, 1);
}
