// GameSpy's server browser (part 10f-iii; sb_serverbrowsing.c, sb_queryengine.c, sb_serverlist.c, sb_server.c, sb_crypt.c):
// the master server's list over TCP (encrypted) and the servers' own values over UDP. The game asks for the list and gets
// the values of each by the query engine. See gamespy.h, gamespy_internal.h.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "net/gamespy_internal.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

#ifndef SB_TEST_PORT
#define SB_TEST_PORT 28910                              // (the parallel test runs' own)
#endif
constexpr u_short MASTER_PORT = SB_TEST_PORT;
constexpr uint32_t QUERY_TIMEOUT = 2500;                // ms
constexpr uint32_t LAN_TIMEOUT = 2000;                  // ms
constexpr int INBUFFER_SIZE = 0x1000;

enum {                                                  // S_SbServer::state
    SERVER_BASIC_KEYS = 1,
    SERVER_FULL_KEYS = 2,
    SERVER_PENDING_BASIC = 4,
    SERVER_PENDING_FULL = 8,
    SERVER_QUERY_FAILED = 0x10,
};
enum {                                                  // S_SbServer::flags, as the master server sends them
    FLAG_PRIVATE_IP = 2,
    FLAG_ICMP_IP = 8,
    FLAG_NONSTANDARD_PORT = 0x10,
    FLAG_NONSTANDARD_PRIVATE_PORT = 0x20,
    FLAG_HAS_KEYS = 0x40,
    FLAG_HAS_FULL_KEYS = 0x80,
};
enum {                                                  // the query engine's callback
    QUERY_SUCCESS,
    QUERY_FAILED,
    QUERY_ENGINE_IDLE,
};
enum {                                                  // the list's callback
    LIST_SERVER_ADDED,
    LIST_SERVER_UPDATED,
    LIST_SERVER_REMOVED,
    LIST_DONE,
    LIST_DISCONNECTED,
    LIST_ERROR_MESSAGE,
    LIST_PUBLIC_IP,
};
enum {                                                  // ServerBrowserCallback (see net_search.cpp)
    BROWSER_SERVER_ADDED,
    BROWSER_SERVER_UPDATED,
    BROWSER_UPDATE_FAILED,
    BROWSER_SERVER_REMOVED,
    BROWSER_UPDATES_DONE,
    BROWSER_ERROR_MESSAGE,
};
enum {                                                  // S_SbServerList::state
    LIST_STATE_LAN,                                     // (only the dead ServerBrowserLANUpdate sets it)
    LIST_STATE_DISCONNECTED,
    LIST_STATE_CONNECTED,
    LIST_STATE_MAIN_LIST,
};
enum {                                                  // S_SbServerList::parseState
    PARSE_CRYPT_HEADER,
    PARSE_FIXED_HEADER,
    PARSE_KEY_LIST,
    PARSE_POPULAR_VALUES,
    PARSE_SERVERS,
    PARSE_DONE,
};
enum {                                                  // the messages after the main list
    MESSAGE_KEY_LIST = 1,
    MESSAGE_SERVER,
    MESSAGE_ECHO,
    MESSAGE_REMOVE,
};
enum {                                                  // ServerBrowserUpdate's and the list's error codes
    SB_SOCKET_ERROR = 1,
    SB_DNS_ERROR,
    SB_CONNECT_ERROR,
    SB_DATA_ERROR,
    SB_MEMORY_ERROR,
    SB_PARAMETER_ERROR,
};
constexpr int OPTION_NO_SERVER_LIST = 2;
constexpr int OPTION_ALTERNATE_SOURCE_IP = 8;
constexpr int OPTION_LIMIT_RESULT_COUNT = 0x80;

struct S_ListKey {                                      // the key list's element
    const char *name;
    int type;                                           // 0 string, 1 byte, 2 short
};

uint32_t ReadU32(const void *at)
{
    uint32_t value;
    std::memcpy(&value, at, 4);
    return value;
}
u_short ReadU16(const void *at)
{
    u_short value;
    std::memcpy(&value, at, 2);
    return value;
}

// The string's size with its terminator; -1: none within `size`.
int StringSize(const char *text, int size)              // 0x0073b3b0
{
    for (int i = 0; i < size; ++i) {
        if (text[i] == 0)
            return i + 1;
    }
    return -1;
}

// ---- the keys' and values' shared strings ------------------------------------------------------------------------------

struct S_SharedString {                                 // 8 bytes
    char *text;
    int references;
};

int HashString(const char *text, int buckets)           // 0x0073a9d0
{
    uint32_t hash = 0;
    for (; *text; ++text)
        hash = static_cast<uint32_t>(tolower(*text)) + hash * 0x9CCF9319u;
    return static_cast<int>(hash % static_cast<uint32_t>(buckets));
}
// (Both tables' hash and compare, 0x0073a290 and 0x0073aac0, 0x0073a2a0 and 0x0073aad0: by the first field, a string.)
int HashFirstField(const void *element, int buckets)
{
    return HashString(*static_cast<char *const *>(element), buckets);
}
int CompareFirstField(const void *a, const void *b)
{
    return _stricmp(*static_cast<char *const *>(a), *static_cast<char *const *>(b));
}
void FreeSharedString(void *element)                    // 0x0073a2c0
{
    std::free(static_cast<S_SharedString *>(element)->text);
}

}  // namespace

S_HashTable *SbSharedStrings()                          // 0x0073a240
{
    static thread_local S_HashTable *table;
    if (!table)
        table = TableNew2(sizeof(S_SharedString), 500, 4, HashFirstField, CompareFirstField, FreeSharedString);
    return table;
}

