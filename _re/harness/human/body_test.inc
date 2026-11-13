// Part 17 of the human harness (HUMAN_BODY builds, build_body.bat; included by human_test.cpp after weapon_test.inc): the
// prone body fitted to the ground, the head's looks and glances (game/human_pose_body.cpp) and the pose in seats: the
// steering wheel, seat weapons, static weapons and their gunner poses (game/human_pose_seats.cpp) against the original's,
// on part 16's skeleton, hands and weapons. The vehicles are fakes of the original's layout (type +0x18, frame +0x20, a
// car's velocity +0x4A0) with each side's vtable; the human's model follows the skeleton's (its lying dummies give the
// prone stance's spheres).
namespace {

#define BT __attribute__((thiscall))
typedef void(BT *T_BVoid)(void *);
typedef void(BT *T_BLook)(void *, uint32_t, const S_vector4 *);
typedef void(BT *T_BInt)(void *, int);
typedef void(BT *T_BTurnHead)(void *, const S_vector *, float);
typedef void(BT *T_BSeat)(void *, uint32_t, void *, int);
typedef void(BT *T_BPtr)(void *, void *);

// ---- the vehicles: a car with a steering wheel (its grips "LWC" / "RWC") and seat weapon grips ("LWS" / "RWS"), a car
// without a wheel's grips, a static weapon with grips and one without.
enum { VEH_CAR, VEH_CAR_BARE, VEH_GUN, VEH_GUN_BARE, VEHICLES };
struct S_SeatScript {
    int seatCount = 4;
    bool hasSeatWeapon = true;
    S_vector aim = S_vector(0, 0, 1);
    float up = 0.5f, down = 0.4f, left = 1.0f, right = 1.0f;
} g_seat;
I3D_frame *g_vehFrame[VEHICLES] = {}, *g_wheel = nullptr;
uint8_t *g_vehBytes[VEHICLES] = {}, *g_vehOrig[VEHICLES] = {};
void *p_vehVt[128], *o_vehVt[96];
C_Actor *Vehicle(int v) { return reinterpret_cast<C_Actor *>(g_vehBytes[v]); }
C_Actor *OrigVehicle(int v) { return reinterpret_cast<C_Actor *>(g_vehOrig[v]); }
// A car's velocity on both sides.
void SetCarVelocity(int v, const S_vector4 &velocity)
{
    const_cast<S_vector4 &>(reinterpret_cast<C_Car *>(Vehicle(v))->m_vehicle.GetVelocity()) = velocity;
    std::memcpy(g_vehOrig[v] + 0x4A0, &velocity, 16);
}

// The port's: virtual members of the same signatures, so that g++ passes and returns as it does for the actor's.
struct S_PortSeatFake {
    virtual int SeatCount();
    virtual bool HasWeapon(int seat);
    virtual S_vector Aim(int seat);
    virtual void Limits(int seat, float *up, float *down, float *left, float *right);
};
int S_PortSeatFake::SeatCount()
{
    Log(this, "GetSeatCount");
    return g_seat.seatCount;
}
bool S_PortSeatFake::HasWeapon(int seat)
{
    Log(this, "HasSeatWeapon", {double(seat)});
    return g_seat.hasSeatWeapon;
}
S_vector S_PortSeatFake::Aim(int seat)
{
    Log(this, "GetSeatWeaponAim", {double(seat)});
    return g_seat.aim;
}
void S_PortSeatFake::Limits(int seat, float *up, float *down, float *left, float *right)
{
    Log(this, "GetSeatWeaponLimits", {double(seat)});
    *up = g_seat.up;
    *down = g_seat.down;
    *left = g_seat.left;
    *right = g_seat.right;
}
// The original's (0x88, 0x98, 0xA4 with its hidden return pointer, 0xB8).
int BT O_GetSeatCount(void *self)
{
    Log(self, "GetSeatCount");
    return g_seat.seatCount;
}
int BT O_HasSeatWeapon(void *self, int seat)
{
    Log(self, "HasSeatWeapon", {double(seat)});
    return g_seat.hasSeatWeapon;
}
float *BT O_GetSeatWeaponAim(void *self, float *out, int seat)
{
    Log(self, "GetSeatWeaponAim", {double(seat)});
    out[0] = g_seat.aim.x;
    out[1] = g_seat.aim.y;
    out[2] = g_seat.aim.z;
    return out;
}
void BT O_GetSeatWeaponLimits(void *self, int seat, float *up, float *down, float *left, float *right)
{
    Log(self, "GetSeatWeaponLimits", {double(seat)});
    *up = g_seat.up;
    *down = g_seat.down;
    *left = g_seat.left;
    *right = g_seat.right;
}
// C_Vehicle::SetSteering (0x004f2420; game/vehicle.cpp is not linked here): logged on both sides.
int BT O_SetSteering(void *self, float value)
{
    Log(self, "SetSteering", {value});
    return 1;
}

}  // namespace

