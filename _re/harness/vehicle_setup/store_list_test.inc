// Part 9e-iii-a's store lists (game/store_list.cpp), in the factory build, run by ControlsTest after
// EquipmentDescTest: the item stock faked (its counts a map; Take and Get logged), the control
// messages read back from the queue (equipment_panel_test.inc's), the menu equipment_desc_test.inc's
// logging one. Prints "storelist:" with 1 per check met.
#define private public
#include "game/store_list.h"
#undef private

// The stock's counts (Add logs: equipment_panel_test.inc's fake).
std::map<int, int> g_slCounts;
int C_ItemStock::Get(int type) const
{
    const auto it = g_slCounts.find(type);
    return it == g_slCounts.end() ? 0 : it->second;
}
int C_ItemStock::Take(int type, int count)
{
    GfLog("stock take %d %d", type, count);
    int &have = g_slCounts[type];
    const int taken = std::min(have, count);
    have -= taken;
    return taken;
}

namespace {

struct S_StoreListResults {
    bool slCount = true, slPage = true, slTab = true, slItem = true, slUpdate = true, slAll = true, slStock = true;
    bool slAddTake = true, slClear = true;
};

// An item type with its ammunition and head gear.
struct C_SlDef : C_ItemDef {
    int behaviour, ammoId, headgearId;
    C_SlDef(E_ItemRecordType record, int b, int ammo, int headgear)
        : C_ItemDef(record, 0), behaviour(b), ammoId(ammo), headgearId(headgear)
    {
    }
    int GetCarryMode() override { return 0; }
    int GetBehaviour() override { return behaviour; }
    int GetAmmoItemId() override { return ammoId; }
    int GetHeadgearItemId() override { return headgearId; }
};
// A tab control answering its chosen tab.
struct C_SlTabs : C_Control {
    uint32_t chosen = 0;
    explicit C_SlTabs(uint32_t id) : C_Control(nullptr, id, nullptr) {}
    bool HandleMessage(S_ControlMessage *message) override
    {
        GfLog("tabs asked %x", message->id);
        message->value = chosen;
        return true;
    }
};

} // namespace

