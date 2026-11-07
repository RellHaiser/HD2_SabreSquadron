// Destructible objects ("versions") — source file 0x0043ad80-0x0043f1e0.
#include "game/versions.h"

#include <cstdio>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/game.h"
#include "game/particles.h"
#include "game/physics.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/igraph.h"
#include "net/bitstream.h"
#include "script/script.h"

C_VersionsModule g_versionsModule(MODULE_VERSIONS, "Versions");
bool g_versionsLoading;

namespace {

// Saved game: one chunk per model (module) and per part (model).
enum : uint16_t {
    CHUNK_VERSION_REMOVED = 1000,   // model removed / part used up
    CHUNK_VERSION_DAMAGED = 2000,   // model: parts follow; part: stage and hit points
    CHUNK_VERSION_INTACT = 3000,
};

// Break sounds are groups of this sound table chapter.
constexpr int VERSION_SOUND_CHAPTER = 7;
// Effects of parts that lose all hit points at once (saved games, net state).
constexpr int DESTROY_ALL_DAMAGE = 1000000;

// Game data of `frame`, created with default values if the frame has none yet.
S_FrameData *EnsureFrameData(I3D_frame *frame)
{
    if (!frame->GetData()) {
        S_FrameData data = {};
        data.physicsObject = g_noPhysicsObject;
        SetFrameData(frame, data);
    }
    return frame->GetData();
}

// A stage frame's children: a dummy holding debris and the next stage's visual.
void ClassifyStageChild(I3D_frame *frame, I3D_frame *&debris, I3D_frame *&next)   // 0x0043ce10
{
    if (!frame)
        return;
    if (frame->GetType() == FRAME_DUMMY)
        debris = frame;
    else if (frame->GetType() == FRAME_VISUAL)
        next = frame;
}

uint32_t __stdcall BuildVersionKeeperProc(I3D_frame *frame, void * /*context*/)   // 0x0043e400
{
    const char *props = frame->GetUserProps();
    if (!*props)
        return 0;
    char text[0x400];
    // Bounded copy; the original copied the properties without a limit.
    std::strncpy(text, props, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    _strlwr(text);
    if (!std::strstr(text, "version") || frame->GetType() == FRAME_MODEL)
        return 0;

    const int hitPoints = ParseUserProp(text, "version", 100);
    const int particleEffect = ParseUserProp(text, "particle", -1);
    const int materialId = ParseUserProp(text, "matid", 0);
    const bool transparent = std::strstr(text, "trans") != nullptr;
    if (materialId == 0) {
        dbgPrintf("###### C_VersionMaster::BuildVersionKeepersFromScene() : frame with default material "
                  "['%s']######", frame->GetName());
    }
    g_versionsModule.AddKeeper(frame, hitPoints, particleEffect, materialId, transparent);   // 0x0043e2a0
    return 0;
}

} // namespace

int ParseUserProp(const char *props, const char *key, int defaultValue)
{
    if (!key || !props)
        return defaultValue;
    const char *found = std::strstr(props, key);
    if (!found)
        return defaultValue;
    int value = -1;
    return std::sscanf(found + std::strlen(key), "%d", &value) > 0 ? value : defaultValue;
}

void BuildVersionKeepers(I3D_scene *scene)
{
    scene->EnumFrames(BuildVersionKeeperProc, nullptr, 0xFFFF, 0);
}

// ---------------------------------------------------------------------------------
// S_VersionCollision

S_VersionCollision::S_VersionCollision(I3D_frame *frame, bool transparent)
{
    if (!frame)
        return;
    volume.frame = frame;
    volume.flags = COLLISION_SHAPE_BOX | (transparent ? COLLISION_VOLUME_SHOOT_THROUGH : 0);
    volume.type = COLLISION_TYPE_VERSION;

    I3D_bbox box;
    // Fixed: the original left the center uninitialized for an empty box.
    S_vector center(0, 0, 0);
    float radius = 0.0f;
    if (frame->GetType() == FRAME_DUMMY) {
        box = static_cast<I3D_dummy *>(frame)->GetBBox();
        center = (box.min + box.max) * 0.5f;
        radius = (box.max.y - box.min.y) * 0.5f;      // only the height counts here
    } else {
        I3D_bbox hierarchy;
        box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, volume.frame);
        if (box.min.x <= box.max.x) {
            const S_vector half = (box.max - box.min) * 0.5f;
            center = box.min + half;
            radius = half.Magnitude();
        }
    }

