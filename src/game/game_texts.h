// The game's texts (all_txt, g_texts; the video player's subtitles and credits use one of their own):
// the text files' lines `id "text"` (UTF-8, its byte order mark skipped; UTF-16 refused), kept sorted
// by id. Part 9g-iii.
#pragma once

#include <vector>

#include "common/c_str.h"

struct S_GameText {                                     // 0x0C bytes
    C_str text;                                         // +0x00
    int id = -1;                                        // +0x04
    bool unknown8 = false;                              // +0x08 (the files' lines: false)
};

class C_GameTexts {                                     // 0x10 bytes
public:
    C_GameTexts() = default;                            // 0x006e4f80 (__fastcall)
    // (Clear, the table freed.)
    ~C_GameTexts() = default;                           // 0x006e4fc0 (also 0x006e5790)
    C_GameTexts(const C_GameTexts &) = delete;
    C_GameTexts &operator=(const C_GameTexts &) = delete;

    // Every text gone.
    void Clear();                                       // 0x006e5050 (__fastcall)
    // A text file's lines added (from the archives or the disk); not read, empty, UTF-16 (its message
    // "For your information ..." first): "Trying add file <..>; FAILED" ("Warning ..."), false.
    bool Load(const char *fileName);                    // 0x006e50c0
    // The text's index (the last of the id; none: -1).
    int Find(int id) const;                             // 0x006e5330
    // Text `id`; "__undefined index <id> in all_txt __" if there is none.
    C_str Get(int id) const;                            // 0x006e5380
    bool Has(int id) const { return Find(id) != -1; }   // 0x006e53e0
    // A text added in its id's place (-1, one already there: false, nothing added).
    bool Add(int id, const char *text, bool unknown8);  // 0x006e5430
    const std::vector<S_GameText> &GetTexts() const { return m_texts; }

private:
    // The lines (between the line breaks; an empty one ends it), each parsed.
    void Parse(const char *data, int size);             // 0x006e56f0
    // A line's id (its number first) and its text (between its first and last quotes) added.
    void ParseLine(const char *line);                   // 0x006e5610

    std::vector<S_GameText> m_texts;                    // +0x04 sorted by id
};

extern C_GameTexts g_texts;                             // 0x008c1368
