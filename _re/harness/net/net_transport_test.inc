// (Part 10c-iii.) The transport's connections and manager (net/net_transport.cpp): two transports over loopback,
// driven by hand (their sockets opened, not their receive threads). Included by net_test.cpp.

struct S_Delivered {
    int connection, size, kind;
    uint32_t flags;
    std::vector<uint8_t> data;
};
std::vector<S_Delivered> g_delivered;

void __fastcall TestDeliver(int connection, void *data, int size, int kind, uint32_t flags)
{
    const auto *p = static_cast<const uint8_t *>(data);
    g_delivered.push_back({connection, size, kind, flags, std::vector<uint8_t>(p, p + size)});
}

// Until `done` or a second.
template <class F> bool Pump(C_NetTransport &t, F done)
{
    for (int tries = 0; tries < 50; ++tries) {
        t.ReceiveAll();
        if (done())
            return true;
    }
    return false;
}

void TestConnection()
{
    C_NetSocket socket;
    const int made = static_cast<int>(GetPerfTimeMs());
    C_NetConnection c(9, 0x0100007F, &socket);
    CHECK(c.m_lastPing >= made && c.m_lastSend >= made && c.m_lastReceive >= made && c.m_lastPing <= made + 5);
    CHECK(c.m_id == 9 && c.m_address == 0x0100007Fu && c.m_port == -1 && c.m_session == 0 && c.m_pingId == 0 &&
          c.m_roundTrip == 0 && c.m_socket == &socket);
    CHECK(dynamic_cast<C_NetReliableChannel *>(c.m_channels[1]) && dynamic_cast<C_NetOrderedChannel *>(c.m_channels[2]) &&
          !dynamic_cast<C_NetReliableChannel *>(c.m_channels[0]));
    // Sent: a copy of the data on the channel, numbered.
    const char text[] = "abc";
    C_NetPacket *p = c.Send(text, 3, 12, 1);
    CHECK(p->m_size == 3 && std::memcmp(p->m_data, "abc", 3) == 0 && p->m_data != text && p->m_kind == 12 &&
          p->m_flags == 1 && c.m_channels[1]->m_outgoing.size() == 1 && p->m_sequence == 0x400000);
    // Again on its channel (the plain one here).
    c.Resend(p);
    CHECK(c.m_channels[1]->m_outgoing.size() == 2);
    // Nothing gathered before the port is known (unless forced: the acknowledgements).
    void *data = reinterpret_cast<void *>(1);
    int size = -1;
    c.Gather(&data, &size, false);
    CHECK(data == nullptr && size == 0);
    c.Gather(&data, &size, true);
    CHECK(data != nullptr && size == 2 + 3 + 3 + 1);   // the header, two acknowledgements of 24 bits, a count of 0
    // The resend time: 10..700, else 700.
    c.SetResendTime(50);
    CHECK(static_cast<C_NetReliableChannel *>(c.m_channels[1])->m_resendTime == 50 &&
          static_cast<C_NetReliableChannel *>(c.m_channels[2])->m_resendTime == 50);
    c.SetResendTime(9);
    CHECK(static_cast<C_NetReliableChannel *>(c.m_channels[1])->m_resendTime == 700);
    c.SetResendTime(10);
    CHECK(static_cast<C_NetReliableChannel *>(c.m_channels[2])->m_resendTime == 10);
    c.SetResendTime(701);
    CHECK(static_cast<C_NetReliableChannel *>(c.m_channels[2])->m_resendTime == 700);
    c.SetResendTime(700);
    CHECK(static_cast<C_NetReliableChannel *>(c.m_channels[2])->m_resendTime == 700);
    // Received: the first channel's first; the data handed over.
    C_NetPacket *r0 = MakePacket(2, 0x21), *r2 = MakePacket(4, 0x42);
    r0->m_sequence = 0x400000;
    r2->m_sequence = 0x400000;
    c.m_channels[2]->Receive(r2);
    c.m_channels[0]->Receive(r0);
    void *got = nullptr;
    int kind = 0;
    uint32_t flags = 99;
    const int idle = g_netPool.m_packets.count;
    CHECK(c.Receive(&got, &size, &kind, &flags) && size == 2 && static_cast<uint8_t *>(got)[1] == 0x21 && kind == 5 &&
          flags == 0 && r0->m_data == nullptr && r0->m_size == 0 && g_netPool.m_packets.count == idle + 1);
    g_netPool.FreeBuffer(got, size);
    CHECK(c.Receive(&got, &size, &kind, &flags) && size == 4);
    g_netPool.FreeBuffer(got, size);
    CHECK(!c.Receive(&got, &size, &kind, &flags) && got == nullptr && size == 0);
    // Pings: the id counts in 8 bits; due after 2 s.
    c.m_pingId = 0xFF;
    c.m_port = 1;
    c.SendPing();
    // (57 bits: 8 bytes, the seal's 3 more; queued, no thread to send it.)
    CHECK(c.m_pingId == 0 && !socket.m_queue.empty() && socket.m_queue.back().size == 8 + 3);
    c.m_lastPing = static_cast<int>(GetPerfTimeMs());
    c.PingIfDue();
    CHECK(c.m_pingId == 0);
    c.m_lastPing -= 2001;
    c.PingIfDue();
    CHECK(c.m_pingId == 1 && c.m_lastPing >= static_cast<int>(GetPerfTimeMs()) - 5);
    c.m_lastPing -= 1990;
    c.PingIfDue();
    CHECK(c.m_pingId == 1);
    // Due packets resent by Flush (not before the port is known).
    auto *reliable = static_cast<C_NetReliableChannel *>(c.m_channels[1]);
    for (auto &pending : reliable->m_pending)
        pending.time = 0;
    c.m_port = -1;
    const int interval = reliable->m_pending.front().interval;
    c.ResendAll();
    CHECK(reliable->m_pending.front().interval == interval);
    c.m_port = 1;
    c.Flush(false);
    CHECK(reliable->m_pending.front().interval == interval * 2 && c.m_lastSend >= made);
    for (auto &d : socket.m_queue)
        g_netPool.FreeBuffer(d.data, d.size);
    socket.m_queue.clear();
}

