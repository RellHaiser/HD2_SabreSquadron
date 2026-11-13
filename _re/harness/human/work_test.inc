// Part 14 of the human harness (HUMAN_WORK builds, build_work.bat; included by human_test.cpp after morph_test.inc):
// the work (game/human_work.cpp: the requests to the body and their blocks, the stance timer, the update that passes
// the requests on to the movement, the saved games) against the original's. Everything under it is real on both
// sides (the movement, the mixer, the player, the special moves, the idle, the visual, the lighting, the morph).

// What the work calls besides part 1-13's fakes: logged on both sides.
namespace {
struct S_WorkScript {
    int projectileKind = 0;                             // C_HumanEquipment::GetHandsProjectileKind
} g_workScript;
}  // namespace

void C_HumanWeaponPose::SetViewDir(const S_vector &dir) { LogV(this, "PoseSetViewDir", &dir.x, 3); }
void C_HumanWeaponPose::OnHit(uint32_t flags, const S_vector &pos, const S_vector &force)
{
    const float v[6] = {pos.x, pos.y, pos.z, force.x, force.y, force.z};
    LogV(this, "PoseOnHit", v, 6, {double(flags)});
}
void C_HumanWeaponPose::SetLook(uint32_t flags, const S_vector4 *pos)
{
    LogV(this, "PoseSetLook", pos ? &pos->x : nullptr, pos ? 4 : 0, {double(flags), double(pos != nullptr)});
}
void C_FirstPersonView::SetPose(int pose) { Log(this, "FpvSetPose", {double(pose)}); }
int C_HumanEquipment::GetHandsProjectileKind()
{
    Log(this, "GetHandsProjectileKind");
    return g_workScript.projectileKind;
}

