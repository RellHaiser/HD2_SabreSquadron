// The profile the network game reads (its multiplayer settings, its detail settings): raw storage, so its class's
// constructor and vtable need not be linked (a variable's symbol carries no type).
alignas(16) unsigned char g_gameProfile[0x4000];
// The actors module (GetActor faked by index).
alignas(16) unsigned char g_actorsModule[0x400];
// The game's version (1.2).
int g_versionMajor = 1;
int g_versionMinor = 2;
