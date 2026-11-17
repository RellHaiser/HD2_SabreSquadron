// Part 40 of the human harness (HUMAN_PLAYERCTL builds, build_playerctl.bat; included by human_test.cpp after
// aictl_test.inc): C_PlayerController, the local player's controller and the server's copy of a network player
// (game/player_controller.cpp), against the original's, on part 33's harness: part 23's controllers, human and fakes,
// part 33b's message and stream fakes.
//
// Each side has its player controller: the port's class and the original's made by the base's constructor (0x00582ff0)
// with its vtable (0x00811848, 0x560 bytes) and its own fields cleared. The port's is copied into the original's layout
// before each call (the base by part 23's table, its own 25 fields by _re/tools/layout_offsets.py's: the pending command
// without its vtable word) and the two compared after it, with the queued messages and the net object's block (part
// 33a's way).

#include "game/heavy_fire_support.h"
#include "game/squad_commander.h"

// ---- what the player controller calls, faked on both sides (the port's here, the original's below)
namespace {
struct S_PlayerCtlScript {
    bool cameraDir = true;                              // the camera module's GetCameraDir: its answer and direction
    S_vector4 cameraView = {0, 0, 1, 0};
    bool scriptedView = false;                          // HasScriptedView
    float viewDistance = 100.0f;                        // GetViewDistance
    bool canUseWeapon = true;                           // the first-person view's CanUseWeapon
    bool invertMouse = false;                           // the profile's detail settings
    bool enemyKnown = false;                            // the team's IsEnemyKnown
    int vipSlot = 0;                                    // the game's GetVipSlot
    bool whispering = false;                            // the squad commander's IsWhispering
    bool groupMembers = false;                          // HasGroupMembers
    int airAttack = 0;                                  // the heavy fire support's answers
    int artillery = 2;
    int epochs[2] = {-1, -1};                           // the replication's write and read epochs (the port's)
    uint32_t seatFlags = 0;                             // the actors' GetSeatFlags
    bool seatCamera = true;                             // GetSeatCamera: the human's frame or none
    bool occupant = true;                               // GetSeatOccupant: the human or none
} g_pcs;
alignas(16) uint8_t g_plOptionsPort[0x100], g_plOptionsOrig[0x100];   // the detail settings (invertMouse +0x39)
alignas(16) uint8_t g_plCommanderPort[0x40], g_plCommanderOrig[0x40];
void *g_plCommanderPortVt[32], *g_plCommanderOrigVt[32];
alignas(16) uint8_t g_plFireSupportPort[0x10], g_plFireSupportOrig[0x10];
void *g_plFireSupportPortVt[8], *g_plFireSupportOrigVt[8];
}  // namespace

bool C_CameraModule::GetCameraDir(int index, S_vector4 *dir)
{
    Log(nullptr, "CameraGetCameraDir", {double(index)});
    *dir = g_pcs.cameraView;
    return g_pcs.cameraDir;
}
float C_CameraModule::GetViewDistance()
{
    Log(nullptr, "GetViewDistance");
    return g_pcs.viewDistance;
}
bool C_CameraModule::HasScriptedView()
{
    Log(nullptr, "CameraHasScriptedView");
    return g_pcs.scriptedView;
}
void C_CameraModule::SetViewDir(const S_vector4 &dir) { Log(nullptr, "CameraSetViewDir", {dir.x, dir.y, dir.z, dir.w}); }
bool C_Car::SetInput(float throttle, float steering)
{
    Log(this, "CarSetInput", {throttle, steering});
    return true;
}
void C_EnemyRecords::Remove(C_Human *enemy) { Log(enemy, "EnemyRecordsRemove"); }
bool C_FirstPersonView::CanUseWeapon() const
{
    Log(nullptr, "FpvCanUseWeapon");
    return g_pcs.canUseWeapon;
}
void C_FirstPersonView::SetAimView(bool on, float fov) { Log(nullptr, "FpvSetAimView", {double(on), fov}); }
void C_FirstPersonView::SetWeaponView(I3D_model *weapon, I3D_frame *magazine, int itemType)
{
    Log(nullptr, ("FpvSetWeaponView " + NameOf(weapon) + " " + NameOf(magazine)).c_str(), {double(itemType)});
}
C_PlayerOptions *C_GameProfile::GetDetailSettings()
{
    Log(nullptr, "GetDetailSettings");
    return reinterpret_cast<C_PlayerOptions *>(g_plOptionsPort);
}
void C_Interface::ShowSoundDirection(const S_vector &dir, int chapter) { Log(nullptr, "ShowSoundDirection", {dir.x, dir.y, dir.z, double(chapter)}); }
// (campaign.cpp's, copied: the original reads the vector inline.)
S_MissionObjective *C_MissionDef::GetObjective(int index) const
{
    if (index < 0 || index >= GetObjectiveCount())
        return nullptr;
    return m_objectives[index];
}
void C_NetGame::AddScore(int slot, int points) { Log(nullptr, "NetGameAddScore", {double(slot), double(points)}); }
// (net_object.cpp's are field writes, inline in the original: the values kept and compared with its +0xC4 / +0xC8.)
void C_NetReplication::SetWriteEpoch(int value) { g_pcs.epochs[0] = value; }
void C_NetReplication::SetReadEpoch(int value) { g_pcs.epochs[1] = value; }
int C_game::GetVipSlot() const
{
    Log(nullptr, "GameGetVipSlot");
    return g_pcs.vipSlot;
}
void C_game::SetObjectiveStatus(int objective, int status) { Log(nullptr, "GameSetObjectiveStatus", {double(objective), double(status)}); }
void C_game::ShowBinocularsView(bool show) { Log(nullptr, "GameShowBinocularsView", {double(show)}); }
I_HeavyFireSupport *GetHeavyFireSupport() { return reinterpret_cast<I_HeavyFireSupport *>(g_plFireSupportPort); }
C_SquadCommander *GetSquadCommander() { return reinterpret_cast<C_SquadCommander *>(g_plCommanderPort); }
void C_Human::RequestThrowItem(C_Item *item, int arg, const S_vector &dir)
{
    Log(this, ("RequestThrowItem " + NameOf(item)).c_str(), {double(arg), dir.x, dir.y, dir.z});
}
bool C_Team::IsEnemyKnown(const C_Human *enemy) const
{
    Log(enemy, "TeamIsEnemyKnown");
    return g_pcs.enemyKnown;
}
void C_Team::ReleaseHoldingMembers(const C_Human *human) { Log(human, "TeamReleaseHoldingMembers"); }
// (The movement's OnFrozen, part 4's, logged: wrapped here.)
extern "C" void __attribute__((thiscall)) __wrap__ZN15C_HumanMovement8OnFrozenEb(C_HumanMovement *m, bool on) { Log(m, "MovementOnFrozen", {double(on)}); }
// The binoculars' net message (net code not linked: raw storage under its vtable's name).
asm(".globl __ZTV22C_NetBinocularsMessage\n\t.data\n\t.balign 4\n__ZTV22C_NetBinocularsMessage:\n\t.space 64\n\t.text");

// The player controller's members in player_net.cpp and player_weapons.cpp (parts 41-42 link them): logged here, the
// original's redirected alike; the ones not called from player_controller.cpp unexpected.
void C_PlayerController::FlushMessages(bool toClients) { Log(this, "PlayerFlushMessages", {double(toClients)}); }
void C_PlayerController::TickLocal(int timeDelta) { Log(this, "PlayerTickLocal", {double(timeDelta)}); }
void C_PlayerController::UpdateRemote(int frameTime) { Log(this, "PlayerUpdateRemote", {double(frameTime)}); }
// (With the four weapon keys Tick latches in: the buttons THROW, FIRE, 7, RELOAD.)
void C_PlayerController::WeaponControls(int timeDelta)
{
    const float *b = m_buttons;
    Log(this, "PlayerWeaponControls", {double(timeDelta), b ? b[BUTTON_THROW] : -1.0, b ? b[BUTTON_FIRE] : -1.0, b ? b[BUTTON_7] : -1.0,
                                       b ? b[BUTTON_RELOAD] : -1.0});
}
void C_PlayerController::IssueCommand(const S_HumanCommand &) { AiUnexpected(__PRETTY_FUNCTION__); }
bool C_PlayerController::OnCollisionEx(C_Actor *, const S_vector *, int, int, int, int, int) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_PlayerController::OnMessage(C_BitStream *) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_PlayerController::OnMessage2(C_BitStream *) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_PlayerController::OnUniformChanged(int) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_PlayerController::RequestAction(int, const S_vector4 *, const S_vector4 *, float, int, int) { AiUnexpected(__PRETTY_FUNCTION__); }
bool C_PlayerController::RequestMoveItem(int, int, int, int, int) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_PlayerController::RequestPickUpItem(int, int) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_PlayerController::RequestTakeFromBox(int, int) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_PlayerController::RequestThrowItem(int, const S_vector &) { AiUnexpected(__PRETTY_FUNCTION__); }
bool C_PlayerController::RequestTransferItem(int, int, int, int, int) { AiUnexpected(__PRETTY_FUNCTION__); }

