// The decorators (see decorators.h).
#include "game/decorators.h"

#include <cctype>
#include <cstring>
#include <string>

#include <cmath>
#include <cstdlib>

#include "common/chunk.h"
#include "game/car.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/geffects.h"
#include "game/human.h"
#include "game/human_movement.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "script/script_module.h"

// (0x0047ab90..0x0047ac30, 0x0047e0c0 and 0x00480360: the file's static initialization.)
C_Decorators *g_decorators;                             // 0x0089bbb0
bool g_decoratorsEnabled;                               // 0x0089b749

namespace {

// A bend deleted, its cell told (inlined in 0x0047ea60 and the update).
void DeleteBend(S_DecoratorBend *bend)
{
    if (!bend)
        return;
    if (bend->cell) {
        bend->cell->bend = nullptr;
        if (bend->springing != 0)
            bend->cell->bending = false;
    }
    delete bend;
}

// A byte of a chunk as a flag (the original keeps the byte).
void ReadFlag(C_chunk &chunk, bool &flag)
{
    uint8_t byte = flag ? 1 : 0;
    chunk.Read(byte);
    flag = byte != 0;
}

}  // namespace

// 0x0047aed0
S_Decorator::~S_Decorator()
{
    if (mesh)
        mesh->Release();
}

// 0x0047d030
void S_Decorator::Reset()
{
    pool = nullptr;
    model = "";
    chance = 1.0f;
    bendable = true;
    randomRotation = true;
    loadFailed = false;
    height = 0.0f;
    alignToGround = false;
    animation = 0;
    amplitude = 0.2f;
    frequency = 1.0f;
    if (mesh) {
        mesh->Release();
        mesh = nullptr;
    }
    vertices.clear();
    std::vector<S_vector4>().swap(restPositions);
}

// 0x0047ae60
void S_DecoratorSet::Reset()
{
    for (size_t i = decorators.size(); i-- > 0;)
        delete decorators[i];
    decorators.clear();
    minSlope = 0.95f;
    groundCover = 0.5f;
}

// 0x0047cc90
bool S_DecoratorSet::Load(C_chunk &chunk, int version)
{
    Reset();
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    int count = 0;
    chunk.Read(count);
    if (version > 2)
        chunk.Read(minSlope);
    if (version > 8)
        chunk.Read(groundCover);
    for (int i = 0; i < count; ++i) {
        if (!chunk.Descend(id))
            return false;
        auto *decorator = new S_Decorator();
        // (The original's buffer is not terminated: a name without its 0 ran on.)
        char text[1000];
        chunk.ReadString(text, sizeof text);
        decorator->model = text;
        chunk.Ascend();
        decorators.push_back(decorator);
        chunk.Read(decorator->chance);
        ReadFlag(chunk, decorator->bendable);
        if (version > 3)
            chunk.Read(decorator->height);
        if (version > 4)
            ReadFlag(chunk, decorator->randomRotation);
        if (version > 5) {
            chunk.Read(decorator->animation);
            chunk.Read(decorator->amplitude);
        }
        if (version > 6)
            chunk.Read(decorator->frequency);
        if (version > 7)
            ReadFlag(chunk, decorator->alignToGround);
    }
    chunk.Ascend();
    return true;
}

// 0x0047ad10
S_DecoratorPool::~S_DecoratorPool()
{
    for (size_t i = models.size(); i-- > 0;) {
        if (models[i])
            models[i]->Release();
    }
}

// 0x0047e0f0
void C_DecoratorGrid::Init()
{
    if (m_sizeX <= 0)
        return;
    m_originZ = 100000;
    m_originX = 100000;
    m_cells = new S_DecoratorCell *[m_sizeX];
    for (int i = 0; i < m_sizeX; ++i)
        m_cells[i] = new S_DecoratorCell[m_sizeZ];
    m_fading = false;
    m_fadeTime = 0;
    m_fadeAlpha = 0.05f;
    m_stance = 0;
    m_firstPerson = GetGame()->IsFirstPersonView();
}

// 0x0047ea60
void C_DecoratorGrid::Free()
{
    if (m_sizeX <= 0)
        return;
    for (size_t i = m_bends.size(); i-- > 0;)
        DeleteBend(m_bends[i]);
    m_bends.clear();
    if (m_cells) {
        for (int z = 0; z < m_sizeZ; ++z) {
            for (int x = 0; x < m_sizeX; ++x) {
                if (I3D_frame *&model = m_cells[z][x].model) {
                    model->Release();
                    model = nullptr;
                }
            }
        }
        for (int i = 0; i < m_sizeX; ++i) {
            delete[] m_cells[i];
            m_cells[i] = nullptr;
        }
        delete[] m_cells;
    }
    m_cells = nullptr;
}

// (Inlined in the GEffects module's constructor, 0x004860b0: the grid 0x004862b0, the light
// map's fields 0x00486300.)
C_Decorators::C_Decorators()
{
    Clear();
}

// (Inlined in 0x00486f00.)
C_Decorators::~C_Decorators()
{
    Clear();
}

