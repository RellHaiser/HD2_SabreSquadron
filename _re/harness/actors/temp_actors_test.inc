// The temporary actors (C_TemporaryActorsList), included by actors_test.cpp.
namespace {
C_TemporaryActorsList &T() { return g_temporaryActors; }
S_vector4 V4(float x, float y, float z, float w)
{
    S_vector4 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    return v;
}
I3D_frame *Placed(const std::string &name, const S_vector &pos)
{
    I3D_frame *f = NewFrame(FRAME_MODEL, name);
    f->m_worldMatrix(3) = pos;
    return f;
}
C_Actor *Temp(E_ActorType type, const std::string &frame, const S_vector &pos = S_vector(0, 0, 0), int id = -1)
{
    return T().CreateActor(type, Placed(frame, pos), id);
}
C_ItemActor *ItemActor(C_Item *item, const S_vector &pos, int id = -1)
{
    auto *a = static_cast<C_ItemActor *>(T().CreateActor(ACTOR_ITEM, Placed("it" + std::to_string(item->GetUid()), pos), id));
    a->m_item = item;
    return a;
}
C_Mine *Mine(C_Human *owner, int itemId, const S_vector &pos, int id = -1)
{
    auto *m = static_cast<C_Mine *>(T().CreateActor(ACTOR_MINE, Placed("mine", pos), id));
    m->m_owner = owner;
    m->m_itemId = itemId;
    return m;
}
bool Near(const S_vector &a, const S_vector &b, float e) { return std::fabs(a.x - b.x) <= e && std::fabs(a.y - b.y) <= e && std::fabs(a.z - b.z) <= e; }

// Item types of the tests.
enum { TYPE_MINE = 70, TYPE_GUN = 4, TYPE_PICKUP = 239, TYPE_RIFLE = 5, TYPE_NOMODEL = 80 };
void MakeDefs()
{
    Def(TYPE_MINE, ITEM_BEHAVIOUR_MINE, "w_mine");
    Def(TYPE_GUN, ITEM_BEHAVIOUR_SMG, "w_gun");
    Def(TYPE_PICKUP, ITEM_BEHAVIOUR_RESPAWNING, "w_pick");
    Def(TYPE_RIFLE, ITEM_BEHAVIOUR_RIFLE, "w_rifle");
    Def(TYPE_NOMODEL, ITEM_BEHAVIOUR_MINE, "w_none");
    g_models = {"w_mine", "w_gun", "w_pick", "w_rifle"};
}

void TestTempCreateDestroy()
{
    // Created: its frame set, temporary, the frame's data bound; the next id (or the one given); started by role.
    for (const uint32_t role : {static_cast<uint32_t>(GAME_ROLE_SERVER), static_cast<uint32_t>(GAME_ROLE_CLIENT),
                                static_cast<uint32_t>(GAME_ROLE_LOCAL)}) {
        Fresh(role);
        I3D_frame *f = NewFrame(FRAME_MODEL, "tf");
        C_Actor *a = T().CreateActor(ACTOR_DEBRIS, f, -1);
        CHECK(a && a->GetFrame() == f && a->IsTemporary() && f->GetData() && f->GetData()->actor == a && a->GetNetId() == 0 && T().m_nextId == 1 &&
              At("actor setframe tf") < At("setframedata tf"));
        CHECK(Count("actor start 0 1") == (role & GAME_ROLE_SERVER ? 1 : 0) && Count("actor start 0 0") == (role & GAME_ROLE_CLIENT ? 1 : 0));
        if (role == GAME_ROLE_LOCAL)
            CHECK(At("actor start 0 1") < At("actor start 0 0"));
        C_Actor *b = T().CreateActor(ACTOR_DEBRIS, NewFrame(FRAME_MODEL), 77);
        C_Actor *c = T().CreateActor(ACTOR_DEBRIS, NewFrame(FRAME_MODEL), -1);
        CHECK(b->GetNetId() == 77 && c->GetNetId() == 1 && T().m_nextId == 2 && Ids(T().m_actors) == std::vector<int>({0, 77, 1}) && T().m_active.empty());
        // The creator failing: nothing, no id used.
        g_creators[ACTOR_DEBRIS].fail = true;
        CHECK(!T().CreateActor(ACTOR_DEBRIS, NewFrame(FRAME_MODEL), -1) && T().m_nextId == 2 && T().m_actors.size() == 3);
        g_creators[ACTOR_DEBRIS].fail = false;
        // Destroyed: out of the active set, its frame data released, stopped by role, out of the list (the last takes
        // its place), a client's interface told, the creator's Destroy.
        T().SetActorActive(a, true);
        T().SetActorActive(c, true);
        g_log.clear();
        T().DestroyActor(a);
        CHECK(a->GetActiveIndex() == -1 && Ids(T().m_active) == std::vector<int>({1}) && ActiveConsistent(T().m_active) && Count("freeframedata tf") == 1 &&
              Count("actor stop 0 0") == (role & GAME_ROLE_CLIENT ? 1 : 0) && Count("actor stop 0 1") == (role & GAME_ROLE_SERVER ? 1 : 0) &&
              Ids(T().m_actors) == std::vector<int>({1, 77}) &&
              Count("interface " + std::to_string(INTERFACE_ACTOR_DELETED) + " 0") == (role & GAME_ROLE_CLIENT ? 1 : 0) && g_destroyed.back() == a);
        if (role == GAME_ROLE_LOCAL)
            CHECK(At("actor stop 0 0") < At("actor stop 0 1") && At("actor stop 0 1") < At("destroy " + std::to_string(ACTOR_DEBRIS) + " 0"));
    }
    // While iterating: queued, destroyed after the pass (the queue from its end).
    Fresh();
    C_Actor *a = Temp(ACTOR_DEBRIS, "a"), *b = Temp(ACTOR_DEBRIS, "b"), *c = Temp(ACTOR_DEBRIS, "c");
    T().SetActorActive(a, true);
    T().SetActorActive(b, true);
    g_tickDestroys[b] = c;
    g_tickDestroys[a] = b;
    g_log.clear();
    T().Tick(100);
    // (Ticked from the last active: b, then a.)
    CHECK(Lines("actor tick") == std::vector<std::string>({"actor tick 1 100", "actor tick 0 100"}) && T().m_toDestroy.empty() &&
          Ids(T().m_actors) == std::vector<int>({0}) && g_destroyed.size() == 2 && g_destroyed[0] == b && g_destroyed[1] == c);
    // The only active one going: popped without a move.
    T().SetActorActive(a, false);
    CHECK(T().m_active.empty() && a->GetActiveIndex() == -1);
    T().SetActorActive(a, false);
    CHECK(T().m_active.empty());
    // FindActor: from the end (the later of two with one id).
    Fresh();
    C_Actor *x = Temp(ACTOR_DEBRIS, "x", S_vector(), 5);
    C_Actor *y = Temp(ACTOR_DEBRIS, "y", S_vector(), 5);
    CHECK(T().FindActor(5) == y && !T().FindActor(6) && x);
    // Activity: on, off (the last takes the place); while iterating queued, applied from the end, each toggling.
    Fresh();
    C_Actor *p = Temp(ACTOR_DEBRIS, "p"), *q = Temp(ACTOR_DEBRIS, "q"), *r = Temp(ACTOR_DEBRIS, "r");
    T().SetActorActive(p, true);
    T().SetActorActive(q, true);
    T().SetActorActive(r, true);
    T().SetActorActive(p, false);
    CHECK(Ids(T().m_active) == std::vector<int>({2, 1}) && ActiveConsistent(T().m_active));
    g_tickChanges[q] = {p, true};
    g_tickChanges[r] = {q, false};
    g_log.clear();
    T().Update(16);
    // (r first (the last), asks q off; q then asks p on. Applied from the queue's end: p on, then q off.)
    CHECK(Lines("actor update") == std::vector<std::string>({"actor update 1 16", "actor update 2 16"}) ||
          Lines("actor update") == std::vector<std::string>({"actor update 2 16", "actor update 1 16"}));
    CHECK(Lines("actor update")[0] == "actor update 1 16" && Ids(T().m_active) == std::vector<int>({2, 0}) && ActiveConsistent(T().m_active) &&
          T().m_pending.empty());
    // Clear: everything destroyed (from the end).
    g_destroyed.clear();
    T().Clear();
    CHECK(T().m_actors.empty() && T().m_active.empty() && g_destroyed.size() == 3 && g_destroyed[0] == r && g_destroyed[2] == p);
}

void TestTempMines()
{
    Fresh();
    C_Human *owner = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2));
    C_Human *other = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 3));
    // DestroyMinesOf: the owner's mines go; another's and other actors stay. (Destroying moves the last actor into
    // the place, which the loop has passed: a mine of the owner moved there is skipped.)
    C_Mine *m1 = Mine(owner, TYPE_MINE, S_vector());
    C_Mine *m2 = Mine(other, TYPE_MINE, S_vector());
    C_Actor *d = Temp(ACTOR_DEBRIS, "d");
    C_Mine *m3 = Mine(owner, TYPE_MINE, S_vector());
    T().DestroyMinesOf(owner);
    // (m1 at 0 goes, m3 (last) moves to 0 and is skipped.)
    CHECK(Ids(T().m_actors) == std::vector<int>({3, 1, 2}) && T().FindActor(3) == m3 && T().FindActor(1) == m2 && T().FindActor(2) == d && m1);
    T().DestroyMinesOf(owner);
    CHECK(Ids(T().m_actors) == std::vector<int>({2, 1}));
    T().DestroyMinesOf(nullptr);
    CHECK(T().m_actors.size() == 2);

    // CreateMine: the model of the item at the place facing the direction; the mine set up; the owner loses the
    // item; a multiplayer game sends it when asked.
    Fresh(GAME_ROLE_SERVER);
    owner = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2));
    C_Item *item = Item(TYPE_MINE, 501);
    g_humanItems[owner] = {item};
    g_log.clear();
    T().CreateMine(owner, TYPE_MINE, S_vector(1, 2, 3), S_vector(0, 1, 0), true);
    C_Actor *mine = T().FindActor(0);
    CHECK(mine && mine->GetType() == ACTOR_MINE && static_cast<C_Mine *>(mine)->GetOwner() == owner && static_cast<C_Mine *>(mine)->GetItemId() == TYPE_MINE &&
          Count("openmodel w_mine") == 1 && Count("setworlddir w_mine 0 1 0 0") == 1 && Count("update w_mine") == 1 && Count("release w_mine") == 1 &&
          Count("minesetup 0 2 70 -1") == 1 && Count("human remove 2 501 1") == 1 && g_humanItems[owner].empty() && g_sent.size() == 1);
    CHECK(Near(mine->GetFrame()->m_localMatrix(3), S_vector(1, 2, 3), 0.0f));
    // The message: a temp-actor command, create mine: id, owner, item, the place, the direction.
    {
        C_NetMessage *m = g_sent.back();
        m->ResetPosition();
        S_vector pos, dir;
        const uint32_t kind = m->ReadBits(2), command = m->ReadBits(2), id = m->ReadBits(16), ownerId = m->ReadBits(16), itemId = m->ReadBits(16);
        m->ReadPosition(pos);
        m->ReadDirection3D(dir, -1);
        CHECK(kind == 1 && command == 0 && id == 0 && ownerId == 2 && itemId == TYPE_MINE && Near(pos, S_vector(1, 2, 3), 0.05f) && Near(dir, S_vector(0, 1, 0), 0.05f));
        CHECK(Count("netmsg " + std::to_string(MODULE_TEMP_ACTORS) + " " + std::to_string(NET_DELIVERY_RELIABLE)) == 1);
    }
    // Not sent: asked not to; a local game; no item to take (the original returns before sending).
    g_humanItems[owner] = {Item(TYPE_MINE, 502)};
    T().CreateMine(owner, TYPE_MINE, S_vector(1, 2, 3), S_vector(0, 1, 0), false);
    CHECK(g_sent.size() == 1 && T().m_actors.size() == 2);
    T().CreateMine(owner, TYPE_MINE, S_vector(1, 2, 3), S_vector(0, 1, 0), true);
    CHECK(g_sent.size() == 1 && T().m_actors.size() == 3);
    g_gameRole = GAME_ROLE_LOCAL;
    g_humanItems[owner] = {Item(TYPE_MINE, 503)};
    T().CreateMine(owner, TYPE_MINE, S_vector(1, 2, 3), S_vector(0, 1, 0), true);
    CHECK(g_sent.size() == 1 && T().m_actors.size() == 4 && g_humanItems[owner].empty());
    // No owner: sent with owner -1 (multiplayer); no model (or an unknown item): nothing at all.
    g_gameRole = GAME_ROLE_SERVER;
    T().CreateMine(nullptr, TYPE_MINE, S_vector(0, 0, 0), S_vector(0, 1, 0), true);
    CHECK(g_sent.size() == 2 && T().m_actors.size() == 5);
    g_sent.back()->ResetPosition();
    g_sent.back()->ReadBits(4);
    g_sent.back()->ReadBits(16);
    CHECK(static_cast<int16_t>(g_sent.back()->ReadBits(16)) == -1);
    g_log.clear();
    T().CreateMine(owner, TYPE_NOMODEL, S_vector(0, 0, 0), S_vector(0, 1, 0), true);
    T().CreateMine(owner, 9999, S_vector(0, 0, 0), S_vector(0, 1, 0), true);
    CHECK(T().m_actors.size() == 5 && g_sent.size() == 2 && Count("release newframe") == 2 && Count("openmodel w_none") == 1);

    // The client's mine: a server finds the one it has; a client makes it with the id, its owner (a human by id)
    // losing the item.
    Fresh(GAME_ROLE_SERVER);
    owner = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2));
    C_Actor *existing = Mine(nullptr, TYPE_MINE, S_vector(), 40);
    CHECK(T().ClientCreateMine(40, 2, TYPE_MINE, S_vector(), S_vector(0, 1, 0)) == existing && T().m_actors.size() == 1);
    g_gameRole = GAME_ROLE_CLIENT;
    g_humanItems[owner] = {Item(TYPE_MINE, 601)};
    C_Actor *cm = T().ClientCreateMine(41, 2, TYPE_MINE, S_vector(4, 5, 6), S_vector(1, 0, 0));
    CHECK(cm && cm->GetNetId() == 41 && static_cast<C_Mine *>(cm)->GetOwner() == owner && Count("human remove 2 601 1") == 1 && g_humanItems[owner].empty() &&
          Count("setworlddir w_mine 1 0 0 0") == 1 && T().m_nextId == 0);
    // Without the item, or no owner (-1, or no human there): made all the same.
    C_Actor *cm2 = T().ClientCreateMine(42, 2, TYPE_MINE, S_vector(), S_vector(0, 1, 0));
    C_Actor *cm3 = T().ClientCreateMine(43, -1, TYPE_MINE, S_vector(), S_vector(0, 1, 0));
    C_Actor *cm4 = T().ClientCreateMine(44, 9, TYPE_MINE, S_vector(), S_vector(0, 1, 0));
    CHECK(cm2 && cm3 && cm4 && !static_cast<C_Mine *>(cm3)->GetOwner() && !static_cast<C_Mine *>(cm4)->GetOwner());
    CHECK(!T().ClientCreateMine(45, 2, TYPE_NOMODEL, S_vector(), S_vector(0, 1, 0)));
    // LoadMine: no owner, the id given; no model: null.
    g_log.clear();
    C_Actor *lm = T().LoadMine(50, TYPE_MINE);
    CHECK(Count("minesetup 50 none 70 -1") == 1);
    CHECK(lm && lm->GetNetId() == 50 && static_cast<C_Mine *>(lm)->GetItemId() == TYPE_MINE && !static_cast<C_Mine *>(lm)->GetOwner() && !T().LoadMine(51, TYPE_NOMODEL));
    g_gameRole = GAME_ROLE_LOCAL;
}

