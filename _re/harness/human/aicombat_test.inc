// Part 36 of the human harness (HUMAN_AICOMBAT builds, build_aicombat.bat; included by human_test.cpp after
// aisenses_test.inc, whose fakes and helpers it uses): the AI controllers' fighting (game/ai_combat.cpp) against the
// original's, on part 33's AI controllers and part 35's humans.
//
// Part 36a: the timers, the aim's spread, the hit chance, turning and looking, the alert state, starting and stopping
// to fire, the stance.
//
// Faked on both sides and logged: IsHumanWithin (ai_movement.cpp, step 4's; the original's 0x0057fe40), the human's
// CountAmmo (0x005a6c50), the sight module's CanSee (0x0046f320), the body's GetCarryAnimLength (0x005ac200).

struct S_AiCombatScript {
    bool within = false;                                // IsHumanWithin
    int ammo = 0;                                       // CountAmmo
    int canSee = 0;                                     // CanSee's count
    bool visible = false;                               // its visible flag
    int before = 0;                                     // its *before
    int animLength = 600;                               // GetCarryAnimLength
} g_ac;

bool C_AIController::IsHumanWithin(const S_vector *pos, float radius)
{
    Log(this, "IsHumanWithin", {pos->x, pos->y, pos->z, radius});
    return g_ac.within;
}
int C_Human::CountAmmo(int typeId, bool backpack) const
{
    Log(this, "CountAmmo", {double(typeId), double(backpack)});
    return g_ac.ammo;
}
int C_SightModule::CanSee(int observer, int target, int *before, bool *visible)
{
    Log(nullptr, "CanSee", {double(observer), double(target)});
    *before = g_ac.before;
    *visible = g_ac.visible;
    return g_ac.canSee;
}
int S_HumanBody::GetCarryAnimLength(int id, int variant, int carryMode)
{
    Log(nullptr, "GetCarryAnimLength", {double(id), double(variant), double(carryMode)});
    return g_ac.animLength;
}

namespace {
int AC OA_IsHumanWithin(void *self, const S_vector *pos, float radius)
{
    Log(self, "IsHumanWithin", {pos->x, pos->y, pos->z, radius});
    return g_ac.within;
}
int AC OA_CountAmmo(void *self, int typeId, int backpack)
{
    Log(self, "CountAmmo", {double(typeId), double(backpack & 0xFF)});
    return g_ac.ammo;
}
int AC OA_CanSee(void *, int observer, int target, int *before, bool *visible)
{
    Log(nullptr, "CanSee", {double(observer), double(target)});
    *before = g_ac.before;
    *visible = g_ac.visible;
    return g_ac.canSee;
}
int AC OA_CarryAnimLength(void *, int id, int variant, int carryMode)
{
    Log(nullptr, "GetCarryAnimLength", {double(id), double(variant), double(carryMode)});
    return g_ac.animLength;
}

void SetUpCombatAi()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0057fe40, FAKE(OA_IsHumanWithin)},
        {0x005a6c50, FAKE(OA_CountAmmo)},
        {0x0046f320, FAKE(OA_CanSee)},
        {0x005ac200, FAKE(OA_CarryAnimLength)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // The controls the fighting presses: each side's kept apart and compared.
    ShareBlock(g_axes, sizeof g_axes, "axes");
    ShareBlock(g_buttons, sizeof g_buttons, "buttons");
}

// Access to the AI controller's protected members.
struct S_CombatAi : S_TestAi {
    using C_AIController::ApplyAlertState;
    using C_AIController::ExtendBurst;
    using C_AIController::GetAimSpread;
    using C_AIController::GetHitChance;
    using C_AIController::PressReload;
    using C_AIController::SetStance;
    using C_AIController::StartFiring;
    using C_AIController::StopFiring;
    using C_AIController::TurnTowards;
    using C_AIController::UpdateCheckpointStance;
    using C_AIController::UpdateLook;
    using C_AIController::UpdateTimers;
};
S_CombatAi *CombatAi() { return static_cast<S_CombatAi *>(g_pa); }

