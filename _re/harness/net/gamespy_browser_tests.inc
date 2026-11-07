// (Part 10f-iii.) GameSpy's server browser (net/gamespy_browser.cpp) on loopback: a TCP listener plays the master server (its
// cipher written apart, from the original's unused encrypting half, 0x00739910), UDP sockets the game servers. Included by
// gamespy_test.cpp.

#ifndef SB_TEST_PORT
#define SB_TEST_PORT 28910
#endif

namespace {

struct S_BrowserEvent {
    int reason;
    S_SbServer *server;
    std::string host;
};
std::vector<S_BrowserEvent> g_browserLog;
S_ServerBrowser *g_lastBrowser;
void *g_lastInstance;
std::string g_lastMessage;
void OnBrowser(S_ServerBrowser *browser, int reason, S_SbServer *server, void *instance)
{
    g_lastBrowser = browser;
    g_lastInstance = instance;
    if (reason == 5)
        g_lastMessage = browser->list.errorMessage;
    g_browserLog.push_back({reason, server, server ? SBServerGetStringValue(server, "hostname", "") : ""});
}
int CountReason(int reason)
{
    int count = 0;
    for (const S_BrowserEvent &event : g_browserLog)
        count += event.reason == reason;
    return count;
}

// The master's end of the cipher.
struct S_TestCipher {
    uint8_t c[256];
    uint8_t rotor, ratchet, avalanche, lastPlain, lastCipher;
    void Init(const uint8_t *key, int keySize)
    {
        for (int i = 0; i < 256; ++i)
            c[i] = static_cast<uint8_t>(i);
        uint8_t sum = 0;
        int keyAt = 0;
        for (int i = 255; i >= 0; --i) {
            uint8_t other = 0;
            if (i != 0) {
                unsigned mask = 1;
                while (mask < static_cast<unsigned>(i))
                    mask = mask * 2 + 1;
                int tries = 0;
                unsigned pick;
                do {
                    sum = static_cast<uint8_t>(c[sum] + key[keyAt]);
                    if (++keyAt >= keySize) {
                        keyAt = 0;
                        sum = static_cast<uint8_t>(sum + keySize);
                    }
                    pick = sum & mask;
                    if (++tries > 11)
                        pick %= static_cast<unsigned>(i);
                } while (pick > static_cast<unsigned>(i));
                other = static_cast<uint8_t>(pick);
            }
            std::swap(c[i], c[other]);
        }
        rotor = c[1];
        ratchet = c[3];
        avalanche = c[5];
        lastPlain = c[7];
        lastCipher = c[sum];
    }
    uint8_t Encrypt(uint8_t in)
    {
        ratchet = static_cast<uint8_t>(ratchet + c[rotor]);
        rotor = static_cast<uint8_t>(rotor + 1);
        const uint8_t t = c[lastCipher];
        c[lastCipher] = c[ratchet];
        c[ratchet] = c[lastPlain];
        c[lastPlain] = c[rotor];
        c[rotor] = t;
        avalanche = static_cast<uint8_t>(avalanche + c[t]);
        const uint8_t out = static_cast<uint8_t>(c[c[(c[lastPlain] + c[ratchet] + c[lastCipher]) & 255]] ^
                                                 c[(c[avalanche] + c[rotor]) & 255] ^ in);
        lastPlain = in;
        lastCipher = out;
        return out;
    }
};

std::string B(const char *text, size_t size) { return std::string(text, size); }
std::string Be16(unsigned value)
{
    const char bytes[2] = {static_cast<char>(value >> 8), static_cast<char>(value)};
    return std::string(bytes, 2);
}
std::string Raw32(uint32_t value) { return std::string(reinterpret_cast<const char *>(&value), 4); }
std::string Raw16(u_short value) { return std::string(reinterpret_cast<const char *>(&value), 2); }
std::string Z(const std::string &text) { return text + std::string(1, '\0'); }

struct S_FakeMaster {
    SOCKET listener = INVALID_SOCKET, client = INVALID_SOCKET;
    S_TestCipher cipher;
    std::string request;

