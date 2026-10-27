// Harness stub of game/items_module.h (the real one is too wide): the item creation the car's updates
// use (game/car_update.cpp) and the seat weapon's, under the real names.
#pragma once

#include "game/item_defs.h"
#include "game/items.h"
#include "ls3d/math.h"

// An item placed in the mission (the temporary list keeps the pick-ups'; game/item_actor.cpp).
struct alignas(16) S_ItemPlacement {                    // 0x30 bytes
    C_Item *item;
    S_vector4 pos;                                      // +0x10
    S_quat rot;                                         // +0x20
};

class C_ItemsModule {
public:
    C_Item *CreateItem(int typeId, int container, C_Actor *owner, int uid);
    C_ItemDef *GetItemDef(int typeId) const;
    // (The seat weapon's item, game/seat_weapon.cpp.)
    C_Item *CreateFreeItem(int typeId);
    void DestroyItem(C_Item *item);
};
extern C_ItemsModule g_itemsModule;
