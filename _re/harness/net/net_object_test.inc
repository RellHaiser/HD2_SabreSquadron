// (Part 10b.) The replicated objects: the properties, the objects, the replication (net/net_object.cpp). Included
// by net_test.cpp.

// A message of the tests: its payload (8 bits) after its type (4 bits; the factory reads the type, 0xF ends).
struct T_Message : C_NetObjectMessage {
    explicit T_Message(int t, int payload = 0) : C_NetObjectMessage(t), payload(payload) { ++alive; }
    ~T_Message() override { --alive; }
    void Write(C_BitStream *stream) override
    {
        stream->WriteBits(static_cast<uint32_t>(type), 4);
        stream->WriteBits(static_cast<uint32_t>(payload), 8);
    }
    void Read(C_BitStream *stream) override { payload = static_cast<int>(stream->ReadBits(8)); }
    int payload;
    static int alive;
};
int T_Message::alive;

int g_factoryCalls, g_factoryWrites;
void *g_factoryContext;
C_NetObjectMessage *__fastcall TestFactory(void *context, C_BitStream *stream, bool write)
{
    ++g_factoryCalls;
    g_factoryContext = context;
    if (write) {
        ++g_factoryWrites;
        stream->WriteBits(0xF, 4);
        return nullptr;
    }
    const uint32_t t = stream->ReadBits(4);
    return t == 0xF ? nullptr : new T_Message(static_cast<int>(t));
}

int g_stateWrites, g_stateReads;
void *g_stateContext;
void __fastcall TestWriteState(void *context, C_BitStream *stream)
{
    ++g_stateWrites;
    g_stateContext = context;
    stream->WriteBits(0x5A, 8);
}
void __fastcall TestReadState(void *context, C_BitStream *stream)
{
    ++g_stateReads;
    g_stateContext = context;
    CHECK(stream->ReadBits(8) == 0x5A);
}
NetMessageFactory MakeTestFactory()
{
    return TestFactory;
}
NetStateHandler MakeTestReader()
{
    return TestReadState;
}

void TestMessage()
{
    T_Message m(3, 9);
    CHECK(m.GetType() == 3 && m.sentTo == 0 && m.refCount == 0 && m.age == 0.0f && !m.fromClient);
    CHECK(m.m_read[0] == nullptr && m.m_write[2] == nullptr);
    CHECK(m.MarkSent(2) && !m.MarkSent(2) && m.sentTo == 4 && m.MarkSent(0) && m.sentTo == 5);
    CHECK(!m.SentToAll());
    m.sentTo = -1;
    CHECK(m.SentToAll() && !m.MarkSent(31));
    m.sentTo = 0x7FFFFFFF;
    CHECK(!m.SentToAll() && m.MarkSent(31) && m.SentToAll());
    m.Update(0.6f);
    CHECK(!m.Expired());
    m.Update(0.4f);
    CHECK(m.age == 1.0f && !m.Expired());
    m.Update(0.01f);
    CHECK(m.Expired());
    m.Refresh();
}

void TestProperty()
{
    MakeQuantizations();
    C_NetProperty p(5);
    CHECK(p.GetIndex() == 5 && p.GetFlags() == 0xC7);
    CHECK(Same3(p.m_lastPosition[0], 0, 0, 0) && Same3(p.m_lastDirection[31], 0, 0, 0));
    CHECK(p.GetTrack().m_flags == 7 && p.GetDirTrack().m_flags == 3);
    // The quantisations: all three stored (null too) and handed down (the direction track keeps its own).
    p.SetReadQuantizations(&g_value, nullptr, &g_rate);
    p.SetWriteQuantizations(&g_delta, &g_value, nullptr);
    CHECK(p.m_read[0] == &g_value && p.m_read[1] == nullptr && p.m_read[2] == &g_rate);
    CHECK(p.m_write[0] == &g_delta && p.m_write[1] == &g_value && p.m_write[2] == nullptr);
    CHECK(p.GetTrack().m_read[0] == &g_value && p.GetTrack().m_read[2] == &g_rate && p.GetTrack().m_read[1] == nullptr);
    CHECK(p.GetTrack().m_write[0] == &g_delta && p.GetTrack().m_write[1] == &g_value);
    CHECK(p.GetDirTrack().m_read[0] == &g_netAngleQuantization && p.GetDirTrack().m_write[0] == &g_netAngleQuantization);
    p.SetReadQuantizations(nullptr, nullptr, nullptr);
    CHECK(p.m_read[0] == nullptr && p.GetTrack().m_read[0] == &g_value);
    // The flags: the word to both (the direction track drops its rate flags); a field.
    p.SetFlags(0x3B000 | 7);
    CHECK(p.GetFlags() == 0x3B007 && p.GetTrack().m_flags == 0x3B007 && p.GetDirTrack().m_flags == (0x3B007u & 0xFFFC3FFFu));
    p.SetField(2, 0x01);
    CHECK(p.GetFlags() == 0x1007 && p.GetTrack().m_flags == 0x1007 && p.GetDirTrack().m_flags == 0x1007);
    p.SetField(0, 5);
    CHECK(p.GetFlags() == 0x1005 && p.GetTrack().m_flags == 0x1005);
    p.SetField(1, 0x3F);
    CHECK(p.GetFlags() == 0x1FC5 && p.GetDirTrack().m_flags == 0x1FC5);
    p.SetField(3, 0x21);
    CHECK(p.GetFlags() == 0x841FC5);
    p.SetField(1, 0);
    CHECK(p.GetFlags() == 0x841005);
    p.SetLag(0.25f);
    CHECK(p.GetTrack().m_lag == 0.25f && p.GetDirTrack().m_lag == 0.25f);
    // Update and Refresh: both tracks.
    p.GetTrack().m_current = {0, 0, 0, 0};
    p.GetTrack().m_target = {0.5f, 0, 0, 0};
    p.GetTrack().m_speed = 1.0f;
    p.GetDirTrack().m_current = {0, 0, 0, 0};
    p.GetDirTrack().m_target = {0, 0.5f, 0, 0};
    p.GetDirTrack().m_speed = 1.0f;
    p.Update(0.1f);
    CHECK(Near(p.GetTrack().m_current.x, 0.1f, 1e-6f) && Near(p.GetDirTrack().m_current.y, 0.1f, 1e-6f));
    p.Refresh();
}

