// Part 9b's text box (game/text_box.cpp: 9b-i its runs, edit, selection, hover, scroll and place;
// 9b-ii its layout and positioning; 9b-iii its render; 9b-iv the paged and the video text boxes,
// game/paged_text_box.cpp and game/video_text_box.cpp), in the text build (build_text.bat: the camera
// build with TEXT_TEST and the real C_TextBox; the loader's text box fakes left out), run by
// CameraTest instead of the camera's tests: IGraph's screen 800 x 600; the fonts 2 and 3 fakes
// (C_TbFont); the render's IGraph and driver calls logged. Prints "textbox:" (9b-i), "layout:"
// (9b-ii-a), "position:" (9b-ii-b), "render:" (9b-iii), "pages:" (9b-iv-a) and "paged:" (9b-iv-b, the
// paged box's layout) with 1 per check met.

#define private public
#define protected public
#include "game/paged_text_box.h"
#include "game/video_text_box.h"
#undef protected
#undef private

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

// A font whose glyphs' aspects are its table's (others 0.5), the codes asked for kept; its texture
// corners 0.125, 0.25, 0.5, 0.75. Logging: its quads and its other calls (the render's tests).
struct C_TbFont : C_Font {
    std::vector<int> asked;
    S_Glyph glyph = {0.125f, 0.25f, 0.5f, 0.75f, 0.0f};
    bool logging = false;
    float caretAspect = 1.0f;                           // '_'
    void GrowVertices() override {}
    void Flush() override
    {
        if (logging)
            GfLog("flush %d", m_id);
    }
    void ClearVertices() override
    {
        if (logging)
            GfLog("clear %d", m_id);
    }
    void Select() override
    {
        if (logging)
            GfLog("select %d", m_id);
    }
    const S_Glyph *GetGlyph(int ch) override
    {
        asked.push_back(ch);
        switch (ch) {
        case ' ':
            glyph.aspect = 0.0f;
            break;
        case 'W':
        case 0x416:                                     // (Cyrillic Zhe)
            glyph.aspect = 1.0f;
            break;
        case '_':                                       // (the caret)
            glyph.aspect = caretAspect;
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
    void AddQuad(int ch, uint32_t color, int width, int height, int x, int y, float z) override
    {
        if (logging)
            GfLog("quad %d %08x %d %d %d %d %g", ch, color, width, height, x, y, z);
    }
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

// --- part 9b-iii ---

struct S_RenderResults {
    bool tbPlaced = true, tbGlyphs = true, tbBlank = true, tbFlush = true, tbBatch = true, tbColour = true;
    bool tbCentred = true, tbCaret = true, tbPieces = true, tbFail = true, tbClip = true, tbBackground = true;
    bool tbOutline = true, tbUtf8 = true, tbStep = true, tbFonts = true;
};

// IGraph's viewport (the screen's), the last primitives drawn, the driver's states.
S_IGraphViewport g_trScreen = {0, 0, 800, 600, 0.0f, 1.0f};
std::vector<S_FontVertex> g_trVertices;
uint32_t g_trStates[16];
const S_IGraphViewport *__stdcall F_TrGetViewport(void *)
{
    GfLog("getviewport");
    return &g_trScreen;
}
void __stdcall F_TrSetViewport(void *, const S_IGraphViewport *v)
{
    GfLog("viewport %d %d %d %d %g %g", v->x, v->y, v->width, v->height, v->minZ, v->maxZ);
}
void __stdcall F_TrSetTexture(void *, void *texture, int stage) { GfLog("texture %d %d", texture != nullptr, stage); }
void __stdcall F_TrDraw(void *, int kind, int count, const void *vertices, int transformed)
{
    const int n = kind == 3 ? count * 3 : count + 2;
    const auto *v = static_cast<const S_FontVertex *>(vertices);
    g_trVertices.assign(v, v + n);
    GfLog("draw %d %d %d", kind, count, transformed);
}
uint32_t __stdcall F_TrGetState(void *, int state)
{
    GfLog("get %d", state);
    return g_trStates[state];
}
void __stdcall F_TrSetState(void *, int state, uint32_t value) { GfLog("set %d %u", state, value); }
int __stdcall F_TrScrnX(void *)
{
    GfLog("scrnx");
    return 800;
}
int __stdcall F_TrScrnY(void *)
{
    GfLog("scrny");
    return 600;
}
// An overflowing render's log: the screen's viewport read, the clip set, `inner`, the screen's set back.
std::vector<std::string> TrClipped(const char *clip, std::vector<std::string> inner,
                                   const char *screen = "viewport 0 0 800 600 0 1")
{
    std::vector<std::string> log = {"getviewport", "scrnx", "scrny", "scrnx", "scrny", clip};
    log.insert(log.end(), inner.begin(), inner.end());
    log.insert(log.end(), {"scrnx", "scrny", screen});
    return log;
}

// The last primitives' vertices: "(x y u v)" each.
std::string TrVertices()
{
    std::string s;
    for (const S_FontVertex &v : g_trVertices)
        s += CmText("(%g %g %g %g)", v.x, v.y, v.u, v.v);
    return s;
}
// Every vertex of the last primitives with this colour, depth and 1 / depth.
bool TrUniform(uint32_t color, float z, float rhw)
{
    for (const S_FontVertex &v : g_trVertices)
        if (v.color != color || v.z != z || v.rhw != rhw)
            return false;
    return !g_trVertices.empty();
}
// A glyph's two triangles at the pen (x, y), `width` wide, `size` high.
std::string TrQuad(int x, int y, int width, int size)
{
    const int r = x + width, t = y - size;
    return CmText("(%d %d 0.125 0.75)(%d %d 0.125 0.25)(%d %d 0.5 0.25)(%d %d 0.5 0.75)(%d %d 0.125 0.75)(%d %d 0.5 0.25)",
                  x, y, x, t, r, t, r, y, x, y, r, t);
}
// A style of font 3 with this colour and flags (`size` of the height: 0.02, 12 pixels).
int TrStyle(uint32_t color, uint32_t flags = 0, float size = 0.02f)
{
    const S_TextStyleDesc desc = {3, color, size, 0.0f, -1.0f, -1.0f, 0.01f, 0.03f, 0, 1, 1, 1, 1, flags};
    return CreateTextStyle(desc);
}
// The box's layout replaced: no lines or pieces, laid out, placed, not overflowing; the log cleared.
void TrClear(C_TextBox &box)
{
    box.FreeLayout();
    box.m_dirty = false;
    box.m_placed = true;
    box.m_overflows = false;
    g_gfLog.clear();
    g_trVertices.clear();
}
S_TextLine *TrLine(C_TextBox &box, int y, int gap = 12)
{
    auto *line = new S_TextLine();
    line->y = static_cast<int16_t>(y);
    line->gap = static_cast<int16_t>(gap);
    box.m_lines.push_back(line);
    return line;
}
void TrWord(S_TextLine *line, const char *text, int style, int x)
{
    line->words.push_back(new S_TextWord{C_str(text), static_cast<uint8_t>(style), 0, static_cast<int16_t>(x)});
}
void TrPiece(C_TextBox &box, const char *text, int style, int x, int y)
{
    auto *piece = new S_TextPiece();
    piece->text = C_str(text);
    piece->style = static_cast<uint8_t>(style);
    piece->x = static_cast<int16_t>(x);
    piece->y = static_cast<int16_t>(y);
    box.m_pieces.push_back(piece);
}

// --- part 9b-iv ---

struct S_PagesResults {
    bool pgNew = true, pgArea = true, pgPage = true, pgRelayout = true, pgBreak = true, pgRender = true;
    bool pgLines = true, pgColour = true, pgStop = true, pgClip = true, vtNew = true, vtLines = true;
    bool vtFirst = true, vtStop = true, vtFade = true, vtScroll = true, vtFinished = true, vtVirtual = true;
};

// The fans drawn logged with their corners "(x y u v colour)"; the triangles as F_TrDraw.
void __stdcall F_PgDraw(void *, int kind, int count, const void *vertices, int transformed)
{
    if (kind != 4)
        return F_TrDraw(nullptr, kind, count, vertices, transformed);
    const auto *v = static_cast<const S_FontVertex *>(vertices);
    std::string s = CmText("fan %d %d", count, transformed);
    for (int i = 0; i < 4; ++i)
        s += CmText(" (%g %g %g %g %08x)", v[i].x, v[i].y, v[i].u, v[i].v, v[i].color);
    GfLog("%s", s.c_str());
}
// The fades' log: the texture, the top fan `fade` high, the bottom one from `height` less it.
std::vector<std::string> PgFades(int width, int height, int fade)
{
    return {"texture 0 0",
            CmText("fan 2 1 (0 0 0 0 ff000000) (%d 0 1 0 ff000000) (%d %d 0 1 00000000) (0 %d 1 1 00000000)", width, width,
                   fade, fade),
            CmText("fan 2 1 (0 %d 0 0 00000000) (%d %d 1 0 00000000) (%d %d 0 1 ff000000) (0 %d 1 1 ff000000)", height - fade,
                   width, height - fade, width, height, height)};
}
// A page of the paged box in area `area`.
S_TextPage *PgPage(C_PagedTextBox &box, int area, bool overflows = false)
{
    auto *page = new S_TextPage();
    page->area = area;
    page->overflows = overflows;
    box.m_pages.push_back(page);
    return page;
}
S_TextLine *PgLine(S_TextPage *page, int y)
{
    auto *line = new S_TextLine();
    line->y = static_cast<int16_t>(y);
    page->lines.push_back(line);
    return line;
}
// The paged box's pages deleted; laid out, placed, at page 0; the log and the codes asked cleared.
void PgClear(C_PagedTextBox &box, C_TbFont &font)
{
    for (S_TextPage *page : box.m_pages)
        delete page;
    box.m_pages.clear();
    box.m_dirty = false;
    box.m_placed = true;
    box.m_page = 0;
    g_gfLog.clear();
    g_trVertices.clear();
    font.asked.clear();
}
// The area's viewport: "x y width height minZ maxZ" (false: "- " first).
std::string PgClip(const C_PagedTextBox &box, S_TextArea area)
{
    S_IGraphViewport clip = {7, 7, 7, 7, 7.0f, 7.0f};
    const bool on = box.ClipArea(&clip, area);
    return CmText("%s%d %d %d %d %g %g", on ? "" : "- ", clip.x, clip.y, clip.width, clip.height, clip.minZ, clip.maxZ);
}

// --- part 9b-iv-b ---

struct S_PagedResults {
    bool lpNone = true, lpLines = true, lpWrap = true, lpPages = true, lpAreas = true, lpForced = true, lpFail = true;
    bool lpNoWrap = true, lpMeasures = true, lpJoins = true, lpBreak = true, lpStack = true, lpAlign = true;
    bool lpJustify = true, lpNext = true, lpOverflow = true, lpAppend = true, lpUtf8 = true;
};

// An area at (left, bottom), `width` by `height` (depth 0.5).
S_TextArea LpRect(float left, float bottom, float width, float height)
{
    return {left, bottom + height, left + width, bottom, width, height, 0.5f};
}
// The pages: "[area room(! overflowing):" then per line " (y above below gap remaining" and its words
// " text/width@x", ")".
std::string LpPages(const C_PagedTextBox &box)
{
    std::string s;
    for (const S_TextPage *p : box.m_pages) {
        s += CmText("[%d %d%s:", p->area, p->room, p->overflows ? "!" : "");
        for (const S_TextLine *l : p->lines) {
            s += CmText(" (%d %d %d %d %d", l->y, l->above, l->below, l->gap, l->remaining);
            for (const S_TextWord *w : l->words)
                s += CmText(" %s/%d@%d", w->text.c_str(), w->width, w->x);
            s += ")";
        }
        s += "]";
    }
    return s;
}
// The box's pages deleted, its runs these, laid out (LayoutPages' result).
bool LpLayout(C_PagedTextBox &box, std::initializer_list<std::pair<const char *, int>> runs)
{
    for (S_TextPage *page : box.m_pages)
        delete page;
    box.m_pages.clear();
    box.Clear();
    for (const auto &r : runs)
        box.AddText(C_str(r.first), static_cast<uint8_t>(r.second));
    return box.LayoutPages();
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
        WM(l.tbAdvance, box.Advance('a', 12) == 9 && box.Advance(' ', 12) == 9 && (font3.asked == (std::vector<int>{'*', '*'})) &&
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

    // ===== part 9b-iii: the render =====
    S_RenderResults v;
    {
        void *const oldGraph[] = {g_igraphVtable[0x44 / 4], g_igraphVtable[0x7C / 4], g_igraphVtable[0x80 / 4],
                                  g_igraphVtable[0x88 / 4]};
        void *const oldDriver[] = {g_driverVtable[0x18 / 4], g_driverVtable[0x1C / 4]};
        g_igraphVtable[0x44 / 4] = reinterpret_cast<void *>(&F_TrSetTexture);
        g_igraphVtable[0x7C / 4] = reinterpret_cast<void *>(&F_TrSetViewport);
        g_igraphVtable[0x80 / 4] = reinterpret_cast<void *>(&F_TrGetViewport);
        g_igraphVtable[0x88 / 4] = reinterpret_cast<void *>(&F_TrDraw);
        g_driverVtable[0x18 / 4] = reinterpret_cast<void *>(&F_TrSetState);
        g_driverVtable[0x1C / 4] = reinterpret_cast<void *>(&F_TrGetState);
        g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&F_TrScrnX);
        g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&F_TrScrnY);
        font3.logging = true;
        const uint32_t white = 0xFFFFFFFF, grey = 0xAABBCCDD;
        const int st = TrStyle(grey);
        // The box: left 10, bottom 20, 200 by 100 (top 120), depth 0.5.
        TbRect(box, 10.0f, 20.0f, 200.0f, 100.0f);
        box.m_scroll = 0;
        box.m_hover = -1;
        box.m_editing = false;
        box.m_caretOn = false;

        // --- nothing before the box is placed; laid out first when dirty (the fonts' batching taken) ---
        {
            C_TextBox fresh;
            fresh.AddText(C_str("ab"), static_cast<uint8_t>(st));
            g_gfLog.clear();
            fresh.Render();
            WM(v.tbPlaced, g_gfLog.empty() && fresh.m_dirty && fresh.m_lines.empty());
            TbRect(fresh, 10.0f, 20.0f, 200.0f, 100.0f);
            fresh.m_batching = true;
            g_gfLog.clear();
            fresh.Render();
            WM(v.tbPlaced, !fresh.m_dirty && !fresh.m_batching && fresh.m_lines.size() == 1 &&
                               std::count(g_gfLog.begin(), g_gfLog.end(), "draw 3 4 1") == 1);
        }

        // --- a word's glyphs: two triangles each at the pen (the line's base less the scroll) ---
        {
            TrClear(box);
            TrWord(TrLine(box, 50), "ab", st, 30);
            box.Render();
            WM(v.tbGlyphs, GfLogIs({"select 3", "draw 3 4 1"}) && TrVertices() == TrQuad(30, 50, 6, 12) + TrQuad(36, 50, 6, 12) &&
                               TrUniform(grey, 0.5f, 2.0f) && box.m_penX == 42 && box.m_penY == 50);
            box.m_scroll = 5;
            TrClear(box);
            TrWord(TrLine(box, 50), "aW", st, 30);
            TrWord(TrLine(box, 80), "i", st, 7);
            box.Render();
            WM(v.tbGlyphs, GfLogIs({"select 3", "draw 3 4 1", "select 3", "draw 3 2 1"}) && TrVertices() == TrQuad(7, 75, 3, 12) &&
                               box.m_penX == 10 && box.m_penY == 75);
            box.m_scroll = 0;
            // (The depth 0.3: 1 / 0.3.)
            const float rect[7] = {10.0f, 120.0f, 210.0f, 20.0f, 200.0f, 100.0f, 0.3f};
            box.SetRect(rect);
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            box.Render();
            WM(v.tbGlyphs, TrUniform(grey, 0.3f, 1.0f / 0.3f));
            TbRect(box, 10.0f, 20.0f, 200.0f, 100.0f);
            // (The bytes unsigned.)
            font3.asked.clear();
            TrClear(box);
            TrWord(TrLine(box, 50), "\xFF", st, 30);
            box.Render();
            WM(v.tbGlyphs, font3.asked == std::vector<int>{255} && GfLogIs({"select 3", "draw 3 2 1"}));
        }

        // --- a blank: the pen moved 0.4 of the size (rounded), nothing drawn ---
        {
            TrClear(box);
            TrWord(TrLine(box, 50), "a b", st, 30);
            box.Render();
            WM(v.tbBlank, GfLogIs({"select 3", "draw 3 4 1"}) && TrVertices() == TrQuad(30, 50, 6, 12) + TrQuad(41, 50, 6, 12) &&
                              box.m_penX == 47);
            // (27 pixels: 10.8, 11.)
            const int big = TrStyle(grey, 0, 27.0f / 600.0f);
            TrClear(box);
            TrWord(TrLine(box, 50), " ", big, 30);
            box.Render();
            WM(v.tbBlank, g_gfLog.empty() && box.m_penX == 41);
            ReleaseTextStyle(big);
            TrClear(box);
            TrWord(TrLine(box, 50), "", st, 30);
            box.Render();
            WM(v.tbBlank, g_gfLog.empty() && box.m_penX == 30);
        }

        // --- 32 glyphs drawn at once, the rest after ---
        {
            TrClear(box);
            TrWord(TrLine(box, 50), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", st, 0);
            box.Render();
            WM(v.tbFlush, GfLogIs({"select 3", "draw 3 64 1", "select 3", "draw 3 2 1"}) && TrVertices() == TrQuad(192, 50, 6, 12));
            TrClear(box);
            TrWord(TrLine(box, 50), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", st, 0);
            box.Render();
            WM(v.tbFlush, GfLogIs({"select 3", "draw 3 64 1"}));
            TrClear(box);
            TrWord(TrLine(box, 50), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa ", st, 0);
            box.Render();
            WM(v.tbFlush, GfLogIs({"select 3", "draw 3 64 1"}) && box.m_penX == 197);
        }

        // --- the fonts batching: the quads the font's (not drawn) ---
        {
            g_gfLog.clear();
            font2.logging = true;
            SetFontBatching(true);
            font2.logging = false;
            WM(v.tbFonts, g_fontManager->IsBatching() && GfLogIs({"clear 2", "clear 3"}));
            TrClear(box);
            TrWord(TrLine(box, 50), "ab", st, 30);
            box.Render();
            WM(v.tbBatch, box.m_batching && GfLogIs({"quad 97 aabbccdd 6 12 30 50 0.5", "quad 98 aabbccdd 6 12 36 50 0.5"}) &&
                              box.m_penX == 42);
            TrClear(box);
            TrWord(TrLine(box, 50), "a b", st, 30);
            box.Render();
            WM(v.tbBatch, GfLogIs({"quad 97 aabbccdd 6 12 30 50 0.5", "quad 98 aabbccdd 6 12 41 50 0.5"}));
            box.m_editing = true;
            box.m_selection = 0;
            box.m_caretOn = true;
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            box.Render();
            WM(v.tbBatch, GfLogIs({"quad 97 aabbccdd 6 12 30 50 0.5", "quad 95 aabbccdd 12 12 36 48 0.5"}) && box.m_penX == 48 &&
                              box.m_penY == 50);
            box.m_editing = false;
            box.m_caretOn = false;
            g_gfLog.clear();
            SetFontBatching(false);
            WM(v.tbFonts, !g_fontManager->IsBatching() && g_gfLog.empty());
            font2.logging = true;
            FlushFonts();
            font2.logging = false;
            WM(v.tbFonts, GfLogIs({"flush 2", "flush 3"}));
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            box.Render();
            WM(v.tbBatch, !box.m_batching && GfLogIs({"select 3", "draw 3 2 1"}));
        }

        // --- the colours: the box's; dimmed (alpha 0x60); the hovered line's white; the style's ---
        {
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            box.m_colour = 0x11223344;
            box.m_dimmed = true;
            box.m_hover = 0;
            box.Render();
            WM(v.tbColour, TrUniform(0x11223344, 0.5f, 2.0f));
            box.m_colour = 0;
            box.Render();
            WM(v.tbColour, TrUniform(0x60BBCCDD, 0.5f, 2.0f));
            box.m_dimmed = false;
            box.Render();
            WM(v.tbColour, TrUniform(white, 0.5f, 2.0f));
            box.m_hover = 1;
            box.Render();
            WM(v.tbColour, TrUniform(grey, 0.5f, 2.0f));
            const int black = TrStyle(0x00000000);
            TrClear(box);
            TrWord(TrLine(box, 50), "a", black, 30);
            box.m_dimmed = true;
            box.Render();
            WM(v.tbColour, TrUniform(0x60000000, 0.5f, 2.0f));
            box.m_dimmed = false;
            box.m_hover = -1;
            ReleaseTextStyle(black);
        }

        // --- a centred word: up by half its line's gap over its size (truncated), the next not ---
        {
            const int centred = TrStyle(grey, STYLE_CENTRED);
            TrClear(box);
            S_TextLine *line = TrLine(box, 50, 20);
            TrWord(line, "a", centred, 30);
            TrWord(line, "b", st, 40);
            box.Render();
            WM(v.tbCentred, GfLogIs({"select 3", "draw 3 2 1", "select 3", "draw 3 2 1"}) && TrVertices() == TrQuad(40, 50, 6, 12));
            g_gfLog.clear();
            delete line->words.back();
            line->words.pop_back();
            box.Render();
            WM(v.tbCentred, TrVertices() == TrQuad(30, 46, 6, 12) && box.m_penY == 50);
            line->gap = 15;
            box.Render();
            WM(v.tbCentred, TrVertices() == TrQuad(30, 49, 6, 12));
            line->gap = 9;
            box.Render();
            WM(v.tbCentred, TrVertices() == TrQuad(30, 51, 6, 12));
            line->gap = 12;
            box.m_scroll = 3;
            box.Render();
            WM(v.tbCentred, TrVertices() == TrQuad(30, 47, 6, 12) && box.m_penY == 47);
            box.m_scroll = 0;
            ReleaseTextStyle(centred);
        }

        // --- the caret on the edited run's line ('_' 0.1 of the size under the base, truncated), in the
        // last word's colour ---
        {
            box.m_editing = true;
            box.m_selection = 0;
            box.m_caretOn = true;
            font3.asked.clear();
            TrClear(box);
            TrWord(TrLine(box, 50), "ab", st, 30);
            box.Render();
            WM(v.tbCaret, GfLogIs({"select 3", "draw 3 4 1", "select 3", "draw 3 2 1"}) && TrVertices() == TrQuad(42, 48, 12, 12) &&
                              TrUniform(grey, 0.5f, 2.0f) && box.m_penX == 54 && box.m_penY == 50 &&
                              font3.asked == (std::vector<int>{'a', 'b', '_'}));
            box.m_caretOn = false;
            g_gfLog.clear();
            box.Render();
            WM(v.tbCaret, GfLogIs({"select 3", "draw 3 4 1"}));
            box.m_caretOn = true;
            box.m_editing = false;
            g_gfLog.clear();
            box.Render();
            WM(v.tbCaret, GfLogIs({"select 3", "draw 3 4 1"}));
            box.m_editing = true;
            box.m_selection = 1;
            g_gfLog.clear();
            box.Render();
            WM(v.tbCaret, GfLogIs({"select 3", "draw 3 4 1"}));
            // Line 1's caret: line 0's colour (the hovered white) and pen x.
            TrClear(box);
            TrWord(TrLine(box, 50), "ab", st, 30);
            TrLine(box, 80);
            box.m_hover = 0;
            box.Render();
            WM(v.tbCaret, GfLogIs({"select 3", "draw 3 4 1", "select 3", "draw 3 2 1"}) && TrVertices() == TrQuad(42, 78, 12, 12) &&
                              TrUniform(white, 0.5f, 2.0f));
            box.m_hover = -1;
            // No word yet: colour 0; the base 0: -1.2, -1.
            box.m_selection = 0;
            TrClear(box);
            TrLine(box, 0);
            box.m_penX = 9;
            box.Render();
            WM(v.tbCaret, GfLogIs({"select 3", "draw 3 2 1"}) && TrVertices() == TrQuad(9, -1, 12, 12) && TrUniform(0, 0.5f, 2.0f));
            // (25 pixels: 2.5 under the base, 25 wide.)
            const int big = TrStyle(grey, 0, 25.0f / 600.0f);
            TrClear(box);
            TrWord(TrLine(box, 50), "", big, 30);
            box.Render();
            WM(v.tbCaret, TrVertices() == TrQuad(30, 47, 25, 25));
            box.m_editing = false;
            TrClear(box);
            TrWord(TrLine(box, 50), "i", big, 30);
            box.Render();
            WM(v.tbGlyphs, TrVertices() == TrQuad(30, 50, 6, 25) && box.m_penX == 36);
            box.m_editing = true;
            ReleaseTextStyle(big);
            // A blank caret: the pen moved only.
            font3.caretAspect = 0.0f;
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            box.Render();
            WM(v.tbCaret, GfLogIs({"select 3", "draw 3 2 1"}) && box.m_penX == 41);
            font3.caretAspect = 1.0f;
            box.m_editing = false;
            box.m_caretOn = false;
        }

        // --- the pieces: at their x and y from the box's left and bottom, coloured as the line the
        // lines stopped at; a piece without its style skipped ---
        {
            TbRect(box, 10.5f, 20.75f, 200.0f, 100.0f);
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            TrWord(TrLine(box, 60), "a", st, 30);
            TrPiece(box, "b", 250, 0, 0);
            TrPiece(box, "a", st, 5, 7);
            box.m_hover = 2;
            box.Render();
            WM(v.tbPieces, GfLogIs({"select 3", "draw 3 2 1", "select 3", "draw 3 2 1", "select 3", "draw 3 2 1"}) &&
                               TrVertices() == TrQuad(15, 27, 6, 12) && TrUniform(white, 0.5f, 2.0f) && box.m_penX == 21);
            box.m_hover = 1;
            box.Render();
            WM(v.tbPieces, TrUniform(grey, 0.5f, 2.0f));
            TrClear(box);
            TrPiece(box, "a", st, -15, -27);
            box.Render();
            WM(v.tbPieces, TrVertices() == TrQuad(-4, -6, 6, 12));
            TbRect(box, 10.0f, 20.0f, 200.0f, 100.0f);
            box.m_hover = -1;
        }

        // --- a word without its style or font size ends the lines (the pieces still drawn) ---
        {
            const int none = TrStyle(grey, 0, 0.0f);
            TrClear(box);
            S_TextLine *line = TrLine(box, 50);
            TrWord(line, "a", st, 30);
            TrWord(line, "b", 250, 40);
            TrWord(line, "c", st, 50);
            TrWord(TrLine(box, 80), "d", st, 30);
            TrPiece(box, "e", st, 5, 7);
            box.m_hover = 0;
            box.m_editing = true;
            box.m_selection = 0;
            box.m_caretOn = true;
            box.Render();
            WM(v.tbFail, GfLogIs({"select 3", "draw 3 2 1", "select 3", "draw 3 2 1"}) && TrVertices() == TrQuad(15, 27, 6, 12) &&
                             TrUniform(white, 0.5f, 2.0f));
            box.m_editing = false;
            box.m_caretOn = false;
            box.m_hover = -1;
            TrClear(box);
            TrWord(TrLine(box, 50), "a", none, 30);
            TrWord(TrLine(box, 80), "d", st, 30);
            box.Render();
            WM(v.tbFail, g_gfLog.empty() && box.m_penX == 30);
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            TrWord(TrLine(box, 80), "d", none, 30);
            TrPiece(box, "e", none, 5, 7);
            TrPiece(box, "f", st, 5, 7);
            box.Render();
            WM(v.tbFail, GfLogIs({"select 3", "draw 3 2 1", "select 3", "draw 3 2 1"}) && TrVertices() == TrQuad(15, 27, 6, 12));
            ReleaseTextStyle(none);
        }

        // --- overflowing: clipped to the box (5 pixels more at the top, on the screen), the screen's
        // viewport back after ---
        {
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            box.m_overflows = true;
            box.Render();
            WM(v.tbClip, g_gfLog == TrClipped("viewport 10 20 201 106 0 1", {"select 3", "draw 3 2 1"}));
            g_trScreen = {3, 4, 640, 480, 0.25f, 0.75f};
            g_gfLog.clear();
            box.Render();
            WM(v.tbClip, g_gfLog == TrClipped("viewport 10 20 201 106 0 1", {"select 3", "draw 3 2 1"},
                                              "viewport 3 4 640 480 0.25 0.75"));
            g_trScreen = {0, 0, 800, 600, 0.0f, 1.0f};
            S_IGraphViewport clip;
            TbRect(box, -5.0f, -3.0f, 900.0f, 700.0f);
            WM(v.tbClip, box.ClipViewport(&clip) && clip.x == 0 && clip.y == 0 && clip.width == 801 && clip.height == 601 &&
                             clip.minZ == 0.0f && clip.maxZ == 1.0f);
            TbRect(box, 900.0f, 700.0f, 50.0f, 50.0f);
            WM(v.tbClip, box.ClipViewport(&clip) && clip.x == 800 && clip.y == 600 && clip.width == 1 && clip.height == 1);
            TbRect(box, -50.0f, -60.0f, 20.0f, 30.0f);
            WM(v.tbClip, box.ClipViewport(&clip) && clip.x == 0 && clip.y == 0 && clip.width == 1 && clip.height == 1);
            TbRect(box, 10.5f, 20.25f, 100.75f, 50.5f);
            WM(v.tbClip, box.ClipViewport(&clip) && clip.x == 10 && clip.y == 20 && clip.width == 101 && clip.height == 56);
            TbRect(box, 10.0f, 20.0f, -1.0f, 50.0f);
            WM(v.tbClip, !box.ClipViewport(&clip) && clip.width == 0);
            TbRect(box, 10.0f, 20.0f, 100.0f, -6.0f);
            WM(v.tbClip, !box.ClipViewport(&clip) && clip.height == 0);
            TbRect(box, 10.0f, 20.0f, 100.0f, -5.0f);
            WM(v.tbClip, box.ClipViewport(&clip) && clip.height == 1);
            // Edges of fractions near 0: kept (the width and the height cross a whole pixel).
            TbRect(box, 0.3f, 0.3f, 9.9f, 4.9f);
            WM(v.tbClip, box.ClipViewport(&clip) && clip.x == 0 && clip.y == 0 && clip.width == 10 && clip.height == 10);
            TbRect(box, 0.15f, 0.15f, 0.1f, -4.9f);
            WM(v.tbClip, box.ClipViewport(&clip) && clip.width == 1 && clip.height == 1);
            // Negative edges at 0.
            TbRect(box, 0.4f, 0.4f, -1.4f, 50.0f);
            WM(v.tbClip, !box.ClipViewport(&clip) && clip.width == 0);
            TbRect(box, 0.4f, 0.4f, 50.0f, -6.4f);
            WM(v.tbClip, !box.ClipViewport(&clip) && clip.height == 0);
            // Nothing of it on the screen: nothing drawn, the viewport kept.
            TbRect(box, 10.0f, 20.0f, -1.0f, 50.0f);
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            box.m_overflows = true;
            box.m_background = 0x80102030;
            box.Render();
            WM(v.tbClip, GfLogIs({"getviewport", "scrnx", "scrny"}));
            box.m_background = 0;
            TbRect(box, 10.0f, 20.0f, 200.0f, 100.0f);
        }

        // --- the background: a quad over the box, the driver's state 3 off around it ---
        {
            TrClear(box);
            TrWord(TrLine(box, 50), "a", st, 30);
            box.m_background = 0x80102030;
            g_trStates[3] = 5;
            box.Render();
            WM(v.tbBackground, GfLogIs({"get 3", "set 3 0", "texture 0 0", "draw 4 2 1", "set 3 1", "select 3", "draw 3 2 1"}));
            g_trStates[3] = 0;
            g_gfLog.clear();
            box.m_hover = 0;
            box.m_overflows = true;
            TrWord(box.m_lines[0], "", 250, 0);
            box.Render();
            WM(v.tbBackground, g_gfLog == TrClipped("viewport 10 20 201 106 0 1", {"get 3", "set 3 0", "texture 0 0", "draw 4 2 1",
                                                                                 "set 3 0", "select 3", "draw 3 2 1"}));
            box.m_hover = -1;
            TrClear(box);
            box.Render();
            WM(v.tbBackground, GfLogIs({"get 3", "set 3 0", "texture 0 0", "draw 4 2 1", "set 3 0"}) &&
                                   TrVertices() == "(10 20 0 0)(210 20 1 0)(210 120 0 1)(10 120 1 1)" &&
                                   g_trVertices[0].color == 0x80102030 && FloatBits(g_trVertices[3].z) == 0x3ce8bf06 &&
                                   FloatBits(g_trVertices[2].rhw) == 0x420cc9df);
            box.m_background = 0;
        }

        // --- the outline (its switch set): the clip's quad after the viewport's restored ---
        {
            g_textBoxOutline = true;
            g_trStates[9] = 7;
            TrClear(box);
            box.Render();
            WM(v.tbOutline, g_gfLog.empty());
            box.m_overflows = true;
            box.Render();
            std::vector<std::string> outlined = TrClipped("viewport 10 20 201 106 0 1", {});
            outlined.insert(outlined.end(), {"texture 0 0", "get 9", "set 9 0", "draw 4 2 1", "set 9 7"});
            WM(v.tbOutline, g_gfLog == outlined &&
                                TrVertices() == "(10 20 0 0)(211 20 1 0)(211 126 0 1)(10 126 1 1)" &&
                                g_trVertices[1].color == 0x2F005000 && FloatBits(g_trVertices[0].z) == 0x3ce8bf06 &&
                                FloatBits(g_trVertices[3].rhw) == 0x420cc9df);
            g_textBoxOutline = false;
            g_trStates[9] = 0;
        }

        // --- the unicode font: UTF-8 characters (not UTF-8: the byte, signed), the password's '*' ---
        {
            TbUnicode(true);
            font3.asked.clear();
            TrClear(box);
            TrWord(TrLine(box, 50), "a\xD0\x96\xFF", st, 30);
            box.Render();
            WM(v.tbUtf8, font3.asked == (std::vector<int>{'a', 0x416, -1}) && GfLogIs({"select 3", "draw 3 6 1"}) &&
                             TrVertices() == TrQuad(30, 50, 6, 12) + TrQuad(36, 50, 12, 12) + TrQuad(48, 50, 6, 12));
            font3.asked.clear();
            TrClear(box);
            TrWord(TrLine(box, 50), "\xFF" "bc", st, 30);
            box.Render();
            WM(v.tbUtf8, font3.asked == (std::vector<int>{-1, 'b', 'c'}) && GfLogIs({"select 3", "draw 3 6 1"}));
            TrClear(box);
            TrWord(TrLine(box, 50), "a b", st, 30);
            box.Render();
            WM(v.tbUtf8, GfLogIs({"select 3", "draw 3 4 1"}) && TrVertices() == TrQuad(30, 50, 6, 12) + TrQuad(41, 50, 6, 12));
            TrClear(box);
            TrWord(TrLine(box, 50), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", st, 0);
            box.Render();
            WM(v.tbUtf8, GfLogIs({"select 3", "draw 3 64 1", "select 3", "draw 3 2 1"}));
            TrClear(box);
            TrWord(TrLine(box, 50), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", st, 0);
            box.Render();
            WM(v.tbUtf8, GfLogIs({"select 3", "draw 3 64 1"}));
            const int password = TrStyle(grey, STYLE_PASSWORD);
            font3.asked.clear();
            TrClear(box);
            TrWord(TrLine(box, 50), "\xD0\x96" "b", password, 30);
            box.Render();
            WM(v.tbUtf8, font3.asked == (std::vector<int>{'*', '*'}) && TrVertices() == TrQuad(30, 50, 9, 12) + TrQuad(39, 50, 9, 12));
            g_gfLog.clear();
            SetFontBatching(true);
            TrClear(box);
            TrWord(TrLine(box, 50), "\xD0\x96 a", st, 30);
            box.Render();
            WM(v.tbUtf8, GfLogIs({"quad 1046 aabbccdd 12 12 30 50 0.5", "quad 97 aabbccdd 6 12 47 50 0.5"}));
            SetFontBatching(false);
            TrClear(box);
            TrWord(TrLine(box, 50), "", st, 30);
            box.Render();
            WM(v.tbUtf8, g_gfLog.empty());
            const int none = TrStyle(grey, 0, 0.0f);
            TrClear(box);
            TrWord(TrLine(box, 50), "a", none, 30);
            TrWord(TrLine(box, 80), "d", st, 30);
            box.Render();
            WM(v.tbUtf8, g_gfLog.empty());
            ReleaseTextStyle(none);
            TbUnicode(false);
            font3.asked.clear();
            TrClear(box);
            TrWord(TrLine(box, 50), "ab", password, 30);
            box.Render();
            WM(v.tbUtf8, font3.asked == (std::vector<int>{'*', '*'}) && TrVertices() == TrQuad(30, 50, 9, 12) + TrQuad(39, 50, 9, 12));
            ReleaseTextStyle(password);
        }

        // --- +0x76: moved by its step, kept from 0 to 2 under the box's top over the last line ---
        {
            TrClear(box);
            box.m_unknown76 = 0;
            box.StepUnknown76(5, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == 0);
            box.StepUnknown76(1, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == 0);
            box.StepUnknown76(-3, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == -2);
            TrLine(box, 200);
            box.m_unknown76 = -50;
            box.StepUnknown76(0, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == -50);
            box.StepUnknown76(-31, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == -82);
            box.m_unknown76 = -80;
            box.StepUnknown76(0, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == -80);
            box.m_unknown76 = 30;
            box.StepUnknown76(10, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == 0);
            box.m_lines.back()->y = 120;
            box.m_unknown76 = -3;
            box.StepUnknown76(0, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == -2);
            box.m_lines.back()->y = 119;
            box.m_unknown76 = -3;
            box.StepUnknown76(0, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == 0);
            TbRect(box, 10.0f, 20.0f, 200.5f, 100.5f);
            box.m_dirty = false;
            box.m_lines.back()->y = 200;
            box.m_unknown76 = -80;
            box.StepUnknown76(0, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == -81);
            box.m_lines.back()->y = 100;
            box.m_unknown76 = -80;
            box.StepUnknown76(0, &box.m_unknown76);
            WM(v.tbStep, box.m_unknown76 == 0);
            TbRect(box, 10.0f, 20.0f, 200.0f, 100.0f);
            // Render's step: its switch's low word.
            TrClear(box);
            box.m_unknown76 = 0;
            g_textBoxStep76 = 0x1FFFD;
            box.Render();
            WM(v.tbStep, box.m_unknown76 == -2);
            g_textBoxStep76 = 0;
            box.m_unknown76 = 0;
        }

        ReleaseTextStyle(st);
        font3.logging = false;
        g_igraphVtable[0x44 / 4] = oldGraph[0];
        g_igraphVtable[0x7C / 4] = oldGraph[1];
        g_igraphVtable[0x80 / 4] = oldGraph[2];
        g_igraphVtable[0x88 / 4] = oldGraph[3];
        g_driverVtable[0x18 / 4] = oldDriver[0];
        g_driverVtable[0x1C / 4] = oldDriver[1];
        g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&F_TbScrnX);
        g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&F_TbScrnY);
    }
    std::printf("render: tbPlaced %d tbGlyphs %d tbBlank %d tbFlush %d tbBatch %d tbColour %d tbCentred %d tbCaret %d "
                "tbPieces %d tbFail %d tbClip %d tbBackground %d tbOutline %d tbUtf8 %d tbStep %d tbFonts %d\n",
                v.tbPlaced, v.tbGlyphs, v.tbBlank, v.tbFlush, v.tbBatch, v.tbColour, v.tbCentred, v.tbCaret, v.tbPieces,
                v.tbFail, v.tbClip, v.tbBackground, v.tbOutline, v.tbUtf8, v.tbStep, v.tbFonts);

    // ===== part 9b-iv: the paged box and the video text box =====
    S_PagesResults g;
    {
        void *const oldGraph[] = {g_igraphVtable[0x44 / 4], g_igraphVtable[0x7C / 4], g_igraphVtable[0x80 / 4],
                                  g_igraphVtable[0x88 / 4]};
        g_igraphVtable[0x44 / 4] = reinterpret_cast<void *>(&F_TrSetTexture);
        g_igraphVtable[0x7C / 4] = reinterpret_cast<void *>(&F_TrSetViewport);
        g_igraphVtable[0x80 / 4] = reinterpret_cast<void *>(&F_TrGetViewport);
        g_igraphVtable[0x88 / 4] = reinterpret_cast<void *>(&F_PgDraw);
        font3.logging = true;
        const uint32_t grey = 0xAABBCCDD;
        const int st = TrStyle(grey);
        const int brk = TrStyle(grey, STYLE_NEW_PAGE);
        const int zero = TrStyle(grey, 0, 0.0f);
        const int bad = 250;

        // --- a new paged box: page 0, no areas or pages, not placed; deleted: its pages and their lines too ---
        {
            C_FontManager *const fonts = g_fontManager;
            const int refs = fonts->m_refs;
            auto *paged = new C_PagedTextBox();
            WM(g.pgNew, paged->m_page == 0 && paged->m_areas.empty() && paged->NumPages() == 0 && !paged->m_placed &&
                            paged->m_screenWidth == 800 && paged->m_screenHeight == 600 && paged->m_margin == 10 &&
                            fonts->m_refs == refs + 1);
            const C_str shared("p");
            S_TextLine *line = PgLine(PgPage(*paged, 0), 5);
            TrWord(line, "", st, 0);
            line->words[0]->text = shared;
            TrWord(PgLine(PgPage(*paged, 0), 5), "q", st, 0);
            WM(g.pgNew, shared.m_rep->refCount == 2);
            C_TextBox *base = paged;
            delete base;
            WM(g.pgNew, shared.m_rep->refCount == 1 && fonts->m_refs == refs);
        }

        C_PagedTextBox pb;
        // --- an area: the visual's screen rectangle (AttachTo's) the box's and a copy after the others ---
        {
            I3D_frame *visual = GfVisual("pgvisual", I3DVISUAL_OBJECT);
            WmField<uint32_t>(visual, 0x1D0) |= VISFLAGS_WBOUND_VALID;
            I3D_bbox bbox;
            bbox.min = S_vector(0.25f, -0.5f, 0.0f);
            bbox.max = S_vector(0.75f, -0.125f, 0.0f);
            bbox.minW = bbox.maxW = 0.0f;
            WmField<I3D_bbox>(visual, 0x170) = bbox;
            pb.AddArea(nullptr, visual);
            const S_TextArea a = pb.m_areas[0];
            WM(g.pgArea, pb.m_areas.size() == 1 && a.left == 800.0f * 0.25f + 7.0f && a.top == 600.0f * -0.5f * -1.3333334f - 1.0f &&
                             a.right == 800.0f * 0.75f - 7.0f && a.bottom == 1.0f - 600.0f * -0.125f * 1.3333334f &&
                             a.width == a.right - a.left && a.height == a.top - a.bottom && a.depth == 0.0f &&
                             pb.m_left == a.left && pb.m_top == a.top && pb.m_right == a.right && pb.m_bottom == a.bottom &&
                             pb.m_width == a.width && pb.m_height == a.height && pb.m_depth == 0.0f && pb.m_dirty && pb.m_placed);
            bbox.min = S_vector(0.5f, -0.25f, 0.0f);
            bbox.max = S_vector(0.625f, 0.0f, 0.0f);
            WmField<I3D_bbox>(visual, 0x170) = bbox;
            pb.m_dirty = false;
            pb.AddArea(nullptr, visual);
            WM(g.pgArea, pb.m_areas.size() == 2 && pb.m_areas[0].left == 207.0f && pb.m_areas[1].left == 407.0f &&
                             pb.m_areas[1].right == 493.0f && pb.m_areas[1].bottom == 1.0f && pb.m_areas[1].top == 199.0f &&
                             pb.m_areas[1].width == 86.0f && pb.m_areas[1].height == 198.0f && pb.m_dirty && pb.m_left == 407.0f);
            pb.m_dirty = false;
            pb.AddArea(nullptr, nullptr);
            WM(g.pgArea, pb.m_areas.size() == 2 && !pb.m_dirty);
        }

        // --- the page shown: 0 always; another in range ---
        {
            PgClear(pb, font3);
            pb.m_page = 5;
            pb.SetPage(0);
            WM(g.pgPage, pb.m_page == 0 && pb.NumPages() == 0);
            pb.SetPage(1);
            WM(g.pgPage, pb.m_page == 0);
            PgPage(pb, 0);
            PgPage(pb, 1);
            PgPage(pb, 0);
            pb.SetPage(2);
            WM(g.pgPage, pb.m_page == 2 && pb.NumPages() == 3 && pb.GetPage() == 2);
            pb.SetPage(3);
            WM(g.pgPage, pb.m_page == 2);
            pb.SetPage(-1);
            WM(g.pgPage, pb.m_page == 2);
            pb.SetPage(1);
            WM(g.pgPage, pb.m_page == 1);
            pb.SetPage(0);
            WM(g.pgPage, pb.m_page == 0);
        }

        // --- laid out again now ---
        {
            PgClear(pb, font3);
            pb.AddText(C_str("ab"), static_cast<uint8_t>(st));
            pb.m_dirty = false;
            pb.Relayout();
            WM(g.pgRelayout, !pb.m_dirty && pb.NumPages() == 1 && pb.m_pages[0]->lines.size() == 1 &&
                                 pb.m_pages[0]->lines[0]->words[0]->text == "ab");
            // No runs: the layout fails, the box dirty.
            pb.Clear();
            pb.m_dirty = false;
            pb.Relayout();
            WM(g.pgRelayout, pb.m_dirty && pb.NumPages() == 1);
            PgClear(pb, font3);
        }

        // --- the last page starting with a new page's run (its first line's first word) ---
        {
            PgClear(pb, font3);
            WM(g.pgBreak, pb.LastPageBreak() == -1);
            TrWord(PgLine(PgPage(pb, 0), 5), "a", brk, 0);
            WM(g.pgBreak, pb.LastPageBreak() == 0);
            TrWord(PgLine(PgPage(pb, 0), 5), "a", st, 0);
            WM(g.pgBreak, pb.LastPageBreak() == 0);
            S_TextLine *line = PgLine(PgPage(pb, 0), 5);
            TrWord(line, "a", brk, 0);
            TrWord(line, "b", st, 0);
            WM(g.pgBreak, pb.LastPageBreak() == 2);
            line = PgLine(PgPage(pb, 0), 5);
            TrWord(line, "a", st, 0);
            TrWord(line, "b", brk, 0);
            TrWord(PgLine(pb.m_pages.back(), 9), "c", brk, 0);
            PgPage(pb, 0);
            PgLine(PgPage(pb, 0), 5);
            WM(g.pgBreak, pb.LastPageBreak() == 2);
        }

        // Two areas (on the screen); the box's rectangle the first's.
        const std::vector<S_TextArea> areas = {{10.0f, 120.0f, 210.0f, 20.0f, 200.0f, 100.0f, 0.5f},
                                               {300.0f, 120.0f, 500.0f, 20.0f, 200.0f, 100.0f, 0.5f}};
        pb.m_areas = areas;
        TbRect(pb, 10.0f, 20.0f, 200.0f, 100.0f);

        // --- the render: nothing before the box is placed; laid out first when dirty (the fonts' batching
        // taken); the pages from the shown one, one per area ---
        {
            PgClear(pb, font3);
            pb.m_placed = false;
            pb.m_dirty = true;
            TrWord(PgLine(PgPage(pb, 0), 50), "a", st, 30);
            pb.Render();
            WM(g.pgRender, g_gfLog.empty() && pb.m_dirty);
            pb.m_placed = true;
            pb.m_batching = !g_fontManager->IsBatching();
            pb.Render();
            // (No runs: the layout fails, the pages kept.)
            WM(g.pgRender, GfLogIs({"select 3", "draw 3 2 1"}) && pb.m_dirty && pb.m_batching == g_fontManager->IsBatching());
            pb.AddText(C_str("q"), static_cast<uint8_t>(st));
            font3.asked.clear();
            pb.Render();
            // (The layout's measures first: each glyph asked for twice.)
            WM(g.pgRender, !pb.m_dirty && pb.NumPages() == 2 && font3.asked == (std::vector<int>{'q', 'q', 'a', 'q'}));
            pb.Clear();
            pb.m_dirty = false;
            delete pb.m_pages.back();
            pb.m_pages.pop_back();
            TrWord(PgLine(PgPage(pb, 1), 50), "b", st, 230);
            TrWord(PgLine(PgPage(pb, 0), 50), "c", st, 330);
            const std::vector<std::pair<int, std::vector<int>>> shown = {
                {0, {'a', 'b'}}, {1, {'b', 'c'}}, {2, {'c'}}, {3, {}}, {-1, {'a'}}, {-2, {}}};
            for (const auto &want : shown) {
                pb.m_page = want.first;
                font3.asked.clear();
                pb.Render();
                WM(g.pgRender, font3.asked == want.second);
            }
            pb.m_page = 1;
            font3.asked.clear();
            pb.m_areas.push_back(areas[0]);
            pb.Render();
            WM(g.pgRender, font3.asked == (std::vector<int>{'b', 'c'}));
            pb.m_areas.resize(1);
            font3.asked.clear();
            pb.Render();
            WM(g.pgRender, font3.asked == (std::vector<int>{'b'}));
            pb.m_areas.clear();
            font3.asked.clear();
            g_gfLog.clear();
            pb.Render();
            WM(g.pgRender, font3.asked.empty() && g_gfLog.empty());
            pb.m_areas = areas;
        }

        // --- a page's lines: at their bases (the scroll not used), the words at their x ---
        {
            PgClear(pb, font3);
            S_TextPage *page = PgPage(pb, 0);
            S_TextLine *line = PgLine(page, 50);
            TrWord(line, "a", st, 30);
            TrWord(line, "b", st, 40);
            pb.m_scroll = 7;
            pb.Render();
            WM(g.pgLines, TrVertices() == TrQuad(40, 50, 6, 12) && TrUniform(grey, 0.5f, 2.0f) && pb.m_penX == 46 && pb.m_penY == 50 &&
                              font3.asked == (std::vector<int>{'a', 'b'}));
            TrWord(PgLine(page, 80), "i", st, 7);
            pb.Render();
            WM(g.pgLines, TrVertices() == TrQuad(7, 80, 3, 12) && pb.m_penY == 80);
            pb.m_scroll = 0;
        }

        // --- the colours: the box's; dimmed (alpha 0x60); the style's (the hovered line's too) ---
        {
            PgClear(pb, font3);
            TrWord(PgLine(PgPage(pb, 0), 50), "a", st, 30);
            pb.m_colour = 0x11223344;
            pb.m_dimmed = true;
            pb.Render();
            WM(g.pgColour, TrUniform(0x11223344, 0.5f, 2.0f));
            pb.m_colour = 0;
            pb.Render();
            WM(g.pgColour, TrUniform(0x60BBCCDD, 0.5f, 2.0f));
            pb.m_dimmed = false;
            pb.m_hover = 0;
            pb.Render();
            WM(g.pgColour, TrUniform(grey, 0.5f, 2.0f));
            pb.m_hover = -1;
        }

        // --- a word without its style: its line's others drawn, not the page's next lines (the next
        // area's page drawn); a word not drawn (font size 0): the page ends ---
        {
            PgClear(pb, font3);
            S_TextPage *page = PgPage(pb, 0);
            S_TextLine *line = PgLine(page, 50);
            TrWord(line, "a", bad, 30);
            TrWord(line, "b", st, 40);
            TrWord(PgLine(page, 80), "c", st, 30);
            TrWord(PgLine(PgPage(pb, 1), 50), "d", st, 300);
            pb.Render();
            WM(g.pgStop, font3.asked == (std::vector<int>{'b', 'd'}));
            PgClear(pb, font3);
            page = PgPage(pb, 0);
            line = PgLine(page, 50);
            TrWord(line, "a", zero, 30);
            TrWord(line, "b", st, 40);
            TrWord(PgLine(page, 80), "c", st, 30);
            TrWord(PgLine(PgPage(pb, 1), 50), "d", st, 300);
            pb.Render();
            WM(g.pgStop, font3.asked == (std::vector<int>{'d'}) && GfLogIs({"select 3", "draw 3 2 1"}));
            PgClear(pb, font3);
            line = PgLine(PgPage(pb, 0), 50);
            TrWord(line, "a", st, 30);
            TrWord(line, "b", zero, 40);
            TrWord(line, "c", st, 50);
            pb.Render();
            WM(g.pgStop, font3.asked == (std::vector<int>{'a'}));
        }

        // --- an overflowing page: clipped to its area (5 pixels more at the top, on the screen, truncated),
        // the screen's viewport back after; its area off the screen: nothing ---
        {
            PgClear(pb, font3);
            pb.m_areas[1] = {10.5f, 120.7f, 210.9f, 20.2f, 0.0f, 0.0f, 0.5f};
            TrWord(PgLine(PgPage(pb, 1, true), 50), "a", st, 30);
            pb.Render();
            WM(g.pgClip, GfLogIs({"getviewport", "viewport 10 20 200 105 0 1", "select 3", "draw 3 2 1", "viewport 0 0 800 600 0 1"}));
            g_trScreen = {3, 4, 640, 480, 0.25f, 0.75f};
            g_gfLog.clear();
            pb.Render();
            WM(g.pgClip, GfLogIs({"getviewport", "viewport 10 20 200 105 0 1", "select 3", "draw 3 2 1", "viewport 3 4 640 480 0.25 0.75"}));
            g_trScreen = {0, 0, 800, 600, 0.0f, 1.0f};
            pb.m_areas[1] = {-50.0f, 120.0f, -10.0f, 20.0f, 0.0f, 0.0f, 0.5f};
            g_gfLog.clear();
            font3.asked.clear();
            pb.Render();
            WM(g.pgClip, GfLogIs({"getviewport"}) && font3.asked.empty());
            pb.m_areas = areas;
            // The clip itself (the area's width and height not used).
            WM(g.pgClip, PgClip(pb, {10.5f, 120.7f, 210.9f, 20.2f, 1.0f, 1.0f, 0.5f}) == "10 20 200 105 0 1");
            WM(g.pgClip, PgClip(pb, {-5.0f, 700.0f, 900.0f, -3.0f, 1.0f, 1.0f, 0.5f}) == "0 0 800 600 0 1");
            WM(g.pgClip, PgClip(pb, {-50.0f, 120.0f, -10.0f, 20.0f, 1.0f, 1.0f, 0.5f}) == "- 0 20 0 105 0 1");
            WM(g.pgClip, PgClip(pb, {900.0f, 100.0f, 950.0f, 10.0f, 1.0f, 1.0f, 0.5f}) == "- 800 10 0 95 0 1");
            WM(g.pgClip, PgClip(pb, {700.0f, 100.0f, 950.0f, 10.0f, 1.0f, 1.0f, 0.5f}) == "700 10 100 95 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, 700.0f, 100.0f, 650.0f, 1.0f, 1.0f, 0.5f}) == "- 10 600 90 0 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, 15.0f, 50.0f, 20.0f, 1.0f, 1.0f, 0.5f}) == "- 10 20 40 0 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, 14.9f, 50.0f, 10.0f, 1.0f, 1.0f, 0.5f}) == "10 10 40 9 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, -10.0f, 50.0f, -20.0f, 1.0f, 1.0f, 0.5f}) == "- 10 0 40 0 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, -4.5f, 50.0f, -20.0f, 1.0f, 1.0f, 0.5f}) == "- 10 0 40 0 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, -3.5f, 50.0f, -20.0f, 1.0f, 1.0f, 0.5f}) == "10 0 40 1 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, 100.0f, 10.9f, -20.0f, 1.0f, 1.0f, 0.5f}) == "- 10 0 0 105 0 1");
            WM(g.pgClip, PgClip(pb, {-0.5f, 100.0f, 0.75f, -20.0f, 1.0f, 1.0f, 0.5f}) == "- 0 0 0 105 0 1");
            WM(g.pgClip, PgClip(pb, {-0.5f, 100.0f, 1.0f, -20.0f, 1.0f, 1.0f, 0.5f}) == "0 0 1 105 0 1");
            // Fractions near 0 (truncated toward 0, negative ones too).
            WM(g.pgClip, PgClip(pb, {10.0f, 100.0f, -5.0f, -20.0f, 1.0f, 1.0f, 0.5f}) == "- 10 0 -10 105 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, 100.0f, 0.3f, -20.0f, 1.0f, 1.0f, 0.5f}) == "- 10 0 -9 105 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, 5.2f, 50.0f, 0.3f, 1.0f, 1.0f, 0.5f}) == "10 0 40 9 0 1");
            WM(g.pgClip, PgClip(pb, {0.3f, 100.0f, 10.2f, 0.0f, 1.0f, 1.0f, 0.5f}) == "0 0 9 105 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, -4.6f, 50.0f, 10.0f, 1.0f, 1.0f, 0.5f}) == "- 10 10 40 -9 0 1");
            WM(g.pgClip, PgClip(pb, {10.0f, -20.0f, 50.0f, 10.0f, 1.0f, 1.0f, 0.5f}) == "- 10 10 40 -10 0 1");
            pb.m_screenWidth = 1000;
            pb.m_screenHeight = 700;
            WM(g.pgClip, PgClip(pb, {-5.0f, 800.0f, 1200.0f, -3.0f, 1.0f, 1.0f, 0.5f}) == "0 0 1000 700 0 1");
            pb.m_screenWidth = 800;
            pb.m_screenHeight = 600;
        }
        PgClear(pb, font3);