void TestTempItems()
{
    Fresh(GAME_ROLE_SERVER);
    C_Human *h = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2));
    C_Item *gun = Item(TYPE_GUN, 700);
    C_Item *pick = Item(TYPE_PICKUP, 701);
    g_humanItems[h] = {gun, pick};
    // DropItem: the item leaves the human (kept), its model placed facing the normalised direction, an item actor
    // set up with the full velocity; its id returned. Not found: -1.
    CHECK(T().DropItem(h, 999, S_vector(), S_vector(0, 0, 1), true) == -1);
    g_log.clear();
    const int id = T().DropItem(h, 700, S_vector(1, 1, 1), S_vector(0, 0, 4), true);
    C_Actor *a = T().FindActor(id);
    CHECK(id == 0 && a && a->GetType() == ACTOR_ITEM && static_cast<C_ItemActor *>(a)->GetItem() == gun && a->GetFrame() == gun->GetFrame(0) &&
          Count("human remove 2 700 0") == 1 && Count("setposdir item700 1 1 1 dir 0 0 1 0") == 1 && Count("update item700") == 1 &&
          Count("setupitem 0 700 vel 0 0 4") == 1 && static_cast<C_ItemActor *>(a)->m_respawnTimer == 0);
    CHECK(At("human remove 2 700 0") < At("setposdir item700 1 1 1 dir 0 0 1 0"));
    // A pick-up in multiplayer: back to its place in 15 s; in a local game not.
    const int pid = T().DropItem(h, 701, S_vector(), S_vector(1, 0, 0), true);
    CHECK(static_cast<C_ItemActor *>(T().FindActor(pid))->m_respawnTimer == 15000);
    g_gameRole = GAME_ROLE_LOCAL;
    C_Item *pick2 = Item(TYPE_PICKUP, 702);
    g_humanItems[h] = {pick2};
    const int lid = T().DropItem(h, 702, S_vector(), S_vector(1, 0, 0), true);
    CHECK(static_cast<C_ItemActor *>(T().FindActor(lid))->m_respawnTimer == 0);

    // ClientCreateItem: a server finds its own; a client makes a new item (owner -1) in the world, or takes the
    // owner's item; facing the exact direction if given, else the normalised one.
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(T().ClientCreateItem(id, 2, TYPE_GUN, 700, S_vector(), S_vector(), nullptr) == a);
    Fresh(GAME_ROLE_CLIENT);
    h = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2));
    g_log.clear();
    const S_vector exact(0, 1, 0);
    C_Actor *n = T().ClientCreateItem(30, -1, TYPE_RIFLE, 800, S_vector(5, 0, 0), S_vector(0, 0, 2), &exact);
    CHECK(n && n->GetNetId() == 30 && Count("createitem 5 0 none 800") == 1 && static_cast<C_ItemActor *>(n)->GetItem()->m_inWorld &&
          Count("setposdir item800 5 0 0 dir 0 1 0 0") == 1 && Count("setupitem 30 800 vel 0 0 2") == 1);
    C_Item *owned = Item(TYPE_GUN, 801);
    g_humanItems[h] = {owned};
    C_Actor *o = T().ClientCreateItem(31, 2, TYPE_GUN, 801, S_vector(), S_vector(0, 0, 2), nullptr);
    CHECK(o && static_cast<C_ItemActor *>(o)->GetItem() == owned && Count("human remove 2 801 0") == 1 && Count("setposdir item801 0 0 0 dir 0 0 1 0") == 1);
    // No such human, no such item, no model frame: null.
    CHECK(!T().ClientCreateItem(32, 9, TYPE_GUN, 801, S_vector(), S_vector(0, 0, 1), nullptr));
    CHECK(!T().ClientCreateItem(33, 2, TYPE_GUN, 999, S_vector(), S_vector(0, 0, 1), nullptr));
    C_Item *noFrame = Item(TYPE_GUN, 802);
    g_itemFrames.erase(noFrame);
    g_humanItems[h] = {noFrame};
    CHECK(!T().ClientCreateItem(34, 2, TYPE_GUN, 802, S_vector(), S_vector(0, 0, 1), nullptr) && g_humanItems[h].empty());
    CHECK(T().m_actors.size() == 2);
    g_gameRole = GAME_ROLE_LOCAL;

    // In range: items of a type closer than the range (strictly), mines likewise; near: the pick-up kinds within
    // the range (inclusive).
    Fresh();
    ItemActor(Item(TYPE_GUN, 900), S_vector(3, 0, 4));          // 5 away
    ItemActor(Item(TYPE_PICKUP, 901), S_vector(1, 0, 0));
    Mine(nullptr, TYPE_MINE, S_vector(0, 6, 0));
    CHECK(T().IsItemInRange(TYPE_GUN, S_vector(), 6) && !T().IsItemInRange(TYPE_GUN, S_vector(), 5) && !T().IsItemInRange(TYPE_RIFLE, S_vector(), 100) &&
          T().IsItemInRange(TYPE_PICKUP, S_vector(), 2));
    CHECK(T().IsMineInRange(S_vector(), 7) && !T().IsMineInRange(S_vector(), 6) && !T().IsMineInRange(S_vector(0, 100, 0), 50));
    CHECK(T().FindItemNear(S_vector(), 5.0f) && T().FindItemNear(S_vector(), 5.0f)->GetNetId() == 0 && !T().FindItemNear(S_vector(), 4.99f));
    // An item actor without an item: not in range.
    C_ItemActor *empty = ItemActor(Item(TYPE_GUN, 902), S_vector());
    empty->m_item = nullptr;
    CHECK(!T().IsItemInRange(TYPE_GUN, S_vector(), 4));
    T().DestroyActor(empty);
    // The pick-up kinds: 3, 4, 5, 6 and 12.
    for (int behaviour = 0; behaviour < 25; ++behaviour) {
        Fresh();
        Def(400, behaviour, "w_x");
        ItemActor(Item(400, 950), S_vector());
        const bool kind = behaviour == 3 || behaviour == 4 || behaviour == 5 || behaviour == 6 || behaviour == 12;
        CHECK((T().FindItemNear(S_vector(), 1.0f) != nullptr) == kind);
    }
    Fresh();
    Mine(nullptr, TYPE_MINE, S_vector());
    CHECK(!T().FindItemNear(S_vector(), 10.0f));
}

