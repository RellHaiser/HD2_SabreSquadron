"""Part 9h-xvii mutations: the main menu's hosted session set-up
(game/main_menu.cpp).

Run with the factory build: --build build_factory.bat --check check_mainmenu.py (FACTORY_TEST; the
"mainmenu:" checks). Generated (deletions, comparison, number, bool and negation flips; the lines
unique in the file) and by hand (the ids, the messages, the comparisons).

45 mutations: 43 killed, 2 not building.
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
V = os.path.join(ROOT, r"src/game/main_menu.cpp")

MUTATIONS = [
    (V, 'V 2504 num', 'constexpr size_t SESSION_NAME_SIZE = 100, SESSION_PLAYER_SIZE = 260;\n', 'constexpr size_t SESSION_NAME_SIZE = 101, SESSION_PLAYER_SIZE = 260;\n'),
    (V, 'V 2506 num', 'constexpr int DEFAULT_COOP_DIFFICULTY = 3, DEFAULT_COOP_LIVES = -1;\n', 'constexpr int DEFAULT_COOP_DIFFICULTY = 4, DEFAULT_COOP_LIVES = -1;\n'),
    (V, 'V 2512 del', '    S_MultiplayerSettings *session = g_mpSettings;\n', '\n'),
    (V, 'V 2513 del', '    session->Reset();\n', '\n'),
    (V, 'V 2514 del', '    session->sessionType = SESSION_HOSTING;\n', '\n'),
    (V, 'V 2517 del', '    std::strncpy(names, m_mpSettings->m_setup.serverName.c_str(), SESSION_NAME_SIZE - 1);\n', '\n'),
    (V, 'V 2517 num', '    std::strncpy(names, m_mpSettings->m_setup.serverName.c_str(), SESSION_NAME_SIZE - 1);\n', '    std::strncpy(names, m_mpSettings->m_setup.serverName.c_str(), SESSION_NAME_SIZE - 2);\n'),
    (V, 'V 2518 del', '    std::strncpy(names + SESSION_NAME_SIZE, m_profileName.c_str(), SESSION_PLAYER_SIZE - 1);\n', '\n'),
    (V, 'V 2518 num', '    std::strncpy(names + SESSION_NAME_SIZE, m_profileName.c_str(), SESSION_PLAYER_SIZE - 1);\n', '    std::strncpy(names + SESSION_NAME_SIZE, m_profileName.c_str(), SESSION_PLAYER_SIZE - 2);\n'),
    (V, 'V 2519 del', '    std::memcpy(session->sessionName, names, sizeof names);\n', '\n'),
    (V, 'V 2520 del', '    session->gameType = GetControlValue(CREATE_STYLE);\n', '\n'),
    (V, 'V 2521 del', '    session->unknown174 = GetControlValue(CREATE_INTERNET) != 0;\n', '\n'),
    (V, 'V 2521 cmp', '    session->unknown174 = GetControlValue(CREATE_INTERNET) != 0;\n', '    session->unknown174 = GetControlValue(CREATE_INTERNET) == 0;\n'),
    (V, 'V 2521 num', '    session->unknown174 = GetControlValue(CREATE_INTERNET) != 0;\n', '    session->unknown174 = GetControlValue(CREATE_INTERNET) != 1;\n'),
    (V, 'V 2522 del', '    session->dedicated = GetControlValue(CREATE_DEDICATED) != 0;\n', '\n'),
    (V, 'V 2522 cmp', '    session->dedicated = GetControlValue(CREATE_DEDICATED) != 0;\n', '    session->dedicated = GetControlValue(CREATE_DEDICATED) == 0;\n'),
    (V, 'V 2522 num', '    session->dedicated = GetControlValue(CREATE_DEDICATED) != 0;\n', '    session->dedicated = GetControlValue(CREATE_DEDICATED) != 1;\n'),
    (V, 'V 2523 cmp', '    if (session->gameType == GAME_TYPE_COOPERATIVE) {\n', '    if (session->gameType != GAME_TYPE_COOPERATIVE) {\n'),
    (V, 'V 2524 del', '        session->coopDifficulty = GetControlValue(ROW_DIFFICULTY) + 1;\n', '\n'),
    (V, 'V 2524 num', '        session->coopDifficulty = GetControlValue(ROW_DIFFICULTY) + 1;\n', '        session->coopDifficulty = GetControlValue(ROW_DIFFICULTY) + 2;\n'),
    (V, 'V 2525 del', '        session->coopLives = GetControlValue(ROW_LIVES);\n', '\n'),
    (V, 'V 2526 del', '        session->teamLives = GetControlValue(ROW_TEAM_LIVES);\n', '\n'),
    (V, 'V 2528 del', '        session->coopDifficulty = DEFAULT_COOP_DIFFICULTY;\n', '\n'),
    (V, 'V 2529 del', '        session->coopLives = DEFAULT_COOP_LIVES;\n', '\n'),
    (V, 'V 2530 del', '        session->teamLives = 0;\n', '\n'),
    (V, 'V 2530 num', '        session->teamLives = 0;\n', '        session->teamLives = 1;\n'),
    (V, 'V 2532 del', '    session->maxClients = GetControlValue(ROW_PLAYERS);\n', '\n'),
    (V, 'V 2533 del', '    session->pointLimit = GetControlValue(ROW_POINT_LIMIT);\n', '\n'),
    (V, 'V 2534 del', '    session->roundLimit = GetControlValue(ROW_ROUND_LIMIT);\n', '\n'),
    (V, 'V 2535 del', '    session->roundCount = GetControlValue(ROW_ROUND_COUNT);\n', '\n'),
    (V, 'V 2536 del', '    session->warmup = GetControlValue(ROW_WARMUP);\n', '\n'),
    (V, 'V 2537 del', '    session->respawnTime = GetControlValue(ROW_RESPAWN_TIME);\n', '\n'),
    (V, 'V 2538 del', '    session->spawnProtection = GetControlValue(ROW_SPAWN_PROTECTION);\n', '\n'),
    (V, 'V 2539 del', '    session->inverseDamage = GetControlValue(ROW_INVERSE_DAMAGE);\n', '\n'),
    (V, 'V 2540 del', '    session->allowRespawn = GetControlValue(ROW_ALLOW_RESPAWN);\n', '\n'),
    (V, 'V 2541 del', '    session->friendlyFire = GetControlValue(ROW_FRIENDLY_FIRE);\n', '\n'),
    (V, 'V 2542 del', '    session->autoTeamBalance = GetControlValue(ROW_TEAM_BALANCE);\n', '\n'),
    (V, 'V 2543 del', '    session->thirdPersonView = GetControlValue(ROW_THIRD_PERSON);\n', '\n'),
    (V, 'V 2544 del', '    session->allowCrosshair = GetControlValue(ROW_CROSSHAIR);\n', '\n'),
    (V, 'V 2545 del', '    session->password = GetEditText(CREATE_PASSWORD);\n', '\n'),
    (V, 'V 2546 del', '    session->port = GetControlValue(CREATE_PORT);\n', '\n'),
    (V, 'V 2547 del', '    session->unknown178 = GetControlValue(CREATE_VOICE);\n', '\n'),
    (V, 'V 2550 del', '        session->maps.push_back(new C_MpMap(*map));\n', '\n'),
    (V, 'V 2504 arg0', 'constexpr size_t SESSION_NAME_SIZE = 100, SESSION_PLAYER_SIZE = 260;', 'constexpr size_t SESSION_NAME_SIZE = 99, SESSION_PLAYER_SIZE = 261;'),
    (V, 'V 2506 arg1', 'constexpr int DEFAULT_COOP_DIFFICULTY = 3, DEFAULT_COOP_LIVES = -1;', 'constexpr int DEFAULT_COOP_DIFFICULTY = 2, DEFAULT_COOP_LIVES = -1;'),
]
