// Debris (C_Debris; original 0x005fd9f0..0x005fddc0).
#include "game/debris.h"

#include "game/actors_module.h"
#include "game/pending.h"

namespace {

constexpr float LIFE_TIME = 30000.0f;                   // ms
constexpr float LIFE_TIME_SPREAD = 10000.0f;            // ms
constexpr float FADE_TIME = 500.0f;                     // ms: the fade-out starts this long before the end

}  // namespace

C_Debris::C_Debris() : C_Actor(ACTOR_DEBRIS) {}         // 0x005fdab0

C_Debris::~C_Debris() = default;                        // 0x005fdb50

bool C_Debris::SetFrame(I3D_frame *frame)               // 0x005fdba0
{
    return C_Actor::SetFrame(frame);
}

void C_Debris::StartMission(bool server)                // 0x005fdbb0
{
    Activate(server);
    m_fading = false;
}

void C_Debris::StopMission(bool server)                 // 0x005fdbd0
{
    m_body.Stop();
    Deactivate(server);
}

void C_Debris::SetupWheel(const S_WheelDebrisParams &params)   // 0x005fdc00
{
    m_lifeTime = static_cast<int>(RandFloat() * LIFE_TIME_SPREAD + LIFE_TIME);
    m_body.SetFrame(m_frame);
    m_body.SetupWheel(params);
    m_body.Start();
    m_body.SetOwner(this);
}

void C_Debris::Setup(const S_DebrisParams &params, bool fadeOut)   // 0x005fdc50
{
    m_lifeTime = static_cast<int>(RandFloat() * LIFE_TIME_SPREAD + LIFE_TIME);
    m_fadeOut = fadeOut;
    m_body.SetFrame(m_frame);
    m_body.Setup(params);
    m_body.Start();
    m_body.SetOwner(this);
}

void C_Debris::Update(int frameTime)                    // 0x005fdd10
{
    m_body.Tick(static_cast<uint32_t>(frameTime));
    if (m_lifeTime < 0)
        return;
    m_lifeTime -= frameTime;
    if (m_lifeTime < 1) {
        // (Without fade-out the debris stays: the life counts on below 0 and stops.)
        if (m_fadeOut) {
            GetGEffects()->ClearModelHighlight(m_frame);
            g_temporaryActors.DestroyActor(this);
        }
        return;
    }
    if (static_cast<float>(m_lifeTime) <= FADE_TIME && !m_fading && m_fadeOut) {
        m_fading = true;
        GetGEffects()->FadeOutModel(m_frame, m_lifeTime, false);
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_DebrisCreator::Create()                      // 0x005fda20
{
    return new C_Debris();                              // _aligned_malloc(0x6a0, 16) in the original
}

void C_DebrisCreator::Destroy(C_Actor *actor)           // 0x005fda90
{
    delete actor;
}

static C_DebrisCreator g_debrisCreator;                 // 0x008ac9f0

// Registers the creator (static initialiser 0x005fda00).
static const bool g_debrisCreatorRegistered = [] {
    g_actorCreators[ACTOR_DEBRIS] = &g_debrisCreator;
    return true;
}();
