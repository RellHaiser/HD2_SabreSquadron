"""Writes cmd_calls.s and cmd_calls.inc for the game command test (cmd_diff.cpp, Script part 3).

cmd_calls.s: for every game function the port's interpreter calls that the test does not link (the link's undefined
references), a stub that enters the shared logger (CmdCallEntry: pushes the call's index and side, the logger decodes
the arguments, logs, answers and pops what the callee would); a variable's stub is 64 KB of zeros.
cmd_calls.inc: the calls' table (name, the original's address, how each side passes the arguments, the argument kinds,
the answer's kind) and the engine's virtual slots (src/ls3d/i3d.h: every I3DMETHOD with its offset), which both sides
call alike (__stdcall, `this` on the stack).

Run it when build_cmd.bat's link reports an undefined reference (the port's symbols changed): python gen_cmd_calls.py,
then build_cmd.bat. A function the table below does not know stops it: add it (the original's address from the port's
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
    'SetFrameData(I3D_frame*, S_FrameData const&)': ("0x006e9ea0", "cdecl", "v"),
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
}


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
    """Every I3DMETHOD of i3d.h: (class, base, offset, name, kinds, answer)."""
    text = open(os.path.join(SRC, "ls3d", "i3d.h"), encoding="latin-1").read()
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


def main():
    env = dict(os.environ, PATH=MINGW + os.pathsep + os.environ["PATH"])
    bat = open(os.path.join(HERE, "build_cmd.bat"), encoding="latin-1").read()
    m = re.search(r"^g\+\+ (.*cmd_diff\.exe.*)$", bat, re.M)
    if not m:
        sys.exit("gen_cmd_calls: no g++ line in build_cmd.bat")
    line = m.group(1).replace("%SRC%", r"..\..\..\src").replace(" cmd_calls.s", "")
    tmp = os.environ.get("TEMP", HERE)
    probe = '-o "%s"' % os.path.join(tmp, "cmd_probe.exe")
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
        member = name.startswith("__ZN")
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
    # A call reached only through the original's side (none on the port's: a vtable slot, RandFloat's import).
    known = {c[1] for c in calls}
    extra = [k for k in ORIG if k not in known and ORIG[k][0]]

    s = ["# cmd_calls.s: GENERATED by gen_cmd_calls.py (the game command test's logged calls); do not edit by hand.", "",
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
    inc = ["// cmd_calls.inc: GENERATED by gen_cmd_calls.py (the game command test's logged calls); do not edit by hand.",
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
    open(os.path.join(HERE, "cmd_calls.s"), "w", newline="\r\n").write("\n".join(s) + "\n")
    open(os.path.join(HERE, "cmd_calls.inc"), "w", newline="\r\n").write("\n".join(inc) + "\n")
    print("gen_cmd_calls: %d calls (%d the original's alone), %d variables, %d engine slots" %
          (len(all_calls), len(extra), len(variables), len(slots)))


main()