// A timer: zero, a little, or anything.
int CombatTimer() { return P(0.2f) ? 0 : P(0.1f) ? 1 : P(0.1f) ? 300 : P(0.5f) ? I(-50, 500) : I(-100000, 100000); }

// Part 35b's humans, then the fighting's state: each human's stance and speed class (the hook), the AI's timers, its
// hands (a weapon of part 23's, a worn item with a definition's carry mode, or none), the walking point it is at.
void RandomCombat()
{
    RandomSensesB();
    C_Human *h = g_h.human;
    for (S_HumanAnswers &a : g_asAnswers) {
        a.body = true;
        a.stance = I(0, 3);
        static const int classes[] = {-1, 0, 1, 2, 3, 5};
        a.speedClass = classes[I(0, 5)];
    }
    S_TestAi *a = g_pa;
    // (An AI controller always has its controls: part 23's weapon tests take them away now and then.)
    a->m_axes = g_axes;
    a->m_buttons = g_buttons;
    for (float &v : g_axes)
        v = P(0.7f) ? 0.0f : 1.0f;
    for (float &v : g_buttons)
        v = P(0.8f) ? 0.0f : 1.0f;
    a->m_unknown120 = CombatTimer();
    a->m_fire.pauseTime = CombatTimer();
    a->m_fire.unknown0C = CombatTimer();
    a->m_fire.burstTime = CombatTimer();
    a->m_lookAroundTime = CombatTimer();
    a->m_unknown110 = CombatTimer();
    a->m_quietTime = P(0.1f) ? 2500 : P(0.5f) ? I(0, 2499) : I(2500, 20000);
    h->m_stats[STAT_SHOOTING] = P(0.2f) ? 100 : I(0, 100);
    const int hands = I(0, 3);
    a->m_handsItem = hands == 0 ? nullptr
                     : hands == 1 ? static_cast<C_Item *>(g_handsWeapon ? g_handsWeapon : g_weaponPort[0])
                                  : reinterpret_cast<C_Item *>(g_wornItems[I(0, 15)]);
    a->m_currentCheckpoint = P(0.3f) ? nullptr : reinterpret_cast<S_Checkpoint *>(g_checkpointBytes[I(0, 7)]);
    for (auto &bytes : g_checkpointBytes) {
        auto *point = reinterpret_cast<S_Checkpoint *>(bytes);
        point->pos = g_h.frame->GetWorldPos() + (P(0.4f) ? S_vector(U(-1, 1), 0, U(-1, 1)) : S_vector(U(-5, 5), U(-1, 1), U(-5, 5)));
        point->radius = uint8_t(I(0, 64));
        point->flags = uint16_t(P(0.5f) ? 0x4000 : 0) | uint16_t(I(0, 0x3FFF));
        point->type = uint8_t(P(0.6f) ? I(4, 8) : I(0, 12));
        // (Now and then within 1.5 m, or exactly 1 m away: the points of types 4 and 6 before their waypoints.)
        if (P(0.3f))
            point->pos = g_h.frame->GetWorldPos() + (P(0.2f) ? S_vector(0, 0, 1) : S_vector(U(-1, 1), U(-1.4f, 1.4f), U(-1, 1)));
    }
    // (The first waypoint's type 4 or 5 now and then: the points of types 4 and 6.)
    if (!a->m_waypoints.empty() && P(0.5f))
        a->m_waypoints[0]->type = uint8_t(I(4, 5));
    g_ac.within = P(0.6f);
    g_ac.ammo = P(0.4f) ? 0 : I(1, 200);
    g_self.difficulty = I(1, 4);
}

// The moving flags the fighting reads (firing, standing / crouching / lying, looking around, behind cover).
void RandomCombatFlags(S_TestAi *a)
{
    const uint32_t keep = ~uint32_t(MOVE_FIRING | MOVE_STANDING | MOVE_CROUCHING | MOVE_LYING | MOVE_LOOKING_AROUND |
                                    MOVE_UNKNOWN_8000000);
    const uint32_t stance = P(0.4f) ? MOVE_STANDING : P(0.5f) ? MOVE_CROUCHING : P(0.7f) ? MOVE_LYING : 0u;
    a->m_moveFlags = (a->m_moveFlags & keep) | stance | (P(0.5f) ? MOVE_FIRING : 0u) | (P(0.3f) ? MOVE_LOOKING_AROUND : 0u) |
                     (P(0.3f) ? MOVE_UNKNOWN_8000000 : 0u);
}

