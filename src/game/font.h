// The menus' fonts (game/font.cpp, original source 0x0063c2f0..0x00641fb0, part 9a): the shared font
// manager (the fonts by their ids, loaded once for every user) and its texture fonts: a texture
// and a table of glyphs (.tga and .tbl files), drawn as quads they gather into a vertex buffer.
// The GDI fonts (Japanese) and FontConfig.hxml: part 9a-ii.
#pragma once

#include <cstdint>
#include <map>
#include <vector>

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
    S_Glyph m_default = {};                             // +0x04
    int m_id = 0;                                       // +0x18
    IImage *m_texture = nullptr;                        // +0x1C
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

    // FontConfig.hxml's fonts for `language` (part 9a-ii).
    bool LoadConfig(const char *path, const char *language);   // 0x0063eae0
    // A GDI font `face` drawn into a texture (part 9a-ii).
    bool AddGdiFont(const char *face, int textureWidth, int textureHeight, int size, int id, bool unknown);   // 0x0063ee20

private:
    int m_refs = 0;                                     // +0x04
    std::vector<C_Font *> m_fonts;                      // +0x10

    friend C_FontManager *AcquireFontManager();
    friend void ReleaseFontManager();
};
// The shared manager while it is acquired (none: null).
extern C_FontManager *g_fontManager;                    // 0x008acbdc
C_FontManager *AcquireFontManager();                    // 0x0063df00
// Releases the shared manager (when there is one).
void ReleaseFontManager();                              // 0x0063df30
