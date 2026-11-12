// Raw storage for the objects the locomotion only names (g++ does not put a variable's type in its name; the test
// fills what is read).
#include <cstdint>

alignas(16) unsigned char g_teamManager[0x400];
void *g_mpSettingsStorage;
void *&g_mpSettings = g_mpSettingsStorage;
uint32_t g_gameRole = 7;                                // GAME_ROLE_LOCAL
// g_noPhysicsObject: &g_noPhysicsHandle {-1, false} (physics.cpp is not linked).
struct S_Handle {
    int index;
    bool active;
} g_noPhysicsHandle = {-1, false};
extern S_Handle *const g_noPhysicsObject;
S_Handle *const g_noPhysicsObject = &g_noPhysicsHandle;
// (The movement base's, HUMAN_BASE builds: named only.)
alignas(16) unsigned char g_actorsModule[0x400];
alignas(16) unsigned char g_temporaryActors[0x100];
alignas(16) unsigned char g_particlesModule[0x400];
void *g_materials;
uint32_t g_materialCount;
float g_footprintBrightness = 1.0f;
uint32_t g_surfaceMarksEnabled;
