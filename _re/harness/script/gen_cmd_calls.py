"""Writes cmd_calls.s and cmd_calls.inc for the game command test (cmd_diff.cpp, Script part 3).

cmd_calls.s: for every game function the port's interpreter calls that the test does not link (the link's undefined
references), a stub that enters the shared logger (CmdCallEntry: pushes the call's index and side, the logger decodes
the arguments, logs, answers and pops what the callee would); a variable's stub is 64 KB of zeros.
cmd_calls.inc: the calls' table (name, the original's address, how each side passes the arguments, the argument kinds,
the answer's kind) and the engine's virtual slots (src/ls3d/i3d.h: every I3DMETHOD with its offset), which both sides
call alike (__stdcall, `this` on the stack).

Run it when build_cmd.bat's link reports an undefined reference (the port's symbols changed): python gen_cmd_calls.py,
then build_cmd.bat. Script parts 6 and 7 (mod_diff.cpp, the script module): python gen_cmd_calls.py mod writes
mod_calls.s and mod_calls.inc from build_mod.bat's link. A function the table below does not know stops it: add it (the original's address from the port's
header comments or Ghidra, its calling convention there, the answer's kind).

Argument kinds: i int (and enums, unsigned), b bool (the low byte), f float, s a string (char *), p a pointer (logged
as the object it names), v a vector (S_vector &: 3 floats), m a matrix (S_matrix &: 16 floats), q a quaternion
(S_quat &), r a raw word, h an unsigned short (the low half), o a vector the callee fills (std::vector<unsigned short> *),
c a human command (S_HumanCommand &: its fields), . the rest of a variadic call; an S_vector by value is three f. Answers:
v none, i int, b bool, p pointer, f float (ST0)."""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MINGW = r"C:\nim-2.2.6\dist\mingw32\bin"
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "..", "src"))

