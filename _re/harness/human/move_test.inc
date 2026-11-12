// Part 6 of the human harness (HUMAN_MOVE builds, build_move.bat; included after ragdoll_test.inc): the movement
// itself, game/human_movement.cpp. Its calls into the movement's other parts (the idle, the visual and its lighting,
// the animation player, the special moves, the mixer, the morph, the work, the weapon pose, the equipment) and into
// the game (the shot manager, the surface marks, the sound module, the camera) are logged on both sides alike: the
// port's faked here, the original's redirected to the same loggers.
//
// Part 6a: the helpers and the small functions: S_HumanAnimRef::Normalize, RotateAboutX / Y / Z, AngleBetween2D,
// IsLowered, GetHeadWater, the camera under water, PlayDubbing, Unknown7865c0, UseObject, SetUnknown94,
// UpdateModelNames, OnRemoteAction, OnModelChanged, Ragdoll, SetPlayerControlled, AddBloodPool, ResetToFrame.
// Part 6b: the updates (UpdateAnimation, UpdatePhysics, UpdatePhysicsParts). 6c: the lifetime (SetOwner, Init, Done,
// Reset). 6d: saved games (SaveGame, LoadGame and AfterLoad; the base's SaveBase and LoadBase). 6e: the sound bank
// (C_MovementSounds) and the base's UpdateBreath under water.

#include "game/camera_module.h"
#include "game/actors_module.h"
#include "game/geffects.h"
#include "game/human_anim_tables.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/shots.h"
#include "game/surface_marks.h"
#include "ls3d/igraph.h"

// ---- the port's side: what the movement calls outside its file (logged; HUMAN_MOVE builds)
namespace {
// What the fakes answer (both sides).
struct S_MoveScript {
    bool carDestroyed = false;                          // C_Car::IsDestroyed
    bool coop = false;                                  // C_GameProfile::IsCooperativeGame
    int useLadder = 0;                                  // C_HumanSpecialMoves::UseLadder
    bool partSaves = true;                              // the parts' SaveGame / LoadGame
    bool soundDriver = false;                           // ISND_driver::IsInitialized
    I3D_frame *sector = nullptr;                        // I3D_scene::GetSector
} g_mv;
alignas(16) uint8_t g_fakeCameraModule[0x200];
alignas(16) uint8_t g_ladderBytes[0x100];                // the object used (only passed on)
// The vehicle the body sits in (a car or a static weapon: its type at +0x18 on both sides; GetSeatFlags at the
// original's slot 0x8C and the port's own, logged).
alignas(16) uint8_t g_carBytes[0x800];
void *g_carVt[128];
uint32_t g_seatFlags = 0;
uint32_t __attribute__((thiscall)) F_GetSeatFlags(void *self, int seat)
{
    Log(self, "GetSeatFlags", {double(seat)});
    return g_seatFlags;
}
alignas(16) uint8_t g_sndDriver[0x40];
void *g_sndDriverVt[32];
bool __stdcall F_SndIsInitialized(void *) { return g_mv.soundDriver; }
void __stdcall F_SndUnexpected(void *)
{
    std::printf("unexpected ISND_driver call\n");
    std::exit(3);
}
// The scene's human models (AddHumanModel 0x138, RemoveHumanModel 0x13C) and sectors (GetSector 0xE0), the driver's
// SetState (0x18): COM calls, the same for both sides.
void __stdcall F_AddHumanModel(void *self, I3D_frame *model, I3D_frame *q, I3D_frame *z)
{
    Log(self, ("AddHumanModel " + NameOf(model) + " " + NameOf(q) + " " + NameOf(z)).c_str());
}
void __stdcall F_RemoveHumanModel(void *self, I3D_frame *model) { Log(self, ("RemoveHumanModel " + NameOf(model)).c_str()); }
I3D_sector *__stdcall F_GetSector(void *self, const S_vector &pos)
{
    Log(self, "GetSector", {pos.x, pos.y, pos.z});
    return reinterpret_cast<I3D_sector *>(g_mv.sector);
}
void __stdcall F_SetState(void *self, int state, uint32_t value) { Log(self, "SetState", {double(state), double(value)}); }
// The scene's FindFrame (0x7C: LoadBase's seat by name): logged; the model's joints by name, else none.
I3D_frame *__stdcall F_SceneFindFrame(void *self, const char *name, uint32_t mask)
{
    Log(self, (std::string("FindFrame ") + name).c_str(), {double(mask)});
    return g_h.frame->FindChildFrame(name, FRAME_MASK_ANY);
}
// The effects module (base_test.inc's raw object): a vtable for SetModelTransparency, the original's slot 0x50 and the
// port's own, the same logger (thiscall: g++'s virtual calls and MSVC's alike).
void *g_geffectsVt[96];
bool __attribute__((thiscall)) F_SetModelTransparency(void *self, I3D_frame *model, float level)
{
    Log(self, ("SetModelTransparency " + NameOf(model)).c_str(), {level});
    return true;
}
void __attribute__((thiscall)) F_GEffectsUnexpected(void *)
{
    std::printf("unexpected effects module call\n");
    std::exit(3);
}
void *O_GetGEffects() { return g_fakeGEffects; }
}  // namespace

// The globals the movement reads (g_shotManager, g_gameProfile, g_soundCache: raw storage in human_globals.cpp).
float g_scanMin[3];
// (Made on first use: the movement's constructor, in MakeHuman, makes the sound bank, which asks it.)
extern "C" ISND_driver *ISndGetDriver()
{
    if (!g_sndDriverVt[0]) {
        for (void *&slot : g_sndDriverVt)
            slot = reinterpret_cast<void *>(F_SndUnexpected);
        g_sndDriverVt[0x0C / 4] = reinterpret_cast<void *>(F_SndIsInitialized);
        *reinterpret_cast<void ***>(g_sndDriver) = g_sndDriverVt;
    }
    return reinterpret_cast<ISND_driver *>(g_sndDriver);
}
extern bool g_openOk;
template <> HRESULT C_ResourceCache<I3D_sound>::OpenInto(I3D_sound *sound, const char *fileName, uint32_t, void *, void *, int)
{
    Log(sound, (std::string("SoundCacheOpenInto ") + fileName).c_str());
    return g_openOk ? I3D_OK : static_cast<HRESULT>(0x80000009);
}

