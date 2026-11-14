// The game's sort of 8-byte (key, value) pairs by the key as an unsigned number: a least
// significant byte first radix sort, so pairs with equal keys keep their order. Used for the
// particles' depth order and the explosive timers.
#pragma once

#include <cstdint>

struct S_RadixPair {
    uint32_t key;
    uint32_t value;
};
static_assert(sizeof(S_RadixPair) == 8, "S_RadixPair is 8 bytes");

void RadixSortPairs(S_RadixPair *pairs, int count);   // 0x006e4ce0 (__fastcall)
