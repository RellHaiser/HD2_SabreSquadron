// Footprints on the ground near humans (original source 0x0078a890..0x0078ccf0).
//
// A footprint is a projector visual ('PROJ') with the left / right texture of its kind, put
// down by C_HumanMovement::Footstep. Each human keeps at most a number of them and each lasts a
// while; then it fades out over 3 s (or goes at once when it was not seen in the last frame).
// The blood pools of wounded humans are remembered here too: stepping into one leaves bloody
// footprints for the next 10 steps. Clients only.
#include "game/surface_marks.h"

#include <malloc.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <vector>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/pending.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

uint32_t g_surfaceMarksEnabled = 1;

namespace {

// The footprint kinds: the ground material and the textures of its prints (table
// 0x0084f548; the textures are "<prefix>l.tga" / "<prefix>r.tga").
struct S_FootprintKind {                                // 0x1C bytes
    int material;                                       // ground material; -2 / -3 bloody, -4 / -5 wet
    char prefix[0x10];
    IImage *left;
    IImage *right;
};
S_FootprintKind g_footprintKinds[] = {
    {34, "s_", nullptr, nullptr},                       // snow
    {35, "s_", nullptr, nullptr},
    {36, "s_", nullptr, nullptr},
    {41, "a_", nullptr, nullptr},                       // sand
    {42, "a_", nullptr, nullptr},
    {86, "a_", nullptr, nullptr},
    {91, "s_", nullptr, nullptr},
    {43, "d_", nullptr, nullptr},                       // dirt
    {44, "d_", nullptr, nullptr},
    {-2, "b_", nullptr, nullptr},                       // bloody
    {-3, "b_", nullptr, nullptr},
    {-4, "w_", nullptr, nullptr},                       // wet
    {-5, "w_", nullptr, nullptr},
};
constexpr int FOOTPRINT_KINDS = static_cast<int>(sizeof(g_footprintKinds) / sizeof(g_footprintKinds[0]));
const char LEFT_SUFFIX[] = "l.tga";                     // 0x0084f6c8
const char RIGHT_SUFFIX[] = "r.tga";                    // 0x0084f6c0

// Limits (saved with the marks): footprints of one human, their lifetime in s, and a value
// that is only saved (0x0084f6bc).
int g_maxFootprintsPerHuman = 30;                       // 0x0084f6b4 (40 once initialised)
int g_footprintLifetime = 30;                           // 0x0084f6b8 (40 once initialised)
int g_unknown84f6bc = 500;                              // 0x0084f6bc

constexpr int FADE_TIME = 3000;                         // ms
constexpr int FADE_STEP = 300;                          // ms between fading steps
constexpr int DETAIL_CHECK_TIME = 1400;                 // ms between looks at the detail setting
// Size of the footprint projectors (the loaded ones are less deep, as in the original).
const S_vector FOOTPRINT_SCALE(0.12f, 0.12f, 0.7f);
const S_vector LOADED_FOOTPRINT_SCALE(0.12f, 0.12f, 0.6f);
constexpr float FOOTPRINT_RANGE = 22.0f;
constexpr float PI_4 = 0.785398f;
// The blood pools near a step (half the box).
constexpr float BLOOD_REACH_XZ = 0.42f;
constexpr float BLOOD_REACH_Y = 0.2f;

std::list<S_SurfaceMark *> g_footprints;                // 0x0098ad38
std::list<S_SurfaceMark *> g_fadingMarks;               // 0x0098ad28 (time: ms of fading)
// Marks not in use (C_auto_00816ca4 at 0x0098ad44: grows by 16).
std::vector<S_SurfaceMark *> g_freeMarks;
constexpr int MARK_POOL_GROWTH = 16;
// Footprints in g_footprints per human (by net id).
constexpr int MARK_COUNTERS = 400;
uint32_t g_marksPerHuman[MARK_COUNTERS];                // 0x0098a6d8
// Where blood pools are (aligned array; w unused).
std::vector<S_vector4> g_bloodPositions;                // 0x0098ad18
int g_detailCheckTime = 0;                              // 0x0098ad64
int g_fadeTime = 0;                                     // 0x0098ad68

// Saved games (chunk 0x349).
enum : uint16_t {
    CHUNK_MARKS = 0x349,
    CHUNK_MARKS_FOOTPRINTS = 100,
    CHUNK_MARKS_FADING = 200,
    CHUNK_MARKS_BLOOD = 300,
    // In the lists:
    CHUNK_MARK_TIME = 1,
    CHUNK_MARK_HUMAN = 2,
    CHUNK_MARK_KIND = 3,
    CHUNK_MARK_LEFT = 4,
    CHUNK_MARK_POS = 5,
    CHUNK_MARK_ROT = 6,                                 // quaternion, scalar first
    CHUNK_MARK_INTENSITY = 7,
    CHUNK_MARK_COLOR = 8,                               // ends a mark
    // In CHUNK_MARKS_BLOOD:
    CHUNK_BLOOD_POS = 1,
    // In CHUNK_MARKS (after the lists):
    CHUNK_MARKS_ENABLED = 1,
    CHUNK_MARKS_MAX_PER_HUMAN = 2,
    CHUNK_MARKS_LIFETIME = 3,
    CHUNK_MARKS_UNKNOWN4 = 4,
};

I3D_projector_visual *Projector(const S_SurfaceMark *mark)
{
    return static_cast<I3D_projector_visual *>(mark->visual);
}

// Fix: the counters are indexed by net id without a check in the original.
uint32_t *Counter(int humanId)
{
    static uint32_t ignored;
    return humanId >= 0 && humanId < MARK_COUNTERS ? &g_marksPerHuman[humanId] : &ignored;
}

void ReturnMark(S_SurfaceMark *mark)
{
    if (mark)
        g_freeMarks.push_back(mark);
}

// Takes a mark's projector off the scene (released with `release`, else kept for reuse).
void RemoveFromScene(S_SurfaceMark *mark, bool release)
{
    I3D_frame *visual = mark->visual;
    visual->LinkTo(nullptr, 0);
    GetGame()->GetScene()->RemoveFrame(visual);
    if (release) {
        visual->Release();
        mark->visual = nullptr;
    } else {
        Projector(mark)->SetBuffered(3);
    }
}

// Puts a mark's projector into the scene with its texture (AddFootprint and the loads).
void ShowMark(S_SurfaceMark *mark)
{
    const S_FootprintKind &kind = g_footprintKinds[mark->kind];
    I3D_projector_visual *visual = Projector(mark);
    visual->SetTexture(mark->left ? kind.left : kind.right);
    visual->SetBuffered(0);
    visual->SetOn(true);
    GetGame()->GetScene()->AddFrame(visual);
}

void SetMarkLook(S_SurfaceMark *mark, const S_vector &scale)
{
    I3D_projector_visual *visual = Projector(mark);
    visual->SetScale(scale);
    visual->SetMode(5);
    visual->SetRange(FOOTPRINT_RANGE);
    visual->SetOrthogonal(1);
}

void SaveMark(C_chunk &chunk, const S_SurfaceMark *mark)
{
    chunk.WriteChunk(CHUNK_MARK_TIME, mark->time);
    chunk.WriteChunk(CHUNK_MARK_HUMAN, mark->humanId);
    chunk.WriteChunk(CHUNK_MARK_KIND, mark->kind);
    chunk.WriteChunk(CHUNK_MARK_LEFT, static_cast<int>(mark->left));
    const S_matrix &matrix = mark->visual->GetMatrix();
    chunk.WriteChunk(CHUNK_MARK_POS, &matrix(3), 0x0C);
    S_quat rot;
    rot.Make(matrix);
    float file[4];
    rot.ToFile(file);
    chunk.WriteChunk(CHUNK_MARK_ROT, file, sizeof(file));
    const I3D_projector_visual *visual = Projector(mark);
    chunk.WriteChunk(CHUNK_MARK_INTENSITY, visual->GetIntensity());
    chunk.WriteChunk(CHUNK_MARK_COLOR, &visual->GetBaseColor(), 0x0C);
}

// Loads the marks of a list section into `list`.
void LoadMarks(C_chunk &chunk, std::list<S_SurfaceMark *> &list)
{
    S_SurfaceMark *mark = nullptr;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_MARK_TIME:
            mark = SurfaceMarks_NewMark(1);
            if (!mark->visual)
                mark->visual = I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR);
            chunk.Read(mark->time);
            break;
        case CHUNK_MARK_HUMAN:
            chunk.Read(mark->humanId);
            break;
        case CHUNK_MARK_KIND:
            chunk.Read(mark->kind);
            break;
        case CHUNK_MARK_LEFT: {
            int left = 0;
            chunk.Read(left);
            mark->left = left != 0;
            break;
        }
        case CHUNK_MARK_POS: {
            S_vector pos;
            chunk.Read(&pos, 0x0C);
            mark->visual->SetPos(pos);
            GetGame()->GetScene()->UpdateFrameSector(mark->visual, mark->visual->GetWorldPos());
            break;
        }
        case CHUNK_MARK_ROT: {
            float file[4];
            chunk.Read(file, sizeof(file));
            mark->visual->SetRot(S_quat::FromFile(file));
            break;
        }
        case CHUNK_MARK_INTENSITY: {
            float opacity = 0.0f;
            chunk.Read(opacity);
            Projector(mark)->SetIntensity(opacity);
            break;
        }
        case CHUNK_MARK_COLOR: {
            S_vector color;
            chunk.Read(&color, 0x0C);
            Projector(mark)->SetColor(color);
            if (mark) {
                ShowMark(mark);
                SetMarkLook(mark, LOADED_FOOTPRINT_SCALE);
                mark->visual->Update();
                Projector(mark)->Reproject(0, 0);
                Projector(mark)->SetBuffered(3);
                list.push_back(mark);
            }
            break;
        }
        default:
            break;
        }
        chunk.Ascend();
    }
}

}  // namespace

