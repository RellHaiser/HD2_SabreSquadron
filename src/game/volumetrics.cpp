// The volumetrics and the clouds (see volumetrics.h).
#include "game/volumetrics.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "common/chunk.h"
#include "game/game.h"
#include "game/human_movement.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

// (0x00482df0..0x00482e30: the clouds' static initialization; 0x00482140 the file's.)
S_Clouds g_clouds;                                      // 0x0089b74c
S_LitVertex *g_cloudVertices[2];                        // 0x0089b754, 0x0089b758

namespace {

// A colour of white with `alpha` (0..1 x 255, rounded to the nearest).
uint32_t Alpha(float alpha255)
{
    const int a = static_cast<int>(std::nearbyint(alpha255));
    return static_cast<uint32_t>(a) << 24 | 0xFFFFFFu;
}

// (0x00480710)
void SetVertex(S_LitVertex &v, const S_vector &pos, uint32_t color, float u, float w)
{
    v.pos = pos;
    v.color = color;
    v.u = u;
    v.v = w;
}

// The engine's inline normalization (x, z, y the order of a short vector's unit form).
void Normalize(S_vector &v, float sq)
{
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

// A frame's world position.
S_vector WorldPos(I3D_frame *frame)
{
    return frame->GetWorldPos();
}

}  // namespace

// ---- the cones ---------------------------------------------------------------------------------

// 0x00481080
S_VolumetricCone::S_VolumetricCone(float base) : texture(""), base(base)
{
    std::memset(fan, 0, sizeof fan);
    std::memset(fanUV, 0, sizeof fanUV);
    std::memset(cone, 0, sizeof cone);
    std::memset(alphas, 0, sizeof alphas);
}

// 0x00480d00
S_VolumetricCone::~S_VolumetricCone()
{
    if (image) {
        image->Release();
        image = nullptr;
    }
}

// 0x00481440
bool S_VolumetricCone::Load(C_chunk &chunk)
{
    uint16_t id = 0;
    chunk.Descend(id);
    for (;;) {
        const int left = chunk.RemainingSize();
        if (left == 0 || left == -1)
            break;
        uint16_t sub = id;
        if (chunk.Descend(sub))
            id = sub;
        switch (id) {
        case 0x3F2: {
            // (The original's buffer is not terminated: a name without its 0 ran on.)
            char text[1000];
            chunk.ReadString(text, sizeof text);
            texture = text;
            chunk.Ascend();
            break;
        }
        case 0x3FC:
            chunk.Read(angle);
            chunk.Ascend();
            break;
        case 0x406:
            chunk.Read(length);
            chunk.Ascend();
            break;
        case 0x410:
            chunk.Read(alphaNear);
            chunk.Ascend();
            break;
        case 0x41A:
            chunk.Read(alphaFar);
            chunk.Ascend();
            break;
        case 0x424: {
            int value = 0;
            chunk.Read(value);
            chunk.Ascend();
            flat = value != 0;
            break;
        }
        case 0x42E:
            chunk.Read(rotate);
            chunk.Ascend();
            break;
        case 0x438:
            chunk.Read(segments);
            chunk.Ascend();
            break;
        case 0x442:
            chunk.Read(spread);
            chunk.Ascend();
            break;
        case 0x44C:
            chunk.Read(scrollU);
            chunk.Ascend();
            break;
        case 0x456:
            chunk.Read(scrollV);
            chunk.Ascend();
            break;
        case 0x460:
            chunk.Read(uvScale);
            chunk.Ascend();
            break;
        default:
            chunk.Ascend();
            break;
        }
    }
    chunk.Ascend();
    return true;
}

// 0x00480740
void S_VolumetricCone::Build(bool load, float zBase)
{
    if (load) {
        if (image) {
            image->Release();
            image = nullptr;
        }
        if (texture.c_str()[0]) {
            // (Fix: the original prints the name as a format into 268 bytes.)
            char name[268];
            std::snprintf(name, sizeof name - 4, "%s", texture.c_str());
            if (!std::strrchr(name, '.'))
                std::strcat(name, ".bmp");
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
            GetIGraph()->LoadTexture(name, name, 0x404, &image);
#endif
        }
    }
    // (Fix: the original has room for 10 segments and does not check the count read.)
    const int count = segments < MAX_SEGMENTS ? segments : MAX_SEGMENTS;
    const float radians = angle * 0.0174533f;
    if (flat) {
        fanUV[0][0] = 0.5f;
        alphas[0][0] = alphaNear;
        fanUV[0][1] = 0.0f;
        const float half = static_cast<float>(std::tan(radians * 0.5f)) * length;
        SetVertex(fan[0], S_vector(0.0f, 0.0f, zBase), Alpha(alphaNear * 255.0f), 0.5f, 0.0f);
        alphas[0][1] = alphaFar;
        const float u1 = 0.5f - half / length * uvScale;
        fanUV[1][0] = u1;
        fanUV[1][1] = uvScale;
        SetVertex(fan[1], S_vector(half, 0.0f, length + zBase), Alpha(alphaFar * 255.0f), u1, uvScale);
        alphas[0][2] = alphaFar;
        const float u2 = half / length * uvScale + 0.5f;
        fanUV[2][0] = u2;
        fanUV[2][1] = uvScale;
        SetVertex(fan[2], S_vector(-half, 0.0f, length + zBase), Alpha(alphaFar * 255.0f), u2, uvScale);
        return;
    }
    float a = -0.5f * radians + 1.5708f;
    const float step = radians / static_cast<float>(segments);
    for (int i = 0; i < count; ++i) {
        alphas[i][0] = alphaNear;
        SetVertex(cone[i][0], S_vector(0.0f, 0.0f, zBase), Alpha(alphaNear * 255.0f), 0.5f, 0.5f);
        // (The cone's two ends open: their outer edges clear.)
        alphas[i][1] = alphaFar;
        if (segments > 1 && i == 0)
            alphas[i][1] = 0.0f;
        SetVertex(cone[i][1], S_vector(std::cos(a) * length, 0.0f, std::sin(a) * length + zBase),
                  Alpha(alphas[i][1] * 255.0f), 0.0f, 0.0f);
        alphas[i][2] = alphaFar;
        if (segments > 1 && i == segments - 1)
            alphas[i][2] = 0.0f;
        const float next = step + a;
        SetVertex(cone[i][2], S_vector(std::cos(next) * length, 0.0f, std::sin(next) * length + zBase),
                  Alpha(alphas[i][2] * 255.0f), 0.0f, 0.0f);
        a = next;
    }
}

// 0x00480390
void S_VolumetricCone::Render(int time, float phase, float fade)
{
    if (!image)
        return;
    IGraph *graph = GetIGraph();
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    graph->SetTexture(image, 0);
#endif
    if (flat) {
        const int t = static_cast<int>(base * 1000.0f + static_cast<float>(time));
        const float du = static_cast<float>(t) * scrollU * 0.001f;
        const float dv = static_cast<float>(t) * scrollV * 0.001f;
        for (int k = 3; k-- > 0;) {
            fan[k].u = du + fanUV[k][0];
            fan[k].v = dv + fanUV[k][1];
            if (!(fade < 0.0f))
                fan[k].color = Alpha(fade * alphas[0][k] * 255.0f);
        }
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->DrawPrimitives(3, 1, fan, 0);
#endif
        return;
    }
    const int count = segments < MAX_SEGMENTS ? segments : MAX_SEGMENTS;
    float a = (phase + base) * rotate;
    const float step = spread * 0.0174533f / static_cast<float>(segments);
    for (int i = 0; i < count; ++i) {
        // The rim's texture coordinates turn round the texture's middle.
        cone[i][1].u = (std::cos(a) + 1.0f) * 0.5f;
        cone[i][1].v = (std::sin(a) + 1.0f) * 0.5f;
        const float next = step + a;
        cone[i][2].u = (std::cos(next) + 1.0f) * 0.5f;
        cone[i][2].v = (std::sin(next) + 1.0f) * 0.5f;
        a = next;
        if (!(fade < 0.0f)) {
            cone[i][0].color = Alpha(fade * alphas[i][0] * 255.0f);
            cone[i][1].color = Alpha(fade * alphas[i][1] * 255.0f);
            cone[i][2].color = Alpha(fade * alphas[i][2] * 255.0f);
        }
    }
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    GetIGraph()->DrawPrimitives(3, segments, cone, 0);
#endif
}

// ---- the shafts --------------------------------------------------------------------------------

// 0x00481320
S_Volumetric::S_Volumetric(I3D_frame *frame) : frame(frame), viewAngle(RandFloat() * 6.2831855f) {}

// 0x00481380
S_Volumetric::~S_Volumetric()
{
    DeleteCones();
}

// 0x004813d0
void S_Volumetric::DeleteCones()
{
    for (size_t i = cones.size(); i-- > 0;)
        delete cones[i];
    cones.clear();
}

// 0x00481810
bool S_Volumetric::Load(C_chunk &chunk)
{
    float lightDistance = 0.0f;
    DeleteCones();
    uint16_t id;
    chunk.Descend(id);
    for (;;) {
        const int left = chunk.RemainingSize();
        if (left == 0 || left == -1 || !chunk.Descend(id))
            break;
        bool stop = false;
        switch (id) {
        case 10: {
            char text[1000];
            chunk.ReadString(text, sizeof text);
            chunk.Ascend();
            frame = GetGame()->GetScene()->FindFrame(text, FRAME_MASK_LIGHT);
            if (!frame)
                stop = true;
            else
                lightDistance = WorldPos(frame).Magnitude();
            break;
        }
        case 0x14:
            chunk.Read(depthDistance);
            chunk.Ascend();
            break;
        case 0x19:
            chunk.Read(fadeInEnd);
            chunk.Ascend();
            break;
        case 0x1C:
            chunk.Read(fadeInStart);
            chunk.Ascend();
            break;
        case 0x1E:
            chunk.Read(distanceTurn);
            chunk.Ascend();
            break;
        case 0x23:
            chunk.Read(viewTurn);
            chunk.Ascend();
            break;
        case 0x26:
            chunk.Read(base);
            chunk.Ascend();
            break;
        case 0x28: {
            // The cones, each its own chunk; one that does not load ends them.
            S_VolumetricCone *cone = nullptr;
            for (;;) {
                const int rest = chunk.RemainingSize();
                if (rest == 0 || rest == -1)
                    break;
                cone = new S_VolumetricCone(static_cast<float>(cones.size()) + lightDistance);
                if (!cone->Load(chunk))
                    break;
                cones.push_back(cone);
                cone = nullptr;
            }
            delete cone;
            chunk.Ascend();
            break;
        }
        case 0x32:
            chunk.Read(fadeOutStart);
            chunk.Ascend();
            break;
        case 0x3C:
            chunk.Read(fadeOutEnd);
            chunk.Ascend();
            break;
        default:
            chunk.Ascend();
            break;
        }
        if (stop)
            break;
    }
    chunk.Ascend();
    return true;
}

// 0x00480fa0
void S_Volumetric::Refresh(bool load)
{
    viewAngle = WorldPos(frame).Magnitude();
    for (S_VolumetricCone *cone : cones) {
        if (!load)
            cone->Build(false, base);
        else if (cone->texture.c_str()[0])
            cone->Build(true, base);
    }
}

// 0x00480d80
bool S_Volumetric::Draw(int time, const S_vector &camera, float yaw)
{
    if (!frame || !frame->IsOn())
        return false;
    // Its z axis kept, turned about it to face the camera.
    S_matrix m = frame->GetMatrix();
    const S_vector dir = m(2);
    const S_vector framePos = frame->GetWorldPos();
    const S_vector away(-(camera.x - framePos.x), -(camera.y - framePos.y), -(camera.z - framePos.z));
    m.SetDir3(dir, away);
    m.m[0][3] = 0.0f;
    m.m[1][3] = 0.0f;
    m.m[2][3] = 0.0f;
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    GetIGraph()->SetWorldMatrix(m);
    GetIGraph()->SetRenderState(0xD, depthDistance < distance);
#endif
    const float distancePhase = distanceTurn * distance;
    const float viewPhase = viewAngle * viewTurn;
    float fade = -1.0f;                                  // (under 0: the colours as they are)
    if (!(distance < fadeOutEnd))
        return false;
    if (!(distance <= fadeOutStart)) {
        fade = 1.0f - (distance - fadeOutStart) / (fadeOutEnd - fadeOutStart);
        if (fade == 0.0f)
            return false;
    } else if (fadeInEnd != 0.0f) {
        if (!(distance < fadeInEnd)) {
            fade = 1.0f;
        } else {
            if (distance <= fadeInStart)
                return false;
            fade = (distance - fadeInStart) / (fadeInEnd - fadeInStart);
            if (fade == 0.0f)
                return false;
        }
    }
    for (S_VolumetricCone *cone : cones)
        cone->Render(time, viewPhase + distancePhase + yaw, fade);
    return true;
}

// ---- the list ----------------------------------------------------------------------------------

// 0x00482d70
void C_Volumetrics::Free()
{
    FreeClouds();
    for (size_t i = m_shafts.size(); i-- > 0;)
        delete m_shafts[i];
    m_shafts.clear();
    m_yaw = 0.0f;
    m_unknown14 = 0.0f;
    m_viewDir = {1.0f, 0.0f};
}

// 0x00482a00
void C_Volumetrics::Load()
{
    Free();
    if (!GetGame())
        return;
    C_chunk chunk;
    if (!chunk.Open(C_fstr("missions\\%s\\%s", GetGame()->GetMissionName(), "volumetrics.bin"), false))
        return;
    uint16_t id;
    if (chunk.Descend(id)) {
        if (id == 1) {
            for (;;) {
                const int left = chunk.RemainingSize();
                if (left == 0 || left == -1)
                    break;
                auto *shaft = new S_Volumetric(nullptr);
                if (!shaft->Load(chunk))
                    delete shaft;
                else
                    m_shafts.push_back(shaft);
            }
        } else {
            chunk.Ascend();
            chunk.Close();
        }
    }
    Prune(true);
}

// 0x00482980
void C_Volumetrics::Prune(bool load)
{
    for (size_t i = 0; i < m_shafts.size();) {
        S_Volumetric *shaft = m_shafts[i];
        if (!shaft->frame) {
            delete shaft;
            m_shafts.erase(m_shafts.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            shaft->Refresh(load);
            ++i;
        }
    }
}

// 0x00482260
void C_Volumetrics::Render(int time)
{
    I3D_driver *driver = I3DGetDriver();
    IGraph *graph = GetIGraph();
    const bool state3 = driver->GetState(3) != 0;
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    const bool state0E = graph->GetRenderState(0xE) != 0;
    const bool state0D = graph->GetRenderState(0xD) != 0;
    GetIGraph()->SetRenderState(0xE, 0);
#else
    const bool state0E = false;
    const bool state0D = false;
#endif
    DrawClouds(time);
    I3DGetDriver()->SetState(3, 1);
    // (The original times the drawing: QueryPerformanceCounter, not used.)
    I3D_frame *camera = GetGame()->GetScene()->GetActiveCamera();
    const S_vector cameraPos = camera->GetWorldPos();
    // The camera's turn: its direction on the ground.
    S_vector forward = camera->GetMatrix()(2);
    Normalize(forward, (forward.x * forward.x + forward.y * forward.y) + forward.z * forward.z);
    S_vector flatDir(forward.x, 0.0f, forward.z);
    {
        const float sq = forward.x * forward.x + forward.z * forward.z;
        if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
            if (!(sq < 1e-8f)) {
                const float k = 1.0f / std::sqrt(sq);
                flatDir.x = forward.x * k;
                flatDir.y = k * 0.0f;
                flatDir.z = k * forward.z;
            } else if (forward.x != 0.0f) {
                flatDir.x = forward.x < 0.0f ? -1.0f : 1.0f;
            } else if (forward.z != 0.0f) {
                flatDir.z = forward.z < 0.0f ? -1.0f : 1.0f;
            } else {
                flatDir.y = 1.0f;
            }
        }
    }
    const float flat2[2] = {flatDir.x, flatDir.z};
    const float turn = AngleBetween2D(&m_viewDir.x, flat2);
    m_viewDir = {flatDir.x, flatDir.z};
    m_yaw = turn + m_yaw;
    // The shafts whose light left the scene go; those in a sector drawn this frame, in front of the
    // camera and nearer than their fade's end are visible (their turn round the viewer counted).
    for (size_t i = 0; i < m_shafts.size();) {
        S_Volumetric *shaft = m_shafts[i];
        I3D_frame *parent = shaft->frame->GetParent();
        if (!parent) {
            delete shaft;
            m_shafts.erase(m_shafts.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        shaft->visible = parent->GetType() == FRAME_SECTOR && static_cast<I3D_sector *>(parent)->IsVisible();
        if (shaft->visible) {
            const S_vector pos = shaft->frame->GetWorldPos();
            const float dx = pos.x - cameraPos.x;
            const float dz = pos.z - cameraPos.z;
            S_vector n(dx, 0.0f, dz);
            {
                const float sq = dx * dx + dz * dz;
                if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
                    if (!(sq < 1e-8f)) {
                        const float k = 1.0f / std::sqrt(sq);
                        n.x = k * dx;
                        n.y = k * 0.0f;
                        n.z = k * dz;
                    } else if (dx != 0.0f) {
                        n.x = dx < 0.0f ? -1.0f : 1.0f;
                    } else if (dz != 0.0f) {
                        n.z = dz < 0.0f ? -1.0f : 1.0f;
                    } else {
                        n.y = 1.0f;
                    }
                }
            }
            if (!((n.x * flatDir.x + n.z * flatDir.z) + n.y * flatDir.y < 0.0f)) {
                shaft->distance = S_vector(dx, 0.0f, dz).Magnitude();
                if (shaft->distance < shaft->fadeOutEnd) {
                    const float to[2] = {n.x, n.z};
                    const float viewTurn = AngleBetween2D(&shaft->viewDir.x, to);
                    shaft->viewDir = {n.x, n.z};
                    shaft->viewAngle = viewTurn + shaft->viewAngle;
                } else {
                    shaft->visible = false;
                }
            } else {
                shaft->visible = false;
            }
        }
        ++i;
    }
    // Drawn farthest first.
    for (size_t i = 0; i < m_shafts.size(); ++i) {
        if (!m_shafts[i]->visible)
            continue;
        for (size_t j = i + 1; j < m_shafts.size(); ++j) {
            if (m_shafts[j]->visible && m_shafts[i]->distance < m_shafts[j]->distance)
                std::swap(m_shafts[i], m_shafts[j]);
        }
        m_shafts[i]->Draw(time, cameraPos, m_yaw);
    }
    I3DGetDriver()->SetState(3, state3);
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    GetIGraph()->SetRenderState(0xE, state0E);
    GetIGraph()->SetRenderState(0xD, state0D);
#endif
}

// ---- the clouds --------------------------------------------------------------------------------

// 0x00483de0
void FreeClouds()
{
    for (IImage *&image : g_clouds.images) {
        if (image) {
            image->Release();
            image = nullptr;
        }
    }
    for (S_LitVertex *&vertices : g_cloudVertices) {
        delete[] vertices;
        vertices = nullptr;
    }
}

// 0x00483e50
void LoadClouds()
{
    FreeClouds();
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
    GetIGraph()->LoadTexture("clouder.tga", nullptr, 0x502, &g_clouds.images[0]);
    GetIGraph()->LoadTexture("clouder2.tga", nullptr, 0x502, &g_clouds.images[1]);
#endif
    // (A quad of 6 vertices for each of the 12 x 12 x 12 cells.)
    g_cloudVertices[0] = new S_LitVertex[0x3CC00 / sizeof(S_LitVertex)];
    g_cloudVertices[1] = new S_LitVertex[0x3CC00 / sizeof(S_LitVertex)];
}

// 0x00482e50
void DrawClouds(int)
{
    if (!g_clouds.images[0])
        return;
    I3D_frame *camera = GetGame()->GetScene()->GetActiveCamera();
    const S_vector cameraPos = camera->GetWorldPos();
    S_vector forward = camera->GetMatrix()(2);
    Normalize(forward, (forward.x * forward.x + forward.y * forward.y) + forward.z * forward.z);
    const S_vector up = camera->GetMatrix()(1);
    const int cellX = static_cast<int>(cameraPos.x * 0.0625f);
    const int cellY = static_cast<int>(cameraPos.y * 0.0625f);
    const int cellZ = static_cast<int>(cameraPos.z * 0.0625f);
    int counts[2] = {0, 0};                             // vertices: the positive sines', the others'
    for (int ix = cellX - 6; ix < cellX + 6; ++ix) {
        for (int iy = cellY - 6; iy < cellY + 6; ++iy) {
            for (int iz = cellZ - 6; iz < cellZ + 6; ++iz) {
                const float index = static_cast<float>(iz * 3 + iy * 5 + ix * 2);
                const float s = std::sin(index);
                const float size = s * 0.75f * 4.0f + 12.2f;
                const S_vector pos((static_cast<float>(ix) + s) * 16.0f, (std::cos(index) + static_cast<float>(iy)) * 16.0f,
                                   (static_cast<float>(iz) + s) * 16.0f);
                S_vector d(cameraPos.x - pos.x, cameraPos.y - pos.y, cameraPos.z - pos.z);
                const float d2 = (d.x * d.x + d.z * d.z) + d.y * d.y;
                Normalize(d, d2);
                const S_vector n(-d.x, -d.y, -d.z);
                if ((n.x * forward.x + n.z * forward.z) + n.y * forward.y < 0.0f)
                    continue;
                // In from 10 m to 20 m, out from 45 m.
                float alpha = 0.4f;
                if (!(d2 < 400.0f)) {
                    if (d2 > 2025.0f)
                        alpha = (1.0f - (d2 - 2025.0f) * 0x1.5aa2fep-12f) * 0.4f;
                } else if (!(d2 > 100.0f)) {
                    alpha = 0.0f;
                } else {
                    const float r = static_cast<float>(std::sqrt(d2) - 10.0);
                    alpha = static_cast<float>(static_cast<float>(r * 0.1) * 0.4);
                }
                if (0.0f > alpha)
                    alpha = 0.0f;
                if (1.0f < alpha)
                    alpha = 1.0f;
                if (!(alpha > 0.001f))
                    continue;
                S_matrix m;
                m.Identity();
                m.SetDir(n, up);
                m(3) = pos;
                const uint32_t color = Alpha(alpha * 255.0f);
                const int which = s <= 0.0f ? 1 : 0;
                S_LitVertex *v = g_cloudVertices[which] + counts[which];
                SetVertex(v[2], S_vector(-size, -size, 0.0f) * m, color, 0.0f, 0.0f);
                SetVertex(v[1], S_vector(size, -size, 0.0f) * m, color, 1.0f, 0.0f);
                SetVertex(v[0], S_vector(size, size, 0.0f) * m, color, 1.0f, 1.0f);
                SetVertex(v[3], S_vector(-size, size, 0.0f) * m, color, 0.0f, 1.0f);
                v[4] = v[0];
                v[5] = v[2];
                counts[which] += 6;
            }
        }
    }
    I3DGetDriver()->SetState(3, 0);
    for (int k = 0; k < 2; ++k) {
        if (counts[k] / 3 == 0)
            continue;
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        GetIGraph()->SetTexture(g_clouds.images[k], 0);
        GetIGraph()->SetWorldMatrix(IdentityMatrix());
        GetIGraph()->DrawPrimitives(3, counts[k] / 3, g_cloudVertices[k], 0);
#endif
    }
}
