// The transport's UDP socket (part 10c-ii): see net_socket.h.
// (The original's gethostbyname / inet_ntoa.)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>

#include "net/net_socket.h"

#include <cstring>

#include "common/msvc_sort.h"
#include "game/perf_timer.h"
#include "net/net_channel.h"

int g_netLastFragmentTime;

namespace {

constexpr int kBufferSize = 0x8000;
constexpr int kFragmentData = 500;
constexpr int kFragmentLifetime = 20000;              // ms

uint32_t LoadDword(const uint8_t *p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void StoreDword(uint8_t *p, uint32_t v)
{
    std::memcpy(p, &v, 4);
}

bool FragmentLess(const S_NetFragment &a, const S_NetFragment &b)   // 0x007480f0 (__fastcall)
{
    if (a.message == b.message)
        return a.index < b.index;
    return a.message < b.message;
}

DWORD WINAPI SendThreadEntry(void *socket)            // 0x007487d0
{
    static_cast<C_NetSocket *>(socket)->SendThread();
    return 0;
}

}  // namespace

C_NetSocket::C_NetSocket()
{
    m_buffer = static_cast<uint8_t *>(operator new(kBufferSize));
    m_fragmentBuffer = static_cast<uint8_t *>(operator new(800));
    InitializeCriticalSection(&m_lock);
}

C_NetSocket::~C_NetSocket()
{
    // (The queued datagrams' buffers are not given back: as the original.)
    DeleteCriticalSection(&m_lock);
    operator delete(m_buffer);
    operator delete(m_fragmentBuffer);
}

void C_NetSocket::Open(int port)
{
    if (m_socket != static_cast<uintptr_t>(-1))
        Close();
    WSADATA data;
    WSAStartup(0x202, &data);
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = 0;
    address.sin_port = port == -1 ? 0 : htons(static_cast<u_short>(port));
    std::memcpy(m_address, &address, sizeof address);
    bind(m_socket, reinterpret_cast<const sockaddr *>(m_address), sizeof address);
    m_stop = false;
    // (Fix: the events made before the thread that waits on them; the original makes them after starting it.)
    m_stopped = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    m_wake = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    CreateThread(nullptr, 0, SendThreadEntry, this, 0, &m_threadId);
}

void C_NetSocket::Close()
{
    if (m_socket == static_cast<uintptr_t>(-1))
        return;
    m_stop = true;
    SetEvent(m_wake);
    WaitForSingleObject(m_stopped, 1000);
    CloseHandle(m_stopped);
    CloseHandle(m_wake);
    closesocket(m_socket);
    m_socket = static_cast<uintptr_t>(-1);
    WSACleanup();
}

void C_NetSocket::Seal(uint8_t *buffer, int size)
{
    const int words = size >> 2;
    uint32_t sum = 0;
    for (int i = 0; i < words; ++i)
        sum += LoadDword(buffer + 3 + i * 4);
    StoreDword(buffer, LoadDword(buffer) ^ ((LoadDword(buffer) ^ sum) & 0xFFFFFF));
    const uint32_t key = static_cast<uint32_t>(size) * 0x800 + m_key;
    for (int i = 0; i < words; ++i)
        StoreDword(buffer + 3 + i * 4, LoadDword(buffer + 3 + i * 4) ^ key);
}

bool C_NetSocket::Unseal(uint8_t *buffer, int size)
{
    const uint32_t key = static_cast<uint32_t>(size) * 0x800 + m_key;
    const int words = size >> 2;
    for (int i = 0; i < words; ++i)
        StoreDword(buffer + 3 + i * 4, LoadDword(buffer + 3 + i * 4) ^ key);
    uint32_t sum = 0;
    for (int i = 0; i < words; ++i)
        sum += LoadDword(buffer + 3 + i * 4);
    return ((LoadDword(buffer) ^ sum) & 0xFFFFFF) == 0;
}

void C_NetSocket::Send(const void *data, int size, uint32_t address, int port)
{
    const auto *from = static_cast<const uint8_t *>(data);
    if (size < kFragmentData) {
        auto *buffer = static_cast<uint8_t *>(g_netPool.AllocBuffer(static_cast<size_t>(size + 3)));
        std::memcpy(buffer + 3, from, static_cast<size_t>(size));
        Seal(buffer, size);
        QueueSend(buffer, size + 3, address, port);
        return;
    }
    uint8_t *f = m_fragmentBuffer;
    uint32_t index = 0;
    ++m_message;
    while (size > 0) {
        f[3] = static_cast<uint8_t>(index | 0x80);
        StoreDword(f + 4, m_message);
        int n = kFragmentData;
        if (size < kFragmentData + 1) {
            StoreDword(f + 4, m_message | 0x80000000u);
            n = size;
        }
        std::memcpy(f + 8, from, static_cast<size_t>(n));
        from += n;
        size -= n;
        ++index;
        Seal(f, n + 5);
        n += 8;
        S_NetDatagram datagram = {};
        datagram.data = g_netPool.AllocBuffer(static_cast<size_t>(n));
        std::memcpy(datagram.data, f, static_cast<size_t>(n));
        datagram.size = n;
        datagram.address = address;
        datagram.port = port;
        m_delayed.push_back(datagram);
    }
}

void C_NetSocket::SendRaw(const void *data, int size, uint32_t address, int port)
{
    auto *to = reinterpret_cast<sockaddr_in *>(m_address);
    to->sin_port = htons(static_cast<u_short>(port));
    to->sin_addr.s_addr = address;
    sendto(m_socket, static_cast<const char *>(data), size, 0, reinterpret_cast<const sockaddr *>(m_address),
           sizeof(sockaddr_in));
    m_sent += size;
}

void C_NetSocket::Receive(uint8_t **data, int *size, uint32_t *address, uint32_t *port)
{
    ReceiveRaw(data, size, address, port);
    if (!*data)
        return;
    const int sealed = *size - 3;
    *size = sealed;
    if (Unseal(*data, sealed)) {
        *data += 3;
        if (static_cast<int8_t>(**data) >= 0)
            return;
        // A fragment: kept until its message is whole.
        S_NetFragment fragment = {};
        fragment.data = g_netPool.AllocBuffer(static_cast<size_t>(*size));
        std::memcpy(fragment.data, *data + 5, static_cast<size_t>(*size - 5));
        fragment.size = *size - 5;
        const uint32_t header = LoadDword(*data + 1);
        fragment.last = (header >> 31) != 0;
        fragment.message = header & 0x7FFFFFFF;
        fragment.index = **data & 0x7F;
        fragment.time = static_cast<int>(GetPerfTimeMs());
        m_fragments.push_back(fragment);
        if (Reassemble(data, size))
            return;
    }
    *data = nullptr;
}

bool C_NetSocket::FindComplete(int *first, int *count)
{
    const int now = static_cast<int>(GetPerfTimeMs());
    size_t i = 0;
    for (;;) {
        if (i >= m_fragments.size())
            return false;
        S_NetFragment *fragment = &m_fragments[i];
        if (now > fragment->time + kFragmentLifetime) {
            // Too old: dropped (its buffer not given back, as the original).
            m_fragments.erase(m_fragments.begin() + static_cast<ptrdiff_t>(i));
            continue;
        }
        if (fragment->index != 0) {
            ++i;
            continue;
        }
        const size_t start = i;
        const uint32_t message = fragment->message;
        uint32_t k = 0;
        do {
            if (k != m_fragments[i].index || i >= m_fragments.size())
                break;
            if (m_fragments[i].last) {
                *first = static_cast<int>(start);
                *count = static_cast<int>(k + 1);
                return true;
            }
            ++i;
            ++k;
        } while (i < m_fragments.size() && m_fragments[i].message == message);
    }
}

bool C_NetSocket::Reassemble(uint8_t **data, int *size)
{
    msvc6::Sort(m_fragments.data(), m_fragments.data() + m_fragments.size(), FragmentLess);
    int first, count;
    if (!FindComplete(&first, &count))
        return false;
    int total = 0;
    for (int n = count; n != 0; --n) {
        S_NetFragment &fragment = m_fragments[static_cast<size_t>(first)];
        std::memcpy(m_buffer + total, fragment.data, static_cast<size_t>(fragment.size));
        g_netPool.FreeBuffer(fragment.data, fragment.size);
        total += fragment.size;
        m_fragments.erase(m_fragments.begin() + first);
    }
    *data = m_buffer;
    *size = total;
    return true;
}

void C_NetSocket::ReceiveRaw(uint8_t **data, int *size, uint32_t *address, uint32_t *port)
{
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(m_socket, &reads);
    fd_set writes, errors;
    FD_ZERO(&writes);
    FD_ZERO(&errors);
    timeval wait = {0, 20000};
    if (select(static_cast<int>(m_socket + 1), &reads, &writes, &errors, &wait) >= 0 && !FD_ISSET(m_socket, &reads)) {
        *data = nullptr;
        return;
    }
    sockaddr_in from = {};
    int length = sizeof from;
    const int n = recvfrom(m_socket, reinterpret_cast<char *>(m_buffer), kBufferSize, 0,
                           reinterpret_cast<sockaddr *>(&from), &length);
    if (n != 0 && n != -1) {
        *data = m_buffer;
        *address = from.sin_addr.s_addr;
        *port = ntohs(from.sin_port);
        *size = n;
        m_received += n;
        return;
    }
    *data = nullptr;
}

void C_NetSocket::FlushDelayed()
{
    if (m_delayed.empty())
        return;
    const int now = static_cast<int>(GetPerfTimeMs());
    if (g_netLastFragmentTime < now) {
        const S_NetDatagram datagram = m_delayed.front();
        g_netLastFragmentTime = now;
        m_delayed.pop_front();
        QueueSend(datagram.data, datagram.size, datagram.address, datagram.port);
    }
}

void C_NetSocket::QueueSend(void *data, int size, uint32_t address, int port)
{
    S_NetDatagram datagram = {};
    datagram.data = data;
    datagram.size = size;
    datagram.address = address;
    datagram.port = port;
    EnterCriticalSection(&m_lock);
    m_queue.push_back(datagram);
    LeaveCriticalSection(&m_lock);
    SetEvent(m_wake);
}

void C_NetSocket::SendQueued()
{
    EnterCriticalSection(&m_lock);
    while (!m_queue.empty()) {
        const S_NetDatagram datagram = m_queue.front();
        m_queue.pop_front();
        LeaveCriticalSection(&m_lock);
        SendRaw(datagram.data, datagram.size, datagram.address, datagram.port);
        g_netPool.FreeBuffer(datagram.data, datagram.size);
        EnterCriticalSection(&m_lock);
    }
    LeaveCriticalSection(&m_lock);
}

void C_NetSocket::SendThread()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    while (!m_stop) {
        WaitForSingleObject(m_wake, INFINITE);
        SendQueued();
        ResetEvent(m_wake);
    }
    SetEvent(m_stopped);
}

void C_NetSocket::TakeCounters(int *received, int *sent)
{
    *received = m_received;
    *sent = m_sent;
    m_sent = 0;
    m_received = 0;
}

void C_NetSocket::LookupLocalAddress()
{
    WSADATA data;
    if (m_socket == static_cast<uintptr_t>(-1))
        WSAStartup(0x202, &data);
    char name[80];
    if (gethostname(name, sizeof name) != -1) {
        if (hostent *host = gethostbyname(name)) {
            if (host->h_addr_list[0]) {
                in_addr address;
                std::memcpy(&address, host->h_addr_list[0], sizeof address);
                std::strcpy(m_localName, inet_ntoa(address));
                m_localAddress = ntohl(address.s_addr);
            }
        }
    }
    if (m_socket == static_cast<uintptr_t>(-1))
        WSACleanup();
}

uint32_t C_NetSocket::GetLocalAddress()
{
    if (m_localAddress == 0)
        LookupLocalAddress();
    return m_localAddress;
}