void TestTransportBasics()
{
    C_NetTransport t;
    CHECK(!t.m_running && t.m_callback == nullptr && t.m_connections.empty() && t.m_session == 0);
    auto *a = new C_NetConnection(1, 10, &t.m_socket), *b = new C_NetConnection(2, 20, &t.m_socket),
         *c = new C_NetConnection(3, 10, &t.m_socket);
    a->m_port = 100;
    b->m_port = 200;
    c->m_port = 300;
    t.m_session = 1;
    t.Add(a);
    t.Add(b);
    t.Add(c);
    CHECK(a->m_session == 1 && t.m_connections.size() == 3);
    CHECK(t.FindById(2) == b && t.FindById(4) == nullptr && t.FindByAddress(10, 300) == c &&
          t.FindByAddress(10, 200) == nullptr && t.FindByAddress(20, 200) == b);
    CHECK(t.Lock() == &t);
    t.Unlock();
    // Removed: the last moved into its place (not deleted).
    t.Remove(a);
    CHECK(t.m_connections.size() == 2 && t.m_connections[0] == c && t.m_connections[1] == b);
    t.Remove(a);
    CHECK(t.m_connections.size() == 2);
    t.Remove(b);
    CHECK(t.m_connections.size() == 1 && t.m_connections[0] == c);
    t.Remove(c);
    CHECK(t.m_connections.empty());
    t.Add(a);
    t.Add(c);
    t.Remove(a);
    CHECK(t.m_connections.size() == 1 && t.m_connections[0] == c);
    t.Remove(c);
    t.Add(a);
    t.Add(b);
    g_deletedCount = 0;
    g_trackDeletes = true;
    t.DeleteConnections();
    g_trackDeletes = false;
    CHECK(t.m_connections.empty() && WasDeleted(a) && WasDeleted(b));
    delete c;
    // Not running: no flush.
    auto *d = new C_NetConnection(4, 10, &t.m_socket);
    t.Add(d);
    d->m_lastSend = 0;
    t.FlushAll(true);
    CHECK(d->m_lastSend == 0);
    t.m_running = true;
    t.FlushAll(false);
    CHECK(d->m_lastSend != 0 && t.m_lastFlush != 0);
    // The tick: a flush after 500 ms (a ping when due).
    d->m_lastSend = 0;
    t.m_lastFlush = static_cast<int>(GetPerfTimeMs());
    t.Tick();
    CHECK(d->m_lastSend == 0);
    t.m_lastFlush -= 501;
    d->m_lastPing -= 2001;
    t.Tick();
    CHECK(d->m_lastSend != 0 && d->m_pingId == 1);
    t.m_running = false;
    t.DeleteConnections();
}

