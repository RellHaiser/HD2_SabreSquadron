// Part 41 of the human harness (HUMAN_PLAYERWPN builds, build_playerwpn.bat; included by human_test.cpp after
// playerctl_test.inc): the player controller's weapons (game/player_weapons.cpp) against the original's, on part 40's
// pair and fakes (part 23's weapons, part 33's senses and fighting harness linked alongside).

// ---- what the weapons call, faked on both sides (the port's here, the original's below)
namespace {
struct S_PlayerWpnScript {
    bool equipmentFlag1 = false;                        // S_HumanBody::GetEquipmentFlag1
    S_vector4 cameraPos = {0, 2, 0, 1};                 // the camera module's GetCameraPos
    bool cameraPosAnswer = true;
    S_vector facing = {0, 0, 1};                        // C_Human::GetFacingDir
    float maxAmount = 30.0f;                            // the weapons' GetMaxAmount
    // The dynamic ray test (the knife's): hit or not, the actor its volume belongs to (part 23's kind, -1 the other
    // human, -2 the tested one), the sticky charge's volume flags and its frame's actor.
    bool dynamicHit = true;
    int dynamicActor = -1;
    uint32_t volumeFlags = 0xC0;
    int frameActor = 3;
    float dispersion = 1.0f;                            // the weapon component's GetAimedDispersion
} g_pw;
}  // namespace
bool S_HumanBody::GetEquipmentFlag1() const
{
    Log(nullptr, "BodyGetEquipmentFlag1");
    return g_pw.equipmentFlag1;
}
void S_HumanBody::RemoveCollision(bool unknown) { Log(nullptr, "BodyRemoveCollision", {double(unknown)}); }
void S_HumanBody::RestoreCollision(bool unknown) { Log(nullptr, "BodyRestoreCollision", {double(unknown)}); }
bool C_CameraModule::GetCameraPos(int index, S_vector4 *pos)
{
    Log(nullptr, "CameraGetCameraPos", {double(index)});
    *pos = g_pw.cameraPos;
    return g_pw.cameraPosAnswer;
}
const S_vector &C_Human::GetFacingDir() const
{
    Log(this, "GetFacingDir");
    return g_pw.facing;
}
void C_Human::IssueCommand(const S_HumanCommand &command)
{
    Log(this, "HumanIssueCommand", {double(command.category), double(command.type), double(command.param1), double(command.param2)});
}
void C_WeaponItem::PlayEmptySound(C_Actor *owner) { Log(this, ("PlayEmptySound " + NameOf(owner)).c_str()); }
void C_WeaponItem::PlayShotSound(C_Actor *shooter, I3D_frame *where, bool quiet)
{
    Log(this, ("PlayShotSound " + NameOf(shooter) + " " + NameOf(where)).c_str(), {double(quiet)});
}