// ------------------------------------------------------------------ the mark pool

S_SurfaceMark *SurfaceMarks_NewMark(int /*unused*/)
{
    if (g_freeMarks.empty()) {
        for (int i = 0; i < MARK_POOL_GROWTH; ++i)
            g_freeMarks.push_back(new S_SurfaceMark);
    }
    S_SurfaceMark *mark = g_freeMarks.back();
    g_freeMarks.pop_back();
    return mark;
}

void SurfaceMarks_Free()
{
    if (!IsClient())
        return;
    for (S_SurfaceMark *mark : g_freeMarks) {
        if (mark->visual)
            mark->visual->Release();
        delete mark;
    }
    g_freeMarks.clear();
}

// ------------------------------------------------------------------ the mission

void SurfaceMarks_Init()
{
    if (!IsClient())
        return;
    g_surfaceMarksEnabled = 1;
    g_maxFootprintsPerHuman = 40;
    g_footprintLifetime = 40;
    g_unknown84f6bc = 500;
    // The textures (all up to the first one missing).
    for (S_FootprintKind &kind : g_footprintKinds) {
        char name[0x20];
        std::strcpy(name, kind.prefix);
        std::strcat(name, LEFT_SUFFIX);
        kind.left = nullptr;
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
        if (GetIGraph()->LoadTexture(name, 0, 0x402, &kind.left) != I3D_OK)
            return;
#else
        return;
#endif
        std::strcpy(name, kind.prefix);
        std::strcat(name, RIGHT_SUFFIX);
        kind.right = nullptr;
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
        if (GetIGraph()->LoadTexture(name, 0, 0x402, &kind.right) != I3D_OK)
            return;
#endif
    }
}

