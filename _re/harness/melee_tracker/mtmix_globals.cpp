// Raw storage for the objects the meleeTracker's mixer only names (the cache's Get is faked in mtmix_diff.cpp; g++
// does not put a variable's type in its name).
alignas(16) unsigned char g_animationCache[0x100];
