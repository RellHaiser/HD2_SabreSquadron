// A static weapon (C_StaticWeapon, actor type 21, vtable 0x00813130, 0x300 bytes; original
// 0x00602a10..0x00606250): a gun on a mount (a machine gun, a flak gun, a cannon) with the seats
// "SEAT00", "SEAT01", ... and one seat weapon (its model's "HORT00" / "VERT00" ... parts, the
// C_SeatWeapon at +0x160) worked from seat 0. Its states: 0 hidden, 1 usable, 2 destroyed (the
// parts take the meshes of the model "C<model>"), 3 not started. Its box (COLLISION_TYPE_PLACE)
// is in the world while it is usable or destroyed. Network games: the server refills the store
// with ammunition every 30 s (15 s for a launcher) and tells the clients the store and the state.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "game/collision.h"
#include "game/seat_weapon.h"
#include "ls3d/math.h"

class C_BitStream;
class C_NetObject;
class I3D_model;

class alignas(16) C_StaticWeapon : public C_Actor {     // "StaticWeapon", vtable 0x00813130
public:
    C_StaticWeapon();                                   // 0x00602ec0
    // (The model for the destroyed state is not released: the original's.)
    ~C_StaticWeapon() override = default;               // 0x00602fc0 (scalar deleting 0x00602fa0)

    // The state (STATIC_WEAPON_*) the script sets: the clients are told; destroyed, everyone in
    // the seats is killed (an explosion's hit at the weapon).
    void SetState(int state);                           // 0x006059b0
    int GetState() const { return m_state; }            // +0x2F0
    // The exit point; without one, a metre behind the weapon (`w` left alone).
    void GetExitPosUnused(S_vector4 *out) const;        // 0x006056d0 (DS 0x0062c810)

    int GetHitPoints() override { return 100; }         // 0x10 0x00602f60 (DS 0x006295f0)
    // The messages (network games), the seat weapon; manned, the model's animation and the box.
    void Update(int frameTime) override;                // 0x14 0x006034d0
    // The messages (not a pure client), the seat weapon; network games: the store sent when it
    // changed (the server), the ammunition refilled.
    void Tick(int timeDelta) override;                  // 0x18 0x00603070
    // Kind 0: the store (added unless a server reads it), 1: the state.
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x00605030
    void Unknown20(C_BitStream *) override {}           // 0x20 0x006050a0
    // A frame only (null: false), then the base's.
    bool SetFrame(I3D_frame *frame) override { return frame && C_Actor::SetFrame(frame); }   // 0x2C 0x00603040 (DS 0x00629720)
    bool WriteProperties(C_chunk &) override { return true; }   // 0x30 0x00604cd0 (DS 0x0062bad0)
    bool ReadProperties(C_chunk &) override { return true; }    // 0x34 0x00604ce0 (DS 0x0062baf0)
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x00603060
    // Once until StopMission: the net object, the seats, the seat weapon (its text becomes the
    // actor's name), the box, the way out, the model of the destroyed state; usable.
    void StartMission(bool server) override;            // 0x3C 0x006036d0
    // The seats forgotten, the seat weapon stopped; not started any more.
    void StopMission(bool server) override;             // 0x40 0x00604120
    // Everyone out of the seats, not started, the seat weapon restarted; usable on the server.
    void RestartMission(bool server) override;          // 0x44 0x006050b0
    // (Not the base's.) Chunk 1000 the state, 0x3EA the store, 0x3E9 the seat weapon.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x00605120
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x006053c0
    // A joining client's: the state, the store.
    void WriteState(C_BitStream *msg) override;         // 0x50 0x00603520
    void ReadState(C_BitStream *msg) override;          // 0x54 0x00603560