void TestCombatA()
{
    std::printf("ai combat: timers, aim, looking, firing, stance\n");
    C_Human *h = g_h.human;
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomCombat();
        RandomCombatFlags(g_pa);
        const int which = I(0, 11);
        switch (which) {
        case 0: {
            const int dt = P(0.1f) ? 0 : I(1, 400);
            AiCall("UpdateTimers", [&] { CombatAi()->UpdateTimers(dt); return 0; }, [&](void *) { OAi<void>(0x00571680, dt); return 0; });
            break;
        }
        case 1: {
            const uint32_t t = uint32_t(P(0.3f) ? g_pa->m_fire.pauseTime : I(0, 2000));
            AiCall("ExtendBurst", [&] { return int(CombatAi()->ExtendBurst(t)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00571a10, t) & 0xFF); });
            break;
        }
        case 2:
            // (Hands: an item: the original reads its definition's carry mode.)
            if (!g_pa->m_handsItem)
                g_pa->m_handsItem = reinterpret_cast<C_Item *>(g_wornItems[I(0, 15)]);
            AiCall("GetAimSpread", [&] { return FloatBitsOf(CombatAi()->GetAimSpread()); },
                   [&](void *) {
                       typedef float(AC * T)(void *);
                       return FloatBitsOf(OriginalAt<T>(0x00572120)(g_oa));
                   });
            break;
        case 3: {
            if (!g_pa->m_handsItem)
                g_pa->m_handsItem = reinterpret_cast<C_Item *>(g_wornItems[I(0, 15)]);
            C_Human *const humans[3] = {h, PortOther(), PortThird()};
            C_Human *target = humans[I(0, 2)];
            void *oTarget = ToOrig(target);
            AiCall("GetHitChance", [&] { return FloatBitsOf(CombatAi()->GetHitChance(target)); },
                   [&](void *) {
                       typedef float(AC * T)(void *, void *);
                       return FloatBitsOf(OriginalAt<T>(0x00571df0)(g_oa, oTarget));
                   });
            break;
        }
        case 4: {
            const S_vector4 from = AiDir(), to = P(0.1f) ? from : AiDir();
            g_cs.workState5 = P(0.3f);
            AiCall("TurnTowards", [&] { return CombatAi()->TurnTowards(from, to); },
                   [&](void *) { return OAi<int>(0x00571730, &from, &to); });
            break;
        }
        case 5:
            // (The move order busy or not: its flags are random words, nearly always busy.)
            if (P(0.5f))
                g_pa->m_move->flags &= ~uint32_t(S_AIMoveOrder::FLAG_BUSY);
            AiCall("ApplyAlertState", [&] { return CombatAi()->ApplyAlertState(); }, [&](void *) { return OAi<int>(0x00575850, 0); });
            break;
        case 6: {
            // A frame to look at, a direction (short or not), the view locked or not, the look timer.
            S_AIFacing &f = *g_pa->m_facing;
            I3D_frame *const frames[3] = {g_otherFrame, g_asThirdFrame, g_asMeleeFrame};
            f.lookAt = P(0.4f) ? frames[I(0, 2)] : nullptr;
            if (P(0.4f))
                f.lookDir = {U(-0.2f, 0.2f), U(-0.2f, 0.2f), U(-0.2f, 0.2f), 0};
            else if (P(0.15f))
                f.lookDir = {f.view.x * 2, f.view.y * 2, f.view.z * 2, 0};   // (along the view: an angle of 0)
            f.lookTimer = P(0.3f) ? I(0, 300) : f.lookTimer;
            const int dt = I(0, 400);
            // (The original's look direction it clears, and the direction to a frame it looks at, take their w from
            // stack temps no path sets: the facing's, view's and look direction's w and the controller's copies of
            // the first two are the port's.)
            AiCall("UpdateLook", [&] { return CombatAi()->UpdateLook(dt); },
                   [&](void *) {
                       const int r = OAi<int>(0x00575940, dt);
                       uint8_t *of;
                       std::memcpy(&of, g_oa + 0x124, 4);
                       const S_AIFacing &pf = *g_pa->m_facing;
                       std::memcpy(of + 0x0C, &pf.facing.w, 4);
                       std::memcpy(of + 0x1C, &pf.view.w, 4);
                       std::memcpy(of + 0x2C, &pf.lookDir.w, 4);
                       std::memcpy(g_oa + 0x1C, &g_pa->m_viewDir.w, 4);
                       std::memcpy(g_oa + 0x2C, &g_pa->m_facingDir.w, 4);
                       return r;
                   });
            break;
        }
        case 7:
            g_pa->m_fire.target = P(0.3f) ? nullptr : static_cast<C_Actor *>(PortOther());
            g_pa->m_fire.flags = uint32_t(I(0, 3));
            g_pa->m_weaponRaised = P(0.5f);
            AiCall("StartFiring", [&] { return int(CombatAi()->StartFiring()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00571a40) & 0xFF); });
            break;
        case 8: {
            // (A weapon in the hands or none: the original reads any item's attack time, the port only a weapon's.)
            if (g_pa->m_handsItem && g_pa->m_handsItem->GetKind() != ITEM_KIND_WEAPON)
                g_pa->m_handsItem = reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
            // (The item busy until exactly now, or the pause exactly its busy time, now and then.)
            const uint32_t now = GetGame()->GetMissionTime();
            if (g_pa->m_handsItem && P(0.15f)) {
                g_pa->m_handsItem->SetTime(now);
                std::memcpy(static_cast<uint8_t *>(OrigWeapon(g_pa->m_handsItem)) + 0x14, &now, 4);   // (the original's copy)
            }
            const int itemBusy = g_pa->m_handsItem ? int(g_pa->m_handsItem->GetTime() - now) : 0;
            const uint32_t pause = uint32_t(P(0.15f) && itemBusy > 0 ? itemBusy : P(0.2f) ? 0 : I(1, 3000));
            AiCall("StopFiring", [&] { CombatAi()->StopFiring(pause); return 0; }, [&](void *) { OAi<void>(0x00572c20, pause); return 0; });
            break;
        }
        case 9:
            AiCall("PressReload", [&] { CombatAi()->PressReload(); return 0; }, [&](void *) { OAi<void>(0x00572c00); return 0; });
            break;
        case 10: {
            // The stance asked for; carrying a body, a special move, animation state 0x5F; the squad prints it.
            static const int stances[] = {MOVE_STANDING, MOVE_CROUCHING, MOVE_LYING, 0, 8};
            const int stance = stances[I(0, 4)];
            C_HumanMovement *m = h->GetMovement();
            h->m_carried = P(0.1f) ? reinterpret_cast<C_Actor *>(g_carriedBytes) : nullptr;
            m->m_unknownA30 = P(0.1f) ? 1 : P(0.1f) ? I(2, 500) : I(-1000, 0);
            m->m_animState.anim.anim = P(0.1f) ? 0x5F : I(0, 0x90);
            if (P(0.3f))
                g_pa->m_type = HUMAN_CONTROLLER_SQUAD;
            AiCall("SetStance", [&] { CombatAi()->SetStance(stance); return 0; }, [&](void *) { OAi<void>(0x005767f0, stance); return 0; });
            break;
        }
        default:
            g_pa->m_stance = P(0.3f) ? MOVE_STANDING : P(0.5f) ? MOVE_CROUCHING : MOVE_LYING;
            AiCall("UpdateCheckpointStance", [&] { CombatAi()->UpdateCheckpointStance(); return 0; },
                   [&](void *) { OAi<void>(0x00576610); return 0; });
            break;
        }
    }
    for (S_HumanAnswers &a : g_asAnswers)
        a.body = false;
}

