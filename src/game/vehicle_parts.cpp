// C_Vehicle: the damage parts and the lamps from the model (original 0x004fa520 AddVehiclePart,
// 0x004faea0 AddToPartGroup, 0x004fe6b0 SetupParts, 0x004e7450 SetupLights, 0x004e87c0 CreateLamp,
// 0x004e8d80 CreateProjector).
#include "game/vehicle.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

uint32_t __stdcall CollectFrame(I3D_frame *frame, void *list)                  // 0x004f9cc0
{
    static_cast<std::vector<I3D_frame *> *>(list)->push_back(frame);
    return 0;
}

// The inverse of `m` (a copy of `m` inverted in place, kept if singular).
S_matrix InverseOf(const S_matrix &m)
{
    S_matrix inverse = m;
    inverse.Inverse(m);
    return inverse;
}

// Adds the visual to the part of `type` whose group is the number 12 characters after the '['
// ("[+DEFORM BODY 13]": group 1, slot 3) or makes it the group's first part. Returns 1 when it
// joined an existing part (no part of its own), 0 when `part` is the group's new part (not
// `fresh`: it keeps the visual's mesh). Quirk kept: the number is read 12 characters on whatever
// the type's length, so "+DEFORM BUMPER / MIRROR / LIGHT <n>" always read 0.
int __fastcall AddToPartGroup(I3D_frame *frame, std::vector<S_VehiclePart> &parts, S_VehiclePart &part,
                              const char *props, size_t start, uint8_t type, bool &fresh)  // 0x004faea0
{
    const size_t len = std::strlen(props);
    const int number = std::atoi(props + std::min(start + 12, len));   // Fix: not past the text
    const int group = number / 10;
    const int slot = number % 10;
    int found = -1;
    for (size_t i = parts.size(); i-- > 0;) {
        if (parts[i].type == type && parts[i].group == group && parts[i].frames) {
            found = static_cast<int>(i);
            break;
        }
    }
    if (slot != 0) {
        if (found < 0) {
            part.group = static_cast<uint8_t>(group);
            part.frames = new std::vector<I3D_frame *>();
            // Fix: the original indexes [slot - 1] unchecked (negative numbers).
            if (slot > 0) {
                part.frames->resize(static_cast<size_t>(slot), nullptr);
                (*part.frames)[slot - 1] = frame;
            }
            part.type = type;
            fresh = false;
            return 0;
        }
        std::vector<I3D_frame *> &frames = *parts[found].frames;
        if (slot > 0) {
            if (frames.size() < static_cast<size_t>(slot))
                frames.resize(static_cast<size_t>(slot), nullptr);
            frames[slot - 1] = frame;
        }
        return 1;
    }
    if (found < 0) {
        part.group = static_cast<uint8_t>(group);
        part.frames = new std::vector<I3D_frame *>();
        part.type = type;
        fresh = false;
        return 0;
    }
    parts[found].frame = frame;
    return 1;
}

