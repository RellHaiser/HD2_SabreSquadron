// Part 16 of the human harness (HUMAN_WEAPON builds, build_weapon.bat; included by human_test.cpp after ik_test.inc):
// the weapon in the hands and its holds (game/human_pose_weapon.cpp, game/human_pose_hold.cpp) against the original's,
// on part 15's jointed skeleton: the hand dummies hw1 / hw2 under the hands, a third person camera dummy, weapon
// models with their grips "LW" / "RW" and stocks "BUTT", an animation set for the arms' activities. The pose is shared
// (its owner each side's own human), the frames' matrices, flags, links and reference counts compared.

#ifndef HUMAN_CORE
// The joint names (game/human_weapon_pose.cpp's, not linked in this build).
const char g_poseBoneNames[POSE_BONE_COUNT][12] = {"u", "q", "t", "p", "e", "f1", "f", "d1", "d", "b", "s", "o",
                                                   "c1", "c", "g", "k", "h", "l", "i", "m", "j", "n", "a"};
#endif
float PoseActivityWeight(const S_PoseBone *bone);       // (game/human_pose_weapon.cpp)

namespace {

#define WT __attribute__((thiscall))
typedef bool(WT *T_WSolveTwoBone)(void *, const S_vector *, float, const S_vector *, float, S_vector4 *, const S_vector *);
typedef void(WT *T_WTurnBone)(void *, int, const S_vector *, const S_vector4 *, const S_vector *);
typedef void(WT *T_WTurnAbout)(void *, int, const S_vector *, const S_vector4 *, const S_matrix *, const S_vector *);
typedef void(WT *T_WAim)(void *, const S_vector4 *, bool, bool, float);
typedef void(WT *T_WReach)(void *, int, int, const S_vector *, const S_vector4 *, I3D_frame *, float, const S_vector *);
typedef void(WT *T_WBool)(void *, bool);
typedef void(WT *T_WModel)(void *, I3D_frame *, int);
typedef void(WT *T_WInt)(void *, int);
typedef void(WT *T_WIntBool)(void *, int, bool);
typedef void(WT *T_WVec4)(void *, const S_vector4 *);
typedef void(WT *T_WVoid)(void *);
typedef void(WT *T_WPtr)(void *, void *);
typedef void(WT *T_WApply)(void *, void *, int *, int *, int, S_PoseBone *, float, void *);

// ---- the arms' animation sets (I3D_animation_set's slots: Release 0x00, GetEndTime 0x08, FindTrack 0x14, NumPosKeys
// 0x18, GetPosKey 0x1C, NumRotKeys 0x20, GetRotKey 0x24), the keys logged.
struct S_PoseAnimKey {
    int time;
    float v[4];
};
struct S_PoseAnim {
    void **vt;
    int refCount;
    int playMode;
    const char *fileName;
    int endTime;
    int track[POSE_BONE_COUNT];                         // the bone's track (-1: none), by E_PoseBone
    std::vector<S_PoseAnimKey> pos[POSE_BONE_COUNT], rot[POSE_BONE_COUNT];
};
constexpr int POSE_ANIMS = 3;
S_PoseAnim g_poseAnims[POSE_ANIMS];
void *g_poseAnimVt[16];
S_PoseAnim *PoseAnimOf(void *self) { return static_cast<S_PoseAnim *>(self); }
int BoneOfTrack(const S_PoseAnim *a, int track)
{
    for (int b = 0; b < POSE_BONE_COUNT; ++b)
        if (a->track[b] == track)
            return b;
    return -1;
}
uint32_t __stdcall PA_Release(void *self) { return static_cast<uint32_t>(--PoseAnimOf(self)->refCount); }
int __stdcall PA_GetEndTime(void *self) { return PoseAnimOf(self)->endTime; }
int __stdcall PA_FindTrack(void *self, const char *name)
{
    S_PoseAnim *a = PoseAnimOf(self);
    for (int b = 0; b < POSE_BONE_COUNT; ++b)
        if (std::strcmp(name, g_poseBoneNames[b]) == 0) {
            Log(self, (std::string("FindTrack ") + name).c_str());
            return a->track[b];
        }
    Log(self, (std::string("FindTrack ? ") + name).c_str());
    return -1;
}
int __stdcall PA_NumPosKeys(void *self, int track)
{
    const int b = BoneOfTrack(PoseAnimOf(self), track);
    return b < 0 ? 0 : int(PoseAnimOf(self)->pos[b].size());
}
int __stdcall PA_NumRotKeys(void *self, int track)
{
    const int b = BoneOfTrack(PoseAnimOf(self), track);
    return b < 0 ? 0 : int(PoseAnimOf(self)->rot[b].size());
}
void __stdcall PA_GetPosKey(void *self, int track, int key, int *time, S_vector *pos)
{
    Log(self, "GetPosKey", {double(track), double(key)});
    const int b = BoneOfTrack(PoseAnimOf(self), track);
    const S_PoseAnimKey &k = PoseAnimOf(self)->pos[b].at(size_t(key));
    *time = k.time;
    *pos = S_vector(k.v[0], k.v[1], k.v[2]);
}
void __stdcall PA_GetRotKey(void *self, int track, int key, int *time, S_quat *rot)
{
    Log(self, "GetRotKey", {double(track), double(key)});
    const int b = BoneOfTrack(PoseAnimOf(self), track);
    const S_PoseAnimKey &k = PoseAnimOf(self)->rot[b].at(size_t(key));
    *time = k.time;
    rot->s = k.v[0];
    rot->v = S_vector(k.v[1], k.v[2], k.v[3]);
}
void __stdcall PA_Unexpected(void *)
{
    std::printf("unexpected pose animation set call\n");
    std::exit(3);
}
I3D_animation_set *PoseAnim(int i) { return reinterpret_cast<I3D_animation_set *>(&g_poseAnims[i]); }

void MakePoseAnims()
{
    for (void *&slot : g_poseAnimVt)
        slot = reinterpret_cast<void *>(PA_Unexpected);
    g_poseAnimVt[0x00 / 4] = reinterpret_cast<void *>(PA_Release);
    g_poseAnimVt[0x08 / 4] = reinterpret_cast<void *>(PA_GetEndTime);
    g_poseAnimVt[0x14 / 4] = reinterpret_cast<void *>(PA_FindTrack);
    g_poseAnimVt[0x18 / 4] = reinterpret_cast<void *>(PA_NumPosKeys);
    g_poseAnimVt[0x1C / 4] = reinterpret_cast<void *>(PA_GetPosKey);
    g_poseAnimVt[0x20 / 4] = reinterpret_cast<void *>(PA_NumRotKeys);
    g_poseAnimVt[0x24 / 4] = reinterpret_cast<void *>(PA_GetRotKey);
    static const char *const names[POSE_ANIMS] = {"anims\\reload.i3d", "anims\\wave.i3d", "anims\\look.i3d"};
    static const int ends[POSE_ANIMS] = {1500, 800, 2000};
    for (int i = 0; i < POSE_ANIMS; ++i) {
        S_PoseAnim &a = g_poseAnims[i];
        a.vt = g_poseAnimVt;
        a.refCount = 1;
        a.fileName = names[i];
        a.endTime = ends[i];
        int next = 0;
        for (int b = 0; b < POSE_BONE_COUNT; ++b) {
            // (Some bones without a track; the tracks in another order than the bones.)
            a.track[b] = (b + i) % 5 == 0 ? -1 : (b * 7 + i) % 31;
            if (a.track[b] < 0)
                continue;
            ++next;
            // Keys at increasing times (some at the same time, some bones with one key or none).
            const int posKeys = (b + i) % 4 == 0 ? 0 : I(1, 6), rotKeys = (b + 2 * i) % 6 == 0 ? 1 : I(0, 7);
            int t = I(0, 100);
            for (int k = 0; k < posKeys; ++k, t += P(0.15f) ? 0 : I(1, 500))
                a.pos[b].push_back({t, {U(-0.3f, 0.3f), U(-0.3f, 0.3f), U(-0.3f, 0.3f), 0.0f}});
            t = I(0, 100);
            for (int k = 0; k < rotKeys; ++k, t += P(0.15f) ? 0 : I(1, 500)) {
                const S_quat q = IkRandomRot(1.5f);
                a.rot[b].push_back({t, {q.s, q.v.x, q.v.y, q.v.z}});
            }
        }
        g_names[&a] = std::string("anim") + std::to_string(i);
    }
}

// ---- the weapons: their grips and stocks (or not)
struct S_WeaponModel {
    const char *name;
    bool leftGrip, rightGrip, stock;
};
const S_WeaponModel kWeapons[] = {
    {"rifle", true, true, true},     {"carbine", true, true, false}, {"pistol", true, true, false},
    {"rightOnly", false, true, false}, {"leftOnly", true, false, false}, {"noGrips", false, false, false},
};
constexpr int WEAPONS = int(sizeof kWeapons / sizeof kWeapons[0]);
I3D_frame *g_weapon[WEAPONS] = {};
I3D_frame *g_hw1 = nullptr, *g_hw2 = nullptr, *g_camera3rd = nullptr;
I3D_frame *WeaponChild(int w, const char *name) { return FindIn(g_weapon[w], name); }

// The frame's world dir (slot 0x1C; SetMode turns the weapon in the hand with it): the rotation looking along `dir`,
// rolled, in its parent's axes; logged.
void __stdcall F_SetWorldDir(I3D_frame *self, const S_vector &dir, float roll)
{
    Log(self, "SetWorldDir", {dir.x, dir.y, dir.z, roll});
    S_matrix m;
    m.Identity();
    m.SetDir(dir, roll);
    if (I3D_frame *parent = self->GetParent())
        m *= PoseInverseRotation(parent->GetMatrix());
    self->SetLocalMatrix(PoseWithLocalPos(m, self));
}

// The pose's owner: each side's own human (or none).
bool g_weaponNoOwner = false;
void WeaponSide(bool original)
{
    IkPose()->m_owner = g_weaponNoOwner ? nullptr : original ? reinterpret_cast<C_Human *>(o_human) : g_h.human;
    IkPose()->m_movement = original ? reinterpret_cast<C_HumanMovement *>(o_movement) : g_h.movement;
}

// (`weapon`: the matrices' m[3][3] left out, +0x7C and +0xBC: AimWeapon's local row 3 comes from a stack temp whose w the
// original never sets, 0x007c0d94.)
void ShareFrameWords(I3D_frame *f, bool links, bool weapon = false)
{
    for (size_t at = 0x40; at < 0xE8; at += 4)
        if (at != 0xDC && !(weapon && (at == 0x7C || at == 0xBC)))
            g_sharedWords.push_back({reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(f) + at),
                                     std::string("frame ") + f->GetName() + "+" + std::to_string(at), at >= 0xE0 ? 0.0f : g_tol});
    if (links)
        for (size_t at : {0x108u, 0x10Cu, 0x110u})
            g_sharedWords.push_back({reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(f) + at),
                                     std::string("frame ") + f->GetName() + "+" + std::to_string(at)});
}

