// Part 4 of the items harness: the human's side of the items (game/human_items.cpp) on the real item system, built by
// build_human.bat (items_test.cpp with HUMAN_ITEMS_TEST: the fakes of C_Human::RemoveItem / OnHandsChanged /
// OnUniformChanged are left out, these tests run instead of parts 1-3). The controller, the temporary actors, the
// supply boxes, the zones, the sight module and the effects are faked (logged).

// ============================================================ fakes
namespace {
std::map<const C_Human *, int> g_teams;                 // GetTeam (default: the allies)
std::set<const C_Human *> g_unconscious;
int g_dropActorId = 100;                                // what the temporary actors' DropItem answers
std::map<int, C_Actor *> g_tempActors;                  // FindActor
std::map<int, C_SightTarget *> g_sightTargets;
bool g_requestResult = true;                            // the controller's RequestMoveItem / RequestTransferItem
S_vector g_viewDir(0.0f, 0.0f, 1.0f);
bool g_spectating;
std::string V(const S_vector &v)
{
    char s[96];
    std::snprintf(s, sizeof s, "%g %g %g", v.x, v.y, v.z);
    return s;
}
// The item `uid` of `human` and its container, as the test's own search (not the port's lookups).
C_Item *OwnFind(C_Human *human, int uid, int *container)
{
    for (int c = 0; c < ITEM_CONTAINER_COUNT; ++c) {
        C_ItemContainer *box = human->m_containers[c];
        if (!box)
            continue;
        if (box->GetKind() == 0) {
            if (C_Item *item = box->GetItem(); item && item->GetUid() == uid) {
                *container = c;
                return item;
            }
            continue;
        }
        for (int i = 0; i < box->GetUsedSlotCount(); ++i)
            for (C_Item *item : box->GetSlots()[i])
                if (item->GetUid() == uid) {
                    *container = c;
                    return item;
                }
    }
    return nullptr;
}
}  // namespace

namespace {
std::vector<bool> g_teamPendingAsked;                   // GetTeam's argument, each call
}
int C_Human::GetTeam(bool pending) const
{
    g_teamPendingAsked.push_back(pending);
    return g_teams.count(this) ? g_teams[this] : TEAM_ALLIES;
}
bool C_Human::IsUnconscious() const { return g_unconscious.count(this) != 0; }
void C_Human::IssueCommand(const S_HumanCommand &c)
{
    Log("command " + std::to_string(c.category) + " " + std::to_string(c.type) + " " + std::to_string(c.param1) + " " +
        std::to_string(c.param2));
}
void C_Human::PlayerSetViewMode(int mode) { Log("viewmode " + std::to_string(mode)); }
void C_HumanController::OnItemDropped(int actorId, int uid, const S_vector *dir, float speed)
{
    char s[64];
    std::snprintf(s, sizeof s, " speed %g", speed);
    Log("ctrl dropped " + std::to_string(actorId) + " " + std::to_string(uid) + " dir " + V(*dir) + s);
}
void C_HumanController::UpdateHandsItem() { Log("ctrl updatehands"); }
void C_Interface::OnActorEvent(int e, C_Actor *actor) { Log("interface actor " + std::to_string(e) + " " + std::to_string(actor->GetNetId())); }
void C_ItemActor::ReleaseItem()
{
    Log("itemactor release");
    m_item = nullptr;
}
bool C_MPZone::GetProgress(int *time, int *total, int *textId) const
{
    Log("zone progress " + std::to_string(GetNetId()));
    *time = 11;
    *total = 22;
    *textId = 33;
    return true;
}
C_SightTarget *C_SightModule::GetTarget(int id) const
{
    Log("sight target " + std::to_string(id));
    return g_sightTargets.count(id) ? g_sightTargets[id] : nullptr;
}
void C_SupplyBox::UseCharge()
{
    Log("box charge");
    --m_charges;
}
C_Actor *C_TemporaryActorsList::ClientCreateItem(int id, int ownerId, int itemType, int itemUid, const S_vector &pos,
                                                 const S_vector &dir, const S_vector *)
{
    Log("temp clientcreate " + std::to_string(id) + " " + std::to_string(ownerId) + " " + std::to_string(itemType) + " " +
        std::to_string(itemUid) + " at " + V(pos) + " vel " + V(dir));
    return nullptr;
}
void C_TemporaryActorsList::DestroyActor(C_Actor *actor) { Log("temp destroy " + std::to_string(actor->GetNetId())); }
// As the real one: the item leaves the human (into the world) unless the drop fails.
int C_TemporaryActorsList::DropItem(C_Human *human, int itemUid, const S_vector &pos, const S_vector &dir, bool unknown)
{
    Log("temp drop " + std::to_string(itemUid) + " at " + V(pos) + " vel " + V(dir) + " " + std::to_string(unknown));
    if (g_dropActorId < 0)
        return g_dropActorId;
    int container;
    if (C_Item *item = OwnFind(human, itemUid, &container))
        g_itemsModule.DropItem(item, human, container);
    return g_dropActorId;
}
C_Actor *C_TemporaryActorsList::FindActor(int id) const
{
    Log("temp find " + std::to_string(id));
    return g_tempActors.count(id) ? g_tempActors[id] : nullptr;
}
namespace {
void *g_effectsVt[256];
struct {
    void **vt = g_effectsVt;
    unsigned char fields[0x800] = {};
} g_effects;
bool __attribute__((thiscall)) F_DressUniform(void *, C_Human *human, int uniform)
{
    Log("dress " + std::to_string(human->GetNetId()) + " " + std::to_string(uniform));
    return true;
}
}  // namespace
C_GEffectsModule *GetGEffects() { return reinterpret_cast<C_GEffectsModule *>(&g_effects); }
void ReleaseHumanSpeech(const C_Human *human) { Log("speech release " + std::to_string(human->GetNetId())); }

