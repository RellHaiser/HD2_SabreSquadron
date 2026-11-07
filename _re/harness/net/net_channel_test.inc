// (Part 10c-i.) The transport's packets, pools, channels and messages (net/net_channel.cpp). Included by
// net_test.cpp.

struct T_Counted : C_NetRefCounted {
    explicit T_Counted(bool *gone) : gone(gone) {}
    ~T_Counted() override { *gone = true; }
    bool *gone;
};

C_NetPacket *MakePacket(int size, uint8_t fill, uint32_t kind = 5)
{
    C_NetPacket *p = g_netPool.AllocPacket();
    p->m_size = size;
    p->m_kind = kind;
    p->m_data = g_netPool.AllocBuffer(static_cast<size_t>(size));
    std::memset(p->m_data, fill, static_cast<size_t>(size));
    return p;
}

void TestUnwrap()
{
    struct {
        uint32_t seq, ref, want;
    } cases[] = {
        {5, 3, 5},                                    // same epoch
        {0x7FFFFE, 0x800003, 0x7FFFFE},               // ref just past a wrap, seq from before it: previous epoch
        {0x000003, 0x7FFFFE, 0x800003},               // ref just before a wrap, seq after it: next epoch
        {0x400000, 0x1000000, 0x1400000},
        {0x1FFFFE, 0x5FFFFE, 0x1FFFFE + 0x800000},     // ref at the upper third, seq in the lower: next epoch
        {0x1FFFFF, 0x5FFFFE, 0x1FFFFF},                // (not below 0x1FFFFF: same epoch)
        {0x5FFFFE, 0x1FFFFE, 0xFFDFFFFEu},     // ref in the lower third, seq in the upper: previous epoch
        {0x5FFFFD, 0x1FFFFE, 0x5FFFFD},
        {0x5FFFFE, 0x1FFFFF, 0x5FFFFE},
        {0xFF800005, 0x00000003, 0x00000005},          // only the low 23 bits of seq count
    };
    for (auto &c : cases) {
        uint32_t s = c.seq;
        UnwrapSequence(&s, c.ref);
        CHECK(s == c.want);
    }
    // The wrap below zero: the previous epoch of 0 is 0xFF800000.
    uint32_t s = 0x7FFFF0;
    UnwrapSequence(&s, 3);
    CHECK(s == 0xFFFFFFF0u);
}

void TestRefCounted()
{
    bool gone = false;
    auto *c = new T_Counted(&gone);
    c->AddRef();
    c->AddRef();
    CHECK(c->m_refs == 2);
    c->Release();
    CHECK(!gone && c->m_refs == 1);
    c->Release();
    CHECK(gone);
    gone = false;
    c = new T_Counted(&gone);
    c->Release();
    CHECK(gone);
}

