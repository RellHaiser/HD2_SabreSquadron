// C_Vehicle: the state saved with the game (original 0x004f7300 StateSize, 0x004f7650 SaveState,
// 0x004f8510 LoadState) and its steps: 0x004f4040 PackPosition, 0x004f4110 UnpackPosition,
// 0x004f41f0 PartKey, 0x004f42f0 FindPartByKey, 0x004e98c0 Replace. C_Car's SaveGame writes the
// block as chunk 10000 (its size first), its LoadGame hands it back.
//
// The block: the fixed part (S_VehicleState, 0x174 bytes), then per lamp a S_VehicleLampState, per
// dented part (one with a kept original) a S_VehiclePartState and its vertices' offsets from the
// original (in both detail levels: the vertex index above three 6-bit offsets of 2 cm), per seat
// a S_VehicleSeatState, per wheel a S_VehicleWheelState.
#include "game/vehicle.h"

#include <cstring>

#include "game/pending.h"                               // g_carScene
#include "ls3d/i3d.h"

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

S_vector4 Row(const S_matrix &m, int row)
{
    return {m.m[row][0], m.m[row][1], m.m[row][2], m.m[row][3]};
}

constexpr uint32_t STATE_VERSION = 10;
constexpr int POSITION_BITS = 0x20;                     // a lamp's place: 10 bits for x and y, 12 for z

#pragma pack(push, 4)
// The block's fixed part (the vehicle's fields as they are; the bytes last).
struct S_VehicleState {
    uint32_t version;                                   // +0x00 STATE_VERSION
    uint32_t size;                                      // +0x04 the whole block's (StateSize)
    float modelPos[3];                                  // +0x08
    float rot[4];                                       // +0x14 the model's turn: s, x, y, z
    float velocity[3];                                  // +0x24
    float turn[3];                                      // +0x30 +0x210
    int u554;                                           // +0x3C
    uint32_t u58C;                                      // +0x40
    uint32_t u5B0;                                      // +0x44
    uint32_t u5B4;                                      // +0x48
    float u5B8, u5BC, u5C0, u5C4, u5C8, u5CC;           // +0x4C
    uint32_t u5D0[3];                                   // +0x64
    uint32_t u5E0;                                      // +0x70
    float u5F4, u5F8;                                   // +0x74
    uint32_t u5FC;                                      // +0x7C
    uint32_t u600;                                      // +0x80
    uint32_t u604;                                      // +0x84
    float u60C, u610, u63C;                             // +0x88
    uint32_t u674;                                      // +0x94
    float u6E8, u6EC, u714, u718, u724;                 // +0x98
    float u1E0[3];                                      // +0xAC
    float handbrake;                                    // +0xB8
    float u220[3];                                      // +0xBC
    float u780, u78C, u790, u7C4, u85C, u860;           // +0xC8
    float engineRevs;                                   // +0xE0
    float u86C;                                         // +0xE4
    int gear, requestedGear;                            // +0xE8
    float u8E0, brake, u904, u90C, u908;                // +0xF0
    uint32_t u910;                                      // +0x104
    float currentMass, u934, steering, u94C, u958;      // +0x108
    float uF54, fuel, uF68;                             // +0x11C
    uint32_t uF6C, uF70;                                // +0x128
    float u270[3], u280[3];                             // +0x130
    float u2224;                                        // +0x148
    uint32_t lamps, parts, seats, wheels;               // +0x14C the records' counts
    uint8_t u2297, u2298, u2299, u229A, u229B, u229C;   // +0x15C
    uint8_t automatic, u229E, u22A0, u22A2, engineOn, u22C0, u22C1, u22C2;   // +0x162
    uint8_t specialVolumesOn;                           // +0x16A
    uint8_t volumesOut;                                 // +0x16B the main volumes not in the world
    uint8_t soundsOn, u228B, u228F, u2291, u2292;       // +0x16C
};
static_assert(sizeof(S_VehicleState) == 0x174, "state layout");
static_assert(offsetof(S_VehicleState, lamps) == 0x14C, "state layout");
static_assert(offsetof(S_VehicleState, u2292) == 0x170, "state layout");

