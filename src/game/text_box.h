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
struct S_IGraphViewport;


// A run of a text box: its text and its style (CreateTextStyle's index).
struct S_TextRun {                                      // 5 bytes in the original's vector (packed)
    C_str text;
    uint8_t style = 0;
};

// The laid-out text (Layout's): the lines of the flowed runs, their words (a run's text or a part
// of it) and their measures in the box's pixels (y up), and the pieces (runs placed where their
// styles say). Their places are Position's (part 9b-ii-b).
struct S_TextWord {                                     // 10 bytes
    C_str text;                                         // +0x00
    uint8_t style = 0;                                  // +0x04
    int16_t width = 0;                                  // +0x06 pixels
    int16_t x = -1;                                     // +0x08 in the line
};
struct S_TextLine {                                     // 0x20 bytes
    S_TextLine() = default;
    ~S_TextLine();                                      // 0x00644f70 (its words freed, its measures -1)
    S_TextLine(const S_TextLine &) = delete;
    S_TextLine &operator=(const S_TextLine &) = delete;

    // Its words deleted, its measures -1.
    void Reset();                                       // 0x00645a10

    std::vector<S_TextWord *> words;                    // +0x00
    int16_t y = -1;                                     // +0x14 its base
    int16_t above = -1;                                 // +0x16 its height above y (its styles' largest)
    int16_t below = -1;                                 // +0x18 below y
    int16_t gap = -1;                                   // +0x1A below that (its largest font size)
    int16_t remaining = -1;                             // +0x1C the width left (the box's at first)
};
struct S_TextPiece {                                    // 0x0E bytes
    C_str text;                                         // +0x00
    uint8_t style = 0;                                  // +0x04
    int16_t width = -1;                                 // +0x06 pixels
    int16_t height = -1;                                // +0x08 its font size
    int16_t x = -1;                                     // +0x0A
    int16_t y = -1;                                     // +0x0C
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
    // The lines' words, the caret on the edited run's line and the pieces drawn (a background
    // first, when it has one; overflowing: clipped to the box). Laid out first when something
    // changed; nothing before the box is placed.
    virtual void Render();                              // vtable +0x04 0x00648200
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
    // The runs laid out: the flowed ones into lines, the placed ones into pieces, then positioned
    // (false: a run without its style or font, or no runs; the box stays dirty).
    bool Layout();                                      // 0x00645590
    // The margin left and right in 1024ths of the screen's width (AttachTo; 10 after construction,
    // the HUD's panels 0).
    void SetUnknown90(uint8_t value) { m_margin = value; }
    // Drawn dimmed: the styles' colours at 0x60 of alpha (the inventory's list captions while the
    // list has no container; no effect when the box has its own colour, m_colour).
    void SetDimmed(bool value) { m_dimmed = value; }
    // Nothing (the interface's text control's message 0x200002c and its margin's, with its scene and
    // root frame; the paged box lays itself out again on 0x200002c).
    void Reattach(I3D_scene *, I3D_frame *) {}          // 0x0064b1e0

protected:
    bool AddRun(S_TextRun run);                         // 0x006448b0
    bool SetRun(S_TextRun run, int index);              // 0x00644410
    bool InsertRun(S_TextRun run, int index);           // 0x00644920
    // The line at the scroll's `y` (none: -1).
    int16_t LineAt(int x, int y) const;                 // 0x00644140
    // A visual's depth between the scene camera's near and far planes (0 .. 1; outside: -1; no
    // scene: 0).
    static float VisualDepth(I3D_scene *scene, I3D_frame *visual);   // 0x00644cf0