void TestTransportLoopback()
{
    C_NetTransport ta, tb;
    ta.m_socket.Open(-1);
    tb.m_socket.Open(-1);
    ta.m_running = tb.m_running = true;
    const int portA = BoundPort(ta.m_socket), portB = BoundPort(tb.m_socket);
    auto *ca = new C_NetConnection(7, 0, &ta.m_socket);
    auto *cb = new C_NetConnection(7, Loopback(), &tb.m_socket);
    cb->m_port = portA;
    ta.Add(ca);
    tb.Add(cb);
    ta.m_callback = TestDeliver;
    g_delivered.clear();
    // A reliable packet: delivered with its kind and channel; the sender's address learnt.
    const char hello[] = "hello";
    cb->Send(hello, 5, 3, 1);
    tb.FlushAll(true);
    CHECK(Pump(ta, [] { return !g_delivered.empty(); }));
    CHECK(g_delivered.size() == 1 && g_delivered[0].connection == 7 && g_delivered[0].size == 5 &&
          g_delivered[0].kind == 3 && g_delivered[0].flags == 1 &&
          std::memcmp(g_delivered[0].data.data(), "hello", 5) == 0);
    CHECK(ca->m_port == portB && ca->m_address == Loopback() && ta.m_lastReceive != 0 && ca->m_lastReceive != 0);
    // Acknowledged back: the sender drops it.
    auto *reliable = static_cast<C_NetReliableChannel *>(cb->m_channels[1]);
    CHECK(reliable->m_pending.size() == 1);
    ta.FlushAll(true);
    CHECK(Pump(tb, [&] { return reliable->m_acked == 0x400000; }));
    cb->ResendAll();
    CHECK(reliable->m_pending.empty());
    // The plain and the ordered channels; several in one datagram, in channel order.
    g_delivered.clear();
    cb->Send("p", 1, 1, 0);
    cb->Send("o1", 2, 2, 2);
    cb->Send("o2", 2, 2, 2);
    tb.FlushAll(false);
    CHECK(Pump(ta, [] { return g_delivered.size() == 3; }));
    CHECK(g_delivered.size() == 3 && g_delivered[0].flags == 0 && g_delivered[1].data[1] == '1' &&
          g_delivered[2].data[1] == '2' && g_delivered[2].flags == 2);
    // A ping: echoed; the round trip measured.
    cb->SendPing();
    CHECK(Pump(ta, [] { return true; }));
    CHECK(Pump(tb, [&] { return cb->m_roundTrip != 0; }));
    // Another session's packets: dropped.
    g_delivered.clear();
    cb->m_session = 1;
    cb->Send("x", 1, 1, 0);
    tb.FlushAll(false);
    Pump(ta, [] { return false; });
    CHECK(g_delivered.empty());
    cb->m_session = 0;
    // An unknown connection id: ignored.
    cb->m_id = 8;
    cb->Send("y", 1, 1, 0);
    tb.FlushAll(false);
    Pump(ta, [] { return false; });
    CHECK(g_delivered.empty());
    cb->m_id = 7;
    // No callback: received, not handed on.
    ta.m_callback = nullptr;
    cb->Send("z", 1, 1, 0);
    tb.FlushAll(false);
    Pump(ta, [] { return false; });
    CHECK(g_delivered.empty());
    ta.m_callback = TestDeliver;
    // A late packet of the plain channel: dropped; of a reliable one: handed on.
    C_NetPacket *late = MakePacket(1, 0x4C, 6);
    late->m_flags = NET_PACKET_LATE;
    late->AddRef();
    ca->m_channels[0]->m_incoming.push_back(late);
    C_NetPacket *lateR = MakePacket(1, 0x52, 6);
    lateR->m_flags = 1 | NET_PACKET_LATE;
    lateR->AddRef();
    ca->m_channels[1]->m_incoming.push_back(lateR);
    cb->Send("w", 1, 1, 0);
    tb.FlushAll(false);
    CHECK(Pump(ta, [] { return !g_delivered.empty(); }));
    bool sawLate = false, sawLateR = false;
    for (auto &d : g_delivered) {
        sawLate |= d.data[0] == 0x4C;
        sawLateR |= d.data[0] == 0x52;
    }
    CHECK(!sawLate && sawLateR);
    // A big packet: fragments, flushed by the sender's receive pass.
    g_delivered.clear();
    ca->m_lastReceive = 0;
    std::vector<uint8_t> big(800, 0x5E);
    cb->Send(big.data(), 800, 4, 0);
    tb.FlushAll(false);
    bool bigCame = false;
    for (int tries = 0; tries < 100 && !bigCame; ++tries) {
        tb.ReceiveAll();
        ta.ReceiveAll();
        for (auto &d : g_delivered)
            bigCame |= d.size == 800 && d.data[799] == 0x5E;
    }
    CHECK(bigCame && ca->m_lastReceive != 0);
    int received = 0, sent = 0;
    ta.m_socket.TakeCounters(&received, &sent);
    CHECK(received > 0 && sent > 0);
    ta.m_running = tb.m_running = false;
    ta.m_socket.Close();
    tb.m_socket.Close();
    ta.DeleteConnections();
    tb.DeleteConnections();
}

void TestTransportThread()
{
    // Started: its thread ticks and receives; stopped within a second.
    C_NetTransport t;
    auto *old = new C_NetConnection(5, 1, &t.m_socket);
    t.Add(old);
    t.m_session = 3;
    t.m_stop = true;
    t.Start(-1);
    CHECK(t.m_running && !t.m_stop && t.m_socket.m_socket != static_cast<uintptr_t>(-1) && t.m_stopped &&
          t.m_connections.empty() && t.m_session == 0 && t.m_lastFlush != 0 && t.m_lastReceive != 0);
    delete old;
    // The thread ticks: a due ping sent.
    auto *c = new C_NetConnection(6, Loopback(), &t.m_socket);
    c->m_port = 9;
    c->m_lastPing -= 5000;
    t.Add(c);
    for (int tries = 0; tries < 50 && c->m_pingId == 0; ++tries)
        Sleep(10);
    CHECK(c->m_pingId != 0);
    t.Stop();
    t.DeleteConnections();
    CHECK(!t.m_running && t.m_stop && t.m_socket.m_socket == static_cast<uintptr_t>(-1));
    t.Stop();
}
