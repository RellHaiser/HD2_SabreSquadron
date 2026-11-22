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
// Part 78b (the "Flow" case): the files loaded, a progress file built from their trees (the game's cursors mostly: the
// children before a cursor walked through, those after it not started; chunks missing, of other ids, out of range;
// the older layout), then the flow (AdvanceToMission, NextStep, PreviousScreen), the queries (out of range arguments
// too), SetCurrentIndex, SetupSingleMission, SetupMultiplayerMission (a multiplayer map on each side: the port's C_MpMap,
// the original's block), SaveGame (compared byte by byte), LoadGame, the containers' own steps and queries through
// their vtables, the play mode and session type changed; the trees compared after each. Prev, NextStep and
// PreviousScreen are run dry first: those that would index out of a container's children or loop for ever (in the
// original too) are not run.
// HD2_CAMPAIGN_ROUNDS: scenarios of each case (default 2,000; HD2_CAMPAIGN_QUICK: 1,000); HD2_INTERFACE_SHOW, HD2_INTERFACE_ECHO,
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
#include "game/mp_game_setup.h"
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
// The original's atexit (its CRT's tables are not set up): the function statics' destructors never run.
int __cdecl O_Atexit(void (*)()) { return 0; }

}  // namespace H
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name, false); }
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpen(name, true); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }
extern "C" void dbgPrintf(const char *format, ...) { H::Log("dbgPrintf %s", format); }
namespace H {
C_MpMap *g_portMap;
alignas(16) uint8_t g_origMap[0x48];
void *__attribute__((thiscall)) O_GetCurrentMap(void *)
{
    Log("mpSettings GetCurrentMap");
    return g_origMap;
}
}  // namespace H
C_MpMap *S_MultiplayerSettings::GetCurrentMap()
{
    H::Log("mpSettings GetCurrentMap");
    return H::g_portMap;
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
// An MSVC6 vector {allocator, first, last, end} over n elements at p.
void PutVector(void *b, uint32_t at, void *p, int n, uint32_t elem)
{
    Put<uint32_t>(b, at, 0);
    Put<uint8_t *>(b, at + 4, static_cast<uint8_t *>(p));
    Put<uint8_t *>(b, at + 8, p ? static_cast<uint8_t *>(p) + n * elem : nullptr);
    Put<uint8_t *>(b, at + 12, p ? static_cast<uint8_t *>(p) + n * elem : nullptr);
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
        if (type == CAMPAIGN_NODE_MP_OBJECTIVE) {
            // (Part 78b: the multiplayer map's objectives: team, allied and axis texts, points, type, VIP uniform.)
            s += " mp";
            const uint32_t fields[2][6] = {{0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C},
                                           {PO(S_MpObjective, team), PO(S_MpObjective, alliedText), PO(S_MpObjective, axisText),
                                            PO(S_MpObjective, points), PO(S_MpObjective, type), PO(S_MpObjective, vipUniform)}};
            for (uint32_t o : fields[side])
                s += " " + std::to_string(At<int>(n, o));
        }
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
        if (type == CAMPAIGN_NODE_MP_MISSION) {
            // (Part 78b: the multiplayer map's fields; the items' fourth word not compared: stack garbage in the
            // original.)
            s += " map " + StrAt(n, side ? PO(C_MpMissionDef, m_mapName) : 0x6C) + " presets " +
                 StrAt(n, side ? PO(C_MpMissionDef, m_presets) : 0x68) + " ints";
            const uint32_t ints[2][6] = {{0x50, 0x54, 0x58, 0x5C, 0x60, 0x64},
                                         {PO(C_MpMissionDef, m_aircraftAttackAxis), PO(C_MpMissionDef, m_aircraftAttackAllied),
                                          PO(C_MpMissionDef, m_balanceAllied), PO(C_MpMissionDef, m_balanceAxis),
                                          PO(C_MpMissionDef, m_unknown60), PO(C_MpMissionDef, m_briefingText)}};
            for (uint32_t o : ints[side])
                s += " " + std::to_string(At<int>(n, o));
            s += " items";
            for (const uint8_t *e : Elements(n, side ? PO(C_MpMissionDef, m_items) : 0x70, side, 16))
                s += " " + std::to_string(At<int>(e, 0)) + "/" + std::to_string(At<int>(e, 4)) + "/" + std::to_string(At<int>(e, 8));
        }
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

// ---- part 78b: the flow's world ----
struct S_FlowWorld {
    int singleMission, sessionType;
    // The multiplayer map.
    int ints[6];
    int objectives, items;
    S_MpMapObjective objective[4];
    S_MpMapItem item[5];
    int nameIndex, directoryIndex, presetsIndex;
};
S_FlowWorld g_f;
const char *const MAP_TEXTS[] = {"Mp_Dm_Africa", "mp_obj_city", "Allied gunner;Axis medic", "", "Zone1"};
std::string g_mapTexts[3];
void RandomFlowWorld()
{
    g_f.singleMission = P(0.2) ? 1 : P(0.5) ? 0 : 2;
    g_f.sessionType = P(0.8) ? 0 : I(1, 2);
    for (int &v : g_f.ints)
        v = P(0.3) ? -1 : I(0, 9);
    g_f.objectives = I(0, 4);
    g_f.items = I(0, 5);
    for (S_MpMapObjective &o : g_f.objective)
        o = S_MpMapObjective{I(0x1C00, 0x1D00), I(0x1C00, 0x1D00), I(0, 9) * 100, Pick({1, -1}), I(-1, 1), P(0.5) ? -1 : I(200, 230)};
    for (S_MpMapItem &i : g_f.item)
        i = S_MpMapItem{I(1, 250), I(0, 30), P(0.5) ? -1 : I(0, 99)};
    g_f.nameIndex = I(0, 4);
    g_f.directoryIndex = I(0, 4);
    g_f.presetsIndex = I(0, 4);
}
// Each side's flags (the play mode's singleMission, the settings' sessionType: the original's through its pointers at
// 0x008aea34 / 0x008aea30) and map (the port's C_MpMap; the original's block: its strings at +0x01 / +0x05 / +0x44,
// its MSVC6 vectors at +0x24 / +0x34).
alignas(16) S_MpMapObjective g_mapObjectives[4];
alignas(16) S_MpMapItem g_mapItems[5];
void SetFlowWorld(int side)
{
    Put<int>(g_playModeBlock[side], side ? uint32_t(offsetof(S_PlayMode, singleMission)) : 4, g_f.singleMission);
    Put<int>(g_mpSettingsBlock[side], side ? uint32_t(offsetof(S_MultiplayerSettings, sessionType)) : 4, g_f.sessionType);
    *OriginalAt<void **>(0x008aea34) = g_playModeBlock[0];
    *OriginalAt<void **>(0x008aea30) = g_mpSettingsBlock[0];
    std::copy(g_f.objective, g_f.objective + 4, g_mapObjectives);
    std::copy(g_f.item, g_f.item + 5, g_mapItems);
    const C_str name = MAP_TEXTS[g_f.nameIndex], directory = MAP_TEXTS[g_f.directoryIndex], presets = MAP_TEXTS[g_f.presetsIndex];
    if (side) {
        delete g_portMap;
        g_portMap = new C_MpMap(name, directory);
        g_portMap->aircraftAxis = g_f.ints[0];
        g_portMap->aircraftAllied = g_f.ints[1];
        g_portMap->balancedAxis = g_f.ints[2];
        g_portMap->balancedAllied = g_f.ints[3];
        g_portMap->frontline = g_f.ints[4];
        g_portMap->description = g_f.ints[5];
        g_portMap->objectives.assign(g_mapObjectives, g_mapObjectives + g_f.objectives);
        g_portMap->items.assign(g_mapItems, g_mapItems + g_f.items);
        g_portMap->presets = presets;
    } else {
        // (The strings: the port's C_str in the original's places, its own C_str class: references shared.)
        std::memset(g_origMap, 0, sizeof g_origMap);
        new (g_origMap + 1) C_str(name);
        new (g_origMap + 5) C_str(directory);
        new (g_origMap + 0x44) C_str(presets);
        for (int k = 0; k < 6; ++k)
            Put<int>(g_origMap, uint32_t(0x0C + 4 * k), g_f.ints[k]);
        PutVector(g_origMap, 0x24, g_f.objectives ? static_cast<void *>(g_mapObjectives) : nullptr, g_f.objectives, 0x18);
        PutVector(g_origMap, 0x34, g_f.items ? static_cast<void *>(g_mapItems) : nullptr, g_f.items, 0x0C);
    }
}
// A node's path in its side's tree: "c<campaign>/<child>/..." (objectives "o<index>"), "single...", "mp...".
bool FindPath(int side, const void *n, const void *target, std::string &path)
{
    if (n == target)
        return true;
    const int type = At<int>(n, 4);
    if (type == CAMPAIGN_NODE_MISSION || type == CAMPAIGN_NODE_MP_MISSION) {
        int k = 0;
        for (const uint8_t *e : Elements(n, side ? PO(C_MissionDef, m_objectives) : 0x30, side, 4)) {
            if (At<const void *>(e, 0) == target) {
                path += "/o" + std::to_string(k);
                return true;
            }
            ++k;
        }
    }
    if (type != CAMPAIGN_NODE_ROOT && type != CAMPAIGN_NODE_OPERATION && type != CAMPAIGN_NODE_MISSION && type != CAMPAIGN_NODE_MP_MISSION)
        return false;
    int k = 0;
    for (const uint8_t *e : Elements(n, side ? PO(C_CampaignContainer, m_children) : 0x10, side, 4)) {
        const std::string before = path;
        path += "/" + std::to_string(k);
        if (FindPath(side, At<const void *>(e, 0), target, path))
            return true;
        path = before;
        ++k;
    }
    return false;
}
std::string Path(int side, const void *target)
{
    if (!target)
        return "null";
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    std::string path;
    int k = 0;
    for (const uint8_t *e : Elements(b, side ? PO(C_Campaign, m_campaigns) : 4, side, 4)) {
        path = "c" + std::to_string(k++);
        if (FindPath(side, At<const void *>(e, 0), target, path))
            return path;
    }
    path = "single";
    if (FindPath(side, b + (side ? PO(C_Campaign, m_singleMission) : 0x94), target, path))
        return path;
    path = "mp";
    if (FindPath(side, b + (side ? PO(C_Campaign, m_mpMission) : 0x14), target, path))
        return path;
    return "?";
}
// The k-th container of the trees (depth first: the campaigns', then the single and the multiplayer missions).
void Containers(int side, const void *n, std::vector<const void *> &out)
{
    const int type = At<int>(n, 4);
    if (type != CAMPAIGN_NODE_ROOT && type != CAMPAIGN_NODE_OPERATION && type != CAMPAIGN_NODE_MISSION && type != CAMPAIGN_NODE_MP_MISSION)
        return;
    out.push_back(n);
    for (const uint8_t *e : Elements(n, side ? PO(C_CampaignContainer, m_children) : 0x10, side, 4))
        Containers(side, At<const void *>(e, 0), out);
}
void *Container(int side, int k)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    std::vector<const void *> all;
    for (const uint8_t *e : Elements(b, side ? PO(C_Campaign, m_campaigns) : 4, side, 4))
        Containers(side, At<const void *>(e, 0), all);
    Containers(side, b + (side ? PO(C_Campaign, m_singleMission) : 0x94), all);
    Containers(side, b + (side ? PO(C_Campaign, m_mpMission) : 0x14), all);
    return all.empty() ? nullptr : const_cast<void *>(all[size_t(k) % all.size()]);
}
// Whether NextStep loops for ever (both sides): in single-mission play the flow is the single mission, which, stepped
// past its end, returns itself, a mission, and the loop goes on (SetupSingleMission ends the mission with an end node,
// which stops it). Its children are walked as the loop would: a container among them is counted as a hang (not
// modelled).
bool NextStepHangs(int side)
{
    if (g_f.singleMission != 1)
        return false;
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    const uint8_t *n = b + (side ? PO(C_Campaign, m_singleMission) : 0x94);
    const int cursor = At<int>(n, side ? PO(C_CampaignContainer, m_cursor) : 0x20);
    const std::vector<const uint8_t *> children = Elements(n, side ? PO(C_CampaignContainer, m_children) : 0x10, side, 4);
    const int first = cursor < 0 ? 0 : cursor + 1;
    if (first >= int(children.size()))
        return true;
    const int t = At<int>(At<const void *>(children[size_t(first)], 0), 4);
    // (A video, screen or play is returned; any other leaf ends the loop with nothing.)
    return t == CAMPAIGN_NODE_ROOT || t == CAMPAIGN_NODE_OPERATION || t == CAMPAIGN_NODE_MISSION || t == CAMPAIGN_NODE_MP_MISSION;
}
// A dry run of Prev on a side's tree (the cursors it would change kept apart): whether it would index out of a
// container's children (a container at cursor 0 without children, as Next leaves an empty one, steps back to child -1;
// one past its end, a progress file's cursor, reads past them). Both sides' are the same.
struct S_PrevRun {
    int side;
    std::map<const void *, int> cursors;
    bool unsafe = false;
    static bool IsContainer(int t)
    {
        return t == CAMPAIGN_NODE_ROOT || t == CAMPAIGN_NODE_OPERATION || t == CAMPAIGN_NODE_MISSION || t == CAMPAIGN_NODE_MP_MISSION;
    }
    int &Cursor(const void *n)
    {
        auto it = cursors.find(n);
        if (it == cursors.end())
            it = cursors.emplace(n, At<int>(n, side ? PO(C_CampaignContainer, m_cursor) : 0x20)).first;
        return it->second;
    }
    std::vector<const void *> Children(const void *n)
    {
        std::vector<const void *> out;
        for (const uint8_t *e : Elements(n, side ? PO(C_CampaignContainer, m_children) : 0x10, side, 4))
            out.push_back(At<const void *>(e, 0));
        return out;
    }
    bool Prev(const void *n)
    {
        if (unsafe || !IsContainer(At<int>(n, 4)))
            return false;
        int &c = Cursor(n);
        const std::vector<const void *> children = Children(n);
        const int count = int(children.size());
        if (c < 0)
            return false;
        if (c > count || (c == count && count == 0)) {
            unsafe = true;
            return true;
        }
        if (c == count) {
            --c;
            Prev(children[size_t(c)]);
            return true;
        }
        if (!Prev(children[size_t(c)])) {
            --c;
            if (c >= 0)
                Prev(children[size_t(c)]);
        }
        return true;
    }
    const void *Current(const void *n)
    {
        if (!IsContainer(At<int>(n, 4)))
            return n;
        const int c = Cursor(n);
        const std::vector<const void *> children = Children(n);
        return c >= 0 && c < int(children.size()) ? Current(children[size_t(c)]) : n;
    }
};
bool PrevUnsafe(int side, const void *n)
{
    S_PrevRun run{side};
    run.Prev(n);
    return run.unsafe;
}
// PreviousScreen's loop run dry: whether one of its Prevs is unsafe, or it loops for ever (in single-mission play,
// stepping back across from the mission's first screen reaches the mission itself, which goes on being returned).
bool PreviousUnsafe(int side)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    const void *flow;
    if (g_f.singleMission == 1)
        flow = b + (side ? PO(C_Campaign, m_singleMission) : 0x94);
    else {
        const std::vector<const uint8_t *> roots = Elements(b, side ? PO(C_Campaign, m_campaigns) : 4, side, 4);
        const unsigned current = At<unsigned>(b, side ? PO(C_Campaign, m_current) : 0xE8);
        if (current >= roots.size())
            return false;
        flow = At<const void *>(roots[current], 0);
    }
    S_PrevRun run{side};
    const void *current = run.Current(flow);
    if (At<int>(current, 4) != CAMPAIGN_NODE_SCREEN)
        return false;
    const bool backAcross = At<uint8_t>(current, side ? PO(C_CampaignScreen, m_backAcross) : 0x14) != 0;
    for (int k = 0; k < 100000; ++k) {
        run.Prev(flow);
        if (run.unsafe)
            return true;
        switch (At<int>(run.Current(flow), 4)) {
        case CAMPAIGN_NODE_OPERATION:
        case CAMPAIGN_NODE_MISSION:
            if (!backAcross)
                return false;
            break;
        case CAMPAIGN_NODE_OBJECTIVE:
        case CAMPAIGN_NODE_VIDEO:
        case CAMPAIGN_NODE_PLAY:
            break;
        default:
            return false;
        }
    }
    return true;
}
// The progress file the loads read: built from a probe of the files' trees (the port's, loaded apart and not logged): each
// campaign's cursors and children's progress, the missions' objectives, with chunks missing, of other ids, values out
// of range; or an older layout (the first campaign's progress alone), or a broken one.
// (State: -1 at random; 0 a node not started, its cursor -1; 1 one walked through, its cursor at the end; 2 one in progress
// as the game leaves it, the children before its cursor walked through, those after it not started.)
std::string ProgressOf(const C_CampaignNode *n, int state = -1)
{
    std::string s;
    const int type = n->GetType();
    if (type == CAMPAIGN_NODE_MISSION) {
        for (const S_MissionObjective *o : static_cast<const C_MissionDef *>(n)->m_objectives) {
            (void)o;
            std::string ob;
            if (P(0.9))
                ob += Chunk(0x2A, Int4(I(0, 4)));
            if (P(0.8))
                ob += Chunk(0x2C, Byte(P(0.5)));
            s += Chunk(0x28, ob);
        }
        if (P(0.2))
            s += Chunk(0x28, Chunk(0x2A, Int4(1)));   // (one objective more than the mission has)
    }
    const auto *c = static_cast<const C_CampaignContainer *>(n);
    if (state < 0 && P(0.7))
        state = 2;
    int cursor = state == 0 ? -1 : state == 1 ? c->GetChildCount() : I(-1, c->GetChildCount());
    if (state < 0 && P(0.02))
        cursor = I(-3, c->GetChildCount() + 2);   // (out of range: Prev is not run on those trees)
    if (state >= 0 || P(0.9))
        s += Chunk(4, Int4(cursor));
    std::string children;
    int index = -1;
    for (const C_CampaignNode *child : c->m_children) {
        ++index;
        if (child->GetType() != CAMPAIGN_NODE_OPERATION && child->GetType() != CAMPAIGN_NODE_MISSION)
            continue;
        if (P(0.05))
            break;   // (cut short)
        const uint16_t id = child->GetType() == CAMPAIGN_NODE_OPERATION ? 0x3C : 0x32;
        const int childState = state < 0 ? -1 : index < cursor ? 1 : index > cursor ? 0 : 2;
        children += Chunk(P(0.95) ? id : uint16_t(0x3D), ProgressOf(child, childState));
    }
    if (state >= 0 || P(0.9))
        s += Chunk(5, children);
    return s;
}
std::string MakeProgress(const std::map<std::string, std::string> &files, int version)
{
    std::map<std::string, std::string> kept = g_files;
    std::vector<std::string> log;
    log.swap(g_log);
    g_files = files;
    C_Campaign probe;
    probe.m_version = version;
    probe.LoadFiles();
    std::string p;
    if (P(0.9))
        p += Chunk(0x50, Int4(P(0.8) ? I(0, std::max(0, probe.GetCount() - 1)) : Pick({-1, 5, probe.GetCount()})));
    if (P(0.5))
        p += Chunk(0x51, Int4(P(0.8) ? version : I(1, 3)));
    if (P(0.6))
        p += Chunk(0x52, Int4(P(0.5) ? -1 : I(0, 12)));
    if (P(0.4))
        p += Chunk(0x53, std::string());
    if (P(0.1) && probe.GetCount() > 0) {
        // (The older layout: the first campaign's progress alone.)
        p += Chunk(1, ProgressOf(probe.m_campaigns[0]));
    } else {
        std::string all;
        for (C_CampaignRoot *root : probe.m_campaigns)
            all += Chunk(P(0.97) ? uint16_t(1) : uint16_t(2), ProgressOf(root));
        p += Chunk(0x54, all);
    }
    if (P(0.05))
        p += Chunk(0x60, Int4(1));   // (an unknown chunk: the load stops)
    g_files = kept;
    log.swap(g_log);
    // (Inside a chunk of the profile's, descended into before LoadGame.)
    return Chunk(0x4000, p);
}

