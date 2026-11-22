// Human part 78: the campaign tree (game/campaign.cpp) against the original's (0x006b5160 .. 0x006c0400). MinGW;
// build_campaign.bat; runs campaign_diff.exe ("campaign_diff: N checks, 0 failures"). campaign.cpp is compiled into this
// file; the chunks real on both sides (common/chunk.cpp, the original's own) over data files in memory (the port's
// rw_data functions, the original's imports redirected to them).
// Part 78a (the "Files" case): the campaign files (C_Campaign::LoadFiles: GameData\Gamedata%02d.gdt from 00 on): the
// shipped ones (HD2_UNPACKED, default H:\HD2_Unpacked: GameData\Gamedata00.gdt, 01) and files the scenario builds in the
// complete format (the version the campaign expects) and in the definition format (another version: the root, operation
// and mission parsers), with unknown chunks, missing ones, files the loading stops at (no version chunk, no root);
// each side's campaign (the original's laid out as its profile's constructor inlines it) loads them, is saved node by
// node (Save, SaveProgress: compared byte by byte), cleared, destroyed; the trees compared node by node after each
// operation (types, ids, texts, the leaves' fields, the containers' children and cursors, the missions' objectives).
// HD2_CAMPAIGN_ROUNDS: scenarios (default 4,000; HD2_CAMPAIGN_QUICK: 1,500); HD2_INTERFACE_SHOW, HD2_INTERFACE_ECHO,
// HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#define INTERFACE_FRAMES 8
#define protected public
#define private public
#include "game/campaign.cpp"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

#include "original_exe.h"

#include "../interface/interface_kit.inc"

// ---- the port's globals the campaign reads ----
alignas(16) static uint8_t g_playModeBlock[2][0x40], g_mpSettingsBlock[2][0x400];
static S_PlayMode *g_playModePointer = reinterpret_cast<S_PlayMode *>(g_playModeBlock[1]);
S_PlayMode *&g_playMode = g_playModePointer;
static S_MultiplayerSettings *g_mpSettingsPointer = reinterpret_cast<S_MultiplayerSettings *>(g_mpSettingsBlock[1]);
S_MultiplayerSettings *&g_mpSettings = g_mpSettingsPointer;

namespace H {

int g_side;
template <class T> void Put(void *b, uint32_t o, T v) { std::memcpy(static_cast<uint8_t *>(b) + o, &v, sizeof v); }
template <class T> T At(const void *b, uint32_t o)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(b) + o, sizeof v);
    return v;
}

// ---- the data files in memory (both sides) ----
std::map<std::string, std::string> g_files;
struct S_OpenFile {
    std::string name;
    size_t at = 0;
};
std::map<int, S_OpenFile> g_openFiles;
int g_nextFile = 1;
int FileOpen(const char *name, bool write)
{
    if (!write && !g_files.count(name)) {
        Log("dta open %s -> -1", name);
        return -1;
    }
    if (write)
        g_files[name].clear();
    g_openFiles[g_nextFile] = S_OpenFile{name, 0};
    Log("dta open %s %d -> %d", name, write, g_nextFile);
    return g_nextFile++;
}
int FileSeek(int handle, int offset, int origin)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    o.at = origin == 0 ? size_t(offset) : origin == 1 ? o.at + offset : f.size() + offset;
    return int(o.at);
}
int FileRead(int handle, void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    const int got = o.at >= f.size() ? 0 : std::max(0, std::min(int(f.size() - o.at), size));
    if (got)
        std::memcpy(data, f.data() + o.at, size_t(got));
    o.at += size_t(got);
    return got;
}
int FileWrite(int handle, const void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    std::string &f = g_files[o.name];
    if (o.at + size_t(size) > f.size())
        f.resize(o.at + size_t(size));
    std::memcpy(&f[o.at], data, size_t(size));
    o.at += size_t(size);
    return size;
}
bool FileClose(int handle)
{
    Log("dta close %d", handle);
    return g_openFiles.erase(handle) != 0;
}
int __stdcall O_dtaOpen(const char *name, bool) { return FileOpen(name, false); }
int __stdcall O_dtaOpenWrite(const char *name, int) { return FileOpen(name, true); }
int __stdcall O_dtaSeek(int h, int offset, int origin) { return FileSeek(h, offset, origin); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
int __stdcall O_dtaWrite(int h, const void *data, int size) { return FileWrite(h, data, size); }
bool __stdcall O_dtaClose(int h) { return FileClose(h); }
// The original's sprintf (its CRT's locks are not set up).
int __cdecl O_Sprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsprintf(buffer, format, args);
    va_end(args);
    return n;
}
void __cdecl O_DbgPrintf(const char *format, ...) { Log("dbgPrintf %s", format); }

}  // namespace H
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name, false); }
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpen(name, true); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }
extern "C" void dbgPrintf(const char *format, ...) { H::Log("dbgPrintf %s", format); }
// (Part 78b: the multiplayer map; none here yet.)
C_MpMap *S_MultiplayerSettings::GetCurrentMap()
{
    H::Log("mpSettings GetCurrentMap");
    return nullptr;
}

