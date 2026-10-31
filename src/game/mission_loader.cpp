#include "game/mission_loader.h"

#include <cstdio>
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/car_tables.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/geffects.h"
#include "game/human_anim_tables.h"
#include "game/lightmap_groups.h"
#include "game/loader_screen.h"
#include "game/module.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/tshift.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The scene's loading progress (an I3D load callback): the loading screen's first stage (not
// for messages 0x10 and 0x20).
void __stdcall SceneLoadProgress(int message, float fraction, uint32_t, void *)   // 0x006d4fc0
{
    if (message != 0x10 && message != 0x20)
        g_loaderScreen.SetProgress(LOADER_STAGE_SCENE, fraction);
}
// The scene file's open flags.
constexpr uint32_t SCENE_OPEN_FLAGS = 7;

// The modules load their mission data when C_game::LoadMission returns, however it ends: the
// server's side, then the client's.
struct S_ModuleMissionData {
    S_ModuleMissionData() = default;
    S_ModuleMissionData(const S_ModuleMissionData &) = delete;
    S_ModuleMissionData &operator=(const S_ModuleMissionData &) = delete;
    ~S_ModuleMissionData()                              // 0x006cb130
    {
        if (g_gameRole & GAME_ROLE_SERVER)
            ModuleManager::BroadcastLoadMissionData(true);
        if (g_gameRole & GAME_ROLE_CLIENT)
            ModuleManager::BroadcastLoadMissionData(false);
    }
};

// actors.bin / sounds.bin: the object chunk if the file starts with one.
void LoadObjectFile(C_MissionLoader &loader, const char *path, int stage)
{
    C_chunk chunk;
    if (!chunk.Open(path, false))
        return;
    // (The original compares an uninitialized id when the file is empty, and ascends then too.)
    uint16_t id = 0;
    if (chunk.Descend(id)) {
        if (id == MISSION_CHUNK_OBJECTS)
            loader.LoadObjects(chunk, stage);
        chunk.Ascend();
    }
    chunk.Close();
}

} // namespace

int C_game::LoadMission(const char *name, C_MissionDef *mission)   // 0x006caad0
{
    m_unknown66 = 0;
    m_unknown68 = 0;
    m_unknownD8 = 0;
    const S_ModuleMissionData moduleData;
    C_MissionLoader loader;
    loader.Begin(name, mission);
    if (!EnsureHumanAnimTablesLoaded())
        return 1;
    loader.LoadScene();
    m_scene->UnknownF0();
    const int failed = loader.LoadScene2();
    // (Dropped: the copy protection's stamps, here and after the loading.)
    loader.LoadActors();
    loader.LoadSounds();
    loader.LoadCheckpoints();
    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.3f);
    if (failed)
        return 1;
    char path[256];
    std::sprintf(path, "missions\\%s\\tree.klz", name);
    g_collisionWorld.Load(path, true);
    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.8f);
    g_particlesModule.SetScene(m_scene);
    g_particlesModule.LoadStaticEmitters();
    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 1.0f);
    m_missionLoaded = true;
    g_loaderScreen.FinishLoading();
    return 0;
}

