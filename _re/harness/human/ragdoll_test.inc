// Part 5 of the human harness (HUMAN_BASE builds; included after base3_test.inc): the ragdoll's simulation
// (game/human_pose_physics.cpp) against the original's. The port's BuildRagdoll makes one ragdoll from a standing
// skeleton of bone frames; its parts, points, pushes and holds are shared by both sides (the same layout), kept apart
// per side and compared word by word (shared blocks), as the weapon pose itself. The pose's owner and movement are
// each side's own (the original reads the movement's flags at its own offset).
#include "game/car.h"
#include "game/human_pose_internal.h"
#include "game/vehicle.h"
namespace {

C_HumanWeaponPose *Pose() { return reinterpret_cast<C_HumanWeaponPose *>(g_h.pose); }
I3D_frame *g_boneFrame[POSE_BONE_COUNT];

// The skeleton standing at the model (its local places; the names as the model's frames).
struct S_BoneAt {
    int bone;
    const char *name;
    S_vector at;
};
const S_BoneAt kBones[POSE_BONE_COUNT] = {
    {POSE_BONE_LEFT_FOOT, "u", {0.12f, 0.08f, 0.02f}},       {POSE_BONE_RIGHT_FOOT, "q", {-0.12f, 0.08f, 0.0f}},
    {POSE_BONE_LEFT_KNEE, "t", {0.11f, 0.5f, 0.04f}},        {POSE_BONE_RIGHT_KNEE, "p", {-0.11f, 0.5f, 0.03f}},
    {POSE_BONE_NECK, "e", {0.0f, 1.5f, 0.01f}},              {POSE_BONE_HEAD_TOP, "f1", {0.0f, 1.76f, 0.02f}},
    {POSE_BONE_HEAD, "f", {0.0f, 1.62f, 0.03f}},             {POSE_BONE_UPPER_CHEST, "d1", {0.0f, 1.36f, 0.0f}},
    {POSE_BONE_CHEST, "d", {0.0f, 1.4f, -0.01f}},            {POSE_BONE_SPINE, "b", {0.0f, 1.04f, 0.0f}},
    {POSE_BONE_LEFT_HIP, "s", {0.1f, 0.9f, 0.0f}},           {POSE_BONE_RIGHT_HIP, "o", {-0.1f, 0.9f, 0.01f}},
    {POSE_BONE_LOWER_BACK, "c1", {0.0f, 1.14f, -0.01f}},     {POSE_BONE_BACK, "c", {0.0f, 1.25f, 0.0f}},
    {POSE_BONE_RIGHT_COLLAR, "g", {-0.08f, 1.45f, 0.0f}},    {POSE_BONE_LEFT_COLLAR, "k", {0.08f, 1.45f, 0.0f}},
    {POSE_BONE_RIGHT_SHOULDER, "h", {-0.2f, 1.44f, 0.0f}},   {POSE_BONE_LEFT_SHOULDER, "l", {0.2f, 1.44f, 0.01f}},
    {POSE_BONE_RIGHT_ELBOW, "i", {-0.25f, 1.15f, 0.02f}},    {POSE_BONE_LEFT_ELBOW, "m", {0.26f, 1.15f, 0.03f}},
    {POSE_BONE_RIGHT_HAND, "j", {-0.27f, 0.9f, 0.06f}},      {POSE_BONE_LEFT_HAND, "n", {0.28f, 0.9f, 0.05f}},
    {POSE_BONE_PELVIS, "a", {0.0f, 0.95f, 0.0f}},
};
const S_vector kBuildAt(32.0f, 1.0f, 32.0f);           // where the model stood at the build

// The ragdoll as built (each block's bytes), the pose's too.
struct S_Block {
    void *at;
    size_t size;
    std::vector<uint8_t> rest;
    std::string name;
};
std::vector<S_Block> g_ragdollBlocks;
// The w words of a part's vectors (savedStart, savedEnd, the limits, axis, savedAxis, unknown220): the original copies
// undefined ones in. (Only those: the scalars between them, thickness +0x18C among them, are compared.)
bool BodyVectorW(size_t at)
{
    return at == 0x16C || at == 0x17C || (at >= 0x1BC && at <= 0x1EC && at % 16 == 12) || (at >= 0x20C && at <= 0x22C && at % 16 == 12);
}
std::vector<uint8_t> g_poseRest;
I3D_frame *g_contactFrame;                              // a moving thing the points may rest on
bool g_carSimulated = true;
bool g_soundGone = false;

S_ContactActor *g_ragdollVehicle;                       // the vehicle the body lies on (each side's own object)
// The scene's FindFrame (0x7C: a loaded point's contact frame by name): logged; the actors' frames by name.
I3D_frame *__stdcall F_RagdollFindFrame(void *self, const char *name, uint32_t mask)
{
    Log(self, (std::string("FindFrame ") + name).c_str(), {double(mask)});
    for (S_ContactActor *c : g_contacts)
        if (std::strcmp(c->frame->GetName(), name) == 0)
            return c->frame;
    return nullptr;
}
void RagdollSide(bool original)
{
    C_HumanWeaponPose *pose = Pose();
    pose->m_vehicle = g_ragdollVehicle ? reinterpret_cast<C_Actor *>(original ? g_ragdollVehicle->orig : g_ragdollVehicle->port) : nullptr;
    pose->m_owner = original ? reinterpret_cast<C_Human *>(o_human) : g_h.human;
    pose->m_movement = original ? reinterpret_cast<C_HumanMovement *>(o_movement) : g_h.movement;
}

// ---- the original's side of the ragdoll's outside calls
void __attribute__((thiscall)) O_AddSettledBloodPool(void *self) { Log(self, "AddSettledBloodPool"); }
bool __attribute__((thiscall)) O_CarIsSimulatedHere(void *self)
{
    Log(self, "CarIsSimulatedHere");
    return g_carSimulated;
}
void __attribute__((thiscall)) O_ApplyImpulse(void *self, const S_vector *impulse, const S_vector *point, int flags, const S_vector *factors)
{
    Log(self, "ApplyImpulse", {impulse->x, impulse->y, impulse->z, point->x, point->y, point->z, double(flags & 0xFF), factors->x,
                               factors->y, factors->z});
}
void *__attribute__((thiscall)) OS_GetSound(void *self, int slot)
{
    Log(self, "GetSound", {double(slot)});
    return g_soundGone ? nullptr : &g_sound;
}

void SetUpRagdoll()
{
    C_HumanWeaponPose *pose = Pose();
    // The skeleton at the build place, the model unturned.
    g_h.frame->SetPosInline(kBuildAt);
    S_quat none;
    none.Identity();
    g_h.frame->SetRot(none);
    UpdateTree(g_h.frame);
    for (const S_BoneAt &b : kBones) {
        I3D_frame *f = NewFrame(FRAME_DUMMY, std::string("bone_") + b.name, b.at, Identity(), Box(S_vector(0.05f, 0.05f, 0.05f)), "",
                                g_h.frame);
        pose->m_bones[b.bone].index = b.bone;
        pose->m_bones[b.bone].frame = f;
        g_boneFrame[b.bone] = f;
    }
    pose->m_model = g_h.frame;
    UpdateTree(g_h.frame);
    RagdollSide(false);
    pose->BuildRagdoll(false);
    for (int i = 0; i < RAGDOLL_BODY_COUNT; ++i)
        g_ragdollBlocks.push_back({pose->m_bodies[i], sizeof(S_RagdollBody), {}, "body" + std::to_string(i)});
    for (int i = 0; i < RAGDOLL_POINT_COUNT; ++i)
        g_ragdollBlocks.push_back({pose->m_points[i], sizeof(S_RagdollPoint), {}, "point" + std::to_string(i)});
    g_ragdollBlocks.push_back({pose->m_pushes, RAGDOLL_PUSH_COUNT * sizeof(S_RagdollPush), {}, "pushes"});
    g_ragdollBlocks.push_back({pose->m_holds, RAGDOLL_HOLD_COUNT * sizeof(S_RagdollPush), {}, "holds"});
    for (S_Block &b : g_ragdollBlocks) {
        const auto *bytes = static_cast<const uint8_t *>(b.at);
        b.rest.assign(bytes, bytes + b.size);
        // (The w of their vectors left out: the original copies undefined ones in. Not the matrices' fourth
        // column: it is set.)
        for (size_t at = 0; at + 4 <= b.size; at += 4) {
            bool w = false;
            if (b.name.compare(0, 4, "body") == 0)
                // (And the world matrix's m[3][3], +0x9C: PlaceRagdollPoint copies a child's position into it as 16
                // bytes, from a stack temp whose w it never stores.)
                w = BodyVectorW(at) || at == 0x9C;
            else if (b.name.compare(0, 5, "point") == 0)
                w = at >= 0x30 && at < 0x150 && at % 16 == 12;
            else
                w = at % 0x30 == 0x1C;
            if (!w)
                g_sharedWords.push_back({reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(b.at) + at), b.name + "+" + std::to_string(at)});
        }
    }
    // The points' velocities (their steps and smoothed ones too) are differences of positions over a substep of 2..10 ms: a
    // position's float rounding (4e-6 m at 40 m) is 2e-3 m/s in them. Compared within 1e-2.
    for (S_SharedWord &w : g_sharedWords)
        for (S_RagdollPoint *point : pose->m_points) {
            const auto at = reinterpret_cast<uint8_t *>(w.first) - reinterpret_cast<uint8_t *>(point);
            if ((at >= 0x50 && at < 0x5C) || (at >= 0x70 && at < 0x7C) || (at >= 0xE0 && at < 0xEC))
                w.tolerance = 1e-2f;
        }
    const auto *p = reinterpret_cast<const uint8_t *>(pose);
    g_poseRest.assign(p, p + sizeof(C_HumanWeaponPose));
    for (int i = 0; i < RAGDOLL_BODY_COUNT; ++i)
        g_names[pose->m_bodies[i]] = "ragdollBody" + std::to_string(i);
    for (int i = 0; i < RAGDOLL_POINT_COUNT; ++i)
        g_names[pose->m_points[i]] = "ragdollPoint" + std::to_string(i);
    g_contactFrame = NewFrame(FRAME_DUMMY, "contactFrame", S_vector(30, 1, 30), Identity(), Box(S_vector(0.5f, 0.5f, 0.5f)), "", g_sector);
    g_sharedFrames.push_back(g_contactFrame);
    // The actors' frames (dynamic volumes the points may rest on): each side's own reference counts.
    for (S_ContactActor *c : g_contacts) {
        g_sharedFrames.push_back(c->frame);
        g_sharedWords.push_back({reinterpret_cast<uint32_t *>(&c->frame->m_refCount), c->name + std::string(" refs")});
    }
    g_sideHook = RagdollSide;
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(F_RagdollFindFrame);
    // (m_gravity.w, +0x68CC: CollideRagdollPoint's water branch copies the gravity in as 16 bytes from a stack temp
    // whose w it never stores.)
    g_poseSkipWords.insert(0x68CC);
    // The bone frames the ragdoll poses: each side starts from the same, their local matrices compared.
    for (I3D_frame *f : g_boneFrame) {
        g_sharedFrames.push_back(f);
        ShareBlock(&f->m_localMatrix, sizeof(S_matrix), std::string("bone ") + f->GetName());
    }
    // The original's side.
    RedirectOriginal(0x007b3c20, reinterpret_cast<const void *>(O_AddSettledBloodPool));
    RedirectOriginal(0x004de800, reinterpret_cast<const void *>(O_CarIsSimulatedHere));
    RedirectOriginal(0x004f2790, reinterpret_cast<const void *>(O_ApplyImpulse));
    g_origSoundVt[0x78 / 4] = reinterpret_cast<void *>(OS_GetSound);
}

