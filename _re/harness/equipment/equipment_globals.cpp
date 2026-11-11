// Raw storage for the modules human_equipment.cpp only names (their methods the test uses are faked in
// equipment_test.cpp; g++ does not put a variable's type in its name).
#include <cstdint>

alignas(16) unsigned char g_actorsModule[0x1000];
alignas(16) unsigned char g_animationCache[0x400];
alignas(16) unsigned char g_modelCache[0x400];
alignas(16) unsigned char g_teamManager[0x2000];
alignas(16) unsigned char g_humanAnimTable[0x100];
