// Ladders (C_Ladder; original 0x005ea300..0x005ebde0).
#include "game/ladder.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/pending.h"                               // g_texts
#include "ls3d/i3d.h"

namespace {

constexpr int TEXT_LADDER = 0x1BF2;                     // the actor's name
constexpr uint8_t PROPERTIES_VERSION = 3;
constexpr int DEFAULT_MATERIAL = 25;

constexpr float TOP_MARGIN = 0.3f;                      // m the box reaches above the top
constexpr float TOP_SLAB_THICKNESS = 0.0075f;
constexpr float TOP_SLAB_BACK = 0.1f;                   // m the slab's back face moves back
constexpr float TOP_SLAB_DEPTH = 0.25f;

// The length from a square the caller summed, as the SDK's inlined helper: 0 when its bits are
// below 1e-8's, exactly 1 for a near-unit square, the square root otherwise.
float LengthOf(float square)
{
    int32_t bits;
    std::memcpy(&bits, &square, sizeof bits);
    if (bits < 0x322bcc77)
        return 0.0f;
    if (!(std::fabs(square - 1.0f) < 1e-08f))
        return std::sqrt(square);
    return 1.0f;
}

// S_vector::Normalize from a square the caller summed (each place sums its own way).
void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-08f)
        return;
    if (square >= 1e-08f) {
        const float scale = 1.0f / std::sqrt(square);
        v.x = v.x * scale;
        v.y = v.y * scale;
        v.z = v.z * scale;
    } else if (v.x != 0.0f) {
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    } else {
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
    }
}

// The inline dot product: z, then y, then x.
float Dot(const S_vector &a, const S_vector4 &b)
{
    return (a.z * b.z + a.y * b.y) + a.x * b.x;
}

// A level unit vector from `x` and `z` (y 0), normalised as CanUse's inline code does.
S_vector Level(float x, float z)
{
    S_vector v(x, 0.0f, z);
    NormalizeBy(v, v.z * v.z + v.x * v.x);
    return v;
}

S_CollisionVolume *MakeVolume(C_Ladder *ladder, I3D_frame *frame, const S_matrix *matrix, const S_matrix *invMatrix,
                              const I3D_bbox &box, float radius)
{
    auto *volume = new S_CollisionVolume();             // _aligned_malloc(0x60, 16) in the original
    volume->flags = COLLISION_SHAPE_BOX;
    volume->actor = ladder;
    volume->type = COLLISION_TYPE_LADDER;
    volume->frame = frame;
    const S_matrix &world = frame->GetMatrix();
    volume->center = world(3);
    volume->centerW = world.m[3][3];
    volume->matrix = matrix;
    volume->invMatrix = invMatrix;
    volume->radius = radius;
    volume->bbox = box;
    g_collisionWorld.AddVolume(volume);
    return volume;
}

}  // namespace

C_Ladder::C_Ladder() : C_Actor(ACTOR_LADDER)           // 0x005ea3d0
{
    m_name = g_texts.Get(TEXT_LADDER);
}

C_Ladder::~C_Ladder()                                   // 0x005ea590
{
    FreeVolumes();
}

void C_Ladder::FreeVolumes()
{
    if (m_volume) {
        g_collisionWorld.RemoveVolume(m_volume);
        delete m_volume;
        m_volume = nullptr;
    }
    if (m_topVolume) {
        g_collisionWorld.RemoveVolume(m_topVolume);
        delete m_topVolume;
        m_topVolume = nullptr;
    }
}