void SetUpWeapon()
{
    SetUpIk();
    MakePoseAnims();
    C_HumanWeaponPose *pose = IkPose();
    S_quat none;
    none.Identity();
    const auto dummy = [&](const std::string &name, const S_vector &at, I3D_frame *parent) {
        return NewFrame(FRAME_DUMMY, name, at, none, Box(S_vector(0.03f, 0.03f, 0.03f)), "", parent);
    };
    g_hw1 = dummy("hw1", S_vector(-0.02f, -0.09f, 0.03f), g_ikBone[POSE_BONE_RIGHT_HAND]);
    g_hw2 = dummy("hw2", S_vector(0.02f, -0.09f, 0.03f), g_ikBone[POSE_BONE_LEFT_HAND]);
    g_camera3rd = dummy("camera3RD", S_vector(0.45f, 1.75f, -2.2f), g_ikModel);
    std::vector<I3D_frame *> added = {g_hw1, g_hw2, g_camera3rd};
    for (int w = 0; w < WEAPONS; ++w) {
        const S_WeaponModel &k = kWeapons[w];
        g_weapon[w] = NewFrame(FRAME_VISUAL, k.name, S_vector(0, 0, 0), none, Box(S_vector(0.05f, 0.1f, 0.5f)), "", nullptr);
        g_weapon[w]->m_refCount = 3;
        added.push_back(g_weapon[w]);
        if (k.leftGrip)
            added.push_back(dummy("LW", S_vector(0.0f, -0.03f, w == 2 ? 0.02f : 0.38f), g_weapon[w]));
        if (k.rightGrip)
            added.push_back(dummy("RW", S_vector(0.0f, -0.05f, w == 2 ? -0.02f : 0.04f), g_weapon[w]));
        if (k.stock)
            added.push_back(dummy("BUTT", S_vector(0.0f, -0.02f, -0.34f), g_weapon[w]));
        g_names[g_weapon[w]] = k.name;
    }
    g_names[g_hw1] = "hw1";
    g_names[g_hw2] = "hw2";
    g_names[g_camera3rd] = "camera3RD";
    for (I3D_frame *f : added) {
        g_ikFrames.push_back(f);
        g_sharedFrames.push_back(f);
        ShareFrameWords(f, true, std::find(std::begin(g_weapon), std::end(g_weapon), f) != std::end(g_weapon));
    }
    // The w of the pose's vectors the original stores from stack temps whose w it leaves undefined (or 1, by chance):
    // the hand targets (UpdateLeft / RightHandTarget inline), the weapon's direction and the aim (the holds, UpdateFree),
    // the hands' offsets (InitHands).
    for (size_t at : {offsetof(C_HumanWeaponPose, m_leftHandTarget), offsetof(C_HumanWeaponPose, m_rightHandTarget),
                      offsetof(C_HumanWeaponPose, m_weaponDir), offsetof(C_HumanWeaponPose, m_aimDir),
                      offsetof(C_HumanWeaponPose, m_leftHandOffset), offsetof(C_HumanWeaponPose, m_rightHandOffset)})
        g_poseSkipWords.insert(at + 12);
    // (The bones and the model: their links too now, the weapon goes in and out of the hands' dummies.)
    for (I3D_frame *f : g_ikBone)
        for (size_t at : {0x10Cu})
            g_sharedWords.push_back({reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(f) + at),
                                     std::string("frame ") + f->GetName() + "+" + std::to_string(at)});
    g_frameVt[0x1C / 4] = reinterpret_cast<void *>(F_SetWorldDir);
    // The pose bound to the model: its dummies, the prone fit's ground (HoldProne reads its normal) and the hands.
    pose->m_hw1 = g_hw1;
    pose->m_hw2 = g_hw2;
    pose->m_camera3rd = g_camera3rd;
    pose->m_dummyZ = nullptr;
    const auto vectors = [] {
        auto *v = static_cast<S_vector4 *>(::operator new(6 * sizeof(S_vector4), std::align_val_t(16)));
        for (int i = 0; i < 6; ++i)
            v[i] = {0.0f, 1.0f, 0.0f, 0.0f};
        return v;
    };
    pose->m_proneGround = vectors();
    pose->m_proneNormals = vectors();
    pose->m_proneFrom = vectors();
    pose->m_proneTo = vectors();
    pose->m_proneNow = vectors();
    pose->m_proneSpheres = vectors();
    UpdateTree(g_ikModel);
    WeaponSide(false);
    pose->InitHands(false);
    const auto *p = reinterpret_cast<const uint8_t *>(pose);
    g_ikPoseRest.assign(p, p + sizeof(C_HumanWeaponPose));
    g_ikFrameRest.clear();
    for (I3D_frame *f : g_ikFrames) {
        const auto *b = reinterpret_cast<const uint8_t *>(f);
        g_ikFrameRest.emplace_back(b, b + 0x400);
    }
    // The prone ground's normals as the pose's (+0x660C points at them): each side starts from the same, compared.
    ShareBlock(pose->m_proneNow, 6 * sizeof(S_vector4), "proneNow", true);   // (their w: the original copies some from temps)
    g_sideHook = WeaponSide;
    g_poseTinyFloats = true;
}

