// GameSpy's query & reporting 2 (part 10f-ii; qr2.c, qr2regkeys.c): a server's heartbeats to the master server, its answers
// to queries (the master's and a browser's, by keys; old-style "\status\" ones), the master's challenge. See gamespy.h,
// gamespy_internal.h.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "net/gamespy_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

void StopGameSpyQueries(uintptr_t *record);

S_Qr2Record g_qr2DefaultRecord = [] {
    S_Qr2Record record = {};
    record.socket = INVALID_SOCKET;
    return record;
}();

namespace {

constexpr int MASTER_PORT = 27900;
constexpr uint32_t HEARTBEAT_TIME = 60000;              // ms
constexpr uint32_t KEEPALIVE_TIME = 20000;
constexpr uint32_t CHALLENGE_RETRY_TIME = 10000;
constexpr int CHALLENGE_TRIES = 4;
constexpr int PACKET_SIZE = QR2_PACKET_SIZE;
constexpr int RECEIVE_SIZE = 0xFF;
constexpr int MAX_LOCAL_IPS = 5;
constexpr int PORT_TRIES = 100;
constexpr int MAX_CHALLENGE = 0x42;
constexpr int MAX_ECHO = 0x20;
constexpr int MESSAGE_KEYS = 10;
constexpr int LISTED = -1;

enum E_Qr2Error { QR2_OK = 0, QR2_SOCKET_ERROR = 1, QR2_BIND_ERROR = 2, QR2_DNS_ERROR = 3, QR2_NO_CHALLENGE_ERROR = 5 };
enum E_Packet {
    PACKET_QUERY = 0,
    PACKET_CHALLENGE = 1,
    PACKET_ECHO = 2,
    PACKET_HEARTBEAT = 3,
    PACKET_ADDERROR = 4,
    PACKET_ECHO_RESPONSE = 5,
    PACKET_CLIENT_MESSAGE = 6,
    PACKET_CLIENT_MESSAGE_ACK = 7,
    PACKET_KEEPALIVE = 8,
};
enum E_State { STATE_NONE = 0, STATE_CHALLENGE_RETRY = 3, STATE_EXITING = 2 };

}  // namespace

const char *g_qr2KeyNames[0x100] = {                       // 0x00845828
    "",           "hostname",   "gamename",  "gamever",  "hostport", "mapname",       "gametype",    "gamevariant",
    "numplayers", "numteams",   "maxplayers", "gamemode", "teamplay", "fraglimit",     "teamfraglimit", "timeelapsed",
    "timelimit",  "roundtime",  "roundelapsed", "password", "groupid", "player_",      "score_",      "skill_",
    "ping_",      "team_",      "deaths_",   "pid_",     "team_t",   "score_t"};

