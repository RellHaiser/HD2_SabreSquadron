// Destructible scene objects (C_AnimationActor; original 0x005b3100..0x005b66e0).
#include "game/animation_actor.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/human.h"
#include "game/particles.h"
#include "game/pending.h"                               // g_gameRole, g_texts
#include "game/resource_cache.h"                        // g_modelCache
#include "game/shots.h"                                 // g_materials
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"

namespace {

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// Saved-game chunks (one of the first three, then the held flag).
constexpr uint16_t CHUNK_INTACT = 1000;
constexpr uint16_t CHUNK_DESTROYED = 2000;               // the animation over
constexpr uint16_t CHUNK_ANIMATING = 3000;               // the animation's time
constexpr uint16_t CHUNK_HELD = 4000;

// ReadProperties' chunks (see g_animationActorProperties).
enum : uint16_t {
    PROPERTY_ANIM_NAME = 0,
    PROPERTY_HITPOINTS = 1,
    PROPERTY_PERMANENT_REFRESH = 2,
    PROPERTY_EXPLOSION_SOUND = 3,
    PROPERTY_PARTICLE = 4,
    PROPERTY_COLLISION_FRAME = 5,
    PROPERTY_NAME_TEXT = 6,
    PROPERTY_SHOW_NO_INFO = 7,
};

constexpr uint32_t FIND_ANY_FRAME = 0xFFFF;
constexpr uint32_t ENUM_STOP = 2;                       // EnumFrames: no further frames
constexpr uint32_t COLLISION_TYPE_ANIMATION = 0x400000; // the boxes' volume type
constexpr int LOAD_REFRESHES = 2;                       // collision refreshes after a load
constexpr uint32_t HEAVY_ARMOUR_COST = 1500;            // penetration costs over these
constexpr uint32_t LIGHT_ARMOUR_COST = 505;
constexpr float WING_MIN_MOVE_SQUARE = 0.04f;           // m²: a wing is swept after moving 0.2 m
constexpr uint32_t WING_EXCLUDE_TYPES = 0x6FD800;       // (its own boxes, doors, mines, ...)
constexpr float WING_HIT = 1000000.0f;                  // what a wing deals a car
constexpr float WING_SELF_HIT = 100000.0f;              // and the actor itself then

// The number of a "matid<n>" user property (0 when there is none).
int ScanInt(const char *text)
{
    int value = 0;
    std::sscanf(text, "%d", &value);
    return value;
}

uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The frame's actor, else its model's (0x006de0d0).
C_Actor *FrameOrModelActor(const I3D_frame *frame)
{
    if (C_Actor *actor = GetFrameActor(frame))
        return actor;
    const I3D_frame *model = frame->GetOwnerModel();
    if (model && model->GetType() == FRAME_MODEL)
        return GetFrameActor(model);
    return nullptr;
}

// A model file's name: the "Anim name" without its extension.
std::string StripExtension(const char *name)
{
    std::string file = name;                            // (the original copies into a fixed buffer)
    const size_t dot = file.rfind('.');
    if (dot != std::string::npos)
        file.erase(dot);
    return file;
}

bool s_loading;                                         // 0x008ac598 LoadGame runs (Stop stays quiet)

}  // namespace

// --- the collision boxes ---------------------------------------------------------------------------

C_AnimationActor::S_Collision::S_Collision(I3D_frame *frame, uint32_t flags, uint32_t type, C_Actor *actor)   // 0x005b4850
{
    volume.actor = actor;
    volume.frame = frame;
    volume.type = type;
    volume.flags = flags;
    Refresh(true);
}

C_AnimationActor::S_Collision::~S_Collision()           // 0x005b4dc0
{
    if (volume.frame)
        g_collisionWorld.RemoveVolume(&volume);
}