    void Listen()
    {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in at = {};
        at.sin_family = AF_INET;
        at.sin_addr.s_addr = inet_addr("127.0.0.1");
        at.sin_port = htons(SB_TEST_PORT);
        const BOOL reuse = TRUE;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof reuse);
        CHECK(bind(listener, reinterpret_cast<sockaddr *>(&at), sizeof at) == 0);
        listen(listener, 4);
    }
    // The client's request; its bytes kept.
    void Accept()
    {
        client = accept(listener, nullptr, nullptr);
        char data[1024];
        const int size = recv(client, data, sizeof data, 0);
        request = size > 0 ? std::string(data, static_cast<size_t>(size)) : "";
        const BOOL noDelay = TRUE;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof noDelay);
    }
    // Our key from the request's challenge (+ the secret key and the server's half), and the header that gives the client it.
    void SendCryptHeader(const char *secret) { Send(MakeCryptHeader(secret)); }
    std::string MakeCryptHeader(const char *secret)
    {
        const std::string serverKey("\x11\x83\x35\xC7\x59\x2B", 6);
        const size_t at = 2 + 3 + 4 + 4 + 4;            // (the size, 3 bytes, the version, "hd2" twice)
        uint8_t challenge[8];
        std::memcpy(challenge, request.data() + at, 8);
        const int secretSize = static_cast<int>(std::strlen(secret));
        for (int i = 0; i < static_cast<int>(serverKey.size()); ++i) {
            challenge[(secret[i % secretSize] * i) % 8] ^=
                static_cast<uint8_t>(challenge[i % 8] ^ static_cast<uint8_t>(serverKey[static_cast<size_t>(i)]));
        }
        cipher.Init(challenge, 8);
        std::string header;
        header += static_cast<char>(3 ^ 0xEC);          // (3 + 2: the size to the key's)
        header += "abc";
        header += static_cast<char>(serverKey.size() ^ 0xEA);
        header += serverKey;
        return header;
    }
    void Send(const std::string &bytes)
    {
        send(client, bytes.data(), static_cast<int>(bytes.size()), 0);
        Sleep(15);
    }
    void SendEncrypted(const std::string &plain)
    {
        std::string out = plain;
        for (char &ch : out)
            ch = static_cast<char>(cipher.Encrypt(static_cast<uint8_t>(ch)));
        Send(out);
    }
    void CloseClient()
    {
        if (client != INVALID_SOCKET)
            closesocket(client);
        client = INVALID_SOCKET;
    }
    void Close()
    {
        CloseClient();
        closesocket(listener);
    }
};

struct S_FakeServer {
    SOCKET socket_;
    u_short port;
    sockaddr_in from;
    S_FakeServer()
    {
        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in at = {};
        at.sin_family = AF_INET;
        at.sin_addr.s_addr = inet_addr("127.0.0.1");
        bind(socket_, reinterpret_cast<sockaddr *>(&at), sizeof at);
        int size = sizeof at;
        getsockname(socket_, reinterpret_cast<sockaddr *>(&at), &size);
        port = at.sin_port;
    }
    ~S_FakeServer() { closesocket(socket_); }
    std::string Receive()
    {
        fd_set read;
        FD_ZERO(&read);
        FD_SET(socket_, &read);
        timeval wait = {0, 30000};
        if (select(0, &read, nullptr, nullptr, &wait) <= 0)
            return "";
        char data[512];
        int size = sizeof from;
        const int got = recvfrom(socket_, data, sizeof data, 0, reinterpret_cast<sockaddr *>(&from), &size);
        return got > 0 ? std::string(data, static_cast<size_t>(got)) : "";
    }
    void Reply(const std::string &bytes)
    {
        sendto(socket_, bytes.data(), static_cast<int>(bytes.size()), 0, reinterpret_cast<const sockaddr *>(&from), sizeof from);
        Sleep(10);
    }
};

const uint32_t LOOPBACK = inet_addr("127.0.0.1");
void Think(S_ServerBrowser *browser, int times = 1)
{
    for (int i = 0; i < times; ++i)
        ServerBrowserThink(browser);
}
S_SbServer *Listed(S_ServerBrowser *browser, int index)
{
    return *static_cast<S_SbServer **>(ArrayNth(browser->list.servers, index));
}
std::string Value(S_SbServer *server, const char *key) { return SBServerGetStringValue(server, key, "<none>"); }

const uint8_t FIELDS[] = {QR2_HOSTNAME, QR2_NUMPLAYERS};

// The public IP, the default port; the keys; the popular values.
std::string ListHeader(uint32_t publicIp, u_short defaultPort)
{
    std::string h = Raw32(publicIp) + Raw16(defaultPort);
    h += static_cast<char>(3);
    h += B("\0", 1) + Z("hostname");
    h += B("\1", 1) + Z("numplayers");
    h += B("\2", 1) + Z("hostport");
    h += static_cast<char>(2);
    h += Z("PopZero") + Z("PopOne");
    return h;
}
const std::string LIST_END = B("\0\xFF\xFF\xFF\xFF", 5);