namespace {

uint32_t g_localIps[MAX_LOCAL_IPS];                     // 0x00982dc0
int g_localIpCount;                                     // 0x00982dd4

void BeginReply(S_Qr2Buffer *buffer, char type, const void *instanceKey)   // 0x00738be0
{
    buffer->data[0] = type;
    std::memcpy(buffer->data + 1, instanceKey, 4);
    buffer->length = 5;
}

void Send(const S_Qr2Record *record, const S_Qr2Buffer &buffer, const sockaddr *to)
{
    sendto(record->socket, buffer.data, buffer.length, 0, to, sizeof(sockaddr_in));
}

// `host` (null: any; a dotted address or a name) and `port`; false: not found.
// (0x007388d0; its fourth argument, always null, dropped.)
bool GetSockAddr(const char *host, u_short port, sockaddr_in *address)
{
    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    address->sin_addr.s_addr = host ? inet_addr(host) : 0;
    if (address->sin_addr.s_addr == INADDR_NONE && std::strcmp(host, "255.255.255.255") != 0) {
        const hostent *found = gethostbyname(host);
        if (!found)
            return false;
        address->sin_addr.s_addr = *reinterpret_cast<const u_long *>(found->h_addr_list[0]);
    }
    return true;
}

// The machine's addresses (5 at most).
void FindLocalIps()                                     // 0x00738890
{
    char name[0x100] = {};                              // (0x00737f20)
    gethostname(name, sizeof name);
    const hostent *host = gethostbyname(name);
    if (!host)
        return;
    g_localIpCount = 0;
    while (host->h_addr_list[g_localIpCount] && g_localIpCount < MAX_LOCAL_IPS) {
        g_localIps[g_localIpCount] = *reinterpret_cast<const uint32_t *>(host->h_addr_list[g_localIpCount]);
        ++g_localIpCount;
    }
}

// A socket on the first free port from `*port` (100 tried).
int InitSocket(SOCKET *out, const char *ip, int *port)  // 0x00738400
{
    SocketStartUp();
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return QR2_SOCKET_ERROR;
    int at = *port;
    const int end = at + PORT_TRIES;
    sockaddr_in address;
    int result = SOCKET_ERROR;
    for (; at < end; ++at) {
        GetSockAddr(ip, static_cast<u_short>(at), &address);
        if (address.sin_addr.s_addr == htonl(INADDR_LOOPBACK))
            address.sin_addr.s_addr = 0;
        result = bind(s, reinterpret_cast<const sockaddr *>(&address), sizeof address);
        if (result == 0)
            break;
    }
    if (result != 0)
        return QR2_BIND_ERROR;
    if (at == 0) {
        int size = sizeof address;
        if (getsockname(s, reinterpret_cast<sockaddr *>(&address), &size) != 0)
            return QR2_BIND_ERROR;
        at = ntohs(address.sin_port);
    }
    *out = s;
    *port = at;
    return QR2_OK;
}

int InitWithSocket(S_Qr2Record **out, SOCKET socket, int port, const char *gameName, const char *secretKey, int isPublic,
                   int natNegotiate, Qr2ServerKeyCallback serverKey, Qr2PlayerKeyCallback playerKey,
                   Qr2TeamKeyCallback teamKey, Qr2KeyListCallback keyList, Qr2CountCallback count,
                   Qr2AddErrorCallback addError, void *userData)   // 0x00738260
{
    S_Qr2Record *record = &g_qr2DefaultRecord;
    if (out) {
        record = static_cast<S_Qr2Record *>(std::malloc(sizeof(S_Qr2Record)));
        *out = record;
    }
    std::srand(GetTickCount());
    std::strcpy(record->gameName, gameName);
    std::strcpy(record->secretKey, secretKey);
    record->queryPort = port;
    record->userData = userData;
    record->serverKey = serverKey;
    record->playerKey = playerKey;
    record->teamKey = teamKey;
    record->keyList = keyList;
    record->socket = socket;
    record->lastHeartbeat = 0;
    record->lastKeepAlive = 0;
    record->listedState = 1;
    record->count = count;
    record->addError = addError;
    record->natCallback = nullptr;
    record->clientMessage = nullptr;
    record->cdKeyProcess = nullptr;
    record->isPublic = isPublic;
    record->readSocket = 0;
    record->natNegotiate = natNegotiate;
    for (uint8_t &byte : record->instanceKey)
        byte = static_cast<uint8_t>(std::rand() % 0xFF);
    for (int &key : record->clientMessageKeys)
        key = -1;
    record->nextMessageKey = 0;
    if (g_localIpCount == 0)
        FindLocalIps();
    if (isPublic) {
        char master[0x40];
        std::sprintf(master, "%s.master.gamespy.com", gameName);
        if (!GetSockAddr(master, MASTER_PORT, &record->masterAddress))
            return QR2_DNS_ERROR;
    }
    return QR2_OK;
}

// One kind of keys' values of a query: `keys` (0xFF: the list's, each named first).
void BuildKeyTypeReply(S_Qr2Record *record, S_Qr2Buffer *buffer, int keyType, int keyCount, const uint8_t *keys)   // 0x00739020
{
    if (keyCount == 0)
        return;
    int count = 1;
    if (keyType == QR2_KEY_PLAYER || keyType == QR2_KEY_TEAM) {
        if (PACKET_SIZE - buffer->length < 2)
            return;
        count = record->count(keyType, record->userData);
        const u_short network = htons(static_cast<u_short>(count));
        std::memcpy(buffer->data + buffer->length, &network, sizeof network);
        buffer->length += 2;
    }
    S_Qr2KeyBuffer list;
    list.count = 0;
    if (keyCount == 0xFF) {
        record->keyList(keyType, &list, record->userData);
        for (int i = 0; i < list.count; ++i) {
            const char *name = g_qr2KeyNames[list.keys[i]];
            qr2_buffer_add(buffer, name ? name : "unknown");
            if (keyType == QR2_KEY_SERVER) {
                const int before = buffer->length;
                record->serverKey(list.keys[i], buffer, record->userData);
                if (before == buffer->length)
                    qr2_buffer_add(buffer, "");
            }
        }
        if (PACKET_SIZE - buffer->length < 1)
            return;
        buffer->data[buffer->length++] = 0;
        keyCount = list.count;
        keys = list.keys;
        if (keyType == QR2_KEY_SERVER)
            return;
    }
    for (int index = 0; index < count; ++index) {
        for (int i = 0; i < keyCount; ++i) {
            const int before = buffer->length;
            if (keyType == QR2_KEY_SERVER)
                record->serverKey(keys[i], buffer, record->userData);
            else if (keyType == QR2_KEY_PLAYER)
                record->playerKey(keys[i], index, buffer, record->userData);
            else if (keyType == QR2_KEY_TEAM)
                record->teamKey(keys[i], index, buffer, record->userData);
            if (before == buffer->length)
                qr2_buffer_add(buffer, "");
        }
    }
}

void BuildReply(S_Qr2Record *record, S_Qr2Buffer *buffer, int serverCount, const uint8_t *serverKeys, int playerCount,
                const uint8_t *playerKeys, int teamCount, const uint8_t *teamKeys)   // 0x00738fd0
{
    BuildKeyTypeReply(record, buffer, QR2_KEY_SERVER, serverCount, serverKeys);
    BuildKeyTypeReply(record, buffer, QR2_KEY_PLAYER, playerCount, playerKeys);
    BuildKeyTypeReply(record, buffer, QR2_KEY_TEAM, teamCount, teamKeys);
}

// A query's three lists of keys (each counted in a byte; 0xFF: all).
void HandleQuery(S_Qr2Record *record, S_Qr2Buffer *buffer, const uint8_t *data, int size)   // 0x00738ef0
{
    if (size < 3)
        return;
    const auto keyList = [&](const uint8_t **keys) {
        const int count = *data++;
        --size;
        *keys = nullptr;
        if (count != 0 && count != 0xFF) {
            *keys = data;
            data += count;
            size -= count;
        }
        return count;
    };
    const uint8_t *serverKeys, *playerKeys, *teamKeys;
    const int serverCount = keyList(&serverKeys);
    if (size < 2)
        return;
    const int playerCount = keyList(&playerKeys);
    if (size < 1)
        return;
    const int teamCount = keyList(&teamKeys);
    if (size < 0)
        return;
    BuildReply(record, buffer, serverCount, serverKeys, playerCount, playerKeys, teamCount, teamKeys);
}

// The challenge's cipher (an RC4 with the key stepped by the data).
void Encrypt(const char *key, int keySize, uint8_t *data, int size)   // 0x00738d80
{
    uint8_t state[0x100];
    for (int i = 0; i < 0x100; ++i)
        state[i] = static_cast<uint8_t>(i);
    unsigned j = 0;
    int k = 0;
    for (int i = 0; i < 0x100; ++i) {
        j = (state[i] + j + static_cast<uint8_t>(key[k])) & 0xFF;
        k = static_cast<uint8_t>(k + 1) % keySize;
        std::swap(state[i], state[j]);
    }
    unsigned x = 0, y = 0;
    for (int i = 0; i < size; ++i) {
        x = (data[i] + 1u + x) & 0xFF;
        y = (state[x] + y) & 0xFF;
        std::swap(state[x], state[y]);
        data[i] = static_cast<uint8_t>(data[i] ^ state[(state[x] + state[y]) & 0xFF]);
    }
}

char Base64Digit(uint8_t value)                         // 0x00738d50
{
    if (value < 26)
        return static_cast<char>(value + 'A');
    if (value < 52)
        return static_cast<char>(value + 'a' - 26);
    if (value < 62)
        return static_cast<char>(value + '0' - 52);
    if (value == 62)
        return '+';
    return value == 63 ? '/' : 0;
}

// In 4 digits for each 3 bytes (no padding: zeros).
void Base64(const uint8_t *data, int size, char *out)   // 0x00738c90
{
    for (int i = 0; i < size; i += 3) {
        const uint8_t a = data[i];
        const uint8_t b = i + 1 < size ? data[i + 1] : 0;
        const uint8_t c = i + 2 < size ? data[i + 2] : 0;
        *out++ = Base64Digit(a >> 2);
        *out++ = Base64Digit(static_cast<uint8_t>((a & 3) << 4 | b >> 4));
        *out++ = Base64Digit(static_cast<uint8_t>((b & 0xF) << 2 | c >> 6));
        *out++ = Base64Digit(c & 0x3F);
    }
    *out = 0;
}

// The master's challenge: encrypted with the secret key, in base 64.
void HandleChallenge(S_Qr2Record *record, S_Qr2Buffer *buffer, const char *data, int size)   // 0x00738c00
{
    if (size <= 0 || size >= MAX_CHALLENGE || data[size - 1] != 0)
        return;
    uint8_t challenge[MAX_CHALLENGE];
    std::memcpy(challenge, data, static_cast<size_t>(size));
    Encrypt(record->secretKey, static_cast<int>(std::strlen(record->secretKey)), challenge, size - 1);
    char *out = buffer->data + buffer->length;
    Base64(challenge, size - 1, out);
    buffer->length += static_cast<int>(std::strlen(out)) + 1;
}

// A client message seen lately (else remembered).
bool MessageKeySeen(S_Qr2Record *record, int key)       // 0x00739520
{
    for (int known : record->clientMessageKeys) {
        if (known == key)
            return true;
    }
    record->nextMessageKey = (record->nextMessageKey + 1) % MESSAGE_KEYS;
    record->clientMessageKeys[record->nextMessageKey] = key;
    return false;
}

// One kind of keys in the old "\key\value\" form (a player's and a team's keys numbered).
void BuildOldKeys(S_Qr2Record *record, S_Qr2Buffer *buffer, int keyType)   // 0x007392b0
{
    int count = 0;
    if (keyType == QR2_KEY_PLAYER || keyType == QR2_KEY_TEAM)
        count = record->count(keyType, record->userData);
    S_Qr2KeyBuffer list;
    list.count = 0;
    record->keyList(keyType, &list, record->userData);
    const auto separate = [&] { buffer->data[buffer->length - 1] = '\\'; };
    for (int i = 0; i < list.count; ++i) {
        const char *name = g_qr2KeyNames[list.keys[i]];
        if (!name)
            name = "unknown";
        if (keyType == QR2_KEY_SERVER) {
            qr2_buffer_add(buffer, name);
            separate();
            const int before = buffer->length;
            record->serverKey(list.keys[i], buffer, record->userData);
            if (before == buffer->length)
                qr2_buffer_add(buffer, "");
            separate();
        } else {
            for (int index = 0; index < count; ++index) {
                char numbered[0x80];
                std::sprintf(numbered, "%s%d", name, index);
                qr2_buffer_add(buffer, numbered);
                separate();
                const int before = buffer->length;
                if (keyType == QR2_KEY_PLAYER)
                    record->playerKey(list.keys[i], index, buffer, record->userData);
                else if (keyType == QR2_KEY_TEAM)
                    record->teamKey(list.keys[i], index, buffer, record->userData);
                if (before == buffer->length)
                    qr2_buffer_add(buffer, "");
                separate();
            }
        }
    }
}

void BuildOldReply(S_Qr2Record *record, S_Qr2Buffer *buffer)   // 0x00739260
{
    buffer->length = 1;
    buffer->data[0] = '\\';
    BuildOldKeys(record, buffer, QR2_KEY_SERVER);
    BuildOldKeys(record, buffer, QR2_KEY_PLAYER);
    BuildOldKeys(record, buffer, QR2_KEY_TEAM);
    qr2_buffer_add(buffer, "final\\\\queryid\\1.1");
    --buffer->length;
}

void HandlePacket(S_Qr2Record *record, char *packet, int size, sockaddr *from)   // 0x00738950
{
    S_Qr2Buffer buffer;
    buffer.length = 0;
    if (packet[0] == ';') {
        if (record->cdKeyProcess)
            record->cdKeyProcess(packet, size, from);
        return;
    }
    if (packet[0] == '\\') {
        BuildOldReply(record, &buffer);
        Send(record, buffer, from);
        return;
    }
    if (size < 7 || static_cast<uint8_t>(packet[0]) != 0xFE || static_cast<uint8_t>(packet[1]) != 0xFD)
        return;
    if (record->listedState > 0)
        record->listedState = 0;
    const char type = packet[2];
    char *const instance = packet + 3;
    char *const data = packet + 7;
    int dataSize = size - 7;
    BeginReply(&buffer, type, instance);
    switch (type) {
    case PACKET_QUERY:
        HandleQuery(record, &buffer, reinterpret_cast<const uint8_t *>(data), dataSize);
        break;
    case PACKET_CHALLENGE:
        HandleChallenge(record, &buffer, data, dataSize);
        break;
    case PACKET_ECHO:
        if (dataSize > MAX_ECHO)
            dataSize = MAX_ECHO;
        buffer.data[0] = PACKET_ECHO_RESPONSE;
        std::memcpy(buffer.data + buffer.length, data, static_cast<size_t>(dataSize));
        buffer.length += dataSize;
        break;
    case PACKET_ADDERROR:
        if (record->listedState == LISTED || std::memcmp(instance, record->instanceKey, 4) != 0 || dataSize < 2)
            return;
        record->listedState = LISTED;
        record->addError(data[0], data + 1, record->userData);
        return;
    case PACKET_CLIENT_MESSAGE: {
        if (std::memcmp(instance, record->instanceKey, 4) != 0 || dataSize < 4)
            return;
        int key;
        std::memcpy(&key, data, sizeof key);
        buffer.data[0] = PACKET_CLIENT_MESSAGE_ACK;
        std::memcpy(buffer.data + buffer.length, &key, sizeof key);
        buffer.length += 4;
        // (The messages' and the NAT cookies' callbacks, 0x00739480: the game sets neither, dropped.)
        MessageKeySeen(record, key);
        break;
    }
    default:
        return;
    }
    Send(record, buffer, from);
}

void SendKeepAlive(S_Qr2Record *record)                 // 0x00739570
{
    S_Qr2Buffer buffer;
    BeginReply(&buffer, PACKET_KEEPALIVE, record->instanceKey);
    Send(record, buffer, reinterpret_cast<const sockaddr *>(&record->masterAddress));
    record->lastKeepAlive = GetTickCount();
}

void SendHeartbeat(S_Qr2Record *record, int state)      // 0x007395d0
{
    S_Qr2Buffer buffer;
    BeginReply(&buffer, PACKET_HEARTBEAT, record->instanceKey);
    for (int i = 0; i < g_localIpCount; ++i) {
        char name[0x10];
        std::sprintf(name, "localip%d", i);
        qr2_buffer_add(&buffer, name);
        in_addr address;
        address.s_addr = g_localIps[i];
        qr2_buffer_add(&buffer, inet_ntoa(address));
    }
    qr2_buffer_add(&buffer, "localport");
    qr2_buffer_add_int(&buffer, record->queryPort);
    qr2_buffer_add(&buffer, "natneg");
    qr2_buffer_add(&buffer, record->natNegotiate ? "1" : "0");
    if (state != STATE_NONE) {
        qr2_buffer_add(&buffer, "statechanged");
        qr2_buffer_add_int(&buffer, state);
    }
    qr2_buffer_add(&buffer, "gamename");
    qr2_buffer_add(&buffer, record->gameName);
    if (state == STATE_EXITING) {
        if (PACKET_SIZE - buffer.length > 0)
            buffer.data[buffer.length++] = 0;
    } else {
        BuildReply(record, &buffer, 0xFF, nullptr, 0xFF, nullptr, 0xFF, nullptr);
    }
    Send(record, buffer, reinterpret_cast<const sockaddr *>(&record->masterAddress));
    const uint32_t now = GetTickCount();
    record->lastHeartbeat = now;
    record->lastKeepAlive = now;
}

void SendHeartbeatsIfDue(S_Qr2Record *record)           // 0x00738670
{
    const uint32_t now = GetTickCount();
    if (record->socket == INVALID_SOCKET)
        return;
    if (record->listedState > 0 && now - record->lastHeartbeat > CHALLENGE_RETRY_TIME) {
        // No challenge yet: asked again, then told.
        if (record->listedState < CHALLENGE_TRIES) {
            SendHeartbeat(record, STATE_CHALLENGE_RETRY);
            ++record->listedState;
        } else {
            record->listedState = 0;
            record->addError(QR2_NO_CHALLENGE_ERROR, const_cast<char *>("No challenge value was received from the master server."),
                             record->userData);
        }
    } else if (now - record->lastHeartbeat > HEARTBEAT_TIME || record->lastHeartbeat == 0 || now < record->lastHeartbeat) {
        SendHeartbeat(record, STATE_NONE);
    }
    if (now - record->lastKeepAlive > KEEPALIVE_TIME)
        SendKeepAlive(record);
}

}  // namespace

