// Part 9a-i's fonts (game/font.cpp: the font manager, the texture fonts), in the camera build, run
// by CameraTest: the profile's language and text directory, the game's version, FontConfig.hxml and
// the GDI fonts (part 9a-ii) and IGraph's ScrnX / LoadTexture / SetTexture / DrawPrimitives faked
// (logged); the .tbl files in g_files. Prints "fonts:" with 1 per check met.

#define private public
#define protected public
#include "game/font.h"
#undef protected
#undef private

size_t DtaOpenCount();                                  // (car_stubs.cpp)
// The profile's language, the game's version.
std::string g_foLanguage = "English";
int g_foMajor = 1, g_foMinor = 12;
C_str C_GameProfile::GetLanguage() { return C_str(g_foLanguage.c_str()); }
bool C_GameProfile::IsLanguage(const char *language)
{
    GfLog("islang %s", language);
    return g_foLanguage == language;
}
C_str C_GameProfile::GetTextDirectory() { return C_fstr("Text\\%s\\", g_foLanguage.c_str()); }
void __stdcall GetGameVersion(int *major, int *minor)
{
    *major = g_foMajor;
    *minor = g_foMinor;
}
// FontConfig.hxml and the GDI fonts: logged; their results (a GDI font: none added).
bool g_foConfig, g_foGdi;
bool C_FontManager::LoadConfig(const char *path, const char *language)
{
    GfLog("config %s %s", path, language);
    return g_foConfig;
}
bool C_FontManager::AddGdiFont(const char *face, int textureWidth, int textureHeight, int size, int id, bool unknown)
{
    GfLog("gdi %s %d %d %d %d %d", face, textureWidth, textureHeight, size, id, unknown);
    return g_foGdi;
}

namespace {

struct S_FontResults {
    bool foSets = true, foLanguages = true, foJapanese = true, foConfig = true, foVersion = true, foShared = true;
    bool foMissing = true, foTable = true, foQuads = true, foGrow = true, foFlush = true, foMap = true;
};

// IGraph: the screen's width; textures ("missing": not found; "fail1": the code 1, loaded) logged.
int g_foWidth = 640;
int __stdcall F_FoScrnX(void *) { return g_foWidth; }
HRESULT __stdcall F_FoLoadTexture(void *self, const char *name, const char *alpha, uint32_t mode, S_Image **out)
{
    GfLog("texture %s %d %x", name, alpha != nullptr, mode);
    if (std::strstr(name, "missing")) {
        *out = reinterpret_cast<S_Image *>(0x1);
        return -1;
    }
    F_LoadTexture(self, name, alpha, mode, out);
    return std::strstr(name, "fail1") ? 1 : 0;
}
void __stdcall F_FoSetTexture(void *, void *texture, int stage)
{
    GfLog("settex %s %d", texture ? static_cast<S_Image *>(texture)->name.c_str() : "-", stage);
}
std::vector<S_FontVertex> g_foDrawn;
void __stdcall F_FoDraw(void *, int kind, int count, const void *vertices, int transformed)
{
    GfLog("draw %d %d %d", kind, count, transformed);
    const auto *v = static_cast<const S_FontVertex *>(vertices);
    g_foDrawn.assign(v, v + count * 3);
}

std::vector<uint8_t> FoBytes(const void *data, size_t size)
{
    const auto *p = static_cast<const uint8_t *>(data);
    return std::vector<uint8_t>(p, p + size);
}
// A table font's file: glyph i (u0 i, v0 i + 0.25, u1 i + 0.5, v1 i + 0.75, aspect i / 10).
std::vector<uint8_t> FoTable()
{
    S_Glyph table[256];
    for (int i = 0; i < 256; ++i)
        table[i] = {float(i), i + 0.25f, i + 0.5f, i + 0.75f, i / 10.0f};
    return FoBytes(table, sizeof table);
}
// Every font's files of the manager's paths (the tables: FoTable).
void FoAddFiles(const char *kind, int set)
{
    const std::string dir = CmText("Text\\Fonts\\%s\\Set%d\\", kind, set);
    const std::string ce = CmText("Text\\Fonts\\Ce\\Set%d\\", set);
    for (const std::string &f : {std::string("Text\\Fonts\\\\SYMBOLS.tbl"), ce + "\\BIG.tbl", ce + "\\SMALL.tbl",
                                 dir + "\\BIG.tbl", dir + "\\SMALL.tbl"})
        g_files[f] = FoTable();
}
// The log's lines starting with `prefix`.
std::vector<std::string> FoLines(const char *prefix)
{
    std::vector<std::string> out;
    for (const std::string &l : g_gfLog)
        if (l.compare(0, std::strlen(prefix), prefix) == 0)
            out.push_back(l);
    return out;
}
// The fonts' ids in the manager's order.
std::vector<int> FoIds(C_FontManager *m)
{
    std::vector<int> ids;
    for (C_Font *f : m->m_fonts)
        ids.push_back(f->GetId());
    return ids;
}
// A fresh shared manager (the last one released).
C_FontManager *FoLoad()
{
    while (g_fontManager)
        ReleaseFontManager();
    g_gfLog.clear();
    return AcquireFontManager();
}
bool FoVertexIs(const S_FontVertex &v, float x, float y, float z, float rhw, uint32_t color, float u, float vv)
{
    return v.x == x && v.y == y && v.z == z && v.rhw == rhw && v.color == color && v.u == u && v.v == vv;
}

} // namespace

