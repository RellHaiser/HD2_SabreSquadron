// The profile, the texts and the actors' module the session's test (sess_diff.cpp) reads: raw storage, so their
// classes' constructors need not be linked (a variable's symbol carries no type).
alignas(16) unsigned char g_gameProfile[0x4000];
alignas(16) unsigned char g_texts[0x400];
alignas(16) unsigned char g_actorsModule[0x400];