namespace {
// ---- the original's side
bool AC PO_GetCameraDir(void *, int index, S_vector4 *dir)
{
    Log(nullptr, "CameraGetCameraDir", {double(index)});
    *dir = g_pcs.cameraView;
    return g_pcs.cameraDir;
}
float AC PO_GetViewDistance(void *)
{
    Log(nullptr, "GetViewDistance");
    return g_pcs.viewDistance;
}
int AC PO_HasScriptedView(void *)
{
    Log(nullptr, "CameraHasScriptedView");
    return g_pcs.scriptedView;
}
void AC PO_SetViewDir(void *, const S_vector4 *dir) { Log(nullptr, "CameraSetViewDir", {dir->x, dir->y, dir->z, dir->w}); }
int AC PO_CarSetInput(void *self, float throttle, float steering)
{
    Log(self, "CarSetInput", {throttle, steering});
    return 1;
}
void AC PO_EnemyRecordsRemove(void *, void *enemy) { Log(enemy, "EnemyRecordsRemove"); }
int AC PO_CanUseWeapon(void *)
{
    Log(nullptr, "FpvCanUseWeapon");
    return g_pcs.canUseWeapon;
}
void AC PO_SetAimView(void *, int on, float fov) { Log(nullptr, "FpvSetAimView", {double(on & 0xFF), fov}); }
void AC PO_SetWeaponView(void *, void *weapon, void *magazine, int itemType)
{
    Log(nullptr, ("FpvSetWeaponView " + NameOf(weapon) + " " + NameOf(magazine)).c_str(), {double(itemType)});
}
void *AC PO_GetDetailSettings(void *)
{
    Log(nullptr, "GetDetailSettings");
    return g_plOptionsOrig;
}
void AC PO_ShowSoundDirection(void *, const S_vector *dir, int chapter) { Log(nullptr, "ShowSoundDirection", {dir->x, dir->y, dir->z, double(chapter)}); }
void AC PO_AddScore(void *, int slot, int points) { Log(nullptr, "NetGameAddScore", {double(slot), double(points)}); }
int AC PO_GetVipSlot(void *)
{
    Log(nullptr, "GameGetVipSlot");
    return g_pcs.vipSlot;
}
void AC PO_SetObjectiveStatus(void *, int objective, int status) { Log(nullptr, "GameSetObjectiveStatus", {double(objective), double(status)}); }
void AC PO_ShowBinocularsView(void *, int show) { Log(nullptr, "GameShowBinocularsView", {double(show & 0xFF)}); }
void *PO_GetHeavyFireSupport() { return g_plFireSupportOrig; }
void *PO_GetSquadCommander() { return g_plCommanderOrig; }
void AC PO_RequestThrowItem(void *self, void *item, int arg, const S_vector *dir)
{
    Log(self, ("RequestThrowItem " + NameOf(item)).c_str(), {double(arg), dir->x, dir->y, dir->z});
}
int AC PO_IsEnemyKnown(void *, const void *enemy)
{
    Log(enemy, "TeamIsEnemyKnown");
    return g_pcs.enemyKnown;
}
void AC PO_ReleaseHoldingMembers(void *, const void *human) { Log(human, "TeamReleaseHoldingMembers"); }
void AC PO_FlushMessages(void *self, int toClients) { Log(self, "PlayerFlushMessages", {double(toClients & 0xFF)}); }
void AC PO_TickLocal(void *self, int timeDelta) { Log(self, "PlayerTickLocal", {double(timeDelta)}); }
void AC PO_UpdateRemote(void *self, int frameTime) { Log(self, "PlayerUpdateRemote", {double(frameTime)}); }
void AC PO_WeaponControls(void *self, int timeDelta)
{
    const float *b;
    std::memcpy(&b, static_cast<uint8_t *>(self) + 0x78, 4);
    Log(self, "PlayerWeaponControls", {double(timeDelta), b ? b[BUTTON_THROW] : -1.0, b ? b[BUTTON_FIRE] : -1.0, b ? b[BUTTON_7] : -1.0,
                                       b ? b[BUTTON_RELOAD] : -1.0});
}
void AC PO_MovementOnFrozen(void *m, int on) { Log(m, "MovementOnFrozen", {double(on & 0xFF)}); }

// The squad commander (its port slots by member, the original's 0x00, 0x0C, 0x2C, 0x30, 0x34) and the heavy fire
// support (0x00, 0x04): one block per side.
void AC CM_ExecuteActionCommand(void *self, const uint8_t *command)
{
    // (The command's fields, +4 .. +0x34: category, direction, action, command, actor, value, member: the two
    // pointers by name.)
    std::vector<double> nums;
    for (size_t at = 4; at < 0x34; at += 4) {
        if (at == 0x28 || at == 0x30)
            continue;
        int32_t w;
        std::memcpy(&w, command + at, 4);
        nums.push_back(w);
    }
    void *actor, *member;
    std::memcpy(&actor, command + 0x28, 4);
    std::memcpy(&member, command + 0x30, 4);
    S_Call c{NameOf(self) + ".ExecuteActionCommand " + NameOf(actor) + " " + NameOf(member), nums};
    g_calls.push_back(c);
}
int AC CM_HasGroupMembers(void *self, int group)
{
    Log(self, "HasGroupMembers", {double(group)});
    return g_pcs.groupMembers;
}
void AC CM_SetWhispering(void *self, int on)
{
    Log(self, "SetWhispering", {double(on & 0xFF)});
    g_pcs.whispering = (on & 0xFF) != 0;
}
int AC CM_IsWhispering(void *self)
{
    Log(self, "IsWhispering");
    return g_pcs.whispering;
}
void AC CM_ReleaseFollowers(void *self, void *leader) { Log(self, ("ReleaseFollowers " + NameOf(leader)).c_str()); }
int AC FS_RequestAirAttack(void *self, const uint8_t *request)
{
    // (x, z, dirX, dirZ, kind, planes, height, caller, team.)
    float f[4], height;
    int kind, planes, team;
    void *caller;
    std::memcpy(f, request, sizeof f);
    std::memcpy(&kind, request + 0x10, 4);
    std::memcpy(&planes, request + 0x14, 4);
    std::memcpy(&height, request + 0x18, 4);
    std::memcpy(&caller, request + 0x1C, 4);
    std::memcpy(&team, request + 0x20, 4);
    Log(self, ("RequestAirAttack " + NameOf(caller)).c_str(), {f[0], f[1], f[2], f[3], double(kind), double(planes), height, double(team)});
    return g_pcs.airAttack;
}
int AC FS_RequestArtillery(void *self, const uint8_t *request)
{
    // (x, z, caller, kind, team.)
    float f[2];
    int kind, team;
    void *caller;
    std::memcpy(f, request, sizeof f);
    std::memcpy(&caller, request + 0x08, 4);
    std::memcpy(&kind, request + 0x0C, 4);
    std::memcpy(&team, request + 0x10, 4);
    Log(self, ("RequestArtillery " + NameOf(caller)).c_str(), {f[0], f[1], double(kind), double(team)});
    return g_pcs.artillery;
}

// The actors' seats (part 23's actor vtables: the port's slots by member, the original's 0x8C, 0x90, 0x78).
uint32_t CT_ PL_GetSeatFlags(void *self, int seat)
{
    Log(self, "GetSeatFlags", {double(seat)});
    return g_pcs.seatFlags;
}
void *CT_ PL_GetSeatCameraPort(void *self, int seat)
{
    Log(self, "GetSeatCamera", {double(seat)});
    return g_pcs.seatCamera ? g_h.frame : nullptr;
}
void *CT_ PL_GetSeatCameraOrig(void *self, int seat)
{
    Log(self, "GetSeatCamera", {double(seat)});
    return g_pcs.seatCamera ? g_h.frame : nullptr;
}
void *CT_ PL_GetSeatOccupantPort(void *self, int seat)
{
    Log(self, "GetSeatOccupant", {double(seat)});
    return g_pcs.occupant ? static_cast<void *>(g_h.human) : nullptr;
}
void *CT_ PL_GetSeatOccupantOrig(void *self, int seat)
{
    Log(self, "GetSeatOccupant", {double(seat)});
    return g_pcs.occupant ? static_cast<void *>(o_human) : nullptr;
}

void SetUpPlayerFakes()
{
    g_actPortVt[VSlot(&C_Actor::GetSeatFlags)] = reinterpret_cast<void *>(PL_GetSeatFlags);
    g_actPortVt[VSlot(&C_Actor::GetSeatCamera)] = reinterpret_cast<void *>(PL_GetSeatCameraPort);
    g_actPortVt[VSlot(&C_Actor::GetSeatOccupant)] = reinterpret_cast<void *>(PL_GetSeatOccupantPort);
    g_actOrigVt[0x8C / 4] = reinterpret_cast<void *>(PL_GetSeatFlags);
    g_actOrigVt[0x90 / 4] = reinterpret_cast<void *>(PL_GetSeatCameraOrig);
    g_actOrigVt[0x78 / 4] = reinterpret_cast<void *>(PL_GetSeatOccupantOrig);
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x004d9280, FAKE(PO_CarSetInput)},
        {0x0045e540, FAKE(PO_EnemyRecordsRemove)},
        {0x00493500, FAKE(PO_CanUseWeapon)},
        {0x00492130, FAKE(PO_SetAimView)},
        {0x00491130, FAKE(PO_SetWeaponView)},
        {0x006b6a60, FAKE(PO_GetDetailSettings)},
        {0x0049de80, FAKE(PO_ShowSoundDirection)},
        {0x0072f1a0, FAKE(PO_AddScore)},
        {0x0069e340, FAKE(PO_GetVipSlot)},
        {0x0069bd30, FAKE(PO_SetObjectiveStatus)},
        {0x0069a7a0, FAKE(PO_ShowBinocularsView)},
        {0x00471fb0, FAKE(PO_GetHeavyFireSupport)},
        {0x004563e0, FAKE(PO_GetSquadCommander)},
        {0x005a2c50, FAKE(PO_RequestThrowItem)},
        {0x0045e920, FAKE(PO_IsEnemyKnown)},
        {0x0045f110, FAKE(PO_ReleaseHoldingMembers)},
        {0x0053f370, FAKE(PO_FlushMessages)},
        {0x0053f9b0, FAKE(PO_TickLocal)},
        {0x0053f520, FAKE(PO_UpdateRemote)},
        {0x005433b0, FAKE(PO_WeaponControls)},
        {0x0075c730, FAKE(PO_MovementOnFrozen)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_camModuleVt[0x68 / 4] = reinterpret_cast<void *>(PO_GetCameraDir);
    g_camModuleVt[0x58 / 4] = reinterpret_cast<void *>(PO_GetViewDistance);
    g_camModuleVt[0x80 / 4] = reinterpret_cast<void *>(PO_HasScriptedView);
    g_camModuleVt[0x48 / 4] = reinterpret_cast<void *>(PO_SetViewDir);
    for (void *&slot : g_plCommanderPortVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    for (void *&slot : g_plCommanderOrigVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_plCommanderPortVt[VSlot(&C_SquadCommander::ExecuteActionCommand)] = reinterpret_cast<void *>(CM_ExecuteActionCommand);
    g_plCommanderPortVt[VSlot(&C_SquadCommander::HasGroupMembers)] = reinterpret_cast<void *>(CM_HasGroupMembers);
    g_plCommanderPortVt[VSlot(&C_SquadCommander::SetWhispering)] = reinterpret_cast<void *>(CM_SetWhispering);
    g_plCommanderPortVt[VSlot(&C_SquadCommander::IsWhispering)] = reinterpret_cast<void *>(CM_IsWhispering);
    g_plCommanderPortVt[VSlot(&C_SquadCommander::ReleaseFollowers)] = reinterpret_cast<void *>(CM_ReleaseFollowers);
    g_plCommanderOrigVt[0x00 / 4] = reinterpret_cast<void *>(CM_ExecuteActionCommand);
    g_plCommanderOrigVt[0x0C / 4] = reinterpret_cast<void *>(CM_HasGroupMembers);
    g_plCommanderOrigVt[0x2C / 4] = reinterpret_cast<void *>(CM_SetWhispering);
    g_plCommanderOrigVt[0x30 / 4] = reinterpret_cast<void *>(CM_IsWhispering);
    g_plCommanderOrigVt[0x34 / 4] = reinterpret_cast<void *>(CM_ReleaseFollowers);
    *reinterpret_cast<void ***>(g_plCommanderPort) = g_plCommanderPortVt;
    *reinterpret_cast<void ***>(g_plCommanderOrig) = g_plCommanderOrigVt;
    g_names[g_plCommanderPort] = g_names[g_plCommanderOrig] = "squadCommander";
    g_extraPointers.push_back({g_plCommanderPort, g_plCommanderOrig});
    g_plFireSupportPortVt[VSlot(&I_HeavyFireSupport::RequestAirAttack)] = reinterpret_cast<void *>(FS_RequestAirAttack);
    g_plFireSupportPortVt[VSlot(&I_HeavyFireSupport::RequestArtillery)] = reinterpret_cast<void *>(FS_RequestArtillery);
    g_plFireSupportOrigVt[0] = reinterpret_cast<void *>(FS_RequestAirAttack);
    g_plFireSupportOrigVt[1] = reinterpret_cast<void *>(FS_RequestArtillery);
    *reinterpret_cast<void ***>(g_plFireSupportPort) = g_plFireSupportPortVt;
    *reinterpret_cast<void ***>(g_plFireSupportOrig) = g_plFireSupportOrigVt;
    g_names[g_plFireSupportPort] = g_names[g_plFireSupportOrig] = "fireSupport";
    // The HUD option (one byte both sides read) and the detail settings (a block per side).
    *OriginalAt<void **>(0x008aea18) = g_hudOption;
    g_names[g_plOptionsPort] = g_names[g_plOptionsOrig] = "detailSettings";
    // (A car's C_Vehicle, OnKilled's throttle: the port's member and the original's +0x90, named for the logs.)
    for (int k = 0; k < ACTOR_KIND_COUNT; ++k)
        g_names[reinterpret_cast<C_Car *>(g_actPort[k])->GetVehicle()] = g_names[g_actOrig[k] + 0x90] = "carVehicle";
}
}  // namespace

// ---- the mission (OnDeath's VIP objectives): the port's C_MissionDef with its objectives, the original's game +0xA8
// pointing at a block whose MSVC6 vector (+0x30) holds the objectives (team +0x18, type +0x28): a block per side each.
namespace {
template <class Tag, typename Tag::type M> struct S_PlRob {
    friend typename Tag::type PlGet(Tag) { return M; }
};
#define PL_ROB(NAME, TYPE, MEMBER) \
    struct NAME { \
        using type = TYPE; \
        friend type PlGet(NAME); \
    }; \
    template struct S_PlRob<NAME, MEMBER>;
PL_ROB(GmMission, C_MissionDef *C_game::*, &C_game::m_mission)
PL_ROB(MsObjectives, std::vector<S_MissionObjective *> C_MissionDef::*, &C_MissionDef::m_objectives)
PL_ROB(GmCameraMode, int C_game::*, &C_game::m_cameraMode)
PL_ROB(GmSpectatedSlot, int C_game::*, &C_game::m_spectatedSlot)
#undef PL_ROB
alignas(16) uint8_t g_plMissionPort[sizeof(C_MpMissionDef) + 0x40], g_plMissionOrig[0x100];
alignas(16) uint8_t g_plObjectivePort[4][sizeof(S_MpObjective) + 0x10], g_plObjectiveOrig[4][0x40];
void *g_plObjectiveOrigList[4];
C_MissionDef *PlMission() { return reinterpret_cast<C_MissionDef *>(g_plMissionPort); }
void SetUpPlayerMission()
{
    // (A zeroed std::vector is an empty one.)
    std::memset(g_plMissionPort, 0, sizeof g_plMissionPort);
    GetGame()->*PlGet(GmMission()) = PlMission();
    void *om = g_plMissionOrig;
    std::memcpy(g_origGame + 0xA8, &om, 4);
    g_names[g_plMissionPort] = g_names[g_plMissionOrig] = "mission";
    g_extraPointers.push_back({g_plMissionPort, g_plMissionOrig});
}
void RandomPlayerMission()
{
    std::vector<S_MissionObjective *> &objectives = PlMission()->*PlGet(MsObjectives());
    objectives.clear();
    const int n = P(0.2f) ? 0 : I(1, 4);
    for (int k = 0; k < n; ++k) {
        auto *o = reinterpret_cast<S_MpObjective *>(g_plObjectivePort[k]);
        o->team = I(0, 1);
        o->type = P(0.4f) ? OBJECTIVE_TYPE_VIP : I(0, 4);
        objectives.push_back(o);
        std::memcpy(g_plObjectiveOrig[k] + 0x18, &o->team, 4);
        std::memcpy(g_plObjectiveOrig[k] + 0x28, &o->type, 4);
        g_plObjectiveOrigList[k] = g_plObjectiveOrig[k];
    }
    uint32_t v[4] = {0, 0, 0, 0};
    if (n) {
        v[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_plObjectiveOrigList));
        v[2] = v[3] = v[1] + 4 * uint32_t(n);
    }
    std::memcpy(g_plMissionOrig + 0x30, v, 16);
    g_pcs.vipSlot = I(-1, 8);
}
}  // namespace

// ---- the pair
namespace {
struct S_TestPlayer : C_PlayerController {
    bool ExecuteCommand(const S_HumanCommand &c) override
    {
        LogCommand(this, c);
        return true;
    }
};
void *g_opVt[0x48];
S_TestPlayer *g_pp;
uint8_t *g_op;                                          // (0x560 bytes)
const S_LayoutField kPlayerFields[] = {
    // (Generated by _re/tools/layout_offsets.py from human_controller.h; the pending command apart.)
    S_LayoutField{0xC0, 172, 1, "m_whispering", 'i'},
    S_LayoutField{0xC4, 176, 256, "m_controlAxes", 'f'},
    S_LayoutField{0x1C4, 432, 256, "m_controlButtons", 'f'},
    S_LayoutField{0x2C4, 688, 256, "m_unknown2C4", 'f'},
    S_LayoutField{0x3C4, 944, 256, "m_unknown3C4", 'f'},
    S_LayoutField{0x4C4, 1200, 4, "m_latchThrow", 'i'},
    S_LayoutField{0x4C8, 1204, 4, "m_latchFire", 'i'},
    S_LayoutField{0x4CC, 1208, 4, "m_latchFire2", 'i'},
    S_LayoutField{0x4D0, 1212, 4, "m_latchReload", 'i'},
    S_LayoutField{0x4D4, 1216, 1, "m_holdingBreath", 'i'},
    S_LayoutField{0x4D8, 1220, 4, "m_breath", 'i'},
    S_LayoutField{0x520, 1276, 4, "m_commandDelay", 'i'},
    S_LayoutField{0x524, 1280, 1, "m_unknown524", 'i'},
    S_LayoutField{0x525, 1281, 1, "m_ordersChanged", 'i'},
    S_LayoutField{0x528, 1284, 4, "m_squadFlags", 'i'},
    S_LayoutField{0x52C, 1288, 4, "m_viewMode", 'i'},
    S_LayoutField{0x530, 1292, 4, "m_scopeDelay", 'i'},
    S_LayoutField{0x534, 1296, 4, "m_raisedTime", 'i'},
    S_LayoutField{0x538, 1300, 1, "m_charging", 'i'},
    S_LayoutField{0x53C, 1304, 4, "m_chargeTime", 'i'},
    S_LayoutField{0x540, 1308, 4, "m_throwDelay", 'i'},
    S_LayoutField{0x544, 1312, 4, "m_chargeAction", 'i'},
    S_LayoutField{0x548, 1316, 4, "m_squadCombatOrder", 'i'},
    S_LayoutField{0x550, 1320, 1, "m_vip", 'i'},
};
const size_t kPlCommand = 0x4E0, kPlCommandPort = 1224;   // the pending command (0x34 bytes; its vtable word apart)
void PlayerToOrig(const C_PlayerController *p, uint8_t *o)
{
    ControllerToOrig(p, o);
    const auto *b = reinterpret_cast<const uint8_t *>(p);
    for (const S_LayoutField &f : kPlayerFields)
        CopyMapped(b + f.port, o + f.orig, f.size);
    CopyMapped(b + kPlCommandPort + 4, o + kPlCommand + 4, 0x30);
}
std::string PlayerDiffers(const C_PlayerController *p, const uint8_t *o)
{
    std::string why = ControllerDiffers(p, o);
    if (!why.empty())
        return why;
    why = AiMessages(p->m_toClients, o + 0x90);
    if (!why.empty())
        return why;
    const auto *b = reinterpret_cast<const uint8_t *>(p);
    for (const S_LayoutField &f : kPlayerFields) {
        const int at = FirstDiff(b + f.port, o + f.orig, f.size);
        if (at >= 0) {
            uint32_t x = 0, y = 0;
            std::memcpy(&x, b + f.port + at, std::min<size_t>(4, f.size - at));
            std::memcpy(&y, o + f.orig + at, std::min<size_t>(4, f.size - at));
            char text[128];
            std::snprintf(text, sizeof text, "%s+%d: port %08x original %08x", f.name, at, x, y);
            return text;
        }
    }
    const int at = FirstDiff(b + kPlCommandPort + 4, o + kPlCommand + 4, 0x30);
    if (at >= 0)
        return "m_pendingCommand+" + std::to_string(at + 4);
    return "";
}
// (The human's net object, part 20's block: each side's writes kept apart, as part 33a's AiCall.)
template <class P, class O> void PlCall(const char *what, P port, O original)
{
    PlayerToOrig(g_pp, g_op);
    ControllerToOrig(g_pc2, g_oc2);
    std::memset(g_netObjectBytes, 0x5A, sizeof g_netObjectBytes);
    std::memcpy(g_aiNetPort, g_netObjectBytes, sizeof g_aiNetPort);
    // (The human's frame is shared, not in the restored bytes: its on flag and SetOn count per side; likewise the
    // player states unlocked.)
    const uint32_t flags0 = g_h.frame->m_frameFlags;
    const int setOn0 = Fake(g_h.frame)->setOn;
    uint32_t portFlags = 0, origFlags = 0;
    int portSetOn = 0, portUnlocked = 0, origSetOn = 0, origUnlocked = 0;
    g_unlockedPlayers = 0;
    DiffCall(what,
             [&] {
                 const int r = port();
                 std::memcpy(g_aiNetOrig, g_netObjectBytes, sizeof g_aiNetOrig);
                 std::memcpy(g_netObjectBytes, g_aiNetPort, sizeof g_aiNetPort);
                 std::memcpy(g_aiNetPort, g_aiNetOrig, sizeof g_aiNetPort);
                 portFlags = g_h.frame->m_frameFlags;
                 portSetOn = Fake(g_h.frame)->setOn - setOn0;
                 portUnlocked = g_unlockedPlayers;
                 g_h.frame->m_frameFlags = flags0;
                 Fake(g_h.frame)->setOn = setOn0;
                 g_unlockedPlayers = 0;
                 return r;
             },
             [&](void *o) {
                 const int r = original(o);
                 std::memcpy(g_aiNetOrig, g_netObjectBytes, sizeof g_aiNetOrig);
                 origFlags = g_h.frame->m_frameFlags;
                 origSetOn = Fake(g_h.frame)->setOn - setOn0;
                 origUnlocked = g_unlockedPlayers;
                 return r;
             });
    std::string why = PlayerDiffers(g_pp, g_op);
    // (Only the on flag: reading the matrix sets its other bits on the port's side.)
    if (why.empty() && ((portFlags ^ origFlags) & 1u || portSetOn != origSetOn))
        why = "the frame's on flag " + std::to_string(portFlags & 1u) + "/" + std::to_string(portSetOn) + " / " +
              std::to_string(origFlags & 1u) + "/" + std::to_string(origSetOn);
    if (why.empty() && portUnlocked != origUnlocked)
        why = "players unlocked " + std::to_string(portUnlocked) + " / " + std::to_string(origUnlocked);
    CHECK(why.empty());
    static std::map<std::string, int> shown;
    if (!why.empty() && shown[what]++ < 3)
        std::printf("  %s: %s\n", what, why.c_str());
}
template <class R, class... A> R OPl(uint32_t at, A... a)
{
    using F = R(AC *)(void *, A...);
    return OriginalAt<F>(at)(static_cast<void *>(g_op), a...);
}

void SetUpPlayerController()
{
    SetUpAi();
    SetUpAiNetwork();
    SetUpPlayerFakes();
    g_pp = new S_TestPlayer;
    g_op = static_cast<uint8_t *>(OC_AlignedNew(0x560, 16));
    typedef void(AC * T_Ctor)(void *, int);
    OriginalAt<T_Ctor>(0x00582ff0)(g_op, HUMAN_CONTROLLER_PLAYER);
    std::memcpy(g_opVt, OriginalAt<void *>(0x00811848), sizeof g_opVt);
    g_opVt[0xDC / 4] = reinterpret_cast<void *>(OA_ExecuteCommand);
    void *vt = g_opVt;
    std::memcpy(g_op, &vt, 4);
    std::memset(g_op + 0xC0, 0, 0x560 - 0xC0);
    g_names[g_pp] = g_names[g_op] = "player";
    g_extraPointers.push_back({g_pp, g_op});
    g_extraPointers.push_back({g_pp->m_controlAxes, g_op + 0xC4});
    g_extraPointers.push_back({g_pp->m_controlButtons, g_op + 0x1C4});
#ifndef HUMAN_AICOMBAT   // (the fighting's tests share them already)
    // The controls Tick latches into and clears: each side's kept apart and compared.
    ShareBlock(g_axes, sizeof g_axes, "axes");
    ShareBlock(g_buttons, sizeof g_buttons, "buttons");
#endif
}

// The network game's player states (LockPlayers: the port's array; the original's 0xC4-byte states behind the vector
// it returns, first at +4), every slot up to the humans' ids.
const int PL_SLOTS = 160;
alignas(16) uint8_t g_plPlayersPort[PL_SLOTS * sizeof(S_NetPlayerState)], g_plPlayersOrig[PL_SLOTS][0xC4];
uint32_t g_plPlayersVector[4];
void *AC PL_LockPlayers(void *) { return g_plPlayersVector; }
void AC PL_UnlockPlayers(void *) { ++g_unlockedPlayers; }
void SetUpPlayerSlots()
{
    g_lockedPlayers = reinterpret_cast<S_NetPlayerState *>(g_plPlayersPort);
    g_plPlayersVector[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_plPlayersOrig));
    g_plPlayersVector[2] = g_plPlayersVector[3] = g_plPlayersVector[1] + PL_SLOTS * 0xC4;
    RedirectOriginal(0x0072f040, FAKE(PL_LockPlayers));
    RedirectOriginal(0x0072f100, FAKE(PL_UnlockPlayers));
}
// The co-op lives: the local player's slot (as ToOriginal sets the original's), its lives used, the settings', the
// game's.
void RandomPlayerLives()
{
    C_Human *h = g_h.human;
    g_script.localPlayer = P(0.6f);
    g_localPlayerSlot = g_script.localPlayer ? h->m_netId : 99;
    for (int slot : {h->m_netId, 99}) {
        if (slot < 0 || slot >= PL_SLOTS)
            continue;
        const int used = I(-1, 5);
        reinterpret_cast<S_NetPlayerState *>(g_plPlayersPort)[slot].livesUsed = used;
        std::memcpy(g_plPlayersOrig[slot] + 0xB0, &used, 4);
    }
    g_mpSettings->teamLives = P(0.5f) ? 0 : I(1, 3);
    g_mpSettings->coopLives = I(-1, 5);
    g_mpSettings->thirdPersonView = P(0.5f) ? 0 : 1;
    const int teamUsed = I(0, 5);
    GetGame()->SetTeamLivesUsed(teamUsed);
    std::memcpy(g_origGame + 0x84, &teamUsed, 4);
    g_self.coop = P(0.5f);
}

// HandleInput's controls, a copy per side (mapped to each other: the controller keeps pointers to them).
alignas(16) float g_plHeld[2][CONTROL_COUNT], g_plPressed[2][CONTROL_COUNT];

// A squad member taken over (Attach's previous): part 23's second controller's copy with the squad type and the squad
// controller's three getters faked (the port's slots by member, the original's 0x118, 0x144, 0x148).
struct S_PlSquad {
    uint32_t flags = 0;
    bool quiet = false;
    int combatOrder = 0xC;
} g_plSquad;
alignas(16) uint8_t g_plSquadPort[sizeof(C_HumanController) + 0x40], g_plSquadOrig[0x100];
void *g_plSquadPortVt[0x80], *g_plSquadOrigVt[0x80];
uint32_t AC SQ_GetFlags(void *self)
{
    Log(self, "SquadGetFlags");
    return g_plSquad.flags;
}
int AC SQ_IsQuiet(void *self)
{
    Log(self, "SquadIsQuiet");
    return g_plSquad.quiet;
}
int AC SQ_GetCombatOrder(void *self)
{
    Log(self, "SquadGetCombatOrder");
    return g_plSquad.combatOrder;
}
void SetUpPlayerSquad()
{
    std::memcpy(g_plSquadPortVt, *reinterpret_cast<void ***>(g_pc2), 0x40 * sizeof(void *));
    std::memcpy(g_plSquadOrigVt, *reinterpret_cast<void ***>(g_oc2), 0x50 * sizeof(void *));
    g_plSquadPortVt[VSlot(&C_SquadController::GetFlags)] = reinterpret_cast<void *>(SQ_GetFlags);
    g_plSquadPortVt[VSlot(&C_SquadController::IsQuiet)] = reinterpret_cast<void *>(SQ_IsQuiet);
    g_plSquadPortVt[VSlot(&C_SquadController::GetCombatOrder)] = reinterpret_cast<void *>(SQ_GetCombatOrder);
    g_plSquadOrigVt[0x118 / 4] = reinterpret_cast<void *>(SQ_GetFlags);
    g_plSquadOrigVt[0x144 / 4] = reinterpret_cast<void *>(SQ_IsQuiet);
    g_plSquadOrigVt[0x148 / 4] = reinterpret_cast<void *>(SQ_GetCombatOrder);
    g_names[g_plSquadPort] = g_names[g_plSquadOrig] = "squadMember";
    g_names[g_plHeld[0]] = g_names[g_plHeld[1]] = "held";
    g_names[g_plPressed[0]] = g_names[g_plPressed[1]] = "pressed";
    g_extraPointers.push_back({g_plHeld[0], g_plHeld[1]});
    g_extraPointers.push_back({g_plPressed[0], g_plPressed[1]});
    g_extraPointers.push_back({g_plSquadPort, g_plSquadOrig});
}
// The squad member from part 23's second controller as it is now.
C_HumanController *PlayerSquadMember()
{
    std::memcpy(g_plSquadPort, static_cast<void *>(g_pc2), sizeof(C_HumanController));
    auto *c = reinterpret_cast<C_HumanController *>(g_plSquadPort);
    c->m_type = HUMAN_CONTROLLER_SQUAD;
    *reinterpret_cast<void ***>(g_plSquadPort) = g_plSquadPortVt;
    ControllerToOrig(c, g_plSquadOrig);
    void *vt = g_plSquadOrigVt;
    std::memcpy(g_plSquadOrig, &vt, 4);
    g_plSquad.flags = uint32_t(I(0, 0xFFFF));
    g_plSquad.quiet = P(0.5f);
    g_plSquad.combatOrder = I(0, 0x20);
    return c;
}

// The controller's state (part 23's), the player's fields, the game's: local or a network game's server or client,
// the camera, the HUD, the profile's detail settings.
// The port's weapon ready time's offset (private; found by GetReadyTime as part 33b's), the original's +0x5C.
size_t PlReadyTimeAt()
{
    static size_t found = 0;
    auto *w = reinterpret_cast<C_WeaponItem *>(g_weaponPort[0]);
    for (size_t at = 4; at + 4 <= 0x200 && !found; at += 4) {
        uint32_t keep;
        std::memcpy(&keep, g_weaponPort[0] + at, 4);
        const uint32_t marker = 0x13572468;
        std::memcpy(g_weaponPort[0] + at, &marker, 4);
        if (w->GetReadyTime() == marker)
            found = at;
        std::memcpy(g_weaponPort[0] + at, &keep, 4);
    }
    if (!found)
        CHECK(found != 0);
    return found;
}

void RandomPlayer()
{
    RandomAi();
    C_PlayerController *p = g_pp;
    RandomControllerFields(p);
    p->m_human = g_h.human;
    p->m_handsItem = g_pc->m_handsItem;
    p->m_toServer.clear();
    p->m_toClients.clear();
    std::memset(g_op + 0x80, 0, 0x20);
    g_h.human->m_controller = p;
    p->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
    // (Part 23's second controller not a squad member's type: that is the squad member's copy's.)
    if (g_pc2->m_type == HUMAN_CONTROLLER_SQUAD)
        g_pc2->m_type = HUMAN_CONTROLLER_ENEMY;
    p->m_whispering = P(0.5f);
    for (int i = 0; i < CONTROL_COUNT; ++i) {
        p->m_controlAxes[i] = P(0.7f) ? 0.0f : U(-1, 1);
        p->m_controlButtons[i] = P(0.8f) ? 0.0f : 1.0f;
        p->m_unknown2C4[i] = P(0.8f) ? 0.0f : U(-1, 1);
        p->m_unknown3C4[i] = P(0.8f) ? 0.0f : U(-1, 1);
    }
    p->m_latchThrow = P(0.6f) ? 0 : 1;
    p->m_latchFire = P(0.6f) ? 0 : 1;
    p->m_latchFire2 = P(0.6f) ? 0 : 1;
    p->m_latchReload = P(0.6f) ? 0 : 1;
    p->m_holdingBreath = P(0.4f);
    p->m_breath = P(0.4f) ? 3500 : I(0, 3500);
    p->m_pendingCommand.category = I(0, 5);
    p->m_pendingCommand.dir = AiDir();
    p->m_pendingCommand.action = I(0, 0x30);
    p->m_pendingCommand.command = I(-1, 0x26);
    p->m_pendingCommand.actor = AiActor();
    p->m_pendingCommand.value = I(0, 100);
    p->m_pendingCommand.member = P(0.5f) ? nullptr : PortOther();
    p->m_commandDelay = P(0.4f) ? 0 : I(-10, 2000);
    p->m_unknown524 = P(0.5f);
    p->m_ordersChanged = P(0.5f);
    p->m_squadFlags = uint32_t(I(0, 0xFFFF));
    p->m_viewMode = I(-1, 3);
    p->m_scopeDelay = P(0.5f) ? 0 : P(0.2f) ? I(-1, 1) : I(-100, 1000);
    p->m_raisedTime = I(0, 3000);
    p->m_charging = P(0.3f);
    p->m_chargeTime = I(0, 1000);
    p->m_throwDelay = P(0.5f) ? 0 : I(-10, 1000);
    p->m_chargeAction = I(0, 0xD);
    p->m_squadCombatOrder = I(0, 0x20);
    p->m_vip = P(0.3f);
    p->m_spectating = P(0.2f);
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT};
    g_gameRole = roles[I(0, 2)];
    g_pcs.cameraDir = P(0.8f);
    g_pcs.cameraView = AiDir();
    g_pcs.scriptedView = P(0.2f);
    g_pcs.canUseWeapon = P(0.7f);
    g_pcs.invertMouse = P(0.3f);
    g_plOptionsOrig[0x39] = g_pcs.invertMouse;
    reinterpret_cast<C_PlayerOptions *>(g_plOptionsPort)->invertMouse = g_pcs.invertMouse;
    g_pcs.whispering = P(0.5f);
    g_pcs.groupMembers = P(0.5f);
    g_pcs.seatFlags = (P(0.4f) ? 0x01u : 0u) | (P(0.3f) ? 0x100u : 0u) | (P(0.3f) ? 0x10u : 0u) | (P(0.4f) ? 0x20u : 0u);
    g_pcs.seatCamera = P(0.7f);
    g_pcs.occupant = P(0.6f);
    g_pcs.epochs[0] = g_pcs.epochs[1] = -1;
    std::memset(g_aiReplication + 0xC4, 0xFF, 8);
    reinterpret_cast<S_HudOption *>(g_hudOption)->shown = P(0.5f);
    RandomPlayerMission();
    RandomPlayerLives();
    // The pace (the keys step it), the stamina for marching; the spectator's camera (the game's +0xB8, +0xB4); the
    // human's frame turned (the seats' and the climbing's turn limit).
    static const uint32_t paces[] = {MOVE_PACE_1, MOVE_PACE_3, MOVE_PACE_4, MOVE_PACE_MARCH};
    if (P(0.7f))
        p->m_stateFlags = paces[I(0, 3)];
    g_h.human->m_stamina = U(0, 20);
    g_script.stanceFlags = P(0.5f) ? uint32_t(MOVE_STANDING) : P(0.5f) ? uint32_t(MOVE_CROUCHING) : 0u;
    // In part 23's car (actor kind 3) now and then (its seat's flags: armed or not); climbing now and then.
    if (P(0.3f))
        g_h.human->m_vehicle = static_cast<C_Actor *>(PortActor(3));
    g_cs.climbState = P(0.2f) ? I(1, 3) : 0;
    const int cameraMode = P(0.5f) ? 0 : I(1, 3), spectated = P(0.3f) ? -1 : (P(0.5f) ? g_h.human : PortOther())->GetNetId();
    GetGame()->*PlGet(GmCameraMode()) = cameraMode;
    GetGame()->*PlGet(GmSpectatedSlot()) = spectated;
    std::memcpy(g_origGame + 0xB8, &cameraMode, 4);
    std::memcpy(g_origGame + 0xB4, &spectated, 4);
    if (P(0.6f)) {
        // (Now and then tilted: the axes' sums with a height.)
        S_vector axis(0, 1, 0);
        if (P(0.2f)) {
            axis = S_vector(U(-1, 1), U(0.3f, 1), U(-1, 1));
            axis.Normalize();
        }
        S_quat turn;
        turn.Make(axis, U(0, 6.2831f));
        g_h.frame->SetRot(turn);
        UpdateTree(g_h.frame);
    }
    // (The scope with a hands item: holding the breath reads it, the original unchecked.)
    if (p->m_viewMode == C_PlayerController::VIEW_SCOPE) {
        g_handsWeapon = g_weaponPort[I(0, 1)];
        g_wornContainers[ITEM_CONTAINER_HANDS].item = g_handsWeapon;
    }
    // The weapons' ready times about the mission time (holding the breath waits for it).
    const uint32_t now = GetGame()->GetMissionTime();
    for (int k = 0; k < 2; ++k) {
        const uint32_t ready = P(0.5f) ? 0u : now + uint32_t(I(-2, 2)) + (P(0.5f) ? 0u : uint32_t(I(-500, 500)));
        std::memcpy(g_weaponPort[k] + PlReadyTimeAt(), &ready, 4);
        std::memcpy(g_weaponOrig[k] + 0x5C, &ready, 4);
    }
    if (P(0.05f))
        p->m_throwDelay = 1;
}

