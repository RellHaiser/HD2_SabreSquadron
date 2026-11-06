// (Part 10f-iii.) The browser's edges, added for what the mutation run showed untested: dirty memory, every record arriving a
// byte at a time, cut replies, the shared strings' counts, the think's results. Included by gamespy_browser_tests.inc.

namespace {

struct S_TestSharedString {
    char *text;
    int references;
};
// The string's count in the shared table; 0: not there.
int SharedRefs(const char *text)
{
    S_TestSharedString key = {const_cast<char *>(text), 0};
    const auto *found = static_cast<S_TestSharedString *>(TableLookup(SbSharedStrings(), &key));
    return found ? found->references : 0;
}

// The heap's next blocks of this size hold junk.
void DirtyHeap(size_t size, int count)
{
    std::vector<void *> blocks;
    for (int i = 0; i < count; ++i) {
        blocks.push_back(std::malloc(size));
        std::memset(blocks.back(), 0x5A, size);
    }
    for (void *block : blocks)
        std::free(block);
}
S_ServerBrowser *NewDirty(const char *key, int maxUpdates, int version)
{
    DirtyHeap(sizeof(S_ServerBrowser), 4);
    DirtyHeap(sizeof(S_SbServer), 64);
    return ServerBrowserNew("hd2", "hd2", key, 1, maxUpdates, version, OnBrowser, nullptr);
}
std::string Message(int type, const std::string &data)
{
    return Be16(static_cast<unsigned>(data.size() + 3)) + std::string(1, static_cast<char>(type)) + data;
}

// All but the last byte of `plain`, a think after each: every think fine.
void FeedAllButLast(S_FakeMaster &master, S_ServerBrowser *b, const std::string &plain)
{
    for (size_t i = 0; i + 1 < plain.size(); ++i) {
        master.SendEncrypted(std::string(1, plain[i]));
        CHECK(ServerBrowserThink(b) == 0);
    }
}
void FeedLast(S_FakeMaster &master, S_ServerBrowser *b, const std::string &plain)
{
    master.SendEncrypted(std::string(1, plain.back()));
    CHECK(ServerBrowserThink(b) == 0);
}

void TestBrowserNewIsClean()
{
    S_ServerBrowser *b = NewDirty("k3y", 3, 1);
    CHECK(b->engine.keyCount == 0 && b->engine.publicIp == 0 && b->engine.querying.count == 0 && b->engine.querying.first == nullptr &&
          b->engine.querying.last == nullptr && b->engine.pending.count == 0 && b->engine.pending.first == nullptr && b->engine.pending.last == nullptr);
    CHECK(b->list.deadList == nullptr && b->list.publicIp == 0 && b->list.inBuffer == nullptr && b->list.inBufferSize == 0 && b->list.keyList == nullptr);
    CHECK(b->list.popularCount == 0 && b->list.sourceIp == 0 && b->list.errorMessage && b->list.errorMessage[0] == 0);
    CHECK(b->dontUpdate == 0 && b->disconnectOnComplete == 0 && std::strcmp(b->list.queryFromKey, "k3y") == 0);
    CHECK(ServerBrowserThink(b) == 0);                  // (disconnected: nothing)
    ServerBrowserClear(b);
}

// Each part of the list a byte at a time: nothing happens before its last byte, everything at it. The shared strings counted.
void TestBrowserByteByByte()
{
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *b = NewDirty("k3y", 8, 1);
    b->dontUpdate = 1;
    b->list.options = 2;                                // (the update's own replace it)
    ServerBrowserUpdate(b, 1, 1, FIELDS, 2, nullptr);
    CHECK(b->list.options == 0);
    master.Accept();
    // The crypt header (plain).
    const std::string header = master.MakeCryptHeader("k3y");
    for (size_t i = 0; i + 1 < header.size(); ++i) {
        master.Send(std::string(1, header[i]));
        CHECK(ServerBrowserThink(b) == 0);
    }
    CHECK(b->list.parseState == 0 && b->list.inBufferSize == static_cast<int>(header.size()) - 1);
    master.Send(std::string(1, header.back()));
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(b->list.parseState == 1 && b->list.inBufferSize == 0);
    // The fixed header.
    const uint32_t publicIp = inet_addr("127.0.0.3");
    const std::string fixed = Raw32(publicIp) + Raw16(2500);
    FeedAllButLast(master, b, fixed);
    CHECK(b->list.parseState == 1 && b->list.inBufferSize == 5 && b->list.publicIp == 0);
    FeedLast(master, b, fixed);
    CHECK(b->list.parseState == 2 && b->list.inBufferSize == 0 && b->list.publicIp == publicIp && b->list.expected == -1);
    // The keys: their names only here. The count byte first.
    const std::string keys = std::string(1, '\3') + B("\0", 1) + Z("strictname") + B("\1", 1) + Z("strictbyte") + B("\2", 1) + Z("strictshort");
    FeedAllButLast(master, b, keys);
    CHECK(b->list.parseState == 2 && b->list.expected == 3 && ArrayLength(b->list.keyList) == 2);
    FeedLast(master, b, keys);
    CHECK(b->list.parseState == 3 && ArrayLength(b->list.keyList) == 3 && SharedRefs("strictname") == 1 && SharedRefs("strictshort") == 1);
    // The popular values.
    const std::string popular = std::string(1, '\2') + Z("strictpop0") + Z("strictpop1");
    FeedAllButLast(master, b, popular);
    CHECK(b->list.parseState == 3 && b->list.popularCount == 1);
    FeedLast(master, b, popular);
    CHECK(b->list.parseState == 4 && b->list.popularCount == 2 && SharedRefs("strictpop0") == 1 && SharedRefs("strictpop1") == 1);
    // The servers, of each form.
    const std::string fields = B("\0\7", 2) + Be16(9);
    const std::string records[] = {
        B("\x40", 1) + Raw32(LOOPBACK) + fields,
        B("\x48", 1) + Raw32(LOOPBACK + 0x01000000u) + Raw32(0x0A0B0C0D) + fields,
        B("\x50", 1) + Raw32(LOOPBACK) + Raw16(1101) + B("\xFF", 1) + Z("own") + B("\7", 1) + Be16(9),
        B("\x7A", 1) + Raw32(LOOPBACK) + Raw16(1102) + Raw32(0x01020304) + Raw16(1103) + Raw32(0x05060708) + fields,
        B("\x10", 1) + Raw32(LOOPBACK) + Raw16(1104),
        B("\x02", 1) + Raw32(LOOPBACK + 0x02000000u) + Raw32(0x11121314),
        B("\x08", 1) + Raw32(LOOPBACK + 0x03000000u) + Raw32(0x15161718),
        B("\x20", 1) + Raw32(LOOPBACK + 0x04000000u) + Raw16(1105),
        B("\x90", 1) + Raw32(LOOPBACK) + Raw16(1106) + Z("strictk") + Z("strictv") + B("\0", 1),
    };
    int count = 0;
    for (const std::string &record : records) {
        FeedAllButLast(master, b, record);
        Sleep(15);
        CHECK(ServerBrowserThink(b) == 0);
        CHECK(ArrayLength(b->list.servers) == count);
        FeedLast(master, b, record);
        Sleep(15);
        CHECK(ServerBrowserThink(b) == 0);
        ++count;
        CHECK(ArrayLength(b->list.servers) == count && b->list.inBufferSize == 0);
    }
    S_SbServer *const first = Listed(b, 0);
    CHECK(first->publicPort == 2500 && first->privateIp == 0 && first->privatePort == 2500 && first->icmpIp == 0 && first->updateTime == 0 && first->state == 1 && first->next == nullptr);
    CHECK(Value(first, "strictname") == "strictpop0" && Value(first, "strictbyte") == "7" && Value(first, "strictshort") == "9");
    S_SbServer *const icmp = Listed(b, 1);
    CHECK(icmp->icmpIp == 0x0A0B0C0Du && Value(icmp, "strictname") == "strictpop0" && Value(icmp, "strictbyte") == "7" && Value(icmp, "strictshort") == "9");
    S_SbServer *const all = Listed(b, 3);
    CHECK(all->publicPort == 1102 && all->privateIp == 0x01020304u && all->privatePort == 1103 && all->icmpIp == 0x05060708u && Value(all, "strictshort") == "9");
    S_SbServer *const plain = Listed(b, 4);
    CHECK(plain->state == 0 && plain->flags == 0x10 && plain->privateIp == 0 && plain->privatePort == 2500);
    CHECK(Listed(b, 5)->privateIp == 0x11121314u && Listed(b, 5)->publicPort == 2500);
    CHECK(Listed(b, 6)->icmpIp == 0x15161718u);
    CHECK(Listed(b, 7)->privatePort == 1105);
    CHECK(Listed(b, 8)->state == 2 && Value(Listed(b, 8), "strictk") == "strictv");
    // Each key's and value's references: the key list + the servers' own; the popular list too.
    CHECK(SharedRefs("strictname") == 1 + 4 && SharedRefs("strictpop0") == 1 + 3 && SharedRefs("strictpop1") == 1 && SharedRefs("strictk") == 1);
    // The end, a byte at a time: disconnected (as asked); the lists' references gone.
    FeedAllButLast(master, b, LIST_END);
    CHECK(b->list.parseState == 4 && b->list.state == 3 && b->list.inBufferSize == 4);
    const SOCKET socketBefore = b->list.socket;
    FeedLast(master, b, LIST_END);
    CHECK(b->list.parseState == 5 && b->list.state == 1 && b->list.inBufferSize == 0 && b->list.inBuffer == nullptr);
    {
        int type = 0, typeSize = sizeof type;
        CHECK(getsockopt(socketBefore, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &typeSize) != 0);
    }
    CHECK(SharedRefs("strictpop1") == 0 && SharedRefs("strictname") == 4 && SharedRefs("strictpop0") == 3);
    // Cleared: all gone, the values, the servers' lists.
    ServerBrowserClear(b);
    CHECK(SharedRefs("strictname") == 0 && SharedRefs("strictpop0") == 0 && SharedRefs("strictk") == 0 && SharedRefs("strictv") == 0 && SharedRefs("7") == 0);
    CHECK(ArrayLength(b->list.servers) == 0 && b->list.deadList == nullptr);
    master.Close();
}

// A list with no keys: connected on, each case's server added (queried for all) and its reply read.
void TestBrowserCutReplies()
{
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *b = NewDirty("k3y", 8, 1);
    ServerBrowserUpdate(b, 1, 0, FIELDS, 0, nullptr);
    master.Accept();
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(3000) + B("\0\0", 2) + LIST_END);
    Think(b, 2);
    CHECK(b->list.state == 2);
    S_FakeServer q;
    const std::string address = Raw32(LOOPBACK) + Raw16(q.port);
    const auto answer = [&](const std::string &reply) {
        master.SendEncrypted(Message(2, B("\x10", 1) + address));
        CHECK(ServerBrowserThink(b) == 0);
        CHECK(q.Receive().size() == 10);
        q.Reply(B("\0abcd", 5) + reply);
        Think(b);
        CHECK(ArrayLength(b->list.servers) == 1 && b->engine.querying.count == 0);
        return Listed(b, 0);
    };
    const auto forget = [&] {
        master.SendEncrypted(Message(4, address));
        CHECK(ServerBrowserThink(b) == 0);
        CHECK(ArrayLength(b->list.servers) == 0 && b->list.deadList != nullptr);
        CHECK(ServerBrowserThink(b) == 0);
        CHECK(b->list.deadList == nullptr);
    };
    const std::string end = B("\0", 1);
    // (The buffer's own terminator sits behind each cut: only the sizes tell it is not part of the reply.)
    S_SbServer *s = answer(Z("k1") + Z("v1") + Z("k2") + "v2");
    CHECK(Value(s, "k1") == "v1" && Value(s, "k2") == "<none>");
    forget();
    s = answer(Z("k1") + Z("v1") + "k2");
    CHECK(Value(s, "k1") == "v1" && Value(s, "k2") == "<none>");
    forget();
    s = answer(Z("k") + Z("v") + end + Be16(2) + Z("col") + end + Z("a") + "b");
    CHECK(Value(s, "col0") == "a" && Value(s, "col1") == "<none>");
    forget();
    s = answer(Z("k") + Z("v") + end + Be16(1) + "col");
    CHECK(Value(s, "k") == "v" && Value(s, "col0") == "<none>");
    forget();
    // Two columns, two tables; a third is not read; no rows.
    s = answer(end + Be16(1) + Z("c") + Z("d") + end + Z("1") + Z("2") + Be16(0) + Z("t") + end + Be16(1) + Z("x") + end + Z("3"));
    CHECK(Value(s, "c0") == "1" && Value(s, "d0") == "2" && Value(s, "t0") == "<none>" && Value(s, "x0") == "<none>");
    forget();
    s = answer(end + Be16(0) + Z("c") + end + Be16(1) + Z("t") + end + Z("9"));
    CHECK(Value(s, "c0") == "<none>" && Value(s, "t0") == "9");
    forget();
    // A column's name: 100 bytes at most, the terminator counted.
    const std::string name99(99, 'n'), name100(100, 'm');
    s = answer(end + Be16(1) + Z(name99) + end + Z("v"));
    CHECK(Value(s, (name99 + "0").c_str()) == "v");
    forget();
    s = answer(end + Be16(1) + Z(name100) + end + Z("v"));
    CHECK(Value(s, (name100 + "0").c_str()) == "<none>");
    forget();
    // No more than the header: no values, a reply all the same.
    s = answer("");
    CHECK(s->state == 3);
    forget();
    master.CloseClient();
    Sleep(10);
    CHECK(ServerBrowserThink(b) == 3);                  // (closed)
    CHECK(b->list.state == 1);
    ServerBrowserClear(b);
    master.Close();
}