void C_CameraModule::SelectView(int view) { Log(this, "SelectView", {double(view)}); }
C_CameraModule *C_game::GetCameraModule() { return reinterpret_cast<C_CameraModule *>(g_fakeCameraModule); }
bool C_Car::IsDestroyed() const { Log(this, "IsDestroyed"); return g_mv.carDestroyed; }
bool C_GameProfile::IsCooperativeGame() const { return g_mv.coop; }
void C_Human::SetMoveFlags(uint32_t flags) { Log(this, "SetMoveFlags", {double(flags)}); }
bool C_HumanAnimMixer::SaveGame(C_chunk &) { Log(this, "MixerSaveGame"); return g_mv.partSaves; }
bool C_HumanAnimMixer::LoadGame(C_chunk &) { Log(this, "MixerLoadGame"); return g_mv.partSaves; }
void C_HumanAnimPlayer::FindHeadDummy() { Log(this, "FindHeadDummy"); }
void C_HumanAnimPlayer::Init(C_HumanMovement *m, C_HumanSpecialMoves *s, C_HumanLocomotion *l, C_Human *o)
{
    Log(this, ("AnimPlayerInit " + NameOf(m) + " " + NameOf(s) + " " + NameOf(l) + " " + NameOf(o)).c_str());
}
bool C_HumanAnimPlayer::SaveGame(C_chunk &) { Log(this, "AnimPlayerSaveGame"); return g_mv.partSaves; }
bool C_HumanAnimPlayer::LoadGame(C_chunk &) { Log(this, "AnimPlayerLoadGame"); return g_mv.partSaves; }
void C_HumanAnimPlayer::Reset() { Log(this, "AnimPlayerReset"); }
void C_HumanAnimPlayer::Update() { Log(this, "AnimPlayerUpdate"); }
C_HumanAnimTable *GetHumanAnimTable(int) { return nullptr; }
int C_HumanAnimTable::GetAnimLength(int, int, int) const { return -1; }
void C_HumanEquipment::Unknown793b40() { Log(this, "Unknown793b40"); }
void C_HumanEquipment::UpdateModelNames() { Log(this, "EquipmentUpdateModelNames"); }
bool C_HumanIdle::SaveGame(C_chunk &) { Log(this, "IdleSaveGame"); return g_mv.partSaves; }
bool C_HumanIdle::LoadGame(C_chunk &) { Log(this, "IdleLoadGame"); return g_mv.partSaves; }
bool C_HumanIdle::SaveStatics(C_chunk &) { Log(nullptr, "IdleSaveStatics"); return g_mv.partSaves; }
bool C_HumanIdle::LoadStatics(C_chunk &) { Log(nullptr, "IdleLoadStatics"); return g_mv.partSaves; }
void C_HumanIdle::Reset() { Log(this, "IdleReset"); }
void C_HumanIdle::Update(int time) { Log(this, "IdleUpdate", {double(time)}); }
void C_HumanIdle::UpdateInteraction(int time) { Log(this, "IdleUpdateInteraction", {double(time)}); }
void C_HumanLighting::Init() { Log(this, "LightingInit"); }
void C_HumanLighting::Sample(I3D_sector *sector, I3D_frame *frame, const S_vector &pos, bool now)
{
    Log(this, ("LightingSample " + NameOf(sector) + " " + NameOf(frame)).c_str(), {pos.x, pos.y, pos.z, double(now)});
}
void C_HumanLighting::SetSector(I3D_sector *sector) { Log(this, ("LightingSetSector " + NameOf(sector)).c_str()); }
void C_HumanLighting::Update(I3D_frame *frame, int time, int mode)
{
    Log(this, ("LightingUpdate " + NameOf(frame)).c_str(), {double(time), double(mode)});
}
bool C_HumanMorph::SaveGame(C_chunk &) { Log(this, "MorphSaveGame"); return g_mv.partSaves; }
bool C_HumanMorph::LoadGame(C_chunk &) { Log(this, "MorphLoadGame"); return g_mv.partSaves; }
void C_HumanMorph::Reset() { Log(this, "MorphReset"); }
void C_HumanSpecialMoves::Init(C_HumanMovement *m, C_HumanLocomotion *l, C_HumanAnimPlayer *a, C_HumanIdle *i, C_Human *o)
{
    Log(this, ("SpecialMovesInit " + NameOf(m) + " " + NameOf(l) + " " + NameOf(a) + " " + NameOf(i) + " " + NameOf(o)).c_str());
}
int C_HumanSpecialMoves::UseLadder(C_Actor *ladder) { Log(this, ("UseLadder " + NameOf(ladder)).c_str()); return g_mv.useLadder; }
void C_HumanVisual::Done() { Log(this, "VisualDone"); }
void C_HumanVisual::Init() { Log(this, "VisualInit"); }
bool C_HumanVisual::SaveGame(C_chunk &) { Log(this, "VisualSaveGame"); return g_mv.partSaves; }
bool C_HumanVisual::LoadGame(C_chunk &) { Log(this, "VisualLoadGame"); return g_mv.partSaves; }
void C_HumanVisual::Reset() { Log(this, "VisualReset"); }
void C_HumanVisual::SetNetworkDirection(const S_vector4 &dir) { LogV(this, "SetNetworkDirection", &dir.x, 4); }
void C_HumanVisual::SetNetworkDriven(bool on) { Log(this, "SetNetworkDriven", {double(on)}); }
void C_HumanVisual::Update(int time) { Log(this, "VisualUpdate", {double(time)}); }
void C_HumanVisual::UpdatePosition(int time) { Log(this, "VisualUpdatePosition", {double(time)}); }
void C_HumanWeaponPose::Attach(I3D_frame *frame, C_Human *owner, bool loaded)
{
    Log(this, ("PoseAttach " + NameOf(frame) + " " + NameOf(owner)).c_str(), {double(loaded)});
}
void C_HumanWeaponPose::BindSkeleton(int kind) { Log(this, "PoseBindSkeleton", {double(kind)}); }
bool C_HumanWeaponPose::SaveGame(C_chunk &) { Log(this, "PoseSaveGame"); return g_mv.partSaves; }
bool C_HumanWeaponPose::LoadGame(C_chunk &) { Log(this, "PoseLoadGame"); return g_mv.partSaves; }
void C_HumanWeaponPose::Refresh(int arg) { Log(this, "PoseRefresh", {double(arg)}); }
void C_HumanWeaponPose::Reset() { Log(this, "PoseReset"); }
void C_HumanWeaponPose::Update(int time) { Log(this, "PoseUpdate", {double(time)}); }
bool C_HumanWork::SaveGame(C_chunk &) { Log(this, "WorkSaveGame"); return g_mv.partSaves; }
bool C_HumanWork::LoadGame(C_chunk &) { Log(this, "WorkLoadGame"); return g_mv.partSaves; }
void C_HumanWork::Update(int time) { Log(this, "WorkUpdate", {double(time)}); }
void C_HumanWork::UpdateStanceTimer(int time) { Log(this, "UpdateStanceTimer", {double(time)}); }
void C_ShotManager::AddBloodPool(const S_vector &pos, const S_vector &normal, float size)
{
    Log(this, "AddBloodPool", {pos.x, pos.y, pos.z, normal.x, normal.y, normal.z, size});
}
bool C_SoundModule::PlayDubbing(C_Human *owner, I3D_frame *frame, int dubbing, float nearRange, float farRange, bool unknown)
{
    Log(this, ("PlayDubbing " + NameOf(owner) + " " + NameOf(frame)).c_str(), {double(dubbing), nearRange, farRange, double(unknown)});
    return true;
}
void SurfaceMarks_AddBlood(const S_vector &pos) { Log(nullptr, "SurfaceMarks_AddBlood", {pos.x, pos.y, pos.z}); }
void SurfaceMarks_Init() { Log(nullptr, "SurfaceMarks_Init"); }
// The movement base's InitBase and DoneBase (they make and free the body's volumes, which the test owns): logged
// through the linker's --wrap (build_move.bat), the original's redirected.
extern "C" {
void __attribute__((thiscall)) __wrap__ZN15C_HumanMovement8InitBaseEv(C_HumanMovement *self) { Log(self, "InitBase"); }
void __attribute__((thiscall)) __wrap__ZN15C_HumanMovement8DoneBaseEv(C_HumanMovement *self) { Log(self, "DoneBase"); }
}
extern int g_missionVariant;
void SurfaceMarks_Done() { Log(nullptr, "SurfaceMarks_Done"); }

// ---- the original's side: the same loggers (MSVC thiscall; bools as words)
namespace {
#define OT __attribute__((thiscall))
void OT OM_SelectView(void *self, int view) { Log(self, "SelectView", {double(view)}); }
void *OT OM_GetCameraModule(void *) { return g_fakeCameraModule; }
bool OT OM_IsDestroyed(void *self) { Log(self, "IsDestroyed"); return g_mv.carDestroyed; }
bool OT OM_IsCooperativeGame(void *) { return g_mv.coop; }
void OT OM_SetMoveFlags(void *self, uint32_t flags) { Log(self, "SetMoveFlags", {double(flags)}); }
bool OT OM_MixerSaveGame(void *self, void *) { Log(self, "MixerSaveGame"); return g_mv.partSaves; }
bool OT OM_MixerLoadGame(void *self, void *) { Log(self, "MixerLoadGame"); return g_mv.partSaves; }
void OT OM_FindHeadDummy(void *self) { Log(self, "FindHeadDummy"); }
void OT OM_AnimPlayerInit(void *self, void *m, void *s, void *l, void *o)
{
    Log(self, ("AnimPlayerInit " + NameOf(m) + " " + NameOf(s) + " " + NameOf(l) + " " + NameOf(o)).c_str());
}
bool OT OM_AnimPlayerSaveGame(void *self, void *) { Log(self, "AnimPlayerSaveGame"); return g_mv.partSaves; }
bool OT OM_AnimPlayerLoadGame(void *self, void *) { Log(self, "AnimPlayerLoadGame"); return g_mv.partSaves; }
void OT OM_AnimPlayerReset(void *self) { Log(self, "AnimPlayerReset"); }
void OT OM_AnimPlayerUpdate(void *self) { Log(self, "AnimPlayerUpdate"); }
void OT OM_Unknown793b40(void *self) { Log(self, "Unknown793b40"); }
void OT OM_EquipmentUpdateModelNames(void *self) { Log(self, "EquipmentUpdateModelNames"); }
bool OT OM_IdleSaveGame(void *self, void *) { Log(self, "IdleSaveGame"); return g_mv.partSaves; }
bool OT OM_IdleLoadGame(void *self, void *) { Log(self, "IdleLoadGame"); return g_mv.partSaves; }
void OT OM_IdleReset(void *self) { Log(self, "IdleReset"); }
void OT OM_IdleUpdate(void *self, int time) { Log(self, "IdleUpdate", {double(time)}); }
void OT OM_IdleUpdateInteraction(void *self, int time) { Log(self, "IdleUpdateInteraction", {double(time)}); }
void OT OM_LightingInit(void *self) { Log(self, "LightingInit"); }
void OT OM_LightingSample(void *self, void *sector, void *frame, const float *pos, int now)
{
    Log(self, ("LightingSample " + NameOf(sector) + " " + NameOf(frame)).c_str(), {pos[0], pos[1], pos[2], double(B(now))});
}
void OT OM_LightingSetSector(void *self, void *sector) { Log(self, ("LightingSetSector " + NameOf(sector)).c_str()); }
void OT OM_LightingUpdate(void *self, void *frame, int time, int mode)
{
    Log(self, ("LightingUpdate " + NameOf(frame)).c_str(), {double(time), double(mode)});
}
bool OT OM_MorphSaveGame(void *self, void *) { Log(self, "MorphSaveGame"); return g_mv.partSaves; }
bool OT OM_MorphLoadGame(void *self, void *) { Log(self, "MorphLoadGame"); return g_mv.partSaves; }
void OT OM_MorphReset(void *self) { Log(self, "MorphReset"); }
void OT OM_SpecialMovesInit(void *self, void *m, void *l, void *a, void *i, void *o)
{
    Log(self, ("SpecialMovesInit " + NameOf(m) + " " + NameOf(l) + " " + NameOf(a) + " " + NameOf(i) + " " + NameOf(o)).c_str());
}
int OT OM_UseLadder(void *self, void *ladder) { Log(self, ("UseLadder " + NameOf(ladder)).c_str()); return g_mv.useLadder; }
void OT OM_VisualDone(void *self) { Log(self, "VisualDone"); }
void OT OM_VisualInit(void *self) { Log(self, "VisualInit"); }
bool OT OM_VisualSaveGame(void *self, void *) { Log(self, "VisualSaveGame"); return g_mv.partSaves; }
bool OT OM_VisualLoadGame(void *self, void *) { Log(self, "VisualLoadGame"); return g_mv.partSaves; }
void OT OM_VisualReset(void *self) { Log(self, "VisualReset"); }
void OT OM_SetNetworkDirection(void *self, const float *dir) { LogV(self, "SetNetworkDirection", dir, 4); }
void OT OM_SetNetworkDriven(void *self, int on) { Log(self, "SetNetworkDriven", {double(B(on))}); }
void OT OM_VisualUpdate(void *self, int time) { Log(self, "VisualUpdate", {double(time)}); }
void OT OM_VisualUpdatePosition(void *self, int time) { Log(self, "VisualUpdatePosition", {double(time)}); }
void OT OM_PoseAttach(void *self, void *frame, void *owner, int loaded)
{
    Log(self, ("PoseAttach " + NameOf(frame) + " " + NameOf(owner)).c_str(), {double(B(loaded))});
}
void OT OM_PoseBindSkeleton(void *self, int kind) { Log(self, "PoseBindSkeleton", {double(kind)}); }
bool OT OM_PoseSaveGame(void *self, void *) { Log(self, "PoseSaveGame"); return g_mv.partSaves; }
bool OT OM_PoseLoadGame(void *self, void *) { Log(self, "PoseLoadGame"); return g_mv.partSaves; }
void OT OM_PoseRefresh(void *self, int arg) { Log(self, "PoseRefresh", {double(arg)}); }
void OT OM_PoseReset(void *self) { Log(self, "PoseReset"); }
void OT OM_PoseUpdate(void *self, int time) { Log(self, "PoseUpdate", {double(time)}); }
bool OT OM_WorkSaveGame(void *self, void *) { Log(self, "WorkSaveGame"); return g_mv.partSaves; }
bool OT OM_WorkLoadGame(void *self, void *) { Log(self, "WorkLoadGame"); return g_mv.partSaves; }
void OT OM_WorkUpdate(void *self, int time) { Log(self, "WorkUpdate", {double(time)}); }
void OT OM_UpdateStanceTimer(void *self, int time) { Log(self, "UpdateStanceTimer", {double(time)}); }
void OT OM_AddBloodPool(void *self, const float *pos, const float *normal, float size)
{
    Log(self, "AddBloodPool", {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], size});
}
bool OT OM_PlayDubbing(void *self, void *owner, void *frame, int dubbing, float nearRange, float farRange, int unknown)
{
    Log(self, ("PlayDubbing " + NameOf(owner) + " " + NameOf(frame)).c_str(), {double(dubbing), nearRange, farRange, double(B(unknown))});
    return true;
}
void __fastcall OM_SurfaceMarksAddBlood(const float *pos) { Log(nullptr, "SurfaceMarks_AddBlood", {pos[0], pos[1], pos[2]}); }
void OM_SurfaceMarksInit() { Log(nullptr, "SurfaceMarks_Init"); }
void OM_SurfaceMarksDone() { Log(nullptr, "SurfaceMarks_Done"); }
void *OM_ISndGetDriver() { return ISndGetDriver(); }
void OT OM_InitBase(void *self) { Log(self, "InitBase"); }
void OT OM_DoneBase(void *self) { Log(self, "DoneBase"); }
void *OM_I3DGetDriver() { return g_driverBytes; }
#undef OT