void C_AnimationActor::S_Collision::Refresh(bool add)   // 0x005b48b0 (DS 0x005c6670)
{
    I3D_frame *frame = volume.frame;
    if (!frame)
        return;
    frame->Update();
    const S_vector scale = frame->GetScale();
    I3D_bbox box;
    // Fix: the centre starts at 0; the original leaves it unset for a frame without geometry.
    S_vector center(0.0f, 0.0f, 0.0f);
    float halfDiagonal = 0.0f;
    if (frame->GetType() == FRAME_DUMMY) {
        box = static_cast<I3D_dummy *>(frame)->GetBBox();
        const float sumX = box.min.x + box.max.x;
        const float sumY = box.min.y + box.max.y;
        const float sumZ = box.min.z + box.max.z;
        center = S_vector(sumX * 0.5f, sumY * 0.5f, sumZ * 0.5f);
    } else {
        I3D_bbox hierarchy;
        box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, frame);
        if (box.min.x <= box.max.x) {
            const S_vector half = (box.max - box.min) * 0.5f;
            center = box.min + half;
            halfDiagonal = std::sqrt((half.z * half.z + half.y * half.y) + half.x * half.x);
        }
    }
    if (add) {
        // The material from a "matid<n>" user property.
        const char *props = frame->GetUserProps();
        if (std::strncmp("matid", props, 5) == 0 && std::strlen(props) > 5)
            volume.flags |= static_cast<uint32_t>(ScanInt(props + 5)) << 24;
    }
    box.max.x *= scale.x;
    box.max.y *= scale.y;
    box.max.z *= scale.z;
    box.min.x *= scale.x;
    box.min.y *= scale.y;
    box.min.z *= scale.z;
    // (The original computes the centre twice, before and after the radius.)
    volume.center = center * frame->GetMatrix();
    volume.bbox = box;
    // The world matrix without the frame's scale, and its inverse.
    matrix = frame->GetMatrix();
    const float kx = 1.0f / scale.x;
    matrix.m[0][0] = kx * matrix.m[0][0];
    matrix.m[0][1] = kx * matrix.m[0][1];
    matrix.m[0][2] = kx * matrix.m[0][2];
    const float ky = 1.0f / scale.y;
    matrix.m[1][0] = ky * matrix.m[1][0];
    matrix.m[1][1] = ky * matrix.m[1][1];
    matrix.m[1][2] = ky * matrix.m[1][2];
    const float kz = 1.0f / scale.z;
    matrix.m[2][0] = kz * matrix.m[2][0];
    matrix.m[2][1] = kz * matrix.m[2][1];
    matrix.m[2][2] = kz * matrix.m[2][2];
    invMatrix = matrix;
    volume.matrix = &matrix;
    invMatrix.Inverse(matrix);
    volume.invMatrix = &invMatrix;
    if (frame->GetType() == FRAME_DUMMY) {
        // Half the box's diagonal.
        const float dx = box.max.x - box.min.x;
        const float dy = box.max.y - box.min.y;
        const float dz = box.max.z - box.min.z;
        const float sq = (dz * dz + dy * dy) + dx * dx;
        float length;
        if (static_cast<int32_t>(FloatBits(sq)) < static_cast<int32_t>(FloatBits(1e-8f)))
            length = 0.0f;
        else if (!(std::fabs(sq - 1.0f) < 1e-8f))
            length = std::sqrt(sq);
        else
            length = 1.0f;
        volume.radius = length * 0.5f;
    } else {
        // The hierarchy's half diagonal times the largest world scale.
        const S_vector world = frame->GetWorldScale();
        const float yz = !(world.y > world.z) ? world.z : world.y;
        const float largest = !(world.x > yz) ? yz : world.x;
        volume.radius = largest * halfDiagonal;
    }
    if (add)
        g_collisionWorld.AddVolume(&volume);
    g_collisionWorld.MoveVolume(&volume);
}

// --- the wings ---------------------------------------------------------------------------------------

C_AnimationActor::S_Wing::S_Wing(I3D_frame *frame) : frame(frame)   // 0x005b5f40
{
    frame->AddRef();
    Reset();
}

C_AnimationActor::S_Wing::~S_Wing()                     // 0x005b5f60
{
    frame->Release();
}

void C_AnimationActor::S_Wing::Reset()                  // 0x005b5f70
{
    const I3D_bound_volume &bound = frame->GetBound();
    lastPos = {bound.center.x, bound.center.y, bound.center.z, bound.radius};
}

