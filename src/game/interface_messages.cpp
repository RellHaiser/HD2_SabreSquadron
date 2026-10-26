// The message log and the chat line (see interface.h; the original's 0x00494950..0x00496360).
#include "game/interface.h"

#include <cstdio>
#include <cstring>

#include "game/game.h"
#include "game/game_internal.h"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The font of the log; the chat line's colour (yellow) and the lines' alpha.
constexpr int LOG_FONT = 3;
constexpr uint32_t INPUT_COLOR = 0xFFFFFF00;

// The lines age every 2 s; the caret shows from 150 to 300 ms of its cycle; a held key repeats
// after 150 ms, then every 50.
constexpr int AGE_TIME = 2000;
constexpr int CARET_ON = 150;
constexpr int CARET_CYCLE = 300;
constexpr int REPEAT_FIRST = 150;
constexpr int REPEAT_NEXT = 50;

// Keys of the history's scrolling (DirectInput codes) and the typed characters.
constexpr int KEY_UP = 200;
constexpr int KEY_DOWN = 0xD0;
constexpr uint32_t CHAR_BACKSPACE = 8;
constexpr uint32_t CHAR_TAB = 9;
constexpr uint32_t CHAR_ENTER = 13;

// The log area's rectangles: its frame is moved to these heights to measure them.
constexpr float AREA_Y_LOG = 0.18f;
constexpr float AREA_Y_HISTORY = -0.3f;
// The interface's screen space: x 0..1, y 0..-0.75 (4:3).
constexpr float SCREEN_Y_SCALE = -1.33333337f;
constexpr float AREA_MARGIN = 0.005f;
// A blank glyph is 0.4 of the height wide.
constexpr float BLANK_WIDTH = 0.4f;
constexpr float VERTEX_Z = 0.02f;

const S_vector INPUT_ECHO_COLOR(255.0f, 255.0f, 0.0f);

int StrLen(const char *text)
{
    return static_cast<int>(std::strlen(text));
}

} // namespace

C_MessageLog::C_MessageLog() = default;

C_MessageLog::~C_MessageLog()
{
    Clear();
    m_fonts = nullptr;
}

void C_MessageLog::Init(I3D_scene *scene, int maxLines)
{
    m_fonts = AcquireFontManager();
    m_area = scene->FindFrame("Text", FRAME_MASK_VISUAL);
    m_maxLines = maxLines;
    m_ageTime = 0;
    m_unknownD384 = true;
    m_vertexCount = 0;
    m_input[0] = '\0';
    m_inputScroll = 0;
    m_scroll = 0;
    m_repeatKey = 0;
    m_repeatTime = 0;
    m_repeatDelay = REPEAT_FIRST;
    m_screenHeight = static_cast<float>(GetIGraph()->ScrnY());
    m_screenWidth = static_cast<float>(GetIGraph()->ScrnX());
    // The two rectangles: the area at the log's height and at the history's.
    const S_vector pos = m_area->GetWorldPos();
    m_area->SetPosInline(S_vector(pos.x, AREA_Y_LOG, pos.z));
    m_area->Update();
    GetAreaRect(&m_rect[0], &m_rect[1], &m_rect[2], &m_rect[3]);
    m_area->SetPosInline(S_vector(pos.x, AREA_Y_HISTORY, pos.z));
    m_area->Update();
    GetAreaRect(&m_historyRect[0], &m_historyRect[1], &m_historyRect[2], &m_historyRect[3]);
    m_area->SetOn(false);
    m_area->Update();
    m_mode = 0;
    m_inputOpen = false;
    m_sayTeam = false;
    m_sayAll = false;
}

void C_MessageLog::Done()
{
    if (m_fonts)
        ReleaseFontManager();
    m_fonts = nullptr;
    Clear();
    m_area = nullptr;
}

void C_MessageLog::Clear()
{
    for (std::size_t i = m_lines.size(); i-- > 0;)
        delete[] m_lines[i].text;
    m_lines.clear();
    m_ageTime = 0;
    if (m_fonts) {
        m_fonts->Release();
        m_fonts = nullptr;
    }
}

void C_MessageLog::AddMessage(const S_vector &color, const char *text)
{
    AddLine(text, color);
}

void C_MessageLog::AddLine(const char *text, const S_vector &color)
{
    const int length = StrLen(text);
    if (length == 0)
        return;
    AgeLines();
    S_LogLine line;
    line.age = 0;
    line.r = static_cast<int>(color.x);
    line.g = static_cast<int>(color.y);
    line.b = static_cast<int>(color.z);
    line.text = new char[length + 1];
    line.font = LOG_FONT;
    std::memcpy(line.text, text, length + 1);
    // The oldest goes when the log is full.
    if (m_lines.size() >= static_cast<std::size_t>(m_maxLines)) {
        delete[] m_lines.front().text;
        m_lines.erase(m_lines.begin());
    }
    m_lines.push_back(line);
}

