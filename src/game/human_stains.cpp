// See human_stains.h.
#include "game/human_stains.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_weapon_pose.h"
#include "game/items.h"
#include "game/pending.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

#include "game/stain_images.inc"

extern int g_missionVariant;                           // 0x0098a6b0

C_StainManager g_stainManager;

namespace {

const uint16_t CK_STAINS = 0xB10D;
const uint32_t TEXTURE_FLAT = 0x200004;
const uint32_t TEXTURE_ALPHA = 0x200084;
const int DRIVER_STATE_LOADING = 0xD;                   // (off while the textures load)
const char MODEL_CHILD[] = "a";                         // 0x00839100
const uint32_t VISUAL_SINGLEMESH = 0x800;
const int OVERLAY_TEXELS = 0x1000;

// The slots not named in the engine's headers, on a material: its image and how it is used.
using MaterialImageSlot = void(__stdcall *)(I3D_material *, IImage *);
using MaterialModeSlot = void(__stdcall *)(I3D_material *, int);
void **VTable(const void *object) { return *reinterpret_cast<void **const *>(object); }
void SetMaterialImage(I3D_material *material, IImage *image)
{
    reinterpret_cast<MaterialImageSlot>(VTable(material)[0x2C / 4])(material, image);
    reinterpret_cast<MaterialModeSlot>(VTable(material)[0x30 / 4])(material, 6);
    reinterpret_cast<MaterialModeSlot>(VTable(material)[0x34 / 4])(material, 5);
}
// On the mesh level: makes the UV array of +0x4C and readies the level's use of it (0x3C, 0x38).
float *LevelUVs(I3D_mesh_level *level) { return *reinterpret_cast<float **>(reinterpret_cast<uint8_t *>(level) + 0x4C); }
uint32_t &LevelFlags(I3D_mesh_level *level) { return *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(level) + 100); }
void LevelMakeUVs(I3D_mesh_level *level, int sets)
{
    reinterpret_cast<void(__stdcall *)(I3D_mesh_level *, int)>(VTable(level)[0x3C / 4])(level, sets);
}
void LevelSetDetail(I3D_mesh_level *level)
{
    reinterpret_cast<void(__stdcall *)(I3D_mesh_level *, int, int, int)>(VTable(level)[0x38 / 4])(level, 0, 0, 0);
}
// On the visual: its own copy of the mesh (0xA0, the mesh's 0x18), the mesh's positions in the current pose (0xBC), done with changing it (0xC4).
const float *VisualPositions(I3D_singlemesh_visual *visual)
{
    return reinterpret_cast<const float *(__stdcall *)(I3D_singlemesh_visual *, int)>(VTable(visual)[0xBC / 4])(visual, 0);
}
void VisualRefresh(I3D_singlemesh_visual *visual)
{
    reinterpret_cast<void(__stdcall *)(I3D_singlemesh_visual *)>(VTable(visual)[0xC4 / 4])(visual);
}

I3D_mesh_level *LevelOfVisual(I3D_frame *visual)
{
    if (!visual || !(static_cast<I3D_visual *>(visual)->VisualFlags() & VISUAL_SINGLEMESH))
        return nullptr;
    return static_cast<I3D_visual *>(visual)->GetVisualMesh()->GetLevel(0);
}

// 4444 pictures: one painted over the other, each channel weighted by the alphas, the larger alpha kept.
uint16_t BlendTexels(uint16_t over, uint16_t under)
{
    const unsigned overAlpha = over >> 12, underAlpha = under >> 12;
    const unsigned sum = underAlpha + overAlpha;
    if (sum == 0)
        return 0;
    const unsigned alpha = overAlpha > underAlpha ? overAlpha : underAlpha;
    const unsigned r = ((under & 0xF00u) * underAlpha + (over & 0xF00u) * overAlpha) / sum;
    const unsigned g = ((under & 0x0F0u) * underAlpha + (over & 0x0F0u) * overAlpha) / sum;
    const unsigned b = ((under & 0x00Fu) * underAlpha + (over & 0x00Fu) * overAlpha) / sum;
    return static_cast<uint16_t>((alpha << 12) | (r & 0xF00u) | (g & 0x0F0u) | (b & 0x00Fu));
}

void BlendOverlay(uint16_t *buffer, const uint16_t *overlay)
{
    for (int i = 0; i < OVERLAY_TEXELS; ++i)
        buffer[i] = BlendTexels(overlay[i], buffer[i]);
}

float SquaredDistance(const float *position, const float *point)
{
    return (position[0] - point[0]) * (position[0] - point[0]) + (position[1] - point[1]) * (position[1] - point[1])
        + (position[2] - point[2]) * (position[2] - point[2]);
}

