// Part 43 of the human harness (HUMAN_ENEMY builds, build_enemy.bat; included by human_test.cpp after
// aicombat_test.inc, whose fakes and helpers it uses): the enemy AI (game/enemy_controller.cpp, enemy_tasks.cpp,
// enemy_combat.cpp, enemy_gunner.cpp) against the original's, on the derived AI controllers' common harness
// (derived_ai.inc).
//
// The port's enemy controller (a test class: ExecuteCommand logged) is the AI pair's port side; the original's is the
// AI constructor's object with a copy of its vtable (0x00811730, 0x1E0 bytes). Its own fields (+0x140 .. +0x1E0: the
// same offsets on both sides but for the MSVC6 vectors, 16 bytes there) are converted and compared around each call;
// the search task's event and the guard's gun share +0x148 in the original (two members in the port: the one set is
// converted; an event the call allocated is compared by its fields).
//
// Faked on both sides and logged: the walking network's GetActor, FindActorPoint, FindNearestPoint, GetNeighbourCover,
// GetReachable, GetReachableCopy, GetReachableCover and PickCoverPoint (it also drops the candidates the test names,
// on each side's vector); the human's FindItemByRecordType, RequestMoveItem and RequestTransferItem; the events'
// GetAttacker, GetStrongestStimulus and IsAwayFromNearbyActors; the body's GetSector, GetUnknown6E0, RemoveCollision
// and RestoreCollision; the collision world's RayTest and RayTestDynamic; the seat weapons' rounds, origin and occupant
// (the seat car and part 23's actors as the post's guns). The events' PopFront, GetNearbyActor and an event's
// SaveGame / LoadGame are the port's copies here (human_events.cpp is not linked); the original runs its own.
//
// Part 43a: enemy_controller.cpp (the life cycle, the events, the task's choice, the weapons, the firing, the grenade,
// the line of fire, the saved games); 43b: enemy_tasks.cpp (the guard, the search, morale broken); 43c:
// enemy_combat.cpp (the combat task, the weapon's range, stances, the points to fight from); 43d: enemy_gunner.cpp
// (the gunner, the seat weapon's targets and aim, the post's guns). HUMAN_ENEMY_PART picks the parts to run.
//
// The original's flaws the test steers clear of (the port keeps them, or its `Fix:` guards them): a task the guard's
// start leaves out read by UpdateFiring; a target gone (forgotten by UpdateTimers) measured to in the combat's move and
// approach and the launcher's state; a stimulus source taken for a human by PickGunnerTarget; network games' IsFriendOf
// taking any actor for a human; FindApproachPoint's list run below empty; CanFireMoving's missing hands item; a missing
// body's events in StartSearch; a missing gun in the guard's walk to it; IsOutOfGunReach's missing vehicle;
// PickGunnerTarget's division by a stimulus's time left of 0; a reload with empty hands (part 36's).
#include "derived_ai.inc"

namespace {
// The fakes' answers.
struct S_EnAnswers {
    std::vector<uint16_t> reachable;                    // GetReachable, GetReachableCopy, GetReachableCover
    bool reachableOk = true;
    std::vector<uint16_t> cover;                        // GetNeighbourCover
    int pick = -1;                                      // PickCoverPoint
    uint32_t pickDrop = 0;                              // ... and the candidates it drops (bits by point)
    int actorPoint = -1;                                // FindActorPoint
    int pointActor[8] = {};                             // GetActor by point: -1 none, else part 23's kind
    int nearestPoint = -1;                              // FindNearestPoint
    bool recordItem = false;                            // FindItemByRecordType
    bool requestOk = true;                              // RequestMoveItem, RequestTransferItem
    int strongest = -1;                                 // GetStrongestStimulus: an event's index
    C_Actor *attacker = nullptr;                        // GetAttacker (the port's)
    bool away = true;                                   // IsAwayFromNearbyActors
    bool outdoors = true;                               // GetSector: the scene's primary sector
    int spheres = 0;                                    // GetUnknown6E0's count
    bool rayHit = false;                                // RayTest
    int dynamicHit = -1;                                // RayTestDynamic: -1 none, else part 23's kind (13: a volume
                                                        // without an actor)
    int rounds = 5;                                     // GetSeatWeaponRounds
    S_vector origin;                                    // GetSeatWeaponOrigin (part 23's actors: by kind, +0.5 m each)
    int occupant = -1;                                  // GetSeatOccupant: -1 none, else 0 the other human, 1 the third
    int behaviours[3] = {};                             // the definitions' behaviours (part 21's three)
} g_en;
alignas(16) S_vector4 g_enSphereStore[5];                // [0]: a sentinel no read reaches (a wide sphere)
S_vector4 *const g_enSpheres = g_enSphereStore + 1;
alignas(16) uint8_t g_enSectorOther[0x20];
alignas(16) uint8_t g_enVolume[0x80];

int EnPointIndex(const void *cp)
{
    for (int k = 0; k < 8; ++k)
        if (cp == g_aiPoints[k])
            return k;
    return cp ? 99 : -1;
}
C_Actor *EnPointActor(int index)
{
    if (index < 0 || index >= 8 || g_en.pointActor[index] < 0)
        return nullptr;
    return static_cast<C_Actor *>(PortActor(g_en.pointActor[index]));
}
// Each side drops the candidates the test names.
std::vector<uint16_t> EnPicked(const std::vector<uint16_t> &in)
{
    std::vector<uint16_t> out;
    for (uint16_t p : in)
        if (!(p < 32 && (g_en.pickDrop >> p) & 1))
            out.push_back(p);
    return out;
}
std::vector<double> EnPoints(const std::vector<uint16_t> &points)
{
    return std::vector<double>(points.begin(), points.end());
}
void LogPoints(const void *self, const char *what, const std::vector<uint16_t> &points, std::initializer_list<double> more)
{
    std::vector<double> nums(more);
    nums.push_back(-1000);
    for (uint16_t p : points)
        nums.push_back(p);
    S_Call c{NameOf(self) + "." + what, nums};
    g_calls.push_back(c);
}
// An MSVC6 vector of 16-bit points at `at` (allocator, first, last, end; its own malloc'd buffer).
void EnVectorPut(uint8_t *at, const std::vector<uint16_t> &items)
{
    uint32_t w[4] = {0, 0, 0, 0};
    if (!items.empty()) {
        const size_t spare = size_t(I(0, 2));
        auto *data = static_cast<uint16_t *>(std::malloc((items.size() + spare) * 2));
        std::memcpy(data, items.data(), items.size() * 2);
        w[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        w[2] = w[1] + 2 * static_cast<uint32_t>(items.size());
        w[3] = w[2] + 2 * static_cast<uint32_t>(spare);
    }
    std::memcpy(at, w, 16);
}
std::vector<uint16_t> EnVectorGet(const uint8_t *at)
{
    uint32_t w[4];
    std::memcpy(w, at, 16);
    if (!w[1])
        return {};
    const auto *first = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(w[1]));
    return std::vector<uint16_t>(first, first + (w[2] - w[1]) / 2);
}
// The answer into an original's vector (the call's local: empty, or the post's list cleared).
// (Appended to what the vector holds, as the network's flood fill does.)
void EnVectorAnswer(uint32_t *out, const std::vector<uint16_t> &items)
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
void EnAppend(std::vector<uint16_t> *out, const std::vector<uint16_t> &items) { out->insert(out->end(), items.begin(), items.end()); }
}  // namespace

// ---- the port's side of the fakes
C_Actor *C_CheckpointNetwork::GetActor(int index) const
{
    Log(this, "CheckpointsGetActor", {double(index)});
    return EnPointActor(index);
}
int C_CheckpointNetwork::FindActorPoint(const C_Actor *actor) const
{
    Log(this, ("CheckpointsFindActorPoint " + NameOf(actor)).c_str());
    return g_en.actorPoint;
}
int C_CheckpointNetwork::FindNearestPoint(S_vector pos, float maxDist, float minDist) const
{
    Log(this, "FindNearestPoint", {pos.x, pos.y, pos.z, maxDist, minDist});
    return g_en.nearestPoint;
}
bool C_CheckpointNetwork::GetNeighbourCover(int from, float radius, std::vector<uint16_t> *out)
{
    Log(this, "GetNeighbourCover", {double(from), radius, double(out->size())});
    EnAppend(out, g_en.cover);
    return true;
}
bool C_CheckpointNetwork::GetReachable(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out)
{
    Log(this, "GetReachable", {double(EnPointIndex(from)), radius, double(out->size())});
    EnAppend(out, g_en.reachable);
    return g_en.reachableOk;
}
bool C_CheckpointNetwork::GetReachableCopy(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out)
{
    Log(this, "GetReachableCopy", {double(EnPointIndex(from)), radius, double(out->size())});
    EnAppend(out, g_en.reachable);
    return g_en.reachableOk;
}
bool C_CheckpointNetwork::GetReachableCover(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out)
{
    Log(this, "GetReachableCover", {double(EnPointIndex(from)), radius, double(out->size())});
    EnAppend(out, g_en.reachable);
    return g_en.reachableOk;
}
int C_CheckpointNetwork::PickCoverPoint(const S_vector &self, const S_vector &enemy, std::vector<uint16_t> *candidates,
                                        uint32_t flags)
{
    LogPoints(this, "PickCoverPoint", *candidates, {self.x, self.y, self.z, enemy.x, enemy.y, enemy.z, double(flags)});
    *candidates = EnPicked(*candidates);
    return g_en.pick;
}
C_Item *C_Human::FindItemByRecordType(int recordType) const
{
    Log(this, "FindItemByRecordType", {double(recordType)});
    return g_en.recordItem ? reinterpret_cast<C_Item *>(g_wornItems[5]) : nullptr;
}
bool C_Human::RequestMoveItem(C_Item *item, C_Actor *other, int fromContainer, int toContainer, int slot)
{
    Log(this, ("RequestMoveItem " + NameOf(item) + " " + NameOf(other)).c_str(), {double(fromContainer), double(toContainer), double(slot)});
    return g_en.requestOk;
}
bool C_Human::RequestTransferItem(C_Item *item, int arg, C_Human *other, C_Item *otherItem, int container)
{
    Log(this, ("RequestTransferItem " + NameOf(item) + " " + NameOf(other) + " " + NameOf(otherItem)).c_str(),
        {double(arg), double(container)});
    return g_en.requestOk;
}
C_Actor *C_HumanEvents::GetAttacker() const
{
    Log(this, "GetAttacker");
    return g_en.attacker;
}
S_HumanEvent *C_HumanEvents::GetStrongestStimulus(const S_vector &pos) const
{
    Log(this, "GetStrongestStimulus", {pos.x, pos.y, pos.z});
    return g_en.strongest < 0 ? nullptr : &g_daEv[g_en.strongest];
}
bool C_HumanEvents::IsAwayFromNearbyActors(const S_vector &pos) const
{
    Log(this, "IsAwayFromNearbyActors", {pos.x, pos.y, pos.z});
    return g_en.away;
}
// (human_events.cpp's, copied: the original runs its own.)
C_Actor *C_HumanEvents::GetNearbyActor(int index) const
{
    if (index < 0 || static_cast<size_t>(index) >= m_nearbyActors.size())
        return nullptr;
    const int id = m_nearbyActors[static_cast<size_t>(index)];
    if (id == -1)
        return nullptr;
    return g_actorsModule.GetActorById(id);
}
S_HumanEvent *C_HumanEvents::PopFront()
{
    if (m_pending.empty())
        return nullptr;
    S_HumanEvent *event = m_pending.front();
    m_pending.pop_front();
    return event;
}
bool S_HumanEvent::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(0x1F8);
    chunk.Write(type);
    chunk.Write(age);
    chunk.Write(timeLeft);
    chunk.Write(&pos, 12);
    chunk.Write(unknown30);
    chunk.Ascend();
    if (actorId >= 0)
        chunk.WriteChunk(0x1F5, actorId);
    if (sourceId >= 0)
        chunk.WriteChunk(0x1F6, sourceId);
    return true;
}
bool S_HumanEvent::LoadGame(C_chunk &chunk)
{
    type = 0;
    age = 0;
    timeLeft = 0;
    actorId = -1;
    sourceId = -1;
    pos.x = pos.y = pos.z = 0.0f;
    unknown30 = 0;
    bool loaded = false;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case 500:
            chunk.Read(&pos, 12);
            chunk.Read(timeLeft);
            chunk.Read(type);
            chunk.Read(age);
            age = 0;
            unknown30 = 0;
            loaded = true;
            break;
        case 0x1F5:
            actorId = -1;
            chunk.Read(actorId);
            break;
        case 0x1F6:
            sourceId = -1;
            chunk.Read(sourceId);
            break;
        case 0x1F7:
            chunk.Read(&pos, 12);
            chunk.Read(timeLeft);
            chunk.Read(type);
            chunk.Read(age);
            unknown30 = 0;
            loaded = true;
            break;
        case 0x1F8:
            chunk.Read(type);
            chunk.Read(age);
            chunk.Read(timeLeft);
            chunk.Read(&pos, 12);
            chunk.Read(unknown30);
            loaded = true;
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    return loaded;
}
namespace {
I3D_sector *EnSector()
{
    return g_en.outdoors ? GetGame()->GetScene()->GetPrimarySector() : reinterpret_cast<I3D_sector *>(g_enSectorOther);
}
}  // namespace
I3D_sector *S_HumanBody::GetSector() const
{
    Log(nullptr, "BodyGetSector");
    return EnSector();
}
I3D_frame *S_HumanBody::GetUnknown6E0(int *out) const
{
    Log(nullptr, "BodyGetUnknown6E0");
    *out = g_en.spheres;
    return reinterpret_cast<I3D_frame *>(g_enSpheres);
}
void S_HumanBody::RemoveCollision(bool unknown) { Log(nullptr, "BodyRemoveCollision", {double(unknown)}); }
void S_HumanBody::RestoreCollision(bool unknown) { Log(nullptr, "BodyRestoreCollision", {double(unknown)}); }

namespace {
void *EnRayAnswer(const S_vector *from, const S_vector *dir, const S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    Log(nullptr, "RayTest", {from->x, from->y, from->z, dir->x, dir->y, dir->z, double(hitNormal != nullptr), double(types), double(flags)});
    return g_en.rayHit ? g_enVolume : nullptr;
}
void *EnDynamicAnswer(const S_vector *from, const S_vector *dir, const S_vector *hitPos, const S_vector *hitNormal, uint32_t types,
                      uint32_t flags, bool original)
{
    Log(nullptr, "RayTestDynamic", {from->x, from->y, from->z, dir->x, dir->y, dir->z, double(hitPos != nullptr),
                                    double(hitNormal != nullptr), double(types), double(flags)});
    if (g_en.dynamicHit < 0)
        return nullptr;
    // The volume's actor (+0x48 on both sides): each side's.
    void *actor = g_en.dynamicHit >= 13 ? nullptr : original ? OrigActor(g_en.dynamicHit) : PortActor(g_en.dynamicHit);
    std::memcpy(g_enVolume + 0x48, &actor, 4);
    return g_enVolume;
}
}  // namespace
extern "C" S_CollisionVolume *__attribute__((thiscall))
__wrap__ZNK16C_CollisionWorld7RayTestERK8S_vectorS2_PS0_S3_jj(const C_CollisionWorld *, const S_vector &from, const S_vector &dir,
                                                              S_vector *, S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    return static_cast<S_CollisionVolume *>(EnRayAnswer(&from, &dir, hitNormal, types, flags));
}
extern "C" S_CollisionVolume *__attribute__((thiscall))
__wrap__ZNK16C_CollisionWorld14RayTestDynamicERK8S_vectorS2_PS0_S3_jj(const C_CollisionWorld *, const S_vector &from, const S_vector &dir,
                                                                      S_vector *hitPos, S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    return static_cast<S_CollisionVolume *>(EnDynamicAnswer(&from, &dir, hitPos, hitNormal, types, flags, false));
}