// A projected light at `frame` (the "[<texture>]" of its properties: the text between the first
// '[' and the last ']') and a spot light (`light`), both switched off; null without a texture name.
I3D_projector_visual *__stdcall CreateLamp(I3D_frame *frame, I3D_light **light)   // 0x004e87c0
{
    const char *props = frame->GetUserProps();
    const size_t len = std::strlen(props);
    if (len <= 2)
        return nullptr;
    size_t i = 0;
    if (props[0] != '[') {
        do {
            if (i >= len)
                return nullptr;
            ++i;
        } while (props[i] != '[');
    }
    if (i >= len)
        return nullptr;
    // Fix: the original copies the rest into a 120-byte stack buffer unchecked.
    std::string name = props + i + 1;
    size_t end = name.size();
    while (name[end] != ']') {
        if (end == 0)
            return nullptr;
        --end;
    }
    if (end == 0)
        return nullptr;
    name.resize(end);

    auto *projector = static_cast<I3D_projector_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR));
    if (!projector)
        return nullptr;
    projector->SetPosInline(frame->GetWorldPos());
    projector->SetWorldDir(frame->GetWorldDir(), 0.0f);
    projector->SetScale(S_vector(8.0f, 5.0f, 20.0f));
    projector->SetColor(S_vector(0.5f, 0.5f, 0.5f));
    projector->SetUnknownB4(4);
    projector->LinkTo(nullptr, 0);
    IImage *texture = nullptr;
    if (GetIGraph()->LoadTexture(name.c_str(), "", 0, &texture) == 0) {
        projector->SetTexture(texture);
        texture->Release();
    }
    projector->SetOrthogonal(0);
    projector->SetOn(false);
    projector->Update();

    *light = static_cast<I3D_light *>(I3DGetDriver()->CreateFrame(FRAME_LIGHT));
    if (I3D_light *spot = *light) {
        spot->SetPosInline(frame->GetWorldPos());
        spot->SetWorldDir(frame->GetWorldDir(), 0.0f);
        spot->SetLightType(I3DLIGHT_SPOT);
        spot->SetMode(0x11);
        spot->SetPower(2.0f);
        spot->SetRange(60.0f);
        spot->SetCone(0.52f, 1.22f);
        spot->SetColorRGB(0.769f, 0.675f, 0.541f);
        spot->LinkTo(nullptr, 0);
        spot->SetOn(false);
        spot->Update();
    }
    return projector;
}

}  // namespace

