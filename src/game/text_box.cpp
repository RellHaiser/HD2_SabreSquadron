#include "game/text_box.h"

#include <cmath>

#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

// --- the text box's runs, place and scroll (part 9b-i) ---------------------------------------------

namespace {

// The run style whose text is a format of the game's version.
constexpr int STYLE_VERSION = 0x28;
// The caret: on for half a second of each.
constexpr int CARET_ON = 500;
constexpr int CARET_PERIOD = 1000;
// AttachTo: the interface's y scale (4 / 3), the margin's unit (1 / 1024 of the screen's width).
constexpr float INTERFACE_Y_SCALE = 1.3333334f;
constexpr float MARGIN_UNIT = 1.0f / 1024.0f;
// VisualDepth: the visual's distance less this.
constexpr float DEPTH_OFFSET = 0.1f;

} // namespace

S_TextLine::~S_TextLine()
{
    while (!words.empty()) {
        delete words.back();
        words.pop_back();
    }
}

C_TextBox::C_TextBox()
{
    m_screenWidth = static_cast<int16_t>(GetIGraph()->ScrnX());
    m_screenHeight = static_cast<int16_t>(GetIGraph()->ScrnY());
    m_fonts = AcquireFontManager();
}

C_TextBox::~C_TextBox()
{
    m_fonts->Release();
    FreeLayout();
}

void C_TextBox::UpdateScrollRange()
{
    if (m_lines.empty())
        m_scrollRange = 0;
    else
        m_scrollRange = static_cast<int>(static_cast<float>(m_lines.back()->y) - m_top + 1.0f);
    if (m_scrollRange < 0)
        m_scrollRange = 0;
    if (m_scrollRange > 0)
        ++m_scrollRange;
    if (m_scroll > m_scrollRange)
        m_scroll = m_scrollRange;
    if (m_scroll < 0)
        m_scroll = 0;
    if (m_scrollRange != 0)
        m_scrolls = true;
}

bool C_TextBox::ScrollTo(int line)
{
    if (line > m_scrollRange)
        return false;
    m_scroll = line;
    return true;
}

bool C_TextBox::AddRun(S_TextRun run)
{
    m_runs.push_back(run);
    m_dirty = true;
    return true;
}

bool C_TextBox::SetRun(S_TextRun run, int index)
{
    if (index < 0 || index > NumRuns() - 1)
        return AddRun(run);
    m_runs[index] = run;
    m_dirty = true;
    return true;
}

bool C_TextBox::InsertRun(S_TextRun run, int index)
{
    if (index < 0 || index > NumRuns() - 1)
        return AddRun(run);
    m_runs.insert(m_runs.begin() + index, run);
    m_dirty = true;
    return true;
}

bool C_TextBox::AddText(const C_str &text, uint8_t style)
{
    return AddRun({text, style});
}

bool C_TextBox::AddTextId(int textId, uint8_t style)
{
    return AddRun({g_texts.Get(textId), style});
}

bool C_TextBox::SetText(const C_str &text, uint8_t style, int index)
{
    return SetRun({text, style}, index);
}

bool C_TextBox::SetTextId(int textId, int style, int index)
{
    C_str text = g_texts.Get(textId);
    if (style == STYLE_VERSION) {
        int major = 0, minor = 0;
        GetGameVersion(&major, &minor);
        text = C_fstr(text, major, minor);
    }
    return SetRun({text, static_cast<uint8_t>(style)}, index);
}

bool C_TextBox::InsertText(const C_str &text, uint8_t style, int index)
{
    return InsertRun({text, style}, index);
}

bool C_TextBox::InsertTextId(int textId, uint8_t style, int index)
{
    return InsertRun({g_texts.Get(textId), style}, index);
}

bool C_TextBox::RemoveRun(int index)
{
    m_dirty = true;
    if (index < 0 || index > NumRuns() - 1)
        return false;
    m_runs.erase(m_runs.begin() + index);
    return true;
}

void C_TextBox::Clear()
{
    m_runs.clear();
    m_dirty = true;
}

C_str C_TextBox::GetRunText(int index) const
{
    if (index < 0 || index > NumRuns() - 1)
        return C_str();
    return m_runs[index].text;
}

bool C_TextBox::EditRun(int index)
{
    if (index < 0 || index > NumRuns() - 1) {
        m_selection = index;
        m_editing = false;
        m_dirty = true;
        return true;
    }
    if (m_editing)
        return false;
    m_selection = index;
    m_caretTime = 0;
    m_editing = true;
    m_dirty = true;
    return true;
}

void C_TextBox::TickCaret(int time)
{
    m_caretOn = false;
    if (!m_editing)
        return;
    m_caretTime += time;
    if (m_caretTime < CARET_ON)
        m_caretOn = true;
    if (m_caretTime > CARET_PERIOD)
        m_caretTime = 0;
}

int16_t C_TextBox::GetSelection()
{
    if (m_runs.empty())
        return -1;
    if (m_selection < 0) {
        m_selection = 0;
        return 0;
    }
    if (m_selection > NumRuns() - 1)
        m_selection = NumRuns() - 1;
    return static_cast<int16_t>(m_selection);
}

int16_t C_TextBox::SelectRun(int index)
{
    if (m_editing || index < 0 || index > NumRuns() - 1)
        return -1;
    // (Quirk kept: the run's index taken as a line's.)
    ScrollToLine(index);
    m_selection = index;
    return static_cast<int16_t>(index);
}

