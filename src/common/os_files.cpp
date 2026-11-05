// File and directory wrappers, the date text (see os_files.h).
#include "common/os_files.h"

#include <cstdio>
#include <ctime>
#include <cstring>
#include <direct.h>
#include <windows.h>

#include "common/c_str.h"

namespace {

const char *const MONTHS[12] = {"01", "02", "03", "04", "05", "06",
                                "07", "08", "09", "10", "11", "12"};   // 0x008440d0

C_str g_dateText;                                   // 0x008cb444 (freed by 0x006edee0)
tm g_dateTime;                                      // 0x008cb44c

// The hour and the minute are the same.
bool SameMinute(const tm &a, const tm &b)           // 0x006edd10
{
    return a.tm_hour == b.tm_hour && a.tm_min == b.tm_min;
}

} // namespace

bool FileExists(const char *path)                   // 0x006edc30
{
    FILE *file = std::fopen(path, "r");
    if (!file)
        return false;
    std::fclose(file);
    return true;
}

int MakeDirectory(const char *path)                 // 0x006edc60
{
    return _mkdir(path);
}

int ChangeDirectory(const char *path)               // 0x006edc70
{
    return _chdir(path);
}

char *GetCurrentDir(char *buffer, int size)         // 0x006edc80
{
    return _getcwd(buffer, size);
}

bool RemoveDir(const char *path)                    // 0x006edca0
{
    return RemoveDirectoryA(path) != FALSE;
}

bool DeleteFileAt(const char *path)                 // 0x006edcb0
{
    return DeleteFileA(path) != FALSE;
}

bool SetAttributes(const char *path, uint32_t attributes)   // 0x006edcc0
{
    return SetFileAttributesA(path, attributes) != FALSE;
}

bool RenameFile(const char *from, const char *to)   // 0x006edc90
{
    return MoveFileA(from, to) != FALSE;
}

intptr_t FileFindFirst(const char *pattern, _finddata_t *data)   // 0x006edcd0
{
    return _findfirst(pattern, data);
}

int FileFindNext(intptr_t handle, _finddata_t *data)   // 0x006edce0
{
    return _findnext(handle, data);
}

void FileFindClose(intptr_t handle)                 // 0x006edcf0
{
    _findclose(handle);
}

bool GetDateString(const char **text, bool forFileName)   // 0x006edd30
{
    bool made = true;
    const time_t now = std::time(nullptr);
    const tm local = *std::localtime(&now);
    if (!SameMinute(g_dateTime, local)) {
        g_dateTime = local;
        if (!forFileName)
            g_dateText = C_fstr("%d. %s. %d  %02d:%02d", local.tm_mday, MONTHS[local.tm_mon],
                                local.tm_year + 1900, local.tm_hour, local.tm_min);
        else
            g_dateText = C_fstr("%d_%s_%d--%d_%d_%d", local.tm_mday, MONTHS[local.tm_mon],
                                local.tm_year + 1900, local.tm_hour, local.tm_min, local.tm_sec);
    } else {
        made = false;
    }
    *text = g_dateText.c_str();
    return made;
}

// ---- a directory's contents deleted (part 9i-v) ------------------------------------------------------------

bool DeleteDirectoryTree(const char *path)          // 0x006e9170
{
    constexpr int PATH_SIZE = 0x200;
    char saved[PATH_SIZE];
    GetCurrentDir(saved, PATH_SIZE);
    // Fix: a directory not entered: nothing deleted (the original went on in the current directory).
    if (ChangeDirectory(path) != 0)
        return false;
    _finddata_t found;
    const intptr_t handle = FileFindFirst("*.*", &found);
    if (handle == -1) {
        ChangeDirectory(saved);
        return false;
    }
    bool ok = true;
    do {
        if (std::strcmp(found.name, ".") == 0 || std::strcmp(found.name, "..") == 0)
            continue;
        if (found.attrib & _A_SUBDIR) {
            // (A subdirectory not emptied is left and the rest goes on; one not removed ends it.)
            ok = DeleteDirectoryTree(found.name);
            if (!ok)
                continue;
            if (!RemoveDir(found.name)) {
                SetAttributes(found.name, FILE_ATTRIBUTE_NORMAL);
                if (!RemoveDir(found.name)) {
                    ok = false;
                    break;
                }
            }
        } else if (!DeleteFileAt(found.name)) {
            SetAttributes(found.name, FILE_ATTRIBUTE_NORMAL);
            DeleteFileAt(found.name);
        }
    } while (FileFindNext(handle, &found) != -1);
    FileFindClose(handle);
    ChangeDirectory(saved);
    return ok;
}
