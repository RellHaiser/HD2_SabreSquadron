// Direct3D 8's fixed-function pipeline as one vertex shader and one pixel shader, driven by uniforms
// (gfx/fixed/fixed_device.cpp fills them from the device's states). One source for every API:
// - compiled to DXIL (Direct3D 12) and SPIR-V (Vulkan) by build.py, with SDL_GPU's bindings: vertex
//   uniforms b0 space1 (Vulkan set 1), fragment samplers t/s0-2 space2 (set 2), fragment uniforms
//   b0 space3 (set 3);
// - compiled as GLSL 3.30 by the OpenGL driver (gfx/opengl), with GLSL and one of VERTEX_STAGE or
//   PIXEL_STAGE defined. So it keeps to what both languages take: no integer as a condition, no scalar
//   assigned to a vector, no C cast, no array initialiser; the macros below cover the rest.
//
// Vertex inputs, whatever the vertex format (missing ones come from a buffer of defaults):
//   0 position (xyz, or xyzw pre-transformed: screen x, y, z, 1/w), 1 normal, 2 diffuse, 3 specular
//   (D3DCOLOR: read as B, G, R, A), 4 and 5 the first two texture coordinate sets.
//
// The rules followed are Direct3D 9's documented fixed-function ones (Direct3D 8's are the same):
// lighting in camera space, colour sources, attenuation and spot falloff; vertex fog (and the
// specular alpha as the fog of pre-transformed vertices); texture stages with their arguments and
// modifiers, each result clamped; alpha test on 8-bit alpha. Pixel centres are Direct3D 9's (at
// whole coordinates): positions move half a pixel.

#define MAX_LIGHTS 8
#define STAGES 3

#ifdef GLSL
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int4 ivec4
#define float4x4 mat4
#define lerp mix
#define saturate(x) clamp((x), 0.0, 1.0)
#define mul(v, m) ((v) * (m))
#define const
#define SEM(x)
#define UNROLL
#define ROW_MAJOR
#define BLOCK(name, b, space) layout(std140, row_major) uniform name
#else
#define SEM(x) : x
#define UNROLL [unroll]
#define ROW_MAJOR row_major
#define BLOCK(name, b, space) cbuffer name : register(b, space)
#define VERTEX_STAGE
#define PIXEL_STAGE
// A stage's texture and sampler: one combined image sampler in Vulkan, SDL_GPU's binding.
#ifdef __spirv__
#define VK_SAMPLER(n) [[vk::combinedImageSampler]][[vk::binding(n, 2)]]
#else
#define VK_SAMPLER(n)
#endif
#endif

// ---- vertex uniforms

struct S_light {
    float4 diffuse, specular, ambient;
    float4 position;            // camera space
    float4 direction;           // camera space, normalised (where the light points)
    float4 range_falloff_att0_att1;
    float4 att2_cosTheta_cosPhi_type;   // (half angles); type 1 point, 2 spot, 3 directional
};

#ifdef VERTEX_STAGE
BLOCK(VertexUniforms, b0, space1)
{
    ROW_MAJOR float4x4 worldView;
    ROW_MAJOR float4x4 projection;
    ROW_MAJOR float4x4 textureMatrix[STAGES];
    float4 rows[4];             // programmable: oPos.x, y, z, w as dot products with these
    float4 viewport;            // pre-transformed: x, y, 2 / width, -2 / height
    float4 pixelOffset;         // half a pixel in clip space (x, y) and its depth range (z: min, w: max - min)
    int4 mode;                  // x: 0 transformed, 1 pre-transformed, 2 programmable; y: lighting;
                                // z: fog (0 none, 1 exp, 2 exp2, 3 linear; 4: from specular alpha); w: flags
    int4 texGen[STAGES];        // x: coordinate set, y: generation (0, 1 normal, 2 position, 3 reflection),
                                // z: transform flags (count, 256 projected)
    float4 fog;                 // start, end, density, 1 / (end - start)
    float4 globalAmbient;
    float4 materialDiffuse, materialAmbient, materialSpecular, materialEmissive;
    float4 materialPower;       // x
    int4 materialSource;        // diffuse, specular, ambient, emissive: 0 material, 1 diffuse, 2 specular
    int4 lightCount;            // x
    S_light lights[MAX_LIGHTS];
};
#endif

