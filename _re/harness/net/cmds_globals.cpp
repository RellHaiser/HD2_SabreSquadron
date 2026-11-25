// The net game, the profile, the actors modules, the texts, the loading screen and the team manager the commands' test
// (cmds_diff.cpp) reads: raw storage, so their classes' constructors need not be linked (a variable's symbol carries no
// type).
alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_netGame[0x8000];
alignas(16) unsigned char g_actorsModule[0x400];
alignas(16) unsigned char g_temporaryActors[0x400];
alignas(16) unsigned char g_texts[0x400];
alignas(16) unsigned char g_loaderScreen[0x1000];
alignas(16) unsigned char g_teamManager[0x1000];
