// The profile the network game reads (its multiplayer settings, its detail settings): raw storage, so its class's
// constructor and vtable need not be linked (a variable's symbol carries no type).
alignas(16) unsigned char g_gameProfile[0x4000];
