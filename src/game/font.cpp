#include "game/font.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>

#include "common/c_str.h"
#include "game/profile.h"
#include "game/xml_node.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"

// --- the texture fonts (part 9a-i) -----------------------------------------------------------------

namespace {

// The vertex buffer's first size (vertices; then doubled).
constexpr int FIRST_VERTICES = 0xC0;
// The quads' primitives: a triangle list, transformed.
constexpr int PRIMITIVE_TRIANGLES = 3;
// The textures' load mode.
constexpr uint32_t FONT_TEXTURE_MODE = 0x402;

} // namespace

C_TextureFont::~C_TextureFont()
{
    if (m_texture) {
        m_texture->Release();
        m_texture = nullptr;
    }
    if (m_vertices) {
        ::operator delete(m_vertices);
        m_vertices = nullptr;
    }
}

void C_TextureFont::GrowVertices()
{
    if (!m_vertices) {
        // (Quirk kept: a buffer gone with its size kept is not made again.)
        if (m_maxVertices == 0) {
            m_vertices = static_cast<S_FontVertex *>(::operator new(sizeof(S_FontVertex) * FIRST_VERTICES));
            m_maxVertices = FIRST_VERTICES;
        }
        return;
    }
    const int size = m_maxVertices;
    if (size <= 0)
        return;
    auto *vertices = static_cast<S_FontVertex *>(::operator new(sizeof(S_FontVertex) * 2 * size));
    std::memcpy(vertices, m_vertices, sizeof(S_FontVertex) * m_numVertices);
    ::operator delete(m_vertices);
    m_maxVertices = size * 2;
    m_vertices = vertices;
}

void C_TextureFont::Flush()
{
    if (m_numVertices != 0 && m_texture && m_vertices) {
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->SetTexture(m_texture, 0);
        GetIGraph()->DrawPrimitives(PRIMITIVE_TRIANGLES, m_numVertices / 3, m_vertices, 1);
#endif
    }
}

void C_TextureFont::ClearVertices()
{
    m_numVertices = 0;
}

void C_TextureFont::Select()
{
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    if (m_texture)
        GetIGraph()->SetTexture(m_texture, 0);
#endif
}

const S_Glyph *C_TextureFont::GetGlyph(int ch)
{
    return static_cast<unsigned>(ch) < 256 ? &m_table[ch] : &m_default;
}

void C_TextureFont::AddQuad(int ch, uint32_t color, int width, int height, int x, int y, float z)
{
    if (m_maxVertices < m_numVertices + 6)
        GrowVertices();
    if (!m_vertices)
        return;
    // Two triangles: bottom left, top left, top right; bottom right, bottom left, top right.
    const S_Glyph &g = m_table[ch & 0xFF];
    const float left = static_cast<float>(x);
    const float right = static_cast<float>(width + x);
    const float bottom = static_cast<float>(y);
    const float top = static_cast<float>(y - height);
    const float rhw = 1.0f / z;
    const struct {
        float x, y, u, v;
    } corners[6] = {{left, bottom, g.u0, g.v1}, {left, top, g.u0, g.v0},     {right, top, g.u1, g.v0},
                    {right, bottom, g.u1, g.v1}, {left, bottom, g.u0, g.v1}, {right, top, g.u1, g.v0}};
    for (const auto &c : corners) {
        S_FontVertex &v = m_vertices[m_numVertices++];
        v.x = c.x;
        v.y = c.y;
        v.z = z;
        v.rhw = rhw;
        v.color = color;
        v.u = c.u;
        v.v = c.v;
    }
}

bool C_TextureFont::Load(const char *texture, const char *table)
{
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
    if (GetIGraph()->LoadTexture(texture, nullptr, FONT_TEXTURE_MODE, &m_texture) < 0) {
        m_texture = nullptr;
        return false;
    }
#else
    m_texture = nullptr;
#endif
    std::memset(m_table, 0, sizeof m_table);
    const int file = dtaOpen(table, false);
    if (file == -1) {
        if (m_texture) {
            m_texture->Release();
            m_texture = nullptr;
        }
        return false;
    }
    dtaRead(file, m_table, sizeof m_table);
    dtaClose(file);
    return true;
}

