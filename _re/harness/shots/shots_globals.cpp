// Raw storage for the modules shots.cpp only names, so those classes need not be linked (their methods the
// test uses are faked in shots_test.cpp; g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_collisionWorld[0x4000];
alignas(16) unsigned char g_gameProfile[0x2000];
alignas(16) unsigned char g_modelCache[0x400];
alignas(16) unsigned char g_soundCache[0x400];
alignas(16) unsigned char g_netGame[0x1000];
alignas(16) unsigned char g_particlesModule[0x1000];
alignas(16) unsigned char g_teamManager[0x2000];
