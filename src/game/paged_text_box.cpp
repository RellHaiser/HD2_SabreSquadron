#include "game/paged_text_box.h"

#include <cctype>

#include "game/profile.h"
#include "ls3d/igraph.h"

// --- the paged box: its areas, its pages shown and its render (part 9b-iv) -------------------------

namespace {

// RenderPage: a dimmed box's alpha.
constexpr uint32_t DIMMED_ALPHA = 0x60000000;
// ClipArea: the area extended up.
constexpr float CLIP_TOP_MARGIN = 5.0f;

} // namespace

S_TextPage::~S_TextPage()
{
    for (size_t i = lines.size(); i-- > 0;)
        delete lines[i];
}

C_PagedTextBox::~C_PagedTextBox()
{
    // The fix: the original's pages are never deleted (its FreePages, 0x0064ae60, is empty; the
    // vector's memory alone freed), nor their lines.
    for (size_t i = m_pages.size(); i-- > 0;)
        delete m_pages[i];
}

void C_PagedTextBox::Render()
{
    if (!m_placed)
        return;
    m_batching = m_fonts->IsBatching();
    if (m_dirty)
        LayoutPages();
    for (size_t i = 0; i < m_areas.size(); ++i) {
        const int page = m_page + static_cast<int>(i);
        if (page >= 0 && page <= NumPages() - 1)
            RenderPage(m_pages[page]);
    }
}

void C_PagedTextBox::RenderPage(const S_TextPage *page)
{
    S_IGraphViewport screen = {};
    if (page->overflows) {
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        screen = *GetIGraph()->GetViewport();
#endif
        S_IGraphViewport clip = {};
        if (!ClipArea(&clip, m_areas[page->area]))
            return;
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->SetViewport(clip);
#endif
    }
    // A word without its style ends the lines after its own (the line's other words still drawn);
    // one not drawn (the font size 0) at once.
    bool stop = false;
    for (size_t i = 0; i < page->lines.size() && !stop; ++i) {
        const S_TextLine *line = page->lines[i];
        m_penY = line->y;
        for (const S_TextWord *word : line->words) {
            if (!SelectStyle(word->style)) {
                stop = true;
                continue;
            }
            // (WordColour's colours without the hovered line's.)
            uint32_t colour = m_colour;
            if (colour == 0)
                colour = m_dimmed ? (m_style->color & 0xFFFFFF) | DIMMED_ALPHA : m_style->color;
            m_penX = word->x;
            if (!DrawString(word->text, colour)) {
                stop = true;
                break;
            }
        }
    }
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    if (page->overflows)
        GetIGraph()->SetViewport(screen);
#endif
}

bool C_PagedTextBox::ClipArea(S_IGraphViewport *viewport, const S_TextArea &area) const
{
    const float screenWidth = static_cast<float>(m_screenWidth);
    const float screenHeight = static_cast<float>(m_screenHeight);
    // Each edge kept on the screen (as ClipViewport's, without its extra pixel).
    float left = area.left, top = area.top + CLIP_TOP_MARGIN, right = area.right, bottom = area.bottom;
    if (right > screenWidth)
        right = screenWidth;
    else if (right < 0.0f)
        right = 0.0f;
    if (bottom > screenHeight)
        bottom = screenHeight;
    else if (bottom < 0.0f)
        bottom = 0.0f;
    if (left > screenWidth)
        left = screenWidth;
    else if (left < 0.0f)
        left = 0.0f;
    if (top > screenHeight)
        top = screenHeight;
    else if (top < 0.0f)
        top = 0.0f;
    const float width = right - left;
    viewport->x = static_cast<int>(left);
    viewport->y = static_cast<int>(bottom);
    viewport->height = static_cast<int>(top - bottom);
    viewport->width = static_cast<int>(width);
    viewport->maxZ = 1.0f;
    viewport->minZ = 0.0f;
    return viewport->height > 0 && viewport->width > 0;
}