// mode.w flags
#define F_NORMAL 1
#define F_DIFFUSE 2
#define F_SPECULAR 4
#define F_NORMALIZE 8
#define F_LOCALVIEWER 16
#define F_SPECULARENABLE 32

struct VSIn {
    float4 position SEM(TEXCOORD0);
    float3 normal SEM(TEXCOORD1);
    float4 diffuse SEM(TEXCOORD2);
    float4 specular SEM(TEXCOORD3);
    float4 tex0 SEM(TEXCOORD4);
    float4 tex1 SEM(TEXCOORD5);
};

struct VSOut {
    float4 position SEM(SV_Position);
    float4 diffuse SEM(TEXCOORD0);
    float4 specular SEM(TEXCOORD1);    // a: the fog factor
    float4 tex0 SEM(TEXCOORD2);
    float4 tex1 SEM(TEXCOORD3);
    float4 tex2 SEM(TEXCOORD4);
};

#ifdef VERTEX_STAGE

float4 Source(int source, float4 material, float4 diffuse, float4 specular, int flags)
{
    if (source == 1 && (flags & F_DIFFUSE) != 0)
        return diffuse;
    if (source == 2 && (flags & F_SPECULAR) != 0)
        return specular;
    return material;
}

float4 TexCoord(int stage, VSIn v, float3 cameraPosition, float3 cameraNormal)
{
    const int4 g = texGen[stage];
    float4 t;
    if (g.y == 1)
        t = float4(cameraNormal, 1);
    else if (g.y == 2)
        t = float4(cameraPosition, 1);
    else if (g.y == 3) {
        const float3 e = normalize(cameraPosition);
        t = float4(e - 2 * dot(e, cameraNormal) * cameraNormal, 1);
    } else
        t = g.x == 0 ? float4(v.tex0.xy, 1, 1) : float4(v.tex1.xy, 1, 1);
    const int count = g.z & 255;
    if (count != 0) {
        // (A 2-element coordinate is (u, v, 1, 0) to the matrix, as Direct3D extends it.)
        float4 in4 = g.y == 0 ? float4(t.xy, 1, 0) : t;
        t = mul(in4, textureMatrix[stage]);
        if ((g.z & 256) != 0) {
            const float q = count == 2 ? t.y : count == 3 ? t.z : t.w;
            t = float4(t.xy, 1, q);
        } else
            t.w = 1;
    } else
        t.w = 1;
    return t;
}

