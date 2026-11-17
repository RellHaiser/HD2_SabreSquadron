// Forced into every unit of formation_test.exe (build_formation.bat: -include): each _aligned_free
// goes through FormationCountedFree (formation_test.cpp), which counts it. The slot array's class
// operator delete[] (team.h) frees with _aligned_free, as the original's SetType (0x00597420) does
// once per free, inline; the global aligned delete[] the test replaced before is not called any more.
#pragma once
#include <malloc.h>

void FormationCountedFree(void *p);
#define _aligned_free FormationCountedFree