bool C_Vehicle::SetSteering(float value)
{
    Log(this, "SetSteering", {value});
    return true;
}
// (game/human_body.cpp's, not linked here: the movement's, logged.)
void S_HumanBody::RestoreCollision(bool unknown) { movement->RestoreCollision(unknown); }
void S_HumanBody::RemoveCollision(bool unknown) { movement->RemoveCollision(unknown); }

namespace {

// Each side's view: its own owner and its own vehicle objects (the pose's vehicle pointer translated).
void BodySide(bool original)
{
    WeaponSide(original);
    C_HumanWeaponPose *pose = IkPose();
    for (int v = 0; v < VEHICLES; ++v) {
        C_Actor *port = Vehicle(v), *orig = OrigVehicle(v);
        if (pose->m_vehicle == (original ? port : orig))
            pose->m_vehicle = original ? orig : port;
    }
}

void SetUpBody()
{
    SetUpWeapon();
    C_HumanWeaponPose *pose = IkPose();
    S_quat none;
    none.Identity();
    const auto dummy = [&](const std::string &name, const S_vector &at, I3D_frame *parent) {
        return NewFrame(FRAME_DUMMY, name, at, none, Box(S_vector(0.03f, 0.03f, 0.03f)), "", parent);
    };
    static const char *const names[VEHICLES] = {"car", "carBare", "gun", "gunBare"};
    std::vector<I3D_frame *> added;
    for (int v = 0; v < VEHICLES; ++v) {
        g_vehFrame[v] = NewFrame(FRAME_VISUAL, names[v], S_vector(30, 1, 30), none, Box(S_vector(1, 1, 2)), "", g_sector);
        added.push_back(g_vehFrame[v]);
        g_names[g_vehFrame[v]] = names[v];
    }
    g_wheel = dummy("wheel", S_vector(-0.35f, 1.0f, 0.55f), g_vehFrame[VEH_CAR]);
    added.push_back(g_wheel);
    added.push_back(dummy("LWC", S_vector(-0.18f, 0.0f, 0.0f), g_wheel));
    added.push_back(dummy("RWC", S_vector(0.18f, 0.0f, 0.0f), g_wheel));
    for (int v : {VEH_CAR, VEH_GUN}) {
        added.push_back(dummy("LWS", S_vector(-0.2f, 1.2f, 0.8f), g_vehFrame[v]));
        added.push_back(dummy("RWS", S_vector(0.2f, 1.2f, 0.8f), g_vehFrame[v]));
    }
    // The bare car's wheel grips: one only (no wheel then).
    added.push_back(dummy("LWC", S_vector(-0.18f, 1.0f, 0.55f), g_vehFrame[VEH_CAR_BARE]));
    for (I3D_frame *f : added) {
        g_ikFrames.push_back(f);
        g_sharedFrames.push_back(f);
        ShareFrameWords(f, false);
    }
    // The actors: the port's (a car of the port's layout; only the fields the pose reads set) and the original's (its
    // layout: type +0x18, frame +0x20, a car's vehicle +0x90 with its velocity at +0x4A0).
    for (int v = 0; v < VEHICLES; ++v) {
        const size_t size = sizeof(C_Car) + 0x100;
        g_vehBytes[v] = static_cast<uint8_t *>(::operator new(size, std::align_val_t(16)));
        std::memset(g_vehBytes[v], 0, size);
        C_Actor *a = Vehicle(v);
        *reinterpret_cast<void ***>(a) = p_vehVt;
        a->m_type = v < VEH_GUN ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
        a->m_frame = g_vehFrame[v];
        g_vehOrig[v] = static_cast<uint8_t *>(::operator new(0x600, std::align_val_t(16)));
        std::memset(g_vehOrig[v], 0, 0x600);
        *reinterpret_cast<void ***>(g_vehOrig[v]) = o_vehVt;
        *reinterpret_cast<int *>(g_vehOrig[v] + 0x18) = a->m_type;
        *reinterpret_cast<I3D_frame **>(g_vehOrig[v] + 0x20) = g_vehFrame[v];
        g_names[a] = g_names[g_vehOrig[v]] = names[v];
        g_names[reinterpret_cast<C_Car *>(a)->GetVehicle()] = g_names[g_vehOrig[v] + 0x90] = std::string(names[v]) + ".vehicle";
    }
    S_PortSeatFake fake;
    void **fakeVt = *reinterpret_cast<void ***>(&fake);
    for (void *&slot : p_vehVt)
        slot = reinterpret_cast<void *>(P_Unexpected);
    p_vehVt[VSlot(&C_Actor::GetSeatCount)] = fakeVt[0];
    p_vehVt[VSlot(&C_Actor::HasSeatWeapon)] = fakeVt[1];
    p_vehVt[VSlot(&C_Actor::GetSeatWeaponAim)] = fakeVt[2];
    p_vehVt[VSlot(&C_Actor::GetSeatWeaponLimits)] = fakeVt[3];
    for (void *&slot : o_vehVt)
        slot = reinterpret_cast<void *>(O_UnexpectedVirtual);
    o_vehVt[0x88 / 4] = reinterpret_cast<void *>(O_GetSeatCount);
    o_vehVt[0x98 / 4] = reinterpret_cast<void *>(O_HasSeatWeapon);
    o_vehVt[0xA4 / 4] = reinterpret_cast<void *>(O_GetSeatWeaponAim);
    o_vehVt[0xB8 / 4] = reinterpret_cast<void *>(O_GetSeatWeaponLimits);
    RedirectOriginal(0x004f2420, reinterpret_cast<const void *>(O_SetSteering));
    // The gunner poses (6 x 23 rotations and positions), the prone fit's buffers: shared and compared.
    pose->m_gunnerRot = static_cast<S_quat **>(::operator new(6 * sizeof(S_quat *)));
    pose->m_gunnerPos = static_cast<S_vector4 **>(::operator new(6 * sizeof(S_vector4 *)));
    for (int k = 0; k < 6; ++k) {
        pose->m_gunnerRot[k] = static_cast<S_quat *>(::operator new(POSE_BONE_COUNT * sizeof(S_quat), std::align_val_t(16)));
        pose->m_gunnerPos[k] = static_cast<S_vector4 *>(::operator new(POSE_BONE_COUNT * sizeof(S_vector4), std::align_val_t(16)));
        std::memset(static_cast<void *>(pose->m_gunnerRot[k]), 0, POSE_BONE_COUNT * sizeof(S_quat));
        std::memset(static_cast<void *>(pose->m_gunnerPos[k]), 0, POSE_BONE_COUNT * sizeof(S_vector4));
        // (Their w: the original copies GetPosKey's 12 bytes into them as 16; left out.)
        ShareBlock(pose->m_gunnerRot[k], POSE_BONE_COUNT * sizeof(S_quat), "gunnerRot" + std::to_string(k));
        ShareBlock(pose->m_gunnerPos[k], POSE_BONE_COUNT * sizeof(S_vector4), "gunnerPos" + std::to_string(k), true);
    }
    pose->m_proneHits = static_cast<uint8_t *>(::operator new(8));
    pose->m_proneBlendTime = static_cast<int *>(::operator new(6 * sizeof(int)));
    std::memset(pose->m_proneHits, 0, 8);
    std::memset(pose->m_proneBlendTime, 0, 6 * sizeof(int));
    const std::pair<S_vector4 *, const char *> prone[] = {{pose->m_proneGround, "proneGround"}, {pose->m_proneNormals, "proneNormals"},
                                                          {pose->m_proneFrom, "proneFrom"},     {pose->m_proneTo, "proneTo"},
                                                          {pose->m_proneSpheres, "proneSpheres"}};
    for (const auto &block : prone)
        ShareBlock(block.first, 6 * sizeof(S_vector4), block.second, true);   // (their w left out, as the pose's)
    // (The prone buffers' floats within the usual tolerance, absolute below 1: their near-zero normals and heights.)
    for (S_SharedWord &w : g_sharedWords)
        if (w.second.compare(0, 5, "prone") == 0)
            w.tolerance = g_tol;
    ShareBlock(pose->m_proneHits, 8, "proneHits");
    ShareBlock(pose->m_proneBlendTime, 6 * sizeof(int), "proneBlendTime");
    const auto *p = reinterpret_cast<const uint8_t *>(pose);
    g_ikPoseRest.assign(p, p + sizeof(C_HumanWeaponPose));
    g_ikFrameRest.clear();
    for (I3D_frame *f : g_ikFrames) {
        const auto *b = reinterpret_cast<const uint8_t *>(f);
        g_ikFrameRest.emplace_back(b, b + 0x400);
    }
    g_sideHook = BodySide;
    // The w of the vectors the original stores from stack temps whose w it leaves undefined: the effectors' targets
    // (the seats' updates), the acceleration's blend.
    for (int i = 0; i < 4; ++i)
        g_poseSkipWords.insert(offsetof(C_HumanWeaponPose, m_chain) + i * sizeof(S_PosePin) + offsetof(S_PosePin, target) + 12);
    for (size_t at : {offsetof(C_HumanWeaponPose, m_accel), offsetof(C_HumanWeaponPose, m_lastAccel), offsetof(C_HumanWeaponPose, m_sampledAccel)})
        g_poseSkipWords.insert(at + 12);
    // (The look's direction, start and end: PoseSlerpDir's 16-byte output, IntoAxes' temps.)
    for (size_t at : {offsetof(C_HumanWeaponPose, m_lookDir), offsetof(C_HumanWeaponPose, m_lookFrom), offsetof(C_HumanWeaponPose, m_lookTo)})
        g_poseSkipWords.insert(at + 12);
}

// The human's model where the skeleton's is (its lying dummies under the body), the human facing the model's way.
void HumanAtModel()
{
    C_Human *h = g_h.human;
    g_h.frame->SetLocalMatrix(g_ikModel->GetLocalMatrix());
    UpdateTree(g_h.frame);
    const S_matrix &m = g_ikModel->GetMatrix();
    h->m_position = {m.m[3][0], m.m[3][1], m.m[3][2], 1.0f};
    const S_vector z = m(2).Normalized();
    h->m_direction = {z.x, z.y, z.z, P(0.5f) ? 0.0f : U(-1, 1)};
}

// A random state for the body's tests: part 16's, then the look and the prone fit.
void RandomBody(bool lying)
{
    RandomWeapon();
    C_HumanWeaponPose *pose = IkPose();
    g_weaponNoOwner = false;                            // (a body always has its owner here)
    if (pose->m_flags & POSE_FLAG_STEER)
        pose->m_vehicle = Vehicle(VEH_CAR);              // (steering: in a car)
    if (lying) {
        // The skeleton lying along the model's z (a standing one: the fit gives up).
        S_quat q;
        const float a = (P(0.5f) ? 1.0f : -1.0f) * U(1.4f, 1.7f) * 0.5f;
        q.v = S_vector(std::sin(a), 0.0f, 0.0f);
        q.s = std::cos(a);
        if (P(0.85f))
            g_ikBone[POSE_BONE_PELVIS]->SetRot(q);
        g_ikBone[POSE_BONE_PELVIS]->SetPosInline(S_vector(0.0f, U(0.1f, 0.3f), 0.0f));
        UpdateTree(g_ikModel);
    }
    HumanAtModel();
    for (int i = 0; i < STANCE_DUMMY_COUNT; ++i)
        g_h.movement->m_stanceDummies[i] = g_stanceDummy[i];   // (every human model has all nine)
    // The head's look.
    static const uint32_t looks[] = {0, LOOK_AT, LOOK_AT | LOOK_ONCE, LOOK_AT | LOOK_RETURNING, LOOK_GLANCE, LOOK_GLANCE | LOOK_RETURNING,
                                     LOOK_GLANCE | LOOK_ONCE, LOOK_GLANCE | LOOK_ONCE | LOOK_RETURNING};
    uint32_t look = looks[I(0, 7)];
    for (uint32_t bit : {LOOK_QUICK, LOOK_NERVOUS, LOOK_GLANCE_LEFT, LOOK_GLANCE_RIGHT, LOOK_LAST_LEFT, LOOK_LAST_RIGHT})
        if (P(0.2f))
            look |= bit;
    pose->m_lookFlags = look;
    const S_vector head = g_ikBone[POSE_BONE_HEAD]->GetWorldPos();
    pose->m_lookAt = Dir4(head + IkRandomDir() * U(0.5f, 20.0f), U(-1, 1));
    const auto ahead = [] {
        return P(0.2f) ? IkRandomDir() : (S_vector(U(-0.8f, 0.8f), U(-0.6f, 0.6f), 1.0f)).Normalized();
    };
    pose->m_lookDir = Dir4(ahead());
    pose->m_lookFrom = Dir4(ahead());
    pose->m_lookTo = Dir4(ahead());
    pose->m_lookLength = I(1, 1500);
    pose->m_lookTime = P(0.2f) ? 0 : I(-50, pose->m_lookLength);
    pose->m_holdTime = P(0.1f) ? 0x7FFFFFFF : I(-100, 1500);
    pose->m_glanceTime = I(-100, 7000);
    pose->m_lookPower = P(0.7f) ? 2.5f : U(0.5f, 4.0f);
    pose->m_lookWeight = U(0, 1);
    // The prone fit.
    const S_matrix &model = g_ikModel->GetMatrix();
    const auto ground = [&] { return Dir4((model(1) + S_vector(U(-0.4f, 0.4f), 0, U(-0.4f, 0.4f))).Normalized(), U(-1, 1)); };
    for (int i = 0; i < 3; ++i) {
        pose->m_proneNow[i] = ground();
        pose->m_proneFrom[i] = P(0.3f) ? pose->m_proneNow[i] : ground();
        pose->m_proneTo[i] = P(0.3f) ? pose->m_proneNow[i] : ground();
    }
    for (S_vector4 *v : {&pose->m_proneNow[3], &pose->m_proneFrom[3], &pose->m_proneTo[3]})
        *v = {U(-0.2f, 0.2f), U(-0.2f, 0.2f), U(-0.2f, 0.2f), U(-1, 1)};
    pose->m_proneNow[4] = P(0.5f) ? PoseWorldPos4(g_ikModel) : Dir4(g_ikModel->GetWorldPos() + IkRandomDir() * U(0, 0.01f), 1.0f);
    if (P(0.25f))
        pose->m_proneNow[0].x = PoseUnset();
    for (int i = 0; i < 6; ++i)
        pose->m_proneBlendTime[i] = P(0.4f) ? 0 : I(0, 300);
    pose->m_proneBlendLength = P(0.6f) ? 100 : I(20, 400);
    pose->m_proneSettle = P(0.6f) ? 0 : I(1, 600);
    pose->m_updateCount = I(0, 1000000);
    const S_vector4 &dir = g_h.human->m_direction;
    pose->m_proneDir = P(0.5f) ? dir : Dir4(IkRandomDir(), U(-1, 1));
    std::memset(pose->m_proneHits, 0, 8);
    g_h.movement->m_unknown250 = U(-1, 1);
}

// The seats' state: the vehicle placed and turned near the body, its wheel turned, the seat script; then the pose put
// into the seat (the port's SetSeat) for the updates.
void RandomVehicles()
{
    for (I3D_frame *f : g_vehFrame) {
        const S_vector at = g_ikModel->GetWorldPos() + S_vector(U(-0.3f, 0.3f), U(-0.8f, -0.2f), U(-0.6f, 0.2f));
        f->SetPosInline(at);
        f->SetRot(P(0.5f) ? IkRandomRot(0.3f) : IkRandomRot(3.2f));
        UpdateTree(f);
    }
    S_quat q;
    const float a = (P(0.2f) ? U(-3.1f, 3.1f) : U(-1.3f, 1.3f)) * 0.5f;
    q.v = S_vector(0.0f, 0.0f, std::sin(a));
    q.s = std::cos(a);
    g_wheel->SetRot(q);
    UpdateTree(g_vehFrame[VEH_CAR]);
    g_seat.seatCount = I(1, 6);
    g_seat.hasSeatWeapon = P(0.6f);
    g_seat.aim = P(0.1f) ? S_vector(0, P(0.5f) ? 1.0f : -1.0f, 0) : IkRandomDir();
    if (P(0.05f)) {
        // (Nearly straight up the weapon's axes; not exactly: a dot past 1 sends the original's acos into its CRT's
        // errno, whose per-thread block the harness's copy of the original cannot allocate, its CRT never set up.)
        const S_matrix &gun = g_vehFrame[VEH_GUN]->GetMatrix();
        g_seat.aim = (gun(1).Normalized() + gun(2).Normalized() * 1e-3f).Normalized();
    }
    {
        // (Any aim within 1e-5 of the weapon's up axis nudged off it, for the same reason.)
        const S_matrix &gun = g_vehFrame[VEH_GUN]->GetMatrix();
        if (std::fabs(g_seat.aim.Dot(gun(1))) > 0.99999f)
            g_seat.aim = (g_seat.aim + gun(2).Normalized() * 1e-2f).Normalized();
    }
    g_seat.up = U(0.2f, 1.5f);
    g_seat.down = U(0.2f, 1.5f);
    g_seat.left = U(0.2f, 1.5f);
    g_seat.right = U(0.2f, 1.5f);
    if (P(0.05f)) {
        // (Limits below 0: the turn and the pitch past their clamps.)
        g_seat.right = -g_seat.right;
        g_seat.down = -g_seat.down;
    }
    for (int v = 0; v < 2; ++v) {
        const S_vector vel = IkRandomDir() * U(0, 20);
        SetCarVelocity(v, {vel.x, vel.y, vel.z, 0.0f});
    }
}

void TestBodyProne()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("body prone\n");
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomBody(true);
        g_softDiff = true;
        DiffCall("FitProneBody", [&] { pose->FitProneBody(); return 0; },
                 [&](void *) { OriginalAt<T_BVoid>(0x007a8980)(p); return 0; });
        g_softDiff = false;
    }
}

