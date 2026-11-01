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
    float x;                                            // +0x10 the column's left
    float unknown14;                                    // -1 or 0.95 .. 0.98
    float unknown18;
    float unknown1C;
    int unknown20;
    int align;                                          // +0x24 1 left, 2 right, 3 centred
    int unknown28;
    uint8_t unknown2C;
    uint8_t unknown2D;
    uint32_t flags30;                                   // 0x08: starts a new line
};

// A new style (a copy of `desc`): its index.
int CreateTextStyle(const S_TextStyleDesc &desc);       // 0x00643c00 (__fastcall; 0x006420b0)
// The style freed (an index not in the list: nothing).
void ReleaseTextStyle(int style);                       // 0x00643bf0 (__fastcall; 0x00641fe0)
// The style at `style` (none, or freed: null). (The text boxes read the list where it is.)
const S_TextStyleDesc *GetTextStyle(int style);
// The list's size (the freed places at its end not in it).
int NumTextStyles();
