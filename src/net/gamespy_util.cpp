// The GameSpy SDK's utilities (part 10f-i): its dynamic array and hash table, MD5, small helpers. See gamespy_internal.h.
#include "net/gamespy_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---- darray.c ----------------------------------------------------------------------------------------------------------

S_DArray *ArrayNew(int elementSize, int initial, ArrayElementFreeFn freeElement)
{
    auto *array = static_cast<S_DArray *>(std::malloc(sizeof(S_DArray)));
    if (initial == 0)
        initial = 8;
    array->count = 0;
    array->capacity = initial;
    array->elementSize = elementSize;
    array->growBy = initial;
    array->freeElement = freeElement;
    array->list = static_cast<char *>(std::malloc(static_cast<size_t>(elementSize * initial)));
    return array;
}

namespace {

void FreeElement(S_DArray *array, int index)            // 0x00732f00
{
    if (array->freeElement)
        array->freeElement(ArrayNth(array, index));
}

void SetElement(S_DArray *array, const void *element, int index)   // 0x00733040
{
    std::memcpy(ArrayNth(array, index), element, static_cast<size_t>(array->elementSize));
}

void Grow(S_DArray *array)                              // 0x00733010
{
    array->capacity += array->growBy;
    array->list = static_cast<char *>(std::realloc(array->list, static_cast<size_t>(array->elementSize * array->capacity)));
}

// The first element from `base` on that compares equal to `key`, null for none.
char *LinearSearch(const void *key, char *base, int count, int size, ArrayCompareFn compare)   // 0x007332f0
{
    for (int i = 0; i < count; ++i) {
        if (compare(key, base + i * size) == 0)
            return base + i * size;
    }
    return nullptr;
}

// Where `key` is or would go; `*found` says which.
char *BinarySearch(const void *key, char *base, int count, int size, ArrayCompareFn compare, bool *found)   // 0x00733350
{
    int high = count - 1;
    int low = 0;
    *found = false;
    while (low <= high) {
        const int middle = (high + low) >> 1;
        const int order = compare(base + middle * size, key);
        if (order == 0)
            *found = true;
        if (order >= 0)
            high = middle - 1;
        else
            low = middle + 1;
    }
    return base + low * size;
}

}  // namespace

void ArrayFree(S_DArray *array)
{
    for (int i = 0; i < array->count; ++i)
        FreeElement(array, i);
    std::free(array->list);
    std::free(array);
}

int ArrayLength(const S_DArray *array)
{
    return array->count;
}

void *ArrayNth(const S_DArray *array, int index)
{
    return array->list + array->elementSize * index;
}

void ArrayAppend(S_DArray *array, const void *element)
{
    if (array)
        ArrayInsertAt(array, element, array->count);
}

void ArrayInsertAt(S_DArray *array, const void *element, int index)
{
    if (array->count == array->capacity)
        Grow(array);
    const int last = array->count++;
    if (index < last) {
        std::memmove(ArrayNth(array, index + 1), ArrayNth(array, index),
                     static_cast<size_t>((array->count - index - 1) * array->elementSize));
    }
    SetElement(array, element, index);
}

void ArrayRemoveAt(S_DArray *array, int index)
{
    if (index < array->count - 1) {
        std::memmove(ArrayNth(array, index), ArrayNth(array, index + 1),
                     static_cast<size_t>((array->count - index - 1) * array->elementSize));
    }
    --array->count;
}

void ArrayDeleteAt(S_DArray *array, int index)
{
    FreeElement(array, index);
    ArrayRemoveAt(array, index);
}

void ArrayReplaceAt(S_DArray *array, const void *element, int index)
{
    FreeElement(array, index);
    SetElement(array, element, index);
}

int ArraySearch(const S_DArray *array, const void *key, ArrayCompareFn compare, int from, bool sorted)
{
    if (!array || array->count == 0)
        return -1;
    bool found = true;
    auto *base = static_cast<char *>(ArrayNth(array, from));
    char *at = sorted ? BinarySearch(key, base, array->count - from, array->elementSize, compare, &found)
                      : LinearSearch(key, base, array->count - from, array->elementSize, compare);
    if (!at || !found)
        return -1;
    return static_cast<int>((at - array->list) / array->elementSize);
}

void ArrayClear(S_DArray *array)
{
    for (int i = ArrayLength(array) - 1; i >= 0; --i)
        ArrayDeleteAt(array, i);
}

// ---- hashtable.c -------------------------------------------------------------------------------------------------------

S_HashTable *TableNew2(int elementSize, int buckets, int chainSize, TableHashFn hash, ArrayCompareFn compare,
                       ArrayElementFreeFn freeElement)
{
    auto *table = static_cast<S_HashTable *>(std::malloc(sizeof(S_HashTable)));
    table->buckets = static_cast<S_DArray **>(std::malloc(static_cast<size_t>(buckets) * sizeof(S_DArray *)));
    for (int i = 0; i < buckets; ++i)
        table->buckets[i] = ArrayNew(elementSize, chainSize, freeElement);
    table->freeElement = freeElement;
    table->bucketCount = buckets;
    table->compare = compare;
    table->hash = hash;
    return table;
}

void TableFree(S_HashTable *table)
{
    for (int i = 0; i < table->bucketCount; ++i)
        ArrayFree(table->buckets[i]);
    std::free(table->buckets);
    std::free(table);
}

