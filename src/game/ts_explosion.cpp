// Timed scripted explosions (C_TSExplosion; original 0x0060ed90..0x00610010).
#include "game/ts_explosion.h"

#include "common/chunk.h"
#include "game/actors_module.h"                         // CreateActorMessage
#include "game/game.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/world.h"                                 // S_Explosion
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/bitstream.h"

namespace {

constexpr uint8_t PROPERTIES_VERSION = 1;               // the properties' first byte (read, not used)

// The saved game's chunks.
constexpr uint16_t CHUNK_EXPLOSION = 0xBE;
enum : uint16_t {
    CHUNK_FLAGS = 0,
    CHUNK_CLIENT_TIME = 1,
    CHUNK_SERVER_TIME = 2,
};

constexpr float EFFECT_HEIGHT = 1.0f;                   // m above the frame: the explosion, the sounds
constexpr float SCORCH_HEIGHT = 0.5f;
constexpr float SCORCH_SCALE = 2.5f;
constexpr int SCORCH_MODE = 5;
constexpr uint32_t TEXTURE_MODE = 0x402;

// A sound of the explosion chapter 1 m above the frame.
void PlaySound(I3D_frame *frame, int group)
{
    S_SoundRequest request;
    const S_vector &pos = frame->GetWorldPos();
    request.pos = S_vector(pos.x, pos.y + EFFECT_HEIGHT, pos.z);   // (posW: a stale local in the original)
    request.flags = SOUND_SILENT_TO_AI;
    request.chapter = SOUND_CHAPTER_EXPLOSION;
    request.group = group;
    GetSoundModule()->Play(request);
}

}  // namespace

bool C_TSExplosion::SetFrame(I3D_frame *frame)          // 0x0060eea0
{
    m_flags = 0;
    m_delay = -1;
    m_clientTime = 0;
    m_serverTime = 0;
    m_countdownSound = -1;
    m_range = 0.0f;
    m_damage = 0.0f;
    m_particle = -1;
    m_particle2 = -1;
    m_joinParticles = 0;
    m_scorch = nullptr;
    return C_Actor::SetFrame(frame);
}

void C_TSExplosion::StartMission(bool server)           // 0x0060eef0
{
    m_clientTime = 0;
    m_serverTime = 0;
    m_flags = 0;
    m_texture = nullptr;
    if (!server && GetIGraph()->LoadTexture("x_expl00.tga", nullptr, TEXTURE_MODE, &m_texture) != 0)
        return;
    Activate(server);
}

void C_TSExplosion::RemoveScorch()
{
    if (!m_scorch)
        return;
    m_scorch->LinkTo(nullptr, 0);
    GetGame()->GetScene()->RemoveFrame(m_scorch);
    m_scorch->Release();
    m_scorch = nullptr;
}

void C_TSExplosion::StopMission(bool server)            // 0x0060ef60
{
    RemoveScorch();
    if (m_texture) {
        m_texture->Release();
        m_texture = nullptr;
    }
    Deactivate(server);
}

bool C_TSExplosion::WriteProperties(C_chunk &chunk)     // 0x0060efd0
{
    chunk.Write(PROPERTIES_VERSION);
    chunk.Write(m_countdownSound);
    chunk.Write(m_delay);
    chunk.Write(m_explosionSound);
    chunk.Write(m_range);
    chunk.Write(m_damage);
    chunk.Write(m_particle);
    chunk.Write(m_particle2);
    chunk.Write(m_joinParticles);
    return true;
}

bool C_TSExplosion::ReadProperties(C_chunk &chunk)      // 0x0060f110
{
    uint8_t version = PROPERTIES_VERSION;
    chunk.Read(version);
    chunk.Read(m_countdownSound);
    chunk.Read(m_delay);
    chunk.Read(m_explosionSound);
    chunk.Read(m_range);
    chunk.Read(m_damage);
    chunk.Read(m_particle);
    chunk.Read(m_particle2);
    chunk.Read(m_joinParticles);
    return true;
}

void C_TSExplosion::CopyFrom(const C_Actor *source)     // 0x0060f350
{
    if (source->GetType() != ACTOR_TS_EXPLOSION)
        return;
    const auto *other = static_cast<const C_TSExplosion *>(source);
    m_countdownSound = other->m_countdownSound;
    m_delay = other->m_delay;
    m_explosionSound = other->m_explosionSound;
    m_range = other->m_range;
    m_damage = other->m_damage;
    m_particle = other->m_particle;
    m_particle2 = other->m_particle2;
    m_joinParticles = other->m_joinParticles;
}

void C_TSExplosion::RestartMission(bool server)         // 0x0060f3c0
{
    RemoveScorch();
    m_clientTime = 0;
    m_serverTime = 0;
    m_flags = 0;
    Reactivate(server);
}

bool C_TSExplosion::SaveGame(C_chunk &chunk)            // 0x0060f420
{
    if (!chunk.Begin(CHUNK_EXPLOSION))
        return false;
    chunk.Begin(CHUNK_FLAGS);
    chunk.Write(m_flags);
    chunk.Ascend();
    chunk.Begin(CHUNK_CLIENT_TIME);
    chunk.Write(m_clientTime);
    chunk.Ascend();
    chunk.Begin(CHUNK_SERVER_TIME);
    chunk.Write(m_serverTime);
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

bool C_TSExplosion::LoadGame(C_chunk &chunk)            // 0x0060f640
{
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id != CHUNK_EXPLOSION) {
        chunk.Ascend();
        return false;
    }
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        int value = 0;                                  // (the original goes on with a stack value)
        switch (id) {
        case CHUNK_FLAGS:
            chunk.Read(value);
            chunk.Ascend();
            m_flags = static_cast<uint32_t>(value);
            break;
        case CHUNK_CLIENT_TIME:
            chunk.Read(value);
            chunk.Ascend();
            m_clientTime = value;
            break;
        case CHUNK_SERVER_TIME:
            chunk.Read(value);
            chunk.Ascend();
            m_serverTime = value;
            break;
        default:
            chunk.Ascend();
            break;
        }
    }
    chunk.Ascend();
    if (m_flags & TS_EXPLOSION_SCORCHED)
        MakeScorch();
    return true;
}

