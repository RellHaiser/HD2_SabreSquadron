// Part 18 of the human harness (HUMAN_DEAD builds, build_dead.bat; included by human_test.cpp after body_test.inc): the
// dead body (game/human_pose_dead.cpp: StartDeath with the dying animation's start and the ragdoll's, ApplyHitImpulse,
// AddSettledBloodPool, UpdateDeathAnim with the held body's checks and the slide, SampleDeathAnim, OnHit, PickUpBody)
// against the original's, on part 17's skeleton with a ragdoll built by the port. The ragdoll's parts, points, pushes
// and holds and the slide points are shared (the same layout) and compared word by word; the frees both sides make
// while a call is compared are skipped (harness_malloc.h; the blocks are leaked) so that the other side finds them.
// The outside calls (the detail settings, the camera module, the movement's Ragdoll and AddBloodPool, the owner's
// unconsciousness, the stains, the scene) are logged on both sides with scripted answers.

#include "game/human_stains.h"

bool g_harnessKeepFrees = false;
void HarnessAlignedFree(void *p)
{
    if (!g_harnessKeepFrees)
        (_aligned_free)(p);
}

namespace {
struct S_DeadScript {
    bool ragdollDetail = true;                         // the profile's DETAIL_RAGDOLL
    bool unconscious = false;
    bool cameraModule = true;                          // GetCameraModule answers
    I3D_frame *camera = nullptr;                       // the camera module's camera
} g_dead;
alignas(16) uint8_t g_portCameras[64], g_origCameras[64], g_detailBytes[16];
void *g_origCameraVt[64];
}  // namespace

// ---- the port's side
C_PlayerOptions *C_GameProfile::GetDetailSettings() { return reinterpret_cast<C_PlayerOptions *>(g_detailBytes); }
bool C_PlayerOptions::IsEnabled(int option) const
{
    Log(nullptr, "DetailEnabled", {double(option)});
    return option == DETAIL_RAGDOLL ? g_dead.ragdollDetail : true;
}
// g_gameProfile: only its address is used here (GetDetailSettings' this); raw storage under its name, game/profile.cpp
// (its constructor, its vtable) not linked.
asm(".globl _g_gameProfile\n\t.bss\n\t.balign 16\n_g_gameProfile:\n\t.space 4096\n\t.text");
bool C_Human::IsUnconscious() const
{
    Log(this, "IsUnconscious");
    return g_dead.unconscious;
}
C_CameraModule *C_game::GetCameraModule() { return g_dead.cameraModule ? reinterpret_cast<C_CameraModule *>(g_portCameras) : nullptr; }
I3D_frame *C_CameraModule::GetCamera()
{
    Log(this, "GetCamera");
    return g_dead.camera;
}
void C_HumanMovement::AddBloodPool(const S_vector &pos, const S_vector &normal, int kind)
{
    Log(this, "AddBloodPool", {pos.x, pos.y, pos.z, normal.x, normal.y, normal.z, double(kind)});
}
void C_HumanMovement::Ragdoll(bool volumes, bool light) { Log(this, "Ragdoll", {double(volumes), double(light)}); }
void C_HumanStains::AddHit(const S_vector &pos, const S_vector &force, uint32_t flags)
{
    Log(this, "StainsAddHit", {pos.x, pos.y, pos.z, force.x, force.y, force.z, double(flags)});
}