void TestPlayerA()
{
    std::printf("player controller: life cycle, input, breath\n");
    // (Not fewer in the quick build: 8 s whole.)
    for (int i = 0; i < 12000; ++i) {
        RandomPlayer();
        C_PlayerController *p = g_pp;
        const int dt = I(0, 200);
        switch (I(0, 7)) {
        case 0: {
            C_HumanController *previous = P(0.4f) ? nullptr : P(0.5f) ? static_cast<C_HumanController *>(g_pc2) : PlayerSquadMember();
            PlCall("Attach", [&] { p->Attach(g_h.human, previous); return 0; },
                   [&](void *) { OPl<void>(0x0053b9a0, ToOrig(g_h.human), previous ? ToOrig(previous) : nullptr); return 0; });
            // (The replication's epochs: the port's kept by its fakes, the original's written inline at +0xC4 / +0xC8.)
            const bool same = std::memcmp(g_aiReplication + 0xC4, g_pcs.epochs, 8) == 0;
            CHECK(same);
            static int shown;
            if (!same && shown++ < 3)
                std::printf("  Attach: the replication's epochs\n");
            break;
        }
        case 1:
            PlCall("Detach", [&] { p->Detach(); return 0; }, [&](void *) { OPl<void>(0x0053bc50); return 0; });
            break;
        case 2:
            PlCall("OnDeath", [&] { p->OnDeath(); return 0; }, [&](void *) { OPl<void>(0x0053bd00); return 0; });
            break;
        case 3:
            PlCall("OnKilled", [&] { p->OnKilled(); return 0; }, [&](void *) { OPl<void>(0x0053be10); return 0; });
            break;
        case 4:
            // (The timers now and then running out exactly, or one short.)
            if (P(0.2f))
                p->m_throwDelay = dt + I(-1, 1);
            if (P(0.2f))
                p->m_commandDelay = dt + I(-1, 2);
            PlCall("Update", [&] { p->Update(dt); return 0; }, [&](void *) { OPl<void>(0x00540090, dt); return 0; });
            break;
        case 5:
            PlCall("Tick", [&] { p->Tick(dt); return 0; }, [&](void *) { OPl<void>(0x00540240, dt); return 0; });
            break;
        case 6: {
            // The controls (each side's copy: HandleInput writes them; mapped to each other).
            float (&held)[2][CONTROL_COUNT] = g_plHeld;
            float (&pressed)[2][CONTROL_COUNT] = g_plPressed;
            for (int k = 0; k < CONTROL_COUNT; ++k) {
                held[0][k] = held[1][k] = P(0.7f) ? 0.0f : U(-1, 1);
                pressed[0][k] = pressed[1][k] = P(0.85f) ? 0.0f : 1.0f;
            }
            // (The pace keys more often: one at a time.)
            if (P(0.4f)) {
                const bool up = P(0.5f);
                pressed[0][4] = pressed[1][4] = up ? 0.0f : 1.0f;
                pressed[0][5] = pressed[1][5] = up ? 1.0f : 0.0f;
            }
            // (The mouse now and then far: the yaw past a full turn; now and then exactly one step across.)
            for (int axis : {0x34, 0x35})
                if (P(0.4f))
                    held[0][axis] = held[1][axis] = P(0.2f) ? float(I(-1, 1)) : U(-8, 8);
            // (Now and then up to marching: pace 4, the key up, the endurance against the stamina.)
            if (P(0.15f)) {
                p->m_stateFlags = MOVE_PACE_4;
                pressed[0][4] = pressed[1][4] = 0.0f;
                pressed[0][5] = pressed[1][5] = 1.0f;
                g_h.human->m_stats[STAT_ENDURANCE] = I(0, 200);
            }
            // (Now and then a weapon in the hands: the drop key's.)
            if (P(0.2f)) {
                g_handsWeapon = g_weaponPort[I(0, 1)];
                g_wornContainers[ITEM_CONTAINER_HANDS].item = g_handsWeapon;
            }
            const bool none = P(0.05f);
            PlCall("HandleInput", [&] { p->HandleInput(none ? nullptr : held[0], pressed[0]); return 0; },
                   [&](void *) { OPl<void>(0x0053bf90, none ? nullptr : held[1], pressed[1]); return 0; });
            const bool same = std::memcmp(held[0], held[1], sizeof held[0]) == 0 && std::memcmp(pressed[0], pressed[1], sizeof pressed[0]) == 0;
            CHECK(same);
            static int shown;
            if (!same && shown++ < 3)
                std::printf("  HandleInput: the controls\n");
            break;
        }
        default: {
            const bool on = P(0.5f);
            // (The scope with a hands item: the original reads a missing one; the port checks.)
            if (on && p->m_viewMode == C_PlayerController::VIEW_SCOPE) {
                g_handsWeapon = g_weaponPort[I(0, 1)];
                g_wornContainers[ITEM_CONTAINER_HANDS].item = g_handsWeapon;
            }
            PlCall("HoldBreath", [&] { p->HoldBreath(on); return 0; }, [&](void *) { OPl<void>(0x0053cd90, int(on)); return 0; });
            break;
        }
        }
    }
}

}  // namespace