// A damage part per visual with a "[DEFORM <type>]" (or "[+DEFORM <type> <group><slot>]") property
// longer than 9 characters: its own copy of the mesh (glass also its own material) and its short
// name. Returns 2 (ending the enumeration) when a mesh cannot be made.
uint32_t __stdcall AddVehiclePart(I3D_frame *frame, void *context)              // 0x004fa520
{
    auto &parts = *static_cast<std::vector<S_VehiclePart> *>(context);
    S_VehiclePart part = {};
    part.frame = frame;
    // The text between the first '[' and the next ']' (for properties longer than 11).
    const char *props = frame->GetUserProps();
    const size_t len = std::strlen(props);
    size_t start = 0;
    int listLen = 0;
    if (len > 11) {
        size_t i = 0;
        bool found = true;
        if (props[0] != '[') {
            do {
                if (i >= len) {
                    found = false;
                    break;
                }
                ++i;
            } while (props[i] != '[');
        }
        if (found && i < len) {
            start = ++i;
            if (props[i] != ']') {
                do {
                    if (i >= len) {
                        found = false;
                        break;
                    }
                    ++i;
                } while (props[i] != ']');
            }
            if (found && i < len)
                listLen = static_cast<int>(i - start);
        }
    }
    bool fresh = true;
    if (listLen < 10)
        return 0;
    const char *list = props + start;
    const auto is = [list](const char *word, size_t n) { return std::strncmp(list, word, n) == 0; };
    // A "+DEFORM" visual: returns whether it joined another part (no part of its own).
    const auto group = [&](uint8_t type) {
        return AddToPartGroup(frame, parts, part, props, start, type, fresh) != 0;
    };
    if (listLen >= 11 && is("DEFORM BODY", 11)) {
        part.type = VEHICLE_PART_BODY;
    } else if (listLen >= 13 && is("DEFORM BUMPER", 13)) {
        part.type = VEHICLE_PART_BUMPER;
    } else if (listLen >= 12 && is("DEFORM LIGHT", 12)) {
        part.type = VEHICLE_PART_LIGHT;
    } else if (is("DEFORM SPZ", 10)) {
        part.type = VEHICLE_PART_PLATE;
    } else if (listLen >= 13 && is("DEFORM MIRROR", 13)) {
        part.type = VEHICLE_PART_MIRROR;
    } else if (listLen >= 11 && is("DEFORM WING", 11)) {
        part.type = VEHICLE_PART_WING;
    } else if (listLen >= 11 && is("DEFORM DOOR", 11)) {
        part.type = VEHICLE_PART_DOOR;
    } else if (listLen >= 11 && is("DEFORM ROOF", 11)) {
        part.type = VEHICLE_PART_ROOF;
    } else if (listLen >= 14 && is("DEFORM STEPNEY", 14)) {
        part.type = VEHICLE_PART_SPARE_WHEEL;
    } else if (listLen >= 12 && is("DEFORM GLASS", 12)) {
        part.type = VEHICLE_PART_GLASS;
    } else {
        uint8_t type = 0;
        if (listLen >= 16 && is("+DEFORM BUMPER", 14))
            type = VEHICLE_PART_BUMPER;
        else if (listLen >= 16 && is("+DEFORM MIRROR", 14))
            type = VEHICLE_PART_MIRROR;
        else if (listLen >= 15 && is("+DEFORM LIGHT", 13))
            type = VEHICLE_PART_LIGHT;
        else if (listLen >= 14 && is("+DEFORM BODY", 12))
            type = VEHICLE_PART_BODY;
        else if (listLen >= 14 && is("+DEFORM WING", 12))
            type = VEHICLE_PART_WING;
        else if (listLen >= 14 && is("+DEFORM DOOR", 12))
            type = VEHICLE_PART_DOOR;
        else if (listLen >= 14 && is("+DEFORM ROOF", 12))
            type = VEHICLE_PART_ROOF;
        else if (listLen >= 13 && is("+DEFORM SPZ", 11))
            type = VEHICLE_PART_PLATE;
        if (type && group(type))
            return 0;
        if (part.type == 0)
            return 0;
    }

    if (fresh) {
        I3D_driver *driver = I3DGetDriver();
        I3D_mesh *mesh = driver->CreateMesh();
        if (!mesh)
            return 2;
        if (mesh->Duplicate(static_cast<I3D_visual *>(frame)->GetVisualMesh()) != 0) {
            mesh->Release();                            // Fix: the original leaks it
            return 2;
        }
        static_cast<I3D_object_visual *>(frame)->SetMesh(mesh, 0);
        mesh->Release();
        if (part.type == VEHICLE_PART_GLASS) {
            I3D_face_group *faces = mesh->GetLevel(0)->GetFaceGroup(0);
            I3D_material *material = driver->CreateMaterial();
            material->CopyFrom(faces->material);
            faces->SetMaterial(material);
            material->Release();
        }
        part.mesh = mesh;
    } else {
        part.mesh = static_cast<I3D_visual *>(frame)->GetVisualMesh();
    }
    // The short name: after the last '.' (Quirk kept: without one, from the second character).
    const std::string fullName = frame->GetName();
    size_t from = fullName.size();
    if (from) {
        while (fullName[from] != '.') {
            --from;
            if (!from)
                break;
        }
    }
    ++from;
    from = std::min(from, fullName.size());             // Fix: an empty name
    part.name = new char[fullName.size() - from + 1];
    std::strcpy(part.name, fullName.c_str() + from);
    parts.push_back(part);
    return 0;
}

bool C_Vehicle::SetupParts()                                                    // 0x004fe6b0
{
    // The bumpers to the end (swapped from the back).
    int last = static_cast<int>(m_parts.size()) - 1;
    for (int i = static_cast<int>(m_parts.size()); i-- > 0;) {
        if (m_parts[i].type != VEHICLE_PART_BUMPER)
            continue;
        if (i < last)
            std::swap(m_parts[i], m_parts[last]);
        --last;
    }
    for (size_t i = m_parts.size(); i-- > 0;) {
        S_VehiclePart &part = m_parts[i];
        if (part.frames) {
            for (size_t k = part.frames->size(); k-- > 0;) {
                I3D_frame *frame = (*part.frames)[k];
                if (!frame)                             // Fix: a group's empty slot crashed the original
                    continue;
                frame->SetOn(false);
                frame->Update();
            }
        }
        part.frame->SetOn(true);
        part.frame->Update();
    }
    return true;
}