void TestBodyHead()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("body head\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomBody(false);
        switch (i % 4) {
        case 0: {
            static const uint32_t asks[] = {0, LOOK_AT, LOOK_AT | LOOK_ONCE, LOOK_GLANCE, LOOK_GLANCE | LOOK_ONCE, LOOK_GLANCE | LOOK_QUICK,
                                            LOOK_GLANCE | LOOK_NERVOUS, LOOK_GLANCE | LOOK_GLANCE_LEFT, LOOK_GLANCE | LOOK_GLANCE_RIGHT};
            uint32_t flags = asks[I(0, 8)];
            if (P(0.1f))
                flags |= uint32_t(I(0, 0xFF)) << 8;
            const S_vector4 at = Dir4(g_ikBone[POSE_BONE_HEAD]->GetWorldPos() + IkRandomDir() * U(0.5f, 20.0f), U(-1, 1));
            const S_vector4 *pos = P(0.1f) ? nullptr : &at;
            DiffCall("SetLook", [&] { pose->SetLook(flags, pos); return 0; },
                     [&](void *) { OriginalAt<T_BLook>(0x007acbe0)(p, flags, pos); return 0; });
            break;
        }
        case 1:
            DiffCall("PickGlance", [&] { pose->PickGlance(); return 0; },
                     [&](void *) { OriginalAt<T_BVoid>(0x007ace90)(p); return 0; });
            break;
        case 2: {
            const int time = P(0.1f) ? 0 : I(1, 200);
            g_softDiff = true;
            DiffCall("UpdateHeadLook", [&] { pose->UpdateHeadLook(time); return 0; },
                     [&](void *) { OriginalAt<T_BInt>(0x007ad120)(p, time); return 0; });
            g_softDiff = false;
            break;
        }
        default: {
            const S_vector dir = P(0.1f) ? g_ikBone[POSE_BONE_HEAD]->GetMatrix()(2).Normalized() : IkRandomDir();
            const float blend = P(0.3f) ? 1.0f : U(0, 1);
            DiffCall("TurnHead", [&] { pose->TurnHead(dir, blend); return 0; },
                     [&](void *) { OriginalAt<T_BTurnHead>(0x007ade70)(p, &dir, blend); return 0; });
            break;
        }
        }
    }
}