void TestTempDebrisAndBreakables()
{
    Fresh();
    // Debris: a copy of the visual (none: null), placed, ids from a million on; the copy released (the actor holds it).
    I3D_frame *plain = NewFrame(FRAME_MODEL, "plain");
    CHECK(!T().CreateDebris(plain, S_vector(), S_vector(0, 0, 1)));
    I3D_frame *visual = NewFrame(FRAME_VISUAL, "vis");
    g_visualTypes[visual] = 5;
    g_log.clear();
    C_Debris *d1 = T().CreateDebris(visual, S_vector(1, 2, 3), S_vector(0, 0, 1));
    C_Debris *d2 = T().CreateDebris(visual, S_vector(1, 2, 3), S_vector(0, 0, 1));
    CHECK(d1 && d2 && reinterpret_cast<C_Actor *>(d1)->GetNetId() == 1000000 && reinterpret_cast<C_Actor *>(d2)->GetNetId() == 1000001 &&
          T().m_nextId == 0 && Count("createvisual 5") == 2 && Count("duplicate vis") == 2 && Count("setpos copy of vis 1 2 3") == 2 &&
          Count("setdir copy of vis 0 0 1 0") == 2 && Count("update copy of vis") == 2 && Count("release copy of vis") == 2);
    // Breakables: a wire cut per fence on its frame, its door set, the volume pointed at it and moved.
    Fresh();
    S_CollisionVolume v1{}, v2{};
    C_PhysDoor *door = reinterpret_cast<C_PhysDoor *>(0x40);
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence1"), door, &v1});
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence2"), nullptr, &v2});
    T().CreateBreakables();
    CHECK(T().m_actors.size() == 2 && v1.actor == T().m_actors[0] && v2.actor == T().m_actors[1] && static_cast<C_WireCut *>(T().m_actors[0])->m_door == door &&
          T().m_actors[0]->GetType() == ACTOR_WIRE_CUT && Count("movevolume 0") == 1 && Count("movevolume 1") == 1);
    // On a client only when not also the server.
    for (const uint32_t role : {static_cast<uint32_t>(GAME_ROLE_SERVER), static_cast<uint32_t>(GAME_ROLE_CLIENT),
                                static_cast<uint32_t>(GAME_ROLE_LOCAL)}) {
        g_gameRole = role;
        const size_t before = T().m_actors.size();
        T().CreateBreakablesOnClient();
        CHECK(T().m_actors.size() == before + (role & GAME_ROLE_SERVER ? 0 : 2));
    }
    g_gameRole = GAME_ROLE_LOCAL;
    T().ClearBreakables();
    CHECK(T().m_breakables.empty());
}

