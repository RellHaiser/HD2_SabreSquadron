// (Part 10c-ii.) The transport's UDP socket (net/net_socket.cpp): sealing, fragments, reassembly, a loopback
// exchange. Included by net_test.cpp (after winsock2.h).

void TestSeal()
{
    C_NetSocket s;
    CHECK(s.m_key == 0x1742BDCDu && s.m_socket == static_cast<uintptr_t>(-1) && s.m_message == 0);
    uint8_t buffer[3 + 13];
    for (int i = 0; i < 13; ++i)
        buffer[3 + i] = static_cast<uint8_t>(i * 7 + 1);
    buffer[0] = buffer[1] = buffer[2] = 0xEE;
    uint8_t original[16];
    std::memcpy(original, buffer, 16);
    s.Seal(buffer, 13);
    // The checksum: the sum of the three whole dwords after the header, its low 24 bits (the 4th byte kept).
    uint32_t sum = 0;
    for (int i = 0; i < 3; ++i) {
        uint32_t d;
        std::memcpy(&d, original + 3 + i * 4, 4);
        sum += d;
    }
    CHECK(buffer[0] == (sum & 0xFF) && buffer[1] == ((sum >> 8) & 0xFF) && buffer[2] == ((sum >> 16) & 0xFF));
    // The dwords XORed with the key plus the size x 0x800; the tail byte left.
    uint32_t d0, o0;
    std::memcpy(&d0, buffer + 3, 4);
    std::memcpy(&o0, original + 3, 4);
    CHECK(d0 == (o0 ^ (13u * 0x800 + 0x1742BDCDu)) && buffer[15] == original[15]);
    CHECK(s.Unseal(buffer, 13) && std::memcmp(buffer + 3, original + 3, 13) == 0);
    // A changed byte, another size or another key: not accepted.
    s.Seal(buffer, 13);
    buffer[5] ^= 1;
    CHECK(!s.Unseal(buffer, 13));
    std::memcpy(buffer, original, 16);
    s.Seal(buffer, 13);
    CHECK(!s.Unseal(buffer, 12));
    std::memcpy(buffer, original, 16);
    s.Seal(buffer, 13);
    s.m_key = 1;
    CHECK(!s.Unseal(buffer, 13));
    s.m_key = 0x1742BDCD;
    // Under four bytes: nothing summed or changed.
    uint8_t small[5] = {9, 9, 9, 0x41, 0x42};
    s.Seal(small, 2);
    CHECK(small[0] == 0 && small[1] == 0 && small[2] == 0 && small[3] == 0x41 && s.Unseal(small, 2));
}

S_NetFragment Fragment(uint32_t message, uint32_t index, bool last, int size, uint8_t fill, int time)
{
    S_NetFragment f = {};
    f.message = message;
    f.index = index;
    f.last = last;
    f.size = size;
    f.time = time;
    f.data = g_netPool.AllocBuffer(static_cast<size_t>(size));
    std::memset(f.data, fill, static_cast<size_t>(size));
    return f;
}

