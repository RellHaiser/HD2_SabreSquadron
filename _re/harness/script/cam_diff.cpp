// Camera parts 1 to 3: the camera module's core (game/camera_module.cpp, C_CameraModule: the original's 0x0089afb0, vtable
// 0x0080fdd0) and the vehicles' camera dummies' keeper (C_CameraFrameKeeper) against the original executable's.
// cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the module's own calls are logged
// through cam_calls.s / cam_calls.inc (gen_cmd_calls.py cam). The module's layout is the original's (the port's
// offsets are checked below), so each case gives both modules the same bytes (the pointers: each side's world frames),
// runs one operation on each and compares: the answer, the module's and its human effects' bytes, the frames, the
// keeper's bytes and every call each side made.
// Part 1: StartMission, StopMission, SaveGame, LoadSettings (the game's tables\gamecam.set, a made file, none),
// ResetUnknown2AC, GetCameraPos / GetCameraDir, SetBlendTime, SetTarget, GetViewDistance, the fields of view,
// SetViewDir, IsGameCamera, the plain getters; the keeper's constructor, Restore, Update and destructor (on a car, a
// submarine, another actor or none).
// Part 2: the command map's tactical camera (C_CameraHumanEffects): every function (the private ones called directly),
// with the faded humans' vector in each side's layout, the player team's known enemies and members, the collision
// queries hitting now and then (a plain volume or a water surface's), the scene's sector the primary one or another.
// Part 3: the update (Update on each of its paths, the car's, the scripted and the debug cameras, PlaceCamera, MoveCamera)
// with the mouse (a made IGraph), the controls' look values, the active player's vehicle and seat (the seat virtuals),
// the head under water, the aiming sway (0x00839268 / g_cameraAimSway), ray tests and dynamic volumes; the directions'
// w words the original writes from uninitialised locals are left out of its comparisons.
// MinGW; build_cam.bat; runs cam_diff.exe ("cam_diff: N checks, 0 failures"). Env: HD2_CAM_CASES (default 40,000;
// HD2_CAM_QUICK 8,000), HD2_CAM_OP (one operation's name), HD2_CAM_PART (1, 2 or 3: that part's operations only),
// HD2_CAM_SEED, HD2_CAM_SHOW, HD2_CAM_DUMP, HD2_CAM_TRACE.
#define HARNESS_NAME "cam_diff"
#define CMD_CALLS_INC "cam_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <malloc.h>

#include "common/chunk.h"
#define private public
#define protected public
#include "game/camera_module.h"
#include "game/first_person_view.h"
#include "game/submarine.h"
#undef protected
#undef private

// ---- LS3DF.dll's math the module uses (the original's calls go there too; a struct answer's address after `this`) ----
template <class T> T Ls3dfFn(const char *name) { return reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df(name))); }
S_matrix __stdcall RotateYMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateYMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix r;
    f(&r, angle);
    return r;
}
float __stdcall S_vector::AngleTo(const S_vector &v) const
{
    static const auto f = Ls3dfFn<float(__stdcall *)(const S_vector *, const S_vector &)>("?AngleTo@S_vector@@QBGMABU1@@Z");
    return f(this, v);
}
S_vector __stdcall S_vector::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_vector *, float *, const S_matrix &)>(
        "?RotateByMatrix@S_vector@@QBG?AU1@ABUS_matrix@@@Z");
    alignas(16) float r[4] = {};
    f(this, r, m);
    return S_vector(r[0], r[1], r[2]);
}
S_vector &__stdcall S_vector::operator*=(const S_matrix &m)
{
    static const auto f = Ls3dfFn<S_vector *(__stdcall *)(S_vector *, const S_matrix &)>("??XS_vector@@QAGAAU0@ABUS_matrix@@@Z");
    f(this, m);
    return *this;
}
void __stdcall S_quat::Make(const S_vector &axis, float angle)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_quat *, const S_vector &, float)>("?Make@S_quat@@QAGXABUS_vector@@M@Z");
    f(this, axis, angle);
}
void __stdcall S_quat::SetDir(const S_vector &dir, float roll)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_quat *, const S_vector &, float)>("?SetDir@S_quat@@QAGXABUS_vector@@M@Z");
    f(this, dir, roll);
}
S_vector __stdcall S_quat::GetDir() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, float *)>("?GetDir@S_quat@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
S_matrix __stdcall S_quat::RotationMatrix() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_matrix *)>("?RotationMatrix@S_quat@@QBG?AUS_matrix@@XZ");
    S_matrix r;
    f(this, &r);
    return r;
}
S_quat __stdcall S_quat::Slerp(const S_quat &q, float t, bool shortestPath) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_quat &, float, bool)>(
        "?Slerp@S_quat@@QBG?AU1@ABU1@M_N@Z");
    S_quat r;
    f(this, &r, q, t, shortestPath);
    return r;
}

// The port's side of the files (sc_shared.inc's in memory, as mod_diff's).
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }

// The debug texts: each side's dbgPrintf logs its text (the original's import thunk 0x007e52d8 redirected).
namespace H {
void LogDebugText(const char *format, va_list args)
{
    char text[1024];
    std::vsnprintf(text, sizeof text, format, args);
    if (*g_case)
        g_log[g_side].push_back(std::string("dbgPrintf ") + text);
}
void __cdecl O_dbgPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogDebugText(format, args);
    va_end(args);
}
}  // namespace H
extern "C" void dbgPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    H::LogDebugText(format, args);
    va_end(args);
}

