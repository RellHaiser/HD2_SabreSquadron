// The GameSpy SDK's internals (part 10f): its dynamic array and hash table, MD5, the query & reporting record the CD key
// client shares its socket through. The game's build of the SDK is __fastcall throughout; the port's is plain C++.
#pragma once

#include <winsock2.h>

#include <cstdint>

#include "net/gamespy.h"

// ---- darray.c ----------------------------------------------------------------------------------------------------------

using ArrayElementFreeFn = void (*)(void *element);
using ArrayCompareFn = int (*)(const void *a, const void *b);

struct S_DArray {                                       // 0x18 bytes
    int count;
    int capacity;
    int elementSize;                                    // +0x08
    int growBy;                                         // +0x0C
    ArrayElementFreeFn freeElement;                     // +0x10
    char *list;                                         // +0x14
};

S_DArray *ArrayNew(int elementSize, int initial, ArrayElementFreeFn freeElement);   // 0x00732e70
void ArrayFree(S_DArray *array);                        // 0x00732ed0
int ArrayLength(const S_DArray *array);                 // 0x00732f80
void *ArrayNth(const S_DArray *array, int index);       // 0x00732f90
void ArrayAppend(S_DArray *array, const void *element); // 0x00732fa0
void ArrayInsertAt(S_DArray *array, const void *element, int index);   // 0x00732fb0
void ArrayRemoveAt(S_DArray *array, int index);         // 0x007330b0 (not freed)
void ArrayDeleteAt(S_DArray *array, int index);         // 0x007330f0
void ArrayReplaceAt(S_DArray *array, const void *element, int index);   // 0x00733110
// The index of `key` from `from` on (`sorted`: by a binary search); -1 for none.
int ArraySearch(const S_DArray *array, const void *key, ArrayCompareFn compare, int from, bool sorted);   // 0x00733160
void ArrayClear(S_DArray *array);                       // 0x007332d0

// ---- hashtable.c -------------------------------------------------------------------------------------------------------

using TableHashFn = int (*)(const void *element, int buckets);

struct S_HashTable {                                    // 0x14 bytes
    S_DArray **buckets;
    int bucketCount;                                    // +0x04
    ArrayElementFreeFn freeElement;                     // +0x08
    TableHashFn hash;                                   // +0x0C
    ArrayCompareFn compare;                             // +0x10
};

S_HashTable *TableNew2(int elementSize, int buckets, int chainSize, TableHashFn hash, ArrayCompareFn compare,
                       ArrayElementFreeFn freeElement);  // 0x00736dd0
void TableFree(S_HashTable *table);                     // 0x00736e40
int TableCount(const S_HashTable *table);               // 0x00736e80
void TableEnter(S_HashTable *table, const void *element);   // 0x00736eb0
bool TableRemove(S_HashTable *table, const void *element);  // 0x00736f00
void *TableLookup(S_HashTable *table, const void *element); // 0x00736f50

// ---- md5c.c, nonport.c, the strings' copy ------------------------------------------------------------------------------

// The digest of `size` bytes as 32 hex digits and a terminator.
void MD5Digest(const void *data, unsigned size, char *hex);   // 0x00737c50
char *goastrdup(const char *text);                      // 0x00737ce0
// The socket has something to read (now).
bool CanReceiveOnSocket(SOCKET socket);                 // 0x00737e60

// ---- qr2: the record (qr2_implementation_s) ----------------------------------------------------------------------------

constexpr int QR2_PACKET_SIZE = 0x800;
struct S_Qr2Buffer {                                    // qr2_buffer_t
    char data[QR2_PACKET_SIZE];
    int length;                                         // +0x800
};
struct S_Qr2KeyBuffer {                                 // qr2_keybuffer_t
    uint8_t keys[0x100];
    int count;                                          // +0x100
};

using Qr2PacketFn = void (*)(char *packet, int size, sockaddr *from);

struct S_Qr2Record {                                    // 0x108 bytes
    SOCKET socket;                                      // +0x00 (INVALID_SOCKET when closed)
    char gameName[0x40];                                // +0x04
    char secretKey[0x40];                               // +0x44
    uint8_t instanceKey[4];                             // +0x84
    Qr2ServerKeyCallback serverKey;                     // +0x88
    Qr2PlayerKeyCallback playerKey;                     // +0x8C
    Qr2TeamKeyCallback teamKey;                         // +0x90
    Qr2KeyListCallback keyList;                         // +0x94
    Qr2CountCallback count;                             // +0x98
    Qr2AddErrorCallback addError;                       // +0x9C
    void *natCallback;                                  // +0xA0 (none in the game)
    void *clientMessage;                                // +0xA4 (none in the game)
    uint32_t lastHeartbeat;                             // +0xA8 ms
    uint32_t lastKeepAlive;                             // +0xAC ms
    int listedState;                                    // +0xB0 1: not yet; -1 listed; 2..3: challenge retries
    int isPublic;                                       // +0xB4
    int queryPort;                                      // +0xB8
    int readSocket;                                     // +0xBC the SDK reads the socket
    int natNegotiate;                                   // +0xC0
    sockaddr_in masterAddress;                          // +0xC4
    Qr2PacketFn cdKeyProcess;                           // +0xD4 ';' packets: the CD key client's
    int clientMessageKeys[10];                          // +0xD8 the last messages' keys
    int nextMessageKey;                                 // +0x100
    void *userData;                                     // +0x104
};
extern S_Qr2Record g_qr2DefaultRecord;                  // 0x00845670
extern const char *g_qr2KeyNames[0x100];                // 0x00845828