void TestBrowserMainList()
{
    g_browserLog.clear();
    S_FakeMaster master;
    master.Listen();
    int instance = 0;
    S_ServerBrowser *browser = ServerBrowserNew("hd2", "hd2", "sK8pQ9", 7, 2, 1, OnBrowser, &instance);
    CHECK(browser && browser->list.state == 1 && browser->engine.maxUpdates == 2 && browser->engine.queryVersion == 1);
    CHECK(browser->list.socket == INVALID_SOCKET && browser->engine.socket != INVALID_SOCKET);
    CHECK(ArrayLength(browser->list.servers) == 0 && browser->list.expected == -1);
    // The request, the challenge from rand().
    std::srand(4242);
    ServerBrowserUpdate(browser, 1, 1, FIELDS, 2, "gametype='x'");
    master.Accept();
    std::string expected = B("\0\1\3", 3) + Raw32(7) + Z("hd2") + Z("hd2");
    std::string challenge;
    std::srand(4242);
    {
        unsigned char c[8];
        c[0] = static_cast<unsigned char>(std::rand() % 0x5D + 0x21);
        unsigned parity = 0;
        for (int i = 1; i < 8; ++i) {
            parity ^= ((c[i - 1] ^ c[0]) & 1u) ^ (i & 1u) ^ static_cast<unsigned>(static_cast<signed char>(c[i - 1]) < static_cast<signed char>(c[0])) ^
                      static_cast<unsigned>(static_cast<signed char>(c[0]) < 'O');
            const int r = std::rand() % 0x5D;
            unsigned char ch = static_cast<unsigned char>(r + 0x21);
            if ((parity == 0) == ((ch & 1) == 1))
                ch = static_cast<unsigned char>(r + 0x22);
            c[i] = ch;
        }
        challenge.assign(reinterpret_cast<const char *>(c), 8);
    }
    expected += challenge + Z("gametype='x'") + Z("\\hostname\\numplayers") + B("\0\0\0\0", 4);
    CHECK(master.request == Be16(static_cast<unsigned>(expected.size() + 2)) + expected);
    CHECK(browser->list.state == 3 && browser->list.parseState == 0 && browser->list.inBuffer && browser->list.inBufferSize == 0);
    CHECK(browser->engine.keyCount == 2 && browser->engine.keys[0] == QR2_HOSTNAME && browser->engine.keys[1] == QR2_NUMPLAYERS);
    CHECK(browser->disconnectOnComplete == 1);
    Think(browser);                                     // (nothing yet)
    CHECK(browser->list.state == 3);

    S_FakeServer q1, q2, q3, priv;
    const uint32_t publicIp = inet_addr("127.0.0.2");
    // A: default port, values by popular index; B: its own port, an inline value; C, D, E: no values (to query; two at most at
    // a time); F: the full keys (with both kinds the original's check of what has arrived reads the wrong part: not sent); G: behind our address, private address and port, ICMP address.
    std::string a = B("\x40", 1) + Raw32(LOOPBACK) + B("\0", 1) + B("\5", 1) + Be16(1234);
    std::string b = B("\x50", 1) + Raw32(LOOPBACK) + Raw16(1001) + B("\xFF", 1) + Z("Inline") + B("\xC8", 1) + Be16(0x8001);
    std::string c = B("\x10", 1) + Raw32(LOOPBACK) + Raw16(q1.port);
    std::string d = B("\x10", 1) + Raw32(LOOPBACK) + Raw16(q2.port);
    std::string e = B("\x10", 1) + Raw32(LOOPBACK) + Raw16(q3.port);
    std::string f = B("\x90", 1) + Raw32(LOOPBACK) + Raw16(1005) + Z("extra") + Z("value") + B("\0", 1);
    std::string g = B("\x3A", 1) + Raw32(publicIp) + Raw16(1006) + Raw32(LOOPBACK) + Raw16(priv.port) + Raw32(0x01020304);
    // In pieces, broken at the worst places.
    master.SendCryptHeader("sK8pQ9");
    Think(browser);
    CHECK(browser->list.parseState == 1);
    const std::string body = ListHeader(publicIp, 2000) + a + b + c + d + e + f + g + LIST_END + B("\x77\x66", 2);
    const size_t cuts[] = {3, 6, 9, 20, 44, 58, 60, 65, 66, 70, 75, 95, 100, 120, 135, 150};
    size_t from = 0;
    for (size_t cut : cuts) {
        if (cut >= body.size())
            break;
        master.SendEncrypted(body.substr(from, cut - from));
        from = cut;
        Think(browser);
    }
    master.SendEncrypted(body.substr(from));
    Think(browser, 2);
    // The list's end: disconnected as asked, the servers kept.
    CHECK(browser->list.state == 1 && browser->list.socket == INVALID_SOCKET && browser->list.inBuffer == nullptr);
    CHECK(browser->list.keyList == nullptr && browser->list.popularCount == 0 && browser->list.expected == -1);
    CHECK(browser->list.parseState == 5);
    CHECK(browser->list.publicIp == publicIp && browser->engine.publicIp == publicIp && browser->list.defaultPort == 2000);
    CHECK(ArrayLength(browser->list.servers) == 7 && CountReason(0) == 7);
    S_SbServer *const A = Listed(browser, 0), *const B_ = Listed(browser, 1), *const C = Listed(browser, 2),
                      *const D = Listed(browser, 3), *const E = Listed(browser, 4), *const F = Listed(browser, 5), *const G = Listed(browser, 6);
    CHECK(g_lastBrowser == browser && g_lastInstance == &instance);
    CHECK(A->publicIp == LOOPBACK && A->publicPort == 2000 && A->state == 1 && A->flags == 0x40);
    CHECK(Value(A, "hostname") == "PopZero" && Value(A, "numplayers") == "5" && Value(A, "hostport") == "1234");
    CHECK(SBServerGetPublicInetAddress(A) == LOOPBACK);
    CHECK(B_->publicPort == 1001 && Value(B_, "hostname") == "Inline" && Value(B_, "numplayers") == "200" && Value(B_, "hostport") == "32769");
    CHECK(C->publicPort == q1.port && C->state != 0 && Value(C, "hostname") == "<none>");
    CHECK(F->state == 2 && F->publicPort == 1005 && Value(F, "extra") == "value");
    CHECK(G->flags == 0x3A && G->publicIp == publicIp && G->publicPort == 1006 && G->privateIp == LOOPBACK &&
          G->privatePort == priv.port && G->icmpIp == 0x01020304u);
    // The queries: basic (the keys) for two; the rest wait.
    CHECK(browser->engine.querying.count == 2 && browser->engine.pending.count == 2 && browser->engine.querying.first == C && browser->engine.querying.last == D);
    CHECK(browser->engine.pending.first == E && browser->engine.pending.last == G && (C->state & 4) && (E->state & 4));
    std::string query = q1.Receive();
    CHECK(query.size() == 12 && query.compare(0, 3, B("\xFE\xFD\0", 3)) == 0 && query.compare(7, 5, B("\x02\x01\x08\0\0", 5)) == 0);
    // Answered: its values, ping, a place for the next. Not for us (a foreign address or type): nothing.
    g_browserLog.clear();
    q2.Receive();
    S_FakeServer stranger;
    stranger.from = q1.from;
    stranger.Reply(B("\0abcd", 5) + Z("Nope") + Z("9"));
    Think(browser);
    CHECK(g_browserLog.empty() && Value(C, "hostname") == "<none>");
    C->updateTime -= 500;
    q1.Reply(B("\x01" "abcd", 5) + Z("Nope") + Z("9"));
    Think(browser);
    CHECK(g_browserLog.empty() && browser->engine.querying.count == 2);
    q1.Reply(B("\0abcd", 5) + Z("Server C") + Z("11"));
    Think(browser);
    CHECK(g_browserLog.size() == 1 && g_browserLog[0].reason == 1 && g_browserLog[0].server == C && g_browserLog[0].host == "Server C");
    CHECK(Value(C, "numplayers") == "11" && C->state == 1);
    CHECK(SBServerGetPing(C) >= 500 && SBServerGetPing(C) < 2000 && SBServerGetIntValue(C, "ping", -1) == static_cast<int>(SBServerGetPing(C)));
    CHECK(SBServerGetIntValue(C, "numplayers", -1) == 11 && SBServerGetIntValue(C, "absent", -1) == -1 && SBServerGetIntValue(C, "hostname", -1) == 0);
    CHECK(browser->engine.querying.first == D && browser->engine.querying.last == E && browser->engine.pending.count == 1 && browser->engine.pending.first == G);
    CHECK(q3.Receive().size() == 12);
    // A short answer: the values that came.
    q2.Reply(B("\0abcd", 5) + Z("Server D") + "partial");
    Think(browser);
    CHECK(Value(D, "hostname") == "Server D" && Value(D, "numplayers") == "<none>" && D->state == 1);
    // G behind our address: its private one, its reply from there.
    CHECK(browser->engine.querying.last == G && browser->engine.pending.count == 0);
    CHECK(priv.Receive().size() == 12);
    priv.Reply(B("\0abcd", 5) + Z("Server G") + Z("3"));
    Think(browser);
    CHECK(Value(G, "hostname") == "Server G" && browser->engine.querying.count == 1);
    // No answer in time: failed (the time-out's marks in its flags); then idle.
    g_browserLog.clear();
    E->updateTime = GetTickCount() - 2000;
    Think(browser);
    CHECK(g_browserLog.empty());
    E->updateTime = GetTickCount() - 2600;
    const uint8_t stateBefore = E->state;
    Think(browser);
    CHECK(g_browserLog.size() == 2 && g_browserLog[0].reason == 2 && g_browserLog[0].server == E && g_browserLog[1].reason == 4 && g_browserLog[1].server == nullptr);
    CHECK((E->flags & 0x10) && E->state == stateBefore && browser->engine.querying.count == 0 && browser->engine.querying.first == nullptr);
    g_browserLog.clear();
    Think(browser);
    CHECK(g_browserLog.empty());                        // (idle told once)
    // Cleared: nothing left.
    ServerBrowserClear(browser);
    CHECK(ArrayLength(browser->list.servers) == 0 && browser->list.deadList == nullptr && browser->engine.pending.count == 0);
    master.Close();
}