bool C_TextureFont::Load(const char *texture, const char *table, int id)
{
    if (!Load(texture, table))
        return false;
    m_id = id;
    return true;
}

const S_Glyph *C_MapFont::GetGlyph(int ch)
{
    const auto it = m_glyphs.find(ch);
    return it != m_glyphs.end() ? &it->second : &m_default;
}

bool C_MapFont::Load(const char *texture, const char *table)
{
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
    if (GetIGraph()->LoadTexture(texture, nullptr, FONT_TEXTURE_MODE, &m_texture) < 0) {
        m_texture = nullptr;
        return false;
    }
#else
    m_texture = nullptr;
#endif
    m_glyphs.clear();
    const int file = dtaOpen(table, false);
    if (file == -1) {
        if (m_texture) {
            m_texture->Release();
            m_texture = nullptr;
        }
        return false;
    }
    // (The entries' reads are not checked: a short file repeats the last values.)
    uint32_t count = 0;
    int code = 0;
    S_Glyph glyph = {};
    if (dtaRead(file, &count, sizeof count) >= 0) {
        for (uint32_t i = 0; i < count; ++i) {
            dtaRead(file, &code, sizeof code);
            dtaRead(file, &glyph, sizeof glyph);
            m_glyphs.insert({code, glyph});            // (the first of a code kept)
        }
    }
    dtaClose(file);
    return true;
}

// --- the font manager (part 9a-i) ------------------------------------------------------------------

C_FontManager *g_fontManager;                           // 0x008acbdc

namespace {

C_FontManager g_fontManagerObject;                      // 0x008acbbc

// The screen's font set: 1 below 800 pixels wide, 2 below 1024, else 3.
int FontSet()
{
    const int width = GetIGraph()->ScrnX();
    return width >= 1024 ? 3 : width >= 800 ? 2 : 1;
}

// A font's two files in `dir`.
C_str FontTexture(const char *dir, const char *name) { return C_fstr("%s\\%s.tga", dir, name); }
C_str FontTable(const char *dir, const char *name) { return C_fstr("%s\\%s.tbl", dir, name); }

// The GDI fonts of a Japanese game by the set: the face, the texture's height, the size.
struct S_GdiFontSize {
    const char *face;
    int textureHeight;
    int size;
};
const S_GdiFontSize JAPANESE_BIG[3] = {
    {"MS UI Gothic", 0x200, 0x1F}, {"Lucida Sans Unicode", 0x200, 0x1C}, {"Lucida Sans Unicode", 0x400, 0x22}};
const S_GdiFontSize JAPANESE_SMALL[3] = {{"MS UI Gothic", 0x200, 0xE}, {"MS UI Gothic", 0x200, 0xF}, {"MS UI Gothic", 0x200, 0x14}};
constexpr int GDI_TEXTURE_WIDTH = 0x200;

// The languages with the big and small fonts of their own set (not FontConfig.hxml's).
const char *const SET_LANGUAGES[] = {"Czech", "Polish", "Italian", "French", "Spanish", "German", "English", "EnglishUS"};

} // namespace

C_FontManager *AcquireFontManager()
{
    if (!g_fontManager) {
        g_fontManager = &g_fontManagerObject;
        g_fontManagerObject.Load();
    }
    ++g_fontManager->m_refs;
    return g_fontManager;
}

void ReleaseFontManager()
{
    if (g_fontManager && --g_fontManager->m_refs == 0) {
        g_fontManager->Free();
        g_fontManager = nullptr;
    }
}

void SetFontBatching(bool on)
{
    g_fontManager->m_batching = on;
    if (on)
        for (size_t i = 0; i < g_fontManager->m_fonts.size(); ++i)
            g_fontManager->m_fonts[i]->ClearVertices();
}

void FlushFonts()
{
    for (size_t i = 0; i < g_fontManager->m_fonts.size(); ++i)
        g_fontManager->m_fonts[i]->Flush();
}

void C_FontManager::Release()
{
    if (--m_refs == 0) {
        Free();
        g_fontManager = nullptr;
    }
}

C_Font *C_FontManager::FindFont(int id)
{
    for (C_Font *font : m_fonts)
        if (font->GetId() == id)
            return font;
    return nullptr;
}

