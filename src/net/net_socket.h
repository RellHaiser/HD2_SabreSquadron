// The transport's UDP socket (part 10c-ii): datagrams sealed (a 24-bit checksum and an XOR key), messages of 500
// bytes or more sent as fragments (one a millisecond), reassembled on arrival; a thread does the sending. Original
// file 0x00747910..0x00748e80.
#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include <windows.h>

// A fragment received (0x20 bytes): the message number, its index, its data (a pool buffer), the last mark, the
// time it came.
struct S_NetFragment {
    uint32_t message;                                             // +0x00
    uint32_t index;                                               // +0x04
    void *data;                                                   // +0x08
    int size;                                                     // +0x0C
    bool last;                                                    // +0x10
    int time;                                                     // +0x14 ms
    int unused[2];
};

// A datagram to send (the lists' 0x20-byte records): a pool buffer and where it goes.
struct S_NetDatagram {
    int unused[2];
    void *data;                                                   // +0x08
    int size;                                                     // +0x0C
    int unused2[2];
    uint32_t address;                                             // +0x18 (network order)
    int port;                                                     // +0x1C
};

// The socket (0x94 bytes; C_NetTransport +0x10).
class C_NetSocket {
public:
    C_NetSocket();                                                // 0x00747940 (__fastcall)
    ~C_NetSocket();                                               // 0x00747a10 (__fastcall)

    // Bound to `port` (-1: any) on every address, the send thread started.
    void Open(int port);                                          // 0x00747af0
    // The thread stopped (a second at most), the socket closed.
    void Close();                                                 // 0x00747bb0 (__fastcall)
    // `size` bytes to `address`:`port`: under 500 at once, else as fragments of 500 queued for FlushDelayed.
    void Send(const void *data, int size, uint32_t address, int port);   // 0x00747cc0
    // A whole message received (null: none, or a fragment that did not complete one): a sealed datagram's data
    // (the socket's buffer), or a reassembled message; its sender.
    void Receive(uint8_t **data, int *size, uint32_t *address, uint32_t *port);   // 0x00747ea0
    // One delayed fragment queued, if a millisecond has passed since the last.
    void FlushDelayed();                                          // 0x007484d0 (__fastcall)
    // The bytes received and sent since asked last (both reset).
    void TakeCounters(int *received, int *sent);                  // 0x007486e0
    // This machine's address (looked up the first time; host order).
    uint32_t GetLocalAddress();                                   // 0x00748790 (__fastcall)

    // The 24-bit checksum of the dwords after the first three bytes (at those bytes), the dwords then XORed with
    // the key and the size; Unseal the reverse, true when the checksum matches. `size`: the bytes after the three.
    void Seal(uint8_t *buffer, int size);                         // 0x00747c00
    bool Unseal(uint8_t *buffer, int size);                       // 0x00747c60
    // The fragments sorted (message, index; MSVC6 std::sort); the first complete message's run (expired ones,
    // over 20 s old, dropped).
    bool FindComplete(int *first, int *count);                    // 0x00748110
    // The complete message's fragments joined into the socket's buffer (their buffers back to the pool).
    bool Reassemble(uint8_t **data, int *size);                   // 0x00748230
    // A datagram (20 ms at most waited for one) into the socket's buffer; its sender.
    void ReceiveRaw(uint8_t **data, int *size, uint32_t *address, uint32_t *port);   // 0x007483b0
    void SendRaw(const void *data, int size, uint32_t address, int port);   // 0x00747e60
    // To the send thread.
    void QueueSend(void *data, int size, uint32_t address, int port);       // 0x00748550
    void SendQueued();                                            // 0x007485e0 (__fastcall)
    void SendThread();                                            // 0x00748680 (__fastcall)
    void LookupLocalAddress();                                    // 0x00748700 (__fastcall)

    uintptr_t m_socket = static_cast<uintptr_t>(-1);              // +0x00
    uint8_t m_address[16] = {};                                   // +0x04 sockaddr_in
    uint8_t *m_buffer;                                            // +0x14 0x8000 bytes
    uint8_t *m_fragmentBuffer;                                    // +0x18 800 bytes
    int m_received = 0;                                           // +0x1C bytes
    int m_sent = 0;                                               // +0x20 bytes
    uint32_t m_message = 0;                                       // +0x24 the last fragmented message's number
    uint32_t m_key = 0x1742BDCD;                                  // +0x28
    std::vector<S_NetFragment> m_fragments;                       // +0x30
    std::list<S_NetDatagram> m_delayed;                           // +0x3C fragments to send
    std::list<S_NetDatagram> m_queue;                             // +0x48 for the send thread
    CRITICAL_SECTION m_lock;                                      // +0x54
    DWORD m_threadId = 0;                                         // +0x6C
    bool m_stop = false;                                          // +0x70
    HANDLE m_stopped = nullptr;                                   // +0x74
    HANDLE m_wake = nullptr;                                      // +0x78
    uint32_t m_localAddress = 0;                                  // +0x7C
    char m_localName[20] = {};                                    // +0x80 its dotted form
};

// When FlushDelayed last queued a fragment (ms).
extern int g_netLastFragmentTime;                                 // 0x00983100
