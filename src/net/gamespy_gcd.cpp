// GameSpy's CD key client and server (part 10f-i; gcdkeyc.c, gcdkeys.c): a client's response to a challenge, a server's
// checks of its players' keys against the validation server (their answers arrive on qr2's socket, the ';' packets),
// the installer key's own check. See gamespy.h, gamespy_internal.h.
#include "net/gamespy_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "game/pending.h"

namespace {

constexpr int MAX_PRODUCTS = 4;
#ifndef GCD_TEST_PORT
#define GCD_TEST_PORT 29910                            // (the parallel test runs' own)
#endif
constexpr u_short VALIDATION_PORT = GCD_TEST_PORT;
constexpr uint32_t RETRY_TIME = 2000;                   // ms
constexpr int MAX_TRIES = 2;
constexpr int HKEY_SIZE = 0x20;
constexpr size_t RESPONSE_LENGTH = 0x48;
constexpr int MAX_RESPONSE_INPUT = 0x1FF;
constexpr int VALUE_LIMIT = 200;

enum E_ClientState {
    CLIENT_WAITING = 0,                                 // the validation server asked
    CLIENT_GOOD = 1,
    CLIENT_BAD = 2,
    CLIENT_DONE = 3,                                    // told good: playing
};

struct S_Client {                                       // 0x50 bytes
    int localId;
    char hkey[0x24];                                    // +0x04
    int sessionKey;                                     // +0x28
    uint32_t ip;                                        // +0x2C
    uint32_t sentTime;                                  // +0x30
    int tries;                                          // +0x34
    int state;                                          // +0x38
    void *instance;                                     // +0x3C
    GcdAuthCallback authenticated;                      // +0x40
    char *error;                                        // +0x44
    char *request;                                      // +0x48
    int requestSize;                                    // +0x4C
};

struct S_Node {                                         // 0x0C bytes (the head's data unused)
    S_Client *client;
    S_Node *next;
    S_Node *previous;
};

struct S_Product {                                      // 0x10 bytes (0x0098c160)
    int id;
    S_Node clients;                                     // +0x04
};

S_Product g_products[MAX_PRODUCTS];
int g_productCount;                                     // 0x0098277c
SOCKET g_socket = INVALID_SOCKET;                       // 0x00845244
sockaddr_in g_validationServer;                         // 0x00982728

// A packet's bytes xored with the game's key.
void XorKey(char *data, int size)                       // 0x00734d10
{
    static const char KEY[] = "gamespy";                // 0x00982518
    const char *key = KEY;
    for (int i = 0; i < size; ++i) {
        data[i] = static_cast<char>(data[i] ^ *key);
        if (*++key == 0)
            key = KEY;
    }
}

S_Product *FindProduct(int id)                          // 0x00734e40
{
    for (int i = 0; i < g_productCount; ++i) {
        if (g_products[i].id == id)
            return &g_products[i];
    }
    return nullptr;
}

S_Client *FindClient(const char *hkey, int sessionKey)  // 0x007346a0
{
    for (int i = 0; i < g_productCount; ++i) {
        for (S_Node *node = g_products[i].clients.next; node; node = node->next) {
            if (std::strcmp(hkey, node->client->hkey) == 0 && (sessionKey == -1 || node->client->sessionKey == sessionKey))
                return node->client;
        }
    }
    return nullptr;
}

void ListAdd(S_Node *node, S_Node *head)                // 0x00734e70
{
    S_Node *last = head;
    while (last->next)
        last = last->next;
    last->next = node;
    node->previous = last;
    node->next = nullptr;
}

void ListRemove(S_Node *node)                           // 0x00734e90
{
    node->previous->next = node->next;
    if (node->next)
        node->next->previous = node->previous;
}

// The value of `\key\value` in a packet, "" for none (two calls' values stay usable).
const char *ValueForKey(const char *packet, const char *key)   // 0x00734c00
{
    static char values[2][0x100];                       // 0x00982528
    static int which;                                   // 0x00982524
    which ^= 1;
    char search[0x100];
    std::snprintf(search, sizeof search, "\\%s\\", key);
    const char *found = std::strstr(packet, search);
    if (!found)
        return "";
    found += std::strlen(search);
    char *out = values[which];
    int length = 0;
    while (*found && *found != '\\' && length < VALUE_LIMIT) {
        out[length++] = *found++;
    }
    out[length] = 0;
    return out;
}

void SendPacket(char *data, int size)
{
    XorKey(data, size);
    sendto(g_socket, data, size, 0, reinterpret_cast<const sockaddr *>(&g_validationServer), sizeof g_validationServer);
}

void SendReply(char *data, int size, const sockaddr *to)
{
    XorKey(data, size);
    sendto(g_socket, data, size, 0, to, sizeof(sockaddr_in));
}

void SendAuthRequest(const S_Product *product, S_Client *client, const char *challenge, const char *response)   // 0x00734a40
{
    client->state = CLIENT_WAITING;
    client->sessionKey = static_cast<int>((GetTickCount() ^ static_cast<uint32_t>(std::rand())) & 0x3FFF);
    client->sentTime = GetTickCount();
    client->tries = 1;
    char packet[0x200];
    const int size = std::sprintf(packet, "\\auth\\\\pid\\%d\\ch\\%s\\resp\\%s\\ip\\%d\\skey\\%d", product->id, challenge, response,
                                  client->ip, client->sessionKey);
    SendPacket(packet, size);
    // (Kept encrypted, for the retries.)
    client->request = static_cast<char *>(std::malloc(static_cast<size_t>(size)));
    std::memcpy(client->request, packet, static_cast<size_t>(size));
    client->requestSize = size;
}

void ResendRequest(S_Client *client)                    // 0x00734bc0
{
    client->sentTime = GetTickCount();
    ++client->tries;
    sendto(g_socket, client->request, client->requestSize, 0, reinterpret_cast<const sockaddr *>(&g_validationServer),
           sizeof g_validationServer);
}

void SendDisconnect(const S_Product *product, const S_Client *client)   // 0x00734970
{
    char packet[0x200];
    const int size = std::sprintf(packet, "\\disc\\\\pid\\%d\\cd\\%s\\ip\\%d", product->id, client->hkey, client->ip);
    SendPacket(packet, size);
}

// The validation server's answer ("uok", "unok").
void ProcessAuthAnswer(const char *packet, bool good)   // 0x007345b0
{
    const int sessionKey = std::atoi(ValueForKey(packet, "skey"));
    char hkey[HKEY_SIZE + 1];
    std::strncpy(hkey, ValueForKey(packet, "cd"), HKEY_SIZE);
    hkey[HKEY_SIZE] = 0;
    S_Client *client = FindClient(hkey, sessionKey);
    if (!client || client->sessionKey != sessionKey || client->state == CLIENT_DONE)
        return;
    if (good) {
        client->state = CLIENT_GOOD;
    } else {
        client->state = CLIENT_BAD;
        client->error = goastrdup(ValueForKey(packet, "errmsg"));
    }
}

// "ucount": the clients of a product (none named: the first).
void ProcessCount(const char *packet, const sockaddr *from)   // 0x00734750
{
    const char *id = ValueForKey(packet, "pid");
    const S_Product *product;
    if (*id == 0 && g_productCount > 0)
        product = &g_products[0];
    else
        product = FindProduct(std::atoi(id));
    int count = 0;
    if (product) {
        for (const S_Node *node = product->clients.next; node; node = node->next)
            ++count;
    }
    char reply[0x40];
    const int size = std::sprintf(reply, "\\ucount\\%d", count);
    SendReply(reply, size, from);
}

// "ison": whether the key is playing here.
void ProcessIsOn(const char *packet, const sockaddr *from)   // 0x00734830
{
    const int sessionKey = std::atoi(ValueForKey(packet, "skey"));
    const S_Client *client = FindClient(ValueForKey(packet, "cd"), -1);
    char reply[0x40];
    const int size = std::sprintf(reply, client && client->state == CLIENT_DONE ? "\\uon\\\\skey\\%d" : "\\uoff\\\\skey\\%d", sessionKey);
    SendReply(reply, size, from);
}

// A packet of the validation server's (';' on qr2's socket too).
void ProcessPacket(char *packet, int size, sockaddr *from)   // 0x007342a0
{
    XorKey(packet, size);
    if (packet[0] != '\\')
        return;
    // (Fix: no second backslash: not a command; the original copied from a null pointer's distance.)
    const char *end = std::strchr(packet + 1, '\\');
    if (!end)
        return;
    const auto length = static_cast<int>(end - packet);
    if (length >= 0x21)
        return;
    char command[0x21];
    std::memcpy(command, packet + 1, static_cast<size_t>(length - 1));
    command[length - 1] = 0;
    if (!command[0])
        return;
    if (std::strcmp(command, "uok") == 0)
        ProcessAuthAnswer(packet, true);
    else if (std::strcmp(command, "unok") == 0)
        ProcessAuthAnswer(packet, false);
    else if (std::strcmp(command, "ison") == 0)
        ProcessIsOn(packet, from);
    else if (std::strcmp(command, "ucount") == 0)
        ProcessCount(packet, from);
}

int AddProduct(int id)                                  // 0x00734530
{
    if (g_productCount >= MAX_PRODUCTS)
        return -1;
    S_Product &product = g_products[g_productCount++];
    product.id = id;
    product.clients = S_Node{nullptr, nullptr, nullptr};
    std::srand(GetTickCount());
    return 0;
}

void FreeNode(S_Node *node)
{
    std::free(node->client);
    std::free(node);
}

}  // namespace

