// The replicated objects (part 10b): the properties (a position and a direction track), the objects (their
// properties, message queues and state handlers) and the replication (the objects and the local one, written to
// and read from each player's stream). Original file 0x0073dd30..0x0073fbe0, 0x00741510..0x00741700.
#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include "net/net_track.h"

class C_BitStream;
class C_NetMessage;

// A message of a replicated object (vtable 0x00810630, 0x30 bytes): queued on the object for the other machines
// (sent to each player once; deleted when sent to all and no queue holds it) or received.
class C_NetObjectMessage : public C_NetQuantized {
public:
    explicit C_NetObjectMessage(int type);                        // 0x0073fb40
    // Its age (seconds).
    void Update(float seconds) override { age += seconds; }       // 0x004c13f0
    void Refresh() override {}                                    // 0x004c1400
    ~C_NetObjectMessage() override = default;                     // 0x004c1450 (scalar deleting)
    // Marks it sent to `player`: false when it already was.
    virtual bool MarkSent(int player);                            // 0x004c13c0 (slot 5)
    virtual bool SentToAll() const { return sentTo == -1; }       // 0x0073fb80
    virtual bool Expired() const { return age > 1.0f; }           // 0x0073fb90
    // The message's data after its type (pure in the base).
    virtual void Write(C_BitStream *stream) = 0;                  // slot 8
    virtual void Read(C_BitStream *stream) = 0;

    int GetType() const { return type; }

    int type;                                                     // +0x1C
    int sentTo = 0;                                               // +0x20 the players it went to (bits; QueueMessage's)
    int refCount = 0;                                             // +0x24 the queues holding it
    float age = 0.0f;                                             // +0x28
    bool fromClient = false;                                      // +0x2C
};

// A replicated place (vtable 0x0081641c, 0x580 bytes, 16-aligned): a position and a direction track sharing the
// flags (the tracks' NET_TRACK_* in 6-bit fields: 0 the components, 7; 1 3; 2 the behaviour), the last values
// written to each of 32 players.
class alignas(16) C_NetProperty : public C_NetQuantized {
public:
    explicit C_NetProperty(int index);                            // 0x0073de20
    ~C_NetProperty() override = default;                          // 0x0073df00

    // All three stored (null too) and handed to both tracks.
    void SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta,
                              S_NetQuantization *rate) override;  // 0x0073e030
    void SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta,
                               S_NetQuantization *rate) override; // 0x0073e070
    void Update(float seconds) override;                          // 0x0073dfe0
    // (The tracks' GetCurrent called, the results dropped.)
    void Refresh() override;                                      // 0x0073e010
    // Both tracks for `player` (`delta`: against what it was last sent).
    virtual void Write(C_NetMessage *stream, int player, bool delta);   // 0x0073df60 (slot 5)
    virtual void Read(C_NetMessage *stream);                      // 0x0073dfb0

    // (These accessors and the object's / replication's below are out of line: the harnesses fake them.)
    C_NetPositionTrack &GetTrack();
    C_NetDirTrack &GetDirTrack();
    int GetIndex() const { return m_index; }
    uint32_t GetFlags() const;
    // The flags, handed to both tracks; one 6-bit field of them.
    void SetFlags(uint32_t flags);                                // 0x0073e0e0
    void SetField(int field, int value);                          // 0x0073e110
    void SetLag(float lag);                                       // 0x0073e0b0

private:
    int m_index;                                                  // +0x1C
    uint32_t m_flags;                                             // +0x20
    C_NetPositionTrack m_position;                                // +0x30
    C_NetDirTrack m_direction;                                    // +0xC0
    S_vector4 m_lastPosition[32];                                 // +0x180
    S_vector4 m_lastDirection[32];                                // +0x380
};

// The owner's state handlers of a net object (the controllers').
using NetStateHandler = void (__fastcall *)(void *context, C_BitStream *stream);
// Makes the object's message whose type `stream` holds (`write`: writes the end of the messages instead; returns
// null; null too at the end when reading).
using NetMessageFactory = C_NetObjectMessage *(__fastcall *)(void *context, C_BitStream *stream, bool write);

// What a net object sends (C_NetObject::SetSendMask): the properties, the messages, the owner's state.
enum : int {
    NET_SEND_PROPERTIES = 1,
    NET_SEND_MESSAGES = 2,
    NET_SEND_STATE = 4,
};