void C_PagedTextBox::AddArea(I3D_scene *scene, I3D_frame *visual)
{
    if (!visual)
        return;
    AttachTo(scene, visual);
    m_areas.push_back({m_left, m_top, m_right, m_bottom, m_width, m_height, m_depth});
}

void C_PagedTextBox::SetPage(int page)
{
    if (page == 0 || (page >= 0 && page < NumPages()))
        m_page = page;
}

void C_PagedTextBox::Relayout()
{
    m_dirty = true;
    LayoutPages();
}

int C_PagedTextBox::LastPageBreak() const
{
    for (int i = NumPages(); i-- > 0;) {
        const S_TextPage *page = m_pages[i];
        if (page->lines.empty() || page->lines[0]->words.empty())
            continue;
        LoadTextStyles();
        if (GetTextStyle(page->lines[0]->words[0]->style)->flags30 & STYLE_NEW_PAGE)
            return i;
    }
    return -1;
}

// --- the paged box's layout: the runs into lines, the lines into pages (part 9b-iv-b) --------------

namespace {

// A flow's measures from the selected style, in pixels: the font size, the space above and below a
// line's base.
struct S_PageMeasures {
    int size, above, below;
};

S_PageMeasures PageMeasures(const S_TextStyleDesc &style, int screenHeight)
{
    const float height = static_cast<float>(screenHeight);
    S_PageMeasures m;
    m.size = static_cast<int>(height * style.size + 0.5f);
    m.above = static_cast<int>(height * style.above + 0.5f);
    m.below = static_cast<int>(height * style.below + 0.5f);
    return m;
}

} // namespace

bool C_PagedTextBox::LayoutPages()
{
    if (m_runs.empty() || m_areas.empty())
        return false;
    bool joined = false;
    S_TextLine *line = nullptr;                         // the line being filled (not yet on a page)
    for (size_t i = 0; i < m_runs.size(); ++i) {
        const S_TextRun run = m_runs[i];
        // (Fix: the line being filled deleted when the layout fails; the original leaks it.)
        if (!SelectStyle(run.style)) {
            delete line;
            return false;
        }
        if (!LastPage())
            NewPage();
        if ((m_style->flags30 & STYLE_NEW_PAGE) && !LastPage()->lines.empty()) {
            if (line)
                AddLineToPage(line);
            line = nullptr;
            NewPage();
        }
        // A line of its own, or joined to the last (a new line for the first, or asked for). (Fix: a
        // joined run after a page break gets a new line; the original flows it into a null line.)
        if (!m_style->joins) {
            joined = false;
            if (line)
                AddLineToPage(line);
            line = NewLine();
        } else if ((m_style->flags30 & STYLE_NEW_LINE) || !joined || !line) {
            joined = true;
            if (line)
                AddLineToPage(line);
            line = NewLine();
        }
        if (!FlowPaged(run, line)) {
            delete line;
            return false;
        }
    }
    if (line)
        AddLineToPage(line);
    PlacePages();
    m_dirty = false;
    return true;
}

S_TextPage *C_PagedTextBox::LastPage() const
{
    return m_pages.empty() ? nullptr : m_pages.back();
}

void C_PagedTextBox::NewPage()
{
    auto *page = new S_TextPage();
    page->area = static_cast<int>(m_pages.size() % m_areas.size());
    page->room = static_cast<int>(m_areas[page->area].height);
    m_pages.push_back(page);
}

S_TextLine *C_PagedTextBox::NewLine() const
{
    auto *line = new S_TextLine();                      // (measures -1: the original resets it too)
    line->remaining = static_cast<int16_t>(static_cast<int>(m_areas[LastPage()->area].width));
    return line;
}

void C_PagedTextBox::AddLineToPage(S_TextLine *line)
{
    const int height = line->gap + line->below + line->above;
    const S_TextPage *last = LastPage();
    if (!last || last->room < height)
        NewPage();
    S_TextPage *page = LastPage();
    page->lines.push_back(line);
    page->room -= height;
}

