// Human part 64: the message log and the chat line (game/interface_messages.cpp) against the original's
// (0x00494950..0x00496360). MinGW; build_messages.bat; runs messages_diff.exe ("messages_diff: N checks, 0 failures").
// interface_messages.cpp is compiled into this file; the kit (interface_kit.inc) fakes the frames, the scene, the driver
// and the graph. Scenarios of operations (Init, Done, Update with the keyboard's answers, Render, AddMessage, SetMode,
// StartSay, StartSayTeam, OpenInput; between them the unicode font's flag, the game's role, the font found or not) run
// from one start on each side (the original's block made by its constructor, the port's object by its), every call out
// logged by the same fakes, the log's fields and lines snapped after each operation, the frames compared at the end.
// Faked on both sides (the original's redirected): the font manager (AcquireFontManager 0x0063df00, ReleaseFontManager
// 0x0063df30, FindFont 0x0063df80, Release 0x0063df60), the fonts (each side its own: the vtables differ, g++ gives the
// virtual destructor two slots; Select logged, GetGlyph from a table of the round's glyphs), the graph's ScrnX, ScrnY,
// DrawPrimitives (every vertex logged), GetAsciiKey, ReadChar, FlushChars, IsKeyDown (the operation's answers), the
// driver's Set and GetState, the area frame's bound (UpdateBound: a new one each call), the console (ExecuteConsoleLine
// 0x00720500, ExecuteConsoleCommand 0x00720710), ReadUnicodeInput (0x0066b680), the game's SetPaused (0x0069bb80; the
// original's game pointer 0x008ae8f4). The port's DecodeUtf8 and EncodeUtf8 are the original's (0x0064d830, 0x0064d910:
// the text box's, tested there).
// HD2_MESSAGES_ROUNDS: scenarios (default 20,000; HD2_MESSAGES_QUICK: 6,000); HD2_INTERFACE_ULPS: float tolerance;
// HD2_INTERFACE_SHOW: a round's two logs; HD2_INTERFACE_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/interface_messages.cpp"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"

