// The controls' random trees, messages and tests (parts 49 on), included by each part's test after controls_kit.inc and
// its kind helpers. The part sets, before its tests run: g_kinds / g_kindCount (its kinds), g_codes (its message codes
// that raise no notification), g_loudCodes (those that do: kept to the cycle-free routes), g_tickKinds (the kinds for
// the ticks' case) and the hooks: g_messageHook (a message's values for its codes), g_afterTree (a tree's fix-ups: the
// children some kinds must have), g_beforeCreate (a node's frame made ready for its Create).
std::vector<uint32_t> g_codes, g_loudCodes;
std::vector<uint32_t> g_noRouteCodes;   // codes routes never carry (a part's: states the original does not survive)
std::vector<int> g_tickKinds;
void (*g_messageHook)(S_ControlMessage &) = nullptr;
void (*g_afterTree)() = nullptr;
void (*g_beforeCreate)(size_t node, int frame) = nullptr;
bool (*g_messageFits)(size_t node, const S_ControlMessage &m) = nullptr;   // (false: another message drawn)
void (*g_beforeMessage)(size_t node, const S_ControlMessage &m) = nullptr;   // (the node's state for the message)
// A part's tree plan (null: RandomTree's draws): each node's parent (an earlier node's index, -1 none), kind, its id's
// last 4 bits OR'd to its parent's (-1: RandomTree's id) and whether it is among its parent's children.
struct S_PlanNode {
    int parent;
    int kind;
    int bits;
    bool child;
};
std::vector<S_PlanNode> (*g_treePlan)() = nullptr;
bool IsLoud(uint32_t code) { return std::find(g_loudCodes.begin(), g_loudCodes.end(), code) != g_loudCodes.end(); }

// ---- random trees (part 48's rules: self routes that raise notifications only on these, notifying only those) ----
const uint32_t NOTIFICATIONS[] = {CONTROL_NOTIFY_CLICK, CONTROL_NOTIFY_02, CONTROL_NOTIFY_ENTER, CONTROL_NOTIFY_LEAVE, CONTROL_NOTIFY_05,
                                  CONTROL_NOTIFY_PRESS, CONTROL_NOTIFY_RELEASE, CONTROL_NOTIFY_08, CONTROL_NOTIFY_09, CONTROL_NOTIFY_0A,
                                  CONTROL_NOTIFY_0B, CONTROL_NOTIFY_HIDDEN, CONTROL_NOTIFY_SHOWN, CONTROL_NOTIFY_WHEEL,
                                  CONTROL_NOTIFY_KEY_CLICK, CONTROL_NOTIFY_CHANGED, CONTROL_NOTIFY_13};
// The notifications loud self routes are on (a part may change them: none of its loud codes may raise one).
std::vector<uint32_t> NOTIFY_RAISING = {CONTROL_NOTIFY_02, CONTROL_NOTIFY_05, CONTROL_NOTIFY_0B};
const uint32_t NOTIFY_RAISED[] = {CONTROL_NOTIFY_08, CONTROL_NOTIFY_09, CONTROL_NOTIFY_0A, CONTROL_NOTIFY_13, CONTROL_NOTIFY_CHANGED};
uint32_t RandomNotification() { return P(0.05) ? 0 : P(0.03) ? CONTROL_NOTIFY_ANY : Pick(NOTIFICATIONS); }
uintptr_t RandomObject() { return P(0.2) ? 0 : reinterpret_cast<uintptr_t>(g_objects[I(0, 1)]); }
uintptr_t RandomFunction() { return P(0.15) ? 0 : g_portCbs[I(0, 2)]; }
const char *const FRAME_NAMES[] = {"normal*", "page1", "x"};
const char *const TEXTURE_NAMES[] = {"menu.bmp", "face.tga", "", "nope.bmp"};
S_ControlMessage RandomMessage(bool quiet)
{
    S_ControlMessage m;
    m.target = P(0.5) ? 0xFFFFFFFF : P(0.7) && !g_portNodes.empty() ? g_portNodes[size_t(I(0, int(g_portNodes.size()) - 1))]->m_id : U32();
    if (quiet || P(0.7)) {
        m.id = g_codes[size_t(I(0, int(g_codes.size()) - 1))];
    } else {
        m.id = g_loudCodes[size_t(I(0, int(g_loudCodes.size()) - 1))];
    }
    m.value = P(0.5) ? uint32_t(I(-2, 8)) : U32();
    m.value2 = P(0.5) ? uint32_t(I(-2, 8)) : U32();
    m.value3 = U32();
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_NOTIFY:
        m.value = Pick(NOTIFY_RAISED);
        break;
    case CONTROL_MSG_SET_TEXTURE:
        m.value2 = P(0.3) ? 0 : reinterpret_cast<uintptr_t>(g_arena.textures[I(0, 3)]);
        break;
    case CONTROL_MSG_ADD_FRAME:
        m.value2 = reinterpret_cast<uintptr_t>(Pick(FRAME_NAMES));
        break;
    case CONTROL_MSG_LOAD_TEXTURE:
    case CONTROL_MSG_FIND_FACE_GROUP:
        m.text = Pick(TEXTURE_NAMES);
        if (P(0.2))
            m.text = C_str();
        break;
    }
    if (g_messageHook)
        g_messageHook(m);
    if (P(0.3) && !m.text.m_rep)
        m.text = P(0.5) ? "hello" : "";
    return m;
}

