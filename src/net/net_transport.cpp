// The transport's connections and their manager (part 10c-iii): see net_transport.h.
#include <winsock2.h>

#include "net/net_transport.h"

#include <cstring>

#include "game/perf_timer.h"
#include "net/bitstream.h"
#include "net/net_channel.h"

C_GrowingBitStream g_netSendStream;

namespace {

int Now()
{
    return static_cast<int>(GetPerfTimeMs());
}

// The send stream's bytes (the bits rounded up).
int StreamBytes()
{
    const uint32_t bits = g_netSendStream.BitPosition();
    int bytes = static_cast<int>(bits) >> 3;
    if (bits & 7)
        ++bytes;
    return bytes;
}

DWORD WINAPI ReceiveThreadEntry(void *transport)    // 0x00746b30
{
    static_cast<C_NetTransport *>(transport)->ReceiveThread();
    return 0;
}

}  // namespace

// ---- the connection ------------------------------------------------------------------------------------------------

C_NetConnection::C_NetConnection(int id, uint32_t address, C_NetSocket *socket)
    : m_id(id), m_address(address), m_socket(socket)
{
    m_lastPing = Now();
    m_lastSend = m_lastReceive = Now();
    m_channels[0] = new C_NetChannel;
    m_channels[1] = new C_NetReliableChannel;
    m_channels[2] = new C_NetOrderedChannel;
    InitializeCriticalSection(&m_lock);
}

C_NetConnection::~C_NetConnection()
{
    DeleteCriticalSection(&m_lock);
    for (C_NetChannel *channel : m_channels)
        delete channel;
}

C_NetPacket *C_NetConnection::Send(const void *data, int size, int kind, int channel)
{
    C_NetPacket *packet = g_netPool.AllocPacket();
    void *copy = g_netPool.AllocBuffer(static_cast<size_t>(size));
    std::memcpy(copy, data, static_cast<size_t>(size));
    packet->m_data = copy;
    packet->m_size = size;
    packet->m_flags = static_cast<uint32_t>(channel);
    packet->m_kind = static_cast<uint32_t>(kind);
    EnterCriticalSection(&m_lock);
    m_channels[channel]->Send(packet);
    LeaveCriticalSection(&m_lock);
    return packet;
}

bool C_NetConnection::Receive(void **data, int *size, int *kind, uint32_t *flags)
{
    C_NetPacket *packet = PopReceived();
    if (!packet) {
        *data = nullptr;
        *size = 0;
        return false;
    }
    *data = packet->m_data;
    *size = packet->m_size;
    *flags = packet->m_flags;
    *kind = static_cast<int>(packet->m_kind);
    packet->m_data = nullptr;
    packet->m_size = 0;
    packet->Release();
    return true;
}

void C_NetConnection::Resend(C_NetPacket *packet)
{
    EnterCriticalSection(&m_lock);
    m_channels[packet->m_flags]->Send(packet);
    LeaveCriticalSection(&m_lock);
}

C_NetPacket *C_NetConnection::PopReceived()
{
    EnterCriticalSection(&m_lock);
    for (C_NetChannel *channel : m_channels) {
        if (C_NetPacket *packet = channel->PopReceived()) {
            LeaveCriticalSection(&m_lock);
            return packet;
        }
    }
    LeaveCriticalSection(&m_lock);
    return nullptr;
}

void C_NetConnection::SendPing()
{
    m_pingId = (m_pingId + 1) & 0xFF;
    C_NetPing ping;
    ping.m_connection = static_cast<uint32_t>(m_id);
    ping.m_id = m_pingId;
    EnterCriticalSection(&m_lock);
    g_netSendStream.ResetPosition();
    ping.Write(&g_netSendStream);
    ping.WriteBody(&g_netSendStream);
    m_socket->Send(g_netSendStream.Data(), StreamBytes(), m_address, m_port);
    LeaveCriticalSection(&m_lock);
}

void C_NetConnection::PingIfDue()
{
    const int now = Now();
    if (m_lastPing + 2000 < now) {
        SendPing();
        m_lastPing = now;
    }
}

void C_NetConnection::Flush(bool force)
{
    m_lastSend = Now();
    ResendAll();
    void *data;
    int size;
    Gather(&data, &size, force);
    if (data)
        m_socket->Send(data, size, m_address, m_port);
}