I3D_projector_visual *C_Vehicle::CreateProjector(I3D_frame *frame, I3D_light **light)   // 0x004e8d80
{
    // "[<a>,<b>,<c>]": three texture names.
    const char *props = frame->GetUserProps();
    const size_t len = std::strlen(props);
    if (len <= 2)
        return nullptr;
    size_t i = 0;
    if (props[0] != '[') {
        do {
            if (i >= len)
                return nullptr;
            ++i;
        } while (props[i] != '[');
    }
    if (i >= len)
        return nullptr;
    std::string names[3];
    for (int n = 0; n < 3; ++n) {
        const char stop = n < 2 ? ',' : ']';
        const size_t from = ++i;
        if (props[i] != stop) {
            do {
                if (i >= len)
                    return nullptr;
                ++i;
            } while (props[i] != stop);
        }
        if (i >= len)
            return nullptr;
        // Fix: the original copies each into a 32-byte stack buffer unchecked.
        names[n].assign(props + from, i - from);
    }

    auto *projector = static_cast<I3D_projector_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR));
    if (!projector)
        return nullptr;
    projector->SetPosInline(frame->GetWorldPos());
    projector->SetWorldDir(frame->GetWorldDir(), 0.0f);
    projector->SetScale(S_vector(8.0f, 5.0f, 20.0f));
    projector->SetColor(S_vector(0.5f, 0.5f, 0.5f));
    projector->SetUnknownB4(4);
    projector->LinkTo(nullptr, 0);
    IImage *textures[3] = {};
    for (int n = 0; n < 3; ++n) {
        if (GetIGraph()->LoadTexture(names[n].c_str(), "", 0, &textures[n]) != 0) {
            projector->Release();
            for (int k = 0; k < n; ++k)
                textures[k]->Release();
            return nullptr;
        }
    }
    projector->SetOrthogonal(0);
    projector->SetOn(false);
    projector->Update();

    // The spot points along the car (the model's z axis at setup), not the projector's.
    *light = static_cast<I3D_light *>(I3DGetDriver()->CreateFrame(FRAME_LIGHT));
    if (I3D_light *spot = *light) {
        spot->SetPosInline(frame->GetWorldPos());
        spot->SetWorldDir(S_vector(m_modelAxisZ.x, m_modelAxisZ.y, m_modelAxisZ.z), 0.0f);
        spot->SetLightType(I3DLIGHT_SPOT);
        spot->SetMode(0x11);
        spot->SetPower(2.0f);
        spot->SetRange(60.0f);
        spot->SetCone(0.52f, 1.22f);
        spot->SetColorRGB(0.769f, 0.675f, 0.541f);
        spot->LinkTo(nullptr, 0);
        spot->SetOn(false);
        spot->Update();
    }
    S_VehicleProjector record = {};
    record.frame = frame;
    record.projector = projector;
    record.light = *light;
    record.textures[0] = textures[0];
    record.textures[1] = textures[1];
    record.textures[2] = textures[2];
    m_projectors.push_back(record);
    return projector;
}

