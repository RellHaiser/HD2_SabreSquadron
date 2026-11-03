"""Part 9e-xv mutations: the in-game menu's equipment panel (game/game_menu.cpp).

Run with the factory build: --build build_factory.bat --check check_gamemenu.py (FACTORY_TEST; the
"gamemenu:" checks). Generated (deletions, comparison, number, bool and negation flips; the lines
unique in the file) and by hand (the frames, their names, links and meshes).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
G = os.path.join(ROOT, r"src/game/game_menu.cpp")

MUTATIONS = [
    (G, 'G 665 cmp', '    if (!panel || !holsters || !backpack)\n', '    if (!panel && !holsters || !backpack)\n'),
    (G, 'G 665 not', '    if (!panel || !holsters || !backpack)\n', '    if (panel || !holsters || !backpack)\n'),
    (G, 'G 666 bool', '        return false;\n    panel->SetName("P_Equipment");\n', '        return true;\n    panel->SetName("P_Equipment");\n'),
    (G, 'G 667 del', '    panel->SetName("P_Equipment");\n', '\n'),
    (G, 'G 669 del', '        frame->LinkTo(panel, 0);\n', '\n'),
    (G, 'G 669 num', '        frame->LinkTo(panel, 0);\n', '        frame->LinkTo(panel, 1);\n'),
    (G, 'G 670 del', '        frame->SetPos(S_vector(0.0f, 0.0f, 0.0f));\n', '\n'),
    (G, 'G 670 num', '        frame->SetPos(S_vector(0.0f, 0.0f, 0.0f));\n', '        frame->SetPos(S_vector(0.25f, 0.0f, 0.0f));\n'),
    (G, 'G 671 del', '        frame->SetName(name);\n', '\n'),
    (G, 'G 673 del', '    group(holsters, "GB_Holsters");\n', '\n'),
    (G, 'G 674 del', '    group(backpack, "GR_BackPack");\n', '\n'),
    (G, 'G 676 del', '        m_models.push_back(frame);\n', '\n'),
    (G, 'G 682 del', '            visual->DuplicateMesh();\n', '\n'),
    (G, 'G 683 del', '            visual->GetVisualMesh()->Unknown18();\n', '\n'),
    (G, 'G 690 del', '            frame->LinkTo(parent, 1);\n', '\n'),
    (G, 'G 690 num', '            frame->LinkTo(parent, 1);\n', '            frame->LinkTo(parent, 2);\n'),
    (G, 'G 694 del', '        own(name);\n', '\n'),
    (G, 'G 697 del', '    move("text_hlstrscap", scroll);\n', '\n'),
    (G, 'G 698 del', '    move("hlcpct00", scroll);\n', '\n'),
    (G, 'G 701 del', '            item->LinkTo(holsters, 1);\n', '\n'),
    (G, 'G 701 num', '            item->LinkTo(holsters, 1);\n', '            item->LinkTo(holsters, 2);\n'),
    (G, 'G 703 del', '    scroll = move("scroll14", panel);\n', '\n'),
    (G, 'G 704 del', '    move("text_bkpckcap", scroll);\n', '\n'),
    (G, 'G 705 del', '    move("hlcpct01", scroll);\n', '\n'),
    (G, 'G 708 del', '            item->LinkTo(backpack, 1);\n', '\n'),
    (G, 'G 708 num', '            item->LinkTo(backpack, 1);\n', '            item->LinkTo(backpack, 2);\n'),
    (G, 'G 710 del', '    m_panel.SetMenu(this);\n', '\n'),
    (G, 'G 711 del', '    m_panel.BuildDescription(desc, model);\n', '\n'),
    (G, 'G 712 bool', '    m_panel.BuildDescription(desc, model);\n    return true;\n', '    m_panel.BuildDescription(desc, model);\n    return false;\n'),
    (G, 'G 661 name0', 'model->FindChildFrame("equippanel", FRAME_MASK_DUMMY);', 'model->FindChildFrame("equippanel", FRAME_MASK_VISUAL);'),
    (G, 'G 662 arg1', 'I3D_frame *holsters = I3DGetDriver()->CreateFrame(FRAME_DUMMY);', 'I3D_frame *holsters = I3DGetDriver()->CreateFrame(FRAME_VISUAL);'),
    (G, 'G 667 name2', 'panel->SetName("P_Equipment");', 'panel->SetName("P_equipment");'),
    (G, 'G 669 arg3', '        frame->LinkTo(panel, 0);', '        frame->LinkTo(panel, 1);'),
    (G, 'G 670 arg4', '        frame->SetPos(S_vector(0.0f, 0.0f, 0.0f));', '        frame->SetPos(S_vector(0.0f, 1.0f, 0.0f));'),
    (G, 'G 673 name5', 'group(holsters, "GB_Holsters");', 'group(holsters, "GR_BackPack");'),
    (G, 'G 674 name6', 'group(backpack, "GR_BackPack");', 'group(backpack, "GB_Holsters");'),
    (G, 'G 675 arg7', 'for (I3D_frame *frame : {panel, holsters, backpack})', 'for (I3D_frame *frame : {holsters, backpack})'),
    (G, 'G 680 arg8', 'auto *visual = static_cast<I3D_object_visual *>(panel->FindChildFrame(name, FRAME_MASK_VISUAL));', 'auto *visual = static_cast<I3D_object_visual *>(panel->FindChildFrame(name, FRAME_MASK_DUMMY));'),
    (G, 'G 688 arg9', 'I3D_frame *frame = panel->FindChildFrame(name, FRAME_MASK_VISUAL);\n        if (frame)', 'I3D_frame *frame = panel->FindChildFrame(name, FRAME_MASK_DUMMY);\n        if (frame)'),
    (G, 'G 690 arg10', '            frame->LinkTo(parent, 1);', '            frame->LinkTo(parent, 0);'),
    (G, 'G 693 name11', 'for (const char *name : {"back", "head", "drop", "hands", "uniform", "shoulder"})', 'for (const char *name : {"back", "head", "drop", "hands", "uniform"})'),
    (G, 'G 696 name12', 'I3D_frame *scroll = move("scroll13", panel);', 'I3D_frame *scroll = move("scroll13", holsters);'),
    (G, 'G 697 name13', 'move("text_hlstrscap", scroll);', 'move("text_hlstrscap", panel);'),
    (G, 'G 698 name14', 'move("hlcpct00", scroll);', 'move("hlcpct00", panel);'),
    (G, 'G 699 name15', 'for (const char *name : {"item00", "item01", "item02", "item03"}) {', 'for (const char *name : {"item00", "item01", "item03"}) {'),
    (G, 'G 701 arg16', '            item->LinkTo(holsters, 1);', '            item->LinkTo(backpack, 1);'),
    (G, 'G 703 name17', 'scroll = move("scroll14", panel);', 'scroll = move("scroll13", panel);'),
    (G, 'G 704 name18', 'move("text_bkpckcap", scroll);', 'move("text_bkpckcap", panel);'),
    (G, 'G 705 name19', 'move("hlcpct01", scroll);', 'move("hlcpct00", scroll);'),
    (G, 'G 706 name20', 'for (const char *name : {"item04", "item05", "item06", "item07"}) {', 'for (const char *name : {"item04", "item05", "item06"}) {'),
    (G, 'G 708 arg21', '            item->LinkTo(backpack, 1);', '            item->LinkTo(backpack, 0);'),
    (G, 'G 710 arg22', '    m_panel.SetMenu(this);\n    m_panel.BuildDescription(desc, model);', '    m_panel.BuildDescription(desc, model);\n    m_panel.SetMenu(this);'),
]