// Connected on: the master's later messages.
void TestBrowserMessages()
{
    g_browserLog.clear();
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *browser = ServerBrowserNew("hd2", "hd2", "sK8pQ9", 1, 8, 1, OnBrowser, nullptr);
    browser->dontUpdate = 1;                            // (no queries)
    ServerBrowserUpdate(browser, 1, 0, FIELDS, 2, nullptr);
    master.Accept();
    master.SendCryptHeader("sK8pQ9");
    S_FakeServer peer;
    master.SendEncrypted(ListHeader(LOOPBACK, 3000) + B("\x40", 1) + Raw32(LOOPBACK) + B("\0\5", 2) + Be16(1) + LIST_END);
    Think(browser, 2);
    CHECK(browser->list.state == 2 && browser->list.parseState == 5 && browser->list.inBuffer && browser->list.inBufferSize == 0);
    CHECK(browser->engine.querying.count == 0 && browser->list.keyList);
    CHECK(ArrayLength(browser->list.servers) == 1);
    g_browserLog.clear();
    const auto message = [](int type, const std::string &data) {
        return Be16(static_cast<unsigned>(data.size() + 3)) + std::string(1, static_cast<char>(type)) + data;
    };
    // A new key list (the old released; a type shown).
    master.SendEncrypted(message(1, B("\2\0", 2) + Z("mapname") + B("\2", 1) + Z("gamemode")));
    Think(browser);
    CHECK(ArrayLength(browser->list.keyList) == 2);
    CHECK(std::strcmp(static_cast<const char **>(ArrayNth(browser->list.keyList, 0))[0], "mapname") == 0);
    // A new server (added + updated; no popular index here), and values for the old one with all their kinds.
    std::string update = B("\x50", 1) + Raw32(LOOPBACK) + Raw16(4000) + Z("Island") + Be16(3);
    master.SendEncrypted(message(2, update));
    Think(browser);
    CHECK(ArrayLength(browser->list.servers) == 2 && g_browserLog.size() == 2 && g_browserLog[0].reason == 0 && g_browserLog[1].reason == 1);
    S_SbServer *const added = Listed(browser, 1);
    CHECK(added->publicPort == 4000 && Value(added, "mapname") == "Island" && Value(added, "gamemode") == "3");
    g_browserLog.clear();
    master.SendEncrypted(message(2, B("\x80", 1) + Raw32(LOOPBACK) + Z("k1") + Z("v1") + Z("k2") + Z("v2") + B("\0", 1)));
    Think(browser);
    S_SbServer *const old = Listed(browser, 0);
    CHECK(ArrayLength(browser->list.servers) == 2 && g_browserLog.size() == 1 && g_browserLog[0].reason == 1 && g_browserLog[0].server == old);
    CHECK(Value(old, "k1") == "v1" && Value(old, "k2") == "v2" && old->state == 3 && old->publicPort == 3000);
    // Both kinds.
    master.SendEncrypted(message(2, B("\xD0", 1) + Raw32(LOOPBACK) + Raw16(3000) + Z("Both") + Be16(9) + Z("k3") + Z("v3") + B("\0", 1)));
    Think(browser);
    CHECK(Value(old, "mapname") == "Both" && Value(old, "gamemode") == "9" && Value(old, "k3") == "v3" && old->state == 3);
    // No values: failed (the browser's).
    g_browserLog.clear();
    old->state = 0;
    master.SendEncrypted(message(2, B("\0", 1) + Raw32(LOOPBACK)));
    Think(browser);
    CHECK(g_browserLog.size() == 1 && g_browserLog[0].reason == 2);
    // The echo: sent back as it is (encrypted? no: its plain bytes after the client's decryption).
    const std::string echo = message(3, "ping");
    master.SendEncrypted(echo);
    Think(browser);
    char back[32];
    Sleep(10);
    const int backSize = recv(master.client, back, sizeof back, 0);
    CHECK(std::string(back, static_cast<size_t>(backSize > 0 ? backSize : 0)) == echo);
    // An unknown type: skipped. Removed: told, gone (freed at the next think); unknown address: nothing. Two in one read.
    g_browserLog.clear();
    master.SendEncrypted(message(9, "zz") + message(4, Raw32(LOOPBACK) + Raw16(4000)) + message(4, Raw32(LOOPBACK) + Raw16(4001)));
    Think(browser);
    CHECK(g_browserLog.size() == 1 && g_browserLog[0].reason == 3 && g_browserLog[0].server == added);
    CHECK(ArrayLength(browser->list.servers) == 1 && browser->list.deadList == added);
    Think(browser);
    CHECK(browser->list.deadList == nullptr);
    // A message split over two reads.
    g_browserLog.clear();
    const std::string split = message(2, B("\x50", 1) + Raw32(LOOPBACK) + Raw16(4100) + Z("Hill") + Be16(1));
    master.SendEncrypted(split.substr(0, 2));
    Think(browser);
    CHECK(browser->list.inBufferSize == 2 && g_browserLog.empty());
    master.SendEncrypted(split.substr(2, 4));
    Think(browser);
    CHECK(browser->list.inBufferSize == 6 && g_browserLog.empty());
    master.SendEncrypted(split.substr(6));
    Think(browser);
    CHECK(browser->list.inBufferSize == 0 && g_browserLog.size() == 2);
    // Short messages: an error, disconnected.
    master.SendEncrypted(message(4, "abc"));
    Think(browser);
    CHECK(browser->list.state == 1 && browser->list.socket == INVALID_SOCKET);
    master.CloseClient();
    master.Close();
    ServerBrowserClear(browser);
}

