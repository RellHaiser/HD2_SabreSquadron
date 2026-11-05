// The replicated objects (part 10b): see net_object.h.
#include "net/net_object.h"

#include <cmath>
#include <windows.h>

#include "net/bitstream.h"

C_NetReplication *g_netReplication;
int64_t g_netPerfFrequency;                                       // 0x00982df8

namespace {

// The objects' ids: 9 bits (the original works it out as floor(log2(511) / log2(2) + 1)); 0x1FF ends them.
constexpr int kIdBits = 9;
constexpr uint32_t kEndOfObjects = 0x1FF;

int64_t PerfCounter()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

}  // namespace

// ---- C_NetObjectMessage ------------------------------------------------------------------------------------------

C_NetObjectMessage::C_NetObjectMessage(int type) : type(type) {}

bool C_NetObjectMessage::MarkSent(int player)
{
    const int bit = 1 << player;
    if (sentTo & bit)
        return false;
    sentTo |= bit;
    return true;
}

// ---- C_NetProperty -----------------------------------------------------------------------------------------------

C_NetProperty::C_NetProperty(int index) : m_index(index), m_flags(0)
{
    // Field 0 (the components) 7, field 1 3.
    m_flags = 7;
    m_flags = (m_flags & ~(0x3Fu << 6)) | (3u << 6);
    for (int i = 0; i < 32; ++i) {
        m_lastPosition[i].x = m_lastPosition[i].y = m_lastPosition[i].z = 0.0f;
        m_lastDirection[i].x = m_lastDirection[i].y = m_lastDirection[i].z = 0.0f;
    }
}

void C_NetProperty::SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    m_read[0] = value;
    m_read[1] = delta;
    m_read[2] = rate;
    m_position.SetReadQuantizations(value, delta, rate);
    m_direction.SetReadQuantizations(value, delta, rate);
}

void C_NetProperty::SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    m_write[0] = value;
    m_write[1] = delta;
    m_write[2] = rate;
    m_position.SetWriteQuantizations(value, delta, rate);
    m_direction.SetWriteQuantizations(value, delta, rate);
}

void C_NetProperty::Update(float seconds)
{
    m_position.Update(seconds);
    m_direction.Update(seconds);
}

void C_NetProperty::Refresh()
{
    m_position.GetCurrent();
    m_direction.GetCurrent();
}

void C_NetProperty::Write(C_NetMessage *stream, int player, bool delta)
{
    m_position.Write(stream, delta, &m_lastPosition[player]);
    m_direction.Write(stream, delta, &m_lastDirection[player]);
}

void C_NetProperty::Read(C_NetMessage *stream)
{
    m_position.Read(stream);
    m_direction.Read(stream);
}

C_NetPositionTrack &C_NetProperty::GetTrack()
{
    return m_position;
}

C_NetDirTrack &C_NetProperty::GetDirTrack()
{
    return m_direction;
}

uint32_t C_NetProperty::GetFlags() const
{
    return m_flags;
}

void C_NetProperty::SetFlags(uint32_t flags)
{
    m_flags = flags;
    m_position.SetFlags(flags);
    m_direction.SetFlags(flags);
}

void C_NetProperty::SetField(int field, int value)
{
    const int shift = field * 6;
    m_flags = (m_flags & ~(0x3Fu << shift)) | (static_cast<uint32_t>(value) << shift);
    m_position.SetFlags(m_flags);
    m_direction.SetFlags(m_flags);
}

void C_NetProperty::SetLag(float lag)
{
    m_position.SetLag(lag);
    m_direction.SetLag(lag);
}

// ---- C_NetObject -------------------------------------------------------------------------------------------------

C_NetObject::C_NetObject()
{
    if (g_netPerfFrequency == 0) {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        g_netPerfFrequency = frequency.QuadPart;
    }
    m_propertyCount = 0;
    m_unknown34 = 2.0f;
    m_id = -1;
    m_sendMask = 0;
    m_live = 7;
    m_forward = true;
    m_writtenTo = 0;
    m_second = 0.0f;
    m_stateContext = nullptr;
    m_writeState = nullptr;
    m_readState = nullptr;
    m_factoryContext = nullptr;
    m_factory = nullptr;
    m_lastRead = PerfCounter();
    AddProperty(1);
}

C_NetObject::~C_NetObject()
{
    // The properties deleted, the last first; the queues' nodes freed (not their messages).
    while (!m_properties.empty()) {
        delete m_properties.back();
        m_properties.pop_back();
    }
}

void C_NetObject::SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    m_read[0] = value;
    m_read[1] = delta;
    m_read[2] = rate;
    for (C_NetProperty *property : m_properties)
        property->SetReadQuantizations(value, delta, rate);
}

