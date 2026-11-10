// The vertex animation module (0x004736d0..0x00474f80): see game/vertex_anim.h.
#include "game/vertex_anim.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "common/c_str.h"
#include "game/game.h"
#include "game/pending.h"                               // g_gameRole
#include "game/profile.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"

C_VertexAnimModule g_vertexAnimModule(MODULE_VERTEX_ANIM, "Vertex anim");   // 0x0089b474
uint32_t g_vertexAnimSortTime;                          // 0x0089b4a8

namespace {

bool IsClientSide()
{
    return (g_gameRole & GAME_ROLE_CLIENT) != 0;
}

bool AnimationsOn()
{
    return IsClientSide() && g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_VERTEX_ANIMATION);
}

// The distance (S_vector::Magnitude inlined: below 1e-8 0, within 1e-8 of 1 exactly 1).
float LengthOf(float square)
{
    if (square < 1e-8f)
        return 0.0f;
    if (std::fabs(square - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(square);
}

}  // namespace

// ---- the animations --------------------------------------------------------------------------

S_VertexAnimation::~S_VertexAnimation()                 // 0x00473720
{
    delete[] vertexCounts;
    if (frames) {
        // (A loading cut short leaves the frames after it null: see LoadAnimations.)
        for (int f = 0; f <= lastFrame && frames[f]; ++f) {
            for (int level = 0; level < levelCount; ++level)
                _aligned_free(frames[f][level]);
            delete[] frames[f];
        }
    }
    delete[] frames;
}

// ---- the players -----------------------------------------------------------------------------

void S_VertexAnimPlayer::Init(I3D_frame *visual_, S_VertexAnimation *animation_)   // 0x00473790
{
    animation = animation_;
    visual = visual_;
    backwards = false;
    frame = 0;
    nextFrame = 1;
    time = 0;
    distant = 0;
}

void S_VertexAnimPlayer::Blend(int from, int to, float t)
{
    auto *object = static_cast<I3D_object_visual *>(visual);
    const int level = (object->VisualFlags() & VISFLAGS_TEXTURE_LOD) ? 0 : object->GetTextureLevel();
    if (level > maxLevel)
        return;
    I3D_mesh_level *mesh = object->GetVisualMesh()->GetLevel(level);
    const S_VertexAnimVertex *a = animation->frames[from][level];
    const S_VertexAnimVertex *b = animation->frames[to][level];
    const float s = 1.0f - t;
    float *out = static_cast<float *>(mesh->GetVertices(0));
    const int count = animation->vertexCounts && level < animation->levelCount ? animation->vertexCounts[level] : 0;
    for (int i = 0; i < count; ++i, out += 8) {
        out[0] = a[i].pos.x * s + b[i].pos.x * t;
        out[1] = a[i].pos.y * s + b[i].pos.y * t;
        out[2] = a[i].pos.z * s + b[i].pos.z * t;
        out[3] = a[i].normal.x * s + b[i].normal.x * t;
        out[4] = a[i].normal.y * s + b[i].normal.y * t;
        out[5] = a[i].normal.z * s + b[i].normal.z * t;
    }
    mesh->UnlockVertices();
    object->RefreshLevels();
}

void S_VertexAnimPlayer::Reset()                        // 0x004737c0 (DS 0x0048f760)
{
    if (!visual || !animation)
        return;
    auto *object = static_cast<I3D_object_visual *>(visual);
#ifdef HD2_DEDICATED   // the server reads the level without testing the texture-LOD flag
    const int level = object->GetTextureLevel();
#else
    const int level = (object->VisualFlags() & VISFLAGS_TEXTURE_LOD) ? 0 : object->GetTextureLevel();
#endif
    if (level > maxLevel)
        return;
    // (The original blends frame 0 with itself: v * 0 + v.)
    Blend(0, 0, 0.0f);
    nextFrame = 1;
    frame = 0;
    time = 0;
    backwards = false;
}

void S_VertexAnimPlayer::Update(int timeDelta)          // 0x00473960
{
    if (!visual || !animation || stopped)
        return;
    time += timeDelta;
    if (time > frameTime) {
        time -= frameTime;
        if (time > frameTime)
            time = 0;
        const int last = animation->lastFrame;
        if (!pingPong) {
            const int was = frame;
            if (++frame > last) {
                if (!loop) {
                    stopped = true;
                    frame = was;
                    time = frameTime;
                    return;
                }
                frame = 0;
            }
            if (++nextFrame > last)
                nextFrame = 0;
        } else if (!backwards) {
            const int was = frame;
            const int wasNext = nextFrame;
            ++frame;
            if (++nextFrame > last) {
                if (!loop) {
                    frame = was;
                    nextFrame = wasNext;
                    stopped = true;
                    time = frameTime;
                    return;
                }
                frame = last;
                nextFrame = last - 1;
                backwards = true;
            }
        } else {
            --frame;
            if (--nextFrame < 0) {
                frame = 0;
                nextFrame = 1;
                backwards = false;
            }
        }
    }
    // Only a visual drawn in the last scene frame (or one never culled) is rewritten.
    if (visual->GetRenderStamp() != GetGame()->GetScene()->GetRenderStamp() && cull != 0)
        return;
    Blend(frame, nextFrame, static_cast<float>(time) / static_cast<float>(frameTime));
}

// ---- the module ------------------------------------------------------------------------------

C_VertexAnimModule::C_VertexAnimModule(int id, const char *name) : C_Module(id, name)   // 0x00473d60
{
}

C_VertexAnimModule::~C_VertexAnimModule() = default;    // 0x00473df0 (the vectors only)

S_VertexAnimation *C_VertexAnimModule::FindAnimation(const C_str &name)   // 0x00473e30
{
    for (S_VertexAnimation *animation : m_animations) {
        if (std::strcmp(name, animation->name) == 0)
            return animation;
    }
    return nullptr;
}

void C_VertexAnimModule::LoadAnimations()               // 0x00473ec0
{
    const int file = dtaOpen("Tables\\Vertanim.def", 0);
    if (file == -1) {
        dtaClose(-1);
        return;
    }
    while (!m_animations.empty()) {
        delete m_animations.back();
        m_animations.pop_back();
    }
    int header;
    dtaRead(file, &header, 4);
    S_VertexAnimation *animation;
    for (;;) {
        animation = new S_VertexAnimation;
        if (dtaRead(file, animation->name, 0x14) < 1 || dtaRead(file, &animation->lastFrame, 4) < 1 ||
            dtaRead(file, &animation->levelCount, 4) < 1)
            break;
        // Fix: the arrays start zeroed; the original's are not, and a file cut short had the
        // destructor free their garbage.
        animation->vertexCounts = new int[animation->levelCount]();
        bool cut = false;
        for (int level = 0; level < animation->levelCount && !cut; ++level)
            cut = dtaRead(file, &animation->vertexCounts[level], 4) < 1;
        if (cut)
            break;
        animation->frames = new S_VertexAnimVertex **[animation->lastFrame + 1]();
        for (int f = 0; f <= animation->lastFrame && !cut; ++f) {
            animation->frames[f] = new S_VertexAnimVertex *[animation->levelCount]();
            for (int level = 0; level < animation->levelCount && !cut; ++level) {
                const int size = animation->vertexCounts[level] * static_cast<int>(sizeof(S_VertexAnimVertex));
                animation->frames[f][level] = static_cast<S_VertexAnimVertex *>(_aligned_malloc(size, 16));
                cut = dtaRead(file, animation->frames[f][level], size) < 1;
            }
        }
        if (cut)
            break;
        m_animations.push_back(animation);
    }
    dtaClose(file);
    delete animation;
}

void C_VertexAnimModule::LoadPlayers()                  // 0x00474290
{
    C_str path("Missions\\");
    path += GetGame()->GetMissionName();
    path += "\\Vertanim.bin";
    const int file = dtaOpen(path, 0);
    if (file == -1) {
        dtaClose(-1);
        return;
    }
    while (!m_players.empty()) {
        delete m_players.back();
        m_players.pop_back();
    }
    int version;
    dtaRead(file, &version, 4);
    char name[0x80];
    S_VertexAnimPlayer *player;
    for (;;) {
        player = new S_VertexAnimPlayer;
        if (dtaRead(file, &player->maxLevel, 4) < 1 || dtaRead(file, name, 0x80) < 1)
            break;
        I3D_frame *visual = GetGame()->GetScene()->FindFrame(name, FRAME_MASK_ALL);
        if (!visual || visual->GetVisualType() != I3DVISUAL_OBJECT) {
            // Quirk kept: the rest of the record is read as the next one.
            delete player;
            continue;
        }
        if (dtaRead(file, &player->startFrame, 4) < 1 || dtaRead(file, &player->time, 4) < 1 ||
            dtaRead(file, &player->frameTime, 4) < 1 || dtaRead(file, &player->pingPong, 1) < 1 ||
            dtaRead(file, name, 0x14) < 1)
            break;
        player->animation = FindAnimation(C_str(name));
        if (version != 0 && dtaRead(file, &player->cull, 4) < 1)
            break;
        if (!player->animation) {
            delete player;
        } else {
            // (Init sets the file's time back to 0.)
            player->Init(visual, player->animation);
            m_players.push_back(player);
        }
    }
    dtaClose(file);
    delete player;
}

void C_VertexAnimModule::RemovePlayer(S_VertexAnimPlayer *player, bool destroy)   // 0x004746b0
{
    for (size_t i = 0; i < m_players.size(); ++i) {
        if (m_players[i] == player) {
            if (destroy)
                delete m_players[i];
            m_players[i] = m_players.back();
            m_players.pop_back();
            return;
        }
    }
}

void C_VertexAnimModule::Init()                         // 0x00474a00
{
    if (!IsClientSide())
        return;
    m_started = 0;
    LoadAnimations();
}

void C_VertexAnimModule::Done()                         // 0x00474a20
{
    if (!IsClientSide())
        return;
    while (!m_animations.empty()) {
        delete m_animations.back();
        m_animations.pop_back();
    }
}

void C_VertexAnimModule::Tick(int timeDelta)            // 0x00474740
{
    if (!AnimationsOn())
        return;
    g_vertexAnimSortTime += timeDelta;
    if (g_vertexAnimSortTime >= 300) {
        g_vertexAnimSortTime -= 300;
        I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera();
        const S_vector eye = camera->GetWorldPos();
        // (The original also takes the view direction's length, then drops it.)
        const float scale = std::tan(camera->GetFOV() * 0.5f) / camera->GetAspectRatio();
        for (S_VertexAnimPlayer *player : m_players) {
            const S_vector d = player->visual->GetWorldPos() - eye;
            const float distance = LengthOf(d.z * d.z + d.y * d.y + d.x * d.x);
            // The visual's size on screen: 1.7 m at that distance.
            const float size = 1.7f / (scale * distance + scale * distance);
            player->distant = size < 0.02f && player->cull != 0 ? 1 : 0;
        }
    }
    for (S_VertexAnimPlayer *player : m_players) {
        if (player->distant == 1)
            player->Update(timeDelta);
    }
}

void C_VertexAnimModule::Update(int timeDelta)          // 0x004749a0
{
    if (!AnimationsOn())
        return;
    for (S_VertexAnimPlayer *player : m_players) {
        if (player->distant == 0)
            player->Update(timeDelta);
    }
}

void C_VertexAnimModule::LoadMissionData(bool server)   // 0x00474aa0
{
    if (IsClientSide() && !server)
        LoadPlayers();
}

void C_VertexAnimModule::FreeMissionData(bool server)   // 0x00474ac0
{
    if (!IsClientSide() || server)
        return;
    while (!m_players.empty()) {
        delete m_players.back();
        m_players.pop_back();
    }
}

void C_VertexAnimModule::StartMission(bool)             // 0x00474b40
{
    if (!IsClientSide() || m_started != 0)
        return;
    m_started = 1;
    for (S_VertexAnimPlayer *player : m_players) {
        player->backwards = false;
        if (player->startFrame < 0) {
            // Quirk kept: the last frame's number as the count (and a division by 0 for an
            // animation of one frame).
            const int count = player->animation->lastFrame;
            if (!player->pingPong)
                player->frame = std::rand() % count;
            else if (count == 1)
                player->frame = 0;
            else
                player->frame = std::rand() % (count - 1);
            player->nextFrame = player->frame + 1;
            if (player->nextFrame >= count)
                player->nextFrame = 0;
        } else {
            player->frame = 0;
            player->nextFrame = 1;
        }
    }
}

void C_VertexAnimModule::StopMission(bool)              // 0x00474c10
{
    if (!IsClientSide() || m_started == 0)
        return;
    while (!m_players.empty()) {
        delete m_players.back();
        m_players.pop_back();
    }
    m_started = 0;
}

void C_VertexAnimModule::RestartMission(bool)           // 0x00474c90
{
    if (!IsClientSide())
        return;
    m_started = 0;
    StartMission(false);
}