# The original's side of each call: address, calling convention ('thiscall', 'stdcall', 'cdecl', 'fastcall'; a member
# called through a vtable there: 'vt+0xNN' on the object), the answer's kind. None: the test provides the function
# itself (not logged).
ORIG = {
    'GetGEffects()': ("0x00486510", "stdcall", "p"),
    'SetFrameData(I3D_frame*, S_FrameData const&)': ("0x006e9ea0", "fastcall", "v"),   # (ECX the frame, EDX the data)
    'GetSoundModule()': ("0x004336c0", "stdcall", "p"),
    'EnableSpawnZone(char const*, int)': ("0x0069f320", "stdcall", "b"),
    # (A C_NetGame member there, its `this` unused: RET 4, the stdcall shape.)
    'CanPlayerRespawn(int)': ("0x00730970", "stdcall", "b"),
    'GetLightMapGroups()': ("0x0048cb00", "stdcall", "p"),
    'SetObjectiveStatus(int, int)': ("0x006b6830", "stdcall", "v"),
    'SendScriptNetMessage(C_BitStream*)': ("0x00424580", "fastcall", "v"),
    'BeginScriptNetMessage(int, bool)': ("0x00424540", "fastcall", "p"),
    'C_AirAttack::SetTarget(bool, I3D_frame*)': ("0x005e7570", "thiscall", "v"),
    'C_BitStream::WriteString(char const*)': ("0x0073d440", "thiscall", "v"),
    'C_BitStream::WriteBits(unsigned int, int)': ("0x0073d320", "thiscall", "v"),
    'C_Interface::SetTimer(int)': ("0x0049dde0", "thiscall", "v"),
    'C_ItemActor::TeleportTo(I3D_frame*)': ("0x005b7920", "thiscall", "v"),
    'C_Junkers88::SetState(int)': ("0x005f0a90", "thiscall", "v"),
    'C_SupplyBox::SetEnabled(bool)': ("0x005fa290", "thiscall", "v"),
    'S_Explosion::Execute()': ("0x006df540", "thiscall", "v"),
    'C_Debriefing::AddText(int)': ("0x006a6c10", "thiscall", "v"),
    'C_Decorators::SetOn(bool)': ("0x0047b010", "thiscall", "v"),
    'C_GameProfile::IsLanguage(char const*)': ("0x006b6a70", "thiscall", "b"),
    'C_GameProfile::GetObjectiveStatus(int, int, int, int)': ("0x006b6820", "thiscall", "i"),
    'C_SoundModule::CrossfadeMusic(int, int, int)': ("0x00436f10", "vt+0x6C", "v"),
    'C_SoundModule::GetDubbingSampleSize(int)': ("0x004326c0", "vt+0x94", "i"),
    'C_SoundModule::Play(S_SoundRequest const&)': ("0x00433fa0", "vt+0x54", "i"),
    'C_SoundModule::PlayMusic(int)': ("0x00436f60", "vt+0x68", "v"),
    'C_TSExplosion::Trigger(int)': ("0x0060fb10", "thiscall", "v"),
    'C_TeamManager::GetFormation(C_Human*, bool)': ("0x00461290", "thiscall", "p"),
    'C_TeamManager::DestroyFormation(C_Formation*)': ("0x00461390", "thiscall", "v"),
    'C_TeamManager::RemoveFromFormation(C_Human*)': ("0x00461240", "thiscall", "b"),
    'C_TrackModule::CreatePlayer(char const*, I3D_frame*, I3D_frame*)': ("0x00401b20", "thiscall", "p"),
    'C_CameraModule::IsGameCamera(I3D_frame*)': ("0x00465ee0", "vt+0x4C", "b"),
    'C_CameraModule::GetState()': ("0x00465070", "vt+0x7C", "b"),
    'C_CameraModule::SetTarget(I3D_frame*, int)': ("0x00465b20", "vt+0x74", "v"),
    'C_MeleeTracker::SetOptimized(bool)': ("0x005d5870", "thiscall", "v"),
    'C_MeleeTracker::ResetAI()': ("0x0041d790", "thiscall", "v"),
    'C_ScriptModule::EndCutscene()': ("0x00423eb0", "thiscall", "v"),
    'C_ScriptModule::RotateFrame(I3D_frame*, int, int, int)': ("0x004274a0", "thiscall", "v"),
    'C_ScriptModule::RunCutscene(int)': ("0x00423c70", "thiscall", "v"),
    'C_ScriptModule::AssignScript(I3D_frame*, char const*)': ("0x00427730", "thiscall", "b"),
    'C_ScriptModule::RestoreFrame(I3D_frame*)': ("0x00423b40", "thiscall", "v"),
    'C_ScriptModule::AddPauseSound(char const*)': ("0x00428d10", "thiscall", "v"),
    'C_ScriptModule::RememberFrame(I3D_frame*, unsigned int)': ("0x00423670", "thiscall", "v"),
    'C_ScriptModule::RegisterDuplicate(I3D_frame*)': ("0x00424420", "thiscall", "b"),
    'C_ScriptModule::RegisterSoundFrame(I3D_frame*)': ("0x00423980", "thiscall", "v"),
    'C_ScriptModule::UnregisterDuplicate(I3D_frame*)': ("0x00424490", "thiscall", "b"),
    'C_ScriptModule::RegisterAnimatedModel(I3D_frame*)': ("0x00424260", "thiscall", "b"),
    'C_ScriptModule::UnregisterAnimatedModel(I3D_frame*)': ("0x00424310", "thiscall", "b"),
    'C_StaticWeapon::SetState(int)': ("0x006059b0", "thiscall", "v"),
    'C_TShiftModule::GetEntry(I3D_mesh const*)': ("0x0048e780", "thiscall", "p"),
    'C_DocumentPlace::SetState(int)': ("0x0060eac0", "thiscall", "v"),
    'C_FowlGenerator::Start(bool, int)': ("0x005bb140", "thiscall", "v"),
    'C_HumanMovement::ResetToFrame()': ("0x0041d800", "thiscall", "v"),
    'C_PlayerProfile::GetDiary()': ("0x006ae7c0", "thiscall", "p"),
    'C_ResourceCache<I3D_animation_set>::Get(char const*, I3D_animation_set**, void ( fastcall*)(char const*, void*), void*)':
        ("0x0041fc10", "thiscall", "i"),
    'C_AnimationActor::Stop(bool)': ("0x005b55e0", "thiscall", "v"),
    'C_ExplosivePlace::SetState(int)': ("0x0060d300", "thiscall", "v"),
    'C_FormationShape::SetType(E_FormationType)': ("0x00597420", "thiscall", "v"),
    'C_FormationShape::SetScale(S_vector const&)': ("0x005977f0", "thiscall", "v"),
    'C_ParticlesModule::Create(int, S_matrix const&, I3D_frame*, bool, bool)': ("0x004451b0", "thiscall", "i"),
    'C_ParticlesModule::Finish(int)': ("0x004458d0", "thiscall", "v"),
    'C_ParticlesModule::Destroy(int)': ("0x00445800", "thiscall", "v"),
    'C_CheckpointNetwork::Get(int)': ("0x0051d5a0", "thiscall", "p"),
    'C_CheckpointNetwork::Join(char const*, char const*)': ("0x0051f490", "thiscall", "b"),
    'C_CheckpointNetwork::Separate(char const*, char const*)': ("0x0051f5a0", "thiscall", "b"),
    'C_TemporaryActorsList::CreateDebris(I3D_frame*, S_vector const&, S_vector const&)': ("0x00479450", "thiscall", "p"),
    'C_MeleeTrackerMovement::SetOptimizedController(bool)': ("0x005c86c0", "thiscall", "v"),
    'C_Car::SendFuel()': ("0x004db310", "thiscall", "v"),
    'C_Door::Lock(int)': ("0x005ff650", "thiscall", "b"),
    'C_Door::Unlock(int)': ("0x005ff5f0", "thiscall", "b"),
    'C_Door::SetState(int, bool, bool, int)': ("0x005ff4b0", "thiscall", "v"),
    'C_Item::GetFrame(int)': ("0x007d3f00", "thiscall", "p"),
    'C_Pike::SetState(int)': ("0x005b8ea0", "thiscall", "v"),
    'C_Team::SetNPCMember(C_Human*, bool)': ("0x0045d100", "thiscall", "v"),
    'C_Team::ReturnControl()': ("0x0045da40", "thiscall", "v"),
    'C_game::RestoreMusic(bool)': ("0x0069ec40", "thiscall", "v"),
    'C_game::GetCameraModule()': ("0x0069a620", "thiscall", "p"),
    'C_game::ShowBinocularsView(bool)': ("0x0069a7a0", "thiscall", "v"),
    'C_game::ShowFirstPersonView(bool)': ("0x0069a770", "thiscall", "v"),
    'C_game::Print(char const*, ...)': ("0x0069c2b0", "cdecl", "v"),
    'C_game::PlayAvi(char const*)': ("0x0069e8b0", "thiscall", "v"),
    'C_Diary::AddText(int)': ("0x006a7ad0", "thiscall", "v"),
    'C_Human::SetTexture(char const*)': ("0x005a87c0", "thiscall", "v"),
    'C_Human::SetAlarmType(unsigned int, bool)': ("0x005af150", "thiscall", "v"),
    'C_Human::SetAIOptimized(int)': ("0x005af5c0", "thiscall", "v"),
    'C_Human::SetHealthPercent(int)': ("0x005a6060", "thiscall", "v"),
    'C_Human::RemoveItemsFromStore(int)': ("0x005a7b30", "thiscall", "v"),
    'C_Human::ResetAI()': ("0x005af450", "thiscall", "v"),
    'C_Human::Suspend(bool)': ("0x005af570", "thiscall", "v"),
    'C_Radio::SetState(int)': ("0x0060b570", "thiscall", "v"),
    'C_Train::FollowTrack(char const*)': ("0x005bd490", "thiscall", "v"),
    'C_Train::Start()': ("0x005bc970", "thiscall", "v"),
    'C_Debris::Setup(S_RigidBodyParams const&, bool)': ("0x005fdc50", "thiscall", "v"),
    'C_Switch::SetState(int)': ("0x006136f0", "thiscall", "v"),
    'C_Vehicle::Reset(unsigned char)': ("0x004e95b0", "thiscall", "v"),
    'C_Vehicle::Restart(float, bool)': ("0x004e9db0", "thiscall", "v"),
    'C_Vehicle::SetFuel(float)': ("0x004f2670", "thiscall", "b"),
    'C_Vehicle::SetEngine(bool, bool)': ("0x004f2530", "thiscall", "b"),
    'C_WireCut::Cut(S_vector const&, bool)': ("0x005e2510", "thiscall", "b"),
    'C_TeamBase::GetAliveCount() const': ("0x004622c0", "thiscall", "i"),
    'C_TeamBase::GetMemberCount() const': ("0x00462300", "thiscall", "i"),
    'C_TeamBase::GetMember(int) const': ("0x004619f0", "thiscall", "p"),
    'C_GameProfile::GetDifficulty() const': ("0x006b6a20", "thiscall", "i"),
    'C_GameProfile::IsCooperativeGame() const': ("0x006b69d0", "thiscall", "b"),
    'C_ActorsModule::GetActorById(int) const': ("0x00475d50", "thiscall", "p"),
    'C_ActorsModule::GetActor(int) const': ("0x00475dd0", "thiscall", "p"),
    'C_TShiftModule::HasFrame(I3D_frame const*) const': ("0x0048e2d0", "thiscall", "b"),
    'C_CheckpointNetwork::Find(char const*) const': ("0x0051e1a0", "thiscall", "i"),
    'C_TemporaryActorsList::IsItemInRange(int, S_vector const&, int) const': ("0x00478a20", "thiscall", "b"),
    'C_LightMapGroupsModule::FindGroup(char const*) const': ("0x0041d5e0", "thiscall", "p"),
    'C_Car::IsDestroyed() const': ("0x004ddcf0", "thiscall", "b"),
    'C_Team::GetActivePlayer() const': ("0x0045d1d0", "thiscall", "p"),
    'C_Team::HasItem(int) const': ("0x0045e9a0", "thiscall", "b"),
    'C_game::IsBinocularsView() const': ("0x0069a7b0", "thiscall", "b"),
    'C_game::IsFirstPersonView() const': ("0x0069a780", "thiscall", "b"),
    'C_Human::SeesFriend() const': ("0x005af640", "thiscall", "b"),
    'C_Human::GetAlarmType() const': ("0x005af170", "thiscall", "i"),
    'C_Human::IsSpectating() const': ("0x005a63f0", "thiscall", "b"),
    'C_Human::IsUnconscious() const': ("0x005a6380", "thiscall", "b"),
    'C_Human::GetHealthPercent() const': ("0x005a5ff0", "thiscall", "i"),
    'C_Human::GetControllerType() const': ("0x005a5fe0", "thiscall", "i"),
    'C_Human::GetTeam(bool) const': ("0x005a8820", "thiscall", "i"),
    'C_Human::FindItem(int, int*) const': ("0x005a78f0", "thiscall", "p"),
    'C_Human::IsInPlay() const': ("0x005a63b0", "thiscall", "b"),
    'C_Human::SeesEnemy() const': ("0x005af620", "thiscall", "b"),
    'C_WireCut::IsCut() const': ("0x005e24c0", "thiscall", "b"),
    # (RandFloat: an import of the engine's on the original's side; its thunk is patched.)
    'RandFloat': ("0x007e5302", "cdecl", "f"),
    # Script part 4: the actor scripts' (actor_scripts.cpp; C_HumanScript / C_MeleeTrackerScript::ExecuteCommand).
    'C_HumanWork::SetWorkingFrame(I3D_frame*, bool)': ("0x0077fb70", "thiscall", "v"),
    'C_AIController::Drive(int, float, int)': ("0x00577a60", "thiscall", "v"),
    'C_Bike::Drive(int, float, I3D_frame*)': ("0x005d8f60", "thiscall", "v"),
    'C_MeleeTrackerAI::MoveTo(unsigned short)': ("0x005cf700", "thiscall", "v"),
    'C_MeleeTrackerAI::Attack(I3D_frame*)': ("0x005cf7c0", "thiscall", "v"),
    'C_MeleeTrackerAI::SetModeRun()': ("0x005cf820", "thiscall", "v"),
    'C_MeleeTrackerAI::SetModeWalk()': ("0x005cf830", "thiscall", "v"),
    'C_MeleeTrackerAI::SetModeLie()': ("0x005cf840", "thiscall", "v"),
    'C_MeleeTrackerAI::SetModeStand()': ("0x005cf850", "thiscall", "v"),
    'C_MeleeTrackerAI::SetAggressive()': ("0x005cf890", "thiscall", "v"),
    'C_MeleeTrackerAI::SetDefensive()': ("0x005cf8a0", "thiscall", "v"),
    'C_MeleeTrackerAI::Suspend(bool)': ("0x005cf930", "thiscall", "v"),
    'C_MeleeTrackerAI::SetOptimized(bool)': ("0x005cf950", "thiscall", "v"),
    'C_MeleeTrackerAI::SetSightRange(int)': ("0x005cf980", "thiscall", "v"),
    'C_MeleeTrackerAI::SetHearRange(int)': ("0x005cf990", "thiscall", "v"),
    'C_MeleeTrackerAI::SetOwner(I3D_frame*)': ("0x005cf9b0", "thiscall", "v"),
    'C_MeleeTrackerScriptedAI::SetBarking(bool)': ("0x005cfbb0", "thiscall", "i"),
    'C_CheckpointNetwork::GetReachable(S_Checkpoint const*, float, std::vector<unsigned short, std::allocator<unsigned short> >*)':
        ("0x00520860", "thiscall", "b"),
    'C_CheckpointNetwork::GetNeighbours(int, float, std::vector<unsigned short, std::allocator<unsigned short> >*)':
        ("0x00520830", "thiscall", "b"),
    'C_CheckpointNetwork::FindNearest(S_vector, float, float) const': ("0x0051d780", "thiscall", "i"),
    'C_Human::FindCheckpoint(char const*) const': ("0x005af210", "thiscall", "i"),
    'C_Human::IssueCommand(S_HumanCommand const&)': ("0x005a45b0", "thiscall", "v"),
    'C_Human::SetControllerAIMode(int)': ("0x005a5ca0", "thiscall", "v"),
    'C_Human::CreateItemInStore(int, int)': ("0x005af760", "thiscall", "v"),
    'C_Human::SetAlarm(bool)': ("0x005af190", "thiscall", "v"),
    'C_Human::MoveTo(int, int)': ("0x005af1d0", "thiscall", "v"),
    'C_Human::AttackMoveTo(int, int)': ("0x005af1b0", "thiscall", "v"),
    'C_Human::AttackActor(C_Actor*, int, bool, int)': ("0x005af230", "thiscall", "v"),
    'C_Human::AttackFrame(I3D_frame*, int, bool, int)': ("0x005af250", "thiscall", "v"),
    'C_Human::MoveToAlarm(int)': ("0x005af6e0", "thiscall", "v"),
    'C_Human::Reload()': ("0x005af4f0", "thiscall", "v"),
    'C_Human::StopAction()': ("0x005af430", "thiscall", "v"),
    'C_Human::SetAnimation(char const*, int, int, bool)': ("0x005af780", "thiscall", "v"),
    'C_Human::TurnAt(I3D_frame*)': ("0x005af660", "thiscall", "v"),
    'C_Human::LookAround(int)': ("0x005af6a0", "thiscall", "v"),
    'C_Human::SetWaitForPlayer(bool, float, float, bool)': ("0x005af5e0", "thiscall", "v"),
    'C_Human::SetSniper(bool, float)': ("0x005af700", "thiscall", "v"),
    'C_Human::SetMoveModeRun()': ("0x005af270", "thiscall", "v"),
    'C_Human::SetMoveModeWalk()': ("0x005af290", "thiscall", "v"),
    'C_Human::SetMoveModeSilence()': ("0x005af2b0", "thiscall", "v"),
    'C_Human::SetMoveModeGuard()': ("0x005af2d0", "thiscall", "v"),
    'C_Human::SetMoveModeLie()': ("0x005af2f0", "thiscall", "v"),
    'C_Human::SetMoveModeCrouch()': ("0x005af310", "thiscall", "v"),
    'C_Human::SetMoveModeStand()': ("0x005af330", "thiscall", "v"),
    'C_Human::SetMoveModeStandFast()': ("0x005af350", "thiscall", "v"),
    'C_Human::SetAIMode(int)': ("0x005af3f0", "thiscall", "v"),
    'C_Human::TakeWeapon()': ("0x005af4b0", "thiscall", "v"),
    'C_Human::HolsterWeapon()': ("0x005af4d0", "thiscall", "v"),
    'C_Human::RaiseWeapon()': ("0x005af740", "thiscall", "v"),
    'C_Human::LowerWeapon()': ("0x005af750", "thiscall", "v"),
    'C_Human::PickObject(I3D_frame*, bool)': ("0x005af470", "thiscall", "v"),
    'C_Human::DropObject()': ("0x005af490", "thiscall", "v"),
    'C_Human::PlayActivity(int)': ("0x005af510", "thiscall", "i"),
    'C_Human::PlayActivity(int, I3D_frame*)': ("0x005af530", "thiscall", "i"),
    'C_Human::ActivityTurk()': ("0x005af390", "thiscall", "v"),
    'C_Human::Sit(I3D_frame*)': ("0x005af370", "thiscall", "v"),
    'C_Human::EnableEvents()': ("0x005af3d0", "thiscall", "v"),
    'C_Human::DisableEvents()': ("0x005af3b0", "thiscall", "v"),
    # Script parts 6 and 7: the script module's (script_module.cpp, script_module_save.cpp; mod_diff.cpp).
    'ResetSpeechSlots()': ("0x0071e680", "cdecl", "v"),
    'AttachModelUpdateHook(I3D_frame*)': ("0x007764d0", "fastcall", "v"),
    'DetachModelUpdateHook(I3D_frame*)': ("0x00776520", "fastcall", "v"),
    'C_BitStream::ReadString(char*, int)': ("0x0073d470", "thiscall", "v"),
    'C_BitStream::ReadBits(int)': ("0x0073d3d0", "thiscall", "i"),
    'C_Interface::OpenCommandMap(bool)': ("0x0049f410", "thiscall", "v"),
    'C_HumanVisual::RemoveShadow()': ("0x007590c0", "thiscall", "v"),
    'C_ActorsModule::SetActorActive(C_Actor*, bool)': ("0x00475ec0", "thiscall", "v"),
    # (A static member: the port's cdecl, the original's __fastcall: ECX the dubbing, EDX the frame.)
    'C_HumanMovement::PlayDubbing(int, I3D_frame*, C_Human*, float, float)': ("0x00786a70", "fastcall", "v"),
    'C_HumanMovement::UpdateModelNames()': ("0x00786b30", "thiscall", "v"),
    'C_QuickMessages::SetSubtitle(int)': ("0x0048f080", "thiscall", "v"),
    'C_QuickMessages::ShowSubtitles(bool)': ("0x0048ee50", "thiscall", "v"),
    'C_ResourceCache<I3D_model>::OpenInto(I3D_model*, char const*, unsigned int, void*, void*, int)':
        ("0x004290a0", "thiscall", "i"),
    'C_AnimationActor::SetCollisionsOn(bool)': ("0x005b54c0", "thiscall", "v"),
    'C_LightMapGroupsModule::SetFrameLightmap(I3D_frame*, int)': ("0x0048d270", "thiscall", "v"),
    'C_LightMapGroupsModule::SetGroupLightmap(S_LightmapGroup const*, int)': ("0x0048d4e0", "thiscall", "v"),
    'C_Team::UpdateCameraTarget()': ("0x0045dac0", "thiscall", "v"),
    'C_game::AddAnimatedModel(I3D_frame*)': ("0x0069a5b0", "thiscall", "v"),
    'C_game::RemoveAnimatedModel(I3D_frame*)': ("0x0069a630", "thiscall", "v"),
    'C_Human::SetScripted(bool)': ("0x005a3df0", "thiscall", "v"),
    'C_NetGame::CreateMessage(int, int)': ("0x0072c280", "thiscall", "p"),
    'C_NetGame::SendNetMessage(C_BitStream*)': ("0x0072c360", "thiscall", "v"),
    'C_CommandPanel::IsOpen() const': ("0x004b6180", "thiscall", "b"),
    'C_Human::IsAISuspended() const': ("0x005a5d30", "thiscall", "b"),
    # (Script part 7: LS3DF's; the original's import thunk is patched.)
    'GetIGraph': ("0x007e530e", "cdecl", "p"),
    # (Camera parts: the camera module's calls.)
    'PoseGetRoll(S_matrix const&)': ("0x007a36c0", "cdecl", "f"),
    'SegmentDistance2(S_vector const&, S_vector const&, S_vector const&, float*)': ("0x00581210", "fastcall", "f"),
    'CollisionSphereTest(S_vector const&, S_vector const&, float, S_vector*, S_vector*, unsigned int, unsigned int)':
        ("0x00718900", "stdcall", "p"),
    'CollisionSlideSphere(S_vector const&, S_vector const&, float, S_vector&, S_vector*, unsigned int, unsigned int)':
        ("0x00718930", "stdcall", "p"),
    'C_Controls::GetValue(int, bool)': ("0x006c76a0", "thiscall", "f"),
    'S_HumanBody::RemoveCollision(bool)': ("0x005ac1c0", "thiscall", "v"),
    'S_HumanBody::RestoreCollision(bool)': ("0x005ac1b0", "thiscall", "v"),
    'S_HumanBody::GetSector() const': ("0x005ac4c0", "thiscall", "p"),
    'C_GameProfile::GetDetailSettings()': ("0x006b6a60", "thiscall", "p"),
    'C_HumanMovement::SetCameraUnderWater(bool)': ("0x00786b20", "fastcall", "v"),
    'C_HumanMovement::GetHeadWater(bool*, float*) const': ("0x00786510", "thiscall", "b"),
    'C_FirstPersonView::Show(bool)': ("0x00491fd0", "thiscall", "v"),
    'C_Human::SetUnknown94(float)': ("0x005a9760", "thiscall", "v"),
    'C_Human::GetViewDir() const': ("0x005a80e0", "thiscall", "p"),
    'C_Human::GetStatFactor(int) const': ("0x005a6530", "thiscall", "f"),
    'C_Human::IsLocalPlayer() const': ("0x005a0890", "thiscall", "b"),
    'C_Human::GetPlayerViewMode() const': ("0x005a8170", "thiscall", "i"),
    'C_Human::IsPlayerHoldingBreath() const': ("0x005a6430", "thiscall", "b"),
    'C_CollisionWorld::RayTest(S_vector const&, S_vector const&, S_vector*, S_vector*, unsigned int, unsigned int) const':
        ("0x006f8060", "thiscall", "p"),
    'C_Team::GetSelectedMember() const': ("0x0045d160", "thiscall", "p"),
    'C_Vehicle::GetCameraOffset(S_vector*) const': ("0x004f3630", "thiscall", "v"),
    # (Mission loader part 1: the loader's calls.)
    'InitCarSurfaces()': ("0x004d57a0", "stdcall", "b"),
    'LoadTyres()': ("0x004d6900", "stdcall", "b"),
    'LoadCheckpointNetwork(C_chunk*, C_CheckpointNetwork*)': ("0x00523c00", "stdcall", "i"),
    'EnsureHumanAnimTablesLoaded()': ("0x007837b0", "stdcall", "b"),
    'C_Checkpoints::Clear()': ("0x00523e10", "thiscall", "v"),
    # (A namespace's function: the port's cdecl, the original's __fastcall: ECX the flag.)
    'ModuleManager::BroadcastLoadMissionData(bool)': ("0x0048dbf0", "fastcall", "v"),
    'C_ActorsModule::CreateActor(E_ActorType, I3D_frame*, bool, int)': ("0x004759c0", "thiscall", "p"),
    'C_ActorsModule::DestroyActor(C_Actor*, bool)': ("0x00475c50", "thiscall", "v"),
    'C_LoaderScreen::SetProgress(int, float)': ("0x006c9920", "thiscall", "v"),
    'C_LoaderScreen::FinishLoading()': ("0x006c9900", "thiscall", "v"),
    'C_LoaderScreen::Start(char const*, bool, bool, bool, C_MissionDef const*)': ("0x006c9990", "thiscall", "v"),
    'C_CollisionWorld::Load(char const*, bool)': ("0x006ee940", "thiscall", "v"),
    'C_ParticlesModule::LoadStaticEmitters()': ("0x00445d90", "thiscall", "v"),
    'C_LightMapGroupsModule::LoadGroups(C_chunk*)': ("0x0048cd70", "thiscall", "b"),
    'C_game::FreeMission()': ("0x0069a6a0", "thiscall", "v"),
    # (Junkers part 1: the bomber's calls.)
    'C_Actor::C_Actor(E_ActorType)': ("0x00610070", "thiscall", "v"),
    'C_Actor::~C_Actor()': ("0x00610170", "thiscall", "v"),
    'C_Actor::Activate(bool)': ("0x00610240", "thiscall", "v"),
    'C_Actor::Deactivate(bool)': ("0x00610510", "thiscall", "v"),
    'C_Actor::SaveGame(C_chunk&)': ("0x00610780", "thiscall", "b"),
    'C_Actor::LoadGame(C_chunk&)': ("0x00610a80", "thiscall", "b"),
    'C_Actor::SetFrame(I3D_frame*)': ("0x00610200", "thiscall", "b"),
    'CollisionRayTest(S_vector const&, S_vector const&, S_vector*, S_vector*, unsigned int, unsigned int)':
        ("0x00718770", "stdcall", "p"),
    'C_CollisionWorld::RayTestStatic(S_vector const&, S_vector const&, S_vector*, S_vector*, unsigned int) const':
        ("0x006f6c40", "thiscall", "p"),
    'C_CollisionWorld::AddVolume(S_CollisionVolume*)': ("0x006ef7b0", "thiscall", "v"),
    'C_CollisionWorld::RemoveVolume(S_CollisionVolume*)': ("0x006ef9b0", "thiscall", "v"),
    'C_CollisionWorld::MoveVolume(S_CollisionVolume*)': ("0x006efbc0", "thiscall", "v"),
    # (The air attack test's.)
    'C_CollisionWorld::SetIgnoredVolume(S_CollisionVolume*, int)': ("0x006efe40", "thiscall", "v"),
    'C_CollisionWorld::RayTestDynamic(S_vector const&, S_vector const&, S_vector*, S_vector*, unsigned int, unsigned int) const':
        ("0x006f7cc0", "thiscall", "p"),
    'C_CameraModule::GetCameraPos(int, S_vector4*)': ("0x00465a10", "vt+0x64", "b"),
    'C_ParticlesModule::CreateAt(int, S_vector const&, S_vector const&, I3D_frame*, bool, bool)':
        ("0x00445150", "thiscall", "i"),
}
# Static members (mangled like members: no `this`, the port's cdecl).
STATIC_MEMBERS = {'C_HumanMovement::PlayDubbing(int, I3D_frame*, C_Human*, float, float)',
                  'C_HumanMovement::SetCameraUnderWater(bool)',
                  'ModuleManager::BroadcastLoadMissionData(bool)'}