void gcd_compute_response(const char *cdKey, const char *challenge, char *response)
{
    if (std::strlen(challenge) + 8 + std::strlen(cdKey) * 2 > MAX_RESPONSE_INPUT) {
        std::strcpy(response, "CD Key or challenge too long");
        return;
    }
    std::srand(static_cast<unsigned>(std::time(nullptr)) ^ 0x33333333u);
    const int high = std::rand();
    const uint32_t random = static_cast<uint32_t>(high) << 16 | static_cast<uint32_t>(std::rand());
    char randomText[16];
    std::sprintf(randomText, "%.8x", random);
    char text[0x200];
    std::sprintf(text, "%s%d%s", cdKey, static_cast<int>(random % 0xFFFF), challenge);
    MD5Digest(cdKey, static_cast<unsigned>(std::strlen(cdKey)), response);
    std::strcpy(response + 0x20, randomText);
    MD5Digest(text, static_cast<unsigned>(std::strlen(text)), response + 0x28);
}

int gcd_init(void *record, int gameId)
{
    S_Qr2Record *reporting = record ? static_cast<S_Qr2Record *>(record) : &g_qr2DefaultRecord;
    g_socket = reporting->socket;
    reporting->cdKeyProcess = ProcessPacket;
    g_validationServer = sockaddr_in{};
    g_validationServer.sin_family = AF_INET;
    g_validationServer.sin_port = htons(VALIDATION_PORT);
    g_validationServer.sin_addr = reporting->masterAddress.sin_addr;
    return AddProduct(gameId);
}

