// Part 44 of the human harness (HUMAN_SQUAD builds, build_squad.bat; included by human_test.cpp after aicombat_test.inc,
// whose fakes and helpers it uses): the squad AI (game/squad_controller.cpp, squad_actions.cpp, squad_combat.cpp,
// squad_movement.cpp) and its orders (squad_order.cpp) against the original's, on the derived AI controllers' common
// harness (derived_ai.inc).
//
// The port's squad controller (a test class: ExecuteCommand logged) is the AI pair's port side; the original's is the
// AI constructor's object with a copy of its vtable (0x008109f8, 0x260 bytes). Its own fields (+0x140 .. +0x260: the
// same offsets on both sides but for the order queue, an MSVC6 vector there) are converted and compared around each
// call; the orders are each side's own (the port's S_HumanOrder, the original's 0x130-byte object with its vtable
// 0x008105a0), made alike, mapped and compared by their fields.
//
// Faked on both sides and logged (the answers the test's, g_sq): the squad commander (the port's object with a fake
// vtable; the original's global 0x0089ad40 with its own vtable, its members redirected); the player team's selected
// member, its members, the enemies it knows and sees; the formation's slots; the car's seat entry, input, crew side and
// entry; the human's last name, medkit, controller type, item counts and collection, requests and commands; the walking
// network's path length, cover, names, actors and points; the wire's cut; the items module's room; the air attacks,
// the Junkers and the submarines; the temporary actors' mines and items; the enemy records' Remove; the command panel's
// order markers; the map path's points; the body's sector and spheres; the collision world's ray tests.
#include "derived_ai.inc"
#include "game/squad_commander.h"
#include "game/squad_order.h"
#include "game/interface.h"
#include "game/car.h"
#include "game/wire_cut.h"
#include "game/submarine.h"
#include "game/junkers88.h"
#include "game/air_attack.h"
#include "game/items_module.h"
#include "game/item_actor.h"

namespace {
// The fakes' answers.
struct S_SqAnswers {
    // the commander
    bool whispering = false;
    C_Actor *squadTarget = nullptr;                     // GetSquadTarget (the port's)
    // the player team
    C_Human *selected = nullptr;                        // GetSelectedMember
    int memberCount = 3;                                // GetMemberCount (members: the human, the other, the third)
    int aliveCount = 3;
    bool enemyKnown = false;                            // IsEnemyKnown
    C_Human *seenNearby = nullptr;                      // GetEnemySeenNearby
    C_Human *unseenBy = nullptr;                        // GetEnemyUnseenBy
    // the formation
    bool slot = true;                                   // FindSlot, GetSlotTarget, GetSlotDir
    int slotIndex = 0;
    S_vector4 slotPos = {};
    uint16_t slotPoint = 0;
    S_vector4 slotDir = {};
    int slotSeat = -1;                                  // GetSlotSeat
    int slotVehicle = -1;
    // the car
    bool seatEntry = true;                              // GetSeatEntry
    S_vector4 entryPos = {}, entryDir = {};
    bool localAboard = false;                           // HasLocalPlayerAboard
    int crewSide = 0;                                   // GetCrewSide
    bool canEnter = true;                               // CanEnter
    // the human
    bool medkit = false;                                // GetMedkit
    int controllerType = 0;                             // GetControllerType
    int itemCount = 0;                                  // CountItems
    int collected = 0;                                  // CollectItems: how many (the weapons, the belt)
    // the network
    float pathLength = 10.0f;                           // GetPathLength
    std::vector<uint16_t> coverWithin;                  // FindCoverWithin
    std::vector<uint16_t> byPrefix;                     // FindByPrefix
    int pointActor[8] = {};                             // GetActor by point: -1 none, else part 23's kind
    int actorPoint = 0xFFFF;                            // FindActorPoint
    // others
    bool wireCut = false;                               // IsCut
    int canPut = 1;                                     // CanPutItem
    C_Actor *airAttack = nullptr;                       // FindAirAttackFor
    C_Actor *junkers = nullptr;                         // FindJunkersTargetFor (its frame: the target frame)
    bool mineInRange = false;                           // IsMineInRange
    C_Actor *itemNear = nullptr;                        // FindItemNear
    bool outdoors = true;                               // GetSector: the scene's primary sector
    int spheres = 0;                                    // GetUnknown6E0's count
    bool rayHit = false;                                // RayTest
    S_vector rayNormal = {0, 1, 0};                     // ... its hit normal
    int dynamicHit = -1;                                // RayTestDynamic: -1 none, else part 23's kind
    S_vector facing = {0, 0, 1};                        // C_Human::GetFacingDir
    // the seat car's seats
    int seatCount = 2;                                  // GetSeatCount
    int occupant = -1;                                  // GetSeatOccupant: -1 none, 0 the human, 1 the other
    int rounds = 0;                                     // GetSeatWeaponRounds
    S_vector origin = {0, 1, 0};                        // GetSeatWeaponOrigin
    int driving = 0;                                    // UpdateDriving
    std::vector<uint16_t> path;                         // FindPath (none: no path)
    bool camera = false;                                // GetSeatCamera: the camera frame or none
} g_sq;
alignas(16) S_vector4 g_sqSpheres[4];
alignas(16) uint8_t g_sqSectorOther[0x20];
alignas(16) uint8_t g_sqVolume[0x80];
const char g_sqLastName[] = "Tester";
// The submarines: none, or the seat car (each side's vector: the port's and an MSVC6 one).
std::vector<C_Actor *> g_sqSubmarines;
alignas(16) uint8_t g_sqOrigSubmarines[16];

C_Actor *SqPointActor(int index)
{
    if (index < 0 || index >= 8 || g_sq.pointActor[index] < 0)
        return nullptr;
    return static_cast<C_Actor *>(PortActor(g_sq.pointActor[index]));
}
void *SqOrigOf(const void *p) { return p ? ToOrig(p) : nullptr; }
I3D_sector *SqSector()
{
    return g_sq.outdoors ? GetGame()->GetScene()->GetPrimarySector() : reinterpret_cast<I3D_sector *>(g_sqSectorOther);
}
// An answer into an original's MSVC6 vector of 16-bit points (appended, as the flood fill does).
void SqVectorAnswer(uint32_t *out, const std::vector<uint16_t> &items)
{
    std::vector<uint16_t> all;
    if (out[1]) {
        const auto *first = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(out[1]));
        all.assign(first, first + (out[2] - out[1]) / 2);
    }
    all.insert(all.end(), items.begin(), items.end());
    out[1] = out[2] = out[3] = 0;
    if (!all.empty()) {
        auto *data = static_cast<uint16_t *>(std::malloc(all.size() * 2));
        std::memcpy(data, all.data(), all.size() * 2);
        out[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        out[2] = out[3] = out[1] + 2 * static_cast<uint32_t>(all.size());
    }
}
std::vector<uint16_t> SqVectorOf(const uint32_t *v)
{
    if (!v[1])
        return {};
    const auto *first = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(v[1]));
    return std::vector<uint16_t>(first, first + (v[2] - v[1]) / 2);
}
void SqLogPoints(const void *self, const char *what, const std::vector<uint16_t> &points, std::initializer_list<double> more)
{
    std::vector<double> nums(more);
    nums.push_back(-1000);
    nums.insert(nums.end(), points.begin(), points.end());
    g_calls.push_back({NameOf(self) + "." + what, nums});
}
#ifndef HUMAN_COMMANDER
// The commander's fake objects (the port's: raw bytes with a fake vtable).
alignas(16) uint8_t g_sqCommanderPort[0x200];
void *g_sqCommanderPortVt[64];
#endif
C_Human *SqMember(int index)
{
    C_Human *const members[3] = {g_h.human, PortOther(), PortThird()};
    return index >= 0 && index < 3 && index < g_sq.memberCount ? members[index] : nullptr;
}
}  // namespace

// ---- the port's side of the fakes
#ifndef HUMAN_COMMANDER
C_SquadCommander *GetSquadCommander() { return reinterpret_cast<C_SquadCommander *>(g_sqCommanderPort); }
void C_SquadCommander::OnOrderMoveDone() { Log(this, "CommanderOnOrderMoveDone"); }
#endif
C_Human *C_Team::GetSelectedMember() const
{
    Log(this, "TeamGetSelectedMember");
    return g_sq.selected;
}
void C_Team::ReleaseHoldingMembers(const C_Human *human) { Log(this, ("TeamReleaseHoldingMembers " + NameOf(human)).c_str()); }
bool C_Team::IsEnemyKnown(const C_Human *enemy) const
{
    Log(this, ("TeamIsEnemyKnown " + NameOf(enemy)).c_str());
    return g_sq.enemyKnown;
}
C_Human *C_Team::GetEnemySeenNearby(const C_Human *human) const
{
    Log(this, ("TeamGetEnemySeenNearby " + NameOf(human)).c_str());
    return g_sq.seenNearby;
}
C_Human *C_Team::GetEnemyUnseenBy(const C_Human *human, bool skipCarCrew) const
{
    Log(this, ("TeamGetEnemyUnseenBy " + NameOf(human)).c_str(), {double(skipCarCrew)});
    return g_sq.unseenBy;
}
int C_TeamBase::GetMemberCount() const
{
    Log(this, "TeamGetMemberCount");
    return g_sq.memberCount;
}
C_Human *C_TeamBase::GetMember(int index) const
{
    Log(this, "TeamGetMember", {double(index)});
    return SqMember(index);
}
int C_TeamBase::GetAliveCount() const
{
    Log(this, "TeamGetAliveCount");
    return g_sq.aliveCount;
}
void C_EnemyRecords::Remove(C_Human *human) { Log(nullptr, ("EnemyRecordsRemove " + NameOf(human)).c_str()); }
bool C_FormationShape::FindSlot(const C_Human *member, int *slot) const
{
    Log(this, ("FormationFindSlot " + NameOf(member)).c_str());
    *slot = g_sq.slotIndex;
    return g_sq.slot;
}
bool C_FormationShape::GetSlotTarget(const C_Human *member, S_vector4 *pos, uint16_t *checkpoint)
{
    Log(this, ("FormationGetSlotTarget " + NameOf(member)).c_str());
    *pos = g_sq.slotPos;
    if (checkpoint)   // (the callers may pass none)
        *checkpoint = g_sq.slotPoint;
    return g_sq.slot;
}
bool C_FormationShape::GetSlotDir(const C_Human *member, S_vector4 *dir)
{
    Log(this, ("FormationGetSlotDir " + NameOf(member)).c_str());
    *dir = g_sq.slotDir;
    return g_sq.slot;
}
int C_FormationShape::GetSlotSeat(const C_Human *member, int *vehicleId)
{
    Log(this, ("FormationGetSlotSeat " + NameOf(member)).c_str());
    *vehicleId = g_sq.slotVehicle;
    return g_sq.slotSeat;
}
void C_FormationShape::InvalidateSlot(const C_Human *member) { Log(this, ("FormationInvalidateSlot " + NameOf(member)).c_str()); }
void C_FormationShape::SetSlotTarget(const C_Human *member, const S_vector4 &pos)
{
    Log(this, ("FormationSetSlotTarget " + NameOf(member)).c_str(), {pos.x, pos.y, pos.z});
}
void C_FormationShape::PlanSlot(int slot) { Log(this, "FormationPlanSlot", {double(slot)}); }
bool C_Car::GetSeatEntry(int seat, S_vector4 *pos, S_vector4 *dir)
{
    Log(this, "CarGetSeatEntry", {double(seat)});
    *pos = g_sq.entryPos;
    *dir = g_sq.entryDir;
    return g_sq.seatEntry;
}
bool C_Car::SetInput(float throttle, float steering)
{
    Log(this, "CarSetInput", {throttle, steering});
    return true;
}
bool C_Car::HasLocalPlayerAboard()
{
    Log(this, "CarHasLocalPlayerAboard");
    return g_sq.localAboard;
}
int C_Car::GetCrewSide()
{
    Log(this, "CarGetCrewSide");
    return g_sq.crewSide;
}
bool C_Car::CanEnter()
{
    Log(this, "CarCanEnter");
    return g_sq.canEnter;
}
int C_AIController::UpdateDriving(int timeDelta)
{
    Log(this, "UpdateDriving", {double(timeDelta)});
    return g_sq.driving;
}
const std::vector<C_Actor *> &GetSubmarines()
{
    Log(nullptr, "GetSubmarines");
    return g_sqSubmarines;
}
C_Actor *FindJunkersTargetFor(C_Human *human, I3D_frame **frame)
{
    Log(human, "FindJunkersTargetFor");
    *frame = g_sq.junkers ? g_targetFrame : nullptr;
    return g_sq.junkers;
}
C_Actor *FindAirAttackFor(C_Human *human)
{
    Log(human, "FindAirAttackFor");
    return g_sq.airAttack;
}
bool C_TemporaryActorsList::IsMineInRange(const S_vector &pos, int range) const
{
    Log(this, "IsMineInRange", {pos.x, pos.y, pos.z, double(range)});
    return g_sq.mineInRange;
}
C_Actor *C_TemporaryActorsList::FindItemNear(const S_vector &pos, float range) const
{
    Log(this, "FindItemNear", {pos.x, pos.y, pos.z, range});
    return g_sq.itemNear;
}
bool C_WireCut::IsCut() const
{
    Log(this, "WireIsCut");
    return g_sq.wireCut;
}
int C_ItemsModule::CanPutItem(C_Actor *actor, C_Item *item)
{
    Log(this, ("CanPutItem " + NameOf(actor) + " " + NameOf(item)).c_str());
    return g_sq.canPut;
}
I3D_frame *SqMarkerFor(bool original);
I3D_frame *C_CommandPanel::AcquireOrderMarker(S_HumanOrder *order)
{
    Log(nullptr, "AcquireOrderMarker");
    return SqMarkerFor(false);
}
void C_CommandPanel::ReleaseOrderMarker(I3D_frame *marker) { Log(nullptr, ("ReleaseOrderMarker " + NameOf(marker)).c_str()); }
void C_CommandPanel::UpdateOrderMarker(S_HumanOrder *order, I3D_frame *marker)
{
    Log(nullptr, ("UpdateOrderMarker " + NameOf(order) + " " + NameOf(marker)).c_str());
}
// (The built path: a point per walking point, evenly between the ends, and the end, w 1 (never empty, as the game's:
// the original's UpdatePath reads its last point); _aligned_malloc'd, the original's free is msvcrt's too.)
S_vector4 *SqPathPoints(const S_vector4 &from, const S_vector4 &to, size_t n)
{
    auto *data = static_cast<S_vector4 *>(_aligned_malloc(sizeof(S_vector4) * n, 16));
    for (size_t k = 0; k < n; ++k) {
        const float t = float(k + 1) / float(n);
        data[k] = {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t, 1.0f};
    }
    return data;
}
void BuildPathPoints(const S_vector4 &from, const S_vector4 &to, const std::vector<uint16_t> &path, S_Vector4Array &out)
{
    SqLogPoints(nullptr, "BuildPathPoints", path, {from.x, from.y, from.z, to.x, to.y, to.z});
    out.data = SqPathPoints(from, to, path.size() + 1);
    out.capacity = out.count = int(path.size() + 1);
}
float C_CheckpointNetwork::GetPathLength(const std::vector<uint16_t> &path) const
{
    SqLogPoints(this, "GetPathLength", path, {});
    return g_sq.pathLength;
}
bool C_CheckpointNetwork::FindCoverWithin(const S_vector &pos, float radius, std::vector<uint16_t> *out) const
{
    Log(this, "FindCoverWithin", {pos.x, pos.y, pos.z, radius, double(out->size())});
    out->insert(out->end(), g_sq.coverWithin.begin(), g_sq.coverWithin.end());
    return true;
}
void C_CheckpointNetwork::FindByPrefix(const char *prefix, std::vector<uint16_t> &out) const
{
    Log(this, (std::string("FindByPrefix ") + prefix).c_str(), {double(out.size())});
    out.insert(out.end(), g_sq.byPrefix.begin(), g_sq.byPrefix.end());
}
C_Actor *C_CheckpointNetwork::GetActor(int index) const
{
    Log(this, "CheckpointsGetActor", {double(index & 0xFFFF)});
    return SqPointActor(index & 0xFFFF);
}
int C_CheckpointNetwork::FindActorPoint(const C_Actor *actor) const
{
    Log(this, ("CheckpointsFindActorPoint " + NameOf(actor)).c_str());
    return g_sq.actorPoint;
}
const char *C_Human::GetLastName() const
{
    Log(this, "GetLastName");
    return g_sqLastName;
}
C_Item *C_Human::GetMedkit() const
{
    Log(this, "GetMedkit");
    return g_sq.medkit ? reinterpret_cast<C_Item *>(g_wornItems[6]) : nullptr;
}
int C_Human::GetControllerType() const
{
#ifdef HUMAN_COMMANDER
    return CmControllerType(this);
#else
    Log(this, "GetControllerType");
    return g_sq.controllerType;
#endif
}
int C_Human::CountItems(int typeId) const
{
    Log(this, "CountItems", {double(typeId)});
    return g_sq.itemCount;
}
bool C_Human::CollectItems(int behaviour, std::vector<C_Item *> &items, std::vector<int> &containers) const
{
    Log(this, "CollectItems", {double(behaviour), double(items.size())});
    for (int k = 0; k < g_sq.collected; ++k) {
        items.push_back(reinterpret_cast<C_Item *>(g_weaponPort[k & 1]));
        containers.push_back(k ? ITEM_CONTAINER_BELT : ITEM_CONTAINER_LONG_GUN);
    }
    return g_sq.collected > 0;
}
void C_Human::RequestTakeFromBox(C_Actor *box, int index) { Log(this, ("RequestTakeFromBox " + NameOf(box)).c_str(), {double(index)}); }
void C_Human::RequestPickUpItem(C_Actor *itemActor) { Log(this, ("RequestPickUpItem " + NameOf(itemActor)).c_str()); }
bool C_Human::RequestMoveItem(C_Item *item, C_Actor *other, int fromContainer, int toContainer, int slot)
{
    Log(this, ("RequestMoveItem " + NameOf(item) + " " + NameOf(other)).c_str(), {double(fromContainer), double(toContainer), double(slot)});
    return true;
}
bool C_Human::RequestTransferItem(C_Item *item, int arg, C_Human *other, C_Item *otherItem, int container)
{
    Log(this, ("RequestTransferItem " + NameOf(item) + " " + NameOf(other) + " " + NameOf(otherItem)).c_str(), {double(arg), double(container)});
    return true;
}
const S_vector &C_Human::GetFacingDir() const
{
    Log(this, "GetFacingDir");
    return g_sq.facing;
}
void C_Human::IssueCommand(const S_HumanCommand &command)
{
    Log(this, "HumanIssueCommand", {double(command.category), double(command.type), double(command.param1), double(command.param2)});
}
void C_WeaponItem::PlayShotSound(C_Actor *shooter, I3D_frame *where, bool quiet)
{
    Log(this, ("PlayShotSound " + NameOf(shooter) + " " + NameOf(where)).c_str(), {double(quiet)});
}
I3D_sector *S_HumanBody::GetSector() const
{
    Log(nullptr, "BodyGetSector");
    return SqSector();
}
I3D_frame *S_HumanBody::GetUnknown6E0(int *out) const
{
    Log(nullptr, "BodyGetUnknown6E0");
    *out = g_sq.spheres;
    return reinterpret_cast<I3D_frame *>(g_sqSpheres);
}
namespace {
void *SqRayAnswer(const S_vector *from, const S_vector *dir, const S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    Log(nullptr, "RayTest", {from->x, from->y, from->z, dir->x, dir->y, dir->z, double(hitNormal != nullptr), double(types), double(flags)});
    if (g_sq.rayHit && hitNormal)
        *const_cast<S_vector *>(hitNormal) = g_sq.rayNormal;
    return g_sq.rayHit ? g_sqVolume : nullptr;
}
void *SqDynamicAnswer(const S_vector *from, const S_vector *dir, const S_vector *hitPos, const S_vector *hitNormal, uint32_t types,
                      uint32_t flags, bool original)
{
    Log(nullptr, "RayTestDynamic", {from->x, from->y, from->z, dir->x, dir->y, dir->z, double(hitPos != nullptr),
                                    double(hitNormal != nullptr), double(types), double(flags)});
    if (g_sq.dynamicHit < 0)
        return nullptr;
    void *actor = g_sq.dynamicHit >= 13 ? nullptr : original ? OrigActor(g_sq.dynamicHit) : PortActor(g_sq.dynamicHit);
    std::memcpy(g_sqVolume + 0x48, &actor, 4);
    return g_sqVolume;
}
}  // namespace
extern "C" S_CollisionVolume *__attribute__((thiscall))
__wrap__ZNK16C_CollisionWorld7RayTestERK8S_vectorS2_PS0_S3_jj(const C_CollisionWorld *, const S_vector &from, const S_vector &dir,
                                                              S_vector *, S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    return static_cast<S_CollisionVolume *>(SqRayAnswer(&from, &dir, hitNormal, types, flags));
}
extern "C" S_CollisionVolume *__attribute__((thiscall))
__wrap__ZNK16C_CollisionWorld14RayTestDynamicERK8S_vectorS2_PS0_S3_jj(const C_CollisionWorld *, const S_vector &from, const S_vector &dir,
                                                                      S_vector *hitPos, S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    return static_cast<S_CollisionVolume *>(SqDynamicAnswer(&from, &dir, hitPos, hitNormal, types, flags, false));
}

