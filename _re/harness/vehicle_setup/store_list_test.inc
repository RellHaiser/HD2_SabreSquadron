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

// The item types' inventory groups (item_defs.cpp is not linked).
std::map<const C_ItemDef *, int> g_slGroups;
int C_ItemDef::GetInventoryGroup()
{
    const auto it = g_slGroups.find(this);
    return it == g_slGroups.end() ? 0 : it->second;
}

namespace {

struct S_StoreListResults {
    bool slCount = true, slPage = true, slTab = true, slItem = true, slUpdate = true, slAll = true, slStock = true;
    bool slAddTake = true, slClear = true, slBuild = true, slBuildNone = true;
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

    // --- a page built: the page's dummy under "P_visualstore" (named, the highlight's copy, the template
    // linked to it), its description (the pages' group, the step, the origin, hidden); an entry per
    // type of the group (not another team's; group 6 not its head gear): the template's copy with
    // its own meshes, "frame_<type>", its messages; group 1 the page shown ---
    {
        bool ok = true;
        const int driverSlots[] = {0x54 / 4};
        void *oldDriver = g_driverVtable[driverSlots[0]];
        g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
        g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        g_msFrameVt[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
        g_msFrameVt[0x7C / 4] = reinterpret_cast<void *>(&MrFake<0x7C, void *>);
        g_msFrameVt[0xA0 / 4] = reinterpret_cast<void *>(&MrFake<0xA0>);
        d31.m_team = 0;
        d33.m_team = 3;
        g_slGroups = {{&d31, 2}, {&d33, 2}, {&d50, 2}, {&d40, 6}, {&d41, 6}, {&d42, 1}};
        C_EdMenu menu;
        S_MrObj *model = MsFrame("smodel"), *anchor = MsFrame("anchor"), *hl = MsFrame("hl8"), *tmpl = MsFrame("tmpl");
        As<I3D_frame>(tmpl)->m_localMatrix.m[3][0] = 0.25f;
        As<I3D_frame>(tmpl)->m_localMatrix.m[3][1] = 0.5f;
        As<I3D_frame>(tmpl)->m_localMatrix.m[3][2] = 0.75f;
        g_msChildren[{model, "P_visualstore"}] = anchor;
        g_msChildren[{model, "selitem08"}] = hl;
        g_msChildren[{model, "vi_template"}] = tmpl;
        S_MrObj *mesh = MrObj("mesh", g_msFrameVt);
        S_MrObj *dummy = MsFrame("pdummy"), *hlCopy = MsFrame("hlcopy"), *c31 = MsFrame("c31"), *c50 = MsFrame("c50"),
                *ammo = MsFrame("cammo");
        for (S_MrObj *c : {c31, c50, ammo})
            *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(c) + 0x1E0) = mesh;
        g_msChildren[{c50, "ammo"}] = ammo;
        g_mrMade.clear();
        g_mrMade[FRAME_DUMMY] = {dummy};
        g_mrClones = {As<I3D_frame>(hlCopy), As<I3D_frame>(c31), As<I3D_frame>(c50)};
        C_StoreList l;
        make(l);
        l.m_menu = &menu;
        l.m_entries.clear();
        S_ControlDesc top(5, "store", 0x08300000);
        top.AddChild(1, "tabs");
        g_gfLog.clear();
        ok &= l.BuildPage(&top, As<I3D_frame>(model), C_str("page2"), 2);
        ok &= MsLogIs({"create 6", "smodel 48 P_visualstore 20", "smodel 48 selitem08 1", "smodel 48 selitem08 1",
                       "smodel 48 vi_template 1", "pdummy 38 anchor 0", "smodel 7c pdummy", "pdummy 8 (0 0 0)",
                       "pdummy 34 page2", "clone tree hl8 0", "tmpl 38 pdummy 1", "tmpl on 1", "pdummy on 1",
                       "clone tree tmpl 0", "c31 a0", "mesh 18", "c31 48 ammo 1", "c31 34 frame_31", "c31 38 pdummy 0",
                       "clone tree tmpl 0", "c50 a0", "mesh 18", "c50 48 ammo 1", "cammo a0", "mesh 18", "c50 34 frame_50",
                       "c50 38 pdummy 0", "tmpl on 0", "tmpl 24"},
                      g_gfLog);
        ok &= l.m_highlight == As<I3D_frame>(hl) && l.m_template == As<I3D_frame>(tmpl) &&
              menu.GetModels() == std::vector<I3D_frame *>({As<I3D_frame>(dummy)}) &&
              menu.GetFrames() == std::vector<I3D_frame *>({As<I3D_frame>(hlCopy), As<I3D_frame>(c31), As<I3D_frame>(c50)}) &&
              l.m_shownPage == 0 && l.m_entries.size() == 500;
        // The description.
        std::vector<std::string> got;
        g_edMenu = &menu;
        g_edPanel = &l;
        ok &= top.children.size() == 2;
        if (ok)
            EdDump(top.children[1], 0, got);
        const S_ControlMessage &origin = top.children[1]->messages[3];
        const auto *o = reinterpret_cast<const float *>(origin.value2);
        ok &= o && o[0] == 0.25f && o[1] == 0.5f && o[2] == 0.75f && o[3] == 1.0f;
        const std::string originText = CmText("  0 100006b 0 %x 0 ", static_cast<uint32_t>(origin.value2));
        const uint32_t page = 0x08302000;
        ok &= MsLogIs({CmText("0 23 page2 %x", page), CmText("  0 100000b %x 0 0 ", base | 0x2000), "  0 1000028 4 0 0 ",
                       "  0 1000029 0 3da5e354 0 ", originText, "  0 1000004 0 0 0 ", "  0 2000085 0 0 0 ",
                       "  0 200002c 0 0 0 ", CmText("1 1d frame_31 %x", page | 0x10), "  0 100007b 1f 0 0 ", "  0 1000009 1f 0 0 ",
                       CmText("  0 1000007 4021000 panel %x ", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&C_StoreList::OnItem))),
                       CmText("1 1d frame_50 %x", page | 0x20), "  0 100007b 32 0 0 ", "  0 1000009 32 0 0 ",
                       CmText("  0 1000007 4021000 panel %x ", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&C_StoreList::OnItem)))},
                      got);
        ok &= l.m_entries[31] == (page | 0x10) && l.m_entries[50] == (page | 0x20) && l.m_entries[33] == 0;
        _aligned_free(reinterpret_cast<void *>(top.children[1]->messages[3].value2));
        // Group 6: not its head gear; group 1: the page shown first.
        S_MrObj *dummy2 = MsFrame("pdummy2"), *hlCopy2 = MsFrame("hlcopy2"), *c41 = MsFrame("c41");
        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(c41) + 0x1E0) = mesh;
        g_mrMade[FRAME_DUMMY] = {dummy2};
        g_mrClones = {As<I3D_frame>(hlCopy2), As<I3D_frame>(c41)};
        g_gfLog.clear();
        ok &= l.BuildPage(&top, As<I3D_frame>(model), C_str("page6"), 6);
        ok &= std::count(g_gfLog.begin(), g_gfLog.end(), std::string("clone tree tmpl 0")) == 1 && MrHas("c41 34 frame_41") &&
              top.children[2]->children.size() == 1 && l.m_shownPage == 0;
        _aligned_free(reinterpret_cast<void *>(top.children[2]->messages[3].value2));
        S_MrObj *dummy3 = MsFrame("pdummy3"), *hlCopy3 = MsFrame("hlcopy3"), *c42 = MsFrame("c42");
        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(c42) + 0x1E0) = mesh;
        g_mrMade[FRAME_DUMMY] = {dummy3};
        g_mrClones = {As<I3D_frame>(hlCopy3), As<I3D_frame>(c42)};
        ok &= l.BuildPage(&top, As<I3D_frame>(model), C_str("page1"), 1) && l.m_shownPage == 0x08304000;
        _aligned_free(reinterpret_cast<void *>(top.children[3]->messages[3].value2));
        // Group 3: type 0 too, and head gear outside the uniforms' group kept.
        C_SlDef d0(ITEM_STATIC, 2, -1, -1), d46(ITEM_STATIC, 16, -1, -1);
        g_gfItemDefs[0] = &d0;
        g_gfItemDefs[46] = &d46;
        g_slGroups[&d0] = 3;
        g_slGroups[&d46] = 3;
        S_MrObj *dummy4 = MsFrame("pdummy4"), *hlCopy4 = MsFrame("hlcopy4"), *c0 = MsFrame("c0"), *c46 = MsFrame("c46");
        for (S_MrObj *c : {c0, c46})
            *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(c) + 0x1E0) = mesh;
        g_mrMade[FRAME_DUMMY] = {dummy4};
        g_mrClones = {As<I3D_frame>(hlCopy4), As<I3D_frame>(c0), As<I3D_frame>(c46)};
        ok &= l.BuildPage(&top, As<I3D_frame>(model), C_str("page3"), 3) && top.children[4]->children.size() == 2 &&
              l.m_entries[0] == 0x08305010 && l.m_entries[46] == 0x08305020;
        _aligned_free(reinterpret_cast<void *>(top.children[4]->messages[3].value2));
        g_gfItemDefs.erase(0);
        g_gfItemDefs.erase(46);
        WM(r.slBuild, ok);

        // --- without a frame (the anchor, the highlight, the template) or a dummy: false, nothing made ---
        ok = true;
        for (int pass = 0; pass < 4; ++pass) {
            g_msChildren[{model, "P_visualstore"}] = pass == 0 ? nullptr : anchor;
            g_msChildren[{model, "selitem08"}] = pass == 1 ? nullptr : hl;
            g_msChildren[{model, "vi_template"}] = pass == 2 ? nullptr : tmpl;
            g_mrMade[FRAME_DUMMY] = {};
            if (pass != 3)
                g_mrMade[FRAME_DUMMY] = {MsFrame("dx")};
            g_gfLog.clear();
            const size_t children = top.children.size();
            ok &= !l.BuildPage(&top, As<I3D_frame>(model), C_str("px"), 2) && top.children.size() == children &&
                  g_gfLog.size() == 5;
        }
        WM(r.slBuildNone, ok);
        g_driverVtable[driverSlots[0]] = oldDriver;
        g_mrClones.clear();
        g_slGroups.clear();
        g_edMenu = g_edPanel = nullptr;
        MsInitVtables();
    }

    g_gfItemDefs = oldDefs;
    g_slCounts.clear();
    g_msChildren.clear();
    g_controlMessages.clear();
    g_gfLog.clear();
    std::printf("storelist: slCount %d slPage %d slTab %d slItem %d slUpdate %d slAll %d slStock %d slAddTake %d "
                "slClear %d slBuild %d slBuildNone %d\n",
                r.slCount, r.slPage, r.slTab, r.slItem, r.slUpdate, r.slAll, r.slStock, r.slAddTake, r.slClear,
                r.slBuild, r.slBuildNone);
}
