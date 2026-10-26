// A checkpoint: a point of a network with its links (see checkpoints.h).
#include "game/checkpoints.h"

#include <cmath>
#include <cstring>

namespace {

// The vectors of the direction bits (0x008aad10; the diagonals are not normalized).
const S_vector4 DIRECTION_VECTORS[8] = {               // (static init 0x005244b0 / 0x005244c0)
    {0.0f, 0.0f, 1.0f, 0.0f},  {1.0f, 0.0f, 1.0f, 0.0f},   {1.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f},  {-1.0f, 0.0f, -1.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 1.0f, 0.0f},
};

constexpr float COS_22_5 = 0.9238795f;
constexpr float SIN_22_5 = 0.38268343f;

} // namespace

S_Checkpoint::~S_Checkpoint()                           // 0x00524840 (thunk 0x00524610)
{
    if (!(flags & CHECKPOINT_SHARED_LINKS) && links) {
        delete[] links;
        flags &= ~CHECKPOINT_LINK_COUNT;
        links = nullptr;
    }
}

S_CheckpointLink *S_Checkpoint::GetLink(int index)      // 0x00524780
{
    if (static_cast<unsigned>(index) >= static_cast<unsigned>(GetLinkCount()))
        return nullptr;
    return &links[index];
}

uint8_t S_Checkpoint::FindLink(int16_t target) const    // 0x005247a0
{
    const int count = GetLinkCount();
    for (int i = 0; i < count; ++i) {
        if (static_cast<int16_t>(links[i].target) == target)
            return static_cast<uint8_t>(i);
    }
    return 0xFF;
}

float S_Checkpoint::GetLinkCost(const S_Checkpoint &to) const   // 0x005247e0
{
    const float dx = to.pos.x - pos.x;
    const float dy = to.pos.y - pos.y;
    const float dz = to.pos.z - pos.z;
    return (1.0f - (to.preference + preference) * (1.0f / 1024.0f)) * std::sqrt(dz * dz + dy * dy + dx * dx);
}

const uint8_t *S_Checkpoint::Read(const uint8_t *data, S_CheckpointLink *&storage)   // 0x00524620
{
    std::memcpy(&pos, data, 12);
    std::memcpy(&cell, data + 0x0C, 4);
    std::memcpy(&unknown10, data + 0x10, 2);
    preference = data[0x12];
    radius = data[0x13];
    std::memcpy(&flags, data + 0x14, 2);
    type = data[0x16];
    cover1 = data[0x17];
    cover2 = data[0x18];
    cover4 = data[0x19];
    unknown1A = data[0x1A];
    std::memcpy(values, data + 0x1B, 4);
    unknown1F = data[0x1F];
    const uint8_t *next = data + 0x20;

    // (Tests the flags just read, not the ones the links were made with.)
    if (!(flags & CHECKPOINT_SHARED_LINKS) && links) {
        delete[] links;
        links = nullptr;
    }
    flags |= CHECKPOINT_SHARED_LINKS;
    const int count = GetLinkCount();
    links = storage;
    storage += count;
    for (int i = 0; i < count; ++i) {
        std::memcpy(&links[i].target, next, 2);
        std::memcpy(&links[i].cost, next + 2, 2);
        next += 4;
    }
    if (flags & CHECKPOINT_SPARE_LINK) {
        ++storage;
        links[count].target = 0xFFFF;
        links[count].cost = 0;
    }
    return next;
}

void S_Checkpoint::ResetState()                         // 0x00524870
{
    flags &= ~(CHECKPOINT_FLAG_CAR_0200 | CHECKPOINT_FLAG_UNDER_CAR);
    unknown1A = 0;
    if ((flags & CHECKPOINT_SPARE_LINK) && (flags & CHECKPOINT_JOINED)) {
        const int count = GetLinkCount();
        if (count != 0) {
            links[count - 1].target = 0xFFFF;
            links[count - 1].cost = 0;
            --flags;
        }
        flags &= ~CHECKPOINT_JOINED;
    }
}