namespace {
#ifndef HUMAN_COMMANDER
// ---- the commander's port side (its fake vtable's slots); the same functions serve the original's redirects
C_Actor *AC SQ_GetSquadTargetPort(void *self)
{
    Log(self, "CommanderGetSquadTarget");
    return g_sq.squadTarget;
}
void AC SQ_OnFriendlyFire(void *self, void *attacker) { Log(self, ("CommanderOnFriendlyFire " + NameOf(attacker)).c_str()); }
void AC SQ_DelayFormationChecks(void *self) { Log(self, "CommanderDelayFormationChecks"); }
void AC SQ_ExecuteCommand(void *self, int command, void *actor, void *member, int quiet, int value)
{
    Log(self, ("CommanderExecuteCommand " + NameOf(actor) + " " + NameOf(member)).c_str(), {double(command), double(quiet & 0xFF), double(value)});
}
void AC SQ_SetWhispering(void *self, int on) { Log(self, "CommanderSetWhispering", {double(on & 0xFF)}); }
int AC SQ_IsWhispering(void *self)
{
    Log(self, "CommanderIsWhispering");
    return g_sq.whispering;
}
void AC SQ_OnMemberIdle(void *self, void *member) { Log(self, ("CommanderOnMemberIdle " + NameOf(member)).c_str()); }

// ---- the original's side
void *AC SQ_GetSquadTargetOrig(void *self)
{
    Log(self, "CommanderGetSquadTarget");
    return SqOrigOf(g_sq.squadTarget);
}
void AC SQ_OnOrderMoveDone(void *self) { Log(self, "CommanderOnOrderMoveDone"); }
#endif
void *AC SQ_GetSelectedMember(void *self)
{
    // (Not from PickCoverDir, 0x00525700: the original's answer goes unused there, and the port does not ask.)
    if (__builtin_return_address(0) != reinterpret_cast<void *>(0x005257d2))
        Log(self, "TeamGetSelectedMember");
    return SqOrigOf(g_sq.selected);
}
void AC SQ_ReleaseHoldingMembers(void *self, void *human) { Log(self, ("TeamReleaseHoldingMembers " + NameOf(human)).c_str()); }
int AC SQ_IsEnemyKnown(void *self, void *enemy)
{
    Log(self, ("TeamIsEnemyKnown " + NameOf(enemy)).c_str());
    return g_sq.enemyKnown;
}
void *AC SQ_GetEnemySeenNearby(void *self, void *human)
{
    Log(self, ("TeamGetEnemySeenNearby " + NameOf(human)).c_str());
    return SqOrigOf(g_sq.seenNearby);
}
void *AC SQ_GetEnemyUnseenBy(void *self, void *human, int skipCarCrew)
{
    Log(self, ("TeamGetEnemyUnseenBy " + NameOf(human)).c_str(), {double(skipCarCrew & 0xFF)});
    return SqOrigOf(g_sq.unseenBy);
}
int AC SQ_GetMemberCount(void *self)
{
    Log(self, "TeamGetMemberCount");
    return g_sq.memberCount;
}
void *AC SQ_GetMember(void *self, int index)
{
    Log(self, "TeamGetMember", {double(index)});
    return SqOrigOf(SqMember(index));
}
int AC SQ_GetAliveCount(void *self)
{
    Log(self, "TeamGetAliveCount");
    return g_sq.aliveCount;
}
void AC SQ_EnemyRecordsRemove(void *, void *human) { Log(nullptr, ("EnemyRecordsRemove " + NameOf(human)).c_str()); }
int AC SQ_FindSlot(void *self, void *member, int *slot)
{
    Log(self, ("FormationFindSlot " + NameOf(member)).c_str());
    *slot = g_sq.slotIndex;
    return g_sq.slot;
}
int AC SQ_GetSlotTarget(void *self, void *member, S_vector4 *pos, uint16_t *checkpoint)
{
    Log(self, ("FormationGetSlotTarget " + NameOf(member)).c_str());
    *pos = g_sq.slotPos;
    if (checkpoint)   // (the callers may pass none)
        *checkpoint = g_sq.slotPoint;
    return g_sq.slot;
}
int AC SQ_GetSlotDir(void *self, void *member, S_vector4 *dir)
{
    Log(self, ("FormationGetSlotDir " + NameOf(member)).c_str());
    *dir = g_sq.slotDir;
    return g_sq.slot;
}
int AC SQ_GetSlotSeat(void *self, void *member, int *vehicleId)
{
    Log(self, ("FormationGetSlotSeat " + NameOf(member)).c_str());
    *vehicleId = g_sq.slotVehicle;
    return g_sq.slotSeat;
}
void AC SQ_InvalidateSlot(void *self, void *member) { Log(self, ("FormationInvalidateSlot " + NameOf(member)).c_str()); }
void AC SQ_SetSlotTarget(void *self, void *member, const S_vector4 *pos)
{
    Log(self, ("FormationSetSlotTarget " + NameOf(member)).c_str(), {pos->x, pos->y, pos->z});
}
void AC SQ_PlanSlot(void *self, int slot) { Log(self, "FormationPlanSlot", {double(slot)}); }
int AC SQ_GetSeatEntry(void *self, int seat, S_vector4 *pos, S_vector4 *dir)
{
    Log(self, "CarGetSeatEntry", {double(seat)});
    *pos = g_sq.entryPos;
    *dir = g_sq.entryDir;
    return g_sq.seatEntry;
}
int AC SQ_SetInput(void *self, float throttle, float steering)
{
    Log(self, "CarSetInput", {throttle, steering});
    return 1;
}
int AC SQ_HasLocalPlayerAboard(void *self)
{
    Log(self, "CarHasLocalPlayerAboard");
    return g_sq.localAboard;
}
int AC SQ_GetCrewSide(void *self)
{
    Log(self, "CarGetCrewSide");
    return g_sq.crewSide;
}
int AC SQ_CanEnter(void *self)
{
    Log(self, "CarCanEnter");
    return g_sq.canEnter;
}
int AC SQ_UpdateDriving(void *self, int timeDelta)
{
    Log(self, "UpdateDriving", {double(timeDelta)});
    return g_sq.driving;
}
void *SQ_GetSubmarines()
{
    Log(nullptr, "GetSubmarines");
    std::vector<uint32_t> words;
    for (C_Actor *a : g_sqSubmarines)
        words.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(SqOrigOf(a))));
    MsvcVectorPut(g_sqOrigSubmarines, words);
    return g_sqOrigSubmarines;
}
void *__fastcall SQ_FindJunkersTargetFor(void *human, void **frame)
{
    Log(human, "FindJunkersTargetFor");
    *frame = g_sq.junkers ? static_cast<void *>(g_targetFrame) : nullptr;
    return SqOrigOf(g_sq.junkers);
}
void *__fastcall SQ_FindAirAttackFor(void *human)
{
    Log(human, "FindAirAttackFor");
    return SqOrigOf(g_sq.airAttack);
}
int AC SQ_IsMineInRange(void *self, const S_vector *pos, int range)
{
    Log(self, "IsMineInRange", {pos->x, pos->y, pos->z, double(range)});
    return g_sq.mineInRange;
}
void *AC SQ_FindItemNear(void *self, const S_vector *pos, float range)
{
    Log(self, "FindItemNear", {pos->x, pos->y, pos->z, range});
    return SqOrigOf(g_sq.itemNear);
}
int AC SQ_IsCut(void *self)
{
    Log(self, "WireIsCut");
    return g_sq.wireCut;
}
int AC SQ_CanPutItem(void *self, void *actor, void *item)
{
    Log(self, ("CanPutItem " + NameOf(actor) + " " + NameOf(item)).c_str());
    return g_sq.canPut;
}
void *AC SQ_AcquireOrderMarker(void *, void *)
{
    Log(nullptr, "AcquireOrderMarker");
    return SqMarkerFor(true);
}
void AC SQ_ReleaseOrderMarker(void *, void *marker) { Log(nullptr, ("ReleaseOrderMarker " + NameOf(marker)).c_str()); }
void AC SQ_UpdateOrderMarker(void *, void *order, void *marker)
{
    Log(nullptr, ("UpdateOrderMarker " + NameOf(order) + " " + NameOf(marker)).c_str());
}
void __fastcall SQ_BuildPathPoints(const S_vector4 *from, const S_vector4 *to, const uint32_t *path, void *out)
{
    const std::vector<uint16_t> points = SqVectorOf(path);
    SqLogPoints(nullptr, "BuildPathPoints", points, {from->x, from->y, from->z, to->x, to->y, to->z});
    auto *array = static_cast<uint32_t *>(out);   // (data, capacity, count, growBy)
    array[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(SqPathPoints(*from, *to, points.size() + 1)));
    array[1] = array[2] = uint32_t(points.size() + 1);
}
float AC SQ_GetPathLength(void *self, const uint32_t *path)
{
    SqLogPoints(self, "GetPathLength", SqVectorOf(path), {});
    return g_sq.pathLength;
}
int AC SQ_FindCoverWithin(void *self, const S_vector *pos, float radius, uint32_t *out)
{
    Log(self, "FindCoverWithin", {pos->x, pos->y, pos->z, radius, double(SqVectorOf(out).size())});
    SqVectorAnswer(out, g_sq.coverWithin);
    return 1;
}
void AC SQ_FindByPrefix(void *self, const char *prefix, uint32_t *out)
{
    Log(self, (std::string("FindByPrefix ") + prefix).c_str(), {double(SqVectorOf(out).size())});
    SqVectorAnswer(out, g_sq.byPrefix);
}
void *AC SQ_GetActor(void *self, int index)
{
    index &= 0xFFFF;
    Log(self, "CheckpointsGetActor", {double(index)});
    return SqOrigOf(SqPointActor(index));
}
int AC SQ_FindActorPoint(void *self, const void *actor)
{
    Log(self, ("CheckpointsFindActorPoint " + NameOf(actor)).c_str());
    return g_sq.actorPoint;
}
const char *AC SQ_GetLastName(void *self)
{
    Log(self, "GetLastName");
    return g_sqLastName;
}
void *AC SQ_GetMedkit(void *self)
{
    Log(self, "GetMedkit");
    return g_sq.medkit ? static_cast<void *>(g_wornItems[6]) : nullptr;
}
int AC SQ_GetControllerType(void *self)
{
#ifdef HUMAN_COMMANDER
    return CmControllerType(self);
#else
    Log(self, "GetControllerType");
    return g_sq.controllerType;
#endif
}
int AC SQ_CountItems(void *self, int typeId)
{
    Log(self, "CountItems", {double(typeId)});
    return g_sq.itemCount;
}
// (Its vectors: MSVC6 ones, appended to.)
int AC SQ_CollectItems(void *self, int behaviour, uint32_t *items, uint32_t *containers)
{
    std::vector<uint32_t> its = MsvcVectorGet(reinterpret_cast<uint8_t *>(items)), cs = MsvcVectorGet(reinterpret_cast<uint8_t *>(containers));
    Log(self, "CollectItems", {double(behaviour), double(its.size())});
    for (int k = 0; k < g_sq.collected; ++k) {
        its.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_weaponOrig[k & 1])));
        cs.push_back(uint32_t(k ? ITEM_CONTAINER_BELT : ITEM_CONTAINER_LONG_GUN));
    }
    MsvcVectorPut(reinterpret_cast<uint8_t *>(items), its);
    MsvcVectorPut(reinterpret_cast<uint8_t *>(containers), cs);
    return g_sq.collected > 0;
}
void AC SQ_RequestTakeFromBox(void *self, void *box, int index) { Log(self, ("RequestTakeFromBox " + NameOf(box)).c_str(), {double(index)}); }
void AC SQ_RequestPickUpItem(void *self, void *itemActor) { Log(self, ("RequestPickUpItem " + NameOf(itemActor)).c_str()); }
int AC SQ_RequestMoveItem(void *self, void *item, void *other, int fromContainer, int toContainer, int slot)
{
    Log(self, ("RequestMoveItem " + NameOf(item) + " " + NameOf(other)).c_str(), {double(fromContainer), double(toContainer), double(slot)});
    return 1;
}
int AC SQ_RequestTransferItem(void *self, void *item, int arg, void *other, void *otherItem, int container)
{
    Log(self, ("RequestTransferItem " + NameOf(item) + " " + NameOf(other) + " " + NameOf(otherItem)).c_str(), {double(arg), double(container)});
    return 1;
}
const S_vector *AC SQ_GetFacingDir(void *self)
{
    Log(self, "GetFacingDir");
    return &g_sq.facing;
}
void AC SQ_IssueCommand(void *self, const S_HumanCommand *command)
{
    Log(self, "HumanIssueCommand", {double(command->category), double(command->type), double(command->param1), double(command->param2)});
}
void AC SQ_PlayShotSound(void *self, void *shooter, void *where, int quiet)
{
    Log(self, ("PlayShotSound " + NameOf(shooter) + " " + NameOf(where)).c_str(), {double(quiet & 0xFF)});
}
void *AC SQ_GetSector(void *)
{
    Log(nullptr, "BodyGetSector");
    return SqSector();
}
void *AC SQ_GetUnknown6E0(void *, int *out)
{
    Log(nullptr, "BodyGetUnknown6E0");
    *out = g_sq.spheres;
    return g_sqSpheres;
}
void *AC SQ_RayTest(void *, const S_vector *from, const S_vector *dir, S_vector *, S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    return SqRayAnswer(from, dir, hitNormal, types, flags);
}
void *AC SQ_RayTestDynamic(void *, const S_vector *from, const S_vector *dir, S_vector *hitPos, S_vector *hitNormal, uint32_t types,
                           uint32_t flags)
{
    return SqDynamicAnswer(from, dir, hitPos, hitNormal, types, flags, true);
}