void TestPool()
{
    // 16 of each made at first; last freed first; grown by 16 when empty.
    CHECK(g_netPool.m_buffers.growBy == 16 && g_netPool.m_packets.growBy == 16);
    const int buffers = g_netPool.m_buffers.count, packets = g_netPool.m_packets.count;
    CHECK(buffers >= 16 && packets >= 16 && buffers == static_cast<int>(g_netPool.m_buffers.items.size()));
    void *a = g_netPool.AllocBuffer(10);
    void *b = g_netPool.AllocBuffer(1000);
    CHECK(a && b && a != b && g_netPool.m_buffers.count == buffers - 2);
    g_netPool.FreeBuffer(a, 10);
    CHECK(g_netPool.AllocBuffer(1) == a);
    g_netPool.FreeBuffer(a, 1000);
    g_netPool.FreeBuffer(b, 0);
    CHECK(g_netPool.m_buffers.count == buffers);
    // Bigger than 1000: malloc'ed and freed, the pool untouched; a null buffer not kept.
    void *big = g_netPool.AllocBuffer(1001);
    CHECK(big && g_netPool.m_buffers.count == buffers);
    std::memset(big, 1, 1001);
    g_netPool.FreeBuffer(big, 1001);
    g_netPool.FreeBuffer(nullptr, 5);
    CHECK(g_netPool.m_buffers.count == buffers);
    // Emptied: grown by 16.
    std::vector<void *> taken;
    for (int i = 0; i < buffers; ++i)
        taken.push_back(g_netPool.AllocBuffer(4));
    CHECK(g_netPool.m_buffers.count == 0);
    void *more = g_netPool.AllocBuffer(4);
    CHECK(g_netPool.m_buffers.count == 15);
    taken.push_back(more);
    for (void *p : taken)
        g_netPool.FreeBuffer(p, 4);
    CHECK(g_netPool.m_buffers.count == buffers + 16);
    // Packets likewise; a released packet goes back cleared (its data back too).
    C_NetPacket *p = g_netPool.AllocPacket();
    CHECK(g_netPool.m_packets.count == packets - 1 && p->m_refs == 0);
    p->AddRef();
    p->m_size = 8;
    p->m_kind = 3;
    p->m_flags = 2;
    p->m_session = 1;
    p->m_data = g_netPool.AllocBuffer(8);
    void *data = p->m_data;
    const int before = g_netPool.m_buffers.count;
    p->Release();
    CHECK(g_netPool.m_packets.count == packets && g_netPool.m_buffers.count == before + 1 && p->m_data == nullptr &&
          p->m_size == 0 && p->m_kind == 0 && p->m_flags == 0 && p->m_session == 0 && p->m_refs == 0);
    CHECK(g_netPool.AllocBuffer(8) == data && g_netPool.AllocPacket() == p);
    g_netPool.FreeBuffer(data, 8);
    g_netPool.FreePacket(p);
    g_netPool.FreePacket(nullptr);
    CHECK(g_netPool.m_packets.count == packets);
    // Released unheld: back with its count 0; deleted with data: the data back to the pool.
    p = g_netPool.AllocPacket();
    p->Release();
    CHECK(p->m_refs == 0 && g_netPool.m_packets.count == packets);
    {
        auto *own = new C_NetPacket;
        own->m_size = 4;
        own->m_data = g_netPool.AllocBuffer(4);
        const int held = g_netPool.m_buffers.count;
        delete own;
        CHECK(g_netPool.m_buffers.count == held + 1);
    }
    // Held twice: the first release keeps it.
    p = g_netPool.AllocPacket();
    p->AddRef();
    p->AddRef();
    p->Release();
    CHECK(p->m_refs == 1 && g_netPool.m_packets.count == packets - 1);
    p->Release();
    CHECK(g_netPool.m_packets.count == packets);
    // A pool of its own: made and destroyed (16 of each; the packets deleted with it).
    {
        C_NetPool own;
        CHECK(own.m_buffers.count == 16 && own.m_packets.count == 16);
        C_NetPacket *q = own.AllocPacket();
        g_deletedCount = 0;
        g_trackDeletes = true;
        own.FreePacket(q);
        own.~C_NetPool();
        g_trackDeletes = false;
        CHECK(WasDeleted(q) && g_deletedCount >= 32);
        new (&own) C_NetPool;
    }
}

