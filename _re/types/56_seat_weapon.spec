# The seat weapon (src/game/seat_weapon.h, part 3): the weapon of a vehicle's seat (C_Car's
# m_seatWeapons, made by its StartMission for SEAT_FLAG_WEAPON seats), also the static weapon's and
# the Junkers 88's. 0x180 bytes, 16-byte aligned (_aligned_malloc); ctor 0x004c19d0, dtor 0x004c1aa0.

# A barrel (Init: "BARREL<seat>_<n>*" visuals from 0 until one is missing; those without a muzzle
# are left out).
struct S_SeatWeaponBarrel size=0x10
  0x00 I3D_frame* frame                        # (its animation notes throw the shells)
  0x04 I3D_frame* muzzle                       # its "SHDUM*": the shots and the flash leave it
  0x08 I3D_frame* ejector                      # its "CARDUM*": FireBulletFrom throws the cartridges there
  0x0c I3D_animation_set* animation            # "<model>_f<seat>_<n>" (the first-person shot)
end

struct C_SeatWeapon size=0x180
  0x000 S_vector4 m_vertRestAxis               # the VERT frame's turn at Init (axis; LS3D's Inverse writes s after it)
  0x010 S_vector4 m_hortRestAxis               # the HORT frame's
  0x020 uint8_t m_unknown20[64]                # (not used by the weapon's code)
  0x060 uint8_t m_unknown60[96]                # (zeroed by the constructor, not used otherwise)
  0x0c0 float m_vertRestAngle
  0x0c4 float m_hortRestAngle
  0x0c8 int m_seat
  0x0cc float m_turnSpeed                      # the controls' traverse (rad/s; the local player's)
  0x0d0 int m_idleTime                         # ms without turning
  0x0d4 bool m_turnReleased                    # the traverse axis at 0
  0x0d8 float m_tiltSpeed                      # the controls' elevation
  0x0dc bool m_tiltReleased
  0x0e0 int m_reloadLeft                       # ms of the reload left (UnknownC8: the HUD)
  0x0e4 int m_reloadTime
  0x0e8 int m_unknownE8                        # (Init clears it)
  0x0ec I3D_frame* m_model                     # the owner's frame
  0x0f0 I3D_frame* m_seatFrame                 # the seat's dummy, its "HORT" parent if it has one
  0x0f4 I3D_frame* m_view                      # "VIEW<seat>": the first-person parts
  0x0f8 I3D_frame* m_projectileFrame           # "projectile" (a launcher's)
  0x0fc C_Human* m_gunner
  0x100 C_Actor* m_owner                       # the car (or the static weapon, the Junkers 88)
  0x104 bool m_started
  0x108 std::vector<S_SeatWeaponBarrel> m_barrels
  0x118 float m_tilt                           # the elevation (rad)
  0x11c float m_turn                           # the traverse
  0x120 I3D_sound* m_motorSound                # HORT's "soundm" (on while turning)
  0x124 I3D_sound* m_stopSound                 # HORT's "sounds"
  0x128 float m_upLimit                        # rad ("up" degrees; 1e6: none)
  0x12c float m_downLimit                      # ("down", negated)
  0x130 float m_leftLimit                      # ("left", negated)
  0x134 float m_rightLimit
  0x138 bool m_failed                          # Init could not set it up
  0x13c I3D_frame* m_hort                      # "HORT<seat>*" (traverse)
  0x140 I3D_frame* m_vert                      # "VERT<seat>*" (elevation)
  0x144 I3D_frame* m_aim                       # "AIM<seat>*"
  0x148 I3D_model* m_shells                    # the "GunShells" model (hidden)
  0x14c uint32_t m_flags                       # 1/2 turning right/left, 4/8 up/down, 0x20 reloading, 0x400 the motor sound on
  0x150 int m_barrel                           # the next to fire
  0x154 int m_delay                            # ms to the next shot (or the reload's end)
  0x158 C_WeaponItem* m_item                   # "itemid" (CreateFreeItem)
  0x15c int m_flashEffect                      # the item's muzzle flash (particle effect)
  0x160 float m_turnRate                       # "hs" (rad/s; pi/4 without)
  0x164 float m_tiltRate                       # "vs"
  0x168 bool m_launcher                        # the item fires projectiles (its component kind 2)
  0x16c void* m_sender                         # network games: SeatWeaponSendMessage
  0x170 void* m_senderContext                  # its context (the car)
  0x174 uint32_t m_refillTime                  # network games: the mission time of the last ammo the server added
  0x178 int m_refillBelow                      # how few rounds in the store let it add more
end
