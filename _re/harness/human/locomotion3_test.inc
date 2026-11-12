// Part 3 of the human harness (included by human_test.cpp after locomotion2_test.inc): climbing, jumping, ladders,
// swimming and its sounds, dying and dead bodies, the resets and the update's whole dispatch, against the original's.
namespace {

typedef void(__attribute__((thiscall)) * T_Vec4Out)(void *, S_vector4 *);
typedef bool(__attribute__((thiscall)) * T_BoolVec4Out)(void *, S_vector4 *);
typedef void(__attribute__((thiscall)) * T_PitchTowards)(void *, const S_vector4 *, int);
typedef void(__attribute__((thiscall)) * T_Init)(void *, void *, void *, void *, void *);

// The frames part 3 reads: three joints of the skeleton, the animation's head dummy (under a root of its own), the
// shadow.
I3D_frame *g_jointF1, *g_jointD1, *g_jointC1, *g_animRoot, *g_headDummy, *g_shadow;

void SetUp3()
{
    const S_quat id = Identity();
    const I3D_bbox small = Box(S_vector(0.05f, 0.05f, 0.05f));
    g_jointF1 = NewFrame(FRAME_DUMMY, "jointF1", S_vector(0, 0, 0), id, small, "", g_sector);
    g_jointD1 = NewFrame(FRAME_DUMMY, "jointD1", S_vector(0, 0, 0), id, small, "", g_sector);
    g_jointC1 = NewFrame(FRAME_DUMMY, "jointC1", S_vector(0, 0, 0), id, small, "", g_sector);
    g_animRoot = NewFrame(FRAME_DUMMY, "animRoot", S_vector(0, 0, 0), id, small, "", g_sector);
    g_headDummy = NewFrame(FRAME_DUMMY, "headDummy", S_vector(0, 1.6f, 0), id, small, "", g_animRoot);
    g_shadow = NewFrame(FRAME_DUMMY, "shadow", S_vector(0, 0, 0), id, small, "", g_sector);
    C_HumanMovement *m = g_h.movement;
    m->m_joints[HUMAN_JOINT_F1].frame = g_jointF1;
    m->m_joints[HUMAN_JOINT_D1].frame = g_jointD1;
    m->m_joints[HUMAN_JOINT_C1].frame = g_jointC1;
    m->m_animPlayer.m_headDummy = g_headDummy;
    m->m_shadow = g_shadow;
    g_sharedWords.push_back({&g_shadow->m_frameFlags, "shadow frame flags"});
    // The ladder faces +z (its climbers stand on its -z side).
    S_ContactActor *ladder = Ladder();
    const S_vector4 dir = {0.196116f, 0.0f, 0.980581f, 0.0f};    // (a little turned: both components read)
    reinterpret_cast<C_Ladder *>(ladder->port)->m_dir = dir;
    std::memcpy(ladder->orig + 0xD0, &dir, 16);
}
void SetLadderFlag180(bool on)
{
    S_ContactActor *ladder = Ladder();
    reinterpret_cast<C_Ladder *>(ladder->port)->m_unknown180 = on;
    ladder->orig[0x180] = on;
}

void PlaceFrame(I3D_frame *frame, const S_vector &pos)
{
    frame->SetPosInline(pos);
    UpdateTree(frame);
}
// The joints about the body: upright, or (dead) lying along the facing.
void PlaceJoints(bool lying)
{
    const S_vector4 &p = g_h.human->m_position;
    const S_vector4 &d = g_h.human->m_direction;
    const S_vector at(p.x, p.y, p.z), flat = S_vector(d.x, 0.0f, d.z).Normalized();
    const S_vector wobble(U(-0.05f, 0.05f), U(-0.05f, 0.05f), U(-0.05f, 0.05f));
    if (lying) {
        PlaceFrame(g_jointF1, at + flat * U(0.6f, 0.9f) + S_vector(0, U(0.1f, 0.4f), 0) + wobble);
        PlaceFrame(g_jointC1, at + flat * U(0.3f, 0.5f) + S_vector(0, U(0.1f, 0.3f), 0));
        PlaceFrame(g_jointD1, at + S_vector(0, 0.2f, 0) + wobble);
    } else {
        PlaceFrame(g_jointF1, at + S_vector(0, 1.6f, 0) + wobble);
        PlaceFrame(g_jointC1, at + S_vector(0, 1.45f, 0) + wobble);
        PlaceFrame(g_jointD1, at + S_vector(0, 1.0f, 0) + wobble);
    }
}

// A random human (as part 2's) with the state part 3 reads.
void RandomHuman3()
{
    RandomHuman2();
    C_HumanMovement *m = g_h.movement;
    C_HumanLocomotion &loco = m->m_locomotion;
    C_HumanAnimPlayer &player = m->m_animPlayer;
    const S_vector4 &p = g_h.human->m_position;
    PlaceJoints(P(0.5f));
    // Climbing: the head dummy of the animation and the rise over its time.
    // (SetRot: it clears the local matrix's valid flag, so the matrix is rebuilt from the rotation.)
    g_animRoot->SetPosInline(S_vector(p.x + U(-2, 2), p.y + U(-1, 1), p.z + U(-2, 2)));
    S_quat yaw;
    yaw.Make(S_vector(0, 1, 0), U(0, 6.2831f));
    g_animRoot->SetRot(yaw);
    UpdateTree(g_animRoot);
    const S_vector head(U(-0.3f, 0.3f), U(0.9f, 1.8f), U(-0.3f, 0.3f));
    PlaceFrame(g_headDummy, head);
    player.m_headDummy = P(0.05f) ? nullptr : g_headDummy;
    player.m_unknown70 = {head.x + U(-0.1f, 0.1f), head.y + U(-0.15f, 0.15f), head.z + U(-0.1f, 0.1f), 1.0f};
    player.m_unknown5C = I(0, 1500);
    player.m_unknown60 = P(0.4f) ? MOVE_NO_TIME : I(0, 1500);
    player.m_unknown90 = {U(-0.5f, 0.5f), U(0, 1.0f), U(-0.5f, 0.5f), 0.0f};
    m->m_unknown958 = U(0.5f, 2.5f);
    m->m_unknown95C = P(0.3f) ? 0.0f : U(0.0f, 0.5f);
    m->m_unknown960 = P(0.3f) ? 0.0f : U(0, 600);
    m->m_unknown964 = m->m_unknown960 + U(1, 1500);
    m->m_unknown968 = static_cast<float>(I(-200, 2200));
    // Water.
    loco.m_waterHeight = P(0.1f) ? MOVE_NO_VALUE : U(-0.2f, 2.0f);
    loco.m_waterDepth = U(0.0f, 3.0f);
    loco.m_waterTransition = P(0.6f) ? 0 : I(-300, 1200);
    m->m_swimBobPhase = P(0.4f) ? 0.0f : U(-3.1f, 3.1f);
    m->m_unknown940 = {U(-1, 1), U(-0.7f, 0.7f), U(-1, 1), 0.0f};
    if (P(0.1f))
        m->m_unknown940 = {0, 0, 0, 0};
    // Dying and dead.
    m->m_unknown1AC = I(0, 9);
    m->m_unknown9B8 = I(-10, 400);
    m->m_unknown9BC = I(-10, 400);
    m->m_unknown9C0 = P(0.2f) ? MOVE_NO_TIME : I(-10, 400);
    m->m_unknown9C4 = I(-10, 400);
    m->m_unknown9C8 = P(0.8f);
    m->m_unknown9CC = P(0.4f) ? MOVE_NO_TIME : I(-10, 1600);
    m->m_unknown9D0 = {P(0.1f) ? -10000.0f : 0.0f, 0, 0, 0};
    m->m_unknown9F0 = {U(-1, 1), U(-1, 1), P(0.7f) ? MOVE_NO_VALUE : 0.0f, 0.0f};
    m->m_unknownA10 = P(0.4f) ? 0x40 : 0;
    m->m_unknownA14 = I(-1, 1);
    m->m_unknownA18 = P(0.5f) ? 0 : I(0, 100);
    m->m_shadow = P(0.1f) ? nullptr : g_shadow;
    loco.m_deadSlideTime = I(0, 150);
    loco.m_jumpTime = I(-20, 200);
    // The swimming sound.
    loco.m_swimSound = P(0.5f) ? static_cast<I3D_sound *>(static_cast<void *>(&g_sound)) : nullptr;
    loco.m_swimSoundFade = P(0.3f) ? MOVE_NO_TIME : I(-600, 600);
    loco.m_swimSoundLeft = I(-10, 900);
    loco.m_swimSoundKind = I(0, 1);
    g_script.headInWater = P(0.6f);
    g_script.headUnderWater = P(0.4f);
    g_script.headSurface = p.y + U(0.5f, 2.0f);
    g_script.cameraUnderWater = P(0.3f);
    g_script.soundPlays = P(0.85f);
    g_script.soundLength = P(0.2f) ? 0 : I(1, 2000);
    g_script.updateWater = P(0.6f);
    SetLadderFlag180(P(0.5f));
    if (g_sound.vt == nullptr)
        FakeSound();
    // What the resets set (the saved games carry it too).
    loco.m_unknown000 = {U(-1, 1), U(-1, 1), U(-1, 1), 0.0f};
    loco.m_unknown030 = {U(-1, 1), U(-1, 1), U(-1, 1), 0.0f};
    loco.m_unknown058 = U(0, 2);
    loco.m_unknown0DC = I(-5, 5);
    loco.m_unknown0E0 = I(-5, 5);
    m->m_unknown764 = I(-1, 3);
    m->m_mixer.m_tracks[1].hold = P(0.3f) ? 1 : 0;
    // The counters on their edges.
    if (P(0.2f))
        loco.m_swimSoundFade = I(-1, 1);
    if (P(0.1f))
        loco.m_swimSoundFade = I(499, 501);
    if (P(0.1f))
        loco.m_swimSoundLeft = I(0, 2);
    // (The shadow's flag set by the death not left over from earlier calls.)
    g_shadow->m_frameFlags &= ~0x80u;
}

// The death animations (to the sides, on the back) and others.
const int kDeathAnims[] = {0x80, 0x81, 0x82, 0x8A, 0x8B, 0x8C, 0x85, 0x2D, 0x3F};

void TestClimbingAndJumping()
{
    for (int i = 0; i < 4000 / QUICK; ++i) {
        RandomHuman3();
        C_HumanMovement *m = g_h.movement;
        C_HumanLocomotion &loco = m->m_locomotion;
        const int time = P(0.05f) ? 0 : Time();
        SetMove(S_vector(U(-1, 1), U(-0.5f, 1.5f), U(-1, 1)), time);
        if (i % 2 == 0) {
            m->m_unknown1AC = P(0.8f) ? 1 : I(2, 9);
            DiffCall("MoveClimbing", [&] { loco.MoveClimbing(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x007681a0)(self, time); return 0; });
        } else {
            // (On the ground, as MoveOnGround's tests.)
            m->m_unknown1AC = 1;
            m->m_animState.anim.anim = P(0.5f) ? 0x18 : P(0.5f) ? 0x17 : 0x12;
            m->m_useObject = nullptr;
            m->m_mixer.m_tracks[0].hold = 0;
            if (P(0.5f))
                loco.m_jumpTime = P(0.5f) ? I(-5, 2) : I(0x4E, 0x52);
            DiffCall("MoveJumping", [&] { loco.MoveJumping(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x007685d0)(self, time); return 0; });
        }
    }
}

// On the ladder (or getting on it): up, down, sliding, idle; at its foot, its middle, its top.
void TestLadders()
{
    S_ContactActor *ladder = Ladder();
    const C_Ladder *portLadder = reinterpret_cast<const C_Ladder *>(ladder->port);
    for (int i = 0; i < 4000 / QUICK; ++i) {
        RandomHuman3();
        C_HumanMovement *m = g_h.movement;
        C_HumanLocomotion &loco = m->m_locomotion;
        C_Human *h = g_h.human;
        const S_vector4 &top = portLadder->m_top, &bottom = portLadder->m_bottom;
        const float t = P(0.2f) ? (P(0.5f) ? U(-0.1f, 0.05f) : U(0.85f, 1.1f)) : U(0, 1);
        // (Mostly at the ladder; else anywhere in the world, where the climb's sphere tests meet things.)
        const bool away = P(0.25f);
        if (away && P(0.4f))
            h->m_position = {U(11, 17), 1.0f + U(0, 0.3f), U(49, 55), 1.0f};   // (under the low roof)
        if (!away)
            h->m_position = {bottom.x + (top.x - bottom.x) * t + U(-0.2f, 0.2f), bottom.y + (top.y - bottom.y) * t,
                             bottom.z + (top.z - bottom.z) * t - 0.17f + U(-0.2f, 0.2f), 1.0f};
        if (P(0.7f))
            h->m_direction = {U(-0.1f, 0.1f), 0.0f, 1.0f, 0.0f};
        m->m_unknown1AC = 1;
        m->m_useObject = reinterpret_cast<C_Actor *>(ladder->port);
        m->m_useState = 0;
        const int anims[] = {0x6A, 0x6A, 0x6B, 0x6C, 0xCE, 0x2D, 0xBE, 0x37};
        m->m_animState.anim.anim = anims[I(0, 7)];
        m->m_nextAnimState.anim.anim = P(0.5f) ? m->m_animState.anim.anim : anims[I(0, 7)];
        const auto onLadder = [](int a) { return a >= 0x6A && a <= 0x6C; };
        if (onLadder(m->m_animState.anim.anim) || onLadder(m->m_nextAnimState.anim.anim))
            m->m_mixer.m_tracks[0].hold = 0;
        else
            m->m_mixer.m_tracks[0].hold = P(0.5f) ? 1 : 0;
        g_h.work->m_request.anim = P(0.5f) ? anims[I(0, 4)] : 0x2D;
        g_script.tryClimb = P(0.5f);
        PlaceSpheres(g_h.work->m_request.stance);
        const int time = Time();
        const float speed = U(0.2f, away ? 6.0f : 1.5f) * (P(0.5f) ? 1.0f : -1.0f);
        SetMove(S_vector(U(-0.1f, 0.1f), P(0.8f) ? speed : 0.0f, U(-0.1f, 0.1f)), time);
        if (P(0.12f)) {
            // Climbing into something: down onto the floor, or up into the low roof (the touch of the sphere tests).
            const bool up = P(0.5f);
            h->m_position = up ? S_vector4{U(11, 17), 1.0f + U(0, 0.3f), U(49, 55), 1.0f}
                               : S_vector4{U(30, 38), 1.0f + U(0.0f, 0.2f), U(20, 28), 1.0f};
            m->m_animState.anim.anim = up ? 0x6A : (P(0.5f) ? 0x6B : 0x6C);
            m->m_nextAnimState.anim.anim = m->m_animState.anim.anim;
            m->m_mixer.m_tracks[0].hold = 0;
            g_h.work->m_request.stance = HUMAN_STANCE_STAND;
            PlaceSpheres(HUMAN_STANCE_STAND);
            m->m_mixer.m_move = {U(-0.05f, 0.05f), up ? U(0.3f, 1.2f) : -U(0.2f, 0.6f), U(-0.05f, 0.05f), 0.0f};
        }
        if (i % 4 == 3) {
            if (P(0.15f))
                m->m_useObject = nullptr;               // (no ladder: the body's own position)
            // (Not zero: a call that clears it must be seen to.)
            alignas(16) S_vector4 outPort = {7, 7, 7, 0}, outOrig = {7, 7, 7, 0};
            DiffCall("GetLadderPos", [&] { loco.GetLadderPos(&outPort); return 0; },
                     [&](void *self) { OriginalAt<T_Vec4Out>(0x00770e20)(self, &outOrig); return 0; });
            CHECK(std::fabs(outPort.x - outOrig.x) <= 2e-4f && std::fabs(outPort.y - outOrig.y) <= 2e-4f &&
                  std::fabs(outPort.z - outOrig.z) <= 2e-4f);
            continue;
        }
        DiffCall("MoveOnLadder", [&] { loco.MoveOnLadder(time); return 0; },
                 [&](void *self) { OriginalAt<T_Int>(0x00768690)(self, time); return 0; });
    }
}

// Swimming: the moves at and under the surface, the pitch, getting in and out.
void TestSwimming()
{
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomHuman3();
        C_HumanMovement *m = g_h.movement;
        C_HumanLocomotion &loco = m->m_locomotion;
        const int time = P(0.03f) ? 0 : Time();
        m->m_unknown1AC = P(0.8f) ? 1 : I(2, 9);
        g_h.work->m_mode = WORK_MODE_SWIMMING;
        PlaceSpheres(HUMAN_STANCE_PRONE);
        if (P(0.03f))
            m->m_stanceVolumeCount = 0;                 // (no stance volumes yet)
        SetMove(P(0.2f) ? S_vector(0, 0, 0) : S_vector(U(-2, 2), U(-1, 1), U(-2, 2)), time);
        switch (i % 4) {
        case 0:
        case 1:
            DiffCall("MoveSwimming", [&] { loco.MoveSwimming(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x007692b0)(self, time); return 0; });
            break;
        case 2: {
            S_vector4 target = {U(-1, 1), U(-1, 1), U(-1, 1), 0.0f};
            if (P(0.1f))
                target = {0, 0, 0, 0};
            if (P(0.1f))
                target = g_h.human->m_direction;
            DiffCall("PitchTowards", [&] { loco.PitchTowards(target, time); return 0; },
                     [&](void *self) { OriginalAt<T_PitchTowards>(0x00769950)(self, &target, time); return 0; });
            break;
        }
        default:
            if (P(0.5f)) {
                const int anims[] = {0xBE, 0xC6, 0xBF, 0xC7, 0x2D, 0x37};
                m->m_animState.anim.anim = anims[I(0, 5)];
                DiffCall("StartSwimming", [&] { loco.StartSwimming(); return 0; },
                         [&](void *self) { OriginalAt<T_Void>(0x0076b0e0)(self); return 0; });
            } else {
                const bool putAway = P(0.5f);
                g_h.work->m_request.stance = I(0, 3);
                DiffCall("StopSwimming", [&] { loco.StopSwimming(putAway); return 0; },
                         [&](void *self) { OriginalAt<T_Int>(0x0076b2d0)(self, putAway); return 0; });
            }
            break;
        }
    }
}

