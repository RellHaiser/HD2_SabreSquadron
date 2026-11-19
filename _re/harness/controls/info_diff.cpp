// Human part 55: the item information (game/controls.cpp lines 3623-3793) against the original's. MinGW; build_info.bat;
// runs info_diff.exe ("info_diff: N checks, 0 failures"). The kit (controls_kit.inc, controls_tree.inc, controls_kinds.inc)
// fakes what the controls reach outside themselves on both sides and compares the trees, the message queue, the engine's
// fakes and the calls on them. The trees are planned: item informations under a plain control (their parent: told of
// counts unchecked by the original), each with its two pictures and its stats' title (all used unchecked by the
// original), now and then two swapped or a child added. Each tree has its item table: a few definitions of each record
// type on both sides (the original's table at 0x0098b030), their components (projectile weapons, firearms, scopes) and
// pictures (cached: GetImage loads none).
// HD2_INFO_ROUNDS: rounds per kind of call (default 20,000; HD2_INFO_QUICK: 4,000); HD2_INFO_CASE: one only;
// HD2_INFO_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/control.h"
#include "game/controls.h"
#include "game/cursor.h"
#include "game/font.h"
#include "game/game_texts.h"
#include "game/item_defs.h"
#include "game/key_bindings.h"
#include "game/paged_text_box.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

#include "controls_kit.inc"

// ---- the item definitions' members item_defs.cpp has (not linked: the fields set by the test) ----
C_ItemDef::C_ItemDef(E_ItemRecordType recordType, int id) : m_recordType(recordType), m_id(id) {}
C_ItemDef::~C_ItemDef() {}
void C_ItemDef::Save(int *, void *) { Unexpected("C_ItemDef::Save"); }
void C_ItemDef::Load(int *, const void *) { Unexpected("C_ItemDef::Load"); }
void C_AmmoItemDef::Save(int *, void *) { Unexpected("C_AmmoItemDef::Save"); }
void C_AmmoItemDef::Load(int *, const void *) { Unexpected("C_AmmoItemDef::Load"); }
C_AnimatedItemDef::~C_AnimatedItemDef() {}
void C_AnimatedItemDef::Save(int *, void *) { Unexpected("C_AnimatedItemDef::Save"); }
void C_AnimatedItemDef::Load(int *, const void *) { Unexpected("C_AnimatedItemDef::Load"); }
void C_StaticItemDef::Save(int *, void *) { Unexpected("C_StaticItemDef::Save"); }
void C_StaticItemDef::Load(int *, const void *) { Unexpected("C_StaticItemDef::Load"); }
void C_ProjectileWeaponComp::Save(int *, void *) { Unexpected("C_ProjectileWeaponComp::Save"); }
void C_ProjectileWeaponComp::Load(int, const void *) { Unexpected("C_ProjectileWeaponComp::Load"); }
void C_FirearmComp::Save(int *, void *) { Unexpected("C_FirearmComp::Save"); }
void C_FirearmComp::Load(int, const void *) { Unexpected("C_FirearmComp::Load"); }
void C_ScopeComp::Save(int *, void *) { Unexpected("C_ScopeComp::Save"); }
void C_ScopeComp::Load(int, const void *) { Unexpected("C_ScopeComp::Load"); }

