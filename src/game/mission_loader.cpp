#include "game/mission_loader.h"

#include <cstdio>
#include <cstring>
#include <vector>

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

// --- scene2.bin's objects (part 8f-iv) -------------------------------------------------------------

namespace {

// An object's entries (chunk 0x4010).
enum : uint16_t {
    OBJECT_NAME = 0x0010,                               // the frame: the scene's, else a new one
    OBJECT_POS = 0x0020,
    OBJECT_ROT = 0x0022,                                // the file's order: s, x, y, z
    OBJECT_UNLINKED_POS = 0x002C,                       // the position without a parent
    OBJECT_SCALE = 0x002D,
    OBJECT_MODEL = 0x2012,                              // a model's file (none: the model removed)
    OBJECT_TYPE = 0x4011,                               // E_I3D_FRAME_TYPE (0: the scene's frame)
    OBJECT_VISUAL_TYPE = 0x4012,
    OBJECT_PARENT = 0x4020,                             // its 0x0010 the parent's name
    OBJECT_HIDDEN = 0x4033,
    OBJECT_CAMERA = 0x4036,                             // 0x3010 the field of view, 0x3011 the far clip
    OBJECT_LIGHT = 0x4040,
    OBJECT_BBOX = 0x4050,                               // a dummy's box
    OBJECT_SOUND = 0x4060,
    OBJECT_OCCLUDER = 0x4080,                           // 0x4082 the vertices, 0x4083 the triangles
    OBJECT_VISUAL = 0x4090,                             // a visual's type: another one replaces it
    OBJECT_LIGHTMAPS = 0x40A0,                          // a lightmapped visual's sets (version 3 on)
    OBJECT_PROJECTOR = 0x40F0,
    OBJECT_UNKNOWN_B110 = 0xB110,                       // six fields (a lens flare's, inferred)
    OBJECT_AREA = 0xB150,                               // 0xB170 the vertices, 0xB180 the triangles
    OBJECT_UNKNOWN_B400 = 0xB400,
    OBJECT_SECTOR = 0xB401,                             // a sector's sound environment
};
enum : uint16_t {
    CAMERA_ENTRY_FOV = 0x3010,
    CAMERA_ENTRY_FAR_CLIP = 0x3011,
    LIGHT_TYPE = 0x4041,
    LIGHT_POWER = 0x4042,                               // (version 7 on: the specular and ambient
    LIGHT_COLOR = 0x0026,                               //   power / colour after it)
    LIGHT_CONE = 0x4043,
    LIGHT_RANGE = 0x4044,                               // (version 7 on: the attenuation after it)
    LIGHT_MODE = 0x4045,
    LIGHT_SECTOR = 0x4046,                              // a sector's name
    SOUND_TYPE = 0x4061,
    SOUND_VOLUME = 0x4062,
    SOUND_OUT_VOLUME = 0x4063,
    SOUND_CONE = 0x4064,
    SOUND_RANGE = 0x4065,                               // three values (the fourth 0)
    SOUND_CHUNK_LOOP = 0x4066,
    SOUND_ON = 0x4067,
    SOUND_RANGE_4 = 0x4068,
    SOUND_FILE = 0x0010,
    SOUND_SECTOR = 0xB200,                              // a sector's name
    SOUND_AREA = 0xB300,                                // an area's name
    SOUND_FREQUENCY = 0xB800,
    OCCLUDER_VERTICES = 0x4082,
    OCCLUDER_FACES = 0x4083,
    AREA_VERTICES = 0xB170,
    AREA_FACES = 0xB180,
    PROJECTOR_RANGE = 0x40F1,                           // (+0x1E4)
    PROJECTOR_INTENSITY = 0x40F2,
    PROJECTOR_COLOR = 0x40F3,
    PROJECTOR_MODE = 0x40F4,
    PROJECTOR_BUFFERED = 0x40F5,
    PROJECTOR_ORTHOGONAL = 0x40F6,                      // a byte
    PROJECTOR_TEXTURE = 0x40F7,                         // its name (the projection rebuilt)
    UNKNOWN_B112 = 0xB112,                              // +0x1E4
    UNKNOWN_B113 = 0xB113,                              // +0x1E0
    UNKNOWN_B114 = 0xB114,                              // +0x1EC (an int)
    UNKNOWN_B115 = 0xB115,                              // +0x220
    UNKNOWN_B116 = 0xB116,                              // +0x224
    UNKNOWN_B117 = 0xB117,                              // +0x240
};

// What an object's entries set (the ones its end reads).
enum : uint32_t {
    OBJECT_HAS_POS = 0x1,
    OBJECT_HAS_ROT = 0x2,
    OBJECT_HAS_SCALE = 0x4,
    OBJECT_HAS_PARENT = 0x10,
    OBJECT_IS_HIDDEN = 0x10000,
    OBJECT_IS_NEW = 0x20000,                            // made by its name's entry
};

// The scene version (C_game +0x66, chunk 0x0001) from which lights read their specular and
// ambient parts and their attenuation, and the ones below which their modes gain bits.
constexpr int16_t SCENE_VERSION_LIGHT_PARTS = 7;
constexpr int16_t SCENE_VERSION_LIGHTMAPS = 3;
constexpr int16_t SCENE_VERSION_LIGHT_MODE_40 = 5;      // (below it: mode | 0x40)
constexpr int16_t SCENE_VERSION_LIGHT_MODE_20 = 6;      // (below it: mode | 0x20)
// An old light's attenuation when it has a linear part or no quadratic one (1 / 1.01, 0.01 / 1.01).
constexpr float OLD_LIGHT_ATTENUATION_0 = 0.990099013f;
constexpr float OLD_LIGHT_ATTENUATION_2 = 0.00990098994f;
// A camera's near clip.
constexpr float OBJECT_CAMERA_NEAR = 0.1f;

// An occluder's or an area's mesh: `vertices` a count then 12 bytes each, `faces` a count then
// three uint16 each, built with each faces entry (vertices 0x10 bytes apart). Another entry is not
// left (quirk kept). False when a header does not read.
template <class T>
bool LoadConvexMesh(C_chunk &chunk, T *frame, uint16_t vertices, uint16_t faces)
{
    // (Uninitialized in the original until the first vertices: the occluder's count kept from
    // the last occluder of the object.)
    int numVerts = 0;
    std::vector<S_vector4> verts;
    while (chunk.HasMore()) {
        uint16_t id;
        if (!chunk.Descend(id))
            return false;
        if (id == vertices) {
            chunk.Read(numVerts);
            std::vector<S_vector> file(numVerts > 0 ? numVerts : 0);
            chunk.Read(file.data(), static_cast<int>(file.size() * sizeof(S_vector)));
            chunk.Ascend();
            verts.assign(file.size(), S_vector4{0.0f, 0.0f, 0.0f, 0.0f});
            for (size_t i = 0; i < file.size(); ++i) {   // (0x006d1ed0)
                verts[i].x = file[i].x;
                verts[i].y = file[i].y;
                verts[i].z = file[i].z;
            }
        } else if (id == faces) {
            int numFaces = 0;
            chunk.Read(numFaces);
            std::vector<uint16_t> tris(numFaces > 0 ? numFaces * 3 : 0);
            chunk.Read(tris.data(), static_cast<int>(tris.size() * sizeof(uint16_t)));
            chunk.Ascend();
            frame->Build(verts.empty() ? nullptr : verts.data(), numVerts, tris.data(), numFaces, sizeof(S_vector4));
        }
    }
    return true;
}

// The frame's type with a null frame safe.
bool IsType(const I3D_frame *frame, E_I3D_FRAME_TYPE type) { return frame && frame->GetType() == type; }

} // namespace

