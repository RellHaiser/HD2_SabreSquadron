"""Part 9e-xxiii mutations: the store list's description (game/store_list.cpp).

Run with the factory build: --build build_factory.bat --check check_storelist.py (FACTORY_TEST; the
"storelist:" checks). Generated (deletions, comparison, number, bool and negation flips; the lines
unique in the file) and by hand (the constants, the targets and the fields).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
L = os.path.join(ROOT, r"src/game/store_list.cpp")

MUTATIONS = [
    (L, 'L 90 del', '    m_screenIndex = desc->id >> 26;\n', '\n'),
    (L, 'L 90 num', '    m_screenIndex = desc->id >> 26;\n', '    m_screenIndex = desc->id >> 27;\n'),
    (L, 'L 91 del', '    S_ControlDesc *group = desc->AddControl(DESC_TYPE_GROUP, STORE_ANCHOR);\n', '\n'),
    (L, 'L 92 del', '    m_controlIndex = static_cast<int>(group->id >> 20 & 0x3F);\n', '\n'),
    (L, 'L 92 num', '    m_controlIndex = static_cast<int>(group->id >> 20 & 0x3F);\n', '    m_controlIndex = static_cast<int>(group->id >> 21 & 0x3F);\n'),
    (L, 'L 93 del', '    S_ControlDesc *tabs = group->AddChild(DESC_TYPE_TABS, "filter");\n', '\n'),
    (L, 'L 94 num', '    AddDescMessage(tabs, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_0E, reinterpret_cast<uintptr_t>(this),\n', '    AddDescMessage(tabs, 1, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_0E, reinterpret_cast<uintptr_t>(this),\n'),
    (L, 'L 96 del', '    AddDescMessage(tabs, 0, CONTROL_MSG_SET_HEADER_TEXT_ID, TABS_HEADER_STYLE, TAB_TEXTS[0]);\n', '\n'),
    (L, 'L 96 num', '    AddDescMessage(tabs, 0, CONTROL_MSG_SET_HEADER_TEXT_ID, TABS_HEADER_STYLE, TAB_TEXTS[0]);\n', '    AddDescMessage(tabs, 1, CONTROL_MSG_SET_HEADER_TEXT_ID, TABS_HEADER_STYLE, TAB_TEXTS[0]);\n'),
    (L, 'L 98 del', '        AddDescMessage(tabs, 0, CONTROL_MSG_ADD_TEXT_ID, TAB_STYLE, text);\n', '\n'),
    (L, 'L 98 num', '        AddDescMessage(tabs, 0, CONTROL_MSG_ADD_TEXT_ID, TAB_STYLE, text);\n', '        AddDescMessage(tabs, 1, CONTROL_MSG_ADD_TEXT_ID, TAB_STYLE, text);\n'),
    (L, 'L 99 del', '    AddDescMessage(tabs, 0, CONTROL_MSG_DEACTIVATE, 0);\n', '\n'),
    (L, 'L 99 num', '    AddDescMessage(tabs, 0, CONTROL_MSG_DEACTIVATE, 0);\n', '    AddDescMessage(tabs, 1, CONTROL_MSG_DEACTIVATE, 0);\n'),
    (L, 'L 100 num', '    AddDescMessage(group->AddChild(DESC_TYPE_SCROLL_BAR, "bscroll"), 0, CONTROL_MSG_SET_VALUE,\n', '    AddDescMessage(group->AddChild(DESC_TYPE_SCROLL_BAR, "bscroll"), 1, CONTROL_MSG_SET_VALUE,\n'),
    (L, 'L 101 num', '                   BaseId() | STORE_FIRST_PAGE << 12);\n', '                   BaseId() | STORE_FIRST_PAGE << 13);\n'),
    (L, 'L 102 cmp', '    for (int i = 0; i < MAX_ITEM_TYPES; ++i)\n        m_entries.push_back(0);\n', '    for (int i = 0; i <= MAX_ITEM_TYPES; ++i)\n        m_entries.push_back(0);\n'),
    (L, 'L 102 num', '    for (int i = 0; i < MAX_ITEM_TYPES; ++i)\n        m_entries.push_back(0);\n', '    for (int i = 1; i < MAX_ITEM_TYPES; ++i)\n        m_entries.push_back(0);\n'),
    (L, 'L 103 del', '        m_entries.push_back(0);\n', '\n'),
    (L, 'L 103 num', '        m_entries.push_back(0);\n', '        m_entries.push_back(1);\n'),
    (L, 'L 106 del', '        BuildPage(group, model, C_fstr(page.name), page.group);\n', '\n'),
    (L, 'L 108 del', '    m_highlight = model->FindChildFrame(STORE_HIGHLIGHT, FIND_VISUALS);\n    m_highlight->LinkTo(nullptr, 1);\n', '\n    m_highlight->LinkTo(nullptr, 1);\n'),
    (L, 'L 109 del', '    m_highlight->LinkTo(nullptr, 1);\n', '\n'),
    (L, 'L 109 num', '    m_highlight->LinkTo(nullptr, 1);\n', '    m_highlight->LinkTo(nullptr, 2);\n'),
    (L, 'L 110 del', '    m_highlight->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));\n', '\n'),
    (L, 'L 110 num', '    m_highlight->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));\n', '    m_highlight->SetPosInline(S_vector(0.25f, 0.0f, 0.0f));\n'),
    (L, 'L 52 const0', 'constexpr int DESC_TYPE_GROUP = 5, DESC_TYPE_TABS = 0x11, DESC_TYPE_SCROLL_BAR = 8;', 'constexpr int DESC_TYPE_GROUP = 6, DESC_TYPE_TABS = 0x11, DESC_TYPE_SCROLL_BAR = 8;'),
    (L, 'L 52 const1', 'constexpr int DESC_TYPE_GROUP = 5, DESC_TYPE_TABS = 0x11, DESC_TYPE_SCROLL_BAR = 8;', 'constexpr int DESC_TYPE_GROUP = 5, DESC_TYPE_TABS = 0x12, DESC_TYPE_SCROLL_BAR = 8;'),
    (L, 'L 52 const2', 'constexpr int DESC_TYPE_GROUP = 5, DESC_TYPE_TABS = 0x11, DESC_TYPE_SCROLL_BAR = 8;', 'constexpr int DESC_TYPE_GROUP = 5, DESC_TYPE_TABS = 0x11, DESC_TYPE_SCROLL_BAR = 9;'),
    (L, 'L 53 const3', 'constexpr uint32_t TABS_HEADER_STYLE = 0xC, TAB_STYLE = 0x13;', 'constexpr uint32_t TABS_HEADER_STYLE = 0xD, TAB_STYLE = 0x13;'),
    (L, 'L 53 const4', 'constexpr uint32_t TABS_HEADER_STYLE = 0xC, TAB_STYLE = 0x13;', 'constexpr uint32_t TABS_HEADER_STYLE = 0xC, TAB_STYLE = 0x14;'),
    (L, 'L 54 const5', 'constexpr uint32_t TAB_TEXTS[] = {0xA9F, 0xAA0, 0xAA1, 0xAA3, 0xAA4, 0xAA5};', 'constexpr uint32_t TAB_TEXTS[] = {0xAA0, 0xAA0, 0xAA1, 0xAA3, 0xAA4, 0xAA5};'),
    (L, 'L 54 const6', 'constexpr uint32_t TAB_TEXTS[] = {0xA9F, 0xAA0, 0xAA1, 0xAA3, 0xAA4, 0xAA5};', 'constexpr uint32_t TAB_TEXTS[] = {0xA9F, 0xAA0, 0xAA1, 0xAA2, 0xAA4, 0xAA5};'),
    (L, 'L 54 const7', 'constexpr uint32_t TAB_TEXTS[] = {0xA9F, 0xAA0, 0xAA1, 0xAA3, 0xAA4, 0xAA5};', 'constexpr uint32_t TAB_TEXTS[] = {0xA9F, 0xAA0, 0xAA1, 0xAA3, 0xAA4};'),
    (L, 'L 60 const8', '{"GR_Explosives", 5}', '{"GR_Explosives", 4}'),
    (L, 'L 60 const9', '{"GR_Dresses", 6}', '{"GR_Dresses", 7}'),
    (L, 'L 59 const10', '{"GR_SmallWeapons", 1}', '{"GR_SmallWeapons", 0}'),
    (L, 'L 60 const11', '{"GR_Other", 7}', '{"GR_Other", 8}'),
    (L, 'L 90 arg12', '    m_screenIndex = desc->id >> 26;', '    m_screenIndex = desc->id >> 25;'),
    (L, 'L 92 arg13', '    m_controlIndex = static_cast<int>(group->id >> 20 & 0x3F);', '    m_controlIndex = static_cast<int>(group->id >> 20 & 0x1F);'),
    (L, 'L 101 arg14', '                   BaseId() | STORE_FIRST_PAGE << 12);', '                   BaseId() | (STORE_FIRST_PAGE + 1) << 12);'),
    (L, 'L 99 arg15', '    AddDescMessage(tabs, 0, CONTROL_MSG_DEACTIVATE, 0);\n    AddDescMessage(group', '    AddDescMessage(group'),
    (L, 'L 102 arg16', '    for (int i = 0; i < MAX_ITEM_TYPES; ++i)\n        m_entries.push_back(0);', '    m_entries.resize(MAX_ITEM_TYPES);'),
    (L, 'L 106 arg17', '        BuildPage(group, model, C_fstr(page.name), page.group);', '        BuildPage(tabs, model, C_fstr(page.name), page.group);'),
    (L, 'L 109 arg18', '    m_highlight->LinkTo(nullptr, 1);', '    m_highlight->LinkTo(nullptr, 0);'),
    (L, 'L 110 arg19', '    m_highlight->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));', '    m_highlight->SetPosInline(HIGHLIGHT_POS);'),
]