namespace H {

// ---- the campaigns: each side's ----
constexpr uint32_t ORIG_SIZE = 0xF4;
uint8_t *g_orig;
C_Campaign *g_port;
using T_V = void(__attribute__((thiscall)) *)(void *);
using T_Ctor = void *(__attribute__((thiscall)) *)(void *);
using T_RetB = bool(__attribute__((thiscall)) *)(void *);
using T_Chunk = bool(__attribute__((thiscall)) *)(void *, void *);
using T_ChunkOpen = bool(__attribute__((thiscall)) *)(void *, const char *, bool);
using T_ChunkClose = bool(__attribute__((thiscall)) *)(void *);
// The original's campaign as C_GameProfile's constructor lays it out (0x006b4d40): its vtable, the campaigns' vector, the
// multiplayer map's mission (its constructor 0x006b5940), the single mission (a C_MissionDef: vtable 0x00815a0c, type
// 3, its cursor and texts -1), the single mission's number -1, the current campaign 0, the version.
void BuildOrig(int version)
{
    g_orig = static_cast<uint8_t *>(std::calloc(1, ORIG_SIZE));
    Put<uint32_t>(g_orig, 0, 0x00815a00);
    OriginalAt<T_Ctor>(0x006b5940)(g_orig + 0x14);
    uint8_t *single = g_orig + 0x94;
    Put<uint32_t>(single, 0, 0x00815a0c);
    Put<int>(single, 4, CAMPAIGN_NODE_MISSION);
    Put<int>(single, 0x20, -1);
    for (int k = 0; k < 4; ++k)
        Put<int>(single, uint32_t(0x40 + 4 * k), -1);
    Put<int>(g_orig, 0xE4, -1);
    Put<int>(g_orig, 0xE8, 0);
    Put<int>(g_orig, 0xEC, version);
}

// ---- the trees, node by node ----
std::string Str(const C_str &s) { return std::string("'") + s.c_str() + "'"; }
std::string StrAt(const void *b, uint32_t o) { return Str(*reinterpret_cast<const C_str *>(static_cast<const uint8_t *>(b) + o)); }
// An MSVC6 vector's elements / a g++ one's.
std::vector<const uint8_t *> Elements(const void *b, uint32_t at, int side, uint32_t elem)
{
    const uint8_t *f = At<const uint8_t *>(b, at + (side ? 0 : 4)), *l = At<const uint8_t *>(b, at + (side ? 4 : 8));
    std::vector<const uint8_t *> out;
    for (const uint8_t *p = f; p && p < l; p += elem)
        out.push_back(p);
    return out;
}
#define PO(cls, f) uint32_t(offsetof(cls, f))
std::string Node(int side, const void *n, int depth);
std::string Children(int side, const void *n, int depth)
{
    std::string s = " cursor " + std::to_string(At<int>(n, side ? PO(C_CampaignContainer, m_cursor) : 0x20)) + " {";
    for (const uint8_t *e : Elements(n, side ? PO(C_CampaignContainer, m_children) : 0x10, side, 4))
        s += Node(side, At<const void *>(e, 0), depth + 1);
    return s + " }";
}
std::string Node(int side, const void *n, int depth)
{
    if (!n)
        return " null";
    if (depth > 12)
        return " (deep)";
    const int type = At<int>(n, 4);
    std::string s = " [" + std::to_string(type) + " id " + std::to_string(At<int>(n, 8)) + " " + StrAt(n, 0xC);
    switch (type) {
    case CAMPAIGN_NODE_VIDEO:
        s += " " + StrAt(n, side ? PO(C_CampaignVideo, m_fileName) : 0x10) + " " + StrAt(n, side ? PO(C_CampaignVideo, m_title) : 0x14) +
             " briefing " + std::to_string(At<uint8_t>(n, side ? PO(C_CampaignVideo, m_briefing) : 0x18));
        break;
    case CAMPAIGN_NODE_SCREEN: {
        s += " kind " + std::to_string(At<int>(n, side ? PO(C_CampaignScreen, m_kind) : 0x10)) + " back " +
             std::to_string(At<uint8_t>(n, side ? PO(C_CampaignScreen, m_backAcross) : 0x14)) + " u15 " +
             std::to_string(At<uint8_t>(n, side ? PO(C_CampaignScreen, m_unknown15) : 0x15)) + " items";
        for (const uint8_t *e : Elements(n, side ? PO(C_CampaignScreen, m_items) : 0x18, side, 16))
            s += " " + std::to_string(At<int>(e, 0)) + "/" + std::to_string(At<int>(e, 4)) + "/" + std::to_string(At<int>(e, 8)) + "/" +
                 std::to_string(At<uint8_t>(e, 12));
        s += " diary";
        for (const uint8_t *e : Elements(n, side ? PO(C_CampaignScreen, m_diaryTexts) : 0x28, side, 4))
            s += " " + std::to_string(At<int>(e, 0));
        break;
    }
    case CAMPAIGN_NODE_OBJECTIVE:
    case CAMPAIGN_NODE_MP_OBJECTIVE:
        s += " status " + std::to_string(At<int>(n, 0x10)) + " bonus " + std::to_string(At<uint8_t>(n, 0x14)) + " rewarded " +
             std::to_string(At<uint8_t>(n, 0x15));
        break;
    case CAMPAIGN_NODE_ROOT:
        s += Children(side, n, depth);
        break;
    case CAMPAIGN_NODE_OPERATION:
        s += " " + StrAt(n, side ? PO(C_CampaignOperation, m_name) : 0x24) + Children(side, n, depth);
        break;
    case CAMPAIGN_NODE_MISSION:
    case CAMPAIGN_NODE_MP_MISSION: {
        s += " " + StrAt(n, side ? PO(C_MissionDef, m_name) : 0x24) + " " + StrAt(n, side ? PO(C_MissionDef, m_image) : 0x28) + " u2c " +
             std::to_string(At<uint8_t>(n, side ? PO(C_MissionDef, m_unknown2C) : 0x2C)) + " texts";
        for (int k = 0; k < 4; ++k)
            s += " " + std::to_string(At<int>(n, (side ? PO(C_MissionDef, m_texts) : 0x40) + uint32_t(4 * k)));
        s += " objectives";
        for (const uint8_t *e : Elements(n, side ? PO(C_MissionDef, m_objectives) : 0x30, side, 4))
            s += Node(side, At<const void *>(e, 0), depth + 1);
        s += Children(side, n, depth);
        break;
    }
    default:
        break;
    }
    return s + "]";
}
std::string Snap(int side)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    std::string s = "campaign current " + std::to_string(At<int>(b, side ? PO(C_Campaign, m_current) : 0xE8)) + " version " +
                    std::to_string(At<int>(b, side ? PO(C_Campaign, m_version) : 0xEC)) + " single " +
                    std::to_string(At<int>(b, side ? PO(C_Campaign, m_singleMissionNumber) : 0xE4)) + " f0 " +
                    std::to_string(At<uint8_t>(b, side ? PO(C_Campaign, m_unknownF0) : 0xF0)) + " campaigns";
    for (const uint8_t *e : Elements(b, side ? PO(C_Campaign, m_campaigns) : 4, side, 4))
        s += Node(side, At<const void *>(e, 0), 0);
    s += " | single" + Node(side, b + (side ? PO(C_Campaign, m_singleMission) : 0x94), 0);
    s += " | mp" + Node(side, b + (side ? PO(C_Campaign, m_mpMission) : 0x14), 0);
    return s;
}

