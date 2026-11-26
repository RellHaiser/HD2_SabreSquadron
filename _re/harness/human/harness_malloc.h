// Forced into every source of the HUMAN_DEAD build (-include): the port's _aligned_free goes through the harness, which
// skips the frees while a call is compared (the dead body drops ragdoll parts and slide points that the other side's
// run, from the same restored state, still reads; they are leaked instead).
#pragma once
#include <malloc.h>

extern bool g_harnessKeepFrees;
void HarnessAlignedFree(void *p);
void HarnessDeferFree(void *p);       // (a plain free deferred while a call is compared: dead_test.inc)
#define _aligned_free(p) HarnessAlignedFree(p)
// (HUMAN_GUARD_ALLOC: every aligned block, both sides', ends flush against a no-access page; dead_test.inc.)
void *HarnessAlignedMalloc(size_t size, size_t alignment);
#define _aligned_malloc(size, alignment) HarnessAlignedMalloc(size, alignment)
