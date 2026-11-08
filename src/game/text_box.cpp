#include "game/text_box.h"

#include <cctype>
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
// Advance: a blank glyph's width (of the font size).
constexpr double BLANK_WIDTH = 0.4;

// UTF-8 (Unicode, Inc.'s ConvertUTF.c, 0x00813ba0 / 0x00813ca0): a lead byte's trailing bytes, and
// what the sum of a sequence's shifted bytes is over its code point.
const int8_t UTF8_TRAILING[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5,
};
const uint32_t UTF8_OFFSETS[6] = {0x00000000, 0x00003080, 0x000E2080, 0x03C82080, 0xFA082080, 0x82082080};

// A sequence of `length` bytes well formed (ConvertUTF.c's isLegalUTF8 without its surrogates' test).
bool IsLegalUtf8(const uint8_t *source, int length)     // 0x0064d400 (__fastcall)
{
    const uint8_t *p = source + length;
    uint8_t a;
    switch (length) {
    default:
        return false;
    case 4:
        if ((a = *--p) < 0x80 || a > 0xBF)
            return false;
        [[fallthrough]];
    case 3:
        if ((a = *--p) < 0x80 || a > 0xBF)
            return false;
        [[fallthrough]];
    case 2:
        if ((a = *--p) > 0xBF)
            return false;
        switch (*source) {
        case 0xE0:
            if (a < 0xA0)
                return false;
            break;
        case 0xF0:
            if (a < 0x90)
                return false;
            break;
        case 0xF4:
            if (a > 0x8F)
                return false;
            break;
        default:
            if (a < 0x80)
                return false;
        }
        [[fallthrough]];
    case 1:
        if (*source >= 0x80 && *source < 0xC2)
            return false;
    }
    return *source <= 0xF4;
}

} // namespace

int DecodeUtf8(uint32_t *codePoint, const char *text, int length)
{
    *codePoint = 0;
    const auto *source = reinterpret_cast<const uint8_t *>(text);
    const int extra = UTF8_TRAILING[*source];
    if (extra >= length)
        return -2;
    if (!IsLegalUtf8(source, extra + 1))
        return -1;
    const uint8_t *p = source;
    uint32_t ch = 0;
    switch (extra) {
    case 3:
        ch += *p++;
        ch <<= 6;
        [[fallthrough]];
    case 2:
        ch += *p++;
        ch <<= 6;
        [[fallthrough]];
    case 1:
        ch += *p++;
        ch <<= 6;
        [[fallthrough]];
    case 0:
        ch += *p++;
    }
    ch -= UTF8_OFFSETS[extra];
    *codePoint = ch > 0x7FFFFFFF ? 0xFFFD : ch;
    return static_cast<int>(p - source);
}

// --- the UTF-8 helpers (part 9b-v) -----------------------------------------------------------------

namespace {

// EncodeUtf8: a sequence's lead byte's mark by its length (ConvertUTF.c's firstByteMark, 0x00813cb8).
const uint8_t UTF8_FIRST_BYTE[7] = {0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC};

} // namespace

bool IsLegalUtf8Range(const uint8_t *begin, const uint8_t *end)
{
    // (Empty: false; the original reads the byte at the end first, which cannot make it true.)
    if (begin >= end)
        return false;
    for (const uint8_t *p = begin;;) {
        const int length = UTF8_TRAILING[*p] + 1;
        if (end - p < length)
            return false;
        if (!IsLegalUtf8(p, length))
            return false;
        p += length;
        if (p >= end)
            return true;
    }
}

int EncodeUtf8(char *out, const uint32_t *codePoint, int maxBytes)
{
    uint32_t ch = *codePoint;
    if (ch >= 0xD800 && ch <= 0xDFFF)
        return -3;
    int length;
    if (ch < 0x80) {
        length = 1;
    } else if (ch < 0x800) {
        length = 2;
    } else if (ch < 0x10000) {
        length = 3;
    } else if (ch < 0x200000) {
        length = 4;
    } else {
        // Quirk kept: the replacement character in 2 bytes (FF BD; ConvertUTF.c's takes 3).
        ch = 0xFFFD;
        length = 2;
    }
    // (Room for a byte more: the caller's terminator.)
    if (length >= maxBytes)
        return -4;
    char *p = out + length;
    switch (length) {
    case 4:
        *--p = static_cast<char>((ch & 0x3F) | 0x80);
        ch >>= 6;
        [[fallthrough]];
    case 3:
        *--p = static_cast<char>((ch & 0x3F) | 0x80);
        ch >>= 6;
        [[fallthrough]];
    case 2:
        *--p = static_cast<char>((ch & 0x3F) | 0x80);
        ch >>= 6;
        [[fallthrough]];
    case 1:
        *--p = static_cast<char>(UTF8_FIRST_BYTE[length] | (ch & 0xFF));
    }
    return length;
}