namespace {
// ---- the original's side
// (The points as 16-bit words: the original's pushes leave the high word stale.)
void *AC EN_GetActor(void *self, int index)
{
    index &= 0xFFFF;
    Log(self, "CheckpointsGetActor", {double(index)});
    C_Actor *a = EnPointActor(index);
    return a ? ToOrig(a) : nullptr;
}
int AC EN_FindActorPoint(void *self, const void *actor)
{
    Log(self, ("CheckpointsFindActorPoint " + NameOf(actor)).c_str());
    return g_en.actorPoint;
}
int AC EN_FindNearestPoint(void *self, float x, float y, float z, float maxDist, float minDist)
{
    Log(self, "FindNearestPoint", {x, y, z, maxDist, minDist});
    return g_en.nearestPoint;
}
size_t EnOrigSize(const uint32_t *out) { return out[1] ? (out[2] - out[1]) / 2 : 0; }
int AC EN_GetNeighbourCover(void *self, int from, float radius, uint32_t *out)
{
    from &= 0xFFFF;
    Log(self, "GetNeighbourCover", {double(from), radius, double(EnOrigSize(out))});
    EnVectorAnswer(out, g_en.cover);
    return 1;
}
int AC EN_GetReachable(void *self, const void *from, float radius, uint32_t *out)
{
    Log(self, "GetReachable", {double(EnPointIndex(from)), radius, double(EnOrigSize(out))});
    EnVectorAnswer(out, g_en.reachable);
    return g_en.reachableOk;
}
int AC EN_GetReachableCopy(void *self, const void *from, float radius, uint32_t *out)
{
    Log(self, "GetReachableCopy", {double(EnPointIndex(from)), radius, double(EnOrigSize(out))});
    EnVectorAnswer(out, g_en.reachable);
    return g_en.reachableOk;
}
int AC EN_GetReachableCover(void *self, const void *from, float radius, uint32_t *out)
{
    Log(self, "GetReachableCover", {double(EnPointIndex(from)), radius, double(EnOrigSize(out))});
    EnVectorAnswer(out, g_en.reachable);
    return g_en.reachableOk;
}
// (Its candidates in place: the kept ones moved down, the last pointer set back.)
int AC EN_PickCoverPoint(void *self, const S_vector *me, const S_vector *enemy, uint32_t *candidates, uint32_t flags)
{
    std::vector<uint16_t> in;
    if (candidates[1]) {
        const auto *first = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(candidates[1]));
        in.assign(first, first + (candidates[2] - candidates[1]) / 2);
    }
    LogPoints(self, "PickCoverPoint", in, {me->x, me->y, me->z, enemy->x, enemy->y, enemy->z, double(flags)});
    const std::vector<uint16_t> out = EnPicked(in);
    if (candidates[1]) {
        auto *first = reinterpret_cast<uint16_t *>(static_cast<uintptr_t>(candidates[1]));
        std::copy(out.begin(), out.end(), first);
        candidates[2] = candidates[1] + 2 * static_cast<uint32_t>(out.size());
    }
    return g_en.pick;
}
void *AC EN_FindItemByRecordType(void *self, int recordType)
{
    Log(self, "FindItemByRecordType", {double(recordType)});
    return g_en.recordItem ? static_cast<void *>(g_wornItems[5]) : nullptr;
}
int AC EN_RequestMoveItem(void *self, void *item, void *other, int fromContainer, int toContainer, int slot)
{
    Log(self, ("RequestMoveItem " + NameOf(item) + " " + NameOf(other)).c_str(), {double(fromContainer), double(toContainer), double(slot)});
    return g_en.requestOk;
}
int AC EN_RequestTransferItem(void *self, void *item, int arg, void *other, void *otherItem, int container)
{
    Log(self, ("RequestTransferItem " + NameOf(item) + " " + NameOf(other) + " " + NameOf(otherItem)).c_str(),
        {double(arg), double(container)});
    return g_en.requestOk;
}
void *AC EN_GetAttacker(void *self)
{
    Log(self, "GetAttacker");
    return g_en.attacker ? ToOrig(g_en.attacker) : nullptr;
}
void *AC EN_GetStrongestStimulus(void *self, const S_vector *pos)
{
    Log(self, "GetStrongestStimulus", {pos->x, pos->y, pos->z});
    return g_en.strongest < 0 ? nullptr : &g_daEv[g_en.strongest];
}
int AC EN_IsAwayFromNearbyActors(void *self, const S_vector *pos)
{
    Log(self, "IsAwayFromNearbyActors", {pos->x, pos->y, pos->z});
    return g_en.away;
}
void *AC EN_GetSector(void *)
{
    Log(nullptr, "BodyGetSector");
    return EnSector();
}
void *AC EN_GetUnknown6E0(void *, int *out)
{
    Log(nullptr, "BodyGetUnknown6E0");
    *out = g_en.spheres;
    return g_enSpheres;
}
void AC EN_RemoveCollision(void *, int unknown) { Log(nullptr, "BodyRemoveCollision", {double(unknown & 0xFF)}); }
void AC EN_RestoreCollision(void *, int unknown) { Log(nullptr, "BodyRestoreCollision", {double(unknown & 0xFF)}); }
void *AC EN_RayTest(void *, const S_vector *from, const S_vector *dir, S_vector *, S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    return EnRayAnswer(from, dir, hitNormal, types, flags);
}
void *AC EN_RayTestDynamic(void *, const S_vector *from, const S_vector *dir, S_vector *hitPos, S_vector *hitNormal, uint32_t types,
                           uint32_t flags)
{
    return EnDynamicAnswer(from, dir, hitPos, hitNormal, types, flags, true);
}

// The seat weapons (the seat car's and part 23's actors' slots: the port's and the original's 0x9C, 0xA8 with its
// hidden return pointer, 0x78).
S_vector EnOrigin(const void *self)
{
    for (int k = 0; k < ACTOR_KIND_COUNT; ++k)
        if (self == g_actPort[k] || self == g_actOrig[k])
            return g_en.origin + S_vector(0.5f * float(k), 0, 0.25f * float(k));
    return g_en.origin;
}
void *EnOccupant(bool original)
{
    if (g_en.occupant < 0)
        return nullptr;
    if (g_en.occupant == 0)
        return original ? static_cast<void *>(g_origOther) : static_cast<void *>(g_portOther);
    return original ? static_cast<void *>(g_origThird) : static_cast<void *>(g_portThird);
}
struct S_EnSeatFake {
    virtual int Rounds(int seat);
    virtual S_vector Origin(int seat);
    virtual C_Actor *Occupant(int seat);
};
int S_EnSeatFake::Rounds(int seat)
{
    Log(this, "GetSeatWeaponRounds", {double(seat)});
    return g_en.rounds;
}
S_vector S_EnSeatFake::Origin(int seat)
{
    Log(this, "GetSeatWeaponOrigin", {double(seat)});
    return EnOrigin(this);
}
C_Actor *S_EnSeatFake::Occupant(int seat)
{
    Log(this, "GetSeatOccupant", {double(seat)});
    return static_cast<C_Actor *>(EnOccupant(false));
}
int AC EN_SeatRounds(void *self, int seat)
{
    Log(self, "GetSeatWeaponRounds", {double(seat)});
    return g_en.rounds;
}
float *AC EN_SeatOrigin(void *self, float *out, int seat)
{
    Log(self, "GetSeatWeaponOrigin", {double(seat)});
    const S_vector o = EnOrigin(self);
    out[0] = o.x;
    out[1] = o.y;
    out[2] = o.z;
    return out;
}
void *AC EN_SeatOccupant(void *self, int seat)
{
    Log(self, "GetSeatOccupant", {double(seat)});
    return EnOccupant(true);
}
// The weapons' capacity (now and then their amount: full).
float g_enMaxAmount = 30.0f;
float AC EN_GetMaxAmount(void *self)
{
    Log(self, "WeaponGetMaxAmount");
    return g_enMaxAmount;
}
float AC EN_MuzzleVelocity(void *self)
{
    float v;
    std::memcpy(&v, static_cast<uint8_t *>(self) + 0x38, 4);
    return v;
}
// The definitions' behaviours (part 21's three definitions: each its own).
int EnBehaviourOf(const void *def)
{
    for (int t = 0; t < 3; ++t)
        if (def == g_wornDefs[t])
            return g_en.behaviours[t];
    return g_behaviour;
}

// ---- the enemy's private members
typedef C_EnemyController S_En;
DA_ROB(EnSetupAggressive, bool (S_En::*)(), &S_En::SetupAggressive)
DA_ROB(EnSetupDefensive, bool (S_En::*)(), &S_En::SetupDefensive)
DA_ROB(EnSetupZombie, bool (S_En::*)(), &S_En::SetupZombie)
DA_ROB(EnTakeOver, bool (S_En::*)(C_HumanController *), &S_En::TakeOver)
DA_ROB(EnFindHomeNeighbours, void (S_En::*)(), &S_En::FindHomeNeighbours)
DA_ROB(EnFindHomeCover, void (S_En::*)(), &S_En::FindHomeCover)
DA_ROB(EnFindGuns, void (S_En::*)(), &S_En::FindGuns)
DA_ROB(EnReturnToScript, void (S_En::*)(), &S_En::ReturnToScript)
DA_ROB(EnCheckThreat, void (S_En::*)(), &S_En::CheckThreat)
DA_ROB(EnUpdateTimers, void (S_En::*)(int), &S_En::UpdateTimers)
DA_ROB(EnIsLowOnAmmo, bool (S_En::*)(), &S_En::IsLowOnAmmo)
DA_ROB(EnUpdateFiring, void (S_En::*)(int), &S_En::UpdateFiring)
DA_ROB(EnHandleTaskEvent, S_HumanEvent *(S_En::*)(S_HumanEvent *), &S_En::HandleTaskEvent)
DA_ROB(EnHandleCommonEvent, S_HumanEvent *(S_En::*)(S_HumanEvent *), &S_En::HandleCommonEvent)
DA_ROB(EnUpdateMode, void (S_En::*)(), &S_En::UpdateMode)
DA_ROB(EnChooseTask, void (S_En::*)(), &S_En::ChooseTask)
DA_ROB(EnChangeMorale, void (S_En::*)(int), &S_En::ChangeMorale)
DA_ROB(EnHasSeatWeapon, bool (S_En::*)(), &S_En::HasSeatWeapon)
DA_ROB(EnUpdateTask, S_AITask *(S_En::*)(int), &S_En::UpdateTask)
DA_ROB(EnRunTask, void (S_En::*)(int), &S_En::RunTask)
DA_ROB(EnStartSearch, void (S_En::*)(), &S_En::StartSearch)
DA_ROB(EnStartCombat, void (S_En::*)(), &S_En::StartCombat)
DA_ROB(EnStartGunner, void (S_En::*)(), &S_En::StartGunner)
DA_ROB(EnSetEnemyStance, void (S_En::*)(int), &S_En::SetEnemyStance)
DA_ROB(EnHasWeapon, bool (S_En::*)(), &S_En::HasWeapon)
DA_ROB(EnDrawWeapon, bool (S_En::*)(), &S_En::DrawWeapon)
DA_ROB(EnTakeFirearm, bool (S_En::*)(), &S_En::TakeFirearm)
DA_ROB(EnHasLauncherInHands, bool (S_En::*)(), &S_En::HasLauncherInHands)
DA_ROB(EnTakeLauncher, bool (S_En::*)(), &S_En::TakeLauncher)
DA_ROB(EnHasLauncher, bool (S_En::*)(), &S_En::HasLauncher)
DA_ROB(EnNearestTargetOnFoot, C_Human *(S_En::*)(), &S_En::NearestTargetOnFoot)
DA_ROB(EnGetFireWait, int (S_En::*)(), &S_En::GetFireWait)
DA_ROB(EnNeedsReload, bool (S_En::*)(), &S_En::NeedsReload)
DA_ROB(EnSeesEnemy, bool (S_En::*)(const C_Human *) const, &S_En::SeesEnemy)
DA_ROB(EnTestSightNow, void (S_En::*)(const C_Human *), &S_En::TestSightNow)
DA_ROB(EnSeesEnemies, bool (S_En::*)() const, &S_En::SeesEnemies)
DA_ROB(EnIsFightingOn, bool (S_En::*)() const, &S_En::IsFightingOn)
DA_ROB(EnNearestEnemy, C_Human *(S_En::*)(), &S_En::NearestEnemy)
DA_ROB(EnLookAtNearestEnemy, bool (S_En::*)(), &S_En::LookAtNearestEnemy)
DA_ROB(EnDropHandsItem, void (S_En::*)(), &S_En::DropHandsItem)
DA_ROB(EnAttack, bool (S_En::*)(int), &S_En::Attack)
DA_ROB(EnSetBurstTimes, void (S_En::*)(C_Actor *), &S_En::SetBurstTimes)

// enemy_tasks.cpp's
DA_ROB(EnIsNotClimbing, bool (S_En::*)(), &S_En::IsNotClimbing)
DA_ROB(EnIsOutdoors, bool (S_En::*)(), &S_En::IsOutdoors)
DA_ROB(EnIsLightWeapon, bool (S_En::*)(), &S_En::IsLightWeapon)
DA_ROB(EnGuardEvent, void (S_En::*)(S_HumanEvent *), &S_En::GuardEvent)
DA_ROB(EnUpdateGuard, void (S_En::*)(int), &S_En::UpdateGuard)
DA_ROB(EnSearchEvent, void (S_En::*)(S_HumanEvent *), &S_En::SearchEvent)
DA_ROB(EnIsSameSource, bool (S_En::*)(const S_HumanEvent *), &S_En::IsSameSource)
DA_ROB(EnSearchSameSource, void (S_En::*)(S_HumanEvent *), &S_En::SearchSameSource)
DA_ROB(EnSetSearchEvent, void (S_En::*)(S_HumanEvent *), &S_En::SetSearchEvent)
DA_ROB(EnSearchStrongerEvent, void (S_En::*)(S_HumanEvent *), &S_En::SearchStrongerEvent)
DA_ROB(EnSearchFriendHeard, void (S_En::*)(S_HumanEvent *), &S_En::SearchFriendHeard)
DA_ROB(EnUpdateSearch, void (S_En::*)(int), &S_En::UpdateSearch)
DA_ROB(EnFindSearchPoint, int (S_En::*)(), &S_En::FindSearchPoint)
DA_ROB(EnFindSearchAround, int (S_En::*)(), &S_En::FindSearchAround)
DA_ROB(EnWalkCarefully, void (S_En::*)(), &S_En::WalkCarefully)
DA_ROB(EnLookTowardIfAside, bool (S_En::*)(const S_vector4 &), &S_En::LookTowardIfAside)
DA_ROB(EnLookAroundToward, void (S_En::*)(const S_vector4 &), &S_En::LookAroundToward)
DA_ROB(EnFindCoverToward, int (S_En::*)(const S_vector4 &), &S_En::FindCoverToward)
DA_ROB(EnBrokenEvent, void (S_En::*)(S_HumanEvent *), &S_En::BrokenEvent)
DA_ROB(EnUpdateBroken, void (S_En::*)(int), &S_En::UpdateBroken)
DA_ROB(EnStartGuard, void (S_En::*)(), &S_En::StartGuard)
DA_ROB(EnStartBroken, void (S_En::*)(), &S_En::StartBroken)
DA_ROB(EnRunForCover, void (S_En::*)(), &S_En::RunForCover)
DA_ROB(EnNearestPoint, int (S_En::*)(const S_vector4 &, const std::vector<uint16_t> &, float *), &S_En::NearestPoint)

// enemy_combat.cpp's
DA_ROB(EnCombatEvent, void (S_En::*)(S_HumanEvent *), &S_En::CombatEvent)
DA_ROB(EnUpdateCombat, void (S_En::*)(int), &S_En::UpdateCombat)
DA_ROB(EnCompareRange, int (S_En::*)(), &S_En::CompareRange)
DA_ROB(EnFindCombatPoint, int (S_En::*)(), &S_En::FindCombatPoint)
DA_ROB(EnGetWeaponRange, void (S_En::*)(float *, float *), &S_En::GetWeaponRange)
DA_ROB(EnGetMinDistance, float (S_En::*)(), &S_En::GetMinDistance)
DA_ROB(EnGetMaxDistance, float (S_En::*)(), &S_En::GetMaxDistance)
DA_ROB(EnIsInWeaponRange, bool (S_En::*)(), &S_En::IsInWeaponRange)
DA_ROB(EnCanFireMoving, bool (S_En::*)(), &S_En::CanFireMoving)
DA_ROB(EnHasLineTo, bool (S_En::*)(const S_vector4 &), &S_En::HasLineTo)
DA_ROB(EnCanFireLying, bool (S_En::*)(), &S_En::CanFireLying)
DA_ROB(EnTakeCoverStance, bool (S_En::*)(const S_Checkpoint *, C_Human *), &S_En::TakeCoverStance)
DA_ROB(EnChooseCombatStance, void (S_En::*)(), &S_En::ChooseCombatStance)
DA_ROB(EnFindRetreatPoint, int (S_En::*)(), &S_En::FindRetreatPoint)
DA_ROB(EnFindApproachPoint, int (S_En::*)(const S_Checkpoint *, C_Human *), &S_En::FindApproachPoint)
DA_ROB(EnFindFlankPoint, int (S_En::*)(const S_vector4 &), &S_En::FindFlankPoint)
DA_ROB(EnFindCoverInRange, int (S_En::*)(), &S_En::FindCoverInRange)
DA_ROB(EnHasFriendNearby, bool (S_En::*)(), &S_En::HasFriendNearby)
DA_ROB(EnShouldStay, bool (S_En::*)(), &S_En::ShouldStay)
DA_ROB(EnMoveToCombatPoint, bool (S_En::*)(int), &S_En::MoveToCombatPoint)
DA_ROB(EnFindLauncherPoint, int (S_En::*)(C_Human *), &S_En::FindLauncherPoint)
DA_ROB(EnFindPointBesideVehicle, int (S_En::*)(C_Human *), &S_En::FindPointBesideVehicle)
DA_ROB(EnPickCoverAwayFrom, int (S_En::*)(C_Human *), &S_En::PickCoverAwayFrom)

// enemy_gunner.cpp's
DA_ROB(EnGunnerEvent, void (S_En::*)(S_HumanEvent *), &S_En::GunnerEvent)
DA_ROB(EnUpdateGunner, void (S_En::*)(int), &S_En::UpdateGunner)
DA_ROB(EnPickGunnerTarget, C_Human *(S_En::*)(), &S_En::PickGunnerTarget)
DA_ROB(EnPickSeenTarget, C_Human *(S_En::*)(), &S_En::PickSeenTarget)
DA_ROB(EnIsThreatToGunner, bool (S_En::*)(C_Human *), &S_En::IsThreatToGunner)
DA_ROB(EnIsGunnerThreatened, bool (S_En::*)(), &S_En::IsGunnerThreatened)
DA_ROB(EnGetGunnerDelay, int (S_En::*)(), &S_En::GetGunnerDelay)
DA_ROB(EnGetGunAimDir, S_vector (S_En::*)(bool), &S_En::GetGunAimDir)
DA_ROB(EnLeaveGun, void (S_En::*)(), &S_En::LeaveGun)
DA_ROB(EnIsOutOfGunReach, bool (S_En::*)(C_Human *), &S_En::IsOutOfGunReach)
DA_ROB(EnIsAtStaticWeapon, bool (S_En::*)(), &S_En::IsAtStaticWeapon)
DA_ROB(EnAimGun, void (S_En::*)(), &S_En::AimGun)
DA_ROB(EnAimAndFireGun, void (S_En::*)(), &S_En::AimAndFireGun)
DA_ROB(EnIsGunEmpty, bool (S_En::*)(), &S_En::IsGunEmpty)
DA_ROB(EnSweepGun, void (S_En::*)(), &S_En::SweepGun)
DA_ROB(EnAimAndFireCannon, void (S_En::*)(), &S_En::AimAndFireCannon)
DA_ROB(EnIsLineOfFireFree, bool (S_En::*)(const S_vector4 &), &S_En::IsLineOfFireFree)
DA_ROB(EnFindGunToUse, C_Actor *(S_En::*)(int), &S_En::FindGunToUse)
DA_ROB(EnIsGunUseless, bool (S_En::*)(C_Actor *), &S_En::IsGunUseless)
DA_ROB(EnNearestGun, C_Actor *(S_En::*)(const std::vector<C_Actor *> &), &S_En::NearestGun)