void TestReassembly()
{
    C_NetSocket s;
    const int now = static_cast<int>(GetPerfTimeMs());
    const int pooled = g_netPool.m_buffers.count;
    // Out of order, another message mixed in, one fragment missing for it.
    s.m_fragments.push_back(Fragment(7, 2, true, 3, 0xC2, now));
    s.m_fragments.push_back(Fragment(9, 1, false, 4, 0x91, now));
    s.m_fragments.push_back(Fragment(7, 0, false, 5, 0xC0, now));
    s.m_fragments.push_back(Fragment(7, 1, false, 2, 0xC1, now));
    uint8_t *data = nullptr;
    int size = 0;
    CHECK(s.Reassemble(&data, &size) && data == s.m_buffer && size == 10);
    CHECK(data[0] == 0xC0 && data[4] == 0xC0 && data[5] == 0xC1 && data[6] == 0xC1 && data[7] == 0xC2 && data[9] == 0xC2);
    CHECK(s.m_fragments.size() == 1 && s.m_fragments[0].message == 9 && g_netPool.m_buffers.count == pooled - 1);
    // Incomplete: nothing.
    CHECK(!s.Reassemble(&data, &size));
    // A gap: not complete; filled: complete.
    s.m_fragments.push_back(Fragment(9, 3, true, 1, 0x93, now));
    CHECK(!s.Reassemble(&data, &size));
    s.m_fragments.push_back(Fragment(9, 0, false, 1, 0x90, now));
    CHECK(!s.Reassemble(&data, &size));
    s.m_fragments.push_back(Fragment(9, 2, false, 1, 0x92, now));
    CHECK(s.Reassemble(&data, &size) && size == 7 && data[0] == 0x90 && data[1] == 0x91 && data[5] == 0x92 &&
          data[6] == 0x93 && s.m_fragments.empty());
    // A single fragment that is the last: whole.
    s.m_fragments.push_back(Fragment(3, 0, true, 2, 0x30, now));
    CHECK(s.Reassemble(&data, &size) && size == 2 && data[1] == 0x30);
    // Over 20 s old: dropped as met.
    s.m_fragments.push_back(Fragment(4, 0, false, 2, 0x40, now - 20001));
    s.m_fragments.push_back(Fragment(4, 1, true, 2, 0x41, now));
    s.m_fragments.push_back(Fragment(5, 0, false, 2, 0x50, now - 19000));
    int first = -1, count = -1;
    CHECK(!s.FindComplete(&first, &count) && s.m_fragments.size() == 2 && s.m_fragments[0].index == 1 &&
          s.m_fragments[1].message == 5);
    // A run broken by a repeated index: found again from the repeat.
    s.m_fragments.clear();
    s.m_fragments.push_back(Fragment(6, 0, false, 1, 0, now));
    s.m_fragments.push_back(Fragment(6, 0, false, 1, 0, now));
    s.m_fragments.push_back(Fragment(6, 1, true, 1, 0, now));
    CHECK(s.FindComplete(&first, &count) && first == 1 && count == 2);
    s.m_fragments.erase(s.m_fragments.begin());
    CHECK(s.FindComplete(&first, &count) && first == 0 && count == 2);
    // The run of the message found after others.
    s.m_fragments.insert(s.m_fragments.begin(), Fragment(2, 5, false, 1, 0, now));
    CHECK(s.FindComplete(&first, &count) && first == 1 && count == 2);
    // A message whose next number belongs to another: not complete.
    s.m_fragments.clear();
    s.m_fragments.push_back(Fragment(1, 0, false, 1, 0, now));
    s.m_fragments.push_back(Fragment(2, 1, true, 1, 0, now));
    CHECK(!s.FindComplete(&first, &count));
    // More than 16 fragments (the quicksort's runs): in order.
    s.m_fragments.clear();
    for (uint32_t i = 0; i < 40; ++i) {
        const uint32_t k = (i * 17) % 40;
        s.m_fragments.push_back(Fragment(8, k, k == 39, 1, static_cast<uint8_t>(k), now));
    }
    CHECK(s.Reassemble(&data, &size) && size == 40);
    bool ordered = true;
    for (int i = 0; i < 40; ++i)
        ordered &= data[i] == i;
    CHECK(ordered && s.m_fragments.empty());
}

uint32_t Loopback()
{
    return inet_addr("127.0.0.1");
}

int BoundPort(C_NetSocket &s)
{
    sockaddr_in a = {};
    int n = sizeof a;
    getsockname(s.m_socket, reinterpret_cast<sockaddr *>(&a), &n);
    return ntohs(a.sin_port);
}

// Received within a second (null: none).
uint8_t *Await(C_NetSocket &s, int *size, uint32_t *address, uint32_t *port)
{
    for (int tries = 0; tries < 50; ++tries) {
        uint8_t *data = nullptr;
        s.Receive(&data, size, address, port);
        if (data)
            return data;
    }
    return nullptr;
}

