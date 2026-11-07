// The GameSpy SDK (part 10f: net/gamespy_*.cpp): containers, MD5, the CD key client against a validation server faked on
// loopback UDP. MinGW; build_gamespy.bat; run gamespy_test.exe ("gamespy: N checks, 0 failures").
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/pending.h"
#include "net/gamespy_internal.h"

void StopGameSpyQueries(uintptr_t *record);

#ifndef GCD_TEST_PORT
#define GCD_TEST_PORT 29910
#endif

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

std::vector<int> g_freed;
void FreeInt(void *element) { g_freed.push_back(*static_cast<int *>(element)); }
int CompareInt(const void *a, const void *b) { return *static_cast<const int *>(a) - *static_cast<const int *>(b); }
int HashInt(const void *element, int buckets) { return *static_cast<const int *>(element) % buckets; }

void TestArray()
{
    S_DArray *a = ArrayNew(sizeof(int), 2, FreeInt);
    CHECK(a->capacity == 2 && a->growBy == 2 && a->elementSize == 4 && ArrayLength(a) == 0);
    S_DArray *d = ArrayNew(sizeof(int), 0, nullptr);
    CHECK(d->capacity == 8 && d->growBy == 8);
    int v = 1;
    ArrayAppend(a, &v);
    v = 3;
    ArrayAppend(a, &v);
    v = 5;
    ArrayAppend(a, &v);                                 // (grown)
    CHECK(a->capacity == 4 && ArrayLength(a) == 3);
    v = 0;
    ArrayInsertAt(a, &v, 0);
    v = 2;
    ArrayInsertAt(a, &v, 2);                            // 0 1 2 3 5
    CHECK(a->capacity == 6 && ArrayLength(a) == 5);
    for (int i = 0; i < 4; ++i)
        CHECK(*static_cast<int *>(ArrayNth(a, i)) == i);
    CHECK(*static_cast<int *>(ArrayNth(a, 4)) == 5);
    ArrayAppend(nullptr, &v);
    // Search: linear from an index; binary.
    v = 3;
    CHECK(ArraySearch(a, &v, CompareInt, 0, false) == 3 && ArraySearch(a, &v, CompareInt, 3, false) == 3);
    CHECK(ArraySearch(a, &v, CompareInt, 4, false) == -1);
    CHECK(ArraySearch(a, &v, CompareInt, 0, true) == 3 && ArraySearch(a, &v, CompareInt, 2, true) == 3);
    v = 0;
    CHECK(ArraySearch(a, &v, CompareInt, 0, true) == 0);
    v = 5;
    CHECK(ArraySearch(a, &v, CompareInt, 0, true) == 4);
    v = 4;
    CHECK(ArraySearch(a, &v, CompareInt, 0, true) == -1 && ArraySearch(a, &v, CompareInt, 0, false) == -1);
    CHECK(ArraySearch(nullptr, &v, CompareInt, 0, false) == -1 && ArraySearch(d, &v, CompareInt, 0, false) == -1);
    // Replace frees; remove does not, delete does.
    v = 9;
    ArrayReplaceAt(a, &v, 1);
    CHECK(g_freed == std::vector<int>{1} && *static_cast<int *>(ArrayNth(a, 1)) == 9);
    g_freed.clear();
    ArrayRemoveAt(a, 1);
    CHECK(g_freed.empty() && ArrayLength(a) == 4 && *static_cast<int *>(ArrayNth(a, 1)) == 2 &&
          *static_cast<int *>(ArrayNth(a, 3)) == 5);
    ArrayDeleteAt(a, 3);
    CHECK(g_freed == std::vector<int>{5} && ArrayLength(a) == 3);
    {
        S_DArray *small = ArrayNew(sizeof(int), 1, nullptr);
        int e = 1;
        ArrayAppend(small, &e);
        CHECK(ArraySearch(small, &e, CompareInt, 0, false) == 0 && ArraySearch(small, &e, CompareInt, 0, true) == 0);
        e = 2;
        ArrayAppend(small, &e);
        e = 3;
        ArrayAppend(small, &e);
        ArrayRemoveAt(small, 1);                        // (the next to last: the last moves down)
        CHECK(ArrayLength(small) == 2 && *static_cast<int *>(ArrayNth(small, 1)) == 3);
        ArrayFree(small);
    }
    g_freed.clear();
    ArrayClear(a);
    CHECK(ArrayLength(a) == 0 && (g_freed == std::vector<int>{3, 2, 0}));
    g_freed.clear();
    v = 7;
    ArrayAppend(a, &v);
    v = 8;
    ArrayAppend(a, &v);
    ArrayFree(a);
    CHECK((g_freed == std::vector<int>{7, 8}));
    ArrayFree(d);
}

