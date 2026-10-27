// S_NetQuantization::Setup (net/bitstream.cpp, 0x0073dbb0) against the formula worked out apart:
// bits = floor(ln(span / step) / ln 2) + 1 with each step rounded to float as the x87 does in the
// game's 24-bit precision (the logarithms from double), scale = the bits' mask / span (float).
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "net/bitstream.h"
#include "ls3d/math.h"

namespace {

int failures;

void Check(const S_vector4 &span, const S_vector4 &from, const S_vector4 &step, int unknown)
{
    S_NetQuantization q = {};
    q.Setup(span, from, step, unknown);
    const float spans[4] = {span.x, span.y, span.z, span.w}, froms[4] = {from.x, from.y, from.z, from.w},
                steps[4] = {step.x, step.y, step.z, step.w};
    bool ok = q.unknown70 == unknown;
    for (int i = 0; i < 4; ++i)
        ok &= q.deadZone[i] == steps[i] && q.min[i] == froms[i] && q.offset[i] == froms[i] && q.range[i] == spans[i] &&
              q.range40[i] == spans[i];
    for (int i = 0; i < 3; ++i) {
        const float ratio = spans[i] / steps[i];
        // The x87's way in 24-bit precision: ln 2 and ln(ratio) (FYL2X) rounded to float, their
        // quotient rounded to float, then floor + 1.
        const float ln2 = static_cast<float>(std::log(2.0));
        const float lnRatio = static_cast<float>(std::log(static_cast<double>(ratio)));
        const float quotient = lnRatio / ln2;
        const int bits = static_cast<int>(std::floor(quotient)) + 1;
        const uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
        const float scale = static_cast<float>(static_cast<double>(mask) / spans[i]);
        if (q.bits[i] != bits || q.scale[i] != scale) {
            ok = false;
            std::printf("  component %d: ratio %.9g bits %d (want %d) scale %.9g (want %.9g)\n", i, ratio, q.bits[i], bits,
                        q.scale[i], scale);
        }
    }
    if (!ok) {
        ++failures;
        std::printf("FAIL span (%g %g %g) step (%g %g %g)\n", span.x, span.y, span.z, step.x, step.y, step.z);
    }
}

}  // namespace

int main()
{
    int cases = 0;
    // The car's two (StartMission) for top speeds from 5 to 60 m/s in 0.1 steps.
    for (int k = 50; k <= 600; ++k) {
        const float top = static_cast<float>(k) * 0.1f;
        Check({top + top, 20.0f, top + top, 0.0f}, {-top, -10.0f, -top, 0.0f}, {top * 0.01f, 0.2f, top * 0.01f, 0.0f}, 100);
        Check({top * 0.1f, top * 0.05f, top * 0.1f, 0.0f}, {top * -0.05f, top * -0.025f, top * -0.05f, 0.0f},
              {0.01f, 0.013f, 0.01f, 0.0f}, 100);
        cases += 2;
    }
    // Exact powers of two and their neighbours (the floor's edges).
    for (int e = 1; e <= 20; ++e) {
        const float p = std::ldexp(1.0f, e);
        Check({p, p * 0.999f, p * 1.001f, 1.0f}, {0.0f, -1.0f, 2.0f, 3.0f}, {1.0f, 1.0f, 1.0f, 4.0f}, e);
        Check({3.0f * p, p, 0.5f * p, 0.0f}, {1.0f, 2.0f, 3.0f, 0.0f}, {3.0f, 1.0f, 0.5f, 0.0f}, 7);
        cases += 2;
    }
    std::printf("quantization setup: %d cases, %d failures\n", cases, failures);
    return failures != 0;
}
