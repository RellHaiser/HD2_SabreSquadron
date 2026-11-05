"""Part 9i-iv mutations: the session's description file
(game/mp_settings.cpp).

Result: 45: 37 killed, 3 not building, 5 equivalent: the keyword tables' sizes (Keyword() is given the counts), the
rules' copy one int longer (read past the end, unseen).

Run with the factory build: --build build_factory.bat --check check_description.py (FACTORY_TEST; the
"mainmenu:" checks). Generated (deletions, comparison, number, bool and negation flips; the lines
unique in the file) and by hand (the ids, the messages, the comparisons).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
V = os.path.join(ROOT, r"src/game/mp_settings.cpp")

MUTATIONS = [
    (V, 'V 258 num', 'const char *const RULE_KEYWORDS[13] = {"maxclients",   "pointlimit",      "roundlimit",   "roundcount", "warmup",\n', 'const char *const RULE_KEYWORDS[14] = {"maxclients",   "pointlimit",      "roundlimit",   "roundcount", "warmup",\n'),
    (V, 'V 260 del', '                                       "3rdpersonview", "spawnprotection", "inversedamage", "fallingdmg"};\n', '\n'),
    (V, 'V 261 num', 'const char *const GAME_TYPE_KEYWORDS[4] = {"deathmatch", "occupation", "objectives", "cooperative"};\n', 'const char *const GAME_TYPE_KEYWORDS[5] = {"deathmatch", "occupation", "objectives", "cooperative"};\n'),
    (V, 'V 262 num', 'const char *const DOMAIN_KEYWORDS[2] = {"local", "internet"};\n', 'const char *const DOMAIN_KEYWORDS[3] = {"local", "internet"};\n'),
    (V, 'V 263 num', 'const char *const VOICE_CODEC_KEYWORDS[7] = {"vr12", "sc03", "sc06", "truespeech", "gsm", "adpcm", "pcm"};\n', 'const char *const VOICE_CODEC_KEYWORDS[8] = {"vr12", "sc03", "sc06", "truespeech", "gsm", "adpcm", "pcm"};\n'),
    (V, 'V 268 cmp', '    return index >= 0 && index < count ? table[index] : "";\n', '    return index >= 0 && index <= count ? table[index] : "";\n'),
    (V, 'V 268 num', '    return index >= 0 && index < count ? table[index] : "";\n', '    return index >= 1 && index < count ? table[index] : "";\n'),
    (V, 'V 275 del', '    FILE *file = std::fopen(fileName ? fileName : DESCRIPTION_FILE, "wt");\n', '\n'),
    (V, 'V 277 not', '    if (!file)\n', '    if (file)\n'),
    (V, 'V 279 del', '    std::fprintf(file, "sessionname \\"%s\\"\\n", sessionName);\n', '\n'),
    (V, 'V 280 del', '    std::fprintf(file, "style %s\\n", Keyword(GAME_TYPE_KEYWORDS, 4, gameType));\n', '\n'),
    (V, 'V 280 num', '    std::fprintf(file, "style %s\\n", Keyword(GAME_TYPE_KEYWORDS, 4, gameType));\n', '    std::fprintf(file, "style %s\\n", Keyword(GAME_TYPE_KEYWORDS, 5, gameType));\n'),
    (V, 'V 282 del', '        std::fprintf(file, "mapname %s\\n", map->GetName());\n', '\n'),
    (V, 'V 283 del', '    std::fprintf(file, "domain %s\\n", Keyword(DOMAIN_KEYWORDS, 2, unknown174));\n', '\n'),
    (V, 'V 283 num', '    std::fprintf(file, "domain %s\\n", Keyword(DOMAIN_KEYWORDS, 2, unknown174));\n', '    std::fprintf(file, "domain %s\\n", Keyword(DOMAIN_KEYWORDS, 3, unknown174));\n'),
    (V, 'V 284 del', '    std::fprintf(file, "dedicated %d\\n", dedicated ? 1 : 0);\n', '\n'),
    (V, 'V 284 num', '    std::fprintf(file, "dedicated %d\\n", dedicated ? 1 : 0);\n', '    std::fprintf(file, "dedicated %d\\n", dedicated ? 2 : 0);\n'),
    (V, 'V 286 cmp', '    if (!dedicated || unknown174 != 0)\n', '    if (!dedicated || unknown174 == 0)\n'),
    (V, 'V 286 num', '    if (!dedicated || unknown174 != 0)\n', '    if (!dedicated || unknown174 != 1)\n'),
    (V, 'V 286 not', '    if (!dedicated || unknown174 != 0)\n', '    if (dedicated || unknown174 != 0)\n'),
    (V, 'V 287 del', '        std::fprintf(file, "port %d\\n", port);\n', '\n'),
    (V, 'V 288 del', '    std::fprintf(file, "password \\"%s\\"\\n", password.c_str());\n', '\n'),
    (V, 'V 289 del', '    std::fprintf(file, "password \\"%s\\"\\n", password.c_str());\n    S_MpGameRules rules;\n', '    std::fprintf(file, "password \\"%s\\"\\n", password.c_str());\n\n'),
    (V, 'V 290 del', '    std::fprintf(file, "password \\"%s\\"\\n", password.c_str());\n    S_MpGameRules rules;\n    GetRules(rules);\n', '    std::fprintf(file, "password \\"%s\\"\\n", password.c_str());\n    S_MpGameRules rules;\n\n'),
    (V, 'V 291 num', '    int values[22];\n    std::memcpy(values, &rules, sizeof values);\n    for (int i = 0; i < 13; ++i)\n', '    int values[23];\n    std::memcpy(values, &rules, sizeof values);\n    for (int i = 0; i < 13; ++i)\n'),
    (V, 'V 292 del', '    std::memcpy(values, &rules, sizeof values);\n    for (int i = 0; i < 13; ++i)\n', '\n    for (int i = 0; i < 13; ++i)\n'),
    (V, 'V 293 cmp', '    for (int i = 0; i < 13; ++i)\n', '    for (int i = 0; i <= 13; ++i)\n'),
    (V, 'V 293 num', '    for (int i = 0; i < 13; ++i)\n', '    for (int i = 1; i < 13; ++i)\n'),
    (V, 'V 294 del', '        std::fprintf(file, "%s %d\\n", RULE_KEYWORDS[i], values[i]);\n', '\n'),
    (V, 'V 295 del', '    std::fprintf(file, "maxfreq %d\\n", g_netMaxFreq);\n', '\n'),
    (V, 'V 296 del', '    std::fprintf(file, "maxping %d\\n", g_netMaxPing);\n', '\n'),
    (V, 'V 297 del', '    std::fprintf(file, "maxinactivity %d\\n", g_netMaxInactivity / MS_PER_SECOND);\n', '\n'),
    (V, 'V 298 del', '    std::fprintf(file, "allowvehicles %d\\n", allowVehicles);\n', '\n'),
    (V, 'V 299 del', '    std::fprintf(file, "autorestart %d\\n", autoRestart);\n', '\n'),
    (V, 'V 300 del', '    std::fprintf(file, "coopdifficulty %d\\n", coopDifficulty);\n', '\n'),
    (V, 'V 301 del', '    std::fprintf(file, "cooplives %d\\n", coopLives);\n', '\n'),
    (V, 'V 302 del', '    std::fprintf(file, "allowcrosshair %d\\n", allowCrosshair);\n', '\n'),
    (V, 'V 303 del', '    std::fprintf(file, "spawnonstart %d\\n", spawnOnStart);\n', '\n'),
    (V, 'V 304 del', '    std::fprintf(file, "teamlives %d\\n", teamLives);\n', '\n'),
    (V, 'V 308 cmp', '    if (codec >= 1 && codec <= 7)\n', '    if (codec >= 1 && codec < 7)\n'),
    (V, 'V 308 num', '    if (codec >= 1 && codec <= 7)\n', '    if (codec >= 2 && codec <= 7)\n'),
    (V, 'V 309 del', '        std::fprintf(file, "voicechat %s\\n", VOICE_CODEC_KEYWORDS[codec - 1]);\n', '\n'),
    (V, 'V 309 num', '        std::fprintf(file, "voicechat %s\\n", VOICE_CODEC_KEYWORDS[codec - 1]);\n', '        std::fprintf(file, "voicechat %s\\n", VOICE_CODEC_KEYWORDS[codec - 2]);\n'),
    (V, 'V 310 del', '    std::fprintf(file, "server\\n");\n', '\n'),
    (V, 'V 311 del', '    std::fclose(file);\n', '\n'),
]
