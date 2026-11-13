// Part 19 of the human harness (HUMAN_CORE builds, build_core.bat; included by human_test.cpp after dead_test.inc): the
// weapon pose's core (game/human_weapon_pose.cpp: Update with every mode, the blends, the dead body's update and the
// cameras; Refresh; BindSkeleton; Attach and Reset without a model; SetStainsVisible) against the original's, on part
// 18's skeleton, ragdoll, weapons and vehicles. The stains are a fake object (their calls logged on both sides).
// (Attach with a model builds a new ragdoll, in new blocks per side, and the saved games are not in this part.)

// ---- the stains (game/human_stains.cpp is not linked): logged
C_HumanStains::C_HumanStains(C_HumanWeaponPose *pose) { Log(this, "StainsNew", {double(pose != nullptr)}); }
C_HumanStains::~C_HumanStains() { Log(this, "StainsDelete"); }
void C_HumanStains::Bind(char mode) { Log(this, "StainsBind", {double(mode)}); }
void C_HumanStains::SaveGame(C_chunk &) { Log(this, "StainsSave"); }
void C_HumanStains::LoadGame(C_chunk &) { Log(this, "StainsLoad"); }
void C_HumanStains::SkipSaved(C_chunk &) { Log(nullptr, "StainsSkip"); }
void C_HumanStains::SetVisible(bool on) { Log(this, "StainsVisible", {double(on)}); }

// The animation cache (its saved animations by name; not used by this part's calls).
asm(".globl _g_animationCache\n\t.bss\n\t.balign 16\n_g_animationCache:\n\t.space 256\n\t.text");
template <>
HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *fileName, I3D_animation_set **out, T_CacheErrorProc, void *)
{
    Log(this, (std::string("AnimationCacheGet ") + fileName).c_str());
    *out = nullptr;
    return 0;
}