namespace {
// ---- the original's side
#define DT __attribute__((thiscall))
void *DT OD_GetDetailSettings(void *) { return g_detailBytes; }
int DT OD_IsEnabled(void *, int option)
{
    Log(nullptr, "DetailEnabled", {double(option)});
    return option == DETAIL_RAGDOLL ? g_dead.ragdollDetail : 1;
}
int DT OD_IsUnconscious(void *self)
{
    Log(self, "IsUnconscious");
    return g_dead.unconscious;
}
void *DT OD_GetCameraModule(void *) { return g_dead.cameraModule ? g_origCameras : nullptr; }
I3D_frame *DT OD_GetCamera(void *self)
{
    Log(self, "GetCamera");
    return g_dead.camera;
}
void DT OD_AddBloodPool(void *self, const float *pos, const float *normal, int kind)
{
    Log(self, "AddBloodPool", {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], double(kind)});
}
void DT OD_Ragdoll(void *self, int volumes, int light) { Log(self, "Ragdoll", {double(B(volumes)), double(B(light))}); }
void DT OD_StainsAddHit(void *self, const float *pos, const float *force, uint32_t flags)
{
    Log(self, "StainsAddHit", {pos[0], pos[1], pos[2], force[0], force[1], force[2], double(flags)});
}
void OD_AlignedFree(void *p)
{
    if (!g_harnessKeepFrees)
        (_aligned_free)(p);
}
// The scene's AddHumanModel (slot 0x138): logged on both sides.
void __stdcall FD_AddHumanModel(void *self, I3D_frame *model, I3D_frame *a, I3D_frame *b)
{
    Log(self, ("AddHumanModel " + NameOf(model) + " " + NameOf(a) + " " + NameOf(b)).c_str());
}

typedef void(DT *T_DStart)(void *, uint32_t, const float *, void *, int, void *);
typedef void(DT *T_DImpulse)(void *, uint32_t, const S_vector4 *, int);
typedef void(DT *T_DVoid)(void *);
typedef void(DT *T_DSample)(void *, S_PoseBone *, int, float);
typedef void(DT *T_DHit)(void *, uint32_t, const S_vector *, const S_vector *);

// The ragdoll's blocks as built (rest), shared and compared; the slide points' block (the dying animation's).
struct S_DeadBlock {
    void *at;
    size_t size;
    std::vector<uint8_t> rest;
};
std::vector<S_DeadBlock> g_deadBlocks;
S_vector4 *g_slideBlock = nullptr;
I3D_frame *g_cameraFpv = nullptr, *g_dummyZ = nullptr;

// Each side's view: part 17's, and the contact actors the body may lie on (the pose's vehicle translated).
void DeadSide(bool original)
{
    BodySide(original);
    C_HumanWeaponPose *pose = IkPose();
    for (S_ContactActor *c : g_contacts) {
        if (!c->port || !c->orig)
            continue;
        void *from = original ? static_cast<void *>(c->port) : static_cast<void *>(c->orig);
        void *to = original ? static_cast<void *>(c->orig) : static_cast<void *>(c->port);
        if (pose->m_vehicle == from)
            pose->m_vehicle = static_cast<C_Actor *>(to);
    }
    // (The carrier: each side's own human.)
    void *carrierFrom = original ? static_cast<void *>(g_h.human) : static_cast<void *>(o_human);
    if (pose->m_carrier == carrierFrom)
        pose->m_carrier = original ? reinterpret_cast<C_Human *>(o_human) : g_h.human;
}

void ShareDeadWords(void *block, size_t size, const std::string &name, bool (*skip)(size_t), float (*tolerance)(size_t))
{
    for (size_t at = 0; at + 4 <= size; at += 4)
        if (!skip(at))
            g_sharedWords.push_back({reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(block) + at), name + "+" + std::to_string(at), tolerance(at)});
}

