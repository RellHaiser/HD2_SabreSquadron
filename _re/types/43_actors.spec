# Actors (src/game/actor.*, actors_module.cpp): the C_Actor base class (0x00610070..
# 0x006115b0 plus inline defaults emitted elsewhere), C_ActorsModule ("Actors",
# g_actorsModule, 0x004755d0..0x00476a50) and C_TemporaryActorsList ("Temp. Actors",
# g_temporaryActors, 0x00476ec0..0x00479600).
#
# Every actor class has a creator object (Create / GetName / Destroy) registered in
# g_actorCreators by E_ActorType; its vtable directly follows the class vtable.

enum E_ActorType
  ACTOR_MINE = 2
  ACTOR_HUMAN = 3
  ACTOR_CAR = 4
  ACTOR_BARREL = 5
  ACTOR_DOOR = 6
  ACTOR_RADIO = 7
  ACTOR_LADDER = 8
  ACTOR_SUPPLY_BOX = 9
  ACTOR_SPAWN_POINT = 10
  ACTOR_ITEM = 11
  ACTOR_DEBRIS = 12
  ACTOR_SWITCH = 13
  ACTOR_TRAIN = 14
  ACTOR_PHOTO_PLACE = 15
  ACTOR_SUBMARINE = 20
  ACTOR_STATIC_WEAPON = 21
  ACTOR_EXPLOSIVE_PLACE = 22
  ACTOR_DOCUMENT_PLACE = 23
  ACTOR_ANIMATION = 25
  ACTOR_CLOCK = 27
  ACTOR_PIKE = 28
  ACTOR_FOWL_GENERATOR = 29
  ACTOR_MELEE_TRACKER = 30
  ACTOR_BIKE = 31
  ACTOR_FISH = 32
  ACTOR_NULL = 33
  ACTOR_JUNKERS88 = 34
  ACTOR_WIRE_CUT = 35
  ACTOR_AIR_ATTACK = 36
  ACTOR_MP_ZONE = 37
  ACTOR_TS_EXPLOSION = 38
end

