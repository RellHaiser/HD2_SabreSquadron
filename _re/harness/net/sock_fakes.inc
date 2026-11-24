// Net part 4: the test's fakes of the system calls the socket makes (see sock_fakes.h), shared by sock_diff.cpp and
// trans_diff.cpp: each logs its call by side (g_log) and answers from the side's script (g_script); outside the compared
// calls (g_sockFaking false) they call the real functions. RedirectSystemCalls points the original's imports at them.
// Included at file scope after the includer's N() and Hex(); it ends inside an anonymous namespace.

// ---- the fakes (both sides; g_side says whose call it is; the real ones outside the calls compared) ----
bool g_sockFaking;
// (Outside the compared calls: the real function, by the fake's name less its "Fake_".)
#define PASS(dll, type, ...)                                                                                          \
    if (!g_sockFaking)                                                                                                \
    return reinterpret_cast<type>(reinterpret_cast<void *>(GetProcAddress(LoadLibraryA(dll), __func__ + 5)))(__VA_ARGS__)
int g_side;                       // 0 the port, 1 the original
int g_recvClockStep;              // ms the clock (the includer's g_now) moves on at each datagram recvfrom gives
std::vector<std::string> g_log[2];

struct S_Wire {                   // a datagram for recvfrom
    std::vector<uint8_t> bytes;
    uint32_t address;
    uint16_t port;
    int result;                   // recvfrom's answer when not the bytes' length (0, -1), else 1
};
struct S_Script {
    std::deque<int> selects;      // select's answers: 1 readable, 0 nothing, -1 an error
    std::deque<S_Wire> wire;
    int hostResult = 0;           // gethostname: 0 a name, -1 fails
    int host = 0;                 // gethostbyname: 0 none, 1 no address, 2 an address
    uint32_t hostAddress = 0;
    int waits = 0;                // SendThread's waits before its stop flag is set
    int selectsBeforeStop = -1;   // (the transport's receive thread) selects before g_selectStopFlag is set; -1 never
} g_script[2];
uint8_t *g_stopFlag[2];
uint8_t *g_selectStopFlag[2];
LPTHREAD_START_ROUTINE g_threadStart[2];
void *g_threadParam[2];
int g_handles[2], g_events[2];   // (counted apart: the port makes its events before the thread, the original after)

namespace {

void Log(const std::string &s) { g_log[g_side].push_back(s); }
std::string Bytes(const void *p, int n)
{
    std::string s;
    const auto *b = static_cast<const uint8_t *>(p);
    char t[4];
    for (int i = 0; i < n; ++i) {
        std::snprintf(t, sizeof t, "%02x", b[i]);
        s += t;
    }
    return s;
}

}   // namespace