namespace {
#define CT __attribute__((thiscall))
void CT OC_StainsBind(void *self, int mode) { Log(self, "StainsBind", {double(static_cast<char>(mode))}); }
void CT OC_StainsVisible(void *self, int on) { Log(self, "StainsVisible", {double(B(on))}); }
void CT OC_StainsDelete(void *self) { Log(self, "StainsDelete"); }

typedef void(CT *T_CInt)(void *, int);
typedef void(CT *T_CAttach)(void *, I3D_frame *, void *, int);
typedef void(CT *T_CVoid)(void *);

alignas(16) uint8_t g_stainsBytes[64];
C_HumanStains *Stains() { return reinterpret_cast<C_HumanStains *>(g_stainsBytes); }

void SetUpCore()
{
    SetUpDead();
    // (The bone frames' short names are the joint names BindSkeleton finds: "ik_u" -> "u".)
    for (I3D_frame *f : g_ikBone)
        f->m_nameOffset = 3;
    const auto *p = reinterpret_cast<const uint8_t *>(IkPose());
    g_ikFrameRest.clear();
    for (I3D_frame *f : g_ikFrames) {
        const auto *b = reinterpret_cast<const uint8_t *>(f);
        g_ikFrameRest.emplace_back(b, b + 0x400);
    }
    g_ikPoseRest.assign(p, p + sizeof(C_HumanWeaponPose));
    g_names[g_stainsBytes] = "stains";
    // (The velocities' w: copied from temps, undefined.)
    g_poseSkipWords.insert(offsetof(C_HumanWeaponPose, m_velocity) + 12);
    g_poseSkipWords.insert(offsetof(C_HumanWeaponPose, m_sampledVelocity) + 12);
    // (The ragdoll's sounds at rest: GetSound as in part 7.)
    g_origSoundVt[0x78 / 4] = reinterpret_cast<void *>(OS_GetSound);
    RedirectOriginal(0x00798720, reinterpret_cast<const void *>(OC_StainsBind));
    RedirectOriginal(0x007997b0, reinterpret_cast<const void *>(OC_StainsVisible));
    RedirectOriginal(0x007986b0, reinterpret_cast<const void *>(OC_StainsDelete));
}

// A living body's state for the updates: part 16's (a weapon in the hands or none, its modes, blends and timers), the
// prone fit, the owner alive or dead, the cameras.
void RandomCoreLiving()
{
    const bool lying = P(0.3f);
    RandomDead(lying);
    C_HumanWeaponPose *pose = IkPose();
    bool armed = false;
    pose->m_flags &= ~(POSE_FLAG_DEAD | POSE_FLAG_STEER | POSE_FLAG_STATIC_WEAPON | POSE_FLAG_SEAT_WEAPON | POSE_FLAG_PASSENGER);
    // (Holding needs a weapon with grips in a hand: KeepsWeapon lets go of anything else, which it is tested with too.)
    if ((pose->m_flags & (POSE_FLAG_HOLD | POSE_FLAG_GRIP)) && P(0.85f)) {
        RandomWeapon(true, true);
        armed = true;
        pose->m_flags = (pose->m_flags | POSE_FLAG_GRIP) & ~(POSE_FLAG_DEAD | POSE_FLAG_STEER | POSE_FLAG_STATIC_WEAPON |
                                                             POSE_FLAG_SEAT_WEAPON | POSE_FLAG_PASSENGER);
        if (!pose->m_weapon->GetParent())
            LinkUnder(pose->m_weapon, (pose->m_hand & 0xFF) == 0 ? g_hw2 : g_hw1);
        HumanAtModel();
        LinkUnder(g_ikModel, g_sector);
        UpdateTree(g_ikModel);
    }
    // (The prone fit on the body test's lying skeleton: on part 16's bent ones the original's FitProneBody can take an
    // acos past 1, whose errno crashes its unstarted CRT. A fit under a hold is not in this test.)
    pose->m_proneFit = lying && !armed && P(0.6f);
    g_h.work->m_mode = P(0.9f) ? 0 : WORK_MODE_SWIMMING;
    g_h.human->m_stats[STAT_HEALTH] = P(0.1f) ? 0 : 80;
    pose->m_velocityTime = I(-20, 100);
    pose->m_lastPos = Dir4(g_ikModel->GetWorldPos() + IkRandomDir() * U(0, 0.5f), 1.0f);
    g_dead.camera = P(0.4f) ? g_camera3rd : (P(0.6f) ? g_cameraFpv : g_ikModel);
    g_h.movement->m_flags = P(0.7f) ? 0u : uint32_t(P(0.5f) ? MOVE_FLAG_TINY : MOVE_FLAG_NOT_DRAWN);
    // (Holding, the kick and the support hand need the grips: SetMode holds only weapons with them.)
    if (pose->m_weapon && !pose->m_rightGrip)
        pose->m_flags &= ~(POSE_FLAG_SUPPORT_OFF | POSE_FLAG_RECOIL | POSE_FLAG_HOLD | POSE_FLAG_FREE | POSE_FLAG_GRIP);
    if (!pose->m_leftGrip)
        pose->m_hand &= ~0x80000000u;
    // (A weapon without a left grip is a pistol: the rifles' holds put the left hand on theirs.)
    if (pose->m_weapon && !pose->m_leftGrip)
        pose->m_weaponKind = WEAPON_KIND_PISTOL;
    // (Raising and lowering divide by the blend's length: RaiseWeapon / LowerWeapon set one.)
    if (pose->m_blendLength < 1)
        pose->m_blendLength = I(1, 600);
    // (The hand 0 or 1 as SetMode leaves it: HoldProne's direction is undefined for 2 and 3.)
    pose->m_hand = (pose->m_hand & 0x80000000u) | (pose->m_hand & 0xFF) % 2;
}

void TestCoreLiving()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("core living\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomCoreLiving();
        const int time = P(0.1f) ? 0 : I(1, 150);
        // (Sometimes the timers run out exactly or one short: the blend at its length, at 0 or 1, the pause and the
        // support hand's at 0 or 1.)
        if (P(0.25f)) {
            const int edge = I(0, 1);
            pose->m_blendTime = P(0.5f) ? pose->m_blendLength - time + edge : time + edge;
            pose->m_pauseTime = time + edge;
            pose->m_supportTime = P(0.5f) ? 200 - time + edge : time + edge;
        }
        g_softDiff = true;
        if (i % 3 == 2) {
            pose->m_updateSeen = P(0.2f) ? pose->m_updateCount : pose->m_updateCount - 1;
            DiffCall("Refresh", [&] { pose->Refresh(time); return 0; },
                     [&](void *) { OriginalAt<T_CInt>(0x007a8760)(p, time); return 0; });
        } else {
            DiffCall("Update", [&] { pose->Update(time); return 0; },
                     [&](void *) { OriginalAt<T_CInt>(0x007a78d0)(p, time); return 0; });
        }
        g_softDiff = false;
    }
}