uint32_t RandomSeatFlags()
{
    uint32_t flags = 0;
    if (P(0.4f))
        flags |= SEAT_FLAG_CONTROLS;
    if (P(0.6f))
        flags |= SEAT_FLAG_SIT;
    if (P(0.1f))
        flags |= SEAT_FLAG_HIDDEN;
    if (P(0.5f))
        flags |= SEAT_FLAG_WEAPON;
    return flags;
}

void TestBodySeats()
{
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    std::printf("body seats\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomBody(false);
        RandomVehicles();
        const int seat = I(0, 5);
        switch (i % 5) {
        case 0: {
            // Into a seat or out of it (from a car's controls too: the steering let go).
            if (P(0.3f)) {
                BodySide(false);
                pose->SetSeat(SEAT_FLAG_CONTROLS, Vehicle(VEH_CAR), I(0, 3));
            }
            pose->m_stance = I(0, 3);
            pose->m_stanceBeforeSeat = I(0, 2);
            const int v = P(0.2f) ? -1 : I(0, VEHICLES - 1);
            C_Actor *vehicle = v < 0 ? nullptr : Vehicle(v), *origVehicle = v < 0 ? nullptr : OrigVehicle(v);
            const uint32_t flags = RandomSeatFlags();
            DiffCall("SetSeat", [&] { pose->SetSeat(flags, vehicle, seat); return 0; },
                     [&](void *) { OriginalAt<T_BSeat>(0x007ae190)(p, flags, origVehicle, seat); return 0; });
            break;
        }
        case 1: {
            BodySide(false);
            pose->SetSeat(SEAT_FLAG_CONTROLS, Vehicle(VEH_CAR), seat);
            RandomVehicles();
            pose->m_accelTime = I(-50, 200);
            // (The accelerations sampled before: the neck's sway, capped.)
            pose->m_lastAccel = Dir4(IkRandomDir() * (P(0.2f) ? 0.0f : U(0, 120)));
            pose->m_sampledAccel = Dir4(IkRandomDir() * (P(0.2f) ? 0.0f : U(0, 120)));
            pose->m_vehicleVelocity = Dir4(IkRandomDir() * U(0, 20));
            g_softDiff = true;
            DiffCall("UpdateSteering", [&] { pose->UpdateSteering(); return 0; },
                     [&](void *) { OriginalAt<T_BVoid>(0x007ae820)(p); return 0; });
            g_softDiff = false;
            break;
        }
        case 2: {
            BodySide(false);
            pose->SetSeat(SEAT_FLAG_SIT | (P(0.6f) ? SEAT_FLAG_WEAPON : 0u), Vehicle(P(0.8f) ? VEH_CAR : VEH_CAR_BARE), seat);
            RandomVehicles();
            pose->m_accelTime = I(-50, 200);
            // (The accelerations sampled before: the neck's sway, capped.)
            pose->m_lastAccel = Dir4(IkRandomDir() * (P(0.2f) ? 0.0f : U(0, 120)));
            pose->m_sampledAccel = Dir4(IkRandomDir() * (P(0.2f) ? 0.0f : U(0, 120)));
            g_softDiff = true;
            DiffCall("UpdateSeat", [&] { pose->UpdateSeat(); return 0; },
                     [&](void *) { OriginalAt<T_BVoid>(0x007af040)(p); return 0; });
            g_softDiff = false;
            break;
        }
        case 3: {
            // (At a static weapon with its grips: the update runs only then; a grip lost sometimes.)
            BodySide(false);
            pose->SetSeat(0, Vehicle(VEH_GUN), seat);
            pose->m_seat = seat;
            if (P(0.1f))
                (P(0.5f) ? pose->m_seatLeftGrip : pose->m_seatRightGrip) = nullptr;
            if (P(0.6f))
                pose->SetSeatWeaponPose(PoseAnim(I(0, POSE_ANIMS - 1)));
            else
                pose->m_hasGunnerPose = P(0.2f);
            RandomVehicles();
            g_softDiff = true;
            DiffCall("UpdateStaticWeapon", [&] { pose->UpdateStaticWeapon(); return 0; },
                     [&](void *) { OriginalAt<T_BVoid>(0x007af780)(p); return 0; });
            g_softDiff = false;
            break;
        }
        default: {
            void *anim = P(0.15f) ? nullptr : PoseAnim(I(0, POSE_ANIMS - 1));
            DiffCall("SetSeatWeaponPose", [&] { pose->SetSeatWeaponPose(static_cast<I3D_animation_set *>(anim)); return 0; },
                     [&](void *) { OriginalAt<T_BPtr>(0x007b9070)(p, anim); return 0; });
            break;
        }
        }
    }
}

void TestBody()
{
    SetUpBody();
    TestBodyProne();
    TestBodyHead();
    TestBodySeats();
    // Judged by their rates: the seats' updates solve 16-pass chains (as part 15's whole solves); the head look and the
    // prone fit differ in about 3 of 10,000 and 1 of 12,000 calls. Those head looks all turn the head back towards its own
    // direction (LOOK_RETURNING) through the clamps, by up to 0.2; not traced: the port's TurnHead aimed at where the
    // original's head ended up reproduces it in some of them and not in others; the sums' groupings, the clamps, the
    // slerp and LS3DF's RotateByMatrix were checked against the original and agree.
    for (const auto &rate : {std::make_pair("UpdateSteering", 0.95), std::make_pair("UpdateSeat", 0.95), std::make_pair("UpdateStaticWeapon", 0.95),
                             std::make_pair("UpdateHeadLook", 0.999), std::make_pair("FitProneBody", 0.999)}) {
        const S_Stats &st = g_stats[rate.first];
        std::printf("%s: %ld of %ld agree\n", rate.first, st.agree, st.calls);
        CHECK(st.calls > 0 && double(st.agree) >= rate.second * double(st.calls));
    }
}

}  // namespace