// ---- the files: the shipped ones, and ones the scenario builds ----
std::string g_shipped[2];
std::string Chunk(uint16_t id, const std::string &payload)
{
    std::string c;
    const uint32_t size = uint32_t(6 + payload.size());
    c.append(reinterpret_cast<const char *>(&id), 2);
    c.append(reinterpret_cast<const char *>(&size), 4);
    return c + payload;
}
std::string Int4(int x) { return std::string(reinterpret_cast<const char *>(&x), 4); }
std::string Byte(int x) { return std::string(1, char(x)); }
const char *const TEXTS[] = {"BriefArctic1a.bik", "Briefing Arktida 1A", "e_i_ar1a.bmp", "Arktida1", "Operation Hammer", "", "x"};
std::string Text() { return std::string(Pick(TEXTS)) + std::string(1, '\0'); }   // (always ended: the shipped files' are)
// (Now and then an unknown chunk among the known ones.)
std::string Unknown() { return P(0.05) ? Chunk(uint16_t(I(100, 120)), Int4(I(0, 9))) : std::string(); }

// The complete format.
std::string FullItem()
{
    std::string item = Int4(I(1, 250)) + Int4(I(1, 30)) + Int4(P(0.5) ? -1 : I(0, 99)) + Byte(P(0.3)) + std::string(3, '\0');
    // (Never short: the original keeps the previous item's bytes in a short one's rest, a stack word for the first;
    // the port zeroes them. The shipped files have none.)
    return item;
}
std::string FullLeaf()
{
    switch (I(0, 2)) {
    case 0: {
        std::string v;
        if (P(0.9))
            v += Chunk(0x0D, Byte(P(0.5)));
        if (P(0.9))
            v += Chunk(0x0B, Text());
        if (P(0.9))
            v += Chunk(0x0C, Text());
        return Chunk(0x0A, v + Unknown());
    }
    case 1: {
        std::string s;
        if (P(0.9))
            s += Chunk(0x15, Int4(I(1, 6)));
        if (P(0.8))
            s += Chunk(0x17, Byte(P(0.5)));
        if (P(0.8))
            s += Chunk(0x16, Byte(P(0.5)));
        for (int n = P(0.5) ? 0 : I(1, 5); n > 0; --n)
            s += Chunk(0x18, FullItem());
        for (int n = P(0.7) ? 0 : I(1, 3); n > 0; --n)
            s += Chunk(0x19, Int4(I(0x1C00, 0x1D00)));
        return Chunk(0x14, s + Unknown());
    }
    default:
        return Chunk(0x1E, std::string());
    }
}
std::string FullMission()
{
    std::string m;
    if (P(0.9))
        m += Chunk(0x33, Int4(I(1, 30)));
    if (P(0.8))
        m += Chunk(0x34, Byte(P(0.1)));
    if (P(0.9))
        m += Chunk(0x35, Text());
    if (P(0.9))
        m += Chunk(0x36, Text());
    if (P(0.9)) {
        std::string texts;
        for (int k = 0; k < 4; ++k)
            texts += Int4(P(0.3) ? -1 : I(0x1C00, 0x1E00));
        m += Chunk(0x37, texts);   // (never short either)
    }
    for (int n = I(0, 4); n > 0; --n) {
        std::string o;
        if (P(0.9))
            o += Chunk(0x29, Int4(I(0x1C00, 0x1E00)));
        if (P(0.8))
            o += Chunk(0x2A, Int4(I(0, 4)));
        if (P(0.8))
            o += Chunk(0x2B, Byte(P(0.3)));
        m += Chunk(0x28, o + Unknown());
    }
    std::string children;
    for (int n = I(0, 5); n > 0; --n)
        children += FullLeaf();
    m += Chunk(5, children) + Unknown();
    return Chunk(0x32, m);
}
std::string FullOperation()
{
    std::string o;
    if (P(0.9))
        o += Chunk(0x3D, Int4(I(0x1C00, 0x1E00)));
    if (P(0.9))
        o += Chunk(0x3E, Text());
    std::string children;
    for (int n = I(0, 5); n > 0; --n)
        children += P(0.5) ? FullMission() : FullLeaf();
    o += Chunk(5, children) + Unknown();
    return Chunk(0x3C, o);
}
std::string FullRoot()
{
    std::string r;
    if (P(0.9))
        r += Chunk(0x47, Int4(I(0, 3)));
    std::string children;
    for (int n = I(0, 4); n > 0; --n)
        children += P(0.6) ? FullOperation() : FullLeaf();
    r += Chunk(5, children) + Unknown();
    return r;
}
// The definition format.
std::string DefString(bool other = false) { return Chunk(other ? 1001 : 1000, Text()); }
std::string DefLeaf()
{
    switch (I(0, 2)) {
    case 0:
        // (Both strings: without the second, both read 0 bytes into their buffers, the original's shows the string read
        // before, its reader not ending the buffer.)
        return Chunk(5, Byte(P(0.5)) + DefString(P(0.1)) + DefString(P(0.1)));
    case 1: {
        std::string s = Int4(I(1, 6)) + Byte(P(0.5)) + Byte(P(0.5));
        for (int n = P(0.5) ? 0 : I(1, 4); n > 0; --n)
            s += Chunk(10, FullItem());
        for (int n = P(0.7) ? 0 : I(1, 3); n > 0; --n)
            s += Chunk(11, Int4(I(0x1C00, 0x1D00)));
        return Chunk(6, s + Unknown());
    }
    default:
        return Chunk(7, std::string());
    }
}
std::string DefMission()
{
    std::string m = Int4(I(1, 30)) + Int4(I(0, 9)) + Int4(I(0, 9)) + Int4(I(0, 9)) + Int4(I(0, 9)) + Byte(P(0.1)) + DefString(P(0.1)) +
                    DefString(P(0.1));
    for (int n = I(0, 6); n > 0; --n)
        m += P(0.4) ? Chunk(4, Int4(I(0x1C00, 0x1E00)) + Int4(I(0, 4))) : DefLeaf();
    return Chunk(3, m + Unknown());
}
std::string DefOperation()
{
    std::string o = Int4(I(0x1C00, 0x1E00)) + DefString(P(0.1));
    for (int n = I(0, 5); n > 0; --n)
        o += P(0.5) ? DefMission() : DefLeaf();
    return Chunk(2, o + Unknown());
}
std::string DefRoot()
{
    // (Now and then too short for its three ints and id: nothing more in it.)
    if (P(0.04))
        return std::string(size_t(I(0, 15)), char(I(0, 9)));
    std::string r = Int4(I(0, 9)) + Int4(I(0, 9)) + Int4(I(0, 9)) + Int4(I(0, 3));
    for (int n = I(0, 4); n > 0; --n)
        r += P(0.6) ? DefOperation() : DefLeaf();
    return r + Unknown();
}
// A file: the version chunk (3), the root (1), in either format; now and then the loading stops at it.
// (No damaged chunk sizes: a chunk grown past its container's end or into its sibling makes both read strings of no
// bytes, where the original shows the string read before (its reader does not end the buffer), or take a sibling's
// middle for headers, one smaller than a header seeking back: both loop for ever.)
std::string MakeFile(int version)
{
    const bool full = P(0.6);
    const int v = full ? version : (P(0.5) ? version + 1 : version - 1);
    switch (P(0.08) ? I(0, 3) : -1) {
    case 0:
        return Chunk(4, Int4(v));   // (no version chunk: the loading stops)
    case 1:
        return Chunk(3, Int4(v)) + Chunk(2, std::string());   // (no root)
    case 2:
        return std::string();   // (empty: the last file's last chunk id stays, a root's after a loaded file)
    case 3:
        return Chunk(1, DefRoot());   // (no version chunk, the root first: read as definitions)
    default:
        break;
    }
    return Chunk(3, Int4(v)) + Chunk(1, full ? FullRoot() : DefRoot());
}

