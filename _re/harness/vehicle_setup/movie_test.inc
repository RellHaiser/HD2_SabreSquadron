// Part 9c-v-d-iii's control (game/controls.cpp: the movie control), in the text build, run by
// ControlsTest after GroupsTest (its screen; the driver's and IGraph's fakes of geffects_test.inc:
// ScrnX 640, ScrnY 480, the states logged "dset" / "dget" / "rset" / "rget"; textbox_test.inc's
// UpdateWBoundProc). Prints "movie:" with 1 per check met.

namespace {

struct S_MovieResults {
    bool mvMake = true, mvArea = true, mvFit = true, mvDraw = true, mvMsgs = true;
};

// A stream logging its calls ("stream <slot> ..."); its open's result, playing, state, size set.
void *g_mvStreamVt[SLOTS];
struct S_MvStream {
    void **vtable = g_mvStreamVt;
    HRESULT openResult = 0;
    bool playing = false;
    int state = 0;
    S_showRect size = {0, 0, 320, 240};
    S_showRect rect = {-1, -1, -1, -1};
    float volume = -1.0f;
};
uint32_t __stdcall F_MvRelease(S_MvStream *) { GfLog("stream release"); return 0; }
bool __stdcall F_MvPlaying(S_MvStream *s) { GfLog("stream playing"); return s->playing; }
HRESULT __stdcall F_MvOpen(S_MvStream *s, const char *name, uint32_t flags)
{
    GfLog("stream open %s %u", name, flags);
    return s->openResult;
}
void __stdcall F_MvClose(S_MvStream *) { GfLog("stream close"); }
void __stdcall F_MvPlay(S_MvStream *) { GfLog("stream play"); }
void __stdcall F_MvStop(S_MvStream *) { GfLog("stream stop"); }
HRESULT __stdcall F_MvSetRect(S_MvStream *s, const S_showRect *rect)
{
    s->rect = *rect;
    GfLog("stream rect %d %d %d %d", rect->x, rect->y, rect->width, rect->height);
    return 0;
}
HRESULT __stdcall F_MvSize(S_MvStream *s, S_showRect *size)
{
    *size = s->size;
    return 0;
}
int __stdcall F_MvState(S_MvStream *s) { return s->state; }
void __stdcall F_MvVolume(S_MvStream *s, float volume)
{
    s->volume = volume;
    GfLog("stream volume %.2f", volume);
}
void __stdcall F_MvVideo(S_MvStream *, int unknown) { GfLog("stream video %d", unknown); }
HRESULT __stdcall F_MvDraw(S_MvStream *, bool keepStates)
{
    GfLog("stream draw %d", keepStates);
    return 0;
}
S_MvStream *g_mvNext;
bool g_mvMakes = true;
void *__stdcall F_MvCreateStream(void *)
{
    GfLog("stream create");
    return g_mvMakes ? g_mvNext : nullptr;
}
// A visual whose world box is up to date.
I3D_frame *MvVisual(const char *name, const S_vector &min, const S_vector &max, bool valid = true)
{
    I3D_frame *f = GfVisual(name, I3DVISUAL_OBJECT);
    auto *v = static_cast<I3D_visual *>(f);
    I3D_bbox box = {};
    box.min = min;
    box.max = max;
    v->Field<I3D_bbox>(0x170) = box;
    if (valid)
        v->VisualFlags() |= VISFLAGS_WBOUND_VALID;
    else
        v->VisualFlags() &= ~VISFLAGS_WBOUND_VALID;
    return f;
}

} // namespace