namespace {
// The volume the ray tests answer with (flags +0x00, frame +0x04, actor +0x48), the car charge's frame (its data's
// actor +0x00): the actors per side (PlSideHook).
alignas(16) uint8_t g_pwVolume[0x60];
I3D_frame *g_pwCarFrame;
S_FrameData g_pwFrameData[2];
void *PwActor(int which, bool original)
{
    if (which == -2)
        return original ? static_cast<void *>(o_human) : static_cast<void *>(g_h.human);
    if (which == -1)
        return original ? static_cast<void *>(g_origOther) : static_cast<void *>(g_portOther);
    if (which < 0 || which >= ACTOR_KIND_COUNT)
        return nullptr;
    return original ? static_cast<void *>(g_actOrig[which]) : static_cast<void *>(g_actPort[which]);
}
// The weapon component's vtable per side (its virtuals at other slots in the two).
void *g_pwCompVt[2][32];
float CT_ PW_GetAimedDispersion(void *self)
{
    Log(self, "CompGetAimedDispersion");
    return g_pw.dispersion;
}
void PwSide(int side)
{
    void *compVt = g_pwCompVt[side == 1];
    std::memcpy(g_weaponCompBytes, &compVt, 4);
    const bool original = side == 1;
    void *actor = side < 0 ? nullptr : PwActor(g_pw.dynamicActor, original);
    std::memcpy(g_pwVolume + 0x48, &actor, 4);
    g_pwFrameData[0].actor = static_cast<C_Actor *>(PwActor(g_pw.frameActor, false));
    g_pwFrameData[1].actor = static_cast<C_Actor *>(PwActor(g_pw.frameActor, true));
    if (g_pwCarFrame)
        g_pwCarFrame->SetData(side < 0 ? nullptr : &g_pwFrameData[original]);
}
void *PwDynamicAnswer(const S_vector *from, const S_vector *dir, const S_vector *hitPos, const S_vector *hitNormal, uint32_t types,
                      uint32_t flags)
{
    Log(nullptr, "RayTestDynamic", {from->x, from->y, from->z, dir->x, dir->y, dir->z, double(hitPos != nullptr),
                                    double(hitNormal != nullptr), double(types), double(flags)});
    return g_pw.dynamicHit ? g_pwVolume : nullptr;
}
void *CT_ PW_RayTestDynamicOrig(void *, const S_vector *from, const S_vector *dir, S_vector *hitPos, S_vector *hitNormal, uint32_t types,
                                uint32_t flags)
{
    return PwDynamicAnswer(from, dir, hitPos, hitNormal, types, flags);
}
}  // namespace
extern "C" S_CollisionVolume *__attribute__((thiscall))
__wrap__ZNK16C_CollisionWorld14RayTestDynamicERK8S_vectorS2_PS0_S3_jj(const C_CollisionWorld *, const S_vector &from, const S_vector &dir,
                                                                      S_vector *hitPos, S_vector *hitNormal, uint32_t types, uint32_t flags)
{
    return static_cast<S_CollisionVolume *>(PwDynamicAnswer(&from, &dir, hitPos, hitNormal, types, flags));
}

