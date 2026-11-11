// Part 4, continued (included by human_items_test.inc): executing moves and transfers, dropping, supply boxes,
// picking up, uniforms, the soldier state, carried bodies, the action's progress.
namespace {

// ============================================================ executing moves and transfers
void TestServerClientMoves()
{
    S_TestActor *s = Soldier(507);
    S_TestActor *o = Soldier(508);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    C_Item *pistol = Give(o, g_types.pistol, ITEM_CONTAINER_HANDS);
    C_Item *ammo = Give(o, g_types.ammo, ITEM_CONTAINER_BELT);
    C_Item *rifle = Give(o, g_types.rifle, ITEM_CONTAINER_LONG_GUN);
    // Not in the container named: false.
    int to = ITEM_CONTAINER_HANDS;
    g_log.clear();
    CHECK(!h->ServerMoveItem(pistol->GetUid(), 508, ITEM_CONTAINER_BELT, &to, -1) && !h->m_inventoryChanged && g_log.empty());
    // From the other's hands into these hands: moved, changed, the view mode reset.
    CHECK(h->ServerMoveItem(pistol->GetUid(), 508, ITEM_CONTAINER_HANDS, &to, -1));
    CHECK(h->GetHandsItem() == pistol && !o->human->GetHandsItem() && h->m_inventoryChanged && !o->human->m_inventoryChanged &&
          Count("viewmode 0") == 1 && to == ITEM_CONTAINER_HANDS);
    // Any container (the count): TransferItemAuto picks the belt for ammunition; no view mode.
    h->m_inventoryChanged = false;
    to = ITEM_CONTAINER_COUNT;
    g_log.clear();
    CHECK(h->ServerMoveItem(ammo->GetUid(), 508, ITEM_CONTAINER_BELT, &to, -1) && to == ITEM_CONTAINER_BELT &&
          h->FindItemInContainer(ammo->GetUid(), ITEM_CONTAINER_BELT) == ammo && h->m_inventoryChanged && !Logged("viewmode"));
    // A move the items module refuses (a rifle on the belt): false, unchanged.
    h->m_inventoryChanged = false;
    to = ITEM_CONTAINER_BELT;
    g_log.clear();
    CHECK(!h->ServerMoveItem(rifle->GetUid(), 508, ITEM_CONTAINER_LONG_GUN, &to, -1) && !h->m_inventoryChanged && !Logged("viewmode") &&
          o->human->GetLongGunItem() == rifle);
    // Its own (-1): the hands' pistol to the belt: the view mode (from the hands).
    to = ITEM_CONTAINER_BELT;
    CHECK(h->ServerMoveItem(pistol->GetUid(), -1, ITEM_CONTAINER_HANDS, &to, -1) && Count("viewmode 0") == 1 && !h->GetHandsItem());
    // ...and back into the hands: the view mode (to the hands).
    Later();
    to = ITEM_CONTAINER_HANDS;
    g_log.clear();
    CHECK(h->ServerMoveItem(pistol->GetUid(), -1, ITEM_CONTAINER_BELT, &to, -1) && Count("viewmode 0") == 1 && h->GetHandsItem() == pistol);
    Later();
    to = ITEM_CONTAINER_BELT;
    CHECK(h->ServerMoveItem(pistol->GetUid(), -1, ITEM_CONTAINER_HANDS, &to, -1));
    Later();
    // From a store actor (one container): its container 0 is read as the head slot, empty for a store: false (as
    // the original's FindItemInContainer 0x005a6db0 on it).
    S_TestActor *box = Vehicle(509);
    g_actorsById[509] = box->human;
    C_Item *boxed = Give(box, g_types.ammo, 0);
    to = ITEM_CONTAINER_BELT;
    CHECK(!h->ServerMoveItem(boxed->GetUid(), 509, 0, &to, -1));

    // The client's side: nothing on a server (or a local game); a client moves whatever is asked, from wherever the
    // item is in that actor (the store actor's too).
    h->m_inventoryChanged = false;
    g_gameRole = GAME_ROLE_SERVER;
    h->ClientMoveItem(rifle->GetUid(), 508, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_LONG_GUN, -1);
    g_gameRole = GAME_ROLE_LOCAL;
    h->ClientMoveItem(rifle->GetUid(), 508, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_LONG_GUN, -1);
    CHECK(o->human->GetLongGunItem() == rifle && !h->m_inventoryChanged);
    g_gameRole = GAME_ROLE_CLIENT;
    h->ClientMoveItem(rifle->GetUid(), 508, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_LONG_GUN, -1);
    CHECK(h->GetLongGunItem() == rifle && !o->human->GetLongGunItem() && h->m_inventoryChanged);
    h->ClientMoveItem(boxed->GetUid(), 509, 0, ITEM_CONTAINER_BELT, -1);
    CHECK(h->CountItems(g_types.ammo) == 2 && box->containers[0]->IsEmpty());
    // Its own (-1): the hands from the belt.
    h->ClientMoveItem(pistol->GetUid(), -1, ITEM_CONTAINER_BELT, ITEM_CONTAINER_HANDS, -1);
    CHECK(h->GetHandsItem() == pistol);
    g_gameRole = GAME_ROLE_LOCAL;

    // Transfers: both found or false; the swap done, both changed, the view mode for the hands on either side.
    C_Item *u1 = Give(s, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    C_Item *u2 = Give(o, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    h->m_inventoryChanged = o->human->m_inventoryChanged = false;
    g_log.clear();
    CHECK(!h->ServerTransferItem(u1->GetUid(), ITEM_CONTAINER_HEAD, 508, u2->GetUid(), ITEM_CONTAINER_UNIFORM));
    CHECK(!h->ServerTransferItem(u1->GetUid(), ITEM_CONTAINER_UNIFORM, 508, u2->GetUid(), ITEM_CONTAINER_BELT));
    CHECK(!h->m_inventoryChanged && g_log.empty());
    CHECK(h->ServerTransferItem(u1->GetUid(), ITEM_CONTAINER_UNIFORM, 508, u2->GetUid(), ITEM_CONTAINER_UNIFORM));
    CHECK(h->GetUniformItem() == u2 && o->human->GetUniformItem() == u1 && h->m_inventoryChanged && o->human->m_inventoryChanged &&
          !Logged("viewmode"));
    // The hands: a pistol for the other's pistol (once the moved one is not busy any more).
    C_Item *p2 = Give(o, g_types.pistol, ITEM_CONTAINER_HANDS);
    Later();
    g_log.clear();
    CHECK(h->ServerTransferItem(pistol->GetUid(), ITEM_CONTAINER_HANDS, 508, p2->GetUid(), ITEM_CONTAINER_HANDS) && Count("viewmode 0") == 1 &&
          h->GetHandsItem() == p2 && o->human->GetHandsItem() == pistol);
    // A transfer the items module refuses (the other's ammunition for the helmet): false, unchanged.
    C_Item *helmet = Give(s, g_types.helmet, ITEM_CONTAINER_HEAD);
    C_Item *ammo2 = Give(o, g_types.ammo, ITEM_CONTAINER_BELT);
    h->m_inventoryChanged = false;
    g_log.clear();
    CHECK(!h->ServerTransferItem(helmet->GetUid(), ITEM_CONTAINER_HEAD, 508, ammo2->GetUid(), ITEM_CONTAINER_BELT) && !h->m_inventoryChanged &&
          !Logged("viewmode"));
    // Its own (-1): the hands' pistol for a pistol on its belt (the view mode: the hands); the client the same.
    C_Item *p3 = Give(s, g_types.pistol, ITEM_CONTAINER_BELT);
    Later();
    g_log.clear();
    CHECK(h->ServerTransferItem(p2->GetUid(), ITEM_CONTAINER_HANDS, -1, p3->GetUid(), ITEM_CONTAINER_BELT) && Count("viewmode 0") == 1 &&
          h->GetHandsItem() == p3 && h->FindItemInContainer(p2->GetUid(), ITEM_CONTAINER_BELT) == p2);
    Later();
    g_gameRole = GAME_ROLE_CLIENT;
    h->ClientTransferItem(p3->GetUid(), ITEM_CONTAINER_HANDS, -1, p2->GetUid(), ITEM_CONTAINER_BELT);
    CHECK(h->GetHandsItem() == p2 && h->FindItemInContainer(p3->GetUid(), ITEM_CONTAINER_BELT) == p3);
    g_gameRole = GAME_ROLE_LOCAL;
    Later();
    // The client's side: not on a server; a client swaps.
    h->m_inventoryChanged = false;
    h->ClientTransferItem(u2->GetUid(), ITEM_CONTAINER_UNIFORM, 508, u1->GetUid(), ITEM_CONTAINER_UNIFORM);
    CHECK(h->GetUniformItem() == u2 && !h->m_inventoryChanged);
    g_gameRole = GAME_ROLE_CLIENT;
    o->human->m_inventoryChanged = false;
    h->ClientTransferItem(u2->GetUid(), ITEM_CONTAINER_UNIFORM, 508, u1->GetUid(), ITEM_CONTAINER_UNIFORM);
    CHECK(h->GetUniformItem() == u1 && o->human->GetUniformItem() == u2 && h->m_inventoryChanged && o->human->m_inventoryChanged);
    g_gameRole = GAME_ROLE_LOCAL;
}

// ============================================================ dropping
// The velocity DropAllItems gives each item: random strength (0..2) times three randoms (0..0.2) on `base` (the
// original 0x005a2fe0: the strength drawn first).
S_vector NextDrop(const S_vector &base)
{
    const float strength = RandFloat() * 2.0f;
    const float a = RandFloat() * 0.2f;
    const float b = RandFloat() * 0.2f;
    const float c = RandFloat() * 0.2f;
    return S_vector(a * strength + base.x, b * strength + base.y, c * strength + base.z);
}
std::vector<std::string> Drops()
{
    std::vector<std::string> out;
    for (const std::string &l : g_log)
        if (l.rfind("temp drop", 0) == 0)
            out.push_back(l);
    return out;
}
void TestDropping()
{
    S_TestActor *s = Soldier(510);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    C_WeaponItem *gun = static_cast<C_WeaponItem *>(Give(s, g_types.rifle, ITEM_CONTAINER_HANDS));
    // DropItem: from the hand's place; the hands' weapon's reload sound stopped; changed; a local game tells no one.
    gun->m_soundId = 8;
    gun->m_readyTime = 6000;
    g_log.clear();
    h->DropItem(gun->GetUid(), S_vector(0.0f, 3.0f, 4.0f));
    CHECK(Count("temp drop " + std::to_string(gun->GetUid()) + " at 1 2 3 vel 0 3 4 1") == 1 && Count("sound stop 8") == 1 && h->m_inventoryChanged &&
          !Logged("ctrl dropped") && !h->GetHandsItem());
    // Not the hands' item: no sound stopped. A server tells the controller the direction and speed.
    C_Item *ammo = Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    C_WeaponItem *gun2 = static_cast<C_WeaponItem *>(Give(s, g_types.rifle, ITEM_CONTAINER_HANDS));
    gun2->m_soundId = 9;
    gun2->m_readyTime = 6000;
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    h->DropItem(ammo->GetUid(), S_vector(0.0f, 3.0f, 4.0f));
    CHECK(!Logged("sound stop") && Count("ctrl dropped 100 " + std::to_string(ammo->GetUid()) + " dir 0 0.6 0.8 speed 5") == 1);
    // A failed drop: nothing told, unchanged.
    g_dropActorId = -3;
    h->m_inventoryChanged = false;
    g_log.clear();
    h->DropItem(gun2->GetUid(), S_vector(1.0f, 0.0f, 0.0f));
    CHECK(Logged("temp drop") && !Logged("ctrl dropped") && !h->m_inventoryChanged && h->GetHandsItem() == gun2 && Count("sound stop 9") == 1);
    g_dropActorId = 100;
    g_gameRole = GAME_ROLE_LOCAL;
    // The client's drop: a new item actor at the hand (not on a server).
    h->m_inventoryChanged = false;
    g_log.clear();
    h->ClientDropItem(55, 66, S_vector(1.0f, 2.0f, 2.0f));
    CHECK(g_log.empty() && !h->m_inventoryChanged);
    g_gameRole = GAME_ROLE_CLIENT;
    h->ClientDropItem(55, 66, S_vector(1.0f, 2.0f, 2.0f));
    CHECK(Count("temp clientcreate 55 510 -1 66 at 1 2 3 vel 1 2 2") == 1 && h->m_inventoryChanged);
    g_gameRole = GAME_ROLE_LOCAL;

    // DropAllItems: the hands' weapon (filled), a mission explosive in the backpack's slot, the pick-ups and mission
    // explosives of the backpack's contents and of the belt (slots from the last, items from the end); the rest kept.
    const int respawning = FindTypeBy(ITEM_BEHAVIOUR_RESPAWNING);
    const int explosive = FindType([](C_ItemDef *d) {
        return d->GetBehaviour() == ITEM_BEHAVIOUR_MISSION_EXPLOSIVE && (d->GetAllowedContainers() & ITEM_FITS_BELT);
    });
    const int packExplosive = FindType([](C_ItemDef *d) {
        return d->GetBehaviour() == ITEM_BEHAVIOUR_MISSION_EXPLOSIVE && (d->GetAllowedContainers() & ITEM_FITS_BACKPACK);
    });
    std::printf("  drop types: respawning %d explosive %d backpack explosive %d\n", respawning, explosive, packExplosive);
    S_TestActor *d = Soldier(511);
    C_Human *dh = d->human;
    C_WeaponItem *w = static_cast<C_WeaponItem *>(Give(d, g_types.rifle, ITEM_CONTAINER_HANDS));
    w->m_rounds = 1.0f;
    C_Item *pack = packExplosive >= 0 ? Give(d, packExplosive, ITEM_CONTAINER_BACKPACK) : nullptr;
    C_Item *keptAmmo = Give(d, g_types.ammo, ITEM_CONTAINER_BELT);
    std::vector<C_Item *> beltDrops;
    if (respawning >= 0) {
        beltDrops.push_back(Give(d, respawning, ITEM_CONTAINER_BELT));
        beltDrops.push_back(Give(d, respawning, ITEM_CONTAINER_BELT));
    }
    C_Item *keptGrenade = Give(d, g_types.grenade, ITEM_CONTAINER_BELT);
    C_Item *beltExplosive = explosive >= 0 ? Give(d, explosive, ITEM_CONTAINER_BELT) : nullptr;
    const S_vector base = S_vector(0.0f, 1.0f, 1.0f).Normalized() * 2.0f;
    std::vector<std::string> want;
    const auto expect = [&](C_Item *item, const S_vector &b) {
        if (item)
            want.push_back("temp drop " + std::to_string(item->GetUid()) + " at 1 2 3 vel " + V(NextDrop(b)) + " 1");
    };
    std::srand(99);
    expect(w, base);
    if (pack)
        expect(pack, base);
    expect(beltExplosive, base);                       // (the last slot first; one item)
    for (size_t i = beltDrops.size(); i-- > 0;)        // (one slot: from its end)
        expect(beltDrops[i], base);
    std::srand(99);
    g_log.clear();
    dh->DropAllItems();
    const std::vector<std::string> got = Drops();
    CHECK(got == want && !want.empty());
    if (got != want) {
        for (const std::string &l : got)
            std::printf("  got  %s\n", l.c_str());
        for (const std::string &l : want)
            std::printf("  want %s\n", l.c_str());
    }
    CHECK(w->m_rounds == w->GetMaxAmount() && dh->CountItems(g_types.ammo) == 1 && dh->FindItemByUid(keptGrenade->GetUid()) == keptGrenade &&
          dh->FindItemByUid(keptAmmo->GetUid()) == keptAmmo && !dh->GetHandsItem());
    // The backpack's contents: its pick-ups too (before the belt's); a backpack that is not a mission explosive
    // stays; a hands item that holds nothing (max amount 0 or less: the knife) stays.
    S_TestActor *e = Soldier(512);
    C_Item *knife = Give(e, g_types.knife, ITEM_CONTAINER_HANDS);
    C_Item *bag = Give(e, g_types.backpack, ITEM_CONTAINER_BACKPACK);
    C_Item *inPack = respawning >= 0 ? Give(e, respawning, ITEM_CONTAINER_BACKPACK_CONTENTS) : nullptr;
    C_Item *packAmmo = Give(e, g_types.ammo, ITEM_CONTAINER_BACKPACK_CONTENTS);
    C_Item *onBelt = respawning >= 0 ? Give(e, respawning, ITEM_CONTAINER_BELT) : nullptr;
    want.clear();
    std::srand(5);
    expect(inPack, base);
    expect(onBelt, base);
    std::srand(5);
    g_log.clear();
    e->human->DropAllItems();
    CHECK(Drops() == want && knife->GetMaxAmount() <= 0.0f && e->human->GetHandsItem() == knife &&
          e->human->GetContainer(ITEM_CONTAINER_BACKPACK)->GetItem() == bag && e->human->FindItemByUid(packAmmo->GetUid()) == packAmmo);
    // Other views: up and along them (straight down: a zero base).
    S_TestActor *f = Soldier(513);
    C_Item *f1 = Give(f, g_types.rifle, ITEM_CONTAINER_HANDS);
    g_viewDir = S_vector(0.0f, -1.0f, 0.0f);
    want.clear();
    std::srand(6);
    // (Up plus straight down is zero: what LS3D's normalising makes of it.)
    expect(f1, S_vector(0.0f, 0.0f, 0.0f).Normalized() * 2.0f);
    std::srand(6);
    g_log.clear();
    f->human->DropAllItems();
    CHECK(Drops() == want);
    C_Item *f2 = Give(f, g_types.rifle, ITEM_CONTAINER_HANDS);
    g_viewDir = S_vector(3.0f, 0.0f, 0.0f);
    want.clear();
    std::srand(7);
    expect(f2, S_vector(3.0f, 1.0f, 0.0f).Normalized() * 2.0f);
    std::srand(7);
    g_log.clear();
    f->human->DropAllItems();
    CHECK(Drops() == want);
    g_viewDir = S_vector(0.0f, 0.0f, 1.0f);
    // Nothing to drop: nothing.
    g_log.clear();
    f->human->DropAllItems();
    CHECK(Drops().empty());
    // A non-weapon that holds something (ammunition) in the hands: dropped as it is (the original writes the
    // weapon's rounds field whatever the kind; the port's fix leaves other kinds alone). A mission explosive in the
    // backpack's slot (put there directly: the table has none that fits it): dropped.
    S_TestActor *g = Soldier(514);
    C_Item *r = nullptr;
    C_Item *handAmmo = New(g_types.ammo);
    handAmmo->SetUid(8200);
    static_cast<C_AmmoItem *>(handAmmo)->SetAmount(3.0f);
    g->human->m_containers[ITEM_CONTAINER_HANDS]->AddItem(handAmmo, &r, -1);
    C_Item *bagExplosive = explosive >= 0 ? New(explosive) : nullptr;
    if (bagExplosive) {
        bagExplosive->SetUid(8201);
        g->human->m_containers[ITEM_CONTAINER_BACKPACK]->AddItem(bagExplosive, &r, -1);
    }
    CHECK(handAmmo->GetMaxAmount() > 0.0f && handAmmo->GetKind() != ITEM_KIND_WEAPON);
    want.clear();
    std::srand(8);
    expect(handAmmo, base);
    expect(bagExplosive, base);
    std::srand(8);
    g_log.clear();
    g->human->DropAllItems();
    CHECK(Drops() == want && handAmmo->GetAmount() == 3.0f);
}

// ============================================================ supply boxes, picking up
C_SupplyBox *Box(int netId, int charges)
{
    auto *box = Raw<C_SupplyBox>();
    *reinterpret_cast<void ***>(box) = g_actorVt;
    box->m_netId = netId;
    box->m_charges = charges;
    box->m_flags = C_SupplyBox::SUPPLY_SHOWN;
    box->m_state = C_SupplyBox::SUPPLY_READY;
    g_actorsById[netId] = box;
    return box;
}
void TestSupplyBoxes()
{
    S_TestActor *s = Soldier(520);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    int container = -5, uid = -5;
    // No box, no charges, busy, not placed: false.
    CHECK(!h->TakeFromSupplyBox(521, 0, &container, &uid));
    C_SupplyBox *box = Box(521, 0);
    CHECK(!h->TakeFromSupplyBox(521, 0, &container, &uid));
    box->m_charges = 2;
    box->m_state = 1;
    CHECK(!h->TakeFromSupplyBox(521, 0, &container, &uid));
    box->m_state = C_SupplyBox::SUPPLY_READY;
    box->m_flags = 0;
    CHECK(!h->TakeFromSupplyBox(521, 0, &container, &uid));
    box->m_flags = C_SupplyBox::SUPPLY_SHOWN;
    // Without item lists: ammunition for the hands' weapon (none in the hands: false).
    CHECK(!h->TakeFromSupplyBox(521, 0, &container, &uid) && box->m_charges == 2 && container == -5 && uid == -5);
    C_Item *gun = Give(s, g_types.rifle, ITEM_CONTAINER_HANDS);
    const int ammoType = gun->GetDef()->GetAmmoItemId();
    const size_t instances = g_itemsModule.m_instances.size();
    g_log.clear();
    CHECK(h->TakeFromSupplyBox(521, 0, &container, &uid));
    C_Item *got = h->FindItemByUid(uid);
    CHECK(got && got->GetTypeId() == ammoType && container == ITEM_CONTAINER_BELT && box->m_charges == 1 && Count("box charge") == 1 &&
          h->m_inventoryChanged && g_itemsModule.m_instances.size() == instances + 1);
    // Charges below zero count on down; zero stops.
    box->m_charges = -1;
    CHECK(h->TakeFromSupplyBox(521, 0, &container, &uid) && box->m_charges == -2);
    // With item lists: the list of the human's team (the allies' for every team but the axis), by index.
    box->m_hasItemLists = 1;
    box->m_alliedItems = {g_types.grenade, g_types.helmet};
    box->m_axisItems = {g_types.mine, g_types.medkit};
    CHECK(h->TakeFromSupplyBox(521, 0, &container, &uid) && h->FindItemByUid(uid)->GetTypeId() == g_types.grenade);
    CHECK(h->TakeFromSupplyBox(521, 1, &container, &uid) && h->FindItemByUid(uid)->GetTypeId() == g_types.helmet && container == ITEM_CONTAINER_HEAD);
    g_teams[h] = TEAM_AXIS;
    CHECK(h->TakeFromSupplyBox(521, 1, &container, &uid) && h->FindItemByUid(uid)->GetTypeId() == g_types.medkit);
    g_teams[h] = 2;
    CHECK(h->TakeFromSupplyBox(521, 0, &container, &uid) && h->FindItemByUid(uid)->GetTypeId() == g_types.grenade);
    // No room for it (a second helmet): destroyed again, false, no charge used.
    g_teams.erase(h);
    const int charges = box->m_charges;
    const size_t n = g_itemsModule.m_instances.size();
    uid = -5;
    CHECK(!h->TakeFromSupplyBox(521, 1, &container, &uid) && uid == -5 && box->m_charges == charges && g_itemsModule.m_instances.size() == n);

    // The client's side: not on a server; no box: nothing; the item made with the server's uid in its container; a
    // charge used only when the box is ready and placed.
    g_log.clear();
    h->ClientTakeFromSupplyBox(521, 0, ITEM_CONTAINER_BELT, 7001);
    CHECK(!h->FindItemByUid(7001) && g_log.empty());
    g_gameRole = GAME_ROLE_CLIENT;
    h->ClientTakeFromSupplyBox(599, 0, ITEM_CONTAINER_BELT, 7001);
    CHECK(!h->FindItemByUid(7001));
    h->m_inventoryChanged = false;
    h->ClientTakeFromSupplyBox(521, 0, ITEM_CONTAINER_BELT, 7001);
    CHECK(h->FindItemInContainer(7001, ITEM_CONTAINER_BELT) && h->FindItemByUid(7001)->GetTypeId() == g_types.grenade &&
          box->m_charges == charges - 1 && h->m_inventoryChanged);
    box->m_state = 1;
    h->ClientTakeFromSupplyBox(521, 0, ITEM_CONTAINER_BELT, 7002);
    CHECK(h->FindItemByUid(7002) && box->m_charges == charges - 1);
    box->m_state = C_SupplyBox::SUPPLY_READY;
    box->m_flags = 0;
    h->ClientTakeFromSupplyBox(521, 0, ITEM_CONTAINER_BELT, 7003);
    CHECK(h->FindItemByUid(7003) && box->m_charges == charges - 1);
    box->m_flags = C_SupplyBox::SUPPLY_SHOWN;
    // Without lists: the hands' weapon's ammunition; no weapon: nothing.
    box->m_hasItemLists = 0;
    h->ClientTakeFromSupplyBox(521, 0, ITEM_CONTAINER_BELT, 7004);
    CHECK(h->FindItemByUid(7004) && h->FindItemByUid(7004)->GetTypeId() == ammoType);
    S_TestActor *bare = Soldier(522);
    bare->human->ClientTakeFromSupplyBox(521, 0, ITEM_CONTAINER_BELT, 7005);
    CHECK(!bare->human->FindItemByUid(7005) && box->m_charges == charges - 2);
    g_gameRole = GAME_ROLE_LOCAL;
}
void TestPickingUp()
{
    S_TestActor *s = Soldier(530);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    int container = -5;
    CHECK(!h->PickUpItem(40, &container));
    // (A grenade is a weapon kind: into the empty hands first.)
    auto *actor = Raw<C_ItemActor>();
    actor->m_netId = 40;
    C_Item *grenade = New(g_types.grenade);
    grenade->SetUid(8001);
    actor->m_item = grenade;
    g_tempActors[40] = actor;
    g_log.clear();
    CHECK(h->PickUpItem(40, &container));
    CHECK(container == ITEM_CONTAINER_HANDS && h->FindItemByUid(8001) == grenade && actor->m_item == nullptr && Count("itemactor release") == 1 &&
          Count("temp destroy 40") == 1 && h->m_inventoryChanged);
    // No room (a second helmet): false, the actor kept.
    Give(s, g_types.helmet, ITEM_CONTAINER_HEAD);
    C_Item *helmet = New(g_types.helmet);
    actor->m_item = helmet;
    h->m_inventoryChanged = false;
    g_log.clear();
    CHECK(!h->PickUpItem(40, &container) && actor->m_item == helmet && !Logged("temp destroy") && !h->m_inventoryChanged);
    // The client's side: not on a server; no actor: nothing; else moved into the container named.
    C_Item *ammo = New(g_types.ammo);
    ammo->SetUid(8002);
    actor->m_item = ammo;
    h->ClientPickUpItem(40, ITEM_CONTAINER_BELT);
    CHECK(!h->FindItemByUid(8002) && actor->m_item == ammo);
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    h->ClientPickUpItem(41, ITEM_CONTAINER_BELT);
    CHECK(Count("temp find 41") == 1 && !Logged("temp destroy"));
    h->ClientPickUpItem(40, ITEM_CONTAINER_BELT);
    CHECK(h->FindItemInContainer(8002, ITEM_CONTAINER_BELT) == ammo && actor->m_item == nullptr && Count("temp destroy 40") == 1 && h->m_inventoryChanged);
    g_gameRole = GAME_ROLE_LOCAL;
}

// ============================================================ uniforms
void *g_sightVt[16];
void __attribute__((thiscall)) F_SightRefresh(C_SightTarget *) { Log("sight refresh"); }
void SetModelFile(C_Human *h, const char *name) { *reinterpret_cast<const char **>(reinterpret_cast<unsigned char *>(h->m_frame) + 0x134) = name; }
void TestUniforms()
{
    g_sightVt[VirtualSlot(&C_SightTarget::Refresh)] = reinterpret_cast<void *>(F_SightRefresh);
    auto *target = static_cast<C_SightTarget *>(::operator new(0x40));
    std::memset(static_cast<void *>(target), 0, 0x40);
    *reinterpret_cast<void ***>(target) = g_sightVt;
    g_sightTargets[61] = target;
    S_TestActor *s = Soldier(540);
    C_Human *h = s->human;
    C_Item *uniform = Give(s, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    const std::string model = uniform->GetDef()->GetModelName();
    std::printf("  uniform %d model %s\n", g_types.uniform, model.c_str());
    CHECK(model.size() >= 3);
    // Not found: false.
    g_log.clear();
    CHECK(!h->PutOnUniform(9999) && g_log.empty());
    // Before the mission runs: always dressed; the body frame found again; no sight target: no refresh.
    GetGame()->m_missionTime = 0;
    SetModelFile(h, model.c_str());
    h->m_bodyFrame = nullptr;
    CHECK(h->PutOnUniform(uniform->GetUid()));
    CHECK(Count("speech release 540") == 1 && Count("dress 540 " + std::to_string(g_types.uniform)) == 1 && h->m_bodyFrame &&
          FrameName(h->m_bodyFrame) == "f" && !Logged("sight") && h->m_inventoryChanged);
    SetModelFile(h, "Models\\other.i3d");
    // A sight target: the target the sight id names refreshed (the original looks it up by the observer id); none
    // found: no refresh.
    h->m_sightTargetId = 60;
    h->m_sightId = 61;
    g_log.clear();
    CHECK(h->PutOnUniform(uniform->GetUid()) && Count("sight target 61") == 1 && Count("sight refresh") == 1);
    h->m_sightId = 62;
    g_log.clear();
    CHECK(h->PutOnUniform(uniform->GetUid()) && Count("sight target 62") == 1 && !Logged("sight refresh"));
    // Once the mission runs: a uniform whose model name holds the model's file name is worn already (true, nothing
    // done); another is dressed.
    GetGame()->m_missionTime = 5000;
    static std::string part;
    part = model.substr(1, model.size() - 2);
    SetModelFile(h, part.c_str());
    h->m_inventoryChanged = false;
    g_log.clear();
    CHECK(h->PutOnUniform(uniform->GetUid()) && g_log.empty() && !h->m_inventoryChanged);
    SetModelFile(h, "Models\\other.i3d");
    CHECK(h->PutOnUniform(uniform->GetUid()) && Count("dress 540 " + std::to_string(g_types.uniform)) == 1);
    // (No file name: "" is in every name: worn.)
    SetModelFile(h, nullptr);
    g_log.clear();
    CHECK(h->PutOnUniform(uniform->GetUid()) && g_log.empty());

    // The client's side: not on a server; the same without the sight target.
    SetModelFile(h, "Models\\other.i3d");
    h->m_sightId = 61;
    h->m_inventoryChanged = false;
    g_log.clear();
    h->ClientPutOnUniform(uniform->GetUid());
    CHECK(g_log.empty() && !h->m_inventoryChanged);
    g_gameRole = GAME_ROLE_CLIENT;
    h->ClientPutOnUniform(9999);
    CHECK(g_log.empty());
    h->m_bodyFrame = nullptr;
    h->ClientPutOnUniform(uniform->GetUid());
    CHECK(Count("speech release 540") == 1 && Count("dress 540 " + std::to_string(g_types.uniform)) == 1 && FrameName(h->m_bodyFrame) == "f" &&
          !Logged("sight") && h->m_inventoryChanged);
    SetModelFile(h, part.c_str());
    g_log.clear();
    h->ClientPutOnUniform(uniform->GetUid());
    CHECK(g_log.empty());
    GetGame()->m_missionTime = 0;
    h->ClientPutOnUniform(uniform->GetUid());
    CHECK(Logged("dress"));
    GetGame()->m_missionTime = 5000;
    g_gameRole = GAME_ROLE_LOCAL;
}

// ============================================================ the soldier state
bool Entry(const S_SoldierItem &e, const C_Item *item, bool uids)
{
    if (!item)
        return e.present == 0 && e.typeId == -1 && e.uid == -1;
    return e.present == 1 && e.typeId == item->GetTypeId() && e.uid == (uids ? item->GetUid() : -1);
}
std::vector<std::pair<int, int>> Entries(const std::vector<S_SoldierItem> &list)
{
    std::vector<std::pair<int, int>> out;
    for (const S_SoldierItem &e : list)
        out.push_back({e.present == 1 ? e.typeId : -99, e.uid});
    return out;
}
void TestSoldierState()
{
    S_TestActor *s = Soldier(550);
    C_Human *h = s->human;
    GetGame()->m_missionTime = 5000;
    // Empty: nothing.
    {
        S_SoldierState st;
        h->GetEquipmentState(st);
        CHECK(Entry(st.head, nullptr, false) && Entry(st.hands, nullptr, false) && Entry(st.longGun, nullptr, false) && st.belt.empty() &&
              st.backpackContents.empty());
    }
    C_Item *helmet = Give(s, g_types.helmet, ITEM_CONTAINER_HEAD);
    C_Item *uniform = Give(s, g_types.uniform, ITEM_CONTAINER_UNIFORM);
    C_Item *pack = Give(s, g_types.backpack, ITEM_CONTAINER_BACKPACK);
    C_Item *longGun = Give(s, g_types.longGun, ITEM_CONTAINER_LONG_GUN);
    C_Item *hands = Give(s, g_types.pistol, ITEM_CONTAINER_HANDS);
    C_Item *a1 = Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    C_Item *a2 = Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    C_Item *g1 = Give(s, g_types.grenade, ITEM_CONTAINER_BELT);
    C_Item *in1 = Give(s, g_types.mine, ITEM_CONTAINER_BACKPACK_CONTENTS);
    // A local game: types without uids; the stores in slot order, each slot from its start.
    for (const bool local : {true, false}) {
        g_gameRole = local ? GAME_ROLE_LOCAL : GAME_ROLE_SERVER;
        S_SoldierState st;
        h->GetEquipmentState(st);
        CHECK(Entry(st.head, helmet, !local) && Entry(st.uniform, uniform, !local) && Entry(st.backpack, pack, !local) &&
              Entry(st.longGun, longGun, !local) && Entry(st.hands, hands, !local));
        const auto e = [local](C_Item *item) { return std::make_pair(item->GetTypeId(), local ? -1 : item->GetUid()); };
        const std::vector<std::pair<int, int>> belt = {e(a1), e(a2), e(g1)}, contents = {e(in1)};
        CHECK(Entries(st.belt) == belt && Entries(st.backpackContents) == contents);
    }
    g_gameRole = GAME_ROLE_LOCAL;
    // Item 38 (the one-shot launcher) empty: left out in single player only; with a round, kept.
    S_TestActor *l = Soldier(551);
    C_Item *r = nullptr;
    C_Item *launcher = New(38);
    launcher->SetUid(8100);
    l->human->m_containers[ITEM_CONTAINER_HANDS]->AddItem(launcher, &r, -1);
    C_Item *launcher2 = New(38);
    launcher2->SetUid(8101);
    l->human->m_containers[ITEM_CONTAINER_LONG_GUN]->AddItem(launcher2, &r, -1);
    static_cast<C_WeaponItem *>(launcher)->m_rounds = 0.0f;
    static_cast<C_WeaponItem *>(launcher2)->m_rounds = 0.0f;
    for (const bool local : {true, false}) {
        g_gameRole = local ? GAME_ROLE_LOCAL : GAME_ROLE_SERVER;
        S_SoldierState st;
        l->human->GetEquipmentState(st);
        CHECK(Entry(st.hands, local ? nullptr : launcher, !local) && Entry(st.longGun, local ? nullptr : launcher2, !local));
    }
    g_gameRole = GAME_ROLE_LOCAL;
    static_cast<C_WeaponItem *>(launcher)->m_rounds = 1.0f;
    static_cast<C_WeaponItem *>(launcher2)->m_rounds = 1.0f;
    {
        S_SoldierState st;
        l->human->GetEquipmentState(st);
        CHECK(Entry(st.hands, launcher, false) && Entry(st.longGun, launcher2, false));
    }
    // Another type empty: kept.
    S_TestActor *m = Soldier(552);
    C_Item *emptyGun = Give(m, g_types.rifle, ITEM_CONTAINER_HANDS);
    static_cast<C_WeaponItem *>(emptyGun)->m_rounds = 0.0f;
    {
        S_SoldierState st;
        m->human->GetEquipmentState(st);
        CHECK(Entry(st.hands, emptyGun, false));
    }

    // GetSoldierState: health, team, profile, progress, the equipment; the VIP and spectating flags only for a
    // player controller outside a local game.
    h->m_stats[STAT_HEALTH] = 42;
    h->m_profileIndex = 3;
    h->m_progress.morale = 0.25f;
    h->m_progress.stealth = 1.5f;
    g_teams[h] = TEAM_AXIS;
    static_cast<C_PlayerController *>(h->m_controller)->m_vip = true;
    g_spectating = true;
    {
        S_SoldierState st;
        h->GetSoldierState(st);
        CHECK(st.health == 42 && st.team == TEAM_AXIS && st.profileIndex == 3 && st.progress.morale == 0.25f && st.progress.stealth == 1.5f &&
              !st.vip && !st.spectating && Entry(st.head, helmet, false));
    }
    g_gameRole = GAME_ROLE_SERVER;
    {
        S_SoldierState st;
        h->GetSoldierState(st);
        CHECK(st.vip && st.spectating && Entry(st.head, helmet, true));
    }
    h->m_controller->m_type = HUMAN_CONTROLLER_AI;
    {
        S_SoldierState st;
        h->GetSoldierState(st);
        CHECK(!st.vip && !st.spectating);
    }
    h->m_controller->m_type = HUMAN_CONTROLLER_PLAYER;
    g_spectating = false;
    static_cast<C_PlayerController *>(h->m_controller)->m_vip = false;
    {
        S_SoldierState st;
        st.vip = st.spectating = true;
        h->GetSoldierState(st);
        CHECK(!st.vip && !st.spectating);
    }
    g_gameRole = GAME_ROLE_LOCAL;
    g_teams.erase(h);
}

// ============================================================ carried bodies, the action's progress
void *g_vehVt[256];
bool g_seatWeapon;
bool __attribute__((thiscall)) F_HasSeatWeapon(C_Actor *, int seat)
{
    Log("hasseatweapon " + std::to_string(seat));
    return g_seatWeapon;
}
bool __attribute__((thiscall)) F_UnknownC8(C_Actor *, int seat, int *a, int *b, int *c)
{
    Log("c8 " + std::to_string(seat));
    *a = 1;
    *b = 2;
    *c = 3;
    return true;
}
void TestCarriedAndProgress()
{
    S_TestActor *s = Soldier(560);
    C_Human *h = s->human;
    S_TestActor *body = Soldier(561);
    // Not carrying, or not a local game: nothing.
    g_log.clear();
    h->DropCarried();
    h->m_carried = body->human;
    g_gameRole = GAME_ROLE_SERVER;
    h->DropCarried();
    CHECK(g_log.empty());
    g_gameRole = GAME_ROLE_LOCAL;
    h->DropCarried();
    CHECK(Count("command " + std::to_string(HUMAN_COMMAND_VEHICLE) + " " + std::to_string(HUMAN_COMMAND_LEAVE_VEHICLE) + " -1 0") == 1 &&
          Count("interface actor " + std::to_string(INTERFACE_ACTOR_DROPPED) + " 561") == 1 && g_log.size() == 2 &&
          At("command") < At("interface"));
    h->m_carried = nullptr;

    // The action's progress: a vehicle seat's weapon first (the four kinds), then the human's own action, then (not
    // in a local game) a running zone's.
    g_vehVt[VirtualSlot(&C_Actor::HasSeatWeapon)] = reinterpret_cast<void *>(F_HasSeatWeapon);
    g_vehVt[VirtualSlot(&C_Actor::UnknownC8)] = reinterpret_cast<void *>(F_UnknownC8);
    S_TestActor *v = Vehicle(562);
    *reinterpret_cast<void ***>(v->human) = g_vehVt;
    int t = -1, total = -1, text = -1;
    CHECK(!h->GetActionProgress(&t, &total, &text) && t == -1);
    h->m_actionTime = 400;
    h->m_actionTotal = 900;
    h->m_actionText = 12;
    CHECK(h->GetActionProgress(&t, &total, &text) && t == 400 && total == 900 && text == 12);
    h->m_vehicle = v->human;
    h->m_seat = 2;
    for (int type = 0; type < ACTOR_TYPE_COUNT; ++type) {
        v->human->m_type = static_cast<E_ActorType>(type);
        const bool kind = type == ACTOR_CAR || type == ACTOR_SUBMARINE || type == ACTOR_STATIC_WEAPON || type == ACTOR_JUNKERS88;
        for (const bool weapon : {false, true}) {
            g_seatWeapon = weapon;
            t = total = text = -1;
            g_log.clear();
            const bool got = h->GetActionProgress(&t, &total, &text);
            if (kind && weapon)
                CHECK(got && t == 1 && total == 2 && text == 3 && Count("hasseatweapon 2") == 1 && Count("c8 2") == 1);
            else
                CHECK(got && t == 400 && total == 900 && text == 12 && Count("hasseatweapon 2") == (kind ? 1 : 0) && !Logged("c8"));
        }
    }
    h->m_vehicle = nullptr;
    h->m_actionTime = 0;
    // A zone: not in a local game; the zone actor's id, a zone, running.
    auto *zone = Raw<C_MPZone>();
    *reinterpret_cast<void ***>(zone) = g_actorVt;
    zone->m_netId = 563;
    zone->m_type = ACTOR_MP_ZONE;
    zone->m_progressShown = true;
    g_actorsById[563] = zone;
    h->m_mpZone = 563;
    CHECK(!h->GetActionProgress(&t, &total, &text) && h->GetActiveMPZone() == 563);
    g_gameRole = GAME_ROLE_CLIENT;
    t = total = text = -1;
    CHECK(h->GetActionProgress(&t, &total, &text) && t == 11 && total == 22 && text == 33);
    zone->m_progressShown = false;
    CHECK(h->GetActiveMPZone() == -1 && !h->GetActionProgress(&t, &total, &text));
    zone->m_progressShown = true;
    zone->m_type = ACTOR_CAR;
    CHECK(h->GetActiveMPZone() == -1 && !h->GetActionProgress(&t, &total, &text));
    zone->m_type = ACTOR_MP_ZONE;
    h->m_mpZone = 564;
    CHECK(h->GetActiveMPZone() == -1 && !h->GetActionProgress(&t, &total, &text));
    g_gameRole = GAME_ROLE_LOCAL;
}

// ============================================================ part 4b: what the first mutation run showed untested
void TestPart4Edges()
{
    GetGame()->m_missionTime = 5000;
    // Init on an object marked single-container: two-way.
    C_Human *raw = Raw<C_Human>(0x100);
    raw->m_singleContainer = true;
    raw->C_Human::Init();
    CHECK(!raw->m_singleContainer);
    raw->m_containers[ITEM_CONTAINER_BACKPACK_CONTENTS] = nullptr;
    raw->C_Human::Done();

    // CountItems: a type only in the backpack's contents while the belt holds another.
    S_TestActor *s = Soldier(570);
    C_Human *h = s->human;
    Give(s, g_types.ammo, ITEM_CONTAINER_BELT);
    Give(s, g_types.backpack, ITEM_CONTAINER_BACKPACK);
    Give(s, g_types.mine, ITEM_CONTAINER_BACKPACK_CONTENTS);
    CHECK(h->CountItems(g_types.mine) == 1 && h->CountItems(g_types.knife) == 0);

    // CreateItemInStore: a new uid (the next).
    g_itemsModule.m_freedUids.clear();
    g_itemsModule.m_lastUid = 700;
    h->CreateItemInStore(g_types.helmet, ITEM_CONTAINER_HEAD);
    CHECK(h->GetContainer(ITEM_CONTAINER_HEAD)->GetItem() && h->GetContainer(ITEM_CONTAINER_HEAD)->GetItem()->GetUid() == 701);

    // DrawSidearm with only a sidearm on the belt (one slot).
    S_TestActor *d = Soldier(571);
    C_Item *sidearm = Give(d, FindType([](C_ItemDef *def) { return def->GetBehaviour() == ITEM_BEHAVIOUR_SIDEARM && def->GetInventorySize() == 1; }),
                           ITEM_CONTAINER_BELT);
    g_log.clear();
    CHECK(d->human->GetContainer(ITEM_CONTAINER_BELT)->GetUsedSlotCount() == 1 && d->human->DrawSidearm(2) &&
          Count("ctrl move " + std::to_string(sidearm->GetUid()) + " -1 " + std::to_string(ITEM_CONTAINER_BELT) + " " +
                std::to_string(ITEM_CONTAINER_HANDS) + " 0") == 1);

    // The mission running: the moves tell the equipment (notify), a locked container refuses (locks not ignored).
    Mission(true);
    S_TestActor *a = Soldier(572);
    S_TestActor *b = Soldier(573);
    C_Item *pistol = Give(b, g_types.pistol, ITEM_CONTAINER_HANDS);
    a->human->m_containers[ITEM_CONTAINER_HANDS]->SetLockTime(6000);
    int to = ITEM_CONTAINER_HANDS;
    CHECK(!a->human->ServerMoveItem(pistol->GetUid(), 573, ITEM_CONTAINER_HANDS, &to, -1) && b->human->GetHandsItem() == pistol);
    a->human->m_containers[ITEM_CONTAINER_HANDS]->SetLockTime(0);
    g_log.clear();
    CHECK(a->human->ServerMoveItem(pistol->GetUid(), 573, ITEM_CONTAINER_HANDS, &to, -1) && Logged("eq added"));
    Later();
    C_Item *ammo = Give(b, g_types.ammo, ITEM_CONTAINER_BELT);
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    a->human->ClientMoveItem(ammo->GetUid(), 573, ITEM_CONTAINER_BELT, ITEM_CONTAINER_BELT, -1);
    CHECK(a->human->FindItemByUid(ammo->GetUid()) == ammo && Logged("eq added"));
    g_gameRole = GAME_ROLE_LOCAL;
    // Taking from a box, picking up (server and client): told too (as a new item: "created"); the client's pick-up
    // appended to the belt.
    C_SupplyBox *box = Box(574, 1);                    // (one charge left: still one to take)
    int container = -5, uid = -5;
    g_log.clear();
    CHECK(a->human->TakeFromSupplyBox(574, 0, &container, &uid) && Logged("eq created") && box->m_charges == 0);
    auto *actor = Raw<C_ItemActor>();
    actor->m_netId = 42;
    C_Item *grenade = New(g_types.grenade);
    grenade->SetUid(8300);
    actor->m_item = grenade;
    g_tempActors[42] = actor;
    g_log.clear();
    CHECK(a->human->PickUpItem(42, &container) && Logged("eq created"));
    C_Item *mine = New(g_types.mine);
    mine->SetUid(8301);
    actor->m_item = mine;
    C_ItemContainer *belt = a->human->GetContainer(ITEM_CONTAINER_BELT);
    const int slots = belt->GetUsedSlotCount();
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    a->human->ClientPickUpItem(42, ITEM_CONTAINER_BELT);
    CHECK(Logged("eq created") && belt->GetUsedSlotCount() == slots + 1 && belt->GetSlots()[slots].front() == mine);
    g_gameRole = GAME_ROLE_LOCAL;
    Mission(false);

    // A drop that makes temporary actor 0 succeeded.
    S_TestActor *e = Soldier(575);
    C_Item *dropped = Give(e, g_types.ammo, ITEM_CONTAINER_BELT);
    g_dropActorId = 0;
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    e->human->DropItem(dropped->GetUid(), S_vector(0.0f, 0.0f, 2.0f));
    CHECK(e->human->m_inventoryChanged && Count("ctrl dropped 0 " + std::to_string(dropped->GetUid()) + " dir 0 0 1 speed 2") == 1);
    g_gameRole = GAME_ROLE_LOCAL;
    g_dropActorId = 100;
    // DropAllItems: a hands weapon holding at most one round is dropped; one holding none (0) stays.
    C_WeaponItem *one = static_cast<C_WeaponItem *>(Give(e, g_types.rifle, ITEM_CONTAINER_HANDS));
    one->m_capacity = 1.0f;
    g_log.clear();
    e->human->DropAllItems();
    CHECK(Drops().size() == 1 && !e->human->GetHandsItem());
    C_WeaponItem *none = static_cast<C_WeaponItem *>(Give(e, g_types.rifle, ITEM_CONTAINER_HANDS));
    none->m_capacity = 0.0f;
    g_log.clear();
    e->human->DropAllItems();
    CHECK(Drops().empty() && e->human->GetHandsItem() == none);

    // The soldier state's team: GetTeam(false) (not the pending one).
    g_teams[e->human] = 2;
    g_teamPendingAsked.clear();
    S_SoldierState st;
    e->human->GetSoldierState(st);
    CHECK(st.team == 2 && g_teamPendingAsked == std::vector<bool>({false}));
    g_teams.erase(e->human);

    // A zone whose actor id is 0.
    auto *zone = Raw<C_MPZone>();
    *reinterpret_cast<void ***>(zone) = g_actorVt;
    zone->m_netId = 0;
    zone->m_type = ACTOR_MP_ZONE;
    zone->m_progressShown = true;
    g_actorsById[0] = zone;
    e->human->m_mpZone = 0;
    g_gameRole = GAME_ROLE_CLIENT;
    int t = -1, total = -1, text = -1;
    CHECK(e->human->GetActiveMPZone() == 0 && e->human->GetActionProgress(&t, &total, &text) && t == 11);
    g_gameRole = GAME_ROLE_LOCAL;
    g_actorsById.erase(0);
}

void RunHumanItemsTests()
{
    InitControllerVt();
    Mission(false);
    TestHumanContainers();
    TestHumanLookups();
    TestBeltItems();
    TestRemoving();
    TestRequests();
    TestDraw();
    TestServerClientMoves();
    TestDropping();
    TestSupplyBoxes();
    TestPickingUp();
    TestUniforms();
    TestSoldierState();
    TestCarriedAndProgress();
    TestPart4Edges();
}
}  // namespace
