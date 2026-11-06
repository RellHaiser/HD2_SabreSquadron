// The game's objects script_module_save.cpp reads: raw storage, so their classes' constructors need not be linked (a
// variable's symbol carries no type).
alignas(16) unsigned char g_teamManager[0x1000];
alignas(16) unsigned char g_particlesModule[0x2000];
alignas(16) unsigned char g_animationCache[0x100];
extern unsigned char g_scriptModuleStorage[];
int g_gameRole;
unsigned char g_netPlayerTable[0x400];
int g_netPlayerCount;