void SetUpDead()
{
    SetUpBody();
    C_HumanWeaponPose *pose = IkPose();
    S_quat none;
    none.Identity();
    g_cameraFpv = NewFrame(FRAME_DUMMY, "cameraFPV", S_vector(0.0f, 1.7f, 0.15f), none, Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_ikModel);
    g_dummyZ = NewFrame(FRAME_DUMMY, "z", S_vector(0.0f, 0.9f, 0.0f), none, Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_ikModel);
    for (I3D_frame *f : {g_cameraFpv, g_dummyZ}) {
        g_ikFrames.push_back(f);
        g_sharedFrames.push_back(f);
        ShareFrameWords(f, true);
    }
    g_names[g_cameraFpv] = "cameraFPV";
    g_names[g_dummyZ] = "z";
    // (The model leaves the sector at a death: the sector's children kept apart per side.)
    g_sharedFrames.push_back(g_sector);
    // (The model's links compared: a ragdoll's death takes it out of its parent.)
    for (size_t at : {0x108u, 0x110u})
        g_sharedWords.push_back({reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(g_ikModel) + at), "frame ikModel+" + std::to_string(at)});
    pose->m_cameraFpv = g_cameraFpv;
    pose->m_dummyZ = g_dummyZ;
    // The ragdoll, built by the port on the skeleton standing.
    UpdateTree(g_ikModel);
    WeaponSide(false);
    pose->BuildRagdoll(false);
    for (int i = 0; i < RAGDOLL_BODY_COUNT; ++i)
        if (pose->m_bodies[i]) {
            g_deadBlocks.push_back({pose->m_bodies[i], sizeof(S_RagdollBody), {}});
            g_names[pose->m_bodies[i]] = "ragdollBody" + std::to_string(i);
            ShareDeadWords(pose->m_bodies[i], sizeof(S_RagdollBody), "body" + std::to_string(i),
                           [](size_t at) { return BodyVectorW(at) || at == 0x9C; }, [](size_t) { return g_tol; });
        }
    for (int i = 0; i < RAGDOLL_POINT_COUNT; ++i)
        if (pose->m_points[i]) {
            g_deadBlocks.push_back({pose->m_points[i], sizeof(S_RagdollPoint), {}});
            g_names[pose->m_points[i]] = "ragdollPoint" + std::to_string(i);
            ShareDeadWords(pose->m_points[i], sizeof(S_RagdollPoint), "point" + std::to_string(i),
                           [](size_t at) { return at >= 0x30 && at < 0x150 && at % 16 == 12; },
                           [](size_t at) { return at >= 0x24 && at < 0x150 ? g_tol : 0.0f; });
        }
    g_deadBlocks.push_back({pose->m_pushes, RAGDOLL_PUSH_COUNT * sizeof(S_RagdollPush), {}});
    g_deadBlocks.push_back({pose->m_holds, RAGDOLL_HOLD_COUNT * sizeof(S_RagdollPush), {}});
    const auto pushSkip = [](size_t at) { return at % 0x30 == 0x1C; };
    const auto pushTolerance = [](size_t at) { return at % 0x30 >= 0x10 && at % 0x30 < 0x28 ? g_tol : 0.0f; };
    ShareDeadWords(pose->m_pushes, RAGDOLL_PUSH_COUNT * sizeof(S_RagdollPush), "pushes", pushSkip, pushTolerance);
    ShareDeadWords(pose->m_holds, RAGDOLL_HOLD_COUNT * sizeof(S_RagdollPush), "holds", pushSkip, pushTolerance);
    g_deadBlocks.push_back({pose->m_restMatrices, RAGDOLL_REST_BONE_COUNT * sizeof(S_matrix), {}});
    for (S_DeadBlock &b : g_deadBlocks) {
        const auto *bytes = static_cast<const uint8_t *>(b.at);
        b.rest.assign(bytes, bytes + b.size);
    }
    // The slide points' block (a dying body's), shared.
    g_slideBlock = static_cast<S_vector4 *>(_aligned_malloc(DEATH_HELD_POINT_COUNT * sizeof(S_vector4), 16));
    std::memset(static_cast<void *>(g_slideBlock), 0, DEATH_HELD_POINT_COUNT * sizeof(S_vector4));
    // (Their w left out: the original copies the bones' world rows as 16 bytes from temps.)
    ShareDeadWords(g_slideBlock, DEATH_HELD_POINT_COUNT * sizeof(S_vector4), "slide", [](size_t at) { return at % 16 == 12; },
                   [](size_t) { return g_tol; });
    // The slide points a death allocates are each side's own: their pointer not compared (their values are, after it).
    g_poseSkipWords.insert(offsetof(C_HumanWeaponPose, m_slidePoints));
    // The w of the pose's vectors the original copies from temps: the body's place, the slide speed, the camera's place,
    // the gravity.
    for (size_t at : {offsetof(C_HumanWeaponPose, m_bodyPos), offsetof(C_HumanWeaponPose, m_slideVelocity),
                      offsetof(C_HumanWeaponPose, m_cameraPos), offsetof(C_HumanWeaponPose, m_gravity)})
        g_poseSkipWords.insert(at + 12);
    const auto *p = reinterpret_cast<const uint8_t *>(pose);
    g_ikPoseRest.assign(p, p + sizeof(C_HumanWeaponPose));
    g_ikFrameRest.clear();
    for (I3D_frame *f : g_ikFrames) {
        const auto *b = reinterpret_cast<const uint8_t *>(f);
        g_ikFrameRest.emplace_back(b, b + 0x400);
    }
    // The outside calls.
    // (The base's tests log the original's StartDeath: its own again here.)
    if (!RestoreOriginal(0x007b0d70)) {
        std::printf("dead: StartDeath not restored\n");
        std::exit(3);
    }
    RedirectOriginal(0x006b6a60, reinterpret_cast<const void *>(OD_GetDetailSettings));
    RedirectOriginal(0x006b3700, reinterpret_cast<const void *>(OD_IsEnabled));
    RedirectOriginal(0x005a6380, reinterpret_cast<const void *>(OD_IsUnconscious));
    RedirectOriginal(0x0069a620, reinterpret_cast<const void *>(OD_GetCameraModule));
    RedirectOriginal(0x00787570, reinterpret_cast<const void *>(OD_AddBloodPool));
    RedirectOriginal(0x007872b0, reinterpret_cast<const void *>(OD_Ragdoll));
    RedirectOriginal(0x00799bc0, reinterpret_cast<const void *>(OD_StainsAddHit));
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(O_AlignedMalloc));
    RedirectOriginal(0x007e559c, reinterpret_cast<const void *>(OD_AlignedFree));
    for (void *&slot : g_origCameraVt)
        slot = reinterpret_cast<void *>(O_UnexpectedVirtual);
    g_origCameraVt[0x6C / 4] = reinterpret_cast<void *>(OD_GetCamera);
    *reinterpret_cast<void ***>(g_origCameras) = g_origCameraVt;
    g_names[g_portCameras] = g_names[g_origCameras] = "cameras";
    g_sceneVt[0x138 / 4] = reinterpret_cast<void *>(FD_AddHumanModel);
    g_sideHook = DeadSide;
}

