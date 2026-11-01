#include "game/video_text_box.h"

#include "ls3d/igraph.h"

// --- the video state's text box: the scrolled lines and the fades (part 9b-iv) ---------------------

namespace {

// Lines drawn down to this far under the screen.
constexpr int BELOW_SCREEN = 100;
// The fades' height: 100 of the screen's width in 1024ths.
constexpr float WIDTH_UNIT = 1.0f / 1024.0f;
constexpr float FADE_HEIGHT = 100.0f;
constexpr uint32_t BLACK = 0xFF000000;
constexpr uint32_t CLEAR = 0x00000000;

} // namespace

void C_VideoTextBox::Render()
{
    if (!m_placed)
        return;
    m_batching = m_fonts->IsBatching();
    if (m_dirty) {
        Layout();
        m_firstLine = 0;
    }
    for (unsigned index = m_firstLine; index < m_lines.size(); ++index) {
        const S_TextLine *line = m_lines[index];
        m_penY = static_cast<int16_t>(line->y - m_scroll);
        if (m_penY < 0) {
            m_firstLine = index + 1;
            continue;
        }
        if (m_penY > m_screenHeight + BELOW_SCREEN)
            break;
        // A word without its style ends its line.
        for (const S_TextWord *word : line->words) {
            if (!SelectStyle(word->style))
                break;
            const uint32_t colour = WordColour(static_cast<int>(index));
            m_penX = word->x;
            DrawString(word->text, colour);
        }
    }

    const int fade = static_cast<int>(static_cast<float>(m_screenWidth) * WIDTH_UNIT * FADE_HEIGHT);
    const float width = static_cast<float>(m_screenWidth), height = static_cast<float>(m_screenHeight);
    GetIGraph()->SetTexture(nullptr, 0);
    SetTextBoxQuad(0.0f, 0.0f, width, static_cast<float>(fade), BLACK, CLEAR);
    DrawTextBoxQuad();
    SetTextBoxQuad(0.0f, static_cast<float>(m_screenHeight - fade), width, height, CLEAR, BLACK);
    DrawTextBoxQuad();
}

bool C_VideoTextBox::ScrollTo(int pixels)
{
    m_scroll = pixels;
    return true;
}

bool C_VideoTextBox::Finished() const
{
    return m_firstLine >= m_lines.size();
}