void ClearPortTree()
{
    // (Each node deleted once: its children and owned control taken away first.)
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        g_portNodes[i]->m_children.clear();
        if (g_nodeKinds[i]->ownedSlot)
            *g_nodeKinds[i]->ownedSlot(g_portNodes[i]) = nullptr;
        if (g_nodeKinds[i]->ownedSlot2)
            *g_nodeKinds[i]->ownedSlot2(g_portNodes[i]) = nullptr;
        if (g_nodeKinds[i]->heldVec)
            g_nodeKinds[i]->heldVec(g_portNodes[i])->clear();
    }
    for (C_Control *c : g_portNodes)
        delete c;
    g_portNodes.clear();
    g_nodeKinds.clear();
}

// `kinds`: the kinds the tree draws from (empty: all).
void RandomTree(const std::vector<int> &kinds = {})
{
    ClearPortTree();
    RandomArena();
    g_cursorMade = P(0.9);
    std::vector<S_PlanNode> plan;
    if (g_treePlan && kinds.empty())
        plan = g_treePlan();
    const int count = !plan.empty() ? int(plan.size()) : P(0.1) ? 1 : I(2, 8);
    std::vector<int> depth;
    const uint32_t top = P(0.8) ? (uint32_t(I(1, 63)) << 26) : 0;
    for (int i = 0; i < count; ++i) {
        // (Parents: groups or plain controls mostly.)
        C_Control *parent = !plan.empty() ? (plan[size_t(i)].parent < 0 ? nullptr : g_portNodes[size_t(plan[size_t(i)].parent)])
                            : i == 0 ? nullptr : g_portNodes[size_t(I(0, i - 1))];
        int d = 0;
        uint32_t id = top;
        if (parent) {
            const size_t pi = size_t(std::find(g_portNodes.begin(), g_portNodes.end(), parent) - g_portNodes.begin());
            d = depth[pi] + 1;
            const uint32_t index = uint32_t(parent->m_children.size() + 1);
            const int shift = d == 1 ? 20 : d == 2 ? 12 : 4;
            id = d <= 3 ? parent->m_id | (index << shift) : parent->m_id;
        }
        if (!plan.empty() && plan[size_t(i)].bits >= 0)
            id = (parent ? parent->m_id : top) | uint32_t(plan[size_t(i)].bits);
        else if (P(0.05))
            id = P(0.5) ? 0xFFFFFFFF : U32();
        const int k = !plan.empty() ? plan[size_t(i)].kind : kinds.empty() ? I(0, g_kindCount - 1) : kinds[size_t(I(0, int(kinds.size()) - 1))];
        C_Control *c = g_kinds[k].make(reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000 + I(0, 3))), id, parent);
        if (parent && (!plan.empty() ? plan[size_t(i)].child : P(0.95)))
            parent->m_children.push_back(c);
        g_portNodes.push_back(c);
        g_nodeKinds.push_back(&g_kinds[k]);
        depth.push_back(d);
        c->m_frame = RandomFrame();
        c->m_type = P(0.5) ? 0 : I(-1, 0x30);
        c->m_value = P(0.5) ? 0 : U32();
        c->m_index = P(0.5) ? -1 : I(-3, 30);
        c->m_inactive = P(0.3);
        c->m_shown = P(0.7);
        c->m_takesInput = P(0.7);
        for (int r = I(0, 3); r > 0; --r) {
            S_ControlMessage m = RandomMessage(false);
            while (std::find(g_noRouteCodes.begin(), g_noRouteCodes.end(), m.id & CONTROL_MSG_MASK) != g_noRouteCodes.end())
                m = RandomMessage(false);
            const uint32_t code = m.id & CONTROL_MSG_MASK;
            const bool loud = IsLoud(code);
            const uint32_t on = loud ? NOTIFY_RAISING[size_t(I(0, int(NOTIFY_RAISING.size()) - 1))] : RandomNotification();
            m.id = (m.id & ~CONTROL_NOTIFY_MASK) | (on & CONTROL_NOTIFY_MASK);
            // (Self routes only with the base's codes and the text's: a self route's message of another kind would need
            // its own state, a texture's message its face group.)
            c->m_routes.push_back(m);
        }
        for (int r = I(0, 2); r > 0; --r) {
            S_ControlCallback cb;
            cb.notification = RandomNotification();
            cb.object = reinterpret_cast<void *>(RandomObject());
            cb.function = reinterpret_cast<ControlCallback>(RandomFunction());
            c->m_callbacks.push_back(cb);
        }
    }
        // (The catch-all callback now and then: every notification shows.)
    for (C_Control *c : g_portNodes)
        if (P(0.3)) {
            c->m_anyCallback.notification = CONTROL_NOTIFY_ANY;
            c->m_anyCallback.object = reinterpret_cast<void *>(RandomObject());
            c->m_anyCallback.function = reinterpret_cast<ControlCallback>(g_portCbs[I(0, 2)]);
        }
    // The kinds' own fields (the group's focus after its children).
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        g_nodeKinds[i]->randomize(g_portNodes[i]);
    g_cbResults.clear();
    for (int k = 0; k < 7; ++k)
        g_cbResults.push_back(P(0.7));
    if (g_afterTree)
        g_afterTree();
}

