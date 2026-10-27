// Harness stub of game/game_app.h.
#pragma once

#include "common/c_str.h"

class C_GameApp {
public:
    bool GetQuickSaveMission(const C_str &fileName, int *mission);
};
C_GameApp *GetGameApp();
