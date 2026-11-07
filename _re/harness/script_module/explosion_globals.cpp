// Raw storage for what the explosions and the periodic links read, so those classes need not be linked.
#include <cstdint>

alignas(16) unsigned char g_collisionWorld[0x4000];
alignas(16) unsigned char g_actorsModule[0x1000];
