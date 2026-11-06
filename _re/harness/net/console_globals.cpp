// The profile and the net game the console commands read (console_test.cpp): raw storage, so their classes'
// constructors need not be linked (a variable's symbol carries no type).
alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_netGame[0x8000];
// (The game's commands, part 10e-ii.)
alignas(16) unsigned char g_controls[0x4000];
alignas(16) unsigned char g_teamManager[0x1000];
alignas(16) unsigned char g_itemsModule[0x1000];