S_StainVertex *FindVertex(const S_StainGroup &group, uint16_t index)    // 0x00797eb0
{
    for (size_t i = group.vertices.size(); i-- != 0;)
        if (group.vertices[i]->index == index)
            return group.vertices[i];
    return nullptr;
}

// (The original keeps the last match going down: the first in the list.)
S_StainEdge *FindEdge(const S_StainVertex &vertex, uint16_t other)
{
    S_StainEdge *found = nullptr;
    for (size_t i = vertex.edges.size(); i-- != 0;)
        if (vertex.edges[i]->a->index == other || vertex.edges[i]->b->index == other)
            found = vertex.edges[i];
    return found;
}

// (0x00797f00)
void AddEdge(S_StainGroup &group, uint16_t a, uint16_t b, int face)
{
    S_StainVertex *vertexA = FindVertex(group, a);
    S_StainVertex *vertexB = FindVertex(group, b);
    S_StainEdge *edge = nullptr;
    if (!vertexA) {
        vertexA = new S_StainVertex;
        vertexA->index = a;
        group.vertices.push_back(vertexA);
    } else {
        edge = FindEdge(*vertexA, b);
        if (edge && edge->face != face && edge->otherFace < 0)
            edge->otherFace = static_cast<int16_t>(face);
    }
    bool create = false;
    if (!vertexB) {
        vertexB = new S_StainVertex;
        vertexB->index = b;
        group.vertices.push_back(vertexB);
    } else {
        S_StainEdge *const fromB = FindEdge(*vertexB, a);
        if (fromB)
            edge = fromB;
        if (!edge)
            create = true;
        else if (edge->face != face && edge->otherFace < 0)
            edge->otherFace = static_cast<int16_t>(face);
    }
    if (edge && !create) {
        if (edge->otherFace < 0)
            edge->otherFace = static_cast<int16_t>(face);
        return;
    }
    edge = new S_StainEdge{vertexA, vertexB, static_cast<int16_t>(face), -1};
    vertexA->edges.push_back(edge);
    vertexB->edges.push_back(edge);
    group.edges.push_back(edge);
}

// (0x00797790; the vertices are 0x20 bytes apart)
void FindExtremes(S_StainGroup &group, const uint8_t *vertices)
{
    group.minX = group.maxX = group.minZ = group.maxZ = group.minY = group.maxY = 0;
    if (group.vertices.empty())                         // (fix: the original reads the first of none)
        return;
    const float *const first = reinterpret_cast<const float *>(vertices + group.vertices[0]->index * 0x20u);
    float minX = first[0], maxX = first[0], minZ = first[2], maxZ = first[2], minY = first[1], maxY = first[1];
    for (unsigned i = 0; i < group.vertices.size(); ++i) {
        const float *const p = reinterpret_cast<const float *>(vertices + group.vertices[i]->index * 0x20u);
        if (p[0] < minX) { minX = p[0]; group.minX = i; }
        if (maxX < p[0]) { maxX = p[0]; group.maxX = i; }
        if (p[2] < minZ) { minZ = p[2]; group.minZ = i; }
        if (maxZ < p[2]) { maxZ = p[2]; group.maxZ = i; }
        if (p[1] < minY) { minY = p[1]; group.minY = i; }
        if (maxY < p[1]) { maxY = p[1]; group.maxY = i; }
    }
}

// (0x00797480)
void BuildMesh(S_StainMesh &mesh, I3D_mesh_level *level)
{
    const uint8_t *const vertices = static_cast<const uint8_t *>(level->GetVertices(0));
    const int numVertices = level->NumVertices();
    for (int g = 0; g < STAIN_GROUPS; ++g) {
        S_StainGroup &group = mesh.groups[g];
        I3D_face_group *const faceGroup = level->GetFaceGroup(g);
        const uint16_t *const faces = faceGroup->Lock(0);
        const int numFaces = faceGroup->numFaces;
        for (int face = 0; face < numFaces; ++face) {
            const uint16_t *const f = faces + face * 3;
            AddEdge(group, f[0], f[1], face);
            AddEdge(group, f[1], f[2], face);
            AddEdge(group, f[2], f[0], face);
        }
        // Vertices at one place are joined.
        for (int i = static_cast<int>(group.vertices.size()) - 1; i >= 0; --i) {
            const uint16_t index = group.vertices[i]->index;
            const float *const p = reinterpret_cast<const float *>(vertices + index * 0x20u);
            for (int j = i - 1; j >= 0; --j) {
                const uint16_t other = group.vertices[j]->index;
                const float *const q = reinterpret_cast<const float *>(vertices + other * 0x20u);
                const float dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2];
                if (dy * dy + dx * dx + dz * dz <= 1e-8f)
                    AddEdge(group, index, other, 0);
            }
        }
        group.numFaces = numFaces;
        group.numVertices = numVertices;
        group.index = g;
        FindExtremes(group, vertices);
    }
    level->UnlockVertices();
}

