// Harness stub of game/pending.h: what the profile layer's files use.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"

class C_GameTexts {
public:
    C_str Get(int id) const;
    void Load(const char *fileName);
};
extern C_GameTexts g_texts;

bool IsLegalUtf8(const uint8_t *begin, const uint8_t *end);
void WideToAnsi(char *out, const wchar_t *text, int maxLength);

class C_Controls {
public:
    void Bind(int action, int16_t device, uint16_t key, int unknown, int direction);
    bool SetMouseScale(int x, int y, int z);
    void SetInvertMouse(bool on);
};
extern C_Controls g_controls;

class C_NetGame {
public:
    void SetVoiceValue(int value);
};
extern C_NetGame g_netGame;

struct S_MultiplayerSettings {
    int sessionType = 0;
};
