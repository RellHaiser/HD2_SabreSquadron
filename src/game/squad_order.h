// The orders of a squad member's queue (C_SquadController, game/human_controller.h): what the
// player gives a soldier on the command map, shown there by a marker and the path to it (the
// original's "map commands", by the saved game's debug string).
//
// Original source 0x0058c540..0x0058d7e0 (the order) and 0x005825c0..0x00582640, 0x00583980
// (its pool; game/squad_order.cpp).
#pragma once

#include <cstdint>

#include "game/checkpoints.h"
#include "game/collision.h"
#include "ls3d/math.h"

class C_Actor;
class C_Human;
class I3D_frame;

// S_HumanOrder::kind (names partly inferred from the squad controller's handling).
enum E_HumanOrderKind {
    HUMAN_ORDER_MOVE = 1,                               // walk (or drive) to `pos`, facing `data`
    HUMAN_ORDER_WATCH = 2,                              // stand at `pos` watching along `data`
    HUMAN_ORDER_ACT = 3,                                // a squad command on `actor` (the module's, `arg`)
    HUMAN_ORDER_FOLLOW = 4,                             // follow the selected member
    HUMAN_ORDER_6 = 6,                                  // wait (the module ends it, EndOrderKind6)
    HUMAN_ORDER_7 = 7,
    HUMAN_ORDER_NONE = 8,                               // (saved games: no order)
};

// The collision volume of an order's marker (the command map picks the markers by its type).
constexpr uint32_t ORDER_VOLUME_SHAPE = 0x40;
constexpr uint32_t ORDER_VOLUME_TYPE = 0x8000;

// An order (vtable 0x008105a0, six empty slots; 0x130 bytes, 16-byte aligned; from the AI
// pool). The leading fields are a body command's (S_BodyCommand, type 1), which shares the
// pool's release (ReleaseHumanOrder) in the original.
struct alignas(16) S_HumanOrder {
    S_HumanOrder() = default;                           // (inline in the pool's growth 0x0058a3f0)
    ~S_HumanOrder();                                    // 0x0058c540
    S_HumanOrder(const S_HumanOrder &) = delete;
    S_HumanOrder &operator=(const S_HumanOrder &) = delete;

    // Sets the order up for `human` and puts its marker on the map (0x0058c5a0; the pool's
    // AllocHumanOrder): the flags are the pace, the stance and the AI mode ORed.
    void Init(C_Human *human, int kind, int arg, const S_vector4 &pos, const S_vector4 &data,
              uint32_t pace, uint32_t stance, uint32_t mode, C_Actor *actor, int unknown70);
    // The same without a marker (0x0058c860): the command map's changes of an order, passed to
    // C_Human::SetOrder (zero fields keep the order's).
    void InitPlain(C_Human *human, const S_vector4 &pos, const S_vector4 &data, uint32_t pace,
                   uint32_t stance, uint32_t mode);
    // Takes the marker off the map and frees the path (0x0058c8e0; the pool's release).
    void Release();
    // Takes the changes of `other` (a non-zero data / pos, non-zero flags) and moves the
    // marker (0x0058d720).
    void CopyFrom(const S_HumanOrder &other);
    // The map's path preview from `from` to the order (0x0058cce0, the command map's).
    void UpdatePath(const S_vector4 &from);
    // Where the order's marker stands.
    void UpdateMarker();                                // 0x0058caf0 (__fastcall)
    // Where the order on `actor` leads to.
    S_vector4 GetActorPoint(const C_Actor *actor) const;   // 0x0058c920
    // Takes the marker off the map (and the collision world).
    void RemoveMarker();                                // 0x0058d6c0 (__fastcall)

    // The flags are the pace, the stance and the AI mode the order asks for (0: the defaults).
    uint32_t GetPace() const { return flags & 0x1F000; }
    uint32_t GetStance() const { return flags & 7; }
    uint32_t GetMode() const { return flags & 0x700; }

    int type = 4;                                       // +0x04 4 (a body command's is 1)
    C_Human *human = nullptr;                           // +0x08 whose order it is
    int kind = 0;                                       // +0x0C E_HumanOrderKind
    int arg = 0;                                        // +0x10 (kind 3: the module's command; 0x20: follow)
    S_vector4 data = {};                                // +0x20 the direction
    S_vector4 pos = {};                                 // +0x30
    C_Actor *actor = nullptr;                           // +0x40 the actor the order is about
    uint32_t flags = 0;                                 // +0x44 MOVE_PACE_* | MOVE_STANCE_MASK | MOVE_AI_MODE_MASK
    bool done = false;                                  // +0x48
    bool started = false;                               // +0x49 its task is set
    bool selected = false;                              // +0x4A the command map's chosen order (the info selector)
    S_vector4 pathFrom = {100000.0f, 100000.0f, 100000.0f, 0.0f};   // +0x50 the path's start
    S_vector4 pathTo = {100000.0f, 100000.0f, 100000.0f, 0.0f};     // +0x60 the path's end
    int unknown70 = 0;                                  // +0x70 (kind 3: passed to the module)
    S_Vector4Array path;                                // +0x74 the path's points
    I3D_frame *marker = nullptr;                        // +0x84 the map's marker frame
    S_CollisionVolume volume;                           // +0x90 a 0.4 x 1.6 m box at the marker (picked on the map)
    S_matrix invMatrix = {};                            // +0xF0 the marker's inverse matrix
};

// The orders' pool (0x008ab170 in the AI pools 0x008ab158). AllocHumanOrder takes one and
// sets it up (0x005825c0).
S_HumanOrder *AllocHumanOrder(C_Human *human, int kind, int arg, const S_vector4 &pos, const S_vector4 &data,
                              uint32_t pace, uint32_t stance, uint32_t mode, C_Actor *actor, int unknown70);