// The weapons' capacity (now and then their amount: full) and the component's muzzle velocity (the port's slot; the
// original reads the field, +0x38 on both).
float g_sqMaxAmount = 30.0f;
float AC SQ_GetMaxAmount(void *self)
{
    Log(self, "WeaponGetMaxAmount");
    return g_sqMaxAmount;
}
float AC SQ_MuzzleVelocity(void *self)
{
    float v;
    std::memcpy(&v, static_cast<uint8_t *>(self) + 0x38, 4);
    return v;
}
// The original's C_fstr constructor (0x00405100, cdecl): the text formatted into a C_str buffer (a count of 1, the
// length, the characters; malloc'd, the original's delete is free here). FindAttackPoint names the map's points by it.
void **SQ_FstrConstruct(void **dest, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    const size_t length = n < 0 ? 0 : std::min<size_t>(size_t(n), sizeof text - 1);
    auto *rep = static_cast<uint32_t *>(std::malloc(8 + length + 1));
    rep[0] = 1;
    rep[1] = uint32_t(length);
    std::memcpy(rep + 2, text, length + 1);
    *dest = rep;
    return dest;
}
// The walking network's FindPath: the test's path (none: not found); the original's an MSVC6 vector of uint16_t (its
// new: malloc here).
bool SqFindPath(const void *network, int from, int to, std::vector<uint16_t> *path, uint32_t excludeFlags)
{
    Log(network, "FindPath", {double(from & 0xFFFF), double(to & 0xFFFF), double(excludeFlags)});   // (16-bit points)
    *path = g_sq.path;
    return !g_sq.path.empty();
}
bool AC SQ_FindPathOrig(void *network, int from, int to, uint32_t *path, uint32_t excludeFlags)
{
    Log(network, "FindPath", {double(from & 0xFFFF), double(to & 0xFFFF), double(excludeFlags)});   // (16-bit points)
    const size_t n = g_sq.path.size();
    auto *data = static_cast<uint16_t *>(std::malloc(n * 2 + 2));
    if (n)
        std::memcpy(data, g_sq.path.data(), n * 2);
    path[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    path[2] = path[1] + 2 * static_cast<uint32_t>(n);
    path[3] = path[2];
    return n != 0;
}
// The orders' boxes in the collision world (the port's through the world's wraps, the original's calls redirected):
// logged, not added (the original's world is not there); their centres not compared (the original's UpdateMarker
// writes stale registers there).
// (Each side's order at its box: the port's member, the original's +0x90; the marker +0x84, the inverse +0xF0.)
void SqLogOrderVolume(const char *what, const S_CollisionVolume &v, const void *order, const void *marker, const void *invMatrix)
{
    const bool add = std::strcmp(what, "AddVolume") == 0;
    const bool own = v.frame == marker && v.invMatrix == invMatrix &&
                     (!marker || v.matrix == &static_cast<const I3D_frame *>(marker)->GetMatrix());
    (void)order;
    const std::string name = std::string(what) + " orderVolume";
    if (add)
        Log(&g_collisionWorld, name.c_str(),
            {double(v.flags), double(v.type), double(own), double(v.actor != nullptr), v.bbox.min.x, v.bbox.min.y, v.bbox.min.z, v.bbox.max.x,
             v.bbox.max.y, v.bbox.max.z, v.center.x, v.center.y, v.center.z, v.centerW, v.radius});
    else
        Log(&g_collisionWorld, name.c_str(), {double(v.flags), double(v.type), double(own), double(v.actor != nullptr)});
}
bool SqOrderVolume(const char *what, const S_CollisionVolume *v)
{
    if (v->type != ORDER_VOLUME_TYPE)
        return false;
    const auto *order = reinterpret_cast<const S_HumanOrder *>(reinterpret_cast<const uint8_t *>(v) - offsetof(S_HumanOrder, volume));
    SqLogOrderVolume(what, *v, order, order->marker, &order->invMatrix);
    return true;
}
void SqOrigVolume(const char *what, const void *v)
{
    S_CollisionVolume copy;
    std::memcpy(static_cast<void *>(&copy), v, sizeof copy);
    if (copy.type != ORDER_VOLUME_TYPE)
        return;   // (others: silent, as before)
    const uint8_t *order = static_cast<const uint8_t *>(v) - 0x90;
    void *marker;
    std::memcpy(&marker, order + 0x84, 4);
    SqLogOrderVolume(what, copy, order, marker, order + 0xF0);
}
void AC SQ_AddVolume(void *, const void *v) { SqOrigVolume("AddVolume", v); }
void AC SQ_MoveVolume(void *, const void *v) { SqOrigVolume("MoveVolume", v); }
void AC SQ_RemoveVolume(void *, const void *v) { SqOrigVolume("RemoveVolume", v); }
// The human's FindItem on the original's side: its own weapon (part 23's answer, a weapon or none here).
void *AC SQ_FindItemOrig(void *self, int typeId, int *container)
{
    Log(self, "FindItem", {double(typeId), double(container != nullptr)});
    if (container)
        *container = 4;
    return OrigWeapon(g_cs.found);
}
void SetUpSquadFakes()
{
    g_compPortVt[VSlot(&C_WeaponComp::GetMuzzleVelocity)] = reinterpret_cast<void *>(SQ_MuzzleVelocity);
    g_weaponPortVt[VSlot(&C_Item::GetMaxAmount)] = reinterpret_cast<void *>(SQ_GetMaxAmount);
    g_weaponOrigVt[0x10 / 4] = reinterpret_cast<void *>(SQ_GetMaxAmount);
    const std::pair<uint32_t, const void *> redirects[] = {
#ifndef HUMAN_COMMANDER
        {0x00456860, FAKE(SQ_GetSquadTargetOrig)},
        {0x00459500, FAKE(SQ_OnFriendlyFire)},
        {0x00459490, FAKE(SQ_DelayFormationChecks)},
        {0x00458e50, FAKE(SQ_ExecuteCommand)},
        {0x00459470, FAKE(SQ_SetWhispering)},
        {0x00459480, FAKE(SQ_IsWhispering)},
        {0x00455af0, FAKE(SQ_OnMemberIdle)},
        {0x00456f00, FAKE(SQ_OnOrderMoveDone)},
#endif
        {0x0045d160, FAKE(SQ_GetSelectedMember)},
        {0x0045f110, FAKE(SQ_ReleaseHoldingMembers)},
        {0x0045e920, FAKE(SQ_IsEnemyKnown)},
        {0x0045f280, FAKE(SQ_GetEnemySeenNearby)},
        {0x0045f3a0, FAKE(SQ_GetEnemyUnseenBy)},
        {0x00462300, FAKE(SQ_GetMemberCount)},
        {0x004619f0, FAKE(SQ_GetMember)},
        {0x004622c0, FAKE(SQ_GetAliveCount)},
        {0x0045e540, FAKE(SQ_EnemyRecordsRemove)},
        {0x005a78f0, FAKE(SQ_FindItemOrig)},
        {0x00405100, FAKE(SQ_FstrConstruct)},
        {0x005223b0, FAKE(SQ_FindPathOrig)},
        {0x006ef7b0, FAKE(SQ_AddVolume)},
        {0x006efbc0, FAKE(SQ_MoveVolume)},
        {0x006ef9b0, FAKE(SQ_RemoveVolume)},
        {0x00598290, FAKE(SQ_FindSlot)},
        {0x00598320, FAKE(SQ_GetSlotTarget)},
        {0x005983c0, FAKE(SQ_GetSlotDir)},
        {0x00598460, FAKE(SQ_GetSlotSeat)},
        {0x005982d0, FAKE(SQ_InvalidateSlot)},
        {0x00599040, FAKE(SQ_SetSlotTarget)},
        {0x0059af30, FAKE(SQ_PlanSlot)},
        {0x004da420, FAKE(SQ_GetSeatEntry)},
        {0x004d9280, FAKE(SQ_SetInput)},
        {0x004dea90, FAKE(SQ_HasLocalPlayerAboard)},
        {0x004ddee0, FAKE(SQ_GetCrewSide)},
        {0x004ddd10, FAKE(SQ_CanEnter)},
        {0x00578770, FAKE(SQ_UpdateDriving)},
        {0x005de7b0, FAKE(SQ_GetSubmarines)},
        {0x005f0e40, FAKE(SQ_FindJunkersTargetFor)},
        {0x005e8b70, FAKE(SQ_FindAirAttackFor)},
        {0x00478ae0, FAKE(SQ_IsMineInRange)},
        {0x00479600, FAKE(SQ_FindItemNear)},
        {0x005e24c0, FAKE(SQ_IsCut)},
        {0x007db3e0, FAKE(SQ_CanPutItem)},
        {0x004b9240, FAKE(SQ_AcquireOrderMarker)},
        {0x004b9080, FAKE(SQ_ReleaseOrderMarker)},
        {0x004b9b10, FAKE(SQ_UpdateOrderMarker)},
        {0x007ce930, FAKE(SQ_BuildPathPoints)},
        {0x005230d0, FAKE(SQ_GetPathLength)},
        {0x0051f830, FAKE(SQ_FindCoverWithin)},
        {0x0051c9c0, FAKE(SQ_FindByPrefix)},
        {0x0051e520, FAKE(SQ_GetActor)},
        {0x0051e690, FAKE(SQ_FindActorPoint)},
        {0x005a81c0, FAKE(SQ_GetLastName)},
        {0x005a7e00, FAKE(SQ_GetMedkit)},
        {0x005a5fe0, FAKE(SQ_GetControllerType)},
        {0x005a6aa0, FAKE(SQ_CountItems)},
        {0x005a7850, FAKE(SQ_CollectItems)},
        {0x005a2cf0, FAKE(SQ_RequestTakeFromBox)},
        {0x005a2d20, FAKE(SQ_RequestPickUpItem)},
        {0x005a2b40, FAKE(SQ_RequestMoveItem)},
        {0x005a2bb0, FAKE(SQ_RequestTransferItem)},
        {0x005a80d0, FAKE(SQ_GetFacingDir)},
        {0x005a45b0, FAKE(SQ_IssueCommand)},
        {0x007d5ee0, FAKE(SQ_PlayShotSound)},
        {0x005ac4c0, FAKE(SQ_GetSector)},
        {0x005ac1d0, FAKE(SQ_GetUnknown6E0)},
        {0x00718830, FAKE(SQ_RayTest)},
        {0x006f7cc0, FAKE(SQ_RayTestDynamic)},
        {0x004d9050, FAKE(OV_CarApplyControls)},
        {0x005e0dd0, FAKE(OV_SubmarineSetControls)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
#ifndef HUMAN_COMMANDER
    // The commander: the port's fake object and vtable; the original's global given its own vtable (its static
    // initialisers never ran here), its members redirected above.
    for (void *&slot : g_sqCommanderPortVt)
        slot = reinterpret_cast<void *>(CT_Unexpected);
    g_sqCommanderPortVt[VSlot(&C_SquadCommander::GetSquadTarget)] = reinterpret_cast<void *>(SQ_GetSquadTargetPort);
    g_sqCommanderPortVt[VSlot(&C_SquadCommander::OnFriendlyFire)] = reinterpret_cast<void *>(SQ_OnFriendlyFire);
    g_sqCommanderPortVt[VSlot(&C_SquadCommander::DelayFormationChecks)] = reinterpret_cast<void *>(SQ_DelayFormationChecks);
    g_sqCommanderPortVt[VSlot(&C_SquadCommander::ExecuteCommand)] = reinterpret_cast<void *>(SQ_ExecuteCommand);
    g_sqCommanderPortVt[VSlot(&C_SquadCommander::SetWhispering)] = reinterpret_cast<void *>(SQ_SetWhispering);
    g_sqCommanderPortVt[VSlot(&C_SquadCommander::IsWhispering)] = reinterpret_cast<void *>(SQ_IsWhispering);
    g_sqCommanderPortVt[VSlot(&C_SquadCommander::OnMemberIdle)] = reinterpret_cast<void *>(SQ_OnMemberIdle);
    void *vt = g_sqCommanderPortVt;
    std::memcpy(g_sqCommanderPort, &vt, 4);
    *OriginalAt<uint32_t *>(0x0089ad40) = 0x0080fb28;
    g_names[g_sqCommanderPort] = g_names[OriginalAt<void *>(0x0089ad40)] = "commander";
    g_extraPointers.push_back({g_sqCommanderPort, OriginalAt<void *>(0x0089ad40)});
#endif
    g_names[g_sqSectorOther] = "otherSector";
    g_names[g_sqVolume] = "volume";
}

// ---- the marker frames the command panel hands out (a pool per side, the first free one; the frames alike by name:
// marker0 .. marker15: the queue holds ten)
I3D_frame *g_sqMarkers[2][16];
int g_sqMarkerNext[2] = {0, 0};
I3D_frame *SqMarker(bool original)
{
    // (The first free one: no order's data on it; both sides' pools are used alike.)
    I3D_frame *f = g_sqMarkers[original][g_sqMarkerNext[original]++ & 15];
    for (I3D_frame *m : g_sqMarkers[original])
        if (!m->GetData()) {
            f = m;
            break;
        }
    f->LinkTo(nullptr, 0);
    return f;
}
void SqSetUpMarkers()
{
    S_quat none;
    none.Identity();
    for (int side = 0; side < 2; ++side)
        for (int k = 0; k < 16; ++k) {
            g_sqMarkers[side][k] = NewFrame(FRAME_DUMMY, "orderMarker", S_vector(0, 0, 0), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "", nullptr);
            g_names[g_sqMarkers[side][k]] = "marker" + std::to_string(k);
        }
    for (int k = 0; k < 16; ++k)
        g_extraPointers.push_back({g_sqMarkers[0][k], g_sqMarkers[1][k]});
}

// ---- the orders: the original's are its pool's (AllocHumanOrder 0x005825c0, __thiscall on the AI pools)
typedef void *(AC *T_OrigAllocOrder)(void *, void *, int, int, const S_vector4 *, const S_vector4 *, uint32_t, uint32_t, uint32_t,
                                     void *, int);
std::map<const S_HumanOrder *, uint8_t *> g_sqOrderPairs;
S_HumanOrder *SqAllocOrder(int kind, int arg, const S_vector4 &pos, const S_vector4 &data, uint32_t pace, uint32_t stance, uint32_t mode,
                           C_Actor *actor, int unknown70)
{
    g_sqMarkerNext[0] = g_sqMarkerNext[1] = 0;
    S_HumanOrder *p = AllocHumanOrder(g_h.human, kind, arg, pos, data, pace, stance, mode, actor, unknown70);
    auto *o = static_cast<uint8_t *>(OriginalAt<T_OrigAllocOrder>(0x005825c0)(AiPools(), o_human, kind, arg, &pos, &data, pace, stance, mode,
                                                                               SqOrigOf(actor), unknown70));
    g_sqOrderPairs[p] = o;
    g_names[p] = g_names[o] = "order";
    return p;
}
uint8_t *SqOrigOrder(const S_HumanOrder *p)
{
    auto it = g_sqOrderPairs.find(p);
    return it == g_sqOrderPairs.end() ? nullptr : it->second;
}
// An order's fields against the original's (the volume left out: the box at the marker). g_sqLoadedOrders: their pos
// and data by x, y, z (the original's loaded w: a stack word).
bool g_sqLoadedOrders = false;
std::string SqOrderDiffers(const S_HumanOrder *p, const uint8_t *o)
{
    auto word = [&](size_t at) {
        uint32_t w;
        std::memcpy(&w, o + at, 4);
        return w;
    };
    auto mapped = [](const void *q) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(SqOrigOf(q))); };
    char text[320];
    if (word(4) != uint32_t(p->type) || word(0xC) != uint32_t(p->kind) || word(0x10) != uint32_t(p->arg) || word(0x44) != p->flags ||
        word(0x70) != uint32_t(p->unknown70)) {
        std::snprintf(text, sizeof text, "order: type %d/%u kind %d/%u arg %d/%u flags %08x/%08x", p->type, word(4), p->kind, word(0xC), p->arg,
                      word(0x10), p->flags, word(0x44));
        return text;
    }
    if (word(8) != mapped(p->human) || word(0x40) != mapped(p->actor) || word(0x84) != mapped(p->marker))
        return "order: human, actor or marker";
    if (o[0x48] != p->done || o[0x49] != p->started || o[0x4A] != p->selected)
        return "order: done, started or selected";
    const size_t vec = g_sqLoadedOrders ? 12 : 16;
    // (The direction by x, y, z: UpdatePath leaves the original's w a stale local, nothing reads it.)
    if (std::memcmp(o + 0x20, &p->data, 12) != 0 || std::memcmp(o + 0x30, &p->pos, vec) != 0) {
        uint32_t pw[8];
        std::memcpy(pw, &p->data, 16);
        std::memcpy(pw + 4, &p->pos, 16);
        std::snprintf(text, sizeof text, "order: data or pos: port %08x %08x %08x %08x / %08x %08x %08x %08x, original %08x %08x %08x %08x / %08x %08x %08x %08x",
                      pw[0], pw[1], pw[2], pw[3], pw[4], pw[5], pw[6], pw[7], word(0x20), word(0x24), word(0x28), word(0x2C), word(0x30), word(0x34),
                      word(0x38), word(0x3C));
        return text;
    }
    // (x, y, z: the original leaves w unset.)
    if (std::memcmp(o + 0x50, &p->pathFrom, 12) != 0 || std::memcmp(o + 0x60, &p->pathTo, 12) != 0) {
        uint32_t pw[8];
        std::memcpy(pw, &p->pathFrom, 16);
        std::memcpy(pw + 4, &p->pathTo, 16);
        std::snprintf(text, sizeof text, "order: the path's ends: port %08x %08x %08x %08x / %08x %08x %08x %08x, original %08x %08x %08x %08x / %08x %08x %08x %08x",
                      pw[0], pw[1], pw[2], pw[3], pw[4], pw[5], pw[6], pw[7], word(0x50), word(0x54), word(0x58), word(0x5C), word(0x60), word(0x64),
                      word(0x68), word(0x6C));
        return text;
    }
    if (word(0x7C) != uint32_t(p->path.count))
        return "order: the path's count";
    if (word(0x78) != uint32_t(p->path.capacity))
        return "order: the path's capacity";
    {
        auto *om = reinterpret_cast<I3D_frame *>(static_cast<uintptr_t>(word(0x84)));
        if (p->marker && om) {
            if (std::memcmp(&p->marker->GetWorldPos(), &om->GetWorldPos(), 12) != 0)
                return "order: the marker's place";
            if (p->marker->GetData() != reinterpret_cast<const S_FrameData *>(p) || om->GetData() != reinterpret_cast<const S_FrameData *>(o))
                return "order: the marker's data";
            if (p->marker->GetParent() != om->GetParent())
                return "order: the marker's parent";
        }
    }
    if (p->path.count > 0 && std::memcmp(reinterpret_cast<const void *>(static_cast<uintptr_t>(word(0x74))), p->path.data,
                                         16 * size_t(p->path.count)) != 0)
        return "order: the path";
    if (p->marker && std::memcmp(o + 0xF0, &p->invMatrix, 0x40) != 0)
        return "order: the marker's inverse matrix";
    return "";
}

// ---- the squad's private members
typedef C_SquadController S_Sq;
DA_ROB(SqSelfHeal, void (S_Sq::*)(int), &S_Sq::SelfHeal)
DA_ROB(SqUpdateIdle, void (S_Sq::*)(int), &S_Sq::UpdateIdle)
DA_ROB(SqUpdateFollowPace, void (S_Sq::*)(), &S_Sq::UpdateFollowPace)
DA_ROB(SqUpdateIconTimers, void (S_Sq::*)(int), &S_Sq::UpdateIconTimers)
DA_ROB(SqSetStatusIcon, void (S_Sq::*)(int), &S_Sq::SetStatusIcon)
DA_ROB(SqSetModeIcon, void (S_Sq::*)(int), &S_Sq::SetModeIcon)
DA_ROB(SqNotifyIfIdle, void (S_Sq::*)(), &S_Sq::NotifyIfIdle)
DA_ROB(SqFollowSelectedMember, void (S_Sq::*)(), &S_Sq::FollowSelectedMember)
DA_ROB(SqControlSubmarine, void (S_Sq::*)(), &S_Sq::ControlSubmarine)
DA_ROB(SqFollowSubmarine, void (S_Sq::*)(C_Actor *), &S_Sq::FollowSubmarine)

DA_ROB(SqEngagePos, S_vector4 S_Sq::*, &S_Sq::m_engagePos)
DA_ROB(SqBoundDir, S_vector4 S_Sq::*, &S_Sq::m_boundDir)
DA_ROB(SqHoldDir, S_vector4 S_Sq::*, &S_Sq::m_holdDir)
DA_ROB(SqPace, uint32_t S_Sq::*, &S_Sq::m_pace)
DA_ROB(SqFlags, uint32_t S_Sq::*, &S_Sq::m_flags)
DA_ROB(SqOrderStance, uint32_t S_Sq::*, &S_Sq::m_orderStance)
DA_ROB(SqCoverStance, uint32_t S_Sq::*, &S_Sq::m_coverStance)
DA_ROB(SqOrderPace, uint32_t S_Sq::*, &S_Sq::m_orderPace)
DA_ROB(SqIdleCheckpoint, int S_Sq::*, &S_Sq::m_idleCheckpoint)
DA_ROB(SqQuiet, bool S_Sq::*, &S_Sq::m_quiet)
DA_ROB(SqFollowing, bool S_Sq::*, &S_Sq::m_following)
DA_ROB(SqSilentMode, bool S_Sq::*, &S_Sq::m_silentMode)
DA_ROB(SqLyingToFire, bool S_Sq::*, &S_Sq::m_lyingToFire)
DA_ROB(SqSteppedAside, bool S_Sq::*, &S_Sq::m_steppedAside)
DA_ROB(SqOrdersHeld, bool S_Sq::*, &S_Sq::m_ordersHeld)
DA_ROB(SqRestartOrder, bool S_Sq::*, &S_Sq::m_restartOrder)
DA_ROB(SqBoundHops, int S_Sq::*, &S_Sq::m_boundHops)
DA_ROB(SqEngageTime, int S_Sq::*, &S_Sq::m_engageTime)
DA_ROB(SqEventTime, int S_Sq::*, &S_Sq::m_eventTime)
DA_ROB(SqAlertTime, int S_Sq::*, &S_Sq::m_alertTime)
DA_ROB(SqWeaponCheckTime, int S_Sq::*, &S_Sq::m_weaponCheckTime)
DA_ROB(SqFormationTime, int S_Sq::*, &S_Sq::m_formationTime)
DA_ROB(SqFireMoveTime, int S_Sq::*, &S_Sq::m_fireMoveTime)
DA_ROB(SqAirCheckTime, int S_Sq::*, &S_Sq::m_airCheckTime)
DA_ROB(SqFollowDelay, int S_Sq::*, &S_Sq::m_followDelay)
DA_ROB(SqStatusIconTime, int S_Sq::*, &S_Sq::m_statusIconTime)
DA_ROB(SqModeIconTime, int S_Sq::*, &S_Sq::m_modeIconTime)
DA_ROB(SqOrdersChangedTime, int S_Sq::*, &S_Sq::m_ordersChangedTime)
DA_ROB(SqCoverRadius, float S_Sq::*, &S_Sq::m_coverRadius)
DA_ROB(SqFriendlyFire, float S_Sq::*, &S_Sq::m_friendlyFire)
DA_ROB(SqStatusIcon, int S_Sq::*, &S_Sq::m_statusIcon)
DA_ROB(SqTempStatusIcon, int S_Sq::*, &S_Sq::m_tempStatusIcon)
DA_ROB(SqModeIcon, int S_Sq::*, &S_Sq::m_modeIcon)
DA_ROB(SqTempModeIcon, int S_Sq::*, &S_Sq::m_tempModeIcon)
DA_ROB(SqCombatMode, int S_Sq::*, &S_Sq::m_combatMode)
DA_ROB(SqAction, S_SquadAction S_Sq::*, &S_Sq::m_action)
DA_ROB(SqGunnerLooksAhead, bool S_Sq::*, &S_Sq::m_gunnerLooksAhead)
DA_ROB(SqSelfHealTime, int S_Sq::*, &S_Sq::m_selfHealTime)
DA_ROB(SqPickedTarget, C_Human *S_Sq::*, &S_Sq::m_pickedTarget)
DA_ROB(SqAirTarget, C_Actor *S_Sq::*, &S_Sq::m_airTarget)
DA_ROB(SqAirTargetFrame, I3D_frame *S_Sq::*, &S_Sq::m_airTargetFrame)
DA_ROB(SqMineNear, bool S_Sq::*, &S_Sq::m_mineNear)
DA_ROB(SqMineCheckTime, int S_Sq::*, &S_Sq::m_mineCheckTime)
DA_ROB(SqOrders, std::vector<S_HumanOrder *> S_Sq::*, &S_Sq::m_orders)

// ---- the pair (the port's: Create()'s object with a copy of its vtable, ExecuteCommand logged)
S_Sq *g_psq;
uint8_t *g_osq;
void *g_osqVt[0x60];
void *g_psqVt[0x80];
S_Sq *Sq() { return g_psq; }
template <class T> T &SqField(T S_Sq::*m) { return Sq()->*m; }

struct S_SqWord {
    const char *name;
    size_t orig;
    void *(*port)();
    size_t size;
    int skip = -1;                                      // a word copied but not compared (its offset)
};
#define SQ_WORD(NAME, AT, TAG, SIZE) {NAME, AT, [] { return static_cast<void *>(&(Sq()->*DaGet(TAG()))); }, SIZE}
#define SQ_SKIP(NAME, AT, TAG, SIZE, SKIP) {NAME, AT, [] { return static_cast<void *>(&(Sq()->*DaGet(TAG()))); }, SIZE, SKIP}
// (engagePos and holdDir: copied whole, compared by x, y, z; their w is neither read nor saved, and LoadGame and several
// writes leave the original's a stack word. The action likewise without its pos's w: the attack point's is a stack word
// in the original.)
const S_SqWord SQ_WORDS[] = {
    SQ_SKIP("engagePos", 0x140, SqEngagePos, 16, 12), SQ_WORD("boundDir", 0x150, SqBoundDir, 16),
    SQ_SKIP("holdDir", 0x160, SqHoldDir, 16, 12), SQ_WORD("pace", 0x170, SqPace, 4), SQ_WORD("flags", 0x174, SqFlags, 4),
    SQ_WORD("orderStance", 0x178, SqOrderStance, 4), SQ_WORD("coverStance", 0x17C, SqCoverStance, 4),
    SQ_WORD("orderPace", 0x180, SqOrderPace, 4), SQ_WORD("idleCheckpoint", 0x184, SqIdleCheckpoint, 4),
    SQ_WORD("quiet", 0x188, SqQuiet, 1), SQ_WORD("following", 0x189, SqFollowing, 1),
    SQ_WORD("silentMode", 0x18A, SqSilentMode, 1), SQ_WORD("lyingToFire", 0x18B, SqLyingToFire, 1),
    SQ_WORD("steppedAside", 0x18C, SqSteppedAside, 1), SQ_WORD("ordersHeld", 0x18D, SqOrdersHeld, 1),
    SQ_WORD("restartOrder", 0x18E, SqRestartOrder, 1), SQ_WORD("boundHops", 0x190, SqBoundHops, 4),
    SQ_WORD("engageTime", 0x194, SqEngageTime, 4), SQ_WORD("eventTime", 0x198, SqEventTime, 4),
    SQ_WORD("alertTime", 0x19C, SqAlertTime, 4), SQ_WORD("weaponCheckTime", 0x1A0, SqWeaponCheckTime, 4),
    SQ_WORD("formationTime", 0x1A4, SqFormationTime, 4), SQ_WORD("fireMoveTime", 0x1A8, SqFireMoveTime, 4),
    SQ_WORD("airCheckTime", 0x1AC, SqAirCheckTime, 4), SQ_WORD("followDelay", 0x1B0, SqFollowDelay, 4),
    SQ_WORD("statusIconTime", 0x1B4, SqStatusIconTime, 4), SQ_WORD("modeIconTime", 0x1B8, SqModeIconTime, 4),
    SQ_WORD("ordersChangedTime", 0x1BC, SqOrdersChangedTime, 4), SQ_WORD("coverRadius", 0x1C0, SqCoverRadius, 4),
    SQ_WORD("friendlyFire", 0x1C4, SqFriendlyFire, 4), SQ_WORD("statusIcon", 0x1C8, SqStatusIcon, 4),
    SQ_WORD("tempStatusIcon", 0x1CC, SqTempStatusIcon, 4), SQ_WORD("modeIcon", 0x1D0, SqModeIcon, 4),
    SQ_WORD("tempModeIcon", 0x1D4, SqTempModeIcon, 4), SQ_WORD("combatMode", 0x1D8, SqCombatMode, 4),
    // (The action's pos without its w: the attack point's is a stack word in the original, nothing reads it.)
    SQ_SKIP("action", 0x1E0, SqAction, 0x44, 0x0C), SQ_WORD("gunnerLooksAhead", 0x230, SqGunnerLooksAhead, 1),
    SQ_WORD("selfHealTime", 0x234, SqSelfHealTime, 4), SQ_WORD("mineNear", 0x244, SqMineNear, 1),
    SQ_WORD("mineCheckTime", 0x248, SqMineCheckTime, 4),
};
uint32_t SqMapped(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(SqOrigOf(p))); }

// The squad's fields in the original's layout: the plain ones, the pointers mapped, the queue (each order's pair).
void SquadToOrig()
{
    for (const S_SqWord &w : SQ_WORDS) {
        std::memset(g_osq + w.orig, 0, w.size == 1 ? 4 : w.size);
        std::memcpy(g_osq + w.orig, w.port(), w.size);
    }
    const uint32_t ptrs[4] = {SqMapped(SqField(DaGet(SqAction())).target), SqMapped(SqField(DaGet(SqPickedTarget()))),
                              SqMapped(SqField(DaGet(SqAirTarget()))),
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(SqField(DaGet(SqAirTargetFrame()))))};
    std::memcpy(g_osq + 0x1E0 + 0x44, &ptrs[0], 4);
    std::memcpy(g_osq + 0x238, &ptrs[1], 4);
    std::memcpy(g_osq + 0x23C, &ptrs[2], 4);
    std::memcpy(g_osq + 0x240, &ptrs[3], 4);
    std::vector<uint32_t> orders;
    for (S_HumanOrder *p : SqField(DaGet(SqOrders())))
        orders.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(SqOrigOrder(p))));
    MsvcVectorPut(g_osq + 0x24C, orders, size_t(I(0, 2)));
    g_sqMarkerNext[0] = g_sqMarkerNext[1] = 0;
}
DA_ROB(SqReadyTime, uint32_t C_WeaponItem::*, &C_WeaponItem::m_readyTime)
DA_ROB(SqSubTurn, float C_Submarine::*, &C_Submarine::m_turn)
std::string SquadDiffers()
{
    {
        auto *sub = static_cast<C_Submarine *>(reinterpret_cast<C_Actor *>(g_asCarPort));
        float speed, turn;
        std::memcpy(&speed, g_asCarOrig + 0xF4, 4);
        std::memcpy(&turn, g_asCarOrig + 0xF8, 4);
        const float portSpeed = sub->GetSpeed();
        if (std::memcmp(&speed, &portSpeed, 4) != 0 || std::memcmp(&turn, &(sub->*DaGet(SqSubTurn())), 4) != 0)
            return "the submarine's speed or turn";
    }
    for (const S_SqWord &w : SQ_WORDS) {
        size_t at = 0;
        auto same = [&](size_t k) {
            return (w.skip >= 0 && k / 4 == size_t(w.skip) / 4) || static_cast<const uint8_t *>(w.port())[k] == g_osq[w.orig + k];
        };
        while (at < w.size && same(at))
            ++at;
        if (at == w.size)
            continue;
        uint32_t a = 0, b = 0;
        const size_t word = at & ~size_t(3);
        std::memcpy(&a, static_cast<const uint8_t *>(w.port()) + word, std::min<size_t>(w.size - word, 4));
        std::memcpy(&b, g_osq + w.orig + word, std::min<size_t>(w.size - word, 4));
        char text[128];
        std::snprintf(text, sizeof text, "%s+%u: port %08x original %08x", w.name, unsigned(word), a, b);
        return text;
    }
    const uint32_t ptrs[4] = {SqMapped(SqField(DaGet(SqAction())).target), SqMapped(SqField(DaGet(SqPickedTarget()))),
                              SqMapped(SqField(DaGet(SqAirTarget()))),
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(SqField(DaGet(SqAirTargetFrame()))))};
    static const size_t at[4] = {0x1E0 + 0x44, 0x238, 0x23C, 0x240};
    static const char *const names[4] = {"action's target", "pickedTarget", "airTarget", "airTargetFrame"};
    for (int k = 0; k < 4; ++k) {
        uint32_t o;
        std::memcpy(&o, g_osq + at[k], 4);
        if (o != ptrs[k])
            return names[k];
    }
    // The queue: a known order must be its pair; a new one (made in the call) is compared by its fields and paired.
    const std::vector<S_HumanOrder *> &orders = SqField(DaGet(SqOrders()));
    const std::vector<uint32_t> origOrders = MsvcVectorGet(g_osq + 0x24C);
    if (origOrders.size() != orders.size())
        return "orders: " + std::to_string(orders.size()) + " / " + std::to_string(origOrders.size());
    for (size_t k = 0; k < orders.size(); ++k) {
        auto *o = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(origOrders[k]));
        const std::string why = SqOrderDiffers(orders[k], o);
        if (!why.empty())
            return "order " + std::to_string(k) + ": " + why;
        g_sqOrderPairs[orders[k]] = o;
    }
    return "";
}
// The worn containers' slots (each container's item; the port's vectors and MSVC6 ones) and a vtable per side: the
// port's slots are the original's + 1 (g++'s two destructors), so GetSlots and IsEmpty meet GetItem and GetSlots.
std::vector<C_Item *> g_sqSlotsPort[16];
alignas(16) uint8_t g_sqSlotsOrig[16][16];
// (The original's slots: an array of heap vector pointers, one slot here.)
void *g_sqSlotArrayOrig[16];
void *g_sqWornPort[64], *g_sqWornOrig[64];
int SqContainerIndex(void *self) { return int(static_cast<S_FakeContainer *>(self) - g_wornContainers); }
void *AC SQ_GetSlotsPort(void *self)
{
    const int k = SqContainerIndex(self);
    g_sqSlotsPort[k].clear();
    if (g_wornContainers[k].item)
        g_sqSlotsPort[k].push_back(static_cast<C_Item *>(g_wornContainers[k].item));
    return &g_sqSlotsPort[k];
}
void *AC SQ_GetSlotsOrig(void *self)
{
    const int k = SqContainerIndex(self);
    std::vector<uint32_t> words;
    if (g_wornContainers[k].item)
        words.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OrigWeapon(g_wornContainers[k].item))));
    MsvcVectorPut(g_sqSlotsOrig[k], words);
    g_sqSlotArrayOrig[k] = g_sqSlotsOrig[k];
    return &g_sqSlotArrayOrig[k];
}
// (The original's: its own weapons.)
void *AC SQ_GetItemOrig(void *self) { return OrigWeapon(static_cast<S_FakeContainer *>(self)->item); }
int AC SQ_UsedSlotCount(void *self) { return g_wornContainers[SqContainerIndex(self)].item ? 1 : 0; }
int AC SQ_IsEmpty(void *self) { return g_wornContainers[SqContainerIndex(self)].item == nullptr; }
void SqWornFor(bool original) { std::memcpy(g_wornVt, original ? g_sqWornOrig : g_sqWornPort, sizeof g_sqWornPort); }

// (The look direction's w, the facing's +0x2C: the turns pass a stack word they do not set, the port 0.)
template <class P, class O> void SqCall(const char *what, P port, O original)
{
    DaCall(what, SquadToOrig, SquadDiffers,
           [] {
               uint8_t *facing;
               std::memcpy(&facing, g_osq + 0x124, 4);
               std::memcpy(facing + 0x2C, reinterpret_cast<const uint8_t *>(g_psq->m_facing) + 0x2C, 4);
           },
           [&] {
               g_sqMarkerNext[0] = 0;
               SqWornFor(false);
               return port();
           },
           [&](void *o) {
               g_sqMarkerNext[1] = 0;
               SqWornFor(true);
               const int r = original(o);
               SqWornFor(false);
               return r;
           });
}

// The markers each side's (the panel's fakes): the port's pool on its side, the original's on its own.