void TestPacket()
{
    C_NetPacket *p = MakePacket(7, 0x5A, 0x2B);
    p->m_flags = 2;
    p->m_sequence = 0x123456;
    p->m_session = 1;
    static_cast<uint8_t *>(p->m_data)[6] = 0x77;
    C_NetMessage msg;
    p->Write(&msg);
    // 48 bits of header, then the bytes.
    CHECK(msg.BitPosition() == 48u + 56u);
    Rewind(msg);
    CHECK(Bits(msg, 2) == 2 && Bits(msg, 16) == 7 && Bits(msg, 6) == 0x2B && Bits(msg, 23) == 0x123456 && Bits(msg, 1) == 1);
    CHECK(Bits(msg, 8) == 0x5A && Bits(msg, 32) == 0x5A5A5A5Au && Bits(msg, 8) == 0x5A && Bits(msg, 8) == 0x77);
    Rewind(msg);
    C_NetPacket *q = g_netPool.AllocPacket();
    q->Read(&msg);
    CHECK(q->m_flags == 2 && q->m_size == 7 && q->m_kind == 0x2B && q->m_sequence == 0x123456 && q->m_session == 1 && q->m_data);
    CHECK(std::memcmp(q->m_data, p->m_data, 7) == 0 && msg.BitPosition() == 48u + 56u);
    // Sizes multiple of four and none. (The size is read signed: a negative one would copy past the stream, as in
    // the original.)
    C_NetPacket *r = MakePacket(8, 0x11);
    C_NetMessage m2;
    r->Write(&m2);
    CHECK(m2.BitPosition() == 48u + 64u);
    C_NetPacket *z = MakePacket(0, 0);
    C_NetMessage m3;
    z->Write(&m3);
    m3.WriteBits(0x3, 2);
    CHECK(m3.BitPosition() == 50u);
    Rewind(m3);
    C_NetPacket *z2 = g_netPool.AllocPacket();
    z2->Read(&m3);
    CHECK(z2->m_size == 0 && Bits(m3, 2) == 3);
    // The data's bytes at the byte the position falls in (whole bytes: after 3 bits, the 4th..7th written over).
    C_NetMessage m5;
    m5.WriteBits(0x7, 3);
    const uint8_t bytes[3] = {0xA1, 0xB2, 0xC3};
    m5.WriteRawBytes(bytes, 3);
    CHECK(m5.BitPosition() == 3u + 24u);
    Rewind(m5);
    CHECK(Bits(m5, 8) == 0xA1 && Bits(m5, 8) == 0xB2);
    uint8_t back[3] = {};
    Rewind(m5);
    Bits(m5, 3);
    m5.ReadRawBytes(back, 3);
    CHECK(back[0] == 0xA1 && back[2] == 0xC3 && m5.BitPosition() == 27u);
    // Four or more: whole words moved on.
    C_NetMessage m6;
    const uint8_t five[5] = {1, 2, 3, 4, 5};
    m6.WriteRawBytes(five, 5);
    CHECK(m6.BitPosition() == 40u && m6.m_wordIndex == 1 && m6.m_bitIndex == 8);
    for (C_NetPacket *x : {p, q, r, z, z2}) {
        x->AddRef();
        x->Release();
    }
}