void BeginListed(S_ServerBrowser *browser, S_FakeMaster &master)
{
    ServerBrowserUpdate(browser, 1, 0, FIELDS, 2, nullptr);
    master.Accept();
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(3000) + B("\0\0", 2) + LIST_END);
    Think(browser, 2);
    CHECK(browser->list.state == 2);
}

void TestBrowserErrors()
{
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *browser = ServerBrowserNew("hd2", "hd2", "k3y", 1, 8, 1, OnBrowser, nullptr);
    // Too long a filter: not sent. A limit that fits does.
    const std::string longFilter(0x101, 'f');
    ServerBrowserUpdate(browser, 1, 0, FIELDS, 2, longFilter.c_str());
    CHECK(browser->list.socket == INVALID_SOCKET && browser->list.state == 1);
    const std::string fits(0x100, 'f');
    ServerBrowserUpdate(browser, 1, 0, FIELDS, 2, fits.c_str());
    master.Accept();
    CHECK(browser->list.state == 3 && master.request.find(Z(fits)) != std::string::npos);
    // The error message in place of the default port.
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + B("\xFF\xFF", 2) + "Go aw");
    Think(browser, 2);
    CHECK(CountReason(5) == 0 && browser->list.parseState == 1);
    g_browserLog.clear();
    master.SendEncrypted(Z("ay"));
    Think(browser);
    CHECK(g_browserLog.size() == 1 && g_browserLog[0].reason == 5 && g_browserLog[0].server == nullptr && g_lastMessage == "Go away");
    // (Its text stays in the buffer and is read as a message: too long, the original's way out.)
    CHECK(browser->list.parseState == 5 && browser->list.state == 1 && browser->list.socket == INVALID_SOCKET);
    master.CloseClient();
    // The bad messages after: below their header (the fix), too long. Closed by the master: disconnected.
    BeginListed(browser, master);
    master.SendEncrypted(Be16(0) + "x");
    Think(browser);
    CHECK(browser->list.state == 1 && browser->list.socket == INVALID_SOCKET);
    master.CloseClient();
    BeginListed(browser, master);
    master.SendEncrypted(Be16(0x1001) + "x");
    Think(browser);
    CHECK(browser->list.state == 1);
    master.CloseClient();
    BeginListed(browser, master);
    master.SendEncrypted(Be16(0x1000) + "x");
    Think(browser);
    CHECK(browser->list.state == 2);                    // (waits for the rest)
    master.CloseClient();
    Sleep(10);
    Think(browser);
    CHECK(browser->list.state == 1 && browser->list.socket == INVALID_SOCKET);
    // No list asked for: over after the header.
    ServerBrowserUpdate(browser, 1, 0, FIELDS, 2, nullptr);
    master.Accept();
    browser->list.options = 2;
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(3000));
    Think(browser, 2);
    CHECK(browser->list.state == 2 && browser->list.parseState == 5 && browser->list.keyList == nullptr);
    master.CloseClient();
    Sleep(10);
    Think(browser);
    // Over 20 keys: 20 kept; the text cut at 255.
    uint8_t many[40];
    std::memset(many, QR2_HOSTNAME, sizeof many);
    ServerBrowserUpdate(browser, 1, 0, many, 40, nullptr);
    CHECK(browser->engine.keyCount == 20);
    master.Accept();
    size_t names = 0;
    for (size_t at = master.request.find("\\hostname"); at != std::string::npos; at = master.request.find("\\hostname", at + 1))
        ++names;
    CHECK(names == 28);                                 // (9 bytes each: 28 fit 255)
    master.CloseClient();
    Sleep(10);
    Think(browser);
    // Not reachable: a refused connection; an unknown host. (The real name's form: "<game>.ms<hash>.gamespy.com".)
    master.Close();
    g_sbMasterOverride = "127.0.0.1";
    ServerBrowserUpdate(browser, 1, 0, FIELDS, 2, nullptr);
    CHECK(browser->list.socket == INVALID_SOCKET && browser->list.state == 1);
    ServerBrowserClear(browser);
}