namespace {
#define WT __attribute__((thiscall))
void WT OW_PoseSetViewDir(void *self, const float *dir) { LogV(self, "PoseSetViewDir", dir, 3); }
void WT OW_PoseOnHit(void *self, uint32_t flags, const float *pos, const float *force)
{
    const float v[6] = {pos[0], pos[1], pos[2], force[0], force[1], force[2]};
    LogV(self, "PoseOnHit", v, 6, {double(flags)});
}
void WT OW_PoseSetLook(void *self, uint32_t flags, const float *pos)
{
    LogV(self, "PoseSetLook", pos, pos ? 4 : 0, {double(flags), double(pos != nullptr)});
}
void WT OW_FpvSetPose(void *self, int pose) { Log(self, "FpvSetPose", {double(pose)}); }
int WT OW_GetHandsProjectileKind(void *self)
{
    Log(self, "GetHandsProjectileKind");
    return g_workScript.projectileKind;
}

typedef void(WT *T_WVoid)(void *);
typedef void(WT *T_WInt)(void *, int);
typedef void(WT *T_WInt2)(void *, int, int);
typedef void(WT *T_WInt3)(void *, int, int, int);
typedef void(WT *T_WPtrInt)(void *, const void *, int);
typedef void(WT *T_WPtr)(void *, const void *);
typedef void(WT *T_WHit)(void *, uint32_t, const void *, const void *);
typedef void(WT *T_WInteract)(void *, void *, uint32_t, int, int);
typedef void(WT *T_WLook)(void *, uint32_t, const void *);
typedef void *(WT *T_WViewDir)(void *, void *);
typedef bool(WT *T_WChunk)(void *, void *);

void SetUpWork()
{
    SetUpMorph();
    // The original's work functions earlier parts redirected to loggers run for real here.
    for (uint32_t at : {0x0077fc10u, 0x00780280u, 0x00780560u, 0x0077e290u, 0x00780140u, 0x0077f4b0u, 0x0077f030u, 0x0077f060u,
                        0x0077f8c0u, 0x0077f6c0u, 0x0077f500u, 0x007800d0u, 0x0077f750u, 0x0077fb70u, 0x0077faf0u})
        RestoreOriginal(at);
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x007b92e0, reinterpret_cast<const void *>(OW_PoseSetViewDir)},
        {0x007b88b0, reinterpret_cast<const void *>(OW_PoseOnHit)},
        {0x007acbe0, reinterpret_cast<const void *>(OW_PoseSetLook)},
        {0x00491640, reinterpret_cast<const void *>(OW_FpvSetPose)},
        {0x00792070, reinterpret_cast<const void *>(OW_GetHandsProjectileKind)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // Undefined in the original, not compared: the w of the facing direction (+0x2C) and the pads after the hit's
    // force and position (+0x7C, +0x8C): the original copies the 4 bytes after the vectors passed to it.
    for (size_t at : {0x2Cu, 0x7Cu, 0x8Cu})
        g_workSkipWords.insert(at);
    // The working frames' references (SetWorkingFrame2 adds one): compared.
    ShareBlock(&g_handsModel->m_refCount, 4, "handsModel refCount");
    ShareBlock(&g_bodyFrame->m_refCount, 4, "bodyFrame refCount");
}

// The work's state: its blocks, mode, timers and the request record; the movement's record of the last update.
void RandomWork()
{
    RandomMorph();
    C_HumanWork *w = g_h.work;
    C_HumanMovement *m = g_h.movement;
    // (The parts' links the update follows: the locomotion's to the special moves (ChangeStance asks them), theirs to
    // the movement. Part 3's Init test leaves the locomotion's cleared.)
    m->m_locomotion.m_movement = m;
    m->m_locomotion.m_owner = g_h.human;
    m->m_locomotion.m_specialMoves = &m->m_specialMoves;
    m->m_locomotion.m_animPlayer = &m->m_animPlayer;
    C_HumanSpecialMoves &s = m->m_specialMoves;
    s.m_movement = m;
    s.m_owner = g_h.human;
    s.m_locomotion = &m->m_locomotion;
    s.m_animPlayer = &m->m_animPlayer;
    s.m_idle = &m->m_idle;
    // (The morph with its visual: a death shows the dead face, and without one the original reads near null, the
    // morph's documented fix; part 13 keeps it out too.)
    m->m_morph.m_morphVisual = g_morphVisual;
    // (The death's seed and last seat death, the same on both sides: an update may die. Unsynced, the port's seed
    // was clock() % 100 at its start and the original's 0, and the dying animations differed with the start-up time.)
    SyncSpecialGlobals();
    const uint32_t bits = 0xFFFFF;
    const auto blocks = [&]() { return P(0.4f) ? 0u : P(0.5f) ? static_cast<uint32_t>(1u << I(0, 19)) : static_cast<uint32_t>(I(0, bits)); };
    w->m_blocked = blocks();
    w->m_blocked2 = blocks();
    w->m_blockedTimer = blocks();
    w->m_flagsD8 = blocks();
    w->m_forced = P(0.2f);
    std::memset(reinterpret_cast<uint8_t *>(&w->m_forced) + 1, 0, 3);
    w->m_mode = P(0.4f) ? 0 : I(0, 6);
    w->m_playCount = I(0, 3);
    w->m_stanceTimer = P(0.5f) ? MOVE_NO_TIME : I(-20, 400);
    w->m_timerStance = I(0, 2);
    w->m_refused = I(0, bits);
    w->m_bodyFlags = I(0, 0x3FF);
    w->m_group = I(1, 2);
    S_MoveRequest &r = w->m_request;
    const int anims[] = {0x2D, 0x2C, 0x2B, 0x36, 0x3E, 0x41, 0x37, 0x3F, 0xBE, 0xBF, 0xC6, 0xC7, 0x12, 0x18, 0x6A, 0x6B, 0x0A, 0x0B,
                         0x0E, 8, 0x22, 0x23, 0x1D, 0x25, 0x26, 0x59, 0x5B, 0x68, 0x80, 0x9A, 0x11, HUMAN_ANIM_NONE};
    r.anim = anims[I(0, static_cast<int>(std::size(anims)) - 1)];
    r.activity = P(0.5f) ? 0x4B : I(0, 0x4C);
    r.stance = I(0, 2);
    r.gait = I(0, 5);
    r.carryMode = P(0.4f) ? 0 : P(0.2f) ? 100 : I(0, 6);
    r.action = P(0.4f) ? 0 : I(0, 0x0E);
    r.unknown5C = I(0, 1);
    r.hitFlags = P(0.6f) ? 0 : static_cast<uint32_t>(I(0, 0x3FF));
    r.interaction = 0;
    r.interactTarget = nullptr;
    r.changed = I(0, bits);
    r.facingDir.y = P(0.5f) ? 0.0f : U(-0.5f, 0.5f);       // (SetFacingDir writes the flat direction's 0)
    // The movement's copy of the record (what the update compares against).
    m->m_request = r;
    if (P(0.5f))
        m->m_request.stance = I(0, 2);
    if (P(0.3f))
        m->m_request.gait = I(0, 5);
    if (P(0.4f))
        m->m_request.carryMode = P(0.4f) ? 0 : I(0, 6);
    // (The record's action: the same as the request's, which the update drops, or another, which it takes.)
    m->m_request.action = P(0.3f) ? r.action : I(0, 0x0E);
    // The pick-up and carrying states the actions read; an interaction to pass on (the ones without a vehicle).
    if (P(0.2f))
        m->m_unknownA2C = P(0.5f) ? 1 : 9;
    if (P(0.2f))
        m->m_unknownA30 = 1;
    if (P(0.1f)) {
        const uint32_t codes[] = {0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1C, 0x1D, 0x24, 0x25};
        S_ContactActor *c = ContactNamed("otherHuman");
        C_Actor *a = reinterpret_cast<C_Actor *>(c->port);
        a->m_position = {g_h.human->m_position.x + U(-2, 2), g_h.human->m_position.y, g_h.human->m_position.z + U(-2, 2), 1.0f};
        std::memcpy(c->orig + 0x30, &a->m_position, 16);
        r.interaction = codes[I(0, 11)] | (P(0.5f) ? 0x400u : 0u);
        r.interactTarget = a;
        r.interactArg = I(0, 3);
    }
    m->m_onGround = P(0.8f);
    m->m_handsCarryState = I(0, 4);
    m->m_animPlayer.m_carryDelay = P(0.7f) ? MOVE_NO_TIME : I(0, 300);
    m->m_locomotion.m_waterTransition = P(0.8f) ? 0 : I(-300, 300);
    g_workScript.projectileKind = I(0, 5);
}

void TestWork()
{
    SetUpWork();
    C_HumanWork *w = g_h.work;
    std::printf("work\n");
    const std::string file = TempFile("work.chk"), file2 = TempFile("work_orig.chk");
    for (int i = 0; i < 60000 / QUICK; ++i) {
        RandomWork();
        const bool on = P(0.5f);
        static const char *const names[] = {"WorkSetGroup", "WorkReset", "WorkUpdate", "SetStanceTimer", "UpdateStanceTimer", "GetViewDir",
                                            "WorkPlay", "PlayWorkAnimation", "WorkSetStance", "SetGait", "WorkSetLookAt", "SetCarryMode",
                                            "DoAction", "SetUnknown5C", "WorkSetFacingDir", "WorkSetViewDir", "SetWorkingFrame",
                                            "SetWorkingFrame2", "WorkApplyHit", "WorkInteract", "WorkSetLook", "WorkSaveGame",
                                            "WorkLoadGame"};
        const int which = i % 23;
        // (Most requests for the animations and stances, the update and the hits more often.)
        const auto orig = [](uint32_t at) { return OriginalAt<void *>(at); };
        switch (which) {
        case 0: {
            const int group = I(0, 3);
            DiffMove(names[which], [&] { w->SetGroup(group); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt>(orig(0x0077e220))(o_work, group); return 0; }); });
            break;
        }
        case 1:
            if (P(0.5f)) {
                DiffMove(names[which], [&] { w->Reset(); return 0; },
                         [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WVoid>(orig(0x0077e280))(o_work); return 0; }); });
            } else {
                // (Its links cleared first: SetMovement sets them.)
                w->m_movement = w->m_movementBase = nullptr;
                w->m_owner = nullptr;
                DiffMove("WorkSetMovement", [&] { w->SetMovement(g_h.movement); return 0; },
                         [&](void *mv) { return IdleOriginal([&] { reinterpret_cast<T_WPtr>(orig(0x0077e200))(o_work, mv); return 0; }); });
                // (Linked again for the next calls, as MakeHuman links it.)
                w->m_movement = w->m_movementBase = g_h.movement;
                w->m_owner = &g_h.movement->m_owner;
            }
            break;
        case 2: {
            const int time = I(0, 200);
            DiffMove(names[which], [&] { w->Update(time); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt>(orig(0x0077e290))(o_work, time); return 0; }); });
            CheckSpecialGlobals("WorkUpdate", g_deathSeed, g_lastSeatDeathAnim);
            break;
        }
        case 3: {
            const int time = P(0.4f) ? MOVE_NO_TIME : I(0, 500), stance = I(0, 2), blocked = I(0, 0xFF);
            DiffMove(names[which], [&] { w->SetStanceTimer(time, stance, blocked); return 0; },
                     [&](void *) {
                         return IdleOriginal([&] { reinterpret_cast<T_WInt3>(orig(0x007800d0))(o_work, time, stance, blocked); return 0; });
                     });
            break;
        }
        case 4: {
            const int time = I(0, 200);
            DiffMove(names[which], [&] { w->UpdateStanceTimer(time); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt>(orig(0x00780140))(o_work, time); return 0; }); });
            break;
        }
        case 5: {
            alignas(16) S_vector4 portView = {}, origView = {};
            DiffMove(names[which], [&] { portView = w->GetViewDir(); return 0; },
                     [&](void *) {
                         return IdleOriginal([&] { reinterpret_cast<T_WViewDir>(orig(0x0077f030))(o_work, &origView); return 0; });
                     });
            CHECK(std::memcmp(&portView, &origView, 16) == 0);
            break;
        }
        case 6: {
            const int anims[] = {0xBE, 0xC6, 0xBF, 0xC7, 0x6A, 0x6B, 8, 0x0E, 0x12, 0x2D, 0x37, 0x1D, 0x25, 0x26, 0x59, 0x5B, 0x68, 0x80,
                                 0xC0, HUMAN_ANIM_NONE};
            const int anim = anims[I(0, static_cast<int>(std::size(anims)) - 1)];
            if (P(0.2f))
                g_h.movement->m_animState.anim.anim = P(0.5f) ? 0x11 : I(0x6A, 0x6C);
            if (P(0.3f))
                w->m_mode = 1;                          // (on a ladder: the moves become climbing)
            g_script.viewDirY = P(0.4f) ? U(-0.5f, 0.5f) : U(-0.3f, 0.3f);
            g_h.movement->m_unknown940 = {U(-1, 1), g_script.viewDirY, U(-1, 1), 0.0f};
            DiffMove(names[which], [&] { w->Play(anim, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt2>(orig(0x0077f060))(o_work, anim, on); return 0; }); });
            break;
        }
        case 7: {
            const int activity = P(0.5f) ? 0x4B : I(0, 0x4C);
            DiffMove(names[which], [&] { w->PlayWorkAnimation(activity, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt2>(orig(0x0077f4b0))(o_work, activity, on); return 0; }); });
            break;
        }
        case 8: {
            const int stance = I(0, 3);
            DiffMove(names[which], [&] { w->SetStance(stance, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt2>(orig(0x0077f500))(o_work, stance, on); return 0; }); });
            break;
        }
        case 9: {
            const int gait = I(0, 6);
            DiffMove(names[which], [&] { w->SetGait(gait, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt2>(orig(0x0077f5f0))(o_work, gait, on); return 0; }); });
            break;
        }
        case 10: {
            S_vector4 target = {U(-1, 1), U(-1, 1), U(-1, 1), U(0, 1)};
            if (P(0.2f))
                target = w->m_request.unknown10;
            DiffMove(names[which], [&] { w->SetLookAt(target, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WPtrInt>(orig(0x0077f6c0))(o_work, &target, on); return 0; }); });
            break;
        }
        case 11: {
            const int mode = P(0.2f) ? 100 : I(0, 6);
            DiffMove(names[which], [&] { w->SetCarryMode(mode, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt2>(orig(0x0077f750))(o_work, mode, on); return 0; }); });
            break;
        }
        case 12: {
            const int action = I(0, 0x0E);
            DiffMove(names[which], [&] { w->DoAction(action, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt2>(orig(0x0077f7e0))(o_work, action, on); return 0; }); });
            break;
        }
        case 13: {
            const bool value = P(0.5f);
            g_script.activeIsOwner = P(0.6f);
            DiffMove(names[which], [&] { w->SetUnknown5C(value, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WInt2>(orig(0x0077f830))(o_work, value, on); return 0; }); });
            break;
        }
        case 14: {
            const float yaw = U(0, 6.2831f);
            S_vector dir(std::sin(yaw) * U(0.5f, 2), U(-0.5f, 0.5f), std::cos(yaw) * U(0.5f, 2));
            if (P(0.2f))
                dir = S_vector(w->m_request.facingDir.x, 0.0f, w->m_request.facingDir.z);
            DiffMove(names[which], [&] { w->SetFacingDir(dir, on); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WPtrInt>(orig(0x0077f8c0))(o_work, &dir.x, on); return 0; }); });
            break;
        }
        case 15: {
            const S_vector dir(U(-1, 1), U(-1, 1), U(-1, 1));
            DiffMove(names[which], [&] { w->SetViewDir(dir); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WPtr>(orig(0x0077faf0))(o_work, &dir.x); return 0; }); });
            break;
        }
        case 16:
        case 17: {
            I3D_frame *frame = P(0.3f) ? w->m_request.workingFrame : P(0.5f) ? g_handsModel : g_bodyFrame;
            if (which == 17 && !frame)
                frame = g_handsModel;
            const uint32_t at = which == 16 ? 0x0077fb70 : 0x0077fbc0;
            DiffMove(names[which],
                     [&] {
                         if (which == 16)
                             w->SetWorkingFrame(frame, on);
                         else
                             w->SetWorkingFrame2(frame, on);
                         return 0;
                     },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WPtrInt>(orig(at))(o_work, frame, on); return 0; }); });
            break;
        }
        case 18: {
            const uint32_t flags = static_cast<uint32_t>(I(0, 0x3F));
            const S_vector pos(U(0, 60), U(1, 3), U(0, 60)), force(U(-5, 5), U(-2, 2), U(-5, 5));
            if (P(0.2f))
                g_h.movement->m_animState.anim.anim = 0x62;
            DiffMove(names[which], [&] { w->ApplyHit(flags, pos, force); return 0; },
                     [&](void *) {
                         return IdleOriginal([&] { reinterpret_cast<T_WHit>(orig(0x0077fc10))(o_work, flags, &pos.x, &force.x); return 0; });
                     });
            break;
        }
        case 19: {
            S_ContactActor *c = ContactNamed("otherHuman");
            const uint32_t interaction = P(0.3f) ? w->m_request.interaction : static_cast<uint32_t>(I(0, 0x25)) | (P(0.5f) ? 0x400u : 0u);
            const int arg = I(0, 4);
            if (P(0.2f))
                g_h.movement->m_animState.anim.anim = I(8, 0x0E);
            DiffMove(names[which], [&] { w->Interact(reinterpret_cast<C_Actor *>(c->port), interaction, arg, on); return 0; },
                     [&](void *) {
                         return IdleOriginal([&] {
                             reinterpret_cast<T_WInteract>(orig(0x00780010))(o_work, c->orig, interaction, arg, on);
                             return 0;
                         });
                     });
            break;
        }
        case 20: {
            const uint32_t flags = static_cast<uint32_t>(I(0, 15));
            const S_vector4 at = {U(-1, 1), U(-1, 1), U(-1, 1), 1.0f};
            const S_vector4 *pos = P(0.3f) ? nullptr : &at;
            DiffMove(names[which], [&] { w->SetLook(flags, pos); return 0; },
                     [&](void *) { return IdleOriginal([&] { reinterpret_cast<T_WLook>(orig(0x00780090))(o_work, flags, pos); return 0; }); });
            break;
        }
        case 21: {
            std::remove(file.c_str());
            std::remove(file2.c_str());
            const bool open = P(0.95f);
            // (The record's three pointers are each side's own objects in the files: none here, so the files compare.)
            w->m_request.killer = nullptr;
            w->m_request.workingFrame = nullptr;
            w->m_request.interactTarget = nullptr;
            DiffMove(names[which],
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), true);
                         return int(w->SaveGame(c));
                     },
                     [&](void *) {
                         return IdleOriginal([&] {
                             C_chunk c;
                             if (open)
                                 c.Open(file2.c_str(), true);
                             return int(reinterpret_cast<T_WChunk>(orig(0x00780280))(o_work, &c));
                         });
                     });
            CHECK(FileBytes(file) == FileBytes(file2));
            CHECK(!open || FileBytes(file).size() > 150);
            break;
        }
        default: {
            // A save of another state loaded back, or made-up chunks.
            std::remove(file.c_str());
            {
                C_chunk c;
                c.Open(file.c_str(), true);
                c.Begin(P(0.1f) ? 0x30A : 0x309);
                if (P(0.7f)) {
                    c.Begin(0x00);
                    S_MoveRequest req = w->m_request;
                    req.killer = nullptr;
                    req.workingFrame = nullptr;
                    req.interactTarget = nullptr;
                    req.stance = I(0, 2);
                    req.anim = I(0, 0xCD);
                    req.SaveGame(c);
                    c.Ascend();
                }
                for (uint16_t id = 1; id <= 0x0C; ++id)
                    if (P(0.7f))
                        c.WriteChunk(id, P(0.3f) ? I(0, 2) : I(-5, 3000));
                c.Ascend();
            }
            const bool open = P(0.95f);
            DiffMove(names[which],
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), false);
                         return int(w->LoadGame(c));
                     },
                     [&](void *) {
                         return IdleOriginal([&] {
                             C_chunk c;
                             if (open)
                                 c.Open(file.c_str(), false);
                             return int(reinterpret_cast<T_WChunk>(orig(0x00780560))(o_work, &c));
                         });
                     });
            break;
        }
        }
    }
}

}  // namespace