size_t RandomNode() { return size_t(I(0, int(g_portNodes.size()) - 1)); }
uint32_t RandomId()
{
    if (P(0.7))
        return g_portNodes[RandomNode()]->m_id ^ (P(0.2) ? (1u << I(0, 31)) : 0);
    return P(0.3) ? 0xF : U32();
}

// The original's slot of a node (its logged vtable: Refresh and HandleMessageCopy logged).
template <class Fn> Fn Slot(size_t i, uint32_t slot) { return reinterpret_cast<Fn>(static_cast<uintptr_t>(OF<uint32_t *>(g_origNodes[i], 0)[slot / 4])); }
using VoidFn = void(__attribute__((thiscall)) *)(void *);
using VoidIntFn = void(__attribute__((thiscall)) *)(void *, int);
using IntFn = int(__attribute__((thiscall)) *)(void *, int);
using Int2Fn = int(__attribute__((thiscall)) *)(void *, int, int);
using BoolFn = bool(__attribute__((thiscall)) *)(void *);
using BoolIntFn = bool(__attribute__((thiscall)) *)(void *, int);
using MsgFn = bool(__attribute__((thiscall)) *)(void *, void *);
using CreateFn = bool(__attribute__((thiscall)) *)(void *, void *, void *, void *);

// A text control's box taken away now and then (Refresh and SetInactive check for one).
void DropBox(size_t i)
{
    if (g_nodeKinds[i]->boxAt && !g_nodeKinds[i]->boxRequired && P(0.2)) {
        if (g_nodeKinds[i]->portBox) {   // (a paged text control's)
            auto *t = static_cast<C_PagedTextControl *>(g_portNodes[i]);
            delete t->m_box;
            t->m_box = nullptr;
        } else {
            auto *t = static_cast<C_TextControl *>(g_portNodes[i]);
            delete t->m_box;
            t->m_box = nullptr;
        }
    }
}