// In a seat: the pose put there by the port's SetSeat (steering, a seat weapon, a passenger, a static weapon).
void TestCoreSeats()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("core seats\n");
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomDead(false);
        RandomVehicles();
        pose->m_flags = 0;
        // (The owner alive and on land, the movement shown: the living test leaves them random.)
        g_h.human->m_stats[STAT_HEALTH] = 80;
        g_h.work->m_mode = 0;
        g_h.movement->m_flags = 0;
        g_seat.hasSeatWeapon = false;
        BodySide(false);
        const int seat = I(0, 5);
        switch (i % 4) {
        case 0:
            pose->SetSeat(SEAT_FLAG_CONTROLS, Vehicle(VEH_CAR), seat);
            break;
        case 1:
            g_seat.hasSeatWeapon = true;
            pose->SetSeat(SEAT_FLAG_SIT | SEAT_FLAG_WEAPON, Vehicle(VEH_CAR), seat);
            break;
        case 2:
            pose->SetSeat(SEAT_FLAG_SIT, Vehicle(VEH_CAR_BARE), seat);
            break;
        default:
            pose->SetSeat(0, Vehicle(VEH_GUN), seat);
            pose->m_seat = seat;
            break;
        }
        RandomVehicles();
        pose->m_accelTime = I(-50, 200);
        g_dead.camera = P(0.4f) ? g_camera3rd : g_cameraFpv;
        const int time = I(1, 150);
        g_softDiff = true;
        if (P(0.3f)) {
            pose->m_updateSeen = pose->m_updateCount - 1;
            DiffCall("Refresh seat", [&] { pose->Refresh(time); return 0; },
                     [&](void *) { OriginalAt<T_CInt>(0x007a8760)(p, time); return 0; });
        } else {
            DiffCall("Update seat", [&] { pose->Update(time); return 0; },
                     [&](void *) { OriginalAt<T_CInt>(0x007a78d0)(p, time); return 0; });
        }
        g_softDiff = false;
    }
}

// A dead body's state for its update. A ragdoll's step (100 ms of its simulation) starts from a fall: the ragdoll let
// go standing and stepped by the port a few times first. (Part 18's random states, bent and half settled, are
// ill-conditioned there: one ulp of the pelvis moves the port's own step past the tolerance in about half of them,
// CORE_SENS 1; they are used for the other paths only.)
int RandomCoreDead(bool fall)
{
    C_HumanWeaponPose *pose = IkPose();
    RandomDead(fall ? false : P(0.5f));
    pose->m_flags |= POSE_FLAG_DEAD;
    // (Not unconscious: the end of the dying animation would build its ragdoll again, in new blocks per side.)
    g_dead.unconscious = false;
    g_h.work->m_mode = 0;
    g_h.human->m_stats[STAT_HEALTH] = 0;
    if (P(0.3f))
        pose->m_carrier = g_h.human;
    g_dead.camera = P(0.6f) ? g_cameraFpv : g_ikModel;
    const int time = I(1, 150);
    if (!fall) {
        if (pose->m_deathAnim && pose->m_deathKind != DEATH_SEATED)
            DeadSlideRandom();
        pose->m_unknown68E8 = time + I(1, 100);
        return time;
    }
    pose->m_deathAnim = nullptr;
    pose->m_ragdollWeight = 1.0f;
    const S_vector push = IkRandomDir() * U(0, 3);
    for (S_RagdollPoint *point : pose->m_points)
        if (point) {
            point->weight = 1.0f;
            point->flags = 0;
            PoseSetXYZ(point->oldPos, PoseXYZ(point->pos) - push * 0.01f);
        }
    g_harnessKeepFrees = true;
    for (int k = I(0, 12); k > 0; --k) {
        pose->m_unknown68E8 = 0;
        pose->Update(100);
    }
    g_harnessKeepFrees = false;
    pose->m_unknown68E8 = I(-20, time);
    return time;
}

