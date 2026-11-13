// The morph (game/human_morph.cpp) compiled here, in a translation unit of its own (HUMAN_MORPH builds,
// build_morph.bat): its tables of Tables\morph.sav live in an anonymous namespace, which this file can reach. The
// test fills them and the original's (0x00984130, 0x00983550) alike.
#define private public
#include "game/human_morph.cpp"   // (through -I src: a mutation run's mutated copy, mutrun.py MUT_WRAPPER)

extern void *const g_portMorphWeights = g_morphWeights;
extern void *const g_portMorphFlags = g_morphFlags;
extern bool *const g_portMorphLoaded = &g_morphTablesLoaded;
extern const unsigned g_morphWeightsSize = sizeof(g_morphWeights);
extern const unsigned g_morphFlagsSize = sizeof(g_morphFlags);
