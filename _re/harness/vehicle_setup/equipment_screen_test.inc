// Part 9f's equipment screen (game/equipment_screen.cpp), in the factory build, run by ControlsTest
// after OptionsTest: the slots not ported yet stubbed (parts 9f-i to -iv), the fakes game_menu_test.inc's and
// its helpers (the queue's text, the logs joined, the controls answering their index). Prints
// "equipmentscreen:" with 1 per check met.
#define private public
#define protected public
#include "game/equipment_screen.h"
#undef protected
#undef private
#include <functional>

// (Not ported yet: logged.)
// (Part 9f-ii: the campaign's mission number; campaign.cpp is not linked.)
int g_esMission;
int C_Campaign::GetMissionNumber(int campaign)
{
    GfLog("mission number %d", campaign);
    return g_esMission;
}
// (Part 9f-iii: the roster's file, logged.)
bool g_esRosterLoads = true;
bool C_HumanStore::Load(C_str fileName)
{
    GfLog("roster load %s", fileName.c_str());
    return g_esRosterLoads;
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
    bool esItemsScreen = true, esSelection = true, esStatsPanel = true, esStore2 = true, esSquadList = true, esSquad = true, esEquipmentPanel = true, esBuild = true;
    bool esPut = true, esReturn = true, esTypePicked = true, esSubstitute = true, esMatch = true;
    bool esCountOwn = true, esAssign = true, esReplenish = true, esDistribute = true, esAutoEquip = true;
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
        g_gfItemDefs = oldDefs;
    }

    // --- (part 9f-iii) the screens' descriptions: each builder's controls (dumped) and frames (logged) ---
    {
        void *const oldFind = g_mrModelVt[0x48 / 4], *const oldSetPos = g_msFrameVt[0x08 / 4];
        g_mrModelVt[0x48 / 4] = reinterpret_cast<void *>(&MsFind);
        g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        void *const oldCreate = g_driverVtable[0x54 / 4];
        g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
        void (*const oldFill)(I3D_model *) = g_cacheFill;
        g_cacheFill = [](I3D_model *) {};
        // A frame logging its own mesh made ("<name> a0", "<name>.mesh 18").
        void **const ownVt = new void *[MR_SLOTS];
        std::copy(g_msFrameVt, g_msFrameVt + MR_SLOTS, ownVt);
        ownVt[0xA0 / 4] = reinterpret_cast<void *>(&MrFake<0xA0>);
        const auto visual = [&](const char *name) {
            S_MrObj *v = MsFrame(name);
            v->vtable = ownVt;
            S_MrObj *mesh = MrObj((std::string(name) + ".mesh").c_str(), g_mrModelVt);
            mesh->vtable = new void *[MR_SLOTS];
            std::copy(g_mrModelVt, g_mrModelVt + MR_SLOTS, mesh->vtable);
            mesh->vtable[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
            *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(v) + 0x1E0) = mesh;
            return v;
        };
        const auto msg = [](uint32_t id, uint32_t value, uintptr_t value2 = 0, uintptr_t value3 = 0) {
            return CmText(" %x/%x/%x/%x", id, value, static_cast<uint32_t>(value2), static_cast<uint32_t>(value3));
        };
        std::function<std::string(const S_ControlDesc *)> dump = [&](const S_ControlDesc *c) {
            std::string s = CmText("[%s %x %x:", c->name.c_str(), c->type, c->id);
            for (const S_ControlMessage &m : c->messages)
                s += msg(m.id, m.value, m.value2, m.value3) + (m.target ? CmText("@%x", m.target) : std::string());
            for (const S_ControlDesc *k : c->children)
                s += dump(k);
            return s + "]";
        };
        const auto dumpAll = [&](const S_ScreenDesc &d) {
            std::string s;
            for (const S_ControlDesc *c : d.controls)
                s += dump(c);
            return s;
        };
        const auto fn = [](auto f) { return reinterpret_cast<uintptr_t>(f); };
        const uint32_t D = 1u << 26;

        // The items screen's controls (the equipment kind; the other kind's texts; no description).
        bool okt = true;
        {
            C_EquipmentMenu m;
            m.m_mode = 3;
            S_LoadoutPreset preset;
            preset.flags = 5;
            preset.textId = 0x1234;
            S_LoadoutPreset other;
            other.flags = 4;
            m.m_presets.m_presets.push_back(&preset);
            m.m_presets.m_presets.push_back(&other);
            S_MrObj *model = MsFrame("esim"), *essentials = MsFrame("esime");
            g_msChildren[{model, "description"}] = MsFrame("esimd");
            g_msChildren[{model, "text_mission essentials"}] = essentials;
            S_ScreenDesc d("Items selection", "items selection", D);
            g_gfLog.clear();
            okt &= m.BuildItemsScreen(&d, As<I3D_frame>(model));
            const uintptr_t self = reinterpret_cast<uintptr_t>(&m);
            const auto c = [&](const char *name, int type, int index) { return CmText("[%s %x %x:", name, type, D | index << 20); };
            const std::string want =
                c("bassign to all", 0x14, 1) + msg(0x1000015, 0xD, 0xAB0) + msg(0x1000007, 0x4001000, self, fn(&C_EquipmentMenu::CallStatsToggle)) + "]" +
                c("text_inventory", 0x13, 2) + msg(0x1000015, 0xB, 0xAA8) + "]" +
                c("soldier type", 0x11, 3) + msg(0x1000007, 0x400E000, self, fn(&C_EquipmentMenuBase::CallItemNotify60)) +
                msg(0x1000081, 0xC, 500) + msg(0x1000017, 0x13, 500) + msg(0x1000017, 0x13, 0x1234) + "]" +
                c("text_mission essentials", 0x12, 4) + "]" + c("text_screen", 0x13, 5) + msg(0x1000015, 0xA, 0xA8D) + "]" +
                c("text_military storages", 0x13, 6) + msg(0x1000015, 0xB, 0xA93) + "]" +
                c("text_team", 0x13, 7) + msg(0x1000015, 0xB, 0xA92) + "]" +
                c("baccept and continue", 0x14, 8) + msg(0x1000015, 0xD, 0xA90) + msg(0x1000007, 0x4001000, self, fn(&C_EquipmentMenu::CallAutoEquip)) + "]" +
                c("bauto equip", 0x14, 9) + msg(0x1000015, 0xD, 0xA8F) + msg(0x600100B, 0) + "]" +
                c("bback", 0x14, 10) + msg(0x1000015, 0xD, 0xA91) + msg(0x600100A, 0) + "]" +
                c("text_h&dversion", 0x13, 11) + msg(0x1000015, 0x28, 0x7D6) + "]" +
                c("text_assortment", 0x13, 12) + msg(0x1000015, 0xB, 0xAA6) + "]" +
                c("description", 0x15, 13) + msg(0x1000015, 0xB, 0x7D2) + "]";
            okt &= dumpAll(d) == want && C_GmBasePeek::WeaponsInfo(m) == static_cast<int>(D | 13 << 20) &&
                   MsLogIs({"esim 48 description 1", "esim 48 text_mission essentials 1", "esime on 0"}, g_gfLog);
            // The other kind; no description, no essentials.
            m.m_mode = 4;
            C_GmBasePeek::SetWeaponsInfo(m, 0);
            g_msChildren.erase({model, "description"});
            g_msChildren.erase({model, "text_mission essentials"});
            S_ScreenDesc d2("Items selection", "items selection", D);
            okt &= m.BuildItemsScreen(&d2, As<I3D_frame>(model)) && d2.controls.size() == 12 && C_GmBasePeek::WeaponsInfo(m) == 0;
            const std::string got = dumpAll(d2);
            okt &= got.find("[text_screen 13 4500000: 1000015/a/a8e/0]") != std::string::npos &&
                   got.find("[baccept and continue 14 4800000: 1000015/d/ab1/0") != std::string::npos;
            m.m_presets.m_presets.clear();
        }
        WM(r.esItemsScreen, okt);

        // The selection screen: its title, a picture per squad soldier (own meshes; past the squad's off,
        // missing ones skipped), yes and cancel; the selector.
        bool oks = true;
        {
            C_EquipmentMenu m;
            S_MrObj *model = MsFrame("essm"), *selector = MsFrame("esss");
            S_MrObj *p3 = visual("esp3"), *p4 = visual("esp4"), *p5 = visual("esp5");
            g_msChildren[{model, "photo03"}] = p3;
            g_msChildren[{model, "photo04"}] = p4;
            g_msChildren[{model, "photo05"}] = p5;
            g_msChildren[{model, "selector"}] = selector;
            alignas(16) static uint8_t profiles[2][sizeof(S_SoldierProfile)];
            std::memset(profiles, 0, sizeof profiles);
            auto *f0 = reinterpret_cast<S_SoldierProfile *>(profiles[0]), *f1 = reinterpret_cast<S_SoldierProfile *>(profiles[1]);
            f0->faceImage = reinterpret_cast<IImage *>(0x1110);
            f1->faceImage = reinterpret_cast<IImage *>(0x2220);
            g_epProfiles = {f1, f0};
            S_SquadSoldier a, b;
            a.profileIndex = 1;
            b.profileIndex = 0;
            squad.states = {&a, &b};
            S_ScreenDesc d("SELECTION SCREEN", "selection screen", 2u << 26);
            g_gfLog.clear();
            oks &= m.BuildSelectionScreen(&d, As<I3D_frame>(model)) && m.m_selector == As<I3D_frame>(selector);
            const uintptr_t self = reinterpret_cast<uintptr_t>(&m);
            const uint32_t S = 2u << 26;
            const auto c = [&](const char *name, int type, int index) { return CmText("[%s %x %x:", name, type, S | index << 20); };
            const std::string want =
                c("text_select one soldier", 0x13, 1) + msg(0x1000015, 0xC, 0xAB2) + "]" +
                c("photo03", 4, 2) + msg(0x1000009, 0) + msg(0x1000038, 0, 0x1110) + msg(0x1000007, 0x4001000, self, fn(&C_EquipmentMenu::CallSelectionPicked)) + "]" +
                c("photo04", 4, 3) + msg(0x1000009, 1) + msg(0x1000038, 0, 0x2220) + msg(0x1000007, 0x4001000, self, fn(&C_EquipmentMenu::CallSelectionPicked)) + "]" +
                c("button01", 0x14, 4) + msg(0x1000015, 0xD, 0x7D1) + msg(0x600106A, 0) + msg(0x6001005, 0) + "]" +
                c("button00", 0x14, 5) + msg(0x1000015, 0xD, 0x7D8) + msg(0x6001005, 0) + "]";
            oks &= dumpAll(d) == want &&
                   MsLogIs({"essm 48 photo03 1", "esp3 on 1", "esp3 a0", "esp3.mesh 18", "essm 48 photo04 1", "esp4 on 1", "esp4 a0",
                            "esp4.mesh 18", "essm 48 photo05 1", "esp5 on 0", "essm 48 photo06 1", "essm 48 selector 1"},
                           g_gfLog);
            squad.states.clear();
            g_epProfiles.clear();
        }
        WM(r.esSelection, oks);

        // The stats panel: hidden, taking no input; per skill its name (0xA6E + the skill), bar, value;
        // "Soldier stats :".
        bool okp = true;
        {
            S_ScreenDesc d("Items selection", "items selection", D);
            okp &= C_EquipmentMenu::BuildStatsPanel(&d, nullptr) && d.controls.size() == 1;
            const char *const rows[][3] = {{"text_shooting", "hlcpct07", "text_shooting01"},
                                           {"text_strenght", "hlcpct08", "text_strenght01"},
                                           {"text_health", "hlcpct09", "text_health01"},
                                           {"text_endurance", "hlcpct03", "text_endurance01"},
                                           {"text_stealth", "hlcpct04", "text_stealth01"},
                                           {"text_first aid", "hlcpct05", "text_first aid01"},
                                           {"text_lockpick", "hlcpct06", "text_lockpick01"}};
            std::string want = CmText("[Panel02 5 %x:", D | 1 << 20) + msg(0x1000005, 0) + msg(0x1000004, 0);
            for (uint32_t i = 0; i < 7; ++i) {
                want += CmText("[%s 13 %x:", rows[i][0], D | 1 << 20 | (3 * i + 1) << 12) + msg(0x2000073, 0) + msg(0x1000015, 0xB, 0xA6E + i) + "]";
                want += CmText("[%s 16 %x:", rows[i][1], D | 1 << 20 | (3 * i + 2) << 12) + msg(0x1000093, 2) + msg(0x1000092, 0) + "]";
                want += CmText("[%s 13 %x:", rows[i][2], D | 1 << 20 | (3 * i + 3) << 12) + msg(0x2000073, 0) + msg(0x1000015, 0xB, 0x7D2) + "]";
            }
            want += CmText("[text_soldier stats 13 %x:", D | 1 << 20 | 22 << 12) + msg(0x1000015, 0xB, 0xAB3) + "]]";
            okp &= dumpAll(d) == want;
        }
        WM(r.esStatsPanel, okp);

        // The store: the selector off, its dummy over the filter and the scroll bar, the template with
        // its count and information texts; the list's description (the equipment screen's), its pages
        // emptied, the stock. A frame missing: false.
        bool okr = true;
        {
            S_MrObj *model = MsFrame("estm"), *sel = MsFrame("estsel"), *filter = MsFrame("estf"), *scroll = MsFrame("ests");
            S_MrObj *entry = MsFrame("este"), *count = MsFrame("estc"), *info = MsFrame("esti"), *hl = MsFrame("esthl");
            for (const auto &child : std::vector<std::pair<const char *, S_MrObj *>>{
                     {"itemselector01", sel}, {"filter", filter}, {"scroll01", scroll}, {"item08", entry}, {"item09", count},
                     {"text_info00", info}, {"itemselector", hl}})
                g_msChildren[{model, child.first}] = child.second;
            C_EquipmentMenu m;
            m.m_mode = 3;
            playMode.singleMission = 0;
            S_MrObj *panel = MsFrame("estp");
            g_mrMade[FRAME_DUMMY] = {panel};
            S_ScreenDesc d("Items selection", "items selection", D);
            g_gfLog.clear();
            g_controlMessages.clear();
            okr &= m.BuildStore(&d, As<I3D_frame>(model));
            std::vector<std::string> want = {"estm 48 itemselector01 1", "estsel on 0", "create 6", "estm 48 filter 1",
                                             "estm 48 scroll01 1", "estp 38 estm 0", "estp 8 (0 0 0)", "estp 34 P_visualstore",
                                             "estf 38 estp 1", "ests 38 estp 1", "estm 48 item08 1", "estm 48 item09 1",
                                             "estm 48 text_info00 1", "este 34 vi_template", "esti 38 este 1", "estc 38 este 1",
                                             "estc 34 ammo", "esti on 0", "estc on 0"};
            for (int page = 0; page < 6; ++page)
                for (const char *line : {"create 6", "estm 48 P_visualstore 20", "estm 48 selitem08 1", "estm 48 selitem08 1",
                                         "estm 48 vi_template 1"})
                    want.push_back(line);
            for (const char *line : {"estm 48 itemselector 1", "esthl 38 null 1", "items loaded"})
                want.push_back(line);
            okr &= MsLogIs(want, g_gfLog) && m.m_models == std::vector<I3D_frame *>({As<I3D_frame>(panel)}) &&
                   m.m_storeList.m_menu == &m && m.m_storeList.m_stock == &stock18 && m.m_storeList.m_highlight == As<I3D_frame>(hl) &&
                   d.controls.size() == 1;
            // (The equipment screen's description: the tabs active, its scroll bar.)
            const std::string desc = dumpAll(d);
            okr &= desc.find("[P_visualstore 5 4100000:") == 0 && desc.find("1000001/") == std::string::npos &&
                   desc.find("[scroll01 8 ") != std::string::npos && desc.find("bscroll") == std::string::npos;
            // (The pages emptied: their lists hidden.)
            okr &= GmQueue().find("4103000 2000085") != std::string::npos;
            // A frame missing: false (the selector looked up first).
            for (const char *missing : {"filter", "scroll01", "item08", "item09", "text_info00"}) {
                S_MrObj *const kept = g_msChildren[{model, missing}];
                g_msChildren.erase({model, missing});
                C_EquipmentMenu m2;
                g_mrMade[FRAME_DUMMY] = {MsFrame("estp2")};
                S_ScreenDesc d2("Items selection", "items selection", D);
                g_gfLog.clear();
                okr &= !m2.BuildStore(&d2, As<I3D_frame>(model)) && d2.controls.empty() && !m2.m_storeList.m_menu &&
                       !MrHas("items loaded") && !MrHas("estc 34 ammo");
                g_msChildren[{model, missing}] = kept;
            }
            C_EquipmentMenu m3;
            g_mrMade[FRAME_DUMMY].clear();
            S_ScreenDesc d3("Items selection", "items selection", D);
            okr &= !m3.BuildStore(&d3, As<I3D_frame>(model)) && m3.m_models.empty();
            g_controlMessages.clear();
        }
        WM(r.esStore2, okr);

        // The squad's list: the tables and pictures off, the group's dummy with the template; the name's
        // text, the list; the dead out of the squad; a picture per soldier (the template's copy across);
        // the selector; the first soldier shown.
        bool okl = true;
        {
            S_MrObj *model = MsFrame("eslm"), *photo = MsFrame("eslp"), *selector = MsFrame("esls");
            g_msChildren[{model, "photo03"}] = photo;
            g_msChildren[{model, "table13"}] = MsFrame("eslt13");
            g_msChildren[{model, "selector"}] = selector;
            As<I3D_frame>(photo)->m_localMatrix.m[3][0] = 0.25f;
            As<I3D_frame>(photo)->m_localMatrix.m[3][1] = -0.5f;
            As<I3D_frame>(photo)->m_localMatrix.m[3][2] = 1.5f;
            alignas(16) static uint8_t profiles[3][sizeof(S_SoldierProfile)];
            std::memset(profiles, 0, sizeof profiles);
            auto *f0 = reinterpret_cast<S_SoldierProfile *>(profiles[0]), *f1 = reinterpret_cast<S_SoldierProfile *>(profiles[1]),
                 *f2 = reinterpret_cast<S_SoldierProfile *>(profiles[2]);
            f0->faceImage = reinterpret_cast<IImage *>(0x100);
            f1->dead = true;
            f2->faceImage = reinterpret_cast<IImage *>(0x300);
            g_epProfiles = {f0, f1, f2};
            auto *a = new S_SquadSoldier(), *b = new S_SquadSoldier(), *c = new S_SquadSoldier();
            a->profileIndex = 2;
            b->profileIndex = 1;
            c->profileIndex = 0;
            auto *dead = new S_SquadSoldier();
            dead->profileIndex = 1;
            squad.states = {dead, a, b, c};
            S_MrObj *list = MsFrame("esll");
            g_mrMade[FRAME_DUMMY] = {list};
            S_MrObj *c0 = visual("eslc0"), *c1 = visual("eslc1");
            g_mrClones = {As<I3D_frame>(c0), As<I3D_frame>(c1)};
            C_EquipmentMenu m;
            m.m_soldier = 3;
            S_ScreenDesc d("Items selection", "items selection", D);
            g_gfLog.clear();
            g_controlMessages.clear();
            okl &= m.BuildSquadList(&d, As<I3D_frame>(model));
            okl &= GmQueue().find(" 1000039 ") != std::string::npos;
            std::vector<std::string> want;
            for (const char *name : {"table11", "table13", "table14", "table15", "photo03", "photo04", "photo05", "photo06", "photo07"}) {
                want.push_back(CmText("eslm 48 %s 1", name));
                if (name == std::string("table13"))
                    want.push_back("eslt13 on 0");
                if (name == std::string("photo03"))
                    want.push_back("eslp on 0");
            }
            for (const char *line : {"create 6", "eslm 48 photo03 1", "esll 38 eslm 0", "esll 8 (0 0 0)", "esll 34 GB_Soldiers",
                                     "eslp 38 esll 1", "eslp on 1", "esll on 1", "eslp 24", "esll 24"})
                want.push_back(line);
            for (int i = 0; i < 2; ++i) {
                const std::string n = CmText("eslc%d", i);
                for (const std::string &line : {std::string("clone tree eslp 0"), n + " a0", n + ".mesh 18", n + CmText(" 34 Soldier%02d", i),
                                                n + " 38 esll 0", n + " on 1", n + " 24"})
                    want.push_back(line);
            }
            for (const char *line : {"eslp on 0", "eslp 24", "eslm 48 selector 1"})
                want.push_back(line);
            okl &= g_gfLog.size() >= want.size() &&
                   MsLogIs(want, std::vector<std::string>(g_gfLog.begin(), g_gfLog.begin() + static_cast<long>(want.size())));
            okl &= squad.states == std::vector<S_SquadSoldier *>({a, c}) && m.m_soldierSelector == As<I3D_frame>(selector) &&
                   m.m_soldier == 0 && m.m_panel.m_soldierIndex == 0 && m.m_panel.m_squadSoldier == a &&
                   m.m_frames == std::vector<I3D_frame *>({As<I3D_frame>(c0), As<I3D_frame>(c1)}) &&
                   m.m_models == std::vector<I3D_frame *>({As<I3D_frame>(list)});
            // (The copies across from the template: 0.106 apart.)
            const auto pos = [](S_MrObj *o) { return As<I3D_frame>(o)->m_localMatrix.m[3]; };
            okl &= pos(c0)[0] == 0.25f && pos(c0)[1] == -0.5f && pos(c0)[2] == 1.5f && pos(c1)[0] == 1.0f * 0.106f + 0.25f &&
                   pos(c1)[1] == -0.5f && pos(c1)[2] == 1.5f;
            const uintptr_t self = reinterpret_cast<uintptr_t>(&m);
            const uint32_t G = D | 2 << 20;
            const std::string wantDesc =
                CmText("[name- capacity 13 %x:", D | 1 << 20) + msg(0x1000015, 0xB, 0x7D2) + "]" + CmText("[GB_Soldiers b %x:", G) +
                msg(0x1000028, 4) + msg(0x1000029, 0, GmBits(0.106f)) + msg(0x200002C, 0) +
                CmText("[Soldier00 4 %x:", G | 1 << 12) + msg(0x1000009, 0) + msg(0x1000038, 0, 0x300) +
                msg(0x1000007, 0x4001000, self, fn(&C_EquipmentMenu::OnSoldierPicked)) + "]" + CmText("[Soldier01 4 %x:", G | 2 << 12) +
                msg(0x1000009, 1) + msg(0x1000038, 0, 0x100) + msg(0x1000007, 0x4001000, self, fn(&C_EquipmentMenu::OnSoldierPicked)) + "]]";
            okl &= dumpAll(d) == wantDesc;
            for (S_SquadSoldier *s : squad.states)
                delete s;
            squad.states.clear();
            m.m_panel.m_squadSoldier = nullptr;
            m.m_frames.clear();
            m.m_models.clear();
            // No template, or no dummy made: false (nothing described).
            C_EquipmentMenu n;
            g_msChildren.erase({model, "photo03"});
            g_mrMade[FRAME_DUMMY] = {MsFrame("esll2")};
            S_ScreenDesc d2("Items selection", "items selection", D);
            okl &= !n.BuildSquadList(&d2, As<I3D_frame>(model)) && d2.controls.empty();
            g_msChildren[{model, "photo03"}] = photo;
            g_mrMade[FRAME_DUMMY].clear();
            okl &= !n.BuildSquadList(&d2, As<I3D_frame>(model)) && d2.controls.empty() && n.m_models.empty();
            g_epProfiles.clear();
            g_mrClones.clear();
        }
        WM(r.esSquadList, okl);

        // The roster (loaded when empty) and a new squad (four soldiers, the first profiles).
        bool okq = true;
        {
            g_gfLog.clear();
            C_EquipmentMenu::EnsureSquad();
            okq &= MsLogIs({"roster load tables\\HumanStore.def", "add soldier", "set profile 0 0", "add soldier",
                            "set profile 1 0", "add soldier", "set profile 2 0", "add soldier", "set profile 3 0"},
                           g_gfLog) &&
                   squad.states.size() == 4;
            g_gfLog.clear();
            C_EquipmentMenu::EnsureSquad();
            okq &= g_gfLog.empty() && squad.states.size() == 4;
            squad.states.clear();
            alignas(16) static uint8_t profile[sizeof(S_SoldierProfile)];
            g_epProfiles = {reinterpret_cast<S_SoldierProfile *>(profile)};
            g_gfLog.clear();
            C_EquipmentMenu::EnsureSquad();
            okq &= !MrHas("roster load tables\\HumanStore.def") && MrHas("set profile 3 0");
            squad.states.clear();
            g_epProfiles.clear();
        }
        WM(r.esSquad, okq);

        // The equipment panel: the in-game menu's (its frames, as its test's; the panel's description with
        // the menu); not found, or a dummy not made: false.
        bool oke = true;
        {
            S_MrObj *em = MsFrame("esem"), *panel = MsFrame("eseq");
            C_EquipmentMenu m0;
            S_ScreenDesc d0("Items selection", "items selection", D);
            g_mrMade[FRAME_DUMMY] = {MsFrame("eseh0"), MsFrame("eseb0")};
            g_gfLog.clear();
            oke &= !m0.BuildEquipmentPanel(&d0, As<I3D_frame>(em)) && d0.controls.empty() && m0.m_models.empty() &&
                   GfLogIs({"esem 48 equippanel 20", "create 6", "create 6"});
            g_msChildren[{em, "equippanel"}] = panel;
            for (int made = 0; made < 2; ++made) {
                C_EquipmentMenu m3;
                S_ScreenDesc d3("Items selection", "items selection", D);
                g_mrMade[FRAME_DUMMY] = {MsFrame("eseh3")};
                if (made == 0)
                    g_mrMade[FRAME_DUMMY].clear();
                g_gfLog.clear();
                oke &= !m3.BuildEquipmentPanel(&d3, As<I3D_frame>(em)) && m3.m_models.empty() && d3.controls.empty() &&
                       !MrHas("eseq 34 P_Equipment");
            }
            const auto own = [&](const char *name, const char *frame) {
                S_MrObj *v = MrObj(frame, g_mrModelVt);
                S_MrObj *mesh = MrObj("esemesh", g_mrModelVt);
                mesh->vtable = new void *[MR_SLOTS];
                std::copy(g_mrModelVt, g_mrModelVt + MR_SLOTS, mesh->vtable);
                mesh->vtable[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
                *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(v) + 0x1E0) = mesh;
                g_msChildren[{panel, name}] = v;
            };
            // (No "drop", no "item02".)
            for (const char *name : {"back", "head", "hands", "uniform", "shoulder", "item00", "item01", "item03", "item04",
                                     "item05", "item06", "item07"})
                own(name, (std::string("esev_") + name).c_str());
            g_msChildren[{panel, "scroll13"}] = MsFrame("eses13");
            g_msChildren[{panel, "text_hlstrscap"}] = MsFrame("eset13");
            g_msChildren[{panel, "hlcpct00"}] = MsFrame("eseh00");
            // (No "scroll14": its texts unlinked.)
            g_msChildren[{panel, "text_bkpckcap"}] = MsFrame("eset14");
            g_msChildren[{panel, "hlcpct01"}] = MsFrame("eseh01");
            C_EquipmentMenu m;
            S_MrObj *holsters = MsFrame("eseh"), *backpack = MsFrame("eseb");
            g_mrMade[FRAME_DUMMY] = {holsters, backpack};
            S_ScreenDesc d("Items selection", "items selection", D);
            g_gfLog.clear();
            oke &= m.BuildEquipmentPanel(&d, As<I3D_frame>(em));
            std::vector<std::string> want = {"esem 48 equippanel 20", "create 6", "create 6", "eseq 34 P_Equipment",
                                             "eseh 38 eseq 0", "eseh 8 (0 0 0)", "eseh 34 GB_Holsters", "eseb 38 eseq 0",
                                             "eseb 8 (0 0 0)", "eseb 34 GR_BackPack"};
            const auto visual = [&](const char *name, const char *parent = nullptr) {
                want.push_back(CmText("eseq 48 %s 1", name));
                if (std::string(name) == "drop" || std::string(name) == "item02")
                    return;
                want.push_back(CmText("esev_%s a0", name));
                want.push_back("esemesh 18");
                if (parent)
                    want.push_back(CmText("esev_%s 38 %s 1", name, parent));
            };
            for (const char *name : {"back", "head", "drop", "hands", "uniform", "shoulder"})
                visual(name);
            for (const char *line : {"eseq 48 scroll13 1", "eses13 38 eseq 1", "eseq 48 text_hlstrscap 1",
                                     "eset13 38 eses13 1", "eseq 48 hlcpct00 1", "eseh00 38 eses13 1"})
                want.push_back(line);
            for (const char *name : {"item00", "item01", "item02", "item03"})
                visual(name, "eseh");
            for (const char *line : {"eseq 48 scroll14 1", "eseq 48 text_bkpckcap 1", "eset14 38 null 1",
                                     "eseq 48 hlcpct01 1", "eseh01 38 null 1"})
                want.push_back(line);
            for (const char *name : {"item04", "item05", "item06", "item07"})
                visual(name, "eseb");
            oke &= g_gfLog.size() > want.size() &&
                   MsLogIs(want, std::vector<std::string>(g_gfLog.begin(), g_gfLog.begin() + static_cast<long>(want.size())));
            oke &= m.m_models == std::vector<I3D_frame *>({As<I3D_frame>(panel), As<I3D_frame>(holsters), As<I3D_frame>(backpack)}) &&
                   m.m_panel.m_menu == &m && !d.controls.empty() && d.controls[0]->name == C_str("P_Equipment");
            g_mrMade[FRAME_DUMMY].clear();
        }
        WM(r.esEquipmentPanel, oke);

        // The screens built: the two models (the items screen's builders, the soldiers to equip equipped
        // on their own; the selection screen's), the screens made, the first opened, the stats; the
        // popup (none loaded here).
        bool okb = true;
        {
            C_EquipmentMenu m;
            S_MrWorld world = MrWorld("esb");
            m.m_scene = As<I3D_scene>(world.scene);
            m.m_mode = 3;
            C_MsScreen box(m.m_scene, "esbbox", 0);
            m.m_screens = {&box};
            S_MrObj *mi = MrObj("esbmi", g_mrModelVt), *ms = MrObj("esbms", g_mrModelVt);
            g_msChildren[{mi, "itemselector01"}] = MsFrame("esbsel");
            g_msChildren[{mi, "Panel02"}] = MsFrame("esbp02");
            S_MrObj *pop = MrObj("esbpop", g_mrModelVt);
            g_mrMade[FRAME_MODEL] = {mi, ms, pop};
            // (The popup's model: its frames none.)
            alignas(16) static uint8_t profile[4][sizeof(S_SoldierProfile)];
            std::memset(profile, 0, sizeof profile);
            g_epProfiles = {reinterpret_cast<S_SoldierProfile *>(profile[0]), reinterpret_cast<S_SoldierProfile *>(profile[1]),
                            reinterpret_cast<S_SoldierProfile *>(profile[2]), reinterpret_cast<S_SoldierProfile *>(profile[3])};
            squad.states.clear();
            g_menuScreens.clear();
            g_gfLog.clear();
            g_controlMessages.clear();
            okb &= m.BuildScreens() && MrHas("esbmi 34 Items selection") && MrHas("esbms 34 SELECTION SCREEN") &&
                   GmInOrder(Join(g_gfLog), {"esbmi 48 equippanel 20", "esbmi 48 table11 1", "esbmi 48 itemselector01 1",
                                             "esbmi 48 description 1", "mission number 0", "esbms 48 photo03 1", "esbms 48 selector 1"}) &&
                   MrHas("add soldier") && squad.states.size() == 4 && m.m_screens.size() == 3 && m.m_screens[1]->GetId() == 1u << 26 &&
                   m.m_screens[2]->GetId() == 2u << 26 && m.m_screens[1]->GetScreenFrame() == As<I3D_frame>(mi) &&
                   !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1] && m.m_popup &&
                   m.m_popup->GetScreenFrame() == As<I3D_frame>(pop) && MrHas("esbpop 48 text 1");
            // (The items screen's controls: the stats panel among them.)
            okb &= static_cast<C_ControlGroup *>(m.m_screens[1])->m_children.size() == 1;
            delete m.m_popup;
            m.m_popup = nullptr;
            okb &= GmQueue().find("4201000 1000014") != std::string::npos;
            for (size_t i = 1; i < m.m_screens.size(); ++i)
                delete m.m_screens[i];
            m.m_screens.clear();
            g_menuScreens.clear();
            g_mrMade.erase(FRAME_MODEL);
            squad.states.clear();
            g_epProfiles.clear();
            m.m_panel.m_squadSoldier = nullptr;
        }
        WM(r.esBuild, okb);

        g_cacheFill = oldFill;
        g_driverVtable[0x54 / 4] = oldCreate;
        g_mrModelVt[0x48 / 4] = oldFind;
        g_msFrameVt[0x08 / 4] = oldSetPos;
        g_mrMade.erase(FRAME_DUMMY);
    }

    // --- (part 9f-iv) the soldier type picked: the store's items put on the soldier, his unrequired
    // items back, the presets matched with their substitutes (the in-game menu's tests, the axis
    // filter's) ---
    {
        C_EquipmentMenu m;
        C_ItemStock stock;
        m.m_storeList.SetStock(&stock);
        m.m_storeList.m_entries.resize(500);
        // (The store's entries of the types: TakeItem answers whether one was updated.)
        for (uint32_t t = 0; t < 20; ++t)
            m.m_storeList.m_entries[t] = 0x04401000 + (t << 12);
        S_SquadSoldier sq;
        sq.deployed = true;
        squad.states = {&sq};
        m.m_panel.m_squadSoldier = &sq;
        std::vector<std::unique_ptr<C_SlDef>> defs;
        const auto def = [&](int type, E_ItemRecordType record, int behaviour, int ammo = -1, int id = -1) {
            defs.push_back(std::make_unique<C_SlDef>(record, behaviour, ammo, -1));
            defs.back()->m_id = id < 0 ? type : id;
            g_gfItemDefs[type] = defs.back().get();
            g_epAllowed[defs.back().get()] = 0xFF;
            return defs.back().get();
        };
        g_gfItemDefs.clear();
        g_slCounts.clear();
        g_epDisplaced.clear();
        g_epRemoved.clear();
        def(3, ITEM_STATIC, 0);
        def(4, ITEM_ANIMATED, 2, 8);
        def(5, ITEM_ANIMATED, 6);
        def(6, ITEM_STATIC, 15);
        def(7, ITEM_STATIC, 0);
        def(8, ITEM_AMMO, 0);
        def(9, ITEM_STATIC, 0);
        def(11, ITEM_ANIMATED, 3, 12);
        def(12, ITEM_AMMO, 0);

        // Put and dropped: not a type, not in the store (unless filled): false; put (what is left back:
        // of another type true, its own false).
        bool oku = true;
        g_gfLog.clear();
        oku &= !m.PutStoreItem(ITEM_FITS_HEAD, -1, 1, false) && g_gfLog.empty();
        oku &= !m.PutStoreItem(ITEM_FITS_HEAD, 3, 1, false) && GfLogIs({"stock take 3 1"});
        g_gfLog.clear();
        // (Filled: the lacking added first; the fake stock keeps its count.)
        oku &= !m.PutStoreItem(ITEM_FITS_HEAD, 3, 1, true) && GfLogIs({"stock add 3 1", "stock take 3 1"});
        g_slCounts = {{3, 1}};
        g_gfLog.clear();
        oku &= m.PutStoreItem(ITEM_FITS_HEAD, 3, 1, true) && GmInOrder(Join(g_gfLog), {"stock take 3 1", "put 1 -1 3 1"}) &&
               !MrHas("stock add 3 1");
        sq.head = EpMake(-1, -1, 0);
        g_slCounts = {{9, 3}};
        g_epDisplaced = {EpMake(5, 7, 1)};
        g_gfLog.clear();
        oku &= m.PutStoreItem(ITEM_FITS_BELT, 9, 2, false) && GmInOrder(Join(g_gfLog), {"stock take 9 2", "put 20 -1 9 2", "stock add 7 1"});
        g_epDisplaced = {EpMake(5, 9, 1)};
        g_gfLog.clear();
        oku &= !m.PutStoreItem(ITEM_FITS_BELT, 9, 1, false) && MrHas("stock add 9 1");
        g_epDisplaced = {EpMake(5, 9, 0)};
        g_slCounts = {{9, 3}};
        oku &= m.PutStoreItem(ITEM_FITS_BELT, 9, 1, false);
        // (Dropped where it goes: the panel's drop.)
        g_slCounts = {{9, 3}};
        g_epDisplaced.clear();
        g_gfLog.clear();
        oku &= m.DropStoreItem(9, 2, false) && GmInOrder(Join(g_gfLog), {"stock take 9 2", "put "});
        g_slCounts.clear();
        g_gfLog.clear();
        oku &= !m.DropStoreItem(9, 2, false) && GfLogIs({"stock take 9 2"});
        g_gfLog.clear();
        oku &= !m.DropStoreItem(9, 2, true) && GfLogIs({"stock add 9 2", "stock take 9 2"});
        g_epDisplaced = {EpMake(5, 7, 1)};
        g_slCounts = {{9, 3}};
        g_gfLog.clear();
        oku &= m.DropStoreItem(9, 1, false) && MrHas("stock add 7 1");
        g_epDisplaced = {EpMake(5, 9, 1)};
        g_slCounts = {{9, 3}};
        oku &= !m.DropStoreItem(9, 1, false);
        g_epDisplaced.clear();
        // (Type 0 an item; a drop's fill forces it past the free weight.)
        def(0, ITEM_STATIC, 0);
        g_slCounts = {{0, 1}};
        g_gfLog.clear();
        oku &= m.PutStoreItem(ITEM_FITS_BELT, 0, 1, false) && MrHas("stock take 0 1");
        defs.back()->m_weight = 50.0f;
        g_epFreeWeight = 10.0f;
        g_slCounts = {{0, 3}};
        g_gfLog.clear();
        m.DropStoreItem(0, 1, false);
        oku &= !MrHas("put 1 -1 0 1") && !MrHas("put 20 -1 0 1");
        g_slCounts = {{0, 3}};
        g_gfLog.clear();
        m.DropStoreItem(0, 1, true);
        oku &= MrHas("put 1 -1 0 1") || MrHas("put 20 -1 0 1");
        g_epFreeWeight = 100.0f;
        sq.head = sq.uniform = sq.hands = sq.longGun = sq.backpack = EpMake(-1, -1, 0);
        sq.belt.clear();
        sq.backpackContents.clear();
        WM(r.esPut, oku);

        // The unrequired items back (the head's with the flag; the required ones' surplus of the shown
        // soldier, none: kept).
        bool okr = true;
        S_MissionItems *record = m.m_missionItems.m_records[0];
        g_esMission = 0;
        record->required = {{8, 1}, {9, 5}};
        sq.head = EpMake(1, 3, 1);
        sq.longGun = EpMake(2, 5, 1);
        sq.hands = EpMake(3, 4, 1);
        sq.belt = {EpMake(4, 8, 2), EpMake(5, 9, 1)};
        sq.backpackContents = {EpMake(6, 9, 1), EpMake(7, 7, 1)};
        sq.backpack = EpMake(8, 7, 1);
        m.m_soldier = 0;
        g_gfLog.clear();
        m.ReturnUnrequired(0, false);
        // (The belt's first given back twice: the fake leaves it there, its next not looked at.)
        okr &= MsLogIs({"mission number 0", "remove 10 2 5 1", "stock add 5 1", "remove 4 3 4 1", "stock add 4 1", "remove 20 4 8 1",
                        "stock add 8 1", "remove 20 4 8 1", "stock add 8 1", "remove 40 7 7 1", "stock add 7 1", "remove 8 8 7 1", "stock add 7 1"},
                       std::vector<std::string>(MsLogWithout("stock take")));
        g_gfLog.clear();
        m.ReturnUnrequired(0, true);
        okr &= g_gfLog.size() > 1 && g_gfLog[1] == "remove 1 1 3 1";
        // (None of a place: nothing; the shown soldier's surplus, not the soldier's.)
        sq.head = sq.longGun = sq.hands = sq.backpack = EpMake(-1, -1, 0);
        sq.belt = {EpMake(4, 8, 2)};
        sq.backpackContents.clear();
        S_SquadSoldier other;
        other.deployed = true;
        squad.states = {&sq, &other};
        m.m_soldier = 1;
        g_gfLog.clear();
        m.ReturnUnrequired(0, true);
        okr &= MsLogIs({"mission number 0"}, g_gfLog) && m.m_panel.m_squadSoldier == &sq;
        // (No soldier: nothing.)
        squad.states.clear();
        g_gfLog.clear();
        m.ReturnUnrequired(0, true);
        okr &= MsLogIs({"mission number 0"}, g_gfLog);
        squad.states = {&sq};
        m.m_soldier = 0;
        // (The soldier selected first: the panel's; type 0 given back.)
        squad.states = {&other, &sq};
        m.m_soldier = 1;
        m.m_panel.SelectSoldier(0);
        sq.head = EpMake(9, 0, 1);
        g_gfLog.clear();
        m.ReturnUnrequired(1, true);
        okr &= m.m_panel.m_squadSoldier == &sq && MrHas("remove 1 9 0 1");
        sq.head = EpMake(-1, -1, 0);
        squad.states = {&sq};
        m.m_soldier = 0;
        // (No record: all back.)
        g_esMission = 7;
        sq.belt = {EpMake(4, 8, 2)};
        g_gfLog.clear();
        m.ReturnUnrequired(0, true);
        okr &= MrHas("remove 20 4 8 2");
        g_esMission = 0;
        record->required.clear();
        WM(r.esReturn, okr);

        // The soldier type picked: none, nothing; the header: his items back, false; a line without a
        // preset: the panel and the store; a preset: its match's items onto him (not the uniform).
        bool okt = true;
        const auto pick = [&](uint32_t value) {
            C_GmPicked control(value);
            g_gfLog.clear();
            return m.OnItemNotify60(nullptr, &control, CONTROL_NOTIFY_0E, 0);
        };
        g_gfLog.clear();
        okt &= m.OnItemNotify60(nullptr, nullptr, 0, 0) && g_gfLog.empty();
        sq.belt = {EpMake(4, 9, 1)};
        sq.head = EpMake(2, 3, 1);
        squad.states = {&other, &sq};
        m.m_soldier = 1;
        m.m_panel.SelectSoldier(0);
        g_controlMessages.clear();
        okt &= !pick(0) && MrHas("remove 20 4 9 1") && MrHas("stock add 9 1") && MrHas("remove 1 2 3 1") &&
               m.m_panel.m_squadSoldier == &sq;
        std::string q = GmQueue();
        okt &= q.find(" 1000039 ") != std::string::npos && q.rfind(" 100007c ") != std::string::npos && q.rfind(" 100007c ") > q.rfind(" 1000039 ");
        sq.head = EpMake(-1, -1, 0);
        m.m_panel.SelectSoldier(0);
        okt &= pick(1) && m.m_panel.m_squadSoldier == &sq;
        q = GmQueue();
        okt &= q.find(" 1000039 ") != std::string::npos && q.rfind(" 100007c ") != std::string::npos && q.rfind(" 100007c ") > q.rfind(" 1000039 ");
        squad.states = {&sq};
        m.m_soldier = 0;
        okt &= !pick(static_cast<uint32_t>(-1)) && g_gfLog.empty();
        sq.belt.clear();
        okt &= pick(1) && !MrHas("stock take 3 1");
        S_LoadoutPreset preset;
        preset.flags = 5;
        preset.head = 3;
        preset.hands = 4;
        preset.longGun = 5;
        preset.uniform = 6;
        preset.backpack = 7;
        preset.beltCount = 1;
        preset.belt[0] = {8, 2};
        preset.backpackCount = 1;
        preset.backpackItems[0] = {9, 1};
        S_LoadoutPreset user;
        user.flags = 4;
        m.m_presets.m_presets = {&user, &preset};
        g_slCounts = {{3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 5}, {9, 5}};
        sq.backpackContents = {EpMake(7, 9, 1)};
        m.m_panel.SelectSoldier(-1);
        okt &= pick(1) && MrHas("remove 40 7 9 1") && m.m_panel.m_squadSoldier == &sq;
        sq.backpackContents.clear();
        sq.head = sq.uniform = sq.hands = sq.longGun = sq.backpack = EpMake(-1, -1, 0);
        sq.belt.clear();
        g_slCounts = {{3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 5}, {9, 5}};
        okt &= pick(1) && GmInOrder(Join(g_gfLog), {"put 1 -1 3 1", "put 4 -1 4 1", "put 10 -1 5 1", "put 8 -1 7 1",
                                                    "put 20 -1 8 2", "put 40 -1 9 1"}) &&
               !MrHas("stock take 6 1");
        // (Another hands' weapon of another kind: not taken, nor its ammunition; the message.)
        C_MessageBox box(scene, GfFrame(FRAME_DUMMY, "esbox4"));
        m.m_messageBox = &box;
        box.m_kind = MESSAGE_BOX_NONE;
        preset.belt[0] = {12, 2};
        g_slCounts = {{11, 1}, {12, 5}};
        sq.head = sq.longGun = sq.hands = sq.backpack = EpMake(-1, -1, 0);
        sq.belt.clear();
        sq.backpackContents.clear();
        g_controlMessages.clear();
        okt &= pick(1) && !MrHas("put 4 -1 11 1") && !MrHas("put 20 -1 12 2") && box.m_kind == MESSAGE_BOX_OK &&
               box.m_yesMessage == 0 && GmQueue().find("1000015 18 2532 ; ") != std::string::npos;
        m.CloseMessage();
        // (Of the same kind: taken.)
        def(11, ITEM_ANIMATED, 2, 12);
        g_slCounts = {{11, 5}, {12, 5}};
        box.m_kind = MESSAGE_BOX_NONE;
        okt &= pick(1) && MrHas("put 4 -1 11 1") && MrHas("put 20 -1 12 2") && box.m_kind == MESSAGE_BOX_NONE;
        // (Of the same kind, a scope's reticle on one: not taken.)
        C_ScopeComp scope;
        scope.m_aimMode = 2;
        def(13, ITEM_ANIMATED, 4, 12);
        def(14, ITEM_ANIMATED, 4, 12)->m_secondaryComponent = &scope;
        preset.hands = 13;
        g_slCounts = {{14, 5}, {12, 5}};
        sq.head = sq.hands = sq.longGun = sq.backpack = EpMake(-1, -1, 0);
        okt &= pick(1) && !MrHas("put 4 -1 14 1");
        g_gfItemDefs[13]->m_secondaryComponent = &scope;
        sq.head = sq.hands = sq.longGun = sq.backpack = EpMake(-1, -1, 0);
        g_slCounts = {{14, 5}, {12, 5}};
        okt &= pick(1) && MrHas("put 4 -1 14 1");
        g_gfItemDefs[13]->m_secondaryComponent = nullptr;
        g_gfItemDefs[14]->m_secondaryComponent = nullptr;
        preset.hands = 4;
        // (A preset without hands: none put, its ammunition all the same.)
        preset.hands = -1;
        g_slCounts = {{12, 5}};
        okt &= pick(1) && MrHas("put 20 -1 12 2");
        m.m_messageBox = nullptr;
        m.m_presets.m_presets.clear();
        WM(r.esTypePicked, okt);
        m.m_panel.m_squadSoldier = nullptr;
        squad.states.clear();
        m.m_storeList.m_stock = nullptr;
        g_slCounts.clear();
        g_gfItemDefs = oldDefs;
        for (const auto &d : defs)
            g_epAllowed.erase(d.get());
    }

    // --- (part 9f-iv, the in-game menu's test) the substitutes: the stock's best by score (the type itself, the same
    // behaviour, scoped reticles, paired ids, the small behaviours' table, a weapon's ammunition);
    // none for ammunition; the type itself best: false. The preset matched: its places swapped (a
    // weapon's ammunition counted, then swapped in the belt and backpack), its items merged ---
    {
        C_EquipmentMenu m;
        C_ItemStock stock;
        m.m_storeList.SetStock(&stock);
        std::vector<std::unique_ptr<C_SlDef>> defs;
        const std::map<int, C_ItemDef *> keptDefs = g_gfItemDefs;
        g_gfItemDefs.clear();
        // A type: its record, behaviour, ammunition, id (its type unless given).
        const auto def = [&](int type, E_ItemRecordType record, int behaviour, int ammo = -1, int id = -1) {
            defs.push_back(std::make_unique<C_SlDef>(record, behaviour, ammo, -1));
            defs.back()->m_id = id < 0 ? type : id;
            g_gfItemDefs[type] = defs.back().get();
            return defs.back().get();
        };
        C_ScopeComp reticle, ironSights;
        reticle.m_aimMode = 2;
        ironSights.m_aimMode = 5;
        bool axisOnly = false;
        const auto best = [&](int type, int ammo) {
            int t = type;
            const bool found = m.FindSubstitute(t, ammo, axisOnly);
            return CmText("%d %d", found, t);
        };
        bool oks = true;
        def(1, ITEM_AMMO, 0);
        g_slCounts = {{1, 1}, {2, 1}};
        def(2, ITEM_STATIC, 0);
        oks &= best(1, 0) == "0 1";
        // The type itself (in the stock) best: false; not there, another of its behaviour.
        def(3, ITEM_STATIC, 0);
        g_slCounts = {{2, 1}, {3, 1}};
        oks &= best(3, 0) == "0 3";
        g_slCounts = {{2, 1}};
        oks &= best(3, 0) == "1 2";
        // None: -1, false.
        g_slCounts = {};
        oks &= best(3, 0) == "0 -1";
        // Behaviour 0x13 never matched; scoped reticles +50 over the same behaviour; paired ids +60.
        def(4, ITEM_STATIC, 0x13);
        def(5, ITEM_STATIC, 0x13);
        g_slCounts = {{5, 1}};
        oks &= best(4, 0) == "0 -1";
        C_SlDef *r10 = def(10, ITEM_ANIMATED, 4), *r11 = def(11, ITEM_ANIMATED, 4), *r12 = def(12, ITEM_ANIMATED, 4);
        r10->m_secondaryComponent = &reticle;
        r12->m_secondaryComponent = &reticle;
        r11->m_secondaryComponent = &ironSights;
        g_slCounts = {{11, 1}, {12, 1}};
        oks &= best(10, 0) == "1 12";
        r12->m_secondaryComponent = &ironSights;
        oks &= best(10, 0) == "1 11";
        // (Paired: the ids 7, 0x16, 0x104.)
        def(20, ITEM_ANIMATED, 2, -1, 7);
        def(21, ITEM_ANIMATED, 2, -1, 0x16);
        def(22, ITEM_ANIMATED, 2, -1, 99);
        g_slCounts = {{22, 1}, {21, 1}};
        oks &= best(20, 0) == "1 21";
        def(23, ITEM_ANIMATED, 2, -1, 0x104);
        g_slCounts = {{22, 1}, {23, 1}};
        oks &= best(20, 0) == "1 23";
        // The small behaviours: by the candidate's (3 and 4: 60 over 5: 50 over 2: 30 over 1: 10).
        def(30, ITEM_ANIMATED, 6);
        def(31, ITEM_ANIMATED, 1);
        def(32, ITEM_ANIMATED, 2);
        def(33, ITEM_ANIMATED, 5);
        def(34, ITEM_ANIMATED, 3);
        def(35, ITEM_ANIMATED, 4);
        def(36, ITEM_ANIMATED, 12);
        def(37, ITEM_ANIMATED, 8);
        g_slCounts = {{31, 1}};
        oks &= best(30, 0) == "1 31";
        g_slCounts = {{31, 1}, {32, 1}};
        oks &= best(30, 0) == "1 32";
        g_slCounts = {{31, 1}, {32, 1}, {33, 1}};
        oks &= best(30, 0) == "1 33";
        g_slCounts = {{33, 1}, {34, 1}};
        oks &= best(30, 0) == "1 34";
        g_slCounts = {{33, 1}, {35, 1}};
        oks &= best(30, 0) == "1 35";
        g_slCounts = {{36, 1}, {32, 1}};
        oks &= best(30, 0) == "1 36";
        // (Not small: nothing; a 12 asking: its 5 +61 more; a small paired with a small +110.)
        g_slCounts = {{37, 1}};
        oks &= best(30, 0) == "0 -1";
        def(38, ITEM_ANIMATED, 12);
        g_slCounts = {{33, 1}, {34, 1}};
        oks &= best(38, 0) == "1 33";
        def(39, ITEM_ANIMATED, 6, -1, 7);
        def(40, ITEM_ANIMATED, 1, -1, 0x104);
        g_slCounts = {{40, 1}, {34, 1}};
        oks &= best(39, 0) == "1 40";
        // (Exact: a paired candidate of the same behaviour 160 against a later rival's 160 (small 60,
        // paired 110, an ammunition short -10); a 12's 5 (111) against an earlier rival's 110.)
        def(490, ITEM_ANIMATED, 2, -1, 7);
        def(491, ITEM_ANIMATED, 2, -1, 0x16);
        def(492, ITEM_ANIMATED, 3, 493, 0x104);
        def(493, ITEM_AMMO, 0);
        g_slCounts = {{491, 1}, {492, 1}, {493, 2}};
        oks &= best(490, 3) == "1 491";
        def(494, ITEM_ANIMATED, 12, -1, 7);
        def(495, ITEM_ANIMATED, 1, 497, 0x16);
        def(496, ITEM_ANIMATED, 5, -1, 99);
        def(497, ITEM_AMMO, 0);
        g_slCounts = {{495, 1}, {496, 1}, {497, 2}};
        oks &= best(494, 3) == "1 496";
        // (The axis' only: another team's best not taken.)
        g_slCounts = {{33, 1}, {34, 1}};
        axisOnly = true;
        defs.back()->m_team = 1;
        oks &= best(30, 0) == "0 -1";
        g_gfItemDefs[33]->m_team = 1;
        oks &= best(30, 0) == "1 33";
        g_gfItemDefs[33]->m_team = 0;
        defs.back()->m_team = 0;
        axisOnly = false;
        WM(r.esSubstitute, oks);
        // A weapon's ammunition: enough +30, short 10 per one, none -100 more.
        def(50, ITEM_ANIMATED, 4, 60);
        def(51, ITEM_ANIMATED, 4, 61);
        def(52, ITEM_ANIMATED, 4, 62);
        def(60, ITEM_AMMO, 0);
        def(61, ITEM_AMMO, 0);
        g_slCounts = {{51, 1}, {52, 1}, {61, 5}};
        oks &= best(50, 4) == "1 51";
        g_slCounts = {{51, 1}, {52, 1}, {61, 2}};
        oks &= best(50, 4) == "1 51";
        g_slCounts = {{51, 1}, {52, 1}, {61, 0}, {62, 4}};
        oks &= best(50, 4) == "1 52";
        // (Neither's ammunition there: both below 0, none.)
        g_slCounts = {{51, 1}, {52, 1}};
        oks &= best(50, 4) == "0 -1";
        // (No ammunition asked: the first of the best.)
        g_slCounts = {{51, 1}, {52, 1}, {61, 0}};
        oks &= best(50, 0) == "1 51";
        // (Above 12 no ammunition counted.)
        def(53, ITEM_ANIMATED, 13, 61);
        def(54, ITEM_ANIMATED, 13, 62);
        g_slCounts = {{54, 1}, {61, 5}};
        oks &= best(53, 4) == "1 54";
        WM(r.esSubstitute, oks);
        // The scores exactly, by ties: the rival's (the lower type) wins a tie, the candidate's
        // (the higher) a point more. Types 100.. per case (the type asked, the rival, the candidate,
        // their ammunition).
        {
            // A case: the asked type's behaviour (paired: id 7), the rival and the candidate (their
            // behaviours, paired (id 0x16), scoped with reticles (aim 2), ammunition have / asked).
            struct S_Side {
                int behaviour;
                bool paired, scoped;
                int ammoHave;                   // -1: no ammunition
            };
            int base = 100;
            const auto tie = [&](int behaviour, bool paired, bool scoped, S_Side rival, S_Side candidate, int asked) {
                const int t = base, r = base + 1, c = base + 2, ra = base + 3, ca = base + 4;
                base += 10;
                def(t, ITEM_ANIMATED, behaviour, -1, paired ? 7 : t);
                if (scoped)
                    g_gfItemDefs[t]->m_secondaryComponent = &reticle;
                g_slCounts.clear();
                const auto side = [&](int type, int ammoType, const S_Side &s) {
                    C_SlDef *d = def(type, ITEM_ANIMATED, s.behaviour, s.ammoHave >= 0 ? ammoType : -1, s.paired ? 0x16 : type);
                    if (s.scoped)
                        d->m_secondaryComponent = &reticle;
                    g_slCounts[type] = 1;
                    if (s.ammoHave >= 0) {
                        def(ammoType, ITEM_AMMO, 0);
                        g_slCounts[ammoType] = s.ammoHave;
                    }
                };
                side(r, ra, rival);
                side(c, ca, candidate);
                return best(t, asked) == CmText("1 %d", r);
            };
            bool okt = true;
            // Same behaviour +100: the rival a paired small one (10 + 110, 2 short: 100).
            okt &= tie(6, true, false, {1, true, false, 1}, {6, false, false, -1}, 3);
            // Paired small +110: a paired 1 (10 + 110 - 20) against the same behaviour (100).
            okt &= tie(6, true, false, {6, false, false, -1}, {1, true, false, 1}, 3);
            // Scoped reticles +50 (150): a paired 3 (60 + 110 - 20).
            okt &= tie(4, true, true, {3, true, false, 1}, {4, false, true, -1}, 3);
            // Paired same +60 (160): a paired 3, 1 short.
            okt &= tie(4, true, false, {3, true, false, 2}, {4, true, false, -1}, 3);
            // The small table by the candidate's: 2 +30, 3 +60, 4 +60, 5 +50, 12 +50, 1 +10 (the rival
            // the same behaviour, short to match).
            okt &= tie(6, false, false, {6, false, false, 1}, {2, false, false, -1}, 8);
            okt &= tie(6, false, false, {6, false, false, 1}, {3, false, false, -1}, 5);
            okt &= tie(6, false, false, {6, false, false, 1}, {4, false, false, -1}, 5);
            okt &= tie(6, false, false, {6, false, false, 1}, {5, false, false, -1}, 6);
            okt &= tie(6, false, false, {6, false, false, 1}, {12, false, false, -1}, 6);
            okt &= tie(6, false, false, {6, false, false, 1}, {1, false, false, -1}, 10);
            // The ammunition enough +30 (130): a paired 3, 4 short.
            okt &= tie(4, true, false, {3, true, false, 1}, {4, false, false, 5}, 5);
            WM(r.esSubstitute, okt);
            // A 12 asking: its 5 +61 more (111), the type itself (100 + 51, 4 short) the rival.
            {
                const int t = base, x = base + 2, a = base + 3;
                base += 10;
                def(t, ITEM_ANIMATED, 12, a);
                def(a, ITEM_AMMO, 0);
                def(x, ITEM_ANIMATED, 5);
                g_slCounts = {{t, 1}, {a, 2}, {x, 1}};
                okt &= best(t, 6) == CmText("0 %d", t);
            }
            // The type itself +51 (131, 2 short) over a later same behaviour with enough (130).
            {
                const int x = base, t = base + 2, a = base + 3, xa = base + 4;
                base += 10;
                def(x, ITEM_ANIMATED, 4, xa);
                def(xa, ITEM_AMMO, 0);
                def(t, ITEM_ANIMATED, 4, a);
                def(a, ITEM_AMMO, 0);
                g_slCounts = {{x, 1}, {xa, 4}, {t, 1}, {a, 2}};
                okt &= best(t, 4) == CmText("0 %d", t);
            }
            // Exactly enough: +30 (130 over a paired 3, 5 short: 120).
            {
                const int t = base, r = base + 1, c = base + 2, ra = base + 3, ca = base + 4;
                base += 10;
                def(t, ITEM_ANIMATED, 4, -1, 7);
                def(r, ITEM_ANIMATED, 3, ra, 0x16);
                def(ra, ITEM_AMMO, 0);
                def(c, ITEM_ANIMATED, 4, ca);
                def(ca, ITEM_AMMO, 0);
                g_slCounts = {{r, 1}, {ra, 1}, {c, 1}, {ca, 6}};
                okt &= best(t, 6) == CmText("1 %d", c);
            }
            // None of its ammunition -100 more: a scoped one (150 - 30 - 100: 20) first, tied by a later
            // small 2 (30, 1 short).
            {
                const int t = base, c = base + 1, r = base + 2, ca = base + 3, ra = base + 4;
                base += 10;
                def(t, ITEM_ANIMATED, 4)->m_secondaryComponent = &reticle;
                def(c, ITEM_ANIMATED, 4, ca)->m_secondaryComponent = &reticle;
                def(ca, ITEM_AMMO, 0);
                def(r, ITEM_ANIMATED, 2, ra);
                def(ra, ITEM_AMMO, 0);
                g_slCounts = {{c, 1}, {ca, 0}, {r, 1}, {ra, 2}};
                okt &= best(t, 3) == CmText("1 %d", c);
                // (Aim mode 3 a reticle too.)
                C_ScopeComp three;
                three.m_aimMode = 3;
                g_gfItemDefs[t]->m_secondaryComponent = &three;
                g_gfItemDefs[c]->m_secondaryComponent = &three;
                okt &= best(t, 3) == CmText("1 %d", c);
            }
            // Ammunition asked only above 0; behaviour 12 counted; an ammunition type 0 counted.
            {
                const int t = base, y = base + 1, x = base + 2, xa = base + 3;
                base += 10;
                def(t, ITEM_ANIMATED, 12);
                def(y, ITEM_ANIMATED, 1);
                def(x, ITEM_ANIMATED, 12, xa);
                def(xa, ITEM_AMMO, 0);
                g_slCounts = {{y, 1}, {x, 1}};
                okt &= best(t, 1) == CmText("1 %d", y) && best(t, 0) == CmText("1 %d", x);
                def(0, ITEM_AMMO, 0);
                g_gfItemDefs[x] = defs.back().get();
                C_SlDef *x0 = def(x, ITEM_ANIMATED, 12, 0);
                (void)x0;
                g_slCounts = {{y, 1}, {x, 1}, {0, 0}};
                okt &= best(t, 1) == CmText("1 %d", y);
                g_gfItemDefs.erase(0);
            }
            // A score of exactly 1 chosen (the type itself: 151, 15 short; not small: its ammunition
            // no candidate; an id paired with none).
            {
                const int t = base, a = base + 3;
                base += 10;
                def(t, ITEM_ANIMATED, 8, a, 9999);
                def(a, ITEM_AMMO, 0);
                g_slCounts = {{t, 1}, {a, 1}};
                okt &= best(t, 16) == CmText("0 %d", t);
            }
            // Type 0: a candidate, and asked.
            def(0, ITEM_ANIMATED, 4);
            {
                const int t = base;
                base += 10;
                def(t, ITEM_ANIMATED, 4);
                g_slCounts = {{0, 1}};
                okt &= best(t, 0) == "1 0" && best(0, 0) == "0 0";
            }
            g_gfItemDefs.erase(0);
            // Behaviour 7 not small (as asked or offered).
            {
                const int t = base, x = base + 2, t2 = base + 4, x2 = base + 5;
                base += 10;
                def(t, ITEM_ANIMATED, 7);
                def(x, ITEM_ANIMATED, 3);
                def(t2, ITEM_ANIMATED, 6);
                def(x2, ITEM_ANIMATED, 7);
                g_slCounts = {{x, 1}};
                okt &= best(t, 0) == "0 -1";
                g_slCounts = {{x2, 1}};
                okt &= best(t2, 0) == "0 -1";
            }
            WM(r.esSubstitute, okt);
        }

        // The match.
        bool okm = true;
        S_LoadoutPreset p;
        p.head = 3;
        p.hands = 50;
        p.longGun = 10;
        p.uniform = 2;
        p.backpack = 1;
        p.beltCount = 3;
        p.belt[0] = {60, 2};
        p.belt[1] = {3, 1};
        p.belt[2] = {60, 1};
        p.backpackCount = 2;
        p.backpackItems[0] = {2, 4};
        p.backpackItems[1] = {1, 1};
        std::strcpy(p.name, "P");
        p.flags = LOADOUT_ALLIED;
        p.textId = 9;
        p.unknown1b4 = 7;
        // (Head 3 kept; weapon 50 swapped for 51 (its ammunition 60 for 61); long gun 10 for 12; the
        // uniform's 2 for 3; the backpack's ammunition none: kept.)
        r12->m_secondaryComponent = &reticle;
        g_slCounts = {{3, 1}, {51, 1}, {61, 9}, {12, 1}};
        S_LoadoutPreset *q = m.MatchPreset(p, false);
        okm &= q && q->head == 3 && q->hands == 51 && q->longGun == 12 && q->uniform == 3 && q->backpack == 1 &&
               q->name[0] == 0 && q->flags == 0 && q->textId == -1 && q->unknown1b4 == 0;
        // (Quirk kept: the long gun without ammunition keeps the weapon's ammunition type, its swap
        // remapping it to the long gun's none.)
        okm &= q && q->beltCount == 2 && q->belt[0].typeId == -1 && q->belt[0].count == 3 && q->belt[1].typeId == 3 &&
               q->belt[1].count == 1 && q->backpackCount == 2 && q->backpackItems[0].typeId == 3 &&
               q->backpackItems[0].count == 4 && q->backpackItems[1].typeId == 1;
        delete q;
        // (Nothing in the stock: the places none, the items dropped but the unswappable ammunition.)
        g_slCounts = {};
        q = m.MatchPreset(p, false);
        okm &= q && q->head == -1 && q->hands == -1 && q->longGun == -1 && q->uniform == -1 && q->backpack == 1 &&
               q->beltCount == 1 && q->belt[0].typeId == 60 && q->belt[0].count == 3 && q->backpackCount == 1 &&
               q->backpackItems[0].typeId == 1;
        delete q;
        WM(r.esMatch, okm);
        // The places' ammunition asked 0 (a weapon candidate short of its own would lose); a type 0
        // kept; the weapons' ammunition counted (its type 0 too: remapped); a weapon without
        // ammunition asks none; 13 belt types at most; the items' substitutes asked no ammunition.
        {
            const int h = 400, w = 401, c = 402, wa = 403;
            def(h, ITEM_STATIC, 6);
            def(w, ITEM_ANIMATED, 3, wa);
            def(wa, ITEM_AMMO, 0);
            def(c, ITEM_ANIMATED, 1);
            g_slCounts = {{w, 9}, {wa, 0}, {c, 9}};
            S_LoadoutPreset places;
            places.head = places.uniform = places.backpack = h;
            places.beltCount = 1;
            places.belt[0] = {h, 1};
            q = m.MatchPreset(places, false);
            okm &= q->head == w && q->uniform == w && q->backpack == w && q->beltCount == 1 && q->belt[0].typeId == w;
            delete q;
            // (Type 0 kept as a place, a weapon and an item.)
            def(0, ITEM_ANIMATED, 4);
            g_slCounts = {{0, 2}};
            S_LoadoutPreset zero;
            zero.head = zero.hands = 0;
            zero.beltCount = 1;
            zero.belt[0] = {0, 2};
            q = m.MatchPreset(zero, false);
            okm &= q->head == 0 && q->hands == 0 && q->beltCount == 1 && q->belt[0].typeId == 0 && q->belt[0].count == 2;
            delete q;
            // (The weapon's ammunition counted: 3 of its type 0 in the belt; the substitute's ammunition
            // there (410 short 0: 130 over 411 none) remaps it.)
            const int x2 = 410, x1 = 411, a1 = 412, a2 = 413, hw = 414;
            def(0, ITEM_AMMO, 0);
            def(hw, ITEM_ANIMATED, 4, 0);
            def(x2, ITEM_ANIMATED, 4, a2);
            def(a2, ITEM_AMMO, 0);
            def(x1, ITEM_ANIMATED, 4, a1);
            def(a1, ITEM_AMMO, 0);
            // (409: enough only for 1 of it: ahead of 410 at the count of 1.)
            def(409, ITEM_ANIMATED, 4, 408);
            def(408, ITEM_AMMO, 0);
            g_slCounts = {{409, 1}, {408, 1}, {x1, 1}, {a1, 3}, {x2, 1}, {a2, 0}};
            S_LoadoutPreset armed;
            armed.hands = hw;
            armed.beltCount = 1;
            armed.belt[0] = {0, 3};
            q = m.MatchPreset(armed, false);
            okm &= q->hands == x1 && q->beltCount == 1 && q->belt[0].typeId == a1;
            delete q;
            // (Weapon type 0 with ammunition 412: counted, 410 chosen.)
            def(0, ITEM_ANIMATED, 4, a1);
            g_slCounts = {{x1, 1}, {a1, 3}, {x2, 1}, {a2, 0}};
            S_LoadoutPreset zeroWeapon;
            zeroWeapon.hands = 0;
            zeroWeapon.beltCount = 1;
            zeroWeapon.belt[0] = {a1, 3};
            q = m.MatchPreset(zeroWeapon, false);
            okm &= q->hands == x1;
            delete q;
            g_gfItemDefs.erase(0);
            // (A long gun without ammunition after a weapon with it: none asked, the first of two ties.)
            const int lg = 420, k1 = 421, k2 = 422, ka = 423, hh = 424, hha = 425;
            def(hh, ITEM_ANIMATED, 2, hha);
            def(hha, ITEM_AMMO, 0);
            def(lg, ITEM_ANIMATED, 3);
            def(k1, ITEM_ANIMATED, 3, ka);
            def(ka, ITEM_AMMO, 0);
            def(k2, ITEM_ANIMATED, 3);
            g_slCounts = {{hh, 1}, {hha, 3}, {k1, 1}, {ka, 0}, {k2, 1}};
            S_LoadoutPreset guns;
            guns.hands = hh;
            guns.longGun = lg;
            guns.beltCount = 1;
            guns.belt[0] = {hha, 3};
            q = m.MatchPreset(guns, false);
            okm &= q->hands == hh && q->longGun == k1;
            delete q;
            // (14 ammunition types in the belt: 13 kept.)
            S_LoadoutPreset full;
            full.beltCount = 14;
            for (int i = 0; i < 14; ++i) {
                def(430 + i, ITEM_AMMO, 0);
                full.belt[i] = {430 + i, 1};
            }
            q = m.MatchPreset(full, false);
            okm &= q->beltCount == 13 && q->belt[12].typeId == 442;
            delete q;
            S_LoadoutPreset packed;
            packed.backpackCount = 32;
            for (int i = 0; i < 32; ++i) {
                def(450 + i, ITEM_AMMO, 0);
                packed.backpackItems[i] = {450 + i, 1};
            }
            q = m.MatchPreset(packed, false);
            okm &= q->backpackCount == 31 && q->backpackItems[30].typeId == 480;
            delete q;
            WM(r.esMatch, okm);
        }
        r10->m_secondaryComponent = r11->m_secondaryComponent = r12->m_secondaryComponent = nullptr;
        g_slCounts.clear();
        g_gfItemDefs = keptDefs;
        m.m_storeList.m_stock = nullptr;
    }

    // --- (part 9f-v) the automatic equipping: the own counts, the kits' slots (against a transcription
    // of the original's, on random squads), the stock-up, the required items given out, the equipping
    // (the menu's: the soldiers to equip; the button's: all, matched, stocked up). The logs exact: the
    // stock's takes and adds, the puts, the removals, and who the puts went to ---
    {
        C_EquipmentMenu m;
        C_ItemStock stock;
        m.m_storeList.SetStock(&stock);
        m.m_storeList.m_entries.resize(500);
        for (uint32_t t = 0; t < 120; ++t)
            m.m_storeList.m_entries[t] = 0x04401000 + (t << 12);
        alignas(16) static uint8_t profileMemory[4][sizeof(S_SoldierProfile)];
        std::memset(profileMemory, 0, sizeof profileMemory);
        S_SoldierProfile *pr[4];
        for (int i = 0; i < 4; ++i)
            pr[i] = reinterpret_cast<S_SoldierProfile *>(profileMemory[i]);
        g_epProfiles = {pr[0], pr[1], pr[2], pr[3]};
        S_SquadSoldier s0, s1, s2, s3;
        S_SquadSoldier *const all[4] = {&s0, &s1, &s2, &s3};
        for (int i = 0; i < 4; ++i) {
            all[i]->profileIndex = i;
            all[i]->deployed = true;
        }
        std::vector<std::unique_ptr<C_SlDef>> defs;
        const auto def = [&](int type, E_ItemRecordType record, int behaviour, int ammo = -1, float weight = 0.0f) {
            defs.push_back(std::make_unique<C_SlDef>(record, behaviour, ammo, -1));
            defs.back()->m_id = type;
            defs.back()->m_weight = weight;
            defs.back()->m_textId = 0x500 + type;
            g_gfItemDefs[type] = defs.back().get();
            g_epAllowed[defs.back().get()] = 0xFF;
            return defs.back().get();
        };
        g_gfItemDefs.clear();
        g_slCounts.clear();
        g_epDisplaced.clear();
        g_epRemoved.clear();
        const auto clear = [](S_SquadSoldier &s) {
            s.head = s.uniform = s.backpack = s.longGun = s.hands = EpMake(-1, -1, 0);
            s.belt.clear();
            s.backpackContents.clear();
        };
        // The log's stock and soldier lines, and who the puts went to (the soldiers' indices).
        const auto ops = [] {
            std::string s;
            for (const std::string &l : g_gfLog) {
                if (l.rfind("stock ", 0) == 0 || l.rfind("put ", 0) == 0 || l.rfind("remove ", 0) == 0)
                    s += l + "; ";
            }
            return s;
        };
        const auto who = [&all] {
            std::string s;
            for (const S_SquadSoldier *p : g_epPutWho)
                for (int i = 0; i < 4; ++i)
                    if (p == all[i])
                        s += CmText("%d", i);
            g_epPutWho.clear();
            return s;
        };
        const auto run = [&](const std::function<void()> &f) {
            g_gfLog.clear();
            g_epPutWho.clear();
            g_controlMessages.clear();
            f();
            return ops() + "| " + who();
        };
        // A queue's panel refreshes' and store updates' messages counted (its texts depend on the loads).
        const auto refreshes = [](const std::string &q) {
            size_t a = 0, b = 0;
            for (size_t at = q.find(" 1000039 "); at != std::string::npos; at = q.find(" 1000039 ", at + 1))
                ++a;
            for (size_t at = q.find(" 100007c "); at != std::string::npos; at = q.find(" 100007c ", at + 1))
                ++b;
            return CmText("%d %d", static_cast<int>(a), static_cast<int>(b));
        };
        // The logs expected (written from the original's flow; the puts' places the fakes').
        const std::map<std::string, std::string> gold = {
            {"distribute", "stock take 32 1; put 1 -1 32 1; stock take 33 1; put 1 -1 33 1; stock take 31 1; put 1 -1 31 1; stock take 34 1; put 1 -1 34 1; stock take 37 1; put 1 -1 37 1; stock take 32 1; put 1 -1 32 1; stock take 38 1; put 1 -1 38 1; stock take 33 1; put 1 -1 33 1; stock take 36 1; put 2 -1 36 1; stock take 35 1; put 2 -1 35 1; | 0100010110"},
            {"distributeCarried", "| "},
            {"distributeNone", "| "},
            {"distributePast", "stock take 35 1; put 2 -1 35 1; | 0"},
            {"distributeShort", "stock take 32 1; put 1 -1 32 1; stock add 32 1; stock take 32 1; stock add 32 1; stock take 32 1; stock take 31 1; put 1 -1 31 1; stock take 31 1; put 1 -1 31 1; stock take 36 1; stock take 35 1; | 001"},
            {"equipAll", "remove 20 5 34 1; stock add 34 1; remove 20 5 34 1; stock add 34 1; remove 20 5 34 1; stock add 34 1; stock take 40 1; put 1 -1 40 1; stock take 28 1; put 4 -1 28 1; stock take 20 1; put 8 -1 20 1; stock take 26 1; put 1 -1 26 1; stock take 27 1; put 1 -1 27 1; stock take 27 1; put 1 -1 27 1; stock take 42 1; put 4 -1 42 1; stock take 20 1; put 8 -1 20 1; stock take 23 1; put 10 -1 23 1; stock take 22 2; put 20 -1 22 2; stock take 24 1; put 40 -1 24 1; stock take 41 3; put 40 -1 41 3; stock take 20 1; put 8 -1 20 1; stock take 26 1; put 1 -1 26 1; stock take 27 1; put 1 -1 27 1; stock take 25 1; put 1 -1 25 1; stock take 26 4; put 1 -1 26 4; stock take 27 1; put 1 -1 27 1; stock take 29 2; put 1 -1 29 2; stock take 25 1; put 1 -1 25 1; stock take 26 3; put 1 -1 26 3; stock take 27 1; put 1 -1 27 1; stock take 25 2; put 1 -1 25 2; stock take 27 1; put 1 -1 27 1; stock take 29 3; put 1 -1 29 3; stock take 25 2; put 1 -1 25 2; stock take 27 1; put 1 -1 27 1; | 111110000000000000111100111"},
            {"equipArmed", "| "},
            {"equipAxis", "remove 20 5 34 1; stock add 34 1; remove 20 5 34 1; stock add 34 1; remove 20 5 34 1; stock add 34 1; stock take 40 1; put 1 -1 40 1; stock take 28 1; put 4 -1 28 1; stock take 20 1; put 8 -1 20 1; stock take 26 1; put 1 -1 26 1; stock take 27 1; put 1 -1 27 1; stock take 27 1; put 1 -1 27 1; stock take 43 1; put 4 -1 43 1; stock take 43 1; put 10 -1 43 1; stock take 22 2; put 20 -1 22 2; stock take 22 1; put 40 -1 22 1; stock take 43 3; put 40 -1 43 1; stock take 20 1; put 8 -1 20 1; stock take 29 2; put 1 -1 29 2; stock take 25 1; put 1 -1 25 1; stock take 26 4; put 1 -1 26 4; stock take 27 1; put 1 -1 27 1; stock take 20 1; put 8 -1 20 1; stock take 29 3; put 1 -1 29 3; stock take 25 2; put 1 -1 25 2; stock take 26 4; put 1 -1 26 4; stock take 27 1; put 1 -1 27 1; | 111110000001111111111"},
            {"equipNew", "stock take 27 1; put 1 -1 27 1; stock take 21 1; put 4 -1 21 1; stock take 20 1; put 8 -1 20 1; stock take 23 1; put 10 -1 23 1; stock take 22 2; put 20 -1 22 2; stock take 24 1; put 40 -1 24 1; stock take 41 3; put 40 -1 41 3; | 0000000"},
            {"replenish0", "stock take 0 1; put 8 -1 0 1; stock take 22 6; put 1 -1 22 6; stock take 24 3; put 1 -1 24 3; stock take 26 1; put 1 -1 26 1; stock take 99 1; put 1 -1 99 1; stock take 9 1; put 1 -1 9 1; stock take 27 1; put 1 -1 27 1; | 0000000"},
            {"replenish1", "stock take 0 1; put 8 -1 0 1; stock take 22 13; put 1 -1 22 13; stock take 29 2; put 1 -1 29 2; stock take 24 3; put 1 -1 24 3; stock take 25 1; put 1 -1 25 1; stock take 26 4; put 1 -1 26 4; stock take 99 4; put 1 -1 99 4; stock take 9 1; put 1 -1 9 1; stock take 27 1; put 1 -1 27 1; | 000000000"},
            {"replenish2", "stock take 0 1; put 8 -1 0 1; stock take 22 20; put 1 -1 22 20; stock take 29 3; put 1 -1 29 3; stock take 24 3; put 1 -1 24 3; stock take 25 2; put 1 -1 25 2; stock take 26 5; put 1 -1 26 5; stock take 99 5; put 1 -1 99 5; stock take 9 1; put 1 -1 9 1; stock take 27 1; put 1 -1 27 1; | 000000000"},
            {"replenishAxis", "stock take 26 1; put 1 -1 26 1; stock take 9 1; put 1 -1 9 1; stock take 27 1; put 1 -1 27 1; | 000"},
            {"replenishBare", "stock take 20 1; put 8 -1 20 1; stock take 0 1; put 1 -1 0 1; stock take 26 1; put 1 -1 26 1; stock take 99 1; put 1 -1 99 1; stock take 9 1; put 1 -1 9 1; | 00000"},
            {"replenishOther", "stock take 0 1; put 8 -1 0 1; stock take 24 3; put 1 -1 24 3; stock take 26 1; put 1 -1 26 1; stock take 99 1; put 1 -1 99 1; stock take 9 1; put 1 -1 9 1; stock take 27 1; put 1 -1 27 1; | 111111"},
            {"replenishOwned", "stock take 29 1; put 2 -1 29 1; stock take 24 3; put 2 -1 24 3; stock take 99 4; put 2 -1 99 4; | 000"},
            {"replenishShort", "stock take 20 1; put 8 -1 20 1; stock take 22 1; put 1 -1 22 1; stock take 25 1; put 1 -1 25 1; stock take 99 1; put 1 -1 99 1; | 0000"},
            {"distributeWrap", "stock take 32 1; put 1 -1 32 1; stock take 33 1; put 1 -1 33 1; stock take 32 1; put 1 -1 32 1; stock take 35 1; | 010"},
            {"distributeFailed", "stock take 32 1; put 1 -1 32 1; stock take 32 1; put 1 -1 32 1; stock add 32 1; stock take 32 1; stock add 32 1; stock take 32 1; stock take 33 1; put 1 -1 33 1; stock take 39 1; put 1 -1 39 1; stock add 39 1; stock take 39 1; stock add 39 1; stock take 39 1; stock take 35 1; | 0101"},
            {"distributeRefresh", "143 47"},
            {"equipNewRefresh", "117 84"},
        };
        const auto check = [&gold](const char *name, const std::string &got) {
            if (!gold.count(name)) {
                std::fprintf(stderr, "GOLD %s: %s\n", name, got.c_str());
                return false;
            }
            const bool same = gold.at(name) == got;
            if (!same)
                std::fprintf(stderr, "%s differs:\n  %s\n  %s\n", name, got.c_str(), gold.at(name).c_str());
            return same;
        };

        // The own counts: a place one, the belt's and the backpack's their counts.
        bool okc = true;
        squad.states = {&s0, &s1};
        s0.head = EpMake(1, 3, 4);
        s0.uniform = EpMake(2, 3, 1);
        s0.belt = {EpMake(3, 3, 2), EpMake(4, 4, 1)};
        s0.backpackContents = {EpMake(5, 3, 5)};
        s0.hands = s0.longGun = s0.backpack = EpMake(9, 3, 1);
        okc &= C_EquipmentMenu::CountOwn(3, 0) == 12 && C_EquipmentMenu::CountOwn(4, 0) == 1 &&
               C_EquipmentMenu::CountOwn(5, 0) == 0 && C_EquipmentMenu::CountOwn(3, 1) == 0;
        clear(s0);
        okc &= C_EquipmentMenu::CountOwn(3, 0) == 0 && C_EquipmentMenu::CountOwn(-1, 0) == 5;
        WM(r.esCountOwn, okc);

        // The kits' slots against a transcription of the original's (0x0067a3f0) on random squads: the
        // kits' weights and weapons, the shots and strengths.
        bool oka = true;
        {
            C_ScopeComp reticle, sights;
            reticle.m_aimMode = 2;
            sights.m_aimMode = 5;
            // The types: 60.. plain items of weights 0..4.5, 70.. rifles (scoped, sighted, plain), 80..
            // other weapons.
            for (int i = 0; i < 10; ++i)
                def(60 + i, ITEM_STATIC, 0, -1, 0.5f * static_cast<float>(i));
            for (int i = 0; i < 6; ++i)
                def(70 + i, ITEM_ANIMATED, 4, -1, 1.0f + static_cast<float>(i))->m_secondaryComponent =
                    i % 3 == 0 ? &reticle : i % 3 == 1 ? &sights : nullptr;
            for (int i = 0; i < 4; ++i)
                def(80 + i, ITEM_ANIMATED, 3, -1, 2.0f * static_cast<float>(i));
            std::vector<std::unique_ptr<S_LoadoutPreset>> kits;
            uint32_t seed = 12345;
            const auto rnd = [&seed](int n) {
                seed = seed * 1103515245u + 12345u;
                return static_cast<int>((seed >> 16) % static_cast<uint32_t>(n));
            };
            const auto anyType = [&rnd]() {
                const int k = rnd(4);
                return k == 0 ? -1 : k == 1 ? 60 + rnd(10) : k == 2 ? 70 + rnd(6) : 80 + rnd(4);
            };
            for (int trial = 0; trial < 1500 && oka; ++trial) {
                const int count = 1 + rnd(4);
                kits.clear();
                std::vector<S_LoadoutPreset *> presets;
                for (int p = 0; p < 4; ++p) {
                    kits.push_back(std::make_unique<S_LoadoutPreset>());
                    S_LoadoutPreset &k = *kits.back();
                    k.head = rnd(2) ? 60 + rnd(10) : -1;
                    k.uniform = rnd(2) ? 60 + rnd(10) : -1;
                    k.hands = anyType();
                    k.longGun = anyType();
                    k.backpack = rnd(2) ? 60 + rnd(10) : -1;
                    k.beltCount = rnd(3);
                    for (int i = 0; i < k.beltCount; ++i)
                        k.belt[i] = {60 + rnd(10), 1 + rnd(4)};
                    k.backpackCount = rnd(3);
                    for (int i = 0; i < k.backpackCount; ++i)
                        k.backpackItems[i] = {60 + rnd(10), 1 + rnd(4)};
                    presets.push_back(&k);
                }
                m.m_presets.m_presets = presets;
                S_MissionItems record;
                for (int k = 0; k < count + 1 && k < 10; ++k)
                    record.slots[k] = {rnd(6) - 1, rnd(2) ? 60 + rnd(10) : -1, rnd(2) ? 60 + rnd(10) : -1};
                std::vector<S_SquadSoldier *> sq;
                for (int i = 0; i < count; ++i) {
                    pr[i]->stats[STAT_SHOOTING] = static_cast<float>(rnd(5) * 10 + rnd(3));
                    pr[i]->stats[STAT_STRENGTH] = static_cast<float>(rnd(trial % 2 ? 40 : 200)) * 0.5f;
                    sq.push_back(all[i]);
                }
                squad.states = sq;
                // The transcription.
                int want[10];
                {
                    float weight[10];
                    int flags[10];
                    bool taken[10];
                    int scoped = 0;
                    const auto w = [](int type) {
                        C_ItemDef *d = type >= 0 ? GetItemDef(type) : nullptr;
                        return d ? d->GetWeight() : 0.0f;
                    };
                    const auto isReticle = [](C_ItemDef *d) {
                        const C_ItemComponent *c = d->GetSecondaryComponent();
                        return c && c->GetKind() == ICK_SCOPE && static_cast<const C_ScopeComp *>(c)->GetAimMode() < 4;
                    };
                    for (int k = 0; k < count; ++k) {
                        const S_MissionItems::S_Slot &sl = record.slots[k];
                        taken[k] = false;
                        weight[k] = 0.0f;
                        flags[k] = 0;
                        if (sl.a < 0 || sl.a >= static_cast<int>(presets.size())) {
                            weight[k] = w(sl.c);
                            weight[k] = w(sl.b) + weight[k];
                            continue;
                        }
                        const S_LoadoutPreset *p = presets[static_cast<size_t>(sl.a)];
                        weight[k] = w(sl.c != -1 ? sl.c : p->uniform);
                        weight[k] = w(sl.b != -1 ? sl.b : p->head) + weight[k];
                        if (C_ItemDef *d = p->hands >= 0 ? GetItemDef(p->hands) : nullptr) {
                            weight[k] += d->GetWeight();
                            if (d->GetBehaviour() == 4) {
                                flags[k] = 2;
                                if (isReticle(d)) {
                                    flags[k] = 3;
                                    ++scoped;
                                }
                            }
                        }
                        if (C_ItemDef *d = p->longGun >= 0 ? GetItemDef(p->longGun) : nullptr) {
                            weight[k] += d->GetWeight();
                            if (d->GetBehaviour() == 4) {
                                const int old = flags[k];
                                flags[k] = old | 2;
                                if (isReticle(d)) {
                                    ++scoped;
                                    flags[k] = old | 3;
                                }
                            }
                        }
                        weight[k] = w(p->backpack) + weight[k];
                        for (int i = 0; i < p->beltCount; ++i)
                            weight[k] = w(p->belt[i].typeId) * static_cast<float>(p->belt[i].count) + weight[k];
                        for (int i = 0; i < p->backpackCount; ++i)
                            weight[k] = w(p->backpackItems[i].typeId) * static_cast<float>(p->backpackItems[i].count) + weight[k];
                    }
                    int best = -1, second = -1, bestAt = -1, secondAt = -1;
                    for (int i = 0; i < count; ++i) {
                        const float sh = pr[i]->stats[STAT_SHOOTING];
                        if (static_cast<float>(best) < sh) {
                            second = best;
                            secondAt = bestAt;
                            best = static_cast<int>(sh);
                            bestAt = i;
                        } else if (static_cast<float>(second) < sh) {
                            second = static_cast<int>(sh);
                            secondAt = i;
                        }
                    }
                    for (int s = 0; s < count; ++s) {
                        const float str = pr[s]->stats[STAT_STRENGTH];
                        int at = -1, sc = -1;
                        for (int k = 0; k < count; ++k) {
                            if (taken[k])
                                continue;
                            if (at == -1) {
                                sc = 0;
                                at = k;
                            }
                            const bool fit = weight[k] < str || weight[k] == str;
                            if (s == bestAt && ((flags[k] & 1) || ((flags[k] & 2) && scoped == 0)) && fit) {
                                sc = 100;
                                at = k;
                            } else if (s == secondAt && (flags[k] & 2) && (!(flags[k] & 1) || scoped > 1) && fit) {
                                sc = 90;
                                at = k;
                            } else if (fit && !(flags[k] & 1)) {
                                int v = static_cast<int>(60.0f - (str - weight[k]));
                                if (flags[k] & 2)
                                    v -= 30;
                                if (sc < v) {
                                    sc = v;
                                    at = k;
                                }
                            }
                        }
                        taken[at] = true;
                        want[s] = count == 1 ? 0 : at;
                    }
                }
                int got[10] = {};
                m.AssignSlots(record, got, count);
                for (int s = 0; s < count; ++s)
                    oka &= got[s] == want[s];
                if (!oka)
                    std::fprintf(stderr, "esAssign trial %d differs\n", trial);
            }
            m.m_presets.m_presets.clear();
            // (Exact: a rifle's kit 30 less: 51 - 30 beats a plain kit's 20.)
            def(76, ITEM_ANIMATED, 4, -1, 31.0f);
            S_LoadoutPreset rifleKit;
            rifleKit.longGun = 76;
            m.m_presets.m_presets = {&rifleKit};
            S_MissionItems exact;
            exact.slots[0] = {-1, -1, -1};
            exact.slots[1] = {0, -1, -1};
            exact.slots[2] = {-1, -1, -1};
            squad.states = {&s0, &s1, &s2};
            const float shots[3] = {10.0f, 90.0f, 80.0f};
            for (int i = 0; i < 3; ++i) {
                pr[i]->stats[STAT_SHOOTING] = shots[i];
                pr[i]->stats[STAT_STRENGTH] = 40.0f;
            }
            int got[10] = {};
            m.AssignSlots(exact, got, 3);
            oka &= got[0] == 1;
            m.m_presets.m_presets.clear();
        }
        WM(r.esAssign, oka);

        // The stock-up.
        bool okp = true;
        squad.states = {&s0, &s1};
        clear(s0);
        clear(s1);
        def(0, ITEM_STATIC, 17);
        def(20, ITEM_STATIC, 17);
        def(21, ITEM_ANIMATED, 3, 22);
        def(22, ITEM_AMMO, 0);
        def(23, ITEM_ANIMATED, 4, 24);
        def(24, ITEM_AMMO, 0);
        def(25, ITEM_STATIC, 13);
        def(26, ITEM_ANIMATED, 9);
        def(99, ITEM_ANIMATED, 9);
        def(100, ITEM_ANIMATED, 9);
        def(27, ITEM_STATIC, 16);
        def(28, ITEM_ANIMATED, 2, 29);
        def(29, ITEM_AMMO, 0);
        def(9, ITEM_STATIC, 0);
        def(255, ITEM_STATIC, 13);
        s0.hands = EpMake(1, 21, 1);
        s0.longGun = EpMake(2, 23, 1);
        s1.hands = EpMake(3, 23, 1);
        pr[0]->stats[STAT_FIRST_AID] = 50.0f;
        pr[1]->stats[STAT_FIRST_AID] = 50.0f;
        const auto counts = [] {
            g_slCounts = {{0, 1}, {20, 1}, {22, 20}, {24, 3}, {25, 5}, {26, 5}, {99, 5}, {100, 5}, {27, 1}, {29, 10}, {9, 1}, {255, 5}};
        };
        counts();
        m.m_panel.SelectSoldier(1);
        okp &= check("replenish0", run([&] { m.Replenish(0, 0, -1); }));
        counts();
        okp &= check("replenish1", run([&] { m.Replenish(0, 1, 28); }));
        counts();
        okp &= check("replenish2", run([&] { m.Replenish(0, 2, 28); }));
        // (Owned: what is short of the limits; a backpack and a head gear none.)
        s0.backpack = EpMake(4, 20, 1);
        s0.head = EpMake(5, 27, 1);
        s0.belt = {EpMake(6, 22, 13), EpMake(7, 26, 4), EpMake(8, 9, 1), EpMake(9, 25, 1), EpMake(10, 29, 1)};
        counts();
        okp &= check("replenishOwned", run([&] { m.Replenish(0, 1, 28); }));
        clear(s0);
        s0.hands = EpMake(1, 21, 1);
        // (Short stocks: what there is; none of a kit, nothing taken.)
        g_slCounts = {{20, 1}, {22, 1}, {24, 1}, {25, 1}, {26, 0}, {99, 1}};
        okp &= check("replenishShort", run([&] { m.Replenish(0, 1, -1); }));
        // (Another soldier: his own places.)
        counts();
        okp &= check("replenishOther", run([&] { m.Replenish(1, 0, -1); }));
        // (An axis uniform: the axis' items only; type 0 the uniform.)
        def(0, ITEM_STATIC, 15)->m_team = 1;
        g_gfItemDefs[26]->m_team = 1;
        g_gfItemDefs[23]->m_team = 1;
        g_gfItemDefs[27]->m_team = 1;
        s0.uniform = EpMake(11, 0, 1);
        counts();
        okp &= check("replenishAxis", run([&] { m.Replenish(0, 0, -1); }));
        g_gfItemDefs[26]->m_team = 0;
        g_gfItemDefs[23]->m_team = 0;
        g_gfItemDefs[27]->m_team = 0;
        // (Uniform 0x79: no head gear; a weapon whose ammunition is type 0.)
        def(0x79, ITEM_STATIC, 15);
        s0.uniform = EpMake(11, 0x79, 1);
        def(0, ITEM_AMMO, 0);
        def(21, ITEM_ANIMATED, 3, 0);
        counts();
        okp &= check("replenishBare", run([&] { m.Replenish(0, 0, -1); }));
        def(21, ITEM_ANIMATED, 3, 22);
        // (A medkit of type 0 counted too.)
        def(0, ITEM_STATIC, 13);
        s0.uniform = EpMake(-1, -1, 0);
        g_slCounts = {{0, 1}};
        g_gfLog.clear();
        m.Replenish(0, 1, -1);
        okp &= MrHas("stock take 0 1");
        def(0, ITEM_STATIC, 17);
        clear(s0);
        clear(s1);
        WM(r.esReplenish, okp);

        // The required items given out (their list, the rotation, the uniforms).
        bool okd = true;
        squad.states = {&s0, &s1};
        S_MissionItems *rec = m.m_missionItems.m_records[0];
        g_esMission = 0;
        def(31, ITEM_STATIC, 15);
        def(32, ITEM_STATIC, 18);
        def(33, ITEM_STATIC, 18);
        def(34, ITEM_STATIC, 0);
        def(35, ITEM_STATIC, 15);
        def(36, ITEM_STATIC, 15);
        def(37, ITEM_STATIC, 16);
        def(38, ITEM_STATIC, 20);
        rec->required = {{32, 1}, {33, 1}, {31, 1}, {34, 1}, {37, 1}, {32, 1}, {38, 1}, {33, 1}};
        rec->slots[0] = {-1, -1, 35};
        rec->slots[1] = {0, -1, -1};
        S_LoadoutPreset wear;
        wear.uniform = 36;
        m.m_presets.m_presets = {&wear};
        g_slCounts = {{31, 1}, {32, 2}, {33, 2}, {34, 1}, {35, 1}, {36, 1}, {37, 1}, {38, 1}};
        m.m_soldier = 1;
        std::string q;
        okd &= check("distribute", run([&] { m.DistributeRequired(); }));
        q = GmQueue();
        // (The essentials' text's messages exactly.)
        std::string essentials;
        for (size_t at = q.find("4900000 "); at != std::string::npos; at = q.find("4900000 ", at + 1))
            essentials += q.substr(at, q.find("; ", at) + 2 - at);
        okd &= essentials ==
               "4900000 2000018 0 0 ; 4900000 1000017 2 aa9 ; 4900000 1000016 f 0 1x ; 4900000 1000017 f 520 ; "
               "4900000 1000016 f 0 , ; 4900000 1000016 f 0 1x ; 4900000 1000017 f 521 ; 4900000 1000016 f 0 , ; "
               "4900000 1000016 f 0 1x ; 4900000 1000017 f 51f ; 4900000 1000016 f 0 , ; 4900000 1000016 f 0 1x ; "
               "4900000 1000017 f 522 ; 4900000 1000016 f 0 , ; 4900000 1000016 f 0 1x ; 4900000 1000017 f 525 ; "
               "4900000 1000016 f 0 , ; 4900000 1000016 f 0 1x ; 4900000 1000017 f 520 ; 4900000 1000016 f 0 , ; "
               "4900000 1000016 f 0 1x ; 4900000 1000017 f 526 ; 4900000 1000016 f 0 , ; 4900000 1000016 f 0 1x ; "
               "4900000 1000017 f 521 ; 4900000 1000016 f 0 . ; ";
        okd &= check("distributeRefresh", refreshes(q)) && m.m_panel.m_squadSoldier == &s1 &&
               q.find(" 1000039 ") != std::string::npos && q.rfind(" 100007c ") != std::string::npos &&
               q.rfind(" 100007c ") > q.rfind(" 1000039 ");
        // (Counts: missing two of one, three tries failing in a row end it; uniforms capped.)
        rec->required = {{32, 2}, {31, 5}};
        g_slCounts = {{32, 1}, {31, 5}};
        okd &= check("distributeShort", run([&] { m.DistributeRequired(); }));
        okd &= rec->required[1].second == 2;
        // (Carried already: none given; the soldiers' own uniforms kept.)
        s0.uniform = EpMake(1, 31, 1);
        s1.uniform = EpMake(2, 31, 1);
        s0.belt = {EpMake(3, 32, 2)};
        g_slCounts = {{31, 5}, {32, 5}, {35, 1}, {36, 1}};
        okd &= check("distributeCarried", run([&] { m.DistributeRequired(); }));
        clear(s0);
        clear(s1);
        // (The slot's preset past the presets: none.)
        rec->required.clear();
        rec->slots[1] = {1, -1, -1};
        g_slCounts = {{35, 1}, {36, 1}};
        okd &= check("distributePast", run([&] { m.DistributeRequired(); }));
        // (The rotation round the squad: a third plain item to the first again.)
        rec->slots[1] = S_MissionItems::S_Slot();
        rec->required = {{32, 1}, {33, 1}, {32, 1}};
        g_slCounts = {{32, 2}, {33, 1}};
        okd &= check("distributeWrap", run([&] { m.DistributeRequired(); }));
        // (The tries failing counted from the last success: two more after one.)
        rec->required = {{32, 3}, {33, 1}, {39, 2}};
        def(39, ITEM_STATIC, 18);
        g_slCounts = {{32, 2}, {33, 1}, {39, 1}};
        okd &= check("distributeFailed", run([&] { m.DistributeRequired(); }));
        rec->required.clear();
        // (No record: nothing.)
        g_esMission = 9;
        okd &= check("distributeNone", run([&] { m.DistributeRequired(); })) && g_controlMessages.empty();
        g_esMission = 0;
        rec->slots[0] = rec->slots[1] = S_MissionItems::S_Slot();
        WM(r.esDistribute, okd);

        // The equipping.
        bool oke = true;
        // (The kits: the best shot the rifle's (slot 0), the other the pistol's (slot 1).)
        def(40, ITEM_STATIC, 16);
        def(41, ITEM_STATIC, 0);
        S_LoadoutPreset riflePreset, pistolPreset;
        riflePreset.hands = 21;
        riflePreset.backpack = 20;
        riflePreset.longGun = 23;
        riflePreset.beltCount = 1;
        riflePreset.belt[0] = {22, 2};
        riflePreset.backpackCount = 2;
        riflePreset.backpackItems[0] = {24, 1};
        riflePreset.backpackItems[1] = {41, 3};
        pistolPreset.head = 40;
        pistolPreset.hands = 28;
        m.m_presets.m_presets = {&riflePreset, &pistolPreset};
        rec->slots[0] = {0, 27, -1};
        rec->slots[1] = {1, -1, -1};
        pr[0]->stats[STAT_SHOOTING] = 20.0f;
        pr[1]->stats[STAT_SHOOTING] = 80.0f;
        pr[0]->stats[STAT_STRENGTH] = pr[1]->stats[STAT_STRENGTH] = 40.0f;
        const auto equipCounts = [] {
            g_slCounts = {{20, 3}, {21, 3}, {22, 40}, {23, 3}, {24, 9}, {25, 9}, {26, 9}, {27, 9}, {28, 3}, {29, 20}, {40, 3},
                          {41, 9}};
        };
        clear(s0);
        clear(s1);
        s1.belt = {EpMake(5, 34, 1)};
        s0.autoEquip = true;
        s1.autoEquip = false;
        m.m_soldier = 0;
        g_esMission = 9;
        oke &= !m.AutoEquip(nullptr, nullptr, 0, 0) && s0.autoEquip;
        g_esMission = 0;
        equipCounts();
        oke &= check("equipNew", run([&] { oke &= m.AutoEquip(nullptr, nullptr, 0, 0); })) && !s0.autoEquip;
        q = GmQueue();
        oke &= check("equipNewRefresh", refreshes(q)) && m.m_panel.m_squadSoldier == &s0;
        // (Armed: nothing put, no more to equip all the same.)
        s0.autoEquip = true;
        s0.longGun = EpMake(9, 23, 1);
        equipCounts();
        oke &= check("equipArmed", run([&] { oke &= m.AutoEquip(nullptr, nullptr, 0, 0); })) && !s0.autoEquip;
        clear(s0);
        // The button's: both soldiers matched (the axis' for an axis uniform), then stocked up.
        C_GmIndexed button(0x04108000, 0);
        s1.belt = {EpMake(5, 34, 1), EpMake(6, 21, 1), EpMake(7, 28, 1)};
        equipCounts();
        g_slCounts.erase(21);
        def(42, ITEM_ANIMATED, 3, 22);
        g_slCounts[42] = 3;
        oke &= check("equipAll", run([&] { oke &= C_EquipmentMenu::CallAutoEquip(&m, nullptr, &button, CONTROL_NOTIFY_CLICK, 0); })) &&
               m.m_panel.m_squadSoldier == &s0;
        def(43, ITEM_ANIMATED, 3, 22)->m_team = 1;
        def(44, ITEM_STATIC, 15)->m_team = 1;
        s0.uniform = EpMake(8, 44, 1);
        equipCounts();
        g_slCounts.erase(21);
        g_slCounts[42] = 3;
        g_slCounts[43] = 3;
        oke &= check("equipAxis", run([&] { oke &= m.AutoEquip(&m, nullptr, 0, 0); }));
        clear(s0);
        clear(s1);
        WM(r.esAutoEquip, oke);

        m.m_presets.m_presets.clear();
        rec->slots[0] = rec->slots[1] = S_MissionItems::S_Slot();
        rec->required.clear();
        squad.states.clear();
        m.m_panel.m_squadSoldier = nullptr;
        m.m_storeList.m_stock = nullptr;
        g_slCounts.clear();
        g_epProfiles.clear();
        g_gfItemDefs = oldDefs;
        for (const auto &d : defs)
            g_epAllowed.erase(d.get());
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
                "esCarried %d esRequired %d esItemsScreen %d esSelection %d esStatsPanel %d esStore2 %d esSquadList %d esSquad %d esEquipmentPanel %d esBuild %d esPut %d esReturn %d esTypePicked %d esSubstitute %d esMatch %d esCountOwn %d esAssign %d esReplenish %d esDistribute %d esAutoEquip %d\n",
                r.esNew, r.esStart, r.esStop, r.esStore, r.esTake, r.esClicked, r.esDragStart, r.esDragMove, r.esDrop,
                r.esHover, r.esStats, r.esToggle, r.esPicked, r.esMessage, r.esChecks,
                r.esCarried, r.esRequired, r.esItemsScreen, r.esSelection, r.esStatsPanel, r.esStore2, r.esSquadList, r.esSquad, r.esEquipmentPanel, r.esBuild, r.esPut, r.esReturn, r.esTypePicked, r.esSubstitute, r.esMatch, r.esCountOwn, r.esAssign, r.esReplenish, r.esDistribute, r.esAutoEquip);
}
