// Bit-level serialisation used by the network messages (original file 0x0073d260).
#pragma once

#include <cstdint>

class C_BitStream {                                          // vtable 0x0081640c
public:
    C_BitStream() = default;                                 // 0x0073d300
    C_BitStream(uint32_t *buffer, uint32_t capacityBits)     // 0x0073d290
        : m_data(buffer), m_capacityBits(capacityBits) {}
    // Makes room for `bits` more bits; the fixed-buffer stream cannot grow and only
    // reports whether it is still within its buffer (`bits` unused). Slot 0 of the vtable,
    // the destructor is slot 1 (WriteBits ignores the result).
    virtual bool Reserve(uint32_t /*bits*/) { return IsValid(); }   // 0x0073d2c0
    virtual ~C_BitStream() = default;                        // 0x0073d2e0

    void WriteBits(uint32_t value, int bitCount);            // 0x0073d320
    uint32_t ReadBits(int bitCount);                         // 0x0073d3d0
    void WriteString(const char *text);                      // 0x0073d440
    void ReadString(char *buffer, int bufferSize);           // 0x0073d470

    // False once more bits were consumed/produced than the buffer holds.
    bool IsValid() const { return BitPosition() <= m_capacityBits; }   // (the base Reserve)
    uint32_t BitPosition() const { return m_wordIndex * 32 + m_bitIndex; }
    const uint32_t *Data() const { return m_data; }

protected:
    uint32_t *m_data = nullptr;                              // +0x04
    uint32_t m_wordIndex = 0;                                // +0x08
    uint32_t m_bitIndex = 0;                                 // +0x0C
    uint32_t m_capacityBits = 0;                             // +0x10
};

// Bit stream that owns a heap buffer and grows it on demand.
class C_GrowingBitStream : public C_BitStream {             // vtable 0x00816414
public:
    C_GrowingBitStream() = default;                          // 0x0073d650
    ~C_GrowingBitStream() override;                          // 0x0073d690
    bool Reserve(uint32_t bits) override;                    // 0x0073d6c0

protected:
    bool m_ownsBuffer = false;                               // +0x14
};

struct S_NetPlayer;                                          // 0x80-byte entries of the player table
struct S_vector4;

// The quantisation of up to three floats (0x80 bytes; the player table's entries start with one,
// C_NetMessage::WriteQuantized's `player`, and the car keeps two for its net object): component i
// is sent in bits[i] bits as (value - offset[i]) x scale[i].
struct alignas(16) S_NetQuantization {
    // Component i from `from` over `span` in steps of `step`: bits[i] = floor(log2(span / step)) +
    // 1, scale[i] = that many bits' mask / span. (Fastcall: this, span; RET 0xC.)
    void Setup(const S_vector4 &span, const S_vector4 &from, const S_vector4 &step, int unknown);   // 0x0073dbb0
    // (Part 10a.) Whether the three components of `v` lie in the ranges [offset, offset + range40]
    // (__thiscall); whether each is smaller in magnitude than its step (all three: a change too
    // small to send).
    bool Contains(const float *v) const;                     // 0x0073dd30
    bool WithinSteps(const float *v) const;                  // 0x0073dda0

    float deadZone[4];                                       // +0x00 the step: smaller magnitudes are sent / read back as 0
    float min[4];                                            // +0x10 (y: the lowest y sent)
    float range[4];                                          // +0x20
    float offset[4];                                         // +0x30 the range's start (min again)
    float range40[4];                                        // +0x40 (range again; not read here)
    int bits[3];                                             // +0x50
    int unknown5C;
    float scale[3];                                          // +0x60 steps per unit
    float unknown6C;
    int unknown70;                                           // +0x70 Setup's `unknown` (the car's: 100)
};
static_assert(sizeof(S_NetQuantization) == 0x80, "S_NetQuantization layout");

// A network message: bit stream plus sender/recipient player references.
class C_NetMessage : public C_GrowingBitStream {            // vtable 0x00816378
public:
    C_NetMessage();                                          // 0x0073d740

    // Player by table index, else the stored player, else the stored index.
    S_NetPlayer *GetSender(int playerIndex) const;           // 0x0073d770
    S_NetPlayer *GetRecipient(int playerIndex) const;        // 0x0073d7c0

    // Horizontal direction sent as its heading angle (y is dropped; read back as
    // (cos, 0, sin)).
    void WriteDirection(const struct S_vector &dir, int playerIndex);   // 0x0073db30
    void ReadDirection(struct S_vector &dir, int playerIndex);          // 0x0073db70
    // Up to three floats, each quantised to the ranges and bit counts the player
    // entry holds (bit i of `mask` selects component i).
    // Returns whether any of the three components (whatever the mask) exceeds its step. The
    // `player` entries start with their quantisation; the net tracks pass one directly.
    bool WriteQuantized(const float *values, const S_NetQuantization *q, uint8_t mask);  // 0x0073d810
    void ReadQuantized(float *values, const S_NetQuantization *q, uint32_t mask);        // 0x0073d900
    bool WriteQuantized(const float *values, const S_NetPlayer *player, uint8_t mask)
    {
        return WriteQuantized(values, reinterpret_cast<const S_NetQuantization *>(player), mask);
    }
    void ReadQuantized(float *values, const S_NetPlayer *player, uint32_t mask)
    {
        ReadQuantized(values, reinterpret_cast<const S_NetQuantization *>(player), mask);
    }
    // A position quantised with the sender's settings (mask 7).
    void WritePosition(const struct S_vector &pos);
    void ReadPosition(struct S_vector &pos);
    // Unit direction sent as heading and pitch.
    void WriteDirection3D(const struct S_vector &dir, int playerIndex);   // 0x0073da90
    void ReadDirection3D(struct S_vector &dir, int playerIndex);          // 0x0073dae0
    // A rotation as its unit axis (heading and pitch, as WriteDirection3D) and its angle.
    void WriteAxisAngle(const struct S_vector &axis, float angle, int playerIndex);   // 0x0073d9e0
    void ReadAxisAngle(struct S_vector &axis, float &angle, int playerIndex);         // 0x0073da30

private:
    uint8_t m_unknown18[0x14];
    S_NetPlayer *m_sender = nullptr;                         // +0x2C
    S_NetPlayer *m_recipient = nullptr;                      // +0x30
    int m_senderIndex = 0;                                   // +0x34
    int m_recipientIndex = 1;                                // +0x38
};

// The global player table (0x80 bytes per entry).
extern S_NetPlayer *g_netPlayerTable;                        // 0x00982dec
extern int g_netPlayerCount;                                 // 0x00982df0
S_NetPlayer *GetNetPlayer(int index);
