// The transport's packets and channels (part 10c-i): the game's own UDP protocol under C_NetGame. A connection
// keeps three channels (plain, reliable, reliable and ordered); each sends packets with 23-bit sequence numbers,
// the reliable ones resend until acknowledged. The packets and their data buffers come from pools. Original files
// 0x00745300..0x00746180, 0x00747400..0x00747910, 0x00749800..0x00749ea0.
#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include <windows.h>

class C_BitStream;

// A reference-counted object (vtable 0x00816678): deleted when released below one.
class C_NetRefCounted {
public:
    virtual ~C_NetRefCounted() = default;                         // 0x00745e90 (DS 0x007a6d60) (scalar deleting 0x00745ea0 (DS 0x007a6d80))
    virtual void AddRef() { ++m_refs; }                           // 0x00745e10 (DS 0x007a6ca0)
    virtual void Release();                                       // 0x00745e20

    int m_refs = 0;                                               // +0x04
};

// The packets' fields: the channel (low 2 bits) and the late mark (set on arriving after newer ones).
enum : uint32_t {
    NET_PACKET_CHANNEL = 3,
    NET_PACKET_LATE = 4,
};

// A packet (vtable 0x0081665c, 0x20 bytes; from the pool): its header (the channel 2 bits, the size 16, the kind 6,
// the sequence number 23, the session bit) and its data (a pool buffer of `size` bytes).
class C_NetPacket : public C_NetRefCounted {
public:
    C_NetPacket() { Clear(); }                                    // 0x00745e40 (__fastcall)
    ~C_NetPacket() override;                                      // 0x00745ee0 (the data back to the pool)
    // Released below one: the data back to the pool, cleared, back to the pool itself.
    void Release() override;                                      // 0x00745f70
    virtual void Clear();                                         // 0x00745f30
    virtual void FreeData();                                      // 0x00745f50
    // The header, then the data copied byte for byte.
    virtual void Write(C_BitStream *stream);                      // 0x00745fa0
    // The header, then the data into a pool buffer.
    virtual void Read(C_BitStream *stream);                       // 0x00746050

    uint32_t m_flags;                                             // +0x08 NET_PACKET_*
    int m_size;                                                   // +0x0C bytes
    uint32_t m_kind;                                              // +0x10
    uint32_t m_sequence;                                          // +0x14
    uint32_t m_session;                                           // +0x18 the connection's session (1 bit)
    void *m_data;                                                 // +0x1C
};

// A free list of the pool (vtable 0x008166dc for the buffers, 0x008166d8 for the packets, 0x18 bytes): the free
// items, last freed first; grown by 16 when empty.
struct S_NetFreeList {
    std::vector<void *> items;                                    // (+0x04 the nodes, +0x08 the spare nodes)
    int growBy = 16;                                              // +0x0C
    int count = 0;                                                // +0x10
};

// The pools (g_netPool, 0x48 bytes): 1000-byte data buffers (bigger ones malloc'ed) and packets; each made 16 at
// first; locked.
class C_NetPool {
public:
    C_NetPool();                                                  // 0x00749830
    ~C_NetPool();                                                 // 0x00749970
    void *AllocBuffer(size_t size);                               // 0x00749a70
    void FreeBuffer(void *buffer, int size);                      // 0x00749b20
    C_NetPacket *AllocPacket();                                   // 0x00749b80 (__fastcall)
    void FreePacket(C_NetPacket *packet);                         // 0x00749bd0

    S_NetFreeList m_buffers;                                      // +0x00
    S_NetFreeList m_packets;                                      // +0x18
    CRITICAL_SECTION m_lock;                                      // +0x30
};
extern C_NetPool g_netPool;                                       // 0x00983150

// A 23-bit sequence number `*seq` widened to 32 bits around `reference` (the nearest wrap of it).
void UnwrapSequence(uint32_t *seq, uint32_t reference);           // 0x00745520

// A channel (vtable 0x008165f0, 0x24 bytes): the packets to send, numbered, and those received (a packet older by
// up to 9 than the newest marked late).
class C_NetChannel {
public:
    C_NetChannel();                                               // 0x00745330
    virtual ~C_NetChannel();                                      // 0x00745420 (the packets to send released)
    // Numbered, held, queued to send.
    virtual void Send(C_NetPacket *packet);                       // 0x00745590 (slot 1)
    virtual C_NetPacket *PopReceived();                           // 0x007455f0
    virtual C_NetPacket *PopOutgoing();                           // 0x00745630
    virtual void Receive(C_NetPacket *packet);                    // 0x00745670
    virtual void WriteAck(C_BitStream *) {}                       // 0x007453c0 (slot 5)
    virtual void ReadAck(C_BitStream *) {}                        // 0x007453d0
    virtual void Resend() {}                                      // 0x007453e0
    virtual void Acknowledge() {}                                 // 0x007453f0 (slot 8)