void TestPropertyWrite()
{
    MakeQuantizations();
    g_netServerSide = false;
    C_NetProperty p(1), q(1);
    p.SetReadQuantizations(&g_value, &g_delta, &g_rate);
    p.SetWriteQuantizations(&g_value, &g_delta, &g_rate);
    q.SetReadQuantizations(&g_value, &g_delta, &g_rate);
    q.SetWriteQuantizations(&g_value, &g_delta, &g_rate);
    p.GetTrack().SetValue({1, 2, 3, 0});
    p.GetDirTrack().SetDirection({0, 0, 1, 0}, 0.5f);
    // To player 4: the position, the direction; their last values for that player kept.
    C_NetMessage msg;
    p.Write(&msg, 4, false);
    CHECK(Same3(p.m_lastPosition[4], 1, 2, 3) && Same3(p.m_lastPosition[3], 0, 0, 0));
    CHECK(Near(p.m_lastDirection[4].x, 1.5707964f) && p.m_lastDirection[4].z == 0.5f && Same3(p.m_lastDirection[5], 0, 0, 0));
    Rewind(msg);
    q.Read(&msg);
    CHECK(Near(q.GetTrack().m_net.x, 1, 0.01f) && Near(q.GetTrack().m_net.z, 3, 0.01f));
    // (The direction track's flags 3: yaw and pitch sent, not the angle.)
    CHECK(Near(q.GetDirTrack().m_net.x, 1.5707964f, 0.01f) && q.GetDirTrack().m_net.z == 0.0f);
    CHECK(msg.BitPosition() == msg.m_wordIndex * 32 + msg.m_bitIndex);
    // A delta for the same player: unchanged: two 0 bits (and the turn sign).
    C_NetMessage msg2;
    p.Write(&msg2, 4, true);
    CHECK(msg2.BitPosition() == 1u + 1u + 2u);
    // Another player: absolute.
    C_NetMessage msg3;
    p.Write(&msg3, 7, true);
    CHECK(msg3.BitPosition() > 10u && Same3(p.m_lastPosition[7], 1, 2, 3));
}

void TestObjectBasics()
{
    MakeQuantizations();
    C_NetObject o;
    CHECK(o.GetId() == -1 && o.m_sendMask == 0 && o.GetLive() == 7 && o.m_forward && o.m_unknown34 == 2.0f);
    CHECK(o.m_writtenTo == 0 && o.m_second == 0.0f && o.m_factory == nullptr && o.m_readState == nullptr &&
          o.m_writeState == nullptr && o.m_stateContext == nullptr && o.m_factoryContext == nullptr);
    CHECK(g_netPerfFrequency != 0 && o.SecondsSinceRead() < 0.5f);
    // Property 1 made with it.
    CHECK(o.m_properties.size() == 1 && o.m_propertyCount == 1 && o.GetProperty(1) != nullptr && o.GetProperty(0) == nullptr);
    CHECK(o.GetProperty(1)->GetIndex() == 1);
    // The quantisations: stored (null too), every property's.
    o.SetReadQuantizations(&g_rate, &g_rate, &g_rate);
    CHECK(o.m_read[0] == &g_rate && o.m_read[1] == &g_rate && o.m_read[2] == &g_rate);
    o.SetReadQuantizations(&g_value, &g_delta, nullptr);
    o.SetWriteQuantizations(nullptr, &g_rate, &g_value);
    CHECK(o.m_read[0] == &g_value && o.m_read[1] == &g_delta && o.m_read[2] == nullptr);
    CHECK(o.m_write[0] == nullptr && o.m_write[1] == &g_rate && o.m_write[2] == &g_value);
    CHECK(o.GetProperty(1)->m_read[1] == &g_delta && o.GetProperty(1)->m_write[2] == &g_value);
    S_NetQuantization *a, *b, *c;
    o.GetReadQuantizations(&a, &b, &c);
    CHECK(a == &g_value && b == &g_delta && c == nullptr);
    o.m_read[2] = &g_rate;
    o.GetReadQuantizations(&a, &b, &c);
    CHECK(c == &g_rate);
    o.m_read[2] = nullptr;
    o.GetWriteQuantizations(&a, &b, &c);
    CHECK(a == nullptr && b == &g_rate && c == &g_value);
    // A property made later gets them (the write set first, then the read).
    C_NetProperty *p0 = o.AddProperty(0);
    CHECK(p0->m_write[0] == nullptr && p0->m_read[2] == nullptr);
    CHECK(o.m_properties.size() == 2 && o.m_propertyCount == 2 && o.GetProperty(0) == p0 && p0->m_read[0] == &g_value &&
          p0->m_write[1] == &g_rate && p0->GetTrack().m_write[2] == &g_value);
    // The first of an index.
    C_NetProperty *p0b = o.AddProperty(0);
    CHECK(o.GetProperty(0) == p0 && p0b != p0 && o.m_propertyCount == 3);
    // Every property's field 2, lag.
    o.SetPropertyField2(0x3B);
    CHECK(o.GetProperty(1)->GetFlags() == 0x3B0C7 && p0->GetFlags() == 0x3B0C7 && p0b->GetFlags() == 0x3B0C7);
    o.SetLag(0.5f);
    CHECK(p0->GetTrack().m_lag == 0.5f && o.GetProperty(1)->GetDirTrack().m_lag == 0.5f && p0b->GetTrack().m_lag == 0.5f);
    // Refresh (nothing to see) and the tick: a second forgets the players written to.
    o.Refresh();
    o.m_writtenTo = 0xFF;
    o.Tick(0.5f);
    CHECK(o.m_second == 0.5f && o.m_writtenTo == 0xFF);
    o.Tick(0.49f);
    CHECK(o.m_writtenTo == 0xFF);
    o.Tick(0.01f);
    CHECK(o.m_second == 0.0f && o.m_writtenTo == 0);
    // What there is to send.
    CHECK(!o.HasDataToSend());
    o.SetSendMask(NET_SEND_PROPERTIES);
    CHECK(o.HasDataToSend());
    o.SetSendMask(NET_SEND_MESSAGES);
    CHECK(!o.HasDataToSend());
    o.QueueMessage(new T_Message(1), 0, true);
    CHECK(o.HasDataToSend());
    o.SetSendMask(NET_SEND_STATE);
    o.ClearMessages(true, true, false);
    CHECK(o.HasDataToSend() && o.m_outgoing.empty());
    o.SetLive(0);
    o.SetForwarding(false);
    CHECK(o.GetLive() == 0 && !o.m_forward);
}