S_vector4 Dir4(const S_vector &d, float w = 0.0f) { return {d.x, d.y, d.z, w}; }

// A random state of the weapon pose: the skeleton bent at a place over the ground, a weapon in a hand dummy (or none),
// the flags, hands, blends, aims, the stance, the human's role and speed.
void RandomWeapon(bool needWeapon = false, bool needGrips = false)
{
    RandomIk();
    C_HumanWeaponPose *pose = IkPose();
    C_Human *h = g_h.human;
    // The place: over the ground (the prone hold's ray meets it), the skeleton bent.
    const float x = U(6, 58), z = U(6, 58);
    g_ikModel->SetPosInline(S_vector(x, GroundAt(x, z) + (P(0.6f) ? 0.0f : U(0.0f, 0.8f)), z));
    UpdateTree(g_ikModel);
    for (int i = 0; i < 6; ++i)
        pose->m_proneNow[i] = Dir4((S_vector(0, 1, 0) + S_vector(U(-0.3f, 0.3f), 0, U(-0.3f, 0.3f))).Normalized());
    // The roles.
    const int roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_LOCAL, GAME_ROLE_CLIENT, GAME_ROLE_SERVER};
    g_gameRole = roles[I(0, 3)];
    h->m_role = P(0.6f) ? HUMAN_ROLE_PLAYER : HUMAN_ROLE_ENEMY;
    g_script = S_Script();
    g_script.localPlayer = h->m_role == HUMAN_ROLE_PLAYER && P(0.6f);
    const float speed = P(0.35f) ? 0.0f : (P(0.1f) ? U(0.0f, 1e-4f) : (P(0.2f) ? U(4.0f, 6.0f) : U(0.0f, 4.0f)));
    const S_vector v = IkRandomDir() * speed;
    h->m_velocity = {v.x, v.y, v.z, 0.0f};
    // The weapon.
    int w = -1;
    if (needWeapon || P(0.85f))
        w = needGrips ? I(0, 3) : (P(0.85f) ? I(0, 3) : I(0, WEAPONS - 1));
    pose->m_weapon = w < 0 ? nullptr : g_weapon[w];
    pose->m_leftGrip = w < 0 ? nullptr : WeaponChild(w, "LW");
    pose->m_rightGrip = w < 0 ? nullptr : WeaponChild(w, "RW");
    pose->m_stock = w < 0 ? nullptr : WeaponChild(w, "BUTT");
    pose->m_behaviour = w == 2 ? 2 : (P(0.3f) ? 3 : I(0, 12));
    static const int kinds[] = {0, 3, 4, 5, 6, 7};
    pose->m_weaponKind = w == 2 ? (P(0.9f) ? WEAPON_KIND_PISTOL : 0) : kinds[I(0, 5)];
    if (pose->m_weaponKind == WEAPON_KIND_PISTOL && w != 2 && P(0.7f))
        pose->m_weaponKind = 3;
    // (The low byte 0 or 1 as SetMode leaves it; 2 and 3 only for the mode tests: HoldProne's direction starts undefined
    // in the original for them.)
    pose->m_hand = uint32_t(P(0.75f) ? 1 : (needGrips || P(0.7f) ? 0 : I(2, 3)));
    if (pose->m_leftGrip && P(0.6f))
        pose->m_hand |= 0x80000000u;
    if (w >= 0) {
        I3D_frame *dummy = (pose->m_hand & 0xFF) == 0 ? g_hw2 : g_hw1;
        if (P(0.1f))
            dummy = nullptr;
        LinkUnder(g_weapon[w], dummy);
        S_matrix m = IkRandomMatrix(P(0.5f) ? 0.4f : 3.2f, false);
        m.m[3][0] = U(-0.1f, 0.1f);
        m.m[3][1] = U(-0.1f, 0.1f);
        m.m[3][2] = U(-0.2f, 0.2f);
        g_weapon[w]->SetLocalMatrix(m);
        UpdateTree(g_weapon[w]);
    }
    // The flags, the blends and timers.
    static const uint32_t modes[] = {POSE_FLAG_HOLD | POSE_FLAG_GRIP, POSE_FLAG_GRIP, POSE_FLAG_FREE, POSE_FLAG_HOLD, 0,
                                     POSE_FLAG_STEER, POSE_FLAG_STATIC_WEAPON, POSE_FLAG_SEAT_WEAPON, POSE_FLAG_PASSENGER};
    uint32_t flags = modes[I(0, 8)];
    if (P(0.2f))
        flags |= POSE_FLAG_PASSENGER;
    if (P(0.2f))
        flags |= POSE_FLAG_ACTIVITY;
    if (P(0.2f))
        flags |= POSE_FLAG_RECOIL;
    static const uint32_t timers[] = {0, POSE_FLAG_SUPPORT_ON, POSE_FLAG_SUPPORT_OFF, POSE_FLAG_PAUSED, POSE_FLAG_NO_RAISE,
                                      POSE_FLAG_RAISE, POSE_FLAG_LOWER, POSE_FLAG_PAUSED | POSE_FLAG_NO_RAISE};
    flags |= timers[I(0, 7)];
    if (P(0.1f))
        flags |= uint32_t(I(0, 0xFFFF)) << 10;          // (bits nothing names)
    pose->m_flags = flags;
    pose->m_raised = P(0.5f);
    pose->m_stance = I(0, 3);
    const auto unit = [] { return P(0.15f) ? 0.0f : (P(0.15f) ? 1.0f : U(0, 1)); };
    pose->m_blend = unit();
    pose->m_support = unit();
    pose->m_blendLength = P(0.1f) ? 0 : I(1, 600);
    pose->m_blendTime = I(0, 700);
    pose->m_pauseTime = I(-100, 2000);
    pose->m_supportTime = I(0, 200);
    pose->m_visible = P(0.85f);
    pose->m_time = P(0.1f) ? 0 : I(1, 120);
    if (P(0.3f)) {
        pose->m_elbowOut = U(0, 1);
        pose->m_elbowIn = U(0, 1);
    }
    // The aims: mostly ahead of the model (in its axes), some anywhere.
    const S_matrix &model = g_ikModel->GetMatrix();
    const auto aim = [&] {
        if (P(0.2f))
            return IkRandomDir();
        return (model(2) * U(0.2f, 1.0f) + model(0) * U(-0.8f, 0.8f) + model(1) * U(-0.9f, 0.9f)).Normalized();
    };
    pose->m_aimDir = Dir4(aim());
    if (P(0.04f))
        pose->m_aimDir = Dir4(P(0.5f) ? model(1) * (P(0.5f) ? 1.0f : -1.0f) : -model(2));   // (straight up / down / back)
    g_weaponNoOwner = P(0.05f);
    pose->m_aimTarget = Dir4(aim());
    pose->m_aimFrom = P(0.2f) ? pose->m_aimTarget : Dir4(aim());
    const float c = std::max(-1.0f, std::min(1.0f, PoseXYZ(pose->m_aimFrom).Dot(PoseXYZ(pose->m_aimTarget))));
    pose->m_aimAngle = std::acos(c);
    pose->m_aimSin = std::sin(pose->m_aimAngle);
    if (pose->m_aimSin < 1e-3f) {
        // (Not a turn: the aim at its target.)
        pose->m_aimFrom = pose->m_aimTarget = pose->m_aimDir;
        pose->m_aimAngle = 0.0f;
        pose->m_aimSin = 0.0f;
        pose->m_aimLength = pose->m_aimTime = 0;
    } else {
        pose->m_aimLength = I(1, 300);
        pose->m_aimTime = P(0.3f) ? 0 : I(-50, pose->m_aimLength);
    }
    pose->m_weaponDir = Dir4(P(0.5f) ? PoseXYZ(pose->m_aimDir) : aim());
    pose->m_cameraOffset = Dir4(P(0.7f) ? S_vector(0.0f, 0.25f, 0.0f) : S_vector(U(-0.2f, 0.2f), U(0, 0.4f), U(-0.2f, 0.2f)));
    // The prone fit's flags, the gunner pose, the model's sampled speed (InitHands resets them).
    pose->m_proneFit = P(0.5f);
    pose->m_proneSettle = P(0.5f) ? 0 : I(1, 500);
    pose->m_hasGunnerPose = P(0.3f);
    pose->m_velocity = Dir4(IkRandomDir() * U(0, 5));
    pose->m_lastVelocity = Dir4(IkRandomDir() * U(0, 5));
    pose->m_sampledVelocity = Dir4(IkRandomDir() * U(0, 5));
    pose->m_velocityTime = I(0, 100);
    // The recoil and the activity.
    pose->m_recoilTime = I(0, 400);
    pose->m_activityAnim = P(0.3f) ? nullptr : PoseAnim(I(0, POSE_ANIMS - 1));
    pose->m_activityTime = I(0, 2300);
    for (int b = 0; b < POSE_BONE_COUNT; ++b) {
        pose->m_activityPosKey[b] = P(0.7f) ? 0 : I(0, 3);
        pose->m_activityRotKey[b] = P(0.7f) ? 0 : I(0, 3);
    }
    // The third person camera somewhere behind.
    g_camera3rd->SetPosInline(S_vector(U(-0.6f, 0.6f), U(1.2f, 2.2f), U(-3, -1.5f)));
    UpdateTree(g_ikModel);
}

