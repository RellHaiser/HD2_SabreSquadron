// The globals controls.cpp names but the controls' tests never reach (later parts fake them): raw storage, so their
// classes need not be linked (a variable's symbol carries no type).
alignas(16) unsigned char g_controls[0x100];
alignas(16) unsigned char g_gameProfile[0x8000];
alignas(16) unsigned char g_netGame[0x4000];
alignas(16) unsigned char g_texts[0x100];
int g_versionMajor = 1, g_versionMinor = 4;