DA_ROB(EnTargetChanged, bool S_En::*, &S_En::m_targetChanged)
DA_ROB(EnFlags, uint32_t S_En::*, &S_En::m_enemyFlags)
DA_ROB(EnEvent, S_HumanEvent *S_En::*, &S_En::m_event)
DA_ROB(EnGun, C_Actor *S_En::*, &S_En::m_gun)
DA_ROB(EnUnknown14C, int S_En::*, &S_En::m_unknown14C)
DA_ROB(EnUnknown150, int S_En::*, &S_En::m_unknown150)
DA_ROB(EnUnknown154, bool S_En::*, &S_En::m_unknown154)
DA_ROB(EnHomePoints, std::vector<uint16_t> S_En::*, &S_En::m_homePoints)
DA_ROB(EnHomeCover, std::vector<uint16_t> S_En::*, &S_En::m_homeCover)
DA_ROB(EnHomePos, S_vector4 S_En::*, &S_En::m_homePos)
DA_ROB(EnHomeCheckpoint, int S_En::*, &S_En::m_homeCheckpoint)
DA_ROB(EnHomeRadius, float S_En::*, &S_En::m_homeRadius)
DA_ROB(EnTaskTime, int S_En::*, &S_En::m_taskTime)
DA_ROB(EnTarget, C_Human *S_En::*, &S_En::m_target)
DA_ROB(EnAimHeight, float S_En::*, &S_En::m_aimHeight)
DA_ROB(EnAlertRange, int S_En::*, &S_En::m_alertRange)
DA_ROB(EnAlertTime, int S_En::*, &S_En::m_alertTime)
DA_ROB(EnGrenadeTime, int S_En::*, &S_En::m_grenadeTime)
DA_ROB(EnFireWait, int S_En::*, &S_En::m_fireWait)
DA_ROB(EnGuns, std::vector<C_Actor *> S_En::*, &S_En::m_guns)
DA_ROB(EnGunCheckTime, int S_En::*, &S_En::m_gunCheckTime)
DA_ROB(EnMoraleCheckTime, int S_En::*, &S_En::m_moraleCheckTime)
DA_ROB(EnCoverTime, int S_En::*, &S_En::m_coverTime)
DA_ROB(EnUnarmedTime, int S_En::*, &S_En::m_unarmedTime)
DA_ROB(EnLowAmmo, bool S_En::*, &S_En::m_lowAmmo)
DA_ROB(EnSurrenderTime, int S_En::*, &S_En::m_surrenderTime)
DA_ROB(EnSeenTime, int S_En::*, &S_En::m_enemySeenTime)

// ---- the pair
struct S_TestEnemy : C_EnemyController {
    bool ExecuteCommand(const S_HumanCommand &c) override
    {
        LogCommand(this, c);
        return true;
    }
};
S_TestEnemy *g_pen;
uint8_t *g_oen;
void *g_oenVt[0x48];
S_En *En() { return g_pen; }
template <class T> T &EnField(T S_En::*m) { return En()->*m; }

// The plain fields: the original's offset, the port's member, its size.
struct S_EnWord {
    const char *name;
    size_t orig;
    void *(*port)();
    size_t size;
};
#define EN_WORD(NAME, AT, TAG, SIZE) {NAME, AT, [] { return static_cast<void *>(&(En()->*DaGet(TAG()))); }, SIZE}
const S_EnWord EN_WORDS[] = {
    EN_WORD("targetChanged", 0x140, EnTargetChanged, 1),
    EN_WORD("enemyFlags", 0x144, EnFlags, 4),
    EN_WORD("unknown14C", 0x14C, EnUnknown14C, 4),
    EN_WORD("unknown150", 0x150, EnUnknown150, 4),
    EN_WORD("unknown154", 0x154, EnUnknown154, 1),
    EN_WORD("homePos", 0x180, EnHomePos, 16),
    EN_WORD("homeCheckpoint", 0x190, EnHomeCheckpoint, 4),
    EN_WORD("homeRadius", 0x194, EnHomeRadius, 4),
    EN_WORD("taskTime", 0x198, EnTaskTime, 4),
    EN_WORD("aimHeight", 0x1A0, EnAimHeight, 4),
    EN_WORD("alertRange", 0x1A4, EnAlertRange, 4),
    EN_WORD("alertTime", 0x1A8, EnAlertTime, 4),
    EN_WORD("grenadeTime", 0x1AC, EnGrenadeTime, 4),
    EN_WORD("fireWait", 0x1B0, EnFireWait, 4),
    EN_WORD("gunCheckTime", 0x1C4, EnGunCheckTime, 4),
    EN_WORD("moraleCheckTime", 0x1C8, EnMoraleCheckTime, 4),
    EN_WORD("coverTime", 0x1CC, EnCoverTime, 4),
    EN_WORD("unarmedTime", 0x1D0, EnUnarmedTime, 4),
    EN_WORD("lowAmmo", 0x1D4, EnLowAmmo, 1),
    EN_WORD("surrenderTime", 0x1D8, EnSurrenderTime, 4),
    EN_WORD("enemySeenTime", 0x1DC, EnSeenTime, 4),
};
uint32_t EnWordOf(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t EnMapped(const void *p) { return p ? EnWordOf(ToOrig(p)) : 0u; }
bool EnIsShared(const void *e) { return DaEventIndex(e) >= 0 && DaEventIndex(e) < DA_EVENTS; }
// +0x148: the port's event (shared or allocated: the same pointer), else its gun mapped.
uint32_t EnSlot148()
{
    S_HumanEvent *e = EnField(DaGet(EnEvent()));
    return e ? EnWordOf(e) : EnMapped(EnField(DaGet(EnGun())));
}

// The enemy's fields in the original's layout.
void EnemyToOrig()
{
    for (const S_EnWord &w : EN_WORDS) {
        std::memset(g_oen + w.orig, 0, w.size == 1 ? 4 : w.size);
        std::memcpy(g_oen + w.orig, w.port(), w.size);
    }
    const uint32_t slot = EnSlot148();
    std::memcpy(g_oen + 0x148, &slot, 4);
    const uint32_t target = EnMapped(EnField(DaGet(EnTarget())));
    std::memcpy(g_oen + 0x19C, &target, 4);
    EnVectorPut(g_oen + 0x158, EnField(DaGet(EnHomePoints())));
    EnVectorPut(g_oen + 0x168, EnField(DaGet(EnHomeCover())));
    std::vector<uint32_t> guns;
    for (C_Actor *g : EnField(DaGet(EnGuns())))
        guns.push_back(EnMapped(g));
    MsvcVectorPut(g_oen + 0x1B4, guns, size_t(I(0, 2)));
}
// An event either side allocated in the call: compared by its fields.
std::string EnEventDiffers(const S_HumanEvent *p, const uint8_t *o)
{
    if (!p || !o)
        return p || o ? "event: one side none" : "";
    const auto *pb = reinterpret_cast<const uint8_t *>(p);
    if (std::memcmp(pb, o, 0x14) != 0 || std::memcmp(pb + 0x20, o + 0x20, 12) != 0 || std::memcmp(pb + 0x30, o + 0x30, 4) != 0) {
        char text[160];
        uint32_t ow[5];
        std::memcpy(ow, o, 20);
        std::snprintf(text, sizeof text, "event: port %08x %d %d %d %d, original %08x %d %d %d %d", p->type, p->timeLeft, p->age,
                      p->actorId, p->sourceId, ow[0], int(ow[1]), int(ow[2]), int(ow[3]), int(ow[4]));
        return text;
    }
    return "";
}
std::string EnemyDiffers()
{
    for (const S_EnWord &w : EN_WORDS) {
        if (std::memcmp(g_oen + w.orig, w.port(), w.size) == 0)
            continue;
        uint32_t a = 0, b = 0;
        std::memcpy(&a, w.port(), std::min<size_t>(w.size, 4));
        std::memcpy(&b, g_oen + w.orig, std::min<size_t>(w.size, 4));
        char text[128];
        std::snprintf(text, sizeof text, "%s: port %08x original %08x", w.name, a, b);
        return text;
    }
    uint32_t slot;
    std::memcpy(&slot, g_oen + 0x148, 4);
    S_HumanEvent *e = EnField(DaGet(EnEvent()));
    const uint32_t expect = EnSlot148();
    if (slot != expect) {
        // (An event the call allocated: a different object per side.)
        const auto *oe = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(slot));
        if (!e || EnIsShared(e) || !oe || EnIsShared(oe))
            return "event/gun (+0x148)";
        const std::string why = EnEventDiffers(e, oe);
        if (!why.empty())
            return why;
    }
    uint32_t target;
    std::memcpy(&target, g_oen + 0x19C, 4);
    if (target != EnMapped(EnField(DaGet(EnTarget()))))
        return "target";
    if (EnVectorGet(g_oen + 0x158) != EnField(DaGet(EnHomePoints())))
        return "homePoints";
    if (EnVectorGet(g_oen + 0x168) != EnField(DaGet(EnHomeCover())))
        return "homeCover";
    std::vector<uint32_t> guns;
    for (C_Actor *g : EnField(DaGet(EnGuns())))
        guns.push_back(EnMapped(g));
    if (MsvcVectorGet(g_oen + 0x1B4) != guns)
        return "guns";
    return "";
}
// (The look direction's w, the facing's +0x2C: the turns pass a stack word they do not set, the port 0.)
template <class P, class O> void EnCall(const char *what, P port, O original)
{
    static const bool trace = std::getenv("EN_TRACE") != nullptr;
    if (trace)
        std::printf("[%s task %d state %x hands %d]\n", what, g_pen->m_task ? g_pen->m_task->type : 0,
                    g_pen->m_task ? g_pen->m_task->unknown08 : 0, g_pen->m_handsItem != nullptr);
    DaCall(what, EnemyToOrig, EnemyDiffers,
           [] {
               uint8_t *facing;
               std::memcpy(&facing, g_oen + 0x124, 4);
               std::memcpy(facing + 0x2C, reinterpret_cast<const uint8_t *>(g_pen->m_facing) + 0x2C, 4);
           },
           port, original);
}

void SetUpEnemy()
{
    SetUpDerivedAi();
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0051e520, FAKE(EN_GetActor)},
        {0x0051e690, FAKE(EN_FindActorPoint)},
        {0x0051d5c0, FAKE(EN_FindNearestPoint)},
        {0x005208e0, FAKE(EN_GetNeighbourCover)},
        {0x00520860, FAKE(EN_GetReachable)},
        {0x005208a0, FAKE(EN_GetReachableCopy)},
        {0x00520910, FAKE(EN_GetReachableCover)},
        {0x00520a30, FAKE(EN_PickCoverPoint)},
        {0x005a7430, FAKE(EN_FindItemByRecordType)},
        {0x005a2b40, FAKE(EN_RequestMoveItem)},
        {0x005a2bb0, FAKE(EN_RequestTransferItem)},
        {0x005ae270, FAKE(EN_GetAttacker)},
        {0x005ae310, FAKE(EN_GetStrongestStimulus)},
        {0x005ae890, FAKE(EN_IsAwayFromNearbyActors)},
        {0x005ac4c0, FAKE(EN_GetSector)},
        {0x005ac1d0, FAKE(EN_GetUnknown6E0)},
        {0x005ac1c0, FAKE(EN_RemoveCollision)},
        {0x005ac1b0, FAKE(EN_RestoreCollision)},
        {0x00718830, FAKE(EN_RayTest)},
        {0x006f7cc0, FAKE(EN_RayTestDynamic)},
        {0x004d9050, FAKE(OV_CarApplyControls)},   // (part 23's, set only in its own test)
        {0x005e0dd0, FAKE(OV_SubmarineSetControls)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // The seat weapons: the seat car's and part 23's actors' (the post's guns).
    S_EnSeatFake fake;
    void **fakeVt = *reinterpret_cast<void ***>(&fake);
    for (void **vt : {g_asCarPortVt, g_actPortVt}) {
        vt[VSlot(&C_Actor::GetSeatWeaponRounds)] = fakeVt[0];
        vt[VSlot(&C_Actor::GetSeatWeaponOrigin)] = fakeVt[1];
        vt[VSlot(&C_Actor::GetSeatOccupant)] = fakeVt[2];
    }
    for (void **vt : {g_asCarOrigVt, g_actOrigVt}) {
        vt[0x9C / 4] = reinterpret_cast<void *>(EN_SeatRounds);
        vt[0xA8 / 4] = reinterpret_cast<void *>(EN_SeatOrigin);
        vt[0x78 / 4] = reinterpret_cast<void *>(EN_SeatOccupant);
    }
    // (Part 23's actors aim as the seat car does: part 35b's slots.)
    for (const auto &slot : {std::make_pair(VSlot(&C_Actor::CanSeatWeaponAim), 0xBC / 4), std::make_pair(VSlot(&C_Actor::HasSeatWeapon), 0x98 / 4),
                             std::make_pair(VSlot(&C_Actor::GetSeatWeaponAim), 0xA4 / 4),
                             std::make_pair(VSlot(&C_Actor::GetSeatWeaponDef), 0xB4 / 4)}) {
        g_actPortVt[slot.first] = g_asCarPortVt[slot.first];
        g_actOrigVt[slot.second] = g_asCarOrigVt[slot.second];
    }
    g_behaviourOf = EnBehaviourOf;
    g_actorLookupsOnce = true;
    g_daAppend = true;
    g_checkpointGetSilent = true;
    // (The component's muzzle velocity: the port's virtual slot; the original reads the field, +0x38 on both.)
    g_compPortVt[VSlot(&C_WeaponComp::GetMuzzleVelocity)] = reinterpret_cast<void *>(EN_MuzzleVelocity);
    g_weaponPortVt[VSlot(&C_Item::GetMaxAmount)] = reinterpret_cast<void *>(EN_GetMaxAmount);
    g_weaponOrigVt[0x10 / 4] = reinterpret_cast<void *>(EN_GetMaxAmount);
    g_names[g_enSectorOther] = "otherSector";
    g_names[g_enVolume] = "volume";
    g_names[g_enSpheres] = "spheres";
    g_pen = new S_TestEnemy;
    g_oen = DaOrigController(g_pen, 0x1E0, HUMAN_CONTROLLER_ENEMY, 0x00811730, g_oenVt, "enemy");
}

void EnPrimeTaskPool(int type);

// The sounds the human heard (each side's copies: the port's member, the original's vector at +0x2A8, set as it runs;
// no source only for chapters 8 and up, as part 35's: a turn toward one reads it on both sides).
struct S_EnSounds {
    std::vector<S_RememberedSound *> port;
    std::vector<uint32_t> orig;
    void Make()
    {
        C_Human *h = g_h.human;
        for (int k = P(0.4f) ? 0 : I(1, 3); k > 0; --k) {
            auto *ps = static_cast<S_RememberedSound *>(_aligned_malloc(sizeof(S_RememberedSound), 16));
            ps->pos = h->GetFrame()->GetWorldPos() + S_vector(U(-30, 30), U(-2, 2), U(-30, 30));
            ps->posW = 1.0f;
            ps->chapter = I(0, 10);
            ps->source = P(0.2f) && ps->chapter >= 8 ? nullptr : static_cast<C_Actor *>(P(0.5f) ? PortOther() : PortThird());
            ps->time = uint32_t(I(0, 100000));
            ps->refreshed = P(0.5f);
            auto *os = static_cast<uint8_t *>(_aligned_malloc(sizeof(S_RememberedSound), 16));
            std::memcpy(os, ps, sizeof(S_RememberedSound));
            void *oSource = ps->source ? ToOrig(ps->source) : nullptr;
            std::memcpy(os + offsetof(S_RememberedSound, source), &oSource, 4);
            port.push_back(ps);
            orig.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(os)));
        }
        h->m_heardSounds = port;
    }
    void Free()
    {
        g_h.human->m_heardSounds.clear();
        std::memset(o_human + 0x2A8, 0, 16);
        for (size_t k = 0; k < port.size(); ++k) {
            _aligned_free(port[k]);
            _aligned_free(reinterpret_cast<void *>(static_cast<uintptr_t>(orig[k])));
        }
    }
};
void EnApproachSafe(S_En *s);

// A scripted controller as the previous one (raw objects: its type and its alarm, the port's members and the original's
// +0x48, +0x140, +0x1B4).
DA_ROB(ScAlarmPos, S_vector4 C_ScriptedController::*, &C_ScriptedController::m_alarmPos)
DA_ROB(ScAlarmType, uint32_t C_ScriptedController::*, &C_ScriptedController::m_alarmType)
alignas(16) uint8_t g_enScriptedPort[sizeof(C_ScriptedController) + 16], g_enScriptedOrig[0x200];
C_HumanController *EnScripted()
{
    static bool named = false;
    if (!named) {
        named = true;
        g_names[g_enScriptedPort] = g_names[g_enScriptedOrig] = "scripted";
        g_extraPointers.push_back({g_enScriptedPort, g_enScriptedOrig});
    }
    std::memset(g_enScriptedPort, 0, sizeof g_enScriptedPort);
    std::memset(g_enScriptedOrig, 0, sizeof g_enScriptedOrig);
    auto *p = reinterpret_cast<C_ScriptedController *>(g_enScriptedPort);
    static const int types[] = {HUMAN_CONTROLLER_AI, HUMAN_CONTROLLER_AI, HUMAN_CONTROLLER_AI, HUMAN_CONTROLLER_ENEMY, HUMAN_CONTROLLER_SNIPER};
    const int type = types[I(0, 4)];
    reinterpret_cast<S_TestAi *>(p)->m_type = static_cast<E_HumanController>(type);
    std::memcpy(g_enScriptedOrig + 0x48, &type, 4);
    // (The alarm's place: now and then out of the world (|y| > 100000: none).)
    const S_vector4 pos = ToVector4(g_h.frame->GetWorldPos() + S_vector(U(-30, 30), P(0.15f) ? (P(0.5f) ? 100001.0f : -100001.0f) : U(-2, 2), U(-30, 30)),
                                    1.0f);
    p->*DaGet(ScAlarmPos()) = pos;
    std::memcpy(g_enScriptedOrig + 0x140, &pos, 16);
    static const uint32_t alarms[] = {0x001, 0x002, 0x004, 0x008, 0x010, 0x020, 0x040, 0x100, 0x200, 0x080, 0, 0x003};
    const uint32_t alarm = alarms[I(0, 11)];
    p->*DaGet(ScAlarmType()) = alarm;
    std::memcpy(g_enScriptedOrig + 0x1B4, &alarm, 4);
    return reinterpret_cast<C_HumanController *>(p);
}

// ---- the state
const int EN_TASK_STATES_1[] = {S_En::GUARD_LOOK, S_En::GUARD_PICK_TARGET, S_En::GUARD_TO_GUN, S_En::GUARD_TO_COVER,
                                S_En::GUARD_MOVE_FIRING, S_En::GUARD_MOVE, S_En::GUARD_HOLD, S_En::GUARD_FIRE,
                                S_En::STATE_LAUNCHER, S_En::STATE_TO_LAUNCHER_POINT, S_En::STATE_LAUNCHER_FIRE,
                                S_En::STATE_ATTACK_DONE, S_En::STATE_HIDE_FROM_VEHICLE, S_En::STATE_TO_HIDING,
                                S_En::STATE_HIDING, 4};
