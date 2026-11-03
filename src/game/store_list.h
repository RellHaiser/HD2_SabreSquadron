// The store's item lists of the in-game menu and the equipment screen (vtable 0x008157c4, 0x38 bytes;
// C_GameMenu +0x580): a page of the item stock per inventory group (the tabs choose the page shown),
// each item type's entry showing its count. Original 0x0067b070..0x0067c390 (parts 9e-iii-a and -b).
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/soldier.h"

class C_Control;
class C_EquipmentMenuBase;
class C_ItemStock;
class I3D_frame;
struct S_ControlDesc;
struct S_ScreenDesc;

class C_StoreList {
public:
    // (Inline in the menus' constructors.)
    C_StoreList() = default;
    C_StoreList(const C_StoreList &) = delete;
    C_StoreList &operator=(const C_StoreList &) = delete;
    // A page of the items of inventory group `group` under `parent` (the model's "P_visualstore"
    // dummy gets a dummy `name` with the highlight's copy and the "vi_template" visual; an entry
    // "frame_<type>" per item type of the group, cloned from the template; group 1 the page shown
    // first). False without the frames.
    virtual bool BuildPage(S_ControlDesc *parent, I3D_frame *model, C_str name, int group);   // 0x00 0x0067b070
    virtual ~C_StoreList() = default;                   // 0x04 0x0067f3a0 (0x0067f370)

    // The count of an item type in the stock (none: 0).
    int GetCount(int typeId) const;                     // 0x0067b930
    // The page `page` shown (the last hidden; the list group's value the page; laid out again).
    void ShowPage(int page);                            // 0x0067b9e0
    // The tabs' callback: the page the tab control answers.
    static bool OnTab(void *list, void *argument, C_Control *control, uint32_t notification,
                      uint32_t value);              // 0x0067b950
    // Every item control's callback (CONTROL_NOTIFY_ANY): the menu's slots by the notification (the
    // cursor entering also puts the highlight on the control's frame); true.
    static bool OnItem(void *list, void *argument, C_Control *control, uint32_t notification,
                       uint32_t value);             // 0x0067bc00
    // An item type's entry (ammunition: the weapons that take it, their second count; behaviour 16:
    // the uniforms (behaviour 15) whose head gear it is) from the stock.
    bool UpdateType(int typeId);                        // 0x0067bd60
    void UpdateAll();                                   // 0x0067c040
    // The stock shown (none: nothing done, false).
    bool UpdateAllIfStock();                            // 0x0067c060
    void SetStock(C_ItemStock *stock);                  // 0x0067c090
    // `item` put back into the stock.
    void AddItem(const S_SoldierItem &item);            // 0x0067c0a0
    // `item.present` of the stock taken (`fill`: those it lacks added first); none: false; else the
    // count taken in `item`.
    bool TakeItem(S_SoldierItem &item, bool fill);      // 0x0067c0d0
    // The pages' lists emptied (CONTROL_MSG_HIDE_ALL).
    void ClearPages();                                  // 0x0067c150
    // The tabs and the page shown on or off.
    void SetActive(bool on);                            // 0x0067c390

    void SetMenu(C_EquipmentMenuBase *menu) { m_menu = menu; }
    // The list's description: the group "P_visualstore", its tabs and pages (not ported yet).
    void BuildDescription(S_ScreenDesc *desc, I3D_frame *model);   // 0x0067aa70
    C_ItemStock *GetStock() const { return m_stock; }

private:
    // The list's controls: the screen's control m_controlIndex of screen m_screenIndex.
    uint32_t BaseId() const { return (static_cast<uint32_t>(m_controlIndex) & 0x3F | m_screenIndex << 6) << 20; }

    uint8_t m_unknown04 = 0;                            // +0x04
    std::vector<uint32_t> m_entries;                    // +0x08 the item types' entry controls (0: none)
    uint32_t m_unknown14 = 0;                           // +0x14
    uint32_t m_shownPage = 0;                           // +0x18 the page's control
    int m_unknown1c = -1;                               // +0x1C
    C_EquipmentMenuBase *m_menu = nullptr;              // +0x20
    C_ItemStock *m_stock = nullptr;                     // +0x24
    I3D_frame *m_template = nullptr;                    // +0x28 "vi_template"
    I3D_frame *m_highlight = nullptr;                   // +0x2C
    uint32_t m_screenIndex = 0;                         // +0x30
    int m_controlIndex = 0;                             // +0x34
};