void TestObjectQueues()
{
    const int alive0 = T_Message::alive;
    {
        C_NetObject o;
        auto *m1 = new T_Message(1), *m2 = new T_Message(2), *m3 = new T_Message(3);
        o.QueueMessage(m1, 6, false);
        o.QueueMessage(m2, 0, false);
        o.QueueMessage(m3, -1, true);
        CHECK(m1->sentTo == 6 && m1->refCount == 1 && m3->sentTo == -1 && o.m_received.size() == 2 && o.m_outgoing.size() == 1);
        // In order, the reference dropped.
        CHECK(o.PopMessage(false) == m1 && m1->refCount == 0 && o.PopMessage(true) == m3 && o.PopMessage(true) == nullptr);
        CHECK(o.PopMessage(false) == m2 && o.PopMessage(false) == nullptr);
        delete m1;
        delete m2;
        delete m3;
        // Clearing: deleted when no queue holds them (or forced); released first when asked.
        auto *a = new T_Message(1), *b = new T_Message(2), *c = new T_Message(3);
        o.QueueMessage(a, 0, false);
        o.QueueMessage(b, 0, false);
        o.QueueMessage(c, 0, true);
        o.QueueMessage(b, 0, true);      // b held twice
        o.ClearMessages(false, false, false);
        CHECK(o.m_received.size() == 2 && T_Message::alive == alive0 + 3);
        o.ClearMessages(true, false, false);
        // a: 0 deleted; b: 1 kept.
        CHECK(o.m_received.size() == 1 && o.m_received.front() == b && b->refCount == 1 && T_Message::alive == alive0 + 2);
        o.ClearMessages(false, true, true);
        CHECK(o.m_outgoing.empty() && T_Message::alive == alive0);
        o.m_received.clear();
        // The received queue forced.
        o.QueueMessage(new T_Message(4), 0, false);
        o.QueueMessage(new T_Message(5), 0, false);
        o.ClearMessages(false, false, true);
        CHECK(o.m_received.empty() && T_Message::alive == alive0);
        // Messages left at the end: not deleted by the object.
        o.QueueMessage(new T_Message(6), 0, true);
    }
    CHECK(T_Message::alive == alive0 + 1);
    T_Message::alive = alive0;
}

void SetupObject(C_NetObject &o)
{
    o.SetReadQuantizations(&g_value, &g_delta, &g_rate);
    o.SetWriteQuantizations(&g_value, &g_delta, &g_rate);
}

