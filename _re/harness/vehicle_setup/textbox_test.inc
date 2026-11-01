// Part 9b-i's text box (game/text_box.cpp: its runs, edit, selection, hover, scroll and place), in
// the text build (build_text.bat: the camera build with TEXT_TEST and the real C_TextBox; the
// loader's text box fakes left out), run by CameraTest instead of the camera's tests: the layout
// (part 9b-ii; its lines from g_tbLines) and Render (9b-iii) faked; IGraph's screen 800 x 600.
// Prints "textbox:" with 1 per check met.

// The layout: the lines asked for (y, above, below, gap), logged.
std::vector<std::array<int16_t, 4>> g_tbLines;
void C_TextBox::Layout()
{
    GfLog("layout");
    FreeLayout();
    for (const auto &l : g_tbLines) {
        auto *line = new S_TextLine();
        line->y = l[0];
        line->above = l[1];
        line->below = l[2];
        line->gap = l[3];
        m_lines.push_back(line);
    }
    m_dirty = false;
}
void C_TextBox::Render() { GfLog("render"); }
// The visuals' world boxes: logged, then up to date.
void __stdcall I3D_visual::UpdateWBoundProc()
{
    GfLog("wbound");
    VisualFlags() |= VISFLAGS_WBOUND_VALID;
}

namespace {

struct S_TextBoxResults {
    bool tbNew = true, tbRuns = true, tbVersion = true, tbInsert = true, tbRemove = true, tbEdit = true;
    bool tbCaret = true, tbSelect = true, tbHover = true, tbRange = true, tbScrollTo = true, tbCentre = true;
    bool tbRect = true, tbAttach = true, tbDepth = true, tbFree = true;
};

int __stdcall F_TbScrnX(void *) { return 800; }
int __stdcall F_TbScrnY(void *) { return 600; }

// A box with its runs "r0" .. ("rN"), style i, laid out (not dirty).
void TbRuns(C_TextBox &box, int n)
{
    box.Clear();
    for (int i = 0; i < n; ++i)
        box.AddText(C_str(CmText("r%d", i).c_str()), static_cast<uint8_t>(i));
    box.m_dirty = false;
}
std::string TbRunsText(const C_TextBox &box)
{
    std::string s;
    for (const S_TextRun &r : box.m_runs)
        s += CmText("%s/%d ", r.text.c_str(), r.style);
    return s;
}
// The box's rectangle: left 100, top 200, right 300, bottom 0 (height 200).
void TbPlace(C_TextBox &box)
{
    const float rect[7] = {100.0f, 200.0f, 300.0f, 0.0f, 200.0f, 200.0f, 0.5f};
    box.SetRect(rect);
    box.m_dirty = false;
}
// A rectangle with fractions: left 100, top 200.5, right 300, bottom 10.25.
void TbPlaceFractions(C_TextBox &box)
{
    const float rect[7] = {100.0f, 200.5f, 300.0f, 10.25f, 200.0f, 190.25f, 0.5f};
    box.SetRect(rect);
    box.m_dirty = false;
}
void TbLayout(C_TextBox &box)
{
    box.Layout();
    g_gfLog.clear();
}

} // namespace