// The input slots, the ticks and the others without a message.
void TestSlots()
{
    const size_t i = RandomNode();
    C_Control *p = g_portNodes[i];
    const int id = int(RandomId()), amount = I(-3, 3), time = I(0, 100);
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter},   {0x34, &C_Control::OnLeave},   {0x38, &C_Control::OnClick},   {0x3C, &C_Control::OnInput02},
        {0x40, &C_Control::OnInput05}, {0x44, &C_Control::OnRelease}, {0x48, &C_Control::OnPress},   {0x50, &C_Control::OnInput08},
        {0x54, &C_Control::OnInput09}, {0x58, &C_Control::OnInput0A}, {0x5C, &C_Control::OnKeyClick}, {0x60, &C_Control::OnInput13},
        {0x64, &C_Control::OnInput0B},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    const int pick = I(0, n + 9);
    if (const char *t = std::getenv("HD2_CONTROLS_TRACE"))
        if (std::atoi(t) == g_round)
            std::printf("  slot pick %d on c%u (%s) of %u nodes\n", pick, unsigned(i), g_nodeKinds[i]->name, unsigned(g_portNodes.size()));
    int rp = 0, ro = 0;
    if (pick < n) {
        Diff([&] { rp = (p->*slots[pick].port)(id); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], id); });
    } else {
        switch (pick - n) {
        case 0:
            Diff([&] { rp = p->OnWheel(id, amount); }, [&] { ro = Slot<Int2Fn>(i, 0x4C)(g_origNodes[i], id, amount); });
            break;
        case 1:
            Diff([&] { p->Tick(time); }, [&] { Slot<VoidIntFn>(i, 0x1C)(g_origNodes[i], time); });
            break;
        case 2:
            Diff([&] { rp = p->Unknown20(id); }, [&] { ro = Slot<BoolIntFn>(i, 0x20)(g_origNodes[i], id); });
            break;
        case 3:
            DropBox(i);
            Diff([&] { p->Refresh(); }, [&] { Slot<VoidFn>(i, 0x18)(g_origNodes[i]); });
            break;
        case 4: {
            DropBox(i);
            const bool on = P(0.5);
            Diff([&] { p->SetInactive(on); }, [&] { Slot<VoidIntFn>(i, 0x7C)(g_origNodes[i], on); });
            break;
        }
        case 5:
            Diff([&] { p->Show(); }, [&] { Slot<VoidFn>(i, 0x10)(g_origNodes[i]); });
            break;
        case 6:
            Diff([&] { p->Hide(); }, [&] { Slot<VoidFn>(i, 0x14)(g_origNodes[i]); });
            break;
        case 7: {
            const int index = I(-2, 6);
            Diff([&] { rp = p->IsChild(index); }, [&] { ro = Slot<BoolIntFn>(i, 0x04)(g_origNodes[i], index); });
            break;
        }
        case 8:
            Diff([&] { rp = p->TakesInput(); }, [&] { ro = Slot<BoolFn>(i, 0x08)(g_origNodes[i]); });
            break;
        default:
            Diff([&] { rp = p->FindChild(uint32_t(id)); }, [&] { ro = Slot<IntFn>(i, 0x00)(g_origNodes[i], id); });
            break;
        }
    }
    CHECK(rp == ro, "slot %d (%s): %d / %d", pick, g_nodeKinds[i]->name, ro, rp);
}