namespace {
// ---- the original's side
int CT_ PW_GetEquipmentFlag1(void *)
{
    Log(nullptr, "BodyGetEquipmentFlag1");
    return g_pw.equipmentFlag1;
}
void CT_ PW_RemoveCollision(void *, int unknown) { Log(nullptr, "BodyRemoveCollision", {double(unknown & 0xFF)}); }
void CT_ PW_RestoreCollision(void *, int unknown) { Log(nullptr, "BodyRestoreCollision", {double(unknown & 0xFF)}); }
int CT_ PW_GetCameraPos(void *, int index, S_vector4 *pos)
{
    Log(nullptr, "CameraGetCameraPos", {double(index)});
    *pos = g_pw.cameraPos;
    return g_pw.cameraPosAnswer;
}
const S_vector *CT_ PW_GetFacingDir(void *self)
{
    Log(self, "GetFacingDir");
    return &g_pw.facing;
}
void CT_ PW_IssueCommand(void *self, const S_HumanCommand *command)
{
    Log(self, "HumanIssueCommand", {double(command->category), double(command->type), double(command->param1), double(command->param2)});
}
void CT_ PW_PlayEmptySound(void *self, void *owner) { Log(self, ("PlayEmptySound " + NameOf(owner)).c_str()); }
void CT_ PW_PlayShotSound(void *self, void *shooter, void *where, int quiet)
{
    Log(self, ("PlayShotSound " + NameOf(shooter) + " " + NameOf(where)).c_str(), {double(quiet & 0xFF)});
}
float CT_ PW_GetMaxAmount(void *self)
{
    Log(self, "WeaponGetMaxAmount");
    return g_pw.maxAmount;
}
void CT_ PW_SetLockTime(void *self, uint32_t time) { Log(self, "ContainerSetLockTime", {double(time)}); }
void AC PL_IssueCommandPort(void *self, const S_HumanCommand *command)
{
    Log(self, "PlayerIssueCommand", {double(command->category), double(command->type), double(command->param1), double(command->param2)});
}

void SetUpPlayerWeapons()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005ac400, FAKE(PW_GetEquipmentFlag1)},
        {0x005ac1c0, FAKE(PW_RemoveCollision)},
        {0x005ac1b0, FAKE(PW_RestoreCollision)},
        {0x005a80d0, FAKE(PW_GetFacingDir)},
        {0x005a45b0, FAKE(PW_IssueCommand)},
        {0x007d52f0, FAKE(PW_PlayEmptySound)},
        {0x007d5ee0, FAKE(PW_PlayShotSound)},
        {0x006f7cc0, FAKE(PW_RayTestDynamicOrig)},
        {0x0053d340, FAKE(PL_IssueCommandPort)},
        {0x00720f50, FAKE(OH_GetCheatValue)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_camModuleVt[0x64 / 4] = reinterpret_cast<void *>(PW_GetCameraPos);
    g_weaponPortVt[VSlot(&C_Item::GetMaxAmount)] = reinterpret_cast<void *>(PW_GetMaxAmount);
    g_weaponOrigVt[0x10 / 4] = reinterpret_cast<void *>(PW_GetMaxAmount);
    g_wornVt[VSlot(&C_ItemContainer::SetLockTime)] = reinterpret_cast<void *>(PW_SetLockTime);
    g_wornVt[0x4C / 4] = reinterpret_cast<void *>(PW_SetLockTime);
    for (int side = 0; side < 2; ++side)
        for (void *&slot : g_pwCompVt[side])
            slot = reinterpret_cast<void *>(CT_Unexpected);
    g_pwCompVt[0][VSlot(&C_WeaponComp::GetAimedDispersion)] = reinterpret_cast<void *>(PW_GetAimedDispersion);
    g_pwCompVt[1][0x10 / 4] = reinterpret_cast<void *>(PW_GetAimedDispersion);
    g_names[g_weaponCompBytes] = "weaponComp";
    g_names[g_pwVolume] = "volume";
    S_quat none;
    none.Identity();
    g_pwCarFrame = NewFrame(FRAME_DUMMY, "carFrame", S_vector(0, 0, 5), none, Box(S_vector(1, 1, 2)), "", g_sector);
    std::memcpy(g_pwVolume + 0x04, &g_pwCarFrame, 4);
    g_plSideHook = PwSide;
    // The copy protection checks that drop the weapon now and then and widen the spread past mission 8 (dropped by
    // the port): passed, as part 40c's. (The spread's runs only past mission 8: the harness's game is at 0.)
    for (const auto &pair : {std::pair<uint32_t, uint32_t>{0x008cacda, 0x008cb3da}, {0x008cad08, 0x008cb408}}) {
        const uint16_t a = *OriginalAt<uint16_t *>(pair.first), b = *OriginalAt<uint16_t *>(pair.second);
        OriginalAt<uint32_t *>(0x008ca840)[a] = OriginalAt<uint32_t *>(0x008caf40)[b];
    }
}