namespace H {

// ---- the fonts: a glyph table, each side's font object ----
S_Glyph g_glyphs[256];
const S_Glyph &GlyphOf(int ch) { return g_glyphs[(ch ^ (ch >> 8) ^ (ch >> 16)) & 0xFF]; }
bool g_fontMissing;
uint8_t g_fontManager[0x20];
const S_Glyph *FontGetGlyph(int ch)
{
    Log("font GetGlyph %d", ch);
    return &GlyphOf(ch);
}
void FontSelect() { Log("font Select"); }
struct C_FakeFont : C_Font {
    void GrowVertices() override { Log("font GrowVertices"); }
    void Flush() override { Log("font Flush"); }
    void ClearVertices() override { Log("font ClearVertices"); }
    void Select() override { FontSelect(); }
    const S_Glyph *GetGlyph(int ch) override { return FontGetGlyph(ch); }
    void AddQuad(int, uint32_t, int, int, int, int, float) override { Log("font AddQuad"); }
};
C_FakeFont g_portFont;
// The original's: its vtable's Select (+0x10) and GetGlyph (+0x14), thiscall.
void __attribute__((thiscall)) OF_Select(void *) { FontSelect(); }
const S_Glyph *__attribute__((thiscall)) OF_GetGlyph(void *, int ch) { return FontGetGlyph(ch); }
void __attribute__((thiscall)) OF_Other(void *) { Log("font other slot"); }
void *g_origFontVt[8];
S_Fake g_origFont{g_origFontVt};
void *__cdecl O_AcquireFonts()
{
    Log("AcquireFontManager");
    return g_fontManager;
}
void __cdecl O_ReleaseFonts() { Log("ReleaseFontManager"); }
void *__attribute__((thiscall)) O_FindFont(void *manager, int id)
{
    Log("%s FindFont %d", Name(manager).c_str(), id);
    return g_fontMissing ? nullptr : &g_origFont;
}
void __attribute__((thiscall)) O_ManagerRelease(void *manager) { Log("%s Release", Name(manager).c_str()); }

// ---- the graph: the screen, the keyboard, the drawing ----
int g_scrnX, g_scrnY;
struct S_Keys {
    int ascii = 0;
    bool down = false;
    uint32_t ch = 0;
    std::vector<uint16_t> typed;   // ReadUnicodeInput's
    bool typedOk = true;
};
S_Keys g_keys;
int __stdcall G_ScrnX(void *)
{
    Log("graph ScrnX");
    return g_scrnX;
}
int __stdcall G_ScrnY(void *)
{
    Log("graph ScrnY");
    return g_scrnY;
}
void __stdcall G_DrawPrimitives(void *, int kind, int count, const void *vertices, int transformed)
{
    Log("graph DrawPrimitives %d %d %d", kind, count, transformed);
    const auto *v = static_cast<const S_TLVertex *>(vertices);
    for (int k = 0; k < count * 3; ++k)
        Log("  v %s %s %s %s %08x %s %s", FloatText(v[k].x).c_str(), FloatText(v[k].y).c_str(), FloatText(v[k].z).c_str(),
            FloatText(v[k].rhw).c_str(), v[k].color, FloatText(v[k].u).c_str(), FloatText(v[k].v).c_str());
}
int __stdcall G_GetAsciiKey(void *)
{
    Log("graph GetAsciiKey");
    return g_keys.ascii;
}
uint32_t __stdcall G_ReadChar(void *)
{
    Log("graph ReadChar");
    return g_keys.ch;
}
void __stdcall G_FlushChars(void *) { Log("graph FlushChars"); }
bool __stdcall G_IsKeyDown(void *, int key)
{
    Log("graph IsKeyDown %d", key & 0xFF);   // (a byte: the original pushes a register with other bits above it)
    return g_keys.down;
}

// ---- the console, the typed text, the game ----
void ConsoleLine(const char *line) { Log("console line '%s'", line); }
void ConsoleCommand(int command, const char *args) { Log("console command %d '%s'", command, args); }
bool UnicodeInput(wchar_t *buffer, int maxLength)
{
    Log("ReadUnicodeInput %d", maxLength);
    if (!g_keys.typedOk)
        return false;
    int n = 0;
    for (; n < int(g_keys.typed.size()) && n < maxLength; ++n)
        buffer[n] = static_cast<wchar_t>(g_keys.typed[n]);
    buffer[n] = 0;
    return true;
}
int __attribute__((fastcall)) O_ConsoleLine(const char *line)
{
    ConsoleLine(line);
    return 0;
}
int __attribute__((fastcall)) O_ConsoleCommand(int command, const char *args)
{
    ConsoleCommand(command, args);
    return 0;
}
bool __attribute__((fastcall)) O_UnicodeInput(wchar_t *buffer, int maxLength) { return UnicodeInput(buffer, maxLength); }
alignas(16) uint8_t g_game[0x100];
void __attribute__((thiscall)) O_SetPaused(void *game, bool paused) { Log("%s SetPaused %d", Name(game).c_str(), paused); }
using T_Decode = int(__attribute__((fastcall)) *)(uint32_t *, const char *, int);
using T_Encode = int(__attribute__((fastcall)) *)(char *, const uint32_t *, int);

bool &OrigUnicode() { return *OriginalAt<bool *>(0x008aea3c); }
uint32_t &OrigRole() { return *OriginalAt<uint32_t *>(0x00982100); }

std::string PartName(const void *p)
{
    if (p == g_fontManager)
        return "fonts";
    if (p == &g_portFont || p == &g_origFont)
        return "font";
    if (p == g_game)
        return "game";
    return "";
}

}  // namespace H

// ---- the port's side ----
C_FontManager *AcquireFontManager() { return static_cast<C_FontManager *>(H::O_AcquireFonts()); }
void ReleaseFontManager() { H::O_ReleaseFonts(); }
C_Font *C_FontManager::FindFont(int id)
{
    H::Log("%s FindFont %d", H::Name(this).c_str(), id);
    return H::g_fontMissing ? nullptr : &H::g_portFont;
}
void C_FontManager::Release() { H::Log("%s Release", H::Name(this).c_str()); }
int ExecuteConsoleLine(const char *line)
{
    H::ConsoleLine(line);
    return 0;
}
int ExecuteConsoleCommand(int command, const char *args)
{
    H::ConsoleCommand(command, args);
    return 0;
}
bool ReadUnicodeInput(wchar_t *buffer, int maxLength) { return H::UnicodeInput(buffer, maxLength); }
int DecodeUtf8(uint32_t *codePoint, const char *text, int length) { return OriginalAt<H::T_Decode>(0x0064d830)(codePoint, text, length); }
int EncodeUtf8(char *out, const uint32_t *codePoint, int maxBytes) { return OriginalAt<H::T_Encode>(0x0064d910)(out, codePoint, maxBytes); }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_game); }
void C_game::SetPaused(bool paused) { H::O_SetPaused(this, paused); }