// A ragdoll falling somewhere in the world: the built one moved there, its points shaken and moving, its state random.
void RandomRagdoll()
{
    RandomHumanBase();
    C_HumanWeaponPose *pose = Pose();
    std::memcpy(static_cast<void *>(pose), g_poseRest.data(), g_poseRest.size());
    for (S_Block &b : g_ragdollBlocks)
        std::memcpy(b.at, b.rest.data(), b.size);
    RagdollSide(false);
    float x = U(4, 60), z = U(4, 60);
    if (P(0.3f)) {
        x = U(30, 39);                                  // (by the actors: their boxes are dynamic volumes)
        z = U(42, 56);
    } else if (P(0.2f)) {
        x = U(53, 59);                                  // (over the pond: the pelvis finds the water)
        z = U(53, 59);
    }
    const S_vector to(x, GroundAt(x, z) + (P(0.4f) ? 0.0f : U(0.05f, 2.5f)), z);
    const S_vector shift = to - kBuildAt;
    const float shake = P(0.3f) ? 0.0f : U(0.0f, 0.15f);
    const S_vector fall(U(-3, 3), U(-6, 2), U(-3, 3));
    for (S_RagdollPoint *point : pose->m_points) {
        const S_vector jitter(U(-shake, shake), U(-shake, shake), U(-shake, shake));
        PoseSetXYZ(point->pos, PoseXYZ(point->pos) + shift + jitter);
        const S_vector v = fall + S_vector(U(-1, 1), U(-1, 1), U(-1, 1));
        PoseSetXYZ(point->oldPos, PoseXYZ(point->pos) - v * U(0.005f, 0.03f));
        PoseSetXYZ(point->lastPos, P(0.5f) ? PoseXYZ(point->oldPos) : PoseXYZ(point->pos) - v * U(0.0f, 0.05f));
        const int flags[] = {0, RAGDOLL_POINT_CONTACT, RAGDOLL_POINT_TOUCHING, RAGDOLL_POINT_CONTACT | RAGDOLL_POINT_TOUCHING};
        point->flags = static_cast<uint8_t>(flags[I(0, 3)]);
        S_vector n(U(-0.5f, 0.5f), P(0.8f) ? U(0.4f, 1.0f) : U(-1.0f, 1.0f), U(-0.5f, 0.5f));
        n.Normalize();
        PoseSetXYZ(point->normal, n);
        point->material = I(0, 0x60);
        point->weight = P(0.5f) ? 1.0f : U(0.0f, 1.0f);
        PoseSetXYZ(point->smoothVelocity, S_vector(U(-2, 2), P(0.3f) ? U(-0.4f, 0.4f) : U(-5, 2), U(-2, 2)));
        PoseSetXYZ(point->step, v * U(0.0f, 1.5f));
        PoseSetXYZ(point->force, S_vector(U(-5, 5), U(-5, 5), U(-5, 5)) * (P(0.5f) ? 0.0f : 1.0f));
        point->contactFrame = nullptr;
    }
    // Some points resting on an actor's frame already (each held once more); the counts as they would be.
    for (S_ContactActor *c : g_contacts)
        c->frame->m_refCount = 1;
    for (S_RagdollPoint *point : pose->m_points)
        if (P(0.15f) && !g_contacts.empty()) {
            point->contactFrame = g_contacts[I(0, static_cast<int>(g_contacts.size()) - 1)]->frame;
            point->contactFrame->AddRef();
            point->contactFramePos = PoseWorldPos4(point->contactFrame);
        }
    for (S_RagdollBody *body : pose->m_bodies) {
        body->world.m[3][0] += shift.x;
        body->world.m[3][1] += shift.y;
        body->world.m[3][2] += shift.z;
        PoseSetXYZ(body->savedStart, PoseXYZ(body->savedStart) + shift);
        PoseSetXYZ(body->savedEnd, PoseXYZ(body->savedEnd) + shift);
    }
    // The parts measured from their points (both sides start from the same).
    pose->UpdateRagdollBodies(pose->m_rootBody, P(0.5f), true);
    // The ragdoll's state.
    pose->m_ragdollWeight = P(0.5f) ? 1.0f : U(0.0f, 1.0f);
    pose->m_settleDelay = P(0.5f) ? U(-1.0f, -0.001f) : U(0.0f, 1.0f);
    pose->m_groundTime = U(-1.0f, 4.0f);
    pose->m_inWater = P(0.15f);
    pose->m_gravity = {0.0f, pose->m_inWater ? -2.25f : -9.0f, 0.0f, 0.0f};
    pose->m_steps = I(3, 6);
    pose->m_groundedPoints = static_cast<uint8_t>(P(0.5f) ? 0 : I(1, 16));
    pose->m_impactSound = P(0.6f) ? -1 : I(-2, 6);
    pose->m_dropSound = P(0.6f) ? -1 : I(-2, 6);
    pose->m_updateCount = I(0, 9);
    pose->m_bloodPool = P(0.5f);
    pose->m_sinceCollision = U(0.0f, 0.1f);
    // Dead on a vehicle (an actor's box) or not.
    g_ragdollVehicle = nullptr;
    pose->m_flags &= ~POSE_FLAG_ON_VEHICLE;
    if (P(0.25f)) {
        float best = 1e9f;                              // (the actor nearest: the one it would lie on)
        for (S_ContactActor *c : g_contacts) {
            const float d = (c->at - to).Square();
            if (c->port && d < best) {
                best = d;
                g_ragdollVehicle = c;
            }
        }
        if (g_ragdollVehicle)
            pose->m_flags |= POSE_FLAG_ON_VEHICLE;
    }
    RagdollSide(false);
    const bool full = P(0.1f);                         // (no free slot)
    for (int i = 0; i < RAGDOLL_PUSH_COUNT; ++i) {
        S_RagdollPush &push = pose->m_pushes[i];
        push.point = P(0.25f) || full ? pose->m_points[I(0, RAGDOLL_POINT_COUNT - 1)] : nullptr;
        push.force = {U(-80, 80), U(-40, 80), U(-80, 80), 0.0f};
        push.time = U(-0.01f, 0.3f);
        push.length = push.time;
    }
    for (int i = 0; i < RAGDOLL_HOLD_COUNT; ++i) {
        S_RagdollPush &hold = pose->m_holds[i];
        hold.point = P(0.25f) || full ? pose->m_points[I(0, RAGDOLL_POINT_COUNT - 1)] : nullptr;
        hold.force = {to.x + U(-1, 1), to.y + U(0, 2), to.z + U(-1, 1), 0.0f};
        hold.time = U(-0.01f, 0.3f);
    }
    // The detail of the body (the substeps).
    const uint32_t details[] = {0, MOVE_FLAG_LARGE, MOVE_FLAG_FULL_DETAIL, MOVE_FLAG_SMALL, MOVE_FLAG_TINY, MOVE_FLAG_HIDDEN, MOVE_FLAG_NOT_DRAWN};
    g_h.movement->m_flags = details[I(0, 6)];
    g_soundGone = P(0.3f);
    g_script.soundPlaying = P(0.5f);
}