void TestObjectWrite()
{
    MakeQuantizations();
    g_netServerSide = true;
    const int alive0 = T_Message::alive;
    C_NetObject o;
    SetupObject(o);
    o.GetProperty(1)->GetTrack().SetValue({5, 6, 7, 0});
    int ctx = 0, sctx = 0;
    // Nothing to send: one 0 bit (no factory, no state handler); a client's write is false.
    {
        C_NetMessage msg;
        CHECK(o.Write(&msg, 3, false));
        CHECK(msg.BitPosition() == 1u && o.m_writtenTo == 8u);
        C_NetMessage msg2;
        CHECK(!o.Write(&msg2, 3, true));
    }
    // The properties: absolute the first time this second, then deltas.
    o.SetSendMask(NET_SEND_PROPERTIES);
    o.m_writtenTo = 0;
    {
        C_NetMessage msg;
        o.Write(&msg, 2, false);
        const uint32_t first = msg.BitPosition();
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && first > 20u);
        C_NetMessage msg2;
        o.Write(&msg2, 2, false);
        // Unchanged deltas: the flag, the position's 0, the direction's 0 and turn sign.
        CHECK(msg2.BitPosition() == 1u + 1u + 1u + 2u && o.m_writtenTo == 4u);
        // A client does not send its properties.
        C_NetMessage msg3;
        o.Write(&msg3, 2, true);
        CHECK(msg3.BitPosition() == 1u);
        // A new second: absolute again; another player too.
        o.Tick(1.0f);
        C_NetMessage msg4;
        o.Write(&msg4, 2, false);
        CHECK(msg4.BitPosition() == first);
        C_NetMessage msg5;
        o.Write(&msg5, 5, false);
        CHECK(msg5.BitPosition() == first && o.m_writtenTo == 0x24u);
    }
    // The messages: not sent unless asked for and queued (a 0 bit when there is a factory).
    o.SetMessageFactory(TestFactory, &ctx);
    o.SetSendMask(0);
    g_factoryCalls = g_factoryWrites = 0;
    {
        C_NetMessage msg;
        o.Write(&msg, 1, false);
        CHECK(msg.BitPosition() == 2u);
        o.SetSendMask(NET_SEND_MESSAGES);
        C_NetMessage msg2;
        o.Write(&msg2, 1, false);
        CHECK(msg2.BitPosition() == 2u && g_factoryWrites == 0);
    }
    // Queued: each written once to each player, of its side; off the queue when sent to all (deleted when no queue
    // holds it).
    auto *a = new T_Message(1, 0x11), *b = new T_Message(2, 0x22), *c = new T_Message(3, 0x33);
    c->fromClient = true;
    o.QueueMessage(a, ~0x6, true);      // to players 1 and 2
    o.QueueMessage(b, ~0x2, true);      // to player 1
    o.QueueMessage(c, 0, true);
    {
        C_NetMessage msg;
        CHECK(o.Write(&msg, 1, false));
        Rewind(msg);
        CHECK(Bits(msg, 1) == 0 && Bits(msg, 1) == 1);
        CHECK(Bits(msg, 4) == 1 && Bits(msg, 8) == 0x11 && Bits(msg, 4) == 2 && Bits(msg, 8) == 0x22 && Bits(msg, 4) == 0xF);
        CHECK(g_factoryWrites == 1 && g_factoryContext == &ctx);
        // b went to everyone: gone (deleted); a waits for player 2; c (a client's) stays.
        CHECK(o.m_outgoing.size() == 2 && o.m_outgoing.front() == a && o.m_outgoing.back() == c &&
              T_Message::alive == alive0 + 2);
        // Again to player 1: nothing new (the end mark only).
        C_NetMessage msg2;
        CHECK(o.Write(&msg2, 1, false));
        Rewind(msg2);
        CHECK(Bits(msg2, 1) == 0 && Bits(msg2, 1) == 1 && Bits(msg2, 4) == 0xF);
        // Player 2: a, then gone.
        C_NetMessage msg3;
        o.Write(&msg3, 2, false);
        Rewind(msg3);
        CHECK(Bits(msg3, 2) == 2 && Bits(msg3, 4) == 1 && Bits(msg3, 8) == 0x11 && Bits(msg3, 4) == 0xF);
        CHECK(o.m_outgoing.size() == 1 && T_Message::alive == alive0 + 1);
        // The client's message by a client's write: true (a message went); then false.
        C_NetMessage msg4;
        CHECK(o.Write(&msg4, 0, true));
        Rewind(msg4);
        CHECK(Bits(msg4, 2) == 2 && Bits(msg4, 4) == 3 && Bits(msg4, 8) == 0x33);
        C_NetMessage msg5;
        CHECK(!o.Write(&msg5, 0, true));
    }
    // Sent to all but held by another queue too: off this one, not deleted.
    {
        auto *d = new T_Message(4, 0x44);
        // (QueueMessage sets the sent mask: the received queue first.)
        o.QueueMessage(d, 0, false);
        o.QueueMessage(d, ~1, true);
        C_NetMessage msg;
        o.Write(&msg, 0, false);
        CHECK(o.m_outgoing.size() == 1 && d->refCount == 1 && T_Message::alive == alive0 + 2);
        CHECK(o.PopMessage(false) == d);
        delete d;
    }
    // The owner's state.
    o.SetStateHandlers(TestReadState, TestWriteState, &sctx);
    o.SetSendMask(0);
    g_stateWrites = 0;
    {
        C_NetMessage msg;
        o.Write(&msg, 0, false);
        CHECK(msg.BitPosition() == 3u && g_stateWrites == 0);
        o.SetSendMask(NET_SEND_STATE);
        C_NetMessage msg2;
        o.Write(&msg2, 0, false);
        Rewind(msg2);
        CHECK(Bits(msg2, 3) == 4 && Bits(msg2, 8) == 0x5A && g_stateWrites == 1 && g_stateContext == &sctx);
        CHECK(msg2.BitPosition() == 11u);
    }
    o.ClearMessages(true, true, true);
    g_netServerSide = false;
    T_Message::alive = alive0;
}

void TestObjectRead()
{
    MakeQuantizations();
    const int alive0 = T_Message::alive;
    C_NetObject src, dst;
    SetupObject(src);
    SetupObject(dst);
    int ctx = 0, sctx = 0;
    src.SetMessageFactory(TestFactory, &ctx);
    dst.SetMessageFactory(TestFactory, &ctx);
    src.SetStateHandlers(TestReadState, TestWriteState, &sctx);
    dst.SetStateHandlers(TestReadState, TestWriteState, &sctx);
    src.SetSendMask(NET_SEND_PROPERTIES | NET_SEND_MESSAGES | NET_SEND_STATE);
    g_netServerSide = false;
    src.GetProperty(1)->GetTrack().SetValue({5, 6, 7, 0});
    src.QueueMessage(new T_Message(7, 0x77), 0, true);
    src.QueueMessage(new T_Message(8, 0x88), 0, true);
    g_netServerSide = true;
    C_NetMessage msg;
    src.Write(&msg, 0, false);
    const uint32_t written = msg.BitPosition();
    g_netServerSide = false;
    Rewind(msg);
    dst.m_lastRead -= g_netPerfFrequency * 5;
    g_stateReads = 0;
    // A client: the properties, the messages received (as from the side given), the state; not passed on.
    dst.Read(&msg, 0x10, true);
    CHECK(msg.BitPosition() == written);
    CHECK(dst.SecondsSinceRead() < 0.5f && Near(dst.GetProperty(1)->GetTrack().m_net.y, 6, 0.01f));
    CHECK(dst.m_received.size() == 2 && dst.m_outgoing.empty() && g_stateReads == 1);
    auto *r1 = static_cast<T_Message *>(dst.PopMessage(false));
    auto *r2 = static_cast<T_Message *>(dst.PopMessage(false));
    CHECK(r1->type == 7 && r1->payload == 0x77 && r1->fromClient && r1->sentTo == 0 && r2->type == 8 && r2->payload == 0x88);
    delete r1;
    delete r2;
    // The server passes them on (marked as sent to the given players): only when it sends something and forwards.
    g_netServerSide = true;
    Rewind(msg);
    dst.Read(&msg, 0x10, false);
    CHECK(dst.m_outgoing.empty() && dst.m_received.size() == 2 && !dst.m_received.front()->fromClient);
    dst.ClearMessages(true, false, true);
    dst.SetSendMask(NET_SEND_STATE);
    Rewind(msg);
    dst.Read(&msg, 0x10, false);
    CHECK(dst.m_outgoing.size() == 2 && dst.m_received.size() == 2 && dst.m_outgoing.front() == dst.m_received.front());
    CHECK(dst.m_outgoing.front()->sentTo == 0x10 && dst.m_received.front()->sentTo == 0x10 &&
          dst.m_outgoing.front()->refCount == 2);
    // (Held by both queues: off the outgoing one, then released from the received one: deleted.)
    CHECK(dst.PopMessage(true) != nullptr && dst.PopMessage(true) != nullptr && dst.m_outgoing.empty());
    dst.ClearMessages(true, false, false);
    CHECK(T_Message::alive == alive0 + 2 && dst.m_received.empty());
    dst.SetForwarding(false);
    Rewind(msg);
    dst.Read(&msg, 0x10, false);
    CHECK(dst.m_outgoing.empty() && dst.m_received.size() == 2);
    dst.ClearMessages(true, false, true);
    dst.SetForwarding(true);
    g_netServerSide = false;
    Rewind(msg);
    dst.Read(&msg, 0x10, false);
    CHECK(dst.m_outgoing.empty());
    dst.ClearMessages(true, false, true);
    // Without a factory / state reader their bits are not read; no properties flagged: none read.
    C_NetObject bare;
    SetupObject(bare);
    C_NetMessage m2;
    m2.WriteBits(0, 1);
    m2.WriteBits(1, 1);
    Rewind(m2);
    bare.Read(&m2, 0, false);
    CHECK(m2.BitPosition() == 1u && Same3(bare.GetProperty(1)->GetTrack().m_net, 0, 0, 0));
    src.ClearMessages(true, true, true);
    T_Message::alive = alive0;
}