struct S_VehicleLampState {                             // 0x0C bytes
    uint32_t pos;                                       // PackPosition(POSITION_BITS) of its place in the model
    uint32_t flags;
    float health;
};
struct S_VehiclePartState {                             // 0x14 bytes, then the offsets
    uint32_t key;                                       // PartKey
    float health;
    uint32_t moved[2];                                  // the offsets per detail level
    uint8_t state, unknown39;
    uint8_t pad[2];
};
struct S_VehicleSeatState {                             // 0x14 bytes
    float load;
    uint32_t flags;                                     // S_VehicleSeat::flags
    float doorAngle, doorTargetAngle;                   // (with a door)
    uint8_t doorFlags, doorPad;
    uint8_t pad[2];
};
struct S_VehicleWheelState {                            // 0x64 bytes
    float u070[3], u080[3], u0A0[3], u0B0[3], u0E0[3];
    float extension, u1B0, u1B4;
    int surfaceEntry, surface;
    float u1D4, spin;
    uint32_t flags;                                     // (WHEEL_SLIDING left out)
    float bend, health;
};
#pragma pack(pop)
static_assert(sizeof(S_VehicleLampState) == 0x0C && sizeof(S_VehiclePartState) == 0x14 &&
              sizeof(S_VehicleSeatState) == 0x14 && sizeof(S_VehicleWheelState) == 0x64, "state layout");

void Put3(float out[3], const S_vector &v)
{
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

S_vector Get3(const float in[3]) { return S_vector(in[0], in[1], in[2]); }

// A vertex's offset from the original in 2 cm steps (truncated), as the 6 bits keep it (out of
// -32..31 it wraps); and back from the low 6 bits of `bits`.
uint32_t OffsetBits(float from, float to)
{
    return static_cast<uint32_t>(static_cast<int>((to - from) * 50.0f)) & 0x3F;
}

float OffsetOf(uint32_t bits)
{
    const uint32_t low = bits & 0x3F;
    return static_cast<float>(static_cast<int8_t>((low & 0x20) ? low | 0xC0 : low)) * 0.02f;
}

// Whether vertex `i` moved from the original's (more than 1 cm).
bool Moved(const float *from, const float *to)
{
    const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    return (dz * dz + dy * dy) + dx * dx > 0.0001f;
}

const float *Vertex(const void *vertices, int i) { return static_cast<const float *>(vertices) + i * 8; }
float *Vertex(void *vertices, int i) { return static_cast<float *>(vertices) + i * 8; }

// The vertices of level `to` moved from `from`'s.
uint32_t CountMoved(I3D_mesh_level *from, I3D_mesh_level *to)
{
    const void *src = from->GetVertices(0);
    const void *dst = to->GetVertices(0);
    uint32_t moved = 0;
    for (int i = to->NumVertices(); i-- > 0;)
        moved += Moved(Vertex(src, i), Vertex(dst, i));
    from->UnlockVertices();
    to->UnlockVertices();
    return moved;
}

}  // namespace

// --- the steps ---------------------------------------------------------------------------------

uint32_t C_Vehicle::PackPosition(int bits, const S_vector &pos) const          // 0x004f4040
{
    const int xyBits = bits / 3;
    const int zBits = xyBits + bits % 3;
    const float x = pos.x + m_boxOffset.x;
    const float y = m_boxOffset.y + pos.y;
    const float z = m_boxOffset.z + pos.z;
    const float range = static_cast<float>((1 << xyBits) - 1);
    uint32_t packed = static_cast<uint32_t>(static_cast<int>(x / m_size.x * range)) << xyBits;
    packed |= static_cast<uint32_t>(static_cast<int>(y / m_size.y * range));
    packed <<= zBits;
    return packed | static_cast<uint32_t>(static_cast<int>(z / m_size.z * static_cast<float>((1 << zBits) - 1)));
}

S_vector C_Vehicle::UnpackPosition(int bits, uint32_t packed) const            // 0x004f4110
{
    const int xyBits = bits / 3;
    const int zBits = xyBits + bits % 3;
    const uint32_t xyRange = (1u << xyBits) - 1;
    const uint32_t zRange = (1u << zBits) - 1;
    const uint32_t z = packed & zRange;
    packed >>= zBits;
    const uint32_t y = packed & xyRange;
    const uint32_t x = (packed >> xyBits) & xyRange;
    return S_vector(static_cast<float>(x) / static_cast<float>(xyRange) * m_size.x - m_boxOffset.x,
                    static_cast<float>(y) / static_cast<float>(xyRange) * m_size.y - m_boxOffset.y,
                    static_cast<float>(z) / static_cast<float>(zRange) * m_size.z - m_boxOffset.z);
}

