// Raw storage for the objects the game's saves only name (their methods the test uses are faked in game_save_diff.cpp;
// g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_actorsModule[0x400];
alignas(16) unsigned char g_netGame[0x4000];
alignas(16) unsigned char g_stainManager[0x400];
alignas(16) unsigned char g_texts[0x400];
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