namespace {
// The controller: its virtuals logged (their g++ slots from the member pointers).
void *g_ctrlVt[256];
void __attribute__((thiscall)) F_CtrlUniform(C_HumanController *, int uid) { Log("ctrl uniform " + std::to_string(uid)); }
void __attribute__((thiscall)) F_CtrlPickUp(C_HumanController *, int actorId, int container)
{
    Log("ctrl pickup " + std::to_string(actorId) + " " + std::to_string(container));
}
void __attribute__((thiscall)) F_CtrlThrow(C_HumanController *, int uid, const S_vector &v) { Log("ctrl throw " + std::to_string(uid) + " " + V(v)); }
bool __attribute__((thiscall)) F_CtrlMove(C_HumanController *, int uid, int otherId, int from, int to, int slot)
{
    Log("ctrl move " + std::to_string(uid) + " " + std::to_string(otherId) + " " + std::to_string(from) + " " + std::to_string(to) + " " +
        std::to_string(slot));
    return g_requestResult;
}
bool __attribute__((thiscall)) F_CtrlTransfer(C_HumanController *, int uid, int arg, int otherId, int otherUid, int container)
{
    Log("ctrl transfer " + std::to_string(uid) + " " + std::to_string(arg) + " " + std::to_string(otherId) + " " + std::to_string(otherUid) +
        " " + std::to_string(container));
    return g_requestResult;
}
void __attribute__((thiscall)) F_CtrlBox(C_HumanController *, int boxId, int index)
{
    Log("ctrl box " + std::to_string(boxId) + " " + std::to_string(index));
}
const S_vector &__attribute__((thiscall)) F_CtrlViewDir(C_HumanController *) { return g_viewDir; }
bool __attribute__((thiscall)) F_CtrlSpectating(C_HumanController *) { return g_spectating; }
void InitControllerVt()
{
    g_ctrlVt[VirtualSlot(&C_HumanController::OnUniformChanged)] = reinterpret_cast<void *>(F_CtrlUniform);
    g_ctrlVt[VirtualSlot(&C_HumanController::RequestPickUpItem)] = reinterpret_cast<void *>(F_CtrlPickUp);
    g_ctrlVt[VirtualSlot(&C_HumanController::RequestThrowItem)] = reinterpret_cast<void *>(F_CtrlThrow);
    g_ctrlVt[VirtualSlot(&C_HumanController::RequestMoveItem)] = reinterpret_cast<void *>(F_CtrlMove);
    g_ctrlVt[VirtualSlot(&C_HumanController::RequestTransferItem)] = reinterpret_cast<void *>(F_CtrlTransfer);
    g_ctrlVt[VirtualSlot(&C_HumanController::RequestTakeFromBox)] = reinterpret_cast<void *>(F_CtrlBox);
    g_ctrlVt[VirtualSlot(&C_HumanController::GetViewDir)] = reinterpret_cast<void *>(F_CtrlViewDir);
    g_ctrlVt[VirtualSlot(&C_HumanController::IsSpectating)] = reinterpret_cast<void *>(F_CtrlSpectating);
    g_effectsVt[VirtualSlot(&C_GEffectsModule::DressUniform)] = reinterpret_cast<void *>(F_DressUniform);
    g_actorVt[VirtualSlot(&C_Actor::GetHitPoints)] = reinterpret_cast<void *>(F_GetHitPoints);
}
template <class T> T *Raw(size_t extra = 0x40)
{
    const size_t size = sizeof(T) + extra;
    auto *bytes = static_cast<unsigned char *>(::operator new(size, std::align_val_t(16)));
    std::memset(bytes, 0, size);
    return reinterpret_cast<T *>(bytes);
}
C_PlayerController *Controller(int type = HUMAN_CONTROLLER_PLAYER)
{
    auto *c = Raw<C_PlayerController>();
    *reinterpret_cast<void ***>(c) = g_ctrlVt;
    c->m_type = static_cast<E_HumanController>(type);
    return c;
}
// A human with a controller, a model ("j" the hand, "f" the body; the file name at +0x134) and its net id known.
S_TestActor *Soldier(int netId, int group = 1)
{
    S_TestActor *a = Human(netId);
    C_Human *h = a->human;
    h->m_controller = Controller();
    h->m_group = group;
    h->m_mpZone = -1;
    h->m_sightId = -1;
    h->m_sightTargetId = -1;
    h->m_profileIndex = -1;
    h->m_seat = -1;
    I3D_frame *hand = Named("j", FRAME_DUMMY);
    hand->m_worldMatrix(3) = S_vector(1.0f, 2.0f, 3.0f);
    hand->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
    I3D_frame *model = Named("soldiermodel", FRAME_MODEL, {hand, Named("f", FRAME_VISUAL)});
    *reinterpret_cast<const char **>(reinterpret_cast<unsigned char *>(model) + 0x134) = "Models\\Soldier_A.i3d";
    h->m_frame = model;
    g_actorsById[netId] = h;
    return a;
}
// A new item of `type` put into `container` of `a` (no notifications, the mission off), not busy.
C_Item *Give(S_TestActor *a, int type, int container, int index = -1)
{
    const bool active = g_itemsModule.m_missionActive;
    g_itemsModule.m_missionActive = false;
    C_Item *item = New(type);
    static int uid = 1000;
    item->SetUid(uid++);
    C_Item *replaced = nullptr;
    const int result = Move(item, &replaced, nullptr, 0, a, container, false, false, false, false, index);
    g_itemsModule.m_missionActive = active;
    if (result != ITEM_RESULT_OK)
        std::printf("  Give %d into %d: %d\n", type, container, result);
    item->SetTime(0);
    return item;
}
int FindTypeBy(int behaviour) { return FindType([behaviour](C_ItemDef *d) { return d->GetBehaviour() == behaviour; }); }

// ============================================================ the containers, the slot getters
void TestHumanContainers()
{
    // Init: seven containers, the backpack's contents none; their limits.
    C_Human *h = Raw<C_Human>(0x100);
    *reinterpret_cast<void ***>(h) = g_actorVt;
    h->m_type = ACTOR_HUMAN;
    h->m_stats[STAT_HEALTH] = 100;
    h->C_Human::Init();
    CHECK(h->m_containers && !h->m_singleContainer && h->m_containers[ITEM_CONTAINER_BACKPACK_CONTENTS] == nullptr);
    struct {
        int container;
        float space;
        int size;
        int kind;
    } const want[] = {{ITEM_CONTAINER_HEAD, 4.0f, 2, 0},
                      {ITEM_CONTAINER_BELT, HUMAN_BELT_SPACE, 1, 1},
                      {ITEM_CONTAINER_LONG_GUN, HUMAN_LONG_GUN_SPACE, 3, 0},
                      {ITEM_CONTAINER_HANDS, 8.0f, 3, 0},
                      {ITEM_CONTAINER_UNIFORM, 8.0f, 3, 0},
                      {ITEM_CONTAINER_BACKPACK, 32.0f, 5, 0}};
    for (const auto &w : want) {
        C_ItemContainer *c = h->m_containers[w.container];
        CHECK(c && c->GetMaxWeight() == 100.0f && c->GetMaxSpace() == w.space && c->GetMaxItemSize() == w.size && c->GetKind() == w.kind);
    }
    // The real containers take items: a helmet on the head, a pistol in the hands, ammunition on the belt.
    S_TestActor a;
    a.human = h;
    C_Item *helmet = Give(&a, g_types.helmet, ITEM_CONTAINER_HEAD);
    C_Item *pistol = Give(&a, g_types.pistol, ITEM_CONTAINER_HANDS);
    Give(&a, g_types.ammo, ITEM_CONTAINER_BELT);
    CHECK(h->GetContainer(ITEM_CONTAINER_HEAD)->GetItem() == helmet && h->GetHandsItem() == pistol &&
          h->GetContainer(ITEM_CONTAINER_BELT)->GetUsedSlotCount() == 1);
    // ClearContainers empties them all (the backpack's contents too, before the backpack).
    C_Item *backpack = Give(&a, g_types.backpack, ITEM_CONTAINER_BACKPACK);
    Give(&a, g_types.ammo, ITEM_CONTAINER_BACKPACK_CONTENTS);
    C_ItemContainer *contents = h->m_containers[ITEM_CONTAINER_BACKPACK_CONTENTS];
    CHECK(contents && contents->GetUsedSlotCount() == 1 && backpack);
    const size_t instances = g_itemsModule.m_instances.size();
    h->ClearContainers();
    // (All five destroyed. The contents' pointer is left as it was, now freed with the backpack, as the original
    // 0x005a29c0 leaves it; its callers clear it first (ClearActorContainers in Respawn / CreateEquipment).)
    CHECK(g_itemsModule.m_instances.size() == instances - 5 && h->m_containers[ITEM_CONTAINER_BACKPACK_CONTENTS] == contents);
    for (int c = 0; c < ITEM_CONTAINER_BACKPACK_CONTENTS; ++c)
        CHECK(h->m_containers[c]->IsEmpty());
    // Done deletes them; again (and ClearContainers) without containers does nothing.
    h->m_containers[ITEM_CONTAINER_BACKPACK_CONTENTS] = nullptr;
    h->C_Human::Done();
    CHECK(h->m_containers == nullptr);
    h->C_Human::Done();
    h->ClearContainers();
    CHECK(h->m_containers == nullptr);

    // The getters.
    S_TestActor *s = Soldier(500);
    CHECK(!s->human->GetHandsItem() && !s->human->GetHandsItemFrame() && !s->human->GetHandsWeapon() && !s->human->GetLongGunItem() &&
          !s->human->GetUniformItem());
    C_Item *gun = Give(s, g_types.rifle, ITEM_CONTAINER_HANDS);
    gun->m_frame = Named("gunframe");
    C_Item *longGun = Give(s, g_types.longGun, ITEM_CONTAINER_LONG_GUN);
    C_Item *uniform = Give(s, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    CHECK(s->human->GetHandsItem() == gun && s->human->GetHandsItemFrame() == gun->m_frame &&
          s->human->GetHandsWeapon() == static_cast<C_WeaponItem *>(gun) && s->human->GetLongGunItem() == longGun &&
          s->human->GetUniformItem() == uniform);
    // An item in the hands that is not a weapon: no weapon.
    C_Item *replaced = nullptr;
    Move(gun, &replaced, s, ITEM_CONTAINER_HANDS, nullptr, 0, false);
    // (Put straight into the slot: MoveItem takes only weapons into the hands.)
    C_Item *medkit = New(g_types.medkit);
    s->human->m_containers[ITEM_CONTAINER_HANDS]->AddItem(medkit, &replaced, -1);
    CHECK(s->human->GetHandsItem() == medkit && medkit->GetKind() != ITEM_KIND_WEAPON && s->human->GetHandsWeapon() == nullptr);

    // OnHandsChanged / OnUniformChanged go to the controller (none: nothing).
    g_log.clear();
    s->human->OnHandsChanged();
    s->human->OnUniformChanged(77);
    CHECK(Count("ctrl updatehands") == 1 && Count("ctrl uniform 77") == 1);
    C_HumanController *ctrl = s->human->m_controller;
    s->human->m_controller = nullptr;
    g_log.clear();
    s->human->OnHandsChanged();
    CHECK(g_log.empty());
    s->human->m_controller = ctrl;
}

// ============================================================ the lookups
void TestHumanLookups()
{
    S_TestActor *s = Soldier(501);
    C_Human *h = s->human;
    CHECK(!h->FindItemByUid(0) && !h->FindItem(g_types.pistol) && !h->GetMedkit() && !h->FindWireCutter());
    C_Item *helmet = Give(s, g_types.helmet, ITEM_CONTAINER_HEAD);
    C_Item *hands = Give(s, g_types.pistol, ITEM_CONTAINER_HANDS);
    C_Item *longGun = Give(s, g_types.longGun, ITEM_CONTAINER_LONG_GUN);
    C_Item *uniform = Give(s, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    C_Item *backpack = Give(s, g_types.backpack, ITEM_CONTAINER_BACKPACK);
    C_Item *ammo1 = Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    C_Item *ammo2 = Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    C_Item *grenade = Give(s, g_types.grenade, ITEM_CONTAINER_BELT);
    C_Item *pistol2 = Give(s, g_types.pistol, ITEM_CONTAINER_BELT);
    C_Item *inPack = Give(s, g_types.ammo, ITEM_CONTAINER_BACKPACK_CONTENTS);
    C_Item *packGrenade = Give(s, g_types.grenade, ITEM_CONTAINER_BACKPACK_CONTENTS);
    C_ItemContainer *belt = h->GetContainer(ITEM_CONTAINER_BELT);
    CHECK(belt->GetUsedSlotCount() == 3 && belt->GetSlots()[0].size() == 2);

    // By uid: everywhere, each in its container only.
    const std::vector<std::pair<C_Item *, int>> all = {{helmet, ITEM_CONTAINER_HEAD},       {hands, ITEM_CONTAINER_HANDS},
                                                      {longGun, ITEM_CONTAINER_LONG_GUN}, {uniform, ITEM_CONTAINER_UNIFORM},
                                                      {backpack, ITEM_CONTAINER_BACKPACK}, {ammo1, ITEM_CONTAINER_BELT},
                                                      {ammo2, ITEM_CONTAINER_BELT},       {grenade, ITEM_CONTAINER_BELT},
                                                      {pistol2, ITEM_CONTAINER_BELT},     {inPack, ITEM_CONTAINER_BACKPACK_CONTENTS},
                                                      {packGrenade, ITEM_CONTAINER_BACKPACK_CONTENTS}};
    for (const auto &[item, container] : all) {
        CHECK(h->FindItemByUid(item->GetUid()) == item);
        for (int c = 0; c <= ITEM_CONTAINER_COUNT; ++c)
            CHECK(h->FindItemInContainer(item->GetUid(), c) == (c == container ? item : nullptr));
    }
    CHECK(!h->FindItemByUid(9999) && !h->FindItemInContainer(9999, ITEM_CONTAINER_BELT) && !h->FindItemInContainer(hands->GetUid(), -1));

    // By type: the slots first (head, long gun, hands, uniform, backpack), then the belt, then the backpack's contents.
    int container = -5;
    CHECK(h->FindItem(g_types.pistol, &container) == hands && container == ITEM_CONTAINER_HANDS);
    CHECK(h->FindItem(g_types.helmet, &container) == helmet && container == ITEM_CONTAINER_HEAD);
    CHECK(h->FindItem(g_types.longGun, &container) == longGun && container == ITEM_CONTAINER_LONG_GUN);
    CHECK(h->FindItem(g_types.uniform, &container) == uniform && container == ITEM_CONTAINER_UNIFORM);
    CHECK(h->FindItem(g_types.backpack, &container) == backpack && container == ITEM_CONTAINER_BACKPACK);
    // (A slot is searched from its end: the second magazine first.)
    CHECK(h->FindItem(g_types.ammo, &container) == ammo2 && container == ITEM_CONTAINER_BELT);
    CHECK(h->FindItem(g_types.grenade) == grenade);
    container = -5;
    CHECK(h->FindItem(g_types.mine, &container) == nullptr && container == -5);
    // By behaviour: the slots in uid order (hands, head, long gun, uniform, backpack), then belt, contents; a miss
    // gives the container count.
    const int pistolBehaviour = hands->GetDef()->GetBehaviour();
    CHECK(h->FindItemByBehaviour(pistolBehaviour, &container) == hands && container == ITEM_CONTAINER_HANDS);
    CHECK(h->FindItemByBehaviour(ITEM_BEHAVIOUR_THROWN, &container) == grenade && container == ITEM_CONTAINER_BELT);
    CHECK(h->FindItemByBehaviour(ITEM_BEHAVIOUR_KEY, &container) == nullptr && container == ITEM_CONTAINER_COUNT);
    CHECK(h->FindItemByRecordType(ITEM_AMMO) == ammo2 && h->FindItemByRecordType(-7) == nullptr);
    CHECK(h->FindItemByRecordType(helmet->GetDef()->GetRecordType()) == (hands->GetDef()->GetRecordType() == helmet->GetDef()->GetRecordType() ? hands : helmet));

    // Without the hands' and slots' pistol: found on the belt, then in the contents.
    C_Item *replaced = nullptr;
    Move(hands, &replaced, s, ITEM_CONTAINER_HANDS, nullptr, 0, false);
    CHECK(h->FindItem(g_types.pistol, &container) == pistol2 && container == ITEM_CONTAINER_BELT);
    CHECK(h->FindItemByBehaviour(pistolBehaviour, &container) == pistol2 && container == ITEM_CONTAINER_BELT);
    Move(grenade, &replaced, s, ITEM_CONTAINER_BELT, nullptr, 0, false);
    CHECK(h->FindItem(g_types.grenade, &container) == packGrenade && container == ITEM_CONTAINER_BACKPACK_CONTENTS);
    CHECK(h->FindItemByBehaviour(ITEM_BEHAVIOUR_THROWN, &container) == packGrenade && container == ITEM_CONTAINER_BACKPACK_CONTENTS);
    CHECK(h->FindItemByRecordType(packGrenade->GetDef()->GetRecordType()) != nullptr);
    CHECK(h->FindContainerOfBehaviour(ITEM_BEHAVIOUR_THROWN, &container) == packGrenade && container == ITEM_CONTAINER_BACKPACK_CONTENTS);
    CHECK(h->FindContainerOfBehaviour(ITEM_BEHAVIOUR_THROWN, nullptr) == packGrenade);
    container = -5;
    CHECK(h->FindContainerOfBehaviour(ITEM_BEHAVIOUR_KEY, &container) == nullptr && container == -5);
    // In container order: the head before the belt.
    CHECK(h->FindContainerOfBehaviour(helmet->GetDef()->GetBehaviour(), &container) == helmet && container == ITEM_CONTAINER_HEAD);

    // Collect: every item of the behaviour with its container (slots from their ends).
    std::vector<C_Item *> items = {hands};
    std::vector<int> containers = {9};
    CHECK(h->CollectItems(ITEM_BEHAVIOUR_AMMO, items, containers) == (ammo1->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_AMMO));
    if (ammo1->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_AMMO)
        CHECK(items == std::vector<C_Item *>({ammo2, ammo1, inPack}) &&
              containers == std::vector<int>({ITEM_CONTAINER_BELT, ITEM_CONTAINER_BELT, ITEM_CONTAINER_BACKPACK_CONTENTS}));
    CHECK(!h->CollectItems(ITEM_BEHAVIOUR_KEY, items, containers) && items.empty() && containers.empty());
    CHECK(h->CollectItems(helmet->GetDef()->GetBehaviour(), items, containers) && items[0] == helmet && containers[0] == ITEM_CONTAINER_HEAD);

    // Counts: a slot item is 1; the belt's slot size; the contents'; none 0.
    CHECK(h->CountItems(g_types.helmet) == 1 && h->CountItems(g_types.backpack) == 1 && h->CountItems(g_types.ammo) == 2 &&
          h->CountItems(g_types.grenade) == 1 && h->CountItems(g_types.mine) == 0 && h->CountItems(g_types.pistol) == 1);
    // Ammunition: the belt's slot plus (asked) the contents' slot.
    CHECK(h->CountAmmo(g_types.ammo, false) == 2 && h->CountAmmo(g_types.ammo, true) == 3 && h->CountAmmo(g_types.grenade, false) == 0 &&
          h->CountAmmo(g_types.grenade, true) == 1 && h->CountAmmo(g_types.mine, true) == 0);
}

// ============================================================ the belt's ready items
void TestBeltItems()
{
    const int wireCutter = FindTypeBy(ITEM_BEHAVIOUR_WIRE_CUTTER);
    CHECK(wireCutter >= 0);
    S_TestActor *s = Soldier(502);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    CHECK(!h->FindBeltItem(ITEM_BEHAVIOUR_THROWN) && h->CountThrownItems() == 0 && !h->IsWearingDivingGear());
    C_Item *g1 = Give(s, g_types.grenade, ITEM_CONTAINER_BELT);
    C_Item *g2 = Give(s, g_types.grenade, ITEM_CONTAINER_BELT);
    C_Item *medkit = Give(s, g_types.medkit, ITEM_CONTAINER_BELT);
    C_Item *cutter = Give(s, wireCutter, ITEM_CONTAINER_BELT);
    C_ItemContainer *belt = h->GetContainer(ITEM_CONTAINER_BELT);
    CHECK(belt->GetUsedSlotCount() == 3 && belt->GetSlots()[0].size() == 2 && belt->GetSlots()[0].front() == g1);
    // A slot's first item stands for it; ready when its time has come (equal is ready).
    CHECK(h->FindBeltItem(ITEM_BEHAVIOUR_THROWN) == g1 && h->CountThrownItems() == 2 && h->GetMedkit() == medkit && h->FindWireCutter() == cutter);
    g1->SetTime(5000);
    CHECK(h->FindBeltItem(ITEM_BEHAVIOUR_THROWN) == g1 && h->CountThrownItems() == 2);
    g1->SetTime(5001);
    g2->SetTime(0);
    CHECK(h->FindBeltItem(ITEM_BEHAVIOUR_THROWN) == nullptr && h->CountThrownItems() == 0);
    medkit->SetTime(9000);
    cutter->SetTime(9000);
    CHECK(h->GetMedkit() == nullptr && h->FindWireCutter() == nullptr);
    // Two ready slots of a behaviour: the last slot is found first.
    const int pistols = FindType([](C_ItemDef *d) { return d->GetBehaviour() == ITEM_BEHAVIOUR_SIDEARM && d->GetInventorySize() == 1; });
    const int otherPistol = FindType([pistols](C_ItemDef *d) {
        return d->GetBehaviour() == ITEM_BEHAVIOUR_SIDEARM && d->GetInventorySize() == 1 && d != g_itemsModule.GetItemDef(pistols);
    });
    if (otherPistol >= 0) {
        C_Item *p1 = Give(s, pistols, ITEM_CONTAINER_BELT);
        C_Item *p2 = Give(s, otherPistol, ITEM_CONTAINER_BELT);
        CHECK(p1 && p2 && h->FindBeltItem(ITEM_BEHAVIOUR_SIDEARM) == p2);
        p2->SetTime(9000);
        CHECK(h->FindBeltItem(ITEM_BEHAVIOUR_SIDEARM) == p1);
    }
    // The thrown items of several ready slots add up.
    g1->SetTime(0);
    const int otherGrenade = FindType([](C_ItemDef *d) {
        return d->GetBehaviour() == ITEM_BEHAVIOUR_THROWN && d->GetInventorySize() <= 1 && d != g_itemsModule.GetItemDef(g_types.grenade);
    });
    if (otherGrenade >= 0) {
        Give(s, otherGrenade, ITEM_CONTAINER_BELT);
        CHECK(h->CountThrownItems() == 3);
    }
    // The medkit in the backpack's slot goes first; another backpack item does not count.
    medkit->SetTime(0);
    C_Item *packMedkit = FindType([](C_ItemDef *d) { return d->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT && (d->GetAllowedContainers() & ITEM_FITS_BACKPACK); }) >= 0
                             ? Give(s, FindType([](C_ItemDef *d) {
                                        return d->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT && (d->GetAllowedContainers() & ITEM_FITS_BACKPACK);
                                    }),
                                    ITEM_CONTAINER_BACKPACK)
                             : nullptr;
    if (packMedkit) {
        CHECK(h->GetContainer(ITEM_CONTAINER_BACKPACK)->GetItem() == packMedkit && h->GetMedkit() == packMedkit);
        C_Item *replaced = nullptr;
        Move(packMedkit, &replaced, s, ITEM_CONTAINER_BACKPACK, nullptr, 0, false);
    }
    C_Item *pack = Give(s, g_types.backpack, ITEM_CONTAINER_BACKPACK);
    CHECK(h->GetMedkit() == medkit && !h->IsWearingDivingGear());
    // Diving gear (type 231) in the backpack's slot.
    C_Item *replaced = nullptr;
    Move(pack, &replaced, s, ITEM_CONTAINER_BACKPACK, nullptr, 0, false);
    C_Item *gear = Give(s, 231, ITEM_CONTAINER_BACKPACK);
    CHECK(h->GetContainer(ITEM_CONTAINER_BACKPACK)->GetItem() == gear && h->IsWearingDivingGear());
}

// ============================================================ removing, creating
void TestRemoving()
{
    S_TestActor *s = Soldier(503);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    C_Item *helmet = Give(s, g_types.helmet, ITEM_CONTAINER_HEAD);
    C_WeaponItem *gun = static_cast<C_WeaponItem *>(Give(s, g_types.pistol, ITEM_CONTAINER_HANDS));
    C_Item *longGun = Give(s, g_types.longGun, ITEM_CONTAINER_LONG_GUN);
    C_Item *uniform = Give(s, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    C_Item *pack = Give(s, g_types.backpack, ITEM_CONTAINER_BACKPACK);
    C_Item *ammo = Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    C_Item *inPack = Give(s, g_types.grenade, ITEM_CONTAINER_BACKPACK_CONTENTS);
    const size_t instances = g_itemsModule.m_instances.size();
    // A uid nobody has: nothing.
    h->m_inventoryChanged = false;
    g_log.clear();
    h->RemoveItem(9999, true);
    CHECK(g_log.empty() && !h->m_inventoryChanged && g_itemsModule.m_instances.size() == instances);
    // The hands' weapon: its reload sound stopped (more than 200 ms to go), the view mode reset; kept.
    gun->m_soundId = 5;
    gun->m_readyTime = 5500;
    h->RemoveItem(gun->GetUid(), false);
    int container;
    CHECK(Count("sound stop 5") == 1 && gun->m_soundId == -1 && Count("viewmode 0") == 1 && h->m_inventoryChanged &&
          !OwnFind(h, gun->GetUid(), &container) && h->GetHandsItem() == nullptr && g_itemsModule.m_instances.size() == instances);
    // Every other container: no view mode; destroyed when asked.
    struct {
        C_Item *item;
        bool destroy;
    } const cases[] = {{helmet, false}, {longGun, true}, {uniform, false}, {ammo, true}, {inPack, true}, {pack, false}};
    size_t left = instances;
    for (const auto &c : cases) {
        g_log.clear();
        h->m_inventoryChanged = false;
        const int uid = c.item->GetUid();
        h->RemoveItem(uid, c.destroy);
        left -= c.destroy;
        CHECK(!OwnFind(h, uid, &container) && h->m_inventoryChanged && !Logged("viewmode") && g_itemsModule.m_instances.size() == left);
    }
    // (The backpack taken off: its contents with it.)
    CHECK(h->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == nullptr);
    // A non-weapon in the hands: no reload sound to stop (the original's call on it is the port's fix), the view
    // mode reset.
    C_Item *replaced = nullptr;
    C_Item *medkit = New(g_types.medkit);
    medkit->SetUid(4321);
    h->m_containers[ITEM_CONTAINER_HANDS]->AddItem(medkit, &replaced, -1);
    g_log.clear();
    h->RemoveItem(4321, true);
    CHECK(Count("viewmode 0") == 1 && !Logged("sound") && h->GetHandsItem() == nullptr);
    // The hands' weapon with its reload sound nearly over (200 ms): not stopped, its id dropped.
    gun = static_cast<C_WeaponItem *>(Give(s, g_types.pistol, ITEM_CONTAINER_HANDS));
    gun->m_soundId = 6;
    gun->m_readyTime = 5200;
    g_log.clear();
    h->RemoveItem(gun->GetUid(), true);
    CHECK(!Logged("sound stop") && Count("viewmode 0") == 1);

    // RemoveItemsFromStore: every item of the type, wherever (destroyed); changed even with none.
    Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    Give(s, g_types.backpack, ITEM_CONTAINER_BACKPACK);
    Give(s, g_types.ammo, ITEM_CONTAINER_BACKPACK_CONTENTS);
    Give(s, g_types.grenade, ITEM_CONTAINER_BELT);
    const size_t before = g_itemsModule.m_instances.size();
    h->RemoveItemsFromStore(g_types.ammo);
    CHECK(h->CountItems(g_types.ammo) == 0 && h->CountItems(g_types.grenade) == 1 && g_itemsModule.m_instances.size() == before - 3);
    h->m_inventoryChanged = false;
    h->RemoveItemsFromStore(g_types.mine);
    CHECK(h->m_inventoryChanged && g_itemsModule.m_instances.size() == before - 3);

    // CreateItemInStore: a new item in the container.
    h->CreateItemInStore(g_types.helmet, ITEM_CONTAINER_HEAD);
    CHECK(h->GetContainer(ITEM_CONTAINER_HEAD)->GetItem() && h->GetContainer(ITEM_CONTAINER_HEAD)->GetItem()->GetTypeId() == g_types.helmet);
    h->CreateItemInStore(g_types.grenade, ITEM_CONTAINER_BELT);
    CHECK(h->CountItems(g_types.grenade) == 2);
}

// ============================================================ the requests to the controller
void TestRequests()
{
    S_TestActor *s = Soldier(504, 2);
    S_TestActor *o = Soldier(505, 2);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    C_Item *gun = Give(s, g_types.rifle, ITEM_CONTAINER_HANDS);
    C_Item *uniform = Give(s, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    C_Item *otherUniform = Give(o, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    const std::string uid = std::to_string(gun->GetUid());

    // Move: the other actor's net id (-1 for none or itself); the controller's answer.
    g_log.clear();
    CHECK(h->RequestMoveItem(gun, nullptr, 3, 4, 5));
    CHECK(h->RequestMoveItem(gun, h, 1, 2, -1));
    CHECK(h->RequestMoveItem(gun, o->human, 0, 6, 2));
    g_requestResult = false;
    CHECK(!h->RequestMoveItem(gun, o->human, 0, 6, 2));
    g_requestResult = true;
    CHECK(Count("ctrl move " + uid + " -1 3 4 5") == 1 && Count("ctrl move " + uid + " -1 1 2 -1") == 1 &&
          Count("ctrl move " + uid + " 505 0 6 2") == 2);

    // Transfer: none / itself -1; a uniform only with an unconscious human of the same group, neither flagged 0x1A5.
    const std::string u = std::to_string(uniform->GetUid()), ou = std::to_string(otherUniform->GetUid());
    g_log.clear();
    CHECK(h->RequestTransferItem(uniform, 8, nullptr, otherUniform, ITEM_CONTAINER_UNIFORM));
    CHECK(Count("ctrl transfer " + u + " 8 -1 " + ou + " " + std::to_string(ITEM_CONTAINER_UNIFORM)) == 1);
    g_log.clear();
    CHECK(!h->RequestTransferItem(uniform, 8, o->human, otherUniform, ITEM_CONTAINER_UNIFORM) && g_log.empty());   // both conscious
    g_unconscious.insert(o->human);
    CHECK(h->RequestTransferItem(uniform, 8, o->human, otherUniform, ITEM_CONTAINER_UNIFORM));
    CHECK(Count("ctrl transfer " + u + " 8 505 " + ou + " " + std::to_string(ITEM_CONTAINER_UNIFORM)) == 1);
    g_unconscious.clear();
    g_unconscious.insert(h);                            // (this one unconscious is enough too)
    g_log.clear();
    CHECK(h->RequestTransferItem(uniform, 8, o->human, otherUniform, ITEM_CONTAINER_UNIFORM) && Logged("ctrl transfer"));
    o->human->m_unknown1a5 = true;
    g_log.clear();
    CHECK(!h->RequestTransferItem(uniform, 8, o->human, otherUniform, ITEM_CONTAINER_UNIFORM) && g_log.empty());
    o->human->m_unknown1a5 = false;
    h->m_unknown1a5 = true;
    CHECK(!h->RequestTransferItem(uniform, 8, o->human, otherUniform, ITEM_CONTAINER_UNIFORM) && g_log.empty());
    h->m_unknown1a5 = false;
    o->human->m_group = 3;
    CHECK(!h->RequestTransferItem(uniform, 8, o->human, otherUniform, ITEM_CONTAINER_UNIFORM) && g_log.empty());
    // Not the uniform: none of that asked; itself -1; the controller's answer.
    g_unconscious.clear();
    CHECK(h->RequestTransferItem(gun, 1, o->human, otherUniform, ITEM_CONTAINER_HANDS));
    CHECK(Count("ctrl transfer " + uid + " 1 505 " + ou + " " + std::to_string(ITEM_CONTAINER_HANDS)) == 1);
    o->human->m_group = 2;
    g_requestResult = false;
    g_log.clear();
    CHECK(!h->RequestTransferItem(gun, 1, h, uniform, ITEM_CONTAINER_HANDS));
    CHECK(Count("ctrl transfer " + uid + " 1 -1 " + u + " " + std::to_string(ITEM_CONTAINER_HANDS)) == 1);
    g_requestResult = true;

    // Throw: (random * 0.2 / 0.1 / 0.2 + dir) * 2.5 (the original 0x005a2c50), changed.
    std::srand(77);
    const float a = RandFloat(), b = RandFloat(), c = RandFloat();
    const S_vector want((a * 0.2f + 1.0f) * 2.5f, (b * 0.1f + 0.5f) * 2.5f, (c * 0.2f - 2.0f) * 2.5f);
    std::srand(77);
    h->m_inventoryChanged = false;
    g_log.clear();
    h->RequestThrowItem(gun, 0, S_vector(1.0f, 0.5f, -2.0f));
    CHECK(Count("ctrl throw " + uid + " " + V(want)) == 1 && h->m_inventoryChanged && a != b);
    // Take from a box, pick up: the actors' net ids, changed.
    S_TestActor *box = Vehicle(77);
    h->m_inventoryChanged = false;
    g_log.clear();
    h->RequestTakeFromBox(box->human, 3);
    CHECK(Count("ctrl box 77 3") == 1 && h->m_inventoryChanged);
    h->m_inventoryChanged = false;
    h->RequestPickUpItem(box->human);
    CHECK(Count("ctrl pickup 77 " + std::to_string(ITEM_CONTAINER_COUNT)) == 1 && h->m_inventoryChanged);
}

// ============================================================ drawing the long gun / the sidearm
void TestDraw()
{
    S_TestActor *s = Soldier(506);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    // Nothing to draw.
    g_log.clear();
    CHECK(!h->DrawLongGun() && !h->DrawSidearm(2) && g_log.empty());
    C_Item *longGun = Give(s, g_types.longGun, ITEM_CONTAINER_LONG_GUN);
    // The long gun: asked of the controller (its answer returned), the hands updated.
    CHECK(h->DrawLongGun());
    CHECK(Count("ctrl move " + std::to_string(longGun->GetUid()) + " -1 " + std::to_string(ITEM_CONTAINER_LONG_GUN) + " " +
                std::to_string(ITEM_CONTAINER_HANDS) + " 0") == 1 &&
          Count("ctrl updatehands") == 1);
    g_requestResult = false;
    g_log.clear();
    CHECK(!h->DrawLongGun() && Count("ctrl updatehands") == 1);
    g_requestResult = true;
    // The sidearm: only for reasons 2, 9, 10, 11; the first ready slot of sidearms (searched from the last).
    const int sidearm = FindType([](C_ItemDef *d) { return d->GetBehaviour() == ITEM_BEHAVIOUR_SIDEARM && d->GetInventorySize() == 1; });
    const int otherSidearm = FindType([sidearm](C_ItemDef *d) {
        return d->GetBehaviour() == ITEM_BEHAVIOUR_SIDEARM && d->GetInventorySize() == 1 && d != g_itemsModule.GetItemDef(sidearm);
    });
    Give(s, g_types.grenade, ITEM_CONTAINER_BELT);
    g_log.clear();
    CHECK(!h->DrawSidearm(2) && g_log.empty());       // (a belt without one)
    C_Item *p1 = Give(s, sidearm, ITEM_CONTAINER_BELT);
    C_Item *p2 = otherSidearm >= 0 ? Give(s, otherSidearm, ITEM_CONTAINER_BELT) : nullptr;
    const std::string belt = " -1 " + std::to_string(ITEM_CONTAINER_BELT) + " " + std::to_string(ITEM_CONTAINER_HANDS) + " 0";
    for (int reason = -1; reason <= 12; ++reason) {
        g_log.clear();
        const bool allowed = reason == 2 || reason == 9 || reason == 10 || reason == 11;
        CHECK(h->DrawSidearm(reason) == allowed && Count("ctrl move " + std::to_string(p1->GetUid()) + belt) == allowed &&
              Count("ctrl updatehands") == allowed);
    }
    if (p2) {
        // p1's slot busy: p2.
        p1->SetTime(5001);
        g_log.clear();
        CHECK(h->DrawSidearm(9) && Count("ctrl move " + std::to_string(p2->GetUid()) + belt) == 1);
        p2->SetTime(5001);
    } else {
        p1->SetTime(5001);
    }
    g_log.clear();
    CHECK(!h->DrawSidearm(9) && g_log.empty());
    p1->SetTime(5000);
    g_requestResult = false;
    CHECK(!h->DrawSidearm(10) && Count("ctrl updatehands") == 1);
    g_requestResult = true;
    // Something in the hands: neither.
    Give(s, g_types.pistol, ITEM_CONTAINER_HANDS);
    g_log.clear();
    CHECK(!h->DrawLongGun() && !h->DrawSidearm(2) && g_log.empty());
}
}  // namespace

#include "human_items_test2.inc"
