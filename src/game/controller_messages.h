// The messages a human's controller sends between the server and the clients (original unit
// 0x005b0e20..0x005b30d0): inventory requests, hits, speech, the controls' state ...
//
// A controller queues them (C_HumanController's two vectors: to the server, to the clients)
// and its network update writes the queue into the human's message, ending it with
// CTRL_MSG_END; the receiving controller reads them one by one, switching on the type.
// Every message starts with its type in CTRL_MSG_TYPE_BITS bits.
#pragma once

#include <cstdint>

#include "ls3d/math.h"

class C_Actor;
class C_Human;
class C_NetMessage;

enum E_ControllerMessage {
    CTRL_MSG_CONTROLS = 0,                              // carries nothing (two cleared arrays)
    CTRL_MSG_HIT = 1,
    CTRL_MSG_PUT_ON_UNIFORM = 2,
    CTRL_MSG_COMMAND = 3,                               // an S_HumanCommand of category 2
    CTRL_MSG_CONTROL_STATE = 4,                         // position, velocity, direction, control bits
    CTRL_MSG_USE_OBJECT = 6,
    CTRL_MSG_MOVE_ITEM = 7,
    CTRL_MSG_TRANSFER_ITEM = 8,
    CTRL_MSG_PICK_UP_ITEM = 9,
    CTRL_MSG_DROP_ITEM = 10,
    CTRL_MSG_TAKE_FROM_BOX = 11,
    CTRL_MSG_ACTION = 12,                               // a body action (C_HumanController::RequestAction)
    CTRL_MSG_INVENTORY_CHECK = 13,
    CTRL_MSG_HEAL = 14,
    CTRL_MSG_REMOTE_REQUEST = 15,
    CTRL_MSG_SOLDIER_CLASS = 16,                        // multiplayer: the soldier's class
    CTRL_MSG_17 = 17,
    CTRL_MSG_ANIMATION = 18,
    CTRL_MSG_SAY = 19,
    CTRL_MSG_EQUIPMENT = 20,
    CTRL_MSG_21 = 21,
    CTRL_MSG_SUSPEND = 22,
    CTRL_MSG_23 = 23,
    CTRL_MSG_END = 24,                                  // ends a controller's messages
};
// Bits of the type (the original computed floor(log2(25)) + 1 each time).
constexpr int CTRL_MSG_TYPE_BITS = 5;

// Base of the messages (vtable 0x00810c7c, both slots pure).
struct alignas(16) S_ControllerMessage {
    explicit S_ControllerMessage(int type_) : type(type_) {}
    // (The original had no destructor: the messages were _aligned_free'd.)
    virtual ~S_ControllerMessage() = default;
    S_ControllerMessage(const S_ControllerMessage &) = delete;
    S_ControllerMessage &operator=(const S_ControllerMessage &) = delete;

    // Reads the message after its type.
    virtual void Read(C_NetMessage &msg) = 0;           // slot 0
    // Writes the type, then the message.
    virtual void Write(C_NetMessage &msg) = 0;          // slot 1

    int type;                                           // +0x04 E_ControllerMessage
};

// CTRL_MSG_CONTROLS (vtable 0x00810cc4): reading clears the arrays, nothing is sent.
struct S_MsgControls : S_ControllerMessage {
    S_MsgControls() : S_ControllerMessage(CTRL_MSG_CONTROLS) {}
    void Read(C_NetMessage &msg) override;              // 0x005b10c0
    void Write(C_NetMessage &msg) override;             // 0x005b1100
    float axes[64] = {};                                // +0x008
    float buttons[64] = {};                             // +0x108
};

// CTRL_MSG_HIT (vtable 0x00810c84): damage taken, for C_Human::ApplyRemoteHit.
struct S_MsgHit : S_ControllerMessage {
    S_MsgHit() : S_ControllerMessage(CTRL_MSG_HIT) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1510 (0x005b16e0)
    void Write(C_NetMessage &msg) override;             // 0x005b1540
    int hitType = 0;                                    // +0x08 E_HitType (4 bits)
    int zone = 0;                                       // +0x0C (4 bits)
    int health = 0;                                     // +0x10 what is left (10 bits)
    int attacker = 0;                                   // +0x14 actor id (16 bits)
    S_vector4 pos = {};                                 // +0x20 bullets only
    S_vector4 velocity = {};                            // +0x30 bullets only (direction and a byte's speed)
};