namespace {
#include "controls_kinds.inc"
#include "controls_tree.inc"

// ---- this part's kinds ----
uint8_t *OrigOf(const C_Control *c) { return c ? ToOrigNode(c) : nullptr; }
template <class T> T *ChildOf(C_Control *c, int n = 0)
{
    for (C_Control *k : c->m_children)
        if (auto *t = dynamic_cast<T *>(k))
            if (n-- == 0)
                return t;
    return nullptr;
}

// The picture (part 50's): the box, +0x70 its texture, +0x74 its face group.
void RandomPicture(C_Control *c)
{
    auto *p = static_cast<C_PictureControl *>(c);
    RandomText(c);
    p->m_texture = P(0.2) ? nullptr : reinterpret_cast<I3D_texture *>(g_arena.textures[I(0, 3)]);
    p->m_faceGroup = reinterpret_cast<I3D_face_group *>(g_arena.groups[I(0, 3)]);
}
void PictureToOrig(C_Control *c, uint8_t *o)
{
    auto *p = static_cast<C_PictureControl *>(c);
    TextToOrig(c, o);
    OF<void *>(o, 0x70) = p->m_texture;
    OF<void *>(o, 0x74) = p->m_faceGroup;
}
std::string PictureDiffers(C_Control *c, const uint8_t *o)
{
    auto *p = static_cast<C_PictureControl *>(c);
    if (OF<void *>(o, 0x70) != p->m_texture || OF<void *>(o, 0x74) != p->m_faceGroup)
        return "texture " + PtrName(OF<void *>(o, 0x70)) + " group " + PtrName(OF<void *>(o, 0x74)) + " / " + PtrName(p->m_texture) + " " +
               PtrName(p->m_faceGroup);
    return TextDiffers(c, o);
}

// The item information: the group's focus, +0x70 the item, +0x74 its count, +0x78 the second item, +0x7C its count,
// +0x80 the picture, +0x84 the second, +0x88 the stats' title.
int RandomCount() { return P(0.3) ? Pick({0, 1, -1, 2}) : I(-50, 1000); }
int RandomItemId();
void RandomInfo(C_Control *c)
{
    auto *n = static_cast<C_ItemInfo *>(c);
    RandomGroup(c);
    n->m_item = RandomItemId();
    n->m_count = RandomCount();
    n->m_second = RandomItemId();
    n->m_secondCount = RandomCount();
    n->m_picture = ChildOf<C_PictureControl>(c, 0);
    n->m_secondPicture = ChildOf<C_PictureControl>(c, 1);
    n->m_stats = ChildOf<C_TitleControl>(c, 0);
}
void InfoToOrig(C_Control *c, uint8_t *o)
{
    auto *n = static_cast<C_ItemInfo *>(c);
    GroupToOrig(c, o);
    OF<int>(o, 0x70) = n->m_item;
    OF<int>(o, 0x74) = n->m_count;
    OF<int>(o, 0x78) = n->m_second;
    OF<int>(o, 0x7C) = n->m_secondCount;
    OF<uint8_t *>(o, 0x80) = OrigOf(n->m_picture);
    OF<uint8_t *>(o, 0x84) = OrigOf(n->m_secondPicture);
    OF<uint8_t *>(o, 0x88) = OrigOf(n->m_stats);
}
std::string InfoDiffers(C_Control *c, const uint8_t *o)
{
    auto *n = static_cast<C_ItemInfo *>(c);
    const std::pair<uint32_t, const void *> parts[] = {{0x80, n->m_picture}, {0x84, n->m_secondPicture}, {0x88, n->m_stats}};
    for (const auto &f : parts)
        if (PtrName(OF<void *>(o, f.first)) != PtrName(f.second))
            return "part +" + std::to_string(f.first) + " " + PtrName(OF<void *>(o, f.first)) + " / " + PtrName(f.second);
    if (OF<int>(o, 0x70) != n->m_item || OF<int>(o, 0x74) != n->m_count || OF<int>(o, 0x78) != n->m_second || OF<int>(o, 0x7C) != n->m_secondCount)
        return "items " + std::to_string(OF<int>(o, 0x70)) + " " + std::to_string(OF<int>(o, 0x74)) + " " + std::to_string(OF<int>(o, 0x78)) + " " +
               std::to_string(OF<int>(o, 0x7C)) + " / " + std::to_string(n->m_item) + " " + std::to_string(n->m_count) + " " +
               std::to_string(n->m_second) + " " + std::to_string(n->m_secondCount);
    return GroupDiffers(c, o);
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"title", 0x00813cc0, 0x70, Make<C_TitleControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"picture", 0x00813ff0, 0x78, Make<C_PictureControl>, RandomPicture, PictureToOrig, PictureDiffers, O_BOX},
    {"info", 0x00814b18, 0x8C, Make<C_ItemInfo>, RandomInfo, InfoToOrig, InfoDiffers},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_TITLE = 1, K_PICTURE = 2, K_INFO = 3;

// ---- the item table: definitions on both sides ----
const uint32_t O_ITEMS = 0x0098b030;   // the original's g_itemsModule.m_items
const uint32_t O_AMMO_VT = 0x008173e4, O_ANIMATED_VT = 0x00817408, O_STATIC_VT = 0x0081742c;
const uint32_t O_PROJECTILE_VT = 0x00817348, O_FIREARM_VT = 0x00817314, O_SCOPE_VT = 0x00817304;
std::vector<int> g_itemIds;               // the ids with a definition
C_ItemDef *g_portItems[MAX_ITEM_TYPES];
std::vector<void *> g_itemBlocks;         // the port's definitions and components and the original's blocks
std::vector<C_ItemDef *> g_portDefs;
std::vector<C_ItemComponent *> g_portComps;
uint32_t *OrigItems() { return reinterpret_cast<uint32_t *>(uintptr_t(O_ITEMS)); }
C_ItemDef *PortItem(int id) { return id >= 0 && id < MAX_ITEM_TYPES ? g_portItems[id] : nullptr; }

int RandomItemId()
{
    if (!g_itemIds.empty() && P(0.7))
        return g_itemIds[size_t(I(0, int(g_itemIds.size()) - 1))];
    return Pick({-1, 0, 499, 500, 1000, -1000, 7, 250});
}
float RandomFloat()
{
    return P(0.3) ? Pick({0.0f, 1.0f, 0.5f, 7.92f, 30.0f, -1.0f, 0.005f, 0.004999f, 1e6f, 2.675f}) : F(-5, 1000);
}

uint8_t *OrigBlock(size_t bytes)
{
    void *p = std::calloc(1, bytes);
    g_itemBlocks.push_back(p);
    return static_cast<uint8_t *>(p);
}
// A component on both sides: a projectile weapon, a firearm (their muzzle velocities), a scope (its aim mode and field of
// view factor).
void MakeComponent(int which, C_ItemComponent *&port, uint8_t *&orig)
{
    orig = OrigBlock(0x80);
    if (which == 0) {
        auto *c = new C_ProjectileWeaponComp();
        c->m_muzzleVelocity = RandomFloat();
        port = c;
        OF<uint32_t>(orig, 0) = O_PROJECTILE_VT;
        OF<float>(orig, 0x38) = c->m_muzzleVelocity;
    } else if (which == 1) {
        auto *c = new C_FirearmComp();
        c->m_muzzleVelocity = RandomFloat();
        port = c;
        OF<uint32_t>(orig, 0) = O_FIREARM_VT;
        OF<float>(orig, 0x34) = c->m_muzzleVelocity;
    } else {
        auto *c = new C_ScopeComp();
        // (The reticles' last and the binoculars' often: the zoom's limit.)
        c->m_aimMode = P(0.3) ? Pick({3, 4}) : P(0.6) ? I(0, 3) : I(-1, 6);
        c->m_aimedFovFactor = P(0.2) ? Pick({0.0f, 1.0f, 0.2f, 0.1f, 3.14159f}) : F(0.02f, 1.5f);
        port = c;
        OF<uint32_t>(orig, 0) = O_SCOPE_VT;
        OF<int>(orig, 0x0C) = c->m_aimMode;
        OF<float>(orig, 0x10) = c->m_aimedFovFactor;
    }
    OF<int>(orig, 0x08) = port->m_kind;
    g_portComps.push_back(port);
}
void ClearItems()
{
    for (C_ItemDef *d : g_portDefs)
        delete d;
    for (C_ItemComponent *c : g_portComps)
        delete c;
    for (void *p : g_itemBlocks)
        std::free(p);
    g_portDefs.clear();
    g_portComps.clear();
    g_itemBlocks.clear();
    std::memset(OrigItems(), 0, MAX_ITEM_TYPES * 4);
    std::memset(g_portItems, 0, sizeof g_portItems);
    g_itemIds.clear();
}
// (Rifles and uniforms more often: the zoom's and the headgear's.)
const int BEHAVIOURS[] = {0, 1, 2, 3, 4, 4, 4, 4, 5, 6, 8, 9, 12, 13, 14, 15, 15, 16, 23, -1};
void RandomItems()
{
    ClearItems();
    for (int k = I(1, 8); k > 0; --k) {
        const int id = P(0.2) ? Pick({0, 1, 499, 498}) : I(0, 499);
        if (!PortItem(id))
            g_itemIds.push_back(id);
    }
    std::vector<uint8_t *> origDefs(MAX_ITEM_TYPES);
    for (int id : g_itemIds) {
        const int type = I(0, 2);
        uint8_t *o = OrigBlock(0x80);
        C_ItemDef *d;
        if (type == 0) {
            auto *a = new C_AmmoItemDef(id);
            a->m_ammoAmount = P(0.3) ? Pick({30.0f, 8.0f, 0.0f, 1.9f, -2.5f, 2147483520.0f}) : F(-10, 500);
            OF<uint32_t>(o, 0) = O_AMMO_VT;
            OF<float>(o, 0x54) = a->m_ammoAmount;
            d = a;
        } else if (type == 1) {
            auto *a = new C_AnimatedItemDef(id);
            a->m_ammoItemId = RandomItemId();
            a->m_carryMode = I(0, 6);
            a->m_behaviour = Pick(BEHAVIOURS);
            OF<uint32_t>(o, 0) = O_ANIMATED_VT;
            OF<int>(o, 0x54) = a->m_ammoItemId;
            OF<int>(o, 0x58) = a->m_carryMode;
            OF<int>(o, 0x5C) = a->m_behaviour;
            d = a;
        } else {
            auto *s = new C_StaticItemDef(id);
            s->m_carryMode = I(0, 6);
            s->m_behaviour = Pick(BEHAVIOURS);
            s->m_headgearItemId = RandomItemId();
            OF<uint32_t>(o, 0) = O_STATIC_VT;
            OF<int>(o, 0x54) = s->m_carryMode;
            OF<int>(o, 0x58) = s->m_behaviour;
            OF<int>(o, 0x60) = s->m_headgearItemId;
            d = s;
        }
        d->m_textId = P(0.8) ? I(1000, 3000) : Pick({0, -1, 2730});
        d->m_weight = RandomFloat();
        d->m_image = P(0.8) ? reinterpret_cast<IImage *>(g_arena.textures[I(0, 3)]) : nullptr;
        OF<int>(o, 0x04) = d->m_recordType;
        OF<int>(o, 0x08) = id;
        OF<int>(o, 0x14) = d->m_textId;
        OF<float>(o, 0x18) = d->m_weight;
        OF<void *>(o, 0x28) = d->m_image;
        uint8_t *oc = nullptr;
        if (P(0.75)) {
            MakeComponent(P(0.1) ? 2 : I(0, 1), d->m_component, oc);
            OF<uint8_t *>(o, 0x48) = oc;
        }
        if (P(0.6)) {
            MakeComponent(P(0.8) ? 2 : I(0, 1), d->m_secondaryComponent, oc);
            OF<uint8_t *>(o, 0x4C) = oc;
        }
        g_portDefs.push_back(d);
        g_portItems[id] = d;
        OrigItems()[id] = uint32_t(uintptr_t(o));
    }
}

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
int PlanAdd(int parent, int kind, int bits, bool child = true)
{
    g_plan.push_back({parent, kind, bits, child});
    return int(g_plan.size()) - 1;
}
void PlanInfo(int parent)
{
    const int at = PlanAdd(parent, K_INFO, P(0.6) ? -1 : I(0, 15) & ~3);
    std::vector<std::pair<int, int>> parts = {{K_PICTURE, 1}, {K_PICTURE, 2}, {K_TITLE, 0}};
    if (P(0.1)) {
        const size_t a = size_t(I(0, 2)), b = size_t(I(0, 2));
        std::swap(parts[a], parts[b]);
    }
    if (P(0.15))
        parts.push_back({Pick({K_CONTROL, K_TITLE, K_PICTURE}), P(0.5) ? -1 : I(0, 15)});
    for (const auto &p : parts)
        PlanAdd(at, p.first, p.second);
}
// (Under a plain control always: the original tells the parent of the counts unchecked.)
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    const int root = PlanAdd(-1, K_CONTROL, -1);
    for (int k = I(1, 2); k > 0; --k)
        PlanInfo(root);
    if (P(0.3))
        PlanAdd(root, Pick({K_CONTROL, K_TITLE, K_PICTURE}), -1);
    return g_plan;
}

