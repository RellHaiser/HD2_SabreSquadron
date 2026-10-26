// The light level of a human's model (original source 0x0078a0e0..0x0078a520): the light at
// the body measured against the light of the sector (the "visibility" the AI uses too), eased
// in over time and given to the model through the effects module.
#include "game/human_movement.h"

#include <cstdlib>

#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"
#include "ls3d/i3d.h"

namespace {

// Light type of a sector's ambient light (I3D_light +0x164).
constexpr int LIGHT_TYPE_AMBIENT = 4;

}  // namespace

void C_HumanLighting::Init()
{
    m_level = 1.0f;
    m_shownLevel = 1.0f;
    // The first samples of the humans are spread over a second.
    m_time = std::rand() % 1000 + 250;
}

void C_HumanLighting::Update(I3D_frame *model, int time, int mode)
{
    int interval;
    switch (mode) {
    case 0:
        interval = 250;
        break;
    case 1:
        interval = 500;
        break;
    case 3:
        interval = 10000000;
        break;
    case 5:
        interval = 1400;
        break;
    default:
        interval = 1000;
        break;
    }
    if (GetLevel() != m_shownLevel) {
        Approach(time);
        GetGEffects()->SetModelLightLevel(model, m_shownLevel);
    }
    if (m_time < interval) {
        m_time += time;
        return;
    }
    m_time += time;
    // A model not drawn in the last frame waits (unless the step is long or in mode 0).
    if (model->GetRenderStamp() != GetGame()->GetScene()->GetRenderStamp() && time < 1000 && mode != 0) {
        m_time = interval;
        return;
    }
    m_sampleDue = true;
    m_time %= interval;
}

void C_HumanLighting::Sample(I3D_sector *sector, I3D_frame *model, const S_vector &pos, bool always)
{
    if (!m_sampleDue)
        return;
    m_sampleDue = false;
    const S_vector at = pos;
    const bool wasOn = model->IsOn();
    model->SetOn(false);
    float reference = 0.0f;
    bool measure = true;
    if (!always) {
        // The controller may know the values (the original reads the human through the
        // model's frame data).
        const auto *data = reinterpret_cast<C_Human *const *>(model->GetData());
        const C_Human *human = data ? *data : nullptr;
        measure = !human->ControllerUnknown5C(&m_intensity, &reference);
    }
    if (measure) {
        reference = GetGame()->GetLightIntensity(at, sector, false);
        m_intensity = GetGame()->GetLightIntensity(at, sector, true);
    }
    m_level = reference != 0.0f ? m_intensity / reference : 1.0f;
    // The climate raises the dark levels.
    m_level = (1.0f - m_level) * m_climate + m_level;
    if (m_level > 1.0f)
        m_level = 1.0f;
    else if (m_level < 0.1f)
        m_level = 0.1f;
    Approach(m_time);
    GetGEffects()->SetModelLightLevel(model, m_shownLevel);
    model->SetOn(wasOn);
    model->Update();
}

void C_HumanLighting::Approach(int time)
{
    const float step = static_cast<float>(time) * 0.001f + static_cast<float>(time) * 0.001f;
    const float target = GetLevel();
    if (target <= m_shownLevel) {
        m_shownLevel -= step;
        if (m_shownLevel <= target)
            m_shownLevel = target;
    } else {
        m_shownLevel += step;
        if (target <= m_shownLevel)
            m_shownLevel = target;
    }
}

void C_HumanLighting::SetSector(I3D_sector *sector)
{
    // The scale of the first lit ambient light of the sector, by its strongest color channel.
    for (int i = 0; i < sector->NumLights(); ++i) {
        I3D_light *light = sector->GetLight(i);
        if (!light->IsOn() || light->GetLightType() != LIGHT_TYPE_AMBIENT)
            continue;
        const S_vector &color = light->GetColor();
        float strongest;
        if (color.y < color.x && color.z < color.x)
            strongest = color.x;
        else if (color.x < color.y && color.z < color.y)
            strongest = color.y;
        else
            strongest = color.z;
        m_ambient = strongest * light->GetPower();
        return;
    }
    m_ambient = 1.0f;
}
