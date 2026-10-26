// RGB <-> HSV conversion (used by the particle and other effects to scale colors
// by a brightness).
#pragma once

// rgb: 0..1 each. hsv: hue in degrees (0..360, -1 for black), saturation and
// value 0..1.
void RgbToHsv(const float rgb[3], float hsv[3]);            // 0x006e9fe0
void HsvToRgb(const float hsv[3], float rgb[3]);            // 0x006ea100
