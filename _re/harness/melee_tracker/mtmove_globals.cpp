// Raw storage for the objects the meleeTracker's movement only names (their methods the test uses are faked in
// mtmove_diff.cpp; g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_animationCache[0x100];
alignas(16) unsigned char g_collisionWorld[0x4000];
alignas(16) unsigned char g_teamManager[0x4000];
void *g_materials = nullptr;
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
uint32_t g_materialCount = 0x60;
float g_moveIterations = 10.0f;