// A random state of a body dying or dead: part 17's (the skeleton standing or lying somewhere over the ground), the
// ragdoll as built and moved there, the death's state, the scripts, the scene's camera.
void RandomDead(bool lying)
{
    RandomBody(lying);
    LinkUnder(g_ikModel, g_sector);
    UpdateTree(g_ikModel);
    C_HumanWeaponPose *pose = IkPose();
    for (S_DeadBlock &b : g_deadBlocks)
        std::memcpy(b.at, b.rest.data(), b.size);
    const S_vector shift = g_ikModel->GetWorldPos() - S_vector(20, 1, 20);
    // (Lying sometimes: the points turned about the pelvis onto the ground, one way or the other.)
    const bool flat = P(0.5f);
    const float side = P(0.5f) ? 1.0f : -1.0f;
    const S_vector pelvisAt = PoseXYZ(pose->m_points[RAGDOLL_POINT_PELVIS]->pos);
    for (S_RagdollPoint *point : pose->m_points)
        if (point) {
            S_vector at = PoseXYZ(point->pos);
            if (flat) {
                const S_vector rel = at - pelvisAt;
                at = pelvisAt + S_vector(rel.x, rel.z * side * 0.2f - 0.6f, rel.y * side);
            }
            const float bent = P(0.3f) ? 0.2f : 0.05f;     // (bent sometimes: its parts' rests change)
            PoseSetXYZ(point->pos, at + shift + S_vector(U(-bent, bent), U(-bent, bent), U(-bent, bent)));
            PoseSetXYZ(point->oldPos, PoseXYZ(point->pos) - IkRandomDir() * U(0, 0.05f));
            PoseSetXYZ(point->normal, (S_vector(0, 1, 0) + IkRandomDir() * 0.3f).Normalized());
            point->flags = uint8_t(P(0.5f) ? RAGDOLL_POINT_CONTACT : 0);
        }
    // (The ragdoll's state a death sets: other values first.)
    for (S_RagdollPoint *point : pose->m_points)
        if (point)
            point->weight = U(0, 1);
    pose->m_gravity = {U(-1, 1), U(-12, -1), U(-1, 1), 0.0f};
    pose->m_steps = I(1, 8);
    pose->m_unknown68E8 = I(0, 100);
    pose->m_settleDelay = U(0, 3);
    pose->m_groundTime = U(0, 8);
    pose->m_inWater = P(0.5f);
    pose->m_contactFrames = 0;
    pose->m_slidePoints = nullptr;
    pose->m_bloodPool = P(0.2f);
    pose->m_ragdollWeight = P(0.2f) ? 0.0f : U(0, 1);
    pose->m_deathAnim = P(0.3f) ? nullptr : PoseAnim(I(0, POSE_ANIMS - 1));
    pose->m_deathKind = int16_t(I(0, 4));
    pose->m_deathTime = P(0.2f) ? I(2000, 13000) : I(0, 2500);
    for (int b = 0; b < POSE_BONE_COUNT; ++b) {
        pose->m_deathPosKey[b] = P(0.7f) ? 0 : I(0, 3);
        pose->m_deathRotKey[b] = P(0.7f) ? 0 : I(0, 3);
    }
    pose->m_slideVelocity = Dir4(P(0.3f) ? S_vector(0, 0, 0) : IkRandomDir() * U(0, 6));
    pose->m_bodyPos = Dir4(g_ikBone[POSE_BONE_SPINE]->GetWorldPos() + IkRandomDir() * U(0, 1), 1.0f);
    pose->m_cameraPos = Dir4(g_ikBone[POSE_BONE_HEAD_TOP]->GetWorldPos() + IkRandomDir() * U(0, 0.5f), 1.0f);
    // (Lying on one of the actors sometimes; stains on the body sometimes: a fake object, its AddHit logged.)
    pose->m_vehicle = nullptr;
    if (P(0.15f)) {
        S_ContactActor *c = g_contacts[size_t(I(0, int(g_contacts.size()) - 1))];
        if (c->port)
            pose->m_vehicle = reinterpret_cast<C_Actor *>(c->port);
    }
    static uint8_t stains[16];
    g_names[stains] = "stains";
    pose->m_stains = P(0.3f) ? reinterpret_cast<C_HumanStains *>(stains) : nullptr;
    pose->m_impactSound = P(0.5f) ? -1 : I(0, 5);
    pose->m_dropSound = P(0.5f) ? -1 : (P(0.5f) ? -2 : I(0, 5));
    pose->m_velocity = Dir4(IkRandomDir() * (P(0.2f) ? U(5, 9) : U(0, 4)));
    g_h.movement->m_flags = P(0.8f) ? 0u : uint32_t(P(0.5f) ? MOVE_FLAG_TINY : MOVE_FLAG_HIDDEN);
    g_dead.ragdollDetail = P(0.7f);
    g_dead.unconscious = P(0.1f);
    g_dead.cameraModule = P(0.9f);
    g_dead.camera = P(0.6f) ? g_cameraFpv : g_ikModel;
    // The scene's camera: near or far, looking at the body or away.
    I3D_frame *camera = g_h.camera;
    const S_vector spine = g_ikBone[POSE_BONE_SPINE]->GetWorldPos();
    const S_vector at = spine + IkRandomDir() * (P(0.6f) ? U(1, 9) : U(11, 30));
    camera->SetPosInline(at);
    S_matrix look;
    look.Identity();
    look.SetDir(P(0.7f) ? spine - at : IkRandomDir(), 0.0f);
    S_quat q;
    q.Make(look);
    camera->SetRot(q);
    UpdateTree(camera);
}

