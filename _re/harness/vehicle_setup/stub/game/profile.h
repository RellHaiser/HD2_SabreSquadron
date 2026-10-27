// Harness stub of game/profile.h (the real one is too wide): what the car's updates ask of the game
// profile (game/car_update.cpp) and the spawn point's (game/spawn_point.cpp; part 7b), under the
// real names.
#pragma once

#include "game/pending.h"                               // S_MultiplayerSettings
#include "game/soldier.h"

// The detail settings (the spawn point asks for the voice chat).
struct S_DetailSettings {
    bool IsVoiceChatEnabled() const { return voiceChat; }
    bool voiceChat = false;
};

class C_GameProfile {
public:
    bool IsCooperativeGame() const;
    S_DetailSettings *GetDetailSettings();
};
extern C_GameProfile g_gameProfile;
extern S_MultiplayerSettings *g_mpSettings;             // (a reference into g_gameProfile in the real header)

// The squad soldier's slots (S_SquadSoldier::PutItem).
enum : uint8_t {
    SQUAD_SLOT_UNIFORM = 0x02,
};
struct S_SquadSoldier : S_SoldierState {
    void SplitStacks();
    void PutItem(S_SoldierItem *displaced, uint8_t slot, const S_SoldierItem &item);
    uint8_t load[0x20] = {};                            // +0x8C (S_SoldierLoad)
};
