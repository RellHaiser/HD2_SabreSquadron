// The GameSpy SDK's utilities (part 10f-i): its dynamic array and hash table, MD5, small helpers. See gamespy_internal.h.
#include "net/gamespy_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

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

// ---- darray.c and hashtable.c, the parts the game's code does not use ----------------------------------

using ArrayMapFn = void (*)(void *element, void *data);
using ArrayMapFn2 = int (*)(void *element, void *data);

void ArraySetDataPtr(S_DArray *array, void *list, int count, int capacity)   // 0x00732f30 (DS 0x0078e110)
{
    if (array->list) {
        for (int i = 0; i < array->count; ++i)
            FreeElement(array, i);
        free(array->list);
    }
    array->list = static_cast<char *>(list);
    array->count = count;
    array->capacity = capacity;
}

void ArrayInsertSorted(S_DArray *array, const void *element, ArrayCompareFn compare)   // 0x00733070 (DS 0x0078e2e0)
{
    bool found;
    const char *place = BinarySearch(element, array->list, array->count, array->elementSize, compare, &found);
    ArrayInsertAt(array, element, static_cast<int>(place - array->list) / array->elementSize);
}

void ArraySort(S_DArray *array, ArrayCompareFn compare) // 0x00733140 (DS 0x0078e3f0)
{
    qsort(array->list, array->count, array->elementSize, compare);
}

void ArrayMapForward(S_DArray *array, ArrayMapFn map, void *data)   // 0x007331f0 (DS 0x0078e4e0)
{
    for (int i = 0; i < array->count; ++i)
        map(ArrayNth(array, i), data);
}

void ArrayMapBackward(S_DArray *array, ArrayMapFn map, void *data)  // 0x00733220 (DS 0x0078e520)
{
    for (int i = array->count; --i >= 0;)
        map(ArrayNth(array, i), data);
}

// The first element for which `map` returns 0, null if none.
void *ArrayMapForward2(S_DArray *array, ArrayMapFn2 map, void *data)    // 0x00733250 (DS 0x0078e560)
{
    for (int i = 0; i < array->count; ++i) {
        void *element = ArrayNth(array, i);
        if (map(element, data) == 0)
            return element;
    }
    return nullptr;
}

void *ArrayMapBackward2(S_DArray *array, ArrayMapFn2 map, void *data)   // 0x00733290 (DS 0x0078e5b0)
{
    for (int i = array->count; --i >= 0;) {
        void *element = ArrayNth(array, i);
        if (map(element, data) == 0)
            return element;
    }
    return nullptr;
}

int TableCount(const S_HashTable *table)                // 0x00736e80 (DS 0x00793390)
{
    int count = 0;
    for (int i = 0; i < table->bucketCount; ++i)
        count += ArrayLength(table->buckets[i]);
    return count;
}

void TableMap(S_HashTable *table, ArrayMapFn map, void *data)   // 0x00736f90 (DS 0x00793500)
{
    for (int i = 0; i < table->bucketCount; ++i)
        ArrayMapForward(table->buckets[i], map, data);
}

void TableMapSafe(S_HashTable *table, ArrayMapFn map, void *data)   // 0x00736fd0 (DS 0x00793550)
{
    for (int i = 0; i < table->bucketCount; ++i)
        ArrayMapBackward(table->buckets[i], map, data);
}

void *TableMap2(S_HashTable *table, ArrayMapFn2 map, void *data)    // 0x00737010 (DS 0x007935a0)
{
    for (int i = 0; i < table->bucketCount; ++i) {
        if (void *element = ArrayMapForward2(table->buckets[i], map, data))
            return element;
    }
    return nullptr;
}

void *TableMapSafe2(S_HashTable *table, ArrayMapFn2 map, void *data)    // 0x00737050 (DS 0x007935f0)
{
    for (int i = 0; i < table->bucketCount; ++i) {
        if (void *element = ArrayMapBackward2(table->buckets[i], map, data))
            return element;
    }
    return nullptr;
}

void TableClear(S_HashTable *table)                     // 0x00737090 (DS 0x00793640)
{
    for (int i = 0; i < table->bucketCount; ++i)
        ArrayClear(table->buckets[i]);
}

