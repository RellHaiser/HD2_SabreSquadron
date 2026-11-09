// Screen-space ambient occlusion (LS3DF.ini [Render] AmbientOcclusion; gfx/fixed/fixed_device.h). Three
// passes over the whole picture, from a depth buffer of the frame's opaque scene:
// - PSOcclusion: how much of the surroundings each pixel's nearby geometry hides (the estimator of McGuire
//   et al., "The Alchemy Screen-Space Ambient Obscurance Algorithm", from a spiral of depth samples);
// - PSBlur: that smoothed across the picture, once each way, not across depth edges;
// - PSApply: multiplied into the picture (the pipeline's blend does it).
//
// One source like fixed_function.hlsl, for the same compilers, with SDL_GPU's bindings: fragment samplers
// t/s0-1 space2 (Vulkan set 2), fragment uniforms b0 space3 (set 3). OpenGL compiles it as GLSL 3.30 with
// GLSL, one of VERTEX_STAGE or PIXEL_STAGE, and OCCLUSION_PASS 0 - 2.
//
// Depth is Direct3D's window z on every API. In this file v = 0 is the picture's top row: OpenGL's vertex
// stage turns y over, as fixed_function.hlsl's does, so its offscreen targets have Direct3D's rows.

#ifdef GLSL
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define frac fract
#define lerp mix
#define saturate(x) clamp((x), 0.0, 1.0)
#define const
#define READ(name, uv) textureLod(name, uv, 0.0)
#else
#ifdef __spirv__
#define VK_SAMPLER(n) [[vk::combinedImageSampler]][[vk::binding(n, 2)]]
#else
#define VK_SAMPLER(n)
#endif
#define READ(name, uv) name.SampleLevel(name##Sampler, uv, 0)
#endif

#ifdef GLSL
#ifdef VERTEX_STAGE
out vec2 vUv;
void main()
{
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? -3.0 : 1.0);
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
    vUv = vec2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
}
#endif
#else
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
#endif

#if !defined(GLSL) || defined(PIXEL_STAGE)

#ifdef GLSL
layout(std140) uniform OcclusionUniforms
#else
cbuffer OcclusionUniforms : register(b0, space3)
#endif
{
    float4 projection;          // the projection's _11, _22, _33, _43
    float4 shape;               // radius (scene units), strength, fog start, 1 / (fog end - fog start) (0: no fog)
    float4 pixel;               // 1 / width, 1 / height, width, height
    float4 direction;           // PSBlur: a step (x, y) in texture coordinates
};

#ifdef GLSL
uniform sampler2D depthTexture;
uniform sampler2D occlusionTexture;
in vec2 vUv;
layout(location = 0) out vec4 outColour;
#else
VK_SAMPLER(0) Texture2D depthTexture : register(t0, space2);
VK_SAMPLER(0) SamplerState depthTextureSampler : register(s0, space2);
VK_SAMPLER(1) Texture2D occlusionTexture : register(t1, space2);
VK_SAMPLER(1) SamplerState occlusionTextureSampler : register(s1, space2);
#endif

#define SPIRAL_SAMPLES 16
#define SPIRAL_TURNS 7.0
#define FAR_DEPTH 0.99999

// Camera-space distance from window z.
float ViewZ(float z) { return projection.w / (z - projection.z); }
float DepthAt(float2 uv) { return READ(depthTexture, uv).r; }
float3 ViewPosition(float2 uv, float z)
{
    const float d = ViewZ(z);
    return float3((2.0 * uv.x - 1.0) * d / projection.x, (1.0 - 2.0 * uv.y) * d / projection.y, d);
}

bool OnScreen(float2 uv) { return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0; }

// The nearer of the two ways along an axis: the far one may cross a depth edge. At the picture's edge, the
// one way there is: beyond it, the depth read is the edge's own, at a place it is not.
float3 NearerStep(float3 p, float2 uv, float2 step)
{
    const float3 a = ViewPosition(uv + step, DepthAt(uv + step)) - p;
    const float3 b = p - ViewPosition(uv - step, DepthAt(uv - step));
    if (!OnScreen(uv + step))
        return b;
    if (!OnScreen(uv - step))
        return a;
    return abs(a.z) < abs(b.z) ? a : b;
}

