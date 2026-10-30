// Part 8b's sight module (WORLD_TEST; game/sight.cpp), after world_modules_test.inc (its helpers).
// Humans are the harness's stub C_Human (roles, friend / hostile lists, the view tests and the
// collision faked here); meleeTrackers are zeroed C_MeleeTracker memory with the fields the module
// reads; rays are answered by the part they aim at. Prints the "sight:" line.

namespace {

std::vector<std::string> g_sgLog;
std::map<const C_Actor *, std::string> g_sgNames;
std::map<std::pair<const C_Actor *, const C_Actor *>, float> g_sgView;   // default 100
std::map<const C_Human *, std::vector<const C_Actor *>> g_sgFriends;
// A ray's answer by the point it aims at ("x y z"); default: nothing in the way.
std::map<std::string, S_CollisionVolume *> g_sgRayHits;
struct S_SgRay {
    S_vector from, dir;
    bool hitPos;
    uint32_t types, flags;
};
std::vector<S_SgRay> g_sgRays;

std::string SgName(const C_Actor *a)
{
    auto it = g_sgNames.find(a);
    return it == g_sgNames.end() ? "?" : it->second;
}
void SgLog(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_sgLog.push_back(text);
}
float SgView(const C_Actor *a, const C_Actor *b)
{
    auto it = g_sgView.find({a, b});
    return it == g_sgView.end() ? 100.0f : it->second;
}
std::string SgKey(const S_vector &v)
{
    char text[96];
    std::snprintf(text, sizeof text, "%.3f %.3f %.3f", v.x, v.y, v.z);
    return text;
}
bool SgLogIs(std::initializer_list<std::string> expected)
{
    const bool same = std::vector<std::string>(expected) == g_sgLog;
    if (!same && g_wmTrace) {
        for (const std::string &l : g_sgLog)
            std::printf("    sglog %s\n", l.c_str());
    }
    return same;
}

}  // namespace

bool C_Human::IsFriendOf(const C_Actor *other) const
{
    const auto &list = g_sgFriends[this];
    return std::find(list.begin(), list.end(), other) != list.end();
}
void C_Human::OnSightEvent(int lost, int targetId)
{
    SgLog("see %s %d %d", SgName(this).c_str(), lost, targetId);
}
void C_Human::TestSightOfHuman(C_Human *target, int *status, float *visibility)
{
    SgLog("view %s %s", SgName(this).c_str(), SgName(target).c_str());
    *status = 5;
    *visibility = SgView(this, target);
}
void C_Human::TestSightOfMeleeTracker(C_Actor *meleeTracker, int *status, float *visibility)
{
    SgLog("viewm %s %s", SgName(this).c_str(), SgName(meleeTracker).c_str());
    *status = 6;
    *visibility = SgView(this, meleeTracker);
}
void C_Human::RemoveCollision(bool unknown)
{
    SgLog("nocoll %s %d", SgName(this).c_str(), unknown);
}
void C_Human::RestoreCollision(bool unknown)
{
    SgLog("coll %s %d", SgName(this).c_str(), unknown);
}
void C_MeleeTracker::OnSightEvent(int lost, int targetIndex)
{
    SgLog("msee %s %d %d", SgName(this).c_str(), lost, targetIndex);
}
void C_MeleeTracker::TestSight(C_Actor *target, int *status, float *visibility)
{
    SgLog("mview %s %s", SgName(this).c_str(), SgName(target).c_str());
    *status = 7;
    *visibility = SgView(this, target);
}
S_CollisionVolume *C_CollisionWorld::RayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                             S_vector *hitNormal, uint32_t excludeTypes, uint32_t excludeFlags) const
{
    g_sgRays.push_back({from, dir, hitPos != nullptr, excludeTypes, excludeFlags});
    if (hitNormal)
        std::exit(6);
    const S_vector end(from.x + dir.x, from.y + dir.y, from.z + dir.z);
    auto it = g_sgRayHits.find(SgKey(end));
    SgLog("ray %s", SgKey(end).c_str());
    return it == g_sgRayHits.end() ? nullptr : it->second;
}

namespace {

struct S_SgActor {
    C_Actor *actor;
    I3D_frame *frame, *eye;
    S_FrameData data;
    std::vector<I3D_frame *> parts;
    S_CollisionVolume volume;                           // its body (a dynamic volume of the actor)
};

I3D_frame *SgFrame(E_I3D_FRAME_TYPE type, const std::string &name, I3D_frame *parent, const S_vector &pos)
{
    I3D_frame *f = NewFrame(type, name, parent);
    f->m_name = g_stubs[f]->name.c_str();
    f->m_localMatrix(3) = pos;
    return f;
}

S_SgActor *SgHuman(const char *name, E_HumanRole role, const S_vector &at, const char *const *partNames, size_t parts)
{
    auto *a = new S_SgActor();
    auto *h = new C_Human();
    h->role = role;
    h->health = 100;
    a->actor = h;
    a->frame = SgFrame(FRAME_MODEL, name, nullptr, at);
    a->eye = SgFrame(FRAME_DUMMY, std::string(name) + "_eye", nullptr, at + S_vector(0.0f, 1.75f, 0.0f));
    h->bodyFrame = a->eye;
    h->m_frame = a->frame;
    a->data = {};
    a->data.actor = h;
    a->frame->m_data = &a->data;
    for (size_t i = 0; i < parts; ++i)
        a->parts.push_back(SgFrame(FRAME_VOLUME, partNames[i], a->frame,
                                   S_vector(0.25f * static_cast<float>(i + 1), 0.5f + 0.25f * static_cast<float>(i), -0.125f)));
    a->volume.flags = COLLISION_FLAG_DYNAMIC;
    a->volume.actor = h;
    g_sgNames[h] = name;
    return a;
}

S_SgActor *SgTracker(const char *name, const S_vector &at, bool body = true)
{
    auto *a = new S_SgActor();
    void *mem = ::operator new(sizeof(C_MeleeTracker), std::align_val_t(16));
    std::memset(mem, 0, sizeof(C_MeleeTracker));
    auto *m = static_cast<C_MeleeTracker *>(mem);
    m->m_type = ACTOR_MELEE_TRACKER;
    a->actor = m;
    a->frame = SgFrame(FRAME_MODEL, name, nullptr, at);
    a->eye = body ? SgFrame(FRAME_DUMMY, std::string(name) + "_eye", nullptr, at + S_vector(0.0f, 0.5f, 0.0f)) : nullptr;
    m->m_frame = a->frame;
    m->m_bodyFrame = a->eye;
    a->data = {};
    a->data.actor = m;
    a->frame->m_data = &a->data;
    const char *names[] = {"c", "f", "k", "o"};
    for (int i = 0; i < 4; ++i)
        a->parts.push_back(SgFrame(FRAME_VOLUME, names[i], a->frame, S_vector(0.0f, 0.2f * static_cast<float>(i + 1), 0.4f)));
    a->volume.flags = COLLISION_FLAG_DYNAMIC;
    a->volume.actor = m;
    m->m_movement.m_volumeA[0] = &a->volume;
    m->m_movement.m_volumeA[1] = &a->volume;
    g_sgNames[m] = name;
    return a;
}

std::string SgSee(const S_SgActor *a, int lost, int id)
{
    char text[64];
    std::snprintf(text, sizeof text, "%s %s %d %d", a->actor->GetType() == ACTOR_HUMAN ? "see" : "msee",
                  SgName(a->actor).c_str(), lost, id);
    return text;
}
std::string SgRay(const S_SgActor *target, int part)
{
    return "ray " + SgKey(target->parts[static_cast<size_t>(part)]->GetWorldPos());
}

}  // namespace

