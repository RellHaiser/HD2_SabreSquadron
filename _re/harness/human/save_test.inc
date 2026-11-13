// Part 19b of the human harness (HUMAN_SAVE builds, build_save.bat; included by human_test.cpp after core_test.inc): the
// weapon pose's saved games (game/human_weapon_pose.cpp: SaveGame, LoadGame) against the original's, on part 19's
// living, seated and dead bodies. SaveGame's files are compared byte for byte; LoadGame reads the port's file on both
// sides. (Animation names are saved, but loaded files have none: the original looks a name up in the animation cache
// inline, its own containers and the driver, which no redirect reaches.)

namespace {

typedef bool(__attribute__((thiscall)) * T_SChunk)(void *, C_chunk *);
void __attribute__((thiscall)) OS_StainsLoad(void *self, C_chunk *) { Log(self, "StainsLoad"); }
void OS_StainsSkip(C_chunk *) { Log(nullptr, "StainsSkip"); }
void __attribute__((thiscall)) OS_StainsSave(void *self, C_chunk *) { Log(self, "StainsSave"); }

// The actors by net id: the human 5, the vehicles 10 + their number (each side its own vehicle objects).
const int HUMAN_NET_ID = 5, VEHICLE_NET_ID = 10;
C_Actor *ActorById(int id, bool original)
{
    if (id == HUMAN_NET_ID)
        return g_h.human;
    if (id >= VEHICLE_NET_ID && id < VEHICLE_NET_ID + VEHICLES)
        return original ? OrigVehicle(id - VEHICLE_NET_ID) : Vehicle(id - VEHICLE_NET_ID);
    return nullptr;
}
void *__attribute__((thiscall)) OS_GetActorById(void *, int id)
{
    Log(&g_actorsModule, "GetActorById", {double(id)});
    return ActorById(id, true);
}

void SetUpSave()
{
    SetUpCore();
    g_actorById = ActorById;
    RedirectOriginal(0x00475d50, reinterpret_cast<const void *>(OS_GetActorById));
    RedirectOriginal(0x00798bc0, reinterpret_cast<const void *>(OS_StainsLoad));
    RedirectOriginal(0x00798f70, reinterpret_cast<const void *>(OS_StainsSkip));
    RedirectOriginal(0x00798930, reinterpret_cast<const void *>(OS_StainsSave));
    // The net ids (the original's C_Actor +0x10; the port's through SetNetId).
    g_h.human->SetNetId(HUMAN_NET_ID);
    std::memcpy(o_human + 0x10, &HUMAN_NET_ID, 4);
    for (int v = 0; v < VEHICLES; ++v) {
        const int id = VEHICLE_NET_ID + v;
        Vehicle(v)->SetNetId(id);
        std::memcpy(g_vehOrig[v] + 0x10, &id, 4);
    }
}

// A pose to save: living (part 19's), in a seat, or dead (the dying animation or the ragdoll, slide points, carried),
// with stains or none, a vehicle or none.
void RandomSaveState(int kind)
{
    C_HumanWeaponPose *pose = IkPose();
    switch (kind) {
    case 0:
        RandomCoreLiving();
        break;
    case 1: {
        RandomDead(false);
        RandomVehicles();
        pose->m_flags = 0;
        BodySide(false);
        const int seat = I(0, 5);
        static const uint32_t seats[] = {SEAT_FLAG_CONTROLS, SEAT_FLAG_SIT | SEAT_FLAG_WEAPON, SEAT_FLAG_SIT, 0};
        const int which = I(0, 3);
        g_seat.hasSeatWeapon = which == 1;
        pose->SetSeat(seats[which], Vehicle(which == 2 ? VEH_CAR_BARE : which == 3 ? VEH_GUN : VEH_CAR), seat);
        RandomVehicles();
        break;
    }
    default:
        RandomCoreDead(P(0.5f));
        if (P(0.3f))
            pose->m_slidePoints = g_slideBlock;
        break;
    }
    pose->m_stains = P(0.4f) ? Stains() : nullptr;
    if (P(0.2f))
        pose->m_vehicle = Vehicle(I(0, VEHICLES - 1));
    pose->m_carrier = P(0.3f) ? g_h.human : nullptr;
    pose->m_dropSound = P(0.5f) ? -1 : I(-2, 5);
    // (Contacts by name are part 7's.)
    for (S_RagdollPoint *point : pose->m_points)
        if (point)
            point->contactFrame = nullptr;
    pose->m_contactFrames = I(0, 3);                    // (written as it is)
    // Pushes and holds on points or none.
    const auto anyPoint = [&]() { return P(0.4f) ? nullptr : pose->m_points[I(0, RAGDOLL_POINT_COUNT - 1)]; };
    for (int i = 0; i < RAGDOLL_PUSH_COUNT; ++i)
        pose->m_pushes[i].point = anyPoint();
    for (int i = 0; i < RAGDOLL_HOLD_COUNT; ++i)
        pose->m_holds[i].point = anyPoint();
    // In a seat: chain frames and joints of any bone (the skeleton's, as numbers), a joint towards the parent (not
    // saved: only the children's are numbered).
    if (kind == 1)
        for (S_PoseBone &bone : pose->m_bones) {
            if (P(0.3f))
                bone.chainFrame = P(0.2f) ? g_ikModel : pose->m_bones[I(0, POSE_BONE_COUNT - 1)].frame;
            if (P(0.3f))
                bone.chainLimit = P(0.2f) ? nullptr : &pose->m_bones[I(0, POSE_BONE_COUNT - 1)].limits[I(0, 5)];
        }
}

// The pose's fields LoadGame reads, given other values first (pointers it sets, counts, timers, vectors).
void ScrambleForLoad()
{
    C_HumanWeaponPose *pose = IkPose();
    const auto any4 = [] { return S_vector4{U(-2, 2), U(-2, 2), U(-2, 2), U(-2, 2)}; };
    pose->m_time = I(0, 200);
    pose->m_updateCount = I(0, 1000);
    pose->m_aimTime = I(0, 300);
    pose->m_aimLength = I(1, 300);
    pose->m_raised = P(0.5f);
    pose->m_weaponKind = I(0, 7);
    pose->m_stance = I(0, 3);
    pose->m_hand = uint32_t(I(0, 3));
    pose->m_flags = uint32_t(I(0, 0xFFFF));
    pose->m_blend = U(0, 1);
    for (S_vector4 *v : {&pose->m_aimDir, &pose->m_weaponDir, &pose->m_velocity, &pose->m_lastPos, &pose->m_lookAt,
                         &pose->m_proneDir, &pose->m_bodyPos, &pose->m_cameraPos, &pose->m_gravity})
        *v = any4();
    pose->m_vehicle = P(0.5f) ? Vehicle(I(0, VEHICLES - 1)) : nullptr;
    pose->m_seatLeftGrip = pose->m_seatRightGrip = P(0.5f) ? g_ikModel : nullptr;
    pose->m_seat = I(0, 5);
    pose->m_lookFlags = uint32_t(I(0, 15));
    pose->m_proneFit = P(0.5f);
    for (int i = 0; i < 6; ++i) {
        pose->m_proneHits[i] = uint8_t(I(0, 1));
        pose->m_proneNow[i] = any4();
        pose->m_proneBlendTime[i] = I(0, 500);
    }
    pose->m_hasGunnerPose = P(0.5f);
    for (int k = 0; k < 6; ++k)
        for (int b = 0; b < POSE_BONE_COUNT; ++b)
            pose->m_gunnerRot[k][b].Make(IkRandomDir(), U(-3, 3));
    pose->m_rootBody = P(0.5f) ? nullptr : pose->m_bodies[I(1, RAGDOLL_BODY_COUNT - 1)];
    pose->m_rootPoint = P(0.5f) ? nullptr : pose->m_points[I(1, RAGDOLL_POINT_COUNT - 1)];
    pose->m_activityAnim = P(0.5f) ? PoseAnim(0) : nullptr;
    pose->m_deathAnim = P(0.5f) ? PoseAnim(1) : nullptr;
    pose->m_deathKind = int16_t(I(0, 4));
    pose->m_deathTime = I(0, 5000);
    pose->m_slidePoints = nullptr;
    pose->m_carrier = P(0.5f) ? g_h.human : nullptr;
    pose->m_ragdollWeight = U(0, 1);
    pose->m_unknown68E8 = I(0, 100);
    pose->m_steps = I(1, 8);
    pose->m_contactFrames = I(0, 3);
    pose->m_inWater = P(0.5f);
    pose->m_dropSound = I(-2, 5);
    for (int i = 0; i < RAGDOLL_PUSH_COUNT; ++i)
        pose->m_pushes[i].time = U(0, 500);
}

void TestSave()
{
    SetUpSave();
    C_HumanWeaponPose *pose = IkPose();
    void *p = pose;
    const std::string portFile = TempFile("pose_port.sav"), origFile = TempFile("pose_orig.sav");
    std::printf("save\n");
    for (int i = 0; i < 4500 / QUICK; ++i) {
        const int kind = i % 3;
        RandomSaveState(kind);
        // (The files to load have no animation names.)
        const bool forLoad = i % 2 == 1;
        if (forLoad)
            pose->m_activityAnim = pose->m_deathAnim = nullptr;
        std::remove(portFile.c_str());
        std::remove(origFile.c_str());
        static const char *const saves[] = {"SaveGame living", "SaveGame seat", "SaveGame dead"};
        DiffCall(saves[kind],
                 [&] {
                     C_chunk c;
                     c.Open(portFile.c_str(), true);
                     return int(pose->SaveGame(c));
                 },
                 [&](void *) {
                     C_chunk c;
                     c.Open(origFile.c_str(), true);
                     return int(OriginalAt<T_SChunk>(0x0079d540)(p, &c));
                 });
        const std::vector<uint8_t> saved = FileBytes(portFile), origSaved = FileBytes(origFile);
        if (saved != origSaved) {
            size_t at = 0;
            while (at < saved.size() && at < origSaved.size() && saved[at] == origSaved[at])
                ++at;
            std::printf("  %s file differs at %zu of %zu / %zu:", saves[kind], at, saved.size(), origSaved.size());
            for (size_t k = at; k < at + 8 && k < saved.size() && k < origSaved.size(); ++k)
                std::printf(" %02x/%02x", saved[k], origSaved[k]);
            std::printf("\n");
        }
        CHECK(saved == origSaved);
        CHECK(saved.size() > 1000);
        if (!forLoad)
            continue;
        // Loaded into another state of the same skeleton and ragdoll: the slide points each side allocates compared
        // apart (their pointers are each side's own).
        if (P(0.5f))
            RandomSaveState(I(0, 2));
        ScrambleForLoad();
        std::vector<float> portSlide, origSlide;
        const auto takeSlide = [&](std::vector<float> &out) {
            out.clear();
            if (pose->m_slidePoints) {
                const float *f = reinterpret_cast<const float *>(pose->m_slidePoints);
                for (int k = 0; k < DEATH_HELD_POINT_COUNT; ++k)
                    out.insert(out.end(), f + 4 * k, f + 4 * k + 3);
                pose->m_slidePoints = nullptr;      // (left allocated: a few blocks)
            }
        };
        static const char *const loads[] = {"LoadGame living", "LoadGame seat", "LoadGame dead"};
        DiffCall(loads[kind],
                 [&] {
                     C_chunk c;
                     c.Open(portFile.c_str(), false);
                     const int r = int(pose->LoadGame(c));
                     takeSlide(portSlide);
                     return r;
                 },
                 [&](void *) {
                     C_chunk c;
                     c.Open(portFile.c_str(), false);
                     const int r = int(OriginalAt<T_SChunk>(0x0079f3f0)(p, &c));
                     takeSlide(origSlide);
                     return r;
                 });
        CHECK(portSlide == origSlide);
        if (i % 30 == 1) {
            // Another chunk where the pose's should be: nothing read.
            {
                C_chunk c;
                c.Open(origFile.c_str(), true);
                c.Begin(uint16_t(I(1, 0xDEEF)));
                const int x = I(0, 1000);
                c.Write(&x, 4);
                c.Ascend();
            }
            DiffCall("LoadGame other chunk",
                     [&] {
                         C_chunk c;
                         c.Open(origFile.c_str(), false);
                         return int(pose->LoadGame(c));
                     },
                     [&](void *) {
                         C_chunk c;
                         c.Open(origFile.c_str(), false);
                         return int(OriginalAt<T_SChunk>(0x0079f3f0)(p, &c));
                     });
        }
    }
    std::remove(portFile.c_str());
    std::remove(origFile.c_str());
}

}  // namespace