    const S_vector scale = frame->GetScale();
    box.min = S_vector(box.min.x * scale.x, box.min.y * scale.y, box.min.z * scale.z);
    box.max = S_vector(box.max.x * scale.x, box.max.y * scale.y, box.max.z * scale.z);
    volume.center = center * frame->GetMatrix();
    volume.radius = radius;
    volume.bbox = box;

    // World matrix without the scale, and its inverse.
    matrix = frame->GetMatrix();
    const float inv[3] = {1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z};
    for (int row = 0; row < 3; ++row) {
        for (int c = 0; c < 3; ++c)
            matrix.m[row][c] *= inv[row];
    }
    volume.matrix = &matrix;
    invMatrix.Inverse(matrix);
    volume.invMatrix = &invMatrix;

    const S_vector worldScale = frame->GetWorldScale();
    float maxScale = worldScale.y > worldScale.z ? worldScale.y : worldScale.z;
    maxScale = worldScale.x > maxScale ? worldScale.x : maxScale;
    volume.radius = maxScale * radius;
    volume.center = center * frame->GetMatrix();
    g_collisionWorld.AddVolume(&volume);
    g_collisionWorld.MoveVolume(&volume);
}

S_VersionCollision::~S_VersionCollision()
{
    g_collisionWorld.RemoveVolume(&volume);
}

// ---------------------------------------------------------------------------------
// C_VersionKeeper

C_VersionKeeper::C_VersionKeeper(I3D_frame *frame, C_VersionMaster *master, int index, int hitPoints,
                                 int particleEffect, int materialId, bool transparent)
    : m_frame(frame),
      m_master(master),
      m_hitPoints(hitPoints),
      m_particleEffect(particleEffect),
      m_materialId(materialId),
      m_index(index),
      m_transparent(transparent)
{
    frame->AddRef();
    S_FrameData *data = EnsureFrameData(frame);
    data->versionMaster = master;
    data->versionKeeper = this;
}

C_VersionKeeper::~C_VersionKeeper()
{
    S_FrameData *data = EnsureFrameData(m_frame);
    data->versionMaster = nullptr;
    data->versionKeeper = nullptr;
    m_frame->Release();
}

bool C_VersionKeeper::Damage(int damage, I3D_frame **outFrame, E_VersionEffects effects)
{
    if (outFrame)
        *outFrame = m_frame;
    if (damage < m_hitPoints) {
        m_hitPoints -= damage;
        return true;
    }
    if (!m_versionsLoaded && !m_master->LoadVersions(this)) {
        m_numVersions = 0;
        m_version = 0;
        m_master->OnKeeperDamaged(this);
        return false;
    }
    if (m_version >= m_numVersions)
        return false;

    // Skip every stage the damage uses up. (A stage takes the hit points given on
    // the frame of the stage after it.)
    int reached = m_version;
    int version = m_version;
    for (;;) {
        reached = version;
        damage -= m_hitPoints;
        ++version;
        if (version == m_numVersions)
            break;
        m_hitPoints = m_versionHitPoints[version];
        if (m_hitPoints > damage) {
            m_hitPoints -= damage;
            break;
        }
    }
    SwapVisual(m_versionFrames[reached]);
    if (outFrame)
        *outFrame = m_frame;

    if (C_BitStream *msg = g_netGame.CreateMessage(MODULE_VERSIONS, false)) {
        msg->WriteBits(m_master->GetIndex(), 16);
        msg->WriteBits(m_index, 8);
        msg->WriteBits(version, 8);
        g_netGame.SendNetMessage(msg);
    }
    if (g_gameRole & GAME_ROLE_CLIENT) {
        bool showEffects;
        if (effects == VERSION_EFFECTS_ALL)
            showEffects = true;
        else if (effects == VERSION_EFFECTS_NONE)
            showEffects = false;
        else
            showEffects = m_master->GetIndex() % 3 == 0;
        SetVersion(version, showEffects);
    }
    m_version = version;
    return m_master->OnKeeperDamaged(this);
}

