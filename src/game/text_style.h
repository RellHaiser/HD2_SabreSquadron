// The text styles (game/text_style.cpp, original source 0x00641fe0..0x00643c20, part 9a-iv): the
// list the text boxes' runs name by index (its object 0x008ae3f8). The first use adds the game's
// 90 own styles (0 .. 89, the menus' and the HUD's); CreateTextStyle adds one in the first free
// place, ReleaseTextStyle frees one (the free places at the end given back).
#pragma once

#include <cstdint>

// A text style: font, colour, size and placement in the text box (fractions of the box; -1:
// automatic) and flags. (The fields' meanings are inferred from the HUD's tables: the columns of a
// score table share +0x10 down the rows.)
struct S_TextStyleDesc {                                // 0x34 bytes
    int font;
    uint32_t color;                                     // ARGB
    float size;                                         // of the screen height
    float width;                                        // +0x0C the column's width
    // A piece's place in the box (both >= 0; else the run is flowed into the lines).
    float x;                                            // +0x10 the column's left
    float y;                                            // +0x14 (-1 or 0.95 .. 0.98)
    float below;                                        // +0x18 a line's space below its base
    float above;                                        // +0x1C above its base (of the screen height)
    int unknown20;
    int align;                                          // +0x24 1 left, 2 centred, 3 right, 4 justified
    int valign;                                         // +0x28 the lines in the box: 1 top, 2 middle, 3 bottom
    uint8_t joins;                                      // +0x2C continues the line (0: a line of its own)
    uint8_t wraps;                                      // +0x2D wrapped into lines (0: one word)
    uint32_t flags30;                                   // STYLE_* below
};

// S_TextStyleDesc::flags30.
constexpr uint32_t STYLE_PASSWORD = 0x04;               // drawn (and measured) as '*'
constexpr uint32_t STYLE_NEW_LINE = 0x08;               // a joined run starts a new line
constexpr uint32_t STYLE_AT_X = 0x10;                   // a word placed at the style's x (its line not aligned)

// A new style (a copy of `desc`): its index.
int CreateTextStyle(const S_TextStyleDesc &desc);       // 0x00643c00 (__fastcall; 0x006420b0)
// The style freed (an index not in the list: nothing).
void ReleaseTextStyle(int style);                       // 0x00643bf0 (__fastcall; 0x00641fe0)
// The style at `style` (none, or freed: null). (The text boxes read the list where it is.)
const S_TextStyleDesc *GetTextStyle(int style);
// The list's size (the freed places at its end not in it).
int NumTextStyles();
// The game's styles added to the list unless they are already (the text boxes' layout does it
// before it reads the list).
void LoadTextStyles();