void StoreListTest(I3D_scene *)
{
    S_StoreListResults r;
    MrInitVtables();
    MsInitVtables();
    g_gfLog.clear();
    g_controlMessages.clear();
    std::map<int, C_ItemDef *> oldDefs = g_gfItemDefs;
    // 30 ammunition for the weapons 31 and 33 (32 another's); 40 head gear of the uniforms 41 and 43
    // (42 a uniform with another); 50 a plain item.
    C_SlDef d30(ITEM_AMMO, 100, -1, -1), d31(ITEM_ANIMATED, 3, 30, -1), d32(ITEM_ANIMATED, 3, 29, -1),
        d33(ITEM_ANIMATED, 4, 30, -1), d34(ITEM_STATIC, 3, 30, -1), d40(ITEM_STATIC, 16, -1, -1), d41(ITEM_STATIC, 15, -1, 40),
        d42(ITEM_STATIC, 15, -1, 39), d43(ITEM_STATIC, 15, -1, 40), d44(ITEM_STATIC, 14, -1, 40), d50(ITEM_STATIC, 2, -1, -1);
    g_gfItemDefs = {{30, &d30}, {31, &d31}, {32, &d32}, {33, &d33}, {34, &d34}, {40, &d40}, {41, &d41},
                    {42, &d42}, {43, &d43}, {44, &d44}, {50, &d50}};
    C_ItemStock stock;
    // The list: control 5 of screen 3.
    const uint32_t base = (5u | 3u << 6) << 20;
    const auto make = [&](C_StoreList &l) {
        l.m_screenIndex = 3;
        l.m_controlIndex = 5;
        l.m_entries.assign(500, 0);
        l.m_stock = &stock;
    };

    // --- a type's count: the stock's (none: 0) ---
    {
        bool ok = true;
        C_StoreList l;
        ok &= l.GetCount(30) == 0;
        make(l);
        g_slCounts = {{30, 7}};
        ok &= l.GetCount(30) == 7 && l.GetCount(31) == 0;
        WM(r.slCount, ok);
    }

    // --- a page shown: the last hidden, the new one's id (3 + the page), the pages' group's value,
    // laid out again; a negative page: nothing ---
    {
        bool ok = true;
        g_controlMessages.clear();
        C_StoreList l;
        make(l);
        l.m_shownPage = 0x777;
        l.ShowPage(2);
        ok &= MsLogIs({EpMsg(0x777, 0x1000004), EpMsg(base | 0x5000, 0x1000003), EpMsg(base | 0x2000, 0x100000b, base | 0x5000),
                       EpMsg(base | 0x5000, 0x200002c)},
                      EpMessages()) &&
              l.m_shownPage == (base | 0x5000);
        l.ShowPage(-1);
        ok &= EpMessages().empty() && l.m_shownPage == (base | 0x5000);
        l.ShowPage(0);
        ok &= EpMessages()[1] == EpMsg(base | 0x3000, 0x1000003);
        WM(r.slPage, ok);
    }

    // --- the tabs: the page the tab control answers (asked with 0x200002F); none: nothing; true ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_StoreList l;
        make(l);
        C_SlTabs tabs(base | 0x1000);
        tabs.chosen = 4;
        ok &= C_StoreList::OnTab(&l, &l, &tabs, CONTROL_NOTIFY_CHANGED, 0) && MsLogIs({"tabs asked 200002f"}, g_gfLog) &&
              l.m_shownPage == (base | 0x7000);
        EpMessages();
        ok &= C_StoreList::OnTab(&l, &l, nullptr, CONTROL_NOTIFY_CHANGED, 0) && EpMessages().empty();
        WM(r.slTab, ok);
    }

    // --- an item control's notifications: the menu's slots (true whatever they answer); the cursor
    // entering: the highlight on the control's frame first (0.1 in front); no menu: nothing ---
    {
        bool ok = true;
        C_StoreList l;
        make(l);
        C_EdMenu menu;
        menu.answer = false;
        S_MrObj *hl = MsFrame("hl"), *cf = MsFrame("cframe");
        C_EpControl control(0x123);
        control.m_frame = As<I3D_frame>(cf);
        l.m_highlight = As<I3D_frame>(hl);
        ok &= C_StoreList::OnItem(&l, &l, &control, CONTROL_NOTIFY_13, 1);
        g_gfLog.clear();
        ok &= C_StoreList::OnItem(&l, &l, &control, CONTROL_NOTIFY_13, 1);
        l.m_menu = &menu;
        g_gfLog.clear();
        const uint32_t notes[] = {CONTROL_NOTIFY_13, CONTROL_NOTIFY_0A, CONTROL_NOTIFY_08, CONTROL_NOTIFY_09,
                                  CONTROL_NOTIFY_LEAVE, CONTROL_NOTIFY_ENTER, CONTROL_NOTIFY_CLICK, CONTROL_NOTIFY_PRESS};
        for (uint32_t n : notes)
            ok &= C_StoreList::OnItem(&l, &menu, &control, n, 5);
        ok &= MsLogIs({"n13 1 123 4013000 5", "n0a 1 123 400a000 5", "n08 1 123 4008000 5", "n09 1 123 4009000 5",
                       "hover 1 123 4004000 5", "hl 38 cframe 0", "hl on 1", "hl 24", "hover 1 123 4003000 5"},
                      g_gfLog) &&
              MsNear(As<I3D_frame>(hl)->GetPos(), S_vector(0.0f, 0.0f, -0.1f));
        g_gfLog.clear();
        ok &= C_StoreList::OnItem(&l, &menu, nullptr, CONTROL_NOTIFY_ENTER, 0) && MsLogIs({"hover 1 0 4003000 0"}, g_gfLog);
        WM(r.slItem, ok);
    }

    // --- a type's entry: its count (no entry: nothing); ammunition: the weapons that take it (the last
    // first) their second count; head gear: the uniforms that come with it; no definition: nothing ---
    {
        bool ok = true;
        g_controlMessages.clear();
        C_StoreList l;
        make(l);
        g_slCounts = {{30, 6}, {40, 2}, {50, 9}};
        for (int t : {30, 31, 32, 33, 34, 40, 41, 42, 43, 44, 50})
            l.m_entries[static_cast<size_t>(t)] = 0x1000u + static_cast<uint32_t>(t);
        ok &= l.UpdateType(50) && MsLogIs({EpMsg(0x1032, 0x100007c, 9)}, EpMessages());
        ok &= l.UpdateType(30) && MsLogIs({EpMsg(0x1021, 0x1000082, 6), EpMsg(0x101f, 0x1000082, 6)}, EpMessages());
        ok &= l.UpdateType(40) && MsLogIs({EpMsg(0x102b, 0x1000082, 2), EpMsg(0x1029, 0x1000082, 2)}, EpMessages());
        ok &= l.UpdateType(31) && MsLogIs({EpMsg(0x101f, 0x100007c, 0)}, EpMessages());
        l.m_entries[31] = 0;
        ok &= l.UpdateType(30) && MsLogIs({EpMsg(0x1021, 0x1000082, 6)}, EpMessages());
        l.m_entries[50] = 0;
        ok &= l.UpdateType(50) && l.UpdateType(99) && l.UpdateType(-1) && EpMessages().empty();
        // Type 0: a weapon taking 30 and a uniform with 40 (the loops reach it, last).
        C_SlDef d0(ITEM_ANIMATED, 15, 30, 40);
        g_gfItemDefs[0] = &d0;
        l.m_entries[0] = 0x1000;
        ok &= l.UpdateType(30) && MsLogIs({EpMsg(0x1021, 0x1000082, 6), EpMsg(0x1000, 0x1000082, 6)}, EpMessages());
        ok &= l.UpdateType(40) && MsLogIs({EpMsg(0x102b, 0x1000082, 2), EpMsg(0x1029, 0x1000082, 2), EpMsg(0x1000, 0x1000082, 2)},
                                          EpMessages());
        g_gfItemDefs.erase(0);
        l.m_entries[0] = 0;
        // (The weapons past the entries are not asked for.)
        l.m_entries.resize(32);
        l.m_entries[31] = 0x101f;
        ok &= l.UpdateType(30) && MsLogIs({EpMsg(0x101f, 0x1000082, 6)}, EpMessages());
        WM(r.slUpdate, ok);
    }

    // --- every type's entry ---
    {
        bool ok = true;
        g_controlMessages.clear();
        C_StoreList l;
        make(l);
        g_slCounts = {{50, 3}};
        l.m_entries[50] = 0x2050;
        l.m_entries[31] = 0x2031;
        // (Type 0 too.)
        C_SlDef d0(ITEM_STATIC, 2, -1, -1);
        g_gfItemDefs[0] = &d0;
        l.m_entries[0] = 0x2000;
        l.UpdateAll();
        ok &= MsLogIs({EpMsg(0x2000, 0x100007c, 0), EpMsg(0x2031, 0x1000082, 0), EpMsg(0x2031, 0x100007c, 0),
                       EpMsg(0x2050, 0x100007c, 3)},
                      EpMessages());
        g_gfItemDefs.erase(0);
        WM(r.slAll, ok);
    }

    // --- the stock set: every entry shown (no stock: nothing, false) ---
    {
        bool ok = true;
        g_controlMessages.clear();
        C_StoreList l;
        make(l);
        l.m_entries[50] = 0x2050;
        g_slCounts = {{50, 4}};
        l.SetStock(nullptr);
        ok &= !l.UpdateAllIfStock() && EpMessages().empty() && !l.m_stock;
        l.SetStock(&stock);
        ok &= l.m_stock == &stock && MsLogIs({EpMsg(0x2050, 0x100007c, 4)}, EpMessages()) && l.UpdateAllIfStock();
        WM(r.slStock, ok);
    }

    // --- an item back into the stock; taken from it (fill: what it lacks added first; none: false) ---
    {
        bool ok = true;
        g_controlMessages.clear();
        C_StoreList l;
        make(l);
        l.m_entries[50] = 0x2050;
        g_slCounts = {{50, 2}};
        g_gfLog.clear();
        l.AddItem(EpMake(1, 50, 3));
        ok &= MsLogIs({"stock add 50 3"}, g_gfLog) && EpMessages().size() == 1;
        S_SoldierItem item = EpMake(1, 50, 5);
        g_gfLog.clear();
        ok &= l.TakeItem(item, false) && item.present == 2 && MsLogIs({"stock take 50 5"}, g_gfLog) &&
              MsLogIs({EpMsg(0x2050, 0x100007c, 0)}, EpMessages());
        item = EpMake(1, 50, 5);
        g_gfLog.clear();
        ok &= !l.TakeItem(item, false) && item.present == 5 && EpMessages().empty();
        g_slCounts = {{50, 1}};
        g_gfLog.clear();
        // (The fake stock's Add only logs: 1 is there to take.)
        ok &= l.TakeItem(item, true) && item.present == 1 && MsLogIs({"stock add 50 4", "stock take 50 5"}, g_gfLog);
        // Exactly enough: nothing added.
        g_slCounts = {{50, 5}};
        item = EpMake(1, 50, 5);
        g_gfLog.clear();
        ok &= l.TakeItem(item, true) && item.present == 5 && MsLogIs({"stock take 50 5"}, g_gfLog);
        g_slCounts = {{50, 7}};
        item = EpMake(1, 50, 3);
        g_gfLog.clear();
        ok &= l.TakeItem(item, true) && item.present == 3 && MsLogIs({"stock take 50 3"}, g_gfLog);
        g_controlMessages.clear();
        WM(r.slAddTake, ok);
    }

    // --- the pages emptied; the tabs and the shown page on or off ---
    {
        bool ok = true;
        g_controlMessages.clear();
        C_StoreList l;
        make(l);
        l.m_shownPage = base | 0x4000;
        l.ClearPages();
        std::vector<std::string> want;
        for (uint32_t k = 3; k <= 8; ++k)
            want.push_back(EpMsg(base | k << 12, 0x2000085));
        ok &= MsLogIs(want, EpMessages());
        l.SetActive(false);
        ok &= MsLogIs({EpMsg(base | 0x1000, 0x1000001), EpMsg(base | 0x4000, 0x1000004)}, EpMessages());
        l.SetActive(true);
        ok &= MsLogIs({EpMsg(base | 0x1000, 0x1000002), EpMsg(base | 0x4000, 0x1000003)}, EpMessages());
        WM(r.slClear, ok);
    }

    g_gfItemDefs = oldDefs;
    g_slCounts.clear();
    g_msChildren.clear();
    g_controlMessages.clear();
    g_gfLog.clear();
    std::printf("storelist: slCount %d slPage %d slTab %d slItem %d slUpdate %d slAll %d slStock %d slAddTake %d "
                "slClear %d\n",
                r.slCount, r.slPage, r.slTab, r.slItem, r.slUpdate, r.slAll, r.slStock, r.slAddTake, r.slClear);
}