typedef void(__attribute__((thiscall)) * T_PInt)(void *, int);
typedef void(__attribute__((thiscall)) * T_PVoid)(void *);
typedef void(__attribute__((thiscall)) * T_PFloat)(void *, float);
typedef void(__attribute__((thiscall)) * T_PPointFloat)(void *, void *, float);
typedef void(__attribute__((thiscall)) * T_PBodyBools)(void *, void *, int, int);
typedef void(__attribute__((thiscall)) * T_PPointOut)(void *, void *, S_vector4 *);
typedef void(__attribute__((thiscall)) * T_PBodyOuts)(void *, void *, S_vector4 *, S_vector4 *);
typedef void(__attribute__((thiscall)) * T_PPoint)(void *, void *);
typedef void(__attribute__((thiscall)) * T_PPointVecInt)(void *, void *, const S_vector4 *, int);
typedef void(__attribute__((thiscall)) * T_PPointBool)(void *, void *, int);

bool Near4(const S_vector4 &a, const S_vector4 &b)
{
    const auto close = [](float x, float y) { return std::fabs(x - y) <= 2e-4f * std::max(1.0f, std::fabs(x)) || (x != x && y != y); };
    return close(a.x, b.x) && close(a.y, b.y) && close(a.z, b.z);
}

// One function at a time from random states.
void TestRagdollParts()
{
    for (int i = 0; i < 9000 / QUICK; ++i) {
        RandomRagdoll();
        C_HumanWeaponPose *pose = Pose();
        const float dt = U(0.003f, 0.02f);
        S_RagdollPoint *point = pose->m_points[I(0, RAGDOLL_POINT_COUNT - 1)];
        S_RagdollBody *body = pose->m_bodies[I(0, RAGDOLL_BODY_COUNT - 1)];
        void *p = pose;
        switch (i % 12) {
        case 0:
            DiffCall("IntegrateRagdollPoints", [&] { pose->IntegrateRagdollPoints(pose->m_rootPoint, dt); return 0; },
                     [&](void *) { OriginalAt<T_PPointFloat>(0x007c7e50)(p, pose->m_rootPoint, dt); return 0; });
            break;
        case 1: {
            const bool save = P(0.5f), rebuild = P(0.7f);
            DiffCall("UpdateRagdollBodies", [&] { pose->UpdateRagdollBodies(pose->m_rootBody, save, rebuild); return 0; },
                     [&](void *) { OriginalAt<T_PBodyBools>(0x007c8540)(p, pose->m_rootBody, save, rebuild); return 0; });
            break;
        }
        case 2:
            DiffCall("ConstrainRagdollBody", [&] { pose->ConstrainRagdollBody(pose->m_rootBody, dt); return 0; },
                     [&](void *) { OriginalAt<T_PPointFloat>(0x007c8f70)(p, pose->m_rootBody, dt); return 0; });
            break;
        case 3:
            pose->m_collideStep = P(0.5f);
            DiffCall("ApplyRagdollCorrections", [&] { pose->ApplyRagdollCorrections(); return 0; },
                     [&](void *) { OriginalAt<T_PVoid>(0x007cbf40)(p); return 0; });
            break;
        case 4:
            DiffCall("CollideRagdoll", [&] { pose->CollideRagdoll(dt); return 0; },
                     [&](void *) { OriginalAt<T_PFloat>(0x007cc1b0)(p, dt); return 0; });
            break;
        case 5: {
            if (P(0.3f)) {
                // The pelvis sweeping through the pond's surface (y 3), going down or up: it finds the water.
                point = pose->m_points[RAGDOLL_POINT_PELVIS];
                const float x = U(53, 59), z = U(53, 59), down = P(0.7f) ? 1.0f : -1.0f;
                PoseSetXYZ(point->lastPos, S_vector(x, 3.0f + down * U(0.05f, 0.4f), z));
                PoseSetXYZ(point->pos, S_vector(x + U(-0.1f, 0.1f), 3.0f - down * U(0.05f, 0.4f), z + U(-0.1f, 0.1f)));
                PoseSetXYZ(point->step, S_vector(U(-0.1f, 0.1f), -down * U(0.01f, 0.2f) * (P(0.2f) ? -1.0f : 1.0f), U(-0.1f, 0.1f)));
                pose->m_inWater = P(0.5f);
                pose->m_gravity = {U(-1, 1), U(-9, -2), U(-1, 1), 0.0f};
            }
            alignas(16) S_vector4 outPort = {7, 7, 7, 0}, outOrig = {7, 7, 7, 0};
            DiffCall("CollideRagdollPoint", [&] { pose->CollideRagdollPoint(point, outPort); return 0; },
                     [&](void *) { OriginalAt<T_PPointOut>(0x007cc3f0)(p, point, &outOrig); return 0; });
            CHECK(Near4(outPort, outOrig));
            break;
        }
        case 6: {
            alignas(16) S_vector4 sPort = {7, 7, 7, 0}, ePort = {7, 7, 7, 0}, sOrig = {7, 7, 7, 0}, eOrig = {7, 7, 7, 0};
            DiffCall("CollideRagdollBody", [&] { pose->CollideRagdollBody(body, sPort, ePort); return 0; },
                     [&](void *) { OriginalAt<T_PBodyOuts>(0x007ccba0)(p, body, &sOrig, &eOrig); return 0; });
            CHECK(Near4(sPort, sOrig) && Near4(ePort, eOrig));
            break;
        }
        case 7:
            DiffCall("SettleRagdoll", [&] { pose->SettleRagdoll(pose->m_rootPoint); return 0; },
                     [&](void *) { OriginalAt<T_PPoint>(0x007cd690)(p, pose->m_rootPoint); return 0; });
            break;
        case 8: {
            const S_vector4 force = {U(-90, 90), U(-90, 90), U(-90, 90), 0.0f};
            const int time = I(0, 500);
            S_RagdollPoint *target = P(0.1f) ? nullptr : point;
            if (P(0.5f))
                DiffCall("AddRagdollPush", [&] { pose->AddRagdollPush(target, force, time); return 0; },
                         [&](void *) { OriginalAt<T_PPointVecInt>(0x007cda70)(p, target, &force, time); return 0; });
            else
                DiffCall("AddRagdollHold", [&] { pose->AddRagdollHold(target, force, time); return 0; },
                         [&](void *) { OriginalAt<T_PPointVecInt>(0x007cdb20)(p, target, &force, time); return 0; });
            break;
        }
        case 9:
            DiffCall("ApplyRagdollPushes", [&] { pose->ApplyRagdollPushes(dt); return 0; },
                     [&](void *) { OriginalAt<T_PFloat>(0x007cdb90)(p, dt); return 0; });
            break;
        case 10: {
            // Points resting on the moving frame (held twice: by the test and the point), it moved or not.
            for (S_RagdollPoint *pt : pose->m_points)
                if (P(0.3f)) {
                    pt->contactFrame = g_contactFrame;
                    g_contactFrame->AddRef();
                    pt->contactFramePos = P(0.5f) ? S_vector4{30.0f, 1.0f, 30.0f, 0.0f}
                                                  : S_vector4{30.0f + U(-0.1f, 0.1f), 1.0f + U(-0.1f, 0.1f), 30.0f + U(-0.1f, 0.1f), 0.0f};
                }
            g_contactFrame->m_refCount = P(0.2f) ? 1 : g_contactFrame->m_refCount;
            if (P(0.3f))
                pose->m_ragdollWeight = 0.0f;               // (settled: a moving contact wakes it)
            const int time = I(5, 120);
            if (P(0.7f))
                DiffCall("UpdateRagdollContacts", [&] { pose->UpdateRagdollContacts(time); return 0; },
                         [&](void *) { OriginalAt<T_PInt>(0x007cdca0)(p, time); return 0; });
            else
                DiffCall("ReleaseRagdollContacts", [&] { pose->ReleaseRagdollContacts(); return 0; },
                         [&](void *) { OriginalAt<T_PVoid>(0x007cdea0)(p); return 0; });
            for (S_RagdollPoint *pt : pose->m_points)
                pt->contactFrame = nullptr;
            g_contactFrame->m_refCount = 1;
            break;
        }
        default: {
            const bool second = P(0.5f);
            if (P(0.5f))
                DiffCall("PlayRagdollSound", [&] { pose->PlayRagdollSound(point, second); return 0; },
                         [&](void *) { OriginalAt<T_PPointBool>(0x007ce260)(p, point, second); return 0; });
            else
                DiffCall("UpdateRagdollSound", [&] { pose->UpdateRagdollSound(second); return 0; },
                         [&](void *) { OriginalAt<T_PInt>(0x007ce370)(p, second); return 0; });
            break;
        }
        }
    }
}

