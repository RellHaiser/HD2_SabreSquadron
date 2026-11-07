// The replicated values (part 10a): a value the server sends and the clients interpolate towards, its rate, the
// quantised delta sent to each player. Original file 0x0073fbe0..0x00741530.
#pragma once

#include <cstdint>

#include "ls3d/math.h"

struct S_NetQuantization;
class C_NetMessage;

// What the net values, objects and messages share (vtable 0x00816438): two sets of three quantisations, the one
// the values are read with (slot 0) and the one they are written with (slot 1). Each set: the absolute value,
// the delta from the one last sent, the rate.
class C_NetQuantized {
public:
    // The base stores only the non-null ones.
    virtual void SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta,
                                      S_NetQuantization *rate);   // 0x004c1360 (slot 0)
    virtual void SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta,
                                       S_NetQuantization *rate);  // 0x004c1390 (slot 1)
    virtual void Update(float seconds) = 0;                       // slot 2
    // (Slot 3: empty in the values; the property calls its tracks' GetCurrent and drops the results.)
    virtual void Refresh() = 0;
    virtual ~C_NetQuantized() = default;                          // slot 4 (the derived classes')

protected:
    S_NetQuantization *m_read[3] = {};                            // +0x04 value, delta, rate
    S_NetQuantization *m_write[3] = {};                           // +0x10
};

// The flags of a track (+0x88; the low 3 bits: the components sent).
enum : uint32_t {
    NET_TRACK_KEEP_CURRENT = 0x1000,    // the value read does not move the current one (only the target)
    NET_TRACK_DELTA = 0x2000,           // a value near the last one sent goes as the delta from it
    NET_TRACK_DERIVE_RATE = 0x4000,     // the rate worked out from the values read (over the update period)
    NET_TRACK_SEND_RATE = 0x8000,       // the rate sent too
    NET_TRACK_EXTRAPOLATE = 0x10000,    // the target leads the value read by the rate x the lag
    NET_TRACK_MOVE_TARGET = 0x20000,    // the target moves at the rate between reads
};

// A replicated vector (vtable 0x00816490, 0x90 bytes, 16-aligned): the current value moves towards the target at
// the speed (the rate's magnitude), jumping to it when near or farther than the snap distance.
class alignas(16) C_NetTrack : public C_NetQuantized {
public:
    C_NetTrack();                                                 // 0x0073fbe0
    ~C_NetTrack() override = default;                             // 0x0073fe40

    // The defaults (the flags 7, the vectors' x, y, z 0, the speed 40, the lag 0.035, the snap 3; the
    // quantisations dropped); also the update period g_netUpdatePeriod back to 0.1.
    void Reset();                                                 // 0x0073fe50 (__fastcall)

    void Update(float seconds) override;                          // 0x00740580
    void Refresh() override {}                                    // 0x0073fe10
    virtual void SetFlags(uint32_t flags) { m_flags = flags; }    // 0x0073fc10 (slot 5)
    virtual void SetLag(float lag) { m_lag = lag; }               // 0x0073fc20
    // Current, target and the net value at `value` (the server's net value cleared: its own sent).
    virtual void SetValue(const S_vector4 &value);                // 0x0073fc30 (slot 7)
    virtual void SetNetValue(const S_vector4 &value) { m_net = value; }   // 0x0073fca0
    // The rate and its magnitude, the speed.
    virtual void SetRate(const S_vector4 &rate);                  // 0x0073fcc0 (slot 9)
    virtual const S_vector4 &GetCurrent() { return m_current; }   // 0x0073fd60 (slot 10)
    virtual const S_vector4 &GetTarget() { return m_target; }     // 0x0073fd70
    virtual const S_vector4 &GetNetValue() { return m_net; }      // 0x0073fd80
    // The rate set or read; when it is zero the step the last update moved by.
    virtual const S_vector4 &GetRate();                           // 0x0073fd90 (slot 13)
    // The value for player whose last value sent is `lastSent` (`delta`: nothing when it moved less than the
    // steps, the delta when in its range); `lastSent` follows it when any component changed.
    virtual void Write(C_NetMessage *stream, bool delta, S_vector4 *lastSent);   // 0x0073fee0 (slot 14)
    virtual void Read(C_NetMessage *stream);                      // 0x007401e0
    // Before and after both (the direction track's turn sign).
    virtual void BeginWrite(C_NetMessage *) {}                    // 0x0073fdd0 (slot 16)
    virtual void BeginRead(C_NetMessage *) {}                     // 0x0073fde0
    virtual void EndWrite(C_NetMessage *) {}                      // 0x0073fdf0
    virtual void EndRead(C_NetMessage *) {}                       // 0x0073fe00 (slot 19)