namespace H {

// ---- the scenarios ----
enum E_Op { OP_INIT, OP_DONE, OP_UPDATE, OP_RENDER, OP_ADD, OP_MODE, OP_SAY, OP_SAY_TEAM, OP_OPEN, OP_UNICODE, OP_ROLE, OP_FONT };
const char *const OP_NAMES[] = {"Init", "Done", "Update", "Render", "AddMessage", "SetMode", "StartSay", "StartSayTeam",
                                "OpenInput", "Unicode", "Role", "Font"};
struct S_Op {
    E_Op kind;
    int a = 0;
    S_Keys keys;
    S_vector color;
    std::string text;
};
struct S_Scenario {
    std::vector<S_Op> ops;
    uint32_t seed;
    bool unicode;
    uint32_t role;
    bool fontMissing;
    int scrnX, scrnY;
};
constexpr int AREA = 0;   // the "Text" frame

// A bound for the area's next UpdateBound (each call another).
S_vector g_boundMin, g_boundMax;
int g_boundCalls;
bool g_boundShift;    // the rectangles' left and right differ too
bool g_boundExactY;   // the bottom not shifted (exactly 0.005: -0 pixels)
void NextBound(I3D_frame *f)
{
    ++g_boundCalls;
    const S_vector &at = f->m_localMatrix(3);
    const float dx = g_boundShift ? g_boundCalls * 0.01f + at.x * 0.001f + at.z * 0.0007f : 0.0f;
    const float dy = g_boundExactY ? 0.0f : g_boundCalls * 0.13f + at.y * 0.25f;
    f->m_bound.bbox.min = S_vector(g_boundMin.x + dx, g_boundMin.y - dy, g_boundMin.z);
    f->m_bound.bbox.max = S_vector(g_boundMax.x + dx, g_boundMax.y - dy, g_boundMax.z);
    f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
}
I3D_frame *FindArea(const char *name) { return std::strcmp(name, "Text") == 0 ? FrameAt(AREA) : nullptr; }

std::string RandomText(int maxLength)
{
    std::string s;
    const int n = P(0.1) ? 0 : I(1, maxLength);
    for (int k = 0; k < n; ++k) {
        switch (I(0, 9)) {
        case 0:   // a UTF-8 sequence (two or three bytes), or a stray continuation byte
            if (P(0.5)) {
                s += char(0xC0 | I(2, 0x1F));
                s += char(0x80 | I(0, 0x3F));
            } else if (P(0.6)) {
                s += char(0xE0 | I(0, 0xF));
                s += char(0x80 | I(0, 0x3F));
                s += char(0x80 | I(0, 0x3F));
            } else {
                s += char(I(0x80, 0xFF));
            }
            break;
        case 1:
            s += ' ';
            break;
        default:
            s += char(I(0x21, 0x7E));
            break;
        }
    }
    return s;
}
S_Keys RandomKeys()
{
    S_Keys k;
    k.ascii = P(0.5) ? 0 : P(0.4) ? Pick({200, 0xD0}) : I(1, 255);
    k.down = P(0.6);
    const uint32_t chars[] = {0, 0, 8, 9, 13, 0x61, 0x20, 0x5F, 0xE9, 0x41, 0x7E, 0x100, 0xFF, 0xF0, 0xF4, 0xF8, 0x80, 0x9F, 0xBF, 0xC3, 0xE2,
                              0xC0, 0xDF, 0xE0, 0xEF, 0xF7};
    k.ch = P(0.6) ? Pick(chars) : uint32_t(I(0x20, 0x7E));
    k.typedOk = P(0.9);
    const int n = P(0.3) ? 0 : I(1, P(0.1) ? 300 : 6);
    for (int j = 0; j < n; ++j) {
        const uint16_t specials[] = {8, 9, 13, 0xE9, 0x20AC, 0x7FF, 0x800, 0xFFFF, 0xD800, 0x41};
        k.typed.push_back(P(0.4) ? Pick(specials) : uint16_t(I(0x20, 0x7E)));
    }
    return k;
}
S_vector RandomColor()
{
    const float specials[] = {0.0f, 255.0f, 255.9f, -1.0f, 256.0f, 1e10f, -1e10f, NAN, INFINITY, 0.5f};
    const auto c = [&specials] { return P(0.15) ? Pick(specials) : F(0, 255); };
    const float r = c();
    const float g = c();
    const float b = c();
    return S_vector(r, g, b);
}

S_Scenario RandomScenario()
{
    S_Scenario s;
    s.seed = U32();
    s.unicode = P(0.4);
    s.role = P(0.8) ? 7u : Pick({0u, 1u, 3u, 6u});
    s.fontMissing = P(0.05);
    s.scrnX = Pick({640, 800, 1024, 1280, 1600});
    s.scrnY = Pick({480, 600, 768, 1024, 1200});
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    g_boundMin = S_vector(F(-0.1f, 0.5f), F(-0.8f, -0.2f), 0);
    g_boundMax = S_vector(g_boundMin.x + F(0.05f, 0.9f), g_boundMin.y + F(0.02f, 0.6f), 0);
    if (P(0.05))
        g_boundMax.x = g_boundMin.x;
    if (P(0.03))
        g_boundMin.x = Pick({NAN, -INFINITY, 1e12f});
    g_boundShift = P(0.6);
    // (Edges exactly on 0: left -0.005 + 0.005, right 0.005 - 0.005, bottom from 0.005 - 0.005 times a negative (-0); the
    // right edge on a whole pixel.)
    if (P(0.05)) {
        g_boundMin.x = -0.005f;
        g_boundShift = false;
    }
    if (P(0.04)) {
        g_boundMax.x = Pick({0.005f, 0.0055f, 0.0049f});
        g_boundShift = false;
    }
    g_boundExactY = P(0.04);
    if (g_boundExactY)
        g_boundMax.y = 0.005f;
    if (P(0.05)) {
        float m = 0.505f;
        for (int k = 0; k < 40 && m - 0.005f != 0.5f; ++k)
            m = std::nextafter(m, (m - 0.005f) < 0.5f ? 1.0f : 0.0f);
        g_boundMax.x = m;
        g_boundShift = false;
    }
    for (S_Glyph &glyph : g_glyphs) {
        glyph.u0 = F(0, 1);
        glyph.v0 = F(0, 1);
        glyph.u1 = F(0, 1);
        glyph.v1 = F(0, 1);
        glyph.aspect = P(0.15) ? 0.0f : P(0.05) ? Pick({1e-6f, 3.0f, 0.4999f}) : F(0.2f, 1.2f);
    }
    bool alive = false;
    const int count = I(1, 24);
    for (int n = 0; n < count; ++n) {
        S_Op op;
        if (!alive)
            op.kind = n == 0 || P(0.5) ? OP_INIT : Pick({OP_ADD, OP_UPDATE, OP_RENDER, OP_UNICODE, OP_ROLE, OP_FONT});
        else
            op.kind = static_cast<E_Op>(Pick({1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11}));
        if (op.kind == OP_DONE && P(0.6))
            op.kind = OP_UPDATE;
        switch (op.kind) {
        case OP_INIT:
            alive = true;
            op.a = P(0.6) ? Pick({50, 1, 2, 3}) : I(1, 60);   // (the game: 50; 0 crashes both: the first line deletes the front of none)
            break;
        case OP_DONE:
            alive = false;
            break;
        case OP_UPDATE:
            op.a = P(0.6) ? Pick({0, 1, 16, 33, 50, 100, 149, 150, 151, 300, 2000, 2001}) : I(0, 3000);
            op.keys = RandomKeys();
            break;
        case OP_ADD:
            op.color = RandomColor();
            op.text = RandomText(P(0.1) ? 300 : 40);
            break;
        case OP_MODE:
            op.a = P(0.9) ? I(0, 1) : 2;
            break;
        case OP_OPEN:
            op.a = P(0.5);
            break;
        case OP_UNICODE:
            op.a = P(0.4);
            break;
        case OP_ROLE:
            op.a = static_cast<int>(P(0.8) ? 7u : Pick({0u, 1u, 3u, 6u}));
            break;
        case OP_FONT:
            op.a = P(0.1);
            break;
        default:
            break;
        }
        s.ops.push_back(op);
        // Now and then a burst: a character of 1 to 4 bytes (or a wrong lead) typed byte by byte in the ASCII mode,
        // then a backspace in the unicode mode (which removes whole characters only).
        if (alive && P(0.04)) {
            const auto add = [&s](E_Op kind, int a, const S_Keys &keys = S_Keys()) {
                S_Op o;
                o.kind = kind;
                o.a = a;
                o.keys = keys;
                s.ops.push_back(o);
            };
            add(OP_OPEN, 1);
            add(OP_UNICODE, 0);
            const int bytes = I(1, 5);
            const uint32_t leads[] = {0x41, 0xC3, 0xE2, 0xF0, 0xF8, 0xC0, 0xDF, 0xE0, 0xEF, 0xF7};
            for (int k = 0; k < bytes; ++k) {
                S_Keys keys;
                keys.ch = k == 0 ? (P(0.6) ? leads[bytes - 1] : Pick(leads)) : uint32_t(I(0x80, 0xBF));
                add(OP_UPDATE, 16, keys);
            }
            add(OP_UNICODE, 1);
            S_Keys back;
            back.typed = {8};
            add(OP_UPDATE, 16, back);
        }
        if (alive && P(0.012)) {
            const auto add = [&s](E_Op kind, int a, const S_Keys &keys = S_Keys()) {
                S_Op o;
                o.kind = kind;
                o.a = a;
                o.keys = keys;
                s.ops.push_back(o);
            };
            add(OP_OPEN, 1);
            const bool unicode = P(0.5);
            add(OP_UNICODE, unicode);
            if (unicode) {
                S_Keys fill;
                const int n = I(240, 300);
                for (int k = 0; k < n; ++k)
                    fill.typed.push_back(P(0.9) ? uint16_t(I(0x21, 0x7E)) : uint16_t(0xE9));
                add(OP_UPDATE, 16, fill);
            } else {
                const int n = I(250, 258);
                for (int k = 0; k < n; ++k) {
                    S_Keys key;
                    key.ch = uint32_t(I(0x21, 0x7E));
                    add(OP_UPDATE, 1, key);
                }
            }
            for (int k = I(0, 3); k > 0; --k) {
                S_Keys back;
                if (unicode)
                    back.typed = {8};
                else
                    back.ch = 8;
                add(OP_UPDATE, 1, back);
            }
            if (P(0.5)) {
                add(OP_OPEN, 0);
                add(OP_OPEN, 1);
                S_Keys back;
                if (unicode)
                    back.typed = {8};
                else
                    back.ch = 8;
                add(OP_UPDATE, 1, back);
            }
        }
    }
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    return s;
}

// A side's snap of the log's fields and lines.
struct S_LineView {
    const char *text;
    int r, g, b, age, font;
};
std::string Snap(const S_vector &color, int mode, const void *fonts, const void *area, const std::vector<S_LineView> &lines, int maxLines,
                 int scroll, int repeatKey, int repeatTime, int repeatDelay, float sw, float sh, const float *rect, const float *history,
                 const char *input, int inputScroll, int vertexCount, float lineHeight, int d374, int d378, int ageTime, int caretTime,
                 bool d384, bool inputOpen, bool sayAll, bool sayTeam)
{
    char text[700];
    std::snprintf(text, sizeof text,
                  "log %s mode %d %s %s max %d scroll %d repeat %d %d %d screen %s %s rect %s %s %s %s history %s %s %s %s scroll %d "
                  "vertices %d height %s %d %d age %d caret %d %d open %d say %d %d input '%s'",
                  VecText(color).c_str(), mode, Name(fonts).c_str(), Name(area).c_str(), maxLines, scroll, repeatKey, repeatTime, repeatDelay,
                  FloatText(sw).c_str(), FloatText(sh).c_str(), FloatText(rect[0]).c_str(), FloatText(rect[1]).c_str(),
                  FloatText(rect[2]).c_str(), FloatText(rect[3]).c_str(), FloatText(history[0]).c_str(), FloatText(history[1]).c_str(),
                  FloatText(history[2]).c_str(), FloatText(history[3]).c_str(), inputScroll, vertexCount, FloatText(lineHeight).c_str(),
                  d374, d378, ageTime, caretTime, d384, inputOpen, sayAll, sayTeam, input);
    std::string s = text;
    s += " lines " + std::to_string(lines.size());
    for (const S_LineView &l : lines) {
        std::snprintf(text, sizeof text, " ['%s' %d %d %d %d %d]", l.text, l.r, l.g, l.b, l.age, l.font);
        s += text;
    }
    return s;
}
std::string SnapPort(const C_MessageLog &m)
{
    std::vector<S_LineView> lines;
    for (const S_LogLine &l : m.m_lines)
        lines.push_back({l.text, l.r, l.g, l.b, l.age, l.font});
    return Snap(m.m_color, m.m_mode, m.m_fonts, m.m_area, lines, m.m_maxLines, m.m_scroll, m.m_repeatKey, m.m_repeatTime, m.m_repeatDelay,
                m.m_screenWidth, m.m_screenHeight, m.m_rect, m.m_historyRect, m.m_input, m.m_inputScroll, m.m_vertexCount, m.m_lineHeight,
                m.m_unknownD374, m.m_unknownD378, m.m_ageTime, m.m_caretTime, m.m_unknownD384, m.m_inputOpen, m.m_sayAll, m.m_sayTeam);
}
template <class T> const T &At(const uint8_t *b, uint32_t o) { return *reinterpret_cast<const T *>(b + o); }
std::string SnapOrig(const uint8_t *m)
{
    std::vector<S_LineView> lines;
    const uint8_t *first = At<const uint8_t *>(m, 0x20), *last = At<const uint8_t *>(m, 0x24);
    for (const uint8_t *l = first; l && l < last; l += 0x18)
        lines.push_back({At<const char *>(l, 0), At<int>(l, 4), At<int>(l, 8), At<int>(l, 0xC), At<int>(l, 0x10), At<int>(l, 0x14)});
    return Snap(At<S_vector>(m, 0), At<int>(m, 0x10), At<void *>(m, 0x14), At<void *>(m, 0x18), lines, At<int>(m, 0x2C), At<int>(m, 0x30),
                At<int>(m, 0x34), At<int>(m, 0x38), At<int>(m, 0x3C), At<float>(m, 0x40), At<float>(m, 0x44), &At<float>(m, 0x48),
                &At<float>(m, 0x58), &At<char>(m, 0x68), At<int>(m, 0x168), At<int>(m, 0xD36C), At<float>(m, 0xD370), At<int>(m, 0xD374),
                At<int>(m, 0xD378), At<int>(m, 0xD37C), At<int>(m, 0xD380), At<bool>(m, 0xD384), At<bool>(m, 0xD385), At<bool>(m, 0xD386),
                At<bool>(m, 0xD387));
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_portRun, g_origRun;

void StartSide(const S_Scenario &s)
{
    std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
    g_log.clear();
    g_boundCalls = 0;
    g_keys = S_Keys();
    g_fontMissing = s.fontMissing;
    g_scrnX = s.scrnX;
    g_scrnY = s.scrnY;
    g_gameProfile.m_unicodeFont = s.unicode;
    OrigUnicode() = s.unicode;
    g_gameRole = s.role;
    OrigRole() = s.role;
    std::memset(g_driverState, 0, sizeof g_driverState);
    g_driverState[3] = 7;
    std::srand(s.seed);
}
// The world's operations (both sides alike).
bool ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case OP_UNICODE:
        g_gameProfile.m_unicodeFont = op.a != 0;
        OrigUnicode() = op.a != 0;
        return true;
    case OP_ROLE:
        g_gameRole = uint32_t(op.a);
        OrigRole() = uint32_t(op.a);
        return true;
    case OP_FONT:
        g_fontMissing = op.a != 0;
        return true;
    default:
        return false;
    }
}