void C_TSExplosion::OnMessage(C_BitStream *)            // 0x0060f970
{
    m_clientTime = 0;
    m_flags |= TS_EXPLOSION_CLIENT_COUNTING;
    ClientStep();
}

void C_TSExplosion::WriteState(C_BitStream *msg)        // 0x0060f9a0
{
    msg->WriteBits(m_flags != 0 ? 1 : 0, 1);
}

void C_TSExplosion::PlayParticles(int effect)
{
    g_particlesModule.CreateAt(effect, m_frame->GetWorldPos(), S_vector(0.0f, 1.0f, 0.0f), nullptr, true, false);
}

void C_TSExplosion::ReadState(C_BitStream *msg)         // 0x0060f9c0
{
    if (!msg->ReadBits(1))
        return;
    MakeScorch();
    if (m_particle != -1 && m_joinParticles)
        PlayParticles(m_particle);
    if (m_particle2 != -1 && m_joinParticles)
        PlayParticles(m_particle2);
}

void C_TSExplosion::Tick(int timeDelta)                 // 0x0060fab0
{
    if (!(m_flags & TS_EXPLOSION_COUNTING))
        return;
    m_serverTime += timeDelta;
    ServerStep();
}

void C_TSExplosion::Update(int frameTime)               // 0x0060fae0
{
    if (!(m_flags & TS_EXPLOSION_CLIENT_COUNTING))
        return;
    m_clientTime += frameTime;
    ClientStep();
}

void C_TSExplosion::Trigger(int)                        // 0x0060fb10
{
    m_flags |= TS_EXPLOSION_COUNTING;
    m_serverTime = 0;
    // (An empty message: the clients start their countdown on it.)
    SendActorMessage(CreateActorMessage(m_netId, true));
    ServerStep();
}

void C_TSExplosion::ClientStep()                        // 0x0060fb60
{
    if (m_clientTime == 0 && m_countdownSound != -1)
        PlaySound(m_frame, m_countdownSound);
    if (m_clientTime < m_delay)
        return;
    m_flags = (m_flags & ~TS_EXPLOSION_CLIENT_COUNTING) | TS_EXPLOSION_SCORCHED;
    if (m_particle != -1)
        PlayParticles(m_particle);
    if (m_particle2 != -1)
        PlayParticles(m_particle2);
    if (m_explosionSound != -1)
        PlaySound(m_frame, m_explosionSound);
    // Quirk kept: a decal made already stays, unreferenced, beside the new one.
    MakeScorch();
}

void C_TSExplosion::ServerStep()                        // 0x0060fd90
{
    if (m_serverTime < m_delay)
        return;
    m_flags = (m_flags & ~TS_EXPLOSION_COUNTING) | TS_EXPLOSION_EXPLODED;
    S_Explosion explosion;
    const S_matrix &world = m_frame->GetMatrix();
    explosion.pos = S_vector(world.m[3][0], world.m[3][1] + EFFECT_HEIGHT, world.m[3][2]);
    explosion.posW = world.m[3][3];
    explosion.range = m_range;
    explosion.damage = m_damage;
    explosion.unknown18 = false;
    explosion.source = nullptr;
    explosion.Execute();
}

void C_TSExplosion::MakeScorch()                        // 0x0060fe50
{
    const S_vector &at = m_frame->GetWorldPos();
    const S_vector pos(at.x, at.y + SCORCH_HEIGHT, at.z);
    auto *scorch = static_cast<I3D_projector_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR));
    m_scorch = scorch;
    scorch->SetTexture(m_texture);
    scorch->SetBuffered(0);
    scorch->SetOn(true);
    I3D_scene *scene = GetGame()->GetScene();
    scene->AddFrame(scorch);
    scorch->SetPos(pos);
    scorch->SetDir(S_vector(0.0f, -1.0f, 0.0f), 0.0f);
    scorch->SetScale(S_vector(SCORCH_SCALE, SCORCH_SCALE, SCORCH_SCALE));
    scorch->SetProjectionMode(SCORCH_MODE);
    scorch->SetOrthogonal(1);
    scorch->LinkTo(scene->GetSector(pos), 0);
    scorch->Update();
    scorch->SetBuffered(3);
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_TSExplosionCreator::Create()                 // 0x0060edc0
{
    return new C_TSExplosion();                         // _aligned_malloc(0xd0, 16) in the original
}

void C_TSExplosionCreator::Destroy(C_Actor *actor)      // 0x0060ee30
{
    delete actor;
}

static C_TSExplosionCreator g_tsExplosionCreator;       // 0x008aca38

// Registers the creator (static initialiser 0x0060eda0, thunk 0x0060ed90).
static const bool g_tsExplosionCreatorRegistered = [] {
    g_actorCreators[ACTOR_TS_EXPLOSION] = &g_tsExplosionCreator;
    return true;
}();