void C_MessageLog::AgeLines()
{
    for (S_LogLine &line : m_lines)
        ++line.age;
}

void C_MessageLog::Update(int timeDelta)
{
    if (m_inputOpen) {
        m_caretTime += timeDelta;
        HandleKeys();
        if (m_repeatKey != 0)
            m_repeatTime += timeDelta;
    }
    m_ageTime += timeDelta;
    if (m_ageTime > AGE_TIME) {
        m_ageTime = 0;
        AgeLines();
    }
}

void C_MessageLog::SetMode(int mode)
{
    m_mode = mode;
    m_scroll = 0;
    if (mode == 0) {
        if (IsLocalGame())
            GetGame()->SetPaused(false);
        m_area->SetOn(false);
        OpenInput(false);
    } else if (mode == 1) {
        // The history (single player pauses meanwhile).
        if (IsLocalGame())
            GetGame()->SetPaused(true);
        m_area->SetOn(true);
        OpenInput(true);
    }
}

void C_MessageLog::StartSayTeam()
{
    if (m_mode == 1)
        return;
    m_sayTeam = true;
    OpenInput(true);
}

void C_MessageLog::StartSay()
{
    if (m_mode == 1)
        return;
    m_sayAll = true;
    OpenInput(true);
}

void C_MessageLog::OpenInput(bool open)
{
    if (open) {
        m_caretTime = 0;
        GetIGraph()->FlushChars();
        m_inputOpen = open;
        return;
    }
    m_input[0] = '\0';
    m_sayTeam = false;
    m_sayAll = false;
    m_inputOpen = false;
}

// The chat line's keys: the history's scrolling (held keys repeat), typing, backspace, enter.
void C_MessageLog::HandleKeys()
{
    int key = GetIGraph()->GetAsciiKey();
    if (key == 0) {
        if (m_repeatKey != 0) {
            if (m_repeatTime > m_repeatDelay) {
                if (GetIGraph()->IsKeyDown(static_cast<uint8_t>(m_repeatKey))) {
                    key = m_repeatKey;
                    m_repeatDelay = REPEAT_NEXT;
                    m_repeatTime = 0;
                } else {
                    m_repeatKey = 0;
                }
            }
        }
    } else {
        m_repeatKey = key;
        m_repeatDelay = REPEAT_FIRST;
        m_repeatTime = 0;
    }

    if (key == KEY_UP) {
        // Quirk kept: unsigned, so an empty history scrolls up anyway (nothing is drawn).
        if (m_mode == 1 && static_cast<uint32_t>(m_scroll) < static_cast<uint32_t>(m_lines.size()) - 1u)
            ++m_scroll;
        return;
    }
    if (key == KEY_DOWN) {
        if (m_mode == 1 && m_scroll != 0)
            --m_scroll;
        return;
    }

    auto enter = [this]() {
        AddMessage(INPUT_ECHO_COLOR, m_input[0] ? m_input : " ");
        if (m_sayAll)
            ExecuteConsoleCommand(CONSOLE_SAY, m_input);
        else if (m_sayTeam)
            ExecuteConsoleCommand(CONSOLE_SAY_TEAM, m_input);
        else
            ExecuteConsoleLine(m_input);
        m_input[0] = '\0';
        m_inputScroll = 0;
        if (m_mode != 1)
            OpenInput(false);
    };

    if (!g_gameProfile.UsesUnicodeFont()) {
        const uint32_t ch = GetIGraph()->ReadChar();
        if (ch == 0)
            return;
        if (ch == CHAR_BACKSPACE) {
            const int length = StrLen(m_input);
            if (length != 0)
                m_input[length - 1] = '\0';
            m_inputScroll = GetInputScroll();
            return;
        }
        if (ch == CHAR_TAB)
            return;
        if (ch == CHAR_ENTER) {
            enter();
            return;
        }
        if (StrLen(m_input) < INPUT_SIZE - 1) {
            const char text = static_cast<char>(ch);
            std::strncat(m_input, &text, 1);
            m_inputScroll = GetInputScroll();
        }
        return;
    }

    // The unicode font: the typed characters in UTF-8.
    wchar_t typed[0x400];
    if (!ReadUnicodeInput(typed, 0x3FF) || typed[0] == 0)
        return;
    for (const wchar_t *it = typed; *it != 0; ++it) {
        const uint32_t ch = static_cast<uint32_t>(static_cast<uint16_t>(*it));
        if (ch == CHAR_BACKSPACE) {
            // The last whole character goes (an incomplete sequence stays).
            const int length = StrLen(m_input);
            if (length == 0) {
                m_inputScroll = GetInputScroll();
                return;
            }
            int start = length - 1;
            while (start >= 0 && static_cast<uint8_t>(m_input[start]) >= 0x80 &&
                   static_cast<uint8_t>(m_input[start]) < 0xC0)
                --start;
            // Fix: a line of only continuation bytes made the original test (and maybe clear) the
            // byte before the buffer; it stays.
            if (start < 0)
                return;
            const int bytes = length - start;
            const uint8_t lead = static_cast<uint8_t>(m_input[start]);
            bool whole;
            switch (bytes) {
            case 1: whole = lead < 0x80; break;
            case 2: whole = lead >= 0xC0 && lead < 0xE0; break;
            case 3: whole = lead >= 0xE0 && lead < 0xF0; break;
            case 4: whole = lead >= 0xF0 && lead < 0xF8; break;
            default: whole = false; break;
            }
            if (!whole)
                return;
            m_input[start] = '\0';
            m_inputScroll = GetInputScroll();
            return;
        }
        if (ch == CHAR_TAB)
            return;
        if (ch == CHAR_ENTER) {
            enter();
            return;
        }
        char bytes[8];
        const int count = EncodeUtf8(bytes, &ch, 6);
        if (count > 0 && StrLen(m_input) + count < INPUT_SIZE - 1) {
            bytes[count] = '\0';
            std::strncat(m_input, bytes, count);
            m_inputScroll = GetInputScroll();
        }
    }
}