// Words compared after the calls with an out-vector (the joint of SolveTwoBone).
std::string JointWords(const S_vector4 &a, const S_vector4 &b) { return IkWords(&a.x, &b.x, 3); }

void TestWeaponArms()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("weapon arms\n");
    for (int i = 0; i < 30000 / QUICK; ++i) {
        RandomWeapon();
        switch (i % 6) {
        case 0: {
            // A limb: reachable or not, the joint anywhere, a hint or none.
            const S_vector root(U(-2, 2), U(-2, 2), U(-2, 2));
            const float upper = U(0.1f, 0.5f), lower = U(0.1f, 0.5f);
            const float reach = P(0.2f) ? U(1.0f, 1.3f) : U(0.05f, 1.0f);
            const S_vector target = root + IkRandomDir() * ((upper + lower) * reach);
            const S_vector4 joint0 = Dir4(root + IkRandomDir() * upper, U(-1, 1));
            S_vector hint = IkRandomDir();
            const S_vector *h = P(0.4f) ? nullptr : &hint;
            S_vector4 jp = joint0, jo = joint0;
            DiffCall("SolveTwoBone", [&] { return int(pose->SolveTwoBone(root, upper, target, lower, jp, h)); },
                     [&](void *) { return int(OriginalAt<T_WSolveTwoBone>(0x007a3b70)(p, &root, upper, &target, lower, &jo, h)); });
            IkNote("SolveTwoBone joint", JointWords(jp, jo));
            break;
        }
        case 1: {
            const int index = I(0, 22);
            const S_vector from = IkRandomDir();
            const S_vector4 to = Dir4(P(0.1f) ? from : IkRandomDir());
            const S_vector weights(U(0, 1), U(0, 1), P(0.3f) ? 0.0f : U(0, 1));
            if (P(0.5f)) {
                DiffCall("TurnBone", [&] { pose->TurnBone(index, from, to, weights); return 0; },
                         [&](void *) { OriginalAt<T_WTurnBone>(0x007bfde0)(p, index, &from, &to, &weights); return 0; });
            } else {
                const S_matrix axes = IkRandomMatrix(3.2f, P(0.3f));
                DiffCall("TurnBoneAbout", [&] { pose->TurnBoneAbout(index, from, to, axes, weights); return 0; },
                         [&](void *) { OriginalAt<T_WTurnAbout>(0x007c02e0)(p, index, &from, &to, &axes, &weights); return 0; });
            }
            break;
        }
        case 2:
        case 3: {
            // The weapon aimed: the hands, the placing (a weapon with a grip in a hand dummy then).
            RandomWeapon(true, true);
            if (!pose->m_weapon->GetParent())
                LinkUnder(pose->m_weapon, (pose->m_hand & 0xFF) == 1 ? g_hw1 : g_hw2);
            const S_vector4 dir = Dir4(P(0.5f) ? PoseXYZ(pose->m_aimDir) : IkRandomDir());
            const bool hands = P(0.7f), place = P(0.7f);
            const float weight = P(0.4f) ? 1.0f : U(0, 1);
            DiffCall("AimWeapon", [&] { pose->AimWeapon(dir, hands, place, weight); return 0; },
                     [&](void *) { OriginalAt<T_WAim>(0x007c0750)(p, &dir, hands, place, weight); return 0; });
            break;
        }
        default: {
            const bool left = P(0.5f);
            const int shoulder = left ? POSE_BONE_LEFT_SHOULDER : POSE_BONE_RIGHT_SHOULDER;
            const int elbow = left ? POSE_BONE_LEFT_ELBOW : POSE_BONE_RIGHT_ELBOW;
            I3D_frame *hand = g_ikBone[left ? POSE_BONE_LEFT_HAND : POSE_BONE_RIGHT_HAND];
            const S_vector &h = hand->GetWorldPos();
            const S_vector target = h + IkRandomDir() * (P(0.2f) ? U(0.5f, 1.5f) : U(0.0f, 0.4f));
            const S_vector4 handTarget = Dir4(h + IkRandomDir() * U(0.0f, 0.1f));
            const float weight = P(0.4f) ? 1.0f : U(0, 1);
            const S_vector down(0.0f, -1.0f, 0.0f);
            const S_vector *hint = P(0.5f) ? nullptr : &down;
            DiffCall("ReachArm", [&] { pose->ReachArm(shoulder, elbow, target, handTarget, hand, weight, hint); return 0; },
                     [&](void *) {
                         OriginalAt<T_WReach>(0x007c0e20)(p, shoulder, elbow, &target, &handTarget, hand, weight, hint);
                         return 0;
                     });
            break;
        }
        }
    }
}