// Down the edges to a vertex nearer `point`, each step the nearest neighbour not yet tried on this walk (0x007982a0). `start` null: a random one.
const S_StainVertex *WalkToNearest(const float *point, const float *positions, const S_StainVertex *start, uint8_t *visited, const S_StainGroup &group)
{
    const S_StainVertex *current = start;
    if (!current)
        current = group.vertices[static_cast<unsigned>(std::rand()) % group.vertices.size()];
    ++visited[current->index];
    float best = SquaredDistance(positions + current->index * 4u, point);
    for (;;) {
        const S_StainVertex *next = nullptr;
        for (size_t i = current->edges.size(); i-- != 0;) {
            const S_StainEdge *const edge = current->edges[i];
            const S_StainVertex *const other = edge->a == current ? edge->b : edge->a;
            if (visited[other->index] == visited[current->index])
                continue;
            const float distance = SquaredDistance(positions + other->index * 4u, point);
            if (distance <= best) {
                best = distance;
                next = other;
            }
        }
        if (!next)
            return current;
        visited[next->index] = visited[current->index];
        current = next;
    }
}

// The walk along every edge that keeps closer than where it was two steps back (0x00798470).
const S_StainVertex *SearchNearest(const float *point, const float *positions, const S_StainVertex *current, uint8_t *visited, float last, float beforeLast, float limit)
{
    ++visited[current->index];
    float best = SquaredDistance(positions + current->index * 4u, point);
    const S_StainVertex *nearest = current;
    for (size_t i = current->edges.size(); i-- != 0;) {
        const S_StainEdge *const edge = current->edges[i];
        const S_StainVertex *const other = edge->a == current ? edge->b : edge->a;
        if (visited[other->index] == visited[current->index])
            continue;
        const float distance = SquaredDistance(positions + other->index * 4u, point);
        if (distance < limit) {
            const S_StainVertex *const found = SearchNearest(point, positions, other, visited, distance, last, beforeLast);
            const float foundDistance = SquaredDistance(positions + found->index * 4u, point);
            if (foundDistance < best) {
                nearest = found;
                best = foundDistance;
            }
        }
    }
    return nearest;
}

// Four walks from random vertices, the best of them (0x00797a80). Fix: the marks start at 0 (the original's are whatever the new block holds).
const S_StainVertex *NearestByWalks(const S_StainMesh &mesh, const float *point, const float *positions, int groupIndex)
{
    const S_StainGroup &group = mesh.groups[groupIndex];
    if (group.vertices.empty())                         // (fix: the original divides by none)
        return nullptr;
    std::vector<uint8_t> visited(static_cast<size_t>(group.numVertices));
    const S_StainVertex *best = nullptr;
    float bestDistance = 1e8f;
    for (int i = 0; i < 4; ++i) {
        const S_StainVertex *const found = WalkToNearest(point, positions, nullptr, visited.data(), group);
        if (found) {
            const float distance = SquaredDistance(positions + found->index * 4u, point);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = found;
            }
        }
    }
    return best;
}

// Searches from the group's four extremes (0x00797be0).
const S_StainVertex *NearestFromExtremes(const S_StainMesh &mesh, const float *point, const float *positions, int groupIndex)
{
    const S_StainGroup &group = mesh.groups[groupIndex];
    if (group.vertices.empty())                         // (fix: as above)
        return nullptr;
    std::vector<uint8_t> visited(static_cast<size_t>(group.numVertices));
    const unsigned starts[4] = {group.minX, group.maxX, group.maxY, group.minY};
    const S_StainVertex *best = nullptr;
    float bestDistance = 1e8f;
    for (const unsigned start : starts) {
        std::fill(visited.begin(), visited.end(), static_cast<uint8_t>(0));
        const S_StainVertex *const found = SearchNearest(point, positions, group.vertices[start], visited.data(), 1e8f, 1e8f, 1e8f);
        const float distance = SquaredDistance(positions + found->index * 4u, point);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = found;
        }
    }
    return best;
}

// As the game's copies of Normalized compile here: (x² + z²) + y².
void NormalizeXZY(float &x, float &y, float &z)
{
    const float sq = x * x + z * z + y * y;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return;
    if (sq >= 1e-8f) {
        const float k = 1.0f / std::sqrt(sq);
        x *= k; y *= k; z *= k;
    } else if (x != 0.0f) {
        x = x >= 0.0f ? 1.0f : -1.0f;
    } else if (z != 0.0f) {
        z = z >= 0.0f ? 1.0f : -1.0f;
    } else {
        y = y >= 0.0f ? 1.0f : -1.0f;
    }
}

}  // namespace