void C_Vehicle::SetupLights()                                                   // 0x004e7450
{
    // The lamps of each kind: the billboard below switched off, its own projected light; the
    // sided kinds by their x in the model's space (right above 0).
    std::vector<I3D_frame *> frames;
    const auto addLamps = [&](const char *mask, uint16_t kind, bool sided) {
        frames.clear();
        m_model->EnumFrames(CollectFrame, &frames, ENUMF_WILDMASK | FRAME_MASK_VISUAL | FRAME_MASK_DUMMY, mask);
        for (I3D_frame *frame : frames) {
            S_VehicleLamp lamp = {};
            lamp.frame = frame;
            lamp.billboard = frame->FindChildFrame("BILLBOARD*", ENUMF_WILDMASK | FRAME_MASK_ANY);
            if (lamp.billboard)
                lamp.billboard->SetOn(false);
            lamp.projector = CreateLamp(frame, &lamp.light);
            if (lamp.projector)
                lamp.projector->SetOn(false);
            if (sided) {
                const S_vector pos = frame->GetWorldPos() * InverseOf(m_model->GetMatrix());
                lamp.kind = pos.x > 0.0f ? static_cast<uint16_t>(kind + 1) : kind;
            } else {
                lamp.kind = kind;
            }
            m_lamps.push_back(lamp);
        }
    };
    addLamps("LIGHTRE*", VEHICLE_LAMP_RE, false);
    addLamps("LIGHTIN*", VEHICLE_LAMP_IN_LEFT, true);
    addLamps("LIGHTBI*", VEHICLE_LAMP_BI_LEFT, true);
    addLamps("LIGHTLI*", VEHICLE_LAMP_LI_LEFT, true);
    addLamps("LIGHTBR*", VEHICLE_LAMP_BR, false);
    addLamps("LIGHTBA*", VEHICLE_LAMP_BA, false);
    frames.clear();

    // Each lamp's damage part: a LIGHT part of its own visual, else the last part of the nearest
    // ancestor below the model.
    for (size_t l = m_lamps.size(); l-- > 0;) {
        S_VehicleLamp &lamp = m_lamps[l];
        lamp.part = 0xFFFF;
        int found = -1;
        for (size_t p = m_parts.size(); p-- > 0;) {
            if (m_parts[p].frame == lamp.frame && m_parts[p].type == VEHICLE_PART_LIGHT) {
                found = static_cast<int>(p);
                break;
            }
        }
        if (found >= 0) {
            lamp.part = static_cast<uint16_t>(found);
            continue;
        }
        for (I3D_frame *frame = lamp.frame->GetParent(); frame != m_model; frame = frame->GetParent()) {
            for (size_t p = m_parts.size(); p-- > 0;) {
                if (m_parts[p].frame == frame) {
                    found = static_cast<int>(p);
                    break;
                }
            }
            if (found >= 0) {
                lamp.part = static_cast<uint16_t>(found);
                break;
            }
        }
    }

    // The projectors: one above exactly two lamps without their own projector lights both.
    std::vector<I3D_frame *> projectors;
    m_model->EnumFrames(CollectFrame, &projectors, ENUMF_WILDMASK | FRAME_MASK_DUMMY, "PROJECTOR*");
    const auto lampOf = [this](I3D_frame *frame) {
        for (size_t l = m_lamps.size(); l-- > 0;) {
            if (m_lamps[l].frame == frame && !m_lamps[l].projector)
                return static_cast<int>(l);
        }
        return -1;
    };
    for (I3D_frame *frame : projectors) {
        std::vector<I3D_frame *> lamps;
        frame->EnumFrames(CollectFrame, &lamps, ENUMF_WILDMASK | FRAME_MASK_VISUAL | FRAME_MASK_DUMMY, "LIGHT*");
        if (lamps.size() != 2)
            continue;
        const int a = lampOf(lamps[0]);
        if (a < 0)
            continue;
        const int b = lampOf(lamps[1]);
        if (b < 0 || a == b)
            continue;
        m_lamps[a].projector = CreateProjector(frame, &m_lamps[a].light);
        m_lamps[b].projector = m_lamps[a].projector;
        m_lamps[b].light = m_lamps[a].light;
        if (m_lamps[a].projector) {
            m_lamps[a].flags |= VEHICLE_LAMP_SHARED;
            m_lamps[b].flags |= VEHICLE_LAMP_SHARED;
            m_lamps[a].projectorIndex = static_cast<uint16_t>(m_projectors.size() - 1);
            m_lamps[b].projectorIndex = m_lamps[a].projectorIndex;
        }
    }
}