bool C_Ladder::SetFrame(I3D_frame *frame)               // 0x005ea5d0
{
    if (!frame)
        return false;
    I3D_frame *up = frame->FindChildFrame("up*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    I3D_frame *down = frame->FindChildFrame("down*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    I3D_frame *visual = frame->FindChildFrame("*", ENUMF_WILDMASK | FRAME_MASK_VISUAL);
    if (!visual)
        visual = frame;
    // (Without either end the original builds the message "TS: chyba - u zebriku '<name>'
    // nenalezen dumac 'up'" ('down') and drops it.)
    if (!up || !down)
        return false;
    const S_matrix &top = up->GetMatrix();
    m_top = {top.m[3][0], top.m[3][1], top.m[3][2], top.m[3][3]};
    const S_matrix &bottom = down->GetMatrix();
    m_bottom = {bottom.m[3][0], bottom.m[3][1], bottom.m[3][2], bottom.m[3][3]};
    const S_matrix &world = frame->GetMatrix();
    S_vector axis = world(2);
    NormalizeBy(axis, (axis.z * axis.z + axis.x * axis.x) + axis.y * axis.y);
    S_vector level(axis.x, 0.0f, axis.z);
    NormalizeBy(level, (level.x * level.x + level.y * level.y) + level.z * level.z);
    m_dir = {level.x, level.y, level.z, world.m[2][3]};
    m_materialId = DEFAULT_MATERIAL;
    // Fix: the user properties are not copied into a fixed buffer of 1028 bytes.
    std::string props = visual->GetUserProps();
    if (!props.empty()) {
        for (char &c : props)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (const char *matid = std::strstr(props.c_str(), "matid")) {
            int id = -1;
            const int read = std::sscanf(matid + std::strlen("matid"), "%d", &id);
            if (read != -1 && read > 0)
                m_materialId = id;
        }
    }
    return C_Actor::SetFrame(frame);
}

bool C_Ladder::WriteProperties(C_chunk &chunk)          // 0x005ebaf0
{
    chunk.Write(PROPERTIES_VERSION);
    chunk.Write(m_unknown170);
    chunk.Write(m_stepSound);
    chunk.Write(m_climbSound);
    chunk.Write(m_unknown17C);
    chunk.Write(m_unknown180);
    return true;
}

bool C_Ladder::ReadProperties(C_chunk &chunk)           // 0x005ebbd0
{
    int8_t version = 0;                                 // (the original goes on with a stack value)
    chunk.Read(version);
    chunk.Read(m_unknown170);
    chunk.Read(m_stepSound);
    chunk.Read(m_climbSound);
    if (version > 1)
        chunk.Read(m_unknown17C);
    if (version > 2)
        chunk.Read(m_unknown180);
    return true;
}

void C_Ladder::StartMission(bool server)                // 0x005ead20 (DS 0x0060b270)
{
    I3D_frame *parent = m_frame->GetParent();
    Activate(server);
    m_frame->LinkTo(parent, I3DLINK_KEEP_WORLD);
    if (m_started)
        return;
    m_started = true;
    I3D_bbox hierarchy;
    I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, m_frame);
    // The box at the frame's own scale; the world matrix without it.
    const S_vector scale = m_frame->GetScale();
    box.max.x = box.max.x * scale.x;
    box.max.y = scale.y * box.max.y;
    box.max.z = box.max.z * scale.z;
    box.min.x = box.min.x * scale.x;
    box.min.y = scale.y * box.min.y;
    box.min.z = box.min.z * scale.z;
    m_matrix = m_frame->GetMatrix();
    const float sx = 1.0f / scale.x, sy = 1.0f / scale.y, sz = 1.0f / scale.z;
    for (int c = 0; c < 3; ++c) {
        m_matrix.m[0][c] = sx * m_matrix.m[0][c];
        m_matrix.m[1][c] = sy * m_matrix.m[1][c];
        m_matrix.m[2][c] = sz * m_matrix.m[2][c];
    }
    m_invMatrix.Inverse(m_matrix);
    // The ladder's box reaches 0.3 m above its top.
    S_vector boxTop(0.0f, box.max.y, 0.0f);
    boxTop *= m_matrix;
    box.max.y = ((m_top.y - boxTop.y) + box.max.y) + TOP_MARGIN;
    m_volume = MakeVolume(this, m_frame, &m_matrix, &m_invMatrix, box, (box.max.y - box.min.y) + TOP_MARGIN);
    m_topVolume = nullptr;
    // A slab at its top, set back.
    const S_vector top = S_vector(m_top.x, m_top.y, m_top.z) * m_invMatrix;
    box.max.y = top.y;
    box.min.y = top.y - TOP_SLAB_THICKNESS;
    box.min.z = box.min.z - TOP_SLAB_BACK;
    box.max.z = box.min.z + TOP_SLAB_DEPTH;
    m_topVolume = MakeVolume(this, m_frame, &m_matrix, &m_invMatrix, box, m_volume->radius);
}

