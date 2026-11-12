// Part 2 of the human harness (included by human_test.cpp after locomotion_test.inc): the locomotion's update, its
// collision volumes, turning, the velocity, the pitch, stance changes and the level of detail, against the original's.
namespace {

typedef void(__attribute__((thiscall)) * T_Int)(void *, int);
typedef bool(__attribute__((thiscall)) * T_BoolInt)(void *, int);
typedef void(__attribute__((thiscall)) * T_Void)(void *);
typedef bool(__attribute__((thiscall)) * T_BoolVoid)(void *);
typedef int(__attribute__((thiscall)) * T_IntVec)(void *, const S_vector *);
typedef void(__attribute__((thiscall)) * T_VecInt)(void *, const S_vector4 *, int);
typedef bool(__attribute__((thiscall)) * T_IntBool)(void *, int, int);

// The animations the velocity and the turns look at (E_HumanAnim).
const int kAnims2[] = {0x2D, 0x37, 0x3F, 0x2B, 0x2C, 0x29, 0x41, 0x12, 0x17, 0x18, 0x1A, 0x1B, 0x1E, 0x1F, 0x20, 0x21, 0x5A, 0x5C,
                       0x6A, 0x6B, 0x6C, 0x08, 0x0A, 0x0E, 0x11, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
                       0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xA2, 0xA5, 0x9F, 0x22, 0x6F};

S_vector RandomFlatDir()
{
    const float a = U(0, 6.2831f);
    return S_vector(std::sin(a), 0.0f, std::cos(a));
}
S_ContactActor *Ladder()
{
    for (S_ContactActor *c : g_contacts)
        if (c->type == ACTOR_LADDER)
            return c;
    return nullptr;
}

// A random human (as part 1's) with the state the turns, the velocity, the pitch and the stance changes read.
void RandomHuman2()
{
    RandomHuman();
    C_Human *h = g_h.human;
    C_HumanMovement *m = g_h.movement;
    C_HumanLocomotion &loco = m->m_locomotion;
    C_HumanWork *w = g_h.work;
    // Facing (sometimes pitched); the roll.
    S_vector facing = RandomFlatDir();
    if (P(0.4f))
        facing.y = P(0.5f) ? U(-0.6f, 0.6f) : U(-0.04f, 0.04f);
    facing.Normalize();
    if (P(0.1f))
        facing = facing * U(0.8f, 1.2f);                // (not of unit length)
    h->m_direction = {facing.x, facing.y, facing.z, 0.0f};
    m->m_unknown98C = P(0.5f);
    m->m_unknown250 = P(0.4f) ? U(-0.5f, 0.5f) : 0.0f;
    // The animations: now and coming; their stance and variant.
    m->m_animState.anim.anim = kAnims2[I(0, static_cast<int>(std::size(kAnims2)) - 1)];
    m->m_animState.anim.stance = I(0, 2);
    m->m_animState.anim.sub = I(0, 2);
    m->m_nextAnimState.anim.anim = P(0.5f) ? m->m_animState.anim.anim : kAnims2[I(0, static_cast<int>(std::size(kAnims2)) - 1)];
    m->m_nextAnimState.anim.stance = I(0, 2);
    m->m_nextAnimState.anim.sub = I(0, 2);
    m->m_animPlayer.m_pendingAnim.anim = P(0.5f) ? I(0xBE, 0xC8) : 0x2D;
    m->m_animPlayer.m_animDelay = P(0.5f) ? I(0, 300) : MOVE_NO_TIME;
    // The work's requests.
    const S_vector asked = RandomFlatDir() * U(0.1f, 3.0f);
    w->m_request.unknown10 = P(0.4f) ? S_vector4{0, 0, 0, 0} : S_vector4{asked.x, P(0.2f) ? U(-1, 1) : 0.0f, asked.z, 0.0f};
    if (P(0.05f))
        w->m_request.unknown10 = {0.0f, U(0.2f, 1.0f), 0.0f, 0.0f};    // (straight up: no length across)
    w->m_request.anim = P(0.3f) ? (P(0.5f) ? 0x2C : 0x2B) : kAnims2[I(0, static_cast<int>(std::size(kAnims2)) - 1)];
    w->m_request.unknown5C = I(0, 2);
    w->m_request.carryMode = I(0, 6);
    w->m_refused = 0;
    w->m_bodyFlags = 0;
    w->m_forced = false;
    // The movement's.
    m->m_request.stance = I(0, 2);
    m->m_unknown940 = {U(-1, 1), U(-0.5f, 0.5f), U(-1, 1), 0.0f};
    m->m_unknown950 = P(0.5f) ? (P(0.1f) ? 1 : I(1, 900)) : I(-100, 0);
    m->m_nextStance2 = I(0, 3);
    m->m_unknown998 = P(0.5f) ? MOVE_NO_TIME : I(1, 500);
    m->m_seatMode = P(0.8f) ? 0 : I(1, 4);
    m->m_interaction = P(0.8f) ? I(0, 1) : I(2, 3);
    m->m_animState.carryAnim = P(0.1f) ? I(0x50, 0x52) : 91;
    m->m_unknownA30 = P(0.2f) ? I(1, 500) : P(0.5f) ? 0 : MOVE_NO_TIME;
    m->m_unknown9B4 = P(0.2f) ? I(0, 300) : MOVE_NO_TIME;
    m->m_stanceChange = P(0.3f) ? I(-3, 300) : MOVE_NO_TIME;
    m->m_unknown9A0 = P(0.3f) ? I(-3, 300) : MOVE_NO_TIME;
    m->m_unknownA1C = P(0.2f) ? I(1, 200) : MOVE_NO_TIME;
    m->m_handsCarryState = I(0, 3);
    m->m_unknown9AC = P(0.2f) ? I(0, 300) : MOVE_NO_TIME;
    m->m_unknown18C = P(0.9f);
    m->m_stanceVolumeScale = 0.8f;
    m->m_hitVolumeCount = I(0, 2);
    for (int i = 0; i < 2; ++i)
        m->m_hitSpheres[i] = {h->m_position.x, h->m_position.y + 0.9f + 0.5f * i, h->m_position.z, 0.3f};
    m->m_hitVolumeScale[0] = U(0.8f, 1.2f);
    m->m_hitVolumeScale[1] = U(0.8f, 1.2f);
    m->m_useObject = P(0.6f) ? reinterpret_cast<C_Actor *>(Ladder()->port) : nullptr;
    // The locomotion's.
    loco.m_swimBack = P(0.3f);
    loco.m_jumpBack = P(0.3f);
    const S_vector target = RandomFlatDir();
    loco.m_turnTarget = P(0.2f) ? S_vector4{MOVE_NO_VALUE, 0, 0, 0} : S_vector4{target.x, 0.0f, target.z, 0.0f};
    if (P(0.1f))
        loco.m_turnTarget = {facing.x, 0.0f, facing.z, 0.0f};          // (already facing it)
    loco.m_turnSpeed = P(0.5f) ? 0.006f : 0.015f;
    loco.m_steadyTurnTime = P(0.5f) ? MOVE_NO_TIME : I(-5, 500);
    loco.m_turning = P(0.5f);
    loco.m_unknown0E4 = P(0.5f);
    loco.m_volumesTime = I(-50, 400);
    loco.m_volumesDir = {facing.x, facing.y, facing.z, 0.0f};
    if (P(0.5f)) {
        const S_vector d = RandomFlatDir();
        loco.m_volumesDir = {d.x, 0, d.z, 0};
    }
    // The frame: at the body or a little away (it lags).
    const S_vector4 &p = h->m_position;
    g_h.frame->SetPosInline(S_vector(p.x + (P(0.5f) ? U(-0.6f, 0.6f) : 0.0f), p.y + (P(0.5f) ? U(-0.3f, 0.3f) : 0.0f), p.z));
    // (SetRot: it clears the local matrix's valid flag, so the matrix is rebuilt from the rotation.)
    S_quat yaw;
    yaw.Make(S_vector(0, 1, 0), U(0, 6.2831f));
    g_h.frame->SetRot(yaw);
    UpdateTree(g_h.frame);
    // On a ladder (now or coming up) there is one, and no hold: the original passes an uninitialised direction without
    // a ladder and an uninitialised vector with a held track (the port defined ones: not comparable).
    const auto onLadder = [](int a) { return a >= 0x6A && a <= 0x6C; };
    if (onLadder(m->m_animState.anim.anim) || onLadder(m->m_nextAnimState.anim.anim)) {
        m->m_useObject = reinterpret_cast<C_Actor *>(Ladder()->port);
        m->m_mixer.m_tracks[0].hold = 0;
    }
    // The answers.
    g_script.activeIsOwner = P(0.3f);
    g_script.hasRoom = P(0.7f);
    g_script.canTurn = P(0.8f);
    // The camera and the frames' render stamps (the level of detail).
    g_h.camera->SetPosInline(S_vector(p.x + U(-60, 60), p.y + U(0, 10), p.z + U(-60, 60)));
    UpdateTree(g_h.camera);
    uint8_t *cam = reinterpret_cast<uint8_t *>(g_h.camera);
    *reinterpret_cast<float *>(cam + 0x120) = U(0.6f, 1.6f);
    *reinterpret_cast<float *>(cam + 0x124) = 0.1f;
    *reinterpret_cast<float *>(cam + 0x128) = U(20, 300);
    *reinterpret_cast<float *>(cam + 0x138) = U(1.0f, 1.8f);
    *reinterpret_cast<int *>(g_sceneBytes + 0x11C) = 77;
    *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(g_h.frame) + 0x11C) = P(0.7f) ? 77 : 76;
    GetGame()->m_missionTime = static_cast<uint32_t>(I(1, 3) * 1000);
}