I3D_frame *g_jointFrame[HUMAN_JOINT_COUNT];              // the model's joints (SetUpMove)
alignas(16) uint8_t g_origFirstPerson[0x80];             // the original's game's +0x5C (RandomUpdate)
// The original's globals the movement reads.
const uint32_t O_SCAN_MIN = 0x0097ca30;
const uint32_t O_SHOT_MANAGER = 0x0089aac0;
const uint32_t O_GAME_PROFILE = 0x008aea0c;

void SetUpMove()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0046c370, reinterpret_cast<const void *>(OM_SelectView)},
        {0x0069a620, reinterpret_cast<const void *>(OM_GetCameraModule)},
        {0x004ddcf0, reinterpret_cast<const void *>(OM_IsDestroyed)},
        {0x006b69d0, reinterpret_cast<const void *>(OM_IsCooperativeGame)},
        {0x005a5c90, reinterpret_cast<const void *>(OM_SetMoveFlags)},
        {0x00776610, reinterpret_cast<const void *>(OM_MixerSaveGame)},
        {0x00776f90, reinterpret_cast<const void *>(OM_MixerLoadGame)},
        {0x0077d870, reinterpret_cast<const void *>(OM_FindHeadDummy)},
        {0x00777ee0, reinterpret_cast<const void *>(OM_AnimPlayerInit)},
        {0x0077d8c0, reinterpret_cast<const void *>(OM_AnimPlayerSaveGame)},
        {0x0077dc90, reinterpret_cast<const void *>(OM_AnimPlayerLoadGame)},
        {0x00777ea0, reinterpret_cast<const void *>(OM_AnimPlayerReset)},
        {0x00779f90, reinterpret_cast<const void *>(OM_AnimPlayerUpdate)},
        {0x00793b40, reinterpret_cast<const void *>(OM_Unknown793b40)},
        {0x00792510, reinterpret_cast<const void *>(OM_EquipmentUpdateModelNames)},
        {0x007513f0, reinterpret_cast<const void *>(OM_IdleSaveGame)},
        {0x007515c0, reinterpret_cast<const void *>(OM_IdleLoadGame)},
        {0x00750b40, reinterpret_cast<const void *>(OM_IdleReset)},
        {0x00750c00, reinterpret_cast<const void *>(OM_IdleUpdate)},
        {0x00755960, reinterpret_cast<const void *>(OM_IdleUpdateInteraction)},
        {0x0078a100, reinterpret_cast<const void *>(OM_LightingInit)},
        {0x0078a210, reinterpret_cast<const void *>(OM_LightingSample)},
        {0x0078a420, reinterpret_cast<const void *>(OM_LightingSetSector)},
        {0x0078a130, reinterpret_cast<const void *>(OM_LightingUpdate)},
        {0x00782310, reinterpret_cast<const void *>(OM_MorphSaveGame)},
        {0x00782730, reinterpret_cast<const void *>(OM_MorphLoadGame)},
        {0x00782cd0, reinterpret_cast<const void *>(OM_MorphReset)},
        {0x0074a290, reinterpret_cast<const void *>(OM_SpecialMovesInit)},
        {0x0074b950, reinterpret_cast<const void *>(OM_UseLadder)},
        {0x007583b0, reinterpret_cast<const void *>(OM_VisualDone)},
        {0x00758270, reinterpret_cast<const void *>(OM_VisualInit)},
        {0x0075a840, reinterpret_cast<const void *>(OM_VisualSaveGame)},
        {0x0075ab60, reinterpret_cast<const void *>(OM_VisualLoadGame)},
        {0x0075b0c0, reinterpret_cast<const void *>(OM_VisualReset)},
        {0x00759fe0, reinterpret_cast<const void *>(OM_SetNetworkDirection)},
        {0x007583c0, reinterpret_cast<const void *>(OM_SetNetworkDriven)},
        {0x00758c90, reinterpret_cast<const void *>(OM_VisualUpdate)},
        {0x00758c50, reinterpret_cast<const void *>(OM_VisualUpdatePosition)},
        {0x0079c190, reinterpret_cast<const void *>(OM_PoseAttach)},
        {0x0079c780, reinterpret_cast<const void *>(OM_PoseBindSkeleton)},
        {0x0079d540, reinterpret_cast<const void *>(OM_PoseSaveGame)},
        {0x0079f3f0, reinterpret_cast<const void *>(OM_PoseLoadGame)},
        {0x007a8760, reinterpret_cast<const void *>(OM_PoseRefresh)},
        {0x0079d530, reinterpret_cast<const void *>(OM_PoseReset)},
        {0x007a78d0, reinterpret_cast<const void *>(OM_PoseUpdate)},
        {0x00780280, reinterpret_cast<const void *>(OM_WorkSaveGame)},
        {0x00780560, reinterpret_cast<const void *>(OM_WorkLoadGame)},
        {0x0077e290, reinterpret_cast<const void *>(OM_WorkUpdate)},
        {0x00780140, reinterpret_cast<const void *>(OM_UpdateStanceTimer)},
        {0x0044f2b0, reinterpret_cast<const void *>(OM_AddBloodPool)},
        {0x0078b090, reinterpret_cast<const void *>(OM_SurfaceMarksAddBlood)},
        {0x0078a950, reinterpret_cast<const void *>(OM_SurfaceMarksInit)},
        {0x0078aa50, reinterpret_cast<const void *>(OM_SurfaceMarksDone)},
        {0x007e531a, reinterpret_cast<const void *>(OM_ISndGetDriver)},
        {0x0075b6e0, reinterpret_cast<const void *>(OM_InitBase)},
        {0x0075bb20, reinterpret_cast<const void *>(OM_DoneBase)},
        {0x007e529c, reinterpret_cast<const void *>(OM_I3DGetDriver)},    // (the import thunk: LS3DF.dll's otherwise)
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_origSoundVt[0x50 / 4] = reinterpret_cast<void *>(OM_PlayDubbing);   // (the original's calls it by its vtable)
    // (The original's calls SelectView through the camera module's vtable, +0x40.)
    static void *cameraVt[32];
    for (void *&slot : cameraVt)
        slot = reinterpret_cast<void *>(F_GEffectsUnexpected);
    cameraVt[0x40 / 4] = reinterpret_cast<void *>(OM_SelectView);
    *reinterpret_cast<void ***>(g_fakeCameraModule) = cameraVt;
    for (void *&slot : g_geffectsVt)
        slot = reinterpret_cast<void *>(F_GEffectsUnexpected);
    g_geffectsVt[0x50 / 4] = reinterpret_cast<void *>(F_SetModelTransparency);
    g_geffectsVt[VSlot(&C_GEffectsModule::SetModelTransparency)] = reinterpret_cast<void *>(F_SetModelTransparency);
    *reinterpret_cast<void ***>(g_fakeGEffects) = g_geffectsVt;
    g_names[g_fakeGEffects] = "geffects";
    RedirectOriginal(0x00486510, reinterpret_cast<const void *>(O_GetGEffects));
    g_sceneVt[0x138 / 4] = reinterpret_cast<void *>(F_AddHumanModel);
    g_sceneVt[0x13C / 4] = reinterpret_cast<void *>(F_RemoveHumanModel);
    g_sceneVt[0xE0 / 4] = reinterpret_cast<void *>(F_GetSector);
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(F_SceneFindFrame);
    g_driverVt[0x18 / 4] = reinterpret_cast<void *>(F_SetState);
    g_names[g_driverBytes] = "driver";
    g_names[g_fakeCameraModule] = "cameraModule";
    g_names[g_ladderBytes] = "ladder";
    for (void *&slot : g_carVt)
        slot = reinterpret_cast<void *>(F_GEffectsUnexpected);
    g_carVt[0x8C / 4] = g_carVt[VSlot(&C_Actor::GetSeatFlags)] = reinterpret_cast<void *>(F_GetSeatFlags);
    *reinterpret_cast<void ***>(g_carBytes) = g_carVt;
    g_names[g_carBytes] = "car";
    g_names[&g_shotManager] = g_names[OriginalAt<void *>(O_SHOT_MANAGER)] = "shotManager";
    C_HumanMovement *m = g_h.movement;
    g_names[&m->m_idle] = g_names[o_movement + 0xCB0] = "idle";
    g_names[&m->m_visual.m_lighting] = g_names[o_movement + 0xCD0 + 0xD4] = "lighting";
    g_scanMin[1] = *OriginalAt<float *>(O_SCAN_MIN + 4) = -20.0f;
    g_mv.sector = NewFrame(FRAME_SECTOR, "sector2", S_vector(0, 0, 0), Identity(), Box(S_vector(10, 10, 10)), "", g_sector);
    // The profile times: microseconds of the performance counter, different on each side.
    g_skipFields.insert("m_animProfileTime");
    g_skipFields.insert("m_physicsProfileTime");
    // The model's other joints (OnModelChanged relinks all 25 by name: the base's model has the feet and the pelvis),
    // their places and reference counts compared.
    static const char *const joints[HUMAN_JOINT_COUNT] = {"u", "q", "t", "p", "f1", "d1", "d", "c1", "g", "k", "h", "l", "i",
                                                          "m", "j", "n", "a", "b", "c", "e", "f", "o", "r", "s", "v"};
    for (int i = 0; i < HUMAN_JOINT_COUNT; ++i) {
        I3D_frame *f = g_h.frame->FindChildFrame(joints[i], FRAME_MASK_ANY);
        if (!f)
            f = ModelDummy(joints[i], S_vector(0.02f * i - 0.25f, 0.06f * i, 0.01f * i), 0.04f);
        g_jointFrame[i] = f;
        g_sharedFrames.push_back(f);
        ShareBlock(&f->m_localMatrix, sizeof(S_matrix), std::string("joint ") + joints[i]);
        g_sharedWords.push_back({reinterpret_cast<uint32_t *>(&f->m_refCount), std::string("joint refs ") + joints[i]});
    }
}

