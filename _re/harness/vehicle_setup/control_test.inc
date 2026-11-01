// Part 9c's controls (game/control.cpp: 9c-i the base, its messages, descriptions and queue), in the
// text build, run by CameraTest after TextBoxTest: the frames GfFrame's (their on / update / find
// logged), the factory CreateChild faked (logged). Prints "control:" with 1 per check met.

#include <functional>

#define private public
#define protected public
#include "game/control.h"
#undef protected
#undef private

// The factory (part 9c's last) faked: logged.
bool C_Control::CreateChild(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    GfLog("child %s %s %s %d", GfName(frame), GfName(root), desc->name.c_str(), desc->type);
    return true;
}

namespace {

struct S_ControlResults {
    bool ctNew = true, ctDesc = true, ctFind = true, ctInput = true, ctShow = true, ctCreate = true, ctEvents = true;
    bool ctMessages = true, ctRoutes = true, ctCallbacks = true, ctRemove = true, ctRefresh = true, ctDelete = true;
    bool ctQueue = true;
};

// A control logging its refresh, its copies handled, its inactivity and its deletion ("<name> ...").
struct C_CtLogged : C_Control {
    const char *name;
    C_CtLogged(const char *n, uint32_t id = 0, C_Control *parent = nullptr) : C_Control(nullptr, id, parent), name(n) {}
    ~C_CtLogged() override { GfLog("%s deleted", name); }
    void Refresh() override
    {
        GfLog("%s refresh", name);
        C_Control::Refresh();
    }
    bool HandleMessageCopy(const S_ControlMessage &m) override
    {
        GfLog("%s copy %x %x %x %x", name, m.target, m.id, m.value, static_cast<uint32_t>(m.value2));
        return C_Control::HandleMessageCopy(m);
    }
    void SetInactive(bool inactive) override
    {
        GfLog("%s inactive %d", name, inactive);
        C_Control::SetInactive(inactive);
    }
};

// A description counting its deletions.
int g_ctDescDeleted;
std::string g_ctDescOrder;
struct S_CtDesc : S_ControlDesc {
    S_CtDesc(const char *name, int type = 2) : S_ControlDesc(type, name, 0) {}
    ~S_CtDesc() override
    {
        ++g_ctDescDeleted;
        g_ctDescOrder += name.c_str();
    }
};

// The callbacks: logged ("cb<n> object argument control notification value"), their result `g_ctResult`.
bool g_ctResult = true;
int g_ctObjects[3];
bool CtCallback(int n, void *object, void *argument, C_Control *control, uint32_t notification, uint32_t value)
{
    const auto index = [](void *p) { return p ? static_cast<int>(static_cast<int *>(p) - g_ctObjects) : -1; };
    GfLog("cb%d %d %d %s %x %u", n, index(object), index(argument), control ? static_cast<C_CtLogged *>(control)->name : "-",
          notification, value);
    return g_ctResult;
}
bool CtCallback1(void *o, void *a, C_Control *c, uint32_t n, uint32_t v) { return CtCallback(1, o, a, c, n, v); }
bool CtCallback2(void *o, void *a, C_Control *c, uint32_t n, uint32_t v) { return CtCallback(2, o, a, c, n, v); }
bool CtCallback3(void *o, void *a, C_Control *c, uint32_t n, uint32_t v) { return CtCallback(3, o, a, c, n, v); }

S_ControlMessage CtMessage(uint32_t target, uint32_t id, uint32_t value = 0, uintptr_t value2 = 0, uintptr_t value3 = 0)
{
    S_ControlMessage m;
    m.target = target;
    m.id = id;
    m.value = value;
    m.value2 = value2;
    m.value3 = value3;
    return m;
}
// The queue as text ("target id value; ...") and emptied.
std::string CtQueue()
{
    std::string s;
    for (const S_ControlMessage &m : g_controlMessages)
        s += CmText("%x %x %x %s; ", m.target, m.id, m.value, m.text.c_str());
    g_controlMessages.clear();
    return s;
}
// The callbacks as text ("notification object function; ...").
std::string CtCallbacks(const C_Control &c)
{
    std::string s;
    for (const S_ControlCallback &cb : c.m_callbacks)
        s += CmText("%x %d %d; ", cb.notification, cb.object ? static_cast<int>(static_cast<int *>(cb.object) - g_ctObjects) : -1,
                    cb.function == CtCallback1 ? 1 : cb.function == CtCallback2 ? 2 : cb.function == CtCallback3 ? 3 : 0);
    return s;
}
uintptr_t CtFn(ControlCallback f) { return reinterpret_cast<uintptr_t>(f); }
uintptr_t CtObj(int i) { return reinterpret_cast<uintptr_t>(&g_ctObjects[i]); }

} // namespace

