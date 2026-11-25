// Net part 13a: the GameSpy SDK's utilities (net/gamespy_util.cpp: the dynamic array, the hash table, MD5, goastrdup,
// the socket helpers, the buckets, the persistent storage's replies, the stats' buckets, the random numbers) against
// the original executable's. MinGW; build_gsutil.bat; runs gsutil_diff.exe ("gsutil_diff: N checks, 0 failures"). The
// port's file is included here (its state and helpers are its own TU's), so the mutation runner rebuilds this file
// (MUT_WRAPPER=gsutil_diff.cpp). (GetUniqueId, which writes the registry, is not called: nothing in the game does.)
//
// The original's SDK is __fastcall, its compare callbacks __cdecl: each side is given its own callbacks, logging
// alike. Each round makes the same structures on both sides (an array of 4 to 24-byte elements, a hash table, a
// bucket set as the default, the persist requests, a stats game) with the same contents, and runs up to 12 calls;
// after each the result, the logs (the callbacks', the allocations', the system's) and the structures are compared.
// HD2_GSUTIL_ROUNDS: rounds (default 40,000; HD2_GSUTIL_QUICK: 10,000); HD2_GSUTIL_SEED.
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

// ---- the logs (both sides) ----
int g_side;
bool g_faking;
std::vector<std::string> g_log[2];
void Log(const std::string &s)
{
    if (g_faking)
        g_log[g_side].push_back(s);
}
std::string N(long long v) { return std::to_string(v); }
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}
std::string Bytes(const void *data, int size)
{
    if (!data)
        return "null";
    std::string s = N(size) + ":";
    const auto *b = static_cast<const uint8_t *>(data);
    for (int i = 0; i < size && i < 200; ++i) {
        char t[4];
        std::snprintf(t, sizeof t, "%02x", b[i]);
        s += t;
    }
    return s;
}
std::string Str(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }

// ---- the system calls the port's file makes (its side; the original's through its imports) ----
int g_selectResult;
extern "C" int WSAAPI FakeSelect(int count, fd_set *readable, fd_set *writable, fd_set *errors, const timeval *wait)
{
    Log("select " + N(count) + " " + N(readable ? static_cast<long long>(readable->fd_count) : -1) + "/" +
        N(readable && readable->fd_count ? static_cast<long long>(readable->fd_array[0]) : -1) + " " + N(writable != nullptr) + " " +
        N(errors != nullptr) + " " + (wait ? N(wait->tv_sec) + "." + N(wait->tv_usec) : "null"));
    return g_selectResult;
}
extern "C" int WSAAPI FakeWSAStartup(WORD version, LPWSADATA)
{
    Log("WSAStartup " + Hex(version));
    return 0;
}
// The allocations (the port's: its file's malloc / realloc / free calls, renamed; the original's CRT's).
// (Fresh blocks hold garbage, as the original's heap's: 0xCD, alike on both sides.)
void *TestMalloc(size_t n)
{
    Log("malloc " + N(static_cast<long long>(n)));
    void *p = std::malloc(n ? n : 1);
    std::memset(p, 0xCD, n ? n : 1);
    return p;
}
void *TestRealloc(void *p, size_t n)
{
    Log("realloc " + N(p != nullptr) + " " + N(static_cast<long long>(n)));
    return std::realloc(p, n ? n : 1);
}
void TestFree(void *p)
{
    Log("free " + N(p != nullptr));
    std::free(p);
}
void TestQsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *))
{
    Log("qsort " + N(static_cast<long long>(count)) + " " + N(static_cast<long long>(size)));
    std::qsort(base, count, size, compare);
}

// ---- the port's file ----
namespace std {
inline void *TestMalloc(size_t n) { return ::TestMalloc(n); }
inline void *TestRealloc(void *p, size_t n) { return ::TestRealloc(p, n); }
inline void TestFree(void *p) { ::TestFree(p); }
}
#define malloc TestMalloc
#define realloc TestRealloc
#define free TestFree
#define qsort TestQsort
#define select FakeSelect
#define WSAStartup FakeWSAStartup
#include "net/gamespy_util.cpp"
#undef malloc
#undef realloc
#undef free
#undef qsort
#undef select
#undef WSAStartup

namespace {

// ---- the original's ----
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define FAST __attribute__((fastcall))
#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
const uint32_t O_BUCKET_SET = 0x0098c1a0, O_PERSIST = 0x00982bb4, O_STATS = 0x00982b98, O_RANDOM = 0x008455f0;
const uint32_t O_BUCKET_INT = 0x00982108, O_BUCKET_DOUBLE = 0x00982110;
void *OMalloc(size_t n) { return TestMalloc(n); }
void *ORealloc(void *p, size_t n) { return TestRealloc(p, n); }
void OFree(void *p) { TestFree(p); }

// ---- the callbacks (each side's convention) ----
int __cdecl CompareInt(const void *a, const void *b)
{
    const int x = *static_cast<const int *>(a), y = *static_cast<const int *>(b);
    Log("compare " + N(x) + " " + N(y));
    return x < y ? -1 : x > y ? 1 : 0;
}
int __cdecl CompareName(const void *a, const void *b)
{
    const char *x = *static_cast<char *const *>(a), *y = *static_cast<char *const *>(b);
    Log("compare " + Str(x) + " " + Str(y));
    return std::strcmp(x, y);
}
int g_elementSize;
void PFreeElement(void *e) { Log("free element " + Bytes(e, g_elementSize)); }
void FAST OFreeElement(void *e) { Log("free element " + Bytes(e, g_elementSize)); }
int Hash(const void *e, int buckets)
{
    const int v = *static_cast<const int *>(e);
    Log("hash " + N(v) + " " + N(buckets));
    return ((v % buckets) + buckets) % buckets;
}
int PHash(const void *e, int buckets) { return Hash(e, buckets); }
int FAST OHash(const void *e, int buckets) { return Hash(e, buckets); }
int HashName(const void *e, int buckets)
{
    const char *name = *static_cast<char *const *>(e);
    Log("hash " + Str(name) + " " + N(buckets));
    unsigned h = 0;
    for (const char *c = name; *c; ++c)
        h = h * 31 + static_cast<unsigned char>(*c);
    return static_cast<int>(h % static_cast<unsigned>(buckets));
}
int PHashName(const void *e, int buckets) { return HashName(e, buckets); }
int FAST OHashName(const void *e, int buckets) { return HashName(e, buckets); }
void PMap(void *e, void *data) { Log("map " + Bytes(e, g_elementSize) + " " + N(*static_cast<int *>(data))); }
void FAST OMap(void *e, void *data) { Log("map " + Bytes(e, g_elementSize) + " " + N(*static_cast<int *>(data))); }
int Map2(void *e, void *data)
{
    Log("map2 " + Bytes(e, g_elementSize) + " " + N(*static_cast<int *>(data)));
    return *static_cast<int *>(e) == *static_cast<int *>(data) ? 0 : 1;
}
int PMap2(void *e, void *data) { return Map2(e, data); }
int FAST OMap2(void *e, void *data) { return Map2(e, data); }
// The persist callbacks (the original's: __fastcall, the request's local id and key first).
std::string Text(const char *t) { return t ? Str(t) : "null"; }
void PAuth(int localId, int key, int result, const char *error, void *instance)
{
    Log("auth " + N(localId) + " " + N(key) + " " + N(result) + " " + Text(error) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance))));
}
void FAST OAuth(int localId, int key, int result, const char *error, void *instance) { PAuth(localId, key, result, error, instance); }
void PGet(int localId, int key, int profile, int dataType, int result, const char *data, int size, void *instance)
{
    Log("get " + N(localId) + " " + N(key) + " " + N(profile) + " " + N(dataType) + " " + N(result) + " " + Text(data) + " " + N(size) + " " +
        Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance))));
}
void FAST OGet(int localId, int key, int profile, int dataType, int result, const char *data, int size, void *instance)
{
    PGet(localId, key, profile, dataType, result, data, size, instance);
}
void PSet(int localId, int key, int profile, int dataType, int result, void *instance)
{
    Log("set " + N(localId) + " " + N(key) + " " + N(profile) + " " + N(dataType) + " " + N(result) + " " +
        Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance))));
}
void FAST OSet(int localId, int key, int profile, int dataType, int result, void *instance) { PSet(localId, key, profile, dataType, result, instance); }
void PSave(int localId, int key, int result, void *instance)
{
    Log("save " + N(localId) + " " + N(key) + " " + N(result) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance))));
}
void FAST OSave(int localId, int key, int result, void *instance) { PSave(localId, key, result, instance); }