// The remote actions: the port's std::deque, the original's MSVC6 deque at +0xA4C: an allocator word; the first and
// the last iterator, four words each (block start, block end, next, map slot); the map, its size, the count. Blocks
// hold 341 actions (0xFFC bytes). Empty, it is all zero. Before the original's call it is built from the port's queue
// (its blocks and map from malloc: the original's operator new and delete are the test's malloc and free); after the
// call both queues are compared whole.
constexpr int DEQUE_BLOCK = 0xFFC / sizeof(S_RemoteAction);
// (The port's queue before its call: its actions are changed in place on the heap, which restoring the movement's
// bytes does not undo.)
std::vector<S_RemoteAction> g_queueBefore, g_portQueue;
std::string g_queueDiffers;
uint32_t g_frameFlagsBefore, g_portFrameFlags;           // the model frame's flags (DiffMove)
std::vector<S_RemoteAction> PortQueue()
{
    const auto &q = g_h.movement->m_remoteActions;
    return std::vector<S_RemoteAction>(q.begin(), q.end());
}
std::vector<S_RemoteAction> OriginalQueue(const uint8_t *movement)
{
    const uint8_t *d = movement + 0xA4C;
    const auto word = [d](int at) { return *reinterpret_cast<const uintptr_t *>(d + at); };
    std::vector<S_RemoteAction> q;
    const uint32_t size = static_cast<uint32_t>(word(0x2C));
    auto *next = reinterpret_cast<const S_RemoteAction *>(word(0x0C));
    auto *last = reinterpret_cast<const S_RemoteAction *>(word(0x08));
    auto *slot = reinterpret_cast<S_RemoteAction *const *>(word(0x10));
    for (uint32_t i = 0; i < size && i < 1000; ++i) {
        if (next == last) {                             // (on to the next block)
            ++slot;
            next = *slot;
            last = next + DEQUE_BLOCK;
        }
        q.push_back(*next++);
    }
    return q;
}
void QueueToOriginal()
{
    uint8_t *d = o_movement + 0xA4C;
    std::memset(d, 0, 0x30);
    const std::vector<S_RemoteAction> &q = g_queueBefore;
    if (q.empty())
        return;
    // One or two blocks (the queue may cross into the second), in a map of 8 slots from the third on.
    auto **map = static_cast<S_RemoteAction **>(std::calloc(8, sizeof(void *)));
    map[2] = static_cast<S_RemoteAction *>(std::malloc(0xFFC));
    const int at = P(0.3f) ? DEQUE_BLOCK - static_cast<int>(q.size()) + I(0, static_cast<int>(q.size()) - 1) : I(0, 20);
    int block = 2, index = at;
    for (const S_RemoteAction &a : q) {
        if (index == DEQUE_BLOCK) {
            ++block;
            map[block] = static_cast<S_RemoteAction *>(std::malloc(0xFFC));
            index = 0;
        }
        map[block][index++] = a;
    }
    const auto put = [d](int offset, const void *value) { *reinterpret_cast<const void **>(d + offset) = value; };
    put(0x04, map[2]);
    put(0x08, map[2] + DEQUE_BLOCK);
    put(0x0C, map[2] + at);
    put(0x10, &map[2]);
    // (The last iterator: one past the last action; at a block's end, the end of that block, as MSVC6 keeps it.)
    put(0x14, map[block]);
    put(0x18, map[block] + DEQUE_BLOCK);
    put(0x1C, map[block] + index);
    put(0x20, &map[block]);
    put(0x24, map);
    *reinterpret_cast<uint32_t *>(d + 0x28) = 8;
    *reinterpret_cast<uint32_t *>(d + 0x2C) = static_cast<uint32_t>(q.size());
}
void CompareQueues()
{
    const std::vector<S_RemoteAction> o = OriginalQueue(o_movement);
    const std::vector<S_RemoteAction> &p = g_portQueue;
    if (p.size() != o.size()) {
        g_queueDiffers = "queue size " + std::to_string(p.size()) + " / " + std::to_string(o.size());
        return;
    }
    for (size_t i = 0; i < p.size(); ++i)
        if (p[i].time != o[i].time || p[i].action != o[i].action || p[i].phase != o[i].phase)
            g_queueDiffers = "queue action " + std::to_string(i) + ": " + std::to_string(p[i].time) + " " + std::to_string(p[i].action) +
                             " " + std::to_string(p[i].phase) + " / " + std::to_string(o[i].time) + " " + std::to_string(o[i].action) + " " +
                             std::to_string(o[i].phase);
}

typedef void(__attribute__((thiscall)) * T_RefNormalize)(S_HumanAnimRef *);
typedef void(__fastcall * T_RotateY)(float *, int, float);
typedef float(__fastcall * T_Angle2D)(const float *, const float *);
typedef bool(__attribute__((thiscall)) * T_MBool)(void *);
typedef bool(__attribute__((thiscall)) * T_MHeadWater)(void *, bool *, float *);
typedef void(__fastcall * T_SetCamera)(int);
typedef void(__fastcall * T_PlayDubbing)(int, void *, void *, float, float);
typedef int(__attribute__((thiscall)) * T_MPtrInt)(void *, void *);
typedef void(__attribute__((thiscall)) * T_MFloat)(void *, float);
typedef void(__attribute__((thiscall)) * T_MBoolBool)(void *, int, int);
typedef void(__attribute__((thiscall)) * T_MBloodPool)(void *, const float *, const float *, int);

// The helpers on their own: exact.
void TestMoveHelpers()
{
    for (int i = 0; i < 20000 / QUICK; ++i) {
        S_HumanAnimRef a;
        a.anim = P(0.7f) ? I(0x18, 0x44) : I(0, 0xC0);
        a.stance = I(-1, 3);
        a.sub = I(0, 3);
        a.valid = P(0.5f);
        S_HumanAnimRef b = a;
        a.Normalize();
        OriginalAt<T_RefNormalize>(0x00783310)(&b);
        CHECK(std::memcmp(&a, &b, 13) == 0);

        const float angle = P(0.1f) ? 0.0f : U(-7.0f, 7.0f);
        S_vector v(U(-10, 10), U(-10, 10), U(-10, 10));
        alignas(16) float o[4] = {v.x, v.y, v.z, 0.0f};
        RotateAboutY(v, angle);
        OriginalAt<T_RotateY>(0x007888f0)(o, 0, angle);
        CHECK(std::memcmp(&v.x, o, 12) == 0);
        S_vector4 x = {U(-10, 10), U(-10, 10), U(-10, 10), 1.0f};
        alignas(16) float ox[4] = {x.x, x.y, x.z, x.w};
        if (i % 2) {
            RotateAboutX(x, angle);
            OriginalAt<T_RotateY>(0x00788950)(ox, 0, angle);
        } else {
            RotateAboutZ(x, angle);
            OriginalAt<T_RotateY>(0x007889b0)(ox, 0, angle);
        }
        CHECK(std::memcmp(&x.x, ox, 12) == 0);
        const float p[2] = {P(0.05f) ? 0.0f : U(-5, 5), U(-5, 5)}, q[2] = {U(-5, 5), P(0.05f) ? 0.0f : U(-5, 5)};
        const float mine = AngleBetween2D(p, q), theirs = OriginalAt<T_Angle2D>(0x00788a10)(p, q);
        CHECK(std::memcmp(&mine, &theirs, 4) == 0);
    }
    // The camera under water: the original's global (read inline by the locomotion) and the port's.
    for (bool on : {true, false, true}) {
        C_HumanMovement::SetCameraUnderWater(on);
        OriginalAt<T_SetCamera>(0x00786b20)(on);
        CHECK(C_HumanMovement::IsCameraUnderWater() == on);
        CHECK(*OriginalAt<bool *>(O_CAMERA_UNDER_WATER) == on);
    }
    C_HumanMovement::SetCameraUnderWater(false);
}

