// The net game, the profile, the actors module, the texts and the pool the players' test (players_diff.cpp) reads:
// raw storage, so their classes' constructors need not be linked (a variable's symbol carries no type).
alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_netGame[0x8000];
alignas(16) unsigned char g_actorsModule[0x400];
alignas(16) unsigned char g_texts[0x400];
alignas(16) unsigned char g_netPool[0x100];
