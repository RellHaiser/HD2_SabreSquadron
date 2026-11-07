"""Part 9h-xxiii mutations: the main menu's add server popup
(game/main_menu.cpp).

Result: 58: 47 killed (1 after a second run: an address not parsed), 6 not building, 5 equivalent: one of
the two resets, the name / map / game type the reset already sets, the unused second argument of AddServerLine.

Run with the factory build: --build build_factory.bat --check check_mainmenu.py (FACTORY_TEST; the
"mainmenu:" checks). Generated (deletions, comparison, number, bool and negation flips; the lines
unique in the file) and by hand (the ids, the messages, the comparisons).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
V = os.path.join(ROOT, r"src/game/main_menu.cpp")

MUTATIONS = [
    (V, 'V 3342 del', '                   ADD_SERVER_YES = ADD_SERVER_SCREEN | 0x400000, ADD_SERVER_NO = ADD_SERVER_SCREEN | 0x500000;\n', '\n'),
    (V, 'V 3346 num', 'const char *const DEFAULT_PORT = "11001";\n', 'const char *const DEFAULT_PORT = "11002";\n'),
    (V, 'V 3348 num', 'constexpr int GAME_TYPE_UNKNOWN = 4;\n', 'constexpr int GAME_TYPE_UNKNOWN = 5;\n'),
    (V, 'V 3355 del', '    ask.target = id;\n', '\n'),
    (V, 'V 3356 del', '    ask.id = CONTROL_MSG_GET_TEXT;\n    menu->HandleMessage(&ask);\n', '\n    menu->HandleMessage(&ask);\n'),
    (V, 'V 3357 del', '    menu->HandleMessage(&ask);\n', '\n'),
    (V, 'V 3365 del', '    model->SetPos(QUIT_MODEL_POS);\n    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text"), 0, CONTROL_MSG_SET_TEXT_ID, STYLE_VALUE, TEXT_ENTER_ADDRESS);\n', '\n    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text"), 0, CONTROL_MSG_SET_TEXT_ID, STYLE_VALUE, TEXT_ENTER_ADDRESS);\n'),
    (V, 'V 3366 del', '    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text"), 0, CONTROL_MSG_SET_TEXT_ID, STYLE_VALUE, TEXT_ENTER_ADDRESS);\n', '\n'),
    (V, 'V 3366 num', '    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text"), 0, CONTROL_MSG_SET_TEXT_ID, STYLE_VALUE, TEXT_ENTER_ADDRESS);\n', '    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text"), 1, CONTROL_MSG_SET_TEXT_ID, STYLE_VALUE, TEXT_ENTER_ADDRESS);\n'),
    (V, 'V 3367 del', '    S_ControlDesc *address = desc->AddControl(CONTROL_EDIT_BOX, "binput01");\n', '\n'),
    (V, 'V 3368 del', '    AddDescMessage(address, 0, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, C_str(DEFAULT_ADDRESS));\n', '\n'),
    (V, 'V 3368 num', '    AddDescMessage(address, 0, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, C_str(DEFAULT_ADDRESS));\n', '    AddDescMessage(address, 1, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, C_str(DEFAULT_ADDRESS));\n'),
    (V, 'V 3369 del', '    AddDescMessage(address, 0, CONTROL_MSG_SET_MAX_LENGTH, ADDRESS_MAX_LENGTH);\n', '\n'),
    (V, 'V 3369 num', '    AddDescMessage(address, 0, CONTROL_MSG_SET_MAX_LENGTH, ADDRESS_MAX_LENGTH);\n', '    AddDescMessage(address, 1, CONTROL_MSG_SET_MAX_LENGTH, ADDRESS_MAX_LENGTH);\n'),
    (V, 'V 3370 del', '    S_ControlDesc *port = desc->AddControl(CONTROL_EDIT_BOX, "binput02");\n', '\n'),
    (V, 'V 3371 del', '    AddDescMessage(port, 0, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, C_str(DEFAULT_PORT));\n', '\n'),
    (V, 'V 3371 num', '    AddDescMessage(port, 0, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, C_str(DEFAULT_PORT));\n', '    AddDescMessage(port, 1, CONTROL_MSG_SET_TEXT, STYLE_BUTTON, C_str(DEFAULT_PORT));\n'),
    (V, 'V 3372 del', '    AddDescMessage(port, 0, CONTROL_MSG_SET_DIGITS_ONLY, 1);\n    const auto self = reinterpret_cast<uintptr_t>(this);\n', '\n    const auto self = reinterpret_cast<uintptr_t>(this);\n'),
    (V, 'V 3372 num', '    AddDescMessage(port, 0, CONTROL_MSG_SET_DIGITS_ONLY, 1);\n    const auto self = reinterpret_cast<uintptr_t>(this);\n', '    AddDescMessage(port, 1, CONTROL_MSG_SET_DIGITS_ONLY, 1);\n    const auto self = reinterpret_cast<uintptr_t>(this);\n'),
    (V, 'V 3374 num', '    AddDescMessage(AddButton(desc, "byes*", TEXT_YES), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,\n                   Callback(&OnAddServerButton));\n', '    AddDescMessage(AddButton(desc, "byes*", TEXT_YES), 1, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,\n                   Callback(&OnAddServerButton));\n'),
    (V, 'V 3376 num', '    AddDescMessage(AddButton(desc, "bno*", TEXT_NO), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,\n                   Callback(&OnAddServerButton));\n', '    AddDescMessage(AddButton(desc, "bno*", TEXT_NO), 1, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,\n                   Callback(&OnAddServerButton));\n'),
    (V, 'V 3378 del', '    AddDescMessage(desc->AddControl(CONTROL_EDIT_BOX, "binput"), 0, CONTROL_MSG_HIDE, 0);\n', '\n'),
    (V, 'V 3378 num', '    AddDescMessage(desc->AddControl(CONTROL_EDIT_BOX, "binput"), 0, CONTROL_MSG_HIDE, 0);\n', '    AddDescMessage(desc->AddControl(CONTROL_EDIT_BOX, "binput"), 1, CONTROL_MSG_HIDE, 0);\n'),
    (V, 'V 3379 bool', '    AddDescMessage(desc->AddControl(CONTROL_EDIT_BOX, "binput"), 0, CONTROL_MSG_HIDE, 0);\n    return true;\n', '    AddDescMessage(desc->AddControl(CONTROL_EDIT_BOX, "binput"), 0, CONTROL_MSG_HIDE, 0);\n    return false;\n'),
    (V, 'V 3384 not', 'bool C_MainMenu::OnAddServerButton(void *menu, void *, C_Control *control, uint32_t, uint32_t)\n{\n    if (!control)\n', 'bool C_MainMenu::OnAddServerButton(void *menu, void *, C_Control *control, uint32_t, uint32_t)\n{\n    if (control)\n'),
    (V, 'V 3385 bool', 'bool C_MainMenu::OnAddServerButton(void *menu, void *, C_Control *control, uint32_t, uint32_t)\n{\n    if (!control)\n        return false;\n', 'bool C_MainMenu::OnAddServerButton(void *menu, void *, C_Control *control, uint32_t, uint32_t)\n{\n    if (!control)\n        return true;\n'),
    (V, 'V 3388 cmp', '    if (id == ADD_SERVER_YES) {\n', '    if (id != ADD_SERVER_YES) {\n'),
    (V, 'V 3390 del', '        const C_str address = AskText(self, ADD_SERVER_ADDRESS);\n', '\n'),
    (V, 'V 3391 del', '        const C_str port = AskText(self, ADD_SERVER_PORT);\n', '\n'),
    (V, 'V 3392 cmp', '        if (!address.IsEmpty() && !port.IsEmpty()) {\n', '        if (!address.IsEmpty() || !port.IsEmpty()) {\n'),
    (V, 'V 3392 not', '        if (!address.IsEmpty() && !port.IsEmpty()) {\n', '        if (address.IsEmpty() && !port.IsEmpty()) {\n'),
    (V, 'V 3394 del', '            server->Reset();\n            server->Reset();\n', '\n            server->Reset();\n'),
    (V, 'V 3395 del', '            server->Reset();\n            int a = 0, b = 0, c = 0, d = 0;\n', '\n            int a = 0, b = 0, c = 0, d = 0;\n'),
    (V, 'V 3396 num', '            int a = 0, b = 0, c = 0, d = 0;\n', '            int a = 1, b = 0, c = 0, d = 0;\n'),
    (V, 'V 3397 del', '            std::sscanf(address.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d);\n', '\n'),
    (V, 'V 3399 num', '            server->address = ((static_cast<uint32_t>(c) & 0xFF | static_cast<uint32_t>(d) << 8) << 8 |\n', '            server->address = ((static_cast<uint32_t>(c) & 0xFF | static_cast<uint32_t>(d) << 9) << 8 |\n'),
    (V, 'V 3400 num', '                               static_cast<uint32_t>(b) & 0xFF) << 8 |\n', '                               static_cast<uint32_t>(b) & 0xFF) << 9 |\n'),
    (V, 'V 3401 del', '                              static_cast<uint32_t>(a) & 0xFF;\n', '\n'),
    (V, 'V 3402 del', '            server->port = std::atoi(port.c_str());\n', '\n'),
    (V, 'V 3403 del', '            server->gameType = GAME_TYPE_UNKNOWN;\n', '\n'),
    (V, 'V 3404 del', '            server->internet = true;\n', '\n'),
    (V, 'V 3405 del', '            std::strcpy(server->name, UNKNOWN_SERVER);\n', '\n'),
    (V, 'V 3406 del', '            std::strcpy(server->map, UNKNOWN_SERVER);\n', '\n'),
    (V, 'V 3407 del', '            server->versionMinor = g_versionMinor;\n', '\n'),
    (V, 'V 3408 del', '            server->versionMajor = g_versionMajor;\n', '\n'),
    (V, 'V 3409 del', '            self->m_mpSettings->GetList(0).push_back(server);\n', '\n'),
    (V, 'V 3409 num', '            self->m_mpSettings->GetList(0).push_back(server);\n', '            self->m_mpSettings->GetList(1).push_back(server);\n'),
    (V, 'V 3410 del', '            self->AddServerLine(server, 1);\n', '\n'),
    (V, 'V 3410 num', '            self->AddServerLine(server, 1);\n', '            self->AddServerLine(server, 2);\n'),
    (V, 'V 3412 cmp', '    } else if (id != ADD_SERVER_NO) {\n', '    } else if (id == ADD_SERVER_NO) {\n'),
    (V, 'V 3413 bool', '        return false;\n    }\n    Post(MENU_MSG_CLOSE_POPUP, 0);\n', '        return true;\n    }\n    Post(MENU_MSG_CLOSE_POPUP, 0);\n'),
    (V, 'V 3415 del', '    Post(MENU_MSG_CLOSE_POPUP, 0);\n    self->m_step = BROWSER_IDLE;\n', '\n    self->m_step = BROWSER_IDLE;\n'),
    (V, 'V 3415 num', '    Post(MENU_MSG_CLOSE_POPUP, 0);\n    self->m_step = BROWSER_IDLE;\n', '    Post(MENU_MSG_CLOSE_POPUP, 1);\n    self->m_step = BROWSER_IDLE;\n'),
    (V, 'V 3416 del', '    self->m_step = BROWSER_IDLE;\n    return true;\n', '\n    return true;\n'),
    (V, 'V 3417 bool', '    self->m_step = BROWSER_IDLE;\n    return true;\n', '    self->m_step = BROWSER_IDLE;\n    return false;\n'),
    (V, 'V 3409 arg0', '            self->m_mpSettings->GetList(0).push_back(server);', '            self->m_mpSettings->GetList(1).push_back(server);'),
    (V, 'V 3404 arg1', '            server->internet = true;', '            server->internet = false;'),
    (V, 'V 3406 arg2', '            std::strcpy(server->map, UNKNOWN_SERVER);', '            std::strcpy(server->map, "");'),
]