    // The layout (part 9b-ii).
    // A style and its font selected for the measures (false: no such style or font).
    bool SelectStyle(int style);                        // 0x00645010
    // The style's font size: its share of the screen's height in pixels (rounded).
    int FontSize() const;                               // 0x006451d0
    // A character's advance at `size` pixels (a blank's 0.4 of the size; the password style's: '*').
    int Advance(int ch, int size);                      // 0x00645130
    int TextWidth(const C_str &text, int size);         // 0x00645080
    // The text's first characters that fit in `maxWidth` (none fit, or all: the text).
    C_str Truncate(const C_str &text, float size, float maxWidth);   // 0x00645210
    // A line at the end (the box's width left).
    S_TextLine *AddLine();
    // A run flowed into the lines from the last: by its UTF-8 characters with the unicode font,
    // else by its words (both: 0).
    int FlowRun(const S_TextRun &run);                  // 0x00645ac0
    int FlowUtf8(const S_TextRun &run);                 // 0x00645ae0
    int FlowWords(const S_TextRun &run);                // 0x00646240
    // A word added to the last line: its measures the line's if larger, its width taken from the
    // line's room.
    void AddWord(S_TextLine *line, const C_str &text, uint8_t style, int width, int height, int above, int below);
    // The positioning (part 9b-ii-b).
    // The lines stacked from the bottom (moved up by the first run's vertical alignment), each
    // aligned; the pieces placed; the scroll's range.
    bool Position();                                    // 0x00646b40
    // The space the box has over the lines' height for the vertical alignment (top: 0; none: 0).
    float VAlignOffset(int valign) const;               // 0x00646bf0
    // The lines' height: their measures less the first's space below and the last's above.
    int TextHeight() const;                             // 0x00646c60
    // The lines' bases from `bottom` up.
    void StackLines(int bottom);                        // 0x00646ce0
    void PlaceLines();                                  // 0x00647250
    // A line's words with the STYLE_AT_X style placed at their styles' x.
    void PlaceWordsAtX(S_TextLine *line);               // 0x006470d0
    // A line's words placed by its first word's style's alignment (`last`: its paragraph's last
    // line, not justified; `index`: the edited run's, as a line).
    void AlignLine(S_TextLine *line, bool last, int index);   // 0x00646d30
    // A line's words split at their blanks (for the justification).
    void SplitWords(S_TextLine *line);                  // 0x00647600 (0x00647620 UTF-8, 0x00647b50 bytes)
    // The space between a justified line's words.
    float JustifyGap(const S_TextLine *line) const;     // 0x00647590
    void PlacePieces();                                 // 0x00647360
    // The box overflows: a line without room, a piece outside the box (the flag).
    bool CheckLineOverflow(const S_TextLine *line);     // 0x00646ab0
    bool CheckPieceOverflow(const S_TextPiece *piece);  // 0x00646ad0
    // The render (part 9b-iii).
    // The box's rectangle on the screen as a viewport, extended 5 pixels up (false: nothing of it on
    // the screen).
    bool ClipViewport(S_IGraphViewport *viewport) const;   // 0x00648020
    // A word's colour on `line` with the selected style: the box's colour; dimmed; the hovered
    // line's white; the style's.
    uint32_t WordColour(int line) const;                // 0x006481b0
    // The text drawn at the pen with the selected style (moved past it): quads of its glyphs, in
    // the font's own gathered quads while the fonts batch (false: the font size 0).
    bool DrawString(const C_str &text, uint32_t colour);  // 0x00648b80 (0x006487e0 bytes, 0x00648ba0 UTF-8)
    bool DrawBytes(const C_str &text, uint32_t colour);
    bool DrawUtf8(const C_str &text, uint32_t colour);
    // A glyph's quad at the pen (`ch`'s glyph; a blank: only the pen moved): its two triangles in
    // the vertices, or the font's (batching).
    void DrawGlyph(int ch, const S_Glyph &glyph, int size, uint32_t colour);
    // The caret at the pen, 0.1 of the font size under the base.
    void DrawCaret(uint32_t colour);                    // 0x00648fa0
    // A vertex of the glyphs' triangles at the box's depth.
    void AddVertex(float x, float y, float u, float v, uint32_t colour);   // 0x006442d0
    // The quads gathered drawn with the font's texture.
    void FlushVertices();
    // `value` (Render: +0x76) moved by `step` and kept from 0 to 2 under what the box's top is over
    // the last line (Render's only step 0, never set: +0x76 stays 0; it is not read).
    void StepUnknown76(int16_t step, int16_t *value);   // 0x006491d0

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
    C_Font *m_font = nullptr;                           // +0x24 the selected style's (the layout's)
    C_FontManager *m_fonts = nullptr;                   // +0x28
    const S_TextStyleDesc *m_style = nullptr;           // +0x2C the selected style
    std::vector<S_TextRun> m_runs;                      // +0x30
    std::vector<S_TextLine *> m_lines;                  // +0x44
    std::vector<S_TextPiece *> m_pieces;                // +0x58
    int16_t m_penX = 0;                                 // +0x6C the render's (pixels)
    int16_t m_penY = 0;                                 // +0x6E its base
    bool m_placed = false;                              // +0x70 (AttachTo / SetRect)
    bool m_dimmed = false;                              // +0x71
    bool m_dirty = false;                               // +0x72 laid out again before use
    bool m_overflows = false;                           // +0x73 the text does not fit the box
    bool m_batching = false;                            // +0x74 the font manager's (Render)
    int16_t m_unknown76 = 0;                            // +0x76
    int m_scroll = 0;                                   // +0x78 pixels
    int m_scrollRange = 0;                              // +0x7C
    bool m_caretOn = false;                             // +0x80
    bool m_editing = false;                             // +0x81
    int m_caretTime = 0;                                // +0x84 ms
    int m_selection = -1;                               // +0x88 a run
    int m_hover = -1;                                   // +0x8C a line
    uint8_t m_margin = 10;                              // +0x90
    uint32_t m_colour = 0;                              // +0x94 every word's (0: their styles')
    uint32_t m_background = 0;                          // +0x98 the box filled (0: none)
};

