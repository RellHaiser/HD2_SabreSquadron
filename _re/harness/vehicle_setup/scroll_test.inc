// Part 9c-iii-a's controls (game/controls.cpp: the slider, the scroll bar, the spin control) and
// C_Control::FindControl (game/control.cpp), in the text build, run by ControlsTest (its fonts, its
// screen and its cursor fakes). Prints "scroll:" with 1 per check met.

namespace {

struct S_ScrollResults {
    bool scFind = true, scSlider = true, scSliderDrag = true, scSliderMsgs = true, scBar = true, scBarMsgs = true;
    bool scBarInput = true, scBarHeld = true, scBarArrows = true, scSpin = true, scSpinMsgs = true, scSpinInput = true;
};

// A frame's local bound (up to date).
void ScBound(I3D_frame *f, const S_vector &min, const S_vector &max)
{
    f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
    f->m_bound.bbox.min = min;
    f->m_bound.bbox.max = max;
}
// A float in a message's value2 and back.
uintptr_t ScFloat(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
float ScValue(uintptr_t value)
{
    const auto bits = static_cast<uint32_t>(value);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
// A button's three frames under a dummy named `name` (its own name's last part on each).
I3D_frame *ScArrow(const std::string &name, const std::string &tag, I3D_frame *parent)
{
    I3D_frame *f = GfFrame(FRAME_DUMMY, name, parent);
    GfVisual("normal" + tag, I3DVISUAL_OBJECT, f);
    GfVisual("actived" + tag, I3DVISUAL_OBJECT, f);
    GfVisual("inactived" + tag, I3DVISUAL_OBJECT, f);
    return f;
}
// A slider's frames under a dummy: the normal's bound (-0.05, -0.02) .. (0.05, 0.02) at `place`,
// the base's `min` .. `max`.
I3D_frame *ScSliderFrames(const std::string &name, const std::string &tag, I3D_frame *parent, const S_vector &place,
                          const S_vector &min, const S_vector &max)
{
    I3D_frame *f = GfFrame(FRAME_DUMMY, name, parent);
    I3D_frame *normal = GfVisual("normal" + tag, I3DVISUAL_OBJECT, f);
    ScBound(normal, S_vector(-0.05f, -0.02f, 0.0f), S_vector(0.05f, 0.02f, 0.0f));
    normal->m_localMatrix(3) = place;
    GfVisual("actived" + tag, I3DVISUAL_OBJECT, f);
    ScBound(GfVisual("sliderbase" + tag, I3DVISUAL_OBJECT, f), min, max);
    return f;
}
// A control recording the messages handled ("<name> msg <target> <id> <value>").
struct C_ScTarget : C_Control {
    const char *name;
    C_ScTarget(const char *n, uint32_t id) : C_Control(nullptr, id, nullptr), name(n) {}
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("%s msg %x %x %u", name, m->target, m->id, m->value);
        return true;
    }
};
std::string ScMessages()
{
    std::string s;
    for (const S_ControlMessage &m : g_controlMessages)
        s += CmText("%x %x %u %u|", m.target, m.id, m.value, static_cast<uint32_t>(m.value2));
    return s;
}

} // namespace

void ScrollTest(I3D_scene *scene, int st)
{
    S_ScrollResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- FindControl: down the children by the ids' fields; up first to the top; the ids' top 6
    // bits the same all the way; no id ---
    {
        C_Control root(scene, 0x04000000, nullptr);
        auto *a = new C_Control(scene, 0x04100000, &root), *b = new C_Control(scene, 0x04200000, &root);
        auto *a1 = new C_Control(scene, 0x04101000, a);
        root.m_children = {a, b};
        a->m_children = {a1};
        WM(r.scFind, root.FindControl(0x04101000, false) == a1 && root.FindControl(0x04000000, false) == &root &&
                         root.FindControl(0x04200000, true) == b && a1->FindControl(0x04200000, true) == b &&
                         a1->FindControl(0x04000000, true) == &root && a1->FindControl(0x04101000, true) == a1);
        WM(r.scFind, a1->FindControl(0x04200000, false) == nullptr && root.FindControl(0x04300000, false) == nullptr &&
                         root.FindControl(0x08101000, false) == nullptr && root.FindControl(0xFFFFFFFF, false) == nullptr &&
                         a1->FindControl(0x08200000, true) == nullptr);
        // Down after going up: not up again.
        WM(r.scFind, b->FindControl(0x04101000, true) == a1);
        // Another screen's top bits (even only bit 26): none.
        C_Control x(scene, 0x04000000, nullptr);
        x.m_children = {new C_Control(scene, 0x00100000, &x)};
        WM(r.scFind, x.FindControl(0x00100000, false) == nullptr);
        // From an id of -1: none (not up to the parent with the id).
        C_Control top(scene, 0xFC000000, nullptr);
        auto *bad = new C_Control(scene, 0xFFFFFFFF, &top);
        top.m_children = {bad};
        WM(r.scFind, bad->FindControl(0xFC000000, true) == nullptr && top.FindControl(0xFC000000, false) == &top);
        // An id of -1 on the way: none.
        a->m_id = 0xFFFFFFFF;
        WM(r.scFind, root.FindControl(0x04101000, false) == nullptr && a1->FindControl(0x04200000, true) == nullptr);
    }

    // --- the slider: its frames, the cursor; the limits by the base's bound, the slider's half
    // size in, from the normal frame's place (its z for the y); across: the value 50 ---
    {
        I3D_frame *frame = ScSliderFrames("slframe", "sl", nullptr, S_vector(0.1f, 0.3f, 0.05f), S_vector(0.2f, -0.5f, 0.0f),
                                          S_vector(0.8f, -0.45f, 0.0f));
        GfPlace(frame, S_vector(0.5f, -0.25f, 3.0f));
        auto *s = new C_Slider(scene, 0x00C00000, nullptr);
        const int refs = g_csCursor.refs;
        g_gfLog.clear();
        WM(r.scSlider, s->Create(frame, frame, nullptr) && s->m_cursor && g_csCursor.refs == refs + 1 && s->m_frame == frame);
        WM(r.scSlider, GfLogIs({"find slframe normal* 10001", "find slframe actived* 10001", "find slframe sliderbase* 10001",
                                "cursor acquire 1", "on activedsl 0", "update normalsl", "update activedsl", "on sliderbasesl 0",
                                "update sliderbasesl", "setpos slframe 0.4000 -0.2500 3.0000", "update slframe"}));
        WM(r.scSlider, s->m_horizontal && !s->m_vertical && s->m_value == 50.0f && s->m_minX == (0.05f + 0.2f) - 0.1f &&
                           s->m_maxX == (0.8f - 0.05f) - 0.1f && s->m_minY == (-0.5f - -0.02f) - 0.05f &&
                           s->m_maxY == (-0.02f + -0.45f) - 0.05f &&
                           s->m_rangeX == s->m_maxX - s->m_minX && s->m_rangeY == s->m_maxY - s->m_minY);
        I3D_frame *active = Find(frame, "actived*", 0x10001);
        WM(r.scSlider, active->GetPos() == S_vector(0.1f, 0.3f, 0.05f) &&
                           active->GetData() == reinterpret_cast<S_FrameData *>(0x00C00000) &&
                           Find(frame, "normal*", 0x10001)->GetData() == reinterpret_cast<S_FrameData *>(0x00C00000));
        // The cursor over it: the active frame, then back; inactive: both off, the enter nothing.
        s->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.scSlider, s->OnEnter(1) == CONTROL_HANDLED &&
                           GfLogIs({"on activedsl 1", "on normalsl 0", "update activedsl", "update normalsl", "notify 4003000 0"}));
        g_gfLog.clear();
        WM(r.scSlider, s->OnLeave(1) == CONTROL_HANDLED &&
                           GfLogIs({"on normalsl 1", "on activedsl 0", "update normalsl", "update activedsl", "notify 4004000 0"}));
        g_gfLog.clear();
        s->SetInactive(true);
        WM(r.scSlider, s->m_inactive && GfLogIs({"on normalsl 0", "on activedsl 0", "update normalsl", "update activedsl"}));
        g_gfLog.clear();
        WM(r.scSlider, s->OnEnter(1) == CONTROL_HANDLED && g_gfLog.empty());
        s->SetInactive(false);
        WM(r.scSlider, !s->m_inactive && GfLogIs({"on normalsl 1", "update normalsl"}));
        s->m_anyCallback = S_ControlCallback();

        // The drag: across, moved along the base (from the cursor's x, less the normal frame's place);
        // its value; the notification only when it changed.
        s->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_csCursor.move = S_vector(0.1f, 0.0f, -1.0f);
        g_csCursor.position = S_vector(0.45f, -0.3f, -1.0f);
        g_gfLog.clear();
        WM(r.scSliderDrag, s->OnInput08(1) == CONTROL_HANDLED && s->m_value == ((0.45f - 0.1f) - s->m_minX) / s->m_rangeX * 100.0f &&
                               GfLogIs({"setpos slframe 0.3500 -0.2500 3.0000", "update slframe", "notify 4008000 0"}));
        g_csCursor.position = S_vector(0.6f, -0.3f, -1.0f);
        g_gfLog.clear();
        s->OnInput08(1);
        WM(r.scSliderDrag, s->m_value == ((0.6f - 0.1f) - s->m_minX) / s->m_rangeX * 100.0f &&
                               GfLogIs({"setpos slframe 0.5000 -0.2500 3.0000", "update slframe", "notify 4008000 0"}));
        g_gfLog.clear();
        s->OnInput08(1);
        WM(r.scSliderDrag, GfLogIs({"setpos slframe 0.5000 -0.2500 3.0000", "update slframe"}));
        // Past the ends: clamped.
        g_csCursor.position = S_vector(2.0f, -0.3f, -1.0f);
        g_gfLog.clear();
        s->OnInput08(1);
        WM(r.scSliderDrag, s->m_value == 100.0f && GfLogIs({"setpos slframe 0.6500 -0.2500 3.0000", "update slframe", "notify 4008000 0"}));
        g_csCursor.position = S_vector(-2.0f, -0.3f, -1.0f);
        g_gfLog.clear();
        s->OnInput08(1);
        WM(r.scSliderDrag, s->m_value == 0.0f && GfLogIs({"setpos slframe 0.1500 -0.2500 3.0000", "update slframe", "notify 4008000 0"}));
        // No move across (only up): nothing.
        g_csCursor.move = S_vector(0.0f, 0.1f, -1.0f);
        g_csCursor.position = S_vector(0.5f, -0.3f, -1.0f);
        g_gfLog.clear();
        WM(r.scSliderDrag, s->OnInput08(1) == CONTROL_HANDLED && g_gfLog.empty() && s->m_value == 0.0f);
        s->m_anyCallback = S_ControlCallback();

        // The messages: the value set (clamped; the same: nothing), got.
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_SLIDER, 0, ScFloat(25.0f));
        g_gfLog.clear();
        handled &= s->HandleMessage(&m);
        WM(r.scSliderMsgs, s->m_value == 25.0f && GfLogIs({"setpos slframe 0.2750 -0.2500 3.0000", "update slframe"}));
        g_gfLog.clear();
        handled &= s->HandleMessage(&m);
        WM(r.scSliderMsgs, g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_SET_SLIDER, 0, ScFloat(150.0f));
        g_gfLog.clear();
        handled &= s->HandleMessage(&m);
        WM(r.scSliderMsgs, s->m_value == 100.0f && GfLogIs({"setpos slframe 0.6500 -0.2500 3.0000", "update slframe"}));
        m = CsMessage(CONTROL_MSG_SET_SLIDER, 0, ScFloat(-5.0f));
        handled &= s->HandleMessage(&m);
        WM(r.scSliderMsgs, s->m_value == 0.0f);
        m = CsMessage(CONTROL_MSG_SET_SLIDER, 0, ScFloat(0.1f));
        handled &= s->HandleMessage(&m);
        WM(r.scSliderMsgs, s->m_value == 0.1f);
        m = CsMessage(CONTROL_MSG_GET_SLIDER, 3, 7);
        handled &= s->HandleMessage(&m);
        WM(r.scSliderMsgs, ScValue(m.value2) == 0.1f && m.value == 3);
        s->m_value = 62.5f;
        handled &= s->HandleMessage(&m);
        WM(r.scSliderMsgs, ScValue(m.value2) == 62.5f);
        // Others: the base's.
        m = CsMessage(CONTROL_MSG_SET_INDEX, 4);
        handled &= s->HandleMessage(&m);
        WM(r.scSliderMsgs, s->m_index == 4);
        g_gfLog.clear();
        delete s;
        WM(r.scSlider, g_csCursor.refs == refs && GfLogIs({"cursor release"}));

