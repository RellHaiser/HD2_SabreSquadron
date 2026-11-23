// Script part 1: the mission script compiler (script/script_compiler.cpp, with script_expression.cpp's parser and the
// script's building members) against the original executable's, over every game script and mutated copies of them.
// Both compile the same file (the dta functions read H:\HD2_Unpacked, or a mutated copy in memory); the compiled
// scripts (commands, literals, variables, whenevers, handlers, expression trees) and the compiler's symbols are written
// out as text and compared line by line; before them, the string literals' printf formatting against the original's
// sprintf, and a few crafted files. MinGW; build_sc.bat; runs sc_diff.exe ("sc_diff: N checks, 0 failures").
// Env: HD2_SC_DATA or HD2_UNPACKED (the unpacked data, default H:\HD2_Unpacked), HD2_SC_MUTANTS (mutated copies per
// file, default 12; quick 2), HD2_SC_QUICK, HD2_SC_ONLY (a substring of the script paths to run), HD2_SC_DUMP (print
// both dumps of the failures), HD2_SC_SEED, HD2_SC_SAVE_FAILS (a folder: each failing mutant saved there, and the one
// being compiled as current.scr), HD2_SC_REPLAY (one saved mutant compiled and dumped), HD2_SC_BISECT (a script path:
// the first line after which the two differ), HD2_SC_FIRST_CHANCE (access violations reported where they happen).
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <new>
#include <string>
#include <vector>

#define private public
#define protected public
#include "game/game.h"
#include "game/pending.h"
#include "ls3d/rw_data.h"
#include "script/script.h"
#include "script/script_compiler.h"
#include "script/script_expression.h"
#include "script/script_opcodes.h"
#include "script/script_tables.h"
#undef protected
#undef private

#include "original_exe.h"

#define TC __attribute__((thiscall))