// Dying and dead bodies: the death's states, sliding off edges, settling, the blood's place.
void TestDying()
{
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomHuman3();
        C_HumanMovement *m = g_h.movement;
        C_HumanLocomotion &loco = m->m_locomotion;
        m->m_animState.anim.anim = kDeathAnims[I(0, static_cast<int>(std::size(kDeathAnims)) - 1)];
        if (P(0.6f))
            m->m_unknown1AC = P(0.5f) ? I(7, 9) : I(0, 6);
        if (P(0.3f)) {
            // On a ramp (the body rolls with the slope).
            const float zs[] = {4.5f, 10.5f, 16.5f, 22.5f};
            const float x = U(4.5f, 9.5f), z = zs[I(0, 3)] + U(0, 3);
            g_h.human->m_position = {x, GroundAt(x, z), z, 1.0f};
        }
        PlaceJoints(P(0.8f));
        PlaceSpheres(HUMAN_STANCE_PRONE);
        const int time = P(0.03f) ? 0 : Time();
        SetMove(P(0.3f) ? S_vector(0, 0, 0) : S_vector(U(-2, 2), U(-2, 0.5f), U(-2, 2)), time);
        switch (i % 4) {
        case 0:
            DiffCall("MoveDying", [&] { loco.MoveDying(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x00769d80)(self, time); return 0; });
            break;
        case 1:
            DiffCall("MoveDeadBody", [&] { loco.MoveDeadBody(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x0076a770)(self, time); return 0; });
            break;
        case 2:
            DiffCall("AlignDeadBody", [&] { loco.AlignDeadBody(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x0076e380)(self, time); return 0; });
            break;
        default: {
            // (Not zero: a call that clears it must be seen to.)
            alignas(16) S_vector4 outPort = {7, 7, 7, 0}, outOrig = {7, 7, 7, 0};
            DiffCall("GetSlideOff", [&] { return int(loco.GetSlideOff(&outPort)); },
                     [&](void *self) { return int(OriginalAt<T_BoolVec4Out>(0x0076a2a0)(self, &outOrig)); });
            CHECK(std::fabs(outPort.x - outOrig.x) <= 2e-4f && std::fabs(outPort.y - outOrig.y) <= 2e-4f &&
                  std::fabs(outPort.z - outOrig.z) <= 2e-4f);
            break;
        }
        }
    }
}