uint32_t C_Vehicle::PartKey(uint32_t part) const                               // 0x004f41f0
{
    const char *name = m_parts[part].frame->GetName();
    if (const char *dot = std::strrchr(name, '.'))
        name = dot + 1;
    int left = static_cast<int>(std::strlen(name));
    if (left == 0)
        return 0xFFFFFFFF;
    // The name's 4-byte words XORed (the last one zero-padded; a name under 4 bytes as it is).
    uint32_t key = 0;
    if (left < 4) {
        std::memcpy(&key, name, static_cast<size_t>(left));
        return key;
    }
    std::memcpy(&key, name, 4);
    name += 4;
    left -= 4;
    while (left > 0) {
        const int n = left < 4 ? left : 4;
        uint32_t word = 0;
        std::memcpy(&word, name, static_cast<size_t>(n));
        key ^= word;
        name += n;
        left -= n;
    }
    return key;
}

int C_Vehicle::FindPartByKey(uint32_t key) const                               // 0x004f42f0
{
    if (key == 0xFFFFFFFF)
        return -1;
    for (int i = static_cast<int>(m_parts.size()); i-- > 0;) {
        if (PartKey(static_cast<uint32_t>(i)) == key)
            return i;
    }
    return -1;
}

int C_Vehicle::FindMeshOriginal(const S_VehiclePart &part) const
{
    int original = static_cast<int>(m_meshOriginals.size());
    while (--original >= 0 && (m_modelFileName != m_meshOriginals[original].modelFile ||
                               m_meshOriginals[original].partName != part.name)) {
    }
    return original;
}

// --- saving --------------------------------------------------------------------------------------

uint32_t C_Vehicle::StateSize() const                                         // 0x004f7300
{
    uint32_t size = sizeof(S_VehicleState) + static_cast<uint32_t>(m_lamps.size()) * sizeof(S_VehicleLampState);
    uint32_t records = 0;
    for (size_t index = m_parts.size(); index-- > 0;) {
        const S_VehiclePart &part = m_parts[index];
        for (size_t k = m_meshOriginals.size(); k-- > 0;) {
            const S_VehicleMeshOriginal &original = m_meshOriginals[k];
            if (m_modelFileName != original.modelFile || original.partName != part.name)
                continue;
            ++records;
            size += CountMoved(original.mesh->GetLevel(0), part.mesh->GetLevel(0)) * 4;
            if (original.mesh->NumLevels() > 1)
                size += CountMoved(original.mesh->GetLevel(1), part.mesh->GetLevel(1)) * 4;
        }
    }
    return size + (static_cast<uint32_t>(m_wheelCount) * 5 + static_cast<uint32_t>(m_seats.size()) + records) * 0x14;
}