int64_t PerfNow()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

void TestObjectUpdate()
{
    C_NetObject o;
    C_NetProperty *p = o.GetProperty(1);
    C_NetPositionTrack &t = p->GetTrack();
    p->SetFlags(7 | NET_TRACK_MOVE_TARGET);
    t.m_rate = {1, 0, 0, 0};
    t.m_speed = 100.0f;
    // Data lately: the target moves.
    o.Update(0.1f);
    CHECK(Near(t.m_target.x, 0.1f, 1e-6f) && Near(t.m_current.x, 0.1f, 1e-6f) && p->GetFlags() == (7 | NET_TRACK_MOVE_TARGET));
    // Silent for over 0.4 s: it stays (the flag back after).
    o.m_lastRead -= g_netPerfFrequency;
    o.Update(0.1f);
    CHECK(Near(t.m_target.x, 0.1f, 1e-6f) && p->GetFlags() == (7 | NET_TRACK_MOVE_TARGET) && t.m_flags == p->GetFlags());
    // Half a second is silent too.
    o.m_lastRead = 0;
    o.Update(0.0f);
    t.m_target.x = 0.0f;
    o.m_lastRead = PerfNow() - g_netPerfFrequency / 2;
    o.Update(0.1f);
    CHECK(t.m_target.x == 0.0f);
    // Without the flag nothing is changed on it.
    p->SetFlags(7);
    o.Update(0.1f);
    CHECK(p->GetFlags() == 7 && t.m_flags == 7);
    // Other flags kept through the silence.
    p->SetFlags(7 | NET_TRACK_MOVE_TARGET | NET_TRACK_KEEP_CURRENT);
    o.Update(0.1f);
    CHECK(p->GetFlags() == (7 | NET_TRACK_MOVE_TARGET | NET_TRACK_KEEP_CURRENT));
    // Every property updated.
    C_NetProperty *p0 = o.AddProperty(0);
    p0->GetTrack().m_current = {0, 0, 0, 0};
    p0->GetTrack().m_target = {0, 0.5f, 0, 0};
    p0->GetTrack().m_speed = 1.0f;
    o.Update(0.1f);
    CHECK(Near(p0->GetTrack().m_current.y, 0.1f, 1e-6f));
    // The tick and the messages' ages.
    auto *a = new T_Message(1), *b = new T_Message(2);
    o.QueueMessage(a, 0, false);
    o.QueueMessage(b, 0, true);
    o.m_writtenTo = 3;
    o.m_second = 0.0f;
    o.Update(0.25f);
    CHECK(a->age == 0.25f && b->age == 0.25f && o.m_second == 0.25f && o.m_writtenTo == 3);
    o.Update(0.75f);
    CHECK(o.m_writtenTo == 0 && o.m_second == 0.0f);
    o.ClearMessages(true, false, true);
    o.ClearMessages(true, true, true);
}

void TestViewCone()
{
    C_NetProperty viewer(0);
    C_NetViewCone cone;
    CHECK(cone.m_viewer == nullptr && cone.m_cosine == 0.737f);
    cone.m_viewer = &viewer;
    viewer.GetTrack().m_current = {10, 0, 10, 0};
    viewer.GetDirTrack().m_direction = {1, 0, 0, 0};
    // Ahead, behind, to the side; the edge (cos 0.737 is about 42.5 degrees).
    CHECK(cone.Contains({20, 0, 10, 0}) && !cone.Contains({0, 0, 10, 0}) && !cone.Contains({10, 0, 20, 0}));
    CHECK(cone.Contains({20, 0, 19, 0}) && !cone.Contains({20, 0, 20, 0}) && cone.Contains({20, 5, 10, 0}));
    CHECK(!cone.Contains({20, 0, 0, 0}) && cone.Contains({20, 0, 1.5f, 0}));
    // Exactly a unit away: not normalised (as is).
    CHECK(cone.Contains({11, 0, 10, 0}) && !cone.Contains({10.6f, 0, 10.8f, 0}));
    // The direction's other components.
    viewer.GetDirTrack().m_direction = {0, 0, -1, 0};
    CHECK(cone.Contains({10, 0, 0, 0}) && !cone.Contains({10, 0, 20, 0}));
    viewer.GetDirTrack().m_direction = {0, 1, 0, 0};
    CHECK(cone.Contains({10, 5, 10, 0}) && !cone.Contains({10, -5, 10, 0}));
    // (Normalised: y 0.447 and z 0.447 are outside.)
    CHECK(!cone.Contains({20, 5, 10, 0}) && cone.Contains({10, 11, 10, 0}));
    viewer.GetDirTrack().m_direction = {0, 0, 1, 0};
    CHECK(!cone.Contains({20, 0, 15, 0}) && cone.Contains({10, 0, 21, 0}));
    viewer.GetDirTrack().m_direction = {0, 1, 0, 0};
    CHECK(cone.Contains({10, 0, 10, 0}));
    // Tiny offsets: the engine's fallback (x, else y when z is 0, else z) to a unit axis.
    viewer.GetDirTrack().m_direction = {1, 0, 0, 0};
    CHECK(cone.Contains({10.00001f, 0, 10, 0}) && !cone.Contains({9.99999f, 0, 10, 0}));
    viewer.GetDirTrack().m_direction = {0, 1, 0, 0};
    CHECK(cone.Contains({10, 0.00001f, 10, 0}) && !cone.Contains({10, -0.00001f, 10, 0}));
    CHECK(!cone.Contains({10, 0.00001f, 10.00001f, 0}));
    viewer.GetDirTrack().m_direction = {0, 0, 1, 0};
    CHECK(cone.Contains({10, 0.00001f, 10.00001f, 0}) && !cone.Contains({10, 0, 9.99999f, 0}));
    CHECK(!cone.Contains({10, 0, 10, 0}));
    cone.m_cosine = -2.0f;
    CHECK(cone.Contains({10, 0, 10, 0}));
}

