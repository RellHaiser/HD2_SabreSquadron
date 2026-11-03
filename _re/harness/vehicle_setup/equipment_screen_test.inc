// Part 9f's equipment screen (game/equipment_screen.cpp), in the factory build, run by ControlsTest
// after OptionsTest: the slots not ported yet stubbed (parts 9f-i, -ii), the fakes game_menu_test.inc's and
// its helpers (the queue's text, the logs joined, the controls answering their index). Prints
// "equipmentscreen:" with 1 per check met.
#define private public
#define protected public
#include "game/equipment_screen.h"
#undef protected
#undef private

// (Not ported yet: logged.)
// (Part 9f-ii: the campaign's mission number; campaign.cpp is not linked.)
int g_esMission;
int C_Campaign::GetMissionNumber(int campaign)
{
    GfLog("mission number %d", campaign);
    return g_esMission;
}
bool C_EquipmentMenu::BuildScreens()
{
    GfLog("es build");
    return true;
}
bool C_EquipmentMenu::OnItemNotify60(void *, C_Control *, uint32_t, uint32_t)
{
    GfLog("es type");
    return true;
}

// The campaign screen's items and kind (private: reached by the explicit instantiation's access).
template <class Tag, typename Tag::type M> struct S_EsRob {
    friend typename Tag::type EsGet(Tag) { return M; }
};
struct S_EsItemsTag {
    using type = std::vector<S_CampaignItem> C_CampaignScreen::*;
    friend type EsGet(S_EsItemsTag);
};
template struct S_EsRob<S_EsItemsTag, &C_CampaignScreen::m_items>;

namespace {

struct S_EquipmentScreenResults {
    bool esNew = true, esStart = true, esStop = true, esStore = true, esTake = true, esClicked = true;
    bool esDragStart = true, esDragMove = true, esDrop = true, esHover = true, esStats = true, esToggle = true;
    bool esPicked = true, esMessage = true, esChecks = true, esCarried = true, esRequired = true;
};

// A frame of the screen's model at a world position (its SetPos logged: "<name> 8 (x y z)").
I3D_frame *EsAt(const char *name, float x, float y, float z)
{
    static void **vt = nullptr;
    if (!vt) {
        vt = new void *[MR_SLOTS];
        std::copy(g_msFrameVt, g_msFrameVt + MR_SLOTS, vt);
        vt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
    }
    S_MrObj *o = MsFrame(name);
    o->vtable = vt;
    I3D_frame *frame = As<I3D_frame>(o);
    frame->m_worldMatrix.m[3][0] = x;
    frame->m_worldMatrix.m[3][1] = y;
    frame->m_worldMatrix.m[3][2] = z;
    return frame;
}

} // namespace

