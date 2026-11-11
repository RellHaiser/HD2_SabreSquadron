// Raw storage for the modules the item system only names (their methods the test uses are faked in items_test.cpp;
// g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_actorsModule[0x1000];
alignas(16) unsigned char g_modelCache[0x400];
alignas(16) unsigned char g_soundCache[0x400];
alignas(16) unsigned char g_teamManager[0x2000];
alignas(16) unsigned char g_shotManager[0x2000];
alignas(16) unsigned char g_particlesModule[0x2000];
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