void C_NetObject::SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    m_write[0] = value;
    m_write[1] = delta;
    m_write[2] = rate;
    for (C_NetProperty *property : m_properties)
        property->SetWriteQuantizations(value, delta, rate);
}

void C_NetObject::GetReadQuantizations(S_NetQuantization **value, S_NetQuantization **delta, S_NetQuantization **rate)
{
    *value = m_read[0];
    *delta = m_read[1];
    *rate = m_read[2];
}

void C_NetObject::GetWriteQuantizations(S_NetQuantization **value, S_NetQuantization **delta, S_NetQuantization **rate)
{
    *value = m_write[0];
    *delta = m_write[1];
    *rate = m_write[2];
}

void C_NetObject::Update(float seconds)
{
    Tick(seconds);
    for (C_NetProperty *property : m_properties) {
        // Nothing read for 0.4 s: the property's target stays where it was.
        const float silent = SecondsSinceRead();
        const uint32_t flags = property->GetFlags();
        const bool moving = (flags >> 17) & 1;
        if (silent > 0.4f && moving)
            property->SetFlags(flags & ~NET_TRACK_MOVE_TARGET);
        property->Update(seconds);
        if (silent > 0.4f && moving)
            property->SetFlags(flags | NET_TRACK_MOVE_TARGET);
    }
    for (C_NetObjectMessage *msg : m_received)
        msg->Update(seconds);
    for (C_NetObjectMessage *msg : m_outgoing)
        msg->Update(seconds);
}

void C_NetObject::Refresh()
{
    for (C_NetProperty *property : m_properties)
        property->Refresh();
}

bool C_NetObject::Write(C_NetMessage *stream, int player, bool fromClient)
{
    const uint32_t before = m_writtenTo;
    const uint32_t bit = 1u << player;
    m_writtenTo = before | bit;
    bool wrote = !fromClient;
    if (!(m_sendMask & NET_SEND_PROPERTIES) || fromClient) {
        stream->WriteBits(0, 1);
    } else {
        stream->WriteBits(1, 1);
        for (C_NetProperty *property : m_properties)
            property->Write(stream, player, (bit & before) != 0);
    }
    if (m_factory) {
        if (!(m_sendMask & NET_SEND_MESSAGES) || m_outgoing.empty()) {
            stream->WriteBits(0, 1);
        } else {
            stream->WriteBits(1, 1);
            for (auto it = m_outgoing.begin(); it != m_outgoing.end();) {
                C_NetObjectMessage *msg = *it;
                if (msg->fromClient == fromClient && msg->MarkSent(player) && m_factory) {
                    wrote = true;
                    msg->Write(stream);
                }
                if (!msg->SentToAll() && m_factory) {
                    ++it;
                } else {
                    // Sent to everyone: off the queue, deleted when no queue holds it.
                    --msg->refCount;
                    if (msg->refCount < 1)
                        delete msg;
                    it = m_outgoing.erase(it);
                }
            }
            m_factory(m_factoryContext, stream, true);
        }
    }
    if (m_writeState) {
        if (m_sendMask & NET_SEND_STATE) {
            stream->WriteBits(1, 1);
            m_writeState(m_stateContext, stream);
            return wrote;
        }
        stream->WriteBits(0, 1);
    }
    return wrote;
}

void C_NetObject::Read(C_NetMessage *stream, int sender, bool fromClient)
{
    m_lastRead = PerfCounter();
    if (stream->ReadBits(1)) {
        for (C_NetProperty *property : m_properties)
            property->Read(stream);
    }
    if (m_factory && stream->ReadBits(1)) {
        for (C_NetObjectMessage *msg = m_factory(m_factoryContext, stream, false); msg;
             msg = m_factory(m_factoryContext, stream, false)) {
            msg->Read(stream);
            msg->fromClient = fromClient;
            QueueMessage(msg, 0, false);
            // The server passes it on (as sent to the players it came through).
            if (m_sendMask != 0 && m_forward && g_netServerSide)
                QueueMessage(msg, sender, true);
        }
    }
    if (m_readState && stream->ReadBits(1))
        m_readState(m_stateContext, stream);
}

C_NetProperty *C_NetObject::AddProperty(int index)
{
    auto *property = new C_NetProperty(index);
    property->SetWriteQuantizations(m_write[0], m_write[1], m_write[2]);
    property->SetReadQuantizations(m_read[0], m_read[1], m_read[2]);
    m_properties.push_back(property);
    ++m_propertyCount;
    return property;
}

C_NetProperty *C_NetObject::GetProperty(int index)
{
    for (C_NetProperty *property : m_properties) {
        if (property->GetIndex() == index)
            return property;
    }
    return nullptr;
}