int qr2_init(void *record, const char *ip, int basePort, const char *gameName, const char *secretKey, int isPublic,
             int natNegotiate, Qr2ServerKeyCallback serverKey, Qr2PlayerKeyCallback playerKey,
             Qr2TeamKeyCallback teamKey, Qr2KeyListCallback keyList, Qr2CountCallback count,
             Qr2AddErrorCallback addError, void *userData)
{
    auto **out = static_cast<S_Qr2Record **>(record);
    SOCKET socket = INVALID_SOCKET;
    int port = basePort;
    int result = InitSocket(&socket, ip, &port);
    if (result != QR2_OK) {
        WSACleanup();
        return result;
    }
    result = InitWithSocket(out, socket, port, gameName, secretKey, isPublic, natNegotiate, serverKey, playerKey, teamKey,
                            keyList, count, addError, userData);
    (out ? *out : &g_qr2DefaultRecord)->readSocket = 1;
    return result;
}

void qr2_think(void *record)
{
    auto *reporting = record ? static_cast<S_Qr2Record *>(record) : &g_qr2DefaultRecord;
    if (reporting->isPublic)
        SendHeartbeatsIfDue(reporting);
    if (!reporting->readSocket)
        return;
    static char packet[RECEIVE_SIZE + 1];               // 0x00982cc0
    while (CanReceiveOnSocket(reporting->socket)) {
        sockaddr_in from;
        int fromSize = sizeof from;
        const int size = recvfrom(reporting->socket, packet, RECEIVE_SIZE, 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
        if (size == SOCKET_ERROR)
            continue;
        packet[size] = 0;
        HandlePacket(reporting, packet, size, reinterpret_cast<sockaddr *>(&from));
    }
}

// (GameSpy's qr2_shutdown; the net game's name, net_game.h.)
void StopGameSpyQueries(uintptr_t *record)
{
    auto *reporting = record ? reinterpret_cast<S_Qr2Record *>(record) : &g_qr2DefaultRecord;
    if (reporting->isPublic)
        SendHeartbeat(reporting, STATE_EXITING);
    if (reporting->socket != INVALID_SOCKET && reporting->readSocket)
        closesocket(reporting->socket);
    reporting->socket = INVALID_SOCKET;
    reporting->lastHeartbeat = 0;
    if (reporting->readSocket)
        WSACleanup();
    if (reporting != &g_qr2DefaultRecord)
        std::free(reporting);
}

void qr2_keybuffer_add(S_Qr2KeyBuffer *keys, int key)
{
    if (keys->count < 0xFE && key > 0 && key < 0xFF)
        keys->keys[keys->count++] = static_cast<uint8_t>(key);
}

void qr2_buffer_add_int(S_Qr2Buffer *buffer, int value)
{
    char text[0x14];
    std::sprintf(text, "%d", value);
    qr2_buffer_add(buffer, text);
}

void qr2_buffer_add(S_Qr2Buffer *buffer, const char *value)
{
    const int room = PACKET_SIZE - buffer->length;
    int size = static_cast<int>(std::strlen(value)) + 1;
    if (size > room)
        size = room;
    if (size == 0)
        return;
    std::memcpy(buffer->data + buffer->length, value, static_cast<size_t>(size));
    buffer->length += size;
    buffer->data[buffer->length - 1] = 0;
}

void qr2_register_key(int key, const char *name)
{
    if (key > 0x31 && key < 0xFF)
        g_qr2KeyNames[key] = name;
}
