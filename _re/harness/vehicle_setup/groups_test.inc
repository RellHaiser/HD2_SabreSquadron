// Part 9c-v-d-ii's controls (game/controls.cpp: the item pictures, the players' list, the check
// group), in the text build, run by ControlsTest after HotKeyTest (its fonts, its screen, its cursor,
// its picture meshes; IfPicture, C_IfDef, g_gfItemDefs, C_ScTarget, CbScrolledFrames, CbRuns, CsRuns;
// the net game's players: g_grPlayers, g_grPlayerCount). Prints "groups:" with 1 per check met.

// The net game's players (the world tests' fakes are not in the text build): logged.
S_NetPlayerState g_grPlayers[8];
int g_grPlayerCount;
S_NetPlayerState *C_NetGame::LockPlayers()
{
    GfLog("lock");
    return g_grPlayers;
}
void C_NetGame::UnlockPlayers() { GfLog("unlock"); }
int C_NetGame::GetPlayerCount() const { return g_grPlayerCount; }

namespace {

struct S_GroupsResults {
    bool ipMake = true, ipItem = true, ipCounts = true, ipMsgs = true, plMake = true, plInput = true;
    bool plShow = true, plMsgs = true, cgMake = true;
};

// A child logging its input ("<name> <slot> <id>"), returning g_grResult (its leave g_grLeave).
int g_grResult = 1, g_grLeave = 1;
struct C_GrChild : C_Control {
    const char *name;
    C_GrChild(const char *n, uint32_t id) : C_Control(nullptr, id, nullptr), name(n) {}
    int Log(const char *slot, int id, int result)
    {
        GfLog("%s %s %x", name, slot, id);
        return result;
    }
    int OnEnter(int id) override { return Log("enter", id, g_grResult); }
    int OnLeave(int id) override { return Log("leave", id, g_grLeave); }
    int OnInput05(int id) override { return Log("in05", id, g_grResult); }
    int OnPress(int id) override { return Log("press", id, g_grResult); }
    void SetInactive(bool inactive) override { GfLog("%s inactive %d", name, inactive); }
};

// The parent's lines of the log ("|" after each).
std::string GrParent()
{
    std::string s;
    for (const std::string &line : g_gfLog)
        if (line.compare(0, 3, "pa ") == 0)
            s += line + "|";
    return s;
}

} // namespace