VSOut VSMain(VSIn v)
{
    VSOut o;
    const int flags = mode.w;
    const float4 vertexDiffuse = v.diffuse.zyxw;
    const float4 vertexSpecular = v.specular.zyxw;
    float3 cameraPosition = float3(0, 0, 0);
    float3 cameraNormal = float3(0, 0, 1);
    float fogFactor = 1;

    if (mode.x == 1) {
        // Pre-transformed: screen position and 1 / w.
        const float w = v.position.w != 0.0 ? 1.0 / v.position.w : 1.0;
        const float2 ndc = float2((v.position.x + 0.5 - viewport.x) * viewport.z - 1.0,
                                  (v.position.y + 0.5 - viewport.y) * viewport.w + 1.0);
        const float z = (v.position.z - pixelOffset.z) / (pixelOffset.w != 0.0 ? pixelOffset.w : 1.0);
        o.position = float4(ndc * w, z * w, w);
        o.diffuse = (flags & F_DIFFUSE) != 0 ? vertexDiffuse : float4(1, 1, 1, 1);
        o.specular = (flags & F_SPECULAR) != 0 ? vertexSpecular : float4(0, 0, 0, 0);
        if (mode.z != 0)
            fogFactor = o.specular.a;
    } else {
        if (mode.x == 2) {
            const float4 p = float4(v.position.xyz, 1);
            o.position = float4(dot(p, rows[0]), dot(p, rows[1]), dot(p, rows[2]), dot(p, rows[3]));
            cameraPosition = p.xyz;
        } else {
            const float4 p = mul(float4(v.position.xyz, 1), worldView);
            cameraPosition = p.xyz;
            o.position = mul(p, projection);
            cameraNormal = mul(float4(v.normal, 0), worldView).xyz;
            if ((flags & F_NORMALIZE) != 0)
                cameraNormal = normalize(cameraNormal);
        }
        // Half a pixel, as Direct3D 9's pixel centres are.
        o.position.xy += pixelOffset.xy * o.position.w;

        if (mode.y != 0 && (flags & F_NORMAL) != 0) {
            const float4 cd = Source(materialSource.x, materialDiffuse, vertexDiffuse, vertexSpecular, flags);
            const float4 cs = Source(materialSource.y, materialSpecular, vertexDiffuse, vertexSpecular, flags);
            const float4 ca = Source(materialSource.z, materialAmbient, vertexDiffuse, vertexSpecular, flags);
            const float4 ce = Source(materialSource.w, materialEmissive, vertexDiffuse, vertexSpecular, flags);
            const float3 n = cameraNormal;
            float3 ambient = globalAmbient.rgb;
            float3 diffuse = float3(0, 0, 0), specular = float3(0, 0, 0);
            for (int i = 0; i < lightCount.x; ++i) {
                const S_light l = lights[i];
                const int type = int(l.att2_cosTheta_cosPhi_type.w);
                float3 toLight;
                float attenuation = 1;
                if (type == 3) {
                    toLight = -l.direction.xyz;
                } else {
                    const float3 d = l.position.xyz - cameraPosition;
                    const float distance = length(d);
                    toLight = d / max(distance, 1e-20);
                    if (distance > l.range_falloff_att0_att1.x)
                        attenuation = 0;
                    else {
                        const float a = l.range_falloff_att0_att1.z + l.range_falloff_att0_att1.w * distance +
                                        l.att2_cosTheta_cosPhi_type.x * distance * distance;
                        attenuation = a > 0.0 ? 1.0 / a : 1.0;
                    }
                    if (type == 2) {
                        const float rho = dot(-toLight, l.direction.xyz);
                        const float cosTheta = l.att2_cosTheta_cosPhi_type.y, cosPhi = l.att2_cosTheta_cosPhi_type.z;
                        float spot;
                        if (rho > cosTheta)
                            spot = 1;
                        else if (rho <= cosPhi)
                            spot = 0;
                        else
                            spot = pow(saturate((rho - cosPhi) / (cosTheta - cosPhi)), l.range_falloff_att0_att1.y);
                        attenuation *= spot;
                    }
                }
                ambient += l.ambient.rgb * attenuation;
                const float nl = dot(n, toLight);
                if (nl > 0.0) {
                    diffuse += l.diffuse.rgb * (nl * attenuation);
                    if ((flags & F_SPECULARENABLE) != 0) {
                        const float3 toViewer = (flags & F_LOCALVIEWER) != 0 ? normalize(-cameraPosition) : float3(0, 0, -1);
                        const float nh = dot(n, normalize(toViewer + toLight));
                        if (nh > 0.0 && materialPower.x > 0.0)
                            specular += l.specular.rgb * (pow(nh, materialPower.x) * attenuation);
                    }
                }
            }
            o.diffuse = float4(saturate(ca.rgb * ambient + cd.rgb * diffuse + ce.rgb), saturate(cd.a));
            o.specular = float4(saturate(cs.rgb * specular), 0);
        } else {
            o.diffuse = (flags & F_DIFFUSE) != 0 ? vertexDiffuse : float4(1, 1, 1, 1);
            o.specular = (flags & F_SPECULAR) != 0 ? vertexSpecular : float4(0, 0, 0, 0);
        }
        // Vertex fog on the camera-space depth.
        const float depth = abs(cameraPosition.z);
        if (mode.z == 3)
            fogFactor = saturate((fog.y - depth) * fog.w);
        else if (mode.z == 1)
            fogFactor = saturate(exp(-fog.z * depth));
        else if (mode.z == 2)
            fogFactor = saturate(exp(-(fog.z * depth) * (fog.z * depth)));
        else if (mode.z == 4)
            fogFactor = o.specular.a;
    }
    o.specular.a = fogFactor;
    o.tex0 = TexCoord(0, v, cameraPosition, cameraNormal);
    o.tex1 = TexCoord(1, v, cameraPosition, cameraNormal);
    o.tex2 = TexCoord(2, v, cameraPosition, cameraNormal);
    return o;
}
#endif

