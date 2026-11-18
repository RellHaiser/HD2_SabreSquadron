// Part 46 of the human harness (HUMAN_AIMOVE builds, build_aimove.bat; included by human_test.cpp after enemy_test.inc,
// whose pair, fakes and state it uses): the AI controllers' movement (game/ai_movement.cpp) against the original's
// (0x00578770 .. 0x00581f20), on the enemy pair (its type set to the others' now and then: the squad's, the scripted
// AI's, the sniper's, the neutral one's).
//
// The route (the waypoints, the one passed), the move order, the facing, the driver and the axes are part of the AI
// pair's comparison (aictl_test.inc); the human's AI memory is shared (its route-changed word compared as a shared
// word).
//
// Faked on both sides and logged (the answers g_am's): the walking network's FindNearestType15, FindNearestFree,
// FindPath and FindEscapePath (the original's paths MSVC6 vectors); the body's movement stance; the human's
// IsAISuspended; the team's selected soldier; the script's OnActionDone; the door's GetSide; the car's SetInput. The
// human's GetOrderCount is human_control.cpp's, copied (not linked here).
namespace {
struct S_AmAnswers {
    int nearest15 = 0xFFFF;                             // FindNearestType15 (0xFFFF: none, as the network's)
    int nearestFree = 0xFFFF;                           // FindNearestFree
    std::vector<uint16_t> path;                         // FindPath (none: not found)
    std::vector<uint16_t> escape;                       // FindEscapePath (none: not found)
    uint32_t stance = 0;                                // S_HumanBody::GetMovementStance
    bool suspended = false;                             // C_Human::IsAISuspended
    C_Human *selected = nullptr;                        // the team's selected soldier
    int side = 0;                                       // C_Door::GetSide
} g_am;
int AmPointIndex(const void *point)
{
    if (!point)
        return -1;
    return int((static_cast<const uint8_t *>(point) - g_aiPoints[0]) / int(sizeof(S_Checkpoint)));
}
void *AmMappedOrNull(const void *p) { return p ? ToOrig(p) : nullptr; }
// An MSVC6 vector of uint16_t (the original's new: malloc here).
void AmPutPath(uint32_t *v, const std::vector<uint16_t> &path)
{
    const size_t n = path.size();
    auto *data = static_cast<uint16_t *>(std::malloc(n * 2 + 2));
    if (n)
        std::memcpy(data, path.data(), n * 2);
    v[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    v[2] = v[1] + 2 * static_cast<uint32_t>(n);
    v[3] = v[2];
}
bool AmFindPath(const void *network, int from, int to, std::vector<uint16_t> *path, uint32_t excludeFlags)
{
    Log(network, "FindPath", {double(from & 0xFFFF), double(to & 0xFFFF), double(excludeFlags)});
    *path = g_am.path;
    return !g_am.path.empty();
}
}  // namespace

// ai_movement.cpp's free functions (called from other modules, not declared in a header).
C_Actor *FindActorNear(const S_vector &pos, const std::vector<C_Actor *> &actors);
bool IsPosNearSegment(const S_vector &pos, const S_AIWaypoint &a, const S_AIWaypoint &b);

// ---- the port's side
int C_CheckpointNetwork::FindNearestType15(S_vector pos, float maxDist, float minDist) const
{
    Log(this, "FindNearestType15", {pos.x, pos.y, pos.z, maxDist, minDist});
    return g_am.nearest15;
}
int C_CheckpointNetwork::FindNearestFree(const S_Checkpoint *from)
{
    Log(this, "FindNearestFree", {double(AmPointIndex(from))});
    return g_am.nearestFree;
}
bool C_CheckpointNetwork::FindEscapePath(int from, const S_Vector4Array &threats, float minDist, float safeDist,
                                         std::vector<uint16_t> &path, uint32_t excludeFlags)
{
    Log(this, "FindEscapePath", {double(from), double(threats.count), threats.count ? threats.data[0].x : 0.0, minDist, safeDist,
                                 double(excludeFlags)});
    path = g_am.escape;
    return !g_am.escape.empty();
}
uint32_t S_HumanBody::GetMovementStance() const
{
    Log(this, "BodyGetMovementStance");
    return g_am.stance;
}
bool C_Human::IsAISuspended() const
{
    Log(this, "IsAISuspended");
    return g_am.suspended;
}
int C_Human::GetOrderCount() const
{
    if (m_controller->GetType() != HUMAN_CONTROLLER_SQUAD)
        return 0;
    return static_cast<C_SquadController *>(m_controller)->GetOrderCount();
}
C_Human *C_Team::GetSelectedMember() const
{
    Log(this, "TeamGetSelectedMember");
    return g_am.selected;
}
void C_Script::OnActionDone(int label) { Log(this, "ScriptOnActionDone", {double(label)}); }
int C_Door::GetSide(const S_vector4 &pos) const
{
    Log(this, "DoorGetSide", {pos.x, pos.y, pos.z});
    return g_am.side;
}
bool C_Car::SetInput(float throttle, float steering)
{
    Log(this, "CarSetInput", {throttle, steering});
    return true;
}

namespace {
// ---- the original's side
int AC AM_FindNearestType15(void *self, S_vector pos, float maxDist, float minDist)
{
    Log(self, "FindNearestType15", {pos.x, pos.y, pos.z, maxDist, minDist});
    return g_am.nearest15;
}
int AC AM_FindNearestFree(void *self, const void *from)
{
    Log(self, "FindNearestFree", {double(AmPointIndex(from))});
    return g_am.nearestFree;
}
int AC AM_FindEscapePath(void *self, int from, const S_Vector4Array *threats, float minDist, float safeDist, uint32_t *path,
                         uint32_t excludeFlags)
{
    Log(self, "FindEscapePath", {double(from), double(threats->count), threats->count ? threats->data[0].x : 0.0, minDist, safeDist,
                                 double(excludeFlags)});
    AmPutPath(path, g_am.escape);
    return !g_am.escape.empty();
}
int AC AM_FindPath(void *self, int from, int to, uint32_t *path, uint32_t excludeFlags)
{
    Log(self, "FindPath", {double(from & 0xFFFF), double(to & 0xFFFF), double(excludeFlags)});
    AmPutPath(path, g_am.path);
    return !g_am.path.empty();
}
uint32_t AC AM_GetMovementStance(void *self)
{
    Log(self, "BodyGetMovementStance");
    return g_am.stance;
}
int AC AM_IsAISuspended(void *self)
{
    Log(self, "IsAISuspended");
    return g_am.suspended;
}
void *AC AM_GetSelectedMember(void *self)
{
    Log(self, "TeamGetSelectedMember");
    return AmMappedOrNull(g_am.selected);
}
void AC AM_OnActionDone(void *self, int label) { Log(self, "ScriptOnActionDone", {double(label)}); }
int AC AM_GetSide(void *self, const S_vector4 *pos)
{
    Log(self, "DoorGetSide", {pos->x, pos->y, pos->z});
    return g_am.side;
}
int AC AM_SetInput(void *self, float throttle, float steering)
{
    Log(self, "CarSetInput", {throttle, steering});
    return 1;
}
void SetUpAiMoveFakes()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0051d970, FAKE(AM_FindNearestType15)}, {0x0051e8a0, FAKE(AM_FindNearestFree)}, {0x00521460, FAKE(AM_FindEscapePath)},
        {0x005223b0, FAKE(AM_FindPath)},          {0x005ac3f0, FAKE(AM_GetMovementStance)}, {0x005a5d30, FAKE(AM_IsAISuspended)},
        {0x0045d160, FAKE(AM_GetSelectedMember)}, {0x00416d50, FAKE(AM_OnActionDone)}, {0x00416d70, FAKE(AM_OnActionDone)},
        {0x00416d90, FAKE(AM_OnActionDone)},  {0x00602320, FAKE(AM_GetSide)},
        {0x004d9280, FAKE(AM_SetInput)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // (The vehicle's setters: the combat harness's loggers, part 34's.)
    RedirectOriginal(0x004f2100, reinterpret_cast<const void *>(OD_SetThrottle));
    RedirectOriginal(0x004f2420, reinterpret_cast<const void *>(OD_SetSteering));
    RedirectOriginal(0x004f2270, reinterpret_cast<const void *>(OD_SetBrake));
    RedirectOriginal(0x004f26a0, reinterpret_cast<const void *>(OD_SetHandbrake));
    g_names[reinterpret_cast<C_Car *>(g_asCarPort)->GetVehicle()] = g_names[g_asCarOrig + 0x90] = "seatCarVehicle";
    g_findPathFake = AmFindPath;
}

DA_ROB(AmUpdateMove, int (C_AIController::*)(int), &C_AIController::UpdateMove)
DA_ROB(AmNeedsNewRoute, bool (C_AIController::*)(), &C_AIController::NeedsNewRoute)
DA_ROB(AmCheckCarsOnRoute, bool (C_AIController::*)(), &C_AIController::CheckCarsOnRoute)
DA_ROB(AmMakeWay, bool (C_AIController::*)(), &C_AIController::MakeWay)
DA_ROB(AmPlanRoute, bool (C_AIController::*)(), &C_AIController::PlanRoute)
DA_ROB(AmRouteToPos, bool (C_AIController::*)(const S_vector4 &, const S_vector4 &), &C_AIController::RouteToPos)
DA_ROB(AmRouteToCheckpoint, bool (C_AIController::*)(const S_vector4 &, uint16_t), &C_AIController::RouteToCheckpoint)
DA_ROB(AmRouteAway, bool (C_AIController::*)(const S_vector4 &), &C_AIController::RouteAwayFromEnemies)
DA_ROB(AmSmoothRoute, void (C_AIController::*)(const S_vector4 &, const S_vector4 &), &C_AIController::SmoothRoute)
DA_ROB(AmSmoothRouteTo, void (C_AIController::*)(const S_vector4 &), &C_AIController::SmoothRouteTo)
DA_ROB(AmFixRouteHeights, void (C_AIController::*)(const S_vector4 &, const S_vector4 &), &C_AIController::FixRouteHeights)
DA_ROB(AmStartRoute, bool (C_AIController::*)(), &C_AIController::StartRoute)
DA_ROB(AmFollowStep, void (C_AIController::*)(int), &C_AIController::FollowStep)
DA_ROB(AmUseLadder, void (C_AIController::*)(int), &C_AIController::UseLadder)
DA_ROB(AmPassDoor, int (C_AIController::*)(int), &C_AIController::PassDoor)
DA_ROB(AmReachedWaypoint, bool (C_AIController::*)(), &C_AIController::ReachedWaypoint)
DA_ROB(AmFollowType15, void (C_AIController::*)(int), &C_AIController::FollowType15Route)
DA_ROB(AmFollowRoute, void (C_AIController::*)(int), &C_AIController::FollowRoute)
DA_ROB(AmClimb, void (C_AIController::*)(), &C_AIController::Climb)
DA_ROB(AmWalkToWaypoint, void (C_AIController::*)(), &C_AIController::WalkToWaypoint)
DA_ROB(AmUpdateMoveStance, void (C_AIController::*)(), &C_AIController::UpdateMoveStance)
DA_ROB(AmIsOnPlainPoints, bool (C_AIController::*)(), &C_AIController::IsOnPlainPoints)
DA_ROB(AmIsAwayFromOthers, bool (C_AIController::*)(), &C_AIController::IsAwayFromOthers)
DA_ROB(AmUpdateAvoidance, void (C_AIController::*)(), &C_AIController::UpdateAvoidance)
DA_ROB(AmHasAvoidance, bool (C_AIController::*)(), &C_AIController::HasAvoidance)
DA_ROB(AmCanAvoid, bool (C_AIController::*)(), &C_AIController::CanAvoid)
DA_ROB(AmWalkAvoiding, void (C_AIController::*)(), &C_AIController::WalkAvoiding)
DA_ROB(AmEndMove, void (C_AIController::*)(int), &C_AIController::EndMove)
DA_ROB(AmMustSlowDown, bool (C_AIController::*)(), &C_AIController::MustSlowDown)
DA_ROB(AmIsMarchBlocked, bool (C_AIController::*)(), &C_AIController::IsMarchBlocked)
DA_ROB(AmUpdateCurrentCheckpoint, void (C_AIController::*)(), &C_AIController::UpdateCurrentCheckpoint)
DA_ROB(AmIsNearSegment, bool (C_AIController::*)(const S_AIWaypoint &, const S_AIWaypoint &), &C_AIController::IsNearSegment)
DA_ROB(AmNearerPoint, S_Checkpoint *(C_AIController::*)(const S_AIWaypoint &, const S_AIWaypoint &), &C_AIController::NearerPoint)
DA_ROB(AmAvoidOthers, void (C_AIController::*)(bool), &C_AIController::AvoidOthersOnRoute)
DA_ROB(AmIsHumanWithin, bool (C_AIController::*)(const S_vector *, float), &C_AIController::IsHumanWithin)
DA_ROB(AmIsNearSelected, bool (C_AIController::*)() const, &C_AIController::IsNearSelectedSoldier)
DA_ROB(AmClearRoute, void (C_AIController::*)(), &C_AIController::ClearRoute)
DA_ROB(AmSkipPassed, void (C_AIController::*)(), &C_AIController::SkipPassedWaypoint)
DA_ROB(AmUpdateDriving, int (C_AIController::*)(int), &C_AIController::UpdateDriving)
DA_ROB(AmOnDriveArrived, int (C_AIController::*)(C_Car *), &C_AIController::OnDriveArrived)
DA_ROB(AmSteerForward, void (C_AIController::*)(C_Car *, const S_vector &, const S_vector &, const S_vector &, const S_vector &, bool,
                                                const S_vector &),
       &C_AIController::SteerForward)
DA_ROB(AmSteerReverse, void (C_AIController::*)(C_Car *, const S_vector &, const S_vector &, const S_vector &, const S_vector &, bool,
                                                const S_vector &),
       &C_AIController::SteerReverse)
DA_ROB(AmSetRoutePoints, void (C_AIController::*)(const std::vector<uint16_t> &), &C_AIController::SetRoutePoints)

DA_ROB(AmClimbers, std::vector<C_Human *> C_Ladder::*, &C_Ladder::m_climbers)
DA_ROB(AmVehSpeed, float C_Vehicle::*, &C_Vehicle::m_speed)
DA_ROB(AmVehTopSpeed, float C_Vehicle::*, &C_Vehicle::m_topSpeed)
DA_ROB(AmVehVelocity, S_vector4 C_Vehicle::*, &C_Vehicle::m_velocity)
void CmSetTypeBoth(void *port, uint8_t *orig, int type)
{
    static_cast<C_Actor *>(port)->m_type = static_cast<E_ActorType>(type);
    std::memcpy(orig + 0x18, &type, 4);
}
DA_ROB(AmDoorMotion, int C_Door::*, &C_Door::m_motion)
DA_ROB(AmDoorLocked, bool C_Door::*, &C_Door::m_locked)
C_AIController *Am() { return g_pa; }
template <class R, class... A> R OAm(uint32_t at, A... a) { return OAi<R>(at, a...); }
int AmBool(int r) { return r & 0xFF; }

// ---- the state
// The human's position.
S_vector AmOwn() { return g_h.frame->GetWorldPos(); }
S_vector4 AmNear(float spread)
{
    return ToVector4(AmOwn() + S_vector(U(-spread, spread), P(0.3f) ? 0.0f : U(-1.5f, 1.5f), U(-spread, spread)), P(0.5f) ? 1.0f : 0.0f);
}
// A waypoint: a walking point's (its position, flags, type, radius) or one of its own (-1), now and then within
// 0.1 .. 1 m of the human.
const uint8_t AM_TYPES[] = {0, 0, 1, 3, 4, 5, 6, 7, 10, 11, 15, 15};
S_AIWaypoint *AmWaypoint()
{
    const int index = P(0.25f) ? -1 : I(0, 7);
    if (index >= 0 && P(0.7f)) {
        const auto *point = reinterpret_cast<const S_Checkpoint *>(g_aiPoints[index]);
        S_AIWaypoint *w = AllocWaypoint(index, ToVector4(point->pos, 0.0f), point->flags & CHECKPOINT_SEARCH_FLAGS, point->type,
                                        static_cast<float>(point->radius) * 0.0625f);
        if (P(0.5f))
            w->pos = AmNear(w->radius);
        return w;
    }
    const S_vector4 pos = P(0.2f) ? ToVector4(AmOwn() + S_vector(U(-1, 1), 0, U(-1, 1)), 0.0f) : AmNear(10);
    S_AIWaypoint *w = AllocWaypoint(index, pos, uint16_t(P(0.5f) ? 0 : I(0, 0xFFFF)), AM_TYPES[I(0, 11)], P(0.2f) ? 0.0f : U(0, 3));
    if (P(0.3f))
        w->flags = 0x0100;
    if (P(0.5f))
        w->pos2 = AmNear(3);
    return w;
}
void AmClearWaypoints()
{
    for (S_AIWaypoint *w : Am()->m_waypoints)
        ReleaseWaypoint(w);
    Am()->m_waypoints.clear();
}
// The first waypoint reached (at the human, the steps' count low) between special points: ladders, the climbs over
// (4), onto (5 from 6), the gap's dive (5, 6, 5); the human's point of the same kinds.
void AmSpecialPoints()
{
    C_AIController *a = Am();
    static const uint8_t kinds[] = {1, 3, 4, 5, 6};
    // (One type per walking point: the waypoints on one point alike.)
    uint8_t typeOf[8];
    for (uint8_t &t : typeOf)
        t = kinds[I(0, 4)];
    for (S_AIWaypoint *w : a->m_waypoints) {
        w->type = w->checkpoint >= 0 && w->checkpoint < 8 ? typeOf[w->checkpoint] : kinds[I(0, 4)];
        // (A door waypoint's point has its door; a ladder's its ladder or none.)
        if (w->checkpoint >= 0 && w->checkpoint < 8)
            g_en.pointActor[w->checkpoint] = w->type == 1 ? 0 : w->type == 3 ? (P(0.8f) ? 7 : -1) : g_en.pointActor[w->checkpoint];
    }
    if (!a->m_waypoints.empty() && P(0.6f)) {
        a->m_waypoints[0]->pos = ToVector4(AmOwn() + S_vector(U(-0.05f, 0.05f), U(-0.5f, 0.5f), U(-0.05f, 0.05f)), 0.0f);
        a->m_move->unknown28 = 0;
    }
    if (a->m_waypoints.size() > 2 && P(0.3f)) {
        a->m_waypoints[0]->type = 5;
        a->m_waypoints[1]->type = 6;
        a->m_waypoints[2]->type = 5;
        a->m_waypoints[2]->pos = ToVector4(XYZ(a->m_waypoints[0]->pos) + S_vector(U(-2.5f, 2.5f), 0, U(-2.5f, 2.5f)), 0.0f);
    }
    if (a->m_currentCheckpoint)
        a->m_currentCheckpoint->type = kinds[I(0, 3)];
}
// The other human on the way to the first waypoint (within 0.6 m of it, not near its end): a detour waypoint.
void AmBlockerOnTheWay()
{
    C_AIController *a = Am();
    if (a->m_waypoints.empty())
        return;
    const S_vector from = AmOwn(), to = XYZ(a->m_waypoints[0]->pos);
    const float t = U(0.3f, 0.7f);
    I3D_frame *frame = PortOther()->GetFrame();
    frame->SetPosInline(from + (to - from) * t + S_vector(U(-0.4f, 0.4f), 0, U(-0.4f, 0.4f)));
    UpdateTree(frame);
}
// The marching pace's conditions now and then: relaxed (alert 1), standing, the weapon down, the pace asked for.
void AmMarch()
{
    C_AIController *a = Am();
    a->m_alert->state = 1;
    a->m_moveFlags = (a->m_moveFlags & ~uint32_t(MOVE_LYING | MOVE_CROUCHING)) | MOVE_STANDING;
    a->m_weaponRaised = false;
    a->m_move->unknown54 = MOVE_PACE_MARCH;
    for (S_AIWaypoint *w : a->m_waypoints) {
        w->flags = 0;
        w->type = 0;
    }
    if (a->m_currentCheckpoint)
        a->m_currentCheckpoint->flags = uint16_t(a->m_currentCheckpoint->flags & ~0x0800);
    g_h.human->m_aiMemory->unknown109 = false;
}
void AmRandomRoute()
{
    C_AIController *a = Am();
    for (S_AIWaypoint *w : a->m_waypoints)
        ReleaseWaypoint(w);
    a->m_waypoints.clear();
    for (int n = P(0.2f) ? 0 : I(1, 5); n > 0; --n)
        a->m_waypoints.push_back(AmWaypoint());
    ReleaseWaypoint(a->m_waypoint);
    a->m_waypoint = P(0.3f) ? nullptr : AmWaypoint();
    // (A door waypoint's point has its door, a ladder waypoint's its ladder or none, as in the game.)
    for (const S_AIWaypoint *w : a->m_waypoints)
        if (w->checkpoint >= 0 && w->checkpoint < 8) {
            if (w->type == CHECKPOINT_TYPE_DOOR)
                g_en.pointActor[w->checkpoint] = 0;
            else if (w->type == 3 && g_en.pointActor[w->checkpoint] != 7)
                g_en.pointActor[w->checkpoint] = -1;
        }
}
// The walking points: types the movement handles more often, the cars' flag, the slow flag.
void AmRandomPoints()
{
    for (int k = 0; k < 8; ++k) {
        auto *point = reinterpret_cast<S_Checkpoint *>(g_aiPoints[k]);
        // (No two points at one place, as in the game's networks, and none exactly where the human stands: the
        // corner cut then divides by zero on both sides, and the facing takes the NaN differently; not tested.)
        if (std::memcmp(&point->pos, &g_h.frame->GetWorldPos(), 12) == 0)
            point->pos = point->pos + S_vector(U(0.2f, 1) * (P(0.5f) ? 1 : -1), 0, U(0.2f, 1) * (P(0.5f) ? 1 : -1));
        if (k > 0)
            for (int j = 0; j < k; ++j)
                if (std::memcmp(&reinterpret_cast<const S_Checkpoint *>(g_aiPoints[j])->pos, &point->pos, 12) == 0)
                    point->pos = point->pos + S_vector(U(0.5f, 3) * (P(0.5f) ? 1 : -1), 0, U(0.5f, 3) * (P(0.5f) ? 1 : -1));
        if (P(0.5f))
            point->type = AM_TYPES[I(0, 11)];
        point->flags = uint16_t((point->flags & ~uint16_t(CHECKPOINT_FLAG_UNDER_CAR | 0x0800)) |
                                (P(0.2f) ? CHECKPOINT_FLAG_UNDER_CAR : 0) | (P(0.2f) ? 0x0800 : 0));
        for (uint8_t &v : point->values)
            v = uint8_t(P(0.3f) ? 0xFF : I(0, 254));
        // The point's actor: a door's its door, a ladder's its ladder (or none), others anything (the original takes a
        // door point's actor for a door unchecked).
        g_en.pointActor[k] = point->type == CHECKPOINT_TYPE_DOOR ? 0 : point->type == 3 ? (P(0.8f) ? 7 : -1) : (P(0.5f) ? -1 : I(0, 11));
    }
    // The door's state (the port's members, the original's +0x224 motion, +0x234 locked).
    auto *door = static_cast<C_Door *>(static_cast<C_Actor *>(PortActor(0)));
    const int motion = I(0, 3);
    const bool locked = P(0.3f);
    door->*DaGet(AmDoorMotion()) = motion;
    door->*DaGet(AmDoorLocked()) = locked;
    uint8_t *od = static_cast<uint8_t *>(OrigActor(0));
    std::memcpy(od + 0x224, &motion, 4);
    od[0x234] = locked;
}
// A path as the network's search gives: distinct points (none: not found).
std::vector<uint16_t> AmPath()
{
    std::vector<uint16_t> all = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int k = 7; k > 0; --k)
        std::swap(all[size_t(k)], all[size_t(I(0, k))]);
    all.resize(size_t(P(0.3f) ? 0 : I(1, 5)));
    return all;
}
const int AM_CONTROLLER_TYPES[] = {HUMAN_CONTROLLER_ENEMY, HUMAN_CONTROLLER_ENEMY, HUMAN_CONTROLLER_SQUAD, HUMAN_CONTROLLER_AI,
                                   HUMAN_CONTROLLER_SNIPER, HUMAN_CONTROLLER_NEUTRAL};