void TestWeaponModes()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("weapon modes\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomWeapon();
        switch (i % 10) {
        case 0: {
            const bool reload = P(0.3f);
            // (The hand dummies found again: lost first sometimes.)
            if (P(0.5f))
                pose->m_hw1 = pose->m_hw2 = nullptr;
            DiffCall("InitHands", [&] { pose->InitHands(reload); return 0; },
                     [&](void *) { OriginalAt<T_WBool>(0x007b8a20)(p, reload); return 0; });
            break;
        }
        case 1: {
            I3D_frame *model = P(0.15f) ? nullptr : (P(0.1f) ? pose->m_weapon : g_weapon[I(0, WEAPONS - 1)]);
            if (model && model != pose->m_weapon)
                LinkUnder(model, nullptr);
            const int behaviour = P(0.1f) ? -1 : I(0, 13);
            DiffCall("SetModel", [&] { pose->SetModel(model, behaviour); return 0; },
                     [&](void *) { OriginalAt<T_WModel>(0x007b9510)(p, model, behaviour); return 0; });
            break;
        }
        case 2: {
            const int mode = P(0.1f) ? I(-1, 4) : I(0, 2);
            DiffCall("SetMode", [&] { pose->SetMode(mode); return 0; },
                     [&](void *) { OriginalAt<T_WInt>(0x007b96c0)(p, mode); return 0; });
            break;
        }
        case 3: {
            const bool on = P(0.5f);
            DiffCall("SetSupportHand", [&] { pose->SetSupportHand(on); return 0; },
                     [&](void *) { OriginalAt<T_WBool>(0x007b9b20)(p, on); return 0; });
            break;
        }
        case 4: {
            const int time = I(0, 600);
            if (P(0.5f))
                DiffCall("RaiseWeapon", [&] { pose->RaiseWeapon(time); return 0; },
                         [&](void *) { OriginalAt<T_WInt>(0x007b9160)(p, time); return 0; });
            else
                DiffCall("LowerWeapon", [&] { pose->LowerWeapon(time); return 0; },
                         [&](void *) { OriginalAt<T_WInt>(0x007b91c0)(p, time); return 0; });
            break;
        }
        case 5: {
            const int time = I(0, 3000);
            if (P(0.5f)) {
                const bool free = P(0.5f);
                DiffCall("PauseWeapon", [&] { pose->PauseWeapon(time, free); return 0; },
                         [&](void *) { OriginalAt<T_WIntBool>(0x007b9270)(p, time, free); return 0; });
            } else {
                DiffCall("PauseHands", [&] { pose->PauseHands(time); return 0; },
                         [&](void *) { OriginalAt<T_WInt>(0x007b9220)(p, time); return 0; });
            }
            break;
        }
        case 6: {
            // (The original reads the view as 16 bytes, its w into the aim: 0 here, as the port's fix sets it.)
            S_vector4 view = Dir4(P(0.2f) ? PoseXYZ(pose->m_aimDir) : (P(0.1f) ? -PoseXYZ(pose->m_aimDir) : IkRandomDir()));
            if (P(0.1f))
                view = Dir4(PoseXYZ(view) * 1.00001f);
            const S_vector v = PoseXYZ(view);
            DiffCall("SetViewDir", [&] { pose->SetViewDir(v); return 0; },
                     [&](void *) { OriginalAt<T_WVec4>(0x007b92e0)(p, &view); return 0; });
            break;
        }
        case 7: {
            if (P(0.5f)) {
                void *anim = PoseAnim(0);
                DiffCall("StartRecoil", [&] { pose->StartRecoil(static_cast<I3D_animation_set *>(anim)); return 0; },
                         [&](void *) { OriginalAt<T_WPtr>(0x007ae7f0)(p, anim); return 0; });
            } else {
                I3D_animation_set *anim = P(0.2f) ? nullptr : PoseAnim(I(0, POSE_ANIMS - 1));
                DiffCall("PlayActivity", [&] { pose->PlayActivity(anim); return 0; },
                         [&](void *) { OriginalAt<T_WPtr>(0x007ae780)(p, anim); return 0; });
            }
            break;
        }
        case 8: {
            // An activity animation on a bone and the bones below it.
            void *anim = PoseAnim(I(0, POSE_ANIMS - 1));
            S_PoseBone *bone = &pose->m_bones[P(0.5f) ? POSE_BONE_PELVIS : I(0, 22)];
            const int time = I(-50, 2200);
            const float weight = P(0.2f) ? 1.0f : U(0, 1);
            const bool weighted = P(0.6f);
            DiffCall("ApplyAnimation",
                     [&] {
                         pose->ApplyAnimation(static_cast<I3D_animation_set *>(anim), pose->m_activityPosKey, pose->m_activityRotKey, time, bone,
                                              weight, weighted ? PoseActivityWeight : nullptr);
                         return 0;
                     },
                     [&](void *) {
                         OriginalAt<T_WApply>(0x007b0710)(p, anim, pose->m_activityPosKey, pose->m_activityRotKey, time, bone, weight,
                                                          weighted ? OriginalAt<void *>(0x007b0170) : nullptr);
                         return 0;
                     });
            break;
        }
        default: {
            if (!pose->m_activityAnim)
                pose->m_activityAnim = PoseAnim(I(0, POSE_ANIMS - 1));
            DiffCall("UpdateActivity", [&] { pose->UpdateActivity(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007b0080)(p); return 0; });
            break;
        }
        }
    }
}