// ---- gbucket.c: named statistics; the game never makes a bucket set ------------------------------------

enum { BUCKET_INT = 0, BUCKET_DOUBLE = 1, BUCKET_STRING = 2 };

struct S_Bucket {                                       // 0x18 bytes
    char *name;
    int type;                                           // +0x04
    int players;                                        // +0x08 values averaged so far
    union {
        int asInt;
        double asDouble;
        char *asString;
    } value;                                            // +0x10
};

struct S_BucketSet {
    S_HashTable *buckets;
};

S_BucketSet *g_bucketSet = nullptr;                     // 0x0098c1a0 the default set

namespace {

int s_bucketInt;                                        // 0x00982108
double s_bucketDouble;                                  // 0x00982110

}  // namespace

void *BucketData(S_Bucket *bucket)                      // 0x00733a10 (DS 0x0078ef90)
{
    if (!bucket)
        return nullptr;
    return bucket->type == BUCKET_STRING ? static_cast<void *>(bucket->value.asString) : static_cast<void *>(&bucket->value);
}

char *BucketFixSlashes(char *text)                      // 0x007339f0 (DS 0x0078ef60)
{
    for (char *c = text; *c; ++c) {
        if (*c == '\\')
            *c = '/';
    }
    return text;
}

void *BucketDoSet(S_Bucket *bucket, const void *value)  // 0x00733970 (DS 0x0078eec0)
{
    if (bucket->type == BUCKET_INT) {
        bucket->value.asInt = *static_cast<const int *>(value);
    } else if (bucket->type == BUCKET_DOUBLE) {
        bucket->value.asDouble = *static_cast<const double *>(value);
    } else if (bucket->type == BUCKET_STRING) {
        if (bucket->value.asString)
            free(bucket->value.asString);
        bucket->value.asString = value ? BucketFixSlashes(goastrdup(static_cast<const char *>(value))) : nullptr;
    }
    return BucketData(bucket);
}

S_Bucket *BucketFind(S_BucketSet *set, const char *name)    // 0x00733a30 (DS 0x0078efc0)
{
    if (!set)
        set = g_bucketSet;
    S_Bucket key;
    key.name = const_cast<char *>(name);
    return static_cast<S_Bucket *>(TableLookup(set->buckets, &key));
}

void BucketElementFree(S_Bucket *bucket)                // 0x00733ac0 (DS 0x0078f0a0)
{
    free(bucket->name);
    if (bucket->type == BUCKET_STRING && bucket->value.asString)
        free(bucket->value.asString);
}

void *BucketInt(int value)                              // 0x00733840 (DS 0x0078ed30)
{
    s_bucketInt = value;
    return &s_bucketInt;
}

void *BucketDouble(double value)                        // 0x00733850 (DS 0x0078ed50)
{
    s_bucketDouble = value;
    return &s_bucketDouble;
}

void FreeBucketSet(S_BucketSet *set)                    // 0x00733400 (DS 0x0078e790)
{
    TableFree(set->buckets);
    free(set);
}

void *BucketNew(S_BucketSet *set, const char *name, int type, const void *value)    // 0x00733470 (DS 0x0078e830)
{
    if (!set)
        set = g_bucketSet;
    S_Bucket bucket;
    bucket.name = goastrdup(name);
    bucket.type = type;
    bucket.players = 1;
    bucket.value.asDouble = 0.0;
    BucketDoSet(&bucket, value);
    TableEnter(set->buckets, &bucket);
    return BucketData(BucketFind(set, name));
}

void BucketSet(S_BucketSet *set, const char *name, const void *value)   // 0x007334e0 (DS 0x0078e8c0)
{
    S_Bucket *bucket = BucketFind(set, name);
    if (!bucket)
        return;
    bucket->players = 0;
    BucketDoSet(bucket, value);
}