bool C_PagedTextBox::FlowPaged(const S_TextRun &run, S_TextLine *&line)
{
    if (g_gameProfile.UsesUnicodeFont())
        return FlowPagedUtf8(run, line);
    return FlowPagedBytes(run, line);
}

// The words go on the line by C_TextBox::AddWord with the style's space below as the line's above and
// its above as the line's below (quirk kept: the plain box's layout has them the other way round).

bool C_PagedTextBox::FlowPagedBytes(const S_TextRun &run, S_TextLine *&line)
{
    C_str text = run.text;
    int room = line->remaining;                         // (not cut to the style's column)
    const S_PageMeasures m = PageMeasures(*m_style, m_screenHeight);
    if (!m_style->wraps) {
        AddWord(line, text, run.style, TextWidth(text, FontSize()), m.size, m.below, m.above);
        return true;
    }
    // Wrapped at the last blank that fits (one first in the text or after a wrap does not count).
    int lastSpace = 0, spaceX = 0, x = 0;
    for (int i = 0; i < static_cast<int>(text.Size());) {
        const uint8_t ch = static_cast<uint8_t>(text.c_str()[i]);
        const int next = Advance(ch, m.size) + x;
        if (next < room) {
            if (ch < 0x80 && std::isspace(ch)) {
                spaceX = x;
                lastSpace = i;
            }
            x = next;
            ++i;
            continue;
        }
        if (lastSpace != 0) {
            AddWord(line, C_str(text.c_str(), static_cast<size_t>(lastSpace)), run.style, spaceX, m.size, m.below,
                    m.above);
            text = C_str(text.c_str() + lastSpace + 1);
        } else {
            // Not even one character fits an empty line: the layout fails.
            if (x == 0)
                return false;
            // A word wider than a line: the part that fits (no width: no blank was seen), the
            // character that did not fit dropped (quirk kept).
            AddWord(line, C_str(text.c_str(), static_cast<size_t>(i)), run.style, spaceX, m.size, m.below, m.above);
            text = C_str(text.c_str() + i + 1);
        }
        x = 0;
        spaceX = 0;
        lastSpace = 0;
        i = 0;
        AddLineToPage(line);
        line = NewLine();
        room = line->remaining;
    }
    AddWord(line, text, run.style, x, m.size, m.below, m.above);
    return true;
}

bool C_PagedTextBox::FlowPagedUtf8(const S_TextRun &run, S_TextLine *&line)
{
    C_str text = run.text;
    int room = line->remaining;
    const S_PageMeasures m = PageMeasures(*m_style, m_screenHeight);
    if (!m_style->wraps) {
        AddWord(line, text, run.style, TextWidth(text, FontSize()), m.size, m.below, m.above);
        return true;
    }
    // As FlowPagedBytes by characters (not UTF-8: the byte, signed), but a word wider than a line
    // keeps its part's width and the character that did not fit.
    int lastSpace = 0, spaceX = 0, x = 0;
    for (int i = 0; i < static_cast<int>(text.Size());) {
        uint32_t ch;
        int bytes = DecodeUtf8(&ch, text.c_str() + i, static_cast<int>(text.Size()) - i);
        if (bytes < 1) {
            ch = static_cast<uint32_t>(static_cast<int>(static_cast<signed char>(text.c_str()[i])));
            bytes = 1;
        }
        const int next = Advance(static_cast<int>(ch), m.size) + x;
        if (next < room) {
            if (ch < 0x80 && std::isspace(static_cast<int>(ch))) {
                spaceX = x;
                lastSpace = i;
            }
            x = next;
            i += bytes;
            continue;
        }
        if (lastSpace != 0) {
            AddWord(line, C_str(text.c_str(), static_cast<size_t>(lastSpace)), run.style, spaceX, m.size, m.below,
                    m.above);
            text = C_str(text.c_str() + lastSpace + 1);
        } else {
            if (x == 0)
                return false;
            AddWord(line, C_str(text.c_str(), static_cast<size_t>(i)), run.style, x, m.size, m.below, m.above);
            text = C_str(text.c_str() + i);
        }
        x = 0;
        spaceX = 0;
        lastSpace = 0;
        i = 0;
        AddLineToPage(line);
        line = NewLine();
        room = line->remaining;
    }
    AddWord(line, text, run.style, x, m.size, m.below, m.above);
    return true;
}