# Base of all actors; 16-byte aligned (MSVC pads the vtable pointer to 16 bytes).
class C_Actor size=0x90 vtable=0x8137f0:56
  0x010 int m_netId                   # index in the actors module / temporary id
  0x014 int m_activeIndex             # index in the module's active list, -1 = inactive
  0x018 E_ActorType m_type
  0x01c bool m_temporary              # kept by g_temporaryActors
  0x020 I3D_frame* m_frame
  0x030 S_vector4 m_position          # simulation position (frame follows)
  0x040 S_vector4 m_direction
  0x050 S_vector4 m_initialPos        # frame placement when activated, restored on deactivation
  0x060 S_quat m_initialRot
  0x070 I3D_frame* m_initialParent
  0x074 void** m_itemStores           # item stores (vtable 0x8171a0): humans 6, vehicles 1
  0x078 bool m_singleItemStore        # vehicles
  0x07c float m_carriedWeight         # weight of the items in the containers
  0x080 C_str m_name
  vslot 0x00 ScalarDeletingDestructor | C_Actor * __thiscall ScalarDeletingDestructor(C_Actor *this, uchar flags)
  vslot 0x01 GetName | C_str * __thiscall GetName(C_Actor *this)
  vslot 0x02 Init | void __thiscall Init(C_Actor *this)
  vslot 0x03 Done | void __thiscall Done(C_Actor *this)
  vslot 0x04 GetHitPoints | int __thiscall GetHitPoints(C_Actor *this)
  vslot 0x05 Update | void __thiscall Update(C_Actor *this, int frameTime)
  vslot 0x06 Tick | void __thiscall Tick(C_Actor *this, int timeDelta)
  vslot 0x07 OnMessage | void __thiscall OnMessage(C_Actor *this, C_BitStream *msg)
  vslot 0x08 Unknown20 | void __thiscall Unknown20(C_Actor *this, C_BitStream *msg)
  vslot 0x09 Unknown24 | bool __thiscall Unknown24(C_Actor *this, C_chunk *chunk)
  vslot 0x0a Unknown28 | bool __thiscall Unknown28(C_Actor *this, C_chunk *chunk)
  vslot 0x0b SetFrame | bool __thiscall SetFrame(C_Actor *this, I3D_frame *frame)
  vslot 0x0c WriteProperties | bool __thiscall WriteProperties(C_Actor *this, C_chunk *chunk)
  vslot 0x0d ReadProperties | bool __thiscall ReadProperties(C_Actor *this, C_chunk *chunk)
  vslot 0x0e CopyFrom | void __thiscall CopyFrom(C_Actor *this, C_Actor *source)
  vslot 0x0f StartMission | void __thiscall StartMission(C_Actor *this, bool server)
  vslot 0x10 StopMission | void __thiscall StopMission(C_Actor *this, bool server)
  vslot 0x11 RestartMission | void __thiscall RestartMission(C_Actor *this, bool server)
  vslot 0x12 SaveGame | bool __thiscall SaveGame(C_Actor *this, C_chunk *chunk)
  vslot 0x13 LoadGame | bool __thiscall LoadGame(C_Actor *this, C_chunk *chunk)
  vslot 0x14 WriteState | void __thiscall WriteState(C_Actor *this, C_BitStream *msg)
  vslot 0x15 ReadState | void __thiscall ReadState(C_Actor *this, C_BitStream *msg)
  vslot 0x16 OnCollision | void __thiscall OnCollision(C_Actor *this, C_Actor *other, int a2, int a3, int a4, int a5, int a6)
  vslot 0x17 OnCollisionEx | bool __thiscall OnCollisionEx(C_Actor *this, C_Actor *other, int a2, int a3, int a4, int a5, int a6, int a7)
  vslot 0x18 Unknown60 | void __thiscall Unknown60(C_Actor *this, int arg)
  vslot 0x19 IsActive | bool __thiscall IsActive(C_Actor *this)
  vslot 0x1a Unknown68 | void __thiscall Unknown68(C_Actor *this, int a1, int a2, int a3)
  vslot 0x1b IsSeatBlocked | bool __thiscall IsSeatBlocked(C_Actor *this, int seat)
  vslot 0x1c SetSeatBlocked | bool __thiscall SetSeatBlocked(C_Actor *this, int seat, bool blocked)
  vslot 0x1d SetSeatOccupant | void __thiscall SetSeatOccupant(C_Actor *this, int seat, C_Actor *occupant)
  vslot 0x1e GetSeatOccupant | C_Actor * __thiscall GetSeatOccupant(C_Actor *this, int seat)
  vslot 0x1f FindSeat | int __thiscall FindSeat(C_Actor *this, C_Actor *occupant)
  vslot 0x20 LeaveSeat | C_Actor * __thiscall LeaveSeat(C_Actor *this, int seat)
  vslot 0x21 ExitSeat | bool __thiscall ExitSeat(C_Actor *this, int seat)
  vslot 0x22 GetSeatCount | int __thiscall GetSeatCount(C_Actor *this)
  vslot 0x23 GetSeatFlags | uint __thiscall GetSeatFlags(C_Actor *this, int seat)
  vslot 0x24 GetSeatCamera | I3D_frame * __thiscall GetSeatCamera(C_Actor *this, int seat)
  vslot 0x25 GetSeatText | int __thiscall GetSeatText(C_Actor *this, int seat)
  vslot 0x26 HasSeatWeapon | bool __thiscall HasSeatWeapon(C_Actor *this, int seat)
  vslot 0x27 GetSeatWeaponRounds | int __thiscall GetSeatWeaponRounds(C_Actor *this, int seat)
  vslot 0x28 GetSeatAmmoCount | int __thiscall GetSeatAmmoCount(C_Actor *this, int seat)
  vslot 0x29 GetSeatWeaponPos | S_vector * __thiscall GetSeatWeaponPos(C_Actor *this, S_vector *out, int seat)
  vslot 0x2a GetSeatWeaponDir | S_vector * __thiscall GetSeatWeaponDir(C_Actor *this, S_vector *out, int seat)
  vslot 0x2b UnknownAC | float __thiscall UnknownAC(C_Actor *this, int seat)
  vslot 0x2c GetSeatWeaponText | int __thiscall GetSeatWeaponText(C_Actor *this, int seat)
  vslot 0x2d GetSeatWeaponDef | C_ItemDef * __thiscall GetSeatWeaponDef(C_Actor *this, int seat)
  vslot 0x2e GetSeatWeaponLimits | void __thiscall GetSeatWeaponLimits(C_Actor *this, int seat, float *a, float *b, float *c, float *d)
  vslot 0x2f CanSeatWeaponAim | bool __thiscall CanSeatWeaponAim(C_Actor *this, int seat, float *pos)
  vslot 0x30 ApplySeatControls | void __thiscall ApplySeatControls(C_Actor *this, int seat, float *axes, float *buttons)
  vslot 0x31 ShowSeatView | void __thiscall ShowSeatView(C_Actor *this, int seat, bool on)
  vslot 0x32 UnknownC8 | bool __thiscall UnknownC8(C_Actor *this, int seat, int *a, int *b, int *c)
  vslot 0x33 IsSeatFirstPersonOnly | bool __thiscall IsSeatFirstPersonOnly(C_Actor *this, int seat)
  vslot 0x34 UnknownD0 | void __thiscall UnknownD0(C_Actor *this, int a1, int a2, int a3, int a4)
  vslot 0x35 Hit | void __thiscall Hit(C_Actor *this, int type, float amount, S_vector *pos, S_vector *dir, C_Actor *attacker, int flags)
  vslot 0x36 GetMaterialId | int __thiscall GetMaterialId(C_Actor *this, I3D_frame *part)
  vslot 0x37 GetBoxCorners | void __thiscall GetBoxCorners(C_Actor *this, S_vector4 *corners)