// ---- part 40b: the squad commands, the radio's target, the soldier's comments, the sight, the sounds heard, the aim
// The collision world's ray test (DesignateTarget's): the port's wrapped, the original's thunk (0x00718830) redirected;
// answered from here while g_plRay.on (the port's own otherwise).
namespace {
struct S_PlRay {
    bool on = false;
    bool hit = true;
    S_vector pos = {0, 0, 0};
} g_plRay;
uint8_t g_plRayVolume[0x40];
void *PlRayAnswer(const S_vector *from, const S_vector *dir, S_vector *hitPos, const S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    Log(nullptr, "RayTest", {from->x, from->y, from->z, dir->x, dir->y, dir->z, double(hitNormal != nullptr), double(types), double(flags)});
    if (!g_plRay.hit)
        return nullptr;
    if (hitPos)
        *hitPos = g_plRay.pos;
    return g_plRayVolume;
}
void *CT_ PL_RayTestOrig(void *, const S_vector *from, const S_vector *dir, S_vector *hitPos, S_vector *hitNormal, uint32_t types,
                         uint32_t flags)
{
    return PlRayAnswer(from, dir, hitPos, hitNormal, types, flags);
}
}  // namespace
extern "C" S_CollisionVolume *__attribute__((thiscall))
__real__ZNK16C_CollisionWorld7RayTestERK8S_vectorS2_PS0_S3_jj(const C_CollisionWorld *, const S_vector &, const S_vector &, S_vector *,
                                                              S_vector *, uint32_t, uint32_t);
