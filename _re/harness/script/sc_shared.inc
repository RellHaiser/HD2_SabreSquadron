// Script harnesses: the infrastructure sc_diff.cpp and vm_diff.cpp share (the files and dta functions, the
// original's operator new / delete with guard bytes, the game's mission name, both sides' script dumps,
// CompileBoth, Compare, SetUp). Define HARNESS_NAME first.
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
    std::printf(HARNESS_NAME ": exception %08lx at %p (linked %08x) [%s %s]\n", e->ExceptionRecord->ExceptionCode,
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
    std::string writeKey;                               // (a file opened for writing: kept in g_overrides when closed)
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
    auto it = g_open.find(h);
    if (it != g_open.end() && !it->second.writeKey.empty())
        g_overrides[it->second.writeKey] = it->second.text;
    return g_open.erase(h) != 0;
}
// Writing (vm_diff's saved games): an empty file in memory, kept under its name when closed.
int FileOpenWrite(const char *name)
{
    g_opened.push_back(std::string("(write) ") + name);
    S_Open o;
    o.writeKey = Lower(name);
    g_open[g_nextHandle] = o;
    return g_nextHandle++;
}
int FileSeek(int h, int offset, int origin)
{
    auto it = g_open.find(h);
    if (it == g_open.end())
        return -1;
    S_Open &o = it->second;
    const long long base = origin == 0 ? 0 : origin == 1 ? (long long)o.at : (long long)o.text.size();
    if (base + offset < 0)
        return -1;
    o.at = size_t(base + offset);
    return int(o.at);
}
int FileWrite(int h, const void *data, int size)
{
    auto it = g_open.find(h);
    if (it == g_open.end() || it->second.writeKey.empty())
        return -1;
    S_Open &o = it->second;
    if (o.at + size_t(size) > o.text.size())
        o.text.resize(o.at + size_t(size));
    std::memcpy(&o.text[o.at], data, size_t(size));
    o.at += size_t(size);
    return size;
}

int __stdcall O_dtaOpen(const char *name, int) { return FileOpen(name); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
int __stdcall O_dtaClose(int h) { return FileClose(h); }
int __stdcall O_dtaOpenWrite(const char *name, int) { return FileOpenWrite(name); }
int __stdcall O_dtaSeek(int h, int offset, int origin) { return FileSeek(h, offset, origin); }
int __stdcall O_dtaWrite(int h, const void *data, int size) { return FileWrite(h, data, size); }
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