// 0x0047b010
void C_Decorators::SetOn(bool on)
{
    if (on == m_on || (!g_decoratorsEnabled && on))
        return;
    if (m_grid.m_cells) {
        for (int z = 0; z < m_grid.m_sizeZ; ++z) {
            for (int x = 0; x < m_grid.m_sizeX; ++x) {
                if (I3D_frame *model = m_grid.m_cells[z][x].model) {
                    model->SetOn(on);
                    model->Update();
                }
            }
        }
    }
    m_on = on;
}

// 0x0047c020
void C_Decorators::Clear()
{
    for (size_t i = m_animated.size(); i-- > 0;) {
        if (I3D_mesh *&mesh = m_animated[i]->mesh) {
            mesh->Release();
            mesh = nullptr;
        }
    }
    m_animated.clear();
    for (size_t i = m_sets.size(); i-- > 0;)
        delete m_sets[i];
    m_sets.clear();
    for (size_t i = m_pools.size(); i-- > 0;)
        delete m_pools[i];
    m_pools.clear();
    FreeLightMap();
}

// 0x0047b090
void C_Decorators::Free()
{
    m_grid.Free();
    Clear();
    m_on = false;
}

// 0x0047c530
void C_Decorators::Load()
{
    if (!GetGame())
        return;
    C_chunk chunk;
    if (!chunk.Open(C_fstr("missions\\%s\\%s", GetGame()->GetMissionName(), "decorations.bin"), false))
        return;
    uint16_t version;
    if (chunk.Descend(version)) {
        if (version > 1) {
            chunk.Read(m_grid.m_sizeX);
            m_grid.m_sizeZ = m_grid.m_sizeX;
            chunk.Read(m_grid.m_cellSize);
            // A material's index, its set; until the chunk's end or a set without its header.
            for (bool more = true; more;) {
                const int left = chunk.RemainingSize();
                if (left == 0 || left == -1)
                    break;
                unsigned material = 0;
                chunk.Read(material);
                while (m_sets.size() <= material)
                    m_sets.push_back(nullptr);
                auto *set = new S_DecoratorSet();
                delete m_sets[material];
                m_sets[material] = set;
                more = set->Load(chunk, version);
            }
        }
        chunk.Ascend();
    }
    chunk.Close();
    LoadLightMap();
}

// 0x0047c930
void C_Decorators::LoadLightMap()
{
    FreeLightMap();
    C_chunk chunk;
    if (!chunk.Open(C_fstr("missions\\%s\\%s", GetGame()->GetMissionName(), "decorator_sl.bin"), false))
        return;
    uint16_t id;
    chunk.Descend(id);
    chunk.Read(m_lightWidth);
    chunk.Read(m_lightHeight);
    chunk.Read(m_lightCellSize);
    chunk.Read(m_lightX);
    chunk.Read(m_lightZ);
    chunk.Read(m_unknown7C);
    chunk.Read(m_unknown80);
    const int size = m_lightHeight * m_lightWidth;
    // (The original's buffer is not cleared: a short file left garbage.)
    m_light = new uint8_t[size]();
    chunk.Read(m_light, size);
    chunk.Ascend();
    chunk.Close();
}

// 0x0047d0c0
void C_Decorators::FreeLightMap()
{
    if (m_light) {
        delete[] m_light;
        m_light = nullptr;
    }
}

// 0x0047b0b0
void C_Decorators::Start()
{
    for (size_t i = m_sets.size(); i-- > 0;) {
        S_DecoratorSet *set = m_sets[i];
        if (!set)
            continue;
        for (size_t j = set->decorators.size(); j-- > 0;) {
            S_Decorator *decorator = set->decorators[j];
            // (Fix: the original copies the name into 256 bytes unchecked.)
            std::string name = decorator->model.c_str();
            for (char &c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            bool found = false;
            for (size_t k = m_pools.size(); k-- > 0 && !found;) {
                if (std::strcmp(name.c_str(), m_pools[k]->name.c_str()) == 0) {
                    decorator->pool = m_pools[k];
                    found = true;
                }
            }
            if (!found) {
                m_pools.push_back(new S_DecoratorPool(name.c_str()));
                decorator->pool = m_pools.back();
            }
            if (decorator->animation != 0)
                m_animated.push_back(decorator);
        }
    }
    m_grid.Init();
    m_on = true;
    g_decoratorsEnabled = g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_DECORATORS);
    if (!g_decoratorsEnabled)
        SetOn(false);
}

// 0x0047b4c0
float C_Decorators::GetGroundCover(unsigned material)
{
    if (material < m_sets.size() && m_sets[material])
        return m_sets[material]->groundCover;
    return 1.0f;
}

// 0x0047b480
float C_Decorators::GetMinSlope(unsigned material)
{
    if (material < m_sets.size() && m_sets[material])
        return m_sets[material]->minSlope;
    return 1000.0f;
}

// ---- the update (part 8d-iii) ------------------------------------------------------------------

namespace {

// The light level a decorator's model was given, kept in its frame data (the bends read it).
float ModelLight(const I3D_frame *model)
{
    const S_FrameData *data = model->GetData();
    float light;
    std::memcpy(&light, &data, sizeof light);
    return light;
}
void SetModelLight(I3D_frame *model, float light)
{
    S_FrameData *data = nullptr;
    std::memcpy(&data, &light, sizeof light);
    model->SetData(data);
}

// (0x0047d0f0)
float Lerp(float a, float b, float t)
{
    return b * t + (1.0f - t) * a;
}

}  // namespace