// Unfaked slots that say which they are (a vtable's CT_Unexpected entries replaced).
template <int N> void AC SqSlotReport(void *self)
{
    std::printf("unexpected call of slot %d (0x%X) on %s\n", N, N * 4, NameOf(self).c_str());
    std::exit(3);
}
template <int... N> void SqReportSlots(void **vt, std::integer_sequence<int, N...>)
{
    ((vt[N] = vt[N] == reinterpret_cast<void *>(CT_Unexpected) ? reinterpret_cast<void *>(SqSlotReport<N>) : vt[N]), ...);
}
// ---- the seat car's seats (both sides: its count, occupant, camera, gun's rounds and origin)
struct S_SqSeatFake {
    virtual int Count();
    virtual C_Actor *Occupant(int seat);
    virtual I3D_frame *Camera(int seat);
    virtual int Rounds(int seat);
    virtual S_vector Origin(int seat);
};
void *SqOccupant(bool original)
{
    if (g_sq.occupant < 0)
        return nullptr;
    if (g_sq.occupant == 0)
        return original ? static_cast<void *>(o_human) : static_cast<void *>(g_h.human);
    return original ? static_cast<void *>(g_origOther) : static_cast<void *>(PortOther());
}
int S_SqSeatFake::Count()
{
    Log(this, "GetSeatCount");
    return g_sq.seatCount;
}
C_Actor *S_SqSeatFake::Occupant(int seat)
{
    Log(this, "GetSeatOccupant", {double(seat)});
    return static_cast<C_Actor *>(SqOccupant(false));
}
I3D_frame *g_sqCamera;                                  // (the seat's camera: one frame, both sides)
I3D_frame *S_SqSeatFake::Camera(int seat)
{
    Log(this, "GetSeatCamera", {double(seat)});
    return g_sq.camera ? g_sqCamera : nullptr;
}
int S_SqSeatFake::Rounds(int seat)
{
    Log(this, "GetSeatWeaponRounds", {double(seat)});
    return g_sq.rounds;
}
S_vector S_SqSeatFake::Origin(int seat)
{
    Log(this, "GetSeatWeaponOrigin", {double(seat)});
    return g_sq.origin;
}
int AC SQ_SeatCount(void *self)
{
    Log(self, "GetSeatCount");
    return g_sq.seatCount;
}
void *AC SQ_SeatOccupant(void *self, int seat)
{
    Log(self, "GetSeatOccupant", {double(seat)});
    return SqOccupant(true);
}
void *AC SQ_SeatCamera(void *self, int seat)
{
    Log(self, "GetSeatCamera", {double(seat)});
    return g_sq.camera ? g_sqCamera : nullptr;
}
int AC SQ_SeatRounds(void *self, int seat)
{
    Log(self, "GetSeatWeaponRounds", {double(seat)});
    return g_sq.rounds;
}
float *AC SQ_SeatOrigin(void *self, float *out, int seat)
{
    Log(self, "GetSeatWeaponOrigin", {double(seat)});
    out[0] = g_sq.origin.x;
    out[1] = g_sq.origin.y;
    out[2] = g_sq.origin.z;
    return out;
}
// The other two humans' controller: ctl2 (each side's), its last target faked (a human or none: FindEnemyTargeting
// takes it for one, on both sides; another actor would be read as a human, an original flaw).
C_Actor *g_sqLastTarget = nullptr;
struct S_SqLastTargetFake {
    virtual C_Actor *LastTarget();
};
C_Actor *S_SqLastTargetFake::LastTarget()
{
    Log(this, "GetLastTarget");
    return g_sqLastTarget;
}
void *AC SQ_LastTargetOrig(void *self)
{
    Log(self, "GetLastTarget");
    return SqOrigOf(g_sqLastTarget);
}
void *g_sqPc2Vt[160], *g_sqOc2Vt[0x60];
void SqSetUpOtherControllers()
{
    std::memcpy(g_sqPc2Vt, *reinterpret_cast<void ***>(g_pc2), sizeof g_sqPc2Vt);
    std::memcpy(g_sqOc2Vt, *reinterpret_cast<void ***>(g_oc2), sizeof g_sqOc2Vt);
    S_SqLastTargetFake fake;
    g_sqPc2Vt[VSlot(&C_HumanController::GetLastTarget)] = (*reinterpret_cast<void ***>(&fake))[0];
    g_sqOc2Vt[0xA4 / 4] = reinterpret_cast<void *>(SQ_LastTargetOrig);
    *reinterpret_cast<void ***>(g_pc2) = g_sqPc2Vt;
    void **ovt = g_sqOc2Vt;
    std::memcpy(g_oc2, &ovt, 4);
}
void SqSetUpSeats()
{
    {
        S_quat turn;
        turn.Make(S_vector(0, 1, 0), 0.7f);
        g_sqCamera = NewFrame(FRAME_DUMMY, "seatCamera", S_vector(0.5f, 1.5f, 0), turn, Box(S_vector(0.1f, 0.1f, 0.1f)), "", nullptr);
        g_names[g_sqCamera] = "seatCamera";
    }
    S_SqSeatFake fake;
    void **fakeVt = *reinterpret_cast<void ***>(&fake);
    g_asCarPortVt[VSlot(&C_Actor::GetSeatCount)] = fakeVt[0];
    g_asCarPortVt[VSlot(&C_Actor::GetSeatOccupant)] = fakeVt[1];
    g_asCarPortVt[VSlot(&C_Actor::GetSeatCamera)] = fakeVt[2];
    g_asCarPortVt[VSlot(&C_Actor::GetSeatWeaponRounds)] = fakeVt[3];
    g_asCarPortVt[VSlot(&C_Actor::GetSeatWeaponOrigin)] = fakeVt[4];
    g_asCarOrigVt[0x88 / 4] = reinterpret_cast<void *>(SQ_SeatCount);
    g_asCarOrigVt[0x78 / 4] = reinterpret_cast<void *>(SQ_SeatOccupant);
    g_asCarOrigVt[0x90 / 4] = reinterpret_cast<void *>(SQ_SeatCamera);
    g_asCarOrigVt[0x9C / 4] = reinterpret_cast<void *>(SQ_SeatRounds);
    g_asCarOrigVt[0xA8 / 4] = reinterpret_cast<void *>(SQ_SeatOrigin);
    // (Part 23's actors, the formation slot's vehicles: their seats' count and occupant.)
    g_actPortVt[VSlot(&C_Actor::GetSeatCount)] = fakeVt[0];
    g_actPortVt[VSlot(&C_Actor::GetSeatOccupant)] = fakeVt[1];
    g_actOrigVt[0x88 / 4] = reinterpret_cast<void *>(SQ_SeatCount);
    g_actOrigVt[0x78 / 4] = reinterpret_cast<void *>(SQ_SeatOccupant);
}
// An item lying near (FindItemNear's answer, as the game's: an item actor; its item a weapon, the original's +0x90).
DA_ROB(SqItemActorItem, C_Item *C_ItemActor::*, &C_ItemActor::m_item)
alignas(16) uint8_t g_sqItemActorPort[0x700], g_sqItemActorOrig[0x700];
C_Actor *SqItemActor(int weapon)
{
    static bool made;
    if (!made) {
        made = true;
        *reinterpret_cast<void ***>(g_sqItemActorPort) = g_actPortVt;
        *reinterpret_cast<void ***>(g_sqItemActorOrig) = g_actOrigVt;
        auto *a = reinterpret_cast<C_Actor *>(g_sqItemActorPort);
        a->m_type = ACTOR_ITEM;
        a->m_netId = 140;
        a->m_frame = g_targetFrame;
        const int type = ACTOR_ITEM, id = 140;
        std::memcpy(g_sqItemActorOrig + 0x18, &type, 4);
        std::memcpy(g_sqItemActorOrig + 0x10, &id, 4);
        std::memcpy(g_sqItemActorOrig + 0x20, &g_targetFrame, 4);
        g_names[g_sqItemActorPort] = g_names[g_sqItemActorOrig] = "itemActor";
        g_extraPointers.push_back({g_sqItemActorPort, g_sqItemActorOrig});
    }
    reinterpret_cast<C_ItemActor *>(g_sqItemActorPort)->*DaGet(SqItemActorItem()) = reinterpret_cast<C_Item *>(g_weaponPort[weapon]);
    void *o = g_weaponOrig[weapon];
    std::memcpy(g_sqItemActorOrig + 0x90, &o, 4);
    return reinterpret_cast<C_Actor *>(g_sqItemActorPort);
}
// The leader's formation (a block per side: the leader at +0x74 on both).
alignas(16) uint8_t g_sqFormation[0x100], g_sqOrigFormation[0x100];
void SetUpSquad()
{
    SqReportSlots(g_wornVt, std::make_integer_sequence<int, 64>());
    std::memcpy(g_sqWornPort, g_wornVt, sizeof g_sqWornPort);
    std::memcpy(g_sqWornOrig, g_wornVt, sizeof g_sqWornOrig);
    g_sqWornPort[VSlot(&C_ItemContainer::GetSlots)] = reinterpret_cast<void *>(SQ_GetSlotsPort);
    g_sqWornPort[VSlot(&C_ItemContainer::IsEmpty)] = reinterpret_cast<void *>(SQ_IsEmpty);
    g_sqWornPort[VSlot(&C_ItemContainer::GetItem)] = reinterpret_cast<void *>(WC_GetItem);
    g_sqWornOrig[0x3C / 4] = reinterpret_cast<void *>(SQ_GetSlotsOrig);
    g_sqWornOrig[0x38 / 4] = reinterpret_cast<void *>(SQ_IsEmpty);
    g_sqWornOrig[0x40 / 4] = reinterpret_cast<void *>(SQ_GetItemOrig);
    g_sqWornPort[VSlot(&C_ItemContainer::GetUsedSlotCount)] = reinterpret_cast<void *>(SQ_UsedSlotCount);
    g_sqWornOrig[0x28 / 4] = reinterpret_cast<void *>(SQ_UsedSlotCount);
    SqWornFor(false);
    SetUpDerivedAi();
    SetUpSquadFakes();
    SqSetUpMarkers();
    SqSetUpSeats();
    SqSetUpOtherControllers();
    g_findPathFake = SqFindPath;
    g_volumeLogger = SqOrderVolume;
    // (The original's static initialiser of the cover compass, 0x008aad90: its copy never ran it.)
    OriginalAt<void (*)()>(0x00525160)();
    // (Part 23's car actor's "body" frame: AimAt aims at it; 1 m above its frame.)
    {
        S_quat none;
        none.Identity();
        NewFrame(FRAME_DUMMY, "body", S_vector(0, 1, 0), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "",
                 static_cast<C_Actor *>(PortActor(3))->GetFrame());
    }
    g_names[g_sqFormation] = g_names[g_sqOrigFormation] = "formation";
    g_names[&g_itemsModule] = g_names[OriginalAt<void *>(0x0098b020)] = "itemsModule";
    // (The cars' vehicle parts: the port's member at its own offset, the original's +0x90.)
    g_names[reinterpret_cast<C_Car *>(g_asCarPort)->GetVehicle()] = g_names[g_asCarOrig + 0x90] = "seatCarVehicle";
    g_names[static_cast<C_Car *>(static_cast<C_Actor *>(PortActor(3)))->GetVehicle()] =
        g_names[static_cast<uint8_t *>(SqOrigOf(PortActor(3))) + 0x90] = "carActorVehicle";
    // (The other two humans' containers: the worn ones (+0x74 in the original); IsFriendOf reads their uniforms.)
    void *table = g_wornTable;
    for (C_Human *o : {PortOther(), PortThird()})
        o->m_containers = g_wornTable;
    std::memcpy(g_origOther + 0x74, &table, 4);
    std::memcpy(g_origThird + 0x74, &table, 4);
    g_extraPointers.push_back({g_sqFormation, g_sqOrigFormation});
    g_actorLookupsOnce = true;
    g_checkpointGetSilent = true;
    g_daAppend = true;
    g_fireDirW = false;
    g_behaviourOf = nullptr;
    // The port's: Create()'s object, its vtable copied, ExecuteCommand logged.
    g_psq = S_Sq::Create();
    void **vt = *reinterpret_cast<void ***>(g_psq);
    std::memcpy(g_psqVt, vt, sizeof g_psqVt);
    g_psqVt[VSlot(&C_HumanController::ExecuteCommand)] = reinterpret_cast<void *>(OA_ExecuteCommand);
    *reinterpret_cast<void ***>(g_psq) = g_psqVt;
    g_osq = DaOrigController(g_psq, 0x260, HUMAN_CONTROLLER_SQUAD, 0x008109f8, g_osqVt, "squad");
    // (The squad's own slots past the copy DaOrigController makes: 83 in all.)
    std::memcpy(g_osqVt + 0x48, OriginalAt<void *const *>(0x008109f8) + 0x48, (83 - 0x48) * sizeof(void *));
}
// ---- the state
DA_ROB(SqFmLeader, C_Human *C_FormationShape::*, &C_FormationShape::m_leader)
int SqTimer(int edge) { return P(0.2f) ? edge + I(-1, 1) : P(0.25f) ? 0 : I(0, 2 * edge); }
void SqReleaseOrders()
{
    std::vector<S_HumanOrder *> &orders = SqField(DaGet(SqOrders()));
    typedef void(__stdcall * T_Release)(void *);
    for (S_HumanOrder *p : orders) {
        uint8_t *o = SqOrigOrder(p);
        ReleaseHumanOrder(p);
        if (o)
            OriginalAt<T_Release>(0x00583980)(o);
    }
    orders.clear();
}
S_vector4 SqNear(float spread)
{
    return ToVector4(g_h.frame->GetWorldPos() + S_vector(U(-spread, spread), U(-1, 1), U(-spread, spread)), 1.0f);
}

// The task pools' next objects alike: DaPrimeTaskPool's stale words and +0x08 too (AllocTask1 and AllocTask12 leave
// it as the pool's object had it).
void SqPrimeTaskPool(int type)
{
    const int stale08 = AiInt(), stale0C = AiInt(), stale10 = AiInt();
    S_AITask *t = AllocTask(type);
    t->unknown08 = stale08;
    t->unknown0C = stale0C;
    t->unknown10 = stale10;
    uint8_t *o = OrigTask(type);
    // (Its own fields too, alike: types 7 .. 12 save only the base's, a loaded one keeps the pool object's.)
    const std::vector<S_AiRun> runs = TaskRuns(type);
    // (The first run's words past the base's, +0x14 on: 0 on both sides, they may be pointers.)
    for (size_t at = 0x14 - runs[0].orig; at + 4 <= runs[0].size; at += 4) {
        std::memset(reinterpret_cast<uint8_t *>(t) + runs[0].port + at, 0, 4);
        std::memset(o + runs[0].orig + at, 0, 4);
    }
    for (size_t r = 1; r < runs.size(); ++r)
        for (size_t at = 0; at + 4 <= runs[r].size; at += 4) {
            const float v = U(-50, 50);
            std::memcpy(reinterpret_cast<uint8_t *>(t) + runs[r].port + at, &v, 4);
            std::memcpy(o + runs[r].orig + at, &v, 4);
        }
    ReleaseTask(t);
    std::memcpy(o + 0x08, &stale08, 4);
    std::memcpy(o + 0x0C, &stale0C, 4);
    std::memcpy(o + 0x10, &stale10, 4);
    typedef void(AC * T_Release)(void *, void *);
    OriginalAt<T_Release>(0x00582c50)(AiPools(), o);
}