void TestLoopback()
{
    C_NetSocket a, b;
    a.Open(-1);
    b.Open(-1);
    CHECK(a.m_socket != static_cast<uintptr_t>(-1) && a.m_stopped && a.m_wake && !a.m_stop);
    const int portA = BoundPort(a), portB = BoundPort(b);
    CHECK(portA > 0 && portB > 0 && portA != portB);
    // A small message: sealed, sent by the thread, received whole (its first byte under 0x80).
    uint8_t message[40];
    for (int i = 0; i < 40; ++i)
        message[i] = static_cast<uint8_t>(i + 1);
    b.Send(message, 40, Loopback(), portA);
    int size = 0;
    uint32_t address = 0, port = 0;
    uint8_t *got = Await(a, &size, &address, &port);
    CHECK(got && size == 40 && std::memcmp(got, message, 40) == 0 && address == Loopback() &&
          static_cast<int>(port) == portB && got == a.m_buffer + 3);
    int received = -1, sent = -1;
    a.TakeCounters(&received, &sent);
    CHECK(received == 43 && sent == 0);
    a.TakeCounters(&received, &sent);
    CHECK(received == 0);
    b.TakeCounters(&received, &sent);
    CHECK(sent == 43);
    b.TakeCounters(&received, &sent);
    CHECK(sent == 0);
    // A message starting with 0 is whole too; the pool's buffers come back (sent by the thread).
    const int buffers = g_netPool.m_buffers.count;
    uint8_t zero[6] = {0, 1, 2, 3, 4, 5};
    b.Send(zero, 6, Loopback(), portA);
    got = Await(a, &size, &address, &port);
    CHECK(got && size == 6 && got[0] == 0 && got[5] == 5);
    Sleep(5);
    CHECK(g_netPool.m_buffers.count == buffers);
    // A big one: fragments of 500 (one queued a millisecond), reassembled.
    std::vector<uint8_t> big(1234);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<uint8_t>((i * 31) & 0x7F);
    b.Send(big.data(), 1234, Loopback(), portA);
    CHECK(b.m_delayed.size() == 3 && b.m_message == 1 && b.m_delayed.front().size == 508 && b.m_delayed.back().size == 242);
    while (!b.m_delayed.empty()) {
        b.FlushDelayed();
        Sleep(2);
    }
    got = nullptr;
    for (int tries = 0; tries < 100 && !got; ++tries) {
        uint8_t *data = nullptr;
        a.Receive(&data, &size, &address, &port);
        got = data;
    }
    CHECK(got && size == 1234 && std::memcmp(got, big.data(), 1234) == 0 && got == a.m_buffer && a.m_fragments.empty());
    // 1001: three fragments (the last of one byte); two big messages at once, each whole.
    std::vector<uint8_t> odd(1001);
    for (size_t i = 0; i < odd.size(); ++i)
        odd[i] = static_cast<uint8_t>((i * 7) & 0x7F);
    std::vector<uint8_t> other(700, 0x33);
    b.Send(odd.data(), 1001, Loopback(), portA);
    CHECK(b.m_delayed.size() == 3 && b.m_delayed.back().size == 9);
    b.Send(other.data(), 700, Loopback(), portA);
    CHECK(b.m_delayed.size() == 5);
    while (!b.m_delayed.empty()) {
        b.FlushDelayed();
        Sleep(2);
    }
    int whole = 0;
    bool sawOdd = false, sawOther = false;
    for (int tries = 0; tries < 200 && whole < 2; ++tries) {
        uint8_t *data = nullptr;
        a.Receive(&data, &size, &address, &port);
        if (data) {
            ++whole;
            sawOdd |= size == 1001 && std::memcmp(data, odd.data(), 1001) == 0;
            sawOther |= size == 700 && std::memcmp(data, other.data(), 700) == 0;
        }
    }
    const int pooled = g_netPool.m_buffers.count;
    CHECK(whole == 2 && sawOdd && sawOther && a.m_fragments.empty());
    Sleep(5);
    CHECK(g_netPool.m_buffers.count >= pooled);
    // Exactly 500: fragmented (one of 500, the last).
    std::vector<uint8_t> five(500, 0x11);
    b.Send(five.data(), 500, Loopback(), portA);
    CHECK(b.m_delayed.size() == 1 && b.m_message == 4 && b.m_delayed.front().size == 508);
    b.m_delayed.clear();
    // 499: whole.
    b.Send(five.data(), 499, Loopback(), portA);
    CHECK(b.m_delayed.empty());
    got = Await(a, &size, &address, &port);
    CHECK(got && size == 499);
    // Flushing waits for the next millisecond.
    b.Send(big.data(), 1234, Loopback(), portA);
    g_netLastFragmentTime = static_cast<int>(GetPerfTimeMs()) + 100000;
    b.FlushDelayed();
    CHECK(b.m_delayed.size() == 3);
    g_netLastFragmentTime = 0;
    b.FlushDelayed();
    CHECK(b.m_delayed.size() == 2 && g_netLastFragmentTime > 0);
    b.m_delayed.clear();
    // A corrupted datagram: dropped.
    auto *junk = static_cast<uint8_t *>(g_netPool.AllocBuffer(12));
    for (int i = 0; i < 12; ++i)
        junk[i] = static_cast<uint8_t>(0x55 + i);
    b.QueueSend(junk, 12, Loopback(), portA);
    got = Await(a, &size, &address, &port);
    CHECK(got == nullptr);
    // Nothing waiting: none (after 20 ms).
    uint8_t *none = reinterpret_cast<uint8_t *>(1);
    a.ReceiveRaw(&none, &size, &address, &port);
    CHECK(none == nullptr);
    // The local address; closed (twice is fine).
    CHECK(a.GetLocalAddress() != 0 && a.m_localName[0] != 0);
    a.Close();
    CHECK(a.m_socket == static_cast<uintptr_t>(-1));
    a.Close();
    // Reopened on the port it had (closed for good before), still sending.
    a.Open(portA);
    CHECK(BoundPort(a) == portA && !a.m_stop);
    b.Send(message, 40, Loopback(), portA);
    got = Await(a, &size, &address, &port);
    CHECK(got && size == 40);
    a.Send(message, 10, Loopback(), portB);
    got = Await(b, &size, &address, &port);
    CHECK(got && size == 10);
    a.Close();
    b.Open(-1);
    CHECK(b.m_socket != static_cast<uintptr_t>(-1));
    b.Close();
}