extern "C" S_CollisionVolume *__attribute__((thiscall))
__wrap__ZNK16C_CollisionWorld7RayTestERK8S_vectorS2_PS0_S3_jj(const C_CollisionWorld *world, const S_vector &from, const S_vector &dir,
                                                              S_vector *hitPos, S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    if (!g_plRay.on)
        return __real__ZNK16C_CollisionWorld7RayTestERK8S_vectorS2_PS0_S3_jj(world, from, dir, hitPos, hitNormal, types, flags);
    return static_cast<S_CollisionVolume *>(PlRayAnswer(&from, &dir, hitPos, hitNormal, types, flags));
}

namespace {
// The mission's type and its two aircraft kinds (the original's +0x50 axis, +0x54 allied).
struct MnType {
    using type = int C_CampaignNode::*;
    friend type PlGet(MnType);
};
template struct S_PlRob<MnType, &C_CampaignNode::m_type>;
struct MpAxis {
    using type = int C_MpMissionDef::*;
    friend type PlGet(MpAxis);
};
template struct S_PlRob<MpAxis, &C_MpMissionDef::m_aircraftAttackAxis>;
struct MpAllied {
    using type = int C_MpMissionDef::*;
    friend type PlGet(MpAllied);
};
template struct S_PlRob<MpAllied, &C_MpMissionDef::m_aircraftAttackAllied>;

// The other human's body frame (the sight measures from body to body).
I3D_frame *g_plOtherBody, *g_plSelfBody;

void SetUpPlayerB()
{
    RedirectOriginal(0x00718830, FAKE(PL_RayTestOrig));
    RedirectOriginal(0x00475dd0, FAKE(OC_GetActor));
    RedirectOriginal(0x005a63f0, FAKE(OC_IsSpectating));
    RedirectOriginal(0x005a63b0, FAKE(OC_IsInPlay));
    g_actorByIndex = PlayerAt;
    g_plRay.on = true;
    g_names[g_plRayVolume] = "rayVolume";
    CHECK(g_otherFrame != nullptr);
    S_quat none;
    none.Identity();
    g_plOtherBody = NewFrame(FRAME_DUMMY, "otherBody", S_vector(0, 1.2f, 0), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_otherFrame);
    g_plSelfBody = NewFrame(FRAME_DUMMY, "selfBody", S_vector(0, 1.2f, 0), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_h.frame);
}

// The state for part 40b: part 40a's, the mission's kinds, the ray's answer, the radios, the players around, the other
// human placed (its frame, body and position), the stances and paces.
void RandomPlayerB()
{
    RandomPlayer();
    C_PlayerController *p = g_pp;
    // The speech (part 23's RandomSpeech: a set or none, the delay, the code and its sample's length).
    g_h.human->m_speechSet = P(0.1f) ? nullptr : reinterpret_cast<C_SpeechSet *>(g_speechSetBytes2);
    p->m_speechDelay = P(0.7f) ? 0 : I(-5, 2000);
    g_cs.speech = P(0.1f) ? I(-3, 0) : P(0.5f) ? I(0, 40000) : int(uint32_t(I(0, 0x7FFFFFFF)));
    g_cs.sampleSize = P(0.1f) ? 0 : I(1, 3000);
    auto *mp = reinterpret_cast<C_MpMissionDef *>(PlMission());
    // (Multiplayer missions only: the original reads the kinds past a single player one; the port has 0.)
    PlMission()->*PlGet(MnType()) = CAMPAIGN_NODE_MP_MISSION;
    const int axis = I(-1, 15), allied = I(-1, 15);
    mp->*PlGet(MpAxis()) = axis;
    mp->*PlGet(MpAllied()) = allied;
    std::memcpy(g_plMissionOrig + 0x50, &axis, 4);
    std::memcpy(g_plMissionOrig + 0x54, &allied, 4);
    g_plRay.hit = P(0.8f);
    g_plRay.pos = S_vector(U(-200, 200), U(-5, 30), U(-200, 200));
    g_cs.found = P(0.7f) ? reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]) : nullptr;
    for (int &who : g_players)
        who = P(0.85f) ? 0 : I(1, 2);
    g_cs.spectating = P(0.2f);
    g_cs.inPlay = P(0.8f);
    const int health = P(0.2f) ? I(-1, 0) : I(1, 100);
    PortOther()->m_stats[STAT_HEALTH] = health;
    std::memcpy(g_origOther + 0xEC, &health, 4);
    // The other human about the tested one (up to 45 m: the comments' 20 m, the sight's range).
    const S_vector at = g_h.frame->GetWorldPos() + S_vector(U(-1, 1), U(-0.2f, 0.2f), U(-1, 1)).Normalized() * U(0, 45);
    g_otherFrame->SetPosInline(at);
    UpdateTree(g_otherFrame);
    PortOther()->m_frame = g_otherFrame;
    std::memcpy(g_origOther + 0x20, &g_otherFrame, 4);
    PortOther()->m_bodyFrame = g_plOtherBody;
    std::memcpy(g_origOther + 0x260, &g_plOtherBody, 4);
    const S_vector4 pos = {at.x, at.y, at.z, 1.0f};
    PortOther()->m_position = pos;
    std::memcpy(g_origOther + 0x30, &pos, 16);
    g_h.human->m_position = {g_h.frame->GetWorldPos().x, g_h.frame->GetWorldPos().y, g_h.frame->GetWorldPos().z, 1.0f};
    g_pcs.enemyKnown = P(0.5f);
    g_pcs.viewDistance = P(0.5f) ? 100.0f : U(5, 60);
    // The stance and the pace (the aim).
    static const uint32_t stances[] = {MOVE_STANDING, MOVE_CROUCHING, 0};
    static const uint32_t paces[] = {0, MOVE_PACE_1, MOVE_PACE_3, MOVE_PACE_4, MOVE_PACE_MARCH};
    p->m_moveFlags = (p->m_moveFlags & MOVE_DEAD) | stances[I(0, 2)] | (P(0.5f) ? (MOVE_MOVING_MASK & (1u << I(0, 31))) : 0u) |
                     paces[I(0, 4)];
    g_h.human->m_stats[STAT_SHOOTING] = I(0, 100);
}

