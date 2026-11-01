// The menus' fonts (game/font.cpp, original source 0x0063c2f0..0x00641fb0, part 9a): the shared font
// manager (the fonts by their ids, loaded once for every user) and its texture fonts: a texture
// and a table of glyphs (.tga and .tbl files), drawn as quads they gather into a vertex buffer.
// FontConfig.hxml: part 9a-ii; the GDI fonts (Japanese): part 9a-iii: TrueType fonts drawn by GDI,
// glyph by glyph as they are asked for, into the cells of a texture (the cells used least lately
// drawn over when they run out).
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "common/c_str.h"

class IImage;

// A glyph of a font's texture: its texture rectangle and its width per height.
struct S_Glyph {                                        // 0x14 bytes
    float u0, v0, u1, v1;
    float aspect;                                       // 0: a blank (0.4 of the height wide)
};

// A screen vertex of a font's quads (D3D's transformed and lit vertex; the interface's S_TLVertex).
struct S_FontVertex {                                   // 0x20 bytes
    float x, y, z, rhw;
    uint32_t color;                                     // ARGB
    uint32_t specular;                                  // (not set)
    float u, v;
};

// The fonts' ids (FindFont): the big and the small font of the language, the symbols, the big and the
// small central European ones (the debriefing's names).
enum : int {
    FONT_BIG = 2,
    FONT_SMALL = 3,
    FONT_SYMBOLS = 4,
    FONT_CE_BIG = 5,
    FONT_CE_SMALL = 6,
};

// A font (vtable 0x00813b2c, abstract): its quads gathered, then drawn at once.
class C_Font {
public:
    C_Font() = default;
    C_Font(const C_Font &) = delete;
    C_Font &operator=(const C_Font &) = delete;

    // Room for more quads.
    virtual void GrowVertices() = 0;                    // +0x00
    // The quads gathered drawn (with the font's texture; they stay gathered).
    virtual void Flush() = 0;                           // +0x04
    virtual void ClearVertices() = 0;                   // +0x08
    virtual ~C_Font() {}                                // +0x0C 0x0063efd0 (0x0063efc0)
    // Selects the font's texture for the next primitives.
    virtual void Select() = 0;                          // +0x10
    // The glyph of `ch` (none: the font's default, blank).
    virtual const S_Glyph *GetGlyph(int ch) = 0;        // +0x14
    // The quad of `ch` at (x, y), its bottom left (`width` by `height` pixels up), at depth `z`.
    virtual void AddQuad(int ch, uint32_t color, int width, int height, int x, int y, float z) = 0;   // +0x18

    int GetId() const { return m_id; }

protected:
    friend class C_FontManager;                         // (the GDI fonts' id)

    S_Glyph m_default = {};                             // +0x04
    int m_id = 0;                                       // +0x18
};

// A texture font with its glyphs in a table of 256 (vtable 0x00813b68; the .tbl file is the table).
class C_TextureFont : public C_Font {                   // 0x142C bytes
public:
    C_TextureFont() = default;
    ~C_TextureFont() override;                          // 0x00640540 (0x0063f3c0)

    void GrowVertices() override;                       // 0x00640740
    void Flush() override;                              // 0x006406d0
    void ClearVertices() override;                      // 0x006406c0
    void Select() override;                             // 0x006405c0
    const S_Glyph *GetGlyph(int ch) override;           // 0x006405e0
    void AddQuad(int ch, uint32_t color, int width, int height, int x, int y, float z) override;   // 0x006407e0
    // The texture and the glyphs' file. False when either does not open.
    virtual bool Load(const char *texture, const char *table);   // +0x1C 0x00640630

    // Load, the id kept when it loaded.
    bool Load(const char *texture, const char *table, int id);   // 0x00640600

protected:
    IImage *m_texture = nullptr;                        // +0x1C
    S_Glyph m_table[256] = {};                          // +0x20
    S_FontVertex *m_vertices = nullptr;                 // +0x1420
    int m_numVertices = 0;                              // +0x1424
    int m_maxVertices = 0;                              // +0x1428
};

// A texture font with its glyphs by code (vtable 0x00813b48; the .tbl file: a count, then each
// code and its glyph). Its quads still take their glyphs from the table (Fix: zero, the original's
// is not set).
class C_MapFont : public C_TextureFont {                // 0x143C bytes
public:
    C_MapFont() = default;
    ~C_MapFont() override = default;                    // 0x00640a00 (0x0063f3e0)

    const S_Glyph *GetGlyph(int ch) override;           // 0x00640cc0
    bool Load(const char *texture, const char *table) override;   // 0x00640d30
    using C_TextureFont::Load;

private:
    std::map<int, S_Glyph> m_glyphs;                    // +0x142C
};

// A cell of a GDI font's texture: its rectangle, its character, its glyph, the frame it was last
// used in and its neighbours in the use order (newest first).
struct S_GlyphCell {                                    // 0x34 bytes
    int x, y;                                           // +0x00
    int right, bottom;                                  // +0x08 (inclusive)
    uint16_t ch;                                        // +0x10 (0: none)
    S_Glyph glyph;                                      // +0x14
    int frame;                                          // +0x28
    S_GlyphCell *newer;                                 // +0x2C
    S_GlyphCell *older;                                 // +0x30
};

// A GDI font (vtable 0x00813b10): a TrueType font drawn into a 64 x 64 DIB a glyph at a time, then
// into a free cell of its texture (white, the glyph's coverage its alpha). Its quads are drawn by
// its users (its vertex slots do nothing).
class C_GdiFont : public C_Font {                       // 0x80 bytes
public:
    C_GdiFont();                                        // (inline in AddGdiFont 0x0063ee20)
    ~C_GdiFont() override;                              // 0x0063f050 (0x0063f030)