bool HasUtf8Sequences(const char *text)
{
    const auto *p = reinterpret_cast<const uint8_t *>(text);
    bool multibyte = false;
    while (*p) {
        int length = 0;
        if (*p < 0x80)
            length = 1;
        else if (*p >= 0xC0 && *p < 0xE0)
            length = 2;
        else if (*p >= 0xE0 && *p < 0xF0)
            length = 3;
        else if (*p >= 0xF0 && *p < 0xF8)
            length = 4;
        if (length == 0)
            return false;
        for (int k = 1; k < length; ++k)
            if (p[k] < 0x80 || p[k] >= 0xC0)
                return false;
        if (length > 1)
            multibyte = true;
        p += length;
    }
    return multibyte;
}

bool HasCjkCharacters(const uint16_t *text)
{
    for (; *text; ++text)
        if (*text > 0x3000)
            return true;
    return false;
}

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
        m_overflows = true;
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
        // (The text: "ver. %d.%02d.%d". The third number is the original's global 0x009820f4, which nothing writes: zero.)
        text = C_fstr(text, major, minor, 0);
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
    // (A line that is not there: the fix, false; the original's reads past its lines, SelectRun
    // passing a run's index.)
    if (index < 0 || index >= static_cast<int>(m_lines.size()))
        return false;
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

// --- the layout: the runs into lines of words and into pieces (part 9b-ii) -------------------------

void S_TextLine::Reset()
{
    while (!words.empty()) {
        delete words.back();
        words.pop_back();
    }
    remaining = -1;
    gap = -1;
    y = -1;
    above = -1;
    below = -1;
}

bool C_TextBox::SelectStyle(int style)
{
    LoadTextStyles();
    m_style = GetTextStyle(style);
    if (!m_style)
        return false;
    m_font = m_fonts->FindFont(m_style->font);
    return m_font != nullptr;
}

int C_TextBox::FontSize() const
{
    return static_cast<int>(static_cast<float>(m_screenHeight) * m_style->size + 0.5f);
}

int C_TextBox::Advance(int ch, int size)
{
    if (m_style->flags30 & STYLE_PASSWORD)
        return static_cast<int>(static_cast<float>(size) * m_font->GetGlyph('*')->aspect + 0.5f);
    const float pixels = static_cast<float>(size);
    if (pixels * m_font->GetGlyph(ch)->aspect == 0.0f)
        return static_cast<int>(static_cast<float>(static_cast<float>(size * BLANK_WIDTH) + 0.5));
    // (The glyph asked for again.)
    return static_cast<int>(pixels * m_font->GetGlyph(ch)->aspect + 0.5f);
}

int C_TextBox::TextWidth(const C_str &text, int size)
{
    const int length = static_cast<int>(text.Size());
    int width = 0;
    if (g_gameProfile.UsesUnicodeFont()) {
        for (int i = 0; i < length;) {
            uint32_t ch;
            const int bytes = DecodeUtf8(&ch, text.c_str() + i, length - i);
            if (bytes > 0) {
                i += bytes;
            } else {
                ch = static_cast<uint32_t>(static_cast<signed char>(text.c_str()[i]));
                ++i;
            }
            width += Advance(static_cast<int>(ch), size);
        }
    } else {
        for (int i = 0; i < length; ++i)
            width += Advance(static_cast<uint8_t>(text.c_str()[i]), size);
    }
    return width;
}

C_str C_TextBox::Truncate(const C_str &text, float size, float maxWidth)
{
    const int length = static_cast<int>(text.Size());
    const bool unicode = g_gameProfile.UsesUnicodeFont();
    int width = 0;
    int i = 0;
    while (i < length) {
        int ch = static_cast<uint8_t>(text.c_str()[i]);
        int bytes = 1;
        if (unicode) {
            uint32_t code;
            bytes = DecodeUtf8(&code, text.c_str() + i, length - i);
            if (bytes < 1) {
                code = static_cast<uint32_t>(static_cast<signed char>(text.c_str()[i]));
                bytes = 1;
            }
            ch = static_cast<int>(code);
        }
        width += Advance(ch, static_cast<int>(size));
        if (static_cast<float>(width) > maxWidth)
            break;
        i += bytes;
    }
    if (i == 0 || i >= length)
        return text;
    return C_str(text.c_str(), static_cast<size_t>(i));
}

S_TextLine *C_TextBox::AddLine()
{
    auto *line = new S_TextLine();                      // (measures -1: the original resets it too)
    line->remaining = static_cast<int16_t>(static_cast<int>(m_width));
    m_lines.push_back(line);
    return line;
}

