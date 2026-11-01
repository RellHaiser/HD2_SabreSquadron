// The game's text library (the menus' code, 0x0063de40..0x0064d060): fonts (game/font.h, part 9a),
// text boxes on interface visuals (not ported yet) and text styles (game/text_style.h, part 9a-iv). Declared here for the HUD,
// which uses it for its texts and the message log's glyphs.
#pragma once

#include <cstdint>

#include "common/c_str.h"
#include "game/font.h"
#include "game/text_style.h"

class I3D_frame;
class I3D_scene;


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
