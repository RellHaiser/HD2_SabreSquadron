// The HUMAN_ANIMTABLES build's stand-in for g_animationCache (game/resource_cache.cpp is not linked): only its
// address reaches the wrapped C_ResourceCache::Get (animtables_test.inc). Here, apart from the header that declares
// the real object.
alignas(16) unsigned char g_animationCache[0x100];