void C_FontManager::Free()
{
    for (size_t i = m_fonts.size(); i-- > 0;)
        delete m_fonts[i];
    m_fonts.clear();
}

bool C_FontManager::AddTextureFont(const char *texture, const char *table, int id)
{
    auto *font = new C_TextureFont();
    if (!font->Load(texture, table, id)) {
        delete font;
        return false;
    }
    m_fonts.push_back(font);
    return true;
}

bool C_FontManager::AddMapFont(const char *texture, const char *table, int id)
{
    auto *font = new C_MapFont();
    if (!font->Load(texture, table, id)) {
        delete font;
        return false;
    }
    m_fonts.push_back(font);
    return true;
}

void C_FontManager::Load()
{
    int major = 0, minor = 0;
    GetGameVersion(&major, &minor);
    // (Dropped: the 1.0 and 1.1 versions' own font set, 0x0063f870; the version is 1.12.)
    if (major == 1 && minor < 2)
        return;
    const int set = FontSet();
    const char *const kind = g_gameProfile.IsLanguage("Czech") || g_gameProfile.IsLanguage("Polish") ? "Ce"
                             : g_gameProfile.IsLanguage("Japan")                                   ? "Japan"
                                                                                                   : "Ansi";
    const C_str dir = C_fstr("Text\\Fonts\\%s\\Set%d\\", kind, set);
    const C_str ceDir = C_fstr("Text\\Fonts\\Ce\\Set%d\\", set);
    AddTextureFont(FontTexture("Text\\Fonts\\", "SYMBOLS"), FontTable("Text\\Fonts\\", "SYMBOLS"), FONT_SYMBOLS);
    AddTextureFont(FontTexture(ceDir, "BIG"), FontTable(ceDir, "BIG"), FONT_CE_BIG);
    AddTextureFont(FontTexture(ceDir, "SMALL"), FontTable(ceDir, "SMALL"), FONT_CE_SMALL);

    bool ownSet = false;
    for (const char *language : SET_LANGUAGES)
        ownSet = ownSet || g_gameProfile.IsLanguage(language);
    if (ownSet) {
        AddTextureFont(FontTexture(dir, "BIG"), FontTable(dir, "BIG"), FONT_BIG);
        AddTextureFont(FontTexture(dir, "SMALL"), FontTable(dir, "SMALL"), FONT_SMALL);
        return;
    }
    C_str config = g_gameProfile.GetTextDirectory();
    config += "FontConfig.hxml";
    if (!g_gameProfile.IsLanguage("Japan")) {
        if (LoadConfig(config, g_gameProfile.GetLanguage()))
            return;
        AddTextureFont(FontTexture(dir, "BIG"), FontTable(dir, "BIG"), FONT_BIG);
        AddTextureFont(FontTexture(dir, "SMALL"), FontTable(dir, "SMALL"), FONT_SMALL);
        return;
    }
    // Japanese: the configuration's fonts, else GDI fonts, else the set's (by code).
    if (LoadConfig(config, "Japanese"))
        return;
    if (!FindFont(FONT_BIG)) {
        const S_GdiFontSize &big = JAPANESE_BIG[set - 1];
        if (!AddGdiFont(big.face, GDI_TEXTURE_WIDTH, big.textureHeight, big.size, FONT_BIG, false))
            AddMapFont(FontTexture(dir, "BIG"), FontTable(dir, "BIG"), FONT_BIG);
    }
    if (FindFont(FONT_SMALL))
        return;
    const S_GdiFontSize &small = JAPANESE_SMALL[set - 1];
    if (!AddGdiFont(small.face, GDI_TEXTURE_WIDTH, small.textureHeight, small.size, FONT_SMALL, false))
        AddMapFont(FontTexture(dir, "SMALL"), FontTable(dir, "SMALL"), FONT_SMALL);
}

// --- FontConfig.hxml (part 9a-ii) ------------------------------------------------------------------

namespace {

// The GDI fonts' texture width.
constexpr int CONFIG_TEXTURE_WIDTH = 0x200;

} // namespace