void TestChannel()
{
    C_NetChannel c;
    CHECK(c.m_sent == 0x3FFFFF && c.m_received == 0x3FFFFF && c.PopOutgoing() == nullptr && c.PopReceived() == nullptr);
    C_NetPacket *a = MakePacket(1, 1), *b = MakePacket(1, 2);
    c.Send(a);
    c.Send(b);
    CHECK(a->m_sequence == 0x400000 && b->m_sequence == 0x400001 && a->m_refs == 1 && c.m_sent == 0x400001);
    CHECK(c.PopOutgoing() == a && c.PopOutgoing() == b && c.PopOutgoing() == nullptr);
    // Received: newer moves the newest; older by up to 9 marked late; older by more taken as the newest.
    C_NetPacket *r[5];
    const uint32_t seqs[5] = {0x400005, 0x400003, 0x400006, 0x3FFFFC, 0x400010};
    for (int i = 0; i < 5; ++i) {
        r[i] = MakePacket(1, 0);
        r[i]->m_sequence = seqs[i];
        c.Receive(r[i]);
    }
    CHECK(c.m_received == 0x400010 && r[0]->m_flags == 0 && r[1]->m_flags == NET_PACKET_LATE && r[2]->m_flags == 0 &&
          r[3]->m_flags == 0 && r[4]->m_flags == 0 && r[1]->m_refs == 1);
    for (int i = 0; i < 5; ++i)
        CHECK(c.PopReceived() == r[i]);
    C_NetPacket *e = MakePacket(1, 0);
    e->m_sequence = 0x400010 - 9;
    c.Receive(e);
    C_NetPacket *f = MakePacket(1, 0);
    f->m_sequence = 0x400010 - 10;
    c.Receive(f);
    CHECK(e->m_flags == NET_PACKET_LATE && f->m_flags == 0 && c.m_received == 0x400010 - 10);
    C_NetPacket *g = MakePacket(1, 0);
    g->m_sequence = 0x400006;
    c.Receive(g);
    CHECK(g->m_flags == NET_PACKET_LATE && c.m_received == 0x400006);
    // The number widened around the newest (a later epoch).
    c.m_received = 0x1000005;
    C_NetPacket *w = MakePacket(1, 0);
    w->m_sequence = 3;
    c.Receive(w);
    CHECK(w->m_flags == NET_PACKET_LATE && c.m_received == 0x1000005);
    c.m_received = 0x400006;
    C_NetPacket *k = MakePacket(1, 0);
    k->m_sequence = 0x400007;
    c.Receive(k);
    CHECK(k->m_flags == 0 && c.m_received == 0x400007);
    // The base's acks do nothing.
    C_NetMessage msg;
    c.WriteAck(&msg);
    c.ReadAck(&msg);
    c.Resend();
    c.Acknowledge();
    CHECK(msg.BitPosition() == 0u);
    // Destroyed: the packets to send released (the received ones only forgotten).
    C_NetPacket *h = MakePacket(1, 0);
    h->AddRef();
    {
        C_NetChannel d;
        d.Send(h);
        CHECK(h->m_refs == 2);
    }
    CHECK(h->m_refs == 1);
    h->Release();
}