void *BucketConcat(S_BucketSet *set, const char *name, const char *value)   // 0x00733700, 0x00733710 (DS 0x0078eb90) (DS 0x0078eba0)
{
    S_Bucket *bucket = BucketFind(set, name);
    if (!bucket)
        return nullptr;
    const char *old = static_cast<const char *>(BucketData(bucket));
    char *joined = static_cast<char *>(malloc(strlen(old) + strlen(value) + 1));
    strcpy(joined, old);
    strcat(joined, value);
    BucketDoSet(bucket, joined);
    free(joined);
    return BucketData(bucket);
}

void BucketAdd(S_BucketSet *set, const char *name, const void *value)   // 0x00733520 (DS 0x0078e920)
{
    S_Bucket *bucket = BucketFind(set, name);
    if (!bucket)
        return;
    if (bucket->type == BUCKET_INT)
        BucketDoSet(bucket, BucketInt(*static_cast<const int *>(value) + bucket->value.asInt));
    else if (bucket->type == BUCKET_DOUBLE)
        BucketDoSet(bucket, BucketDouble(bucket->value.asDouble + *static_cast<const double *>(value)));
    else
        BucketConcat(set, name, static_cast<const char *>(value));
}

void BucketSub(S_BucketSet *set, const char *name, const void *value)   // 0x007335b0 (DS 0x0078e9e0)
{
    S_Bucket *bucket = BucketFind(set, name);
    if (!bucket)
        return;
    if (bucket->type == BUCKET_INT)
        BucketDoSet(bucket, BucketInt(bucket->value.asInt - *static_cast<const int *>(value)));
    else if (bucket->type == BUCKET_DOUBLE)
        BucketDoSet(bucket, BucketDouble(bucket->value.asDouble - *static_cast<const double *>(value)));
}

void BucketMult(S_BucketSet *set, const char *name, const void *value)  // 0x00733620 (DS 0x0078ea70)
{
    S_Bucket *bucket = BucketFind(set, name);
    if (!bucket)
        return;
    if (bucket->type == BUCKET_INT)
        BucketDoSet(bucket, BucketInt(*static_cast<const int *>(value) * bucket->value.asInt));
    else if (bucket->type == BUCKET_DOUBLE)
        BucketDoSet(bucket, BucketDouble(bucket->value.asDouble * *static_cast<const double *>(value)));
}

void BucketDiv(S_BucketSet *set, const char *name, const void *value)   // 0x00733690 (DS 0x0078eb00)
{
    S_Bucket *bucket = BucketFind(set, name);
    if (!bucket)
        return;
    if (bucket->type == BUCKET_INT)
        BucketDoSet(bucket, BucketInt(bucket->value.asInt / *static_cast<const int *>(value)));
    else if (bucket->type == BUCKET_DOUBLE)
        BucketDoSet(bucket, BucketDouble(bucket->value.asDouble / *static_cast<const double *>(value)));
}

void BucketAvg(S_BucketSet *set, const char *name, const void *value)   // 0x007337a0 (DS 0x0078ec60)
{
    S_Bucket *bucket = BucketFind(set, name);
    if (!bucket)
        return;
    if (bucket->type == BUCKET_INT) {
        ++bucket->players;
        BucketDoSet(bucket, BucketInt((bucket->value.asInt * bucket->players + *static_cast<const int *>(value)) / bucket->players));
    } else if (bucket->type == BUCKET_DOUBLE) {
        ++bucket->players;
        const double players = static_cast<double>(bucket->players);
        BucketDoSet(bucket, BucketDouble((bucket->value.asDouble * players + *static_cast<const double *>(value)) / players));
    }
}

// ---- gstats: the persistent storage service's replies ----------------------------------------------------------------------