// ---- the scenarios ----
enum { OP_LOAD, OP_CLEAR, OP_SAVE, OP_COUNT };
struct S_Op {
    int kind;
};
struct S_Run {
    std::vector<std::string> log;
};
S_Run g_runs[2];
// Each campaign's Save and SaveProgress, through each side's own vtable (slots 0x0C, 0x14), into files compared.
void SaveAll(int side)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    const std::vector<const uint8_t *> roots = Elements(b, side ? PO(C_Campaign, m_campaigns) : 4, side, 4);
    for (size_t k = 0; k < roots.size(); ++k) {
        void *root = At<void *>(roots[k], 0);
        for (int progress = 0; progress < 2; ++progress) {
            const std::string name = "save" + std::to_string(k) + (progress ? "p" : "") + ".sav";
            bool result;
            if (side) {
                C_chunk chunk;
                chunk.Open(name.c_str(), true);
                chunk.Begin(1);
                C_CampaignNode *node = static_cast<C_CampaignNode *>(root);
                result = progress ? node->SaveProgress(chunk) : node->Save(chunk);
                chunk.Ascend();
                // (The next chunk after it: a level left open shows.)
                chunk.WriteChunk(0x7777, int(0x12345678));
                chunk.Close();
            } else {
                alignas(16) uint8_t chunk[0x500] = {};
                OriginalAt<T_ChunkOpen>(0x00405620)(chunk, name.c_str(), true);
                using T_Begin = bool(__attribute__((thiscall)) *)(void *, uint16_t);
                using T_Ascend = bool(__attribute__((thiscall)) *)(void *, uint32_t *);
                OriginalAt<T_Begin>(0x00405820)(chunk, 1);
                result = reinterpret_cast<T_Chunk>((*reinterpret_cast<void ***>(root))[(progress ? 0x14 : 0x0C) / 4])(root, chunk);
                OriginalAt<T_Ascend>(0x00405890)(chunk, nullptr);
                const int next = 0x12345678;
                OriginalAt<T_Begin>(0x00405820)(chunk, 0x7777);
                using T_Write = bool(__attribute__((thiscall)) *)(void *, const void *, int);
                OriginalAt<T_Write>(0x004059b0)(chunk, &next, 4);
                OriginalAt<T_Ascend>(0x00405890)(chunk, nullptr);
                OriginalAt<T_ChunkClose>(0x00405750)(chunk);
            }
            std::string hex;
            for (unsigned char c : g_files[name]) {
                char t[4];
                std::snprintf(t, sizeof t, "%02x", c);
                hex += t;
            }
            Log("saved %s %d %s", name.c_str(), result, hex.c_str());
        }
    }
}
void Run(int side, const S_Op &op)
{
    switch (op.kind) {
    case OP_LOAD:
        Log("result %d", side ? g_port->LoadFiles() : OriginalAt<T_RetB>(0x006bb860)(g_orig));
        break;
    case OP_CLEAR:
        side ? g_port->Clear() : OriginalAt<T_V>(0x006b8c60)(g_orig);
        break;
    case OP_SAVE:
        SaveAll(side);
        break;
    default:
        break;
    }
}
void TestScenarios()
{
    // The files: GameData\Gamedata00.gdt on, each the shipped one (its number's), a built one, or none (the end).
    const int version = P(0.9) ? C_Campaign::VERSION : Pick({1, 3});
    std::map<std::string, std::string> files;
    for (int k = 0; k < 4; ++k) {
        char path[64];
        std::snprintf(path, sizeof path, "GameData\\Gamedata%02d.gdt", k);
        if (k < 2 && !g_shipped[k].empty() && P(0.3))
            files[path] = g_shipped[k];
        else if (P(0.75))
            files[path] = MakeFile(version);
        else
            break;
    }
    std::vector<S_Op> ops;
    for (int n = I(1, 5); n > 0; --n)
        ops.push_back(S_Op{P(0.5) ? int(OP_LOAD) : P(0.6) ? int(OP_SAVE) : int(OP_CLEAR)});
    if (P(0.7))
        ops.push_back(S_Op{OP_SAVE});
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_log.clear();
        g_files = files;
        g_openFiles.clear();
        g_nextFile = 1;
        if (side) {
            g_port = new C_Campaign();
            g_port->m_version = version;
        } else
            BuildOrig(version);
        for (const S_Op &op : ops) {
            Log("-- %d", op.kind);
            Run(side, op);
            g_log.push_back(Snap(side));
        }
        Log("-- destroy");
        if (side)
            delete g_port;
        else {
            OriginalAt<T_V>(0x006b6420)(g_orig);
            std::free(g_orig);
        }
        g_runs[side].log.swap(g_log);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

void SetUp()
{
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e6334, reinterpret_cast<const void *>(O_Sprintf)},
        {0x007e52d8, reinterpret_cast<const void *>(O_DbgPrintf)},
        {0x007e53a4, reinterpret_cast<const void *>(O_dtaOpen)},
        {0x007e53aa, reinterpret_cast<const void *>(O_dtaOpenWrite)},
        {0x007e53b0, reinterpret_cast<const void *>(O_dtaClose)},
        {0x007e5392, reinterpret_cast<const void *>(O_dtaWrite)},
        {0x007e5398, reinterpret_cast<const void *>(O_dtaSeek)},
        {0x007e539e, reinterpret_cast<const void *>(O_dtaRead)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // The shipped campaign files.
    const char *data = std::getenv("HD2_UNPACKED");
    const std::string dir = std::string(data ? data : "H:\\HD2_Unpacked") + "\\GameData\\";
    for (int k = 0; k < 2; ++k) {
        char name[32];
        std::snprintf(name, sizeof name, "Gamedata%02d.gdt", k);
        std::ifstream in(dir + name, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        g_shipped[k] = s.str();
        std::printf("campaign_diff: %s%s %u bytes\n", dir.c_str(), name, unsigned(g_shipped[k].size()));
    }
    g_log.clear();
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("campaign_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_CAMPAIGN_QUICK") ? 1500 : 4000;
    if (const char *r = std::getenv("HD2_CAMPAIGN_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "Files";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    std::printf("campaign_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