// A query of everything (no keys asked for): the values and the players' and the teams' tables. The old query's form.
void TestBrowserFullReplies()
{
    g_browserLog.clear();
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *browser = ServerBrowserNew("hd2", "hd2", "k3y", 1, 8, 1, OnBrowser, nullptr);
    ServerBrowserUpdate(browser, 1, 1, FIELDS, 0, nullptr);
    CHECK(browser->engine.keyCount == 0);
    master.Accept();
    CHECK(master.request.find(B("\0\0\0\0\0", 5)) != std::string::npos);
    S_FakeServer q;
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(3000) + B("\0\0", 2) + B("\x10", 1) + Raw32(LOOPBACK) + Raw16(q.port) + LIST_END);
    Think(browser, 2);
    CHECK(q.Receive().substr(7) == B("\xFF\xFF\xFF", 3));
    S_SbServer *const server = Listed(browser, 0);
    CHECK((server->state & 8) && !(server->state & 4));
    std::string reply = B("\0abcd", 5) + Z("hostname") + Z("Full") + Z("mapname") + Z("") + B("\0", 1);
    reply += Be16(2) + Z("player_") + Z("score_") + B("\0", 1) + Z("Ann") + Z("5") + Z("Bob") + Z("6");
    reply += Be16(1) + Z("team_t") + B("\0", 1) + Z("Axis");
    q.Reply(reply);
    g_browserLog.clear();
    Think(browser);
    CHECK(g_browserLog.size() >= 1 && g_browserLog[0].reason == 1 && server->state == 3);
    CHECK(Value(server, "hostname") == "Full" && Value(server, "mapname") == "");
    CHECK(Value(server, "player_0") == "Ann" && Value(server, "score_0") == "5" && Value(server, "player_1") == "Bob" && Value(server, "score_1") == "6");
    CHECK(Value(server, "team_t0") == "Axis");
    // Key names ignore case; a value replaced.
    CHECK(Value(server, "HOSTNAME") == "Full");
    ServerBrowserClear(browser);
    master.CloseClient();

    // The old form.
    S_ServerBrowser *old = ServerBrowserNew("hd2", "hd2", "k3y", 1, 8, 0, OnBrowser, nullptr);
    ServerBrowserUpdate(old, 1, 1, FIELDS, 2, nullptr);
    master.Accept();
    S_FakeServer oq, oq2;
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(3000) + B("\0\0", 2) + B("\x10", 1) + Raw32(LOOPBACK) + Raw16(oq.port) + LIST_END);
    Think(old, 2);
    CHECK(oq.Receive() == "\\basic\\\\info\\");
    S_SbServer *const oldServer = Listed(old, 0);
    g_browserLog.clear();
    oq.Reply("\\hostname\\Old One\\empty\\\\mapname\\Hill");
    Think(old);
    CHECK(g_browserLog.empty() && old->engine.querying.count == 1 && Value(oldServer, "hostname") == "Old One");
    oq.Reply("\\numplayers\\4\\final\\\\queryid\\1.1");
    Think(old);
    CHECK(g_browserLog.size() >= 1 && g_browserLog[0].reason == 1 && oldServer->state == 1 && old->engine.querying.count == 0);
    CHECK(Value(oldServer, "numplayers") == "4" && Value(oldServer, "queryid") == "<none>" && Value(oldServer, "final") == "<none>");
    ServerBrowserClear(old);
    master.CloseClient();
    ServerBrowserUpdate(old, 1, 1, FIELDS, 0, nullptr);
    master.Accept();
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(3000) + B("\0\0", 2) + B("\x10", 1) + Raw32(LOOPBACK) + Raw16(oq2.port) + LIST_END);
    Think(old, 2);
    CHECK(oq2.Receive() == "\\status\\");
    S_SbServer *const status = Listed(old, 0);
    oq2.Reply("\\hostname\\S\\final\\");
    Think(old);
    CHECK(status->state == 2);
    ServerBrowserClear(old);
    master.CloseClient();
    master.Close();
}

