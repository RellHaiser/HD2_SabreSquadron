// Raw storage for the objects the actor lists only name (their methods the test uses are faked in actors_test.cpp;
// g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_itemsModule[0x1000];
alignas(16) unsigned char g_collisionWorld[0x2000];
alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_modelCache[0x400];
alignas(16) unsigned char g_netGame[0x4000];
void *g_netPlayerTable;
int g_netPlayerCount;
void *g_actorCreators;                                  // C_ActorCreator ** (set by the test)
static int g_noPhysicsHandle;
extern void *const g_noPhysicsObject = &g_noPhysicsHandle;
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