// A squad command's message: the port's and the original's (pos +0x10, the command +0x20 with its pointers mapped).
alignas(16) uint8_t g_plActionOrig[0x60];
void RandomAction(S_MsgAction &m, int action)
{
    m.pos = AiDir();
    S_ActionCommand &c = m.command;
    c.category = I(0, 5);
    c.dir = AiDir();
    c.action = action;
    c.command = I(-1, 0x28);
    c.actor = AiActor();
    c.value = I(0, 100);
    c.member = P(0.5f) ? nullptr : PortOther();
    std::memset(g_plActionOrig, 0, sizeof g_plActionOrig);
    std::memcpy(g_plActionOrig + 0x10, &m.pos, 16);
    CopyMapped(reinterpret_cast<const uint8_t *>(&c) + 4, g_plActionOrig + 0x24, 0x30);
}
int RandomSquadAction()
{
    if (P(0.15f))
        return P(0.5f) ? SQUAD_COMMAND_AIR_ATTACK : SQUAD_COMMAND_ARTILLERY;
    if (P(0.2f))
        return P(0.5f) ? SQUAD_COMMAND_ON_ACTOR : SQUAD_COMMAND_ON_ACTOR_QUIET;
    // (Up to 0x3D: the original reads on past its tables for higher ones; the port has none.)
    return I(0, 0x3D);
}