// The swimming sound: starting, fading in and out, renewing, stopping; the resets.
void TestSwimSounds()
{
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomHuman3();
        C_HumanLocomotion &loco = g_h.movement->m_locomotion;
        const int time = Time();
        switch (i % 6) {
        case 0:
        case 1:
            DiffCall("UpdateSwimSound", [&] { loco.UpdateSwimSound(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x00770bd0)(self, time); return 0; });
            break;
        case 2:
            DiffCall("StartSwimSound", [&] { loco.StartSwimSound(); return 0; },
                     [&](void *self) { OriginalAt<T_Void>(0x00770a40)(self); return 0; });
            break;
        case 3: {
            const bool now = P(0.5f);
            DiffCall("StopSwimSound", [&] { loco.StopSwimSound(now); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x00770b50)(self, now); return 0; });
            break;
        }
        case 4:
            if (P(0.3f)) {
                // One of the player's squad high up (a parachute over 20 m above the ground).
                g_h.human->m_role = HUMAN_ROLE_PLAYER;
                g_script.serverSimulated = false;
                const S_vector4 &p = g_h.human->m_position;
                PlaceFrame(g_h.frame, S_vector(p.x, p.y + U(5.0f, 40.0f), p.z));
            }
            if (i % 12 == 4) {
                C_HumanMovement *m = g_h.movement;
                // (Its links cleared: Init sets them.)
                loco.m_owner = nullptr;
                loco.m_movement = nullptr;
                loco.m_specialMoves = nullptr;
                loco.m_animPlayer = nullptr;
                DiffCall("Init", [&] { loco.Init(m, &m->m_specialMoves, &m->m_animPlayer, g_h.human); return 0; },
                         [&](void *self) {
                             OriginalAt<T_Init>(0x00763be0)(self, o_movement, o_movement + 0xAA0, o_movement + 0xC00, o_human);
                             return 0;
                         });
            } else if (P(0.5f))
                DiffCall("ResetState", [&] { loco.ResetState(); return 0; },
                         [&](void *self) { OriginalAt<T_Void>(0x00763950)(self); return 0; });
            else
                DiffCall("Reset", [&] { loco.Reset(); return 0; },
                         [&](void *self) { OriginalAt<T_Void>(0x007724d0)(self); return 0; });
            break;
        default:
            DiffCall("Done", [&] { loco.Done(); return 0; },
                     [&](void *self) { OriginalAt<T_Void>(0x00763c20)(self); return 0; });
            break;
        }
    }
}