bool C_FontManager::LoadConfig(const char *path, const char *language)
{
    // <FONTCONFIG> <LANGUAGE name=".."> <SET resolution=".."> <FONT id="FONT_BIG" name=".." size=".."
    // bold="true" texture_height=".."/> ... (each found by the walk of the one above it).
    const int width = GetIGraph()->ScrnX();
    const char *const resolution = width >= 1024 ? "1024x768" : width >= 800 ? "800x600" : "640x480";
    C_XmlNode config;
    if (!config.Load(path) || !config.Next()->IsNamed("FONTCONFIG"))
        return false;
    C_XmlNode *entry = config.Next();
    for (; entry; entry = config.Next()) {
        // (Fix: an entry without a name skipped; the original compares through null.)
        const char *name = entry->IsNamed("LANGUAGE") ? entry->GetAttribute("name") : nullptr;
        if (name && _stricmp(name, language) == 0)
            break;
    }
    if (!entry)
        return false;
    C_XmlNode *const lang = entry;
    C_XmlNode *set = lang->Next();
    for (; set; set = lang->Next()) {
        const char *at = set->IsNamed("SET") ? set->GetAttribute("resolution") : nullptr;
        if (at && std::strcmp(at, resolution) == 0)   // (Fix: as above)
            break;
    }
    if (!set)
        return false;
    // (Quirk kept: loaded once the set is found, whatever its fonts do; an entry missing a value
    // fails it.)
    bool loaded = true;
    for (C_XmlNode *font = set->Next(); font; font = set->Next()) {
        if (!font->IsNamed("FONT"))
            continue;
        const char *const id = font->GetAttribute("id");
        const char *const face = font->GetAttribute("name");
        const char *const size = font->GetAttribute("size");
        const char *const bold = font->GetAttribute("bold");
        const char *const textureHeight = font->GetAttribute("texture_height");
        if (!id || !face || !size || !bold || !textureHeight)
            return false;
        const int fontId = std::strcmp(id, "FONT_BIG") == 0 ? FONT_BIG : std::strcmp(id, "FONT_SMALL") == 0 ? FONT_SMALL : 0;
        if (fontId == 0)
            continue;
        loaded = AddGdiFont(face, CONFIG_TEXTURE_WIDTH, std::atoi(textureHeight), std::atoi(size), fontId,
                            std::strcmp(bold, "true") == 0) || loaded;
    }
    return loaded;
}

// --- the GDI fonts (part 9a-iii) -------------------------------------------------------------------

namespace {

// The DIB a glyph is drawn into: 64 x 64, 24-bit, bottom up.
constexpr int DIB_SIZE = 64;
constexpr int DIB_ROW = DIB_SIZE * 3;
// The font's size (pixels high): 8 .. 60; a cell at least 10 high.
constexpr int MIN_GDI_SIZE = 8;
constexpr int MAX_GDI_SIZE = 60;
constexpr int MIN_CELL = 10;
// The texture's flags; its rows (the copy's pitch: the texture's width, 512, in pixels).
constexpr uint32_t GDI_TEXTURE_FLAGS = 0x8402;
constexpr int TEXTURE_ROW = 0x200;
// The cells' gap and start.
constexpr int CELL_GAP = 1;

HDC Dc(void *dc) { return static_cast<HDC>(dc); }

} // namespace

C_GdiFont::C_GdiFont()
{
    Reset();
}

C_GdiFont::~C_GdiFont()
{
    Close();
    Reset();
}