// The render's debugging switches (never set: 0): the overflowing boxes' clips outlined, +0x76's
// step (C_TextBox::StepUnknown76).
extern bool g_textBoxOutline;                           // 0x008ae600
extern int g_textBoxStep76;                             // 0x008ae604
// The quad the boxes fill or fade the screen with (a fan of 4; the render's background and outline, the
// video text box's fades): from (x0, y0) to (x1, y1) at a fixed depth, its texture's corners (0, 0),
// (1, 0), (0, 1), (1, 1); the first two corners `colour0`, the others `colour1`. Drawn with the
// texture the caller set (all of them: none).
extern S_FontVertex g_textBoxQuad[4];                   // 0x008ae410
void SetTextBoxQuad(float x0, float y0, float x1, float y1, uint32_t colour0, uint32_t colour1);
void DrawTextBoxQuad();
// Reads the screen's size and drops it (a check left without its body; the render's and RunGame's).
void ReadScreenSize(int x, int y);                      // 0x00648190 (__stdcall)

// UTF-8 of the menus' text input (the unicode font option).
bool ReadUnicodeInput(wchar_t *buffer, int maxLength);  // 0x0066b680 (__fastcall)
// One code point to UTF-8, not terminated (fewer bytes than `maxBytes`; the bytes written; a
// surrogate: -3; no room: -4; over 0x1FFFFF: 0xFFFD in 2 bytes, the original's quirk).
int EncodeUtf8(char *out, const uint32_t *codePoint, int maxBytes);   // 0x0064d910 (__fastcall)
// The code point at `text` (`length` bytes left; the bytes it takes, 0 or less: invalid).
int DecodeUtf8(uint32_t *codePoint, const char *text, int length);    // 0x0064d830 (__fastcall)
// The bytes from `begin` to `end` are whole, well-formed UTF-8 sequences (empty: false).
bool IsLegalUtf8Range(const uint8_t *begin, const uint8_t *end);      // 0x0064d3d0 (__fastcall)
// A NUL-terminated text of UTF-8 sequences by their lead bytes and continuations (not checked further)
// with at least one of more than a byte: plain ASCII is false, as is an empty text.
bool HasUtf8Sequences(const char *text);                // 0x0064d9e0 (__fastcall)
// A NUL-terminated wide text with a character over U+3000 (the CJK blocks and above).
bool HasCjkCharacters(const uint16_t *text);            // 0x0064da80 (__fastcall)