// 0x0047d110
float C_Decorators::SampleLight(float x, float z)
{
    if (!m_light)
        return 0.0f;
    const float u = x - m_lightX;
    const float v = z - m_lightZ;
    const float inv = 1.0f / m_lightCellSize;
    const int column = static_cast<int>(inv * u);
    const int row = static_cast<int>(inv * v);
    const float fu = (u - static_cast<float>(column) * m_lightCellSize) * inv;
    const float fv = (v - static_cast<float>(row) * m_lightCellSize) * inv;
    // (Quirk kept: off the map reads outside it.)
    const uint8_t *first = m_light + row * m_lightWidth + column;
    const uint8_t *second = m_light + (row + 1) * m_lightWidth + column;
    const float b = Lerp(second[0], second[1], fu);
    const float a = Lerp(first[0], first[1], fu);
    return Lerp(a, b, fv) * 0.00390625f;
}

// 0x0047b500
I3D_frame *C_Decorators::TakeModel(unsigned set, int, int *decorator, bool *randomRotation, float *height,
                                   bool *alignToGround)
{
    if (set >= m_sets.size() || !m_sets[set])
        return nullptr;
    const float pick = RandomFloat(100.0f);
    const S_DecoratorSet *s = m_sets[set];
    int i = 0;
    for (;; ++i) {
        if (static_cast<size_t>(i) >= s->decorators.size())
            return nullptr;
        if (pick < s->decorators[i]->chance)
            break;
    }
    S_Decorator *d = s->decorators[i];
    *height = d->height;
    *randomRotation = d->randomRotation;
    *alignToGround = d->alignToGround;
    std::vector<I3D_frame *> &models = d->pool->models;
    I3D_frame *model = nullptr;
    if (!models.empty()) {
        if (models.size() > 1) {
            model = models.back();
            models.pop_back();
        } else {
            model = I3DGetDriver()->CreateFrame(FRAME_MODEL);
            static_cast<I3D_model *>(model)->DuplicateModel(static_cast<I3D_model *>(models[0]));
            model->LinkTo(GetGame()->GetScene()->GetPrimarySector(), 0);
        }
    } else if (!d->loadFailed) {
        model = I3DGetDriver()->CreateFrame(FRAME_MODEL);
        if (g_modelCache.OpenInto(static_cast<I3D_model *>(model), d->model.c_str()) != I3D_OK) {
            d->loadFailed = true;
            model->Release();
            *decorator = i;
            return nullptr;
        }
        if (d->animation != 0 && !d->mesh)
            d->PrepareAnimated(model);
        model->LinkTo(GetGame()->GetScene()->GetPrimarySector(), 0);
        // Lit by its own colours: the first face group's material (LS3DF's names: emissive 1,
        // ambient, specular and power 0).
        I3D_mesh *mesh = static_cast<I3D_visual *>(model->GetChild(0))->GetVisualMesh();
        I3D_material *material = mesh->GetLevel(0)->GetFaceGroup(0)->material;
        material->Flags() |= I3DMATERIAL_TRACE_FLAG;
        material->SetDiffuse(S_vector(1.0f, 1.0f, 1.0f));
        material->SetUnknown04(S_vector(0.0f, 0.0f, 0.0f));
        material->SetEmissive(S_vector(0.0f, 0.0f, 0.0f));
        material->SetAlpha(0.0f);
        // The pool keeps a copy as its original.
        I3D_frame *original = I3DGetDriver()->CreateFrame(FRAME_MODEL);
        models.push_back(original);
        static_cast<I3D_model *>(original)->DuplicateModel(static_cast<I3D_model *>(model));
    }
    *decorator = i;
    return model;
}

// 0x0047b8e0
void C_Decorators::ReturnModel(int set, I3D_frame *model, int decorator)
{
    m_sets[set]->decorators[decorator]->pool->models.push_back(model);
}

// 0x0047baa0
void S_Decorator::PrepareAnimated(I3D_frame *model)
{
    I3D_frame *child = model->GetChild(0);
    if (!child || child->GetVisualType() != I3DVISUAL_OBJECT)
        return;
    auto *visual = static_cast<I3D_object_visual *>(child);
    I3D_mesh *visualMesh = visual->GetVisualMesh();
    const I3D_bbox box = visual->GetLocalBound().bbox;
    const S_vector axis(box.min.x - box.max.x, 0.0f, box.min.z - box.max.z);
    for (uint32_t l = visualMesh->NumLevels(); l-- > 0;) {
        I3D_mesh_level *level = visualMesh->GetLevel(static_cast<int>(l));
        auto *base = static_cast<uint8_t *>(level->GetVertices(0));
        for (int v = level->NumVertices(); v-- > 0;) {
            auto *position = reinterpret_cast<float *>(base + v * 0x20);
            const float sq = (axis.z * axis.z + axis.y * axis.y) + axis.x * axis.x;
            bool close = sq < 1e-8f;
            if (!close) {
                // The vertex's distance from the line through the box's top corner along the axis.
                const S_vector d(box.max.x - position[0], box.max.y - position[1], box.max.z - position[2]);
                const float t = (d.z * axis.z + d.y * axis.y + d.x * axis.x) / sq;
                close = (d - axis * t).Magnitude() < 0.1f;
            }
            if (close) {
                vertices.push_back(position);
                restPositions.push_back(S_vector4{position[0], position[1], position[2], position[3]});
            }
        }
        level->UnlockVertices();
        level->Unknown2C();
        visual->UnknownAC(static_cast<int>(l));
    }
    visual->Update();
    mesh = visualMesh;
    ++*reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(visualMesh) + 0x40);   // (its reference count)
}