void gcd_authenticate_user(int gameId, int localId, uint32_t address, const char *challenge, const char *response,
                           GcdAuthCallback callback, void *instance)
{
    S_Product *product = FindProduct(gameId);
    if (!product)
        return;
    char hkey[HKEY_SIZE + 4] = {};
    std::strncpy(hkey, response, HKEY_SIZE);
    char *error = nullptr;
    if (std::strlen(response) < RESPONSE_LENGTH)
        error = goastrdup("Bad CD Key");
    for (const S_Node *node = product->clients.next; node; node = node->next) {
        if (std::strcmp(hkey, node->client->hkey) == 0)
            error = goastrdup("CD Key in use");         // (A bad key's text lost, as in the original.)
    }
    auto *client = static_cast<S_Client *>(std::malloc(sizeof(S_Client)));
    client->localId = localId;
    client->ip = address;
    client->instance = instance;
    client->authenticated = callback;
    client->error = nullptr;
    client->request = nullptr;
    std::strcpy(client->hkey, hkey);
    auto *node = static_cast<S_Node *>(std::malloc(sizeof(S_Node)));
    node->client = client;
    ListAdd(node, &product->clients);
    if (error) {
        client->state = CLIENT_BAD;
        client->error = error;
        return;
    }
    SendAuthRequest(product, client, challenge, response);
}