// CTRL_MSG_PUT_ON_UNIFORM (vtable 0x00810c8c).
struct S_MsgPutOnUniform : S_ControllerMessage {
    S_MsgPutOnUniform() : S_ControllerMessage(CTRL_MSG_PUT_ON_UNIFORM) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1980 (0x005b19f0)
    void Write(C_NetMessage &msg) override;             // 0x005b19a0
    int uid = 0;                                        // +0x08 (16 bits)
};

// CTRL_MSG_COMMAND (vtable 0x00810c6c): an interaction command (S_HumanCommand category 2).
struct S_MsgCommand : S_ControllerMessage {
    S_MsgCommand() : S_ControllerMessage(CTRL_MSG_COMMAND) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1180 (0x005b1230)
    void Write(C_NetMessage &msg) override;             // 0x005b11a0
    int command = 0;                                    // +0x08 command type (6 bits)
    int actor = 0;                                      // +0x0C actor id (16 bits)
    int arg = 0;                                        // +0x10 (8 bits; the seat)
};

// CTRL_MSG_CONTROL_STATE (vtable 0x00810cbc).
struct S_MsgControlState : S_ControllerMessage {
    S_MsgControlState() : S_ControllerMessage(CTRL_MSG_CONTROL_STATE) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1000 (0x005b1090)
    void Write(C_NetMessage &msg) override;             // 0x005b1030
    S_vector4 pos = {};                                 // +0x10
    S_vector4 velocity = {};                            // +0x20
    S_vector4 dir = {};                                 // +0x30
    S_vector4 unused40 = {};                            // +0x40 (passed to the helpers, not used)
    uint32_t controlBits = 0;                           // +0x50 EncodeControlBits (9 bits)
};

// CTRL_MSG_USE_OBJECT (vtable 0x00810cfc).
struct S_MsgUseObject : S_ControllerMessage {
    S_MsgUseObject() : S_ControllerMessage(CTRL_MSG_USE_OBJECT) {}
    void Read(C_NetMessage &msg) override;              // 0x005b0f70 (0x005b0fe0)
    void Write(C_NetMessage &msg) override;             // 0x005b0f90
    int actor = 0;                                      // +0x08 actor id (10 bits)
};

// CTRL_MSG_MOVE_ITEM (vtable 0x00810ca4).
struct S_MsgMoveItem : S_ControllerMessage {
    S_MsgMoveItem() : S_ControllerMessage(CTRL_MSG_MOVE_ITEM) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1a10
    void Write(C_NetMessage &msg) override;             // 0x005b1ad0
    int uid = 0;                                        // +0x08 (16 bits)
    int other = -1;                                     // +0x0C actor id (10 bits; -1 none)
    int fromContainer = 0;                              // +0x10 (3 bits)
    int toContainer = 0;                                // +0x14 (3 bits)
    int slot = 0;                                       // +0x18 (4 bits)
};

// CTRL_MSG_TRANSFER_ITEM (vtable 0x00810cac).
struct S_MsgTransferItem : S_ControllerMessage {
    S_MsgTransferItem() : S_ControllerMessage(CTRL_MSG_TRANSFER_ITEM) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1bb0
    void Write(C_NetMessage &msg) override;             // 0x005b1c40
    int uid = 0;                                        // +0x08 (16 bits)
    int arg = 0;                                        // +0x0C (3 bits)
    int other = -1;                                     // +0x10 actor id (10 bits; -1 none)
    int otherUid = 0;                                   // +0x14 (16 bits)
    int container = 0;                                  // +0x18 (3 bits)
};

// CTRL_MSG_PICK_UP_ITEM (vtable 0x00810c9c).
struct S_MsgPickUpItem : S_ControllerMessage {
    S_MsgPickUpItem() : S_ControllerMessage(CTRL_MSG_PICK_UP_ITEM) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1e40
    void Write(C_NetMessage &msg) override;             // 0x005b1e90
    int uid = 0;                                        // +0x08 (16 bits)
    int container = 0;                                  // +0x0C (3 bits)
};

