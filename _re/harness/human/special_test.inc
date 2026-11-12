// Part 9 of the human harness (HUMAN_SPECIAL builds, build_special.bat; included by human_test.cpp after
// player_test.inc): the special moves (game/human_special_moves.cpp: turning and leaning, pushing out of walls, the
// water, the ladders, climbing over, the room to fall, the death, the room for the stance changes) against the
// original's, in part 1's world. The movement base, the mixer and the animation player are the real ones on both
// sides (HUMAN_BASE / MIXER / PLAYER); the original's special moves, which earlier parts redirected to loggers, run.

// What the special moves call besides part 1-8's fakes: logged on both sides.
void C_Human::DropCarried() { Log(this, "DropCarried"); }
void C_Ladder::AddClimber(C_Human *human) { Log(this, "AddClimber", {double(human != nullptr)}); }
namespace {
void __attribute__((thiscall)) OS_DropCarried(void *self) { Log(self, "DropCarried"); }
void __attribute__((thiscall)) OS_AddClimber(void *self, void *human) { Log(self, "AddClimber", {double(human != nullptr)}); }
// The vehicle's LeaveSeat (the original's slot 0x80, the port's own): logged, no one in the seat after.
void *__attribute__((thiscall)) F_LeaveSeat(void *self, int seat)
{
    Log(self, "LeaveSeat", {double(seat)});
    return nullptr;
}
}  // namespace