// The fighting's state (part 36's), then the squad's: its fields, its action, its orders, the leader and the formation,
// the answers.
void RandomSquad(bool aiming)
{
    RandomFireOrder(true);
    S_Sq *s = Sq();
    C_Human *h = g_h.human;
    C_Human *const humans[3] = {h, aiming ? h : PortOther(), aiming ? h : PortThird()};
    if (aiming) {
        h->m_seenEnemies.clear();
        for (int k = P(0.3f) ? 0 : I(1, 2); k > 0; --k)
            h->m_seenEnemies.push_back(h);
        if (g_pa->m_fireOrder->target && g_pa->m_fireOrder->target != h)
            g_pa->m_fireOrder->target = P(0.5f) ? nullptr : h;
    } else {
        auto &enemies = h->m_seenEnemies;
        enemies.clear();
        for (int k = P(0.5f) ? 0 : I(1, 2); k > 0; --k)
            enemies.push_back(k == 1 ? PortOther() : PortThird());
    }
    // (Part 23's actors' frame, turned: around the member now and then (an action's 1.5 m, a weapon's ranges), else
    // up to 60 m off.)
    {
        const S_vector &at = h->GetFrame()->GetWorldPos();
        const float spread = P(0.5f) ? 2.5f : U(3, 60);
        S_quat turn;
        turn.Make(S_vector(0, 1, 0), U(-3.1f, 3.1f));
        g_targetFrame->SetRot(turn);
        g_targetFrame->SetPos(S_vector(at.x + U(-spread, spread), at.y + U(-2, 2), at.z + U(-spread, spread)));
        g_targetFrame->Update();
    }
    // (The squad is single player's: a local game.)
    g_gameRole = GAME_ROLE_LOCAL;
    // (The fire order's target a human or none: the squad sets only humans there (its commander's target is a human
    // that hit it), and CanFireAt reads the target's body frame on both sides.)
    if (g_pa->m_fireOrder->target && g_pa->m_fireOrder->target->GetType() != ACTOR_HUMAN)
        g_pa->m_fireOrder->target = P(0.5f) ? nullptr : static_cast<C_Actor *>(h);   // (the others have no "a" mesh to aim at)
    // (The other two humans' controller: ctl2, not the AI pair, whose fire target the tick changes; their last target
    // a human or none.)
    for (int k = 0; k < 2; ++k) {
        (k ? PortThird() : PortOther())->m_controller = g_pc2;
        void *oc = g_oc2;
        std::memcpy((k ? g_origThird : g_origOther) + 0xB0, &oc, 4);
    }
    g_sqLastTarget = P(0.4f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 2)]);
    // (The leader's controller, ctl2: a weapon in its hands, raised or not; the carry mode of the member's.)
    g_pc2->m_handsItem = P(0.4f) ? nullptr : reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
    g_pc2->m_weaponRaised = P(0.5f);
    g_asCarryMode = P(0.4f) ? 3 : I(0, 8);
    // The stimuli (shared events): the kinds the squad tells apart, their sources humans, none or unknown ids (both
    // sides take a source for a human; another actor would be read as one); now and then an enemy killed 0.75 s ago.
    {
        std::deque<S_HumanEvent *> &stimuli = g_daEvents.*DaGet(EvStimuli());
        stimuli.clear();
        for (int k = P(0.4f) ? 0 : I(1, 4); k > 0; --k)
            stimuli.push_back(&g_daEv[I(0, DA_EVENTS - 1)]);
        for (S_HumanEvent &ev : g_daEv) {
            static const uint32_t kinds[] = {AI_EVENT_KILLED_ENEMY, AI_EVENT_HIT_5, AI_EVENT_LOUD_SOUND, AI_EVENT_SHOT_AT,
                                             AI_EVENT_SOUND_0, AI_EVENT_SOUND_2, AI_EVENT_ENEMY_LOST, AI_EVENT_BODY_SEEN, 1};
            ev.type = kinds[I(0, 8)];
            ev.sourceId = P(0.2f) ? -1 : P(0.75f) ? humans[I(0, 2)]->GetNetId() : I(0, 50);
            ev.age = P(0.3f) ? 750 + I(-1, 1) : I(0, 3000);
            ev.timeLeft = I(0, 30000);
        }
    }
    s->m_events = &g_daEvents;
    h->m_events = &g_daEvents;
    g_pc2->m_human = PortOther();
    if (!g_pa->m_handsItem)
        g_pa->m_fireOrder->reload = false;
    g_cs.itemFrame = g_shdumParent;
    // (The belt and the backpack's contents: a weapon or none; the squad looks through them and asks their amounts,
    // part 21's worn items have no vtable.)
    for (int c : {ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_BELT, ITEM_CONTAINER_BACKPACK_CONTENTS})
        g_wornContainers[c].item = P(0.4f) ? nullptr : static_cast<void *>(g_weaponPort[I(0, 1)]);
    // (A uniform worn, always, as in the game: the swap reads the new one.)
    g_wornContainers[ITEM_CONTAINER_UNIFORM].item = g_wornItems[ITEM_CONTAINER_UNIFORM];
    // (The hands: the controller's item a weapon or none, the container's the same; now and then another one, unless a
    // reload is due: the tick takes the container's, and the original reloads a missing item.)
    C_Item *&handsItem = g_pa->m_handsItem;
    if (handsItem && handsItem != static_cast<void *>(g_weaponPort[0]) && handsItem != static_cast<void *>(g_weaponPort[1]))
        handsItem = P(0.4f) ? nullptr : reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
    if (!handsItem)
        g_pa->m_fireOrder->reload = false;
    void *&hands = g_wornContainers[ITEM_CONTAINER_HANDS].item;
    hands = handsItem;
    if (!g_pa->m_fireOrder->reload && P(0.1f))
        hands = P(0.4f) ? nullptr : static_cast<void *>(g_weaponPort[I(0, 1)]);
    g_handsWeapon = hands;
    g_cs.found = P(0.4f) ? nullptr : reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);                              // (the original's own put in before it runs: WeaponsToOriginal)
    // (The initial endurance not 0: UpdateStamina divides by it, on both sides.)
    if (h->m_initialStats[STAT_ENDURANCE] == 0)
        h->m_initialStats[STAT_ENDURANCE] = I(1, 900);
    // (First aid a percentage, as the game's: SelfHeal divides by (100 - it) / 2 + 10.)
    if (h->m_stats[STAT_FIRST_AID] > 100 || h->m_stats[STAT_FIRST_AID] < 0)
        h->m_stats[STAT_FIRST_AID] = I(0, 100);
    // The move flags: dead now and then, the stance, the pace.
    uint32_t &f = s->m_moveFlags;
    f &= ~uint32_t(MOVE_DEAD | MOVE_UNCONSCIOUS | MOVE_PACE_MASK | MOVE_SWIMMING);
    if (P(0.05f))
        f |= MOVE_DEAD;
    if (P(0.05f))
        f |= MOVE_SWIMMING;
    static const uint32_t paces[] = {MOVE_PACE_1, MOVE_PACE_3, MOVE_PACE_4, MOVE_PACE_2, MOVE_PACE_MARCH, 0};
    f |= paces[I(0, 5)];
    g_pa->m_move->unknown54 = (g_pa->m_move->unknown54 & ~MOVE_PACE_MASK) | paces[I(0, 5)];
    // The squad's fields.
    s->*DaGet(SqEngagePos()) = SqNear(30);
    s->*DaGet(SqBoundDir()) = AiDir();
    s->*DaGet(SqHoldDir()) = AiDir();
    s->*DaGet(SqPace()) = paces[I(0, 5)];
    s->*DaGet(SqFlags()) = uint32_t(I(0, 0x3FF));
    s->*DaGet(SqOrderStance()) = P(0.5f) ? 0u : uint32_t(I(1, 4));
    s->*DaGet(SqCoverStance()) = P(0.5f) ? 0u : uint32_t(I(1, 4));
    s->*DaGet(SqOrderPace()) = P(0.5f) ? 0u : paces[I(0, 5)];
    s->*DaGet(SqIdleCheckpoint()) = P(0.3f) ? -1 : I(0, 7);
    s->*DaGet(SqQuiet()) = P(0.3f);
    s->*DaGet(SqFollowing()) = P(0.6f);
    s->*DaGet(SqSilentMode()) = P(0.3f);
    s->*DaGet(SqLyingToFire()) = P(0.2f);
    s->*DaGet(SqSteppedAside()) = P(0.2f);
    s->*DaGet(SqOrdersHeld()) = P(0.2f);
    s->*DaGet(SqRestartOrder()) = P(0.3f);
    s->*DaGet(SqBoundHops()) = I(0, 5);
    s->*DaGet(SqEngageTime()) = SqTimer(1000);
    s->*DaGet(SqEventTime()) = SqTimer(1000);
    s->*DaGet(SqAlertTime()) = SqTimer(1000);
    s->*DaGet(SqWeaponCheckTime()) = SqTimer(200);
    s->*DaGet(SqFormationTime()) = SqTimer(1000);
    s->*DaGet(SqFireMoveTime()) = P(0.5f) ? 0 : I(-2000, 2000);
    s->*DaGet(SqAirCheckTime()) = SqTimer(1000);
    s->*DaGet(SqFollowDelay()) = SqTimer(500);
    s->*DaGet(SqStatusIconTime()) = SqTimer(100);
    s->*DaGet(SqModeIconTime()) = SqTimer(100);
    const int now = int(GetGame()->GetTime());
    s->*DaGet(SqOrdersChangedTime()) = P(0.3f) ? now + I(-1, 1) : I(0, 100000);
    s->*DaGet(SqCoverRadius()) = U(0, 10);
    s->*DaGet(SqFriendlyFire()) = P(0.5f) ? 0.0f : U(0, 100);
    s->*DaGet(SqStatusIcon()) = I(0, 15);
    s->*DaGet(SqTempStatusIcon()) = P(0.5f) ? -1 : I(16, 19);
    s->*DaGet(SqModeIcon()) = I(0, 11);
    s->*DaGet(SqTempModeIcon()) = P(0.5f) ? -1 : I(12, 39);
    s->*DaGet(SqCombatMode()) = I(0, 3);
    S_SquadAction &a = s->*DaGet(SqAction());
    // (The actions: the squad's own and the commands, each with the target kind the game gives it below.)
    static const int actions[] = {0x16, 0x17, 0x18, 0x19, 0x1A, 0x20, 0x21, 0x25, 0x28, 0x29, 0x2A, 0x34, 3, 7,
                                  HUMAN_COMMAND_LOCK_DOOR, HUMAN_COMMAND_UNLOCK_DOOR, HUMAN_COMMAND_DEFUSE_MINE, HUMAN_COMMAND_PLANT_EXPLOSIVE,
                                  HUMAN_COMMAND_LEAVE_VEHICLE, HUMAN_COMMAND_PICK_UP_BODY, HUMAN_COMMAND_PICK_LOCK, HUMAN_COMMAND_HEAL,
                                  HUMAN_COMMAND_CUT_WIRE, HUMAN_COMMAND_TIE_UP, HUMAN_COMMAND_OPEN_DOOR, HUMAN_COMMAND_CLOSE_DOOR,
                                  HUMAN_COMMAND_DELIVER_DOCUMENTS, HUMAN_COMMAND_DISARM_EXPLOSIVE};
    a.type = actions[I(0, 27)];
    a.pos = SqNear(20);
    a.dir = AiDir();
    a.arrived = P(0.5f);
    a.turned = P(0.5f);
    a.finished = P(0.5f);
    a.keepGoing = P(0.5f);
    a.noCommand = P(0.3f);
    a.started = P(0.5f);
    a.completed = P(0.5f);
    a.skipTurn = P(0.3f);
    a.notifyModule = P(0.3f);
    a.useDir = P(0.3f);
    a.targetId = I(-1, 120);
    a.slot = I(0, 3);
    a.delay = SqTimer(500);
    a.stance = P(0.5f) ? 0u : uint32_t(I(1, 4));
    a.radius = U(0, 5);
    a.target = P(0.3f) ? nullptr : P(0.5f) ? static_cast<C_Actor *>(humans[I(1, 2)]) : static_cast<C_Actor *>(PortActor(I(0, 11)));
    // (An attack has a human target, a vehicle attack a car or a static weapon, following its leader, as in the game:
    // both sides read them. The attacked human: the tested one, the others have no model to aim at.)
    if (a.type == SQUAD_ACTION_ATTACK || a.type == SQUAD_ACTION_ATTACK_SILENTLY)
        a.target = h;
    if (a.type == SQUAD_ACTION_ATTACK_VEHICLE)
        a.target = static_cast<C_Actor *>(PortActor(P(0.5f) ? 3 : 5));
    if (!a.target && a.type == SQUAD_ACTION_FOLLOW)
        a.target = humans[I(1, 2)];
    switch (a.type) {
    case HUMAN_COMMAND_LOCK_DOOR:
    case HUMAN_COMMAND_UNLOCK_DOOR:
    case HUMAN_COMMAND_PICK_LOCK:
    case HUMAN_COMMAND_OPEN_DOOR:
    case HUMAN_COMMAND_CLOSE_DOOR:
        a.target = static_cast<C_Actor *>(PortActor(0));
        break;
    case HUMAN_COMMAND_DEFUSE_MINE:
        a.target = static_cast<C_Actor *>(PortActor(1));
        break;
    case HUMAN_COMMAND_PLANT_EXPLOSIVE:
    case HUMAN_COMMAND_DISARM_EXPLOSIVE:
        a.target = static_cast<C_Actor *>(PortActor(2));
        break;
    case HUMAN_COMMAND_CUT_WIRE:
        a.target = static_cast<C_Actor *>(PortActor(11));
        break;
    case HUMAN_COMMAND_DELIVER_DOCUMENTS:
        a.target = static_cast<C_Actor *>(PortActor(10));
        break;
    case HUMAN_COMMAND_ENTER_VEHICLE:
    case HUMAN_COMMAND_LEAVE_VEHICLE: {
        static const int kinds[] = {3, 4, 5, 7, 8, 9};
        a.target = P(0.3f) ? reinterpret_cast<C_Actor *>(g_asCarPort) : static_cast<C_Actor *>(PortActor(kinds[I(0, 5)]));
        break;
    }
    case HUMAN_COMMAND_PICK_UP_BODY:
        a.target = P(0.3f) ? SqItemActor(I(0, 1)) : static_cast<C_Actor *>(humans[I(1, 2)]);
        break;
    case HUMAN_COMMAND_HEAL:
    case HUMAN_COMMAND_TIE_UP:
    case SQUAD_ACTION_SEARCH_BODY:
    case SQUAD_ACTION_SWAP_UNIFORM:
        a.target = humans[I(1, 2)];
        break;
    case SQUAD_ACTION_21:   // (taking items: from a human, the game's always has one)
        if (!a.target)
            a.target = humans[I(1, 2)];
        break;
    default:
        break;
    }
    s->*DaGet(SqGunnerLooksAhead()) = P(0.5f);
    s->*DaGet(SqSelfHealTime()) = SqTimer(1000);
    s->*DaGet(SqPickedTarget()) = nullptr;
    {
        const bool air = P(0.25f);
        s->*DaGet(SqAirTarget()) = air ? static_cast<C_Actor *>(PortActor(6)) : nullptr;
        s->*DaGet(SqAirTargetFrame()) = air ? g_targetFrame : nullptr;
    }
    s->*DaGet(SqMineNear()) = P(0.3f);
    s->*DaGet(SqMineCheckTime()) = SqTimer(250);
    // The orders (each side's own, made alike).
    SqReleaseOrders();
    for (int k = P(0.35f) ? 0 : I(1, 4); k > 0; --k) {
        const int kind = P(0.8f) ? I(1, 4) : P(0.5f) ? 6 : 7;
        const int arg = P(0.2f) ? SQUAD_ACTION_FOLLOW : I(0, 0x40);
        C_Actor *actor = P(0.5f) ? nullptr : P(0.5f) ? static_cast<C_Actor *>(humans[I(1, 2)]) : static_cast<C_Actor *>(PortActor(I(0, 11)));
        SqField(DaGet(SqOrders())).push_back(SqAllocOrder(kind, arg, SqNear(25), AiDir(), paces[I(0, 5)], uint32_t(I(0, 4)),
                                                          uint32_t(I(0, 4) << 8), actor, I(0, 5)));
    }
    // The leader (the other human) and its formation (its +0x2C8; the human's leader +0x2CC).
    std::memset(g_sqFormation, 0, sizeof g_sqFormation);
    std::memset(g_sqOrigFormation, 0, sizeof g_sqOrigFormation);
    C_Human *leader = P(0.2f) ? nullptr : PortOther();
    reinterpret_cast<C_FormationShape *>(g_sqFormation)->*DaGet(SqFmLeader()) = leader;
    void *ol = SqOrigOf(leader);
    std::memcpy(g_sqOrigFormation + 0x74, &ol, 4);
    h->m_leader = leader;
    std::memcpy(o_human + 0x2CC, &ol, 4);
    PortOther()->m_formation = reinterpret_cast<C_FormationShape *>(g_sqFormation);
    void *of = g_sqOrigFormation;
    std::memcpy(g_origOther + 0x2C8, &of, 4);
    // The answers.
    g_sq.whispering = P(0.3f);
    g_sq.seatCount = I(1, 4);
    g_sq.occupant = I(-1, 1);
    g_sq.rounds = P(0.3f) ? 0 : I(1, 50);
    const S_vector4 origin = SqNear(3);
    g_sq.origin = S_vector(origin.x, origin.y + 1, origin.z);
    g_sq.squadTarget = P(0.7f) ? nullptr : static_cast<C_Actor *>(humans[I(1, 2)]);
    g_sq.selected = P(0.3f) ? nullptr : humans[I(0, 2)];
    g_sq.memberCount = I(0, 3);
    g_sq.aliveCount = I(0, 3);
    g_sq.enemyKnown = P(0.5f);
    g_sq.seenNearby = P(0.5f) ? nullptr : humans[I(1, 2)];
    g_sq.unseenBy = P(0.5f) ? nullptr : humans[I(1, 2)];
    g_sq.slot = P(0.8f);
    g_sq.slotIndex = I(0, 5);
    g_sq.slotPos = SqNear(P(0.3f) ? 0.4f : 15);
    g_sq.slotPoint = uint16_t(P(0.2f) ? 0xFFFF : I(0, 7));
    g_sq.slotDir = AiDir();
    g_sq.slotSeat = I(-1, 3);
    g_sq.slotVehicle = P(0.5f) ? -1 : 100 + I(3, 6);
    g_sq.seatEntry = P(0.8f);
    g_sq.entryPos = SqNear(5);
    g_sq.entryDir = AiDir();
    g_sq.localAboard = P(0.3f);
    g_sq.crewSide = I(0, 2);
    g_sq.canEnter = P(0.7f);
    g_sq.medkit = P(0.5f);
    g_sq.controllerType = I(0, 5);
    g_sq.itemCount = I(0, 3);
    g_sq.collected = I(0, 2);
    g_sq.pathLength = U(0, 50);
    g_sq.coverWithin.clear();
    for (int k = I(0, 4); k > 0; --k)
        g_sq.coverWithin.push_back(uint16_t(I(0, 7)));
    g_sq.byPrefix.clear();
    for (int k = P(0.5f) ? 0 : I(1, 3); k > 0; --k)
        g_sq.byPrefix.push_back(uint16_t(I(0, 7)));
    for (int &k : g_sq.pointActor)
        k = P(0.6f) ? -1 : I(0, 11);
    g_sq.actorPoint = P(0.3f) ? 0xFFFF : I(0, 7);
    g_sq.wireCut = P(0.5f);
    g_sq.canPut = I(0, 2);
    g_sq.airAttack = nullptr;
    g_sq.junkers = nullptr;
    g_sq.mineInRange = P(0.3f);
    g_sq.itemNear = P(0.6f) ? nullptr : SqItemActor(I(0, 1));
    g_sq.outdoors = P(0.6f);
    g_sq.spheres = P(0.3f) ? 0 : I(1, 4);
    for (S_vector4 &sp : g_sqSpheres)
        sp = ToVector4(XYZ(SqNear(20)), U(0.1f, 3));
    g_sq.rayHit = P(0.4f);
    g_sq.dynamicHit = P(0.5f) ? -1 : I(0, 11);
    g_sq.facing = S_vector(U(-1, 1), 0, U(-1, 1)).Normalized();
    g_sqMaxAmount = P(0.4f) ? g_ws.amount : float(I(0, 40));
    for (int k = 0; k < 2; ++k) {
        const uint32_t now = GetGame()->GetMissionTime();
        const uint32_t ready = P(0.4f) ? 0u : now + uint32_t(P(0.3f) ? I(399, 401) : I(0, 2000));
        reinterpret_cast<C_WeaponItem *>(g_weaponPort[k])->*DaGet(SqReadyTime()) = ready;
        std::memcpy(g_weaponOrig[k] + 0x5C, &ready, 4);
    }
    g_sqSubmarines.clear();
    // The vehicle: none, or the seat car as a car, a submarine or a static weapon.
    static const int vehicleTypes[] = {ACTOR_CAR, ACTOR_SUBMARINE, ACTOR_STATIC_WEAPON, ACTOR_DOOR};
    const int vehicleType = vehicleTypes[I(0, 3)];
    auto *car = reinterpret_cast<C_Actor *>(g_asCarPort);
    car->m_type = static_cast<E_ActorType>(vehicleType);
    std::memcpy(g_asCarOrig + 0x18, &vehicleType, 4);
    h->m_vehicle = P(0.7f) ? nullptr : car;
    h->m_seat = I(0, 3);
    void *ov = h->m_vehicle ? static_cast<void *>(g_asCarOrig) : nullptr;
    std::memcpy(o_human + 0x160, &ov, 4);
    std::memcpy(o_human + 0x164, &h->m_seat, 4);
    // (The leader, the other human: in the seat car, in part 23's car or on foot; +0x160 in the original.)
    {
        C_Actor *leaderVehicle = P(0.6f) ? nullptr : P(0.5f) ? car : static_cast<C_Actor *>(PortActor(3));
        PortOther()->m_vehicle = leaderVehicle;
        void *olv = SqOrigOf(leaderVehicle);
        std::memcpy(g_origOther + 0x160, &olv, 4);
    }
    // (The seat car as a submarine: its speed, +0xF4 in the original; the submarines to follow: it and part 23's.)
    {
        const float speed = P(0.3f) ? 2.0f + U(-0.05f, 0.05f) : U(0, 6);
        static_cast<C_Submarine *>(car)->SetSpeed(speed);
        std::memcpy(g_asCarOrig + 0xF4, &speed, 4);
        const float turn = U(-0.5f, 0.5f);
        static_cast<C_Submarine *>(car)->SetTurn(turn);
        std::memcpy(g_asCarOrig + 0xF8, &turn, 4);
        // (Their places and headings, +0x30 and +0x40 on both sides: 0..60 m apart, now and then on one spot or
        // heading straight up.)
        auto *other = static_cast<C_Actor *>(PortActor(4));
        const S_vector4 at = SqNear(P(0.2f) ? 0 : 8);
        const S_vector4 heading = P(0.15f) ? S_vector4{0, 1, 0, 0} : AiDir();
        const float range = P(0.2f) ? 0 : U(0, 60);
        const S_vector4 otherAt = {at.x + range * heading.x, at.y + U(-3, 3), at.z + range * heading.z, 1.0f};
        car->m_position = at;
        car->m_direction = heading;
        other->m_position = otherAt;
        std::memcpy(g_asCarOrig + 0x30, &at, 16);
        std::memcpy(g_asCarOrig + 0x40, &heading, 16);
        std::memcpy(static_cast<uint8_t *>(SqOrigOf(other)) + 0x30, &otherAt, 16);
        if (P(0.6f)) {
            g_sqSubmarines.push_back(car);
            if (P(0.7f))
                g_sqSubmarines.push_back(static_cast<C_Actor *>(PortActor(4)));
            if (P(0.3f))
                std::swap(g_sqSubmarines.front(), g_sqSubmarines.back());
        }
    }
    g_asSeat.hasWeapon = P(0.5f);
    g_asSeat.def = g_asSeat.hasWeapon || P(0.3f);       // (a seat with a weapon has its definition: AimSeatWeapon reads it)
    g_asSeatFlags = P(0.5f) ? 0x10u : uint32_t(I(0, 0xFF));
    // The speech (part 23's).
    h->m_speechSet = P(0.1f) ? nullptr : reinterpret_cast<C_SpeechSet *>(g_speechSetBytes2);
    s->m_speechDelay = P(0.7f) ? 0 : I(-5, 2000);
    g_cs.speech = P(0.1f) ? I(-3, 0) : I(0, 40000);
    g_cs.sampleSize = P(0.1f) ? 0 : I(1, 3000);
    g_combat.seenEnemy = P(0.5f);
    g_cs.answer = P(0.5f);
    g_cs.nearest = P(0.3f) ? CHECKPOINT_NONE : I(0, 7);
    // (UpdateMove: the results the tick tells apart, and others.)
    static const int moves[] = {0, MOVE_RESULT_MOVING, MOVE_RESULT_STAYING, MOVE_RESULT_ENDED, MOVE_RESULT_NO_PATH, -6,
                                MOVE_RESULT_STUCK, MOVE_RESULT_WAITED, MOVE_RESULT_WAITING, MOVE_RESULT_DOOR_LOCKED};
    g_da.move = moves[I(0, 9)];
    g_sq.driving = P(0.3f) ? 1 : I(-1, 2);
    g_sq.camera = P(0.6f);
    g_sq.path.clear();
    for (int k = P(0.3f) ? 0 : I(1, 4); k > 0; --k)
        g_sq.path.push_back(uint16_t(I(0, 7)));
    g_da.longGun = P(0.5f);
    g_da.neighbours.clear();
    for (int k = I(0, 6); k > 0; --k)
        g_da.neighbours.push_back(uint16_t(I(0, 7)));
    for (int t = 1; t <= 12; ++t)
        SqPrimeTaskPool(t);
    DaRandomPoints(s);
}

// A player's or a script's controller as Attach's previous one: ctl2's bytes in blocks big enough for the player's
// squad flags and combat order (+0x528, +0x548) and the script's flags (+0x1B0), set alike (the port's at its own
// offsets).
DA_ROB(SqPlSquadFlags, uint32_t C_PlayerController::*, &C_PlayerController::m_squadFlags)
DA_ROB(SqPlCombatOrder, int C_PlayerController::*, &C_PlayerController::m_squadCombatOrder)
DA_ROB(SqScScriptFlags, uint32_t C_ScriptedController::*, &C_ScriptedController::m_scriptFlags)
alignas(16) uint8_t g_sqPrevPort[0x800], g_sqPrevOrig[0x800];
C_HumanController *SqPreviousBlocks(int type)
{
    static bool named;
    if (!named) {
        named = true;
        g_names[g_sqPrevPort] = g_names[g_sqPrevOrig] = "previous";
        g_extraPointers.push_back({g_sqPrevPort, g_sqPrevOrig});
    }
    std::memset(g_sqPrevPort, 0, sizeof g_sqPrevPort);
    std::memset(g_sqPrevOrig, 0, sizeof g_sqPrevOrig);
    std::memcpy(g_sqPrevPort, static_cast<void *>(g_pc2), sizeof(C_HumanController));
    std::memcpy(g_sqPrevOrig, g_oc2, 0xC0);
    auto *pc = reinterpret_cast<C_HumanController *>(g_sqPrevPort);
    pc->m_type = static_cast<E_HumanController>(type);
    ControllerToOrig(pc, g_sqPrevOrig);
    const uint32_t flags = P(0.3f) ? 0u : uint32_t(I(0, 0xFFFF));
    // (Not MODE0_ALT: its engaging can leave the hands empty, and the original then reads the missing item's type, a
    // crash the port fixes.)
    int order = P(0.1f) ? I(0, 0x14) : I(SQUAD_ORDER_MODE0, SQUAD_ORDER_MODE1_ALT);
    if (order == SQUAD_ORDER_MODE0_ALT)
        order = SQUAD_ORDER_MODE0;
    reinterpret_cast<C_PlayerController *>(g_sqPrevPort)->*DaGet(SqPlSquadFlags()) = flags;
    reinterpret_cast<C_PlayerController *>(g_sqPrevPort)->*DaGet(SqPlCombatOrder()) = order;
    reinterpret_cast<C_ScriptedController *>(g_sqPrevPort)->*DaGet(SqScScriptFlags()) = flags;
    std::memcpy(g_sqPrevOrig + 0x528, &flags, 4);
    std::memcpy(g_sqPrevOrig + 0x548, &order, 4);
    std::memcpy(g_sqPrevOrig + 0x1B0, &flags, 4);
    return pc;
}

int SqDt() { return P(0.1f) ? 0 : P(0.1f) ? I(1000, 6000) : I(1, 200); }
int SqHuman(const void *p, bool orig) { return DaWord(p, orig); }

