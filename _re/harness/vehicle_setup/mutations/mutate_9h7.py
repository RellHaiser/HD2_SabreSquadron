"""Part 9h-vii mutations: the main menu's single-player screen
(game/main_menu.cpp).

Run with the factory build: --build build_factory.bat --check check_mainmenu.py (FACTORY_TEST; the
"mainmenu:" checks). Generated (deletions, comparison, number, bool and negation flips; the lines
unique in the file) and by hand (the modes, the styles, the messages).

68 mutations: all killed but 2 not building and 1 equivalent: V 750 num (a number in a comment).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
V = os.path.join(ROOT, r"src/game/main_menu.cpp")

MUTATIONS = [
    (V, 'V 736 del', '                   SINGLE_MISSION_CARNAGE = 0xC800000, SINGLE_BACK = 0xCC00000;\n', '\n'),
    (V, 'V 738 num', 'constexpr uint32_t STYLE_MODE_HEADING = 2, STYLE_MODE_TEXT = 3;\n', 'constexpr uint32_t STYLE_MODE_HEADING = 3, STYLE_MODE_TEXT = 3;\n'),
    (V, 'V 750 num', '    int campaign;                                       // (-1: a single mission)\n', '    int campaign;                                       // (-2: a single mission)\n'),
    (V, 'V 754 num', '    {0xC400000, 0, 0x8A3, 0, PLAY_MODE_CAMPAIGN},       // "bcampaign"\n', '    {0xC400000, 1, 0x8A3, 0, PLAY_MODE_CAMPAIGN},       // "bcampaign"\n'),
    (V, 'V 755 num', '    {0xC500000, 1, 0x8A4, 0, PLAY_MODE_LONE_WOLF},      // "blone wolf"\n', '    {0xC500000, 2, 0x8A4, 0, PLAY_MODE_LONE_WOLF},      // "blone wolf"\n'),
    (V, 'V 756 num', '    {0xC600000, 2, 0x8A5, 0, PLAY_MODE_CARNAGE},        // "carnage"\n', '    {0xC600000, 3, 0x8A5, 0, PLAY_MODE_CARNAGE},        // "carnage"\n'),
    (V, 'V 757 num', '    {0xC700000, 3, 0x8A6, -1, 0},                       // "bsingle mission"\n', '    {0xC700000, 4, 0x8A6, -1, 0},                       // "bsingle mission"\n'),
    (V, 'V 758 num', '    {0xC800000, 4, 0x8A7, -1, PLAY_MODE_CARNAGE},       // "bsingle mission- carnage"\n', '    {0xC800000, 5, 0x8A7, -1, PLAY_MODE_CARNAGE},       // "bsingle mission- carnage"\n'),
    (V, 'V 759 num', '    {0xC900000, 5, 0x8A3, 1, PLAY_MODE_CAMPAIGN},       // "bcampaign01" (the second campaign\'s)\n', '    {0xC900000, 6, 0x8A3, 1, PLAY_MODE_CAMPAIGN},       // "bcampaign01" (the second campaign\'s)\n'),
    (V, 'V 760 num', '    {0xCA00000, 6, 0x8A4, 1, PLAY_MODE_LONE_WOLF},      // "blone wolf01"\n', '    {0xCA00000, 7, 0x8A4, 1, PLAY_MODE_LONE_WOLF},      // "blone wolf01"\n'),
    (V, 'V 761 num', '    {0xCB00000, 7, 0x8A5, 1, PLAY_MODE_CARNAGE},        // "carnage01"\n', '    {0xCB00000, 8, 0x8A5, 1, PLAY_MODE_CARNAGE},        // "carnage01"\n'),
    (V, 'V 769 del', '    self->m_saveName = C_str("");\n    PostTo(SINGLE_MISSION, CONTROL_MSG_ACTIVATE, 0);\n', '\n    PostTo(SINGLE_MISSION, CONTROL_MSG_ACTIVATE, 0);\n'),
    (V, 'V 770 del', '    PostTo(SINGLE_MISSION, CONTROL_MSG_ACTIVATE, 0);\n', '\n'),
    (V, 'V 770 num', '    PostTo(SINGLE_MISSION, CONTROL_MSG_ACTIVATE, 0);\n', '    PostTo(SINGLE_MISSION, CONTROL_MSG_ACTIVATE, 1);\n'),
    (V, 'V 771 del', '    PostTo(SINGLE_MISSION_CARNAGE, CONTROL_MSG_ACTIVATE, 0);\n', '\n'),
    (V, 'V 771 num', '    PostTo(SINGLE_MISSION_CARNAGE, CONTROL_MSG_ACTIVATE, 0);\n', '    PostTo(SINGLE_MISSION_CARNAGE, CONTROL_MSG_ACTIVATE, 1);\n'),
    (V, 'V 772 bool', '    PostTo(SINGLE_MISSION_CARNAGE, CONTROL_MSG_ACTIVATE, 0);\n    return true;\n', '    PostTo(SINGLE_MISSION_CARNAGE, CONTROL_MSG_ACTIVATE, 0);\n    return false;\n'),
    (V, 'V 777 not', '    if (!control)\n        return false;\n    PostTo(SINGLE_TEXT, CONTROL_MSG_CLEAR, 0);\n', '    if (control)\n        return false;\n    PostTo(SINGLE_TEXT, CONTROL_MSG_CLEAR, 0);\n'),
    (V, 'V 778 bool', '        return false;\n    PostTo(SINGLE_TEXT, CONTROL_MSG_CLEAR, 0);\n', '        return true;\n    PostTo(SINGLE_TEXT, CONTROL_MSG_CLEAR, 0);\n'),
    (V, 'V 779 del', '    PostTo(SINGLE_TEXT, CONTROL_MSG_CLEAR, 0);\n', '\n'),
    (V, 'V 779 num', '    PostTo(SINGLE_TEXT, CONTROL_MSG_CLEAR, 0);\n', '    PostTo(SINGLE_TEXT, CONTROL_MSG_CLEAR, 1);\n'),
    (V, 'V 781 del', '    const S_SingleMode *mode = nullptr;\n', '\n'),
    (V, 'V 783 cmp', '        if (known.button == id)\n', '        if (known.button != id)\n'),
    (V, 'V 784 del', '            mode = &known;\n', '\n'),
    (V, 'V 786 cmp', '    if (notification == CONTROL_NOTIFY_ENTER) {\n', '    if (notification != CONTROL_NOTIFY_ENTER) {\n'),
    (V, 'V 788 cmp', '        if (id == SINGLE_BACK) {\n', '        if (id != SINGLE_BACK) {\n'),
    (V, 'V 789 del', '            PostTo(SINGLE_SIDE, CONTROL_MSG_HIDE_FRAME, 0);\n', '\n'),
    (V, 'V 789 num', '            PostTo(SINGLE_SIDE, CONTROL_MSG_HIDE_FRAME, 0);\n', '            PostTo(SINGLE_SIDE, CONTROL_MSG_HIDE_FRAME, 1);\n'),
    (V, 'V 790 bool', '            return true;\n        }\n        if (!mode)\n', '            return false;\n        }\n        if (!mode)\n'),
    (V, 'V 792 not', '        if (!mode)\n', '        if (mode)\n'),
    (V, 'V 793 bool', '            return false;\n', '            return true;\n'),
    (V, 'V 794 del', '        PostTo(SINGLE_SIDE, CONTROL_MSG_SELECT_FRAME, static_cast<uint32_t>(mode->frame));\n', '\n'),
    (V, 'V 795 del', '        PostTo(SINGLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, TEXT_MODE_HEADING);\n', '\n'),
    (V, 'V 796 del', '        PostTo(SINGLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_HEADING, TEXT_EMPTY);\n', '\n'),
    (V, 'V 797 del', '        PostTo(SINGLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_TEXT, mode->text);\n', '\n'),
    (V, 'V 798 bool', '        return true;\n    }\n    if (notification != CONTROL_NOTIFY_CLICK)\n', '        return false;\n    }\n    if (notification != CONTROL_NOTIFY_CLICK)\n'),
    (V, 'V 800 cmp', '    if (notification != CONTROL_NOTIFY_CLICK)\n', '    if (notification == CONTROL_NOTIFY_CLICK)\n'),
    (V, 'V 801 bool', '        return false;\n\n', '        return true;\n\n'),
    (V, 'V 804 del', '    PostTo(SINGLE_SIDE, CONTROL_MSG_HIDE_FRAME, 0);\n    g_mpSettings->sessionType = SESSION_NONE;\n', '\n    g_mpSettings->sessionType = SESSION_NONE;\n'),
    (V, 'V 804 num', '    PostTo(SINGLE_SIDE, CONTROL_MSG_HIDE_FRAME, 0);\n    g_mpSettings->sessionType = SESSION_NONE;\n', '    PostTo(SINGLE_SIDE, CONTROL_MSG_HIDE_FRAME, 1);\n    g_mpSettings->sessionType = SESSION_NONE;\n'),
    (V, 'V 805 del', '    g_mpSettings->sessionType = SESSION_NONE;\n', '\n'),
    (V, 'V 806 cmp', '    if (id == SINGLE_BACK) {\n        Post(MENU_MSG_BACK, 0);\n', '    if (id != SINGLE_BACK) {\n        Post(MENU_MSG_BACK, 0);\n'),
    (V, 'V 807 del', '        Post(MENU_MSG_BACK, 0);\n        return true;\n', '\n        return true;\n'),
    (V, 'V 807 num', '        Post(MENU_MSG_BACK, 0);\n        return true;\n', '        Post(MENU_MSG_BACK, 1);\n        return true;\n'),
    (V, 'V 808 bool', '        return true;\n    }\n    if (!mode)\n', '        return false;\n    }\n    if (!mode)\n'),
    (V, 'V 810 not', '    if (!mode)\n        return false;\n', '    if (mode)\n        return false;\n'),
    (V, 'V 811 bool', '        return false;\n    if (mode->campaign >= 0) {\n', '        return true;\n    if (mode->campaign >= 0) {\n'),
    (V, 'V 812 cmp', '    if (mode->campaign >= 0) {\n', '    if (mode->campaign > 0) {\n'),
    (V, 'V 812 num', '    if (mode->campaign >= 0) {\n', '    if (mode->campaign >= 1) {\n'),
    (V, 'V 813 del', '        g_gameProfile.m_campaign->SetCurrentIndex(mode->campaign);\n', '\n'),
    (V, 'V 814 del', '        g_gameProfile.m_playMode->singleMission = 0;\n', '\n'),
    (V, 'V 814 num', '        g_gameProfile.m_playMode->singleMission = 0;\n', '        g_gameProfile.m_playMode->singleMission = 1;\n'),
    (V, 'V 815 del', '        g_gameProfile.m_playMode->mode = mode->playMode;\n        Post(FROM_SCREEN | MAIN_MSG_START_CAMPAIGN, 0);\n', '\n        Post(FROM_SCREEN | MAIN_MSG_START_CAMPAIGN, 0);\n'),
    (V, 'V 816 del', '        Post(FROM_SCREEN | MAIN_MSG_START_CAMPAIGN, 0);\n', '\n'),
    (V, 'V 816 num', '        Post(FROM_SCREEN | MAIN_MSG_START_CAMPAIGN, 0);\n', '        Post(FROM_SCREEN | MAIN_MSG_START_CAMPAIGN, 1);\n'),
    (V, 'V 818 del', '        g_gameProfile.m_playMode->singleMission = 1;\n', '\n'),
    (V, 'V 818 num', '        g_gameProfile.m_playMode->singleMission = 1;\n', '        g_gameProfile.m_playMode->singleMission = 2;\n'),
    (V, 'V 819 del', '        g_gameProfile.m_playMode->mode = mode->playMode;\n        Post(FROM_SCREEN | MAIN_MSG_SINGLE_MISSIONS, 0);\n', '\n        Post(FROM_SCREEN | MAIN_MSG_SINGLE_MISSIONS, 0);\n'),
    (V, 'V 820 del', '        Post(FROM_SCREEN | MAIN_MSG_SINGLE_MISSIONS, 0);\n', '\n'),
    (V, 'V 820 num', '        Post(FROM_SCREEN | MAIN_MSG_SINGLE_MISSIONS, 0);\n', '        Post(FROM_SCREEN | MAIN_MSG_SINGLE_MISSIONS, 1);\n'),
    (V, 'V 822 bool', '        Post(FROM_SCREEN | MAIN_MSG_SINGLE_MISSIONS, 0);\n    }\n    return true;\n', '        Post(FROM_SCREEN | MAIN_MSG_SINGLE_MISSIONS, 0);\n    }\n    return false;\n'),
    (V, 'V 754 arg0', '    {0xC400000, 0, 0x8A3, 0, PLAY_MODE_CAMPAIGN},       // "bcampaign"', '    {0xC400000, 0, 0x8A3, 1, PLAY_MODE_CAMPAIGN},       // "bcampaign"'),
    (V, 'V 758 arg1', '    {0xC800000, 4, 0x8A7, -1, PLAY_MODE_CARNAGE},       // "bsingle mission- carnage"', '    {0xC800000, 4, 0x8A7, -1, 0},       // "bsingle mission- carnage"'),
    (V, 'V 738 arg2', 'constexpr uint32_t STYLE_MODE_HEADING = 2, STYLE_MODE_TEXT = 3;', 'constexpr uint32_t STYLE_MODE_HEADING = 3, STYLE_MODE_TEXT = 3;'),
    (V, 'V 742 arg3', 'constexpr uint32_t MAIN_MSG_START_CAMPAIGN = 0x2000033, MAIN_MSG_SINGLE_MISSIONS = 0x2000074;', 'constexpr uint32_t MAIN_MSG_START_CAMPAIGN = 0x2000074, MAIN_MSG_SINGLE_MISSIONS = 0x2000033;'),
    (V, 'V 794 arg4', '        PostTo(SINGLE_SIDE, CONTROL_MSG_SELECT_FRAME, static_cast<uint32_t>(mode->frame));', '        PostTo(SINGLE_SIDE, CONTROL_MSG_SELECT_FRAME, 0);'),
    (V, 'V 797 arg5', '        PostTo(SINGLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_TEXT, mode->text);', '        PostTo(SINGLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, STYLE_MODE_TEXT, TEXT_MODE_HEADING);'),
    (V, 'V 805 arg6', '    g_mpSettings->sessionType = SESSION_NONE;', '    g_mpSettings->sessionType = SESSION_JOINING;'),
]