void C_NetConnection::HandleDatagram(uint32_t type, void *data, int size, NetPacketCallback callback)
{
    m_lastReceive = Now();
    C_BitStream stream(static_cast<uint32_t *>(data), static_cast<uint32_t>(size));
    C_NetHeader header;
    header.Read(&stream);
    if (type == 1) {
        ReadPackets(data, size);
        if (!callback)
            return;
        void *packet;
        int bytes, kind;
        uint32_t flags;
        while (Receive(&packet, &bytes, &kind, &flags)) {
            // (A late packet of the plain channel is dropped.)
            if ((flags & NET_PACKET_CHANNEL) != 0 || (flags & NET_PACKET_LATE) == 0)
                callback(m_id, packet, bytes, kind, flags);
            g_netPool.FreeBuffer(packet, bytes);
        }
        AcknowledgeAll();
    } else if (type == 2) {
        C_NetPing ping;
        ping.ReadBody(&stream);
        if (ping.m_reply == 0) {
            // Echoed with the sender's time.
            ping.m_connection = static_cast<uint32_t>(m_id);
            ping.m_reply = 1;
            EnterCriticalSection(&m_lock);
            g_netSendStream.ResetPosition();
            ping.Write(&g_netSendStream);
            ping.WriteBody(&g_netSendStream);
            m_socket->Send(g_netSendStream.Data(), StreamBytes(), m_address, m_port);
            LeaveCriticalSection(&m_lock);
        } else if (ping.m_reply == 1 && m_pingId == ping.m_id) {
            m_roundTrip = ping.m_time;
        }
    }
}

void C_NetConnection::ReadPackets(void *data, int size)
{
    C_NetPacketList list;
    C_BitStream stream(static_cast<uint32_t *>(data), static_cast<uint32_t>(size));
    EnterCriticalSection(&m_lock);
    list.Read(&stream);
    for (C_NetChannel *channel : m_channels)
        channel->ReadAck(&stream);
    list.ReadBody(&stream);
    while (C_NetPacket *packet = list.Pop()) {
        if (packet->m_session == m_session)
            m_channels[packet->m_flags]->Receive(packet);
        else
            packet->Release();
    }
    LeaveCriticalSection(&m_lock);
}

void C_NetConnection::Gather(void **data, int *size, bool force)
{
    C_NetPacketList list;
    int count = 0;
    EnterCriticalSection(&m_lock);
    if (m_port != -1) {
        for (C_NetChannel *channel : m_channels) {
            while (C_NetPacket *packet = channel->PopOutgoing()) {
                packet->m_session = m_session;
                list.Add(packet);
                ++count;
            }
        }
    }
    if (count == 0 && !force) {
        *data = nullptr;
        *size = 0;
        LeaveCriticalSection(&m_lock);
        return;
    }
    g_netSendStream.ResetPosition();
    list.m_connection = static_cast<uint32_t>(m_id);
    list.Write(&g_netSendStream);
    for (C_NetChannel *channel : m_channels)
        channel->WriteAck(&g_netSendStream);
    list.WriteBody(&g_netSendStream);
    *data = const_cast<uint32_t *>(g_netSendStream.Data());
    *size = StreamBytes();
    LeaveCriticalSection(&m_lock);
}

void C_NetConnection::ResendAll()
{
    if (m_port == -1)
        return;
    EnterCriticalSection(&m_lock);
    for (C_NetChannel *channel : m_channels)
        channel->Resend();
    LeaveCriticalSection(&m_lock);
}

void C_NetConnection::AcknowledgeAll()
{
    EnterCriticalSection(&m_lock);
    for (C_NetChannel *channel : m_channels)
        channel->Acknowledge();
    LeaveCriticalSection(&m_lock);
}

void C_NetConnection::SetResendTime(int ms)
{
    if (ms < 10 || ms > 700)
        ms = 700;
    EnterCriticalSection(&m_lock);
    static_cast<C_NetReliableChannel *>(m_channels[1])->m_resendTime = ms;
    static_cast<C_NetReliableChannel *>(m_channels[2])->m_resendTime = ms;
    LeaveCriticalSection(&m_lock);
}