namespace {

typedef void(__attribute__((thiscall)) * T_SInit)(void *, void *, void *, void *, void *, void *);
typedef bool(__attribute__((thiscall)) * T_SBoolFloat)(void *, const float *);   // (the angle by reference)
typedef bool(__attribute__((thiscall)) * T_SBoolPtr)(void *, void *);
typedef void(__attribute__((thiscall)) * T_SVoidPtr)(void *, void *);
typedef bool(__attribute__((thiscall)) * T_SBool)(void *);
typedef void(__attribute__((thiscall)) * T_SVoid)(void *);
typedef int(__attribute__((thiscall)) * T_SIntPtr)(void *, void *);
typedef bool(__attribute__((thiscall)) * T_SBoolPtrInt)(void *, const void *, int);
typedef bool(__attribute__((thiscall)) * T_SBoolInt)(void *, int);

const uint32_t O_DEATH_SEED = 0x009831a0, O_LAST_SEAT_DEATH_ANIM = 0x0084e858;
const size_t O_NEXT_ANIM_STATE = 0x1FC, O_SPECIAL_MOVES = 0xAA0, O_IDLE = 0xCB0;

// The original's special moves (the movement copy's +0xAA0) from DiffMove's movement.
uint8_t *SpecialOf(void *movement) { return static_cast<uint8_t *>(movement) + O_SPECIAL_MOVES; }
void *NextOf(void *movement) { return static_cast<uint8_t *>(movement) + O_NEXT_ANIM_STATE; }

// The obstacles to climb over or to meet (x / z extents of part 1's blocks; the truck).
struct S_Obstacle {
    float x0, z0, x1, z1;
};
const S_Obstacle kObstacles[] = {
    {20, 4, 22, 8},     {20, 10, 22, 14},   {20, 16, 22, 20}, {20, 22, 22, 26}, {40, 4, 40.5f, 28},
    {24, 30, 30, 36},   {32, 30, 36, 36},   {46, 40, 52, 46}, {10, 40, 30, 40.4f}, {44, 30, 44.4f, 38},
    {54.5f, 7, 57.5f, 13}, {11, 49, 12.5f, 55},   {15, 49, 16.5f, 55},   // (under the low roof)
    {3, 31, 4.5f, 37},     {3, 43, 4.5f, 47},                             // (under the slabs)
};

// The body before one of an obstacle's faces, `gap` from it, facing it (a little turned).
void FaceObstacle(float gap, int which = -1)
{
    const S_Obstacle &o = kObstacles[which >= 0 ? which : I(0, static_cast<int>(std::size(kObstacles)) - 1)];
    float x, z, dx = 0, dz = 0;
    switch (I(0, 3)) {
    case 0:
        x = o.x0 - gap, z = U(o.z0, o.z1), dx = 1;
        break;
    case 1:
        x = o.x1 + gap, z = U(o.z0, o.z1), dx = -1;
        break;
    case 2:
        x = U(o.x0, o.x1), z = o.z0 - gap, dz = 1;
        break;
    default:
        x = U(o.x0, o.x1), z = o.z1 + gap, dz = -1;
        break;
    }
    const float turn = U(-0.4f, 0.4f);
    const S_vector dir = S_vector(dx + dz * turn, 0.0f, dz - dx * turn).Normalized();
    // (On the floor, at 1 m before every obstacle: GroundAt would find the slabs and the low roof above.)
    g_h.human->m_position = {x, 1.0f, z, 1.0f};
    g_h.human->m_direction = {dir.x, 0.0f, dir.z, 0.0f};
}

// The model, its dummies and the stance volumes where the body is now.
void PlaceBody()
{
    C_HumanMovement *m = g_h.movement;
    const S_vector4 &p = g_h.human->m_position;
    g_h.frame->SetPosInline(S_vector(p.x, p.y, p.z));
    const S_vector4 &d = g_h.human->m_direction;
    S_quat yaw;
    yaw.Make(S_vector(0, 1, 0), std::atan2(d.x, d.z));
    g_h.frame->SetRot(yaw);
    UpdateTree(g_h.frame);
    PlaceJoints(false);
    m->UpdateDummies();
    m->UpdateStanceVolumes(g_h.work->m_request.stance);
}

// A random human for the special moves: part 6's and the player's state, then where and how the body stands.
void RandomSpecial()
{
    RandomPlayerHuman();
    C_HumanMovement *m = g_h.movement;
    C_HumanWork *w = g_h.work;
    // (The tables named by both sides: the death reads them.)
    RandomTables();
    g_humanAnimTable = reinterpret_cast<C_HumanAnimTable *>(g_animTableBytes[0]);
    g_humanAnimTable2 = reinterpret_cast<C_HumanAnimTable *>(g_animTableBytes[1]);
    *OriginalAt<void **>(O_HUMAN_ANIM_TABLE) = g_humanAnimTable;
    *OriginalAt<void **>(O_HUMAN_ANIM_TABLE2) = g_humanAnimTable2;
    w->m_group = I(1, 2);
    w->m_request.stance = I(0, 2);
    m->m_unknownA2C = P(0.8f) ? MOVE_NO_TIME : I(0, 500);
    m->m_unknownA30 = P(0.8f) ? MOVE_NO_TIME : I(0, 2);
    m->m_unknown9AC = P(0.7f) ? MOVE_NO_TIME : I(0, 1000);
    m->m_useState = P(0.8f) ? MOVE_NO_TIME : I(0, 3);
    // (A ladder in use is one the body has: both sides read it then.)
    m->m_useObject = P(0.5f) || m->m_useState != MOVE_NO_TIME ? reinterpret_cast<C_Actor *>(Ladder()->port) : nullptr;
    m->m_vehicle = nullptr;
    g_h.human->m_vehicle = nullptr;
    // (A live body: only one that pushes, state 1 (7, 8), has stance volumes. Part 3's state has any.)
    m->m_unknown1AC = P(0.9f) ? 1 : I(0, 9);
    // (Mostly the volumes at their scale, not frozen, no stance change under way: part 4's state varies them.)
    if (P(0.9f)) {
        m->m_stanceVolumeScale = 0.8f;
        m->m_flags &= ~MOVE_FLAG_FROZEN;
        m->m_nextStance = 3;                            // (3: none)
        m->m_nextStance2 = 3;
        m->m_stanceChange = MOVE_NO_TIME;
    }
    m->m_seatMode = P(0.8f) ? 0 : I(1, 5);             // (6..8, at a seat weapon, only with a vehicle: Die's test)
    m->m_trailCount = I(0, 4);
    m->m_locomotion.m_fallSpeed = P(0.8f) ? 0.0f : U(-2, 14);
    m->m_locomotion.m_fallStartY = P(0.5f) ? MOVE_NO_VALUE : U(0, 6);
    m->m_locomotion.m_unknown0F0 = P(0.7f) ? 0 : 1;
    m->m_locomotion.m_waterLevel = P(0.1f) ? -FLT_MAX : 3.0f;
    // Where: before an obstacle (half the time), else part 1's places.
    if (P(0.5f))
        FaceObstacle(P(0.2f) ? U(0.0f, 0.15f) : U(0.15f, 1.2f));
    PlaceBody();
}

// The original's own global state the special moves keep: the death's seed and the last seat death animation.
void SyncSpecialGlobals()
{
    g_deathSeed = I(0, 200);
    g_lastSeatDeathAnim = P(0.5f) ? -1 : I(0x83, 0x85);
    *OriginalAt<int *>(O_DEATH_SEED) = g_deathSeed;
    *OriginalAt<int *>(O_LAST_SEAT_DEATH_ANIM) = g_lastSeatDeathAnim;
}
void CheckSpecialGlobals(const char *what, int seed, int lastSeat)
{
    const bool same = seed == *OriginalAt<int *>(O_DEATH_SEED) && lastSeat == *OriginalAt<int *>(O_LAST_SEAT_DEATH_ANIM);
    if (!same) {
        static int shown;
        if (shown++ < 10)
            std::printf("  %s: seed %d / %d, last seat death %d / %d\n", what, seed, *OriginalAt<int *>(O_DEATH_SEED), lastSeat,
                        *OriginalAt<int *>(O_LAST_SEAT_DEATH_ANIM));
    }
    CHECK(same);
}

void SetUpSpecial()
{
    SetUpPlayer();
    // The original's special moves part 1 and part 6 redirected to loggers run for real here.
    for (uint32_t at : {0x0074a2c0u, 0x0074f3c0u, 0x0074aaf0u, 0x0074eee0u, 0x0074a290u, 0x0074b950u})
        if (!RestoreOriginal(at)) {
            std::printf("special: %08x not restored\n", at);
            std::exit(3);
        }
    // The original's static initialisers of the lying body's head / feet offsets and their reference direction
    // (0x009831b0..0x009831d8: the test's copy of the image never ran them).
    for (uint32_t at : {0x0074a110u, 0x0074a140u, 0x0074a170u})
        OriginalAt<void (*)()>(at)();
    RedirectOriginal(0x005a97c0, reinterpret_cast<const void *>(OS_DropCarried));
    RedirectOriginal(0x005eb8c0, reinterpret_cast<const void *>(OS_AddClimber));
    g_carVt[0x80 / 4] = g_carVt[VSlot(&C_Actor::LeaveSeat)] = reinterpret_cast<void *>(F_LeaveSeat);
    // Undefined in the original, not compared: the w of the look-at vector the climbs and the ladders clear (a stack
    // temp's: the work's +0x1C), and the w of the trail spheres they place (+0x3C).
    g_workSkipWords.insert(0x1C);
    g_sharedWords.erase(std::remove_if(g_sharedWords.begin(), g_sharedWords.end(),
                                       [](const auto &w) {
                                           return w.second.rfind("trailVolume", 0) == 0 &&
                                                  w.second.size() > 3 && w.second.compare(w.second.size() - 3, 3, "+60") == 0;
                                       }),
                        g_sharedWords.end());
}

// PushOutOfWalls on a hit: the port puts the body's collision back (its documented fix), the original leaves it
// out. The original's side gets the restore after its call, so that both end alike.
typedef void(__attribute__((thiscall)) * T_SRestore)(void *, int);
void RestoreLikeThePort(void *movement)
{
    int removed = 0, restored = 0;
    for (const S_Call &c : g_calls) {
        if (c.what.find("SetIgnoredVolumes") != std::string::npos)
            ++removed;
        else if (c.what.find("SetIgnoredVolume null") != std::string::npos)
            ++restored;
    }
    if (removed > restored)
        OriginalAt<T_SRestore>(0x0075cf20)(movement, 0);
}

// Turning, leaning, sitting, pushing out of walls, the water.
void TestSpecialSmall()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanSpecialMoves &s = m->m_specialMoves;
    std::printf("special: small\n");
    for (int i = 0; i < 30000 / QUICK; ++i) {
        RandomSpecial();
        C_HumanWork *w = g_h.work;
        switch (i % 6) {
        case 0:
            s.m_movement = nullptr;
            s.m_owner = nullptr;
            s.m_locomotion = nullptr;
            s.m_animPlayer = nullptr;
            s.m_idle = nullptr;
            DiffMove("Init", [&] { s.Init(m, &m->m_locomotion, &m->m_animPlayer, &m->m_idle, g_h.human); return 0; },
                     [&](void *mv) {
                         OriginalAt<T_SInit>(0x0074a290)(SpecialOf(mv), mv, static_cast<uint8_t *>(mv) + 0xAC0,
                                                         static_cast<uint8_t *>(mv) + 0xC00, static_cast<uint8_t *>(mv) + O_IDLE, o_human);
                         return 0;
                     });
            break;
        case 1: {
            // Swimming, leaning or lying now and then (the test runs only then).
            if (P(0.3f))
                w->m_mode = WORK_MODE_SWIMMING;
            if (P(0.3f))
                m->m_animState.anim.anim = I(0xA2, 0xA5);
            if (P(0.2f))
                m->m_nextAnimState.anim.anim = P(0.5f) ? 0x2B : 0x2C;
            if (P(0.3f)) {
                // On a ramp: the swept spheres meet its slope (a hit that is not a wall).
                const float x = U(4.5f, 9.5f), z = P(0.5f) ? U(4.5f, 7.5f) : U(10.5f, 13.5f);
                g_h.human->m_position = {x, GroundAt(x, z), z, 1.0f};
                PlaceBody();
            }
            const float angle = P(0.3f) ? U(-0.5f, 0.5f) : U(-4.0f, 4.0f);
            DiffMove("CanTurn", [&] { return int(s.CanTurn(angle)); },
                     [&](void *mv) { return int(OriginalAt<T_SBoolFloat>(0x0074a2c0)(SpecialOf(mv), &angle)); });
            break;
        }
        case 2:
            if (P(0.8f))
                m->m_nextAnimState.anim.anim = I(0xA2, 0xA5);
            m->m_nextAnimState.anim.stance = I(0, 2);
            if (P(0.1f))
                m->m_nextAnimState.anim = m->m_animState.anim;
            DiffMove("AdjustLean", [&] { s.AdjustLean(&m->m_nextAnimState); return 0; },
                     [&](void *mv) { OriginalAt<T_SVoidPtr>(0x0074a6f0)(SpecialOf(mv), NextOf(mv)); return 0; });
            break;
        case 3:
            m->m_nextAnimState.anim.anim = P(0.5f) ? 0x22 : I(0, 0x30);
            DiffMove("StartSitting", [&] { s.StartSitting(&m->m_nextAnimState); return 0; },
                     [&](void *mv) { OriginalAt<T_SVoidPtr>(0x0074ec80)(SpecialOf(mv), NextOf(mv)); return 0; });
            break;
        case 4:
            if (P(0.6f))
                FaceObstacle(U(0.0f, 0.5f));
            PlaceBody();
            DiffMove("PushOutOfWalls", [&] { s.PushOutOfWalls(); return 0; },
                     [&](void *mv) {
                         OriginalAt<T_SVoid>(0x0074ecb0)(SpecialOf(mv));
                         RestoreLikeThePort(mv);
                         return 0;
                     });
            break;
        default: {
            // In and by the pond (its surface at y 3 over the floor at 1) or the shallow one (1.75), swimming or not.
            if (P(0.4f)) {
                const float x = U(51.5f, 60.5f), z = U(51.5f, 60.5f);
                g_h.human->m_position = {x, U(0.9f, 3.6f), z, 1.0f};
                PlaceBody();
            } else if (P(0.3f)) {
                const float x = U(57.5f, 63.5f), z = U(39.5f, 48.5f);
                g_h.human->m_position = {x, U(0.9f, 2.0f), z, 1.0f};
                PlaceBody();
            } else if (P(0.7f)) {
                // Over the sloping floor (1 at x 58 to 2.4 at x 63; the surface at 2.6): every depth.
                const float x = U(58.0f, 63.0f), z = U(49.5f, 55.5f);
                g_h.human->m_position = {x, 1.0f + 1.4f * (x - 58.0f) / 5.0f + U(-0.05f, 1.0f), z, 1.0f};
                PlaceBody();
            }
            if (P(0.4f))
                w->m_mode = WORK_MODE_SWIMMING;
            if (P(0.1f))
                w->m_request.stance = 3;
            DiffMove("UpdateWater", [&] { return int(s.UpdateWater()); },
                     [&](void *mv) { return int(OriginalAt<T_SBool>(0x0074eee0)(SpecialOf(mv))); });
            break;
        }
        }
    }
}