// CTRL_MSG_DROP_ITEM (vtable 0x00810cb4).
struct S_MsgDropItem : S_ControllerMessage {
    S_MsgDropItem() : S_ControllerMessage(CTRL_MSG_DROP_ITEM) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1f10
    void Write(C_NetMessage &msg) override;             // 0x005b2020
    int uid = 0;                                        // +0x08 (16 bits)
    S_vector4 dir = {};                                 // +0x10
    float speed = 0.0f;                                 // +0x20 1..5 (4 bits)
    int actor = -1;                                     // +0x24 the dropped item's actor (16 bits; -1 none)
};

// CTRL_MSG_TAKE_FROM_BOX (vtable 0x00810c94).
struct S_MsgTakeFromBox : S_ControllerMessage {
    S_MsgTakeFromBox() : S_ControllerMessage(CTRL_MSG_TAKE_FROM_BOX) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1d10
    void Write(C_NetMessage &msg) override;             // 0x005b1d80
    int box = 0;                                        // +0x08 actor id (10 bits)
    int item = 0;                                       // +0x0C (4 bits)
    int uid = 0;                                        // +0x10 (16 bits; sent with a container)
    int container = 0;                                  // +0x14 (3 bits; 7: none)
};

// The squad commands of the HUD (S_ActionCommand::action): the player says them (the player
// controller), the squad's command module carries them out (C_SquadCommander; single
// player). Most come in pairs, said aloud and whispered (the members sneak). `dir` is the
// command's direction or point.
enum E_SquadCommand {
    SQUAD_COMMAND_FOLLOW = 0x00,                        // follow the selected soldier
    SQUAD_COMMAND_FOLLOW_QUIET = 0x01,
    SQUAD_COMMAND_STOP = 0x02,                          // drop the action and settle there
    SQUAD_COMMAND_STOP_QUIET = 0x03,
    SQUAD_COMMAND_BOUND = 0x04,                         // hop from cover to cover along `dir`
    SQUAD_COMMAND_BOUND_QUIET = 0x05,
    SQUAD_COMMAND_HOLD = 0x06,                          // hold the position in cover
    SQUAD_COMMAND_HOLD_QUIET = 0x07,
    SQUAD_COMMAND_TAKE_COVER = 0x08,                    // take cover against `dir`
    SQUAD_COMMAND_TAKE_COVER_QUIET = 0x09,
    SQUAD_COMMAND_MOVE_NEAR = 0x0A,                     // go to places around the point `dir`
    SQUAD_COMMAND_MOVE_NEAR_QUIET = 0x0B,
    // 0x0C..0x11: the combat orders (E_SquadCombatOrder).
    SQUAD_COMMAND_ENGAGE = 0x12,                        // fire at what is seen around the point
    SQUAD_COMMAND_THROW = 0x13,                         // a grenade at the point
    SQUAD_COMMAND_FIRE_LAUNCHER = 0x14,                 // a launcher at the point
    SQUAD_COMMAND_FORMATION_0 = 0x15,                   // the selected soldier's formation's type 0
    SQUAD_COMMAND_FORMATION_1 = 0x16,                   // ... type 1
    SQUAD_COMMAND_FORMATION_FOLLOW = 0x17,              // ... FORMATION_FOLLOW
    SQUAD_COMMAND_SPACING_LESS = 0x18,                  // the formation's spacing
    SQUAD_COMMAND_SPACING_MORE = 0x19,
    SQUAD_COMMAND_FOLLOW_ME = 0x1A,                     // follow the selected soldier (C_SquadController::FollowSelected)
    // The commands to the members with orders (the order queue).
    SQUAD_COMMAND_END_ORDER_6 = 0x1B,                   // the first order is done if of kind 6
    SQUAD_COMMAND_END_ORDER_7 = 0x1C,                   // ... of kind 7
    SQUAD_COMMAND_HOLD_ORDERS = 0x1D,                   // the orders wait
    SQUAD_COMMAND_RESUME_ORDERS = 0x1E,
    SQUAD_COMMAND_CANCEL_ORDERS = 0x1F,
    SQUAD_COMMAND_FOLLOW_ME_ORDER = 0x20,               // follow the selected soldier as the queue's order (kind 4)
    // The radios' targets of the heavy fire support (the player controller).
    SQUAD_COMMAND_AIR_ATTACK = 0x21,
    SQUAD_COMMAND_ARTILLERY = 0x22,
    // A command on an actor (S_ActionCommand::command, E_HumanCommandType; its sentence by it).
    SQUAD_COMMAND_ON_ACTOR = 0x35,
    SQUAD_COMMAND_ON_ACTOR_QUIET = 0x38,
};

