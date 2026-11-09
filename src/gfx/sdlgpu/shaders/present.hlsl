// The back buffer drawn through the game's gamma ramp into the window (gfx/fixed/fixed_device.h,
// Renderer::Present). Direct3D's ramp changes what the display shows, not the back buffer, so read-backs
// and screenshots never see it. One source like fixed_function.hlsl, for the same compilers, with
// SDL_GPU's bindings: fragment samplers t/s0-1 space2 (Vulkan set 2).
//
// The ramp is a 256 x 1 texture: each channel's table in its own channel. The picture may be scaled and
// so filtered, so the table is read with linear filtering too: each texel's centre is an exact entry.

#ifdef GLSL

#ifdef VERTEX_STAGE
out vec2 vUv;
void main()
{
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? -3.0 : 1.0);
    gl_Position = vec4(p, 0.0, 1.0);
    vUv = vec2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
}
#endif

#ifdef PIXEL_STAGE
uniform sampler2D picture;
uniform sampler2D ramp;
in vec2 vUv;
layout(location = 0) out vec4 outColour;
void main()
{
    vec3 c = texture(picture, vUv).rgb * 255.0 / 256.0 + 0.5 / 256.0;
    outColour = vec4(texture(ramp, vec2(c.r, 0.5)).r, texture(ramp, vec2(c.g, 0.5)).g, texture(ramp, vec2(c.b, 0.5)).b, 1.0);
}
#endif

#else

#ifdef __spirv__
#define VK_SAMPLER(n) [[vk::combinedImageSampler]][[vk::binding(n, 2)]]
#else
#define VK_SAMPLER(n)
#endif

struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    const float2 p = float2(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0);
    VSOut o;
    o.position = float4(p, 0.0, 1.0);
    o.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return o;
}

VK_SAMPLER(0) Texture2D picture : register(t0, space2);
VK_SAMPLER(0) SamplerState pictureSampler : register(s0, space2);
VK_SAMPLER(1) Texture2D ramp : register(t1, space2);
VK_SAMPLER(1) SamplerState rampSampler : register(s1, space2);

float4 PSMain(VSOut i) : SV_Target
{
    const float3 c = picture.Sample(pictureSampler, i.uv).rgb * 255.0 / 256.0 + 0.5 / 256.0;
    return float4(ramp.Sample(rampSampler, float2(c.r, 0.5)).r, ramp.Sample(rampSampler, float2(c.g, 0.5)).g,
                  ramp.Sample(rampSampler, float2(c.b, 0.5)).b, 1.0);
}

#endif