        // ----- the video text box -----
        // --- a new one: from the first line, no lines (finished), not placed: nothing drawn ---
        C_VideoTextBox vb;
        {
            WM(g.vtNew, vb.m_firstLine == 0 && vb.Finished() && !vb.m_placed && vb.m_screenWidth == 800 && vb.m_margin == 10);
            g_gfLog.clear();
            vb.Render();
            WM(g.vtNew, g_gfLog.empty());
        }
        // The box over the screen.
        TbRect(vb, 0.0f, 0.0f, 800.0f, 600.0f);
        const std::vector<std::string> fades = PgFades(800, 600, 78);
        const auto logIs = [](std::vector<std::string> head, const std::vector<std::string> &tail) {
            head.insert(head.end(), tail.begin(), tail.end());
            return g_gfLog == head;
        };

        // --- the lines at their bases less the scroll (down to 100 pixels under the screen), then the fades ---
        {
            TrClear(vb);
            vb.m_firstLine = 0;
            S_TextLine *line = TrLine(vb, 50);
            TrWord(line, "a", st, 30);
            TrWord(line, "b", st, 40);
            TrWord(TrLine(vb, 120), "c", st, 50);
            font3.asked.clear();
            vb.Render();
            WM(g.vtLines, logIs({"select 3", "draw 3 2 1", "select 3", "draw 3 2 1", "select 3", "draw 3 2 1"}, fades) &&
                              font3.asked == (std::vector<int>{'a', 'b', 'c'}) && TrVertices() == TrQuad(50, 120, 6, 12) &&
                              TrUniform(grey, 0.5f, 2.0f) && vb.m_penY == 120 && vb.m_penX == 56);
            // The quads' depth.
            WM(g.vtLines, g_textBoxQuad[0].z == 0.0284113996f && g_textBoxQuad[3].rhw == 35.1971397f);
            vb.m_scroll = 20;
            vb.Render();
            WM(g.vtLines, TrVertices() == TrQuad(50, 100, 6, 12) && vb.m_firstLine == 0);
            // 700 drawn; 701: the lines end.
            TrClear(vb);
            vb.m_scroll = 0;
            TrWord(TrLine(vb, 700), "a", st, 30);
            TrWord(TrLine(vb, 701), "b", st, 30);
            TrWord(TrLine(vb, 100), "c", st, 30);
            font3.asked.clear();
            vb.Render();
            WM(g.vtLines, font3.asked == (std::vector<int>{'a'}));
            vb.m_screenHeight = 500;
            TrClear(vb);
            TrWord(TrLine(vb, 600), "a", st, 30);
            TrWord(TrLine(vb, 601), "b", st, 30);
            font3.asked.clear();
            vb.Render();
            WM(g.vtLines, font3.asked == (std::vector<int>{'a'}) && logIs({"select 3", "draw 3 2 1"}, PgFades(800, 500, 78)));
            vb.m_screenHeight = 600;
            // The hovered line's white (WordColour).
            TrClear(vb);
            TrWord(TrLine(vb, 50), "a", st, 30);
            TrWord(TrLine(vb, 60), "b", st, 30);
            vb.m_hover = 1;
            vb.Render();
            WM(g.vtLines, TrUniform(0xFFFFFFFF, 0.5f, 2.0f));
            vb.m_hover = 0;
            vb.Render();
            WM(g.vtLines, TrUniform(grey, 0.5f, 2.0f));
            vb.m_hover = -1;
        }