// ---- checks ----
int checks, failures;
int g_round;
std::string g_line, g_trace;
void Fail(const std::string &what)
{
    ++failures;
    if (failures <= 40)
        std::printf("round %d, %s: %s\n   (ops:%s)\n", g_round, g_line.c_str(), what.c_str(), g_trace.c_str());
}
void Check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok)
        Fail(what);
}
std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }

void CompareLogs()
{
    if (g_log[0] == g_log[1]) {
        ++checks;
        return;
    }
    std::string s = "the calls differ:";
    const size_t n = std::max(g_log[0].size(), g_log[1].size());
    if (std::getenv("HD2_GSUTIL_VERBOSE"))   // (every call of both)
        for (size_t i = 0; i < n; ++i)
            std::printf("  %-50s | %s\n", (i < g_log[1].size() ? g_log[1][i] : "-").substr(0, 50).c_str(), (i < g_log[0].size() ? g_log[0][i] : "-").substr(0, 50).c_str());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < g_log[1].size() ? g_log[1][i] : "-", y = i < g_log[0].size() ? g_log[0][i] : "-";
        if (x != y) {
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 400) + "\n     port: " + y.substr(0, 400);
            break;
        }
    }
    Check(false, s);
}

// ---- the structures (each side's) ----
S_DArray *g_array[2];
S_HashTable *g_table[2];
S_BucketSet g_sets[2];
S_StatsGame g_stats[2];
bool g_haveArray, g_haveTable, g_bucketsLive;
std::string ArrayText(const S_DArray *a, bool withFree = true)
{
    if (!a)
        return "null";
    std::string s = N(a->count) + "/" + N(a->capacity) + "/" + N(a->elementSize) + "/" + N(a->growBy) + "/" + N(withFree && a->freeElement != nullptr) + " ";
    return s + Bytes(a->list, a->count * a->elementSize);
}
// The persist requests (each side's callbacks: whether there is one).
std::string PersistText(const S_DArray *a)
{
    if (!a)
        return "null";
    std::string s = N(a->count) + "/" + N(a->capacity) + " ";
    for (int i = 0; i < a->count; ++i) {
        const auto *r = reinterpret_cast<const S_PersistRequest *>(a->list) + i;
        s += Bytes(r, 0x18) + "," + N(r->callback != nullptr) + " ";
    }
    return s;
}
// A bucket's data handed back (by the bucket's type; null).
std::string DataText(const void *data, const S_Bucket *b)
{
    if (!data)
        return "null";
    if (!b || b->type == BUCKET_INT)
        return N(*static_cast<const int *>(data));
    if (b->type == BUCKET_DOUBLE)
        return Bytes(data, 8);
    return Str(static_cast<const char *>(data));
}
std::string TableText(const S_HashTable *t)
{
    if (!t)
        return "null";
    std::string s = N(t->bucketCount) + ":";
    for (int i = 0; i < t->bucketCount; ++i)
        s += ArrayText(t->buckets[i]) + ";";
    return s;
}
std::string BucketText(const S_Bucket *b)
{
    std::string s = Str(b->name) + "," + N(b->type) + "," + N(b->players) + ",";
    if (b->type == BUCKET_STRING)
        return s + Text(b->value.asString);
    if (b->type == BUCKET_DOUBLE)
        return s + Bytes(&b->value.asDouble, 8);
    return s + N(b->value.asInt);
}
std::string SetText(const S_BucketSet &set)
{
    if (!set.buckets)
        return "null";
    std::string s;
    for (int i = 0; i < set.buckets->bucketCount; ++i) {
        const S_DArray *a = set.buckets->buckets[i];
        s += N(a->count) + "[";
        for (int k = 0; k < a->count; ++k)
            s += BucketText(reinterpret_cast<const S_Bucket *>(a->list + k * a->elementSize)) + " ";
        s += "]";
    }
    return s;
}
void CompareState()
{
    std::string s;
    if (g_haveArray && ArrayText(g_array[0]) != ArrayText(g_array[1]))
        s += "the array " + ArrayText(g_array[1]).substr(0, 300) + " / " + ArrayText(g_array[0]).substr(0, 300) + "; ";
    if (g_haveTable && TableText(g_table[0]) != TableText(g_table[1]))
        s += "the table " + TableText(g_table[1]).substr(0, 300) + " / " + TableText(g_table[0]).substr(0, 300) + "; ";
    if (g_bucketsLive && SetText(g_sets[0]) != SetText(g_sets[1]))
        s += "the buckets " + SetText(g_sets[1]).substr(0, 300) + " / " + SetText(g_sets[0]).substr(0, 300) + "; ";
    const std::string pp = PersistText(g_persistRequests), op = PersistText(G<S_DArray *>(O_PERSIST));
    if (pp != op)
        s += "the persist requests " + op.substr(0, 300) + " / " + pp.substr(0, 300) + "; ";
    if (s_randomNumber != G<int32_t>(O_RANDOM))
        s += "the random number " + N(G<int32_t>(O_RANDOM)) + " / " + N(s_randomNumber) + "; ";
    if (s_bucketInt != G<int>(O_BUCKET_INT) || std::memcmp(&s_bucketDouble, reinterpret_cast<void *>(static_cast<uintptr_t>(O_BUCKET_DOUBLE)), 8) != 0)
        s += "the bucket temporaries; ";
    Check(s.empty(), s);
}

template <class A, class B> void Run(const std::string &what, A port, B orig)
{
    g_line = what;
    g_trace += " " + what;
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    const long long x = port();
    g_side = 1;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareState();
}
// A result that is a pointer into each side's structure: its offset there, or a string.
long long Offset(const void *p, const void *base) { return p ? static_cast<long long>(static_cast<const char *>(p) - static_cast<const char *>(base)) : -1; }