// ---- part 36b: the fire order (UpdateFiring, CheckFireOrder, CheckReload, UpdateFireOrder), AdjustAim, ThrowGrenade
//
// The weapons are part 23's (each side its copy: the time +0x14, the attack time +0x58, the ready time +0x5C, the
// flags +0x60); their definition (shared) has part 23's component, whose virtuals the two sides find at other slots:
// its vtable is each side's own during the call (the port's GetReloadTimeMs, the original's 0x20, logged alike; the
// muzzle velocity both read from the field, +0x38). The definition's GetAmmoItemId (the port's slot, the original's 0x18) is logged. The
// AI's line-of-fire test (Unknown114, the original's slot 0x114) answers part 33's script on both sides.
struct S_FireScript {
    float muzzleVelocity = 20.0f;
    int reloadTime = 1500;
    int ammoId = 7;
} g_fs;
size_t g_readyTimeAt = 0;                               // the port's C_WeaponItem ready time (found by GetReadyTime)
void *g_compPortVt[32], *g_compOrigVt[32];
int AC CP_GetReloadTimeMs(void *self)
{
    Log(self, "GetReloadTimeMs");
    return g_fs.reloadTime;
}
int AC DF_GetAmmoItemId(void *self)
{
    Log(self, "GetAmmoItemId");
    return g_fs.ammoId;
}
int AC OA_Unknown114(void *self, const S_vector *dir)
{
    Log(self, "Unknown114", {dir->x, dir->y, dir->z});
    return g_ai.lineOfFire;
}
void CompVtableFor(bool orig)
{
    void *vt = orig ? static_cast<void *>(g_compOrigVt) : static_cast<void *>(g_compPortVt);
    std::memcpy(g_weaponCompBytes, &vt, 4);
}