    void GrowVertices() override {}                     // 0x0063eff0
    void Flush() override {}                            // 0x0063f000
    void ClearVertices() override {}                    // 0x0063f010
    // The texture selected; a new frame of glyphs begins (the cells used in it are not drawn over).
    void Select() override;                             // 0x0063c9d0
    // The glyph of `ch` (a tab: a space's), drawn first when it is not in a cell (no cell free: the
    // spare cell's glyph).
    const S_Glyph *GetGlyph(int ch) override;           // 0x0063ced0
    void AddQuad(int, uint32_t, int, int, int, int, float) override {}   // 0x0063f020

    // The GDI objects, the texture and its cells for `face` (`size` pixels high, 8 .. 60). False
    // when one of them is not made (what was made stays, for Close).
    bool Init(void *dc, const char *face, int textureWidth, int textureHeight, int size, bool bold);   // 0x0063c2f0
    // The GDI objects, the texture and the cells freed.
    void Close();                                       // 0x0063c740
    // Every field cleared (the free cells forgotten).
    void Reset();                                       // 0x0063d310

private:
    // The texture cut into cells `width` x `height`, `gap` pixels apart, from `start`.
    bool MakeCells(int width, int height, int gap, int start);   // 0x0063c500
    // The cell for a new glyph: a free one, else the least lately used unless used in this frame.
    S_GlyphCell *TakeCell();                            // 0x0063d150
    // `ch` drawn into the DIB, copied into `cell`, its glyph's rectangle and width.
    bool DrawGlyph(S_GlyphCell *cell, int ch);          // 0x0063cda0
    void CopyToTexture(const S_GlyphCell *cell);        // 0x0063ccc0
    // The cell first in the use order (once a frame).
    void Touch(S_GlyphCell *cell);                      // 0x0063d0c0

    void *m_dc = nullptr;                               // +0x1C HDC
    void *m_bitmap = nullptr;                           // +0x20 the DIB
    uint8_t *m_bits = nullptr;                          // +0x24 its pixels (24-bit, bottom up)
    int m_cellWidth = -1;                               // +0x28
    int m_cellHeight = -1;                              // +0x2C
    void *m_font = nullptr;                             // +0x30 HFONT
    int m_size = 0;                                     // +0x34
    C_str m_face;                                       // +0x38
    bool m_bold = false;                                // +0x3C
    IImage *m_image = nullptr;                          // +0x40
    int m_textureWidth = -1;                            // +0x44
    int m_textureHeight = -1;                           // +0x48
    int m_frame = 0;                                    // +0x4C
    S_GlyphCell *m_spare = nullptr;                     // +0x50 the last cell, kept out
    std::vector<S_GlyphCell *> m_free;                  // +0x54
    std::map<uint16_t, S_GlyphCell *> m_cells;          // +0x64 by character
    S_GlyphCell *m_newest = nullptr;                    // +0x74
    S_GlyphCell *m_cursor = nullptr;                    // +0x78 (moved off a cell used again; not read)
    S_GlyphCell *m_oldest = nullptr;                    // +0x7C
};

// The shared font manager (its object 0x008acbbc), reference counted: the first AcquireFontManager
// loads the fonts, the last Release frees them.
class C_FontManager {
public:
    C_FontManager() = default;
    C_FontManager(const C_FontManager &) = delete;
    C_FontManager &operator=(const C_FontManager &) = delete;

    C_Font *FindFont(int id);                           // 0x0063df80
    void Release();                                     // 0x0063df60

    // The fonts for the screen's width and the language (1.0 / 1.1 versions: their own set,
    // dropped).
    void Load();                                        // 0x0063e000
    void Free();                                        // 0x0063edd0
    // A texture font (a table: C_TextureFont; by code: C_MapFont) added when it loads.
    bool AddTextureFont(const char *texture, const char *table, int id);   // 0x0063f590
    bool AddMapFont(const char *texture, const char *table, int id);       // 0x0063f400

    // FontConfig.hxml's GDI fonts for `language` and the screen's resolution (part 9a-ii).
    bool LoadConfig(const char *path, const char *language);   // 0x0063eae0
    // A GDI font `face` drawn into a texture (part 9a-iii).
    bool AddGdiFont(const char *face, int textureWidth, int textureHeight, int size, int id, bool unknown);   // 0x0063ee20
    // The text boxes' quads gathered in their fonts (SetFontBatching), not drawn glyph by glyph.
    bool IsBatching() const { return m_batching; }

private:
    int m_refs = 0;                                     // +0x04
    std::vector<C_Font *> m_fonts;                      // +0x10
    bool m_batching = false;                            // +0x1C

    friend C_FontManager *AcquireFontManager();
    friend void ReleaseFontManager();
    friend void SetFontBatching(bool on);
    friend void FlushFonts();
};
// The shared manager while it is acquired (none: null).
extern C_FontManager *g_fontManager;                    // 0x008acbdc
C_FontManager *AcquireFontManager();                    // 0x0063df00
// Releases the shared manager (when there is one).
void ReleaseFontManager();                              // 0x0063df30
// The shared manager's batching (on: every font's quads cleared first; a menu's render, its flag
// never set). Both need the manager acquired.
void SetFontBatching(bool on);                          // 0x0063f800 (__fastcall)
// Every font's gathered quads drawn.
void FlushFonts();                                      // 0x0063f7d0
