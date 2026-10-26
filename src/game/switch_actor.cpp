// Switches (C_Switch; original 0x006131d0..0x006140c0).
#include "game/switch_actor.h"

#include <cstdio>

#include "common/chunk.h"
#include "game/frame_box_volume.h"
#include "game/pending.h"                               // g_gameRole, g_texts
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }

// ReadProperties' / WriteProperties' chunks.
enum : uint16_t {
    PROPERTY_STATE = 0,
    PROPERTY_SOUND = 1,
};

constexpr uint32_t FIND_ANY_FRAME = 0xFFFF;
constexpr int TEXT_SWITCH = 0x1BF3;                     // the actor's name
constexpr int MAX_STATE = 3;
constexpr int STATE_BITS = 8;

}  // namespace

C_Switch::C_Switch() : C_Actor(ACTOR_SWITCH)            // 0x00613450
{
    m_name = g_texts.Get(TEXT_SWITCH);
}

void C_Switch::RestartMission(bool)                     // 0x00613290
{
    m_quiet = true;
    StopMission(true);
    StartMission(true);
}

bool C_Switch::SaveGame(C_chunk &chunk)                 // 0x006132b0
{
    C_Actor::SaveGame(chunk);
    chunk.Write(m_state);
    return true;
}

bool C_Switch::LoadGame(C_chunk &chunk)                 // 0x006132f0
{
    C_Actor::LoadGame(chunk);
    int state = 0;
    chunk.Read(state);
    m_quiet = true;
    SetState(state);
    m_quiet = false;
    return true;
}

bool C_Switch::Unknown24(C_chunk &chunk)                // 0x006133a0
{
    chunk.Write(m_state);
    return true;
}

bool C_Switch::Unknown28(C_chunk &chunk)                // 0x006133d0
{
    chunk.Read(m_state);
    SetState(m_state);
    return true;
}

bool C_Switch::SetFrame(I3D_frame *frame)               // 0x00613550
{
    if (!frame)
        return false;
    const bool ok = C_Actor::SetFrame(frame);
    m_stateCount = 0;
    // Each state frame is put where "1" is, under "1"'s parent.
    I3D_frame *first = m_frame->FindChildFrame("1", FIND_ANY_FRAME);
    for (;;) {
        char name[16];                                  // (8 bytes in the original)
        std::sprintf(name, "%d", m_stateCount + 1);
        I3D_frame *state = m_frame->FindChildFrame(name, FIND_ANY_FRAME);
        if (!state)
            break;
        if (first && first != state) {
            state->LinkTo(first->GetParent(), 0);
            state->SetPosInline(first->GetPos());
        }
        ++m_stateCount;
    }
    SetState(0);
    return ok;
}

void C_Switch::Toggle()                                 // 0x00613640
{
    if (m_state == m_stateCount) {
        m_state = 1;
        SetState(m_state);
        return;
    }
    SetState(++m_state);
}

void C_Switch::WriteState(C_BitStream *msg)             // 0x00613680
{
    msg->WriteBits(static_cast<uint32_t>(m_state), STATE_BITS);
}

void C_Switch::ReadState(C_BitStream *msg)              // 0x006136a0
{
    SetState(static_cast<int>(msg->ReadBits(STATE_BITS)));
}

void C_Switch::CopyFrom(const C_Actor *source)          // 0x006136c0
{
    if (!source)
        return;
    C_Actor::CopyFrom(source);
    const auto *other = static_cast<const C_Switch *>(source);
    m_state = other->m_state;
    m_sound = other->m_sound;
}

void C_Switch::SetState(int state)                      // 0x006136f0
{
    m_state = state;
    if (state < 1 || state > MAX_STATE)
        m_state = 1;
    if (!m_frame)
        return;
    if (IsServer() && !m_quiet) {
        if (const S_FrameData *data = m_frame->GetData()) {
            if (data->script)
                data->script->OnUse(m_state);
        }
    }
    I3D_frame *first = m_frame->FindChildFrame("1", FIND_ANY_FRAME);
    if (!first)
        return;
    I3D_frame *parent = first->GetParent();
    const S_vector pos = first->GetPos();
    if (!parent)
        return;
    char name[16];
    std::sprintf(name, "%d", m_state);                  // (_itoa in the original)
    I3D_frame *shown = m_frame->FindChildFrame(name, FIND_ANY_FRAME);
    if (!shown)
        return;
    shown->LinkTo(parent, 0);
    shown->SetPosInline(pos);
    shown->Update();
    // Every child of the actor's frame hidden, then the state's frame shown.
    for (int i = m_frame->NumChildren(); i-- > 0;) {
        if (m_frame->GetChild(i))
            m_frame->GetChild(i)->SetOn(false);
    }
    shown->SetOn(true);
    if (m_sound < 0)
        return;
    S_SoundRequest request;
    const S_matrix &world = m_frame->GetMatrix();
    request.pos = world(3);
    request.posW = world.m[3][3];
    request.flags = SOUND_SILENT_TO_AI;
    request.chapter = SOUND_CHAPTER_OBJECT_MANIPULATION;
    request.group = m_sound;
    GetSoundModule()->Play(request);
}

void C_Switch::StartMission(bool server)                // 0x00613920
{
    Activate(server);
    if (!(server || !IsServer()) || !m_frame)
        return;
    MakeFrameBoxVolume(m_volume, m_invMatrix, m_frame, COLLISION_TYPE_SWITCH, this);
    g_collisionWorld.AddVolume(&m_volume);
    g_collisionWorld.MoveVolume(&m_volume);
    m_quiet = false;
    m_initialState = m_state;
}

void C_Switch::StopMission(bool server)                 // 0x00613bd0
{
    Deactivate(server);
    g_collisionWorld.RemoveVolume(&m_volume);
    if (!server)
        return;
    m_state = m_initialState;
    SetState(m_initialState);
}

bool C_Switch::WriteProperties(C_chunk &chunk)          // 0x00613c10
{
    chunk.Begin(PROPERTY_STATE);
    chunk.Write(m_state);
    chunk.Ascend();
    chunk.Begin(PROPERTY_SOUND);
    chunk.Write(m_sound);
    chunk.Ascend();
    return false;                                       // Quirk kept: false.
}

bool C_Switch::ReadProperties(C_chunk &chunk)           // 0x00613e10
{
    // Quirk kept: a chunk of another id is entered and not left (its contents are read as
    // chunks).
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == PROPERTY_STATE) {
            int state = 0;                              // (the original goes on with a stack value)
            chunk.Read(state);
            chunk.Ascend();
            m_state = state;
            SetState(state);
        } else if (id == PROPERTY_SOUND) {
            int sound = 0;
            chunk.Read(sound);
            chunk.Ascend();
            m_sound = sound;
        }
    }
    return true;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_SwitchCreator::Create()                      // 0x00613200
{
    return new C_Switch();                              // _aligned_malloc(0x150, 16) in the original
}

void C_SwitchCreator::Destroy(C_Actor *actor)           // 0x00613270
{
    delete actor;
}

static C_SwitchCreator g_switchCreator;                 // 0x008aca68

// Registers the creator (static initialiser 0x006131e0, thunk 0x006131d0).
static const bool g_switchCreatorRegistered = [] {
    g_actorCreators[ACTOR_SWITCH] = &g_switchCreator;
    return true;
}();