S_StainGroup::~S_StainGroup()                           // (0x007972f0)
{
    for (size_t i = vertices.size(); i-- != 0;)
        delete vertices[i];
    for (size_t i = edges.size(); i-- != 0;)
        delete edges[i];
}

// ---- the manager ---------------------------------------------------------------------------------------------------------------

C_StainManager::~C_StainManager()
{
    if (m_initialized)
        Done();
}

void C_StainManager::Init(int)
{
    if (m_initialized)
        return;
    I3D_driver *const driver = I3DGetDriver();
    const uint32_t state = driver->GetState(DRIVER_STATE_LOADING);
    driver->SetState(DRIVER_STATE_LOADING, 0);

    if (m_null)
        m_null->Release();
    m_null = nullptr;
    GetIGraph()->LoadTexture("stainNull.bmp", "stainNullA.bmp", TEXTURE_FLAT, &m_null);
    if (m_head)
        m_head->Release();
    m_head = nullptr;
    GetIGraph()->LoadTexture("hshadow.bmp", "hshadowA.bmp", TEXTURE_ALPHA, &m_head);
    for (IImage *&paint : m_paint) {
        if (paint) {
            paint->Release();
            paint = nullptr;
        }
    }
    static const char *const PAINT_FILES[2] = {"camouflage.bmp", "camouflage2.bmp"};
    static const char *const PAINT_ALPHAS[4] = {"camouflageA.bmp", "camouflageB.bmp", "camouflageC.bmp", "camouflageD.bmp"};
    for (int file = 0; file < 2; ++file)
        for (int alpha = 0; alpha < 4; ++alpha)
            GetIGraph()->LoadTexture(PAINT_FILES[file], PAINT_ALPHAS[alpha], TEXTURE_ALPHA, &m_paint[file * 4 + alpha]);

    for (int body = 0; body < STAIN_LENT_BODIES; ++body) {
        if (m_borrowers[body])
            m_borrowers[body]->ReturnTextures();
        m_borrowers[body] = nullptr;
        for (IImage *&lent : m_lent[body]) {
            if (lent)
                lent->Release();
            lent = nullptr;
            GetIGraph()->LoadTexture("stainNull.bmp", "stainNullA.bmp", TEXTURE_FLAT, &lent);
        }
    }
    driver->SetState(DRIVER_STATE_LOADING, state);

    for (size_t i = m_meshes.size(); i-- != 0;)
        delete m_meshes[i];
    m_meshes.clear();
    m_paintCounter = 0;
    m_initialized = true;
}

void C_StainManager::Reset()
{
    Init(0);
}

void C_StainManager::Done()
{
    if (!m_initialized)
        return;
    for (size_t i = m_meshes.size(); i-- != 0;) {
        delete m_meshes[i];
        m_meshes[i] = nullptr;
    }
    m_meshes.clear();
    if (m_null) {
        m_null->Release();
        m_null = nullptr;
    }
    if (m_head) {
        m_head->Release();
        m_head = nullptr;
    }
    for (IImage *&paint : m_paint) {
        if (paint) {
            paint->Release();
            paint = nullptr;
        }
    }
    for (int body = 0; body < STAIN_LENT_BODIES; ++body) {
        m_borrowers[body] = nullptr;
        for (IImage *&lent : m_lent[body]) {
            if (lent) {
                lent->Release();
                lent = nullptr;
            }
        }
    }
    m_initialized = false;
}

S_StainMesh *C_StainManager::AcquireMesh(I3D_frame *model)
{
    if (!model)
        return nullptr;
    const char *const name = static_cast<I3D_model *>(model)->GetFileName();
    for (size_t i = m_meshes.size(); i-- != 0;) {
        if (!std::strcmp(m_meshes[i]->name, name)) {
            ++m_meshes[i]->refCount;
            return m_meshes[i];
        }
    }
    I3D_mesh_level *const level = LevelOfVisual(model->FindChildFrame(MODEL_CHILD, 0xFFFF));
    if (!level)
        return nullptr;
    S_StainMesh *const mesh = new S_StainMesh;
    mesh->name = new char[std::strlen(name) + 1];
    std::strcpy(mesh->name, name);
    BuildMesh(*mesh, level);
    ++mesh->refCount;
    m_meshes.push_back(mesh);
    return mesh;
}

void C_StainManager::ReleaseMesh(S_StainMesh *mesh)
{
    for (size_t i = m_meshes.size(); i-- != 0;) {
        if (m_meshes[i] != mesh)
            continue;
        if (--mesh->refCount == 0) {
            delete mesh;
            m_meshes[i] = m_meshes.back();
            m_meshes.pop_back();
        }
        return;
    }
}