int Time() { return I(5, 120); }

void TestTurnsAndVelocity()
{
    for (int i = 0; i < 18000 / QUICK; ++i) {
        RandomHuman2();
        C_HumanLocomotion &loco = g_h.movement->m_locomotion;
        const int time = Time();
        switch (i % 9) {
        case 0:
            DiffCall("UpdatePitch", [&] { loco.UpdatePitch(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x00770020)(self, time); return 0; });
            break;
        case 1: {
            const bool replace = P(0.5f);
            DiffCall("UpdateVelocity", [&] { loco.UpdateVelocity(replace); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x0076bad0)(self, replace); return 0; });
            break;
        }
        case 2:
            DiffCall("IsTurnAllowed", [&] { return int(loco.IsTurnAllowed()); },
                     [&](void *self) { return int(OriginalAt<T_BoolVoid>(0x0076d3f0)(self)); });
            break;
        case 3: {
            const S_vector dir = RandomFlatDir() * U(0.2f, 2.0f);
            alignas(16) float padded[4] = {dir.x, dir.y, dir.z, 0.0f};
            DiffCall("GetTurnTime", [&] { return loco.GetTurnTime(dir); },
                     [&](void *self) { return OriginalAt<T_IntVec>(0x0076d290)(self, reinterpret_cast<const S_vector *>(padded)); });
            break;
        }
        case 4: {
            S_vector d = RandomFlatDir() * U(0.2f, 2.0f);
            if (P(0.1f))
                d = S_vector(0, 0, 0);
            if (P(0.15f)) {
                const S_vector4 &f = g_h.human->m_direction;
                d = S_vector(f.x, 0, f.z) + RandomFlatDir() * 0.0001f;   // (nearly facing it)
            }
            const S_vector4 dir = {d.x, U(-0.5f, 0.5f), d.z, 0.0f};
            if (P(0.3f)) {
                // The local player turning in place, within the limit, a turn animation asked: it ends.
                g_h.human->m_role = HUMAN_ROLE_PLAYER;
                g_script.localPlayer = true;
                g_script.serverSimulated = false;
                C_HumanWork *w = g_h.work;
                w->m_request.unknown5C = 1;
                w->m_request.carryMode = I(1, 5);
                w->m_request.stance = I(0, 1);
                w->m_request.anim = P(0.7f) ? (P(0.5f) ? 0x2B : 0x2C) : 0x2D;
                g_h.movement->m_animState.carryAnim = 91;
                g_h.movement->m_animState.anim.anim = P(0.5f) ? 0x2D : 0x37;
                g_h.movement->m_unknown950 = I(-1, 2);
            }
            DiffCall("SetTurnTarget", [&] { loco.SetTurnTarget(dir, time); return 0; },
                     [&](void *self) { OriginalAt<T_VecInt>(0x0076df90)(self, &dir, time); return 0; });
            break;
        }
        case 5: {
            const bool steady = P(0.5f);
            DiffCall("UpdateTurn", [&] { return int(loco.UpdateTurn(time, steady)); },
                     [&](void *self) { return int(OriginalAt<T_IntBool>(0x0076d440)(self, time, steady)); });
            break;
        }
        case 6:
            DiffCall("ChangeStance", [&] { loco.ChangeStance(); return 0; },
                     [&](void *self) { OriginalAt<T_Void>(0x0076b3d0)(self); return 0; });
            break;
        case 7:
            if (P(0.2f))
                g_h.movement->m_stanceVolumeCount = 1;
            DiffCall("UpdateDetail", [&] { loco.UpdateDetail(); return 0; },
                     [&](void *self) { OriginalAt<T_Void>(0x00770560)(self); return 0; });
            break;
        default: {
            const bool enlarged = P(0.5f);
            DiffCall("ScaleStanceVolumes", [&] { loco.ScaleStanceVolumes(enlarged); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x00770990)(self, enlarged); return 0; });
            break;
        }
        }
    }
}

