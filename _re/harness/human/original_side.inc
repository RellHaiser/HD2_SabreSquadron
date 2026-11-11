// The original's side of the human harness (included by human_test.cpp): its functions outside the code under test
// redirected (RedirectOriginal) to loggers that write the same log as the port's fakes and answer from the same
// script. MSVC's thiscall: `this` in ECX, the callee pops; bools are passed as 4-byte words (the low byte counts);
// a returned S_vector4 goes through a hidden pointer after `this`.
namespace {
#define OT __attribute__((thiscall))
bool B(int word) { return (word & 0xFF) != 0; }

bool OT O_AdjustToPathHeight(void *self, float *pos)
{
    LogV(self, "AdjustToPathHeight", pos, 3);
    if (g_script.pathHeight)
        pos[1] = g_script.pathY;
    return g_script.pathHeight;
}
uint32_t OT O_GetStanceFlags(void *self) { Log(self, "GetStanceFlags"); return 0; }
int OT O_GetStat(void *self, int stat) { Log(self, "GetStat", {double(stat)}); return g_script.stat; }
bool OT O_IsLocalPlayer(void *) { return g_script.localPlayer; }
bool OT O_IsServerSimulated(void *) { return g_script.serverSimulated; }
bool OT O_IsWearingDivingGear(void *self) { Log(self, "IsWearingDivingGear"); return g_script.divingGear; }
void OT O_PlayerRequestDamage(void *self, int type, int damage, int attackerId, int zone)
{
    Log(self, "PlayerRequestDamage", {double(type), double(damage), double(attackerId), double(zone)});
}
void OT O_SendRemoteRequest(void *self, int request) { Log(self, "SendRemoteRequest", {double(request)}); }

void OT O_AddMove(void *self, const float *move, int duration) { LogV(self, "AddMove", move, 4, {double(duration)}); }
void OT O_AddMove2(void *self, const float *move, int duration) { LogV(self, "AddMove2", move, 4, {double(duration)}); }
void OT O_ReplaceVelocity(void *self, const float *velocity, float weight, float maxSpeed, float *result)
{
    LogV(self, "ReplaceVelocity", velocity, 4, {weight, maxSpeed});
    if (result)
        std::memcpy(result, velocity, 16);
}
void OT O_SetVelocity(void *self, const float *velocity, float weight, float maxSpeed, float *result)
{
    LogV(self, "SetVelocity", velocity, 4, {weight, maxSpeed});
    if (result)
        std::memcpy(result, velocity, 16);
}
void OT O_SetTrackRate(void *self, int track, float rate) { Log(self, "SetTrackRate", {double(track), rate}); }
void OT O_Slide(void *self, const float *offset, int duration) { LogV(self, "Slide", offset, 4, {double(duration)}); }
void OT O_MixerUpdate(void *self, int time) { Log(self, "MixerUpdate", {double(time)}); }

void OT O_PutAwayHandsItem(void *self, int putAway) { Log(self, "PutAwayHandsItem", {double(B(putAway))}); }
void OT O_SetExhausted(void *self, int start, int cap) { Log(self, "SetExhausted", {double(B(start)), double(B(cap))}); }
void OT O_ShowDeathFace(void *self) { Log(self, "ShowDeathFace"); }

void OT O_DamageByContacts(void *self, const S_MoveContacts *contacts, const S_CollisionVolume *volume, int time)
{
    Log(self, "DamageByContacts", {double(contacts ? contacts->count : -1), double(volume != nullptr), double(time)});
}
bool OT O_GetHeadWater(void *self, bool *underWater, float *surface)
{
    Log(self, "GetHeadWater");
    if (underWater)
        *underWater = g_script.headUnderWater;
    if (surface)
        *surface = g_script.headSurface;
    return g_script.headInWater;
}
void OT O_MoveDummies(void *self, const float *pos) { LogV(self, "MoveDummies", pos, 3); }
void OT O_MoveHitSpheres(void *self, const float *pos) { LogV(self, "MoveHitSpheres", pos, 3); }
void OT O_PlaySound(void *self, int group) { Log(self, "PlaySound", {double(group)}); }
void OT O_RemoveCollision(void *self, int unknown) { Log(self, "RemoveCollision", {double(B(unknown))}); }
void OT O_RestoreCollision(void *self, int unknown) { Log(self, "RestoreCollision", {double(B(unknown))}); }
void OT O_SetSeatMode(void *self, int mode, int on) { Log(self, "SetSeatMode", {double(mode), double(B(on))}); }
void OT O_UpdateDummies(void *self) { LogV(self, "UpdateDummies", FramePos(), 3); }
void OT O_UpdateHitSpheres(void *self) { Log(self, "UpdateHitSpheres"); }
void OT O_UpdateStanceVolumes(void *self, int stance) { Log(self, "UpdateStanceVolumes", {double(stance)}); }

// (The original's takes the angle by pointer.)
bool OT O_CanTurn(void *self, const float *angle) { Log(self, "CanTurn", {*angle}); return g_script.canTurn; }
bool OT O_HasRoomForStance(void *self, int change) { Log(self, "HasRoomForStance", {double(change)}); return g_script.hasRoom; }
bool OT O_TryClimbOver(void *self, void *) { Log(self, "TryClimbOver"); return g_script.tryClimb; }
bool OT O_UpdateWater(void *self) { Log(self, "UpdateWater"); return g_script.updateWater; }

void OT O_SetNetworkPosition(void *self, const float *pos) { LogV(self, "SetNetworkPosition", pos, 4); }

float *OT O_GetViewDir(void *self, float *out)
{
    Log(self, "GetViewDir");
    out[0] = 0.0f;
    out[1] = g_script.viewDirY;
    out[2] = 1.0f;
    out[3] = 0.0f;
    return out;
}
void OT O_Play(void *self, int animation, int on) { Log(self, "Play", {double(animation), double(B(on))}); }
void OT O_SetFacingDir(void *self, const float *dir, int on) { LogV(self, "SetFacingDir", dir, 3, {double(B(on))}); }
void OT O_SetLookAt(void *self, const float *target, int on)
{
    LogV(self, "SetLookAt", target, 4, {double(B(on))});
    ApplyLookAt(self, target);
}
void OT O_SetStance(void *self, int stance, int on) { Log(self, "SetStance", {double(stance), double(B(on))}); }
void OT O_SetStanceTimer(void *self, int time, int stance, uint32_t blocked)
{
    Log(self, "SetStanceTimer", {double(time), double(stance), double(blocked)});
}

bool OT O_LadderCanUse(void *self, const float *pos, const float *dir, const float *velocity, int lookingUp)
{
    Log(self, "LadderCanUse", {pos[0], pos[1], pos[2], dir[0], dir[1], dir[2], velocity[0], velocity[1], velocity[2], double(B(lookingUp))});
    return g_script.ladderCanUse;
}
void OT O_RemoveClimber(void *self, void *human) { Log(self, "RemoveClimber", {double(human != nullptr)}); }
void *OT O_SoundsPlay(void *self, int group, void *frame, const float *pos)
{
    LogV(self, ("SoundsPlay " + NameOf(frame)).c_str(), pos, 3, {double(group)});
    return FakeSound();
}
void OT O_SoundsReturn(void *self, void *sound) { Log(self, ("SoundsReturn " + NameOf(sound)).c_str()); }
void *O_GetMovementSounds() { return nullptr; }
// (The port's team object stands for the original's too: only its name is logged.)
void *OT O_GetPlayerTeam(void *) { return g_teamManager.GetPlayerTeam(); }
void *OT O_GetActivePlayer(void *self) { Log(self, "GetActivePlayer"); return g_script.activeIsOwner ? o_human : nullptr; }
void OT O_DoorSetState(void *self, int action, int autoClose, int linked, int force)
{
    Log(self, "DoorSetState", {double(action), double(B(autoClose)), double(B(linked)), double(force)});
}
// The world's volumes: the original's never registered (its allocator never started).
void OT O_NoVolume(void *, void *) {}
void *O_Unexpected()
{
    std::printf("unexpected call in the original\n");
    std::abort();
}
float O_RandFloat() { return RandFloat(); }
// The original's game: its scene at +0x18.
alignas(16) uint8_t g_origGame[0x100];
void *O_GetGame()
{
    *reinterpret_cast<void **>(g_origGame + 0x18) = GetGame()->m_scene;
    *reinterpret_cast<uint32_t *>(g_origGame + 0x28) = GetGame()->m_missionTime;
    return g_origGame;
}
// The human's virtual calls (the original's slots: Hit 0xD4, OnCollisionEx 0x5C).
void OT O_Hit(void *self, int type, float amount, const float *pos, const float *dir, void *attacker, int flags)
{
    Log(self, "Hit", {double(type), amount, pos[0], pos[1], pos[2], dir[0], dir[1], dir[2], double(attacker != nullptr), double(flags)});
}
bool OT O_OnCollisionEx(void *self, void *other, const float *pos, int a3, int a4, int a5, int a6, int a7)
{
    const float *move = reinterpret_cast<const float *>(a3), *normal = reinterpret_cast<const float *>(a4);
    Log(self, ("OnCollisionEx " + NameOf(other)).c_str(), {pos[0], pos[1], pos[2], move[0], move[1], move[2], normal[0], normal[1], normal[2],
                                                            double(a5), double(a6), double(a7)});
    return false;
}
void OT O_UnexpectedVirtual(void *self)
{
    std::printf("unexpected virtual call in the original on %s\n", NameOf(self).c_str());
    std::abort();
}
#undef OT

// The scene's MarkTrackedModel (slot 0x144), logged for both sides.
void __stdcall F_MarkTrackedModel(void *, I3D_frame *frame) { Log(GetGame()->m_scene, "MarkTrackedModel", {double(frame != nullptr)}); }

void InstallOriginalSide()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005a66f0, reinterpret_cast<const void *>(O_AdjustToPathHeight)},
        {0x005a5d10, reinterpret_cast<const void *>(O_GetStanceFlags)},
        {0x005a6600, reinterpret_cast<const void *>(O_GetStat)},
        {0x005a0890, reinterpret_cast<const void *>(O_IsLocalPlayer)},
        {0x005aa790, reinterpret_cast<const void *>(O_IsServerSimulated)},
        {0x005a66b0, reinterpret_cast<const void *>(O_IsWearingDivingGear)},
        {0x005a9bf0, reinterpret_cast<const void *>(O_PlayerRequestDamage)},
        {0x005a9680, reinterpret_cast<const void *>(O_SendRemoteRequest)},
        {0x00773ef0, reinterpret_cast<const void *>(O_AddMove)},
        {0x00773f80, reinterpret_cast<const void *>(O_AddMove2)},
        {0x00773de0, reinterpret_cast<const void *>(O_ReplaceVelocity)},
        {0x00773cb0, reinterpret_cast<const void *>(O_SetVelocity)},
        {0x007751b0, reinterpret_cast<const void *>(O_SetTrackRate)},
        {0x00773fc0, reinterpret_cast<const void *>(O_Slide)},
        {0x00772e60, reinterpret_cast<const void *>(O_MixerUpdate)},
        {0x007920c0, reinterpret_cast<const void *>(O_PutAwayHandsItem)},
        {0x007815b0, reinterpret_cast<const void *>(O_SetExhausted)},
        {0x00781770, reinterpret_cast<const void *>(O_ShowDeathFace)},
        {0x0075f5e0, reinterpret_cast<const void *>(O_DamageByContacts)},
        {0x00786510, reinterpret_cast<const void *>(O_GetHeadWater)},
        {0x0075c6b0, reinterpret_cast<const void *>(O_MoveDummies)},
        {0x0075c050, reinterpret_cast<const void *>(O_MoveHitSpheres)},
        {0x0075ebd0, reinterpret_cast<const void *>(O_PlaySound)},
        {0x0075cdc0, reinterpret_cast<const void *>(O_RemoveCollision)},
        {0x0075cf20, reinterpret_cast<const void *>(O_RestoreCollision)},
        {0x0075d1b0, reinterpret_cast<const void *>(O_SetSeatMode)},
        {0x0075c4d0, reinterpret_cast<const void *>(O_UpdateDummies)},
        {0x0075bcd0, reinterpret_cast<const void *>(O_UpdateHitSpheres)},
        {0x0075c0e0, reinterpret_cast<const void *>(O_UpdateStanceVolumes)},
        {0x0074a2c0, reinterpret_cast<const void *>(O_CanTurn)},
        {0x0074f3c0, reinterpret_cast<const void *>(O_HasRoomForStance)},
        {0x0074aaf0, reinterpret_cast<const void *>(O_TryClimbOver)},
        {0x0074eee0, reinterpret_cast<const void *>(O_UpdateWater)},
        {0x00759e50, reinterpret_cast<const void *>(O_SetNetworkPosition)},
        {0x0077f030, reinterpret_cast<const void *>(O_GetViewDir)},
        {0x0077f060, reinterpret_cast<const void *>(O_Play)},
        {0x0077f8c0, reinterpret_cast<const void *>(O_SetFacingDir)},
        {0x0077f6c0, reinterpret_cast<const void *>(O_SetLookAt)},
        {0x0077f500, reinterpret_cast<const void *>(O_SetStance)},
        {0x007800d0, reinterpret_cast<const void *>(O_SetStanceTimer)},
        {0x005eb1d0, reinterpret_cast<const void *>(O_LadderCanUse)},
        {0x005eba70, reinterpret_cast<const void *>(O_RemoveClimber)},
        {0x007856f0, reinterpret_cast<const void *>(O_SoundsPlay)},
        {0x00785860, reinterpret_cast<const void *>(O_SoundsReturn)},
        {0x0075ee90, reinterpret_cast<const void *>(O_GetMovementSounds)},
        {0x00461410, reinterpret_cast<const void *>(O_GetPlayerTeam)},
        {0x0045d1d0, reinterpret_cast<const void *>(O_GetActivePlayer)},
        {0x005ff4b0, reinterpret_cast<const void *>(O_DoorSetState)},
        {0x004374d0, reinterpret_cast<const void *>(O_Unexpected)},          // S_SoundRequest::Init
        {0x0053b1d0, reinterpret_cast<const void *>(O_Unexpected)},
        {0x0055fe60, reinterpret_cast<const void *>(O_Unexpected)},
        {0x007e5302, reinterpret_cast<const void *>(O_RandFloat)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x006ef7b0, reinterpret_cast<const void *>(O_NoVolume)},           // AddVolume
        {0x006efbc0, reinterpret_cast<const void *>(O_NoVolume)},           // MoveVolume
        {0x006ef9b0, reinterpret_cast<const void *>(O_NoVolume)},           // RemoveVolume
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    for (void *&slot : o_humanVt)
        slot = reinterpret_cast<void *>(O_UnexpectedVirtual);
    o_humanVt[0xD4 / 4] = reinterpret_cast<void *>(O_Hit);
    o_humanVt[0x5C / 4] = reinterpret_cast<void *>(O_OnCollisionEx);
    g_sceneVt[0x144 / 4] = reinterpret_cast<void *>(F_MarkTrackedModel);
}
}  // namespace