namespace {

enum E_PersistKind { PERSIST_AUTH, PERSIST_GET, PERSIST_SET, PERSIST_SAVE };

struct S_PersistRequest {
    int kind;
    int localId;
    int key;                                            // the player's id (the auth reply fills it in)
    int profile, dataType;                              // passed back by the get and set callbacks
    void *instance;
    void *callback;
};

using PersistAuthFn = void (*)(int result, const char *error, void *instance);
using PersistGetFn = void (*)(int profile, int dataType, int result, const char *data, int size, void *instance);
using PersistSetFn = void (*)(int profile, int dataType, int result, void *instance);
using PersistSaveFn = void (*)(int result, void *instance);

S_DArray *g_persistRequests;                            // 0x00982bb4

// A key's value in a "\key\value" reply; the result stays valid for one more call.
const char *ValueForKey(const char *reply, const char *key)     // 0x00736080 (DS 0x00792110)
{
    static int index;
    static char values[2][256];
    char spec[256] = "\\";
    index ^= 1;
    strcat(spec, key);
    strcat(spec, "\\");
    const char *at = strstr(reply, spec);
    if (!at)
        return nullptr;
    at += strlen(spec);
    char *out = values[index];
    while (*at && *at != '\\')
        *out++ = *at++;
    *out = '\0';
    return values[index];
}

const char *ValueForKeySafe(const char *reply, const char *key)  // 0x00736700 (DS 0x00792970)
{
    const char *value = ValueForKey(reply, key);
    return value ? value : "";
}

int FindPersistRequest(int kind, int localId, int key)  // 0x00736710 (DS 0x00792990)
{
    if (g_persistRequests) {
        for (int i = 0; i < ArrayLength(g_persistRequests); ++i) {
            const auto *request = static_cast<const S_PersistRequest *>(ArrayNth(g_persistRequests, i));
            if (request->kind == kind && request->localId == localId && request->key == key)
                return i;
        }
    }
    return -1;
}

void FinishPersistRequest(int index, int result, const char *text, int size)    // 0x00736900 (DS 0x00792c10)
{
    if (index < 0 || index >= ArrayLength(g_persistRequests))
        return;
    const auto *r = static_cast<const S_PersistRequest *>(ArrayNth(g_persistRequests, index));
    if (r->callback) {
        switch (r->kind) {
        case PERSIST_AUTH:
            reinterpret_cast<PersistAuthFn>(r->callback)(result, text, r->instance);
            break;
        case PERSIST_GET:
            reinterpret_cast<PersistGetFn>(r->callback)(r->profile, r->dataType, result, text, size, r->instance);
            break;
        case PERSIST_SET:
            reinterpret_cast<PersistSetFn>(r->callback)(r->profile, r->dataType, result, r->instance);
            break;
        case PERSIST_SAVE:
            reinterpret_cast<PersistSaveFn>(r->callback)(result, r->instance);
            break;
        }
    }
    ArrayDeleteAt(g_persistRequests, index);
}

}  // namespace

void PersistAuthReply(const char *reply)                // 0x00736680 (DS 0x007928d0)
{
    const int result = atoi(ValueForKeySafe(reply, "pauthr"));
    const int localId = atoi(ValueForKeySafe(reply, "lid"));
    const char *error = ValueForKeySafe(reply, "errmsg");
    const int index = FindPersistRequest(PERSIST_AUTH, localId, 0);
    if (index != -1) {
        static_cast<S_PersistRequest *>(ArrayNth(g_persistRequests, index))->key = result;
        FinishPersistRequest(index, result > 0, error, 0);
    }
}

void PersistGetReply(const char *reply)                 // 0x007367e0 (DS 0x00792aa0)
{
    const int result = atoi(ValueForKeySafe(reply, "getpdr"));
    const int localId = atoi(ValueForKeySafe(reply, "lid"));
    const int key = atoi(ValueForKeySafe(reply, "pid"));
    const int index = FindPersistRequest(PERSIST_GET, localId, key);
    if (index == -1)
        return;
    const int size = atoi(ValueForKeySafe(reply, "length"));
    const char *data = strstr(reply, "\\data\\");
    if (data)
        FinishPersistRequest(index, result, data + 6, size);
    else
        FinishPersistRequest(index, result, "", 0);
}

void PersistSetReply(const char *reply)                 // 0x00736890 (DS 0x00792b80)
{
    const int result = atoi(ValueForKeySafe(reply, "setpdr"));
    const int key = atoi(ValueForKeySafe(reply, "pid"));
    const int localId = atoi(ValueForKeySafe(reply, "lid"));
    const int index = FindPersistRequest(PERSIST_SET, localId, key);
    if (index != -1)
        FinishPersistRequest(index, result, nullptr, 0);
}