bool C_Vehicle::SaveState(void *buffer) const                                 // 0x004f7650
{
    auto &s = *static_cast<S_VehicleState *>(buffer);
    // (The bytes the original leaves unwritten are zero here.)
    std::memset(&s, 0, sizeof s);
    s.version = STATE_VERSION;
    s.size = StateSize();
    Put3(s.modelPos, XYZ(m_modelPos));
    const S_quat &rot = m_model->GetRot();
    s.rot[0] = rot.s;
    s.rot[1] = rot.v.x;
    s.rot[2] = rot.v.y;
    s.rot[3] = rot.v.z;
    Put3(s.velocity, XYZ(m_velocity));
    Put3(s.turn, m_unknown210);
    s.u228B = m_unknown228B;
    s.u554 = m_unknown554;
    s.u58C = m_unknown58C;
    s.u5B0 = m_unknown5B0;
    s.u5B4 = m_unknown5B4;
    s.u5B8 = m_unknown5B8;
    s.u5BC = m_unknown5BC;
    s.u5C0 = m_unknown5C0;
    s.u5C4 = m_unknown5C4;
    s.u5C8 = m_unknown5C8;
    s.u5CC = m_unknown5CC;
    std::memcpy(s.u5D0, m_unknown5D0, sizeof s.u5D0);
    s.u5E0 = m_unknown5E0;
    s.u5F4 = m_unknown5F4;
    s.u5F8 = m_unknown5F8;
    s.u5FC = m_unknown5FC;
    s.u600 = m_unknown600;
    s.u604 = m_unknown604;
    s.u60C = m_unknown60C;
    s.u610 = m_unknown610;
    s.u63C = m_unknown63C;
    s.u674 = m_unknown674;
    s.u228F = m_unknown228F;
    s.u2291 = m_driven;
    s.u2292 = m_unknown2292;
    s.u6E8 = m_unknown6E8;
    s.u6EC = m_unknown6EC;
    s.u714 = m_unknown714;
    s.u718 = m_unknown718;
    s.u724 = m_unknown724;
    Put3(s.u1E0, XYZ(m_unknown1E0));
    s.soundsOn = m_soundsOn;
    s.handbrake = m_handbrake;
    Put3(s.u220, XYZ(m_unknown220));
    s.u2297 = m_unknown2297;
    s.u2298 = m_unknown2298;
    s.u780 = m_unknown780;
    s.u78C = m_unknown78C;
    s.u790 = m_unknown790;
    s.u2299 = m_unknown2299;
    s.u229A = m_unknown229A;
    s.u229B = m_unknown229B;
    s.u229C = m_unknown229C;
    s.u7C4 = m_unknown7C4;
    s.automatic = m_automatic;
    s.u85C = m_unknown85C;
    s.u860 = m_unknown860;
    s.engineRevs = m_engineRevs;
    s.u229E = m_unknown229E;
    s.u86C = m_unknown86C;
    s.gear = m_gear;
    s.requestedGear = m_requestedGear;
    s.u8E0 = m_unknown8E0;
    s.brake = m_brake;
    s.u904 = m_unknown904;
    s.u90C = m_unknown90C;
    s.u908 = m_unknown908;
    s.u910 = m_unknown910;
    s.u22A0 = m_unknown22A0;
    s.currentMass = m_currentMass;
    s.u934 = m_unknown934;
    s.steering = m_steering;
    s.u94C = m_unknown94C;
    s.u958 = m_unknown958;
    s.u22A2 = m_unknown22A2;
    s.uF54 = m_unknownF54;
    s.engineOn = m_engineOn;
    s.fuel = m_fuel;
    s.u22C0 = m_unknown22C0;
    s.uF68 = m_unknownF68;
    s.uF6C = m_unknownF6C;
    s.uF70 = m_unknownF70;
    Put3(s.u270, m_unknown270);
    Put3(s.u280, m_unknown280);
    s.u2224 = m_unknown2224;
    s.u22C1 = m_unknown22C1;
    s.u22C2 = m_unknown22C2;
    s.specialVolumesOn = m_specialVolumesOn;
    s.volumesOut = !m_mainVolumesInWorld;

    // The lamps: their place in the model, flags and health.
    s.lamps = static_cast<uint32_t>(m_lamps.size());
    auto *lamps = reinterpret_cast<S_VehicleLampState *>(&s + 1);
    for (size_t i = m_lamps.size(); i-- > 0;) {
        const S_VehicleLamp &lamp = m_lamps[i];
        lamps[i].pos = PackPosition(POSITION_BITS, lamp.frame->GetWorldPos() * m_invModelMatrix);
        lamps[i].flags = lamp.flags;
        lamps[i].health = lamp.health;
    }

    // The dented parts: per part and kept original of its name, its key, state and health and the
    // vertices moved from the original (both levels).
    auto *cursor = reinterpret_cast<uint8_t *>(lamps + m_lamps.size());
    for (size_t index = 0; index < m_parts.size(); ++index) {
        const S_VehiclePart &part = m_parts[index];
        for (size_t k = m_meshOriginals.size(); k-- > 0;) {
            const S_VehicleMeshOriginal &original = m_meshOriginals[k];
            if (m_modelFileName != original.modelFile || original.partName != part.name)
                continue;
            ++s.parts;
            auto &record = *reinterpret_cast<S_VehiclePartState *>(cursor);
            std::memset(&record, 0, sizeof record);
            record.key = PartKey(static_cast<uint32_t>(index));
            record.state = part.state;
            record.unknown39 = part.unknown39;
            record.health = part.health;
            auto *offsets = reinterpret_cast<uint32_t *>(&record + 1);
            for (int level = 0; level < 2; ++level) {
                if (level == 1 && !(original.mesh->NumLevels() > 1))
                    break;
                I3D_mesh_level *from = original.mesh->GetLevel(level);
                I3D_mesh_level *to = part.mesh->GetLevel(level);
                const void *src = from->GetVertices(0);
                const void *dst = to->GetVertices(0);
                uint32_t &moved = record.moved[level];
                for (int i = to->NumVertices(); i-- > 0;)
                    moved += Moved(Vertex(src, i), Vertex(dst, i));
                uint32_t *out = offsets + moved;
                for (int i = to->NumVertices(); i-- > 0;) {
                    const float *a = Vertex(src, i), *b = Vertex(dst, i);
                    if (!Moved(a, b))
                        continue;
                    const uint32_t xy = (static_cast<uint32_t>(i) << 6 | OffsetBits(a[0], b[0])) << 6 |
                                        OffsetBits(a[1], b[1]);
                    *--out = xy << 6 | OffsetBits(a[2], b[2]);
                }
                offsets += moved;
                from->UnlockVertices();
                to->UnlockVertices();
            }
            cursor = reinterpret_cast<uint8_t *>(offsets);
        }
    }

    // The seats (with a door: its angles and flags), the wheels.
    s.seats = static_cast<uint32_t>(m_seats.size());
    auto *seats = reinterpret_cast<S_VehicleSeatState *>(cursor);
    for (size_t i = m_seats.size(); i-- > 0;) {
        const S_VehicleSeat &seat = m_seats[i];
        S_VehicleSeatState &record = seats[i];
        std::memset(&record, 0, sizeof record);
        record.load = seat.load;
        record.flags = seat.flags;
        if (const S_VehicleDoor *door = seat.door) {
            record.doorAngle = door->angle;
            record.doorTargetAngle = door->targetAngle;
            record.doorFlags = door->flags;
        }
    }
    s.wheels = static_cast<uint32_t>(m_wheelCount);
    auto *wheels = reinterpret_cast<S_VehicleWheelState *>(seats + m_seats.size());
    for (int i = m_wheelCount; i-- > 0;) {
        const S_VehicleWheel *w = m_wheels[i];
        S_VehicleWheelState &record = wheels[i];
        std::memset(&record, 0, sizeof record);
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        Put3(record.u070, w->unknown070);
        Put3(record.u080, w->unknown080);
        Put3(record.u0A0, w->unknown0A0);
        Put3(record.u0B0, w->unknown0B0);
        Put3(record.u0E0, w->unknown0E0);
        record.extension = w->extension;
        record.u1B0 = w->unknown1B0;
        record.u1B4 = w->unknown1B4;
        record.surfaceEntry = w->surfaceEntry;
        record.surface = w->surface;
        record.u1D4 = w->unknown1D4;
        record.spin = w->unknown1F0;
        record.bend = w->bend;
        record.health = w->health;
        record.flags = w->flags & ~WHEEL_SLIDING;
    }
    return true;
}