S_ControlMessage g_messageBefore;
void TestMessage()
{
    const size_t i = RandomNode();
    C_Control *p = g_portNodes[i];
    S_ControlMessage mp = RandomMessage(P(0.6));
    while (g_messageFits && !g_messageFits(i, mp))
        mp = RandomMessage(P(0.6));
    if (P(0.1))
        mp.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    if ((mp.id & CONTROL_NOTIFY_MASK) == 0 && (mp.id & CONTROL_MSG_MASK) == CONTROL_MSG_NOTIFY)
        mp.value = Pick(NOTIFY_RAISED);
    g_messageBefore = mp;
    if (g_beforeMessage)
        g_beforeMessage(i, mp);
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    const bool copy = P(0.3);
    Diff([&] { rp = copy ? p->HandleMessageCopy(mp) : p->HandleMessage(&mp); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, copy ? 0x74 : 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "HandleMessage (%s) %x: %d / %d", g_nodeKinds[i]->name, g_messageBefore.id, ro, rp);
    if (!copy) {
        const std::string why = MessageDiffers(mp, mo);
        CHECK(why.empty(), "the message after (%s): %s", g_nodeKinds[i]->name, why.c_str());
    }
}

// Create on the node's frame (its children's frames there: the button's three when it has three children).
void TestCreate()
{
    const size_t i = RandomNode();
    C_Control *p = g_portNodes[i];
    I3D_frame *frame = RandomFrame(), *root = RandomFrame();
    const int fi = FrameIndex(frame);
    if (g_beforeCreate)
        g_beforeCreate(i, fi);
    // A description: none, or one without children (the factory's controls are a later part's).
    static S_ControlDesc *desc;
    delete desc;
    desc = new S_ControlDesc(I(0, 0x28), "d", U32());
    static uint8_t origDesc[0x40];
    std::memset(origDesc, 0, sizeof origDesc);
    OF<int>(origDesc, 0x0C) = desc->type;
    const bool none = P(0.2);
    bool rp = false, ro = false;
    Diff([&] { rp = p->Create(frame, root, none ? nullptr : desc); },
         [&] { ro = Slot<CreateFn>(i, 0x2C)(g_origNodes[i], frame, root, none ? nullptr : origDesc); });
    CHECK(rp == ro, "Create (%s): %d / %d", g_nodeKinds[i]->name, ro, rp);
}

// An original's node's kind by its vtable: the real one (made by a call) or the logged copy's.
const S_Kind *OrigKindOf(const uint8_t *n)
{
    const uint32_t vt = OF<uint32_t>(n, 0);
    if (const S_Kind *k = KindByVtable(vt))
        return k;
    for (int k = 0; k < g_kindCount; ++k)
        if (LoggedVt(g_kinds[k].vtable) == vt)
            return &g_kinds[k];
    return nullptr;
}

// The destructor: a subtree deleted on both sides (the original's blocks copies its operator delete may free).
void TestDestructor()
{
    ToOrig();
    const size_t i = RandomNode();
    std::map<uint8_t *, uint8_t *> moved;
    std::vector<uint8_t *> stack{g_origNodes[i]};
    while (!stack.empty()) {
        uint8_t *n = stack.back();
        stack.pop_back();
        // Only the node's own bytes (its kind's size): a fixed 0x100 read past the end of the smaller blocks (0x40
        // spare on the test's nodes, none on those the original's new made) and faulted when one ended a heap region.
        const size_t at0 = size_t(std::find(g_origNodes.begin(), g_origNodes.end(), n) - g_origNodes.begin());
        const S_Kind *kind = at0 < g_origNodes.size() ? g_nodeKinds[at0] : OrigKindOf(n);
        CHECK(kind, "the destructor: node %p's kind unknown (vtable %x)", static_cast<void *>(n), OF<uint32_t>(n, 0));
        const size_t size = kind ? kind->size : 0x6C;
        auto *copy = static_cast<uint8_t *>(std::calloc(1, std::max<size_t>(size, 0x100)));
        std::memcpy(copy, n, size);
        moved[n] = copy;
        const size_t kids = OVecBytes(n, O_CHILDREN) / 4;
        for (size_t k = 0; k < kids; ++k)
            stack.push_back(reinterpret_cast<uint8_t **>(OVecData(n, O_CHILDREN))[k]);
        const size_t at = size_t(std::find(g_origNodes.begin(), g_origNodes.end(), n) - g_origNodes.begin());
        for (int which = 0; which < 2; ++which)
            if (at < g_origNodes.size() && OrigOwned(at, n, which))
                stack.push_back(OrigOwned(at, n, which));
        if (at < g_origNodes.size() && g_nodeKinds[at]->heldAt)
            for (size_t k = 0; k < OVecBytes(n, g_nodeKinds[at]->heldAt) / 4; ++k)
                stack.push_back(reinterpret_cast<uint8_t **>(OVecData(n, g_nodeKinds[at]->heldAt))[k]);
    }
    for (auto &m : moved) {
        uint8_t **kids = reinterpret_cast<uint8_t **>(OVecData(m.second, O_CHILDREN));
        const size_t n = OVecBytes(m.second, O_CHILDREN) / 4;
        for (size_t k = 0; k < n; ++k)
            if (moved.count(kids[k]))
                kids[k] = moved[kids[k]];
        const size_t at = size_t(std::find(g_origNodes.begin(), g_origNodes.end(), m.first) - g_origNodes.begin());
        if (at < g_origNodes.size() && g_nodeKinds[at]->heldAt) {
            uint8_t **held = reinterpret_cast<uint8_t **>(OVecData(m.second, g_nodeKinds[at]->heldAt));
            for (size_t k = 0; k < OVecBytes(m.second, g_nodeKinds[at]->heldAt) / 4; ++k)
                if (moved.count(held[k]))
                    held[k] = moved[held[k]];
        }
        for (int which = 0; which < 2 && at < g_origNodes.size(); ++which) {
            const uint32_t owned = which ? g_nodeKinds[at]->ownedAt2 : g_nodeKinds[at]->ownedAt;
            if (owned && moved.count(OF<uint8_t *>(m.second, owned)))
                OF<uint8_t *>(m.second, owned) = moved[OF<uint8_t *>(m.second, owned)];
        }
    }
    // (The copies named as their originals in the logs.)
    std::vector<uint8_t *> saved = g_origNodes;
    for (auto &m : moved)
        for (uint8_t *&o : g_origNodes)
            if (o == m.first)
                o = m.second;
    g_log.clear();
    Slot<IntFn>(i, 0x80)(g_origNodes[i], 1);
    const std::vector<std::string> origLog = g_log;
    g_origNodes = saved;
    // The port's.
    std::vector<C_Control *> gone, todo{g_portNodes[i]};
    while (!todo.empty()) {
        C_Control *n = todo.back();
        todo.pop_back();
        gone.push_back(n);
        for (C_Control *k : n->m_children)
            todo.push_back(k);
        const size_t at = size_t(std::find(g_portNodes.begin(), g_portNodes.end(), n) - g_portNodes.begin());
        for (int which = 0; which < 2; ++which)
            if (at < g_portNodes.size() && PortOwned(at, which))
                todo.push_back(PortOwned(at, which));
        if (at < g_portNodes.size() && g_nodeKinds[at]->heldVec)
            for (C_Control *k : *g_nodeKinds[at]->heldVec(n))
                todo.push_back(k);
    }
    C_Control *root = g_portNodes[i];
    if (root->m_parent) {
        std::vector<C_Control *> &siblings = root->m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), root), siblings.end());
        const size_t pi = size_t(std::find(g_portNodes.begin(), g_portNodes.end(), root->m_parent) - g_portNodes.begin());
        if (pi < g_portNodes.size() && g_nodeKinds[pi]->heldVec) {
            std::vector<C_Control *> &held = *g_nodeKinds[pi]->heldVec(root->m_parent);
            held.erase(std::remove(held.begin(), held.end(), root), held.end());
        }
    }
    g_log.clear();
    delete root;
    CHECK(g_log == origLog, "destructor (%s): %s / %s", g_nodeKinds[i]->name, Joined(origLog).c_str(), Joined(g_log).c_str());
    for (C_Control *n : gone) {
        const size_t at = size_t(std::find(g_portNodes.begin(), g_portNodes.end(), n) - g_portNodes.begin());
        g_portNodes.erase(g_portNodes.begin() + std::ptrdiff_t(at));
        g_nodeKinds.erase(g_nodeKinds.begin() + std::ptrdiff_t(at));
    }
    g_origNodes.clear();
}

// Ticks: trees of the part's ticking kinds.
void TestTick()
{
    RandomTree(g_tickKinds);
    const size_t i = RandomNode();
    const int time = I(0, 100);
    if (P(0.5))
        *reinterpret_cast<int *>(g_arena.cursor + 0x40) = Pick({299, 300, 301, 499, 500, 501, -1, 0});
    Diff([&] { g_portNodes[i]->Tick(time); }, [&] { Slot<VoidIntFn>(i, 0x1C)(g_origNodes[i], time); });
}