// ---- gstats: a stats game's buckets; a bucket that does not exist is made with the value --------------------------------------

struct S_StatsGame {
    int reserved[3];
    S_BucketSet *buckets;                               // +0x0C
    uint8_t unknown10[0x0C];                            // +0x10
    S_DArray *players;                                  // +0x1C the players' numbers, by index
    S_DArray *teams;                                    // +0x20 the teams' numbers, by index
};

S_StatsGame *g_statsGame;                               // 0x00982b98 the game given by default
using BucketOpFn = void *(*)(S_BucketSet *set, const char *name, const void *value);

int StatsBucketInt(S_StatsGame *game, const char *name, BucketOpFn op, int value)   // 0x00736ad0 (DS 0x00792e70)
{
    if (!game && !(game = g_statsGame))
        return value;
    void *result = op(game->buckets, name, &value);
    if (!result)
        result = BucketNew(game->buckets, name, 0, &value);
    return *static_cast<int *>(result);
}

double StatsBucketDouble(S_StatsGame *game, const char *name, BucketOpFn op, double value)  // 0x00736b20 (DS 0x00792ee0)
{
    if (!game && !(game = g_statsGame))
        return value;
    void *result = op(game->buckets, name, &value);
    if (!result)
        result = BucketNew(game->buckets, name, 1, &value);
    return *static_cast<double *>(result);
}

char *StatsBucketString(S_StatsGame *game, const char *name, BucketOpFn op, char *value)    // 0x00736b70 (DS 0x00792f50)
{
    if (!game && !(game = g_statsGame))
        return value;
    void *result = op(game->buckets, name, value);
    if (!result)
        result = BucketNew(game->buckets, name, 2, value);
    return static_cast<char *>(result);
}

// A team's and a player's number in the game (the index itself with no game).
int StatsTeamNumber(S_StatsGame *game, int index)       // 0x00736a90 (DS 0x00792e10)
{
    if (!game && !(game = g_statsGame))
        return index;
    return *static_cast<int *>(ArrayNth(game->teams, index));
}

int StatsPlayerNumber(S_StatsGame *game, int index)     // 0x00736ab0 (DS 0x00792e40)
{
    if (!game && !(game = g_statsGame))
        return index;
    return *static_cast<int *>(ArrayNth(game->players, index));
}

// A team's buckets are "<name>_t<team number>", a player's "<name>_<player number>".
int StatsTeamBucketInt(S_StatsGame *game, const char *name, BucketOpFn op, int value, int index)   // 0x00736bc0 (DS 0x00792fc0)
{
    char full[64];
    std::sprintf(full, "%s_t%d", name, StatsTeamNumber(game, index));
    return StatsBucketInt(game, full, op, value);
}

double StatsTeamBucketDouble(S_StatsGame *game, const char *name, BucketOpFn op, double value, int index)   // 0x00736c10 (DS 0x00793030)
{
    char full[64];
    std::sprintf(full, "%s_t%d", name, StatsTeamNumber(game, index));
    return StatsBucketDouble(game, full, op, value);
}

char *StatsTeamBucketString(S_StatsGame *game, const char *name, BucketOpFn op, char *value, int index)   // 0x00736c60 (DS 0x007930a0)
{
    char full[64];
    std::sprintf(full, "%s_t%d", name, StatsTeamNumber(game, index));
    return StatsBucketString(game, full, op, value);
}

int StatsPlayerBucketInt(S_StatsGame *game, const char *name, BucketOpFn op, int value, int index)   // 0x00736cb0 (DS 0x00793110)
{
    char full[64];
    std::sprintf(full, "%s_%d", name, StatsPlayerNumber(game, index));
    return StatsBucketInt(game, full, op, value);
}

double StatsPlayerBucketDouble(S_StatsGame *game, const char *name, BucketOpFn op, double value, int index)   // 0x00736d00 (DS 0x00793180)
{
    char full[64];
    std::sprintf(full, "%s_%d", name, StatsPlayerNumber(game, index));
    return StatsBucketDouble(game, full, op, value);
}