void SurfaceMarks_Done()
{
    if (!IsClient())
        return;
    g_bloodPositions.clear();
    g_bloodPositions.shrink_to_fit();
    for (S_SurfaceMark *mark : g_footprints) {
        RemoveFromScene(mark, true);
        ReturnMark(mark);
    }
    g_footprints.clear();
    for (S_SurfaceMark *mark : g_fadingMarks) {
        RemoveFromScene(mark, true);
        ReturnMark(mark);
    }
    g_fadingMarks.clear();
    std::memset(g_marksPerHuman, 0, sizeof(g_marksPerHuman));
    for (S_FootprintKind &kind : g_footprintKinds) {
        if (kind.left) {
            kind.left->Release();
            kind.left = nullptr;
        }
        if (kind.right) {
            kind.right->Release();
            kind.right = nullptr;
        }
    }
}

void SurfaceMarks_Clear()
{
    if (!IsClient())
        return;
    g_bloodPositions.clear();
    g_bloodPositions.shrink_to_fit();
    for (S_SurfaceMark *mark : g_footprints) {
        RemoveFromScene(mark, false);
        ReturnMark(mark);
    }
    g_footprints.clear();
    for (S_SurfaceMark *mark : g_fadingMarks) {
        RemoveFromScene(mark, false);
        ReturnMark(mark);
    }
    g_fadingMarks.clear();
    std::memset(g_marksPerHuman, 0, sizeof(g_marksPerHuman));
}