void SetUpFireOrder()
{
    for (int i = 0; i < 32; ++i)
        g_compPortVt[i] = g_compOrigVt[i] = reinterpret_cast<void *>(CT_Unexpected);
    g_compPortVt[VSlot(&C_WeaponComp::GetReloadTimeMs)] = reinterpret_cast<void *>(CP_GetReloadTimeMs);
    g_compOrigVt[0x20 / 4] = reinterpret_cast<void *>(CP_GetReloadTimeMs);
    // (The definition's table serves both sides: the slots must not meet.)
    const size_t ammoSlot = VSlot(&C_ItemDef::GetAmmoItemId);
    CHECK(ammoSlot != VSlot(&C_ItemDef::GetCarryMode) && ammoSlot != VSlot(&C_ItemDef::GetBehaviour) && ammoSlot != 0x0C / 4 &&
          ammoSlot != 0x10 / 4 && 0x18 / 4 != VSlot(&C_ItemDef::GetCarryMode) && 0x18 / 4 != VSlot(&C_ItemDef::GetBehaviour));
    g_defVt[ammoSlot] = reinterpret_cast<void *>(DF_GetAmmoItemId);
    g_defVt[0x18 / 4] = reinterpret_cast<void *>(DF_GetAmmoItemId);
    g_oaVt[0x114 / 4] = reinterpret_cast<void *>(OA_Unknown114);
    // The port's ready time.
    auto *w = reinterpret_cast<C_WeaponItem *>(g_weaponPort[0]);
    for (size_t at = 4; at + 4 <= 0x200 && !g_readyTimeAt; at += 4) {
        uint32_t keep;
        std::memcpy(&keep, g_weaponPort[0] + at, 4);
        const uint32_t marker = 0x13572468;
        std::memcpy(g_weaponPort[0] + at, &marker, 4);
        if (w->GetReadyTime() == marker)
            g_readyTimeAt = at;
        std::memcpy(g_weaponPort[0] + at, &keep, 4);
    }
    CHECK(g_readyTimeAt != 0);
}