// ---- the transport -------------------------------------------------------------------------------------------------

C_NetTransport::C_NetTransport()
{
    InitializeCriticalSection(&m_lock);
}

C_NetTransport::~C_NetTransport()
{
    DeleteCriticalSection(&m_lock);
}

void C_NetTransport::Start(int port)
{
    m_stopped = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    m_socket.Open(port);
    m_connections.clear();
    m_running = true;
    m_stop = false;
    m_session = 0;
    m_lastFlush = m_lastReceive = Now();
    CreateThread(nullptr, 0, ReceiveThreadEntry, this, 0, &m_threadId);
}

void C_NetTransport::Stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_stop = true;
    WaitForSingleObject(m_stopped, 1000);
    m_socket.Close();
    CloseHandle(m_stopped);
}

void C_NetTransport::DeleteConnections()
{
    EnterCriticalSection(&m_lock);
    while (!m_connections.empty()) {
        delete m_connections.back();
        m_connections.pop_back();
    }
    LeaveCriticalSection(&m_lock);
}

C_NetConnection *C_NetTransport::FindById(int id)
{
    EnterCriticalSection(&m_lock);
    for (C_NetConnection *connection : m_connections) {
        if (connection->m_id == id) {
            LeaveCriticalSection(&m_lock);
            return connection;
        }
    }
    LeaveCriticalSection(&m_lock);
    return nullptr;
}

C_NetConnection *C_NetTransport::FindByAddress(uint32_t address, int port)
{
    EnterCriticalSection(&m_lock);
    for (C_NetConnection *connection : m_connections) {
        if (connection->m_address == address && connection->m_port == port) {
            LeaveCriticalSection(&m_lock);
            return connection;
        }
    }
    LeaveCriticalSection(&m_lock);
    return nullptr;
}

C_NetTransport *C_NetTransport::Lock()
{
    EnterCriticalSection(&m_lock);
    return this;
}

void C_NetTransport::Unlock()
{
    LeaveCriticalSection(&m_lock);
}

void C_NetTransport::ReceiveAll()
{
    uint8_t *data;
    int size;
    uint32_t address, port;
    m_socket.FlushDelayed();
    m_socket.Receive(&data, &size, &address, &port);
    while (data) {
        m_lastReceive = Now();
        C_BitStream stream(reinterpret_cast<uint32_t *>(data), static_cast<uint32_t>(size));
        C_NetHeader header;
        header.Read(&stream);
        EnterCriticalSection(&m_lock);
        if (C_NetConnection *connection = FindById(static_cast<int>(header.m_connection))) {
            connection->m_port = static_cast<int>(port);
            connection->m_address = address;
            connection->HandleDatagram(header.m_type, data, size, m_callback);
        }
        LeaveCriticalSection(&m_lock);
        m_socket.FlushDelayed();
        m_socket.Receive(&data, &size, &address, &port);
    }
}

void C_NetTransport::Add(C_NetConnection *connection)
{
    EnterCriticalSection(&m_lock);
    connection->m_session = m_session;
    m_connections.push_back(connection);
    LeaveCriticalSection(&m_lock);
}

void C_NetTransport::Remove(C_NetConnection *connection)
{
    EnterCriticalSection(&m_lock);
    for (size_t i = 0; i < m_connections.size(); ++i) {
        if (m_connections[i] == connection) {
            if (m_connections.size() > 1)
                m_connections[i] = m_connections.back();
            m_connections.pop_back();
            break;
        }
    }
    LeaveCriticalSection(&m_lock);
}

void C_NetTransport::Tick()
{
    EnterCriticalSection(&m_lock);
    for (C_NetConnection *connection : m_connections)
        connection->PingIfDue();
    LeaveCriticalSection(&m_lock);
    if (Now() - m_lastFlush > 500)
        FlushAll(true);
}

void C_NetTransport::FlushAll(bool force)
{
    if (!m_running)
        return;
    m_lastFlush = Now();
    EnterCriticalSection(&m_lock);
    for (C_NetConnection *connection : m_connections)
        connection->Flush(force);
    LeaveCriticalSection(&m_lock);
}

void C_NetTransport::ReceiveThread()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    while (!m_stop) {
        Tick();
        ReceiveAll();
    }
    SetEvent(m_stopped);
}