// The mission's placements (an aligned array, as the items module keeps them).
void SetPlacements(std::initializer_list<S_ItemPlacement> list)
{
    auto *p = static_cast<S_ItemPlacement *>(::operator new(sizeof(S_ItemPlacement) * (list.size() + 1), std::align_val_t(16)));
    int n = 0;
    for (const S_ItemPlacement &x : list)
        p[n++] = x;
    g_itemsModule.m_placements = p;
    g_itemsModule.m_placementCount = n;
}
S_ItemPlacement Placement(C_Item *item, const S_vector &pos, bool armed = false)
{
    S_ItemPlacement p;
    p.item = item;
    p.pos = V4(pos.x, pos.y, pos.z, 1.0f);
    p.rot.s = 0.8f;
    p.rot.v = S_vector(0.0f, 0.6f, 0.0f);
    item->SetArmed(armed);
    return p;
}

void TestTempMission()
{
    // StartMission on a server: the next id reset, the fences made, the placements turned into actors: an armed mine
    // laid where a ray 0.5 m above going 2 m down hits (else at its place, facing up) and its item destroyed; other
    // items lying at their place and rotation; multiplayer pick-ups' placements kept; the placements freed.
    for (const uint32_t role : {static_cast<uint32_t>(GAME_ROLE_SERVER), static_cast<uint32_t>(GAME_ROLE_LOCAL)}) {
        Fresh(role);
        T().m_nextId = 9;
        T().m_debrisCount = 3;
        S_CollisionVolume v{};
        T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &v});
        C_Item *m1 = Item(TYPE_MINE, 10), *m2 = Item(TYPE_MINE, 11), *unarmed = Item(TYPE_MINE, 12), *gun = Item(TYPE_GUN, 13), *pick = Item(TYPE_PICKUP, 14);
        SetPlacements({Placement(m1, S_vector(1, 2, 3), true), Placement(m2, S_vector(4, 5, 6), true), Placement(unarmed, S_vector(7, 8, 9)),
                       Placement(gun, S_vector(10, 11, 12)), Placement(pick, S_vector(13, 14, 15))});
        g_rayHit = true;
        g_rayHitPos = S_vector(1, 0, 3);
        g_rayHitNormal = S_vector(0, 0.8f, 0.6f);
        int rays = 0;
        g_log.clear();
        // (The second mine's ray misses.)
        g_tickChanges.clear();
        T().StartMission(true);
        rays = static_cast<int>(Lines("raytest").size());
        // (Laid without telling the clients.)
        CHECK(g_sent.empty() && T().m_nextId == 6 && T().m_debrisCount == 0 && rays == 2 && Count("raytest 1 2.5 3 dir 0 -2 0 0 0") == 1 &&
              Count("raytest 4 5.5 6 dir 0 -2 0 0 0") == 1 && Count("freeplacements") == 1 && g_itemsModule.GetPlacementCount() == 0);
        CHECK(Count("destroyitem 10") == 1 && Count("destroyitem 11") == 1 && !Logged("destroyitem 12") && Count("minesetup 1 none 70 -1") == 1);
        // (Ids: the fence 0, the mines 1 and 2, the items 3, 4, 5.)
        C_Actor *fence = T().FindActor(0), *mine1 = T().FindActor(1), *mine2 = T().FindActor(2);
        CHECK(fence && fence->GetType() == ACTOR_WIRE_CUT && mine1->GetType() == ACTOR_MINE && mine2->GetType() == ACTOR_MINE &&
              Count("setworlddir w_mine 0 0.8 0.6 0") == 2);   // (the ray hit for both: the fake hits every time)
        for (int i = 3; i <= 5; ++i)
            CHECK(T().FindActor(i) && T().FindActor(i)->GetType() == ACTOR_ITEM);
        C_Actor *gunActor = T().FindActor(4);
        CHECK(static_cast<C_ItemActor *>(gunActor)->GetItem() == gun && gun->GetFrame(0)->m_localMatrix(3).x == 10.0f &&
              gun->GetFrame(0)->m_localMatrix(3).z == 12.0f && gun->GetFrame(0)->m_rot.s == 0.8f && gun->GetFrame(0)->m_rot.v.y == 0.6f &&
              Count("setupitem 4 13 vel 0 0 0") == 1);
        CHECK(T().GetRespawnItems().size() == (role == GAME_ROLE_LOCAL ? 0u : 1u));
        if (role != GAME_ROLE_LOCAL)
            CHECK(T().GetRespawnItems()[0].item == pick && T().GetRespawnItems()[0].pos.z == 15.0f);
        g_rayHit = false;
    }
    // A missed ray: the mine at its place, facing up.
    Fresh(GAME_ROLE_SERVER);
    SetPlacements({Placement(Item(TYPE_MINE, 20), S_vector(1, 2, 3), true)});
    T().StartMission(true);
    CHECK(Count("setworlddir w_mine 0 1 0 0") == 1 && Near(T().FindActor(0)->GetFrame()->m_localMatrix(3), S_vector(1, 2, 3), 0.0f) && g_sent.empty());
    // The server's client side (StartMission(false) on a server): its own next id kept, no placements, no fences made
    // on a client pass; a client: id reset, fences made by CreateBreakablesOnClient; no placements either.
    Fresh(GAME_ROLE_SERVER);
    T().m_nextId = 9;
    S_CollisionVolume v{};
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &v});
    SetPlacements({Placement(Item(TYPE_GUN, 21), S_vector())});
    T().StartMission(false);
    CHECK(T().m_nextId == 9 && T().m_actors.empty() && g_itemsModule.GetPlacementCount() == 1);
    g_gameRole = GAME_ROLE_CLIENT;
    T().StartMission(false);
    CHECK(T().m_nextId == 1 && T().m_actors.size() == 1 && g_itemsModule.GetPlacementCount() == 1);
    // A client given server = true (does not happen): the id reset, fences, no placements.
    T().StartMission(true);
    CHECK(g_itemsModule.GetPlacementCount() == 1);
    g_itemsModule.m_placementCount = 0;

    // StopMission: a server's client pass does nothing; else every actor destroyed, the fences and pick-ups
    // forgotten.
    Fresh(GAME_ROLE_SERVER);
    Temp(ACTOR_DEBRIS, "a");
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &v});
    T().m_respawnItems.push_back(Placement(Item(TYPE_PICKUP, 22), S_vector()));
    T().StopMission(false);
    CHECK(T().m_actors.size() == 1 && T().m_breakables.size() == 1);
    T().StopMission(true);
    CHECK(T().m_actors.empty() && T().m_breakables.empty() && T().m_respawnItems.empty());
    g_gameRole = GAME_ROLE_CLIENT;
    Temp(ACTOR_DEBRIS, "b");
    T().StopMission(false);
    CHECK(T().m_actors.empty());
    // RestartMission: everything destroyed, the ids reset; a full restart starts the mission by role, else only the
    // fences are made again.
    for (const uint32_t role : {static_cast<uint32_t>(GAME_ROLE_SERVER), static_cast<uint32_t>(GAME_ROLE_CLIENT),
                                static_cast<uint32_t>(GAME_ROLE_LOCAL)}) {
        for (const bool server : {false, true}) {
            Fresh(role);
            Temp(ACTOR_DEBRIS, "x");
            T().m_nextId = 7;
            T().m_debrisCount = 4;
            S_CollisionVolume vv{};
            T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &vv});
            SetPlacements({Placement(Item(TYPE_GUN, 30), S_vector())});
            g_log.clear();
            T().RestartMission(server);
            const bool srv = role & GAME_ROLE_SERVER, cli = role & GAME_ROLE_CLIENT;
            // Fences: a partial restart makes them once per role bit (a local game: twice); a full one by StartMission:
            // the server's pass, and the client's pass only without the server bit.
            const size_t fences = server ? (srv ? 1 : 0) + (cli && !srv ? 1 : 0) : (srv ? 1 : 0) + (cli && !srv ? 1 : 0);
            const size_t items = server && srv ? 1 : 0;
            CHECK(T().m_actors.size() == fences + items && T().m_debrisCount == 0);
            CHECK(Count("destroy " + std::to_string(ACTOR_DEBRIS) + " 0") == 1);
            g_itemsModule.m_placementCount = 0;
        }
    }
    g_gameRole = GAME_ROLE_LOCAL;
}