// A human for part 6: the base's random one, the movement's own fields varied.
void RandomMove()
{
    RandomHumanBase();
    C_HumanMovement *m = g_h.movement;
    C_HumanWork *w = g_h.work;
    m->m_remoteActions.clear();
    m->m_sounds.clear();
    for (int k = P(0.3f) ? I(1, 2) : 0; k > 0; --k)
        m->m_sounds.push_back(static_cast<I3D_sound *>(static_cast<void *>(P(0.5f) ? &g_sound : &g_sound2)));
    for (int k = P(0.7f) ? 0 : I(1, 3); k > 0; --k)
        m->m_remoteActions.push_back({P(0.3f) ? I(-100, 0) : I(1, 2500), I(0, 4), I(0, 3)});
    const int anims[] = {0x6A, 0x6B, 0x6C, 8, 0x0E, 9, 0x12, 0x2D, 0x37, 0x3F, 0x9F, 0x22, 0x6F, 0x9B, 0x9E, 0x1E};
    m->m_animState.anim.anim = P(0.6f) ? anims[I(0, 15)] : I(0, 0xC0);
    m->m_animState.anim.stance = I(0, 2);
    m->m_nextAnimState.anim.anim = P(0.2f) ? anims[I(0, 5)] : I(0, 0xC0);
    w->m_mode = P(0.3f) ? I(2, 5) : I(0, 1);
    w->m_request.stance = I(0, 2);
    m->m_unknown9AC = P(0.6f) ? MOVE_NO_TIME : I(-50, 500);
    m->m_seatMode = P(0.6f) ? 0 : I(0, 10);
    m->m_unknownA30 = P(0.5f) ? MOVE_NO_TIME : I(0, 2);
    m->m_bloodZone = I(0, 6);
    m->m_bloodFrame = P(0.5f) ? g_pelvis : g_footU;
    m->m_shadow = P(0.4f) ? nullptr : g_footQ;
    m->m_visual.m_sector = P(0.3f) ? nullptr : reinterpret_cast<I3D_sector *>(P(0.5f) ? g_sector : g_mv.sector);
    m->m_visual.m_sectorTime = I(0, 1000);
    m->m_useState = P(0.5f) ? MOVE_NO_TIME : I(0, 3);
    m->m_unknown98C = P(0.5f);
    m->m_locomotion.m_waterSurface = P(0.5f) ? MOVE_NO_VALUE : U(-1.0f, 4.0f);
    // (In a seat now and then: a car, burnt out or not, or a static weapon.)
    m->m_vehicle = nullptr;
    g_h.human->m_vehicle = nullptr;
    if (P(0.15f)) {
        // (Its type at the port's own offset and at the original's, +0x18.)
        const E_ActorType type = P(0.7f) ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
        reinterpret_cast<C_Actor *>(g_carBytes)->m_type = type;
        *reinterpret_cast<int *>(g_carBytes + 0x18) = type;
        m->m_vehicle = reinterpret_cast<C_Actor *>(g_carBytes);
        g_h.human->m_vehicle = P(0.8f) ? m->m_vehicle : nullptr;
        m->m_seatMode = I(1, 8);
        g_seatFlags = P(0.5f) ? 0x10 : 0;              // (SEAT_FLAG_HIDDEN)
    }
    g_script.localPlayer = P(0.3f);
    g_script.serverSimulated = P(0.2f);
    g_script.activeIsOwner = P(0.3f);
    g_gameRole = P(0.5f) ? GAME_ROLE_LOCAL : (P(0.5f) ? GAME_ROLE_CLIENT : GAME_ROLE_SERVER);
    g_mv.useLadder = I(-1, 2);
    g_mv.carDestroyed = P(0.5f);
    for (I3D_frame *&f : g_equipmentModel)
        f = nullptr;
    if (P(0.5f))
        g_equipmentModel[EQUIPMENT_PLACE_CARRIED] = g_handsModel;
}

// DiffCall for the movement's own functions (the original's on its movement copy), the queues compared too.
template <class P_, class O_> void DiffMove(const char *what, P_ port, O_ original)
{
    g_queueDiffers.clear();
    DiffCall(what,
             [&] {
                 // (The port's camera-under-water flag, the original's global from the script: ToOriginal.)
                 C_HumanMovement::SetCameraUnderWater(g_script.cameraUnderWater);
                 g_queueBefore = PortQueue();
                 // (Likewise the sound vector: part 4c's hook gives the original a copy of it, from this snapshot.)
                 g_soundsBefore = g_h.movement->m_sounds;
                 // (The model's flags too: the frame is shared, not in the restored bytes; SetOn(true) of a hidden one.)
                 g_frameFlagsBefore = g_h.frame->m_frameFlags;
                 const int r = port();
                 g_portQueue = PortQueue();
                 g_portFrameFlags = g_h.frame->m_frameFlags;
                 return r;
             },
             [&](void *self) {
                 QueueToOriginal();
                 g_h.frame->m_frameFlags = g_frameFlagsBefore;
                 const int r = original(MovementOf(self));
                 CompareQueues();
                 if (g_queueDiffers.empty() && g_h.frame->m_frameFlags != g_portFrameFlags)
                     g_queueDiffers = "model frame flags " + std::to_string(g_portFrameFlags) + " / " +
                                      std::to_string(g_h.frame->m_frameFlags);
                 return r;
             });
    if (!g_queueDiffers.empty()) {
        static int shown;
        if (shown++ < 10)
            std::printf("  %s: %s\n", what, g_queueDiffers.c_str());
    }
    CHECK(g_queueDiffers.empty());
}

void TestMoveSmall()
{
    C_HumanMovement *m = g_h.movement;
    for (int i = 0; i < 30000 / QUICK; ++i) {
        RandomMove();
        switch (i % 15) {
        case 0:
            DiffMove("IsLowered", [&] { return int(m->IsLowered()); },
                     [&](void *self) { return int(OriginalAt<T_MBool>(0x00784d60)(self)); });
            break;
        case 1: {
            bool under = P(0.5f);
            float surface = -7.0f;
            static bool oUnder;
            static float oSurface;
            DiffMove("GetHeadWater", [&] { return int(m->GetHeadWater(&under, &surface)) + 2 * under + 4 * (surface == -7.0f); },
                     [&](void *self) {
                         oUnder = !under;
                         oSurface = -7.0f;
                         const bool r = OriginalAt<T_MHeadWater>(0x00786510)(self, &oUnder, &oSurface);
                         return int(r) + 2 * oUnder + 4 * (oSurface == -7.0f);
                     });
            break;
        }
        case 2: {
            const int dubbing = I(0, 500);
            I3D_frame *frame = P(0.5f) ? g_pelvis : nullptr;
            const float nearRange = U(0, 5), farRange = U(5, 50);
            DiffMove("PlayDubbing", [&] { C_HumanMovement::PlayDubbing(dubbing, frame, g_h.human, nearRange, farRange); return 0; },
                     [&](void *) {
                         OriginalAt<T_PlayDubbing>(0x00786a70)(dubbing, frame, o_human, nearRange, farRange);
                         return 0;
                     });
            break;
        }
        case 3: {
            const int arg = I(-2, 5);
            DiffMove("Unknown7865c0", [&] { m->Unknown7865c0(arg); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x007865c0)(self, arg); return 0; });
            break;
        }
        case 4:
        case 5: {
            C_Actor *object = P(0.15f) ? nullptr : reinterpret_cast<C_Actor *>(g_ladderBytes);
            DiffMove("UseObject", [&] { return m->UseObject(object); },
                     [&](void *self) { return OriginalAt<T_MPtrInt>(0x00786600)(self, object); });
            break;
        }
        case 6: {
            const float value = U(0, 1);
            DiffMove("SetUnknown94", [&] { m->SetUnknown94(value); return 0; },
                     [&](void *self) { OriginalAt<T_MFloat>(0x00786ab0)(self, value); return 0; });
            break;
        }
        case 7:
            DiffMove("UpdateModelNames", [&] { m->UpdateModelNames(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x00786b30)(self); return 0; });
            break;
        case 8:
        case 9: {
            const int request = I(-1, 7);
            DiffMove("OnRemoteAction", [&] { m->OnRemoteAction(request); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x00786b50)(self, request); return 0; });
            break;
        }
        case 10: {
            const bool on = P(0.5f);
            DiffMove("SetPlayerControlled", [&] { m->SetPlayerControlled(on); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x00787530)(self, on); return 0; });
            break;
        }
        case 11: {
            const S_vector pos(U(4, 60), U(1, 4), U(4, 60)), normal(U(-1, 1), U(0, 1), U(-1, 1));
            DiffMove("AddBloodPool", [&] { m->AddBloodPool(pos, normal, 0); return 0; },
                     [&](void *self) { OriginalAt<T_MBloodPool>(0x00787570)(self, &pos.x, &normal.x, 0); return 0; });
            break;
        }
        case 13:
            // The joints turned and moved (kept over the change), held once more or not.
            for (I3D_frame *f : g_jointFrame) {
                S_quat q;
                q.Make(S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(), U(-1.0f, 1.0f));
                f->SetRot(q);
                f->SetPosInline(S_vector(U(-0.5f, 0.5f), U(0, 1.8f), U(-0.5f, 0.5f)));
                f->m_refCount = I(2, 3);
            }
            for (int k = 0; k < HUMAN_JOINT_COUNT; ++k)
                m->m_joints[k].frame = g_jointFrame[k];
            g_h.frame->m_parent = P(0.8f) ? g_sector : g_animRoot;   // (in a seat: not in a sector)
            DiffMove("OnModelChanged", [&] { m->OnModelChanged(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x00787080)(self); return 0; });
            g_h.frame->m_parent = g_sector;
            break;
        case 14:
            DiffMove("ResetToFrame", [&] { m->ResetToFrame(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x0041d800)(self); return 0; });
            break;
        default: {
            const bool volumes = P(0.6f), light = P(0.6f);
            DiffMove("Ragdoll", [&] { m->Ragdoll(volumes, light); return 0; },
                     [&](void *self) { OriginalAt<T_MBoolBool>(0x007872b0)(self, volumes, light); return 0; });
            break;
        }
        }
    }
}