void C_MissionLoader::Begin(const char *name, const C_MissionDef *mission)
{
    g_loaderScreen.Start(name, (g_gameRole & GAME_ROLE_CLIENT) != 0, (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL,
                         true, mission);
    GetGame()->FreeMission();
    // Fix: the name as text (the original used it as the format, here and for the loader's).
    GetGame()->m_missionName = C_fstr("%s", name);
    g_carScene = GetGame()->GetScene();
    m_name = C_fstr("%s", name);
    g_modelCache.Clear();
    g_shadowCache.Clear();
    g_versionModelCache.Clear();
}

HRESULT C_MissionLoader::LoadScene()
{
    const C_fstr path("missions\\%s\\Scene", m_name.c_str());
    return GetGame()->GetScene()->Open(path, SCENE_OPEN_FLAGS, reinterpret_cast<void *>(&SceneLoadProgress), nullptr);
}

void C_MissionLoader::LoadActors()
{
    LoadObjectFile(*this, C_fstr("missions\\%s\\actors.bin", m_name.c_str()), LOADER_STAGE_ACTORS);
}

void C_MissionLoader::LoadSounds()
{
    LoadObjectFile(*this, C_fstr("missions\\%s\\sounds.bin", m_name.c_str()), LOADER_STAGE_SOUNDS);
}

void C_MissionLoader::LoadCheckpoints()
{
    C_Checkpoints *checkpoints = GetGame()->GetCheckpoints();
    C_chunk chunk;
    if (chunk.Open(C_fstr("missions\\%s\\check2.bin", m_name.c_str()), false)) {
        LoadCheckpointNetwork(&chunk, checkpoints);
        LoadCheckpointNetwork(&chunk, GetGame()->GetCheckpoints()->GetRoadNetwork());
    } else {
        checkpoints->Clear();
        // (Quirk kept: the old format's check.bin is opened, not read.)
        if (!chunk.Open(C_fstr("missions\\%s\\check.bin", m_name.c_str()), false))
            GetGame()->GetCheckpoints()->Clear();
    }
    chunk.Close();
}

// --- scene2.bin (part 8f-iii) -----------------------------------------------------------------------

namespace {

// scene2.bin's object chunk's entries.
enum : uint16_t {
    OBJECTS_VERSION = 0x0001,                           // C_game +0x66 / +0x68
    OBJECTS_FOV = 0x3010,                               // the game's field of view
    OBJECTS_FAR_CLIP = 0x3011,                          // the near one then 0.1
    OBJECTS_ORTHOGONAL = 0x3012,                        // the scene's camera orthogonal
    OBJECTS_UNKNOWN_3013 = 0x3013,                      // (a float, not used)
    OBJECTS_UNKNOWN_3200 = 0x3200,                      // the scene's +0x210
    OBJECTS_UNKNOWN_3211 = 0x3211,                      // the scene's slot 0xE4
    OBJECTS_LIST = 0x4000,                              // its 0x4010 objects (part 8f-iv)
    OBJECTS_OBJECT = 0x4010,
    OBJECTS_TSHIFT_LIST = 0xAE00,                       // its 0xAE01 shifting textures
    OBJECTS_TSHIFT = 0xAE01,
    OBJECTS_ACTOR_LIST = 0xAE20,                        // its 0xAE21 actors
    OBJECTS_ACTOR = 0xAE21,                             // 0xAE23 the frame's name, 0xAE22 the type,
    OBJECTS_ACTOR_TYPE = 0xAE22,                        //   0xAE24 the properties
    OBJECTS_ACTOR_FRAME = 0xAE23,
    OBJECTS_ACTOR_PROPERTIES = 0xAE24,
    OBJECTS_CHECKPOINTS = 0xAE40,                       // the walking and the road networks
    OBJECTS_DRIVER_STATE_1 = 0xB004,                    // the driver's state 1 (a byte)
    OBJECTS_RENDER_STATE_C = 0xB600,                    // IGraph's render state 0xC
    OBJECTS_UNKNOWN_B650 = 0xB650,                      // C_game +0xD8
    OBJECTS_SHADOWS = 0xB700,                           // the effects module's shadows' parameters
    OBJECTS_LIGHTMAP_GROUPS = 0xB810,
    OBJECTS_UNKNOWN_B820 = 0xB820,                      // its 0xB820 frames' names (the scene's slot 0x128)
    OBJECTS_MUSIC = 0xB840,
    OBJECTS_ALARM_MUSIC = 0xB880,
};

// The loading screen's progress: how far into the file.
float FileProgress(const C_chunk &chunk)
{
    const int position = chunk.GetPosition();
    const int size = chunk.GetFileSize();
    return size != 0 ? static_cast<float>(position) / static_cast<float>(size) : 1.0f;
}

// The rest of the current chunk as text (at most 999 characters, 0x00415d00), the chunk left.
C_str ReadRemainingString(C_chunk &chunk)
{
    char text[1000];
    chunk.ReadString(text, sizeof text);
    chunk.Ascend();
    return C_str(text);
}

// The scene's camera: a new one at (10, 5, 10) looking down and back without one.
const S_vector DEFAULT_CAMERA_POS(10.0f, 5.0f, 10.0f);
const S_vector DEFAULT_CAMERA_DIR(-1.0f, -0.1f, -1.0f);
constexpr float CAMERA_NEAR = 0.1f;
constexpr float CAMERA_FAR = 500.0f;
constexpr float CAMERA_FOV = 1.3962635f;                // 80 degrees
// scene2.bin's chunk 0x3011: the near clip; 0x3211's first float at least.
constexpr float SCENE_NEAR_CLIP = 0.1f;
constexpr float UNKNOWN_E4_MIN = 0.1f;
// The scene's frame types EnumFrames names.
constexpr uint32_t NAMED_FRAME_TYPES = 0x4FFFF;

} // namespace

uint32_t __stdcall C_MissionLoader::NameFrame(I3D_frame *frame, void *context)
{
    // Fix: the name as text (the original used it as the format, as for the sectors below).
    static_cast<C_MissionLoader *>(context)->m_frames[C_fstr("%s", frame->GetName())] = frame;
    return 0;
}

int C_MissionLoader::LoadScene2()
{
    LoadTyres();
    InitCarSurfaces();
    C_chunk chunk;
    if (!chunk.Open(C_fstr("missions\\%s\\scene2.bin", m_name.c_str()), false))
        return 3;
    GetGame()->GetScene()->EnumFrames(&NameFrame, this, NAMED_FRAME_TYPES, nullptr);
    // The three sectors by their names.
    I3D_frame *const sectors[] = {reinterpret_cast<I3D_frame *>(GetGame()->GetScene()->GetPrimarySector()),
                                  reinterpret_cast<I3D_frame *>(GetGame()->GetScene()->GetBackdropSector()),
                                  reinterpret_cast<I3D_frame *>(GetGame()->GetScene()->GetOmniSector())};
    for (I3D_frame *sector : sectors)
        m_frames[C_fstr("%s", sector->GetName())] = sector;

    I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera();
    if (!camera) {
        camera = static_cast<I3D_camera *>(I3DGetDriver()->CreateFrame(FRAME_CAMERA));
        GetGame()->GetScene()->SetActiveCamera(camera);
        GetGame()->GetScene()->AddFrame(camera);
        camera->SetWorldDir(DEFAULT_CAMERA_DIR, 0.0f);
        camera->SetPosInline(DEFAULT_CAMERA_POS);
        camera->Update();
        camera->Release();
    }
    camera->SetOrthogonal(false);
    camera->SetRange(CAMERA_NEAR, CAMERA_FAR);
    camera->SetFOV(CAMERA_FOV);
    GetGame()->GetScene()->SetActiveCamera(camera);
    camera->Update();

    uint16_t id;
    if (!chunk.Descend(id))
        return 1;
    if (id == MISSION_CHUNK_OBJECTS && !LoadObjects(chunk, LOADER_STAGE_SCENE2))
        return 1;
    chunk.Ascend();
    return 0;
}

bool C_MissionLoader::LoadObjects(C_chunk &chunk, int stage)
{
    I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera();
    // (The entries' values live across the entries: a short entry keeps the last one's.)
    float value = 0.0f;
    S_vector unknown210(0.0f, 0.0f, 0.0f);
    float unknownE4[2] = {0.0f, 0.0f};
    uint8_t driverState = 0;
    int renderState = 0;
    while (chunk.HasMore()) {
        uint16_t id;
        if (!chunk.Descend(id))
            return false;
        g_loaderScreen.SetProgress(stage, FileProgress(chunk));
        switch (id) {
        case OBJECTS_VERSION:
            chunk.Read(GetGame()->m_unknown66);
            chunk.Read(GetGame()->m_unknown68);
            break;
        case OBJECTS_FOV:
        case OBJECTS_FAR_CLIP:
        case OBJECTS_UNKNOWN_3013:
            chunk.Read(value);
            if (id == OBJECTS_FOV) {
                GetGame()->m_fov = value;
            } else if (id == OBJECTS_FAR_CLIP) {
                GetGame()->m_nearClip = SCENE_NEAR_CLIP;
                GetGame()->m_farClip = value;
            }
            break;
        case OBJECTS_ORTHOGONAL:
            if (camera) {
                camera->SetOrthogonal(true);
                camera->Update();
            }
            break;
        case OBJECTS_UNKNOWN_3200:
            chunk.Read(&unknown210, sizeof(float) * 3);
            GetGame()->GetScene()->SetUnknown210(unknown210);
            break;
        case OBJECTS_UNKNOWN_3211:
            chunk.Read(unknownE4, sizeof unknownE4);
            unknownE4[0] = unknownE4[0] > UNKNOWN_E4_MIN ? unknownE4[0] : UNKNOWN_E4_MIN;
            GetGame()->GetScene()->UnknownE4(unknownE4[0], unknownE4[1]);
            break;
        case OBJECTS_LIST:
            while (chunk.HasMore()) {
                g_loaderScreen.SetProgress(stage, FileProgress(chunk));
                if (!chunk.Descend(id))
                    return false;
                if (id == OBJECTS_OBJECT && !LoadObject(chunk))
                    return false;
                chunk.Ascend();
            }
            break;
        case OBJECTS_TSHIFT_LIST:
            while (chunk.HasMore()) {
                uint16_t entry;
                if (!chunk.Descend(entry))
                    return false;
                if (entry == OBJECTS_TSHIFT) {
                    // (Uninitialized in the original when they do not read.)
                    int unknown0C = 0, unknown10 = 0, length = 0;
                    chunk.Read(unknown0C);
                    chunk.Read(unknown10);
                    chunk.Read(length);
                    // Fix: the length kept in the buffer (the original wrote past it), the name as
                    // text (the original used it as the format).
                    char text[256];
                    if (length < 0 || length > static_cast<int>(sizeof text) - 1)
                        length = sizeof text - 1;
                    chunk.Read(text, length);
                    text[length] = '\0';
                    I3D_frame *frame = m_frames[C_fstr("%s", text)];
                    if (frame) {
                        const uint32_t type = frame->GetVisualType();
                        if (type == I3DVISUAL_LMAP || type == I3DVISUAL_OBJECT) {
                            I3D_mesh *mesh = static_cast<I3D_visual *>(frame)->GetVisualMesh();
                            if (mesh) {
                                S_TShiftEntry shift = {mesh, frame};
                                shift.unknown0C = unknown0C;
                                shift.unknown10 = unknown10;
                                g_tshiftModule.AddEntry(shift);
                            }
                        }
                    }
                }
                chunk.Ascend();
            }
            break;
        case OBJECTS_ACTOR_LIST:
            if (!chunk.HasMore())
                break;
            do {
                uint16_t entry;
                if (!chunk.Descend(entry))
                    return false;
                if (entry == OBJECTS_ACTOR) {
                    // The frame's name first, then the type makes the actor, then its properties.
                    C_Actor *actor = nullptr;
                    I3D_frame *frame = nullptr;
                    while (chunk.HasMore()) {
                        uint16_t part;
                        if (!chunk.Descend(part))
                            return false;
                        if (part == OBJECTS_ACTOR_TYPE) {
                            int type = 0;
                            chunk.Read(type);
                            chunk.Ascend();
                            actor = g_actorsModule.CreateActor(static_cast<E_ActorType>(type), frame, true, -1);
                            if (actor && !actor->SetFrame(frame)) {
                                g_actorsModule.DestroyActor(actor, true);
                                actor = nullptr;
                            }
                        } else if (part == OBJECTS_ACTOR_FRAME) {
                            const C_str name = ReadRemainingString(chunk);
                            frame = m_frames[name];
                            if (!frame)
                                frame = GetGame()->GetScene()->FindFrame(name, 0xCFFFF);
                            if (!frame)
                                frame = I3DGetDriver()->FindFrame(GetGame()->GetScene(), name);
                        } else if (part == OBJECTS_ACTOR_PROPERTIES) {
                            if (actor && chunk.HasMore())
                                actor->ReadProperties(chunk);
                            chunk.Ascend();
                        }
                        // (Quirk kept: another part is not left; the next parts are read in it.)
                    }
                }
                chunk.Ascend();
            } while (chunk.HasMore());
            break;
        case OBJECTS_CHECKPOINTS:
            LoadCheckpointNetwork(&chunk, GetGame()->GetCheckpoints());
            LoadCheckpointNetwork(&chunk, GetGame()->GetCheckpoints()->GetRoadNetwork());
            break;
        case OBJECTS_DRIVER_STATE_1: {
            const bool current = I3DGetDriver()->GetState(1) != 0;
            chunk.Read(&driverState, 1);
            if (driverState != static_cast<uint8_t>(current))
                I3DGetDriver()->SetState(1, driverState);
            break;
        }
        case OBJECTS_RENDER_STATE_C:
            chunk.Read(renderState);
            GetIGraph()->SetRenderState(0xC, renderState);
            break;
        case OBJECTS_UNKNOWN_B650: {
            int unknown = 0;
            chunk.Read(unknown);
            GetGame()->m_unknownD8 = unknown;
            break;
        }
        case OBJECTS_SHADOWS: {
            // The file's order: the heights and sizes first, then the colour and the rest. (Quirk
            // kept: the defaults are overwritten even when the entry does not read; zero here, the
            // original's stack there.)
            uint32_t raw[11] = {};
            chunk.Read(raw, sizeof raw);
            S_ShadowParams params;
            std::memcpy(&params.maxHeight, &raw[0], sizeof(uint32_t) * 5);
            std::memcpy(&params.color, &raw[5], sizeof(uint32_t) * 3);
            std::memcpy(&params.unknown10, &raw[8], sizeof(uint32_t) * 3);
            GetGEffects()->SetShadowParams(&params);
            break;
        }
        case OBJECTS_LIGHTMAP_GROUPS:
            GetLightMapGroups()->LoadGroups(&chunk);
            break;
        case OBJECTS_UNKNOWN_B820: {
            // (Quirk kept: a header that does not read keeps the last entry's id.)
            uint16_t entry = 0;
            while (chunk.HasMore()) {
                chunk.Descend(entry);
                if (entry != OBJECTS_UNKNOWN_B820) {
                    chunk.Ascend();
                    break;
                }
                const C_str name = ReadRemainingString(chunk);
                if (I3D_frame *frame = GetGame()->GetScene()->FindFrame(name, 0xFFFF))
                    GetGame()->GetScene()->Unknown128(frame);
            }
            break;
        }
        case OBJECTS_MUSIC:
            chunk.Read(GetGame()->m_music);
            break;
        case OBJECTS_ALARM_MUSIC:
            chunk.Read(GetGame()->m_alarmMusic);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    return true;
}