void SetupReplication(C_NetReplication &r)
{
    r.SetReadQuantizations(&g_value, &g_delta, &g_rate);
    r.SetWriteQuantizations(&g_value, &g_delta, &g_rate);
    r.m_makeFactory = MakeTestFactory;
    r.m_makeStateReader = MakeTestReader;
}

void TestReplicationBasics()
{
    MakeQuantizations();
    C_NetReplication r;
    CHECK(!r.m_useViewCone && r.m_objects.empty() && r.m_readEpoch == 0 && r.m_field2 == 0 && r.m_bitsWritten == 0 &&
          r.m_bitsRead == 0 && !r.m_enabled && r.m_makeStateReader == nullptr && r.m_makeFactory == nullptr &&
          r.m_writeEpoch == 1 && r.m_lag == 0.08f && r.GetLocalId() == -2);
    CHECK(r.m_viewCone.m_viewer == nullptr && r.m_viewCone.m_cosine == 0.737f && r.m_local.GetId() == -1);
    CHECK(r.m_read[0] == nullptr && r.m_write[2] == nullptr);
    // The quantisations: the non-null stored; every object's set as given.
    SetupReplication(r);
    C_NetObject *a = r.FindObject(4, true);
    CHECK(a && a->GetId() == 4 && r.m_objects.size() == 1 && r.FindObject(4, false) == a && r.FindObject(5, false) == nullptr);
    CHECK(r.m_objects.size() == 1);
    // Made with the replication's settings: the lag, the quantisations, field 2, the handlers from the makers.
    CHECK(a->m_read[0] == &g_value && a->m_write[1] == &g_delta && a->GetProperty(1)->GetTrack().m_lag == 0.08f &&
          a->m_factory == TestFactory && a->m_readState == TestReadState && a->m_writeState == nullptr &&
          a->m_factoryContext == nullptr && a->m_stateContext == nullptr && a->GetProperty(1)->GetFlags() == 0xC7);
    r.SetReadQuantizations(nullptr, &g_rate, nullptr);
    CHECK(r.m_read[0] == &g_value && r.m_read[1] == &g_rate && r.m_read[2] == &g_rate);
    CHECK(a->m_read[0] == nullptr && a->m_read[1] == &g_rate && a->m_read[2] == nullptr);
    r.SetWriteQuantizations(&g_rate, nullptr, nullptr);
    CHECK(r.m_write[0] == &g_rate && r.m_write[1] == &g_delta && a->m_write[0] == &g_rate && a->m_write[1] == nullptr);
    SetupReplication(r);
    r.m_field2 = 0x3B;
    C_NetObject *b = r.FindObject(9, true);
    CHECK(b->GetProperty(1)->GetFlags() == 0x3B0C7 && a->GetProperty(1)->GetFlags() == 0xC7 && r.m_objects.size() == 2);
    // The lag: every object's.
    r.SetLag(0.035f);
    CHECK(r.m_lag == 0.035f && a->GetProperty(1)->GetDirTrack().m_lag == 0.035f && b->GetProperty(1)->GetTrack().m_lag == 0.035f);
    // Update: the objects and the local one; Refresh.
    a->m_second = b->m_second = r.m_local.m_second = 0.0f;
    r.Update(0.25f);
    CHECK(a->m_second == 0.25f && b->m_second == 0.25f && r.m_local.m_second == 0.25f);
    r.Refresh();
    // Removed: deleted; the epochs; the local id.
    r.RemoveObject(a);
    CHECK(r.m_objects.size() == 1 && r.m_objects[0] == b && r.FindObject(4, false) == nullptr);
    C_NetObject stray;
    r.RemoveObject(&stray);
    CHECK(r.m_objects.size() == 1);
    r.SetWriteEpoch(0);
    r.SetReadEpoch(1);
    CHECK(r.m_writeEpoch == 0 && r.m_readEpoch == 1 && &r.GetLocalObject() == &r.m_local);
    r.FindObject(1, true);
    r.FindObject(2, true);
    r.DeleteObjects();
    CHECK(r.m_objects.empty());
    // Reset: the defaults (the objects forgotten, not deleted).
    C_NetObject *c = r.FindObject(3, true);
    r.m_useViewCone = r.m_enabled = true;
    r.m_bitsRead = r.m_bitsWritten = 5;
    r.Reset();
    CHECK(r.m_objects.empty() && !r.m_useViewCone && !r.m_enabled && r.m_bitsRead == 0 && r.m_bitsWritten == 0 &&
          r.m_writeEpoch == 1 && r.m_readEpoch == 0 && r.m_lag == 0.08f && r.m_localId == -2 && r.m_makeFactory == nullptr);
    delete c;
}

