// The vehicles' tyre and surface tables (original 0x004d6900 LoadTyres, 0x004d6bf0 a tyre surface's
// decoding, 0x004ec980 the tyre types' copy, 0x004eccd0 FreeTyres, 0x004d57a0 InitCarSurfaces,
// 0x004ecd60 FillCarSurfaces, 0x004ecde0 ClearCarSurfaces; 0x004df230, 0x004df520, 0x004dfc40,
// 0x004fb7f0, 0x004fca40, 0x004fca80 and two empty functions are their std::vector code).
#include "game/car_tables.h"

#include <algorithm>
#include <cstring>

#include "game/shots.h"                                 // g_materials
#include "ls3d/rw_data.h"

std::vector<S_TyreType> g_tyreTypes;                    // 0x0089c174
std::vector<S_CarSurface> g_carSurfaces;                // 0x0089c160
int g_carSurfaceOfMaterial[256];                        // 0x0089bd60

namespace {

// A tyre surface's 12 floats as tables\tyres.bin keeps them.
void DecodeTyreSurface(const float *v, S_TyreSurface &surface)                 // 0x004d6bf0
{
    surface.unknown04 = v[0] * 1000.0f;
    surface.unknown08 = v[1] * 0.017453292f;
    surface.unknown0C = v[2] * 0.017453292f;
    surface.unknown10 = v[3];
    surface.unknown14 = v[4];
    surface.unknown18 = v[5];
    surface.unknown1C = v[6];
    surface.unknown20 = v[7];
    surface.unknown24 = v[8] * 1000.0f;
    surface.unknown28 = v[9];
    surface.unknown2C = v[10];
    surface.unknown30 = v[11];
}

// Each type's table released, the last first; the list emptied.
void DeleteTables(std::vector<S_TyreType> &types)
{
    for (size_t i = types.size(); i-- > 0;)
        delete types[i].surfaces;
    types.clear();
}

// g_tyreTypes made again: "develop" (id 0, no table), then each of `types` with a copy of its table.
bool SetTyreTypes(const std::vector<S_TyreType> &types)                       // 0x004ec980
{
    FreeTyres();
    S_TyreType develop = {};
    std::strcpy(develop.name, "develop");
    g_tyreTypes.push_back(develop);
    for (const S_TyreType &type : types) {
        g_tyreTypes.push_back(type);
        auto *surfaces = new std::vector<S_TyreSurface>;
        g_tyreTypes.back().surfaces = surfaces;
        // Fix: a type without a table (the original reads through its null pointer).
        if (type.surfaces)
            *surfaces = *type.surfaces;
    }
    return true;
}

// g_carSurfaces from `surfaces`, each material's entry into g_carSurfaceOfMaterial (true).
bool FillCarSurfaces(const std::vector<S_CarSurface> &surfaces)                // 0x004ecd60
{
    ClearCarSurfaces();
    // Quirk kept: the last entry (255) is left as it was.
    std::fill(g_carSurfaceOfMaterial, g_carSurfaceOfMaterial + 255, 0);
    for (size_t i = 0; i < surfaces.size(); ++i) {
        g_carSurfaces.push_back(surfaces[i]);
        // Fix: a material past the table's 256 (the original writes past it).
        if (static_cast<uint32_t>(surfaces[i].material) < 256)
            g_carSurfaceOfMaterial[surfaces[i].material] = static_cast<int>(i);
    }
    return true;
}

}  // namespace

bool LoadTyres()                                                                // 0x004d6900
{
    std::vector<S_TyreType> types;
    SetTyreTypes(types);                                // "develop" alone until the file is read
    const int handle = dtaOpen("tables\\tyres.bin", 0);
    if (handle == -1)
        return false;
    int version = 0;
    dtaRead(handle, &version, 4);
    if (version != 1) {
        dtaClose(handle);
        return false;
    }
    int count = 0;
    dtaRead(handle, &count, 4);
    while (count-- != 0) {
        // A type: its record's size (unused), name, id, the table's entries.
        S_TyreType type = {};
        int size = 0;
        dtaRead(handle, &size, 4);
        dtaRead(handle, type.name, sizeof type.name);
        dtaRead(handle, &type.id, 4);
        int entries = 0;
        dtaRead(handle, &entries, 4);
        while (entries-- != 0) {
            // An entry: the surface, the size of its values, the values. (The original returns false
            // when the values' buffer cannot be had.)
            S_TyreSurface surface = {};
            if (!type.surfaces)
                type.surfaces = new std::vector<S_TyreSurface>;
            dtaRead(handle, &surface.surface, 4);
            uint32_t bytes = 0;
            dtaRead(handle, &bytes, 4);
            // Fix: room for the 12 floats DecodeTyreSurface reads (the original reads past a
            // shorter buffer; the shipped file's are 13).
            std::vector<float> values(std::max<size_t>((bytes + 3) / 4, 12), 0.0f);
            dtaRead(handle, values.data(), static_cast<int>(bytes));
            DecodeTyreSurface(values.data(), surface);
            type.surfaces->push_back(surface);
        }
        types.push_back(type);
    }
    dtaClose(handle);
    SetTyreTypes(types);
    DeleteTables(types);
    return true;
}

void FreeTyres()                                                                // 0x004eccd0
{
    DeleteTables(g_tyreTypes);
}

bool InitCarSurfaces()                                                          // 0x004d57a0
{
    std::vector<S_CarSurface> surfaces;
    if (g_materialCount == 0) {
        // Without materials one entry of defaults.
        S_CarSurface surface = {};
        surface.unknown04 = surface.unknown08 = surface.unknown0C = surface.unknown10 = 100.0f;
        surface.friction = 0.35f;
        surface.bounce = 1.0f;
        surfaces.push_back(surface);
    } else {
        for (uint32_t i = 0; i < g_materialCount; ++i) {
            const S_MaterialDef &m = g_materials[i];
            S_CarSurface surface = {};
            surface.material = static_cast<int>(i);
            surface.friction = m.carFriction;
            surface.bounce = m.carBounce;
            surface.unknown08 = m.carUnknownE0;
            surface.unknown04 = m.carUnknownE4;
            surface.unknown10 = m.carUnknownE8;
            surface.unknown0C = m.carUnknownEC;
            surface.unknown14 = m.carUnknownF0;
            surface.dustEffect = m.carDustEffect;
            surface.skidEffect = m.carSkidEffect;
            surface.contactEffect = m.carContactEffect;
            if (surface.friction < 0.0f)
                surface.friction = 0.0f;
            else if (surface.friction > 1.0f)
                surface.friction = 1.0f;
            if (surface.bounce < 1.0f)
                surface.bounce = 1.0f;
            else if (surface.bounce > 2.0f)
                surface.bounce = 2.0f;
            if (surface.unknown04 > surface.unknown08)
                surface.unknown04 = surface.unknown08;
            if (surface.unknown0C > surface.unknown10)
                surface.unknown0C = surface.unknown10;
            if (surface.unknown14 < 0.0f)
                surface.unknown14 = 0.0f;
            else if (surface.unknown14 > 0.2f)
                surface.unknown14 = 0.2f;
            // The hard grounds (roads, rock, concrete, paving): squealing, skid marks.
            switch (i) {
            case 0x30:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x36:
            case 0x37:
            case 0x40:
            case 0x41:
            case 0x5F:
                surface.tyreSqueal = surface.skidMarks = 1;
                break;
            default:
                break;
            }
            surfaces.push_back(surface);
        }
    }
    FillCarSurfaces(surfaces);
    return true;
}

void ClearCarSurfaces()                                                         // 0x004ecde0
{
    g_carSurfaces.clear();
}
