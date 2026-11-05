// The transport's packets and channels (part 10c-i): see net_channel.h.
#include "net/net_channel.h"

#include <cstdlib>

#include "game/perf_timer.h"
#include "net/bitstream.h"

C_NetPool g_netPool;

// ---- the reference count and the packet ----------------------------------------------------------------------------

void C_NetRefCounted::Release()
{
    if (--m_refs < 1)
        delete this;
}

C_NetPacket::~C_NetPacket()
{
    FreeData();
}

void C_NetPacket::Release()
{
    if (--m_refs < 1) {
        FreeData();
        Clear();
        g_netPool.FreePacket(this);
    }
}

void C_NetPacket::Clear()
{
    m_flags = 0;
    m_kind = 0;
    m_size = 0;
    m_data = nullptr;
    m_session = 0;
    m_refs = 0;
}

void C_NetPacket::FreeData()
{
    if (m_data)
        g_netPool.FreeBuffer(m_data, m_size);
}

void C_NetPacket::Write(C_BitStream *stream)
{
    stream->WriteBits(m_flags, 2);
    stream->WriteBits(static_cast<uint32_t>(m_size), 16);
    stream->WriteBits(m_kind, 6);
    stream->WriteBits(m_sequence, 23);
    stream->WriteBits(m_session, 1);
    stream->WriteRawBytes(m_data, static_cast<uint32_t>(m_size));
}

void C_NetPacket::Read(C_BitStream *stream)
{
    m_flags = stream->ReadBits(2);
    m_size = static_cast<int16_t>(stream->ReadBits(16));
    m_kind = stream->ReadBits(6);
    m_sequence = stream->ReadBits(23);
    m_session = stream->ReadBits(1);
    m_data = g_netPool.AllocBuffer(static_cast<size_t>(m_size));
    stream->ReadRawBytes(m_data, static_cast<uint32_t>(m_size));
}

// ---- the pools -----------------------------------------------------------------------------------------------------

namespace {

constexpr int kBufferSize = 1000;

void GrowBuffers(S_NetFreeList &list)
{
    for (int i = 0; i < list.growBy; ++i) {
        list.items.push_back(operator new(kBufferSize));
        ++list.count;
    }
}

void GrowPackets(S_NetFreeList &list)
{
    for (int i = 0; i < list.growBy; ++i) {
        list.items.push_back(new C_NetPacket);
        ++list.count;
    }
}

void *Pop(S_NetFreeList &list)
{
    void *item = list.items.back();
    list.items.pop_back();
    --list.count;
    return item;
}

}  // namespace

C_NetPool::C_NetPool()
{
    GrowBuffers(m_buffers);
    GrowPackets(m_packets);
    InitializeCriticalSection(&m_lock);
}

C_NetPool::~C_NetPool()
{
    DeleteCriticalSection(&m_lock);
    for (void *packet : m_packets.items)
        delete static_cast<C_NetPacket *>(packet);
    m_packets.items.clear();
    m_packets.count = 0;
    for (void *buffer : m_buffers.items)
        operator delete(buffer);
    m_buffers.items.clear();
    m_buffers.count = 0;
}

void *C_NetPool::AllocBuffer(size_t size)
{
    EnterCriticalSection(&m_lock);
    if (static_cast<int>(size) <= kBufferSize) {
        if (m_buffers.items.empty())
            GrowBuffers(m_buffers);
        void *buffer = Pop(m_buffers);
        LeaveCriticalSection(&m_lock);
        return buffer;
    }
    void *buffer = std::malloc(size);
    LeaveCriticalSection(&m_lock);
    return buffer;
}

void C_NetPool::FreeBuffer(void *buffer, int size)
{
    EnterCriticalSection(&m_lock);
    if (size > kBufferSize) {
        std::free(buffer);
    } else if (buffer) {
        m_buffers.items.push_back(buffer);
        ++m_buffers.count;
    }
    LeaveCriticalSection(&m_lock);
}

