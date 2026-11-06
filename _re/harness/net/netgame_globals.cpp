// The profile the network game reads (its multiplayer settings, its detail settings): raw storage, so its class's
// constructor and vtable need not be linked (a variable's symbol carries no type).
alignas(16) unsigned char g_gameProfile[0x4000];
// The actors module (GetActor faked by index).
alignas(16) unsigned char g_actorsModule[0x400];
// The temporary actors (WriteState faked; the messages' build).
alignas(16) unsigned char g_temporaryActors[0x400];
// The loading screen and the team manager (the commands' build: AddPlayer faked; the player team's vtable set).
alignas(16) unsigned char g_loaderScreen[0x1000];
alignas(16) unsigned char g_teamManager[0x1000];
// Fields of the original's C_NetGame, the game's own globals.
unsigned g_graphFlags;
int g_netMaxFreq, g_netMaxPing, g_netMaxInactivity;
// The game's version (1.2).
int g_versionMajor = 1;
int g_versionMinor = 2;