// The whole update, every way it goes: swimming, climbing, jumping, on the ground, on a ladder, dying, seated.
void TestUpdates3()
{
    for (int i = 0; i < 3000 / QUICK; ++i) {
        RandomHuman3();
        C_HumanMovement *m = g_h.movement;
        C_HumanLocomotion &loco = m->m_locomotion;
        const int way = I(0, 6);
        m->m_unknown1AC = way == 5 ? I(0, 9) : 1;
        g_h.work->m_mode = way == 0 ? WORK_MODE_SWIMMING : P(0.1f) ? I(2, 3) : 0;
        const int climbs[] = {0x08, 0x09, 0x0A, 0x0C, 0x0E};
        if (way == 1)
            m->m_animState.anim.anim = climbs[I(0, 4)];
        else if (way == 2)
            m->m_animState.anim.anim = P(0.5f) ? 0x18 : P(0.5f) ? 0x17 : 0x12;
        else if ((m->m_animState.anim.anim >= 0x08 && m->m_animState.anim.anim <= 0x0E) || m->m_animState.anim.anim == 0x12 ||
                 m->m_animState.anim.anim == 0x17 || m->m_animState.anim.anim == 0x18)
            m->m_animState.anim.anim = 0xBE;
        m->m_useState = way == 4 ? 0 : MOVE_NO_TIME;
        m->m_ground = P(0.5f) ? &g_truck : nullptr;
        m->m_damageMaterial = I(-1, 3);
        if (way == 4) {
            m->m_useObject = reinterpret_cast<C_Actor *>(Ladder()->port);
            m->m_mixer.m_tracks[0].hold = 0;
        }
        if (way == 1 && P(0.5f)) {
            m->m_nextAnimState.anim.anim = 0xBE;          // (no climbing coming: UpdateVelocity's own source)
        }
        PlaceSpheres(way == 0 ? HUMAN_STANCE_PRONE : g_h.work->m_request.stance);
        const int time = P(0.05f) ? 0 : Time();
        SetMove(S_vector(U(-2, 2), U(-0.5f, 0.5f), U(-2, 2)), time);
        DiffCall("Update (all moves)", [&] { loco.Update(time); return 0; },
                 [&](void *self) { OriginalAt<T_Int>(0x00763c30)(self, time); return 0; });
    }
}