void TableEnter(S_HashTable *table, const void *element)
{
    S_DArray *bucket = table->buckets[table->hash(element, table->bucketCount)];
    const int index = ArraySearch(bucket, element, table->compare, 0, false);
    if (index == -1)
        ArrayAppend(bucket, element);
    else
        ArrayReplaceAt(bucket, element, index);
}

bool TableRemove(S_HashTable *table, const void *element)
{
    S_DArray *bucket = table->buckets[table->hash(element, table->bucketCount)];
    const int index = ArraySearch(bucket, element, table->compare, 0, false);
    if (index == -1)
        return false;
    ArrayDeleteAt(bucket, index);
    return true;
}

void *TableLookup(S_HashTable *table, const void *element)
{
    S_DArray *bucket = table->buckets[table->hash(element, table->bucketCount)];
    const int index = ArraySearch(bucket, element, table->compare, 0, false);
    return index == -1 ? nullptr : ArrayNth(bucket, index);
}

// ---- md5c.c (RSA's reference code) ------------------------------------------------------------------------------------

namespace {

struct S_Md5 {                                          // 0x58 bytes
    uint32_t state[4];
    uint32_t count[2];                                  // +0x10 bits
    uint8_t buffer[64];                                 // +0x18
};

inline uint32_t Rotate(uint32_t x, int n) { return x << n | x >> (32 - n); }
inline uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
inline uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
inline uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
inline uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }

void Md5Transform(uint32_t *state, const uint8_t *block)   // 0x00737200
{
    // The sine table, the shifts and the message indexes of RFC 1321's rounds.
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[4][4] = {{7, 12, 17, 22}, {5, 9, 14, 20}, {4, 11, 16, 23}, {6, 10, 15, 21}};
    uint32_t x[16];
    for (int i = 0; i < 16; ++i) {                      // (0x00737b80)
        x[i] = static_cast<uint32_t>(block[i * 4]) | static_cast<uint32_t>(block[i * 4 + 1]) << 8 |
               static_cast<uint32_t>(block[i * 4 + 2]) << 16 | static_cast<uint32_t>(block[i * 4 + 3]) << 24;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int m;
        switch (i / 16) {
        case 0:
            f = F(b, c, d);
            m = i;
            break;
        case 1:
            f = G(b, c, d);
            m = (5 * i + 1) % 16;
            break;
        case 2:
            f = H(b, c, d);
            m = (3 * i + 5) % 16;
            break;
        default:
            f = I(b, c, d);
            m = (7 * i) % 16;
            break;
        }
        const uint32_t next = b + Rotate(a + f + x[m] + K[i], S[i / 16][i % 4]);
        a = d;
        d = c;
        c = b;
        b = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void Md5Init(S_Md5 *context)                            // 0x007370c0
{
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
    context->count[0] = context->count[1] = 0;
}

void Md5Update(S_Md5 *context, const void *data, unsigned size)   // 0x007370f0
{
    const auto *input = static_cast<const uint8_t *>(data);
    unsigned index = context->count[0] >> 3 & 0x3F;
    context->count[0] += size * 8;
    if (context->count[0] < size * 8)
        ++context->count[1];
    context->count[1] += size >> 29;
    const unsigned part = 64 - index;
    unsigned i = 0;
    if (size >= part) {
        std::memcpy(context->buffer + index, input, part);
        Md5Transform(context->state, context->buffer);
        for (i = part; i + 63 < size; i += 64)
            Md5Transform(context->state, input + i);
        index = 0;
    }
    std::memcpy(context->buffer + index, input + i, size - i);
}

void Md5Final(uint8_t *digest, S_Md5 *context)          // 0x00737190
{
    static const uint8_t PADDING[64] = {0x80};          // 0x008455a8
    uint8_t bits[8];
    std::memcpy(bits, context->count, sizeof bits);
    const unsigned index = context->count[0] >> 3 & 0x3F;
    Md5Update(context, PADDING, (index < 56 ? 56 : 120) - index);
    Md5Update(context, bits, sizeof bits);
    std::memcpy(digest, context->state, 16);
    std::memset(context, 0, sizeof *context);
}

}  // namespace

void MD5Digest(const void *data, unsigned size, char *hex)
{
    S_Md5 context;
    uint8_t digest[16];
    Md5Init(&context);
    Md5Update(&context, data, size);
    Md5Final(digest, &context);
    for (int i = 0; i < 16; ++i)
        std::sprintf(hex + i * 2, "%02x", digest[i]);
}

// ---- small helpers -----------------------------------------------------------------------------------------------------

char *goastrdup(const char *text)
{
    if (!text)
        return nullptr;
    const size_t size = std::strlen(text) + 1;
    auto *copy = static_cast<char *>(std::malloc(size));
    if (copy)
        std::memcpy(copy, text, size);
    return copy;
}

bool CanReceiveOnSocket(SOCKET socket)
{
    fd_set read;
    FD_ZERO(&read);
    FD_SET(socket, &read);
    timeval zero = {0, 0};
    const int ready = select(0x40, &read, nullptr, nullptr, &zero);
    return ready != SOCKET_ERROR && ready != 0;
}

void SocketStartUp()
{
    WSADATA data;
    WSAStartup(0x101, &data);
}
