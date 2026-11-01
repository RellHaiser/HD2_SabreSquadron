// Part 9b's text box (game/text_box.cpp: 9b-i its runs, edit, selection, hover, scroll and place;
// 9b-ii its layout and positioning), in the text build (build_text.bat: the camera build with
// TEXT_TEST and the real C_TextBox; the loader's text box fakes left out), run by CameraTest instead
// of the camera's tests: Render (9b-iii) faked; IGraph's screen 800 x 600; the fonts 2 and 3 fakes
// (C_TbFont). Prints "textbox:" (9b-i), "layout:" (9b-ii-a) and "position:" (9b-ii-b) with 1 per
// check met.

// The lines asked for (y, above, below, gap): the 9b-i tests' (TbLayout).
std::vector<std::array<int16_t, 4>> g_tbLines;
void TbSetLines(std::vector<S_TextLine *> &lines)
{
    for (size_t i = lines.size(); i-- > 0;)
        delete lines[i];
    lines.clear();
    for (const auto &l : g_tbLines) {
        auto *line = new S_TextLine();
        line->y = l[0];
        line->above = l[1];
        line->below = l[2];
        line->gap = l[3];
        lines.push_back(line);
    }
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
// Laid out by the real layout: the runs r0 .. r2 (the game's styles 0 .. 2) on three lines.
bool TbLaidOut(const C_TextBox &box)
{
    return box.m_lines.size() == 3 && box.m_lines[0]->words.size() == 1 && box.m_lines[0]->words[0]->text == "r0" &&
           box.m_lines[2]->words[0]->text == "r2";
}
void TbLayout(C_TextBox &box)
{
    box.FreeLayout();
    TbSetLines(box.m_lines);
    box.m_dirty = false;
    g_gfLog.clear();
}

// --- part 9b-ii ---

struct S_PositionResults {
    bool tbHeight = true, tbStack = true, tbAlign = true, tbCaret = true, tbAtX = true, tbJustify = true, tbSplit = true;
    bool tbPieceAt = true, tbOverflow = true, tbPosition = true;
};

// The box at (left, bottom), `width` by `height` pixels.
void TbRect(C_TextBox &box, float left, float bottom, float width, float height)
{
    const float rect[7] = {left, bottom + height, left + width, bottom, width, height, 0.5f};
    box.SetRect(rect);
}
// The places: per line its y and its words' x ("[y: x x]"), per piece "(x y)".
std::string TbPlaces(const C_TextBox &box)
{
    std::string s;
    for (const S_TextLine *line : box.m_lines) {
        s += CmText("[%d:", line->y);
        for (const S_TextWord *w : line->words)
            s += CmText(" %d", w->x);
        s += "]";
    }
    for (const S_TextPiece *p : box.m_pieces)
        s += CmText("(%d %d)", p->x, p->y);
    return s;
}
// A line's words: their texts and widths.
std::string TbWordsText(const S_TextLine &line)
{
    std::string s;
    for (const S_TextWord *w : line.words)
        s += CmText("%s/%d ", w->text.c_str(), w->width);
    return s;
}
// A line of words (text, style), for SplitWords.
void TbWords(S_TextLine &line, std::initializer_list<std::pair<const char *, int>> words)
{
    for (S_TextWord *w : line.words)
        delete w;
    line.words.clear();
    for (const auto &w : words)
        line.words.push_back(new S_TextWord{C_str(w.first), static_cast<uint8_t>(w.second), 1});
}
struct S_LayoutResults {
    bool tbStyle = true, tbAdvance = true, tbWidth = true, tbUtf8 = true, tbTruncate = true, tbPieces = true;
    bool tbLines = true, tbWords = true, tbChars = true, tbFail = true, tbMeasures = true;
};

// A font whose glyphs' aspects are its table's (others 0.5), the codes asked for kept.
struct C_TbFont : C_Font {
    std::vector<int> asked;
    S_Glyph glyph = {};
    void GrowVertices() override {}
    void Flush() override {}
    void ClearVertices() override {}
    void Select() override {}
    const S_Glyph *GetGlyph(int ch) override
    {
        asked.push_back(ch);
        switch (ch) {
        case ' ':
            glyph.aspect = 0.0f;
            break;
        case 'W':
        case '_':                                       // (the caret)
        case 0x416:                                     // (Cyrillic Zhe)
            glyph.aspect = 1.0f;
            break;
        case 'i':
            glyph.aspect = 0.25f;
            break;
        case '*':
            glyph.aspect = 0.75f;
            break;
        default:
            glyph.aspect = 0.5f;
        }
        return &glyph;
    }
    void AddQuad(int, uint32_t, int, int, int, int, float) override {}
};

// The profile's unicode font option.
void TbUnicode(bool on) { g_gameProfile.m_unicodeFont = on; }

// A style (font 3, 0.02 of the height: 12 pixels; above 18, below 6; left and top aligned): its index.
int TbStyle(float width, uint8_t joins, uint8_t wraps, uint32_t flags = 0, float x = -1.0f, float y = -1.0f, int font = 3,
            float size = 0.02f, float above = 0.03f, float below = 0.01f, int align = 1, int valign = 1)
{
    const S_TextStyleDesc desc = {font, 0xFFFFFFFF, size, width, x, y, below, above, 0, align, valign, joins, wraps, flags};
    return CreateTextStyle(desc);
}
// The box `width` pixels wide (100 high).
void TbWidth(C_TextBox &box, float width)
{
    const float rect[7] = {0.0f, 100.0f, width, 0.0f, width, 100.0f, 0.5f};
    box.SetRect(rect);
}
// The box's runs (text, style), laid out; the log cleared first.
bool TbLayoutRuns(C_TextBox &box, std::initializer_list<std::pair<const char *, int>> runs)
{
    box.Clear();
    for (const auto &r : runs)
        box.AddText(C_str(r.first), static_cast<uint8_t>(r.second));
    box.m_overflows = true;                             // (not positioned: left set)
    return box.Layout();
}
// The lines: their words (text/width) and measures (gap above below remaining).
std::string TbLinesText(const C_TextBox &box)
{
    std::string s;
    for (const S_TextLine *line : box.m_lines) {
        s += "[";
        for (const S_TextWord *w : line->words)
            s += CmText("%s/%d ", w->text.c_str(), w->width);
        s += CmText("%d %d %d %d]", line->gap, line->above, line->below, line->remaining);
    }
    return s;
}
std::string TbPiecesText(const C_TextBox &box)
{
    std::string s;
    for (const S_TextPiece *p : box.m_pieces)
        s += CmText("[%s/%d %d %d %d %d]", p->text.c_str(), p->style, p->width, p->height, p->x, p->y);
    return s;
}
// A code point decoded: its bytes taken (and the code point).
int TbDecode(const char *text, int length, uint32_t *ch)
{
    *ch = 0x12345;
    return DecodeUtf8(ch, text, length);
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
    // The fonts 2 and 3 the fakes (first in the manager's list).
    C_TbFont font2, font3;
    font2.m_id = 2;
    font3.m_id = 3;
    box.m_fonts->m_fonts.insert(box.m_fonts->m_fonts.begin(), {&font2, &font3});
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
        box.m_overflows = false;
        box.UpdateScrollRange();
        WM(r.tbRange, box.m_scrollRange == 0 && box.m_scroll == 0 && !box.m_overflows);
        g_tbLines = {{300, 1, 1, 1}, {250, 1, 1, 1}};
        TbLayout(box);
        box.m_scroll = 80;
        box.UpdateScrollRange();
        WM(r.tbRange, box.m_scrollRange == 52 && box.m_scroll == 52 && box.m_overflows);
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
        const int16_t range = box.GetScrollRange();
        WM(r.tbRange, range == box.m_scrollRange && range != 33 && !box.m_dirty && TbLaidOut(box));
        box.m_scrollRange = 33;
        WM(r.tbRange, box.GetScrollRange() == 33);
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
        box.ScrollToLine(0);
        WM(r.tbScrollTo, !box.m_dirty && TbLaidOut(box));
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
        box.GetLineCentre(0);
        WM(r.tbCentre, !box.m_dirty && TbLaidOut(box));
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
        WM(r.tbFree, line.y == -1 && line.above == -1 && line.below == -1 && line.gap == -1 && line.remaining == -1);
    }

    // ===== part 9b-ii: the layout =====
    S_LayoutResults l;
    g_tbLines.clear();
    const int st12 = TbStyle(0.0f, 1, 1);
    // --- the style selected: its font (the manager's), its font size; none: false ---
    {
        WM(l.tbStyle, box.SelectStyle(st12) && box.m_style == GetTextStyle(st12) && box.m_font == &font3 && box.FontSize() == 12);
        WM(l.tbStyle, !box.SelectStyle(-1) && box.m_style == nullptr && box.m_font == &font3);
        WM(l.tbStyle, !box.SelectStyle(NumTextStyles()) && box.m_style == nullptr);
        const int noFont = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 99);
        WM(l.tbStyle, !box.SelectStyle(noFont) && box.m_style == GetTextStyle(noFont) && box.m_font == nullptr);
        // (15.54 and 15.06, rounded.)
        const int big = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 2, 0.0259f);
        WM(l.tbStyle, box.SelectStyle(big) && box.m_font == &font2 && box.FontSize() == 16);
        const int small = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.0251f);
        WM(l.tbStyle, box.SelectStyle(small) && box.m_font == &font3 && box.FontSize() == 15);
        const int third = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.0255f);
        WM(l.tbStyle, box.SelectStyle(third) && box.FontSize() == 15);
        ReleaseTextStyle(third);
        ReleaseTextStyle(small);
        ReleaseTextStyle(big);
        ReleaseTextStyle(noFont);
    }
    // --- a character's advance: its glyph's aspect at the size, rounded (the glyph asked for twice);
    // a blank 0.4 of the size; the password style's '*' ---
    {
        box.SelectStyle(st12);
        font3.asked.clear();
        WM(l.tbAdvance, box.Advance('a', 12) == 6 && (font3.asked == std::vector<int>{'a', 'a'}));
        font3.asked.clear();
        WM(l.tbAdvance, box.Advance(' ', 12) == 5 && (font3.asked == std::vector<int>{' '}));
        WM(l.tbAdvance, box.Advance('W', 12) == 12 && box.Advance('i', 20) == 5 && box.Advance('a', 13) == 7);
        WM(l.tbAdvance, box.Advance(' ', 13) == 5 && box.Advance(' ', 14) == 6 && box.Advance('i', 2) == 1 && box.Advance('i', 1) == 0 &&
                            box.Advance(' ', 0) == 0 && box.Advance('W', -4) == -3);
        font3.asked.clear();
        WM(l.tbAdvance, box.Advance(-61, 12) == 6 && (font3.asked == std::vector<int>{-61, -61}));
        const int password = TbStyle(0.0f, 1, 1, STYLE_PASSWORD);
        box.SelectStyle(password);
        font3.asked.clear();
        WM(l.tbAdvance, box.Advance('a', 12) == 9 && box.Advance(' ', 12) == 9 && (font3.asked == std::vector<int>{'*', '*'}) &&
                            box.Advance('a', 13) == 10 && box.Advance('a', 11) == 8);
        const int others = TbStyle(0.0f, 1, 1, ~STYLE_PASSWORD);
        box.SelectStyle(others);
        WM(l.tbAdvance, box.Advance('a', 12) == 6 && box.Advance(' ', 12) == 5);
        ReleaseTextStyle(others);
        ReleaseTextStyle(password);
    }
    // --- a text's width: its characters' advances (the unicode font: its UTF-8 characters, a bad
    // byte one, signed) ---
    {
        box.SelectStyle(st12);
        WM(l.tbWidth, box.TextWidth(C_str("a Wi"), 12) == 26 && box.TextWidth(C_str(""), 12) == 0 && box.TextWidth(C_str(), 12) == 0);
        font3.asked.clear();
        WM(l.tbWidth, box.TextWidth(C_str("\xD0\x96"), 12) == 12 && (font3.asked == std::vector<int>{0xD0, 0xD0, 0x96, 0x96}));
        TbUnicode(true);
        font3.asked.clear();
        WM(l.tbWidth, box.TextWidth(C_str("a\xD0\x96"), 12) == 18 && (font3.asked == std::vector<int>{'a', 'a', 0x416, 0x416}));
        font3.asked.clear();
        WM(l.tbWidth, box.TextWidth(C_str("\xC3("), 12) == 12 && (font3.asked == std::vector<int>{-61, -61, '(', '('}));
        font3.asked.clear();
        WM(l.tbWidth, box.TextWidth(C_str("a\xD0"), 12) == 12 && (font3.asked == std::vector<int>{'a', 'a', -48, -48}));
        font3.asked.clear();
        WM(l.tbWidth, box.TextWidth(C_str("\xF0\x9F\x98\x80" "b"), 12) == 12 &&
                          (font3.asked == std::vector<int>{0x1F600, 0x1F600, 'b', 'b'}));
        WM(l.tbWidth, box.TextWidth(C_str("a Wi"), 12) == 26);
        TbUnicode(false);
    }
    // --- UTF-8: a code point and its bytes; malformed: -1; too few bytes: -2 (the code point 0) ---
    {
        uint32_t ch;
        WM(l.tbUtf8, TbDecode("A", 1, &ch) == 1 && ch == 'A' && TbDecode("\xD0\x96", 2, &ch) == 2 && ch == 0x416);
        WM(l.tbUtf8, TbDecode("\xE2\x82\xAC", 3, &ch) == 3 && ch == 0x20AC && TbDecode("\xF0\x9F\x98\x80", 4, &ch) == 4 && ch == 0x1F600);
        WM(l.tbUtf8, TbDecode("\xE0\xA0\x80", 3, &ch) == 3 && ch == 0x800 && TbDecode("\xE0\x9F\xBF", 3, &ch) == -1 && ch == 0);
        WM(l.tbUtf8, TbDecode("\xF0\x90\x80\x80", 4, &ch) == 4 && ch == 0x10000 && TbDecode("\xF0\x8F\xBF\xBF", 4, &ch) == -1);
        WM(l.tbUtf8, TbDecode("\xF4\x8F\xBF\xBF", 4, &ch) == 4 && ch == 0x10FFFF && TbDecode("\xF4\x90\x80\x80", 4, &ch) == -1);
        // (No surrogates' test: 0xD800 taken.)
        WM(l.tbUtf8, TbDecode("\xED\xA0\x80", 3, &ch) == 3 && ch == 0xD800);
        // A continuation below 0x80 or above 0xBF, in each place; 0x80 and 0xBF taken.
        WM(l.tbUtf8, TbDecode("\xD0\x7F", 2, &ch) == -1 && TbDecode("\xD0\xC0", 2, &ch) == -1 && TbDecode("\xE2\x7F\x80", 3, &ch) == -1 &&
                         TbDecode("\xE2\xC0\x80", 3, &ch) == -1);
        WM(l.tbUtf8, TbDecode("\xE2\x82\x7F", 3, &ch) == -1 && TbDecode("\xE2\x82\xC0", 3, &ch) == -1 &&
                         TbDecode("\xF0\x9F\x98\x7F", 4, &ch) == -1 && TbDecode("\xF0\x9F\x98\xC0", 4, &ch) == -1);
        WM(l.tbUtf8, TbDecode("\xF0\x9F\x7F\x80", 4, &ch) == -1 && TbDecode("\xF0\x9F\xC0\x80", 4, &ch) == -1 &&
                         TbDecode("\xF0\xC0\x98\x80", 4, &ch) == -1);
        WM(l.tbUtf8, TbDecode("\xDF\xBF", 2, &ch) == 2 && ch == 0x7FF && TbDecode("\xC2\x80", 2, &ch) == 2 && ch == 0x80);
        WM(l.tbUtf8, TbDecode("\xEF\xBF\xBF", 3, &ch) == 3 && ch == 0xFFFF && TbDecode("\xE1\x80\x80", 3, &ch) == 3 && ch == 0x1000);
        WM(l.tbUtf8, TbDecode("\xF1\x80\x80\x80", 4, &ch) == 4 && ch == 0x40000 && TbDecode("\xF3\xBF\xBF\xBF", 4, &ch) == 4 &&
                         ch == 0xFFFFF);
        // Lone continuations, C0 and C1: malformed.
        WM(l.tbUtf8, TbDecode("\x80", 1, &ch) == -1 && TbDecode("\xBF", 1, &ch) == -1 && TbDecode("\xC1\x81", 2, &ch) == -1 &&
                         TbDecode("\xC0\x80", 2, &ch) == -1);
        WM(l.tbUtf8, TbDecode("\x7F", 1, &ch) == 1 && ch == 0x7F && TbDecode("\xD0\x96", 1, &ch) == -2 && ch == 0 &&
                         TbDecode("A", 0, &ch) == -2);
        WM(l.tbUtf8, TbDecode("\xF5\x80\x80\x80", 4, &ch) == -1 && TbDecode("\xF8\x88\x80\x80", 4, &ch) == -2 &&
                         TbDecode("\xF8\x88\x80\x80\x80", 5, &ch) == -1 && TbDecode("\xFC\x84\x80\x80\x80", 5, &ch) == -2);
        // Each lead byte: its sequence's length (with 4 and 5 bytes: the long ones' too few).
        int wrong = 0;
        for (int b = 0; b < 256; ++b) {
            char seq[6] = {static_cast<char>(b), '\x80', '\x80', '\x80', '\x80', '\x80'};
            if (b == 0xE0)
                seq[1] = '\xA0';
            if (b == 0xF0)
                seq[1] = '\x90';
            const int want = b < 0x80 ? 1 : b < 0xC2 ? -1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF5 ? 4 : -1;
            const uint32_t c1 = static_cast<uint8_t>(seq[1]) & 0x3F;
            const uint32_t cp = b < 0x80 ? b : b < 0xE0 ? (b & 0x1F) << 6 : b < 0xF0 ? ((b & 0x0F) << 12) | (c1 << 6)
                                                                                  : ((b & 0x07) << 18) | (c1 << 12);
            const int got = TbDecode(seq, 6, &ch);
            wrong += got != want || (want > 0 && ch != cp);
            if (b >= 0xF8)
                wrong += TbDecode(seq, 4, &ch) != -2;
            if (b >= 0xF8)
                wrong += TbDecode(seq, 5, &ch) != (b >= 0xFC ? -2 : -1);
        }
        WM(l.tbUtf8, wrong == 0);
    }
    // --- a text cut to its first characters that fit (none fit, or all: the text; the size truncated) ---
    {
        box.SelectStyle(st12);
        WM(l.tbTruncate, box.Truncate(C_str("abcdef"), 12.0f, 20.0f) == "abc" && box.Truncate(C_str("abcdef"), 12.0f, 18.0f) == "abc" &&
                             box.Truncate(C_str("abcdef"), 12.0f, 17.9f) == "ab");
        WM(l.tbTruncate, box.Truncate(C_str("Wa"), 12.0f, 5.0f) == "Wa" && box.Truncate(C_str("ab"), 12.0f, 100.0f) == "ab" &&
                             box.Truncate(C_str("ab"), 12.0f, 12.0f) == "ab" && box.Truncate(C_str("abc"), 12.0f, 12.0f) == "ab" &&
                             box.Truncate(C_str(""), 12.0f, 1.0f) == "");
        WM(l.tbTruncate, box.Truncate(C_str("abcdef"), 12.9f, 20.0f) == "abc" && box.Truncate(C_str("abcdef"), 13.0f, 20.0f) == "ab");
        font3.asked.clear();
        WM(l.tbTruncate, box.Truncate(C_str("ab"), 12.0f, 100.0f) == "ab" && (font3.asked == std::vector<int>{'a', 'a', 'b', 'b'}));
        // (By bytes: a two-byte character two.)
        WM(l.tbTruncate, box.Truncate(C_str("\xD0\x96" "a"), 12.0f, 13.0f) == "\xD0\x96");
        TbUnicode(true);
        WM(l.tbTruncate, box.Truncate(C_str("a\xD0\x96" "b"), 12.0f, 20.0f) == "a\xD0\x96" &&
                             box.Truncate(C_str("a\xD0\x96" "b"), 12.0f, 10.0f) == "a");
        font3.asked.clear();
        WM(l.tbTruncate, box.Truncate(C_str("a\xC3("), 12.0f, 100.0f) == "a\xC3(" &&
                             (font3.asked == std::vector<int>{'a', 'a', -61, -61, '(', '('}));
        WM(l.tbTruncate, box.Truncate(C_str("a\xC3("), 12.0f, 13.0f) == "a\xC3" &&
                             box.Truncate(C_str("\xD0\x96\xD0\x96"), 12.0f, 5.0f) == "\xD0\x96\xD0\x96" &&
                             box.Truncate(C_str("abcdef"), 12.9f, 20.0f) == "abc" && box.Truncate(C_str("ab"), 12.0f, 12.0f) == "ab");
        TbUnicode(false);
    }
    // --- pieces: the runs whose styles place them (x and y both >= 0), their font size and width,
    // cut to their column (the screen's width times the style's, not rounded); not in the lines ---
    {
        TbWidth(box, 200.0f);
        const int place = TbStyle(0.0f, 1, 1, 0, 0.1f, 0.2f);
        const int placeCut = TbStyle(0.0201f, 1, 1, 0, 0.0f, 0.0f);
        const int half = TbStyle(0.0f, 1, 1, 0, 0.1f, -1.0f);
        const int otherHalf = TbStyle(0.0f, 1, 1, 0, -0.5f, 0.3f);
        const int nan = TbStyle(0.0f, 1, 1, 0, std::nanf(""), 0.3f);
        WM(l.tbPieces, TbLayoutRuns(box, {{"ab", place}, {"abcdef", placeCut}, {"x", half}, {"abc", placeCut}, {"y", otherHalf},
                                          {"aiii", placeCut}, {"z", nan}, {"abcdefghijklmnop", place}}) &&
                           !box.m_dirty);
        // (16.08 pixels: "ab", 12; "aiii", 15.)
        WM(l.tbPieces, TbPiecesText(box) == CmText("[ab/%d 12 12 20 20][ab/%d 12 12 0 0][ab/%d 12 12 0 0][aiii/%d 15 12 0 0]"
                                                   "[abcdefghijklmnop/%d 93 12 20 20]",
                                                   place, placeCut, placeCut, placeCut, place) &&
                           TbLinesText(box) == "[x/6 y/6 z/6 12 18 6 182]");
        WM(l.tbPieces, TbLayoutRuns(box, {{"ab", place}}) && box.m_lines.empty() && box.m_pieces.size() == 1);
        const int tiny = TbStyle(0.0f, 1, 1, 0, 0.1f, 0.2f, 3, 0.001f);
        WM(l.tbPieces, TbLayoutRuns(box, {{" ab", tiny}}) && TbPiecesText(box) == CmText("[ ab/%d 2 1 20 20]", tiny));
        ReleaseTextStyle(tiny);
        ReleaseTextStyle(nan);
        ReleaseTextStyle(otherHalf);
        ReleaseTextStyle(half);
        ReleaseTextStyle(placeCut);
        ReleaseTextStyle(place);
    }
    // --- the lines: a run of its own on a new line; joined to the last (the first joined after one
    // of its own on a new line; the new-line flag: a new line) ---
    {
        TbWidth(box, 200.0f);
        const int own = TbStyle(0.0f, 0, 1);
        const int newLine = TbStyle(0.0f, 1, 1, STYLE_NEW_LINE);
        const int other = TbStyle(0.0f, 1, 1, ~(STYLE_NEW_LINE | STYLE_PASSWORD));
        WM(l.tbLines, TbLayoutRuns(box, {{"x", own}, {"y", st12}, {"z", newLine}, {"w", st12}, {"v", own}, {"u", st12}}) &&
                          !box.m_dirty);
        WM(l.tbLines, TbLinesText(box) == "[x/6 12 18 6 194][y/6 12 18 6 194][z/6 w/6 12 18 6 188][v/6 12 18 6 194][u/6 12 18 6 194]");
        WM(l.tbLines, TbLayoutRuns(box, {{"a", own}, {"b", own}, {"c", st12}, {"d", other}, {"e", newLine}}) &&
                          TbLinesText(box) == "[a/6 12 18 6 194][b/6 12 18 6 194][c/6 d/6 12 18 6 188][e/6 12 18 6 194]");
        WM(l.tbLines, TbLayoutRuns(box, {{"z", newLine}, {"w", st12}}) && TbLinesText(box) == "[z/6 w/6 12 18 6 188]");
        WM(l.tbLines, box.m_lines[0]->words[1]->x == 6 && box.m_lines[0]->words[1]->style == st12 &&
                          box.m_lines[0]->words[0]->style == newLine);
        ReleaseTextStyle(other);
        ReleaseTextStyle(newLine);
        ReleaseTextStyle(own);
    }
    // --- words: wrapped at the last blank that fits; none on the first line: an empty line, then
    // what fits (no width) and the rest dropped; a style's column; one word when not wrapped ---
    {
        TbWidth(box, 60.0f);
        WM(l.tbWords, TbLayoutRuns(box, {{"ab cd ef", st12}}) && TbLinesText(box) == "[ab cd ef/46 12 18 6 14]");
        TbWidth(box, 30.0f);
        WM(l.tbWords, TbLayoutRuns(box, {{"ab cd ef", st12}}) && TbLinesText(box) == "[ab/12 12 18 6 18][cd ef/29 12 18 6 1]");
        WM(l.tbWords, TbLayoutRuns(box, {{"a b c d e", st12}}) && TbLinesText(box) == "[a b/17 12 18 6 13][c d e/28 12 18 6 2]");
        WM(l.tbWords, TbLayoutRuns(box, {{"abcdefgh", st12}}) && TbLinesText(box) == "[-1 -1 -1 30][abcde/0 12 18 6 30]");
        WM(l.tbWords, TbLayoutRuns(box, {{"", st12}}) && TbLinesText(box) == "[/0 12 18 6 30]");
        // The fix: a blank first on an empty line, too long a word after it.
        WM(l.tbWords, TbLayoutRuns(box, {{" abcdefgh", st12}}) && TbLinesText(box) == "[ abcd/0 12 18 6 30]");
        WM(l.tbWords, TbLayoutRuns(box, {{"ab", st12}, {" cdefghij", st12}}) &&
                          TbLinesText(box) == "[ab/12 12 18 6 18][ cdef/0 12 18 6 30]");
        WM(l.tbWords, TbLayoutRuns(box, {{"abcd  xxxxxxxxx", st12}}) && TbLinesText(box) == "[abcd/24 12 18 6 6][ xxxx/0 12 18 6 30]");
        WM(l.tbWords, TbLayoutRuns(box, {{"abcd  xy", st12}}) && TbLinesText(box) == "[abcd/24 12 18 6 6][ xy/17 12 18 6 13]");
        WM(l.tbWords, TbLayoutRuns(box, {{"ab cdefghijkl", st12}}) && TbLinesText(box) == "[ab/12 12 18 6 18][cdefg/0 12 18 6 30]");
        const int column1 = TbStyle(0.001f, 1, 1);
        WM(l.tbWords, TbLayoutRuns(box, {{"ab cd", column1}}) && TbLinesText(box) == "[-1 -1 -1 30][/0 12 18 6 30]");
        const int tinyWhole = TbStyle(0.0f, 1, 0, 0, -1.0f, -1.0f, 3, 0.001f);
        WM(l.tbWords, TbLayoutRuns(box, {{" ab", tinyWhole}}) && TbLinesText(box) == "[ ab/2 1 18 6 28]");
        ReleaseTextStyle(tinyWhole);
        ReleaseTextStyle(column1);
        TbWidth(box, 12.0f);
        WM(l.tbWords, TbLayoutRuns(box, {{"ab", st12}}) && TbLinesText(box) == "[ab/12 12 18 6 0]");
        TbWidth(box, 11.0f);
        WM(l.tbWords, TbLayoutRuns(box, {{"ab", st12}}) && TbLinesText(box) == "[-1 -1 -1 11][a/0 12 18 6 11]");
        // A tab a blank; a byte over 0x7F not.
        TbWidth(box, 20.0f);
        WM(l.tbWords, TbLayoutRuns(box, {{"ab\tcd", st12}}) && TbLinesText(box) == "[ab/12 12 18 6 8][cd/12 12 18 6 8]");
        WM(l.tbWords, TbLayoutRuns(box, {{"ab\xA0" "cd", st12}}) && TbLinesText(box) == "[-1 -1 -1 20][ab\xA0/0 12 18 6 20]");
        // A column: 20 pixels; wider than the line's room: the room.
        const int column = TbStyle(0.025f, 1, 1);
        TbWidth(box, 60.0f);
        WM(l.tbWords, TbLayoutRuns(box, {{"ab cd", column}}) && TbLinesText(box) == "[ab/12 12 18 6 48][cd/12 12 18 6 48]");
        TbWidth(box, 15.0f);
        WM(l.tbWords, TbLayoutRuns(box, {{"ab cd", column}}) && TbLinesText(box) == "[-1 -1 -1 15][ab/0 12 18 6 15]");
        // Not wrapped: one word (the box's width not looked at), cut to the column (rounded: 20.98, 21.06).
        const int cut = TbStyle(0.025f, 1, 0);
        const int whole = TbStyle(0.0f, 1, 0);
        const int cut20 = TbStyle(0.0256f, 1, 0);
        const int cut21 = TbStyle(0.0257f, 1, 0);
        TbWidth(box, 60.0f);
        WM(l.tbWords, TbLayoutRuns(box, {{"abcdef", cut}}) && TbLinesText(box) == "[abc/18 12 18 6 24]");
        WM(l.tbWords, TbLayoutRuns(box, {{"abcdef", whole}}) && TbLinesText(box) == "[abcdef/36 12 18 6 24]");
        WM(l.tbWords, TbLayoutRuns(box, {{"ab", cut}}) && TbLinesText(box) == "[ab/12 12 18 6 48]");
        WM(l.tbWords, TbLayoutRuns(box, {{"abcdefghijklmn", whole}}) && TbLinesText(box) == "[abcdefghijklmn/81 12 18 6 -21]");
        WM(l.tbWords, TbLayoutRuns(box, {{"iiiiiiii", cut20}}) && TbLinesText(box) == "[iiiiii/18 12 18 6 36]");
        WM(l.tbWords, TbLayoutRuns(box, {{"iiiiiiii", cut21}}) && TbLinesText(box) == "[iiiiiii/21 12 18 6 36]");
        ReleaseTextStyle(cut21);
        ReleaseTextStyle(cut20);
        ReleaseTextStyle(whole);
        ReleaseTextStyle(cut);
        ReleaseTextStyle(column);
    }
    // --- the unicode font's flow: wrapped at the character that does not fit (blanks or not); one
    // wider than an empty line kept on it (the fix) ---
    {
        TbUnicode(true);
        TbWidth(box, 30.0f);
        WM(l.tbChars, TbLayoutRuns(box, {{"abcdefgh", st12}}) && TbLinesText(box) == "[abcde/30 12 18 6 0][fgh/18 12 18 6 12]");
        WM(l.tbChars, TbLayoutRuns(box, {{"ab cd ef", st12}}) && TbLinesText(box) == "[ab cd/29 12 18 6 1][ ef/17 12 18 6 13]");
        WM(l.tbChars, TbLayoutRuns(box, {{"\xD0\x96\xD0\x96\xD0\x96", st12}}) &&
                          TbLinesText(box) == "[\xD0\x96\xD0\x96/24 12 18 6 6][\xD0\x96/12 12 18 6 18]");
        font3.asked.clear();
        WM(l.tbChars, TbLayoutRuns(box, {{"abcd\xC3(x", st12}}) && TbLinesText(box) == "[abcd\xC3/30 12 18 6 0][(x/12 12 18 6 18]" &&
                          std::count(font3.asked.begin(), font3.asked.end(), -61) == 2);
        const int column1 = TbStyle(0.001f, 1, 1);
        WM(l.tbChars, TbLayoutRuns(box, {{"abc", column1}}) && TbLinesText(box) == "[a/6 12 18 6 24][b/6 12 18 6 24][c/6 12 18 6 24]");
        const int tinyWhole = TbStyle(0.0f, 1, 0, 0, -1.0f, -1.0f, 3, 0.001f);
        WM(l.tbChars, TbLayoutRuns(box, {{" ab", tinyWhole}}) && TbLinesText(box) == "[ ab/2 1 18 6 28]");
        ReleaseTextStyle(tinyWhole);
        ReleaseTextStyle(column1);
        WM(l.tbChars, TbLayoutRuns(box, {{"", st12}}) && TbLinesText(box) == "[/0 12 18 6 30]");
        TbWidth(box, 10.0f);
        WM(l.tbChars, TbLayoutRuns(box, {{"Wa", st12}}) && TbLinesText(box) == "[W/12 12 18 6 -2][a/6 12 18 6 4]");
        // Not first on an empty line: an empty word, a new line.
        TbWidth(box, 20.0f);
        WM(l.tbChars, TbLayoutRuns(box, {{"ab", st12}, {"Wc", st12}}) && TbLinesText(box) == "[ab/12 /0 12 18 6 8][Wc/18 12 18 6 2]");
        WM(l.tbChars, TbLayoutRuns(box, {{"abc", st12}}) && TbLinesText(box) == "[abc/18 12 18 6 2]");
        const int column = TbStyle(0.025f, 1, 1);
        const int cut = TbStyle(0.025f, 1, 0);
        const int cut21 = TbStyle(0.0257f, 1, 0);
        TbWidth(box, 60.0f);
        WM(l.tbChars, TbLayoutRuns(box, {{"abcdefgh", column}}) &&
                          TbLinesText(box) == "[abc/18 12 18 6 42][def/18 12 18 6 42][gh/12 12 18 6 48]");
        WM(l.tbChars, TbLayoutRuns(box, {{"a\xD0\x96" "bc", cut}}) && TbLinesText(box) == "[a\xD0\x96/18 12 18 6 30]");
        WM(l.tbChars, TbLayoutRuns(box, {{"ab", cut}}) && TbLinesText(box) == "[ab/12 12 18 6 48]");
        WM(l.tbChars, TbLayoutRuns(box, {{"iiiiiiii", cut21}}) && TbLinesText(box) == "[iiiiiii/21 12 18 6 36]");
        TbWidth(box, 15.0f);
        WM(l.tbChars, TbLayoutRuns(box, {{"abc", column}}) && TbLinesText(box) == "[ab/12 12 18 6 3][c/6 12 18 6 9]");
        ReleaseTextStyle(cut21);
        ReleaseTextStyle(cut);
        ReleaseTextStyle(column);
        TbUnicode(false);
    }
    // --- no layout: no runs, a run's style or font missing (the box stays dirty; not positioned) ---
    {
        TbWidth(box, 60.0f);
        TbLayoutRuns(box, {{"a", st12}, {"b", st12}});
        box.Clear();
        g_gfLog.clear();
        box.m_overflows = true;
        WM(l.tbFail, !box.Layout() && box.m_dirty && box.m_lines.empty() && box.m_pieces.empty() && box.m_overflows);
        const int noFont = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 99);
        WM(l.tbFail, !TbLayoutRuns(box, {{"a", st12}, {"b", noFont}, {"c", st12}}) && box.m_dirty && TbLinesText(box) == "[a/6 12 18 6 54]" &&
                         box.m_overflows);
        WM(l.tbFail, !TbLayoutRuns(box, {{"a", st12}, {"b", 250}}) && box.m_dirty && box.m_lines.size() == 1);
        const int placedNoFont = TbStyle(0.0f, 1, 1, 0, 0.1f, 0.1f, 99);
        WM(l.tbFail, !TbLayoutRuns(box, {{"p", placedNoFont}}) && box.m_pieces.empty() && box.m_dirty);
        WM(l.tbFail, TbLayoutRuns(box, {{"a", st12}}) && !box.m_dirty);
        ReleaseTextStyle(placedNoFont);
        ReleaseTextStyle(noFont);
    }
    // --- a line's measures: its words' largest font size, space above and below (rounded); its room
    // the box's width (truncated) less its words'; a line reset ---
    {
        TbWidth(box, 200.0f);
        const int tall = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 2, 0.04f, 0.05f);
        const int deep = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.02f);
        const int rounded = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.0259f, 0.0251f);
        WM(l.tbMeasures, TbLayoutRuns(box, {{"a", st12}, {"b", tall}, {"c", st12}}) && TbLinesText(box) == "[a/6 b/12 c/6 24 30 6 176]");
        WM(l.tbMeasures, TbLayoutRuns(box, {{"b", tall}, {"a", st12}}) && TbLinesText(box) == "[b/12 a/6 24 30 6 182]");
        WM(l.tbMeasures, TbLayoutRuns(box, {{"a", st12}, {"d", deep}}) && TbLinesText(box) == "[a/6 d/6 12 18 12 188]");
        WM(l.tbMeasures, TbLayoutRuns(box, {{"a", rounded}}) && TbLinesText(box) == "[a/6 12 16 15 194]");
        const int third = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.0255f, 0.0255f, 0.0255f);
        WM(l.tbMeasures, TbLayoutRuns(box, {{"a", third}}) && TbLinesText(box) == "[a/8 15 15 15 192]");
        ReleaseTextStyle(third);
        TbWidth(box, 30.9f);
        WM(l.tbMeasures, TbLayoutRuns(box, {{"a", st12}}) && TbLinesText(box) == "[a/6 12 18 6 24]");
        S_TextLine line;
        line.words.push_back(new S_TextWord{C_str("w")});
        line.y = line.above = line.below = line.gap = line.remaining = 5;
        line.Reset();
        WM(l.tbMeasures, line.words.empty() && line.y == -1 && line.above == -1 && line.below == -1 && line.gap == -1 &&
                             line.remaining == -1);
        ReleaseTextStyle(rounded);
        ReleaseTextStyle(deep);
        ReleaseTextStyle(tall);
    }
    // ===== part 9b-ii-b: the positioning =====
    S_PositionResults q;
    const int own = TbStyle(0.0f, 0, 1);
    const int tall = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 2, 0.04f, 0.05f);
    // --- the lines' height (less the first's space below and the last's above); the vertical
    // alignment's offset (none when the text is higher than the box) ---
    {
        TbRect(box, 0.0f, 0.0f, 100.0f, 100.0f);
        WM(q.tbHeight, TbLayoutRuns(box, {{"a", st12}}) && box.TextHeight() == 12);
        WM(q.tbHeight, TbLayoutRuns(box, {{"a", own}, {"b", own}}) && box.TextHeight() == 48);
        WM(q.tbHeight, TbLayoutRuns(box, {{"a", tall}, {"b", own}}) && box.TextHeight() == 72);
        TbLayoutRuns(box, {{"a", st12}});
        WM(q.tbHeight, box.VAlignOffset(1) == 0.0f && box.VAlignOffset(2) == 44.0f && box.VAlignOffset(3) == 88.0f &&
                           box.VAlignOffset(0) == 0.0f && box.VAlignOffset(4) == 0.0f);
        TbRect(box, 0.0f, 0.0f, 100.0f, 10.0f);
        TbLayoutRuns(box, {{"a", st12}});
        WM(q.tbHeight, box.VAlignOffset(2) == 0.0f && box.VAlignOffset(3) == 0.0f);
        TbRect(box, 0.0f, 0.0f, 100.0f, 100.5f);
        TbLayoutRuns(box, {{"a", st12}});
        WM(q.tbHeight, box.VAlignOffset(2) == 44.25f && box.VAlignOffset(3) == 88.5f);
    }
    // --- the lines stacked from the bottom (truncated) and the first run's vertical alignment up:
    // each base its space below and font size above the last line's top ---
    {
        const int v2 = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 1, 2);
        const int v3 = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 1, 3);
        const int pieceV3 = TbStyle(0.0f, 1, 1, 0, 0.1f, 0.1f, 3, 0.02f, 0.03f, 0.01f, 1, 3);
        TbRect(box, 0.0f, 10.75f, 100.0f, 100.0f);
        WM(q.tbStack, TbLayoutRuns(box, {{"a", own}, {"b", own}}) && TbPlaces(box) == "[28: 0][64: 0]");
        box.StackLines(-5);
        WM(q.tbStack, box.m_lines[0]->y == 13 && box.m_lines[1]->y == 49);
        WM(q.tbStack, TbLayoutRuns(box, {{"a", v3}, {"b", st12}}) && TbPlaces(box) == "[116: 0 6]");
        WM(q.tbStack, TbLayoutRuns(box, {{"a", st12}, {"b", v3}}) && TbPlaces(box) == "[28: 0 6]");
        WM(q.tbStack, TbLayoutRuns(box, {{"p", pieceV3}, {"a", st12}}) && TbPlaces(box) == "[116: 0](10 22)");
        TbRect(box, 0.0f, 10.5f, 100.0f, 100.5f);
        WM(q.tbStack, TbLayoutRuns(box, {{"a", v2}}) && TbPlaces(box) == "[72: 0]");
        TbRect(box, 0.0f, 10.75f, 100.0f, 10.0f);
        WM(q.tbStack, TbLayoutRuns(box, {{"a", v3}}) && TbPlaces(box) == "[28: 0]");
        TbRect(box, 0.0f, 10.75f, 100.0f, 12.6f);
        WM(q.tbStack, TbLayoutRuns(box, {{"a", v2}}) && TbPlaces(box) == "[29: 0]" && box.VAlignOffset(2) > 0.25f);
        ReleaseTextStyle(pieceV3);
        ReleaseTextStyle(v3);
        ReleaseTextStyle(v2);
    }
    // --- a line's words from the left, centred in its room (halved), right; no room: from the left;
    // its first word's style decides; another alignment: not placed ---
    {
        const int centre = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 2);
        const int right = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 3);
        const int wholeCentre = TbStyle(0.0f, 1, 0, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 2);
        const int none = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 0);
        const int five = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 5);
        TbRect(box, 5.0f, 0.0f, 100.0f, 100.0f);
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", st12}, {"cd", st12}}) && TbPlaces(box) == "[18: 5 17]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", centre}}) && TbPlaces(box) == "[18: 49]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", centre}, {"cd", centre}}) && TbPlaces(box) == "[18: 43 55]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", right}, {"cd", right}}) && TbPlaces(box) == "[18: 81 93]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", st12}, {"cd", centre}}) && TbPlaces(box) == "[18: 5 17]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", centre}, {"cd", st12}}) && TbPlaces(box) == "[18: 43 55]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", none}}) && TbPlaces(box) == "[18: -1]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", five}}) && TbPlaces(box) == "[18: -1]");
        TbRect(box, 5.0f, 0.0f, 99.0f, 100.0f);
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", centre}}) && TbPlaces(box) == "[18: 48]");
        TbRect(box, 5.0f, 0.0f, 12.0f, 100.0f);
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", centre}}) && TbPlaces(box) == "[18: 5]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", right}}) && TbPlaces(box) == "[18: 5]");
        TbRect(box, 5.0f, 0.0f, 10.0f, 100.0f);
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", wholeCentre}}) && TbPlaces(box) == "[18: 5]");
        TbRect(box, 5.5f, 0.0f, 100.0f, 100.0f);
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", st12}, {"cd", st12}}) && TbPlaces(box) == "[18: 5 17]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", centre}, {"cd", centre}}) && TbPlaces(box) == "[18: 43 55]");
        WM(q.tbAlign, TbLayoutRuns(box, {{"ab", right}}) && TbPlaces(box) == "[18: 93]");
        // --- the edited line: moved left by what its caret ('_' at its style's size: as wide)
        // lacks ---
        TbRect(box, 5.0f, 0.0f, 18.0f, 100.0f);
        WM(q.tbCaret, TbLayoutRuns(box, {{"ab", centre}}) && TbPlaces(box) == "[18: 8]");
        box.m_editing = true;
        box.m_selection = 0;
        WM(q.tbCaret, TbLayoutRuns(box, {{"ab", centre}}) && TbPlaces(box) == "[18: -1]");
        TbRect(box, 5.0f, 0.0f, 24.0f, 100.0f);
        WM(q.tbCaret, TbLayoutRuns(box, {{"ab", centre}}) && TbPlaces(box) == "[18: 5]");
        TbRect(box, 5.0f, 0.0f, 25.0f, 100.0f);
        WM(q.tbCaret, TbLayoutRuns(box, {{"ab", centre}}) && TbPlaces(box) == "[18: 11]");
        TbRect(box, 5.0f, 0.0f, 15.0f, 100.0f);
        WM(q.tbCaret, TbLayoutRuns(box, {{"a", centre}, {"b", centre}}) && TbPlaces(box) == "[18: -4 2]");
        const int ownCentre = TbStyle(0.0f, 0, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 2);
        TbRect(box, 5.0f, 0.0f, 18.0f, 100.0f);
        box.m_selection = 1;
        WM(q.tbCaret, TbLayoutRuns(box, {{"ab", ownCentre}, {"cd", ownCentre}}) && TbPlaces(box) == "[18: 8][54: -1]");
        const int big = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.04f);
        TbRect(box, 5.0f, 0.0f, 30.0f, 100.0f);
        box.m_selection = 0;
        WM(q.tbCaret, TbLayoutRuns(box, {{"ab", big}}) && TbPlaces(box) == "[30: -13]");
        // (16.3 pixels: 16.)
        const int size16 = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.0271667f);
        WM(q.tbCaret, TbLayoutRuns(box, {{"ab", size16}}) && TbPlaces(box) == "[22: 3]");
        ReleaseTextStyle(size16);
        box.m_editing = false;
        WM(q.tbCaret, TbLayoutRuns(box, {{"ab", big}}) && TbPlaces(box) == "[30: 5]");
        box.m_selection = -1;
        ReleaseTextStyle(big);
        ReleaseTextStyle(ownCentre);
        ReleaseTextStyle(five);
        ReleaseTextStyle(none);
        ReleaseTextStyle(wholeCentre);
        ReleaseTextStyle(right);
        ReleaseTextStyle(centre);
    }
    // --- words at their style's x (of the box's width, from its left; left, centred, right): their
    // line not aligned; after a word of another style: aligned with it ---
    {
        const int at1 = TbStyle(0.0f, 1, 1, STYLE_AT_X, 0.5f, -1.0f);
        const int at2 = TbStyle(0.0f, 1, 1, STYLE_AT_X, 0.5f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 2);
        const int at3 = TbStyle(0.0f, 1, 1, STYLE_AT_X, 0.5f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 3);
        const int at4 = TbStyle(0.0f, 1, 1, STYLE_AT_X, 0.5f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 4);
        const int atThird = TbStyle(0.0f, 1, 1, STYLE_AT_X, 0.333f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 2);
        TbRect(box, 5.0f, 0.0f, 100.0f, 100.0f);
        WM(q.tbAtX, TbLayoutRuns(box, {{"ab", at1}}) && TbPlaces(box) == "[18: 55]");
        WM(q.tbAtX, TbLayoutRuns(box, {{"ab", at2}}) && TbPlaces(box) == "[18: 49]");
        WM(q.tbAtX, TbLayoutRuns(box, {{"ab", at3}}) && TbPlaces(box) == "[18: 43]");
        WM(q.tbAtX, TbLayoutRuns(box, {{"ab", at4}}) && TbPlaces(box) == "[18: -1]");
        WM(q.tbAtX, TbLayoutRuns(box, {{"a", atThird}}) && TbPlaces(box) == "[18: 35]");
        WM(q.tbAtX, TbLayoutRuns(box, {{"ab", at1}, {"cd", st12}}) && TbPlaces(box) == "[18: 55 -1]");
        WM(q.tbAtX, TbLayoutRuns(box, {{"ab", st12}, {"cd", at1}}) && TbPlaces(box) == "[18: 5 17]");
        ReleaseTextStyle(atThird);
        ReleaseTextStyle(at4);
        ReleaseTextStyle(at3);
        ReleaseTextStyle(at2);
        ReleaseTextStyle(at1);
    }
    // --- justified: a paragraph's lines but its last (the next line's first word justified) split
    // at their blanks, the box's width less the words' shared between them ---
    {
        const int just = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 4);
        const int justNew = TbStyle(0.0f, 1, 1, STYLE_NEW_LINE, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 4);
        const int leftNew = TbStyle(0.0f, 1, 1, STYLE_NEW_LINE);
        const int fiveNew = TbStyle(0.0f, 1, 1, STYLE_NEW_LINE, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 5);
        TbRect(box, 5.0f, 0.0f, 40.0f, 100.0f);
        WM(q.tbJustify, TbLayoutRuns(box, {{"ab cd ef gh", just}}) && TbPlaces(box) == "[18: 5 33][54: 5]" &&
                            TbWordsText(*box.m_lines[0]) == "ab/12 cd/12 " && TbWordsText(*box.m_lines[1]) == "ef gh/29 ");
        TbRect(box, 5.0f, 0.0f, 41.0f, 100.0f);
        WM(q.tbJustify, TbLayoutRuns(box, {{"ab cd ef gh", just}}) && TbPlaces(box) == "[18: 5 34][54: 5]");
        TbRect(box, 5.0f, 0.0f, 40.0f, 100.0f);
        WM(q.tbJustify, TbLayoutRuns(box, {{"ab cd", just}, {"x", leftNew}}) && TbPlaces(box) == "[18: 5][54: 5]" &&
                            TbWordsText(*box.m_lines[0]) == "ab cd/29 ");
        WM(q.tbJustify, TbLayoutRuns(box, {{"ab cd", just}, {"x", fiveNew}}) && TbPlaces(box) == "[18: 5 33][54: -1]");
        WM(q.tbJustify, TbLayoutRuns(box, {{"abcd", just}, {"x", justNew}}) && TbPlaces(box) == "[18: 5][54: 5]" &&
                            TbWordsText(*box.m_lines[0]) == "abcd/24 ");
        WM(q.tbJustify, TbLayoutRuns(box, {{"", just}, {"x", justNew}}) && TbPlaces(box) == "[18:][54: 5]");
        TbRect(box, 5.0f, 0.0f, 29.0f, 100.0f);
        WM(q.tbJustify, TbLayoutRuns(box, {{"ab cd", just}, {"x", justNew}}) && TbPlaces(box) == "[18: 5][54: 5]" &&
                            TbWordsText(*box.m_lines[0]) == "ab cd/29 ");
        // Two words: the last kept first (the quirk), the gap a third of the room.
        TbRect(box, 5.0f, 0.0f, 100.0f, 100.0f);
        const float gap = (100.0f - 48.0f) / 3.0f;
        const float x1 = (12.0f + gap) + 5.0f, x2 = (12.0f + gap) + x1, x3 = (12.0f + gap) + x2;
        WM(q.tbJustify, TbLayoutRuns(box, {{"ab cd", just}, {"ef", just}, {"gh", justNew}}) &&
                            TbWordsText(*box.m_lines[0]) == "ef/12 ab/12 cd/12 ef/12 " &&
                            TbPlaces(box) == CmText("[18: 5 %d %d %d][54: 5]", static_cast<int>(x1), static_cast<int>(x2),
                                                    static_cast<int>(x3)) &&
                            static_cast<int>(x1) == 34 && static_cast<int>(x2) == 63);
        ReleaseTextStyle(fiveNew);
        ReleaseTextStyle(leftNew);
        ReleaseTextStyle(justNew);
        ReleaseTextStyle(just);
    }
    // --- a line's words split at their blanks (the blanks' advances asked for too; a part after the
    // last blank kept with a width); of more than one word the last stays first; a word's style
    // missing: no more ---
    {
        S_TextLine line;
        const int tiny = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.001f);
        const int big = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.04f);
        TbWords(line, {{"ab cd", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab/12 cd/12 " && line.words[1]->style == st12 && line.words[1]->x == -1);
        TbWords(line, {{"ab  cd ", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab/12 /0 cd/12 ");
        TbWords(line, {{" ab", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "/0 ab/12 ");
        TbWords(line, {{"ab i", tiny}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab/2 ");
        TbWords(line, {{"ab", st12}, {"c d", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "c d/1 ab/12 c/6 d/6 ");
        TbWords(line, {{"a", st12}, {"b", st12}, {"c", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "c/1 a/6 b/6 c/6 ");
        TbWords(line, {{"ab cd", 250}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab cd/1 ");
        TbWords(line, {{"ab cd", st12}, {"x", 250}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab cd/1 x/1 ab/12 cd/12 ");
        TbWords(line, {{"ab cd", big}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab/24 cd/24 ");
        TbWords(line, {{"ab\tcd", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab/12 cd/12 ");
        TbWords(line, {{"ab\xA0" "cd", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab\xA0" "cd/30 ");
        TbWords(line, {{"a b", st12}});
        font3.asked.clear();
        box.SplitWords(&line);
        WM(q.tbSplit, (font3.asked == std::vector<int>{'a', 'a', ' ', 'b', 'b'}));
        TbWords(line, {});
        box.SplitWords(&line);
        WM(q.tbSplit, line.words.empty());
        TbUnicode(true);
        TbWords(line, {{"ab\xD0\x96 cd", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab\xD0\x96/24 cd/12 ");
        TbWords(line, {{"\xC3 x", st12}});
        font3.asked.clear();
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "\xC3/6 x/6 " && font3.asked.size() == 5 && font3.asked[0] == -61);
        TbWords(line, {{"ab cd", st12}});
        box.SplitWords(&line);
        WM(q.tbSplit, TbWordsText(line) == "ab/12 cd/12 ");
        TbUnicode(false);
        ReleaseTextStyle(big);
        ReleaseTextStyle(tiny);
    }
    // --- pieces at their styles' x and y of the box's size (rounded; not offset by its place),
    // aligned by their styles: left, centred, right; top, middle, bottom ---
    {
        auto piece = [](int align, int valign) {
            return TbStyle(0.0f, 1, 1, 0, 0.1f, 0.2f, 3, 0.02f, 0.03f, 0.01f, align, valign);
        };
        const int p11 = piece(1, 1), p21 = piece(2, 1), p31 = piece(3, 1), p12 = piece(1, 2), p13 = piece(1, 3);
        const int p00 = piece(0, 0);
        const int corner = TbStyle(0.0f, 1, 1, 0, 0.0f, 0.0f);
        TbRect(box, 50.0f, 30.0f, 200.0f, 100.0f);
        WM(q.tbPieceAt, TbLayoutRuns(box, {{"ab", p11}, {"ab", p21}, {"ab", p31}, {"ab", p12}, {"ab", p13}, {"ab", p00}, {"ab", corner}}) &&
                            TbPlaces(box) == "(20 20)(14 20)(8 20)(20 26)(20 32)(-1 -1)(0 0)");
        TbRect(box, 50.0f, 30.0f, 203.0f, 101.5f);
        WM(q.tbPieceAt, TbLayoutRuns(box, {{"ab", p11}}) && TbPlaces(box) == "(20 20)");
        TbRect(box, 50.0f, 30.0f, 200.0f, 100.0f);
        // --- overflows: a line without room, a piece outside the box ---
        WM(q.tbOverflow, TbLayoutRuns(box, {{"ab", p11}}) && !box.m_overflows);
        WM(q.tbOverflow, TbLayoutRuns(box, {{"ab", p00}}) && box.m_overflows);
        const int farRight = TbStyle(0.0f, 1, 1, 0, 0.99f, 0.0f);
        WM(q.tbOverflow, TbLayoutRuns(box, {{"ab", farRight}}) && box.m_overflows);
        S_TextPiece pc;
        pc.width = pc.height = 12;
        const auto check = [&](int x, int y) {
            pc.x = static_cast<int16_t>(x);
            pc.y = static_cast<int16_t>(y);
            box.m_overflows = false;
            return box.CheckPieceOverflow(&pc);
        };
        WM(q.tbOverflow, !check(0, 0) && check(-1, 0) && check(0, -1) && !check(0, 88) && check(0, 89) && !check(188, 0) &&
                             check(189, 0));
        TbRect(box, 0.0f, 0.0f, 200.5f, 100.5f);
        WM(q.tbOverflow, check(0, 89) && !check(0, 88) && check(189, 0) && !check(188, 0));
        box.m_overflows = true;
        pc.x = pc.y = 0;
        WM(q.tbOverflow, box.CheckPieceOverflow(&pc));
        S_TextLine line;
        box.m_overflows = false;
        line.remaining = 0;
        WM(q.tbOverflow, !box.CheckLineOverflow(&line));
        line.remaining = -1;
        WM(q.tbOverflow, box.CheckLineOverflow(&line));
        line.remaining = 5;
        WM(q.tbOverflow, box.CheckLineOverflow(&line));
        const int whole = TbStyle(0.0f, 1, 0);
        TbRect(box, 0.0f, 0.0f, 60.0f, 100.0f);
        WM(q.tbOverflow, TbLayoutRuns(box, {{"abcdefghijklmn", whole}}) && box.m_overflows);
        WM(q.tbOverflow, TbLayoutRuns(box, {{"ab", whole}}) && !box.m_overflows);
        ReleaseTextStyle(whole);
        ReleaseTextStyle(farRight);
        ReleaseTextStyle(corner);
        ReleaseTextStyle(p00);
        ReleaseTextStyle(p13);
        ReleaseTextStyle(p12);
        ReleaseTextStyle(p31);
        ReleaseTextStyle(p21);
        ReleaseTextStyle(p11);
    }
    // --- the positioning: the flag cleared first, the scroll's range after (its flag) ---
    {
        const int place = TbStyle(0.0f, 1, 1, 0, 0.1f, 0.2f);
        TbRect(box, 0.0f, 0.0f, 100.0f, 100.0f);
        box.m_scrollRange = 77;
        WM(q.tbPosition, TbLayoutRuns(box, {{"a", own}, {"b", own}, {"c", own}, {"d", own}}) && box.m_scrollRange == 28 &&
                             box.m_overflows && TbPlaces(box) == "[18: 0][54: 0][90: 0][126: 0]");
        WM(q.tbPosition, TbLayoutRuns(box, {{"a", own}, {"b", own}, {"c", own}}) && box.m_scrollRange == 0 && !box.m_overflows);
        box.m_scrollRange = 77;
        WM(q.tbPosition, TbLayoutRuns(box, {{"ab", place}}) && box.m_scrollRange == 0 && TbPlaces(box) == "(10 20)");
        box.FreeLayout();
        box.m_overflows = true;
        WM(q.tbPosition, box.Position() && !box.m_overflows);
        ReleaseTextStyle(place);
    }
    ReleaseTextStyle(tall);
    ReleaseTextStyle(own);
    std::printf("position: tbHeight %d tbStack %d tbAlign %d tbCaret %d tbAtX %d tbJustify %d tbSplit %d tbPieceAt %d "
                "tbOverflow %d tbPosition %d\n",
                q.tbHeight, q.tbStack, q.tbAlign, q.tbCaret, q.tbAtX, q.tbJustify, q.tbSplit, q.tbPieceAt, q.tbOverflow,
                q.tbPosition);
    ReleaseTextStyle(st12);
    std::printf("layout: tbStyle %d tbAdvance %d tbWidth %d tbUtf8 %d tbTruncate %d tbPieces %d tbLines %d tbWords %d tbChars %d "
                "tbFail %d tbMeasures %d\n",
                l.tbStyle, l.tbAdvance, l.tbWidth, l.tbUtf8, l.tbTruncate, l.tbPieces, l.tbLines, l.tbWords, l.tbChars, l.tbFail,
                l.tbMeasures);

    box.m_fonts->m_fonts.erase(box.m_fonts->m_fonts.begin(), box.m_fonts->m_fonts.begin() + 2);
    g_textIds = oldIds;
    g_igraphVtable[0x18 / 4] = oldX;
    g_igraphVtable[0x1C / 4] = oldY;
    std::printf("textbox: tbNew %d tbRuns %d tbVersion %d tbInsert %d tbRemove %d tbEdit %d tbCaret %d tbSelect %d tbHover %d "
                "tbRange %d tbScrollTo %d tbCentre %d tbRect %d tbAttach %d tbDepth %d tbFree %d\n",
                r.tbNew, r.tbRuns, r.tbVersion, r.tbInsert, r.tbRemove, r.tbEdit, r.tbCaret, r.tbSelect, r.tbHover, r.tbRange,
                r.tbScrollTo, r.tbCentre, r.tbRect, r.tbAttach, r.tbDepth, r.tbFree);
}