// --- loading -------------------------------------------------------------------------------------

void C_Vehicle::Replace()                                                     // 0x004e98c0
{
    m_model->Update();
    ReadModelPlace();
    // The turn and the movement Reset took into the body's space back into the world's.
    m_unknown210 *= m_unknown310;
    S_vector velocity = XYZ(m_velocity);
    velocity *= m_unknown310;
    SetXYZ(m_velocity, velocity);

    // The wheels: their place, contact under them at their radius, skid marks in the scene, off
    // the ground.
    for (int i = 0; i < m_wheelCount; ++i) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        w->worldPos = Row(w->frame->GetMatrix(), 3);
        const float down = -w->radius;
        SetXYZ(w->unknown050, S_vector(down * m_modelAxisY.x + w->worldPos.x, down * m_modelAxisY.y + w->worldPos.y,
                                       down * m_modelAxisY.z + w->worldPos.z));
        SetXYZ(w->unknown060, XYZ(w->unknown050) - XYZ(m_modelAxisY));
        for (size_t k = w->skidMarks.size(); k-- > 0;) {
            w->skidMarks[k].frame0->LinkTo(g_carScene->GetPrimarySector(), 0);
            w->skidMarks[k].frame1->LinkTo(g_carScene->GetPrimarySector(), 0);
        }
        w->ground = nullptr;
        w->flags &= ~(WHEEL_ON_GROUND | WHEEL_SLIDING | WHEEL_SPINNING | WHEEL_FLAG_1000000 | WHEEL_HIT);
    }
    m_unknown228E = 0;
    m_unknown908 = 1.0f;
    m_unknown90C = 1.0f;
    m_unknown2293 = 0;
    m_throttle = 0.0f;
    m_unknown700 = 0.0f;
    m_unknown2294 = 0;
    m_unknown2295 = 0;

    // The lamps' own and the projectors' objects back in the model's parent.
    for (size_t i = m_lamps.size(); i-- > 0;) {
        const S_VehicleLamp &lamp = m_lamps[i];
        if (!lamp.projector || (lamp.flags & VEHICLE_LAMP_SHARED))
            continue;
        lamp.projector->LinkTo(m_unknown544, 0);
        if (lamp.light)
            lamp.light->LinkTo(m_unknown544, 0);
    }
    for (size_t i = m_projectors.size(); i-- > 0;) {
        m_projectors[i].projector->LinkTo(m_unknown544, 0);
        if (m_projectors[i].light)
            m_projectors[i].light->LinkTo(m_unknown544, 0);
    }
    UpdateWheelSpin(1.0f);
    SetVolumesInWorld(true, true);
    m_brake = 0.0f;
    m_unknown700 = 0.0f;
    m_throttle = 0.0f;
    m_rearGroundVolume = nullptr;
    m_frontGroundVolume = nullptr;
    m_unknown4F8[1] = 0;
    m_unknown4F8[0] = 0;
    m_unknown22C0 = 1;
}

