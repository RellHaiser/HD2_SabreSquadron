#include "game/paged_text_box.h"

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
        screen = *GetIGraph()->GetViewport();
        S_IGraphViewport clip = {};
        if (!ClipArea(&clip, m_areas[page->area]))
            return;
        GetIGraph()->SetViewport(clip);
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
    if (page->overflows)
        GetIGraph()->SetViewport(screen);
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