// The collision volumes and the frame (UpdateCollisionVolumes), and the whole update on the ground (Update: the
// climbing, jumping, ladder, swimming and dying moves are part 3's).
void TestUpdates()
{
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomHuman2();
        C_HumanMovement *m = g_h.movement;
        C_HumanLocomotion &loco = m->m_locomotion;
        if (i % 2 == 0) {
            bool remote = P(0.3f);
            g_h.work->m_mode = P(0.2f) ? WORK_MODE_SWIMMING : 0;
            if (P(0.5f)) {
                // A client's view of another soldier, standing or crouched: whether the frame drifted decides.
                g_gameRole = GAME_ROLE_CLIENT;
                g_h.human->m_role = P(0.5f) ? HUMAN_ROLE_ENEMY : HUMAN_ROLE_PLAYER;
                g_script.localPlayer = false;
                g_script.serverSimulated = false;
                g_h.work->m_mode = 0;
                g_h.work->m_request.stance = P(0.5f) ? HUMAN_STANCE_STAND : HUMAN_STANCE_CROUCH;
                m->m_seatMode = 0;
                m->m_unknown1AC = 1;
                m->m_unknown18C = true;
                loco.m_shiftTime = MOVE_NO_TIME;
                remote = false;
                if (P(0.5f))
                    m->m_stillTime = I(-500, 1500);
                if (P(0.5f))
                    m->m_unknown9AC = MOVE_NO_TIME;
                if (P(0.5f))
                    m->m_interaction = I(0, 1);
                if (P(0.5f))
                    g_h.human->m_velocity.y = U(-0.9f, 0.9f);
                if (P(0.4f))
                    m->m_animState.anim.anim = P(0.5f) ? 0xBE : 0x2D;
                if (P(0.4f))
                    loco.m_volumesDir = g_h.human->m_direction;
                // The frame on the body, or off it by a little or a lot along one axis.
                const S_vector4 &p = g_h.human->m_position;
                S_vector at(p.x, p.y, p.z);
                if (P(0.6f))
                    (&at.x)[I(0, 2)] += (P(0.5f) ? 1.0f : -1.0f) * (P(0.5f) ? U(0.0005f, 0.003f) : U(0.4f, 0.7f));
                g_h.frame->SetPosInline(at);
                UpdateTree(g_h.frame);
                if (P(0.5f))
                    PlaceSpheres(g_h.work->m_request.stance);
                // The counters on their edges.
                if (P(0.3f))
                    loco.m_volumesTime = I(0, 2);
                if (P(0.1f))
                    m->m_stillTime = -1000;
                if (P(0.1f))
                    m->m_clock = 5000;
            }
            DiffCall("UpdateCollisionVolumes", [&] { loco.UpdateCollisionVolumes(remote); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x00764320)(self, remote); return 0; });
            continue;
        }
        // On the ground: not climbing, jumping, on a ladder, swimming or dying.
        g_h.work->m_mode = P(0.9f) ? 0 : I(2, 3);
        const int anim = m->m_animState.anim.anim;
        if ((anim >= 0x08 && anim <= 0x0E) || anim == 0x12 || anim == 0x17 || anim == 0x18)
            m->m_animState.anim.anim = 0xBE;
        m->m_useState = MOVE_NO_TIME;
        m->m_unknown1AC = 1;
        // Trajectories: the steps follow (the stance spheres move with the body).
        const float yaw = U(0, 6.2831f), speed = P(0.2f) ? 0.0f : U(0.3f, 5.0f);
        const S_vector velocity(std::sin(yaw) * speed, 0.0f, std::cos(yaw) * speed);
        for (int step = 0; step < 12; ++step) {
            const int time = P(0.05f) ? 0 : Time();
            if (step == 0 && P(0.3f)) {
                // The counters on their edges: running out exactly now, or at -2 (the stance change's "off").
                m->m_stanceChange = P(0.3f) ? -2 : P(0.5f) ? 0 : time;
                m->m_unknown9A0 = P(0.5f) ? 0 : time;
                m->m_unknown9B4 = P(0.5f) ? 0 : time;
                m->m_unknown998 = P(0.5f) ? time : time + 1;
            }
            SetMove(velocity, time);
            DiffCall("Update", [&] { loco.Update(time); return 0; },
                     [&](void *self) { OriginalAt<T_Int>(0x00763c30)(self, time); return 0; });
            PlaceSpheres(loco.m_shiftTime != MOVE_NO_TIME ? loco.m_shiftStance : g_h.work->m_request.stance);
        }
    }
}

void TestLocomotion2()
{
    TestTurnsAndVelocity();
    TestUpdates();
}
}  // namespace
