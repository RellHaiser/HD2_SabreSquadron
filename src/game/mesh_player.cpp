// The mesh player (see mesh_player.h).
#include "game/mesh_player.h"

#include <cstdio>

#include "common/chunk.h"
#include "game/game.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The mesh of a mesh visual (LMAP, MRPH, SNGM, SMMR, OBJ_), null for others. (0x004846a0)
I3D_mesh *VisualMesh(I3D_frame *frame)
{
    switch (frame->GetVisualType()) {
    case I3DVISUAL_LMAP:
    case I3DVISUAL_MORPH:
    case I3DVISUAL_SINGLEMESH:
    case I3DVISUAL_SINGLEMORPH:
    case I3DVISUAL_OBJECT:
        return static_cast<I3D_visual *>(frame)->GetVisualMesh();
    default:
        return nullptr;
    }
}

// The texture of the first face group of the visual's first level (null: none). (0x004846e0)
I3D_texture *OwnTexture(I3D_frame *frame)
{
    I3D_mesh *mesh = VisualMesh(frame);
    if (!mesh || !mesh->GetLevel(0) || !mesh->GetLevel(0)->FaceGroups())
        return nullptr;
    I3D_material *material = mesh->GetLevel(0)->FaceGroups()->material;
    return material ? material->GetTexture() : nullptr;
}

// Every face group of the visual's first level shows `texture`. (0x00484740)
void ShowTexture(I3D_frame *frame, I3D_texture *texture)
{
    I3D_mesh *mesh = VisualMesh(frame);
    if (!mesh)
        return;
    I3D_mesh_level *level = mesh->GetLevel(0);
    if (!level)
        return;
    for (int i = level->NumFaceGroups(); i-- > 0;) {
        if (level->GetFaceGroup(i) && level->GetFaceGroup(i)->material)
            level->GetFaceGroup(i)->material->SetTexture(texture);
    }
}

}  // namespace

// 0x00484100
C_MeshPlayer::C_MeshPlayer() : m_movie("") {}

// 0x004841e0
C_MeshPlayer::~C_MeshPlayer()
{
    Free();
}

// 0x00484260
void C_MeshPlayer::Load()
{
    Free();
    C_chunk chunk;
    const C_fstr fileName("missions\\%s\\%s", GetGame()->GetMissionName(), "meshplayer.bin");
    std::vector<I3D_frame *> frames;
    uint16_t id;
    if (chunk.Open(fileName, false) && chunk.Descend(id)) {
        for (;;) {
            const int left = chunk.RemainingSize();
            if (left == 0 || left == -1)
                break;
            chunk.Descend(id);
            char text[1000];
            if (id == 0x44C) {
                // (The original's buffer is not terminated: a name without its 0 ran on.)
                chunk.ReadString(text, sizeof text);
                m_movie = text;
                chunk.Ascend();
            } else if (id == 0x4B0) {
                chunk.ReadString(text, sizeof text);
                chunk.Ascend();
                if (I3D_frame *frame = GetGame()->GetScene()->FindFrame(text, FRAME_MASK_ANY))
                    frames.push_back(frame);
            } else {
                chunk.Ascend();
            }
        }
    }
    const C_str movie = m_movie;
    Play(frames, movie);
}

// 0x004847a0
void C_MeshPlayer::Play(const std::vector<I3D_frame *> &frames, const char *movie)
{
    Free();
    if (frames.empty())
        return;
    char path[0x100];
    std::snprintf(path, sizeof path, "GameMovie\\%s", movie);
    m_stream = GetIGraph()->CreateStream();
    m_frames.clear();
    if (m_stream->Open(path, 0) != 0) {
        m_stream->Release();
        m_stream = nullptr;
        m_movie = "";
        return;
    }
    for (I3D_frame *frame : frames) {
        if (frame->GetType() != FRAME_VISUAL || !(static_cast<I3D_visual *>(frame)->VisualFlags() & VISFLAGS_OBJECT_PARAMS))
            continue;
        I3D_texture *own = OwnTexture(frame);
        m_textures.push_back(own);
        if (own)
            own->AddRef();
        ShowTexture(frame, *m_stream->GetVideoTexture());
        m_frames.push_back(frame);
    }
    m_stream->Play();
    m_movie = movie;
}

// 0x00484a70
void C_MeshPlayer::Update(int)
{
    if (!m_stream)
        return;
    if (m_stream->GetState() == ISNDSTREAM_STATE_FINISHED)
        m_stream->Play();
    for (size_t i = m_frames.size(); i-- > 0;) {
        if (m_frames[i]->GetRenderStamp() == GetGame()->GetScene()->GetRenderStamp()) {
            m_stream->UpdateVideo(0);
            return;
        }
    }
}

// 0x00484ae0
void C_MeshPlayer::Free()
{
    if (!m_stream)
        return;
    for (size_t i = m_frames.size(); i-- > 0;) {
        ShowTexture(m_frames[i], m_textures[i]);
        if (m_textures[i]) {
            m_textures[i]->Release();
            m_textures[i] = nullptr;
        }
    }
    m_stream->Stop();
    m_stream->Release();
    m_stream = nullptr;
    m_movie = "";
    m_frames.clear();
    m_textures.clear();
}