void TestTable()
{
    g_freed.clear();
    S_HashTable *t = TableNew2(sizeof(int), 3, 2, HashInt, CompareInt, FreeInt);
    CHECK(t->bucketCount == 3);
    int v = 4;
    TableEnter(t, &v);
    v = 7;
    TableEnter(t, &v);                                  // (the same bucket)
    v = 5;
    TableEnter(t, &v);
    CHECK(ArrayLength(t->buckets[1]) == 2 && ArrayLength(t->buckets[2]) == 1 && ArrayLength(t->buckets[0]) == 0);
    v = 7;
    auto *found = static_cast<int *>(TableLookup(t, &v));
    CHECK(found && *found == 7);
    v = 4;                                              // (the first in its chain)
    found = static_cast<int *>(TableLookup(t, &v));
    CHECK(found && *found == 4);
    v = 10;
    CHECK(TableLookup(t, &v) == nullptr);
    // Entered again: replaced (the old freed).
    v = 4;
    TableEnter(t, &v);
    CHECK(g_freed == std::vector<int>{4} && ArrayLength(t->buckets[1]) == 2);
    g_freed.clear();
    v = 7;
    CHECK(TableRemove(t, &v) && g_freed == std::vector<int>{7} && !TableRemove(t, &v));
    g_freed.clear();
    v = 4;                                              // (the first in its chain)
    CHECK(TableRemove(t, &v) && g_freed == std::vector<int>{4} && ArrayLength(t->buckets[1]) == 0);
    g_freed.clear();
    v = 6;                                              // (bucket 0 is freed too)
    TableEnter(t, &v);
    TableFree(t);
    CHECK(g_freed.size() == 2);
}