void TestReliable()
{
    C_NetReliableChannel c;
    CHECK(c.m_delivered == 0x3FFFFF && c.m_acked == 0x3FFFFF && c.m_resendTime == 200 && c.m_pending.empty());
    const int now = static_cast<int>(GetPerfTimeMs());
    C_NetPacket *a = MakePacket(1, 1), *b = MakePacket(1, 2);
    c.Send(a);
    c.Send(b);
    CHECK(a->m_refs == 2 && c.m_outgoing.size() == 2 && c.m_pending.size() == 2);
    CHECK(c.m_pending.front().packet == a && c.m_pending.front().interval == 200 &&
          c.m_pending.front().time >= now + 200 && c.m_pending.front().time <= now + 300);
    c.PopOutgoing()->Release();
    c.PopOutgoing()->Release();
    // Not due: nothing.
    c.Resend();
    CHECK(c.m_outgoing.empty());
    // Due: queued again, the interval doubled (capped at 5 s), 7 at most a call.
    c.m_pending.front().time = now - 1;
    c.m_pending.back().time = now - 1;
    c.m_pending.back().interval = 3000;
    c.Resend();
    CHECK(c.m_outgoing.size() == 2 && a->m_refs == 2 && c.m_pending.front().interval == 400 &&
          c.m_pending.back().interval == 5000 && c.m_pending.front().time >= now + 200);
    c.PopOutgoing()->Release();
    c.PopOutgoing()->Release();
    std::vector<C_NetPacket *> many;
    for (int i = 0; i < 9; ++i) {
        many.push_back(MakePacket(1, 0));
        c.Send(many.back());
        c.PopOutgoing()->Release();
    }
    for (auto &pending : c.m_pending)
        pending.time = now - 10;
    c.Resend();
    CHECK(c.m_outgoing.size() == 7);
    while (C_NetPacket *p = c.PopOutgoing())
        p->Release();
    // Acknowledged: dropped from the front (released).
    C_NetMessage ack;
    ack.WriteBits(0x400000, 23);
    ack.WriteBits(1, 1);
    Rewind(ack);
    c.ReadAck(&ack);
    CHECK(c.m_acked == 0x400000 && ack.BitPosition() == 24u);
    c.Resend();
    CHECK(c.m_pending.front().packet == b && a->m_refs == 0);
    CHECK(c.m_pending.size() == 10);
    // An older acknowledgement does not go back.
    C_NetMessage old;
    old.WriteBits(0x3FFFF0, 23);
    old.WriteBits(0, 1);
    Rewind(old);
    c.ReadAck(&old);
    CHECK(c.m_acked == 0x400000);
    // Everything acknowledged.
    C_NetMessage all;
    all.WriteBits(0x400010, 23);
    all.WriteBits(0, 1);
    Rewind(all);
    c.ReadAck(&all);
    c.Resend();
    CHECK(c.m_pending.empty());
    // Received: repeats and the delivered ones dropped (released); the in-order run counted as delivered.
    C_NetPacket *r[4];
    const uint32_t seqs[4] = {0x400001, 0x400000, 0x400001, 0x400003};
    for (int i = 0; i < 4; ++i)
        r[i] = MakePacket(1, 0);
    for (int i = 0; i < 4; ++i) {
        r[i]->AddRef();
        r[i]->m_sequence = seqs[i];
        c.Receive(r[i]);
    }
    CHECK(c.m_unordered.size() == 3 && c.m_incoming.size() == 3 && r[2]->m_refs == 0 && r[0]->m_refs == 3);
    c.Acknowledge();
    CHECK(c.m_delivered == 0x400001 && c.m_unordered.size() == 1 && r[0]->m_refs == 2 && r[1]->m_refs == 2);
    C_NetMessage w;
    c.WriteAck(&w);
    Rewind(w);
    CHECK(Bits(w, 23) == 0x400001 && Bits(w, 1) == 0);
    C_NetPacket *old2 = MakePacket(1, 0);
    old2->AddRef();
    old2->m_sequence = 0x400001;
    c.Receive(old2);
    CHECK(old2->m_refs == 0 && c.m_incoming.size() == 3);
    C_NetPacket *gap = MakePacket(1, 0);
    gap->m_sequence = 0x400002;
    c.Receive(gap);
    c.Acknowledge();
    CHECK(c.m_delivered == 0x400003 && c.m_unordered.empty());
    // A later epoch: the number widened around the newest received and kept so; the acknowledgement likewise.
    {
        C_NetReliableChannel e;
        e.m_received = 0x1000005;
        e.m_delivered = 0x1000000;
        C_NetPacket *x = MakePacket(1, 0);
        x->m_sequence = 3;
        e.Receive(x);
        CHECK(x->m_sequence == 0x1000003 && e.m_unordered.size() == 1);
        e.m_acked = 0x1000005;
        C_NetMessage m;
        m.WriteBits(7, 23);
        m.WriteBits(0, 1);
        Rewind(m);
        e.ReadAck(&m);
        CHECK(e.m_acked == 0x1000007);
        C_NetMessage m2;
        e.WriteAck(&m2);
        CHECK(m2.BitPosition() == 24u);
        Rewind(m2);
        CHECK(Bits(m2, 23) == 0x000000 && Bits(m2, 1) == 0);
    }
    // Destroyed: the held packets released.
    C_NetPacket *h = MakePacket(1, 0);
    h->AddRef();
    {
        C_NetReliableChannel d;
        d.Send(h);
        CHECK(h->m_refs == 3);
    }
    CHECK(h->m_refs == 1);
    h->Release();
    while (C_NetPacket *p = c.PopReceived())
        p->Release();
    for (int i = 0; i < 4; ++i)
        if (r[i]->m_refs > 0)
            r[i]->Release();
}