namespace H {

// ---- the original's module ----
// (The original's 0x0089b208, the keeper's "vehicle camera speed", is the module's +0x258.)
constexpr uint32_t O_CAM = 0x0089afb0, O_CAM_CTOR = 0x00464ef0;
constexpr uint32_t O_START = 0x00465160, O_STOP = 0x00465610, O_SAVE = 0x00464e60, O_GET_POS = 0x00465a10,
                   O_GET_DIR = 0x00465b40, O_BLEND = 0x00465ad0, O_TARGET = 0x00465b20, O_VIEW_DISTANCE = 0x00465de0,
                   O_SET_NEAR_FOV = 0x00465e00, O_GET_NEAR_FOV = 0x00465e30, O_SET_FOV = 0x00465e50,
                   O_GET_FOV = 0x00465e80, O_SET_VIEW_DIR = 0x00465ea0, O_IS_GAME_CAMERA = 0x00465ee0,
                   O_GET78 = 0x00465060, O_GET_STATE = 0x00465070, O_HAS_SCRIPTED = 0x00465080, O_GET84 = 0x00465090,
                   O_GET_EFFECTS = 0x004650a0, O_GET_CAMERA = 0x004650b0, O_GET_SECTOR = 0x004650c0,
                   O_LOAD_SETTINGS = 0x004656d0, O_RESET_2AC = 0x00465a00, O_GET_MODULE = 0x00464e90;
constexpr uint32_t O_KEEPER_CTOR = 0x00465f00, O_KEEPER_DTOR = 0x00466000, O_KEEPER_RESTORE = 0x00466010,
                   O_KEEPER_UPDATE = 0x00466110;
// The human effects (thiscall on the module's +0x0C).
constexpr uint32_t O_FX_ZOOM = 0x00462750, O_FX_IS_TACTICAL = 0x00462770, O_FX_MOTION = 0x004633a0,
                   O_FX_START_FLIGHT = 0x00462350, O_FX_LOOK_POINT = 0x004630e0, O_FX_LOOK_AT = 0x00462780,
                   O_FX_SET_TACTICAL = 0x00462ae0, O_FX_REMOVE = 0x00463040, O_FX_SET_MODE = 0x004632c0,
                   O_FX_RESET_HUMANS = 0x00463620, O_FX_TRANSPARENCY = 0x00463690, O_FX_UPDATE_HUMAN = 0x00463780,
                   O_FX_UPDATE_HUMANS = 0x00463890, O_FX_UPDATE_TACTICAL = 0x00463ba0, O_FX_MOVE = 0x00464b80;
// Part 3: the update (thiscall on the module, the time step).
constexpr uint32_t O_UPDATE = 0x00469300, O_UPDATE_CAR = 0x0046ada0, O_UPDATE_SCRIPTED = 0x0046b550,
                   O_UPDATE_DEBUG = 0x0046beb0, O_PLACE = 0x00468130, O_MOVE_CAMERA = 0x00466970,
                   O_AIM_SWAY = 0x00839268;
uint8_t *OCam() { return OriginalAt<uint8_t *>(O_CAM); }
uint8_t *PCam() { return reinterpret_cast<uint8_t *>(&g_cameraModule); }
uint8_t *Cam() { return g_side ? OCam() : PCam(); }
uint8_t *Effects() { return *reinterpret_cast<uint8_t **>(Cam() + 0x0C); }

// The port's layout is the original's (the headers' offsets).
#define CAM_AT(m, o) static_assert(offsetof(C_CameraModule, m) == o, "C_CameraModule::" #m);
CAM_AT(m_humanEffects, 0x0C) CAM_AT(m_wantedPos, 0x10) CAM_AT(m_wantedDir, 0x20) CAM_AT(m_pos, 0x30) CAM_AT(m_dir, 0x40)
CAM_AT(m_unknown050, 0x50) CAM_AT(m_waterOffset, 0x54) CAM_AT(m_unknown058, 0x58) CAM_AT(m_unknown05C, 0x5C)
CAM_AT(m_shakePos, 0x60) CAM_AT(m_shakeDir, 0x70) CAM_AT(m_offsetDir, 0x80) CAM_AT(m_offsetPos, 0x90)
CAM_AT(m_unknown0A0, 0xA0) CAM_AT(m_unknown0D0, 0xD0) CAM_AT(m_unknown0D4, 0xD4) CAM_AT(m_settings, 0xE0)
CAM_AT(m_gameCamera, 0x240) CAM_AT(m_sceneCamera, 0x244) CAM_AT(m_camera, 0x248) CAM_AT(m_listenerSector, 0x24C)
CAM_AT(m_listenerTime, 0x250) CAM_AT(m_distance, 0x254) CAM_AT(m_turnSpeed, 0x258) CAM_AT(m_hit, 0x25C)
CAM_AT(m_view, 0x260) CAM_AT(m_viewSettings, 0x264) CAM_AT(m_debugSpeed, 0x268) CAM_AT(m_cut, 0x26C)
CAM_AT(m_debugCamera, 0x26D) CAM_AT(m_spectating, 0x26E) CAM_AT(m_mouseLook, 0x26F) CAM_AT(m_underwater, 0x270)
CAM_AT(m_spectator, 0x274) CAM_AT(m_unknown278, 0x278) CAM_AT(m_waterLevel, 0x27C) CAM_AT(m_blendTime, 0x280)
CAM_AT(m_shakeTime, 0x284) CAM_AT(m_target, 0x288) CAM_AT(m_targetSpeed, 0x28C) CAM_AT(m_aiming, 0x290)
CAM_AT(m_aimTime, 0x294) CAM_AT(m_time, 0x298) CAM_AT(m_offsetPosDelay, 0x29C) CAM_AT(m_offsetDirDelay, 0x2A0)
CAM_AT(m_fadeTarget, 0x2A4) CAM_AT(m_fade, 0x2A8) CAM_AT(m_unknown2AC, 0x2AC) CAM_AT(m_scriptedViewOn, 0x2B0)
CAM_AT(m_scriptedView, 0x2B4) CAM_AT(m_carPitch, 0x2B8) CAM_AT(m_carYaw, 0x2BC)
#undef CAM_AT
#define FX_AT(m, o) static_assert(offsetof(C_CameraHumanEffects, m) == o, "C_CameraHumanEffects::" #m);
FX_AT(m_target, 0x00) FX_AT(m_lookPoint, 0x10) FX_AT(m_pos, 0x20) FX_AT(m_lookDistance, 0x30) FX_AT(m_motionX, 0x34)
FX_AT(m_motionY, 0x38) FX_AT(m_height, 0x3C) FX_AT(m_zoom, 0x40) FX_AT(m_clickTime, 0x44) FX_AT(m_tactical, 0x48)
FX_AT(m_keepSpeed, 0x49) FX_AT(m_mode, 0x4C) FX_AT(m_flightFrom, 0x50) FX_AT(m_flightTo, 0x60) FX_AT(m_flightSpeed, 0x70)
FX_AT(m_flightAccel, 0x74) FX_AT(m_flightDistance, 0x78) FX_AT(m_flightTime, 0x7C) FX_AT(m_camera, 0x80)
FX_AT(m_gameCamera, 0x84) FX_AT(m_humans, 0x8C)
#undef FX_AT
#define KEEPER_AT(m, o) static_assert(offsetof(C_CameraFrameKeeper, m) == o, "C_CameraFrameKeeper::" #m);
KEEPER_AT(m_rot, 0x00) KEEPER_AT(m_localPos, 0x10) KEEPER_AT(m_parentPos, 0x20) KEEPER_AT(m_worldPos, 0x30)
KEEPER_AT(m_frame, 0x40) KEEPER_AT(m_owner, 0x44) KEEPER_AT(m_cut, 0x48)
#undef KEEPER_AT

// The module's words that hold pointers (the frames: each side's own), the floats (printed as such) and the human
// effects' (its camera +0x80, the game camera +0x84; its vector from +0x88 is each side's own).
const uint32_t CAM_POINTERS[] = {0x240, 0x244, 0x248, 0x24C, 0x25C, 0x288, 0x2B4};
const uint32_t FX_POINTERS[] = {0x80, 0x84};
bool IsCamFloat(uint32_t o)
{
    if (o >= 0x10 && o < 0xA0)
        return true;
    if (o >= 0xE0 && o < 0x240)
        return (o - 0xE0) % 0x20 != 0x10;               // (the settings' flags)
    static const uint32_t floats[] = {0x254, 0x258, 0x27C, 0x28C, 0x2A4, 0x2A8, 0x2AC, 0x2B8, 0x2BC};
    return std::find(std::begin(floats), std::end(floats), o) != std::end(floats);
}
bool IsFxFloat(uint32_t o) { return o < 0x44 || (o >= 0x50 && o < 0x80); }

// ---- one case ----
enum E_Op {
    OP_START, OP_STOP, OP_SAVE, OP_LOAD_SETTINGS, OP_RESET_2AC, OP_GET_POS, OP_GET_DIR, OP_BLEND, OP_TARGET,
    OP_VIEW_DISTANCE, OP_SET_NEAR_FOV, OP_GET_NEAR_FOV, OP_SET_FOV, OP_GET_FOV, OP_SET_VIEW_DIR, OP_IS_GAME_CAMERA,
    OP_GETTERS, OP_KEEPER,
    // Part 2: the human effects.
    OP_FX_ZOOM, OP_FX_IS_TACTICAL, OP_FX_MOTION, OP_FX_START_FLIGHT, OP_FX_LOOK_POINT, OP_FX_LOOK_AT, OP_FX_SET_TACTICAL,
    OP_FX_REMOVE, OP_FX_SET_MODE, OP_FX_RESET_HUMANS, OP_FX_TRANSPARENCY, OP_FX_UPDATE_HUMAN, OP_FX_UPDATE_HUMANS,
    OP_FX_UPDATE_TACTICAL, OP_FX_MOVE,
    // Part 3: the update.
    OP_UPDATE, OP_UPDATE_CAR, OP_UPDATE_SCRIPTED, OP_UPDATE_DEBUG, OP_PLACE, OP_MOVE_CAMERA, OP_COUNT
};
const char *const OP_NAMES[OP_COUNT] = {"StartMission", "StopMission", "SaveGame", "LoadSettings", "ResetUnknown2AC",
                                        "GetCameraPos", "GetCameraDir", "SetBlendTime", "SetTarget", "GetViewDistance",
                                        "SetNearSectorFov", "GetNearSectorFov", "SetFov", "GetFov", "SetViewDir",
                                        "IsGameCamera", "Getters", "Keeper",
                                        "SetTacticalZoom", "IsTactical", "SetTacticalMotion", "StartFlight",
                                        "UpdateLookPoint", "LookAt", "SetTactical", "Remove", "SetTacticalMode",
                                        "ResetHumans", "GetTransparency", "UpdateHuman", "UpdateHumans", "UpdateTactical",
                                        "Move",
                                        "Update", "UpdateCarCamera", "UpdateScriptedView", "UpdateDebugCamera",
                                        "PlaceCamera", "MoveCamera"};
const int OP_WEIGHTS[OP_COUNT] = {12, 12, 2, 12, 2, 8, 8, 8, 5, 3, 5, 4, 5, 4, 8, 4, 4, 30,
                                  2, 2, 2, 10, 6, 12, 14, 6, 14, 5, 8, 12, 10, 40, 12,
                                  60, 25, 15, 15, 35, 35};
bool IsFxOp(int op) { return op >= OP_FX_ZOOM && op < OP_UPDATE; }
int OpPart(int op) { return op >= OP_UPDATE ? 3 : IsFxOp(op) ? 2 : 1; }

// A frame of the world by index (-1: null; -2: a camera the case made, CreateFrame's).
struct S_CamCase {
    int op = 0;
    uint8_t image[0x2C0] = {};                          // the module's bytes from +0x10
    int ptrs[7] = {};                                   // CAM_POINTERS' frames (+0x25C: 1 a volume)
    uint8_t fx[0x88] = {};                              // the human effects' bytes
    int fxPtrs[2] = {};
    // The game: its cutscene FOV, clips, time, the scene's active camera.
    float fov = 0, nearClip = 0, farClip = 0;
    uint32_t time = 0;
    int activeCamera = 4;
    bool paused = false, inputEnabled = true, fpvShown = false, binocularsShown = false;
    int player = -1;                                    // the active player: one of the world's humans (-1 none)
    bool ownedCamera = false;                           // the camera dummy (+0x248) in a human's model (model1)
    bool keeperUpright = false;                         // the keeper's dummy looking straight up or down
    bool keeperAligned = false;                         // ... 0.3 up (its view's y exactly 0.3), its owner along its view
    bool keeperYawed = false;                           // (that view (0.1, 0.3, z), else (0, 0.3, z))
    bool playerRides = false;                           // the active player in the keeper's vehicle
    int groundHit = 0;                                  // CollisionSphereTest: 0 none, else a hit (its y)
    // The settings file: 0 the game's, 1 made, 2 none.
    int fileKind = 0;
    std::string file;
    // Arguments.
    bool flag = false;
    int number = 0;
    float value = 0;
    int frame = -1;
    S_vector4 vector = {};
    // The keeper: its frame and owner (an actor's index; -1 none; -2 the submarine), its steps (0 Update, 1 Restore),
    // the car's or the submarine's fields.
    int keeperFrame = -1, keeperOwner = -1;
    std::vector<int> steps;
    bool deleteKeeper = true;
    S_vector4 direction = {}, velocity = {};
    float speed = 0, bob = 0;
    // Part 2: the faded humans (a world human's index, its transparency), the player team's known enemies (humans'
    // indices), the human argument (-1 null), its vehicle's kind (-2 the world's; -1 none; else a made actor of that
    // type), the mode's point (none: null), the motion, the humans' view direction (GetViewDir), the collision queries
    // answered (part 2's operations).
    std::vector<std::pair<int, float>> faded;
    std::vector<int> enemies;
    int human = -1, vehicleType = -2;
    bool hasPoint = false;
    float motionX = 0, motionY = 0;
    S_vector viewDir = {0, 0, 1};
    // Part 3: the mouse (IGraph's), the inverted mouse, the controls' look and debug speed values (the speed: the first
    // and the second call's), the camera dummy's roll (PoseGetRoll), the selected member (a human's index, -1 none),
    // the player's vehicle (-1 none; else a made actor of that type) and seat, its flags, first person only, the
    // player's head under water and the surface, the aiming sway.
    int mouseX = 0, mouseY = 0;
    uint32_t buttons = 0;
    bool invertMouse = false;
    float lookX = 0, lookY = 0, speed1 = 0, speed2 = 0, roll = 0, surface = 0;
    int selected = -1, playerVehicle = -1, seat = 0;
    uint32_t seatFlags = 0;
    bool fpvOnly = false, headUnder = false;
    float aimSway = 0.005f;
    bool waterNear = false;                             // the water level the camera dummy's height less waterDelta
    float waterDelta = 0;
};
int g_camCases, g_opCases[OP_COUNT], g_onlyOp = -1, g_onlyPart;
const S_CamCase *g_cur;                                 // (part 3's answers)
int g_speedCalls;                                       // (the debug speed action's calls this side)
uint8_t *g_igraph[2], *g_dynamic[2];                    // (each side's IGraph; a dynamic volume, its frame the case's)
uint8_t *g_player[2];                                   // the case's active player on each side
int g_groundHit;                                        // CollisionSphereTest's answer (the keeper's ground)
bool g_fxCollisions;                                    // part 2: the collision queries hit now and then
S_vector4 g_viewDir;                                    // C_Human::GetViewDir's answer (read as an S_vector4)
uint8_t *HumanAt(int index)
{
    if (index < 0 || W().humans.empty())
        return nullptr;
    return W().actors[size_t(W().humans[size_t(index) % W().humans.size()])].bytes;
}

float CF()
{
    switch (I(0, 9)) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2: return -1.0f;
    case 3: return float(I(-8, 8)) * 0.125f;
    case 4: return float(I(-1000, 1000)) * 0.01f;
    case 5: return float(I(-40, 40)) * 0.5f;
    case 6: return I(0, 3) ? float(I(-100000, 100000)) * 0.0001f : float(I(-3, 3)) * (I(0, 1) ? 1e-5f : 1e-4f);
    default: return float(I(-100000, 100000)) * 0.0001f;
    }
}
void PutF(uint8_t *p, uint32_t o, float f) { std::memcpy(p + o, &f, 4); }
void PutI(uint8_t *p, uint32_t o, int v) { std::memcpy(p + o, &v, 4); }
template <class T> T At(const uint8_t *p, uint32_t o)
{
    T v;
    std::memcpy(&v, p + o, sizeof v);
    return v;
}

// The frames the cases point at: the cameras (cam1 4, cam2 13), the dummies and joints (with parents), the sector.
int AnyFrame() { return I(0, WORLD_FRAMES - 1); }
int CameraFrame() { return I(0, 1) ? 4 : 13; }

uint8_t *FramePtr(int index)
{
    if (index < 0)
        return nullptr;
    return W().frames[size_t(index)].bytes;
}