void ControlTest()
{
    S_ControlResults r;
    g_controlMessages.clear();

    // --- a new control: shown, active, taking input; no frame, index -1 ---
    {
        I3D_scene *const scene = reinterpret_cast<I3D_scene *>(0x1234);
        C_Control parent(nullptr, 0, nullptr);
        C_Control c(scene, 0x00300000, &parent);
        WM(r.ctNew, c.m_scene == scene && c.m_id == 0x00300000 && c.m_parent == &parent && !c.m_frame && c.m_type == 0 &&
                        c.m_value == 0 && c.m_index == -1 && !c.m_inactive && c.m_shown && c.m_takesInput && c.m_children.empty() &&
                        c.m_routes.empty() && c.m_callbacks.empty() && c.m_anyCallback.notification == 0 && !c.m_anyCallback.function);
        WM(r.ctNew, !c.GetHighlightFrame() && !c.Unknown20(1) && !c.Unknown24(1) && !c.Unknown28() &&
                        c.Unknown68(1) == CONTROL_NOT_HANDLED);
    }

    // --- a description: its kind, name and id; messages added after the others; deleted: its
    // children, the last first ---
    {
        g_ctDescDeleted = 0;
        g_ctDescOrder.clear();
        auto *desc = new S_CtDesc("top*", 7);
        WM(r.ctDesc, desc->type == 7 && desc->name == "top*" && desc->id == 0 && desc->messages.empty());
        S_ControlDesc plain(5, "name", 0x123);
        WM(r.ctDesc, plain.type == 5 && plain.name == "name" && plain.id == 0x123);
        AddDescMessage(desc, 1, 2, 3);
        AddDescMessage(desc, 4, 5, 6, 7);
        AddDescMessage(desc, 8, 9, 10, 11, 12);
        AddDescMessage(desc, 13, 14, 15, C_str("t"));
        const auto &m = desc->messages;
        WM(r.ctDesc, m.size() == 4 && m[0].target == 1 && m[0].id == 2 && m[0].value == 3 && m[0].value2 == 0 && m[0].value3 == 0 &&
                         m[0].text.IsEmpty() && m[1].target == 4 && m[1].value == 6 && m[1].value2 == 7 && m[1].value3 == 0 &&
                         m[2].target == 8 && m[2].id == 9 && m[2].value == 10 && m[2].value2 == 11 && m[2].value3 == 12 &&
                         m[3].target == 13 && m[3].id == 14 && m[3].value == 15 && m[3].value2 == 0 && m[3].text == "t");
        auto *a = new S_CtDesc("a"), *b = new S_CtDesc("b");
        b->children.push_back(new S_CtDesc("c"));
        desc->children = {a, b};
        delete desc;
        WM(r.ctDesc, g_ctDescDeleted == 4 && g_ctDescOrder == "top*bca");
    }

    // --- children by id: the index in the field after the control's own; a child's index in range ---
    {
        C_Control top(nullptr, 0, nullptr), mid(nullptr, 0x00200000, nullptr), low(nullptr, 0x00203000, nullptr);
        const uint32_t topIds[] = {0x00100000, 0x00200000, 0x00300000}, midIds[] = {0x00201000, 0x00202000},
                       lowIds[] = {0x00202010, 0x00202020, 0x00202030};
        for (uint32_t id : topIds)
            top.m_children.push_back(new C_Control(nullptr, id, &top));
        for (uint32_t id : midIds)
            mid.m_children.push_back(new C_Control(nullptr, id, &mid));
        for (uint32_t id : lowIds)
            low.m_children.push_back(new C_Control(nullptr, id, &low));
        WM(r.ctFind, top.FindChild(0x00200000) == 1 && top.FindChild(0x00301230) == 2 && top.FindChild(0x00400000) == -1 &&
                         top.FindChild(0x00100FFF) == 0 && top.FindChild(0) == -1);
        WM(r.ctFind, mid.FindChild(0x00202000) == 1 && mid.FindChild(0x00F01000) == 0 && mid.FindChild(0x00203000) == -1 &&
                         mid.FindChild(0x00200000) == -1);
        WM(r.ctFind, low.FindChild(0x00000030) == 2 && low.FindChild(0x00F0F010) == 0 && low.FindChild(0x00000040) == -1);
        // (Six bits for the first field, eight for the others.)
        top.m_children[0]->m_id = 0x00500000 | 0x04000000;
        WM(r.ctFind, top.FindChild(0x00500000) == 0 && top.FindChild(0x01500000) == -1);
        mid.m_children[0]->m_id = 0x0020F000;
        WM(r.ctFind, mid.FindChild(0x0000F000) == 0 && mid.FindChild(0x0010F000) == 0);
        low.m_children[0]->m_id = 0x002030F0;
        WM(r.ctFind, low.FindChild(0x000000F0) == 0 && low.FindChild(0x000001F0) == -1);
        WM(r.ctFind, top.IsChild(0) && top.IsChild(2) && !top.IsChild(3) && !top.IsChild(-1) && !C_Control(nullptr, 0, nullptr).IsChild(0));
    }

    // --- taking input: shown, not inactive and taking it ---
    {
        C_Control c(nullptr, 0, nullptr);
        int met = 0;
        for (int i = 0; i < 8; ++i) {
            c.m_shown = i & 1;
            c.m_inactive = i & 2;
            c.m_takesInput = i & 4;
            met += c.TakesInput() == (i == 5);
        }
        WM(r.ctInput, met == 8);
    }

    // --- shown / hidden: the frame on or off and updated, the notification ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "ctshow");
        C_CtLogged c("c");
        c.m_frame = frame;
        c.m_routes = {CtMessage(0x100, CONTROL_NOTIFY_SHOWN | 0x123), CtMessage(0x200, CONTROL_NOTIFY_HIDDEN | 0x456)};
        c.m_anyCallback = {CONTROL_NOTIFY_ANY, &g_ctObjects[0], CtCallback1};
        c.m_shown = false;
        g_gfLog.clear();
        c.Show();
        WM(r.ctShow, c.m_shown && GfLogIs({"on ctshow 1", "update ctshow", "cb1 0 -1 c 400d000 0"}) && CtQueue() == "100 123 0 ; ");
        g_gfLog.clear();
        c.Hide();
        WM(r.ctShow, !c.m_shown && GfLogIs({"on ctshow 0", "update ctshow", "cb1 0 -1 c 400c000 0"}) && CtQueue() == "200 456 0 ; ");
    }

    // --- made: the frame taken (its data the id), the kind, the children made under the frame ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "ctframe"), *root = GfFrame(FRAME_DUMMY, "ctroot");
        C_Control c(nullptr, 0x00230000, nullptr);
        S_ControlDesc desc(9, "x", 0);
        desc.children = {new S_ControlDesc(3, "kid1*", 0), new S_ControlDesc(4, "kid2", 0)};
        g_gfLog.clear();
        WM(r.ctCreate, c.Create(frame, root, &desc) && c.m_frame == frame &&
                           frame->GetData() == reinterpret_cast<S_FrameData *>(0x00230000) && c.m_type == 9 &&
                           GfLogIs({"child ctframe ctroot kid1* 3", "child ctframe ctroot kid2 4"}));
        C_Control d(nullptr, 0x00110000, nullptr);
        g_gfLog.clear();
        WM(r.ctCreate, d.Create(frame, root, nullptr) && d.m_frame == frame &&
                           frame->GetData() == reinterpret_cast<S_FrameData *>(0x00110000) && d.m_type == 0 && g_gfLog.empty());
        frame->SetData(nullptr);
    }

    // --- the cursor's input: each slot's notification (value 0), not handled; the key's click, then the click ---
    {
        C_CtLogged c("e");
        c.m_anyCallback = {CONTROL_NOTIFY_ANY, &g_ctObjects[0], CtCallback1};
        const uint32_t codes[] = {CONTROL_NOTIFY_ENTER, CONTROL_NOTIFY_LEAVE, CONTROL_NOTIFY_CLICK, CONTROL_NOTIFY_02,
                                  CONTROL_NOTIFY_05,    CONTROL_NOTIFY_RELEASE, CONTROL_NOTIFY_PRESS, CONTROL_NOTIFY_WHEEL,
                                  CONTROL_NOTIFY_08,    CONTROL_NOTIFY_09,    CONTROL_NOTIFY_0A,    CONTROL_NOTIFY_KEY_CLICK,
                                  CONTROL_NOTIFY_13,    CONTROL_NOTIFY_0B};
        for (int i = 0; i < 14; ++i)
            c.m_routes.push_back(CtMessage(0x10 + i, codes[i] | (0x100 + i), i));
        const std::function<int(C_Control &, int)> slots[] = {
            &C_Control::OnEnter,   &C_Control::OnLeave,   &C_Control::OnClick,
            &C_Control::OnInput02, &C_Control::OnInput05, &C_Control::OnRelease,
            &C_Control::OnPress,   [](C_Control &x, int id) { return x.OnWheel(id, 3); },
            &C_Control::OnInput08, &C_Control::OnInput09, &C_Control::OnInput0A,
            &C_Control::OnKeyClick, &C_Control::OnInput13, &C_Control::OnInput0B};
        for (int i = 0; i < 14; ++i) {
            g_gfLog.clear();
            const int result = slots[i](c, 5);
            const std::string want = i == 11 ? "1b 10b b ; 12 102 2 ; " : CmText("%x %x %x ; ", 0x10 + i, 0x100 + i, i);
            const std::vector<std::string> log =
                i == 11 ? std::vector<std::string>{"cb1 0 -1 e 4012000 0", "cb1 0 -1 e 4001000 0"}
                        : std::vector<std::string>{CmText("cb1 0 -1 e %x 0", codes[i])};
            WM(r.ctEvents, result == CONTROL_NOT_HANDLED && CtQueue() == want && g_gfLog == log);
        }
        g_gfLog.clear();
    }

    // --- messages: a route kept; each code; a value read back in the message ---
    {
        C_CtLogged c("m");
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "ctmsg");
        c.m_frame = frame;
        S_ControlMessage m = CtMessage(5, CONTROL_NOTIFY_CLICK | CONTROL_MSG_HIDE, 7);
        WM(r.ctMessages, c.HandleMessage(&m) && c.m_routes.size() == 1 && c.m_routes[0].target == 5 &&
                             c.m_routes[0].id == (CONTROL_NOTIFY_CLICK | CONTROL_MSG_HIDE) && c.m_routes[0].value == 7);
        m = CtMessage(0, 0x40000000 | CONTROL_NOTIFY_05);
        WM(r.ctMessages, c.HandleMessage(&m) && c.m_routes.size() == 2);
        g_gfLog.clear();
        m = CtMessage(0, CONTROL_MSG_DEACTIVATE);
        c.HandleMessage(&m);
        c.HandleMessage(&m);
        WM(r.ctMessages, c.m_inactive && GfLogIs({"m inactive 1"}));
        m = CtMessage(0, CONTROL_MSG_ACTIVATE);
        c.HandleMessage(&m);
        c.HandleMessage(&m);
        WM(r.ctMessages, !c.m_inactive && GfLogIs({"m inactive 1", "m inactive 0"}));
        g_gfLog.clear();
        m = CtMessage(0, CONTROL_MSG_HIDE);
        c.HandleMessage(&m);
        WM(r.ctMessages, !c.m_shown && GfLogIs({"on ctmsg 0", "update ctmsg"}));
        m = CtMessage(0, CONTROL_MSG_SHOW | 0x80000000);
        c.HandleMessage(&m);
        WM(r.ctMessages, c.m_shown);
        m = CtMessage(0, CONTROL_MSG_IGNORE_INPUT);
        c.HandleMessage(&m);
        WM(r.ctMessages, !c.m_takesInput);
        m = CtMessage(0, CONTROL_MSG_TAKE_INPUT);
        c.HandleMessage(&m);
        WM(r.ctMessages, c.m_takesInput);
        m = CtMessage(0, CONTROL_MSG_SET_INDEX, 42);
        c.HandleMessage(&m);
        m = CtMessage(0, CONTROL_MSG_SET_VALUE, 77);
        c.HandleMessage(&m);
        WM(r.ctMessages, c.m_index == 42 && c.m_value == 77);
        m = CtMessage(0, CONTROL_MSG_GET_INDEX, 1);
        WM(r.ctMessages, c.HandleMessage(&m) && m.value == 42);
        m = CtMessage(0, CONTROL_MSG_GET_VALUE, 1);
        WM(r.ctMessages, c.HandleMessage(&m) && m.value == 77);
        // A raised notification (its bit 0x04000000 needed).
        c.m_routes = {CtMessage(0x77, CONTROL_NOTIFY_08 | 0x5)};
        c.m_anyCallback = {CONTROL_NOTIFY_ANY, &g_ctObjects[1], CtCallback2};
        m = CtMessage(0, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_08, 7, 8);
        g_gfLog.clear();
        c.HandleMessage(&m);
        WM(r.ctMessages, CtQueue() == "77 5 0 ; " && GfLogIs({"cb2 1 -1 m 4008000 0"}));
        c.m_anyCallback = S_ControlCallback();
        m = CtMessage(0, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_08 & ~0x4000000u);
        c.HandleMessage(&m);
        WM(r.ctMessages, CtQueue().empty());
        // Unknown codes: nothing (true).
        const uint32_t unknown[] = {0x1000000, 0x100000D, 0x10000A0, 0x10000A2, 0x200002B, 0x200002D, 0x2000001};
        for (uint32_t id : unknown) {
            m = CtMessage(0, id, 9);
            g_gfLog.clear();
            WM(r.ctMessages, c.HandleMessage(&m) && m.value == 9 && g_gfLog.empty() && CtQueue().empty());
        }
        // The refresh: passed to each child (a message of its own).
        c.m_children = {new C_CtLogged("k1"), new C_CtLogged("k2")};
        g_gfLog.clear();
        m = CtMessage(3, CONTROL_MSG_REFRESH, 4, 5);
        c.HandleMessage(&m);
        WM(r.ctMessages, GfLogIs({"k1 copy 0 200002c 0 0", "k2 copy 0 200002c 0 0"}));
        // HandleMessages: each in turn; a copy: the copy handled (the original not changed).
        std::vector<S_ControlMessage> list = {CtMessage(0, CONTROL_MSG_SET_VALUE, 5), CtMessage(0, CONTROL_MSG_GET_INDEX, 1)};
        WM(r.ctMessages, c.HandleMessages(list) && c.m_value == 5 && list[1].value == 42);
        const S_ControlMessage get = CtMessage(0, CONTROL_MSG_GET_VALUE, 1);
        WM(r.ctMessages, c.HandleMessageCopy(get) && get.value == 1);
        g_gfLog.clear();
    }

    // --- notifications: each route on it, its message (the notification's bits cleared) posted to its
    // target, or handled by the control itself (-1) ---
    {
        C_CtLogged c("n");
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "ctnote");
        c.m_frame = frame;
        S_ControlMessage text = CtMessage(0x300, CONTROL_NOTIFY_CLICK | 0x8000000 | 0x10, 2);
        text.text = C_str("hello");
        c.m_routes = {CtMessage(0x100, CONTROL_NOTIFY_CLICK | 0x30000FF, 1), CtMessage(0x200, CONTROL_NOTIFY_LEAVE | 0x5, 9), text,
                      CtMessage(0xFFFFFFFF, CONTROL_NOTIFY_CLICK | CONTROL_MSG_HIDE, 0),
                      CtMessage(0x400, CONTROL_NOTIFY_CLICK | 0x40000000, 3)};
        g_gfLog.clear();
        c.Notify(CONTROL_NOTIFY_CLICK, 0);
        WM(r.ctRoutes, CtQueue() == "100 30000ff 1 ; 300 8000010 2 hello; 400 40000000 3 ; " && !c.m_shown &&
                           GfLogIs({"on ctnote 0", "update ctnote"}));
        // (A route on a notification with more bits: not that notification.)
        c.Notify(CONTROL_NOTIFY_CLICK | 0x100000, 0);
        WM(r.ctRoutes, CtQueue().empty());
        // A route to the control added by its own route's message: kept for the next.
        c.m_routes = {CtMessage(0xFFFFFFFF, CONTROL_NOTIFY_SHOWN | CONTROL_NOTIFY_LEAVE, 0)};
        c.Notify(CONTROL_NOTIFY_SHOWN, 0);
        WM(r.ctRoutes, c.m_routes.size() == 1);
    }

    // --- callbacks: added (a notification and a function needed; ANY's apart), the first on the
    // notification called, ANY's instead of the list ---
    {
        C_CtLogged c("cb");
        S_ControlMessage m = CtMessage(0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, CtObj(0), CtFn(CtCallback1));
        c.HandleMessage(&m);
        m = CtMessage(0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, CtObj(1), CtFn(CtCallback2));
        c.HandleMessage(&m);
        m = CtMessage(0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_LEAVE, CtObj(2), CtFn(CtCallback3));
        c.HandleMessage(&m);
        m = CtMessage(0, CONTROL_MSG_ADD_CALLBACK, 0, CtObj(2), CtFn(CtCallback3));
        c.HandleMessage(&m);
        m = CtMessage(0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_ENTER, CtObj(2), 0);
        c.HandleMessage(&m);
        WM(r.ctCallbacks, c.AddCallback(CtMessage(0, 0, CONTROL_NOTIFY_02, 0, CtFn(CtCallback1))) &&
                              !c.AddCallback(CtMessage(0, 0, 0, 0, CtFn(CtCallback1))) &&
                              !c.AddCallback(CtMessage(0, 0, CONTROL_NOTIFY_02, CtObj(0), 0)));
        WM(r.ctCallbacks, CtCallbacks(c) == "4001000 0 1; 4001000 1 2; 4004000 2 3; 4002000 -1 1; ");
        g_gfLog.clear();
        g_ctResult = false;
        WM(r.ctCallbacks, !c.CallCallbacks(CONTROL_NOTIFY_CLICK, 7) && GfLogIs({"cb1 0 0 cb 4001000 7"}));
        g_ctResult = true;
        g_gfLog.clear();
        c.Notify(CONTROL_NOTIFY_LEAVE, 8);
        WM(r.ctCallbacks, GfLogIs({"cb3 2 2 cb 4004000 8"}) && c.CallCallbacks(CONTROL_NOTIFY_ENTER, 0) && g_gfLog.size() == 1);
        // A callback without its function in the list: skipped (the next on the notification called).
        c.m_callbacks.insert(c.m_callbacks.begin(), S_ControlCallback{CONTROL_NOTIFY_CLICK, &g_ctObjects[2], nullptr});
        g_gfLog.clear();
        WM(r.ctCallbacks, c.CallCallbacks(CONTROL_NOTIFY_CLICK, 1) && GfLogIs({"cb1 0 0 cb 4001000 1"}));
        c.m_callbacks.erase(c.m_callbacks.begin());
        // ANY: for every notification, its argument 0.
        m = CtMessage(0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_ANY, CtObj(1), CtFn(CtCallback3));
        c.HandleMessage(&m);
        WM(r.ctCallbacks, c.m_anyCallback.notification == CONTROL_NOTIFY_ANY && c.m_callbacks.size() == 4);
        g_gfLog.clear();
        g_ctResult = false;
        WM(r.ctCallbacks, !c.CallCallbacks(CONTROL_NOTIFY_CLICK, 2) && GfLogIs({"cb3 1 -1 cb 4001000 2"}));
        g_ctResult = true;
        // Removed (ANY's): the list again.
        m = CtMessage(0, CONTROL_MSG_REMOVE_CALLBACK, CONTROL_NOTIFY_ANY);
        c.HandleMessage(&m);
        g_gfLog.clear();
        WM(r.ctCallbacks, c.m_anyCallback.notification == 0 && !c.m_anyCallback.object && !c.m_anyCallback.function &&
                              c.CallCallbacks(CONTROL_NOTIFY_CLICK, 3) && GfLogIs({"cb1 0 0 cb 4001000 3"}) && c.m_callbacks.size() == 4);
    }
    // --- removed: by the object given, else the notification, else the function (the last given
    // decides); none: nothing ---
    {
        C_Control c(nullptr, 0, nullptr);
        const auto fill = [&c]() {
            c.m_callbacks = {{CONTROL_NOTIFY_CLICK, &g_ctObjects[0], CtCallback1}, {CONTROL_NOTIFY_LEAVE, &g_ctObjects[1], CtCallback1},
                             {CONTROL_NOTIFY_CLICK, &g_ctObjects[1], CtCallback2}, {CONTROL_NOTIFY_ENTER, &g_ctObjects[2], CtCallback3}};
        };
        fill();
        c.RemoveCallback(CtMessage(0, 0, 0, 0, 0));
        WM(r.ctRemove, c.m_callbacks.size() == 4);
        c.RemoveCallback(CtMessage(0, 0, 0, 0, CtFn(CtCallback1)));
        WM(r.ctRemove, CtCallbacks(c) == "4001000 1 2; 4003000 2 3; ");
        fill();
        c.RemoveCallback(CtMessage(0, 0, CONTROL_NOTIFY_CLICK, 0, CtFn(CtCallback3)));
        WM(r.ctRemove, CtCallbacks(c) == "4004000 1 1; 4003000 2 3; ");
        fill();
        c.RemoveCallback(CtMessage(0, 0, CONTROL_NOTIFY_ENTER, CtObj(1), CtFn(CtCallback3)));
        WM(r.ctRemove, CtCallbacks(c) == "4001000 0 1; 4003000 2 3; ");
        fill();
        c.RemoveCallback(CtMessage(0, 0, CONTROL_NOTIFY_02));
        WM(r.ctRemove, c.m_callbacks.size() == 4);
        // (Nothing given: not even a callback without its function.)
        fill();
        c.m_callbacks[1].function = nullptr;
        c.RemoveCallback(CtMessage(0, 0, 0, 0, 0));
        WM(r.ctRemove, c.m_callbacks.size() == 4);
        fill();
        c.RemoveCallback(CtMessage(0, 0, 0, CtObj(2)));
        WM(r.ctRemove, CtCallbacks(c) == "4001000 0 1; 4004000 1 1; 4001000 1 2; ");
        // ANY's: cleared, the list by the object too.
        fill();
        c.m_anyCallback = {CONTROL_NOTIFY_ANY, &g_ctObjects[0], CtCallback1};
        c.RemoveCallback(CtMessage(0, 0, CONTROL_NOTIFY_ANY, CtObj(0)));
        WM(r.ctRemove, c.m_anyCallback.notification == 0 && CtCallbacks(c) == "4004000 1 1; 4001000 1 2; 4003000 2 3; ");
        fill();
        c.m_anyCallback = {CONTROL_NOTIFY_ANY, &g_ctObjects[0], CtCallback1};
        c.RemoveCallback(CtMessage(0, 0, CONTROL_NOTIFY_ANY));
        WM(r.ctRemove, c.m_anyCallback.notification == 0 && !c.m_anyCallback.function && c.m_callbacks.size() == 4);
    }

    // --- refreshed: the children (each in turn), only while shown ---
    {
        C_CtLogged top("t");
        auto *a = new C_CtLogged("a"), *b = new C_CtLogged("b");
        b->m_children.push_back(new C_CtLogged("bb"));
        top.m_children = {a, b};
        g_gfLog.clear();
        top.Refresh();
        WM(r.ctRefresh, GfLogIs({"t refresh", "a refresh", "b refresh", "bb refresh"}));
        b->m_shown = false;
        g_gfLog.clear();
        top.Refresh();
        WM(r.ctRefresh, GfLogIs({"t refresh", "a refresh", "b refresh"}));
        top.m_shown = false;
        g_gfLog.clear();
        top.Refresh();
        WM(r.ctRefresh, GfLogIs({"t refresh"}));
        g_gfLog.clear();
    }

    // --- deleted: the children, the last first ---
    {
        auto *top = new C_CtLogged("t");
        auto *b = new C_CtLogged("b");
        b->m_children = {new C_CtLogged("b1"), new C_CtLogged("b2")};
        top->m_children = {new C_CtLogged("a"), b, new C_CtLogged("c")};
        g_gfLog.clear();
        C_Control *base = top;
        delete base;
        WM(r.ctDelete, GfLogIs({"t deleted", "c deleted", "b deleted", "b2 deleted", "b1 deleted", "a deleted"}));
    }

    // --- the queue: messages kept in order, with their texts ---
    {
        S_ControlMessage m = CtMessage(1, 2, 3, 4, 5);
        m.text = C_str("q");
        PostControlMessage(m);
        PostControlMessage(CtMessage(6, 7, 8));
        WM(r.ctQueue, g_controlMessages.size() == 2 && g_controlMessages[0].value2 == 4 && g_controlMessages[0].value3 == 5 &&
                          CtQueue() == "1 2 3 q; 6 7 8 ; ");
    }

    g_gfLog.clear();
    std::printf("control: ctNew %d ctDesc %d ctFind %d ctInput %d ctShow %d ctCreate %d ctEvents %d ctMessages %d ctRoutes %d "
                "ctCallbacks %d ctRemove %d ctRefresh %d ctDelete %d ctQueue %d\n",
                r.ctNew, r.ctDesc, r.ctFind, r.ctInput, r.ctShow, r.ctCreate, r.ctEvents, r.ctMessages, r.ctRoutes, r.ctCallbacks,
                r.ctRemove, r.ctRefresh, r.ctDelete, r.ctQueue);
}