namespace H {

int checks, failures;
const char *g_case = "";
std::string g_current;                                  // the script being compiled (for a crash)

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    std::printf("sc_diff: exception %08lx at %p (linked %08x) [%s %s]\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, unsigned(at - base + 0x20000000), g_case, g_current.c_str());
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
// HD2_SC_FIRST_CHANCE: every access violation reported where it happens (a fault under the original's exception frames
// is otherwise silent).
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW) {
        const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        char text[300];
        const int n = std::snprintf(text, sizeof text, "first chance %08lx at %08x (linked %08x) reading %08x [%s %s]\n", code,
                                    unsigned(at), unsigned(at - base + 0x20000000),
                                    unsigned(e->ExceptionRecord->ExceptionInformation[1]), g_case, g_current.c_str());
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, DWORD(n), &written, nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- random numbers (a fixed sequence) ----
uint32_t g_seed = 12345;
uint32_t U32()
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
int I(int lo, int hi) { return lo + int(U32() % uint32_t(hi - lo + 1)); }
bool P(double p) { return (U32() & 0xFFFFFF) < uint32_t(p * 0x1000000); }

// ---- the files: the data folder, or a mutated copy in memory (both sides' dta functions) ----
std::string g_data = "H:\\HD2_Unpacked";
std::map<std::string, std::string> g_overrides;        // by lower-case path
std::map<std::string, std::string> g_cache;
std::vector<std::string> g_opened;                      // the paths each side opened, in order
std::string Lower(std::string s)
{
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    for (char &c : s)
        if (c == '/')
            c = '\\';
    return s;
}
bool ReadDisk(const std::string &path, std::string &out)
{
    FILE *f = std::fopen((g_data + "\\" + path).c_str(), "rb");
    if (!f)
        return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return true;
}
bool FileText(const std::string &name, std::string &out)
{
    const std::string key = Lower(name);
    auto o = g_overrides.find(key);
    if (o != g_overrides.end()) {
        out = o->second;
        return true;
    }
    auto c = g_cache.find(key);
    if (c == g_cache.end()) {
        std::string text;
        if (!ReadDisk(name, text))
            text = std::string("\x01missing", 8);
        c = g_cache.emplace(key, text).first;
    }
    if (c->second == std::string("\x01missing", 8))
        return false;
    out = c->second;
    return true;
}
struct S_Open {
    std::string text;
    size_t at = 0;
};
std::map<int, S_Open> g_open;
int g_nextHandle = 1;
int FileOpen(const char *name)
{
    g_opened.push_back(name);
    S_Open o;
    if (!FileText(name, o.text))
        return -1;
    g_open[g_nextHandle] = o;
    return g_nextHandle++;
}
int FileRead(int h, void *data, int size)
{
    auto it = g_open.find(h);
    if (it == g_open.end())
        return -1;
    S_Open &o = it->second;
    const int got = o.at >= o.text.size() ? 0 : int(std::min(o.text.size() - o.at, size_t(size)));
    if (got)
        std::memcpy(data, o.text.data() + o.at, size_t(got));
    o.at += size_t(got);
    return got;
}
bool FileClose(int h)
{
    g_opened.push_back("(close)");
    return g_open.erase(h) != 0;
}

int __stdcall O_dtaOpen(const char *name, int) { return FileOpen(name); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
int __stdcall O_dtaClose(int h) { return FileClose(h); }
// The original's frees are held until its script is dumped: its GroupRange deletes a node the new group still points to
// (a use after free), and a block reused by a later allocation would make the dump depend on the heap. Held, the node
// stays as it was deleted.
std::vector<void *> g_heldFrees;
bool g_holdFrees;
// Each block has its size before it and 16 guard bytes after it, checked when it is freed: a write past its end (the
// original's GroupRange past its list) is reported with the case that made it.
std::string g_overrun;
int g_overruns;
void *__cdecl O_New(size_t size)
{
    uint8_t *p = static_cast<uint8_t *>(std::calloc(1, size + 32));
    std::memcpy(p, &size, sizeof size);
    std::memset(p + 16 + size, 0xAB, 16);
    return p + 16;
}
void FreeBlock(void *q)
{
    uint8_t *p = static_cast<uint8_t *>(q) - 16;
    size_t size;
    std::memcpy(&size, p, sizeof size);
    for (int i = 0; i < 16; ++i)
        if (p[16 + size + i] != 0xAB && g_overrun.empty())
            g_overrun = g_current;
    std::free(p);
}
void __cdecl O_Delete(void *p)
{
    if (!p)
        return;
    if (g_holdFrees)
        g_heldFrees.push_back(p);
    else
        FreeBlock(p);
}
void ReleaseHeldFrees()
{
    g_holdFrees = false;
    std::sort(g_heldFrees.begin(), g_heldFrees.end());
    g_heldFrees.erase(std::unique(g_heldFrees.begin(), g_heldFrees.end()), g_heldFrees.end());
    for (void *p : g_heldFrees)
        FreeBlock(p);
    g_heldFrees.clear();
}

// The game: the mission name (the original's C_str at +0x6A, unaligned; the port's member).
alignas(16) uint8_t g_origGame[0x400];
alignas(16) uint8_t g_portGameBuf[sizeof(C_game) + 64];
std::string g_mission;
uint8_t g_origMissionRep[0x110];
void SetMission(const std::string &name)
{
    g_mission = name;
    std::memset(g_origMissionRep, 0, sizeof g_origMissionRep);
    const int one = 1, length = int(name.size());
    std::memcpy(g_origMissionRep, &one, 4);
    std::memcpy(g_origMissionRep + 4, &length, 4);
    std::memcpy(g_origMissionRep + 8, name.c_str(), name.size() + 1);
    void *rep = g_origMissionRep;
    std::memcpy(g_origGame + 0x6A, &rep, 4);
    reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName = name.c_str();
}
void *__cdecl O_GetGame() { return g_origGame; }

// ---- the dumps ----
std::string F(const char *format, ...)
{
    char text[1200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    return text;
}
template <class T> T At(const void *p, uint32_t off)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + off, sizeof v);
    return v;
}
// An MSVC6 vector at `off`: its allocator word, then first, last, end.
std::vector<uint32_t> OrigVec(const void *p, uint32_t off, uint32_t elemSize)
{
    std::vector<uint32_t> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    if (!first)
        return out;
    for (uint32_t q = first; q < last; q += elemSize)
        out.push_back(q);
    return out;
}
std::string OrigStr(uint32_t rep)
{
    if (!rep)
        return "\"\"";
    const uint32_t length = At<uint32_t>(reinterpret_cast<void *>(rep), 4);
    return "\"" + std::string(reinterpret_cast<const char *>(rep + 8), length) + "\"";
}
std::string PortStr(const C_str &s) { return "\"" + std::string(s.c_str(), s.Size()) + "\""; }
std::string Args(int opcode, const int *args)
{
    if (!args)
        return " -";
    std::string s;
    int count = GetScriptArgCount(opcode);
    if (opcode == OP_BlockBegin)
        count = 1;
    for (int i = 0; i < count; ++i)
        s += F(" %d", args[i]);
    return s;
}

// The original's expression at `e` (its layout: the port's offsets).
void OrigExpr(std::vector<std::string> &out, uint32_t e, const std::string &indent)
{
    const void *p = reinterpret_cast<void *>(e);
    std::string s = indent + "expr ops";
    for (int i = 0; i < 4; ++i)
        s += F(" %d/%d", At<int>(p, 4 * i), At<int>(p, 0x10 + 4 * i));
    s += F(" fn %d %d %d depth %d", int(At<uint8_t>(p, 0x20)), int(At<uint8_t>(p, 0x21)), At<int>(p, 0x24), At<int>(p, 0x58));
    s += " operators";
    for (uint32_t q : OrigVec(p, 0x38, 4))
        s += F(" %d", At<int>(reinterpret_cast<void *>(q), 0));
    s += F(" tokens %u", unsigned(OrigVec(p, 0x48, 12).size()));
    out.push_back(s);
    for (uint32_t q : OrigVec(p, 0x28, 4))
        OrigExpr(out, At<uint32_t>(reinterpret_cast<void *>(q), 0), indent + "  ");
}
void PortExpr(std::vector<std::string> &out, const C_ScriptExpression *x, const std::string &indent)
{
    std::string s = indent + "expr ops";
    for (int i = 0; i < 4; ++i)
        s += F(" %d/%d", x->m_operands[i], x->m_operandTypes[i]);
    s += F(" fn %d %d %d depth %d", int(x->m_isFunction), int(x->m_negate), x->m_function, x->m_parenDepth);
    s += " operators";
    for (int o : x->m_operators)
        s += F(" %d", o);
    s += F(" tokens %u", unsigned(x->m_tokens.size()));
    out.push_back(s);
    for (const C_ScriptExpression *c : x->m_children)
        PortExpr(out, c, indent + "  ");
}
// Both sides' trees walked together: a node the original deleted while its parent kept it (GroupRange's use after
// free; its destructor emptied it) is marked on both sides and not compared further (the port keeps it whole).
int g_freedNodes;
bool OrigFreed(uint32_t e) { return std::binary_search(g_heldFrees.begin(), g_heldFrees.end(), reinterpret_cast<void *>(e)); }
void PairExpr(std::vector<std::string> &o, std::vector<std::string> &p, uint32_t oe, const C_ScriptExpression *pe,
              const std::string &indent)
{
    if (OrigFreed(oe)) {
        ++g_freedNodes;
        o.push_back(indent + "expr freed by the original");
        p.push_back(indent + "expr freed by the original");
        return;
    }
    std::vector<std::string> ol, pl;
    OrigExpr(ol, oe, indent);
    PortExpr(pl, pe, indent);
    o.push_back(ol[0]);
    p.push_back(pl[0]);
    const std::vector<uint32_t> oc = OrigVec(reinterpret_cast<void *>(oe), 0x28, 4);
    for (size_t i = 0; i < std::max(oc.size(), pe->m_children.size()); ++i) {
        const uint32_t oci = i < oc.size() ? At<uint32_t>(reinterpret_cast<void *>(oc[i]), 0) : 0;
        if (i < oc.size() && i < pe->m_children.size())
            PairExpr(o, p, oci, pe->m_children[i], indent + "  ");
        else if (i < oc.size())
            OrigExpr(o, oci, indent + "  ");
        else
            PortExpr(p, pe->m_children[i], indent + "  ");
    }
}
void PairExprs(std::vector<std::string> &o, std::vector<std::string> &p, const uint8_t *b, const C_Script &s)
{
    std::sort(g_heldFrees.begin(), g_heldFrees.end());
    const std::vector<uint32_t> oe = OrigVec(b, 0x6C, 4);
    for (size_t i = 0; i < std::max(oe.size(), s.m_expressions.size()); ++i) {
        const uint32_t e = i < oe.size() ? At<uint32_t>(reinterpret_cast<void *>(oe[i]), 0) : 0;
        if (i < oe.size() && i < s.m_expressions.size())
            PairExpr(o, p, e, s.m_expressions[i], "");
        else if (i < oe.size())
            OrigExpr(o, e, "");
        else
            PortExpr(p, s.m_expressions[i], "");
    }
}

// The original's script (block `b`) and compiler (0x008993f0).
std::vector<std::string> OrigDump(const uint8_t *b, bool result)
{
    std::vector<std::string> out;
    out.push_back(F("result %d", int(result)));
    out.push_back(F("start %d file %s handlers", At<int>(b, 0x1C), OrigStr(At<uint32_t>(b, 0xCC)).c_str()));
    for (uint32_t off = 0xDC; off <= 0xF8; off += 4)
        out.back() += F(" %d", At<int>(b, off));
    for (uint32_t q : OrigVec(b, 0x5C, 4)) {
        const uint32_t c = At<uint32_t>(reinterpret_cast<void *>(q), 0);
        if (!c) {
            out.push_back("cmd null");
            continue;
        }
        const int opcode = At<int>(reinterpret_cast<void *>(c), 0);
        out.push_back(F("cmd %d", opcode) + Args(opcode, reinterpret_cast<const int *>(At<uint32_t>(reinterpret_cast<void *>(c), 4))));
    }
    for (uint32_t q : OrigVec(b, 0x9C, 8))
        out.push_back(F("number %d %d", At<int>(reinterpret_cast<void *>(q), 0), int(At<uint8_t>(reinterpret_cast<void *>(q), 4))));
    for (uint32_t q : OrigVec(b, 0xAC, 4))
        out.push_back("string " + OrigStr(At<uint32_t>(reinterpret_cast<void *>(q), 0)));
    out.push_back(F("frames %u", unsigned(OrigVec(b, 0x8C, 4).size())));
    for (uint32_t q : OrigVec(b, 0x7C, 4)) {
        const void *w = reinterpret_cast<void *>(At<uint32_t>(reinterpret_cast<void *>(q), 0));
        out.push_back(F("whenever %d %d %d %d %d", At<int>(w, 0), At<int>(w, 4), int(At<uint8_t>(w, 8)), At<int>(w, 12), At<int>(w, 16)));
    }
    const char *kinds[3] = {"signal", "cutscene", "cutsceneDone"};
    for (int k = 0; k < 3; ++k)
        for (uint32_t q : OrigVec(b, 0x2C + 0x10 * k, 4)) {
            const void *h = reinterpret_cast<void *>(At<uint32_t>(reinterpret_cast<void *>(q), 0));
            out.push_back(F("%s %d %d", kinds[k], At<int>(h, 0), At<int>(h, 4)));
        }
    // (The expressions: PairExprs.)
    const uint8_t *c = OriginalAt<const uint8_t *>(0x008993f0);
    // (The line counters, +0x408 / +0x40C, not compared: written only, and the port's include handling counts
    // differently.)
    out.push_back(std::string("compiler token ") + "\"" +
                  std::string(reinterpret_cast<const char *>(c + 8)) + "\"");
    for (uint32_t q : OrigVec(c, 0x410, 12))
        out.push_back("var " + OrigStr(At<uint32_t>(reinterpret_cast<void *>(q), 0)) +
                      F(" %d %d", At<int>(reinterpret_cast<void *>(q), 4), At<int>(reinterpret_cast<void *>(q), 8)));
    for (uint32_t q : OrigVec(c, 0x420, 8))
        out.push_back("label " + OrigStr(At<uint32_t>(reinterpret_cast<void *>(q), 0)) + F(" %d", At<int>(reinterpret_cast<void *>(q), 4)));
    for (uint32_t q : OrigVec(c, 0x430, 4))
        out.push_back(F("open %d", At<int>(reinterpret_cast<void *>(q), 0)));
    return out;
}
std::vector<std::string> PortDump(const C_Script &s, bool result)
{
    std::vector<std::string> out;
    out.push_back(F("result %d", int(result)));
    out.push_back(F("start %d file %s handlers", s.m_startPoint, PortStr(s.m_fileName).c_str()));
    for (int h : {s.m_onDeath, s.m_onUse, s.m_onHit, s.m_onAlarm, s.m_onAlarmDone, s.m_unusedHandler, s.m_onGlobalAlarm, s.m_onExit})
        out.back() += F(" %d", h);
    for (const S_ScriptCommand *c : s.m_commands) {
        if (!c) {
            out.push_back("cmd null");
            continue;
        }
        out.push_back(F("cmd %d", c->opcode) + Args(c->opcode, c->args));
    }
    for (const S_ScriptNumber &n : s.m_numbers)
        out.push_back(F("number %d %d", n.value, int(n.isConstant)));
    for (const C_str &t : s.m_strings)
        out.push_back("string " + PortStr(t));
    out.push_back(F("frames %u", unsigned(s.m_frames.size())));
    for (const S_ScriptWhenever *w : s.m_whenevers)
        out.push_back(F("whenever %d %d %d %d %d", w->expression, w->label, int(w->enabled), w->period, w->elapsed));
    const char *kinds[3] = {"signal", "cutscene", "cutsceneDone"};
    const std::vector<S_ScriptHandler *> *lists[3] = {&s.m_signalHandlers, &s.m_cutsceneHandlers, &s.m_cutsceneDoneHandlers};
    for (int k = 0; k < 3; ++k)
        for (const S_ScriptHandler *h : *lists[k])
            out.push_back(F("%s %d %d", kinds[k], h->id, h->label));
    const C_ScriptCompiler &c = g_scriptCompiler;
    out.push_back(std::string("compiler token ") + "\"" + std::string(c.m_token) + "\"");
    for (const S_ScriptVariable &v : c.m_variables)
        out.push_back("var " + PortStr(v.name) + F(" %d %d", v.type, v.index));
    for (const S_ScriptLabel &l : c.m_labels)
        out.push_back("label " + PortStr(l.name) + F(" %d", l.index));
    for (int o : c.m_openBlocks)
        out.push_back(F("open %d", o));
    return out;
}

// ---- one compilation on both sides ----
using T_Ctor = void *(TC *)(void *, void *, int);
using T_Compile = bool(TC *)(void *, const char *, void *);
using T_Void = void(TC *)(void *);
struct S_Result {
    std::vector<std::string> orig, port;
    std::vector<std::string> openedOrig, openedPort;
};
// `pre`: a file compiled into the same script first (the second compilation clears it).
S_Result CompileBoth(const std::string &path, const char *pre = nullptr)
{
    // HD2_SC_TRACE: each compilation named first (for a hang).
    static const bool trace = std::getenv("HD2_SC_TRACE") != nullptr;
    if (trace)
        std::printf("compile %s\n", g_current.c_str());
    S_Result r;
    // The original's.
    g_case = "original";
    g_opened.clear();
    uint8_t *block = static_cast<uint8_t *>(std::calloc(1, 0x200));
    OriginalAt<T_Ctor>(0x0041dcb0)(block, nullptr, 1);
    g_holdFrees = true;
    if (pre)
        OriginalAt<T_Compile>(0x0042c850)(OriginalAt<void *>(0x008993f0), pre, block);
    const bool ro = OriginalAt<T_Compile>(0x0042c850)(OriginalAt<void *>(0x008993f0), path.c_str(), block);
    r.orig = OrigDump(block, ro);
    r.openedOrig = g_opened;
    // The port's.
    g_case = "port";
    g_opened.clear();
    {
        C_Script script(nullptr, true);
        if (pre)
            g_scriptCompiler.Compile(pre, &script);
        const bool rp = g_scriptCompiler.Compile(path.c_str(), &script);
        r.port = PortDump(script, rp);
        r.port.push_back("expressions");
        r.orig.push_back("expressions");
        PairExprs(r.orig, r.port, block, script);
    }
    r.openedPort = g_opened;
    // (The original's Clear deletes the freed node again: still held, and each block freed once.)
    g_case = "original";
    OriginalAt<T_Void>(0x0041e140)(block);
    ReleaseHeldFrees();
    std::free(block);
    g_case = "";
    return r;
}

int g_dump;
bool Compare(const std::string &what, const S_Result &r)
{
    ++checks;
    if (!g_overrun.empty()) {
        // The original wrote past a block in this case (GroupRange at a level with more operators than operands: its
        // list's end): its tree is undefined, not compared. (The guard bytes took the write.)
        ++g_overruns;
        if (g_dump)
            std::printf("skipped (the original wrote past a block): %s\n", g_overrun.c_str());
        g_overrun.clear();
        return true;
    }
    std::vector<std::string> o = r.orig, p = r.port;
    o.push_back("opened");
    p.push_back("opened");
    for (const std::string &s : r.openedOrig)
        o.push_back("open " + Lower(s));
    for (const std::string &s : r.openedPort)
        p.push_back("open " + Lower(s));
    size_t k = 0;
    while (k < o.size() && k < p.size() && o[k] == p[k])
        ++k;
    if (k == o.size() && k == p.size())
        return true;
    ++failures;
    if (failures <= 40 || g_dump) {
        std::printf("FAIL %s line %u of %u / %u:\n  orig %s\n  port %s\n", what.c_str(), unsigned(k), unsigned(o.size()),
                    unsigned(p.size()), k < o.size() ? o[k].c_str() : "(end)", k < p.size() ? p[k].c_str() : "(end)");
        if (g_dump)
            for (size_t i = 0; i < std::max(o.size(), p.size()); ++i)
                std::printf("    %-60s | %s\n", i < o.size() ? o[i].c_str() : "-", i < p.size() ? p[i].c_str() : "-");
    }
    return false;
}

// ---- the corpus ----
struct S_File {
    std::string mission;                                // the folder under Scripts (the mission's name)
    std::string path;                                   // "Scripts\<mission>\<file>"
};
void Walk(const std::string &dir, const std::string &rel, std::vector<std::string> &out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        const std::string name = fd.cFileName;
        if (name == "." || name == "..")
            continue;
        const std::string r = rel.empty() ? name : rel + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            Walk(dir + "\\" + name, r, out);
        else if (name.size() > 4 && Lower(name.substr(name.size() - 4)) == ".scr")
            out.push_back(r);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
std::vector<S_File> Corpus()
{
    std::vector<std::string> rel;
    Walk(g_data + "\\SCRIPTS", "", rel);
    std::sort(rel.begin(), rel.end());
    std::vector<S_File> files;
    for (const std::string &r : rel) {
        const size_t slash = r.find('\\');
        if (slash == std::string::npos)
            continue;                                   // (a script directly under Scripts: no mission)
        files.push_back(S_File{r.substr(0, slash), "Scripts\\" + r});
    }
    return files;
}

// ---- the mutations: a copy of the file with a few tokens changed ----
const char *const kVocabulary[] = {
    "{", "}", "(", ")", ";", ",", ":", "=", "==", "<", ">", "<=", ">=", "!=", "<>", "!", "+", "-", "*", "/", "&&",
    "||", "and", "or", "not", "If", "Else", "Whenever", "Label", "GoTo", "Gosub", "Return", "Block", "Integer",
    "String", "Frame", "Bool", "Enum", "ScriptStartPoint", "OnSignal", "OnCutscene", "OnCutsceneDone", "OnDeath",
    "OnUse", "OnHit", "OnAlarm", "OnAlarmDone", "OnGlobalAlarm", "OnExit", "SetWhenever", "SetWheneverTime", "0",
    "1", "-1", "12", "3.5", "1.", "..", "99999999999", "\"\"", "\"a%%b\"", "\"text\"", "\"%\"", "\"%B\"", "\"%5B\"",
    "%", "true", "false", "TRUE", "x", "count", "_PlayerInRange", "_RandomInt", "_SignalReceived", "Delay",
    "EndScript", "FRM_SetOn", "\"unterminated", "//", "// comment", "\r\n", "\t", " ", "@", "$",
    // Whole statements.
    "ScriptStartPoint:", "ScriptStartPoint :", "Label sc_l:", "GoTo sc_l;", "Gosub sc_l;", "Return;", "Block {",
    "SetWhenever(sc_w, 1);", "SetWheneverTime(sc_w, 300);", "Whenever sc_w (1 == 1)", "Integer sc_i = 3 * 4 + 2;",
    "String sc_s = \"a\";", "Frame sc_f;", "OnSignal(3)", "OnCutscene(2) {", "OnDeath() {", "Else {", "If (sc_i > -1) {",
};
// (Not in the vocabulary or the edits: "#include" and its lines (the port's include handling is its own, see
// script_compiler.cpp), and real printf conversions in a literal or name ("%d": the original reads a missing argument).)
std::vector<std::pair<size_t, size_t>> Tokens(const std::string &t)
{
    // Rough token spans: names, numbers, strings, single characters; none on a line with "#include".
    std::vector<std::pair<size_t, size_t>> spans;
    std::vector<std::pair<size_t, size_t>> includeLines;
    for (size_t at = t.find("#include"); at != std::string::npos; at = t.find("#include", at + 1)) {
        const size_t from = t.find_last_of("\r\n", at);
        includeLines.push_back({from == std::string::npos ? 0 : from + 1, std::min(t.find_first_of("\r\n", at), t.size())});
    }
    size_t i = 0;
    while (i < t.size()) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (std::isspace(c)) {
            ++i;
            continue;
        }
        size_t j = i + 1;
        if (std::isalnum(c) || c == '_' || c == '.') {
            while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '_' || t[j] == '.'))
                ++j;
        } else if (c == '"') {
            while (j < t.size() && t[j] != '"' && t[j] != '\r' && t[j] != '\n')
                ++j;
            if (j < t.size() && t[j] == '"')
                ++j;
        }
        bool onInclude = false;
        for (const auto &line : includeLines)
            onInclude |= i >= line.first && i <= line.second;
        if (!onInclude)
            spans.push_back({i, j - i});
        i = j;
    }
    return spans;
}
std::string MutateOnce(const std::string &text);
// (No line of 1000 characters or more: the original copies a token into its 0x400-byte buffer unbounded and
// overwrites the compiler's tables; the longest shipped line has 526.)
std::string Mutate(const std::string &text)
{
    for (;;) {
        const std::string t = MutateOnce(text);
        size_t longest = 0, start = 0;
        for (size_t i = 0; i <= t.size(); ++i)
            if (i == t.size() || t[i] == '\n' || t[i] == '\r') {
                longest = std::max(longest, i - start);
                start = i + 1;
            }
        if (longest < 1000)
            return t;
    }
}
std::string MutateOnce(const std::string &text)
{
    std::string t = text;
    const int edits = P(0.6) ? 1 : I(2, 4);
    for (int e = 0; e < edits; ++e) {
        const auto spans = Tokens(t);
        if (spans.empty())
            break;
        const auto sp = spans[size_t(I(0, int(spans.size()) - 1))];
        const std::string word = kVocabulary[I(0, int(sizeof kVocabulary / sizeof *kVocabulary) - 1)];
        switch (I(0, 5)) {
        case 0:                                         // delete a token
            t.erase(sp.first, sp.second);
            break;
        case 1:                                         // replace it
            t.replace(sp.first, sp.second, word);
            break;
        case 2:                                         // insert a word before it
            t.insert(sp.first, word + " ");
            break;
        case 3:                                         // duplicate it
            t.insert(sp.first, t.substr(sp.first, sp.second) + " ");
            break;
        case 4:                                         // cut the file there
            t.resize(sp.first + (P(0.5) ? sp.second : 0));
            break;
        default: {                                      // swap it with another token
            const auto sp2 = spans[size_t(I(0, int(spans.size()) - 1))];
            if (sp2.first > sp.first + sp.second) {
                const std::string a = t.substr(sp.first, sp.second), b = t.substr(sp2.first, sp2.second);
                t.replace(sp2.first, sp2.second, a);
                t.replace(sp.first, sp.second, b);
            }
            break;
        }
        }
    }
    return t;
}