// ---- the settings file ----
std::string Word(uint32_t v, int n = 4) { return std::string(reinterpret_cast<const char *>(&v), size_t(n)); }
std::string Chunk(uint16_t id, const std::string &data) { return Word(id, 2) + Word(uint32_t(data.size() + 6)) + data; }
std::string MakeSettingsFile()
{
    const uint32_t version = I(0, 4) ? 2u : uint32_t(I(0, 3));
    std::string records;
    const int size = I(0, 3) ? 11 * 0x20 : I(0, 3) ? 0x20 * I(0, 12) : I(0, 0x180);
    for (int i = 0; i < size; i += 4) {
        float f = std::fabs(CF());
        if (i % 0x20 == 0x10)
            records += Word(uint32_t(I(0, 1) ? 0x8B : I(0, 0xFF)));
        else
            records += Word(*reinterpret_cast<uint32_t *>(&f));
    }
    records.resize(size_t(size));
    std::string file;
    if (I(0, 7))
        file += Chunk(0, I(0, 7) ? Word(version) + std::string(size_t(I(0, 3) ? 0 : I(1, 8)), char(I(0, 255)))
                                 : Word(version, I(0, 3)));
    if (I(0, 7))
        file += Chunk(0, records);
    if (!I(0, 7))
        file += std::string(size_t(I(1, 5)), char(I(0, 255)));
    return file;
}

// ---- each side's module and keeper ----
void SetCamState(const S_CamCase &c)
{
    uint8_t *cam = Cam();
    std::memcpy(cam + 0x10, c.image + 0x10, 0x2C0 - 0x10);
    for (int i = 0; i < 7; ++i) {
        uint8_t *p = FramePtr(c.ptrs[i]);
        if (CAM_POINTERS[i] == 0x25C)
            p = c.ptrs[i] ? Block(0x40) : nullptr;
        if (CAM_POINTERS[i] == 0x25C && p)
            Name(p, "volume");
        std::memcpy(cam + CAM_POINTERS[i], &p, 4);
    }
    // The view's settings: the index's.
    uint8_t *settings = cam + 0xE0 + 0x20 * At<int>(c.image, 0x260);
    std::memcpy(cam + 0x264, &settings, 4);
    uint8_t *fx = Effects();
    std::memcpy(fx, c.fx, 0x80);
    for (int i = 0; i < 2; ++i) {
        uint8_t *p = FramePtr(c.fxPtrs[i]);
        std::memcpy(fx + FX_POINTERS[i], &p, 4);
    }
    // The camera dummy in a human's model: its owner model model1, a human's frame.
    if (c.ownedCamera && c.ptrs[2] >= 0 && !W().humans.empty()) {
        uint8_t *model = W().frames[2].bytes;
        AsFrame(FramePtr(c.ptrs[2]))->m_ownerModel = AsFrame(model);
        if (!AsFrame(model)->m_data)
            AsFrame(model)->m_data = reinterpret_cast<S_FrameData *>(Block(0x20));
        AsFrame(model)->m_data->actor = reinterpret_cast<C_Actor *>(W().actors[size_t(W().humans[0])].bytes);
    }
    // The active player (the original asks for it again where the port keeps it: answered unlogged).
    g_player[g_side] = c.player >= 0 && !W().humans.empty()
                           ? W().actors[size_t(W().humans[size_t(c.player) % W().humans.size()])].bytes
                           : nullptr;
    // The game (the original's offsets; the port's members) and its first-person view (shown +0x58, the binoculars
    // +0x59).
    uint8_t *fpv = Block(std::max(sizeof(C_FirstPersonView), size_t(0x100)) + 0x40);
    Name(fpv, "first-person view");
    if (g_side) {
        SetGameField(0xBC, *reinterpret_cast<const uint32_t *>(&c.fov));
        SetGameField(0xC0, *reinterpret_cast<const uint32_t *>(&c.nearClip));
        SetGameField(0xC4, *reinterpret_cast<const uint32_t *>(&c.farClip));
        SetGameField(0x2C, c.time);
        SetGameField(0x5C, reinterpret_cast<uint32_t>(fpv));
        g_origGame[0x64] = c.paused;
        g_origGame[0x6E] = c.inputEnabled;
        fpv[0x58] = c.fpvShown;
        fpv[0x59] = c.binocularsShown;
    } else {
        C_game *g = reinterpret_cast<C_game *>(g_portGameBuf);
        g->m_fov = c.fov;
        g->m_nearClip = c.nearClip;
        g->m_farClip = c.farClip;
        g->m_time = c.time;
        g->m_firstPersonView = reinterpret_cast<C_FirstPersonView *>(fpv);
        g->m_paused = c.paused;
        g->m_playerInputEnabled = c.inputEnabled;
        reinterpret_cast<C_FirstPersonView *>(fpv)->m_on = c.fpvShown;
        reinterpret_cast<C_FirstPersonView *>(fpv)->m_binoculars = c.binocularsShown;
    }
    *reinterpret_cast<uint8_t **>(W().scene + 0x15C) = FramePtr(c.activeCamera);
    // The faded humans (+0x8C: the original's MSVC 6 vector first +0x8C, last +0x90, end +0x94, room enough never to
    // grow; the port's std::vector).
    const size_t room = 64;
    if (g_side) {
        uint8_t *buf = Block(8 * room);
        uint8_t *p = buf;
        for (const auto &e : c.faded) {
            uint8_t *h = HumanAt(e.first);
            std::memcpy(p, &h, 4);
            std::memcpy(p + 4, &e.second, 4);
            p += 8;
        }
        uint8_t *end = buf + 8 * room;
        std::memcpy(fx + 0x8C, &buf, 4);
        std::memcpy(fx + 0x90, &p, 4);
        std::memcpy(fx + 0x94, &end, 4);
    } else {
        auto &v = reinterpret_cast<C_CameraHumanEffects *>(fx)->m_humans;
        v.clear();
        v.reserve(room);
        for (const auto &e : c.faded)
            v.push_back({reinterpret_cast<C_Human *>(HumanAt(e.first)), e.second});
    }
    // The player team's known enemies (the original's records vector first +0x3C, last +0x40; the port's at +0x38).
    std::vector<uint8_t *> records;
    for (int e : c.enemies) {
        uint8_t *h = HumanAt(e);
        if (!h)
            continue;                                   // (a world without humans)
        uint8_t *r = Block(0x10);
        std::memcpy(r, &h, 4);
        records.push_back(r);
    }
    if (g_side) {
        uint8_t *team = W().playerTeam;
        uint8_t *buf = Block(4 * (records.size() + 1));
        if (!records.empty())
            std::memcpy(buf, records.data(), 4 * records.size());
        uint8_t *last = buf + 4 * records.size();
        std::memcpy(team + 0x3C, &buf, 4);
        std::memcpy(team + 0x40, &last, 4);
        std::memcpy(team + 0x44, &last, 4);
    } else {
        auto &v = g_teamManager.GetPlayerTeam()->m_enemyRecords.m_records;
        v.clear();
        for (uint8_t *r : records)
            v.push_back(reinterpret_cast<S_EnemyRecord *>(r));
    }
    g_viewDir = {c.viewDir.x, c.viewDir.y, c.viewDir.z, 0.5f};
    g_fxCollisions = OpPart(c.op) >= 2;
    // Part 3: the screen's mouse, the player's vehicle and seat, the aiming sway; a dynamic volume (its frame the camera
    // dummy or another: PlaceCamera drops hits on the dummy's own model).
    g_cur = &c;
    g_speedCalls = 0;
    g_igraph[g_side] = Block(0x200);
    Name(g_igraph[g_side], "igraph");
    *reinterpret_cast<void ***>(g_igraph[g_side]) = EngineVtable("IGraph");
    g_dynamic[g_side] = Block(0x60);
    Name(g_dynamic[g_side], "dynamic volume");
    reinterpret_cast<S_CollisionVolume *>(g_dynamic[g_side])->flags = COLLISION_FLAG_DYNAMIC;
    reinterpret_cast<S_CollisionVolume *>(g_dynamic[g_side])->frame =
        AsFrame(c.flag ? At<uint8_t *>(cam, 0x248) : FramePtr(c.frame));
    if (OpPart(c.op) == 3) {
        if (uint8_t *h = g_player[g_side]) {
            uint8_t *vehicle = nullptr;
            if (c.playerVehicle >= 0 && !W().actors.empty()) {
                vehicle = Block(0x1000);
                Name(vehicle, F("vehicle %d", c.playerVehicle));
                *reinterpret_cast<void **>(vehicle) = *reinterpret_cast<void **>(W().actors[0].bytes);
            }
            if (g_side) {
                if (vehicle)
                    *reinterpret_cast<int *>(vehicle + 0x18) = c.playerVehicle;
                *reinterpret_cast<uint8_t **>(h + 0x160) = vehicle;
                PutI(h, 0x164, c.seat);
            } else {
                if (vehicle)
                    reinterpret_cast<C_Actor *>(vehicle)->m_type = E_ActorType(c.playerVehicle);
                reinterpret_cast<C_Human *>(h)->m_vehicle = reinterpret_cast<C_Actor *>(vehicle);
                reinterpret_cast<C_Human *>(h)->m_seat = c.seat;
            }
        }
    }
    if (c.waterNear) {
        if (uint8_t *dummy = At<uint8_t *>(cam, 0x248))
            PutF(cam, 0x27C, AsFrame(dummy)->GetMatrix().m[3][1] - c.waterDelta);
    }
    if (g_side)
        *OriginalAt<float *>(O_AIM_SWAY) = c.aimSway;
    else
        g_cameraAimSway = c.aimSway;
}

std::string WordText(const uint8_t *base, uint32_t o, bool isFloat, bool isPointer)
{
    const uint32_t w = At<uint32_t>(base, o);
    if (isPointer)
        return NameOf(w);
    if (isFloat)
        return FloatText(At<float>(base, o));
    return F("%08x", w);
}
std::vector<std::string> CamState()
{
    std::vector<std::string> out;
    const uint8_t *cam = Cam();
    out.push_back(F("module %d ", At<int>(cam, 8)) + reinterpret_cast<const C_str *>(cam + 4)->c_str());
    for (uint32_t o = 0x10; o < 0x2C0; o += 0x10) {
        std::string line = F("module +%03x", o);
        for (uint32_t w = o; w < o + 0x10; w += 4) {
            if (w == 0x264) {
                const uint32_t p = At<uint32_t>(cam, w);
                const uint32_t first = reinterpret_cast<uint32_t>(cam + 0xE0);
                line += p >= first && (p - first) % 0x20 == 0 && p - first < 0x20 * 11 ? F(" settings[%d]", int((p - first) / 0x20))
                                                                                         : F(" %08x?", p);
                continue;
            }
            // (Part 3: the directions' and places' w the original writes from uninitialised locals (RotateByMatrix's,
            // the slerped look's, the shake's): not read.)
            if (g_cur && OpPart(g_cur->op) == 3 && (w == 0x2C || w == 0x3C || w == 0x4C || w == 0x6C || w == 0x7C)) {
                line += " -";
                continue;
            }
            line += " " + WordText(cam, w, IsCamFloat(w),
                                   std::find(std::begin(CAM_POINTERS), std::end(CAM_POINTERS), w) != std::end(CAM_POINTERS));
        }
        out.push_back(line);
    }
    const uint8_t *fx = Effects();
    for (uint32_t o = 0; o < 0x88; o += 0x10) {
        std::string line = F("effects +%02x", o);
        for (uint32_t w = o; w < o + 0x10 && w < 0x88; w += 4)
            // (The point's and the position's w: the original's uninitialised locals; not read.)
            line += w == 0x0C || w == 0x2C ? std::string(" -") : " " + WordText(fx, w, IsFxFloat(w),
                                   std::find(std::begin(FX_POINTERS), std::end(FX_POINTERS), w) != std::end(FX_POINTERS));
        out.push_back(line);
    }
    // The faded humans.
    const uint32_t first = At<uint32_t>(fx, 0x8C), last = At<uint32_t>(fx, 0x90);
    std::string humans = F("effects humans %d:", int((last - first) / 8));
    for (uint32_t p = first; p < last; p += 8)
        humans += " " + NameOf(*reinterpret_cast<const uint32_t *>(p)) + " " +
                  FloatText(*reinterpret_cast<const float *>(p + 4));
    out.push_back(humans);
    out.push_back("aim sway " + FloatText(g_side ? *OriginalAt<float *>(O_AIM_SWAY) : g_cameraAimSway));
    return out;
}