void TestSquadA()
{
    std::printf("squad: life cycle, updates, the idle member, the order queue, icons, senses, vehicles, saves\n");
    C_Human *h = g_h.human;
    // The destructor (the port's alone: the original frees only the queue's storage, its orders leak): the orders left
    // in the queue go back to their pool, the next one made is the last of them.
    {
        C_SquadController *t = S_Sq::Create();
        const S_vector4 at = SqNear(5), dir = AiDir();
        S_HumanOrder *first = AllocHumanOrder(h, 1, 0, at, dir, MOVE_PACE_4, 0, 0, nullptr, 0);
        S_HumanOrder *last = AllocHumanOrder(h, 2, 0, at, dir, MOVE_PACE_4, 0, 0, nullptr, 0);
        (static_cast<S_Sq *>(t)->*DaGet(SqOrders())).push_back(first);
        (static_cast<S_Sq *>(t)->*DaGet(SqOrders())).push_back(last);
        delete t;
        S_HumanOrder *again = AllocHumanOrder(h, 1, 0, at, dir, MOVE_PACE_4, 0, 0, nullptr, 0);
        CHECK(again == last);
        ReleaseHumanOrder(again);
    }
    // (HUMAN_SQUAD_CASE: one case alone, for debugging.)
    const char *only = std::getenv("HUMAN_SQUAD_CASE");
    const int rounds = only ? 2000 : 30000 / QUICK;
    for (int i = 0; i < rounds; ++i) {
        int which = I(0, 42);   // (31 .. 42: the tick)
        if (only)
            which = std::atoi(only);
        RandomSquad(false);
        S_Sq *s = Sq();
        const int dt = SqDt();
        C_Human *other = P(0.5f) ? PortOther() : PortThird();
        switch (which) {
        case 0: {
            C_HumanController *previous = P(0.3f) ? nullptr : P(0.5f) ? static_cast<C_HumanController *>(g_pc2) : static_cast<C_HumanController *>(g_pa);
            // (A player's or a script's previous controller: ctl2 copied into blocks with room for their fields.)
            if (previous && (previous->m_type == HUMAN_CONTROLLER_PLAYER || previous->m_type == HUMAN_CONTROLLER_AI))
                previous = SqPreviousBlocks(previous->m_type);
            SqCall("Attach", [&] { s->Attach(h, previous); return 0; },
                   [&](void *) { OAi<void>(0x00524e70, static_cast<void *>(o_human), previous ? ToOrig(previous) : nullptr); return 0; });
            break;
        }
        case 1:
            SqCall("Update", [&] { s->Update(dt); return 0; }, [&](void *) { OAi<void>(0x00525da0, dt); return 0; });
            break;
        case 2: {
            const int uid = I(0, 500);
            SqCall("OnUniformChanged", [&] { s->OnUniformChanged(uid); return 0; }, [&](void *) { OAi<void>(0x0052f6f0, uid); return 0; });
            break;
        }
        case 3:
            SqCall("OnEnemySeen", [&] { s->OnEnemySeen(other); return 0; }, [&](void *) { OAi<void>(0x0052e5b0, ToOrig(other)); return 0; });
            break;
        case 4:
            SqCall("OnEnemyLost", [&] { s->OnEnemyLost(other); return 0; }, [&](void *) { OAi<void>(0x0052e6f0, ToOrig(other)); return 0; });
            break;
        case 5:
            SqCall("OnBodySeen", [&] { s->OnBodySeen(other); return 0; }, [&](void *) { OAi<void>(0x0052e6d0, ToOrig(other)); return 0; });
            break;
        case 6:
            SqCall("OnLoudSound", [&] { s->OnLoudSound(other); return 0; }, [&](void *) { OAi<void>(0x0052e750, ToOrig(other)); return 0; });
            break;
        case 7: {
            const S_vector v(U(-30, 30), U(-3, 3), U(-30, 30));
            SqCall("OnShotAt", [&] { s->OnShotAt(other, v); return 0; }, [&](void *) { OAi<void>(0x0052e780, ToOrig(other), &v); return 0; });
            break;
        }
        case 8: {
            const bool completed = P(0.5f);
            SqCall("EndInteraction", [&] { s->EndInteraction(completed); return 0; },
                   [&](void *) { OAi<void>(0x005324a0, int(completed)); return 0; });
            break;
        }
        case 9:
            SqCall("Unknown104", [&] { return int(s->Unknown104()); }, [&](void *) { return int(OAi<uint32_t>(0x00532880) & 0xFF); });
            break;
        case 10:
            if (!h->m_vehicle)
                break;
            SqCall("ControlVehicle", [&] { s->ControlVehicle(); return 0; }, [&](void *) { OAi<void>(0x00531930); return 0; });
            break;
        case 11: {
            const int k = I(0, 5);
            static const char *const names[] = {"GetFlags", "HaveOrdersChanged", "GetOrderCount", "GetStatusIcon", "GetModeIcon", "GetCombatOrder"};
            static const uint32_t at[] = {0x00524e00, 0x0052f110, 0x0052f130, 0x00538590, 0x005385b0, 0x005385f0};
            SqCall(names[k],
                   [&] {
                       switch (k) {
                       case 0: return int(s->GetFlags());
                       case 1: return int(s->HaveOrdersChanged());
                       case 2: return s->GetOrderCount();
                       case 3: return s->GetStatusIcon();
                       case 4: return s->GetModeIcon();
                       default: return s->GetCombatOrder();
                       }
                   },
                   [&](void *) {
                       const uint32_t r = OAi<uint32_t>(at[k]);
                       return k == 1 ? int(r & 0xFF) : int(r);
                   });
            break;
        }
        case 12:
            SqCall("IsQuiet", [&] { return int(s->IsQuiet()); }, [&](void *) { return int(OAi<uint32_t>(0x005385d0) & 0xFF); });
            break;
        case 13: {
            // A new order (each side's, alike), added or inserted.
            // (A full queue now and then: ten orders, no more taken.)
            if (P(0.2f))
                while (SqField(DaGet(SqOrders())).size() < 10)
                    SqField(DaGet(SqOrders())).push_back(
                        SqAllocOrder(I(1, 4), I(0, 0x40), SqNear(25), AiDir(), MOVE_PACE_4, uint32_t(I(0, 4)), 0, nullptr, 0));
            // (Inserted after a selected order, 0 .. count - 1, as the command panel does.)
            const int count = int(SqField(DaGet(SqOrders())).size());
            const bool insert = count > 0 && P(0.5f);
            const int index = insert ? I(0, count - 1) : 0;
            C_Actor *actor = P(0.6f) ? nullptr : static_cast<C_Actor *>(PortActor(I(0, 11)));
            S_HumanOrder *order = SqAllocOrder(P(0.8f) ? I(1, 4) : 6, P(0.2f) ? SQUAD_ACTION_FOLLOW : I(0, 0x40), SqNear(25), AiDir(),
                                               MOVE_PACE_4, uint32_t(I(0, 4)), 0, actor, I(0, 5));
            uint8_t *o = SqOrigOrder(order);
            SqCall(insert ? "InsertOrder" : "AddOrder",
                   [&] {
                       if (insert)
                           s->InsertOrder(order, index);
                       else
                           s->AddOrder(order);
                       return 0;
                   },
                   [&](void *) {
                       if (insert)
                           OAi<void>(0x0052eb20, static_cast<void *>(o), index);
                       else
                           OAi<void>(0x0052ed00, static_cast<void *>(o));
                       return 0;
                   });
            break;
        }
        case 14: {
            const int count = int(SqField(DaGet(SqOrders())).size());
            if (count == 0)
                break;
            const int index = I(0, count - 1);
            S_HumanOrder *order = SqAllocOrder(P(0.5f) ? HUMAN_ORDER_WATCH : I(1, 4), I(0, 0x40), P(0.3f) ? S_vector4{0, 0, 0, 0} : SqNear(25),
                                               P(0.3f) ? S_vector4{0, 0, 0, 0} : AiDir(), P(0.5f) ? 0u : MOVE_PACE_4, uint32_t(I(0, 4)), 0,
                                               nullptr, 0);
            uint8_t *o = SqOrigOrder(order);
            SqCall("SetOrder", [&] { s->SetOrder(index, order); return 0; },
                   [&](void *) { OAi<void>(0x0052ee60, index, static_cast<void *>(o)); return 0; });
            typedef void(__stdcall * T_Release)(void *);
            ReleaseHumanOrder(order);
            OriginalAt<T_Release>(0x00583980)(o);
            break;
        }
        case 15: {
            const bool turn = P(0.5f);
            SqCall("CancelOrders", [&] { s->CancelOrders(turn); return 0; }, [&](void *) { OAi<void>(0x0052ef20, int(turn)); return 0; });
            break;
        }
        case 16: {
            const int count = int(SqField(DaGet(SqOrders())).size());
            if (count == 0)
                break;
            const int index = I(0, count - 1);
            const bool turn = P(0.5f);
            SqCall("RemoveOrder", [&] { s->RemoveOrder(index, turn); return 0; },
                   [&](void *) { OAi<void>(0x0052efa0, index, int(turn)); return 0; });
            break;
        }
        case 17: {
            const int count = int(SqField(DaGet(SqOrders())).size());
            if (count == 0)
                break;
            const int index = I(0, count - 1);
            SqCall("GetOrder", [&] { return SqMapped(s->GetOrder(index)) != 0; },
                   [&](void *) { return OAi<void *>(0x0052f150, index) != nullptr; });
            break;
        }
        case 18:
            // (Now and then quiet long enough, done with its action, hurt: the member heals itself.)
            if (P(0.5f)) {
                s->m_quietTime = I(4999, 9000);
                s->*DaGet(SqSelfHealTime()) = 0;
                SqField(DaGet(SqAction())).completed = true;
                h->m_stats[STAT_HEALTH] = I(1, h->m_initialStats[STAT_HEALTH] > 1 ? h->m_initialStats[STAT_HEALTH] : 1);
                h->m_carried = nullptr;
            }
            SqCall("SelfHeal", [&] { (s->*DaGet(SqSelfHeal()))(dt); return 0; }, [&](void *) { OAi<void>(0x00526400, dt); return 0; });
            break;
        case 19:
            // (Calm now and then: no event, not alert, no enemy seen; the idle member turns back.)
            if (P(0.4f)) {
                s->*DaGet(SqEventTime()) = 0;
                s->*DaGet(SqAlertTime()) = 0;
                h->m_seenEnemies.clear();
            }
            SqCall("UpdateIdle", [&] { (s->*DaGet(SqUpdateIdle()))(dt); return 0; }, [&](void *) { OAi<void>(0x00536120, dt); return 0; });
            break;
        case 20:
            SqCall("UpdateFollowPace", [&] { (s->*DaGet(SqUpdateFollowPace()))(); return 0; }, [&](void *) { OAi<void>(0x005376f0); return 0; });
            break;
        case 21: {
            const int icon = I(-2, 42);
            const bool mode = P(0.5f);
            SqCall(mode ? "SetModeIcon" : "SetStatusIcon",
                   [&] {
                       (s->*(mode ? DaGet(SqSetModeIcon()) : DaGet(SqSetStatusIcon())))(icon);
                       return 0;
                   },
                   [&](void *) { OAi<void>(mode ? 0x00538550 : 0x00538510, icon); return 0; });
            break;
        }
        case 22:
            SqCall("NotifyIfIdle", [&] { (s->*DaGet(SqNotifyIfIdle()))(); return 0; }, [&](void *) { OAi<void>(0x00538390); return 0; });
            break;
        case 23:
            SqCall("FollowSelectedMember", [&] { (s->*DaGet(SqFollowSelectedMember()))(); return 0; },
                   [&](void *) { OAi<void>(0x0052f0e0); return 0; });
            break;
        case 24: {
            const int sentence = I(0x60, 0x80);
            const bool alt = P(0.5f), force = P(0.5f);
            const int delay = I(0, 3000);
            SqCall("SayReply", [&] { s->SayReply(sentence, alt, delay, force); return 0; },
                   [&](void *) { OAi<void>(0x005383d0, sentence, int(alt), delay, int(force)); return 0; });
            break;
        }
        case 25:
            SqCall("UpdateModeIcon", [&] { s->UpdateModeIcon(); return 0; }, [&](void *) { OAi<void>(0x00538640); return 0; });
            break;
        case 26:
            SqCall("UpdateIconTimers", [&] { (s->*DaGet(SqUpdateIconTimers()))(dt); return 0; }, [&](void *) {
                // (The original's two, the status icon's and the mode icon's: Tick calls them one after the other.)
                OAi<void>(0x00537690, dt);
                OAi<void>(0x005376c0, dt);
                return 0;
            });
            break;
        case 27:
            SqCall("OnDeath", [&] { s->OnDeath(); return 0; }, [&](void *) { OAi<void>(0x00525130); return 0; });
            break;
        case 28: {
            // The saved games (an interaction has its target, as part 33's).
            static const int kinds[] = {0, 1, 2, 9, 11, KIND_HUMAN};
            if (P(0.6f)) {
                s->m_interaction = P(0.2f) ? HUMAN_COMMAND_HEAL_SELF : I(0, 0x26);
                s->m_interactionTarget = static_cast<C_Actor *>(PortActor(kinds[I(0, 5)]));
            } else {
                s->m_interaction = -1;
            }
            const std::string portFile = AiTempFile("_sqport.sav"), origFile = AiTempFile("_sqorig.sav");
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            auto save = [&](const std::string &file, bool orig) {
                C_chunk c;
                c.Open(file.c_str(), true);
                c.Begin(0x7777);
                if (orig)
                    OAi<void>(0x00538cb0, &c);
                else
                    s->SaveGame(c);
                c.Ascend();
                c.Begin(0x0BD1);
                c.Ascend();
                return 0;
            };
            SqCall("SaveGame", [&] { return save(portFile, false); }, [&](void *) { return save(origFile, true); });
            const bool same = AiFileBytes(portFile) == AiFileBytes(origFile);
            CHECK(same);
            static int shown;
            if (!same && shown++ < 3)
                std::printf("  SaveGame: the files differ\n");
            RandomSquad(false);
            SqReleaseOrders();
            g_daGoneId = -1000;
            auto load = [&](bool orig) {
                C_chunk c;
                c.Open(portFile.c_str(), false);
                uint16_t id;
                c.Descend(id);
                if (orig) {
                    OAi<void>(0x00539990, &c);
                    // (The loaded vectors' w: the original's a stack word, the port's its own; the port's copied (the
                    // port has loaded first). engagePos, boundDir, holdDir, the action's pos and dir.)
                    std::memcpy(g_osq + 0x14C, &(s->*DaGet(SqEngagePos())).w, 4);
                    std::memcpy(g_osq + 0x15C, &(s->*DaGet(SqBoundDir())).w, 4);
                    std::memcpy(g_osq + 0x16C, &(s->*DaGet(SqHoldDir())).w, 4);
                    const auto *pa = reinterpret_cast<const uint8_t *>(&(s->*DaGet(SqAction())));
                    std::memcpy(g_osq + 0x1E0 + 0x0C, pa + 0x0C, 4);
                    std::memcpy(g_osq + 0x1E0 + 0x1C, pa + 0x1C, 4);
                } else {
                    s->LoadGame(c);
                }
                int depth;
                std::memcpy(&depth, reinterpret_cast<const uint8_t *>(&c) + 0x410, 4);
                const int at = c.GetPosition();
                c.Ascend();
                return depth * 0x10000 + (at & 0xFFFF) + AiNextChunk(c) * 0x100000;
            };
            g_sqLoadedOrders = true;
            SqCall("LoadGame", [&] { return load(false); }, [&](void *) { return load(true); });
            g_sqLoadedOrders = false;
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            break;
        }
        case 29:
            SqCall("Detach", [&] { s->Detach(); return 0; }, [&](void *) { OAi<void>(0x00525110); return 0; });
            s->m_human = h;
            break;
        case 30: {
            static const int types[] = {0, 1, 2, 3, 4, 5, 6};
            const int type = types[I(0, 6)], zone = I(0, 6), health = I(-10, 200);
            C_Actor *attacker = P(0.2f) ? nullptr : P(0.6f) ? static_cast<C_Actor *>(other) : static_cast<C_Actor *>(PortActor(I(0, 11)));
            const S_vector pos = XYZ(SqNear(3)), dir = XYZ(AiDir());
            SqCall("OnHit", [&] { s->OnHit(type, zone, health, attacker, pos, dir); return 0; },
                   [&](void *) { OAi<void>(0x0052eaf0, type, zone, health, SqOrigOf(attacker), &pos, &dir); return 0; });
            break;
        }
        default:
            SqCall("Tick", [&] { s->Tick(dt); return 0; }, [&](void *) { OAi<void>(0x00526550, dt); return 0; });
            break;
        }
    }
}


// ---- part 44a's orders: an order's own functions (each side's order, made alike; compared by their fields after)
DA_ROB(SqWirePoints, uint16_t (C_WireCut::*)[2], &C_WireCut::m_points)
void TestSquadOrders()
{
    std::printf("squad orders: markers, actor points, paths, changes\n");
    C_Human *h = g_h.human;
    typedef void(__fastcall * T_Fast)(void *);
    typedef void(AC * T_Path)(void *, const S_vector4 *);
    typedef S_vector4 *(AC * T_Point)(void *, S_vector4 *, void *);
    typedef void(AC * T_Copy)(void *, void *);
    typedef void(AC * T_Plain)(void *, void *, const S_vector4 *, const S_vector4 *, uint32_t, uint32_t, uint32_t);
    typedef void(__stdcall * T_Release)(void *);
    // (The orders' actors: part 23's with places of their own; the Ju 88, without one, only asked for its point.)
    static const int actorKinds[] = {0, 3, 4, 5, 7, 8, 9, 11, 1, 2};
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomSquad(false);
        // The order's actor: none, the other human, or one of part 23's (doors, cars, guns, wire cuts, ...).
        C_Actor *actor = P(0.35f) ? nullptr : P(0.25f) ? static_cast<C_Actor *>(PortOther())
                                                       : static_cast<C_Actor *>(PortActor(actorKinds[I(0, 9)]));
        const int kind = I(1, 7);
        const S_vector4 dir = P(0.3f) ? S_vector4{0, 0, 0, 0} : AiDir();
        S_HumanOrder *order = SqAllocOrder(kind, I(0, 0x40), SqNear(20), dir, P(0.3f) ? 0u : MOVE_PACE_4, uint32_t(I(0, 4)),
                                           uint32_t(I(0, 4) << 8), actor, I(0, 5));
        uint8_t *o = SqOrigOrder(order);
        // (The answers: the door's two sides, the wire's two points, both sides alike.)
        g_ai.ladderPoints.clear();
        if (P(0.6f))
            g_ai.ladderPoints = {uint16_t(I(0, 7)), uint16_t(I(0, 7))};
        {
            const uint16_t points[2] = {uint16_t(I(0, 7)), uint16_t(I(0, 7))};
            auto *wire = reinterpret_cast<C_WireCut *>(PortActor(11));
            std::memcpy(wire->*DaGet(SqWirePoints()), points, 4);
            std::memcpy(static_cast<uint8_t *>(SqOrigOf(wire)) + 0x94, points, 4);
        }
        const int which = I(0, 7);
        switch (which) {
        case 0:
            SqCall("OrderUpdateMarker", [&] { order->UpdateMarker(); return 0; },
                   [&](void *) { OriginalAt<T_Fast>(0x0058caf0)(o); return 0; });
            break;
        case 1: {
            C_Actor *target = actor ? actor : static_cast<C_Actor *>(PortActor(P(0.2f) ? 6 : actorKinds[I(0, 9)]));
            // (The kinds without a place of their own (the Ju 88 here) take the path's end: a path first, the original
            // reads before an empty one, a fault the port fixes.)
            const bool pathFirst = target->GetType() == ACTOR_JUNKERS88 || P(0.3f);
            const S_vector4 from = SqNear(10);
            S_vector4 portPoint{}, origPoint{};
            SqCall("OrderGetActorPoint",
                   [&] {
                       if (pathFirst)
                           order->UpdatePath(from);
                       portPoint = order->GetActorPoint(target);
                       return 0;
                   },
                   [&](void *) {
                       if (pathFirst)
                           OriginalAt<T_Path>(0x0058cce0)(o, &from);
                       OriginalAt<T_Point>(0x0058c920)(o, &origPoint, SqOrigOf(target));
                       return 0;
                   });
            // (x, y, z: the w of several kinds is a stale local in the original.)
            CHECK(std::memcmp(&portPoint, &origPoint, 12) == 0);
            break;
        }
        case 2:
        case 3: {
            // From the human (now and then a second time, moved a little or not: the path kept or made again).
            const S_vector4 from = SqNear(P(0.3f) ? 0.1f : 10);
            const bool again = which == 3;
            const S_vector4 from2 = P(0.5f) ? from : S_vector4{from.x + U(-0.3f, 0.3f), from.y, from.z + U(-0.3f, 0.3f), from.w};
            SqCall("OrderUpdatePath",
                   [&] {
                       order->UpdatePath(from);
                       if (again)
                           order->UpdatePath(from2);
                       return 0;
                   },
                   [&](void *) {
                       OriginalAt<T_Path>(0x0058cce0)(o, &from);
                       if (again)
                           OriginalAt<T_Path>(0x0058cce0)(o, &from2);
                       return 0;
                   });
            break;
        }
        case 4: {
            // The changes of another order (zero fields keep the order's).
            S_HumanOrder *other = SqAllocOrder(I(1, 7), 0, P(0.3f) ? S_vector4{0, 0, 0, 0} : SqNear(20),
                                               P(0.3f) ? S_vector4{0, 0, 0, 0} : AiDir(), P(0.4f) ? 0u : MOVE_PACE_3, 0, 0, nullptr, 0);
            uint8_t *oo = SqOrigOrder(other);
            SqCall("OrderCopyFrom", [&] { order->CopyFrom(*other); return 0; },
                   [&](void *) { OriginalAt<T_Copy>(0x0058d720)(o, oo); return 0; });
            ReleaseHumanOrder(other);
            OriginalAt<T_Release>(0x00583980)(oo);
            break;
        }
        case 7: {
            // Set up again (as the pool's AllocHumanOrder does): its marker taken off first.
            order->done = order->started = order->selected = true;
            o[0x48] = o[0x49] = o[0x4A] = 1;
            C_Human *human = P(0.5f) ? h : PortOther();
            C_Actor *newActor = P(0.5f) ? nullptr : static_cast<C_Actor *>(PortActor(actorKinds[I(0, 9)]));
            const int newKind = I(1, 7), arg = I(0, 0x40), unknown70 = I(0, 5);
            const S_vector4 pos = SqNear(15), data = AiDir();
            const uint32_t pace = P(0.3f) ? 0u : MOVE_PACE_3, stance = uint32_t(I(0, 4)), mode = uint32_t(I(0, 4) << 8);
            typedef void(AC * T_Init)(void *, void *, int, int, const S_vector4 *, const S_vector4 *, uint32_t, uint32_t, uint32_t, void *, int);
            SqCall("OrderInit",
                   [&] {
                       order->Init(human, newKind, arg, pos, data, pace, stance, mode, newActor, unknown70);
                       return 0;
                   },
                   [&](void *) {
                       OriginalAt<T_Init>(0x0058c5a0)(o, SqOrigOf(human), newKind, arg, &pos, &data, pace, stance, mode, SqOrigOf(newActor),
                                                      unknown70);
                       return 0;
                   });
            break;
        }
        case 5: {
            order->done = order->started = order->selected = true;
            o[0x48] = o[0x49] = o[0x4A] = 1;
            const S_vector4 pos = SqNear(15), data = AiDir();
            const uint32_t pace = P(0.3f) ? 0u : MOVE_PACE_3, stance = uint32_t(I(0, 4)), mode = uint32_t(I(0, 4) << 8);
            C_Human *human = P(0.5f) ? h : PortOther();
            SqCall("OrderInitPlain", [&] { order->InitPlain(human, pos, data, pace, stance, mode); return 0; },
                   [&](void *) { OriginalAt<T_Plain>(0x0058c860)(o, SqOrigOf(human), &pos, &data, pace, stance, mode); return 0; });
            break;
        }
        default: {
            I3D_frame *portMarker = order->marker, *origMarker;
            std::memcpy(&origMarker, o + 0x84, 4);
            SqCall("OrderRemoveMarker", [&] { order->RemoveMarker(); return 0; },
                   [&](void *) { OriginalAt<T_Fast>(0x0058d6c0)(o); return 0; });
            // (The markers' data cleared on both sides.)
            CHECK((portMarker ? portMarker->GetData() : nullptr) == nullptr && (origMarker ? origMarker->GetData() : nullptr) == nullptr);
            break;
        }
        }
        const std::string why = SqOrderDiffers(order, o);
        CHECK(why.empty());
        static int shown;
        if (!why.empty() && shown++ < 5)
            std::printf("  order (%d): %s\n", which, why.c_str());
        ReleaseHumanOrder(order);
        OriginalAt<T_Release>(0x00583980)(o);
    }
}

// ---- part 44b: the squad's actions (game/squad_actions.cpp): the commander's commands, the orders' start, the action's
// execution, the follow and hold modes, the combat orders, looting and equipping
DA_ROB(SqActOn, void (S_Sq::*)(int, C_Actor *, bool, float, bool), &S_Sq::ActOn)
DA_ROB(SqProcessOrders, bool (S_Sq::*)(int), &S_Sq::ProcessOrders)
DA_ROB(SqStartOrder, void (S_Sq::*)(int), &S_Sq::StartOrder)
DA_ROB(SqSetOrderTask, void (S_Sq::*)(S_HumanOrder *), &S_Sq::SetOrderTask)
DA_ROB(SqSetIdleTask, void (S_Sq::*)(S_HumanOrder *), &S_Sq::SetIdleTask)
DA_ROB(SqQueueWatchOrder, void (S_Sq::*)(const S_vector4 &, const S_vector4 &, S_HumanOrder *), &S_Sq::QueueWatchOrder)
DA_ROB(SqExecuteAction, bool (S_Sq::*)(int), &S_Sq::ExecuteAction)
DA_ROB(SqIsActionNeeded, bool (S_Sq::*)(), &S_Sq::IsActionNeeded)
DA_ROB(SqIsAtTarget, bool (S_Sq::*)(C_Actor *), &S_Sq::IsAtTarget)
DA_ROB(SqShowActionIcon, void (S_Sq::*)(int), &S_Sq::ShowActionIcon)
DA_ROB(SqSearchBodyItems, bool (S_Sq::*)(C_Human *), &S_Sq::SearchBodyItems)
DA_ROB(SqTakeItems, bool (S_Sq::*)(C_Actor *, int), &S_Sq::TakeItems)
DA_ROB(SqLootAmmo, bool (S_Sq::*)(C_Human *), &S_Sq::LootAmmo)
DA_ROB(SqLootWeapon, bool (S_Sq::*)(C_Human *), &S_Sq::LootWeapon)
DA_ROB(SqLootItemsOfType, bool (S_Sq::*)(C_Human *, int), &S_Sq::LootItemsOfType)
DA_ROB(SqEquip, void (S_Sq::*)(C_Item *, int *), &S_Sq::Equip)
DA_ROB(SqFindWeaponFor, C_Item *(S_Sq::*)(C_Actor *, int *), &S_Sq::FindWeaponFor)
DA_ROB(SqAdjustStanceForThreat, void (S_Sq::*)(), &S_Sq::AdjustStanceForThreat)

// The actors the commands take (as the game's: a door for the door commands, a mine to defuse, ...); part 23's by kind.
C_Actor *SqActor(int kind) { return static_cast<C_Actor *>(PortActor(kind)); }
C_Actor *SqVehicle()
{
    static const int kinds[] = {3, 4, 5};
    return P(0.4f) ? reinterpret_cast<C_Actor *>(g_asCarPort) : SqActor(kinds[I(0, 2)]);
}