// A replicated object (vtable 0x00816448, 0x80 bytes, 16-aligned): its properties (property 1 made with it), the
// messages received and those to send, the owner's state handlers.
class alignas(16) C_NetObject : public C_NetQuantized {
public:
    C_NetObject();                                                // 0x0073e190
    ~C_NetObject() override;                                      // 0x0073e340

    // All three stored (null too) and handed to every property.
    void SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta,
                              S_NetQuantization *rate) override;  // 0x0073ecf0
    void SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta,
                               S_NetQuantization *rate) override; // 0x0073eca0
    // Tick, the properties (no data read for 0.4 s: without moving their targets), the messages' ages.
    void Update(float seconds) override;                          // 0x0073eb20
    void Refresh() override;                                      // 0x0073ec30
    virtual void GetReadQuantizations(S_NetQuantization **value, S_NetQuantization **delta,
                                      S_NetQuantization **rate);  // 0x0073ec60 (slot 5)
    virtual void GetWriteQuantizations(S_NetQuantization **value, S_NetQuantization **delta,
                                       S_NetQuantization **rate); // 0x0073ec80
    // For `player`: the properties (deltas when already written to it this second), the messages queued from this
    // side (`fromClient`) not yet sent to it, the owner's state; true when a message went (or a server's data).
    virtual bool Write(C_NetMessage *stream, int player, bool fromClient);   // 0x0073e6d0 (slot 7)
    // The properties, the messages (queued as received; the server forwards them, marked as sent to `sender`'s
    // bits), the owner's state.
    virtual void Read(C_NetMessage *stream, int sender, bool fromClient);    // 0x0073e890

    // A property of `index`, made with the quantisations (write first).
    C_NetProperty *AddProperty(int index);                        // 0x0073e460
    C_NetProperty *GetProperty(int index);                        // 0x0073e690
    // Queues `msg` (outgoing: to send, else received), its sent mask set to `sentTo`.
    void QueueMessage(C_NetObjectMessage *msg, int sentTo, bool outgoing);   // 0x0073e970
    // The first message of a queue (null: none), taken off it.
    C_NetObjectMessage *PopMessage(bool outgoing);                // 0x0073e9d0
    // A queue's messages released (`release`: one reference dropped first) and deleted when no queue holds them
    // (`force`: all).
    void ClearMessages(bool release, bool outgoing, bool force);  // 0x0073ea30
    // Counts `seconds` up to a second, then forgets which players were written to.
    void Tick(float seconds);                                     // 0x0073eaf0
    void SetPropertyField2(int value);                            // 0x0073ed40 (every property's field 2)
    void SetLag(float lag);                                       // 0x0073ed80 (every property's)
    // Whether there is anything to send (a mask; only messages: when some are queued).
    bool HasDataToSend() const;                                   // 0x0073edc0 (__fastcall)
    // Seconds since data was last read.
    float SecondsSinceRead() const;                               // 0x0073ede0 (__fastcall)

    int GetId() const { return m_id; }
    void SetId(int id) { m_id = id; }
    void SetStateHandlers(NetStateHandler read, NetStateHandler write, void *context);
    void SetMessageFactory(NetMessageFactory factory, void *context);
    void SetSendMask(int mask);
    // Whether the data read is taken (7 by default; else the replication reads it into a scratch object).
    void SetLive(int value);
    int GetLive() const;
    // Whether the server forwards the messages read (true by default).
    void SetForwarding(bool value);

private:
    friend class C_NetReplication;
    int m_id;                                                     // +0x1C
    std::vector<C_NetProperty *> m_properties;                    // +0x24
    int m_propertyCount;                                          // +0x30
    float m_unknown34;                                            // +0x34 (2.0; not read here)
    int m_sendMask;                                               // +0x38
    int m_live;                                                   // +0x3C
    bool m_forward;                                               // +0x40
    float m_second;                                               // +0x44
    int64_t m_lastRead;                                           // +0x48 (the performance counter)
    uint32_t m_writtenTo;                                         // +0x50 the players written to this second
    NetStateHandler m_readState;                                  // +0x54
    NetStateHandler m_writeState;                                 // +0x58
    void *m_stateContext;                                         // +0x5C
    void *m_factoryContext;                                       // +0x60
    NetMessageFactory m_factory;                                  // +0x64
    std::list<C_NetObjectMessage *> m_received;                   // +0x68
    std::list<C_NetObjectMessage *> m_outgoing;                   // +0x74
};