bool C_AnimationActor::S_Wing::Sweep()                  // 0x005b5fb0
{
    const I3D_bound_volume &bound = frame->GetBound();
    const S_vector4 pos = {bound.center.x, bound.center.y, bound.center.z, bound.radius};
    const S_vector move(pos.x - lastPos.x, pos.y - lastPos.y, pos.z - lastPos.z);
    if (!((move.z * move.z + move.y * move.y) + move.x * move.x > WING_MIN_MOVE_SQUARE))
        return false;
    const S_vector from(lastPos.x, lastPos.y, lastPos.z);
    bool car = false;
    // (The original also tests the flags against 0xC0, which the dynamic flag already passes.)
    S_CollisionVolume *hit = CollisionSphereTest(from, move, pos.w, nullptr, nullptr, WING_EXCLUDE_TYPES, 0xFFFFFFFF);
    if (hit && (hit->flags & COLLISION_FLAG_DYNAMIC)) {
        if (C_Actor *actor = FrameOrModelActor(hit->frame)) {
            if (actor->GetType() == ACTOR_HUMAN) {
                C_Actor *vehicle = static_cast<C_Human *>(actor)->GetVehicle();
                if (vehicle && vehicle->GetType() == ACTOR_CAR) {
                    vehicle->Hit(HIT_EXPLOSION, WING_HIT, from, move, nullptr, 0);
                    actor->Hit(HIT_EXPLOSION, WING_HIT, from, move, nullptr, 0);
                    car = true;
                } else {
                    actor->Hit(HIT_EXPLOSION, WING_HIT, from, move, nullptr, 0);   // (not a car)
                }
            } else if (actor->GetType() == ACTOR_CAR) {
                car = true;
                actor->Hit(HIT_EXPLOSION, WING_HIT, from, move, nullptr, 0);
            }
        }
    }
    lastPos = pos;
    return car;
}

// --- the actor ------------------------------------------------------------------------------------

C_AnimationActor::C_AnimationActor() : C_Actor(ACTOR_ANIMATION), m_animName(""), m_collisionFrameName("")   // 0x005b3740
{
    ReleaseModel();
}

C_AnimationActor::~C_AnimationActor()                   // 0x005b3930
{
    ReleaseModel();
    // Fix: wings left by a missing StopMission are let go (the original only frees the array).
    for (S_Wing *wing : m_wings)
        delete wing;
}

bool C_AnimationActor::SetFrame(I3D_frame *frame)       // 0x005b3710
{
    return C_Actor::SetFrame(frame) && frame && frame->GetType() == FRAME_MODEL;
}

void C_AnimationActor::CopyFrom(const C_Actor *source)  // 0x005b3a00
{
    C_Actor::CopyFrom(source);
    // (The name's text id is not copied.)
    const auto *other = static_cast<const C_AnimationActor *>(source);
    m_animName = other->m_animName;
    m_collisionFrameName = other->m_collisionFrameName;
    m_permanentRefresh = other->m_permanentRefresh;
    m_showNoInfo = other->m_showNoInfo;
    m_hitPoints = other->m_hitPoints;
    m_explosionSound = other->m_explosionSound;
    m_particleId = other->m_particleId;
}

void C_AnimationActor::RestartMission(bool)             // 0x005b31c0
{
    if (IsServer())
        StopMission(true);
    if (IsClient())
        StopMission(false);
    if (IsServer())
        StartMission(true);
    if (IsClient())
        StartMission(false);
}

