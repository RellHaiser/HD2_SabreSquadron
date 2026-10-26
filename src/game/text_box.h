// The game's text library (the menus' code, 0x0063de40..0x0064d060; not ported yet): fonts
// (FontConfig.hxml), text boxes on interface visuals and text styles. Declared here for the HUD,
// which uses it for its texts and the message log's glyphs.
#pragma once

#include <cstdint>

#include "common/c_str.h"

class I3D_frame;
class I3D_scene;

// A glyph of a font's texture: its texture rectangle and its width per height.
struct S_Glyph {                                        // 0x14 bytes
    float u0, v0, u1, v1;
    float aspect;                                       // 0: a blank (0.4 of the height wide)
};

class C_Font {
public:
    // Selects the font's texture for the next primitives.
    void Select();                                      // vtable +0x10
    const S_Glyph *GetGlyph(int ch);                    // vtable +0x14
};

// The shared font manager (FontConfig.hxml), reference counted.
class C_FontManager {
public:
    C_Font *FindFont(int id);                           // 0x0063df80
    void Release();                                     // 0x0063df60
};
C_FontManager *AcquireFontManager();                    // 0x0063df00
// Releases the shared manager (when there is one).
void ReleaseFontManager();                              // 0x0063df30

// A text style (CreateTextStyle): font, colour, size and placement in the text box (fractions of
// the box; -1: automatic) and flags. (The fields' meanings are inferred from the HUD's tables: the
// columns of a score table share +0x10 down the rows.)
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
    uint32_t flags2C;
    uint32_t flags30;                                   // 0x08: starts a new line
};
int CreateTextStyle(const S_TextStyleDesc &desc);       // 0x00643c00 (__fastcall)
void ReleaseTextStyle(int style);                       // 0x00643bf0 (__fastcall)

// Rich text in the rectangle of an interface visual: runs of text, each with a style (vtable
// 0x00810458; was C_auto_00810458).
class C_TextBox {                                       // 0x9C bytes
public:
    C_TextBox();                                        // 0x00496cc0
    ~C_TextBox();                                       // 0x0048ecf0
    C_TextBox(const C_TextBox &) = delete;
    C_TextBox &operator=(const C_TextBox &) = delete;

    void UpdateScrollRange();                           // vtable +0x00 0x006441a0
    void Render();                                      // vtable +0x04 0x00648200
    bool ScrollTo(int line);                            // vtable +0x08 0x00644250
    // (vtable +0x0C: the scalar deleting destructor 0x0048ecd0.)

    bool AddText(const C_str &text, uint8_t style);     // 0x00644830
    bool AddTextId(int textId, uint8_t style);          // 0x00644770
    // Run `index` replaced (out of range: appended).
    bool SetText(const C_str &text, uint8_t style, int index);   // 0x006446e0
    bool SetTextId(int textId, int style, int index);   // 0x00644520 (style 0x28: the text formatted with the version)
    void Clear();                                       // 0x00644c80
    // Takes the visual's screen rectangle (the interface scene's coordinates).
    void AttachTo(I3D_scene *scene, I3D_frame *visual); // 0x00644da0
    // A rectangle in pixels: x0, y0, x1, y1, width, height and the depth (0..1 of the camera's range).
    void SetRect(const float *rect);                    // 0x00644ea0
    // (Byte +0x90, 10 after construction; the HUD's panels set 0. Not identified.)
    void SetUnknown90(uint8_t value) { m_opaque[0x90] = value; }
    // (Byte +0x71, 0 after construction; the inventory sets it on a list's caption while the list
    // has no container. Not identified.)
    void SetUnknown71(bool value) { m_opaque[0x71] = value; }

private:
    uint8_t m_opaque[0x9C];
};

// UTF-8 of the menus' text input (the unicode font option).
bool ReadUnicodeInput(wchar_t *buffer, int maxLength);  // 0x0066b680 (__fastcall)
// One code point to UTF-8 (at most `maxBytes`; the bytes written, 0 or less: none).
int EncodeUtf8(char *out, const uint32_t *codePoint, int maxBytes);   // 0x0064d910 (__fastcall)
// The code point at `text` (`length` bytes left; the bytes it takes, 0 or less: invalid).
int DecodeUtf8(uint32_t *codePoint, const char *text, int length);    // 0x0064d830 (__fastcall)