void TestMd5()
{
    char hex[33];
    const auto digest = [&](const char *text) {
        MD5Digest(text, static_cast<unsigned>(std::strlen(text)), hex);
        return std::string(hex);
    };
    CHECK(digest("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(digest("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(digest("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(digest("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(digest("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
    CHECK(digest("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") == "d174ab98d277d9f5a5611c2c9f419d9f");
    CHECK(digest("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
    // Long: across several blocks and a partial one (a million 'a's' known digest needs too long; 1000 of them).
    std::string a(1000, 'a');
    CHECK(digest(a.c_str()) == "cabe45dcc9ae5b66ba86600cca6b8ba8");
    // Around the blocks' and the padding's boundaries.
    static const struct { size_t size; const char *digest; } BOUNDARIES[] = {
        {55, "ef1772b6dff9a122358552954ad0df65"},  {56, "3b0c8ac703f828b04c6c197006d17218"},
        {57, "652b906d60af96844ebd21b674f35e93"},  {63, "b06521f39153d618550606be297466d5"},
        {64, "014842d480b571495a4a0363793f7367"},  {65, "c743a45e0d2e6a95cb859adae0248435"},
        {119, "8a7bd0732ed6a28ce75f6dabc90e1613"}, {120, "5f61c0ccad4cac44c75ff505e1f1e537"},
        {128, "e510683b3f5ffe4093d021808bc6ff70"}, {200, "887f30b43b2867f4a9accceee7d16e6c"}};
    for (const auto &boundary : BOUNDARIES)
        CHECK(digest(std::string(boundary.size, 'a').c_str()) == boundary.digest);
    CHECK(goastrdup(nullptr) == nullptr);
    char *copy = goastrdup("abc");
    CHECK(std::strcmp(copy, "abc") == 0);
    std::free(copy);
}

void TestResponse()
{
    char response[0x200];
    gcd_compute_response("KEY", "challenge", response);
    CHECK(std::strlen(response) == 0x48);
    char expected[33];
    MD5Digest("KEY", 3, expected);
    CHECK(std::strncmp(response, expected, 0x20) == 0);
    char random[9] = {};
    std::memcpy(random, response + 0x20, 8);
    const uint32_t r = std::strtoul(random, nullptr, 16);
    char text[128];
    std::sprintf(text, "KEY%dchallenge", static_cast<int>(r % 0xFFFF));
    MD5Digest(text, static_cast<unsigned>(std::strlen(text)), expected);
    CHECK(std::strcmp(response + 0x28, expected) == 0);
    // Too long: told.
    std::string longKey(250, 'k');
    gcd_compute_response(longKey.c_str(), "abc", response);       // 3 + 8 + 500 = 511: taken
    CHECK(std::strlen(response) == 0x48);
    gcd_compute_response(longKey.c_str(), "abcd", response);      // 512: too long
    CHECK(std::strcmp(response, "CD Key or challenge too long") == 0);
}

void TestValidate()
{
    // Key digits 0-2, 5-8, 11-13; the check 15-18.
    const long long number = 0x1234567ABCLL;
    const unsigned check = static_cast<unsigned>(number % 0xFFFF) ^ 0x1DEA;
    char checkText[8];
    std::sprintf(checkText, "%04x", check);
    std::string key = "123--4567--abc-";
    key += checkText;
    CHECK(ValidateCDKey(key.c_str()));
    key[0] = '2';
    CHECK(!ValidateCDKey(key.c_str()));
    key[0] = '1';
    key[6] = '9';
    CHECK(!ValidateCDKey(key.c_str()));
    key[6] = '5';
    key[12] = '9';
    CHECK(!ValidateCDKey(key.c_str()));
    key[12] = 'b';
    key[18] = key[18] == '0' ? '1' : '0';
    CHECK(!ValidateCDKey(key.c_str()));
    // The separators do not count.
    key = "123XX4567XXabcX";
    key += checkText;
    CHECK(ValidateCDKey(key.c_str()));
    // Digits that do not parse count as 0.
    CHECK(ValidateCDKey("zzzXXzzzzXXzzzX1dea"));
    CHECK(ValidateCDKey("000XX0001XXdeaXzzzz"));
}

// ---- the CD key client on loopback -------------------------------------------------------------------------------------

SOCKET g_server;                                        // the validation server (port 29910)
sockaddr_in g_clientAddress;
struct S_Answer {
    int gameId, localId, authenticated;
    std::string message;
    void *instance;
};
std::vector<S_Answer> g_answers;
void OnAnswer(int gameId, int localId, int authenticated, char *message, void *instance)
{
    g_answers.push_back({gameId, localId, authenticated, message, instance});
}
void Xor(char *data, int size)
{
    static const char key[] = "gamespy";
    for (int i = 0; i < size; ++i)
        data[i] = static_cast<char>(data[i] ^ key[i % 7]);
}
// The next packet the server got ("": none within 500 ms).
std::string ServerReceive()
{
    fd_set read;
    FD_ZERO(&read);
    FD_SET(g_server, &read);
    timeval wait = {0, 500000};
    if (select(0, &read, nullptr, nullptr, &wait) <= 0)
        return "";
    char packet[1024];
    sockaddr_in from;
    int fromSize = sizeof from;
    const int size = recvfrom(g_server, packet, sizeof packet, 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
    if (size <= 0)
        return "";
    Xor(packet, size);
    return std::string(packet, static_cast<size_t>(size));
}
void ServerSend(std::string text)
{
    Xor(&text[0], static_cast<int>(text.size()));
    sendto(g_server, text.data(), static_cast<int>(text.size()), 0, reinterpret_cast<const sockaddr *>(&g_clientAddress),
           sizeof g_clientAddress);
    Sleep(20);
}
std::string Value(const std::string &packet, const char *key)
{
    const std::string search = std::string("\\") + key + "\\";
    const size_t at = packet.find(search);
    if (at == std::string::npos)
        return "";
    const size_t from = at + search.size();
    return packet.substr(from, packet.find('\\', from) - from);
}

void TestCanReceive()
{
    SocketStartUp();
    SOCKET in = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in at = {};
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in, reinterpret_cast<sockaddr *>(&at), sizeof at);
    int size = sizeof at;
    getsockname(in, reinterpret_cast<sockaddr *>(&at), &size);
    const DWORD start = GetTickCount();
    CHECK(!CanReceiveOnSocket(in));
    CHECK(GetTickCount() - start < 500);                // (it does not wait)
    SOCKET out = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sendto(out, "x", 1, 0, reinterpret_cast<sockaddr *>(&at), sizeof at);
    Sleep(20);
    CHECK(CanReceiveOnSocket(in));
    closesocket(out);
    closesocket(in);
    CHECK(!CanReceiveOnSocket(in));                     // (an error: nothing to read)
}

void TestCdKey()
{
    WSADATA data;
    WSAStartup(0x101, &data);
    g_server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in at = {};
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = inet_addr("127.0.0.1");
    at.sin_port = htons(GCD_TEST_PORT);
    CHECK(bind(g_server, reinterpret_cast<sockaddr *>(&at), sizeof at) == 0);
    SOCKET client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    at.sin_port = 0;
    bind(client, reinterpret_cast<sockaddr *>(&at), sizeof at);
    int size = sizeof g_clientAddress;
    getsockname(client, reinterpret_cast<sockaddr *>(&g_clientAddress), &size);
    // The record: its socket and the master's address taken.
    S_Qr2Record record = {};
    record.socket = client;
    record.masterAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
    CHECK(gcd_init(&record, 750) == 0 && record.cdKeyProcess != nullptr);
    std::string response(0x48, 'r');
    int token = 0;
    // A key: asked of the server; "uok": told good once.
    gcd_authenticate_user(750, 1, 0x1020304, "CHAL", response.c_str(), OnAnswer, &token);
    std::string packet = ServerReceive();
    CHECK(packet.compare(0, 6, "\\auth\\") == 0 && Value(packet, "pid") == "750" && Value(packet, "ch") == "CHAL" &&
          Value(packet, "resp") == response && Value(packet, "ip") == "16909060");
    const std::string skey = Value(packet, "skey");
    CHECK(!skey.empty() && std::atoi(skey.c_str()) < 0x4000);
    const std::string hkey(0x20, 'r');
    // (Another key or session: nothing.)
    ServerSend("\\uok\\\\cd\\wrong\\skey\\" + skey);
    gcd_think();
    ServerSend("\\uok\\\\cd\\" + hkey + "\\skey\\" + std::to_string(std::atoi(skey.c_str()) + 1));
    gcd_think();
    ServerSend("not a command");
    ServerSend("\\nothing");
    gcd_think();
    CHECK(g_answers.empty());
    ServerSend("\\uok\\\\cd\\" + hkey + "\\skey\\" + skey);
    gcd_think();
    CHECK(g_answers.size() == 1 && g_answers[0].gameId == 750 && g_answers[0].localId == 1 && g_answers[0].authenticated == 1 &&
          g_answers[0].message == "Validated" && g_answers[0].instance == &token);
    gcd_think();
    CHECK(g_answers.size() == 1);
    // "ison", "ucount".
    ServerSend("\\ison\\\\cd\\" + hkey + "\\skey\\55");
    gcd_think();
    CHECK(ServerReceive() == "\\uon\\\\skey\\55");
    ServerSend("\\ison\\\\cd\\zzz\\skey\\56");
    gcd_think();
    CHECK(ServerReceive() == "\\uoff\\\\skey\\56");
    ServerSend("\\ucount\\");
    gcd_think();
    CHECK(ServerReceive() == "\\ucount\\1");
    ServerSend("\\ucount\\\\pid\\750");
    gcd_think();
    CHECK(ServerReceive() == "\\ucount\\1");
    ServerSend("\\ucount\\\\pid\\751");
    gcd_think();
    CHECK(ServerReceive() == "\\ucount\\0");
    // A key in use: refused at once, no packet; removed when told.
    g_answers.clear();
    gcd_authenticate_user(750, 2, 9, "CHAL", response.c_str(), OnAnswer, &token);
    gcd_think();
    CHECK(g_answers.size() == 1 && g_answers[0].authenticated == 0 && g_answers[0].message == "CD Key in use" &&
          g_answers[0].localId == 2);
    CHECK(ServerReceive().empty());
    ServerSend("\\ucount\\");
    gcd_think();
    CHECK(ServerReceive() == "\\ucount\\1");
    // A short response: bad; no such product: nothing.
    g_answers.clear();
    gcd_authenticate_user(750, 3, 9, "CHAL", "short", OnAnswer, &token);
    gcd_think();
    CHECK(g_answers.size() == 1 && g_answers[0].message == "Bad CD Key" && g_answers[0].authenticated == 0);
    g_answers.clear();
    gcd_authenticate_user(999, 3, 9, "CHAL", response.c_str(), OnAnswer, &token);
    gcd_think();
    CHECK(g_answers.empty() && ServerReceive().empty());
    gcd_disconnect_user(999, 1);
    gcd_disconnect_user(750, 77);
    CHECK(ServerReceive().empty());
    // "unok": the server's message; the client gone. (47 chars of response: 0x47 short by one is bad.)
    std::string other(0x48, 's');
    gcd_authenticate_user(750, 4, 9, "CHAL", other.c_str(), OnAnswer, &token);
    packet = ServerReceive();
    ServerSend("\\unok\\\\cd\\" + std::string(0x20, 's') + "\\skey\\" + Value(packet, "skey") + "\\errmsg\\Stolen");
    gcd_think();
    CHECK(g_answers.size() == 1 && g_answers[0].authenticated == 0 && g_answers[0].message == "Stolen");
    g_answers.clear();
    gcd_authenticate_user(750, 4, 9, "CHAL", other.c_str(), OnAnswer, &token);
    packet = ServerReceive();
    ServerSend("\\unok\\\\cd\\" + std::string(0x20, 's') + "\\skey\\" + Value(packet, "skey") + "\\errmsg\\" +
               std::string(300, 'm'));
    gcd_think();
    CHECK(g_answers.size() == 1 && g_answers[0].message == std::string(200, 'm'));
    std::string shortByOne(0x47, 't');
    g_answers.clear();
    gcd_authenticate_user(750, 5, 9, "CHAL", shortByOne.c_str(), OnAnswer, &token);
    gcd_think();
    CHECK(g_answers.size() == 1 && g_answers[0].message == "Bad CD Key");
    // Disconnected: told; another may use it.
    g_answers.clear();
    gcd_disconnect_user(750, 1);
    packet = ServerReceive();
    CHECK(packet.compare(0, 6, "\\disc\\") == 0 && Value(packet, "pid") == "750" && Value(packet, "cd") == hkey &&
          Value(packet, "ip") == "16909060");
    ServerSend("\\ucount\\");
    gcd_think();
    CHECK(ServerReceive() == "\\ucount\\0");
    // No answer: asked again after 2 s, twice in all more, then taken for good (a timeout).
    std::string third(0x48, 'u');
    gcd_authenticate_user(750, 6, 9, "CHAL", third.c_str(), OnAnswer, &token);
    const std::string first = ServerReceive();
    CHECK(!first.empty());
    gcd_think();
    CHECK(ServerReceive().empty() && g_answers.empty());
    Sleep(1600);
    gcd_think();
    CHECK(ServerReceive() == first);                    // (the same request again)
    gcd_think();
    CHECK(ServerReceive().empty());                     // (and waits again)
    Sleep(2100);
    gcd_think();
    CHECK(g_answers.empty());
    Sleep(2100);
    gcd_think();
    CHECK(g_answers.size() == 1 && g_answers[0].authenticated == 1 && g_answers[0].message == "Validation Timeout" &&
          g_answers[0].localId == 6);
    gcd_disconnect_user(750, 6);
    // Four products at most.
    CHECK(gcd_init(&record, 1) == 0 && gcd_init(&record, 2) == 0 && gcd_init(&record, 3) == 0);
    CHECK(gcd_init(&record, 4) == -1);
    closesocket(client);
    closesocket(g_server);
}

}  // namespace

#ifdef GAMESPY_QR2
#include "gamespy_qr2_tests.inc"
#include "gamespy_browser_tests.inc"
#else
S_Qr2Record g_qr2DefaultRecord;
#endif

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifndef GAMESPY_BROWSER_ONLY                            // (the browser's mutation runs)
    TestArray();
    TestTable();
    TestMd5();
    TestResponse();
    TestValidate();
    TestCanReceive();
    TestCdKey();
#ifdef GAMESPY_QR2
    TestQrBuffers();
    TestQrQueries();
    TestQrHeartbeats();
#endif
#endif
#ifdef GAMESPY_QR2
    TestBrowser();
#endif
    std::printf("gamespy: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