int WSAAPI Fake_WSAStartup(WORD version, LPWSADATA data)
{
    PASS("ws2_32.dll", int(WSAAPI *)(WORD, LPWSADATA), version, data);
    Log("WSAStartup " + Hex(version));
    return 0;
}
int WSAAPI Fake_WSACleanup()
{
    PASS("ws2_32.dll", int(WSAAPI *)());
    Log("WSACleanup");
    return 0;
}
SOCKET WSAAPI Fake_socket(int af, int type, int protocol)
{
    PASS("ws2_32.dll", SOCKET(WSAAPI *)(int, int, int), af, type, protocol);
    Log("socket " + N(af) + " " + N(type) + " " + N(protocol));
    return 0x77;
}
int WSAAPI Fake_bind(SOCKET s, const sockaddr *name, int length)
{
    PASS("ws2_32.dll", int(WSAAPI *)(SOCKET, const sockaddr *, int), s, name, length);
    Log("bind " + N(s) + " " + Bytes(name, length) + " " + N(length));
    return 0;
}
int WSAAPI Fake_closesocket(SOCKET s)
{
    PASS("ws2_32.dll", int(WSAAPI *)(SOCKET), s);
    Log("closesocket " + N(s));
    return 0;
}
int WSAAPI Fake_sendto(SOCKET s, const char *buffer, int length, int flags, const sockaddr *to, int toLength)
{
    PASS("ws2_32.dll", int(WSAAPI *)(SOCKET, const char *, int, int, const sockaddr *, int), s, buffer, length, flags, to, toLength);
    Log("sendto " + N(s) + " " + Bytes(buffer, length) + " " + N(flags) + " " + Bytes(to, toLength));
    return length;
}
int WSAAPI Fake_recvfrom(SOCKET s, char *buffer, int length, int flags, sockaddr *from, int *fromLength)
{
    PASS("ws2_32.dll", int(WSAAPI *)(SOCKET, char *, int, int, sockaddr *, int *), s, buffer, length, flags, from, fromLength);
    S_Script &sc = g_script[g_side];
    Log("recvfrom " + N(s) + " " + N(length) + " " + N(flags) + " " + N(*fromLength));
    if (sc.wire.empty())
        return -1;
    const S_Wire w = sc.wire.front();
    g_now += g_recvClockStep;
    sc.wire.pop_front();
    if (w.result != 1)
        return w.result;
    const int n = std::min(length, static_cast<int>(w.bytes.size()));
    std::memcpy(buffer, w.bytes.data(), static_cast<size_t>(n));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = w.address;
    a.sin_port = htons(w.port);
    std::memcpy(from, &a, sizeof a);
    *fromLength = sizeof a;
    return n;
}
int WSAAPI Fake_select(int count, fd_set *reads, fd_set *writes, fd_set *errors, const timeval *wait)
{
    PASS("ws2_32.dll", int(WSAAPI *)(int, fd_set *, fd_set *, fd_set *, const timeval *), count, reads, writes, errors, wait);
    S_Script &sc = g_script[g_side];
    Log("select " + N(count) + " " + N(reads->fd_count) + " " + N(writes->fd_count) + " " + N(errors->fd_count) + " " +
        N(wait->tv_sec) + "." + N(wait->tv_usec));
    int r = 0;
    if (g_selectStopFlag[g_side] && sc.selectsBeforeStop >= 0 && sc.selectsBeforeStop-- == 0)
        *g_selectStopFlag[g_side] = 1;
    if (!sc.selects.empty()) {
        r = sc.selects.front();
        sc.selects.pop_front();
    }
    if (r <= 0 && r != -1)
        reads->fd_count = 0;
    return r;
}
int WSAAPI Fake_gethostname(char *name, int length)
{
    PASS("ws2_32.dll", int(WSAAPI *)(char *, int), name, length);
    Log("gethostname " + N(length));
    if (g_script[g_side].hostResult != 0)
        return -1;
    std::strcpy(name, "testhost");
    return 0;
}
hostent *WSAAPI Fake_gethostbyname(const char *name)
{
    PASS("ws2_32.dll", hostent *(WSAAPI *)(const char *), name);
    Log(std::string("gethostbyname ") + name);
    static hostent host[2];
    static char *lists[2][2];
    static uint32_t addresses[2];
    const S_Script &sc = g_script[g_side];
    if (sc.host == 0)
        return nullptr;
    addresses[g_side] = sc.hostAddress;
    lists[g_side][0] = sc.host == 2 ? reinterpret_cast<char *>(&addresses[g_side]) : nullptr;
    lists[g_side][1] = nullptr;
    host[g_side].h_addr_list = lists[g_side];
    host[g_side].h_length = 4;
    return &host[g_side];
}
HANDLE WINAPI Fake_CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags,
                                LPDWORD id)
{
    PASS("kernel32.dll", HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD), attributes,
         stack, start, parameter, flags, id);
    Log("CreateThread " + N(stack) + " " + N(flags));
    g_threadStart[g_side] = start;   // (run later by the test: OpThreadEntry)
    g_threadParam[g_side] = parameter;
    if (id)
        *id = 0x4444;
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x5000 + g_handles[g_side]++));
}
HANDLE WINAPI Fake_CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL initial, LPCSTR name)
{
    PASS("kernel32.dll", HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR), attributes, manual, initial, name);
    Log("CreateEventA " + N(manual) + " " + N(initial) + " " + (name ? name : "null"));
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x6000 + g_events[g_side]++));
}
BOOL WINAPI Fake_SetEvent(HANDLE event)
{
    PASS("kernel32.dll", BOOL(WINAPI *)(HANDLE), event);
    Log("SetEvent " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(event))));
    return TRUE;
}
BOOL WINAPI Fake_ResetEvent(HANDLE event)
{
    PASS("kernel32.dll", BOOL(WINAPI *)(HANDLE), event);
    Log("ResetEvent " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(event))));
    return TRUE;
}
DWORD WINAPI Fake_WaitForSingleObject(HANDLE handle, DWORD ms)
{
    PASS("kernel32.dll", DWORD(WINAPI *)(HANDLE, DWORD), handle, ms);
    Log("WaitForSingleObject " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle))) + " " + N(ms));
    if (g_stopFlag[g_side] && g_script[g_side].waits-- <= 0)
        *g_stopFlag[g_side] = 1;
    return WAIT_OBJECT_0;
}
BOOL WINAPI Fake_CloseHandle(HANDLE handle)
{
    PASS("kernel32.dll", BOOL(WINAPI *)(HANDLE), handle);
    Log("CloseHandle " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle))));
    return TRUE;
}
BOOL WINAPI Fake_SetThreadPriority(HANDLE thread, int priority)
{
    PASS("kernel32.dll", BOOL(WINAPI *)(HANDLE, int), thread, priority);
    Log("SetThreadPriority " + N(priority));
    return TRUE;
}