// 0x0047e240
I3D_frame *S_DecoratorCell::Place(int material, int seed, bool *randomRotation, float *height, bool *alignToGround)
{
    set = material;
    model = g_decorators->TakeModel(static_cast<unsigned>(material), seed, &decorator, randomRotation, height,
                                    alignToGround);
    if (model)
        model->SetOn(true);
    return model;
}

// 0x0047e290
void S_DecoratorCell::Clear()
{
    if (!model)
        return;
    model->SetOn(false);
    g_decorators->ReturnModel(set, model, decorator);
    model = nullptr;
}

// 0x0047e2d0
void C_DecoratorGrid::Refill(int z, int x)
{
    // The cell's seed from its place in the world (the same cell, the same decorator).
    const int baseX = m_originX - m_sizeX / 2 + m_shiftX;
    const int baseZ = m_originZ - m_sizeZ / 2 + m_shiftZ;
    const int worldX = baseX + x;
    float hash;
    if (worldX != 0 && baseZ + z != 0)
        hash = std::sin(static_cast<float>(baseZ + z)) * static_cast<float>(worldX);
    else
        hash = std::sin(static_cast<float>(baseZ - x + z)) * static_cast<float>(baseX - z + x);
    const int seed = static_cast<int>(std::cos(hash + m_cellSize) * 10000.0f + 123456.0f);
    std::srand(static_cast<unsigned>(seed));
    const float placeZ = ((RandomFloat(1.0f) - 0.5f) * 0.75f + static_cast<float>(baseZ + z)) * m_cellSize;
    const float placeX = ((RandomFloat(1.0f) - 0.5f) * 0.75f + static_cast<float>(worldX)) * m_cellSize;
    int column = x + m_shiftX;
    if (column >= m_sizeX)
        column -= m_sizeX;
    int row = z + m_shiftZ;
    if (row >= m_sizeZ)
        row -= m_sizeZ;
    S_vector hitPos, normal;
    const S_CollisionVolume *hit = CollisionRayTest(S_vector(placeX, 100.0f, placeZ), S_vector(0.0f, -500.0f, 0.0f), &hitPos,
                                                    &normal, 0xFFFFFFFFu, 0x210000);
    S_DecoratorCell &cell = m_cells[row][column];
    if (S_DecoratorBend *bend = cell.bend) {
        for (size_t i = m_bends.size(); i-- > 0;) {
            if (m_bends[i] == bend) {
                DeleteBend(bend);
                m_bends[i] = m_bends.back();
                m_bends.pop_back();
                break;
            }
        }
    }
    cell.bending = false;
    if (!hit) {
        cell.Clear();
        return;
    }
    const int material = GetVolumeMaterial(*hit);
    const float slope = (normal.z + normal.x) * 0.0f + normal.y;
    if (!(g_decorators->GetMinSlope(static_cast<unsigned>(material)) < slope)) {
        cell.Clear();
        return;
    }
    cell.Clear();
    bool randomRotation = true, alignToGround = false;
    float height = 0.0f;
    if (!cell.model)
        cell.Place(material, seed, &randomRotation, &height, &alignToGround);
    I3D_frame *model = cell.model;
    if (!model)
        return;
    const float light = g_decorators->SampleLight(hitPos.x, hitPos.z) * 0.5f;
    GetGEffects()->SetModelLightLevel(model, light);
    SetModelLight(model, light);
    if (height != 0.0f)
        hitPos.y = height;
    model->SetPosInline(hitPos);
    if (!alignToGround) {
        if (randomRotation) {
            const float dx = RandomFloat(2.0f) - 1.0f;
            const float dz = RandomFloat(2.0f) - 1.0f;
            model->SetWorldDir(S_vector(dx, 0.0f, dz), 0.0f);
        }
    } else {
        const float roll = randomRotation ? RandomFloat(6.2831855f) : 0.0f;
        model->SetWorldDir(normal, roll);
    }
    const float scale = (RandomFloat(2.0f) - 1.0f) * 0.2f + 0.8f;
    model->SetScale(S_vector(scale, scale, scale));
    model->Update();
}

// 0x0047e930
void C_DecoratorGrid::RefillAll()
{
    m_shiftX = 0;
    m_shiftZ = 0;
    for (int x = 0; x < m_sizeX; ++x)
        RefillColumn(x);
}

// 0x0047ea00
void C_DecoratorGrid::RefillColumn(int x)
{
    for (int z = 0; z < m_sizeZ; ++z)
        Refill(z, x);
}

// 0x0047ea30
void C_DecoratorGrid::RefillRow(int z)
{
    for (int x = 0; x < m_sizeX; ++x)
        Refill(z, x);
}

