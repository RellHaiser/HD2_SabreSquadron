// The video state's text box (game/video_text_box.cpp, part 9b-iv; C_VideoState's +0x38, made in
// 0x00688ab0 over the screen less 2 % at each edge): the videos' subtitles, or the credits scrolled
// up the screen (its scroll the pixels moved, set each frame) between two fades to black.
#pragma once

#include "game/text_box.h"

class C_VideoTextBox : public C_TextBox {               // 0xA0 bytes, vtable 0x008158f8
public:
    C_VideoTextBox() = default;                         // (inline in 0x00688ab0)
    ~C_VideoTextBox() override = default;               // vtable +0x0C 0x00688cc0 (0x00688ce0)

    // Nothing (no scroll range).
    void UpdateScrollRange() override {}                // vtable +0x00 0x00688cb0
    // The lines from the first not yet scrolled off the top (those above it passed for good) to
    // 100 pixels under the screen, then the top's and the bottom's fades (black to clear, 100 /
    // 1024 of the screen's width high). Laid out first when something changed (from the first line).
    void Render() override;                             // vtable +0x04 0x0064bc70
    // The scroll in pixels (always true).
    bool ScrollTo(int pixels) override;                 // vtable +0x08 0x0064bc60
    // Every line scrolled off the top (or none laid out).
    bool Finished() const;                              // 0x0064bfe0 (__fastcall)

protected:
    unsigned m_firstLine = 0;                           // +0x9C the first not yet off the top
};