// The keeper's owner: a world actor, the case's submarine (built here: type 0x14, its direction, bob, speed) or none.
uint8_t *g_submarine[2];
uint8_t *KeeperOwner(const S_CamCase &c)
{
    if (c.keeperOwner == -1)
        return nullptr;
    uint8_t *a;
    if (c.keeperOwner == -2) {
        a = Block(std::max(sizeof(C_Submarine), size_t(0x300)) + 0x40);
        Name(a, "submarine");
        g_submarine[g_side] = a;
        if (g_side)
            *reinterpret_cast<int *>(a + 0x18) = ACTOR_SUBMARINE;
        else
            reinterpret_cast<C_Actor *>(a)->m_type = ACTOR_SUBMARINE;
        if (g_side) {
            PutF(a, 0xF0, c.bob);
            PutF(a, 0xF4, c.speed);
        } else {
            reinterpret_cast<C_Submarine *>(a)->m_bob = c.bob;
            reinterpret_cast<C_Submarine *>(a)->m_speed = c.speed;
        }
    } else {
        a = W().actors[size_t(c.keeperOwner) % W().actors.size()].bytes;
    }
    if (g_side)
        std::memcpy(a + 0x40, &c.direction, 16);
    else
        reinterpret_cast<C_Actor *>(a)->m_direction = c.direction;
    const int type = g_side ? At<int>(a, 0x18) : int(reinterpret_cast<C_Actor *>(a)->m_type);
    if (type == ACTOR_CAR) {
        if (g_side) {
            std::memcpy(a + 0x4A0, &c.velocity, 16);
            PutF(a, 0x958, c.speed);
        } else {
            C_Car *car = reinterpret_cast<C_Car *>(a);
            std::memcpy(&car->m_vehicle.m_velocity, &c.velocity, 16);
            car->m_vehicle.m_speed = c.speed;
        }
    }
    return a;
}
std::string KeeperText(const uint8_t *k)
{
    std::string s = "keeper";
    for (uint32_t o = 0; o < 0x40; o += 4)
        s += " " + FloatText(At<float>(k, o));
    return s + " " + NameOf(At<uint32_t>(k, 0x40)) + " " + NameOf(At<uint32_t>(k, 0x44)) + F(" cut %d", k[0x48]);
}

// ---- the original's calls ----
template <class R, class... A> R OCall(uint32_t addr, A... a)
{
    using T = R(TC *)(void *, A...);
    return OriginalAt<T>(addr)(OCam(), a...);
}
std::string BoolText(bool b) { return b ? "true" : "false"; }

std::string RunKeeper(const S_CamCase &c)
{
    uint8_t *frame = FramePtr(c.keeperFrame);
    uint8_t *owner = KeeperOwner(c);
    if (frame && c.keeperUpright) {
        // (Its parent unturned, the dummy turned a quarter about x: its view straight down or up.)
        I3D_frame *f = AsFrame(frame), *parent = f->m_parent;
        const S_vector parentPos = parent->m_localMatrix(3), pos = f->m_localMatrix(3);
        parent->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        parent->m_localMatrix.SetRot3(parent->m_rot);
        parent->m_localMatrix(3) = parentPos;
        // (The matrix exact: its view's x and z 0.)
        const float up = c.speed < 0.0f ? -1.0f : 1.0f;
        f->m_rot = S_quat(0.70710677f, up * -0.70710677f, 0.0f, 0.0f);
        f->m_localMatrix.Identity();
        f->m_localMatrix.m[1][1] = 0.0f;
        f->m_localMatrix.m[1][2] = -up;
        f->m_localMatrix.m[2][1] = up;
        f->m_localMatrix.m[2][2] = 0.0f;
        f->m_localMatrix(3) = pos;
        parent->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
    }
    if (frame && c.keeperAligned) {
        // (The dummy's ancestors unturned; its view (0, 0.3, z), z making it exactly unit: not normalized.)
        I3D_frame *f = AsFrame(frame);
        for (I3D_frame *p = f->m_parent; p; p = p->m_parent) {
            const S_vector pos = p->m_localMatrix(3);
            p->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
            p->m_localMatrix.SetRot3(p->m_rot);
            p->m_localMatrix(3) = pos;
            p->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        }
        const S_vector pos = f->m_localMatrix(3);
        // ((0.1, 0.3, 0.9486833) is as exactly unit in every order of its sum.)
        f->m_localMatrix.Identity();
        f->m_localMatrix.m[2][0] = c.keeperYawed ? 0.1f : 0.0f;
        f->m_localMatrix.m[2][1] = 0.3f;
        f->m_localMatrix.m[2][2] = c.keeperYawed ? 0.9486833f : 0.9539392f;
        f->m_localMatrix(3) = pos;
        f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
    }
    if (c.playerRides && owner && g_player[g_side]) {
        if (g_side)
            *reinterpret_cast<uint8_t **>(g_player[g_side] + 0x160) = owner;
        else
            reinterpret_cast<C_Human *>(g_player[g_side])->m_vehicle = reinterpret_cast<C_Actor *>(owner);
    }
    g_groundHit = c.groundHit;
    std::string out;
    uint8_t *k;
    if (g_side) {
        k = static_cast<uint8_t *>(_aligned_malloc(0x50, 16));
        std::memset(k, 0xCD, 0x50);
        using T_Ctor = void *(TC *)(void *, void *, void *);
        OriginalAt<T_Ctor>(O_KEEPER_CTOR)(k, owner, frame);
    } else {
        C_CameraFrameKeeper *p = static_cast<C_CameraFrameKeeper *>(_aligned_malloc(0x50, 16));
        std::memset(p, 0xCD, 0x50);
        ::new (p) C_CameraFrameKeeper(reinterpret_cast<C_Actor *>(owner), AsFrame(frame));
        k = reinterpret_cast<uint8_t *>(p);
    }
    out += KeeperText(k) + ";";
    for (int step : c.steps) {
        if (step == 2) {
            if (frame) {
                I3D_frame *parent = AsFrame(frame)->m_parent;
                parent->m_localMatrix.m[3][0] += 1.0f;
                parent->m_localMatrix.m[3][1] += 2.0f;
                parent->m_localMatrix.m[3][2] -= 0.5f;
                parent->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
                AsFrame(frame)->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            }
            out += " parent moved;";
            continue;
        }
        bool r;
        if (g_side) {
            using T_Update = bool(TC *)(void *, int);
            using T_Restore = bool(TC *)(void *);
            r = step ? OriginalAt<T_Restore>(O_KEEPER_RESTORE)(k) : OriginalAt<T_Update>(O_KEEPER_UPDATE)(k, I(0, 100));
        } else {
            C_CameraFrameKeeper *p = reinterpret_cast<C_CameraFrameKeeper *>(k);
            r = step ? p->Restore() : p->Update(0);
        }
        out += F(" %s %s;", step ? "Restore" : "Update", BoolText(r).c_str()) + KeeperText(k);
    }
    if (c.deleteKeeper) {
        if (g_side) {
            using T_Dtor = void(TC *)(void *);
            OriginalAt<T_Dtor>(O_KEEPER_DTOR)(k);
        } else {
            reinterpret_cast<C_CameraFrameKeeper *>(k)->~C_CameraFrameKeeper();
        }
        out += " deleted;" + KeeperText(k);
    }
    _aligned_free(k);
    return out;
}

// ---- part 2: the human effects ----
template <class R, class... A> R FCall(uint32_t addr, A... a)
{
    using T = R(TC *)(void *, A...);
    return OriginalAt<T>(addr)(Effects(), a...);
}
// The human argument's vehicle: the world's, none or a made actor of the case's type.
void SetHumanVehicle(uint8_t *h, int type)
{
    if (!h || type == -2)
        return;
    uint8_t *vehicle = nullptr;
    if (type >= 0) {
        vehicle = Block(0x1000);
        Name(vehicle, F("vehicle %d", type));
        if (g_side)
            *reinterpret_cast<int *>(vehicle + 0x18) = type;
        else
            reinterpret_cast<C_Actor *>(vehicle)->m_type = E_ActorType(type);
    }
    if (g_side)
        *reinterpret_cast<uint8_t **>(h + 0x160) = vehicle;
    else
        reinterpret_cast<C_Human *>(h)->m_vehicle = reinterpret_cast<C_Actor *>(vehicle);
}
std::string RunFxOp(const S_CamCase &c)
{
    const bool o = g_side != 0;
    C_CameraHumanEffects &e = *reinterpret_cast<C_CameraHumanEffects *>(Effects());
    uint8_t *h = HumanAt(c.human);
    void *hv = h;
    C_Human *hp = reinterpret_cast<C_Human *>(h);
    // (Both sides read these unchecked: a world without humans, a null game camera.)
    if ((c.op == OP_FX_TRANSPARENCY || c.op == OP_FX_UPDATE_HUMAN) && !h)
        return "no human";
    if (c.op == OP_FX_SET_TACTICAL && c.flag && !Effects()[0x48] && !g_player[g_side])
        return "no player";
    if (c.op == OP_FX_LOOK_AT && !h && !At<uint32_t>(Effects(), 0x84))
        return "no game camera";
    switch (c.op) {
    case OP_FX_ZOOM:
        if (o)
            FCall<void>(O_FX_ZOOM, c.value);
        else
            e.SetTacticalZoom(c.value);
        return "";
    case OP_FX_IS_TACTICAL:
        return BoolText(o ? FCall<bool>(O_FX_IS_TACTICAL) : e.IsTactical());
    case OP_FX_MOTION:
        if (o)
            FCall<void>(O_FX_MOTION, c.motionX, c.motionY);
        else
            e.SetTacticalMotion(c.motionX, c.motionY);
        return "";
    case OP_FX_START_FLIGHT:
        if (o)
            FCall<void>(O_FX_START_FLIGHT);
        else
            e.StartFlight();
        return "";
    case OP_FX_LOOK_POINT:
        if (o)
            FCall<void>(O_FX_LOOK_POINT);
        else
            e.UpdateLookPoint();
        return "";
    case OP_FX_LOOK_AT:
        SetHumanVehicle(h, c.vehicleType);
        if (o)
            FCall<void>(O_FX_LOOK_AT, hv);
        else
            e.LookAt(hp);
        return "";
    case OP_FX_SET_TACTICAL:
        if (o)
            FCall<void>(O_FX_SET_TACTICAL, c.flag);
        else
            e.SetTactical(c.flag);
        return "";
    case OP_FX_REMOVE:
        if (o)
            FCall<void>(O_FX_REMOVE, hv);
        else
            e.Remove(hp);
        return "";
    case OP_FX_SET_MODE: {
        alignas(16) S_vector4 point = c.vector;
        const S_vector4 *pp = c.hasPoint ? &point : nullptr;
        if (o)
            FCall<void>(O_FX_SET_MODE, c.number, static_cast<const void *>(pp));
        else
            e.SetTacticalMode(c.number, pp);
        return "";
    }
    case OP_FX_RESET_HUMANS:
        if (o)
            FCall<void>(O_FX_RESET_HUMANS, c.flag);
        else
            e.ResetHumans(c.flag);
        return "";
    case OP_FX_TRANSPARENCY:
        return FloatText(o ? FCall<float>(O_FX_TRANSPARENCY, hv) : e.GetTransparency(hp));
    case OP_FX_UPDATE_HUMAN:
        if (o)
            FCall<void>(O_FX_UPDATE_HUMAN, hv);
        else
            e.UpdateHuman(hp);
        return "";
    case OP_FX_UPDATE_HUMANS:
        if (o)
            FCall<void>(O_FX_UPDATE_HUMANS, c.number);
        else
            e.UpdateHumans(c.number);
        return "";
    case OP_FX_UPDATE_TACTICAL:
        if (o)
            FCall<void>(O_FX_UPDATE_TACTICAL, c.number);
        else
            e.UpdateTactical(c.number);
        return "";
    case OP_FX_MOVE: {
        const S_vector move(c.vector.x, c.vector.y, c.vector.z);
        void *r = o ? FCall<void *>(O_FX_MOVE, static_cast<const void *>(&move)) : static_cast<void *>(e.Move(move));
        return NameOf(reinterpret_cast<uint32_t>(r));
    }
    default:
        return "?";
    }
}

