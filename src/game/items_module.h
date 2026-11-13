// C_ItemsModule — the "Items" module (original source 0x007d7ad0..0x007df2f0).
//
// Owns the item definitions (Tables\items.sav) and every item instance, and implements
// the inventory rules: which item may go into which container of an actor, moving
// items between actors and containers (with the weight an actor can carry), automatic
// placement, swapping, dropping items into the world, and saving inventories. During a
// mission it also tells the human equipment models and the HUD about changes.
//
// Mission items come from Missions\<mission>\items.dat: items given to actors and items
// placed in the scene (S_ItemPlacement), which the temporary actors list turns into item
// actors when the mission starts.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "game/item_defs.h"
#include "game/items.h"
#include "game/module.h"
#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class I3D_frame;

// A mission item placed in the scene (copies kept for respawning pick-ups).
struct alignas(16) S_ItemPlacement {                    // 0x30 bytes
    C_Item *item;
    S_vector4 pos;                                      // +0x10
    S_quat rot;                                         // +0x20
};

// Inventory results beyond ITEM_RESULT_* (items.h).
enum : int {
    ITEM_RESULT_BAD_TARGET = -2,    // no such container at the target (actor missing or a vehicle)
    ITEM_RESULT_BAD_SOURCE = -3,
    ITEM_RESULT_ACTOR_OVERLOADED = -4,  // more than the actor can carry (GetHitPoints())
    ITEM_RESULT_WRONG_KIND = -8,
    ITEM_RESULT_SOURCE_LOCKED = -9,
    ITEM_RESULT_TARGET_LOCKED = -10,
    ITEM_RESULT_HANDS_BUSY = -11,   // the item in the hands is still busy
    ITEM_RESULT_SAME_PLACE = -12,
    ITEM_RESULT_ITEM_BUSY = -20,    // TransferItem: the item was moved less than a second ago
    ITEM_RESULT_SWAP_BLOCKED = -21, // TransferItem: the displaced item fits nowhere
    ITEM_RESULT_NO_ACTOR = -100,
    ITEM_RESULT_SINGLE_CONTAINER = -101,
    ITEM_RESULT_NO_PLACE = -102,
    ITEM_RESULT_NULL_ITEM = -666,
    ITEM_RESULT_NO_HEAD = -1000,    // CanPutItemInSlot: no actor for the head slot
};

// Container value meaning "dropped" in TransferItem's results.
constexpr int ITEM_CONTAINER_DROPPED = 7;

class C_ItemsModule : public C_Module {                 // "Items", vtable 0x00817208, 0x848 bytes
public:
    C_ItemsModule(int id, const char *name);            // 0x007dce90
    ~C_ItemsModule() override;                          // 0x007dcfd0

    void Init() override;                               // 0x007dd180
    void Done() override;                               // 0x007dd260 (DS 0x00818270)
    void Tick(int timeDelta) override;                  // 0x007dd7f0
    void Update(int timeDelta) override;                // 0x007dd810
    void LoadMissionData(bool server) override;         // 0x007dd300
    void FreeMissionData(bool server) override;         // 0x007dd410
    void StartMission(bool server) override;            // 0x007dd470
    void StopMission(bool server) override;             // 0x007dd6a0
    void RestartMission(bool server) override;          // 0x007ddbb0
    bool SaveGame(C_chunk &chunk) override;             // 0x007ddef0
    bool LoadGame(C_chunk &chunk) override;             // 0x007de110

    // --- definitions --------------------------------------------------------------
    // Loads Tables\items.sav on first use.
    void EnsureItemsLoaded();                           // 0x007dc940 (DS 0x008176c0)
    C_ItemDef *GetItemDef(int typeId) const;            // 0x007df190