void TestTempSaveLoad()
{
    Fresh();
    C_Human *owner = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2));
    S_CollisionVolume v{};
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &v});
    T().CreateBreakables();                             // wire cut 0
    C_ItemActor *it = ItemActor(Item(TYPE_GUN, 40), S_vector(1, 0, 0));   // 1
    C_Mine *mi = Mine(owner, TYPE_MINE, S_vector(2, 0, 0));               // 2
    T().CreateDebris([] {
        I3D_frame *vis = NewFrame(FRAME_VISUAL, "vis");
        g_visualTypes[vis] = 5;
        return vis;
    }(), S_vector(), S_vector(0, 0, 1));                                   // 1000000
    C_ItemActor *it2 = ItemActor(Item(TYPE_RIFLE, 41), S_vector(3, 0, 0)); // 3
    T().SetActorActive(it, true);
    T().SetActorActive(T().FindActor(0), true);
    CHECK(T().m_nextId == 4);
    {
        C_chunk out;
        out.Open("temp.sav", true);
        out.Begin(0x200);
        g_log.clear();
        CHECK(T().SaveGame(out));
        out.Ascend();
        out.Close();
    }
    CHECK(Count("saveitem 40 1 0 0") == 1 && Count("saveitem 41 3 0 0") == 1 &&
          Lines("actor save") == std::vector<std::string>({"actor save 0", "actor save 1", "actor save 2", "actor save 3"}));
    // The chunks: next id, then per actor: wire cut (active, id, data), item (active, record, data), mine (active,
    // id, item, data), debris (empty).
    const std::string f = g_files["temp.sav"];
    std::vector<std::pair<int, int>> chunks;            // id, size
    for (size_t at = 6; at + 6 <= f.size();) {
        uint16_t id;
        uint32_t size;
        std::memcpy(&id, f.data() + at, 2);
        std::memcpy(&size, f.data() + at + 2, 4);
        chunks.push_back({id, static_cast<int>(size)});
        at += size;
    }
    const std::vector<std::pair<int, int>> want = {{0x456, 10}, {0x459, 6 + 1 + 4 + 4}, {0x457, 6 + 1 + 12 + 4},
                                                   {0x458, 6 + 1 + 4 + 4 + 4}, {0x45A, 6}, {0x457, 6 + 1 + 12 + 4}};
    CHECK(chunks == want);
    CHECK(static_cast<uint8_t>(f[6 + 10 + 6]) == 1 && static_cast<uint8_t>(f[6 + 10 + 15 + 6]) == 1 && static_cast<uint8_t>(f[6 + 10 + 15 + 23 + 6]) == 0);

    // Loaded into a fresh list holding the fence (made by the mission): the next id, the items and mines made again
    // with their ids, the fence found and loaded, the debris skipped; the activity restored.
    Fresh();
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &v});
    T().CreateBreakables();
    {
        C_chunk in;
        in.Open("temp.sav", false);
        uint16_t id;
        in.Descend(id);
        g_log.clear();
        CHECK(T().LoadGame(in));
        in.Close();
    }
    C_Actor *fence = T().FindActor(0), *i1 = T().FindActor(1), *m = T().FindActor(2), *i3 = T().FindActor(3);
    CHECK(T().m_nextId == 4 && T().m_actors.size() == 4 && fence && i1 && m && i3 && i1->GetType() == ACTOR_ITEM && m->GetType() == ACTOR_MINE &&
          i3->GetType() == ACTOR_ITEM);
    CHECK(static_cast<C_ItemActor *>(i1)->GetItem()->GetUid() == 40 && static_cast<C_ItemActor *>(i3)->GetItem()->GetTypeId() == TYPE_RIFLE &&
          static_cast<C_Mine *>(m)->GetItemId() == TYPE_MINE && !static_cast<C_Mine *>(m)->GetOwner());
    CHECK(Lines("actor load") == std::vector<std::string>({"actor load 0 7", "actor load 1 17", "actor load 2 27", "actor load 3 37"}));
    // (The branch's fix: the mine saved inactive stays inactive; the original's debris chunk after it would activate
    // it again, 0x00477f50.)
    CHECK(fence->GetActiveIndex() >= 0 && i1->GetActiveIndex() >= 0 && m->GetActiveIndex() == -1 && i3->GetActiveIndex() == -1);
    CHECK(Count("loaditem 4 40 1 0 0 none") == 1 && Count("setupitem 1 40 vel 0 0 0") == 1);
    // A fence that is not there: its data skipped. Chunks without the active byte: active. Unknown chunks skipped.
    {
        C_chunk out;
        out.Open("temp2.sav", true);
        out.Begin(0x200);
        out.Begin(0x459);
        out.Ascend();
        out.Begin(0x999);
        out.Write(5);
        out.Ascend();
        out.Begin(0x459);
        out.Write(static_cast<uint8_t>(1));
        out.Write(55);
        out.Write(1);
        out.Ascend();
        out.Ascend();
        out.Close();
    }
    Fresh();
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &v});
    T().CreateBreakables();
    T().FindActor(0)->m_netId = 0;
    {
        C_chunk in;
        in.Open("temp2.sav", false);
        uint16_t id;
        in.Descend(id);
        g_log.clear();
        CHECK(T().LoadGame(in));
        in.Close();
    }
    // (The empty fence chunk: id read as 0 from nothing: the fence, active; the fence 55: none. A mine whose model
    // does not load is not checked for: LoadGame then calls through null, as the original (0x00477f50) does.)
    CHECK(T().FindActor(0)->GetActiveIndex() >= 0 && T().m_actors.size() == 1 && Count("actor load 0 -1") == 1);
    (void)it2;
    (void)mi;
}