int C_MessageLog::GetInputScroll()
{
    const float *rect = m_mode == 0 ? m_rect : m_historyRect;
    const float left = rect[0];
    const int height = static_cast<int>(m_lineHeight * m_screenHeight);
    C_Font *font = m_fonts->FindFont(LOG_FONT);
    if (!font)
        return 0;
    // The room left of the end of the area, less the caret's ('_').
    const float heightF = static_cast<float>(height);
    int pos = static_cast<int>(rect[2]) - static_cast<int>(heightF * font->GetGlyph('_')->aspect);
    for (int i = StrLen(m_input) - 1; i >= 0; --i) {
        const float width = heightF * font->GetGlyph(static_cast<uint8_t>(m_input[i]))->aspect;
        if (width == 0.0f)
            pos += static_cast<int>(static_cast<double>(height) * -0.4);
        else
            pos -= static_cast<int>(width);
        if (static_cast<float>(pos) <= left)
            return i;
    }
    return 0;
}

void C_MessageLog::AddVertex(float x, float y, float u, float v, uint32_t color)
{
    S_TLVertex &vertex = m_vertices[m_vertexCount];
    vertex.x = x;
    vertex.y = y;
    vertex.z = VERTEX_Z;
    vertex.rhw = 1.0f / vertex.z;
    vertex.color = color;
    vertex.u = u;
    vertex.v = v;
    ++m_vertexCount;
}

// Two triangles of a glyph from `x` (a blank only advances); the advance.
int C_MessageLog::AddGlyph(const S_Glyph &glyph, int x, int y, int height, uint32_t color)
{
    const float width = static_cast<float>(height) * glyph.aspect;
    if (width == 0.0f)
        return static_cast<int>(static_cast<double>(height) * BLANK_WIDTH);
    const int advance = static_cast<int>(width);
    const float x0 = static_cast<float>(x);
    const float y0 = static_cast<float>(y);
    const float y1 = static_cast<float>(y + height);
    const float x1 = static_cast<float>(advance + x);
    AddVertex(x0, y0, glyph.u0, glyph.v0, color);
    AddVertex(x1, y1, glyph.u1, glyph.v1, color);
    AddVertex(x0, y1, glyph.u0, glyph.v1, color);
    AddVertex(x0, y0, glyph.u0, glyph.v0, color);
    AddVertex(x1, y0, glyph.u1, glyph.v0, color);
    AddVertex(x1, y1, glyph.u1, glyph.v1, color);
    return advance;
}

void C_MessageLog::Flush(C_Font *font)
{
    if (m_vertexCount == 0)
        return;
    font->Select();
    GetIGraph()->DrawPrimitives(3, m_vertexCount / 3, m_vertices, 1);
    m_vertexCount = 0;
}