// Started by the dead LAN update only: its socket's answers, the time up.
void TestBrowserLan()
{
    g_browserLog.clear();
    S_ServerBrowser *browser = ServerBrowserNew("hd2", "hd2", "k3y", 1, 8, 1, OnBrowser, nullptr);
    S_FakeServer own, a;
    browser->list.socket = own.socket_;
    own.socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);    // (its own closes the list's)
    browser->list.state = 0;
    browser->list.lanStartTime = GetTickCount();
    browser->dontUpdate = 1;
    Think(browser);
    CHECK(browser->list.state == 0 && g_browserLog.empty());
    a.from = sockaddr_in{};
    a.from.sin_family = AF_INET;
    a.from.sin_addr.s_addr = LOOPBACK;
    a.from.sin_port = own.port;
    a.Reply("hello");
    a.Reply("again");
    Think(browser);
    CHECK(ArrayLength(browser->list.servers) == 1 && g_browserLog.size() == 1 && g_browserLog[0].reason == 0);
    S_SbServer *const server = Listed(browser, 0);
    CHECK(server->publicIp == LOOPBACK && server->publicPort == a.port && server->flags == 0x11);
    browser->list.lanStartTime = GetTickCount() - 1500;
    Think(browser);
    CHECK(browser->list.state == 0);
    browser->list.lanStartTime = GetTickCount() - 2500;
    Think(browser);
    CHECK(browser->list.state == 1 && browser->list.socket == INVALID_SOCKET);
    ServerBrowserClear(browser);
    // Added with queries in LAN state: all its values.
    browser->dontUpdate = 0;
}