const int EN_TASK_STATES_2[] = {S_En::SEARCH_START, 1, S_En::SEARCH_IDLE, S_En::SEARCH_GO, S_En::SEARCH_TURN,
                                S_En::SEARCH_LOOK, S_En::SEARCH_TO_POINT, S_En::SEARCH_WALK, S_En::SEARCH_AIM,
                                S_En::SEARCH_AROUND, S_En::SEARCH_AROUND_WALK, S_En::SEARCH_TO_SOURCE, S_En::SEARCH_WAIT,
                                S_En::SEARCH_HOME, S_En::SEARCH_CLIMBING, S_En::SEARCH_OFF_LADDER};
const int EN_TASK_STATES_3[] = {S_En::COMBAT_PICK_TARGET, S_En::COMBAT_MOVE, S_En::COMBAT_TO_RANGE, S_En::COMBAT_TO_COVER,
                                S_En::COMBAT_FIRE, S_En::COMBAT_LOOK, S_En::COMBAT_APPROACH, S_En::STATE_LAUNCHER,
                                S_En::STATE_TO_LAUNCHER_POINT, S_En::STATE_LAUNCHER_FIRE, S_En::STATE_ATTACK_DONE,
                                S_En::STATE_HIDE_FROM_VEHICLE, S_En::STATE_TO_HIDING, S_En::STATE_HIDING, 7};
const int EN_TASK_STATES_5[] = {S_En::BROKEN_CALM, S_En::BROKEN_START, S_En::BROKEN_SURRENDER, S_En::BROKEN_SURRENDERING,
                                S_En::BROKEN_SURRENDERED, S_En::BROKEN_FLEEING, S_En::BROKEN_FLEE, S_En::BROKEN_FLEE_WALK,
                                S_En::BROKEN_FLED, S_En::BROKEN_FIGHT, S_En::BROKEN_FIGHT_PICK, S_En::BROKEN_FIGHT_ON,
                                S_En::BROKEN_FIGHT_WAIT, 0};
const int EN_TASK_STATES_6[] = {S_En::GUNNER_START, S_En::GUNNER_WATCH, S_En::GUNNER_PICK, S_En::GUNNER_FIRE,
                                S_En::GUNNER_PAUSE, S_En::GUNNER_DONE, S_En::GUNNER_GIVE_UP, S_En::GUNNER_LEAVE, 8};
template <size_t N> int Pick(const int (&a)[N]) { return a[I(0, int(N) - 1)]; }
int EnTaskState(int type)
{
    switch (type) {
    case 1: return Pick(EN_TASK_STATES_1);
    case 2: return Pick(EN_TASK_STATES_2);
    case 3: return Pick(EN_TASK_STATES_3);
    case 5: return Pick(EN_TASK_STATES_5);
    case 6: return Pick(EN_TASK_STATES_6);
    default: return I(0, 10);
    }
}
// A timer now and then at its edges.
int EnTimer(int edge) { return P(0.2f) ? edge + I(-1, 1) : P(0.2f) ? 0 : I(0, 2 * edge); }

// The fighting's state (part 36's), then the enemy's: its fields, its task (types 1, 2, 3, 5 and 6 in their states,
// now and then none or another type), the post's points and guns, the events (pending and stimuli), the morale, the
// script, the frame's data, the containers and the definitions' behaviours, the vehicle, the fakes' answers.
// `aiming`: the call may fire (Tick, UpdateFiring): the targets and the enemies in view the tested human only (its
// model has the bones GetAimPoint picks from; the copies have none).
void RandomEnemy(bool aiming, int forceTask = 0)
{
    RandomFireOrder(true);
    S_En *s = En();
    C_Human *h = g_h.human;
    C_Human *const humans[3] = {h, aiming ? h : PortOther(), aiming ? h : PortThird()};
    if (aiming) {
        h->m_seenEnemies.clear();
        for (int k = P(0.3f) ? 0 : I(1, 2); k > 0; --k)
            h->m_seenEnemies.push_back(h);
        if (g_pa->m_fireOrder->target && g_pa->m_fireOrder->target != h)
            g_pa->m_fireOrder->target = P(0.5f) ? nullptr : h;
    }
    // (Not aiming: the human is not among its own enemies in view (the nearest one then 0 m away, every threshold
    // passed); the others near the AI's thresholds now and then; the human above or below the ground's level.)
    if (!aiming) {
        auto &enemies = h->m_seenEnemies;
        enemies.erase(std::remove(enemies.begin(), enemies.end(), h), enemies.end());
        {
            // (About the height the human started at: not a walk away from it.)
            I3D_frame *own = g_h.frame;
            static const float baseY = own->GetWorldPos().y;
            const S_vector at = own->GetWorldPos();
            own->SetPosInline(S_vector(at.x, baseY + (P(0.5f) ? U(-2, 2) : 0.0f), at.z));
            UpdateTree(own);
        }
        for (I3D_frame *f : {g_otherFrame, g_asThirdFrame}) {
            if (!P(0.6f))
                continue;
            static const float limits[] = {5, 6, 8, 10, 12, 15, 20, 24, 25, 30, 40};
            const float d = limits[I(0, 10)] * (P(0.2f) ? 1.0f : U(0.85f, 1.15f));
            const float a = U(0, 6.2831f);
            f->SetPosInline(g_h.frame->GetWorldPos() + S_vector(std::cos(a) * d, U(-1.5f, 1.5f), std::sin(a) * d));
            UpdateTree(f);
        }
    }
    // (The others' controllers' quiet time now and then at the gunner's edge, 400 ms.)
    if (P(0.3f)) {
        g_pa->m_quietTime = 400 + I(-1, 1);
        g_pc2->m_quietTime = 400 + I(-1, 1);
    }
    h->m_seenBodies.clear();
    for (int k = P(0.6f) ? 0 : I(1, 2); k > 0; --k)
        h->m_seenBodies.push_back(humans[I(0, 2)]);
    s->m_events = &g_daEvents;
    h->m_events = &g_daEvents;
    // (The second base controller, the others' in view: its human the other one; IsFacing measures from it.)
    g_pc2->m_human = PortOther();
    PortOther()->m_events = &g_daEvents;
    PortThird()->m_events = &g_daEvents;
    void *events = g_daOrigEvents;
    std::memcpy(g_origOther + 0xB4, &events, 4);
    std::memcpy(g_origThird + 0xB4, &events, 4);
    // (Their containers: the worn ones; a network game's IsFriendOf reads their uniforms. +0x74 in the original.)
    void *table = g_wornTable;
    for (C_Human *o : {PortOther(), PortThird()})
        o->m_containers = g_wornTable;
    std::memcpy(g_origOther + 0x74, &table, 4);
    std::memcpy(g_origThird + 0x74, &table, 4);
    // (Their velocities: the port's member and the original's +0x1B0, alike; PredictPos and the gun's lead read them.)
    for (int k = 0; k < 2; ++k) {
        const S_vector4 v = P(0.3f) ? S_vector4{0, 0, 0, 0} : S_vector4{U(-6, 6), U(-1, 1), U(-6, 6), 0};
        (k ? PortThird() : PortOther())->m_velocity = v;
        std::memcpy((k ? g_origThird : g_origOther) + 0x1B0, &v, 16);
    }
    // The move flags: the AI mode (now and then none or two), dead or unconscious now and then, the ladder, the pace.
    uint32_t &f = s->m_moveFlags;
    f &= ~uint32_t(MOVE_AI_MODE_MASK | MOVE_DEAD | MOVE_UNCONSCIOUS | MOVE_ON_LADDER | MOVE_PACE_MASK);
    static const uint32_t modes[] = {MOVE_AI_AGGRESSIVE, MOVE_AI_DEFENSIVE, MOVE_AI_ZOMBIE, 0, MOVE_AI_AGGRESSIVE | MOVE_AI_DEFENSIVE};
    f |= P(0.85f) ? modes[I(0, 2)] : modes[I(3, 4)];
    if (P(0.08f))
        f |= P(0.5f) ? MOVE_DEAD : MOVE_UNCONSCIOUS;
    if (P(0.15f))
        f |= MOVE_ON_LADDER;
    static const uint32_t paces[] = {MOVE_PACE_1, MOVE_PACE_3, MOVE_PACE_4, MOVE_PACE_MARCH, 0};
    f |= paces[I(0, 4)];
    // The enemy's fields.
    s->*DaGet(EnTargetChanged()) = P(0.5f);
    s->*DaGet(EnFlags()) = uint32_t(I(0, 63)) & (P(0.5f) ? ~uint32_t(S_En::FLAG_DONE) : ~0u);
    s->*DaGet(EnUnknown14C()) = AiInt();
    s->*DaGet(EnUnknown150()) = AiInt();
    s->*DaGet(EnUnknown154()) = P(0.5f);
    std::vector<uint16_t> &homePoints = s->*DaGet(EnHomePoints());
    std::vector<uint16_t> &homeCover = s->*DaGet(EnHomeCover());
    homePoints.clear();
    homeCover.clear();
    for (int k = P(0.2f) ? 0 : I(1, 6); k > 0; --k)
        homePoints.push_back(uint16_t(I(0, 7)));
    for (uint16_t p : homePoints)
        if (P(0.5f))
            homeCover.push_back(p);
    s->*DaGet(EnHomePos()) = ToVector4(h->GetFrame()->GetWorldPos() + S_vector(U(-15, 15), U(-1, 1), U(-15, 15)), 1.0f);
    s->*DaGet(EnHomeCheckpoint()) = P(0.1f) ? -1 : P(0.1f) ? CHECKPOINT_NONE : I(0, 7);
    s->*DaGet(EnHomeRadius()) = U(5, 30);
    s->*DaGet(EnTaskTime()) = AiInt();
    s->*DaGet(EnTarget()) = P(0.3f) ? nullptr : humans[P(0.15f) ? 0 : I(1, 2)];
    static const float heights[] = {0.45f, 0.59f, 0.0f};
    s->*DaGet(EnAimHeight()) = P(0.8f) ? heights[I(0, 2)] : U(0, 1);
    s->*DaGet(EnAlertRange()) = P(0.2f) ? 100000 : I(0, 120);
    s->*DaGet(EnAlertTime()) = P(0.3f) ? 0 : EnTimer(1000);
    s->*DaGet(EnGrenadeTime()) = EnTimer(10000);
    s->*DaGet(EnFireWait()) = P(0.4f) ? 0 : EnTimer(300);
    std::vector<C_Actor *> &guns = s->*DaGet(EnGuns());
    guns.clear();
    for (int k = P(0.4f) ? 0 : I(1, 3); k > 0; --k)
        guns.push_back(static_cast<C_Actor *>(PortActor(I(3, 6))));
    s->*DaGet(EnGunCheckTime()) = P(0.4f) ? 0 : EnTimer(1000);
    s->*DaGet(EnMoraleCheckTime()) = EnTimer(100);
    s->*DaGet(EnCoverTime()) = EnTimer(7000);
    s->*DaGet(EnUnarmedTime()) = EnTimer(1000);
    s->*DaGet(EnLowAmmo()) = P(0.5f);
    s->*DaGet(EnSurrenderTime()) = P(0.5f) ? -1 : I(0, 100000);
    s->*DaGet(EnSeenTime()) = EnTimer(2000);
    // The task.
    if (s->m_task)
        ReleaseTask(s->m_task);
    const float r = U(0, 1);
    static const int types[] = {1, 2, 3, 5, 6};
    if (forceTask) {
        s->m_task = AllocTask(forceTask);
        RandomAiTask(s->m_task);
        s->m_task->unknown08 = P(0.95f) ? EnTaskState(forceTask) : I(-2, 20);
        s->m_task->unknown0C = P(0.3f) ? 0 : I(0, 9000);
        s->m_task->unknown10 = P(0.4f) ? 0 : P(0.2f) ? 1 : I(0, 20000);
    } else if (r < 0.1f) {
        s->m_task = nullptr;
    } else if (r < 0.15f) {
        s->m_task = AllocTask(I(1, 12));
        RandomAiTask(s->m_task);
    } else {
        s->m_task = AllocTask(types[I(0, 4)]);
        RandomAiTask(s->m_task);
        s->m_task->unknown08 = P(0.9f) ? EnTaskState(s->m_task->type) : I(-2, 20);
        s->m_task->unknown0C = P(0.3f) ? 0 : I(0, 9000);
        s->m_task->unknown10 = P(0.4f) ? 0 : P(0.2f) ? 1 : I(0, 20000);
    }
    // +0x148: a search's event, a guard's gun, now and then the other with another task.
    s->*DaGet(EnEvent()) = nullptr;
    s->*DaGet(EnGun()) = nullptr;
    const int taskType = s->m_task ? s->m_task->type : 0;
    // (Never both, nor with another task: the original keeps one field, the game sets it with its task.)
    if (taskType == 2 && P(0.85f))
        s->*DaGet(EnEvent()) = &g_daEv[I(0, DA_EVENTS - 1)];
    else if (taskType == 1 && P(0.6f))
        s->*DaGet(EnGun()) = static_cast<C_Actor *>(PortActor(I(3, 6)));
    // (Going to the gun with one: the original reads a missing one, a crash the port fixes.)
    if (taskType == 1 && s->m_task->unknown08 == S_En::GUARD_TO_GUN) {
        s->*DaGet(EnEvent()) = nullptr;
        s->*DaGet(EnGun()) = static_cast<C_Actor *>(PortActor(I(3, 6)));
    }
    // The move order.
    uint32_t &mf = s->m_move->flags;
    mf &= ~uint32_t(S_AIMoveOrder::FLAG_DONE | S_AIMoveOrder::FLAG_ROUTE | S_AIMoveOrder::FLAG_REROUTE);
    mf |= (P(0.4f) ? S_AIMoveOrder::FLAG_DONE : 0u) | (P(0.5f) ? S_AIMoveOrder::FLAG_ROUTE : 0u) |
          (P(0.2f) ? S_AIMoveOrder::FLAG_REROUTE : 0u);
    static const uint32_t movePaces[] = {0x4008, 0x10008, 0x2008, 0x1008, 0x4010, 0};
    s->m_move->unknown54 = movePaces[I(0, 5)];
    s->m_move->ladderFrom = uint16_t(P(0.2f) ? 0xFFFF : I(0, 9));
    s->m_move->ladderTo = uint16_t(P(0.2f) ? 0xFFFF : I(0, 9));
    // A network game's net object (the idle movement's send mask), the messages queued for the clients.
    if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL && P(0.8f))
        s->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
    g_h.movement->m_unknown1AC = P(0.5f) ? 0 : I(1, 3);
    g_h.movement->m_unknown9C0 = P(0.5f) ? 0 : I(1, 900);
    if (P(0.3f))
        RandomAiQueue(g_pa->m_toClients, g_oa + 0x90);
    // The events: the pending ones and the stimuli (shared events from the humans and others).
    C_HumanEvents &e = g_daEvents;
    std::deque<S_HumanEvent *> &pending = e.*DaGet(EvPending());
    std::deque<S_HumanEvent *> &stimuli = e.*DaGet(EvStimuli());
    pending.clear();
    stimuli.clear();
    // (Each a different event, as raised.)
    std::vector<int> order(DA_EVENTS);
    for (int k = 0; k < DA_EVENTS; ++k)
        order[size_t(k)] = k;
    for (int k = DA_EVENTS - 1; k > 0; --k)
        std::swap(order[size_t(k)], order[size_t(I(0, k))]);
    for (int k = P(0.6f) ? 0 : I(1, 3); k > 0; --k)
        pending.push_back(&g_daEv[order[size_t(k)]]);
    for (int k = P(0.4f) ? 0 : I(1, 4); k > 0; --k)
        stimuli.push_back(&g_daEv[I(0, DA_EVENTS - 1)]);
    std::vector<int> &nearby = e.*DaGet(EvNearby());
    nearby.clear();
    for (int k = P(0.4f) ? 0 : I(1, 3); k > 0; --k)
        nearby.push_back(P(0.2f) ? -1 : P(0.7f) ? humans[I(0, 2)]->GetNetId() : I(0, 50));
    for (S_HumanEvent &ev : g_daEv) {
        static const uint32_t kinds[] = {AI_EVENT_ENEMY_LOST, AI_EVENT_KILLED_ENEMY, AI_EVENT_BODY_SEEN, AI_EVENT_BODY_HEARD,
                                         AI_EVENT_FRIEND_HEARD, AI_EVENT_ENEMY_SEEN, AI_EVENT_SOUND_0, AI_EVENT_SOUND_2,
                                         AI_EVENT_SOUND_1, AI_EVENT_SOUND_6, AI_EVENT_SOUND_8, AI_EVENT_SHOT_AT,
                                         AI_EVENT_LOUD_SOUND, AI_EVENT_HIT_5, HUMAN_EVENT_HIT, 1, 0x20000009};
        ev.type = kinds[I(0, 16)];
        // (Part 23's actors as sources in local games only: a network game's IsFriendOf takes any actor for a human, on
        // both sides.)
        const bool local = (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL;
        ev.sourceId = P(0.2f) ? -1 : P(0.75f) ? humans[I(0, 2)]->GetNetId() : P(0.5f) && local ? 100 + I(0, 11) : I(0, 50);
        ev.actorId = P(0.3f) ? -1 : P(0.75f) ? humans[I(0, 2)]->GetNetId() : I(0, 50);
        ev.timeLeft = P(0.1f) ? 0 : I(0, 30000);
        ev.age = P(0.5f) ? I(0, ev.timeLeft / 2 + 1) : I(0, 30000);
        // (No time left and no age: the original's PickGunnerTarget divides by the time left, a crash the port fixes.)
        if (ev.timeLeft == 0 && ev.age == 0)
            ev.age = I(1, 100);
        // (Near, about, or far: the search runs from 60..80 m.)
        const float reach = P(0.2f) ? 110.0f : 50.0f;
        ev.pos = ToVector4(h->GetFrame()->GetWorldPos() + (P(0.2f) ? S_vector(U(-3, 3), 0, U(-3, 3)) : S_vector(U(-reach, reach), U(-2, 2), U(-reach, reach))),
                           1.0f);
        ev.unknown30 = I(0, 3);
        if (P(0.3f)) {
            static const float limits[] = {5, 6, 7, 10, 20, 30, 60, 80};
            const float d = limits[I(0, 7)] * (P(0.2f) ? 1.0f : U(0.85f, 1.15f));
            const float a = U(0, 6.2831f);
            ev.pos = ToVector4(h->GetFrame()->GetWorldPos() + S_vector(std::cos(a) * d, U(-1, 1), std::sin(a) * d), 1.0f);
        }
    }
    // The morale (now and then at its edges).
    auto morale = [] { return P(0.2f) ? (P(0.5f) ? I(19, 21) : I(39, 41)) : P(0.2f) ? I(74, 76) : P(0.2f) ? I(89, 91) : I(0, 100); };
    h->m_stats[STAT_MORALE] = morale();
    h->m_initialStats[STAT_MORALE] = morale();
    h->m_stats[STAT_SIGHT_RANGE] = P(0.2f) ? I(0, 120) : I(40, 250);
    // The script (OnAlarmDone or none) in the frame's data (the port's member and the original's +0xEC).
    const int onAlarmDone = P(0.5f) ? -1 : I(0, 50);
    reinterpret_cast<C_Script *>(g_daScriptBytes)->*DaGet(ScOnAlarmDone()) = onAlarmDone;
    std::memcpy(g_daScriptBytes + 0xEC, &onAlarmDone, 4);
    g_daFrameData = S_FrameData();
    g_daFrameData.actor = h;
    g_daFrameData.script = P(0.8f) ? reinterpret_cast<C_Script *>(g_daScriptBytes) : nullptr;
    g_daHandsLocked = P(0.2f);
    // The target gone now and then (GetActor no longer knows its id).
    // (The states that measure to the target unchecked have one, and it stays: both sides read a missing one when it
    // is gone (UpdateTimers forgets it): the combat's move and approach, the launcher's.)
    const int state = s->m_task ? s->m_task->unknown08 : -1;
    const int type = s->m_task ? s->m_task->type : 0;
    const bool needsTarget = ((type == 1 || type == 3) && state == S_En::STATE_LAUNCHER) ||
                             (type == 3 && (state == S_En::COMBAT_MOVE ||
                                            (state == S_En::COMBAT_APPROACH && (s->m_move->flags & S_AIMoveOrder::FLAG_ROUTE))));
    if (needsTarget && !(s->*DaGet(EnTarget())))
        s->*DaGet(EnTarget()) = humans[I(0, 2)];
    C_Human *const target = s->*DaGet(EnTarget());
    g_daGoneId = !needsTarget && target && P(0.15f) ? target->GetNetId() : -1000;
    // The weapons: single-shot launchers now and then (item type 0x26, +0x0C on both sides); the containers' items (the
    // hands, the long gun: part 21's dual-layout items) and the definitions' behaviours (the weapons' definition 0).
    for (int k = 0; k < 2; ++k) {
        const int type = P(0.3f) ? 0x26 : I(0, 0x40);
        reinterpret_cast<C_Item *>(g_weaponPort[k])->*DaGet(ItTypeId()) = type;
        std::memcpy(g_weaponOrig[k] + 0x0C, &type, 4);
    }
    static const int behaviours[] = {ITEM_BEHAVIOUR_MELEE,  ITEM_BEHAVIOUR_SIDEARM, ITEM_BEHAVIOUR_SMG, ITEM_BEHAVIOUR_RIFLE,
                                     ITEM_BEHAVIOUR_LMG,    ITEM_BEHAVIOUR_LAUNCHER, 7,                 ITEM_BEHAVIOUR_THROWN,
                                     ITEM_BEHAVIOUR_ASSAULT, 12,                     1,                 0x20};
    for (int &b : g_en.behaviours)
        b = P(0.25f) ? ITEM_BEHAVIOUR_LAUNCHER : behaviours[I(0, 11)];
    g_behaviour = g_en.behaviours[0];
    for (int c : {ITEM_CONTAINER_HANDS, ITEM_CONTAINER_LONG_GUN}) {
        g_wornContainers[c].item = P(0.3f) ? nullptr : g_wornItems[c];
        const int t = c == ITEM_CONTAINER_LONG_GUN && P(0.5f) ? 1 : I(0, 2);
        S_ItemAccess::SetDef(g_wornItems[c], g_wornDefs[t]);
        void *def = g_wornDefs[t];
        std::memcpy(g_wornItems[c] + 0x04, &def, 4);
    }
    // (The hands container's item: a weapon or none (part 21's worn items have no vtable); part 23 puts the original's
    // weapon in before it runs. The hands item now and then the container's.)
    const int hands = I(0, 2);
    g_handsWeapon = hands < 2 ? static_cast<void *>(g_weaponPort[hands]) : nullptr;
    g_wornContainers[ITEM_CONTAINER_HANDS].item = g_handsWeapon;
    if (P(0.3f))
        g_pa->m_handsItem = static_cast<C_Item *>(g_handsWeapon);
    // (The guard and the combat with a weapon in the hands: the original's CanFireMoving reads the behaviour of a
    // missing one, a crash the port fixes.)
    if ((taskType == 1 || taskType == 3) && !g_pa->m_handsItem)
        g_pa->m_handsItem = reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
    // (No reload asked with empty hands: the original reads the missing weapon's reload time, part 36's.)
    if (!g_pa->m_handsItem)
        g_pa->m_fireOrder->reload = false;
    g_cs.beltItem = P(0.4f) ? nullptr : reinterpret_cast<C_Item *>(g_weaponPort[1]);
    // The vehicle: the seat car as a car, a submarine, a static weapon, a Junkers or a door.
    static const int vehicleTypes[] = {ACTOR_CAR, ACTOR_SUBMARINE, ACTOR_STATIC_WEAPON, ACTOR_JUNKERS88, ACTOR_DOOR};
    const int vehicleType = vehicleTypes[I(0, 4)];
    auto *car = reinterpret_cast<C_Actor *>(g_asCarPort);
    car->m_type = static_cast<E_ActorType>(vehicleType);
    std::memcpy(g_asCarOrig + 0x18, &vehicleType, 4);
    // (The gunner's task in its vehicle: the original reads a missing one in IsOutOfGunReach, a crash the port fixes.)
    h->m_vehicle = P(0.6f) && !(s->m_task && s->m_task->type == 6) ? nullptr : car;
    h->m_seat = I(0, 3);
    void *ov = h->m_vehicle ? static_cast<void *>(g_asCarOrig) : nullptr;
    std::memcpy(o_human + 0x160, &ov, 4);
    std::memcpy(o_human + 0x164, &h->m_seat, 4);
    g_asSeat.hasWeapon = P(0.6f);
    g_asSeatFlags = P(0.5f) ? 0x10u : uint32_t(I(0, 0xFF));
    // The fakes' answers.
    g_en.reachable.clear();
    for (int k = P(0.2f) ? 0 : I(1, 6); k > 0; --k)
        g_en.reachable.push_back(uint16_t(I(0, 7)));
    g_en.reachableOk = P(0.9f);
    g_en.cover.clear();
    for (int k = P(0.3f) ? 0 : I(1, 4); k > 0; --k)
        g_en.cover.push_back(uint16_t(I(0, 7)));
    // (None: CHECKPOINT_NONE, 0xFFFF, as the network's answers; the original's are 16 bits.)
    g_en.pick = P(0.3f) ? CHECKPOINT_NONE : I(0, 7);
    g_en.pickDrop = P(0.5f) ? 0u : uint32_t(I(0, 255));
    g_en.actorPoint = P(0.3f) ? CHECKPOINT_NONE : I(0, 7);
    for (int &a : g_en.pointActor)
        a = P(0.6f) ? -1 : I(0, 6);
    g_en.nearestPoint = P(0.3f) ? CHECKPOINT_NONE : I(0, 7);
    g_en.recordItem = P(0.5f);
    g_en.requestOk = P(0.7f);
    g_en.strongest = P(0.3f) ? -1 : I(0, DA_EVENTS - 1);
    g_en.attacker = P(0.3f) ? nullptr : P(0.6f) ? static_cast<C_Actor *>(humans[I(0, 2)]) : static_cast<C_Actor *>(PortActor(I(0, 6)));
    g_en.away = P(0.7f);
    g_en.outdoors = P(0.6f);
    g_en.spheres = P(0.3f) ? 0 : I(1, 4);
    for (int k = 0; k < 4; ++k)
        g_enSpheres[k] = ToVector4(h->GetFrame()->GetWorldPos() + S_vector(U(-20, 20), U(0, 2), U(-20, 20)), U(0.1f, 3));
    g_enSphereStore[0] = ToVector4(h->GetFrame()->GetWorldPos(), 1e6f);
    g_en.rayHit = P(0.4f);
    g_en.dynamicHit = P(0.4f) ? -1 : P(0.2f) ? 13 : I(0, 6);
    g_en.rounds = P(0.3f) ? 0 : I(1, 30);
    g_en.origin = h->GetFrame()->GetWorldPos() + S_vector(U(-8, 8), U(0, 2), U(-8, 8));
    g_en.occupant = P(0.5f) ? -1 : I(0, 1);
    g_da.move = I(0, 3);
    g_da.longGun = P(0.5f);
    g_da.neighbours.clear();
    for (int k = I(0, 6); k > 0; --k)
        g_da.neighbours.push_back(uint16_t(I(0, 7)));
    g_cs.answer = P(0.5f);
    // (The weapons' model frame: the fire's shot dummy reads it unchecked, as part 41 found.)
    g_cs.itemFrame = g_shdumParent;
    // The speech (part 23's: a set or none, the delay, the code and its sample's length).
    h->m_speechSet = P(0.1f) ? nullptr : reinterpret_cast<C_SpeechSet *>(g_speechSetBytes2);
    s->m_speechDelay = P(0.7f) ? 0 : I(-5, 2000);
    g_cs.speech = P(0.1f) ? I(-3, 0) : I(0, 40000);
    g_cs.sampleSize = P(0.1f) ? 0 : I(1, 3000);
    // The AI's controls (UpdateTimers clears them).
    for (float &v : g_aiAxes)
        v = P(0.6f) ? 0.0f : U(-1, 1);
    for (float &v : g_aiButtons)
        v = P(0.7f) ? 0.0f : 1.0f;
    // (HasSeenEnemy: part 21's answer, for every human.)
    g_combat.seenEnemy = P(0.5f);
    // (A search with nobody in view now and then: with enemies in view it hands over to the fighting at once.)
    if (s->m_task && s->m_task->type == 2 && !aiming && P(0.6f))
        h->m_seenEnemies.clear();
    // (FindNearest: a point or none, 0xFFFF: the original's answer is 16 bits.)
    g_cs.nearest = P(0.3f) ? CHECKPOINT_NONE : I(0, 7);
    g_enMaxAmount = P(0.4f) ? g_ws.amount : float(I(0, 40));
    for (int t : {1, 2, 3, 5, 6})
        EnPrimeTaskPool(t);
    // (Part 23's actors as the events' sources only where the human cannot man a gun: PickGunnerTarget takes a source
    // for a human unchecked, on both sides.)
    if (h->m_vehicle || (s->m_task && s->m_task->type == 6))
        for (S_HumanEvent &ev : g_daEv)
            if (ev.sourceId >= 100 && ev.sourceId < 100 + ACTOR_KIND_COUNT)
                ev.sourceId = humans[I(0, 2)]->GetNetId();
    // (The gunner's events name human attackers: the original reads a non-human's health in GunnerEvent, the port's fix.)
    if ((h->m_vehicle || (s->m_task && s->m_task->type == 6)) && g_en.attacker && g_en.attacker->GetType() != ACTOR_HUMAN)
        g_en.attacker = humans[I(0, 2)];
    // (A body's event names a known human: the original reads a missing one's events in StartSearch, a crash the port
    // fixes.)
    for (S_HumanEvent &ev : g_daEv) {
        if (ev.type != AI_EVENT_BODY_HEARD && ev.type != AI_EVENT_BODY_SEEN)
            continue;
        C_Human *const all[3] = {h, PortOther(), PortThird()};
        C_Human *body = all[I(0, 2)];
        for (int k = 0; k < 3 && body->GetNetId() == g_daGoneId; ++k)
            body = all[k];
        ev.actorId = body->GetNetId();
    }
    DaRandomPoints(s);
}

