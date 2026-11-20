// Raw storage for the objects the interface's parts only name (their methods the tests use are faked in each test;
// g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_teamManager[0x1000];
alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_texts[0x100];
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