// ---- part 3: the update ----
void RunUpdateCall(const S_CamCase &c);
std::string RunUpdateOp(const S_CamCase &c)
{
    const bool o = g_side != 0;
    C_CameraModule &m = g_cameraModule;
    // (Both sides read these unchecked: the game camera in the update's paths that use it, the camera dummy in the
    // car's, the turret's arm (the dummy's parent) in view 8.)
    if (c.op == OP_UPDATE_CAR && (!g_player[g_side] || c.playerVehicle < 0 || W().actors.empty()))
        return "no player in a vehicle";
    RunUpdateCall(c);
    // (The inputs, for a failure's report.)
    return F("step %d sway ", c.number) + FloatText(c.aimSway);
}
void RunUpdateCall(const S_CamCase &c)
{
    const bool o = g_side != 0;
    C_CameraModule &m = g_cameraModule;
    switch (c.op) {
    case OP_UPDATE:
        if (o)
            OCall<void>(O_UPDATE, c.number);
        else
            m.Update(c.number);
        return;
    case OP_UPDATE_CAR:
        if (o)
            OCall<void>(O_UPDATE_CAR, c.number);
        else
            m.UpdateCarCamera(c.number);
        return;
    case OP_UPDATE_SCRIPTED:
        if (o)
            OCall<void>(O_UPDATE_SCRIPTED, c.number);
        else
            m.UpdateScriptedView(c.number);
        return;
    case OP_UPDATE_DEBUG:
        if (o)
            OCall<void>(O_UPDATE_DEBUG, c.number);
        else
            m.UpdateDebugCamera(c.number);
        return;
    case OP_PLACE:
        if (o)
            OCall<void>(O_PLACE, c.number);
        else
            m.PlaceCamera(c.number);
        return;
    case OP_MOVE_CAMERA:
        if (o)
            OCall<void>(O_MOVE_CAMERA, c.number);
        else
            m.MoveCamera(c.number);
        return;
    default:
        return;
    }
}

std::string RunOp(const S_CamCase &c)
{
    const bool o = g_side != 0;
    C_CameraModule &m = g_cameraModule;
    switch (c.op) {
    case OP_START:
        if (o)
            OCall<void>(O_START, c.flag);
        else
            m.StartMission(c.flag);
        return "";
    case OP_STOP:
        if (o)
            OCall<void>(O_STOP, c.flag);
        else
            m.StopMission(c.flag);
        return "";
    case OP_SAVE: {
        C_chunk chunk;                                  // (not used by either)
        return BoolText(o ? OCall<bool>(O_SAVE, static_cast<void *>(&chunk)) : m.SaveGame(chunk));
    }
    case OP_LOAD_SETTINGS: {
        if (o)
            OCall<void>(O_LOAD_SETTINGS);
        else
            m.LoadSettings();
        std::string hex = F("file kind %d:", c.fileKind);   // (the made file, for a failure's report)
        for (unsigned char ch : c.file)
            hex += F(" %02x", ch);
        return hex;
    }
    case OP_RESET_2AC:
        if (o)
            OCall<void>(O_RESET_2AC);
        else
            m.ResetUnknown2AC();
        return "";
    case OP_GET_POS:
    case OP_GET_DIR: {
        alignas(16) S_vector4 v = {9.0f, 9.0f, 9.0f, 9.0f};
        bool r;
        if (c.op == OP_GET_POS)
            r = o ? OCall<bool>(O_GET_POS, c.number, &v) : m.GetCameraPos(c.number, &v);
        else
            r = o ? OCall<bool>(O_GET_DIR, c.number, &v) : m.GetCameraDir(c.number, &v);
        return BoolText(r) + " " + VectorText(reinterpret_cast<uint32_t>(&v), 4);
    }
    case OP_BLEND:
        if (o)
            OCall<void>(O_BLEND, c.number);
        else
            m.SetBlendTime(c.number);
        return "";
    case OP_TARGET: {
        I3D_frame *f = AsFrame(FramePtr(c.frame));
        if (o)
            OCall<void>(O_TARGET, static_cast<void *>(f), c.number);
        else
            m.SetTarget(f, c.number);
        return "";
    }
    case OP_VIEW_DISTANCE:
        return FloatText(o ? OCall<float>(O_VIEW_DISTANCE) : m.GetViewDistance());
    case OP_SET_NEAR_FOV:
        return BoolText(o ? OCall<bool>(O_SET_NEAR_FOV, c.value) : m.SetNearSectorFov(c.value));
    case OP_GET_NEAR_FOV:
        return FloatText(o ? OCall<float>(O_GET_NEAR_FOV) : m.GetNearSectorFov());
    case OP_SET_FOV:
        return BoolText(o ? OCall<bool>(O_SET_FOV, c.value) : m.SetFov(c.value));
    case OP_GET_FOV:
        return FloatText(o ? OCall<float>(O_GET_FOV) : m.GetFov());
    case OP_SET_VIEW_DIR: {
        alignas(16) S_vector4 v = c.vector;
        if (o)
            OCall<void>(O_SET_VIEW_DIR, static_cast<const void *>(&v));
        else
            m.SetViewDir(v);
        return "";
    }
    case OP_IS_GAME_CAMERA: {
        I3D_frame *f = AsFrame(FramePtr(c.frame));
        return BoolText(o ? OCall<bool>(O_IS_GAME_CAMERA, static_cast<void *>(f)) : m.IsGameCamera(f));
    }
    case OP_GETTERS: {
        std::string s;
        if (o) {
            s = F("%d %d %d %d", int(OCall<bool>(O_GET78)), int(OCall<bool>(O_GET_STATE)), int(OCall<bool>(O_HAS_SCRIPTED)),
                  int(OCall<bool>(O_GET84)));
            s += reinterpret_cast<uint32_t>(OCall<void *>(O_GET_EFFECTS)) == At<uint32_t>(OCam(), 0x0C) ? " effects" : " other";
            s += " " + NameOf(reinterpret_cast<uint32_t>(OCall<void *>(O_GET_CAMERA)));
            s += " " + NameOf(reinterpret_cast<uint32_t>(OCall<void *>(O_GET_SECTOR)));
            using T_Get = void *(__stdcall *)();
            s += OriginalAt<T_Get>(O_GET_MODULE)() == OCam() ? " module" : " other";
        } else {
            s = F("%d %d %d %d", int(m.GetUnknown78()), int(m.GetState()), int(m.HasScriptedView()), int(m.GetUnknown84()));
            s += m.GetHumanEffects() == g_cameraHumanEffects ? " effects" : " other";
            s += " " + NameOf(reinterpret_cast<uint32_t>(m.GetCamera()));
            s += " " + NameOf(reinterpret_cast<uint32_t>(m.GetListenerSector()));
            s += GetCameraModule() == &m && m.IsUnderwater() == m.GetState() ? " module" : " other";
        }
        return s;
    }
    case OP_KEEPER:
        return RunKeeper(c);
    default:
        return IsFxOp(c.op) ? RunFxOp(c) : RunUpdateOp(c);
    }
}