    bool IsSeatBlocked(int seat) override;              // 0x6C 0x00604a90 (true out of range)
    bool SetSeatBlocked(int seat, bool blocked) override;   // 0x70 0x00604ad0
    // Into a free seat (taken: nothing): linked to the seat's dummy, facing along it; the seat
    // weapon's gunner, the frame's script told; the server sends the rounds.
    void SetSeatOccupant(int seat, C_Actor *occupant) override;   // 0x74 0x006041a0
    C_Actor *GetSeatOccupant(int seat) override;        // 0x78 0x00604590
    int FindSeat(const C_Actor *occupant) override;     // 0x7C 0x006045e0
    // Out of the seat where it is (into the primary sector), the seat free.
    C_Actor *LeaveSeat(int seat) override;              // 0x80 0x00604630
    // Out to the way out (the walking point at the weapon, else 1 m behind it), facing along it.
    bool ExitSeat(int seat) override;                   // 0x84 0x006046d0
    int GetSeatCount() override { return static_cast<int>(m_seats.size()); }   // 0x88 0x00602f70
    // The digits after "SEAT" in the dummy's user properties.
    uint32_t GetSeatFlags(int seat) override;           // 0x8C 0x00604a10
    // The "cameraFPV*" (first person) or "camera3RD*" dummy under the seat's dummy, else under
    // its parent.
    I3D_frame *GetSeatCamera(int seat) override;        // 0x90 0x00604b70
    // The number after "name" in the dummy's user properties (-1: none).
    int GetSeatText(int seat) override;                 // 0x94 0x00604b10
    bool HasSeatWeapon(int seat) override { return seat == 0; }   // 0x98 0x00604d20
    // (The rest ask the seat weapon whatever the seat.)
    int GetSeatWeaponRounds(int) override { return m_weapon.GetRounds(); }   // 0x9C 0x00604d30 (DS 0x0062bb70)
    S_vector GetSeatWeaponAim(int) override { return m_weapon.GetAim(); }     // 0xA4 0x00604d70
    S_vector GetSeatWeaponOrigin(int) override { return m_weapon.GetOrigin(); }   // 0xA8 0x00604d40
    float UnknownAC(int) override { return m_weapon.UnknownAC(); }            // 0xAC 0x00604d60 (DS 0x0062bbc0)
    int GetSeatWeaponText(int) override { return m_weapon.GetItemText(); }    // 0xB0 0x00604d90 (DS 0x0062bc10)
    C_ItemDef *GetSeatWeaponDef(int) override { return m_weapon.GetItemDef(); }   // 0xB4 0x00604da0 (DS 0x0062bc30)
    void GetSeatWeaponLimits(int, float *up, float *down, float *left, float *right) override   // 0xB8 0x00604dd0 (DS 0x0062bc80)
    {
        m_weapon.GetLimits(up, down, left, right);
    }
    bool CanSeatWeaponAim(int, float *pos) override { return m_weapon.CanAim(pos); }   // 0xBC 0x006055a0 (DS 0x0062c680)
    // Manned seat: the seat weapon's controls.
    void ApplySeatControls(int seat, float *axes, float *buttons) override;   // 0xC0 0x00604cf0
    void ShowSeatView(int, bool on) override { m_weapon.ShowView(on); }       // 0xC4 0x00604db0 (DS 0x0062bc50)
    bool UnknownC8(int, int *a, int *b, int *c) override { return m_weapon.UnknownC8(a, b, c); }   // 0xC8 0x00604e10
    // "NO3RD" in the dummy's user properties.
    bool IsSeatFirstPersonOnly(int seat) override;      // 0xCC 0x00604e30
    // Passed on to the gunner (seat 0): a bullet or melee hit missing him alarms him (if hostile
    // and alive); otherwise his hit, in its zone.
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int flags) override;                       // 0xD4 0x006057b0
    // The corners of the weapon's box (GetWeaponBox) in the world.
    void GetBoxCorners(S_vector4 corners[8]) override;  // 0xDC 0x00605e50

private:
    struct S_Seat {                                     // 0x0C bytes
        C_Actor *occupant = nullptr;                    // +0x00
        bool blocked = false;                           // +0x04 (SEAT_FLAG_TAKEN)
        I3D_frame *frame = nullptr;                     // +0x08 "SEAT<nn>"
    };

    // The seat weapon's messages from the net object.
    void ReadMessages();                                // 0x00603290
    // The store: each item's uid (16 bits) and type (8), 0xFFFF last; read back (added with `add`).
    void WriteStore(C_BitStream *msg);                  // 0x006035b0
    void ReadStore(C_BitStream *msg, bool add);         // 0x00603650
    // To `state` (nothing when already in it): shown / hidden, destroyed / intact, the box.
    void ApplyState(int state);                         // 0x00605ae0
    // The visuals take the meshes and rotations of the model "C<model>" / the model again (the
    // occupants unlinked meanwhile).
    void ShowDestroyed();                               // 0x00605bb0
    void ShowIntact();                                  // 0x00605c80
    static uint32_t __stdcall CopyPart(I3D_frame *frame, void *model);   // 0x00605d80
    // The box moved with the frame (while in the world).
    void UpdateVolume();                                // 0x00604e90 (__fastcall)
    // The way out: the walking point at the weapon, else half a metre behind it.
    void FindExit();                                    // 0x006055c0 (__fastcall)
    bool IsSeat(int seat) const { return seat >= 0 && static_cast<size_t>(seat) < m_seats.size(); }

    C_NetObject *m_netObject = nullptr;                 // +0x90 (network games)
    S_matrix m_invMatrix = {};                          // +0xA0 the box's
    S_vector4 m_exit = {};                              // +0xE0 (0: none)
    S_CollisionVolume m_volume;                         // +0xF0
    bool m_volumeAdded = false;                         // +0x150
    I3D_model *m_destroyed = nullptr;                   // +0x154 "C<model>" (null: none)
    bool m_destroyedShown = false;                      // +0x158
    bool m_started = false;                             // +0x159
    C_SeatWeapon m_weapon;                              // +0x160
    std::vector<S_Seat> m_seats;                        // +0x2E0
    int m_state = 3;                                    // +0x2F0 STATIC_WEAPON_*
    bool m_storeChanged = false;                        // +0x2F4 (the server sends the store)
};

// C_StaticWeapon::GetState() / SetState().
enum : int {
    STATIC_WEAPON_HIDDEN = 0,
    STATIC_WEAPON_USABLE = 1,
    STATIC_WEAPON_DESTROYED = 2,
    STATIC_WEAPON_OFF = 3,                              // before StartMission, after StopMission
};

// The static weapons' creator (g_actorCreators[ACTOR_STATIC_WEAPON], vtable 0x00813120).
class C_StaticWeaponCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x00602a70
    const char *GetName() override { return "StaticWeapon"; }   // 0x00602ad0
    void Destroy(C_Actor *actor) override;              // 0x00602ae0
};
