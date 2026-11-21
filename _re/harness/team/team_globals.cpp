// Raw storage for the objects the teams only name (their methods the tests use are faked in team_diff.cpp; g++ does
// not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_actorsModule[0x400];
alignas(16) unsigned char g_sightModule[0x400];
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
