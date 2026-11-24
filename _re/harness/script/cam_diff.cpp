// Camera part 1: the camera module's core (game/camera_module.cpp, C_CameraModule: the original's 0x0089afb0, vtable
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
// MinGW; build_cam.bat; runs cam_diff.exe ("cam_diff: N checks, 0 failures"). Env: HD2_CAM_CASES (default 40,000;
// HD2_CAM_QUICK 8,000), HD2_CAM_OP (one operation's name), HD2_CAM_SEED, HD2_CAM_SHOW, HD2_CAM_DUMP, HD2_CAM_TRACE.
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
    OP_GETTERS, OP_KEEPER, OP_COUNT
};
const char *const OP_NAMES[OP_COUNT] = {"StartMission", "StopMission", "SaveGame", "LoadSettings", "ResetUnknown2AC",
                                        "GetCameraPos", "GetCameraDir", "SetBlendTime", "SetTarget", "GetViewDistance",
                                        "SetNearSectorFov", "GetNearSectorFov", "SetFov", "GetFov", "SetViewDir",
                                        "IsGameCamera", "Getters", "Keeper"};
const int OP_WEIGHTS[OP_COUNT] = {12, 12, 2, 12, 2, 8, 8, 8, 5, 3, 5, 4, 5, 4, 8, 4, 4, 30};

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
};
int g_camCases, g_opCases[OP_COUNT], g_onlyOp = -1;
uint8_t *g_player[2];                                   // the case's active player on each side
int g_groundHit;                                        // CollisionSphereTest's answer (the keeper's ground)

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
            line += " " + WordText(cam, w, IsCamFloat(w),
                                   std::find(std::begin(CAM_POINTERS), std::end(CAM_POINTERS), w) != std::end(CAM_POINTERS));
        }
        out.push_back(line);
    }
    const uint8_t *fx = Effects();
    for (uint32_t o = 0; o < 0x88; o += 0x10) {
        std::string line = F("effects +%02x", o);
        for (uint32_t w = o; w < o + 0x10 && w < 0x88; w += 4)
            line += " " + WordText(fx, w, IsFxFloat(w),
                                   std::find(std::begin(FX_POINTERS), std::end(FX_POINTERS), w) != std::end(FX_POINTERS));
        out.push_back(line);
    }
    // The faded humans (empty in part 1).
    const uint32_t first = At<uint32_t>(fx, 0x8C), last = At<uint32_t>(fx, 0x90);
    out.push_back(F("effects humans %d", int((last - first) / 8)));
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
        return "?";
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
        int r = I(0, total - 1);
        while (r >= OP_WEIGHTS[c.op])
            r -= OP_WEIGHTS[c.op++];
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
    g_answers["CollisionSphereTest"] = [](const std::vector<uint32_t> &a) {
        if (!g_groundHit)
            return 0u;
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(0.5f, float(g_groundHit) * 0.25f, -0.5f);
        static uint8_t volume[0x40];
        Name(volume, "volume");
        return reinterpret_cast<uint32_t>(volume);
    };
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
