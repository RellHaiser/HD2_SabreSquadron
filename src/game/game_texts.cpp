// The game's texts (game/game_texts.h, part 9g-iii).
#include "game/game_texts.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "game/pending.h"                               // ShowMessage
#include "ls3d/rw_data.h"

C_GameTexts g_texts;

void C_GameTexts::Clear()
{
    m_texts.clear();
}

bool C_GameTexts::Load(const char *fileName)
{
    const int file = dtaOpen(fileName, false);
    std::vector<char> data;
    bool read = false;
    if (file != -1) {
        const int size = dtaSeek(file, 0, 2);
        if (size > 0) {
            data.resize(static_cast<size_t>(size));
            read = dtaSeek(file, 0, 0) == 0 && dtaRead(file, data.data(), size) == size;
        }
    }
    if (read) {
        const auto byte = [&data](size_t i) { return static_cast<unsigned char>(data[i]); };
        // (The original read the marks' bytes past a one- or two-byte file.)
        if (data.size() < 2 || byte(0) != 0xFF || byte(1) != 0xFE) {
            const bool mark = data.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF;
            const char *text = data.data() + (mark ? 3 : 0);
            Parse(text, static_cast<int>(data.data() + data.size() - text));
            // (Fix: the original closed the file only when its handle was 0.)
            dtaClose(file);
            return true;
        }
        ShowMessage(C_fstr("Trying add file <%s>; Unicode format detected, not supporting now.", fileName).c_str(),
                    "For your information ...", 0);
    }
    ShowMessage(C_fstr("Trying add file <%s>; FAILED", fileName).c_str(), "Warning ...", 0);
    if (file != -1)
        dtaClose(file);
    return false;
}

int C_GameTexts::Find(int id) const
{
    for (size_t i = m_texts.size(); i-- > 0;)
        if (m_texts[i].id == id)
            return static_cast<int>(i);
    return -1;
}

C_str C_GameTexts::Get(int id) const
{
    const int index = Find(id);
    if (index == -1)
        return C_fstr("__undefined index <%d> in all_txt __", id);
    return m_texts[static_cast<size_t>(index)].text;
}

bool C_GameTexts::Add(int id, const char *text, bool unknown8)
{
    if (id == -1 || Has(id))
        return false;
    S_GameText entry;
    entry.text = text;
    entry.id = id;
    entry.unknown8 = unknown8;
    // (The original appended it and moved it down before the first greater id: the table stays sorted.)
    const auto at = std::upper_bound(m_texts.begin(), m_texts.end(), id,
                                     [](int key, const S_GameText &t) { return key < t.id; });
    m_texts.insert(at, entry);
    return true;
}

void C_GameTexts::Parse(const char *data, int size)
{
    const char *const end = data + size;
    while (data < end) {
        while (*data == '\n' || *data == '\r') {
            if (++data >= end)
                return;
        }
        const char *stop = data;
        while (stop < end && *stop != '\n' && *stop != '\r')
            ++stop;
        if (stop == data)
            return;
        ParseLine(std::string(data, stop).c_str());
        data = stop;
    }
}

void C_GameTexts::ParseLine(const char *line)
{
    const char *first = std::strchr(line, '"');
    const char *last = std::strrchr(line, '"');
    if (!first || !last || first == last)
        return;
    int id = -1;
    const int read = std::sscanf(line, "%d", &id);
    const std::string text(first + 1, last);
    if (read != -1 && read != 0)
        Add(id, text.c_str(), false);
}