// 0x0047e960
void C_DecoratorGrid::ShiftX(bool back)
{
    if (!back) {
        if (++m_shiftX >= m_sizeX) {
            m_shiftX = 0;
            m_originX += m_sizeX;
        }
    } else if (--m_shiftX < 0) {
        m_shiftX = m_sizeX - 1;
        m_originX -= m_sizeX;
    }
}

// 0x0047e9b0
void C_DecoratorGrid::ShiftZ(bool back)
{
    if (!back) {
        if (++m_shiftZ >= m_sizeZ) {
            m_shiftZ = 0;
            m_originZ += m_sizeZ;
        }
    } else if (--m_shiftZ < 0) {
        m_shiftZ = m_sizeZ - 1;
        m_originZ -= m_sizeZ;
    }
}

// 0x0047eb80
void C_DecoratorGrid::Follow(float x, float z)
{
    if (m_sizeX < 1)
        return;
    const float inv = 1.0f / m_cellSize;
    int cellX = static_cast<int>(x * inv);
    int cellZ = static_cast<int>(z * inv);
    if (x < 0.0f)
        --cellX;
    if (z < 0.0f)
        --cellZ;
    const int dx = cellX - m_originX - m_shiftX;
    const int dz = cellZ - m_shiftZ - m_originZ;
    const int stepsX = std::abs(dx);
    const int stepsZ = std::abs(dz);
    // (The parts of a cell gone through: x by the reciprocal, the others by a division.)
    const float partX = x - static_cast<float>(cellX) * m_cellSize;
    const float partZ = z - static_cast<float>(cellZ) * m_cellSize;
    if (stepsX >= m_sizeX / 2 || stepsZ >= m_sizeZ / 2) {
        // Far off: all anew.
        m_subX = static_cast<int>(partX * inv * static_cast<float>(m_sizeZ));
        m_subZ = static_cast<int>(partZ * inv * static_cast<float>(m_sizeX));
        m_originX = cellX;
        m_originZ = cellZ;
        RefillAll();
        return;
    }
    // In the same cell: the next column's / row's cells refilled as far as the viewer got.
    if (m_originX + m_shiftX == cellX) {
        const int n = static_cast<int>(partX * inv * static_cast<float>(m_sizeZ));
        if (n > m_subX) {
            for (int i = m_subX + 1; i <= n; ++i)
                Refill(i, m_sizeX - 1);
            m_subX = n;
        } else if (n < m_subX) {
            for (int i = n + 1; i <= m_subX; ++i)
                Refill(i, 0);
            m_subX = n;
        }
    }
    if (m_shiftZ + m_originZ == cellZ) {
        const int n = static_cast<int>(partZ / m_cellSize * static_cast<float>(m_sizeX));
        if (n > m_subZ) {
            for (int i = m_subZ + 1; i <= n; ++i)
                Refill(m_sizeZ - 1, i);
            m_subZ = n;
        } else if (n < m_subZ) {
            for (int i = n + 1; i <= m_subZ; ++i)
                Refill(0, i);
            m_subZ = n;
        }
    }
    // Into other cells: the column finished, the window shifted, the new columns filled.
    if (m_shiftX + m_originX != cellX) {
        if (dx > 0) {
            for (int i = m_subX + 1; i < m_sizeZ; ++i)
                Refill(i, m_sizeX - 1);
        } else if (dx < 0) {
            const int last = m_subX;
            for (int i = 0; i <= last; ++i)
                Refill(i, 0);
        }
        if (stepsX != 0) {
            if (dx > 0) {
                for (int n = stepsX;;) {
                    --n;
                    ShiftX(false);
                    if (n == 0)
                        break;
                    RefillColumn(m_sizeX - 1);
                }
                Refill(m_sizeZ - 1, m_subZ);
                if (m_subZ > 0)
                    Refill(m_sizeZ - 1, m_subZ - 1);
            } else if (dx < 0) {
                for (int n = stepsX; n != 0;) {
                    --n;
                    ShiftX(true);
                    if (n != 0)
                        RefillColumn(0);
                    if (m_subZ < m_sizeX - 1)
                        Refill(0, m_subZ + 1);
                }
            }
            const int n = static_cast<int>(partX / m_cellSize * static_cast<float>(m_sizeZ));
            if (dx > 0) {
                for (int i = 0; i <= n; ++i)
                    Refill(i, m_sizeX - 1);
                m_subX = n;
            }
            if (dx < 0) {
                for (int i = n + 1; i < m_sizeZ; ++i)
                    Refill(i, 0);
                m_subX = n;
            }
        }
    }
    if (m_shiftZ + m_originZ != cellZ) {
        if (dz > 0) {
            for (int i = m_subZ + 1; i < m_sizeX; ++i)
                Refill(m_sizeZ - 1, i);
        } else if (dz < 0) {
            const int last = m_subZ;
            for (int i = 0; i <= last; ++i)
                Refill(0, i);
        }
        if (stepsZ != 0) {
            if (dz > 0) {
                for (int n = stepsZ;;) {
                    --n;
                    ShiftZ(false);
                    if (n == 0)
                        break;
                    RefillRow(m_sizeZ - 1);
                }
                Refill(m_subX, m_sizeX - 1);
                if (m_subX > 0)
                    Refill(m_subX - 1, m_sizeX - 1);
            } else if (dz < 0) {
                for (int n = stepsZ; n != 0;) {
                    --n;
                    ShiftZ(true);
                    if (n != 0)
                        RefillRow(0);
                    if (m_subX < m_sizeZ - 1)
                        Refill(m_subX + 1, 0);
                }
            }
            const int n = static_cast<int>(partZ / m_cellSize * static_cast<float>(m_sizeX));
            if (dz > 0) {
                for (int i = 0; i <= n; ++i)
                    Refill(m_sizeZ - 1, i);
                m_subZ = n;
            }
            if (dz < 0) {
                for (int i = n + 1; i < m_sizeX; ++i)
                    Refill(0, i);
                m_subZ = n;
            }
        }
    }
    // The bends: the player's soldier on foot, the cars over the grid.
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    if (player && !player->GetVehicle())
        BendAround(player);
    if (g_cars.empty())
        return;
    const float centreX = m_cellSize * static_cast<float>(m_shiftX + m_originX);
    const float centreZ = static_cast<float>(m_shiftZ + m_originZ) * m_cellSize;
    const float reach = static_cast<float>(m_sizeX) * m_cellSize * 0.5f;
    const float reach2 = reach * reach;
    for (size_t i = g_cars.size(); i-- > 0;) {
        C_Car *car = g_cars[i];
        const S_vector pos = car->GetPosition();
        const float ox = pos.x - centreX;
        const float oz = pos.z - centreZ;
        if (ox * ox + oz * oz < reach2)
            BendAround(car);
    }
}

