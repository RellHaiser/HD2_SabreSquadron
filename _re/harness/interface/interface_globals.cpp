// Raw storage for the objects the interface's parts only name (their methods the tests use are faked in each test;
// g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_teamManager[0x1000];
alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_texts[0x100];
alignas(16) unsigned char g_actorsModule[0x400];
alignas(16) unsigned char g_netGame[0x8000];
alignas(16) unsigned char g_scriptModule[0x1000];
alignas(16) unsigned char g_modelCache[0x400];
alignas(16) unsigned char g_controls[0x400];
alignas(16) unsigned char g_collisionWorld[0x430];
alignas(16) unsigned char g_itemsModule[0x400];
int g_localPlayerSlot;
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