DWORD WINAPI MasterThread(void *argument)
{
    auto *master = static_cast<S_FakeMaster *>(argument);
    master->Accept();
    master->SendCryptHeader("k3y");
    master->SendEncrypted(Raw32(LOOPBACK) + Raw16(3000) + B("\0\0", 2) + LIST_END);
    return 0;
}

// Waits for the list and the queries when not asynchronous.
void TestBrowserBlocking()
{
    g_browserLog.clear();
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *browser = ServerBrowserNew("hd2", "hd2", "k3y", 1, 8, 1, OnBrowser, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, MasterThread, &master, 0, nullptr);
    ServerBrowserUpdate(browser, 0, 1, FIELDS, 2, nullptr);
    CHECK(browser->list.state == 1 && browser->list.parseState == 5);
    WaitForSingleObject(thread, 2000);
    CloseHandle(thread);
    master.Close();
    ServerBrowserClear(browser);
}

}  // namespace

#include "gamespy_browser_strict.inc"

void TestBrowser()
{
    g_sbMasterOverride = "127.0.0.1";
    SocketStartUp();                                    // (the fake master listens first)
    TestBrowserMainList();
    TestBrowserMessages();
    TestBrowserErrors();
    TestBrowserFullReplies();
    TestBrowserLan();
    TestBrowserBlocking();
    TestBrowserStrict();
}