// The fight's state with the fire order: a weapon in the hands (or none where the code allows it), its times about the
// mission time, rounds or none, reloading or not; the order's target (a human, part 23's actors in local games), its
// direction (none, short or long), reload and throw requests; the component's kind and numbers; the answers.
void RandomFireOrder(bool allowNone)
{
    RandomCombat();
    RandomCombatFlags(g_pa);
    S_TestAi *a = g_pa;
    const uint32_t now = GetGame()->GetMissionTime();
    for (int k = 0; k < 2; ++k) {
        auto time = [&] { return uint32_t(P(0.5f) ? now - uint32_t(I(0, 3000)) : P(0.3f) ? now : now + uint32_t(I(1, 3000))); };
        const uint32_t t = time(), attack = time(), ready = P(0.7f) ? 0u : time();
        const uint32_t flags = uint32_t(I(0, 7));
        auto *w = reinterpret_cast<C_WeaponItem *>(g_weaponPort[k]);
        w->SetTime(t);
        std::memcpy(g_weaponOrig[k] + 0x14, &t, 4);
        w->SetAttackTime(attack);
        std::memcpy(g_weaponOrig[k] + 0x58, &attack, 4);
        std::memcpy(g_weaponPort[k] + g_readyTimeAt, &ready, 4);
        std::memcpy(g_weaponOrig[k] + 0x5C, &ready, 4);
        w->SetFlags(flags);
        std::memcpy(g_weaponOrig[k] + 0x60, &flags, 4);
    }
    // (The tested human, a target, now and then with a health of exactly 1.)
    if (P(0.1f))
        g_h.human->m_stats[STAT_HEALTH] = 1;
    a->m_handsItem = allowNone && P(0.1f) ? nullptr : reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
    g_ws.amount = P(0.3f) ? 0.0f : float(I(1, 30));
    g_ac.ammo = P(0.4f) ? 0 : I(1, 200);
    g_ac.canSee = I(0, 4);
    g_ac.visible = P(0.5f);
    g_ac.before = I(0, 4);
    g_ai.lineOfFire = P(0.6f);
    g_cs.itemFrame = P(0.2f) ? nullptr : g_shdumParent;
    g_shdumParent->SetPos(g_h.frame->GetWorldPos() + S_vector(U(-1, 1), U(0, 2), U(-1, 1)));
    S_quat turn;
    // (Turned, now and then tilted: the weapon's axis with a height.)
    turn.Make(P(0.3f) ? S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized() : S_vector(0, 1, 0), U(0, 6.2831f));
    g_shdumParent->SetRot(turn);
    UpdateTree(g_shdumParent);
    // The order.
    S_AIFireOrder &o = *a->m_fireOrder;
    C_Human *const humans[3] = {g_h.human, PortOther(), PortThird()};
    const bool local = (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL;
    // (A human target: the tested one, whose model has the bones GetAimPoint picks from (the copies have none); part 23's
    // actors in local games only: IsFriendOf takes network games' targets as humans.)
    o.target = P(0.3f) ? nullptr
               : (P(0.6f) || !local) ? static_cast<C_Actor *>(g_h.human)
                                     : static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1)));
    o.fireDir = P(0.3f) ? S_vector4{0, 0, 0, 0} : P(0.1f) ? S_vector4{1e-9f, 0, 0, 0} : AiDir();
    // (No reload asked with empty hands: the original reads the missing weapon's reload time, a crash the port fixes.)
    o.reload = a->m_handsItem && P(0.15f);
    o.throwGrenade = P(0.1f);
    o.throwPos = ToVector4(g_h.frame->GetWorldPos() + S_vector(U(-30, 30), U(-3, 3), U(-30, 30)), 1.0f);
    o.burstTime = P(0.5f) ? -1 : I(0, 1000);
    o.pauseTime = P(0.5f) ? -1 : I(0, 1000);
    a->m_fire.target = P(0.3f) ? nullptr : P(0.5f) ? o.target : static_cast<C_Actor *>(humans[I(1, 2)]);
    a->m_fire.flags = uint32_t(I(0, 3));
    a->m_fire.burstTime = P(0.3f) ? 0 : I(-100, 600);
    a->m_fire.pauseTime = P(0.6f) ? 0 : I(-100, 600);
    // The component (the weapons' definition is the one part 35b gives its seat component: part 23's back).
    void *component = g_weaponCompBytes;
    std::memcpy(g_wornDefs[0] + 0x48, &component, 4);
    const int kind =P(0.4f) ? ICK_PROJECTILE_WEAPON : P(0.7f) ? ICK_FIREARM : I(0, 4);
    std::memcpy(g_weaponCompBytes + 8, &kind, 4);
    g_fs.muzzleVelocity = U(5, 60);
    std::memcpy(g_weaponCompBytes + 0x38, &g_fs.muzzleVelocity, 4);   // (both read the field: +0x38)
    g_fs.reloadTime = I(0, 4000);
    g_fs.ammoId = I(0, 40);
    g_cs.beltItem = P(0.2f) ? nullptr : reinterpret_cast<C_Item *>(g_weaponPort[1]);
    g_ac.animLength = I(200, 1500);
}