C_NetPacket *C_NetPool::AllocPacket()
{
    EnterCriticalSection(&m_lock);
    if (m_packets.items.empty())
        GrowPackets(m_packets);
    auto *packet = static_cast<C_NetPacket *>(Pop(m_packets));
    LeaveCriticalSection(&m_lock);
    return packet;
}

void C_NetPool::FreePacket(C_NetPacket *packet)
{
    EnterCriticalSection(&m_lock);
    if (packet) {
        m_packets.items.push_back(packet);
        ++m_packets.count;
    }
    LeaveCriticalSection(&m_lock);
}

// ---- the channels --------------------------------------------------------------------------------------------------

void UnwrapSequence(uint32_t *seq, uint32_t reference)
{
    const uint32_t low = *seq & 0x7FFFFF;
    *seq = low;
    const uint32_t ref = reference & 0x7FFFFF;
    if (ref < 0x5FFFFE) {
        if (ref < 0x1FFFFF && low > 0x5FFFFD) {
            *seq = ((reference - 0x7FFFFF) & 0xFF800000u) + low;
            return;
        }
    } else if (low < 0x1FFFFF) {
        *seq = ((reference + 0x7FFFFF) & 0xFF800000u) + low;
        return;
    }
    *seq = (reference & 0xFF800000u) + low;
}

C_NetChannel::C_NetChannel() = default;

C_NetChannel::~C_NetChannel()
{
    // The packets to send released (the last first); those received only forgotten.
    while (!m_outgoing.empty()) {
        m_outgoing.back()->Release();
        m_outgoing.pop_back();
    }
}

void C_NetChannel::Send(C_NetPacket *packet)
{
    packet->m_sequence = ++m_sent;
    packet->AddRef();
    m_outgoing.push_back(packet);
}

C_NetPacket *C_NetChannel::PopReceived()
{
    if (m_incoming.empty())
        return nullptr;
    C_NetPacket *packet = m_incoming.front();
    m_incoming.pop_front();
    return packet;
}

C_NetPacket *C_NetChannel::PopOutgoing()
{
    if (m_outgoing.empty())
        return nullptr;
    C_NetPacket *packet = m_outgoing.front();
    m_outgoing.pop_front();
    return packet;
}

void C_NetChannel::Receive(C_NetPacket *packet)
{
    uint32_t seq = packet->m_sequence;
    const uint32_t newest = m_received;
    UnwrapSequence(&seq, newest);
    if (static_cast<int>(newest) < static_cast<int>(seq) || static_cast<int>(newest - seq) > 9)
        m_received = seq;
    else
        packet->m_flags |= NET_PACKET_LATE;
    packet->AddRef();
    m_incoming.push_back(packet);
}

C_NetReliableChannel::C_NetReliableChannel() = default;

C_NetReliableChannel::~C_NetReliableChannel()
{
    // The held packets released (the last first); those received out of order only forgotten.
    while (!m_pending.empty()) {
        m_pending.back().packet->Release();
        m_pending.pop_back();
    }
}

void C_NetReliableChannel::Send(C_NetPacket *packet)
{
    C_NetChannel::Send(packet);
    const int interval = m_resendTime;
    const int now = static_cast<int>(GetPerfTimeMs());
    packet->AddRef();
    m_pending.push_back({now + interval, interval, packet});
}

void C_NetReliableChannel::Receive(C_NetPacket *packet)
{
    uint32_t seq = packet->m_sequence;
    UnwrapSequence(&seq, C_NetChannel::m_received);
    if (static_cast<int>(seq) <= static_cast<int>(m_delivered)) {
        packet->Release();
        return;
    }
    packet->m_sequence = seq;
    for (C_NetPacket *held : m_unordered) {
        if (held->m_sequence == seq) {
            packet->Release();
            return;
        }
    }
    packet->AddRef();
    m_unordered.push_back(packet);
    C_NetChannel::Receive(packet);
}

void C_NetReliableChannel::WriteAck(C_BitStream *stream)
{
    stream->WriteBits(m_delivered & 0x7FFFFF, 23);
    stream->WriteBits(0, 1);
}