// ------------------------------------------------------------------ marks

int SurfaceMarks_FindKind(int material)
{
    for (int i = 0; i < FOOTPRINT_KINDS; ++i) {
        if (g_footprintKinds[i].material == material)
            return i;
    }
    return -1;
}

void SurfaceMarks_AddFootprint(S_SurfaceMark *mark, const S_vector &pos, float angle, I3D_frame *sector)
{
    if (!IsClient() || !mark || g_surfaceMarksEnabled != 1)
        return;
    mark->time = GetGame()->GetMissionTime();
    ++*Counter(mark->humanId);
    ShowMark(mark);
    I3D_frame *visual = mark->visual;
    visual->SetPos(S_vector(pos.x, pos.y + 0.1f, pos.z));
    // Projecting down, turned along the foot.
    visual->SetDir(S_vector(0.0f, -1.0f, 0.0f), angle + PI_4);
    SetMarkLook(mark, FOOTPRINT_SCALE);
    visual->LinkTo(sector, 0);
    visual->Update();
    Projector(mark)->Reproject(0, 0);
    Projector(mark)->SetBuffered(3);
    g_footprints.push_back(mark);
}

void SurfaceMarks_AddBlood(const S_vector &pos)
{
    if (!IsClient())
        return;
    g_bloodPositions.push_back({pos.x, pos.y, pos.z, 0.0f});
}

bool SurfaceMarks_IsBloodAt(const S_vector &pos)
{
    if (!IsClient())
        return false;
    for (const S_vector4 &blood : g_bloodPositions) {
        if (std::abs(pos.x - blood.x) < BLOOD_REACH_XZ && std::abs(pos.y - blood.y) < BLOOD_REACH_Y &&
            std::abs(pos.z - blood.z) < BLOOD_REACH_XZ)
            return true;
    }
    return false;
}

void SurfaceMarks_Tick(int timeDelta)
{
    if (!IsClient())
        return;
    const uint32_t now = GetGame()->GetMissionTime();
    // Marks drawn in the last frame fade out; others go at once.
    uint32_t seen = static_cast<uint32_t>(GetGame()->GetScene()->GetRenderStamp());
    seen = seen < 2 ? 0 : seen - 1;
    const auto retire = [seen](S_SurfaceMark *mark) {
        if (static_cast<uint32_t>(mark->visual->GetRenderStamp()) < seen) {
            RemoveFromScene(mark, false);
            ReturnMark(mark);
        } else {
            mark->time = 0;
            g_fadingMarks.push_back(mark);
        }
    };

    g_detailCheckTime += timeDelta;
    if (g_detailCheckTime > DETAIL_CHECK_TIME) {
        g_detailCheckTime += -DETAIL_CHECK_TIME - std::rand() % 200;
        g_surfaceMarksEnabled = g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_DECALS);
        // The oldest footprints of humans with too many.
        for (auto it = g_footprints.begin(); it != g_footprints.end();) {
            S_SurfaceMark *mark = *it;
            uint32_t *count = Counter(mark->humanId);
            if (*count > static_cast<uint32_t>(g_maxFootprintsPerHuman)) {
                --*count;
                it = g_footprints.erase(it);
                retire(mark);
            } else {
                ++it;
            }
        }
    }
    if (g_footprintLifetime != 0) {
        while (!g_footprints.empty() &&
               g_footprints.front()->time + static_cast<uint32_t>(g_footprintLifetime) * 1000 <= now) {
            S_SurfaceMark *mark = g_footprints.front();
            --*Counter(mark->humanId);
            g_footprints.pop_front();
            retire(mark);
        }
    }

    g_fadeTime += timeDelta;
    if (g_fadeTime < FADE_STEP)
        return;
    g_fadeTime -= FADE_STEP;
    for (auto it = g_fadingMarks.begin(); it != g_fadingMarks.end();) {
        S_SurfaceMark *mark = *it;
        mark->time += FADE_STEP;
        if (mark->time < FADE_TIME) {
            const float opacity = 1.0f - static_cast<float>(mark->time) * 0.00033333333f;
            I3D_projector_visual *visual = Projector(mark);
            if (opacity < visual->GetIntensity()) {
                if (mark->time <= FADE_STEP) {
                    visual->SetBuffered(0);
                    visual->SetMode(5);
                }
                visual->SetIntensity(opacity);
                visual->Update();
            }
            ++it;
        } else {
            mark->visual->LinkTo(nullptr, 0);
            GetGame()->GetScene()->RemoveFrame(mark->visual);
            it = g_fadingMarks.erase(it);
            Projector(mark)->SetBuffered(3);
            ReturnMark(mark);
        }
    }
}

