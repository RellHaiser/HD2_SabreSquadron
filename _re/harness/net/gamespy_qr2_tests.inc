// (Part 10f-ii.) GameSpy's query & reporting (net/gamespy_qr2.cpp) on loopback UDP: a test socket plays the browser and the
// master server. Included by gamespy_test.cpp.

namespace {

std::vector<std::string> g_qrLog;
int g_players = 2;
void QrServerKey(int key, S_Qr2Buffer *buffer, void *)
{
    if (key == QR2_HOSTNAME)
        qr2_buffer_add(buffer, "My server");
    else if (key == QR2_NUMPLAYERS)
        qr2_buffer_add_int(buffer, g_players);
    // (Others: nothing: the SDK adds an empty value.)
}
void QrPlayerKey(int key, int index, S_Qr2Buffer *buffer, void *)
{
    if (key == QR2_PLAYER) {
        char name[8];
        std::sprintf(name, "P%d", index);
        qr2_buffer_add(buffer, name);
    }
}
void QrTeamKey(int key, int index, S_Qr2Buffer *buffer, void *)
{
    if (key == QR2_TEAM_NAME)
        qr2_buffer_add(buffer, index ? "Allies" : "Axis");
}
void QrKeyList(int keyType, S_Qr2KeyBuffer *keys, void *)
{
    if (keyType == QR2_KEY_SERVER) {
        qr2_keybuffer_add(keys, QR2_HOSTNAME);
        qr2_keybuffer_add(keys, QR2_NUMPLAYERS);
        qr2_keybuffer_add(keys, QR2_MAPNAME);           // (no value)
        qr2_keybuffer_add(keys, QR2_ISDEDICATED);       // (a registered name)
        qr2_keybuffer_add(keys, 0x40);                  // (no name)
    } else if (keyType == QR2_KEY_PLAYER) {
        qr2_keybuffer_add(keys, QR2_PLAYER);
    } else {
        qr2_keybuffer_add(keys, QR2_TEAM_NAME);
    }
}
int QrCount(int keyType, void *) { return keyType == QR2_KEY_PLAYER ? g_players : 2; }
void QrAddError(int error, char *message, void *data)
{
    g_qrLog.push_back("error " + std::to_string(error) + " " + message + " " + std::to_string(data == &g_players));
}

SOCKET g_peer;
sockaddr_in g_qrAddress;

// `text` (with its nulls) sent to the reporting socket; the SDK thinks; its answer ("": none).
std::string Exchange(const std::string &packet)
{
    sendto(g_peer, packet.data(), static_cast<int>(packet.size()), 0, reinterpret_cast<const sockaddr *>(&g_qrAddress),
           sizeof g_qrAddress);
    Sleep(10);
    qr2_think(nullptr);
    fd_set read;
    FD_ZERO(&read);
    FD_SET(g_peer, &read);
    timeval wait = {0, 20000};
    if (select(0, &read, nullptr, nullptr, &wait) <= 0)
        return "";
    char reply[4096];
    const int size = recv(g_peer, reply, sizeof reply, 0);
    return size > 0 ? std::string(reply, static_cast<size_t>(size)) : "";
}
std::string Packet(char type, const std::string &instance, const std::string &data)
{
    std::string p("\xFE\xFD");
    p += type;
    p += instance;
    return p + data;
}
std::string S(const char *text, size_t size) { return std::string(text, size); }

void TestQrBuffers()
{
    // A value cut to the room; none left: nothing; ints.
    S_Qr2Buffer b;
    b.length = 0;
    qr2_buffer_add(&b, "abc");
    CHECK(b.length == 4 && std::strcmp(b.data, "abc") == 0);
    qr2_buffer_add_int(&b, -12);
    CHECK(b.length == 8 && std::strcmp(b.data + 4, "-12") == 0);
    b.length = QR2_PACKET_SIZE - 2;
    qr2_buffer_add(&b, "xyz");
    CHECK(b.length == QR2_PACKET_SIZE && b.data[QR2_PACKET_SIZE - 2] == 'x' && b.data[QR2_PACKET_SIZE - 1] == 0);
    qr2_buffer_add(&b, "q");
    CHECK(b.length == QR2_PACKET_SIZE);
    b.length = QR2_PACKET_SIZE - 4;
    qr2_buffer_add(&b, "abc");                          // (it just fits)
    CHECK(b.length == QR2_PACKET_SIZE && std::strcmp(b.data + QR2_PACKET_SIZE - 4, "abc") == 0);
    // Keys 1 .. 0xFE, 0xFE of them at most.
    S_Qr2KeyBuffer k;
    k.count = 0;
    qr2_keybuffer_add(&k, 0);
    qr2_keybuffer_add(&k, 0xFF);
    CHECK(k.count == 0);
    qr2_keybuffer_add(&k, 1);
    qr2_keybuffer_add(&k, 0xFE);
    CHECK(k.count == 2 && k.keys[0] == 1 && k.keys[1] == 0xFE);
    k.count = 0xFD;
    qr2_keybuffer_add(&k, 7);
    CHECK(k.count == 0xFE && k.keys[0xFD] == 7);
    qr2_keybuffer_add(&k, 8);
    CHECK(k.count == 0xFE);
}

void TestQrQueries()
{
    g_peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in at = {};
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(g_peer, reinterpret_cast<sockaddr *>(&at), sizeof at);
    qr2_register_key(0x31, "low");                      // (not a game's: below 0x32)
    qr2_register_key(0xFF, "high");
    qr2_register_key(0x33, "isdedicated");
    CHECK(g_qr2KeyNames[0x31] == nullptr && g_qr2KeyNames[0xFF] == nullptr && std::strcmp(g_qr2KeyNames[0x33], "isdedicated") == 0);
    qr2_register_key(0x32, "first");
    qr2_register_key(0xFE, "last");
    CHECK(std::strcmp(g_qr2KeyNames[0x32], "first") == 0 && std::strcmp(g_qr2KeyNames[0xFE], "last") == 0);
    // On the base port, or the next free one.
    const int base = 21000 + static_cast<int>(GetCurrentProcessId() % 4000);
    SOCKET blocker = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    at.sin_port = htons(static_cast<u_short>(base));
    at.sin_addr.s_addr = 0;
    CHECK(bind(blocker, reinterpret_cast<sockaddr *>(&at), sizeof at) == 0);
    CHECK(qr2_init(nullptr, nullptr, base, "hd2", "sK8pQ9", 0, 0, QrServerKey, QrPlayerKey, QrTeamKey, QrKeyList, QrCount,
                   QrAddError, &g_players) == 0);
    S_Qr2Record &record = g_qr2DefaultRecord;
    CHECK(record.queryPort == base + 1 && record.readSocket == 1 && record.listedState == 1 && record.isPublic == 0);
    CHECK(std::strcmp(record.gameName, "hd2") == 0 && std::strcmp(record.secretKey, "sK8pQ9") == 0 && record.nextMessageKey == 0);
    CHECK(record.clientMessageKeys[0] == -1 && record.clientMessageKeys[9] == -1 && record.userData == &g_players);
    closesocket(blocker);
    g_qrAddress = sockaddr_in{};
    g_qrAddress.sin_family = AF_INET;
    g_qrAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
    g_qrAddress.sin_port = htons(static_cast<u_short>(record.queryPort));
    const std::string inst("\x11\x22\x33\x44", 4);
    // Short or foreign packets: nothing.
    CHECK(Exchange("\xFE\xFD").empty());
    CHECK(Exchange("garbage!").empty());
    CHECK(Exchange(Packet(0, inst, "").substr(0, 6)).empty());
    CHECK(record.listedState == 1);
    // Any packet of the SDK's stops the wait for the challenge (even of an unknown type: not answered).
    CHECK(Exchange(Packet(9, inst, "abc")).empty());
    CHECK(record.listedState == 0);
    // A query of named keys: server 1 (hostname), players 1 (name), teams 1.
    std::string q = S("\x02\x01\x08\x01\x15\x01\x1C", 7);
    std::string expected = S("\x00\x11\x22\x33\x44", 5) + S("My server\0002\0", 12) + S("\x00\x02P0\0P1\0", 8) + S("\x00\x02" "Axis\0Allies\0", 14);
    CHECK(Exchange(Packet(0, inst, q)) == expected);
    // A key without a value: empty; no player keys; no teams.
    q = S("\x01\x05\x00\x00", 4);
    CHECK(Exchange(Packet(0, inst, q)) == S("\x00\x11\x22\x33\x44", 5) + S("\0", 1));
    // All: names (registered ones; "unknown") and values; players' and teams' names, then the values.
    q = S("\xFF\xFF\xFF", 3);
    expected = S("\x00\x11\x22\x33\x44", 5);
    expected += S("hostname\0My server\0numplayers\0002\0mapname\0\0isdedicated\0\0unknown\0\0\0", 64);
    expected += S("\x00\x02player_\0\0P0\0P1\0", 17);
    expected += S("\x00\x02team_t\0\0Axis\0Allies\0", 22);
    CHECK(Exchange(Packet(0, inst, q)) == expected);
    // Truncated lists: nothing added.
    CHECK(Exchange(Packet(0, inst, S("\x05\x01", 2))) == S("\x00\x11\x22\x33\x44", 5));
    CHECK(Exchange(Packet(0, inst, S("\x05\x01\x02\x03\x04\x05\x00", 7))) == S("\x00\x11\x22\x33\x44", 5));
    CHECK(Exchange(Packet(0, inst, S("\x00\x05\x01\x02\x03", 5))) == S("\x00\x11\x22\x33\x44", 5));
    // The challenge: encrypted and in base 64 (values from the decompile's algorithm, computed apart).
    CHECK(Exchange(Packet(1, inst, S("ABCDEF\0", 7))) == S("\x01\x11\x22\x33\x44", 5) + S("H41/Ss56\0", 9));
    CHECK(Exchange(Packet(1, inst, S("xyz\0", 4))) == S("\x01\x11\x22\x33\x44", 5) + S("jyVv\0", 5));
    CHECK(Exchange(Packet(1, inst, S("1234567\0", 8))) == S("\x01\x11\x22\x33\x44", 5) + S("rkvpJpTDJwAA\0", 13));
    CHECK(Exchange(Packet(1, inst, "abc")) == S("\x01\x11\x22\x33\x44", 5));   // (not terminated)
    CHECK(Exchange(Packet(1, inst, "")) == S("\x01\x11\x22\x33\x44", 5));
    CHECK(Exchange(Packet(1, inst, S("\0", 1))) == S("\x01\x11\x22\x33\x44", 5) + S("\0", 1));
    {
        std::string all;                                // (all the digits; computed apart)
        for (int i = 0; i < 63; ++i)
            all += static_cast<char>(0x30 + i);
        CHECK(Exchange(Packet(1, inst, all + S("\0", 1))) == S("\x01\x11\x22\x33\x44", 5) +
              std::string("SE/7q+Ei/mLdm/GQjh9xVbN+A2A1h05DmO8m4MRmrQo5h/1O3ZxaBFrbvWFIbn9CJsJtfhVPfNmFuOSYCjWg") + S("\0", 1));
    }
    std::string longChallenge(0x41, 'c');
    longChallenge += '\0';
    CHECK(Exchange(Packet(1, inst, longChallenge)) == S("\x01\x11\x22\x33\x44", 5));
    longChallenge.erase(0, 1);
    CHECK(Exchange(Packet(1, inst, longChallenge)).size() > 0x50);
    // The echo (0x20 at most).
    CHECK(Exchange(Packet(2, inst, "ping")) == S("\x05\x11\x22\x33\x44", 5) + "ping");
    std::string echo(0x20, 'e');
    CHECK(Exchange(Packet(2, inst, echo)) == S("\x05\x11\x22\x33\x44", 5) + echo);
    echo.assign(0x30, 'e');
    CHECK(Exchange(Packet(2, inst, echo)) == S("\x05\x11\x22\x33\x44", 5) + echo.substr(0, 0x20));
    // Client messages: acknowledged with their key (this instance's); the keys kept (10, round).
    const std::string own(reinterpret_cast<const char *>(record.instanceKey), 4);
    CHECK(Exchange(Packet(6, inst, S("\x01\x00\x00\x00", 4))).empty());
    CHECK(Exchange(Packet(6, own, S("\x01\x00", 2))).empty());
    CHECK(Exchange(Packet(6, own, S("\x01\x00\x00\x00", 4))) == S("\x07", 1) + own + S("\x01\x00\x00\x00", 4));
    CHECK(record.clientMessageKeys[1] == 1 && record.nextMessageKey == 1);
    Exchange(Packet(6, own, S("\x01\x00\x00\x00", 4)));
    CHECK(record.nextMessageKey == 1);
    for (int key = 2; key <= 10; ++key)
        Exchange(Packet(6, own, std::string(reinterpret_cast<const char *>(&key), 4)));
    CHECK(record.nextMessageKey == 0 && record.clientMessageKeys[0] == 10);
    // The add error: this instance's, once.
    record.listedState = 0;
    CHECK(Exchange(Packet(4, inst, S("\x07Nope\0", 6))).empty() && g_qrLog.empty());
    CHECK(Exchange(Packet(4, own, S("\x07", 1))).empty() && g_qrLog.empty());
    CHECK(Exchange(Packet(4, own, S("\x07Nope\0", 6))).empty());
    CHECK(g_qrLog.size() == 1 && g_qrLog[0] == "error 7 Nope 1" && record.listedState == -1);
    CHECK(Exchange(Packet(4, own, S("\x08Again\0", 7))).empty() && g_qrLog.size() == 1);
    // The old form.
    expected = "\\hostname\\My server\\numplayers\\2\\mapname\\\\isdedicated\\\\unknown\\\\player_0\\P0\\player_1\\P1\\team_t0\\Axis\\team_t1\\Allies\\final\\\\queryid\\1.1";
    CHECK(Exchange("\\status\\") == expected);
    // ';': the CD key's (when set).
    CHECK(Exchange(";abc").empty());
    static std::string seen;
    record.cdKeyProcess = [](char *packet, int size, sockaddr *) { seen = std::string(packet, static_cast<size_t>(size)); };
    CHECK(Exchange(";abc").empty() && seen == ";abc");
    record.cdKeyProcess = nullptr;
    // Not read here: nothing answered until read again.
    record.readSocket = 0;
    CHECK(Exchange(Packet(2, inst, "x")).empty());
    record.readSocket = 1;
    qr2_think(nullptr);
    char drop[64];
    Sleep(10);
    CHECK(recv(g_peer, drop, sizeof drop, 0) > 0);
}

// The next packet the master got ("": none).
std::string MasterReceive()
{
    fd_set read;
    FD_ZERO(&read);
    FD_SET(g_peer, &read);
    timeval wait = {0, 30000};
    if (select(0, &read, nullptr, nullptr, &wait) <= 0)
        return "";
    char packet[4096];
    const int size = recv(g_peer, packet, sizeof packet, 0);
    return size > 0 ? std::string(packet, static_cast<size_t>(size)) : "";
}

void TestQrHeartbeats()
{
    S_Qr2Record &record = g_qr2DefaultRecord;
    int size = sizeof record.masterAddress;
    getsockname(g_peer, reinterpret_cast<sockaddr *>(&record.masterAddress), &size);
    record.isPublic = 1;
    record.listedState = 0;
    record.natNegotiate = 0;
    g_qrLog.clear();
    const std::string inst(reinterpret_cast<const char *>(record.instanceKey), 4);
    // The first: at once, with all the values.
    record.lastHeartbeat = 0;
    qr2_think(nullptr);
    std::string beat = MasterReceive();
    CHECK(beat.compare(0, 5, S("\x03", 1) + inst) == 0);
    const std::string port = "localport" + S("\0", 1) + std::to_string(record.queryPort) + S("\0", 1);
    CHECK(beat.find(port) != std::string::npos && beat.find(S("natneg\0" "0\0", 9)) != std::string::npos);
    CHECK(beat.find(S("gamename\0hd2\0", 13)) != std::string::npos && beat.find("My server") != std::string::npos);
    CHECK(beat.find("statechanged") == std::string::npos && beat.find(S("P1\0", 3)) != std::string::npos);
    CHECK(beat.find(S("\0localip0\0", 10)) != std::string::npos || beat.find("localip0") != std::string::npos);
    CHECK(record.lastHeartbeat != 0 && record.lastKeepAlive == record.lastHeartbeat);
    {
        char name[256];
        gethostname(name, sizeof name);
        const hostent *host = gethostbyname(name);
        CHECK(host != nullptr);
        if (host && host->h_addr_list[0]) {
            in_addr first;
            std::memcpy(&first, host->h_addr_list[0], 4);
            CHECK(beat.find(S("localip0\0", 9) + inet_ntoa(first) + S("\0", 1)) != std::string::npos);
        }
    }
    // Not yet due: nothing.
    qr2_think(nullptr);
    CHECK(MasterReceive().empty());
    // NAT negotiation shown; due after 60 s.
    record.natNegotiate = 1;
    record.lastHeartbeat -= 59000;
    qr2_think(nullptr);
    CHECK(MasterReceive().empty());
    record.lastHeartbeat -= 2000;
    record.lastKeepAlive = GetTickCount();
    qr2_think(nullptr);
    beat = MasterReceive();
    CHECK(beat.compare(0, 1, "\x03") == 0 && beat.find(S("natneg\0" "1\0", 9)) != std::string::npos);
    // Keep-alives after 20 s (a bare header).
    record.lastKeepAlive -= 19000;
    qr2_think(nullptr);
    CHECK(MasterReceive().empty());
    record.lastKeepAlive -= 2000;
    qr2_think(nullptr);
    CHECK(MasterReceive() == S("\x08", 1) + inst);
    CHECK(GetTickCount() - record.lastKeepAlive < 1000);
    // Waiting for the challenge: asked again each 10 s (statechanged 3), three times; then told.
    record.listedState = 1;
    for (int state = 1; state < 4; ++state) {
        record.lastHeartbeat -= 9000;
        qr2_think(nullptr);
        CHECK(MasterReceive().empty() && record.listedState == state);
        record.lastHeartbeat -= 2000;
        qr2_think(nullptr);
        beat = MasterReceive();
        CHECK(beat.find(S("statechanged\0" "3\0", 15)) != std::string::npos && record.listedState == state + 1);
    }
    record.lastHeartbeat -= 11000;
    qr2_think(nullptr);
    CHECK(MasterReceive().empty() && record.listedState == 0);
    CHECK(g_qrLog.size() == 1 && g_qrLog[0] == "error 5 No challenge value was received from the master server. 1");
    // Not public: no heartbeats.
    record.isPublic = 0;
    record.lastHeartbeat = 0;
    qr2_think(nullptr);
    CHECK(MasterReceive().empty());
    // The shutdown: a last heartbeat (statechanged 2, no values), the socket closed.
    record.isPublic = 1;
    const SOCKET closing = record.socket;
    StopGameSpyQueries(nullptr);
    {
        int type = 0, typeSize = sizeof type;
        CHECK(getsockopt(closing, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &typeSize) != 0);
    }
    beat = MasterReceive();
    CHECK(beat.find(S("statechanged\0" "2\0", 15)) != std::string::npos && beat.find("My server") == std::string::npos);
    CHECK(beat.size() > 1 && beat[beat.size() - 1] == 0 && beat[beat.size() - 2] == 0);
    CHECK(record.socket == INVALID_SOCKET && record.lastHeartbeat == 0);
    SocketStartUp();
    std::memset(&record, 0x5A, sizeof record);
    std::memset(record.instanceKey, 0, 4);
    CHECK(qr2_init(nullptr, nullptr, 0, "hd2", "k", 0, 0, QrServerKey, QrPlayerKey, QrTeamKey, QrKeyList, QrCount, QrAddError,
                   nullptr) == 0);
    CHECK(record.lastHeartbeat == 0 && record.lastKeepAlive == 0 && record.natCallback == nullptr && record.clientMessage == nullptr);
    CHECK(record.cdKeyProcess == nullptr && record.isPublic == 0 && record.nextMessageKey == 0 && record.natNegotiate == 0);
    CHECK(std::memcmp(record.instanceKey, "\0\0\0\0", 4) != 0);
    StopGameSpyQueries(nullptr);
    SocketStartUp();
    // A record of its own; freed at the shutdown (not public: nothing sent).
    S_Qr2Record *own = nullptr;
    CHECK(qr2_init(&own, "127.0.0.1", 0, "hd2", "k", 0, 1, QrServerKey, QrPlayerKey, QrTeamKey, QrKeyList, QrCount, QrAddError,
                   nullptr) == 0);
    CHECK(own && own != &g_qr2DefaultRecord && own->readSocket == 1 && own->queryPort != 0 && own->natNegotiate == 1);
    StopGameSpyQueries(reinterpret_cast<uintptr_t *>(own));
    CHECK(MasterReceive().empty());
    closesocket(g_peer);
}

}  // namespace