// The state: part 40c's, the weapon in the hands (the gun or the grenade, a behaviour, its amounts and times), the
// buttons and axes, the charge, the spawn protection, the vehicle seat, the camera and the rays, the cheat.
void RandomPlayerWeapons()
{
    RandomPlayerC();
    C_PlayerController *p = g_pp;
    C_Human *h = g_h.human;
    p->m_buttons = g_buttons;
    p->m_axes = g_axes;
    if (P(0.03f))
        p->m_buttons = nullptr;
    else if (P(0.03f))
        p->m_axes = nullptr;
    for (int k = 0; k < CONTROL_COUNT; ++k) {
        g_buttons[k] = P(0.8f) ? 0.0f : 1.0f;
        g_axes[k] = P(0.75f) ? 0.0f : 1.0f;
    }
    static const int behaviours[] = {ITEM_BEHAVIOUR_MELEE,   ITEM_BEHAVIOUR_THROWN, ITEM_BEHAVIOUR_MINE,  ITEM_BEHAVIOUR_STICKY_CHARGE,
                                     ITEM_BEHAVIOUR_SIDEARM, ITEM_BEHAVIOUR_SMG,    ITEM_BEHAVIOUR_RIFLE, ITEM_BEHAVIOUR_LMG,
                                     ITEM_BEHAVIOUR_LAUNCHER, 7, ITEM_BEHAVIOUR_ASSAULT};
    g_behaviour = P(0.9f) ? behaviours[I(0, 10)] : I(0, 15);
    g_handsWeapon = P(0.1f) ? nullptr : g_weaponPort[I(0, 1)];
    g_wornContainers[ITEM_CONTAINER_HANDS].item = static_cast<C_Item *>(g_handsWeapon);
    p->m_handsItem = static_cast<C_Item *>(g_handsWeapon);
    g_ws.amount = P(0.2f) ? 0.0f : float(I(1, 30));
    g_pw.maxAmount = P(0.3f) ? g_ws.amount : float(I(1, 30));
    g_ws.reload = P(0.7f);
    g_ws.fire = I(0, 3);
    p->m_charging = P(0.3f);
    p->m_chargeTime = P(0.2f) ? I(990, 1010) : P(0.2f) ? (P(0.5f) ? 700 : 850) + I(-1, 1) : I(0, 1200);
    // (Lying now and then: the throw's cap; part 40b's stances stand or crouch.)
    if (P(0.25f))
        p->m_moveFlags = (p->m_moveFlags & ~uint32_t(MOVE_STANDING | MOVE_CROUCHING)) | MOVE_LYING;
    static const int actions[] = {BODY_ACTION_THROW_BELT_GRENADE, BODY_ACTION_THROW, BODY_ACTION_THROW_2};
    p->m_chargeAction = P(0.9f) ? actions[I(0, 2)] : I(0, 0xD);
    p->m_throwDelay = P(0.6f) ? 0 : I(1, 2500);
    p->m_unknownAC = P(0.8f);
    p->m_interaction = P(0.8f) ? -1 : I(0, 0x26);
    p->m_interactionTime = I(0, 3);
    g_pw.equipmentFlag1 = P(0.1f);
    g_pw.cameraPos = {U(-5, 5), U(0, 3), U(-5, 5), 1.0f};
    g_pw.cameraPosAnswer = P(0.9f);
    g_pw.facing = S_vector(U(-1, 1), U(-0.2f, 0.2f), U(-1, 1)).Normalized();
    g_pw.dynamicHit = P(0.7f);
    g_pw.dynamicActor = P(0.6f) ? (P(0.5f) ? -1 : -2) : I(0, ACTOR_KIND_COUNT - 1);
    g_pw.volumeFlags = P(0.7f) ? (P(0.5f) ? 0x40u : 0x80u) : uint32_t(I(0, 0x3F));
    std::memcpy(g_pwVolume, &g_pw.volumeFlags, 4);
    // (The static ray test, part 40b's, answers with its own volume: the same flags and the car's frame.)
    std::memcpy(g_plRayVolume, &g_pw.volumeFlags, 4);
    std::memcpy(g_plRayVolume + 0x04, &g_pwCarFrame, 4);
    g_pw.frameActor = P(0.6f) ? 3 : I(-2, ACTOR_KIND_COUNT - 1);
    g_seatWeapon = P(0.3f);
    h->m_seat = I(0, 3);
    h->m_aliveTime = I(0, 8000);
    g_mpSettings->spawnProtection = I(0, 5);
    g_combat.cheat = P(0.8f) ? 0 : 1;
    g_cs.aimOrigin = {U(-2, 2), U(0, 2), U(-2, 2), 1.0f};
    g_cs.aimDir = {U(-1, 1), U(-0.5f, 0.5f), U(-1, 1), 0.0f};
    reinterpret_cast<S_vector &>(g_cs.aimDir) = XYZ(g_cs.aimDir).Normalized();
    g_fpvBytes[0x5A] = P(0.5f);
    // (The weapons' model: part 23's with its "shdum" child, as every weapon has.)
    g_cs.itemFrame = g_shdumParent;
    // The weapons' component: its dispersion (none now and then), its attack delay (the port's offset, the original's
    // +0x18), the definition's component back (part 23's).
    g_pw.dispersion = P(0.2f) ? 0.0f : U(0.1f, 20.0f);
    const float delay = float(I(0, 1500));
    std::memcpy(g_weaponCompBytes + g_attackDelayAt, &delay, 4);
    std::memcpy(g_weaponCompBytes + 0x18, &delay, 4);
    void *component = g_weaponCompBytes;
    std::memcpy(g_wornDefs[0] + 0x48, &component, 4);
    // Edges: the spawn protection's end, the attack time just past the empty click's delay, the camera's ray hitting
    // about 3 m from the muzzle, the hands item not yet the container's (UpdateHandsItem's).
    if (P(0.2f))
        h->m_aliveTime = g_mpSettings->spawnProtection * 1000 + I(-1, 1);
    const uint32_t now = GetGame()->GetMissionTime();
    for (int k = 0; k < 2; ++k)
        if (P(0.3f)) {
            const uint32_t attack = now - uint32_t(I(150, 500));
            reinterpret_cast<C_WeaponItem *>(g_weaponPort[k])->SetAttackTime(attack);
            std::memcpy(g_weaponOrig[k] + 0x58, &attack, 4);
        }
    if (P(0.3f)) {
        I3D_frame *shdum = g_shdumParent->FindChildFrame("shdum", 0x20);
        g_plRay.pos = shdum->GetWorldPos() + S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized() * U(2.5f, 4.0f);
    }
    if (P(0.1f))
        p->m_handsItem = P(0.5f) ? nullptr : reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
}