IImage **C_StainManager::AcquireTextures(C_HumanStains *body)
{
    if (!body)
        return nullptr;
    for (int i = 0; i < STAIN_LENT_BODIES; ++i)
        if (m_borrowers[i] == body)
            return m_lent[i];
    C_HumanStains *oldest = nullptr;
    int oldestIndex = 0;
    uint32_t oldestTime = 0xFFFFFFFF;
    for (int i = 0; i < STAIN_LENT_BODIES; ++i) {
        C_HumanStains *const other = m_borrowers[i];
        if (!other) {
            m_borrowers[i] = body;
            return m_lent[i];
        }
        if (other->LastHitTime() < oldestTime) {
            oldest = other;
            oldestTime = other->LastHitTime();
            oldestIndex = i;
        }
    }
    if (!oldest)
        return nullptr;
    oldest->ReturnTextures();
    m_borrowers[oldestIndex] = body;
    return m_lent[oldestIndex];
}

void C_StainManager::ReleaseTextures(const C_HumanStains *body)
{
    for (C_HumanStains *&borrower : m_borrowers) {
        if (borrower == body) {
            borrower = nullptr;
            return;
        }
    }
}

// ---- a body ---------------------------------------------------------------------------------------------------------------------

C_HumanStains::C_HumanStains(C_HumanWeaponPose *pose) : m_pose(pose) {}

C_HumanStains::~C_HumanStains()
{
    for (uint16_t *&buffer : m_buffers) {
        delete[] buffer;
        buffer = nullptr;
    }
    delete[] m_clean;
    m_clean = nullptr;
    if (m_ownTextures)
        g_stainManager.ReleaseTextures(this);
    if (m_mesh)
        g_stainManager.ReleaseMesh(m_mesh);
}

void C_HumanStains::Bind(char mode)
{
    if (mode == 1) {
        if (m_ownTextures) {
            m_ownTextures = false;
            TakeTextures();
        }
        return;
    }
    if (mode != 0 && mode != 2)
        return;
    if (mode == 2 && m_mesh) {
        g_stainManager.ReleaseMesh(m_mesh);
        m_mesh = nullptr;
    }
    I3D_frame *const model = m_pose->m_model;
    if (!m_mesh)
        m_mesh = g_stainManager.AcquireMesh(model);
    I3D_frame *const child = model->FindChildFrame(MODEL_CHILD, 0xFFFF);
    m_visual = static_cast<I3D_singlemesh_visual *>(child);
    m_level = LevelOfVisual(child);
    if (m_visual) {
        reinterpret_cast<I3D_object_visual *>(m_visual)->DuplicateMesh();
        m_visual->GetVisualMesh()->DuplicateMaterials();
    }
    if (!m_level)
        return;
    const uint8_t *const vertices = static_cast<const uint8_t *>(m_level->GetVertices(0));
    const int numVertices = m_level->NumVertices();
    if (!LevelUVs(m_level)) {
        LevelMakeUVs(m_level, 2);
        float *const uvs = LevelUVs(m_level);
        for (int i = 0; i < numVertices; ++i) {
            const float *const uv = reinterpret_cast<const float *>(vertices + i * 0x20 + 0x18);
            uvs[i * 2] = uv[0];
            uvs[i * 2 + 1] = uv[1];
        }
    }
    LevelSetDetail(m_level);
    std::memset(m_painted, 0, sizeof(m_painted));
    m_lastHitTime = 0;
    PrepareBuffers();

    // Soldiers of side 1, in some missions, are painted, one of four by turns.
    const int variant = m_pose->m_owner->GetRole() == 1 ? g_missionVariant : 0;
    unsigned paint = 0;
    if (variant == 1)
        paint = (++g_stainManager.m_paintCounter & 3u) + 1;
    else if (variant == 2)
        paint = (++g_stainManager.m_paintCounter & 3u) + 5;
    SetPaint(paint);

    m_visible = false;
    C_ItemContainer *const container = m_pose->m_owner->GetContainer(0);
    C_Item *const item = container ? container->GetItem() : nullptr;   // (container checked: the fix, the original's without it crashes)
    if (item)
        SetVisible(item->GetWeight() > 0.1f);
    uint32_t &flags = LevelFlags(m_level);
    flags |= 8;
    flags = (flags & ~2u) | 5;
    VisualRefresh(m_visual);
}

IImage *C_HumanStains::BaseTexture(int group) const
{
    if (group != 0)
        return g_stainManager.NullTexture();
    if (m_paint)
        return g_stainManager.PaintTexture(m_paint - 1);
    return m_visible ? g_stainManager.HeadTexture() : g_stainManager.NullTexture();
}