// The updates of the weapon modes and the stances' holds (a weapon with grips in a hand dummy). The holds (and the
// updates that run them, UpdateHold and UpdateFree) chain a dozen turns and slerps, and some states are ill-conditioned
// there: moving the aim by 1e-7 alone moves the port's own result past the tolerance in about 0.2 % of them
// (WEAPON_SENS), the rate the sides differ at. Those are judged by their rate (99.5 %); every function they call agrees
// on its own.
void TestWeaponHolds()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("weapon holds\n");
    for (int i = 0; i < 48000 / QUICK; ++i) {
        RandomWeapon(true, true);
        if (!pose->m_weapon->GetParent())
            LinkUnder(pose->m_weapon, (pose->m_hand & 0xFF) == 0 ? g_hw2 : g_hw1);
        g_softDiff = i % 8 != 0 && i % 8 != 3;
        switch (i % 8) {
        case 0:
            DiffCall("UpdateGrip", [&] { pose->UpdateGrip(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007b9bb0)(p); return 0; });
            break;
        case 1:
            DiffCall("UpdateHold", [&] { pose->UpdateHold(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007ba110)(p); return 0; });
            break;
        case 2:
            DiffCall("UpdateFree", [&] { pose->UpdateFree(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007ba520)(p); return 0; });
            break;
        case 3:
            if (P(0.2f))
                pose->m_weapon = nullptr;
            DiffCall("UpdateRecoil", [&] { pose->UpdateRecoil(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007b01d0)(p); return 0; });
            break;
        case 4:
            DiffCall("HoldStanding", [&] { pose->HoldStanding(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007bd420)(p); return 0; });
            break;
        case 5:
            DiffCall("HoldCrouching", [&] { pose->HoldCrouching(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007bb9a0)(p); return 0; });
            break;
        case 6:
            DiffCall("HoldSeated", [&] { pose->HoldSeated(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007beb50)(p); return 0; });
            break;
        default:
            DiffCall("HoldProne", [&] { pose->HoldProne(); return 0; },
                     [&](void *) { OriginalAt<T_WVoid>(0x007bac90)(p); return 0; });
            break;
        }
        g_softDiff = false;
    }
    for (const char *name : {"UpdateHold", "UpdateFree", "HoldStanding", "HoldCrouching", "HoldSeated", "HoldProne"}) {
        const S_Stats &st = g_stats[name];
        std::printf("%s: %ld of %ld agree\n", name, st.agree, st.calls);
        CHECK(st.calls > 0 && double(st.agree) >= 0.995 * double(st.calls));
    }
}