// Whole steps: a ragdoll falling and settling, step after step. (A step is 3..6 substeps of the functions above,
// each of which agrees: collisions and limits there turn on thresholds, so a last-bit difference of a position can
// flip one (a pelvis axis from nearly parallel hips, a joint limit side). The steps must agree in 99 % of the calls.)
void TestRagdollSteps()
{
    g_softDiff = true;
    for (int i = 0; i < 300 / QUICK; ++i) {
        RandomRagdoll();
        C_HumanWeaponPose *pose = Pose();
        void *p = pose;
        for (int step = 0; step < 30; ++step) {
            const int time = I(10, 60);
            DiffCall("StepRagdoll", [&] { pose->StepRagdoll(time); return 0; },
                     [&](void *) { OriginalAt<T_PInt>(0x007c7c40)(p, time); return 0; });
        }
    }
    g_softDiff = false;
    const S_Stats &st = g_stats["StepRagdoll"];
    CHECK(st.agree >= st.calls * 99 / 100);
}

// The model posed by the ragdoll, the rest pose and its blends, points placed, a part measured, saved and loaded.
typedef void(__attribute__((thiscall)) * T_PBody)(void *, const void *);
typedef void(__attribute__((thiscall)) * T_PBodyFloat)(void *, const void *, float);
typedef void(__attribute__((thiscall)) * T_PMeasure)(void *, void *, int, int, int, int, int);
typedef bool(__attribute__((thiscall)) * T_PAddBone)(void *, void *, int);
typedef void(__attribute__((thiscall)) * T_PChunkBody)(void *, C_chunk *, const void *);
void TestRagdollPose()
{
    const std::string portFile = TempFile("ragdoll_port.chk"), origFile = TempFile("ragdoll_orig.chk");
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomRagdoll();
        C_HumanWeaponPose *pose = Pose();
        void *p = pose;
        // The bones a little turned (their own local matrices: the weight's slerp reads them).
        for (I3D_frame *f : g_boneFrame) {
            S_quat q;
            q.Make(S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(), U(-0.5f, 0.5f));
            f->SetRot(q);
        }
        UpdateTree(g_h.frame);
        S_RagdollBody *body = pose->m_bodies[I(0, RAGDOLL_BODY_COUNT - 1)];
        S_RagdollPoint *point = pose->m_points[I(0, RAGDOLL_POINT_COUNT - 1)];
        const float weight = P(0.4f) ? 1.0f : U(0.0f, 1.0f);
        switch (i % 12) {
        case 0:
            DiffCall("RagdollBoneMatrices", [&] { pose->RagdollBoneMatrices(pose->m_rootBody); return 0; },
                     [&](void *) { OriginalAt<T_PBody>(0x007c5e20)(p, pose->m_rootBody); return 0; });
            break;
        case 1:
            pose->RagdollBoneMatrices(pose->m_rootBody);
            DiffCall("SetRagdollBones", [&] { pose->SetRagdollBones(body, weight); return 0; },
                     [&](void *) { OriginalAt<T_PBodyFloat>(0x007c5ef0)(p, body, weight); return 0; });
            break;
        case 2: {
            const S_RagdollBody *from = P(0.3f) ? nullptr : body;
            DiffCall("PoseFromRagdoll", [&] { pose->PoseFromRagdoll(from, weight); return 0; },
                     [&](void *) { OriginalAt<T_PBodyFloat>(0x007c5d10)(p, from, weight); return 0; });
            break;
        }
        case 3:
            DiffCall("StoreRagdollRest", [&] { C_HumanWeaponPose::StoreRagdollRest(pose->m_rootBody); return 0; },
                     [&](void *) { OriginalAt<T_PBody>(0x007cdfd0)(p, pose->m_rootBody); return 0; });
            break;
        case 4:
            // (The blend's start moved off the rest pose: the reset puts it back.)
            for (S_RagdollBody *b : pose->m_bodies) {
                S_quat q;
                q.Make(S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(), U(-1.0f, 1.0f));
                b->fromRot = q;
                b->from.m[3][1] += U(-0.1f, 0.1f);
                b->fromLength = b->restLength * U(0.8f, 1.2f);
            }
            DiffCall("ResetRagdollToRest", [&] { C_HumanWeaponPose::ResetRagdollToRest(pose->m_rootBody); return 0; },
                     [&](void *) { OriginalAt<T_PBody>(0x007cdef0)(p, pose->m_rootBody); return 0; });
            break;
        case 5: {
            const float t = P(0.2f) ? (P(0.5f) ? 0.0f : 1.0f) : U(0.0f, 1.0f);
            C_HumanWeaponPose::StoreRagdollRest(pose->m_rootBody);
            for (S_RagdollBody *b : pose->m_bodies) {
                S_quat q;
                q.Make(S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(), U(-1.0f, 1.0f));
                b->fromRot = q;
                b->from.m[3][0] += U(-0.1f, 0.1f);
                b->fromLength = b->restLength * U(0.8f, 1.2f);
            }
            DiffCall("BlendRagdollToRest", [&] { C_HumanWeaponPose::BlendRagdollToRest(pose->m_rootBody, t); return 0; },
                     [&](void *) { OriginalAt<T_PBodyFloat>(0x007ce0f0)(p, pose->m_rootBody, t); return 0; });
            break;
        }
        case 6: {
            S_RagdollPoint *at = P(0.2f) ? nullptr : point;
            const S_vector4 pos = {U(4, 60), U(1, 5), U(4, 60), P(0.5f) ? 0.0f : 1.0f};
            const bool stop = P(0.5f);
            DiffCall("PlaceRagdollPoint", [&] { pose->PlaceRagdollPoint(at, pos, stop); return 0; },
                     [&](void *) { OriginalAt<T_PPointVecInt>(0x007c6560)(p, at, &pos, stop); return 0; });
            break;
        }
        case 7: {
            // A part measured again from two bones (or its own), what it sets chosen.
            const int start = P(0.3f) ? -1 : I(0, POSE_BONE_COUNT - 1);
            int end = P(0.3f) ? -1 : I(0, POSE_BONE_COUNT - 1);
            if (end == start)
                end = -1;
            const bool length = P(0.5f), thickness = P(0.5f), roll = P(0.5f);
            DiffCall("MeasureRagdollBody", [&] { pose->MeasureRagdollBody(body, start, end, length, thickness, roll); return 0; },
                     [&](void *) { OriginalAt<T_PMeasure>(0x007c5740)(p, body, start, end, length, thickness, roll); return 0; });
            break;
        }
        case 8: {
            // A bone more for a part (its tables have room only for the bones the build gave it, and the function
            // checks only 16: one is taken off first).
            if (body->boneCount > 0)
                --body->boneCount;
            const int bone = I(0, POSE_BONE_COUNT - 1);
            DiffCall("AddRagdollBone", [&] { return int(pose->AddRagdollBone(body, bone)); },
                     [&](void *) { return int(OriginalAt<T_PAddBone>(0x007c5a80)(p, body, bone)); });
            break;
        }
        case 9:
        case 10: {
            // Saved: the parts or the points (the files byte for byte), then loaded back into the ragdoll moved on. Some
            // points on an actor's frame (named in the file when something else holds it too: found again by name).
            for (S_RagdollPoint *pt : pose->m_points) {
                if (pt->contactFrame)
                    pt->contactFrame->Release();
                pt->contactFrame = nullptr;
                if (P(0.2f) && !g_contacts.empty()) {
                    pt->contactFrame = g_contacts[I(0, static_cast<int>(g_contacts.size()) - 1)]->frame;
                    pt->contactFrame->AddRef();
                    if (P(0.5f))
                        pt->contactFrame->AddRef();
                }
            }
            const bool bodies = i % 12 == 9;
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            DiffCall(bodies ? "SaveRagdollBodies" : "SaveRagdollPoints",
                     [&] {
                         C_chunk c;
                         c.Open(portFile.c_str(), true);
                         if (bodies)
                             C_HumanWeaponPose::SaveRagdollBodies(c, pose->m_rootBody);
                         else
                             C_HumanWeaponPose::SaveRagdollPoints(c, pose->m_rootPoint);
                         return 0;
                     },
                     [&](void *) {
                         C_chunk c;
                         c.Open(origFile.c_str(), true);
                         if (bodies)
                             OriginalAt<T_PChunkBody>(0x007c3c90)(p, &c, pose->m_rootBody);
                         else
                             OriginalAt<T_PChunkBody>(0x007c4a70)(p, &c, pose->m_rootPoint);
                         return 0;
                     });
            CHECK(FileBytes(portFile) == FileBytes(origFile));
            CHECK(FileBytes(portFile).size() > 1000);
            pose->StepRagdoll(I(10, 60));
            // The links the file gives scrambled first (each read before it is followed; the children only cleared: a
            // leaf's load frees whatever child it finds).
            const auto anyBody = [&]() { return P(0.2f) ? nullptr : pose->m_bodies[I(0, RAGDOLL_BODY_COUNT - 1)]; };
            const auto anyPoint = [&]() { return P(0.2f) ? nullptr : pose->m_points[I(0, RAGDOLL_POINT_COUNT - 1)]; };
            if (P(0.7f)) {
                for (S_RagdollBody *b : pose->m_bodies) {
                    // (And the quaternions, which the step does not change: the file gives them back.)
                    for (S_quat *q : {&b->rot, &b->restRot, &b->fromRot})
                        q->Make(S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(), U(-1.0f, 1.0f));
                    if (b != pose->m_rootBody)
                        b->parent = anyBody();
                    b->start = anyPoint();
                    b->end = anyPoint();
                    if (bodies && P(0.3f))
                        b->children[0] = b->children[1] = b->children[2] = nullptr;
                }
                for (S_RagdollPoint *pt : pose->m_points) {
                    if (pt != pose->m_rootPoint)
                        pt->parent = anyPoint();
                    for (S_RagdollBody *&b : pt->bodies)
                        b = anyBody();
                    if (!bodies && P(0.3f))
                        pt->children[0] = pt->children[1] = pt->children[2] = nullptr;
                }
            }
            DiffCall(bodies ? "LoadRagdollBodies" : "LoadRagdollPoints",
                     [&] {
                         C_chunk c;
                         c.Open(portFile.c_str(), false);
                         if (bodies)
                             pose->LoadRagdollBodies(c, pose->m_rootBody);
                         else
                             pose->LoadRagdollPoints(c, pose->m_rootPoint);
                         return 0;
                     },
                     [&](void *) {
                         C_chunk c;
                         c.Open(portFile.c_str(), false);
                         if (bodies)
                             OriginalAt<T_PChunkBody>(0x007c42a0)(p, &c, pose->m_rootBody);
                         else
                             OriginalAt<T_PChunkBody>(0x007c4f40)(p, &c, pose->m_rootPoint);
                         return 0;
                     });
            break;
        }
        default:
            DiffCall("PoseFromRagdoll", [&] { pose->PoseFromRagdoll(nullptr, weight); return 0; },
                     [&](void *) { OriginalAt<T_PBodyFloat>(0x007c5d10)(p, nullptr, weight); return 0; });
            break;
        }
    }
}

