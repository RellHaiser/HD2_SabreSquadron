// Human part 82: the globals game/melee_tracker.cpp (and the controllers it links) declares as engine objects: storage
// here (the harness never constructs them; the members it reaches are its fakes, defined in mt_diff.cpp).
#include <cstdint>

alignas(16) unsigned char g_actorsModule[0x400];
alignas(16) unsigned char g_sightModule[0x100];
alignas(16) unsigned char g_controls[0x400];
alignas(16) unsigned char g_texts[0x100];
alignas(16) unsigned char g_modelCache[0x100];
alignas(16) unsigned char g_animationCache[0x100];
void *g_actorCreatorSlots[40];
void **g_actorCreators = g_actorCreatorSlots;
uint32_t g_gameRole = 7;