def kind(t):
    t = t.strip()
    if t == "...":
        return "."
    if t in ("bool",):
        return "b"
    if t in ("float",):
        return "f"
    if t in ("char const*", "char*"):
        return "s"
    if t in ("S_vector const&", "S_vector4 const&"):
        return "v"
    if t == "S_matrix const&":
        return "m"
    if t == "S_vector":
        return "fff"                                    # (by value: three words)
    if t.startswith("std::vector<unsigned short"):
        return "o"                                      # (a vector the callee fills: each side's layout)
    if t == "S_HumanCommand const&":
        return "c"                                      # (logged by its fields)
    if t == "unsigned short":
        return "h"                                      # (the low half: the upper one is the caller's leftover)
    if t == "S_quat const&":
        return "q"
    if t.endswith("*") or t.endswith("&") or "(" in t:
        return "p"
    if t in ("int", "unsigned int", "long", "unsigned long", "unsigned char", "char", "short", "unsigned short") or \
            t.startswith("E_"):
        return "i"
    sys.exit("gen_cmd_calls: no kind for the type %r" % t)


def params(plain):
    inner = plain[plain.index("(") + 1:plain.rindex(")")]
    out, depth, cur = [], 0, ""
    for ch in inner:
        if ch in "(<":
            depth += 1
        elif ch in ")>":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [p.strip() for p in out]


