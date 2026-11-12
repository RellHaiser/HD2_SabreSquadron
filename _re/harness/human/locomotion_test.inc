// Part 1 of the human harness (included by human_test.cpp): the locomotion on the ground against the original's.
namespace {

#include "synth_level.inc"

// Ground: a floor at y 1 over 64 x 64 m; ramps of 15, 35, 50 and 65 degrees (their high ends open: edges); steps of
// 0.15 to 0.9 m; a wall; platforms of 3, 6 and 12 m; a narrow beam; a low roof; a dynamic box (a truck's).
S_CollisionVolume g_truck;
S_matrix g_truckMatrix, g_truckInv;
void LoadGround()
{
    S_Synth s;
    s.withRect = false;
    for (int x = 0; x < 64; x += 8)
        for (int z = 0; z < 64; z += 8)
            AddQuad(s, S_vector(x, 1, z), S_vector(x + 8, 1, z), S_vector(x + 8, 1, z + 8), S_vector(x, 1, z + 8), S_vector(0, 1, 0));
    AddRamp(s, 4, 4, 6, 4, 1.6f);
    AddRamp(s, 4, 10, 6, 4, 4.2f);
    AddRamp(s, 4, 16, 5, 4, 6.0f);
    AddRamp(s, 4, 22, 4, 4, 8.6f);
    AddBlock(s, S_vector(20, 1, 4), S_vector(22, 1.15f, 8));
    AddBlock(s, S_vector(20, 1, 10), S_vector(22, 1.3f, 14));
    AddBlock(s, S_vector(20, 1, 16), S_vector(22, 1.5f, 20));
    AddBlock(s, S_vector(20, 1, 22), S_vector(22, 1.9f, 26));
    AddBlock(s, S_vector(40, 1, 4), S_vector(40.5f, 4, 28));
    AddBlock(s, S_vector(24, 1, 30), S_vector(30, 4, 36));
    AddBlock(s, S_vector(32, 1, 30), S_vector(36, 7, 36));
    AddBlock(s, S_vector(46, 1, 40), S_vector(52, 13, 46));
    AddBlock(s, S_vector(10, 1, 40), S_vector(30, 3, 40.4f));
    AddBlock(s, S_vector(10, 2.4f, 48), S_vector(18, 2.8f, 56));
    // A low wall the others pass (static flag 0x800000: only the player's squad's bodies meet it), material 5.
    {
        S_Synth w;
        AddBlock(w, S_vector(44, 1, 30), S_vector(44.4f, 2.2f, 38));
        for (S_SynthTri &t : w.tris) {
            t.flags |= 0x800000 | (5u << 24);
            s.tris.push_back(t);
        }
    }
    // A pond: a two-sided water surface at y 3 (fall damage looks for water below).
    s.tris.push_back(Tri(S_vector(52, 3, 52), S_vector(60, 3, 52), S_vector(60, 3, 60), COLLISION_FLAG_WATER | COLLISION_TRIANGLE_TWO_SIDED, false));
    s.tris.push_back(Tri(S_vector(52, 3, 52), S_vector(60, 3, 60), S_vector(52, 3, 60), COLLISION_FLAG_WATER | COLLISION_TRIANGLE_TWO_SIDED, false));
    const std::string path = TempFile("ground.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
    // The truck: a dynamic box 3 x 1.5 x 6 m on the floor.
    g_truck = S_CollisionVolume();
    g_truck.flags = COLLISION_SHAPE_BOX;
    g_truck.type = 0x2;
    g_truck.bbox.min = S_vector(-1.5f, -0.75f, -3.0f);
    g_truck.bbox.max = S_vector(1.5f, 0.75f, 3.0f);
    g_truckMatrix.Identity();
    g_truckMatrix.m[3][0] = 56;
    g_truckMatrix.m[3][1] = 1.75f;
    g_truckMatrix.m[3][2] = 10;
    g_truckInv.Inverse(g_truckMatrix);
    g_truck.matrix = &g_truckMatrix;
    g_truck.invMatrix = &g_truckInv;
    g_truck.center = S_vector(56, 1.75f, 10);
    g_truck.radius = 3.5f;
    g_collisionWorld.AddVolume(&g_truck);
    g_collisionWorld.MoveVolume(&g_truck);
}

// ---- the actors a body can walk into (ProcessMoveContacts): a dynamic box each, its frame's data naming the actor.
// Each side finds its own object there (the port's layout; the original's: type +0x18, position +0x30, a door's motion
// +0x224, OnCollision in slot 0x58), swapped by DiffCall; a physics object's handle likewise (the "none" handle is
// each side's own).
struct S_ContactActor {
    const char *name;
    int type;                                           // E_ActorType; -1: a physics object; -2: no actor
    S_vector at, half;
    S_CollisionVolume volume;
    S_matrix matrix, inv;
    I3D_frame *frame = nullptr;
    S_FrameData data = {};
    uint8_t *port = nullptr, *orig = nullptr;
};
std::vector<S_ContactActor *> g_contacts;
void *p_actorVt[128], *o_actorVt[96], *p_physVt[64], *o_physVt[32];
alignas(16) uint8_t g_physHandle[16], p_physModule[64], o_physModule[64];
void __attribute__((thiscall)) P_ActorOnCollision(C_Actor *self, C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6)
{
    const S_vector *move = reinterpret_cast<const S_vector *>(a3), *normal = reinterpret_cast<const S_vector *>(a4);
    Log(self, "OnCollision", {double(other != nullptr), pos->x, pos->y, pos->z, move->x, move->y, move->z, normal->x, normal->y, normal->z,
                              double(a5), double(a6)});
}
void __attribute__((thiscall)) O_ActorOnCollision(void *self, void *other, const float *pos, int a3, int a4, int a5, int a6)
{
    const float *move = reinterpret_cast<const float *>(a3), *normal = reinterpret_cast<const float *>(a4);
    Log(self, "OnCollision", {double(other != nullptr), pos[0], pos[1], pos[2], move[0], move[1], move[2], normal[0], normal[1], normal[2],
                              double(a5), double(a6)});
}
bool __attribute__((thiscall)) P_PhysHit(void *self, void *handle, int kind, const S_vector *dir, void *extra)
{
    Log(self, "Hit", {double(handle == g_physHandle), double(kind), dir->x, dir->y, dir->z, double(extra != nullptr)});
    return false;
}
bool __attribute__((thiscall)) O_PhysHit(void *self, void *handle, int kind, const float *dir, void *extra)
{
    Log(self, "Hit", {double(handle == g_physHandle), double(kind), dir[0], dir[1], dir[2], double(extra != nullptr)});
    return false;
}
void *O_GetPhysicsModule() { return o_physModule; }
const uint32_t O_NO_PHYSICS_OBJECT = 0x00832cdc, O_NO_PHYSICS_HANDLE = 0x00867a98;

void AddContact(const char *name, int type, const S_vector &at, const S_vector &half, int motion = 0)
{
    auto *c = new S_ContactActor();
    c->name = name;
    c->type = type;
    c->at = at;
    c->half = half;
    c->volume.flags = COLLISION_SHAPE_BOX;
    c->volume.type = type == ACTOR_DOOR ? COLLISION_TYPE_ACTOR_DOOR : type == ACTOR_LADDER ? COLLISION_TYPE_LADDER : 0;
    c->volume.bbox.min = -half;
    c->volume.bbox.max = half;
    c->matrix.Identity();
    c->matrix.m[3][0] = at.x;
    c->matrix.m[3][1] = at.y;
    c->matrix.m[3][2] = at.z;
    c->inv.Inverse(c->matrix);
    c->volume.matrix = &c->matrix;
    c->volume.invMatrix = &c->inv;
    c->volume.center = at;
    c->volume.radius = half.Magnitude();
    c->frame = NewFrame(FRAME_VISUAL, name, at, Identity(), Box(half), "", g_sector);
    c->frame->SetData(&c->data);
    c->volume.frame = c->frame;
    if (type >= 0) {
        // The port's object: an actor of the port's layout (a door's size: the largest read).
        const size_t size = sizeof(C_Door) + 0x100;
        c->port = static_cast<uint8_t *>(::operator new(size, std::align_val_t(16)));
        std::memset(c->port, 0, size);
        auto *a = reinterpret_cast<C_Actor *>(c->port);
        *reinterpret_cast<void ***>(a) = p_actorVt;
        a->m_type = static_cast<E_ActorType>(type);
        a->m_position = {at.x, at.y - half.y, at.z, 1.0f};
        if (type == ACTOR_DOOR)
            reinterpret_cast<C_Door *>(a)->m_motion = motion;
        S_vector4 top = {}, bottom = {};
        if (type == ACTOR_LADDER) {
            top = {at.x, at.y + half.y, at.z + 0.3f, 1.0f};
            bottom = {at.x, at.y - half.y, at.z - 0.1f, 1.0f};
            reinterpret_cast<C_Ladder *>(a)->m_top = top;
            reinterpret_cast<C_Ladder *>(a)->m_bottom = bottom;
        }
        // The original's.
        c->orig = static_cast<uint8_t *>(::operator new(0x400, std::align_val_t(16)));
        std::memset(c->orig, 0, 0x400);
        *reinterpret_cast<void ***>(c->orig) = o_actorVt;
        *reinterpret_cast<int *>(c->orig + 0x18) = type;
        std::memcpy(c->orig + 0x30, &a->m_position, 16);
        *reinterpret_cast<int *>(c->orig + 0x224) = motion;
        if (type == ACTOR_LADDER) {
            std::memcpy(c->orig + 0xA0, &top, 16);
            std::memcpy(c->orig + 0xB0, &bottom, 16);
        }
        g_names[c->port] = g_names[c->orig] = name;
        g_extraPointers.push_back({c->port, c->orig});
    }
    g_collisionWorld.AddVolume(&c->volume);
    g_collisionWorld.MoveVolume(&c->volume);
    g_contacts.push_back(c);
}
// Each side's view of the frames' data.
void ContactsFor(bool original)
{
    for (S_ContactActor *c : g_contacts) {
        c->data.actor = reinterpret_cast<C_Actor *>(original ? c->orig : c->port);
        if (c->type == -1)
            c->data.physicsObject = reinterpret_cast<T_PhysicsObject>(g_physHandle);
        else
            c->data.physicsObject = original ? *OriginalAt<T_PhysicsObject *>(O_NO_PHYSICS_OBJECT) : g_noPhysicsObject;
    }
}
void MakeContacts()
{
    for (void *&slot : p_actorVt)
        slot = reinterpret_cast<void *>(P_Unexpected);
    p_actorVt[VSlot(&C_Actor::OnCollision)] = reinterpret_cast<void *>(P_ActorOnCollision);
    for (void *&slot : o_actorVt)
        slot = reinterpret_cast<void *>(O_UnexpectedVirtual);
    o_actorVt[0x58 / 4] = reinterpret_cast<void *>(O_ActorOnCollision);
    for (void *&slot : p_physVt)
        slot = reinterpret_cast<void *>(P_Unexpected);
    p_physVt[VSlot(&C_PhysicsModuleBase::Hit)] = reinterpret_cast<void *>(P_PhysHit);
    *reinterpret_cast<void ***>(p_physModule) = p_physVt;
    g_portPhysicsModule = p_physModule;
    for (void *&slot : o_physVt)
        slot = reinterpret_cast<void *>(O_UnexpectedVirtual);
    o_physVt[0x4C / 4] = reinterpret_cast<void *>(O_PhysHit);
    *reinterpret_cast<void ***>(o_physModule) = o_physVt;
    g_names[p_physModule] = g_names[o_physModule] = "physics";
    RedirectOriginal(0x00410630, reinterpret_cast<const void *>(O_GetPhysicsModule));
    // The original's "no physics object" handle {-1, false} and the pointer to it (its static initialiser never ran).
    *OriginalAt<int *>(O_NO_PHYSICS_HANDLE) = -1;
    *OriginalAt<uint8_t *>(O_NO_PHYSICS_HANDLE + 4) = 0;
    *OriginalAt<void **>(O_NO_PHYSICS_OBJECT) = OriginalAt<void *>(O_NO_PHYSICS_HANDLE);
    AddContact("otherHuman", ACTOR_HUMAN, S_vector(34, 2, 44), S_vector(0.3f, 0.9f, 0.3f));
    AddContact("door", ACTOR_DOOR, S_vector(34, 2, 48), S_vector(0.6f, 1.0f, 0.05f));
    AddContact("openingDoor", ACTOR_DOOR, S_vector(34, 2, 51), S_vector(0.6f, 1.0f, 0.05f), 2);
    AddContact("ladder", ACTOR_LADDER, S_vector(34, 2.5f, 54), S_vector(0.3f, 1.5f, 0.1f));
    AddContact("barrel", ACTOR_BARREL, S_vector(37, 1.5f, 44), S_vector(0.3f, 0.5f, 0.3f));
    AddContact("train", ACTOR_TRAIN, S_vector(37, 2, 48), S_vector(1.0f, 1.0f, 2.0f));
    AddContact("crate", -1, S_vector(37, 1.4f, 53), S_vector(0.4f, 0.4f, 0.4f));
    AddContact("box", 0, S_vector(31, 1.4f, 53), S_vector(0.4f, 0.4f, 0.4f));
    AddContact("nothing", -2, S_vector(31, 1.4f, 47), S_vector(0.4f, 0.4f, 0.4f));
    // A volume of type 0x100 (only the local player's sweeps meet it).
    AddContact("playerOnly", 0, S_vector(46, 1.5f, 34), S_vector(0.3f, 0.5f, 1.5f));
    g_contacts.back()->volume.type = 0x100;
}

// The stance spheres around the position (the feet), as UpdateStanceVolumes would leave them (faked here).
void PlaceSpheres(int stance)
{
    C_HumanMovement *m = g_h.movement;
    const S_vector4 &p = g_h.human->m_position;
    const S_vector4 &d = g_h.human->m_direction;
    const S_vector dir = S_vector(d.x, 0, d.z).Normalized();
    if (stance == HUMAN_STANCE_STAND) {
        m->m_stanceVolumeCount = 3;
        for (int i = 0; i < 3; ++i)
            m->m_stanceSpheres[i] = {p.x, p.y + 0.35f + 0.55f * i, p.z, 0.33f};
    } else if (stance == HUMAN_STANCE_CROUCH) {
        m->m_stanceVolumeCount = 2;
        for (int i = 0; i < 2; ++i)
            m->m_stanceSpheres[i] = {p.x, p.y + 0.35f + 0.45f * i, p.z, 0.33f};
    } else {
        m->m_stanceVolumeCount = 4;
        for (int i = 0; i < 4; ++i)
            m->m_stanceSpheres[i] = {p.x + dir.x * (0.45f * i - 0.7f), p.y + 0.26f, p.z + dir.z * (0.45f * i - 0.7f), 0.25f};
    }
}

// ---- one call compared
struct S_PortState {
    std::vector<uint8_t> human, movement, work, frame, equipment, pose, volumes, camera;
    std::vector<std::vector<uint8_t>> frames;
    float moveIterations;
};
// More frames the code under test may move (the model's dummies follow the model): each side starts from the same.
std::vector<I3D_frame *> g_sharedFrames;
// The human's stance and hit volumes, as one block.
uint8_t *HumanVolumes() { return reinterpret_cast<uint8_t *>(g_h.stanceVolumes); }
const size_t HUMAN_VOLUMES_SIZE = sizeof(S_CollisionVolume) * 6;
static_assert(offsetof(S_TestHuman, hitVolumes) == offsetof(S_TestHuman, stanceVolumes) + sizeof(S_CollisionVolume) * 4, "volumes");
// (The port's MoveVolume files them in the world; the test's world never keeps them: the body's own sweeps would meet
// them, and the original's are not registered either.)
void RemoveHumanVolumes()
{
    for (S_CollisionVolume &v : g_h.stanceVolumes)
        g_collisionWorld.RemoveVolume(&v);
    for (S_CollisionVolume &v : g_h.hitVolumes)
        g_collisionWorld.RemoveVolume(&v);
}
S_PortState TakePort()
{
    S_PortState s;
    const auto *h = reinterpret_cast<const uint8_t *>(g_h.human);
    const auto *m = reinterpret_cast<const uint8_t *>(g_h.movement);
    const auto *w = reinterpret_cast<const uint8_t *>(g_h.work);
    const auto *f = reinterpret_cast<const uint8_t *>(g_h.frame);
    s.human.assign(h, h + sizeof(C_Human));
    s.movement.assign(m, m + sizeof(C_HumanMovement));
    s.work.assign(w, w + sizeof(C_HumanWork));
    s.frame.assign(f, f + 0x400);
    s.equipment.assign(g_h.equipment, g_h.equipment + 0x400);
    s.pose.assign(g_h.pose, g_h.pose + sizeof(C_HumanWeaponPose));
    s.volumes.assign(HumanVolumes(), HumanVolumes() + HUMAN_VOLUMES_SIZE);
    const auto *c = reinterpret_cast<const uint8_t *>(g_h.camera);
    s.camera.assign(c, c + 0x400);
    s.moveIterations = g_moveIterations;
    for (I3D_frame *f : g_sharedFrames) {
        const auto *b = reinterpret_cast<const uint8_t *>(f);
        s.frames.emplace_back(b, b + 0x400);
    }
    return s;
}
void PutPort(const S_PortState &s)
{
    std::memcpy(static_cast<void *>(g_h.human), s.human.data(), s.human.size());
    std::memcpy(static_cast<void *>(g_h.movement), s.movement.data(), s.movement.size());
    std::memcpy(static_cast<void *>(g_h.work), s.work.data(), s.work.size());
    std::memcpy(static_cast<void *>(g_h.frame), s.frame.data(), s.frame.size());
    std::memcpy(g_h.equipment, s.equipment.data(), s.equipment.size());
    std::memcpy(g_h.pose, s.pose.data(), s.pose.size());
    std::memcpy(HumanVolumes(), s.volumes.data(), s.volumes.size());
    std::memcpy(static_cast<void *>(g_h.camera), s.camera.data(), s.camera.size());
    g_moveIterations = s.moveIterations;
    for (size_t i = 0; i < g_sharedFrames.size() && i < s.frames.size(); ++i)
        std::memcpy(static_cast<void *>(g_sharedFrames[i]), s.frames[i].data(), 0x400);
}

void NoteAssumedGround()
{
    const S_CollisionVolume *g = g_h.movement->m_ground;
    // (In this executable's own image: it may not sit at its preferred base.)
    static uintptr_t imageLo, imageHi;
    if (!imageLo) {
        const auto *base = reinterpret_cast<const uint8_t *>(GetModuleHandleA(nullptr));
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
        imageLo = reinterpret_cast<uintptr_t>(base);
        imageHi = imageLo + nt->OptionalHeader.SizeOfImage;
    }
    const uintptr_t at = reinterpret_cast<uintptr_t>(g);
    if (g && g != &g_truck && at >= imageLo && at < imageHi && g->flags == COLLISION_FLAG_DISABLED)
        g_portAssumedGround = g;
}

struct S_Stats {
    long calls = 0, agree = 0;
    std::map<std::string, long> differ;
};
std::map<std::string, S_Stats> g_stats;
// `port` runs the port's call, `original(locomotion copy)` the original's; both return an int (0 when void).
template <class P, class O> int DiffCall(const char *what, P port, O original)
{
    S_Stats &st = g_stats[what];
    ++st.calls;
    const S_PortState before = TakePort();
    const unsigned seed = ++g_randSeed;
    std::srand(seed);
    g_calls.clear();
    ContactsFor(false);
    if (std::getenv("HUMAN_TRACE"))
        std::printf("[%s %ld port]", what, st.calls);
    // (How often the frame is placed and linked: the fake frame counts them, past its engine bytes.)
    S_FakeFrame *fake = Fake(g_h.frame);
    const int setPos0 = fake->setPos, links0 = fake->links;
    std::vector<uint32_t> sharedBefore, sharedPort;
    for (const auto &w : g_sharedWords)
        sharedBefore.push_back(*w.first);
    const int portResult = port();
    for (size_t i = 0; i < g_sharedWords.size(); ++i)
        sharedPort.push_back(*g_sharedWords[i].first);
    const std::vector<S_Call> portCalls = g_calls;
    const int portSetPos = fake->setPos - setPos0, portLinks = fake->links - links0;
    fake->setPos = setPos0;
    fake->links = links0;
    RemoveHumanVolumes();
    NoteAssumedGround();
    const S_PortState after = TakePort();
    for (size_t i = 0; i < g_sharedWords.size(); ++i)
        *g_sharedWords[i].first = sharedBefore[i];
    // The port's result in the original's layout.
    ToOriginal();
    static alignas(16) uint8_t bh[sizeof o_human], bm[sizeof o_movement], bw[sizeof o_work];
    std::memcpy(bh, o_human, sizeof bh);
    std::memcpy(bm, o_movement, sizeof bm);
    std::memcpy(bw, o_work, sizeof bw);
    const float portIterations = g_moveIterations;
    // The original's from the same state.
    PutPort(before);
    ToOriginal();
    g_calls.clear();
    std::srand(seed);
    ContactsFor(true);
    if (std::getenv("HUMAN_TRACE"))
        std::printf("[original]");
    const int origResult = WithOriginalWorldMapped(true, [](S_CollisionVolume *v) { return v; },
                                                   [&] { return original(static_cast<void *>(o_movement + 0xAC0)); });
    const std::vector<S_Call> origCalls = g_calls;
    ContactsFor(false);
    const int origSetPos = fake->setPos - setPos0, origLinks = fake->links - links0;
    // Compared.
    std::string why;
    if (portResult != origResult)
        why = "result " + std::to_string(portResult) + " / " + std::to_string(origResult);
    if (why.empty() && !CallsAgree(portCalls, origCalls, why))
        why = "calls: " + why;
    if (why.empty())
        why = FieldsDiffer(kActorFields, bh, o_human, "human");
    if (why.empty())
        why = FieldsDiffer(kHumanFields, bh, o_human, "human");
    if (why.empty())
        why = FieldsDiffer(kMovementFields, bm, o_movement, "movement");
    if (why.empty()) {
        for (size_t i = 0; i + 4 <= sizeof(C_HumanWork); i += 4) {
            uint32_t x, y;
            std::memcpy(&x, bw + i, 4);
            std::memcpy(&y, o_work + i, 4);
            if (!WordsNear(x, y, 'm')) {
                why = "work+" + std::to_string(i);
                break;
            }
        }
    }
    if (why.empty() && std::fabs(*OriginalAt<float *>(O_MOVE_ITERATIONS) - portIterations) > 0.0f)
        why = "g_moveIterations";
    // The shared objects (the same layout on both sides): the port's after its call against what the original left.
    const auto sharedDiffers = [&](const std::vector<uint8_t> &port, const uint8_t *orig, const char *name) -> std::string {
        for (size_t i = 0; i + 4 <= port.size(); i += 4) {
            uint32_t x, y;
            std::memcpy(&x, port.data() + i, 4);
            std::memcpy(&y, orig + i, 4);
            if (!WordsNear(x, y, 'm'))
                return std::string(name) + "+" + std::to_string(i);
        }
        return "";
    };
    if (why.empty())
        why = sharedDiffers(after.equipment, g_h.equipment, "equipment");
    if (why.empty())
        why = sharedDiffers(after.pose, g_h.pose, "weaponPose");
    if (why.empty()) {
        // The volumes: their places and sizes (where each side files them is its world's business).
        for (int i = 0; i < 6 && why.empty(); ++i) {
            const auto *p = reinterpret_cast<const S_CollisionVolume *>(after.volumes.data()) + i;
            const S_CollisionVolume *o = reinterpret_cast<const S_CollisionVolume *>(HumanVolumes()) + i;
            const float pv[5] = {p->center.x, p->center.y, p->center.z, p->centerW, p->radius};
            const float ov[5] = {o->center.x, o->center.y, o->center.z, o->centerW, o->radius};
            for (int k = 0; k < 5; ++k) {
                uint32_t x, y;
                std::memcpy(&x, &pv[k], 4);
                std::memcpy(&y, &ov[k], 4);
                if (!WordsNear(x, y, 'f')) {
                    why = "volume " + std::to_string(i) + " word " + std::to_string(k);
                    break;
                }
            }
        }
    }
    if (why.empty()) {
        // The frame: its place.
        const auto *pf = reinterpret_cast<const I3D_frame *>(after.frame.data());
        const I3D_frame *of = g_h.frame;
        for (int k = 0; k < 16 && why.empty(); ++k) {
            uint32_t x, y;
            std::memcpy(&x, &pf->m_localMatrix.m[k / 4][k % 4], 4);
            std::memcpy(&y, &of->m_localMatrix.m[k / 4][k % 4], 4);
            if (!WordsNear(x, y, 'f'))
                why = "frame matrix word " + std::to_string(k);
        }
        if (why.empty() && pf->m_parent != of->m_parent)
            why = "frame parent";
        if (why.empty() && (portSetPos != origSetPos || portLinks != origLinks))
            why = "frame SetPos / LinkTo count";
    }
    for (size_t i = 0; i < g_sharedWords.size(); ++i) {
        if (why.empty() && !WordsNear(*g_sharedWords[i].first, sharedPort[i], 'm'))
            why = g_sharedWords[i].second;
        *g_sharedWords[i].first = sharedPort[i];
    }
    PutPort(after);
    CHECK(why.empty());
    if (why.empty()) {
        ++st.agree;
    } else {
        ++st.differ[why.substr(0, std::min(why.find(" ("), why.find(": port")))];
        static int shown;
        if (shown++ < 15)
            std::printf("  %s differs: %s\n", what, why.c_str());
        if (std::getenv("HUMAN_DUMP") && shown <= std::atoi(std::getenv("HUMAN_DUMP"))) {
            const auto &bph = *reinterpret_cast<const C_Human *>(before.human.data());
            const auto &bmv = *reinterpret_cast<const C_HumanMovement *>(before.movement.data());
            const auto &bwk = *reinterpret_cast<const C_HumanWork *>(before.work.data());
            std::printf("    before: pos %g %g %g, move %g %g %g, stance %d, flags %x, anim %d, fall %g, local %d, sim %d, role %d, game %u\n",
                        bph.m_position.x, bph.m_position.y, bph.m_position.z, bmv.m_mixer.m_move.x, bmv.m_mixer.m_move.y, bmv.m_mixer.m_move.z,
                        bwk.m_request.stance, bmv.m_flags, bmv.m_animState.anim.anim, bmv.m_locomotion.m_fallSpeed, g_script.localPlayer,
                        g_script.serverSimulated, int(bph.m_role), g_gameRole);
            std::printf("    next anim %d, useObject %s, asked %g %g %g, facing %g %g %g, hold %d, pending %d delay %d\n",
                        bmv.m_nextAnimState.anim.anim, NameOf(bmv.m_useObject).c_str(), bwk.m_request.unknown10.x, bwk.m_request.unknown10.y,
                        bwk.m_request.unknown10.z, bph.m_direction.x, bph.m_direction.y, bph.m_direction.z, bmv.m_mixer.m_tracks[0].hold,
                        bmv.m_animPlayer.m_pendingAnim.anim, bmv.m_animPlayer.m_animDelay);
            for (int side = 0; side < 2; ++side) {
                const uint8_t *l = (side ? o_movement : bm) + 0xAC0;
                const auto F = [&](int at) { float v; std::memcpy(&v, l + at, 4); return v; };
                const auto N = [&](int at) { int v; std::memcpy(&v, l + at, 4); return v; };
                std::printf("    %s after: fallGround %g fallHeight %d fallSpeed %g step %g startY %g push %d edge %d %g\n", side ? "original" : "port",
                            F(0x5C), N(0x60), F(0xF4), F(0xF8), F(0xFC), N(0x64), N(0x80), F(0x84));
            }
            for (int side = 0; side < 2; ++side) {
                std::printf("    %s calls:", side ? "original" : "port");
                for (const S_Call &c : side ? origCalls : portCalls) {
                    std::printf(" %s(", c.what.c_str());
                    for (double v : c.nums)
                        std::printf("%g ", v);
                    std::printf(")");
                }
                std::printf("\n");
            }
        }
    }
    return portResult;
}

typedef bool(__attribute__((thiscall)) * T_MoveOnGround)(void *, int);
typedef void(__attribute__((thiscall)) * T_UpdateFall)(void *, S_CollisionVolume *, int, int, int, int);
typedef void(__attribute__((thiscall)) * T_SlowOnSlope)(void *, float, float, float, S_vector4 *, const S_vector4 *, int);
typedef void(__attribute__((thiscall)) * T_ApplyFallDamage)(void *, int);
bool DiffMoveOnGround(int time)
{
    return DiffCall("MoveOnGround", [&] { return int(g_h.movement->m_locomotion.MoveOnGround(time)); },
                    [&](void *self) { return int(OriginalAt<T_MoveOnGround>(0x00764b50)(self, time)); }) != 0;
}

// ---- random humans on the ground
std::mt19937 g_rng(4711);
float U(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }
int I(int a, int b) { return std::uniform_int_distribution<int>(a, b)(g_rng); }
bool P(float p) { return U(0, 1) < p; }

// The ground's height under (x, z) from above `top` (the floor's when nothing).
float GroundAt(float x, float z, float top = 15.5f)
{
    S_vector pos, normal;
    if (CollisionRayTest(S_vector(x, top, z), S_vector(0, -20, 0), &pos, &normal, 0xFFFFFFFF, 0))
        return pos.y;
    return 1.0f;
}

// A human in a random state at a random place.
void RandomHuman()
{
    C_Human *h = g_h.human;
    C_HumanMovement *m = g_h.movement;
    C_HumanLocomotion &loco = m->m_locomotion;
    // The roles of the game and the human, and who simulates it.
    const int gameRoles[] = {GAME_ROLE_LOCAL, GAME_ROLE_LOCAL, GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT};
    g_gameRole = gameRoles[I(0, 4)];
    const E_HumanRole roles[] = {HUMAN_ROLE_PLAYER, HUMAN_ROLE_ENEMY, HUMAN_ROLE_NEUTRAL, HUMAN_ROLE_NPC_TEAM};
    h->m_role = roles[I(0, 3)];
    g_script = S_Script();
    // (As the original's IsLocalPlayer and IsServerSimulated decide, also where it has them inline.)
    g_script.localPlayer = h->m_role == HUMAN_ROLE_PLAYER && P(0.6f);
    g_script.serverSimulated = (g_gameRole & GAME_ROLE_SERVER) && h->m_role != HUMAN_ROLE_PLAYER;
    g_mpSettings->fallingDamage = P(0.7f) ? 1 : 0;
    g_script.stat = P(0.3f) ? I(1, 60) : 100;
    g_script.pathHeight = P(0.5f);
    g_script.updateWater = P(0.06f);
    g_script.ladderCanUse = P(0.5f);
    g_script.viewDirY = P(0.5f) ? U(-0.5f, 0.5f) : -0.08f;
    g_script.divingGear = P(0.3f);
    // Where: on the floor, a ramp, a step, a platform, or in the air.
    float x = U(3, 61), z = U(3, 61);
    const int where = I(0, 9);
    if (where == 0) {                                   // on the truck
        x = U(54.6f, 57.4f);
        z = U(7.2f, 12.8f);
    } else if (where == 1) {                            // by the actors
        x = U(30, 39);
        z = U(42, 56);
    } else if (where == 2) {                            // over the pond
        x = U(52.5f, 59.5f);
        z = U(52.5f, 59.5f);
    } else if (where == 3) {                            // by the low wall and the player-only box
        x = U(41, 49);
        z = U(30, 38);
    }
    float y = GroundAt(x, z);
    if (P(0.15f))
        y += U(0.2f, 8.0f);
    h->m_position = {x, y, z, 1.0f};
    const float yaw = U(0, 6.2831f);
    h->m_direction = {std::sin(yaw), 0.0f, std::cos(yaw), 0.0f};
    h->m_velocity = {U(-3, 3), U(-6, 1), U(-3, 3), 0.0f};
    // The work's requests.
    C_HumanWork *w = g_h.work;
    w->m_request.stance = P(0.6f) ? HUMAN_STANCE_STAND : P(0.5f) ? HUMAN_STANCE_CROUCH : HUMAN_STANCE_PRONE;
    w->m_request.gait = I(0, 5);
    w->m_mode = 0;
    // The movement.
    const uint32_t flagSets[] = {0, 0, 0, MOVE_FLAG_GROUND_RAY, MOVE_FLAG_FOLLOW_PATH, MOVE_FLAG_REMOTE, MOVE_FLAG_HIDDEN, MOVE_FLAG_SMALL,
                                 MOVE_FLAG_GROUND_RAY | MOVE_FLAG_SMALL};
    m->m_flags = flagSets[I(0, 8)];
    m->m_clock = P(0.8f) ? I(2500, 60000) : I(0, 2000);
    m->m_stillTime = P(0.05f) ? 10000000 + I(0, 100) : P(0.5f) ? I(0, 3000) : I(-500, 0);
    m->m_unknown1AC = 1;
    const int anims[] = {0xBE, 0xBF, 0xC3, 0xC7, 0x2D, 0x37, 0x3F, 0x11, 0x38, 0x14, 0x6A, 0x08, 0x0E};
    m->m_animState.anim.anim = anims[I(0, 12)];
    m->m_animState.carryAnim = P(0.1f) ? I(0x4C, 0x4F) : 91;
    m->m_unknownA30 = P(0.1f) ? I(0, 500) : MOVE_NO_TIME;
    m->m_parachuting = P(0.1f);
    m->m_onDynamicGround = where == 0 ? P(0.8f) : P(0.1f);
    m->m_onGround = P(0.5f);
    m->m_slideFactor = P(0.3f) ? U(0, 1.5f) : 0.0f;
    m->m_nextStance = P(0.1f) ? HUMAN_STANCE_PRONE : 3;
    m->m_unknown9A0 = P(0.2f) ? I(0, 300) : 0;
    m->m_mixer.m_tracks[0].hold = P(0.2f) ? 1 : 0;
    m->m_ground = nullptr;
    m->m_groundMaterial = I(-1, 40);
    // The locomotion's own state.
    loco.m_fallSpeed = P(0.3f) ? U(0.5f, 20.0f) : 0.0f;
    loco.m_fallStepTime = U(0, 80);
    loco.m_fallStartY = P(0.3f) ? y + U(-1, 15) : MOVE_NO_VALUE;
    loco.m_fallHeight = P(0.2f) ? I(0, 3) : MOVE_NO_TIME;
    loco.m_fallGround = P(0.2f) ? y - U(0, 10) : MOVE_NO_VALUE;
    loco.m_pushTime = P(0.15f) ? I(-20, 100) : MOVE_NO_TIME;
    loco.m_push = {U(-1.5f, 1.5f), 0.0f, U(-1.5f, 1.5f), 0.0f};
    loco.m_edgeTime = P(0.3f) ? I(0, 400) : 0;
    loco.m_edgeHeight = P(0.3f) ? y + U(-0.3f, 0.3f) : MOVE_NO_VALUE;
    loco.m_shiftTime = P(0.1f) ? I(1, 300) : MOVE_NO_TIME;
    loco.m_shiftStance = I(0, 2);
    loco.m_unknown0EC = P(0.2f) ? I(1, 200) : 0;
    loco.m_groundTestTime = I(-60, 250);
    loco.m_slopeSlowdown = P(0.3f) ? U(0, 1) : 0.0f;
    loco.m_waterSurface = P(0.1f) ? y + U(0.5f, 2) : MOVE_NO_VALUE;
    loco.m_waterTransition = P(0.1f) ? I(-200, -1) : 0;
    loco.m_waterTestTime = I(0, 600);
    loco.m_positionChanged = false;
    g_moveIterations = 10.0f;
    PlaceSpheres(loco.m_shiftTime != MOVE_NO_TIME ? loco.m_shiftStance : w->m_request.stance);
    if (P(0.02f))
        m->m_stanceVolumeCount = 0;                     // (no stance volumes yet)
}

// The move of the next step: the mixer's (faked), along a direction at a speed.
void SetMove(const S_vector &velocity, int time)
{
    const float s = static_cast<float>(time) * 0.001f;
    g_h.movement->m_mixer.m_move = {velocity.x * s, velocity.y * s, velocity.z * s, 0.0f};
}

void TestGroundMoves()
{
    for (int i = 0; i < 1500 / QUICK; ++i) {
        RandomHuman();
        float yaw = U(0, 6.2831f);
        if (!g_contacts.empty() && P(0.4f)) {           // (towards an actor)
            const S_vector &to = g_contacts[I(0, static_cast<int>(g_contacts.size()) - 1)]->at;
            yaw = std::atan2(to.x - g_h.human->m_position.x, to.z - g_h.human->m_position.z);
        }
        const float speed = P(0.2f) ? 0.0f : U(0.3f, 7.0f);
        const S_vector velocity(std::sin(yaw) * speed, P(0.2f) ? U(-1, 1) : 0.0f, std::cos(yaw) * speed);
        g_script.pathY = g_h.human->m_position.y + U(-0.5f, 0.5f);
        for (int step = 0; step < 40; ++step) {
            const int time = I(10, 100);
            SetMove(velocity, time);
            // (Cleared after every update by C_HumanLocomotion::Update.)
            g_h.movement->m_locomotion.m_positionChanged = false;
            g_h.movement->m_locomotion.m_directionChanged = false;
            DiffMoveOnGround(time);
            // Between steps the stance spheres follow the body (UpdateStanceVolumes, faked).
            const C_HumanLocomotion &loco = g_h.movement->m_locomotion;
            PlaceSpheres(loco.m_shiftTime != MOVE_NO_TIME ? loco.m_shiftStance : g_h.work->m_request.stance);
            if (g_h.human->m_position.y < -5.0f)
                break;
        }
    }
}

// UpdateFall alone: random states, the ground volume (none, the truck), standing or not, against a wall or not.
void TestFalls()
{
    for (int i = 0; i < 3000 / QUICK; ++i) {
        RandomHuman();
        S_CollisionVolume *ground = P(0.5f) ? nullptr : &g_truck;
        const bool standing = P(0.5f), onWall = P(0.3f), slide = P(0.7f);
        const int time = I(10, 100);
        SetMove(S_vector(U(-3, 3), U(-1, 0.5f), U(-3, 3)), time);
        g_h.movement->m_locomotion.m_move = g_h.movement->m_mixer.m_move;
        DiffCall("UpdateFall", [&] { g_h.movement->m_locomotion.UpdateFall(ground, standing, time, onWall, slide); return 0; },
                 [&](void *self) { OriginalAt<T_UpdateFall>(0x00767990)(self, ground, standing, time, onWall, slide); return 0; });
    }
}

void TestLocomotion()
{
    LoadGround();
    MakeContacts();
    TestFalls();
    TestGroundMoves();
}
void PrintStats()
{
    for (auto &[what, st] : g_stats) {
        std::printf("  %s: %ld, agree %ld", what.c_str(), st.calls, st.agree);
        for (auto &[field, n] : st.differ)
            std::printf(", %s differs %ld", field.c_str(), n);
        std::printf("\n");
    }
}
}  // namespace