// The driver: the seat car (a car: its +0x5E0 armour word aside), the human in it now and then in the driver's seat;
// the road path (its points around the car, the last one the destination), the state of following it.
void AmRandomDriver()
{
    C_AIController *a = Am();
    C_AIDriver *d = a->m_driver;
    // (Not the squad's type: OnDriveArrived's GetOrderCount takes such a controller for a squad controller.)
    if (a->m_type == HUMAN_CONTROLLER_SQUAD)
        a->m_type = HUMAN_CONTROLLER_ENEMY;
    C_Car *car = reinterpret_cast<C_Car *>(g_asCarPort);
    CmSetTypeBoth(g_asCarPort, g_asCarOrig, ACTOR_CAR);
    d->m_vehicle = P(0.1f) ? nullptr : static_cast<C_Actor *>(car);
    C_Human *h = g_h.human;
    h->m_vehicle = P(0.1f) ? nullptr : static_cast<C_Actor *>(car);
    void *ov = h->m_vehicle ? static_cast<void *>(g_asCarOrig) : nullptr;
    std::memcpy(o_human + 0x160, &ov, 4);
    h->m_seat = P(0.7f) ? 0 : I(1, 3);
    std::memcpy(o_human + 0x164, &h->m_seat, 4);
    const S_vector at = car->GetFrame()->GetWorldPos();
    // The car: its speed (the vehicle's +0x8C8, the car's +0x958), top speed (+0x780), velocity (+0x4A0), direction
    // (+0x40).
    {
        C_Vehicle *v = car->GetVehicle();
        const float speed = P(0.2f) ? 0.0f : U(-5, 30), top = P(0.1f) ? 0.0f : U(5, 40);
        const S_vector4 velocity = ToVector4(S_vector(U(-20, 20), U(-1, 1), U(-20, 20)), 0.0f);
        v->*DaGet(AmVehSpeed()) = speed;
        v->*DaGet(AmVehTopSpeed()) = top;
        v->*DaGet(AmVehVelocity()) = velocity;
        std::memcpy(g_asCarOrig + 0x958, &speed, 4);
        std::memcpy(g_asCarOrig + 0x780, &top, 4);
        std::memcpy(g_asCarOrig + 0x4A0, &velocity, 16);
        const S_vector4 dir = ToVector4(S_vector(U(-1, 1), U(-0.2f, 0.2f), U(-1, 1)).Normalized(), 0.0f);
        car->m_direction = dir;
        std::memcpy(g_asCarOrig + 0x40, &dir, 16);
    }
    d->m_path.clear();
    // (Two points at least: the original reads a shorter path past its end, the port's fix arrives.)
    for (int n = I(2, 6); n > 0; --n) {
        S_AIPathPoint q;
        q.pos = ToVector4(at + S_vector(U(-30, 30), U(-0.5f, 0.5f), U(-30, 30)), 0.0f);
        q.dir = ToVector4(S_vector(U(-10, 10), 0, U(-10, 10)), 0.0f);
        d->m_path.push_back(q);
    }
    if (!d->m_path.empty())
        d->m_path.back().last = true;
    d->m_pathIndex = uint16_t(d->m_path.size() > 1 ? I(0, int(d->m_path.size()) - 2) : 0);
    d->m_progress = P(0.3f) ? 0.0f : U(0, 1);
    d->m_nearestDist2 = P(0.3f) ? 1e8f : U(0, 900);
    d->m_reversing = P(0.2f);
    d->m_moving = P(0.6f);
    d->m_stuckTime = P(0.3f) ? 3000 + I(-200, 200) : I(0, 3000);
    d->m_noProgressTime = P(0.3f) ? 5000 + I(-200, 200) : I(0, 5000);
    d->m_lastPos = ToVector4(at + S_vector(P(0.3f) ? 0.0f : U(-1, 1), 0, P(0.3f) ? 0.0f : U(-1, 1)), 1.0f);
    d->m_flags = uint32_t(I(0, 7));
    d->m_target = ToVector4(at + S_vector(U(-50, 50), 0, U(-50, 50)), 1.0f);
    d->m_targetDir = ToVector4(S_vector(U(-1, 1), 0, U(-1, 1)), 0.0f);
    d->m_speed = P(0.3f) ? -U(1, 20) : U(0, 30);
    static const uint32_t paces[] = {0, MOVE_PACE_1, MOVE_PACE_2, MOVE_PACE_3, MOVE_PACE_4, MOVE_PACE_MARCH};
    d->m_pace = paces[I(0, 5)];
    d->m_label = I(-1, 50);
    // The game's walking network (the roads hang off it: +0x38 in the port, +0x3C in the original).
    void *net = g_checkpointNet;
    GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_checkpointNet);
    std::memcpy(g_origGame + 0x54, &net, 4);
    // (The driver's own pointer to them, +0x70; the harness's conversion leaves it out.)
    d->m_roads = reinterpret_cast<C_CheckpointNetwork *>(g_checkpointNet + 0x38);
    uint8_t *od;
    std::memcpy(&od, g_oa + 0x130, 4);
    void *oroads = g_checkpointNet + 0x3C;
    std::memcpy(od + 0x70, &oroads, 4);
}
void AmRandom()
{
    RandomEnemy(false);
    // (The humans' frames' world matrices up to date: the original recomputes a dirty one where the harness's frames
    // hand out the stored one.)
    UpdateTree(g_h.frame);
    UpdateTree(PortOther()->GetFrame());
    C_AIController *a = Am();
    a->m_type = static_cast<E_HumanController>(AM_CONTROLLER_TYPES[I(0, 5)]);
    AmRandomPoints();
    AmRandomRoute();
    // The network's answers (none: 0xFFFF, as the network's; the original compares 16 bits).
    g_cs.nearest = P(0.25f) ? CHECKPOINT_NONE : I(0, 7);
    g_am.nearest15 = P(0.4f) ? CHECKPOINT_NONE : I(0, 7);
    g_am.nearestFree = P(0.3f) ? CHECKPOINT_NONE : I(0, 7);
    g_am.path = AmPath();
    g_am.escape = AmPath();
    g_am.stance = uint32_t(I(0, 7));
    g_am.suspended = P(0.2f);
    g_am.selected = P(0.3f) ? nullptr : P(0.5f) ? g_h.human : PortOther();
    g_am.side = I(0, 2);
    // The move order.
    S_AIMoveOrder *m = a->m_move;
    static const uint32_t flagBits[] = {
        S_AIMoveOrder::FLAG_NEW,        S_AIMoveOrder::FLAG_CHECKPOINT_POS, S_AIMoveOrder::FLAG_FLEE,
        S_AIMoveOrder::FLAG_TO_LADDER,  S_AIMoveOrder::FLAG_ON_LADDER,      S_AIMoveOrder::FLAG_LADDER_DOWN,
        0x00000100,                     S_AIMoveOrder::FLAG_DOOR,           S_AIMoveOrder::FLAG_TURN_FIRST,
        S_AIMoveOrder::FLAG_REROUTE,    S_AIMoveOrder::FLAG_MAKE_WAY,       S_AIMoveOrder::FLAG_WAIT,
        S_AIMoveOrder::FLAG_STANCE_SAVED, S_AIMoveOrder::FLAG_AVOIDING,     S_AIMoveOrder::FLAG_ROUTE,
        S_AIMoveOrder::FLAG_DONE,
    };
    m->flags = 0;
    for (uint32_t bit : flagBits)
        if (P(0.2f))
            m->flags |= bit;
    if (P(0.5f))
        m->flags |= S_AIMoveOrder::FLAG_ROUTE;
    m->pos = AmNear(P(0.3f) ? 0.6f : 12);
    m->checkpoint = uint16_t(P(0.4f) ? 0xFFFF : I(0, 7));
    m->unknown24 = P(0.5f) ? 0 : I(-50, 600);
    m->unknown28 = P(0.3f) ? I(0, 4) : P(0.3f) ? I(28, 31) : I(0, 40);
    m->unknown2C = P(0.3f) ? 3.402823466e+38f : U(0, 5);
    m->unknown30 = P(0.5f) ? -1 : I(0, 7);
    m->unknown34 = P(0.5f) ? 0 : I(-50, 600);
    m->unknown38 = I(0, 10);
    m->unknown3C = I(10, 60);
    m->door = P(0.6f) ? static_cast<C_Actor *>(PortActor(0)) : nullptr;
    m->ladder = P(0.6f) ? static_cast<C_Actor *>(PortActor(7)) : nullptr;
    m->ladderFrom = uint16_t(P(0.4f) ? 0xFFFF : I(0, 7));
    m->ladderTo = uint16_t(P(0.4f) ? 0xFFFF : I(0, 7));
    m->unknown4C = I(-11, 4);
    static const uint32_t paces[] = {0, MOVE_PACE_1, MOVE_PACE_2, MOVE_PACE_3, MOVE_PACE_4, MOVE_PACE_MARCH};
    m->unknown54 = paces[I(0, 5)] | (P(0.2f) ? 0x10u : 0u) | (P(0.2f) ? paces[I(0, 5)] : 0u);
    m->unknown58 = P(0.2f);
    // (The game's invariants: a ladder's flags with the ladder, the door's with the door; a route followed, or a way
    // made, with waypoints (the original's IsOnPlainPoints reads the first).)
    if (m->flags & (S_AIMoveOrder::FLAG_TO_LADDER | S_AIMoveOrder::FLAG_ON_LADDER))
        m->ladder = static_cast<C_Actor *>(PortActor(7));
    // (The door's flag only while a route is followed: Finish clears both, StartRoute's BeginRoute clears the door
    // only.)
    if (!(m->flags & S_AIMoveOrder::FLAG_ROUTE))
        m->flags &= ~uint32_t(S_AIMoveOrder::FLAG_DOOR);
    if (m->flags & S_AIMoveOrder::FLAG_DOOR)
        m->door = static_cast<C_Actor *>(PortActor(0));
    if (a->m_waypoints.empty())
        a->m_waypoints.push_back(AmWaypoint());
    // The ladder's climbers (the port's member, the original's +0x164): none, the human, the other.
    {
        std::vector<C_Human *> climbers;
        for (int n = I(0, 2); n > 0; --n)
            climbers.push_back(P(0.5f) ? g_h.human : PortOther());
        reinterpret_cast<C_Ladder *>(PortActor(7))->*DaGet(AmClimbers()) = climbers;
        std::vector<uint32_t> words;
        for (C_Human *h : climbers)
            words.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ToOrig(h))));
        MsvcVectorPut(static_cast<uint8_t *>(OrigActor(7)) + 0x160, words);
    }
    // The humans near (the other's id, as the events list others; the original takes any actor for a human).
    {
        std::vector<int> &nearby = g_daEvents.*DaGet(EvNearby());
        nearby.clear();
        g_daGoneId = -1000;   // (none gone: the original takes a gone one for a human)
        for (int n = I(0, 3); n > 0; --n)
            nearby.push_back(PortOther()->m_netId);
    }
    // The human's AI memory: stepping aside, its step, the slow flag.
    C_HumanAIMemory *memory = g_h.human->m_aiMemory;
    memory->stepping = P(0.3f);
    for (float &s : memory->step)
        s = P(0.4f) ? 0.0f : U(-1, 1);
    memory->unknown109 = P(0.2f);
    memory->routeChanged = P(0.5f);
    // The movement's own flags, the walk speed, the climb timer.
    static const uint32_t moveBits[] = {MOVE_ON_LADDER, MOVE_SWIMMING, MOVE_LYING, MOVE_STANDING, MOVE_CROUCHING, MOVE_MEDIUM_80000,
                                        MOVE_UNKNOWN_8000000, MOVE_FORWARD, MOVE_BACK, MOVE_LEFT, MOVE_RIGHT, MOVE_DEAD};
    for (uint32_t bit : moveBits)
        a->m_moveFlags = P(0.25f) ? (a->m_moveFlags | bit) : (a->m_moveFlags & ~bit);
    a->m_walkSpeed = U(0, 3);
    a->m_unknown120 = P(0.5f) ? 0 : I(-10, 1000);
    a->m_currentCheckpoint = P(0.25f) ? nullptr : reinterpret_cast<S_Checkpoint *>(g_aiPoints[I(0, 7)]);
}