void C_TextBox::AddWord(S_TextLine *line, const C_str &text, uint8_t style, int width, int height, int above, int below)
{
    line->words.push_back(new S_TextWord{text, style, static_cast<int16_t>(width)});
    if (height > line->gap)
        line->gap = static_cast<int16_t>(height);
    if (above > line->above)
        line->above = static_cast<int16_t>(above);
    if (below > line->below)
        line->below = static_cast<int16_t>(below);
    line->remaining = static_cast<int16_t>(line->remaining - width);
}

int C_TextBox::FlowRun(const S_TextRun &run)
{
    if (g_gameProfile.UsesUnicodeFont())
        return FlowUtf8(run);
    return FlowWords(run);
}

namespace {

// A flow's measures from the selected style, in pixels: the font size, the column's width, the room
// in the line (the column's width when it is less), the line's space above and below its base.
struct S_FlowMeasures {
    int size, column, room, above, below;
};

S_FlowMeasures FlowMeasures(const S_TextStyleDesc &style, int screenWidth, int screenHeight, const S_TextLine &line)
{
    const float height = static_cast<float>(screenHeight);
    S_FlowMeasures m;
    m.size = static_cast<int>(height * style.size + 0.5f);
    m.column = static_cast<int>(static_cast<float>(screenWidth) * style.width + 0.5f);
    m.room = line.remaining;
    if (m.column > 0 && m.column < m.room)
        m.room = m.column;
    m.above = static_cast<int>(height * style.above + 0.5f);
    m.below = static_cast<int>(height * style.below + 0.5f);
    return m;
}

} // namespace

int C_TextBox::FlowWords(const S_TextRun &run)
{
    C_str text = run.text;
    S_TextLine *line = m_lines.back();
    S_FlowMeasures m = FlowMeasures(*m_style, m_screenWidth, m_screenHeight, *line);
    if (!m_style->wraps) {
        // One word, cut to the column.
        AddWord(line, text, run.style, TextWidth(text, FontSize()), m.size, m.above, m.below);
        S_TextWord *word = line->words.back();
        if (m_style->width != 0.0f && m.column < word->width) {
            word->text = Truncate(word->text, static_cast<float>(FontSize()), static_cast<float>(m.column));
            word->width = static_cast<int16_t>(TextWidth(word->text, FontSize()));
        }
        return 0;
    }
    // Wrapped at the last blank that fits (none yet: 0 on the first line, -1 on the flow's own; 0 is
    // a blank first too).
    int lastSpace = 0, spaceX = 0, x = 0;
    bool spaceSeen = false;                             // (a blank first: once one is, it stays)
    for (int i = 0; i < static_cast<int>(text.Size());) {
        const uint8_t ch = static_cast<uint8_t>(text.c_str()[i]);
        const int next = Advance(ch, m.size) + x;
        if (next <= m.room) {
            if (ch < 0x80 && std::isspace(ch)) {
                spaceX = x;
                lastSpace = i;
                spaceSeen = true;
            }
            x = next;
            ++i;
            continue;
        }
        // (Fix: a blank first on an empty line with too long a word after it taken as no blank; the
        // original starts new lines without end.)
        if (lastSpace == -1 || (lastSpace == 0 && spaceSeen && line->words.empty())) {
            // A word wider than a line: the part that fits kept (no width), the run's rest dropped.
            AddWord(line, C_str(text.c_str(), static_cast<size_t>(i)), run.style, spaceX, m.size, m.above, m.below);
            return 0;
        }
        if (lastSpace != 0) {
            AddWord(line, C_str(text.c_str(), static_cast<size_t>(lastSpace)), run.style, spaceX, m.size, m.above,
                    m.below);
            text = C_str(text.c_str() + lastSpace + 1);
        }
        line = AddLine();
        m.room = line->remaining;
        if (m.column > 0 && m.column < m.room)
            m.room = m.column;
        lastSpace = -1;
        spaceX = 0;
        x = 0;
        i = 0;
    }
    AddWord(line, text, run.style, x, m.size, m.above, m.below);
    return 0;
}