void C_Ladder::StopMission(bool server)                 // 0x005eb120
{
    Deactivate(server);
    if (!m_started)
        return;
    m_started = false;
    FreeVolumes();
    m_climbers.clear();
}

bool C_Ladder::CanUse(const S_vector4 &pos, const S_vector4 &dir, const S_vector &velocity, bool lookingUp) const   // 0x005eb1d0
{
    S_vector facing(dir.x, dir.y, dir.z);
    if (facing.y != 0.0f) {
        facing.y = 0.0f;
        NormalizeBy(facing, facing.z * facing.z + facing.x * facing.x);
    }
    S_vector move = velocity;
    if (move.y != 0.0f)
        move.y = 0.0f;
    const float speed = LengthOf((move.z * move.z + move.x * move.x) + move.y * move.y);
    if (speed != 0.0f) {
        const float scale = 1.0f / speed;
        move.x = move.x * scale;
        move.y = move.y * scale;
        move.z = move.z * scale;
    }
    const S_vector toTop(pos.x - m_top.x, pos.y - m_top.y, pos.z - m_top.z);
    const S_vector toBottom(pos.x - m_bottom.x, pos.y - m_bottom.y, pos.z - m_bottom.z);
    const float topSquare = (toTop.z * toTop.z + toTop.y * toTop.y) + toTop.x * toTop.x;
    const float bottomSquare = (toBottom.z * toBottom.z + toBottom.y * toBottom.y) + toBottom.x * toBottom.x;
    if (!(bottomSquare > topSquare)) {
        // At its bottom: looking up, at most 0.5 m above it, towards it and facing it.
        if (!lookingUp)
            return false;
        if (pos.y - m_bottom.y > 0.5f)
            return false;
        const S_vector towards = Level(m_top.x - pos.x, m_top.z - pos.z);
        const float placeLimit = m_unknown180 ? 0.5f : 0.85f;
        const float moveLimit = m_unknown180 ? 0.5f : 0.95f;
        if (Dot(towards, m_dir) < placeLimit)
            return false;
        if (Dot(move, m_dir) <= moveLimit)
            return false;
        return !(Dot(facing, m_dir) <= moveLimit);
    }
    // At its top: moving, not looking up, at most 0.3 m below it, past it, backwards.
    if (speed < 0.001)
        return false;
    if (lookingUp)
        return false;
    if (pos.y - m_top.y < -0.3f)
        return false;
    const S_vector away = Level(toBottom.x, toBottom.z);
    if (Dot(away, m_dir) < 0.8f)
        return false;
    if (Dot(move, m_dir) > -0.8f)
        return false;
    return !(std::fabs(Dot(facing, m_dir)) <= 0.8);
}

void C_Ladder::AddClimber(C_Human *human)               // 0x005eb8c0
{
    m_climbers.push_back(human);
}

void C_Ladder::RemoveClimber(C_Human *human)            // 0x005eba70
{
    for (size_t i = 0; i < m_climbers.size(); ++i) {
        if (m_climbers[i] == human) {
            m_climbers[i] = m_climbers.back();
            m_climbers.pop_back();
            return;
        }
    }
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_LadderCreator::Create()                      // 0x005ea330
{
    return new C_Ladder();                              // _aligned_malloc(0x190, 16) in the original
}

void C_LadderCreator::Destroy(C_Actor *actor)           // 0x005ea3b0
{
    delete actor;
}

static C_LadderCreator g_ladderCreator;                 // 0x008ac9d8

// Registers the creator (static initialiser 0x005ea310, thunk 0x005ea300).
static const bool g_ladderCreatorRegistered = [] {
    g_actorCreators[ACTOR_LADDER] = &g_ladderCreator;
    return true;
}();