// A looser float tolerance for one comparison (the words' own and WordsNear's).
struct S_LooseTolerance {
    float saved;
    std::vector<float> savedWords;
    explicit S_LooseTolerance(float tol) : saved(g_tol)
    {
        g_tol = tol;
        for (S_SharedWord &w : g_sharedWords) {
            savedWords.push_back(w.tolerance);
            if (w.tolerance > 0.0f)
                w.tolerance = tol;
        }
    }
    ~S_LooseTolerance()
    {
        g_tol = saved;
        for (size_t k = 0; k < savedWords.size(); ++k)
            g_sharedWords[k].tolerance = savedWords[k];
    }
};
// The ragdoll's step: one ulp of the pelvis moves the port's own fall past 1 % in 4.5 % of the steps (CORE_SENS 2,
// CORE_TOL 1e-2), past 2e-4 in two thirds of them.
const float kStepTolerance = 1e-2f;

// The dead body's update: the dying animation, or the ragdoll (stepping, blended between its steps, or at rest).
void TestCoreDead()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("core dead\n");
    for (int i = 0; i < 12000 / QUICK; ++i) {
        const int time = RandomCoreDead(i % 3 == 0);
        g_harnessKeepFrees = true;
        g_softDiff = true;
        const char *path = pose->m_deathAnim                ? "Update dead anim"
                           : !(pose->m_ragdollWeight > 1e-8f) ? "Update dead rest"
                           : pose->m_unknown68E8 - time < 1    ? "Update dead step"
                                                               : "Update dead blend";
        std::unique_ptr<S_LooseTolerance> loose;
        if (std::strcmp(path, "Update dead step") == 0)
            loose.reset(new S_LooseTolerance(kStepTolerance));
        DiffCall(path, [&] { pose->Update(time); return 0; },
                 [&](void *) { OriginalAt<T_CInt>(0x007a78d0)(p, time); return 0; });
        g_softDiff = false;
        g_harnessKeepFrees = false;
    }
}

// (CORE_SENS: how often the port's own dead step moves past the tolerance when the pelvis moves by one ulp; 1: part
// 18's random states, 2: the falls, 0: neither moved, the reruns' baseline.)
void CoreSensitivity()
{
    C_HumanWeaponPose *pose = IkPose();
    const int mode = std::atoi(std::getenv("CORE_SENS"));
    const float tol = std::getenv("CORE_TOL") ? float(std::atof(std::getenv("CORE_TOL"))) : g_tol;
    int steps = 0, moved = 0;
    for (int i = 0; i < 4000; ++i) {
        int time = RandomCoreDead(mode != 1);
        if (mode == 1) {
            pose->m_deathAnim = nullptr;
            pose->m_ragdollWeight = U(0.1f, 1.0f);
        }
        pose->m_unknown68E8 = I(-20, 0);
        const S_PortState before = TakePort();
        std::vector<uint32_t> shared;
        for (const auto &w : g_sharedWords)
            shared.push_back(*w.first);
        g_harnessKeepFrees = true;
        pose->Update(time);
        std::vector<uint32_t> a;
        for (const auto &w : g_sharedWords)
            a.push_back(*w.first);
        PutPort(before);
        for (size_t k = 0; k < shared.size(); ++k)
            *g_sharedWords[k].first = shared[k];
        float &x = pose->m_points[RAGDOLL_POINT_PELVIS]->pos.x;
        if (mode != 0)
            x = std::nextafter(x, 1e9f);
        pose->Update(time);
        g_harnessKeepFrees = false;
        bool differs = false;
        for (size_t k = 0; k < a.size() && !differs; ++k) {
            float u, v;
            std::memcpy(&u, &a[k], 4);
            std::memcpy(&v, g_sharedWords[k].first, 4);
            if (g_sharedWords[k].tolerance > 0 && std::fabs(u - v) > tol * std::max(1.0f, std::fabs(u))) {
                differs = true;
                if (moved < 30)
                    std::printf("moved: %s %g %g\n", g_sharedWords[k].second.c_str(), u, v);
            }
        }
        ++steps;
        moved += differs;
        PutPort(before);
        for (size_t k = 0; k < shared.size(); ++k)
            *g_sharedWords[k].first = shared[k];
    }
    std::printf("sensitivity: the dead step moved past the tolerance in %d of %d\n", moved, steps);
}