        // --- the lines above the top passed for good (the base's 16 bits less the scroll's); laid out:
        // from the first ---
        {
            TrClear(vb);
            vb.m_firstLine = 0;
            TrWord(TrLine(vb, 50), "a", st, 30);
            TrWord(TrLine(vb, 120), "b", st, 30);
            vb.m_scroll = 50;
            font3.asked.clear();
            vb.Render();
            WM(g.vtFirst, vb.m_firstLine == 0 && font3.asked == (std::vector<int>{'a', 'b'}) && TrVertices() == TrQuad(30, 70, 6, 12));
            vb.m_scroll = 51;
            font3.asked.clear();
            vb.Render();
            WM(g.vtFirst, vb.m_firstLine == 1 && font3.asked == (std::vector<int>{'b'}) && !vb.Finished());
            vb.m_scroll = 0;
            font3.asked.clear();
            vb.Render();
            WM(g.vtFirst, vb.m_firstLine == 1 && font3.asked == (std::vector<int>{'b'}));
            vb.m_scroll = 121;
            vb.Render();
            WM(g.vtFirst, vb.m_firstLine == 2 && vb.Finished());
            // (30000 + 5000 in 16 bits: negative.)
            TrClear(vb);
            vb.m_firstLine = 0;
            TrWord(TrLine(vb, 30000), "a", st, 30);
            vb.m_scroll = -5000;
            font3.asked.clear();
            vb.Render();
            WM(g.vtFirst, vb.m_firstLine == 1 && font3.asked.empty() && logIs({}, fades));
            vb.m_scroll = 0;
            // Out of order: a later line above the top passes those before it.
            TrClear(vb);
            vb.m_firstLine = 0;
            TrWord(TrLine(vb, 100), "a", st, 30);
            TrWord(TrLine(vb, -5), "b", st, 30);
            TrWord(TrLine(vb, 200), "c", st, 30);
            font3.asked.clear();
            vb.Render();
            WM(g.vtFirst, vb.m_firstLine == 2 && font3.asked == (std::vector<int>{'a', 'c'}));
            // Laid out (dirty): from the first line.
            TrClear(vb);
            vb.m_firstLine = 5;
            vb.AddText(C_str("ab"), static_cast<uint8_t>(st));
            vb.m_batching = !g_fontManager->IsBatching();
            g_gfLog.clear();
            vb.Render();
            WM(g.vtFirst, !vb.m_dirty && vb.m_lines.size() == 1 && vb.m_firstLine == 0 && vb.m_batching == g_fontManager->IsBatching() &&
                              std::count(g_gfLog.begin(), g_gfLog.end(), "draw 3 4 1") == 1);
            vb.Clear();
            TrClear(vb);
        }

