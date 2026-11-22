// Human part 82: the port symbols melee_tracker.cpp and melee_tracker_ai.cpp reach that part 82a's operations never
// call (the mission life cycle, save / load, the controllers' own code): a call ends the test.
#include "game/camera_module.h"
#include "game/checkpoints.h"
#include "game/controller_messages.h"
#include "game/geffects.h"
#include "game/game_texts.h"
#include "script/script.h"

namespace H {
[[noreturn]] void Unexpected(const char *what)
{
    std::printf("mt_diff: unexpected call: %s\n", what);
    std::exit(3);
}
}  // namespace H
#define UNEXPECTED H::Unexpected(__func__)

bool __fastcall ReadControlState(C_NetMessage *, float *, float *, float *, void *, uint32_t *) { UNEXPECTED; }
bool __fastcall WriteControlState(C_NetMessage *, const float *, const float *, const float *, const void *, uint32_t) { UNEXPECTED; }
int ActorToId(const C_Actor *) { UNEXPECTED; }
S_HumanEvent *AllocHumanEvent() { UNEXPECTED; }
void C_Actor::Activate(bool) { UNEXPECTED; }
C_Actor::C_Actor(E_ActorType) { UNEXPECTED; }
void C_Actor::CopyFrom(const C_Actor *) { UNEXPECTED; }
void C_Actor::Deactivate(bool) { UNEXPECTED; }
void C_Actor::Done() { UNEXPECTED; }
void C_Actor::GetBoxCorners(S_vector4 *) { UNEXPECTED; }
int C_Actor::GetSeatAmmoCount(int) { UNEXPECTED; }
void C_Actor::Init() { UNEXPECTED; }
bool C_Actor::LoadGame(C_chunk &) { UNEXPECTED; }
bool C_Actor::SaveGame(C_chunk &) { UNEXPECTED; }
bool C_Actor::SetFrame(I3D_frame *) { UNEXPECTED; }
C_Actor::~C_Actor() {}
C_Actor *C_ActorsModule::GetActor(int) const { UNEXPECTED; }
bool C_CameraModule::GetUnknown78() { UNEXPECTED; }
void C_CameraModule::SetViewDir(const S_vector4 &) { UNEXPECTED; }
bool C_CheckpointNetwork::FindMeleeTrackerPath(int, int, std::vector<uint16_t> *) { UNEXPECTED; }
int C_CheckpointNetwork::FindNearest(S_vector, float, float) const { UNEXPECTED; }
S_Checkpoint *C_CheckpointNetwork::Get(int) { UNEXPECTED; }
C_str C_GameTexts::Get(int) const { UNEXPECTED; }
int C_Human::GetStance() const { UNEXPECTED; }
void C_Human::RaiseEvent(S_HumanEvent *) { UNEXPECTED; }
bool C_MeleeTrackerAI::RouteAroundCars() { UNEXPECTED; }
void C_MeleeTrackerMovement::Bind(I3D_model *, C_MeleeTracker *) { UNEXPECTED; }
void C_MeleeTrackerMovement::CheckAttackSequence() { UNEXPECTED; }
void C_MeleeTrackerMovement::FreeVolumesAndSounds() { UNEXPECTED; }
int C_MeleeTrackerMovement::GetSeqAnimC() { UNEXPECTED; }
bool C_MeleeTrackerMovement::LoadGame(C_chunk &) { UNEXPECTED; }
void C_MeleeTrackerMovement::Reset() { UNEXPECTED; }
bool C_MeleeTrackerMovement::SaveGame(C_chunk &) { UNEXPECTED; }
void C_MeleeTrackerMovement::SetFacing(const float *) { UNEXPECTED; }
void C_MeleeTrackerMovement::SetOptimizedController(bool) { UNEXPECTED; }
void C_MeleeTrackerMovement::SetSubModeB(int, int) { UNEXPECTED; }
template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *, I3D_animation_set **, T_CacheErrorProc, void *) { UNEXPECTED; }
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *, const char *, uint32_t, void *, void *, int) { UNEXPECTED; }
void C_Script::Advance() { UNEXPECTED; }
void C_Script::OnAlarm() { UNEXPECTED; }
int C_SightModule::AddObserver(I3D_frame *, uint32_t) { UNEXPECTED; }
int C_SightModule::AddTarget(I3D_frame *, uint32_t) { UNEXPECTED; }
void C_SightModule::SetTargetState(int, int) { UNEXPECTED; }
int C_SoundModule::AddListener(I3D_frame *, T_SoundHeardProc) { UNEXPECTED; }
int C_SoundModule::Play(const S_SoundRequest &) { UNEXPECTED; }
C_CameraModule *C_game::GetCameraModule() { UNEXPECTED; }
C_BitStream *CreateActorMessageEx(int, bool) { UNEXPECTED; }
C_GEffectsModule *GetGEffects() { UNEXPECTED; }
uint32_t MeleeTrackerBarkTurn(C_MeleeTracker *) { UNEXPECTED; }
float __stdcall S_vector::AngleTo(const S_vector &) const { UNEXPECTED; }
void SendActorMessage(C_BitStream *) { UNEXPECTED; }
int ShowMessage(const char *, const char *, unsigned int) { UNEXPECTED; }
int RW_DATA_CALL dtaOpen(const char *, bool) { UNEXPECTED; }
int RW_DATA_CALL dtaOpenWrite(const char *, int) { UNEXPECTED; }
int RW_DATA_CALL dtaSeek(int, int, int) { UNEXPECTED; }
int RW_DATA_CALL dtaRead(int, void *, int) { UNEXPECTED; }
int RW_DATA_CALL dtaWrite(int, const void *, int) { UNEXPECTED; }
bool RW_DATA_CALL dtaClose(int) { UNEXPECTED; }