// Part 2's fields: the effects' state, the faded humans, the enemies, the arguments.
S_vector UnitChoice()
{
    static const S_vector units[] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0.6f, 0, 0.8f}, {0, 0.6f, -0.8f},
                                     {0.1f, 0.3f, 0.9486833f}, {-0.48f, 0.6f, 0.64f}};
    return units[I(0, 7)];
}
void PutQuat(uint8_t *p, uint32_t o)
{
    static const float quats[][4] = {{1, 0, 0, 0}, {0.70710677f, 0, 0.70710677f, 0}, {0, 0, 1, 0}, {0.5f, 0.5f, 0.5f, 0.5f},
                                     {0.8f, 0, -0.6f, 0}, {-0.70710677f, 0, 0.70710677f, 0}, {-0.6f, 0, 0, 0.8f}};
    if (I(0, 3) == 0)
        return;                                         // (the noise CF() gave)
    std::memcpy(p + o, quats[I(0, 6)], 16);
}
void RandomFx(S_CamCase &c)
{
    c.fx[0x48] = uint8_t(I(0, 1));
    c.fx[0x49] = uint8_t(I(0, 1));
    PutI(c.fx, 0x44, I(0, 2) ? 0 : I(0, 3) ? I(1, 600) : I(-50, 0));
    PutI(c.fx, 0x4C, I(0, 7) ? I(0, 12) : I(-1, 14));
    PutF(c.fx, 0x40, I(0, 2) ? float(I(1, 16)) * 0.25f : CF());
    PutQuat(c.fx, 0x50);
    PutQuat(c.fx, 0x60);
    // The flight: its distance mostly positive, its time near the drag's 300 ms now and then.
    PutF(c.fx, 0x78, I(0, 3) ? std::fabs(CF()) : CF());
    static const float times[] = {299.5f, 300.0f, 300.5f, 0.0f};
    PutF(c.fx, 0x7C, I(0, 1) ? times[I(0, 3)] : CF() * 100.0f);
    if (I(0, 1)) {
        // The camera at small integers, the point an axis' length away (the flight's lengths exact).
        const float px = float(I(-5, 5)), py = float(I(-5, 5)), pz = float(I(-5, 5));
        static const float lengths[] = {0.5f, 1.0f, 2.0f, 4.0f, 5.0f, 6.0f, 30.0f, 35.0f, 40.0f, 34.5f, 2e-4f, 5e-5f};
        const float l = lengths[I(0, 11)];
        // (The flight's distance three or one and a half times the way left now and then: its thirds' edges.)
        if (I(0, 2) == 0)
            PutF(c.fx, 0x78, l * (I(0, 2) == 0 ? 3.0000002f : I(0, 1) ? 3.0f : 1.5f));
        // (Now and then the way's square 0.99999994 in the flight's order: the summed length's 1e-8 edge.)
        if (I(0, 7) == 0) {
            for (uint32_t o = 0x00; o < 0x0C; o += 4)
                PutF(c.fx, o + 0x20, 0.0f);
            PutF(c.fx, 0x00, 0.001f);
            PutF(c.fx, 0x04, 0.0f);
            PutF(c.fx, 0x08, 0.99999946f);
        }
        const int axis = I(0, 2);
        const float d = I(0, 1) ? l : -l;
        PutF(c.fx, 0x20, px);
        PutF(c.fx, 0x24, py);
        PutF(c.fx, 0x28, pz);
        PutF(c.fx, 0x00, px + (axis == 0 ? d : 0.0f));
        PutF(c.fx, 0x04, py + (axis == 1 ? d : 0.0f));
        PutF(c.fx, 0x08, pz + (axis == 2 ? d : 0.0f));
    }
    c.fxPtrs[1] = I(0, 7) ? CameraFrame() : -1;
    // The faded humans and the known enemies.
    static const float fades[] = {0.0f, 0.5f, 1.0f, 0.25f};
    for (int n = I(0, 3) ? I(0, 4) : 0; n > 0; --n)
        c.faded.push_back({I(0, 20), I(0, 4) ? fades[I(0, 3)] : CF()});
    for (int n = I(0, 1) ? I(0, 4) : 0; n > 0; --n)
        c.enemies.push_back(I(0, 20));
    c.human = I(0, 7) ? I(0, 20) : -1;
    if (!c.faded.empty() && I(0, 1))
        c.human = c.faded[size_t(I(0, int(c.faded.size()) - 1))].first;
    c.vehicleType = I(0, 1) ? -2 : I(0, 4) ? std::vector<int>{ACTOR_CAR, ACTOR_SUBMARINE, ACTOR_STATIC_WEAPON,
                                                              ACTOR_JUNKERS88, ACTOR_HUMAN, ACTOR_BIKE}[size_t(I(0, 5))]
                                            : -1;
    c.hasPoint = I(0, 3) != 0;
    c.motionX = I(0, 3) ? float(I(-4, 4)) * 0.25f : CF();
    c.motionY = I(0, 3) ? float(I(-4, 4)) * 0.25f : CF();
    PutF(c.fx, 0x34, c.motionX);
    PutF(c.fx, 0x38, c.motionY);
    if (I(0, 1)) {
        c.motionX = CF();
        c.motionY = CF();
    }
    c.viewDir = UnitChoice();
    switch (c.op) {
    case OP_FX_SET_MODE:
        c.number = I(0, 2) == 0 ? TACTICAL_DRAG : I(0, 7) ? I(0, 12) : I(-1, 14);
        if (I(0, 2) == 0)
            PutI(c.fx, 0x4C, TACTICAL_FLY);
        break;
    case OP_FX_SET_TACTICAL:
        // (Off: SelectView(0), part 4's; the debug camera on returns from it at once. On: the active player there.)
        c.image[0x26D] = 1;
        c.player = I(0, 20);
        break;
    case OP_FX_UPDATE_TACTICAL:
    case OP_FX_UPDATE_HUMANS:
        c.number = I(0, 3) ? I(0, 100) : I(-20, 1000);
        // (The double click's time running out exactly, or 1 ms short.)
        if (I(0, 3) == 0)
            PutI(c.fx, 0x44, c.number + I(0, 1));
        break;
    case OP_FX_TRANSPARENCY:
    case OP_FX_UPDATE_HUMAN:
        if (c.human < 0)
            c.human = I(0, 20);
        break;
    case OP_FX_MOVE:
        if (I(0, 1))
            c.vector = {float(I(-4, 4)) * 0.5f, float(I(-4, 4)) * 0.5f, float(I(-4, 4)) * 0.5f, 0.0f};
        break;
    default:
        break;
    }
}

// Part 3's fields: the paths the update takes, the mouse, the controls, the player's vehicle and seat.
void RandomUpdate(S_CamCase &c)
{
    static const int steps[] = {0, 1, 10, 16, 33, 50, 100, 250, -16};
    // (And the steps whose sway product a double rounds twice: the rates' midpoints.)
    static const int ties[] = {55, 75, 110, 150, 220, 300, 440, 600, 325, 425};
    c.number = I(0, 3) ? steps[I(0, 8)] : I(0, 1) ? ties[I(0, 9)] : I(-50, 600);
    // The update's path: the debug camera, the spectators' (returns), the scripted view, the car, the main one.
    if (c.op == OP_UPDATE) {
        const int path = I(0, 9);
        c.image[0x26D] = path == 0;
        c.image[0x26E] = path == 1 || (path == 0 && I(0, 1));
        c.image[0x2B0] = path == 2 || (path == 0 && I(0, 1));
    }
    if (c.op == OP_UPDATE_SCRIPTED || (c.op == OP_UPDATE && c.image[0x2B0]))
        c.ptrs[6] = I(0, 5) ? AnyFrame() : -1;
    // The game camera (the update's paths read it unchecked: the game has one).
    if (c.op != OP_PLACE && c.op != OP_MOVE_CAMERA && c.op != OP_UPDATE_SCRIPTED)
        c.ptrs[0] = CameraFrame();
    else if (I(0, 7))
        c.ptrs[0] = CameraFrame();
    // The view: mostly those the update treats apart.
    if (I(0, 1))
        PutI(c.image, 0x260, std::vector<int>{1, 2, 7, 8, 9, 10}[size_t(I(0, 5))]);
    // The camera dummy: mostly one with a parent (the turret's arm in view 8; the car reads it unchecked).
    static const int children[] = {1, 11, 8, 12, 16, 18, 19, 20};
    c.ptrs[2] = I(0, 2) ? children[I(0, 7)] : I(0, 1) ? AnyFrame() : -1;
    const int view = At<int>(c.image, 0x260);
    // (View 2 reads it unchecked with the head under water.)
    if (view == 8 || view == 2 || c.op == OP_UPDATE_CAR || I(0, 3) == 0)
        c.ptrs[2] = children[I(0, 7)];
    c.ownedCamera = I(0, 1) != 0;
    c.player = I(0, 5) ? I(0, 20) : -1;
    if (c.op == OP_UPDATE_CAR && c.player < 0)
        c.player = I(0, 20);
    // The player's vehicle: a car mostly (the car's camera), another kind, none.
    static const int kinds[] = {ACTOR_CAR, ACTOR_CAR, ACTOR_CAR, ACTOR_SUBMARINE, ACTOR_STATIC_WEAPON, ACTOR_JUNKERS88,
                                ACTOR_BIKE};
    c.playerVehicle = I(0, 2) ? kinds[I(0, 6)] : -1;
    if (c.op == OP_UPDATE_CAR)
        c.playerVehicle = I(0, 5) ? ACTOR_CAR : kinds[I(0, 6)];
    // (The car's camera reads the camera dummy unchecked: a car's seat has one.)
    if (c.playerVehicle == ACTOR_CAR)
        c.ptrs[2] = children[I(0, 7)];
    c.seat = I(0, 3) ? I(0, 3) : I(-1, 8);
    static const uint32_t flags[] = {0, SEAT_FLAG_WEAPON, SEAT_FLAG_HIDDEN, SEAT_FLAG_ARMED, SEAT_FLAG_HIDDEN | SEAT_FLAG_ARMED,
                                     SEAT_FLAG_CONTROLS | SEAT_FLAG_SIT, SEAT_FLAG_WEAPON | SEAT_FLAG_ARMED};
    c.seatFlags = I(0, 5) ? flags[I(0, 6)] : uint32_t(I(0, 0x7FF));
    c.fpvOnly = I(0, 2) == 0;
    c.selected = I(0, 2) ? -1 : I(0, 1) ? c.player : I(0, 20);
    // The mouse, the controls.
    static const int deltas[] = {0, 0, 1, -1, 5, -5, 40, -40, 200};
    c.mouseX = deltas[I(0, 8)];
    c.mouseY = deltas[I(0, 8)];
    c.buttons = uint32_t(I(0, 3) ? I(0, 3) : I(0, 255));
    c.invertMouse = I(0, 3) == 0;
    static const float looks[] = {0.0f, 0.0f, 0.25f, -0.25f, 1.0f, -1.0f, 0.05f, 0.6f, -0.6f};
    c.lookX = I(0, 3) ? looks[I(0, 8)] : CF();
    c.lookY = I(0, 3) ? looks[I(0, 8)] : CF();
    static const float speeds[] = {0.0f, 0.0f, 1.0f, -1.0f, 0.5f};
    c.speed1 = speeds[I(0, 4)];
    c.speed2 = speeds[I(0, 4)];
    c.roll = I(0, 2) ? 0.0f : CF();
    c.headUnder = I(0, 2) == 0;
    c.surface = CF();
    static const float sways[] = {0.005f, 0.0015f, 0.008f, 0.002f, 0.0016f, 0.0079f};
    c.aimSway = sways[I(0, 5)];
    // The module's view and settings near their edges now and then: the water level around the dummy, the blend.
    c.image[0x26F] = uint8_t(I(0, 3) != 0);           // (the mouse look mostly on)
    if (I(0, 2) == 0)
        PutI(c.image, 0x280, 0);
    if (I(0, 2) == 0)
        PutI(c.image, 0x284, 0);
    if (I(0, 1))
        PutF(c.image, 0x27C, float(I(-4, 4)) * 0.5f);
    // The directions unit now and then.
    if (I(0, 1)) {
        const S_vector d = UnitChoice();
        PutF(c.image, 0x20, d.x);
        PutF(c.image, 0x24, d.y);
        PutF(c.image, 0x28, d.z);
    }
    if (I(0, 1)) {
        const S_vector d = UnitChoice();
        PutF(c.image, 0x40, d.x);
        PutF(c.image, 0x44, d.y);
        PutF(c.image, 0x48, d.z);
    }
    c.flag = I(0, 2) == 0;                              // (the dynamic volume's frame the camera dummy)
    // The water's surface around the camera dummy (view 2 keeps the camera out of the band around it), the aim time
    // near its 5 s limit.
    if (I(0, 2) == 0) {
        static const float deltas[] = {0.0f, 0.05f, 0.1f, 0.2f, -0.05f, -0.15f, 1.0f, -1.0f};
        c.waterNear = true;
        c.waterDelta = deltas[I(0, 7)];
    }
    if (I(0, 3) == 0)
        PutI(c.image, 0x294, I(4700, 5000));
    // The shake's and the blend's times at their edges: 1 ms, the step, a ms either side.
    if (I(0, 3) == 0)
        PutI(c.image, 0x284, std::vector<int>{1, c.number, c.number + 1, c.number - 1}[size_t(I(0, 3))]);
    if (I(0, 3) == 0)
        PutI(c.image, 0x280, std::vector<int>{1, c.number, c.number + 1, -1}[size_t(I(0, 3))]);
    // The car's look just inside its limits, a small look over them; the debug camera's elevation near its 85 degrees.
    if (I(0, 3) == 0) {
        static const float yaws[] = {0.5215f, -0.5215f, 0.4330f, -0.4330f, 0.5f, -0.4f};
        PutF(c.image, 0x2BC, yaws[I(0, 5)]);
        PutF(c.image, 0x2B8, yaws[I(0, 5)]);
        static const float small[] = {0.001f, -0.001f, 0.0025f, -0.0025f, 0.004f};
        c.lookX = small[I(0, 4)];
        c.lookY = small[I(0, 4)];
    }
    if (c.op == OP_UPDATE_DEBUG && I(0, 2) == 0) {
        const float e = std::vector<float>{1.482f, -1.482f, 1.4835f, -1.479f}[size_t(I(0, 3))];
        PutF(c.image, 0x20, 0.0f);
        PutF(c.image, 0x24, std::sin(e));
        PutF(c.image, 0x28, std::cos(e));
    }
    // PlaceCamera's water band under the surface (0.1 m plus the radius, a millimetre or so either side).
    if (I(0, 4) == 0) {
        c.image[0x270] = 1;
        const float radius = At<float>(c.image, 0xE0 + 0x20 * uint32_t(At<int>(c.image, 0x260)) + 0x0C);
        c.waterNear = true;
        c.waterDelta = -(0.1f + radius) + std::vector<float>{-0.005f, 0.005f, -0.012f}[size_t(I(0, 2))];
    }
}