namespace {

const char *AddSharedString(const char *text)           // 0x0073cec0
{
    S_SharedString key = {const_cast<char *>(text), 0};
    auto *found = static_cast<S_SharedString *>(TableLookup(SbSharedStrings(), &key));
    if (found) {
        ++found->references;
        return found->text;
    }
    key.text = goastrdup(text);
    key.references = 1;
    TableEnter(SbSharedStrings(), &key);
    return key.text;
}

void ReleaseSharedString(const char *text)              // 0x0073cf20
{
    S_SharedString key = {const_cast<char *>(text), 0};
    auto *found = static_cast<S_SharedString *>(TableLookup(SbSharedStrings(), &key));
    if (found && --found->references == 0)
        TableRemove(SbSharedStrings(), &key);
}

// ---- a server ----------------------------------------------------------------------------------------------------------

struct S_KeyValue {                                     // 8 bytes
    const char *key;
    const char *value;
};

void FreeKeyValue(void *element)                        // 0x0073aaa0
{
    const auto *pair = static_cast<S_KeyValue *>(element);
    ReleaseSharedString(pair->key);
    ReleaseSharedString(pair->value);
}

// (0x0073aa20; its unused list argument dropped.)
S_SbServer *NewServer(uint32_t ip, u_short port)
{
    auto *server = static_cast<S_SbServer *>(std::malloc(sizeof(S_SbServer)));
    if (!server)
        return nullptr;
    server->keyValues = TableNew2(sizeof(S_KeyValue), 8, 4, HashFirstField, CompareFirstField, FreeKeyValue);
    if (!server->keyValues) {
        std::free(server);
        return nullptr;
    }
    server->publicIp = ip;
    server->publicPort = port;
    server->state = 0;
    server->flags = 0;
    server->next = nullptr;
    server->updateTime = 0;
    server->icmpIp = 0;
    server->privateIp = 0;
    server->privatePort = 0;
    return server;
}

void FreeServer(S_SbServer *server)                     // 0x0073a310
{
    TableFree(server->keyValues);
    std::free(server);
}

void SetValue(S_SbServer *server, const char *key, const char *value)   // 0x0073a330
{
    S_KeyValue pair;
    pair.key = AddSharedString(key);
    pair.value = AddSharedString(value);
    TableEnter(server->keyValues, &pair);
}

void SetIntValue(S_SbServer *server, const char *key, int value)   // 0x0073a370
{
    char text[20];
    std::sprintf(text, "%d", value);
    SetValue(server, key, text);
}

// strtok's like, of the old queries' answers; an empty token is none (its separator gone).
char *NextToken(char *from, char separator)             // 0x0073a810
{
    static char *next;                                  // 0x00982dd8
    if (from)
        next = from;
    char *token = next;
    while (*next && *next != separator)
        ++next;
    if (next == token)
        token = nullptr;
    if (*next) {
        *next = 0;
        ++next;
    }
    return token;
}

bool IsValueKey(const char *key)                        // 0x0073a7a0
{
    return std::strcmp(key, "queryid") != 0 && std::strcmp(key, "final") != 0;
}

// An old query's answer: \key\value...
void ParseOldReply(S_SbServer *server, char *reply)     // 0x0073a740
{
    char *key = NextToken(reply + 1, '\\');
    while (key) {
        const char *value = NextToken(nullptr, '\\');
        if (!value)
            value = "";
        if (IsValueKey(key))
            SetValue(server, key, value);
        key = NextToken(nullptr, '\\');
    }
}

// A full answer: the server's keys and values, then the players' and the teams' tables (their columns' names, their rows).
void ParseFullReply(S_SbServer *server, const char *data, int size)   // 0x0073a860
{
    while (*data) {
        const int keySize = StringSize(data, size);
        if (keySize < 0)
            return;
        const char *value = data + keySize;
        const int valueSize = StringSize(value, size - keySize);
        if (valueSize < 0)
            return;
        size -= keySize + valueSize;
        SetValue(server, data, value);
        data = value + valueSize;
    }
    ++data;
    --size;
    for (int table = 0; table < 2; ++table) {
        if (size < 2)
            return;
        const u_short rows = ntohs(ReadU16(data));
        const char *const columns = data + 2;
        data = columns;
        size -= 2;
        int columnCount = 0;
        while (*data) {
            const int nameSize = StringSize(data, size);
            if (nameSize < 0 || nameSize > 100)
                return;
            data += nameSize;
            size -= nameSize;
            ++columnCount;
        }
        ++data;
        --size;
        for (int row = 0; row < rows; ++row) {
            const char *column = columns;
            for (int i = 0; i < columnCount; ++i) {
                const int valueSize = StringSize(data, size);
                if (valueSize < 0)
                    return;
                char key[0x80];
                std::sprintf(key, "%s%d", column, row);
                SetValue(server, key, data);
                data += valueSize;
                size -= valueSize;
                column += std::strlen(column) + 1;
            }
        }
    }
}

// ---- the query engine --------------------------------------------------------------------------------------------------

void AddLast(S_SbQueryList *list, S_SbServer *server)   // 0x00739ce0
{
    if (list->last)
        list->last->next = server;
    list->last = server;
    server->next = nullptr;
    if (!list->first)
        list->first = server;
    ++list->count;
}

void AddFirst(S_SbQueryList *list, S_SbServer *server)  // 0x00739d10
{
    server->next = list->first;
    list->first = server;
    if (!list->last)
        list->last = server;
    ++list->count;
}

// (The server's next stays: the engine goes on from it while the replies remove the servers.)
bool RemoveServer(S_SbQueryList *list, S_SbServer *server)   // 0x0073a070
{
    S_SbServer *previous = nullptr;
    S_SbServer *at = list->first;
    while (at != server) {
        if (!at)
            return false;
        previous = at;
        at = at->next;
    }
    if (previous)
        previous->next = at->next;
    if (list->first == at)
        list->first = at->next;
    if (list->last == at)
        list->last = previous;
    --list->count;
    return true;
}

S_SbServer *RemoveFirst(S_SbQueryList *list)            // 0x0073a190
{
    S_SbServer *first = list->first;
    if (first) {
        list->first = first->next;
        if (!list->first)
            list->last = nullptr;
        --list->count;
    }
    return first;
}

void SendQuery(S_SbQueryEngine *engine, S_SbServer *server)   // 0x00739d30
{
    AddLast(&engine->querying, server);
    const uint32_t now = GetTickCount();
    server->updateTime = now;
    char packet[0x100];
    int size;
    if (engine->queryVersion == 1) {
        packet[0] = static_cast<char>(0xFE);
        packet[1] = static_cast<char>(0xFD);
        packet[2] = 0;
        std::memcpy(packet + 3, &now, 4);
        if (server->state & SERVER_PENDING_BASIC) {
            packet[7] = static_cast<char>(engine->keyCount);
            std::memcpy(packet + 8, engine->keys, static_cast<size_t>(engine->keyCount));
            packet[8 + engine->keyCount] = 0;
            packet[9 + engine->keyCount] = 0;
            size = engine->keyCount + 10;
        } else {
            std::memset(packet + 7, 0xFF, 3);
            size = 10;
        }
    } else if (server->state & SERVER_PENDING_BASIC) {
        std::memcpy(packet, "\\basic\\\\info\\", 13);
        size = 13;
    } else {
        std::memcpy(packet, "\\status\\", 8);
        size = 8;
    }
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    // Behind the same address as ours: its private one.
    if (server->publicIp == engine->publicIp && (server->flags & FLAG_PRIVATE_IP)) {
        to.sin_addr.s_addr = server->privateIp;
        to.sin_port = server->privatePort;
    } else {
        to.sin_addr.s_addr = server->publicIp;
        to.sin_port = server->publicPort;
    }
    sendto(engine->socket, packet, size, 0, reinterpret_cast<const sockaddr *>(&to), sizeof to);
}

// `full`: all its values, else the engine's keys'.
void QueueQuery(S_SbQueryEngine *engine, S_SbServer *server, bool front, int mode)   // 0x00739c90
{
    server->state &= static_cast<uint8_t>(~(SERVER_PENDING_BASIC | SERVER_PENDING_FULL | SERVER_QUERY_FAILED));
    if (mode == 0)
        server->state |= SERVER_PENDING_BASIC;
    else if (mode == 1)
        server->state |= SERVER_PENDING_FULL;
    if (engine->querying.count < engine->maxUpdates)
        SendQuery(engine, server);
    else if (front)
        AddFirst(&engine->pending, server);
    else
        AddLast(&engine->pending, server);
}

void FinishQuery(S_SbQueryEngine *engine, S_SbServer *server)
{
    server->state &= static_cast<uint8_t>(~(SERVER_PENDING_BASIC | SERVER_PENDING_FULL));
    server->updateTime = GetTickCount() - server->updateTime;
    RemoveServer(&engine->querying, server);
    engine->callback(engine, QUERY_SUCCESS, server, engine->instance);
}

void HandleReply(S_SbQueryEngine *engine, S_SbServer *server, const char *reply, int size)   // 0x00739fa0
{
    if (*reply != 0)
        return;
    const char *data = reply + 5;
    size -= 5;
    if (server->state & SERVER_PENDING_BASIC) {
        for (int i = 0; i < engine->keyCount; ++i) {
            const int valueSize = StringSize(data, size);
            if (valueSize < 0)
                break;
            SetValue(server, g_qr2KeyNames[engine->keys[i]], data);
            data += valueSize;
            size -= valueSize;
        }
        server->state |= SERVER_BASIC_KEYS;
    } else {
        ParseFullReply(server, data, size);
        server->state |= SERVER_BASIC_KEYS | SERVER_FULL_KEYS;
    }
    FinishQuery(engine, server);
}

// (0x0073a0c0; its unused size argument dropped.)
void HandleOldReply(S_SbQueryEngine *engine, S_SbServer *server, char *reply)
{
    const bool final = std::strstr(reply, "\\final\\") != nullptr;
    ParseOldReply(server, reply);
    if (final) {
        server->state |= (server->state & SERVER_PENDING_BASIC) ? SERVER_BASIC_KEYS : SERVER_FULL_KEYS;
        FinishQuery(engine, server);
    }
}

void ReceiveReplies(S_SbQueryEngine *engine)            // 0x00739ee0
{
    char reply[0x834];
    sockaddr_in from;
    int fromSize = sizeof from;
    while (CanReceiveOnSocket(engine->socket)) {
        const int size = recvfrom(engine->socket, reply, 0x833, 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
        if (size == -1)
            return;
        reply[size] = 0;
        for (S_SbServer *server = engine->querying.first; server; server = server->next) {
            const uint32_t ip = from.sin_addr.s_addr;
            if ((server->publicIp == ip && server->publicPort == from.sin_port) ||
                (server->publicIp == engine->publicIp && (server->flags & FLAG_PRIVATE_IP) && server->privateIp == ip &&
                 server->privatePort == from.sin_port)) {
                if (engine->queryVersion == 1)
                    HandleReply(engine, server, reply, size);
                else
                    HandleOldReply(engine, server, reply);
            }
        }
    }
}

// (The original marks the time-out in the server's flags, not its state: the state's pending bits stay.)
void TimeOutQueries(S_SbQueryEngine *engine)            // 0x0073a130
{
    const uint32_t now = GetTickCount();
    S_SbQueryList *const list = &engine->querying;
    while (list->first && list->first->updateTime + QUERY_TIMEOUT < now) {
        list->first->flags |= SERVER_QUERY_FAILED;
        list->first->flags &= static_cast<uint8_t>(~(SERVER_PENDING_BASIC | SERVER_PENDING_FULL));
        engine->callback(engine, QUERY_FAILED, list->first, engine->instance);
        RemoveFirst(list);
    }
}

void SendPending(S_SbQueryEngine *engine)               // 0x0073a1b0
{
    while (engine->querying.count < engine->maxUpdates) {
        if (engine->pending.count < 1)
            return;
        SendQuery(engine, RemoveFirst(&engine->pending));
    }
}

void EngineThink(S_SbQueryEngine *engine)               // 0x00739ea0
{
    if (engine->querying.count == 0)
        return;
    ReceiveReplies(engine);
    TimeOutQueries(engine);
    if (engine->pending.count > 0)
        SendPending(engine);
    if (engine->querying.count == 0)
        engine->callback(engine, QUERY_ENGINE_IDLE, nullptr, engine->instance);
}

void EngineRemove(S_SbQueryEngine *engine, S_SbServer *server)   // 0x0073a210
{
    if (!RemoveServer(&engine->querying, server))
        RemoveServer(&engine->pending, server);
}

void EngineClear(S_SbQueryEngine *engine)               // 0x00739c40
{
    engine->pending = S_SbQueryList{};
    engine->querying = S_SbQueryList{};
}

void EngineInit(S_SbQueryEngine *engine, int maxUpdates, int queryVersion, SbQueryCallback callback, void *instance)   // 0x00739bd0
{
    SocketStartUp();
    engine->queryVersion = queryVersion;
    engine->maxUpdates = maxUpdates;
    engine->keyCount = 0;
    engine->callback = callback;
    engine->instance = instance;
    engine->publicIp = 0;
    engine->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    engine->pending = S_SbQueryList{};
    engine->querying = S_SbQueryList{};
}

void EngineAddKey(S_SbQueryEngine *engine, uint8_t key) // 0x0073a1f0
{
    if (engine->keyCount < 0x14)
        engine->keys[engine->keyCount++] = key;
}

// ---- the master server's cipher ----------------------------------------------------------------------------------------

void CipherInitNoKey(S_SbCipher *cipher)                // 0x00739770
{
    cipher->rotor = 1;
    cipher->ratchet = 3;
    cipher->avalanche = 5;
    cipher->lastPlain = 7;
    cipher->lastCipher = 0xB;
    for (int i = 0; i < 0x100; ++i)
        cipher->cards[i] = static_cast<uint8_t>(0xFF - i);
}

uint8_t CipherShuffle(const S_SbCipher *cipher, uint32_t limit, const uint8_t *key, uint8_t keySize, uint8_t *sum, uint32_t *keyAt)   // 0x00739870
{
    if (limit == 0)
        return 0;
    uint32_t mask = 1;
    while (mask < limit)
        mask = mask * 2 + 1;
    uint32_t retries = 0;
    uint32_t choice;
    do {
        *sum = static_cast<uint8_t>(cipher->cards[*sum] + key[*keyAt]);
        if (++*keyAt >= keySize) {
            *keyAt = 0;
            *sum = static_cast<uint8_t>(*sum + keySize);
        }
        choice = *sum & mask;
        if (++retries > 0xB)
            choice %= limit;
    } while (choice > limit);
    return static_cast<uint8_t>(choice);
}

void CipherInit(S_SbCipher *cipher, const uint8_t *key, uint8_t keySize)   // 0x007397b0
{
    if (keySize == 0) {
        CipherInitNoKey(cipher);
        return;
    }
    for (int i = 0; i < 0x100; ++i)
        cipher->cards[i] = static_cast<uint8_t>(i);
    uint8_t sum = 0;
    uint32_t keyAt = 0;
    for (int i = 0xFF; i >= 0; --i) {
        const uint8_t other = CipherShuffle(cipher, static_cast<uint32_t>(i), key, keySize, &sum, &keyAt);
        const uint8_t card = cipher->cards[i];
        cipher->cards[i] = cipher->cards[other];
        cipher->cards[other] = card;
    }
    cipher->rotor = cipher->cards[1];
    cipher->ratchet = cipher->cards[3];
    cipher->avalanche = cipher->cards[5];
    cipher->lastPlain = cipher->cards[7];
    cipher->lastCipher = cipher->cards[sum];
}

uint8_t CipherDecryptByte(S_SbCipher *cipher, uint8_t in)   // 0x00739a50
{
    uint8_t *const c = cipher->cards;
    cipher->ratchet = static_cast<uint8_t>(cipher->ratchet + c[cipher->rotor++]);
    const uint8_t swap = c[cipher->lastCipher];
    c[cipher->lastCipher] = c[cipher->ratchet];
    c[cipher->ratchet] = c[cipher->lastPlain];
    c[cipher->lastPlain] = c[cipher->rotor];
    c[cipher->rotor] = swap;
    cipher->avalanche = static_cast<uint8_t>(cipher->avalanche + c[swap]);
    const uint8_t first = c[c[(c[cipher->lastPlain] + c[cipher->ratchet] + c[cipher->lastCipher]) & 0xFF]];
    const uint8_t second = c[(c[cipher->avalanche] + c[cipher->rotor]) & 0xFF];
    cipher->lastCipher = in;
    cipher->lastPlain = static_cast<uint8_t>(first ^ second ^ in);
    return cipher->lastPlain;
}

void CipherDecrypt(S_SbCipher *cipher, char *data, int size)   // 0x00739b60
{
    for (int i = 0; i < size; ++i)
        data[i] = static_cast<char>(CipherDecryptByte(cipher, static_cast<uint8_t>(data[i])));
}

// ---- the server list ---------------------------------------------------------------------------------------------------

void ReleasePopularValues(S_SbServerList *list)         // 0x0073bb20
{
    for (int i = 0; i < list->popularCount; ++i)
        ReleaseSharedString(list->popularValues[i]);
    list->popularCount = 0;
}

void ReleaseKeyList(S_SbServerList *list)               // 0x0073bb60
{
    if (!list->keyList)
        return;
    for (int i = 0; i < ArrayLength(list->keyList); ++i)
        ReleaseSharedString(static_cast<S_ListKey *>(ArrayNth(list->keyList, i))->name);
    ArrayFree(list->keyList);
    list->keyList = nullptr;
}

void ListDisconnect(S_SbServerList *list)               // 0x0073bac0
{
    std::free(list->inBuffer);
    list->inBuffer = nullptr;
    list->inBufferSize = 0;
    if (list->socket != INVALID_SOCKET)
        closesocket(list->socket);
    list->socket = INVALID_SOCKET;
    list->state = LIST_STATE_DISCONNECTED;
    ReleaseKeyList(list);
    list->expected = -1;
    ReleasePopularValues(list);
}

void ListNotify(S_SbServerList *list, int reason, S_SbServer *server)
{
    list->callback(list, reason, server, list->instance);
}

void ListError(S_SbServerList *list)                    // 0x0073bcd0
{
    ListNotify(list, LIST_DISCONNECTED, nullptr);
    ListDisconnect(list);
}

void AddToList(S_SbServerList *list, S_SbServer *server)   // 0x0073ccc0
{
    ArrayAppend(list->servers, &server);
    ListNotify(list, LIST_SERVER_ADDED, server);
}

int FindServer(const S_SbServerList *list, uint32_t ip, u_short port)   // 0x0073cd30
{
    const int count = ArrayLength(list->servers);
    for (int i = 0; i < count; ++i) {
        const S_SbServer *server = *static_cast<S_SbServer **>(ArrayNth(list->servers, i));
        if (server->publicIp == ip && server->publicPort == port)
            return i;
    }
    return -1;
}

void AddToDeadList(S_SbServerList *list, S_SbServer *server)   // 0x0073cdd0
{
    server->next = list->deadList;
    list->deadList = server;
}

void RemoveFromList(S_SbServerList *list, int index)    // 0x0073cd90
{
    S_SbServer *server = *static_cast<S_SbServer **>(ArrayNth(list->servers, index));
    ListNotify(list, LIST_SERVER_REMOVED, server);
    ArrayDeleteAt(list->servers, index);
    AddToDeadList(list, server);
}

void FreeDeadList(S_SbServerList *list)                 // 0x0073ce10
{
    S_SbServer *server = list->deadList;
    while (server) {
        S_SbServer *next = server->next;
        FreeServer(server);
        server = next;
    }
    list->deadList = nullptr;
}

void ClearList(S_SbServerList *list)                    // 0x0073ce50
{
    const int count = ArrayLength(list->servers);
    for (int i = 0; i < count; ++i)
        AddToDeadList(list, *static_cast<S_SbServer **>(ArrayNth(list->servers, i)));
    ArrayClear(list->servers);
    FreeDeadList(list);
}

void ListInit(S_SbServerList *list, const char *queryForGame, const char *queryFromGame, const char *queryFromKey,
              int queryFromVersion, SbListCallback callback, void *instance)   // 0x0073b3d0
{
    list->state = LIST_STATE_DISCONNECTED;
    list->servers = ArrayNew(sizeof(S_SbServer *), 100, nullptr);
    list->deadList = nullptr;
    SbSharedStrings();
    std::strcpy(list->queryForGame, queryForGame);
    std::strcpy(list->queryFromGame, queryFromGame);
    std::strcpy(list->queryFromKey, queryFromKey);
    list->callback = callback;
    list->instance = instance;
    list->publicIp = 0;
    list->socket = INVALID_SOCKET;
    list->inBuffer = nullptr;
    list->inBufferSize = 0;
    list->keyList = nullptr;
    list->expected = -1;
    list->popularCount = 0;
    list->errorMessage = "";
    list->sourceIp = 0;
    list->queryFromVersion = queryFromVersion;
    std::srand(GetTickCount());
    SocketStartUp();
}

int ConnectToMaster(S_SbServerList *list)               // 0x0073b6d0
{
    char host[0x80];
    if (g_sbMasterOverride)
        std::strcpy(host, g_sbMasterOverride);
    else
        std::sprintf(host, "%s.ms%d.gamespy.com", list->queryForGame, HashString(list->queryForGame, 0x14));   // (0x0073b7e0: the same hash)
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(MASTER_PORT);
    to.sin_addr.s_addr = inet_addr(host);
    if (to.sin_addr.s_addr == INADDR_NONE) {
        const hostent *found = gethostbyname(host);
        if (!found)
            return SB_DNS_ERROR;
        std::memcpy(&to.sin_addr, found->h_addr_list[0], 4);
    }
    if (list->socket == INVALID_SOCKET) {
        list->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (list->socket == INVALID_SOCKET)
            return SB_SOCKET_ERROR;
    }
    if (connect(list->socket, reinterpret_cast<const sockaddr *>(&to), sizeof to) == 0)
        return 0;
    closesocket(list->socket);
    list->socket = INVALID_SOCKET;
    return SB_CONNECT_ERROR;
}

// Our half of the cipher's key: printable, with a parity the server's end can check.
void MakeChallenge(S_SbServerList *list)                // 0x0073b8f0
{
    auto *const challenge = reinterpret_cast<uint8_t *>(list->challenge);
    challenge[0] = static_cast<uint8_t>(std::rand() % 0x5D + 0x21);
    uint32_t parity = 0;
    for (int i = 1; i < 8; ++i) {
        const uint8_t first = challenge[0];
        const uint8_t previous = challenge[i - 1];
        parity ^= ((previous ^ first) & 1u) ^ (static_cast<uint32_t>(i) & 1u) ^
                  static_cast<uint32_t>(static_cast<int8_t>(previous) < static_cast<int8_t>(first)) ^
                  static_cast<uint32_t>(static_cast<int8_t>(first) < 'O');
        const int digit = std::rand() % 0x5D;
        const auto character = static_cast<uint8_t>(digit + 0x21);
        const bool bump = parity == 0 ? (character & 1) == 1 : (character & 1) == 0;
        challenge[i] = bump ? static_cast<uint8_t>(digit + 0x22) : character;
    }
}

// (From 0x0073b4a0: the request's builders 0x0073b820, 0x0073b870, 0x0073b890, 0x0073b8b0.)
struct S_RequestWriter {
    char *at;
    int size;
    void Bytes(const void *data, int count)
    {
        std::memcpy(at, data, static_cast<size_t>(count));
        at += count;
        size += count;
    }
    void Byte(uint8_t value) { Bytes(&value, 1); }
    void Int(uint32_t value) { Bytes(&value, 4); }
    void String(const char *text) { Bytes(text, static_cast<int>(std::strlen(text)) + 1); }
};

int ListConnectAndQuery(S_SbServerList *list, const char *fields, const char *filter, uint32_t options, uint32_t maxServers)   // 0x0073b4a0
{
    if (!fields)
        fields = "";
    if (!filter)
        filter = "";
    if (std::strlen(fields) > 0x100 || std::strlen(filter) > 0x100)
        return SB_PARAMETER_ERROR;
    const int connected = ConnectToMaster(list);
    if (connected != 0)
        return connected;
    list->options = options;
    MakeChallenge(list);
    char packet[0x300];
    S_RequestWriter out = {packet + 2, 2};
    out.Byte(0);
    out.Byte(1);
    out.Byte(3);
    out.Int(static_cast<uint32_t>(list->queryFromVersion));
    out.String(list->queryForGame);
    out.String(list->queryFromGame);
    out.Bytes(list->challenge, 8);
    out.String(filter);
    out.String(fields);
    out.Int(htonl(options));
    if (options & OPTION_ALTERNATE_SOURCE_IP)
        out.Int(list->sourceIp);
    if (options & OPTION_LIMIT_RESULT_COUNT)
        out.Int(maxServers);
    const u_short size = htons(static_cast<u_short>(out.size));
    std::memcpy(packet, &size, 2);
    if (send(list->socket, packet, out.size, 0) < 1) {
        ListDisconnect(list);
        return SB_CONNECT_ERROR;
    }
    list->state = LIST_STATE_MAIN_LIST;
    list->parseState = PARSE_CRYPT_HEADER;
    if (!list->inBuffer) {
        list->inBuffer = static_cast<char *>(std::malloc(INBUFFER_SIZE));
        if (!list->inBuffer)
            return SB_MEMORY_ERROR;
        list->inBufferSize = 0;
    }
    return 0;
}

// The server's half of the key mixed into ours.
void SetUpCipher(S_SbServerList *list, const char *data, int size)   // 0x0073c2e0
{
    const auto keySize = static_cast<int>(std::strlen(list->queryFromKey));
    auto *const challenge = reinterpret_cast<uint8_t *>(list->challenge);
    for (int i = 0; i < size; ++i) {
        challenge[(list->queryFromKey[i % keySize] * i) % 8] ^= static_cast<uint8_t>(challenge[i % 8] ^ static_cast<uint8_t>(data[i]));
    }
    CipherInit(&list->cipher, challenge, 8);
}

int ServerHeaderSize(uint8_t flags)                     // 0x0073c480
{
    int size = (flags & FLAG_PRIVATE_IP) ? 9 : 5;
    if (flags & FLAG_ICMP_IP)
        size += 4;
    if (flags & FLAG_NONSTANDARD_PORT)
        size += 2;
    if (flags & FLAG_NONSTANDARD_PRIVATE_PORT)
        size += 2;
    return size;
}

// The list's fixed fields all here.
bool HasFixedFields(const S_SbServerList *list, const char *data, int size)   // 0x0073c4f0
{
    const int count = ArrayLength(list->keyList);
    for (int i = 0; i < count; ++i) {
        const int type = static_cast<S_ListKey *>(ArrayNth(list->keyList, i))->type;
        if (type == 0) {
            if (size < 1)
                return false;
            const char popular = *data++;
            --size;
            if (popular == -1) {
                const int valueSize = StringSize(data, size);
                if (valueSize == -1)
                    return false;
                data += valueSize;
                size -= valueSize;
            }
        } else if (type == 1) {
            ++data;
            --size;
        } else if (type == 2) {
            data += 2;
            size -= 2;
        } else {
            return false;
        }
        if (size < 0)
            return false;
    }
    return true;
}

// The list of keys and values all here (to its empty key).
bool HasFullKeys(const char *data, int size)            // 0x0073c4b0
{
    while (size >= 1 && *data) {
        const int keySize = StringSize(data, size);
        if (keySize < 0)
            return false;
        const int valueSize = StringSize(data + keySize, size - keySize);
        if (valueSize < 0)
            return false;
        data += keySize + valueSize;
        size -= keySize + valueSize;
    }
    return size != 0 && *data == 0;
}

void ReadAddress(const S_SbServerList *list, const char *data, int size, uint32_t *ip, u_short *port)   // 0x0073c580
{
    if (size <= 4)
        return;
    *ip = ReadU32(data + 1);
    if (!(static_cast<uint8_t>(*data) & FLAG_NONSTANDARD_PORT))
        *port = list->defaultPort;
    else if (size - 5 > 1)
        *port = ReadU16(data + 5);
}

// A server's record into `server`; the bytes used. `usePopular`: the main list's (string values by their popular index).
int ParseServer(const S_SbServerList *list, S_SbServer *server, const char *record, int size, bool usePopular)   // 0x0073c5d0
{
    const auto flags = static_cast<uint8_t>(*record);
    server->flags = flags;
    const char *data = record + 5;
    int left = size - 5;
    if (flags & FLAG_NONSTANDARD_PORT) {
        data = record + 7;
        left = size - 7;
    }
    uint32_t privateIp = 0;
    if (flags & FLAG_PRIVATE_IP) {
        privateIp = ReadU32(data);
        data += 4;
        left -= 4;
    }
    u_short privatePort = list->defaultPort;
    if (flags & FLAG_NONSTANDARD_PRIVATE_PORT) {
        privatePort = ReadU16(data);
        data += 2;
        left -= 2;
    }
    server->privateIp = privateIp;
    server->privatePort = privatePort;
    if (flags & FLAG_ICMP_IP) {
        server->icmpIp = ReadU32(data);
        data += 4;
        left -= 4;
    }
    if (flags & FLAG_HAS_KEYS) {
        const int count = ArrayLength(list->keyList);
        for (int i = 0; i < count; ++i) {
            const auto *key = static_cast<S_ListKey *>(ArrayNth(list->keyList, i));
            if (key->type == 0) {
                int popular = 0xFF;
                if (usePopular) {
                    popular = static_cast<uint8_t>(*data++);
                    --left;
                }
                if (popular != 0xFF) {
                    SetValue(server, key->name, list->popularValues[popular]);
                } else {
                    SetValue(server, key->name, data);
                    const auto valueSize = static_cast<int>(std::strlen(data)) + 1;
                    data += valueSize;
                    left -= valueSize;
                }
            } else if (key->type == 1) {
                SetIntValue(server, key->name, static_cast<uint8_t>(*data));
                ++data;
                --left;
            } else if (key->type == 2) {
                SetIntValue(server, key->name, ntohs(ReadU16(data)));
                data += 2;
                left -= 2;
            }
        }
        server->state |= SERVER_BASIC_KEYS;
    }
    if (flags & FLAG_HAS_FULL_KEYS) {
        while (*data && left > 0) {
            const char *value = data + std::strlen(data) + 1;
            SetValue(server, data, value);
            const char *next = value + std::strlen(value) + 1;
            left -= static_cast<int>(next - data);
            data = next;
        }
        --left;
        server->state |= SERVER_FULL_KEYS;
    }
    return size - left;
}

// The main list's next server; the bytes used, 0: not all here yet, -1: the list's end, -2: out of memory.
int ParseListServer(S_SbServerList *list, const char *data, int size)   // 0x0073c370
{
    if (size < 1)
        return 0;
    const auto flags = static_cast<uint8_t>(*data);
    const int headerSize = ServerHeaderSize(flags);
    if (size < headerSize)
        return 0;
    if ((flags & FLAG_HAS_KEYS) && !HasFixedFields(list, data + headerSize, size - headerSize))
        return 0;
    if ((flags & FLAG_HAS_FULL_KEYS) && !HasFullKeys(data + headerSize, size - headerSize))
        return 0;
    if (ReadU32(data + 1) == 0xFFFFFFFFu)
        return -1;
    uint32_t ip = 0;
    u_short port = 0;
    ReadAddress(list, data, size, &ip, &port);
    S_SbServer *server = NewServer(ip, port);
    if (!server)
        return -2;
    const int used = ParseServer(list, server, data, size, true);
    AddToList(list, server);
    return used;
}

// What arrived of the main list (it may stop anywhere: the rest is kept for the next time).
int ParseMainList(S_SbServerList *list)                 // 0x0073bfb0
{
    char *data = list->inBuffer;
    int size = list->inBufferSize;
    int state = list->parseState;
    const auto keep = [&] {
        if (list->inBuffer) {
            if (size != 0)
                std::memmove(list->inBuffer, data, static_cast<size_t>(size));
            list->inBufferSize = size;
        }
        return 0;
    };
    if (state == PARSE_CRYPT_HEADER) {
        if (size < 1)
            return keep();
        const int headerSize = (static_cast<uint8_t>(*data) ^ 0xEC) + 2;
        if (size < headerSize)
            return keep();
        const int keySize = static_cast<uint8_t>(data[headerSize - 1]) ^ 0xEA;
        const int total = headerSize + keySize;
        if (size < total)
            return keep();
        SetUpCipher(list, data + headerSize, keySize);
        data += total;
        size -= total;
        list->parseState = state = PARSE_FIXED_HEADER;
        CipherDecrypt(&list->cipher, data, size);
    }
    if (state == PARSE_FIXED_HEADER) {
        if (size < 6)
            return keep();
        list->publicIp = ReadU32(data);
        ListNotify(list, LIST_PUBLIC_IP, nullptr);
        list->defaultPort = ReadU16(data + 4);
        if (list->defaultPort == 0xFFFF) {
            if (StringSize(data + 6, size - 6) == -1)
                return keep();
            list->errorMessage = data + 6;
            ListNotify(list, LIST_ERROR_MESSAGE, nullptr);
            if (!list->inBuffer)
                return keep();
        }
        data += 6;
        size -= 6;
        if ((list->options & OPTION_NO_SERVER_LIST) || list->defaultPort == 0xFFFF) {
            list->parseState = PARSE_DONE;
            list->state = LIST_STATE_CONNECTED;
            return keep();
        }
        list->parseState = state = PARSE_KEY_LIST;
        list->expected = -1;
    }
    if (state == PARSE_KEY_LIST) {
        if (list->expected == -1) {
            if (size < 1)
                return keep();
            list->expected = static_cast<uint8_t>(*data);
            list->keyList = ArrayNew(sizeof(S_ListKey), list->expected, nullptr);
            if (!list->keyList)
                return SB_MEMORY_ERROR;
            ++data;
            --size;
        }
        while (ArrayLength(list->keyList) < list->expected) {
            if (size < 2)
                break;
            const int nameSize = StringSize(data + 1, size - 1);
            if (nameSize == -1)
                break;
            const S_ListKey key = {AddSharedString(data + 1), static_cast<uint8_t>(*data)};
            ArrayAppend(list->keyList, &key);
            data += nameSize + 1;
            size -= nameSize + 1;
        }
        if (ArrayLength(list->keyList) < list->expected)
            return keep();
        list->parseState = state = PARSE_POPULAR_VALUES;
        list->expected = -1;
    }
    if (state == PARSE_POPULAR_VALUES) {
        if (list->expected == -1) {
            if (size < 1)
                return keep();
            list->expected = static_cast<uint8_t>(*data);
            ++data;
            --size;
            list->popularCount = 0;
        }
        while (list->popularCount < list->expected) {
            const int valueSize = StringSize(data, size);
            if (valueSize == -1)
                break;
            list->popularValues[list->popularCount] = AddSharedString(data);
            data += valueSize;
            size -= valueSize;
            ++list->popularCount;
        }
        if (list->popularCount < list->expected)
            return keep();
        list->parseState = state = PARSE_SERVERS;
    }
    if (state == PARSE_SERVERS && size > 4) {
        for (;;) {
            const int used = ParseListServer(list, data, size);
            if (used == -2)
                return SB_MEMORY_ERROR;
            if (used == -1)
                break;
            data += used;
            size -= used;
            if (!list->inBuffer || used == 0)
                return keep();
        }
        list->parseState = PARSE_DONE;
        list->state = LIST_STATE_CONNECTED;
        size -= 5;
        data += 5;
        ListNotify(list, LIST_DONE, nullptr);
    }
    return keep();
}

int ReadKeyListMessage(S_SbServerList *list, const char *data, int size)   // 0x0073c8a0
{
    const int count = static_cast<uint8_t>(*data);
    ++data;
    --size;
    if (list->keyList)
        ReleaseKeyList(list);
    list->keyList = ArrayNew(sizeof(S_ListKey), count, nullptr);
    if (!list->keyList)
        return SB_MEMORY_ERROR;
    for (int i = 0; i < count; ++i) {
        if (size < 2)
            return SB_DATA_ERROR;
        const int nameSize = StringSize(data + 1, size - 1);
        if (nameSize == -1)
            return SB_DATA_ERROR;
        const S_ListKey key = {AddSharedString(data + 1), static_cast<uint8_t>(*data)};
        ArrayAppend(list->keyList, &key);
        data += nameSize + 1;
        size -= nameSize + 1;
    }
    return 0;
}

int ReadServerMessage(S_SbServerList *list, const char *data, int size)   // 0x0073c9b0
{
    if (size < 5)
        return SB_DATA_ERROR;
    uint32_t ip = 0;
    u_short port = 0;
    ReadAddress(list, data, size, &ip, &port);
    const int index = FindServer(list, ip, port);
    S_SbServer *server;
    if (index == -1) {
        server = NewServer(ip, port);
        if (!server)
            return SB_MEMORY_ERROR;
    } else {
        server = *static_cast<S_SbServer **>(ArrayNth(list->servers, index));
    }
    if (ParseServer(list, server, data, size, false) < 0)
        return SB_DATA_ERROR;
    if (index == -1)
        AddToList(list, server);
    ListNotify(list, LIST_SERVER_UPDATED, server);
    return 0;
}

int ReadRemoveMessage(S_SbServerList *list, const char *data, int size)   // 0x0073c970
{
    if (size < 6)
        return SB_DATA_ERROR;
    const int index = FindServer(list, ReadU32(data), ReadU16(data + 4));
    if (index != -1)
        RemoveFromList(list, index);
    return 0;
}

// The messages after the main list.
int ProcessMessages(S_SbServerList *list)               // 0x0073c7a0
{
    if (list->inBufferSize <= 2)
        return 0;
    int result = 0;
    for (;;) {
        const int size = ntohs(ReadU16(list->inBuffer));
        // (Fix: below a message's header: an error; the original stayed on it for ever.)
        if (size > INBUFFER_SIZE || size < 3) {
            result = SB_DATA_ERROR;
            break;
        }
        if (list->inBufferSize < size)
            return 0;
        const char *message = list->inBuffer;
        switch (message[2]) {
        case MESSAGE_KEY_LIST:
            result = ReadKeyListMessage(list, message + 3, size - 3);
            break;
        case MESSAGE_SERVER:
            result = ReadServerMessage(list, message + 3, size - 3);
            break;
        case MESSAGE_ECHO:
            if (send(list->socket, message, size, 0) < 1)
                return SB_CONNECT_ERROR;
            break;
        case MESSAGE_REMOVE:
            result = ReadRemoveMessage(list, message + 3, size - 3);
            break;
        default:
            break;
        }
        list->inBufferSize -= size;
        if (list->inBufferSize != 0 && list->inBuffer)
            std::memmove(list->inBuffer, list->inBuffer + size, static_cast<size_t>(list->inBufferSize));
        if (result != 0)
            break;
        if (list->inBufferSize < 3)
            return 0;
    }
    ListError(list);
    return result;
}

int ReadFromMaster(S_SbServerList *list)                // 0x0073bf00
{
    if (!CanReceiveOnSocket(list->socket))
        return 0;
    const int oldSize = list->inBufferSize;
    const int size = recv(list->socket, list->inBuffer + oldSize, INBUFFER_SIZE - oldSize, 0);
    if (size == -1 || size == 0) {
        ListError(list);
        return SB_CONNECT_ERROR;
    }
    list->inBufferSize = oldSize + size;
    if (list->state == LIST_STATE_CONNECTED || list->parseState > PARSE_CRYPT_HEADER)
        CipherDecrypt(&list->cipher, list->inBuffer + oldSize, size);
    if (list->state == LIST_STATE_MAIN_LIST) {
        const int result = ParseMainList(list);
        if (result != 0)
            return result;
    }
    if (list->state == LIST_STATE_CONNECTED && list->inBufferSize > 0)
        return ProcessMessages(list);
    return 0;
}

// (Only the dead ServerBrowserLANUpdate starts it: its broadcast's answers.)
int ReadLanReplies(S_SbServerList *list)                // 0x0073bdf0
{
    char reply[499];
    sockaddr_in from;
    int fromSize = sizeof from;
    for (;;) {
        if (!CanReceiveOnSocket(list->socket)) {
            if (GetTickCount() - list->lanStartTime > LAN_TIMEOUT) {
                closesocket(list->socket);
                list->socket = INVALID_SOCKET;
                list->state = LIST_STATE_DISCONNECTED;
            }
            return 0;
        }
        if (recvfrom(list->socket, reply, sizeof reply, 0, reinterpret_cast<sockaddr *>(&from), &fromSize) == -1)
            continue;
        if (FindServer(list, from.sin_addr.s_addr, from.sin_port) != -1)
            continue;
        S_SbServer *server = NewServer(from.sin_addr.s_addr, from.sin_port);
        if (!server)
            return SB_MEMORY_ERROR;
        server->flags = 0x11;
        AddToList(list, server);
    }
}

int ListThink(S_SbServerList *list)                     // 0x0073bed0
{
    FreeDeadList(list);
    if (list->state == LIST_STATE_LAN)
        return ReadLanReplies(list);
    if (list->state == LIST_STATE_CONNECTED || list->state == LIST_STATE_MAIN_LIST)
        return ReadFromMaster(list);
    return 0;
}

// ---- the browser -------------------------------------------------------------------------------------------------------

void OnListEvent(S_SbServerList *list, int reason, S_SbServer *server, void *instance)   // 0x0073abf0
{
    auto *browser = static_cast<S_ServerBrowser *>(instance);
    const auto notify = [&](int browserReason, S_SbServer *about) {
        browser->callback(browser, browserReason, about, browser->instance);
    };
    switch (reason) {
    case LIST_SERVER_ADDED:
        notify(BROWSER_SERVER_ADDED, server);
        if (!(server->state & (SERVER_BASIC_KEYS | SERVER_FULL_KEYS)) && !browser->dontUpdate) {
            if (browser->list.state != LIST_STATE_LAN && browser->engine.keyCount != 0)
                QueueQuery(&browser->engine, server, false, 0);
            else
                QueueQuery(&browser->engine, server, false, 1);
        }
        break;
    case LIST_SERVER_UPDATED:
        notify((server->state & (SERVER_BASIC_KEYS | SERVER_FULL_KEYS)) ? BROWSER_SERVER_UPDATED : BROWSER_UPDATE_FAILED, server);
        break;
    case LIST_SERVER_REMOVED:
        if (server->state & (SERVER_PENDING_BASIC | SERVER_PENDING_FULL))
            EngineRemove(&browser->engine, server);
        notify(BROWSER_SERVER_REMOVED, server);
        break;
    case LIST_DONE:
        if (browser->disconnectOnComplete)
            ListDisconnect(list);
        break;
    case LIST_ERROR_MESSAGE:
        notify(BROWSER_ERROR_MESSAGE, nullptr);
        break;
    case LIST_PUBLIC_IP:
        browser->engine.publicIp = browser->list.publicIp;
        break;
    default:
        break;
    }
}

void OnQueryEvent(S_SbQueryEngine *, int reason, S_SbServer *server, void *instance)   // 0x0073ad50
{
    auto *browser = static_cast<S_ServerBrowser *>(instance);
    if (reason == QUERY_SUCCESS)
        browser->callback(browser, BROWSER_SERVER_UPDATED, server, browser->instance);
    else if (reason == QUERY_FAILED)
        browser->callback(browser, BROWSER_UPDATE_FAILED, server, browser->instance);
    else if (reason == QUERY_ENGINE_IDLE)
        browser->callback(browser, BROWSER_UPDATES_DONE, server, browser->instance);
}

}  // namespace

const char *g_sbMasterOverride;                         // 0x00982de4 (never set in the game)

// (The browser's "trigger" address of the original, for the dead ServerBrowserAuxUpdateServer, is left out.)
S_ServerBrowser *ServerBrowserNew(const char *queryForGame, const char *queryFromGame, const char *queryFromKey,
                                  int queryFromVersion, int maxUpdates, int queryVersion,
                                  ServerBrowserCallback callback, void *instance)
{
    auto *browser = static_cast<S_ServerBrowser *>(std::malloc(sizeof(S_ServerBrowser)));
    if (!browser)
        return nullptr;
    browser->callback = callback;
    browser->instance = instance;
    browser->dontUpdate = 0;
    browser->disconnectOnComplete = 0;
    ListInit(&browser->list, queryForGame, queryFromGame, queryFromKey, queryFromVersion, OnListEvent, browser);
    EngineInit(&browser->engine, maxUpdates, queryVersion, OnQueryEvent, browser);
    return browser;
}

// (0x0073af00, over 0x0073adf0 with no options: its callers' only form.)
void ServerBrowserUpdate(S_ServerBrowser *browser, int async, int disconnectOnComplete, const uint8_t *fields,
                         int fieldCount, const char *filter)
{
    char fieldText[0x100] = {};
    browser->disconnectOnComplete = disconnectOnComplete;
    browser->engine.keyCount = 0;
    int length = 0;
    for (int i = 0; i < fieldCount; ++i) {
        const char *name = g_qr2KeyNames[fields[i]];
        if (static_cast<int>(std::strlen(name)) + 1 + length > 0xFF)
            break;
        length += std::sprintf(fieldText + length, "\\%s", name);
        EngineAddKey(&browser->engine, fields[i]);
    }
    if (ListConnectAndQuery(&browser->list, fieldText, filter, 0, 0) != 0 || async)
        return;
    int result = 0;
    while (browser->list.state == LIST_STATE_MAIN_LIST || (browser->engine.querying.count > 0 && result == 0)) {
        Sleep(10);
        result = ServerBrowserThink(browser);
    }
}

int ServerBrowserThink(S_ServerBrowser *browser)        // 0x0073b240
{
    EngineThink(&browser->engine);
    return ListThink(&browser->list);
}

// (0x0073b260 inside.)
void ServerBrowserClear(S_ServerBrowser *browser)       // 0x0073b280
{
    ListDisconnect(&browser->list);
    EngineClear(&browser->engine);
    ClearList(&browser->list);
}

const char *SBServerGetStringValue(S_SbServer *server, const char *key, const char *fallback)   // 0x0073a3f0
{
    const S_KeyValue search = {key, nullptr};
    const auto *found = static_cast<S_KeyValue *>(TableLookup(server->keyValues, &search));
    return found ? found->value : fallback;
}

int SBServerGetIntValue(S_SbServer *server, const char *key, int fallback)   // 0x0073a420
{
    if (std::strcmp(key, "ping") == 0)
        return static_cast<int>(SBServerGetPing(server));
    const char *value = SBServerGetStringValue(server, key, nullptr);
    return value ? std::atoi(value) : fallback;
}

uint32_t SBServerGetPing(S_SbServer *server)            // 0x0073aa10
{
    return server->updateTime;
}

uint32_t SBServerGetPublicInetAddress(S_SbServer *server)   // 0x0073a6a0
{
    return server->publicIp;
}