void C_VersionKeeper::SetVersion(int version, bool effects)
{
    if (!m_versionsLoaded && m_master && !m_master->LoadVersions(this)) {
        m_numVersions = 0;
        m_version = 0;
        m_master->OnKeeperDamaged(this);
        return;
    }
    if (version < 0)
        version = static_cast<int>(m_versionFrames.size());
    if (version <= m_version)
        return;

    // The server side already swapped the visual in Damage(); in a local game it
    // is repeated only for changes without effects.
    const bool server = (g_gameRole & GAME_ROLE_SERVER) != 0;
    const bool local = (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL;
    if ((!server || (local && !effects)) && version <= static_cast<int>(m_versionFrames.size()))
        SwapVisual(m_versionFrames[version - 1]);
    if (effects && m_particleEffect >= 0)
        SpawnParticles(m_particleEffect);
    if (version <= static_cast<int>(m_debrisFrames.size()))
        SpawnDebris(m_debrisFrames[version - 1], effects);
    if (version <= static_cast<int>(m_versionSounds.size()) && effects)
        PlaySound(m_versionSounds[version - 1]);
    m_version = version;
}

void C_VersionKeeper::SpawnDebris(I3D_frame *debris, bool physics)
{
    if (!debris)
        return;
    // Place the stage frame holding the debris where the part is.
    if (I3D_frame *holder = debris->GetParent()) {
        holder->SetPos(m_frame->GetWorldPos());
        holder->SetDir(m_frame->GetWorldDir(), 0.0f);
        holder->SetScale(m_frame->GetWorldScale());
        holder->Update();
    }
    if (!physics)
        return;

    for (int i = debris->NumChildren(); i-- > 0;) {
        I3D_frame *source = debris->GetChild(i);
        if (!source || source->GetType() != FRAME_VISUAL)
            continue;
        I3D_driver *driver = I3DGetDriver();
        auto *piece = static_cast<I3D_visual *>(driver->CreateVisual(source->GetVisualType()));
        if (!piece)
            return;
        piece->Duplicate(source);
        piece->VisualFlags() &= ~0x2000u;

        const S_vector pos = source->GetWorldPos();
        if (I3D_scene *scene = GetGame()->GetScene()) {
            piece->LinkTo(scene->GetPrimarySector(), 0);
            piece->Update();
            piece->SetPos(pos);
            piece->SetDir(source->GetWorldDir(), 0.0f);
            piece->SetScale(source->GetWorldScale());
            piece->Update();
            scene->UpdateFrameSector(piece, piece->GetWorldPos());
            piece->Update();
        }

        S_PhysicsObjectDesc desc;
        desc.frame = piece;
        desc.type = PHYS_FALLING;
        desc.materialId = m_materialId;
        desc.startActive = true;
        desc.shootThrough = true;
        desc.radius = -1.0f;
        const T_PhysicsObject body = GetPhysicsModule()->CreateObject(desc);

        // Kick the piece away from the model, a little randomly.
        const float randomZ = RandomFloat(1.5f) - 0.75f;
        const float randomY = RandomFloat(1.5f) - 0.75f;
        const float randomX = RandomFloat(1.5f) - 0.75f;
        const S_vector piecePos = piece->GetWorldPos();
        if (I3D_frame *model = m_master->GetFrame()) {
            const S_vector away = (piecePos - model->GetWorldPos()) * 3.0f;
            const S_vector impulse(away.x + randomX, away.y + randomY, away.z + randomZ);
            if (body != g_noPhysicsObject) {
                const S_vector point(piecePos.x + randomX, piecePos.y + randomY, piecePos.z + randomZ);
                GetPhysicsModule()->ApplyImpulse(body, impulse, point, 1.0f);
            }
        }
        piece->Release();
    }
}

void C_VersionKeeper::PlaySound(int group)
{
    if (group == -1)
        return;
    S_SoundRequest request;
    request.chapter = VERSION_SOUND_CHAPTER;
    if (!m_frame)
        return;
    const S_matrix &m = m_frame->GetMatrix();
    request.pos = m(3);
    request.posW = m.m[3][3];
    request.group = group;
    request.flags = SOUND_SILENT_TO_AI;
    request.attachTo = nullptr;
    request.source = nullptr;
    GetSoundModule()->Play(request);
}

void C_VersionKeeper::SwapVisual(I3D_frame *source)
{
    if (!m_frame)
        return;
    I3D_driver *driver = I3DGetDriver();
    I3D_frame *visual = driver->CreateVisual(m_frame->GetVisualType());
    if (!visual)
        return;
    visual->Duplicate(source);
    visual->LinkTo(m_frame->GetParent(), 0);
    visual->SetPosInline(m_frame->GetPos());
    visual->SetWorldDir(m_frame->GetLocalDir(), 0.0f);
    visual->SetScale(m_frame->GetScale());
    visual->Update();
    EnsureFrameData(visual)->versionMaster = m_master;
    EnsureFrameData(visual)->versionKeeper = this;
    EnsureFrameData(m_frame)->versionMaster = nullptr;
    EnsureFrameData(m_frame)->versionKeeper = nullptr;

    // (Without an owner model the new visual stays next to the old one, as in the original.)
    auto *model = static_cast<I3D_model *>(m_frame->GetOwnerModel());
    if (!model)
        return;
    model->AddModelFrame(visual);
    model->RemoveModelFrame(m_frame);
    m_frame->SetOn(false);
    m_frame->Update();
    // Decals move over to the new visual.
    for (int i = m_frame->NumChildren(); i-- > 0;) {
        I3D_frame *child = m_frame->GetChild(i);
        if (child && child->GetVisualType() == I3DVISUAL_PROJECTOR) {
            child->LinkTo(visual, I3DLINK_KEEP_WORLD);
            child->Update();
        }
    }
    // So does the lighting.
    if (m_frame->GetVisualType() == I3DVISUAL_LMAP && visual->GetVisualType() == I3DVISUAL_LMAP) {
        auto *from = static_cast<I3D_lmap_visual *>(m_frame);
        auto *to = static_cast<I3D_lmap_visual *>(visual);
        const int index = from->GetLightmapIndex();
        to->LoadLightmap(from->LightmapId(index));
        to->LightmapParam1(to->GetLightmapIndex()) = from->LightmapParam1(index);
        to->LightmapParam2(to->GetLightmapIndex()) = from->LightmapParam2(index);
        to->SelectLightmap(index);
        const S_vector up(0, 1, 0);
        to->Relight(GetGame()->GetScene(), up, from->LightmapParam1(index), 4, 0, 0);
        to->Update();
    }
    m_frame->Release();
    m_frame = visual;
    for (int i = visual->NumChildren(); i-- > 0;) {
        I3D_frame *child = m_frame->GetChild(i);
        if (child && child->GetVisualType() == I3DVISUAL_PROJECTOR) {
            static_cast<I3D_projector *>(child)->Reproject(0, 0);
            child->Update();
        }
    }
}

void C_VersionKeeper::SpawnParticles(int effect)
{
    if (m_frame)
        g_particlesModule.Create(effect, m_frame->GetMatrix(), nullptr, true, false);
}

// ---------------------------------------------------------------------------------
// C_VersionMaster

C_VersionMaster::C_VersionMaster(I3D_frame *model, int index, bool transparent)
    : m_frame(model),
      m_index(index)
{
    model->AddRef();
    EnsureFrameData(model)->versionMaster = this;
    m_collision = new S_VersionCollision(m_frame, transparent);
}

C_VersionMaster::~C_VersionMaster()
{
    // Large models keep blocking shots until the mission ends.
    auto *model = static_cast<I3D_model *>(m_collision->volume.frame);
    if (!model->IsBoundValid())
        model->UpdateBound();
    if (model->GetBoundRadius() <= 0.1f)
        delete m_collision;
    else
        g_versionsModule.KeepCollision(m_collision);

    EnsureFrameData(m_frame)->versionMaster = nullptr;
    if (m_damaged && m_frame)
        g_versionsModule.AddDamagedModel(m_frame);
    Clear();
}

void C_VersionMaster::Clear()
{
    for (std::size_t i = m_keepers.size(); i-- > 0;)
        delete m_keepers[i];
    m_keepers.clear();
    if (m_versionsModel) {
        m_versionsModel->Release();
        m_versionsModel = nullptr;
    }
    if (m_frame) {
        m_frame->Release();
        m_frame = nullptr;
    }
}

bool C_VersionMaster::AddKeeper(I3D_frame *frame, int hitPoints, int particleEffect, int materialId,
                                bool transparent)
{
    if (const S_FrameData *data = frame->GetData(); data && data->versionMaster)
        return false;
    m_keepers.push_back(new C_VersionKeeper(frame, this, static_cast<int>(m_keepers.size()), hitPoints,
                                            particleEffect, materialId, transparent));
    return true;
}

bool C_VersionMaster::DamageFrame(I3D_frame *frame, int damage)
{
    if (!frame)
        return false;
    const S_FrameData *data = frame->GetData();
    if (!data || data->versionMaster != this || !data->versionKeeper)
        return false;
    return data->versionKeeper->Damage(damage, &frame, VERSION_EFFECTS_ALL);
}

bool C_VersionMaster::DamageAll(int damage, E_VersionEffects effects)
{
    bool result = true;
    const int index = m_index;
    for (std::size_t i = m_keepers.size(); i-- > 0;) {
        C_VersionKeeper *keeper = m_keepers[i];
        if (!keeper)
            continue;
        if (!keeper->Damage(damage, nullptr, effects))
            result = false;
        // The last part going removes (deletes) this model.
        if (!g_versionsModule.GetMaster(index))
            return false;
    }
    return result;
}

bool C_VersionMaster::OnKeeperDamaged(C_VersionKeeper *keeper)
{
    if (!keeper)
        return false;
    bool alive = true;
    m_damaged = true;
    if (const S_FrameData *data = m_frame->GetData(); data && data->script)
        data->script->OnHit();

    if (keeper->m_version == keeper->m_numVersions) {
        for (std::size_t i = m_keepers.size(); i-- > 0;) {
            if (m_keepers[i] && m_keepers[i] == keeper) {
                delete keeper;
                m_keepers[i] = nullptr;
                alive = false;
                break;
            }
        }
    }

    // Refit the collision box to what is left of the model.
    if (m_collision && m_collision->volume.frame) {
        I3D_frame *model = m_collision->volume.frame;
        I3D_bbox hierarchy;
        const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, model);
        // Fixed: the original left the center uninitialized for an empty box.
        S_vector center(0, 0, 0);
        float radius = 0.0f;
        if (box.min.x <= box.max.x) {
            const S_vector half = (box.max - box.min) * 0.5f;
            center = box.min + half;
            radius = half.Magnitude();
        }
        m_collision->volume.radius = radius;
        m_collision->volume.bbox = box;
        m_collision->invMatrix = model->GetMatrix();
        m_collision->volume.center = center * m_collision->invMatrix;
        const S_matrix world = m_collision->invMatrix;
        m_collision->invMatrix.Inverse(world);
        g_collisionWorld.MoveVolume(&m_collision->volume);
    }

    if (!m_keepers.empty()) {
        bool anyLeft = false;
        for (const C_VersionKeeper *k : m_keepers)
            anyLeft |= k != nullptr;
        if (!anyLeft)
            g_versionsModule.RemoveMaster(this);    // may delete this
    }
    return alive;
}

