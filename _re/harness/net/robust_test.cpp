// Branch net-robust-datagrams: the socket and the transport given the datagrams that crash the original (and the port
// on main): sealed datagrams of 1 to 7 bytes that pass as fragments (any old buffer byte after a short one), a message
// of fragments past the socket's 32 KB buffer, packet lists claiming packets of 32 KB or more (read sign-extended) or
// past the datagram's end. Each must be dropped without a fault; good ones after them still arrive. MinGW;
// build_robust.bat (every file with -include sock_fakes.h: part 4a's fakes); runs robust_test.exe
// ("robust: N checks, 0 failures").
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "sock_fakes.h"

#define protected public
#define private public
#include "net/bitstream.h"
#include "net/net_channel.h"
#include "net/net_socket.h"
#include "net/net_transport.h"
#undef private
#undef protected

S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

int64_t g_now = 1000000;
int64_t GetPerfTimeMs() { return g_now; }

namespace {

int checks, failures;
void Check(bool ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%x", v);
    return b;
}
std::string N(long long v) { return std::to_string(v); }

}   // namespace

#include "sock_fakes.inc"   // (it leaves an anonymous namespace open)

}   // namespace

int g_callbacks;
std::vector<uint8_t> g_lastPacket;
void __fastcall Callback(int, void *data, int size, int, uint32_t)
{
    ++g_callbacks;
    g_lastPacket.assign(static_cast<uint8_t *>(data), static_cast<uint8_t *>(data) + size);
}

// recvfrom gives `datagrams` (select readable before each, then nothing).
void Feed(const std::vector<std::vector<uint8_t>> &datagrams)
{
    S_Script s;
    for (const std::vector<uint8_t> &d : datagrams) {
        s.selects.push_back(1);
        S_Wire w;
        w.bytes = d;
        w.address = 0x0100007F;
        w.port = 7777;
        w.result = 1;
        s.wire.push_back(w);
    }
    s.selects.push_back(0);
    g_script[0] = s;
}

// A sealed datagram of `body` (the 3 checksum bytes before it).
std::vector<uint8_t> Sealed(C_NetSocket &socket, const std::vector<uint8_t> &body)
{
    std::vector<uint8_t> d(3 + body.size() + 4, 0);
    std::memcpy(d.data() + 3, body.data(), body.size());
    socket.Seal(d.data(), static_cast<int>(body.size()));
    d.resize(3 + body.size());
    return d;
}

// A fragment: its index (the high bit set), the message number (bit 31: the last), its data.
std::vector<uint8_t> Fragment(C_NetSocket &socket, int index, uint32_t message, bool last, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> body(5 + data.size());
    body[0] = static_cast<uint8_t>(index | 0x80);
    const uint32_t header = message | (last ? 0x80000000u : 0u);
    std::memcpy(&body[1], &header, 4);
    std::memcpy(&body[5], data.data(), data.size());
    return Sealed(socket, body);
}

// A packet list for connection 0 (type 1): the two acknowledgements, then packets given by their header fields and
// the bytes actually there.
struct S_FakePacket {
    uint32_t size;   // as written (16 bits)
    std::vector<uint8_t> data;
};
uint32_t g_sequence;   // (each packet its own number: the plain channel drops a repeat as late)
std::vector<uint8_t> PacketList(C_NetSocket &socket, const std::vector<S_FakePacket> &packets)
{
    std::vector<uint32_t> words(20000, 0);
    C_BitStream s(words.data(), static_cast<uint32_t>(words.size() * 32));
    s.WriteBits(1, 8);
    s.WriteBits(0, 8);
    for (int i = 0; i < 2; ++i) {   // (the reliable channels' acknowledgements: the plain one writes none)
        s.WriteBits(0x7FFFFF, 23);
        s.WriteBits(0, 1);
    }
    s.WriteBits(static_cast<uint32_t>(packets.size()), 8);
    for (const S_FakePacket &p : packets) {
        s.WriteBits(0, 2);
        s.WriteBits(p.size, 16);
        s.WriteBits(5, 6);
        s.WriteBits(++g_sequence, 23);
        s.WriteBits(0, 1);
        s.WriteRawBytes(p.data.data(), static_cast<uint32_t>(p.data.size()));
    }
    const uint8_t *b = reinterpret_cast<const uint8_t *>(words.data());
    return Sealed(socket, std::vector<uint8_t>(b, b + s.ByteSize()));
}