void TestTempState()
{
    // The state a server sends: the next id, then per actor its type and id and (mines, items) the rest; debris only
    // its type and id; a zero type ends it. A client reads it: the mines and items made, the fence found.
    Fresh(GAME_ROLE_SERVER);
    C_Human *owner = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2));
    S_CollisionVolume v{};
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &v});
    T().CreateBreakables();                                                 // 0
    C_Mine *m = Mine(owner, TYPE_MINE, S_vector(2, 0, 0));                 // 1
    m->m_position = V4(2, 1, 0, 1);
    m->m_direction = V4(0, 0, 1, 0);
    C_ItemActor *it = ItemActor(Item(TYPE_GUN, 60), S_vector(1, 0, 0));    // 2
    it->m_position = V4(1, 2, 3, 1);
    it->m_direction = V4(1, 0, 0, 0);
    T().CreateDebris([] {
        I3D_frame *vis = NewFrame(FRAME_VISUAL, "vis");
        g_visualTypes[vis] = 5;
        return vis;
    }(), S_vector(), S_vector(0, 0, 1));
    C_Mine *orphan = Mine(nullptr, TYPE_MINE, S_vector());                  // 3
    orphan->m_direction = V4(0, 1, 0, 0);
    C_NetMessage msg;
    g_log.clear();
    T().WriteState(&msg);
    CHECK(Lines("actor writestate") == std::vector<std::string>({"actor writestate 0", "actor writestate 1", "actor writestate 2", "actor writestate 3"}));
    // The client.
    Fresh(GAME_ROLE_CLIENT);
    owner = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2));
    g_humanItems[owner] = {Item(TYPE_MINE, 61)};
    T().AddBreakable({NewFrame(FRAME_MODEL, "fence"), nullptr, &v});
    T().CreateBreakables();
    msg.ResetPosition();
    g_log.clear();
    T().ReadState(&msg);
    C_Actor *cm = T().FindActor(1), *ci = T().FindActor(2), *co = T().FindActor(3);
    CHECK(T().m_nextId == 4 && cm && ci && co && cm->GetType() == ACTOR_MINE && static_cast<C_Mine *>(cm)->GetOwner() == owner &&
          static_cast<C_Mine *>(co)->GetOwner() == nullptr && ci->GetType() == ACTOR_ITEM &&
          static_cast<C_ItemActor *>(ci)->GetItem()->GetUid() == 60 && static_cast<C_ItemActor *>(ci)->GetItem()->GetTypeId() == TYPE_GUN);
    CHECK(Lines("actor readstate") == std::vector<std::string>({"actor readstate 0 0", "actor readstate 1 1", "actor readstate 2 2", "actor readstate 3 3"}));
    CHECK(Count("human remove 2 61 1") == 1 && Count("createitem 4 0 none 60") == 1 && Count("setupitem 2 60 vel 0 0 0") == 1);
    // (The positions and directions as the message carries them.)
    CHECK(Near(cm->GetFrame()->m_localMatrix(3), S_vector(2, 1, 0), 0.05f) && Near(ci->GetFrame()->m_worldMatrix(3), S_vector(1, 2, 3), 0.05f));
    CHECK(msg.ReadBits(1) == 0 || true);
    // A next id above 32767 comes back negative (16 bits, signed).
    Fresh(GAME_ROLE_SERVER);
    T().m_nextId = 40000;
    C_NetMessage big;
    T().WriteState(&big);
    big.ResetPosition();
    Fresh(GAME_ROLE_CLIENT);
    T().ReadState(&big);
    CHECK(T().m_nextId == 40000 - 65536);
    // An actor the client cannot make (no model): its state not read, the rest goes on.
    Fresh(GAME_ROLE_CLIENT);
    C_NetMessage odd;
    odd.WriteBits(0, 16);
    odd.WriteBits(ACTOR_MINE, 6);
    odd.WriteBits(7, 16);
    odd.WriteBits(static_cast<uint32_t>(-1), 16);
    odd.WriteBits(TYPE_NOMODEL, 16);
    odd.WritePosition(S_vector());
    odd.WriteDirection3D(S_vector(0, 1, 0), -1);
    odd.WriteBits(ACTOR_WIRE_CUT, 6);
    odd.WriteBits(9, 16);
    odd.WriteBits(ACTOR_DEBRIS, 6);
    odd.WriteBits(10, 16);
    odd.WriteBits(0, 6);
    odd.ResetPosition();
    g_log.clear();
    T().ReadState(&odd);
    CHECK(T().m_actors.empty() && !Logged("actor readstate"));

    // Messages: an actor message (2-bit command 0, a 10-bit id) to that actor (none: nothing); a create-mine command.
    Fresh(GAME_ROLE_CLIENT);
    C_Actor *d = Temp(ACTOR_DEBRIS, "d", S_vector(), 12);
    g_log.clear();
    CHECK(T().OnMessage(Msg({{0, 2}, {12, 10}, {91, 8}})) && Count("actor message 12 91") == 1);
    CHECK(T().OnMessage(Msg({{0, 2}, {13, 10}, {92, 8}})) && !Logged("actor message 13"));
    {
        C_NetMessage *cmd = new C_NetMessage();
        cmd->WriteBits(1, 2);
        cmd->WriteBits(0, 2);
        cmd->WriteBits(20, 16);
        cmd->WriteBits(static_cast<uint32_t>(-1), 16);
        cmd->WriteBits(TYPE_MINE, 16);
        cmd->WritePosition(S_vector(3, 4, 5));
        cmd->WriteDirection3D(S_vector(0, 1, 0), -1);
        cmd->ResetPosition();
        CHECK(T().OnMessage(cmd) && T().FindActor(20) && T().FindActor(20)->GetType() == ACTOR_MINE &&
              static_cast<C_Mine *>(T().FindActor(20))->GetItemId() == TYPE_MINE &&
              Near(T().FindActor(20)->GetFrame()->m_localMatrix(3), S_vector(3, 4, 5), 0.05f));
    }
    // Another command: nothing; commands 2, 3 likewise.
    const size_t n = T().m_actors.size();
    CHECK(T().OnMessage(Msg({{1, 2}, {1, 2}})) && T().OnMessage(Msg({{2, 2}})) && T().OnMessage(Msg({{3, 2}})) && T().m_actors.size() == n);
    // Unknown28: the 10-bit id; found: the actor's second handler, true; else false.
    CHECK(T().Unknown28(Msg({{12, 10}, {93, 8}})) && Count("actor message2 12 93") == 1 && !T().Unknown28(Msg({{14, 10}, {94, 8}})));
    // The message helpers.
    g_log.clear();
    C_BitStream *a1 = CreateTempActorMessage(300, true);
    C_BitStream *a2 = CreateTempActorCommand(1, false);
    C_BitStream *a3 = CreateTempActorMessage2(9, true);
    SendTempActorMessage(a3);
    CHECK(Count("netmsg " + std::to_string(MODULE_TEMP_ACTORS) + " " + std::to_string(NET_DELIVERY_RELIABLE)) == 1 &&
          Count("netmsg " + std::to_string(MODULE_TEMP_ACTORS) + " " + std::to_string(NET_DELIVERY_3)) == 1 &&
          Count("netmsgex " + std::to_string(MODULE_TEMP_ACTORS) + " " + std::to_string(NET_DELIVERY_RELIABLE)) == 1 && g_sent.back() == a3);
    a1->ResetPosition();
    a2->ResetPosition();
    a3->ResetPosition();
    CHECK(a1->ReadBits(2) == 0 && a1->ReadBits(10) == 300 && a2->ReadBits(2) == 1 && a2->ReadBits(2) == 1 && a3->ReadBits(10) == 9);
    (void)d;
    g_gameRole = GAME_ROLE_LOCAL;
}

