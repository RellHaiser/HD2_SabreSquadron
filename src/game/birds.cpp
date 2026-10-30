// The mission's birds (see birds.h).
#include "game/birds.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include "game/game.h"
#include "game/pending.h"
#include "ls3d/i3d.h"

std::vector<C_Bird *> g_birds;                          // 0x0089bb90 (static init 0x00484ec0)
std::vector<I3D_frame *> g_spareBirdModels;             // 0x0089bba0

namespace {

// The engine's inline normalization with the square summed (x² + z²) + y² (a climbing bird).
void NormalizeXZY(S_vector &v)
{
    const float sq = (v.x * v.x + v.z * v.z) + v.y * v.y;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return;
    if (!(sq < 1e-8f)) {
        const float k = 1.0f / std::sqrt(sq);
        v.x = k * v.x;
        v.y = v.y * k;
        v.z = k * v.z;
    } else if (v.x != 0.0f) {
        v.x = v.x < 0.0f ? -1.0f : 1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z < 0.0f ? -1.0f : 1.0f;
    } else {
        v.y = v.y < 0.0f ? -1.0f : 1.0f;
    }
}

// The same with the square summed (z² + y²) + x² (a bird flying off).
void NormalizeZYX(S_vector &v)
{
    const float sq = (v.z * v.z + v.y * v.y) + v.x * v.x;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return;
    if (!(sq < 1e-8f)) {
        const float k = 1.0f / std::sqrt(sq);
        v.x = v.x * k;
        v.y = v.y * k;
        v.z = v.z * k;
    } else if (v.x != 0.0f) {
        v.x = v.x < 0.0f ? -1.0f : 1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z < 0.0f ? -1.0f : 1.0f;
    } else {
        v.y = v.y < 0.0f ? -1.0f : 1.0f;
    }
}

// CollectBirds' search (0x0047a7b0).
uint32_t __stdcall CollectBird(I3D_frame *frame, void *)
{
    std::string name = frame->GetName();
    for (char &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name.find("ptacekletacek") != std::string::npos)
        g_birds.push_back(new C_Bird(frame, false));
    return 0;
}

}  // namespace

// 0x004797c0
C_Bird::C_Bird(I3D_frame *model, bool ownModel) : m_model(model), m_ownModel(ownModel)
{
    m_unknown40 = std::rand() % 5000 + 5000;
    m_timer = static_cast<float>(std::rand() % 20000 + 3000);
    model->LinkTo(GetGame()->GetScene()->GetPrimarySector(), I3DLINK_KEEP_WORLD);
    model->Update();
    S_vector pos = model->GetPos();
    // (Quirk kept: any frame data is taken for a bird there before.)
    if (const auto *before = reinterpret_cast<const C_Bird *>(model->GetData())) {
        pos = before->m_startPos;
        model->SetPosInline(pos);
        model->Update();
    }
    model->SetData(reinterpret_cast<S_FrameData *>(this));
    m_pos = pos;
    m_startPos = pos;
    m_startRot = model->GetRot();
    m_leftWing = model->FindChildFrame("Object03", FRAME_MASK_ANY);
    if (m_leftWing)
        m_leftWing->SetOn(false);
    m_rightWing = model->FindChildFrame("Object01", FRAME_MASK_ANY);
    if (m_rightWing)
        m_rightWing->SetOn(false);
}

// 0x004799e0
C_Bird::~C_Bird()
{
    if (m_model->GetData() && m_model->GetData() == reinterpret_cast<S_FrameData *>(this))
        m_model->SetData(nullptr);
    if (!m_ownModel) {
        m_model->SetPosInline(m_startPos);
        m_model->SetRot(m_startRot);
        m_model->SetOn(false);
        m_model->Update();
        g_spareBirdModels.push_back(m_model);
    } else {
        GetGame()->GetScene()->RemoveFrame(m_model);
    }
    m_model = nullptr;
    m_leftWing = nullptr;
    m_rightWing = nullptr;
}

// 0x0047a2e0
bool C_Bird::Update(int time)
{
    return Fly(static_cast<float>(time));
}