// 0x0047f4e0
void C_DecoratorGrid::BendAround(C_Human *human)
{
    const S_vector pos = human->GetFrame()->GetWorldPos();
    const S_vector velocity = human->GetVelocity();
    const int stance = human->GetStance();
    // Into or out of the crouch (and a view change while crouching): the alpha fades over 1 s.
    if (stance != m_stance && (stance == 2 || m_stance == 2)) {
        m_fading = true;
        m_fadeTime = 1000;
    }
    m_stance = stance;
    if (GetGame()->IsFirstPersonView() != m_firstPerson && stance == 2)
        m_fading = true;
    const bool firstPerson = GetGame()->IsFirstPersonView();
    m_firstPerson = firstPerson;
    const bool fade = stance == 2 || m_fading;
    const float sq = (velocity.y * velocity.y + velocity.z * velocity.z) + velocity.x * velocity.x;
    float dirX = 0.0f, dirZ = 0.0f;
    if (m_fading) {
        m_fadeTime -= m_time;
        if (m_fadeTime < 0) {
            m_fadeTime = 0;
            m_fading = false;
        }
        float alpha = static_cast<float>(m_fadeTime) * 0.001f;
        if (m_stance != 2)
            alpha = 1.0f - alpha;
        m_fadeAlpha = alpha + 0.05f;
        if (!firstPerson)
            m_fadeAlpha = 1.0f;
        if (!(m_fadeAlpha <= 1.0f))
            m_fadeAlpha = 1.0f;
        if (sq > 1e-8f) {
            const float k = 1.0f / std::sqrt(sq);
            dirX = velocity.x * k;
            dirZ = velocity.z * k;
        }
    } else {
        if (sq < 1e-8f)
            return;
        const float k = 1.0f / std::sqrt(sq);
        dirX = velocity.x * k;
        dirZ = velocity.z * k;
    }
    float inner, outer;
    if (stance == 0) {
        inner = 0.25f;
        outer = 0.4f;
    } else if (stance == 2) {
        inner = 0.75f;
        outer = 1.0f;
    } else {
        inner = 0.3f;
        outer = 0.5f;
    }
    Bend(pos, dirX, dirZ, inner * inner, outer * outer, 3.0f, 0.9f, fade);
}

// 0x0047f730
void C_DecoratorGrid::BendAround(C_Car *car)
{
    if (car->GetSpeed() < 0.1f)
        return;
    const C_Vehicle &vehicle = *car->GetVehicle();
    const int wheels = vehicle.m_wheelCount;
    if (vehicle.m_tracksR.empty() && vehicle.m_tracksL.empty()) {
        // Each wheel, a bit towards its axle's other one, along its rolling direction.
        for (int i = wheels; i-- > 0;) {
            const S_VehicleWheel *wheel = vehicle.m_wheels[i];
            const S_vector4 &other = (i % 2 == 0 ? vehicle.m_wheels[i + 1] : vehicle.m_wheels[i - 1])->worldPos;
            S_vector d(other.x - wheel->unknown050.x, other.y - wheel->unknown050.y, other.z - wheel->unknown050.z);
            const float sq = (d.z * d.z + d.y * d.y) + d.x * d.x;
            if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
                if (!(sq < 1e-8f)) {
                    const float k = 1.0f / std::sqrt(sq);
                    d.x *= k;
                    d.y *= k;
                    d.z *= k;
                } else if (d.x != 0.0f) {
                    d.x = d.x < 0.0f ? -1.0f : 1.0f;
                } else if (d.z != 0.0f) {
                    d.z = d.z < 0.0f ? -1.0f : 1.0f;
                } else {
                    d.y = d.y < 0.0f ? -1.0f : 1.0f;
                }
            }
            const S_vector pos(d.x * 0.2f + wheel->unknown050.x, d.y * 0.2f + wheel->unknown050.y,
                               d.z * 0.2f + wheel->unknown050.z);
            Bend(pos, wheel->unknown0B0.x, wheel->unknown0B0.z, 0.25f, 0.36f, 0.8f, 0.75f, false);
        }
        return;
    }
    // Tracks: the middle of each axle, along the car's velocity.
    const S_vector &velocity = car->GetVelocity();
    for (int i = 0; i < wheels; i += 2) {
        const S_vector4 &a = vehicle.m_wheels[i]->unknown050;
        const S_vector4 &b = vehicle.m_wheels[i + 1]->unknown050;
        const S_vector pos((b.x + a.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);
        Bend(pos, velocity.x, velocity.z, 5.76f, 6.25f, 2.6f, 0.75f, false);
    }
}