int C_TextBox::FlowUtf8(const S_TextRun &run)
{
    C_str text = run.text;
    S_TextLine *line = m_lines.back();
    S_FlowMeasures m = FlowMeasures(*m_style, m_screenWidth, m_screenHeight, *line);
    if (!m_style->wraps) {
        AddWord(line, text, run.style, TextWidth(text, FontSize()), m.size, m.above, m.below);
        S_TextWord *word = line->words.back();
        if (m_style->width != 0.0f && word->width > m.column) {
            word->text = Truncate(word->text, static_cast<float>(FontSize()), static_cast<float>(m.column));
            word->width = static_cast<int16_t>(TextWidth(word->text, FontSize()));
        }
        return 0;
    }
    // Wrapped at the character that does not fit (the blanks not looked for).
    int x = 0;
    for (int i = 0; i < static_cast<int>(text.Size());) {
        uint32_t ch;
        int bytes = DecodeUtf8(&ch, text.c_str() + i, static_cast<int>(text.Size()) - i);
        if (bytes <= 0) {
            ch = static_cast<uint32_t>(static_cast<signed char>(text.c_str()[i]));
            bytes = 1;
        }
        const int next = Advance(static_cast<int>(ch), m.size) + x;
        // (Fix: a character wider than an empty line kept on it; the original starts new lines without
        // end.)
        if (next > m.room && !(i == 0 && line->words.empty())) {
            AddWord(line, C_str(text.c_str(), static_cast<size_t>(i)), run.style, x, m.size, m.above, m.below);
            text = C_str(text.c_str() + i);
            line = AddLine();
            m.room = line->remaining;
            if (m.column > 0 && m.column < m.room)
                m.room = m.column;
            i = 0;
            x = 0;
            continue;
        }
        i += bytes;
        x = next;
    }
    AddWord(line, text, run.style, x, m.size, m.above, m.below);
    return 0;
}

bool C_TextBox::Layout()
{
    FreeLayout();
    if (m_runs.empty())
        return false;
    bool started = false;
    for (size_t i = 0; i < m_runs.size(); ++i) {
        if (!SelectStyle(m_runs[i].style))
            return false;
        const S_TextRun run = m_runs[i];
        if (m_style->x >= 0.0f && m_style->y >= 0.0f) {
            // A piece, cut to its column.
            auto *piece = new S_TextPiece{run.text, run.style};
            m_pieces.push_back(piece);
            piece->height = static_cast<int16_t>(FontSize());
            piece->width = static_cast<int16_t>(TextWidth(run.text, FontSize()));
            if (m_style->width != 0.0f) {
                const float maxWidth = static_cast<float>(m_screenWidth) * m_style->width;
                if (static_cast<float>(piece->width) > maxWidth) {
                    piece->text = Truncate(piece->text, static_cast<float>(piece->height), maxWidth);
                    piece->width = static_cast<int16_t>(TextWidth(piece->text, FontSize()));
                }
            }
            continue;
        }
        // Flowed: a line of its own, or joined to the last (a new line for the first, or asked for).
        if (!m_style->joins) {
            started = false;
            AddLine();
        } else if (m_style->flags30 & STYLE_NEW_LINE) {
            started = true;
            AddLine();
        } else if (!started) {
            started = true;
            AddLine();
        }
        if (FlowRun(run) < 0)
            return false;
    }
    Position();
    m_dirty = false;
    return true;
}

// --- the positioning: the lines' and pieces' places in the box (part 9b-ii-b) ----------------------

bool C_TextBox::Position()
{
    m_overflows = false;
    if (!m_lines.empty()) {
        // (The first run's vertical alignment, whatever its kind.)
        const S_TextStyleDesc *style = GetTextStyle(m_runs[0].style);
        StackLines(static_cast<int>(VAlignOffset(style->valign) + m_bottom));
        PlaceLines();
    }
    if (!m_pieces.empty())
        PlacePieces();
    UpdateScrollRange();
    return true;
}

float C_TextBox::VAlignOffset(int valign) const
{
    const float height = static_cast<float>(TextHeight());
    float offset;
    switch (valign) {
    case 2:
        offset = (m_height - height) * 0.5f;
        break;
    case 3:
        offset = m_height - height;
        break;
    default:
        return 0.0f;
    }
    return offset < 0.0f ? 0.0f : offset;
}

int C_TextBox::TextHeight() const
{
    int height = -m_lines.front()->below;
    for (const S_TextLine *line : m_lines)
        height += line->above + line->gap + line->below;
    return height - m_lines.back()->above;
}

void C_TextBox::StackLines(int bottom)
{
    for (S_TextLine *line : m_lines) {
        bottom += line->below + line->gap;
        line->y = static_cast<int16_t>(bottom);
        bottom += line->above;
    }
}

void C_TextBox::PlaceLines()
{
    for (size_t i = 0; i < m_lines.size(); ++i) {
        S_TextLine *line = m_lines[i];
        if (line->words.empty())
            continue;
        PlaceWordsAtX(line);
        // A paragraph's last line: the next one's first word not justified.
        bool last = true;
        if (i + 1 < m_lines.size()) {
            const S_TextLine *next = m_lines[i + 1];
            if (!next->words.empty() && (GetTextStyle(next->words[0]->style)->align & 4))
                last = false;
        }
        // (The original checks the line's room here again: PlaceWordsAtX did.)
        AlignLine(line, last, static_cast<int>(i));
    }
}

