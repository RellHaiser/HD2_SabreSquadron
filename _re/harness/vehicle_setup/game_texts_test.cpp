// Part 9g-iii's text database (game/game_texts.cpp) on its own: the archives' file functions faked over
// files in memory (their calls logged), the message boxes logged. Prints "gametexts:" with 1 per check
// met (build_texts.bat, check_gametexts.py; the arguments, the models' files, are not read).
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "game/game_texts.h"
#include "ls3d/rw_data.h"

namespace {

std::vector<std::string> g_log;
std::map<std::string, std::string> g_files;
std::map<int, std::string> g_open;
int g_nextHandle = 0;
bool g_seekFails = false, g_readShort = false;

std::string Format(const char *format, ...) __attribute__((format(printf, 1, 2)));
std::string Format(const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    return buffer;
}

} // namespace

int ShowMessage(const char *text, const char *title, unsigned int flags)
{
    g_log.push_back(Format("message %s | %s | %u", text, title, flags));
    return 0;
}

RW_DATA_API int RW_DATA_CALL dtaOpen(const char *fileName, bool archivesFirst)
{
    g_log.push_back(Format("open %s %d", fileName, archivesFirst));
    if (!g_files.count(fileName))
        return -1;
    g_open[g_nextHandle] = fileName;
    return g_nextHandle++;
}
RW_DATA_API int RW_DATA_CALL dtaSeek(int handle, int position, int origin)
{
    g_log.push_back(Format("seek %d %d %d", handle, position, origin));
    if (origin == 2)
        return static_cast<int>(g_files[g_open[handle]].size()) + position;
    return g_seekFails ? -1 : position;
}
RW_DATA_API int RW_DATA_CALL dtaRead(int handle, void *buffer, int size)
{
    g_log.push_back(Format("read %d %d", handle, size));
    const std::string &data = g_files[g_open[handle]];
    const int n = g_readShort ? size - 1 : size;
    std::memcpy(buffer, data.data(), static_cast<size_t>(n));
    return n;
}
RW_DATA_API bool RW_DATA_CALL dtaClose(int handle)
{
    g_log.push_back(Format("close %d", handle));
    g_open.erase(handle);
    return true;
}

namespace {

// The table: "id 'text' flag; ..".
std::string Dump(const C_GameTexts &texts)
{
    std::string s;
    for (const S_GameText &t : texts.GetTexts())
        s += Format("%d '%s' %d; ", t.id, t.text.c_str(), t.unknown8);
    return s;
}
std::string Log()
{
    std::string s;
    for (const std::string &line : g_log)
        s += line + "; ";
    g_log.clear();
    return s;
}
bool Expect(const char *name, const std::string &got, const std::string &want)
{
    if (got == want)
        return true;
    std::printf("%s:\n  got  %s\n  want %s\n", name, got.c_str(), want.c_str());
    return false;
}

} // namespace