void FontTest()
{
    S_FontResults r;
    void *const oldSlots[] = {g_igraphVtable[0x18 / 4], g_igraphVtable[0x3C / 4], g_igraphVtable[0x44 / 4],
                              g_igraphVtable[0x88 / 4]};
    g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&F_FoScrnX);
    g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_FoLoadTexture);
    g_igraphVtable[0x44 / 4] = reinterpret_cast<void *>(&F_FoSetTexture);
    g_igraphVtable[0x88 / 4] = reinterpret_cast<void *>(&F_FoDraw);
    g_foConfig = g_foGdi = false;

    // --- the sets by the screen's width; a language with its own set ---
    {
        g_foLanguage = "English";
        const int widths[] = {640, 799, 800, 1023, 1024, 1600};
        const int sets[] = {1, 1, 2, 2, 3, 3};
        for (int i = 0; i < 6; ++i) {
            g_foWidth = widths[i];
            FoAddFiles("Ansi", sets[i]);
            C_FontManager *m = FoLoad();
            const std::string set = CmText("Set%d", sets[i]);
            const std::vector<std::string> tex = FoLines("texture");
            WM(r.foSets, FoIds(m) == std::vector<int>({4, 5, 6, 2, 3}) && tex.size() == 5 &&
                             tex[0] == "texture Text\\Fonts\\\\SYMBOLS.tga 0 402" &&
                             tex[1] == "texture Text\\Fonts\\Ce\\" + set + "\\\\BIG.tga 0 402" &&
                             tex[2] == "texture Text\\Fonts\\Ce\\" + set + "\\\\SMALL.tga 0 402" &&
                             tex[3] == "texture Text\\Fonts\\Ansi\\" + set + "\\\\BIG.tga 0 402" &&
                             tex[4] == "texture Text\\Fonts\\Ansi\\" + set + "\\\\SMALL.tga 0 402" &&
                             FoLines("config").empty());
        }
        g_foWidth = 640;
        FoAddFiles("Ce", 1);
        FoAddFiles("Japan", 1);
        // Each of the set's languages; Czech and Polish from the Ce set.
        for (const char *language : {"Czech", "Polish", "Italian", "French", "Spanish", "German", "English", "EnglishUS"}) {
            g_foLanguage = language;
            C_FontManager *m = FoLoad();
            const char *kind = g_foLanguage == "Czech" || g_foLanguage == "Polish" ? "Ce" : "Ansi";
            const std::vector<std::string> tex = FoLines("texture");
            WM(r.foLanguages, FoIds(m) == std::vector<int>({4, 5, 6, 2, 3}) && tex.size() == 5 &&
                                  tex[3] == CmText("texture Text\\Fonts\\%s\\Set1\\\\BIG.tga 0 402", kind) &&
                                  FoLines("config").empty());
        }
    }

    // --- Japanese: the configuration, else GDI fonts by the set, else the map fonts ---
    {
        g_foLanguage = "Japan";
        g_foConfig = true;
        C_FontManager *m = FoLoad();
        WM(r.foJapanese, FoIds(m) == std::vector<int>({4, 5, 6}) && FoLines("config") ==
                                                                         std::vector<std::string>{"config Text\\Japan\\FontConfig.hxml Japanese"} &&
                             FoLines("gdi").empty() && FoLines("texture").size() == 3 &&
                             FoLines("texture")[1] == "texture Text\\Fonts\\Ce\\Set1\\\\BIG.tga 0 402");
        g_foConfig = false;
        const int widths[] = {640, 800, 1024};
        const char *const big[] = {"gdi MS UI Gothic 512 512 31 2 0", "gdi Lucida Sans Unicode 512 512 28 2 0",
                                   "gdi Lucida Sans Unicode 512 1024 34 2 0"};
        const char *const small[] = {"gdi MS UI Gothic 512 512 14 3 0", "gdi MS UI Gothic 512 512 15 3 0",
                                     "gdi MS UI Gothic 512 512 20 3 0"};
        for (int i = 0; i < 3; ++i) {
            g_foWidth = widths[i];
            FoAddFiles("Japan", i + 1);
            g_foGdi = true;
            m = FoLoad();
            WM(r.foJapanese, FoIds(m) == std::vector<int>({4, 5, 6}) &&
                                 FoLines("gdi") == std::vector<std::string>({big[i], small[i]}));
            // The GDI fonts failing: the set's, by code.
            g_foGdi = false;
            m = FoLoad();
            WM(r.foJapanese, FoIds(m) == std::vector<int>({4, 5, 6, 2, 3}) &&
                                 FoLines("gdi") == std::vector<std::string>({big[i], small[i]}) &&
                                 dynamic_cast<C_MapFont *>(m->FindFont(2)) && dynamic_cast<C_MapFont *>(m->FindFont(3)) &&
                                 !dynamic_cast<C_MapFont *>(m->FindFont(4)) &&
                                 FoLines("texture").back() == CmText("texture Text\\Fonts\\Japan\\Set%d\\\\SMALL.tga 0 402", i + 1));
        }
        g_foWidth = 640;
    }

    // --- another language: FontConfig.hxml for it, else the set's ---
    {
        g_foLanguage = "Russian";
        g_foConfig = true;
        C_FontManager *m = FoLoad();
        WM(r.foConfig, FoIds(m) == std::vector<int>({4, 5, 6}) &&
                           FoLines("config") == std::vector<std::string>{"config Text\\Russian\\FontConfig.hxml Russian"} &&
                           FoLines("gdi").empty());
        g_foConfig = false;
        FoAddFiles("Ansi", 1);
        m = FoLoad();
        WM(r.foConfig, FoIds(m) == std::vector<int>({4, 5, 6, 2, 3}) && FoLines("gdi").empty() &&
                           !dynamic_cast<C_MapFont *>(m->FindFont(2)) &&
                           FoLines("texture")[3] == "texture Text\\Fonts\\Ansi\\Set1\\\\BIG.tga 0 402");
        g_foLanguage = "English";
    }

    // --- 1.0 and 1.1: none (their set dropped); 1.2 on and 2.x: the fonts ---
    {
        for (int minor : {0, 1}) {
            g_foMinor = minor;
            WM(r.foVersion, FoLoad()->m_fonts.empty() && g_gfLog.empty());
        }
        g_foMinor = 2;
        WM(r.foVersion, FoLoad()->m_fonts.size() == 5);
        g_foMajor = 2;
        g_foMinor = 0;
        WM(r.foVersion, FoLoad()->m_fonts.size() == 5);
        g_foMajor = 1;
        g_foMinor = 12;
    }

    // --- shared: loaded once; the last release frees (the textures released), the next loads ---
    {
        C_FontManager *m = FoLoad();
        S_Image *const tex = static_cast<S_Image *>(static_cast<void *>(static_cast<C_TextureFont *>(m->FindFont(2))->m_texture));
        S_Image *const first = static_cast<S_Image *>(static_cast<void *>(static_cast<C_TextureFont *>(m->m_fonts[0])->m_texture));
        WM(r.foShared, AcquireFontManager() == m && FoLines("texture").size() == 5 && m->m_refs == 2 && tex->refs == 1);
        ReleaseFontManager();
        WM(r.foShared, g_fontManager == m && m->m_refs == 1 && tex->refs == 1);
        m->Release();
        WM(r.foShared, g_fontManager == nullptr && m->m_fonts.empty() && tex->refs == 0 && first->refs == 0);
        ReleaseFontManager();                           // (none: nothing)
        g_gfLog.clear();
        WM(r.foShared, AcquireFontManager() == m && FoLines("texture").size() == 5 && m->m_refs == 1);
        WM(r.foShared, m->FindFont(7) == nullptr && m->FindFont(6) == m->m_fonts[2]);
        m->Release();
    }

    // --- missing files: no font (a texture loaded without its table released) ---
    {
        C_FontManager m;
        g_files["fo.tbl"] = FoTable();
        g_gfLog.clear();
        WM(r.foMissing, !m.AddTextureFont("missing.tga", "fo.tbl", 7) && m.m_fonts.empty() && FoLines("texture").size() == 1);
        WM(r.foMissing, !m.AddTextureFont("a.tga", "none.tbl", 7) && m.m_fonts.empty() && g_images.back()->refs == 0);
        WM(r.foMissing, !m.AddMapFont("missing.tga", "fo.tbl", 7) && !m.AddMapFont("b.tga", "none.tbl", 7) &&
                            m.m_fonts.empty() && g_images.back()->name == "b.tga" && g_images.back()->refs == 0);
        // A texture's code above 0: loaded.
        WM(r.foMissing, m.AddTextureFont("fail1.tga", "fo.tbl", 8) && m.FindFont(8));
        m.Free();
    }

    // --- a table font: its glyphs, the default, the texture ---
    {
        C_TextureFont font;
        g_files["fo.tbl"] = FoTable();
        g_dtaArchivesFirst = true;
        const size_t open = DtaOpenCount();
        WM(r.foTable, font.Load("t.tga", "fo.tbl", 9) && font.GetId() == 9 && !g_dtaArchivesFirst && DtaOpenCount() == open);
        WM(r.foTable, font.GetGlyph('A')->u0 == 65.0f && font.GetGlyph(255)->aspect == 25.5f &&
                          font.GetGlyph(0)->v1 == 0.75f);
        WM(r.foTable, font.GetGlyph(256) == &font.m_default && font.GetGlyph(-1) == &font.m_default &&
                          font.m_default.aspect == 0.0f);
        g_gfLog.clear();
        font.Select();
        WM(r.foTable, GfLogIs({"settex t.tga 0"}));
        // A short table: the rest zero; a second load replaces it.
        const std::vector<uint8_t> full = FoTable();
        g_files["fo.tbl"] = std::vector<uint8_t>(full.begin(), full.begin() + 0x14 * 2);
        WM(r.foTable, font.Load("t2.tga", "fo.tbl") && font.GetGlyph(1)->u0 == 1.0f && font.GetGlyph(2)->aspect == 0.0f &&
                          font.GetGlyph(255)->u0 == 0.0f);
        // A failed load: the id kept.
        WM(r.foTable, !font.Load("missing.tga", "fo.tbl", 10) && font.GetId() == 9 && font.m_texture == nullptr);
        g_gfLog.clear();
        font.Select();
        WM(r.foTable, g_gfLog.empty());
    }

    // --- the quads: six vertices each, by the glyph ---
    {
        C_TextureFont font;
        g_files["fo.tbl"] = FoTable();
        font.Load("q.tga", "fo.tbl");
        font.AddQuad(0x141, 0xFF102030, 10, 20, 100, 200, 0.5f);   // (the code's low byte: 'A')
        const S_FontVertex *v = font.m_vertices;
        WM(r.foQuads, font.m_numVertices == 6 && font.m_maxVertices == 0xC0 &&
                          FoVertexIs(v[0], 100, 200, 0.5f, 2.0f, 0xFF102030, 65.0f, 65.75f) &&
                          FoVertexIs(v[1], 100, 180, 0.5f, 2.0f, 0xFF102030, 65.0f, 65.25f) &&
                          FoVertexIs(v[2], 110, 180, 0.5f, 2.0f, 0xFF102030, 65.5f, 65.25f) &&
                          FoVertexIs(v[3], 110, 200, 0.5f, 2.0f, 0xFF102030, 65.5f, 65.75f) &&
                          FoVertexIs(v[4], 100, 200, 0.5f, 2.0f, 0xFF102030, 65.0f, 65.75f) &&
                          FoVertexIs(v[5], 110, 180, 0.5f, 2.0f, 0xFF102030, 65.5f, 65.25f));
        font.AddQuad(-1, 7, -3, 4, -5, 6, 0.25f);
        WM(r.foQuads, font.m_numVertices == 12 && FoVertexIs(v[6], -5, 6, 0.25f, 4.0f, 7, 255.0f, 255.75f) &&
                          FoVertexIs(v[8], -8, 2, 0.25f, 4.0f, 7, 255.5f, 255.25f));
        font.ClearVertices();
        WM(r.foQuads, font.m_numVertices == 0 && font.m_vertices == v);
    }

    // --- the buffer: 192 first, then doubled (the quads kept) ---
    {
        C_TextureFont font;
        g_files["fo.tbl"] = FoTable();
        font.Load("g.tga", "fo.tbl");
        for (int i = 0; i < 32; ++i)
            font.AddQuad(i, 0, 1, 1, i, 0, 1.0f);
        WM(r.foGrow, font.m_numVertices == 192 && font.m_maxVertices == 0xC0);
        font.AddQuad(40, 0, 1, 1, 7, 0, 1.0f);
        WM(r.foGrow, font.m_numVertices == 198 && font.m_maxVertices == 0x180 && font.m_vertices[6].x == 1.0f &&
                         font.m_vertices[186].x == 31.0f && font.m_vertices[192].x == 7.0f && font.m_vertices[192].u == 40.0f);
        // (At the size: room for one more quad only after it grows.)
        C_TextureFont exact;
        exact.Load("g.tga", "fo.tbl");
        exact.m_maxVertices = 0;
        exact.GrowVertices();
        exact.m_numVertices = 0xBA;
        exact.AddQuad(1, 0, 1, 1, 0, 0, 1.0f);
        WM(r.foGrow, exact.m_maxVertices == 0xC0 && exact.m_numVertices == 0xC0);
        exact.AddQuad(1, 0, 1, 1, 0, 0, 1.0f);
        WM(r.foGrow, exact.m_maxVertices == 0x180 && exact.m_numVertices == 0xC6);
        // A buffer gone with its size kept: none made (the quads dropped).
        ::operator delete(exact.m_vertices);
        exact.m_vertices = nullptr;
        exact.m_numVertices = 0;
        exact.AddQuad(1, 0, 1, 1, 0, 0, 1.0f);
        WM(r.foGrow, exact.m_vertices == nullptr && exact.m_numVertices == 0);
        exact.GrowVertices();
        WM(r.foGrow, exact.m_vertices == nullptr && exact.m_maxVertices == 0x180);
        // A size of 1: doubled.
        C_TextureFont one;
        one.m_maxVertices = 0;
        one.GrowVertices();
        one.m_maxVertices = 1;
        one.GrowVertices();
        WM(r.foGrow, one.m_maxVertices == 2 && one.m_vertices != nullptr);
        // A buffer whose size is 0 or less: kept as it is.
        font.m_maxVertices = 0;
        S_FontVertex *const kept = font.m_vertices;
        font.GrowVertices();
        WM(r.foGrow, font.m_vertices == kept && font.m_maxVertices == 0);
        font.m_maxVertices = 0x180;
    }

    // --- the flush: the texture and the triangles (kept gathered) ---
    {
        C_TextureFont font;
        g_files["fo.tbl"] = FoTable();
        g_gfLog.clear();
        font.Flush();
        WM(r.foFlush, g_gfLog.empty());
        font.Load("f.tga", "fo.tbl");
        g_gfLog.clear();
        font.Flush();                                   // (no quads)
        WM(r.foFlush, g_gfLog.empty());
        font.AddQuad('B', 1, 2, 3, 4, 5, 1.0f);
        font.AddQuad('C', 1, 2, 3, 4, 5, 1.0f);
        g_gfLog.clear();
        font.Flush();
        WM(r.foFlush, GfLogIs({"settex f.tga 0", "draw 3 4 1"}) && g_foDrawn.size() == 12 && g_foDrawn[6].u == 67.0f &&
                          font.m_numVertices == 12);
        // Without a texture: none.
        IImage *const tex = font.m_texture;
        font.m_texture = nullptr;
        g_gfLog.clear();
        font.Flush();
        WM(r.foFlush, g_gfLog.empty());
        font.m_texture = tex;
        // One vertex: no triangle drawn (still asked).
        font.m_numVertices = 1;
        g_gfLog.clear();
        font.Flush();
        WM(r.foFlush, GfLogIs({"settex f.tga 0", "draw 3 0 1"}));
        // Seven vertices: two triangles.
        font.m_numVertices = 7;
        g_gfLog.clear();
        font.Flush();
        WM(r.foFlush, GfLogIs({"settex f.tga 0", "draw 3 2 1"}));
    }

    // --- a map font: its glyphs by code (the first of a code kept), the default ---
    {
        C_MapFont font;
        std::vector<uint8_t> file;
        const auto add = [&file](int code, float u0) {
            const S_Glyph g = {u0, 2.0f, 3.0f, 4.0f, 0.5f};
            file.insert(file.end(), reinterpret_cast<const uint8_t *>(&code), reinterpret_cast<const uint8_t *>(&code) + 4);
            file.insert(file.end(), reinterpret_cast<const uint8_t *>(&g), reinterpret_cast<const uint8_t *>(&g) + sizeof g);
        };
        const uint32_t count = 4;
        file = FoBytes(&count, 4);
        add(0x3042, 1.0f);
        add(-7, 2.0f);
        add(0x3042, 3.0f);
        add(65, 4.0f);
        g_files["map.tbl"] = file;
        g_dtaArchivesFirst = true;
        const size_t open = DtaOpenCount();
        WM(r.foMap, font.Load("m.tga", "map.tbl", 11) && !g_dtaArchivesFirst && DtaOpenCount() == open && font.m_glyphs.size() == 3 && font.GetGlyph(0x3042)->u0 == 1.0f &&
                        font.GetGlyph(-7)->u0 == 2.0f && font.GetGlyph(65)->u0 == 4.0f &&
                        font.GetGlyph(66) == &font.m_default && font.GetGlyph(0x3043) == &font.m_default);
        // Its quads: the table's glyphs (zero).
        font.AddQuad(65, 0, 1, 1, 0, 0, 1.0f);
        WM(r.foMap, font.m_numVertices == 6 && font.m_vertices[0].u == 0.0f && font.m_vertices[2].u == 0.0f);
        // A count past the entries: the last read again; a new load clears the old glyphs.
        const uint32_t two = 3;
        file = FoBytes(&two, 4);
        add(5, 6.0f);
        add(9, 7.0f);
        g_files["map.tbl"] = file;
        WM(r.foMap, font.Load("m.tga", "map.tbl") && font.m_glyphs.size() == 2 && font.GetGlyph(9)->u0 == 7.0f &&
                        font.GetGlyph(0x3042) == &font.m_default);
        // A count without its entries: code 0, its glyph zero.
        const uint32_t lone = 1;
        g_files["map.tbl"] = FoBytes(&lone, 4);
        WM(r.foMap, font.Load("m.tga", "map.tbl") && font.m_glyphs.size() == 1 && font.GetGlyph(0) != &font.m_default &&
                        font.GetGlyph(0)->u0 == 0.0f);
        // An empty file: none.
        g_files["map.tbl"] = {};
        WM(r.foMap, font.Load("m.tga", "map.tbl") && font.m_glyphs.empty());
        g_files["map.tbl"] = std::vector<uint8_t>(4, 0);
        WM(r.foMap, font.Load("m.tga", "map.tbl") && font.m_glyphs.empty() && font.GetId() == 11);
    }

    while (g_fontManager)
        ReleaseFontManager();
    g_foLanguage = "English";
    g_foWidth = 640;
    g_igraphVtable[0x18 / 4] = oldSlots[0];
    g_igraphVtable[0x3C / 4] = oldSlots[1];
    g_igraphVtable[0x44 / 4] = oldSlots[2];
    g_igraphVtable[0x88 / 4] = oldSlots[3];
    std::printf("fonts: foSets %d foLanguages %d foJapanese %d foConfig %d foVersion %d foShared %d foMissing %d "
                "foTable %d foQuads %d foGrow %d foFlush %d foMap %d\n",
                r.foSets, r.foLanguages, r.foJapanese, r.foConfig, r.foVersion, r.foShared, r.foMissing, r.foTable,
                r.foQuads, r.foGrow, r.foFlush, r.foMap);
}