    // (A vector given as S_vector: the original reads a w past it.)
    void SetRate(const S_vector &rate) { SetRate(S_vector4{rate.x, rate.y, rate.z, 0.0f}); }

    uint32_t GetFlags() const { return m_flags; }
    float GetSpeed() const { return m_speed; }
    float GetLag() const { return m_lag; }

protected:
    uint32_t m_unknown1C = 0;
    S_vector4 m_current = {};                                     // +0x20 the value shown
    S_vector4 m_target = {};                                      // +0x30
    S_vector4 m_net = {};                                         // +0x40 the value read (the server: the one forwarded)
    S_vector4 m_rate = {};                                        // +0x50
    S_vector4 m_sentRate = {};                                    // +0x60 the rate when last written
    S_vector4 m_step = {};                                        // +0x70 the velocity of the last update's move
    float m_speed = 0.0f;                                         // +0x80
    float m_lag = 0.0f;                                           // +0x84
    uint32_t m_flags = 0;                                         // +0x88
    float m_snapDistance = 0.0f;                                  // +0x8C
};

// The position track (vtable 0x008164e0; the base's slots).
class alignas(16) C_NetPositionTrack : public C_NetTrack {
public:
    C_NetPositionTrack() = default;                               // 0x00740770
    ~C_NetPositionTrack() override = default;                     // 0x007407b0
};

// The direction track (vtable 0x00816530, 0xC0 bytes): the value is yaw, pitch and an angle (the car's rotation
// angle); the direction vector made from them; the angles wrapped at pi; the rate worked out on the clients from
// the change read and the turn sign the server sends. Its quantisations are fixed (the angles over 2 pi, their
// deltas, their rates; steps of 0.0122).
class alignas(16) C_NetDirTrack : public C_NetTrack {
public:
    C_NetDirTrack();                                              // 0x007407e0
    ~C_NetDirTrack() override = default;                          // 0x007409e0

    // The base's defaults, then the flags 3, the direction and the previous angles cleared.
    void Reset();                                                 // 0x00740ac0 (__fastcall)

    void SetReadQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) override {}    // 0x007409a0
    void SetWriteQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) override {}   // 0x007409b0
    void Update(float seconds) override;                          // 0x00741010
    // The rate flags (0x4000 .. 0x20000) dropped.
    void SetFlags(uint32_t flags) override { m_flags = flags & 0xFFFC3FFFu; }   // 0x007414c0
    const S_vector4 &GetCurrent() override { return m_direction; }   // 0x00740aa0
    void BeginWrite(C_NetMessage *) override {}                   // 0x00740b00
    // The turn sign of the yaw since the direction was set: 0 decreasing, 1 increasing, 2 unchanged (2 bits).
    void EndWrite(C_NetMessage *stream) override;                 // 0x00740b10
    // The sign read (2: the last kept); the angular rate from the angles' change to the target, its sign the
    // sign's, at most 8.
    void EndRead(C_NetMessage *stream) override;                  // 0x00740b70
    // The direction (yaw atan2(z, x), pitch asin(y)) and the angle; the angles before it kept.
    virtual void SetDirection(const S_vector4 &dir, float angle); // 0x007409f0 (slot 20)
    virtual void SetAngle(float angle) { m_current.z = angle; }   // 0x00740a90
    virtual float GetAngle() { return m_current.z; }              // 0x00740ab0

    const S_vector4 &GetPrevious() const { return m_previous; }
    int GetTurnSign() const { return m_turnSign; }

private:
    S_vector4 m_direction = {};                                   // +0x90
    S_vector4 m_previous = {};                                    // +0xA0 the angles before the last SetDirection
    int m_turnSign = 0;                                           // +0xB0
};

// The period the clients' derived rates divide by (0.1 s: the server's tick; set by every track's Reset).
extern float g_netUpdatePeriod;                                   // 0x00982f90
// Whether this machine is the server (the game role's bit 0; set when the replication starts).
extern bool g_netServerSide;                                      // 0x00982f94
// The direction tracks' quantisations (made by the first one).
extern S_NetQuantization g_netAngleQuantization;                  // 0x00982f10
extern S_NetQuantization g_netAngleDeltaQuantization;             // 0x00982e90
extern S_NetQuantization g_netAngleRateQuantization;              // 0x00982e10