void C_HumanStains::PrepareBuffers()
{
    if (m_ownTextures)
        GiveBackTextures();
    IImage *const null = g_stainManager.NullTexture();
    if (!m_level || !null)
        return;
    const uint8_t *const image = reinterpret_cast<const uint8_t *>(null);
    const unsigned width = *reinterpret_cast<const uint16_t *>(image + 0x20);
    const unsigned height = *reinterpret_cast<const uint16_t *>(image + 0x22);
    m_width = static_cast<int>(width);
    m_height = static_cast<int>(height);
    m_maskX = m_width - 1;
    m_maskY = m_height - 1;
    m_shiftX = 0;
    for (unsigned rest = width >> 1; rest; rest >>= 1)
        ++m_shiftX;
    m_shiftY = 0;
    for (unsigned rest = height >> 1; rest; rest >>= 1)
        ++m_shiftY;
    if (!g_stainManager.HeadTexture() || !g_stainManager.PaintTexture(m_paint ? m_paint - 1 : 0))
        return;
    const size_t texels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    for (int g = 0; g < STAIN_GROUPS; ++g) {
        I3D_face_group *const faceGroup = m_level->GetFaceGroup(g);
        faceGroup->DuplicateMaterial();
        if (!m_buffers[g])
            m_buffers[g] = new uint16_t[texels];
        const uint16_t *source = nullptr;
        if (g == 0) {
            if (m_paint)
                source = STAIN_OVERLAYS[m_paint];
            else if (m_visible)
                source = STAIN_OVERLAYS[0];
        }
        if (source)
            std::memcpy(m_buffers[g], source, (texels < OVERLAY_TEXELS ? texels : OVERLAY_TEXELS) * 2);
        else
            std::memset(m_buffers[g], 0, texels * 2);
        SetMaterialImage(faceGroup->material, BaseTexture(g));
    }
    if (!m_clean)
        m_clean = new uint16_t[texels];
    std::memset(m_clean, 0, texels * 2);
    m_ownTextures = false;
}

void C_HumanStains::TakeTextures()
{
    if (m_ownTextures || !m_level)
        return;
    IImage **const lent = g_stainManager.AcquireTextures(this);
    if (!lent)
        return;
    for (int g = 0; g < STAIN_GROUPS; ++g) {
        I3D_face_group *const faceGroup = m_level->GetFaceGroup(g);
        if (!faceGroup || !m_buffers[g])
            return;
        m_textures[g] = lent[g];
        int pitch = 0;
        if (void *const pixels = m_textures[g]->Lock(0, &pitch, 0)) {
            std::memcpy(pixels, m_buffers[g], static_cast<size_t>(m_height) * static_cast<size_t>(pitch));
            m_textures[g]->Unlock();
        }
        SetMaterialImage(faceGroup->material, m_textures[g]);
    }
    m_ownTextures = true;
}

void C_HumanStains::PutBaseTextures()
{
    if (!g_stainManager.NullTexture() || !g_stainManager.HeadTexture() || !g_stainManager.PaintTexture(m_paint ? m_paint - 1 : 0))
        return;
    for (int g = 0; g < STAIN_GROUPS; ++g) {
        I3D_face_group *const faceGroup = m_level->GetFaceGroup(g);
        if (!faceGroup)
            return;
        SetMaterialImage(faceGroup->material, BaseTexture(g));
        m_textures[g] = nullptr;
    }
    m_ownTextures = false;
}

void C_HumanStains::GiveBackTextures()
{
    if (!m_ownTextures || !m_level)
        return;
    g_stainManager.ReleaseTextures(this);
    PutBaseTextures();
}

void C_HumanStains::ReturnTextures()
{
    PutBaseTextures();
}

void C_HumanStains::ShowBuffer0()
{
    if (!m_ownTextures) {
        SetMaterialImage(m_level->GetFaceGroup(0)->material, BaseTexture(0));
    } else if (m_textures[0]) {
        int pitch = 0;
        if (void *const pixels = m_textures[0]->Lock(0, &pitch, 0)) {
            std::memcpy(pixels, m_buffers[0], static_cast<size_t>(m_height) * static_cast<size_t>(pitch));
            m_textures[0]->Unlock();
        }
    }
}

void C_HumanStains::SetVisible(bool on)
{
    if (m_paint != 0 || on == m_visible)
        return;
    if (on) {
        if (!m_buffers[0])
            return;
        BlendOverlay(m_buffers[0], STAIN_OVERLAYS[0]);
    } else if (m_buffers[0] && m_clean) {
        std::memcpy(m_buffers[0], m_clean, static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 2);
    }
    m_visible = on;
    ShowBuffer0();
}

void C_HumanStains::SetPaint(unsigned paint)
{
    const uint8_t value = static_cast<uint8_t>(paint);
    if (m_paint == value)
        return;
    const size_t bytes = static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 2;
    if (value == 0) {
        if (m_buffers[0] && m_clean)
            std::memcpy(m_buffers[0], m_clean, bytes);
    } else {
        if (!m_buffers[0])
            return;
        if (m_clean)
            std::memcpy(m_buffers[0], m_clean, bytes);
        BlendOverlay(m_buffers[0], STAIN_OVERLAYS[value]);
    }
    m_paint = value;
    ShowBuffer0();
}

