// The profile and the net game the search's test (search_diff.cpp) reads: raw storage, so their classes'
// constructors need not be linked (a variable's symbol carries no type; zeroed: the net game's containers empty).
alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_netGame[0x6000];