void SetUp()
{
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e53a4, reinterpret_cast<const void *>(O_dtaOpen)},
        {0x007e539e, reinterpret_cast<const void *>(O_dtaRead)},
        {0x007e53b0, reinterpret_cast<const void *>(O_dtaClose)},
        {0x007e566f, reinterpret_cast<const void *>(O_New)},
        {0x007e2120, reinterpret_cast<const void *>(O_Delete)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // The original's compiler: its constructor (the static initialiser never ran).
    OriginalAt<T_Void>(0x0042cb80)(OriginalAt<void *>(0x008993f0));
}

void SetRole(uint32_t role)
{
    g_gameRole = role;
    *OriginalAt<uint32_t *>(0x00982100) = role;
}

}  // namespace H

// The port's side of the fakes.
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGameBuf); }
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }

#include "sc_stubs.inc"

int main()
{
    using namespace H;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("sc_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    if (const char *d = std::getenv("HD2_UNPACKED"))
        g_data = d;
    if (const char *d = std::getenv("HD2_SC_DATA"))
        g_data = d;
    g_dump = std::getenv("HD2_SC_DUMP") != nullptr;
    if (const char *s = std::getenv("HD2_SC_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    int mutants = std::getenv("HD2_SC_QUICK") ? 2 : 12;
    if (const char *m = std::getenv("HD2_SC_MUTANTS"))
        mutants = std::atoi(m);
    const char *only = std::getenv("HD2_SC_ONLY");
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    // The string literals: ScriptStringLiteral against the original's sprintf (no arguments) on every byte after a '%'
    // and on random strings of the characters its state machine knows (no conversion that reads an argument, no '*').
    {
        g_case = "literals";
        using T_Sprintf = int(__cdecl *)(char *, const char *, ...);
        auto one = [](const std::string &format) {
            char out[256] = {};
            OriginalAt<T_Sprintf>(0x007e6334)(out, format.c_str());
            const C_str port = ScriptStringLiteral(format.c_str(), format.size());
            ++checks;
            if (std::string(port.c_str(), port.Size()) != out) {
                ++failures;
                if (failures <= 40)
                    std::printf("FAIL literal \"%s\": orig \"%s\" port \"%s\"\n", format.c_str(), out, port.c_str());
            }
        };
        for (int x = 1; x < 256; ++x)
            if (!std::strchr("cCdeEfgGinopsSuxXZ*", x))
                one(std::string("a%") + char(x) + "b");
        // (Flags carried into the next '%' and combined, a size at the start.)
        for (const char *s : {"%0B%5B", "%-05B", "%0-5B", "%I64B", "%I6B", "%0%5B", "%05B%3B", "%-3B%03B", "%h5B"})
            one(s);
        static const char alphabet[] = "%%%%-+ #0123456789.hlwLFNIB6 4abAD_qrtvyzjkm:,\t\x80\xe9";
        const int literals = std::getenv("HD2_SC_QUICK") ? 20000 : 100000;
        for (int n = 0; n < literals; ++n) {
            std::string s;
            int digits = 0;
            for (int k = I(0, 12); k > 0; --k) {
                const char c = alphabet[I(0, int(sizeof alphabet) - 2)];
                // (At most two digits in a row: a width stays under the 256-byte buffer.)
                digits = c >= '0' && c <= '9' ? digits + 1 : 0;
                if (digits <= 2)
                    s += c;
            }
            one(s);
        }
        g_case = "";
    }

    const char *saveFails = std::getenv("HD2_SC_SAVE_FAILS");
    if (const char *replay = std::getenv("HD2_SC_REPLAY")) {
        // A saved failing mutant: its first line the script's path, the rest its text.
        std::string all;
        FILE *in = std::fopen(replay, "rb");
        if (!in) {
            std::printf("sc_diff: no %s\n", replay);
            return 1;
        }
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
            all.append(buf, n);
        std::fclose(in);
        const size_t eol = all.find('\n');
        const std::string path = all.substr(0, eol);
        SetRole(GAME_ROLE_LOCAL);
        SetMission(path.substr(8, path.find('\\', 8) - 8));
        g_overrides[Lower(path)] = all.substr(eol + 1);
        g_current = path + " (replay)";
        g_dump = 1;
        Compare(g_current, CompileBoth(path));
        return 0;
    }
    if (const char *seq = std::getenv("HD2_SC_SEQ")) {
        // A file of statements, one per line: each compiled in turn after the random expressions' declarations.
        SetRole(GAME_ROLE_LOCAL);
        SetMission("AFRICA1");
        std::ifstream in(seq);
        std::string line;
        const std::string path = "Scripts\\AFRICA1\\sc_x.scr";
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            g_overrides[Lower(path)] = "Integer a = 2;\r\nInteger b;\r\nString s;\r\nFrame f;\r\n" + line + "\r\n";
            g_current = line;
            Compare(line, CompileBoth(path));
        }
        std::printf("sc_diff: sequence done, %d failures\n", failures);
        return 0;
    }
    if (const char *b = std::getenv("HD2_SC_BISECT")) {
        // The file cut after each line: the first cut whose dumps differ, and the line it ends with.
        const std::string path = b;
        SetRole(GAME_ROLE_LOCAL);
        SetMission(path.substr(8, path.find('\\', 8) - 8));
        std::string text;
        FileText(path, text);
        for (size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 1)) {
            g_overrides[Lower(path)] = text.substr(0, at + 1);
            const S_Result r = CompileBoth(path);
            if (r.orig != r.port) {
                const size_t from = text.rfind('\n', at - 1);
                std::printf("first difference at byte %u: %s\n", unsigned(at),
                            text.substr(from == std::string::npos ? 0 : from + 1, at - from).c_str());
                Compare(path, r);
                return 0;
            }
        }
        std::printf("no difference in any cut\n");
        return 0;
    }
    const std::vector<S_File> files = Corpus();
    if (files.empty()) {
        std::printf("sc_diff: no scripts under %s\\SCRIPTS\n", g_data.c_str());
        return 1;
    }
    // Crafted cases: a missing file, an include found only in Scripts\include, a missing include, start points.
    {
        SetRole(GAME_ROLE_LOCAL);
        SetMission("AFRICA1");
        const struct {
            const char *path, *text;
        } crafted[] = {
            {"Scripts\\AFRICA1\\sc_none.scr", nullptr},
            {"Scripts\\AFRICA1\\sc_inc.scr", "#include \"sc_only.scr\"\r\nInteger a = b + 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_inc2.scr", "#include \"sc_missing.scr\"\r\nInteger a;\r\n"},
            {"Scripts\\AFRICA1\\sc_start.scr", "Integer a;\r\nScriptStartPoint:\r\na = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_start2.scr", "Integer a;\r\nScriptStartPoint:\r\na = 1;\r\nScriptStartPoint:\r\n"},
            {"Scripts\\AFRICA1\\sc_start3.scr", "ScriptStartPoint;\r\n"},
            {"Scripts\\AFRICA1\\sc_empty.scr", ""},
            // Include directives both read alike: indented, spaced, no quotes, unterminated, an empty name, a lone
            // quote at the line's end, a later "#include" inside a comment.
            {"Scripts\\AFRICA1\\sc_i1.scr", " #include \"sc_only.scr\"\r\nInteger a = b + 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i2.scr", "\t \t#include \t \"sc_only.scr\"\r\nInteger a = b + 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i3.scr", "#include sc_only.scr\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i4.scr", "#include \"sc_only.scr\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i5.scr", "#include \"\"\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i6.scr", "#include \"\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i7.scr", "#include \"sc_only.scr\"\r\nInteger x; // #include \"sc_only.scr\"\r\n"},
            {"Scripts\\AFRICA1\\sc_i8.scr", "Integer a;\r\nFRM_SetOn(\"x#include\", 1);\r\n"},
            {"Scripts\\AFRICA1\\sc_i9.scr", "#include\"sc_only.scr\"\r\nInteger a = 1;\r\n"},
            {"Scripts\\AFRICA1\\sc_i10.scr", "#include  \"sc_only.scr\"\r\nInteger a = b;\r\n"},
            // (Not "#include" at the very end of the file: the original's search restarts there for ever.)
            // Else: first, not after a '}', last; handlers followed by no block.
            {"Scripts\\AFRICA1\\sc_e1.scr", "Else { }\r\n"},
            {"Scripts\\AFRICA1\\sc_e2.scr", "Integer a;\r\nElse { }\r\n"},
            {"Scripts\\AFRICA1\\sc_e3.scr", "If (1) { }\r\nElse\r\n"},
            {"Scripts\\AFRICA1\\sc_e4.scr", "If (1) { }\r\nElse\r\nInteger a;\r\n"},
            // A label defined twice; "#include" right after the file's first character; a control character.
            {"Scripts\\AFRICA1\\sc_l1.scr", "Label l:\r\nLabel l:\r\nGoTo l;\r\n"},
            {"Scripts\\AFRICA1\\sc_i11.scr", "x#include \"sc_only.scr\"\r\nInteger a = b;\r\n"},
            {"Scripts\\AFRICA1\\sc_c1.scr", "Integer a;\x01\r\nInteger b;\r\n"},
            {"Scripts\\AFRICA1\\sc_i12.scr", "#include \""},
            {"Scripts\\AFRICA1\\sc_i13.scr", "#include \"sc_only.scr"},
            {"Scripts\\AFRICA1\\sc_s1.scr", "Integer a;\r\nString s = \"abc"},
        };
        g_overrides[Lower("Scripts\\include\\sc_only.scr")] = "Integer b = 2;\r\n";
        for (const auto &c : crafted) {
            if (c.text)
                g_overrides[Lower(c.path)] = c.text;
            g_current = c.path;
            Compare(c.path, CompileBoth(c.path));
            g_overrides.erase(Lower(c.path));
        }
        // A NUL byte in the file and in an include (the text after it never read).
        {
            const std::string withNul("Integer a = 1;\r\n\0Integer b;\r\n", 29);
            g_overrides[Lower("Scripts\\AFRICA1\\sc_n1.scr")] = withNul;
            g_current = "Scripts\\AFRICA1\\sc_n1.scr";
            Compare(g_current, CompileBoth(g_current));
            g_overrides[Lower("Scripts\\include\\sc_nul.scr")] = std::string("Integer b = 2;\r\n\0Integer c;\r\n", 29);
            g_overrides[Lower("Scripts\\AFRICA1\\sc_n2.scr")] = "#include \"sc_nul.scr\"\r\nInteger a = b;\r\nc = 1;\r\n";
            g_current = "Scripts\\AFRICA1\\sc_n2.scr";
            Compare(g_current, CompileBoth(g_current));
        }
        for (const char *handler : {"OnDeath()", "OnUse()", "OnHit()", "OnAlarm()", "OnAlarmDone()", "OnGlobalAlarm()",
                                    "OnExit()", "OnSignal(1)", "OnCutscene(1)", "OnCutsceneDone(1)", "Whenever w (1)"}) {
            for (const char *after : {"\r\nInteger a;\r\n", "\r\n", " { }\r\n", " { }\r\nElse { }\r\n"}) {
                const std::string path = "Scripts\\AFRICA1\\sc_h.scr";
                g_overrides[Lower(path)] = std::string(handler) + after;
                g_current = path + " " + handler + after;
                Compare(g_current, CompileBoth(path));
            }
        }
        // Compiled into a script that already holds another (the second compilation clears it first).
        g_overrides[Lower("Scripts\\AFRICA1\\sc_a.scr")] =
            "Integer a = 3;\r\nString s = \"x\";\r\nFrame f;\r\nOnSignal(2) { }\r\nOnCutscene(4) { }\r\nOnCutsceneDone(5) { }\r\n"
            "Whenever w (a > 1) { }\r\nOnDeath() { }\r\nLabel l:\r\nGoTo l;\r\n";
        g_overrides[Lower("Scripts\\AFRICA1\\sc_b.scr")] = "Integer b = 1;\r\nOnSignal(2) { }\r\nOnUse() { }\r\n";
        g_current = "Scripts\\AFRICA1\\sc_b.scr (after sc_a)";
        Compare(g_current, CompileBoth("Scripts\\AFRICA1\\sc_b.scr", "Scripts\\AFRICA1\\sc_a.scr"));
        g_overrides.clear();
    }
    // Random expressions from a grammar (in an If, an assignment, a Whenever): terms of each type and literals, every
    // operator (mixed precedence: the grouping, and the original's use after free), parentheses, NOT, a sign on a
    // literal, the built-in functions with right and wrong arguments. (Well formed: at a level with two operators more
    // than operands the original's list runs out of its bounds, and its damage shows in later cases; the mutated
    // scripts above bring the malformed ones.)
    {
        static const char *const operators[] = {"+", "-", "*", "/", "AND", "OR", "and", "==", "!=", "<>", "<", ">", "<=", ">="};
        static const char *const terms[] = {
            "a", "b", "0", "1", "7", "12", "3.9", "\"t\"", "\"%%\"", "s", "f", "_RandomInt(5)", "_SignalReceived(a)",
            "_GetNumLiveEnemies()", "_EnemyInRange(f, 10)", "_EnemyInRange(f, -2)", "_GetObjectiveStatus(1, 2, 3, 4)",
            "_IsLanguageVersion(\"cz\")", "_FrameInRange(a)", "_SPGetGameDifficulty()", "_RandomInt(s)",
        };
        std::function<std::string(int)> expression = [&](int depth) {
            std::string e;
            bool signed_ = false;                       // (one sign per level: one operator more than operands)
            for (int n = I(1, 4), t = 0; t < n; ++t) {
                if (t)
                    e += std::string(" ") + operators[I(0, int(sizeof operators / sizeof *operators) - 1)] + " ";
                if (P(0.1))
                    e += P(0.5) ? "NOT " : "! ";
                const int kind = I(0, 9);
                if (kind == 0 && depth < 3)
                    e += "(" + expression(depth + 1) + ")";
                else if (kind == 1 && !signed_) {
                    signed_ = true;
                    e += std::string("- ") + (P(0.5) ? "1" : "4");
                } else
                    e += terms[I(0, int(sizeof terms / sizeof *terms) - 1)];
            }
            return e;
        };
        const int count = std::getenv("HD2_SC_QUICK") ? 6000 : 30000;
        const std::string path = "Scripts\\AFRICA1\\sc_x.scr";
        for (int n = 0; n < count; ++n) {
            const std::string expr = expression(0);
            std::string text = "Integer a = 2;\r\nInteger b;\r\nString s;\r\nFrame f;\r\n";
            switch (I(0, 2)) {
            case 0: text += "If (" + expr + ") { b = 1; }\r\n"; break;
            case 1: text += "b = " + expr + ";\r\n"; break;
            default: text += "Whenever w (" + expr + ") { }\r\n"; break;
            }
            g_overrides[Lower(path)] = text;
            g_current = path + " [" + text.substr(text.find("Frame f;") + 10) + "]";
            if (!Compare(g_current, CompileBoth(path)) && saveFails) {
                if (FILE *out = std::fopen((std::string(saveFails) + F("\\%04d.scr", failures)).c_str(), "wb")) {
                    std::fprintf(out, "%s\n%s", path.c_str(), text.c_str());
                    std::fclose(out);
                }
            }
        }
        g_overrides.clear();
    }
    int compiled = 0, failed = 0;
    for (const S_File &f : files) {
        if (only && Lower(f.path).find(Lower(only)) == std::string::npos)
            continue;
        SetMission(f.mission);
        g_current = f.path;
        // The file as shipped. (Only on a server: on a client the original's AddCommand answers -1 and its '}' reads the
        // command before the list, so no client ever compiles one.)
        SetRole(GAME_ROLE_LOCAL);
        const S_Result r = CompileBoth(f.path);
        Compare(f.path, r);
        (r.port[0] == "result 1" ? compiled : failed)++;
        // Mutated copies.
        std::string text;
        if (!FileText(f.path, text))
            continue;
        for (int m = 0; m < mutants; ++m) {
            const std::string key = Lower(f.path);
            g_overrides[key] = Mutate(text);
            g_current = f.path + F(" (mutant %d)", m);
            if (saveFails) {
                // (The one being compiled, for a crash.)
                if (FILE *out = std::fopen((std::string(saveFails) + "\\current.scr").c_str(), "wb")) {
                    std::fprintf(out, "%s\n", f.path.c_str());
                    std::fwrite(g_overrides[key].data(), 1, g_overrides[key].size(), out);
                    std::fclose(out);
                }
            }
            if (!Compare(g_current, CompileBoth(f.path)) && saveFails) {
                // (Kept for HD2_SC_REPLAY: the mutant's text, named by its failure's number.)
                const std::string name = std::string(saveFails) + F("\\%04d.scr", failures);
                if (FILE *out = std::fopen(name.c_str(), "wb")) {
                    std::fprintf(out, "%s\n", f.path.c_str());
                    std::fwrite(g_overrides[key].data(), 1, g_overrides[key].size(), out);
                    std::fclose(out);
                }
            }
            g_overrides.erase(key);
        }
    }
    std::printf("sc_diff: %d scripts (%d compile, %d fail in the port), %d expression nodes the original freed, %d cases it wrote past a block in (not compared), %d checks, "
                "%d failures\n", compiled + failed, compiled, failed, g_freedNodes, g_overruns, checks, failures);
    return failures != 0;
}