void TestOrdered()
{
    C_NetOrderedChannel c;
    CHECK(c.m_next == 0x3FFFFF && c.m_resendTime == 200);
    C_NetPacket *p[3];
    for (int i = 0; i < 3; ++i)
        p[i] = MakePacket(1, 0);
    p[0]->m_sequence = 0x400001;
    p[1]->m_sequence = 0x400000;
    p[2]->m_sequence = 0x400002;
    c.Receive(p[0]);
    c.Receive(p[1]);
    CHECK(p[1]->m_flags == NET_PACKET_LATE);
    CHECK(c.PopReceived() == p[1] && p[1]->m_flags == 0 && c.m_next == 0x400000 && c.m_incoming.size() == 1);
    CHECK(c.PopReceived() == p[0] && c.PopReceived() == nullptr);
    c.Receive(p[2]);
    CHECK(c.PopReceived() == p[2] && c.m_next == 0x400002);
}

void TestMessages()
{
    // The header: type and connection, 8 bits each.
    C_NetHeader h;
    CHECK(h.m_type == 0 && h.m_connection == 0);
    h.m_type = 0x81;
    h.m_connection = 0x42;
    C_NetMessage msg;
    h.Write(&msg);
    h.WriteBody(&msg);
    CHECK(msg.BitPosition() == 16u);
    Rewind(msg);
    C_NetHeader g;
    g.Read(&msg);
    g.ReadBody(&msg);
    CHECK(g.m_type == 0x81 && g.m_connection == 0x42);
    // The packet list: the count, each packet; read from the pool; released with it.
    C_NetPacketList list;
    CHECK(list.m_type == 1 && list.Pop() == nullptr);
    C_NetPacket *a = MakePacket(3, 0xAA), *b = MakePacket(2, 0xBB);
    a->AddRef();
    b->AddRef();
    list.Add(a);
    list.Add(b);
    C_NetMessage m2;
    list.Write(&m2);
    list.WriteBody(&m2);
    Rewind(m2);
    C_NetPacketList back;
    back.Read(&m2);
    CHECK(back.m_type == 1);
    back.ReadBody(&m2);
    CHECK(back.m_packets.size() == 2 && back.m_packets.front()->m_size == 3 &&
          static_cast<uint8_t *>(back.m_packets.back()->m_data)[1] == 0xBB);
    CHECK(list.Pop() == a && list.m_packets.size() == 1);
    a->Release();
    {
        C_NetPacketList gone;
        gone.Add(b);
    }
    CHECK(list.m_packets.size() == 1 && b->m_refs == 0);
    list.m_packets.clear();
    // The ping: the time sent; its echo gives the round trip (at least 1).
    C_NetPing ping;
    CHECK(ping.m_type == 2 && ping.m_reply == 0);
    ping.m_id = 9;
    C_NetMessage m3;
    const uint32_t sent = static_cast<uint32_t>(GetPerfTimeMs());
    ping.WriteBody(&m3);
    CHECK(m3.BitPosition() == 41u);
    m3.WriteBits(1, 1);
    Rewind(m3);
    C_NetPing got;
    got.ReadBody(&m3);
    CHECK(m3.BitPosition() == 41u);
    CHECK(got.m_reply == 0 && got.m_id == 9 && got.m_time >= sent && got.m_time <= sent + 50);
    got.m_reply = 1;
    got.m_time = sent - 100;
    C_NetMessage m4;
    got.WriteBody(&m4);
    CHECK(m4.BitPosition() == 41u);
    m4.WriteBits(1, 1);
    Rewind(m4);
    C_NetPing echo;
    echo.ReadBody(&m4);
    CHECK(m4.BitPosition() == 41u);
    CHECK(echo.m_reply == 1 && echo.m_id == 9 && echo.m_time >= 100 && echo.m_time <= 200);
    // An echo of now: 1.
    C_NetMessage m5;
    m5.WriteBits(1, 1);
    m5.WriteBits(0, 8);
    m5.WriteBits(static_cast<uint32_t>(GetPerfTimeMs()), 32);
    Rewind(m5);
    echo.ReadBody(&m5);
    CHECK(echo.m_time >= 1 && echo.m_time <= 20);
    // The empty message: no body.
    C_NetEmptyMessage e;
    C_NetMessage m6;
    e.WriteBody(&m6);
    e.ReadBody(&m6);
    CHECK(m6.BitPosition() == 0u);
}