// ---- pixel uniforms

#ifdef PIXEL_STAGE
BLOCK(PixelUniforms, b0, space3)
{
    int4 colorStage[STAGES];    // op, arg1, arg2, (bound: has a texture)
    int4 alphaStage[STAGES];    // op, arg1, arg2, -
    float4 bumpMatrix[STAGES];  // BUMPENVMAT00, 01, 10, 11
    float4 textureFactor;
    float4 fogColor;
    int4 pixelFlags;            // x: fog, y: specular, z: alpha test function (1 never ... 8 always, 0 off), w: alpha ref (0-255)
};

#ifdef GLSL
uniform sampler2D texture0, texture1, texture2;
#else
VK_SAMPLER(0) Texture2D texture0 : register(t0, space2);
VK_SAMPLER(0) SamplerState sampler0 : register(s0, space2);
VK_SAMPLER(1) Texture2D texture1 : register(t1, space2);
VK_SAMPLER(1) SamplerState sampler1 : register(s1, space2);
VK_SAMPLER(2) Texture2D texture2 : register(t2, space2);
VK_SAMPLER(2) SamplerState sampler2 : register(s2, space2);
#endif

float4 Argument(int a, float4 diffuse, float4 current, float4 tex, float4 specular, float4 temp)
{
    float4 r;
    switch (a & 15) {
    case 0: r = diffuse; break;
    case 1: r = current; break;
    case 2: r = tex; break;
    case 3: r = textureFactor; break;
    case 4: r = specular; break;
    default: r = temp; break;
    }
    if ((a & 0x20) != 0)
        r = r.aaaa;
    if ((a & 0x10) != 0)
        r = 1.0 - r;
    return r;
}

float4 Operate(int op, float4 a1, float4 a2, float4 diffuse, float4 current, float4 tex)
{
    switch (op) {
    case 2: return a1;
    case 3: return a2;
    case 4: return a1 * a2;
    case 5: return a1 * a2 * 2.0;
    case 6: return a1 * a2 * 4.0;
    case 7: return a1 + a2;
    case 8: return a1 + a2 - 0.5;
    case 9: return (a1 + a2 - 0.5) * 2.0;
    case 10: return a1 - a2;
    case 11: return a1 + a2 - a1 * a2;
    case 12: return lerp(a2, a1, diffuse.a);
    case 13: return lerp(a2, a1, tex.a);
    case 14: return lerp(a2, a1, textureFactor.a);
    case 15: return a1 + a2 * (1.0 - tex.a);
    case 16: return lerp(a2, a1, current.a);
    case 18: return float4(a1.rgb + a1.a * a2.rgb, a1.a);
    case 19: return float4(a1.rgb * a2.rgb + a1.a, a1.a);
    case 20: return float4((1.0 - a1.a) * a2.rgb + a1.rgb, a1.a);
    case 21: return float4((1.0 - a1.rgb) * a2.rgb + a1.a, a1.a);
    case 24: {
        const float d = saturate(dot(a1.rgb - 0.5, a2.rgb - 0.5) * 4.0);
        return float4(d, d, d, d);
    }
    default: return current;
    }
}

bool AlphaPasses(float alpha, int func, int reference)
{
    const int a = int(floor(saturate(alpha) * 255.0 + 0.5));
    switch (func) {
    case 1: return false;
    case 2: return a < reference;
    case 3: return a == reference;
    case 4: return a <= reference;
    case 5: return a > reference;
    case 6: return a != reference;
    case 7: return a >= reference;
    default: return true;
    }
}

float4 Sample(int stage, float4 t, float2 offset)
{
    float2 uv = t.xy / (t.w != 0.0 ? t.w : 1.0) + offset;
#ifdef GLSL
    if (stage == 0)
        return texture(texture0, uv);
    if (stage == 1)
        return texture(texture1, uv);
    return texture(texture2, uv);
#else
    if (stage == 0)
        return texture0.Sample(sampler0, uv);
    if (stage == 1)
        return texture1.Sample(sampler1, uv);
    return texture2.Sample(sampler2, uv);
#endif
}