// Part 6b: the per-frame updates. The state they read varied: still or moving bodies, frozen, detached, drawn or not,
// on dynamic ground, the timers about to run out, a carried body, the mixer's root motion, the remote turn target.
void RandomUpdate()
{
    RandomMove();
    C_HumanMovement *m = g_h.movement;
    C_HumanWork *w = g_h.work;
    C_HumanWeaponPose *pose = reinterpret_cast<C_HumanWeaponPose *>(g_h.pose);
    m->m_detached = P(0.05f);
    // (On a ladder, or using one: part 3's ladder actor, each side its own; the locomotion's update moves the body
    // along it.)
    const auto onLadder = [](int a) { return a >= 0x6A && a <= 0x6C; };
    if (onLadder(m->m_animState.anim.anim) || onLadder(m->m_nextAnimState.anim.anim) || m->m_useState != MOVE_NO_TIME)
        m->m_useObject = reinterpret_cast<C_Actor *>(Ladder()->port);
    else
        m->m_useObject = P(0.5f) ? nullptr : reinterpret_cast<C_Actor *>(g_ladderBytes);
    // (A ladder animation on hold: the original passes the mixer an uninitialised vector, the port none: its
    // documented fix in UpdateVelocity. Not met here.)
    if (onLadder(m->m_animState.anim.anim) || onLadder(m->m_nextAnimState.anim.anim))
        m->m_mixer.m_tracks[0].hold = 0;
    m->m_unknown1AC = P(0.15f) ? 0 : P(0.7f) ? 1 : I(2, 3);
    m->m_unknown9C0 = P(0.5f) ? 0 : P(0.5f) ? MOVE_NO_TIME : I(1, 300);
    m->m_inUpdate = P(0.3f);
    m->m_flags = 0;
    if (P(0.15f))
        m->m_flags |= MOVE_FLAG_FROZEN;
    if (P(0.3f))
        m->m_flags |= MOVE_FLAG_NOT_DRAWN;
    if (P(0.2f))
        m->m_flags |= MOVE_FLAG_REMOTE;
    m->m_onDynamicGround = P(0.2f);
    m->m_unknown18C = P(0.6f);
    m->m_stillTime = P(0.3f) ? I(-600, 0) : I(0, 400);
    m->m_parachuting = P(0.1f);
    m->m_unknown9AC = P(0.6f) ? MOVE_NO_TIME : I(-10, 100);
    m->m_unknownA24 = P(0.5f) ? I(0, 1) : I(2, 100);
    m->m_unknownA28 = P(0.5f) ? 0 : I(-5, 100);
    m->m_trailTime = P(0.5f) ? 0 : I(1, 100);
    m->m_timer200 = I(0, 400);
    m->m_timer300 = I(0, 600);
    m->m_timer400 = I(0, 800);
    m->m_timer600 = I(0, 1200);
    m->m_unknown950 = P(0.5f) ? 0 : I(-5, 100);
    m->m_unknown998 = P(0.5f) ? MOVE_NO_TIME : I(-5, 100);
    m->m_footstepTime = I(0, 1200);
    m->m_clock = P(0.05f) ? 0xFFFFFC00u + I(0, 100) : static_cast<uint32_t>(I(0, 100000));
    m->m_unknownDC8 = I(0, 3);
    m->m_damageMaterial = P(0.5f) ? -1 : I(0x40, 0x5E);
    m->m_unknown760 = P(0.3f);
    m->m_unknown764 = P(0.5f) ? 0x46 : I(0, 0x50);
    m->m_animState.activity = P(0.2f) ? I(0x4A, 0x4B) : I(0, 0x60);
    m->m_animPlayer.m_unknown58 = P(0.3f) ? MOVE_NO_TIME : I(-10, 100);
    m->m_mixer.m_weightA = U(0, 1);
    m->m_mixer.m_weightB = P(0.2f) ? m->m_mixer.m_weightA : U(0, 1);
    m->m_mixer.m_velocityA = {U(-3, 3), U(-1, 1), U(-3, 3), 0.0f};
    m->m_mixer.m_velocityB = {U(-3, 3), U(-1, 1), U(-3, 3), 0.0f};
    m->m_locomotion.m_turnTarget.x = P(0.5f) ? MOVE_NO_VALUE : U(-1, 1);
    // (Water anywhere, or over the head: the breath reads the backpack, TestMoveBreath's, which runs first.)
    const float head = m->m_joints[HUMAN_JOINT_F1].frame->GetWorldPos().y;
    m->m_locomotion.m_waterSurface = P(0.6f) ? MOVE_NO_VALUE : P(0.5f) ? head + U(0.1f, 1.0f) : U(-1.0f, 0.5f);
    w->m_request.unknown5C = I(0, 2);
    w->m_request.changed = P(0.5f) ? 0 : I(1, 7);       // (UpdateAnimation clears it)
    pose->m_flags = P(0.5f) ? 0x200 : 0;
    pose->m_ragdollWeight = P(0.3f) ? 0.0f : U(0, 1);
    g_firstPerson = P(0.3f);
    // (The original's UpdatePhysics has IsFirstPersonView inline: the game's +0x5C, its bytes +0x58 / +0x59.)
    g_origFirstPerson[0x58] = g_firstPerson && P(0.5f);
    g_origFirstPerson[0x59] = g_firstPerson && !g_origFirstPerson[0x58];
    *reinterpret_cast<void **>(g_origGame + 0x5C) = g_origFirstPerson;
    g_script.soundPlaying = P(0.5f);
    g_h.frame->m_frameFlags = P(0.8f) ? (g_h.frame->m_frameFlags | 1) : (g_h.frame->m_frameFlags & ~1u);
    g_scanMin[1] = *OriginalAt<float *>(O_SCAN_MIN + 4) = P(0.1f) ? 10.0f : -20.0f;   // (fallen out of the world)
}

void TestMoveUpdates()
{
    C_HumanMovement *m = g_h.movement;
    for (int i = 0; i < 9000 / QUICK; ++i) {
        RandomUpdate();
        const int time = P(0.1f) ? I(200, 700) : I(1, 120);
        g_h.frame->m_parent = P(0.8f) ? g_sector : g_animRoot;   // (in a seat: the hidden model shown by its vehicle)
        switch (i % 3) {
        case 0:
            DiffMove("UpdateAnimation", [&] { m->UpdateAnimation(time); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x007876d0)(self, time); return 0; });
            break;
        case 1:
            DiffMove("UpdatePhysics", [&] { m->UpdatePhysics(time); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x007881b0)(self, time); return 0; });
            break;
        default:
            DiffMove("UpdatePhysicsParts", [&] { m->UpdatePhysicsParts(time); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x00788630)(self, time); return 0; });
            break;
        }
        g_h.frame->m_parent = g_sector;
    }
    g_scanMin[1] = *OriginalAt<float *>(O_SCAN_MIN + 4) = -20.0f;
}

// Part 6c: the lifetime: SetOwner, Init (the mission's climate and variant by its name, the first human's setup),
// Done (the last one's), Reset. The humans of the mission are counted on each side (the port's g_humansInMission and
// the original's 0x0098a6a0 move together: every call runs on both).
const uint32_t O_HUMANS_IN_MISSION = 0x0098a6a0;
const uint32_t O_CAMERA_UNDER_WATER_FLAG = 0x0098a6ac;
const uint32_t O_COLD_MISSION = 0x0098a6ad;
const uint32_t O_MISSION_VARIANT = 0x0098a6b0;
const uint32_t O_FOOTPRINT_BRIGHTNESS = 0x0084f414;
const uint32_t O_UNKNOWN84F418 = 0x0084f418;alignas(16) uint8_t g_origMission[0x40];                 // the original's game's +0x6A: its C_str's text at +8
int g_humans = 0;                                       // the count both sides keep
// The model's SetAnimation (slot 0x84: Reset clears the mixer's eight stages), logged for both sides.
void __stdcall F_SetAnimation(I3D_frame *self, void *anim, int stage, float weight, int flags)
{
    Log(self, "SetAnimation", {double(anim != nullptr), double(stage), weight, double(flags)});
}
void SetMission(const char *name)
{
    GetGame()->m_missionName = name;
    std::memset(g_origMission, 0, sizeof g_origMission);
    std::strncpy(reinterpret_cast<char *>(g_origMission) + 8, name, sizeof g_origMission - 9);
    void *rep = g_origMission;
    std::memcpy(g_origGame + 0x6A, &rep, 4);
}
void CompareMoveGlobals(const char *what)
{
    const bool same = *OriginalAt<int *>(O_HUMANS_IN_MISSION) == g_humans && *OriginalAt<bool *>(O_COLD_MISSION) == g_coldMission &&
                      *OriginalAt<int *>(O_MISSION_VARIANT) == g_missionVariant &&
                      *OriginalAt<float *>(O_FOOTPRINT_BRIGHTNESS) == g_footprintBrightness &&
                      *OriginalAt<bool *>(O_CAMERA_UNDER_WATER_FLAG) == C_HumanMovement::IsCameraUnderWater() &&
                      !*OriginalAt<bool *>(O_UNKNOWN84F418);
    if (!same) {
        static int shown;
        if (shown++ < 10)
            std::printf("  %s: globals differ: humans %d / %d, cold %d / %d, variant %d / %d, footprints %g / %g\n", what, g_humans,
                        *OriginalAt<int *>(O_HUMANS_IN_MISSION), g_coldMission, *OriginalAt<bool *>(O_COLD_MISSION), g_missionVariant,
                        *OriginalAt<int *>(O_MISSION_VARIANT), g_footprintBrightness, *OriginalAt<float *>(O_FOOTPRINT_BRIGHTNESS));
    }
    CHECK(same);
}

void TestMoveLifetime()
{
    C_HumanMovement *m = g_h.movement;
    g_frameVt[0x84 / 4] = reinterpret_cast<void *>(F_SetAnimation);
    static const char *const missions[] = {"Arctic1", "Arctic3", "Africa4", "Arcti9", "Norway1", "Norway2", "Prole", "Ardennes1", "Co_Ar2",
                                           "Brest", "Co_Brest", "Sicily1", "Co_Sicily1", "Sicily2", "Co_Sicily2", "Burma1", "Co_Bu1",
                                           "Czech", "Africa1", "Italy2", "Co_It", "arctic1", "NORWAY3", "Br", "Si"};
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomMove();
        for (int k = 0; k < HUMAN_JOINT_COUNT; ++k)
            m->m_joints[k].frame = g_jointFrame[k];
        m->m_mixer.m_model = reinterpret_cast<I3D_model *>(g_h.frame);
        g_mv.coop = P(0.5f);
        SetMission(missions[I(0, static_cast<int>(sizeof missions / sizeof *missions) - 1)]);
        C_HumanMovement::SetCameraUnderWater(P(0.5f));
        *OriginalAt<bool *>(O_CAMERA_UNDER_WATER_FLAG) = C_HumanMovement::IsCameraUnderWater();
        g_script.cameraUnderWater = C_HumanMovement::IsCameraUnderWater();
        g_h.frame->m_parent = P(0.8f) ? g_sector : g_animRoot;
        switch (i % 4) {
        case 0:
            DiffMove("SetOwner", [&] { m->SetOwner(g_h.human); return 0; },
                     [&](void *self) { OriginalAt<T_MPtrInt>(0x00785f80)(self, o_human); return 0; });
            break;
        case 1:
            // (Done only while there are humans: the first and the last one set up and free the mission's marks.)
            if (g_humans > 0 && P(0.5f)) {
                DiffMove("Done", [&] { m->Done(); return 0; },
                         [&](void *self) { OriginalAt<T_MVoid>(0x00786490)(self); return 0; });
                --g_humans;
            } else {
                DiffMove("Init", [&] { m->Init(); return 0; },
                         [&](void *self) { OriginalAt<T_MVoid>(0x00786040)(self); return 0; });
                ++g_humans;
            }
            CompareMoveGlobals("Init / Done");
            break;
        case 2:
            // (Back to none now and then: the first human's setup again.)
            while (g_humans > 0 && P(0.3f)) {
                DiffMove("Done", [&] { m->Done(); return 0; },
                         [&](void *self) { OriginalAt<T_MVoid>(0x00786490)(self); return 0; });
                --g_humans;
            }
            CompareMoveGlobals("Done");
            break;
        default:
            DiffMove("Reset", [&] { m->Reset(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x00784db0)(self); return 0; });
            break;
        }
        g_h.frame->m_parent = g_sector;
    }
}