// The next task of `type` of each side's pool alike, all its fields (the allocations leave some as the pool object
// had them: AllocTask2's +0x40).
void EnPrimeTaskPool(int type)
{
    S_AITask *t = AllocTask(type);
    RandomAiTask(t);
    uint8_t *o = OrigTask(type);
    for (const S_AiRun &r : TaskRuns(type))
        CopyMapped(reinterpret_cast<const uint8_t *>(t) + r.port, o + r.orig, r.size);
    ReleaseTask(t);
    typedef void(AC * T_Release)(void *, void *);
    OriginalAt<T_Release>(0x00582c50)(AiPools(), o);
}

// A weapon in the hands where the call may reach the guard's or the combat's points (a task of type 1, 2 or 3, or none:
// ChooseTask starts one): the original's CanFireMoving reads the behaviour of a missing one, a crash the port fixes.
void EnArm(S_En *s, bool always = false)
{
    if (always || !s->m_task || s->m_task->type <= 3) {
        // (The hands container's too: UpdateHandsItem takes the item from it.)
        if (!s->m_handsItem)
            s->m_handsItem = reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
        g_handsWeapon = s->m_handsItem;
        g_wornContainers[ITEM_CONTAINER_HANDS].item = g_handsWeapon;
    }
}

// The health now and then at 49, 50 or 51 % (the modes' limits).
void EnHealthEdge()
{
    if (P(0.4f)) {
        C_Human *h = g_h.human;
        h->m_initialStats[STAT_HEALTH] = 200;
        h->m_stats[STAT_HEALTH] = 98 + 2 * I(0, 2);
    }
}

int EnDt() { return P(0.1f) ? 0 : P(0.1f) ? I(1000, 6000) : I(1, 200); }
S_HumanEvent *EnAnEvent() { return &g_daEv[I(0, DA_EVENTS - 1)]; }
int EnHuman(const void *p, bool orig) { return DaWord(p, orig); }