// Values other than the ones BindSkeleton and Attach write (the bound rest state already holds those): the bones'
// children, chains and joints, the chains' pins, the dummies found on the model, the model's scale.
void ScrambleSkeleton(C_HumanWeaponPose *pose)
{
    const auto anyBone = [&]() { return P(0.3f) ? nullptr : &pose->m_bones[I(0, POSE_BONE_COUNT - 1)]; };
    const auto any4 = [] { return S_vector4{U(-2, 2), U(-2, 2), U(-2, 2), 0.0f}; };
    for (S_PoseBone &bone : pose->m_bones) {
        for (int i = 0; i < 3; ++i) {
            bone.childPos[i] = any4();
            bone.childLength[i] = U(0, 2);
            bone.childMatrix[i] = IkRandomMatrix(3.0f, false);
            bone.chainChildren[i] = anyBone();
        }
        bone.chainLength = U(0, 2);
        bone.pinMask = uint32_t(I(0, 0xFFFF));
        bone.chainParent = anyBone();
        for (S_PoseLimit &l : bone.limits) {
            l.max = any4();
            l.min = any4();
            l.unknown20 = any4();
            l.axes = uint32_t(I(0, 7));
            l.stiffness = U(0, 1);
        }
    }
    for (S_PosePin &pin : pose->m_chain.pins) {
        pin.bone = anyBone();
        pin.target = any4();
        pin.weight = U(0, 2);
    }
    pose->m_chain.root = anyBone();
    pose->m_chain.count = I(0, 6);
    pose->m_root = anyBone();
    pose->m_hw1 = pose->m_hw2 = pose->m_dummyZ = pose->m_leftHand = pose->m_rightHand = nullptr;
    g_ikModel->SetScale(S_vector(U(0.5f, 2), U(0.5f, 2), U(0.5f, 2)));
}