// ---- building and freeing: each side builds its own ragdoll (the original's _aligned_malloc / _aligned_free, its
// own C runtime's, redirected to the test's: zeroed blocks, as the port's), compared part by part with the pointers
// as numbers (parts 1..11, points 1..16) and the bone tables by their contents; then each frees its own.
void *O_AlignedMalloc(size_t size, size_t alignment)
{
    void *p = _aligned_malloc(size, alignment);
    if (p)
        std::memset(p, 0, size);
    return p;
}
void O_AlignedFree(void *p) { _aligned_free(p); }

struct S_RagdollCanon {
    std::vector<uint32_t> words;
    std::vector<bool> quaternion;   // (words of the parts' quaternions: 5e-3 apart allowed, S_quat::Make's branch on a
                                    // nearly orthonormal matrix)
};
uint32_t BodyIndex(const C_HumanWeaponPose *pose, const void *p)
{
    for (int i = 0; i < RAGDOLL_BODY_COUNT; ++i)
        if (pose->m_bodies[i] == p)
            return static_cast<uint32_t>(i + 1);
    return p ? 0xEEEEu : 0u;
}
uint32_t PointIndex(const C_HumanWeaponPose *pose, const void *p)
{
    for (int i = 0; i < RAGDOLL_POINT_COUNT; ++i)
        if (pose->m_points[i] == p)
            return static_cast<uint32_t>(i + 1);
    return p ? 0xEEEEu : 0u;
}
void PutWords(std::vector<uint32_t> &out, const void *p, size_t size)
{
    const auto *w = static_cast<const uint32_t *>(p);
    out.insert(out.end(), w, w + size / 4);
}
// The pose (its own pointers and the sides' owner, movement and vehicle left out) and the ragdoll it has.
S_RagdollCanon CanonRagdoll(const C_HumanWeaponPose *pose)
{
    S_RagdollCanon c;
    std::vector<uint32_t> poseWords;
    PutWords(poseWords, pose, sizeof(C_HumanWeaponPose));
    const auto zero = [&](size_t from, size_t to) {
        for (size_t i = from / 4; i < to / 4; ++i)
            poseWords[i] = 0;
    };
    zero(0x0, 0x8);
    zero(0x6414, 0x6418);
    zero(0x6848, 0x68BC);
    zero(0x68DC, 0x68E8);
    zero(0x68CC, 0x68D0);   // (m_gravity's w: the original's build leaves an undefined one)
    c.words = poseWords;
    for (int i = 0; i < RAGDOLL_BODY_COUNT; ++i) {
        const S_RagdollBody *b = pose->m_bodies[i];
        c.words.push_back(b ? 1u : 0u);
        if (!b)
            continue;
        std::vector<uint32_t> w;
        PutWords(w, b, sizeof(S_RagdollBody));
        w[0x4 / 4] = BodyIndex(pose, b->parent);
        for (int k = 0; k < 3; ++k)
            w[0x8 / 4 + k] = BodyIndex(pose, b->children[k]);
        w[0x1A8 / 4] = w[0x1AC / 4] = 0;
        w[0x1F0 / 4] = PointIndex(pose, b->start);
        w[0x1F4 / 4] = PointIndex(pose, b->end);
        // (The w of its vectors: the original's build copies undefined ones in.)
        for (size_t at = 0; at < sizeof(S_RagdollBody); at += 4)
            if (BodyVectorW(at))
                w[at / 4] = 0;
        c.quaternion.resize(c.words.size(), false);
        for (size_t k = 0; k < w.size(); ++k) {
            const size_t at = k * 4;
            c.quaternion.push_back((at >= 0xA0 && at < 0xB0) || (at >= 0x130 && at < 0x150));
        }
        c.words.insert(c.words.end(), w.begin(), w.end());
        for (int k = 0; k < b->boneCount; ++k) {
            c.words.push_back(static_cast<uint32_t>(b->bones[k]));
            PutWords(c.words, &b->boneOffsets[k], sizeof(S_matrix));
        }
    }
    for (int i = 0; i < RAGDOLL_POINT_COUNT; ++i) {
        const S_RagdollPoint *p = pose->m_points[i];
        c.words.push_back(p ? 1u : 0u);
        if (!p)
            continue;
        std::vector<uint32_t> w;
        PutWords(w, p, sizeof(S_RagdollPoint));
        for (int k = 0; k < 4; ++k)
            w[0x4 / 4 + k] = BodyIndex(pose, p->bodies[k]);
        w[0x14 / 4] = PointIndex(pose, p->parent);
        for (int k = 0; k < 3; ++k)
            w[0x18 / 4 + k] = PointIndex(pose, p->children[k]);
        for (size_t at = 0x30; at < 0x150; at += 16)        // (likewise its vectors' w)
            w[(at + 12) / 4] = 0;
        c.words.insert(c.words.end(), w.begin(), w.end());
    }
    c.words.push_back(PointIndex(pose, pose->m_rootPoint));
    c.words.push_back(BodyIndex(pose, pose->m_rootBody));
    if (pose->m_restMatrices)
        PutWords(c.words, pose->m_restMatrices, RAGDOLL_REST_BONE_COUNT * sizeof(S_matrix));
    if (pose->m_pushes)
        PutWords(c.words, pose->m_pushes, RAGDOLL_PUSH_COUNT * sizeof(S_RagdollPush));
    if (pose->m_holds)
        PutWords(c.words, pose->m_holds, RAGDOLL_HOLD_COUNT * sizeof(S_RagdollPush));
    return c;
}
std::string CanonDiffers(const S_RagdollCanon &a, const S_RagdollCanon &b)
{
    if (a.words.size() != b.words.size())
        return "size " + std::to_string(a.words.size()) + " / " + std::to_string(b.words.size());
    for (size_t i = 0; i < a.words.size(); ++i) {
        if (i < a.quaternion.size() && a.quaternion[i]) {
            float fa, fb;
            std::memcpy(&fa, &a.words[i], 4);
            std::memcpy(&fb, &b.words[i], 4);
            // (Or negated: q and -q are the same rotation; the quaternion of a matrix picks its sign on a near tie.)
            if (std::fabs(fa - fb) <= 5e-3f || std::fabs(fa + fb) <= 5e-3f)
                continue;
        }
        if (!WordsNear(a.words[i], b.words[i], 'm')) {
            float fa, fb;
            std::memcpy(&fa, &a.words[i], 4);
            std::memcpy(&fb, &b.words[i], 4);
            return "word " + std::to_string(i) + " (" + std::to_string(fa) + " / " + std::to_string(fb) + ")";
        }
    }
    return "";
}
void DetachRagdoll(C_HumanWeaponPose *pose)
{
    for (S_RagdollBody *&b : pose->m_bodies)
        b = nullptr;
    for (S_RagdollPoint *&p : pose->m_points)
        p = nullptr;
    pose->m_rootBody = nullptr;
    pose->m_rootPoint = nullptr;
    pose->m_restMatrices = nullptr;
    pose->m_pushes = nullptr;
    pose->m_holds = nullptr;
}
void FreeExtras(C_HumanWeaponPose *pose)
{
    _aligned_free(pose->m_restMatrices);
    _aligned_free(pose->m_pushes);
    _aligned_free(pose->m_holds);
}