// ---- the world ----
std::vector<uint8_t> RandomElement(int size)
{
    std::vector<uint8_t> e(static_cast<size_t>(size));
    for (uint8_t &b : e)
        b = static_cast<uint8_t>(I(0, 255));
    const int key = I(0, 20);
    std::memcpy(e.data(), &key, 4);
    return e;
}
void FreeStructures()
{
    g_faking = false;
    for (int s = 0; s < 2; ++s) {
        if (g_array[s]) {
            std::free(g_array[s]->list);
            std::free(g_array[s]);
            g_array[s] = nullptr;
        }
        if (g_table[s]) {
            for (int i = 0; i < g_table[s]->bucketCount; ++i) {
                std::free(g_table[s]->buckets[i]->list);
                std::free(g_table[s]->buckets[i]);
            }
            std::free(g_table[s]->buckets);
            std::free(g_table[s]);
            g_table[s] = nullptr;
        }
        g_sets[s].buckets = nullptr;   // (leaked: the buckets' strings)
    }
}
// The same array on both sides (made by the port's ArrayNew, the original's by hand alike).
S_DArray *CopyArray(const S_DArray *a, ArrayElementFreeFn freeElement)
{
    auto *c = static_cast<S_DArray *>(std::malloc(sizeof(S_DArray)));
    *c = *a;
    c->freeElement = freeElement;
    c->list = static_cast<char *>(std::malloc(static_cast<size_t>(a->capacity * a->elementSize) + 1));
    std::memcpy(c->list, a->list, static_cast<size_t>(a->capacity * a->elementSize));
    return c;
}
S_DArray *MakeArray(int size, int initial, ArrayElementFreeFn freeElement, int count)
{
    auto *a = static_cast<S_DArray *>(std::malloc(sizeof(S_DArray)));
    a->count = 0;
    a->capacity = initial ? initial : 8;
    a->elementSize = size;
    a->growBy = a->capacity;
    a->freeElement = freeElement;
    a->list = static_cast<char *>(std::calloc(static_cast<size_t>(a->capacity * size) + 1, 1));
    for (int i = 0; i < count; ++i) {
        if (a->count == a->capacity) {
            a->capacity += a->growBy;
            a->list = static_cast<char *>(std::realloc(a->list, static_cast<size_t>(a->capacity * size) + 1));
        }
        const std::vector<uint8_t> e = RandomElement(size);
        std::memcpy(a->list + a->count * size, e.data(), static_cast<size_t>(size));
        ++a->count;
    }
    return a;
}
// (A long string: the text of the set grows past its first block.)
const char g_longText[] = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
                          "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
                          "0123456789012345678901234567890123456789";
// A bucket set's table (each side's hash and the SDK's own free) with buckets of every kind.
void MakeBuckets()
{
    const int buckets = I(1, 5);
    for (int s = 0; s < 2; ++s) {
        auto *t = static_cast<S_HashTable *>(std::malloc(sizeof(S_HashTable)));
        t->bucketCount = buckets;
        t->buckets = static_cast<S_DArray **>(std::malloc(sizeof(S_DArray *) * static_cast<size_t>(buckets)));
        t->hash = s ? reinterpret_cast<TableHashFn>(&OHashName) : &PHashName;
        t->compare = CompareName;
        t->freeElement = s ? reinterpret_cast<ArrayElementFreeFn>(static_cast<uintptr_t>(0x00733ac0)) : reinterpret_cast<ArrayElementFreeFn>(&BucketElementFree);
        g_sets[s].buckets = t;
    }
    std::vector<std::vector<S_Bucket>> chains(static_cast<size_t>(buckets));
    static const char *names[] = {"kills", "score", "name", "ratio", "kills_t0", "score_1", "kills_1", "x"};
    for (int k = I(0, 6); k > 0; --k) {
        const char *name = names[I(0, 7)];
        unsigned h = 0;
        for (const char *c = name; *c; ++c)
            h = h * 31 + static_cast<unsigned char>(*c);
        auto &chain = chains[h % static_cast<unsigned>(buckets)];
        bool dup = false;
        for (const S_Bucket &b : chain)
            dup = dup || std::strcmp(b.name, name) == 0;
        if (dup)
            continue;
        S_Bucket b{};
        b.name = const_cast<char *>(name);
        b.type = I(0, 2);
        b.players = I(0, 4);
        if (b.type == BUCKET_INT)
            b.value.asInt = I(-50, 50);
        else if (b.type == BUCKET_DOUBLE)
            b.value.asDouble = I(-50, 50) / 4.0;
        else
            // (A long one of any length to 240: past it, the original's text overruns its block.)
            b.value.asString = const_cast<char *>(P(0.2) ? g_longText + I(0, static_cast<int>(sizeof g_longText) - 81) : P(0.5) ? "abc" : "x\\y");
        chain.push_back(b);
    }
    std::vector<int> initial(static_cast<size_t>(buckets));
    for (int &n : initial)
        n = I(0, 3);
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < buckets; ++i) {
            S_DArray *a = MakeArray(sizeof(S_Bucket), initial[static_cast<size_t>(i)], g_sets[s].buckets->freeElement, 0);
            for (const S_Bucket &b : chains[static_cast<size_t>(i)]) {
                if (a->count == a->capacity) {
                    a->capacity += a->growBy;
                    a->list = static_cast<char *>(std::realloc(a->list, static_cast<size_t>(a->capacity) * sizeof(S_Bucket) + 1));
                }
                S_Bucket c = b;
                c.name = static_cast<char *>(std::malloc(std::strlen(b.name) + 1));
                std::strcpy(c.name, b.name);
                if (b.type == BUCKET_STRING) {
                    c.value.asString = static_cast<char *>(std::malloc(std::strlen(b.value.asString) + 1));
                    std::strcpy(c.value.asString, b.value.asString);
                }
                std::memcpy(a->list + a->count * static_cast<int>(sizeof(S_Bucket)), &c, sizeof c);
                ++a->count;
            }
            g_sets[s].buckets->buckets[i] = a;
        }
    g_bucketSet = P(0.8) ? &g_sets[0] : nullptr;
    G<S_BucketSet *>(O_BUCKET_SET) = g_bucketSet ? &g_sets[1] : nullptr;
}

