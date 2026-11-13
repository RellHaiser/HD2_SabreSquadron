// Thin wrappers of the C runtime's file and directory functions, and the date text (original
// source file 0x006edc30..0x006edf00; all __fastcall).
#pragma once

#include <cstdint>
#include <io.h>

// The file can be opened for reading.
bool FileExists(const char *path);                  // 0x006edc30
int MakeDirectory(const char *path);                // 0x006edc60 (_mkdir: 0 or -1)
int ChangeDirectory(const char *path);              // 0x006edc70 (_chdir: 0 or -1)
// The current directory into `buffer` (_getcwd; null when it does not fit; part 9i-v).
char *GetCurrentDir(char *buffer, int size);        // 0x006edc80 (DS 0x00739140)
bool RenameFile(const char *from, const char *to);  // 0x006edc90 (MoveFileA)
// RemoveDirectoryA, DeleteFileA, SetFileAttributesA: done (part 9i-v).
bool RemoveDir(const char *path);                   // 0x006edca0 (DS 0x00739180)
bool DeleteFileAt(const char *path);                // 0x006edcb0 (DS 0x007391a0)
bool SetAttributes(const char *path, uint32_t attributes);   // 0x006edcc0 (DS 0x007391c0)
// _findfirst / _findnext / _findclose.
intptr_t FileFindFirst(const char *pattern, _finddata_t *data);   // 0x006edcd0
int FileFindNext(intptr_t handle, _finddata_t *data);             // 0x006edce0
void FileFindClose(intptr_t handle);                              // 0x006edcf0

// Everything in directory `path` deleted (read-only too; its directories first emptied, then removed); the
// current directory kept. False when a directory could not be removed (the rest left), when nothing was
// found, or (fix) when `path` could not be entered (part 9i-v).
bool DeleteDirectoryTree(const char *path);         // 0x006e9170 (DS 0x00733350)

// The local date and time as "d. mm. yyyy  hh:mm", or for a file name "d_mm_yyyy--h_m_s".
// The text is kept and only made again when the hour or the minute changed (whichever format
// was asked for then); returns whether it was made anew.
bool GetDateString(const char **text, bool forFileName);          // 0x006edd30