def engine_slots():
    """Every I3DMETHOD of i3d.h and of igraph.h's IGraph: (class, base, offset, name, kinds, answer)."""
    text = open(os.path.join(SRC, "ls3d", "i3d.h"), encoding="latin-1").read()
    graph = open(os.path.join(SRC, "ls3d", "igraph.h"), encoding="latin-1").read()
    for cls in ("class IGraph {", "class IImage {", "class ISND_driver {"):
        text += "\n" + graph[graph.index(cls):graph.index("\n};", graph.index(cls)) + 3]
    slots, bases = [], {}
    cls = None
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r"class (\w+)(?: : public (\w+))? \{", line)
        if m:
            cls = m.group(1)
            bases[cls] = m.group(2) or ""
        if "I3DMETHOD(" in line and cls:
            full = line
            while ") = 0;" not in full:
                i += 1
                full += " " + lines[i].strip()
            m = re.search(r"I3DMETHOD\(([^,]+), (\w+)\)\((.*?)\) = 0;\s*// (0x[0-9A-Fa-f]+)", full)
            if not m:
                sys.exit("gen_cmd_calls: unparsed: %s" % full)
            ret, name, plist, off = m.groups()
            ks = ""
            for p in [x.strip() for x in plist.split(",") if x.strip()]:
                p = re.sub(r"\s*=.*$", "", p)
                named = re.match(r"(.*?[\s*&])\s*(\w+)$", p)
                if named and named.group(1).strip():
                    p = named.group(1)                      # (the parameter's name dropped)
                p = re.sub(r"\s+", " ", p.replace("const ", "")).replace(" *", "*").replace(" &", "&").strip()
                if p.startswith("PI3D"):
                    p += "*"
                if p == "char*":
                    ks += "s"
                elif p in ("S_vector&",):
                    ks += "v"
                elif p in ("S_matrix&",):
                    ks += "m"
                elif p in ("S_quat&",):
                    ks += "q"
                elif p.endswith("*") or p.endswith("&"):
                    ks += "p"
                elif p == "bool":
                    ks += "b"
                elif p == "float":
                    ks += "f"
                elif p == "void":
                    pass
                else:
                    ks += "i"
            r = ret.strip()
            rk = "v" if r == "void" else "b" if r == "bool" else "f" if r == "float" else \
                "p" if r.endswith("*") or r.startswith("PI3D") else "i"
            slots.append((cls, int(off, 16), name, ks, rk))
        i += 1
    return slots, bases


