#include "radix_sort.h"

#include <cstdlib>
#include <cstring>

namespace {

// Bucket counts of each key byte; left all zero after a sort.
uint32_t g_countsByte0[256];   // 0x008c0b50
uint32_t g_countsByte1[256];   // 0x008c0350
uint32_t g_countsByte2[256];   // 0x008c0750
uint32_t g_countsByte3[256];   // 0x008c0f50

// The second buffer of the passes, grown to the largest sort (never freed).
S_RadixPair *g_scratch;        // 0x008c1360
uint32_t g_scratchSize;        // 0x008c1364 (bytes)

// One pass: every pair of src to its bucket's next slot in dst (offsets: each bucket's start).
void ScatterByte0(S_RadixPair *dst, const S_RadixPair *src, uint32_t *offsets, int count)
{
    // 0x006e4b60 (DS 0x0072f6b0)
    for (int i = 0; i < count; ++i)
        dst[offsets[src[i].key & 0xff]++] = src[i];
}

void ScatterByte1(S_RadixPair *dst, const S_RadixPair *src, uint32_t *offsets, int count)
{
    // 0x006e4bc0 (DS 0x0072f730)
    for (int i = 0; i < count; ++i)
        dst[offsets[(src[i].key >> 8) & 0xff]++] = src[i];
}

void ScatterByte2(S_RadixPair *dst, const S_RadixPair *src, uint32_t *offsets, int count)
{
    // 0x006e4c20 (DS 0x0072f7b0)
    for (int i = 0; i < count; ++i)
        dst[offsets[(src[i].key >> 16) & 0xff]++] = src[i];
}

void ScatterByte3(S_RadixPair *dst, const S_RadixPair *src, uint32_t *offsets, int count)
{
    // 0x006e4c80 (DS 0x0072f830)
    for (int i = 0; i < count; ++i)
        dst[offsets[src[i].key >> 24]++] = src[i];
}

// The buckets' start offsets into offsets; the counts are cleared for the next sort.
void TakeOffsets(uint32_t *counts, uint32_t *offsets)
{
    uint32_t sum = 0;
    for (int i = 0; i < 256; ++i) {
        offsets[i] = sum;
        sum += counts[i];
        counts[i] = 0;
    }
}

}   // namespace

void RadixSortPairs(S_RadixPair *pairs, int count)
{
    // 0x006e4ce0 (DS 0x0072f8b0)
    if (count == 0)
        return;
    for (int i = count - 1; i >= 0; --i) {
        const uint32_t key = pairs[i].key;
        ++g_countsByte0[key & 0xff];
        ++g_countsByte1[(key >> 8) & 0xff];
        ++g_countsByte2[(key >> 16) & 0xff];
        ++g_countsByte3[key >> 24];
    }
    const uint32_t size = static_cast<uint32_t>(count) * sizeof(S_RadixPair);
    if (g_scratchSize < size) {
        std::free(g_scratch);
        g_scratch = static_cast<S_RadixPair *>(std::malloc(size));
        g_scratchSize = size;
    }

    // A pass is skipped when every key has the same byte there (tested for the values 0 and 255;
    // 128 and 127 for the top byte); only that count is then nonzero.
    S_RadixPair *src = pairs;
    S_RadixPair *dst = g_scratch;
    uint32_t offsets[256];
    if (g_countsByte0[0] == static_cast<uint32_t>(count)) {
        g_countsByte0[0] = 0;
    } else if (g_countsByte0[255] == static_cast<uint32_t>(count)) {
        g_countsByte0[255] = 0;
    } else {
        TakeOffsets(g_countsByte0, offsets);
        ScatterByte0(dst, src, offsets, count);
        dst = src;
        src = g_scratch;
    }
    if (g_countsByte1[0] == static_cast<uint32_t>(count)) {
        g_countsByte1[0] = 0;
    } else if (g_countsByte1[255] == static_cast<uint32_t>(count)) {
        g_countsByte1[255] = 0;
    } else {
        TakeOffsets(g_countsByte1, offsets);
        ScatterByte1(dst, src, offsets, count);
        S_RadixPair *swapped = src;
        src = dst;
        dst = swapped;
    }
    if (g_countsByte2[0] == static_cast<uint32_t>(count)) {
        g_countsByte2[0] = 0;
    } else if (g_countsByte2[255] == static_cast<uint32_t>(count)) {
        g_countsByte2[255] = 0;
    } else {
        TakeOffsets(g_countsByte2, offsets);
        ScatterByte2(dst, src, offsets, count);
        S_RadixPair *swapped = src;
        src = dst;
        dst = swapped;
    }
    if (g_countsByte3[128] == static_cast<uint32_t>(count)) {
        g_countsByte3[128] = 0;
    } else if (g_countsByte3[127] == static_cast<uint32_t>(count)) {
        g_countsByte3[127] = 0;
    } else {
        TakeOffsets(g_countsByte3, offsets);
        ScatterByte3(dst, src, offsets, count);
        src = dst;
    }
    if (src != pairs)
        std::memcpy(pairs, g_scratch, size);
}