        // Up and down (the base taller), its limits clamped to the interface (y -0.75 .. 0, x 0 .. 1).
        I3D_frame *tall = ScSliderFrames("sltall", "st", nullptr, S_vector(0.0f, 0.0f, 0.0f), S_vector(-0.5f, -2.0f, 0.0f),
                                         S_vector(-0.4f, 1.0f, 0.0f));
        C_Slider v(scene, 0x00C10000, nullptr);
        g_gfLog.clear();
        WM(r.scSlider, v.Create(tall, tall, nullptr) && v.m_vertical && !v.m_horizontal && v.m_minX == 0.0f &&
                           v.m_maxX == 0.0f && v.m_minY == -0.75f && v.m_maxY == 0.0f && v.m_value == 50.0f);
        WM(r.scSlider, !g_gfLog.empty() && g_gfLog[g_gfLog.size() - 2] == "setpos sltall 0.0000 -0.3750 0.0000");
        g_csCursor.move = S_vector(0.0f, 0.1f, -1.0f);
        g_csCursor.position = S_vector(0.5f, -0.6f, -1.0f);
        g_gfLog.clear();
        v.OnInput08(1);
        WM(r.scSliderDrag, v.m_value == (0.0f - -0.6f) / 0.75f * 100.0f &&
                               GfLogIs({"setpos sltall 0.0000 -0.6000 0.0000", "update sltall", }));
        g_csCursor.position = S_vector(0.5f, -2.0f, -1.0f);
        g_gfLog.clear();
        v.OnInput08(1);
        WM(r.scSliderDrag, v.m_value == 100.0f && GfLogIs({"setpos sltall 0.0000 -0.7500 0.0000", "update sltall"}));
        g_csCursor.move = S_vector(0.1f, 0.0f, -1.0f);
        g_gfLog.clear();
        WM(r.scSliderDrag, v.OnInput08(1) == CONTROL_HANDLED && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_SET_SLIDER, 0, ScFloat(20.0f));
        g_gfLog.clear();
        handled &= v.HandleMessage(&m);
        WM(r.scSliderMsgs, GfLogIs({"setpos sltall 0.0000 -0.1500 0.0000", "update sltall"}));
        // The limits past the other ends.
        I3D_frame *wide = ScSliderFrames("slwide", "sw", nullptr, S_vector(0.0f, 0.0f, 0.0f), S_vector(-3.0f, -0.1f, 0.0f),
                                         S_vector(3.0f, -0.05f, 0.0f));
        C_Slider w(scene, 0x00C20000, nullptr);
        WM(r.scSlider, w.Create(wide, wide, nullptr) && w.m_horizontal && w.m_minX == 0.0f && w.m_maxX == 1.0f &&
                           w.m_minY == -0.1f - -0.02f && w.m_maxY == -0.02f + -0.05f);
        // Small limits: inside x 0 .. 1 kept, above y 0 clamped.
        I3D_frame *small = ScSliderFrames("slsmall", "sm", nullptr, S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 0.05f, 0.0f),
                                          S_vector(0.2f, 0.15f, 0.0f));
        C_Slider sm(scene, 0x00C30000, nullptr);
        WM(r.scSlider, sm.Create(small, small, nullptr) && sm.m_minX == 0.05f && sm.m_maxX == 0.2f - 0.05f && sm.m_minY == 0.0f &&
                           sm.m_maxY == 0.0f && sm.m_horizontal);
        // The ranges the same: across.
        I3D_frame *square = GfFrame(FRAME_DUMMY, "slsquare");
        ScBound(GfVisual("normalsq", I3DVISUAL_OBJECT, square), S_vector(-0.125f, -0.125f, 0.0f), S_vector(0.125f, 0.125f, 0.0f));
        GfVisual("activedsq", I3DVISUAL_OBJECT, square);
        ScBound(GfVisual("sliderbasesq", I3DVISUAL_OBJECT, square), S_vector(0.0f, -0.75f, 0.0f), S_vector(0.5f, -0.25f, 0.0f));
        C_Slider sq(scene, 0x00C40000, nullptr);
        WM(r.scSlider, sq.Create(square, square, nullptr) && sq.m_rangeX == 0.25f && sq.m_rangeY == 0.25f && sq.m_horizontal &&
                           !sq.m_vertical);
        // No normal frame: false.
        I3D_frame *nonormal = GfFrame(FRAME_DUMMY, "slnonormal");
        GfVisual("activednn", I3DVISUAL_OBJECT, nonormal);
        GfVisual("sliderbasenn", I3DVISUAL_OBJECT, nonormal);
        C_Slider nn(scene, 0, nullptr);
        WM(r.scSlider, !nn.Create(nonormal, nonormal, nullptr) && !nn.m_normalFrame && nn.m_baseFrame);
        // A frame missing: false (the cursor taken).
        I3D_frame *none = GfFrame(FRAME_DUMMY, "slnone");
        GfVisual("normalsn", I3DVISUAL_OBJECT, none);
        GfVisual("activedsn", I3DVISUAL_OBJECT, none);
        C_Slider n(scene, 0, nullptr);
        g_gfLog.clear();
        WM(r.scSlider, !n.Create(none, none, nullptr) && n.m_cursor && !n.m_baseFrame &&
                           GfLogIs({"find slnone normal* 10001", "find slnone actived* 10001", "find slnone sliderbase* 10001",
                                    "cursor acquire 1"}));
        tall->SetData(nullptr);
    }

    // --- the scroll bar: its arrows and slider made (posted to be ticked, the bar too), routed to
    // the bar; inactive until it has positions ---
    {
        C_Control root(scene, 0x10000000, nullptr);
        I3D_frame *frame = CsVisual("sbframe");
        ScArrow("barrow upsb", "su", frame);
        ScArrow("barrow downsb", "sd", frame);
        ScSliderFrames("slidersb", "ss", frame, S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, -0.5f, 0.0f),
                       S_vector(0.5f, -0.45f, 0.0f));
        auto *bar = new C_ScrollBar(scene, 0x10100000, &root);
        auto *target = new C_ScTarget("tg", 0x10200000);
        root.m_children = {bar, target};
        WM(r.scBar, bar->m_focus == -1 && bar->m_pageStep == 1 && !bar->m_held && bar->m_takesInput);
        g_gfLog.clear();
        WM(r.scBar, bar->Create(frame, frame, nullptr) && bar->m_children.size() == 3 && bar->m_upButton == bar->m_children[0] &&
                        bar->m_downButton == bar->m_children[1] && bar->m_slider == static_cast<C_Control *>(bar->m_children[2]));
        WM(r.scBar, bar->m_upButton->m_id == 0x10100001 && bar->m_downButton->m_id == 0x10100002 && bar->m_slider->m_id == 0x10100003 &&
                        bar->m_upButton->m_parent == bar && bar->m_slider->m_parent == bar && bar->m_inactive &&
                        bar->m_upButton->m_inactive && bar->m_downButton->m_inactive && bar->m_slider->m_inactive);
        WM(r.scBar, ScMessages() == CmText("0 200007a 0 %u|0 200007a 0 %u|0 200007a 0 %u|",
                                           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bar->m_upButton)),
                                           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bar->m_downButton)),
                                           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bar))));
        WM(r.scBar, bar->m_upButton->m_routes.size() == 1 && bar->m_upButton->m_routes[0].target == 0x10100000 &&
                        bar->m_upButton->m_routes[0].id == 0x5006023 && bar->m_downButton->m_routes.size() == 1 &&
                        bar->m_downButton->m_routes[0].id == 0x5006024 && bar->m_slider->m_routes.size() == 1 &&
                        bar->m_slider->m_routes[0].target == 0x10100000 && bar->m_slider->m_routes[0].id == 0x5008025 &&
                        bar->m_slider->m_value == 0.0f);
        WM(r.scBar, Find(frame, "normalsu", 0xFFFF)->GetData() == reinterpret_cast<S_FrameData *>(0x10100001) &&
                        Find(frame, "normalss", 0xFFFF)->GetData() == reinterpret_cast<S_FrameData *>(0x10100003));
        g_controlMessages.clear();
        // Without positions, or under an inactive parent: not active.
        bar->SetInactive(false);
        WM(r.scBar, bar->m_inactive);

        // The count: active; the slider's step; the position kept within it.
        bar->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_COUNT, 5);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_count == 5 && bar->m_step == 25.0f && !bar->m_inactive && !bar->m_upButton->m_inactive &&
                            !bar->m_slider->m_inactive && bar->m_slider->m_value == 0.0f);
        root.m_inactive = true;
        bar->SetInactive(false);
        bar->SetInactive(true);
        bar->SetInactive(false);
        WM(r.scBarMsgs, bar->m_inactive);
        root.m_inactive = false;
        bar->SetInactive(false);
        WM(r.scBarMsgs, !bar->m_inactive && !bar->m_downButton->m_inactive);
        // Down a step: the slider there, the position sent to the control the value names, notified.
        bar->m_value = 0x10200000;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SCROLL_DOWN);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 1 && bar->m_slider->m_value == 25.0f &&
                            GfLogIs({"setpos slidersb 0.1500 0.0000 0.0000", "update slidersb", "tg msg 0 200002b 1", "notify 4010000 0"}));
        m = CsMessage(CONTROL_MSG_SET_PAGE_STEP, 0);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_pageStep == 1);
        m = CsMessage(CONTROL_MSG_SET_PAGE_STEP, 2);
        handled &= bar->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SCROLL_DOWN);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_pageStep == 2 && bar->m_position == 3);
        // (Down past the last: the count, the original's.)
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 5 && bar->m_slider->m_value == 100.0f);
        g_gfLog.clear();
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 5 && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_GET_POSITION, 9);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, m.value == 5);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SCROLL_UP);
        handled &= bar->HandleMessage(&m);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 1 &&
                            GfLogIs({"setpos slidersb 0.3500 0.0000 0.0000", "update slidersb", "tg msg 0 200002b 3", "notify 4010000 0",
                                     "setpos slidersb 0.1500 0.0000 0.0000", "update slidersb", "tg msg 0 200002b 1", "notify 4010000 0"}));
        handled &= bar->HandleMessage(&m);
        g_gfLog.clear();
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 0 && g_gfLog.empty());
        // The position set (the count too, the original's; past: kept), not sent, not notified.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 5);
        g_gfLog.clear();
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 5 && GfLogIs({"setpos slidersb 0.4500 0.0000 0.0000", "update slidersb"}));
        m = CsMessage(CONTROL_MSG_SET_POSITION, 6);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 5);
        // The count again: the position the last; down there: kept.
        m = CsMessage(CONTROL_MSG_SET_COUNT, 5);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 4);
        m = CsMessage(CONTROL_MSG_SCROLL_DOWN);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 4);
        // The slider follows a new step.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 1);
        handled &= bar->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_COUNT, 3);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 1 && bar->m_slider->m_value == 50.0f);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 3);
        handled &= bar->HandleMessage(&m);
        // A smaller count: the position the last.
        m = CsMessage(CONTROL_MSG_SET_COUNT, 3);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_count == 3 && bar->m_position == 2 && bar->m_step == 50.0f && bar->m_slider->m_value == 100.0f);
        // The slider moved: the position from its value (rounded), posted to the control the value
        // names and notified when it changed.
        bar->m_slider->m_value = 30.0f;
        g_controlMessages.clear();
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SLIDER_MOVED);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 1 && ScMessages() == "10200000 200002b 1 0|" && GfLogIs({"notify 4010000 0"}));
        bar->m_slider->m_value = 24.0f;
        g_controlMessages.clear();
        g_gfLog.clear();
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 0 && ScMessages() == "10200000 200002b 0 0|");
        g_controlMessages.clear();
        g_gfLog.clear();
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, g_controlMessages.empty() && g_gfLog.empty());
        bar->m_value = 0;
        bar->m_slider->m_value = 100.0f;
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_position == 2 && g_controlMessages.empty() && GfLogIs({"notify 4010000 0"}));
        // Its slider's value.
        m = CsMessage(CONTROL_MSG_GET_SLIDER);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, ScValue(m.value2) == 100.0f);
        // Others: the base's.
        m = CsMessage(CONTROL_MSG_SET_INDEX, 6);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_index == 6);
        // The wheel: a page up per 120 (or part), down.
        m = CsMessage(CONTROL_MSG_SET_COUNT, 9);
        handled &= bar->HandleMessage(&m);
        bar->m_pageStep = 1;
        bar->m_position = 4;
        WM(r.scBarMsgs, bar->OnWheel(1, 240) == CONTROL_HANDLED && bar->m_position == 2);
        bar->OnWheel(1, 121);
        WM(r.scBarMsgs, bar->m_position == 0);
        bar->OnWheel(1, -1);
        WM(r.scBarMsgs, bar->m_position == 1);
        bar->OnWheel(1, -360);
        WM(r.scBarMsgs, bar->m_position == 4);
        WM(r.scBarMsgs, bar->OnWheel(1, 0) == CONTROL_HANDLED && bar->m_position == 4);
        bar->m_position = 2;
        bar->OnWheel(1, 1);
        WM(r.scBarMsgs, bar->m_position == 1);
        // Two positions are enough.
        m = CsMessage(CONTROL_MSG_SET_COUNT, 2);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_count == 2 && bar->m_step == 100.0f);
        // Fewer than 2: none, inactive.
        m = CsMessage(CONTROL_MSG_SET_COUNT, 1);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarMsgs, bar->m_count == 0 && bar->m_position == 0 && bar->m_step == 100.0f && bar->m_inactive &&
                            bar->m_slider->m_inactive);
        bar->m_anyCallback = S_ControlCallback();

        // The input: to the child the id names (by its last 4 bits); left, none.
        m = CsMessage(CONTROL_MSG_SET_COUNT, 4);
        handled &= bar->HandleMessage(&m);
        WM(r.scBarInput, bar->FindChild(0x10100002) == 1 && bar->FindChild(0x77700003) == 2 && bar->FindChild(0x10100004) == -1 &&
                             bar->FindChild(0x10100009) == -1 &&
                             bar->FindChild(0x10100000) == -1);
        g_gfLog.clear();
        WM(r.scBarInput, bar->OnEnter(0x10100001) == CONTROL_HANDLED && bar->m_focus == 0 &&
                             GfLogIs({"on activedsu 1", "on normalsu 0", "on inactivedsu 0", "update inactivedsu", "update activedsu",
                                      "update normalsu"}));
        // The press: the arrow's (its route: SCROLL_UP to the bar, posted).
        g_controlMessages.clear();
        WM(r.scBarInput, bar->OnPress(0x10100001) == CONTROL_HANDLED && bar->m_upButton->m_held && !bar->m_held &&
                             ScMessages() == "10100000 1000023 0 0|");
        // Over the other: the leave, the enter.
        g_gfLog.clear();
        WM(r.scBarInput, bar->OnInput05(0x10100002) == CONTROL_HANDLED && bar->m_focus == 1 && !bar->m_upButton->m_held &&
                             GfLogIs({"on normalsu 1", "on activedsu 0", "on inactivedsu 0", "update normalsu", "update activedsu",
                                      "update inactivedsu", "on activedsd 1", "on normalsd 0", "on inactivedsd 0", "update inactivedsd",
                                      "update activedsd", "update normalsd"}));
        g_gfLog.clear();
        WM(r.scBarInput, bar->OnInput05(0x10100002) == CONTROL_NOT_HANDLED && g_gfLog.empty());
        // The slider: its enter; the drag its.
        g_gfLog.clear();
        WM(r.scBarInput, bar->OnEnter(0x10100003) == CONTROL_HANDLED && bar->m_focus == 2 &&
                             GfLogIs({"on normalsd 1", "on activedsd 0", "on inactivedsd 0", "update normalsd", "update activedsd",
                                      "update inactivedsd", "on activedss 1", "on normalss 0", "update activedss", "update normalss"}));
        g_csCursor.move = S_vector(0.1f, 0.0f, -1.0f);
        g_csCursor.position = S_vector(0.37f, -0.3f, -1.0f);
        g_controlMessages.clear();
        g_gfLog.clear();
        WM(r.scBarInput, bar->OnInput08(0x10100003) == CONTROL_HANDLED &&
                             bar->m_slider->m_value == (0.37f - bar->m_slider->m_minX) / bar->m_slider->m_rangeX * 100.0f &&
                             GfLogIs({"setpos slidersb 0.3700 0.0000 0.0000", "update slidersb"}) &&
                             ScMessages() == "10100000 1000025 0 0|");
        // Off the children: the leave.
        g_gfLog.clear();
        WM(r.scBarInput, bar->OnInput05(0x10100000) == CONTROL_HANDLED && bar->m_focus == -1 &&
                             GfLogIs({"on normalss 1", "on activedss 0", "update normalss", "update activedss"}));
        WM(r.scBarInput, bar->OnEnter(0x10100000) == 0 && bar->m_focus == -1 && bar->OnLeave(1) == CONTROL_HANDLED);
        WM(r.scBarInput, bar->OnInput08(0x10100000) == 0);
        // Inactive: the press and the drag handled, nothing done.
        bar->m_inactive = true;
        bar->m_focus = 2;
        g_gfLog.clear();
        WM(r.scBarInput, bar->OnPress(1) == CONTROL_HANDLED && bar->OnInput08(1) == CONTROL_HANDLED && g_gfLog.empty() && !bar->m_held);
        bar->m_inactive = false;
        bar->m_focus = -1;
        // A child that does not take the leave: the enter not handled.
        bar->m_children.push_back(new C_CsChild("k5", 0x10100005));
        g_csReturn = 0;
        bar->OnEnter(0x10100005);
        g_gfLog.clear();
        WM(r.scBarInput, bar->OnEnter(0x10100001) == CONTROL_NOT_HANDLED && bar->m_focus == 3 && GfLogIs({"k5 leave 10100001"}));
        WM(r.scBarInput, bar->OnLeave(7) == 0 && bar->m_focus == 3);
        g_csReturn = 5;
        WM(r.scBarInput, bar->OnLeave(7) == CONTROL_HANDLED && bar->m_focus == -1);
        delete bar->m_children.back();
        bar->m_children.pop_back();

        // Held on the bar (no child under the cursor): a step toward the cursor's place on the bar's
        // world box (across: its width the longer), then every 50 ms after 300 while a button is down.
        m = CsMessage(CONTROL_MSG_SET_COUNT, 5);
        handled &= bar->HandleMessage(&m);
        bar->m_position = 0;
        bar->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_csCursor.position = S_vector(0.75f, -0.3f, -1.0f);
        g_gfLog.clear();
        WM(r.scBarHeld, bar->OnPress(0x10100000) == CONTROL_HANDLED && bar->m_held && bar->m_repeatTime == 300 &&
                            bar->m_position == 1 &&
                            GfLogIs({"cursor acquire 0", "cursor release", "setpos slidersb 0.1500 0.0000 0.0000",
                                     "update slidersb", "notify 4010000 0"}));
        g_csCursor.buttons = 2;
        g_gfLog.clear();
        bar->Tick(100);
        WM(r.scBarHeld, bar->m_repeatTime == 200 && bar->m_position == 1 && GfLogIs({"cursor acquire 0", "cursor release"}));
        bar->Tick(200);
        WM(r.scBarHeld, bar->m_repeatTime == 0 && bar->m_position == 1);
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_repeatTime == 50 && bar->m_position == 2);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 4);
        // There: no step.
        bar->m_repeatTime = 0;
        g_gfLog.clear();
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 4 && bar->m_repeatTime == 50 &&
                            GfLogIs({"cursor acquire 0", "cursor acquire 0", "cursor release", "cursor release"}));
        // Back toward the start; below it: 0.
        g_csCursor.position = S_vector(0.0f, -0.3f, -1.0f);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 3);
        // Up and down when the box is taller.
        WmField<I3D_bbox>(frame, 0x170).max.y = 0.5f;
        g_csCursor.position = S_vector(0.0f, 0.5f, -1.0f);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 2);
        g_csCursor.position = S_vector(0.0f, -0.5f, -1.0f);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 3);
        WmField<I3D_bbox>(frame, 0x170).max.y = -0.125f;
        // To a place inside: one step, sent to the control the value names.
        bar->m_value = 0x10200000;
        g_csCursor.position = S_vector(0.5f, -0.3f, -1.0f);
        bar->m_repeatTime = 0;
        g_gfLog.clear();
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 2 && std::find(g_gfLog.begin(), g_gfLog.end(), "tg msg 0 200002b 2") != g_gfLog.end());
        g_csCursor.position = S_vector(0.55f, -0.3f, -1.0f);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 2);
        // Past the end: to the last.
        g_csCursor.position = S_vector(2.0f, -0.3f, -1.0f);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 4);
        // Taller: from the top.
        WmField<I3D_bbox>(frame, 0x170).max.y = 0.5f;
        g_csCursor.position = S_vector(0.0f, 0.0f, -1.0f);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 3);
        // As tall as wide: across.
        WmField<I3D_bbox>(frame, 0x170).max.y = 0.0f;
        g_csCursor.position = S_vector(0.25f, -0.5f, -1.0f);
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, bar->m_position == 2);
        WmField<I3D_bbox>(frame, 0x170).max.y = -0.125f;
        bar->m_value = 0;
        // No button down: stopped.
        g_csCursor.buttons = 0;
        bar->m_repeatTime = 0;
        bar->Tick(1);
        WM(r.scBarHeld, !bar->m_held && bar->m_position == 2);
        g_gfLog.clear();
        bar->Tick(1);
        WM(r.scBarHeld, g_gfLog.empty());
        bar->m_anyCallback = S_ControlCallback();
        // Not a visual: nothing.
        I3D_frame *dummy = GfFrame(FRAME_DUMMY, "sbdummy");
        C_ScrollBar plain(scene, 0x10300000, nullptr);
        plain.Create(dummy, dummy, nullptr);
        m = CsMessage(CONTROL_MSG_SET_COUNT, 5);
        handled &= plain.HandleMessage(&m);
        g_gfLog.clear();
        WM(r.scBarHeld, plain.OnPress(1) == CONTROL_HANDLED && plain.m_held && plain.m_position == 0 &&
                            GfLogIs({"cursor acquire 0", "cursor release"}));
        WM(r.scBar, plain.m_children.empty() && !plain.m_upButton && !plain.m_slider);
        frame->SetData(nullptr);
    }

    // --- the scroll bar without a slider: its arrows inactive at the ends ---
    {
        I3D_frame *frame = CsVisual("saframe");
        ScArrow("barrow upsa", "au", frame);
        ScArrow("barrow downsa", "ad", frame);
        C_ScrollBar bar(scene, 0x10400000, nullptr);
        bar.Create(frame, frame, nullptr);
        WM(r.scBarArrows, bar.m_children.size() == 2 && !bar.m_slider);
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_COUNT, 3);
        g_gfLog.clear();
        handled &= bar.HandleMessage(&m);
        WM(r.scBarArrows, bar.m_upButton->m_inactive && !bar.m_downButton->m_inactive);
        m = CsMessage(CONTROL_MSG_SCROLL_DOWN);
        handled &= bar.HandleMessage(&m);
        WM(r.scBarArrows, !bar.m_upButton->m_inactive && !bar.m_downButton->m_inactive);
        handled &= bar.HandleMessage(&m);
        WM(r.scBarArrows, !bar.m_upButton->m_inactive && bar.m_downButton->m_inactive);
        // Inactive: not updated.
        bar.m_inactive = true;
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= bar.HandleMessage(&m);
        WM(r.scBarArrows, bar.m_position == 0 && !bar.m_upButton->m_inactive);
        bar.SetInactive(false);
        WM(r.scBarArrows, !bar.m_inactive && bar.m_upButton->m_inactive && !bar.m_downButton->m_inactive);
        // (Its slider's value: none.)
        m = CsMessage(CONTROL_MSG_GET_SLIDER, 0, 7);
        handled &= bar.HandleMessage(&m);
        WM(r.scBarArrows, m.value2 == 7);
        frame->SetData(nullptr);
    }

    // --- the spin control: a text control, its arrows made (not posted), routed to it; inactive
    // until it has items ---
    {
        I3D_frame *frame = CsVisual("spframe");
        ScArrow("barrow upsp", "pu", frame);
        ScArrow("barrow downsp", "pd", frame);
        auto *c = new C_SpinControl(scene, 0x20100000, nullptr);
        WM(r.scSpin, c->m_selected == -1 && c->m_focus == -1);
        g_controlMessages.clear();
        WM(r.scSpin, c->Create(frame, frame, nullptr) && c->m_box && CsPlaced(*c->m_box, 10) && c->m_children.size() == 2 &&
                         c->m_upButton == c->m_children[0] && c->m_downButton == c->m_children[1] && g_controlMessages.empty());
        WM(r.scSpin, c->m_upButton->m_id == 0x20100001 && c->m_downButton->m_id == 0x20100002 && c->m_inactive &&
                         c->m_box->m_dimmed && c->m_upButton->m_inactive && c->m_downButton->m_inactive &&
                         c->m_upButton->m_routes.size() == 1 && c->m_upButton->m_routes[0].target == 0x20100000 &&
                         c->m_upButton->m_routes[0].id == 0x600601b && c->m_downButton->m_routes[0].id == 0x600601c);

        // The items: the first shown, active; by text or text id.
        C_TextBox &box = *c->m_box;
        c->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_ITEM, 0, 0, "a");
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_items.size() == 1 && c->m_selected == 0 && CsRuns(box) == "a/20 " && !c->m_inactive &&
                             !box.m_dimmed && !c->m_upButton->m_inactive && g_gfLog.size() == 12);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "q");
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_ITEM, 42, 0, "x");
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_ITEM, 0, 0, "c");
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_items.size() == 3 && std::string(c->m_items[1].c_str()) == "text 42" && CsRuns(box) == "q/3 ");
        // The next and the previous: around, shown, notified with the index.
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 1 && CsRuns(box) == "text 42/20 " && GfLogIs({"notify 4010000 1"}));
        handled &= c->HandleMessage(&m);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 0 && CsRuns(box) == "a/20 ");
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 2 && CsRuns(box) == "c/20 " && GfLogIs({"notify 4010000 2"}));
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 1);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 0 && CsRuns(box) == "a/20 ");
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= c->HandleMessage(&m);
        // The selected: its index and text; an item's text (none: -1).
        m = CsMessage(CONTROL_MSG_GET_SELECTED, 9);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, m.value == 1 && std::string(m.text.c_str()) == "text 42");
        m = CsMessage(CONTROL_MSG_GET_SELECTED_2, 9);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, m.value == 1 && std::string(m.text.c_str()) == "text 42");
        m = CsMessage(CONTROL_MSG_GET_ITEM, 2, 0, "old");
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, m.value == 2 && std::string(m.text.c_str()) == "c");
        m = CsMessage(CONTROL_MSG_GET_ITEM, 0, 0, "old");
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, m.value == 0 && std::string(m.text.c_str()) == "a");
        m = CsMessage(CONTROL_MSG_GET_ITEM, 3, 0, "old");
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, m.value == 0xFFFFFFFF && std::string(m.text.c_str()) == "old");
        m = CsMessage(CONTROL_MSG_GET_ITEM, 0xFFFFFFFE, 0, "old");
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, m.value == 0xFFFFFFFF && std::string(m.text.c_str()) == "old");
        // Selected by index (past the last: nothing).
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SELECT_ITEM, 2);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 2 && CsRuns(box) == "c/20 " && GfLogIs({"notify 4010000 2"}));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SELECT_ITEM, 3);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 2 && g_gfLog.empty());
        // Removed (the selected kept within; the text not changed; the first selected: inactive, the
        // original's).
        m = CsMessage(CONTROL_MSG_REMOVE_ITEM, 0);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_items.size() == 2 && std::string(c->m_items[0].c_str()) == "text 42" && c->m_selected == 1 &&
                             !c->m_inactive && CsRuns(box) == "c/20 ");
        m = CsMessage(CONTROL_MSG_REMOVE_ITEM, 5);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_items.size() == 2 && !c->m_inactive);
        m = CsMessage(CONTROL_MSG_REMOVE_ITEM, 1);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_items.size() == 1 && c->m_selected == 0 && c->m_inactive && box.m_dimmed);
        // Inactive: the next and the previous nothing.
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 0 && g_gfLog.empty());
        // Added to: active again (the selected kept).
        m = CsMessage(CONTROL_MSG_ADD_ITEM, 0, 0, "d");
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, !c->m_inactive && c->m_selected == 0 && c->m_items.size() == 2);
        // Cleared: none selected, inactive.
        m = CsMessage(CONTROL_MSG_CLEAR_ITEMS);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_items.empty() && c->m_selected == -1 && c->m_inactive);
        // (Without items, active: the next notifies, nothing shown; the fix.)
        c->SetInactive(false);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == 0 && GfLogIs({"notify 4010000 0"}));
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_selected == -1);
        // Others: the text control's.
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "t");
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, CsRuns(box) == "t/3 ");
        m = CsMessage(0x100006B, 3);
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 7);
        handled &= c->HandleMessage(&m);
        WM(r.scSpinMsgs, c->m_index == 7);
        c->m_anyCallback = S_ControlCallback();

        // The input: the enter the box's hover and the arrow's; the press its (the route posted); the
        // click handled.
        m = CsMessage(CONTROL_MSG_ADD_ITEM, 0, 0, "e");
        handled &= c->HandleMessage(&m);
        // (A line to hover: laid out by hand.)
        if (box.m_lines.empty())
            box.m_lines.push_back(new S_TextLine());
        box.m_hover = -1;
        g_gfLog.clear();
        WM(r.scSpinInput, c->OnEnter(0x20100002) == CONTROL_HANDLED && c->m_focus == 1 && box.m_hover == 0 &&
                              GfLogIs({"on activedpd 1", "on normalpd 0", "on inactivedpd 0", "update inactivedpd",
                                       "update activedpd", "update normalpd"}));
        g_controlMessages.clear();
        WM(r.scSpinInput, c->OnPress(0x20100002) == CONTROL_HANDLED && ScMessages() == "20100000 200001c 0 0|");
        WM(r.scSpinInput, c->OnClick(0x20100002) == CONTROL_HANDLED && c->OnInput05(0x20100002) == CONTROL_NOT_HANDLED);
        // Over the other arrow: the leave (the box's hover cleared), the enter.
        g_gfLog.clear();
        WM(r.scSpinInput, c->OnInput05(0x20100001) == CONTROL_HANDLED && c->m_focus == 0 && box.m_hover == 0 &&
                              GfLogIs({"on normalpd 1", "on activedpd 0", "on inactivedpd 0", "update normalpd", "update activedpd",
                                       "update inactivedpd", "on activedpu 1", "on normalpu 0", "on inactivedpu 0",
                                       "update inactivedpu", "update activedpu", "update normalpu"}));
        g_gfLog.clear();
        WM(r.scSpinInput, c->OnLeave(1) == CONTROL_HANDLED && c->m_focus == -1 && box.m_hover == -1 && g_gfLog.size() == 6);
        // Off the arrows: the enter handled, no focus; the press not.
        WM(r.scSpinInput, c->OnEnter(0x20100000) == CONTROL_HANDLED && c->m_focus == -1 && c->OnPress(1) == 0);
        WM(r.scSpinInput, c->OnInput05(0x20100000) == CONTROL_HANDLED && c->m_focus == -1);
        // Inactive: the press handled, nothing.
        c->m_focus = 0;
        c->m_inactive = true;
        g_controlMessages.clear();
        WM(r.scSpinInput, c->OnPress(1) == CONTROL_HANDLED && g_controlMessages.empty());
        c->m_inactive = false;
        // An arrow that does not take the leave.
        c->m_children.push_back(new C_CsChild("k6", 0x20100003));
        c->m_focus = 2;
        g_csReturn = 0;
        WM(r.scSpinInput, c->OnLeave(1) == 0 && c->m_focus == 2);
        g_csReturn = 5;
        delete c->m_children.back();
        c->m_children.pop_back();
        c->m_focus = -1;
        // Inactive: the box dimmed, the arrows inactive.
        g_gfLog.clear();
        c->SetInactive(true);
        WM(r.scSpinInput, c->m_inactive && box.m_dimmed && c->m_upButton->m_inactive && c->m_downButton->m_inactive &&
                              g_gfLog.size() == 12);
        c->SetInactive(false);
        WM(r.scSpinInput, !c->m_inactive && !box.m_dimmed && !c->m_upButton->m_inactive && !c->m_downButton->m_inactive);
        g_gfLog.clear();
        C_Control *base = c;
        delete base;

        // Without arrows: made; without a box: false.
        I3D_frame *bare = CsVisual("spbare");
        C_SpinControl b(scene, 0x20200000, nullptr);
        WM(r.scSpin, b.Create(bare, bare, nullptr) && b.m_children.empty() && b.m_inactive);
        m = CsMessage(CONTROL_MSG_ADD_ITEM, 0, 0, "z");
        handled &= b.HandleMessage(&m);
        WM(r.scSpin, !b.m_inactive && CsRuns(*b.m_box) == "z/20 ");
        frame->SetData(nullptr);
        bare->SetData(nullptr);
    }

    WM(r.scSliderMsgs, handled);
    (void)st;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("scroll: scFind %d scSlider %d scSliderDrag %d scSliderMsgs %d scBar %d scBarMsgs %d scBarInput %d scBarHeld %d "
                "scBarArrows %d scSpin %d scSpinMsgs %d scSpinInput %d\n",
                r.scFind, r.scSlider, r.scSliderDrag, r.scSliderMsgs, r.scBar, r.scBarMsgs, r.scBarInput, r.scBarHeld,
                r.scBarArrows, r.scSpin, r.scSpinMsgs, r.scSpinInput);
}
