// Part 9a-i's fonts (game/font.cpp: the font manager, the texture fonts), in the camera build, run
// by CameraTest: the profile's language and text directory, the game's version, FontConfig.hxml and
// the GDI fonts (part 9a-ii) and IGraph's ScrnX / LoadTexture / SetTexture / DrawPrimitives faked
// (logged); the .tbl files in g_files. Prints "fonts:" with 1 per check met.

#define private public
#define protected public
#include "game/font.h"
#include "game/xml_node.h"
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
// The GDI fonts (part 9a-iii): logged; their result (none added).
bool g_foGdi;
bool C_FontManager::AddGdiFont(const char *face, int textureWidth, int textureHeight, int size, int id, bool unknown)
{
    GfLog("gdi %s %d %d %d %d %d", face, textureWidth, textureHeight, size, id, unknown);
    return g_foGdi;
}

namespace {

struct S_FontResults {
    bool foSets = true, foLanguages = true, foJapanese = true, foConfig = true, foVersion = true, foShared = true;
    bool foMissing = true, foTable = true, foQuads = true, foGrow = true, foFlush = true, foMap = true;
    bool xmTree = true, xmText = true, xmComments = true, xmAttributes = true, xmBad = true, xmWalk = true;
    bool xmFile = true, cfLanguage = true, cfSet = true, cfFonts = true, cfValues = true;
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
// FontConfig.hxml in Text\<dir>\: `language`'s 640x480 set with one big font.
void FoConfigFile(const char *dir, const char *language)
{
    const std::string text = CmText("<FONTCONFIG><LANGUAGE name=\"%s\"><SET resolution=\"640x480\">"
                                    "<FONT id=\"FONT_BIG\" name=\"Cfg Face\" size=\"20\" bold=\"true\" "
                                    "texture_height=\"256\"/></SET></LANGUAGE></FONTCONFIG>",
                                    language);
    g_files[CmText("Text\\%s\\FontConfig.hxml", dir)] = std::vector<uint8_t>(text.begin(), text.end());
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
    g_foGdi = false;

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
                             FoLines("gdi").empty());
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
                                  FoLines("gdi").empty());
        }
    }

    // --- Japanese: the configuration, else GDI fonts by the set, else the map fonts ---
    {
        g_foLanguage = "Japan";
        FoConfigFile("Japan", "japanese");
        C_FontManager *m = FoLoad();
        WM(r.foJapanese, FoIds(m) == std::vector<int>({4, 5, 6}) &&
                             FoLines("gdi") == std::vector<std::string>{"gdi Cfg Face 512 256 20 2 1"} &&
                             FoLines("texture").size() == 3 &&
                             FoLines("texture")[1] == "texture Text\\Fonts\\Ce\\Set1\\\\BIG.tga 0 402");
        g_files.erase("Text\\Japan\\FontConfig.hxml");
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
        FoConfigFile("Russian", "RUSSIAN");
        C_FontManager *m = FoLoad();
        WM(r.foConfig, FoIds(m) == std::vector<int>({4, 5, 6}) &&
                           FoLines("gdi") == std::vector<std::string>{"gdi Cfg Face 512 256 20 2 1"});
        g_files.erase("Text\\Russian\\FontConfig.hxml");
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


    // --- part 9a-ii: the XML reader ---
    {
        const auto file = [](const std::string &text) {
            g_files["xm.hxml"] = std::vector<uint8_t>(text.begin(), text.end());
            return "xm.hxml";
        };
        const size_t open = DtaOpenCount();
        g_dtaArchivesFirst = true;
        {
            C_XmlNode root;
            WM(r.xmTree, root.Load(file("<root a=\"1\" b=\"two\"><child x=\"y\">hello</child><empty/><child2><g/></child2></root>")) &&
                             !g_dtaArchivesFirst && DtaOpenCount() == open);
            WM(r.xmTree, root.GetName() == "root" && root.m_attributes.size() == 2 && std::string(root.GetAttribute("a")) == "1" &&
                             std::string(root.GetAttribute("b")) == "two" && root.GetAttribute("c") == nullptr &&
                             root.NumChildren() == 3);
            WM(r.xmTree, root.NumChildren() == 3 && root.GetChild(0)->GetName() == "child" &&
                             std::string(root.GetChild(0)->GetAttribute("x")) == "y" && root.GetChild(0)->GetText() == "hello" &&
                             root.GetChild(1)->GetName() == "empty" && root.GetChild(1)->NumChildren() == 0 &&
                             root.GetChild(1)->m_attributes.empty() && root.GetChild(2)->GetChild(0)->GetName() == "g" &&
                             root.IsNamed("root") && !root.IsNamed("roo") && !root.IsNamed("roots"));
            // Text: an element without children's, its spaces kept; with children none.
            WM(r.xmText, root.GetText() == "" && root.GetChild(2)->GetText() == "");
        }
        {
            C_XmlNode node;
            WM(r.xmText, node.Load(file("<tx>  a b \n</tx>")) && node.GetText() == "  a b \n");
            C_XmlNode empty;
            WM(r.xmText, empty.Load(file("<tx></tx>")) && empty.GetText() == "");
            // (Fix: at most 4095 characters.)
            C_XmlNode big;
            WM(r.xmText, big.Load(file("<tx>" + std::string(5000, 'z') + "</tx>")) && big.GetText().Size() == 4095);
            // A tag with its attributes and spaces; a name ended by '>'.
            C_XmlNode spaced;
            WM(r.xmText, spaced.Load(file("<sp  k=\"v\"   ><in>t</in></sp>")) && std::string(spaced.GetAttribute("k")) == "v" &&
                             spaced.GetChild(0)->GetText() == "t");
        }
        {
            C_XmlNode node;
            WM(r.xmComments, node.Load(file("<!-- first --><!-- 2nd --><cm><!-- in --><aa/><!--x--><bb/></cm>")) &&
                                 node.GetName() == "cm" && node.NumChildren() == 2 && node.GetChild(1)->GetName() == "bb");
            C_XmlNode bad;
            WM(r.xmComments, !bad.Load(file("<!-><cm></cm>")) && !bad.Load(file("<!--><cm></cm>")) &&
                                 !bad.Load(file("<cm><!-- open</cm>")) && !bad.Load(file("<cm><!x--></cm>")));
            // An empty comment (its end 4 after its start), one ending inside its start: none.
            C_XmlNode four, three;
            WM(r.xmComments, four.Load(file("<!----><cm/>")) && four.GetName() == "cm" && !three.Load(file("<!---><cm/>")));
            // Text after a comment: the text from the content's start, the comment in it.
            C_XmlNode text;
            WM(r.xmComments, text.Load(file("<cm><!--c-->xy</cm>")) && text.GetText() == "<!--c-->xy");
            // A comment's end looked for from where the scan is.
            C_XmlNode after;
            WM(r.xmComments, after.Load(file("<!-- a --> <cm2>t</cm2>")) && after.GetName() == "cm2" && after.GetText() == "t");
        }
        {
            C_XmlNode node;
            WM(r.xmAttributes, node.Load(file("<at z=\"3\" y=\"2\" x=\"1\" z=\"4\"/>")) && node.m_attributes.size() == 4 &&
                                   node.m_attributes[0].first == "z" && node.m_attributes[2].second == "1" &&
                                   std::string(node.GetAttribute("z")) == "3");
            // An empty value: the closing quote past it (quirk kept): the element fails.
            C_XmlNode empty;
            WM(r.xmAttributes, !empty.Load(file("<at v=\"\" w=\"x\">t</at>")) && empty.m_attributes.size() == 1 &&
                                   std::string(empty.GetAttribute("v")) == "\" w=");
            // A name only, no quotes, a value to the tag's end: the element fails.
            C_XmlNode bad;
            WM(r.xmAttributes, !bad.Load(file("<at d>t</at>")) && !bad.Load(file("<at d=1>t</at>")) &&
                                   !bad.Load(file("<at d=\"1>t</at>")) && !bad.Load(file("<at =\"1\">t</at>")));
            C_XmlNode close;
            WM(r.xmAttributes, close.Load(file("<at d=\"1\" />")) && std::string(close.GetAttribute("d")) == "1");
            // A name with a quote in it; a value without quotes; a value not closed.
            C_XmlNode quoted;
            WM(r.xmAttributes, quoted.Load(file("<at k\"x=\"v\"/>")) && quoted.m_attributes.size() == 1 &&
                                   quoted.m_attributes[0].first == "k\"x" && std::string(quoted.GetAttribute("k\"x")) == "v");
            C_XmlNode bare, open;
            WM(r.xmAttributes, !bare.Load(file("<at k=123>t</at>")) && !open.Load(file("<at k=\"12>t</at>")));
            // A control character: a character like the others (in a name, before a name, in a text).
            C_XmlNode control;
            WM(r.xmAttributes, control.Load(file("<a\x01" "bc>t</a\x01" "bc>")) && control.GetName() == "a\x01" "bc");
            C_XmlNode before;
            WM(r.xmAttributes, before.Load(file("<ab \x01k=\"v\"/>")) && std::string(before.GetAttribute("\x01k")) == "v");
            C_XmlNode inText;
            WM(r.xmAttributes, inText.Load(file("<ab>x\x01y</ab>")) && inText.GetText() == "x\x01y");
            // An '=' and its quote close to the tag's end.
            C_XmlNode edge;
            WM(r.xmAttributes, edge.Load(file("<at d=\"a\">t</at>")) && !edge.Load(file("<at d=\"\">t</at>")));
        }
        {
            C_XmlNode node;
            // The tags' lengths: 3 .. 255; a closing tag at most 100; the closing tag's name.
            WM(r.xmBad, !node.Load(file("<a>t</a>")) && !node.Load(file("no tags")) && !node.Load(file("<ab")) &&
                            !node.Load(file("<abc>t</abd>")) && !node.Load(file("<abc>t")) && !node.Load(file("<abc>t<")) &&
                            !node.Load(file("<abc>t</")));
            C_XmlNode ok;
            WM(r.xmBad, ok.Load(file("<ab>t</ab>")) && ok.GetName() == "ab");
            const std::string n252(253, 'n'), n253(254, 'n'), n97(98, 'n'), n98(99, 'n');
            C_XmlNode longest, tooLong, longClose, tooLongClose;
            WM(r.xmBad, longest.Load(file("<" + n252 + "/>")) && longest.GetName().Size() == 253 &&
                            !tooLong.Load(file("<" + n253 + "/>")));
            WM(r.xmBad, longClose.Load(file("<" + n97 + ">t</" + n97 + ">")) &&
                            !tooLongClose.Load(file("<" + n98 + ">t</" + n98 + ">")));
            // A child that fails fails its parent; an empty name; an element with no name.
            C_XmlNode parent;
            WM(r.xmBad, !parent.Load(file("<pa><b c></pa>")) && parent.NumChildren() == 1);
            C_XmlNode noName;
            WM(r.xmBad, noName.Load(file("< />")) && noName.GetName() == "");
            C_XmlNode spaceName;
            WM(r.xmBad, !spaceName.Load(file("<   ab/>")));
            // A '>' before the '<': no element.
            C_XmlNode before;
            WM(r.xmBad, !before.Load(file("> <ab/>")));
        }
        {
            C_XmlNode root;
            WM(r.xmWalk, root.Load(file("<rr><aa><a1/><a2/></aa><bb/><cc><c1/></cc></rr>")));
            std::string order;
            for (C_XmlNode *n = root.Next(); n; n = root.Next())
                order += n->GetName().c_str() + std::string(" ");
            WM(r.xmWalk, order == "rr aa a1 a2 bb cc c1 " && root.Next() == nullptr);
            // A node's own walk: itself, then its own; the root's walk goes on after it.
            C_XmlNode tree;
            WM(r.xmWalk, tree.Load(file("<rr><aa><a1/><a2/></aa><bb/></rr>")));
            tree.Next();
            C_XmlNode *a = tree.Next();
            WM(r.xmWalk, a && a->GetName() == "aa" && a->Next()->GetName() == "a1" && a->Next()->GetName() == "a2" &&
                             a->Next() == nullptr && tree.Next()->GetName() == "bb" && tree.Next() == nullptr);
            // A load starts the walk again.
            WM(r.xmWalk, tree.Load(file("<dd/>")) && tree.Next() == &tree);
            // A walk of a leaf: itself, then none.
            C_XmlNode leaf;
            leaf.Load(file("<lf/>"));
            WM(r.xmWalk, leaf.Next() == &leaf && leaf.Next() == nullptr && leaf.Next() == nullptr);
        }
        {
            C_XmlNode node;
            WM(r.xmFile, !node.Load(nullptr) && !node.Load("none.hxml") && DtaOpenCount() == open);
            // At most 1 MB.
            std::string text = "<big>" + std::string(0x100000 - 11, ' ') + "</big>";
            C_XmlNode fits, tooBig;
            WM(r.xmFile, text.size() == 0x100000 && fits.Load(file(text)) && fits.GetName() == "big" &&
                             !tooBig.Load(file(text + " ")) && DtaOpenCount() == open);
            // A short read: none.
            C_XmlNode empty;
            WM(r.xmFile, !empty.Load(file("")));
        }
    }

    // --- part 9a-ii: FontConfig.hxml ---
    {
        C_FontManager m;
        const auto config = [](const std::string &text) {
            g_files["cfg.hxml"] = std::vector<uint8_t>(text.begin(), text.end());
        };
        const auto font = [](const char *id, const char *name, const char *size, const char *bold, const char *height) {
            std::string f = "<FONT";
            if (id)
                f += std::string(" id=\"") + id + "\"";
            if (name)
                f += std::string(" name=\"") + name + "\"";
            if (size)
                f += std::string(" size=\"") + size + "\"";
            if (bold)
                f += std::string(" bold=\"") + bold + "\"";
            if (height)
                f += std::string(" texture_height=\"") + height + "\"";
            return f + "/>";
        };
        const std::string big = font("FONT_BIG", "Big Face", "24", "true", "512");
        g_foGdi = false;
        // The root's name; the language's (any case; the first matching; a language without a name
        // skipped); not found: none.
        config("<FONTCONF><LANGUAGE name=\"German\"><SET resolution=\"640x480\">" + big + "</SET></LANGUAGE></FONTCONF>");
        g_gfLog.clear();
        WM(r.cfLanguage, !m.LoadConfig("cfg.hxml", "German") && FoLines("gdi").empty());
        WM(r.cfLanguage, !m.LoadConfig("none.hxml", "German"));
        config("<FONTCONFIG><LANGUAGE><SET resolution=\"640x480\">" + font("FONT_BIG", "Nameless", "1", "true", "1") +
               "</SET></LANGUAGE><OTHER name=\"german\"><SET resolution=\"640x480\">" +
               font("FONT_BIG", "Other", "1", "true", "1") + "</SET></OTHER><LANGUAGE name=\"French\"><SET resolution=\"640x480\">" +
               font("FONT_BIG", "French", "1", "true", "1") + "</SET></LANGUAGE><LANGUAGE name=\"gErMaN\"><SET resolution=\"640x480\">" +
               big + "</SET></LANGUAGE><LANGUAGE name=\"German\"><SET resolution=\"640x480\">" +
               font("FONT_BIG", "Second", "1", "true", "1") + "</SET></LANGUAGE></FONTCONFIG>");
        g_gfLog.clear();
        WM(r.cfLanguage, m.LoadConfig("cfg.hxml", "German") && FoLines("gdi") == std::vector<std::string>{"gdi Big Face 512 512 24 2 1"});
        g_gfLog.clear();
        WM(r.cfLanguage, !m.LoadConfig("cfg.hxml", "Spanish") && FoLines("gdi").empty());
        WM(r.cfLanguage, !m.LoadConfig("cfg.hxml", "Germa") && !m.LoadConfig("cfg.hxml", "Germans"));

        // The set of the screen's resolution (inside the language), else none.
        const std::string sets = "<FONTCONFIG><LANGUAGE name=\"X\"><SET resolution=\"640x480\">" + font("FONT_BIG", "S640", "1", "true", "1") +
                                 "</SET><SET>" + font("FONT_BIG", "NoRes", "1", "true", "1") + "</SET><RES resolution=\"800x600\">" +
                                 font("FONT_BIG", "NotSet", "1", "true", "1") + "</RES><SET resolution=\"800x600\">" +
                                 font("FONT_BIG", "S800", "1", "true", "1") + "</SET><SET resolution=\"1024x768\">" +
                                 font("FONT_BIG", "S1024", "1", "true", "1") + "</SET></LANGUAGE><LANGUAGE name=\"Y\">" +
                                 "<SET resolution=\"640x480\">" + font("FONT_BIG", "Y640", "1", "true", "1") + "</SET></LANGUAGE></FONTCONFIG>";
        config(sets);
        const int widths[] = {640, 799, 800, 1023, 1024, 2000};
        const char *const faces[] = {"S640", "S640", "S800", "S800", "S1024", "S1024"};
        for (int i = 0; i < 6; ++i) {
            g_foWidth = widths[i];
            g_gfLog.clear();
            WM(r.cfSet, m.LoadConfig("cfg.hxml", "X") && FoLines("gdi") == std::vector<std::string>{CmText("gdi %s 512 1 1 2 1", faces[i])});
        }
        g_foWidth = 640;
        config("<FONTCONFIG><LANGUAGE name=\"X\"><SET resolution=\"800x600\">" + big + "</SET></LANGUAGE><LANGUAGE name=\"Y\">"
               "<SET resolution=\"640x480\">" + big + "</SET></LANGUAGE></FONTCONFIG>");
        g_gfLog.clear();
        WM(r.cfSet, !m.LoadConfig("cfg.hxml", "X") && FoLines("gdi").empty());

        // The fonts: big and small, others skipped; loaded whatever the fonts do (no fonts too).
        config("<FONTCONFIG><LANGUAGE name=\"X\"><SET resolution=\"640x480\">" + big + font("FONT_SMALL", "Small", "12", "false", "256") +
               font("FONT_HUGE", "Huge", "1", "true", "1") + font("FONT_BIGGER", "Bigger", "1", "true", "1") +
               font("FONT_SMAL", "Smal", "1", "true", "1") + "<NOTFONT id=\"FONT_BIG\" name=\"N\" size=\"1\" bold=\"true\" texture_height=\"1\"/>" +
               "<GROUP>" + font("FONT_SMALL", "Nested", "9", "True", "64") + "</GROUP></SET></LANGUAGE></FONTCONFIG>");
        for (const bool gdi : {false, true}) {
            g_foGdi = gdi;
            g_gfLog.clear();
            WM(r.cfFonts, m.LoadConfig("cfg.hxml", "X") &&
                              FoLines("gdi") == std::vector<std::string>({"gdi Big Face 512 512 24 2 1", "gdi Small 512 256 12 3 0",
                                                                          "gdi Nested 512 64 9 3 0"}));
        }
        g_foGdi = false;
        config("<FONTCONFIG><LANGUAGE name=\"X\"><SET resolution=\"640x480\"></SET></LANGUAGE></FONTCONFIG>");
        WM(r.cfFonts, m.LoadConfig("cfg.hxml", "X"));

        // A font missing a value: none (after the fonts before it).
        const char *const full[5] = {"FONT_BIG", "F", "7", "yes", "128"};
        for (int missing = 0; missing < 5; ++missing) {
            const char *v[5];
            for (int k = 0; k < 5; ++k)
                v[k] = k == missing ? nullptr : full[k];
            config("<FONTCONFIG><LANGUAGE name=\"X\"><SET resolution=\"640x480\">" + big + font(v[0], v[1], v[2], v[3], v[4]) +
                   "</SET></LANGUAGE></FONTCONFIG>");
            g_gfLog.clear();
            WM(r.cfValues, !m.LoadConfig("cfg.hxml", "X") && FoLines("gdi") == std::vector<std::string>{"gdi Big Face 512 512 24 2 1"});
        }
        // The values: atoi, "true" only.
        config("<FONTCONFIG><LANGUAGE name=\"X\"><SET resolution=\"640x480\">" + font("FONT_BIG", "F", "7", "yes", "128") +
               font("FONT_BIG", "G", "x12", "truex", "-3") + font("FONT_BIG", "H", " 5", "true ", "64pt") + "</SET></LANGUAGE></FONTCONFIG>");
        g_gfLog.clear();
        WM(r.cfValues, m.LoadConfig("cfg.hxml", "X") && FoLines("gdi") == std::vector<std::string>({"gdi F 512 128 7 2 0", "gdi G 512 -3 0 2 0",
                                                                                                     "gdi H 512 64 5 2 0"}));
        m.Free();
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
    std::printf("xml: xmTree %d xmText %d xmComments %d xmAttributes %d xmBad %d xmWalk %d xmFile %d cfLanguage %d "
                "cfSet %d cfFonts %d cfValues %d\n",
                r.xmTree, r.xmText, r.xmComments, r.xmAttributes, r.xmBad, r.xmWalk, r.xmFile, r.cfLanguage, r.cfSet,
                r.cfFonts, r.cfValues);
}
