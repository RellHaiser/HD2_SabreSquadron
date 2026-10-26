// Key=value text files and the game directory (see config_file.h).
#include "common/config_file.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

#include "common/os_files.h"
#include "ls3d/rw_data.h"

namespace {

char g_gameDirectory[MAX_PATH];                     // 0x008ca4d0
// FindValue's copy of the line it tests (size not recovered).
char g_configLine[1024];                            // 0x008ca5d8

} // namespace

const char *GetGameDirectory()                      // 0x006e92a0
{
    if (g_gameDirectory[0] == '\0') {
        GetModuleFileNameA(nullptr, g_gameDirectory, MAX_PATH);   // (0x006edd00)
        if (char *slash = std::strrchr(g_gameDirectory, '\\'))
            *slash = '\0';
    }
    return g_gameDirectory;
}

bool SetWorkingDirectory(const char *a, const char *b, const char *c)   // 0x006e92e0
{
    C_str path = GetGameDirectory();
    if (a) {
        path += C_fstr("\\%s", a);
        if (b) {
            path += C_fstr("\\%s", b);
            if (c)
                path += C_fstr("\\%s", c);
        }
    }
    return ChangeDirectory(path.c_str()) != -1;
}

// ---------------------------------------------------------------------------
// C_ConfigFile

bool C_ConfigFile::Load(C_str fileName)             // 0x006e9600
{
    const int file = dtaOpen(fileName.c_str(), 0);
    if (file == -1)
        return false;
    const int size = dtaSeek(file, 0, 2);
    if (size < 1) {
        dtaClose(file);
        return false;
    }
    char *buffer = new char[size];
    dtaSeek(file, 0, 0);
    if (dtaRead(file, buffer, size) != size) {
        dtaClose(file);
        delete[] buffer;
        return false;
    }
    for (int i = 0; i < size; ++i) {
        C_str line = "";
        for (; i < size && buffer[i] != '\n' && buffer[i] != '\r'; ++i)
            line += C_str(&buffer[i], 1);
        if (!line.IsEmpty())
            m_lines.push_back(line);
    }
    delete[] buffer;
    dtaClose(file);
    return true;
}

bool C_ConfigFile::Save(C_str fileName)             // 0x006e9880
{
    const int file = dtaOpenWrite(fileName.c_str(), 0x10000);
    if (file == -1)
        return false;
    for (const C_str &line : m_lines) {
        const int length = static_cast<int>(line.Size());
        if (dtaWrite(file, line.c_str(), length) != length || dtaWrite(file, "\r\n", 2) != 2) {
            dtaClose(file);
            return false;
        }
    }
    dtaClose(file);
    return true;
}

bool C_ConfigFile::SetString(const char *key, const char *value)   // 0x006e9a60 (__fastcall)
{
    for (C_str &line : m_lines) {
        if (std::strstr(line.c_str(), key)) {
            line = C_fstr("%s=%s", key, value);
            return true;
        }
    }
    m_lines.push_back(C_fstr("%s=%s", key, value));
    return true;
}

const char *C_ConfigFile::FindValue(const char *key)   // 0x006e9c10 (__fastcall)
{
    for (const C_str &line : m_lines) {
        // Fix: the line was copied to the static buffer without a size check.
        std::strncpy(g_configLine, line.c_str(), sizeof(g_configLine) - 1);
        g_configLine[sizeof(g_configLine) - 1] = '\0';
        if (std::strstr(g_configLine, key)) {
            // Fix: a matching line without '=' returned the address 1.
            const char *equals = std::strchr(g_configLine, '=');
            return equals ? equals + 1 : nullptr;
        }
    }
    return nullptr;
}

bool C_ConfigFile::GetInt(const char *key, int *value)  // 0x006e9cf0 (__fastcall)
{
    const char *text = FindValue(key);
    if (!text)
        return false;
    // Fix: the word went into a 16-byte buffer without a width (and stayed uninitialised
    // for an empty value).
    char word[16] = "";
    std::sscanf(text, "%15s", word);
    char *end = nullptr;
    const long number = std::strtol(word, &end, 10);
    if (static_cast<size_t>(end - word) != std::strlen(word))
        return false;
    *value = static_cast<int>(number);
    return true;
}

bool C_ConfigFile::GetString(const char *key, char *value, int size)   // 0x006e9e40 (__fastcall; size added by the port)
{
    const char *text = FindValue(key);
    if (!text)
        return false;
    // Fix: the original copied without a size (its callers have 100- and 1000-byte buffers).
    std::strncpy(value, text, size - 1);
    value[size - 1] = '\0';
    return true;
}