void C_HumanStains::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CK_STAINS);
    const uint32_t own = m_ownTextures, visible = m_visible, paint = m_paint;
    chunk.Write(own);
    chunk.Write(visible);
    chunk.Write(paint);
    chunk.Write(m_width);
    chunk.Write(m_height);
    chunk.Write(m_maskX);
    chunk.Write(m_maskY);
    chunk.Write(m_shiftX);
    chunk.Write(m_shiftY);
    chunk.Write(m_lastHitTime);
    const int bytes = m_width * m_height * 2;
    for (int g = 0; g < STAIN_GROUPS; ++g) {
        const uint32_t painted = m_painted[g];
        chunk.Write(painted);
        if (painted)
            chunk.Write(m_buffers[g], bytes);
    }
    if (m_painted[0])
        chunk.Write(m_clean, bytes);
    chunk.Ascend();
}

void C_HumanStains::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.Descend(id))
        return;
    if (id != CK_STAINS) {
        chunk.Ascend();
        return;
    }
    uint32_t value = 0;
    chunk.Read(value);
    m_ownTextures = value != 0;
    chunk.Read(value);
    m_visible = value != 0;
    chunk.Read(value);
    m_paint = static_cast<uint8_t>(value);
    chunk.Read(m_width);
    chunk.Read(m_height);
    chunk.Read(m_maskX);
    chunk.Read(m_maskY);
    chunk.Read(m_shiftX);
    chunk.Read(m_shiftY);
    chunk.Read(m_lastHitTime);
    const int bytes = m_width * m_height * 2;
    for (int g = 0; g < STAIN_GROUPS; ++g) {
        value = 0;
        chunk.Read(value);
        m_painted[g] = value != 0;
        if (value && m_buffers[g] && chunk.RemainingSize() >= bytes)
            chunk.Read(m_buffers[g], bytes);
    }
    if (m_painted[0] && m_clean && chunk.RemainingSize() >= bytes)
        chunk.Read(m_clean, bytes);
    chunk.Ascend();
}

void C_HumanStains::SkipSaved(C_chunk &chunk)
{
    uint16_t id;
    if (chunk.Descend(id))
        chunk.Ascend();
}

void C_HumanStains::PaintSplat(uint16_t *buffer, uint16_t *texture, int16_t x, int16_t y, const uint16_t *image, uint16_t width, uint16_t height)
{
    const uint16_t maskX = static_cast<uint16_t>(m_maskX), maskY = static_cast<uint16_t>(m_maskY);
    uint16_t row = static_cast<uint16_t>(y - (height >> 1));
    for (unsigned j = 0; j < height; ++j, ++row) {
        uint16_t column = static_cast<uint16_t>(x - (width >> 1));
        for (unsigned i = 0; i < width; ++i, ++column, ++image) {
            // Only what falls inside (the sign bits of a position before the edge count as outside too).
            if ((row & static_cast<uint16_t>(~maskY)) != 0 || (column & static_cast<uint16_t>(~maskX)) != 0)
                continue;
            const int index = ((static_cast<int16_t>(row) & m_maskY) << m_shiftX) + (static_cast<int16_t>(column) & m_maskX);
            buffer[index] = BlendTexels(*image, buffer[index]);
        }
    }
    if (texture)
        std::memcpy(texture, buffer, static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 2);
}