void SurfaceMarks_SetHumanVisible(C_Human *human, bool hide)
{
    if (!IsClient())
        return;
    for (S_SurfaceMark *mark : g_footprints) {
        C_Actor *owner = g_actorsModule.GetHuman(mark->humanId);
        if (owner && (owner == human || !human))
            mark->visual->SetOn(!hide);
    }
}

// ------------------------------------------------------------------ saved games

bool SurfaceMarks_Save(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_MARKS))
        return false;
    chunk.Begin(CHUNK_MARKS_FOOTPRINTS);
    for (const S_SurfaceMark *mark : g_footprints)
        SaveMark(chunk, mark);
    chunk.Ascend();
    chunk.Begin(CHUNK_MARKS_FADING);
    for (const S_SurfaceMark *mark : g_fadingMarks)
        SaveMark(chunk, mark);
    chunk.Ascend();
    chunk.Begin(CHUNK_MARKS_BLOOD);
    for (const S_vector4 &blood : g_bloodPositions)
        chunk.WriteChunk(CHUNK_BLOOD_POS, &blood, 0x0C);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_MARKS_ENABLED, g_surfaceMarksEnabled);
    chunk.WriteChunk(CHUNK_MARKS_MAX_PER_HUMAN, g_maxFootprintsPerHuman);
    chunk.WriteChunk(CHUNK_MARKS_LIFETIME, g_footprintLifetime);
    chunk.WriteChunk(CHUNK_MARKS_UNKNOWN4, g_unknown84f6bc);
    chunk.Ascend();
    return true;
}

bool SurfaceMarks_Load(C_chunk &chunk)
{
    SurfaceMarks_Clear();
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_MARKS) {
        chunk.Ascend();
        return false;
    }
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        int value = 0;
        switch (id) {
        case CHUNK_MARKS_ENABLED:
            chunk.Read(value);
            g_surfaceMarksEnabled = value != 0;
            break;
        case CHUNK_MARKS_MAX_PER_HUMAN:
            chunk.Read(g_maxFootprintsPerHuman);
            break;
        case CHUNK_MARKS_LIFETIME:
            chunk.Read(g_footprintLifetime);
            break;
        case CHUNK_MARKS_UNKNOWN4:
            chunk.Read(g_unknown84f6bc);
            break;
        case CHUNK_MARKS_FOOTPRINTS:
            // (The loaded footprints are not counted for their humans, as in the original.)
            LoadMarks(chunk, g_footprints);
            break;
        case CHUNK_MARKS_FADING:
            LoadMarks(chunk, g_fadingMarks);
            break;
        case CHUNK_MARKS_BLOOD: {
            uint16_t bloodId;
            while (!chunk.AtEnd() && chunk.Descend(bloodId)) {
                S_vector pos;
                chunk.Read(&pos, 0x0C);
                chunk.Ascend();
                g_bloodPositions.push_back({pos.x, pos.y, pos.z, 0.0f});
            }
            break;
        }
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}