void TestReplicationWrite()
{
    MakeQuantizations();
    g_netServerSide = true;
    C_NetReplication r;
    SetupReplication(r);
    C_NetObject *a = r.FindObject(4, true), *b = r.FindObject(300, true), *c = r.FindObject(7, true);
    a->SetSendMask(NET_SEND_PROPERTIES);
    b->SetSendMask(NET_SEND_MESSAGES);  // nothing queued: skipped
    c->SetSendMask(NET_SEND_STATE);
    int sctx = 0;
    c->SetStateHandlers(TestReadState, TestWriteState, &sctx);
    a->GetProperty(1)->GetTrack().SetValue({1, 2, 3, 0});
    C_NetMessage msg;
    msg.WriteBits(0x3, 2);                 // (counted from where it starts)
    CHECK(r.Write(&msg, 1, false));
    const uint32_t end = msg.BitPosition();
    CHECK(r.m_bitsWritten == static_cast<int>(end) - 2);
    Rewind(msg);
    Bits(msg, 2);
    // The epoch, a (4), c (7), the end.
    CHECK(Bits(msg, 1) == 1 && Bits(msg, 9) == 4);
    // a's data: its properties, no messages (no factory? the replication gave one: a 0 bit), its state (none).
    CHECK(Bits(msg, 1) == 1);
    {
        C_NetObject skip;
        skip.SetReadQuantizations(&g_value, &g_delta, &g_rate);
        // (As the replication's objects: field 2 set, which hands the flags 0xC7 to the tracks: the direction
        // track's three components.)
        skip.SetPropertyField2(0);
        skip.GetProperty(1)->Read(&msg);
        CHECK(Near(skip.GetProperty(1)->GetTrack().m_net.y, 2, 0.01f));
    }
    CHECK(Bits(msg, 1) == 0);                              // a's messages
    CHECK(Bits(msg, 9) == 7 && Bits(msg, 1) == 0 && Bits(msg, 1) == 0 && Bits(msg, 1) == 1 && Bits(msg, 8) == 0x5A);
    CHECK(Bits(msg, 9) == 0x1FF && msg.BitPosition() == end);
    // The epoch written as set; a client's write (objects' Write false): false.
    r.SetWriteEpoch(0);
    C_NetMessage msg2;
    CHECK(!r.Write(&msg2, 1, true));
    const uint32_t end2 = msg2.BitPosition();
    Rewind(msg2);
    CHECK(Bits(msg2, 1) == 0 && Bits(msg2, 9) == 4);
    CHECK(r.m_bitsWritten == static_cast<int>(end) - 2 + static_cast<int>(end2));
    // Nothing to send: the epoch and the end.
    a->SetSendMask(0);
    c->SetSendMask(0);
    C_NetMessage msg3;
    CHECK(!r.Write(&msg3, 1, false));
    CHECK(msg3.BitPosition() == 10u);
    // The view cone: only the objects ahead of the viewer (their property 0's place).
    a->SetSendMask(NET_SEND_PROPERTIES);
    c->SetSendMask(NET_SEND_PROPERTIES);
    b->SetSendMask(NET_SEND_PROPERTIES);
    a->AddProperty(0)->GetTrack().m_current = {10, 0, 0, 0};
    b->AddProperty(0)->GetTrack().m_current = {-10, 0, 0, 0};
    c->AddProperty(0)->GetTrack().m_current = {10, 0, 1, 0};
    C_NetProperty viewer(0);
    viewer.GetDirTrack().m_direction = {1, 0, 0, 0};
    r.m_viewCone.m_viewer = &viewer;
    r.m_useViewCone = true;
    C_NetMessage msg4;
    r.Write(&msg4, 2, false);
    Rewind(msg4);
    Bits(msg4, 1);
    CHECK(Bits(msg4, 9) == 4);
    r.m_useViewCone = false;
    C_NetMessage msg5;
    r.Write(&msg5, 2, false);
    CHECK(msg5.BitPosition() > msg4.BitPosition());
    g_netServerSide = false;
}

