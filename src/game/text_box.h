// The game's text library (the menus' code, 0x0063de40..0x0064d060): fonts (game/font.h, part 9a),
// text boxes on interface visuals (game/text_box.cpp, part 9b) and text styles (game/text_style.h, part 9a-iv). Declared here for the HUD,
// which uses it for its texts and the message log's glyphs.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/font.h"
#include "game/text_style.h"
#include "ls3d/math.h"

class I3D_frame;
class I3D_scene;


// A run of a text box: its text and its style (CreateTextStyle's index).
struct S_TextRun {                                      // 5 bytes in the original's vector (packed)
    C_str text;
    uint8_t style = 0;
};

// The laid-out text (the layout's, part 9b-ii): a line's words and its measures in the box's
// pixels (y up), and the pieces the layout keeps.
struct S_TextWord {
    C_str text;                                         // +0x00
    // (Its other fields: part 9b-ii.)
};
struct S_TextLine {                                     // 0x20 bytes
    S_TextLine() = default;
    ~S_TextLine();                                      // 0x00644f70 (its words freed, its measures -1)
    S_TextLine(const S_TextLine &) = delete;
    S_TextLine &operator=(const S_TextLine &) = delete;

    std::vector<S_TextWord *> words;                    // +0x00
    int16_t y = -1;                                     // +0x14 its base
    int16_t above = -1;                                 // +0x16 its height above y
    int16_t below = -1;                                 // +0x18 below y
    int16_t gap = -1;                                   // +0x1A below that (to the next line)
    int16_t unknown1C = -1;                             // +0x1C
};
struct S_TextPiece {
    C_str text;                                         // +0x00
    // (Its other fields: part 9b-ii.)
};

// Rich text in the rectangle of an interface visual: runs of text, each with a style (vtable
// 0x00810458). Laid out into lines when something changed (the next query or Render), scrolled by
// lines' pixels; one run can be edited (a blinking caret) and one line hovered.
class C_TextBox {                                       // 0x9C bytes
public:
    C_TextBox();                                        // 0x00496cc0
    virtual ~C_TextBox();                               // vtable +0x0C 0x0048ecf0 (0x0048ecd0)
    C_TextBox(const C_TextBox &) = delete;
    C_TextBox &operator=(const C_TextBox &) = delete;

    // The scroll's range from the last line (the scroll kept in it).
    virtual void UpdateScrollRange();                   // vtable +0x00 0x006441a0
    virtual void Render();                              // vtable +0x04 0x00648200 (part 9b-iii)
    // False past the range.
    virtual bool ScrollTo(int line);                    // vtable +0x08 0x00644250

    bool AddText(const C_str &text, uint8_t style);     // 0x00644830
    bool AddTextId(int textId, uint8_t style);          // 0x00644770
    // Run `index` replaced (out of range: appended).
    bool SetText(const C_str &text, uint8_t style, int index);   // 0x006446e0
    bool SetTextId(int textId, int style, int index);   // 0x00644520 (style 0x28: the text formatted with the version)
    // A run before run `index` (out of range: appended).
    bool InsertText(const C_str &text, uint8_t style, int index);   // 0x00644b20
    bool InsertTextId(int textId, uint8_t style, int index);        // 0x00644a50
    // False out of range.
    bool RemoveRun(int index);                          // 0x00644bb0
    void Clear();                                       // 0x00644c80
    C_str GetRunText(int index) const;                  // 0x00643dd0 (out of range: none)
    int NumRuns() const { return static_cast<int>(m_runs.size()); }

    // The edited run: `index` begins its edit (false when one is being edited); out of range: the
    // edit ends, the selection `index`.
    bool EditRun(int index);                            // 0x00643c20
    // The caret's blink (half a second on of each second) while a run is edited.
    void TickCaret(int time);                           // 0x00644280
    // The selected run (out of range: the nearest; none: -1).
    int16_t GetSelection();                             // 0x00643fb0
    // The run `index` selected and its line (the same index) scrolled to; -1 while editing or out of
    // range.
    int16_t SelectRun(int index);                       // 0x00643f50
    // The selected run's text (none: empty) and its replacement.
    C_str GetSelectedText() const;                      // 0x006443a0
    bool SetSelectedText(const C_str &text);            // 0x00644320
    // The hovered line (false while editing).
    bool SetHover(int line);                            // 0x00644090
    // The line at the box's pixel `y` (x not used) hovered (false: none, or editing).
    bool HoverAt(int x, int y);                         // 0x006440e0
    bool ClearHover();                                  // 0x00644120
    // The hovered line selected and scrolled to (its index; editing: -1).
    int16_t SelectHovered();                            // 0x00643f10