void C_NetObject::QueueMessage(C_NetObjectMessage *msg, int sentTo, bool outgoing)
{
    msg->sentTo = sentTo;
    ++msg->refCount;
    (outgoing ? m_outgoing : m_received).push_back(msg);
}

C_NetObjectMessage *C_NetObject::PopMessage(bool outgoing)
{
    std::list<C_NetObjectMessage *> &queue = outgoing ? m_outgoing : m_received;
    if (queue.empty())
        return nullptr;
    C_NetObjectMessage *msg = queue.front();
    --msg->refCount;
    queue.pop_front();
    return msg;
}

void C_NetObject::ClearMessages(bool release, bool outgoing, bool force)
{
    std::list<C_NetObjectMessage *> &queue = outgoing ? m_outgoing : m_received;
    for (auto it = queue.begin(); it != queue.end();) {
        C_NetObjectMessage *msg = *it;
        if (release)
            --msg->refCount;
        if (force || msg->refCount < 1) {
            delete msg;
            it = queue.erase(it);
        } else {
            // (Fix: the original does not move on here and loops for ever.)
            ++it;
        }
    }
}

void C_NetObject::Tick(float seconds)
{
    m_second = seconds + m_second;
    if (m_second >= 1.0f) {
        m_second = 0.0f;
        m_writtenTo = 0;
    }
}

void C_NetObject::SetPropertyField2(int value)
{
    for (C_NetProperty *property : m_properties)
        property->SetField(2, value);
}

void C_NetObject::SetLag(float lag)
{
    for (C_NetProperty *property : m_properties)
        property->SetLag(lag);
}

void C_NetObject::SetStateHandlers(NetStateHandler read, NetStateHandler write, void *context)
{
    m_readState = read;
    m_writeState = write;
    m_stateContext = context;
}

void C_NetObject::SetMessageFactory(NetMessageFactory factory, void *context)
{
    m_factory = factory;
    m_factoryContext = context;
}

void C_NetObject::SetSendMask(int mask)
{
    m_sendMask = mask;
}

void C_NetObject::SetLive(int value)
{
    m_live = value;
}

int C_NetObject::GetLive() const
{
    return m_live;
}

void C_NetObject::SetForwarding(bool value)
{
    m_forward = value;
}

bool C_NetObject::HasDataToSend() const
{
    return m_sendMask != 0 && (m_sendMask != NET_SEND_MESSAGES || !m_outgoing.empty());
}

float C_NetObject::SecondsSinceRead() const
{
    return static_cast<float>(static_cast<double>(PerfCounter() - m_lastRead) / static_cast<double>(g_netPerfFrequency));
}

// ---- C_NetViewCone -----------------------------------------------------------------------------------------------

bool C_NetViewCone::Contains(const S_vector4 &pos) const
{
    const S_vector4 dir = m_viewer->GetDirTrack().GetCurrent();
    const S_vector4 &from = m_viewer->GetTrack().GetCurrent();
    float x = pos.x - from.x;
    float y = pos.y - from.y;
    float z = pos.z - from.z;
    const float square = z * z + x * x + y * y;
    if (!(std::fabs(square - 1.0f) < 1e-8f)) {
        if (square >= 1e-8f) {
            const float k = 1.0f / std::sqrt(square);
            x *= k;
            y *= k;
            z *= k;
        } else if (x != 0.0f) {
            x = x >= 0.0f ? 1.0f : -1.0f;
        } else if (z == 0.0f) {
            y = y >= 0.0f ? 1.0f : -1.0f;
        } else {
            z = z >= 0.0f ? 1.0f : -1.0f;
        }
    }
    return dir.z * z + dir.x * x + dir.y * y > m_cosine;
}

// ---- C_NetReplication --------------------------------------------------------------------------------------------

C_NetReplication::C_NetReplication()
{
    Reset();
}

C_NetReplication::~C_NetReplication()
{
    DeleteObjects();
}

void C_NetReplication::Reset()
{
    m_useViewCone = false;
    m_objects.clear();
    m_readEpoch = 0;
    m_field2 = 0;
    m_bitsWritten = 0;
    m_bitsRead = 0;
    m_enabled = false;
    m_makeStateReader = nullptr;
    m_makeFactory = nullptr;
    m_writeEpoch = 1;
    m_lag = 0.08f;
    m_localId = -2;
}

void C_NetReplication::SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    if (value)
        m_read[0] = value;
    if (delta)
        m_read[1] = delta;
    if (rate)
        m_read[2] = rate;
    for (C_NetObject *object : m_objects)
        object->SetReadQuantizations(value, delta, rate);
}

void C_NetReplication::SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    if (value)
        m_write[0] = value;
    if (delta)
        m_write[1] = delta;
    if (rate)
        m_write[2] = rate;
    for (C_NetObject *object : m_objects)
        object->SetWriteQuantizations(value, delta, rate);
}

