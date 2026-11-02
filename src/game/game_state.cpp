#include "game/game_state.h"

// The base's slots (part 9d-ii-a): nothing done, nothing handled.

C_GameState::~C_GameState() = default;

bool C_GameState::Start(void *)
{
    return false;
}

bool C_GameState::Stop()
{
    return false;
}

bool C_GameState::HandleInput(float *, float *)
{
    return false;
}

void C_GameState::Tick(int) {}

void C_GameState::Update(int) {}

void C_GameState::Render() {}

int C_GameState::GetTypeId() const
{
    return GAME_STATE_TYPE_NONE;
}

int C_GameState::GetState() const
{
    return m_state;
}

void C_GameState::SetState(int state)
{
    m_state = state;
}

int C_GameState::GetPendingState() const
{
    return m_pendingState;
}

void C_GameState::SetPendingState(int state)
{
    m_pendingState = state;
}

void C_GameState::CommitPendingState()
{
    if (m_pendingState == GAME_STATE_NO_PENDING)
        return;
    m_state = m_pendingState;
    m_pendingState = GAME_STATE_NO_PENDING;
}