void EquipmentScreenTest(I3D_scene *scene)
{
    S_EquipmentScreenResults r;
    g_gfLog.clear();
    const std::map<std::string, std::vector<uint8_t>> oldFiles = g_files;
    alignas(16) static uint8_t campaignMemory[sizeof(C_Campaign)];
    std::memset(campaignMemory, 0, sizeof campaignMemory);
    C_Campaign *const oldCampaign = g_gameProfile.m_campaign;
    g_gameProfile.m_campaign = reinterpret_cast<C_Campaign *>(campaignMemory);
    S_MissionSquad squad;
    S_MissionSquad *const oldSquad = g_gameProfile.m_missionSquad;
    g_gameProfile.m_missionSquad = &squad;
    C_ItemStock stock18, stock1C;
    C_ItemStock *const old18 = g_gameProfile.m_itemStock18, *const old1C = g_gameProfile.m_itemStock1C;
    g_gameProfile.m_itemStock18 = &stock18;
    g_gameProfile.m_itemStock1C = &stock1C;
    S_PlayMode playMode;
    S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
    g_gameProfile.m_playMode = &playMode;
    g_lpMissions = {2};
    {
        std::vector<uint8_t> presets(8 + sizeof(S_LoadoutPreset));
        reinterpret_cast<int *>(presets.data())[0] = 2;
        reinterpret_cast<int *>(presets.data())[1] = 1;
        g_files["GameData\\presets.def"] = presets;
    }
    g_files.erase("prof\\user_presets.def");
    g_files.erase("GameData\\miss_itm.def");
    const std::map<int, C_ItemDef *> oldDefs = g_gfItemDefs;

    // --- made: the presets loaded, the missions' items set up; the fields' first values; deleted ---
    {
        g_gfLog.clear();
        auto *m = new C_EquipmentMenu();
        WM(r.esNew, MsLogIs({"operations 0", "missions 0 0"}, g_gfLog) && g_menuState == m &&
                        m->GetTypeId() == GAME_STATE_TYPE_EQUIPMENT_MENU && m->m_presets.GetPresetCount() == 1 &&
                        m->m_presets.GetUserPresetCount() == 24 && m->m_missionItems.m_records[0] &&
                        m->m_missionItems.m_records[1] && !m->m_missionItems.m_records[2]);
        WM(r.esNew, C_GmBasePeek::Fresh(*m) && !m->m_selector && !m->m_soldierSelector && m->m_soldier == 0 &&
                        m->m_selected == 0 && m->m_panel.m_soldierIndex == -1 && !m->m_storeList.m_stock);
        const uint8_t *bytes = m->m_unknown1C4;
        WM(r.esNew, std::all_of(bytes, bytes + sizeof m->m_unknown1C4, [](uint8_t b) { return b == 0; }));
        g_gfLog.clear();
        delete m;
        WM(r.esNew, !g_menuState && g_gfLog.empty());
    }

    // --- started: no screen nothing; the kinds but the equipment's only the items loaded; the
    // equipment's: the first stock reset, the screen's items added (those of ammunition: the item's
    // rounds; an unknown item's nothing) ---
    {
        C_EquipmentMenu m;
        bool ok = true;
        g_gfLog.clear();
        ok &= m.OnStart(nullptr) && g_gfLog.empty();
        // (The screen's class is not linked: its items' vector made in zeroed memory.)
        alignas(16) static uint8_t screenMemory[sizeof(C_CampaignScreen)];
        std::memset(screenMemory, 0, sizeof screenMemory);
        auto &screen = *reinterpret_cast<C_CampaignScreen *>(screenMemory);
        std::vector<S_CampaignItem> &items = screen.*EsGet(S_EsItemsTag());
        new (&items) std::vector<S_CampaignItem>{{3, 2, -1, false}, {4, 25, -1, true}, {9, 7, -1, true}, {4, 1, 5, false}};
        C_SlDef gun(ITEM_ANIMATED, 2, 33, -1);
        g_gfItemDefs = {{4, &gun}};
        m.m_mode = 4;
        g_gfLog.clear();
        ok &= m.OnStart(&screen) && MsLogIs({"items loaded"}, g_gfLog);
        m.m_mode = C_CampaignScreen::KIND_EQUIPMENT;
        g_gfLog.clear();
        ok &= m.OnStart(&screen) &&
              MsLogIs({"items loaded", "stock reset", "stock add 3 2", "stock add 33 25", "stock add 4 1"}, g_gfLog);
        items.~vector();
        g_gfItemDefs = oldDefs;
        WM(r.esStart, ok);
    }

    // --- stopped: the popup deleted, the panel's soldier, the store's pages emptied (a page's list
    // hidden) ---
    {
        C_EquipmentMenu m;
        m.m_popup = new C_MsScreen(scene, "espopup", 0x04000000);
        m.m_panel.m_soldier = new C_MenuSoldier();
        m.m_storeList.m_screenIndex = 1;
        m.m_storeList.m_controlIndex = 4;
        g_controlMessages.clear();
        g_gfLog.clear();
        m.OnStop();
        bool ok = !m.m_popup && MsLogIs({"cursor release"}, g_gfLog) && !m.m_panel.m_soldier;
        const std::string q = GmQueue();
        ok &= q.find("4403000") != std::string::npos;
        // Again: nothing more to delete.
        m.OnStop();
        ok &= !m.m_popup;
        g_controlMessages.clear();
        WM(r.esStop, ok);
    }

    // --- the store's stock: a campaign's by the kind (3 the first, 4 the second, else none); a single
    // mission's the first, the squad's items taken ---
    {
        C_EquipmentMenu m;
        bool ok = true;
        const auto store = [&](int mode, int single) {
            m.m_mode = mode;
            playMode.singleMission = single;
            m.m_storeList.m_stock = nullptr;
            g_gfLog.clear();
            m.SetupStore();
            return m.m_storeList.m_stock;
        };
        ok &= store(3, 0) == &stock18 && MsLogIs({"items loaded"}, g_gfLog);
        ok &= store(4, 0) == &stock1C && MsLogIs({"items loaded"}, g_gfLog);
        ok &= !store(5, 0) && !store(1, 0) && MsLogIs({"items loaded"}, g_gfLog);
        // (Single: the squad's soldiers, none here.)
        ok &= store(4, 1) == &stock18 && MsLogIs({"items loaded"}, g_gfLog);
        S_SquadSoldier a;
        a.head = EpMake(1, 3, 1);
        squad.states = {&a};
        g_slCounts = {{3, 1}};
        ok &= store(5, 1) == &stock18 && MsLogIs({"items loaded", "stock take 3 1", "update load"}, g_gfLog);
        squad.states.clear();
        playMode.singleMission = 0;
        WM(r.esStore, ok);

        // The squad's items taken: every soldier's (the last first), as the in-game menu's soldier's.
        bool okt = true;
        m.m_storeList.SetStock(&stock18);
        S_SquadSoldier sq;
        sq.head = EpMake(1, 3, 1);
        sq.uniform = EpMake(2, 4, 1);
        sq.hands = EpMake(3, 6, 1);
        sq.longGun = EpMake(9, 5, 1);
        sq.belt = {EpMake(4, 7, 1), EpMake(5, 8, 1)};
        sq.backpack = EpMake(6, 9, 1);
        sq.backpackContents = {EpMake(7, 10, 1)};
        S_SquadSoldier sb;
        sb.backpack = EpMake(6, 12, 1);
        sb.backpackContents = {EpMake(7, 10, 1), EpMake(8, 11, 1)};
        squad.states = {&sq, &sb};
        g_slCounts = {{3, 1}, {5, 1}, {6, 2}, {7, 1}, {10, 2}, {12, 1}};
        g_epRemoved.clear();
        g_gfLog.clear();
        m.TakeSquadItems();
        okt &= MsLogIs({"stock take 12 1", "stock take 11 1", "remove 40 8 11 1", "stock take 10 1", "update load",
                        "stock take 3 1", "stock take 4 1", "stock take 5 1", "stock take 6 1", "stock take 8 1",
                        "remove 20 5 8 1", "stock take 7 1", "stock take 9 1", "update load"},
                       g_gfLog) &&
               sq.head.typeId == 3 && sq.uniform.typeId == -1 && sq.uniform.uid == -1 && sq.uniform.present == 0 &&
               sq.hands.typeId == 6 && sq.longGun.typeId == 5 && sq.backpack.typeId == -1 && sq.backpack.uid == -1 &&
               sq.backpack.present == 0 && sq.backpackContents.empty() && sb.backpack.typeId == 12 &&
               sb.backpackContents.size() == 2;
        squad.states.clear();
        g_slCounts.clear();
        WM(r.esTake, okt);
    }

    // --- the item slots: clicked twice (from the panel: back to the store, the backpack with its
    // contents, the cursor's input notified; from the store: taken, dropped onto the soldier, what it
    // displaced (or the item) back; not in the store: false); dragged: its start, its move, its drop ---
    {
        C_EquipmentMenu m;
        S_SquadSoldier sq;
        sq.backpackContents = {EpMake(1, 30, 1)};
        sq.backpack = EpMake(2, 31, 1);
        m.m_panel.m_squadSoldier = &sq;
        for (auto &h : m.m_panel.m_highlights)
            h = As<I3D_frame>(MsFrame("esdh"));
        I3D_frame *const firstHighlight = m.m_panel.m_highlights[0];
        C_ItemStock stock;
        m.m_storeList.SetStock(&stock);
        g_slCounts.clear();
        g_epRemoved.clear();
        g_controlMessages.clear();
        const uint32_t head = 0x04101000, backpack = 0x04103000, uniform = 0x04102000, store = 0x04401000;
        // (The cursor's input notified last, after the store's and the panel's updates.)
        const std::string notified = "0 10000a1 4004000 0 ; 0 10000a1 4005000 0 ; ";
        const auto endsNotified = [&notified](const std::string &q) {
            return q.size() >= notified.size() && q.compare(q.size() - notified.size(), notified.size(), notified) == 0;
        };
        const auto clicked = [&](uint32_t id, int type) {
            C_GmIndexed control(id, 0);
            control.m_index = type;
            g_gfLog.clear();
            g_controlMessages.clear();
            return m.OnItemNotify13(&m, &control, CONTROL_NOTIFY_13, 0);
        };
        bool okc = clicked(head, -1) && g_gfLog.empty() && GmQueue().empty();
        g_epRemoved = {1};
        okc &= clicked(head, 5) && GmInOrder(Join(g_gfLog), {"remove 1 -1 5 1", "stock add 5 1"}) && endsNotified(GmQueue());
        okc &= clicked(head, 5) && MsLogIs({"remove 1 -1 5 1"}, g_gfLog) && endsNotified(GmQueue());
        m.m_storeList.m_entries.resize(500);
        m.m_storeList.m_entries[31] = 0x044F1000;
        C_SlDef d31(ITEM_STATIC, 0, -1, -1);
        g_gfItemDefs[31] = &d31;
        okc &= clicked(backpack, 31) && GmInOrder(Join(g_gfLog), {"remove 40 1 30 1", "remove 8 2 31 1"});
        std::string q = GmQueue();
        okc &= q.find("44f1000") != std::string::npos && endsNotified(q);
        g_gfItemDefs = oldDefs;
        // (Type 0 an item too.)
        g_epRemoved = {1};
        okc &= clicked(head, 0) && MrHas("remove 1 -1 0 1");
        // From the store (or another control): not there, false; there: taken, dropped (no type the
        // soldier takes: given back), no notification.
        okc &= !clicked(store, 7) && GfLogIs({"stock take 7 1"}) && GmQueue().empty();
        okc &= !clicked(0x04201000, 7) && GfLogIs({"stock take 7 1"});
        g_slCounts[7] = 1;
        okc &= clicked(store, 7) && GmInOrder(Join(g_gfLog), {"stock take 7 1", "stock add 7 1"}) && GmQueue().empty();
        C_SlDef d7(ITEM_STATIC, 0, -1, -1);
        g_gfItemDefs = {{7, &d7}};
        g_epAllowed[&d7] = 1;
        sq.head = EpMake(-1, -1, 0);
        g_slCounts[7] = 1;
        okc &= clicked(store, 7) && GmInOrder(Join(g_gfLog), {"stock take 7 1", "put 1 -1 7 1"}) && !MrHas("stock add 7 1") &&
               !MrHas("stock add -1 0");
        g_slCounts[7] = 1;
        g_epDisplaced = {EpMake(9, 12, 2)};
        okc &= clicked(store, 7) && GmInOrder(Join(g_gfLog), {"stock take 7 1", "put 1 -1 7 1", "stock add 12 2"});
        g_slCounts[7] = 1;
        g_epDisplaced = {EpMake(9, 7, 4)};
        okc &= clicked(store, 7) && GmInOrder(Join(g_gfLog), {"put 1 -1 7 1", "stock add 7 1"}) && !MrHas("stock add 7 4");
        // (A large weapon with another: taken all the same.)
        C_SlDef large(ITEM_ANIMATED, 0, -1, -1);
        large.m_inventorySize = 2;
        g_gfItemDefs = {{8, &large}};
        sq.hands = EpMake(3, 8, 1);
        g_slCounts[8] = 1;
        g_epDisplaced.clear();
        okc &= clicked(store, 8) && MrHas("stock take 8 1");
        sq.hands = EpMake(-1, -1, 0);
        g_epAllowed.erase(&d7);
        g_gfItemDefs = oldDefs;
        WM(r.esClicked, okc);

        // The drag's start: no store check (none there, a large weapon with another: dragged).
        void **const copyVt = new void *[MR_SLOTS];
        std::copy(g_msFrameVt, g_msFrameVt + MR_SLOTS, copyVt);
        copyVt[0x4C / 4] = reinterpret_cast<void *>(&MrFake<0x4C, void *>);
        copyVt[0x04 / 4] = reinterpret_cast<void *>(&MrFake<0x04>);
        copyVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        S_MrObj *source = MsFrame("esds"), *copy = MsFrame("esdc");
        source->vtable = copyVt;
        copy->vtable = copyVt;
        As<I3D_frame>(source)->m_worldMatrix.m[3][0] = 0.25f;
        As<I3D_frame>(source)->m_worldMatrix.m[3][1] = -0.5f;
        As<I3D_frame>(source)->m_worldMatrix.m[3][2] = 0.75f;
        S_MrWorld world = MrWorld("esd");
        m.m_scene = As<I3D_scene>(world.scene);
        void *const oldVisual = g_driverVtable[0x50 / 4];
        g_driverVtable[0x50 / 4] = reinterpret_cast<void *>(&GmCreateVisual);
        g_gmVisual = As<I3D_frame>(copy);
        const auto start = [&](uint32_t id, int type) {
            C_GmIndexed control(id, type);
            control.m_frame = As<I3D_frame>(source);
            g_gfLog.clear();
            return m.OnItemNotify0A(&m, &control, CONTROL_NOTIFY_0A, 0);
        };
        bool oks = start(store, -1) && C_GmBasePeek::DragType(m) == -1 && GfLogIs({"index 100000a"});
        g_slCounts[7] = 0;
        oks &= start(store, 7) && C_GmBasePeek::DragType(m) == 7 && C_GmBasePeek::DragItem(m).typeId == 7 &&
               C_GmBasePeek::DragItem(m).present == 1 && C_GmBasePeek::DragVisual(m) == As<I3D_frame>(copy);
        oks &= GmInOrder(Join(g_gfLog), {"index 100000a", "esdh on 0", "esds 4", "create visual 0", "esdc 4c esds",
                                         "esdc 38 sector 0", "esdc c (0 0 1) 0", "esdc 8 (0.25 -0.5 0.85)", "esdc on 1",
                                         "esdc 24"});
        C_GmBasePeek::DragType(m) = -1;
        oks &= start(head, 0) && C_GmBasePeek::DragType(m) == 0;
        // (The places shown: from the panel the weight ignored, from the store not.)
        C_SlDef heavy(ITEM_STATIC, 0, -1, -1);
        heavy.m_weight = 50.0f;
        g_gfItemDefs = {{10, &heavy}};
        g_epAllowed[&heavy] = 1;
        g_epFreeWeight = 10.0f;
        oks &= start(head, 10) && firstHighlight->IsOn();
        oks &= start(store, 10) && !firstHighlight->IsOn();
        oks &= start(0x04201000, 10) && !firstHighlight->IsOn();
        g_epFreeWeight = 100.0f;
        g_epAllowed.erase(&heavy);
        g_gfItemDefs = oldDefs;
        // (No visual made: no copy.)
        g_gmVisual = nullptr;
        oks &= start(head, 9) && C_GmBasePeek::DragType(m) == 9 && !C_GmBasePeek::DragVisual(m) && !MrHas("esdc 24");
        g_gmVisual = As<I3D_frame>(copy);
        C_GmBasePeek::DragType(m) = -1;
        WM(r.esDragStart, oks);

        // Its move: the copy by the cursor's move (its depth the move's).
        bool okm = true;
        m.m_cursor = C_Cursor::Acquire(m.m_scene);
        C_GmBasePeek::DragVisual(m) = As<I3D_frame>(copy);
        As<I3D_frame>(copy)->m_worldMatrix.m[3][0] = 0.5f;
        As<I3D_frame>(copy)->m_worldMatrix.m[3][1] = 0.25f;
        As<I3D_frame>(copy)->m_worldMatrix.m[3][2] = 0.5f;
        g_csCursor.move = S_vector(0.125f, -0.25f, -1.0f);
        C_GmBasePeek::DragType(m) = 9;
        g_gfLog.clear();
        okm &= m.OnItemNotify08(&m, nullptr, CONTROL_NOTIFY_08, 0) && GfLogIs({"esdc 8 (0.625 0 -1)", "esdc 24"});
        C_GmBasePeek::DragType(m) = 0;
        g_gfLog.clear();
        okm &= m.OnItemNotify08(&m, nullptr, CONTROL_NOTIFY_08, 0) && GfLogIs({"esdc 8 (0.625 0 -1)", "esdc 24"});
        C_GmBasePeek::DragType(m) = -1;
        g_gfLog.clear();
        okm &= m.OnItemNotify08(&m, nullptr, CONTROL_NOTIFY_08, 0) && g_gfLog.empty();
        g_csCursor.move = S_vector(0.0f, 0.0f, 0.0f);
        WM(r.esDragMove, okm);

        // Its drop: the copy released, the places hidden, dropped on what is under the cursor; ended.
        bool okd = true;
        C_MsScreen top(m.m_scene, "esdtop", 0x04000000);
        g_menuScreens = {&top};
        {
            C_SlDef light(ITEM_STATIC, 0, -1, -1);
            g_gfItemDefs = {{11, &light}};
            g_epAllowed[&light] = 1;
            C_GmBasePeek::DragType(m) = 11;
            C_GmBasePeek::DragItem(m) = EpMake(-1, 11, 1);
            firstHighlight->SetOn(true);
            C_GmIndexed control(store, 11);
            okd &= m.OnItemNotify09(&m, &control, CONTROL_NOTIFY_09, 0) && !firstHighlight->IsOn();
            // (Under the cursor the panel's uniform: put there, taken.)
            S_MrObj *hit = MsFrame("esdhit");
            As<I3D_frame>(hit)->SetData(reinterpret_cast<S_FrameData *>(static_cast<uintptr_t>(uniform)));
            g_mrRet[{world.scene, 0x104}] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hit));
            g_epAllowed[&light] = 2;
            g_slCounts[11] = 1;
            C_GmBasePeek::DragType(m) = 0;
            C_GmBasePeek::DragItem(m) = EpMake(-1, 11, 1);
            g_gfLog.clear();
            okd &= m.OnItemNotify09(&m, &control, CONTROL_NOTIFY_09, 0) &&
                   GmInOrder(Join(g_gfLog), {"put 2 -1 11 1", "stock take 11 1"}) && C_GmBasePeek::DragType(m) == -1;
            g_mrRet.erase({world.scene, 0x104});
            g_epAllowed.erase(&light);
            g_gfItemDefs = oldDefs;
            g_slCounts.clear();
        }
        C_GmBasePeek::DragType(m) = 7;
        C_GmBasePeek::DragItem(m) = EpMake(4, 7, 1);
        C_GmBasePeek::DragVisual(m) = As<I3D_frame>(copy);
        {
            C_GmIndexed control(store, 7);
            g_gfLog.clear();
            okd &= m.OnItemNotify09(&m, &control, CONTROL_NOTIFY_09, 0) && !C_GmBasePeek::DragVisual(m) &&
                   C_GmBasePeek::DragType(m) == -1 && C_GmBasePeek::DragItem(m).typeId == -1 &&
                   C_GmBasePeek::DragItem(m).present == 0 && C_GmBasePeek::DragItem(m).uid == 4 &&
                   GmInOrder(Join(g_gfLog), {"esdc 0", "esdh on 0"}) && !MrHas("stock take 7 1");
            C_GmBasePeek::DragVisual(m) = As<I3D_frame>(copy);
            g_gfLog.clear();
            okd &= m.OnItemNotify09(&m, &control, CONTROL_NOTIFY_09, 0) && GfLogIs({"esdc 0"}) && !C_GmBasePeek::DragVisual(m);
        }
        // The drop: from the panel, moved onto another place of it, then back to the store with the
        // cursor right of the middle (its count; the backpack with its contents); from the store onto
        // a place (taken; what it displaced back unless of the type), to nowhere nothing.
        const auto drop = [&](uint32_t from, uint32_t target, int type, float x) {
            C_GmIndexed control(from, type);
            C_GmBasePeek::DragItem(m) = EpMake(-1, type, 1);
            g_csCursor.position = S_vector(x, 0.25f, 0.0f);
            g_gfLog.clear();
            return m.DropItem(&control, target);
        };
        g_epCount = 3;
        g_epRemoved = {3};
        okd &= drop(head, 0, 5, 0.75f) && GmInOrder(Join(g_gfLog), {"count 1 -1 5 1", "remove 1 -1 5 3", "stock add 5 3"});
        okd &= drop(head, 0, 5, 0.5f) && g_gfLog.empty();
        okd &= drop(head, 0, 5, 0.25f) && g_gfLog.empty();
        okd &= drop(head, store, 5, 0.25f) && g_gfLog.empty();
        okd &= drop(head, 0, 5, std::nanf("")) && g_gfLog.empty();
        g_epRemoved = {3};
        okd &= drop(head, store, 5, 0.5001f) && MrHas("stock add 5 3");
        g_controlMessages.clear();
        g_gfItemDefs[31] = &d31;
        okd &= drop(backpack, 0, 31, 0.75f) && MrHas("remove 8 2 31 1") && !MrHas("count 8 -1 31 1") &&
               GmQueue().find("44f1000") != std::string::npos;
        g_gfItemDefs.erase(31);
        g_epCount = 1;
        g_epRemoved = {0};
        okd &= drop(head, 0, 5, 0.75f) && !MrHas("stock add 5 1");
        // (Onto another place: moved; then the cursor's side.)
        C_SlDef d5(ITEM_STATIC, 0, -1, -1), d6(ITEM_STATIC, 0, -1, -1);
        g_gfItemDefs = {{5, &d5}, {6, &d6}};
        g_epAllowed[&d5] = g_epAllowed[&d6] = 3;
        g_epRemoved = {1};
        okd &= drop(head, uniform, 5, 0.25f) && GmInOrder(Join(g_gfLog), {"remove 1 -1 5 1", "put 2 -1 5 1"}) &&
               !MrHas("stock add 5 1") && !MrHas("count 1 -1 5 1");
        g_epRemoved = {1, 1};
        g_epDisplaced = {EpMake(9, 12, 1)};
        // (What the move displaced the panel puts back itself.)
        okd &= drop(head, uniform, 5, 0.75f) &&
               MsLogIs({"remove 1 -1 5 1", "put 2 -1 5 1", "count 1 -1 5 1", "remove 1 -1 5 1", "stock add 5 1"}, g_gfLog);
        g_epDisplaced.clear();
        g_slCounts = {{6, 1}};
        okd &= drop(store, uniform, 6, 0.75f) && GmInOrder(Join(g_gfLog), {"put 2 -1 6 1", "stock take 6 1"}) &&
               !MrHas("stock add 6 1") && !MrHas("stock add -1 0");
        g_slCounts = {{6, 0}};
        g_epDisplaced = {EpMake(9, 12, 1)};
        okd &= drop(store, uniform, 6, 0.75f) && GmInOrder(Join(g_gfLog), {"put 2 -1 6 1", "stock take 6 1", "stock add 12 1"});
        g_epDisplaced = {EpMake(9, 6, 1)};
        okd &= drop(store, uniform, 6, 0.75f) && MrHas("put 2 -1 6 1") && !MrHas("stock take 6 1");
        g_epDisplaced.clear();
        g_epAllowed.erase(&d5);
        g_epAllowed.erase(&d6);
        g_gfItemDefs = oldDefs;
        okd &= drop(store, 0, 6, 0.75f) && g_gfLog.empty();
        okd &= drop(store, store, 6, 0.75f) && g_gfLog.empty();
        WM(r.esDrop, okd);
        g_csCursor.position = S_vector(0.0f, 0.0f, 0.0f);
        g_menuScreens.clear();
        m.m_cursor->Release();
        m.m_cursor = nullptr;
        g_driverVtable[0x50 / 4] = oldVisual;
        g_gmVisual = nullptr;
        g_epCount = 0;
        m.m_panel.m_squadSoldier = nullptr;
        m.m_scene = nullptr;
    }

    // --- the hover: the popup's text the item's name (sent; shown), hidden when left; no popup, no
    // control, the store's items, other notifications, no item: nothing ---
    {
        C_EquipmentMenu m;
        bool okh = true;
        C_GmPopup popup(scene);
        C_Control control(nullptr, 0x04101000, nullptr);
        C_GmHoverDef d(0);
        d.m_textId = 0x345;
        g_gfItemDefs = {{6, &d}};
        const auto hover = [&](uint32_t notification, int type = 6) {
            control.m_index = type;
            g_controlMessages.clear();
            g_gfLog.clear();
            const bool answer = m.OnItemHover(nullptr, &control, notification, 0);
            return CmText("%d ", answer) + GmQueue() + Join(g_gfLog);
        };
        okh &= hover(CONTROL_NOTIFY_ENTER) == "1 ";
        m.m_popup = &popup;
        okh &= hover(CONTROL_NOTIFY_ENTER) == "1 popup 0 1000015 3 345 ; popup 0 1000003 0 345 ; ";
        okh &= hover(CONTROL_NOTIFY_LEAVE) == "1 popup 0 1000004 0 0 ; ";
        okh &= hover(CONTROL_NOTIFY_CLICK) == "1 " && hover(CONTROL_NOTIFY_ENTER | 1) == "1 ";
        okh &= hover(CONTROL_NOTIFY_ENTER, 7) == "1 " && hover(CONTROL_NOTIFY_ENTER, -1) == "1 ";
        okh &= hover(CONTROL_NOTIFY_LEAVE, -1) == "1 popup 0 1000004 0 0 ; ";
        control.m_id = 0x04401000;
        okh &= hover(CONTROL_NOTIFY_ENTER) == "1 " && hover(CONTROL_NOTIFY_LEAVE) == "1 ";
        control.m_id = 0x04501000;
        okh &= hover(CONTROL_NOTIFY_LEAVE) == "1 popup 0 1000004 0 0 ; ";
        g_gfLog.clear();
        okh &= m.OnItemHover(nullptr, nullptr, CONTROL_NOTIFY_ENTER, 0) && g_gfLog.empty();
        m.m_popup = nullptr;
        g_gfItemDefs = oldDefs;
        WM(r.esHover, okh);
    }

    // --- the stats: the shown soldier's profile's name (the custom one, else the full), bars and
    // values; no profile: the bars empty, the values blank, the selection screen's text ---
    {
        C_EquipmentMenu m;
        // (The profiles' class is not linked: zeroed memory, the names set and released.)
        alignas(16) static uint8_t profileMemory[2][sizeof(S_SoldierProfile)];
        std::memset(profileMemory, 0, sizeof profileMemory);
        S_SoldierProfile &p0 = *reinterpret_cast<S_SoldierProfile *>(profileMemory[0]);
        S_SoldierProfile &p1 = *reinterpret_cast<S_SoldierProfile *>(profileMemory[1]);
        p0.fullName = "Full Name";
        p0.customName = "Custom";
        const float stats[9] = {612.5f, 37.25f, 64.5f, 1.0f, 55.5f, 2.0f, 75.0f, 12.25f, 99.5f};
        std::copy(stats, stats + 9, p0.stats);
        p1.fullName = "Other One";
        g_epProfiles = {&p0, nullptr, &p1};
        S_SquadSoldier a, b, c;
        a.profileIndex = 0;
        b.profileIndex = 1;
        c.profileIndex = 2;
        squad.states = {&a, &b, &c};
        bool okt = true;
        g_controlMessages.clear();
        m.m_soldier = 0;
        m.ShowSoldierStats();
        // (A row's bar and value; CmText's buffer holds one row at a time.)
        const auto rowText = [](uint32_t bar, float fill, const char *value) {
            return CmText("%x 1000092 0 %x ; %x 1000014 17 0 %s; ", bar, GmBits(fill), bar + 0x1000, value);
        };
        const std::string full = "4201000 1000014 d 0 Custom; " + rowText(0x4502000, 55.5f * 0.01f, " 56 %") +
                                 rowText(0x4505000, 37.25f * 0.01f, " 37 Kg") + rowText(0x4508000, 1.0f, "612") +
                                 rowText(0x450b000, 64.5f * 0.01f, " 64") + rowText(0x450e000, 75.0f * 0.01f, " 75 %") +
                                 rowText(0x4511000, 12.25f * 0.01f, " 12 %") + rowText(0x4514000, 99.5f * 0.01f, "100 %");
        const std::string got = GmQueue();
        okt &= got == full;
        // (The full name without a custom one.)
        m.m_soldier = 2;
        m.ShowSoldierStats();
        okt &= GmQueue().find("4201000 1000014 d 0 Other One; ") == 0;
        // No profile.
        m.m_soldier = 1;
        m.ShowSoldierStats();
        std::string none;
        for (uint32_t row = 1; row <= 7; ++row)
            none += CmText("%x 1000092 0 0 ; %x 1000015 b 7d2 ; ", 0x04500000 | (3 * row - 1) << 12, 0x04500000 | (3 * row) << 12);
        okt &= GmQueue() == none + "8100000 1000015 d 7d2 ; ";
        WM(r.esStats, okt);

        // The toggle: the stats (shown, the panel hidden, the button's text, the inventory's text and
        // the soldier types hidden), then the panel (refreshed, shown) again.
        bool okg = true;
        g_equipmentStatsShown = false;
        m.m_soldier = 1;
        g_gfLog.clear();
        okg &= C_EquipmentMenu::CallStatsToggle(&m, nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) && g_equipmentStatsShown &&
               GmQueue() == none + "8100000 1000015 d 7d2 ; 4500000 1000003 0 0 ; 4100000 1000004 0 0 ; "
                                   "4600000 1000015 c aaf ; 4700000 1000004 0 0 ; 4800000 1000004 0 0 ; ";
        // (The panel refreshed first: its places' pictures.)
        m.m_panel.m_squadSoldier = &a;
        okg &= m.OnStatsToggle(nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) && !g_equipmentStatsShown;
        const std::string back = GmQueue();
        okg &= back.find("4100000 1000003 0 0 ; 4500000 1000004 0 0 ; 4600000 1000015 c ab0 ; "
                         "4700000 1000003 0 0 ; 4800000 1000003 0 0 ; ") != std::string::npos &&
               back.find("4502000") == std::string::npos && back.find(" 1000039 ") < back.find("4100000 1000003");
        m.m_panel.m_squadSoldier = nullptr;
        WM(r.esToggle, okg);

        // The pickers: the selection screen's (its selector onto the picture, its own depth kept; the
        // soldier kept), the squad list's (its selector; the soldier shown: the panel's, the stats).
        bool okp = true;
        I3D_frame *selector = EsAt("essel", 0.0f, 0.0f, 0.5f), *list = EsAt("eslist", 0.0f, 0.0f, -0.25f);
        m.m_selector = selector;
        m.m_soldierSelector = list;
        C_GmIndexed picture(0x08101000, 2);
        picture.m_frame = EsAt("espic", 0.125f, -0.375f, 2.0f);
        g_gfLog.clear();
        okp &= C_EquipmentMenu::CallSelectionPicked(&m, nullptr, &picture, CONTROL_NOTIFY_CLICK, 0) && m.m_selected == 2 &&
               m.m_soldier == 1 && GfLogIs({"essel 8 (0.125 -0.375 0.5)", "index 100000a"});
        m.m_selected = 0;
        g_gfLog.clear();
        okp &= C_EquipmentMenu::OnSoldierPicked(&m, nullptr, &picture, CONTROL_NOTIFY_CLICK, 0) && m.m_soldier == 2 &&
               m.m_selected == 0 && m.m_panel.m_soldierIndex == 2 && m.m_panel.m_squadSoldier == &c &&
               GmInOrder(Join(g_gfLog), {"eslist 8 (0.125 -0.375 -0.25)", "index 100000a"});
        const std::string picked = GmQueue();
        okp &= picked.find("4201000 1000014 d 0 Other One; ") != std::string::npos &&
               picked.find(" 1000039 ") < picked.find("4201000 1000014");
        WM(r.esPicked, okp);
        m.m_panel.m_squadSoldier = nullptr;
        squad.states.clear();
        g_epProfiles.clear();
        p0.fullName = p0.customName = p1.fullName = C_str();
    }

    // --- (part 9f-ii) the messages: escape (the selection screen closed, else the question), continue
    // (the selection screen for the first campaign's missions 2 and 15 with more than one soldier;
    // else all deployed, the checks), the selection's accept (the pick alone), the checks (the
    // required items, the weights, the weapons: their messages), done; others nothing ---
    {
        C_EquipmentMenu m;
        bool ok = true;
        C_MessageBox box(scene, GfFrame(FRAME_DUMMY, "esbox"));
        m.m_messageBox = &box;
        S_SquadSoldier a, b;
        squad.states = {&a, &b};
        C_SlDef gun(ITEM_ANIMATED, 2, -1, -1), cap(ITEM_STATIC, 0, -1, -1);
        g_gfItemDefs = {{4, &gun}, {5, &cap}};
        a.hands = EpMake(1, 4, 1);
        b.longGun = EpMake(2, 4, 1);
        const auto message = [&](uint32_t id) {
            S_ControlMessage msg;
            msg.id = id;
            g_controlMessages.clear();
            g_gfLog.clear();
            box.m_kind = MESSAGE_BOX_NONE;
            box.m_yesMessage = box.m_noMessage = 0x1234;
            m.OnMessage(&msg);
        };
        const auto deployed = [&]() { return CmText("%d%d", a.deployed, b.deployed); };
        // Escape: no screen, nothing; the items screen: the question; the selection screen: closed.
        g_menuScreens.clear();
        message(0x200000A);
        ok &= g_controlMessages.empty() && box.m_kind == MESSAGE_BOX_NONE;
        C_MsScreen items(scene, "esitems", 0x04000000), selection(scene, "essel", 0x08000000), other(scene, "eso", 0x0C000000);
        g_menuScreens = {&selection, &items};
        message(0x200000A | 0x4000);
        ok &= box.m_kind == MESSAGE_BOX_YES_NO && box.m_yesMessage == 0x2000007 && box.m_noMessage == 0 &&
              GmQueue().find("1000015 18 7d4 ; ") != std::string::npos;
        m.CloseMessage();
        g_menuScreens = {&other};
        message(0x200000A);
        ok &= box.m_kind == MESSAGE_BOX_YES_NO;
        m.CloseMessage();
        g_menuScreens = {&items, &selection};
        message(0x200000A);
        ok &= box.m_kind == MESSAGE_BOX_NONE && GmQueue() == "0 6001005 0 0 ; ";
        g_menuScreens.clear();
        // Continue.
        C_Campaign *campaign = g_gameProfile.m_campaign;
        for (int mission : {2, 15}) {
            g_esMission = mission;
            message(0x200000B);
            ok &= GmQueue() == "0 2000002 2 0 ; " && deployed() == "00" && MsLogIs({"mission number 0"}, g_gfLog);
        }
        for (int mission : {1, 3, 14, 16}) {
            g_esMission = mission;
            a.deployed = b.deployed = false;
            message(0x200000B);
            ok &= GmQueue() == "0 20000c1 0 0 ; " && deployed() == "11";
        }
        g_esMission = 2;
        campaign->SetCurrentIndex(1);
        a.deployed = b.deployed = false;
        message(0x200000B);
        ok &= GmQueue() == "0 20000c1 0 0 ; " && deployed() == "11" && MsLogIs({"mission number 1"}, g_gfLog);
        campaign->SetCurrentIndex(0);
        squad.states = {&a};
        a.deployed = false;
        message(0x200000B);
        ok &= GmQueue() == "0 20000c1 0 0 ; " && a.deployed;
        squad.states = {&a, &b};
        // The selection's accept.
        a.deployed = b.deployed = true;
        m.m_selected = 1;
        message(0x200006A);
        ok &= GmQueue() == "0 20000c1 0 0 ; " && deployed() == "01";
        m.m_selected = 0;
        message(0x200006A);
        ok &= GmQueue() == "0 20000c1 0 0 ; " && deployed() == "10";
        for (int pick : {2, -1}) {
            m.m_selected = pick;
            a.deployed = b.deployed = true;
            message(0x200006A);
            ok &= g_controlMessages.empty() && deployed() == "00";
        }
        // The checks: all well, done; overloaded; unarmed (asked).
        a.deployed = b.deployed = true;
        g_esMission = 0;
        m.m_missionItems.m_records[0]->required.clear();
        message(0x20000C1);
        ok &= GmQueue() == "0 20000c6 0 0 ; " && box.m_kind == MESSAGE_BOX_NONE;
        g_epWeightFills[&b.load] = 1.0f;
        message(0x20000C1);
        ok &= GmQueue() == "0 20000c6 0 0 ; ";
        g_epWeightFills[&b.load] = 1.001f;
        message(0x20000C1);
        ok &= box.m_kind == MESSAGE_BOX_OK && box.m_yesMessage == 0 && box.m_noMessage == 0 &&
              GmQueue().find("1000015 18 7e7 ; ") != std::string::npos && GmQueue().find("20000c6") == std::string::npos;
        m.CloseMessage();
        // (A soldier not deployed counts all the same.)
        b.deployed = false;
        message(0x20000C1);
        ok &= box.m_kind == MESSAGE_BOX_OK;
        m.CloseMessage();
        g_epWeightFills.clear();
        b.deployed = true;
        b.longGun = EpMake(2, 5, 1);
        message(0x20000C1);
        ok &= box.m_kind == MESSAGE_BOX_YES_NO && box.m_yesMessage == 0x20000C6 && box.m_noMessage == 0 &&
              GmQueue().find("1000015 18 2531 ; ") != std::string::npos;
        m.CloseMessage();
        // (Overloaded asked first.)
        g_epWeightFills[&a.load] = 2.0f;
        message(0x20000C1);
        ok &= box.m_kind == MESSAGE_BOX_OK;
        m.CloseMessage();
        g_epWeightFills.clear();
        // (A required item missing: its message, the rest not checked.)
        m.m_missionItems.m_records[0]->required = {{5, 1}};
        a.longGun = EpMake(-1, -1, 0);
        b.longGun = EpMake(-1, -1, 0);
        b.hands = EpMake(3, 4, 1);
        const bool oldIds = g_textIds;
        g_textIds = true;
        cap.m_textId = 0x77;
        g_epWeightFills[&a.load] = 2.0f;
        message(0x20000C1);
        const std::string q = GmQueue();
        ok &= box.m_kind == MESSAGE_BOX_OK && box.m_yesMessage == 0 && box.m_noMessage == 0 &&
              q.find("text 2052 'text 119'") != std::string::npos && q.find("20000c6") == std::string::npos;
        m.CloseMessage();
        g_epWeightFills.clear();
        g_textIds = oldIds;
        m.m_missionItems.m_records[0]->required.clear();
        // Done: no soldier to equip, the next step, the second stock reset.
        a.autoEquip = b.autoEquip = true;
        message(0x20000C6);
        g_gmResetStock = nullptr;
        message(0x20000C6);
        ok &= GmQueue() == "0 2000008 0 0 ; " && MsLogIs({"stock reset"}, g_gfLog) && g_gmResetStock == &stock1C;
        a.autoEquip = b.autoEquip = true;
        message(0x20000C6);
        ok &= !a.autoEquip && !b.autoEquip;
        for (uint32_t id : {0x2000001u, 0x2000007u, 0x20000C0u, 0x20000C2u, 0x2000069u, 0x20000C5u}) {
            message(id);
            ok &= g_controlMessages.empty() && g_gfLog.empty() && box.m_kind == MESSAGE_BOX_NONE;
        }
        m.m_messageBox = nullptr;
        WM(r.esMessage, ok);

        // The weights and the weapons alone.
        bool okc = true;
        okc &= C_EquipmentMenu::IsSquadWithinWeight();
        g_epWeightFills[&a.load] = 1.5f;
        okc &= !C_EquipmentMenu::IsSquadWithinWeight();
        g_epWeightFills[&a.load] = std::nanf("");
        okc &= C_EquipmentMenu::IsSquadWithinWeight();
        g_epWeightFills.clear();
        squad.states.clear();
        okc &= C_EquipmentMenu::IsSquadWithinWeight() && C_EquipmentMenu::IsSquadArmed();
        squad.states = {&a, &b};
        // The weapon's place: the hands, the shoulder, the belt, the backpack; the other items not.
        a.hands = EpMake(-1, -1, 0);
        a.deployed = b.deployed = true;
        b.hands = EpMake(1, 4, 1);
        okc &= !C_EquipmentMenu::IsSquadArmed();
        a.deployed = false;
        okc &= C_EquipmentMenu::IsSquadArmed();
        a.deployed = true;
        for (int place = 0; place < 4; ++place) {
            a.hands = a.longGun = EpMake(-1, -1, 0);
            a.belt.clear();
            a.backpackContents.clear();
            const S_SoldierItem weapon = EpMake(1, 4, 1), other = EpMake(2, 5, 1);
            if (place == 0)
                a.hands = weapon;
            if (place == 1)
                a.longGun = weapon;
            if (place == 2)
                a.belt = {other, weapon, other};
            if (place == 3)
                a.backpackContents = {weapon, other};
            okc &= C_EquipmentMenu::IsSquadArmed();
            a.hands = a.longGun = other;
            a.belt = {other};
            a.backpackContents = {other};
            okc &= !C_EquipmentMenu::IsSquadArmed();
        }
        a.hands = a.longGun = EpMake(-1, -1, 0);
        a.belt.clear();
        a.backpackContents.clear();
        WM(r.esChecks, okc);

        // The items carried: a place one, the belt's and the backpack's their counts; the deployed
        // only; fewer than asked, 0; all, or a soldier's own at most the surplus; an axis item with its
        // first carrier only.
        bool okn = true;
        C_SlDef axis(ITEM_STATIC, 0, -1, -1);
        axis.m_team = 1;
        C_SlDef allied(ITEM_STATIC, 0, -1, -1);
        g_gfItemDefs = {{6, &allied}, {7, &axis}, {9, &allied}};
        a.head = EpMake(1, 6, 5);
        a.belt = {EpMake(2, 6, 3), EpMake(3, 9, 4)};
        b.uniform = EpMake(4, 6, 2);
        b.backpackContents = {EpMake(5, 6, 4)};
        b.hands = EpMake(-1, -1, 0);
        g_requiredCarrier = -1;
        // (a: 1 + 3, b: 1 + 4.)
        okn &= C_EquipmentMenu::CountCarried({6, 9}, -1) == 9 && C_EquipmentMenu::CountCarried({6, 10}, -1) == 0;
        okn &= C_EquipmentMenu::CountCarried({6, 0}, -1) == 9 && C_EquipmentMenu::CountCarried({9, 4}, -1) == 4;
        // (Own: a's present counts 5 + 3; surplus 9 - 2.)
        okn &= C_EquipmentMenu::CountCarried({6, 2}, 0) == 7 && C_EquipmentMenu::CountCarried({6, 7}, 0) == 2 &&
               C_EquipmentMenu::CountCarried({6, 2}, 1) == 6 && C_EquipmentMenu::CountCarried({6, 9}, 1) == 0 &&
               C_EquipmentMenu::CountCarried({6, 2}, 5) == 0;
        b.deployed = false;
        okn &= C_EquipmentMenu::CountCarried({6, 1}, -1) == 4 && C_EquipmentMenu::CountCarried({6, 1}, 1) == 0;
        b.deployed = true;
        // (Every place: the head, the uniform, the backpack, the long gun, the hands.)
        S_SquadSoldier p;
        p.deployed = true;
        p.head = p.uniform = p.backpack = p.longGun = p.hands = EpMake(1, 6, 3);
        squad.states = {&p};
        okn &= C_EquipmentMenu::CountCarried({6, 1}, -1) == 5 && C_EquipmentMenu::CountCarried({6, 1}, 0) == 4;
        squad.states = {&a, &b};
        // The axis item: the first carrier kept (b's then, after it a's not counted).
        a.head = EpMake(1, 7, 1);
        b.uniform = EpMake(4, 7, 1);
        b.backpackContents = {EpMake(5, 7, 2)};
        g_requiredCarrier = 1;
        okn &= C_EquipmentMenu::CountCarried({7, 1}, -1) == 3 && g_requiredCarrier == 1;
        g_requiredCarrier = -1;
        okn &= C_EquipmentMenu::CountCarried({7, 1}, -1) == 1 && g_requiredCarrier == 0;
        okn &= C_EquipmentMenu::CountCarried({7, 1}, -1) == 1;
        g_requiredCarrier = 0;
        a.deployed = false;
        okn &= C_EquipmentMenu::CountCarried({7, 1}, -1) == 0;
        a.deployed = true;
        g_requiredCarrier = -1;
        a.head = EpMake(-1, -1, 0);
        a.belt.clear();
        b.uniform = EpMake(-1, -1, 0);
        b.backpackContents.clear();
        WM(r.esCarried, okn);

        // The required items: the record's (none: nothing required); a uniform or the diving gear at
        // most one per soldier (the record changed), the carrier reset first.
        bool okr = true;
        C_SlDef uniform(ITEM_STATIC, 15, -1, -1), diving(ITEM_STATIC, 0, -1, -1), plain(ITEM_STATIC, 0, -1, -1);
        g_gfItemDefs = {{8, &uniform}, {231, &diving}, {10, &plain}, {7, &axis}};
        S_MissionItems *record = m.m_missionItems.m_records[1];
        record->required = {{8, 5}, {231, 3}, {10, 3}};
        a.uniform = EpMake(1, 8, 1);
        b.uniform = EpMake(2, 8, 1);
        a.belt = {EpMake(3, 231, 1), EpMake(4, 10, 3)};
        b.backpack = EpMake(5, 231, 1);
        g_esMission = 1;
        m.m_messageBox = &box;
        box.m_kind = MESSAGE_BOX_NONE;
        g_requiredCarrier = 5;
        okr &= m.HasRequiredItems() && box.m_kind == MESSAGE_BOX_NONE &&
               record->required == std::vector<std::pair<int, int>>({{8, 2}, {231, 2}, {10, 3}}) && g_requiredCarrier == -1;
        record->required = {{10, 4}};
        okr &= !m.HasRequiredItems() && box.m_kind == MESSAGE_BOX_OK && record->required[0].second == 4;
        m.CloseMessage();
        // (The cap only for those: another's count kept; the first one missing stops.)
        record->required = {{10, 1}, {7, 1}, {10, 9}};
        box.m_kind = MESSAGE_BOX_NONE;
        okr &= !m.HasRequiredItems() && box.m_kind == MESSAGE_BOX_OK;
        m.CloseMessage();
        // (An axis item reset before: carried by the second soldier.)
        b.belt = {EpMake(6, 7, 1)};
        g_requiredCarrier = 0;
        record->required = {{7, 1}};
        okr &= m.HasRequiredItems();
        b.belt.clear();
        g_esMission = 5;
        okr &= m.HasRequiredItems();
        g_esMission = -1;
        okr &= m.HasRequiredItems();
        g_esMission = 200;
        okr &= m.HasRequiredItems();
        // (The second campaign's records after the first's thirty.)
        m.m_missionItems.m_campaign = 1;
        m.m_missionItems.m_records[31] = record;
        record->required = {{10, 4}};
        g_esMission = 1;
        box.m_kind = MESSAGE_BOX_NONE;
        okr &= !m.HasRequiredItems() && box.m_kind == MESSAGE_BOX_OK;
        m.CloseMessage();
        // (Past the records: none, the campaign's field not read as one.)
        g_esMission = 90;
        okr &= m.HasRequiredItems();
        m.m_missionItems.m_records[31] = nullptr;
        m.m_missionItems.m_campaign = 0;
        record->required.clear();
        m.m_messageBox = nullptr;
        a.uniform = b.uniform = b.backpack = EpMake(-1, -1, 0);
        a.belt.clear();
        squad.states.clear();
        g_esMission = 0;
        WM(r.esRequired, okr);
    }

    g_gfItemDefs = oldDefs;
    g_gameProfile.m_playMode = oldPlayMode;
    g_gameProfile.m_itemStock18 = old18;
    g_gameProfile.m_itemStock1C = old1C;
    g_gameProfile.m_missionSquad = oldSquad;
    g_gameProfile.m_campaign = oldCampaign;
    g_lpMissions.clear();
    g_files = oldFiles;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("equipmentscreen: esNew %d esStart %d esStop %d esStore %d esTake %d esClicked %d esDragStart %d "
                "esDragMove %d esDrop %d esHover %d esStats %d esToggle %d esPicked %d esMessage %d esChecks %d "
                "esCarried %d esRequired %d\n",
                r.esNew, r.esStart, r.esStop, r.esStore, r.esTake, r.esClicked, r.esDragStart, r.esDragMove, r.esDrop,
                r.esHover, r.esStats, r.esToggle, r.esPicked, r.esMessage, r.esChecks,
                r.esCarried, r.esRequired);
}
