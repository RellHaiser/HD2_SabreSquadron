// Raw storage for the objects the physics code only names (their methods the test uses are faked in
// physics_test.cpp; g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_temporaryActors[0x400];
alignas(16) unsigned char g_modelCache[0x400];
void *g_netPlayerTable;
int g_netPlayerCount;
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
float g_moveIterations = 10.0f;                         // 0x008441e4