void RunPort(const S_Scenario &s)
{
    StartSide(s);
    C_MessageLog *m = new C_MessageLog;
    for (const S_Op &op : s.ops) {
        Log("-- %s %d", OP_NAMES[op.kind], op.a);
        g_keys = op.keys;
        switch (op.kind) {
        case OP_INIT:
            m->Init(reinterpret_cast<I3D_scene *>(&g_scene), op.a);
            break;
        case OP_DONE:
            m->Done();
            break;
        case OP_UPDATE:
            m->Update(op.a);
            break;
        case OP_RENDER:
            m->Render();
            break;
        case OP_ADD:
            m->AddMessage(op.color, op.text.c_str());
            break;
        case OP_MODE:
            m->SetMode(op.a);
            break;
        case OP_SAY:
            m->StartSay();
            break;
        case OP_SAY_TEAM:
            m->StartSayTeam();
            break;
        case OP_OPEN:
            m->OpenInput(op.a != 0);
            break;
        default:
            ApplyWorld(op);
            break;
        }
        g_log.push_back(SnapPort(*m));
    }
    Log("-- delete");
    delete m;
    g_portRun.log.swap(g_log);
    std::memcpy(&g_portRun.arena, &g_arena, sizeof g_arena);
}
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Init = void(__attribute__((thiscall)) *)(void *, void *, int);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_Add = void(__attribute__((thiscall)) *)(void *, const S_vector *, const char *);
void RunOrig(const S_Scenario &s)
{
    StartSide(s);
    uint8_t *m = static_cast<uint8_t *>(std::calloc(1, 0xD400));
    OriginalAt<T_Void>(0x00494980)(m);
    for (const S_Op &op : s.ops) {
        Log("-- %s %d", OP_NAMES[op.kind], op.a);
        g_keys = op.keys;
        switch (op.kind) {
        case OP_INIT:
            OriginalAt<T_Init>(0x00495f40)(m, &g_scene, op.a);
            break;
        case OP_DONE:
            OriginalAt<T_Void>(0x004960f0)(m);
            break;
        case OP_UPDATE:
            OriginalAt<T_Int>(0x00496230)(m, op.a);
            break;
        case OP_RENDER:
            OriginalAt<T_Void>(0x00495f00)(m);
            break;
        case OP_ADD:
            OriginalAt<T_Add>(0x00494a40)(m, &op.color, op.text.c_str());
            break;
        case OP_MODE:
            OriginalAt<T_Int>(0x00496120)(m, op.a);
            break;
        case OP_SAY:
            OriginalAt<T_Void>(0x004961c0)(m);
            break;
        case OP_SAY_TEAM:
            OriginalAt<T_Void>(0x004961a0)(m);
            break;
        case OP_OPEN:
            OriginalAt<T_Bool>(0x004961e0)(m, op.a != 0);
            break;
        default:
            ApplyWorld(op);
            break;
        }
        g_log.push_back(SnapOrig(m));
    }
    Log("-- delete");
    OriginalAt<T_Void>(0x004949e0)(m);
    std::free(m);
    g_origRun.log.swap(g_log);
    std::memcpy(&g_origRun.arena, &g_arena, sizeof g_arena);
}

