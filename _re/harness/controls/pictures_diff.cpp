// Human part 61a: the item pictures (game/controls.cpp lines 5161-5296) against the original's. MinGW; build_pictures.bat;
// runs pictures_diff.exe ("pictures_diff: N checks, 0 failures"). The kit (controls_kit.inc, controls_tree.inc,
// controls_kinds.inc) fakes what the controls reach outside themselves on both sides and compares the trees, the message
// queue, the engine's fakes and the calls on them. The trees are planned: item pictures under a plain control (their
// parent: told of counts unchecked by the original), each with its two pictures (the original uses both unchecked), now
// and then the two swapped or a child added. Each tree has its item table (part 55's): a few definitions of each record
// type on both sides (the original's table at 0x0098b030) and their pictures (cached: GetImage loads none). Without its
// second picture (the original's crash) the port runs alone.
// HD2_PICTURES_ROUNDS: rounds per kind of call (default 20,000; HD2_PICTURES_QUICK: 4,000); HD2_PICTURES_CASE: one only;
// HD2_PICTURES_FIRST_CHANCE: faults under the original's exception frames shown.
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

// The item pictures: the group's focus, +0x70 the item, +0x74 its count, +0x78 the second item, +0x7C its count, +0x80
// the picture, +0x84 the second.
int RandomCount() { return P(0.3) ? Pick({0, 1, -1, 2}) : I(-50, 1000); }
int RandomItemId();
void RandomPictures(C_Control *c)
{
    auto *n = static_cast<C_ItemPictures *>(c);
    RandomGroup(c);
    n->m_item = RandomItemId();
    n->m_count = RandomCount();
    n->m_second = RandomItemId();
    n->m_secondCount = RandomCount();
    n->m_picture = ChildOf<C_PictureControl>(c, 0);
    n->m_secondPicture = ChildOf<C_PictureControl>(c, 1);
}
void PicturesToOrig(C_Control *c, uint8_t *o)
{
    auto *n = static_cast<C_ItemPictures *>(c);
    GroupToOrig(c, o);
    OF<int>(o, 0x70) = n->m_item;
    OF<int>(o, 0x74) = n->m_count;
    OF<int>(o, 0x78) = n->m_second;
    OF<int>(o, 0x7C) = n->m_secondCount;
    OF<uint8_t *>(o, 0x80) = OrigOf(n->m_picture);
    OF<uint8_t *>(o, 0x84) = OrigOf(n->m_secondPicture);
}
std::string PicturesDiffers(C_Control *c, const uint8_t *o)
{
    auto *n = static_cast<C_ItemPictures *>(c);
    const std::pair<uint32_t, const void *> parts[] = {{0x80, n->m_picture}, {0x84, n->m_secondPicture}};
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
    {"pictures", 0x00814a90, 0x88, Make<C_ItemPictures>, RandomPictures, PicturesToOrig, PicturesDiffers},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_TITLE = 1, K_PICTURE = 2, K_PICTURES = 3;

// ---- the item table: definitions on both sides (part 55's) ----
const uint32_t O_ITEMS = 0x0098b030;   // the original's g_itemsModule.m_items
const uint32_t O_AMMO_VT = 0x008173e4, O_ANIMATED_VT = 0x00817408, O_STATIC_VT = 0x0081742c;
std::vector<int> g_itemIds;               // the ids with a definition
C_ItemDef *g_portItems[MAX_ITEM_TYPES];
std::vector<void *> g_itemBlocks;         // the original's blocks
std::vector<C_ItemDef *> g_portDefs;
uint32_t *OrigItems() { return reinterpret_cast<uint32_t *>(uintptr_t(O_ITEMS)); }
C_ItemDef *PortItem(int id) { return id >= 0 && id < MAX_ITEM_TYPES ? g_portItems[id] : nullptr; }

int RandomItemId()
{
    if (!g_itemIds.empty() && P(0.7))
        return g_itemIds[size_t(I(0, int(g_itemIds.size()) - 1))];
    return Pick({-1, 0, 499, 500, 1000, -1000, 7, 250});
}

uint8_t *OrigBlock(size_t bytes)
{
    void *p = std::calloc(1, bytes);
    g_itemBlocks.push_back(p);
    return static_cast<uint8_t *>(p);
}
void ClearItems()
{
    for (C_ItemDef *d : g_portDefs)
        delete d;
    for (void *p : g_itemBlocks)
        std::free(p);
    g_portDefs.clear();
    g_itemBlocks.clear();
    std::memset(OrigItems(), 0, MAX_ITEM_TYPES * 4);
    std::memset(g_portItems, 0, sizeof g_portItems);
    g_itemIds.clear();
}
// (Uniforms more often: the headgear's; 15 the uniform, its neighbours.)
const int BEHAVIOURS[] = {0, 1, 4, 14, 15, 15, 15, 15, 16, 23, -1};
void RandomItems()
{
    ClearItems();
    for (int k = I(1, 8); k > 0; --k) {
        const int id = P(0.2) ? Pick({0, 1, 499, 498}) : I(0, 499);
        if (!PortItem(id))
            g_itemIds.push_back(id);
    }
    for (int id : g_itemIds) {
        const int type = I(0, 2);
        uint8_t *o = OrigBlock(0x80);
        C_ItemDef *d;
        if (type == 0) {
            auto *a = new C_AmmoItemDef(id);
            OF<uint32_t>(o, 0) = O_AMMO_VT;
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
        d->m_image = P(0.8) ? reinterpret_cast<IImage *>(g_arena.textures[I(0, 3)]) : nullptr;
        OF<int>(o, 0x04) = d->m_recordType;
        OF<int>(o, 0x08) = id;
        OF<void *>(o, 0x28) = d->m_image;
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
void PlanPictures(int parent)
{
    const int at = PlanAdd(parent, K_PICTURES, P(0.6) ? -1 : I(0, 15) & ~3);
    std::vector<std::pair<int, int>> parts = {{K_PICTURE, 1}, {K_PICTURE, 2}};
    if (P(0.1))
        std::swap(parts[0], parts[1]);
    if (P(0.15))
        parts.insert(parts.begin() + I(0, 2), {Pick({K_CONTROL, K_TITLE, K_PICTURE}), P(0.5) ? -1 : I(0, 15)});
    for (const auto &p : parts)
        PlanAdd(at, p.first, p.second);
}
// (Under a plain control always: the original tells the parent of the counts unchecked.)
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    const int root = PlanAdd(-1, K_CONTROL, -1);
    for (int k = I(1, 2); k > 0; --k)
        PlanPictures(root);
    if (P(0.3))
        PlanAdd(root, Pick({K_CONTROL, K_TITLE, K_PICTURE}), -1);
    return g_plan;
}

// Create's frames: the second picture's mostly.
void CreateFrames(size_t node, int fi)
{
    if (fi < 0 || g_nodeKinds[node] != &KINDS[K_PICTURES])
        return;
    int16_t &child = g_arena.child[fi][ChildName("ammo*")];
    if (child < 0 && P(0.8))
        child = static_cast<int16_t>(I(0, 15));
}
// (A texture named as Create's face group's, "X_SQUARE.BMP", in half the rounds: the pictures' FIND_FACE_GROUP finds it.)
void AfterTree()
{
    g_cursorMade = true;
    if (P(0.5))
        std::strcpy(g_arena.names[I(0, 3)], "X_SQUARE.BMP");
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
                              CONTROL_MSG_SET_ITEM,     CONTROL_MSG_SET_ITEM_COUNT,  0x1000000,                 0x2000001};
size_t PickPictures()
{
    const std::vector<size_t> nodes = NodesOf(K_PICTURES);
    return nodes[size_t(I(0, int(nodes.size()) - 1))];
}
uint32_t PicturesTarget(size_t i)
{
    if (P(0.2))
        return 0xFFFFFFFF;
    if (P(0.6))
        return (g_portNodes[i]->m_id & ~0xFu) | uint32_t(I(0, 15));
    return RandomId();
}
S_ControlMessage OwnMessage(size_t i)
{
    S_ControlMessage m;
    m.target = PicturesTarget(i);
    m.id = Pick(OWN_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    if (P(0.3) && !m.text.m_rep)
        m.text = P(0.5) ? "hello" : "";
    return m;
}
// The pictures' own messages sent straight to them.
void TestOwn()
{
    const size_t i = PickPictures();
    S_ControlMessage m = OwnMessage(i);
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

// The pictures' input slots with ids naming their parts (any last 4 bits), FindChild, Refresh shown or not.
void TestInput()
{
    const size_t i = PickPictures();
    C_Control *p = g_portNodes[i];
    const int id = int(PicturesTarget(i));
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
    const int pick = I(0, n + 1);
    int rp = 0, ro = 0;
    if (pick < n) {
        Diff([&] { rp = (p->*slots[pick].port)(id); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], id); });
    } else if (pick == n) {
        Diff([&] { rp = p->FindChild(uint32_t(id)); }, [&] { ro = Slot<IntFn>(i, 0x00)(g_origNodes[i], id); });
    } else {
        p->m_shown = P(0.6);
        Diff([&] { p->Refresh(); }, [&] { Slot<VoidFn>(i, 0x18)(g_origNodes[i]); });
    }
    CHECK(rp == ro, "input slot %d %x: %d / %d", pick, unsigned(id), ro, rp);
}

// Without the second picture (no "ammo*": the original's crash in all but the first's paths): the port alone, its calls
// those of the first picture and the parent only.
void TestNoSecond()
{
    const size_t i = PickPictures();
    auto *p = static_cast<C_ItemPictures *>(g_portNodes[i]);
    C_Control *second = p->m_secondPicture;
    p->m_secondPicture = nullptr;
    // (Its log names: a call on the second picture would name its node.)
    const std::string secondName = PtrName(second);
    g_log.clear();
    if (P(0.2)) {
        p->m_shown = P(0.8);
        p->Refresh();
    } else {
        // (Its own codes to its own id: one naming the second's id goes to it as the group's child, still there.)
        S_ControlMessage m = OwnMessage(i);
        static const uint32_t codes[] = {CONTROL_MSG_SET_ITEM,         CONTROL_MSG_SET_ITEM,     CONTROL_MSG_SET_ITEM_COUNT,
                                         CONTROL_MSG_SET_SECOND_COUNT, CONTROL_MSG_ADD_CALLBACK, CONTROL_MSG_REMOVE_CALLBACK,
                                         CONTROL_MSG_REFRESH};
        m.id = Pick(codes);
        m.target = P(0.5) ? 0xFFFFFFFF : p->m_id & ~0xFu;
        MessageValues(m);
        if (P(0.5))
            p->HandleMessage(&m);
        else
            p->HandleMessageCopy(m);
    }
    bool named = false;
    // (The call's object: the line's second word, "copy c3 ..."; its box's "box(c3) ...". Values print in hex: "c3" too.)
    for (const std::string &line : g_log) {
        const size_t space = line.find(' ');
        const std::string second = space == std::string::npos ? "" : line.substr(space + 1, line.find(' ', space + 1) - space - 1);
        named = named || second == secondName || line.rfind("box(" + secondName + ")", 0) == 0;
    }
    CHECK(!named, "no second picture: a call on it: %s", Joined(g_log).c_str());
    CHECK(p->m_secondPicture == nullptr, "no second picture: one set");
    g_log.clear();
    p->m_secondPicture = static_cast<C_PictureControl *>(second);
}

// The constructor: inline in the original's factory (0x0065d470, type 0x1c: +0x6C -1, +0x70 -1, +0x74 0, +0x78 -1, +0x7C
// 0, +0x80 0, +0x84 0).
void TestConstruct()
{
    auto *scene = reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000));
    const uint32_t id = U32();
    C_ItemPictures pictures(scene, id, nullptr);
    CHECK(pictures.m_focus == -1 && pictures.m_item == -1 && pictures.m_count == 0 && pictures.m_second == -1 &&
              pictures.m_secondCount == 0 && !pictures.m_picture && !pictures.m_secondPicture && pictures.m_id == id &&
              pictures.m_scene == scene && pictures.m_takesInput,
          "the item pictures' fields at construction");
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
               CONTROL_MSG_SET_ITEM,   CONTROL_MSG_SET_ITEM_COUNT, CONTROL_MSG_SET_SECOND_COUNT, CONTROL_MSG_ADD_CALLBACK,
               CONTROL_MSG_REMOVE_CALLBACK, 0x1000000, 0x2000001, 0};
    // (SET_ITEM sets the pictures' textures: shown or hidden, notifying.)
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_SET_ITEM};
    g_tickKinds = {K_PICTURES, K_PICTURE, K_TITLE, K_CONTROL};
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
    if (std::getenv("HD2_PICTURES_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("pictures_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_PICTURES_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_PICTURES_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_PICTURES_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate},       {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Input", TestInput},         {"NoSecond", TestNoSecond},
        {"Construct", TestConstruct},
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
    std::printf("pictures_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