int main()
{
    bool gtAdd = true, gtFind = true, gtGet = true, gtParse = true, gtLoad = true, gtBom = true, gtClear = true;

    // --- added in their ids' order; -1 and one already there refused (the first kept) ---
    {
        C_GameTexts t;
        gtAdd &= t.Add(20, "twenty", false) && t.Add(5, "five", true) && t.Add(30, "thirty", false) &&
                 t.Add(10, "ten", false) && t.Add(-5, "minus", false) && t.Add(0, "zero", false);
        gtAdd &= !t.Add(-1, "none", false) && !t.Add(10, "again", true) && !t.Add(5, "again", false);
        gtAdd &= Expect("add", Dump(t), "-5 'minus' 0; 0 'zero' 0; 5 'five' 1; 10 'ten' 0; 20 'twenty' 0; 30 'thirty' 0; ");
        // A null text: empty.
        gtAdd &= t.Add(40, nullptr, false) && t.Get(40) == "" && t.GetTexts().back().id == 40;
        // Many, in random order: sorted like a map's, each once.
        C_GameTexts many;
        std::map<int, std::string> want;
        unsigned seed = 12345;
        for (int i = 0; i < 2000; ++i) {
            seed = seed * 1103515245u + 12345u;
            const int id = static_cast<int>(seed >> 16) % 700 - 100;
            const std::string text = Format("t%d", i);
            const bool added = many.Add(id, text.c_str(), (i & 1) != 0);
            gtAdd &= added == (id != -1 && !want.count(id));
            if (added)
                want[id] = text;
        }
        std::string wanted;
        for (const auto &w : want)
            wanted += Format("%d %s; ", w.first, w.second.c_str());
        std::string got;
        for (const S_GameText &e : many.GetTexts())
            got += Format("%d %s; ", e.id, e.text.c_str());
        gtAdd &= Expect("many", got, wanted);
    }

    // --- found: the index (none: -1), has ---
    {
        C_GameTexts t;
        gtFind &= t.Find(3) == -1 && !t.Has(3) && t.Find(-1) == -1;
        t.Add(3, "a", false);
        t.Add(1, "b", false);
        t.Add(7, "c", false);
        gtFind &= t.Find(1) == 0 && t.Find(3) == 1 && t.Find(7) == 2 && t.Find(2) == -1 && t.Has(7) && !t.Has(8) &&
                  !t.Has(-1);
    }

    // --- the text, or the undefined index's ---
    {
        C_GameTexts t;
        t.Add(2052, "Item '%s'", false);
        gtGet &= t.Get(2052) == "Item '%s'" && t.Get(5) == "__undefined index <5> in all_txt __" &&
                 t.Get(-1) == "__undefined index <-1> in all_txt __";
    }

    // --- a file's lines: the breaks skipped; the id first (sscanf's %d), the text between the first and
    // the last quote; no number, no quotes or one: skipped ---
    {
        C_GameTexts t;
        g_files["t.txt"] = std::string("\r\n\r\n100 \"Hello\"\r\n") + "  \"no id\"\n" + "abc\n" + "200 no quotes\n" +
                           "201 \"one quote\n" + "5 \"a \"quoted\" b\" tail\n" + "-1 \"minus one\"\n" + "7\"glued\"\r" +
                           "8 \"\"\n" + "  12abc \"spaced\"\n" + "100 \"duplicate\"\n" + "\n\n" + "+9 \"plus\"\r\n" +
                           "0x10 \"hex\"\n" + "300 \"last\"";
        gtParse &= t.Load("t.txt") && Expect("parse", Dump(t),
                                             "0 'hex' 0; 5 'a \"quoted\" b' 0; 7 'glued' 0; 8 '' 0; 9 'plus' 0; "
                                             "12 'spaced' 0; 100 'Hello' 0; 300 'last' 0; ");
        // Only breaks, nothing; a single character.
        C_GameTexts u;
        g_files["breaks.txt"] = "\r\n\n\r";
        g_files["one.txt"] = "x";
        gtParse &= u.Load("breaks.txt") && u.Load("one.txt") && u.GetTexts().empty();
        g_files["quote.txt"] = "4 \"";
        gtParse &= u.Load("quote.txt") && u.GetTexts().empty();
        g_log.clear();
    }

    // --- loaded: opened from the disk first, its size, read whole, closed; added to what was there; not
    // there, empty, not read: the warning, false (an opened one closed) ---
    {
        C_GameTexts t;
        t.Add(1, "kept", false);
        g_files["a.txt"] = "2 \"two\"\n";
        g_log.clear();
        gtLoad &= t.Load("a.txt");
        const int h = g_nextHandle - 1;
        gtLoad &= Expect("load", Log(), Format("open a.txt 0; seek %d 0 2; seek %d 0 0; read %d 8; close %d; ", h, h, h, h)) &&
                  Expect("loaded", Dump(t), "1 'kept' 0; 2 'two' 0; ");
        gtLoad &= !t.Load("missing.txt") &&
                  Expect("missing", Log(), "open missing.txt 0; message Trying add file <missing.txt>; FAILED | Warning ... | 0; ");
        g_files["empty.txt"] = "";
        gtLoad &= !t.Load("empty.txt");
        const int e = g_nextHandle - 1;
        gtLoad &= Expect("empty", Log(), Format("open empty.txt 0; seek %d 0 2; message Trying add file <empty.txt>; "
                                                "FAILED | Warning ... | 0; close %d; ", e, e));
        g_seekFails = true;
        gtLoad &= !t.Load("a.txt");
        const int s = g_nextHandle - 1;
        gtLoad &= Expect("seek", Log(), Format("open a.txt 0; seek %d 0 2; seek %d 0 0; message Trying add file <a.txt>; "
                                               "FAILED | Warning ... | 0; close %d; ", s, s, s));
        g_seekFails = false;
        g_readShort = true;
        gtLoad &= !t.Load("a.txt");
        const int r = g_nextHandle - 1;
        gtLoad &= Expect("short", Log(), Format("open a.txt 0; seek %d 0 2; seek %d 0 0; read %d 8; message Trying add "
                                                "file <a.txt>; FAILED | Warning ... | 0; close %d; ", r, r, r, r));
        g_readShort = false;
        gtLoad &= Expect("unchanged", Dump(t), "1 'kept' 0; 2 'two' 0; ") && g_open.empty();
    }

    // --- the byte order marks: UTF-8's skipped; UTF-16's refused (its message, the warning) ---
    {
        C_GameTexts t;
        g_files["bom.txt"] = "\xEF\xBB\xBF" "3 \"three\"";
        g_files["bom2.txt"] = "\xEF\xBB" "4 \"four\"\n";
        g_files["utf16.txt"] = std::string("\xFF\xFE" "5 \"five\"\n");
        g_files["fffe.txt"] = std::string("\xFE\xFF" "6 \"six\"\n");
        gtBom &= t.Load("bom.txt") && t.Load("bom2.txt") && Expect("bom", Dump(t), "3 'three' 0; ");
        g_log.clear();
        gtBom &= !t.Load("utf16.txt");
        const int u = g_nextHandle - 1;
        gtBom &= Expect("utf16", Log(), Format("open utf16.txt 0; seek %d 0 2; seek %d 0 0; read %d 11; message Trying "
                                               "add file <utf16.txt>; Unicode format detected, not supporting now. | "
                                               "For your information ... | 0; message Trying add file <utf16.txt>; "
                                               "FAILED | Warning ... | 0; close %d; ", u, u, u, u));
        // (Its mark alone too.)
        g_files["utf16short.txt"] = std::string("\xFF\xFE");
        gtBom &= !t.Load("utf16short.txt") && g_log.size() == 7 && g_log[4].find("Unicode format") != std::string::npos;
        g_log.clear();
        // (Only FF FE is UTF-16's: FE FF is read, its line without a number.)
        gtBom &= t.Load("fffe.txt") && Expect("fffe", Dump(t), "3 'three' 0; ");
        g_files["bomonly.txt"] = "\xEF\xBB\xBF";
        g_files["short.txt"] = "\xEF";
        gtBom &= t.Load("bomonly.txt") && t.Load("short.txt") && Expect("short bom", Dump(t), "3 'three' 0; ");
        g_log.clear();
    }

    // --- cleared: every text gone; added again ---
    {
        C_GameTexts t;
        t.Add(1, "a", false);
        t.Add(2, "b", false);
        t.Clear();
        gtClear &= t.GetTexts().empty() && t.Find(1) == -1 && t.Add(2, "c", false) && Expect("clear", Dump(t), "2 'c' 0; ");
        t.Clear();
        t.Clear();
        gtClear &= t.GetTexts().empty();
    }

    std::printf("gametexts: gtAdd %d gtFind %d gtGet %d gtParse %d gtLoad %d gtBom %d gtClear %d\n", gtAdd, gtFind, gtGet,
                gtParse, gtLoad, gtBom, gtClear);
    return 0;
}