void TestSquadB()
{
    std::printf("squad actions: commands, orders started, actions executed, follow and hold, combat orders, looting\n");
    C_Human *h = g_h.human;
    const char *only = std::getenv("HUMAN_SQUAD_CASE");
    const int rounds = only ? 2000 : 30000 / QUICK;
    for (int i = 0; i < rounds; ++i) {
        int which = I(0, 65);   // (62 .. 65: the tick)
        if (only)
            which = std::atoi(only);
        RandomSquad(false);
        S_Sq *s = Sq();
        const int dt = SqDt();
        C_Human *other = P(0.5f) ? PortOther() : PortThird();
        C_Human *anyHuman = P(0.3f) ? h : other;
        // A command on an actor: the port's member and the original's at its address, the actor mapped.
        auto command = [&](const char *what, void (S_Sq::*port)(C_Actor *), uint32_t at, C_Actor *actor) {
            SqCall(what, [&] { (s->*port)(actor); return 0; }, [&](void *) { OAi<void>(at, SqOrigOf(actor)); return 0; });
        };
        switch (which) {
        case 0: {
            // (Not MODE0_ALT with empty hands: its engaging can leave the hands empty, and the original then reads the
            // missing item's type, a crash the port fixes.)
            int order = P(0.1f) ? I(0, 0x14) : I(SQUAD_ORDER_MODE0, SQUAD_ORDER_MODE1_ALT);
            if (order == SQUAD_ORDER_MODE0_ALT)
                order = SQUAD_ORDER_MODE0;
            SqCall("SetCombatOrder", [&] { s->SetCombatOrder(order); return 0; }, [&](void *) { OAi<void>(0x005380c0, order); return 0; });
            break;
        }
        case 1: {
            if (P(0.3f)) {
                S_SquadAction &a = SqField(DaGet(SqAction()));
                a.type = HUMAN_COMMAND_ENTER_VEHICLE;
                a.target = SqActor(P(0.5f) ? 3 : 5);
                if (P(0.6f))
                    SqReleaseOrders();
            }
            const bool follow = P(0.5f), quiet = P(0.5f);
            SqCall("SetFollow", [&] { s->SetFollow(follow, quiet); return 0; },
                   [&](void *) { OAi<void>(0x005377f0, int(follow), int(quiet)); return 0; });
            break;
        }
        case 2: {
            const bool quiet = P(0.5f), notify = P(0.5f);
            SqCall("HoldPosition", [&] { s->HoldPosition(quiet, notify); return 0; },
                   [&](void *) { OAi<void>(0x00537e50, int(quiet), int(notify)); return 0; });
            break;
        }
        case 3: {
            const bool on = P(0.6f);
            const S_vector4 pos = SqNear(20);
            SqCall("Engage", [&] { s->Engage(on, pos); return 0; }, [&](void *) { OAi<void>(0x005382d0, int(on), &pos); return 0; });
            break;
        }
        case 4: {
            const bool on = P(0.5f);
            SqCall("HoldOrders", [&] { s->HoldOrders(on); return 0; }, [&](void *) { OAi<void>(0x00538490, int(on)); return 0; });
            break;
        }
        case 5: {
            const bool fromOrders = P(0.5f);
            SqCall("FollowSelected", [&] { s->FollowSelected(fromOrders); return 0; },
                   [&](void *) { OAi<void>(0x005357e0, int(fromOrders)); return 0; });
            break;
        }
        case 6: {
            static const int types[] = {HUMAN_COMMAND_ENTER_VEHICLE, 8, 9, 10, 11, 12, 13, HUMAN_COMMAND_USE_LADDER, SQUAD_ACTION_FOLLOW,
                                        SQUAD_ACTION_BOUND, SQUAD_ACTION_MOVE_NEAR, SQUAD_ACTION_ORDER_MOVE, SQUAD_ACTION_ATTACK, 0, 5};
            const int type = types[I(0, 14)];
            SqCall("FinishAction", [&] { s->FinishAction(type); return 0; }, [&](void *) { OAi<void>(0x00535c00, type); return 0; });
            break;
        }
        case 7:
            if (P(0.4f)) {
                SqField(DaGet(SqAction())).type = SQUAD_ACTION_THROW;
                s->*DaGet(SqFlags()) |= 0x008;   // (the throw's flag)
            }
            SqCall("DropAction", [&] { s->DropAction(); return 0; }, [&](void *) { OAi<void>(0x00535730); return 0; });
            break;
        case 8:
            SqCall("EndOrderKind6", [&] { s->EndOrderKind6(); return 0; }, [&](void *) { OAi<void>(0x00538430); return 0; });
            break;
        case 9:
            SqCall("EndOrderKind7", [&] { s->EndOrderKind7(); return 0; }, [&](void *) { OAi<void>(0x00538460); return 0; });
            break;
        case 10:
            command("Act21", &S_Sq::Act21, 0x005343e0, P(0.5f) ? SqActor(I(0, 11)) : static_cast<C_Actor *>(anyHuman));
            break;
        case 11:
            command("DeliverDocuments", &S_Sq::DeliverDocuments, 0x00534400, SqActor(10));
            break;
        case 12: {
            C_Human *leader = other;
            SqCall("Follow", [&] { s->Follow(leader); return 0; }, [&](void *) { OAi<void>(0x00534460, SqOrigOf(leader)); return 0; });
            break;
        }
        case 13: {
            C_Actor *vehicle = SqVehicle();
            const int seat = I(-1, 4);
            const S_vector4 pos = SqNear(5);
            SqCall("EnterVehicle", [&] { s->EnterVehicle(vehicle, seat, pos); return 0; },
                   [&](void *) { OAi<void>(0x00534510, SqOrigOf(vehicle), seat, &pos); return 0; });
            break;
        }
        case 14: {
            C_Actor *actor = P(0.5f) ? SqActor(I(0, 11)) : static_cast<C_Actor *>(other);
            const int item = I(-1, 30);
            SqCall("PickUp", [&] { s->PickUp(actor, item); return 0; }, [&](void *) { OAi<void>(0x005346c0, SqOrigOf(actor), item); return 0; });
            break;
        }
        case 15:
            command("LockDoor", &S_Sq::LockDoor, 0x00534840, SqActor(0));
            break;
        case 16:
            command("UnlockDoor", &S_Sq::UnlockDoor, 0x00534860, SqActor(0));
            break;
        case 17:
            command("OpenDoor", &S_Sq::OpenDoor, 0x00534880, SqActor(0));
            break;
        case 18:
            command("CloseDoor", &S_Sq::CloseDoor, 0x005348f0, SqActor(0));
            break;
        case 19: {
            static const int kinds[] = {7, 8, 9, 10, 0, 5, 4, 3};
            command("UseActor", &S_Sq::UseActor, 0x00534910, SqActor(kinds[I(0, 7)]));
            break;
        }
        case 20:
            command("DefuseMine", &S_Sq::DefuseMine, 0x00534a00, SqActor(1));
            break;
        case 21:
            command("DisarmExplosive", &S_Sq::DisarmExplosive, 0x00534a70, SqActor(2));
            break;
        case 22:
            command("SearchBody", &S_Sq::SearchBody, 0x00534af0, other);
            break;
        case 23:
            command("PlantExplosive", &S_Sq::PlantExplosive, 0x00534b30, SqActor(2));
            break;
        case 24:
            command("SwapUniform", &S_Sq::SwapUniform, 0x00534bf0, other);
            break;
        case 25:
            command("CutWire", &S_Sq::CutWire, 0x00534c40, SqActor(11));
            break;
        case 26:
            command("PickLock", &S_Sq::PickLock, 0x00534f80, SqActor(0));
            break;
        case 27:
            command("OrderLeaveVehicle", &S_Sq::OrderLeaveVehicle, 0x00534ff0, SqVehicle());
            break;
        case 28:
            command("Attack", &S_Sq::Attack, 0x00535050, h);
            break;
        case 29:
            command("AttackSilently", &S_Sq::AttackSilently, 0x00535110, h);
            break;
        case 30:
            command("AttackVehicle", &S_Sq::AttackVehicle, 0x00535250, SqActor(P(0.5f) ? 3 : 5));
            break;
        case 31:
            command("TieUp", &S_Sq::TieUp, 0x005353a0, other);
            break;
        case 32: {
            const S_vector4 pos = SqNear(20);
            const bool quiet = P(0.5f);
            SqCall("MoveNear", [&] { s->MoveNear(pos, quiet); return 0; }, [&](void *) { OAi<void>(0x00535440, &pos, int(quiet)); return 0; });
            break;
        }
        case 33: {
            const S_vector4 pos = SqNear(20);
            const int delay = P(0.5f) ? 0 : I(1, 3000);
            const bool notify = P(0.5f);
            SqCall("OrderMove", [&] { s->OrderMove(pos, delay, notify); return 0; },
                   [&](void *) { OAi<void>(0x005354f0, &pos, delay, int(notify)); return 0; });
            break;
        }
        case 34:
            command("ThrowGrenadeAt", &S_Sq::ThrowGrenadeAt, 0x00535580, P(0.5f) ? static_cast<C_Actor *>(h) : SqActor(I(0, 11)));
            break;
        case 35:
            command("Heal", &S_Sq::Heal, 0x00535630, anyHuman);
            break;
        case 36: {
            const S_vector4 pos = SqNear(P(0.3f) ? 4 : 30);
            SqCall("ThrowAt", [&] { s->ThrowAt(pos); return 0; }, [&](void *) { OAi<void>(0x00535910, &pos); return 0; });
            break;
        }
        case 37: {
            const S_vector4 pos = SqNear(P(0.3f) ? 4 : 30);
            SqCall("FireLauncherAt", [&] { s->FireLauncherAt(pos); return 0; }, [&](void *) { OAi<void>(0x00535a60, &pos); return 0; });
            break;
        }
        case 38: {
            const S_vector4 pos = P(0.2f) ? ToVector4(h->GetFrame()->GetWorldPos(), 1.0f) : SqNear(20), dir = AiDir();
            const bool quiet = P(0.5f);
            const uint32_t stance = uint32_t(I(0, 4));
            SqCall("MoveTo", [&] { s->MoveTo(pos, dir, quiet, stance); return 0; },
                   [&](void *) { OAi<void>(0x00537a40, &pos, &dir, int(quiet), stance); return 0; });
            break;
        }
        case 39: {
            const S_vector4 dir = AiDir();
            const bool quiet = P(0.5f);
            SqCall("Bound", [&] { s->Bound(dir, quiet); return 0; }, [&](void *) { OAi<void>(0x00537c60, &dir, int(quiet)); return 0; });
            break;
        }
        case 40: {
            static const int types[] = {SQUAD_ACTION_FOLLOW, SQUAD_ACTION_ATTACK, SQUAD_ACTION_ATTACK_VEHICLE, HUMAN_COMMAND_HEAL,
                                        HUMAN_COMMAND_LOCK_DOOR, HUMAN_COMMAND_CUT_WIRE, SQUAD_ACTION_MOVE_NEAR, 3};
            const int type = types[I(0, 7)];
            C_Actor *actor = type == SQUAD_ACTION_ATTACK ? static_cast<C_Actor *>(h)
                             : type == SQUAD_ACTION_ATTACK_VEHICLE ? SqActor(3)
                             : type == HUMAN_COMMAND_LOCK_DOOR ? SqActor(0)
                             : type == HUMAN_COMMAND_CUT_WIRE ? SqActor(11)
                                                                : static_cast<C_Actor *>(other);
            const bool keepGoing = P(0.5f), notify = P(0.5f);
            const float radius = U(0, 3);
            SqCall("ActOn", [&] { (s->*DaGet(SqActOn()))(type, actor, keepGoing, radius, notify); return 0; },
                   [&](void *) { OAi<void>(0x00534620, type, SqOrigOf(actor), int(keepGoing), radius, int(notify)); return 0; });
            break;
        }
        case 41:
            // (The first order done now and then; not a follow order: starting one empties the queue, and a new one
            // is never done in the game, the original would then remove from the empty queue.)
            if (!SqField(DaGet(SqOrders())).empty() && SqField(DaGet(SqOrders()))[0]->kind != HUMAN_ORDER_FOLLOW && P(0.4f)) {
                S_HumanOrder *first = SqField(DaGet(SqOrders()))[0];
                first->done = true;
                SqOrigOrder(first)[0x48] = 1;
            }
            SqCall("ProcessOrders", [&] { return int((s->*DaGet(SqProcessOrders()))(dt)); },
                   [&](void *) { return int(OAi<uint32_t>(0x005282e0, dt) & 0xFF); });
            break;
        case 42:
            // (With an order: ProcessOrders calls it so; started now and then.)
            if (SqField(DaGet(SqOrders())).empty())
                break;
            if (P(0.5f)) {
                S_HumanOrder *first = SqField(DaGet(SqOrders()))[0];
                first->started = true;
                SqOrigOrder(first)[0x49] = 1;
            }
            SqCall("StartOrder", [&] { (s->*DaGet(SqStartOrder()))(dt); return 0; }, [&](void *) { OAi<void>(0x00529f60, dt); return 0; });
            break;
        case 43:
        case 44: {
            // The task of an order (the first in the queue, or a new one) / the idle task (with an order or none).
            std::vector<S_HumanOrder *> &orders = SqField(DaGet(SqOrders()));
            S_HumanOrder *order = !orders.empty() && P(0.7f) ? orders[0] : nullptr;
            const bool idle = which == 44;
            if (!order && !idle)
                break;
            uint8_t *o = order ? SqOrigOrder(order) : nullptr;
            SqCall(idle ? "SetIdleTask" : "SetOrderTask",
                   [&] {
                       if (idle)
                           (s->*DaGet(SqSetIdleTask()))(order);
                       else
                           (s->*DaGet(SqSetOrderTask()))(order);
                       return 0;
                   },
                   [&](void *) {
                       OAi<void>(idle ? 0x0052a3b0 : 0x0052a1e0, static_cast<void *>(o));
                       return 0;
                   });
            break;
        }
        case 45: {
            std::vector<S_HumanOrder *> &orders = SqField(DaGet(SqOrders()));
            // (The model is the queue's first order, as the callers pass it: the original reads that slot.)
            if (orders.empty())
                break;
            S_HumanOrder *model = orders[0];
            uint8_t *om = model ? SqOrigOrder(model) : nullptr;
            const S_vector4 pos = SqNear(15), dir = AiDir();
            SqCall("QueueWatchOrder", [&] { (s->*DaGet(SqQueueWatchOrder()))(pos, dir, model); return 0; },
                   [&](void *) { OAi<void>(0x00531f60, &pos, &dir, static_cast<void *>(om)); return 0; });
            break;
        }
        case 46:
        case 47:
            SqCall("ExecuteAction", [&] { return int((s->*DaGet(SqExecuteAction()))(dt)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00528950, dt) & 0xFF); });
            break;
        case 48: {
            // (The action's target as the type wants it, as the game's: IsActionNeeded casts it.)
            S_SquadAction &a = SqField(DaGet(SqAction()));
            static const struct {
                int type, kind;
            } needs[] = {{HUMAN_COMMAND_LOCK_DOOR, 0},   {HUMAN_COMMAND_UNLOCK_DOOR, 0}, {HUMAN_COMMAND_PLANT_EXPLOSIVE, 2},
                         {HUMAN_COMMAND_HEAL, -1},       {HUMAN_COMMAND_CUT_WIRE, 11},   {HUMAN_COMMAND_TIE_UP, -1},
                         {SQUAD_ACTION_ATTACK_VEHICLE, 3}, {SQUAD_ACTION_FOLLOW, -1},    {0x26, 5}};
            const auto &n = needs[I(0, 8)];
            a.type = n.type;
            a.target = n.kind < 0 ? static_cast<C_Actor *>(other) : SqActor(n.kind);
            SqCall("IsActionNeeded", [&] { return int((s->*DaGet(SqIsActionNeeded()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x005387c0) & 0xFF); });
            break;
        }
        case 49: {
            C_Actor *actor = P(0.3f) ? static_cast<C_Actor *>(other) : P(0.5f) ? SqVehicle() : SqActor(I(0, 11));
            SqCall("IsAtTarget", [&] { return int((s->*DaGet(SqIsAtTarget()))(actor)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00535e70, SqOrigOf(actor)) & 0xFF); });
            break;
        }
        case 50: {
            const int type = I(-2, 0x40);
            SqCall("ShowActionIcon", [&] { (s->*DaGet(SqShowActionIcon()))(type); return 0; },
                   [&](void *) { OAi<void>(0x005374a0, type); return 0; });
            break;
        }
        case 51:
            SqCall("SearchBodyItems", [&] { return int((s->*DaGet(SqSearchBodyItems()))(other)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00537300, SqOrigOf(other)) & 0xFF); });
            break;
        case 52: {
            C_Actor *from = P(0.6f) ? static_cast<C_Actor *>(other) : SqActor(I(0, 11));
            const int behaviour = I(0, 12);
            SqCall("TakeItems", [&] { return int((s->*DaGet(SqTakeItems()))(from, behaviour)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00537350, SqOrigOf(from), behaviour) & 0xFF); });
            break;
        }
        case 53:
            SqCall("LootAmmo", [&] { return int((s->*DaGet(SqLootAmmo()))(other)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00536dc0, SqOrigOf(other)) & 0xFF); });
            break;
        case 54:
            SqCall("LootWeapon", [&] { return int((s->*DaGet(SqLootWeapon()))(other)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00536fb0, SqOrigOf(other)) & 0xFF); });
            break;
        case 55: {
            const int typeId = I(0, 0x40);
            SqCall("LootItemsOfType", [&] { return int((s->*DaGet(SqLootItemsOfType()))(other, typeId)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00536d30, SqOrigOf(other), typeId) & 0xFF); });
            break;
        }
        case 56: {
            C_Item *item = reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
            int portSlot = I(0, 6), origSlot = portSlot;
            SqCall("Equip", [&] { (s->*DaGet(SqEquip()))(item, &portSlot); return portSlot; },
                   [&](void *) {
                       OAi<void>(0x00538770, OrigWeapon(item), &origSlot);
                       return origSlot;
                   });
            break;
        }
        case 57: {
            C_Actor *target = P(0.3f) ? nullptr : P(0.5f) ? static_cast<C_Actor *>(h) : SqActor(I(0, 11));
            int portContainer = -1, origContainer = -1;
            SqCall("FindWeaponFor",
                   [&] {
                       C_Item *item = (s->*DaGet(SqFindWeaponFor()))(target, &portContainer);
                       return int(SqMapped(item)) ^ portContainer;
                   },
                   [&](void *) {
                       void *item = OAi<void *>(0x00538980, SqOrigOf(target), &origContainer);
                       return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item))) ^ origContainer;
                   });
            break;
        }
        case 58:
            SqCall("AdjustStanceForThreat", [&] { (s->*DaGet(SqAdjustStanceForThreat()))(); return 0; },
                   [&](void *) { OAi<void>(0x00537b60); return 0; });
            break;
        case 61: {
            S_SquadAction &a = SqField(DaGet(SqAction()));
            a.finished = false;
            a.completed = false;
            a.delay = 0;
            s->*DaGet(SqOrdersHeld()) = false;
            if (P(0.5f)) {
                // The walk's start (in the seat car's driver's seat now and then).
                a.started = false;
                a.arrived = false;
                if (P(0.5f)) {
                    auto *car = reinterpret_cast<C_Actor *>(g_asCarPort);
                    car->m_type = ACTOR_CAR;
                    const int type = ACTOR_CAR;
                    std::memcpy(g_asCarOrig + 0x18, &type, 4);
                    h->m_vehicle = car;
                    h->m_seat = 0;
                    void *ov = g_asCarOrig;
                    std::memcpy(o_human + 0x160, &ov, 4);
                    std::memcpy(o_human + 0x164, &h->m_seat, 4);
                }
            } else {
                // Walking to a body to pick up, within 4 m.
                a.started = true;
                a.arrived = false;
                a.type = HUMAN_COMMAND_PICK_UP_BODY;
                a.target = other;
                const S_vector &at = h->GetFrame()->GetWorldPos();
                a.pos = {at.x + U(-2.5f, 2.5f), at.y, at.z + U(-2.5f, 2.5f), 1.0f};
            }
            SqCall("ExecuteAction", [&] { return int((s->*DaGet(SqExecuteAction()))(dt)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00528950, dt) & 0xFF); });
            break;
        }
        case 59:
        case 60: {
            // The action at its end: arrived, turning (59: its command; the throw (0x008) and launcher (0x200)
            // flags mostly off) or turned (60: a bound's next hop, the stance).
            S_SquadAction &a = SqField(DaGet(SqAction()));
            a.started = true;
            a.arrived = true;
            a.finished = false;
            a.completed = false;
            a.delay = 0;
            s->*DaGet(SqOrdersHeld()) = false;
            if (which == 59) {
                a.turned = false;
                a.skipTurn = P(0.8f);
                a.noCommand = P(0.2f);
                if (P(0.8f))
                    s->*DaGet(SqFlags()) &= ~uint32_t(0x008 | 0x200);
                if (P(0.25f)) {
                    a.type = HUMAN_COMMAND_ENTER_VEHICLE;
                    a.target = SqActor(3);
                } else if (P(0.2f)) {
                    a.type = SQUAD_ACTION_SWAP_UNIFORM;
                    a.target = other;
                    g_wornContainers[ITEM_CONTAINER_HEAD].item = P(0.6f) ? g_wornItems[ITEM_CONTAINER_HEAD] : nullptr;
                }
            } else {
                a.turned = true;
                if (P(0.6f))
                    a.type = SQUAD_ACTION_BOUND;
                s->*DaGet(SqBoundHops()) = I(0, 3);
            }
            SqCall("ExecuteAction", [&] { return int((s->*DaGet(SqExecuteAction()))(dt)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00528950, dt) & 0xFF); });
            break;
        }
        default:
            // The tick (the action's execution, the orders' start through it).
            SqCall("Tick", [&] { s->Tick(dt); return 0; }, [&](void *) { OAi<void>(0x00526550, dt); return 0; });
            break;
        }
    }
}