bool C_VersionMaster::LoadVersions(C_VersionKeeper *keeper)
{
    if (!m_versionsModel)
        LoadVersionsModel();
    if (!m_versionsModel)
        return false;
    I3D_frame *stage = m_versionsModel->FindChildFrame(keeper->m_frame->GetShortName(), 0xFFFF);
    if (!stage)
        return false;
    do {
        const int hitPoints = ParseUserProp(stage->GetUserProps(), "version", 100);
        const int sound = ParseUserProp(stage->GetUserProps(), "sound", -1);
        I3D_frame *debris = nullptr;
        I3D_frame *next = nullptr;
        for (int i = stage->NumChildren(); i-- > 0;)
            ClassifyStageChild(stage->GetChild(i), debris, next);
        keeper->m_debrisFrames.push_back(debris);
        keeper->m_versionFrames.push_back(stage);
        keeper->m_versionHitPoints.push_back(hitPoints);
        keeper->m_versionSounds.push_back(sound);
        stage = next;
    } while (stage);
    keeper->m_numVersions = static_cast<int>(keeper->m_versionHitPoints.size());
    keeper->m_versionsLoaded = true;
    return true;
}

void C_VersionMaster::LoadVersionsModel()
{
    if (m_versionsModel || !m_frame)
        return;
    // "d" + the model's file name without directory and extension.
    char path[128];
    // Bounded copy; the original copied the file name without a limit.
    std::strncpy(path, static_cast<I3D_model *>(m_frame)->GetFileName(), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (char *dot = std::strrchr(path, '.'))
        *dot = '\0';
    const char *slash = std::strrchr(path, '\\');
    const C_fstr fileName("d%s", slash ? slash + 1 : path);

    // The original inlines this cache lookup (logging "Version cache : ..." texts).
    I3D_model *model = nullptr;
    g_versionModelCache.Get(fileName, &model);
    m_versionsModel = model;
    if (model && !g_versionsLoading) {
        if (const S_FrameData *data = m_frame->GetData(); data && data->script)
            data->script->OnDeath();
    }
}

void C_VersionMaster::SaveGame(C_chunk &chunk)
{
    for (const C_VersionKeeper *keeper : m_keepers) {
        if (!keeper) {
            chunk.Begin(CHUNK_VERSION_REMOVED);
        } else if (keeper->m_version == 0) {
            chunk.Begin(CHUNK_VERSION_INTACT);
        } else {
            chunk.Begin(CHUNK_VERSION_DAMAGED);
            chunk.Write(keeper->m_version);
            chunk.Write(keeper->m_hitPoints);
        }
        chunk.Ascend();
    }
}

void C_VersionMaster::LoadGame(C_chunk &chunk)
{
    g_versionsLoading = true;
    uint16_t id = 0;
    for (std::size_t i = 0; i < m_keepers.size(); ++i) {
        chunk.Descend(id);
        C_VersionKeeper *keeper = m_keepers[i];
        // (Null checks added: a part can be gone if the save does not match the mission.)
        if (id == CHUNK_VERSION_REMOVED) {
            if (keeper)
                keeper->SetVersion(-1, false);
        } else if (id == CHUNK_VERSION_DAMAGED) {
            int version = 0;
            int hitPoints = 0;
            chunk.Read(version);
            chunk.Read(hitPoints);
            if (keeper) {
                keeper->m_hitPoints = hitPoints;
                keeper->SetVersion(version, false);
            }
        }
        chunk.Ascend();
    }
    g_versionsLoading = false;
}

void C_VersionMaster::WriteState(C_BitStream *msg)
{
    if (!(g_gameRole & GAME_ROLE_SERVER) || !msg)
        return;
    for (std::size_t i = 0; i < m_keepers.size(); ++i) {
        const C_VersionKeeper *keeper = m_keepers[i];
        if (!keeper) {
            msg->WriteBits(static_cast<uint32_t>(i), 16);
            msg->WriteBits(0xFFFFFFFFu, 8);     // used up: last stage
        } else if (keeper->m_version != 0) {
            msg->WriteBits(static_cast<uint32_t>(i), 16);
            msg->WriteBits(keeper->m_version, 8);
        }
    }
    msg->WriteBits(0xFFFFFFFFu, 16);
}

void C_VersionMaster::ReadState(C_BitStream *msg)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT) || !msg)
        return;
    for (int index = static_cast<int16_t>(msg->ReadBits(16)); index != -1;
         index = static_cast<int16_t>(msg->ReadBits(16))) {
        const int version = static_cast<int8_t>(msg->ReadBits(8));
        if (!(g_gameRole & GAME_ROLE_SERVER))
            m_keepers[index]->SetVersion(version, false);
    }
}

