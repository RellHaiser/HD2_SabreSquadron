"""Part 9i-v mutations: the profile deletion (common/os_files.cpp, game/player_profile.cpp).

Run with the profile harness (_re/harness/profile: "harness.exe delete" in a scratch directory; the scratchpad's
mutprof.py). Generated (deletions, comparison, number, bool and negation flips; the lines unique in the file).
Unsafe, left out: the three mutations of DeleteDirectoryTree's "." / ".." skip (a mutant recursing into
".." would delete outside the scratch directory).

Result: 42: 35 killed (6 after a second run: read-only directories, one held open), 3 not building, 4
equivalent: the find-first failure (an entered directory always lists ".") and its three lines, the find
handle's close (a leak).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
V = os.path.join(ROOT, r"src/common/os_files.cpp")
M = os.path.join(ROOT, r"src/game/player_profile.cpp")

MUTATIONS = [
    (V, 'V 54 cmp', '    return RemoveDirectoryA(path) != FALSE;\n', '    return RemoveDirectoryA(path) == FALSE;\n'),
    (V, 'V 59 cmp', '    return DeleteFileA(path) != FALSE;\n', '    return DeleteFileA(path) == FALSE;\n'),
    (V, 'V 64 cmp', '    return SetFileAttributesA(path, attributes) != FALSE;\n', '    return SetFileAttributesA(path, attributes) == FALSE;\n'),
    (V, 'V 113 del', '    GetCurrentDir(saved, PATH_SIZE);\n', '\n'),
    (V, 'V 115 cmp', '    if (ChangeDirectory(path) != 0)\n', '    if (ChangeDirectory(path) == 0)\n'),
    (V, 'V 115 num', '    if (ChangeDirectory(path) != 0)\n', '    if (ChangeDirectory(path) != 1)\n'),
    (V, 'V 116 bool', '        return false;\n    _finddata_t found;\n', '        return true;\n    _finddata_t found;\n'),
    (V, 'V 117 del', '    _finddata_t found;\n', '\n'),
    (V, 'V 118 del', '    const intptr_t handle = FileFindFirst("*.*", &found);\n', '\n'),
    (V, 'V 119 cmp', '    if (handle == -1) {\n', '    if (handle != -1) {\n'),
    (V, 'V 119 num', '    if (handle == -1) {\n', '    if (handle == -2) {\n'),
    (V, 'V 120 del', '        ChangeDirectory(saved);\n', '\n'),
    (V, 'V 121 bool', '        return false;\n    }\n', '        return true;\n    }\n'),
    (V, 'V 129 del', '            ok = DeleteDirectoryTree(found.name);\n', '\n'),
    (V, 'V 130 not', '            if (!ok)\n', '            if (ok)\n'),
    (V, 'V 131 del', '                continue;\n', '\n'),
    (V, 'V 132 not', '            if (!RemoveDir(found.name)) {\n                SetAttributes(found.name, FILE_ATTRIBUTE_NORMAL);\n', '            if (RemoveDir(found.name)) {\n                SetAttributes(found.name, FILE_ATTRIBUTE_NORMAL);\n'),
    (V, 'V 133 del', '                SetAttributes(found.name, FILE_ATTRIBUTE_NORMAL);\n', '\n'),
    (V, 'V 134 not', '                if (!RemoveDir(found.name)) {\n', '                if (RemoveDir(found.name)) {\n'),
    (V, 'V 135 del', '                    ok = false;\n', '\n'),
    (V, 'V 139 not', '        } else if (!DeleteFileAt(found.name)) {\n', '        } else if (DeleteFileAt(found.name)) {\n'),
    (V, 'V 140 del', '            SetAttributes(found.name, FILE_ATTRIBUTE_NORMAL);\n            DeleteFileAt(found.name);\n', '\n            DeleteFileAt(found.name);\n'),
    (V, 'V 141 del', '            DeleteFileAt(found.name);\n', '\n'),
    (V, 'V 143 del', '    } while (FileFindNext(handle, &found) != -1);\n', '\n'),
    (V, 'V 143 cmp', '    } while (FileFindNext(handle, &found) != -1);\n', '    } while (FileFindNext(handle, &found) == -1);\n'),
    (V, 'V 143 num', '    } while (FileFindNext(handle, &found) != -1);\n', '    } while (FileFindNext(handle, &found) != -2);\n'),
    (V, 'V 144 del', '    FileFindClose(handle);\n', '\n'),
    (V, 'V 145 del', '    ChangeDirectory(saved);\n    return ok;\n', '\n    return ok;\n'),
    (M, 'M 191 not', '    if (!*name)\n', '    if (*name)\n'),
    (M, 'M 193 del', '    SetWorkingDirectory(PROFILES_DIRECTORY);\n    // (With the unicode font a profile lives in its own directory.)\n', '\n    // (With the unicode font a profile lives in its own directory.)\n'),
    (M, 'M 197 del', '        directory = FindProfileDirectory(name);\n', '\n'),
    (M, 'M 199 not', '        if (!directory)\n', '        if (directory)\n'),
    (M, 'M 200 bool', '            return false;\n', '            return true;\n'),
    (M, 'M 204 not', '        if (!RemoveDir(directory)) {\n', '        if (RemoveDir(directory)) {\n'),
    (M, 'M 205 del', '            SetAttributes(directory, FILE_ATTRIBUTE_NORMAL);\n', '\n'),
    (M, 'M 206 del', '            RemoveDir(directory);\n', '\n'),
    (M, 'M 208 del', '        ForgetProfileName(name);\n', '\n'),
    (M, 'M 210 del', '    SetWorkingDirectory(nullptr);\n    return deleted;\n', '\n    return deleted;\n'),
    (M, 'M 217 cmp', '    if (it == g_profileDirectories.end())\n        return false;\n    g_profileDirectories.erase(it);\n', '    if (it != g_profileDirectories.end())\n        return false;\n    g_profileDirectories.erase(it);\n'),
    (M, 'M 218 bool', '        return false;\n    g_profileDirectories.erase(it);\n', '        return true;\n    g_profileDirectories.erase(it);\n'),
    (M, 'M 219 del', '    g_profileDirectories.erase(it);\n    return true;\n', '\n    return true;\n'),
    (M, 'M 220 bool', '    g_profileDirectories.erase(it);\n    return true;\n', '    g_profileDirectories.erase(it);\n    return false;\n'),
]