void C_TextBox::PlaceWordsAtX(S_TextLine *line)
{
    for (S_TextWord *word : line->words) {
        const S_TextStyleDesc *style = GetTextStyle(word->style);
        if (!(style->flags30 & STYLE_AT_X))
            continue;
        const float x = style->x * m_width + m_left;
        switch (style->align) {
        case 1:
            word->x = static_cast<int16_t>(static_cast<int>(x));
            break;
        case 2:
            word->x = static_cast<int16_t>(static_cast<int>(x - static_cast<float>(word->width) * 0.5f));
            break;
        case 3:
            word->x = static_cast<int16_t>(static_cast<int>(x - static_cast<float>(word->width)));
            break;
        }
    }
    CheckLineOverflow(line);
}

void C_TextBox::AlignLine(S_TextLine *line, bool last, int index)
{
    float x = m_left;
    const S_TextStyleDesc *style = GetTextStyle(line->words[0]->style);
    if (style->flags30 & STYLE_AT_X)
        return;
    if (m_editing && m_selection == index) {
        // The edited line moved left when its caret does not fit. (The caret measured with the
        // style and font selected last, at the line's style's size.)
        const int caret = Advance('_', static_cast<int>(static_cast<float>(m_screenHeight) * style->size + 0.5f));
        const float room = static_cast<float>(line->remaining - caret);
        if (room <= 0.0f) {
            x = room + x;
            for (S_TextWord *word : line->words) {
                word->x = static_cast<int16_t>(static_cast<int>(x));
                x += static_cast<float>(word->width);
            }
            return;
        }
    }
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
            gap = JustifyGap(line);
        break;
    default:
        return;
    }
    for (S_TextWord *word : line->words) {
        word->x = static_cast<int16_t>(static_cast<int>(x));
        x = (static_cast<float>(word->width) + gap) + x;
    }
}

void C_TextBox::SplitWords(S_TextLine *line)
{
    const bool unicode = g_gameProfile.UsesUnicodeFont();
    const int count = static_cast<int>(line->words.size());
    if (count == 0)
        return;
    // Each word's parts between its blanks added at the end (the blanks dropped; a part after the
    // last blank only when it has a width).
    for (int k = 0; k < count; ++k) {
        if (!SelectStyle(line->words[k]->style))
            return;
        const int size = FontSize();
        const uint8_t style = line->words[k]->style;
        C_str text = line->words[k]->text;
        int width = 0;
        for (int i = 0;;) {
            const int length = static_cast<int>(text.Size());
            if (i >= length) {
                if (length != 0 && width > 0)
                    line->words.push_back(new S_TextWord{text, style, static_cast<int16_t>(width)});
                break;
            }
            int ch = static_cast<uint8_t>(text.c_str()[i]);
            int bytes = 1;
            if (unicode) {
                uint32_t code;
                bytes = DecodeUtf8(&code, text.c_str() + i, length - i);
                if (bytes <= 0) {
                    code = static_cast<uint32_t>(static_cast<signed char>(text.c_str()[i]));
                    bytes = 1;
                }
                ch = static_cast<int>(code);
            }
            const int advance = Advance(ch, size);
            if (static_cast<uint32_t>(ch) < 0x80 && std::isspace(ch)) {
                line->words.push_back(new S_TextWord{C_str(text.c_str(), static_cast<size_t>(i)), style,
                                                     static_cast<int16_t>(width)});
                text = C_str(text.c_str() + i + 1);
                width = 0;
                i = 0;
                continue;
            }
            width += advance;
            i += bytes;
        }
    }
    // The words split dropped. (Quirk kept: of more than one, the last stays, first. Fix: the
    // dropped words deleted; the original leaks them.)
    const int dropped = count == 1 ? 1 : count - 1;
    for (int k = 0; k < dropped; ++k)
        delete line->words[k];
    line->words.erase(line->words.begin(), line->words.begin() + dropped);
}

float C_TextBox::JustifyGap(const S_TextLine *line) const
{
    int width = 0;
    for (const S_TextWord *word : line->words)
        width += word->width;
    return (m_width - static_cast<float>(width)) / static_cast<float>(static_cast<int>(line->words.size()) - 1);
}