        // --- a word without its style ends its line (the next drawn); one not drawn: the next words drawn ---
        {
            TrClear(vb);
            vb.m_firstLine = 0;
            S_TextLine *line = TrLine(vb, 50);
            TrWord(line, "a", bad, 30);
            TrWord(line, "b", st, 40);
            TrWord(TrLine(vb, 60), "c", st, 30);
            font3.asked.clear();
            vb.Render();
            WM(g.vtStop, font3.asked == (std::vector<int>{'c'}));
            TrClear(vb);
            line = TrLine(vb, 50);
            TrWord(line, "a", zero, 30);
            TrWord(line, "b", st, 40);
            font3.asked.clear();
            vb.Render();
            WM(g.vtStop, font3.asked == (std::vector<int>{'b'}) && TrVertices() == TrQuad(40, 50, 6, 12));
        }

        // --- the fades: 100 / 1024 of the screen's width high (truncated) ---
        {
            TrClear(vb);
            vb.m_screenWidth = 1000;
            vb.m_screenHeight = 700;
            vb.Render();
            WM(g.vtFade, g_gfLog == PgFades(1000, 700, 97));
            vb.m_screenWidth = 1024;
            g_gfLog.clear();
            vb.Render();
            WM(g.vtFade, g_gfLog == PgFades(1024, 700, 100));
            vb.m_screenWidth = 1023;
            g_gfLog.clear();
            vb.Render();
            WM(g.vtFade, g_gfLog == PgFades(1023, 700, 99));
            vb.m_screenWidth = 800;
            vb.m_screenHeight = 600;
        }