// The body action of CTRL_MSG_ACTION: a command of category 3 (vtable 0x00810528, the no-op
// methods of S_HumanCommand). The last four fields only travel in local games (single
// player): the pointers as this process's 32-bit values.
struct S_ActionCommand {
    int category = 3;                                   // +0x04 (of the message: +0x24)
    int unknown08 = 0;
    int unknown0C = 0;
    S_vector4 dir = {};                                 // +0x10 the direction or the point
    int action = 0;                                     // +0x20 E_SquadCommand (6 bits)
    int command = 0;                                    // +0x24 SQUAD_COMMAND_ON_ACTOR's E_HumanCommandType
    C_Actor *actor = nullptr;                           // +0x28 ... its actor
    int value = 0;                                      // +0x2C ... its value (a supply box's item)
    C_Human *member = nullptr;                          // +0x30 the member the command is for (null: the squad)
};

// CTRL_MSG_ACTION (vtable 0x00810c74).
struct S_MsgAction : S_ControllerMessage {
    S_MsgAction() : S_ControllerMessage(CTRL_MSG_ACTION) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1290
    void Write(C_NetMessage &msg) override;             // 0x005b13f0
    S_vector4 pos = {};                                 // +0x10
    S_ActionCommand command;                            // +0x20
};

// CTRL_MSG_INVENTORY_CHECK (vtable 0x00810cf4): the items the human carries, by container;
// a client that misses one only prints it (C_game::Print, empty in the release build).
struct S_MsgInventoryCheck : S_ControllerMessage {
    S_MsgInventoryCheck() : S_ControllerMessage(CTRL_MSG_INVENTORY_CHECK) {}
    void Read(C_NetMessage &msg) override;              // 0x005b2340
    void Write(C_NetMessage &msg) override;             // 0x005b2700
    C_Human *human = nullptr;                           // +0x08
};

// CTRL_MSG_HEAL (vtable 0x00810d04): C_HumanController::Heal on the other side.
struct S_MsgHeal : S_ControllerMessage {
    S_MsgHeal() : S_ControllerMessage(CTRL_MSG_HEAL) {}
    void Read(C_NetMessage &msg) override;              // 0x005b2100
    void Write(C_NetMessage &msg) override;             // 0x005b2150
    int human = 0;                                      // +0x08 actor id (10 bits)
    int itemUid = 0;                                    // +0x0C (16 bits)
    int health = 0;                                     // +0x10 (10 bits)
    bool removeItem = false;                            // +0x14
};

// CTRL_MSG_REMOTE_REQUEST (vtable 0x00810d0c): C_HumanController::SendRemoteRequest.
struct S_MsgRemoteRequest : S_ControllerMessage {
    S_MsgRemoteRequest() : S_ControllerMessage(CTRL_MSG_REMOTE_REQUEST) {}
    void Read(C_NetMessage &msg) override;              // 0x005b0ee0 (0x005b0f50)
    void Write(C_NetMessage &msg) override;             // 0x005b0f00
    int request = 0;                                    // +0x08 E_RemoteRequest (4 bits)
};

// CTRL_MSG_SOLDIER_CLASS (vtable 0x00810d14).
struct S_MsgSoldierClass : S_ControllerMessage {
    S_MsgSoldierClass() : S_ControllerMessage(CTRL_MSG_SOLDIER_CLASS) {}
    void Read(C_NetMessage &msg) override;              // 0x005b0e50 (0x005b0ec0)
    void Write(C_NetMessage &msg) override;             // 0x005b0e70
    int soldierClass = 0;                               // +0x08 (4 bits)
};