void SetWorld()
{
    FreeStructures();
    g_selectResult = P(0.5) ? I(0, 1) : I(-1, 2);
    // An array (the same elements; each side's free callback or none).
    g_elementSize = std::vector<int>{4, 8, 12, 24}[static_cast<size_t>(I(0, 3))];
    const bool frees = P(0.7);
    g_array[0] = MakeArray(g_elementSize, I(0, 4), frees ? &PFreeElement : nullptr, I(0, 12));
    g_array[1] = CopyArray(g_array[0], frees ? reinterpret_cast<ArrayElementFreeFn>(&OFreeElement) : nullptr);
    g_haveArray = true;
    // A table of ints (its elements 8 bytes: the key first).
    const int buckets = I(1, 6);
    for (int s = 0; s < 2; ++s) {
        auto *t = static_cast<S_HashTable *>(std::malloc(sizeof(S_HashTable)));
        t->bucketCount = buckets;
        t->buckets = static_cast<S_DArray **>(std::malloc(sizeof(S_DArray *) * static_cast<size_t>(buckets)));
        t->hash = s ? reinterpret_cast<TableHashFn>(&OHash) : &PHash;
        t->compare = CompareInt;
        t->freeElement = frees ? (s ? reinterpret_cast<ArrayElementFreeFn>(&OFreeElement) : &PFreeElement) : nullptr;
        g_table[s] = t;
    }
    for (int i = 0; i < buckets; ++i) {
        const int saved = g_elementSize;
        g_elementSize = 8;
        S_DArray *a = MakeArray(8, I(0, 3), nullptr, 0);
        for (int k = I(0, 3); k > 0; --k) {
            std::vector<uint8_t> e = RandomElement(8);
            const int key = I(0, 6) * buckets + i;   // (in its bucket; unique there)
            bool dup = false;
            for (int j = 0; j < a->count; ++j)
                dup = dup || *reinterpret_cast<int *>(a->list + j * 8) == key;
            if (dup)
                continue;
            std::memcpy(e.data(), &key, 4);
            if (a->count == a->capacity) {
                a->capacity += a->growBy;
                a->list = static_cast<char *>(std::realloc(a->list, static_cast<size_t>(a->capacity) * 8 + 1));
            }
            std::memcpy(a->list + a->count * 8, e.data(), 8);
            ++a->count;
        }
        g_elementSize = saved;
        g_table[0]->buckets[i] = a;
        g_table[1]->buckets[i] = CopyArray(a, g_table[1]->freeElement);
        a->freeElement = g_table[0]->freeElement;
    }
    g_haveTable = true;
    // The buckets, the persist requests, the stats game, the random number.
    MakeBuckets();
    g_bucketsLive = true;
    {
        S_DArray *requests = MakeArray(sizeof(S_PersistRequest), 4, nullptr, 0);
        for (int k = I(0, 5); k > 0; --k) {
            S_PersistRequest r{};
            r.kind = I(0, 3);
            r.localId = I(0, 3);
            r.key = I(0, 3);
            r.profile = I(0, 99);
            r.dataType = I(0, 3);
            r.instance = reinterpret_cast<void *>(static_cast<uintptr_t>(I(0, 0xFFFF)));
            r.callback = P(0.9) ? reinterpret_cast<void *>(1) : nullptr;   // (each side's below)
            if (requests->count == requests->capacity) {
                requests->capacity += requests->growBy;
                requests->list = static_cast<char *>(std::realloc(requests->list, static_cast<size_t>(requests->capacity) * sizeof r + 1));
            }
            std::memcpy(requests->list + requests->count * static_cast<int>(sizeof r), &r, sizeof r);
            ++requests->count;
        }
        S_DArray *orig = CopyArray(requests, nullptr);
        for (int i = 0; i < requests->count; ++i) {
            auto *p = reinterpret_cast<S_PersistRequest *>(requests->list) + i;
            auto *o = reinterpret_cast<S_PersistRequest *>(orig->list) + i;
            if (!p->callback)
                continue;
            static void *const ports[4] = {reinterpret_cast<void *>(&PAuth), reinterpret_cast<void *>(&PGet), reinterpret_cast<void *>(&PSet),
                                           reinterpret_cast<void *>(&PSave)};
            static void *const origs[4] = {reinterpret_cast<void *>(&OAuth), reinterpret_cast<void *>(&OGet), reinterpret_cast<void *>(&OSet),
                                           reinterpret_cast<void *>(&OSave)};
            p->callback = ports[p->kind];
            o->callback = origs[o->kind];
        }
        const bool none = P(0.1);
        g_persistRequests = none ? nullptr : requests;
        G<S_DArray *>(O_PERSIST) = none ? nullptr : orig;
    }
    for (int s = 0; s < 2; ++s) {
        S_StatsGame &g = g_stats[s];
        std::memset(&g, 0, sizeof g);
        g.buckets = &g_sets[s];
    }
    {
        S_DArray *teams = MakeArray(4, 4, nullptr, 0), *players = MakeArray(4, 8, nullptr, 0);
        for (int i = 0; i < 3; ++i) {
            const int v = I(0, 9);
            std::memcpy(teams->list + 4 * i, &v, 4);
        }
        teams->count = 3;
        for (int i = 0; i < 6; ++i) {
            const int v = I(0, 9);
            std::memcpy(players->list + 4 * i, &v, 4);
        }
        players->count = 6;
        g_stats[0].teams = teams;
        g_stats[0].players = players;
        g_stats[1].teams = CopyArray(teams, nullptr);
        g_stats[1].players = CopyArray(players, nullptr);
    }
    const bool stats = P(0.8);
    g_statsGame = stats ? &g_stats[0] : nullptr;
    G<S_StatsGame *>(O_STATS) = stats ? &g_stats[1] : nullptr;
    s_randomNumber = P(0.1) ? 1 : I(1, 0x7FFFFFFF);
    G<int32_t>(O_RANDOM) = s_randomNumber;
    s_bucketInt = I(-9, 9);
    G<int>(O_BUCKET_INT) = s_bucketInt;
    s_bucketDouble = I(-9, 9) / 2.0;
    std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(O_BUCKET_DOUBLE)), &s_bucketDouble, 8);
}

// A reply of the persistent storage ("\key\value" pairs).
std::string Reply(const char *first)
{
    std::string s = "\\" + std::string(first) + "\\" + N(I(-1, 3));
    static const char *keys[] = {"lid", "pid", "errmsg", "length", "data"};
    for (int k = I(0, 5); k > 0; --k) {
        const char *key = keys[I(0, 4)];
        s += "\\" + std::string(key) + "\\";
        s += std::strcmp(key, "errmsg") == 0 ? "bad" : std::strcmp(key, "data") == 0 ? "payload" : N(I(0, 3));
    }
    return s;
}