void C_TextBox::PlacePieces()
{
    for (S_TextPiece *piece : m_pieces) {
        const S_TextStyleDesc *style = GetTextStyle(piece->style);
        // (In the box's pixels from its corner, not offset by its place.)
        const float x = style->x * m_width + 0.5f;
        const float y = style->y * m_height + 0.5f;
        switch (style->align) {
        case 1:
            piece->x = static_cast<int16_t>(static_cast<int>(x));
            break;
        case 2:
            piece->x = static_cast<int16_t>(static_cast<int>(x - static_cast<float>(piece->width) * 0.5f));
            break;
        case 3:
            piece->x = static_cast<int16_t>(static_cast<int>(x - static_cast<float>(piece->width)));
            break;
        }
        switch (style->valign) {
        case 1:
            piece->y = static_cast<int16_t>(static_cast<int>(y));
            break;
        case 2:
            piece->y = static_cast<int16_t>(static_cast<int>(static_cast<float>(piece->height) * 0.5f + y));
            break;
        case 3:
            piece->y = static_cast<int16_t>(static_cast<int>(static_cast<float>(piece->height) + y));
            break;
        }
        CheckPieceOverflow(piece);
    }
}

bool C_TextBox::CheckLineOverflow(const S_TextLine *line)
{
    if (!m_overflows && line->remaining < 0)
        m_overflows = true;
    return m_overflows;
}

bool C_TextBox::CheckPieceOverflow(const S_TextPiece *piece)
{
    if (!m_overflows &&
        (piece->y < 0 || piece->x < 0 || static_cast<float>(piece->height + piece->y) > m_height ||
         static_cast<float>(piece->width + piece->x) > m_width))
        m_overflows = true;
    return m_overflows;
}

// --- the render (part 9b-iii) ----------------------------------------------------------------------

bool g_textBoxOutline;                                  // 0x008ae600
int g_textBoxStep76;                                    // 0x008ae604

namespace {

// The glyphs' triangles gathered (32 quads' worth), then drawn with the font's texture.
constexpr int MAX_TEXT_VERTICES = 0xC0;
S_FontVertex g_textVertices[MAX_TEXT_VERTICES];         // 0x008acbf8
int g_numTextVertices;                                  // 0x008acbf0
// The quad's depth and 1 / depth.
constexpr float QUAD_Z = 0.0284113996f;
constexpr float QUAD_RHW = 35.1971397f;
// The outline's colour.
constexpr uint32_t OUTLINE_COLOUR = 0x2F005000;
// IGraph::DrawPrimitives' kinds.
constexpr int PRIMITIVE_TRIANGLES = 3;
constexpr int PRIMITIVE_FAN = 4;
// ClipViewport: the box extended up.
constexpr float CLIP_TOP_MARGIN = 5.0f;
// WordColour: a dimmed box's alpha; the hovered line's colour.
constexpr uint32_t DIMMED_ALPHA = 0x60000000;
constexpr uint32_t HOVER_COLOUR = 0xFFFFFFFF;
// DrawCaret: the caret under the base by this of the font size.
constexpr float CARET_DROP = 0.1f;
// The driver's states the quads turn off (3 around the background, 9 around the outline; not
// identified).
constexpr int DRIVER_STATE_3 = 3;
constexpr int DRIVER_STATE_9 = 9;

} // namespace

S_FontVertex g_textBoxQuad[4];                          // 0x008ae410