// The ladders: getting on at the foot or the top, the two tests of it.
void TestSpecialLadders()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanSpecialMoves &s = m->m_specialMoves;
    S_ContactActor *ladder = Ladder();
    const C_Ladder *portLadder = reinterpret_cast<const C_Ladder *>(ladder->port);
    std::printf("special: ladders\n");
    C_Ladder *placed = reinterpret_cast<C_Ladder *>(ladder->port);
    const S_vector4 top0 = placed->m_top, bottom0 = placed->m_bottom, dir0 = placed->m_dir;
    const auto placeLadder = [&](const S_vector4 &t, const S_vector4 &b, const S_vector4 &d) {
        placed->m_top = t;
        placed->m_bottom = b;
        placed->m_dir = d;
        std::memcpy(ladder->orig + 0xA0, &t, 16);
        std::memcpy(ladder->orig + 0xB0, &b, 16);
        std::memcpy(ladder->orig + 0xD0, &d, 16);
    };
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomSpecial();
        // The ladder where it stands (in the open), or now and then against the 3 m platform's face (x 24): its
        // ends in or by the wall, so that the tests of getting on meet it.
        if (P(0.35f)) {
            const S_vector4 t = {24.0f - U(-0.2f, 0.4f), 4.0f + U(-0.1f, 0.2f), U(30.5f, 35.5f), 1.0f};
            const S_vector4 b = {t.x + U(-0.4f, 0.6f), 1.0f, t.z + U(-0.3f, 0.3f), 1.0f};
            const S_vector d = S_vector(1.0f, 0.0f, U(-0.2f, 0.2f)).Normalized();
            placeLadder(t, b, {d.x, 0.0f, d.z, 0.0f});
        } else {
            placeLadder(top0, bottom0, dir0);
        }
        const S_vector4 &top = portLadder->m_top, &bottom = portLadder->m_bottom;
        const S_vector4 &dir = portLadder->m_dir;
        m->m_useObject = reinterpret_cast<C_Actor *>(ladder->port);
        m->m_useState = P(0.85f) ? MOVE_NO_TIME : I(0, 2);
        if (P(0.5f)) {
            // At the foot, a little before it.
            g_h.human->m_position = {bottom.x + U(-0.3f, 0.3f), bottom.y, bottom.z - U(0.0f, 0.6f), 1.0f};
        } else {
            // At the top (in the air: nothing there to stand on), above it or a little below.
            g_h.human->m_position = {top.x + U(-0.3f, 0.3f), top.y + (P(0.7f) ? U(-0.2f, 0.4f) : U(-1.5f, -0.3f)),
                                     top.z + U(-0.1f, 0.8f), 1.0f};
        }
        if (P(0.7f)) {
            const float turn = U(-0.6f, 0.6f);
            const S_vector f = S_vector(dir.x + turn, 0.0f, dir.z).Normalized() * (P(0.8f) ? 1.0f : -1.0f);
            g_h.human->m_direction = {f.x, 0.0f, f.z, 0.0f};
        }
        PlaceBody();
        if (P(0.1f))
            m->m_animState.carryAnim = I(0x50, 0x52);
        if (P(0.2f))
            g_h.work->m_mode = WORK_MODE_SWIMMING;          // (onto a ladder out of the water)
        const S_vector4 ladderDir = dir;
        switch (i % 3) {
        case 0:
            DiffMove("UseLadder", [&] { return s.UseLadder(reinterpret_cast<C_Actor *>(ladder->port)); },
                     [&](void *mv) { return OriginalAt<T_SIntPtr>(0x0074b950)(SpecialOf(mv), ladder->orig); });
            break;
        case 1:
            DiffMove("EnterLadderFromTop", [&] { return int(s.EnterLadderFromTop(ladderDir)); },
                     [&](void *mv) { return int(OriginalAt<T_SBoolPtr>(0x0074c040)(SpecialOf(mv), const_cast<S_vector4 *>(&ladderDir))); });
            break;
        default:
            DiffMove("CanEnterLadderFromBottom", [&] { return int(s.CanEnterLadderFromBottom(ladderDir)); },
                     [&](void *mv) { return int(OriginalAt<T_SBoolPtr>(0x0074c640)(SpecialOf(mv), const_cast<S_vector4 *>(&ladderDir))); });
            break;
        }
    }
    placeLadder(top0, bottom0, dir0);
}