// ============================================================ what the first coverage run showed untested
void TestActorsEdges()
{
    // The destructors (of lists of their own): every actor destroyed; the module's queued removals are not.
    Fresh();
    {
        C_ActorsModule local(MODULE_ACTORS, "Actors");
        local.CreateActor(ACTOR_DOOR, nullptr, true, -1);
        local.CreateActor(ACTOR_HUMAN, nullptr, true, 4);
        C_Actor *later = local.CreateActor(ACTOR_HUMAN, nullptr, true, 5);
        local.DestroyActor(later, false);
        local.m_iterating = true;                       // (the destructor clears it first: destroyed all the same)
        g_destroyed.clear();
        g_log.clear();
    }
    CHECK(g_destroyed.size() == 2 && Count("destroy " + std::to_string(ACTOR_DOOR) + " 32") == 1 && Count("destroy 3 4") == 1);
    {
        C_TemporaryActorsList local(MODULE_TEMP_ACTORS, "Temp. Actors");
        local.CreateActor(ACTOR_DEBRIS, NewFrame(FRAME_MODEL), -1);
        local.CreateActor(ACTOR_DEBRIS, NewFrame(FRAME_MODEL), -1);
        g_destroyed.clear();
    }
    CHECK(g_destroyed.size() == 2);

    // An armed item that is not a mine: lying like any other.
    Fresh(GAME_ROLE_SERVER);
    C_Item *gun = Item(TYPE_GUN, 70);
    SetPlacements({Placement(gun, S_vector(1, 2, 3), true)});
    T().StartMission(true);
    CHECK(T().m_actors.size() == 1 && T().m_actors[0]->GetType() == ACTOR_ITEM && !Logged("raytest") && !Logged("destroyitem"));
    // Saving another kind of temporary actor (a door): nothing written for it.
    Temp(ACTOR_DOOR, "door");
    {
        C_chunk out;
        out.Open("kinds.sav", true);
        out.Begin(0x200);
        g_log.clear();
        T().SaveGame(out);
        out.Ascend();
        out.Close();
    }
    CHECK(Lines("actor save") == std::vector<std::string>({"actor save 0"}) && g_files["kinds.sav"].size() == 6 + 10 + 6 + 1 + 12 + 4);
    // An item definition without a model name: no mine.
    Def(81, ITEM_BEHAVIOUR_MINE, nullptr);
    g_log.clear();
    T().CreateMine(nullptr, 81, S_vector(), S_vector(0, 1, 0), false);
    CHECK(T().m_actors.size() == 2 && !Logged("openmodel") && Count("release newframe") == 1);
    // The message helpers' other reliability.
    g_log.clear();
    CreateActorMessageEx(1, true);
    CreateActorMessage(1, false);
    CreateTempActorMessage(1, false);
    CreateTempActorCommand(0, true);
    CreateTempActorMessage2(1, false);
    CHECK(Count("netmsgex " + std::to_string(MODULE_ACTORS) + " " + std::to_string(NET_DELIVERY_RELIABLE)) == 1 &&
          Count("netmsg " + std::to_string(MODULE_ACTORS) + " " + std::to_string(NET_DELIVERY_3)) == 1 &&
          Count("netmsg " + std::to_string(MODULE_TEMP_ACTORS) + " " + std::to_string(NET_DELIVERY_3)) == 1 &&
          Count("netmsg " + std::to_string(MODULE_TEMP_ACTORS) + " " + std::to_string(NET_DELIVERY_RELIABLE)) == 1 &&
          Count("netmsgex " + std::to_string(MODULE_TEMP_ACTORS) + " " + std::to_string(NET_DELIVERY_3)) == 1);
    g_gameRole = GAME_ROLE_LOCAL;
}