void TestReplicationRead()
{
    MakeQuantizations();
    const int alive0 = T_Message::alive;
    C_NetReplication server, client;
    SetupReplication(server);
    SetupReplication(client);
    g_netServerSide = true;
    C_NetObject *a = server.FindObject(4, true), *b = server.FindObject(9, true);
    a->SetSendMask(NET_SEND_PROPERTIES | NET_SEND_MESSAGES | NET_SEND_STATE);
    b->SetSendMask(NET_SEND_PROPERTIES);
    // (The owners set both state handlers: the readers made by the replication expect the state's bit.)
    a->SetStateHandlers(TestReadState, TestWriteState, nullptr);
    b->SetStateHandlers(TestReadState, TestWriteState, nullptr);
    a->GetProperty(1)->GetTrack().SetValue({1, 2, 3, 0});
    b->GetProperty(1)->GetTrack().SetValue({4, 5, 6, 0});
    a->QueueMessage(new T_Message(2, 0x21), 0, true);
    C_NetMessage msg;
    server.Write(&msg, 1, false);
    const uint32_t end = msg.BitPosition();
    g_netServerSide = false;
    // Not enabled: nothing read.
    Rewind(msg);
    client.Read(&msg, 0, false);
    CHECK(msg.BitPosition() == 0u && client.m_objects.empty());
    // Enabled, the epoch expected: the objects made and read.
    client.m_enabled = true;
    client.m_readEpoch = 1;
    Rewind(msg);
    client.Read(&msg, 0x3, false);
    CHECK(msg.BitPosition() == end && client.m_bitsRead == static_cast<int>(end));
    C_NetObject *ca = client.FindObject(4, false), *cb = client.FindObject(9, false);
    CHECK(ca && cb && client.m_objects.size() == 2);
    CHECK(Near(ca->GetProperty(1)->GetTrack().m_net.x, 1, 0.01f) && Near(cb->GetProperty(1)->GetTrack().m_net.z, 6, 0.01f));
    CHECK(ca->m_received.size() == 1 && static_cast<T_Message *>(ca->m_received.front())->payload == 0x21);
    // (A client does not forward; the messages' sent mask is what the queue gave: 0.)
    CHECK(ca->m_outgoing.empty() && ca->m_received.front()->sentTo == 0);
    ca->ClearMessages(true, false, true);
    // The other epoch: read but dropped (a scratch object; the objects' data unchanged), with the object's read
    // quantisations (the replication's differ now) and state context.
    S_NetQuantization other;
    other.Setup({100.0f, 100.0f, 100.0f, 0.0f}, {-50.0f, -50.0f, -50.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, 100);
    client.m_read[0] = &other;
    client.m_read[1] = &other;
    int stateMark = 0;
    ca->m_stateContext = &stateMark;
    g_stateContext = nullptr;
    client.m_readEpoch = 0;
    ca->GetProperty(1)->GetTrack().m_net = {9, 9, 9, 0};
    Rewind(msg);
    client.Read(&msg, 0, false);
    CHECK(msg.BitPosition() == end && ca->GetProperty(1)->GetTrack().m_net.x == 9 && ca->m_received.empty());
    CHECK(g_stateContext == &stateMark);
    client.m_read[0] = &g_value;
    client.m_read[1] = &g_delta;
    ca->m_stateContext = nullptr;
    // (The scratch object's message is not deleted with it: it leaks, as in the original.)
    CHECK(T_Message::alive == alive0 + 2);
    // An object not live: likewise dropped.
    client.m_readEpoch = 1;
    ca->SetLive(0);
    Rewind(msg);
    client.Read(&msg, 0, false);
    CHECK(ca->GetProperty(1)->GetTrack().m_net.x == 9 && Near(cb->GetProperty(1)->GetTrack().m_net.z, 6, 0.01f));
    // The local player's object: read into the local one (made like the others: its handlers' contexts cleared),
    // its messages dropped.
    ca->SetLive(0);
    client.m_local.m_id = 4;
    client.m_local.m_stateContext = &stateMark;
    client.m_local.m_factoryContext = &stateMark;
    client.m_local.m_writeState = TestWriteState;
    Rewind(msg);
    client.Read(&msg, 0, false);
    CHECK(Near(client.m_local.GetProperty(1)->GetTrack().m_net.y, 2, 0.01f) && client.m_local.m_received.empty());
    CHECK(client.m_local.m_factory == TestFactory && ca->GetProperty(1)->GetTrack().m_net.x == 9);
    CHECK(client.m_local.m_stateContext == nullptr && client.m_local.m_factoryContext == nullptr &&
          client.m_local.m_writeState == nullptr && client.m_local.m_readState == TestReadState);
    CHECK(T_Message::alive == alive0 + 3);
    // A live local object of that id: its own read (the live test first).
    ca->SetLive(7);
    client.m_local.GetProperty(1)->GetTrack().m_net = {0, 0, 0, 0};
    Rewind(msg);
    client.Read(&msg, 0, false);
    CHECK(Near(ca->GetProperty(1)->GetTrack().m_net.x, 1, 0.01f) && client.m_local.GetProperty(1)->GetTrack().m_net.y == 0);
    ca->ClearMessages(true, false, true);
    // The server forwards what a client sends, as sent to all but `forwardTo`.
    g_netServerSide = true;
    C_NetReplication relay;
    SetupReplication(relay);
    relay.m_enabled = true;
    relay.m_readEpoch = 1;
    relay.FindObject(4, true)->SetSendMask(NET_SEND_STATE);
    Rewind(msg);
    relay.Read(&msg, 0x6, true);
    C_NetObject *ra = relay.FindObject(4, false);
    CHECK(ra->m_outgoing.size() == 1 && ra->m_outgoing.front()->sentTo == ~0x6 && ra->m_outgoing.front()->fromClient);
    ra->PopMessage(true);
    ra->ClearMessages(true, false, false);
    a->ClearMessages(true, true, true);
    g_netServerSide = false;
    CHECK(T_Message::alive == alive0 + 2);
    T_Message::alive = alive0;
}

// The constructors over dirty memory (every field set); the destructors delete what they own.
void TestOwnership()
{
    alignas(16) static unsigned char buffer[sizeof(C_NetObject)];
    std::memset(buffer, 0xAB, sizeof buffer);
    auto *o = new (buffer) C_NetObject;
    CHECK(o->m_propertyCount == 1 && o->m_second == 0.0f && o->m_stateContext == nullptr && o->m_factoryContext == nullptr &&
          o->m_writeState == nullptr && o->m_readState == nullptr && o->m_factory == nullptr && o->m_writtenTo == 0 &&
          o->m_sendMask == 0 && o->m_live == 7 && o->m_forward && o->m_id == -1 && o->m_unknown34 == 2.0f);
    for (int i = 0; i < 3; ++i)
        CHECK(o->m_read[i] == nullptr && o->m_write[i] == nullptr);
    C_NetProperty *p1 = o->GetProperty(1), *p0 = o->AddProperty(0);
    g_deletedCount = 0;
    g_trackDeletes = true;
    o->~C_NetObject();
    g_trackDeletes = false;
    CHECK(WasDeleted(p1) && WasDeleted(p0));
    alignas(16) static unsigned char rbuffer[sizeof(C_NetReplication)];
    std::memset(rbuffer, 0xAB, sizeof rbuffer);
    auto *r = new (rbuffer) C_NetReplication;
    CHECK(r->m_objects.empty() && !r->m_useViewCone && r->m_field2 == 0 && r->m_writeEpoch == 1 && r->m_readEpoch == 0 &&
          r->m_bitsRead == 0 && r->m_bitsWritten == 0 && !r->m_enabled && r->m_lag == 0.08f && r->m_localId == -2 &&
          r->m_makeFactory == nullptr && r->m_makeStateReader == nullptr);
    // (Objects are made with the factory makers: called unchecked, as in the original.)
    SetupReplication(*r);
    C_NetObject *a = r->FindObject(1, true), *b = r->FindObject(2, true), *c = r->FindObject(3, true);
    g_deletedCount = 0;
    g_trackDeletes = true;
    r->RemoveObject(b);
    g_trackDeletes = false;
    CHECK(WasDeleted(b) && !WasDeleted(a) && r->m_objects.size() == 2);
    g_deletedCount = 0;
    g_trackDeletes = true;
    r->~C_NetReplication();
    g_trackDeletes = false;
    CHECK(WasDeleted(a) && WasDeleted(c));
    C_NetReplication r2;
    SetupReplication(r2);
    C_NetObject *d = r2.FindObject(4, true);
    g_deletedCount = 0;
    g_trackDeletes = true;
    r2.DeleteObjects();
    g_trackDeletes = false;
    CHECK(WasDeleted(d) && r2.m_objects.empty());
}
