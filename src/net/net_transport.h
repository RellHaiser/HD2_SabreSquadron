// The transport's connections and their manager (part 10c-iii): each connection to a machine has the three
// channels, pings it every 2 s, sends its packets with the acknowledgements every 500 ms; a receive thread hands
// the datagrams to the connection of their id and the packets to the game's callback. Original file
// 0x00746180..0x00747400.
#pragma once

#include <cstdint>
#include <vector>

#include <windows.h>

#include "net/net_socket.h"

class C_NetChannel;
class C_NetPacket;

// The game's handler of a packet received: the connection's id, the data (the pool's; freed after), its size,
// kind and flags (the channel, the late mark).
using NetPacketCallback = void(__fastcall *)(int connection, void *data, int size, int kind, uint32_t flags);

// A connection (vtable 0x00816684, 0x50 bytes).
class C_NetConnection {
public:
    // `id`, to `address` (the port unknown until a datagram comes: -1), over `socket`.
    C_NetConnection(int id, uint32_t address, C_NetSocket *socket);   // 0x00746180
    virtual ~C_NetConnection();                                   // 0x007462a0

    // A packet of `kind` with a copy of the data, sent on `channel` (0 plain, 1 reliable, 2 ordered).
    C_NetPacket *Send(const void *data, int size, int kind, int channel);   // 0x00746300
    // The first packet received of the channels (in their order); its data handed over (the caller frees it to
    // the pool), the packet released. False: none.
    bool Receive(void **data, int *size, int *kind, uint32_t *flags);   // 0x00746380
    void Resend(C_NetPacket *packet);                             // 0x007463e0 (on its channel again)
    C_NetPacket *PopReceived();                                   // 0x00746410 (__fastcall)
    // A ping of the next id (8 bits), sent now; every 2 s.
    void SendPing();                                              // 0x00746460 (__fastcall)
    void PingIfDue();                                             // 0x00746550 (__fastcall)
    // The due packets queued again, the outgoing ones and the acknowledgements sent (`force`: the
    // acknowledgements even with no packets).
    void Flush(bool force);                                       // 0x00746580
    // A datagram of `type` for it: packets (handed to `callback`; an unreliable one marked late dropped), a ping
    // (echoed) or an echo (the round trip of the last ping).
    void HandleDatagram(uint32_t type, void *data, int size, NetPacketCallback callback);   // 0x007465d0
    // The acknowledgements and the packets of a type 1 datagram; packets of another session dropped.
    void ReadPackets(void *data, int size);                       // 0x007467f0
    // The packets to send (stamped with the session) and the acknowledgements in g_netSendStream (null: nothing
    // to send, unless `force`; nothing before the port is known).
    void Gather(void **data, int *size, bool force);              // 0x007468f0
    void ResendAll();                                             // 0x00746a50 (__fastcall)
    void AcknowledgeAll();                                        // 0x00746a90 (__fastcall)
    // The reliable channels' first resend time (10..700 ms; else 700).
    void SetResendTime(int ms);                                   // 0x00746ac0

    int m_id;                                                     // +0x04
    uint32_t m_session = 0;                                       // +0x08 (the transport's when added)
    uint32_t m_address;                                           // +0x0C
    int m_port = -1;                                              // +0x10
    C_NetChannel *m_channels[3];                                  // +0x14 plain, reliable, ordered
    CRITICAL_SECTION m_lock;                                      // +0x20
    C_NetSocket *m_socket;                                        // +0x38
    int m_lastSend;                                               // +0x3C ms
    int m_lastReceive;                                            // +0x40
    int m_lastPing;                                               // +0x44
    uint32_t m_pingId = 0;                                        // +0x48
    uint32_t m_roundTrip = 0;                                     // +0x4C ms
};

// The transport (0xDC bytes; C_NetGame's): the connections, the socket, the receive thread.
class C_NetTransport {
public:
    C_NetTransport();                                             // 0x00746b40 (__fastcall)
    ~C_NetTransport();                                            // 0x00746bb0 (__fastcall)

    // The socket opened on `port`, the connections forgotten (not deleted), the thread started.
    void Start(int port);                                         // 0x00746c10
    // The thread stopped (a second at most), the socket closed.
    void Stop();                                                  // 0x00746ca0 (__fastcall)
    void DeleteConnections();                                     // 0x00746cf0 (__fastcall)
    C_NetConnection *FindById(int id);                            // 0x00746d60
    C_NetConnection *FindByAddress(uint32_t address, int port);   // 0x00746dc0
    C_NetTransport *Lock();                                       // 0x00746e40 (__fastcall)
    void Unlock();                                                // 0x00746e60 (__fastcall)
    // The datagrams waiting, each to the connection of its id (its sender's address taken).
    void ReceiveAll();                                            // 0x00746e70 (__fastcall)
    // Added with the transport's session; removed (not deleted; the last moved into its place).
    void Add(C_NetConnection *connection);                        // 0x00746fb0
    void Remove(C_NetConnection *connection);                     // 0x00747180
    // The pings due; every 500 ms the connections flushed with their acknowledgements.
    void Tick();                                                  // 0x00747200 (__fastcall)
    void FlushAll(bool force);                                    // 0x00747260
    void ReceiveThread();                                         // 0x00747310 (__fastcall)

    std::vector<C_NetConnection *> m_connections;                 // +0x04
    C_NetSocket m_socket;                                         // +0x10
    bool m_running = false;                                       // +0xA4
    DWORD m_threadId = 0;                                         // +0xA8
    bool m_stop = false;                                          // +0xAC
    HANDLE m_stopped = nullptr;                                   // +0xB0
    CRITICAL_SECTION m_lock;                                      // +0xB4
    int m_lastFlush = 0;                                          // +0xCC ms
    int m_lastReceive = 0;                                        // +0xD0
    uint32_t m_session = 0;                                       // +0xD4
    NetPacketCallback m_callback = nullptr;                       // +0xD8
};

// The stream the transport's datagrams are written in (one at a time, under a connection's lock).
class C_GrowingBitStream;
extern C_GrowingBitStream g_netSendStream;                        // 0x009830e8