void GroupsTest(I3D_scene *scene)
{
    S_GroupsResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- the item pictures: its picture and its second, hidden, their "X_SQUARE.BMP" face groups ---
    {
        S_IfMesh mesh, ammoMesh;
        I3D_frame *frame = IfPicture("ipframe", nullptr, mesh);
        IfPicture("ammoip", frame, ammoMesh);
        C_ScTarget parent("pa", 0x1C000000);
        auto *p = new C_ItemPictures(scene, 0x1C100000, &parent);
        WM(r.ipMake, p->m_item == -1 && p->m_second == -1 && p->m_count == 0 && p->m_secondCount == 0 && p->m_focus == -1);
        WM(r.ipMake, p->Create(frame, frame, nullptr) && p->m_children.size() == 2 && p->m_children[0] == p->m_picture &&
                         p->m_children[1] == p->m_secondPicture && p->m_picture->m_id == 0x1C100001 &&
                         p->m_secondPicture->m_id == 0x1C100002 && !p->m_picture->m_shown && !p->m_secondPicture->m_shown &&
                         p->m_picture->m_faceGroup == reinterpret_cast<I3D_face_group *>(&mesh.squareGroup) &&
                         p->m_secondPicture->m_faceGroup == reinterpret_cast<I3D_face_group *>(&ammoMesh.squareGroup));
        WM(r.ipMake, p->FindChild(1) == 0 && p->FindChild(0x1C100002) == 1 && p->FindChild(3) == -1 && p->FindChild(0) == -1 &&
                         p->FindChild(0x11) == 0);
        // The counts: the parent told (either above 0); the pictures active by them; the second
        // shown only with an item and a count from 0; the texts.
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_ITEM_COUNT, 2);
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, p->m_count == 2 && !p->m_picture->m_inactive && CbRuns(p->m_picture) == "2 /18 " &&
                           CbRuns(p->m_secondPicture) == "0 /18 " && !p->m_secondPicture->m_shown &&
                           GrParent() == "pa msg 0 2000083 470810624|");
        p->m_second = 5;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 0);
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, p->m_secondPicture->m_shown && p->m_secondPicture->m_inactive && CbRuns(p->m_secondPicture) == "0 /18 " &&
                           GrParent() == "pa msg 0 2000083 470810624|");
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 3);
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, !p->m_secondPicture->m_inactive && CbRuns(p->m_secondPicture) == "3 /18 ");
        m = CsMessage(CONTROL_MSG_SET_ITEM_COUNT, 0);
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, p->m_picture->m_inactive && CbRuns(p->m_picture) == "0 /18 ");
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 0);
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, GrParent().empty() && p->m_secondPicture->m_inactive);
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, static_cast<uint32_t>(-1));
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, !p->m_secondPicture->m_shown && CbRuns(p->m_secondPicture) == "-1 /18 ");
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 1);
        handled &= p->HandleMessage(&m);
        p->m_second = -1;
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 2);
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, !p->m_secondPicture->m_shown && CbRuns(p->m_secondPicture) == "2 /18 ");
        // A count below 0: the parent told to hide it; the texts kept.
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_ITEM_COUNT, static_cast<uint32_t>(-1));
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, GrParent() == "pa msg 0 2000083 470810624|pa msg 0 2000084 470810624|" && CbRuns(p->m_picture) == "0 /18 " &&
                           CbRuns(p->m_secondPicture) == "2 /18 " && p->m_picture->m_inactive);
        p->m_secondCount = 0;
        g_gfLog.clear();
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, GrParent() == "pa msg 0 2000084 470810624|");
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_ITEM_COUNT, 1);
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, !p->m_picture->m_inactive && CbRuns(p->m_picture) == "1 /18 " && GrParent() == "pa msg 0 2000083 470810624|");
        // A second count of 1, its item there: active.
        p->m_second = 4;
        m = CsMessage(CONTROL_MSG_SET_ITEM_COUNT, 0);
        handled &= p->HandleMessage(&m);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_SECOND_COUNT, 1);
        handled &= p->HandleMessage(&m);
        WM(r.ipCounts, p->m_secondPicture->m_shown && !p->m_secondPicture->m_inactive && GrParent() == "pa msg 0 2000083 470810624|");
        // The items: a rifle and its ammo; a uniform and its headgear; neither; none.
        C_IfDef rifle(ITEM_ANIMATED, 300, ITEM_BEHAVIOUR_RIFLE, 301);
        rifle.Set(4.5f, nullptr, nullptr, &g_csTexA);
        C_IfDef rounds(ITEM_AMMO, 301, 1);
        rounds.Set(0.5f, nullptr, nullptr, &g_csTexB);
        C_IfDef uniform(ITEM_STATIC, 307, ITEM_BEHAVIOUR_UNIFORM, 301, 308);
        uniform.Set(2.0f, nullptr, nullptr, &g_csTexB);
        C_IfDef cap(ITEM_STATIC, 308, ITEM_BEHAVIOUR_HEADGEAR);
        cap.Set(0.25f, nullptr, nullptr, &g_csTexC);
        C_IfDef medkit(ITEM_STATIC, 309, ITEM_BEHAVIOUR_MEDKIT, 301, 308);
        medkit.Set(0.75f, nullptr, nullptr, &g_csTexA);
        C_IfDef pistol(ITEM_ANIMATED, 302, ITEM_BEHAVIOUR_SIDEARM, 99);
        pistol.Set(1.0f, nullptr, nullptr, &g_csTexC);
        for (C_IfDef *d : {&rifle, &rounds, &uniform, &cap, &medkit, &pistol})
            g_gfItemDefs[d->GetId()] = d;
        m = CsMessage(CONTROL_MSG_SET_ITEM, 300);
        handled &= p->HandleMessage(&m);
        WM(r.ipItem, p->m_item == 300 && p->m_second == 301 && p->m_picture->m_index == 300 && p->m_secondPicture->m_index == 301 &&
                         mesh.squareMat.texture == &g_csTexA && ammoMesh.squareMat.texture == &g_csTexB && p->m_picture->m_shown);
        m = CsMessage(CONTROL_MSG_SET_ITEM, 307);
        handled &= p->HandleMessage(&m);
        WM(r.ipItem, p->m_second == 308 && p->m_secondPicture->m_index == 308 && ammoMesh.squareMat.texture == &g_csTexC &&
                         mesh.squareMat.texture == &g_csTexB);
        m = CsMessage(CONTROL_MSG_SET_ITEM, 309);
        handled &= p->HandleMessage(&m);
        WM(r.ipItem, p->m_item == 309 && p->m_second == 308 && p->m_picture->m_index == 309);
        m = CsMessage(CONTROL_MSG_SET_ITEM, 302);
        handled &= p->HandleMessage(&m);
        WM(r.ipItem, p->m_second == 99 && p->m_secondPicture->m_index == 308 && p->m_picture->m_index == 302);
        m = CsMessage(CONTROL_MSG_SET_ITEM, 450);
        handled &= p->HandleMessage(&m);
        WM(r.ipItem, p->m_item == 450 && p->m_picture->m_index == 302);
        // The callbacks to both; REFRESH to both; to a picture by its id; the rest the group's.
        m = CsMessage(CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK);
        m.value2 = reinterpret_cast<uintptr_t>(p);
        m.value3 = reinterpret_cast<uintptr_t>(&CsNotified);
        handled &= p->HandleMessage(&m);
        WM(r.ipMsgs, p->m_picture->m_callbacks.size() == 1 && p->m_secondPicture->m_callbacks.size() == 1 && p->m_callbacks.empty());
        m = CsMessage(CONTROL_MSG_REMOVE_CALLBACK, 0);
        m.value2 = reinterpret_cast<uintptr_t>(p);
        handled &= p->HandleMessage(&m);
        WM(r.ipMsgs, p->m_picture->m_callbacks.empty() && p->m_secondPicture->m_callbacks.empty());
        p->m_picture->m_box->m_placed = false;
        p->m_secondPicture->m_box->m_placed = false;
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= p->HandleMessage(&m);
        WM(r.ipMsgs, p->m_picture->m_box->m_placed && p->m_secondPicture->m_box->m_placed);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 6, 0, nullptr, 0x1C100002);
        handled &= p->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 5);
        handled &= p->HandleMessage(&m);
        WM(r.ipMsgs, p->m_secondPicture->m_index == 6 && p->m_index == 5);
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SET_ITEM, 0, 0, nullptr, 0x1C100000);
        handled &= p->HandleMessage(&m);
        WM(r.ipMsgs, p->m_routes.size() == 1);
        // Drawn: both, shown.
        for (C_TextControl *part : std::initializer_list<C_TextControl *>{p->m_picture, p->m_secondPicture}) {
            delete part->m_box;
            part->m_box = new C_CsBox(part == p->m_picture ? "pp" : "p2");
        }
        g_gfLog.clear();
        p->Refresh();
        WM(r.ipMsgs, GfLogIs({"box pp render", "box p2 render"}));
        p->m_shown = false;
        g_gfLog.clear();
        p->Refresh();
        WM(r.ipMsgs, g_gfLog.empty());
        delete p;
        for (int id : {300, 301, 302, 307, 308, 309})
            g_gfItemDefs.erase(id);
        // Without "ammo*": one picture.
        S_IfMesh lone;
        C_ItemPictures one(scene, 0x1C200000, &parent);
        WM(r.ipMake, one.Create(IfPicture("iplone", nullptr, lone), nullptr, nullptr) && one.m_children.size() == 1 &&
                         !one.m_secondPicture);
    }

    // --- the players' list: a scrolled text on its frame, a title on "header*" ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "plframe");
        CbScrolledFrames(frame, "pl");
        CsVisual("headerpl", frame);
        frame->SetData(reinterpret_cast<S_FrameData *>(5));
        C_PlayerList l(scene, 0x1C100000, nullptr);
        WM(r.plMake, l.Create(frame, frame, nullptr) && !frame->GetData() && l.m_children.size() == 2 &&
                         l.m_children[0] == l.m_players && l.m_children[1] == l.m_title && l.m_players->m_id == 0x1C100000 &&
                         l.m_title->m_id == 0x1C100002 && !l.m_title->m_takesInput && l.m_players->m_bar->m_pageStep == 5 &&
                         l.m_players->m_parent == &l && !l.m_button && !l.m_list && !l.m_header);
        WM(r.plMake, l.FindChild(4) == 0 && l.FindChild(0x1C100008) == 0 && l.FindChild(0xF) == 0 && l.FindChild(2) == 2 &&
                         l.FindChild(3) == -1 && l.FindChild(1) == -1 && l.FindChild(0) == -1);
        // Shown: the columns' names, the connected players' names and stats 1 and 3.
        const int oldCount = g_grPlayerCount;
        g_grPlayerCount = 3;
        for (S_NetPlayerState &player : g_grPlayers)
            player = S_NetPlayerState();
        std::snprintf(g_grPlayers[0].name, sizeof g_grPlayers[0].name, "Ann");
        g_grPlayers[0].stats[1] = 4;
        g_grPlayers[0].stats[3] = -2;
        g_grPlayers[1].stats[1] = 9;
        std::snprintf(g_grPlayers[2].name, sizeof g_grPlayers[2].name, "Bo");
        g_grPlayers[2].stats[1] = 12;
        g_grPlayers[2].stats[3] = 7;
        std::snprintf(g_grPlayers[3].name, sizeof g_grPlayers[3].name, "Out");
        S_ControlMessage m = CsMessage(CONTROL_MSG_SHOW_PLAYERS);
        g_gfLog.clear();
        WM(r.plShow, l.HandleMessage(&m) && CbRuns(l.m_title) == "text 2611/35 text 2613/36 text 2615/37 " &&
                         CsRuns(*l.m_players->m_list->m_box) == "Ann/35 4/36 -2/37 Bo/35 12/36 7/37 ");
        g_grPlayerCount = oldCount;
        WM(r.plShow, std::count(g_gfLog.begin(), g_gfLog.end(), "lock") == 1 && g_gfLog.back() == "unlock");
        // Ignored: 0x100007E, 0x100007F; to a child by its id; notifications and the rest the control's
        // (not the combo box's).
        for (uint32_t code : {CONTROL_MSG_IGNORED_7E, CONTROL_MSG_IGNORED_7F}) {
            m = CsMessage(code, 1);
            WM(r.plMsgs, l.HandleMessage(&m) && l.m_value == 0);
        }
        m = CsMessage(CONTROL_MSG_SET_INDEX, 3, 0, nullptr, 0x1C100008);
        WM(r.plMsgs, l.HandleMessage(&m) && l.m_index == -1);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "x");
        WM(r.plMsgs, l.HandleMessage(&m) && CbRuns(l.m_title).find("text 2611") == 0);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 7);
        WM(r.plMsgs, l.HandleMessage(&m) && l.m_value == 7);
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SHOW_PLAYERS, 0, 0, nullptr, 0x1C100000);
        WM(r.plMsgs, l.HandleMessage(&m) && l.m_routes.size() == 1);
        // The parts inactive (not itself); its strings ignored.
        l.SetInactive(true);
        WM(r.plMsgs, l.m_players->m_list->m_inactive && l.m_title->m_inactive && !l.m_inactive);
        l.SetInactive(false);
        l.SetStrings({C_str("a")});
        WM(r.plMsgs, CbRuns(l.m_title).find("text 2611") == 0);
        // The input with logging children: the enter, the leave, the hover, the press.
        C_PlayerList g(scene, 0x1C100000, nullptr);
        auto *a = new C_GrChild("ga", 0x1C100008), *b = new C_GrChild("gb", 0x1C100002);
        g.m_children = {a, b, new C_GrChild("gc", 0x1C10000C)};
        g_gfLog.clear();
        WM(r.plInput, g.OnEnter(0x18) == CONTROL_HANDLED && g.m_focus == 0 && GfLogIs({"ga enter 18"}));
        g_grResult = 0;
        g_gfLog.clear();
        WM(r.plInput, g.OnEnter(0x18) == 0 && g.m_focus == 0 && GfLogIs({"ga leave 18", "ga enter 18"}));
        g_grResult = 1;
        g_grLeave = 0;
        g_gfLog.clear();
        WM(r.plInput, g.OnEnter(0x18) == CONTROL_NOT_HANDLED && GfLogIs({"ga leave 18"}));
        WM(r.plInput, g.OnLeave(5) == 0 && g.m_focus == 0);
        g_grLeave = 1;
        g_gfLog.clear();
        WM(r.plInput, g.OnLeave(5) == CONTROL_HANDLED && g.m_focus == -1 && GfLogIs({"ga leave 5"}));
        g_gfLog.clear();
        WM(r.plInput, g.OnLeave(5) == CONTROL_HANDLED && g_gfLog.empty());
        WM(r.plInput, g.OnEnter(1) == 0 && g.m_focus == -1);
        // The title's id: index 2, a child here (the third).
        g_gfLog.clear();
        WM(r.plInput, g.OnEnter(0x12) == CONTROL_HANDLED && g.m_focus == 2 && GfLogIs({"gc enter 12"}));
        // The hover: off the children, the leave; another child: the leave, the enter; the focus' own.
        g_gfLog.clear();
        g_grResult = 7;
        g_grLeave = 0;
        WM(r.plInput, g.OnInput05(0x18) == CONTROL_NOT_HANDLED && g.m_focus == 2 && GfLogIs({"gc leave 18", "gc leave 18"}));
        g_grLeave = 1;
        g_gfLog.clear();
        WM(r.plInput, g.OnInput05(0x18) == CONTROL_HANDLED && g.m_focus == 0 && GfLogIs({"gc leave 18", "ga enter 18"}));
        g_gfLog.clear();
        WM(r.plInput, g.OnInput05(0x18) == 7 && GfLogIs({"ga in05 18"}));
        g_gfLog.clear();
        WM(r.plInput, g.OnInput05(1) == CONTROL_HANDLED && g.m_focus == -1 && GfLogIs({"ga leave 1"}));
        // The press: the focus' when it is the child with the id.
        g.m_focus = 0;
        g_gfLog.clear();
        WM(r.plInput, g.OnPress(0x18) == 7 && GfLogIs({"ga press 18"}) && g.OnPress(0x12) == 0 && g.OnPress(1) == 0);
        g.m_focus = -1;
        WM(r.plInput, g.OnPress(0x18) == 0);
        g_grResult = 1;
        // Inactive: the parts'.
        g.m_players = reinterpret_cast<C_ScrolledText *>(a);
        g.m_title = reinterpret_cast<C_TitleControl *>(b);
        g_gfLog.clear();
        g.SetInactive(true);
        WM(r.plInput, GfLogIs({"ga inactive 1", "gb inactive 1"}) && !g.m_inactive);
        // Without "header*": no title.
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "plbare");
        CbScrolledFrames(bare, "pb");
        C_PlayerList n(scene, 0x1C200000, nullptr);
        WM(r.plMake, n.Create(bare, bare, nullptr) && n.m_children.size() == 1 && !n.m_title);
    }

    // --- the check group: a highlighted check box on "text*" ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "cgframe");
        I3D_frame *text = CsVisual("textcg", frame);
        for (const char *half : {"yes_emptycg", "yes_fullcg", "no_emptycg", "no_fullcg"})
            CsVisual(half, text);
        C_CheckGroup c(scene, 0x1C100000, nullptr);
        WM(r.cgMake, c.FindChild(0x1C100000) == -1 && c.m_focus == -1);
        WM(r.cgMake, c.Create(frame, frame, nullptr) && c.m_box && c.m_children.size() == 1 && c.m_children[0] == c.m_box &&
                         c.m_box->m_id == 0x1C100000 && c.m_box->GetFrame() == text && c.m_box->m_parent == &c);
        WM(r.cgMake, c.FindChild(0) == 0 && c.FindChild(0x77) == 0);
        I3D_frame *none = GfFrame(FRAME_DUMMY, "cgnone");
        C_CheckGroup d(scene, 0x1C200000, nullptr);
        WM(r.cgMake, !d.Create(none, none, nullptr) && !d.m_box && d.m_children.empty() && none->GetData() == CkId(0x1C200000));
    }

    WM(r.cgMake, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("groups: ipMake %d ipItem %d ipCounts %d ipMsgs %d plMake %d plInput %d plShow %d plMsgs %d cgMake %d\n",
                r.ipMake, r.ipItem, r.ipCounts, r.ipMsgs, r.plMake, r.plInput, r.plShow, r.plMsgs, r.cgMake);
}