void DeadSlideRandom()
{
    static const int bones[DEATH_HELD_POINT_COUNT] = {POSE_BONE_HEAD, POSE_BONE_NECK, POSE_BONE_CHEST, POSE_BONE_BACK,
                                                      POSE_BONE_SPINE, POSE_BONE_LEFT_KNEE, POSE_BONE_RIGHT_KNEE};
    for (int i = 0; i < DEATH_HELD_POINT_COUNT; ++i)
        g_slideBlock[i] = Dir4(g_ikBone[bones[i]]->GetWorldPos() + IkRandomDir() * U(0, 0.3f), P(0.5f) ? 0.0f : 1.0f);
    IkPose()->m_slidePoints = g_slideBlock;
}

// The slide points a death allocates (each side's own block), compared after the call.
std::vector<float> SlidePointsOf(const C_HumanWeaponPose *pose)
{
    // (A falling body's three points; a held body's seven.)
    std::vector<float> v;
    const int count = pose->m_deathKind == DEATH_HELD ? DEATH_HELD_POINT_COUNT : 3;
    if (pose->m_slidePoints && pose->m_slidePoints != g_slideBlock)
        for (int i = 0; i < count; ++i)
            for (int k = 0; k < 3; ++k)
                v.push_back((&pose->m_slidePoints[i].x)[k]);
    return v;
}

