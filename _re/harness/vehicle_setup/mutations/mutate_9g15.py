"""Part 9g-xv mutations: the automatic selection (game/squad_menu.cpp).

Run with the factory build: --build build_factory.bat --check check_campaignscreens.py (FACTORY_TEST;
the "campaignscreens:" checks). Generated (deletions, comparison, number, bool and negation flips; the
lines unique in the file) and by hand (the scores, the candidates, the slots).

68 mutations: all killed but 3, equivalent: V 1138 num (worst = -2: only its sign is read), V 1175 cmp / num
(slot <= 4, slot < 5: past four filled slots the fifth "slot" read changes nothing, the filling loop stops at
four).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
V = os.path.join(ROOT, r"src/game/squad_menu.cpp")

MUTATIONS = [
    (V, 'V 1125 num', 'constexpr int AUTO_CANDIDATES = 5;\n', 'constexpr int AUTO_CANDIDATES = 6;\n'),
    (V, 'V 1131 cmp', '    if (*count < AUTO_CANDIDATES) {\n', '    if (*count <= AUTO_CANDIDATES) {\n'),
    (V, 'V 1132 del', '        ids[*count] = id;\n', '\n'),
    (V, 'V 1133 del', '        scores[*count] = score;\n', '\n'),
    (V, 'V 1134 del', '        ++*count;\n', '\n'),
    (V, 'V 1138 num', '    int worst = -1, worstScore = -1;\n', '    int worst = -2, worstScore = -1;\n'),
    (V, 'V 1139 cmp', '    for (int i = 0; i < *count; ++i) {\n', '    for (int i = 0; i <= *count; ++i) {\n'),
    (V, 'V 1139 num', '    for (int i = 0; i < *count; ++i) {\n', '    for (int i = 1; i < *count; ++i) {\n'),
    (V, 'V 1140 cmp', '        if (scores[i] < score && (worst < 0 || scores[i] < worstScore)) {\n', '        if (scores[i] <= score && (worst < 0 || scores[i] < worstScore)) {\n'),
    (V, 'V 1140 num', '        if (scores[i] < score && (worst < 0 || scores[i] < worstScore)) {\n', '        if (scores[i] < score && (worst < 1 || scores[i] < worstScore)) {\n'),
    (V, 'V 1141 del', '            worstScore = scores[i];\n', '\n'),
    (V, 'V 1142 del', '            worst = i;\n', '\n'),
    (V, 'V 1145 cmp', '    if (worst >= 0) {\n', '    if (worst > 0) {\n'),
    (V, 'V 1145 num', '    if (worst >= 0) {\n', '    if (worst >= 1) {\n'),
    (V, 'V 1146 del', '        scores[worst] = score;\n', '\n'),
    (V, 'V 1147 del', '        ids[worst] = id;\n', '\n'),
    (V, 'V 1154 del', '    C_HumanStore *store = g_gameProfile.m_humanStore;\n    S_SquadSlot *slots = self->m_slots;\n', '\n    S_SquadSlot *slots = self->m_slots;\n'),
    (V, 'V 1155 del', '    S_SquadSlot *slots = self->m_slots;\n    if (IsOneSoldier()) {\n        // One soldier: a living one of the first 30 at random. (Quirk kept: the roster not checked.)\n', '\n    if (IsOneSoldier()) {\n        // One soldier: a living one of the first 30 at random. (Quirk kept: the roster not checked.)\n'),
    (V, 'V 1158 cmp', '        if (slots[0].profile < 0) {\n', '        if (slots[0].profile <= 0) {\n'),
    (V, 'V 1158 num', '        if (slots[0].profile < 0) {\n', '        if (slots[1].profile < 0) {\n'),
    (V, 'V 1162 del', '                    pick = std::rand() % ROSTER_LIMIT;\n', '\n'),
    (V, 'V 1163 del', '                while (store->Get(pick)->dead);\n', '\n'),
    (V, 'V 1164 del', '            } while (self->IsInSlots(pick));\n', '\n'),
    (V, 'V 1165 del', '            Post(ROSTER_CONTROLS, CONTROL_MSG_HIDE_ITEM, RosterPicture(pick));\n', '\n'),
    (V, 'V 1166 del', '            slots[0].SetSoldier(pick, true);\n', '\n'),
    (V, 'V 1166 num', '            slots[0].SetSoldier(pick, true);\n', '            slots[1].SetSoldier(pick, true);\n'),
    (V, 'V 1168 del', '        self->SelectFreeSoldier(0);\n', '\n'),
    (V, 'V 1168 num', '        self->SelectFreeSoldier(0);\n', '        self->SelectFreeSoldier(1);\n'),
    (V, 'V 1169 bool', '        return true;\n    }\n    // Each empty slot from the first: one of the five best of the living free roster soldiers at random\n', '        return false;\n    }\n    // Each empty slot from the first: one of the five best of the living free roster soldiers at random\n'),
    (V, 'V 1174 num', '    int slot = 0;\n', '    int slot = 1;\n'),
    (V, 'V 1175 cmp', '    while (slot < 4 && slots[slot].profile > -1)\n', '    while (slot <= 4 && slots[slot].profile > -1)\n'),
    (V, 'V 1175 num', '    while (slot < 4 && slots[slot].profile > -1)\n', '    while (slot < 5 && slots[slot].profile > -1)\n'),
    (V, 'V 1176 del', '        ++slot;\n', '\n'),
    (V, 'V 1177 num', '    for (; slot < 4; ++slot) {\n', '    for (; slot < 5; ++slot) {\n'),
    (V, 'V 1178 num', '        int ids[AUTO_CANDIDATES], scores[AUTO_CANDIDATES], count = 0;\n', '        int ids[AUTO_CANDIDATES], scores[AUTO_CANDIDATES], count = 1;\n'),
    (V, 'V 1179 cmp', '        for (int i = 0; i < ROSTER_LIMIT; ++i) {\n', '        for (int i = 0; i <= ROSTER_LIMIT; ++i) {\n'),
    (V, 'V 1179 num', '        for (int i = 0; i < ROSTER_LIMIT; ++i) {\n', '        for (int i = 1; i < ROSTER_LIMIT; ++i) {\n'),
    (V, 'V 1180 del', '            const S_SoldierProfile *profile = store->Get(i);\n', '\n'),
    (V, 'V 1181 cmp', '            if (profile->dead || self->IsInSlots(i))\n', '            if (profile->dead && self->IsInSlots(i))\n'),
    (V, 'V 1182 del', '                continue;\n            int score;\n', '\n            int score;\n'),
    (V, 'V 1184 cmp', '            if (slot == 0 || slot == 2)\n', '            if (slot != 0 || slot == 2)\n'),
    (V, 'V 1184 num', '            if (slot == 0 || slot == 2)\n', '            if (slot == 1 || slot == 2)\n'),
    (V, 'V 1185 del', '                score = static_cast<int>(profile->stats[4]);\n', '\n'),
    (V, 'V 1185 num', '                score = static_cast<int>(profile->stats[4]);\n', '                score = static_cast<int>(profile->stats[5]);\n'),
    (V, 'V 1186 cmp', '            else if (slot == 1)\n', '            else if (slot != 1)\n'),
    (V, 'V 1186 num', '            else if (slot == 1)\n', '            else if (slot == 2)\n'),
    (V, 'V 1187 del', '                score = static_cast<int>(profile->stats[7]);\n', '\n'),
    (V, 'V 1187 num', '                score = static_cast<int>(profile->stats[7]);\n', '                score = static_cast<int>(profile->stats[8]);\n'),
    (V, 'V 1189 del', '                score = static_cast<int>((profile->stats[1] - profile->stats[4] * 0.1f) * 10.0f);\n', '\n'),
    (V, 'V 1189 num', '                score = static_cast<int>((profile->stats[1] - profile->stats[4] * 0.1f) * 10.0f);\n', '                score = static_cast<int>((profile->stats[2] - profile->stats[4] * 0.1f) * 10.0f);\n'),
    (V, 'V 1190 del', '            KeepBest(ids, scores, &count, i, score);\n', '\n'),
    (V, 'V 1192 cmp', '        if (count <= 0)\n', '        if (count < 0)\n'),
    (V, 'V 1192 num', '        if (count <= 0)\n', '        if (count <= 1)\n'),
    (V, 'V 1193 del', '            continue;\n        const int pick = ids[std::rand() % count];\n', '\n        const int pick = ids[std::rand() % count];\n'),
    (V, 'V 1195 del', '        Post(ROSTER_CONTROLS, CONTROL_MSG_HIDE_ITEM, RosterPicture(pick));\n        slots[slot].SetSoldier(pick, true);\n', '\n        slots[slot].SetSoldier(pick, true);\n'),
    (V, 'V 1196 del', '        slots[slot].SetSoldier(pick, true);\n', '\n'),
    (V, 'V 1198 del', '    self->SelectFreeSoldier(0);\n    return true;\n', '\n    return true;\n'),
    (V, 'V 1198 num', '    self->SelectFreeSoldier(0);\n    return true;\n', '    self->SelectFreeSoldier(1);\n    return true;\n'),
    (V, 'V 1199 bool', '    self->SelectFreeSoldier(0);\n    return true;\n', '    self->SelectFreeSoldier(0);\n    return false;\n'),
    (V, 'V 1125 c0', 'constexpr int AUTO_CANDIDATES = 5;', 'constexpr int AUTO_CANDIDATES = 4;'),
    (V, 'V 1140 arg1', '        if (scores[i] < score && (worst < 0 || scores[i] < worstScore)) {', '        if (scores[i] < score && (worst < 0 || scores[i] <= worstScore)) {'),
    (V, 'V 1185 arg2', '                score = static_cast<int>(profile->stats[4]);', '                score = static_cast<int>(profile->stats[3]);'),
    (V, 'V 1187 arg3', '                score = static_cast<int>(profile->stats[7]);', '                score = static_cast<int>(profile->stats[6]);'),
    (V, 'V 1189 arg4', '                score = static_cast<int>((profile->stats[1] - profile->stats[4] * 0.1f) * 10.0f);', '                score = static_cast<int>(profile->stats[1] * 10.0f - profile->stats[4]);'),
    (V, 'V 1184 arg5', '            if (slot == 0 || slot == 2)', '            if (slot == 0 || slot == 3)'),
    (V, 'V 1196 arg6', '        slots[slot].SetSoldier(pick, true);', '        slots[slot].SetSoldier(pick, false);'),
    (V, 'V 1166 arg7', '            slots[0].SetSoldier(pick, true);', '            slots[0].SetSoldier(pick, false);'),
    (V, 'V 1175 arg8', '    while (slot < 4 && slots[slot].profile > -1)', '    while (slot < 4 && slots[slot].profile > 0)'),
]