void C_AnimationActor::StartMission(bool server)        // 0x005b3be0
{
    m_animating = false;
    m_destroy = false;
    m_damage = 0.0f;
    m_animationHeld = false;
    m_refreshes = 0;
    m_name = m_nameTextId == -1 ? C_str("") : g_texts.Get(m_nameTextId);
    Activate(server);
    // (On a listen server the client side does the rest.)
    if (server && IsClient())
        return;
    m_collisionFrame = nullptr;
    m_particleFrame = m_frame->FindChildFrame("particle", FIND_ANY_FRAME);
    CreateCollisions(m_frame);

    // The animated copy: "Anim name" without its extension, through the model cache.
    m_model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (g_modelCache.OpenInto(m_model, StripExtension(m_animName).c_str()) != I3D_OK) {
        m_model->Release();
        m_model = nullptr;
        return;
    }
    m_model->SetCallback(NoteProc, I3DCALLBACK_ANIM_NOTE);
    for (int i = m_model->NumChildren(); i-- > 0;)
        m_model->GetChild(i)->SetCallback(NoteProc, I3DCALLBACK_ANIM_NOTE);
    // In the frame's place, beside it, hidden.
    m_model->SetPosInline(m_frame->GetPos());
    m_model->SetRot(m_frame->GetRot());
    m_model->SetScale(m_frame->GetScale());
    const C_fstr name("%s#%s", m_frame->GetName(), "aanim");
    m_model->LinkTo(m_frame->GetParent(), 0);
    m_model->SetName(name);
    m_model->SetOn(false);
    m_model->Update();

    m_frame->EnumFrames(ArmourProc, this, FIND_ANY_FRAME, nullptr);
    if (m_frame->GetType() != FRAME_MODEL)
        return;
    std::string file = static_cast<I3D_model *>(m_frame)->GetFileName();   // (the original copies into a fixed buffer)
    for (char &c : file)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (file != "#mchescape")
        return;
    if (m_frame->FindChildFrame("right_wing", FIND_ANY_FRAME))
        m_wings.push_back(new S_Wing(m_frame->FindChildFrame("right_wing", FIND_ANY_FRAME)));
    if (m_frame->FindChildFrame("left_wing", FIND_ANY_FRAME))
        m_wings.push_back(new S_Wing(m_frame->FindChildFrame("left_wing", FIND_ANY_FRAME)));
}

void C_AnimationActor::StopMission(bool server)         // 0x005b5000 (DS 0x005c6fc0)
{
    Deactivate(server);
    if (server && IsClient())
        return;
    m_animating = false;
    m_stopped = false;
    for (size_t i = m_wings.size(); i-- > 0;)
        delete m_wings[i];
    m_wings.clear();
    if (m_collisionFrame) {
        EnablePrimitives(m_collisionFrame, true);
        m_collisionFrame = nullptr;
    }
    ReleaseModel();
}

void C_AnimationActor::ReleaseModel()                   // 0x005b4790
{
    DeleteCollisions();
    if (m_model) {
        m_model->LinkTo(nullptr, 0);
        m_model->Release();
        m_model = nullptr;
    }
    if (m_frame) {
        m_frame->SetOn(true);
        m_frame->Update();
    }
    m_damage = 0.0f;
}

void C_AnimationActor::DeleteCollisions()
{
    for (size_t i = m_collisions.size(); i-- > 0;)
        delete m_collisions[i];
    m_collisions.clear();
}

void C_AnimationActor::CreateCollisions(I3D_frame *frame)   // 0x005b3ac0
{
    DeleteCollisions();
    if (!frame)
        return;
    for (int i = 0;; ++i) {
        I3D_frame *box = frame->FindChildFrame(C_fstr("BODYCOLB%02d", i), FIND_ANY_FRAME);
        if (!box)
            break;
        AddCollision(box, COLLISION_SHAPE_BOX, COLLISION_TYPE_ANIMATION);
    }
}

void C_AnimationActor::SetCollisionsOn(bool on)         // 0x005b54c0
{
    if (on)
        CreateCollisions(m_frame);
    else
        DeleteCollisions();
}

void C_AnimationActor::AddCollision(I3D_frame *frame, uint32_t flags, uint32_t type)   // 0x005b4de0
{
    if (frame)
        m_collisions.push_back(new S_Collision(frame, flags, type, this));
}

void C_AnimationActor::EnablePrimitives(I3D_frame *frame, bool enable)   // 0x005b5550
{
    std::vector<void *> primitives;
    const int count = g_collisionWorld.GetFramePrimitives(frame, primitives);
    for (int i = 0; i < count; ++i) {
        uint32_t &flags = *static_cast<uint32_t *>(primitives[i]);
        flags = enable ? flags & ~COLLISION_FLAG_DISABLED : flags | COLLISION_FLAG_DISABLED;
    }
}

