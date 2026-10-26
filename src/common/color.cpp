#include "common/color.h"

#include <cmath>

void RgbToHsv(const float rgb[3], float hsv[3])
{
    const float r = rgb[0], g = rgb[1], b = rgb[2];

    float maxValue;
    if (r < g || r < b)
        maxValue = (g < r || g < b) ? b : g;
    else
        maxValue = r;

    float minValue;
    if (r > g || r > b)
        minValue = (g > r || g > b) ? b : g;
    else
        minValue = r;

    hsv[2] = maxValue;
    const float delta = maxValue - minValue;
    if (maxValue == 0.0f) {
        hsv[1] = 0.0f;
        hsv[0] = -1.0f;
        return;
    }
    hsv[1] = delta / maxValue;
    // (The original divides by zero for grays; the hue is unused when the
    // saturation is 0.)
    if (delta == 0.0f) {
        hsv[0] = 0.0f;
        return;
    }

    float hue;
    if (maxValue == r)
        hue = (g - b) / delta;
    else if (maxValue == g)
        hue = (b - r) / delta + 2.0f;
    else
        hue = (r - g) / delta + 4.0f;
    hue *= 60.0f;
    if (hue < 0.0f)
        hue += 360.0f;
    hsv[0] = hue;
}

void HsvToRgb(const float hsv[3], float rgb[3])
{
    const float saturation = hsv[1];
    const float value = hsv[2];
    if (saturation == 0.0f) {
        rgb[0] = rgb[1] = rgb[2] = value;
        return;
    }

    const float sector = hsv[0] * 0.016666668f;   // / 60
    const int index = static_cast<int>(std::floor(sector));
    const float fraction = sector - static_cast<float>(index);
    const float p = (1.0f - saturation) * value;
    const float q = (1.0f - fraction * saturation) * value;
    const float t = (1.0f - (1.0f - fraction) * saturation) * value;

    switch (index) {
    case 0: rgb[0] = value; rgb[1] = t;     rgb[2] = p;     break;
    case 1: rgb[0] = q;     rgb[1] = value; rgb[2] = p;     break;
    case 2: rgb[0] = p;     rgb[1] = value; rgb[2] = t;     break;
    case 3: rgb[0] = p;     rgb[1] = q;     rgb[2] = value; break;
    case 4: rgb[0] = t;     rgb[1] = p;     rgb[2] = value; break;
    default: rgb[0] = value; rgb[1] = p;    rgb[2] = q;     break;
    }
}