void C_HumanStains::PaintHit(const S_StainVertex &vertex, const S_vector &point, const float *positions, int group)
{
    const float *const uvs = LevelUVs(m_level);
    float u, v;
    if (vertex.edges.size() > 1) {
        // Along two edges of the vertex, as if they were the texture's axes there.
        const float *const here = positions + vertex.index * 4u;
        const S_StainEdge *const edge1 = vertex.edges[0];
        const S_StainEdge *const edge2 = vertex.edges[1];
        const uint16_t index1 = (edge1->a == &vertex ? edge1->b : edge1->a)->index;
        const uint16_t index2 = (edge2->a == &vertex ? edge2->b : edge2->a)->index;
        const float *const there1 = positions + index1 * 4u;
        const float *const there2 = positions + index2 * 4u;
        float ax = there1[0] - here[0], ay = there1[1] - here[1], az = there1[2] - here[2];
        float bx = there2[0] - here[0], by = there2[1] - here[1], bz = there2[2] - here[2];
        const float du1 = uvs[index1 * 2] - uvs[vertex.index * 2], dv1 = uvs[index1 * 2 + 1] - uvs[vertex.index * 2 + 1];
        const float du2 = uvs[index2 * 2] - uvs[vertex.index * 2], dv2 = uvs[index2 * 2 + 1] - uvs[vertex.index * 2 + 1];
        NormalizeXZY(ax, ay, az);
        NormalizeXZY(bx, by, bz);
        const float px = point.x - here[0], py = point.y - here[1], pz = point.z - here[2];
        const float along1 = az * pz + ay * py + ax * px;
        const float along2 = bx * px + by * py + bz * pz;
        u = along2 * du2 + along1 * du1 + uvs[vertex.index * 2];
        v = along2 * dv2 + along1 * dv1 + uvs[vertex.index * 2 + 1];
    } else {
        u = uvs[vertex.index * 2];
        v = uvs[vertex.index * 2 + 1];
    }
    m_lastHitTime = GetGame()->GetTime();
    if (!m_buffers[group])
        return;
    m_painted[group] = 1;
    if (!m_ownTextures)
        TakeTextures();
    IImage *const texture = m_textures[group];
    uint16_t *pixels = nullptr;
    if (texture) {
        int pitch = 0;
        pixels = static_cast<uint16_t *>(texture->Lock(0, &pitch, 0));
        // (Kept: another pitch leaves it locked and paints nothing.)
        if (pitch != m_width * 2)
            return;
    }
    const uint16_t *const splat = STAIN_SPLATS[std::rand() % 11];
    const int16_t y = static_cast<int16_t>(static_cast<int>(static_cast<float>(m_height) * v));
    const int16_t x = static_cast<int16_t>(static_cast<int>(static_cast<float>(m_width) * u));
    PaintSplat(m_buffers[group], pixels, x, y, splat, 16, 16);
    if (group == 0)
        PaintSplat(m_clean, nullptr, x, y, splat, 16, 16);
    if (texture)
        texture->Unlock();
}

void C_HumanStains::AddHit(const S_vector &pos, const S_vector &dir, uint32_t flags)
{
    if (flags == 0 || !m_mesh || !m_visual || !m_level)
        return;
    if (!m_level->GetVertices(0))
        return;
    const float *const positions = VisualPositions(m_visual);
    const S_matrix &world = m_visual->GetMatrix();
    const S_vector offset = pos - world(3);
    const float point[3] = {
        offset.x * world.m[0][0] + offset.y * world.m[0][1] + offset.z * world.m[0][2],
        offset.x * world.m[1][0] + offset.y * world.m[1][1] + offset.z * world.m[1][2],
        offset.x * world.m[2][0] + offset.y * world.m[2][1] + offset.z * world.m[2][2],
    };
    const S_vector localPoint(point[0], point[1], point[2]);
    const float front = world.m[2][0] * dir.x + world.m[2][2] * dir.z + world.m[2][1] * dir.y;
    const float side = world.m[0][0] * dir.x + world.m[0][2] * dir.z + world.m[0][1] * dir.y;

    enum : uint32_t { FROM_NEG_FRONT = 0x40, FROM_POS_FRONT = 0x80, FROM_POS_SIDE = 0x100, FROM_NEG_SIDE = 0x200 };
    // (Sign tests of the floats as the original's.)
    uint32_t zone = flags;
    if (std::fabs(front) <= std::fabs(side))
        zone |= std::signbit(side) ? FROM_NEG_SIDE : FROM_POS_SIDE;
    else
        zone |= std::signbit(front) ? FROM_NEG_FRONT : FROM_POS_FRONT;

    int group1 = -1, group2 = -1;
    switch (zone & 0x3F) {
    case 1:
        group1 = 0;
        break;
    case 2:
        group1 = 5;
        break;
    case 4:
        group1 = 6;
        break;
    case 8:
        if (zone & FROM_NEG_FRONT) {
            group1 = 1;
        } else if (zone & FROM_POS_FRONT) {
            group1 = 2;
        } else if (zone & (FROM_POS_SIDE | FROM_NEG_SIDE)) {
            group1 = 1;
            group2 = 2;
        }
        break;
    case 0x10:
    case 0x20:
        if (zone & FROM_NEG_FRONT) {
            group1 = 3;
        } else if (zone & FROM_POS_FRONT) {
            group1 = 4;
        } else if (zone & (FROM_POS_SIDE | FROM_NEG_SIDE)) {
            group1 = 3;
            group2 = 4;
        }
        break;
    default:
        break;
    }
    if (group1 >= 0) {
        const S_StainVertex *const first = group1 == 0 ? NearestByWalks(*m_mesh, point, positions, 0) : NearestFromExtremes(*m_mesh, point, positions, group1);
        const S_StainVertex *const second = group2 >= 0 ? NearestFromExtremes(*m_mesh, point, positions, group2) : nullptr;
        if (first)
            PaintHit(*first, localPoint, positions, group1);
        if (second)
            PaintHit(*second, localPoint, positions, group2);
    }
    m_level->UnlockVertices();
}
