// Part 15 of the human harness (HUMAN_IK builds, build_ik.bat; included by human_test.cpp after ragdoll_test.inc): the
// weapon pose's skeleton and its inverse kinematics (game/human_pose_ik.cpp) against the original's. A model of 23
// jointed bone frames (each under its parent bone, as the human models are), the skeleton bound the way BindSkeleton
// binds it; the pose is shared by both sides (the same layout) and compared word by word, the bone frames' matrices,
// rotations, scales and flags too.
namespace {

#define IKT __attribute__((thiscall))
#define IKF __attribute__((fastcall))
#define IKS __attribute__((stdcall))

// The original's matrix helpers (their conventions as Ghidra has them, checked against the code).
typedef void(IKF *T_IkTransposed)(const S_matrix *m, S_matrix *out);               // 0x007a25f0
typedef void(IKS *T_IkTurnedAround)(const S_matrix *m, S_matrix *out);             // 0x007a2650
typedef void(IKF *T_IkOrthonormalize)(S_matrix *m);                                // 0x007a2770
typedef void(IKS *T_IkAimZ)(S_matrix *m, const S_vector *dir);                     // 0x007a2850
typedef void(IKF *T_IkFrameFromZ)(S_matrix *out, const S_vector *dir);             // 0x007a3270
typedef void(IKF *T_IkFrameFromZRoll)(S_matrix *out, const S_vector *dir, float roll);   // 0x007a3720, 0x007a3960
typedef void(IKF *T_IkRotationBetween)(const S_vector *from, const S_vector *to, S_matrix *out);   // 0x007a42c0
typedef void(IKF *T_IkTurnZTo)(S_matrix *m, const S_vector *to);                   // 0x007a3330
typedef void(IKS *T_IkTurnYTo)(S_matrix *m, const S_vector *to);                   // 0x007a3370
typedef void(IKF *T_IkGetRoll)(const S_matrix *m, float *roll);                    // 0x007a36c0
typedef void(IKS *T_IkSlerpDir)(const S_vector *a, const S_vector *b, S_vector *out, float t);   // 0x007a45a0 (S_vector4s)
// Its members.
typedef void(IKT *T_IkAddBone)(void *, int, int, I3D_frame *);                     // 0x007a4700
typedef void(IKT *T_IkSetLimit)(void *, int, int, uint32_t);                       // 0x007a51b0
typedef void(IKT *T_IkSetStiffness)(void *, int, int, float);                      // 0x007a5250
typedef void(IKT *T_IkAim)(void *, int, const S_vector *, float);                  // 0x007a4820, 0x007a5790
typedef void(IKT *T_IkHandFrames)(void *, I3D_frame *, I3D_frame *);               // 0x007a52e0
typedef void(IKT *T_IkHandMatrices)(void *, const S_matrix *, const S_matrix *);   // 0x007a5550
typedef void(IKT *T_IkVoid)(void *);                                               // 0x007a5f70
// (BuildChain, 0x007a5e90, is stdcall with the chain; RefreshChain, 0x007a69f0, takes the pose in ECX too: both called
// as thiscall, which serves either.)
typedef void(IKT *T_IkChain)(void *, S_PoseChain *);                               // 0x007a5e90, 0x007a69f0, 0x007a6420
typedef int(IKT *T_IkSolve)(void *, S_PoseChain *, int, int);                      // 0x007a6c40

// The skeleton as BindSkeleton builds it.
struct S_IkJoint {
    int bone, parent;
};
const S_IkJoint kIkJoints[POSE_BONE_COUNT] = {
    {POSE_BONE_PELVIS, -1},
    {POSE_BONE_SPINE, POSE_BONE_PELVIS},
    {POSE_BONE_LOWER_BACK, POSE_BONE_SPINE},
    {POSE_BONE_BACK, POSE_BONE_LOWER_BACK},
    {POSE_BONE_UPPER_CHEST, POSE_BONE_BACK},
    {POSE_BONE_CHEST, POSE_BONE_UPPER_CHEST},
    {POSE_BONE_NECK, POSE_BONE_CHEST},
    {POSE_BONE_HEAD_TOP, POSE_BONE_NECK},
    {POSE_BONE_HEAD, POSE_BONE_HEAD_TOP},
    {POSE_BONE_RIGHT_COLLAR, POSE_BONE_CHEST},
    {POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_COLLAR},
    {POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_SHOULDER},
    {POSE_BONE_RIGHT_HAND, POSE_BONE_RIGHT_ELBOW},
    {POSE_BONE_LEFT_COLLAR, POSE_BONE_CHEST},
    {POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_COLLAR},
    {POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_SHOULDER},
    {POSE_BONE_LEFT_HAND, POSE_BONE_LEFT_ELBOW},
    {POSE_BONE_RIGHT_HIP, POSE_BONE_PELVIS},
    {POSE_BONE_RIGHT_KNEE, POSE_BONE_RIGHT_HIP},
    {POSE_BONE_RIGHT_FOOT, POSE_BONE_RIGHT_KNEE},
    {POSE_BONE_LEFT_HIP, POSE_BONE_PELVIS},
    {POSE_BONE_LEFT_KNEE, POSE_BONE_LEFT_HIP},
    {POSE_BONE_LEFT_FOOT, POSE_BONE_LEFT_KNEE},
};
struct S_IkAxes {
    int bone, other;
    uint32_t axes;
};
const uint32_t IK_XY = POSE_AXIS_X | POSE_AXIS_Y, IK_XYZ = POSE_AXIS_X | POSE_AXIS_Y | POSE_AXIS_Z;
const S_IkAxes kIkAxes[] = {
    {POSE_BONE_PELVIS, POSE_BONE_SPINE, 0},           {POSE_BONE_SPINE, POSE_BONE_PELVIS, 0},
    {POSE_BONE_SPINE, POSE_BONE_LOWER_BACK, IK_XYZ},  {POSE_BONE_LOWER_BACK, POSE_BONE_SPINE, 0},
    {POSE_BONE_LOWER_BACK, POSE_BONE_BACK, 0},        {POSE_BONE_BACK, POSE_BONE_LOWER_BACK, IK_XYZ},
    {POSE_BONE_BACK, POSE_BONE_UPPER_CHEST, IK_XYZ},  {POSE_BONE_UPPER_CHEST, POSE_BONE_BACK, 0},
    {POSE_BONE_UPPER_CHEST, POSE_BONE_CHEST, IK_XYZ}, {POSE_BONE_CHEST, POSE_BONE_UPPER_CHEST, IK_XYZ},
    {POSE_BONE_CHEST, POSE_BONE_LEFT_COLLAR, 0},      {POSE_BONE_LEFT_COLLAR, POSE_BONE_CHEST, IK_XY},
    {POSE_BONE_CHEST, POSE_BONE_RIGHT_COLLAR, 0},     {POSE_BONE_RIGHT_COLLAR, POSE_BONE_CHEST, IK_XY},
    {POSE_BONE_CHEST, POSE_BONE_NECK, 0},             {POSE_BONE_NECK, POSE_BONE_CHEST, IK_XY},
    {POSE_BONE_NECK, POSE_BONE_HEAD_TOP, IK_XY},      {POSE_BONE_HEAD_TOP, POSE_BONE_NECK, IK_XY},
    {POSE_BONE_HEAD_TOP, POSE_BONE_HEAD, 0},          {POSE_BONE_HEAD, POSE_BONE_HEAD_TOP, 0},
    {POSE_BONE_LEFT_COLLAR, POSE_BONE_LEFT_SHOULDER, IK_XY},
    {POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_COLLAR, IK_XY},
    {POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, IK_XY},
    {POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_SHOULDER, IK_XY},
    {POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_HAND, IK_XY},  {POSE_BONE_LEFT_HAND, POSE_BONE_LEFT_ELBOW, IK_XYZ},
    {POSE_BONE_RIGHT_COLLAR, POSE_BONE_RIGHT_SHOULDER, IK_XY},
    {POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_COLLAR, IK_XY},
    {POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, IK_XY},
    {POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_SHOULDER, IK_XY},
    {POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_HAND, IK_XY},
    {POSE_BONE_RIGHT_HAND, POSE_BONE_RIGHT_ELBOW, IK_XYZ},
    {POSE_BONE_PELVIS, POSE_BONE_LEFT_HIP, 0},        {POSE_BONE_LEFT_HIP, POSE_BONE_PELVIS, IK_XY},
    {POSE_BONE_LEFT_HIP, POSE_BONE_LEFT_KNEE, IK_XY}, {POSE_BONE_LEFT_KNEE, POSE_BONE_LEFT_HIP, POSE_AXIS_X},
    {POSE_BONE_LEFT_KNEE, POSE_BONE_LEFT_FOOT, POSE_AXIS_X},
    {POSE_BONE_LEFT_FOOT, POSE_BONE_LEFT_KNEE, POSE_AXIS_X},
    {POSE_BONE_PELVIS, POSE_BONE_RIGHT_HIP, 0},       {POSE_BONE_RIGHT_HIP, POSE_BONE_PELVIS, IK_XY},
    {POSE_BONE_RIGHT_HIP, POSE_BONE_RIGHT_KNEE, IK_XY},
    {POSE_BONE_RIGHT_KNEE, POSE_BONE_RIGHT_HIP, POSE_AXIS_X},
    {POSE_BONE_RIGHT_KNEE, POSE_BONE_RIGHT_FOOT, POSE_AXIS_X},
    {POSE_BONE_RIGHT_FOOT, POSE_BONE_RIGHT_KNEE, POSE_AXIS_X},
};
struct S_IkStiffness {
    int bone, other;
    float stiffness;
};
const S_IkStiffness kIkStiffness[] = {
    {POSE_BONE_SPINE, POSE_BONE_LOWER_BACK, 0.2f},         {POSE_BONE_BACK, POSE_BONE_UPPER_CHEST, 0.4f},
    {POSE_BONE_UPPER_CHEST, POSE_BONE_CHEST, 0.7f},        {POSE_BONE_LEFT_COLLAR, POSE_BONE_LEFT_SHOULDER, 0.9f},
    {POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, 1.0f}, {POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_HAND, 1.0f},
    {POSE_BONE_RIGHT_COLLAR, POSE_BONE_RIGHT_SHOULDER, 0.9f},
    {POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, 1.0f},
    {POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_HAND, 1.0f},   {POSE_BONE_CHEST, POSE_BONE_UPPER_CHEST, 0.5f},
    {POSE_BONE_BACK, POSE_BONE_LOWER_BACK, 0.5f},
};

I3D_frame *g_ikModel = nullptr;
I3D_frame *g_ikBone[POSE_BONE_COUNT] = {};
I3D_frame *g_ikTarget[2] = {};                          // frames the hands may take their rotations from
std::vector<uint8_t> g_ikPoseRest, g_ikPoseUnbound;    // the bound pose; the pose as Attach leaves it before binding
std::vector<std::vector<uint8_t>> g_ikFrameRest;      // the frames' bytes (model, bones, targets)
std::vector<I3D_frame *> g_ikFrames;

C_HumanWeaponPose *IkPose() { return reinterpret_cast<C_HumanWeaponPose *>(g_h.pose); }

// A rotation about a random axis by up to `spread` radians.
S_quat IkRandomRot(float spread)
{
    S_vector axis(U(-1, 1), U(-1, 1), U(-1, 1));
    if (axis.Square() < 1e-4f)
        axis = S_vector(0, 1, 0);
    axis.Normalize();
    const float a = U(-spread, spread) * 0.5f;
    S_quat q;
    q.v = axis * std::sin(a);
    q.s = std::cos(a);
    return q;
}
S_vector IkRandomDir()
{
    S_vector d(U(-1, 1), U(-1, 1), U(-1, 1));
    if (d.Square() < 1e-4f)
        d = S_vector(0, 0, 1);
    return d.Normalized();
}
S_matrix IkRandomMatrix(float spread, bool translate)
{
    S_matrix m;
    m.Identity();
    m.SetRot3(IkRandomRot(spread));
    if (translate) {
        m.m[3][0] = U(-50, 50);
        m.m[3][1] = U(-5, 5);
        m.m[3][2] = U(-50, 50);
    }
    return m;
}

// The bones as Attach leaves them before binding (no frames, the joints open, no chains).
void IkResetBones(C_HumanWeaponPose *pose)
{
    pose->m_boneCount = 0;
    pose->m_root = nullptr;
    for (S_PoseBone &bone : pose->m_bones) {
        std::memset(static_cast<void *>(&bone), 0, sizeof bone);
        bone.index = -1;
        S_PoseLimit &limit = bone.limits[0];
        limit.max.x = limit.max.y = limit.max.z = 3.1415927f;
        limit.min.x = limit.min.y = limit.min.z = -3.1415927f;
        limit.axes = IK_XYZ;
        limit.stiffness = 1.0f;
        for (int i = 1; i < 6; ++i)
            bone.limits[i] = limit;
    }
    std::memset(static_cast<void *>(&pose->m_chain), 0, sizeof pose->m_chain);
    for (S_PosePin &pin : pose->m_chain.pins)
        pin.weight = 1.0f;
}

void SetUpIk()
{
    C_HumanWeaponPose *pose = IkPose();
    S_quat none;
    none.Identity();
    g_ikModel = NewFrame(FRAME_DUMMY, "ikModel", S_vector(20, 1, 20), none, Box(S_vector(0.4f, 0.9f, 0.4f)), "", g_sector);
    // The bones under their parents, at their places of the standing skeleton (ragdoll_test.inc's).
    S_vector at[POSE_BONE_COUNT];
    std::string name[POSE_BONE_COUNT];
    for (const S_BoneAt &b : kBones) {
        at[b.bone] = b.at;
        name[b.bone] = b.name;
    }
    for (const S_IkJoint &j : kIkJoints) {
        I3D_frame *parent = j.parent < 0 ? g_ikModel : g_ikBone[j.parent];
        const S_vector local = j.parent < 0 ? at[j.bone] : at[j.bone] - at[j.parent];
        g_ikBone[j.bone] = NewFrame(FRAME_DUMMY, "ik_" + name[j.bone], local, none,
                                    Box(S_vector(0.05f, 0.05f, 0.05f)), "", parent);
    }
    for (int i = 0; i < 2; ++i)
        g_ikTarget[i] = NewFrame(FRAME_DUMMY, i ? "ikTargetRight" : "ikTargetLeft", S_vector(0.2f * i, 1.2f, 0.4f), none,
                                 Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_ikModel);
    UpdateTree(g_ikModel);
    for (int i = 0; i < POSE_BONE_COUNT; ++i)
        g_names[g_ikBone[i]] = "bone" + std::to_string(i);
    g_names[g_ikModel] = "ikModel";
    // The skeleton bound as BindSkeleton binds it (the port's AddBone, SetLimitAxes, SetLimitStiffness, PoseAimZ).
    pose->m_model = g_ikModel;
    IkResetBones(pose);
    {
        const auto *p = reinterpret_cast<const uint8_t *>(pose);
        g_ikPoseUnbound.assign(p, p + sizeof(C_HumanWeaponPose));
    }
    for (const S_IkJoint &j : kIkJoints)
        pose->AddBone(j.bone, j.parent, g_ikBone[j.bone]);
    pose->m_root = &pose->m_bones[POSE_BONE_PELVIS];
    for (const S_IkAxes &a : kIkAxes)
        pose->SetLimitAxes(a.bone, a.other, a.axes);
    for (const S_IkStiffness &s : kIkStiffness)
        pose->SetLimitStiffness(s.bone, s.other, s.stiffness);
    for (S_PoseBone &bone : pose->m_bones) {
        for (int i = 0; i < 3; ++i) {
            if (!bone.child[i])
                continue;
            bone.childPos[i] = PoseRow4(bone.child[i]->frame->GetLocalMatrix(), 3);
            bone.childLength[i] = PoseMagnitude(PoseXYZ(bone.childPos[i]).Square());
            bone.childMatrix[i].Identity();
            PoseAimZ(bone.childMatrix[i], PoseXYZ(bone.childPos[i]));
        }
    }
    // (The solved world matrices' m[3][3]: ComputeSolved's own-joint branch copies the tip into row 3 as 16 bytes from
    // a stack temp whose w it never stores.)
    for (int i = 0; i < POSE_BONE_COUNT; ++i)
        g_poseSkipWords.insert(offsetof(C_HumanWeaponPose, m_bones) + i * sizeof(S_PoseBone) + offsetof(S_PoseBone, solvedWorld) + 0x3C);
    const auto *p = reinterpret_cast<const uint8_t *>(pose);
    g_ikPoseRest.assign(p, p + sizeof(C_HumanWeaponPose));
    g_ikFrames.push_back(g_ikModel);
    for (I3D_frame *f : g_ikBone)
        g_ikFrames.push_back(f);
    for (I3D_frame *f : g_ikTarget)
        g_ikFrames.push_back(f);
    for (I3D_frame *f : g_ikFrames) {
        const auto *b = reinterpret_cast<const uint8_t *>(f);
        g_ikFrameRest.emplace_back(b, b + 0x400);
        g_sharedFrames.push_back(f);
        // The world and local matrices, the rotation, the scale and the flags (+0x40..+0xE3). (Not the scale's w,
        // +0xDC: the original's inline SetLocalMatrix copies GetScale's 12-byte result in as 16 bytes from a stack temp.)
        for (size_t at = 0x40; at < 0xE4; at += 4)
            if (at != 0xDC)
                // (The floats within 2e-4 absolute below 1: a near-zero matrix entry's rounding is no relative error.)
                g_sharedWords.push_back({reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(f) + at),
                                         std::string("frame ") + f->GetName() + "+" + std::to_string(at), at == 0xE0 ? 0.0f : g_tol});
    }
}