void MovieTest(I3D_scene *scene)
{
    S_MovieResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();
    static bool once;
    if (!once) {
        once = true;
        g_mvStreamVt[0x04 / 4] = reinterpret_cast<void *>(&F_MvRelease);
        g_mvStreamVt[0x08 / 4] = reinterpret_cast<void *>(&F_MvPlaying);
        g_mvStreamVt[0x0C / 4] = reinterpret_cast<void *>(&F_MvOpen);
        g_mvStreamVt[0x10 / 4] = reinterpret_cast<void *>(&F_MvClose);
        g_mvStreamVt[0x14 / 4] = reinterpret_cast<void *>(&F_MvPlay);
        g_mvStreamVt[0x18 / 4] = reinterpret_cast<void *>(&F_MvStop);
        g_mvStreamVt[0x20 / 4] = reinterpret_cast<void *>(&F_MvSetRect);
        g_mvStreamVt[0x28 / 4] = reinterpret_cast<void *>(&F_MvSize);
        g_mvStreamVt[0x2C / 4] = reinterpret_cast<void *>(&F_MvState);
        g_mvStreamVt[0x3C / 4] = reinterpret_cast<void *>(&F_MvVolume);
        g_mvStreamVt[0x4C / 4] = reinterpret_cast<void *>(&F_MvVideo);
        g_mvStreamVt[0x50 / 4] = reinterpret_cast<void *>(&F_MvDraw);
    }
    void *driverSlots[SLOTS], *igraphSlots[SLOTS];
    std::memcpy(driverSlots, g_driverVtable, sizeof driverSlots);
    std::memcpy(igraphSlots, g_igraphVtable, sizeof igraphSlots);
    g_driverVtable[0x18 / 4] = reinterpret_cast<void *>(&F_GfSetState);
    g_driverVtable[0x1C / 4] = reinterpret_cast<void *>(&F_GfGetState);
    g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&F_GfScrnX);
    g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&F_GfScrnY);
    g_igraphVtable[0x38 / 4] = reinterpret_cast<void *>(&F_MvCreateStream);
    g_igraphVtable[0x74 / 4] = reinterpret_cast<void *>(&F_GfSetRenderState);
    g_igraphVtable[0x78 / 4] = reinterpret_cast<void *>(&F_GfGetRenderState);

    // --- made: the frame off, a stream, the area from the frame's world box (640 x 480; y by -4/3) ---
    {
        S_MvStream stream;
        g_mvNext = &stream;
        I3D_frame *frame = MvVisual("mvframe", S_vector(0.25f, -0.5f, 0.0f), S_vector(0.75f, -0.125f, 0.0f));
        auto *c = new C_MovieControl(scene, 0x1C100000, nullptr);
        WM(r.mvMake, !c->m_stream && !c->m_takesInput && c->m_left == 0.0f && c->m_unknown88 == 0);
        g_gfLog.clear();
        WM(r.mvMake, c->Create(frame, frame, nullptr) && c->m_stream == reinterpret_cast<ISND_stream *>(&stream) &&
                         GfLogIs({"on mvframe 0", "stream create"}) && frame->GetData() == CkId(0x1C100000));
        WM(r.mvArea, c->m_left == 160.0f && c->m_right == 480.0f && c->m_bottom == -0.5f * 480.0f * -1.3333334f &&
                         c->m_top == 480.0f * -0.125f * -1.3333334f && c->m_width == 320.0f &&
                         c->m_height == c->m_bottom - c->m_top);
        // Fitted: the area (320 x 240) the video's shape (320 x 240): its width.
        const float left = c->m_left, top = c->m_top, width = c->m_width, height = c->m_height;
        auto fit = [&](float w, float h, int vw, int vh) {
            c->m_width = w;
            c->m_height = h;
            stream.size = {0, 0, vw, vh};
            c->FitRect();
            return CmText("%d %d %d %d", stream.rect.x, stream.rect.y, stream.rect.width, stream.rect.height);
        };
        WM(r.mvFit, fit(320.0f, 240.0f, 320, 240) == CmText("160 %d 320 240", static_cast<int>(top)));
        // Taller than the video: its width, the height by the shape (not centred).
        WM(r.mvFit, fit(320.0f, 300.0f, 640, 360) == CmText("160 %d 320 180", static_cast<int>(top)));
        // Wider: its height, centred across.
        WM(r.mvFit, fit(400.0f, 150.0f, 640, 480) == CmText("%d %d 200 150", static_cast<int>(100.0f + left),
                                                             static_cast<int>(top)));
        // Truncated: 333 x 100 for 4:3 -> 133.33 wide, (333 - 133) / 2 = 100.
        WM(r.mvFit, fit(333.0f, 100.0f, 4, 3) == CmText("%d %d 133 100", static_cast<int>(100.0f + left), static_cast<int>(top)));
        WM(r.mvFit, fit(100.0f, 333.0f, 4, 3) == CmText("160 %d 100 75", static_cast<int>(top)));
        // Equal shapes: the width's.
        WM(r.mvFit, fit(300.0f, 150.0f, 200, 100) == CmText("160 %d 300 150", static_cast<int>(top)));
        // Equal shapes with a fractional edge: still the width's (its left truncated, not centred).
        c->m_left = 159.9f;
        WM(r.mvFit, fit(301.5f, 150.75f, 2, 1) == CmText("159 %d 301 150", static_cast<int>(top)));
        // Another top: its.
        c->m_top = 12.7f;
        WM(r.mvFit, fit(320.0f, 240.0f, 320, 240) == "159 12 320 240");
        c->m_top = top;
        c->m_width = width;
        c->m_height = height;
        c->m_left = left;
        stream.size = {0, 0, 320, 240};
        // Drawn while playing: the picture, the states kept; at its end played again.
        g_gfLog.clear();
        c->Refresh();
        WM(r.mvDraw, GfLogIs({"stream playing"}));
        stream.playing = true;
        g_gfDriverState3 = 5;
        g_gfRenderState14 = 0;
        g_gfLog.clear();
        c->Refresh();
        WM(r.mvDraw, GfLogIs({"stream playing", "stream video 0", "dget 3", "dset 3 0", "rget 0", "stream draw 0", "rset 0 0",
                              "dset 3 1"}));
        g_gfDriverState3 = 0;
        stream.state = 1;
        g_gfLog.clear();
        c->Refresh();
        WM(r.mvDraw, GfLogIs({"stream playing", "stream video 0", "dget 3", "dset 3 0", "rget 0", "stream draw 0", "rset 0 0",
                              "dset 3 0", "stream stop", "stream rect 160 " + std::to_string(static_cast<int>(top)) + " 320 240",
                              "stream play"}));
        stream.state = -1;
        g_gfLog.clear();
        c->Refresh();
        WM(r.mvDraw, std::count(g_gfLog.begin(), g_gfLog.end(), "stream play") == 1);
        stream.state = 2;
        g_gfLog.clear();
        c->Refresh();
        WM(r.mvDraw, std::count(g_gfLog.begin(), g_gfLog.end(), "stream play") == 0);
        // Messages: played (the name; fitted, muted); closed; stopped.
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_PLAY_MOVIE, 0, 0, "intro");
        WM(r.mvMsgs, c->HandleMessage(&m) && stream.volume == 0.0f &&
                         GfLogIs({"stream open GameMovie\\intro2 0", "stream rect 160 " + std::to_string(static_cast<int>(top)) +
                                                                       " 320 240",
                                  "stream volume 0.00", "stream play"}));
        // The open failing: while another plays, nothing more; else played anyway.
        stream.openResult = -1;
        g_gfLog.clear();
        WM(r.mvMsgs, c->HandleMessage(&m) && GfLogIs({"stream open GameMovie\\intro2 0", "stream playing"}));
        stream.playing = false;
        g_gfLog.clear();
        WM(r.mvMsgs, c->HandleMessage(&m) && g_gfLog.size() == 5 && g_gfLog.back() == "stream play");
        stream.openResult = 1;
        stream.playing = true;
        g_gfLog.clear();
        WM(r.mvMsgs, c->HandleMessage(&m) && g_gfLog.size() == 4 && g_gfLog.back() == "stream play");
        m = CsMessage(CONTROL_MSG_PLAY_MOVIE);
        g_gfLog.clear();
        WM(r.mvMsgs, c->HandleMessage(&m) && g_gfLog[0] == "stream open GameMovie\\2 0");
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_CLOSE_MOVIE);
        WM(r.mvMsgs, c->HandleMessage(&m) && GfLogIs({"stream stop", "stream close"}));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_STOP_MOVIE);
        WM(r.mvMsgs, c->HandleMessage(&m) && GfLogIs({"stream stop"}));
        // Notifications: the base's result; others the base's (true).
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_PLAY_MOVIE, 0, 0, nullptr, 0x1C100000);
        WM(r.mvMsgs, c->HandleMessage(&m) && c->m_routes.size() == 1);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 3);
        g_gfLog.clear();
        WM(r.mvMsgs, c->HandleMessage(&m) && c->m_value == 3 && g_gfLog.empty());
        // Deleted: the stream closed, released.
        g_gfLog.clear();
        delete c;
        WM(r.mvMake, GfLogIs({"stream close", "stream release"}));
        // No stream: not made; deleted without one.
        g_mvMakes = false;
        C_MovieControl n(scene, 0x1C200000, nullptr);
        n.m_stream = reinterpret_cast<ISND_stream *>(&stream);
        WM(r.mvMake, !n.Create(frame, frame, nullptr) && !n.m_stream && n.m_left == 0.0f);
        g_mvMakes = true;
        // The box not up to date: brought up to date first.
        I3D_frame *stale = MvVisual("mvstale", S_vector(0.0f, 0.0f, 0.0f), S_vector(0.5f, 0.5f, 0.0f), false);
        C_MovieControl s(scene, 0x1C300000, nullptr);
        S_MvStream other;
        g_mvNext = &other;
        g_gfLog.clear();
        WM(r.mvArea, s.Create(stale, stale, nullptr) && std::count(g_gfLog.begin(), g_gfLog.end(), "wbound") == 1 &&
                         s.m_right == 320.0f && s.m_left == 0.0f);
        // No frame: no area (false).
        s.m_frame = nullptr;
        s.m_right = 7.0f;
        s.m_frame = stale;
        WM(r.mvArea, s.FindArea() && s.m_right == 320.0f);
        s.m_frame = nullptr;
        s.m_right = 7.0f;
        WM(r.mvArea, !s.FindArea() && s.m_right == 7.0f);
        g_gfLog.clear();
    }

    std::memcpy(g_driverVtable, driverSlots, sizeof driverSlots);
    std::memcpy(g_igraphVtable, igraphSlots, sizeof igraphSlots);
    WM(r.mvMsgs, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("movie: mvMake %d mvArea %d mvFit %d mvDraw %d mvMsgs %d\n", r.mvMake, r.mvArea, r.mvFit, r.mvDraw, r.mvMsgs);
}