bool C_MissionLoader::LoadObject(C_chunk &chunk)
{
    I3D_scene *const scene = GetGame()->GetScene();
    const auto SceneVersion = [] { return GetGame()->m_unknown66; };
    I3D_frame *frame = nullptr;
    I3D_frame *parent = nullptr;
    C_str name;
    int type = 0;
    uint32_t visualType = 0;
    uint32_t flags = 0;
    bool haveUnlinkedPos = false;
    S_vector pos(0.0f, 0.0f, 0.0f), unlinkedPos(0.0f, 0.0f, 0.0f);
    S_quat rot(1.0f, 0.0f, 0.0f, 0.0f);
    S_vector scale(1.0f, 1.0f, 1.0f);
    // (The entries' values live across the entries: a short entry keeps the last one's; zero
    // here, the original's stack there.)
    float value = 0.0f;
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int number = 0;
    S_vector vector(0.0f, 0.0f, 0.0f);
    I3D_bbox box = {S_vector(0.0f, 0.0f, 0.0f), 0.0f, S_vector(0.0f, 0.0f, 0.0f), 0.0f};
    uint16_t id = 0;

    while (chunk.HasMore()) {
        if (!chunk.Descend(id))
            return false;
        switch (id) {
        case OBJECT_NAME: {
            name = ReadRemainingString(chunk);
            if (type == 0) {
                frame = m_frames[name];
                break;
            }
            if (m_frames[name])
                break;
            // (Another type keeps the frame: the object's last one.)
            switch (type) {
            case FRAME_VISUAL:
                frame = I3DGetDriver()->CreateVisual(visualType);
                break;
            case FRAME_CAMERA:
                frame = I3DGetDriver()->CreateFrame(FRAME_CAMERA);
                static_cast<I3D_camera *>(frame)->SetFOV(CAMERA_FOV);
                break;
            case FRAME_LIGHT:
            case FRAME_SOUND:
            case FRAME_DUMMY:
            case FRAME_USER:
            case FRAME_MODEL:
            case FRAME_OCCLUDER:
            case FRAME_AREA:
                frame = I3DGetDriver()->CreateFrame(type);
                break;
            default:
                break;
            }
            if (frame) {
                frame->SetName(name);
                flags |= OBJECT_IS_NEW;
                scene->AddFrame(frame);
                frame->Release();
                // Fix: the name as text (the original used it as the format, as below).
                m_frames[C_fstr("%s", frame->GetName())] = frame;
            }
            break;
        }
        case OBJECT_POS:
            chunk.Read(&pos, sizeof pos);
            chunk.Ascend();
            flags |= OBJECT_HAS_POS;
            break;
        case OBJECT_ROT: {
            float file[4] = {rot.s, rot.v.x, rot.v.y, rot.v.z};
            chunk.Read(file, sizeof file);
            chunk.Ascend();
            rot = S_quat::FromFile(file);
            flags |= OBJECT_HAS_ROT;
            break;
        }
        case OBJECT_UNLINKED_POS:
            chunk.Read(&unlinkedPos, sizeof unlinkedPos);
            chunk.Ascend();
            haveUnlinkedPos = true;
            break;
        case OBJECT_SCALE:
            chunk.Read(&scale, sizeof scale);
            chunk.Ascend();
            flags |= OBJECT_HAS_SCALE;
            break;
        case OBJECT_MODEL: {
            if (!IsType(frame, FRAME_MODEL)) {
                chunk.Ascend();
                break;
            }
            name = ReadRemainingString(chunk);
            if (name.Size() == 0) {
                // No file: the model leaves the scene (and the names).
                I3D_frame *&entry = m_frames[C_fstr("%s", frame->GetName())];
                if (entry)
                    entry = nullptr;
                frame->AddRef();
                scene->RemoveFrame(frame);
                frame->Release();
                frame = nullptr;
                break;
            }
            char file[1000];
            std::strcpy(file, name);
            if (char *dot = std::strrchr(file, '.'))
                *dot = '\0';
            I3D_model *const model = static_cast<I3D_model *>(frame);
            if (g_modelCache.OpenInto(model, file) >= 0)
                model->EnumFrames(&NameFrame, this, FRAME_MASK_ANY, nullptr);
            else
                dbgPrintf(C_fstr("# ERROR LOADER : modelfile '%s' not found", file));
            GetGEffects()->CreateShadow(frame);
            break;
        }
        case OBJECT_TYPE:
            chunk.Read(type);
            chunk.Ascend();
            break;
        case OBJECT_VISUAL_TYPE:
            chunk.Read(visualType);
            chunk.Ascend();
            break;
        case OBJECT_PARENT:
            if (frame) {
                uint16_t part;
                if (!chunk.Descend(part))
                    return false;
                if (part == OBJECT_NAME) {
                    name = ReadRemainingString(chunk);
                    parent = m_frames[name];
                    if (parent)
                        flags |= OBJECT_HAS_PARENT;
                    else if (haveUnlinkedPos)
                        pos = unlinkedPos;
                } else {
                    chunk.Ascend();
                }
            }
            chunk.Ascend();
            break;
        case OBJECT_HIDDEN:
            flags |= OBJECT_IS_HIDDEN;
            chunk.Ascend();
            break;
        case OBJECT_CAMERA:
            if (IsType(frame, FRAME_CAMERA)) {
                I3D_camera *const camera = static_cast<I3D_camera *>(frame);
                while (chunk.HasMore()) {
                    // (A header that does not read is skipped; another entry is not left.)
                    if (!chunk.Descend(id))
                        continue;
                    if (id == CAMERA_ENTRY_FOV) {
                        chunk.Read(value);
                        chunk.Ascend();
                        camera->SetFOV(value);
                        camera->Update();
                    } else if (id == CAMERA_ENTRY_FAR_CLIP) {
                        chunk.Read(value);
                        chunk.Ascend();
                        camera->SetRange(OBJECT_CAMERA_NEAR, value);
                    }
                }
            }
            chunk.Ascend();
            break;
        case OBJECT_LIGHT: {
            if (!IsType(frame, FRAME_LIGHT)) {
                chunk.Ascend();
                break;
            }
            I3D_light *const light = static_cast<I3D_light *>(frame);
            while (light->NumSectors() != 0)
                if (I3D_sector *sector = light->GetSector(0))
                    sector->RemoveLight(light);
            while (chunk.HasMore()) {
                if (!chunk.Descend(id))
                    return false;
                switch (id) {
                case LIGHT_TYPE:
                    chunk.Read(number);
                    light->SetLightType(number);
                    break;
                case LIGHT_POWER:
                    chunk.Read(value);
                    light->SetPower(value);
                    if (SceneVersion() >= SCENE_VERSION_LIGHT_PARTS) {
                        chunk.Read(value);
                        light->SetSpecularPower(value);
                        chunk.Read(value);
                        light->SetAmbientPower(value);
                    }
                    break;
                case LIGHT_COLOR:
                    chunk.Read(&vector, sizeof vector);
                    light->SetColor(vector);
                    if (SceneVersion() >= SCENE_VERSION_LIGHT_PARTS) {
                        chunk.Read(&vector, sizeof vector);
                        light->SetSpecularColor(vector);
                        chunk.Read(&vector, sizeof vector);
                        light->SetAmbientColor(vector);
                    }
                    break;
                case LIGHT_CONE:
                    chunk.Read(values, sizeof(float) * 2);
                    light->SetCone(values[0], values[1]);
                    break;
                case LIGHT_RANGE:
                    if (SceneVersion() < SCENE_VERSION_LIGHT_PARTS) {
                        // The near range and the range; the attenuation a default one unless
                        // it is linear-free and quadratic.
                        chunk.Read(values, sizeof(float) * 2);
                        light->SetRange(values[1]);
                        if (light->GetOwnAttenuation(1) != 0.0f || !(light->GetOwnAttenuation(2) > 0.0f))
                            light->SetAttenuation(OLD_LIGHT_ATTENUATION_0, 0.0f, OLD_LIGHT_ATTENUATION_2);
                    } else {
                        float range[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                        chunk.Read(range, sizeof range);
                        light->SetRange(range[0]);
                        light->SetAttenuation(range[1], range[2], range[3]);
                    }
                    break;
                case LIGHT_MODE: {
                    chunk.Read(number);
                    uint32_t mode = static_cast<uint32_t>(number);
                    if (SceneVersion() < SCENE_VERSION_LIGHT_MODE_40)
                        mode |= 0x40;
                    if (SceneVersion() < SCENE_VERSION_LIGHT_MODE_20)
                        mode |= 0x20;
                    light->SetMode(mode);
                    break;
                }
                case LIGHT_SECTOR: {
                    // (Left by the name's read.)
                    I3D_frame *sector = m_frames[ReadRemainingString(chunk)];
                    if (IsType(sector, FRAME_SECTOR))
                        static_cast<I3D_sector *>(sector)->AddLight(light);
                    continue;
                }
                default:
                    break;
                }
                chunk.Ascend();
            }
            // (Dropped: the copy protection's fog on mission "agtlgf7".)
            light->Update();
            chunk.Ascend();
            break;
        }
        case OBJECT_BBOX:
            // (Short: the last box; its w parts not set.)
            {
                float file[6] = {box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z};
                chunk.Read(file, sizeof file);
                box.min = S_vector(file[0], file[1], file[2]);
                box.max = S_vector(file[3], file[4], file[5]);
            }
            chunk.Ascend();
            if (IsType(frame, FRAME_DUMMY))
                static_cast<I3D_dummy *>(frame)->SetBBox(box);
            break;
        case OBJECT_SOUND: {
            if (!IsType(frame, FRAME_SOUND)) {
                chunk.Ascend();
                break;
            }
            I3D_sound *const sound = static_cast<I3D_sound *>(frame);
            sound->SetLoop(false);
            sound->SetOn(false);
            bool on = false;
            while (chunk.HasMore()) {
                if (!chunk.Descend(id))
                    return false;
                switch (id) {
                case SOUND_TYPE:
                    chunk.Read(number);
                    sound->SetSoundType(number);
                    break;
                case SOUND_VOLUME:
                    chunk.Read(value);
                    sound->SetVolume(value);
                    break;
                case SOUND_OUT_VOLUME:
                    chunk.Read(value);
                    sound->SetOutVolume(value);
                    break;
                case SOUND_CONE:
                    chunk.Read(values, sizeof(float) * 2);
                    sound->SetCone(values[0], values[1]);
                    break;
                case SOUND_RANGE:
                    chunk.Read(values, sizeof(float) * 3);
                    sound->SetRange(values[0], values[1], values[2], 0.0f);
                    break;
                case SOUND_RANGE_4:
                    chunk.Read(values, sizeof(float) * 4);
                    sound->SetRange(values[0], values[1], values[2], values[3]);
                    break;
                case SOUND_CHUNK_LOOP:
                    sound->SetLoop(true);
                    break;
                case SOUND_ON:
                    on = true;
                    break;
                case SOUND_FILE: {
                    C_str file = ReadRemainingString(chunk);
                    if (!std::strchr(file, '.'))
                        file += ".wav";
                    if (g_soundCache.OpenInto(sound, file) < 0)
                        dbgPrintf(C_fstr("# ERROR LOADER : soundfile '%s' not found", file.c_str()));
                    continue;
                }
                case SOUND_SECTOR:
                case SOUND_AREA: {
                    I3D_frame *owner = m_frames[ReadRemainingString(chunk)];
                    if (id == SOUND_SECTOR && IsType(owner, FRAME_SECTOR))
                        static_cast<I3D_sector *>(owner)->AddSound(sound);
                    else if (id == SOUND_AREA && IsType(owner, FRAME_AREA))
                        static_cast<I3D_area *>(owner)->AddSound(sound);
                    continue;
                }
                case SOUND_FREQUENCY:
                    chunk.Read(value);
                    sound->SetFrequency(value);
                    break;
                default:
                    break;
                }
                chunk.Ascend();
            }
            sound->SetOn(on);
            chunk.Ascend();
            break;
        }
        case OBJECT_OCCLUDER:
            if (IsType(frame, FRAME_OCCLUDER) &&
                !LoadConvexMesh(chunk, static_cast<I3D_occluder *>(frame), OCCLUDER_VERTICES, OCCLUDER_FACES))
                return false;
            chunk.Ascend();
            break;
        case OBJECT_AREA:
            if (IsType(frame, FRAME_AREA) && !LoadConvexMesh(chunk, static_cast<I3D_area *>(frame), AREA_VERTICES, AREA_FACES))
                return false;
            chunk.Ascend();
            break;
        case OBJECT_VISUAL: {
            uint32_t newType = visualType;
            chunk.Read(newType);
            chunk.Ascend();
            if (!IsType(frame, FRAME_VISUAL) || frame->GetVisualType() == newType)
                break;
            I3D_frame *const visual = I3DGetDriver()->CreateVisual(newType);
            if (!visual)
                break;
            if (visual->Duplicate(frame) >= 0) {
                // The new visual takes the old one's place: its parent, its children, its name.
                visual->LinkTo(frame->GetParent(), 0);
                while (frame->NumChildren() != 0)
                    frame->GetChild(0)->LinkTo(visual, 0);
                visual->Update();
                frame->LinkTo(nullptr, 0);
                m_frames[C_fstr("%s", frame->GetName())] = visual;
                I3D_frame *model = visual->GetParent();
                while (model && model->GetType() != FRAME_MODEL)
                    model = model->GetParent();
                if (model) {
                    static_cast<I3D_model *>(model)->RemoveModelFrame(frame);
                    static_cast<I3D_model *>(model)->AddModelFrame(visual);
                } else {
                    scene->RemoveFrame(frame);
                    scene->AddFrame(visual);
                }
                frame = visual;
            }
            visual->Release();
            break;
        }
        case OBJECT_LIGHTMAPS:
            if (SceneVersion() >= SCENE_VERSION_LIGHTMAPS && frame && frame->GetVisualType() == I3DVISUAL_LMAP)
                static_cast<I3D_lmap_visual *>(frame)->LoadLightmapSets(chunk.IsOpen() ? chunk.GetHandle() : -1);
            chunk.Ascend();
            break;
        case OBJECT_PROJECTOR: {
            if (!frame || frame->GetVisualType() != I3DVISUAL_PROJECTOR) {
                chunk.Ascend();
                break;
            }
            I3D_projector_visual *const projector = static_cast<I3D_projector_visual *>(frame);
            uint8_t orthogonal = 0;
            while (chunk.HasMore()) {
                if (!chunk.Descend(id))
                    return false;
                switch (id) {
                case PROJECTOR_RANGE:
                    chunk.Read(value);
                    projector->SetRange(value);
                    break;
                case PROJECTOR_INTENSITY:
                    chunk.Read(value);
                    projector->SetIntensity(value);
                    break;
                case PROJECTOR_COLOR:
                    chunk.Read(&vector, sizeof vector);
                    projector->SetColor(vector);
                    break;
                case PROJECTOR_MODE:
                    chunk.Read(number);
                    projector->SetMode(number);
                    break;
                case PROJECTOR_BUFFERED:
                    chunk.Read(number);
                    projector->SetBuffered(number);
                    break;
                case PROJECTOR_ORTHOGONAL:
                    // (The original passes the byte with its stack's three above it.)
                    chunk.Read(&orthogonal, 1);
                    projector->SetOrthogonal(orthogonal);
                    break;
                case PROJECTOR_TEXTURE: {
                    const C_str file = ReadRemainingString(chunk);
                    IImage *image = nullptr;
                    if (GetIGraph()->LoadTexture(file, nullptr, 0, &image) == 0 && image) {
                        projector->SetTexture(image);
                        image->Release();
                        projector->Update();
                        projector->Reproject(0, 0);
                        projector->Update();
                    }
                    continue;
                }
                default:
                    break;
                }
                chunk.Ascend();
            }
            chunk.Ascend();
            break;
        }
        case OBJECT_UNKNOWN_B110:
            if (frame) {
                I3D_lensflare_visual *const flare = static_cast<I3D_lensflare_visual *>(frame);
                while (chunk.HasMore()) {
                    // (Quirk kept: a header that does not read keeps the last entry's id; another
                    // entry is not left.)
                    chunk.Descend(id);
                    if (id < UNKNOWN_B112 || id > UNKNOWN_B117)
                        continue;
                    chunk.Read(value);
                    chunk.Ascend();
                    switch (id) {
                    case UNKNOWN_B112: flare->SetUnknown1E4(value); break;
                    case UNKNOWN_B113: flare->SetUnknown1E0(value); break;
                    case UNKNOWN_B114: flare->SetUnknown1EC(static_cast<int>(value)); break;
                    case UNKNOWN_B115: flare->SetUnknown220(value); break;
                    case UNKNOWN_B116: flare->SetUnknown224(value); break;
                    default: flare->SetUnknown240(value); break;
                    }
                }
            }
            chunk.Ascend();
            break;
        case OBJECT_SECTOR:
            if (IsType(frame, FRAME_SECTOR)) {
                // The sound environment: twenty values, then two vectors (by their address).
                static const int PARAMS[] = {11, 0, 1, 6, 7, 4, 5, 8, 2, 3, 10, 12, 13, 14, 15, 17, 16, 18, 19, 9};
                I3D_sector *const sector = static_cast<I3D_sector *>(frame);
                for (int param : PARAMS) {
                    chunk.Read(number);
                    sector->SetEnvironmentParam(param, static_cast<uint32_t>(number));
                }
                S_vector4 environment = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int param : {21, 20}) {
                    chunk.Read(&environment, sizeof(float) * 3);
                    sector->SetEnvironmentParam(param, reinterpret_cast<uintptr_t>(&environment));
                }
            }
            chunk.Ascend();
            break;
        default:   // (OBJECT_UNKNOWN_B400 too)
            chunk.Ascend();
            break;
        }
    }

    if (!frame)
        return true;
    if (flags & OBJECT_HAS_POS)
        frame->SetPosInline(pos);
    frame->Update();
    if ((flags & OBJECT_IS_NEW) && !(flags & OBJECT_HAS_PARENT))
        scene->UpdateFrameSector(frame, frame->GetWorldPos());
    if (flags & OBJECT_HAS_PARENT) {
        if (parent)
            frame->LinkTo(parent, 0);
        else if (!frame->GetParent())
            scene->UpdateFrameSector(frame, frame->GetWorldPos());
        frame->Update();
    }
    if (flags & OBJECT_HAS_ROT)
        frame->SetRot(rot);
    if (flags & OBJECT_HAS_SCALE)
        frame->SetScale(scale);
    if (flags & OBJECT_IS_HIDDEN)
        frame->SetOn(false);
    frame->Update();
    if (frame->GetType() == FRAME_MODEL && static_cast<I3D_model *>(frame)->GetAnimation(0))
        GetGame()->AddAnimatedModel(frame);
    return true;
}