// ---------------------------------------------------------------------------------
// C_VersionsModule

C_VersionsModule::C_VersionsModule(int id, const char *name)
    : C_Module(id, name)
{
}

C_VersionsModule::~C_VersionsModule() = default;

void C_VersionsModule::StartMission(bool server)
{
    if (server || !(g_gameRole & GAME_ROLE_SERVER)) {
        Clear();
        BuildVersionKeepers(GetGame()->GetScene());
    }
}

void C_VersionsModule::StopMission(bool server)
{
    if (server || !(g_gameRole & GAME_ROLE_SERVER))
        Clear();
}

void C_VersionsModule::RestartMission(bool /*server*/)
{
    if (g_gameRole & GAME_ROLE_SERVER)
        StopMission(true);
    if (g_gameRole & GAME_ROLE_CLIENT)
        StopMission(false);
    if (g_gameRole & GAME_ROLE_SERVER)
        StartMission(true);
    if (g_gameRole & GAME_ROLE_CLIENT)
        StartMission(false);
}

void C_VersionsModule::Clear()
{
    for (std::size_t i = m_masters.size(); i-- > 0;)
        delete m_masters[i];
    m_masters.clear();
    for (std::size_t i = m_keptCollisions.size(); i-- > 0;)
        delete m_keptCollisions[i];
    m_keptCollisions.clear();
    // Damaged models get their original visuals back.
    for (std::size_t i = m_damagedModels.size(); i-- > 0;) {
        if (auto *model = static_cast<I3D_model *>(m_damagedModels[i]))
            g_modelCache.OpenInto(model, model->GetFileName(), 0, nullptr);
    }
    m_damagedModels.clear();
}