void gcd_disconnect_user(int gameId, int localId)
{
    S_Product *product = FindProduct(gameId);
    if (!product)
        return;
    S_Node *node = product->clients.next;
    while (node && node->client->localId != localId)
        node = node->next;
    if (!node)
        return;
    SendDisconnect(product, node->client);
    ListRemove(node);
    std::free(node->client->request);
    FreeNode(node);
}

void gcd_think()
{
    static char packet[0x400];                          // 0x00982118
    for (;;) {
        fd_set read;
        FD_ZERO(&read);
        FD_SET(g_socket, &read);
        timeval zero = {0, 0};
        const int ready = select(0x40, &read, nullptr, nullptr, &zero);
        if (ready == SOCKET_ERROR || ready == 0)
            break;
        sockaddr_in from;
        int fromSize = sizeof from;
        const int size = recvfrom(g_socket, packet, sizeof packet - 1, 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
        if (size != SOCKET_ERROR) {
            packet[size] = 0;
            ProcessPacket(packet, size, reinterpret_cast<sockaddr *>(&from));
        }
    }
    for (int i = 0; i < g_productCount; ++i) {
        S_Product &product = g_products[i];
        for (S_Node *node = product.clients.next; node; node = node->next) {
            S_Client *client = node->client;
            bool answer = client->state == CLIENT_GOOD;
            if (client->state == CLIENT_WAITING && GetTickCount() >= client->sentTime + RETRY_TIME) {
                // No answer after the tries: taken for good.
                if (client->tries > MAX_TRIES)
                    answer = true;
                else
                    ResendRequest(client);
            }
            if (answer) {
                client->authenticated(product.id, client->localId, 1,
                                      const_cast<char *>(client->state == CLIENT_GOOD ? "Validated" : "Validation Timeout"),
                                      client->instance);
                client->state = CLIENT_DONE;
                std::free(client->request);
                client->request = nullptr;
            } else if (client->state == CLIENT_BAD) {
                S_Node *previous = node->previous;
                ListRemove(node);
                client->authenticated(product.id, client->localId, 0, client->error ? client->error : const_cast<char *>(""),
                                      client->instance);
                std::free(client->request);
                std::free(client->error);
                FreeNode(node);
                node = previous;
            }
        }
    }
}

// The installer's key: its digits' 64-bit number modulo 0xFFFF, xored, is its last four.
bool ValidateCDKey(const char *key)
{
    char hex[16] = "0x";
    std::strncpy(hex + 2, key, 3);
    std::strncpy(hex + 5, key + 5, 4);
    std::strncpy(hex + 9, key + 0xB, 3);
    hex[12] = 0;
    long long number = 0;
    std::sscanf(hex, "%I64x", &number);
    std::strncpy(hex + 2, key + 0xF, 4);
    hex[6] = 0;
    unsigned check = 0;
    std::sscanf(hex, "%x", &check);
    return static_cast<uint16_t>(check) == static_cast<uint16_t>(static_cast<uint16_t>(number % 0xFFFF) ^ 0x1DEA);
}