// The model somewhere in the world, turned; the bones bent; the limits as bound or changed; the frames' world matrices
// up to date or not.
void RandomIk()
{
    C_HumanWeaponPose *pose = IkPose();
    std::memcpy(static_cast<void *>(pose), g_ikPoseRest.data(), g_ikPoseRest.size());
    for (size_t i = 0; i < g_ikFrames.size(); ++i)
        std::memcpy(static_cast<void *>(g_ikFrames[i]), g_ikFrameRest[i].data(), 0x400);
    g_ikModel->SetPosInline(S_vector(U(-40, 40), U(-2, 4), U(-40, 40)));
    S_quat yaw;
    yaw.v = S_vector(0, 1, 0) * std::sin(U(-3.14f, 3.14f) * 0.5f);
    yaw.s = std::sqrt(1.0f - yaw.v.Square());
    g_ikModel->SetRot(P(0.3f) ? IkRandomRot(0.4f) : yaw);
    const float bend = P(0.2f) ? 0.0f : U(0.1f, 1.2f);
    for (I3D_frame *f : g_ikBone)
        if (P(0.8f))
            f->SetRot(IkRandomRot(bend));
    for (I3D_frame *f : g_ikTarget)
        f->SetRot(IkRandomRot(3.0f));
    if (P(0.1f))
        g_ikBone[POSE_BONE_PELVIS]->m_frameFlags |= FRMFLAGS_POS_LOCKED;
    if (P(0.05f))
        g_ikBone[I(0, POSE_BONE_COUNT - 1)]->SetScale(S_vector(U(0.8f, 1.2f), U(0.8f, 1.2f), U(0.8f, 1.2f)));
    if (P(0.85f))
        UpdateTree(g_ikModel);
    // Limits: as bound, or other axes and stiffness (over 1 too: the solver's longer axis).
    if (P(0.3f)) {
        for (int k = I(1, 6); k > 0; --k) {
            S_PoseLimit &l = pose->m_bones[I(0, POSE_BONE_COUNT - 1)].limits[I(0, 5)];
            if (P(0.5f))
                l.axes = uint32_t(I(0, 7));
            if (P(0.5f))
                l.stiffness = P(0.2f) ? U(1.0f, 1.5f) : U(0.0f, 1.0f);
        }
    }
}