bool C_Vehicle::LoadState(const void *buffer)                                 // 0x004f8510
{
    const auto &s = *static_cast<const S_VehicleState *>(buffer);
    if (s.version != STATE_VERSION)
        return false;
    Reset(VEHICLE_RESET_UNLINK_SKID_MARKS);

    // The model's place and turn; one saved upside down (its y axis within 0.52 rad of straight
    // down) is rolled over onto its wheels.
    m_model->SetPosInline(Get3(s.modelPos));
    m_model->SetRot(S_quat(s.rot[0], s.rot[1], s.rot[2], s.rot[3]));
    m_model->Update();
    const S_matrix world = m_model->GetMatrix();
    if (world(1).AngleTo(S_vector(0.0f, -1.0f, 0.0f)) < 0.52f) {
        m_modelAxisZ = Row(world, 2);
        const S_vector y(-world.m[1][0], -world.m[1][1], -world.m[1][2]);
        SetXYZ(m_modelAxisY, y);
        const S_vector4 &z = m_modelAxisZ;
        SetXYZ(m_modelAxisX, S_vector(z.z * y.y - z.y * y.z, z.x * y.z - z.z * y.x, z.y * y.x - y.y * z.x));
        const S_vector4 &x = m_modelAxisX;
        SetXYZ(m_modelAxisZ, S_vector(y.z * x.y - x.z * y.y, y.x * x.z - x.x * y.z, x.x * y.y - y.x * x.y));
        S_vector up = XYZ(m_modelAxisY);
        up.Normalize();
        SetXYZ(m_modelAxisY, up);
        S_vector dir = XYZ(m_modelAxisZ);
        dir.Normalize();
        SetXYZ(m_modelAxisZ, dir);
        m_model->SetLocalDir3(dir, up);
        m_model->Update();
    }
    Replace();

    // The motion and the state's fields.
    SetXYZ(m_velocity, Get3(s.velocity));
    m_speed = XYZ(m_velocity).Magnitude();
    m_velocityDir = m_velocity;
    SetXYZ(m_velocityDir, XYZ(m_velocity).Normalized());
    m_unknown210 = Get3(s.turn);
    m_unknown228B = s.u228B;
    m_unknown554 = s.u554;
    m_unknown58C = s.u58C;
    m_unknown5B0 = s.u5B0;
    m_unknown5B4 = s.u5B4;
    m_unknown5B8 = s.u5B8;
    m_unknown5BC = s.u5BC;
    m_unknown5C0 = s.u5C0;
    m_unknown5C4 = s.u5C4;
    m_unknown5C8 = s.u5C8;
    m_unknown5CC = s.u5CC;
    std::memcpy(m_unknown5D0, s.u5D0, sizeof m_unknown5D0);
    m_unknown5E0 = s.u5E0;
    m_unknown5F4 = s.u5F4;
    m_unknown5F8 = s.u5F8;
    m_unknown5FC = s.u5FC;
    m_unknown600 = s.u600;
    m_unknown604 = s.u604;
    m_unknown60C = s.u60C;
    m_unknown610 = s.u610;
    m_unknown63C = s.u63C;
    m_unknown674 = s.u674;
    m_unknown228F = s.u228F;
    m_driven = s.u2291;
    m_unknown2292 = s.u2292;
    m_unknown6E8 = s.u6E8;
    m_unknown6EC = s.u6EC;
    m_unknown714 = s.u714;
    m_unknown718 = s.u718;
    m_unknown724 = s.u724;
    SetXYZ(m_unknown1E0, Get3(s.u1E0));
    m_soundsOn = s.soundsOn;
    m_handbrake = s.handbrake;
    SetXYZ(m_unknown220, Get3(s.u220));
    m_unknown2297 = s.u2297;
    m_unknown2298 = s.u2298;
    m_unknown780 = s.u780;
    m_unknown78C = s.u78C;
    m_unknown790 = s.u790;
    m_unknown2299 = s.u2299;
    m_unknown229A = s.u229A;
    m_unknown229B = s.u229B;
    m_unknown229C = s.u229C;
    m_unknown7C4 = s.u7C4;
    m_automatic = s.automatic;
    m_unknown85C = s.u85C;
    m_unknown860 = s.u860;
    m_engineRevs = s.engineRevs;
    m_unknown229E = s.u229E;
    m_unknown86C = s.u86C;
    m_gear = s.gear;
    m_requestedGear = s.requestedGear;
    m_unknown8E0 = s.u8E0;
    m_brake = s.brake;
    m_unknown904 = s.u904;
    m_unknown908 = s.u908;
    m_unknown90C = s.u90C;
    m_unknown910 = s.u910;
    m_unknown22A0 = s.u22A0;
    m_currentMass = s.currentMass;
    m_unknown934 = s.u934;
    m_steering = s.steering;
    m_unknown94C = s.u94C;
    m_unknown958 = s.u958;
    m_unknown22A2 = s.u22A2;
    m_unknownF54 = s.uF54;
    m_engineOn = s.engineOn;
    m_fuel = s.fuel;
    m_unknown22C0 = s.u22C0;
    m_unknownF68 = s.uF68;
    m_unknownF6C = s.uF6C;
    m_unknownF70 = s.uF70;
    m_unknown270 = Get3(s.u270);
    m_unknown280 = Get3(s.u280);
    m_unknown2224 = s.u2224;
    m_unknown22C1 = s.u22C1;
    m_unknown22C2 = s.u22C2;
    m_specialVolumesOn = s.specialVolumesOn;
    SetVolumesInWorld(s.volumesOut == 0, m_volume490InWorld);

    // The lamps: each record to the lamp nearest its place. Quirk kept: the search keeps the
    // record's own index, not the nearest lamp's (so record i goes to lamp i, as saved).
    const auto *lamps = reinterpret_cast<const S_VehicleLampState *>(&s + 1);
    for (int i = 0; i < static_cast<int>(s.lamps); ++i) {
        const S_vector pos = UnpackPosition(POSITION_BITS, lamps[i].pos);
        size_t best = m_lamps.size();
        float bestDistance = 1e11f;
        for (size_t j = m_lamps.size(); j-- > 0;) {
            const S_vector d = m_lamps[j].frame->GetWorldPos() * m_invModelMatrix - pos;
            const float distance = (d.z * d.z + d.y * d.y) + d.x * d.x;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = static_cast<size_t>(i);
            }
        }
        if (best < m_lamps.size()) {
            m_lamps[best].flags = lamps[i].flags;
            m_lamps[best].health = lamps[i].health;
        }
    }

    // The dented parts: found by key, their original (kept anew from the part's mesh when there is
    // none), health, state (glass: cracked by its health instead), damage shown, and the vertices
    // moved from the original.
    const auto *cursor = reinterpret_cast<const uint8_t *>(lamps + s.lamps);
    for (int r = 0; r < static_cast<int>(s.parts); ++r) {
        const auto &record = *reinterpret_cast<const S_VehiclePartState *>(cursor);
        const auto *offsets = reinterpret_cast<const uint32_t *>(&record + 1);
        cursor = reinterpret_cast<const uint8_t *>(offsets + record.moved[0] + record.moved[1]);
        const int index = FindPartByKey(record.key);
        if (index < 0)
            continue;
        S_VehiclePart &part = m_parts[index];
        int original = FindMeshOriginal(part);
        if (original >= 0) {
            ++m_meshOriginals[original].refs;
        } else {
            S_VehicleMeshOriginal kept;
            kept.refs = 1;
            kept.partName = part.name;
            kept.modelFile = m_modelFileName;
            kept.mesh = I3DGetDriver()->CreateMesh();
            kept.mesh->Duplicate(part.mesh);
            for (int level = static_cast<int>(kept.mesh->NumLevels()) - 1; level > 1; --level)
                kept.mesh->SetLevel(nullptr, level);
            m_meshOriginals.push_back(kept);
            original = static_cast<int>(m_meshOriginals.size()) - 1;
        }
        part.health = record.health;
        // Quirk kept: a glass part's state is not restored (CrackGlass sets what it sets; a dented
        // glass is not marked deformed, so RepairParts leaves its dents).
        if (part.type == VEHICLE_PART_GLASS) {
            CrackGlass(static_cast<uint32_t>(index), S_vector(0.0f, -10.0f, 0.0f));
        } else {
            part.state = record.state;
            part.unknown39 = record.unknown39;
        }
        ShowPartDamage(static_cast<uint32_t>(index), nullptr, false);
        if ((part.state & VEHICLE_PART_STATE_1) && part.type != VEHICLE_PART_BODY)
            part.frame->SetOn(false);

        for (int level = 0; level < 2; ++level) {
            I3D_mesh *originalMesh = m_meshOriginals[original].mesh;
            if (level == 1 && !(originalMesh->NumLevels() > 1))
                break;
            I3D_mesh_level *from = originalMesh->GetLevel(level);
            I3D_mesh_level *to = part.mesh->GetLevel(level);
            const void *src = from->GetVertices(0);
            void *dst = to->GetVertices(0);
            const uint32_t *moved = offsets + (level ? record.moved[0] : 0);
            for (uint32_t k = record.moved[level]; k-- > 0;) {
                // (The original checks the record's position against the vertex count, not the
                // vertex index.)
                if (!(k < static_cast<uint32_t>(to->NumVertices())))
                    continue;
                const uint32_t entry = moved[k];
                const uint32_t i = entry >> 18;
                if (i >= static_cast<uint32_t>(to->NumVertices()))   // Fix: an index past the vertices
                    continue;
                const float *a = Vertex(src, static_cast<int>(i));
                float *b = Vertex(dst, static_cast<int>(i));
                b[0] = OffsetOf(entry >> 12) + a[0];
                b[1] = OffsetOf(entry >> 6) + a[1];
                b[2] = OffsetOf(entry) + a[2];
            }
            from->UnlockVertices();
            to->UnlockVertices();
            static_cast<I3D_object_visual *>(part.frame)->UnknownAC(level);
            part.mesh->GetLevel(level)->Unknown2C();
            part.mesh->ComputeBounds();
        }
        part.frame->Update();
    }

    // The seats and their doors, the wheels.
    const auto *seats = reinterpret_cast<const S_VehicleSeatState *>(cursor);
    for (int i = static_cast<int>(s.seats); i-- > 0;) {
        if (i >= static_cast<int>(m_seats.size()))
            continue;
        S_VehicleSeat &seat = m_seats[i];
        seat.flags = seats[i].flags;
        seat.load = seats[i].load;
        if (S_VehicleDoor *door = seat.door) {
            door->angle = seats[i].doorAngle;
            door->targetAngle = seats[i].doorTargetAngle;
            door->flags = seats[i].doorFlags;
        }
    }
    const auto *wheels = reinterpret_cast<const S_VehicleWheelState *>(seats + s.seats);
    for (int i = static_cast<int>(s.wheels); i-- > 0;) {
        if (i >= m_wheelCount)
            continue;
        S_VehicleWheel *w = m_wheels[i];
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        const S_VehicleWheelState &record = wheels[i];
        w->unknown070 = Get3(record.u070);
        w->unknown080 = Get3(record.u080);
        w->unknown090 = w->unknown080;
        w->unknown0A0 = Get3(record.u0A0);
        w->unknown0B0 = Get3(record.u0B0);
        w->unknown0E0 = Get3(record.u0E0);
        w->extension = record.extension;
        w->unknown1B0 = record.u1B0;
        w->unknown1B4 = record.u1B4;
        w->surfaceEntry = record.surfaceEntry;
        w->surface = record.surface;
        w->unknown1D4 = record.u1D4;
        w->unknown1F0 = record.spin;
        w->bend = record.bend;
        w->health = record.health;
        w->flags = record.flags;
        w->unknown168 = w->extension * w->unknown160;
        w->holder->SetPosInline(S_vector(w->holderPos.x, w->holderPos.y - w->extension, w->holderPos.z));
        w->holder->Update();
        w->worldPos = Row(w->frame->GetMatrix(), 3);
        const float r = w->radius;
        SetXYZ(w->unknown050, S_vector(w->worldPos.x - r * m_modelAxisY.x, w->worldPos.y - m_modelAxisY.y * r,
                                       w->worldPos.z - m_modelAxisY.z * r));
        w->unknown060 = w->unknown050;
        if (w->flags & (WHEEL_DETACHED | WHEEL_DUMMY))
            w->frame->SetOn(false);
    }
    return true;
}