// Climbing over: before the obstacles, the steps, the walls and the platforms; standing or not; from a ladder.
void TestSpecialClimb()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanSpecialMoves &s = m->m_specialMoves;
    std::printf("special: climb\n");
    for (int i = 0; i < 16000 / QUICK; ++i) {
        RandomSpecial();
        // (A quarter of the time the blocks under the slabs: the room over an obstacle, crouching or standing.)
        const int last = static_cast<int>(std::size(kObstacles)) - 1;
        FaceObstacle(P(0.3f) ? U(0.0f, 0.2f) : U(0.2f, 1.0f), P(0.25f) ? I(last - 1, last) : -1);
        if (P(0.7f))
            g_h.work->m_request.stance = HUMAN_STANCE_STAND;
        if (P(0.6f))
            m->m_unknown9AC = MOVE_NO_TIME;
        if (P(0.8f)) {
            m->m_unknownA2C = MOVE_NO_TIME;
            m->m_unknownA30 = MOVE_NO_TIME;
        }
        if (P(0.8f) && (m->m_animState.anim.anim >= 8 && m->m_animState.anim.anim <= 0x0E))
            m->m_animState.anim.anim = P(0.5f) ? 0x2D : 0xBE;
        g_h.human->m_velocity.y = P(0.8f) ? U(-0.05f, 0.5f) : U(-3, 0);
        g_h.work->m_request.gait = I(0, 5);
        m->m_unknown740 = {0.0f, U(1.2f, 1.9f), 0.0f, 0.0f};
        PlaceBody();
        DiffMove("TryClimbOver", [&] { return int(s.TryClimbOver(&m->m_nextAnimState)); },
                 [&](void *mv) { return int(OriginalAt<T_SBoolPtr>(0x0074aaf0)(SpecialOf(mv), NextOf(mv))); });
    }
}