void TextBoxTest()
{
    S_TextBoxResults r;
    void *const oldX = g_igraphVtable[0x18 / 4], *const oldY = g_igraphVtable[0x1C / 4];
    g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&F_TbScrnX);
    g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&F_TbScrnY);
    const bool oldIds = g_textIds;
    g_textIds = true;
    g_tbLines.clear();

    // --- a new box: the screen's size, the shared fonts, its defaults; deleted: the fonts released ---
    {
        auto *box = new C_TextBox();
        C_FontManager *const fonts = g_fontManager;
        const int refs = fonts ? fonts->m_refs : 0;
        WM(r.tbNew, fonts && box->m_fonts == fonts && box->m_screenWidth == 800 && box->m_screenHeight == 600 &&
                        box->m_margin == 10 && box->m_selection == -1 && box->m_hover == -1 && !box->m_dirty &&
                        !box->m_editing && box->m_scroll == 0 && box->m_scrollRange == 0 && !box->m_placed);
        auto *second = new C_TextBox();
        WM(r.tbNew, g_fontManager == fonts && fonts->m_refs == refs + 1);
        delete second;
        WM(r.tbNew, fonts->m_refs == refs);
        delete box;
        WM(r.tbNew, g_fontManager == nullptr);
    }

    C_TextBox box;
    // --- runs: added, by id, replaced (out of range: added), the text of one ---
    {
        box.m_dirty = false;
        WM(r.tbRuns, box.AddText(C_str("a"), 3) && box.m_dirty && box.NumRuns() == 1);
        box.m_dirty = false;
        WM(r.tbRuns, box.AddTextId(42, 4) && box.m_dirty && TbRunsText(box) == "a/3 text 42/4 ");
        box.m_dirty = false;
        WM(r.tbRuns, box.SetText(C_str("b"), 5, 1) && box.m_dirty && TbRunsText(box) == "a/3 b/5 ");
        WM(r.tbRuns, box.SetText(C_str("c"), 6, 2) && box.SetText(C_str("d"), 7, -1) && TbRunsText(box) == "a/3 b/5 c/6 d/7 ");
        WM(r.tbRuns, box.SetTextId(43, 8, 0) && TbRunsText(box) == "text 43/8 b/5 c/6 d/7 ");
        WM(r.tbRuns, box.GetRunText(1) == "b" && box.GetRunText(4) == "" && box.GetRunText(-1) == "" && box.NumRuns() == 4);
        box.m_dirty = false;
        box.Clear();
        WM(r.tbRuns, box.NumRuns() == 0 && box.m_dirty);
        // A version's text: formatted with the game's version (any other style: as it is).
        WM(r.tbVersion, box.SetTextId(99999, 0x28, 0) && TbRunsText(box) == "Version 1.12/40 ");
        WM(r.tbVersion, box.SetTextId(99999, 0x27, 0) && TbRunsText(box) == "Version %d.%02d/39 ");
        WM(r.tbVersion, box.SetTextId(99999, 0x128, 0) && TbRunsText(box) == "Version %d.%02d/40 ");
    }
    // --- inserted before a run (out of range: added), by id; removed ---
    {
        TbRuns(box, 3);
        WM(r.tbInsert, box.InsertText(C_str("x"), 9, 1) && box.m_dirty && TbRunsText(box) == "r0/0 x/9 r1/1 r2/2 ");
        WM(r.tbInsert, box.InsertText(C_str("y"), 9, 0) && box.InsertText(C_str("z"), 9, 5) && box.InsertText(C_str("w"), 9, -1) &&
                           TbRunsText(box) == "y/9 r0/0 x/9 r1/1 r2/2 z/9 w/9 ");
        WM(r.tbInsert, box.InsertTextId(7, 1, 6) && box.InsertTextId(8, 1, 9) &&
                           TbRunsText(box) == "y/9 r0/0 x/9 r1/1 r2/2 z/9 text 7/1 w/9 text 8/1 ");
        TbRuns(box, 4);
        WM(r.tbRemove, box.RemoveRun(1) && box.m_dirty && TbRunsText(box) == "r0/0 r2/2 r3/3 ");
        box.m_dirty = false;
        WM(r.tbRemove, !box.RemoveRun(3) && box.m_dirty && !box.RemoveRun(-1) && box.RemoveRun(2) && box.RemoveRun(0) &&
                           TbRunsText(box) == "r2/2 ");
    }
    // --- the edit: one run at a time; out of range: ended ---
    {
        TbRuns(box, 3);
        box.m_caretTime = 77;
        WM(r.tbEdit, box.EditRun(2) && box.m_editing && box.m_selection == 2 && box.m_caretTime == 0 && box.m_dirty);
        box.m_dirty = false;
        WM(r.tbEdit, !box.EditRun(1) && box.m_selection == 2 && !box.m_dirty);
        WM(r.tbEdit, box.EditRun(3) && !box.m_editing && box.m_selection == 3 && box.m_dirty);
        WM(r.tbEdit, box.EditRun(-1) && !box.m_editing && box.m_selection == -1);
        WM(r.tbEdit, box.EditRun(0) && box.m_editing && box.EditRun(9) && !box.m_editing && box.EditRun(1) && box.m_editing);
        // The caret: on below 500 ms, then off; the time back to 0 past 1000.
        box.TickCaret(100);
        WM(r.tbCaret, box.m_caretOn && box.m_caretTime == 100);
        box.TickCaret(399);
        WM(r.tbCaret, box.m_caretOn && box.m_caretTime == 499);
        box.TickCaret(1);
        WM(r.tbCaret, !box.m_caretOn && box.m_caretTime == 500);
        box.TickCaret(500);
        WM(r.tbCaret, !box.m_caretOn && box.m_caretTime == 1000);
        box.TickCaret(1);
        WM(r.tbCaret, !box.m_caretOn && box.m_caretTime == 0);
        box.TickCaret(0);
        WM(r.tbCaret, box.m_caretOn);
        box.EditRun(-1);
        box.TickCaret(10);
        WM(r.tbCaret, !box.m_caretOn && box.m_caretTime == 0);
    }
    // --- the selection: clamped; a run selected (its line scrolled to); its text ---
    {
        TbRuns(box, 0);
        box.m_selection = 5;
        WM(r.tbSelect, box.GetSelection() == -1 && box.m_selection == 5);
        TbRuns(box, 3);
        box.m_selection = -4;
        WM(r.tbSelect, box.GetSelection() == 0 && box.m_selection == 0);
        box.m_selection = 7;
        WM(r.tbSelect, box.GetSelection() == 2 && box.m_selection == 2);
        box.m_selection = 3;
        WM(r.tbSelect, box.GetSelection() == 2 && box.m_selection == 2);
        box.m_selection = 1;
        WM(r.tbSelect, box.GetSelection() == 1 && box.m_selection == 1);
        WM(r.tbSelect, box.GetSelectedText() == "r1" && box.SetSelectedText(C_str("new")) && box.m_dirty &&
                           TbRunsText(box) == "r0/0 new/1 r2/2 ");
        box.m_selection = 0;
        WM(r.tbSelect, box.GetSelectedText() == "r0" && box.SetSelectedText(C_str("first")) && TbRunsText(box) == "first/0 new/1 r2/2 ");
        box.m_selection = 3;
        WM(r.tbSelect, box.GetSelectedText() == "" && !box.SetSelectedText(C_str("no")) && TbRunsText(box) == "first/0 new/1 r2/2 ");
        box.m_selection = -1;
        WM(r.tbSelect, box.GetSelectedText() == "" && !box.SetSelectedText(C_str("no")));
        // SelectRun: its line (the same index) scrolled into the box.
        TbPlace(box);
        g_tbLines = {{200, 10, 5, 2}, {150, 10, 5, 2}, {-50, 10, 5, 2}};
        TbLayout(box);
        box.m_scroll = 0;
        WM(r.tbSelect, box.SelectRun(2) == 2 && box.m_selection == 2 && box.m_scroll == -57);
        WM(r.tbSelect, box.SelectRun(3) == -1 && box.SelectRun(-1) == -1 && box.m_selection == 2);
        WM(r.tbSelect, box.SelectRun(0) == 0 && box.m_selection == 0);
        box.EditRun(0);
        WM(r.tbSelect, box.SelectRun(1) == -1 && box.m_selection == 0);
        box.EditRun(-1);
    }
    // --- the hover: a line by index or by its y (the scroll's), none; the hovered selected ---
    {
        TbPlace(box);
        g_tbLines = {{100, 10, 5, 2}, {80, 10, 5, 2}};
        TbLayout(box);
        box.m_scroll = 0;
        WM(r.tbHover, box.SetHover(1) && box.m_hover == 1 && !box.SetHover(2) && !box.SetHover(-1) && box.m_hover == 1 &&
                          box.SetHover(0) && box.m_hover == 0);
        // Line 0 holds 93 < y <= 110; line 1 73 < y <= 90 (the last first).
        WM(r.tbHover, box.HoverAt(0, 110) && box.m_hover == 0 && box.HoverAt(0, 94) && box.m_hover == 0);
        WM(r.tbHover, box.HoverAt(0, 93) == false && box.m_hover == -1 && box.HoverAt(0, 90) && box.m_hover == 1 &&
                          box.HoverAt(0, 74) && !box.HoverAt(0, 73) && !box.HoverAt(0, 111));
        box.m_scroll = 20;
        WM(r.tbHover, box.HoverAt(0, 70) && box.m_hover == 1 && box.HoverAt(0, 90) && box.m_hover == 0);
        // Overlapping lines: the last one's.
        g_tbLines = {{100, 10, 5, 2}, {100, 10, 5, 2}};
        TbLayout(box);
        box.m_scroll = 0;
        WM(r.tbHover, box.HoverAt(0, 100) && box.m_hover == 1);
        WM(r.tbHover, box.ClearHover() && box.m_hover == -1);
        box.m_hover = 1;
        box.m_selection = 0;
        g_tbLines = {{200, 10, 5, 2}, {-50, 10, 5, 2}};
        TbLayout(box);
        WM(r.tbHover, box.SelectHovered() == 1 && box.m_selection == 1 && box.m_scroll == -57);
        box.m_hover = -1;
        box.m_scroll = 3;
        WM(r.tbHover, box.SelectHovered() == -1 && box.m_selection == -1 && box.m_scroll == 3);
        TbRuns(box, 3);
        box.EditRun(0);
        box.m_hover = 1;
        WM(r.tbHover, !box.SetHover(0) && !box.HoverAt(0, 100) && !box.ClearHover() && box.SelectHovered() == -1 &&
                          box.m_hover == 1 && box.m_selection == 0);
        box.EditRun(-1);
    }
    // --- the scroll's range: the last line's y above the top, 1 more; the scroll kept in it ---
    {
        TbPlace(box);
        g_tbLines = {};
        TbLayout(box);
        box.m_scroll = 5;
        box.m_scrolls = false;
        box.UpdateScrollRange();
        WM(r.tbRange, box.m_scrollRange == 0 && box.m_scroll == 0 && !box.m_scrolls);
        g_tbLines = {{300, 1, 1, 1}, {250, 1, 1, 1}};
        TbLayout(box);
        box.m_scroll = 80;
        box.UpdateScrollRange();
        WM(r.tbRange, box.m_scrollRange == 52 && box.m_scroll == 52 && box.m_scrolls);
        g_tbLines = {{300, 1, 1, 1}, {199, 1, 1, 1}};
        TbLayout(box);
        box.UpdateScrollRange();
        WM(r.tbRange, box.m_scrollRange == 0 && box.m_scroll == 0);
        g_tbLines = {{200, 1, 1, 1}};
        TbLayout(box);
        box.m_scroll = -3;
        box.UpdateScrollRange();
        WM(r.tbRange, box.m_scrollRange == 2 && box.m_scroll == 0);
        WM(r.tbRange, box.ScrollTo(2) && box.m_scroll == 2 && !box.ScrollTo(3) && box.m_scroll == 2 && box.ScrollTo(-9) &&
                          box.GetScroll() == -9);
        // A top with a fraction: the range truncated; the last line under the top: 0.
        TbPlaceFractions(box);
        g_tbLines = {{250, 1, 1, 1}};
        TbLayout(box);
        box.UpdateScrollRange();
        WM(r.tbRange, box.m_scrollRange == 51);
        g_tbLines = {{150, 1, 1, 1}};
        TbLayout(box);
        box.UpdateScrollRange();
        WM(r.tbRange, box.m_scrollRange == 0);
        TbPlace(box);
        // The range after the layout.
        box.m_dirty = true;
        box.m_scrollRange = 33;
        g_gfLog.clear();
        WM(r.tbRange, box.GetScrollRange() == 33 && GfLogIs({"layout"}) && box.GetScrollRange() == 33 && g_gfLog.size() == 1);
    }
    // --- a line scrolled into the box: above it, below it; already in it, or too high: none ---
    {
        TbPlace(box);
        // (The box: top 200, bottom 0, 200 high.)
        g_tbLines = {{250, 20, 5, 3}, {-30, 20, 5, 3}, {100, 20, 5, 3}, {100, 150, 40, 10}, {100, 150, 40, 9}};
        TbLayout(box);
        box.m_scroll = 0;
        WM(r.tbScrollTo, box.ScrollToLine(0) && box.m_scroll == 69);
        box.m_scroll = 0;
        WM(r.tbScrollTo, box.ScrollToLine(1) && box.m_scroll == -38);
        box.m_scroll = 0;
        WM(r.tbScrollTo, !box.ScrollToLine(2) && box.m_scroll == 0);
        WM(r.tbScrollTo, !box.ScrollToLine(3) && box.m_scroll == 0 && box.ScrollToLine(4));
        // At the edges: the top's (the line's top at 200), the bottom's (at 0).
        g_tbLines = {{180, 20, 5, 3}, {179, 20, 5, 3}, {8, 20, 5, 3}, {7, 20, 5, 3}};
        TbLayout(box);
        box.m_scroll = 0;
        WM(r.tbScrollTo, box.ScrollToLine(0) && box.m_scroll == -1);
        box.m_scroll = 0;
        WM(r.tbScrollTo, !box.ScrollToLine(1) && !box.ScrollToLine(2) && box.ScrollToLine(3) && box.m_scroll == -1);
        // A box with fractions (top 200.5, bottom 10.25), scrolled.
        TbPlaceFractions(box);
        g_tbLines = {{250, 20, 5, 3}, {20, 20, 5, 3}};
        TbLayout(box);
        box.m_scroll = 0;
        WM(r.tbScrollTo, box.ScrollToLine(0) && box.m_scroll == 69);
        box.m_scroll = 5;
        WM(r.tbScrollTo, box.ScrollToLine(1) && box.m_scroll == 2);
        box.m_scroll = -5;
        WM(r.tbScrollTo, !box.ScrollToLine(1) && box.m_scroll == -5);
        TbPlace(box);
        // The layout first.
        box.m_dirty = true;
        box.m_scroll = 0;
        g_gfLog.clear();
        WM(r.tbScrollTo, box.ScrollToLine(0) && GfLogIs({"layout"}));
    }
    // --- a line's centre: the box's x over the screen's width, its y (less half its gap and the
    // scroll) over the width negated; outside the box: 0, 0 ---
    {
        TbPlace(box);
        g_tbLines = {{150, 10, 5, 4}, {250, 10, 5, 4}, {5, 10, 5, 4}, {191, 10, 5, 4}, {192, 10, 5, 4}, {9, 10, 5, 4}, {8, 10, 5, 4}};
        TbLayout(box);
        box.m_scroll = 0;
        S_vector4 c = box.GetLineCentre(0);
        WM(r.tbCentre, c.x == 200.0f / 800.0f && c.y == (150.0f - 2.0f) / -800.0f && c.z == 0.0f && c.w == 0.0f);
        c = box.GetLineCentre(1);
        WM(r.tbCentre, c.x == 0.0f && c.y == 0.0f);
        c = box.GetLineCentre(2);
        WM(r.tbCentre, c.x == 0.0f && c.y == 0.0f);
        WM(r.tbCentre, box.GetLineCentre(3).x != 0.0f && box.GetLineCentre(4).x == 0.0f && box.GetLineCentre(5).x != 0.0f);
        WM(r.tbCentre, box.GetLineCentre(6).x == 0.0f && box.GetLineCentre(7).x == 0.0f && box.GetLineCentre(-1).y == 0.0f);
        box.m_scroll = 100;
        c = box.GetLineCentre(1);
        WM(r.tbCentre, c.y == (250.0f - 2.0f - 100.0f) / -800.0f && box.GetLineCentre(0).x != 0.0f && box.GetLineCentre(2).x == 0.0f);
        // A box with fractions, scrolled 5: the top's edge 206, the bottom's 15.
        TbPlaceFractions(box);
        g_tbLines = {{196, 10, 5, 4}, {197, 10, 5, 4}, {24, 10, 5, 4}, {23, 10, 5, 4}};
        TbLayout(box);
        box.m_scroll = 5;
        WM(r.tbCentre, box.GetLineCentre(0).x != 0.0f && box.GetLineCentre(1).x == 0.0f && box.GetLineCentre(2).x != 0.0f &&
                           box.GetLineCentre(3).x == 0.0f);
        TbPlace(box);
        box.m_dirty = true;
        g_gfLog.clear();
        box.GetLineCentre(0);
        WM(r.tbCentre, GfLogIs({"layout"}));
    }
    // --- the rectangle: set, attached to a visual (its world box, the margins), its depth ---
    {
        const float rect[7] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
        box.m_dirty = box.m_placed = false;
        box.SetRect(rect);
        WM(r.tbRect, box.m_left == 1.0f && box.m_top == 2.0f && box.m_right == 3.0f && box.m_bottom == 4.0f && box.m_width == 5.0f &&
                         box.m_height == 6.0f && box.m_depth == 7.0f && box.m_dirty && box.m_placed);
        I3D_frame *visual = GfVisual("tbvisual", I3DVISUAL_OBJECT);
        WmField<uint32_t>(visual, 0x1D0) |= VISFLAGS_WBOUND_VALID;
        I3D_bbox bbox;
        bbox.min = S_vector(0.25f, -0.5f, 0.0f);
        bbox.max = S_vector(0.75f, -0.125f, 0.0f);
        bbox.minW = bbox.maxW = 0.0f;
        WmField<I3D_bbox>(visual, 0x170) = bbox;
        box.m_dirty = box.m_placed = false;
        box.AttachTo(nullptr, visual);
        // (800 x 600; a margin of 10 / 1024 of 800: 7.)
        WM(r.tbAttach, box.m_left == 800.0f * 0.25f + 7.0f && box.m_top == 600.0f * -0.5f * -1.3333334f - 1.0f &&
                           box.m_right == 800.0f * 0.75f - 7.0f && box.m_bottom == 1.0f - 600.0f * -0.125f * 1.3333334f &&
                           box.m_width == box.m_right - box.m_left && box.m_height == box.m_top - box.m_bottom &&
                           box.m_depth == 0.0f && box.m_dirty && box.m_placed);
        box.SetUnknown90(0);
        box.AttachTo(nullptr, visual);
        WM(r.tbAttach, box.m_left == 200.0f && box.m_right == 600.0f);
        box.SetUnknown90(255);
        box.AttachTo(nullptr, visual);
        WM(r.tbAttach, box.m_left == 200.0f + 199.0f);
        box.SetUnknown90(10);
        // A visual's box out of date: brought up to date first.
        WmField<uint32_t>(visual, 0x1D0) &= ~VISFLAGS_WBOUND_VALID;
        box.m_dirty = false;
        g_gfLog.clear();
        box.AttachTo(nullptr, visual);
        WM(r.tbAttach, (WmField<uint32_t>(visual, 0x1D0) & VISFLAGS_WBOUND_VALID) != 0 && box.m_dirty && GfLogIs({"wbound"}));
        // No visual: nothing.
        box.m_dirty = false;
        box.m_left = 3.0f;
        box.AttachTo(nullptr, nullptr);
        WM(r.tbAttach, !box.m_dirty && box.m_left == 3.0f);
        // The depth: the distance in z less 0.1 between the camera's near and far range.
        I3D_frame *const oldActive = CmActive();
        I3D_frame *camera = CmCamera("tbcam", S_vector(0.0f, 0.0f, 2.0f));
        WmField<float>(camera, 0x124) = 1.0f;
        WmField<float>(camera, 0x128) = 11.0f;
        CmActive() = camera;
        I3D_scene *const scene = GetGame()->GetScene();
        const float zs[] = {8.1f, -4.1f, 2.0f, 13.1f, 13.2f, 3.1f};
        int inside = 0;
        for (int i = 0; i < 6; ++i) {
            GfPlace(visual, S_vector(0.0f, 0.0f, zs[i]));
            box.AttachTo(scene, visual);
            const float expected = (std::fabs(zs[i] - 2.0f) - 0.1f - 1.0f) / (11.0f - 1.0f);
            const float want = expected > 1.0f || !(expected >= 0.0f) ? -1.0f : expected;
            inside += want != -1.0f;
            WM(r.tbDepth, box.m_depth == want);
        }
        WM(r.tbDepth, inside >= 2);
        GfPlace(visual, S_vector(0.0f, 0.0f, 12.0f));
        box.AttachTo(scene, visual);
        WM(r.tbDepth, box.m_depth == (10.0f - 0.1f - 1.0f) / 10.0f);
        // Exactly at the near plane: 0.
        GfPlace(camera, S_vector(0.0f, 0.0f, 0.0f));
        WmField<float>(camera, 0x124) = 0.0f;
        GfPlace(visual, S_vector(0.0f, 0.0f, 0.1f));
        box.AttachTo(scene, visual);
        WM(r.tbDepth, box.m_depth == 0.0f);
        CmActive() = nullptr;
        box.AttachTo(scene, visual);
        WM(r.tbDepth, box.m_depth == -1.0f);
        CmActive() = oldActive;
    }
    // --- the layout freed: its lines (their words) and pieces ---
    {
        g_tbLines = {{1, 1, 1, 1}, {2, 2, 2, 2}};
        TbLayout(box);
        box.m_lines[0]->words.push_back(new S_TextWord{C_str("w")});
        box.m_pieces.push_back(new S_TextPiece{C_str("p")});
        box.FreeLayout();
        WM(r.tbFree, box.m_lines.empty() && box.m_pieces.empty());
        S_TextLine line;
        WM(r.tbFree, line.y == -1 && line.above == -1 && line.below == -1 && line.gap == -1 && line.unknown1C == -1);
    }

    g_textIds = oldIds;
    g_igraphVtable[0x18 / 4] = oldX;
    g_igraphVtable[0x1C / 4] = oldY;
    std::printf("textbox: tbNew %d tbRuns %d tbVersion %d tbInsert %d tbRemove %d tbEdit %d tbCaret %d tbSelect %d tbHover %d "
                "tbRange %d tbScrollTo %d tbCentre %d tbRect %d tbAttach %d tbDepth %d tbFree %d\n",
                r.tbNew, r.tbRuns, r.tbVersion, r.tbInsert, r.tbRemove, r.tbEdit, r.tbCaret, r.tbSelect, r.tbHover, r.tbRange,
                r.tbScrollTo, r.tbCentre, r.tbRect, r.tbAttach, r.tbDepth, r.tbFree);
}