uint32_t __stdcall C_AnimationActor::ArmourProc(I3D_frame *frame, void *context)   // 0x005b45b0
{
    auto *actor = static_cast<C_AnimationActor *>(context);
    const uint32_t cost = g_materials[actor->GetMaterialId(frame)].penetrationCost;
    if (cost > HEAVY_ARMOUR_COST) {
        actor->m_armour = ANIMATION_ARMOUR_HEAVY;
        return ENUM_STOP;
    }
    if (cost > LIGHT_ARMOUR_COST) {
        actor->m_armour = ANIMATION_ARMOUR_LIGHT;
        return ENUM_STOP;
    }
    return 0;
}

int __stdcall C_AnimationActor::NoteProc(I3D_frame *frame, uint32_t message, uintptr_t note, uintptr_t)   // 0x005b4610
{
    const char *text = reinterpret_cast<const char *>(note);
    if (!(message & I3DCALLBACK_ANIM_NOTE) || !text)
        return 0;
    S_SoundRequest sound;
    std::string copy;                                   // (Fix: the original copies into 248 bytes unchecked)
    if (text[0] == '$') {
        // "$<file> <near range> <far range>"
        copy = text;
        char *file = &copy[1];
        char *space = std::strchr(file, ' ');
        if (!space)
            return 0;
        *space = '\0';
        char *range = space + 1;
        char *space2 = std::strchr(range, ' ');
        if (!space2)
            return 0;
        *space2 = '\0';
        std::sscanf(range, "%f", &sound.nearRange);
        std::sscanf(space2 + 1, "%f", &sound.farRange);
        sound.fileName = file;
        sound.attachTo = frame;
    } else if (text[0] == '@') {
        // "@<group>" of the action sounds
        sound.group = std::atoi(text + 1);
        sound.attachTo = frame;
        sound.chapter = SOUND_CHAPTER_ACTION;
    } else {
        return 0;
    }
    sound.flags = SOUND_SILENT_TO_AI;
    sound.source = nullptr;
    GetSoundModule()->Play(sound);
    return 0;
}

void C_AnimationActor::Stop(bool immediately)           // 0x005b55e0
{
    if (m_stopped)
        return;
    // The static collision of the frame named off.
    const char *collisionName = m_collisionFrameName;
    if (collisionName[0]) {
        m_collisionFrame = GetGame()->GetScene()->FindFrame(collisionName, FIND_ANY_FRAME);
        if (m_collisionFrame)
            EnablePrimitives(m_collisionFrame, false);
    }
    DeleteCollisions();
    if (IsClient() && !immediately && !s_loading) {
        if (m_explosionSound != -1) {
            S_SoundRequest sound;
            sound.flags = SOUND_SILENT_TO_AI;
            sound.attachTo = m_particleFrame ? m_particleFrame : m_frame;
            sound.source = this;
            sound.chapter = SOUND_CHAPTER_EXPLOSION;
            sound.group = m_explosionSound;
            GetSoundModule()->Play(sound);
        }
        if (m_particleId != -1 && m_particleFrame)
            g_particlesModule.Create(m_particleId, m_particleFrame->GetMatrix(), nullptr, true, false);
    }
    if (!m_frame)
        return;
    if (m_model) {
        if (I3D_animation_set *animation = m_model->GetAnimation(0)) {
            animation->SetPlayMode(1);
            m_animating = true;
        }
        m_stopped = true;
        m_model->SetOn(true);
        m_model->Update();
    }
    m_frame->SetOn(false);
    m_frame->Update();
    if (IsServer() && !s_loading) {
        if (S_FrameData *data = m_frame->GetData()) {
            if (data->script)
                data->script->OnDeath();
        }
    }
    if (!IsLocalGame() && IsServer() && !s_loading) {
        C_BitStream *msg = CreateActorMessage(m_netId, true);
        msg->WriteBits(immediately ? 1 : 0, 1);
        SendActorMessage(msg);
    }
}

void C_AnimationActor::Finish()                         // 0x005b50c0
{
    DeleteCollisions();
    // Fix: without the model (it did not load) nothing runs; the original crashes.
    if (m_model) {
        if (I3D_animation_set *animation = m_model->GetAnimation(0)) {
            const int time = m_model->GetAnimTime(0);
            m_model->Tick(animation->GetEndTime() - time - 1);
        }
    }
    CreateCollisions(m_model);
    m_animating = false;
}