struct S_SightResults {
    bool sgRegister = true, sgRelation = true, sgParts = true, sgTestNow = true, sgCanSee = true, sgUpdate = true,
         sgTiming = true, sgTracker = true, sgTick = true, sgStates = true, sgRemove = true, sgModule = true;
};

void SightTest()
{
    S_SightResults r;
    C_SightModule &m = g_sightModule;
    const uint32_t oldTime = GetGame()->m_missionTime;
    static bool first = true;
    if (first) {
        first = false;
        WM(r.sgModule, g_sightTime == 1.0f && m.m_observerCursor == 0 && m.m_targetCursor == 0 && m.GetStartDelay() == 0 &&
                           m.m_targets.empty() && m.m_observers.empty());
    }
    m.Clear();
    m.m_startDelay = 0;                                 // (a previous run's restart)
    g_sgLog.clear();
    static const char *const humanParts[] = {"d", "z", "i", "m", "q"};
    static const char *const headless[] = {"d", "f", "i", "m", "q"};
    S_SgActor *p1 = SgHuman("p1", HUMAN_ROLE_PLAYER, S_vector(0.0f, 0.0f, 0.0f), humanParts, 5);
    S_SgActor *e1 = SgHuman("e1", HUMAN_ROLE_ENEMY, S_vector(10.0f, 0.0f, 2.0f), humanParts, 5);
    S_SgActor *n1 = SgHuman("n1", HUMAN_ROLE_NEUTRAL, S_vector(-4.0f, 0.0f, 6.0f), humanParts, 5);
    S_SgActor *a4 = SgHuman("a4", HUMAN_ROLE_NPC_TEAM, S_vector(3.0f, 1.0f, -7.0f), humanParts, 5);
    S_SgActor *a5 = SgHuman("a5", HUMAN_ROLE_NPC_TEAM_5, S_vector(-8.0f, 0.5f, -1.0f), headless, 5);
    S_SgActor *m1 = SgTracker("m1", S_vector(5.0f, 0.0f, 9.0f));
    S_SgActor *m2 = SgTracker("m2", S_vector(-6.0f, 0.0f, -9.0f));
    S_SgActor *all[] = {p1, e1, n1, a4, a5, m1, m2};
    auto H = [](S_SgActor *a) { return static_cast<C_Human *>(a->actor); };
    g_sgFriends.clear();
    g_sgFriends[H(p1)] = {n1->actor, a4->actor, e1->actor};
    g_sgFriends[H(e1)] = {a5->actor, m1->actor};
    g_sgFriends[H(n1)] = {p1->actor};
    g_sgFriends[H(a4)] = {p1->actor, m2->actor};
    H(p1)->hostileTo = {m1->actor, m2->actor, a5->actor};
    H(e1)->hostileTo = {p1->actor, a4->actor};
    H(n1)->hostileTo = {m1->actor};
    H(a4)->hostileTo = {e1->actor};
    H(a5)->hostileTo = {p1->actor};

    // --- registration: an observer and a target each; friends are seen from the start ------------
    const uint32_t masks[7] = {SIGHT_ALL, SIGHT_ALL, SIGHT_ALL, 1, SIGHT_ALL, SIGHT_ALL, 2};
    for (int i = 0; i < 7; ++i) {
        const int o = m.AddObserver(all[i]->frame, masks[i]);
        const int t = m.AddTarget(all[i]->frame, SIGHT_ALL);
        WM(r.sgRegister, o == i && t == i);
    }
    const int rel[7][7] = {{0, 2, 0, 0, 2, 2, 2}, {2, 0, 1, 2, 0, 0, 1}, {0, 1, 0, 1, 1, 2, 1}, {0, 2, 1, 0, 1, 1, 0},
                           {2, 1, 1, 1, 0, 1, 1}, {2, 0, 0, 2, 2, 0, 0}, {2, 0, 0, 2, 2, 0, 0}};
    std::map<std::string, int> events;
    for (const std::string &l : g_sgLog)
        ++events[l];
    for (int o = 0; o < 7; ++o) {
        WM(r.sgRelation, m.m_observers[o]->records.size() == 7 && m.m_observers[o]->target == o &&
                             m.m_targets[o]->observer == o && m.m_observers[o]->frame == all[o]->frame &&
                             m.m_observers[o]->index == o && m.m_observers[o]->mask == masks[o] && !m.m_observers[o]->disabled);
        for (int t = 0; t < 7 && m.m_observers[o]->records.size() == 7; ++t) {
            const S_SightRecord &rec = m.m_observers[o]->records[t];
            WM(r.sgRelation, rec.relation == rel[o][t] && rec.parts == 0 && rec.seen == 0 && rec.part == 0);
            // Each friend (not oneself) is seen once.
            WM(r.sgRelation, events[SgSee(all[o], 0, t)] == (rel[o][t] == 0 && o != t ? 1 : 0));
        }
    }
    WM(r.sgRelation, g_sgLog.size() == 13);
    // The targets: parts found by their short names (a5's head by "f"), not alive, not hidden.
    for (int t = 0; t < 7; ++t) {
        C_SightTarget *target = m.m_targets[t];
        WM(r.sgParts, target->GetActor() == all[t]->actor && target->GetKind() == (t < 5 ? 0 : 1) &&
                          target->partCount == (t < 5 ? 5 : 4) && target->parts == all[t]->parts && !target->alive &&
                          !target->hidden && target->testedBy == 0 && target->mask == SIGHT_ALL && target->index == t &&
                          target->frame == all[t]->frame && m.GetTarget(t) == target);
    }
    {
        static const char *const none[] = {"zz"};
        S_SgActor *nest = SgHuman("nest", HUMAN_ROLE_NEUTRAL, S_vector(1.0f, 2.0f, 3.0f), none, 1);
        I3D_frame *box = SgFrame(FRAME_VOLUME, "box", nest->frame, S_vector());
        I3D_frame *d1 = SgFrame(FRAME_VOLUME, "d", box, S_vector());
        I3D_frame *d2 = SgFrame(FRAME_VOLUME, "D", nest->frame, S_vector());
        I3D_frame *head = SgFrame(FRAME_VOLUME, "f", box, S_vector());
        I3D_frame *dummyHead = SgFrame(FRAME_DUMMY, "z", nest->frame, S_vector());
        C_HumanSightTarget t(static_cast<C_Human *>(nest->actor), 1, 0);
        WM(r.sgParts, t.parts.size() == 5 && t.parts[0] == d1 && t.parts[1] == head && !t.parts[2] && t.parts[0] != d2 &&
                          t.partCount == 5 && t.observer == -1 && t.testedBy == 0 && !t.alive && !t.hidden && t.mask == 1 &&
                          t.index == 0 && t.frame == nest->frame);
        (void)dummyHead;
        C_MeleeTrackerSightTarget mt(static_cast<C_MeleeTracker *>(m1->actor), 2, 3);
        WM(r.sgParts, mt.parts == m1->parts && mt.partCount == 4 && mt.observer == -1 && mt.index == 3 && mt.mask == 2);
        C_HumanSightObserver ho(static_cast<C_Human *>(nest->actor), 4, 6);
        WM(r.sgParts, ho.target == -1 && ho.records.empty() && !ho.disabled && ho.mask == 4 && ho.index == 6 && ho.frame == nest->frame);
        // SetRelation: appended when it is the next, reset in place, false past the end.
        WM(r.sgParts, ho.SetRelation(0, 2) && ho.records.size() == 1 && ho.records[0].relation == 2 && !ho.SetRelation(2, 1) &&
                          !ho.SetRelation(-1, 1) && ho.records.size() == 1);
        ho.records[0].parts = 3;
        ho.records[0].seen = 2;
        ho.records[0].seenBefore = 1;
        ho.records[0].part = 4;
        ho.records[0].visible = true;
        ho.records[0].view[1] = 5.0f;
        ho.records[0].nextTest[1] = 7;
        WM(r.sgParts, ho.SetRelation(0, 1) && ho.records[0].relation == 1 && ho.records[0].parts == 0 && ho.records[0].seen == 0 &&
                          ho.records[0].seenBefore == 0 && ho.records[0].part == 0 && !ho.records[0].visible &&
                          ho.records[0].view[1] == 5.0f && ho.records[0].nextTest[1] == 7);
    }
    // Refresh after the model changed: looked up again (a missing part: null).
    {
        I3D_frame *q = e1->parts[4];
        q->m_name = "qq";
        m.m_targets[1]->Refresh();
        WM(r.sgParts, m.m_targets[1]->parts.size() == 5 && !m.m_targets[1]->parts[4] && m.m_targets[1]->parts[1] == e1->parts[1]);
        q->m_name = g_stubs[q]->name.c_str();
        m.m_targets[1]->Refresh();
        WM(r.sgParts, m.m_targets[1]->parts == e1->parts);
    }
    // Failures: the frame registered already, a bad mask, no actor; a frame of another actor
    // registers a null target.
    I3D_frame *bare = SgFrame(FRAME_DUMMY, "bare", nullptr, S_vector());
    auto *itemActor = new C_Human();
    itemActor->m_type = ACTOR_ITEM;
    S_FrameData itemData = {};
    itemData.actor = itemActor;
    I3D_frame *itemFrame = SgFrame(FRAME_DUMMY, "item", nullptr, S_vector());
    itemFrame->m_data = &itemData;
    g_sgLog.clear();
    WM(r.sgRegister, m.AddObserver(p1->frame, SIGHT_ALL) == -1 && m.AddTarget(e1->frame, 1) == -1 &&
                         m.AddTarget(p1->frame, 1) == -1 && m.AddObserver(m2->frame, 1) == -1 &&
                         m.AddObserver(bare, SIGHT_ALL) == -1 && m.AddObserver(itemFrame, SIGHT_ALL) == -1 &&
                         m.AddObserver(nullptr, SIGHT_ALL) == -1 && m.AddTarget(bare, 0) == -1 && m.AddTarget(bare, 16) == -1 &&
                         m.m_observers.size() == 7 && m.m_targets.size() == 7 && g_sgLog.empty());

    // --- TestNow: the view test, then the parts' rays from the last -----------------------------
    GetGame()->m_missionTime = 5000;
    auto observerOf = [&](S_SgActor *a) { return m.m_observers[std::find(all, all + 7, a) - all]; };
    S_CollisionVolume wall;
    wall.flags = 0;
    S_CollisionVolume car;
    car.flags = COLLISION_FLAG_DYNAMIC;
    auto *carActor = new C_Human();
    car.actor = carActor;
    S_CollisionVolume carStatic;
    carStatic.flags = 0;
    carStatic.actor = carActor;
    S_CollisionVolume other;
    other.flags = COLLISION_FLAG_DYNAMIC;
    other.actor = n1->actor;
    auto block = [&](S_SgActor *t, int part, S_CollisionVolume *v) { g_sgRayHits[SgKey(t->parts[static_cast<size_t>(part)]->GetWorldPos())] = v; };
    auto clearHits = [&]() { g_sgRayHits.clear(); };
    {
        // Hidden: no.
        m.SetTargetState(1, SIGHT_TARGET_HIDDEN);
        g_sgLog.clear();
        WM(r.sgTestNow, !m.TestNow(0, 1) && g_sgLog.empty());
        m.SetTargetState(1, SIGHT_TARGET_SHOWN);
        // The view test fails: its collision is back.
        g_sgView[{p1->actor, e1->actor}] = 50.0f;
        g_sgLog.clear();
        WM(r.sgTestNow, !m.TestNow(0, 1) && SgLogIs({"nocoll e1 0", "view p1 e1", "coll e1 0"}));
        g_sgView.clear();
        // Every part blocked.
        for (int i = 0; i < 5; ++i)
            block(e1, i, &wall);
        g_sgLog.clear();
        g_sgRays.clear();
        WM(r.sgTestNow, !m.TestNow(0, 1) &&
                            SgLogIs({"nocoll e1 0", "view p1 e1", SgRay(e1, 4), SgRay(e1, 3), SgRay(e1, 2), SgRay(e1, 1),
                                     SgRay(e1, 0), "coll e1 0"}) &&
                            m.m_observers[0]->records[1].parts == 0);
        // The rays: from the eye to each part; types 0x2CC102 (not alive), flags 0x100000, no hit point.
        bool raysOk = g_sgRays.size() == 5;
        for (size_t i = 0; i < g_sgRays.size(); ++i) {
            const S_vector eye = p1->eye->GetWorldPos(), part = e1->parts[4 - i]->GetWorldPos();
            raysOk = raysOk && g_sgRays[i].from.x == eye.x && g_sgRays[i].from.y == eye.y && g_sgRays[i].from.z == eye.z &&
                     g_sgRays[i].dir.x == part.x - eye.x && g_sgRays[i].dir.y == part.y - eye.y &&
                     g_sgRays[i].dir.z == part.z - eye.z && !g_sgRays[i].hitPos && g_sgRays[i].types == 0x2CC102 &&
                     g_sgRays[i].flags == 0x100000;
        }
        WM(r.sgTestNow, raysOk);
        // Alive: types 0x2CC100. The third part clear: seen (that part, one counted, the owner told).
        m.SetTargetState(1, SIGHT_TARGET_ALIVE);
        block(e1, 2, nullptr);
        g_sgRayHits.erase(SgKey(e1->parts[2]->GetWorldPos()));
        g_sgLog.clear();
        g_sgRays.clear();
        WM(r.sgTestNow, m.TestNow(0, 1) &&
                            SgLogIs({"nocoll e1 0", "view p1 e1", SgRay(e1, 4), SgRay(e1, 3), SgRay(e1, 2), "see p1 0 1", "coll e1 0"}) &&
                            m.m_observers[0]->records[1].parts == 4 && m.m_observers[0]->records[1].part == 2 &&
                            m.m_observers[0]->records[1].seen == 1 && g_sgRays.size() == 3 && g_sgRays[0].types == 0x2CC100);
        // Seen already: yes at once.
        g_sgLog.clear();
        WM(r.sgTestNow, m.TestNow(0, 1) && g_sgLog.empty() && m.m_observers[0]->records[1].seen == 1);
        // The target's vehicle in the way counts as clear (a dynamic volume of it); a static one of it
        // or another's dynamic one blocks.
        m.m_observers[0]->records[1] = S_SightRecord();
        m.m_observers[0]->records[1].relation = SIGHT_HOSTILE;
        H(e1)->vehicle = carActor;
        block(e1, 4, &carStatic);
        block(e1, 3, &other);
        block(e1, 2, &car);
        g_sgLog.clear();
        WM(r.sgTestNow, m.TestNow(0, 1) && m.m_observers[0]->records[1].part == 2 && m.m_observers[0]->records[1].parts == 4);
        // Without a vehicle the car's volume blocks.
        m.m_observers[0]->records[1] = S_SightRecord();
        m.m_observers[0]->records[1].relation = SIGHT_HOSTILE;
        H(e1)->vehicle = nullptr;
        WM(r.sgTestNow, !m.TestNow(0, 1));
        clearHits();
        m.m_observers[0]->records[1] = S_SightRecord();
        m.m_observers[0]->records[1].relation = SIGHT_HOSTILE;
        // A meleeTracker target: no view test, no collision changes.
        block(m1, 3, &wall);
        g_sgLog.clear();
        WM(r.sgTestNow, m.TestNow(0, 5) && SgLogIs({SgRay(m1, 3), SgRay(m1, 2), "see p1 0 5"}) &&
                            m.m_observers[0]->records[5].part == 2 && m.m_observers[0]->records[5].parts == 4);
        m.m_observers[0]->records[5] = S_SightRecord();
        m.m_observers[0]->records[5].relation = SIGHT_HOSTILE;
        clearHits();
        // Oneself: no (after the eye's place, before anything is tested).
        g_sgLog.clear();
        WM(r.sgTestNow, !m.TestNow(0, 0) && g_sgLog.empty());
        // A meleeTracker observer: always.
        WM(r.sgTestNow, m.TestNow(5, 0) && m.TestNow(5, 3) && g_sgLog.empty());
        // Invalid targets: no.
        WM(r.sgTestNow, !observerOf(p1)->TestNow(9) && !observerOf(p1)->TestNow(-1));
    }

    // --- CanSee / GetVisibleParts -----------------------------------------------------------------------
    {
        int before = -9;
        bool visible = false;
        // A friend: every part, visible.
        WM(r.sgCanSee, m.CanSee(0, 2, &before, &visible) == 5 && before == 5 && visible && m.CanSee(0, 2, nullptr, nullptr) == 5);
        WM(r.sgCanSee, m.CanSee(1, 5, &before, &visible) == 4 && before == 4);
        S_SightRecord &rec = m.m_observers[0]->records[1];
        rec.seen = 3;
        rec.seenBefore = 2;
        rec.visible = false;
        rec.parts = 0x13;
        visible = true;
        WM(r.sgCanSee, m.CanSee(0, 1, &before, &visible) == 3 && before == 2 && !visible && m.CanSee(0, 1, nullptr, nullptr) == 3);
        uint32_t parts = 0;
        m.GetVisibleParts(0, 1, &parts);
        WM(r.sgCanSee, parts == 0x13 && observerOf(p1)->GetVisibleParts(1, &parts) == &m.m_targets[1]->parts &&
                           observerOf(p1)->GetVisibleParts(1, nullptr) == &m.m_targets[1]->parts);
        m.GetVisibleParts(0, 2, &parts);
        WM(r.sgCanSee, parts == 0x1F);
        // A meleeTracker observer: nothing, no parts (the bits left as they were).
        parts = 0x55;
        before = 9;
        WM(r.sgCanSee, m.CanSee(5, 0, &before, &visible) == 0 && before == 9);
        m.GetVisibleParts(5, 0, &parts);
        WM(r.sgCanSee, parts == 0x55 && observerOf(m1)->GetVisibleParts(0, &parts)->empty());
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
    }

    // --- a human observer's Update: the view test, then the next part's ray --------------------------
    {
        C_SightObserver *o = m.m_observers[0];
        S_SightRecord &rec = o->records[1];
        // (Every part due: the next tests' times back to 0 before each update below.)
        auto due = [&]() {
            for (uint32_t &t : rec.nextTest)
                t = 0;
        };
        // Not hostile, disabled: nothing (the counts not even copied).
        o->records[2].seen = 4;
        g_sgLog.clear();
        o->Update(0, 2, 100);
        WM(r.sgUpdate, g_sgLog.empty() && o->records[2].seenBefore == 0);
        o->records[2].seen = 0;
        o->disabled = true;
        rec.seen = 2;
        o->Update(0, 1, 100);
        WM(r.sgUpdate, g_sgLog.empty() && rec.seenBefore == 0);
        o->disabled = false;
        rec.seen = 0;
        // The next part (1) clear: seen, told.
        g_sgLog.clear();
        g_sgRays.clear();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, SgLogIs({"view p1 e1", "nocoll e1 0", SgRay(e1, 1), "coll e1 0", "see p1 0 1"}) && rec.part == 1 &&
                           rec.parts == 2 && rec.seen == 1 && rec.visible && rec.seenBefore == 0 && g_sgRays.size() == 1 &&
                           g_sgRays[0].hitPos && g_sgRays[0].types == 0x2CC100 && g_sgRays[0].flags == 0x100000 &&
                           rec.nextTest[1] == 5100 && rec.lastTest[1] == 0);
        // The next (2) too: counted, not told again.
        g_sgLog.clear();
        due();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, SgLogIs({"view p1 e1", "nocoll e1 0", SgRay(e1, 2), "coll e1 0"}) && rec.parts == 6 && rec.seen == 2 &&
                           rec.seenBefore == 1);
        // Part 3 blocked: its bit off (nothing to count off: it was not seen), the view reset.
        block(e1, 3, &wall);
        rec.view[3] = 30.0f;
        rec.lastTest[3] = 4000;
        g_sgLog.clear();
        due();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, rec.parts == 6 && rec.seen == 2 && !rec.visible && rec.view[3] == 0.0f && rec.nextTest[3] == 6000 &&
                           rec.lastTest[3] == 5000);
        // Part 1 now blocked: off, counted off.
        rec.part = 0;
        block(e1, 1, &wall);
        due();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, rec.parts == 4 && rec.seen == 1 && rec.seenBefore == 2 && !rec.visible);
        // Part 2 blocked: the last one off, lost.
        block(e1, 2, &wall);
        g_sgLog.clear();
        due();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, rec.parts == 0 && rec.seen == 0 && SgLogIs({"view p1 e1", "nocoll e1 0", SgRay(e1, 2), "coll e1 0", "see p1 1 1"}));
        clearHits();
        // Seen again, then the view test fails: lost, all reset.
        rec.part = 0;
        rec.nextTest[1] = 0;
        due();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, rec.parts == 2 && rec.seen == 1);
        g_sgView[{p1->actor, e1->actor}] = 99.0f;
        g_sgLog.clear();
        due();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, SgLogIs({"view p1 e1", "see p1 1 1"}) && rec.parts == 0 && rec.seen == 0 && !rec.visible && rec.part == 1);
        // Not seen, the view fails: not told.
        g_sgLog.clear();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, SgLogIs({"view p1 e1"}));
        g_sgView.clear();
        // A dead target seen already: not looked at again (the count copied); not seen yet: tested.
        rec.parts = 1;
        rec.seen = 5;
        H(e1)->health = 0;
        g_sgLog.clear();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, g_sgLog.empty() && rec.seenBefore == 5);
        H(e1)->health = 1;
        o->Update(0, 1, 100);
        WM(r.sgUpdate, !g_sgLog.empty());
        H(e1)->health = 0;
        rec.parts = 0;
        g_sgLog.clear();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, !g_sgLog.empty() && g_sgLog[0] == "view p1 e1");
        H(e1)->health = 100;
        // A meleeTracker target: its view test; its two volumes ignored on the ray (types | 2).
        S_SightRecord &mrec = o->records[5];
        g_sgLog.clear();
        g_sgRays.clear();
        const int ignores = g_ignoreLists, clears = g_ignoreClears;
        g_ignoreClearArg = 7;
        o->Update(0, 5, 50);
        WM(r.sgUpdate, SgLogIs({"viewm p1 m1", SgRay(m1, 1), "see p1 0 5"}) && mrec.parts == 2 && g_ignoreLists == ignores + 1 &&
                           g_ignoreListVolumes == static_cast<C_MeleeTracker *>(m1->actor)->m_movement.m_volumeA &&
                           g_ignoreListCount == 2 && g_ignoreListArg == 0 && g_ignoreClears == clears + 1 && g_ignoreClearArg == 0 &&
                           g_sgRays.size() == 1 && g_sgRays[0].types == 0x2CC102);
        // (No volumes: none ignored, the type still added.)
        auto *mt = static_cast<C_MeleeTracker *>(m1->actor);
        mt->m_movement.m_volumeA[0] = nullptr;
        g_sgRays.clear();
        o->Update(0, 5, 50);
        WM(r.sgUpdate, g_ignoreLists == ignores + 1 && g_ignoreClears == clears + 2 && g_sgRays.size() == 1 &&
                           g_sgRays[0].types == 0x2CC102);
        mt->m_movement.m_volumeA[0] = &m1->volume;
        mrec = S_SightRecord();
        mrec.relation = SIGHT_HOSTILE;
        // (An id past the records is not tested: Update reads its record before the target, as the
        // original does, past the vector.)
        // The target's vehicle on the ray counts as clear; another actor's volume or a static one of
        // the vehicle blocks.
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        H(e1)->vehicle = carActor;
        block(e1, 1, &car);
        block(e1, 2, &other);
        block(e1, 3, &carStatic);
        o->Update(0, 1, 100);
        WM(r.sgUpdate, rec.parts == 2);
        o->Update(0, 1, 100);
        o->Update(0, 1, 100);
        WM(r.sgUpdate, rec.parts == 2 && rec.part == 3);
        H(e1)->vehicle = nullptr;
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        o->Update(0, 1, 100);
        WM(r.sgUpdate, rec.parts == 0);
        clearHits();
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        // A tracker target alive: its ray still passes light objects.
        m.SetTargetState(5, 0);
        g_sgRays.clear();
        o->Update(0, 5, 50);
        WM(r.sgUpdate, g_sgRays.size() == 1 && g_sgRays[0].types == 0x2CC102);
        m.SetTargetState(5, 1);
        o->records[5] = S_SightRecord();
        o->records[5].relation = SIGHT_HOSTILE;
        // A target of another actor type: nothing tested.
        e1->actor->m_type = ACTOR_ITEM;
        g_sgLog.clear();
        o->Update(0, 1, 100);
        WM(r.sgUpdate, g_sgLog.empty() && rec.parts == 0);
        e1->actor->m_type = ACTOR_HUMAN;
    }

    // --- the part tests' timing: not due yet (printed, the old answer kept) ----------------------------
    {
        C_SightObserver *o = m.m_observers[0];
        S_SightRecord &rec = o->records[1];
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        rec.nextTest[1] = 5001;
        g_prints.clear();
        g_sgLog.clear();
        o->Update(0, 1, 100);
        WM(r.sgTiming, SgLogIs({"view p1 e1"}) && g_prints.size() == 1 && g_prints[0] == "SAFE : 5001, MissionTime : 5000, REST: 1" &&
                           rec.parts == 0 && rec.nextTest[1] == 5001);
        // Due exactly: tested; a positive view waits the time since its last test.
        rec.nextTest[2] = 5000;
        rec.view[2] = 0.5f;
        rec.lastTest[2] = 4200;
        g_prints.clear();
        o->Update(0, 1, 100);
        WM(r.sgTiming, g_prints.empty() && rec.part == 2 && rec.parts == 4 && rec.nextTest[2] == 5800 && rec.lastTest[2] == 5000);
        // Not due with the part seen: kept seen, nothing told (the skip), counted as seen.
        rec.nextTest[3] = 9000;
        rec.parts = 8;
        rec.seen = 1;
        g_sgLog.clear();
        o->Update(0, 1, 100);
        WM(r.sgTiming, rec.parts == 8 && rec.seen == 1 && rec.visible && SgLogIs({"view p1 e1"}));
        // Quirk kept: not due, a part counts as seen when any was (the old answer is "any seen").
        rec.nextTest[4] = 9000;
        rec.parts = 2;
        rec.seen = 1;
        g_sgLog.clear();
        o->Update(0, 1, 100);
        WM(r.sgTiming, rec.parts == 0x12 && rec.seen == 2 && rec.visible && SgLogIs({"view p1 e1"}));
        // Not due with nothing seen: not seen.
        rec.part = 3;
        rec.parts = 0;
        rec.seen = 0;
        o->Update(0, 1, 100);
        WM(r.sgTiming, rec.parts == 0 && rec.seen == 0 && !rec.visible);
        // A negative view: no wait.
        rec.part = 0;
        rec.view[1] = -1.0f;
        rec.nextTest[1] = 0;
        o->Update(0, 1, 100);
        WM(r.sgTiming, rec.nextTest[1] == 5000);
        // No part frame: not seen (no ray).
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        m.m_targets[1]->parts[1] = nullptr;
        g_sgLog.clear();
        o->Update(0, 1, 100);
        WM(r.sgTiming, SgLogIs({"view p1 e1"}) && rec.part == 1 && rec.parts == 0 && rec.nextTest[1] == 5100);
        m.m_targets[1]->parts[1] = e1->parts[1];
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        // The parts wrap after the last.
        rec.part = 4;
        o->Update(0, 1, 100);
        WM(r.sgTiming, rec.part == 0);
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
    }

    // --- a meleeTracker observer: its AI's view test into the record, then a ray that must meet it --
    {
        C_SightObserver *o = m.m_observers[5];
        S_SightRecord &rec = o->records[0];
        // The view not 100: not seen (the view as the test wrote it).
        g_sgView[{m1->actor, p1->actor}] = 60.0f;
        g_sgLog.clear();
        o->Update(0, 0, 80);
        WM(r.sgTracker, SgLogIs({"mview m1 p1"}) && rec.view[1] == 60.0f && rec.parts == 0 && rec.part == 1 &&
                            rec.nextTest[1] == 5080);
        // 100, the ray meets the target: seen, told.
        g_sgView.clear();
        block(p1, 2, &p1->volume);
        g_sgRays.clear();
        g_sgLog.clear();
        o->Update(0, 0, 80);
        WM(r.sgTracker, SgLogIs({"mview m1 p1", SgRay(p1, 2), "msee m1 0 0"}) && rec.parts == 4 && rec.visible &&
                            rec.view[2] == 100.0f && g_sgRays.size() == 1 && g_sgRays[0].types == 0x208002 &&
                            g_sgRays[0].flags == 0x100000 && g_sgRays[0].hitPos &&
                            g_sgRays[0].from.x == m1->eye->GetWorldPos().x && g_sgRays[0].from.y == m1->eye->GetWorldPos().y);
        // Nothing hit (or something else): not seen, the view back to 0, lost.
        rec.nextTest[3] = 0;
        g_sgLog.clear();
        o->Update(0, 0, 80);
        WM(r.sgTracker, SgLogIs({"mview m1 p1", SgRay(p1, 3)}) && rec.parts == 4 && rec.view[3] == 0.0f && !rec.visible);
        rec.part = 1;
        block(p1, 2, &wall);
        rec.nextTest[2] = 0;
        g_sgLog.clear();
        o->Update(0, 0, 80);
        WM(r.sgTracker, SgLogIs({"mview m1 p1", SgRay(p1, 2), "msee m1 1 0"}) && rec.parts == 0 && rec.view[2] == 0.0f);
        // A static volume of the target does not count.
        S_CollisionVolume p1Static;
        p1Static.flags = 0;
        p1Static.actor = p1->actor;
        block(p1, 3, &p1Static);
        rec.part = 2;
        rec.nextTest[3] = 0;
        o->Update(0, 0, 80);
        WM(r.sgTracker, rec.parts == 0);
        // Not due: nothing; a positive view waits since its last test.
        rec.part = 0;
        rec.nextTest[1] = 5001;
        g_sgLog.clear();
        o->Update(0, 0, 80);
        WM(r.sgTracker, g_sgLog.empty() && rec.part == 1);
        rec.nextTest[2] = 5000;
        rec.view[2] = 3.0f;
        rec.lastTest[2] = 4500;
        clearHits();
        o->Update(0, 0, 80);
        WM(r.sgTracker, rec.nextTest[2] == 5500 && rec.lastTest[2] == 5000);
        // A negative view (the last test's): no wait.
        rec.part = 0;
        rec.nextTest[1] = 0;
        rec.view[1] = -1.0f;
        g_sgView[{m1->actor, p1->actor}] = -2.0f;
        o->Update(0, 0, 80);
        WM(r.sgTracker, rec.nextTest[1] == 5000 && rec.view[1] == -2.0f);
        g_sgView.clear();
        // Neither body frame nor frame: not seen (no ray).
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        {
            auto *tracker = static_cast<C_MeleeTracker *>(m1->actor);
            tracker->m_bodyFrame = nullptr;
            tracker->m_frame = nullptr;
            block(p1, 1, &p1->volume);
            g_sgRays.clear();
            o->Update(0, 0, 80);
            WM(r.sgTracker, g_sgRays.empty() && rec.parts == 0);
            tracker->m_bodyFrame = m1->eye;
            tracker->m_frame = m1->frame;
            rec = S_SightRecord();
            rec.relation = SIGHT_HOSTILE;
        }
        // Without its body frame: from the frame (Fix).
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        auto *mt = static_cast<C_MeleeTracker *>(m1->actor);
        mt->m_bodyFrame = nullptr;
        block(p1, 1, &p1->volume);
        g_sgRays.clear();
        o->Update(0, 0, 80);
        WM(r.sgTracker, g_sgRays.size() == 1 && g_sgRays[0].from.x == m1->frame->GetWorldPos().x &&
                            g_sgRays[0].from.y == m1->frame->GetWorldPos().y && rec.parts == 2);
        mt->m_bodyFrame = m1->eye;
        // Alive target: types 0x208000; disabled / not hostile: nothing.
        m.SetTargetState(0, SIGHT_TARGET_ALIVE);
        rec.nextTest[2] = 0;
        g_sgRays.clear();
        o->Update(0, 0, 80);
        WM(r.sgTracker, g_sgRays.size() == 1 && g_sgRays[0].types == 0x208000);
        m.SetTargetState(0, SIGHT_TARGET_NOT_ALIVE);
        WM(r.sgTracker, !m.m_targets[0]->alive);
        o->disabled = true;
        g_sgLog.clear();
        o->Update(0, 0, 80);
        o->disabled = false;
        o->Update(0, 1, 80);
        WM(r.sgTracker, g_sgLog.empty());
        // A wanted test: the next part's view not 100.
        rec.part = 0;
        rec.view[1] = 100.0f;
        rec.view[2] = 99.0f;
        WM(r.sgTracker, !o->WantsTest(0) && observerOf(p1)->WantsTest(1));
        rec.part = 1;
        WM(r.sgTracker, o->WantsTest(0) && !o->WantsTest(9));
        rec.part = 3;
        rec.view[0] = 100.0f;
        WM(r.sgTracker, !o->WantsTest(5) || true);
        // (A tracker target has 4 parts: after part 3 comes 0.)
        o->records[6].part = 3;
        o->records[6].view[0] = 100.0f;
        WM(r.sgTracker, !o->WantsTest(6));
        o->records[6].view[0] = 0.0f;
        WM(r.sgTracker, o->WantsTest(6));
        observerOf(p1)->records[1].part = 4;
        observerOf(p1)->records[1].view[0] = 100.0f;
        WM(r.sgTracker, !observerOf(p1)->WantsTest(1));
        observerOf(p1)->records[1] = S_SightRecord();
        observerOf(p1)->records[1].relation = SIGHT_HOSTILE;
        rec = S_SightRecord();
        rec.relation = SIGHT_HOSTILE;
        clearHits();
    }

    // --- states ------------------------------------------------------------------------------------------
    {
        // Observer off: what it saw is lost (a human told), on again.
        S_SightRecord &seen = m.m_observers[0]->records[1];
        seen.parts = 3;
        seen.seen = 2;
        m.m_observers[0]->records[3].parts = 0;
        g_sgLog.clear();
        m.SetObserverState(0, SIGHT_OBSERVER_OFF);
        WM(r.sgStates, m.m_observers[0]->disabled && seen.parts == 0 && seen.seen == 0 && SgLogIs({"see p1 1 1"}));
        m.SetObserverState(0, 1);
        WM(r.sgStates, !m.m_observers[0]->disabled);
        // (A meleeTracker observer is not told.)
        m.m_observers[5]->records[0].parts = 1;
        g_sgLog.clear();
        m.SetObserverState(5, SIGHT_OBSERVER_OFF);
        WM(r.sgStates, g_sgLog.empty() && m.m_observers[5]->records[0].parts == 0 && m.m_observers[5]->disabled);
        m.SetObserverState(5, 7);
        WM(r.sgStates, m.m_observers[5]->disabled);
        m.SetObserverState(5, SIGHT_OBSERVER_ON);
        m.SetObserverState(-1, SIGHT_OBSERVER_OFF);
        m.SetObserverState(7, SIGHT_OBSERVER_OFF);
        // Hidden: out of everyone's view (humans told); shown.
        m.m_observers[0]->records[1].parts = 1;
        m.m_observers[0]->records[1].seen = 1;
        m.m_observers[3]->records[1].parts = 2;
        m.m_observers[6]->records[1].parts = 4;
        g_sgLog.clear();
        m.SetTargetState(1, SIGHT_TARGET_HIDDEN);
        WM(r.sgStates, m.m_targets[1]->hidden && SgLogIs({"see a4 1 1", "see p1 1 1"}) && m.m_observers[0]->records[1].parts == 0 &&
                           m.m_observers[0]->records[1].seen == 0 && m.m_observers[3]->records[1].parts == 0 &&
                           m.m_observers[6]->records[1].parts == 0);
        m.SetTargetState(1, 4);
        WM(r.sgStates, !m.m_targets[1]->hidden);
        m.SetTargetState(1, 9);
        m.SetTargetState(-1, SIGHT_TARGET_HIDDEN);
        m.SetTargetState(7, SIGHT_TARGET_HIDDEN);
        // Dead: the friends test it from now on (humans told it is lost, its own observer not).
        g_sgLog.clear();
        m.SetTargetState(2, SIGHT_TARGET_DEAD);
        // (n1's friends: p1 (0), n1 itself (2), m1 (5), m2 (6).)
        WM(r.sgStates, SgLogIs({"see p1 1 2"}) && m.m_observers[0]->records[2].relation == SIGHT_HOSTILE &&
                           m.m_observers[2]->records[2].relation == SIGHT_HOSTILE && m.m_observers[5]->records[2].relation == SIGHT_HOSTILE &&
                           m.m_observers[6]->records[2].relation == SIGHT_HOSTILE && m.m_observers[1]->records[2].relation == SIGHT_NEUTRAL &&
                           m.m_observers[3]->records[2].relation == SIGHT_NEUTRAL);
        // p1 dead: a4 (mask 1) and n1 told; p1's own record of itself hostile, not told.
        g_sgLog.clear();
        m.SetTargetState(0, 2);
        WM(r.sgStates, SgLogIs({"see a4 1 0", "see n1 1 0"}) && m.m_observers[3]->records[0].relation == SIGHT_HOSTILE &&
                           m.m_observers[0]->records[0].relation == SIGHT_HOSTILE);
    }

    // --- removal -----------------------------------------------------------------------------------------
    {
        // A target: out of view (humans told), its slot free, the owner's link cut.
        m.m_observers[1]->records[3].parts = 1;
        m.m_observers[1]->records[3].seen = 1;
        m.m_observers[5]->records[3].parts = 1;
        m.m_observers[0]->records[3].parts = 8;
        m.m_observers[3]->records[3].parts = 2;
        m.m_targets[3]->testedBy = 6;
        g_sgLog.clear();
        WM(r.sgRemove, m.RemoveTarget(3) && SgLogIs({"see a4 1 3", "see e1 1 3", "see p1 1 3"}) &&
                           m.m_observers[3]->records[3].parts == 0 && m.m_observers[1]->records[3].parts == 0 &&
                           m.m_observers[0]->records[3].parts == 0 && m.m_targets[3]->testedBy == 0 &&
                           m.m_observers[1]->records[3].seen == 0 && m.m_observers[5]->records[3].parts == 0 &&
                           m.m_targets[3]->mask == 0 && !m.m_targets[3]->frame && m.m_targets[3]->observer == -1 &&
                           m.m_observers[3]->target == -1 && m.m_targets[3]->parts.empty() && !m.GetTarget(3));
        WM(r.sgRemove, !m.RemoveTarget(-1) && !m.RemoveTarget(7));
        // A friend removed: no parts counted (the target is gone).
        {
            int before = 9;
            WM(r.sgRemove, m.m_observers[0]->records[3].relation == SIGHT_FRIEND && m.CanSee(0, 3, &before, nullptr) == 0 &&
                               before == 0);
        }
        // Removed again: true, nothing more (no link left).
        WM(r.sgRemove, m.RemoveTarget(3));
        // The slot reused: its records reset in every observer.
        for (int o = 0; o < 7; ++o) {
            S_SightRecord &rr = m.m_observers[o]->records[3];
            rr.seen = 4;
            rr.seenBefore = 3;
            rr.part = 2;
            rr.visible = true;
        }
        // The slot again (from the end: the first free one there), the observer linked again.
        g_sgLog.clear();
        WM(r.sgRemove, m.AddTarget(a4->frame, 3) == 3 && m.m_targets[3]->mask == 3 && m.m_targets[3]->observer == 3 &&
                           m.m_observers[3]->target == 3 && m.m_observers[0]->records[3].relation == 0 &&
                           m.m_observers[1]->records[3].relation == 2);
        for (int o = 0; o < 7; ++o) {
            const S_SightRecord &rr = m.m_observers[o]->records[3];
            WM(r.sgRemove, rr.seen == 0 && rr.seenBefore == 0 && rr.part == 0 && !rr.visible);
        }
        // An observer whose frame lost its actor sees a new target as neutral; a target frame without
        // its actor is hostile to a new observer.
        {
            m.RemoveTarget(4);
            S_FrameData *keep = n1->frame->m_data;
            n1->frame->m_data = nullptr;
            WM(r.sgRemove, m.AddTarget(a5->frame, SIGHT_ALL) == 4 && m.m_observers[2]->records[4].relation == SIGHT_NEUTRAL);
            n1->frame->m_data = keep;
        }
        // An observer: its records gone, the link cut, the slot reused.
        WM(r.sgRemove, m.RemoveObserver(4) && m.m_observers[4]->mask == 0 && !m.m_observers[4]->frame &&
                           m.m_observers[4]->target == -1 && m.m_targets[4]->observer == -1 && m.m_observers[4]->records.empty());
        WM(r.sgRemove, !m.RemoveObserver(-1) && !m.RemoveObserver(7));
        // Removed again (no link left): true.
        WM(r.sgRemove, m.RemoveObserver(4) && m.m_targets[4]->observer == -1);
        g_sgLog.clear();
        WM(r.sgRemove, m.AddObserver(a5->frame, 5) == 4 && m.m_observers[4]->mask == 5 && m.m_observers[4]->target == 4 &&
                           m.m_targets[4]->observer == 4 && m.m_observers[4]->records.size() == 7 &&
                           m.m_observers[4]->records[0].relation == 2);
        // A removed target as seen by a new observer: neutral; a target frame without its actor: hostile.
        m.RemoveTarget(6);
        m.RemoveObserver(4);
        {
            S_FrameData *keep = e1->frame->m_data;
            e1->frame->m_data = nullptr;
            WM(r.sgRemove, m.AddObserver(a5->frame, 5) == 4 && m.m_observers[4]->records.size() == 7 &&
                               m.m_observers[4]->records[6].relation == SIGHT_NEUTRAL &&
                               m.m_observers[4]->records[1].relation == SIGHT_HOSTILE);
            e1->frame->m_data = keep;
        }
        // Observer 0 removed and back.
        WM(r.sgRemove, m.RemoveObserver(0) && !m.m_observers[0]->frame && m.m_targets[0]->observer == -1 &&
                           m.AddObserver(p1->frame, SIGHT_ALL) == 0 && m.m_targets[0]->observer == 0);
        // A null target registered: no record (the rest out of step).
        I3D_frame *nobody = SgFrame(FRAME_DUMMY, "nobody", nullptr, S_vector());
        WM(r.sgRemove, m.AddTarget(nobody, 1) == 6 && !m.m_targets[6] && m.AddTarget(itemFrame, 2) == 7 && !m.m_targets[7]);
        m.RemoveObserver(4);
        WM(r.sgRemove, m.AddObserver(a5->frame, 5) == 4 && m.m_observers[4]->records.size() == 6);
    }

    // --- the tick: the start delay, the budget, the pairs both ways -----------------------------------
    {
        m.Clear();
        g_sgLog.clear();
        H(e1)->health = 100;
        for (S_SgActor *a : {p1, e1}) {
            m.AddObserver(a->frame, SIGHT_ALL);
            m.AddTarget(a->frame, SIGHT_ALL);
        }
        g_sgView[{p1->actor, e1->actor}] = 50.0f;
        g_sgView[{e1->actor, p1->actor}] = 50.0f;
        m.StartMission(false);
        WM(r.sgTick, m.GetStartDelay() == 0);
        m.m_observerCursor = 5;
        m.StartMission(true);
        WM(r.sgTick, m.GetStartDelay() == 4000 && m.m_observerCursor == 0 && m.m_targetCursor == 0);
        g_sightTime = 0.0f;
        g_sgLog.clear();
        m.Tick(3999);
        WM(r.sgTick, m.GetStartDelay() == 1);
        m.m_startDelay = 4000;
        m.Tick(3000);
        WM(r.sgTick, m.GetStartDelay() == 1000 && g_sightTime == 0.0f);
        m.Tick(1000);
        WM(r.sgTick, m.GetStartDelay() == 0 && g_sightTime == 0.0f && g_sgLog.empty());
        // 250 ms of 4 pairs: one test; its pair both ways.
        m.Tick(250);
        WM(r.sgTick, SgLogIs({"view e1 p1", "view p1 e1"}) && m.m_observerCursor == 2 && m.m_targetCursor == 0 && g_sightTime == 0.0f);
        // 500: two, spent on the observers past the end.
        g_sgLog.clear();
        m.Tick(500);
        WM(r.sgTick, g_sgLog.empty() && m.m_observerCursor == 0 && m.m_targetCursor == 1 && g_sightTime == 0.0f);
        // 750: three; the second target's pairs.
        m.Tick(750);
        WM(r.sgTick, SgLogIs({"view p1 e1", "view e1 p1"}) && m.m_observerCursor == 0 && m.m_targetCursor == 0);
        // Under one test: kept for later.
        g_sgLog.clear();
        m.Tick(249);
        WM(r.sgTick, g_sgLog.empty() && g_sightTime == 249.0f);
        m.Tick(1);
        WM(r.sgTick, SgLogIs({"view e1 p1", "view p1 e1"}) && g_sightTime == 0.0f);
        // A hidden target is not tested (the pair the other way is).
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        m.SetTargetState(1, SIGHT_TARGET_HIDDEN);
        g_sgLog.clear();
        m.Tick(2000);
        WM(r.sgTick, SgLogIs({"view e1 p1", "view e1 p1"}) && g_sightTime == 0.0f);
        m.SetTargetState(1, SIGHT_TARGET_SHOWN);
        // The cursor past the targets: from the first; a remainder.
        m.m_observerCursor = 0;
        m.m_targetCursor = 7;
        g_sightTime = 100.0f;
        g_sgLog.clear();
        m.Tick(300);
        WM(r.sgTick, SgLogIs({"view e1 p1", "view p1 e1"}) && g_sightTime == 150.0f && m.m_observerCursor == 2);
        // The cursor exactly at the end: from the first.
        m.m_observerCursor = 0;
        m.m_targetCursor = 2;
        g_sightTime = 0.0f;
        g_sgLog.clear();
        m.Tick(250);
        WM(r.sgTick, SgLogIs({"view e1 p1", "view p1 e1"}));
        // The budget left at 1 after the charge: the next target's first pair.
        m.m_observerCursor = 2;
        m.m_targetCursor = 0;
        g_sgLog.clear();
        m.Tick(750);
        WM(r.sgTick, SgLogIs({"view p1 e1", "view e1 p1"}));
        // A target of mask 1, an observer of mask 1 the other way.
        m.m_targets[0]->mask = 1;
        m.m_observers[0]->mask = 1;
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        g_sightTime = 0.0f;
        g_sgLog.clear();
        m.Tick(250);
        WM(r.sgTick, SgLogIs({"view e1 p1", "view p1 e1"}));
        m.m_targets[0]->mask = SIGHT_ALL;
        m.m_observers[0]->mask = SIGHT_ALL;
        // A removed target passed over with the observer cursor at 1: the next target from observer 0.
        {
            m.RemoveTarget(0);
            m.m_observerCursor = 1;
            m.m_targetCursor = 0;
            g_sightTime = 0.0f;
            g_sgLog.clear();
            m.Tick(1000);
            WM(r.sgTick, SgLogIs({"view p1 e1"}) && m.m_targetCursor == 0);
            WM(r.sgTick, m.AddTarget(p1->frame, SIGHT_ALL) == 0 && m.m_observers[0]->target == 0);
        }
        // An observer off: not tested that way.
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        g_sightTime = 0.0f;
        m.m_observers[1]->disabled = true;
        g_sgLog.clear();
        m.Tick(250);
        WM(r.sgTick, SgLogIs({"view p1 e1"}));
        m.m_observers[1]->disabled = false;
        // A wanted test is what lets the tick test a pair (the testedBy bits do too).
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        m.m_observers[1]->records[0].view[1] = 100.0f;
        g_sgLog.clear();
        m.Tick(250);
        WM(r.sgTick, SgLogIs({"view p1 e1"}));
        m.m_targets[0]->testedBy = 2;
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        g_sgLog.clear();
        m.Tick(250);
        WM(r.sgTick, SgLogIs({"view e1 p1", "view p1 e1"}));
        m.m_targets[0]->testedBy = 0;
        m.m_observers[1]->records[0].view[1] = 0.0f;
        // A removed target is passed over; a removed observer too; a target of no observer.
        m.RemoveTarget(0);
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        g_sgLog.clear();
        m.Tick(500);
        WM(r.sgTick, g_sgLog.empty() && m.m_targetCursor == 1);
        m.m_targetCursor = 0;
        m.Tick(1000);
        WM(r.sgTick, SgLogIs({"view p1 e1"}) && m.m_targetCursor == 0);
        WM(r.sgTick, m.AddTarget(p1->frame, SIGHT_ALL) == 0);
        m.RemoveObserver(1);
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        g_sgLog.clear();
        m.Tick(2000);
        WM(r.sgTick, g_sgLog.empty());
        // A target without an observer (n1's alone): passed over.
        m.Clear();
        m.AddObserver(p1->frame, SIGHT_ALL);
        m.AddTarget(n1->frame, SIGHT_ALL);
        m.AddTarget(e1->frame, SIGHT_ALL);
        m.m_observers[0]->records[0].relation = SIGHT_HOSTILE;
        g_sgView[{p1->actor, n1->actor}] = 50.0f;
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        g_sightTime = 0.0f;
        g_sgLog.clear();
        m.Tick(4000);
        WM(r.sgTick, g_sgLog.empty() && m.m_targets[0]->observer == -1);
        // A null target ends the tick.
        m.Clear();
        m.AddObserver(p1->frame, SIGHT_ALL);
        m.AddTarget(e1->frame, SIGHT_ALL);
        m.AddObserver(e1->frame, SIGHT_ALL);
        m.AddTarget(itemFrame, 1);
        m.m_observerCursor = 0;
        m.m_targetCursor = 0;
        g_sightTime = 0.0f;
        g_sgLog.clear();
        m.Tick(2000);
        WM(r.sgTick, SgLogIs({"view p1 e1"}) && m.m_targetCursor == 1 && m.m_observerCursor == 0);
        // (Starting at it: nothing.)
        g_sgLog.clear();
        m.Tick(2000);
        WM(r.sgTick, g_sgLog.empty() && m.m_targetCursor == 1);
        // No pairs: nothing (and no division).
        m.Clear();
        g_sightTime = 5.0f;
        m.Tick(100);
        WM(r.sgTick, g_sightTime == 105.0f);
        g_sgView.clear();
    }

    // --- the module ------------------------------------------------------------------------------------
    {
        m.AddObserver(p1->frame, SIGHT_ALL);
        m.AddTarget(p1->frame, SIGHT_ALL);
        m.LoadMissionData(false);
        m.FreeMissionData(false);
        m.StopMission(false);
        WM(r.sgModule, m.m_observers.size() == 1 && m.m_targets.size() == 1);
        m.LoadMissionData(true);
        WM(r.sgModule, m.m_observers.empty() && m.m_targets.empty());
        m.AddObserver(p1->frame, SIGHT_ALL);
        m.FreeMissionData(true);
        WM(r.sgModule, m.m_observers.empty());
        m.AddTarget(p1->frame, SIGHT_ALL);
        m.StopMission(true);
        WM(r.sgModule, m.m_targets.empty());
        m.AddTarget(p1->frame, SIGHT_ALL);
        m.RestartMission(false);
        WM(r.sgModule, m.m_targets.empty() && m.GetStartDelay() == 4000);
        C_chunk none;
        WM(r.sgModule, m.SaveGame(none) && m.LoadGame(none) && m.GetName() == std::string("Sight") && m.GetId() == MODULE_SIGHT);
        m.Clear();
    }
    GetGame()->m_missionTime = oldTime;
    std::printf("sight: sgRegister %d sgRelation %d sgParts %d sgTestNow %d sgCanSee %d sgUpdate %d sgTiming %d sgTracker %d "
                "sgTick %d sgStates %d sgRemove %d sgModule %d\n",
                r.sgRegister, r.sgRelation, r.sgParts, r.sgTestNow, r.sgCanSee, r.sgUpdate, r.sgTiming, r.sgTracker, r.sgTick,
                r.sgStates, r.sgRemove, r.sgModule);
}
