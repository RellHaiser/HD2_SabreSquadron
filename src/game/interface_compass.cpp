// The compass and its radar (see interface.h; the original's 0x00493680..0x00494950).
#include "game/interface.h"

#include <cmath>
#include <cstdlib>

#include "common/c_str.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/human_movement.h"
#include "game/profile.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

namespace {

constexpr float PI = 3.14159274f;                       // 0x0080f7ac
constexpr float TWO_PI = 6.28318548f;                   // 0x0080f94c

// The needle's spring: the pull per ms is sin(heading - needle) / 100, the friction 3 % of that,
// the speed at most 0.35.
constexpr float NEEDLE_PULL = 0.01f;
constexpr float NEEDLE_FRICTION = 0.03f;
constexpr float NEEDLE_MAX_SPEED = 0.35f;

// The sound marks flash for 500 ms (their alpha the time left / 500).
constexpr int SOUND_MARK_TIME = 500;
constexpr float SOUND_MARK_FADE = 0.002f;

// The radar is off at this difficulty.
constexpr int DIFFICULTY_NO_RADAR = 4;

// The step's wobble: how fast it decays (per ms) and how many steps are skipped between kicks.
float s_wobbleDecay = 1.0f;                             // 0x0089bc5c
float s_unused89bc58 = 0.0f;                            // 0x0089bc58 (Init clears it; nothing reads it)
int s_wobbleSkip = 0;                                   // 0x0089bc64

C_Human *ActivePlayer()
{
    return g_teamManager.GetPlayerTeam()->GetActivePlayer();
}

float WrapAngle(float angle)
{
    if (angle > PI)
        angle -= TWO_PI;
    if (angle < -PI)
        angle += TWO_PI;
    return angle;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// S_RadarBlip

S_RadarBlip::S_RadarBlip(C_Human *human_, I3D_frame *ally, I3D_frame *enemy) : human(human_)
{
    I3D_frame *source = human_->GetRole() != HUMAN_ROLE_ENEMY ? ally : enemy;
    visual = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
    visual->Duplicate(source);
    visual->SetOn(true);
    visual->LinkTo(source->GetParent(), 1);
    visual->SetPosInline(source->GetPos());
    const S_vector &pos = source->GetPos();
    radius = MagnitudeBy((pos.z * pos.z + pos.y * pos.y) + pos.x * pos.x);
    visual->SetRot(source->GetRot());
    visual->Update();
    static_cast<I3D_object_visual *>(visual)->SetAlpha(0.0f);
    alpha = 1.0f;
}

S_RadarBlip::~S_RadarBlip()
{
    if (visual) {
        visual->Release();
        visual = nullptr;
    }
}

void S_RadarBlip::FadeOut()
{
    if (alpha != 0.0f)
        alpha = 0.0f;
}

bool S_RadarBlip::Update(int timeDelta, C_Human *active)
{
    I3D_object_visual *object = static_cast<I3D_object_visual *>(visual);
    float shown = object->GetAlpha();
    if (alpha != 0.0f && (human->IsDead() || human->IsSurrendered() || human->IsUnconscious()))
        FadeOut();
    // Fades in / out at 1 per second; gone when faded out.
    bool fade = true;
    if (alpha == 0.0f) {
        shown -= static_cast<float>(timeDelta) * 0.001f;
        if (shown <= 0.0f)
            return false;
    } else if (shown >= 1.0f) {
        fade = false;
    } else {
        shown = static_cast<float>(timeDelta) * 0.001f + shown;
        if (shown > 1.0f)
            shown = 1.0f;
    }
    if (fade)
        object->SetAlpha(shown);

    // Hidden for the active soldier himself (and without one).
    if (!active || active == human) {
        visual->SetOn(false);
        return true;
    }
    if (!visual->IsOn())
        visual->SetOn(true);
    // The bearing from the active soldier's facing to the human, on the radar's circle.
    float facing[2] = {active->GetFacingDir().x, active->GetFacingDir().z};
    const float facingLength = std::sqrt(facing[0] * facing[0] + facing[1] * facing[1]);
    if (facingLength > 1e-8f) {
        const float k = 1.0f / facingLength;
        facing[0] *= k;
        facing[1] = k * facing[1];
    }
    const S_vector &from = active->GetFrame()->GetWorldPos();
    const S_vector &to = human->GetFrame()->GetWorldPos();
    float toHuman[2] = {to.x - from.x, to.z - from.z};
    const float length = std::sqrt(toHuman[1] * toHuman[1] + toHuman[0] * toHuman[0]);
    if (length > 1e-8f) {
        const float k = 1.0f / length;
        toHuman[0] *= k;
        toHuman[1] = k * toHuman[1];
    }
    const float angle = AngleBetween2D(toHuman, facing);
    // (FSIN / FCOS on the x87 stack.)
    const float x = static_cast<float>(std::sin(static_cast<double>(angle)) * radius);
    const float y = static_cast<float>(std::cos(static_cast<double>(angle)) * radius);
    visual->SetPosInline(S_vector(x, y, visual->GetPos().z));
    visual->Update();
    return true;
}

// ---------------------------------------------------------------------------------------------
// C_Radar

C_Radar::C_Radar(I3D_scene *scene)
{
    m_ally = scene->FindFrame("kul_us", FRAME_MASK_ANY);
    m_enemy = scene->FindFrame("kul_en", FRAME_MASK_ANY);
    if (m_ally)
        m_ally->SetOn(false);
    if (m_enemy)
        m_enemy->SetOn(false);
}

C_Radar::~C_Radar()
{
    for (std::size_t i = m_blips.size(); i-- > 0;)
        delete m_blips[i];
    m_blips.clear();
}

void C_Radar::Update(int timeDelta)
{
    C_Human *active = ActivePlayer();
    for (std::size_t i = m_blips.size(); i-- > 0;) {
        if (!m_blips[i]->Update(timeDelta, active)) {
            delete m_blips[i];
            m_blips[i] = m_blips.back();
            m_blips.pop_back();
        }
    }
}

void C_Radar::Add(C_Human *human)
{
    if (!human || !IsLocalGame())
        return;
    // (The original also tests for difficulty 3 first.)
    if (g_gameProfile.GetDifficulty() == DIFFICULTY_NO_RADAR)
        return;
    if (!m_ally || !m_enemy)
        return;
    m_blips.push_back(new S_RadarBlip(human, m_ally, m_enemy));
}

void C_Radar::Remove(C_Human *human)
{
    if (!human)
        return;
    // (The newest of the human's blips.)
    for (std::size_t i = m_blips.size(); i-- > 0;) {
        if (m_blips[i]->human == human) {
            m_blips[i]->FadeOut();
            return;
        }
    }
}

void C_Radar::Forget(const C_Human *human)
{
    for (std::size_t i = m_blips.size(); i-- > 0;) {
        if (m_blips[i]->human == human) {
            delete m_blips[i];
            m_blips[i] = m_blips.back();
            m_blips.pop_back();
        }
    }
}

// ---------------------------------------------------------------------------------------------
// C_Compass

bool C_Compass::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("kompas", FRAME_MASK_ANY);
    m_card = scene->FindFrame("kmpscvr", FRAME_MASK_ANY);
    m_arrow = scene->FindFrame("strelka", FRAME_MASK_ANY);
    m_radar = new C_Radar(scene);
    for (auto &times : m_soundTimes)
        times[0] = times[1] = 0;
    for (int i = 0; i < 8; ++i) {
        m_soundMarks[i][0] = m_frame->FindChildFrame(C_fstr("kmps_bl%02d", i), FRAME_MASK_ANY);
        m_soundMarks[i][0]->SetOn(false);
        m_soundMarks[i][1] = m_frame->FindChildFrame(C_fstr("kmps_rd%02d", i), FRAME_MASK_ANY);
        m_soundMarks[i][1]->SetOn(false);
    }
    m_wobble = 0.0f;
    m_needle = 0.0f;
    m_needleSpeed = 0.0f;
    m_heading = 0.0f;
    s_wobbleDecay = 1.0f;
    s_unused89bc58 = 0.0f;
    m_frame->Update();
    return true;
}

void C_Compass::Done()
{
    delete m_radar;
    m_radar = nullptr;
    m_frame = nullptr;
}

void C_Compass::Show(bool show)
{
    if (m_frame)
        m_frame->SetOn(show);
}

void C_Compass::Update(int timeDelta)
{
    UpdateHeading();
    const float wobble = UpdateWobble(timeDelta);
    // The needle swings toward the heading (a spring with friction).
    const float k = static_cast<float>(timeDelta) * NEEDLE_PULL;
    const float pull = WrapAngle(m_heading - m_needle);
    m_needleSpeed = static_cast<float>(std::sin(static_cast<double>(pull)) * k) + m_needleSpeed;
    if (m_needleSpeed > 0.0f) {
        m_needleSpeed = m_needleSpeed - k * NEEDLE_FRICTION;
        if (m_needleSpeed < 0.0f)
            m_needleSpeed = 0.0f;
    } else if (m_needleSpeed < 0.0f) {
        m_needleSpeed = k * NEEDLE_FRICTION + m_needleSpeed;
        if (m_needleSpeed > 0.0f)
            m_needleSpeed = 0.0f;
    }
    if (m_needleSpeed >= NEEDLE_MAX_SPEED)
        m_needleSpeed = NEEDLE_MAX_SPEED;
    else if (m_needleSpeed <= -NEEDLE_MAX_SPEED)
        m_needleSpeed = -NEEDLE_MAX_SPEED;
    m_needle = WrapAngle(m_needleSpeed * k + m_needle);
    const S_vector up(0.0f, 0.0f, 1.0f);
    m_card->SetWorldDir(up, -m_needle);
    m_card->Update();
    m_arrow->SetWorldDir(up, -wobble);
    m_arrow->Update();
    UpdateSoundMarks(timeDelta);
    if (m_radar)
        m_radar->Update(timeDelta);
}

// The arrow's wobble: each change of the active soldier's leading foot (every few steps, by his
// pace) kicks the heading aside and starts a decaying swing.
float C_Compass::UpdateWobble(int timeDelta)
{
    if (m_wobble > 0.0f) {
        m_wobble = m_wobble - static_cast<float>(timeDelta) * s_wobbleDecay;
        if (m_wobble <= 0.0f)
            m_wobble = 0.0f;
    }
    C_Human *active = ActivePlayer();
    if (active && active->GetMoveMode()) {
        const uint32_t state = active->GetStateFlags();
        const bool leftFoot = active->IsLeftFootForward();
        if (leftFoot != m_leftFoot) {
            m_leftFoot = leftFoot;
            if (s_wobbleSkip == 0) {
                // Stronger and slower to settle the faster the pace (state bits 0x10000 / 0x4000
                // / 0x2000; inferred: sprinting, running, walking).
                if (state & 0x10000) {
                    m_wobble = 0.2f;
                    s_wobbleDecay = 0.00018f;
                } else if (state & 0x4000) {
                    m_wobble = 0.1f;
                    s_wobbleDecay = 0.00015f;
                } else if (state & 0x2000) {
                    m_wobble = 0.05f;
                    s_wobbleDecay = 0.0001f;
                } else {
                    m_wobble = 0.025f;
                    s_wobbleDecay = 0.00005f;
                }
                s_wobbleSkip = std::rand() % 4 + 3;
                const float side = m_leftFoot ? 1.0f : -1.0f;
                m_heading = m_heading - (RandFloat() * 2.5f + 1.5f) * side * m_wobble;
            } else {
                --s_wobbleSkip;
            }
        }
    }
    // (FSIN on the x87 stack.)
    const float time = static_cast<float>(static_cast<double>(GetGame()->GetTime()) * static_cast<double>(0.03f));
    const float swing = static_cast<float>(std::sin(static_cast<double>(time)) * m_wobble);
    const float tilt = static_cast<float>(std::sin(static_cast<double>(m_needle + m_needle)) * 0.1);
    return swing + tilt;
}

void C_Compass::UpdateHeading()
{
    C_Human *active = ActivePlayer();
    if (!active)
        return;
    const S_vector &view = active->GetViewDir();
    S_vector dir(view.x, 0.0f, view.z);
    NormalizeBy(dir, dir.x * dir.x + dir.z * dir.z);
    float heading = dir.AngleTo(S_vector(0.0f, 0.0f, 1.0f));
    m_heading = heading;
    // (The original's test: (z + y) * 0 + x, x for any finite vector.)
    if ((dir.z + dir.y) * 0.0f + dir.x < 0.0f)
        m_heading = TWO_PI - heading;
    m_heading = WrapAngle(m_heading);
}

void C_Compass::UpdateSoundMarks(int timeDelta)
{
    for (int direction = 8; direction-- > 0;) {
        for (int color = 2; color-- > 0;) {
            I3D_frame *mark = m_soundMarks[direction][color];
            int &time = m_soundTimes[direction][color];
            if (time == 0) {
                mark->SetOn(false);
            } else {
                if (!mark->IsOn())
                    mark->SetOn(true);
                static_cast<I3D_object_visual *>(mark)->SetAlpha(static_cast<float>(time) * SOUND_MARK_FADE);
                time -= timeDelta;
                if (time < 0)
                    time = 0;
            }
            mark->Update();
        }
    }
}

void C_Compass::ShowSound(const S_vector &dir, int chapter)
{
    if (!ActivePlayer())
        return;
    float flat[2] = {dir.x, dir.z};
    const float length = std::sqrt(flat[1] * flat[1] + flat[0] * flat[0]);
    if (length > 1e-8f) {
        const float k = 1.0f / length;
        flat[0] *= k;
        flat[1] = k * flat[1];
    }
    const float north[2] = {0.0f, 1.0f};
    float angle = AngleBetween2D(flat, north);
    if (angle < 0.0f)
        angle += TWO_PI;
    // Eight directions of 45 degrees.
    int direction = static_cast<int>(angle * 1.27323949f);
    // Fix: a tiny negative angle (+ 2 pi) gave direction 8, past the marks.
    if (direction > 7)
        direction = 7;
    // Footsteps (SOUND_CHAPTER_PUPPET_MOVEMENT) blue, the rest red.
    m_soundTimes[direction][chapter == 0 ? 0 : 1] = SOUND_MARK_TIME;
}