// BindSkeleton (a new skeleton after Attach's resets, or found again after a saved game), Attach and Reset without a
// model, SetStainsVisible.
void TestCoreBind()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("core bind\n");
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomDead(false);
        g_dead.ragdollDetail = P(0.5f);
        pose->m_stains = P(0.5f) ? Stains() : nullptr;
        // (The stains detail only where there are some already: a new one is each side's own allocation.)
        g_dead.stainsDetail = pose->m_stains && P(0.7f);
        switch (i % 4) {
        case 0: {
            // A new skeleton: the bones as Attach leaves them (mode 0); or bound again on the bound one (mode 2, the
            // movement's model change), its chain frames kept through their bone numbers.
            const int mode = P(0.5f) ? 0 : 2;
            if (mode == 0)
                IkResetBones(pose);
            ScrambleSkeleton(pose);
            if (mode == 2)
                for (S_PoseBone &bone : pose->m_bones)
                    bone.chainFrame = P(0.4f) ? g_ikBone[I(0, POSE_BONE_COUNT - 1)] : nullptr;
            pose->m_camera3rd = P(0.5f) ? g_camera3rd : nullptr;
            pose->m_cameraFpv = P(0.5f) ? g_cameraFpv : nullptr;
            DiffCall(mode == 0 ? "BindSkeleton" : "BindSkeleton 2", [&] { pose->BindSkeleton(mode); return 0; },
                     [&](void *) { OriginalAt<T_CInt>(0x0079c780)(p, mode); return 0; });
            break;
        }
        case 1: {
            // After a saved game: the frames found again, the chain frames from their bone numbers, a dead body laid.
            for (S_PoseBone &bone : pose->m_bones) {
                bone.frame = nullptr;
                if (P(0.3f))
                    bone.chainFrame = reinterpret_cast<I3D_frame *>(static_cast<intptr_t>(I(1, POSE_BONE_COUNT)));
                else
                    bone.chainFrame = nullptr;
            }
            if (P(0.4f))
                pose->m_flags |= POSE_FLAG_DEAD;
            DiffCall("BindSkeleton reload", [&] { pose->BindSkeleton(1); return 0; },
                     [&](void *) { OriginalAt<T_CInt>(0x0079c780)(p, 1); return 0; });
            break;
        }
        case 2: {
            // Attach without a model: everything back to the start (the ragdoll's blocks let go: frees skipped).
            // (A reload lays a dead body down again on its model: a living one here.)
            const bool reload = P(0.3f);
            if (reload) {
                pose->m_flags &= ~POSE_FLAG_DEAD;
                pose->m_deathAnim = nullptr;
            }
            pose->m_slidePoints = nullptr;
            pose->m_restMatrices = nullptr;
            pose->m_pushes = nullptr;
            pose->m_holds = nullptr;
            pose->m_rootBody = nullptr;
            pose->m_rootPoint = nullptr;
            pose->m_weapon = nullptr;
            pose->m_stains = nullptr;
            // (Values other than the ones Attach writes; a weapon let go of: it holds a reference.)
            ScrambleSkeleton(pose);
            for (S_PoseBone &bone : pose->m_bones)
                bone.chainFrame = P(0.4f) ? g_ikBone[I(0, POSE_BONE_COUNT - 1)] : nullptr;
            pose->m_updateSeen = I(1, 1000);
            pose->m_stanceBeforeSeat = I(1, 3);
            pose->m_carrier = P(0.5f) ? g_h.human : nullptr;
            for (float &roll : pose->m_savedRolls)
                roll = U(-1, 1);
            pose->m_owner = nullptr;
            pose->m_movement = nullptr;
            if (P(0.5f)) {
                pose->m_weapon = g_weapon[I(0, 3)];
                pose->m_weapon->AddRef();
            }
            // (The joints' limits are copied from a temp: their w words undefined.)
            const std::set<size_t> skips = g_poseSkipWords;
            for (int b = 0; b < POSE_BONE_COUNT; ++b)
                for (int l = 0; l < 6; ++l)
                    for (size_t v : {offsetof(S_PoseLimit, max), offsetof(S_PoseLimit, min), offsetof(S_PoseLimit, unknown20)})
                        g_poseSkipWords.insert(offsetof(C_HumanWeaponPose, m_bones) + b * sizeof(S_PoseBone) +
                                               offsetof(S_PoseBone, limits) + l * sizeof(S_PoseLimit) + v + 12);
            g_harnessKeepFrees = true;
            if (!reload && P(0.3f)) {
                // Reset: Attach again on its own model and owner (none and the human here).
                pose->m_model = nullptr;
                DiffCall("Reset", [&] { pose->Reset(); return 0; },
                         [&](void *) { OriginalAt<T_CVoid>(0x0079d530)(p); return 0; });
            } else {
                DiffCall("Attach", [&] { pose->Attach(nullptr, g_h.human, reload); return 0; },
                         [&](void *) { OriginalAt<T_CAttach>(0x0079c190)(p, nullptr, o_human, reload); return 0; });
            }
            g_harnessKeepFrees = false;
            g_poseSkipWords = skips;
            break;
        }
        default: {
            const bool on = P(0.5f);
            DiffCall("SetStainsVisible", [&] { pose->SetStainsVisible(on); return 0; },
                     [&](void *) { OriginalAt<T_CInt>(0x007b8a00)(p, on); return 0; });
            break;
        }
        }
    }
}

void TestCore()
{
    SetUpCore();
    if (std::getenv("CORE_SENS")) {
        CoreSensitivity();
        return;
    }
    TestCoreBind();
    TestCoreLiving();
    TestCoreSeats();
    TestCoreDead();
    // The rates (the subcalls judged by theirs in parts 16..18: the holds, the seats' chains, the head's look, the
    // dying animation; the ragdoll's step at 1 %).
    const std::pair<const char *, double> rates[] = {{"Update", 0.999},           {"Refresh", 0.999},
                                                     {"Update seat", 0.95},      {"Refresh seat", 0.95},
                                                     {"Update dead anim", 0.999}, {"Update dead step", 0.96},
                                                     {"Update dead blend", 0.999}, {"Update dead rest", 0.999}};
    for (const auto &r : rates) {
        const S_Stats &st = g_stats[r.first];
        std::printf("%s: %ld of %ld agree\n", r.first, st.agree, st.calls);
        CHECK(st.calls > 0 && st.agree >= st.calls * r.second);
    }
}

}  // namespace