// The time-out's marks (a server without the port's bit); servers removed while being asked or waiting; a clear in the middle.
void TestBrowserEngineLists()
{
    g_browserLog.clear();
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *b = NewDirty("k3y", 1, 1);
    ServerBrowserUpdate(b, 1, 0, FIELDS, 2, nullptr);
    master.Accept();
    master.SendCryptHeader("k3y");
    S_FakeServer peer;
    const uint32_t ipA = LOOPBACK + 0x05000000u, ipB = LOOPBACK + 0x06000000u, ipC = LOOPBACK + 0x07000000u;
    // Directly after the list's end in the same read, the messages that keep it going.
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(peer.port) + B("\0\0", 2) +
                         B("\x08", 1) + Raw32(ipA) + Raw32(0x0102) + LIST_END + Message(2, B("\0", 1) + Raw32(ipB)) + Message(2, B("\0", 1) + Raw32(ipC)));
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(b->list.state == 2 && ArrayLength(b->list.servers) == 3);
    S_SbServer *const a = Listed(b, 0), *const middle = Listed(b, 1), *const last = Listed(b, 2);
    CHECK(b->engine.querying.first == a && b->engine.querying.count == 1 && b->engine.pending.count == 2);
    CHECK(b->engine.pending.first == middle && b->engine.pending.last == last && middle->next == last && last->next == nullptr);
    // Waiting, removed: out of the waiting list only.
    master.SendEncrypted(Message(4, Raw32(ipB) + Raw16(peer.port)));
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(b->engine.pending.count == 1 && b->engine.pending.first == last && b->engine.pending.last == last && b->engine.querying.count == 1);
    // The time-out: its flags 0x08 -> 0x10 (their own bits, the original's way).
    a->updateTime = GetTickCount() - 3000;
    const uint8_t stateBefore = a->state;
    g_browserLog.clear();
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(a->flags == 0x10 && a->state == stateBefore && g_browserLog.size() == 1 && g_browserLog[0].reason == 2 && g_browserLog[0].server == a);
    CHECK(b->engine.querying.first == last && b->engine.pending.count == 0 && b->engine.pending.first == nullptr && b->engine.pending.last == nullptr);
    // Being asked, removed: out of that list; idle is not told then (the think that sees none does).
    master.SendEncrypted(Message(4, Raw32(ipC) + Raw16(peer.port)));
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(b->engine.querying.count == 0 && b->engine.querying.first == nullptr && b->engine.querying.last == nullptr && ArrayLength(b->list.servers) == 1);
    // A clear in the middle of it all: the connection closed, the queries forgotten.
    master.SendEncrypted(Message(2, B("\0", 1) + Raw32(ipB)));
    master.SendEncrypted(Message(2, B("\0", 1) + Raw32(ipC)));
    Think(b, 2);
    CHECK(b->engine.querying.count == 1 && b->engine.pending.count == 1);
    const SOCKET open = b->list.socket;
    ServerBrowserClear(b);
    CHECK(b->list.socket == INVALID_SOCKET && b->list.state == 1 && b->engine.querying.count == 0 && b->engine.querying.first == nullptr && b->engine.pending.count == 0);
    {
        int type = 0, typeSize = sizeof type;
        CHECK(getsockopt(open, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &typeSize) != 0);
    }
    master.Close();
}

