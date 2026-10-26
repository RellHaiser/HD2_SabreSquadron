// Key=value text files (game.cfg, the old profile.pr) and the game directory (original source
// file 0x006e92a0..0x006e9e70).
#pragma once

#include <vector>

#include "common/c_str.h"

// The directory of the executable (cached; no trailing backslash).
const char *GetGameDirectory();                     // 0x006e92a0
// Makes <game directory>\a\b\c the working directory (b is only used with a, c only with b);
// false if it does not exist. (0, 0, 0): the game directory.
bool SetWorkingDirectory(const char *a, const char *b = nullptr, const char *c = nullptr);   // 0x006e92e0 (__fastcall)

// The lines of a text file. A key matches the first line that contains it anywhere (strstr),
// so "LASTSAVE" also finds a "LASTSAVETEXT=" line that comes first; the value is what
// follows the line's first '='.
class C_ConfigFile {
public:
    // Appends the file's non-empty lines (CR and LF both end a line); false if it cannot be
    // read or is empty.
    bool Load(C_str fileName);                      // 0x006e9600
    // Writes every line followed by CR LF.
    bool Save(C_str fileName);                      // 0x006e9880
    // Replaces the key's line with "key=value", or appends one.
    bool SetString(const char *key, const char *value);   // 0x006e9a60 (__fastcall; thunk 0x006e9c00)
    // The key's value as an int: only when the whole first word of the value is a number.
    bool GetInt(const char *key, int *value);       // 0x006e9cf0 (__fastcall)
    // Copies the key's value (to the end of its line).
    bool GetString(const char *key, char *value, int size);   // 0x006e9e40 (__fastcall; size added by the port)

private:
    // The value of the key's line, in a static buffer; null if there is none.
    const char *FindValue(const char *key);         // 0x006e9c10 (__fastcall)

    std::vector<C_str> m_lines;                     // +0x00
};