    uint32_t m_sent = 0x3FFFFF;                                   // +0x04 the last number sent
    uint32_t m_received = 0x3FFFFF;                               // +0x08 the newest number received
    std::list<C_NetPacket *> m_outgoing;                          // +0x0C
    std::list<C_NetPacket *> m_incoming;                          // +0x18
};

// A reliable channel (vtable 0x00816614, 0x48 bytes): each packet sent held and sent again (after 200 ms, then
// doubling to 5 s; 7 at most a call) until the other side acknowledges it; the packets received are acknowledged
// up to the first missing one; repeats dropped.
class C_NetReliableChannel : public C_NetChannel {
public:
    C_NetReliableChannel();                                       // 0x007457b0
    ~C_NetReliableChannel() override;                             // 0x00745870
    void Send(C_NetPacket *packet) override;                      // 0x00745980
    void Receive(C_NetPacket *packet) override;                   // 0x00745a00
    // The last number received in order (23 bits) and a 0 bit.
    void WriteAck(C_BitStream *stream) override;                  // 0x00745ab0
    void ReadAck(C_BitStream *stream) override;                   // 0x00745ae0
    // The acknowledged packets dropped; those whose time came queued again.
    void Resend() override;                                       // 0x00745b30
    // The in-order run of the packets received counted as delivered.
    void Acknowledge() override;                                  // 0x00745c20

    struct S_Pending {
        int time;                                                 // ms (GetPerfTimeMs) when it goes again
        int interval;
        C_NetPacket *packet;
    };
    uint32_t m_delivered = 0x3FFFFF;                              // +0x24 the last received in order
    uint32_t m_acked = 0x3FFFFF;                                  // +0x28 the last the other side acknowledged
    int m_resendTime = 200;                                       // +0x2C ms (C_NetConnection::SetResendTime)
    std::list<S_Pending> m_pending;                               // +0x30
    std::list<C_NetPacket *> m_unordered;                         // +0x3C received, not yet in order
};

// A reliable channel that hands the packets on in order (vtable 0x00816638, 0x4C bytes).
class C_NetOrderedChannel : public C_NetReliableChannel {
public:
    C_NetOrderedChannel() = default;                              // 0x00745d30 (__fastcall)
    ~C_NetOrderedChannel() override = default;                    // 0x00745d70
    // The next packet in order (null until it came), its late mark cleared.
    C_NetPacket *PopReceived() override;                          // 0x00745d80

    uint32_t m_next = 0x3FFFFF;                                   // +0x48 the last handed on
};

// The transport's messages: a type and a connection id (8 bits each), then their body (vtable 0x00816688).
class C_NetHeader {
public:
    C_NetHeader() = default;                                      // 0x00747430
    virtual ~C_NetHeader() = default;                             // 0x00747490
    virtual void Read(C_BitStream *stream);                       // 0x007474a0
    virtual void Write(C_BitStream *stream);                      // 0x007474d0
    virtual void WriteBody(C_BitStream *) {}                      // 0x00747450
    virtual void ReadBody(C_BitStream *) {}                       // 0x00747460

    uint32_t m_type = 0;                                          // +0x04 (1 packets, 2 ping)
    uint32_t m_connection = 0;                                    // +0x08
};

// Type 1 (vtable 0x0081669c): packets (an 8-bit count; each packet's Write).
class C_NetPacketList : public C_NetHeader {
public:
    C_NetPacketList() { m_type = 1; }                             // 0x00747500
    ~C_NetPacketList() override;                                  // 0x00747590 (the packets released)
    void WriteBody(C_BitStream *stream) override;                 // 0x00747660
    // Each packet from the pool.
    void ReadBody(C_BitStream *stream) override;                  // 0x007476a0
    void Add(C_NetPacket *packet) { m_packets.push_back(packet); }   // 0x00747720
    C_NetPacket *Pop();                                           // 0x00747770 (__fastcall; null: none)

    std::list<C_NetPacket *> m_packets;                           // +0x0C
};

// Type 2 (vtable 0x008166b0): a ping (0) and its echo (1): the reply bit, an 8-bit id, the sender's time (ms); the
// echo read gives the round trip (at least 1).
class C_NetPing : public C_NetHeader {
public:
    C_NetPing() { m_type = 2; }                                   // 0x007477b0 (__fastcall)
    ~C_NetPing() override = default;                              // 0x007477f0
    void WriteBody(C_BitStream *stream) override;                 // 0x00747800
    void ReadBody(C_BitStream *stream) override;                  // 0x00747850

    uint32_t m_reply = 0;                                         // +0x0C
    uint32_t m_time = 0;                                          // +0x10 the time sent; the echo: the round trip
    uint32_t m_id = 0;                                            // +0x14
};

// A message of no body (vtable 0x008166c4).
class C_NetEmptyMessage : public C_NetHeader {
public:
    ~C_NetEmptyMessage() override = default;                      // 0x007478e0
    void WriteBody(C_BitStream *) override {}                     // 0x007478f0
    void ReadBody(C_BitStream *) override {}                      // 0x00747900
};