    // --- instances ----------------------------------------------------------------
    // New item of type `typeId` with uid `uid` (< 0: a new uid) put into container
    // `container` of `owner` (none: a free item); null if it does not fit.
    C_Item *CreateItem(int typeId, int container, C_Actor *owner, int uid);   // 0x007d7c70
    // Single player: gives `actor` a new item, in the container its kind suggests.
    C_Item *GiveItem(int typeId, int unused, C_Actor *actor, int unused2);   // 0x007d7d90 (callers: 0, actor, -1)
    // New item without an owner or uid (-1).
    C_Item *CreateFreeItem(int typeId);                 // 0x007d7f30
    // Deletes an instance and forgets it.
    void DestroyItem(C_Item *item);                     // 0x007dcd00
    void AddRespawningItem(C_Item *item);               // 0x007ddcc0
    void RemoveRespawningItem(C_Item *item);            // 0x007dde70
    const std::vector<C_Item *> &GetRespawningItems() const { return m_respawningItems; }

    // --- inventory rules ----------------------------------------------------------
    // Moves `item` from container `fromContainer` of `from` (null: a new / free item)
    // into container `toContainer` of `to` (null with container 0: drops it), checking
    // the containers' limits and what the receiver can carry. A single-item container
    // hands back its previous item in `*replaced`. `carrier` is the actor whose weight
    // is reduced (default `from`). `notify`: tell the equipment models / HUD;
    // `ignoreLocks`: move even out of / into locked containers; `keepWeights`: leave
    // the source's weight (and the replaced item's) alone; `testOnly`: only check.
    // Returns ITEM_RESULT_OK or an error (ITEM_RESULT_*).
    int MoveItem(C_Item *item, C_Item **replaced, C_Actor *from, int fromContainer, C_Actor *to,
                 int toContainer, int index, C_Actor *carrier, bool notify, bool ignoreLocks,
                 bool keepWeights, bool testOnly);      // 0x007d8fa0
    // Puts a free item into the container of `actor` its kind suggests (*container
    // receives it, ITEM_CONTAINER_DROPPED if none).
    int PutItem(C_Item *item, C_Actor *actor, int *container, bool notify);   // 0x007d9fa0
    // Same check without moving anything.
    int CanPutItem(C_Actor *actor, C_Item *item);       // 0x007db3e0
    // Moves `item` between two actors' containers; an item already in the target slot
    // goes back to the source (its old container, any free one, or dropped:
    // *replacedContainer). Updates the equipment models and the HUD itself.
    int TransferItem(C_Item *item, C_Actor *from, int fromContainer, C_Actor *to, int toContainer,
                     C_Item **replaced, int *toContainerOut, int *replacedContainer);   // 0x007da240
    // TransferItem into the container of `to` the item's kind suggests.
    int TransferItemAuto(C_Item *item, C_Actor *from, int fromContainer, C_Actor *to,
                         int *toContainer);             // 0x007da850
    // Checks for an item type (no instance) instead.
    int CanAddItemToInventory(C_Actor *actor, int itemId);  // 0x007db5d0
    int CanPutItemInSlot(C_Actor *actor, int container, int itemId);   // 0x007db780
    // Takes `item` out of `container` of `owner` and drops its model in front of him.
    void DropItem(C_Item *item, C_Actor *owner, int container);   // 0x007dc1e0 (DS 0x00816e10)
    // Takes `item` out of `container` of `owner` (owner and weight updated).
    void RemoveFromContainer(C_Item *item, C_Actor *owner, int container);   // 0x007dc700 (DS 0x00817440)

    // --- saved games / mission files ------------------------------------------------
    // One item (chunk 0xAD2C) with three caller-defined numbers, and the contents of
    // containers; returns false (also on success, like the original).
    bool SaveItem(C_Item *item, int container, int slot, int index, C_chunk &chunk);   // 0x007de840
    // Reads such a chunk; contained items go back into the item (their weight to `owner`).
    C_Item *LoadItem(int *container, int *slot, int *index, C_chunk &chunk, C_Actor *owner);   // 0x007deb60
    // Restores the items of Missions\<mission>\items.dat.
    bool LoadMissionItems();                            // 0x007d8600
    // Empties the containers of every actor and forgets the placed items.
    void ClearActorItems();                             // 0x007d8ef0

    // Items placed in the mission (read by the temporary actors list).
    int GetPlacementCount() const { return m_placementCount; }
    S_ItemPlacement &GetPlacement(int index) { return m_placements[index]; }
    // Forgets the placements (their items are not destroyed).
    void FreePlacements();