void BeginConnected(S_ServerBrowser *b, S_FakeMaster &master, const std::string &keys)
{
    ServerBrowserUpdate(b, 1, 0, FIELDS, 2, nullptr);
    master.Accept();
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(3000) + keys + B("\0", 1) + LIST_END);
    Think(b, 2);
    CHECK(b->list.state == 2);
}

// The check that a record has all its fixed fields, with strings behind the numbers (nothing before its last byte). The query's
// time, its type, its keys' end.
void TestBrowserFieldOrder()
{
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *b = NewDirty("k3y", 8, 1);
    ServerBrowserUpdate(b, 1, 0, FIELDS, 2, nullptr);
    master.Accept();
    master.SendCryptHeader("k3y");
    S_FakeServer q;
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(q.port) + B("\4\1", 2) + Z("orderb") + B("\2", 1) + Z("orders") + B("\0", 1) + Z("ordera") + B("\0", 1) + Z("orderc") + B("\0", 1));
    Think(b, 2);
    CHECK(b->list.parseState == 4);
    const std::string record = B("\x40", 1) + Raw32(LOOPBACK) + B("\1", 1) + Be16(2) + B("\xFF", 1) + Z("one") + B("\xFF", 1) + Z("two");
    FeedAllButLast(master, b, record);
    Sleep(15);
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(ArrayLength(b->list.servers) == 0);
    FeedLast(master, b, record);
    Sleep(15);
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(ArrayLength(b->list.servers) == 1);
    S_SbServer *const server = Listed(b, 0);
    CHECK(Value(server, "orderb") == "1" && Value(server, "orders") == "2" && Value(server, "ordera") == "one" && Value(server, "orderc") == "two");
    master.CloseClient();
    Sleep(10);
    ServerBrowserThink(b);
    ServerBrowserClear(b);
    const DWORD before = GetTickCount();
    // Asked directly, a server of ours.
    ServerBrowserUpdate(b, 1, 1, FIELDS, 2, nullptr);
    master.Accept();
    master.SendCryptHeader("k3y");
    master.SendEncrypted(Raw32(LOOPBACK) + Raw16(3000) + B("\0\0", 2) + B("\x10", 1) + Raw32(LOOPBACK) + Raw16(q.port) + LIST_END);
    Think(b, 2);
    const std::string query = q.Receive();
    const DWORD after = GetTickCount();
    CHECK(query.size() == 12);
    if (query.size() == 12) {
        DWORD sent;
        std::memcpy(&sent, query.data() + 3, 4);
        CHECK(sent >= before && sent <= after);
        CHECK(query[2] == 0 && query[7] == 2 && query[10] == 0 && query[11] == 0);
    }
    ServerBrowserClear(b);
    master.CloseClient();
    master.Close();
}