C_str C_TextBox::GetSelectedText() const
{
    return GetRunText(m_selection);
}

bool C_TextBox::SetSelectedText(const C_str &text)
{
    if (m_selection < 0 || m_selection > NumRuns() - 1)
        return false;
    m_runs[m_selection].text = text;
    m_dirty = true;
    return true;
}

bool C_TextBox::SetHover(int line)
{
    if (m_editing || line < 0 || line > static_cast<int>(m_lines.size()) - 1)
        return false;
    m_hover = line;
    return true;
}

bool C_TextBox::HoverAt(int x, int y)
{
    if (m_editing)
        return false;
    const int16_t line = LineAt(x, y);
    m_hover = line;
    return line >= 0;
}

bool C_TextBox::ClearHover()
{
    if (m_editing)
        return false;
    m_hover = -1;
    return true;
}

int16_t C_TextBox::SelectHovered()
{
    if (m_editing)
        return -1;
    if (m_hover != -1)
        ScrollToLine(m_hover);
    m_selection = m_hover;
    return static_cast<int16_t>(m_hover);
}

int16_t C_TextBox::LineAt(int, int y) const
{
    const int at = static_cast<int16_t>(static_cast<int16_t>(m_scroll) + y);
    for (size_t i = m_lines.size(); i-- > 0;) {
        const S_TextLine *line = m_lines[i];
        if (at <= line->y + line->above && line->y - line->gap - line->below < at)
            return static_cast<int16_t>(i);
    }
    return -1;
}

bool C_TextBox::ScrollToLine(int index)
{
    if (m_dirty)
        Layout();
    const S_TextLine *line = m_lines[index];
    if (!(static_cast<float>(line->above + line->below + line->gap) < m_height))
        return false;
    if (m_top <= static_cast<float>(line->y - m_scroll + line->above)) {
        m_scroll = line->y - static_cast<int>(m_top + 1.0f) + line->above;
        return true;
    }
    const int bottom = line->y - line->below - line->gap;
    if (static_cast<float>(bottom - m_scroll) < m_bottom) {
        m_scroll = bottom - static_cast<int>(m_bottom);
        return true;
    }
    return false;
}

int16_t C_TextBox::GetScrollRange()
{
    if (m_dirty)
        Layout();
    return static_cast<int16_t>(m_scrollRange);
}

S_vector4 C_TextBox::GetLineCentre(int index)
{
    if (m_dirty)
        Layout();
    // (Fix: w 0; the original's is its stack's.)
    S_vector4 centre = {0.0f, 0.0f, 0.0f, 0.0f};
    if (index < 0 || index > static_cast<int>(m_lines.size()) - 1)
        return centre;
    const float scroll = static_cast<float>(m_scroll);
    const S_TextLine *line = m_lines[index];
    const int y = line->y;
    if (line->above - static_cast<int>(scroll + m_top + 1.0f) + y > 0)
        return centre;
    if (y - static_cast<int>(scroll + m_bottom) - line->below - line->gap < 0)
        return centre;
    // (The y over the screen's width too.)
    centre.x = (m_right + m_left) * 0.5f / static_cast<float>(m_screenWidth);
    centre.y = (static_cast<float>(y) - static_cast<float>(line->gap) * 0.5f - scroll) / static_cast<float>(-m_screenWidth);
    return centre;
}

float C_TextBox::VisualDepth(I3D_scene *scene, I3D_frame *visual)
{
    if (!scene)
        return 0.0f;
    I3D_camera *camera = scene->GetActiveCamera();
    if (!visual || !camera)
        return -1.0f;
    float nearRange, farRange;
    camera->GetRange(nearRange, farRange);
    const float cameraZ = camera->GetWorldPos().z;
    const float depth = (std::fabs(visual->GetWorldPos().z - cameraZ) - DEPTH_OFFSET - nearRange) / (farRange - nearRange);
    if (depth > 1.0f || !(depth >= 0.0f))
        return -1.0f;
    return depth;
}

void C_TextBox::AttachTo(I3D_scene *scene, I3D_frame *visual)
{
    if (!visual)
        return;
    const I3D_bbox box = static_cast<I3D_visual *>(visual)->GetWorldBound();
    const float width = static_cast<float>(m_screenWidth);
    const float height = static_cast<float>(m_screenHeight);
    const float margin = static_cast<float>(static_cast<int>(width * MARGIN_UNIT * static_cast<float>(m_margin)));
    m_left = width * box.min.x + margin;
    m_top = height * box.min.y * -INTERFACE_Y_SCALE - 1.0f;
    m_right = width * box.max.x - margin;
    m_bottom = 1.0f - height * box.max.y * INTERFACE_Y_SCALE;
    m_width = m_right - m_left;
    m_height = m_top - m_bottom;
    m_depth = VisualDepth(scene, visual);
    m_dirty = true;
    m_placed = true;
}

void C_TextBox::SetRect(const float *rect)
{
    m_left = rect[0];
    m_top = rect[1];
    m_right = rect[2];
    m_bottom = rect[3];
    m_width = rect[4];
    m_height = rect[5];
    m_depth = rect[6];
    m_dirty = true;
    m_placed = true;
}

void C_TextBox::FreeLayout()
{
    for (size_t i = m_lines.size(); i-- > 0;)
        delete m_lines[i];
    m_lines.clear();
    for (size_t i = m_pieces.size(); i-- > 0;)
        delete m_pieces[i];
    m_pieces.clear();
}