// ---- the scenarios ----
enum {
    OP_LOAD, OP_CLEAR, OP_SAVE, OP_COUNT,
    // Part 78b.
    F_ADVANCE = OP_COUNT, F_NEXT_STEP, F_PREVIOUS, F_MISSION, F_SET_CURRENT, F_MISSION_NUMBER, F_OPERATION_INDEX, F_COUNT_OPERATIONS,
    F_COUNT_MISSIONS, F_COUNT_OPERATION_MISSIONS, F_OPERATION_ID, F_OPERATION_NAME, F_MISSION_ID, F_MISSION_IMAGE, F_MISSION_TEXT,
    F_OBJECTIVE_STATUS, F_SINGLE, F_MULTIPLAYER, F_SAVE_GAME, F_LOAD_GAME, F_WORLD, F_NODE_NEXT, F_NODE_PREV, F_NODE_CURRENT,
    F_NODE_RESET, F_NODE_STATE, F_NODE_CHILD, F_NODE_COUNT, F_NODE_OF_TYPE, F_NODE_INDEX, F_MISSION_DEACTIVATE, F_MISSION_CREATE,
    F_MISSION_CLEAR, F_COUNT,
};
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0, d = 0;
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
using T_I = void(__attribute__((thiscall)) *)(void *, int);
using T_RetI = int(__attribute__((thiscall)) *)(void *);
using T_RetII = int(__attribute__((thiscall)) *)(void *, int);
using T_RetIII = int(__attribute__((thiscall)) *)(void *, int, int);
using T_RetIIII = int(__attribute__((thiscall)) *)(void *, int, int, int);
using T_RetIIIII = int(__attribute__((thiscall)) *)(void *, int, int, int, int);
using T_RetP = void *(__attribute__((thiscall)) *)(void *);
using T_RetPI = void *(__attribute__((thiscall)) *)(void *, int);
using T_RetPII = void *(__attribute__((thiscall)) *)(void *, int, int);
using T_RetPIB = void *(__attribute__((thiscall)) *)(void *, int, bool);
using T_RetBII = bool(__attribute__((thiscall)) *)(void *, int, int);
void *Slot(void *object, uint32_t slot) { return (*reinterpret_cast<void ***>(object))[slot / 4]; }
std::string StrText(const void *p) { return p ? Str(*static_cast<const C_str *>(p)) : "null"; }
void Run(int side, const S_Op &op)
{
    void *o = g_orig;
    C_Campaign *c = g_port;
    switch (op.kind) {
    case F_ADVANCE:
        Log("result %d", side ? c->AdvanceToMission() : OriginalAt<T_RetB>(0x006b8d40)(o));
        return;
    case F_NEXT_STEP:
        if (NextStepHangs(side)) {
            Log("next step hangs");
            return;
        }
        Log("result %s", Path(side, side ? static_cast<void *>(c->NextStep()) : OriginalAt<T_RetP>(0x006b8dc0)(o)).c_str());
        return;
    case F_PREVIOUS:
        if (PreviousUnsafe(side)) {
            Log("prev unsafe");
            return;
        }
        Log("result %s", Path(side, side ? static_cast<void *>(c->PreviousScreen()) : OriginalAt<T_RetP>(0x006b8e40)(o)).c_str());
        return;
    case F_MISSION:
        Log("result %s", Path(side, side ? static_cast<void *>(c->GetCurrentMission()) : OriginalAt<T_RetP>(0x006b94b0)(o)).c_str());
        return;
    case F_SET_CURRENT:
        // (SetCurrentIndex: inline on both.)
        side ? c->SetCurrentIndex(op.a) : Put<int>(o, 0xE8, op.a);
        return;
    case F_MISSION_NUMBER:
        Log("result %d", side ? c->GetMissionNumber(op.a) : OriginalAt<T_RetII>(0x006b91e0)(o, op.a));
        return;
    case F_OPERATION_INDEX:
        Log("result %d", side ? c->GetOperationIndex(op.a) : OriginalAt<T_RetII>(0x006b92b0)(o, op.a));
        return;
    case F_COUNT_OPERATIONS:
        Log("result %d", side ? c->CountOperations(op.a) : OriginalAt<T_RetII>(0x006b9320)(o, op.a));
        return;
    case F_COUNT_MISSIONS:
        Log("result %d", side ? c->CountMissions(op.a) : OriginalAt<T_RetII>(0x006b9350)(o, op.a));
        return;
    case F_COUNT_OPERATION_MISSIONS:
        Log("result %d", side ? c->CountOperationMissions(op.a, op.b) : OriginalAt<T_RetIII>(0x006b93c0)(o, op.a, op.b));
        return;
    case F_OPERATION_ID:
        Log("result %d", side ? c->GetOperationId(op.a, op.b) : OriginalAt<T_RetIII>(0x006b8f60)(o, op.a, op.b));
        return;
    case F_OPERATION_NAME:
        Log("result %s", StrText(side ? static_cast<const void *>(&c->GetOperationName(op.a, op.b)) : OriginalAt<T_RetPII>(0x006b8fa0)(o, op.a, op.b)).c_str());
        return;
    case F_MISSION_ID:
        Log("result %d", side ? c->GetMissionId(op.a, op.b) : OriginalAt<T_RetIII>(0x006b9040)(o, op.a, op.b));
        return;
    case F_MISSION_IMAGE:
        Log("result %s", StrText(side ? static_cast<const void *>(&c->GetMissionImage(op.a, op.b)) : OriginalAt<T_RetPII>(0x006b90e0)(o, op.a, op.b)).c_str());
        return;
    case F_MISSION_TEXT:
        Log("result %d", side ? c->GetMissionText(op.a, op.b, op.c) : OriginalAt<T_RetIIII>(0x006b9420)(o, op.a, op.b, op.c));
        return;
    case F_OBJECTIVE_STATUS:
        Log("result %d", side ? c->GetObjectiveStatus(op.a, op.b, op.c, op.d) : OriginalAt<T_RetIIIII>(0x006b8ed0)(o, op.a, op.b, op.c, op.d));
        return;
    case F_SINGLE:
        Log("result %d", side ? c->SetupSingleMission(op.a, op.b) : OriginalAt<T_RetBII>(0x006b99a0)(o, op.a, op.b));
        return;
    case F_MULTIPLAYER:
        Log("result %d", side ? c->SetupMultiplayerMission() : OriginalAt<T_RetB>(0x006b8a20)(o));
        return;
    case F_WORLD: {
        const std::mt19937 kept = g_rng;
        g_rng.seed(uint32_t(op.a));
        RandomFlowWorld();
        g_rng = kept;
        SetFlowWorld(side);
        return;
    }
    case F_SAVE_GAME:
    case F_LOAD_GAME: {
        const bool save = op.kind == F_SAVE_GAME;
        const char *name = save ? "game.sav" : "progress.sav";
        bool result;
        if (side) {
            C_chunk chunk;
            chunk.Open(name, save);
            uint16_t parent = 0;
            if (save)
                chunk.Begin(0x4000);
            else
                chunk.Descend(parent);
            result = save ? c->SaveGame(chunk) : c->LoadGame(chunk);
            if (save) {
                chunk.Ascend();
                chunk.WriteChunk(0x7777, int(0x12345678));
            } else
                Log("left at %d depth %d", chunk.GetPosition(), chunk.m_depth);
            chunk.Close();
        } else {
            alignas(16) uint8_t chunk[0x500] = {};
            using T_Begin = bool(__attribute__((thiscall)) *)(void *, uint16_t);
            using T_Ascend = bool(__attribute__((thiscall)) *)(void *, uint32_t *);
            using T_Write = bool(__attribute__((thiscall)) *)(void *, const void *, int);
            using T_Descend = bool(__attribute__((thiscall)) *)(void *, uint16_t *);
            OriginalAt<T_ChunkOpen>(0x00405620)(chunk, name, save);
            uint16_t parent = 0;
            if (save)
                OriginalAt<T_Begin>(0x00405820)(chunk, 0x4000);
            else
                OriginalAt<T_Descend>(0x00415c90)(chunk, &parent);
            result = reinterpret_cast<T_Chunk>(Slot(o, save ? 4 : 0))(o, chunk);
            if (save) {
                OriginalAt<T_Ascend>(0x00405890)(chunk, nullptr);
                const int next = 0x12345678;
                OriginalAt<T_Begin>(0x00405820)(chunk, 0x7777);
                OriginalAt<T_Write>(0x004059b0)(chunk, &next, 4);
                OriginalAt<T_Ascend>(0x00405890)(chunk, nullptr);
            } else
                Log("left at %d depth %d", FileSeek(At<int>(chunk, 4), 0, 1), At<int>(chunk, 0x410));
            OriginalAt<T_ChunkClose>(0x00405750)(chunk);
        }
        Log("result %d", result);
        if (save) {
            std::string hex;
            for (unsigned char ch : g_files[name]) {
                char t[4];
                std::snprintf(t, sizeof t, "%02x", ch);
                hex += t;
            }
            Log("saved %s", hex.c_str());
        }
        return;
    }
    default:
        break;
    }
    if (op.kind >= F_NODE_NEXT && op.kind <= F_MISSION_CLEAR) {
        // A container of the trees (the k-th, depth first), through its vtable on both sides; the original's per-type
        // copies of CountChildren / GetChildOfType / GetCurrentIndexOfType (missions 0x006b8710 / 0x006b8740 /
        // 0x006b87a0, operations 0x006b8800 / 0x006b8830 / 0x006b8890).
        void *n = Container(side, op.a);
        if (!n) {
            Log("no container");
            return;
        }
        auto *pn = static_cast<C_CampaignContainer *>(n);
        const int type = At<int>(n, 4);
        const bool mission = type == CAMPAIGN_NODE_MISSION || type == CAMPAIGN_NODE_MP_MISSION;
        const E_CampaignNodeType of = op.c ? CAMPAIGN_NODE_OPERATION : CAMPAIGN_NODE_MISSION;
        switch (op.kind) {
        case F_NODE_NEXT:
            Log("result %d", side ? pn->Next() : reinterpret_cast<T_RetB>(Slot(n, 0x18))(n));
            break;
        case F_NODE_PREV:
            if (PrevUnsafe(side, n)) {
                Log("prev unsafe");
                break;
            }
            // (Prev on a container before its first step reads nothing; past its last it steps back into the last
            // child.)
            Log("result %d", side ? pn->Prev() : reinterpret_cast<T_RetB>(Slot(n, 0x1C))(n));
            break;
        case F_NODE_CURRENT:
            Log("result %s", Path(side, side ? static_cast<void *>(pn->GetCurrent()) : reinterpret_cast<T_RetP>(Slot(n, 0x20))(n)).c_str());
            break;
        case F_NODE_RESET:
            side ? pn->Reset() : reinterpret_cast<T_V>(Slot(n, 0x24))(n);
            break;
        case F_NODE_STATE:
            Log("result %d", side ? pn->GetCursorState() : reinterpret_cast<T_RetI>(Slot(n, 0x2C))(n));
            break;
        case F_NODE_CHILD:
            Log("result %s", Path(side, side ? static_cast<void *>(pn->GetChild(op.b)) : reinterpret_cast<T_RetPI>(Slot(n, 0x30))(n, op.b)).c_str());
            break;
        case F_NODE_COUNT:
            Log("result %d", side ? pn->CountChildren(of) : OriginalAt<T_RetI>(op.c ? 0x006b8800 : 0x006b8710)(n));
            break;
        case F_NODE_OF_TYPE:
            Log("result %s", Path(side, side ? static_cast<void *>(pn->GetChildOfType(of, op.b, op.d != 0))
                                            : OriginalAt<T_RetPIB>(op.c ? 0x006b8830 : 0x006b8740)(n, op.b, op.d != 0)).c_str());
            break;
        case F_NODE_INDEX:
            Log("result %d", side ? pn->GetCurrentIndexOfType(of) : OriginalAt<T_RetI>(op.c ? 0x006b8890 : 0x006b87a0)(n));
            break;
        case F_MISSION_DEACTIVATE:
            if (mission)
                side ? static_cast<C_MissionDef *>(pn)->DeactivateObjectives() : OriginalAt<T_V>(0x006b86e0)(n);
            break;
        case F_MISSION_CREATE:
            if (mission) {
                side ? static_cast<C_MissionDef *>(pn)->CreateObjective() : reinterpret_cast<T_V>(Slot(n, 0x34))(n);
                if (!side && type == CAMPAIGN_NODE_MP_MISSION) {
                    // (The original's multiplayer objective leaves its team, texts and type unset: heap bytes; the port
                    // zeroes them.)
                    const std::vector<const uint8_t *> objectives = Elements(n, 0x30, side, 4);
                    void *made = At<void *>(objectives.back(), 0);
                    for (uint32_t o : {0x18u, 0x1Cu, 0x20u, 0x28u})
                        Put<int>(made, o, 0);
                }
            }
            break;
        case F_MISSION_CLEAR:
            // (Only the single and the multiplayer missions: a campaign's mission cleared would leave the cursors of
            // its parents pointing at nothing new; both do it the same.)
            if (mission)
                side ? pn->Clear() : reinterpret_cast<T_V>(Slot(n, 0x28))(n);
            break;
        default:
            break;
        }
        return;
    }
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
bool g_flow;   // part 78b's case
S_Op RandomFlowOp(int campaigns)
{
    S_Op op;
    op.kind = I(F_ADVANCE, F_COUNT - 1);
    if (op.kind == F_ADVANCE || op.kind == F_NEXT_STEP || op.kind == F_PREVIOUS)
        op.kind = Pick({int(F_ADVANCE), int(F_NEXT_STEP), int(F_NEXT_STEP), int(F_NEXT_STEP), int(F_PREVIOUS), int(F_PREVIOUS)});
    if (P(0.08))
        op.kind = F_NODE_PREV;   // (more Prevs: stepping back into a walked-through first child is rare)
    op.a = P(0.85) ? I(0, std::max(0, campaigns - 1)) : Pick({-1, campaigns, campaigns + 1, -5});
    op.b = P(0.8) ? I(0, 12) : Pick({-1, 30, 99});
    op.c = I(-1, 4);
    op.d = I(-1, 6);
    switch (op.kind) {
    case F_OBJECTIVE_STATUS:
        // (1-based campaign, operation and mission; 0-based objective.)
        // (Not 0: the original reads the pointer before the vector, the port's fix returns -1.)
        op.a = P(0.85) ? I(1, std::max(1, campaigns)) : Pick({-1, campaigns + 1});
        op.b = I(0, 5);
        op.c = I(0, 5);
        op.d = I(-1, 8);
        break;
    case F_SET_CURRENT:
        op.a = P(0.8) ? I(0, std::max(0, campaigns - 1)) : Pick({-1, campaigns, 7});
        break;
    case F_WORLD:
        op.a = int(U32());
        break;
    case F_NODE_NEXT:
    case F_NODE_PREV:
    case F_NODE_CURRENT:
    case F_NODE_RESET:
    case F_NODE_STATE:
    case F_NODE_CHILD:
    case F_NODE_COUNT:
    case F_NODE_OF_TYPE:
    case F_NODE_INDEX:
    case F_MISSION_DEACTIVATE:
    case F_MISSION_CREATE:
    case F_MISSION_CLEAR:
        op.a = P(0.6) ? I(0, 6) : I(0, 400);   // (mostly the first containers: the roots and their operations)
        op.b = I(-1, 8);
        op.c = P(0.5);
        op.d = P(0.5);
        break;
    default:
        break;
    }
    return op;
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
    if (g_flow) {
        // The flow: the files loaded, then its steps, queries, single and multiplayer missions, progress saves and loads
        // (a progress file built from the files' trees), the containers' own steps.
        if (std::getenv("HD2_CAMPAIGN_TRACE"))
            std::fprintf(stderr, "flow round %d\n", g_round);
        RandomFlowWorld();
        files["progress.sav"] = MakeProgress(files, version);
        ops.push_back(S_Op{OP_LOAD});
        const int campaigns = int(files.size()) - 1;
        for (int n = I(1, 30); n > 0; --n)
            ops.push_back(RandomFlowOp(campaigns));
        if (P(0.5))
            ops.push_back(S_Op{F_SAVE_GAME});
    } else {
        for (int n = I(1, 5); n > 0; --n)
            ops.push_back(S_Op{P(0.5) ? int(OP_LOAD) : P(0.6) ? int(OP_SAVE) : int(OP_CLEAR)});
        if (P(0.7))
            ops.push_back(S_Op{OP_SAVE});
    }
    const S_FlowWorld flowStart = g_f;
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_log.clear();
        g_files = files;
        g_openFiles.clear();
        g_nextFile = 1;
        g_f = flowStart;
        SetFlowWorld(side);
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
        {0x006ad830, reinterpret_cast<const void *>(O_GetCurrentMap)},
        {0x007e52d8, reinterpret_cast<const void *>(O_DbgPrintf)},
        {0x007e562e, reinterpret_cast<const void *>(O_Atexit)},
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
    int rounds = std::getenv("HD2_CAMPAIGN_QUICK") ? 1000 : 2000;
    if (const char *r = std::getenv("HD2_CAMPAIGN_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "Files";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    g_case = "Flow";
    g_flow = true;
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    g_flow = false;
    std::printf("campaign_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