void SetTextBoxQuad(float x0, float y0, float x1, float y1, uint32_t colour0, uint32_t colour1)
{
    const float xy[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const float uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
    for (int i = 0; i < 4; ++i) {
        S_FontVertex &v = g_textBoxQuad[i];
        v.x = xy[i][0];
        v.y = xy[i][1];
        v.z = QUAD_Z;
        v.rhw = QUAD_RHW;
        v.color = i < 2 ? colour0 : colour1;
        v.u = uv[i][0];
        v.v = uv[i][1];
    }
}

void DrawTextBoxQuad()
{
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    GetIGraph()->DrawPrimitives(PRIMITIVE_FAN, 2, g_textBoxQuad, 1);
#endif
}

void ReadScreenSize(int, int)
{
    GetIGraph()->ScrnX();
    GetIGraph()->ScrnY();
}

void C_TextBox::Render()
{
    if (!m_placed)
        return;
    S_IGraphViewport screen = {};
    S_IGraphViewport clip = {};
    m_batching = m_fonts->IsBatching();
    if (m_dirty)
        Layout();
    if (m_overflows) {
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        screen = *GetIGraph()->GetViewport();
#endif
        ReadScreenSize(screen.x + screen.width, screen.y + screen.height);
        if (!ClipViewport(&clip))
            return;
        ReadScreenSize(clip.x + clip.width, clip.y + clip.height);
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->SetViewport(clip);
#endif
    }
    if (m_background != 0) {
        const bool state = I3DGetDriver()->GetState(DRIVER_STATE_3) != 0;
        I3DGetDriver()->SetState(DRIVER_STATE_3, 0);
        SetTextBoxQuad(m_left, m_bottom, m_right, m_top, m_background, m_background);
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->SetTexture(nullptr, 0);
#endif
        DrawTextBoxQuad();
        I3DGetDriver()->SetState(DRIVER_STATE_3, state);
    }
    StepUnknown76(static_cast<int16_t>(g_textBoxStep76), &m_unknown76);

    // The lines (a word without its style or font size ends them, the pieces still drawn). The
    // caret's colour is the last word's drawn (of any line; none yet: 0).
    uint32_t colour = 0;
    int index = 0;
    for (; index < static_cast<int>(m_lines.size()); ++index) {
        const S_TextLine *line = m_lines[index];
        m_penY = static_cast<int16_t>(line->y - m_scroll);
        bool drawn = true;
        for (const S_TextWord *word : line->words) {
            if (!SelectStyle(word->style)) {
                drawn = false;
                break;
            }
            colour = WordColour(index);
            m_penX = word->x;
            if (m_style->flags30 & STYLE_CENTRED) {
                m_penY = static_cast<int16_t>(m_penY - (line->gap - FontSize()) / 2);
                drawn = DrawString(word->text, colour);
                m_penY = static_cast<int16_t>(line->y - m_scroll);
            } else {
                drawn = DrawString(word->text, colour);
            }
            if (!drawn)
                break;
        }
        if (!drawn)
            break;
        if (m_editing && index == m_selection && m_caretOn)
            DrawCaret(colour);
    }
    // The pieces (coloured as the line the lines stopped at: their count when all were drawn).
    for (const S_TextPiece *piece : m_pieces) {
        if (!SelectStyle(piece->style))
            continue;
        const uint32_t pieceColour = WordColour(index);
        m_penX = static_cast<int16_t>(static_cast<int>(static_cast<float>(piece->x) + m_left));
        m_penY = static_cast<int16_t>(static_cast<int>(static_cast<float>(piece->y) + m_bottom));
        DrawString(piece->text, pieceColour);
    }

    if (m_overflows) {
        ReadScreenSize(screen.x + screen.width, screen.y + screen.height);
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->SetViewport(screen);
#endif
    }
    if (g_textBoxOutline && m_overflows) {
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->SetTexture(nullptr, 0);
#endif
        const uint32_t state = I3DGetDriver()->GetState(DRIVER_STATE_9);
        I3DGetDriver()->SetState(DRIVER_STATE_9, 0);
        // (The clip's numbers as unsigned.)
        const auto pixels = [](int value) { return static_cast<float>(static_cast<uint32_t>(value)); };
        SetTextBoxQuad(pixels(clip.x), pixels(clip.y), pixels(clip.x + clip.width), pixels(clip.y + clip.height),
                       OUTLINE_COLOUR, OUTLINE_COLOUR);
        DrawTextBoxQuad();
        I3DGetDriver()->SetState(DRIVER_STATE_9, state);
    }
}

bool C_TextBox::ClipViewport(S_IGraphViewport *viewport) const
{
    const float screenWidth = static_cast<float>(m_screenWidth);
    const float screenHeight = static_cast<float>(m_screenHeight);
    // Each edge kept on the screen.
    float left = m_left, top = m_top + CLIP_TOP_MARGIN, right = m_right, bottom = m_bottom;
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
    const float height = top - bottom;
    viewport->x = static_cast<int>(left);
    viewport->y = static_cast<int>(bottom);
    viewport->height = static_cast<int>(height + 1.0f);
    viewport->width = static_cast<int>(width + 1.0f);
    viewport->maxZ = 1.0f;
    viewport->minZ = 0.0f;
    return viewport->height > 0 && viewport->width > 0;
}

uint32_t C_TextBox::WordColour(int line) const
{
    if (m_colour != 0)
        return m_colour;
    if (m_dimmed)
        return (m_style->color & 0xFFFFFF) | DIMMED_ALPHA;
    if (line == m_hover)
        return HOVER_COLOUR;
    return m_style->color;
}

bool C_TextBox::DrawString(const C_str &text, uint32_t colour)
{
    if (g_gameProfile.UsesUnicodeFont())
        return DrawUtf8(text, colour);
    return DrawBytes(text, colour);
}

bool C_TextBox::DrawBytes(const C_str &text, uint32_t colour)
{
    const int size = FontSize();
    const int length = static_cast<int>(text.Size());
    if (size == 0)
        return false;
    if (!m_batching)
        g_numTextVertices = 0;
    for (int i = 0; i < length; ++i) {
        const int ch = m_style->flags30 & STYLE_PASSWORD ? '*' : static_cast<uint8_t>(text.c_str()[i]);
        DrawGlyph(ch, *m_font->GetGlyph(ch), size, colour);
        if (!m_batching && g_numTextVertices == MAX_TEXT_VERTICES)
            FlushVertices();
    }
    if (!m_batching && g_numTextVertices != 0)
        FlushVertices();
    return true;
}

bool C_TextBox::DrawUtf8(const C_str &text, uint32_t colour)
{
    const int size = FontSize();
    const int length = static_cast<int>(text.Size());
    if (size == 0)
        return false;
    if (!m_batching)
        g_numTextVertices = 0;
    for (int i = 0; i < length;) {
        uint32_t code;
        int bytes = DecodeUtf8(&code, text.c_str() + i, static_cast<int>(text.Size()) - i);
        if (bytes < 1) {
            // (Not UTF-8: the byte, signed.)
            code = static_cast<uint32_t>(static_cast<int>(static_cast<signed char>(text.c_str()[i])));
            bytes = 1;
        }
        if (m_style->flags30 & STYLE_PASSWORD)
            code = '*';
        DrawGlyph(static_cast<int>(code), *m_font->GetGlyph(static_cast<int>(code)), size, colour);
        if (!m_batching && g_numTextVertices == MAX_TEXT_VERTICES)
            FlushVertices();
        i += bytes;
    }
    if (!m_batching && g_numTextVertices != 0)
        FlushVertices();
    return true;
}

void C_TextBox::DrawGlyph(int ch, const S_Glyph &glyph, int size, uint32_t colour)
{
    if (glyph.aspect == 0.0f) {
        m_penX = static_cast<int16_t>(m_penX + static_cast<int>(static_cast<float>(size * BLANK_WIDTH) + 0.5));
        return;
    }
    const int width = static_cast<int>(static_cast<float>(size) * glyph.aspect + 0.5f);
    if (m_batching) {
        m_font->AddQuad(ch, colour, width, size, m_penX, m_penY, m_depth);
    } else {
        // Two triangles: base left, top left, top right; base right, base left, top right.
        const float left = static_cast<float>(m_penX), right = static_cast<float>(m_penX + width);
        const float base = static_cast<float>(m_penY), top = static_cast<float>(m_penY - size);
        AddVertex(left, base, glyph.u0, glyph.v1, colour);
        AddVertex(left, top, glyph.u0, glyph.v0, colour);
        AddVertex(right, top, glyph.u1, glyph.v0, colour);
        AddVertex(right, base, glyph.u1, glyph.v1, colour);
        AddVertex(left, base, glyph.u0, glyph.v1, colour);
        AddVertex(right, top, glyph.u1, glyph.v0, colour);
    }
    m_penX = static_cast<int16_t>(m_penX + width);
}

void C_TextBox::DrawCaret(uint32_t colour)
{
    const int size = FontSize();
    const int16_t base = m_penY;
    m_penY = static_cast<int16_t>(
        static_cast<int>(static_cast<float>(m_penY) - static_cast<float>(size) * CARET_DROP));
    const S_Glyph &glyph = *m_font->GetGlyph('_');
    const bool quads = glyph.aspect != 0.0f && !m_batching;
    if (quads)
        g_numTextVertices = 0;
    DrawGlyph('_', glyph, size, colour);
    if (quads)
        FlushVertices();
    m_penY = base;
}

void C_TextBox::AddVertex(float x, float y, float u, float v, uint32_t colour)
{
    S_FontVertex &vertex = g_textVertices[g_numTextVertices++];
    vertex.x = x;
    vertex.y = y;
    vertex.z = m_depth;
    vertex.rhw = 1.0f / m_depth;
    vertex.color = colour;
    vertex.u = u;
    vertex.v = v;
}

void C_TextBox::FlushVertices()
{
    m_font->Select();
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    GetIGraph()->DrawPrimitives(PRIMITIVE_TRIANGLES, g_numTextVertices / 3, g_textVertices, 1);
#endif
    g_numTextVertices = 0;
}

void C_TextBox::StepUnknown76(int16_t step, int16_t *value)
{
    *value = static_cast<int16_t>(*value + step);
    const int16_t stepped = *value;
    int limit = 0;
    if (!m_lines.empty()) {
        limit = static_cast<int>(m_top - static_cast<float>(m_lines.back()->y));
        if (limit > 0) {
            *value = 0;
            return;
        }
    }
    if (stepped > 0)
        *value = 0;
    if (*value < limit)
        *value = static_cast<int16_t>(limit - 2);
}
