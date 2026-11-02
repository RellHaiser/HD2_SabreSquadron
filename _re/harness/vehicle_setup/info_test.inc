// Part 9c-iv-c's controls (game/controls.cpp: the skill and the item information), in the text build,
// run by ControlsTest after ComboTest (its fonts, its screen, its cursor, its picture meshes; the
// item definitions by g_gfItemDefs; ScArrow, ScMessages, C_ScTarget, CkId, CbRuns). Prints "info:"
// with 1 per check met.

// The item definitions' pictures (item_defs.cpp is not in the text build): the definition's own.
IImage *C_ItemDef::GetImage() { return m_image; }
void C_ScopeComp::Save(int *, void *) {}
void C_ScopeComp::Load(int, const void *) {}

namespace {

struct S_InfoResults {
    bool skBase = true, skKeys = true, skPool = true, skInput = true, skMsgs = true, ifBase = true;
    bool ifItem = true, ifStats = true, ifCounts = true, ifMsgs = true;
};

// A definition: its kind, behaviour, ammo / headgear, rounds, text, weight, components, picture.
struct C_IfDef : C_ItemDef {
    int behaviour, ammo, headgear;
    float amount;
    C_IfDef(E_ItemRecordType type, int id, int b, int a = -1, int h = -1, float n = -1.0f)
        : C_ItemDef(type, id), behaviour(b), ammo(a), headgear(h), amount(n)
    {
        m_textId = 100 + id;
        m_weight = 1.25f;
    }
    int GetCarryMode() override { return 0; }
    int GetBehaviour() override { return behaviour; }
    float GetAmmoAmount() override { return amount; }
    int GetAmmoItemId() override { return ammo; }
    int GetHeadgearItemId() override { return headgear; }
    void Set(float weight, C_ItemComponent *component, C_ItemComponent *secondary, void *image)
    {
        m_weight = weight;
        m_component = component;
        m_secondaryComponent = secondary;
        m_image = static_cast<IImage *>(image);
    }
};
struct C_IfWeapon : C_WeaponComp {
    float speed;
    C_IfWeapon(E_ItemComponentKind kind, float s) : C_WeaponComp(kind), speed(s) {}
    void Save(int *, void *) override {}
    void Load(int, const void *) override {}
    float GetMuzzleVelocity() override { return speed; }
};
C_ScopeComp *IfScope(int mode, float fov)
{
    auto *scope = new C_ScopeComp();
    WmField<int>(scope, 0x0C) = mode;
    WmField<float>(scope, 0x10) = fov;
    return scope;
}
// A picture's visual: its mesh's face groups textured "other.bmp" and "X_SQUARE.BMP".
struct S_IfMesh {
    S_CsTexture other, square;
    S_CsMaterial otherMat, squareMat;
    S_CsGroup otherGroup, squareGroup;
    S_CsLevel level;
    S_CsMesh mesh;
};
I3D_frame *IfPicture(const char *name, I3D_frame *parent, S_IfMesh &m)
{
    m.other.vtable = m.square.vtable = g_csTextureVt;
    m.other.name = "other.bmp";
    m.square.name = "X_SQUARE.BMP";
    m.otherMat.vtable = m.squareMat.vtable = g_csMaterialVt;
    m.otherMat.name = "mo";
    m.squareMat.name = "ms";
    m.otherMat.texture = &m.other;
    m.squareMat.texture = &m.square;
    m.otherGroup.material = &m.otherMat;
    m.squareGroup.material = &m.squareMat;
    m.otherGroup.next = &m.squareGroup;
    m.level.groups = &m.otherGroup;
    m.mesh.vtable = g_csMeshVt;
    m.mesh.level = &m.level;
    I3D_frame *frame = CsVisual(name, parent);
    WmField<void *>(frame, 0x1E0) = &m.mesh;
    return frame;
}

} // namespace

