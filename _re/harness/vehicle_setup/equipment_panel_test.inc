// Part 9e-ii-a's equipment panel (game/equipment_panel.cpp), in the factory build, run by ControlsTest
// after MenuSoldierTest: the squad soldier's items, the item stock and the human store faked (each
// call logged, its answers set by the test), the control messages read back from the queue
// ("msg <target> <id> <value> <value2> <text>"), the menu soldier menu_soldier_test.inc's. Prints
// "equipmentpanel:" with 1 per check met.
#define private public
#include "game/equipment_panel.h"
#undef private

// The squad soldier's items and load (logged; the answers the test's).
std::deque<S_SoldierItem> g_epDisplaced;                // PutItem's, in order (none: an empty item)
std::deque<int> g_epRemoved;                            // RemoveItem's counts (none: 0)
int g_epCount;
float g_epFreeWeight = 100.0f;
int g_epFreeBelt = 12, g_epFreeBackpack = 10;
float g_epWeightFill, g_epBeltFill, g_epBackpackFill;
std::string EpItem(const S_SoldierItem &item) { return CmText("%d %d %d", item.uid, item.typeId, item.present); }
void S_SquadSoldier::PutItem(S_SoldierItem *displaced, uint8_t slot, const S_SoldierItem &item)
{
    GfLog("put %x %s", slot, EpItem(item).c_str());
    S_SoldierItem none;
    *displaced = none;
    if (!g_epDisplaced.empty()) {
        *displaced = g_epDisplaced.front();
        g_epDisplaced.pop_front();
    }
}
int S_SquadSoldier::RemoveItem(uint8_t slot, S_SoldierItem &item)
{
    GfLog("remove %x %s", slot, EpItem(item).c_str());
    if (g_epRemoved.empty())
        return 0;
    const int n = g_epRemoved.front();
    g_epRemoved.pop_front();
    return n;
}
int S_SquadSoldier::CountItems(uint8_t slot, const S_SoldierItem &item) const
{
    GfLog("count %x %s", slot, EpItem(item).c_str());
    return g_epCount;
}
void S_SquadSoldier::ReturnItemsTo(C_ItemStock *) { GfLog("return items"); }
int S_SquadSoldier::GetFreeBeltVolume() const { return g_epFreeBelt; }
int S_SquadSoldier::GetFreeBackpackVolume() const { return g_epFreeBackpack; }
float S_SquadSoldier::GetFreeWeight() const { return g_epFreeWeight; }
float S_SoldierLoad::GetWeightFill() const { return g_epWeightFill; }
float S_SoldierLoad::GetBeltFill() const { return g_epBeltFill; }
float S_SoldierLoad::GetBackpackFill() const { return g_epBackpackFill; }
void C_ItemStock::Add(int type, int count) { GfLog("stock add %d %d", type, count); }
std::vector<S_SoldierProfile *> g_epProfiles;
S_SoldierProfile *C_HumanStore::Get(int index) const { return g_epProfiles[static_cast<size_t>(index)]; }
// The item types' containers.
std::map<const C_ItemDef *, uint32_t> g_epAllowed;
uint32_t C_ItemDef::GetAllowedContainers()
{
    const auto it = g_epAllowed.find(this);
    return it == g_epAllowed.end() ? 0 : it->second;
}

namespace {

struct S_EquipmentPanelResults {
    bool epSlots = true, epSelect = true, epShowItem = true, epRefresh = true, epLoad = true, epAllowed = true;
    bool epScroll = true, epRemove = true, epPut = true, epChecked = true, epDrop = true, epMove = true;
    bool epReturn = true, epHighlights = true, epLarge = true, epSoldier = true;
};

// An item type with its weight, size, picture, record type and rounds.
struct C_EpDef : C_ItemDef {
    int behaviour;
    float ammo;
    C_EpDef(E_ItemRecordType record, int b, float weight, int size, uintptr_t image, float rounds = -1.0f)
        : C_ItemDef(record, 0), behaviour(b), ammo(rounds)
    {
        m_weight = weight;
        m_inventorySize = size;
        m_image = reinterpret_cast<IImage *>(image);
    }
    int GetCarryMode() override { return 0; }
    int GetBehaviour() override { return behaviour; }
    float GetAmmoAmount() override { return ammo; }
};

S_SoldierItem EpMake(int uid, int type, int present)
{
    S_SoldierItem item;
    item.uid = static_cast<int16_t>(uid);
    item.typeId = static_cast<int16_t>(type);
    item.present = static_cast<int16_t>(present);
    return item;
}
// The messages posted since the last call.
std::vector<std::string> EpMessages()
{
    std::vector<std::string> out;
    for (const S_ControlMessage &m : g_controlMessages)
        out.push_back(CmText("msg %x %x %x %x %s", m.target, m.id, m.value, static_cast<uint32_t>(m.value2), m.text.c_str()));
    g_controlMessages.clear();
    return out;
}
std::string EpMsg(uint32_t target, uint32_t id, uint32_t value = 0, uint32_t value2 = 0, const char *text = "")
{
    return CmText("msg %x %x %x %x %s", target, id, value, value2, text);
}
uint32_t EpFloat(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}
bool EpIs(const S_SoldierItem &a, const S_SoldierItem &b) { return a.uid == b.uid && a.typeId == b.typeId && a.present == b.present; }

// A panel with its controls' ids (kind << 12 | child << 4) and a squad soldier.
void EpIds(C_EquipmentPanel &p)
{
    p.m_weightBar = 0x101;
    p.m_overweightBar = 0x102;
    p.m_beltBar = 0x103;
    p.m_backpackBar = 0x104;
    p.m_beltUp = 0x111;
    p.m_beltDown = 0x112;
    p.m_backpackUp = 0x113;
    p.m_backpackDown = 0x114;
    p.m_weightText = 0x121;
    p.m_beltText = 0x122;
    p.m_backpackText = 0x123;
    p.m_headSlot = 0x1000;
    p.m_uniformSlot = 0x2000;
    p.m_backpackSlot = 0x3000;
    p.m_longGunSlot = 0x4000;
    p.m_handsSlot = 0x5000;
    p.m_dropSlot = 0x6000;
    for (uint32_t i = 0; i < 4; ++i) {
        p.m_beltSlots[i] = 0x8000 | (i + 1) << 4;
        p.m_backpackSlots[i] = 0xA000 | (i + 1) << 4;
    }
}
struct C_EpControl : C_Control {
    explicit C_EpControl(uint32_t id) : C_Control(nullptr, id, nullptr) {}
};

} // namespace

