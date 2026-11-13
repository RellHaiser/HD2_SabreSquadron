// The equipment panel of the in-game menu and the equipment screen (vtable 0x008157cc, 0xA4 bytes;
// C_GameMenu +0x4DC): the squad soldier being dressed, the controls of his places (head, uniform,
// backpack, long gun, hands, the drop place, four belt and four backpack items with their scroll
// arrows), the load bars and texts, the highlights of the places an item may go, and the menu
// soldier that wears it all. Original 0x0066f160..0x00672a30 (parts 9e-ii-a and -b).
#pragma once

#include <cstdint>

#include "game/soldier.h"

class C_Control;
class C_EquipmentMenuBase;
class C_ItemStock;
class C_MenuSoldier;
class I3D_frame;
class I3D_visual;
struct S_ScreenDesc;
struct S_SquadSoldier;

class C_EquipmentPanel {
public:
    // (Inline in the menus' constructors: only the soldier, the menu, the bars' first two, the
    // index and the squad soldier set; the rest the description's.)
    C_EquipmentPanel() = default;
    C_EquipmentPanel(const C_EquipmentPanel &) = delete;
    C_EquipmentPanel &operator=(const C_EquipmentPanel &) = delete;

    // The load bars and texts: the weight (over the capacity: the overweight bar's, when there is
    // one), the belt's and the backpack's fill.
    virtual void UpdateLoad();                          // 0x00 0x00672a30
    // The highlights of the places `item` may go (`ignoreWeight`: even over the free weight; the
    // belt and the backpack only with room for it).
    virtual void ShowAllowed(const S_SoldierItem &item, bool ignoreWeight);   // 0x04 0x006728a0
    virtual ~C_EquipmentPanel() = default;              // 0x08 0x0067f3d0; the body 0x0067f3c0 (DS 0x006bc740)

    // The panel's description in `screen` (its controls' ids kept; the item controls' callbacks the
    // menu's slots, the scroll arrows OnScroll), then the highlights; `model` the screen's model (its
    // overweight bar and description text frames optional).
    void BuildDescription(S_ScreenDesc *screen, I3D_frame *model);   // 0x0066f200

    // The squad soldier `index` of the mission's squad (none past them; the scroll arrows reset).
    void SelectSoldier(int index);                      // 0x00671010
    S_SquadSoldier *GetSquadSoldier() const { return m_squadSoldier; }

    // The place (ITEM_FITS_*) of a control of the panel by its id's kind (bits 12-19; the belt's and
    // the backpack's items by their child index, bits 4-11); 0: none.
    static uint32_t SlotOfControl(uint32_t id);         // 0x00671290
    int CountItems(uint32_t control, const S_SoldierItem &item) const;   // 0x00671260
    // Takes `item` from a place (the soldier's items too); how many.
    int RemoveItem(uint32_t slot, S_SoldierItem &item); // 0x00671360
    int RemoveItemAt(uint32_t control, S_SoldierItem &item);   // 0x006713c0
    // Puts `item` at a place; what it displaced (the soldier's items changed to match).
    S_SoldierItem PutItem(uint32_t slot, const S_SoldierItem &item);   // 0x00671410
    // ... only where the item fits and (unless `force`) the place is empty; else `item` back.
    S_SoldierItem PutItemChecked(uint32_t slot, const S_SoldierItem &item, bool force);   // 0x00671500
    S_SoldierItem PutItemAt(uint32_t control, const S_SoldierItem &item);   // 0x006716c0 (forced)
    // Puts `item` at the first place it fits (as much as the free weight allows); what is left.
    S_SoldierItem DropItem(S_SoldierItem &item, bool force);   // 0x006716f0
    // Moves `item` from one control's place to another's (to the drop place: given back; what the
    // target displaced goes back to the source, else anywhere); what is left.
    S_SoldierItem MoveItem(S_SoldierItem &item, uint32_t from, uint32_t to);   // 0x006719a0
    // The belt's and the backpack's scroll arrows (a click on one scrolls), then Refresh; the
    // arrows' callback (CONTROL_NOTIFY_06).
    static bool OnScroll(void *panel, void *argument, C_Control *control, uint32_t notification,
                         uint32_t value);           // 0x00671ac0
    // A place's control showing `item` (its picture and type; the belt's and the backpack's items
    // their count, ammunition its rounds too).
    static void ShowItem(uint32_t control, const S_SoldierItem &item);   // 0x00671f60
    // The backpack's item controls off (no backpack) or on.
    void SetBackpackInactive(bool inactive);            // 0x00672330
    // Every place's control again, then the load.
    void Refresh();                                     // 0x00672580
    // The backpack and its contents back into `stock`.
    void ReturnBackpack(C_ItemStock *stock);            // 0x00672720
    // Everything back into `stock`; the menu soldier undressed.
    void ReturnItems(C_ItemStock *stock);               // 0x006727f0
    // The highlights' frames of the screen's model (hidden).
    void FindHighlights(I3D_frame *model);              // 0x00672820
    // An item in the hands or on the shoulder that is a large weapon (IsLargeWeapon).
    bool HasLargeWeapon() const;                        // 0x0066f1a0
    // A weapon (an animated item) bigger than one inventory square, not behaviour 6.
    static bool IsLargeWeapon(int typeId);              // 0x0066f160