        // --- the scroll: the pixels (true); no scroll range ---
        {
            C_TextBox *base = &vb;
            vb.m_scrollRange = 5;
            vb.m_scroll = 3;
            TrClear(vb);
            TrLine(vb, 900);
            WM(g.vtScroll, vb.ScrollTo(-42) && vb.m_scroll == -42 && base->ScrollTo(1000) && vb.m_scroll == 1000);
            base->UpdateScrollRange();
            vb.UpdateScrollRange();
            WM(g.vtScroll, vb.m_scrollRange == 5 && vb.m_scroll == 1000);
            vb.m_scroll = 0;
            vb.m_scrollRange = 0;
        }

        // --- finished: the first line past the last ---
        {
            TrClear(vb);
            TrLine(vb, 10);
            TrLine(vb, 20);
            const bool want[] = {false, false, true, true};
            for (unsigned i = 0; i < 4; ++i) {
                vb.m_firstLine = i;
                WM(g.vtFinished, vb.Finished() == want[i]);
            }
            TrClear(vb);
            vb.m_firstLine = 0;
            WM(g.vtFinished, vb.Finished());
        }

        // --- through the base: the render and the scroll the video box's; deleted: the fonts released ---
        {
            C_FontManager *const fonts = g_fontManager;
            const int refs = fonts->m_refs;
            C_TextBox *base = new C_VideoTextBox();
            WM(g.vtVirtual, fonts->m_refs == refs + 1);
            TbRect(*base, 0.0f, 0.0f, 800.0f, 600.0f);
            base->m_dirty = false;
            g_gfLog.clear();
            base->Render();
            WM(g.vtVirtual, g_gfLog == fades && base->ScrollTo(12345) && base->m_scroll == 12345);
            delete base;
            WM(g.vtVirtual, fonts->m_refs == refs);
            base = new C_PagedTextBox();
            TbRect(*base, 0.0f, 0.0f, 800.0f, 600.0f);
            g_gfLog.clear();
            base->Render();
            WM(g.vtVirtual, g_gfLog.empty() && base->m_dirty);
            delete base;
            WM(g.vtVirtual, fonts->m_refs == refs);
        }