float Occlusion(float2 uv)
{
    const float z = DepthAt(uv);
    if (z >= FAR_DEPTH)
        return 1.0;
    const float3 p = ViewPosition(uv, z);
    const float radius = shape.x;
    // The radius on the picture; too small to see, nothing to do. Its reach is kept from being wider than the
    // picture only for a camera against a wall: a smaller reach is a smaller radius, and the occlusion would
    // change as the camera comes nearer or goes further.
    float2 reach = float2(radius * projection.x / p.z, radius * projection.y / p.z) * 0.5;
    if (reach.y * pixel.w < 2.0)
        return 1.0;
    reach *= min(1.0, 1.0 / reach.y);
    float3 normal = normalize(cross(NearerStep(p, uv, float2(pixel.x, 0.0)), NearerStep(p, uv, float2(0.0, pixel.y))));
    if (normal.z > 0.0)
        normal = -normal;
    // Each pixel's own start on the spiral (interleaved gradient noise); the blur evens it out.
    const float2 screen = floor(uv * pixel.zw);
    const float start = 6.2831853 * frac(52.9829189 * frac(screen.x * 0.06711056 + screen.y * 0.00583715));
    const float radius2 = radius * radius;
    const float bias = 0.002 * p.z;
    float sum = 0.0;
    float count = 0.0;
    for (int i = 0; i < SPIRAL_SAMPLES; ++i) {
        const float t = (float(i) + 0.5) / float(SPIRAL_SAMPLES);
        const float angle = start + t * SPIRAL_TURNS * 6.2831853;
        const float2 at = uv + float2(cos(angle), sin(angle)) * reach * t;
        // Off the picture there is nothing to read: the edge's depth there would be a wall that is not. The
        // samples on it stand for those off it (not open space either: near the picture's edge, the
        // occlusion would come and go with how much of the surroundings is on it).
        if (!OnScreen(at))
            continue;
        count += 1.0;
        const float sampleZ = DepthAt(at);
        if (sampleZ < FAR_DEPTH) {
            const float3 v = ViewPosition(at, sampleZ) - p;
            const float vv = dot(v, v);
            const float falloff = max(radius2 - vv, 0.0);
            sum += falloff * falloff * falloff * max((dot(v, normal) - bias) / (vv + 0.0001), 0.0);
        }
    }
    const float scale = radius / (radius2 * radius2 * radius2 * max(count, 4.0));
    const float seen = max(0.0, 1.0 - shape.y * scale * sum);
    // What fog hides shows less of it.
    return lerp(seen, 1.0, saturate((p.z - shape.z) * shape.w));
}

float4 Blur(float2 uv)
{
    const float z = ViewZ(DepthAt(uv));
    // The surface's slope along the blur, from the nearer neighbour: a floor, a stair or a wall seen at a
    // slant changes depth fast from pixel to pixel and is still one surface, to be smoothed as one.
    const float dA = ViewZ(DepthAt(uv + direction.xy)) - z;
    const float dB = z - ViewZ(DepthAt(uv - direction.xy));
    const float slope = abs(dA) < abs(dB) ? dA : dB;
    const float tolerance = 0.02 * z + 0.05;
    float sum = READ(occlusionTexture, uv).r;
    float weight = 1.0;
    for (int i = 1; i <= 3; ++i) {
        const float gauss = 1.0 - float(i) * 0.2;
        const float2 a = uv + direction.xy * float(i), b = uv - direction.xy * float(i);
        const float closeA = saturate(1.0 - abs(ViewZ(DepthAt(a)) - (z + slope * float(i))) / tolerance);
        const float closeB = saturate(1.0 - abs(ViewZ(DepthAt(b)) - (z - slope * float(i))) / tolerance);
        sum += READ(occlusionTexture, a).r * gauss * closeA + READ(occlusionTexture, b).r * gauss * closeB;
        weight += gauss * (closeA + closeB);
    }
    const float value = sum / weight;
    return float4(value, value, value, 1.0);
}

#ifdef GLSL
void main()
{
#if OCCLUSION_PASS == 0
    const float value = Occlusion(vUv);
    outColour = vec4(value, value, value, 1.0);
#elif OCCLUSION_PASS == 1
    outColour = Blur(vUv);
#else
    const float value = READ(occlusionTexture, vUv).r;
    outColour = vec4(value, value, value, 1.0);
#endif
}
#else
float4 PSOcclusion(VSOut i) : SV_Target
{
    const float value = Occlusion(i.uv);
    return float4(value, value, value, 1.0);
}
float4 PSBlur(VSOut i) : SV_Target { return Blur(i.uv); }
float4 PSApply(VSOut i) : SV_Target
{
    const float value = READ(occlusionTexture, i.uv).r;
    return float4(value, value, value, 1.0);
}
#endif

#endif