    // The menu soldier.
    // Made in `area`'s screen rectangle (the visual hidden).
    void CreateSoldier(I3D_visual *area);               // 0x00670c00
    void DeleteSoldier();                               // 0x00670e40
    void TickSoldier(int time);                         // 0x00670be0
    void RenderSoldier();                               // 0x00670bf0
    // The squad soldier's face.
    void UpdateFace();                                  // 0x00670e90
    // The soldier dressed in the squad soldier's items.
    void DressSoldier();                                // 0x00670f20
    // `item.present` of `item` at `place`.
    void AddToSoldier(uint32_t place, const S_SoldierItem &item);   // 0x00670ee0
    C_MenuSoldier *GetSoldier() const { return m_soldier; }

    // The menu whose slots the controls' callbacks call (part 9e-ii-b).
    void SetMenu(C_EquipmentMenuBase *menu) { m_menu = menu; }

private:
    static constexpr int SLOT_COUNT = 4;                // the belt's and the backpack's shown items
    static constexpr int HIGHLIGHT_COUNT = 8;

    C_MenuSoldier *m_soldier = nullptr;                 // +0x04
    C_EquipmentMenuBase *m_menu = nullptr;              // +0x08
    uint32_t m_weightBar = 0;                           // +0x0C
    uint32_t m_overweightBar = 0;                       // +0x10 (0: none)
    uint32_t m_beltBar = 0;                             // +0x14
    uint32_t m_backpackBar = 0;                         // +0x18
    int m_beltScroll = 0;                               // +0x1C
    int m_backpackScroll = 0;                           // +0x20
    // "selhead", "seluniform", "selbck", "selshoulder", "selhands", "seldrop", "selholsters",
    // "selbackpack".
    I3D_frame *m_highlights[HIGHLIGHT_COUNT] = {};      // +0x24
    uint32_t m_beltUp = 0;                              // +0x44
    uint32_t m_beltDown = 0;                            // +0x48
    uint32_t m_backpackUp = 0;                          // +0x4C
    uint32_t m_backpackDown = 0;                        // +0x50
    uint32_t m_descriptionText = 0;                     // +0x54
    uint32_t m_weightText = 0;                          // +0x58
    uint32_t m_beltText = 0;                            // +0x5C
    uint32_t m_backpackText = 0;                        // +0x60
    int m_soldierIndex = -1;                            // +0x64
    S_SquadSoldier *m_squadSoldier = nullptr;           // +0x68
    uint32_t m_headSlot = 0;                            // +0x6C
    uint32_t m_uniformSlot = 0;                         // +0x70
    uint32_t m_backpackSlot = 0;                        // +0x74
    uint32_t m_longGunSlot = 0;                         // +0x78
    uint32_t m_handsSlot = 0;                           // +0x7C
    uint32_t m_dropSlot = 0;                            // +0x80
    uint32_t m_beltSlots[SLOT_COUNT] = {};              // +0x84
    uint32_t m_backpackSlots[SLOT_COUNT] = {};          // +0x94
};