void TestScenarios()
{
    const S_Scenario s = RandomScenario();
    RunOrig(s);
    RunPort(s);
    CompareLogs(g_origRun.log, g_portRun.log);
    CompareArenas(g_origRun.arena, g_portRun.arena);
    ShowLogs(g_origRun.log, g_portRun.log);
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindArea;
    g_updateBound = NextBound;
    for (void *&s : g_origFontVt)
        s = reinterpret_cast<void *>(OF_Other);
    g_origFontVt[0x10 / 4] = reinterpret_cast<void *>(OF_Select);
    g_origFontVt[0x14 / 4] = reinterpret_cast<void *>(OF_GetGlyph);
    g_graphVt[0x18 / 4] = reinterpret_cast<void *>(G_ScrnX);
    g_graphVt[0x1C / 4] = reinterpret_cast<void *>(G_ScrnY);
    g_graphVt[0x88 / 4] = reinterpret_cast<void *>(G_DrawPrimitives);
    g_graphVt[0xA0 / 4] = reinterpret_cast<void *>(G_GetAsciiKey);
    g_graphVt[0xA8 / 4] = reinterpret_cast<void *>(G_ReadChar);
    g_graphVt[0xAC / 4] = reinterpret_cast<void *>(G_FlushChars);
    g_graphVt[0xB0 / 4] = reinterpret_cast<void *>(G_IsKeyDown);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x0063df00, reinterpret_cast<const void *>(O_AcquireFonts)},
        {0x0063df30, reinterpret_cast<const void *>(O_ReleaseFonts)},
        {0x0063df80, reinterpret_cast<const void *>(O_FindFont)},
        {0x0063df60, reinterpret_cast<const void *>(O_ManagerRelease)},
        {0x00720500, reinterpret_cast<const void *>(O_ConsoleLine)},
        {0x00720710, reinterpret_cast<const void *>(O_ConsoleCommand)},
        {0x0066b680, reinterpret_cast<const void *>(O_UnicodeInput)},
        {0x0069bb80, reinterpret_cast<const void *>(O_SetPaused)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    *OriginalAt<void **>(0x008ae8f4) = g_game;
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("messages_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_MESSAGES_QUICK") ? 6000 : 20000;
    if (const char *r = std::getenv("HD2_MESSAGES_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "Scenarios";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    std::printf("messages_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