void Call()
{
    const int op = I(0, 46);
    S_DArray *pa = g_array[0], *oa = g_array[1];
    const int count = pa->count;
    const std::vector<uint8_t> element = RandomElement(g_elementSize);
    switch (op) {
    case 0: {
        // A new array (freed at once: its fields compared through the logs).
        const int size = I(1, 24), initial = I(0, 9);
        std::string pt, ot;
        Run("ArrayNew " + N(size) + " " + N(initial),
            [&] {
                S_DArray *a = ArrayNew(size, initial, &PFreeElement);
                pt = ArrayText(a, true).substr(0, 20);
                std::free(a->list);
                std::free(a);
                return 0;
            },
            [&] {
                auto *a = O_CALL(0x00732e70, S_DArray *(FAST *)(int, int, void *))(size, initial, reinterpret_cast<void *>(&OFreeElement));
                ot = ArrayText(a, true).substr(0, 20);
                std::free(a->list);
                std::free(a);
                return 0;
            });
        Check(pt == ot, "the new array " + ot + " / " + pt);
        break;
    }
    case 45: {
        // A copy of the array freed (its elements' free callbacks).
        S_DArray *pc = CopyArray(pa, pa->freeElement), *oc = CopyArray(oa, oa->freeElement);
        Run("ArrayFree", [&] { ArrayFree(pc); return 0; }, [&] { O_CALL(0x00732ed0, void(FAST *)(S_DArray *))(oc); return 0; });
        break;
    }
    case 46: {
        // A bucket freed (a copy: its name and string the test's blocks), the sets freed.
        if (P(0.5)) {
            S_Bucket b[2];
            for (S_Bucket &x : b) {
                x.name = static_cast<char *>(std::malloc(4));
                std::strcpy(x.name, "nm");
                x.type = I(0, 2);
                x.players = 0;
                x.value.asString = nullptr;
            }
            if (b[0].type == BUCKET_STRING && P(0.7))
                for (S_Bucket &x : b) {
                    x.value.asString = static_cast<char *>(std::malloc(4));
                    std::strcpy(x.value.asString, "sv");
                }
            b[1].type = b[0].type;
            Run("BucketElementFree", [&] { BucketElementFree(&b[0]); return 0; }, [&] { O_CALL(0x00733ac0, void(FAST *)(S_Bucket *))(&b[1]); return 0; });
        } else if (g_bucketsLive) {
            // (The set itself is freed: a heap copy of each side's.)
            S_BucketSet *sets[2];
            for (int k = 0; k < 2; ++k) {
                sets[k] = static_cast<S_BucketSet *>(std::malloc(sizeof(S_BucketSet)));
                *sets[k] = g_sets[k];
            }
            // (The sets are gone after it: freed blocks; not compared, no more calls on them this round.)
            g_bucketsLive = false;
            g_sets[0].buckets = g_sets[1].buckets = nullptr;
            g_bucketSet = nullptr;
            G<S_BucketSet *>(O_BUCKET_SET) = nullptr;
            g_statsGame = nullptr;
            G<S_StatsGame *>(O_STATS) = nullptr;
            Run("FreeBucketSet", [&] { FreeBucketSet(sets[0]); return 0; }, [&] { O_CALL(0x00733400, void(FAST *)(S_BucketSet *))(sets[1]); return 0; });
        }
        break;
    }
    case 1:
        Run("ArrayLength", [&] { return static_cast<long long>(ArrayLength(pa)); },
            [&] { return static_cast<long long>(O_CALL(0x00732f80, int(FAST *)(const S_DArray *))(oa)); });
        break;
    case 2: {
        const int index = I(-1, count + 1);
        Run("ArrayNth " + N(index), [&] { return Offset(ArrayNth(pa, index), pa->list); },
            [&] { return Offset(O_CALL(0x00732f90, void *(FAST *)(const S_DArray *, int))(oa, index), oa->list); });
        break;
    }
    case 3:
    case 4: {
        const bool none = P(0.05);
        Run("ArrayAppend " + N(none), [&] { ArrayAppend(none ? nullptr : pa, element.data()); return 0; },
            [&] { O_CALL(0x00732fa0, void(FAST *)(S_DArray *, const void *))(none ? nullptr : oa, element.data()); return 0; });
        break;
    }
    case 5:
    case 6: {
        const int index = I(0, count);
        Run("ArrayInsertAt " + N(index), [&] { ArrayInsertAt(pa, element.data(), index); return 0; },
            [&] { O_CALL(0x00732fb0, void(FAST *)(S_DArray *, const void *, int))(oa, element.data(), index); return 0; });
        break;
    }
    case 7:
        if (count) {
            const int index = I(0, count - 1);
            Run("ArrayRemoveAt " + N(index), [&] { ArrayRemoveAt(pa, index); return 0; },
                [&] { O_CALL(0x007330b0, void(FAST *)(S_DArray *, int))(oa, index); return 0; });
        }
        break;
    case 8:
        if (count) {
            const int index = I(0, count - 1);
            Run("ArrayDeleteAt " + N(index), [&] { ArrayDeleteAt(pa, index); return 0; },
                [&] { O_CALL(0x007330f0, void(FAST *)(S_DArray *, int))(oa, index); return 0; });
        }
        break;
    case 9:
        if (count) {
            const int index = I(0, count - 1);
            Run("ArrayReplaceAt " + N(index), [&] { ArrayReplaceAt(pa, element.data(), index); return 0; },
                [&] { O_CALL(0x00733110, void(FAST *)(S_DArray *, const void *, int))(oa, element.data(), index); return 0; });
        }
        break;
    case 10:
    case 11: {
        // A search (sorted: on the array sorted first, alike).
        const bool sorted = op == 11;
        if (sorted) {
            g_faking = false;
            std::qsort(pa->list, static_cast<size_t>(pa->count), static_cast<size_t>(pa->elementSize), CompareInt);
            std::memcpy(oa->list, pa->list, static_cast<size_t>(pa->count * pa->elementSize));
        }
        const int key = I(-1, 21), from = count ? I(0, count - 1) : 0;
        const bool none = P(0.02);
        Run("ArraySearch " + N(key) + " " + N(from) + " " + N(sorted), [&] { return static_cast<long long>(ArraySearch(none ? nullptr : pa, &key, CompareInt, from, sorted)); },
            [&] { return static_cast<long long>(O_CALL(0x00733160, int(FAST *)(const S_DArray *, const void *, void *, int, int))(none ? nullptr : oa, &key, reinterpret_cast<void *>(&CompareInt), from, sorted)); });
        break;
    }
    case 12:
        Run("ArrayClear", [&] { ArrayClear(pa); return 0; }, [&] { O_CALL(0x007332d0, void(FAST *)(S_DArray *))(oa); return 0; });
        break;
    case 13:
        Run("ArraySort", [&] { ArraySort(pa, CompareInt); return 0; },
            [&] { O_CALL(0x00733140, void(FAST *)(S_DArray *, void *))(oa, reinterpret_cast<void *>(&CompareInt)); return 0; });
        break;
    case 14: {
        // Sorted first (alike), then an insertion where it belongs.
        g_faking = false;
        std::qsort(pa->list, static_cast<size_t>(pa->count), static_cast<size_t>(pa->elementSize), CompareInt);
        std::memcpy(oa->list, pa->list, static_cast<size_t>(pa->count * pa->elementSize));
        Run("ArrayInsertSorted", [&] { ArrayInsertSorted(pa, element.data(), CompareInt); return 0; },
            [&] { O_CALL(0x00733070, void(FAST *)(S_DArray *, const void *, void *))(oa, element.data(), reinterpret_cast<void *>(&CompareInt)); return 0; });
        break;
    }
    case 15:
    case 16: {
        int data = I(0, 20);
        const bool backward = op == 16;
        Run(std::string("ArrayMap") + (backward ? "Backward" : "Forward"),
            [&] { (backward ? ArrayMapBackward : ArrayMapForward)(pa, PMap, &data); return 0; },
            [&] { O_CALL(backward ? 0x00733220 : 0x007331f0, void(FAST *)(S_DArray *, void *, void *))(oa, reinterpret_cast<void *>(&OMap), &data); return 0; });
        break;
    }
    case 17:
    case 18: {
        int data = I(0, 20);
        const bool backward = op == 18;
        Run(std::string("ArrayMap2 ") + (backward ? "Backward" : "Forward"),
            [&] { return Offset((backward ? ArrayMapBackward2 : ArrayMapForward2)(pa, PMap2, &data), pa->list); },
            [&] { return Offset(O_CALL(backward ? 0x00733290 : 0x00733250, void *(FAST *)(S_DArray *, void *, void *))(oa, reinterpret_cast<void *>(&OMap2), &data), oa->list); });
        break;
    }
    case 19: {
        // A new list given (its own block each side).
        const int n = I(0, 6), cap = n + I(0, 3);
        char *pl = static_cast<char *>(std::calloc(static_cast<size_t>(cap * g_elementSize) + 1, 1)), *ol = static_cast<char *>(std::calloc(static_cast<size_t>(cap * g_elementSize) + 1, 1));
        for (int i = 0; i < n * g_elementSize; ++i)
            pl[i] = ol[i] = static_cast<char>(I(0, 255));
        Run("ArraySetDataPtr " + N(n), [&] { ArraySetDataPtr(pa, pl, n, cap); return 0; },
            [&] { O_CALL(0x00732f30, void(FAST *)(S_DArray *, void *, int, int))(oa, ol, n, cap); return 0; });
        break;
    }
    case 20: {
        const int saved = g_elementSize;
        g_elementSize = 8;   // (the table's elements' size, for the free callback's log)
        const int key = I(-1, 40);
        int entry[2] = {key, I(0, 999)};
        Run("TableEnter " + N(key), [&] { TableEnter(g_table[0], entry); return 0; },
            [&] { O_CALL(0x00736eb0, void(FAST *)(S_HashTable *, const void *))(g_table[1], entry); return 0; });
        g_elementSize = saved;
        break;
    }
    case 21: {
        const int saved = g_elementSize;
        g_elementSize = 8;
        int key[2] = {I(-1, 40), 0};
        Run("TableRemove " + N(key[0]), [&] { return static_cast<long long>(TableRemove(g_table[0], key)); },
            [&] { return static_cast<long long>(O_CALL(0x00736f00, uint8_t(FAST *)(S_HashTable *, const void *))(g_table[1], key)); });
        g_elementSize = saved;
        break;
    }
    case 22: {
        int key[2] = {I(-1, 40), 0};
        Run("TableLookup " + N(key[0]),
            [&] {
                void *p = TableLookup(g_table[0], key);
                return p ? static_cast<long long>(*static_cast<int *>(p)) * 1000 + static_cast<int *>(p)[1] % 1000 : -1LL;
            },
            [&] {
                void *p = O_CALL(0x00736f50, void *(FAST *)(S_HashTable *, const void *))(g_table[1], key);
                return p ? static_cast<long long>(*static_cast<int *>(p)) * 1000 + static_cast<int *>(p)[1] % 1000 : -1LL;
            });
        break;
    }
    case 23:
        Run("TableCount", [&] { return static_cast<long long>(TableCount(g_table[0])); },
            [&] { return static_cast<long long>(O_CALL(0x00736e80, int(FAST *)(const S_HashTable *))(g_table[1])); });
        break;
    case 24:
    case 25: {
        int data = I(0, 40);
        const int saved = g_elementSize;
        g_elementSize = 8;
        const bool safe = op == 25;
        Run(std::string("TableMap") + (safe ? "Safe" : ""), [&] { (safe ? TableMapSafe : TableMap)(g_table[0], PMap, &data); return 0; },
            [&] { O_CALL(safe ? 0x00736fd0 : 0x00736f90, void(FAST *)(S_HashTable *, void *, void *))(g_table[1], reinterpret_cast<void *>(&OMap), &data); return 0; });
        g_elementSize = saved;
        break;
    }
    case 26:
    case 27: {
        int data = I(0, 40);
        const int saved = g_elementSize;
        g_elementSize = 8;
        const bool safe = op == 27;
        long long base[2] = {0, 0};
        Run(std::string("TableMap2") + (safe ? "Safe" : ""),
            [&] {
                void *p = (safe ? TableMapSafe2 : TableMap2)(g_table[0], PMap2, &data);
                return p ? static_cast<long long>(*static_cast<int *>(p)) : -1LL;
            },
            [&] {
                void *p = O_CALL(safe ? 0x00737050 : 0x00737010, void *(FAST *)(S_HashTable *, void *, void *))(g_table[1], reinterpret_cast<void *>(&OMap2), &data);
                return p ? static_cast<long long>(*static_cast<int *>(p)) : -1LL;
            });
        (void)base;
        g_elementSize = saved;
        break;
    }
    case 28: {
        const int saved = g_elementSize;
        g_elementSize = 8;
        Run("TableClear", [&] { TableClear(g_table[0]); return 0; }, [&] { O_CALL(0x00737090, void(FAST *)(S_HashTable *))(g_table[1]); return 0; });
        g_elementSize = saved;
        break;
    }
    case 29: {
        // A table made and freed at once (its buckets' arrays' fields through the logs).
        const int buckets = I(1, 6), chain = I(0, 4), size = I(1, 16);
        std::string pt, ot;
        Run("TableNew2", [&] {
                S_HashTable *t = TableNew2(size, buckets, chain, &PHash, CompareInt, &PFreeElement);
                pt = TableText(t) + N(t->hash == &PHash) + N(t->compare == CompareInt) + N(t->freeElement == &PFreeElement);
                TableFree(t);
                return 0;
            },
            [&] {
                auto *t = O_CALL(0x00736dd0, S_HashTable *(FAST *)(int, int, int, void *, void *, void *))(size, buckets, chain, reinterpret_cast<void *>(&OHash),
                                                                                                          reinterpret_cast<void *>(&CompareInt), reinterpret_cast<void *>(&OFreeElement));
                ot = TableText(t) + N(t->hash == reinterpret_cast<TableHashFn>(&OHash)) + N(t->compare == CompareInt) +
                     N(t->freeElement == reinterpret_cast<ArrayElementFreeFn>(&OFreeElement));
                O_CALL(0x00736e40, void(FAST *)(S_HashTable *))(t);
                return 0;
            });
        Check(pt == ot, "the new table");
        break;
    }
    case 30: {
        std::vector<uint8_t> data(static_cast<size_t>(P(0.2) ? I(0, 3) : P(0.5) ? I(50, 70) : I(0, 300)));
        for (uint8_t &b : data)
            b = static_cast<uint8_t>(I(0, 255));
        char hex[2][40];
        std::memset(hex, 'x', sizeof hex);
        Run("MD5Digest " + N(static_cast<long long>(data.size())), [&] { MD5Digest(data.data(), static_cast<unsigned>(data.size()), hex[0]); return 0; },
            [&] { O_CALL(0x00737c50, void(FAST *)(const void *, unsigned, char *))(data.data(), static_cast<unsigned>(data.size()), hex[1]); return 0; });
        Check(std::memcmp(hex[0], hex[1], 40) == 0, "the digest");
        break;
    }
    case 31: {
        const bool none = P(0.1);
        const std::string text = "copy" + N(I(0, 999));
        std::string pt, ot;
        Run("goastrdup", [&] {
                char *c = goastrdup(none ? nullptr : text.c_str());
                pt = Text(c);
                std::free(c);
                return 0;
            },
            [&] {
                char *c = O_CALL(0x00737ce0, char *(FAST *)(const char *))(none ? nullptr : text.c_str());
                ot = Text(c);
                std::free(c);
                return 0;
            });
        Check(pt == ot, "the copy");
        break;
    }
    case 32: {
        const SOCKET s = static_cast<SOCKET>(I(1, 999));
        if (P(0.5))
            Run("CanReceiveOnSocket", [&] { return static_cast<long long>(CanReceiveOnSocket(s)); },
                [&] { return static_cast<long long>(O_CALL(0x00737e60, uint8_t(FAST *)(SOCKET))(s)); });
        else
            Run("SocketStartUp", [&] { SocketStartUp(); return 0; }, [&] { O_CALL(0x00737cb0, void(FAST *)())(); return 0; });
        break;
    }
    case 33: {
        // The buckets (the default set or a given one).
        if (!g_bucketsLive)
            break;
        static const char *names[] = {"kills", "score", "name", "ratio", "new", "x"};
        const char *name = names[I(0, 5)];
        const bool given = P(0.5) || !g_bucketSet;
        S_BucketSet *ps = given ? &g_sets[0] : nullptr, *os = given ? &g_sets[1] : nullptr;
        const int kind = I(0, 8);
        int iv = I(-9, 9);
        if (kind == 4 && iv == 0)
            iv = 1;   // (an int division by zero: both would fault)
        double dv = I(-9, 9) / 2.0;
        if (dv == 0)
            dv = 1.5;
        const char *sv = P(0.5) ? "z\\z" : "q";
        S_Bucket *pb = BucketFind(ps, name);
        const void *value = !pb ? static_cast<const void *>(&iv) : pb->type == BUCKET_INT ? static_cast<const void *>(&iv) : pb->type == BUCKET_DOUBLE ? static_cast<const void *>(&dv) : static_cast<const void *>(sv);
        static const uint32_t origs[] = {0x007334e0, 0x00733520, 0x007335b0, 0x00733620, 0x00733690, 0x007337a0};
        static void *(*const ports[])(S_BucketSet *, const char *, const void *) = {BucketSet, BucketAdd, BucketSub, BucketMult, BucketDiv, BucketAvg};
        if (kind < 6) {
            std::string pt, ot;
            Run("Bucket op " + N(kind) + " " + name, [&] { pt = DataText(ports[kind](ps, name, value), pb); return 0; },
                [&] { ot = DataText(O_CALL(origs[kind], void *(FAST *)(S_BucketSet *, const char *, const void *))(os, name, value), pb); return 0; });
            Check(pt == ot, "the data " + ot + " / " + pt);
        } else if (kind == 6) {
            const int type = pb ? pb->type : I(0, 2);
            const void *v = type == BUCKET_INT ? static_cast<const void *>(&iv) : type == BUCKET_DOUBLE ? static_cast<const void *>(&dv) : static_cast<const void *>(sv);
            if (pb)
                break;   // (a new name only: TableEnter would replace the old one's without freeing its name)
            Run("BucketNew " + std::string(name), [&] { return static_cast<long long>(BucketNew(ps, name, type, v) != nullptr); },
                [&] { return static_cast<long long>(O_CALL(0x00733470, void *(FAST *)(S_BucketSet *, const char *, int, const void *))(os, name, type, v) != nullptr); });
        } else if (kind == 7) {
            if (pb && pb->type != BUCKET_STRING)
                break;
            Run("BucketConcat " + std::string(name), [&] { return static_cast<long long>(BucketConcat(ps, name, sv) != nullptr); },
                [&] { return static_cast<long long>(O_CALL(0x00733700, void *(FAST *)(S_BucketSet *, const char *, const char *))(os, name, sv) != nullptr); });
        } else {
            std::string pt, ot;
            Run("BucketSetToString", [&] {
                    char *t = BucketSetToString(ps);
                    pt = Text(t);
                    std::free(t);
                    return 0;
                },
                [&] {
                    char *t = O_CALL(0x00733420, char *(FAST *)(S_BucketSet *))(os);
                    ot = Text(t);
                    std::free(t);
                    return 0;
                });
            Check(pt == ot, "the text " + ot + " / " + pt);
        }
        break;
    }
    case 34: {
        // The small bucket helpers.
        const int which = I(0, 3);
        if (which == 0) {
            const int v = I(-99, 99);
            Run("BucketInt", [&] { return static_cast<long long>(*static_cast<int *>(BucketInt(v))); },
                [&] { return static_cast<long long>(*static_cast<int *>(O_CALL(0x00733840, void *(FAST *)(int))(v))); });
        } else if (which == 1) {
            const double v = I(-99, 99) / 8.0;
            Run("BucketDouble", [&] { return static_cast<long long>(*static_cast<double *>(BucketDouble(v)) * 8); },
                [&] { return static_cast<long long>(*static_cast<double *>(O_CALL(0x00733850, void *(FAST *)(double))(v)) * 8); });
        } else {
            char text[2][16];
            std::snprintf(text[0], 16, "%s", P(0.5) ? "a\\b\\c" : "abc");
            std::strcpy(text[1], text[0]);
            Run("BucketFixSlashes", [&] { BucketFixSlashes(text[0]); return 0; },
                [&] { O_CALL(0x007339f0, char *(FAST *)(char *))(text[1]); return 0; });
            Check(std::strcmp(text[0], text[1]) == 0, "the slashes");
        }
        break;
    }
    case 35:
    case 36:
    case 37: {
        // A persist reply (auth, get, set).
        static const char *firsts[] = {"pauthr", "getpdr", "setpdr"};
        const int k = op - 35;
        const std::string reply = Reply(firsts[k]);
        static const uint32_t origs[] = {0x00736680, 0x007367e0, 0x00736890};
        static void (*const ports[])(const char *) = {PersistAuthReply, PersistGetReply, PersistSetReply};
        Run(std::string("Persist ") + firsts[k] + " " + reply, [&] { ports[k](reply.c_str()); return 0; },
            [&] { O_CALL(origs[k], void(FAST *)(const char *))(reply.c_str()); return 0; });
        break;
    }
    case 38: {
        const std::string reply = Reply("x");
        static const char *keys[] = {"x", "lid", "pid", "nope", "data"};
        const char *key = keys[I(0, 4)];
        std::string pt, ot;
        const bool safe = P(0.5);
        Run(std::string("ValueForKey ") + key, [&] { pt = Text(safe ? ValueForKeySafe(reply.c_str(), key) : ValueForKey(reply.c_str(), key)); return 0; },
            [&] { ot = Text(O_CALL(safe ? 0x00736700 : 0x00736080, const char *(FAST *)(const char *, const char *))(reply.c_str(), key)); return 0; });
        Check(pt == ot, "the value " + ot + " / " + pt);
        break;
    }
    case 39: {
        const int kind = I(0, 4), localId = I(0, 3), key = I(0, 3);
        Run("FindPersistRequest", [&] { return static_cast<long long>(FindPersistRequest(kind, localId, key)); },
            [&] { return static_cast<long long>(O_CALL(0x00736710, int(FAST *)(int, int, int))(kind, localId, key)); });
        break;
    }
    case 40: {
        const int index = I(-1, 5), result = I(0, 2), size = I(0, 9);
        const char *text = P(0.5) ? "t" : nullptr;
        if (!g_persistRequests)
            break;
        Run("FinishPersistRequest " + N(index), [&] { FinishPersistRequest(index, result, text, size); return 0; },
            [&] { O_CALL(0x00736900, void(FAST *)(int, int, const char *, int))(index, result, text, size); return 0; });
        break;
    }
    case 41:
    case 42: {
        if (!g_bucketsLive)
            break;
        // A stats game's buckets (the game given or the default; an op of each side's).
        const bool given = P(0.5);
        S_StatsGame *pg = given ? &g_stats[0] : nullptr, *og = given ? &g_stats[1] : nullptr;
        static const char *names[] = {"kills", "score", "ratio", "new"};
        const char *name = names[I(0, 3)];
        const int index = I(0, 2), opk = I(0, 1);
        static const uint32_t origOps[] = {0x007334e0, 0x00733520};
        static BucketOpFn const portOps[] = {&BucketSet, &BucketAdd};
        // (The buckets of the default set: the game's; the bucket's kind as the call's, or a new name.)
        const int kind = I(0, 2), scope = I(0, 2);
        const int saved = g_elementSize;
        g_elementSize = 4;
        if (P(0.3)) {
            Run("StatsTeamNumber " + N(index), [&] { return static_cast<long long>(StatsTeamNumber(pg, index)); },
                [&] { return static_cast<long long>(O_CALL(0x00736a90, int(FAST *)(S_StatsGame *, int))(og, index)); });
            Run("StatsPlayerNumber " + N(index), [&] { return static_cast<long long>(StatsPlayerNumber(pg, index)); },
                [&] { return static_cast<long long>(O_CALL(0x00736ab0, int(FAST *)(S_StatsGame *, int))(og, index)); });
            g_elementSize = saved;
            break;
        }
        // (The full name the call looks up: a bucket of another kind there would be read as this one.)
        char full[64];
        const S_StatsGame *game = pg ? pg : g_statsGame;
        if (scope == 0)
            std::snprintf(full, sizeof full, "%s", name);
        else if (game)
            std::snprintf(full, sizeof full, scope == 1 ? "%s_t%d" : "%s_%d", name,
                          *reinterpret_cast<const int *>((scope == 1 ? game->teams : game->players)->list + 4 * index));
        else
            std::snprintf(full, sizeof full, scope == 1 ? "%s_t%d" : "%s_%d", name, index);
        if (game) {
            S_Bucket *pb = BucketFind(game->buckets, full);
            if (pb && pb->type != kind) {
                g_elementSize = saved;
                break;
            }
        }
        void *const op = opk ? reinterpret_cast<void *>(portOps[opk]) : reinterpret_cast<void *>(portOps[0]);
        const uint32_t oop = origOps[opk];
        if (kind == 0) {
            const int v = I(-9, 9);
            static const uint32_t origs[] = {0x00736ad0, 0x00736bc0, 0x00736cb0};
            Run("StatsBucketInt " + N(scope) + " " + full,
                [&] {
                    return static_cast<long long>(scope == 0 ? StatsBucketInt(pg, name, reinterpret_cast<BucketOpFn>(op), v)
                                                  : scope == 1 ? StatsTeamBucketInt(pg, name, reinterpret_cast<BucketOpFn>(op), v, index)
                                                               : StatsPlayerBucketInt(pg, name, reinterpret_cast<BucketOpFn>(op), v, index));
                },
                [&] {
                    // (The original's plain one takes a third stack word it does not read.)
                    return static_cast<long long>(O_CALL(origs[scope], int(FAST *)(S_StatsGame *, const char *, uint32_t, int, int))(og, name, oop, v, index));
                });
        } else if (kind == 1) {
            const double v = I(-9, 9) / 4.0;
            static const uint32_t origs[] = {0x00736b20, 0x00736c10, 0x00736d00};
            Run("StatsBucketDouble " + N(scope) + " " + full,
                [&] {
                    const double r = scope == 0 ? StatsBucketDouble(pg, name, reinterpret_cast<BucketOpFn>(op), v)
                                     : scope == 1 ? StatsTeamBucketDouble(pg, name, reinterpret_cast<BucketOpFn>(op), v, index)
                                                  : StatsPlayerBucketDouble(pg, name, reinterpret_cast<BucketOpFn>(op), v, index);
                    long long bits;
                    std::memcpy(&bits, &r, 8);
                    return bits;
                },
                [&] {
                    const double r = O_CALL(origs[scope], double(FAST *)(S_StatsGame *, const char *, uint32_t, double, int))(og, name, oop, v, index);
                    long long bits;
                    std::memcpy(&bits, &r, 8);
                    return bits;
                });
        } else {
            char v[2][8] = {"ab", "ab"};
            static const uint32_t origs[] = {0x00736b70, 0x00736c60, 0x00736d50};
            std::string pt, ot;
            Run("StatsBucketString " + N(scope) + " " + full,
                [&] {
                    pt = Text(scope == 0 ? StatsBucketString(pg, name, reinterpret_cast<BucketOpFn>(op), v[0])
                              : scope == 1 ? StatsTeamBucketString(pg, name, reinterpret_cast<BucketOpFn>(op), v[0], index)
                                           : StatsPlayerBucketString(pg, name, reinterpret_cast<BucketOpFn>(op), v[0], index));
                    return 0;
                },
                [&] {
                    ot = Text(O_CALL(origs[scope], char *(FAST *)(S_StatsGame *, const char *, uint32_t, char *, int))(og, name, oop, v[1], index));
                    return 0;
                });
            Check(pt == ot, "the string " + ot + " / " + pt);
        }
        g_elementSize = saved;
        break;
    }
    default: {
        const int which = I(0, 2);
        if (which == 0) {
            const uint32_t seed = P(0.1) ? 0 : static_cast<uint32_t>(I(0, 0x7FFFFFFF)) * 2u + static_cast<uint32_t>(I(0, 1));
            Run("RandomSeed", [&] { RandomSeed(seed); return 0; }, [&] { O_CALL(0x00738190, void(FAST *)(uint32_t))(seed); return 0; });
        } else if (which == 1) {
            const int low = I(-5, 5), high = low + I(1, 100);
            Run("RandomInt " + N(low) + " " + N(high), [&] { return static_cast<long long>(RandomInt(low, high)); },
                [&] { return static_cast<long long>(O_CALL(0x007381b0, int(FAST *)(int, int))(low, high)); });
        } else {
            // (Some whose step carries twice.)
            // (And two whose steps meet 0x7FFFFFFF exactly.)
            static const int32_t carries[] = {0x36b27c52, 0x74ed3054, 0x4e2b597c, 0x6aa33165, 0x5fb379c7, 0x75d33fc6, 0x33cba9e9, 0x7fffffff};
            const int32_t seed = P(0.2) ? carries[I(0, 7)] : I(0, 0x7FFFFFFF);
            Run("NextLongRand", [&] { return static_cast<long long>(NextLongRand(seed)); },
                [&] { return static_cast<long long>(O_CALL(0x007381f0, int32_t(FAST *)(int32_t))(seed)); });
        }
        break;
    }
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side\n   (ops:%s)\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, g_round,
                g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
    std::fflush(stdout);
    ExitProcess(3);
}

int RedirectImport(const void *real, const void *to)
{
    if (!real)
        return 0;
    const auto *base = reinterpret_cast<const uint8_t *>(0x400000);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int n = 0;
    for (auto *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); imp->Name; ++imp)
        for (auto *t = reinterpret_cast<IMAGE_THUNK_DATA32 *>(const_cast<uint8_t *>(base) + imp->FirstThunk); t->u1.Function; ++t)
            if (t->u1.Function == static_cast<DWORD>(reinterpret_cast<uintptr_t>(real))) {
                t->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(to));
                ++n;
            }
    return n;
}

}   // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("gsutil_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    // (The original's CRT's float conversions set up, as its start would: its sprintf's %f.)
    O_CALL(0x007e599c, void(__cdecl *)())();
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(OMalloc));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(OFree));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e8b79, reinterpret_cast<const void *>(TestQsort));
    for (const char *dll : {"ws2_32.dll", "wsock32.dll"}) {
        HMODULE m = LoadLibraryA(dll);
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "select")), reinterpret_cast<const void *>(&FakeSelect));
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "WSAStartup")), reinterpret_cast<const void *>(&FakeWSAStartup));
    }

    int rounds = std::getenv("HD2_GSUTIL_QUICK") ? 10000 : 40000;
    if (const char *r = std::getenv("HD2_GSUTIL_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_GSUTIL_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        g_trace.clear();
        SetWorld();
        for (int k = I(1, 12); k > 0; --k)
            Call();
    }
    std::printf("gsutil_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
