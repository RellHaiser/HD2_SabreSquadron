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
// The GDI fonts (part 9a-iii): made while g_foGdi (else their image is not); their fonts logged
// ("gdi FACE SIZE WEIGHT").
bool g_foGdi;
// --- part 9a-iii: GDI (the fonts' calls through their import pointers) and IGraph's images ---------

// The DCs, fonts and DIBs made; what was deleted (each handle as often as it was).
struct S_FoGdiObject {
    int kind = 0;                                       // 1 DC, 2 font, 3 DIB, 4 stock
    int height = 0;                                     // a font's
    std::vector<uint8_t> bits;                          // a DIB's (64 x 64, 24-bit)
    S_FoGdiObject *bitmap = nullptr, *font = nullptr;   // a DC's
};
std::vector<S_FoGdiObject *> g_foGdiObjects;
std::vector<const void *> g_foDeleted;
// Failing steps: the DC, the font (its face "nofont"), the DIB; the image (g_foGdi false).
bool g_foNoDc, g_foNoDib, g_foNoLock;
S_FoGdiObject g_foStock[8];
S_FoGdiObject *FoGdi(int kind)
{
    g_foGdiObjects.push_back(new S_FoGdiObject());
    g_foGdiObjects.back()->kind = kind;
    return g_foGdiObjects.back();
}
HDC WINAPI F_FoCreateCompatibleDC(HDC dc)
{
    GfLog("cdc %d", dc != nullptr);
    return g_foNoDc ? nullptr : reinterpret_cast<HDC>(FoGdi(1));
}
HFONT WINAPI F_FoCreateFontA(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                             DWORD underline, DWORD strike, DWORD charset, DWORD precision, DWORD clip, DWORD quality,
                             DWORD pitch, LPCSTR face)
{
    GfLog("gdi %s %d %d", face, height, weight);
    GfLog("cfont %d %d %d %d %u %u %u %u %u %u %u %u", width, escapement, orientation, weight, italic, underline,
          strike, charset, precision, clip, quality, pitch);
    if (std::strstr(face, "nofont"))
        return nullptr;
    S_FoGdiObject *f = FoGdi(2);
    f->height = height;
    return reinterpret_cast<HFONT>(f);
}
HBITMAP WINAPI F_FoCreateDIBSection(HDC dc, const BITMAPINFO *info, UINT usage, void **bits, HANDLE section, DWORD offset)
{
    GfLog("dib %d %d %d %d %d %d %u %d %u", dc != nullptr, info->bmiHeader.biSize, info->bmiHeader.biWidth,
          info->bmiHeader.biHeight, info->bmiHeader.biPlanes, info->bmiHeader.biBitCount, info->bmiHeader.biCompression,
          section != nullptr, usage + offset);
    if (g_foNoDib) {
        *bits = nullptr;
        return nullptr;
    }
    S_FoGdiObject *b = FoGdi(3);
    b->bits.assign(64 * 64 * 3, 0x5A);
    *bits = b->bits.data();
    return reinterpret_cast<HBITMAP>(b);
}
HGDIOBJ WINAPI F_FoSelectObject(HDC dc, HGDIOBJ object)
{
    auto *d = reinterpret_cast<S_FoGdiObject *>(dc);
    auto *o = static_cast<S_FoGdiObject *>(object);
    if (o->kind == 3)
        d->bitmap = o;
    else if (o->kind == 2)
        d->font = o;
    GfLog("select %d %d", o->kind, o->kind == 4 ? static_cast<int>(o - g_foStock) : -1);
    return nullptr;
}
COLORREF WINAPI F_FoSetTextColor(HDC, COLORREF c)
{
    GfLog("textcolor %06x", static_cast<unsigned>(c));
    return 0;
}
COLORREF WINAPI F_FoSetBkColor(HDC, COLORREF c)
{
    GfLog("bkcolor %06x", static_cast<unsigned>(c));
    return 0;
}
int WINAPI F_FoSetMapMode(HDC, int mode)
{
    GfLog("mapmode %d", mode);
    return 0;
}
// A text's extent: the font's height high; "Wg" as wide, a character 4 + its code % 13.
int g_foExtentHeight = -1;                              // (>= 0: this height instead)
BOOL WINAPI F_FoGetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size)
{
    const int height = reinterpret_cast<S_FoGdiObject *>(dc)->font->height;
    size->cy = g_foExtentHeight >= 0 ? g_foExtentHeight : height;
    size->cx = count == 2 ? height : 4 + text[0] % 13;
    GfLog("extent %d %d", count, count == 2 && text[0] == L'W' && text[1] == L'g');
    return TRUE;
}
HGDIOBJ WINAPI F_FoGetStockObject(int index)
{
    g_foStock[index].kind = 4;
    return &g_foStock[index];
}
BOOL WINAPI F_FoRectangle(HDC dc, int left, int top, int right, int bottom)
{
    GfLog("rect %d %d %d %d", left, top, right, bottom);
    std::vector<uint8_t> &bits = reinterpret_cast<S_FoGdiObject *>(dc)->bitmap->bits;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            for (int c = 0; c < 3; ++c)
                bits[(63 - y) * 192 + x * 3 + c] = 0;
    return TRUE;
}
// A glyph: its extent's pixels (from the top left), blue (code + x + 7 y) & 0xFF.
BOOL WINAPI F_FoExtTextOutW(HDC dc, int x0, int y0, UINT options, const RECT *rect, LPCWSTR text, UINT count, const INT *dx)
{
    GfLog("textout %d %d %u %d %04x %u %d", x0, y0, options, rect != nullptr, text[0], count, dx != nullptr);
    auto *d = reinterpret_cast<S_FoGdiObject *>(dc);
    SIZE size;
    F_FoGetTextExtentPoint32W(dc, text, 1, &size);
    g_gfLog.pop_back();
    for (int y = 0; y < size.cy && y < 64; ++y)
        for (int x = 0; x < size.cx && x < 64; ++x) {
            uint8_t *p = &d->bitmap->bits[(63 - y) * 192 + x * 3];
            p[0] = static_cast<uint8_t>(text[0] + x + 7 * y);
            p[1] = 0x11;
            p[2] = 0x22;
        }
    return TRUE;
}
BOOL WINAPI F_FoDeleteDC(HDC dc)
{
    g_foDeleted.push_back(dc);
    return TRUE;
}
BOOL WINAPI F_FoDeleteObject(HGDIOBJ object)
{
    g_foDeleted.push_back(object);
    return TRUE;
}
extern "C" {
HDC(WINAPI *g_foImpCreateCompatibleDC)(HDC) __asm__("__imp__CreateCompatibleDC@4") = &F_FoCreateCompatibleDC;
HFONT(WINAPI *g_foImpCreateFontA)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                  LPCSTR) __asm__("__imp__CreateFontA@56") = &F_FoCreateFontA;
HBITMAP(WINAPI *g_foImpCreateDIBSection)(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD) __asm__(
    "__imp__CreateDIBSection@24") = &F_FoCreateDIBSection;