bool S_Checkpoint::AddJoinedLink(int16_t index, const S_Checkpoint *target)   // 0x005248c0
{
    if (!(flags & CHECKPOINT_SHARED_LINKS) || !(flags & CHECKPOINT_SPARE_LINK) || (flags & CHECKPOINT_JOINED) ||
        this == target)
        return false;
    if (FindLink(index) != 0xFF)
        return false;
    const int count = GetLinkCount();
    if (count == 0x20)
        return false;
    S_CheckpointLink &slot = links[count];
    if (slot.target != 0xFFFF || slot.cost != 0)
        return false;
    const float cost = GetLinkCost(*target);
    if (cost > 250.0f || cost < 0.001f)
        return false;
    slot.target = static_cast<uint16_t>(index);
    slot.cost = static_cast<uint16_t>(static_cast<int>(cost * 256.0f));
    flags = static_cast<uint16_t>((flags + 1) | CHECKPOINT_JOINED);
    return true;
}

bool S_Checkpoint::RemoveJoinedLink(int16_t index)      // 0x005249a0
{
    if (!(flags & CHECKPOINT_SHARED_LINKS) || !(flags & CHECKPOINT_SPARE_LINK) || !(flags & CHECKPOINT_JOINED))
        return false;
    const uint8_t link = FindLink(index);
    if (link == 0xFF || link != GetLinkCount() - 1)
        return false;
    links[link].target = 0xFFFF;
    links[link].cost = 0;
    flags = static_cast<uint16_t>((flags - 1) & ~CHECKPOINT_JOINED);
    return true;
}

uint8_t S_Checkpoint::GetCoverMask(int stances) const   // 0x00524b10
{
    if (stances == 1)
        return cover1;
    if (stances == 2)
        return cover2;
    if (stances == 4)
        return cover4;
    return cover1 | cover4 | cover2;
}

uint8_t S_Checkpoint::GetCoverStances(uint8_t direction, uint8_t stances) const   // 0x0045acf0
{
    uint8_t covered = 0;
    if ((stances & 1) && (cover1 & direction))
        covered = 1;
    if ((stances & 2) && (cover2 & direction))
        covered |= 2;
    if ((stances & 4) && (cover4 & direction))
        covered |= 4;
    return covered;
}

bool S_Checkpoint::GetValuePair(int set, float *first, float *second) const   // 0x00524b50
{
    auto value = [](uint8_t v) { return v == 0xFF ? -1.0f : v * 0.003937008f; };   // 1/254
    *first = -1.0f;
    *second = -1.0f;
    if (set == 0) {
        *first = value(values[1]);                      // +0x1C
        *second = value(values[3]);                     // +0x1E
    } else if (set == 1) {
        *first = value(values[0]);                      // +0x1B
        *second = value(values[2]);                     // +0x1D
    }
    return *first >= 0.0f && *second >= 0.0f;
}

int __stdcall S_Checkpoint::GetCell(float x)            // 0x00524750
{
    // The truncation rounds down only for x not a multiple of 10: -10 goes to cell -2. (The
    // product rounds to a float first: the game runs with Direct3D's single-precision FPU, and
    // the data's stored cells agree.)
    int cell = static_cast<int>(x * 0.1f);
    if (x < 0.0f)
        --cell;
    return cell;
}

uint8_t S_Checkpoint::GetDirectionBit(const S_vector &dir)   // 0x00524a30 (__fastcall)
{
    float x = dir.x;
    float z = dir.z;
    const float length = std::sqrt(z * z + x * x);
    if (length > 1e-8f) {
        const float scale = 1.0f / length;
        x *= scale;
        z *= scale;
    }
    if (z > COS_22_5)
        return 0x01;
    if (z > SIN_22_5)
        return x <= 0.0f ? 0x80 : 0x02;
    if (z <= -SIN_22_5) {
        if (z <= -COS_22_5)
            return 0x10;
        return x <= 0.0f ? 0x20 : 0x08;
    }
    return x <= 0.0f ? 0x40 : 0x04;
}

const S_vector4 *S_Checkpoint::GetDirectionVector(uint8_t bits)   // 0x005245c0 (__fastcall)
{
    for (int i = 0; i < 8; ++i) {
        if (bits & 1)
            return &DIRECTION_VECTORS[i];
        bits >>= 1;
    }
    return &DIRECTION_VECTORS[0];
}