S_CamCase RandomCamCase()
{
    S_CamCase c;
    int total = 0;
    for (int w : OP_WEIGHTS)
        total += w;
    if (g_onlyOp >= 0) {
        c.op = g_onlyOp;
    } else {
        // (HD2_CAM_PART: one part's operations only.)
        do {
            c.op = 0;
            int r = I(0, total - 1);
            while (r >= OP_WEIGHTS[c.op])
                r -= OP_WEIGHTS[c.op++];
        } while (g_onlyPart && OpPart(c.op) != g_onlyPart);
    }
    // The module's bytes: noise, then each field a value of its kind.
    for (uint8_t &b : c.image)
        b = uint8_t(I(0, 255));
    for (uint32_t o = 0x10; o < 0xA0; o += 4)
        PutF(c.image, o, CF());
    PutI(c.image, 0xD0, I(-2, 2));
    for (uint32_t s = 0; s < 11; ++s) {
        const uint32_t b = 0xE0 + 0x20 * s;
        for (uint32_t o = 0; o < 0x20; o += 4)
            PutF(c.image, b + o, o == 0x18 ? -std::fabs(CF()) : std::fabs(CF()));
        PutI(c.image, b + 0x10, I(0, 1) ? 0x8B : I(0, 0xFF));
    }
    PutI(c.image, 0x250, I(-600, 600));
    PutF(c.image, 0x254, CF());
    PutF(c.image, 0x258, CF());
    PutI(c.image, 0x260, I(0, 10));
    PutI(c.image, 0x268, I(0, 9));
    for (uint32_t o : {0x26Cu, 0x26Du, 0x26Eu, 0x26Fu, 0x270u, 0x278u, 0x290u, 0x2B0u})
        c.image[o] = uint8_t(I(0, 1));
    PutI(c.image, 0x274, I(-1, 4));
    for (uint32_t o : {0x280u, 0x284u, 0x294u, 0x298u, 0x29Cu, 0x2A0u})
        PutI(c.image, o, I(0, 3) ? I(-300, 3000) : 0);
    for (uint32_t o : {0x27Cu, 0x28Cu, 0x2A4u, 0x2A8u, 0x2ACu, 0x2B8u, 0x2BCu})
        PutF(c.image, o, CF());
    // Its pointers: the cameras (the game camera mostly one), the camera dummy, the sector, the hit, the target, the
    // scripted view.
    c.ptrs[0] = I(0, 4) ? CameraFrame() : -1;           // +0x240
    c.ptrs[1] = I(0, 2) ? CameraFrame() : -1;           // +0x244
    c.ptrs[2] = I(0, 2) ? AnyFrame() : -1;              // +0x248
    c.ptrs[3] = I(0, 1) ? 9 : -1;                       // +0x24C the sector
    c.ptrs[4] = I(0, 1);                                // +0x25C a volume
    c.ptrs[5] = I(0, 2) ? -1 : AnyFrame();              // +0x288
    c.ptrs[6] = I(0, 2) ? -1 : AnyFrame();              // +0x2B4
    // The human effects: its fields; tactical now and then (StopMission ends it: SelectView, the game camera back).
    for (uint8_t &b : c.fx)
        b = uint8_t(I(0, 255));
    for (uint32_t o = 0; o < 0x80; o += 4)
        if (IsFxFloat(o))
            PutF(c.fx, o, CF());
    PutI(c.fx, 0x44, I(0, 600));
    c.fx[0x48] = 0;
    c.fx[0x49] = uint8_t(I(0, 1));
    PutI(c.fx, 0x4C, I(0, 12));
    c.fxPtrs[0] = CameraFrame();
    c.fxPtrs[1] = I(0, 3) ? CameraFrame() : -1;
    // The game.
    c.fov = float(I(30, 120)) * 0.017453292f;
    c.nearClip = float(I(1, 20)) * 0.05f;
    c.farClip = float(I(50, 3000));
    c.time = U32() % 1000000;
    c.activeCamera = CameraFrame();
    c.player = I(0, 4) ? I(0, 20) : -1;
    c.paused = I(0, 7) == 0;
    c.inputEnabled = I(0, 3) != 0;
    c.fpvShown = I(0, 3) == 0;
    c.binocularsShown = I(0, 7) == 0;
    // The arguments.
    c.flag = I(0, 1) != 0;
    c.number = I(0, 3) ? I(-1, 4) : I(-500, 3000);
    c.value = CF();
    c.frame = I(0, 3) ? AnyFrame() : -1;
    c.vector = {CF(), CF(), CF(), CF()};
    switch (c.op) {
    case OP_GET_POS:
    case OP_GET_DIR:
        if (I(0, 3) == 0)
            c.activeCamera = -1;                        // (no scene camera: the other operations read it unchecked)
        break;
    case OP_START:
        if (I(0, 7) == 0)
            c.activeCamera = -1;
        [[fallthrough]];
    case OP_LOAD_SETTINGS:
        c.fileKind = I(0, 3) ? I(0, 1) : 2;
        if (c.fileKind == 1)
            c.file = MakeSettingsFile();
        if (c.op == OP_START)
            c.flag = I(0, 3) == 0;                      // (mostly the client's: the server returns at once)
        break;
    case OP_STOP:
        c.ownedCamera = I(0, 1) != 0;
        // (Tactical now and then: SetTactical(false) then calls SelectView, part 4's: the debug camera on returns from
        // it at once.)
        if (I(0, 7) == 0) {
            c.fx[0x48] = 1;
            c.image[0x26D] = 1;
        }
        break;
    case OP_IS_GAME_CAMERA:
        if (I(0, 1))
            c.frame = c.ptrs[0];
        break;
    case OP_BLEND:
        if (I(0, 1))
            c.number = I(-1, 1000);
        break;
    case OP_KEEPER: {
        // A camera dummy under a parent (hlava, child1, joint1, hw1, rw, d under model1; child2 under model2; pack1
        // under d), or none. (Both sides read a frame's parent unchecked: the dummies always have one.)
        static const int children[] = {1, 11, 8, 12, 16, 18, 19, 20};
        c.keeperFrame = I(0, 7) ? children[I(0, 7)] : -1;
        c.keeperOwner = I(0, 2) ? -2 : I(0, 4) ? I(0, 30) : -1;
        // (A car among the world's actors: found by its kind in RunCamCase; -3 asks for one.)
        if (c.keeperOwner >= 0 && I(0, 1))
            c.keeperOwner = -3;
        // (Update, Restore, or the dummy's parent moved.)
        for (int n = I(0, 4); n > 0; --n)
            c.steps.push_back(I(0, 3) ? 0 : I(1, 2));
        c.deleteKeeper = I(0, 3) != 0;
        c.keeperUpright = I(0, 5) == 0;
        c.keeperAligned = !c.keeperUpright && I(0, 4) == 0;
        c.playerRides = I(0, 1) != 0;
        c.groundHit = I(0, 2) ? 0 : I(-20, 20);
        c.direction = {CF(), CF(), CF(), CF()};
        c.velocity = {CF(), CF(), CF(), CF()};
        c.speed = CF();
        c.bob = CF();
        if (c.keeperAligned) {
            // (A car moving along its heading, a submarine at 0 .. 2 m/s, its heading a little up or down now and then,
            // or turned a little: the dummy's view lined up with it, nearly or not.)
            c.keeperYawed = I(0, 1) != 0;
            const float x = c.keeperYawed ? 0.1f : 0.0f, z = c.keeperYawed ? 0.9486833f : 0.9539392f;
            c.direction = {x, I(0, 2) ? 0.0f : float(I(-2, 2)) * 0.05f, z, 0.0f};
            const float m = float(I(0, 4)) * 0.5f;
            c.velocity = {x * m + (I(0, 2) ? 0.0f : float(I(-3, 3)) * 0.01f), 0.0f, z * m, 0.0f};
            c.speed = float(I(0, 8)) * 0.25f + (I(0, 2) ? 0.0f : float(I(-3, 3)) * 0.001f);
        }
        // (Now and then a car barely moving: its direction too short to normalize.)
        if (I(0, 5) == 0) {
            // (Squared lengths under 1e-8, or between 1e-8 and 1e-7.)
            const float unit = I(0, 1) ? 1e-5f : 1e-4f;
            const bool noX = I(0, 1) != 0;
            c.direction = {noX ? 0.0f : float(I(-2, 2)) * unit, 0.0f, float(I(-2, 2)) * unit, 0.0f};
            c.velocity = {noX ? 0.0f : float(I(-2, 2)) * unit, 0.0f, float(I(-2, 2)) * unit, 0.0f};
            c.keeperOwner = -3;
            c.steps.insert(c.steps.begin(), 0);
        }
        break;
    }
    default:
        break;
    }
    if (IsFxOp(c.op))
        RandomFx(c);
    else if (OpPart(c.op) == 3)
        RandomUpdate(c);
    return c;
}

void RunCamCase(S_CamCase c)
{
    ++g_camCases;
    ++g_opCases[c.op];
    g_holdFrees = true;
    g_current = F("case %d: %s", g_camCases, OP_NAMES[c.op]);
    static const bool trace = std::getenv("HD2_CAM_TRACE") != nullptr;
    if (trace)
        std::printf("%s\n", g_current.c_str());
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    const std::string key = Lower("tables\\gamecam.set");
    g_overrides.erase(key);
    g_cache.erase(key);
    if (c.fileKind == 1)
        g_overrides[key] = c.file;
    else if (c.fileKind == 2)
        g_cache[key] = std::string("\x01missing", 8);
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        BuildWorld(side, worldSeed);
        // (The keeper's car: the world's first, the same on both sides.)
        if (c.keeperOwner == -3) {
            c.keeperOwner = -1;
            for (size_t i = 0; i < W().actors.size(); ++i)
                if (W().actors[i].type == ACTOR_CAR) {
                    c.keeperOwner = int(i);
                    break;
                }
        }
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        SetCamState(c);
        g_log[side].clear();
        std::srand(randSeed);
        g_case = SIDE_NAMES[side];
        const std::string answer = RunOp(c);
        g_case = "";
        std::vector<std::string> &out = side ? r.orig : r.port;
        out.push_back(answer);
        for (const std::string &s : CamState())
            out.push_back(s);
        for (const std::string &s : FrameStates())
            out.push_back(s);
        for (const S_FakeFrame &fr : W().frames)
            out.push_back("matrix " + fr.name + " " +
                          VectorText(reinterpret_cast<uint32_t>(&AsFrame(fr.bytes)->m_localMatrix.m[0][0]), 16));
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
    }
    if (g_show) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
    }
    if (!Compare(g_current, r) && std::getenv("HD2_CAM_DUMP")) {
        for (int side = 1; side >= 0; --side) {
            std::printf("  -- %s\n", SIDE_NAMES[side]);
            for (const std::string &s : side ? r.orig : r.port)
                std::printf("    %s\n", s.c_str());
        }
    }
    ReleaseHeldFrees();
    FreeWorld(0);
    FreeWorld(1);
}