        ReleaseTextStyle(zero);
        ReleaseTextStyle(brk);
        ReleaseTextStyle(st);
        font3.logging = false;
        g_igraphVtable[0x44 / 4] = oldGraph[0];
        g_igraphVtable[0x7C / 4] = oldGraph[1];
        g_igraphVtable[0x80 / 4] = oldGraph[2];
        g_igraphVtable[0x88 / 4] = oldGraph[3];
    }
    std::printf("pages: pgNew %d pgArea %d pgPage %d pgRelayout %d pgBreak %d pgRender %d pgLines %d pgColour %d pgStop %d "
                "pgClip %d vtNew %d vtLines %d vtFirst %d vtStop %d vtFade %d vtScroll %d vtFinished %d vtVirtual %d\n",
                g.pgNew, g.pgArea, g.pgPage, g.pgRelayout, g.pgBreak, g.pgRender, g.pgLines, g.pgColour, g.pgStop, g.pgClip,
                g.vtNew, g.vtLines, g.vtFirst, g.vtStop, g.vtFade, g.vtScroll, g.vtFinished, g.vtVirtual);

    // ===== part 9b-iv-b: the paged box's layout =====
    // (The default style: 12 pixels, 18 above and 6 below its base: a line's above 6, below 18 (the
    // quirk), 36 high; 'a' 6 wide, a blank 5, 'i' 3, 'W' and 'Ж' 12.)
    S_PagedResults lp;
    {
        const int st = TbStyle(0.0f, 1, 1);
        const int own = TbStyle(0.0f, 0, 1);
        const int newLine = TbStyle(0.0f, 1, 1, STYLE_NEW_LINE);
        const int newPage = TbStyle(0.0f, 1, 1, STYLE_NEW_PAGE);
        const int noWrap = TbStyle(0.05f, 1, 0);
        const int noWrapRight = TbStyle(0.05f, 1, 0, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 3);
        const int big = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.03f, 0.05f, 0.02f);
        const int centred = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 2);
        const int right = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 3);
        const int none = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 0);
        const int atX = TbStyle(0.0f, 1, 1, STYLE_AT_X);
        const int justified = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 4);
        const int ownJustified = TbStyle(0.0f, 0, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 4);
        const int bad = 250;
        C_PagedTextBox lb;
        TbUnicode(false);

        // --- no runs or no areas: nothing (false) ---
        {
            lb.m_areas.clear();
            WM(lp.lpNone, !LpLayout(lb, {{"aa", st}}) && lb.m_pages.empty());
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 100.0f)};
            WM(lp.lpNone, !LpLayout(lb, {}) && lb.m_pages.empty());
            lb.m_dirty = true;
            lb.Relayout();
            WM(lp.lpNone, lb.m_dirty);
        }

        // --- a line: as wide as the area, its words' measures, stacked from the area's bottom, aligned
        // at its left; the page's room less the line's height ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 100.0f)};
            lb.m_dirty = true;
            WM(lp.lpLines, LpLayout(lb, {{"aa aa aa", st}}) && !lb.m_dirty &&
                               LpPages(lb) == "[0 64: (50 6 18 12 54 aa aa aa/46@10)]");
            // (Fractions truncated: the width 100.9, the bottom 20.9, the left 10.6, the height 100.9.)
            lb.m_areas = {{10.6f, 121.8f, 110.6f, 20.9f, 100.9f, 100.9f, 0.5f}};
            WM(lp.lpLines, LpLayout(lb, {{"aa", st}, {"aa", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 76 aa/12@10 aa/12@22)]");
        }

        // --- wrapped at the last blank that fits (a character fits under the room, not at it) ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 100.0f)};
            WM(lp.lpWrap, LpLayout(lb, {{"aa aa aa", st}}) &&
                              LpPages(lb) == "[0 28: (50 6 18 12 11 aa aa/29@10) (86 6 18 12 28 aa/12@10)]");
            lb.m_areas = {LpRect(10.0f, 20.0f, 41.0f, 100.0f)};
            WM(lp.lpWrap, LpLayout(lb, {{"aa aa aa", st}}) &&
                              LpPages(lb) == "[0 28: (50 6 18 12 12 aa aa/29@10) (86 6 18 12 29 aa/12@10)]");
            // Two blanks: the last that fits.
            lb.m_areas = {LpRect(10.0f, 20.0f, 60.0f, 200.0f)};
            WM(lp.lpWrap, LpLayout(lb, {{"aa aa aa aa aa", st}}) &&
                              LpPages(lb) == "[0 128: (50 6 18 12 14 aa aa aa/46@10) (86 6 18 12 31 aa aa/29@10)]");
            // A tab a blank; a byte over 0x7F not (its glyph 6 wide).
            lb.m_areas = {LpRect(10.0f, 20.0f, 30.0f, 200.0f)};
            WM(lp.lpWrap, LpLayout(lb, {{"aa\taa", st}}) && LpPages(lb) == "[0 128: (50 6 18 12 18 aa/12@10) (86 6 18 12 18 aa/12@10)]");
            font3.asked.clear();
            WM(lp.lpWrap, LpLayout(lb, {{"aa\xA0" "aaaa", st}}) &&
                              std::count(font3.asked.begin(), font3.asked.end(), 0xA0) == 2 &&
                              LpPages(lb) == "[0 128: (50 6 18 12 30 aa\xA0" "a/0@10) (86 6 18 12 18 aa/12@10)]");
        }

        // --- a line with no room on its page: a new page, in the next area in turn (its room the area's
        // height, truncated), the line as wide as the area it was made for ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 50.0f)};
            WM(lp.lpPages, LpLayout(lb, {{"aa aa aa", st}}) &&
                               LpPages(lb) == "[0 14: (50 6 18 12 11 aa aa/29@10)][0 14: (50 6 18 12 28 aa/12@10)]");
            // Exactly the room: kept.
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 72.9f)};
            WM(lp.lpPages, LpLayout(lb, {{"aa aa aa", st}}) &&
                               LpPages(lb) == "[0 0: (50 6 18 12 11 aa aa/29@10) (86 6 18 12 28 aa/12@10)]");
            // Less than a line: an empty page first.
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 35.0f)};
            WM(lp.lpPages, LpLayout(lb, {{"aa", st}}) && LpPages(lb) == "[0 35:][0 -1: (50 6 18 12 28 aa/12@10)]");
            WM(lp.lpPages, lb.LastPage() == lb.m_pages[1]);
            for (S_TextPage *page : lb.m_pages)
                delete page;
            lb.m_pages.clear();
            WM(lp.lpPages, lb.LastPage() == nullptr);
        }
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 50.0f), LpRect(300.0f, 25.0f, 60.0f, 60.0f), LpRect(500.0f, 0.0f, 80.0f, 40.0f)};
            WM(lp.lpAreas, LpLayout(lb, {{"aa aa aa aa aa aa aa", st}}) &&
                               LpPages(lb) == "[0 14: (50 6 18 12 11 aa aa/29@10)][1 24: (55 6 18 12 11 aa aa/29@300)]"
                                              "[2 4: (30 6 18 12 14 aa aa aa/46@500)]");
            lb.NewPage();
            WM(lp.lpAreas, lb.NumPages() == 4 && lb.m_pages[3]->area == 0 && lb.m_pages[3]->room == 50 && !lb.m_pages[3]->overflows &&
                               lb.m_pages[3]->lines.empty());
            lb.NewPage();
            WM(lp.lpAreas, lb.m_pages[4]->area == 1 && lb.m_pages[4]->room == 60);
        }

        // --- a word wider than a line: the part that fits (no width), the next character dropped; not a
        // character on an empty line: the layout fails (the box dirty) ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 20.0f, 100.0f)};
            WM(lp.lpForced, LpLayout(lb, {{"aaaaa", st}}) && LpPages(lb) == "[0 28: (50 6 18 12 20 aaa/0@10) (86 6 18 12 14 a/6@10)]");
            // A blank first: no blank.
            WM(lp.lpForced, LpLayout(lb, {{" aaaaa", st}}) && LpPages(lb) == "[0 28: (50 6 18 12 20  aa/0@10) (86 6 18 12 8 aa/12@10)]");
            // After a wrap too.
            lb.m_areas = {LpRect(10.0f, 20.0f, 30.0f, 200.0f)};
            WM(lp.lpForced, LpLayout(lb, {{"aa aaaaaa", st}}) &&
                                LpPages(lb) == "[0 92: (50 6 18 12 18 aa/12@10) (86 6 18 12 30 aaaa/0@10) (122 6 18 12 24 a/6@10)]");
        }
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 6.0f, 100.0f)};
            lb.m_dirty = true;
            WM(lp.lpFail, !LpLayout(lb, {{"a", st}}) && LpPages(lb) == "[0 100:]" && lb.m_dirty);
            lb.m_areas = {LpRect(10.0f, 20.0f, 7.0f, 100.0f)};
            WM(lp.lpFail, LpLayout(lb, {{"a", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 1 a/6@10)]");
            // A run without its style: false (the line being filled dropped).
            WM(lp.lpFail, !LpLayout(lb, {{"a", bad}}) && LpPages(lb).empty());
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 100.0f)};
            WM(lp.lpFail, !LpLayout(lb, {{"aa", st}, {"b", bad}}) && LpPages(lb) == "[0 100:]");
            WM(lp.lpFail, !LpLayout(lb, {{"aa", own}, {"bb", own}, {"b", bad}}) && LpPages(lb) == "[0 64: (-1 6 18 12 88 aa/12@-1)]");
        }

        // --- not wrapped: one word as wide as its text (not cut to the column), the line's room negative:
        // aligned at the left (the box overflows) ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 100.0f)};
            lb.m_overflows = false;
            WM(lp.lpNoWrap, LpLayout(lb, {{"aaaaaaaaaa", noWrap}}) && LpPages(lb) == "[0 64: (50 6 18 12 -20 aaaaaaaaaa/60@10)]" &&
                                lb.m_overflows);
            WM(lp.lpNoWrap, LpLayout(lb, {{"aaaaaaaaaa", noWrapRight}}) && LpPages(lb) == "[0 64: (50 6 18 12 -20 aaaaaaaaaa/60@10)]");
            lb.m_areas = {LpRect(10.0f, 20.0f, 60.0f, 100.0f)};
            lb.m_overflows = false;
            WM(lp.lpNoWrap, LpLayout(lb, {{"aaaaaaaaaa", noWrapRight}}) && LpPages(lb) == "[0 64: (50 6 18 12 0 aaaaaaaaaa/60@10)]" &&
                                !lb.m_overflows);
            const int noWrapJustified = TbStyle(0.0f, 0, 0, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 4);
            const int ownJustifiedToo = TbStyle(0.0f, 0, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 4);
            lb.m_areas = {LpRect(10.0f, 20.0f, 29.0f, 100.0f)};
            WM(lp.lpNoWrap, LpLayout(lb, {{"aa aa", noWrapJustified}, {"bb", ownJustifiedToo}}) &&
                                LpPages(lb) == "[0 28: (50 6 18 12 0 aa aa/29@10) (86 6 18 12 17 bb/12@10)]");
            ReleaseTextStyle(ownJustifiedToo);
            ReleaseTextStyle(noWrapJustified);
            lb.m_areas = {LpRect(10.0f, 20.0f, 61.0f, 100.0f)};
            WM(lp.lpNoWrap, LpLayout(lb, {{"aaaaaaaaaa", noWrapRight}}) && LpPages(lb) == "[0 64: (50 6 18 12 1 aaaaaaaaaa/60@11)]");
            // (Never reset: overflowing stays.)
            lb.m_overflows = false;
            WM(lp.lpNoWrap, LpLayout(lb, {{"a", st}}) && !lb.m_overflows);
            lb.m_overflows = true;
            WM(lp.lpNoWrap, LpLayout(lb, {{"a", st}}) && lb.m_overflows);
            lb.m_overflows = false;
        }

        // --- the line's measures: its words' largest (the style's space below its above, its above its
        // below) ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 100.0f)};
            WM(lp.lpMeasures, LpLayout(lb, {{"aa", st}, {"b", big}}) && LpPages(lb) == "[0 40: (68 12 30 18 79 aa/12@10 b/9@22)]");
            WM(lp.lpMeasures, LpLayout(lb, {{"b", big}, {"aa", st}}) && LpPages(lb) == "[0 40: (68 12 30 18 79 b/9@10 aa/12@19)]");
            // (Rounded: 0.0251 of 600, 15.06: 15.)
            const int odd = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.0251f, 0.0259f, 0.0249f);
            WM(lp.lpMeasures, LpLayout(lb, {{"a", odd}}) && LpPages(lb) == "[0 54: (51 15 16 15 92 a/8@10)]");
            ReleaseTextStyle(odd);
            const int low = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.0205f, 0.0305f, 0.0105f);
            WM(lp.lpMeasures, LpLayout(lb, {{"aa", low}}) && LpPages(lb) == "[0 64: (50 6 18 12 88 aa/12@10)]");
            ReleaseTextStyle(low);
        }

        // --- runs on a line of their own, joined to the last (a new line for the first after one of its
        // own), or on a new line asked for ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 200.0f)};
            WM(lp.lpJoins, LpLayout(lb, {{"aa", st}, {"bb", st}, {"cc", own}, {"dd", st}, {"ee", newLine}, {"ff", st}}) &&
                               LpPages(lb) == "[0 56: (50 6 18 12 76 aa/12@10 bb/12@22) (86 6 18 12 88 cc/12@10) "
                                              "(122 6 18 12 88 dd/12@10) (158 6 18 12 76 ee/12@10 ff/12@22)]");
            WM(lp.lpJoins, LpLayout(lb, {{"aa", own}, {"bb", own}}) &&
                               LpPages(lb) == "[0 128: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)]");
            WM(lp.lpJoins, LpLayout(lb, {{"aa", newLine}, {"bb", newLine}}) &&
                               LpPages(lb) == "[0 128: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)]");
        }

        // --- a new page's run: a new page when the last has lines (the line being filled ends there; a
        // joined run after it on a new line: the fix) ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 200.0f)};
            WM(lp.lpBreak, LpLayout(lb, {{"aa", newPage}, {"bb", newPage}}) && LpPages(lb) == "[0 164: (50 6 18 12 76 aa/12@10 bb/12@22)]");
            WM(lp.lpBreak, LpLayout(lb, {{"aa", own}, {"bb", st}, {"cc", newPage}, {"dd", st}}) &&
                               LpPages(lb) == "[0 128: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)]"
                                              "[0 164: (50 6 18 12 76 cc/12@10 dd/12@22)]" &&
                               lb.LastPageBreak() == 1);
            WM(lp.lpBreak, LpLayout(lb, {{"aa", own}, {"bb", own}, {"cc", newPage}}) &&
                               LpPages(lb) == "[0 128: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)][0 164: (50 6 18 12 88 cc/12@10)]");
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 200.0f), LpRect(300.0f, 20.0f, 50.0f, 200.0f)};
            WM(lp.lpBreak, LpLayout(lb, {{"aa", own}, {"bb", own}, {"cc", newPage}, {"dd", st}}) &&
                               LpPages(lb) == "[0 128: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)]"
                                              "[1 164: (50 6 18 12 26 cc/12@300 dd/12@312)]");
        }

        // --- stacked from the area's bottom (truncated); overflowing: the last line's base over its top ---
        {
            lb.m_areas = {{10.0f, 40.0f, 110.0f, 20.0f, 100.0f, 100.0f, 0.5f}};
            WM(lp.lpStack, LpLayout(lb, {{"aa", st}}) && LpPages(lb) == "[0 64!: (50 6 18 12 88 aa/12@10)]");
            lb.m_areas = {{10.0f, 50.0f, 110.0f, 20.9f, 100.0f, 100.0f, 0.5f}};
            WM(lp.lpStack, LpLayout(lb, {{"aa", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 88 aa/12@10)]");
            lb.m_areas = {{10.0f, 49.9f, 110.0f, -0.5f, 100.0f, 100.0f, 0.5f}};
            WM(lp.lpStack, LpLayout(lb, {{"aa", st}}) && LpPages(lb) == "[0 64: (30 6 18 12 88 aa/12@10)]");
            lb.m_areas = {{10.0f, 85.9f, 110.0f, 20.0f, 100.0f, 100.0f, 0.5f}};
            WM(lp.lpStack, LpLayout(lb, {{"aa", own}, {"bb", own}}) && LpPages(lb) == "[0 28!: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)]");
            lb.m_areas = {{10.0f, 86.0f, 110.0f, 20.0f, 100.0f, 100.0f, 0.5f}};
            WM(lp.lpStack, LpLayout(lb, {{"aa", own}, {"bb", own}}) && LpPages(lb) == "[0 28: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)]");
        }

        // --- aligned by the first word's style: left; centred (half the room, truncated); right; none and
        // the words at x not placed ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 100.0f)};
            WM(lp.lpAlign, LpLayout(lb, {{"i", centred}}) && LpPages(lb) == "[0 64: (50 6 18 12 97 i/3@58)]");
            WM(lp.lpAlign, LpLayout(lb, {{"i", right}}) && LpPages(lb) == "[0 64: (50 6 18 12 97 i/3@107)]");
            WM(lp.lpAlign, LpLayout(lb, {{"i", centred}, {"aa", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 85 i/3@52 aa/12@55)]");
            WM(lp.lpAlign, LpLayout(lb, {{"i", st}, {"aa", right}}) && LpPages(lb) == "[0 64: (50 6 18 12 85 i/3@10 aa/12@13)]");
            WM(lp.lpAlign, LpLayout(lb, {{"i", right}, {"aa", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 85 i/3@95 aa/12@98)]");
            WM(lp.lpAlign, LpLayout(lb, {{"i", none}}) && LpPages(lb) == "[0 64: (50 6 18 12 97 i/3@-1)]");
            WM(lp.lpAlign, LpLayout(lb, {{"i", atX}}) && LpPages(lb) == "[0 64: (50 6 18 12 97 i/3@-1)]");
            lb.m_areas = {{10.6f, 120.0f, 110.6f, 20.0f, 100.0f, 100.0f, 0.5f}};
            WM(lp.lpAlign, LpLayout(lb, {{"i", st}, {"aa", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 85 i/3@10 aa/12@13)]");
            WM(lp.lpAlign, LpLayout(lb, {{"i", right}, {"aa", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 85 i/3@95 aa/12@98)]");
            WM(lp.lpAlign, LpLayout(lb, {{"i", centred}, {"aa", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 85 i/3@52 aa/12@55)]");
            lb.m_areas = {{10.4f, 120.0f, 110.4f, 20.0f, 100.0f, 100.0f, 0.5f}};
            WM(lp.lpAlign, LpLayout(lb, {{"i", right}, {"aa", st}}) && LpPages(lb) == "[0 64: (50 6 18 12 85 i/3@95 aa/12@98)]");
        }

        // --- justified: a line followed by a justified one split at its blanks, the area's width shared
        // out between its words; the paragraph's last line as it is ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 100.0f)};
            WM(lp.lpJustify, LpLayout(lb, {{"aa aa aa", justified}}) &&
                                 LpPages(lb) == "[0 28: (50 6 18 12 11 aa/12@10 aa/12@38) (86 6 18 12 28 aa/12@10)]");
            lb.m_areas = {LpRect(10.0f, 20.0f, 101.0f, 200.0f)};
            WM(lp.lpJustify, LpLayout(lb, {{"aa aa aa", ownJustified}, {"bb", ownJustified}}) &&
                                 LpPages(lb) == "[0 128: (50 6 18 12 55 aa/12@10 aa/12@54 aa/12@99) (86 6 18 12 89 bb/12@10)]");
            WM(lp.lpJustify, LpLayout(lb, {{"aa aa", ownJustified}, {"bb", own}}) &&
                                 LpPages(lb) == "[0 128: (50 6 18 12 72 aa aa/29@10) (86 6 18 12 89 bb/12@10)]");
            // One word: as it is.
            WM(lp.lpJustify, LpLayout(lb, {{"aa", ownJustified}, {"bb", ownJustified}}) &&
                                 LpPages(lb) == "[0 128: (50 6 18 12 89 aa/12@10) (86 6 18 12 89 bb/12@10)]");
            // The gap direct.
            S_TextPage page;
            page.area = 0;
            S_TextLine line;
            TrWord(&line, "a", st, 0);
            WM(lp.lpJustify, lb.JustifyPageGap(&page, &line) == 0.0f);
            TrWord(&line, "b", st, 0);
            TrWord(&line, "c", st, 0);
            line.words[0]->width = 10;
            line.words[1]->width = 20;
            line.words[2]->width = 30;
            WM(lp.lpJustify, lb.JustifyPageGap(&page, &line) == 20.5f);
        }

        // --- the next line justified: the page's next, else the next page's first (none: not) ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 36.0f)};
            WM(lp.lpNext, LpLayout(lb, {{"aa aa aa", justified}}) &&
                              LpPages(lb) == "[0 0: (50 6 18 12 11 aa/12@10 aa/12@38)][0 0: (50 6 18 12 28 aa/12@10)]");
            // Direct: the next line without words, a next page without lines.
            for (S_TextPage *p : lb.m_pages)
                delete p;
            lb.m_pages.clear();
            S_TextPage *first = PgPage(lb, 0);
            TrWord(PgLine(first, 5), "a", justified, 0);
            PgLine(first, 5);
            S_TextPage *second = PgPage(lb, 0);
            TrWord(PgLine(second, 5), "a", justified, 0);
            S_TextPage *third = PgPage(lb, 0);
            TrWord(PgLine(third, 5), "a", st, 0);
            PgPage(lb, 0);
            S_TextPage *fifth = PgPage(lb, 0);
            TrWord(PgLine(fifth, 5), "a", justified, 0);
            WM(lp.lpNext, !lb.NextJustified(0, 0) && lb.NextJustified(0, 1) && !lb.NextJustified(1, 0) && !lb.NextJustified(2, 0) &&
                              lb.NextJustified(3, 0) && !lb.NextJustified(4, 0) && lb.NextJustified(0, -2) && !lb.NextJustified(1, -2) &&
                              lb.NextJustified(1, -1));
            first->lines[1]->words.push_back(new S_TextWord{C_str("b"), static_cast<uint8_t>(justified), 1});
            WM(lp.lpNext, lb.NextJustified(0, 0) && lb.NextJustified(0, 1));
            const int five = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 5);
            const int eight = TbStyle(0.0f, 1, 1, 0, -1.0f, -1.0f, 3, 0.02f, 0.03f, 0.01f, 8);
            second->lines[0]->words[0]->style = static_cast<uint8_t>(five);
            WM(lp.lpNext, lb.NextJustified(0, 1));
            second->lines[0]->words[0]->style = static_cast<uint8_t>(eight);
            WM(lp.lpNext, !lb.NextJustified(0, 1));
            ReleaseTextStyle(eight);
            ReleaseTextStyle(five);
            for (S_TextPage *p : lb.m_pages)
                delete p;
            lb.m_pages.clear();
        }

        // --- the box overflows: a line's room negative ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 200.0f)};
            lb.m_overflows = false;
            WM(lp.lpOverflow, LpLayout(lb, {{"aa", own}, {"aaaaaaaaaaaaaaaaaa", noWrap}}) && lb.m_overflows);
            lb.m_overflows = false;
            WM(lp.lpOverflow, LpLayout(lb, {{"aaaaaaaaaaaaaaaa", noWrap}}) && !lb.m_overflows &&
                                  LpPages(lb) == "[0 164: (50 6 18 12 4 aaaaaaaaaaaaaaaa/96@10)]");
            WM(lp.lpOverflow, LpLayout(lb, {{"aaaaaaaaaaaaaaaaa", noWrap}, {"aa", own}}) && lb.m_overflows);
            lb.m_overflows = false;
        }

        // --- laid out again: the earlier pages kept, the new lines after them (quirk: FreePages empty) ---
        {
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 80.0f), LpRect(300.0f, 20.0f, 100.0f, 80.0f)};
            WM(lp.lpAppend, LpLayout(lb, {{"aa", st}}) && LpPages(lb) == "[0 44: (50 6 18 12 88 aa/12@10)]");
            lb.Clear();
            lb.AddText(C_str("bb"), static_cast<uint8_t>(st));
            lb.Relayout();
            WM(lp.lpAppend, LpPages(lb) == "[0 8: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)]");
            lb.Relayout();
            WM(lp.lpAppend, LpPages(lb) == "[0 8: (50 6 18 12 88 aa/12@10) (86 6 18 12 88 bb/12@10)][1 44: (50 6 18 12 88 bb/12@300)]");
            // (Every page placed again: one moved by hand comes back.)
            lb.m_pages[0]->lines[0]->y = 7;
            lb.m_pages[0]->lines[0]->words[0]->x = 7;
            lb.m_pages[0]->overflows = true;
            lb.Relayout();
            WM(lp.lpAppend, lb.NumPages() == 2 && lb.m_pages[1]->lines.size() == 2 && lb.m_pages[0]->lines[0]->y == 50 && lb.m_pages[0]->lines[0]->words[0]->x == 10 &&
                                !lb.m_pages[0]->overflows);
        }

        // --- the unicode font: by UTF-8 characters (not UTF-8: the byte, signed); a word wider than a line
        // keeps its part's width and the next character ---
        {
            TbUnicode(true);
            lb.m_areas = {LpRect(10.0f, 20.0f, 40.0f, 100.0f)};
            WM(lp.lpUtf8, LpLayout(lb, {{"aa aa aa", st}}) &&
                              LpPages(lb) == "[0 28: (50 6 18 12 11 aa aa/29@10) (86 6 18 12 28 aa/12@10)]");
            lb.m_areas = {LpRect(10.0f, 20.0f, 20.0f, 100.0f)};
            WM(lp.lpUtf8, LpLayout(lb, {{"aaaaa", st}}) && LpPages(lb) == "[0 28: (50 6 18 12 2 aaa/18@10) (86 6 18 12 8 aa/12@10)]");
            WM(lp.lpUtf8, LpLayout(lb, {{" aaaaa", st}}) && LpPages(lb) == "[0 28: (50 6 18 12 3  aa/17@10) (86 6 18 12 2 aaa/18@10)]");
            WM(lp.lpUtf8, LpLayout(lb, {{"\xD0\x96\xD0\x96", st}}) &&
                              LpPages(lb) == "[0 28: (50 6 18 12 8 \xD0\x96/12@10) (86 6 18 12 8 \xD0\x96/12@10)]");
            lb.m_areas = {LpRect(10.0f, 20.0f, 100.0f, 100.0f)};
            font3.asked.clear();
            WM(lp.lpUtf8, LpLayout(lb, {{"\xFF" "a\xD0\x96", st}}) && font3.asked == (std::vector<int>{-1, -1, 'a', 'a', 0x416, 0x416}) &&
                              LpPages(lb) == "[0 64: (50 6 18 12 76 \xFF" "a\xD0\x96/24@10)]");
            // (A blank in UTF-8 after a character of two bytes: its index the byte's.)
            lb.m_areas = {LpRect(10.0f, 20.0f, 28.0f, 100.0f)};
            WM(lp.lpUtf8, LpLayout(lb, {{"\xD0\x96 aa", st}}) && LpPages(lb) == "[0 28: (50 6 18 12 16 \xD0\x96/12@10) (86 6 18 12 16 aa/12@10)]");
            lb.m_areas = {LpRect(10.0f, 20.0f, 30.0f, 100.0f)};
            WM(lp.lpUtf8, LpLayout(lb, {{"aaaaaaa", noWrap}}) && LpPages(lb) == "[0 64: (50 6 18 12 -12 aaaaaaa/42@10)]");
            lb.m_areas = {LpRect(10.0f, 20.0f, 6.0f, 100.0f)};
            WM(lp.lpUtf8, !LpLayout(lb, {{"a", st}}) && LpPages(lb) == "[0 100:]");
            TbUnicode(false);
        }

        for (S_TextPage *page : lb.m_pages)
            delete page;
        lb.m_pages.clear();
        for (const int style : {ownJustified, justified, atX, none, right, centred, big, noWrapRight, noWrap, newPage, newLine, own, st})
            ReleaseTextStyle(style);
    }
    std::printf("paged: lpNone %d lpLines %d lpWrap %d lpPages %d lpAreas %d lpForced %d lpFail %d lpNoWrap %d lpMeasures %d "
                "lpJoins %d lpBreak %d lpStack %d lpAlign %d lpJustify %d lpNext %d lpOverflow %d lpAppend %d lpUtf8 %d\n",
                lp.lpNone, lp.lpLines, lp.lpWrap, lp.lpPages, lp.lpAreas, lp.lpForced, lp.lpFail, lp.lpNoWrap, lp.lpMeasures,
                lp.lpJoins, lp.lpBreak, lp.lpStack, lp.lpAlign, lp.lpJustify, lp.lpNext, lp.lpOverflow, lp.lpAppend, lp.lpUtf8);

    box.m_fonts->m_fonts.erase(box.m_fonts->m_fonts.begin(), box.m_fonts->m_fonts.begin() + 2);
    g_textIds = oldIds;
    g_igraphVtable[0x18 / 4] = oldX;
    g_igraphVtable[0x1C / 4] = oldY;
    std::printf("textbox: tbNew %d tbRuns %d tbVersion %d tbInsert %d tbRemove %d tbEdit %d tbCaret %d tbSelect %d tbHover %d "
                "tbRange %d tbScrollTo %d tbCentre %d tbRect %d tbAttach %d tbDepth %d tbFree %d\n",
                r.tbNew, r.tbRuns, r.tbVersion, r.tbInsert, r.tbRemove, r.tbEdit, r.tbCaret, r.tbSelect, r.tbHover, r.tbRange,
                r.tbScrollTo, r.tbCentre, r.tbRect, r.tbAttach, r.tbDepth, r.tbFree);
}