# The tests: prefix -> (build script, executable, what the generated files' first line calls the calls).
TESTS = {"cmd": ("build_cmd.bat", "cmd_diff", "the game command test's logged calls"),
         "mod": ("build_mod.bat", "mod_diff", "the script module test's logged calls"),
         "cam": ("build_cam.bat", "cam_diff", "the camera module test's logged calls"),
         "ml": ("build_ml.bat", "ml_diff", "the mission loader test's logged calls"),
         "jk": ("build_jk.bat", "jk_diff", "the Junkers test's logged calls"),
         "aa": ("build_aa.bat", "aa_diff", "the air attack test's logged calls")}


def main():
    env = dict(os.environ, PATH=MINGW + os.pathsep + os.environ["PATH"])
    prefix = sys.argv[1] if len(sys.argv) > 1 else "cmd"
    if prefix not in TESTS:
        sys.exit("gen_cmd_calls: the test is cmd, mod, cam or ml")
    bat_name, exe, what = TESTS[prefix]
    bat = open(os.path.join(HERE, bat_name), encoding="latin-1").read()
    m = re.search(r"^g\+\+ (.*%s\.exe.*)$" % exe, bat, re.M)
    if not m:
        sys.exit("gen_cmd_calls: no g++ line in " + bat_name)
    line = m.group(1).replace("%SRC%", r"..\..\..\src").replace(" %s_calls.s" % prefix, "")
    tmp = os.environ.get("TEMP", HERE)
    probe = '-o "%s"' % os.path.join(tmp, prefix + "_probe.exe")
    line = re.sub(r'-o "[^"]*"', lambda _: probe, line)
    line = line.replace(" 2>&1", "") + " -Wl,--no-demangle"
    p = subprocess.run("g++ " + line, cwd=HERE, env=env, shell=True, capture_output=True, text=True)
    names = sorted(set(re.findall(r"undefined reference to `([^']+)'", p.stdout + p.stderr)) - {"_CmdCallEntry"})
    if not names:
        print(p.stdout + p.stderr)
        sys.exit("gen_cmd_calls: no undefined references (nothing to write, or another error above)")
    demangled = subprocess.run(["c++filt"], input="\n".join(names), env=env, capture_output=True, text=True,
                               shell=True).stdout.splitlines()
    calls, variables, unknown = [], [], []
    for name, plain in zip(names, demangled):
        if plain.startswith("_") and "(" not in plain:
            key = plain[1:]                              # (a C name: its underscore)
            if key in ORIG:
                calls.append((name, key, ORIG[key], "", "cdecl"))
            else:
                variables.append((name, plain))
            continue
        if "(" not in plain or plain.startswith(("vtable for", "typeinfo")):
            variables.append((name, plain))
            continue
        key = re.sub(r"@\d+$", "", plain)
        if key not in ORIG:
            unknown.append(key)
            continue
        ks = "".join(kind(t) for t in params(key) if t != "void")
        member = name.startswith("__ZN") and key not in STATIC_MEMBERS
        stdcall = re.search(r"@(\d+)$", name)
        if "." in ks:
            conv = "cdecl_this" if member else "cdecl"
        elif stdcall:
            conv = "stdcall_this" if member else "stdcall"
        else:
            conv = "thiscall" if member else "cdecl"
        if member:
            ks = "p" + ks
        calls.append((name, key, ORIG[key], ks, conv))
    if unknown:
        sys.exit("gen_cmd_calls: not in the table (add them to ORIG):\n  " + "\n  ".join(unknown))
    # A call reached only through the original's side (none on the port's: a vtable slot, RandFloat's import). (Not
    # the rest of the table: a function the port links in this test, the script module's in mod's, runs on both sides.)
    known = {c[1] for c in calls}
    extra = [k for k in ORIG if k not in known and ORIG[k][0] and (ORIG[k][1].startswith("vt+") or k == "RandFloat")]

    s = ["# %s_calls.s: GENERATED by gen_cmd_calls.py (%s); do not edit by hand." % (prefix, what), "",
         "\t.text", "\t.globl\t_CmdCallEntry",
         "# [ESP] side (0 the port, 1 the original), [ESP+4] the call's index, [ESP+8] the return address, then the",
         "# arguments. CmdCall (C++) gets the saved registers (PUSHAD's block) and sets g_cmdPop (the bytes the callee",
         "# pops) and g_cmdFloat (an answer in ST0).",
         "_CmdCallEntry:", "\tpushal", "\tpushl\t%esp", "\tcall\t_CmdCall", "\taddl\t$4, %esp",
         "\tmovl\t%eax, 28(%esp)", "\tmovl\t%edx, 20(%esp)", "\tpopal",
         "\tcmpl\t$0, _g_cmdFloat", "\tje\t1f", "\tflds\t_g_cmdFloatValue", "1:",
         "\tmovl\t%eax, _g_cmdSaveEax", "\tmovl\t8(%esp), %eax", "\tmovl\t%eax, _g_cmdSaveRet",
         "\taddl\t$12, %esp", "\taddl\t_g_cmdPop, %esp", "\tpushl\t_g_cmdSaveRet", "\tmovl\t_g_cmdSaveEax, %eax",
         "\tret", ""]
    inc = ["// %s_calls.inc: GENERATED by gen_cmd_calls.py (%s); do not edit by hand." % (prefix, what),
           "// name, the original's address (0: none), its convention, the port's convention, argument kinds, answer kind;",
           "// the original's vtable offset (-1: called directly).",
           "const S_CallDef CALLS[] = {"]
    all_calls = calls + [(None, k, ORIG[k], None, None) for k in extra]
    for i, (name, key, (addr, oconv, ret), ks, pconv) in enumerate(all_calls):
        vt = -1
        if oconv.startswith("vt+"):
            vt = int(oconv[3:], 16)
            oconv = "thiscall"
        if ks is None:
            ks = "p" if "::" in key else ""
            if key == "RandFloat":
                ks = ""
            pconv = "none"
        inc.append('    {"%s", %s, "%s", "%s", "%s", \'%s\', %d},' % (key.replace('"', "'"), addr or "0", oconv, pconv, ks,
                                                                     ret, vt))
        if name:
            s += ["# " + key, "\t.globl\t%s" % name, "%s:" % name, "\tpushl\t$%d" % i, "\tpushl\t$0",
                  "\tjmp\t_CmdCallEntry", ""]
    inc.append("};")
    for name, plain in variables:
        s += ["# " + plain, "\t.bss", "\t.align\t16", "\t.globl\t%s" % name, "%s:" % name, "\t.space\t65536", "\t.text", ""]
    slots, bases = engine_slots()
    inc.append("// The engine's virtual slots (i3d.h): class, base, offset, name, argument kinds (after `this`), answer kind.")
    inc.append("const S_SlotDef ENGINE_SLOTS[] = {")
    for cls, off, name, ks, rk in slots:
        inc.append('    {"%s", "%s", 0x%02X, "%s", "%s", \'%s\'},' % (cls, bases.get(cls, ""), off, name, ks, rk))
    inc.append("};")
    inc.append("// The engine's classes and their bases.")
    inc.append("const char *const ENGINE_CLASSES[][2] = {")
    for cls in sorted(bases):
        inc.append('    {"%s", "%s"},' % (cls, bases[cls]))
    inc.append("};")
    open(os.path.join(HERE, prefix + "_calls.s"), "w", newline="\r\n").write("\n".join(s) + "\n")
    open(os.path.join(HERE, prefix + "_calls.inc"), "w", newline="\r\n").write("\n".join(inc) + "\n")
    print("gen_cmd_calls: %d calls (%d the original's alone), %d variables, %d engine slots" %
          (len(all_calls), len(extra), len(variables), len(slots)))


main()