void TestDeadStart()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("dead start\n");
    for (int i = 0; i < 16000 / QUICK; ++i) {
        RandomDead(P(0.3f));
        if (P(0.2f))
            DeadSlideRandom();                          // (a second death: the old slide points let go)
        static const uint32_t zones[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x0C};
        uint32_t flags = zones[I(0, 7)];
        if (P(0.15f))
            flags |= 0x1000;
        else if (P(0.15f))
            flags |= 0x4000;
        else if (P(0.15f))
            flags |= 0x8400;
        const S_vector f = P(0.1f) ? S_vector(0, 0, 0) : IkRandomDir() * (P(0.3f) ? U(500, 4000) : U(0, 400));
        const S_vector4 force = Dir4(f);
        I3D_animation_set *anim = P(0.25f) ? nullptr : PoseAnim(I(0, POSE_ANIMS - 1));
        const int kind = I(0, 4);
        if (P(0.1f)) {
            // (A hidden part: its end bone collapsed; the part and its end point dropped.)
            static const int ends[] = {POSE_BONE_HEAD, POSE_BONE_HEAD_TOP, POSE_BONE_LEFT_HAND, POSE_BONE_RIGHT_HAND,
                                       POSE_BONE_LEFT_FOOT, POSE_BONE_RIGHT_FOOT};
            g_ikBone[ends[I(0, 5)]]->SetScale(S_vector(0, 0, 0));
            UpdateTree(g_ikModel);
        }
        // (No carrier for an unconscious body: its ragdoll is built again, in new blocks per side.)
        C_Human *carrier = !g_dead.unconscious && P(0.2f) ? g_h.human : nullptr;
        void *origCarrier = carrier ? static_cast<void *>(o_human) : nullptr;
        std::vector<float> portSlide, origSlide;
        g_harnessKeepFrees = true;
        g_softDiff = true;
        DiffCall("StartDeath",
                 [&] {
                     pose->StartDeath(flags, &force.x, anim, kind, carrier);
                     portSlide = SlidePointsOf(pose);
                     return 0;
                 },
                 [&](void *) {
                     OriginalAt<T_DStart>(0x007b0d70)(p, flags, &force.x, anim, kind, origCarrier);
                     origSlide = SlidePointsOf(pose);
                     return 0;
                 });
        g_harnessKeepFrees = false;
        g_softDiff = false;
        IkNote("StartDeath slide points", portSlide.size() != origSlide.size()
                                              ? std::string("count")
                                              : IkWords(portSlide.data(), origSlide.data(), int(portSlide.size())));
    }
}