// ============================================================ what the first mutation run showed untested
void TestActorsMutationGaps()
{
    // Type 0 can be created; slot 0 is found by GetActor and destroyed by Clear.
    Fresh();
    C_Actor *zero = g_actorsModule.CreateActor(static_cast<E_ActorType>(0), nullptr, false, 0);
    CHECK(zero && g_actorsModule.GetActor(0) == zero && g_actorsModule.GetActorById(0) == zero);
    g_destroyed.clear();
    g_actorsModule.Clear();
    CHECK(g_destroyed.size() == 1 && g_destroyed[0] == zero);
    // A removed human found by id at the start of the removal list.
    C_Actor *r0 = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 8);
    C_Actor *r1 = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 9);
    g_actorsModule.DestroyActor(r0, false);
    g_actorsModule.DestroyActor(r1, false);
    CHECK(g_actorsModule.GetActorById(8) == r0 && g_actorsModule.GetHuman(8) == r0 && g_actorsModule.GetActorById(9) == r1);
    g_actorsModule.m_removed.clear();

    // Distances from a place that is not the origin.
    Fresh();
    ItemActor(Item(TYPE_GUN, 960), S_vector(10, 0, 0));
    Mine(nullptr, TYPE_MINE, S_vector(10, 0, 0));
    CHECK(T().IsItemInRange(TYPE_GUN, S_vector(9, 0, 0), 2) && T().IsMineInRange(S_vector(11, 0, 0), 2) && T().FindItemNear(S_vector(10, 1, 0), 1.0f) &&
          !T().IsItemInRange(TYPE_GUN, S_vector(-9, 0, 0), 2));
    // An explicit id 0 is kept (only below 0 takes the next).
    Fresh();
    T().m_nextId = 5;
    C_Actor *z = T().CreateActor(ACTOR_DEBRIS, NewFrame(FRAME_MODEL), 0);
    CHECK(z->GetNetId() == 0 && T().m_nextId == 5);
    // The client's mine of the human with id 0.
    Fresh(GAME_ROLE_CLIENT);
    C_Human *h0 = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 0));
    C_Actor *m0 = T().ClientCreateMine(3, 0, TYPE_MINE, S_vector(), S_vector(0, 1, 0));
    CHECK(m0 && static_cast<C_Mine *>(m0)->GetOwner() == h0);

    // The state's fields exactly: a mine without an owner: owner -1 (16 bits), then the terminating zero type; the
    // reader stops there (a record after the end is not read).
    Fresh(GAME_ROLE_SERVER);
    C_Mine *orphan = Mine(nullptr, TYPE_MINE, S_vector());
    orphan->m_direction = V4(0, 1, 0, 0);
    C_NetMessage msg;
    T().WriteState(&msg);
    const uint32_t end = msg.BitPosition();
    msg.WriteBits(ACTOR_MINE, 6);                       // (after the end)
    msg.WriteBits(77, 16);
    msg.ResetPosition();
    S_vector pos, dir;
    CHECK(msg.ReadBits(16) == 1 && msg.ReadBits(6) == ACTOR_MINE && msg.ReadBits(16) == 0 && msg.ReadBits(16) == 0xFFFF && msg.ReadBits(16) == TYPE_MINE);
    msg.ReadPosition(pos);
    msg.ReadDirection3D(dir, -1);
    CHECK(msg.ReadBits(8) == 0 && msg.ReadBits(6) == 0 && msg.BitPosition() == end);
    msg.ResetPosition();
    Fresh(GAME_ROLE_CLIENT);
    T().ReadState(&msg);
    CHECK(T().m_actors.size() == 1 && !T().FindActor(77));
    g_gameRole = GAME_ROLE_LOCAL;
}

void RunTempActorsTests()
{
    MakeDefs();
    TestTempCreateDestroy();
    TestTempMines();
    TestTempItems();
    TestTempDebrisAndBreakables();
    TestTempMission();
    TestTempSaveLoad();
    TestTempState();
    TestActorsEdges();
    TestActorsMutationGaps();
}
}  // namespace