HGDIOBJ(WINAPI *g_foImpSelectObject)(HDC, HGDIOBJ) __asm__("__imp__SelectObject@8") = &F_FoSelectObject;
COLORREF(WINAPI *g_foImpSetTextColor)(HDC, COLORREF) __asm__("__imp__SetTextColor@8") = &F_FoSetTextColor;
COLORREF(WINAPI *g_foImpSetBkColor)(HDC, COLORREF) __asm__("__imp__SetBkColor@8") = &F_FoSetBkColor;
int(WINAPI *g_foImpSetMapMode)(HDC, int) __asm__("__imp__SetMapMode@8") = &F_FoSetMapMode;
BOOL(WINAPI *g_foImpGetTextExtentPoint32W)(HDC, LPCWSTR, int, LPSIZE) __asm__("__imp__GetTextExtentPoint32W@16") =
    &F_FoGetTextExtentPoint32W;
HGDIOBJ(WINAPI *g_foImpGetStockObject)(int) __asm__("__imp__GetStockObject@4") = &F_FoGetStockObject;
BOOL(WINAPI *g_foImpRectangle)(HDC, int, int, int, int) __asm__("__imp__Rectangle@20") = &F_FoRectangle;
BOOL(WINAPI *g_foImpExtTextOutW)(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *) __asm__(
    "__imp__ExtTextOutW@32") = &F_FoExtTextOutW;
BOOL(WINAPI *g_foImpDeleteDC)(HDC) __asm__("__imp__DeleteDC@4") = &F_FoDeleteDC;
BOOL(WINAPI *g_foImpDeleteObject)(HGDIOBJ) __asm__("__imp__DeleteObject@4") = &F_FoDeleteObject;
}

// IGraph's images (slot 0x40, CreateImage: none while g_foGdi is false): a 512-wide buffer of the
// rows asked for; the calls logged.
struct S_FoImage {
    void **vtable;
    int refs = 1;
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
};
void *g_foImageVt[SLOTS];
std::vector<S_FoImage *> g_foImages;
uint32_t __stdcall F_FoImageRelease(S_FoImage *self)
{
    GfLog("irelease");
    return static_cast<uint32_t>(--self->refs);
}
HRESULT __stdcall F_FoImageOpen(S_FoImage *self, const char *name, const char *alpha, uint32_t flags, int width,
                                int height, int unknown)
{
    GfLog("iopen %d %d %x %d %d %d", name != nullptr, alpha != nullptr, flags, width, height, unknown);
    self->width = width;
    self->height = height;
    self->pixels.assign(static_cast<size_t>(512) * 4 * (height > 0 ? height : 0), 0x77);
    return 0;
}
void *__stdcall F_FoImageLock(S_FoImage *self, int unknown1, int *pitch, int unknown2)
{
    GfLog("ilock %d %d %d", unknown1, pitch != nullptr, unknown2);
    return g_foNoLock ? nullptr : self->pixels.data();
}
void __stdcall F_FoImageUnlock(S_FoImage *) { GfLog("iunlock"); }
void __stdcall F_FoImageFill(S_FoImage *self, const void *rect, uint32_t color, int unknown)
{
    GfLog("ifill %d %06x %d", rect != nullptr, color, unknown);
    for (size_t i = 0; i < self->pixels.size(); i += 4) {
        self->pixels[i] = static_cast<uint8_t>(color);
        self->pixels[i + 1] = static_cast<uint8_t>(color >> 8);
        self->pixels[i + 2] = static_cast<uint8_t>(color >> 16);
        self->pixels[i + 3] = static_cast<uint8_t>(color >> 24);
    }
}
IImage *__stdcall F_FoCreateImage(void *)
{
    GfLog("cimage");
    if (!g_foGdi)
        return nullptr;
    auto *image = new S_FoImage();
    image->vtable = g_foImageVt;
    g_foImages.push_back(image);
    return reinterpret_cast<IImage *>(image);
}
void FoInitImages()
{
    for (int i = 0; i < SLOTS; ++i)
        g_foImageVt[i] = g_imageVtable[i];
    g_foImageVt[0x00 / 4] = reinterpret_cast<void *>(&F_FoImageRelease);
    g_foImageVt[0x08 / 4] = reinterpret_cast<void *>(&F_FoImageOpen);
    g_foImageVt[0x14 / 4] = reinterpret_cast<void *>(&F_FoImageLock);
    g_foImageVt[0x18 / 4] = reinterpret_cast<void *>(&F_FoImageUnlock);
    g_foImageVt[0x20 / 4] = reinterpret_cast<void *>(&F_FoImageFill);
}