float4 PSMain(VSOut i) SEM(SV_Target)
{
    const float4 diffuse = i.diffuse;
    const float4 specular = float4(i.specular.rgb, 1);
    float4 current = diffuse;
    const float4 temp = float4(0, 0, 0, 0);
    float2 bump = float2(0, 0);
    float4 tc[STAGES];
    tc[0] = i.tex0;
    tc[1] = i.tex1;
    tc[2] = i.tex2;
    // Every stage's texture is sampled outside any branch: a sample in divergent control flow has no
    // reliable derivatives, and Vulkan's drivers then pick the smallest mip level. (An unbound stage
    // samples white.)
    bool on = true;
    UNROLL for (int s = 0; s < STAGES; ++s) {
        const int4 c = colorStage[s];
        const int4 a = alphaStage[s];
        const float4 tex = Sample(s, tc[s], bump);
        // DISABLE ends the stages; so does a stage that reads a texture it does not have (Direct3D's
        // drivers treat it as disabled).
        const bool readsTexture = (c.y & 15) == 2 || (c.z & 15) == 2 || (a.x != 1 && ((a.y & 15) == 2 || (a.z & 15) == 2));
        on = on && c.x != 1 && !(c.w == 0 && readsTexture);
        const bool bumpStage = on && (c.x == 22 || c.x == 23);
        if (on && !bumpStage) {
            const float4 colour = Operate(c.x, Argument(c.y, diffuse, current, tex, specular, temp),
                                          Argument(c.z, diffuse, current, tex, specular, temp), diffuse, current, tex);
            float alpha = current.a;
            if (a.x != 1) {
                const float4 r = Operate(a.x, Argument(a.y, diffuse, current, tex, specular, temp),
                                         Argument(a.z, diffuse, current, tex, specular, temp), diffuse, current, tex);
                alpha = r.a;
            }
            current = saturate(float4(colour.rgb, alpha));
        }
        // BUMPENVMAP: the next stage's coordinates move by the matrix times this texel's (du, dv).
        const float4 m = bumpMatrix[s];
        bump = bumpStage ? float2(m.x * tex.x + m.z * tex.y, m.y * tex.x + m.w * tex.y) : float2(0, 0);
    }
    if (pixelFlags.y != 0)
        current.rgb = saturate(current.rgb + i.specular.rgb);
    if (pixelFlags.z != 0 && !AlphaPasses(current.a, pixelFlags.z, pixelFlags.w))
        discard;
    if (pixelFlags.x != 0)
        current.rgb = lerp(fogColor.rgb, current.rgb, saturate(i.specular.a));
    return current;
}
#endif

// ---- OpenGL's entry points

#if defined(GLSL) && defined(VERTEX_STAGE)
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inDiffuse;
layout(location = 3) in vec4 inSpecular;
layout(location = 4) in vec4 inTex0;
layout(location = 5) in vec4 inTex1;
out vec4 vDiffuse, vSpecular, vTex0, vTex1, vTex2;
void main()
{
    VSIn v;
    v.position = inPosition;
    v.normal = inNormal;
    v.diffuse = inDiffuse;
    v.specular = inSpecular;
    v.tex0 = inTex0;
    v.tex1 = inTex1;
    VSOut o = VSMain(v);
    // OpenGL's clip depth is -w to w, Direct3D's 0 to w. And y turned over: OpenGL's window y runs up,
    // so this draws the back buffer with its row 0 first, as Direct3D's. An edge exactly on a pixel
    // centre (a pre-transformed quad's often is) then falls as by Direct3D's top-left rule.
    gl_Position = vec4(o.position.x, -o.position.y, 2.0 * o.position.z - o.position.w, o.position.w);
    vDiffuse = o.diffuse;
    vSpecular = o.specular;
    vTex0 = o.tex0;
    vTex1 = o.tex1;
    vTex2 = o.tex2;
}
#endif

#if defined(GLSL) && defined(PIXEL_STAGE)
in vec4 vDiffuse, vSpecular, vTex0, vTex1, vTex2;
layout(location = 0) out vec4 outColour;
void main()
{
    VSOut i;
    i.position = gl_FragCoord;
    i.diffuse = vDiffuse;
    i.specular = vSpecular;
    i.tex0 = vTex0;
    i.tex1 = vTex1;
    i.tex2 = vTex2;
    outColour = PSMain(i);
}
#endif
