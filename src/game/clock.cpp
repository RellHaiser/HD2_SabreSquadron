// Clocks (C_Clock; original 0x005b8010..0x005b8860).
#include "game/clock.h"

#include <cstring>

#include "common/chunk.h"
#include "ls3d/i3d.h"

namespace {

constexpr uint32_t FIND_ANY_FRAME = 0xFFFF;
// The hands' turn about z in radians per second: 6°, 0.1° and 1/120° (clockwise).
constexpr float SECOND_HAND_RATE = -0.104719758f;
constexpr float MINUTE_HAND_RATE = -0.00174532935f;
constexpr float HOUR_HAND_RATE = -0.000145444108f;

// The hand turned by `angle` about z from its rotation at the start.
void TurnHand(I3D_frame *hand, const S_quat &start, float angle)
{
    hand->SetRot(start.RotateByMatrix(RotateZMatrix(angle)));
    hand->Update();
}

void ResetHand(I3D_frame *hand, const S_quat &start)
{
    hand->SetRot(start);
    hand->Update();
}

}  // namespace

bool C_Clock::SetFrame(I3D_frame *frame)                // 0x005b8240
{
    m_second = nullptr;
    m_minute = nullptr;
    m_hour = nullptr;
    return C_Actor::SetFrame(frame);
}

void C_Clock::RestartMission(bool)                      // 0x005b80d0
{
    StopMission(false);
    StartMission(false);
}

uint32_t __stdcall C_Clock::FindHandsProc(I3D_frame *frame, void *context)   // 0x005b8430
{
    C_Clock *clock = static_cast<C_Clock *>(context);
    if (!_stricmp(frame->GetShortName(), "hour"))
        clock->m_hour = frame;
    if (!_stricmp(frame->GetShortName(), "min"))
        clock->m_minute = frame;
    if (!_stricmp(frame->GetShortName(), "sec"))
        clock->m_second = frame;
    return 0;
}

void C_Clock::StartMission(bool server)                 // 0x005b82d0
{
    if (server)
        return;
    m_frame->EnumFrames(FindHandsProc, this, FIND_ANY_FRAME, nullptr);
    m_time = 0.0f;
    m_tick = 0.0f;
    if (m_second)
        m_secondRot = m_second->GetRot();
    if (m_minute)
        m_minuteRot = m_minute->GetRot();
    if (m_hour)
        m_hourRot = m_hour->GetRot();
}

void C_Clock::StopMission(bool server)                  // 0x005b84e0
{
    if (server)
        return;
    if (m_second)
        ResetHand(m_second, m_secondRot);
    if (m_minute)
        ResetHand(m_minute, m_minuteRot);
    if (m_hour)
        ResetHand(m_hour, m_hourRot);
}

void C_Clock::Update(int frameTime)                     // 0x005b8610
{
    const float seconds = static_cast<float>(static_cast<uint32_t>(frameTime)) * 0.001f;
    m_time = seconds + m_time;
    m_tick = seconds + m_tick;
    if (!m_second || !(m_tick > 1.0f))
        return;
    TurnHand(m_second, m_secondRot, m_time * SECOND_HAND_RATE);
    if (m_minute)
        TurnHand(m_minute, m_minuteRot, m_time * MINUTE_HAND_RATE);
    if (m_hour)
        TurnHand(m_hour, m_hourRot, m_time * HOUR_HAND_RATE);
    m_tick -= static_cast<float>(static_cast<int>(m_tick));
}

bool C_Clock::SaveGame(C_chunk &chunk)                  // 0x005b80f0
{
    chunk.Write(m_time);
    chunk.Write(m_tick);
    return true;
}

bool C_Clock::LoadGame(C_chunk &chunk)                  // 0x005b8150
{
    m_time = 0.0f;
    m_tick = 0.0f;
    float time = 0.0f;                                  // (the original goes on with stack values)
    float tick = 0.0f;
    chunk.Read(time);
    chunk.Read(tick);
    // The hands turned to the saved time (in whole ms), then the gathered part restored.
    Update(static_cast<int>(time * 1000.0f));
    m_tick = tick;
    return true;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_ClockCreator::Create()                       // 0x005b8040
{
    return new C_Clock();                               // _aligned_malloc(0xe0, 16) in the original
}

void C_ClockCreator::Destroy(C_Actor *actor)            // 0x005b80b0
{
    delete actor;
}

static C_ClockCreator g_clockCreator;                   // 0x008ac5a0

// Registers the creator (static initialiser 0x005b8020, thunk 0x005b8010).
static const bool g_clockCreatorRegistered = [] {
    g_actorCreators[ACTOR_CLOCK] = &g_clockCreator;
    return true;
}();
