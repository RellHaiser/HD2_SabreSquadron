#include "net/bitstream.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>

#include "ls3d/math.h"

namespace {

// Low-bit masks: kBitMask[n] has the n lowest bits set.  0x00816388
constexpr uint32_t BitMask(int n)
{
    return n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
}

} // namespace

void C_BitStream::WriteBits(uint32_t value, int bitCount)
{
    Reserve(bitCount);

    // `value` is not masked (as in the original): callers pass in-range values.
    uint32_t &word = m_data[m_wordIndex];
    word = (word & ~(BitMask(bitCount) << m_bitIndex)) | (value << m_bitIndex);

    const uint32_t end = m_bitIndex + bitCount;
    if (end < 32) {
        m_bitIndex = end;
        return;
    }
    ++m_wordIndex;
    m_bitIndex = end & 31;
    if (m_bitIndex != 0) {
        // The bits that did not fit go to the low end of the next word.
        const int spilled = bitCount - static_cast<int>(m_bitIndex);
        uint32_t &next = m_data[m_wordIndex];
        next = (next & ~(BitMask(bitCount) >> spilled)) | (value >> spilled);
    }
}

uint32_t C_BitStream::ReadBits(int bitCount)
{
    uint32_t result = (m_data[m_wordIndex] >> m_bitIndex) & BitMask(bitCount);

    const uint32_t end = m_bitIndex + bitCount;
    if (end < 32) {
        m_bitIndex = end;
        return result;
    }
    ++m_wordIndex;
    m_bitIndex = end & 31;
    if (m_bitIndex != 0) {
        const int alreadyRead = bitCount - static_cast<int>(m_bitIndex);
        result |= (m_data[m_wordIndex] << alreadyRead) & BitMask(bitCount);
    }
    return result;
}

void C_BitStream::WriteString(const char *text)
{
    // Characters including the terminating zero, 8 bits each.
    char ch;
    do {
        ch = *text++;
        WriteBits(static_cast<uint8_t>(ch), 8);
    } while (ch != '\0');
}

void C_BitStream::ReadString(char *buffer, int bufferSize)
{
    for (int i = 0; i < bufferSize; ++i) {
        buffer[i] = static_cast<char>(ReadBits(8));
        if (buffer[i] == '\0')
            break;
    }
    buffer[bufferSize - 1] = '\0';
}

C_GrowingBitStream::~C_GrowingBitStream()
{
    if (m_data && m_ownsBuffer)
        std::free(m_data);
}

bool C_GrowingBitStream::Reserve(uint32_t bits)
{
    if (BitPosition() + bits < m_capacityBits)
        return true;

    if (!m_data) {
        m_capacityBits = bits + 32 > 800 ? bits + 32 : 800;
        // The original always allocates 100 bytes here even when the requested
        // capacity is larger; the buffer is sized to the capacity instead.
        m_data = static_cast<uint32_t *>(std::malloc((m_capacityBits + 31) / 32 * 4));
        m_ownsBuffer = true;
        return true;
    }

    // Grow by the current capacity (doubling), or by the request when larger.
    const uint32_t growth = m_capacityBits < bits ? bits + 32 : m_capacityBits;
    m_capacityBits += growth;
    m_data = static_cast<uint32_t *>(std::realloc(m_data, m_capacityBits >> 3));
    return true;
}

C_NetMessage::C_NetMessage() = default;

S_NetPlayer *GetNetPlayer(int index)
{
    if (index < 0 || index >= g_netPlayerCount)
        return nullptr;
    return reinterpret_cast<S_NetPlayer *>(reinterpret_cast<uint8_t *>(g_netPlayerTable) + index * 0x80);
}

S_NetPlayer *C_NetMessage::GetSender(int playerIndex) const
{
    if (S_NetPlayer *player = GetNetPlayer(playerIndex))
        return player;
    if (m_sender)
        return m_sender;
    return GetNetPlayer(m_senderIndex);
}

S_NetPlayer *C_NetMessage::GetRecipient(int playerIndex) const
{
    if (S_NetPlayer *player = GetNetPlayer(playerIndex))
        return player;
    if (m_recipient)
        return m_recipient;
    return GetNetPlayer(m_recipientIndex);
}

// ---- quantised floats (0x0073d810..0x0073dbb0) -----------------------------------

namespace {

static_assert(offsetof(S_NetQuantization, bits) == 0x50, "S_NetQuantization layout");
static_assert(offsetof(S_NetQuantization, scale) == 0x60, "S_NetQuantization layout");

// The player entries start with their quantisation.
const S_NetQuantization &Quantization(const S_NetPlayer *player)
{
    return *reinterpret_cast<const S_NetQuantization *>(player);
}

uint32_t Quantize(const S_NetQuantization &q, int i, float value)
{
    return static_cast<uint32_t>(static_cast<int>((value - q.offset[i]) * q.scale[i]));
}

float Dequantize(const S_NetQuantization &q, int i, uint32_t raw)
{
    return static_cast<float>(static_cast<int>(raw)) / q.scale[i] + q.offset[i];
}

} // namespace