// Part 6d: saved games: SaveGame (the movement's chunk: the work, the base, the idle's statics, the parts, the weapon
// pose; the files compared byte for byte), LoadGame (the same file into another state; then AfterLoad: the light,
// the network place, the model kept or not, the burnt-out car, the volumes), the parts' saves failing.
// SaveBase's static spheres (4 of 0x20 bytes: center, radius, padding), written whole: the unused ones keep what an
// earlier save left there (the port writes zeros: cleared before each original save).
const uint32_t O_SAVED_TRAIL = 0x009832c0;
// The actors by index or id, the temporary ones (LoadBase: the vehicle, the object used): logged, none found.
void *__attribute__((thiscall)) OM_GetActor(void *self, int index)
{
    Log(self, "GetActor", {double(index)});
    return index == 0 ? g_actorAtIndex0 : nullptr;
}
void *__attribute__((thiscall)) OM_GetActorById(void *self, int id) { Log(self, "GetActorById", {double(id)}); return nullptr; }
void *__attribute__((thiscall)) OM_FindActor(void *self, int id) { Log(self, "FindTemporaryActor", {double(id)}); return nullptr; }
bool __stdcall OM_IdleSaveStatics(void *) { Log(nullptr, "IdleSaveStatics"); return g_mv.partSaves; }
bool __stdcall OM_IdleLoadStatics(void *) { Log(nullptr, "IdleLoadStatics"); return g_mv.partSaves; }

void TestMoveSaves()
{
    RedirectOriginal(0x00750ae0, reinterpret_cast<const void *>(OM_IdleSaveStatics));
    RedirectOriginal(0x00750af0, reinterpret_cast<const void *>(OM_IdleLoadStatics));
    RedirectOriginal(0x00475dd0, reinterpret_cast<const void *>(OM_GetActor));
    RedirectOriginal(0x00475d50, reinterpret_cast<const void *>(OM_GetActorById));
    RedirectOriginal(0x004772f0, reinterpret_cast<const void *>(OM_FindActor));
    g_names[&g_actorsModule] = g_names[OriginalAt<void *>(0x0089b4b0)] = "actors";
    g_names[&g_temporaryActors] = g_names[OriginalAt<void *>(0x0089b5d0)] = "temporaryActors";
    // (The vehicle saved, the raw car: its id is 0 on both sides; found again by index.)
    g_actorAtIndex0 = reinterpret_cast<C_Actor *>(g_carBytes);
    C_HumanMovement *m = g_h.movement;
    const std::string portFile = TempFile("move_port.chk"), origFile = TempFile("move_orig.chk");
    for (int i = 0; i < 3000 / QUICK; ++i) {
        RandomUpdate();
        m->m_detached = false;
        if (!m->m_vehicle)
            m->m_seatMode = P(0.7f) ? 0 : I(0, 9);
        m->m_interaction = P(0.6f) ? 0 : P(0.5f) ? I(INTERACT_SIT, INTERACT_STAND_UP) : I(1, 0x20);
        m->m_seatFrame = g_jointFrame[I(0, HUMAN_JOINT_COUNT - 1)];
        m->m_bloodFrame = P(0.3f) ? nullptr : g_jointFrame[I(0, HUMAN_JOINT_COUNT - 1)];
        m->m_trailCount = I(0, 4);
        g_mv.partSaves = P(0.9f);
        const bool open = P(0.95f);
        std::remove(portFile.c_str());
        std::remove(origFile.c_str());
        std::memset(OriginalAt<void *>(O_SAVED_TRAIL), 0, 0x80);
        DiffMove("SaveGame",
                 [&] {
                     C_chunk c;
                     if (open)
                         c.Open(portFile.c_str(), true);
                     return int(m->SaveGame(c));
                 },
                 [&](void *self) {
                     C_chunk c;
                     if (open)
                         c.Open(origFile.c_str(), true);
                     return int(OriginalAt<T_Chunk>(0x00784430)(self, &c));
                 });
        const std::vector<uint8_t> savedPort = FileBytes(portFile), savedOrig = FileBytes(origFile);
        if (savedPort != savedOrig) {
            static int shown;
            size_t at = 0;
            while (at < savedPort.size() && at < savedOrig.size() && savedPort[at] == savedOrig[at])
                ++at;
            if (shown++ < 8) {
                std::printf("  SaveGame: files differ at %zu (sizes %zu / %zu); from %zu:", at, savedPort.size(), savedOrig.size(),
                            at > 24 ? at - 24 : 0);
                for (int side = 0; side < 2; ++side) {
                    const std::vector<uint8_t> &f = side ? savedOrig : savedPort;
                    std::printf(side ? "\n    original" : "\n    port    ");
                    for (size_t k = at > 24 ? at - 24 : 0; k < at + 12 && k < f.size(); ++k)
                        std::printf(" %02x", f[k]);
                }
                std::printf("\n");
            }
        }
        CHECK(savedPort == savedOrig);
        CHECK(!open || !g_mv.partSaves || savedPort.size() > 300);   // (the work's save failing: the movement stops there)
        // Loaded back into another state (or nothing open; or the parts failing).
        const bool loadOpen = P(0.97f);
        RandomUpdate();
        m->m_detached = false;
        g_mv.partSaves = P(0.9f);
        g_mv.carDestroyed = P(0.5f);
        g_h.frame->m_parent = P(0.8f) ? g_sector : g_animRoot;
        DiffMove("LoadGame",
                 [&] {
                     C_chunk c;
                     if (loadOpen)
                         c.Open(portFile.c_str(), false);
                     return int(m->LoadGame(c));
                 },
                 [&](void *self) {
                     C_chunk c;
                     if (loadOpen)
                         c.Open(portFile.c_str(), false);
                     return int(OriginalAt<T_Chunk>(0x00784770)(self, &c));
                 });
        g_h.frame->m_parent = g_sector;
    }
    g_mv.partSaves = true;
    g_actorAtIndex0 = nullptr;
}

// Part 6e: the movement sound bank (C_MovementSounds): each group's files in a shuffled order played in turn, each one
// put back at a random place behind the split; a played sound from the group's cache or made new (its file opened, its
// range and volume set); returned sounds cached again (two per file) or released. The port's bank (game/
// human_movement.cpp: the other files' calls into it go to loggers through the linker's --wrap) against the
// original's, whose entries the other tests redirect: restored here (their first five bytes, as in the executable).
const uint8_t kPlayEntry[5] = {0x83, 0xEC, 0x0C, 0x8B, 0x44};   // 0x007856f0 SUB ESP,0xC; MOV EAX,[ESP+..]
const uint8_t kReturnEntry[5] = {0x53, 0x56, 0x8B, 0x74, 0x24}; // 0x00785860 PUSH EBX; PUSH ESI; MOV ESI,[ESP+..]
const uint8_t kBankEntry[5] = {0x6A, 0xFF, 0x68, 0xFB, 0xD6};   // 0x0075ee90 PUSH -1; PUSH handler
extern "C" {
I3D_sound *__attribute__((thiscall)) __real__ZN16C_MovementSounds4PlayEiP9I3D_framePK8S_vector(C_MovementSounds *, int, I3D_frame *,
                                                                                              const S_vector *);
void __attribute__((thiscall)) __real__ZN16C_MovementSounds6ReturnEP9I3D_sound(C_MovementSounds *, I3D_sound *);
}
// The sounds each side makes (the driver's CreateFrame), named by the order they were made in, per side.
bool g_bankOriginalSide = false;
int g_bankMade[2];
I3D_frame *__stdcall F_BankCreateFrame(void *driver, int type)
{
    I3D_frame *frame = F_CreateFrame(driver, type);
    g_names[frame] = std::string("sound") + std::to_string(g_bankMade[g_bankOriginalSide]++);
    return frame;
}
}  // namespace
bool g_openOk = false;                                  // what the sound cache's OpenInto answers
namespace {
// (A failure as 0x80000009: the original then skips its "not found" message, a C_str it builds and drops.)
const HRESULT OPEN_FAILED = static_cast<HRESULT>(0x80000009);
HRESULT __attribute__((thiscall)) OM_OpenInto(void *, void *sound, const char *name, uint32_t, void *, void *, int)
{
    Log(sound, (std::string("SoundCacheOpenInto ") + name).c_str());
    return g_openOk ? I3D_OK : OPEN_FAILED;
}
// The sounds' own calls (the fake frames' vtable: SetSoundType 0x7C, SetRange 0x80, SetVolume 0x8C, SetLoop 0x90,
// Play 0x94), logged by the sound's name.
void __stdcall FB_SetSoundType(I3D_frame *self, int type) { Log(self, "SetSoundType", {double(type)}); }
void __stdcall FB_SetRange(I3D_frame *self, float a, float b, float c, float d) { Log(self, "SetRange", {a, b, c, d}); }
void __stdcall FB_SetVolume(I3D_frame *self, float volume) { Log(self, "SetVolume", {volume}); }
void __stdcall FB_SetLoop(I3D_frame *self, int loop) { Log(self, "SetLoop", {double(loop & 0xFF)}); }
void __stdcall FB_Play(I3D_frame *self, int position) { Log(self, "SoundPlayAt", {double(position)}); }
typedef void *(__attribute__((thiscall)) * T_BankPlay)(void *, int, void *, const S_vector *);
typedef void(__attribute__((thiscall)) * T_BankReturn)(void *, void *);
typedef int(__fastcall * T_BankCount)(int);
// A bank's caches by name (each side's sounds have the same names).
std::string BankCaches(const C_MovementSounds *bank)
{
    std::string s;
    for (int g = 0; g < C_MovementSounds::GROUPS; ++g)
        for (int v = 0; v < C_MovementSounds::GetCount(g); ++v) {
            const C_MovementSounds::S_Cache &c = bank->m_caches[g][v];
            s += std::to_string(c.count);
            for (int k = 0; k < c.count && k < C_MovementSounds::CACHED; ++k)
                s += NameOf(c.sounds[k]);
            s += ";";
        }
    return s;
}

void TestMoveSounds()
{
    uint8_t playPatch[5], returnPatch[5], bankPatch[5];
    std::memcpy(playPatch, OriginalAt<uint8_t *>(0x007856f0), 5);
    std::memcpy(returnPatch, OriginalAt<uint8_t *>(0x00785860), 5);
    std::memcpy(bankPatch, OriginalAt<uint8_t *>(0x0075ee90), 5);
    std::memcpy(OriginalAt<uint8_t *>(0x007856f0), kPlayEntry, 5);
    std::memcpy(OriginalAt<uint8_t *>(0x00785860), kReturnEntry, 5);
    std::memcpy(OriginalAt<uint8_t *>(0x0075ee90), kBankEntry, 5);
    RedirectOriginal(0x0044a1c0, reinterpret_cast<const void *>(OM_OpenInto));   // (called directly by GetSound)
    void *const createFrame = g_driverVt[0x54 / 4];
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_BankCreateFrame);
    g_frameVt[0x7C / 4] = reinterpret_cast<void *>(FB_SetSoundType);
    g_frameVt[0x80 / 4] = reinterpret_cast<void *>(FB_SetRange);
    g_frameVt[0x8C / 4] = reinterpret_cast<void *>(FB_SetVolume);
    g_frameVt[0x90 / 4] = reinterpret_cast<void *>(FB_SetLoop);
    g_frameVt[0x94 / 4] = reinterpret_cast<void *>(FB_Play);