void TestEnemyA()
{
    std::printf("enemy: life cycle, events, the task's choice, weapons, firing, grenades, saves\n");
    C_Human *h = g_h.human;
    for (int i = 0; i < 30000 / QUICK; ++i) {
        const int which = I(0, 45);
        RandomEnemy(which == 3 || which == 11);
        S_En *s = En();
        const int dt = EnDt();
        switch (which) {
        case 0: {
            C_HumanController *previous = P(0.3f) ? nullptr : P(0.3f) ? static_cast<C_HumanController *>(g_pc2)
                                                     : P(0.3f) ? static_cast<C_HumanController *>(g_pa) : EnScripted();
            g_h.frame->SetData(&g_daFrameData);
            S_EnSounds sounds;
            sounds.Make();
            EnCall("Attach", [&] { s->Attach(h, previous); return 0; },
                   [&](void *) {
                       MsvcVectorPut(o_human + 0x2A8, sounds.orig);
                       OAi<void>(0x005478c0, static_cast<void *>(o_human), previous ? ToOrig(previous) : nullptr);
                       return 0;
                   });
            sounds.Free();
            break;
        }
        case 1:
            EnCall("Detach", [&] { s->Detach(); return 0; }, [&](void *) { OAi<void>(0x00547cf0); return 0; });
            break;
        case 2:
            EnCall("Update", [&] { s->Update(dt); return 0; }, [&](void *) { OAi<void>(0x00548130, dt); return 0; });
            break;
        case 3:
            // (A home point: StartGuard without one leaves no task, and a burst ending then reads the missing task's
            // state on both sides.)
            if (s->*DaGet(EnHomeCheckpoint()) < 0)
                s->*DaGet(EnHomeCheckpoint()) = I(0, 7);
            // (No reload asked: a hands item dropped in the tick leaves the order's reload to the original's unchecked
            // read, part 36's.)
            g_pa->m_fireOrder->reload = false;
            EnArm(s);
            EnApproachSafe(s);
            EnCall("Tick", [&] { s->Tick(dt); return 0; }, [&](void *) { OAi<void>(0x00548170, dt); return 0; });
            break;
        case 4: {
            static const int modes[] = {MOVE_AI_AGGRESSIVE, MOVE_AI_DEFENSIVE, MOVE_AI_ZOMBIE, 0, 0x800};
            const int mode = modes[I(0, 4)];
            EnHealthEdge();
            EnCall("SetAIMode", [&] { s->SetAIMode(mode); return 0; }, [&](void *) { OAi<void>(0x00558400, mode); return 0; });
            break;
        }
        case 5: {
            S_HumanEvent *ev = EnAnEvent();
            EnCall("HandleEvent", [&] { return DaEventIndex(s->HandleEvent(ev)); },
                   [&](void *) { return DaEventIndex(OAi<void *>(0x00548490, static_cast<void *>(ev))); });
            break;
        }
        case 6:
            EnCall("IsSurrendered", [&] { return int(s->IsSurrendered()); }, [&](void *) { return int(OAi<uint32_t>(0x00558e20) & 0xFF); });
            break;
        case 7:
            EnCall("UnknownE8", [&] { return s->UnknownE8(); }, [&](void *) { return OAi<int>(0x005a4460); });
            break;
        case 8:
            EnCall("ReleaseTask", [&] { s->ReleaseTask(); return 0; }, [&](void *) { OAi<void>(0x00548040); return 0; });
            break;
        case 9:
            if (P(0.6f)) {
                s->m_moveFlags &= ~uint32_t(MOVE_FIRING | MOVE_LYING);
                s->*DaGet(EnGrenadeTime()) = I(10000, 60000);
                s->*DaGet(EnAlertTime()) = I(1, 9000);
                g_cs.beltItem = reinterpret_cast<C_Item *>(g_weaponPort[1]);
                const S_vector own = g_h.frame->GetWorldPos();
                const S_vector a = own + S_vector(U(-45, 45), U(-1, 1), U(-45, 45));
                g_otherFrame->SetPosInline(a);
                g_asThirdFrame->SetPosInline(P(0.5f) ? a + S_vector(U(-8, 8), 0, U(-8, 8)) : own + S_vector(U(-45, 45), U(-1, 1), U(-45, 45)));
                UpdateTree(g_otherFrame);
                UpdateTree(g_asThirdFrame);
                h->m_seenEnemies.clear();
                for (int k = I(1, 3); k > 0; --k)
                    h->m_seenEnemies.push_back(k == 1 ? PortOther() : PortThird());
            }
            EnCall("PlanGrenade", [&] { return int(s->PlanGrenade()); }, [&](void *) { return int(OAi<uint32_t>(0x005584b0) & 0xFF); });
            break;
        case 10: {
            const S_vector dir = P(0.05f) ? S_vector(0, 0, 0)
                                 : S_vector(U(-1, 1), U(-0.3f, 0.3f), U(-1, 1)) * (P(0.1f) ? 1e-7f : 1.0f);
            // (The friends in view: the humans, now and then the tested one or dead.)
            h->m_seenFriends.clear();
            for (int k = P(0.2f) ? 0 : I(1, 3); k > 0; --k)
                h->m_seenFriends.push_back(P(0.15f) ? h : P(0.5f) ? PortOther() : PortThird());
            if (P(0.7f)) {
                const S_vector from = h->GetBodyFrame()->GetWorldPos();
                S_vector ahead = dir;
                ahead.Normalize();
                g_otherFrame->SetPosInline(from + ahead * U(-10, 60) + S_vector(U(-3, 3), 0, U(-3, 3)));
                UpdateTree(g_otherFrame);
                for (int k = 0; k < 4; ++k) {
                    S_vector4 &sp = g_enSpheres[k];
                    const float r = U(0.2f, 2.0f);
                    const S_vector c = from + dir * (200.0f * U(-0.1f, 1.1f)) + S_vector(U(-1.5f, 1.5f), U(-1.5f, 1.5f), U(-1.5f, 1.5f)) * r;
                    sp = ToVector4(c, r);
                }
            }
            EnCall("Unknown114", [&] { return int(s->Unknown114(&dir)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00558e50, &dir) & 0xFF); });
            break;
        }
        case 11:
            // (A task: a burst ending reads its state unchecked, both sides.)
            if (!s->m_task) {
                s->m_task = AllocTask(I(0, 1) ? 3 : 5);
                RandomAiTask(s->m_task);
            }
            EnCall("UpdateFiring", [&] { (s->*DaGet(EnUpdateFiring()))(dt); return 0; }, [&](void *) { OAi<void>(0x00548080, dt); return 0; });
            break;
        case 12: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x00547ad0, 0x00547af0, 0x00547bf0};
            static const char *const names[] = {"SetupAggressive", "SetupDefensive", "SetupZombie"};
            EnCall(names[k],
                   [&] {
                       return int(k == 0 ? (s->*DaGet(EnSetupAggressive()))() : k == 1 ? (s->*DaGet(EnSetupDefensive()))() : (s->*DaGet(EnSetupZombie()))());
                   },
                   [&](void *) { return int(OAi<uint32_t>(at[k]) & 0xFF); });
            break;
        }
        case 13: {
            // The previous controller: none, the second base one, the enemy itself, or a scripted one with its alarm (a
            // raw object each side: the original reads its type +0x48, the alarm's position +0x140 and kind +0x1B4).
            C_HumanController *previous = P(0.15f) ? nullptr : P(0.2f) ? static_cast<C_HumanController *>(g_pc2) : P(0.2f) ? static_cast<C_HumanController *>(g_pa)
                                                                                                                     : EnScripted();
            // The sounds the human heard (each side's copies: the original's vector at +0x2A8, set as it runs).
            std::vector<S_RememberedSound *> sounds;
            std::vector<uint32_t> origSounds;
            for (int k = P(0.4f) ? 0 : I(1, 3); k > 0; --k) {
                auto *ps = static_cast<S_RememberedSound *>(_aligned_malloc(sizeof(S_RememberedSound), 16));
                ps->pos = h->GetFrame()->GetWorldPos() + S_vector(U(-30, 30), U(-2, 2), U(-30, 30));
                ps->posW = 1.0f;
                ps->chapter = I(0, 10);
                // (No source only for chapters 8 and up, as part 35's: a turn toward one reads it on both sides.)
                ps->source = P(0.2f) && ps->chapter >= 8 ? nullptr : static_cast<C_Actor *>(P(0.5f) ? PortOther() : PortThird());
                ps->time = uint32_t(I(0, 100000));
                ps->refreshed = P(0.5f);
                auto *os = static_cast<uint8_t *>(_aligned_malloc(sizeof(S_RememberedSound), 16));
                std::memcpy(os, ps, sizeof(S_RememberedSound));
                void *oSource = ps->source ? ToOrig(ps->source) : nullptr;
                std::memcpy(os + offsetof(S_RememberedSound, source), &oSource, 4);
                sounds.push_back(ps);
                origSounds.push_back(EnWordOf(os));
            }
            h->m_heardSounds = sounds;
            EnCall("TakeOver", [&] { return int((s->*DaGet(EnTakeOver()))(previous)); },
                   [&](void *) {
                       MsvcVectorPut(o_human + 0x2A8, origSounds);
                       return int(OAi<uint32_t>(0x005518f0, previous ? ToOrig(previous) : nullptr) & 0xFF);
                   });
            h->m_heardSounds.clear();
            std::memset(o_human + 0x2A8, 0, 16);
            for (size_t k = 0; k < sounds.size(); ++k) {
                _aligned_free(sounds[k]);
                _aligned_free(reinterpret_cast<void *>(static_cast<uintptr_t>(origSounds[k])));
            }
            break;
        }
        case 14: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x00551b00, 0x00551b50, 0x00551ba0};
            static const char *const names[] = {"FindHomeNeighbours", "FindHomeCover", "FindGuns"};
            EnCall(names[k],
                   [&] {
                       if (k == 0)
                           (s->*DaGet(EnFindHomeNeighbours()))();
                       else if (k == 1)
                           (s->*DaGet(EnFindHomeCover()))();
                       else
                           (s->*DaGet(EnFindGuns()))();
                       return 0;
                   },
                   [&](void *) { OAi<void>(at[k]); return 0; });
            break;
        }
        case 15:
            EnCall("ReturnToScript", [&] { (s->*DaGet(EnReturnToScript()))(); return 0; }, [&](void *) { OAi<void>(0x00547d80); return 0; });
            break;
        case 16:
            if (P(0.3f)) {
                h->m_seenEnemies.clear();
                h->m_seenEnemies.push_back(PortOther());
            }
            if (P(0.5f)) {
                S_vector toward = g_h.frame->GetWorldPos() - PortOther()->GetFrame()->GetWorldPos();
                toward.Normalize();
                g_pc2->m_viewDir = ToVector4(toward + S_vector(U(-0.3f, 0.3f), 0, U(-0.3f, 0.3f)), 0.0f);
            }
            EnCall("CheckThreat", [&] { (s->*DaGet(EnCheckThreat()))(); return 0; }, [&](void *) { OAi<void>(0x00547da0); return 0; });
            break;
        case 17: {
            // (The timers at their edges: run out exactly, by one, or 1 left; the alert at 1000 after the step.)
            auto edge = [&](int &t, int to) {
                const int k = I(0, 4);
                t = k == 0 ? dt + to + I(-1, 1) : k == 1 ? I(0, 1) : t;
            };
            edge(s->*DaGet(EnFireWait()), 0);
            edge(s->*DaGet(EnAlertTime()), P(0.5f) ? 1000 : 0);
            edge(s->*DaGet(EnGunCheckTime()), 0);
            edge(s->*DaGet(EnMoraleCheckTime()), 1);
            if (s->m_task)
                edge(s->m_task->unknown10, 0);
            EnCall("UpdateTimers", [&] { (s->*DaGet(EnUpdateTimers()))(dt); return 0; }, [&](void *) { OAi<void>(0x00547e70, dt); return 0; });
            break;
        }
        case 18:
            EnCall("IsLowOnAmmo", [&] { return int((s->*DaGet(EnIsLowOnAmmo()))()); }, [&](void *) { return int(OAi<uint32_t>(0x00548010) & 0xFF); });
            break;
        case 19: {
            S_HumanEvent *ev = EnAnEvent();
            const bool task = P(0.5f);
            // (HandleTaskEvent's answer is not used: the original's is a stale EAX.)
            EnCall(task ? "HandleTaskEvent" : "HandleCommonEvent",
                   [&] {
                       if (task) {
                           (s->*DaGet(EnHandleTaskEvent()))(ev);
                           return 0;
                       }
                       return DaEventIndex((s->*DaGet(EnHandleCommonEvent()))(ev));
                   },
                   [&](void *) {
                       if (task) {
                           OAi<void *>(0x005482a0, static_cast<void *>(ev));
                           return 0;
                       }
                       return DaEventIndex(OAi<void *>(0x005482f0, static_cast<void *>(ev)));
                   });
            break;
        }
        case 20:
            EnHealthEdge();
            EnCall("UpdateMode", [&] { (s->*DaGet(EnUpdateMode()))(); return 0; }, [&](void *) { OAi<void>(0x005484b0); return 0; });
            break;
        case 21:
            EnCall("ChooseTask", [&] { (s->*DaGet(EnChooseTask()))(); return 0; }, [&](void *) { OAi<void>(0x00548520); return 0; });
            break;
        case 22: {
            const int delta = I(-20, 20);
            EnCall("ChangeMorale", [&] { (s->*DaGet(EnChangeMorale()))(delta); return 0; }, [&](void *) { OAi<void>(0x00548580, delta); return 0; });
            break;
        }
        case 23:
            EnCall("HasSeatWeapon", [&] { return int((s->*DaGet(EnHasSeatWeapon()))()); }, [&](void *) { return int(OAi<uint32_t>(0x00548590) & 0xFF); });
            break;
        case 24:
            if (P(0.3f)) {
                s->m_handsItem = nullptr;
                g_handsWeapon = nullptr;
                g_wornContainers[ITEM_CONTAINER_HANDS].item = nullptr;
                s->*DaGet(EnUnarmedTime()) = 1000 + I(-1, 1);
                h->m_stats[STAT_MORALE] = I(20, 100);
            }
            EnCall("UpdateTask", [&] { return DaWord((s->*DaGet(EnUpdateTask()))(dt), false) != 0; },
                   [&](void *) { return OAi<void *>(0x00548610, dt) != nullptr; });
            break;
        case 25:
            EnArm(s);
            EnApproachSafe(s);
            EnCall("RunTask", [&] { (s->*DaGet(EnRunTask()))(dt); return 0; }, [&](void *) { OAi<void>(0x00548840, dt); return 0; });
            break;
        case 26: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x00549360, 0x0054ab00, 0x0054cb70};
            static const char *const names[] = {"StartSearch", "StartCombat", "StartGunner"};
            if (k == 0 && P(0.5f)) {
                if (s->m_task)
                    ReleaseTask(s->m_task);
                s->m_task = nullptr;
                s->*DaGet(EnEvent()) = nullptr;
                s->*DaGet(EnGun()) = nullptr;
                EnPrimeTaskPool(2);   // (the task released went back to the port's pool only)
            }
            if (k == 0 && g_en.strongest >= 0 && P(0.3f)) {
                g_daGoneId = -1000;   // (the body known: the original reads a missing one's events, the port's fix)
                S_HumanEvent &ev = g_daEv[g_en.strongest];
                ev.type = P(0.5f) ? AI_EVENT_BODY_HEARD : AI_EVENT_BODY_SEEN;
                ev.actorId = (P(0.5f) ? PortOther() : PortThird())->GetNetId();
                g_en.attacker = P(0.2f) ? nullptr : static_cast<C_Actor *>(P(0.7f) ? h : PortThird());
            }
            EnCall(names[k],
                   [&] {
                       if (k == 0)
                           (s->*DaGet(EnStartSearch()))();
                       else if (k == 1)
                           (s->*DaGet(EnStartCombat()))();
                       else
                           (s->*DaGet(EnStartGunner()))();
                       return 0;
                   },
                   [&](void *) { OAi<void>(at[k]); return 0; });
            break;
        }
        case 27: {
            static const int stances[] = {MOVE_STANDING, MOVE_CROUCHING, MOVE_LYING, 0, 3};
            const int stance = stances[I(0, 4)];
            EnCall("SetEnemyStance", [&] { (s->*DaGet(EnSetEnemyStance()))(stance); return 0; },
                   [&](void *) { OAi<void>(0x00559360, stance); return 0; });
            break;
        }
        case 28: {
            const int k = I(0, 5);
            static const uint32_t at[] = {0x005593d0, 0x00559420, 0x005594b0, 0x00559570, 0x00559590, 0x00559660};
            static const char *const names[] = {"HasWeapon", "DrawWeapon", "TakeFirearm", "HasLauncherInHands", "TakeLauncher", "HasLauncher"};
            static bool (S_En::*const members[])() = {DaGet(EnHasWeapon()),          DaGet(EnDrawWeapon()),  DaGet(EnTakeFirearm()),
                                                      DaGet(EnHasLauncherInHands()), DaGet(EnTakeLauncher()), DaGet(EnHasLauncher())};
            EnCall(names[k], [&] { return int((s->*members[k])()); }, [&](void *) { return int(OAi<uint32_t>(at[k]) & 0xFF); });
            break;
        }
        case 29:
            EnCall("NearestTargetOnFoot", [&] { return EnHuman((s->*DaGet(EnNearestTargetOnFoot()))(), false); },
                   [&](void *) { return EnHuman(OAi<void *>(0x005596d0), true); });
            break;
        case 30:
            if (P(0.6f)) {
                g_otherFrame->SetPosInline(g_h.frame->GetWorldPos() + S_vector(U(4, 22), 0, 0));
                UpdateTree(g_otherFrame);
                s->*DaGet(EnTarget()) = PortOther();
            }
            EnCall("GetFireWait", [&] { return (s->*DaGet(EnGetFireWait()))(); }, [&](void *) { return OAi<int>(0x00558110); });
            break;
        case 31:
            EnCall("NeedsReload", [&] { return int((s->*DaGet(EnNeedsReload()))()); }, [&](void *) { return int(OAi<uint32_t>(0x00558230) & 0xFF); });
            break;
        case 32: {
            C_Human *who = P(0.2f) ? nullptr : P(0.5f) ? h : P(0.5f) ? PortOther() : PortThird();
            const bool sees = P(0.5f);
            EnCall(sees ? "SeesEnemy" : "TestSightNow",
                   [&] {
                       if (sees)
                           return int((s->*DaGet(EnSeesEnemy()))(who));
                       (s->*DaGet(EnTestSightNow()))(who);
                       return 0;
                   },
                   [&](void *) {
                       void *o = who ? ToOrig(who) : nullptr;
                       if (sees)
                           return int(OAi<uint32_t>(0x005582a0, o) & 0xFF);
                       OAi<void>(0x005582c0, o);
                       return 0;
                   });
            break;
        }
        case 33: {
            const int k = I(0, 1);
            EnCall(k ? "IsFightingOn" : "SeesEnemies",
                   [&] { return int(k ? (s->*DaGet(EnIsFightingOn()))() : (s->*DaGet(EnSeesEnemies()))()); },
                   [&](void *) { return int(OAi<uint32_t>(k ? 0x00558960 : 0x005582f0) & 0xFF); });
            break;
        }
        case 34:
            EnCall("NearestEnemy", [&] { return EnHuman((s->*DaGet(EnNearestEnemy()))(), false); },
                   [&](void *) { return EnHuman(OAi<void *>(0x00558980), true); });
            break;
        case 35:
            EnCall("LookAtNearestEnemy", [&] { return int((s->*DaGet(EnLookAtNearestEnemy()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00558a60) & 0xFF); });
            break;
        case 36:
            EnCall("DropHandsItem", [&] { (s->*DaGet(EnDropHandsItem()))(); return 0; }, [&](void *) { OAi<void>(0x00558aa0); return 0; });
            break;
        case 37:
            EnCall("Attack", [&] { return int((s->*DaGet(EnAttack()))(dt)); }, [&](void *) { return int(OAi<uint32_t>(0x00558ae0, dt) & 0xFF); });
            break;
        case 38: {
            C_Actor *target = P(0.2f) ? nullptr : P(0.7f) ? static_cast<C_Actor *>(P(0.5f) ? PortOther() : PortThird())
                                                          : static_cast<C_Actor *>(PortActor(I(0, 6)));
            // (Now and then at the tables' edges: 5, 10, 15, 20, 25, 50 and 100 m.)
            if (P(0.4f) && target) {
                static const float edges[] = {5, 10, 15, 20, 25, 50, 100};
                const float d = edges[I(0, 6)] + (P(0.5f) ? 0.0f : U(-0.01f, 0.01f));
                const S_vector at = g_h.frame->GetWorldPos();
                const S_vector whole(std::floor(at.x), at.y, std::floor(at.z));
                g_h.frame->SetPosInline(whole);
                UpdateTree(g_h.frame);
                g_otherFrame->SetPosInline(whole + S_vector(d, 0, 0));
                UpdateTree(g_otherFrame);
                target = PortOther();
            }
            EnCall("SetBurstTimes", [&] { (s->*DaGet(EnSetBurstTimes()))(target); return 0; },
                   [&](void *) { OAi<void>(0x00558bd0, target ? ToOrig(target) : nullptr); return 0; });
            break;
        }
        case 39: {
            C_Human *who = P(0.1f) ? nullptr : P(0.5f) ? PortOther() : PortThird();
            typedef int(__stdcall * T_Shielded)(void *);
            EnCall("IsShieldedInVehicle", [&] { return int(IsShieldedInVehicle(who)); },
                   [&](void *) { return int(OriginalAt<T_Shielded>(0x00559610)(who ? ToOrig(who) : nullptr) & 0xFF); });
            break;
        }
        case 40: {
            // (An interaction has its target, as part 33's: the original saves the target's id without a check.)
            static const int kinds[] = {0, 1, 2, 9, 11, KIND_HUMAN};
            if (P(0.6f)) {
                s->m_interaction = P(0.2f) ? HUMAN_COMMAND_HEAL_SELF : I(0, 0x26);
                s->m_interactionTarget = static_cast<C_Actor *>(PortActor(kinds[I(0, 5)]));
            } else {
                s->m_interaction = -1;
            }
            if (P(0.2f)) {
                if (s->m_task)
                    ReleaseTask(s->m_task);
                s->m_task = AllocTask(1);
                RandomAiTask(s->m_task);
                s->m_task->unknown08 = S_En::GUARD_TO_GUN;
                s->*DaGet(EnEvent()) = nullptr;
                s->*DaGet(EnGun()) = P(0.9f) ? static_cast<C_Actor *>(PortActor(I(3, 6))) : nullptr;
                EnPrimeTaskPool(1);   // (the pools' next tasks alike again)
            }
            const std::string portFile = AiTempFile("_enport.sav"), origFile = AiTempFile("_enorig.sav");
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            auto save = [&](const std::string &file, bool orig) {
                C_chunk c;
                c.Open(file.c_str(), true);
                c.Begin(0x7777);
                if (orig)
                    OAi<void>(0x0054c000, &c);
                else
                    s->SaveGame(c);
                c.Ascend();
                c.Begin(0x0BD1);
                c.Ascend();
                return 0;
            };
            EnCall("SaveGame", [&] { return save(portFile, false); }, [&](void *) { return save(origFile, true); });
            const bool same = AiFileBytes(portFile) == AiFileBytes(origFile);
            CHECK(same);
            static int shown;
            if (!same && shown++ < 3)
                std::printf("  SaveGame: the files differ\n");
            // (Now and then an older game's: the surrender time alone, chunk 0x13B6, after the saved chunks.)
            if (P(0.2f)) {
                std::remove(portFile.c_str());
                C_chunk c;
                c.Open(portFile.c_str(), true);
                c.Begin(0x7777);
                if (P(0.5f)) {
                    // (A state chunk cut after its first fields: those read only while they fit.)
                    c.Begin(0x13B1);
                    const bool lowAmmo = P(0.5f), unknown154 = P(0.5f);
                    c.Write(lowAmmo);
                    c.Write(unknown154);
                    for (int k = I(0, 5); k > 0; --k) {
                        const int v = I(-5, 5000);
                        c.Write(v);
                    }
                    c.Ascend();
                }
                c.Begin(0x13B6);
                const int surrender = P(0.5f) ? -1 : I(0, 100000);
                c.Write(surrender);
                c.Ascend();
                if (P(0.5f)) {
                    c.Begin(0x13B8);
                    const bool changed = P(0.5f);
                    c.Write(changed);
                    c.Ascend();
                }
                c.Ascend();
                c.Begin(0x0BD1);
                c.Ascend();
            }
            RandomEnemy(false);
            // (A controller loaded has neither a search's event nor a guard's gun: the original's one field.)
            s->*DaGet(EnEvent()) = nullptr;
            s->*DaGet(EnGun()) = nullptr;
            // (Every saved actor known: a task naming an unknown one keeps its pool object's, which differ per side.)
            g_daGoneId = -1000;
            auto load = [&](bool orig) {
                C_chunk c;
                c.Open(portFile.c_str(), false);
                uint16_t id;
                c.Descend(id);
                if (orig)
                    OAi<void>(0x0054c430, &c);
                else
                    s->LoadGame(c);
                int depth;
                std::memcpy(&depth, reinterpret_cast<const uint8_t *>(&c) + 0x410, 4);
                const int at = c.GetPosition();
                c.Ascend();
                return depth * 0x10000 + (at & 0xFFFF) + AiNextChunk(c) * 0x100000;
            };
            EnCall("LoadGame", [&] { return load(false); }, [&](void *) { return load(true); });
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            break;
        }
        default: {
            // HandleEvent's paths with the task's handlers: an event of the task's kinds.
            S_HumanEvent *ev = EnAnEvent();
            EnCall("HandleEvent", [&] { return DaEventIndex(s->HandleEvent(ev)); },
                   [&](void *) { return DaEventIndex(OAi<void *>(0x00548490, static_cast<void *>(ev))); });
            break;
        }
        }
    }
}