// ---- the server browser ------------------------------------------------------------------------------------------------

struct S_SbServer {                                     // SBServer, 0x24 bytes
    uint32_t publicIp;
    u_short publicPort;                                 // +0x04 (network order, as the address)
    uint32_t privateIp;                                 // +0x08
    u_short privatePort;                                // +0x0C
    uint32_t icmpIp;                                    // +0x10
    uint8_t state;                                      // +0x14 what is known, what is asked
    uint8_t flags;                                      // +0x15 the master server's
    S_HashTable *keyValues;                             // +0x18
    uint32_t updateTime;                                // +0x1C the query's time; the ping after its answer
    S_SbServer *next;                                   // +0x20 in the engine's lists, the dead list
};

struct S_SbQueryList {                                  // 12 bytes
    S_SbServer *first;
    S_SbServer *last;
    int count;
};

struct S_SbQueryEngine;
using SbQueryCallback = void (*)(S_SbQueryEngine *engine, int reason, S_SbServer *server, void *instance);
struct S_SbQueryEngine {                                // 0x48 bytes
    int queryVersion;                                   // 1: the binary queries, else the old text
    int maxUpdates;                                     // +0x04 queries at a time
    S_SbQueryList querying;                             // +0x08
    S_SbQueryList pending;                              // +0x14
    SOCKET socket;                                      // +0x20
    uint32_t publicIp;                                  // +0x24 ours
    uint8_t keys[0x14];                                 // +0x28 the keys of a basic query
    int keyCount;                                       // +0x3C
    SbQueryCallback callback;                           // +0x40
    void *instance;                                     // +0x44
};

struct S_SbCipher {                                     // GOACryptState, 0x105 bytes
    uint8_t cards[0x100];
    uint8_t rotor, ratchet, avalanche, lastPlain, lastCipher;
};

struct S_SbServerList;
using SbListCallback = void (*)(S_SbServerList *list, int reason, S_SbServer *server, void *instance);
struct S_SbServerList {                                 // 0x5C0 bytes
    int state;                                          // 0 LAN, 1 disconnected, 2 connected, 3 reading the main list
    S_DArray *servers;                                  // +0x004 of S_SbServer *
    S_DArray *keyList;                                  // +0x008 name, type
    char queryForGame[0x20];                            // +0x00C
    char queryFromGame[0x20];                           // +0x02C
    char queryFromKey[0x20];                            // +0x04C
    char challenge[8];                                  // +0x06C
    char *inBuffer;                                     // +0x074
    int inBufferSize;                                   // +0x078
    const char *popularValues[0xFF];                    // +0x07C
    int popularCount;                                   // +0x478
    int expected;                                       // +0x47C the part's count, -1: not read yet
    SbListCallback callback;                            // +0x480
    void *instance;                                     // +0x484
    uint32_t publicIp;                                  // +0x490 ours, as the master server saw it
    uint32_t sourceIp;                                  // +0x494
    u_short defaultPort;                                // +0x498
    const char *errorMessage;                           // +0x49C the master server's
    SOCKET socket;                                      // +0x4A0
    uint32_t lanStartTime;                              // +0x4A4
    int queryFromVersion;                               // +0x4A8
    S_SbCipher cipher;                                  // +0x4AC
    uint32_t options;                                   // +0x5B4
    int parseState;                                     // +0x5B8
    S_SbServer *deadList;                               // +0x5BC freed at the next think
};

struct S_ServerBrowser {                                // 0x620 bytes
    S_SbQueryEngine engine;
    S_SbServerList list;                                // +0x048
    int disconnectOnComplete;                           // +0x608
    int dontUpdate;                                     // +0x60C no queries by itself
    uint32_t triggerIp;                                 // +0x610 a trigger update's server, 0 when none is waited for
    ServerBrowserCallback callback;                     // +0x618
    void *instance;                                     // +0x61C
};

void SbFreeSharedStrings();                             // 0x0073a2d0
S_HashTable *SbSharedStrings();                         // 0x0073a240 the keys' and values' strings, counted (this thread's)
extern const char *g_sbMasterOverride;                  // 0x00982de4 the master server's name instead of the usual