// The death: the room to fall, then Die for every kind of hit, stance and seat.
void TestSpecialDeath()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanSpecialMoves &s = m->m_specialMoves;
    std::printf("special: death\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomSpecial();
        C_HumanWork *w = g_h.work;
        if (P(0.4f))
            FaceObstacle(U(0.0f, 2.0f));
        if (P(0.3f))
            m->m_animState.anim.anim = P(0.5f) ? 0x2D : 0xBE;
        PlaceBody();
        if (i % 3 == 0) {
            const float yaw = U(0, 6.2831f);
            const S_vector4 dir = {std::sin(yaw) * U(0.5f, 2.0f), U(-0.3f, 0.3f), std::cos(yaw) * U(0.5f, 2.0f), 0.0f};
            const int side = P(0.6f) ? 0 : I(1, 2);
            DiffMove("HasRoomToFall", [&] { return int(s.HasRoomToFall(dir, side)); },
                     [&](void *mv) { return int(OriginalAt<T_SBoolPtrInt>(0x0074cb40)(SpecialOf(mv), &dir, side)); });
            continue;
        }
        // The hit: its side (one or none), the drowning, the special deaths.
        const uint32_t sides[] = {0, 0x40, 0x80, 0x100, 0x200};
        uint32_t flags = sides[I(0, 4)];
        if (P(0.1f))
            flags |= 0x400;
        if (P(0.08f))
            flags |= 0x800;
        if (P(0.08f))
            flags |= 0x1000;
        if (P(0.08f))
            flags |= 0x2000;
        w->m_request.hitFlags = flags | I(0, 0x3F);
        w->m_request.hitForce = S_vector(U(-5, 5), U(-2, 5), U(-5, 5));
        // The killer (a backstab holds the body before it): none, or the other human (its position at +0x30 on both
        // sides' objects).
        w->m_request.killer = nullptr;
        if (P(0.6f)) {
            for (S_ContactActor *c : g_contacts) {
                if (std::strcmp(c->name, "otherHuman") != 0)
                    continue;
                // (Near the body; the original's copy of the actor, which its killer pointer is mapped to, too.)
                C_Actor *killer = reinterpret_cast<C_Actor *>(c->port);
                killer->m_position = {g_h.human->m_position.x + U(-1, 1), g_h.human->m_position.y, g_h.human->m_position.z + U(-1, 1), 1.0f};
                std::memcpy(c->orig + 0x30, &killer->m_position, 16);
                w->m_request.killer = killer;
            }
        }
        // The work's blocks Die clears; the trail it ends; a slide it may keep (all of it).
        w->m_blocked2 = I(0, 0xFFF);
        w->m_blockedTimer = I(0, 0xFFF);
        m->m_trailTime = I(0, 500);
        m->m_unknown9D0 = {U(-1, 1), U(-1, 1), U(-1, 1), 0.0f};
        // (The model moved since the dummies were placed: Die places them again.)
        if (P(0.5f)) {
            const S_vector at = g_h.frame->GetWorldPos();
            g_h.frame->SetPosInline(S_vector(at.x + U(-0.3f, 0.3f), at.y + U(-0.2f, 0.2f), at.z + U(-0.3f, 0.3f)));
            UpdateTree(g_h.frame);
        }
        m->m_unknown1AC = P(0.85f) ? 1 : I(0, 8);
        m->m_unknown9E0 = P(0.5f) ? 3 : I(0, 2);
        if (P(0.2f))
            w->m_mode = P(0.5f) ? WORK_MODE_SWIMMING : I(2, 3);
        const int anims[] = {0x2D, 0x37, 0x3F, 0xBE, 0x62, 0x9F, 0x9E, 0x9B, 0x6F, 0x22, 0x6A, 0x0B};
        if (P(0.5f))
            m->m_animState.anim.anim = anims[I(0, 11)];
        if (P(0.15f)) {
            // In a seat (a car, or at a static weapon).
            const E_ActorType type = P(0.6f) ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
            reinterpret_cast<C_Actor *>(g_carBytes)->m_type = type;
            *reinterpret_cast<int *>(g_carBytes + 0x18) = type;
            m->m_vehicle = reinterpret_cast<C_Actor *>(g_carBytes);
            g_h.human->m_vehicle = m->m_vehicle;
            m->m_seatMode = P(0.5f) ? 4 : I(1, 8);
            m->m_seat = I(0, 4);
            g_seatFlags = P(0.5f) ? SEAT_FLAG_200 : 0;
            if (m->m_seatMode == 4 && P(0.5f))
                m->m_animState.anim.anim = I(0x37, 0x3E);
        }
        // (Carrying a body: dropped without its actor, as part 4b's OnDropBody(nullptr). A body with its actor goes
        // through the effects module and the carried human's own movement, not part of this harness.)
        g_equipmentModel[9] = nullptr;
        SyncSpecialGlobals();
        DiffMove("Die", [&] { return int(s.Die(&m->m_nextAnimState)); },
                 [&](void *mv) { return int(OriginalAt<T_SBoolPtr>(0x0074d970)(SpecialOf(mv), NextOf(mv))); });
        CheckSpecialGlobals("Die", g_deathSeed, g_lastSeatDeathAnim);
    }
}