    // The line scrolled into the box (false when it is higher than the box or in it already).
    bool ScrollToLine(int line);                        // 0x00643e40
    int GetScroll() const { return m_scroll; }          // 0x00644270
    int16_t GetScrollRange();                           // 0x00644230 (after the layout)
    // A line's centre in the interface's coordinates (x of the box's centre; the line outside the
    // box: 0, 0).
    S_vector4 GetLineCentre(int line);                  // 0x00643ca0

    // Takes the visual's screen rectangle (the interface scene's coordinates).
    void AttachTo(I3D_scene *scene, I3D_frame *visual); // 0x00644da0
    // A rectangle in pixels: x0, y0, x1, y1, width, height and the depth (0..1 of the camera's range).
    void SetRect(const float *rect);                    // 0x00644ea0
    // The lines and the pieces freed.
    void FreeLayout();                                  // 0x00644ec0
    // The runs laid out into lines (part 9b-ii).
    void Layout();                                      // 0x00645590
    // The margin left and right in 1024ths of the screen's width (AttachTo; 10 after construction,
    // the HUD's panels 0).
    void SetUnknown90(uint8_t value) { m_margin = value; }
    // (Byte +0x71, 0 after construction; the inventory sets it on a list's caption while the list
    // has no container. Not identified.)
    void SetUnknown71(bool value) { m_unknown71 = value; }

private:
    bool AddRun(S_TextRun run);                         // 0x006448b0
    bool SetRun(S_TextRun run, int index);              // 0x00644410
    bool InsertRun(S_TextRun run, int index);           // 0x00644920
    // The line at the scroll's `y` (none: -1).
    int16_t LineAt(int x, int y) const;                 // 0x00644140
    // A visual's depth between the scene camera's near and far planes (0 .. 1; outside: -1; no
    // scene: 0).
    static float VisualDepth(I3D_scene *scene, I3D_frame *visual);   // 0x00644cf0

    int16_t m_screenWidth = 0;                          // +0x04
    int16_t m_screenHeight = 0;                         // +0x06
    // The rectangle (pixels; y up: top above bottom).
    float m_left = 0.0f;                                // +0x08
    float m_top = 0.0f;                                 // +0x0C
    float m_right = 0.0f;                               // +0x10
    float m_bottom = 0.0f;                              // +0x14
    float m_width = 0.0f;                               // +0x18
    float m_height = 0.0f;                              // +0x1C
    float m_depth = 0.0f;                               // +0x20
    C_FontManager *m_fonts = nullptr;                   // +0x28
    int m_unknown2C = 0;                                // +0x2C
    std::vector<S_TextRun> m_runs;                      // +0x30
    std::vector<S_TextLine *> m_lines;                  // +0x44
    std::vector<S_TextPiece *> m_pieces;                // +0x58
    bool m_placed = false;                              // +0x70 (AttachTo / SetRect)
    bool m_unknown71 = false;                           // +0x71
    bool m_dirty = false;                               // +0x72 laid out again before use
    bool m_scrolls = false;                             // +0x73 (a range once)
    bool m_unknown74 = false;                           // +0x74
    int16_t m_unknown76 = 0;                            // +0x76
    int m_scroll = 0;                                   // +0x78 pixels
    int m_scrollRange = 0;                              // +0x7C
    bool m_caretOn = false;                             // +0x80
    bool m_editing = false;                             // +0x81
    int m_caretTime = 0;                                // +0x84 ms
    int m_selection = -1;                               // +0x88 a run
    int m_hover = -1;                                   // +0x8C a line
    uint8_t m_margin = 10;                              // +0x90
    int m_unknown94 = 0;                                // +0x94
    int m_unknown98 = 0;                                // +0x98
};

// UTF-8 of the menus' text input (the unicode font option).
bool ReadUnicodeInput(wchar_t *buffer, int maxLength);  // 0x0066b680 (__fastcall)
// One code point to UTF-8 (at most `maxBytes`; the bytes written, 0 or less: none).
int EncodeUtf8(char *out, const uint32_t *codePoint, int maxBytes);   // 0x0064d910 (__fastcall)
// The code point at `text` (`length` bytes left; the bytes it takes, 0 or less: invalid).
int DecodeUtf8(uint32_t *codePoint, const char *text, int length);    // 0x0064d830 (__fastcall)