// The view cone the replication may filter the objects by (vtable 0x00816594, 0xC bytes): whether a position lies
// within the angle (its cosine) of the local player's view, from the player's place.
class C_NetViewCone {
public:
    C_NetViewCone() = default;                                    // 0x00741510
    virtual ~C_NetViewCone() = default;                           // 0x00741550
    bool Contains(const S_vector4 &pos) const;                    // 0x00741560

    C_NetProperty *m_viewer = nullptr;                            // +0x04 the local player's property 0
    float m_cosine = 0.737f;                                      // +0x08
};

// The replicated objects (vtable 0x0081646c, 0xE8 bytes, 16-aligned; g_netReplication): written to each player's
// stream as the objects' ids (9 bits; 0x1FF ends) and data, read back into the objects of those ids (made when
// new), the local one (the machine's player) apart.
class alignas(16) C_NetReplication : public C_NetQuantized {
public:
    C_NetReplication();                                           // 0x0073f020
    ~C_NetReplication() override;                                 // 0x0073f0d0

    // The non-null ones stored; every object's set (null too).
    void SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta,
                              S_NetQuantization *rate) override;  // 0x0073f620
    void SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta,
                               S_NetQuantization *rate) override; // 0x0073f5c0
    // The objects, then the local one.
    void Update(float seconds) override;                          // 0x0073f550
    void Refresh() override;                                      // 0x0073f590
    // The write epoch, then each object with data to send (in the view cone when filtering) for `player`; true
    // when any object's Write was.
    virtual bool Write(C_NetMessage *stream, int player, bool fromClient);   // 0x0073f1e0 (slot 5)
    // (Enabled only.) The objects read: a live one of the expected epoch reads its data; the local one's into it,
    // its messages dropped; the others' into a scratch object and dropped. `players`: those it came through.
    virtual void Read(C_NetMessage *stream, uint32_t players, bool fromClient);   // 0x0073f320

    // The defaults: no filter, no objects (not deleted), the epochs, counters and handlers 0, the lag 0.08, the
    // local id -2.
    void Reset();                                                 // 0x0073f150 (__fastcall)
    // The object of `id` (none: made when `create`, else null).
    C_NetObject *FindObject(int id, bool create);                 // 0x0073f9c0
    C_NetObject *CreateObject(int id);                            // 0x0073f680
    // An object given the replication's settings: the lag, the quantisations, field 2, its handlers from the
    // replication's factories.
    void InitObject(C_NetObject *object);                         // 0x0073f880
    void RemoveObject(C_NetObject *object);                       // 0x0073f900 (deleted)
    void DeleteObjects();                                         // 0x0073f970 (the last first)
    void SetLag(float lag);                                       // 0x0073fa30 (every object's too)

    C_NetObject &GetLocalObject() { return m_local; }
    bool HasObjects() const { return !m_objects.empty(); }
    int GetLocalId() const;
    void SetWriteEpoch(int value);
    void SetReadEpoch(int value);

private:
    std::vector<C_NetObject *> m_objects;                         // +0x20
    C_NetObject m_local;                                          // +0x30
    C_NetViewCone m_viewCone;                                     // +0xB0
    bool m_useViewCone;                                           // +0xBC
    int m_field2;                                                 // +0xC0 every object's properties' field 2
    int m_writeEpoch;                                             // +0xC4
    int m_readEpoch;                                              // +0xC8
    int m_bitsRead;                                               // +0xCC
    int m_bitsWritten;                                            // +0xD0
    bool m_enabled;                                               // +0xD4
    float m_lag;                                                  // +0xD8
    int m_localId;                                                // +0xDC
    NetStateHandler (*m_makeStateReader)();                       // +0xE0 the objects' state reader (InitObject)
    NetMessageFactory (*m_makeFactory)();                         // +0xE4 the objects' message factory
};
extern C_NetReplication *g_netReplication;                        // 0x0097d084
// The performance counter's frequency (the first net object takes it).
extern int64_t g_netPerfFrequency;                                // 0x00982df8
