// The operation's end (vtable 0x00815c90, 0x1B0 bytes; the campaign screen kind 6): the history of
// the operation just finished on its own model ("CAMPAIGN COMPLETED"), until Space or Escape. Part
// 9g-i.
#pragma once

#include <cstdint>

#include "game/menu_state.h"

class I3D_frame;
struct S_ScreenDesc;

// An operation's end screen (the table 0x00841670: ten rows per campaign, the first two campaigns;
// the first row the default).
struct S_OperationEnd {                                 // 0x18 bytes
    const char *model;                                  // +0x00 the screen's model ("f_s_arctic", ..)
    uint32_t colour;                                    // +0x04 the texts' (0xFF000000)
    bool bigTitle;                                      // +0x08 the title's style 0x59 (else 0xA)
    uint32_t titleText;                                 // +0x0C
    uint32_t historyText;                               // +0x10
    int unknown14;                                      // +0x14 (-1; not read)
};

class C_OperationEndMenu : public C_MenuState {
public:
    // (Inline in C_GameApp::StartStep; the destructor is the base's: 0x006c2170.)
    C_OperationEndMenu() = default;

    int GetTypeId() const override { return GAME_STATE_TYPE_OPERATION_END_MENU; }   // 0x1C 0x006c2160

    void OnMessage(S_ControlMessage *) override {}      // 0x34 0x0068c060
    // The screen (the operation's model: none in the table, its model not loaded: false), its texts,
    // opened. True.
    bool BuildScreens() override;                       // 0x38 0x0068c0d0
    void OnStop() override {}                           // 0x3C 0x0068c050
    bool OnStart(void *) override { return true; }      // 0x40 0x0068c040
    // Escape or Space: the screen cleared, flipped, the state ended (the campaign's next step). True.
    bool OnInput(float *held, float *pressed) override; // 0x44 0x0068c070

    // The row of the campaign played (the first two campaigns' operations 0 .. 7; else the first).
    static const S_OperationEnd *GetOperationEnd();     // 0x0068c000
    // The model's file of it (0x0068c0c0, inline).
    static const char *GetModelFile() { return GetOperationEnd()->model; }

private:
    // The title (its style, the text, the colour), the bar's "Press space to continue ...", the
    // history (the colour) where the model has their frames. True.
    bool DescribeScreen(S_ScreenDesc *desc, I3D_frame *model);   // 0x0068c3e0
};