namespace {

// The original's imports of `real` (its address in the loaded DLL) pointed at `to`.
int RedirectImport(const void *real, const void *to)
{
    if (!real)
        return 0;
    const auto *base = reinterpret_cast<const uint8_t *>(0x400000);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int n = 0;
    for (auto *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); imp->Name; ++imp)
        for (auto *t = reinterpret_cast<IMAGE_THUNK_DATA32 *>(const_cast<uint8_t *>(base) + imp->FirstThunk); t->u1.Function; ++t)
            if (t->u1.Function == static_cast<DWORD>(reinterpret_cast<uintptr_t>(real))) {
                t->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(to));
                ++n;
            }
    return n;
}
bool RedirectApi(const char *name, const void *to)
{
    int n = 0;
    for (const char *dll : {"ws2_32.dll", "wsock32.dll", "kernel32.dll"}) {
        HMODULE m = GetModuleHandleA(dll);
        if (!m)
            m = LoadLibraryA(dll);
        if (m)
            n += RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, name)), to);
    }
    if (!n)
        std::printf("the original does not import %s\n", name);
    return n > 0;
}

// The original's imports of the faked functions pointed at the fakes; false when one is not imported.
bool RedirectSystemCalls()
{
    bool ok = true;
    ok &= RedirectApi("WSAStartup", reinterpret_cast<const void *>(Fake_WSAStartup));
    ok &= RedirectApi("WSACleanup", reinterpret_cast<const void *>(Fake_WSACleanup));
    ok &= RedirectApi("socket", reinterpret_cast<const void *>(Fake_socket));
    ok &= RedirectApi("bind", reinterpret_cast<const void *>(Fake_bind));
    ok &= RedirectApi("closesocket", reinterpret_cast<const void *>(Fake_closesocket));
    ok &= RedirectApi("sendto", reinterpret_cast<const void *>(Fake_sendto));
    ok &= RedirectApi("recvfrom", reinterpret_cast<const void *>(Fake_recvfrom));
    ok &= RedirectApi("select", reinterpret_cast<const void *>(Fake_select));
    ok &= RedirectApi("gethostname", reinterpret_cast<const void *>(Fake_gethostname));
    ok &= RedirectApi("gethostbyname", reinterpret_cast<const void *>(Fake_gethostbyname));
    ok &= RedirectApi("CreateThread", reinterpret_cast<const void *>(Fake_CreateThread));
    ok &= RedirectApi("CreateEventA", reinterpret_cast<const void *>(Fake_CreateEventA));
    ok &= RedirectApi("SetEvent", reinterpret_cast<const void *>(Fake_SetEvent));
    ok &= RedirectApi("ResetEvent", reinterpret_cast<const void *>(Fake_ResetEvent));
    ok &= RedirectApi("WaitForSingleObject", reinterpret_cast<const void *>(Fake_WaitForSingleObject));
    ok &= RedirectApi("CloseHandle", reinterpret_cast<const void *>(Fake_CloseHandle));
    ok &= RedirectApi("SetThreadPriority", reinterpret_cast<const void *>(Fake_SetThreadPriority));
    return ok;
}