// CTRL_MSG_17 (vtable 0x00810d1c).
struct S_Msg17 : S_ControllerMessage {
    S_Msg17() : S_ControllerMessage(CTRL_MSG_17) {}
    void Read(C_NetMessage &msg) override;              // 0x005b1800 (0x005b18f0)
    void Write(C_NetMessage &msg) override;             // 0x005b1820
    int value08 = 0;                                    // +0x08 (4 bits)
    int value0C = 0;                                    // +0x0C (4 bits)
    int value10 = 0;                                    // +0x10 (10 bits; clamped to 0..1023 when written)
    int value14 = 0;                                    // +0x14 (10 bits)
};

// CTRL_MSG_ANIMATION (vtable 0x00810d24): C_ScriptedController::SetAnimation.
struct S_MsgAnimation : S_ControllerMessage {
    S_MsgAnimation() : S_ControllerMessage(CTRL_MSG_ANIMATION) {}
    void Read(C_NetMessage &msg) override;              // 0x005b21d0
    void Write(C_NetMessage &msg) override;             // 0x005b2230
    char name[0x80] = {};                               // +0x08
    int blendTime = 0;                                  // +0x88 (16 bits)
    int cancelTime = 0;                                 // +0x8C (16 bits)
    bool loop = false;                                  // +0x90
};

// CTRL_MSG_SAY (vtable 0x00810cec): C_HumanController::Say.
struct S_MsgSay : S_ControllerMessage {
    S_MsgSay() : S_ControllerMessage(CTRL_MSG_SAY) {}
    void Read(C_NetMessage &msg) override;              // 0x005b22b0
    void Write(C_NetMessage &msg) override;             // 0x005b22e0
    int sentence = 0;                                   // +0x08 (8 bits)
    bool force = false;                                 // +0x0C
};

// CTRL_MSG_EQUIPMENT (vtable 0x00810ce4): a human's equipment (S_SoldierState), applied to a
// human under a remote controller.
struct S_MsgEquipment : S_ControllerMessage {
    S_MsgEquipment() : S_ControllerMessage(CTRL_MSG_EQUIPMENT) {}
    void Read(C_NetMessage &msg) override;              // 0x005b2a90
    void Write(C_NetMessage &msg) override;             // 0x005b2d20
    int human = -1;                                     // +0x08 actor id (16 bits)
};

// CTRL_MSG_21 (vtable 0x00810ccc).
struct S_Msg21 : S_ControllerMessage {
    S_Msg21() : S_ControllerMessage(CTRL_MSG_21) {}
    void Read(C_NetMessage &msg) override;              // 0x005b2f80
    void Write(C_NetMessage &msg) override;             // 0x005b2fa0
    int value = 0;                                      // +0x08 (16 bits)
};

// CTRL_MSG_SUSPEND (vtable 0x00810cdc): C_ScriptedController::Suspend.
struct S_MsgSuspend : S_ControllerMessage {
    S_MsgSuspend() : S_ControllerMessage(CTRL_MSG_SUSPEND) {}
    void Read(C_NetMessage &msg) override;              // 0x005b2ff0
    void Write(C_NetMessage &msg) override;             // 0x005b3010
    bool on = false;                                    // +0x08
};

// CTRL_MSG_23 (vtable 0x00810cd4).
struct S_Msg23 : S_ControllerMessage {
    S_Msg23() : S_ControllerMessage(CTRL_MSG_23) {}
    void Read(C_NetMessage &msg) override;              // 0x005b3060
    void Write(C_NetMessage &msg) override;             // 0x005b3080
    bool on = false;                                    // +0x08
};

// The control state of a human as sent over the network (CTRL_MSG_CONTROL_STATE, the meleeTracker's
// messages): position and velocity quantised with the sender's settings, the direction,
// 9 control bits. Reading drops velocities under 0.01 in every axis.
bool __fastcall WriteControlState(C_NetMessage *msg, const float *pos, const float *velocity,
                                  const float *dir, const void *unused, uint32_t bits);   // 0x005ac5f0
bool __fastcall ReadControlState(C_NetMessage *msg, float *pos, float *velocity, float *dir,
                                 void *unused, uint32_t *bits);                          // 0x005ac660