void EquipmentPanelTest(I3D_scene *)
{
    S_EquipmentPanelResults r;
    MrInitVtables();
    MsInitVtables();
    g_gfLog.clear();
    g_controlMessages.clear();
    const int driverSlots[] = {0x54 / 4, 0x1C / 4, 0x18 / 4};
    void *oldDriver[3];
    for (int i = 0; i < 3; ++i)
        oldDriver[i] = g_driverVtable[driverSlots[i]];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
    g_driverVtable[0x1C / 4] = reinterpret_cast<void *>(&MrGetState);
    g_driverVtable[0x18 / 4] = reinterpret_cast<void *>(&MrSetState);
    void (*const oldFill)(I3D_model *) = g_cacheFill;
    g_cacheFill = [](I3D_model *) {};
    g_msNamer = &MrFrameName;
    void **const oldEffectsVt = *reinterpret_cast<void ***>(GetGEffects());
    static void *effectsVt[64];
    std::memcpy(effectsVt, oldEffectsVt, sizeof effectsVt);
    {
        bool (C_GEffectsModule::*pmf)(I3D_frame *, const char *) = &C_GEffectsModule::SwitchFaceTexture;
        std::ptrdiff_t raw[2];
        std::memcpy(raw, &pmf, sizeof raw);
        effectsVt[(raw[0] - 1) / static_cast<std::ptrdiff_t>(sizeof(void *))] = reinterpret_cast<void *>(&MsSwitchFace);
    }
    *reinterpret_cast<void ***>(GetGEffects()) = effectsVt;
    std::map<int, C_ItemDef *> oldDefs = g_gfItemDefs;
    // 20 head gear, 21 a uniform, 22 a rifle (large), 23 a pistol, 24 ammunition (8 rounds), 25 a
    // grenade (belt / backpack), 26 a backpack, 27 the item kept from the hands (0x105 is 261).
    C_EpDef d20(ITEM_STATIC, 0, 1.0f, 1, 0xA20), d21(ITEM_STATIC, 11, 2.0f, 2, 0xA21), d22(ITEM_ANIMATED, 3, 4.0f, 3, 0xA22),
        d23(ITEM_ANIMATED, 1, 1.0f, 1, 0xA23), d24(ITEM_AMMO, 100, 0.5f, 0, 0xA24, 8.0f), d25(ITEM_ANIMATED, 6, 0.5f, 2, 0xA25),
        d26(ITEM_STATIC, 12, 3.0f, 4, 0xA26), d261(ITEM_ANIMATED, 1, 1.0f, 1, 0xA261);
    d21.m_modelName = const_cast<char *>("GE03");
    g_gfItemDefs = {{20, &d20}, {21, &d21}, {22, &d22}, {23, &d23}, {24, &d24}, {25, &d25}, {26, &d26}, {261, &d261}};
    g_epAllowed = {{&d20, ITEM_FITS_HEAD}, {&d21, ITEM_FITS_UNIFORM}, {&d22, ITEM_FITS_HANDS | ITEM_FITS_LONG_GUN},
                   {&d23, ITEM_FITS_HANDS | ITEM_FITS_BELT}, {&d24, ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS},
                   {&d25, ITEM_FITS_BELT | ITEM_FITS_HANDS | ITEM_FITS_BACKPACK_CONTENTS}, {&d26, ITEM_FITS_BACKPACK},
                   {&d261, ITEM_FITS_HANDS | ITEM_FITS_LONG_GUN}};
    // The mission's squad.
    S_SquadSoldier sq0, sq1;
    sq0.belt.assign(3, EpMake(1, 24, 2));
    sq1.belt.assign(6, EpMake(2, 23, 1));
    sq1.backpackContents.assign(5, EpMake(3, 25, 1));
    S_MissionSquad squad;
    squad.states = {&sq0, &sq1};
    C_HumanStore store;
    S_MissionSquad *const oldSquad = g_gameProfile.m_missionSquad;
    C_HumanStore *const oldStore = g_gameProfile.m_humanStore;
    g_gameProfile.m_missionSquad = &squad;
    g_gameProfile.m_humanStore = &store;

    // --- the controls' places by their ids' kinds (the groups' items 1..4 only) ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        const uint32_t ids[] = {0x1000, 0x2abc, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000, 0x7010, 0x8000, 0x8010, 0x8040,
                                0x8050, 0x9000, 0x9020, 0xA000, 0xA040, 0xA050, 0xB000, 0x101000};
        const uint32_t want[] = {1, 2, 8, 0x10, 4, 0, 0x20, 0, 0, 0x20, 0x20, 0, 0x40, 0, 0, 0x40, 0, 0, 1};
        for (size_t i = 0; i < sizeof ids / sizeof ids[0]; ++i)
            ok &= C_EquipmentPanel::SlotOfControl(ids[i]) == want[i];
        WM(r.epSlots, ok);
    }

    // --- a squad soldier selected: the scroll reset, the arrows (down on with more than four);
    // none past the squad or before it ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        p.m_beltScroll = p.m_backpackScroll = 3;
        p.SelectSoldier(0);
        ok &= p.m_squadSoldier == &sq0 && p.m_soldierIndex == 0 && p.m_beltScroll == 0 && p.m_backpackScroll == 0 &&
              EpMessages() == std::vector<std::string>({EpMsg(0x111, 0x1000001), EpMsg(0x112, 0x1000001),
                                                        EpMsg(0x113, 0x1000001), EpMsg(0x114, 0x1000001)});
        p.SelectSoldier(1);
        ok &= p.m_squadSoldier == &sq1 && p.m_soldierIndex == 1 &&
              EpMessages() == std::vector<std::string>({EpMsg(0x111, 0x1000001), EpMsg(0x112, 0x1000002),
                                                        EpMsg(0x113, 0x1000001), EpMsg(0x114, 0x1000002)});
        p.SelectSoldier(2);
        ok &= !p.m_squadSoldier && p.m_soldierIndex == -1 && EpMessages().empty();
        p.SelectSoldier(-1);
        ok &= !p.m_squadSoldier && p.m_soldierIndex == -1 && EpMessages().empty();
        // Exactly four: no scrolling.
        sq0.belt.assign(4, EpMake(1, 24, 2));
        sq0.backpackContents.assign(5, EpMake(1, 24, 2));
        p.SelectSoldier(0);
        ok &= EpMessages() == std::vector<std::string>({EpMsg(0x111, 0x1000001), EpMsg(0x112, 0x1000001),
                                                        EpMsg(0x113, 0x1000001), EpMsg(0x114, 0x1000002)});
        sq0.belt.assign(5, EpMake(1, 24, 2));
        sq0.backpackContents.assign(4, EpMake(1, 24, 2));
        p.SelectSoldier(0);
        ok &= EpMessages() == std::vector<std::string>({EpMsg(0x111, 0x1000001), EpMsg(0x112, 0x1000002),
                                                        EpMsg(0x113, 0x1000001), EpMsg(0x114, 0x1000001)});
        sq0.belt.assign(3, EpMake(1, 24, 2));
        sq0.backpackContents.clear();
        // The count of a control's place (no soldier: 0).
        g_epCount = 3;
        C_EquipmentPanel q;
        ok &= q.CountItems(0x8010, EpMake(1, 24, 2)) == 0 && g_gfLog.empty();
        q.m_squadSoldier = &sq0;
        ok &= q.CountItems(0x8010, EpMake(1, 24, 2)) == 3 && q.CountItems(0x3000, EpMake(2, 26, 1)) == 3 &&
              MsLogIs({"count 20 1 24 2", "count 8 2 26 1"}, g_gfLog);
        g_epCount = 0;
        WM(r.epSelect, ok);
    }

    // --- an item shown: its picture and type (none: the texture back, no index); the belt's and
    // the backpack's items their count ("%d ", none: text 0x7D2), ammunition "rounds/count " ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel::ShowItem(0x1000, EpMake(5, 20, 1));
        ok &= EpMessages() == std::vector<std::string>({EpMsg(0x1000, 0x1000038, 0, 0xA20), EpMsg(0x1000, 0x1000009, 20)});
        g_controlMessages.clear();
        C_EquipmentPanel::ShowItem(0x1000, EpMake(5, 20, 1));
        std::vector<std::string> got = EpMessages();
        ok &= got.size() == 2 && got[1] == EpMsg(0x1000, 0x1000009, 20);
        C_EquipmentPanel::ShowItem(0x5000, EpMake(-1, -1, 0));
        ok &= EpMessages() == std::vector<std::string>({EpMsg(0x5000, 0x1000039), EpMsg(0x5000, 0x1000009, 0xffffffff)});
        C_EquipmentPanel::ShowItem(0x8010, EpMake(1, 23, 3));
        ok &= EpMessages() == std::vector<std::string>({EpMsg(0x8010, 0x1000038, 0, 0xA23), EpMsg(0x8010, 0x1000009, 23),
                                                        EpMsg(0x8010, 0x1000014, 0x12, 0, "3 ")});
        C_EquipmentPanel::ShowItem(0xA020, EpMake(1, 24, 3));
        ok &= EpMessages() == std::vector<std::string>({EpMsg(0xA020, 0x1000038, 0, 0xA24), EpMsg(0xA020, 0x1000009, 24),
                                                        EpMsg(0xA020, 0x1000014, 0x12, 0, "24/3 ")});
        C_EquipmentPanel::ShowItem(0xA020, EpMake(1, 24, 0));
        ok &= EpMessages() == std::vector<std::string>({EpMsg(0xA020, 0x1000038, 0, 0xA24), EpMsg(0xA020, 0x1000009, 24),
                                                        EpMsg(0xA020, 0x1000015, 0x12, 0x7d2)});
        C_EquipmentPanel::ShowItem(0x8030, EpMake(-1, -1, 0));
        ok &= EpMessages() == std::vector<std::string>({EpMsg(0x8030, 0x1000039), EpMsg(0x8030, 0x1000009, 0xffffffff),
                                                        EpMsg(0x8030, 0x1000015, 0x12, 0x7d2)});
        // Ammunition of under a round a magazine: the count twice.
        d24.ammo = 0.5f;
        C_EquipmentPanel::ShowItem(0x8030, EpMake(1, 24, 2));
        got = EpMessages();
        ok &= got.size() == 3 && got[2] == EpMsg(0x8030, 0x1000014, 0x12, 0, "2/2 ");
        d24.ammo = 8.0f;
        WM(r.epShowItem, ok);
    }

    // --- the panel refreshed: each place, the backpack's items on / off, the belt's and the
    // backpack's shown items from their scroll (past them: none), then the load ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        p.Refresh();
        ok &= EpMessages().empty();
        S_SquadSoldier s;
        s.head = EpMake(1, 20, 1);
        s.longGun = EpMake(2, 22, 1);
        s.belt = {EpMake(3, 23, 1), EpMake(4, 24, 2), EpMake(5, 25, 1), EpMake(6, 23, 2), EpMake(7, 24, 5)};
        s.backpackContents = {EpMake(8, 25, 4)};
        s.load.backpackMaxWeight = 0.0f;
        p.m_squadSoldier = &s;
        p.m_beltScroll = 1;
        g_epWeightFill = 0.5f;
        g_epBeltFill = 0.25f;
        g_epBackpackFill = 0.0f;
        p.Refresh();
        std::vector<std::string> got = EpMessages();
        const std::vector<std::string> head = {EpMsg(0x1000, 0x1000038, 0, 0xA20), EpMsg(0x1000, 0x1000009, 20),
                                               EpMsg(0x2000, 0x1000039), EpMsg(0x2000, 0x1000009, 0xffffffff),
                                               EpMsg(0x4000, 0x1000038, 0, 0xA22), EpMsg(0x4000, 0x1000009, 22),
                                               EpMsg(0x5000, 0x1000039), EpMsg(0x5000, 0x1000009, 0xffffffff),
                                               EpMsg(0x3000, 0x1000039), EpMsg(0x3000, 0x1000009, 0xffffffff),
                                               EpMsg(0xA010, 0x1000001), EpMsg(0xA020, 0x1000001),
                                               EpMsg(0xA030, 0x1000001), EpMsg(0xA040, 0x1000001),
                                               EpMsg(0x8010, 0x1000038, 0, 0xA24), EpMsg(0x8010, 0x1000009, 24),
                                               EpMsg(0x8010, 0x1000014, 0x12, 0, "16/2 "),
                                               EpMsg(0x8020, 0x1000038, 0, 0xA25), EpMsg(0x8020, 0x1000009, 25),
                                               EpMsg(0x8020, 0x1000014, 0x12, 0, "1 "),
                                               EpMsg(0x8030, 0x1000038, 0, 0xA23), EpMsg(0x8030, 0x1000009, 23),
                                               EpMsg(0x8030, 0x1000014, 0x12, 0, "2 "),
                                               EpMsg(0x8040, 0x1000038, 0, 0xA24), EpMsg(0x8040, 0x1000009, 24),
                                               EpMsg(0x8040, 0x1000014, 0x12, 0, "40/5 "),
                                               EpMsg(0xA010, 0x1000038, 0, 0xA25), EpMsg(0xA010, 0x1000009, 25),
                                               EpMsg(0xA010, 0x1000014, 0x12, 0, "4 "),
                                               EpMsg(0xA020, 0x1000039), EpMsg(0xA020, 0x1000009, 0xffffffff),
                                               EpMsg(0xA020, 0x1000015, 0x12, 0x7d2)};
        ok &= got.size() == head.size() + 6 + 7 && MsLogIs(head, std::vector<std::string>(got.begin(), got.begin() + head.size())) &&
              got.back() == EpMsg(0x123, 0x1000015, 0xb, 0xaa7);
        // (The load's messages follow: UpdateLoad's check.) With a backpack: its items on.
        s.load.backpackMaxWeight = 5.0f;
        p.m_beltScroll = 0;
        p.Refresh();
        got = EpMessages();
        ok &= got.size() > 14 && got[10] == EpMsg(0xA010, 0x1000002) && got[13] == EpMsg(0xA040, 0x1000002) &&
              got[14] == EpMsg(0x8010, 0x1000038, 0, 0xA23);
        WM(r.epRefresh, ok);
    }

    // --- the load: the weight bar (over 1 on the overweight bar when there is one), its text, the
    // belt's bar and percentage, the backpack's (none: 0 and text 0xAA7) ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        p.UpdateLoad();
        ok &= EpMessages().empty();
        S_SquadSoldier s;
        s.load.capacity = 30;
        s.load.weight = 12.25f;
        s.load.backpackMaxWeight = 5.0f;
        p.m_squadSoldier = &s;
        g_epWeightFill = 0.75f;
        g_epBeltFill = 0.375f;
        g_epBackpackFill = 0.5f;
        p.UpdateLoad();
        const std::string weight = CmText("%s: %3.1f / %d Kg", g_texts.Get(0xAB4).c_str(), 12.25, 30);
        ok &= MsLogIs({EpMsg(0x101, 0x1000092, 0, EpFloat(0.75f)), EpMsg(0x102, 0x1000092, 0, 0),
                       EpMsg(0x121, 0x1000014, 0xb, 0, weight.c_str()), EpMsg(0x103, 0x1000092, 0, EpFloat(0.375f)),
                       EpMsg(0x122, 0x1000014, 0xb, 0, "37.5%"), EpMsg(0x104, 0x1000092, 0, EpFloat(0.5f)),
                       EpMsg(0x123, 0x1000014, 0xb, 0, "50.0%")},
                      EpMessages());
        // Over the capacity; no backpack.
        g_epWeightFill = 1.25f;
        s.load.backpackMaxWeight = 0.0f;
        p.UpdateLoad();
        std::vector<std::string> got = EpMessages();
        ok &= got.size() == 7 && got[0] == EpMsg(0x101, 0x1000092, 0, 0) && got[1] == EpMsg(0x102, 0x1000092, 0, EpFloat(1.25f)) &&
              got[5] == EpMsg(0x104, 0x1000092, 0, 0) && got[6] == EpMsg(0x123, 0x1000015, 0xb, 0xaa7);
        // Exactly full: the weight bar; no overweight bar: the weight bar even over.
        g_epWeightFill = 1.0f;
        p.UpdateLoad();
        got = EpMessages();
        ok &= got[0] == EpMsg(0x101, 0x1000092, 0, EpFloat(1.0f));
        g_epWeightFill = 1.5f;
        p.m_overweightBar = 0;
        p.UpdateLoad();
        got = EpMessages();
        ok &= got[0] == EpMsg(0x101, 0x1000092, 0, EpFloat(1.5f)) && got[1] == EpMsg(0, 0x1000092, 0, 0);
        WM(r.epLoad, ok);
    }

    // --- the allowed places highlighted (over the free weight: none, unless ignored; the belt and
    // the backpack with room; no backpack: not its contents; the drop place untouched) ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        S_MrObj *h[8];
        for (int i = 0; i < 8; ++i) {
            h[i] = MsFrame(CmText("h%d", i).c_str());
            p.m_highlights[i] = As<I3D_frame>(h[i]);
        }
        p.ShowAllowed(EpMake(1, 25, 1), false);
        ok &= g_gfLog.empty() || true;
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 25, 1), false);
        ok &= g_gfLog.empty();
        S_SquadSoldier s;
        s.load.backpackMaxWeight = 2.0f;
        p.m_squadSoldier = &s;
        g_epFreeWeight = 10.0f;
        g_epFreeBelt = 4;
        g_epFreeBackpack = 4;
        p.ShowAllowed(EpMake(1, 25, 1), false);
        ok &= MsLogIs({"h0 on 0", "h1 on 0", "h2 on 0", "h3 on 0", "h4 on 1", "h6 on 1", "h7 on 1"}, g_gfLog);
        // Too little room: a size of 2 needs 4.
        g_epFreeBelt = 3;
        g_epFreeBackpack = 3;
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 25, 1), false);
        ok &= MsLogIs({"h0 on 0", "h1 on 0", "h2 on 0", "h3 on 0", "h4 on 1", "h6 on 0", "h7 on 0"}, g_gfLog);
        // A size of 0 needs 1; no backpack: not its contents.
        d25.m_inventorySize = 0;
        g_epFreeBelt = 1;
        g_epFreeBackpack = 1;
        s.load.backpackMaxWeight = 0.0f;
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 25, 1), false);
        ok &= MsLogIs({"h0 on 0", "h1 on 0", "h2 on 0", "h3 on 0", "h4 on 1", "h6 on 1", "h7 on 0"}, g_gfLog);
        g_epFreeBelt = 0;
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 25, 1), false);
        ok &= g_gfLog[5] == "h6 on 0";
        s.load.backpackMaxWeight = 0.2f;
        g_epFreeBackpack = 4;
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 25, 1), false);
        ok &= g_gfLog[6] == "h7 on 1";
        s.load.backpackMaxWeight = 0.0f;
        d25.m_inventorySize = 2;
        // Too heavy: none, unless the weight is ignored; exactly the free weight: allowed.
        g_epFreeWeight = 3.0f;
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 22, 1), false);
        ok &= MsLogIs({"h0 on 0", "h1 on 0", "h2 on 0", "h3 on 0", "h4 on 0", "h6 on 0", "h7 on 0"}, g_gfLog);
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 22, 1), true);
        ok &= MsLogIs({"h0 on 0", "h1 on 0", "h2 on 0", "h3 on 1", "h4 on 1", "h6 on 0", "h7 on 0"}, g_gfLog);
        g_epFreeWeight = 4.0f;
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 22, 1), false);
        ok &= g_gfLog[3] == "h3 on 1";
        g_gfLog.clear();
        p.ShowAllowed(EpMake(1, 20, 1), false);
        p.ShowAllowed(EpMake(1, 21, 1), false);
        p.ShowAllowed(EpMake(1, 26, 1), false);
        p.ShowAllowed(EpMake(1, 99, 1), false);
        ok &= g_gfLog[0] == "h0 on 1" && g_gfLog[8] == "h1 on 1" && g_gfLog[16] == "h2 on 1" &&
              std::count(g_gfLog.end() - 7, g_gfLog.end(), std::string("h7 on 0")) == 1 &&
              std::find(g_gfLog.end() - 7, g_gfLog.end(), "h4 on 1") == g_gfLog.end();
        g_epFreeWeight = 100.0f;
        g_epFreeBelt = 12;
        g_epFreeBackpack = 10;
        g_gfLog.clear();
        WM(r.epAllowed, ok);
    }

    // --- the scroll arrows: a click moves the shown items (within them), the arrows on or off, then
    // the refresh; no soldier: nothing, false ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        ok &= !C_EquipmentPanel::OnScroll(&p, nullptr, nullptr, 0, 0) && EpMessages().empty();
        S_SquadSoldier s;
        s.belt.assign(6, EpMake(1, 23, 1));
        s.backpackContents.assign(7, EpMake(1, 25, 1));
        p.m_squadSoldier = &s;
        g_epWeightFill = g_epBeltFill = g_epBackpackFill = 0.0f;
        C_EpControl beltUp(0x111), beltDown(0x112), packUp(0x113), packDown(0x114), other(0x999);
        const auto arrows = [&](std::vector<std::string> got) { return std::vector<std::string>(got.begin(), got.begin() + 4); };
        ok &= C_EquipmentPanel::OnScroll(&p, nullptr, &beltDown, 0, 0) && p.m_beltScroll == 1 &&
              arrows(EpMessages()) == std::vector<std::string>({EpMsg(0x111, 0x1000002), EpMsg(0x112, 0x1000002),
                                                                EpMsg(0x113, 0x1000001), EpMsg(0x114, 0x1000002)});
        C_EquipmentPanel::OnScroll(&p, nullptr, &beltDown, 0, 0);
        ok &= p.m_beltScroll == 2 && arrows(EpMessages())[1] == EpMsg(0x112, 0x1000001);
        C_EquipmentPanel::OnScroll(&p, nullptr, &beltDown, 0, 0);
        ok &= p.m_beltScroll == 2;
        EpMessages();
        C_EquipmentPanel::OnScroll(&p, nullptr, &beltUp, 0, 0);
        EpMessages();
        C_EquipmentPanel::OnScroll(&p, nullptr, &beltUp, 0, 0);
        ok &= p.m_beltScroll == 0 && arrows(EpMessages())[0] == EpMsg(0x111, 0x1000001);
        C_EquipmentPanel::OnScroll(&p, nullptr, &beltUp, 0, 0);
        ok &= p.m_beltScroll == 0;
        C_EquipmentPanel::OnScroll(&p, nullptr, &packDown, 0, 0);
        C_EquipmentPanel::OnScroll(&p, nullptr, &packDown, 0, 0);
        C_EquipmentPanel::OnScroll(&p, nullptr, &packDown, 0, 0);
        EpMessages();
        C_EquipmentPanel::OnScroll(&p, nullptr, &other, 0, 0);
        ok &= p.m_backpackScroll == 3 && arrows(EpMessages()) == std::vector<std::string>({EpMsg(0x111, 0x1000001),
                                                                                         EpMsg(0x112, 0x1000002),
                                                                                         EpMsg(0x113, 0x1000002),
                                                                                         EpMsg(0x114, 0x1000001)});
        C_EquipmentPanel::OnScroll(&p, nullptr, &packUp, 0, 0);
        ok &= p.m_backpackScroll == 2 && arrows(EpMessages())[3] == EpMsg(0x114, 0x1000002);
        C_EquipmentPanel::OnScroll(&p, nullptr, &packUp, 0, 0);
        C_EquipmentPanel::OnScroll(&p, nullptr, &packUp, 0, 0);
        ok &= p.m_backpackScroll == 0;
        C_EquipmentPanel::OnScroll(&p, nullptr, &packUp, 0, 0);
        ok &= p.m_backpackScroll == 0;
        EpMessages();
        p.m_beltScroll = 2;
        s.belt.resize(3);
        p.m_backpackScroll = 1;
        s.backpackContents.resize(5);
        C_EquipmentPanel::OnScroll(&p, nullptr, nullptr, 0, 0);
        ok &= p.m_beltScroll == 0 && p.m_backpackScroll == 1 &&
              arrows(EpMessages()) == std::vector<std::string>({EpMsg(0x111, 0x1000001), EpMsg(0x112, 0x1000001),
                                                                EpMsg(0x113, 0x1000002), EpMsg(0x114, 0x1000001)});
        s.belt.resize(6);
        s.backpackContents.resize(7);
        // Items taken away: the scroll back within them; four or fewer: none, the arrows off.
        p.m_beltScroll = 2;
        s.belt.resize(5);
        p.m_backpackScroll = 3;
        s.backpackContents.resize(4);
        C_EquipmentPanel::OnScroll(&p, nullptr, nullptr, 0, 0);
        ok &= p.m_beltScroll == 1 && p.m_backpackScroll == 0 &&
              arrows(EpMessages()) == std::vector<std::string>({EpMsg(0x111, 0x1000002), EpMsg(0x112, 0x1000001),
                                                                EpMsg(0x113, 0x1000001), EpMsg(0x114, 0x1000001)});
        // Then the refresh (the head's first).
        g_controlMessages.clear();
        C_EquipmentPanel::OnScroll(&p, nullptr, nullptr, 0, 0);
        std::vector<std::string> got = EpMessages();
        ok &= got.size() > 5 && got[4] == EpMsg(0x1000, 0x1000039);
        WM(r.epScroll, ok);
    }

    // --- items taken away: the squad soldier's count, the menu soldier's as many; by a control's
    // place (none: nothing), then the arrows ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        S_SoldierItem item = EpMake(4, 23, 2);
        ok &= p.RemoveItem(0x20, item) == 0 && p.RemoveItemAt(0x8010, item) == 0 && g_gfLog.empty();
        S_SquadSoldier s;
        p.m_squadSoldier = &s;
        g_epRemoved = {2};
        ok &= p.RemoveItem(0x20, item) == 2 && MsLogIs({"remove 20 4 23 2"}, g_gfLog);
        // With a soldier: its holsters' items.
        C_MenuSoldier soldier;
        MsWorld(soldier);
        S_MrObj *f0 = MsFrame("hf0"), *f1 = MsFrame("hf1");
        soldier.m_holsters[0].frameCount = 2;
        soldier.m_holsters[0].frames[0] = As<I3D_frame>(f0);
        soldier.m_holsters[0].frames[1] = As<I3D_frame>(f1);
        soldier.m_holsters[0].itemCount = 5;
        g_msHolsterSlots = {{23, 0}};
        p.m_soldier = &soldier;
        g_epRemoved = {3};
        g_gfLog.clear();
        ok &= p.RemoveItem(0x20, item) == 3 && soldier.m_holsters[0].itemCount == 2;
        g_epRemoved = {0};
        ok &= p.RemoveItem(0x20, item) == 0 && soldier.m_holsters[0].itemCount == 2;
        // By a control: the place, then the arrows (the refresh after).
        g_epRemoved = {1};
        g_gfLog.clear();
        g_controlMessages.clear();
        ok &= p.RemoveItemAt(0x8020, item) == 1 && g_gfLog[0] == "remove 20 4 23 2" && soldier.m_holsters[0].itemCount == 1;
        std::vector<std::string> got = EpMessages();
        ok &= got.size() > 4 && got[0] == EpMsg(0x111, 0x1000001);
        g_gfLog.clear();
        ok &= p.RemoveItemAt(0x6000, item) == 0 && g_gfLog.empty() && EpMessages().empty();
        p.m_soldier = nullptr;
        g_msHolsterSlots.clear();
        delete soldier.m_pose;
        soldier.m_pose = nullptr;
        WM(r.epRemove, ok);
    }

    // --- items put: what was displaced; the menu soldier's items: the new ones (none displaced),
    // the more of the same type, or the old ones away first (not the uniform's) ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        S_SquadSoldier s;
        p.m_squadSoldier = &s;
        C_MenuSoldier soldier;
        MsWorld(soldier);
        S_MrObj *f[4];
        for (int i = 0; i < 4; ++i) {
            f[i] = MsFrame(CmText("pf%d", i).c_str());
            soldier.m_holsters[i < 2 ? 0 : 1].frames[i % 2] = As<I3D_frame>(f[i]);
        }
        soldier.m_holsters[0].frameCount = soldier.m_holsters[1].frameCount = 2;
        g_msHolsterSlots = {{23, 0}, {24, 1}};
        p.m_soldier = &soldier;
        g_gfLog.clear();
        ok &= EpIs(p.PutItem(0x20, EpMake(1, 23, 2)), EpMake(-1, -1, 0)) && g_gfLog[0] == "put 20 1 23 2" &&
              soldier.m_holsters[0].itemCount == 2;
        g_epDisplaced = {EpMake(1, 23, 1)};
        ok &= EpIs(p.PutItem(0x20, EpMake(1, 23, 3)), EpMake(1, 23, 1)) && soldier.m_holsters[0].itemCount == 4;
        g_epDisplaced = {EpMake(1, 23, 5)};
        ok &= EpIs(p.PutItem(0x20, EpMake(1, 23, 3)), EpMake(1, 23, 5)) && soldier.m_holsters[0].itemCount == 4;
        soldier.m_holsters[1].itemCount = 2;
        g_epDisplaced = {EpMake(7, 24, 2)};
        ok &= EpIs(p.PutItem(0x20, EpMake(1, 23, 1)), EpMake(7, 24, 2)) && soldier.m_holsters[0].itemCount == 5 &&
              soldier.m_holsters[1].itemCount == 0;
        // The uniform: the old not taken off (the new model replaces it).
        S_MsModel body = MsModel("body", "mp_CI01.i3d");
        g_mrMade.clear();
        g_mrMade[FRAME_MODEL] = {body.model};
        g_epDisplaced = {EpMake(8, 20, 1)};
        g_gfLog.clear();
        ok &= EpIs(p.PutItem(0x2, EpMake(1, 21, 1)), EpMake(8, 20, 1)) && !MrHas("pose delete") == false &&
              soldier.m_model == As<I3D_model>(body.model);
        // No soldier: only the squad's.
        p.m_soldier = nullptr;
        g_gfLog.clear();
        ok &= EpIs(p.PutItem(0x40, EpMake(2, 25, 1)), EpMake(-1, -1, 0)) && MsLogIs({"put 40 2 25 1"}, g_gfLog);
        g_msHolsterSlots.clear();
        delete soldier.m_pose;
        soldier.m_pose = nullptr;
        WM(r.epPut, ok);
    }

    // --- put checked: no soldier: the item back; no definition: none; not fitting: back; a single
    // place taken (not forced): back; else put, then the arrows ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        ok &= EpIs(p.PutItemChecked(0x1, EpMake(1, 20, 1), false), EpMake(1, 20, 1)) && g_gfLog.empty();
        S_SquadSoldier s;
        p.m_squadSoldier = &s;
        ok &= EpIs(p.PutItemChecked(0x1, EpMake(1, 99, 1), false), EpMake(-1, -1, 0)) && g_gfLog.empty();
        ok &= EpIs(p.PutItemChecked(0x2, EpMake(1, 20, 1), false), EpMake(1, 20, 1)) && g_gfLog.empty();
        g_epDisplaced = {EpMake(3, 20, 1)};
        ok &= EpIs(p.PutItemChecked(0x1, EpMake(1, 20, 1), false), EpMake(3, 20, 1)) && MsLogIs({"put 1 1 20 1"}, g_gfLog);
        std::vector<std::string> got = EpMessages();
        ok &= got.size() > 4 && got[0] == EpMsg(0x111, 0x1000001);
        // Taken places.
        s.head = EpMake(5, 20, 1);
        s.uniform = EpMake(5, 21, 1);
        s.hands = EpMake(5, 23, 1);
        s.backpack = EpMake(5, 26, 1);
        s.longGun = EpMake(5, 22, 1);
        g_gfLog.clear();
        ok &= EpIs(p.PutItemChecked(0x1, EpMake(1, 20, 1), false), EpMake(1, 20, 1)) &&
              EpIs(p.PutItemChecked(0x2, EpMake(1, 21, 1), false), EpMake(1, 21, 1)) &&
              EpIs(p.PutItemChecked(0x4, EpMake(1, 23, 1), false), EpMake(1, 23, 1)) &&
              EpIs(p.PutItemChecked(0x8, EpMake(1, 26, 1), false), EpMake(1, 26, 1)) &&
              EpIs(p.PutItemChecked(0x10, EpMake(1, 22, 1), false), EpMake(1, 22, 1)) && g_gfLog.empty();
        // Forced, or the belt (no single place): put.
        ok &= EpIs(p.PutItemChecked(0x4, EpMake(1, 23, 1), true), EpMake(-1, -1, 0)) &&
              EpIs(p.PutItemChecked(0x20, EpMake(1, 23, 1), false), EpMake(-1, -1, 0)) &&
              MsLogIs({"put 4 1 23 1", "put 20 1 23 1"}, g_gfLog);
        // By a control: forced.
        g_gfLog.clear();
        ok &= EpIs(p.PutItemAt(0x1000, EpMake(1, 20, 1)), EpMake(-1, -1, 0)) && MsLogIs({"put 1 1 20 1"}, g_gfLog);
        g_controlMessages.clear();
        WM(r.epChecked, ok);
    }

    // --- dropped where it fits first: head, uniform, the belt (a different type given back), hands
    // (not item 261), backpack, long gun, the backpack's contents; as many as the free weight
    // carries (none: back unless forced), the rest added to what is given back ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        S_SoldierItem item = EpMake(1, 20, 1);
        ok &= EpIs(p.DropItem(item, false), item) && g_gfLog.empty();
        S_SquadSoldier s;
        p.m_squadSoldier = &s;
        item = EpMake(1, 99, 1);
        ok &= EpIs(p.DropItem(item, false), EpMake(1, 99, 1)) && g_gfLog.empty();
        item = EpMake(1, 20, 1);
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 0)) && MsLogIs({"put 1 1 20 1"}, g_gfLog);
        std::vector<std::string> arrows = EpMessages();
        ok &= arrows.size() > 4 && arrows[0] == EpMsg(0x111, 0x1000001);
        g_gfLog.clear();
        item = EpMake(1, 21, 1);
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 0)) && MsLogIs({"put 2 1 21 1"}, g_gfLog);
        // The belt: what is left of the same type goes on to the hands.
        g_gfLog.clear();
        item = EpMake(1, 23, 3);
        g_epDisplaced = {EpMake(1, 23, 1)};
        g_controlMessages.clear();
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 0)) && MsLogIs({"put 20 1 23 3", "put 4 1 23 1"}, g_gfLog) &&
              item.present == 1;
        arrows = EpMessages();
        ok &= arrows.size() > 8 && arrows[0] == EpMsg(0x111, 0x1000001) &&
              std::count(arrows.begin(), arrows.end(), EpMsg(0x111, 0x1000001)) == 2;
        // One of them only (a belt item: the belt).
        g_gfLog.clear();
        item = EpMake(1, 24, 1);
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 0)) && MsLogIs({"put 20 1 24 1"}, g_gfLog);
        // None of them: nowhere.
        g_gfLog.clear();
        item = EpMake(1, 24, 0);
        ok &= EpIs(p.DropItem(item, false), EpMake(1, 24, 0)) && g_gfLog.empty();
        // Another type off the belt: given back.
        g_gfLog.clear();
        item = EpMake(1, 23, 3);
        g_epDisplaced = {EpMake(9, 24, 4)};
        ok &= EpIs(p.DropItem(item, false), EpMake(9, 24, 4)) && MsLogIs({"put 20 1 23 3"}, g_gfLog);
        // Hands taken: the long gun; item 261 not in the hands.
        s.hands = EpMake(5, 23, 1);
        g_gfLog.clear();
        item = EpMake(1, 22, 1);
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 0)) && MsLogIs({"put 10 1 22 1"}, g_gfLog);
        s.hands = EpMake(-1, -1, 0);
        g_gfLog.clear();
        item = EpMake(1, 261, 1);
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 0)) && MsLogIs({"put 10 1 261 1"}, g_gfLog);
        g_gfLog.clear();
        item = EpMake(1, 26, 1);
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 0)) && MsLogIs({"put 8 1 26 1"}, g_gfLog);
        // Everything taken: back (the contents for what fits them).
        s.head = EpMake(5, 20, 1);
        s.uniform = EpMake(5, 21, 1);
        s.hands = EpMake(5, 23, 1);
        s.backpack = EpMake(5, 26, 1);
        s.longGun = EpMake(5, 22, 1);
        g_gfLog.clear();
        item = EpMake(1, 22, 1);
        ok &= EpIs(p.DropItem(item, false), EpMake(1, 22, 1)) && g_gfLog.empty();
        item = EpMake(1, 22, 1);
        ok &= EpIs(p.DropItem(item, true), EpMake(-1, -1, 0)) && MsLogIs({"put 4 1 22 1"}, g_gfLog);
        g_gfLog.clear();
        item = EpMake(1, 25, 2);
        g_epDisplaced = {EpMake(1, 25, 1), EpMake(1, 25, 1)};
        ok &= EpIs(p.DropItem(item, false), EpMake(1, 25, 1)) && MsLogIs({"put 20 1 25 2", "put 40 1 25 1"}, g_gfLog);
        g_gfLog.clear();
        item = EpMake(1, 25, 2);
        g_epDisplaced = {EpMake(1, 25, 0), EpMake(3, 24, 1)};
        ok &= EpIs(p.DropItem(item, false), EpMake(1, 25, 0)) && MsLogIs({"put 20 1 25 2"}, g_gfLog);
        g_gfLog.clear();
        item = EpMake(1, 25, 2);
        g_epDisplaced = {EpMake(1, 25, 2), EpMake(1, 25, 1)};
        g_controlMessages.clear();
        ok &= EpIs(p.DropItem(item, false), EpMake(1, 25, 1)) && MsLogIs({"put 20 1 25 2", "put 40 1 25 2"}, g_gfLog);
        arrows = EpMessages();
        ok &= std::count(arrows.begin(), arrows.end(), EpMsg(0x111, 0x1000001)) == 2;
        g_gfLog.clear();
        item = EpMake(1, 25, 2);
        g_epDisplaced = {EpMake(1, 25, 2), EpMake(3, 24, 1)};
        ok &= EpIs(p.DropItem(item, false), EpMake(3, 24, 1)) && MsLogIs({"put 20 1 25 2", "put 40 1 25 2"}, g_gfLog);
        // The weight: 3 of 5 carried, the other 2 added to what is left.
        s = S_SquadSoldier();
        g_epFreeWeight = 1.5f;
        g_gfLog.clear();
        item = EpMake(1, 25, 5);
        g_epDisplaced = {EpMake(1, 25, 1)};
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 2)) && MsLogIs({"put 20 1 25 3", "put 4 1 25 1"}, g_gfLog);
        // Exactly one carried.
        g_epFreeWeight = 1.0f;
        g_gfLog.clear();
        item = EpMake(1, 20, 4);
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 3)) && MsLogIs({"put 1 1 20 1"}, g_gfLog);
        // Two carried, nowhere to go: all given back.
        s.head = EpMake(5, 20, 1);
        g_epFreeWeight = 2.0f;
        g_gfLog.clear();
        item = EpMake(1, 20, 5);
        ok &= EpIs(p.DropItem(item, false), EpMake(1, 20, 5)) && g_gfLog.empty();
        s.head = EpMake(-1, -1, 0);
        // None carried: back, or (forced) all of them.
        g_epFreeWeight = 0.25f;
        g_gfLog.clear();
        item = EpMake(1, 25, 5);
        ok &= EpIs(p.DropItem(item, false), EpMake(1, 25, 5)) && g_gfLog.empty();
        item = EpMake(1, 20, 2);
        ok &= EpIs(p.DropItem(item, true), EpMake(-1, -1, 0)) && MsLogIs({"put 1 1 20 2"}, g_gfLog);
        // Exactly the free weight: all.
        g_epFreeWeight = 2.0f;
        g_gfLog.clear();
        item = EpMake(1, 20, 2);
        ok &= EpIs(p.DropItem(item, false), EpMake(-1, -1, 0)) && MsLogIs({"put 1 1 20 2"}, g_gfLog);
        g_epFreeWeight = 100.0f;
        g_epDisplaced.clear();
        g_controlMessages.clear();
        WM(r.epDrop, ok);
    }

    // --- moved between controls: the same place: nothing; taken from the source (none: nothing);
    // to the drop place: given back; else put, the displaced back to the source, then anywhere ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        S_SquadSoldier s;
        p.m_squadSoldier = &s;
        S_SoldierItem item = EpMake(1, 23, 1);
        ok &= EpIs(p.MoveItem(item, 0x8010, 0x8020), EpMake(-1, -1, 0)) && g_gfLog.empty();
        ok &= EpIs(p.MoveItem(item, 0x8010, 0x5000), EpMake(-1, -1, 0)) && MsLogIs({"remove 20 1 23 1"}, g_gfLog);
        g_gfLog.clear();
        g_epRemoved = {1};
        ok &= EpIs(p.MoveItem(item, 0x8010, 0x6000), item) && MsLogIs({"remove 20 1 23 1"}, g_gfLog);
        g_gfLog.clear();
        g_epRemoved = {1};
        ok &= EpIs(p.MoveItem(item, 0x8010, 0x5000), EpMake(-1, -1, 0)) && MsLogIs({"remove 20 1 23 1", "put 4 1 23 1"}, g_gfLog);
        // The displaced pistol back to the belt.
        g_gfLog.clear();
        g_epRemoved = {1};
        g_epDisplaced = {EpMake(6, 23, 1)};
        ok &= EpIs(p.MoveItem(item, 0x8010, 0x5000), EpMake(-1, -1, 0)) &&
              MsLogIs({"remove 20 1 23 1", "put 4 1 23 1", "put 20 6 23 1"}, g_gfLog);
        // ... which displaces something too: dropped anywhere (the head).
        g_gfLog.clear();
        g_epRemoved = {1};
        g_epDisplaced = {EpMake(6, 23, 1), EpMake(7, 20, 1)};
        ok &= EpIs(p.MoveItem(item, 0x8010, 0x5000), EpMake(-1, -1, 0)) &&
              MsLogIs({"remove 20 1 23 1", "put 4 1 23 1", "put 20 6 23 1", "put 1 7 20 1"}, g_gfLog);
        g_epDisplaced.clear();
        g_controlMessages.clear();
        WM(r.epMove, ok);
    }

    // --- given back to a stock: the backpack's contents (the last first) and the backpack; all of
    // it (the soldier undressed); no soldier or stock: nothing ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        EpIds(p);
        C_ItemStock stock;
        p.ReturnBackpack(&stock);
        p.ReturnItems(&stock);
        ok &= g_gfLog.empty();
        S_SquadSoldier s;
        s.backpackContents = {EpMake(1, 25, 2), EpMake(2, 24, 3)};
        s.backpack = EpMake(3, 26, 1);
        p.m_squadSoldier = &s;
        p.ReturnBackpack(nullptr);
        p.ReturnItems(nullptr);
        ok &= g_gfLog.empty();
        g_epRemoved = {3, 2, 1};
        p.ReturnBackpack(&stock);
        ok &= MsLogIs({"remove 40 2 24 3", "stock add 24 3", "remove 40 1 25 2", "stock add 25 2", "remove 8 3 26 1",
                       "stock add 26 1"},
                      g_gfLog);
        std::vector<std::string> got = EpMessages();
        ok &= !got.empty() && got[0] == EpMsg(0x1000, 0x1000039);
        g_gfLog.clear();
        p.ReturnItems(&stock);
        ok &= MsLogIs({"return items"}, g_gfLog) && !EpMessages().empty();
        // The soldier's items reset (the naked model).
        C_MenuSoldier soldier;
        MsWorld(soldier);
        S_MsModel naked = MsModel("naked", "mp_Na01");
        g_mrMade.clear();
        g_mrMade[FRAME_MODEL] = {naked.model};
        p.m_soldier = &soldier;
        g_gfLog.clear();
        p.ReturnItems(&stock);
        ok &= g_gfLog[0] == "return items" && soldier.m_model == As<I3D_model>(naked.model);
        p.m_soldier = nullptr;
        delete soldier.m_pose;
        soldier.m_pose = nullptr;
        g_controlMessages.clear();
        WM(r.epReturn, ok);
    }

    // --- the highlights' frames found (as visuals) and hidden ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        S_MrObj *model = MsFrame("scr"), *a = MsFrame("ha"), *b = MsFrame("hb");
        g_msChildren[{model, "selhead"}] = a;
        g_msChildren[{model, "selbackpack"}] = b;
        g_gfLog.clear();
        p.FindHighlights(As<I3D_frame>(model));
        ok &= MsLogIs({"scr 48 selhead 1", "ha on 0", "scr 48 seluniform 1", "scr 48 selbck 1", "scr 48 selshoulder 1",
                       "scr 48 selhands 1", "scr 48 seldrop 1", "scr 48 selholsters 1", "scr 48 selbackpack 1",
                       "hb on 0"},
                      g_gfLog) &&
              p.m_highlights[0] == As<I3D_frame>(a) && p.m_highlights[7] == As<I3D_frame>(b) && !p.m_highlights[3];
        WM(r.epHighlights, ok);
    }

    // --- a large weapon: animated, bigger than one square, not behaviour 6; in the hands or on the
    // shoulder (not with none of it) ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        ok &= C_EquipmentPanel::IsLargeWeapon(22) && !C_EquipmentPanel::IsLargeWeapon(23) &&
              !C_EquipmentPanel::IsLargeWeapon(25) && !C_EquipmentPanel::IsLargeWeapon(21) &&
              !C_EquipmentPanel::IsLargeWeapon(99) && !C_EquipmentPanel::IsLargeWeapon(-1);
        d23.m_inventorySize = 2;
        ok &= C_EquipmentPanel::IsLargeWeapon(23);
        d23.m_inventorySize = 1;
        C_EquipmentPanel p;
        ok &= !p.HasLargeWeapon();
        S_SquadSoldier s;
        p.m_squadSoldier = &s;
        ok &= !p.HasLargeWeapon();
        s.hands = EpMake(1, 22, 1);
        ok &= p.HasLargeWeapon();
        s.hands = EpMake(1, 22, 0);
        ok &= !p.HasLargeWeapon();
        s.longGun = EpMake(1, 22, 1);
        ok &= p.HasLargeWeapon();
        s.hands = EpMake(1, 23, 1);
        s.longGun = EpMake(1, 23, 1);
        ok &= !p.HasLargeWeapon();
        s.longGun = EpMake(1, 22, 0);
        ok &= !p.HasLargeWeapon();
        WM(r.epLarge, ok);
    }

    // --- the menu soldier: made in the visual's screen rectangle (hidden), ticked, drawn, dressed in
    // the squad soldier's items (the belt's and the backpack's the last first), its face; deleted
    // (the diving gear released) ---
    {
        bool ok = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        C_EquipmentPanel p;
        p.TickSoldier(5);
        p.RenderSoldier();
        p.UpdateFace();
        p.DressSoldier();
        p.CreateSoldier(nullptr);
        p.DeleteSoldier();
        ok &= g_gfLog.empty() && !p.m_soldier;
        // Made: 800 x 600 (menu_run_test.inc's IGraph), the bound (0.25 .. 0.75, -0.6 .. -0.15).
        const int igraphSlots[] = {0x18 / 4, 0x1C / 4};
        void *oldIgraph[2] = {g_igraphVtable[igraphSlots[0]], g_igraphVtable[igraphSlots[1]]};
        g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&MrScrnX);
        g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&MrScrnY);
        S_MrObj *area = MsFrame("area");
        auto *bound = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(area) + 0x170);
        bound[0] = 0.25f;
        bound[1] = -0.6f;
        bound[4] = 0.75f;
        bound[5] = -0.15f;
        reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(area) + 0x1D0)[0] = 0x40000;
        g_mrMade.clear();
        I3D_scene *const oldGameScene = GetGame()->m_scene;
        GetGame()->m_scene = nullptr;
        g_gfLog.clear();
        p.CreateSoldier(As<I3D_visual>(area));
        GetGame()->m_scene = oldGameScene;
        // (No scene made: the soldier's Setup stops at once.)
        ok &= p.m_soldier && MsLogIs({"create 13", "area on 0"}, g_gfLog);
        delete p.m_soldier;
        p.m_soldier = nullptr;
        S_MrObj *scene = MrObj("ss", g_msSceneVt), *sector = MrObj("ssec", g_msSectorVt);
        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(scene) + 0x1F8) = sector;
        g_mrMade[FRAME_SCENE] = {scene};
        g_gfLog.clear();
        p.CreateSoldier(As<I3D_visual>(area));
        ok &= g_gfLog.size() > 2 && g_gfLog[1] == "ss 148 200 120 600 480";
        delete p.m_soldier;
        p.m_soldier = nullptr;
        g_igraphVtable[igraphSlots[0]] = oldIgraph[0];
        g_igraphVtable[igraphSlots[1]] = oldIgraph[1];
        // Ticked and drawn.
        C_MenuSoldier *soldier = new C_MenuSoldier();
        MsWorld(*soldier);
        p.m_soldier = soldier;
        g_gfLog.clear();
        p.TickSoldier(7);
        ok &= g_gfLog.size() == 4 && g_gfLog[3] == "ss 20 7";
        g_gfLog.clear();
        p.RenderSoldier();
        ok &= MrHas("ss 78 0");
        // Dressed.
        S_SquadSoldier s;
        s.head = EpMake(1, 20, 1);
        s.uniform = EpMake(6, 21, 1);
        s.hands = EpMake(7, 23, 1);
        s.backpack = EpMake(8, 26, 1);
        s.longGun = EpMake(9, 22, 1);
        s.belt = {EpMake(2, 23, 2), EpMake(3, 24, 1)};
        s.backpackContents = {EpMake(4, 25, 1)};
        p.m_squadSoldier = &s;
        S_MrObj *f[3];
        for (int i = 0; i < 3; ++i) {
            f[i] = MsFrame(CmText("df%d", i).c_str());
            soldier->m_holsters[i].frameCount = 1;
            soldier->m_holsters[i].frames[0] = As<I3D_frame>(f[i]);
        }
        g_msHolsterSlots = {{23, 0}, {24, 1}, {25, 2}};
        g_mrClone = As<I3D_frame>(MsFrame("headgear"));
        S_MsModel body = MsModel("body", "mp_CI01.i3d");
        g_mrMade.clear();
        g_mrMade[FRAME_MODEL] = {body.model};
        g_gfLog.clear();
        p.DressSoldier();
        ok &= soldier->m_headTypeId == 20 && soldier->m_holsters[0].itemCount == 2 && soldier->m_holsters[1].itemCount == 1 &&
              soldier->m_holsters[2].itemCount == 0 && soldier->m_model == As<I3D_model>(body.model) &&
              soldier->m_handsTypeId == 23 && soldier->m_backpackTypeId == 26 && soldier->m_longGunTypeId == 22;
        {
            // The head gear first: cloned before the uniform's model is made.
            const auto clone = std::find_if(g_gfLog.begin(), g_gfLog.end(), [](const std::string &l) { return l.rfind("clone tree", 0) == 0; });
            const auto made = std::find(g_gfLog.begin(), g_gfLog.end(), "create 9");
            ok &= clone < made && made != g_gfLog.end();
        }
        // (The holsters of the new model are none: put them back for the belt's order.)
        for (int i = 0; i < 3; ++i) {
            soldier->m_holsters[i].frameCount = 1;
            soldier->m_holsters[i].frames[0] = As<I3D_frame>(f[i]);
            soldier->m_holsters[i].itemCount = 0;
        }
        s.head = s.uniform = s.hands = s.backpack = s.longGun = EpMake(-1, -1, 0);
        g_gfLog.clear();
        p.DressSoldier();
        // The belt's last first.
        const auto at = [](const char *line) { return std::find(g_gfLog.begin(), g_gfLog.end(), line) - g_gfLog.begin(); };
        ok &= at("df1 on 1") < at("df0 on 1") && at("df0 on 1") < static_cast<long>(g_gfLog.size());
        // No soldier: nothing.
        p.m_soldier = nullptr;
        p.DressSoldier();
        p.m_soldier = soldier;
        // An item of none: nothing (not even at the head).
        soldier->m_headModel = As<I3D_frame>(MsFrame("hm1"));
        g_gfLog.clear();
        p.AddToSoldier(MENU_SOLDIER_HEAD, EpMake(1, -1, 1));
        ok &= g_gfLog.empty() && soldier->m_headTypeId == 20;
        soldier->m_headModel = nullptr;
        g_gfLog.clear();
        p.AddToSoldier(MENU_SOLDIER_HOLSTERS, EpMake(1, -1, 3));
        p.AddToSoldier(MENU_SOLDIER_HOLSTERS, EpMake(1, 23, 0));
        ok &= g_gfLog.empty() && soldier->m_holsters[0].itemCount == 2;
        // The face of the squad soldier's profile (none: nothing).
        // (Zeroed memory: the profile's constructor is not linked.)
        alignas(16) static uint8_t profMemory[2][sizeof(S_SoldierProfile)];
        std::memset(profMemory, 0, sizeof profMemory);
        S_SoldierProfile &prof0 = *reinterpret_cast<S_SoldierProfile *>(profMemory[0]);
        S_SoldierProfile &prof1 = *reinterpret_cast<S_SoldierProfile *>(profMemory[1]);
        prof1.faceTexture = "e_f007.bmp";
        g_epProfiles = {&prof0, &prof1};
        s.profileIndex = 1;
        g_gfLog.clear();
        p.UpdateFace();
        s.profileIndex = 0;
        p.UpdateFace();
        s.profileIndex = -1;
        p.UpdateFace();
        ok &= MsLogIs({"face body e_f007.bmp", "face body "}, g_gfLog);
        prof1.faceTexture = C_str();
        // Deleted: released, its diving gear too.
        S_MrObj *gear = MsFrame("gear");
        soldier->m_divingGear[1] = As<I3D_frame>(gear);
        delete soldier->m_pose;
        soldier->m_pose = nullptr;
        soldier->m_model = nullptr;
        for (auto &anim : soldier->m_anims)
            anim = nullptr;
        g_gfLog.clear();
        p.DeleteSoldier();
        ok &= !p.m_soldier && MsLogIs({"ss 0", "gear 0"}, g_gfLog);
        g_msHolsterSlots.clear();
        g_mrClone = nullptr;
        g_epProfiles.clear();
        WM(r.epSoldier, ok);
    }

    for (int i = 0; i < 3; ++i)
        g_driverVtable[driverSlots[i]] = oldDriver[i];
    g_cacheFill = oldFill;
    g_msNamer = nullptr;
    *reinterpret_cast<void ***>(GetGEffects()) = oldEffectsVt;
    g_gameProfile.m_missionSquad = oldSquad;
    g_gameProfile.m_humanStore = oldStore;
    g_gfItemDefs = oldDefs;
    g_epAllowed.clear();
    g_epDisplaced.clear();
    g_epRemoved.clear();
    g_msChildren.clear();
    g_controlMessages.clear();
    g_gfLog.clear();
    std::printf("equipmentpanel: epSlots %d epSelect %d epShowItem %d epRefresh %d epLoad %d epAllowed %d epScroll %d "
                "epRemove %d epPut %d epChecked %d epDrop %d epMove %d epReturn %d epHighlights %d epLarge %d "
                "epSoldier %d\n",
                r.epSlots, r.epSelect, r.epShowItem, r.epRefresh, r.epLoad, r.epAllowed, r.epScroll, r.epRemove, r.epPut,
                r.epChecked, r.epDrop, r.epMove, r.epReturn, r.epHighlights, r.epLarge, r.epSoldier);
}