void C_NetReplication::Update(float seconds)
{
    for (C_NetObject *object : m_objects)
        object->Update(seconds);
    m_local.Update(seconds);
}

void C_NetReplication::Refresh()
{
    for (C_NetObject *object : m_objects)
        object->Refresh();
}

bool C_NetReplication::Write(C_NetMessage *stream, int player, bool fromClient)
{
    const uint32_t start = stream->BitPosition();
    bool wrote = false;
    stream->WriteBits(static_cast<uint32_t>(m_writeEpoch), 1);
    for (C_NetObject *object : m_objects) {
        if (m_useViewCone && !m_viewCone.Contains(object->GetProperty(0)->GetTrack().GetCurrent()))
            continue;
        if (!object->HasDataToSend())
            continue;
        stream->WriteBits(static_cast<uint32_t>(object->GetId()), kIdBits);
        wrote |= object->Write(stream, player, fromClient);
    }
    stream->WriteBits(kEndOfObjects, kIdBits);
    m_bitsWritten += static_cast<int>(stream->BitPosition() - start);
    return wrote;
}

void C_NetReplication::Read(C_NetMessage *stream, uint32_t forwardTo, bool fromClient)
{
    if (!m_enabled)
        return;
    const int asSent = static_cast<int>(~forwardTo);
    const uint32_t start = stream->BitPosition();
    const bool otherEpoch = stream->ReadBits(1) != static_cast<uint32_t>(m_readEpoch);
    for (uint32_t id = stream->ReadBits(kIdBits); id != kEndOfObjects; id = stream->ReadBits(kIdBits)) {
        C_NetObject *object = FindObject(static_cast<int>(id), true);
        if (object->m_live != 0 && !otherEpoch) {
            object->Read(stream, asSent, fromClient);
        } else if (static_cast<int>(id) == m_local.m_id) {
            // The local player's own object: read into it, the messages that came with it dropped.
            InitObject(&m_local);
            m_local.Read(stream, asSent, fromClient);
            m_local.ClearMessages(false, false, true);
        } else {
            // Not taken: read into a scratch object like it, then dropped.
            C_NetObject scratch;
            scratch.m_id = static_cast<int>(id);
            InitObject(&scratch);
            scratch.m_stateContext = object->m_stateContext;
            scratch.m_live = 0;
            S_NetQuantization *q[3];
            object->GetReadQuantizations(&q[0], &q[1], &q[2]);
            scratch.C_NetObject::SetReadQuantizations(q[0], q[1], q[2]);
            object->GetWriteQuantizations(&q[0], &q[1], &q[2]);
            scratch.C_NetObject::SetWriteQuantizations(q[0], q[1], q[2]);
            scratch.Read(stream, asSent, fromClient);
        }
    }
    m_bitsRead += static_cast<int>(stream->BitPosition() - start);
}

C_NetObject *C_NetReplication::FindObject(int id, bool create)
{
    for (C_NetObject *object : m_objects) {
        if (object->m_id == id)
            return object;
    }
    return create ? CreateObject(id) : nullptr;
}

C_NetObject *C_NetReplication::CreateObject(int id)
{
    auto *object = new C_NetObject;
    object->m_id = id;
    InitObject(object);
    m_objects.push_back(object);
    return object;
}

void C_NetReplication::InitObject(C_NetObject *object)
{
    object->SetLag(m_lag);
    object->SetReadQuantizations(m_read[0], m_read[1], m_read[2]);
    object->SetWriteQuantizations(m_write[0], m_write[1], m_write[2]);
    object->SetPropertyField2(m_field2);
    object->m_factory = m_makeFactory();
    object->m_factoryContext = nullptr;
    object->m_readState = m_makeStateReader();
    object->m_writeState = nullptr;
    object->m_stateContext = nullptr;
}

void C_NetReplication::RemoveObject(C_NetObject *object)
{
    for (size_t i = 0; i < m_objects.size(); ++i) {
        if (m_objects[i] == object) {
            m_objects.erase(m_objects.begin() + static_cast<ptrdiff_t>(i));
            delete object;
        }
    }
}

void C_NetReplication::DeleteObjects()
{
    while (!m_objects.empty()) {
        delete m_objects.back();
        m_objects.pop_back();
    }
}

int C_NetReplication::GetLocalId() const
{
    return m_localId;
}

void C_NetReplication::SetWriteEpoch(int value)
{
    m_writeEpoch = value;
}

void C_NetReplication::SetReadEpoch(int value)
{
    m_readEpoch = value;
}

void C_NetReplication::SetLag(float lag)
{
    m_lag = lag;
    for (C_NetObject *object : m_objects)
        object->SetLag(lag);
}