// 0x00479c70
bool C_Bird::Fly(float time)
{
    if (!m_leftWing || !m_rightWing)
        return false;
    bool moved = false;
    int more = 0;                                       // ms to run again in the next state
    switch (m_state) {
    case BIRD_TAKING_OFF:
        if (m_startPos.y + 10.0f < m_pos.y) {
            m_state = BIRD_CLIMBING;
            m_speed *= 0.75f;
            more = static_cast<int>(time);
        }
        break;
    case BIRD_CLIMBING:
        // Gravity; once the flight points down it levels out (gliding on the remaining time).
        m_velocity.y -= time * 5.000000555810402e-07f;
        NormalizeXZY(m_velocity);
        if (m_velocity.y < 0.0f) {
            m_velocity.y = -0.1f;
            m_state = BIRD_GLIDING;
            const float sq = (m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z) + 0.010000000707805157f;
            if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
                if (!(sq < 1e-8f)) {
                    const float k = 1.0f / std::sqrt(sq);
                    m_velocity.x = k * m_velocity.x;
                    m_velocity.y = -0.1f * k;
                    m_velocity.z = k * m_velocity.z;
                } else if (m_velocity.x != 0.0f) {
                    m_velocity.x = m_velocity.x < 0.0f ? -1.0f : 1.0f;
                } else if (m_velocity.z != 0.0f) {
                    m_velocity.z = m_velocity.z < 0.0f ? -1.0f : 1.0f;
                } else {
                    m_velocity.y = -1.0f;
                }
            }
            more = static_cast<int>(m_velocity.y * -2e6f);
        }
        {
            const float scale = m_speed * 0.001f;
            m_velocity.x = scale * m_velocity.x;
            m_velocity.y = scale * m_velocity.y;
            m_velocity.z = scale * m_velocity.z;
        }
        break;
    case BIRD_GLIDING: {
        // Gone beyond the far clip or 500 m.
        const S_vector &eye = GetGame()->GetScene()->GetActiveCamera()->GetWorldPos();
        const float dx = eye.x - m_pos.x;
        const float dy = eye.y - m_pos.y;
        const float dz = eye.z - m_pos.z;
        const float distance = (dy * dy + dz * dz) + dx * dx;
        const float farClip = GetGame()->GetFarClip();
        if (distance > farClip * farClip || distance > 250000.0f) {
            m_leftWing = nullptr;
            m_rightWing = nullptr;
            m_pos.y = m_startPos.y + 10.0f;
            return false;
        }
        break;
    }
    case BIRD_SITTING: {
        // A hop when the wait is over, a landing back at the start (then 20..40 s).
        m_timer -= time;
        const bool toggle = m_flapping ? m_pos.y < m_startPos.y : m_timer < 0.0f;
        if (toggle) {
            m_flapping = !m_flapping;
            if (!m_flapping)
                m_timer = static_cast<float>(std::rand() % 20000 + 20000);
            m_leftWing->SetOn(m_flapping);
            m_rightWing->SetOn(m_flapping);
            m_model->SetPosInline(m_startPos);
            m_pos = m_model->GetPos();
            const float lift = RandomFloat(0.001f);
            m_velocity = S_vector(0.0f, m_flapping ? static_cast<float>(lift + 0.0001) : 0.0f, 0.0f);
            moved = true;
        } else if (m_flapping) {
            m_velocity.y -= time * 1e-6f;
        }
        break;
    }
    default:
        break;
    }
    if (m_state != BIRD_SITTING || m_flapping) {
        m_pos.x = time * m_velocity.x + m_pos.x;
        m_pos.y = time * m_velocity.y + m_pos.y;
        m_pos.z = time * m_velocity.z + m_pos.z;
        m_model->SetPosInline(m_pos);
        moved = true;
    }
    m_wingPhase = time * m_speed * 0.007f + m_wingPhase;
    if (m_flapping) {
        // The wings flap about z, the right one mirrored.
        S_quat rot;
        rot.Make(S_vector(0.0f, 0.0f, 1.0f), static_cast<float>(std::sin(m_wingPhase) * 0.75f - 0.4));
        m_leftWing->SetRot(rot);
        m_rightWing->SetRot(S_quat(-rot.s, rot.v.x, rot.v.y, rot.v.z));
        m_model->Update();
    } else if (moved) {
        m_model->Update();
    }
    if (more != 0)
        Fly(static_cast<float>(more));
    return true;
}

// 0x0047a2f0
void C_Bird::FlyAway()
{
    if (m_flownAway)
        return;
    m_speed = RandomFloat(2.0f) + 5.0f;
    const float z = RandomFloat(0.5f);
    const float x = RandomFloat(0.5f);
    const S_vector ahead = m_model->GetMatrix()(2).NormalizedGame();
    S_vector velocity(ahead.x + x, ahead.y + 1.0f, ahead.z + z);
    NormalizeZYX(velocity);
    const float scale = m_speed * 0.001f;
    m_velocity = S_vector(scale * velocity.x, scale * velocity.y, scale * velocity.z);
    m_state = BIRD_TAKING_OFF;
    m_flapping = true;
    m_wingPhase = 3.1415927f / static_cast<float>(std::rand() % 100 + 1);
    if (m_rightWing && m_leftWing) {
        m_flownAway = true;
        m_leftWing->SetOn(true);
        m_rightWing->SetOn(true);
    }
}

// 0x0047a700
void UpdateBirds(int timeDelta)
{
    for (size_t i = g_birds.size(); i-- > 0;) {
        if (!g_birds[i]->Update(timeDelta)) {
            delete g_birds[i];
            g_birds[i] = g_birds.back();
            g_birds.pop_back();
        }
    }
}

// 0x0047a790
void CollectBirds()
{
    GetGame()->GetScene()->EnumFrames(&CollectBird, nullptr, 0x80, nullptr);
}

// 0x0047aa10
void FreeBirds()
{
    for (size_t i = g_birds.size(); i-- > 0;)
        delete g_birds[i];
    g_birds.clear();
    for (size_t i = g_spareBirdModels.size(); i-- > 0;) {
        g_spareBirdModels[i]->SetOn(true);
        g_spareBirdModels[i]->Update();
    }
    g_spareBirdModels.clear();
}