LONG WINAPI DebugText(EXCEPTION_POINTERS *e)
{
    const EXCEPTION_RECORD *r = e->ExceptionRecord;
    if (r->ExceptionCode != 0x40010006u || r->NumberParameters < 2)   // DBG_PRINTEXCEPTION_C
        return EXCEPTION_CONTINUE_SEARCH;
    if (*g_case)
        g_log[g_side].push_back("debug " + std::string(reinterpret_cast<const char *>(r->ExceptionInformation[1]),
                                                       size_t(r->ExceptionInformation[0])));
    return EXCEPTION_CONTINUE_EXECUTION;
}
// The original's PoseGetRoll is __fastcall (the matrix in ECX, the roll written through EDX; the port's returns it):
// answered with the case's roll on both sides, unlogged.
void __fastcall O_PoseGetRoll(const void *, float *roll) { *roll = g_cur->roll; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align) { return _aligned_malloc(size, align); }
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
void SetUpCam()
{
    SetUpCmd();
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(&O_AlignedMalloc));
    RedirectOriginal(0x007e559c, reinterpret_cast<const void *>(&O_AlignedFree));
    RedirectOriginal(0x007e52d8, reinterpret_cast<const void *>(&O_dbgPrintf));
    RedirectOriginal(0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite));
    RedirectOriginal(0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek));
    RedirectOriginal(0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite));
    // The original's module (its static initialiser never ran): made in a world of its own (its human effects' camera
    // comes from the driver's CreateFrame; each case gives it another).
    BuildWorld(1, 1);
    using T_Ctor = void *(TC *)(void *, int, const char *);
    OriginalAt<T_Ctor>(O_CAM_CTOR)(OCam(), MODULE_CAMERA, "CameraModule");
    FreeWorld(1);
    AddVectoredExceptionHandler(1, DebugText);
    g_answers["C_Team::GetActivePlayer"] = [](const std::vector<uint32_t> &) { return reinterpret_cast<uint32_t>(g_player[g_side]); };
    g_extraQuiet.insert("C_Team::GetActivePlayer");
    // The sphere test: a hit (a volume; the hit point's y the case's) or none.
    // Part 2: a hit now and then, part of the way along the move (a volume, or a water surface's).
    static uint8_t volume[0x40], water[0x40];
    Name(volume, "volume");
    Name(water, "water");
    reinterpret_cast<S_CollisionVolume *>(water)->flags = COLLISION_FLAG_WATER;
    g_answers["CollisionSphereTest"] = [](const std::vector<uint32_t> &a) {
        if (g_fxCollisions) {
            const uint32_t r = NextFake() % 6;
            if (r < 2)
                return 0u;
            static const float parts[] = {0.0f, 0.25f, 0.5f, 1.0f};
            const S_vector &pos = *reinterpret_cast<const S_vector *>(a[0]);
            const S_vector &move = *reinterpret_cast<const S_vector *>(a[1]);
            const float f = parts[r - 2];
            if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
                *hit = S_vector(pos.x + move.x * f, pos.y + move.y * f, pos.z + move.z * f);
            // (Part 3: a dynamic volume now and then, its frame the camera dummy's or another.)
            const uint32_t kind = NextFake() % 3;
            if (kind == 2 && OpPart(g_cur->op) == 3)
                return reinterpret_cast<uint32_t>(g_dynamic[g_side]);
            return reinterpret_cast<uint32_t>(kind ? water : volume);
        }
        if (!g_groundHit)
            return 0u;
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(0.5f, float(g_groundHit) * 0.25f, -0.5f);
        return reinterpret_cast<uint32_t>(volume);
    };
    g_answers["CollisionSlideSphere"] = [](const std::vector<uint32_t> &a) {
        const uint32_t r = NextFake() % 7;
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[0]);
        const S_vector &move = *reinterpret_cast<const S_vector *>(a[1]);
        S_vector &end = *reinterpret_cast<S_vector *>(a[3]);
        if (r < 3) {
            end = S_vector(pos.x + move.x, pos.y + move.y, pos.z + move.z);
            return 0u;
        }
        static const float parts[] = {0.0f, 0.25f, 0.5f, 1.0f};
        const float f = parts[r - 3];
        // (Slid sideways a little.)
        end = S_vector(pos.x + move.x * f + 0.125f, pos.y + move.y * f, pos.z + move.z * f);
        return reinterpret_cast<uint32_t>(NextFake() % 2 ? water : volume);
    };
    // The squared distances: GetTransparency's edges among them.
    g_answers["SegmentDistance2"] = [](const std::vector<uint32_t> &) {
        static const float values[] = {0.0f, 0.4f, 2.4f, 1.0f, 0.5f, 10.0f, -1.0f, 2.5f, 0.39999998f, 2.4000003f, 1.4f,
                                       0.405f, 2.405f, 0.40000004f,
                                       // (MoveCamera's fade: 0.2 and 1.2.)
                                       0.2f, 1.2f, 0.205f, 1.205f, 0.25f, 0.19999999f};
        const float f = values[NextFake() % 20];
        uint32_t w;
        std::memcpy(&w, &f, 4);
        return w;
    };
    // The scene's sector at a point: the primary one or another.
    g_answers["I3D_scene::GetSector"] = [](const std::vector<uint32_t> &a) {
        static uint8_t other[0x40];
        Name(other, "other sector");
        return NextFake() % 2 ? *reinterpret_cast<const uint32_t *>(a[0] + 0x1F8) : reinterpret_cast<uint32_t>(other);
    };
    g_answers["C_Human::GetViewDir"] = [](const std::vector<uint32_t> &) { return reinterpret_cast<uint32_t>(&g_viewDir); };
    // Part 3: the mouse, the options' inverted mouse, the selected member, the controls, the dummy's roll, the head under
    // water, the ray test, the seats.
    g_answers["GetIGraph"] = [](const std::vector<uint32_t> &) { return reinterpret_cast<uint32_t>(g_igraph[g_side]); };
    g_extraQuiet.insert("GetIGraph");
    g_answers["IGraph::GetMouseDeltaX"] = [](const std::vector<uint32_t> &) { return uint32_t(g_cur->mouseX); };
    g_answers["IGraph::GetMouseDeltaY"] = [](const std::vector<uint32_t> &) { return uint32_t(g_cur->mouseY); };
    g_answers["IGraph::GetMouseButtons"] = [](const std::vector<uint32_t> &) { return g_cur->buttons; };
    g_answers["C_GameProfile::GetDetailSettings"] = [](const std::vector<uint32_t> &) {
        static uint8_t options[0x40];
        Name(options, "options");
        options[0x39] = g_cur->invertMouse;
        return reinterpret_cast<uint32_t>(options);
    };
    g_answers["C_Team::GetSelectedMember"] = [](const std::vector<uint32_t> &) {
        return reinterpret_cast<uint32_t>(HumanAt(g_cur->selected));
    };
    auto floatWord = [](float f) {
        uint32_t w;
        std::memcpy(&w, &f, 4);
        return w;
    };
    g_answers["C_Controls::GetValue"] = [floatWord](const std::vector<uint32_t> &a) {
        switch (a[1]) {
        case 0x34: return floatWord(g_cur->lookX);
        case 0x35: return floatWord(g_cur->lookY);
        case 0x36: return floatWord(g_speedCalls++ ? g_cur->speed2 : g_cur->speed1);
        default: return floatWord(0.0f);
        }
    };
    g_answers["PoseGetRoll"] = [floatWord](const std::vector<uint32_t> &) { return floatWord(g_cur->roll); };
    g_extraQuiet.insert("PoseGetRoll");
    RedirectOriginal(0x007a36c0, reinterpret_cast<const void *>(&O_PoseGetRoll));
    g_answers["C_HumanMovement::GetHeadWater"] = [](const std::vector<uint32_t> &a) {
        if (bool *under = reinterpret_cast<bool *>(a[1]))
            *under = g_cur->headUnder;
        if (float *surface = reinterpret_cast<float *>(a[2]))
            *surface = g_cur->surface;
        return uint32_t(g_cur->headUnder);
    };
    g_answers["C_CollisionWorld::RayTest"] = [](const std::vector<uint32_t> &a) {
        const uint32_t r = NextFake() % 5;
        if (r < 2)
            return 0u;
        static const float parts[] = {0.0f, 0.5f, 1.0f};
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        const S_vector &dir = *reinterpret_cast<const S_vector *>(a[2]);
        const float f = parts[r - 2];
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(pos.x + dir.x * f, pos.y + dir.y * f, pos.z + dir.z * f);
        if (S_vector *normal = reinterpret_cast<S_vector *>(a[4]))
            *normal = std::vector<S_vector>{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, {0.6f, 0, 0.8f}}[NextFake() % 4];
        static uint8_t ray[0x60];
        Name(ray, "ray volume");
        return reinterpret_cast<uint32_t>(NextFake() % 3 ? ray : g_dynamic[g_side]);
    };
    g_answers["C_Actor::GetSeatFlags"] = [](const std::vector<uint32_t> &) { return g_cur->seatFlags; };
    g_answers["C_Actor::IsSeatFirstPersonOnly"] = [](const std::vector<uint32_t> &) { return uint32_t(g_cur->fpvOnly); };
    if (const char *part = std::getenv("HD2_CAM_PART"))
        g_onlyPart = std::atoi(part);
    if (const char *op = std::getenv("HD2_CAM_OP"))
        for (int i = 0; i < OP_COUNT; ++i)
            if (Lower(op) == Lower(OP_NAMES[i]))
                g_onlyOp = i;
}

}  // namespace H

int main()
{
    using namespace H;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("cam_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *d = std::getenv("HD2_UNPACKED"))
        g_data = d;
    if (const char *s = std::getenv("HD2_CAM_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    g_show = std::getenv("HD2_CAM_SHOW") != nullptr;
    int cases = std::getenv("HD2_CAM_QUICK") ? 8000 : 40000;
    if (const char *s = std::getenv("HD2_CAM_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpCam();
    SetMission("AFRICA1");
    for (int n = 0; n < cases; ++n)
        RunCamCase(RandomCamCase());
    std::string ops;
    for (int i = 0; i < OP_COUNT; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_NAMES[i], g_opCases[i]);
    std::printf("cam_diff: %d cases (%s), %d calls logged, %d checks, %d failures\n", g_camCases, ops.c_str(), g_calls[0],
                checks, failures);
    // (The static module's destructor releases its human effects' camera: the frame the static driver made, not a case's
    // freed one.)
    g_cameraModule.m_humanEffects->m_camera = static_cast<I3D_camera *>(StaticCreateFrame(nullptr, FRAME_CAMERA));
    return failures != 0;
}