void TestPlayerB()
{
    std::printf("player controller: squad commands, the radio, comments, sight, sounds, aim\n");
    for (int i = 0; i < 12000; ++i) {
        RandomPlayerB();
        C_PlayerController *p = g_pp;
        switch (I(0, 11)) {
        case 0: {
            const int command = P(0.8f) ? I(0, 0x14) : I(0x15, 0x40);
            PlCall("SetWhisperingFor", [&] { C_PlayerController::SetWhisperingFor(command); return 0; },
                   [&](void *) { OriginalAt<void(__stdcall *)(int)>(0x0053d520)(command); return 0; });
            break;
        }
        case 1:
            PlCall("ExecutePendingCommand", [&] { p->ExecutePendingCommand(); return 0; }, [&](void *) { OPl<void>(0x0053d5b0); return 0; });
            break;
        case 2:
        case 3: {
            S_MsgAction m;
            RandomAction(m, RandomSquadAction());
            PlCall("OnSquadCommand", [&] { return int(p->OnSquadCommand(m)); },
                   [&](void *) { return int(OPl<bool>(0x0053d5d0, static_cast<void *>(g_plActionOrig))); });
            break;
        }
        case 4: {
            S_MsgAction m;
            RandomAction(m, RandomSquadAction());
            PlCall("PlayCommandGesture", [&] { p->PlayCommandGesture(m); return 0; },
                   [&](void *) { OPl<void>(0x0053d870, static_cast<void *>(g_plActionOrig)); return 0; });
            break;
        }
        case 5: {
            const int command = P(0.45f) ? SQUAD_COMMAND_AIR_ATTACK : P(0.8f) ? SQUAD_COMMAND_ARTILLERY : I(0, 0x40);
            const S_vector origin(U(-50, 50), U(0, 5), U(-50, 50));
            const S_vector4 d = AiDir();
            const S_vector dir(d.x, d.y, d.z);
            PlCall("DesignateTarget", [&] { return int(p->DesignateTarget(command, origin, dir)); },
                   [&](void *) { return int(OPl<bool>(0x005467c0, command, &origin, &dir)); });
            break;
        }
        case 6: {
            const int sentence = I(0, 0x8A), delay = I(0, 5000);
            PlCall("SayNear", [&] { p->SayNear(sentence, delay); return 0; }, [&](void *) { OPl<void>(0x00542060, sentence, delay); return 0; });
            break;
        }
        case 7: {
            C_Human *enemy = PortOther();
            if (P(0.5f))
                PlCall("OnEnemySeen", [&] { p->OnEnemySeen(enemy); return 0; },
                       [&](void *) { OPl<void>(0x005421c0, ToOrig(enemy)); return 0; });
            else
                PlCall("OnEnemyLost", [&] { p->OnEnemyLost(enemy); return 0; },
                       [&](void *) { OPl<void>(0x005422b0, ToOrig(enemy)); return 0; });
            break;
        }
        case 8: {
            C_Human *victim = PortOther();
            PlCall("OnKilledEnemy", [&] { p->OnKilledEnemy(victim); return 0; },
                   [&](void *) { OPl<void>(0x00542300, ToOrig(victim)); return 0; });
            break;
        }
        case 9: {
            C_Human *target = PortOther();
            // (The tested human's body its own frame, turned and now and then tilted: the triangle's axes.)
            I3D_frame *const body = g_h.human->m_bodyFrame;
            S_vector axis(0, 1, 0);
            if (P(0.4f)) {
                axis = S_vector(U(-1, 1), U(0.2f, 1), U(-1, 1));
                axis.Normalize();
            }
            S_quat turn;
            turn.Make(axis, U(0, 6.2831f));
            g_plSelfBody->SetRot(turn);
            UpdateTree(g_plSelfBody);
            g_h.human->m_bodyFrame = g_plSelfBody;
            PlCall("TestSightOfHuman",
                   [&] {
                       int status = -7;
                       float visibility = -7.0f;
                       p->TestSightOfHuman(target, &status, &visibility);
                       return status * 4 + (visibility == 100.0f ? 1 : visibility == 0.0f ? 0 : visibility == -7.0f ? 2 : 3);
                   },
                   [&](void *) {
                       int status = -7;
                       float visibility = -7.0f;
                       OPl<void>(0x00542320, ToOrig(target), &status, &visibility);
                       return status * 4 + (visibility == 100.0f ? 1 : visibility == 0.0f ? 0 : visibility == -7.0f ? 2 : 3);
                   });
            g_h.human->m_bodyFrame = body;
            break;
        }
        case 10: {
            S_RememberedSound sound = {};
            sound.pos = S_vector(U(-50, 50), U(-5, 5), U(-50, 50));
            sound.posW = 1.0f;
            sound.chapter = P(0.6f) ? (P(0.33f) ? 0 : P(0.5f) ? 2 : 6) : I(0, 8);
            PlCall("OnSoundHeard", [&] { p->OnSoundHeard(&sound); return 0; }, [&](void *) { OPl<void>(0x005449b0, &sound); return 0; });
            break;
        }
        default:
            PlCall("GetAimBonus", [&] { return int(FloatBits(p->GetAimBonus())); },
                   [&](void *) { return int(FloatBits(OPl<float>(0x00542b10))); });
            break;
        }
    }
}

void TestPlayerController()
{
    SetUpPlayerController();
    SetUpPlayerSquad();
    SetUpPlayerMission();
    SetUpPlayerSlots();
    TestPlayerA();
    SetUpPlayerB();
    TestPlayerB();
}
}  // namespace
