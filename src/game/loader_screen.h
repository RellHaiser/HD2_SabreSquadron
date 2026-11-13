// The loading screen (game/loader_screen.cpp, original source 0x006c97b0..0x006caad0; the object
// at 0x008aea80): a client's "missions\<m>\loader" scene shown while the mission loads, its text
// boxes filled from the mission (the briefing, the title; a network map's players, objectives,
// game type and teams), the "loadbar" frame scaled to the progress. A dedicated server prints the
// progress instead. In single player the screen waits for the space key when it closes.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"

class C_MissionDef;
class C_TextBox;
class I3D_camera;
class I3D_frame;
class I3D_light;
class I3D_scene;

class C_LoaderScreen {
public:
    // The text boxes, by the loader scene's visuals (LOADER_TEXTS).
    static constexpr int TEXT_COUNT = 15;

    C_LoaderScreen() = default;                         // 0x006c97f0
    ~C_LoaderScreen();                                  // 0x006c9840
    C_LoaderScreen(const C_LoaderScreen &) = delete;
    C_LoaderScreen &operator=(const C_LoaderScreen &) = delete;

    // Starts the screen for mission `name` (the open one closed first): `show` (a client) loads
    // its scene and fills the texts from `mission` (may be null), the players added since the
    // last start among them (then forgotten); `waitForKey` (single player): the close waits for
    // space; `twoParts`: the progress spans the mission's loading and C_game::Start (the second
    // stage table; Finish closes it), else the loading alone (FinishLoading closes it). Without
    // `show` the progress is only counted (a dedicated server prints it).
    void Start(const char *name, bool show, bool waitForKey, bool twoParts,
               const C_MissionDef *mission);            // 0x006c9990
    // Progress `fraction` of loading stage `stage` (LOADER_STAGE_*): drawn when it moved on by
    // more than 0.003.
    void SetProgress(int stage, float fraction);        // 0x006c9920
    // The end of C_game::Start: closes a screen started in two parts.
    void Finish();                                      // 0x006c9910
    // The end of the mission's loading: closes a screen started in one part.
    void FinishLoading();                               // 0x006c9900
    // A player's line on a network map's screen (`team` 1: the first list, MP_list1; any other:
    // the second); null or empty: none.
    void AddPlayer(const char *text, int team);         // 0x006ca920

private:
    bool IsShown() const { return m_shown; }            // 0x006ca610
    // At 1 (drawn), the wait for the key, then everything let go.
    void Close();                                       // 0x006ca620 (DS 0x0070ee30)
    void Draw();                                        // 0x006ca7c0 (DS 0x0070ee60)
    void ClearPlayers();                                // 0x006ca9c0

    float m_progress = 0.0f;                            // +0x00 0..1
    bool m_shown = false;                               // +0x04 started (shown or not)
    bool m_waitForKey = false;                          // +0x05
    bool m_twoParts = false;                            // +0x06
    I3D_scene *m_scene = nullptr;                       // +0x08 the loader scene
    I3D_light *m_light = nullptr;                       // +0x0C its ambient light
    I3D_camera *m_camera = nullptr;                     // +0x10
    I3D_frame *m_loadBar = nullptr;                     // +0x14 "loadbar" (scaled in x)
    C_TextBox *m_texts[TEXT_COUNT] = {};                // +0x18
    // The players' names (AddPlayer): MP_list1's, MP_list2's.
    std::vector<C_str> m_players[2];                    // +0x54 / +0x64
};
extern C_LoaderScreen g_loaderScreen;                   // 0x008aea80 (static init 0x006c97b0)
// SetProgress's stages (the stage table's names; the share of the bar each takes: the first
// table's when the screen shows the loading alone, the second's when C_game::Start follows).
enum : int {
    LOADER_STAGE_SCENE = 0,                             // "scene" (0.3; 0.15)
    LOADER_STAGE_SCENE2 = 1,                            // "scene2.bin" (0.5; 0.25)
    LOADER_STAGE_ACTORS = 2,                            // "actors.bin" (0.05; 0.05)
    LOADER_STAGE_SOUNDS = 3,                            // "sounds.bin" (0.05; 0.05)
    LOADER_STAGE_STUFF = 4,                             // "stuff.bin": the rest of C_game::LoadMission (0.1; 0.05)
    LOADER_STAGE_MISSION_INIT = 5,                      // "mission init": C_game::Start (0; 0.45)
    LOADER_STAGE_COUNT = 6,
};