void InfoTest(I3D_scene *scene)
{
    S_InfoResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- the skill: its name and value titles (id | 1; the value's margin 1), the minus (id | 2)
    // and plus (id | 3) buttons routed to it; both: the titles take input ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "skframe");
        CsVisual("skill_textsk", frame);
        CsVisual("skill_valuesk", frame);
        ScArrow("barrow upsk", "ku", frame);
        ScArrow("barrow downsk", "kd", frame);
        C_Control root(scene, 0x08000000, nullptr);
        auto *s = new C_SkillControl(scene, 0x08100000, &root);
        auto *pool = new C_SkillControl(scene, 0x08200000, &root);
        root.m_children = {s, pool};
        WM(r.skBase, s->m_points == 0 && s->m_base == 0 && s->m_focus == -1);
        WM(r.skBase, s->Create(frame, frame, nullptr) && s->m_children.size() == 4 && s->m_children[0] == s->m_nameTitle &&
                         s->m_children[1] == s->m_valueTitle && s->m_children[2] == s->m_minusButton &&
                         s->m_children[3] == s->m_plusButton && s->m_takesInput);
        WM(r.skBase, s->m_nameTitle->m_id == 0x08100001 && s->m_valueTitle->m_id == 0x08100001 &&
                         s->m_minusButton->m_id == 0x08100002 && s->m_plusButton->m_id == 0x08100003 &&
                         s->m_nameTitle->m_takesInput && s->m_valueTitle->m_takesInput && s->m_valueTitle->m_box->m_margin == 1 &&
                         s->m_minusButton->m_routes.size() == 1 && s->m_minusButton->m_routes[0].target == 0x08100000 &&
                         s->m_minusButton->m_routes[0].id == 0x5006071 && s->m_plusButton->m_routes[0].id == 0x5006070);
        WM(r.skBase, s->FindChild(0x08100001) == 0 && s->FindChild(2) == 2 && s->FindChild(3) == 3 && s->FindChild(4) == -1 &&
                         s->m_frame == frame);
        // A notification's route kept (the base's).
        S_ControlMessage route = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SKILL_UP, 0, 0, nullptr, 0x08100000);
        handled &= s->HandleMessage(&route);
        WM(r.skBase, s->m_routes.size() == 1);
        // The name kept, shown with the base: the value, the minus inactive (no points), the plus not.
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "Shooting");
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, std::string(s->m_name.c_str()) == "Shooting" && CbRuns(s->m_nameTitle).empty());
        m = CsMessage(CONTROL_MSG_SET_BASE, 40);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, s->m_base == 40 && s->m_points == 0 && CbRuns(s->m_nameTitle) == "Shooting/11 " &&
                         CbRuns(s->m_valueTitle) == "40/22 " && s->m_minusButton->m_inactive && !s->m_plusButton->m_inactive);
        // Points up and down (no pool), the value got.
        m = CsMessage(CONTROL_MSG_SKILL_UP);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, m.value == 1 && s->m_points == 1 && CbRuns(s->m_valueTitle) == "41/22 " && !s->m_minusButton->m_inactive);
        m = CsMessage(CONTROL_MSG_GET_NUMBER, 9);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, m.value == 41);
        m = CsMessage(CONTROL_MSG_SKILL_DOWN);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, m.value == 1 && s->m_points == 0 && s->m_minusButton->m_inactive);
        m = CsMessage(CONTROL_MSG_SKILL_DOWN, 7);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, m.value == 0 && s->m_points == 0);
        // The total: the points the rest (below the base: 0); 100 at most.
        m = CsMessage(CONTROL_MSG_SET_TOTAL, 50);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, s->m_points == 10 && CbRuns(s->m_valueTitle) == "50/22 ");
        m = CsMessage(CONTROL_MSG_SET_TOTAL, 30);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, s->m_points == 0 && CbRuns(s->m_valueTitle) == "40/22 ");
        m = CsMessage(CONTROL_MSG_SET_TOTAL, 99);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, s->m_points == 59 && !s->m_plusButton->m_inactive);
        m = CsMessage(CONTROL_MSG_SKILL_UP);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, m.value == 1 && s->m_points == 60 && s->m_plusButton->m_inactive);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, m.value == 0 && s->m_points == 60 && CbRuns(s->m_valueTitle) == "100/22 ");
        m = CsMessage(CONTROL_MSG_SET_TOTAL, 101);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, s->m_points == 61 && s->m_plusButton->m_inactive);
        // The keys: minus (and the keypad's) a point down, equals (and the keypad's plus) up.
        m = CsMessage(CONTROL_MSG_SET_TOTAL, 45);
        handled &= s->HandleMessage(&m);
        WM(r.skKeys, s->Unknown20(0x0D) && s->m_points == 6 && s->Unknown20(0x4E) && s->m_points == 7);
        WM(r.skKeys, s->Unknown20(0x0C) && s->m_points == 6 && s->Unknown20(0x4A) && s->m_points == 5);
        WM(r.skKeys, s->Unknown20(0x10C) && s->m_points == 4 && !s->Unknown20(0x0E) && !s->Unknown20(0) && s->m_points == 4);
        m = CsMessage(CONTROL_MSG_SET_TOTAL, 40);
        handled &= s->HandleMessage(&m);
        WM(r.skKeys, s->Unknown20(0x0C) && s->m_points == 0);

        // A pool (the control the value names): a point taken from it to go up, given back to go
        // down; none left (or full): refused.
        pool->Create(GfFrame(FRAME_DUMMY, "skpool"), nullptr, nullptr);
        m = CsMessage(CONTROL_MSG_SET_TOTAL, 2);
        handled &= pool->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0x08200000);
        handled &= s->HandleMessage(&m);
        WM(r.skPool, s->m_value == 0x08200000 && pool->m_points == 2);
        m = CsMessage(CONTROL_MSG_SKILL_UP);
        handled &= s->HandleMessage(&m);
        WM(r.skPool, m.value == 1 && s->m_points == 1 && pool->m_points == 1);
        handled &= s->HandleMessage(&m);
        WM(r.skPool, m.value == 1 && s->m_points == 2 && pool->m_points == 0);
        handled &= s->HandleMessage(&m);
        WM(r.skPool, m.value == 0 && s->m_points == 2 && pool->m_points == 0);
        m = CsMessage(CONTROL_MSG_SKILL_DOWN);
        handled &= s->HandleMessage(&m);
        WM(r.skPool, m.value == 1 && s->m_points == 1 && pool->m_points == 1);
        m = CsMessage(CONTROL_MSG_SET_BASE, 100);
        handled &= pool->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SKILL_DOWN);
        handled &= s->HandleMessage(&m);
        WM(r.skPool, m.value == 0 && s->m_points == 1 && pool->m_points == 0);
        // (The value naming no control: refused, the fix.)
        s->m_value = 0x08300000;
        m = CsMessage(CONTROL_MSG_SKILL_UP);
        handled &= s->HandleMessage(&m);
        WM(r.skPool, m.value == 0 && s->m_points == 1);
        s->m_value = 0;
        // The buttons' presses routed to it (posted).
        g_controlMessages.clear();
        s->m_minusButton->m_inactive = false;
        s->m_minusButton->OnPress(1);
        s->m_plusButton->OnPress(1);
        WM(r.skInput, ScMessages() == "8100000 1000071 0 0|8100000 1000070 0 0|");
        s->m_minusButton->m_held = s->m_plusButton->m_held = false;

        // The input: the name title's enter and leave first; the focus by the id.
        s->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        s->m_nameTitle->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.skInput, s->OnEnter(0x08100003) == CONTROL_HANDLED && s->m_focus == 3 && !g_gfLog.empty() &&
                          g_gfLog[0] == "notify 4003000 0");
        WM(r.skInput, s->OnInput05(0x08100003) == CONTROL_NOT_HANDLED && s->m_focus == 3);
        WM(r.skInput, s->OnPress(0x08100003) == CONTROL_HANDLED && s->m_plusButton->m_held);
        s->m_plusButton->m_held = false;
        WM(r.skInput, s->OnInput08(1) == CONTROL_NOT_HANDLED);
        s->m_inactive = true;
        WM(r.skInput, s->OnPress(1) == CONTROL_HANDLED && s->OnInput08(1) == CONTROL_HANDLED && !s->m_plusButton->m_held);
        s->m_inactive = false;
        g_gfLog.clear();
        WM(r.skInput, s->OnInput05(0x08100002) == CONTROL_HANDLED && s->m_focus == 2 && !g_gfLog.empty() &&
                          g_gfLog[0] == "notify 4004000 0");
        g_gfLog.clear();
        WM(r.skInput, s->OnLeave(1) == CONTROL_HANDLED && s->m_focus == -1 && !g_gfLog.empty() && g_gfLog[0] == "notify 4004000 0");
        WM(r.skInput, s->OnInput05(0x08100004) == CONTROL_HANDLED && s->m_focus == -1);
        WM(r.skInput, s->OnEnter(0x08100004) == CONTROL_HANDLED && s->m_focus == -1 && s->OnPress(1) == 0 && s->OnInput08(1) == 0);
        // A focus refusing its leave: the enter and the leave not handled; the move handled.
        auto *k = new C_CsChild("kk", 0x08100005);
        s->m_children.push_back(k);
        s->m_focus = 4;
        g_csReturn = 0;
        WM(r.skInput, s->OnEnter(0x08100002) == CONTROL_NOT_HANDLED && s->m_focus == 4 && s->OnLeave(1) == 0 && s->m_focus == 4 &&
                          s->OnInput05(0x08100002) == CONTROL_HANDLED && s->m_focus == 4);
        g_csReturn = 5;
        WM(r.skInput, s->OnPress(1) == 5 && s->OnInput08(1) == 5);
        s->m_children.pop_back();
        delete k;
        s->m_focus = -1;
        s->m_anyCallback = S_ControlCallback();
        s->m_nameTitle->m_anyCallback = S_ControlCallback();
        // Others: to both titles; the strings nothing.
        m = CsMessage(CONTROL_MSG_SET_TEXT_ID, 2, 5);
        handled &= s->HandleMessage(&m);
        WM(r.skMsgs, CbRuns(s->m_nameTitle) == "text 5/2 " && CbRuns(s->m_valueTitle) == "text 5/2 ");
        s->SetStrings({C_str("x")});
        WM(r.skMsgs, CbRuns(s->m_nameTitle) == "text 5/2 ");
        // The refresh: the titles only (shown).
        delete s->m_nameTitle->m_box;
        s->m_nameTitle->m_box = new C_CsBox("sn");
        delete s->m_valueTitle->m_box;
        s->m_valueTitle->m_box = new C_CsBox("sv");
        g_gfLog.clear();
        s->Refresh();
        WM(r.skBase, GfLogIs({"box sn render", "box sv render"}));
        s->m_shown = false;
        g_gfLog.clear();
        s->Refresh();
        WM(r.skBase, g_gfLog.empty());
        s->m_shown = true;
        // Inactive: the parts too.
        s->SetInactive(true);
        WM(r.skBase, s->m_inactive && s->m_nameTitle->m_inactive && s->m_valueTitle->m_inactive && s->m_plusButton->m_inactive &&
                         s->m_minusButton->m_inactive);
        s->SetInactive(false);
        WM(r.skBase, !s->m_valueTitle->m_inactive && !s->m_plusButton->m_inactive);
        // Without a button: no input, the titles not taking it.
        I3D_frame *half = GfFrame(FRAME_DUMMY, "skhalf");
        CsVisual("skill_texths", half);
        CsVisual("skill_valuehs", half);
        ScArrow("barrow uphs", "hu", half);
        C_SkillControl h(scene, 0x08400000, nullptr);
        WM(r.skBase, h.Create(half, half, nullptr) && !h.m_takesInput && h.m_minusButton && !h.m_plusButton &&
                         !h.m_nameTitle->m_takesInput);
        I3D_frame *half2 = GfFrame(FRAME_DUMMY, "skhalf2");
        ScArrow("barrow downh2", "h2", half2);
        C_SkillControl h2(scene, 0x08600000, nullptr);
        WM(r.skBase, h2.Create(half2, half2, nullptr) && !h2.m_takesInput && !h2.m_minusButton && h2.m_plusButton);
        // Without parts: made; the messages and the refresh nothing.
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "skbare");
        C_SkillControl b(scene, 0x08500000, nullptr);
        WM(r.skBase, b.Create(bare, bare, nullptr) && b.m_children.empty() && !b.m_takesInput);
        m = CsMessage(CONTROL_MSG_SET_BASE, 3);
        handled &= b.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_TEXT_ID, 2, 5);
        handled &= b.HandleMessage(&m);
        b.Refresh();
        b.SetInactive(true);
        WM(r.skBase, b.m_base == 3 && b.m_inactive);
        root.m_children.clear();
        delete s;
        delete pool;
        for (I3D_frame *f : {frame, half})
            for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
                c->SetData(nullptr);
    }

    // --- the item information: its picture (on its frame, id | 1) and second (on "ammo*", id | 2),
    // hidden, their "X_SQUARE.BMP" face groups; the stats' title ---
    {
        S_IfMesh mesh, ammoMesh;
        I3D_frame *frame = IfPicture("ifframe", nullptr, mesh);
        IfPicture("ammoif", frame, ammoMesh);
        CsVisual("text_infoif", frame);
        C_ScTarget parent("pa", 0x0C000000);
        auto *f = new C_ItemInfo(scene, 0x0C100000, &parent);
        WM(r.ifBase, f->m_item == -1 && f->m_second == -1 && f->m_count == 0 && f->m_secondCount == 0);
        WM(r.ifBase, f->Create(frame, frame, nullptr) && f->m_children.size() == 3 && f->m_children[0] == f->m_picture &&
                         f->m_children[1] == f->m_secondPicture && f->m_children[2] == f->m_stats);
        WM(r.ifBase, f->m_picture->m_id == 0x0C100001 && f->m_secondPicture->m_id == 0x0C100002 && f->m_stats->m_id == 0x0C100000 &&
                         !f->m_picture->m_shown && !f->m_secondPicture->m_shown && f->m_picture->m_frame == frame &&
                         f->m_picture->m_faceGroup == reinterpret_cast<I3D_face_group *>(&mesh.squareGroup) &&
                         f->m_secondPicture->m_faceGroup == reinterpret_cast<I3D_face_group *>(&ammoMesh.squareGroup) &&
                         frame->GetData() == CkId(0x0C100001));
        WM(r.ifBase, f->FindChild(1) == 0 && f->FindChild(0x0C100002) == 1 && f->FindChild(3) == -1 && f->FindChild(0) == -1);

        // The counts: the pictures active by them, shown; the parent told while either is.
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_ITEM_COUNT, 2);
        handled &= f->HandleMessage(&m);
        WM(r.ifCounts, f->m_count == 2 && !f->m_picture->m_inactive && f->m_secondPicture->m_inactive &&
                           CbRuns(f->m_picture) == "2 /18 " && CbRuns(f->m_secondPicture) == "0 /18 " &&
                           std::count(g_gfLog.begin(), g_gfLog.end(), "pa msg 0 2000083 202375168") == 1);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 3);
        handled &= f->HandleMessage(&m);
        WM(r.ifCounts, f->m_secondCount == 3 && !f->m_secondPicture->m_inactive && CbRuns(f->m_secondPicture) == "3 /18 " &&
                           std::count(g_gfLog.begin(), g_gfLog.end(), "pa msg 0 2000083 202375168") == 1);
        m = CsMessage(CONTROL_MSG_SET_ITEM_COUNT, 0);
        handled &= f->HandleMessage(&m);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 0);
        handled &= f->HandleMessage(&m);
        WM(r.ifCounts, f->m_picture->m_inactive && f->m_secondPicture->m_inactive && CbRuns(f->m_picture) == "0 /18 " &&
                           std::count(g_gfLog.begin(), g_gfLog.end(), "pa msg 0 2000083 202375168") == 0);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 1);
        handled &= f->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 0);
        handled &= f->HandleMessage(&m);
        WM(r.ifCounts, std::count(g_gfLog.begin(), g_gfLog.end(), "pa msg 0 2000083 202375168") == 1);

        // The definitions: a scoped rifle and its ammo, a pistol, a SMG with a scope, an LMG, an
        // assault weapon, a launcher, a uniform and its headgear, a medkit.
        C_IfWeapon firearm(ICK_FIREARM, 740.5f), projectile(ICK_PROJECTILE_WEAPON, 120.25f), melee(ICK_MELEE, 9.0f);
        C_ScopeComp *scope = IfScope(2, 0.25f), *binoculars = IfScope(4, 0.25f), *reticle3 = IfScope(3, 0.5f);
        C_IfDef rifle(ITEM_ANIMATED, 300, ITEM_BEHAVIOUR_RIFLE, 301);
        rifle.Set(4.5f, &firearm, scope, &g_csTexA);
        C_IfDef rounds(ITEM_AMMO, 301, 1, -1, -1, 5.75f);
        rounds.Set(0.5f, nullptr, nullptr, &g_csTexB);
        C_IfDef pistol(ITEM_ANIMATED, 302, ITEM_BEHAVIOUR_SIDEARM, 99);
        pistol.Set(1.0f, &projectile, nullptr, &g_csTexC);
        C_IfDef smg(ITEM_ANIMATED, 303, ITEM_BEHAVIOUR_SMG, 301);
        smg.Set(3.0f, &melee, scope, &g_csTexA);
        C_IfDef lmg(ITEM_ANIMATED, 304, ITEM_BEHAVIOUR_LMG, 301);
        lmg.Set(9.0f, &firearm, reticle3, &g_csTexA);
        C_IfDef assault(ITEM_ANIMATED, 305, ITEM_BEHAVIOUR_ASSAULT, -1);
        assault.Set(5.0f, &firearm, nullptr, &g_csTexA);
        C_IfDef launcher(ITEM_ANIMATED, 306, ITEM_BEHAVIOUR_LAUNCHER, 301);
        launcher.Set(7.0f, &projectile, nullptr, &g_csTexA);
        C_IfDef uniform(ITEM_STATIC, 307, ITEM_BEHAVIOUR_UNIFORM, 301, 308);
        uniform.Set(2.0f, nullptr, nullptr, &g_csTexB);
        C_IfDef cap(ITEM_STATIC, 308, ITEM_BEHAVIOUR_HEADGEAR);
        cap.Set(0.25f, nullptr, nullptr, &g_csTexC);
        C_IfDef medkit(ITEM_STATIC, 309, ITEM_BEHAVIOUR_MEDKIT, 301, 308);
        medkit.Set(0.75f, nullptr, nullptr, &g_csTexA);
        C_IfDef scoped(ITEM_ANIMATED, 310, ITEM_BEHAVIOUR_RIFLE, 301);
        scoped.Set(4.0f, &firearm, binoculars, &g_csTexA);
        C_IfDef sidearmed(ITEM_ANIMATED, 311, ITEM_BEHAVIOUR_RIFLE, 301);
        sidearmed.Set(4.0f, &firearm, &firearm, &g_csTexA);
        C_IfDef melee0(ITEM_ANIMATED, 312, ITEM_BEHAVIOUR_MELEE, 301);
        melee0.Set(0.5f, nullptr, nullptr, &g_csTexA);
        for (C_IfDef *d : {&rifle, &rounds, &pistol, &smg, &lmg, &assault, &launcher, &uniform, &cap, &medkit, &scoped, &sidearmed,
                           &melee0})
            g_gfItemDefs[d->GetId()] = d;
        const float zoom2 = static_cast<float>(std::tan(0.6981317202250164) / std::tan(0.25f * 0.5));
        const float zoom3 = static_cast<float>(std::tan(0.6981317202250164) / std::tan(0.5f * 0.5));

        // A rifle: its picture (the index its id), the ammo's; the stats: the name, the weight, the
        // rounds, the muzzle speed, the zoom.
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_ITEM, 300);
        handled &= f->HandleMessage(&m);
        WM(r.ifItem, f->m_item == 300 && f->m_second == 301 && f->m_picture->m_index == 300 && f->m_secondPicture->m_index == 301 &&
                         mesh.squareMat.texture == &g_csTexA && ammoMesh.squareMat.texture == &g_csTexB && f->m_picture->m_shown &&
                         f->m_secondPicture->m_shown);
        WM(r.ifStats, CbRuns(f->m_stats) == CmText("text 400/16 text 2730 : 4.50 kg/17 text 2731 : 5/17 text 2732 : 740.50 m/s/17 "
                                                   "text 2734 : %.2f x/17 ", zoom2));
        // A pistol: no ammo definition (no rounds), a projectile's speed.
        m = CsMessage(CONTROL_MSG_SET_ITEM, 302);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == "text 402/16 text 2730 : 1.00 kg/17 text 2732 : 120.25 m/s/17 " && f->m_second == 99 &&
                          f->m_secondPicture->m_index == 301 && f->m_picture->m_index == 302);
        // A SMG: no speed (a melee component), no zoom (not a rifle).
        m = CsMessage(CONTROL_MSG_SET_ITEM, 303);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == "text 403/16 text 2730 : 3.00 kg/17 text 2731 : 5/17 ");
        m = CsMessage(CONTROL_MSG_SET_ITEM, 304);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == "text 404/16 text 2730 : 9.00 kg/17 text 2731 : 5/17 text 2732 : 740.50 m/s/17 ");
        m = CsMessage(CONTROL_MSG_SET_ITEM, 305);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == "text 405/16 text 2730 : 5.00 kg/17 text 2732 : 740.50 m/s/17 " && f->m_second == -1);
        // A launcher, a medkit, a melee weapon: the name and the weight.
        m = CsMessage(CONTROL_MSG_SET_ITEM, 306);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == "text 406/16 text 2730 : 7.00 kg/17 ");
        m = CsMessage(CONTROL_MSG_SET_ITEM, 312);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == "text 412/16 text 2730 : 0.50 kg/17 ");
        // A scope of the binoculars' mode, another component as the secondary: no zoom; a reticle 3.
        m = CsMessage(CONTROL_MSG_SET_ITEM, 310);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == "text 410/16 text 2730 : 4.00 kg/17 text 2731 : 5/17 text 2732 : 740.50 m/s/17 ");
        m = CsMessage(CONTROL_MSG_SET_ITEM, 311);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == "text 411/16 text 2730 : 4.00 kg/17 text 2731 : 5/17 text 2732 : 740.50 m/s/17 ");
        WmField<int>(scoped.GetSecondaryComponent(), 0x0C) = 3;
        WmField<float>(scoped.GetSecondaryComponent(), 0x10) = 0.5f;
        m = CsMessage(CONTROL_MSG_SET_ITEM, 310);
        handled &= f->HandleMessage(&m);
        WM(r.ifStats, CbRuns(f->m_stats) == CmText("text 410/16 text 2730 : 4.00 kg/17 text 2731 : 5/17 text 2732 : 740.50 m/s/17 "
                                                   "text 2734 : %.2f x/17 ", zoom3));
        // A uniform: its headgear's picture; a medkit (neither): the second kept.
        m = CsMessage(CONTROL_MSG_SET_ITEM, 307);
        handled &= f->HandleMessage(&m);
        WM(r.ifItem, f->m_second == 308 && f->m_secondPicture->m_index == 308 && ammoMesh.squareMat.texture == &g_csTexC &&
                         mesh.squareMat.texture == &g_csTexB && CbRuns(f->m_stats) == "text 407/16 text 2730 : 2.00 kg/17 ");
        m = CsMessage(CONTROL_MSG_SET_ITEM, 309);
        handled &= f->HandleMessage(&m);
        WM(r.ifItem, f->m_item == 309 && f->m_second == 308 && f->m_secondPicture->m_index == 308 && f->m_picture->m_index == 309);
        // An item without a definition: only its id kept.
        m = CsMessage(CONTROL_MSG_SET_ITEM, 450);
        handled &= f->HandleMessage(&m);
        WM(r.ifItem, f->m_item == 450 && f->m_picture->m_index == 309 && CbRuns(f->m_stats) == "text 409/16 text 2730 : 0.75 kg/17 ");
        // (A headgear item none: the second kept.)
        uniform.headgear = 460;
        m = CsMessage(CONTROL_MSG_SET_ITEM, 307);
        handled &= f->HandleMessage(&m);
        WM(r.ifItem, f->m_second == 460 && f->m_secondPicture->m_index == 308);

        // The callbacks to the pictures; REFRESH to the parts; the rest the group's.
        m = CsMessage(CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK);
        m.value2 = reinterpret_cast<uintptr_t>(f);
        m.value3 = reinterpret_cast<uintptr_t>(&CsNotified);
        handled &= f->HandleMessage(&m);
        WM(r.ifMsgs, f->m_picture->m_callbacks.size() == 1 && f->m_secondPicture->m_callbacks.size() == 1 && f->m_callbacks.empty());
        m = CsMessage(CONTROL_MSG_REMOVE_CALLBACK, 0);
        m.value2 = reinterpret_cast<uintptr_t>(f);
        handled &= f->HandleMessage(&m);
        WM(r.ifMsgs, f->m_picture->m_callbacks.empty() && f->m_secondPicture->m_callbacks.empty());
        f->m_picture->m_box->m_placed = false;
        f->m_stats->m_box->m_placed = false;
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= f->HandleMessage(&m);
        WM(r.ifMsgs, f->m_picture->m_box->m_placed && f->m_stats->m_box->m_placed && f->m_secondPicture->m_box->m_placed);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 6, 0, nullptr, 0x0C100002);
        handled &= f->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 5);
        handled &= f->HandleMessage(&m);
        WM(r.ifMsgs, f->m_secondPicture->m_index == 6 && f->m_index == 5);
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SET_ITEM, 0, 0, nullptr, 0x0C100000);
        handled &= f->HandleMessage(&m);
        WM(r.ifMsgs, f->m_routes.size() == 1);
        // The refresh: the parts (shown).
        for (C_TextControl *part : std::initializer_list<C_TextControl *>{f->m_picture, f->m_secondPicture, f->m_stats}) {
            delete part->m_box;
            part->m_box = new C_CsBox(part == f->m_stats ? "fs" : part == f->m_picture ? "fp" : "f2");
        }
        g_gfLog.clear();
        f->Refresh();
        WM(r.ifBase, GfLogIs({"box fp render", "box f2 render", "box fs render"}));
        f->m_shown = false;
        g_gfLog.clear();
        f->Refresh();
        WM(r.ifBase, g_gfLog.empty());
        delete f;
        for (int id = 300; id <= 312; ++id)
            g_gfItemDefs.erase(id);
        delete scope;
        delete binoculars;
        delete reticle3;
        frame->SetData(nullptr);
    }

    WM(r.skMsgs, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("info: skBase %d skKeys %d skPool %d skInput %d skMsgs %d ifBase %d ifItem %d ifStats %d ifCounts %d ifMsgs %d\n",
                r.skBase, r.skKeys, r.skPool, r.skInput, r.skMsgs, r.ifBase, r.ifItem, r.ifStats, r.ifCounts, r.ifMsgs);
}