// (WEAPON_SENS: how often the port's own hold moves past the tolerance when the aim moves by 1e-7: the conditioning.)
void WeaponSensitivity()
{
    C_HumanWeaponPose *pose = IkPose();
    std::map<std::string, int> moved;
    const int n = 6000;
    for (int i = 0; i < n; ++i) {
        RandomWeapon(true, true);
        if (!pose->m_weapon->GetParent())
            LinkUnder(pose->m_weapon, (pose->m_hand & 0xFF) == 0 ? g_hw2 : g_hw1);
        const S_PortState before = TakePort();
        std::vector<uint32_t> shared;
        for (const auto &w : g_sharedWords)
            shared.push_back(*w.first);
        const int which = i % 5;
        const auto call = [&] {
            WeaponSide(false);
            switch (which) {
            case 0: pose->HoldStanding(); break;
            case 1: pose->HoldCrouching(); break;
            case 2: pose->HoldSeated(); break;
            case 3: pose->HoldProne(); break;
            default: pose->UpdateFree(); break;
            }
        };
        call();
        std::vector<uint32_t> a;
        for (const auto &w : g_sharedWords)
            a.push_back(*w.first);
        const S_PortState first = TakePort();
        PutPort(before);
        for (size_t k = 0; k < shared.size(); ++k)
            *g_sharedWords[k].first = shared[k];
        pose->m_aimDir.x += 1e-7f;
        call();
        bool differs = false;
        for (size_t k = 0; k < a.size() && !differs; ++k) {
            float x, y;
            std::memcpy(&x, &a[k], 4);
            std::memcpy(&y, g_sharedWords[k].first, 4);
            if (g_sharedWords[k].tolerance > 0 && std::fabs(x - y) > g_tol * std::max(1.0f, std::fabs(x)))
                differs = true;
        }
        static const char *const names[] = {"HoldStanding", "HoldCrouching", "HoldSeated", "HoldProne", "UpdateFree"};
        if (differs)
            ++moved[names[which]];
        PutPort(before);
        for (size_t k = 0; k < shared.size(); ++k)
            *g_sharedWords[k].first = shared[k];
        (void)first;
    }
    for (const auto &m : moved)
        std::printf("sensitivity: %s moved past the tolerance in %d of %d\n", m.first.c_str(), m.second, n / 5);
}

void TestWeapon()
{
    SetUpWeapon();
    if (std::getenv("WEAPON_SENS")) {
        WeaponSensitivity();
        return;
    }
    TestWeaponArms();
    TestWeaponModes();
    TestWeaponHolds();
}

}  // namespace