// The messages' limits and the think's results.
void TestBrowserMessageLimits()
{
    g_browserLog.clear();
    S_FakeMaster master;
    master.Listen();
    S_ServerBrowser *b = NewDirty("k3y", 8, 1);
    b->dontUpdate = 1;
    const std::string keys = B("\2\0", 2) + Z("firstkey") + B("\1", 1) + Z("secondkey");
    BeginConnected(b, master, keys);
    CHECK(SharedRefs("firstkey") == 1 && SharedRefs("secondkey") == 1);
    CHECK(ServerBrowserThink(b) == 0);                  // (nothing to read)
    // Only a header; split; two bytes are not a size yet.
    master.SendEncrypted(Message(9, ""));
    CHECK(ServerBrowserThink(b) == 0 && b->list.state == 2 && b->list.inBufferSize == 0);
    const std::string two = Message(9, "a") + Message(9, "");
    master.SendEncrypted(two.substr(0, 2));
    CHECK(ServerBrowserThink(b) == 0 && b->list.inBufferSize == 2);
    master.SendEncrypted(two.substr(2, 3));
    CHECK(ServerBrowserThink(b) == 0 && b->list.inBufferSize == 1);   // (the first done, a byte of the next)
    master.SendEncrypted(two.substr(5));
    CHECK(ServerBrowserThink(b) == 0 && b->list.inBufferSize == 0 && b->list.state == 2);
    // A key list replaces the old (released, each of them).
    master.SendEncrypted(Message(1, B("\1\0", 2) + Z("thirdkey")));
    CHECK(ServerBrowserThink(b) == 0);
    CHECK(SharedRefs("firstkey") == 0 && SharedRefs("secondkey") == 0 && SharedRefs("thirdkey") == 1);
    // Key lists cut: at the type; in the name. An error, 4, and disconnected.
    g_browserLog.clear();
    master.SendEncrypted(Message(1, B("\2\0", 2) + Z("a") + B("\1", 1)));
    CHECK(ServerBrowserThink(b) == 4 && b->list.state == 1);
    master.CloseClient();
    BeginConnected(b, master, keys);
    master.SendEncrypted(Message(1, B("\1\0", 2) + "noend"));
    CHECK(ServerBrowserThink(b) == 4 && b->list.state == 1);
    master.CloseClient();
    BeginConnected(b, master, keys);
    master.SendEncrypted(Message(1, B("\2\0", 2) + Z("a") + B("\1", 1) + "b"));
    CHECK(ServerBrowserThink(b) == 4 && b->list.state == 1);
    master.CloseClient();
    // Records too short; the size below a header (the fix) and above the buffer.
    BeginConnected(b, master, keys);
    master.SendEncrypted(Message(2, "abcd"));
    CHECK(ServerBrowserThink(b) == 4 && b->list.state == 1);
    master.CloseClient();
    BeginConnected(b, master, keys);
    master.SendEncrypted(Message(4, "abcde"));
    CHECK(ServerBrowserThink(b) == 4 && b->list.state == 1);
    master.CloseClient();
    BeginConnected(b, master, keys);
    master.SendEncrypted(Message(4, "abcdef"));         // (just enough: not known, no error)
    CHECK(ServerBrowserThink(b) == 0 && b->list.state == 2);
    master.SendEncrypted(Be16(2) + "x");
    CHECK(ServerBrowserThink(b) == 4 && b->list.state == 1);
    master.CloseClient();
    BeginConnected(b, master, keys);
    master.SendEncrypted(Be16(0x1001) + "x");
    CHECK(ServerBrowserThink(b) == 4 && b->list.state == 1);
    master.CloseClient();
    // A message of the buffer's size exactly: waited for.
    BeginConnected(b, master, keys);
    master.SendEncrypted(Be16(0x1000) + "x");
    CHECK(ServerBrowserThink(b) == 0 && b->list.state == 2 && b->list.inBufferSize == 3);
    master.CloseClient();
    Sleep(10);
    CHECK(ServerBrowserThink(b) == 3 && b->list.state == 1);
    // After the error: their keys released.
    CHECK(SharedRefs("firstkey") == 0 && SharedRefs("secondkey") == 0 && b->list.keyList == nullptr && b->list.inBufferSize == 0);
    ServerBrowserClear(b);
    // The master by a name, not an address; an unknown one.
    g_sbMasterOverride = "localhost";
    b->dontUpdate = 1;
    ServerBrowserUpdate(b, 1, 0, FIELDS, 2, nullptr);
    CHECK(b->list.state == 3 && b->list.socket != INVALID_SOCKET);
    master.Accept();
    CHECK(master.request.size() > 8);
    ServerBrowserClear(b);
    master.CloseClient();
    g_sbMasterOverride = "no.such.host.invalid";
    ServerBrowserUpdate(b, 1, 0, FIELDS, 2, nullptr);
    CHECK(b->list.state == 1 && b->list.socket == INVALID_SOCKET);
    g_sbMasterOverride = "127.0.0.1";
    ServerBrowserClear(b);
    master.Close();
}

}  // namespace

void TestBrowserStrict()
{
    TestBrowserNewIsClean();
    TestBrowserByteByByte();
    TestBrowserCutReplies();
    TestBrowserEngineLists();
    TestBrowserMessageLimits();
    TestBrowserFieldOrder();
}