// The chat line ("Say: ", "SayTeam: " or nothing before it) from its scroll on, and the caret.
void C_MessageLog::DrawInput(int x, int y, int height, int right)
{
    m_vertexCount = 0;
    C_Font *font = m_fonts->FindFont(LOG_FONT);
    if (!font)
        return;
    char text[0x200];
    const char *format = m_sayAll ? "Say: %s" : m_sayTeam ? "SayTeam: %s" : "%s";
    std::sprintf(text, format, m_input);
    // Quirk kept: the scroll counts the characters of the input, the drawing those of the line
    // with its prefix.
    const int length = StrLen(text);
    if (!g_gameProfile.UsesUnicodeFont()) {
        for (int i = 0; i < length; ++i) {
            if (i < m_inputScroll)
                continue;
            x += AddGlyph(*font->GetGlyph(static_cast<uint8_t>(text[i])), x, y, height, INPUT_COLOR);
            if (x > right)
                break;
        }
    } else {
        for (int pos = 0; pos < length;) {
            uint32_t ch;
            int bytes = DecodeUtf8(&ch, text + pos, length - pos);
            if (bytes < 1) {
                bytes = 1;
                ch = ' ';
            }
            if (pos >= m_inputScroll) {
                x += AddGlyph(*font->GetGlyph(static_cast<int>(ch)), x, y, height, INPUT_COLOR);
                if (x > right)
                    break;
            }
            pos += bytes;
        }
    }
    // The caret ('_') shows in the second half of its cycle.
    if (m_caretTime >= CARET_ON) {
        if (m_caretTime <= CARET_CYCLE)
            AddGlyph(*font->GetGlyph('_'), x, y, height, INPUT_COLOR);
        else
            m_caretTime = 0;
    }
    Flush(font);
}

// The lines from the newest up: in the log a line's age leaves that many lines' gap below it;
// the history scrolls by m_scroll lines.
void C_MessageLog::DrawLines()
{
    const float *rect = m_mode == 1 ? m_historyRect : m_rect;
    const float left = rect[0];
    const float right = rect[2];
    const float limit = rect[3];
    const int lineHeight = static_cast<int>(m_lineHeight * m_screenHeight);
    const float lineHeightF = static_cast<float>(lineHeight);
    int y = static_cast<int>(rect[1] - lineHeightF);
    if (m_inputOpen)
        DrawInput(static_cast<int>(left), y, lineHeight, static_cast<int>(right));
    y -= lineHeight;
    m_vertexCount = 0;
    int line = 0;
    for (int i = static_cast<int>(m_lines.size()) - 1; i >= 0; --i, ++line) {
        if (line < m_scroll)
            continue;
        const S_LogLine &entry = m_lines[i];
        while (line < entry.age && m_mode == 0) {
            y -= lineHeight;
            ++line;
        }
        const float lineY = static_cast<float>(y);
        if (lineY < limit)
            return;
        C_Font *font = m_fonts->FindFont(entry.font);
        if (!font)
            continue;
        int x = static_cast<int>(left);
        const uint32_t color = static_cast<uint32_t>(((entry.r - 0x100) * 0x100 + entry.g) * 0x100 + entry.b);
        const int length = StrLen(entry.text);
        if (!g_gameProfile.UsesUnicodeFont()) {
            for (int c = 0; c < length; ++c) {
                x += AddGlyph(*font->GetGlyph(static_cast<uint8_t>(entry.text[c])), x, y, lineHeight, color);
                if (right < static_cast<float>(x))
                    break;
            }
        } else {
            for (int pos = 0; pos < length;) {
                uint32_t ch;
                int bytes = DecodeUtf8(&ch, entry.text + pos, length - pos);
                if (bytes < 1) {
                    ch = ' ';
                    bytes = 1;
                }
                x += AddGlyph(*font->GetGlyph(static_cast<int>(ch)), x, y, lineHeight, color);
                if (static_cast<float>(x) > right)
                    break;
                pos += bytes;
            }
        }
        Flush(font);
        y -= lineHeight;
    }
}

void C_MessageLog::Render()
{
    I3D_driver *driver = I3DGetDriver();
    const int old = driver->GetState(3);
    driver->SetState(3, 1);
    DrawLines();
    I3DGetDriver()->SetState(3, old);
}

void C_MessageLog::GetAreaRect(float *left, float *top, float *right, float *bottom)
{
    if (!m_area)
        return;
    const I3D_bound_volume bound = m_area->GetBound();
    *right = bound.bbox.max.x - AREA_MARGIN;
    *bottom = bound.bbox.max.y - AREA_MARGIN;
    *left = bound.bbox.min.x + AREA_MARGIN;
    *top = bound.bbox.min.y + AREA_MARGIN;
    *left = m_screenWidth * *left;
    *top = *top * m_screenHeight * SCREEN_Y_SCALE;
    *right = m_screenWidth * *right;
    *bottom = *bottom * m_screenHeight * SCREEN_Y_SCALE;
    if (*right < 0.0f)
        *right = m_screenWidth;
    if (*bottom > m_screenHeight) {
        *bottom = m_screenHeight;
        return;
    }
    if (*bottom < 0.0f)
        *bottom = 0.0f;
}
