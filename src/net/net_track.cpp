// The replicated values (part 10a): see net_track.h.
#include "net/net_track.h"

#include <cmath>
#include <cstring>

#include "net/bitstream.h"

float g_netUpdatePeriod;
bool g_netServerSide;
S_NetQuantization g_netAngleQuantization;
S_NetQuantization g_netAngleDeltaQuantization;
S_NetQuantization g_netAngleRateQuantization;

namespace {

constexpr float kPi = 3.1415927f;
constexpr float kTwoPi = 6.2831855f;

// The game's magnitude from a squared length: below ~1e-8 (compared as the float's bits) 0, within 1e-8 of 1
// exactly 1, else the root.
float Magnitude(float square)
{
    int32_t bits;
    std::memcpy(&bits, &square, sizeof bits);
    if (bits < 0x322bcc77)
        return 0.0f;
    if (std::fabs(square - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(square);
}

// The squares as the code sums them: a stored vector's x first, a difference's z first.
float SquareXYZ(const S_vector4 &v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

float SquareZYX(float x, float y, float z)
{
    return z * z + y * y + x * x;
}

// An angle above pi brought down a turn (once).
void Wrap(float &angle)
{
    if (angle > kPi)
        angle -= kTwoPi;
}

}  // namespace

// ---- the base ----------------------------------------------------------------------------------------------------

void C_NetQuantized::SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    if (value)
        m_read[0] = value;
    if (delta)
        m_read[1] = delta;
    if (rate)
        m_read[2] = rate;
}

void C_NetQuantized::SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    if (value)
        m_write[0] = value;
    if (delta)
        m_write[1] = delta;
    if (rate)
        m_write[2] = rate;
}

// ---- C_NetTrack --------------------------------------------------------------------------------------------------

C_NetTrack::C_NetTrack()
{
    Reset();
}

void C_NetTrack::Reset()
{
    m_flags = 7;
    // (The vectors' w components are left alone.)
    m_current.x = m_current.y = m_current.z = 0.0f;
    m_target.x = m_target.y = m_target.z = 0.0f;
    m_net.x = m_net.y = m_net.z = 0.0f;
    m_rate.x = m_rate.y = m_rate.z = 0.0f;
    m_sentRate.x = m_sentRate.y = m_sentRate.z = 0.0f;
    m_step.x = m_step.y = m_step.z = 0.0f;
    m_speed = 40.0f;
    m_lag = 0.035f;
    for (int i = 0; i < 3; ++i)
        m_read[i] = m_write[i] = nullptr;
    m_snapDistance = 3.0f;
    g_netUpdatePeriod = 0.1f;
}

void C_NetTrack::SetValue(const S_vector4 &value)
{
    if (!g_netServerSide) {
        m_net = value;
    } else {
        m_net.x = 0.0f;
        m_net.y = 0.0f;
        m_net.z = 0.0f;
    }
    m_current = value;
    m_target = value;
}

void C_NetTrack::SetRate(const S_vector4 &rate)
{
    m_rate = rate;
    m_speed = Magnitude(SquareXYZ(rate));
}

const S_vector4 &C_NetTrack::GetRate()
{
    if (SquareXYZ(m_rate) >= 1e-16f)
        return m_rate;
    return m_step;
}

void C_NetTrack::Update(float seconds)
{
    if (m_flags & NET_TRACK_MOVE_TARGET) {
        m_target.x = seconds * m_rate.x + m_target.x;
        m_target.y = seconds * m_rate.y + m_target.y;
        m_target.z = seconds * m_rate.z + m_target.z;
    }
    const float dx = m_target.x - m_current.x;
    const float dy = m_target.y - m_current.y;
    const float dz = m_target.z - m_current.z;
    const float distance = Magnitude(SquareZYX(dx, dy, dz));
    m_step.x = m_step.y = m_step.z = 0.0f;
    if (distance <= m_snapDistance && distance >= 1e-5f) {
        float speed = m_speed;
        if (m_flags & NET_TRACK_MOVE_TARGET)
            speed = speed * (distance * 0.33333334f + 1.0f);
        if (!(distance < seconds * speed)) {
            const float k = speed / distance;
            // (The step's w: an uninitialised local in the original; left alone.)
            m_step.x = dx * k;
            m_step.y = dy * k;
            m_step.z = dz * k;
            m_current.x = m_step.x * seconds + m_current.x;
            m_current.y = m_step.y * seconds + m_current.y;
            m_current.z = m_step.z * seconds + m_current.z;
            return;
        }
    }
    m_current = m_target;
}

void C_NetTrack::Write(C_NetMessage *stream, bool delta, S_vector4 *lastSent)
{
    BeginWrite(stream);
    const S_vector4 *values = &m_current;
    if (g_netServerSide && !(SquareXYZ(m_net) < 1e-16f))
        values = &m_net;
    const bool rateZero = SquareXYZ(m_rate) < 1e-16f;
    bool changed = false;
    float d[3];
    if (delta) {
        d[0] = values->x - lastSent->x;
        d[1] = values->y - lastSent->y;
        d[2] = values->z - lastSent->z;
        if (m_write[0]->WithinSteps(d) && (SquareXYZ(m_sentRate) < 1e-16f) == rateZero) {
            stream->WriteBits(0, 1);
            goto rate;
        }
    }
    stream->WriteBits(1, 1);
    if ((m_flags & NET_TRACK_DELTA) && delta) {
        d[0] = values->x - lastSent->x;
        d[1] = values->y - lastSent->y;
        d[2] = values->z - lastSent->z;
        if (m_write[1]->Contains(d) && (SquareXYZ(m_sentRate) < 1e-16f) == (SquareXYZ(m_rate) < 1e-16f)) {
            stream->WriteBits(1, 1);
            d[0] = values->x - lastSent->x;
            d[1] = values->y - lastSent->y;
            d[2] = values->z - lastSent->z;
            changed = stream->WriteQuantized(d, m_write[1], static_cast<uint8_t>(m_flags));
            goto sent;
        }
    }
    stream->WriteBits(0, 1);
    changed = stream->WriteQuantized(&values->x, m_write[0], static_cast<uint8_t>(m_flags));
sent:
    m_sentRate = m_rate;
    if (changed)
        *lastSent = *values;
rate:
    if (m_flags & NET_TRACK_SEND_RATE) {
        if (changed) {
            stream->WriteBits(1, 1);
            stream->WriteQuantized(&m_rate.x, m_write[2], static_cast<uint8_t>(m_flags));
        } else {
            stream->WriteBits(0, 1);
        }
    }
    EndWrite(stream);
}

void C_NetTrack::Read(C_NetMessage *stream)
{
    BeginRead(stream);
    const S_vector4 old = m_net;
    if (stream->ReadBits(1)) {
        if (!stream->ReadBits(1)) {
            // An absolute value: no rate worked out.
            stream->ReadQuantized(&m_net.x, m_read[0], m_flags);
            goto rate;
        }
        float d[3];
        stream->ReadQuantized(d, m_read[1], m_flags);
        m_net.x = d[0] + m_net.x;
        m_net.y = d[1] + m_net.y;
        m_net.z = d[2] + m_net.z;
    }
    if (m_flags & NET_TRACK_DERIVE_RATE) {
        const float dz = m_net.z - old.z;
        const float inverse = 1.0f / g_netUpdatePeriod;
        m_rate.x = (m_net.x - old.x) * inverse;
        m_rate.y = (m_net.y - old.y) * inverse;
        m_rate.z = dz * inverse;
        m_rate.w = old.w;
        if (SquareXYZ(m_rate) < 0.1f)
            m_rate.x = m_rate.y = m_rate.z = 0.0f;
        if (SquareXYZ(m_rate) >= 1e-16f)
            m_speed = Magnitude(SquareXYZ(m_rate));
    }
rate:
    if ((m_flags & NET_TRACK_SEND_RATE) && stream->ReadBits(1)) {
        stream->ReadQuantized(&m_rate.x, m_read[2], m_flags);
        if (SquareXYZ(m_rate) < 0.02f)
            m_rate.x = m_rate.y = m_rate.z = 0.0f;
        if (SquareXYZ(m_rate) >= 1e-16f)
            m_speed = Magnitude(SquareXYZ(m_rate));
    }
    if (m_flags & NET_TRACK_EXTRAPOLATE) {
        const float z = m_rate.z * m_lag;
        m_target.x = m_lag * m_rate.x + m_net.x;
        m_target.y = m_rate.y * m_lag + m_net.y;
        m_target.z = z + m_net.z;
        m_target.w = old.w;
    } else {
        m_target = m_net;
    }
    if (!(m_flags & NET_TRACK_KEEP_CURRENT))
        m_current = m_net;
    EndRead(stream);
}

// ---- C_NetDirTrack -----------------------------------------------------------------------------------------------

C_NetDirTrack::C_NetDirTrack()
{
    Reset();
    static bool made;                                             // 0x00982e00
    if (!made) {
        g_netAngleQuantization.Setup({kTwoPi, kTwoPi, kTwoPi, 0.0f}, {-kPi, -kPi, -kPi, 0.0f},
                                     {0.0122f, 0.0122f, 0.0122f, 0.0f}, 100);
        g_netAngleDeltaQuantization.Setup({0.31415927f, 0.31415927f, 0.31415927f, 0.0f},
                                          {-0.15707964f, -0.15707964f, -0.15707964f, 0.0f},
                                          {0.0122f, 0.0122f, 0.0122f, 0.0f}, 100);
        g_netAngleRateQuantization.Setup({0.15707964f, 0.15707964f, 0.15707964f, 0.0f},
                                         {-0.07853982f, -0.07853982f, -0.07853982f, 0.0f},
                                         {0.0122f, 0.0122f, 0.0122f, 0.0f}, 100);
        made = true;
    }
    m_write[0] = m_read[0] = &g_netAngleQuantization;
    m_write[1] = m_read[1] = &g_netAngleDeltaQuantization;
    m_write[2] = m_read[2] = &g_netAngleRateQuantization;
}

void C_NetDirTrack::Reset()
{
    C_NetTrack::Reset();
    m_flags = 3;
    m_direction.x = m_direction.y = m_direction.z = 0.0f;
    m_previous.x = m_previous.y = m_previous.z = 0.0f;
}

void C_NetDirTrack::SetDirection(const S_vector4 &dir, float angle)
{
    m_previous = m_current;
    m_current.x = std::atan2(dir.z, dir.x);
    m_current.y = std::asin(dir.y);
    m_current.z = angle;
    m_net = m_current;
    m_target = m_current;
    m_direction = dir;
}

void C_NetDirTrack::EndWrite(C_NetMessage *stream)
{
    const float turn = m_current.x - m_previous.x;
    if (turn < -0.001f) {
        stream->WriteBits(0, 2);
        return;
    }
    stream->WriteBits(turn <= 0.001f ? 2 : 1, 2);
}

void C_NetDirTrack::EndRead(C_NetMessage *stream)
{
    const uint32_t sign = stream->ReadBits(2);
    if (sign != 2)
        m_turnSign = static_cast<int>(sign);
    float dx = m_current.x - m_target.x;
    float dy = m_current.y - m_target.y;
    float dz = m_current.z - m_target.z;
    float square = SquareZYX(dx, dy, dz);
    if (square < 1e-16f)
        return;
    const float distance = Magnitude(square);
    const S_NetQuantization &q = *m_read[0];
    const float span = Magnitude(q.range40[0] * q.range40[0] + q.range40[1] * q.range40[1] + q.range40[2] * q.range40[2]);
    if (span * 0.5f < distance) {
        // The short way round.
        dx = kTwoPi - dx;
        dy = kTwoPi - dy;
        dz = kTwoPi - dz;
        Wrap(dx), Wrap(dy), Wrap(dz);
        Wrap(dx), Wrap(dy), Wrap(dz);
    }
    if ((dx < 0.0f && m_turnSign == 1) || (dx > 0.0f && m_turnSign == 0))
        dx = -dx;
    const float inverse = 1.0f / g_netUpdatePeriod;
    // (The rate's w: an uninitialised local in the original; left alone.)
    m_rate.x = inverse * dx;
    m_rate.y = dy * inverse;
    m_rate.z = inverse * dz;
    m_speed = Magnitude(SquareXYZ(m_rate));
    if (m_speed <= 8.0f)
        return;
    m_speed = (m_speed >= 0.0f ? 1.0f : -1.0f) * 8.0f;
    // The rate normalised (the engine's way: a tiny vector to its largest-looking axis) and scaled to 8.
    square = SquareXYZ(m_rate);
    if (!(std::fabs(square - 1.0f) < 1e-8f)) {
        if (square >= 1e-8f) {
            const float k = 1.0f / std::sqrt(square);
            m_rate.x = k * m_rate.x;
            m_rate.y = k * m_rate.y;
            m_rate.z = k * m_rate.z;
        } else if (m_rate.x != 0.0f) {
            m_rate.x = m_rate.x >= 0.0f ? 1.0f : -1.0f;
        } else if (m_rate.z == 0.0f) {
            m_rate.y = m_rate.y >= 0.0f ? 1.0f : -1.0f;
        } else {
            m_rate.z = m_rate.z >= 0.0f ? 1.0f : -1.0f;
        }
    }
    m_rate.x = m_speed * m_rate.x;
    m_rate.y = m_speed * m_rate.y;
    m_rate.z = m_speed * m_rate.z;
}

void C_NetDirTrack::Update(float seconds)
{
    if (m_flags & NET_TRACK_MOVE_TARGET) {
        m_target.x = seconds * m_rate.x + m_target.x;
        m_target.y = seconds * m_rate.y + m_target.y;
        m_target.z = seconds * m_rate.z + m_target.z;
    }
    float dx = m_target.x - m_current.x;
    float dy = m_target.y - m_current.y;
    float dz = m_target.z - m_current.z;
    float distance = Magnitude(SquareZYX(dx, dy, dz));
    if (m_snapDistance < distance || distance < 1e-5f) {
        m_current = m_target;
    } else {
        const S_NetQuantization &q = *m_read[0];
        const float span =
            Magnitude(q.range40[0] * q.range40[0] + q.range40[1] * q.range40[1] + q.range40[2] * q.range40[2]);
        if (span * 0.5f < distance) {
            // The short way round.
            dx = kTwoPi - dx * -1.0f;
            dy = kTwoPi - dy * -1.0f;
            dz = kTwoPi - dz * -1.0f;
            Wrap(dx), Wrap(dy), Wrap(dz);
            Wrap(dx), Wrap(dy), Wrap(dz);
            distance = Magnitude(SquareZYX(dx, dy, dz));
        }
        if (seconds * m_speed <= distance) {
            const float k = m_speed / distance;
            dx = dx * k;
            dy = dy * k;
            dz = k * dz;
            // (Beyond 0.785 the yaw moves at the rate's x instead: as the original.)
            m_current.x = (distance > 0.785f ? seconds * m_rate.x : dx * seconds) + m_current.x;
            m_current.y = dy * seconds + m_current.y;
            m_current.z = dz * seconds + m_current.z;
        } else {
            m_current = m_target;
        }
        Wrap(m_current.x), Wrap(m_current.y), Wrap(m_current.z);
        Wrap(m_current.x), Wrap(m_current.y), Wrap(m_current.z);
    }
    const float cosPitch = std::cos(m_current.y);
    m_direction.x = std::cos(m_current.x) * cosPitch;
    m_direction.z = std::sin(m_current.x) * cosPitch;
    m_direction.y = std::sin(m_current.y);
}