void C_VersionsModule::AddKeeper(I3D_frame *frame, int hitPoints, int particleEffect, int materialId,
                                 bool transparent)
{
    if (!frame)
        return;
    I3D_frame *model = frame->GetOwnerModel();
    if (!model || model->GetType() != FRAME_MODEL)
        return;
    C_VersionMaster *master = model->GetData() ? model->GetData()->versionMaster : nullptr;
    if (!master) {
        master = new C_VersionMaster(model, static_cast<int>(m_masters.size()), transparent);
        m_masters.push_back(master);
    }
    master->AddKeeper(frame, hitPoints, particleEffect, materialId, transparent);
}

void C_VersionsModule::RemoveMaster(C_VersionMaster *master)
{
    if (!master)
        return;
    for (std::size_t i = m_masters.size(); i-- > 0;) {
        if (m_masters[i] && m_masters[i] == master) {
            master->m_removed = true;
            if (--master->m_refCount == 0)
                delete master;
            m_masters[i] = nullptr;
        }
    }
}

void C_VersionsModule::KeepCollision(S_VersionCollision *collision)
{
    m_keptCollisions.push_back(collision);
}

bool C_VersionsModule::SaveGame(C_chunk &chunk)
{
    for (C_VersionMaster *master : m_masters) {
        if (!master) {
            chunk.Begin(CHUNK_VERSION_REMOVED);
        } else if (!master->m_damaged) {
            chunk.Begin(CHUNK_VERSION_INTACT);
        } else {
            chunk.Begin(CHUNK_VERSION_DAMAGED);
            master->SaveGame(chunk);
        }
        chunk.Ascend();
    }
    return true;
}