    bool IsInfiniteAmmo() const { return m_infiniteAmmo; }
    void SetInfiniteAmmo(bool on) { m_infiniteAmmo = on; }
    bool IsMissionActive() const { return m_missionActive; }

private:
    // A uid released for reuse with the time it was released (QueryPerformanceCounter us).
    struct S_FreedUid {
        uint32_t time;
        int uid;
    };

    // New uid: a released one at least 10 ms old, otherwise the next unused number.
    int NextUid();                                      // 0x007dda30
    void ResetUids();
    // Remembers an instance (DestroyItem forgets it).
    void AddInstance(C_Item *item);                     // 0x007dcb40
    // New instance of the class that item type `typeId` needs.
    C_Item *NewItem(int typeId) const;
    // Loads Tables\items.sav into m_items.
    void LoadItemsTable();                              // 0x007dc960
    // Container-by-container part of the automatic placement shared by PutItem and
    // CanPutItem; returns the result and the container used.
    int PutItemAuto(C_Item *item, C_Actor *actor, int *container, bool notify, bool testOnly);
    void AddPlacement(const S_ItemPlacement &placement);
    // HUD / equipment updates of MoveItem.
    void NotifyMove(C_Item *item, C_Actor *fromHuman, C_ItemContainer *source, int fromContainer,
                    C_Actor *target, C_ItemContainer *dest, int toContainer, bool fromNew, bool notify);

    bool m_infiniteAmmo = false;                        // +0x0C console cheat (single player)
    bool m_itemsLoaded = false;                         // +0x0D
    C_ItemDef *m_items[MAX_ITEM_TYPES] = {};            // +0x10
    int m_lastUid = -1;                                 // +0x7E0
    bool m_missionActive = false;                       // +0x7E4 StartMission..StopMission
    bool m_unknown7E5 = false;                          // +0x7E5 cleared by StartMission
    S_ItemPlacement *m_placements = nullptr;            // +0x7E8 _aligned_malloc'd
    int m_placementCapacity = 0;                        // +0x7EC
    int m_placementCount = 0;                           // +0x7F0
    int m_placementGrowBy = 2;                          // +0x7F4
    std::vector<C_Item *> m_respawningItems;            // +0x7F8
    std::deque<S_FreedUid> m_freedUids;                 // +0x808 (nothing adds to it in this build)
    std::vector<C_Item *> m_instances;                  // +0x838
};

extern C_ItemsModule g_itemsModule;                     // 0x0098b020

// Definitions of the containers of `actor` (items saved in chunk 0x2775). (__thiscall
// methods of g_itemsModule in the original.)
bool SaveActorItems(C_Actor *actor, C_chunk &chunk);    // 0x007de3c0
bool LoadActorItems(C_Actor *actor, C_chunk &chunk);    // 0x007de5a0
// TransferItem: whether the item `item` would displace from `toContainer` of `to` fits
// nowhere in `from` (taking into account the place `item` leaves there).
bool IsSwapBlocked(C_Actor *from, int fromContainer, C_Actor *to, int toContainer, C_Item *item);   // 0x007daa40
// Destroys the items of every container of `actor` and unlocks them.
void ClearActorContainers(C_Actor *actor);              // 0x007de350
// Missions\<mission>\items.dat: the items of every actor.
bool SaveMissionItems();                                // 0x007d81f0
void DeleteMissionItemsFile();                          // 0x007d8040
// Shared models of the idle activity props (0 cards, 1 bottle, 2 cigarette), loaded on
// first use and released when the mission stops.
I3D_frame *GetActivityProp(int index);                  // 0x007d7b20
// Copies `source` and its children (new engine frames duplicating them) into `copy`,
// linked to `parent`; false if a frame could not be created.
bool CloneFrameTree(I3D_frame *&copy, I3D_frame *source, I3D_frame *parent);   // 0x007dc800
// Unlinks and releases a frame and its children (sounds, lights, projectors and
// emitters are only unlinked).
void ReleaseFrameTree(I3D_frame *frame);                // 0x007dc8c0