bool C_GdiFont::Init(void *dc, const char *face, int textureWidth, int textureHeight, int size, bool bold)
{
    m_dc = CreateCompatibleDC(Dc(dc));
    if (!m_dc)
        return false;
    size = size > MAX_GDI_SIZE ? MAX_GDI_SIZE : size < MIN_GDI_SIZE ? MIN_GDI_SIZE : size;
    m_face = face;
    m_bold = bold;
    m_size = size;
    m_font = CreateFontA(size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                         CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, face);
    if (!m_font)
        return false;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = DIB_SIZE;
    info.bmiHeader.biHeight = DIB_SIZE;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    m_bitmap = CreateDIBSection(Dc(m_dc), &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    m_bits = static_cast<uint8_t *>(bits);
    if (!m_bitmap)
        return false;
    SelectObject(Dc(m_dc), static_cast<HGDIOBJ>(m_bitmap));
    SelectObject(Dc(m_dc), static_cast<HGDIOBJ>(m_font));
    SetTextColor(Dc(m_dc), RGB(255, 255, 255));
    SetBkColor(Dc(m_dc), RGB(0, 0, 0));
    SetMapMode(Dc(m_dc), MM_TEXT);
    // The cells: as high and as wide as "Wg" is high.
    SIZE extent = {};
    GetTextExtentPoint32W(Dc(m_dc), L"Wg", 2, &extent);
    if (extent.cy < MIN_CELL)
        extent.cy = MIN_CELL;
    m_cellHeight = m_cellWidth = extent.cy;
    m_textureWidth = textureWidth;
    m_textureHeight = textureHeight;
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
    m_image = GetIGraph()->CreateImage();
#else
    m_image = nullptr;
#endif
    if (!m_image)
        return false;
    m_image->Open(nullptr, nullptr, GDI_TEXTURE_FLAGS, textureWidth, textureHeight, 0);
    m_image->Fill(nullptr, 0xFFFFFF, 0);
    if (!MakeCells(m_cellWidth, m_cellHeight, CELL_GAP, CELL_GAP))
        return false;
    // (Fix: no cell, no spare; the original takes one from the empty list.)
    if (!m_free.empty()) {
        m_spare = m_free.back();
        m_free.pop_back();
    }
    m_newest = m_cursor = m_oldest = nullptr;
    m_frame = 1;
    return true;
}

bool C_GdiFont::MakeCells(int width, int height, int gap, int start)
{
    const int stepX = width + gap;
    const int stepY = height + start;
    for (int y = start; stepY - 1 + y < m_textureHeight; y += stepY) {
        for (int x = gap; stepX - 1 + x < m_textureWidth; x += stepX) {
            // (Fix: zero; the original's glyph and links are not set.)
            auto *cell = new S_GlyphCell{};
            cell->x = x;
            cell->y = y;
            cell->right = width - 1 + x;
            cell->bottom = height - 1 + y;
            cell->ch = 0;
            m_free.push_back(cell);
        }
    }
    return true;
}

void C_GdiFont::Close()
{
    if (m_image) {
        m_image->Release();
        m_image = nullptr;
    }
    m_bits = nullptr;
    // (Quirk kept: the handles stay; a second Close deletes them again, as the destructor's does.)
    DeleteDC(Dc(m_dc));
    DeleteObject(static_cast<HGDIOBJ>(m_bitmap));
    DeleteObject(static_cast<HGDIOBJ>(m_font));
    delete m_spare;
    m_spare = nullptr;
    m_cells.clear();
    for (size_t i = m_free.size(); i-- > 0;)
        delete m_free[i];
    m_free.clear();
    // The cells in use: the use order owns them.
    while (S_GlyphCell *cell = m_newest) {
        m_newest = cell->older;
        delete cell;
    }
    m_newest = m_cursor = m_oldest = nullptr;
}

void C_GdiFont::Reset()
{
    m_dc = nullptr;
    m_bitmap = nullptr;
    m_bits = nullptr;
    m_cellWidth = m_cellHeight = -1;
    m_font = nullptr;
    m_size = 0;
    m_image = nullptr;
    m_textureWidth = m_textureHeight = -1;
    m_frame = 0;
    m_spare = nullptr;
    m_free.clear();                                     // (the cells not deleted)
    m_cells.clear();
    m_newest = m_cursor = m_oldest = nullptr;
}

void C_GdiFont::Select()
{
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    if (m_image)
        GetIGraph()->SetTexture(m_image, 0);
#endif
    ++m_frame;
}

S_GlyphCell *C_GdiFont::TakeCell()
{
    if (!m_free.empty()) {
        S_GlyphCell *cell = m_free.back();
        m_free.pop_back();
        cell->newer = cell->older = nullptr;
        cell->frame = 0;
        return cell;
    }
    // (Fix: none in use, none; the original reads through null.)
    return m_oldest && m_oldest->frame != m_frame ? m_oldest : nullptr;
}

const S_Glyph *C_GdiFont::GetGlyph(int ch)
{
    if (ch == '\t')
        ch = ' ';
    const uint16_t key = static_cast<uint16_t>(ch);
    S_GlyphCell *cell;
    const auto it = m_cells.find(key);
    if (it != m_cells.end()) {
        cell = it->second;
    } else {
        cell = TakeCell();
        if (!cell)
            return m_spare ? &m_spare->glyph : &m_default;   // (Fix: no spare: the default)
        if (cell->ch != 0) {
            m_cells.erase(cell->ch);
            cell->ch = 0;
        }
        DrawGlyph(cell, ch);
        cell->ch = key;
        m_cells.insert({key, cell});
    }
    Touch(cell);
    return &cell->glyph;
}

bool C_GdiFont::DrawGlyph(S_GlyphCell *cell, int ch)
{
    const WCHAR text = static_cast<WCHAR>(ch);
    SelectObject(Dc(m_dc), GetStockObject(BLACK_BRUSH));
    SelectObject(Dc(m_dc), GetStockObject(BLACK_PEN));
    Rectangle(Dc(m_dc), 0, 0, DIB_SIZE - 1, DIB_SIZE - 1);
    SIZE extent = {};
    GetTextExtentPoint32W(Dc(m_dc), &text, 1, &extent);
    ExtTextOutW(Dc(m_dc), 0, 0, ETO_OPAQUE, nullptr, &text, 1, nullptr);
    CopyToTexture(cell);
    const float width = static_cast<float>(m_textureWidth - 1);
    const float height = static_cast<float>(m_textureHeight - 1);
    cell->glyph.u0 = static_cast<float>(cell->x) / width;
    cell->glyph.u1 = static_cast<float>(extent.cx + cell->x) / width;
    cell->glyph.v0 = static_cast<float>(cell->y) / height;
    cell->glyph.v1 = static_cast<float>(cell->y + extent.cy) / height;
    cell->glyph.aspect = extent.cy == 0 ? 0.0f : static_cast<float>(extent.cx) / static_cast<float>(extent.cy);
    return true;
}

void C_GdiFont::CopyToTexture(const S_GlyphCell *cell)
{
    int pitch = 0;
    auto *pixels = static_cast<uint8_t *>(m_image->Lock(0, &pitch, 0));
    if (pixels) {
        // The DIB's rows from its top (its last in memory) into the cell's: white, the blue its alpha.
        const uint8_t *source = m_bits + (DIB_SIZE - 1) * DIB_ROW;
        int row = (cell->y * TEXTURE_ROW + cell->x) * 4;
        for (int line = DIB_SIZE - 1; line >= DIB_SIZE - m_cellHeight; --line) {
            uint8_t *out = pixels + row;
            const uint8_t *in = source;
            for (int column = 0; column < m_cellWidth; ++column) {
                out[0] = out[1] = out[2] = 0xFF;
                out[3] = in[0];
                out += 4;
                in += 3;
            }
            row += TEXTURE_ROW * 4;
            source -= DIB_ROW;
        }
    }
    m_image->Unlock();
}

void C_GdiFont::Touch(S_GlyphCell *cell)
{
    if (cell->frame == m_frame)
        return;
    cell->frame = m_frame;
    if (!m_newest) {
        if (!m_oldest) {
            cell->newer = cell->older = nullptr;
            m_cursor = nullptr;
            m_oldest = m_newest = cell;
        }
        return;
    }
    if (!m_oldest)
        return;
    if (cell == m_cursor)
        m_cursor = cell->older;
    if (cell == m_newest)
        return;
    // Out of its place (when it has one), then first.
    if (cell->older || cell->newer) {
        if (cell->older)
            cell->older->newer = cell->newer;
        if (cell->newer)
            cell->newer->older = cell->older;
        if (cell == m_oldest)
            m_oldest = cell->newer;
    }
    cell->newer = nullptr;
    cell->older = m_newest;
    m_newest->newer = cell;
    m_newest = cell;
}

bool C_FontManager::AddGdiFont(const char *face, int textureWidth, int textureHeight, int size, int id, bool bold)
{
    auto *font = new C_GdiFont();
    if (!font->Init(nullptr, face, textureWidth, textureHeight, size, bold)) {
        font->Close();
        delete font;
        return false;
    }
    font->m_id = id;
    m_fonts.push_back(font);
    return true;
}