// Saved games: the port's and the original's SaveGame write the same bytes; LoadGame reads them (and files made up
// of the locomotion's chunks in any order, unknown ones, another chunk) into the same state.
typedef bool(__attribute__((thiscall)) * T_Chunk)(void *, C_chunk *);
std::vector<uint8_t> FileBytes(const std::string &path)
{
    std::vector<uint8_t> bytes;
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
        int c;
        while ((c = std::fgetc(f)) != EOF)
            bytes.push_back(static_cast<uint8_t>(c));
        std::fclose(f);
    }
    return bytes;
}
// A made-up saved locomotion: its chunks (with their own sizes: the original's int reads take an undefined value
// from a short one) in a random order and subset, unknown ones among them.
void WriteMadeUp(const std::string &path)
{
    static const uint16_t vectors[] = {0x01, 0x02, 0x0C, 0x0F, 0x1B, 0x20, 0x24};
    C_chunk c;
    c.Open(path.c_str(), true);
    c.Begin(P(0.1f) ? 0xDF : 0xDE);
    for (int n = I(0, 30); n > 0; --n) {
        const uint16_t id = static_cast<uint16_t>(P(0.1f) ? I(0x30, 0x40) : I(0x01, 0x2F));
        const bool vector = std::find(std::begin(vectors), std::end(vectors), id) != std::end(vectors);
        float v[3] = {U(-50, 50), U(-50, 50), U(-50, 50)};
        if (!vector && P(0.5f)) {
            const int k = I(-20000, 20000);
            std::memcpy(v, &k, 4);
        }
        c.WriteChunk(id, v, vector ? 12 : 4);
    }
    c.Close();
}
void TestSaveLoad()
{
    const std::string portFile = TempFile("loco_port.chk"), origFile = TempFile("loco_orig.chk"), madeUp = TempFile("loco_madeup.chk");
    for (int i = 0; i < 2000 / QUICK; ++i) {
        RandomHuman3();
        C_HumanLocomotion &loco = g_h.movement->m_locomotion;
        const bool open = P(0.95f);
        std::remove(portFile.c_str());
        std::remove(origFile.c_str());
        DiffCall("SaveGame",
                 [&] {
                     C_chunk c;
                     if (open)
                         c.Open(portFile.c_str(), true);
                     return int(loco.SaveGame(c));
                 },
                 [&](void *self) {
                     C_chunk c;
                     if (open)
                         c.Open(origFile.c_str(), true);
                     return int(OriginalAt<T_Chunk>(0x00771570)(self, &c));
                 });
        CHECK(FileBytes(portFile) == FileBytes(origFile));
        CHECK(!open || FileBytes(portFile).size() > 300);
        // Loaded back into another state; or a made-up one; or nothing open.
        const bool madeUpOne = P(0.4f);
        if (madeUpOne)
            WriteMadeUp(madeUp);
        const std::string &from = madeUpOne ? madeUp : portFile;
        const bool loadOpen = P(0.97f);
        RandomHuman3();
        DiffCall("LoadGame",
                 [&] {
                     C_chunk c;
                     if (loadOpen)
                         c.Open(from.c_str(), false);
                     return int(loco.LoadGame(c));
                 },
                 [&](void *self) {
                     C_chunk c;
                     if (loadOpen)
                         c.Open(from.c_str(), false);
                     return int(OriginalAt<T_Chunk>(0x00771b00)(self, &c));
                 });
    }
}

void TestLocomotion3()
{
    SetUp3();
    TestClimbingAndJumping();
    TestLadders();
    TestSwimming();
    TestDying();
    TestSwimSounds();
    TestUpdates3();
    TestSaveLoad();
}
}  // namespace
