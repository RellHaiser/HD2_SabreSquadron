"""Part 9h-xiv mutations: the main menu's create server screen callbacks
(game/main_menu.cpp).

Run with the factory build: --build build_factory.bat --check check_mainmenu.py (FACTORY_TEST; the
"mainmenu:" checks). Generated (deletions, comparison, number, bool and negation flips; the lines
unique in the file) and by hand (the ids, the messages, the comparisons). Also game/mp_game_setup.cpp's new
methods (M).

63 mutations: 51 killed, 11 not building, 1 equivalent:
- M 644 del: SelectRotation's Clear of the first rotation; its four lists are replaced right after.
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
V = os.path.join(ROOT, r"src/game/main_menu.cpp")
M = os.path.join(ROOT, r"src/game/mp_game_setup.cpp")

MUTATIONS = [
    (V, 'V 2121 del', '                   CREATE_VOICE = 0x24800000, CREATE_START = 0x24900000, CREATE_PRESET = 0x25200000;\n', '\n'),
    (V, 'V 2123 num', 'constexpr int LISTING_INTERNET = 1;\n', 'constexpr int LISTING_INTERNET = 2;\n'),
    (V, 'V 2130 del', '    self->m_mpSettings->Load();\n', '\n'),
    (V, 'V 2131 del', '    const C_MpGameSetup &setup = self->m_mpSettings->m_setup;\n', '\n'),
    (V, 'V 2132 del', '    PostText(CREATE_HOST, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, setup.serverName);\n', '\n'),
    (V, 'V 2133 del', '    PostTo(CREATE_STYLE, CONTROL_MSG_SELECT_ITEM, static_cast<uint32_t>(setup.gameType));\n', '\n'),
    (V, 'V 2134 del', '    PostTo(CREATE_INTERNET, CONTROL_MSG_SET_CHECKED_QUIET, g_mpSettings->unknown174 == LISTING_INTERNET);\n', '\n'),
    (V, 'V 2134 cmp', '    PostTo(CREATE_INTERNET, CONTROL_MSG_SET_CHECKED_QUIET, g_mpSettings->unknown174 == LISTING_INTERNET);\n', '    PostTo(CREATE_INTERNET, CONTROL_MSG_SET_CHECKED_QUIET, g_mpSettings->unknown174 != LISTING_INTERNET);\n'),
    (V, 'V 2135 del', '    PostTo(CREATE_DEDICATED, CONTROL_MSG_SET_CHECKED_QUIET, setup.dedicated);\n', '\n'),
    (V, 'V 2136 del', '    PostText(CREATE_PASSWORD, CONTROL_MSG_SET_NUMBER, STYLE_BUTTON, setup.password);\n', '\n'),
    (V, 'V 2137 del', '    PostTo(CREATE_PORT, CONTROL_MSG_SET_NUMBER, STYLE_BUTTON, static_cast<uintptr_t>(setup.port));\n', '\n'),
    (V, 'V 2138 del', '    PostTo(CREATE_VOICE, CONTROL_MSG_SELECT_ITEM, static_cast<uint32_t>(setup.voiceCodec));\n', '\n'),
    (V, 'V 2139 del', '    PostTo(CREATE_PRESET, CONTROL_MSG_PICK_INDEX, static_cast<uint32_t>(setup.rotation));\n', '\n'),
    (V, 'V 2140 bool', '    PostTo(CREATE_PRESET, CONTROL_MSG_PICK_INDEX, static_cast<uint32_t>(setup.rotation));\n    return true;\n', '    PostTo(CREATE_PRESET, CONTROL_MSG_PICK_INDEX, static_cast<uint32_t>(setup.rotation));\n    return false;\n'),
    (V, 'V 2145 del', '    static_cast<C_MainMenu *>(menu)->m_mpSettings->Save();\n', '\n'),
    (V, 'V 2147 bool', '    // (The original returns what was left in AL; the notification drops it.)\n    return true;\n', '    // (The original returns what was left in AL; the notification drops it.)\n    return false;\n'),
    (V, 'V 2153 cmp', '    if (notification == CONTROL_NOTIFY_0B) {\n', '    if (notification != CONTROL_NOTIFY_0B) {\n'),
    (V, 'V 2154 del', '        C_str &name = self->m_mpSettings->m_setup.serverName;\n', '\n'),
    (V, 'V 2155 del', '        const C_str typed = ControlText(control);\n', '\n'),
    (V, 'V 2159 del', '            restore.id = CONTROL_MSG_SET_TEXT;\n', '\n'),
    (V, 'V 2160 del', '            restore.value = STYLE_BUTTON;\n', '\n'),
    (V, 'V 2161 del', '            restore.text = name;\n', '\n'),
    (V, 'V 2162 del', '            control->HandleMessage(&restore);\n', '\n'),
    (V, 'V 2164 del', '            name = typed;\n', '\n'),
    (V, 'V 2166 del', '        }\n        self->RefreshStartButton();\n', '        }\n\n'),
    (V, 'V 2168 bool', '        }\n        self->RefreshStartButton();\n    }\n    return true;\n', '        }\n        self->RefreshStartButton();\n    }\n    return false;\n'),
    (V, 'V 2176 del', '        ask.id = CONTROL_MSG_GET_TEXT;\n        control->HandleMessage(&ask);\n', '\n        control->HandleMessage(&ask);\n'),
    (V, 'V 2177 del', '        control->HandleMessage(&ask);\n        self->m_mpSettings->m_setup.SelectRotation(static_cast<int>(ask.value));\n', '\n        self->m_mpSettings->m_setup.SelectRotation(static_cast<int>(ask.value));\n'),
    (V, 'V 2178 del', '        self->m_mpSettings->m_setup.SelectRotation(static_cast<int>(ask.value));\n', '\n'),
    (V, 'V 2179 del', '        self->ShowPickedMaps();\n', '\n'),
    (V, 'V 2180 del', '        self->ShowPickedMaps();\n        self->RefreshStartButton();\n', '        self->ShowPickedMaps();\n\n'),
    (V, 'V 2182 bool', '        self->ShowPickedMaps();\n        self->RefreshStartButton();\n    }\n    return true;\n', '        self->ShowPickedMaps();\n        self->RefreshStartButton();\n    }\n    return false;\n'),
    (V, 'V 2189 not', '    if (!m_hint.m_on)\n        Post(CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_LEAVE);\n', '    if (m_hint.m_on)\n        Post(CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_LEAVE);\n'),
    (V, 'V 2190 del', '        Post(CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_LEAVE);\n', '\n'),
    (V, 'V 2192 del', '        current = static_cast<uint32_t>(m_hint.m_unknown00);\n', '\n'),
    (V, 'V 2193 del', '    PostTo(CREATE_PICKED_LIST, CONTROL_MSG_HIDE_ALL, 0);\n', '\n'),
    (V, 'V 2193 num', '    PostTo(CREATE_PICKED_LIST, CONTROL_MSG_HIDE_ALL, 0);\n', '    PostTo(CREATE_PICKED_LIST, CONTROL_MSG_HIDE_ALL, 1);\n'),
    (V, 'V 2194 del', '    const std::vector<C_MpMap *> maps = m_mpSettings->m_setup.GetCurrentGroup().maps;\n', '\n'),
    (V, 'V 2195 cmp', '    for (uint32_t i = 0; i < maps.size(); ++i)\n', '    for (uint32_t i = 0; i <= maps.size(); ++i)\n'),
    (V, 'V 2195 num', '    for (uint32_t i = 0; i < maps.size(); ++i)\n', '    for (uint32_t i = 1; i < maps.size(); ++i)\n'),
    (V, 'V 2196 cmp', '        PostTo(CREATE_PICKED_LIST, i != current ? CONTROL_MSG_ADD_ENTRY : CONTROL_MSG_ADD_CURRENT_ENTRY, i,\n', '        PostTo(CREATE_PICKED_LIST, i == current ? CONTROL_MSG_ADD_ENTRY : CONTROL_MSG_ADD_CURRENT_ENTRY, i,\n'),
    (V, 'V 2198 not', '    if (!m_hint.m_on)\n        Post(CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);\n', '    if (m_hint.m_on)\n        Post(CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);\n'),
    (V, 'V 2199 del', '        Post(CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);\n', '\n'),
    (V, 'V 2204 del', '    const C_MpGameSetup &setup = m_mpSettings->m_setup;\n', '\n'),
    (V, 'V 2205 del', '    m_startable = !setup.GetCurrentGroup().maps.empty() && !setup.serverName.IsEmpty();\n', '\n'),
    (V, 'V 2205 cmp', '    m_startable = !setup.GetCurrentGroup().maps.empty() && !setup.serverName.IsEmpty();\n', '    m_startable = !setup.GetCurrentGroup().maps.empty() || !setup.serverName.IsEmpty();\n'),
    (V, 'V 2206 del', '    PostTo(CREATE_START, m_startable ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE, 0);\n', '\n'),
    (V, 'V 2206 num', '    PostTo(CREATE_START, m_startable ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE, 0);\n', '    PostTo(CREATE_START, m_startable ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE, 1);\n'),
    (M, 'M 626 del', '    S_MpRotation &current = rotations[rotation];\n', '\n'),
    (M, 'M 637 cmp', '    if (index < 0 || index >= MP_ROTATIONS)\n', '    if (index <= 0 || index >= MP_ROTATIONS)\n'),
    (M, 'M 637 num', '    if (index < 0 || index >= MP_ROTATIONS)\n', '    if (index < 1 || index >= MP_ROTATIONS)\n'),
    (M, 'M 639 del', '    rotation = index;\n', '\n'),
    (M, 'M 640 cmp', '    if (index != 0)\n', '    if (index == 0)\n'),
    (M, 'M 640 num', '    if (index != 0)\n', '    if (index != 1)\n'),
    (M, 'M 643 del', '    S_MpRotation &first = rotations[0];\n', '\n'),
    (M, 'M 643 num', '    S_MpRotation &first = rotations[0];\n', '    S_MpRotation &first = rotations[1];\n'),
    (M, 'M 644 del', '    first.Clear();\n', '\n'),
    (M, 'M 645 cmp', '    for (int type = 0; type < MP_GAME_TYPES; ++type)\n        first.types[type].maps = maps[type].maps;\n', '    for (int type = 0; type <= MP_GAME_TYPES; ++type)\n        first.types[type].maps = maps[type].maps;\n'),
    (M, 'M 645 num', '    for (int type = 0; type < MP_GAME_TYPES; ++type)\n        first.types[type].maps = maps[type].maps;\n', '    for (int type = 1; type < MP_GAME_TYPES; ++type)\n        first.types[type].maps = maps[type].maps;\n'),
    (M, 'M 646 del', '        first.types[type].maps = maps[type].maps;\n', '\n'),
    (V, 'V 2123 arg0', 'constexpr int LISTING_INTERNET = 1;', 'constexpr int LISTING_INTERNET = 2;'),
    (V, 'V 2139 arg1', '    PostTo(CREATE_PRESET, CONTROL_MSG_PICK_INDEX, static_cast<uint32_t>(setup.rotation));', '    PostTo(CREATE_PRESET, CONTROL_MSG_PICK_INDEX, static_cast<uint32_t>(setup.gameType));'),
    (V, 'V 2196 arg2', '        PostTo(CREATE_PICKED_LIST, i != current ? CONTROL_MSG_ADD_ENTRY : CONTROL_MSG_ADD_CURRENT_ENTRY, i,', '        PostTo(CREATE_PICKED_LIST, i == current ? CONTROL_MSG_ADD_ENTRY : CONTROL_MSG_ADD_CURRENT_ENTRY, i,'),
]
