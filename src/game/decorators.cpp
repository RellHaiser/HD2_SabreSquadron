// The decorators (see decorators.h).
#include "game/decorators.h"

#include <cctype>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/game.h"
#include "game/profile.h"
#include "ls3d/i3d.h"

// (0x0047ab90..0x0047ac30 and 0x0047e0c0: the file's static initialization.)
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
        if (bend->unknown18 != 0)
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
    unknown04 = 0.0f;
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
            chunk.Read(decorator->unknown04);
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
    m_unknown40 = false;
    m_unknown44 = 0;
    m_unknown3C = 0.05f;
    m_unknown48 = 0;
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
