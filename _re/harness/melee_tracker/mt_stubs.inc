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
S_HumanEvent *AllocHumanEvent() { UNEXPECTED; }
C_Actor::C_Actor(E_ActorType) { UNEXPECTED; }
void C_Actor::Done() { UNEXPECTED; }
void C_Actor::GetBoxCorners(S_vector4 *) { UNEXPECTED; }
int C_Actor::GetSeatAmmoCount(int) { UNEXPECTED; }
void C_Actor::Init() { UNEXPECTED; }
C_Actor::~C_Actor() { H::Log("%s actor destructor", H::Name(this).c_str()); }   // (part 82b: the destructor's base)
bool C_CameraModule::GetUnknown78() { UNEXPECTED; }
void C_CameraModule::SetViewDir(const S_vector4 &dir) { H::Log("camera SetViewDir %s", H::VecText(S_vector(dir.x, dir.y, dir.z)).c_str()); }
bool C_CheckpointNetwork::FindMeleeTrackerPath(int, int, std::vector<uint16_t> *) { UNEXPECTED; }
int C_CheckpointNetwork::FindNearest(S_vector, float, float) const { UNEXPECTED; }
S_Checkpoint *C_CheckpointNetwork::Get(int) { UNEXPECTED; }
C_str C_GameTexts::Get(int) const { UNEXPECTED; }
int C_Human::GetStance() const { UNEXPECTED; }
void C_Human::RaiseEvent(S_HumanEvent *) { UNEXPECTED; }
bool C_MeleeTrackerAI::RouteAroundCars() { UNEXPECTED; }
void C_MeleeTrackerMovement::CheckAttackSequence() { UNEXPECTED; }
int C_MeleeTrackerMovement::GetSeqAnimC() { UNEXPECTED; }
void C_MeleeTrackerMovement::SetFacing(const float *) { UNEXPECTED; }
void C_MeleeTrackerMovement::SetOptimizedController(bool) { UNEXPECTED; }
void C_MeleeTrackerMovement::SetSubModeB(int, int) { UNEXPECTED; }
template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *, I3D_animation_set **, T_CacheErrorProc, void *) { UNEXPECTED; }
void C_Script::Advance() { UNEXPECTED; }
void C_Script::OnAlarm() { UNEXPECTED; }
int C_SoundModule::Play(const S_SoundRequest &) { UNEXPECTED; }
C_CameraModule *C_game::GetCameraModule() { return reinterpret_cast<C_CameraModule *>(H::g_cameraObj); }
C_BitStream *CreateActorMessageEx(int, bool) { UNEXPECTED; }
uint32_t MeleeTrackerBarkTurn(C_MeleeTracker *) { UNEXPECTED; }
float __stdcall S_vector::AngleTo(const S_vector &) const { UNEXPECTED; }
void SendActorMessage(C_BitStream *) { UNEXPECTED; }
int ShowMessage(const char *, const char *, unsigned int) { UNEXPECTED; }