// The room for the stance changes: lying down (still or moving), standing up, at a wall, getting up.
void TestSpecialStance()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanSpecialMoves &s = m->m_specialMoves;
    std::printf("special: stance\n");
    for (int i = 0; i < 21000 / QUICK; ++i) {
        RandomSpecial();
        const int mode = I(0, 6);
        if (mode == 4 || mode == 5)
            g_h.work->m_request.stance = HUMAN_STANCE_PRONE;
        if (P(0.5f))
            FaceObstacle(U(0.0f, 1.6f));
        else if (P(mode >= 4 ? 0.6f : 0.3f))
            g_h.human->m_position = {U(9, 19), 1.0f, U(47, 57), 1.0f};   // (under the low roof: room to get up)
        m->m_animState.anim.anim = P(0.4f) ? I(0xBD, 0xCD) : P(0.5f) ? 0x2D : 0x3F;
        m->m_animState.carryAnim = P(0.1f) ? (P(0.5f) ? I(0x50, 0x52) : I(0x23, 0x24)) : 0x5B;
        const float speed = P(0.3f) ? U(0, 1) : U(0, 3.5f);
        const float yaw = U(0, 6.2831f);
        g_h.human->m_velocity = {std::sin(yaw) * speed, 0.0f, std::cos(yaw) * speed, 0.0f};
        m->m_unknown740 = {0.0f, U(1.2f, 1.9f), 0.0f, 0.0f};
        // (Half the time the volumes placed for another stance than the one asked for: the test places its own.)
        const int asked = g_h.work->m_request.stance;
        if (P(0.5f))
            g_h.work->m_request.stance = I(0, 2);
        PlaceBody();
        g_h.work->m_request.stance = asked;
        static const char *const labels[] = {"HasRoomForStance 0", "HasRoomForStance 1", "HasRoomForStance 2", "HasRoomForStance 3",
                                             "HasRoomForStance 4", "HasRoomForStance 5", "HasRoomForStance 6"};
        DiffMove(labels[mode], [&] { return int(s.HasRoomForStance(mode)); },
                 [&](void *mv) { return int(OriginalAt<T_SBoolInt>(0x0074f3c0)(SpecialOf(mv), mode)); });
    }
}

void TestSpecial()
{
    SetUpSpecial();
    TestSpecialSmall();
    TestSpecialLadders();
    TestSpecialClimb();
    TestSpecialDeath();
    TestSpecialStance();
}

}  // namespace
