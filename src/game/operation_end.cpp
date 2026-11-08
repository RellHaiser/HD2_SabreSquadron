// The operation's end screen (game/operation_end.h, part 9g-i).
#include "game/operation_end.h"

#include "game/campaign.h"
#include "game/controls.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The texts: "CAMPAIGN COMPLETED", the operations' histories (17750 ..), "Press space to continue ...".
constexpr uint32_t TEXT_COMPLETED = 0x45BA, TEXT_PRESS_SPACE = 0x7F8;
constexpr uint32_t COLOUR = 0xFF000000;
constexpr uint32_t TITLE_STYLE = 0xA, BIG_TITLE_STYLE = 0x59, BAR_STYLE = 0xD, HISTORY_STYLE = 3;
constexpr int CONTROL_LABEL = 0x12, CONTROL_TEXT = 0x13;
// The keys (DirectInput's codes).
constexpr int KEY_ESCAPE = 1, KEY_SPACE = 0x39;
constexpr int ROWS_PER_CAMPAIGN = 10, OPERATIONS = 8;

}   // namespace

// 0x00841670 (the unused rows zero).
static const S_OperationEnd g_operationEnds[2 * ROWS_PER_CAMPAIGN] = {
    {"f_s_tutorial", COLOUR, true, TEXT_COMPLETED, 17760, -1},
    {"f_s_arctic", COLOUR, true, TEXT_COMPLETED, 17750, -1},
    {"f_s_africa", COLOUR, true, TEXT_COMPLETED, 17751, -1},
    {"f_s_norway", COLOUR, true, TEXT_COMPLETED, 17752, -1},
    {"f_s_burma", COLOUR, true, TEXT_COMPLETED, 17753, -1},
    {"f_s_alps", COLOUR, true, TEXT_COMPLETED, 17754, -1},
    {"f_s_norman", COLOUR, true, TEXT_COMPLETED, 17755, -1},
    {"", COLOUR, true, 0xFFFFFFFF, 0xFFFFFFFF, -1},
    {},
    {},
    {"f_s_LaRochel", COLOUR, true, TEXT_COMPLETED, 17756, -1},
    {"f_s_libye", COLOUR, true, TEXT_COMPLETED, 17757, -1},
    {"f_s_sicily", COLOUR, true, TEXT_COMPLETED, 17758, -1},
    {"f_s_burgundy", COLOUR, true, TEXT_COMPLETED, 17759, -1},
};

const S_OperationEnd *C_OperationEndMenu::GetOperationEnd()
{
    C_Campaign *campaign = g_gameProfile.m_campaign;
    const int current = campaign->GetCurrentIndex();
    if (current == 0 || current == 1) {
        const int operation = campaign->GetOperationIndex(current);
        if (operation >= 0 && operation < OPERATIONS)
            return &g_operationEnds[operation + current * ROWS_PER_CAMPAIGN];
    }
    return &g_operationEnds[0];
}

bool C_OperationEndMenu::BuildScreens()
{
    const char *file = GetModelFile();
    if (!file)
        return false;
    S_MenuDesc menu;
    menu.AddScreen("Campaign_debrief", file);
    for (S_ScreenDesc *desc : menu.screens) {
        I3D_frame *model = LoadModel(desc->name.c_str(), desc->file.c_str());
        if (!model)
            return false;
        if ((desc->id & 0xFC000000) == 1u << SCREEN_ID_SHIFT)
            DescribeScreen(desc, model);
        auto *screen = new C_Screen(m_scene, model);
        screen->Build(desc);
        screen->HandleMessages(desc->messages);
        m_screens.push_back(screen);
    }
    S_ControlMessage open;
    open.id = MENU_MSG_OPEN_SCREEN;
    open.value = 1;
    HandleMessage(&open);
    return true;
}

bool C_OperationEndMenu::DescribeScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    if (model->FindChildFrame("title", FRAME_MASK_VISUAL)) {
        S_ControlDesc *title = desc->AddControl(CONTROL_TEXT, "title");
        const S_OperationEnd *end = GetOperationEnd();
        AddDescMessage(title, 0, CONTROL_MSG_SET_TEXT_ID, end->bigTitle ? BIG_TITLE_STYLE : TITLE_STYLE,
                       end->titleText);
        AddDescMessage(title, 0, CONTROL_MSG_SET_COLOUR, GetOperationEnd()->colour);
    }
    if (model->FindChildFrame("textbar", FRAME_MASK_VISUAL))
        AddDescMessage(desc->AddControl(CONTROL_TEXT, "textbar"), 0, CONTROL_MSG_SET_TEXT_ID, BAR_STYLE,
                       TEXT_PRESS_SPACE);
    if (model->FindChildFrame("text01", FRAME_MASK_VISUAL)) {
        S_ControlDesc *history = desc->AddControl(CONTROL_LABEL, "text01");
        AddDescMessage(history, 0, CONTROL_MSG_SET_TEXT_ID, HISTORY_STYLE, GetOperationEnd()->historyText);
        AddDescMessage(history, 0, CONTROL_MSG_SET_COLOUR, GetOperationEnd()->colour);
    }
    return true;
}

bool C_OperationEndMenu::OnInput(float *, float *)
{
#ifndef HD2_DEDICATED   // no keyboard in LS3DRF.dll
    const int key = GetIGraph()->GetAsciiKey();
#else
    const int key = 0;
#endif
    if (key == KEY_ESCAPE || key == KEY_SPACE) {
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->Clear(0, 1.0f, 3);
        GetIGraph()->Flip();
#endif
        m_state = GAME_STATE_ENDED;
    }
    return true;
}