void C_PagedTextBox::PlacePages()
{
    for (size_t i = 0; i < m_pages.size(); ++i) {
        m_pages[i]->overflows = false;
        StackPage(static_cast<int>(i));
        AlignPage(static_cast<int>(i));
    }
}

void C_PagedTextBox::StackPage(int index)
{
    S_TextPage *page = m_pages[index];
    const S_TextArea &area = m_areas[page->area];
    int y = static_cast<int>(area.bottom);
    for (S_TextLine *line : page->lines) {
        y += line->below + line->gap;
        line->y = static_cast<int16_t>(y);
        y += line->above;
    }
    if (!page->lines.empty() && static_cast<float>(page->lines.back()->y) > area.top)
        page->overflows = true;
}

void C_PagedTextBox::AlignPage(int index)
{
    const S_TextPage *page = m_pages[index];
    for (size_t k = 0; k < page->lines.size(); ++k) {
        if (page->lines[k]->words.empty())
            continue;
        AlignPageLine(index, static_cast<int>(k), !NextJustified(index, static_cast<int>(k)));
        if (page->lines[k]->remaining < 0)
            m_overflows = true;
    }
}

bool C_PagedTextBox::NextJustified(int pageIndex, int lineIndex) const
{
    const S_TextPage *page = m_pages[pageIndex];
    const S_TextLine *next = nullptr;
    if (lineIndex + 1 >= 0 && lineIndex + 1 < static_cast<int>(page->lines.size()))
        next = page->lines[lineIndex + 1];
    else if (pageIndex + 1 >= 0 && pageIndex + 1 < NumPages() && !m_pages[pageIndex + 1]->lines.empty())
        next = m_pages[pageIndex + 1]->lines[0];
    if (!next || next->words.empty())
        return false;
    LoadTextStyles();
    return (GetTextStyle(next->words[0]->style)->align >> 2) & 1;
}

void C_PagedTextBox::AlignPageLine(int pageIndex, int lineIndex, bool last)
{
    const S_TextPage *page = m_pages[pageIndex];
    S_TextLine *line = page->lines[lineIndex];
    float x = m_areas[page->area].left;
    LoadTextStyles();
    const S_TextStyleDesc *style = GetTextStyle(line->words[0]->style);
    if (style->flags30 & STYLE_AT_X)
        return;
    if (style->align == 1 || line->remaining <= 0) {
        for (S_TextWord *word : line->words) {
            word->x = static_cast<int16_t>(static_cast<int>(x));
            x = static_cast<float>(word->width) + x;
        }
        return;
    }
    float gap = 0.0f;
    switch (style->align) {
    case 2:
        x = static_cast<float>(line->remaining / 2) + x;
        break;
    case 3:
        x = static_cast<float>(line->remaining) + x;
        break;
    case 4:
        if (!last)
            SplitWords(line);
        if (line->words.size() > 1 && !last)
            gap = JustifyPageGap(page, line);
        break;
    default:
        return;
    }
    for (S_TextWord *word : line->words) {
        word->x = static_cast<int16_t>(static_cast<int>(x));
        x = (static_cast<float>(word->width) + gap) + x;
    }
}

float C_PagedTextBox::JustifyPageGap(const S_TextPage *page, const S_TextLine *line) const
{
    const int count = static_cast<int>(line->words.size());
    if (count <= 1)
        return 0.0f;
    int width = 0;
    for (const S_TextWord *word : line->words)
        width += word->width;
    return (m_areas[page->area].width - static_cast<float>(width)) / static_cast<float>(count - 1);
}