// 0x0047fae0
void C_DecoratorGrid::Bend(const S_vector &pos, float dirX, float dirZ, float innerSq, float outerSq, float radius,
                           float darken, bool fade)
{
    const float inv = 1.0f / m_cellSize;
    const int reach = static_cast<int>(radius * inv);
    int cellX = static_cast<int>(inv * pos.x);
    int cellZ = static_cast<int>(inv * pos.z);
    if (pos.x < 0.0f)
        --cellX;
    if (pos.z < 0.0f)
        --cellZ;
    // The window's cells (the shift already in: the ring buffer's index).
    const int centreX = m_sizeX / 2 - m_originX + cellX;
    const int centreZ = m_sizeZ / 2 - m_originZ + cellZ;
    for (int i = centreX - reach; i < centreX + reach; ++i) {
        for (int j = centreZ - reach; j < centreZ + reach; ++j) {
            int column = i < 0 ? m_sizeX + i : i;
            int row = j < 0 ? m_sizeZ + j : j;
            if (column >= m_sizeX)
                column -= m_sizeX;
            if (row >= m_sizeZ)
                row -= m_sizeZ;
            if (column < 0 || row < 0 || column >= m_sizeX || row >= m_sizeZ)
                continue;
            S_DecoratorCell &cell = m_cells[row][column];
            if (cell.decorator == -1 || static_cast<unsigned>(cell.set) >= g_decorators->m_sets.size())
                continue;
            const S_DecoratorSet *set = g_decorators->m_sets[cell.set];
            if (!set || static_cast<unsigned>(cell.decorator) >= set->decorators.size() ||
                !set->decorators[cell.decorator]->bendable)
                continue;
            const bool springing = cell.bend && cell.bend->springing != 0;
            I3D_frame *model = cell.model;
            if (!model)
                continue;
            if (fade) {
                float alpha = static_cast<float>(std::abs(j - centreZ) + std::abs(i - centreX)) /
                                  static_cast<float>(reach * 2) + m_fadeAlpha;
                if (!(alpha <= 1.0f))
                    alpha = 1.0f;
                static_cast<I3D_object_visual *>(model->GetChild(0))->SetAlpha(alpha);
            }
            if (cell.bending && !springing)
                continue;
            const S_vector &modelPos = model->GetWorldPos();
            const float ox = pos.x - modelPos.x;
            const float oy = pos.y - modelPos.y;
            const float oz = pos.z - modelPos.z;
            const float distance2 = (oz * oz + oy * oy) + ox * ox;
            if (!(distance2 < outerSq))
                continue;
            S_DecoratorBend *bend;
            if (springing) {
                bend = cell.bend;
                bend->speed = 1.0f;
            } else {
                bend = new S_DecoratorBend();
                bend->speed = 1.0f;
                bend->angle = 0.0f;
                bend->model = cell.model;
                bend->cell = &cell;
                cell.bend = bend;
                m_bends.push_back(bend);
            }
            bend->target = 1.2566371f;
            // Away from the move: the model's z axis (x and z each normalized on their own) against it.
            const S_matrix &world = model->GetMatrix();
            float axisZ = world.m[2][2];
            {
                const float ax = world.m[2][0], ay = world.m[2][1];
                const float sq = (ay * ay + axisZ * axisZ) + ax * ax;
                if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
                    if (!(sq < 1e-8f))
                        axisZ = axisZ * (1.0f / std::sqrt(sq));
                    else if (ax == 0.0f && axisZ != 0.0f)
                        axisZ = axisZ < 0.0f ? -1.0f : 1.0f;
                }
            }
            float axisX = world.m[2][0];
            {
                const float ay = world.m[2][1], az = world.m[2][2];
                const float sq = (ay * ay + az * az) + axisX * axisX;
                if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
                    if (!(sq < 1e-8f))
                        axisX = axisX * (1.0f / std::sqrt(sq));
                    else if (axisX != 0.0f)
                        axisX = axisX < 0.0f ? -1.0f : 1.0f;
                }
            }
            const float axis[2] = {axisX, axisZ};
            const float move[2] = {dirX, dirZ};
            if (AngleBetween2D(axis, move) > 0.0f)
                bend->target = bend->target * -1.0f;
            cell.bending = true;
            if (distance2 < innerSq) {
                // Close: bent over and darkened.
                bend->springing = 0;
                bend->light = ModelLight(bend->model);
                bend->lightTarget = bend->light * darken;
            } else {
                // Further: a small swing.
                bend->springing = 1;
                bend->target = bend->target * 0.1f;
                bend->target = (RandomFloat(0.3f) + 1.0f) * bend->target;
            }
        }
    }
}