// A call of both sides with the weapons compared after it (each side's: the attack time, the flags, the throw's
// strength, the original's +0x58, +0x60, +0x98).
template <class P, class O> void PwCall(const char *what, P port, O original)
{
    PlCall(what, port, original);
    bool same = WeaponsAgree();
    for (int k = 0; k < 2; ++k)
        same = same && reinterpret_cast<const C_WeaponItem *>(g_weaponPort[k])->GetThrowStrength() ==
                           *reinterpret_cast<const float *>(g_weaponOrig[k] + 0x98);
    CHECK(same);
    static std::map<std::string, int> shown;
    if (!same && shown[what]++ < 3)
        std::printf("  %s: the weapons differ\n", what);
}

void TestPlayerWeaponsA()
{
    std::printf("player weapons: controls, knife, charge\n");
    for (int i = 0; i < 12000; ++i) {
        RandomPlayerWeapons();
        C_PlayerController *p = g_pp;
        const int dt = I(0, 200);
        switch (I(0, 5)) {
        case 0:
        case 1:
        case 2:
            PwCall("WeaponControls", [&] { p->WeaponControls(dt); return 0; }, [&](void *) { OPl<void>(0x005433b0, dt); return 0; });
            break;
        case 3:
            // (A knife in the hands: the original reads a missing one; the port checks.)
            if (!p->m_handsItem)
                p->m_handsItem = reinterpret_cast<C_Item *>(g_weaponPort[0]);
            PwCall("MeleeStrike", [&] { p->MeleeStrike(); return 0; }, [&](void *) { OPl<void>(0x00542b80); return 0; });
            break;
        default:
            // (Its caller checked the controls.)
            p->m_buttons = g_buttons;
            p->m_axes = g_axes;
            PwCall("UpdateCharge", [&] { return int(p->UpdateCharge(dt)); },
                   [&](void *) { return int(OPl<uint32_t>(0x00542e20, dt) & 0xFF); });
            break;
        }
    }
}