void C_AnimationActor::Step(int time)                   // 0x005b5190
{
    if (m_destroy) {
        m_destroy = false;
        Stop(false);
    }
    if (m_model && m_animating && m_model->GetAnimation(0)) {
        I3D_animation_set *animation = m_model->GetAnimation(0);
        const int now = m_model->GetAnimTime(0) + time;
        // (Compared unsigned.)
        if (!(static_cast<uint32_t>(now) < static_cast<uint32_t>(animation->GetEndTime())))
            Finish();
        else if (!m_animationHeld)
            m_model->Tick(time);
    }
    // The boxes follow their frames while it stands (always with the permanent refresh), and a
    // while after a load; so do the wings.
    if (!m_permanentRefresh || ((m_stopped || m_animating) && m_refreshes == 0))
        return;
    if (m_refreshes != 0)
        --m_refreshes;
    for (size_t i = m_collisions.size(); i-- > 0;)
        m_collisions[i]->Refresh(false);
    for (size_t i = m_wings.size(); i-- > 0;) {
        if (m_wings[i]->Sweep())
            Hit(HIT_EXPLOSION, WING_SELF_HIT, m_frame->GetWorldPos(), m_frame->GetWorldDir(), nullptr, 0);
    }
}

void C_AnimationActor::Update(int frameTime)            // 0x005b5490
{
    Step(frameTime);
}

void C_AnimationActor::Tick(int timeDelta)              // 0x005b54a0
{
    if (!IsClient())
        Step(timeDelta);
}

void C_AnimationActor::Hit(E_HitType, float amount, const S_vector &, const S_vector &, C_Actor *, int)   // 0x005b5eb0
{
    // (A NaN passes the tests, as in the original.)
    if (m_hitPoints <= 0.0f || m_stopped || m_damage < 0.0f)
        return;
    m_damage = amount + m_damage;
    if (!(m_damage > m_hitPoints))
        return;
    m_damage = -1.0f;
    m_destroy = true;
}

int C_AnimationActor::GetMaterialId(I3D_frame *part)    // 0x005b5f20 (DS 0x005c81b0)
{
    const int material = GetFrameMaterialId(part);
    return material < 0 ? 11 : material;
}

void C_AnimationActor::OnMessage(C_BitStream *msg)      // 0x005b58d0
{
    const bool immediately = msg->ReadBits(1) != 0;
    if (!IsServer())
        Stop(immediately);
}

void C_AnimationActor::WriteState(C_BitStream *msg)     // 0x005b6130
{
    msg->WriteBits(m_stopped ? 1 : 0, 1);
}

void C_AnimationActor::ReadState(C_BitStream *msg)      // 0x005b6150
{
    if (msg->ReadBits(1)) {
        Stop(true);
        Finish();
    }
}

bool C_AnimationActor::SaveGame(C_chunk &chunk)         // 0x005b3210
{
    // (The base's actor chunk is not written.)
    if (!m_stopped) {
        chunk.Begin(CHUNK_INTACT);
        chunk.Ascend();
    } else if (!m_animating) {
        chunk.Begin(CHUNK_DESTROYED);
        chunk.Ascend();
    } else {
        const int time = m_model->GetAnimTime(0);
        chunk.Begin(CHUNK_ANIMATING);
        chunk.Write(time);
        chunk.Ascend();
    }
    chunk.Begin(CHUNK_HELD);
    chunk.Write(static_cast<uint8_t>(m_animationHeld));
    chunk.Ascend();
    return true;
}

bool C_AnimationActor::LoadGame(C_chunk &chunk)         // 0x005b3450
{
    // Quirk kept: only the first chunk is read (the held flag after it is lost).
    s_loading = true;
    uint16_t id = 0;
    if (!chunk.Descend(id))                             // (the original goes on with a stack value)
        id = 0;
    switch (id) {
    case CHUNK_ANIMATING: {
        int time = 0;
        chunk.Read(time);
        chunk.Ascend();
        Stop(false);
        Update(time);
        break;
    }
    case CHUNK_INTACT:
        chunk.Ascend();
        break;
    case CHUNK_DESTROYED:
        chunk.Ascend();
        Stop(true);
        Finish();
        break;
    case CHUNK_HELD: {
        uint8_t held = 0;
        if (chunk.RemainingSize() > 0 && chunk.Read(held))
            m_animationHeld = held != 0;
        chunk.Ascend();
        break;
    }
    default:
        chunk.Ascend();                                 // (without a chunk entered: the actor's own)
        break;
    }
    for (size_t i = m_wings.size(); i-- > 0;)
        m_wings[i]->Reset();
    if (m_permanentRefresh) {
        for (size_t i = m_collisions.size(); i-- > 0;)
            m_collisions[i]->Refresh(false);
        m_refreshes = LOAD_REFRESHES;
    }
    s_loading = false;
    return true;
}

