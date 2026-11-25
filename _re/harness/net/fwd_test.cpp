// Branch net-forwarded-messages: a server's forwarded object messages cleared by force (C_NetObject::ClearMessages).
// The server reads a message into its object's received queue and queues it again to send on (the same message in
// both queues). The original, and main, delete it in the first forced clear and leave the other queue's pointer
// dangling; ResetReplication clears both by force and deletes it twice. On this branch each message is deleted once,
// when no queue holds it. The test's messages are never freed (their operator delete only counts), so a second delete
// is counted, not a heap fault. MinGW; build_fwd.bat; runs fwd_test.exe ("fwd: N checks, 0 failures").
#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#define protected public
#define private public
#include "net/bitstream.h"
#include "net/net_object.h"
#undef private
#undef protected

extern bool g_netServerSide;
S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

extern "C" BOOL WINAPI FakeQueryPerformanceCounter(LARGE_INTEGER *now)
{
    now->QuadPart = 1000;
    return TRUE;
}
extern "C" BOOL WINAPI FakeQueryPerformanceFrequency(LARGE_INTEGER *frequency)
{
    frequency->QuadPart = 1000;
    return TRUE;
}

namespace {

int checks, failures;
void Check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

// How many times each message was deleted.
std::map<const void *, int> g_deletes;

class TMsg : public C_NetObjectMessage {
public:
    TMsg() : C_NetObjectMessage(1) { g_deletes[this] = 0; }
    ~TMsg() override { ++g_deletes[this]; }
    void Write(C_BitStream *) override {}
    void Read(C_BitStream *) override {}
    // Kept allocated: a second delete is counted, not a fault.
    static void operator delete(void *) {}
};

int Deleted(const void *m) { return g_deletes.count(m) ? g_deletes[m] : -1; }

// The test's factory: a 1 bit for each message, a 0 at the end.
C_NetObjectMessage *__fastcall Factory(void *, C_BitStream *stream, bool write)
{
    if (write) {
        stream->WriteBits(0, 1);
        return nullptr;
    }
    return stream->ReadBits(1) ? new TMsg : nullptr;
}

// A server's object that read `count` messages from a client and forwarded them (each in both queues).
void ReadForwarded(C_NetObject &object, int count, int sender)
{
    object.SetMessageFactory(&Factory, nullptr);
    object.SetSendMask(NET_SEND_MESSAGES);
    object.SetForwarding(true);
    g_netServerSide = true;
    C_NetMessage stream;
    stream.WriteBits(0, 1);   // no properties
    stream.WriteBits(1, 1);   // messages
    for (int i = 0; i < count; ++i)
        stream.WriteBits(1, 1);
    stream.WriteBits(0, 1);
    stream.SetBitPosition(0);
    object.Read(&stream, sender, true);
}

std::string N(long long v) { return std::to_string(v); }

}   // namespace

int main()
{
    // ResetReplication's two forced, releasing clears (received, then to send; and the other order).
    for (int order = 0; order < 2; ++order) {
        C_NetObject object;
        ReadForwarded(object, 3, 0);
        const std::string tag = "reset order " + N(order) + ": ";
        Check(object.m_received.size() == 3 && object.m_outgoing.size() == 3, tag + "queued in both");
        TMsg *first = static_cast<TMsg *>(object.m_received.front());
        Check(first == object.m_outgoing.front() && first->refCount == 2, tag + "the same message, two references");
        object.ClearMessages(true, order != 0, true);
        Check(Deleted(first) == 0, tag + "kept while the other queue holds it");
        Check(first->refCount == 1, tag + "one reference left");
        Check((order != 0 ? object.m_outgoing : object.m_received).empty(), tag + "the first queue emptied");
        object.ClearMessages(true, order == 0, true);
        Check(object.m_received.empty() && object.m_outgoing.empty(), tag + "both emptied");
        for (auto &[m, n] : g_deletes)
            Check(n == 1, tag + "deleted once (" + N(n) + ")");
        g_deletes.clear();
    }

    // The local player's object (C_NetReplication::Read): its received messages dropped by force without releasing;
    // a forwarded one stays queued to send and goes when sent to everyone.
    {
        C_NetObject object;
        ReadForwarded(object, 2, -1);
        TMsg *first = static_cast<TMsg *>(object.m_received.front());
        object.ClearMessages(false, false, true);
        Check(object.m_received.empty() && object.m_outgoing.size() == 2, "local: received dropped, to send kept");
        Check(Deleted(first) == 0 && first->refCount == 1, "local: kept for the send queue");
        C_NetMessage out;
        object.Write(&out, 0, true);
        Check(object.m_outgoing.empty(), "local: sent to everyone, off the queue");
        for (auto &[m, n] : g_deletes)
            Check(n == 1, "local: deleted once (" + N(n) + ")");
        g_deletes.clear();
    }

    // A message in one queue only: deleted by a forced clear, released or not (as the original).
    for (int release = 0; release < 2; ++release) {
        for (int outgoing = 0; outgoing < 2; ++outgoing) {
            C_NetObject object;
            TMsg *a = new TMsg, *b = new TMsg;
            object.QueueMessage(a, 0, outgoing != 0);
            object.QueueMessage(b, 0, outgoing != 0);
            object.ClearMessages(release != 0, outgoing != 0, true);
            const std::string tag = "single " + N(release) + N(outgoing) + ": ";
            Check((outgoing ? object.m_outgoing : object.m_received).empty(), tag + "emptied");
            Check(Deleted(a) == 1 && Deleted(b) == 1, tag + "deleted once");
            g_deletes.clear();
        }
    }

    // Not forced, not released: unchanged (a message still held is kept). (Not forced and releasing, the original
    // releases a held message again until it goes, as on main; no caller in the game clears without force.)
    {
        C_NetObject object;
        ReadForwarded(object, 2, 0);
        TMsg *first = static_cast<TMsg *>(object.m_received.front());
        object.ClearMessages(false, false, false);
        Check(object.m_received.size() == 2 && Deleted(first) == 0, "unforced, not released: kept");
        object.ClearMessages(true, false, true);
        object.ClearMessages(true, true, true);
        g_deletes.clear();
    }

    std::printf("fwd: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