// Create's frames: the second picture's and the stats' now and then.
void CreateFrames(size_t node, int fi)
{
    if (fi < 0 || g_nodeKinds[node] != &KINDS[K_INFO])
        return;
    for (const char *name : {"ammo*", "text_info*"}) {
        int16_t &child = g_arena.child[fi][ChildName(name)];
        if (child < 0 && P(0.8))
            child = static_cast<int16_t>(I(0, 15));
    }
}
void AfterTree()
{
    g_cursorMade = true;
}

std::vector<size_t> NodesOf(int kind)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[kind])
            out.push_back(i);
    return out;
}

// ---- the round's state and the messages' values ----
void PickRound()
{
    g_box.editRun = P(0.8);
    g_box.hoverAt = P(0.7);
    g_box.hovered = int16_t(I(-1, 6));
    g_box.selection = int16_t(I(-1, 6));
    g_box.scrollRange = int16_t(P(0.4) ? 0 : I(1, 20));
    RandomItems();
}

const char *const TEXTS[] = {"Thompson", "", "12", "Colt"};
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_CALLBACK:
    case CONTROL_MSG_REMOVE_CALLBACK:
        // (A function always: the original calls a callback without checking it.)
        m.value = RandomNotification();
        m.value2 = RandomObject();
        m.value3 = g_portCbs[I(0, 2)];
        break;
    case CONTROL_MSG_SET_ITEM:
        m.value = uint32_t(RandomItemId());
        break;
    case CONTROL_MSG_SET_ITEM_COUNT:
    case CONTROL_MSG_SET_SECOND_COUNT:
        m.value = uint32_t(RandomCount());
        break;
    case CONTROL_MSG_ADD_TEXT_ID:
    case CONTROL_MSG_SET_TEXT_ID:
        m.value2 = uint32_t(Pick({2002, 1, 77, 0}));
        break;
    case CONTROL_MSG_ADD_TEXT:
    case CONTROL_MSG_SET_TEXT:
        m.text = Pick(TEXTS);
        break;
    }
}