// The w words of the facing's directions and the controller's copies: the port's in the original's (the directions
// the original hands SetView take their w from stack words it does not set: ThrowGrenade's, GetAimDirTo's,
// GetDirToFrame's).
void FacingWFromPort()
{
    uint8_t *of;
    std::memcpy(&of, g_oa + 0x124, 4);
    const S_AIFacing &pf = *g_pa->m_facing;
    std::memcpy(of + 0x0C, &pf.facing.w, 4);
    std::memcpy(of + 0x1C, &pf.view.w, 4);
    std::memcpy(g_oa + 0x1C, &g_pa->m_viewDir.w, 4);
    std::memcpy(g_oa + 0x2C, &g_pa->m_facingDir.w, 4);
}

// A call with the component's vtable each side's.
template <class P, class O> void FireCall(const char *what, P port, O original)
{
    AiCall(what,
           [&] {
               CompVtableFor(false);
               return port();
           },
           [&](void *o) {
               CompVtableFor(true);
               const int r = original(o);
               CompVtableFor(false);
               FacingWFromPort();
               return r;
           });
}

void TestCombatB()
{
    std::printf("ai combat: the fire order, the aim, the grenade\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        const int which = I(0, 7);
        RandomFireOrder(which == 1 || (which >= 3 && which != 6));   // (AdjustAim's spread reads the hands item on both sides)
        switch (which) {
        case 0: {
            const int dt = I(0, 400);
            FireCall("UpdateFiring", [&] { g_pa->UpdateFiring(dt); return 0; }, [&](void *) { OAi<void>(0x00572af0, dt); return 0; });
            break;
        }
        case 1:
            FireCall("CheckFireOrder", [&] { return int(g_pa->CheckFireOrder()); }, [&](void *) { return int(OAi<uint32_t>(0x00575d50)); });
            break;
        case 2:
            FireCall("CheckReload", [&] { return int(g_pa->CheckReload()); }, [&](void *) { return int(OAi<uint32_t>(0x005762b0)); });
            break;
        case 6: {
            // (Room for the original's fourth word: it writes the target frame's w after the direction.)
            S_vector4 pd = AiDir(), od = pd;
            FireCall("AdjustAim",
                     [&] { return int(g_pa->AdjustAim(reinterpret_cast<S_vector *>(&pd))) * 7 + VectorHash(S_vector(pd.x, pd.y, pd.z)); },
                     [&](void *) {
                         return int(OAi<uint32_t>(0x005722b0, &od) & 0xFF) * 7 + VectorHash(S_vector(od.x, od.y, od.z));
                     });
            break;
        }
        case 7:
            FireCall("ThrowGrenade", [&] { g_pa->ThrowGrenade(); return 0; }, [&](void *) { OAi<void>(0x00572de0); return 0; });
            break;
        default: {
            const int dt = I(0, 400);
            FireCall("UpdateFireOrder", [&] { return int(g_pa->UpdateFireOrder(dt)); },
                     [&](void *) { return int(OAi<uint32_t>(0x00576350, dt)); });
            break;
        }
        }
    }
    for (S_HumanAnswers &a : g_asAnswers)
        a.body = false;
}

void TestAiCombat()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    SetUpFireOrder();
    TestCombatA();
    TestCombatB();
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
}
}  // namespace