void TestRagdollBuild()
{
    typedef void(__attribute__((thiscall)) * T_PBool)(void *, int);
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(O_AlignedMalloc));
    RedirectOriginal(0x007e559c, reinterpret_cast<const void *>(O_AlignedFree));
    long builds = 0, agree = 0;
    for (int i = 0; i < 1000 / QUICK; ++i) {
        RandomRagdoll();
        C_HumanWeaponPose *pose = Pose();
        void *p = pose;
        // The skeleton bent and the model turned and moved.
        g_h.frame->SetPosInline(S_vector(U(4, 60), U(1, 3), U(4, 60)));
        S_quat yaw;
        yaw.Make(S_vector(0, 1, 0), U(0, 6.2831f));
        g_h.frame->SetRot(yaw);
        for (const S_BoneAt &b : kBones) {
            I3D_frame *f = g_boneFrame[b.bone];
            f->SetPosInline(b.at + S_vector(U(-0.05f, 0.05f), U(-0.05f, 0.05f), U(-0.05f, 0.05f)));
            S_quat q;
            q.Make(S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(), U(-0.4f, 0.4f));
            f->SetRot(q);
        }
        UpdateTree(g_h.frame);
        std::vector<uint8_t> before(reinterpret_cast<uint8_t *>(pose), reinterpret_cast<uint8_t *>(pose) + sizeof(C_HumanWeaponPose));
        // The port's.
        DetachRagdoll(pose);
        ContactsFor(false);
        pose->BuildRagdoll(false);
        const S_RagdollCanon portBuilt = CanonRagdoll(pose);
        FreeExtras(pose);
        pose->FreeRagdollBody(pose->m_rootBody);
        pose->FreeRagdollPoint(pose->m_rootPoint);
        pose->m_restMatrices = nullptr;
        pose->m_pushes = pose->m_holds = nullptr;
        const S_RagdollCanon portFreed = CanonRagdoll(pose);
        // The original's.
        std::memcpy(static_cast<void *>(pose), before.data(), before.size());
        DetachRagdoll(pose);
        ContactsFor(true);
        WithOriginalWorldMapped(true, [](S_CollisionVolume *v) { return v; }, [&] {
            OriginalAt<T_PBool>(0x007c11f0)(p, 0);
            return 0;
        });
        ContactsFor(false);
        const S_RagdollCanon origBuilt = CanonRagdoll(pose);
        FreeExtras(pose);
        ContactsFor(true);
        OriginalAt<T_PBody>(0x007c5600)(p, pose->m_rootBody);
        OriginalAt<T_PBody>(0x007c64b0)(p, pose->m_rootPoint);
        ContactsFor(false);
        pose->m_restMatrices = nullptr;
        pose->m_pushes = pose->m_holds = nullptr;
        const S_RagdollCanon origFreed = CanonRagdoll(pose);
        std::memcpy(static_cast<void *>(pose), before.data(), before.size());
        ++builds;
        const std::string built = CanonDiffers(portBuilt, origBuilt), freed = CanonDiffers(portFreed, origFreed);
        if (built.empty() && freed.empty())
            ++agree;
        else if (builds - agree <= 5)
            std::printf("  BuildRagdoll differs: %s%s\n", built.empty() ? "" : ("built " + built).c_str(),
                        freed.empty() ? "" : (" freed " + freed).c_str());
        CHECK(built.empty() && freed.empty());
    }
    std::printf("  BuildRagdoll / Free: %ld, agree %ld\n", builds, agree);
    // The model back as the other tests have it.
    g_h.frame->SetPosInline(kBuildAt);
    S_quat none;
    none.Identity();
    g_h.frame->SetRot(none);
    for (const S_BoneAt &b : kBones) {
        g_boneFrame[b.bone]->SetPosInline(b.at);
        g_boneFrame[b.bone]->SetRot(none);
    }
    UpdateTree(g_h.frame);
}

void TestRagdoll()
{
    SetUpRagdoll();
    TestRagdollParts();
    TestRagdollSteps();
    TestRagdollPose();
    TestRagdollBuild();
}
}  // namespace

// ---- the port's side of the ragdoll's outside calls
bool C_Car::IsSimulatedHere()
{
    Log(this, "CarIsSimulatedHere");
    return g_carSimulated;
}
void C_Vehicle::ApplyImpulse(const S_vector &impulse, const S_vector &point, uint8_t flags, const S_vector &factors)
{
    Log(this, "ApplyImpulse", {impulse.x, impulse.y, impulse.z, point.x, point.y, point.z, double(flags), factors.x, factors.y, factors.z});
}
I3D_sound *C_SoundModule::GetSound(int slot) const
{
    Log(this, "GetSound", {double(slot)});
    return g_soundGone ? nullptr : static_cast<I3D_sound *>(static_cast<void *>(&g_sound));
}
#ifndef HUMAN_DEAD   // (the dead body's build links game/human_pose_dead.cpp: its own)
void C_HumanWeaponPose::AddSettledBloodPool() { Log(this, "AddSettledBloodPool"); }
#endif