// ---- the tests ----
const uint32_t OWN_CODES[] = {CONTROL_MSG_SET_ITEM,     CONTROL_MSG_SET_ITEM,       CONTROL_MSG_SET_ITEM_COUNT, CONTROL_MSG_SET_SECOND_COUNT,
                              CONTROL_MSG_ADD_CALLBACK, CONTROL_MSG_REMOVE_CALLBACK, CONTROL_MSG_REFRESH,       CONTROL_MSG_SET_TEXT,
                              CONTROL_MSG_GET_VALUE,    CONTROL_MSG_SET_VALUE,       CONTROL_MSG_DEACTIVATE,    CONTROL_MSG_HIDE,
                              0x1000000,                0x2000001};
size_t PickInfo()
{
    const std::vector<size_t> infos = NodesOf(K_INFO);
    return infos[size_t(I(0, int(infos.size()) - 1))];
}
uint32_t InfoTarget(size_t i)
{
    if (P(0.2))
        return 0xFFFFFFFF;
    if (P(0.6))
        return (g_portNodes[i]->m_id & ~0xFu) | uint32_t(I(0, 15));
    return RandomId();
}
// The information's own messages sent straight to it.
void TestOwn()
{
    const size_t i = PickInfo();
    S_ControlMessage m;
    m.target = InfoTarget(i);
    m.id = Pick(OWN_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    if (P(0.3) && !m.text.m_rep)
        m.text = P(0.5) ? "hello" : "";
    g_messageBefore = m;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    const bool copy = P(0.2);
    Diff([&] { rp = copy ? g_portNodes[i]->HandleMessageCopy(m) : g_portNodes[i]->HandleMessage(&m); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, copy ? 0x74 : 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "message %x: %d / %d", g_messageBefore.id, ro, rp);
    if (!copy) {
        const std::string why = MessageDiffers(m, mo);
        CHECK(why.empty(), "the message after: %s", why.c_str());
    }
}

// The information's input slots with ids naming its parts (any last 4 bits).
void TestInput()
{
    const size_t i = PickInfo();
    C_Control *p = g_portNodes[i];
    const int id = int(InfoTarget(i));
    if (P(0.5))
        static_cast<C_ControlGroup *>(p)->m_focus = I(-1, int(p->m_children.size()));
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter}, {0x34, &C_Control::OnLeave},   {0x38, &C_Control::OnClick},
        {0x48, &C_Control::OnPress}, {0x40, &C_Control::OnInput05},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    const int pick = I(0, n);
    int rp = 0, ro = 0;
    if (pick < n)
        Diff([&] { rp = (p->*slots[pick].port)(id); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], id); });
    else
        Diff([&] { rp = p->FindChild(uint32_t(id)); }, [&] { ro = Slot<IntFn>(i, 0x00)(g_origNodes[i], id); });
    CHECK(rp == ro, "input slot %d %x: %d / %d", pick, unsigned(id), ro, rp);
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_arenaNames = 46;   // (its names: the list group's after them)
    g_treePlan = TreePlan;
    g_itemDef = PortItem;
    g_codes = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE,  CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_INDEX,
               CONTROL_MSG_GET_INDEX,  CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE,    CONTROL_MSG_REFRESH,    CONTROL_MSG_SET_TEXT,
               CONTROL_MSG_SET_TEXT_ID, CONTROL_MSG_ADD_TEXT, CONTROL_MSG_ADD_TEXT_ID,  CONTROL_MSG_CLEAR,      CONTROL_MSG_SET_MARGIN,
               CONTROL_MSG_SET_ITEM_COUNT, CONTROL_MSG_SET_SECOND_COUNT, CONTROL_MSG_ADD_CALLBACK, CONTROL_MSG_REMOVE_CALLBACK,
               0x1000000,              0x2000001,             0};
    // (SET_ITEM sets the pictures' textures: shown or hidden, notifying.)
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_SET_ITEM};
    g_tickKinds = {K_INFO, K_PICTURE, K_TITLE, K_CONTROL};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    g_afterTree = AfterTree;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_INFO_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("info_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_INFO_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_INFO_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_INFO_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Input", TestInput},
    };
    for (const S_Case &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round) {
            PickRound();
            RandomTree();
            c.run();
        }
    }
    std::printf("info_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