bool C_AnimationActor::ReadProperties(C_chunk &chunk)   // 0x005b5af0
{
    if (chunk.RemainingSize() <= 0)
        return false;
    // Quirk kept: a chunk of another id is entered and not left (its contents are read as the
    // actor's).
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case PROPERTY_ANIM_NAME: {
            char text[1000];
            chunk.ReadString(text, sizeof text);        // (999 characters at most)
            m_animName = text;
            chunk.Ascend();
            break;
        }
        case PROPERTY_HITPOINTS: {
            float hitPoints = 0.0f;
            chunk.Read(hitPoints);
            chunk.Ascend();
            m_hitPoints = hitPoints < -1.0f ? -1.0f : hitPoints;
            break;
        }
        case PROPERTY_PERMANENT_REFRESH: {
            uint8_t on = 0;
            if (chunk.RemainingSize() > 0 && chunk.Read(on))
                m_permanentRefresh = on != 0;
            chunk.Ascend();
            break;
        }
        case PROPERTY_EXPLOSION_SOUND:
            chunk.Read(m_explosionSound);
            chunk.Ascend();
            break;
        case PROPERTY_PARTICLE:
            chunk.Read(m_particleId);
            chunk.Ascend();
            break;
        case PROPERTY_COLLISION_FRAME: {
            char text[1000];
            chunk.ReadString(text, sizeof text);
            m_collisionFrameName = text;
            chunk.Ascend();
            break;
        }
        case PROPERTY_NAME_TEXT:
            chunk.Read(m_nameTextId);
            chunk.Ascend();
            break;
        case PROPERTY_SHOW_NO_INFO: {
            uint8_t on = 0;
            if (chunk.Read(on))
                m_showNoInfo = on != 0;
            chunk.Ascend();
            break;
        }
        default:
            break;
        }
    }
    return true;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_AnimationActorCreator::Create()              // 0x005b3130
{
    return new C_AnimationActor();                      // _aligned_malloc(0xf0, 16) in the original
}

void C_AnimationActorCreator::Destroy(C_Actor *actor)   // 0x005b31a0
{
    delete actor;
}

static C_AnimationActorCreator g_animationActorCreator; // 0x008ac594

// Registers the creator (static initialiser 0x005b3110, thunk 0x005b3100).
static const bool g_animationActorCreatorRegistered = [] {
    g_actorCreators[ACTOR_ANIMATION] = &g_animationActorCreator;
    return true;
}();

// The editor's property table (static initialiser 0x005b5910, thunk 0x005b5900).
const S_AnimationActorProperty g_animationActorProperties[8] = {
    {9, PROPERTY_ANIM_NAME, "Anim name", 0.0f, 0.0f, 0.0f},
    {5, PROPERTY_HITPOINTS, "Hitpoints", -1.0f, 10000.0f, -1.0f},
    {3, PROPERTY_PERMANENT_REFRESH, "Permanent collision refresh", 0.0f, 0.0f, 0.0f},
    {4, PROPERTY_EXPLOSION_SOUND, "Sound ID from chapter explosion", -1.0f, 1000.0f, -1.0f},
    {4, PROPERTY_PARTICLE, "Particle ID", -1.0f, 1000.0f, -1.0f},
    {9, PROPERTY_COLLISION_FRAME, "Collision frame name", 0.0f, 0.0f, 0.0f},
    {4, PROPERTY_NAME_TEXT, "Actor name text ID", -1.0f, 10000.0f, -1.0f},
    {3, PROPERTY_SHOW_NO_INFO, "Show no info flag", 0.0f, 0.0f, 0.0f},
};