end

# Actor creator registered in g_actorCreators[type] (vtable follows the class vtable).
class C_ActorCreator size=0x4 vtable=0x0
  vslot 0x00 Create | C_Actor * __thiscall Create(C_ActorCreator *this)
  vslot 0x01 GetName | char * __thiscall GetName(C_ActorCreator *this)
  vslot 0x02 Destroy | void __thiscall Destroy(C_ActorCreator *this, C_Actor *actor)
end

# Actor scheduled for deletion (C_ActorsModule::m_removed).
struct S_RemovedActor size=0x8
  0x00 C_Actor* actor
  0x04 int timeLeft                   # ms
end

# Item of the mission scene (g_modelPool entries, and the temporary list's respawn copies).
struct S_ItemPlacement size=0x30
  0x00 C_LoadedModel* model
  0x10 S_vector4 pos
  0x20 S_quat rot
end

class C_ActorsModule : C_Module size=0x7c vtable=0x8100a8:16
  0x0c std::vector<C_Actor*> m_actors        # [0, 32) reserved for players (may be null)
  0x1c std::vector<C_Actor*> m_active
  0x2c std::vector<C_Actor*> m_pending       # activation changes requested while iterating
  0x3c std::vector<S_RemovedActor> m_removed
  0x4c int m_liveEnemies
  0x50 int m_carnageEnemies
  0x54 C_Actor* m_carnageEnemy               # the one carnage enemy, when there is exactly one
  0x58 bool m_iterating
  0x59 uint8_t m_playerSlotUse[32]
end

class C_TemporaryActorsList : C_Module size=0x78 vtable=0x8100e8:16
  0x0c std::vector<C_Actor*> m_actors
  0x1c std::vector<C_Actor*> m_active
  0x2c std::vector<C_Actor*> m_pending       # activation changes requested while iterating
  0x3c std::vector<C_Actor*> m_toDestroy     # destruction requested while iterating
  0x4c S_BreakableObject* m_breakables       # _aligned_malloc'ed
  0x50 int m_breakableCapacity
  0x54 int m_breakableCount
  0x58 int m_breakableGrowBy                 # 10
  0x5c S_ItemPlacement* m_respawnItems       # _aligned_malloc'ed
  0x60 int m_respawnItemCapacity
  0x64 int m_respawnItemCount
  0x68 int m_respawnItemGrowBy               # 2
  0x6c bool m_iterating
  0x70 int m_nextId
  0x74 int m_debrisCount
end