// ---- part 41b: the spread, the recoil, the zoom, raising and lowering
// The hands weapon's scope (part 40c's component): its kind and aim mode at the port's offsets (found by GetKind and
// GetAimMode) and the original's +0x08 / +0x0C.
size_t g_pwScopeKindAt = 0, g_pwAimModeAt = 0;
void SetUpPlayerWeaponsB()
{
    for (size_t at = 4; at + 4 <= sizeof g_plScope; at += 4) {
        std::memset(g_plScope, 0, sizeof g_plScope);
        const int marker = 0x5A5A1234;
        std::memcpy(g_plScope + at, &marker, 4);
        const auto *scope = reinterpret_cast<const C_ScopeComp *>(g_plScope);
        if (!g_pwScopeKindAt && int(scope->GetKind()) == marker)
            g_pwScopeKindAt = at;
        if (!g_pwAimModeAt && scope->GetAimMode() == marker)
            g_pwAimModeAt = at;
    }
    CHECK(g_pwScopeKindAt != 0 && g_pwAimModeAt != 0);
    std::memset(g_plScope, 0, sizeof g_plScope);
}
// The scope's kind (a scope or another component) and aim mode (the sights 5, the reticles 0..3, the binoculars 4);
// now and then no secondary component.
void RandomScope()
{
    const int kind = P(0.8f) ? ICK_SCOPE : I(0, 6), mode = P(0.4f) ? 5 : I(0, 6);
    for (size_t at : {g_pwScopeKindAt, size_t(0x08)})
        std::memcpy(g_plScope + at, &kind, 4);
    for (size_t at : {g_pwAimModeAt, size_t(0x0C)})
        std::memcpy(g_plScope + at, &mode, 4);
    void *scope = P(0.1f) ? nullptr : static_cast<void *>(g_plScope);
    S_ScopeAccess::SetSecondary(g_wornDefs[0], scope);
    std::memcpy(g_wornDefs[0] + 0x4C, &scope, 4);
}
// A direction's words as one number (the spread's and the recoil's results compared exactly).
int VectorWords(const float *v, int n)
{
    uint32_t h = 0;
    for (int k = 0; k < n; ++k) {
        uint32_t w;
        std::memcpy(&w, v + k, 4);
        h = h * 0x9E3779B1u + w;
    }
    return int(h);
}

void TestPlayerWeaponsB()
{
    std::printf("player weapons: spread, recoil, zoom, ready\n");
    for (int i = 0; i < 12000; ++i) {
        RandomPlayerWeapons();
        RandomScope();
        C_PlayerController *p = g_pp;
        const int dt = I(0, 200);
        // (A weapon in the hands for the spread and the recoil: the original reads its definition unchecked, as the
        // port; their callers have one.)
        if (!p->m_handsItem)
            p->m_handsItem = reinterpret_cast<C_Item *>(g_weaponPort[I(0, 1)]);
        switch (I(0, 6)) {
        case 0: {
            const S_vector4 d = AiDir();
            const S_vector start(d.x, d.y, d.z);
            PwCall("ApplySpread",
                   [&] {
                       S_vector dir = start;
                       p->ApplySpread(dir);
                       return VectorWords(&dir.x, 3);
                   },
                   [&](void *) {
                       S_vector dir = start;
                       OPl<void>(0x00545140, &dir);
                       return VectorWords(&dir.x, 3);
                   });
            break;
        }
        case 1: {
            const S_vector4 start = AiDir();
            PwCall("ApplyRecoil",
                   [&] {
                       S_vector4 view = start;
                       p->ApplyRecoil(view);
                       return VectorWords(&view.x, 3);
                   },
                   [&](void *) {
                       S_vector4 view = start;
                       OPl<void>(0x005454d0, &view);
                       return VectorWords(&view.x, 3);
                   });
            break;
        }
        case 2:
        case 3:
            // (The buttons there: the original reads missing ones; the port checks.)
            p->m_buttons = g_buttons;
            PwCall("UpdateZoom", [&] { p->UpdateZoom(); return 0; }, [&](void *) { OPl<void>(0x005457e0); return 0; });
            break;
        default:
            // (Its caller checked the controls.)
            p->m_buttons = g_buttons;
            p->m_axes = g_axes;
            p->m_weaponRaised = P(0.5f);
            p->m_raisedTime = P(0.3f) ? 0 : P(0.2f) ? dt + I(-1, 1) : I(1, 3000);
            PwCall("UpdateWeaponReady", [&] { p->UpdateWeaponReady(dt); return 0; }, [&](void *) { OPl<void>(0x00545b90, dt); return 0; });
            break;
        }
    }
}

void TestPlayerWeapons()
{
    SetUpPlayerController();
    SetUpPlayerSquad();
    SetUpPlayerMission();
    SetUpPlayerSlots();
    SetUpPlayerB();
    SetUpPlayerC();
    SetUpPlayerWeapons();
    TestPlayerWeaponsA();
    SetUpPlayerWeaponsB();
    TestPlayerWeaponsB();
}
}  // namespace