void C_NetReliableChannel::ReadAck(C_BitStream *stream)
{
    uint32_t seq = stream->ReadBits(23);
    stream->ReadBits(1);
    UnwrapSequence(&seq, m_acked);
    if (static_cast<int>(m_acked) < static_cast<int>(seq))
        m_acked = seq;
}

void C_NetReliableChannel::Resend()
{
    while (!m_pending.empty() &&
           static_cast<int>(m_pending.front().packet->m_sequence) <= static_cast<int>(m_acked)) {
        m_pending.front().packet->Release();
        m_pending.pop_front();
    }
    const int now = static_cast<int>(GetPerfTimeMs());
    int budget = 7;
    for (S_Pending &pending : m_pending) {
        if (budget == 0)
            return;
        if (pending.time < now) {
            pending.time = pending.interval + now;
            const int interval = pending.interval;
            pending.interval = interval * 2;
            if (interval * 2 > 5000)
                pending.interval = 5000;
            pending.packet->AddRef();
            m_outgoing.push_back(pending.packet);
            --budget;
        }
    }
}

void C_NetReliableChannel::Acknowledge()
{
    for (;;) {
        const uint32_t next = m_delivered + 1;
        auto it = m_unordered.begin();
        while (it != m_unordered.end() && (*it)->m_sequence != next)
            ++it;
        if (it == m_unordered.end())
            return;
        m_delivered = next;
        C_NetPacket *packet = *it;
        m_unordered.erase(it);
        packet->Release();
    }
}

C_NetPacket *C_NetOrderedChannel::PopReceived()
{
    const uint32_t next = m_next + 1;
    for (auto it = m_incoming.begin(); it != m_incoming.end(); ++it) {
        if ((*it)->m_sequence == next) {
            m_next = next;
            C_NetPacket *packet = *it;
            m_incoming.erase(it);
            packet->m_flags &= ~NET_PACKET_LATE;
            return packet;
        }
    }
    return nullptr;
}

// ---- the messages --------------------------------------------------------------------------------------------------

void C_NetHeader::Read(C_BitStream *stream)
{
    m_type = stream->ReadBits(8);
    m_connection = stream->ReadBits(8);
}

void C_NetHeader::Write(C_BitStream *stream)
{
    stream->WriteBits(m_type, 8);
    stream->WriteBits(m_connection, 8);
}

C_NetPacketList::~C_NetPacketList()
{
    while (!m_packets.empty()) {
        m_packets.front()->Release();
        m_packets.pop_front();
    }
}

void C_NetPacketList::WriteBody(C_BitStream *stream)
{
    stream->WriteBits(static_cast<uint32_t>(m_packets.size()), 8);
    for (C_NetPacket *packet : m_packets)
        packet->Write(stream);
}

void C_NetPacketList::ReadBody(C_BitStream *stream)
{
    for (uint32_t n = stream->ReadBits(8); n != 0; --n) {
        C_NetPacket *packet = g_netPool.AllocPacket();
        packet->Read(stream);
        m_packets.push_back(packet);
    }
}

C_NetPacket *C_NetPacketList::Pop()
{
    if (m_packets.empty())
        return nullptr;
    C_NetPacket *packet = m_packets.front();
    m_packets.pop_front();
    return packet;
}

void C_NetPing::WriteBody(C_BitStream *stream)
{
    stream->WriteBits(m_reply, 1);
    stream->WriteBits(m_id, 8);
    if (m_reply == 0)
        stream->WriteBits(static_cast<uint32_t>(GetPerfTimeMs()), 32);
    else
        stream->WriteBits(m_time, 32);
}

void C_NetPing::ReadBody(C_BitStream *stream)
{
    m_reply = stream->ReadBits(1);
    m_id = stream->ReadBits(8);
    uint32_t time;
    if (m_reply == 0)
        time = stream->ReadBits(32);
    else
        time = static_cast<uint32_t>(GetPerfTimeMs()) - stream->ReadBits(32);
    m_time = time;
    if (time == 0)
        m_time = 1;
}