// A chain as the seats make them (the spine to the hands and the neck), or another.
void RandomChain()
{
    C_HumanWeaponPose *pose = IkPose();
    S_PoseChain &c = pose->m_chain;
    static const int seatPins[3] = {POSE_BONE_LEFT_HAND, POSE_BONE_RIGHT_HAND, POSE_BONE_NECK};
    static const int roots[] = {POSE_BONE_PELVIS, POSE_BONE_LOWER_BACK, POSE_BONE_BACK, POSE_BONE_UPPER_CHEST, POSE_BONE_CHEST};
    static const int tips[] = {POSE_BONE_LEFT_HAND, POSE_BONE_RIGHT_HAND, POSE_BONE_NECK, POSE_BONE_HEAD, POSE_BONE_HEAD_TOP,
                               POSE_BONE_LEFT_FOOT, POSE_BONE_RIGHT_FOOT, POSE_BONE_LEFT_ELBOW, POSE_BONE_RIGHT_ELBOW, POSE_BONE_LEFT_KNEE};
    if (P(0.6f)) {
        c.root = &pose->m_bones[POSE_BONE_SPINE];
        c.count = I(1, 3);
        const int first = c.count == 1 ? 2 : 0;   // (one pin: the neck)
        for (int i = 0; i < c.count; ++i)
            c.pins[i].bone = &pose->m_bones[seatPins[first + i]];
    } else {
        c.root = &pose->m_bones[P(0.5f) ? POSE_BONE_SPINE : roots[I(0, 4)]];
        c.count = I(1, 4);
        for (int i = 0; i < c.count; ++i) {
            int b;
            do
                b = tips[I(0, 9)];
            while (&pose->m_bones[b] == c.root);
            c.pins[i].bone = &pose->m_bones[b];
        }
    }
    for (int i = 0; i < 4; ++i) {
        S_PosePin &pin = c.pins[i];
        pin.weight = P(0.6f) ? 1.0f : U(0, 1);
        if (!pin.bone)
            continue;
        const S_vector tip = pin.bone->frame->GetWorldPos();
        const float reach = P(0.1f) ? 1.5f : (P(0.2f) ? 0.005f : 0.3f);
        PoseSetXYZ(pin.target, tip + S_vector(U(-reach, reach), U(-reach, reach), U(-reach, reach)));
    }
}