namespace {

struct S_FontResults {
    bool foSets = true, foLanguages = true, foJapanese = true, foConfig = true, foVersion = true, foShared = true;
    bool foMissing = true, foTable = true, foQuads = true, foGrow = true, foFlush = true, foMap = true;
    bool xmTree = true, xmText = true, xmComments = true, xmAttributes = true, xmBad = true, xmWalk = true;
    bool xmFile = true, cfLanguage = true, cfSet = true, cfFonts = true, cfValues = true;
    bool gdInit = true, gdCells = true, gdFail = true, gdGlyph = true, gdTexture = true, gdReuse = true;
    bool gdEvict = true, gdOrder = true, gdSelect = true, gdClose = true;
    bool stFirst = true, stDefaults = true, stCreate = true, stRelease = true;
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
    const bool gdi = std::find(g_foImages.begin(), g_foImages.end(), texture) != g_foImages.end();
    GfLog("settex %s %d", gdi ? "gdi" : texture ? static_cast<S_Image *>(texture)->name.c_str() : "-", stage);
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
                              g_igraphVtable[0x88 / 4], g_igraphVtable[0x40 / 4]};
    FoInitImages();
    g_igraphVtable[0x40 / 4] = reinterpret_cast<void *>(&F_FoCreateImage);
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
                             FoLines("gdi") == std::vector<std::string>{"gdi Cfg Face 20 700"} &&
                             FoLines("texture").size() == 3 &&
                             FoLines("texture")[1] == "texture Text\\Fonts\\Ce\\Set1\\\\BIG.tga 0 402");
        g_files.erase("Text\\Japan\\FontConfig.hxml");
        const int widths[] = {640, 800, 1024};
        const char *const big[] = {"gdi MS UI Gothic 31 400", "gdi Lucida Sans Unicode 28 400",
                                   "gdi Lucida Sans Unicode 34 400"};
        const char *const small[] = {"gdi MS UI Gothic 14 400", "gdi MS UI Gothic 15 400",
                                     "gdi MS UI Gothic 20 400"};
        for (int i = 0; i < 3; ++i) {
            g_foWidth = widths[i];
            FoAddFiles("Japan", i + 1);
            g_foGdi = true;
            m = FoLoad();
            WM(r.foJapanese, FoIds(m) == std::vector<int>({4, 5, 6, 2, 3}) && dynamic_cast<C_GdiFont *>(m->FindFont(2)) &&
                                 dynamic_cast<C_GdiFont *>(m->FindFont(3)) &&
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
                           FoLines("gdi") == std::vector<std::string>{"gdi Cfg Face 20 700"});
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
        WM(r.cfLanguage, m.LoadConfig("cfg.hxml", "German") && FoLines("gdi") == std::vector<std::string>{"gdi Big Face 24 700"});
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
            WM(r.cfSet, m.LoadConfig("cfg.hxml", "X") && FoLines("gdi") == std::vector<std::string>{CmText("gdi %s 8 700", faces[i])});
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
                              FoLines("gdi") == std::vector<std::string>({"gdi Big Face 24 700", "gdi Small 12 400",
                                                                          "gdi Nested 9 400"}));
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
            WM(r.cfValues, !m.LoadConfig("cfg.hxml", "X") && FoLines("gdi") == std::vector<std::string>{"gdi Big Face 24 700"});
        }
        // The values: atoi, "true" only.
        config("<FONTCONFIG><LANGUAGE name=\"X\"><SET resolution=\"640x480\">" + font("FONT_BIG", "F", "7", "yes", "128") +
               font("FONT_BIG", "G", "x12", "truex", "-3") + font("FONT_BIG", "H", " 5", "true ", "64pt") + "</SET></LANGUAGE></FONTCONFIG>");
        g_gfLog.clear();
        WM(r.cfValues, m.LoadConfig("cfg.hxml", "X") && FoLines("gdi") == std::vector<std::string>({"gdi F 8 400", "gdi G 8 400",
                                                                                                     "gdi H 8 400"}));
        m.Free();
    }


    // --- part 9a-iii: the GDI fonts ---
    {
        g_foGdi = true;
        g_foNoDc = g_foNoDib = g_foNoLock = false;
        g_foExtentHeight = -1;
        const auto count = [](const void *handle) {
            return std::count(g_foDeleted.begin(), g_foDeleted.end(), handle);
        };
        // The DC, the font, the DIB, the text's settings, the cells' size, the texture, the cells.
        {
            C_FontManager m;
            g_gfLog.clear();
            WM(r.gdInit, m.AddGdiFont("Face", 512, 128, 20, 2, false) && dynamic_cast<C_GdiFont *>(m.FindFont(2)));
            auto *f = static_cast<C_GdiFont *>(m.FindFont(2));
            WM(r.gdInit, GfVecIs(g_gfLog, std::vector<std::string>{"cdc 0", "gdi Face 20 400", "cfont 0 0 0 400 0 0 0 1 4 0 4 2",
                                                                    "dib 1 40 64 64 1 24 0 0 0", "select 3 -1", "select 2 -1",
                                                                    "textcolor ffffff", "bkcolor 000000", "mapmode 1", "extent 2 1",
                                                                    "cimage", "iopen 0 0 8402 512 128 0", "ifill 0 ffffff 0"}));
            WM(r.gdInit, f && f->m_cellWidth == 20 && f->m_cellHeight == 20 && f->m_size == 20 && f->m_face == "Face" &&
                             !f->m_bold && f->m_frame == 1 && f->m_textureWidth == 512 && f->m_textureHeight == 128 &&
                             f->m_bits == g_foGdiObjects.back()->bits.data() && f->m_newest == nullptr && f->m_cells.empty());
            // The cells: 24 by 6, a pixel apart from (1, 1); the last one the spare.
            WM(r.gdCells, f && f->m_free.size() == 143 && f->m_spare && f->m_spare->x == 484 && f->m_spare->y == 106 &&
                              f->m_spare->right == 503 && f->m_spare->bottom == 125 && f->m_free[0]->x == 1 && f->m_free[0]->y == 1 &&
                              f->m_free[1]->x == 22 && f->m_free[24]->x == 1 && f->m_free[24]->y == 22 && f->m_free[0]->ch == 0 &&
                              f->m_free[142]->x == 463 && f->m_free[142]->y == 106);
            // Bold; the size within 8 .. 60; a cell at least 10.
            g_gfLog.clear();
            WM(r.gdInit, m.AddGdiFont("Bold", 512, 128, 7, 3, true) && FoLines("gdi") == std::vector<std::string>{"gdi Bold 8 700"} &&
                             static_cast<C_GdiFont *>(m.FindFont(3))->m_cellHeight == 10 &&
                             static_cast<C_GdiFont *>(m.FindFont(3))->m_bold);
            g_gfLog.clear();
            WM(r.gdInit, m.AddGdiFont("Big", 512, 128, 61, 4, false) && FoLines("gdi") == std::vector<std::string>{"gdi Big 60 400"} &&
                             static_cast<C_GdiFont *>(m.FindFont(4))->m_cellWidth == 60);
            g_foExtentHeight = 25;
            WM(r.gdInit, m.AddGdiFont("Tall", 512, 128, 8, 5, false) && static_cast<C_GdiFont *>(m.FindFont(5))->m_cellWidth == 25 &&
                             static_cast<C_GdiFont *>(m.FindFont(5))->m_free.size() == 19 * 4 - 1);
            g_foExtentHeight = 10;
            WM(r.gdInit, m.AddGdiFont("Ten", 512, 128, 8, 6, false) && static_cast<C_GdiFont *>(m.FindFont(6))->m_cellWidth == 10);
            g_foExtentHeight = -1;
            // The grid's edges: a cell 20 needs 22 rows (21 none), 22 columns for one (43 for two).
            const int sizes[][3] = {{512, 21, 0}, {512, 22, 24}, {42, 22, 1}, {43, 22, 2}, {21, 22, 0}};
            for (const auto &t : sizes) {
                C_FontManager grid;
                WM(r.gdCells, grid.AddGdiFont("G", t[0], t[1], 20, 2, false));
                auto *g = static_cast<C_GdiFont *>(grid.FindFont(2));
                WM(r.gdCells, g && static_cast<int>(g->m_free.size()) + (g->m_spare ? 1 : 0) == t[2] && (t[2] == 0) == !g->m_spare);
                grid.Free();
            }
            m.Free();
        }
        // A step failing: no font (what was made deleted, twice: the close, then the destructor's).
        {
            C_FontManager m;
            g_foDeleted.clear();
            g_foNoDc = true;
            WM(r.gdFail, !m.AddGdiFont("Face", 512, 128, 20, 2, false) && m.m_fonts.empty() && g_foDeleted.size() == 6 &&
                             count(nullptr) == 6);
            g_foNoDc = false;
            g_foDeleted.clear();
            WM(r.gdFail, !m.AddGdiFont("nofont", 512, 128, 20, 2, false) && m.m_fonts.empty() && g_foDeleted.size() == 6 &&
                             count(g_foGdiObjects.back()) == 2);
            g_foNoDib = true;
            g_foDeleted.clear();
            WM(r.gdFail, !m.AddGdiFont("Face", 512, 128, 20, 2, false) && m.m_fonts.empty() &&
                             count(g_foGdiObjects.back()) == 2 && count(g_foGdiObjects[g_foGdiObjects.size() - 2]) == 2 &&
                             count(nullptr) == 2);
            g_foNoDib = false;
            g_foGdi = false;
            g_foDeleted.clear();
            g_gfLog.clear();
            WM(r.gdFail, !m.AddGdiFont("Face", 512, 128, 20, 2, false) && m.m_fonts.empty() && FoLines("cimage").size() == 1 &&
                             count(g_foGdiObjects.back()) == 2 && count(g_foGdiObjects[g_foGdiObjects.size() - 3]) == 2 &&
                             FoLines("irelease").empty());
            g_foGdi = true;
            // No room for a cell: no spare; its glyphs the default.
            WM(r.gdFail, m.AddGdiFont("Face", 512, 20, 20, 7, false));
            auto *f = static_cast<C_GdiFont *>(m.FindFont(7));
            WM(r.gdFail, f && f->m_spare == nullptr && f->m_free.empty() && f->GetGlyph('A') == &f->m_default);
            m.Free();
        }
        // A glyph: drawn in the last free cell, copied (white, its blue the alpha), its rectangle.
        {
            C_FontManager m;
            m.AddGdiFont("Face", 512, 128, 20, 2, false);
            auto *f = static_cast<C_GdiFont *>(m.FindFont(2));
            S_FoImage *image = g_foImages.back();
            S_GlyphCell *const cell = f->m_free.back();
            g_gfLog.clear();
            const S_Glyph *g = f->GetGlyph('A');
            WM(r.gdGlyph, g == &cell->glyph && cell->ch == 'A' && f->m_free.size() == 142 && f->m_cells.size() == 1 &&
                              GfVecIs(g_gfLog, std::vector<std::string>{"select 4 4", "select 4 7", "rect 0 0 63 63", "extent 1 0",
                                                                         "textout 0 0 2 0 0041 1 0", "ilock 0 1 0", "iunlock"}));
            WM(r.gdGlyph, g->u0 == 463.0f / 511.0f && g->u1 == 467.0f / 511.0f && g->v0 == 106.0f / 127.0f &&
                              g->v1 == 126.0f / 127.0f && g->aspect == 4.0f / 20.0f);
            bool pixels = true;
            for (int row = 0; row < 20; ++row)
                for (int col = 0; col < 21; ++col) {
                    const uint8_t *p = &image->pixels[((106 + row) * 512 + 463 + col) * 4];
                    const int alpha = col == 20 ? 0 : col < 4 ? (0x41 + col + 7 * row) & 0xFF : 0;
                    pixels = pixels && p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF && p[3] == alpha;
                }
            const uint8_t *below = &image->pixels[((106 + 20) * 512 + 463) * 4];
            WM(r.gdTexture, pixels && below[3] == 0 && below[0] == 0xFF);
            // The DIB cleared to its last row and column: what is left there stays.
            S_FoGdiObject *dib = g_foGdiObjects[g_foGdiObjects.size() - 1];
            WM(r.gdTexture, dib->kind == 3 && dib->bits[0] == 0x5A && dib->bits[(63 - 62) * 192 + 62 * 3] == 0 &&
                                dib->bits[63 * 192 + 63 * 3] == 0x5A);
            // A cell's rows from the DIB's top: a glyph 25 high in a font of 30.
            C_FontManager m2;
            m2.AddGdiFont("Face", 512, 128, 30, 2, false);
            auto *f2 = static_cast<C_GdiFont *>(m2.FindFont(2));
            g_foExtentHeight = 25;
            S_GlyphCell *const cell2 = f2->m_free.back();
            f2->GetGlyph('B');
            g_foExtentHeight = -1;
            const uint8_t *last = &g_foImages.back()->pixels[((cell2->y + 24) * 512 + cell2->x + 1) * 4];
            const uint8_t *past = &g_foImages.back()->pixels[((cell2->y + 25) * 512 + cell2->x + 1) * 4];
            WM(r.gdTexture, last[3] == ((0x42 + 1 + 7 * 24) & 0xFF) && past[3] == 0 && past[0] == 0xFF &&
                                cell2->glyph.v1 == (cell2->y + 25) / 127.0f && cell2->glyph.aspect == 5.0f / 25.0f);
            // A glyph wider than its cell: the cell's columns only; the colour written over the cell.
            C_FontManager m3;
            m3.AddGdiFont("Face", 512, 128, 8, 2, false);
            auto *f3 = static_cast<C_GdiFont *>(m3.FindFont(2));
            S_GlyphCell *const cell3 = f3->m_free.back();
            std::vector<uint8_t> &px3 = g_foImages.back()->pixels;
            for (int row = 0; row < 10; ++row)
                for (int col = 0; col < 11; ++col)
                    std::memset(&px3[((cell3->y + row) * 512 + cell3->x + col) * 4], 0x77, 4);
            f3->GetGlyph('I');                          // (12 wide in a cell of 10)
            bool wide = true;
            for (int row = 0; row < 10; ++row)
                for (int col = 0; col < 11; ++col) {
                    const uint8_t *p = &px3[((cell3->y + row) * 512 + cell3->x + col) * 4];
                    wide = wide && (col == 10 ? p[0] == 0x77 && p[3] == 0x77
                                              : p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF && p[3] == (row < 8 ? (0x49 + col + 7 * row) & 0xFF : 0));
                }
            WM(r.gdTexture, wide);
            m3.Free();
            // No pixels: nothing copied (unlocked all the same).
            g_foNoLock = true;
            g_gfLog.clear();
            f->GetGlyph('C');
            WM(r.gdTexture, FoLines("ilock").size() == 1 && FoLines("iunlock").size() == 1);
            g_foNoLock = false;
            // An extent 0 high: no width.
            g_foExtentHeight = 0;
            WM(r.gdTexture, f->GetGlyph('D')->aspect == 0.0f);
            g_foExtentHeight = -1;
            // Again: the same cell, not drawn again; a tab: a space; the code's low 16 bits.
            g_gfLog.clear();
            // Code 0: a glyph like the others (a new cell does not forget it).
            const S_Glyph *zero = f->GetGlyph(0);
            WM(r.gdReuse, f->GetGlyph('E') != zero && f->GetGlyph(0) == zero && f->m_cells.count(0) == 1);
            f->m_cells.erase(0);
            f->m_cells.erase('E');
            g_gfLog.clear();
            WM(r.gdReuse, f->GetGlyph('A') == g && g_gfLog.empty() && f->GetGlyph('\t') == f->GetGlyph(' ') &&
                              f->GetGlyph(0x10041) == g && f->m_cells.size() == 4 && f->m_cells.count(' ') == 1 &&
                              f->m_cells.count('\t') == 0);
            m.Free();
            m2.Free();
        }
        // No free cell: the least lately used, unless used in this frame (then the spare's glyph).
        {
            C_FontManager m;
            m.AddGdiFont("Face", 512, 22, 20, 2, false);
            auto *f = static_cast<C_GdiFont *>(m.FindFont(2));
            WM(r.gdEvict, f->m_free.size() == 23);
            std::vector<const S_Glyph *> glyphs;
            for (int i = 0; i < 23; ++i)
                glyphs.push_back(f->GetGlyph(0x100 + i));
            g_gfLog.clear();
            WM(r.gdEvict, f->GetGlyph(0x200) == &f->m_spare->glyph && FoLines("textout").empty() && f->m_cells.count(0x200) == 0);
            f->Select();
            g_gfLog.clear();
            const S_Glyph *reused = f->GetGlyph(0x200);
            WM(r.gdEvict, reused == glyphs[0] && FoLines("textout").size() == 1 && f->m_cells.count(0x100) == 0 &&
                              f->m_cells.count(0x200) == 1 && f->m_newest->ch == 0x200);
            // The next: the one after; the first again: drawn again, in the next one.
            WM(r.gdEvict, f->GetGlyph(0x201) == glyphs[1] && f->GetGlyph(0x100) == glyphs[2] && f->m_cells.count(0x101) == 0);
            WM(r.gdEvict, f->m_oldest->ch == 0x103 && f->m_cells.size() == 23);
            m.Free();
        }
        // The use order: newest first; once a frame; a cell used again moved first.
        {
            C_FontManager m;
            m.AddGdiFont("Face", 512, 22, 20, 2, false);
            auto *f = static_cast<C_GdiFont *>(m.FindFont(2));
            const S_Glyph *a = f->GetGlyph('a');
            const S_Glyph *b = f->GetGlyph('b');
            const S_Glyph *c = f->GetGlyph('c');
            S_GlyphCell *const ca = f->m_cells['a'], *const cb = f->m_cells['b'], *const cc = f->m_cells['c'];
            WM(r.gdOrder, f->m_newest == cc && f->m_oldest == ca && cc->older == cb && cb->older == ca && ca->older == nullptr &&
                              ca->newer == cb && cb->newer == cc && cc->newer == nullptr);
            // In the same frame: not moved.
            f->GetGlyph('a');
            WM(r.gdOrder, f->m_newest == cc && f->m_oldest == ca);
            f->Select();
            f->GetGlyph('a');
            WM(r.gdOrder, f->m_newest == ca && f->m_oldest == cb && ca->older == cc && cc->newer == ca && cb->newer == cc &&
                              ca->newer == nullptr && ca->frame == 2);
            // The middle one moved first.
            f->GetGlyph('c');
            WM(r.gdOrder, f->m_newest == cc && cc->older == ca && ca->older == cb && cb->newer == ca && ca->newer == cc &&
                              f->m_oldest == cb);
            // The oldest moved first.
            f->GetGlyph('b');
            WM(r.gdOrder, f->m_newest == cb && f->m_oldest == ca && cb->older == cc && ca->newer == cc && ca->older == nullptr);
            for (int i = 0; i < 20; ++i)
                f->GetGlyph(0x300 + i);
            f->Select();
            // The least lately used (here 'a') reused first.
            WM(r.gdOrder, f->GetGlyph(0x400) == a && f->GetGlyph(0x401) == c && f->GetGlyph(0x402) == b);
            // (The cursor: moved off a cell used again.)
            f->m_cursor = f->m_newest;
            S_GlyphCell *const older = f->m_newest->older;
            f->Select();
            f->GetGlyph(0x402);
            WM(r.gdOrder, f->m_cursor == older);
            m.Free();
        }
        // Select: the texture, the next frame.
        {
            C_FontManager m;
            m.AddGdiFont("Face", 512, 128, 20, 2, false);
            auto *f = static_cast<C_GdiFont *>(m.FindFont(2));
            g_gfLog.clear();
            f->Select();
            WM(r.gdSelect, GfLogIs({"settex gdi 0"}) && f->m_frame == 2);
            C_GdiFont none;
            g_gfLog.clear();
            none.Select();
            WM(r.gdSelect, g_gfLog.empty() && none.m_frame == 1 && none.m_cellWidth == -1 && none.m_textureHeight == -1);
            m.Free();
        }
        // Close: the texture released, the GDI objects deleted, every cell freed; then cleared.
        {
            C_FontManager m;
            m.AddGdiFont("Face", 512, 128, 20, 2, false);
            auto *f = static_cast<C_GdiFont *>(m.FindFont(2));
            f->GetGlyph('A');
            f->GetGlyph('B');
            S_FoGdiObject *const dib = g_foGdiObjects.back();
            S_FoGdiObject *const font = g_foGdiObjects[g_foGdiObjects.size() - 2];
            S_FoGdiObject *const dc = g_foGdiObjects[g_foGdiObjects.size() - 3];
            g_foDeleted.clear();
            g_gfLog.clear();
            f->Close();
            WM(r.gdClose, GfLogIs({"irelease"}) && g_foDeleted == std::vector<const void *>({dc, dib, font}) &&
                              f->m_image == nullptr && f->m_bits == nullptr && f->m_spare == nullptr && f->m_free.empty() &&
                              f->m_cells.empty() && f->m_newest == nullptr && f->m_oldest == nullptr && f->m_dc == dc);
            // Reset: every field cleared.
            f->m_free.push_back(nullptr);
            f->m_cells[1] = nullptr;
            f->m_newest = f->m_cursor = f->m_oldest = reinterpret_cast<S_GlyphCell *>(0x10);
            f->m_spare = reinterpret_cast<S_GlyphCell *>(0x20);
            f->m_frame = 9;
            f->m_bits = reinterpret_cast<uint8_t *>(0x30);
            f->m_image = reinterpret_cast<IImage *>(0x40);
            f->Reset();
            WM(r.gdClose, f->m_dc == nullptr && f->m_bitmap == nullptr && f->m_bits == nullptr && f->m_cellWidth == -1 &&
                              f->m_cellHeight == -1 && f->m_font == nullptr && f->m_size == 0 && f->m_image == nullptr &&
                              f->m_textureWidth == -1 && f->m_textureHeight == -1 && f->m_frame == 0 && f->m_spare == nullptr &&
                              f->m_free.empty() && f->m_cells.empty() && f->m_newest == nullptr && f->m_cursor == nullptr &&
                              f->m_oldest == nullptr);
            g_foDeleted.clear();
            m.Free();
            WM(r.gdClose, g_foDeleted == std::vector<const void *>({nullptr, nullptr, nullptr}) && m.m_fonts.empty());
        }
        g_foGdi = false;
    }


    // --- part 9a-iv: the text styles ---
    {
        // The first use of the process (the first mission): the list empty until then; a release
        // adds the game's styles too.
        static bool used;
        if (!used) {
            used = true;
            WM(r.stFirst, GetTextStyle(0) == nullptr);
            ReleaseTextStyle(-5);
            WM(r.stFirst, GetTextStyle(0) != nullptr && GetTextStyle(89) != nullptr && GetTextStyle(90) == nullptr);
        }
        // The game's 90 styles (their values: the asm's, by styles_sim.py's hash) and a few by hand.
        uint32_t hash = 0x811C9DC5;
        const auto mix = [&hash](uint32_t v) {
            for (int k = 0; k < 4; ++k) {
                hash ^= (v >> (8 * k)) & 0xFF;
                hash *= 0x01000193;
            }
        };
        const auto bits = [](float f) {
            uint32_t u;
            std::memcpy(&u, &f, 4);
            return u;
        };
        bool all = true;
        for (int i = 0; i < 90; ++i) {
            const S_TextStyleDesc *d = GetTextStyle(i);
            all = all && d;
            if (!d)
                break;
            for (uint32_t v : {static_cast<uint32_t>(d->font), d->color, bits(d->size), bits(d->width), bits(d->x),
                               bits(d->y), bits(d->below), bits(d->above), static_cast<uint32_t>(d->unknown20),
                               static_cast<uint32_t>(d->align), static_cast<uint32_t>(d->valign),
                               static_cast<uint32_t>(d->joins), static_cast<uint32_t>(d->wraps), d->flags30})
                mix(v);
        }
        WM(r.stDefaults, all && hash == 0xD7D65343u);
        const S_TextStyleDesc *s85 = GetTextStyle(85);
        WM(r.stDefaults, s85 && s85->font == 4 && s85->color == 0xFFFFFFFF && s85->size == 0.012f && s85->align == 1 &&
                             s85->joins == 1 && s85->wraps == 1 && s85->flags30 == 0x88);
        // New styles: after the game's; a freed place taken again; the free places at the end given
        // back (not the first place); an index not in the list: nothing.
        S_TextStyleDesc desc = {3, 0x12345678, 0.5f, 0.25f, 0.125f, 1.0f, 2.0f, 3.0f, 4, 5, 6, 7, 8, 9};
        const int a = CreateTextStyle(desc);
        desc.font = 9;
        const int b = CreateTextStyle(desc);
        const int c = CreateTextStyle(desc);
        WM(r.stCreate, a == 90 && b == 91 && c == 92 && GetTextStyle(90)->color == 0x12345678 && GetTextStyle(91)->font == 9 &&
                           GetTextStyle(90) != &desc && GetTextStyle(90)->flags30 == 9 && GetTextStyle(90)->wraps == 8);
        ReleaseTextStyle(91);
        WM(r.stRelease, GetTextStyle(91) == nullptr && GetTextStyle(92) != nullptr && NumTextStyles() == 93);
        desc.font = 11;
        WM(r.stCreate, CreateTextStyle(desc) == 91 && GetTextStyle(91)->font == 11);
        ReleaseTextStyle(91);
        ReleaseTextStyle(92);
        // (92 and the free 91 given back: the next new one is 91.)
        desc.font = 12;
        WM(r.stRelease, NumTextStyles() == 91 && GetTextStyle(92) == nullptr && GetTextStyle(91) == nullptr &&
                            CreateTextStyle(desc) == 91);
        ReleaseTextStyle(93);
        ReleaseTextStyle(-1);
        WM(r.stRelease, GetTextStyle(91) && GetTextStyle(91)->font == 12 && GetTextStyle(92) == nullptr);
        WM(r.stRelease, CreateTextStyle(desc) == 92);
        ReleaseTextStyle(92);
        ReleaseTextStyle(91);
        ReleaseTextStyle(90);
        WM(r.stRelease, GetTextStyle(90) == nullptr && GetTextStyle(89) != nullptr && CreateTextStyle(desc) == 90);
        ReleaseTextStyle(90);
        // Every style freed: the first place kept (empty); the game's styles made again in order.
        std::vector<S_TextStyleDesc> saved;
        for (int i = 0; i < 90; ++i)
            saved.push_back(*GetTextStyle(i));
        ReleaseTextStyle(45);
        WM(r.stRelease, GetTextStyle(45) == nullptr && GetTextStyle(89) != nullptr && NumTextStyles() == 90);
        for (int i = 89; i >= 0; --i)
            ReleaseTextStyle(i);
        WM(r.stRelease, GetTextStyle(0) == nullptr && GetTextStyle(1) == nullptr && NumTextStyles() == 1);
        bool back = true;
        for (int i = 0; i < 90; ++i)
            back = back && CreateTextStyle(saved[i]) == i;
        desc.font = 13;
        WM(r.stRelease, back && GetTextStyle(89)->flags30 == saved[89].flags30 && CreateTextStyle(desc) == 90);
        ReleaseTextStyle(90);
        WM(r.stRelease, GetTextStyle(90) == nullptr);
    }

    while (g_fontManager)
        ReleaseFontManager();
    g_foLanguage = "English";
    g_foWidth = 640;
    g_igraphVtable[0x18 / 4] = oldSlots[0];
    g_igraphVtable[0x3C / 4] = oldSlots[1];
    g_igraphVtable[0x44 / 4] = oldSlots[2];
    g_igraphVtable[0x88 / 4] = oldSlots[3];
    g_igraphVtable[0x40 / 4] = oldSlots[4];
    std::printf("fonts: foSets %d foLanguages %d foJapanese %d foConfig %d foVersion %d foShared %d foMissing %d "
                "foTable %d foQuads %d foGrow %d foFlush %d foMap %d\n",
                r.foSets, r.foLanguages, r.foJapanese, r.foConfig, r.foVersion, r.foShared, r.foMissing, r.foTable,
                r.foQuads, r.foGrow, r.foFlush, r.foMap);
    std::printf("xml: xmTree %d xmText %d xmComments %d xmAttributes %d xmBad %d xmWalk %d xmFile %d cfLanguage %d "
                "cfSet %d cfFonts %d cfValues %d\n",
                r.xmTree, r.xmText, r.xmComments, r.xmAttributes, r.xmBad, r.xmWalk, r.xmFile, r.cfLanguage, r.cfSet,
                r.cfFonts, r.cfValues);
    std::printf("gdi: gdInit %d gdCells %d gdFail %d gdGlyph %d gdTexture %d gdReuse %d gdEvict %d gdOrder %d gdSelect %d "
                "gdClose %d\n",
                r.gdInit, r.gdCells, r.gdFail, r.gdGlyph, r.gdTexture, r.gdReuse, r.gdEvict, r.gdOrder, r.gdSelect,
                r.gdClose);
    std::printf("styles: stFirst %d stDefaults %d stCreate %d stRelease %d\n", r.stFirst, r.stDefaults, r.stCreate,
                r.stRelease);
}