bool C_VersionsModule::LoadGame(C_chunk &chunk)
{
    uint16_t id = 0;
    for (std::size_t i = 0; i < m_masters.size(); ++i) {
        chunk.Descend(id);
        C_VersionMaster *master = m_masters[i];
        // (Null checks added, see C_VersionMaster::LoadGame.)
        if (id == CHUNK_VERSION_REMOVED) {
            if (master)
                master->DamageAll(DESTROY_ALL_DAMAGE, VERSION_EFFECTS_NONE);
        } else if (id == CHUNK_VERSION_DAMAGED) {
            if (master) {
                master->m_damaged = true;
                master->LoadGame(chunk);
            }
        }
        chunk.Ascend();
    }
    return true;
}

void C_VersionsModule::WriteState(C_BitStream *msg)
{
    if (!(g_gameRole & GAME_ROLE_SERVER) || !msg)
        return;
    const int count = static_cast<int>(m_masters.size());
    for (int i = 0; i < count; ++i) {
        C_VersionMaster *master = m_masters[i];
        if (!master) {
            msg->WriteBits(static_cast<uint32_t>(-1 - i), 16);     // removed model
        } else if (master->m_damaged) {
            msg->WriteBits(static_cast<uint32_t>(i), 16);
            master->WriteState(msg);
        }
    }
    msg->WriteBits(static_cast<uint32_t>(-0x20 - count), 16);     // end
}