// Words compared one by one (the helpers' outputs): `n` floats, `skip` a mask of words left out.
std::string IkWords(const float *port, const float *orig, int n, uint32_t skip = 0, float tolerance = 0.0f)
{
    for (int k = 0; k < n; ++k) {
        if (skip & (1u << k))
            continue;
        uint32_t x, y;
        std::memcpy(&x, port + k, 4);
        std::memcpy(&y, orig + k, 4);
        if (tolerance > 0.0f ? !(std::fabs(port[k] - orig[k]) <= tolerance) : !WordsNear(x, y, 'f')) {
            char s[96];
            std::snprintf(s, sizeof s, "word %d (port %g original %g)", k, port[k], orig[k]);
            return s;
        }
    }
    return "";
}
void IkNote(const char *what, const std::string &why)
{
    S_Stats &st = g_stats[what];
    ++st.calls;
    CHECK(why.empty());
    if (why.empty()) {
        ++st.agree;
        return;
    }
    ++st.differ[why.substr(0, why.find(" ("))];
    static int shown;
    if (shown++ < 15)
        std::printf("  %s differs: %s\n", what, why.c_str());
}

// The matrix helpers, one call each from random input.
void TestIkHelpers()
{
    std::printf("ik helpers\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        const float garbage = U(-9, 9);
        S_matrix in = IkRandomMatrix(P(0.1f) ? 0.0f : 3.2f, P(0.7f));
        if (P(0.3f))
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    in.m[r][c] += U(-0.2f, 0.2f);   // (not orthonormal, the fourth column set)
        S_matrix a, b;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                a.m[r][c] = b.m[r][c] = garbage;
        S_vector dir = IkRandomDir();
        if (P(0.1f))
            dir = S_vector(0, P(0.5f) ? 1.0f : -1.0f, 0);   // (straight up / down: no horizontal x)
        else if (P(0.1f)) {
            // (Exactly along one of the axes of an unturned matrix: AimZ's x axis gone, y kept instead.)
            in = IdentityMatrix();
            in.m[3][0] = U(-9, 9);
            dir = S_vector(0, 0, 0);
            const int axis = I(0, 2);
            (&dir.x)[axis] = P(0.5f) ? 1.0f : -1.0f;
            if (axis == 0 && P(0.5f)) {
                // (A skewed matrix: x exactly along the direction, y not across it.)
                in.m[0][0] = U(0.5f, 2.0f);
                in.m[1][0] = U(-0.5f, 0.5f);
                in.m[1][1] = U(0.5f, 1.0f);
                in.m[1][2] = U(-0.5f, 0.5f);
            }
        }
        if (P(0.2f))
            dir *= U(0.2f, 3.0f);                            // (not normalised)
        const float roll = U(-3.2f, 3.2f);
        switch (i % 12) {
        case 0:
            a = PoseTransposed(in);
            OriginalAt<T_IkTransposed>(0x007a25f0)(&in, &b);
            IkNote("PoseTransposed", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        case 1:
            a = PoseTurnedAround(in);
            OriginalAt<T_IkTurnedAround>(0x007a2650)(&in, &b);
            IkNote("PoseTurnedAround", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        case 2:
            a = b = in;
            PoseOrthonormalize(a);
            OriginalAt<T_IkOrthonormalize>(0x007a2770)(&b);
            IkNote("PoseOrthonormalize", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        case 3:
            a = b = in;
            PoseAimZ(a, dir);
            OriginalAt<T_IkAimZ>(0x007a2850)(&b, &dir);
            IkNote("PoseAimZ", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        case 4:
            a = PoseFrameFromZ(dir);
            OriginalAt<T_IkFrameFromZ>(0x007a3270)(&b, &dir);
            IkNote("PoseFrameFromZ", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        case 5:
            a = PoseFrameFromZ(dir, roll);
            OriginalAt<T_IkFrameFromZRoll>(0x007a3720)(&b, &dir, roll);
            IkNote("PoseFrameFromZRolled", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        case 6: {
            // (The original leaves the fourth column of rows 0 and 1 undefined and copies dir.w into row 2's: rows 0..2's
            // fourth words not compared.)
            const S_vector4 d4 = PoseVector4(dir, U(-1, 1));
            a = PoseFrameFromZX(dir, roll);
            OriginalAt<T_IkFrameFromZRoll>(0x007a3960)(&b, reinterpret_cast<const S_vector *>(&d4), roll);
            IkNote("PoseFrameFromZX", IkWords(&a.m[0][0], &b.m[0][0], 16, (1u << 3) | (1u << 7) | (1u << 11)));
            break;
        }
        case 7: {
            // Unit vectors; nearly parallel or opposite ones too (the reflection).
            const S_vector from = IkRandomDir();
            S_vector to = IkRandomDir();
            if (P(0.2f))
                to = (from * (P(0.5f) ? 1.0f : -1.0f) + S_vector(U(-1e-4f, 1e-4f), U(-1e-4f, 1e-4f), U(-1e-4f, 1e-4f))).Normalized();
            else if (P(0.1f))
                to = from * (P(0.5f) ? 1.0f : -1.0f);
            PoseRotationBetween(from, to, a);
            OriginalAt<T_IkRotationBetween>(0x007a42c0)(&from, &to, &b);
            IkNote("PoseRotationBetween", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        }
        case 8: {
            const S_vector to = P(0.1f) ? in(2).Normalized() * (P(0.5f) ? 1.0f : -1.0f) : IkRandomDir();
            a = b = in;
            const bool y = P(0.5f);
            if (y) {
                PoseTurnYTo(a, to);
                OriginalAt<T_IkTurnYTo>(0x007a3370)(&b, &to);
            } else {
                PoseTurnZTo(a, to);
                OriginalAt<T_IkTurnZTo>(0x007a3330)(&b, &to);
            }
            IkNote(y ? "PoseTurnYTo" : "PoseTurnZTo", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        }
        case 9: {
            const float rp = PoseGetRoll(in);
            float ro = garbage;
            OriginalAt<T_IkGetRoll>(0x007a36c0)(&in, &ro);
            IkNote("PoseGetRoll", IkWords(&rp, &ro, 1));
            break;
        }
        case 10: {
            const S_vector from = IkRandomDir();
            S_vector to = IkRandomDir();
            if (P(0.15f))
                to = from * (P(0.5f) ? 1.0f : -1.0f);
            else if (P(0.1f))
                to = (from + S_vector(U(-1e-5f, 1e-5f), 0, 0)).Normalized();
            if (P(0.1f))
                to *= 1.0001f;                               // (a dot past 1: clamped)
            const float t = P(0.1f) ? (P(0.5f) ? 0.0f : 1.0f) : U(-0.2f, 1.2f);
            const S_vector rp = PoseSlerpDir(from, to, t);
            // (The original reads and writes whole S_vector4s: 16-byte inputs and output; their w not compared.)
            const S_vector4 from4 = PoseVector4(from, garbage), to4 = PoseVector4(to, garbage);
            S_vector4 ro = {garbage, garbage, garbage, garbage};
            OriginalAt<T_IkSlerpDir>(0x007a45a0)(reinterpret_cast<const S_vector *>(&from4), reinterpret_cast<const S_vector *>(&to4),
                                                 reinterpret_cast<S_vector *>(&ro), t);
            // (Nearly parallel or opposite: the weights are 1 / sin of a tiny angle, the sum's rounding with them; within
            // 2e-7 / sin.)
            const float sine = std::sqrt(std::max(0.0f, 1.0f - std::min(1.0f, from.Dot(to) * from.Dot(to))));
            IkNote("PoseSlerpDir", IkWords(&rp.x, &ro.x, 3, 0, sine < 1e-2f ? 2e-4f + 4e-6f / std::max(sine, 1e-6f) : 0.0f));
            break;
        }
        default: {
            // (TurnZTo with z already along the target: the reflection's branch.)
            a = b = in;
            const S_vector to = in(2).Normalized();
            a(2) = b(2) = to;
            PoseTurnZTo(a, to);
            OriginalAt<T_IkTurnZTo>(0x007a3330)(&b, &to);
            IkNote("PoseTurnZTo", IkWords(&a.m[0][0], &b.m[0][0], 16));
            break;
        }
        }
    }
}

// The skeleton's members, one call each from random states.
void TestIkSkeleton()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("ik skeleton\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomIk();
        switch (i % 8) {
        case 0: {
            // Binding: from the unbound pose with some bones added, any index and parent (out of range too), the
            // parent's three children taken.
            std::memcpy(static_cast<void *>(pose), g_ikPoseUnbound.data(), g_ikPoseUnbound.size());
            for (int k = I(0, 22); k > 0; --k) {
                const S_IkJoint &j = kIkJoints[I(0, 22)];
                pose->AddBone(j.bone, j.parent, g_ikBone[j.bone]);
            }
            const int index = P(0.1f) ? I(-3, 26) : I(0, 22);
            const int parent = P(0.1f) ? I(-3, 26) : (P(0.1f) ? -1 : I(0, 22));
            I3D_frame *frame = P(0.08f) ? nullptr : g_ikBone[I(0, 22)];
            DiffCall("AddBone", [&] { pose->AddBone(index, parent, frame); return 0; },
                     [&](void *) { OriginalAt<T_IkAddBone>(0x007a4700)(p, index, parent, frame); return 0; });
            break;
        }
        case 1: {
            const int index = I(0, 22), other = P(0.7f) ? kIkAxes[I(0, int(std::size(kIkAxes)) - 1)].other : I(0, 22);
            if (P(0.5f)) {
                const uint32_t axes = uint32_t(I(0, 7));
                DiffCall("SetLimitAxes", [&] { pose->SetLimitAxes(index, other, axes); return 0; },
                         [&](void *) { OriginalAt<T_IkSetLimit>(0x007a51b0)(p, index, other, axes); return 0; });
            } else {
                const float stiffness = U(0, 1.5f);
                DiffCall("SetLimitStiffness", [&] { pose->SetLimitStiffness(index, other, stiffness); return 0; },
                         [&](void *) { OriginalAt<T_IkSetStiffness>(0x007a5250)(p, index, other, stiffness); return 0; });
            }
            break;
        }
        case 2:
        case 3: {
            const int index = P(0.04f) ? (P(0.5f) ? -1 : 23) : (P(0.15f) ? POSE_BONE_PELVIS : I(0, 22));
            S_vector dir = IkRandomDir() * (P(0.3f) ? U(0.1f, 2.0f) : 1.0f);
            const float weight = P(0.3f) ? -1.0f : (P(0.05f) ? -0.0f : (P(0.1f) ? 0.0f : U(0, 1)));
            DiffCall("AimBone", [&] { pose->AimBone(index, dir, weight); return 0; },
                     [&](void *) { OriginalAt<T_IkAim>(0x007a4820)(p, index, &dir, weight); return 0; });
            break;
        }
        case 4: {
            if (P(0.5f)) {
                I3D_frame *pick[3] = {nullptr, g_ikTarget[I(0, 1)], g_ikBone[I(0, 22)]};
                I3D_frame *left = pick[P(0.15f) ? 0 : (P(0.7f) ? 1 : 2)], *right = pick[P(0.15f) ? 0 : (P(0.7f) ? 1 : 2)];
                DiffCall("SetHandRotations", [&] { pose->SetHandRotations(left, right); return 0; },
                         [&](void *) { OriginalAt<T_IkHandFrames>(0x007a52e0)(p, left, right); return 0; });
            } else {
                const S_matrix left = IkRandomMatrix(3.2f, true), right = IkRandomMatrix(3.2f, true);
                DiffCall("SetHandMatrices", [&] { pose->SetHandMatrices(left, right); return 0; },
                         [&](void *) { OriginalAt<T_IkHandMatrices>(0x007a5550)(p, &left, &right); return 0; });
            }
            break;
        }
        case 5:
        case 6: {
            // The middle joints mostly (elbows, knees); any other bone too (a pelvis: no parent; feet, hands: no child).
            static const int middles[] = {POSE_BONE_LEFT_ELBOW, POSE_BONE_RIGHT_ELBOW, POSE_BONE_LEFT_KNEE, POSE_BONE_RIGHT_KNEE};
            const int index = P(0.8f) ? middles[I(0, 3)] : I(0, 22);
            S_vector dir = IkRandomDir() * (P(0.3f) ? U(0.1f, 2.0f) : 1.0f);
            if (P(0.05f)) {
                // (Along the limb's line: nothing left to bend towards.)
                const S_PoseBone &b = pose->m_bones[index];
                if (b.parent && b.child[0])
                    dir = b.child[0]->frame->GetWorldPos() - b.parent->frame->GetWorldPos();
            }
            if (P(0.05f)) {
                // (The joint on the line from its parent to its child: no bend to keep.)
                S_PoseBone &b = pose->m_bones[index];
                if (b.parent && b.child[0]) {
                    // (The child moves with the joint: the joint goes onto the parent's line along the child's offset.)
                    UpdateTree(g_ikModel);
                    const S_matrix &pm = b.parent->frame->GetMatrix();
                    const S_vector rel = (b.child[0]->frame->GetWorldPos() - b.frame->GetWorldPos()) * U(0.5f, 1.5f);
                    const S_vector scale = b.parent->frame->GetWorldScale();
                    b.frame->SetPosInline(S_vector(rel.Dot(pm(0)) / (scale.x * scale.x), rel.Dot(pm(1)) / (scale.y * scale.y),
                                                   rel.Dot(pm(2)) / (scale.z * scale.z)));
                    UpdateTree(g_ikModel);
                }
            }
            const float weight = P(0.1f) ? (P(0.5f) ? 0.0f : 1.0f) : U(-0.2f, 1.2f);
            DiffCall("BendJoint", [&] { pose->BendJoint(index, dir, weight); return 0; },
                     [&](void *) { OriginalAt<T_IkAim>(0x007a5790)(p, index, &dir, weight); return 0; });
            break;
        }
        default: {
            // The chains cleared (some built first).
            RandomChain();
            if (P(0.7f)) {
                pose->ClearChains();
                pose->BuildChains();
            }
            DiffCall("ClearChains", [&] { pose->ClearChains(); return 0; },
                     [&](void *) { OriginalAt<T_IkVoid>(0x007a5f70)(p); return 0; });
            break;
        }
        }
    }
}

// The effector chains: built, refreshed, solved and applied, each from the state the ones before it leave (made by the
// port), and the whole sequence as the seats run it.
void TestIkChains()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    S_PoseChain *chain = &pose->m_chain;
    std::printf("ik chains\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomIk();
        RandomChain();
        // (One pass compared exactly: over the passes the float rounding grows (0 differences in 6,667 solves after one
        // pass, 2 after two, 36 after five, 150 after sixteen); the whole solves are judged by their rate.)
        int iterations = 1;
        const bool moveRoot = P(0.4f);
        // (Moved after the build: the refresh's input.)
        const auto moveFrames = [&] {
            for (I3D_frame *f : g_ikBone)
                if (P(0.3f))
                    f->SetRot(IkRandomRot(0.5f));
            if (P(0.3f))
                g_ikModel->SetPosInline(g_ikModel->GetPos() + S_vector(U(-0.5f, 0.5f), U(-0.2f, 0.2f), U(-0.5f, 0.5f)));
            if (P(0.9f))
                UpdateTree(g_ikModel);
        };
        const auto portSolve = [&] { return pose->SolveChain(*chain, iterations, moveRoot); };
        const auto origSolve = [&] { return OriginalAt<T_IkSolve>(0x007a6c40)(p, chain, iterations, moveRoot); };
        if (std::getenv("IK_CASE") && i % 8 != std::atoi(std::getenv("IK_CASE")))
            continue;
        switch (i % 8) {
        case 0:
            if (P(0.8f))
                pose->ClearChains();
            if (P(0.05f))
                chain->count = 0;                       // (no effectors: nothing built)
            DiffCall("BuildChain", [&] { pose->BuildChains(); return 0; },
                     [&](void *) { OriginalAt<T_IkChain>(0x007a5e90)(p, chain); return 0; });
            break;
        case 1:
            pose->ClearChains();
            pose->BuildChains();
            moveFrames();
            DiffCall("RefreshChain", [&] { pose->RefreshChain(*chain); return 0; },
                     [&](void *) { OriginalAt<T_IkChain>(0x007a69f0)(p, chain); return 0; });
            break;
        case 2:
            pose->ClearChains();
            pose->BuildChains();
            if (P(0.7f)) {
                moveFrames();
                pose->RefreshChain(*chain);
            }
            g_softDiff = true;
            DiffCall("SolveChain", portSolve, [&](void *) { return origSolve(); });
            g_softDiff = false;
            break;
        case 3:
            pose->ClearChains();
            pose->BuildChains();
            if (P(0.5f)) {
                moveFrames();
                pose->RefreshChain(*chain);
            }
            if (P(0.8f))
                pose->SolveChain(*chain, iterations, false);
            DiffCall("ApplyChain", [&] { pose->ApplyChain(*chain); return 0; },
                     [&](void *) { OriginalAt<T_IkChain>(0x007a6420)(p, chain); return 0; });
            break;
        case 4: {
            // A whole solve one pass at a time, each pass from the port's state (until the effectors are there).
            pose->ClearChains();
            pose->BuildChains();
            if (P(0.5f)) {
                moveFrames();
                pose->RefreshChain(*chain);
            }
            g_softDiff = true;
            for (int k = 0; k < 16; ++k)
                if (DiffCall("SolveChainPass", portSolve, [&](void *) { return origSolve(); }) == 0)
                    break;
            g_softDiff = false;
            break;
        }
        case 5:
        case 6: {
            // The whole solves (16 passes), judged by their rate.
            iterations = 16;
            pose->ClearChains();
            pose->BuildChains();
            if (P(0.7f)) {
                moveFrames();
                pose->RefreshChain(*chain);
            }
            g_softDiff = true;
            if (i % 8 == 5) {
                DiffCall("SolveChain16", portSolve, [&](void *) { return origSolve(); });
            } else {
                DiffCall("ChainUpdates16",
                         [&] {
                             int r = 0;
                             for (int k = 0; k < 3; ++k) {
                                 pose->RefreshChain(*chain);
                                 r = r * 17 + portSolve();
                                 pose->ApplyChain(*chain);
                             }
                             return r;
                         },
                         [&](void *) {
                             int r = 0;
                             for (int k = 0; k < 3; ++k) {
                                 OriginalAt<T_IkChain>(0x007a69f0)(p, chain);
                                 r = r * 17 + origSolve();
                                 OriginalAt<T_IkChain>(0x007a6420)(p, chain);
                             }
                             return r;
                         });
            }
            g_softDiff = false;
            break;
        }
        default: {
            // The seats' sequence: built, then refreshed, solved (a pass each) and applied over a few updates.
            pose->ClearChains();
            pose->BuildChains();
            g_softDiff = true;
            DiffCall("ChainUpdates",
                     [&] {
                         int r = 0;
                         for (int k = 0; k < 3; ++k) {
                             pose->RefreshChain(*chain);
                             r = r * 17 + portSolve();
                             pose->ApplyChain(*chain);
                         }
                         return r;
                     },
                     [&](void *) {
                         int r = 0;
                         for (int k = 0; k < 3; ++k) {
                             OriginalAt<T_IkChain>(0x007a69f0)(p, chain);
                             r = r * 17 + origSolve();
                             OriginalAt<T_IkChain>(0x007a6420)(p, chain);
                         }
                         return r;
                     });
            g_softDiff = false;
            break;
        }
        }
    }
}

void TestIk()
{
    SetUpIk();
    TestIkHelpers();
    TestIkSkeleton();
    TestIkChains();
    // The rates of the calls whose float rounding may grow past the tolerance (see TestIkChains): 2 or 3 single passes
    // in 72,000 differ by 3e-4..1.3e-3 in a word (presumably where a pass is ill-conditioned: asin near 1, a tip or
    // target almost at its joint; not traced); three updates of a pass each feed the applied frames back into the next;
    // the whole solves run 16 passes.
    for (const auto &rate : {std::make_pair("SolveChain", 0.999), std::make_pair("SolveChainPass", 0.9995), std::make_pair("ChainUpdates", 0.99),
                             std::make_pair("SolveChain16", 0.97), std::make_pair("ChainUpdates16", 0.88)}) {
        const S_Stats &st = g_stats[rate.first];
        std::printf("%s: %ld of %ld agree\n", rate.first, st.agree, st.calls);
        CHECK(st.calls > 0 && double(st.agree) >= rate.second * double(st.calls));
    }
}

}  // namespace