void TestSocket()
{
    C_NetSocket socket;
    g_sockFaking = true;
    uint8_t *data;
    int size;
    uint32_t address, port;
    // Every short datagram, its fourth byte (or the buffer's old one) with the high bit.
    for (int n = 1; n < 8; ++n) {
        socket.m_buffer[3] = 0x80;   // (the old byte read after a datagram under 4 bytes)
        std::vector<uint8_t> d(static_cast<size_t>(n), 0);
        if (n > 3)
            d[3] = 0x80;
        if (n >= 7)
            d = Sealed(socket, std::vector<uint8_t>(d.begin() + 3, d.end()));
        Feed({d});
        socket.Receive(&data, &size, &address, &port);
        Check(data == nullptr, "a short datagram dropped");
    }
    // A message of fragments past 32 KB: 70 of 500 bytes, then a good message after it.
    std::vector<std::vector<uint8_t>> big;
    for (int i = 0; i < 70; ++i)
        big.push_back(Fragment(socket, i, 5, i == 69, std::vector<uint8_t>(500, static_cast<uint8_t>(i))));
    Feed(big);
    int whole = 0;
    for (size_t i = 0; i < big.size(); ++i) {
        socket.Receive(&data, &size, &address, &port);
        whole += data != nullptr;
    }
    Check(whole == 0 && socket.m_fragments.empty(), "a message past the buffer dropped, its fragments given back");
    Feed({Fragment(socket, 0, 6, false, std::vector<uint8_t>(400, 1)), Fragment(socket, 1, 6, true, std::vector<uint8_t>(300, 2))});
    socket.Receive(&data, &size, &address, &port);
    socket.Receive(&data, &size, &address, &port);
    Check(data != nullptr && size == 700 && data[0] == 1 && data[699] == 2, "a good message after it");
    // Two big fragments (each under the buffer, together past it).
    Feed({Fragment(socket, 0, 7, false, std::vector<uint8_t>(20000, 3)), Fragment(socket, 1, 7, true, std::vector<uint8_t>(20000, 4))});
    socket.Receive(&data, &size, &address, &port);
    socket.Receive(&data, &size, &address, &port);
    Check(data == nullptr && socket.m_fragments.empty(), "two big fragments past the buffer dropped");
    g_sockFaking = false;
}

void TestTransport()
{
    C_NetTransport transport;
    g_sockFaking = true;
    transport.Start(7777);
    transport.m_callback = Callback;
    auto *connection = new C_NetConnection(0, 0x0100007F, &transport.m_socket);
    connection->m_port = 7777;
    transport.Add(connection);
    C_NetSocket &socket = transport.m_socket;
    // A packet of 32 KB or more (read as negative), a packet past the datagram's end, then a good list.
    g_callbacks = 0;
    Feed({PacketList(socket, {{0x8000, {1, 2, 3}}}), PacketList(socket, {{0xFFFF, {}}}),
          PacketList(socket, {{3, {9, 9, 9}}, {600, std::vector<uint8_t>(20, 7)}}), PacketList(socket, {{4, {1, 2, 3, 4}}})});
    transport.ReceiveAll();
    // (The third: its first packet is good and comes; the second claims 600 bytes of 20: dropped. The fourth's comes.)
    if (g_callbacks != 2) {
        std::printf("  callbacks: %d; bytes received %d; the log:\n", g_callbacks, socket.m_received);
        for (const std::string &line : g_log[0])
            std::printf("    %.100s\n", line.c_str());
    }
    Check(g_callbacks == 2, "only the good packets reach the game");
    Check(g_lastPacket == std::vector<uint8_t>({1, 2, 3, 4}), "the last good packet whole");
    transport.DeleteConnections();
    g_sockFaking = false;
}

int main()
{
    TestSocket();
    TestTransport();
    std::printf("robust: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