char *StatsPlayerBucketString(S_StatsGame *game, const char *name, BucketOpFn op, char *value, int index)   // 0x00736d50 (DS 0x007931f0)
{
    char full[64];
    std::sprintf(full, "%s_%d", name, StatsPlayerNumber(game, index));
    return StatsBucketString(game, full, op, value);
}

// ---- gutil.c: the SDK's random numbers (Park and Miller's minimal standard) and the machine's unique id ------------------------

namespace {

int32_t s_randomNumber = 1;                             // 0x008455f0

int32_t NextLongRand(int32_t seed)                      // 0x007381f0 (DS 0x00794cb0)
{
    uint32_t lo = 16807 * (static_cast<uint32_t>(seed) & 0xFFFF);
    const uint32_t hi = 16807 * (static_cast<uint32_t>(seed) >> 16);
    lo += (hi & 0x7FFF) << 16;
    if (lo > 0x7FFFFFFF) {
        lo &= 0x7FFFFFFF;
        ++lo;
    }
    lo += hi >> 15;
    if (lo > 0x7FFFFFFF) {
        lo &= 0x7FFFFFFF;
        ++lo;
    }
    return static_cast<int32_t>(lo);
}

int32_t LongRand()                                      // 0x007381d0 (DS 0x00794c80)
{
    s_randomNumber = NextLongRand(s_randomNumber);
    return s_randomNumber;
}

}  // namespace

void RandomSeed(uint32_t seed)                       // 0x00738190 (DS 0x00794c20)
{
    s_randomNumber = seed ? static_cast<int32_t>(seed & 0x7FFFFFFF) : 1;
}

// low .. high - 1 (the SDK's check for an empty range is not in this build).
int RandomInt(int low, int high)                     // 0x007381b0 (DS 0x00794c50)
{
    return LongRand() % (high - low) + low;
}

namespace {

// A new id: 19 letters and digits at random, '-' at 4, 9 and 14.
void GenerateUniqueId(char *id)                         // 0x007380f0 (DS 0x00794b50)
{
    const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    LARGE_INTEGER counter;
    const uint32_t counterSeed = QueryPerformanceCounter(&counter) ? counter.HighPart ^ counter.LowPart : 0;
    RandomSeed(static_cast<uint32_t>(std::time(nullptr)) ^ static_cast<uint32_t>(std::clock()) ^ GetTickCount() ^ counterSeed);
    for (int i = 0; i < 19; ++i)
        id[i] = (i == 4 || i == 9 || i == 14) ? '-' : chars[RandomInt(0, 62)];
    id[19] = 0;
}

char s_uniqueId[0x104];                                 // 0x00982bb8

}  // namespace

// The id kept in HKCU\Software\GameSpy\GameSpy 3D\Registration "Crypt" (a new one written when it is missing or not 19 long),
// without its dashes: 16 characters.
const char *GetUniqueId()                               // 0x00737fd0 (DS 0x007949e0)
{
    static const char KEY[] = "Software\\GameSpy\\GameSpy 3D\\Registration";
    HKEY key;
    DWORD type = REG_SZ;
    DWORD size = sizeof(s_uniqueId);
    DWORD disposition;
    const bool create = RegOpenKeyExA(HKEY_CURRENT_USER, KEY, 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS;
    if (RegQueryValueExA(key, "Crypt", nullptr, &type, reinterpret_cast<BYTE *>(s_uniqueId), &size) != ERROR_SUCCESS ||
        std::strlen(s_uniqueId) != 19) {
        GenerateUniqueId(s_uniqueId);
        if (create)
            RegCreateKeyExA(HKEY_CURRENT_USER, KEY, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition);
        RegSetValueExA(key, "Crypt", 0, REG_SZ, reinterpret_cast<const BYTE *>(s_uniqueId),
                       static_cast<DWORD>(std::strlen(s_uniqueId) + 1));
    }
    RegCloseKey(key);
    std::memmove(s_uniqueId + 4, s_uniqueId + 5, 4);
    std::memmove(s_uniqueId + 8, s_uniqueId + 10, 4);
    std::memmove(s_uniqueId + 12, s_uniqueId + 15, 4);
    s_uniqueId[16] = 0;
    return s_uniqueId;
}