// A place for the turns and searches: about the human, every side, now and then where it stands or exactly ahead.
S_vector4 EnPlace()
{
    const S_vector &own = g_h.frame->GetWorldPos();
    if (P(0.05f))
        return ToVector4(own, 1.0f);
    if (P(0.1f)) {
        const S_matrix &m = g_h.human->GetBodyFrame()->GetMatrix();
        return ToVector4(own + S_vector(m.m[2][0], 0, m.m[2][2]) * U(1, 30), 1.0f);
    }
    return ToVector4(own + S_vector(U(-40, 40), U(-2, 2), U(-40, 40)), U(-2, 2));
}
// A point index answer as a 16-bit word (the original's are; the port's int the same for 0..0xFFFF).
int EnPoint(int p) { return p & 0xFFFF; }

void TestEnemyB()
{
    std::printf("enemy: guard, search, morale broken\n");
    for (int i = 0; i < 30000 / QUICK; ++i) {
        const int which = I(0, 23);
        // (The handlers and the updates with their own task: the game calls them so.)
        static const int taskOf[24] = {0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0};
        RandomEnemy(false, taskOf[which]);
        S_En *s = En();
        const int dt = EnDt();
        // (The search's own: an event to search for, as SearchEvent sets it first.)
        if (which >= 5 && which <= 13 && which != 11 && !(s->*DaGet(EnEvent())))
            s->*DaGet(EnEvent()) = EnAnEvent();
        switch (which) {
        case 0: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x00553cc0, 0x00553ce0, 0x00554760};
            static const char *const names[] = {"IsNotClimbing", "IsOutdoors", "IsLightWeapon"};
            static bool (S_En::*const members[])() = {DaGet(EnIsNotClimbing()), DaGet(EnIsOutdoors()), DaGet(EnIsLightWeapon())};
            EnCall(names[k], [&] { return int((s->*members[k])()); }, [&](void *) { return int(OAi<uint32_t>(at[k]) & 0xFF); });
            break;
        }
        case 1: {
            const int k = I(0, 1);
            EnCall(k ? "RunForCover" : "WalkCarefully",
                   [&] {
                       (s->*(k ? DaGet(EnRunForCover()) : DaGet(EnWalkCarefully())))();
                       return 0;
                   },
                   [&](void *) { OAi<void>(k ? 0x00554800 : 0x005547b0); return 0; });
            break;
        }
        case 2: {
            const int k = I(0, 1);
            EnCall(k ? "StartBroken" : "StartGuard",
                   [&] {
                       (s->*(k ? DaGet(EnStartBroken()) : DaGet(EnStartGuard())))();
                       return 0;
                   },
                   [&](void *) { OAi<void>(k ? 0x0054b730 : 0x00548890); return 0; });
            break;
        }
        case 3: {
            S_HumanEvent *ev = EnAnEvent();
            EnCall("GuardEvent", [&] { (s->*DaGet(EnGuardEvent()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x00548900, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 4:
            EnArm(s);
            // (Running now and then: the hiding's walk again.)
            if (P(0.3f))
                s->m_moveFlags |= MOVE_PACE_MARCH;
            EnCall("UpdateGuard", [&] { (s->*DaGet(EnUpdateGuard()))(dt); return 0; }, [&](void *) { OAi<void>(0x00548b00, dt); return 0; });
            break;
        case 5: {
            S_HumanEvent *ev = EnAnEvent();
            if (P(0.3f))
                s->*DaGet(EnEvent()) = nullptr;
            EnCall("SearchEvent", [&] { (s->*DaGet(EnSearchEvent()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x00549940, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 6: {
            S_HumanEvent *ev = EnAnEvent();
            EnCall("IsSameSource", [&] { return int((s->*DaGet(EnIsSameSource()))(ev)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00549700, static_cast<void *>(ev)) & 0xFF); });
            break;
        }
        case 7: {
            S_HumanEvent *ev = EnAnEvent();
            // (Now and then walking: the comparisons of the old place and the new.)
            if (P(0.5f))
                s->m_task->unknown08 = S_En::SEARCH_WALK;
            EnCall("SearchSameSource", [&] { (s->*DaGet(EnSearchSameSource()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x00549760, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 8: {
            S_HumanEvent *ev = EnAnEvent();
            EnCall("SetSearchEvent", [&] { (s->*DaGet(EnSetSearchEvent()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x00554c30, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 9: {
            S_HumanEvent *ev = EnAnEvent();
            if (P(0.5f))
                s->m_task->unknown08 = S_En::SEARCH_WALK;
            EnCall("SearchStrongerEvent", [&] { (s->*DaGet(EnSearchStrongerEvent()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x00554d20, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 10: {
            S_HumanEvent *ev = EnAnEvent();
            if (P(0.4f))
                s->m_task->unknown08 = S_En::SEARCH_WALK;
            EnCall("SearchFriendHeard", [&] { (s->*DaGet(EnSearchFriendHeard()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x00554e20, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 11:
            // (Points 10..20 m away now and then: the walk around the source finds one.)
            if (P(0.4f))
                for (int k = 0; k < 8; ++k)
                    if (P(0.5f)) {
                        const float d = U(10, 20), a = U(0, 6.2831f);
                        reinterpret_cast<S_Checkpoint *>(g_aiPoints[k])->pos = g_h.frame->GetWorldPos() + S_vector(std::cos(a) * d, 0, std::sin(a) * d);
                    }
            if (P(0.4f)) {
                g_h.human->m_seenEnemies.clear();
                if (!(s->*DaGet(EnEvent())))
                    s->*DaGet(EnEvent()) = EnAnEvent();
                S_HumanEvent *ev = s->*DaGet(EnEvent());
                const int k = I(0, 2);
                if (k == 0) {
                    // (Walking, the event 1..14 m away, at a pace that slows down near it.)
                    s->m_task->unknown08 = S_En::SEARCH_WALK;
                    s->m_move->flags = (s->m_move->flags | S_AIMoveOrder::FLAG_ROUTE) & ~S_AIMoveOrder::FLAG_DONE;
                    static const uint32_t paces[] = {0x10008, 0x4008, 0x2008, 0x1008};
                    s->m_move->unknown54 = paces[I(0, 3)];
                    s->m_weaponRaised = P(0.5f);
                    s->m_lookAroundTime = P(0.5f) ? 0 : I(1, 3000);
                    const float d = U(1, 14), a = U(0, 6.2831f);
                    ev->pos = ToVector4(g_h.frame->GetWorldPos() + S_vector(std::cos(a) * d, U(-0.5f, 0.5f), std::sin(a) * d), 1.0f);
                    // (Now and then its source a body seen: the walk ends.)
                    if (P(0.3f)) {
                        g_h.human->m_seenBodies.push_back(PortOther());
                        ev->sourceId = PortOther()->GetNetId();
                    }
                } else if (k == 1) {
                    s->m_task->unknown08 = S_En::SEARCH_CLIMBING;
                    s->m_moveFlags &= ~MOVE_ON_LADDER;
                    if (P(0.5f))
                        s->*DaGet(EnEvent()) = nullptr;
                } else {
                    s->m_task->unknown08 = S_En::SEARCH_TO_SOURCE;
                    s->m_move->flags |= S_AIMoveOrder::FLAG_DONE;
                }
            } else if (P(0.2f)) {
                // (Aggressive, outdoors, to a point toward an event far away: runs.)
                g_h.human->m_seenEnemies.clear();
                if (!(s->*DaGet(EnEvent())))
                    s->*DaGet(EnEvent()) = EnAnEvent();
                S_HumanEvent *ev = s->*DaGet(EnEvent());
                ev->sourceId = PortOther()->GetNetId();
                g_daGoneId = -1000;
                const float d = U(55, 100), a = U(0, 6.2831f);
                ev->pos = ToVector4(g_h.frame->GetWorldPos() + S_vector(std::cos(a) * d, 0, std::sin(a) * d), 1.0f);
                s->m_task->unknown08 = S_En::SEARCH_TO_POINT;
                s->m_moveFlags = (s->m_moveFlags & ~uint32_t(MOVE_AI_MODE_MASK)) | MOVE_AI_AGGRESSIVE;
                g_en.outdoors = true;
                g_en.nearestPoint = I(0, 7);
            }
            EnCall("UpdateSearch", [&] { (s->*DaGet(EnUpdateSearch()))(dt); return 0; }, [&](void *) { OAi<void>(0x00549ae0, dt); return 0; });
            break;
        case 12:
            EnCall("FindSearchPoint", [&] { return EnPoint((s->*DaGet(EnFindSearchPoint()))()); },
                   [&](void *) { return EnPoint(OAi<int>(0x00553eb0)); });
            break;
        case 13:
            // (Now and then a point off the network: Get answers none, which FindSearchAround skips.)
            if (P(0.3f))
                g_en.reachable.push_back(uint16_t(I(8, 9)));
            EnCall("FindSearchAround", [&] { return EnPoint((s->*DaGet(EnFindSearchAround()))()); },
                   [&](void *) { return EnPoint(OAi<int>(0x005541f0)); });
            break;
        case 14: {
            const S_vector4 pos = EnPlace();
            EnCall("LookTowardIfAside", [&] { return int((s->*DaGet(EnLookTowardIfAside()))(pos)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00554840, &pos) & 0xFF); });
            break;
        }
        case 15: {
            const S_vector4 pos = EnPlace();
            EnCall("LookAroundToward", [&] { (s->*DaGet(EnLookAroundToward()))(pos); return 0; },
                   [&](void *) { OAi<void>(0x00557f70, &pos); return 0; });
            break;
        }
        case 16: {
            // (Now and then the origin: none.)
            const S_vector4 pos = P(0.1f) ? S_vector4{0, 0, 0, 1} : EnPlace();
            EnCall("FindCoverToward", [&] { return EnPoint((s->*DaGet(EnFindCoverToward()))(pos)); },
                   [&](void *) { return EnPoint(OAi<int>(0x00557540, &pos)); });
            break;
        }
        case 17: {
            const S_vector4 pos = EnPlace();
            std::vector<uint16_t> points;
            for (int k = P(0.15f) ? 0 : I(1, 6); k > 0; --k)
                points.push_back(uint16_t(I(0, 7)));
            alignas(16) uint8_t orig[16];
            EnVectorPut(orig, points);
            float portD = -5.0f, origD = -5.0f;
            EnCall("NearestPoint",
                   [&] {
                       const int r = EnPoint((s->*DaGet(EnNearestPoint()))(pos, points, &portD));
                       return r;
                   },
                   [&](void *) { return EnPoint(OAi<int>(0x00557d10, &pos, static_cast<void *>(orig), &origD)); });
            CHECK(std::memcmp(&portD, &origD, 4) == 0);
            uint32_t w[4];
            std::memcpy(w, orig, 16);
            std::free(reinterpret_cast<void *>(static_cast<uintptr_t>(w[1])));
            break;
        }
        case 18: {
            S_HumanEvent *ev = EnAnEvent();
            // (Now and then shot at while surrendering, by a shooter in view, with no weapon to fight with: flees.)
            if (P(0.2f)) {
                s->m_task->unknown08 = S_En::BROKEN_SURRENDERED;
                ev->type = AI_EVENT_SHOT_AT;
                ev->sourceId = PortOther()->GetNetId();
                g_daGoneId = -1000;
                g_combat.seenEnemy = true;
                s->m_handsItem = nullptr;
                g_en.recordItem = P(0.3f);
            }
            EnCall("BrokenEvent", [&] { (s->*DaGet(EnBrokenEvent()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x0054b770, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 19:
            if (P(0.4f)) {
                s->m_task->unknown08 = S_En::BROKEN_START;
                g_h.human->m_seenEnemies.clear();
                g_h.human->m_seenEnemies.push_back(P(0.5f) ? PortOther() : PortThird());
            }
            EnCall("UpdateBroken", [&] { (s->*DaGet(EnUpdateBroken()))(dt); return 0; }, [&](void *) { OAi<void>(0x0054b8f0, dt); return 0; });
            break;
        default:
            // The tasks run through RunTask (each type's update with its handlers' states).
            EnArm(s);
            EnApproachSafe(s);
            EnCall("RunTask", [&] { (s->*DaGet(EnRunTask()))(dt); return 0; }, [&](void *) { OAi<void>(0x00548840, dt); return 0; });
            break;
        }
    }
}

// The target out of the band where FindApproachPoint's list runs below empty in the original (the port's fix): a
// point too far from the target and from the human, or too far from it and within 6 m of it, is dropped twice. With
// the target 24 m away or more (the network's points are within 17 m of the human) none is; within the weapon's reach
// FindCombatPoint does not ask (the combat's call: the target beyond its farthest).
void EnApproachSafe(S_En *s)
{
    // (Every human the call may pick: the target, the others in view.)
    float nearest, farthest;
    (s->*DaGet(EnGetWeaponRange()))(&nearest, &farthest);
    const S_vector own = g_h.frame->GetWorldPos();
    for (C_Human *t : {PortOther(), PortThird()}) {
        I3D_frame *f = t->GetFrame();
        const float d = (f->GetWorldPos() - own).Magnitude();
        if (d <= farthest || d >= 24.0f || d < 1e-3f)
            continue;
        const S_vector dir = (f->GetWorldPos() - own).Normalized();
        f->SetPosInline(own + dir * (farthest >= 24.0f ? U(0.5f, farthest) : U(24, 60)));
        UpdateTree(f);
    }
}

// FindApproachPoint's removals for `target` (its port's arithmetic) within the list PickCoverPoint leaves.
bool EnApproachFits(C_Human *target)
{
    const std::vector<uint16_t> points = EnPicked(g_en.reachable);
    const S_vector own = g_h.frame->GetWorldPos();
    const S_vector pos = target->GetFrame()->GetWorldPos();
    const float distance = (own - pos).Magnitude();
    size_t removed = 0;
    for (uint16_t index : points) {
        if (index >= 8)
            continue;
        const auto *point = reinterpret_cast<const S_Checkpoint *>(g_aiPoints[index]);
        const float fromHuman = (own - point->pos).Magnitude();
        const float fromTarget = (pos - point->pos).Magnitude() - static_cast<float>(point->radius) * 0.0625f;
        removed += fromTarget > distance;
        removed += fromTarget < 6.0f || fromHuman > distance;
    }
    // (A margin of one: the comparisons' rounding.)
    return removed + 1 < points.size() || removed == 0;
}

// The other human as the target `d` m away (every side, now and then level or straight up), in a vehicle now and
// then (the seat car, turned: the vehicle points' fronts).
C_Human *EnTargetAt(float d, bool vehicle)
{
    const S_vector own = g_h.frame->GetWorldPos();
    const float a = U(0, 6.2831f);
    const float up = P(0.3f) ? 0.0f : U(-0.3f, 0.3f);
    g_otherFrame->SetPosInline(own + S_vector(std::cos(a) * d, up * d, std::sin(a) * d));
    UpdateTree(g_otherFrame);
    C_Human *t = PortOther();
    t->m_vehicle = vehicle ? reinterpret_cast<C_Actor *>(g_asCarPort) : nullptr;
    void *ov = vehicle ? static_cast<void *>(g_asCarOrig) : nullptr;
    std::memcpy(g_origOther + 0x160, &ov, 4);
    if (vehicle) {
        g_targetFrame->SetPosInline(g_otherFrame->GetWorldPos() + S_vector(U(-2, 2), U(-1, 1), U(-2, 2)));
        S_quat turn;
        // (Now and then tilted: the fronts' height left out.)
        turn.Make(P(0.3f) ? S_vector(U(-0.3f, 0.3f), 1, U(-0.3f, 0.3f)).Normalized() : S_vector(0, 1, 0), U(0, 6.2831f));
        g_targetFrame->SetRot(turn);
        UpdateTree(g_targetFrame);
    }
    En()->*DaGet(EnTarget()) = t;
    return t;
}
float EnDistance() { return P(0.15f) ? float(I(1, 8) * 5) : U(1, 90); }

void TestEnemyC()
{
    std::printf("enemy: combat, the weapon's range, stances, points to fight from\n");
    for (int i = 0; i < 30000 / QUICK; ++i) {
        const int which = I(0, 23);
        static const int taskOf[24] = {3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 3, 3};
        RandomEnemy(false, taskOf[which]);
        S_En *s = En();
        C_Human *h = g_h.human;
        const int dt = EnDt();
        // (A target where the call measures to it unchecked, on both sides; a weapon where CanFireMoving reads its
        // behaviour unchecked in the original.)
        static const bool needsTarget[24] = {false, false, true, true, false, false, false, false, true, true, false, false, true,
                                             false, false, true, false, false, false, false, false, false, false, false};
        if (needsTarget[which] || P(0.5f))
            EnTargetAt(EnDistance(), P(0.4f));
        if (which == 3 || which == 7 || which == 12 || which == 15 || which == 18)
            EnArm(s, true);
        const S_Checkpoint *cp = P(0.15f) ? nullptr : reinterpret_cast<const S_Checkpoint *>(g_aiPoints[I(0, 7)]);
        switch (which) {
        case 0: {
            S_HumanEvent *ev = EnAnEvent();
            EnCall("CombatEvent", [&] { (s->*DaGet(EnCombatEvent()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x0054ab70, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 1:
            EnArm(s);
            if (P(0.4f)) {
                s->m_task->unknown08 = S_En::COMBAT_APPROACH;
                s->m_task->unknown10 = 0;
                s->m_task->unknown0C = P(0.5f) ? I(0, 3999) : I(4000, 8000);
                s->m_move->flags |= S_AIMoveOrder::FLAG_ROUTE;
                if (!(s->*DaGet(EnTarget())))
                    EnTargetAt(EnDistance(), false);
            }
            EnApproachSafe(s);
            EnCall("UpdateCombat", [&] { (s->*DaGet(EnUpdateCombat()))(dt); return 0; }, [&](void *) { OAi<void>(0x0054ac40, dt); return 0; });
            break;
        case 2:
            EnCall("CompareRange", [&] { return (s->*DaGet(EnCompareRange()))(); }, [&](void *) { return OAi<int>(0x00551db0); });
            break;
        case 3:
            EnApproachSafe(s);
            EnCall("FindCombatPoint", [&] { return EnPoint((s->*DaGet(EnFindCombatPoint()))()); },
                   [&](void *) { return EnPoint(OAi<int>(0x00551e10)); });
            break;
        case 4: {
            float pn = -1, pf = -1, on = -1, of = -1;
            EnCall("GetWeaponRange", [&] { (s->*DaGet(EnGetWeaponRange()))(&pn, &pf); return 0; },
                   [&](void *) { OAi<void>(0x00551e80, &on, &of); return 0; });
            CHECK(pn == on && pf == of);
            break;
        }
        case 5: {
            const bool mx = P(0.5f);
            EnCall(mx ? "GetMaxDistance" : "GetMinDistance",
                   [&] { return FloatBitsOf(mx ? (s->*DaGet(EnGetMaxDistance()))() : (s->*DaGet(EnGetMinDistance()))()); },
                   [&](void *) {
                       typedef float(AC * T)(void *);
                       return FloatBitsOf(OriginalAt<T>(mx ? 0x00552040 : 0x00551fc0)(g_oa));
                   });
            break;
        }
        case 6:
            EnCall("IsInWeaponRange", [&] { return int((s->*DaGet(EnIsInWeaponRange()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x005520e0) & 0xFF); });
            break;
        case 7:
            EnCall("CanFireMoving", [&] { return int((s->*DaGet(EnCanFireMoving()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00552140) & 0xFF); });
            break;
        case 8: {
            const S_vector4 from = ToVector4(h->GetFrame()->GetWorldPos() + S_vector(U(-1, 1), U(0, 1), U(-1, 1)), U(-1, 1));
            EnCall("HasLineTo", [&] { return int((s->*DaGet(EnHasLineTo()))(from)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00552190, &from) & 0xFF); });
            break;
        }
        case 9:
            // (Now and then exactly 1 m or 20 m away, or level.)
            if (P(0.2f)) {
                const S_vector own = h->GetFrame()->GetWorldPos();
                g_otherFrame->SetPosInline(own + S_vector(P(0.5f) ? 1.0f : 20.0f, 0, 0));
                UpdateTree(g_otherFrame);
            }
            EnCall("CanFireLying", [&] { return int((s->*DaGet(EnCanFireLying()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00552200) & 0xFF); });
            break;
        case 10: {
            C_Human *target = P(0.15f) ? nullptr : P(0.6f) ? EnTargetAt(EnDistance(), P(0.6f)) : PortThird();
            EnCall("TakeCoverStance", [&] { return int((s->*DaGet(EnTakeCoverStance()))(cp, target)); },
                   [&](void *) { return int(OAi<uint32_t>(0x005525f0, static_cast<const void *>(cp), target ? ToOrig(target) : nullptr) & 0xFF); });
            break;
        }
        case 11:
            if (P(0.2f))
                s->*DaGet(EnTarget()) = nullptr;
            EnCall("ChooseCombatStance", [&] { (s->*DaGet(EnChooseCombatStance()))(); return 0; },
                   [&](void *) { OAi<void>(0x00552970); return 0; });
            break;
        case 12:
            EnCall("FindRetreatPoint", [&] { return EnPoint((s->*DaGet(EnFindRetreatPoint()))()); },
                   [&](void *) { return EnPoint(OAi<int>(0x00552b00)); });
            break;
        case 13: {
            // (Any distance, but the points' removals (one or two each) not more than the list holds: beyond, the
            // original's list runs below empty, the port's fix; EnApproachSafe's otherwise.)
            C_Human *target = P(0.1f) ? nullptr : EnTargetAt(P(0.5f) ? U(1, 24) : U(24, 90), false);
            if (target && !EnApproachFits(target))
                EnTargetAt(U(24, 90), false);
            EnCall("FindApproachPoint", [&] { return EnPoint((s->*DaGet(EnFindApproachPoint()))(cp, target)); },
                   [&](void *) { return EnPoint(OAi<int>(0x00554e80, static_cast<const void *>(cp), target ? ToOrig(target) : nullptr)); });
            break;
        }
        case 14: {
            const S_vector4 pos = P(0.1f) ? S_vector4{0, 0, 0, 1} : EnPlace();
            EnCall("FindFlankPoint", [&] { return EnPoint((s->*DaGet(EnFindFlankPoint()))(pos)); },
                   [&](void *) { return EnPoint(OAi<int>(0x005572f0, &pos)); });
            break;
        }
        case 15:
            EnCall("FindCoverInRange", [&] { return EnPoint((s->*DaGet(EnFindCoverInRange()))()); },
                   [&](void *) { return EnPoint(OAi<int>(0x00553580)); });
            break;
        case 16:
            EnCall("HasFriendNearby", [&] { return int((s->*DaGet(EnHasFriendNearby()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00553b90) & 0xFF); });
            break;
        case 17:
            // (The times at their edges.)
            if (P(0.3f))
                s->*DaGet(EnSeenTime()) = 2000 + I(-1, 0);
            if (P(0.3f))
                s->*DaGet(EnCoverTime()) = 7000 + I(0, 1);
            EnCall("ShouldStay", [&] { return int((s->*DaGet(EnShouldStay()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00553be0) & 0xFF); });
            break;
        case 18: {
            const int point = P(0.2f) ? CHECKPOINT_NONE : I(0, 7);
            EnCall("MoveToCombatPoint", [&] { return int((s->*DaGet(EnMoveToCombatPoint()))(point)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00553d10, point) & 0xFF); });
            break;
        }
        case 19:
        case 20: {
            // (Now and then a point off the network: Get answers none, which the loops skip.)
            if (P(0.3f)) {
                (s->*DaGet(EnHomeCover())).push_back(uint16_t(I(8, 9)));
                g_en.reachable.push_back(uint16_t(I(8, 9)));
            }
            C_Human *target = P(0.1f) ? nullptr : EnTargetAt(EnDistance(), P(0.85f));
            const bool launcher = which == 19;
            EnCall(launcher ? "FindLauncherPoint" : "FindPointBesideVehicle",
                   [&] {
                       return EnPoint(launcher ? (s->*DaGet(EnFindLauncherPoint()))(target) : (s->*DaGet(EnFindPointBesideVehicle()))(target));
                   },
                   [&](void *) { return EnPoint(OAi<int>(launcher ? 0x00555400 : 0x00555ca0, target ? ToOrig(target) : nullptr)); });
            break;
        }
        case 21: {
            if (P(0.3f))
                (s->*DaGet(EnHomeCover())).push_back(uint16_t(I(8, 9)));
            C_Human *target = P(0.15f) ? nullptr : EnTargetAt(EnDistance(), false);
            EnCall("PickCoverAwayFrom", [&] { return EnPoint((s->*DaGet(EnPickCoverAwayFrom()))(target)); },
                   [&](void *) { return EnPoint(OAi<int>(0x005579e0, target ? ToOrig(target) : nullptr)); });
            break;
        }
        default:
            EnArm(s);
            EnApproachSafe(s);
            EnCall("RunTask", [&] { (s->*DaGet(EnRunTask()))(dt); return 0; }, [&](void *) { OAi<void>(0x00548840, dt); return 0; });
            break;
        }
    }
}

// A vector's xyz as words (the w: a stale register in the original's helpers).
int EnXyzHash(const float *v)
{
    uint32_t w[3];
    std::memcpy(w, v, 12);
    return int(w[0] * 2654435761u ^ w[1] * 40503u ^ w[2]);
}

void TestEnemyD()
{
    std::printf("enemy: the gunner, the seat weapon's targets and aim, the post's guns\n");
    for (int i = 0; i < 30000 / QUICK; ++i) {
        const int which = I(0, 23);
        // (The gunner's own with its task and so in the vehicle (RandomEnemy keeps one then): the original reads the
        // vehicle unchecked in most of them.)
        static const int taskOf[24] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0, 0, 0, 0, 6, 6};
        RandomEnemy(false, taskOf[which]);
        S_En *s = En();
        C_Human *h = g_h.human;
        const int dt = EnDt();
        // (A target where the call aims at it unchecked.)
        if ((which >= 6 && which <= 10) || P(0.4f))
            EnTargetAt(EnDistance(), P(0.2f));
        // (The seat weapon's aim now and then toward the target: the turns' and the firing's tests.)
        if (P(0.5f) && s->*DaGet(EnTarget())) {
            S_vector dir = (s->*DaGet(EnTarget()))->GetBodyFrame()->GetWorldPos() - g_en.origin;
            dir.Normalize();
            g_asSeat.aim = (dir + (P(0.5f) ? S_vector(0, 0, 0) : S_vector(U(-0.05f, 0.05f), U(-0.05f, 0.05f), U(-0.05f, 0.05f)))).Normalized();
        }
        // (The cannon now and then: the seat weapon's component kind.)
        if (P(0.4f))
            s->*DaGet(EnFlags()) |= S_En::FLAG_CANNON;
        C_Human *const humans[3] = {h, PortOther(), PortThird()};
        switch (which) {
        case 0:
        case 1: {
            S_HumanEvent *ev = EnAnEvent();
            if (P(0.4f))
                s->m_task->unknown08 = S_En::GUNNER_WATCH;
            if (P(0.3f)) {
                const int health = I(0, 2);
                for (C_Human *o : {PortOther(), PortThird()})
                    o->m_stats[STAT_HEALTH] = health;
                std::memcpy(g_origOther + 0xEC, &health, 4);
                std::memcpy(g_origThird + 0xEC, &health, 4);
            }
            EnCall("GunnerEvent", [&] { (s->*DaGet(EnGunnerEvent()))(ev); return 0; },
                   [&](void *) { OAi<void>(0x0054cbb0, static_cast<void *>(ev)); return 0; });
            break;
        }
        case 2:
        case 3:
            EnCall("UpdateGunner", [&] { (s->*DaGet(EnUpdateGunner()))(dt); return 0; }, [&](void *) { OAi<void>(0x0054cf70, dt); return 0; });
            break;
        case 4:
            EnCall("PickGunnerTarget", [&] { return EnHuman((s->*DaGet(EnPickGunnerTarget()))(), false); },
                   [&](void *) { return EnHuman(OAi<void *>(0x0054d300), true); });
            break;
        case 5:
            EnCall("PickSeenTarget", [&] { return EnHuman((s->*DaGet(EnPickSeenTarget()))(), false); },
                   [&](void *) { return EnHuman(OAi<void *>(0x0054f040), true); });
            break;
        case 6: {
            const bool scatter = P(0.5f);
            float pv[4] = {}, ov[4] = {};
            EnCall("GetGunAimDir",
                   [&] {
                       const S_vector v = (s->*DaGet(EnGetGunAimDir()))(scatter);
                       std::memcpy(pv, &v, 12);
                       return EnXyzHash(pv);
                   },
                   [&](void *) {
                       OAi<void *>(0x0054d800, static_cast<void *>(ov), int(scatter));
                       return EnXyzHash(ov);
                   });
            break;
        }
        case 7:
        case 8:
        case 9:
        case 10: {
            static const uint32_t at[] = {0x0054dd20, 0x0054ded0, 0x0054f280, 0x0054e7d0};
            static const char *const names[] = {"AimGun", "AimAndFireGun", "AimAndFireCannon", "SweepGun"};
            static void (S_En::*const members[])() = {DaGet(EnAimGun()), DaGet(EnAimAndFireGun()), DaGet(EnAimAndFireCannon()),
                                                      DaGet(EnSweepGun())};
            const int k = which - 7;
            if (P(0.2f))
                s->*DaGet(EnTarget()) = nullptr;
            EnCall(names[k], [&] { (s->*members[k])(); return 0; }, [&](void *) { OAi<void>(at[k]); return 0; });
            break;
        }
        case 11: {
            C_Human *who = humans[I(0, 2)];
            const bool threat = P(0.5f);
            if (P(0.5f))
                EnTargetAt(P(0.3f) ? float(I(0, 2) * 4 + 8) : U(1, 30), false);
            EnCall(threat ? "IsThreatToGunner" : "IsOutOfGunReach",
                   [&] { return int(threat ? (s->*DaGet(EnIsThreatToGunner()))(who) : (s->*DaGet(EnIsOutOfGunReach()))(who)); },
                   [&](void *) { return int(OAi<uint32_t>(threat ? 0x0054d570 : 0x0054dc50, ToOrig(who)) & 0xFF); });
            break;
        }
        case 12:
            EnCall("IsGunnerThreatened", [&] { return int((s->*DaGet(EnIsGunnerThreatened()))()); },
                   [&](void *) { return int(OAi<uint32_t>(0x0054d5c0) & 0xFF); });
            break;
        case 13:
            EnCall("GetGunnerDelay", [&] { return (s->*DaGet(EnGetGunnerDelay()))(); }, [&](void *) { return OAi<int>(0x0054d620); });
            break;
        case 14: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x0054dc10, 0x0054dcf0, 0x0054e7a0};
            static const char *const names[] = {"LeaveGun", "IsAtStaticWeapon", "IsGunEmpty"};
            EnCall(names[k],
                   [&] {
                       if (k == 0) {
                           (s->*DaGet(EnLeaveGun()))();
                           return 0;
                       }
                       return int(k == 1 ? (s->*DaGet(EnIsAtStaticWeapon()))() : (s->*DaGet(EnIsGunEmpty()))());
                   },
                   [&](void *) {
                       if (k == 0) {
                           OAi<void>(at[0]);
                           return 0;
                       }
                       return int(OAi<uint32_t>(at[k]) & 0xFF);
                   });
            break;
        }
        case 15: {
            const S_vector4 pos = ToVector4(h->GetFrame()->GetWorldPos() + S_vector(U(-40, 40), U(-1, 3), U(-40, 40)), 1.0f);
            EnCall("IsLineOfFireFree", [&] { return int((s->*DaGet(EnIsLineOfFireFree()))(pos)); },
                   [&](void *) { return int(OAi<uint32_t>(0x0054f910, &pos) & 0xFF); });
            break;
        }
        case 16:
            EnCall("FindGunToUse", [&] { return DaWord((s->*DaGet(EnFindGunToUse()))(dt), false); },
                   [&](void *) { return DaWord(OAi<void *>(0x0054e600, dt), true); });
            break;
        case 17: {
            C_Actor *gun = static_cast<C_Actor *>(PortActor(I(3, 6)));
            // (Within 6 m, 6..10 m, or farther: the gun's origin about the human.)
            const float d = P(0.3f) ? U(0, 6) : P(0.5f) ? U(6, 10) : U(10, 20);
            g_en.origin = h->GetFrame()->GetWorldPos() + S_vector(d, U(-0.5f, 0.5f), 0) - S_vector(0.5f, 0, 0.25f) * float(gun == PortActor(3) ? 3 : gun == PortActor(4) ? 4 : gun == PortActor(5) ? 5 : 6);
            EnCall("IsGunUseless", [&] { return int((s->*DaGet(EnIsGunUseless()))(gun)); },
                   [&](void *) { return int(OAi<uint32_t>(0x0054e390, ToOrig(gun)) & 0xFF); });
            break;
        }
        case 18: {
            std::vector<C_Actor *> guns;
            std::vector<uint32_t> words;
            for (int k = P(0.1f) ? 0 : I(1, 4); k > 0; --k) {
                guns.push_back(static_cast<C_Actor *>(PortActor(I(3, 6))));
                words.push_back(EnMapped(guns.back()));
            }
            alignas(16) uint8_t orig[16];
            MsvcVectorPut(orig, words);
            EnCall("NearestGun", [&] { return DaWord((s->*DaGet(EnNearestGun()))(guns), false); },
                   [&](void *) { return DaWord(OAi<void *>(0x0054e520, static_cast<void *>(orig)), true); });
            break;
        }
        case 19: {
            C_Human *who = humans[I(0, 2)];
            alignas(16) float pv[4], ov[4];
            EnCall("GetLeadPoint",
                   [&] {
                       const S_vector4 v = GetLeadPoint(who);
                       std::memcpy(pv, &v, 16);
                       return EnXyzHash(pv);
                   },
                   [&](void *) {
                       typedef void *(__stdcall * T_Lead)(void *, void *);
                       OriginalAt<T_Lead>(0x0054d6b0)(ov, ToOrig(who));
                       return EnXyzHash(ov);
                   });
            break;
        }
        default:
            EnCall("RunTask", [&] { (s->*DaGet(EnRunTask()))(dt); return 0; }, [&](void *) { OAi<void>(0x00548840, dt); return 0; });
            break;
        }
    }
}

void TestEnemy()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    SetUpFireOrder();
    SetUpEnemy();
    // (HUMAN_ENEMY_PART: the parts to run, "abcd" all of them; the mutation runs take their own.)
    const char *parts = std::getenv("HUMAN_ENEMY_PART");
    if (!parts || std::strchr(parts, 'a'))
        TestEnemyA();
    if (!parts || std::strchr(parts, 'b'))
        TestEnemyB();
    if (!parts || std::strchr(parts, 'c'))
        TestEnemyC();
    if (!parts || std::strchr(parts, 'd'))
        TestEnemyD();
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
}
}  // namespace
