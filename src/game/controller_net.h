// The human controllers' messages of the human's net object (player_net.cpp; original source
// 0x0053b370..0x0053b990): the body actions of a client (C_HumanController::RequestAction), the
// dive and the binoculars, with the object's handlers for them and for the control bits.
#pragma once

#include <cstdint>

#include "game/pending.h"
#include "ls3d/math.h"

class C_BitStream;

// The kinds of the messages (C_NetObjectMessage::type; 2 bits).
enum : int {
    NET_MSG_ACTION = 0,
    NET_MSG_DIVE = 1,
    NET_MSG_BINOCULARS = 2,
    NET_MSG_KINDS = 3,                                  // what the factory writes when asked
};

// A body action (vtable 0x00810be8, 0x70 bytes): E_ControllerAction with what it needs.
class C_NetActionMessage : public C_NetObjectMessage {
public:
    C_NetActionMessage() : C_NetObjectMessage(NET_MSG_ACTION) {}
    // The action (5 bits); a reload's magazine and rounds; the fire actions' direction (raw),
    // position (quantized by the sender), shot interval and throw strength (4 bits).
    void Write(C_BitStream *stream) override;           // 0x0053b370
    void Read(C_BitStream *stream) override;            // 0x0053b510

    S_vector4 dir = {};                                 // +0x30
    S_vector4 pos = {};                                 // +0x40
    int shotInterval = 0;                               // +0x50 the weapon's / 100 (255 at most)
    float strength = 0.0f;                              // +0x54 0..1
    int ammoUid = 0;                                    // +0x58
    uint8_t rounds = 0;                                 // +0x5C
    int action = 0;                                     // +0x60 E_ControllerAction
};

// The dive (vtable 0x00810c10, 0x30 bytes; nothing but the kind).
class C_NetDiveMessage : public C_NetObjectMessage {
public:
    C_NetDiveMessage() : C_NetObjectMessage(NET_MSG_DIVE) {}
    void Write(C_BitStream *stream) override;           // 0x0053b6a0
    void Read(C_BitStream * /*stream*/) override {}     // 0x0053b6e0
};

// The binoculars taken out / put away (vtable 0x00810c38, 0x34 bytes).
class C_NetBinocularsMessage : public C_NetObjectMessage {
public:
    C_NetBinocularsMessage() : C_NetObjectMessage(NET_MSG_BINOCULARS) {}
    void Write(C_BitStream *stream) override;           // 0x0053b6f0
    void Read(C_BitStream *stream) override;            // 0x0053b740
    bool on = false;                                    // +0x30
};

// A message popped from the human's net object that was not queued again: deleted unless the
// object still holds it (C_NetObjectMessage::unknown24 1 or more).
inline void ReleaseNetMessage(C_NetObjectMessage *message)
{
    if (message->unknown24 < 1)
        delete message;
}

// The human's net object's handlers (the player, AI and remote controllers set them): the
// message factory (+0x64) and the control bits read / written (into / from the controller's
// +0x54, 9 bits).
C_NetObjectMessage *__fastcall NetCreateMessage(void *controller, C_BitStream *stream, bool write);   // 0x0053b760
void __fastcall NetReadControlBits(void *controller, C_BitStream *stream);   // 0x0053b970
void __fastcall NetWriteControlBits(void *controller, C_BitStream *stream);  // 0x0053b990
