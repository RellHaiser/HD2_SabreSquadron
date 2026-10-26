// C_Vehicle: lifetime and the setup from the model (original 0x004e2900 C_Vehicle, 0x004e2e30
// Setup, 0x004e6de0 SetupStruts, 0x004e7f10 ~C_Vehicle, 0x004e81a0 Release; the frame callbacks
// and helpers 0x004e8770, 0x004f9be0..0x004fa2e0).
#include "game/vehicle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

std::vector<I3D_sector *> C_Vehicle::s_lightSectors;   // 0x0089c1b0

namespace {

// --- frame names and properties ----------------------------------------------------------------

// A character of a frame name (0 past its end). Fix: the original indexes the name blindly and
// reads past short names.
char CharAt(const char *s, size_t i)
{
    return i <= std::strlen(s) ? s[i] : '\0';
}

// The rest of a frame name from `i` (empty past its end; Fix, as CharAt).
const char *Suffix(const char *s, size_t i)
{
    const size_t n = std::strlen(s);
    return s + (i <= n ? i : n);
}

// The text after the '[' of a frame's properties (e.g. "[30]" or "[12 -5 0]"); null without one
// or for texts of up to 2 characters.
const char *PropsList(const char *props)
{
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
    return props + i + 1;
}

// Whether the "[..]" part of a frame's properties has `item` right after the '[' or a ','. The
// scan goes on to the end of the text (past the ']'). Fix: the original reads on past the end of
// a text without a '['.
bool HasPropsItem(const char *props, char item)
{
    if (!*props)
        return false;
    size_t i = 0;
    while (props[i] != '[' && props[i])
        ++i;
    if (!props[i])
        return false;
    for (;;) {
        const char c = props[++i];
        if (c == item) {
            if (props[i - 1] == ',' || props[i - 1] == '[')
                return true;
            continue;
        }
        if (!c)
            return false;
    }
}

// "[x y z]" in a frame's properties (cm) into `v` as metres (left as it is without one).
void ReadPropsVector(I3D_frame *frame, S_vector &v)
{
    if (const char *list = PropsList(frame->GetUserProps())) {
        std::sscanf(list, " %f %f %f", &v.x, &v.y, &v.z);
        v.x *= 0.01f;
        v.y *= 0.01f;
        v.z *= 0.01f;
    }
}

void SetXYZ(S_vector4 &dst, const S_vector &v)
{
    dst.x = v.x;
    dst.y = v.y;
    dst.z = v.z;
}

// The inverse of `m` (as the original: a copy of `m` inverted in place, kept if singular).
S_matrix InverseOf(const S_matrix &m)
{
    S_matrix inverse = m;
    inverse.Inverse(m);
    return inverse;
}

S_vector4 Row(const S_matrix &m, int row)
{
    return {m.m[row][0], m.m[row][1], m.m[row][2], m.m[row][3]};
}

// The local position with its w (the original copies the four floats).
S_vector4 LocalPos4(I3D_frame *frame)
{
    const S_vector &p = frame->GetPos();
    return {p.x, p.y, p.z, frame->GetPosW()};
}

// Puts the frame numbered i (the number at `numberAt` in its name) at position i where one
// follows: each position takes the first later frame with its number (DOOR*, SEAT*).
void SortByNumber(std::vector<I3D_frame *> &frames, size_t numberAt)
{
    for (size_t i = 0; i < frames.size(); ++i) {
        if (std::atoi(Suffix(frames[i]->GetName(), numberAt)) == static_cast<int>(i))
            continue;
        for (size_t j = i + 1; j < frames.size(); ++j) {
            if (std::atoi(Suffix(frames[j]->GetName(), numberAt)) == static_cast<int>(i)) {
                std::swap(frames[i], frames[j]);
                break;
            }
        }
    }
}

// The capacity the original's frame lists (MSVC 6 std::vector) have after `count` push_backs
// (0x004f9cc0 grows a full list by its size, at least 1).
size_t PushBackCapacity(size_t count)
{
    size_t capacity = 0;
    for (size_t size = 0; size < count; ++size) {
        if (size == capacity)
            capacity = size + (size < 2 ? 1 : size);
    }
    return capacity;
}

// --- EnumFrames callbacks ----------------------------------------------------------------------

uint32_t __stdcall StoreFoundFrame(I3D_frame *frame, void *found)              // 0x004f9cb0
{
    *static_cast<I3D_frame **>(found) = frame;
    return 0;
}

uint32_t __stdcall CollectFrame(I3D_frame *frame, void *list)                  // 0x004f9cc0
{
    static_cast<std::vector<I3D_frame *> *>(list)->push_back(frame);
    return 0;
}

// A wheel per "WH*" frame, appended to the list.
uint32_t __stdcall AddVehicleWheel(I3D_frame *frame, void *list)               // 0x004f9be0
{
    // Fix: the original sets only these fields of the (aligned) allocation.
    auto *wheel = new S_VehicleWheel();
    wheel->unknown000.v.x = 0.0f;
    wheel->frame = frame;
    wheel->name = frame->GetName();
    wheel->next = nullptr;
    auto **link = static_cast<S_VehicleWheel **>(list);
    while (*link)
        link = &(*link)->next;
    *link = wheel;
    return 0;
}

// --- the body collision boxes ------------------------------------------------------------------

struct S_BodyBoxContext {
    size_t nameLen;                                     // the model name's length + 1
    I3D_frame *model;
    S_BodyCollisionBox **root;
};

struct S_BodyPointContext {
    size_t nameLen;
    S_BodyCollisionBox *box;
};

// Adds `box` below the box of its frame's parent in `list` (searched depth first); it then moves
// with that box's moving parent.
bool __fastcall InsertBodyCollisionBox(S_BodyCollisionBox *list, S_BodyCollisionBox *box)  // 0x004fa0e0
{
    I3D_frame *parent = box->frame->GetParent();
    for (; list; list = list->next) {
        if (parent == list->frame) {
            S_BodyCollisionBox **link = &list->children;
            while (*link)
                link = &(*link)->next;
            *link = box;
            if (list->parent)
                box->parent = list->parent;
            return true;
        }
        if (InsertBodyCollisionBox(list->children, box))
            return true;
    }
    return false;
}

// A box per "BODYCOL*" dummy: its box and position scaled by the frame's world scale.
uint32_t __stdcall AddBodyCollisionBox(I3D_frame *frame, void *context)       // 0x004f9e70
{
    const auto &c = *static_cast<S_BodyBoxContext *>(context);
    auto *box = new S_BodyCollisionBox();
    box->frame = frame;
    if (CharAt(frame->GetName(), c.nameLen + 7) == 'B')
        box->flagB = true;
    const I3D_bbox bbox = static_cast<I3D_dummy *>(frame)->GetBBox();
    box->halfHeight = std::fabs(bbox.max.y - bbox.min.y) * 0.5f;
    box->halfHeight = frame->GetWorldScale().y * box->halfHeight;
    box->pos = frame->GetPos();
    box->unknown2C = frame->GetPosW();
    box->min = bbox.min;
    box->unknown0C = bbox.minW;
    box->max = bbox.max;
    box->unknown1C = bbox.maxW;
    S_vector scale = frame->GetWorldScale();
    box->min = S_vector(scale.x * box->min.x, scale.y * box->min.y, scale.z * box->min.z);
    scale = frame->GetWorldScale();
    box->max = S_vector(scale.x * box->max.x, scale.y * box->max.y, scale.z * box->max.z);

    S_BodyCollisionBox *&root = *c.root;
    if (!root) {
        root = box;
        if (frame->GetParent() != c.model)
            box->parent = frame->GetParent();
    } else if (!InsertBodyCollisionBox(root, box)) {
        S_BodyCollisionBox *last = root;
        while (last->next)
            last = last->next;
        last->next = box;
        if (frame->GetParent() != c.model)
            box->parent = frame->GetParent();
    }
    return 0;
}

// A point per dummy right below a box's frame (other than "BODYCOL*" ones).
uint32_t __stdcall AddBodyCollisionPoint(I3D_frame *frame, void *context)     // 0x004fa190
{
    const auto &c = *static_cast<S_BodyPointContext *>(context);
    // Fix: the original allocates the point first and leaks it when these tests fail.
    if (frame->GetParent() != c.box->frame)
        return 0;
    if (std::strncmp("BODYCOL", Suffix(frame->GetName(), c.nameLen), 7) == 0)
        return 0;
    auto *point = new S_BodyCollisionPoint();
    const I3D_bbox &bbox = static_cast<I3D_dummy *>(frame)->GetBBox();
    point->radius = std::fabs(bbox.max.y - bbox.min.y) * 0.5f;
    point->radius = frame->GetWorldScale().y * point->radius;
    point->pos = frame->GetPos();
    point->unknown0C = frame->GetPosW();
    point->frame = frame;
    S_BodyCollisionPoint **link = &c.box->points;
    while (*link)
        link = &(*link)->next;
    *link = point;
    return 0;
}

// Collects the points of every box of the tree.
void CollectBodyCollisionPoints(size_t nameLen, S_BodyCollisionBox *list)      // 0x004fa140
{
    for (; list; list = list->next) {
        S_BodyPointContext context = {nameLen, list};
        list->frame->EnumFrames(AddBodyCollisionPoint, &context, ENUMF_WILDMASK | FRAME_MASK_DUMMY, "*");
        CollectBodyCollisionPoints(nameLen, list->children);
    }
}

void FreeBodyCollision(S_BodyCollisionBox *list)                               // 0x004e8770
{
    while (list) {
        for (S_BodyCollisionPoint *point = list->points; point;) {
            S_BodyCollisionPoint *next = point->next;
            delete point;
            point = next;
        }
        FreeBodyCollision(list->children);
        S_BodyCollisionBox *next = list->next;
        delete list;
        list = next;
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------

C_Vehicle::C_Vehicle()                                                          // 0x004e2900
{
    m_unknown2289 = 1;
    m_unknown2292 = 1;
    m_soundsOn = true;
    m_unknownF6C = 0x8000DFF9;
    m_unknownF70 = 0x8000DFF9;
    m_unknown53C = 2;
    m_unknown5F4 = 0.5f;
    m_unknown5F8 = 0.5f;
    std::fill(std::begin(m_callbacks), std::end(m_callbacks), nullptr);
    m_bodyCollision = nullptr;
    m_unknown534 = 0xF02;
    m_unknown538 = 0;
    m_unknown540 = 0;
    m_unknown550 = 0;
    m_unknown548 = 1.0f;
    m_unknown54C = 0;
    m_unknown228C = 0;
    m_unknown228D = 0;
    m_unknown5B8 = 0.4f;
    m_unknown5BC = 0.3f;
    m_unknown5C0 = 0.0f;
    m_unknown5C4 = 0.2f;
    m_unknown5CC = 0.0f;
    m_unknown5C8 = 0.3f;
    m_unknown5F0 = 0.0f;
    m_unknown650 = 0;
    m_unknown2297 = 0;
    m_unknown2298 = 0;
    m_wheelList = nullptr;
    m_wheels = nullptr;
    m_callbackContext = nullptr;
    m_unknown678 = 50;
    m_unknownF54 = 0.028f;
    m_unknownF74 = 0;
    std::fill(std::begin(m_sounds1), std::end(m_sounds1), nullptr);
    std::fill(std::begin(m_sounds2), std::end(m_sounds2), nullptr);
    m_model = nullptr;
}

C_Vehicle::~C_Vehicle()                                                         // 0x004e7f10
{
    Release();
}

void C_Vehicle::Release()                                                       // 0x004e81a0
{
    m_modelName.clear();
    m_modelFileName.clear();
    FreeBodyCollision(m_bodyCollision);
    m_bodyCollision = nullptr;                          // (the original leaves the pointer)
    while (S_VehicleWheel *wheel = m_wheelList) {
        m_wheelList = wheel->next;
        delete wheel;
    }
    delete[] m_wheels;
    m_wheels = nullptr;

    // A lamp's own projector and light (a shared one is the projector record's).
    for (size_t i = m_lamps.size(); i-- > 0;) {
        S_VehicleLamp &lamp = m_lamps[i];
        if (lamp.projector && !(lamp.flags & VEHICLE_LAMP_SHARED)) {
            lamp.projector->LinkTo(nullptr, 0);
            lamp.projector->Release();
            if (lamp.light) {
                lamp.light->LinkTo(nullptr, 0);
                lamp.light->Release();
            }
        }
    }
    m_lamps.clear();
    s_lightSectors.clear();
    for (size_t i = m_projectors.size(); i-- > 0;) {
        S_VehicleProjector &projector = m_projectors[i];
        projector.projector->LinkTo(nullptr, 0);
        projector.projector->Release();
        projector.textures[1]->Release();
        projector.textures[2]->Release();
        projector.textures[0]->Release();
        if (projector.light) {
            projector.light->LinkTo(nullptr, 0);
            projector.light->Release();
        }
    }
    m_projectors.clear();
    for (size_t i = m_parts.size(); i-- > 0;) {
        S_VehiclePart &part = m_parts[i];
        if (part.glassTexture)
            part.glassTexture->Release();
        part.glassTexture = nullptr;
        delete[] part.name;
        delete part.frames;
    }
    m_parts.clear();

    m_specialCollisionCorners.clear();
    m_exhausts.clear();
    m_specialCollisions.clear();
    m_specialCollisionDescs.clear();
    m_doors.clear();
    m_ballasts.clear();
    m_seats.clear();
    m_axleCount = 0;
    m_axleStruts.clear();
    m_sideStruts.clear();
    m_tracksL.clear();
    m_tracksR.clear();
    if (m_model)
        m_model->Release();
    m_model = nullptr;
}

void C_Vehicle::RemoveModelFrame(I3D_frame *frame)
{
    frame->LinkTo(nullptr, 0);
    static_cast<I3D_model *>(m_model)->RemoveModelFrame(frame);
}

void C_Vehicle::PlaceBodyCollision(S_BodyCollisionBox *list)                   // 0x004fa2e0
{
    for (; list; list = list->next) {
        for (S_BodyCollisionPoint *point = list->points; point; point = point->next) {
            point->pos *= list->frame->GetMatrix();
            if (!list->parent) {
                point->pos *= m_invModelMatrix;
            } else {
                point->pos *= InverseOf(list->parent->GetMatrix());
            }
            RemoveModelFrame(point->frame);
            point->frame = nullptr;
        }
        PlaceBodyCollision(list->children);
        list->pos *= list->frame->GetParent()->GetMatrix();
        if (!list->parent) {
            list->pos *= m_invModelMatrix;
        } else {
            list->pos *= InverseOf(list->parent->GetMatrix());
        }
        list->max += list->pos;
        list->min += list->pos;
        RemoveModelFrame(list->frame);
        list->frame = nullptr;
    }
}

void C_Vehicle::SetupStruts()                                                   // 0x004e6de0
{
    std::vector<I3D_frame *> frames;
    m_model->EnumFrames(CollectFrame, &frames, ENUMF_WILDMASK | FRAME_MASK_VISUAL, "AXIS*");
    // "<model>.AXIS<kind><axle>": the kind character, then the number.
    const size_t numberAt = m_modelName.size() + 1 + 5;
    S_VehicleStrut strut = {};                          // reused as the original's stack record

    // Whole axles: "AXIS0<axle>".
    for (size_t i = frames.size(); i-- > 0;) {
        const char *name = frames[i]->GetName();
        if (std::atoi(Suffix(name, numberAt)) >= m_axleCount || CharAt(name, numberAt - 1) != '0')
            continue;
        strut = {};
        ReadPropsVector(frames[i], strut.offset);
        strut.frame = frames[i];
        strut.axle = std::atoi(Suffix(name, numberAt));
        strut.single = true;
        m_axleStruts.push_back(strut);
        frames.erase(frames.begin() + i);
    }

    // One side: "AXISL<axle>" / "AXISR<axle>" (the right one's x offset mirrored), counted per
    // side and axle.
    constexpr int MAX_STRUT_AXLES = 20;
    int left[MAX_STRUT_AXLES] = {};
    int right[MAX_STRUT_AXLES] = {};
    for (size_t i = frames.size(); i-- > 0;) {
        const char *name = frames[i]->GetName();
        strut.single = false;
        strut.offset = S_vector(0.0f, 0.0f, 0.0f);
        ReadPropsVector(frames[i], strut.offset);
        int *count;
        if (std::atoi(Suffix(name, numberAt)) < m_axleCount && CharAt(name, numberAt - 1) == 'L') {
            strut.frame = frames[i];
            strut.axle = std::atoi(Suffix(name, numberAt));
            strut.left = true;
            count = left;
        } else if (std::atoi(Suffix(name, numberAt)) < m_axleCount && CharAt(name, numberAt - 1) == 'R') {
            strut.frame = frames[i];
            strut.axle = std::atoi(Suffix(name, numberAt));
            strut.offset.x = -strut.offset.x;
            strut.left = false;
            count = right;
        } else {
            continue;
        }
        m_sideStruts.push_back(strut);
        frames.erase(frames.begin() + i);
        // Fix: the original counts into its 20-entry arrays unchecked (negative numbers, axles
        // past 20).
        if (strut.axle >= 0 && strut.axle < MAX_STRUT_AXLES)
            ++count[strut.axle];
    }
    for (size_t i = m_sideStruts.size(); i-- > 0;) {
        S_VehicleStrut &s = m_sideStruts[i];
        if (s.axle < 0 || s.axle >= MAX_STRUT_AXLES)
            continue;
        if ((s.left ? left[s.axle] : right[s.axle]) == 1)
            s.single = true;
    }
}

bool C_Vehicle::Setup(I3D_frame *model)                                         // 0x004e2e30
{
    m_model = model;
    if (!model)
        return false;
    model->AddRef();
    if (model->GetType() != FRAME_MODEL)
        return false;
    m_modelName = model->GetName();
    m_modelFileName = static_cast<I3D_model *>(model)->GetFileName();
    m_modelPos = Row(m_model->GetMatrix(), 3);
    m_modelMatrix = m_model->GetMatrix();
    m_modelAxisX = Row(m_modelMatrix, 0);
    m_modelAxisY = Row(m_modelMatrix, 1);
    m_modelAxisZ = Row(m_modelMatrix, 2);

    m_body = nullptr;
    m_model->EnumFrames(StoreFoundFrame, &m_body, FRAME_MASK_VISUAL | FRAME_MASK_DUMMY, "BODY");
    if (!m_body)
        return false;

    // The exhausts (in reverse order), removed from the model: positions and directions through
    // the body's local matrix (the directions without its translation).
    std::vector<I3D_frame *> frames;
    m_model->EnumFrames(CollectFrame, &frames, ENUMF_WILDMASK | FRAME_MASK_VISUAL | FRAME_MASK_DUMMY,
                        "EXHAUST*");
    for (size_t i = frames.size(); i-- > 0;) {
        S_matrix m = m_body->GetLocalMatrix();
        S_VehicleExhaust exhaust = {};
        SetXYZ(exhaust.pos, frames[i]->GetPos() * m);
        m(3) = S_vector(0.0f, 0.0f, 0.0f);
        SetXYZ(exhaust.dir, frames[i]->GetLocalDir() * m);
        exhaust.effect = -1;
        m_exhausts.push_back(exhaust);
        RemoveModelFrame(frames[i]);
    }
    frames.clear();

    // The special collision volumes: the box corners through the frame's local matrix, the
    // parent's inverse matrix when it is not the model; removed from the model.
    m_model->EnumFrames(CollectFrame, &frames, ENUMF_WILDMASK | FRAME_MASK_VISUAL | FRAME_MASK_DUMMY,
                        "SPECCOLL*");
    for (size_t i = 0; i < frames.size(); ++i) {
        I3D_frame *frame = frames[i];
        const I3D_bbox bbox = static_cast<I3D_dummy *>(frame)->GetBBox();
        S_vector4 corner = {};
        SetXYZ(corner, bbox.min * frame->GetLocalMatrix());
        m_specialCollisionCorners.push_back(corner);
        SetXYZ(corner, bbox.max * frame->GetLocalMatrix());
        m_specialCollisionCorners.push_back(corner);

        S_VehicleSpecialCollision special = {};
        special.invParentMatrix.Identity();
        special.localPos = {0.0f, 0.0f, 0.0f, 0.0f};
        special.parent = nullptr;
        I3D_frame *parent = frame->GetParent();
        if (parent != m_model) {
            special.parent = parent;
            special.invParentMatrix = InverseOf(parent->GetMatrix());
            special.localPos = LocalPos4(frame);
        }
        m_specialCollisions.push_back(special);

        S_VehicleCollisionDesc desc;
        desc.hasParent = special.parent != nullptr;
        m_specialCollisionDescs.push_back(desc);
        if (auto callback = reinterpret_cast<T_VehicleCollisionCallback>(m_callbacks[VEHICLE_CALLBACK_COLLISION]))
            callback(m_callbackContext, frame, &m_specialCollisionDescs.back(), static_cast<int>(i));
        RemoveModelFrame(frame);
    }

    // The head ("HEAD"), removed from the model.
    I3D_frame *found = nullptr;
    m_model->EnumFrames(StoreFoundFrame, &found, FRAME_MASK_DUMMY, "HEAD");
    if (found) {
        SetXYZ(m_headPos, found->GetPos() * m_body->GetLocalMatrix());
        RemoveModelFrame(found);
    } else {
        SetXYZ(m_headPos, S_vector(0.0f, 0.0f, 0.0f));
    }

    // The body collision boxes, then their points; placed once the model's inverse matrix is
    // known.
    m_bodyCollision = nullptr;
    S_BodyBoxContext boxContext = {m_modelName.size() + 1, m_model, &m_bodyCollision};
    m_model->EnumFrames(AddBodyCollisionBox, &boxContext, ENUMF_WILDMASK | FRAME_MASK_DUMMY, "BODYCOL*");
    CollectBodyCollisionPoints(boxContext.nameLen, m_bodyCollision);
    m_invModelMatrix = InverseOf(m_model->GetMatrix());
    PlaceBodyCollision(m_bodyCollision);

    // The doors: only "DOOR<digit>..", sorted by number; the window below each.
    std::vector<I3D_frame *> doors;
    m_model->EnumFrames(CollectFrame, &doors, ENUMF_WILDMASK | FRAME_MASK_VISUAL, "DOOR*");
    size_t numberAt = m_modelName.size() + 1 + 4;
    for (size_t i = 0; i < doors.size(); ++i) {
        const char c = CharAt(doors[i]->GetName(), numberAt);
        if (c < '0' || c > '9') {
            doors.erase(doors.begin() + i);
            --i;
        }
    }
    SortByNumber(doors, numberAt);
    for (I3D_frame *frame : doors) {
        S_VehicleDoor door = {};
        SetXYZ(door.pos, frame->GetPos() * frame->GetParent()->GetLocalMatrix());
        door.speed = 1.45f;
        door.unknown2C = 1.25f;
        door.doorPart = 0xFFFF;
        door.frame = frame;
        door.closedRot = frame->GetRot();
        if (frame->GetLocalDir().z < 0.0f)
            door.flags |= DOOR_FLAG_4 | DOOR_BACKWARD;
        if (HasPropsItem(frame->GetUserProps(), 'I'))
            door.flags |= DOOR_FLAG_4;
        if (I3D_frame *window = frame->FindChildFrame("WINDOW*", ENUMF_WILDMASK | FRAME_MASK_VISUAL)) {
            door.window = window;
            door.windowPos = LocalPos4(window);
            door.windowX = door.windowPos.x;
            door.windowY = door.windowPos.y;
            door.windowLowY = door.windowPos.y - 0.5f;
            door.unknown60 = 0.3f;
            door.windowPart = 0xFFFF;
        }
        m_doors.push_back(door);
    }

    // The ballast weights, sorted by number (an insertion sort), removed from the model.
    std::vector<I3D_frame *> ballasts;
    m_model->EnumFrames(CollectFrame, &ballasts, ENUMF_WILDMASK | FRAME_MASK_DUMMY, "BALLAST*");
    numberAt = m_modelName.size() + 1 + 7;
    size_t capacity = PushBackCapacity(ballasts.size());
    for (size_t i = 1; i < ballasts.size(); ++i) {
        const int number = std::atoi(Suffix(ballasts[i]->GetName(), numberAt));
        for (size_t j = 0; j < i; ++j) {
            if (number < std::atoi(Suffix(ballasts[j]->GetName(), numberAt))) {
                // Quirk kept: the original inserts through a reference to ballasts[i]; with spare
                // capacity its vector shifts first, so the copy is of ballasts[i - 1] (a frame
                // is lost, another doubled). It then moves the last frame over the old place.
                I3D_frame *moved = ballasts.size() < capacity ? ballasts[i - 1] : ballasts[i];
                if (ballasts.size() >= capacity)
                    capacity = ballasts.size() * 2;
                ballasts.insert(ballasts.begin() + j, moved);
                ballasts[i + 1] = ballasts.back();
                ballasts.pop_back();
                break;
            }
        }
    }
    for (I3D_frame *frame : ballasts) {
        S_matrix m = frame->GetParent()->GetLocalMatrix();
        S_VehicleBallast ballast = {};
        SetXYZ(ballast.pos, frame->GetPos() * m);
        m(3) = S_vector(0.0f, 0.0f, 0.0f);
        SetXYZ(ballast.dir, frame->GetLocalDir() * m);
        m_ballasts.push_back(ballast);
        RemoveModelFrame(frame);
    }

    // The seats (at least one), sorted by number; each takes the door of its number.
    std::vector<I3D_frame *> seats;
    m_model->EnumFrames(CollectFrame, &seats, ENUMF_WILDMASK | FRAME_MASK_DUMMY, "SEAT*");
    if (!(m_unknownF74 & 0x2) && seats.empty())
        return false;
    numberAt = m_modelName.size() + 1 + 4;
    SortByNumber(seats, numberAt);
    for (I3D_frame *frame : seats) {
        S_matrix m = frame->GetParent()->GetLocalMatrix();
        S_VehicleSeat seat = {};
        SetXYZ(seat.pos, frame->GetPos() * m);
        m(3) = S_vector(0.0f, 0.0f, 0.0f);
        SetXYZ(seat.dir, frame->GetLocalDir() * m);
        seat.frame = frame;
        seat.occupant = nullptr;
        m_seats.push_back(seat);
        if (auto callback = reinterpret_cast<T_VehicleSeatCallback>(m_callbacks[VEHICLE_CALLBACK_SEAT]))
            callback(m_callbackContext, frame);
    }
    for (size_t i = 0; i < m_seats.size(); ++i) {
        if (i < m_doors.size())
            m_seats[i].door = &m_doors[i];
    }

    // The wheels: "<model>.WH<side><axle>" below "<model>.DWHL.." / ".DWHR.." holders; an even
    // number, at least 4. The radius and width from the frame's box (the width also from the
    // holder's "[<cm>]").
    m_model->EnumFrames(AddVehicleWheel, &m_wheelList, ENUMF_WILDMASK | FRAME_MASK_VISUAL | FRAME_MASK_DUMMY,
                        "WH*");
    if (!(m_unknownF74 & 0x2) && !m_wheelList)
        return false;
    int wheelCount = 0;
    for (S_VehicleWheel *wheel = m_wheelList; wheel; wheel = wheel->next) {
        const size_t sideAt = m_modelName.size() + 3;
        if (!std::isalnum(static_cast<unsigned char>(CharAt(wheel->name.c_str(), sideAt))))
            return false;
        I3D_frame *holder = wheel->frame->GetParent();
        std::string prefix = m_modelName + ".DWHL";
        if (std::strncmp(holder->GetName(), prefix.c_str(), prefix.size()) != 0) {
            prefix = m_modelName + ".DWHR";
            if (std::strncmp(holder->GetName(), prefix.c_str(), prefix.size()) != 0)
                return false;
        }
        wheel->holder = holder;
        wheel->flags = 0;
        I3D_bbox bbox;
        I3DGetDriver()->GetHierarchyBBox(bbox, wheel->frame);
        wheel->unknown1E0 = 0.0f;
        wheel->radius = std::fabs(bbox.max.y - bbox.min.y) * 0.5f;
        wheel->innerRadius = wheel->radius * 0.8f;
        wheel->width = std::fabs(bbox.max.x - bbox.min.x);
        if (const char *list = PropsList(holder->GetUserProps())) {
            std::sscanf(list, "%f", &wheel->width);
            wheel->width *= 0.01f;
        }
        const float width2 = wheel->width * wheel->width;
        const float outer = std::sqrt(wheel->radius * wheel->radius + width2);
        wheel->unknown1E4 = outer;
        wheel->unknown1EC = static_cast<float>(std::acos(static_cast<double>(wheel->radius / outer)));
        wheel->unknown1E8 = std::sqrt(wheel->innerRadius * wheel->innerRadius + width2);
        wheel->unknown15C = wheel->frame->GetPos().x;
        ++wheelCount;
    }
    if (!(m_unknownF74 & 0x2) && (wheelCount < 4 || wheelCount % 2 != 0))
        return false;
    m_wheelCount = wheelCount;
    m_axleCount = wheelCount / 2;
    m_wheels = new S_VehicleWheel *[wheelCount];
    // Per axle the left wheel, then the right one (null when missing).
    const size_t sideAt = m_modelName.size() + 3;
    for (int i = 0, axle = 0; i < m_wheelCount; ++axle) {
        for (const char side : {'L', 'R'}) {
            S_VehicleWheel *wheel = m_wheelList;
            for (; wheel; wheel = wheel->next) {
                const char *name = wheel->name.c_str();
                const char c = CharAt(name, sideAt);
                if (std::atoi(Suffix(name, sideAt + 1)) == axle &&
                    (c == side || c == std::tolower(static_cast<unsigned char>(side))))
                    break;
            }
            // Fix: the original writes one past the array for an odd count (allowed with
            // +0xF74 bit 1).
            if (i < m_wheelCount)
                m_wheels[i] = wheel;
            ++i;
        }
    }
    SetupStruts();
    UpdateStruts();

    // The CENTRE box: its corners through its and the body's local matrices.
    I3D_frame *centre = nullptr;
    m_model->EnumFrames(StoreFoundFrame, &centre, FRAME_MASK_DUMMY, "CENTRE");
    if (!centre)
        return false;
    m_bodyCollisionDesc = S_VehicleCollisionDesc();
    if (auto callback = reinterpret_cast<T_VehicleCollisionCallback>(m_callbacks[VEHICLE_CALLBACK_COLLISION]))
        callback(m_callbackContext, centre, &m_bodyCollisionDesc, -1);
    const I3D_bbox centreBox = static_cast<I3D_dummy *>(centre)->GetBBox();
    SetXYZ(m_bodyMin, centreBox.min * centre->GetLocalMatrix());
    SetXYZ(m_bodyMax, centreBox.max * centre->GetLocalMatrix());
    SetXYZ(m_bodyMin, S_vector(m_bodyMin.x, m_bodyMin.y, m_bodyMin.z) * m_body->GetLocalMatrix());
    SetXYZ(m_bodyMax, S_vector(m_bodyMax.x, m_bodyMax.y, m_bodyMax.z) * m_body->GetLocalMatrix());
    RemoveModelFrame(centre);

    // The model's box (its size and the offset of its min corner), also scaled. (The w
    // components are left: the original copies stack leftovers or the scale's w.)
    m_model->Update();
    I3D_bbox modelBoxStore;
    const I3D_bbox modelBox = *I3DGetDriver()->GetHierarchyBBox(modelBoxStore, m_model);
    m_size.x = modelBox.max.x - modelBox.min.x;
    m_size.y = modelBox.max.y - modelBox.min.y;
    m_size.z = modelBox.max.z - modelBox.min.z;
    S_vector scale = m_model->GetWorldScale();
    SetXYZ(m_scaledSize, S_vector(scale.x * m_size.x, scale.y * m_size.y, scale.z * m_size.z));
    SetXYZ(m_boxOffset, -modelBox.min);
    scale = m_model->GetWorldScale();
    SetXYZ(m_scaledBoxOffset, S_vector(scale.x * m_boxOffset.x, scale.y * m_boxOffset.y,
                                       scale.z * m_boxOffset.z));

    // The steering wheel (kept in the model) and the engine smoke (removed).
    m_steeringWheel = nullptr;
    m_model->EnumFrames(StoreFoundFrame, &m_steeringWheel, FRAME_MASK_VISUAL, "SWHEEL");
    if (m_steeringWheel)
        SetXYZ(m_steeringAxis, m_steeringWheel->GetLocalDir());
    found = nullptr;
    m_model->EnumFrames(StoreFoundFrame, &found, FRAME_MASK_DUMMY, "SMOKE_MOTOR");
    if (found) {
        SetXYZ(m_smokePos, found->GetPos() * m_body->GetLocalMatrix());
        RemoveModelFrame(found);
    } else {
        SetXYZ(m_smokePos, S_vector(0.0f, 0.0f, 0.0f));
    }

    // The wheels' holders in the model's space.
    for (int i = 0; i < m_wheelCount; ++i) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (!wheel)                                     // Fix: the original crashes on a missing wheel
            continue;
        wheel->holderPos = LocalPos4(wheel->holder);
        if (wheel->holder->GetParent() != m_model) {
            SetXYZ(wheel->modelPos, wheel->holder->GetWorldPos() * m_invModelMatrix);
        } else {
            wheel->modelPos = wheel->holderPos;
        }
        wheel->extension = 0.0f;
        wheel->unknown0A0 = S_vector(0.0f, 0.0f, 0.0f);
        wheel->unknown0B0 = S_vector(0.0f, 0.0f, 0.0f);
        wheel->unknown1F0 = 0.0f;
        if (wheel->frame->GetType() == FRAME_DUMMY)
            wheel->flags |= WHEEL_DUMMY;
    }

    // The track strips (their own copy of the mesh): per strip and wheel of its side, the
    // vertices below the wheel's centre within 0.05 of its z, with their heights below the
    // centre shifted by the gap between the wheel's bottom and the highest of them.
    std::vector<float> offsets;
    std::vector<uint16_t> vertices;
    const auto addTracks = [&](const char *mask, int side, std::vector<S_VehicleTrack> &tracks) {
        std::vector<I3D_frame *> strips;
        m_model->EnumFrames(CollectFrame, &strips, ENUMF_WILDMASK | FRAME_MASK_VISUAL, mask);
        for (I3D_frame *frame : strips) {
            auto *strip = static_cast<I3D_object_visual *>(frame);
            strip->DuplicateMesh();
            for (int k = m_wheelCount; k-- > 0;) {
                if (k % 2 != side)
                    continue;
                offsets.clear();
                vertices.clear();
                S_VehicleWheel *wheel = m_wheels[k];
                if (!wheel)                             // Fix: as above
                    continue;
                float highest = -1000.0f;
                float gap = 0.0f;
                const float bottom = wheel->modelPos.y - wheel->radius;
                const float z = wheel->modelPos.z;
                I3D_mesh_level *level = strip->GetVisualMesh()->GetLevel(0);
                const auto *data = static_cast<const uint8_t *>(level->GetVertices(0));
                for (int v = level->NumVertices(); v-- > 0;) {
                    S_vector p = *reinterpret_cast<const S_vector *>(data + v * 0x20);
                    p *= strip->GetLocalMatrix();
                    if (!(std::fabs(p.z - z) < 0.05))
                        continue;
                    if (!(p.y < wheel->modelPos.y))
                        continue;
                    if (highest < p.y) {
                        highest = p.y;
                        gap = bottom - p.y;
                    }
                    offsets.push_back(p.y - wheel->modelPos.y);
                    vertices.push_back(static_cast<uint16_t>(v));
                }
                level->UnlockVertices();
                if (offsets.empty())
                    continue;
                for (size_t j = offsets.size(); j-- > 0;)
                    offsets[j] = gap + offsets[j];
                S_VehicleTrack track;
                track.strip = strip;
                track.vertices = vertices;
                track.offsets = offsets;
                track.wheel = static_cast<uint16_t>(k);
                tracks.push_back(std::move(track));
                wheel->unknown1E0 += 0.1f;
            }
        }
    };
    addTracks("STRIPL*", 0, m_tracksL);
    addTracks("STRIPR*", 1, m_tracksR);

    // The damage parts (every visual), then each seat's door and window learn theirs (the last
    // part with the frame).
    m_automatic = true;
    m_model->EnumFrames(AddVehiclePart, &m_parts, ENUMF_WILDMASK | FRAME_MASK_VISUAL, "*");
    SetupParts();
    for (size_t s = m_seats.size(); s-- > 0;) {
        S_VehicleDoor *door = m_seats[s].door;
        if (!door || !door->window)
            continue;
        for (size_t p = m_parts.size(); p-- > 0;) {
            if (m_parts[p].frame == door->window) {
                door->windowPart = static_cast<uint16_t>(p);
                break;
            }
        }
    }
    for (size_t s = m_seats.size(); s-- > 0;) {
        S_VehicleDoor *door = m_seats[s].door;
        if (!door || !door->frame)
            continue;
        for (size_t p = m_parts.size(); p-- > 0;) {
            if (m_parts[p].frame == door->frame) {
                door->doorPart = static_cast<uint16_t>(p);
                break;
            }
        }
    }
    SetupLights();

    // The default parameters (a 1.4 t car: 4 gears, 550..4600 rpm, rear-wheel drive, the front
    // axle flag 0x1, the last axle 0x4), applied without fixing.
    S_VehicleParams p = {};
    p.mass = 1400.0f;
    static const float u004[19] = {130.0f, 80.0f, 4200.0f, 4600.0f, 550.0f, 240.0f, 80.0f, 160.0f, 2800.0f,
                                   3200.0f, 100.0f, 0.24f, 340.0f, 0.25f, 0.6f, 550.0f, 550.0f, 2800.0f, 4.0f};
    std::copy(std::begin(u004), std::end(u004), p.unknown004);
    p.gearRatios[0] = 14.0f;
    p.gearRatios[1] = 14.0f;
    p.gearRatios[2] = 9.8f;
    p.gearRatios[3] = 6.5f;
    p.gearRatios[4] = 4.5f;
    p.shiftUpPercent[1] = 75.0f;
    p.shiftUpPercent[2] = 75.8f;
    p.shiftUpPercent[3] = 80.2f;
    p.shiftDownPercent[2] = 20.5f;
    p.shiftDownPercent[3] = 20.9f;
    p.shiftDownPercent[4] = 20.5f;
    static const float u110[13] = {0.6f, 1.63f, 1.2f, 1.4f, 1.2f, 1.4f, 40.0f, 30.0f, 10.0f, 60.0f, 100.0f,
                                   60.0f, 100.0f};
    std::copy(std::begin(u110), std::end(u110), p.unknown110);
    p.centreOfMass[0] = 0.5f;
    p.centreOfMass[1] = 0.2f;
    p.centreOfMass[2] = 0.5f;
    p.tankSize = 50.0f;
    p.unknown158 = 17.5f;
    p.unknown690 = 20;
    p.unknown121D = 0;
    p.unknown121E = 0;
    p.unknown121F = 0;
    p.unknown694 = 0.9f;
    p.unknown698 = 1;
    p.unknown69C = 20.0f;
    p.unknown6A0 = 1.0f;
    p.unknown6A4 = 0;
    p.unknown6A8 = 35.0f;
    p.unknown6AC = 0.2f;
    p.unknown6B0 = 1.4f;
    p.unknown6B4 = 90.0f;
    p.unknown6B8[0] = 0.0f;
    p.unknown6B8[1] = 3.0f;
    p.unknown6B8[2] = -4.0f;
    p.unknown6C4 = 2.0f;
    // Fix: the original fills every axle the model has; the block holds VEHICLE_AXLES.
    const int axles = std::min(m_axleCount, VEHICLE_AXLES);
    for (int a = 0; a < axles; ++a) {
        p.axleUnknown15C[a] = 50.0f;
        p.axleUnknown18C[a] = 0.2f;
        p.axleUnknown1BC[a] = 800.0f;
        p.axleUnknown1EC[a] = 2000.0f;
        p.axleUnknown21C[a] = -1.0f;
        p.axleUnknown24C[a] = -1.0f;
        p.axleUnknown27C[a] = 0.0f;
        p.axleUnknown2AC[a] = 0.0f;
        p.axleTyreType[a] = 0;
        p.axleUnknown30C[a] = 12.0f;
        p.axleUnknown33C[a] = 9.0f;
        p.axleUnknown36C[a] = 20.0f;
        p.axleUnknown39C[a] = 0.8f;
        p.axleUnknown3CC[a] = 0.5f;
        p.axleUnknown3FC[a] = 1.2f;
        p.axleUnknown42C[a] = 1.2f;
        p.axleUnknown45C[a] = 0.02f;
        p.axleUnknown48C[a] = 12.0f;
        p.axleUnknown4BC[a] = 0.8f;
        p.axleUnknown4EC[a] = 0.8f;
        p.axleUnknown51C[a] = 0.1f;
        p.axleUnknown54C[a] = 100.0f;
        if (a == 1)
            p.axleFlag2[1] = 1;
        else if (a == 0)
            p.axleFlag1[0] = 1;
        if (a == m_axleCount - 1)
            p.axleFlag4[a] = 1;
        p.axleLeftShare[a] = a < 1 ? 100.0f : 80.0f;
        p.axleRightShare[a] = a < 1 ? 100.0f : 80.0f;
        p.axleUnknown5DC[a] = 20.0f;
        p.axleUnknown60C[a] = 6.0f;
        p.axleUnknown63C[a] = 17.0f;
        p.axleFlag800[a] = 1;
    }
    p.unknown1220 = 1;
    p.unknown6C8[0] = 20.0f;
    p.unknown6C8[1] = 10.0f;
    p.unknown6C8[2] = 2.0f;
    p.unknown6C8[3] = 1.0f;
    p.unknown6C8[4] = 0.02f;
    p.gravity = 10.0f;
    p.inertia[0] = 2200.0f;
    p.inertia[1] = 1300.0f;
    p.inertia[2] = 2200.0f;
    p.unknown67C = 1.0f;
    p.unknown678 = 1200.0f;
    p.unknown680 = 1200.0f;
    p.unknown684 = 0.7f;
    p.unknown121C = 1;
    p.unknown688 = 0.32f;
    p.unknown68C = 1.0f;
    static const float u6DC[20] = {140.0f, 20.0f, 100.0f, 85.0f, 0.5f, 60.0f, 30.0f, 35.0f, 0.0f, 0.0f,
                                   100.0f, 20.0f, 100.0f, 75.0f, 100.0f, 30.0f, 100.0f, 50.0f, 15.0f, 60.0f};
    std::copy(std::begin(u6DC), std::end(u6DC), p.unknown6DC);
    static const float u72C[21] = {70.0f, 10.0f, 30.0f, 30.0f, 30.0f, 30.0f, 100.0f, 10.0f, 100.0f, 30.0f, 10.0f,
                                   10.0f, 30.0f, 30.0f, 10.0f, 100.0f, 0.0f, 5.0f, 5.0f, 5.0f, 80.0f};
    std::copy(std::begin(u72C), std::end(u72C), p.unknown72C);
    std::strcpy(p.glassTexture, "2ft19r.bmp");
    std::strcpy(p.glassTextureAlpha, "2ft19r+.bmp");
    static const float u7A0[59] = {
        50.0f, 10.0f, 100.0f, 80.0f, 20.0f, 5.0f, 5.0f, 5.0f,       // [0..7]
        50.0f, 10.0f, 100.0f, 20.0f, 5.0f, 5.0f, 5.0f,              // [8..14]
        50.0f, 10.0f, 100.0f, 20.0f, 5.0f, 5.0f, 5.0f,              // [15..21]
        50.0f, 10.0f, 100.0f, 20.0f, 10.0f, 5.0f, 10.0f,            // [22..28]
        50.0f, 10.0f, 100.0f, 20.0f, 10.0f, 5.0f, 5.0f,             // [29..35]
        50.0f, 10.0f, 100.0f, 20.0f, 10.0f, 5.0f, 5.0f,             // [36..42]
        50.0f, 10.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f,               // [43..49]
        4.0f, 50.0f, 0.3f, 0.2f, 5.0f, 10.0f, 2.5f, 0.0f, 20.0f,    // [50..58]
    };
    std::copy(std::begin(u7A0), std::end(u7A0), p.unknown7A0);
    p.unknown11A0[2] = 4.0f;
    p.unknown11A0[3] = 50.0f;
    p.unknown11A0[4] = 0.3f;
    p.unknown11A0[5] = 0.2f;
    p.unknown11B8[5] = 4.0f;
    p.unknown11B8[6] = 50.0f;
    p.unknown11B8[7] = 0.3f;
    p.unknown11B8[8] = 0.2f;
    return ApplyParams(p, 0);
}