void C_VersionsModule::ReadState(C_BitStream *msg)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT) || !msg)
        return;
    for (;;) {
        const int index = static_cast<int16_t>(msg->ReadBits(16));
        if (index < -0x10 - static_cast<int>(m_masters.size()))
            break;
        if (index < 0) {
            // A removed model: all its parts are in their last stage.
            if (!(g_gameRole & GAME_ROLE_SERVER)) {
                if (C_VersionMaster *master = m_masters[-index - 1]) {
                    for (std::size_t k = master->m_keepers.size(); k-- > 0;) {
                        if (C_VersionKeeper *keeper = master->m_keepers[k])
                            keeper->SetVersion(-1, false);
                    }
                }
            }
        } else if (C_VersionMaster *master = m_masters[index]) {
            master->ReadState(msg);
        }
    }
}

bool C_VersionsModule::OnMessage(C_BitStream *msg)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT) || !msg)
        return false;
    const auto masterIndex = static_cast<uint32_t>(static_cast<int16_t>(msg->ReadBits(16)));
    const auto keeperIndex = static_cast<uint32_t>(static_cast<int8_t>(msg->ReadBits(8)));
    const int version = static_cast<int8_t>(msg->ReadBits(8));
    if (g_gameRole & GAME_ROLE_SERVER)
        return true;
    if (masterIndex >= m_masters.size())
        return true;
    C_VersionMaster *master = m_masters[masterIndex];
    if (!master || keeperIndex >= master->m_keepers.size())
        return true;
    if (C_VersionKeeper *keeper = master->m_keepers[keeperIndex]) {
        keeper->SetVersion(version, true);
        master->OnKeeperDamaged(master->m_keepers[keeperIndex]);
    }
    return true;
}