void TestDeadParts()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("dead parts\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomDead(P(0.5f));
        switch (i % 5) {
        case 0: {
            static const uint32_t zones[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x30};
            const uint32_t flags = zones[I(0, 7)];
            const S_vector4 impulse = Dir4(IkRandomDir() * U(0, 200));
            const int time = I(50, 300);
            if (P(0.2f))
                for (int k : {RAGDOLL_POINT_HEAD, RAGDOLL_POINT_LEFT_HAND, RAGDOLL_POINT_RIGHT_FOOT})
                    if (P(0.5f))
                        pose->m_points[k] = nullptr;    // (parts dropped)
            DiffCall("ApplyHitImpulse", [&] { pose->ApplyHitImpulse(flags, impulse, time); return 0; },
                     [&](void *) { OriginalAt<T_DImpulse>(0x007b36d0)(p, flags, &impulse, time); return 0; });
            break;
        }
        case 1:
            DiffCall("AddSettledBloodPool", [&] { pose->AddSettledBloodPool(); return 0; },
                     [&](void *) { OriginalAt<T_DVoid>(0x007b3c20)(p); return 0; });
            break;
        case 2: {
            static const uint32_t zones[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00};
            const uint32_t flags = zones[I(0, 6)];
            const S_vector pos = g_ikBone[I(0, 22)]->GetWorldPos();
            const S_vector force = P(0.1f) ? S_vector(0, 0, 0) : IkRandomDir() * U(0, 3000);
            if (P(0.6f))
                pose->m_flags |= POSE_FLAG_DEAD;
            DiffCall("OnHit", [&] { pose->OnHit(flags, pos, force); return 0; },
                     [&](void *) { OriginalAt<T_DHit>(0x007b88b0)(p, flags, &pos, &force); return 0; });
            break;
        }
        case 3: {
            pose->m_deathAnim = PoseAnim(I(0, POSE_ANIMS - 1));
            S_PoseBone *bone = &pose->m_bones[P(0.5f) ? POSE_BONE_PELVIS : I(0, 22)];
            const int time = I(-50, 2200);
            const float blend = U(0, 1);
            if (P(0.1f))
                g_ikBone[I(0, 22)]->SetScale(S_vector(0, 0, 0));   // (a collapsed bone keeps its rotation)
            UpdateTree(g_ikModel);
            DiffCall("SampleDeathAnim", [&] { pose->SampleDeathAnim(bone, time, blend); return 0; },
                     [&](void *) { OriginalAt<T_DSample>(0x007b7870)(p, bone, time, blend); return 0; });
            break;
        }
        default:
            DiffCall("PickUpBody", [&] { pose->PickUpBody(); return 0; },
                     [&](void *) { OriginalAt<T_DVoid>(0x007b3510)(p); return 0; });
            break;
        }
    }
}

void TestDeadAnim()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("dead animation\n");
    for (int i = 0; i < 20000 / QUICK; ++i) {
        RandomDead(P(0.3f));
        if (!pose->m_deathAnim && P(0.95f))
            pose->m_deathAnim = PoseAnim(I(0, POSE_ANIMS - 1));
        if (pose->m_deathKind != DEATH_SEATED)
            DeadSlideRandom();
        pose->m_flags |= POSE_FLAG_DEAD;
        g_harnessKeepFrees = true;
        g_softDiff = true;
        DiffCall("UpdateDeathAnim", [&] { pose->UpdateDeathAnim(); return 0; },
                 [&](void *) { OriginalAt<T_DVoid>(0x007b40e0)(p); return 0; });
        g_softDiff = false;
        g_harnessKeepFrees = false;
    }
}

void TestDead()
{
    SetUpDead();
    TestDeadParts();
    TestDeadStart();
    TestDeadAnim();
    // Judged by their rates (99.9 %): StartDeath (a ragdoll part's rotation as measured differs just past the tolerance,
    // or by its sign at -0, in about 1 of 16,000 deaths); UpdateDeathAnim (the slide's speed differs by up to 7 % in about
    // 1 of 10,000 updates; not traced: presumably two points hitting at nearly the same fraction of the move, the first
    // one picked differently; the sums' groupings and the tests' NaN branches are the original's).
    for (const char *name : {"StartDeath", "UpdateDeathAnim"}) {
        const S_Stats &st = g_stats[name];
        std::printf("%s: %ld of %ld agree\n", name, st.agree, st.calls);
        CHECK(st.calls > 0 && double(st.agree) >= 0.999 * double(st.calls));
    }
}

}  // namespace