void C_NetMessage::WriteQuantized(const float *values, const S_NetPlayer *player, uint8_t mask)
{
    const S_NetQuantization &q = Quantization(player);
    if (mask & 1)
        WriteBits(Quantize(q, 0, values[0]), q.bits[0]);
    if (mask & 2) {
        // y carries a presence bit: values in its dead zone are not sent
        const float y = values[1] >= q.min[1] ? values[1] : q.min[1];
        if (std::fabs(y) <= q.deadZone[1]) {
            WriteBits(0, 1);
        } else {
            WriteBits(1, 1);
            WriteBits(Quantize(q, 1, y), q.bits[1]);
        }
    }
    if (mask & 4)
        WriteBits(Quantize(q, 2, values[2]), q.bits[2]);
    // (The original also returns whether any component lies outside its dead
    // zone; no caller uses it.)
}

void C_NetMessage::ReadQuantized(float *values, const S_NetPlayer *player, uint32_t mask)
{
    const S_NetQuantization &q = Quantization(player);
    values[0] = (mask & 1) ? Dequantize(q, 0, ReadBits(q.bits[0])) : 0.0f;
    values[1] = (mask & 2) && ReadBits(1) ? Dequantize(q, 1, ReadBits(q.bits[1])) : 0.0f;
    values[2] = (mask & 4) ? Dequantize(q, 2, ReadBits(q.bits[2])) : 0.0f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(values[i]) < q.deadZone[i])
            values[i] = 0.0f;
    }
}

void S_NetQuantization::Setup(const S_vector4 &span, const S_vector4 &from, const S_vector4 &step,
                              int unknown)              // 0x0073dbb0 (__fastcall: this, span)
{
    const float froms[4] = {from.x, from.y, from.z, from.w};
    const float spans[4] = {span.x, span.y, span.z, span.w};
    const float steps[4] = {step.x, step.y, step.z, step.w};
    for (int i = 0; i < 4; ++i) {
        offset[i] = froms[i];
        min[i] = froms[i];
        range40[i] = spans[i];
        range[i] = spans[i];
        deadZone[i] = steps[i];
    }
    unknown70 = unknown;
    // (The original works the logarithms out with FYL2X, in the game's float precision: as here, an
    // exact power of two can come out just below its exponent, e.g. 8192 gives 13 bits, not 14;
    // _re/harness/net_quantization checks it.)
    const float ln2 = std::log(2.0f);
    for (int i = 0; i < 3; ++i)
        bits[i] = static_cast<int>(std::floor(std::log(spans[i] / steps[i]) / ln2) + 1.0f);
    for (int i = 0; i < 3; ++i)
        scale[i] = static_cast<float>(static_cast<double>(bits[i] > 0 ? BitMask(bits[i]) : 0u) / spans[i]);
}

void C_NetMessage::WritePosition(const S_vector &pos)
{
    WriteQuantized(&pos.x, GetSender(-1), 7);
}

void C_NetMessage::ReadPosition(S_vector &pos)
{
    float v[3];
    ReadQuantized(v, GetSender(-1), 7);
    pos = S_vector(v[0], v[1], v[2]);
}

void C_NetMessage::WriteDirection(const S_vector &dir, int playerIndex)
{
    const float heading[1] = {std::atan2(dir.z, dir.x)};
    WriteQuantized(heading, GetRecipient(playerIndex), 1);
}

void C_NetMessage::ReadDirection(S_vector &dir, int playerIndex)
{
    float v[3];
    ReadQuantized(v, GetRecipient(playerIndex), 1);
    dir = S_vector(std::cos(v[0]), 0.0f, std::sin(v[0]));
}

void C_NetMessage::WriteDirection3D(const S_vector &dir, int playerIndex)
{
    const float angles[2] = {std::atan2(dir.z, dir.x), std::asin(dir.y)};   // heading, pitch
    WriteQuantized(angles, GetRecipient(playerIndex), 3);
}

void C_NetMessage::ReadDirection3D(S_vector &dir, int playerIndex)
{
    float v[3];
    ReadQuantized(v, GetRecipient(playerIndex), 3);
    const float cosPitch = std::cos(v[1]);
    dir = S_vector(std::cos(v[0]) * cosPitch, std::sin(v[1]), std::sin(v[0]) * cosPitch);
}

void C_NetMessage::WriteAxisAngle(const S_vector &axis, float angle, int playerIndex)
{
    const float v[3] = {std::atan2(axis.z, axis.x), std::asin(axis.y), angle};   // heading, pitch, angle
    WriteQuantized(v, GetRecipient(playerIndex), 7);
}

void C_NetMessage::ReadAxisAngle(S_vector &axis, float &angle, int playerIndex)
{
    float v[3];
    ReadQuantized(v, GetRecipient(playerIndex), 7);
    const float cosPitch = std::cos(v[1]);
    axis = S_vector(std::cos(v[0]) * cosPitch, std::sin(v[1]), std::sin(v[0]) * cosPitch);
    angle = v[2];
}