// ---- part 44c: the squad's combat (game/squad_combat.cpp): events and stimuli, hits, targets, bursts, the line of fire,
// weapons chosen, the vehicle gunner, the alert preset, cover
DA_ROB(SqFindStimuli, bool (S_Sq::*)(int, std::vector<int> &, bool), &S_Sq::FindStimuli)
DA_ROB(SqReactToStimuli, bool (S_Sq::*)(), &S_Sq::ReactToStimuli)
DA_ROB(SqTakeHit, void (S_Sq::*)(int, int, int, C_Actor *, const S_vector &, const S_vector &, float), &S_Sq::TakeHit)
DA_ROB(SqUpdateCombat, void (S_Sq::*)(int, bool), &S_Sq::UpdateCombat)
DA_ROB(SqFindAirTarget, C_Actor *(S_Sq::*)(I3D_frame **), &S_Sq::FindAirTarget)
DA_ROB(SqSetBurstTimes, void (S_Sq::*)(C_Actor *), &S_Sq::SetBurstTimes)
DA_ROB(SqGetBurstDefaults, void (S_Sq::*)(int *, int *, int *, int *), &S_Sq::GetBurstDefaults)
DA_ROB(SqCanFireAt, bool (S_Sq::*)(), &S_Sq::CanFireAt)
DA_ROB(SqHasClearLine, bool (S_Sq::*)(), &S_Sq::HasClearLine)
DA_ROB(SqFindNearMeleeTracker, C_Actor *(S_Sq::*)(float), &S_Sq::FindNearMeleeTracker)
DA_ROB(SqFindMeleeTrackerTarget, C_Actor *(S_Sq::*)(float), &S_Sq::FindMeleeTrackerTarget)
DA_ROB(SqAimAt, void (S_Sq::*)(C_Actor *), &S_Sq::AimAt)
DA_ROB(SqPickCombatTarget, C_Human *(S_Sq::*)(const S_vector *), &S_Sq::PickCombatTarget)
DA_ROB(SqFindEnemyTargeting, C_Human *(S_Sq::*)(bool), &S_Sq::FindEnemyTargeting)
DA_ROB(SqWeaponCheckDue, bool (S_Sq::*)(), &S_Sq::WeaponCheckDue)
DA_ROB(SqBuildWeaponPrefs, void (S_Sq::*)(C_Actor *, std::vector<uint32_t> &), &S_Sq::BuildWeaponPrefs)
DA_ROB(SqFindWeapon, C_Item *(S_Sq::*)(int, bool, bool, int *), &S_Sq::FindWeapon)
DA_ROB(SqPickWeapon, C_Item *(S_Sq::*)(const std::vector<uint32_t> &, int *), &S_Sq::PickWeapon)
DA_ROB(SqChooseWeapon, void (S_Sq::*)(C_Actor *), &S_Sq::ChooseWeapon)
DA_ROB(SqIsWeaponSuitable, bool (S_Sq::*)(C_Actor *, C_Item *), &S_Sq::IsWeaponSuitable)
DA_ROB(SqUpdateWeaponChoice, bool (S_Sq::*)(), &S_Sq::UpdateWeaponChoice)
DA_ROB(SqReloadFromBelt, bool (S_Sq::*)(), &S_Sq::ReloadFromBelt)
DA_ROB(SqMelee, void (S_Sq::*)(C_Actor *), &S_Sq::Melee)
DA_ROB(SqVehicleCombat, bool (S_Sq::*)(int), &S_Sq::VehicleCombat)
DA_ROB(SqGetVehicleAimDir, void (S_Sq::*)(S_vector4 *, C_Actor *), &S_Sq::GetVehicleAimDir)
DA_ROB(SqAimSeatWeapon, void (S_Sq::*)(C_Actor *), &S_Sq::AimSeatWeapon)
DA_ROB(SqUpdateGunner, void (S_Sq::*)(int), &S_Sq::UpdateGunner)
DA_ROB(SqUpdateAlertPreset, void (S_Sq::*)(), &S_Sq::UpdateAlertPreset)
DA_ROB(SqChooseCoverFacing, void (S_Sq::*)(S_vector4 *, uint32_t *), &S_Sq::ChooseCoverFacing)
DA_ROB(SqCoverStanceFn, void (S_Sq::*)(bool), &S_Sq::CoverStance)
// (Part 44d's.)
DA_ROB(SqUpdatePace, void (S_Sq::*)(int, bool), &S_Sq::UpdatePace)
DA_ROB(SqStepAside, void (S_Sq::*)(const S_vector4 &, const S_vector4 &, bool, bool), &S_Sq::StepAside)
DA_ROB(SqStepAsideFrom, void (S_Sq::*)(C_Actor *), &S_Sq::StepAsideFrom)
DA_ROB(SqMakeRoomForLeader, void (S_Sq::*)(), &S_Sq::MakeRoomForLeader)
DA_ROB(SqFindAttackPoint, bool (S_Sq::*)(S_vector4 *, float *, C_Actor *), &S_Sq::FindAttackPoint)
DA_ROB(SqAtSpecialPoint, bool (S_Sq::*)(), &S_Sq::AtSpecialPoint)

// The seat car as a vehicle the member sits in (car, submarine, static weapon or the Junkers), both sides alike.
void SqSeatTheMember(int type)
{
    C_Human *h = g_h.human;
    auto *car = reinterpret_cast<C_Actor *>(g_asCarPort);
    car->m_type = static_cast<E_ActorType>(type);
    std::memcpy(g_asCarOrig + 0x18, &type, 4);
    h->m_vehicle = car;
    h->m_seat = I(0, 3);
    void *ov = g_asCarOrig;
    std::memcpy(o_human + 0x160, &ov, 4);
    std::memcpy(o_human + 0x164, &h->m_seat, 4);
}

void TestSquadC()
{
    std::printf("squad combat: events, stimuli, hits, targets, bursts, the line of fire, weapons, the gunner, cover\n");
    C_Human *h = g_h.human;
    const char *only = std::getenv("HUMAN_SQUAD_CASE");
    const int rounds = only ? 2000 : 30000 / QUICK;
    for (int i = 0; i < rounds; ++i) {
        int which = I(0, 37);
        if (only)
            which = std::atoi(only);
        RandomSquad(P(0.3f));
        S_Sq *s = Sq();
        const int dt = SqDt();
        C_Human *other = P(0.5f) ? PortOther() : PortThird();
        C_Actor *target = P(0.2f) ? nullptr : P(0.4f) ? static_cast<C_Actor *>(h) : P(0.5f) ? static_cast<C_Actor *>(other) : SqActor(I(0, 11));
        // (A weapon in the hands where the game has one: the line of fire's tests and the melee read it.)
        if ((which == 9 || which == 10 || which == 25) && !s->m_handsItem) {
            s->m_handsItem = reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
            g_wornContainers[ITEM_CONTAINER_HANDS].item = g_handsWeapon = s->m_handsItem;
        }
        switch (which) {
        case 0: {
            S_HumanEvent *event = &g_daEv[I(0, DA_EVENTS - 1)];
            SqCall("HandleEvent", [&] { return int(SqMapped(s->HandleEvent(event))); },
                   [&](void *) { return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OAi<void *>(0x00528580, static_cast<void *>(event))))); });
            break;
        }
        case 1: {
            static const int types[] = {AI_EVENT_HIT_5, AI_EVENT_LOUD_SOUND, AI_EVENT_SHOT_AT, AI_EVENT_SOUND_0, AI_EVENT_SOUND_2, AI_EVENT_KILLED_ENEMY};
            const int type = types[I(0, 5)];
            const bool filter = P(0.5f);
            std::vector<int> portFound;
            alignas(16) uint8_t origFound[16] = {};
            SqCall("FindStimuli",
                   [&] {
                       const bool r = (s->*DaGet(SqFindStimuli()))(type, portFound, filter);
                       int sum = int(r);
                       for (int k : portFound)
                           sum = sum * 31 + k;
                       return sum;
                   },
                   [&](void *) {
                       const bool r = (OAi<uint32_t>(0x00527560, type, static_cast<void *>(origFound), int(filter)) & 0xFF) != 0;
                       int sum = int(r);
                       for (uint32_t k : MsvcVectorGet(origFound))
                           sum = sum * 31 + int(k);
                       return sum;
                   });
            break;
        }
        case 2:
            SqCall("ReactToStimuli", [&] { return int((s->*DaGet(SqReactToStimuli()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x005276e0) & 0xFF); });
            break;
        case 3: {
            const int type = I(0, 6), zone = I(0, 6), health = I(-10, 200);
            C_Actor *attacker = P(0.2f) ? nullptr : P(0.6f) ? static_cast<C_Actor *>(other) : SqActor(I(0, 11));
            const S_vector pos = XYZ(SqNear(3)), dir = XYZ(AiDir());
            const float force = P(0.5f) ? 100.0f : U(0, 200);
            SqCall("TakeHit", [&] { (s->*DaGet(SqTakeHit()))(type, zone, health, attacker, pos, dir, force); return 0; },
                   [&](void *) { OAi<void>(0x0052e7c0, type, zone, health, SqOrigOf(attacker), &pos, &dir, force); return 0; });
            break;
        }
        case 4:
        case 5: {
            const bool ordersDone = P(0.5f);
            SqCall("UpdateCombat", [&] { (s->*DaGet(SqUpdateCombat()))(dt, ordersDone); return 0; },
                   [&](void *) { OAi<void>(0x0052b810, dt, int(ordersDone)); return 0; });
            break;
        }
        case 6: {
            I3D_frame *portFrame = nullptr;
            void *origFrame = nullptr;
            SqCall("FindAirTarget", [&] { return int(SqMapped((s->*DaGet(SqFindAirTarget()))(&portFrame))) ^ int(SqMapped(portFrame)); },
                   [&](void *) {
                       void *r = OAi<void *>(0x0052cea0, &origFrame);
                       return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r))) ^ int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(origFrame)));
                   });
            break;
        }
        case 7:
            SqCall("SetBurstTimes", [&] { (s->*DaGet(SqSetBurstTimes()))(target); return 0; },
                   [&](void *) { OAi<void>(0x0052a5d0, SqOrigOf(target)); return 0; });
            break;
        case 8: {
            int p[4] = {-7, -7, -7, -7}, o[4] = {-7, -7, -7, -7};
            SqCall("GetBurstDefaults",
                   [&] {
                       (s->*DaGet(SqGetBurstDefaults()))(&p[0], &p[1], &p[2], &p[3]);
                       return ((p[0] * 31 + p[1]) * 31 + p[2]) * 31 + p[3];
                   },
                   [&](void *) {
                       OAi<void>(0x0052b080, &o[0], &o[1], &o[2], &o[3]);
                       return ((o[0] * 31 + o[1]) * 31 + o[2]) * 31 + o[3];
                   });
            break;
        }
        case 9:
            SqCall("CanFireAt", [&] { return int((s->*DaGet(SqCanFireAt()))()); }, [&](void *) { return int(OAi<uint32_t>(0x0052a810) & 0xFF); });
            break;
        case 10:
            SqCall("HasClearLine", [&] { return int((s->*DaGet(SqHasClearLine()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x0052ac20) & 0xFF); });
            break;
        case 11: {
            const float range = P(0.5f) ? 7.0f : U(1, 30);
            SqCall("FindNearMeleeTracker", [&] { return int(SqMapped((s->*DaGet(SqFindNearMeleeTracker()))(range))); },
                   [&](void *) { return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OAi<void *>(0x00532a20, range)))); });
            break;
        }
        case 12: {
            const float range = P(0.5f) ? 7.0f : U(1, 30);
            SqCall("FindMeleeTrackerTarget", [&] { return int(SqMapped((s->*DaGet(SqFindMeleeTrackerTarget()))(range))); },
                   [&](void *) { return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OAi<void *>(0x0052b180, range)))); });
            break;
        }
        case 13: {
            // (Cars (part 23's, with its body frame), static weapons and the others, as AimAt is given.)
            C_Actor *aimed = SqActor(P(0.4f) ? 3 : P(0.5f) ? 5 : I(0, 11));
            SqCall("AimAt", [&] { (s->*DaGet(SqAimAt()))(aimed); return 0; }, [&](void *) { OAi<void>(0x0052b580, SqOrigOf(aimed)); return 0; });
            break;
        }
        case 14: {
            const S_vector dir = XYZ(AiDir());
            const bool withDir = P(0.5f);
            SqCall("PickCombatTarget", [&] { return int(SqMapped((s->*DaGet(SqPickCombatTarget()))(withDir ? &dir : nullptr))); },
                   [&](void *) {
                       return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OAi<void *>(0x0052d0d0, withDir ? &dir : nullptr))));
                   });
            break;
        }
        case 15: {
            const bool friendOnly = P(0.5f);
            SqCall("FindEnemyTargeting", [&] { return int(SqMapped((s->*DaGet(SqFindEnemyTargeting()))(friendOnly))); },
                   [&](void *) { return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OAi<void *>(0x0052f160, int(friendOnly))))); });
            break;
        }
        case 16: {
            // (Always a direction: the view's, as the AI's fire check passes it.)
            const S_vector dir = XYZ(AiDir());
            SqCall("Unknown114", [&] { return int(s->Unknown114(&dir)); }, [&](void *) { return int(OAi<uint32_t>(0x0052f1f0, &dir) & 0xFF); });
            break;
        }
        case 17:
            SqCall("WeaponCheckDue", [&] { return int((s->*DaGet(SqWeaponCheckDue()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x0052f700) & 0xFF); });
            break;
        case 18: {
            std::vector<uint32_t> prefs;
            alignas(16) uint8_t origPrefs[16] = {};
            SqCall("BuildWeaponPrefs",
                   [&] {
                       (s->*DaGet(SqBuildWeaponPrefs()))(target, prefs);
                       int sum = 0;
                       for (uint32_t k : prefs)
                           sum = sum * 31 + int(k);
                       return sum;
                   },
                   [&](void *) {
                       OAi<void>(0x0052f750, SqOrigOf(target), static_cast<void *>(origPrefs));
                       int sum = 0;
                       for (uint32_t k : MsvcVectorGet(origPrefs))
                           sum = sum * 31 + int(k);
                       return sum;
                   });
            break;
        }
        case 19: {
            static const int behaviours[] = {ITEM_BEHAVIOUR_LAUNCHER, ITEM_BEHAVIOUR_SMG, ITEM_BEHAVIOUR_RIFLE, ITEM_BEHAVIOUR_THROWN,
                                             ITEM_BEHAVIOUR_SIDEARM, ITEM_BEHAVIOUR_LMG, ITEM_BEHAVIOUR_ASSAULT, ITEM_BEHAVIOUR_MELEE};
            const int behaviour = behaviours[I(0, 7)];
            const bool scoped = P(0.3f), silenced = P(0.3f);
            int pc = -5, oc = -5;
            SqCall("FindWeapon", [&] { return int(SqMapped((s->*DaGet(SqFindWeapon()))(behaviour, scoped, silenced, &pc))) ^ (pc << 4); },
                   [&](void *) {
                       void *r = OAi<void *>(0x005304b0, behaviour, int(scoped), int(silenced), &oc);
                       return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r))) ^ (oc << 4);
                   });
            break;
        }
        case 20: {
            std::vector<uint32_t> prefs;
            for (int k = I(0, 5); k > 0; --k)
                prefs.push_back(P(0.8f) ? (1u << I(0, 11)) : uint32_t(I(0, 0xFFF)));
            alignas(16) uint8_t origPrefs[16] = {};
            MsvcVectorPut(origPrefs, prefs);
            int pc = -5, oc = -5;
            SqCall("PickWeapon", [&] { return int(SqMapped((s->*DaGet(SqPickWeapon()))(prefs, &pc))) ^ (pc << 4); },
                   [&](void *) {
                       void *r = OAi<void *>(0x00530930, static_cast<void *>(origPrefs), &oc);
                       return int(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r))) ^ (oc << 4);
                   });
            break;
        }
        case 21:
            SqCall("ChooseWeapon", [&] { (s->*DaGet(SqChooseWeapon()))(target); return 0; },
                   [&](void *) { OAi<void>(0x00530a40, SqOrigOf(target)); return 0; });
            break;
        case 22: {
            C_Item *item = P(0.2f) ? nullptr : reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
            SqCall("IsWeaponSuitable", [&] { return int((s->*DaGet(SqIsWeaponSuitable()))(target, item)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00530b30, SqOrigOf(target), OrigWeapon(item)) & 0xFF); });
            break;
        }
        case 23:
            SqCall("UpdateWeaponChoice", [&] { return int((s->*DaGet(SqUpdateWeaponChoice()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00530d40) & 0xFF); });
            break;
        case 24:
            SqCall("ReloadFromBelt", [&] { return int((s->*DaGet(SqReloadFromBelt()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x005318c0) & 0xFF); });
            break;
        case 25: {
            C_Actor *victim = P(0.6f) ? static_cast<C_Actor *>(h) : SqActor(I(0, 11));
            SqCall("Melee", [&] { (s->*DaGet(SqMelee()))(victim); return 0; }, [&](void *) { OAi<void>(0x00536a30, SqOrigOf(victim)); return 0; });
            break;
        }
        case 26:
            SqCall("HasSeatWeapon", [&] { return int(s->HasSeatWeapon()); }, [&](void *) { return int(OAi<uint32_t>(0x005328a0) & 0xFF); });
            break;
        case 27:
            SqCall("HasProjectileSeatWeapon", [&] { return int(s->HasProjectileSeatWeapon()); },
                   [&](void *) { return int(OAi<uint32_t>(0x005328d0) & 0xFF); });
            break;
        case 28:
        case 29:
        case 30:
        case 31: {
            // The gunner: in the seat car's seat (a car, a static weapon, a submarine or the Junkers), its gun.
            static const int types[] = {ACTOR_CAR, ACTOR_STATIC_WEAPON, ACTOR_SUBMARINE, ACTOR_JUNKERS88};
            SqSeatTheMember(types[I(0, 3)]);
            g_asSeat.hasWeapon = P(0.8f);
            g_asSeat.def = true;
            C_Actor *aimed = P(0.5f) ? static_cast<C_Actor *>(h) : SqActor(P(0.5f) ? 3 : I(0, 11));
            // (The Junkers aimed at through the frame the air target search gave: set with it, both sides' +0x240.)
            if (aimed->GetType() == ACTOR_JUNKERS88)
                s->*DaGet(SqAirTargetFrame()) = g_targetFrame;
            if (which == 28) {
                SqCall("VehicleCombat", [&] { return int((s->*DaGet(SqVehicleCombat()))(dt)); },
                       [&](void *) { return int(OAi<uint32_t>(0x00527fd0, dt) & 0xFF); });
            } else if (which == 29) {
                S_vector4 pd{}, od{};
                SqCall("GetVehicleAimDir",
                       [&] {
                           (s->*DaGet(SqGetVehicleAimDir()))(&pd, aimed);
                           return 0;
                       },
                       [&](void *) {
                           OAi<void>(0x0052c510, &od, SqOrigOf(aimed));
                           return 0;
                       });
                CHECK(std::memcmp(&pd, &od, 12) == 0);
            } else if (which == 30) {
                SqCall("AimSeatWeapon", [&] { (s->*DaGet(SqAimSeatWeapon()))(aimed); return 0; },
                       [&](void *) { OAi<void>(0x00532910, SqOrigOf(aimed)); return 0; });
            } else {
                SqCall("UpdateGunner", [&] { (s->*DaGet(SqUpdateGunner()))(dt); return 0; }, [&](void *) { OAi<void>(0x00532ba0, dt); return 0; });
            }
            break;
        }
        case 32:
            SqCall("UpdateAlertPreset", [&] { (s->*DaGet(SqUpdateAlertPreset()))(); return 0; },
                   [&](void *) { OAi<void>(0x005325c0); return 0; });
            break;
        case 33: {
            S_vector4 pd{}, od{};
            uint32_t ps = 77, os = 77;
            SqCall("ChooseCoverFacing",
                   [&] {
                       (s->*DaGet(SqChooseCoverFacing()))(&pd, &ps);
                       return int(ps);
                   },
                   [&](void *) {
                       OAi<void>(0x00525c90, &od, &os);
                       return int(os);
                   });
            CHECK(std::memcmp(&pd, &od, 12) == 0);
            break;
        }
        case 34: {
            const bool firing = P(0.5f);
            SqCall("CoverStance", [&] { (s->*DaGet(SqCoverStanceFn()))(firing); return 0; },
                   [&](void *) { OAi<void>(0x00537fb0, int(firing)); return 0; });
            break;
        }
        default:
            SqCall("Tick", [&] { s->Tick(dt); return 0; }, [&](void *) { OAi<void>(0x00526550, dt); return 0; });
            break;
        }
    }
}

// ---- part 44d: the squad's movement (game/squad_movement.cpp): the pace, stepping aside, room for the leader, the
// points to attack from, the special points, running from a grenade
void TestSquadD()
{
    std::printf("squad movement: pace, stepping aside, room for the leader, attack points, special points, grenades\n");
    C_Human *h = g_h.human;
    const char *only = std::getenv("HUMAN_SQUAD_CASE");
    const int rounds = only ? 2000 : 20000 / QUICK;
    for (int i = 0; i < rounds; ++i) {
        int which = I(0, 9);
        if (only)
            which = std::atoi(only);
        RandomSquad(false);
        S_Sq *s = Sq();
        const int dt = SqDt();
        C_Human *other = P(0.5f) ? PortOther() : PortThird();
        switch (which) {
        case 0: {
            const bool ordersDone = P(0.5f);
            SqCall("UpdatePace", [&] { (s->*DaGet(SqUpdatePace()))(dt, ordersDone); return 0; },
                   [&](void *) { OAi<void>(0x0052d460, dt, int(ordersDone)); return 0; });
            break;
        }
        case 1: {
            const S_vector4 pos = SqNear(P(0.5f) ? 1.5f : 10), dir = AiDir();
            const bool sideways = P(0.5f), frontOnly = P(0.5f);
            SqCall("StepAside", [&] { (s->*DaGet(SqStepAside()))(pos, dir, sideways, frontOnly); return 0; },
                   [&](void *) { OAi<void>(0x0052dc10, &pos, &dir, int(sideways), int(frontOnly)); return 0; });
            break;
        }
        case 2: {
            C_Actor *actor = P(0.5f) ? static_cast<C_Actor *>(other) : SqActor(I(0, 11));
            SqCall("StepAsideFrom", [&] { (s->*DaGet(SqStepAsideFrom()))(actor); return 0; },
                   [&](void *) { OAi<void>(0x005356e0, SqOrigOf(actor)); return 0; });
            break;
        }
        case 3: {
            // (The selected member near now and then: room is made.)
            if (g_sq.selected && P(0.6f)) {
                const S_vector &at = h->GetFrame()->GetWorldPos();
                const S_vector4 close = {at.x + U(-1, 1), at.y, at.z + U(-1, 1), 1.0f};
                g_sq.selected->m_position = close;
                std::memcpy(static_cast<uint8_t *>(SqOrigOf(g_sq.selected)) + 0x30, &close, 16);
            }
            SqCall("MakeRoomForLeader", [&] { (s->*DaGet(SqMakeRoomForLeader()))(); return 0; },
                   [&](void *) { OAi<void>(0x0052e310); return 0; });
            break;
        }
        case 4: {
            C_Actor *target = P(0.5f) ? static_cast<C_Actor *>(h) : SqActor(I(0, 11));
            S_vector4 pp{}, op{};
            float pr = -1, orr = -1;
            SqCall("FindAttackPoint", [&] { return int((s->*DaGet(SqFindAttackPoint()))(&pp, &pr, target)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00532190, &op, &orr, SqOrigOf(target)) & 0xFF); });
            CHECK(std::memcmp(&pp, &op, 12) == 0 && pr == orr);
            break;
        }
        case 5:
            SqCall("AtSpecialPoint", [&] { return int((s->*DaGet(SqAtSpecialPoint()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x005324d0) & 0xFF); });
            break;
        case 6:
        case 7: {
            // A grenade thrown: from 0 .. 40 m, toward the member or past it.
            const S_vector &at = h->GetFrame()->GetWorldPos();
            const float range = U(0, 40);
            const S_vector from = {at.x + U(-1, 1) * range, at.y + U(-1, 1), at.z + U(-1, 1) * range};
            S_vector dir = P(0.6f) ? S_vector(at.x - from.x + U(-8, 8), 0, at.z - from.z + U(-8, 8)) : XYZ(AiDir());
            C_Actor *thrower = P(0.1f) ? static_cast<C_Actor *>(h) : static_cast<C_Actor *>(other);
            SqCall("OnGrenadeThrown", [&] { s->OnGrenadeThrown(from, dir, thrower); return 0; },
                   [&](void *) { OAi<void>(0x00530ff0, &from, &dir, SqOrigOf(thrower)); return 0; });
            break;
        }
        default:
            SqCall("Tick", [&] { s->Tick(dt); return 0; }, [&](void *) { OAi<void>(0x00526550, dt); return 0; });
            break;
        }
    }
}

void TestSquad()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    SetUpFireOrder();
    SetUpSquad();
    // (HUMAN_SQUAD_PART: the parts to run; the mutation runs take their own.)
    const char *parts = std::getenv("HUMAN_SQUAD_PART");
    if (!parts || std::strchr(parts, 'a')) {
        TestSquadA();
        TestSquadOrders();
    }
    if (!parts || std::strchr(parts, 'b'))
        TestSquadB();
    if (!parts || std::strchr(parts, 'c'))
        TestSquadC();
    if (!parts || std::strchr(parts, 'd'))
        TestSquadD();
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
}
}  // namespace

I3D_frame *SqMarkerFor(bool original) { return SqMarker(original); }