void TestAiMoveCalls()
{
    const char *only = std::getenv("HUMAN_AIMOVE_CASE");
    const int rounds = only ? (std::getenv("HUMAN_AIMOVE_ROUNDS") ? std::atoi(std::getenv("HUMAN_AIMOVE_ROUNDS")) : 2000) : 30000 / QUICK;
    C_AIController *a = Am();
    for (int i = 0; i < rounds; ++i) {
        AmRandom();
        const int pick = I(0, 43);
        const int which = only ? std::atoi(only) : pick;
        const int dt = P(0.1f) ? 0 : I(1, 200);
        switch (which) {
        case 0:
            EnCall("UpdateMove", [&] { return (a->*DaGet(AmUpdateMove()))(dt); }, [&](void *) { return OAm<int>(0x0057a750, dt); });
            break;
        case 1:
            EnCall("NeedsNewRoute", [&] { return int((a->*DaGet(AmNeedsNewRoute()))()); },
                   [&](void *) { return AmBool(OAm<int>(0x0057a420)); });
            break;
        case 2:
            EnCall("CheckCarsOnRoute", [&] { return int((a->*DaGet(AmCheckCarsOnRoute()))()); },
                   [&](void *) { return AmBool(OAm<int>(0x0057a610)); });
            break;
        case 3:
            EnCall("MakeWay", [&] { return int((a->*DaGet(AmMakeWay()))()); }, [&](void *) { return AmBool(OAm<int>(0x0057b270)); });
            break;
        case 4:
            EnCall("PlanRoute", [&] { return int((a->*DaGet(AmPlanRoute()))()); }, [&](void *) { return AmBool(OAm<int>(0x0057bd00)); });
            break;
        case 5: {
            const S_vector4 from = AmNear(3), to = AmNear(15);
            EnCall("RouteToPos", [&] { return int((a->*DaGet(AmRouteToPos()))(from, to)); },
                   [&](void *) { return AmBool(OAm<int>(0x0057aaf0, &from, &to)); });
            break;
        }
        case 6: {
            const S_vector4 from = AmNear(3);
            const uint16_t point = uint16_t(P(0.1f) ? 0xFFFF : I(0, 9));
            EnCall("RouteToCheckpoint", [&] { return int((a->*DaGet(AmRouteToCheckpoint()))(from, point)); },
                   [&](void *) { return AmBool(OAm<int>(0x0057b2e0, &from, int(point))); });
            break;
        }
        case 7: {
            const S_vector4 from = AmNear(3);
            EnCall("RouteAwayFromEnemies", [&] { return int((a->*DaGet(AmRouteAway()))(from)); },
                   [&](void *) { return AmBool(OAm<int>(0x0057b740, &from)); });
            break;
        }
        case 8: {
            const S_vector4 from = AmNear(3), to = AmNear(15);
            EnCall("SmoothRoute", [&] { (a->*DaGet(AmSmoothRoute()))(from, to); return 0; },
                   [&](void *) { OAm<void>(0x0057bdc0, &from, &to); return 0; });
            break;
        }
        case 9: {
            const S_vector4 from = AmNear(3);
            if (a->m_waypoints.empty())
                a->m_waypoints.push_back(AmWaypoint());   // (the original reads the first of an empty route)
            EnCall("SmoothRouteTo", [&] { (a->*DaGet(AmSmoothRouteTo()))(from); return 0; },
                   [&](void *) { OAm<void>(0x0057c2b0, &from); return 0; });
            break;
        }
        case 10: {
            const S_vector4 from = AmNear(3), to = AmNear(15);
            EnCall("FixRouteHeights", [&] { (a->*DaGet(AmFixRouteHeights()))(from, to); return 0; },
                   [&](void *) { OAm<void>(0x0057c760, &from, &to); return 0; });
            break;
        }
        case 11:
            EnCall("StartRoute", [&] { return int((a->*DaGet(AmStartRoute()))()); }, [&](void *) { return AmBool(OAm<int>(0x0057ca70)); });
            break;
        case 12:
            EnCall("FollowStep", [&] { (a->*DaGet(AmFollowStep()))(dt); return 0; }, [&](void *) { OAm<void>(0x0057f670, dt); return 0; });
            break;
        case 13:
            if (a->m_waypoints.empty())
                a->m_waypoints.push_back(AmWaypoint());
            EnCall("UseLadder", [&] { (a->*DaGet(AmUseLadder()))(dt); return 0; }, [&](void *) { OAm<void>(0x0057cc50, dt); return 0; });
            break;
        case 14:
            if (a->m_waypoints.empty())
                a->m_waypoints.push_back(AmWaypoint());
            a->m_move->door = static_cast<C_Actor *>(PortActor(0));
            EnCall("PassDoor", [&] { return (a->*DaGet(AmPassDoor()))(dt); }, [&](void *) { return OAm<int>(0x0057cf80, dt); });
            break;
        case 15:
            EnCall("ReachedWaypoint", [&] { return int((a->*DaGet(AmReachedWaypoint()))()); },
                   [&](void *) { return AmBool(OAm<int>(0x0057d5a0)); });
            break;
        case 16:
            if (P(0.1f))
                AmClearWaypoints();   // (the route run out)
            else if (P(0.5f))
                AmBlockerOnTheWay();
            EnCall("FollowType15Route", [&] { (a->*DaGet(AmFollowType15()))(dt); return 0; },
                   [&](void *) { OAm<void>(0x0057da40, dt); return 0; });
            break;
        case 17:
        case 41:
        case 42:
            if (which == 17 && P(0.1f))
                AmClearWaypoints();
            else if (which != 17 || P(0.5f))
                AmSpecialPoints();
            EnCall("FollowRoute", [&] { (a->*DaGet(AmFollowRoute()))(dt); return 0; }, [&](void *) { OAm<void>(0x0057dbd0, dt); return 0; });
            break;
        case 18:
            EnCall("Climb", [&] { (a->*DaGet(AmClimb()))(); return 0; }, [&](void *) { OAm<void>(0x0057db70); return 0; });
            break;
        case 19:
            if (a->m_waypoints.empty())
                a->m_waypoints.push_back(AmWaypoint());
            if (P(0.3f))
                AmMarch();
            EnCall("WalkToWaypoint", [&] { (a->*DaGet(AmWalkToWaypoint()))(); return 0; }, [&](void *) { OAm<void>(0x0057e030); return 0; });
            break;
        case 20:
            EnCall("UpdateMoveStance", [&] { (a->*DaGet(AmUpdateMoveStance()))(); return 0; },
                   [&](void *) { OAm<void>(0x0057ec60); return 0; });
            break;
        case 21:
            EnCall("IsOnPlainPoints", [&] { return int((a->*DaGet(AmIsOnPlainPoints()))()); },
                   [&](void *) { return AmBool(OAm<int>(0x0057edc0)); });
            break;
        case 22:
            EnCall("UpdateAvoidance", [&] { (a->*DaGet(AmUpdateAvoidance()))(); return 0; },
                   [&](void *) { OAm<void>(0x0057f610); return 0; });
            break;
        case 23:
            EnCall("WalkAvoiding", [&] { (a->*DaGet(AmWalkAvoiding()))(); return 0; }, [&](void *) { OAm<void>(0x0057ef30); return 0; });
            break;
        case 24: {
            const int result = I(-11, 4);
            EnCall("EndMove", [&] { (a->*DaGet(AmEndMove()))(result); return 0; }, [&](void *) { OAm<void>(0x0057f6f0, result); return 0; });
            break;
        }
        case 25:
            EnCall("IsMarchBlocked", [&] { return int((a->*DaGet(AmIsMarchBlocked()))()); },
                   [&](void *) { return AmBool(OAm<int>(0x0057fd30)); });
            break;
        case 26:
            EnCall("UpdateCurrentCheckpoint", [&] { (a->*DaGet(AmUpdateCurrentCheckpoint()))(); return 0; },
                   [&](void *) { OAm<void>(0x0057ff10); return 0; });
            break;
        case 27: {
            alignas(16) static S_AIWaypoint wa, wb;
            S_AIWaypoint *x = AmWaypoint(), *y = AmWaypoint();
            wa = *x;
            wb = *y;
            ReleaseWaypoint(x);
            ReleaseWaypoint(y);
            if (P(0.5f)) {
                EnCall("IsNearSegment", [&] { return int((a->*DaGet(AmIsNearSegment()))(wa, wb)); },
                       [&](void *) { return AmBool(OAm<int>(0x005802a0, &wa, &wb)); });
            } else {
                EnCall("NearerPoint", [&] { return DaWord((a->*DaGet(AmNearerPoint()))(wa, wb), true); },
                       [&](void *) { return DaWord(OAm<void *>(0x00580460, &wa, &wb), true); });
            }
            break;
        }
        case 28: {
            if (P(0.5f))
                AmBlockerOnTheWay();
            const bool reached = P(0.5f);
            EnCall("AvoidOthersOnRoute", [&] { (a->*DaGet(AmAvoidOthers()))(reached); return 0; },
                   [&](void *) { OAm<void>(0x00581e30, int(reached)); return 0; });
            break;
        }
        case 29: {
            alignas(16) static S_vector pos;
            pos = XYZ(AmNear(P(0.5f) ? 1.0f : 5.0f));
            const float radius = U(0, 3);
            EnCall("IsHumanWithin", [&] { return int((a->*DaGet(AmIsHumanWithin()))(&pos, radius)); },
                   [&](void *) { return AmBool(OAm<int>(0x0057fe40, &pos, radius)); });
            break;
        }
        case 30: {
            S_vector port = XYZ(AmNear(6)), orig = port;
            EnCall("AdjustToPathHeight",
                   [&] {
                       const int r = int(a->AdjustToPathHeight(&port));
                       return r * 7 + int(port.y * 1000);
                   },
                   [&](void *) {
                       const int r = AmBool(OAm<int>(0x0057fbc0, &orig));
                       return r * 7 + int(orig.y * 1000);
                   });
            break;
        }
        case 31: {
            float pi = 7.0f, pr = 8.0f, oi = 7.0f, orr = 8.0f;
            EnCall("Unknown5C", [&] { return int(a->Unknown5C(&pi, &pr)) * 7 + int(pi * 1000) + int(pr * 100000); },
                   [&](void *) { return AmBool(OAm<int>(0x0057f880, &oi, &orr)) * 7 + int(oi * 1000) + int(orr * 100000); });
            break;
        }
        case 32:
            EnCall("ClearRoute", [&] { (a->*DaGet(AmClearRoute()))(); return 0; }, [&](void *) { OAm<void>(0x0057f7d0); return 0; });
            break;
        case 33:
            EnCall("SkipPassedWaypoint", [&] { (a->*DaGet(AmSkipPassed()))(); return 0; },
                   [&](void *) { OAm<void>(0x0057b1b0); return 0; });
            break;
        case 34:
        case 35:
            AmRandomDriver();
            if (which == 35) {
                // (Driving on, nothing new: the car standing (the stuck time's 3 s) or not getting nearer (5 s).)
                C_AIDriver *d = a->m_driver;
                d->m_flags = C_AIDriver::FLAG_DRIVING;
                d->m_moving = true;
                if (P(0.5f))
                    d->m_lastPos = WorldPos4Of(reinterpret_cast<C_Car *>(g_asCarPort)->GetFrame());
                d->m_stuckTime = 3000 - I(0, 100);
                d->m_noProgressTime = 5000 - I(0, 100);
                d->m_nearestDist2 = U(0, 10);
            }
            EnCall("UpdateDriving", [&] { return (a->*DaGet(AmUpdateDriving()))(dt); }, [&](void *) { return OAm<int>(0x00578770, dt); });
            break;
        case 36: {
            AmRandomDriver();
            C_Car *car = reinterpret_cast<C_Car *>(g_asCarPort);
            EnCall("OnDriveArrived", [&] { return (a->*DaGet(AmOnDriveArrived()))(car); },
                   [&](void *) { return OAm<int>(0x005796d0, static_cast<void *>(g_asCarOrig)); });
            break;
        }
        case 37: {
            AmRandomDriver();
            C_Car *car = reinterpret_cast<C_Car *>(g_asCarPort);
            alignas(16) static S_vector v[5];
            const S_vector at = car->GetFrame()->GetWorldPos();
            v[0] = at + S_vector(U(-3, 3), 0, U(-3, 3));
            v[1] = S_vector(U(-1, 1), U(-0.2f, 0.2f), U(-1, 1));
            v[2] = at + S_vector(U(-6, 6), 0, U(-6, 6));
            v[3] = S_vector(U(-1, 1), 0, U(-1, 1)).Normalized();
            v[4] = at + S_vector(U(-8, 8), 0, U(-8, 8));
            const bool last = P(0.5f), reverse = P(0.5f);
            EnCall(reverse ? "SteerReverse" : "SteerForward",
                   [&] {
                       (a->*(reverse ? DaGet(AmSteerReverse()) : DaGet(AmSteerForward())))(car, v[0], v[1], v[2], v[3], last, v[4]);
                       return 0;
                   },
                   [&](void *) {
                       OAm<void>(reverse ? 0x00579d50 : 0x00579760, static_cast<void *>(g_asCarOrig), &v[0], &v[1], &v[2], &v[3],
                                 int(last), &v[4]);
                       return 0;
                   });
            break;
        }
        case 38: {
            const int which2 = I(0, 4);
            static const char *const names[] = {"IsNearSelectedSoldier", "MustSlowDown", "HasAvoidance", "CanAvoid", "IsAwayFromOthers"};
            static const uint32_t at[] = {0x0057ee30, 0x0057fda0, 0x0057eec0, 0x0057ef00, 0x0057ee00};
            EnCall(names[which2],
                   [&] {
                       switch (which2) {
                       case 0: return int((a->*DaGet(AmIsNearSelected()))());
                       case 1: return int((a->*DaGet(AmMustSlowDown()))());
                       case 2: return int((a->*DaGet(AmHasAvoidance()))());
                       case 3: return int((a->*DaGet(AmCanAvoid()))());
                       default: return int((a->*DaGet(AmIsAwayFromOthers()))());
                       }
                   },
                   [&](void *) { return AmBool(OAm<int>(at[which2])); });
            break;
        }
        case 40: {
            // The free functions (fastcall: the position in ECX; the actors' vector or the first waypoint in EDX).
            const S_vector pos = XYZ(AmNear(P(0.5f) ? 1.0f : 4.0f));
            if (P(0.5f)) {
                std::vector<C_Actor *> actors;
                for (int n = I(0, 3); n > 0; --n)
                    actors.push_back(P(0.5f) ? static_cast<C_Actor *>(g_h.human) : static_cast<C_Actor *>(PortOther()));
                std::vector<uint32_t> words;
                for (C_Actor *x : actors)
                    words.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ToOrig(x))));
                alignas(16) static uint8_t vec[16];
                EnCall("FindActorNear", [&] { return DaWord(FindActorNear(pos, actors), false); },
                       [&](void *) {
                           MsvcVectorPut(vec, words);
                           typedef void *(__fastcall * T_Find)(const S_vector *, const void *);
                           return DaWord(OriginalAt<T_Find>(0x00581440)(&pos, vec), true);
                       });
            } else {
                alignas(16) static S_AIWaypoint wa, wb;
                S_AIWaypoint *x = AmWaypoint(), *y = AmWaypoint();
                wa = *x;
                wb = *y;
                ReleaseWaypoint(x);
                ReleaseWaypoint(y);
                EnCall("IsPosNearSegment", [&] { return int(IsPosNearSegment(pos, wa, wb)); },
                       [&](void *) {
                           typedef int(__fastcall * T_Near)(const S_vector *, const S_AIWaypoint *, const S_AIWaypoint *);
                           return AmBool(OriginalAt<T_Near>(0x00580120)(&pos, &wa, &wb));
                       });
            }
            break;
        }
        default: {
            const std::vector<uint16_t> path = AmPath();
            alignas(16) static uint8_t orig[16];
            std::vector<uint32_t> words(path.size() / 2 + 1, 0);
            EnCall("SetRoutePoints", [&] { (a->*DaGet(AmSetRoutePoints()))(path); return 0; },
                   [&](void *) {
                       AmPutPath(reinterpret_cast<uint32_t *>(orig), path);
                       OAm<void>(0x0057af70, static_cast<void *>(orig));
                       return 0;
                   });
            break;
        }
        }
    }
}

void TestAiMove()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    SetUpFireOrder();
    SetUpEnemy();
    SetUpAiMoveFakes();
    // (The waypoints' pos w: the original's corner cutting and path points leave stale words there.)
    g_waypointW = false;
    // (The human's AI memory, shared by both sides: its route-changed word compared.)
    g_sharedWords.push_back({reinterpret_cast<uint32_t *>(g_aiMemoryBytes + (offsetof(C_HumanAIMemory, routeChanged) & ~size_t(3))),
                             "aiMemory.routeChanged"});
    TestAiMoveCalls();
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
}
}  // namespace