    // The original's bank made now, its decks shuffled from the start of the random numbers, as the port's were (its
    // bank made by the movement's constructor in MakeHuman, before anything else drew one).
    C_MovementSounds *port = g_movementSounds;
    if (std::getenv("HUMAN_TRACE"))
        std::printf("[bank: making the original's]");
    std::srand(1);
    g_mv.soundDriver = false;
    void *orig = OriginalAt<void *(*)()>(0x0075ee90)();
    g_names[port] = g_names[orig] = "bank";
    if (std::getenv("HUMAN_TRACE")) {
        std::printf("[bank %p / %p; built %d;", static_cast<void *>(port), orig, *OriginalAt<uint8_t *>(0x0098a698));
        for (int g = 0; g < C_MovementSounds::GROUPS; ++g)
            std::printf(" %d:%d/%p", g, OriginalAt<int *>(0x0098a640)[g], *OriginalAt<void **>(0x0098a4b0 + 0x14 * g + 4));
        std::printf("]\n");
    }
    for (int g = 0; g < C_MovementSounds::GROUPS; ++g)
        CHECK(C_MovementSounds::GetCount(g) == OriginalAt<T_BankCount>(0x007850a0)(g));

    std::vector<I3D_frame *> live[2];                   // each side's sounds out of the bank
    S_Stats &st = g_stats["MovementSounds"];
    for (int i = 0; i < 20000 / QUICK; ++i) {
        g_mv.soundDriver = P(0.9f);
        g_openOk = P(0.7f);
        const bool play = live[0].empty() || P(0.6f);
        int group = I(0, C_MovementSounds::GROUPS - 1);
        if (group == 16) {
            // (The group without files: the original reads through its null order; the port's fix plays nothing.)
            g_calls.clear();
            CHECK(!__real__ZN16C_MovementSounds4PlayEiP9I3D_framePK8S_vector(g_movementSounds, 16, nullptr, nullptr) && g_calls.empty());
            group = 17;
        }
        if (P(0.02f)) {
            // A sound the bank did not make (its data names no group of it): released.
            std::vector<S_Call> foreign[2];
            for (int side = 0; side < 2; ++side) {
                I3D_frame *f = NewFrame(FRAME_SOUND, "", S_vector(0, 0, 0), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)));
                g_names[f] = "foreign";
                f->SetData(reinterpret_cast<S_FrameData *>(static_cast<uintptr_t>(P(0.5f) ? (25 << 8) : (3 << 8 | 9))));
                f->m_refCount = 2;
                g_calls.clear();
                if (side)
                    OriginalAt<T_BankReturn>(0x00785860)(orig, f);
                else
                    __real__ZN16C_MovementSounds6ReturnEP9I3D_sound(port, reinterpret_cast<I3D_sound *>(f));
                foreign[side] = g_calls;
                foreign[side].push_back({"refs " + std::to_string(f->m_refCount), {}});
            }
            std::string why;
            CHECK(CallsAgree(foreign[0], foreign[1], why));
        }
        I3D_frame *frame = P(0.3f) ? g_jointFrame[I(0, HUMAN_JOINT_COUNT - 1)] : nullptr;
        const S_vector pos(U(-1, 1), U(0, 2), U(-1, 1));
        const size_t which = live[0].empty() ? 0 : static_cast<size_t>(I(0, static_cast<int>(live[0].size()) - 1));
        const unsigned seed = ++g_randSeed;
        std::string results[2];
        std::vector<S_Call> calls[2];
        for (int side = 0; side < 2; ++side) {
            if (std::getenv("HUMAN_TRACE"))
                std::printf("[bank %d %s %d side %d]", i, play ? "play" : "return", group, side);
            g_bankOriginalSide = side == 1;
            g_calls.clear();
            std::srand(seed);
            if (play) {
                void *sound = side ? OriginalAt<T_BankPlay>(0x007856f0)(orig, group, frame, &pos)
                                   : __real__ZN16C_MovementSounds4PlayEiP9I3D_framePK8S_vector(port, group, frame, &pos);
                results[side] = NameOf(sound);
                if (sound) {
                    const I3D_frame *f = static_cast<const I3D_frame *>(sound);
                    results[side] += " data " + std::to_string(reinterpret_cast<uintptr_t>(f->GetData())) + " on " +
                                     std::to_string(f->m_frameFlags & 1) + " under " + NameOf(f->m_parent);
                    live[side].push_back(static_cast<I3D_frame *>(sound));
                }
            } else {
                I3D_frame *sound = live[side][which];
                live[side].erase(live[side].begin() + static_cast<long>(which));
                if (side)
                    OriginalAt<T_BankReturn>(0x00785860)(orig, sound);
                else
                    __real__ZN16C_MovementSounds6ReturnEP9I3D_sound(port, reinterpret_cast<I3D_sound *>(sound));
                results[side] = NameOf(sound) + " on " + std::to_string(sound->m_frameFlags & 1) + " under " + NameOf(sound->m_parent);
            }
            calls[side] = g_calls;
            results[side] += " caches " + BankCaches(side ? static_cast<C_MovementSounds *>(orig) : port);
        }
        g_bankOriginalSide = false;
        std::string why;
        if (results[0] != results[1])
            why = "result: port " + results[0].substr(0, 120) + " / original " + results[1].substr(0, 120);
        else if (!CallsAgree(calls[0], calls[1], why))
            why = "calls: " + why;
        ++st.calls;
        if (why.empty()) {
            ++st.agree;
        } else {
            ++st.differ[play ? "Play" : "Return"];
            static int shown;
            if (shown++ < 10)
                std::printf("  MovementSounds %s differs: %s\n", play ? "Play" : "Return", why.c_str());
        }
        CHECK(why.empty());
    }
    // Back as the other tests had it.
    g_driverVt[0x54 / 4] = createFrame;
    g_mv.soundDriver = false;
    std::memcpy(OriginalAt<uint8_t *>(0x007856f0), playPatch, 5);
    std::memcpy(OriginalAt<uint8_t *>(0x00785860), returnPatch, 5);
    std::memcpy(OriginalAt<uint8_t *>(0x0075ee90), bankPatch, 5);
}

// The movement base's UpdateBreath under water (and UpdateAnimation's with the head under it, later): the backpack with an aqualung
// (or another item, or none); the breath held, running out, drowning. The backpack is a fake container (IsEmpty at
// the original's slot 0x38, GetItem at 0x40, and the port's own slots); the item a raw one (its type at +0x0C). The
// original's copy protection test compares two tables of its image that the test leaves zero: intact, as the port's.
alignas(16) uint8_t g_backpack[0x40], g_backpackItem[0x40];
void *g_backpackVt[64];
C_ItemContainer *g_containers[16];
bool g_backpackEmpty = true;
bool __attribute__((thiscall)) F_BackpackIsEmpty(void *) { return g_backpackEmpty; }
void *__attribute__((thiscall)) F_BackpackGetItem(void *) { return g_backpackItem; }
typedef void(__attribute__((thiscall)) * T_MBreath)(void *, int, int);
void __attribute__((thiscall)) OM_PlayWorkAnimation(void *self, int animation, int on)
{
    Log(self, "PlayWorkAnimation", {double(animation), double(on & 0xFF)});
}

void TestMoveBreath()
{
    for (void *&slot : g_backpackVt)
        slot = reinterpret_cast<void *>(F_GEffectsUnexpected);
    g_backpackVt[0x38 / 4] = g_backpackVt[VSlot(&C_ItemContainer::IsEmpty)] = reinterpret_cast<void *>(F_BackpackIsEmpty);
    g_backpackVt[0x40 / 4] = g_backpackVt[VSlot(&C_ItemContainer::GetItem)] = reinterpret_cast<void *>(F_BackpackGetItem);
    *reinterpret_cast<void ***>(g_backpack) = g_backpackVt;
    RedirectOriginal(0x0077f4b0, reinterpret_cast<const void *>(OM_PlayWorkAnimation));
    g_containers[ITEM_CONTAINER_BACKPACK] = reinterpret_cast<C_ItemContainer *>(g_backpack);
    C_HumanMovement *m = g_h.movement;
    // (Kept for the later tests: the updates under water read it.)
    g_h.human->m_containers = g_containers;
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomMove();
        g_backpackEmpty = P(0.4f);
        *reinterpret_cast<int *>(g_backpackItem + 0x0C) = P(0.6f) ? 0xE7 : I(0, 0x100);   // (0xE7: the aqualung)
        m->m_animState.activity = P(0.4f) ? 0x42 : I(0, 0x60);                              // (0x42: drowning)
        m->m_breathTime = P(0.3f) ? I(0, 9000) : P(0.5f) ? I(25000, 31000) : I(29000, 60000);
        m->m_gaspTime = I(-500, 3000);
        g_h.human->m_stats[STAT_HEALTH] = P(0.2f) ? I(-5, 0) : I(1, 100);
        const int time = I(1, 200);
        const bool under = P(0.8f);
        DiffMove("UpdateBreath", [&] { m->UpdateBreath(time, under); return 0; },
                 [&](void *self) { OriginalAt<T_MBreath>(0x0075f040)(self, time, under); return 0; });
    }
}

void TestMove()
{
    SetUpMove();
    TestMoveHelpers();
    TestMoveSmall();
    TestMoveBreath();
    TestMoveUpdates();
    TestMoveLifetime();
    TestMoveSaves();
    TestMoveSounds();
}
}  // namespace