// 0x0047f150
void C_DecoratorGrid::UpdateBends(int time)
{
    for (size_t i = m_bends.size(); i-- > 0;) {
        if (!m_bends[i]->Update(time)) {
            DeleteBend(m_bends[i]);
            m_bends[i] = m_bends.back();
            m_bends.pop_back();
        }
    }
}

// 0x0047f200
bool S_DecoratorBend::Update(int time)
{
    const float rate = springing != 0 ? 0.3f : 3.0f;
    const float t = static_cast<float>(time);
    float step = t * speed * rate * 0.001f;
    const bool up = !(target <= angle);
    if (!up)
        step = -step;
    angle = step + angle;
    bool going = true;
    if (up ? angle >= target : angle <= target) {
        going = false;
        angle = target;
    }
    if (springing != 0 && !going) {
        // Swung back the other way, less far, until under a degree.
        if (!(static_cast<double>(std::fabs(target)) <= 0x1.1df46aaaaaaabp-6)) {
            target = target * -0.9f;
            going = true;
            speed = speed * 0.9f;
        } else {
            angle = 0.0f;
        }
    }
    // Rolled about its own z axis.
    bool normalize;
    S_vector dir = model->GetLocalDirUnnormalized(&normalize);
    if (normalize) {
        const float sq = (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x;
        if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
            if (!(sq < 1e-8f)) {
                const float k = 1.0f / std::sqrt(sq);
                dir.x = dir.x * k;
                dir.y = dir.y * k;
                dir.z = dir.z * k;
            } else if (dir.x != 0.0f) {
                dir.x = dir.x < 0.0f ? -1.0f : 1.0f;
            } else if (dir.z != 0.0f) {
                dir.z = dir.z < 0.0f ? -1.0f : 1.0f;
            } else {
                dir.y = dir.y < 0.0f ? -1.0f : 1.0f;
            }
        }
    }
    model->SetWorldDir(dir, angle);
    model->Update();
    if (springing == 0 && !(light <= lightTarget)) {
        light = light - t * 0.0003f;
        if (light < lightTarget)
            light = lightTarget;
        model->SetUnknown50(light);
        return true;
    }
    return going;
}

// 0x0047bcc0
void C_Decorators::Update(int timeDelta)
{
    const bool enabled = g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_DECORATORS);
    if (!g_decoratorsEnabled) {
        if (enabled) {
            g_decoratorsEnabled = true;
            SetOn(true);
        }
    } else if (!enabled) {
        g_decoratorsEnabled = false;
        SetOn(false);
    }
    if (!m_on || m_sets.empty())
        return;
    // The animated decorators sway: their swaying vertices moved from rest by the time's sine.
    const uint32_t time = GetGame()->GetTime();
    for (size_t i = m_animated.size(); i-- > 0;) {
        S_Decorator *d = m_animated[i];
        I3D_mesh *mesh = d->mesh;
        if (!mesh)
            continue;
        for (uint32_t l = mesh->NumLevels(); l-- > 0;)
            mesh->GetLevel(static_cast<int>(l))->GetVertices(0);
        const float phase = static_cast<float>(time) * d->frequency * 0.001f + static_cast<float>(i);
        const float s = std::sin(phase) * d->amplitude;
        const float c = std::cos(phase) * s;
        for (size_t v = d->vertices.size(); v-- > 0;) {
            const S_vector4 &rest = d->restPositions[v];
            float *position = d->vertices[v];
            // (Quirk kept: the height from y twice, not from the length.)
            if (d->animation == 1) {
                position[0] = rest.x;
                position[1] = std::sqrt((rest.y * rest.y + rest.y * rest.y) - s * s);
                position[2] = s + rest.z;
            } else if (d->animation == 2) {
                position[0] = rest.x;
                position[1] = std::sqrt((rest.y * rest.y + rest.y * rest.y) - c * c);
                position[2] = c + rest.z;
            }
        }
        for (uint32_t l = mesh->NumLevels(); l-- > 0;) {
            I3D_mesh_level *level = mesh->GetLevel(static_cast<int>(l));
            level->UnlockVertices();
            level->Unknown2C();
        }
        if (d->pool && !d->pool->models.empty())
            static_cast<I3D_object_visual *>(d->pool->models[0]->GetChild(0))->UnknownAC(0);
    }
    // The grid around the camera, or the player's soldier (not in a cutscene, nor the tactical view).
    S_vector centre = GetGame()->GetScene()->GetActiveCamera()->GetWorldPos();
    if (!g_scriptModule.IsCutsceneRunning() && !g_scriptModule.IsFakeCutscene() &&
        g_teamManager.GetPlayerTeam()->GetActivePlayer() && !GetCameraModule()->GetHumanEffects()->IsTactical())
        centre = g_teamManager.GetPlayerTeam()->GetActivePlayer()->GetFrame()->GetWorldPos();
    m_grid.m_time = timeDelta;
    m_grid.Follow(centre.x, centre.z);
    m_grid.UpdateBends(timeDelta);
}
