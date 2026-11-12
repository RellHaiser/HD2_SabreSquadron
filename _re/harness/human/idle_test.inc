// Part 10 of the human harness (HUMAN_IDLE builds, build_idle.bat; included by human_test.cpp after special_test.inc):
// the idle and the interactions (game/human_idle.cpp: the fidgets, the swimming bob and the breath vapour; Interact,
// UpdateInteraction; the saved games) against the original's. The special moves' build (HUMAN_SPECIAL) under it: the
// movement base, the mixer, the animation player and the special moves are the real ones on both sides.

// ---- what the idle calls besides part 1-9's fakes (logged on both sides)
namespace {
struct S_IdleScript {
    int doorSide = 0;                                   // C_Door::GetSide
    bool exitSeat = true;                               // the vehicle's ExitSeat
    bool hasSeatWeapon = false;                         // HasSeatWeapon
    int occupant = 0;                                   // GetSeatOccupant: 0 none, 1 the body itself, 2 the barrel
    bool fadedOut = false;                              // C_Human::IsFadedOut
} g_idle;
}  // namespace

int C_Door::GetSide(const S_vector4 &pos) const
{
    LogV(this, "DoorGetSide", &pos.x, 3);
    return g_idle.doorSide;
}
uint32_t C_Human::GetMoveMode() const { Log(this, "GetMoveMode"); return 0x30; }
#ifndef HUMAN_VISUAL   // (human_visual.cpp's own in HUMAN_VISUAL builds)
void C_HumanVisual::RemoveShadow() { Log(this, "RemoveShadow"); }
#endif
void C_HumanWeaponPose::SetSeat(uint32_t seatFlags, C_Actor *vehicle, int seat)
{
    Log(this, ("SetSeat " + NameOf(vehicle)).c_str(), {double(seatFlags), double(seat)});
}
void C_HumanWeaponPose::SetSeatWeaponPose(I3D_animation_set *anim) { Log(this, ("SetSeatWeaponPose " + NameOf(anim)).c_str()); }
// (Inline in the original's UpdateInteraction: human_weapon_pose.cpp's body.)
void C_HumanWeaponPose::SetSeatStance(int stance)
{
    m_stance = stance;
    if (stance == POSE_STANCE_PRONE) {
        m_proneFit = true;
        m_proneSettle = 500;
    }
    if (stance != POSE_STANCE_PRONE)
        m_proneFit = false;
}
void C_HumanWork::ApplyHit(uint32_t flags, const S_vector &pos, const S_vector &force)
{
    const float v[6] = {pos.x, pos.y, pos.z, force.x, force.y, force.z};
    LogV(this, "WorkApplyHit", v, 6, {double(flags)});
}
void C_Team::UpdateCameraTarget() { Log(this, "UpdateCameraTarget"); }
// The animation lengths from part 8's tables (the original's own, inline in GetAnimLength, reads them): game/
// human_anim_tables.cpp's code (part 6's fakes answer none).
C_HumanAnimTable *GetHumanAnimTable(int group)
{
    if (group == 1)
        return g_humanAnimTable;
    return group == 2 ? g_humanAnimTable2 : nullptr;
}
int C_HumanAnimTable::GetAnimLength(int id, int variant, int sub) const
{
    if (id >= HUMAN_ANIM_NONE)
        return -1;
    I3D_animation_set *anim = id < HUMAN_ANIM_FIRST_6X ? GetAnim(id, variant) : GetAnim6(id, variant, sub);
    return anim ? anim->GetEndTime() : -1;
}
C_SeatWeaponAnimTable *g_seatWeaponAnimTable = nullptr;    // (0x0098a49c: none in the tests; no seat weapon def)

namespace {
#define IT __attribute__((thiscall))
uint32_t IT OI_GetMoveMode(void *self) { Log(self, "GetMoveMode"); return 0x30; }
void IT OI_RemoveShadow(void *self) { Log(self, "RemoveShadow"); }
void IT OI_SetSeat(void *self, uint32_t seatFlags, void *vehicle, int seat)
{
    Log(self, ("SetSeat " + NameOf(vehicle)).c_str(), {double(seatFlags), double(seat)});
}
void IT OI_SetSeatWeaponPose(void *self, void *anim) { Log(self, ("SetSeatWeaponPose " + NameOf(anim)).c_str()); }
void IT OI_ApplyHit(void *self, uint32_t flags, const float *pos, const float *force)
{
    const float v[6] = {pos[0], pos[1], pos[2], force[0], force[1], force[2]};
    LogV(self, "WorkApplyHit", v, 6, {double(flags)});
}
void IT OI_UpdateCameraTarget(void *self) { Log(self, "UpdateCameraTarget"); }
int IT OI_DoorGetSide(void *self, const float *pos)
{
    LogV(self, "DoorGetSide", pos, 3);
    return g_idle.doorSide;
}

// The vehicle's seats. The port's virtual slots sit 4 bytes after the original's (g++'s two destructor entries), so
// the shared car gets one vtable per side: the original's swapped in for its call (IdleOriginal).
void *g_carVtO[128];
S_ContactActor *ContactNamed(const char *name)
{
    for (S_ContactActor *c : g_contacts)
        if (std::strcmp(c->name, name) == 0)
            return c;
    std::printf("idle: no contact %s\n", name);
    std::exit(3);
}
void *SeatOccupant(bool original)
{
    switch (g_idle.occupant) {
    case 1:
        return original ? static_cast<void *>(o_human) : static_cast<void *>(g_h.human);
    case 2:
        return original ? static_cast<void *>(ContactNamed("barrel")->orig) : static_cast<void *>(ContactNamed("barrel")->port);
    default:
        return nullptr;
    }
}
void IT FC_SetSeatOccupant(void *self, int seat, void *occupant) { Log(self, ("SetSeatOccupant " + NameOf(occupant)).c_str(), {double(seat)}); }
// (A getter: not logged. The original calls it up to three times where the port calls it once.)
void *IT FC_GetSeatOccupantP(void *, int) { return SeatOccupant(false); }
void *IT FC_GetSeatOccupantO(void *, int) { return SeatOccupant(true); }
bool IT FC_ExitSeat(void *self, int seat) { Log(self, "ExitSeat", {double(seat)}); return g_idle.exitSeat; }
bool IT FC_HasSeatWeapon(void *self, int seat) { Log(self, "HasSeatWeapon", {double(seat)}); return g_idle.hasSeatWeapon; }
void *IT FC_GetSeatWeaponDef(void *self, int seat) { Log(self, "GetSeatWeaponDef", {double(seat)}); return nullptr; }
void *IT FC_LeaveSeat(void *self, int seat) { Log(self, "LeaveSeat", {double(seat)}); return nullptr; }

typedef void(IT *T_IVoid)(void *);
typedef void(IT *T_IInt)(void *, int);
typedef void(IT *T_IInteract)(void *, void *, uint32_t, int);
typedef bool(IT *T_IChunk)(void *, void *);
const size_t O_IDLE_OFFSET = 0xCB0;
uint8_t *IdleOf(void *movement) { return static_cast<uint8_t *>(movement) + O_IDLE_OFFSET; }

void SetUpIdle()
{
    SetUpSpecial();
    // The original's idle functions part 6 redirected to loggers run for real here.
    for (uint32_t at : {0x007513f0u, 0x007515c0u, 0x00750b40u, 0x00750c00u, 0x00755960u})
        if (!RestoreOriginal(at)) {
            std::printf("idle: %08x not restored\n", at);
            std::exit(3);
        }
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00602320, reinterpret_cast<const void *>(OI_DoorGetSide)},
        {0x005a5c70, reinterpret_cast<const void *>(OI_GetMoveMode)},
        {0x007590c0, reinterpret_cast<const void *>(OI_RemoveShadow)},
        {0x007ae190, reinterpret_cast<const void *>(OI_SetSeat)},
        {0x007b9070, reinterpret_cast<const void *>(OI_SetSeatWeaponPose)},
        {0x0077fc10, reinterpret_cast<const void *>(OI_ApplyHit)},
        {0x0045dac0, reinterpret_cast<const void *>(OI_UpdateCameraTarget)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    *OriginalAt<void **>(0x0098a49c) = nullptr;           // g_seatWeaponAnimTable
    // The animation player's update runs inside UpdateInteraction: part 8's body to pick up and the car's model with its
    // door handle (TestPlayerStarted makes them there).
    g_bodyFrame = NewFrame(FRAME_DUMMY, "body", S_vector(21, 0.2f, 20), Identity(), Box(S_vector(0.3f, 0.2f, 0.9f)), "", g_sector);
    g_bodyFrame->SetData(&g_bodyData);
    g_carFrame = NewFrame(FRAME_MODEL, "carModel", S_vector(24, 1, 22), Identity(), Box(S_vector(1, 1, 2)), "", g_sector);
    I3D_frame *handle = NewFrame(FRAME_DUMMY, "klika", S_vector(23, 1.1f, 21), Identity(), Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_carFrame);
    g_names[handle] = "klika";
    UpdateTree(g_carFrame);
    reinterpret_cast<C_Actor *>(g_carBytes)->m_frame = g_carFrame;
    *reinterpret_cast<I3D_frame **>(g_carBytes + 0x20) = g_carFrame;
    // The two vtables of the car: the port's slots (VSlot) and the original's.
    for (int i = 0; i < 128; ++i)
        g_carVtO[i] = g_carVt[i];
    g_carVt[VSlot(&C_Actor::SetSeatOccupant)] = reinterpret_cast<void *>(FC_SetSeatOccupant);
    g_carVt[VSlot(&C_Actor::GetSeatOccupant)] = reinterpret_cast<void *>(FC_GetSeatOccupantP);
    g_carVt[VSlot(&C_Actor::LeaveSeat)] = reinterpret_cast<void *>(FC_LeaveSeat);
    g_carVt[VSlot(&C_Actor::ExitSeat)] = reinterpret_cast<void *>(FC_ExitSeat);
    g_carVt[VSlot(&C_Actor::GetSeatFlags)] = reinterpret_cast<void *>(F_GetSeatFlags);
    g_carVt[VSlot(&C_Actor::HasSeatWeapon)] = reinterpret_cast<void *>(FC_HasSeatWeapon);
    g_carVt[VSlot(&C_Actor::GetSeatWeaponDef)] = reinterpret_cast<void *>(FC_GetSeatWeaponDef);
    g_carVtO[0x74 / 4] = reinterpret_cast<void *>(FC_SetSeatOccupant);
    g_carVtO[0x78 / 4] = reinterpret_cast<void *>(FC_GetSeatOccupantO);
    g_carVtO[0x80 / 4] = reinterpret_cast<void *>(FC_LeaveSeat);
    g_carVtO[0x84 / 4] = reinterpret_cast<void *>(FC_ExitSeat);
    g_carVtO[0x8C / 4] = reinterpret_cast<void *>(F_GetSeatFlags);
    g_carVtO[0x98 / 4] = reinterpret_cast<void *>(FC_HasSeatWeapon);
    g_carVtO[0xB4 / 4] = reinterpret_cast<void *>(FC_GetSeatWeaponDef);
}

// The original's call with its vtable on the car and its cold-mission flag.
template <class F> int IdleOriginal(F call)
{
    *reinterpret_cast<void ***>(g_carBytes) = g_carVtO;
    *OriginalAt<bool *>(O_COLD_MISSION) = g_coldMission;
    g_playerOriginalSide = true;                        // (the props it copies are named on its own count)
    const int r = WithOriginalBody(call);
    g_playerOriginalSide = false;
    *reinterpret_cast<void ***>(g_carBytes) = g_carVt;
    return r;
}

// A random human for the idle: the special moves' state, the idle's own, the interaction under way.
void RandomIdle()
{
    RandomSpecial();
    // (The state part 8's update tests keep to: the player's Update runs inside UpdateInteraction.)
    RandomPlayerUpdate(true);
    g_propsMade[0] = g_propsMade[1] = 0;
    C_HumanMovement *m = g_h.movement;
    C_HumanIdle &idle = m->m_idle;
    idle.m_animPlayer = &m->m_animPlayer;
    idle.m_movement = m;
    idle.m_locomotion = &m->m_locomotion;
    idle.m_owner = g_h.human;
    idle.m_standTime = P(0.4f) ? 0 : P(0.05f) ? 100000000 - I(0, 50) : I(1, 30000);
    idle.m_fidgetTime = P(0.3f) ? MOVE_NO_TIME : I(-100, 30000);
    idle.m_vapourTime = P(0.3f) ? I(-50, 50) : I(0, 8000);
    g_idle = S_IdleScript();
    g_idle.doorSide = I(0, 3);
    g_idle.exitSeat = P(0.8f);
    g_idle.hasSeatWeapon = P(0.3f);
    g_idle.occupant = P(0.6f) ? 0 : I(1, 2);
    g_coldMission = P(0.5f);
    m->m_interaction = 0;
    m->m_interactionTime = MOVE_NO_TIME;
    m->m_vehicle = nullptr;
    g_h.human->m_vehicle = nullptr;
    g_h.human->m_fadedOut = P(0.1f);
    m->m_visual.m_lighting.m_sampleDue = P(0.5f);       // (getting out asks for a new sample)
    // The equipment's models of places 1..3 (shown and hidden by the seats).
    for (int place = 1; place <= 3; ++place)
        g_equipmentModel[place] = P(0.5f) ? g_handsModel : nullptr;
    // The remote steps the interactions' ends take off the front: the kinds they look for, now and then a queue full
    // to their limits.
    m->m_remoteActions.clear();
    const int kinds[] = {REMOTE_ACTION_STAND, REMOTE_ACTION_CROUCH, REMOTE_ACTION_PRONE, 5, 6, 8, 9, 10, 0x0B, 0x10, 0x11, 0x12};
    const int steps = P(0.1f) ? I(11, 27) : P(0.5f) ? I(1, 3) : 0;
    for (int k = 0; k < steps; ++k)
        m->m_remoteActions.push_back({I(-100, 2500), kinds[I(0, 11)], P(0.5f) ? 2 : I(0, 3)});
}

void TestIdleSmall()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanIdle &idle = m->m_idle;
    std::printf("idle: small\n");
    for (int i = 0; i < 20000 / QUICK; ++i) {
        RandomIdle();
        switch (i % 2) {
        case 0:
            DiffMove("IdleReset", [&] { idle.Reset(); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_IVoid>(0x00750b40)(IdleOf(mv)); return 0; }); });
            if (i % 100 == 0) {
                // The statics: nothing saved or loaded (both answer true).
                C_chunk c;
                typedef bool(__stdcall * T_IStatics)(void *);
                CHECK(C_HumanIdle::SaveStatics(c) == OriginalAt<T_IStatics>(0x00750ae0)(&c));
                CHECK(C_HumanIdle::LoadStatics(c) == OriginalAt<T_IStatics>(0x00750af0)(&c));
            }
            break;
        default: {
            const int time = P(0.1f) ? 0 : I(1, 200);
            // Swimming in place now and then (the bob at and under the surface); the breath vapour's conditions.
            if (P(0.3f)) {
                g_h.work->m_mode = WORK_MODE_SWIMMING;
                m->m_animState.anim.anim = P(0.5f) ? 0x5B : 0x5D;
                m->m_nextAnimState.anim.anim = P(0.5f) ? 0x5B : 0x5D;
                m->m_locomotion.m_waterHeight = U(-0.2f, 1.0f);
                m->m_swimBobPhase = P(0.2f) ? 0.0f : U(5.5f, 6.3f);
            }
            if (P(0.6f))
                m->m_flags |= MOVE_FLAG_FULL_DETAIL;
            if (P(0.6f))
                m->m_locomotion.m_waterSurface = MOVE_NO_VALUE;
            if (P(0.4f))
                idle.m_vapourTime = I(-50, 20);
            // (The soldier seen in the first person has none; a stance outside the three asks no fidget.)
            g_script.activeIsOwner = P(0.5f);
            if (P(0.4f)) {
                // (Seen in the first person: the game's flag and the original's inline bytes, as RandomMove sets them.)
                g_firstPerson = true;
                g_origFirstPerson[0x58] = P(0.5f);
                g_origFirstPerson[0x59] = !g_origFirstPerson[0x58];
            }
            if (P(0.05f))
                m->m_animState.anim.stance = 3;
            DiffMove("IdleUpdate", [&] { idle.Update(time); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_IInt>(0x00750c00)(IdleOf(mv), time); return 0; }); });
            break;
        }
        }
    }
}

// The chairs to sit at: one with its exit dummy "__*" (the name the original looks up, found as such), one without.
// PlaceChair puts one of them by the body (g_chair).
I3D_frame *g_chairs[2] = {}, *g_chair = nullptr;
void PlaceChair()
{
    if (!g_chairs[0]) {
        for (int k = 0; k < 2; ++k)
            g_chairs[k] = NewFrame(FRAME_DUMMY, k ? "stool" : "chair", S_vector(0, 0, 0), Identity(), Box(S_vector(0.3f, 0.3f, 0.3f)), "", g_sector);
        NewFrame(FRAME_DUMMY, "__*", S_vector(0.0f, 0.0f, 0.6f), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_chairs[0]);
    }
    g_chair = g_chairs[P(0.5f) ? 0 : 1];
    const S_vector4 &p = g_h.human->m_position;
    g_chair->SetPosInline(S_vector(p.x + U(-1, 1), p.y + 0.5f + U(-0.1f, 0.3f), p.z + U(-1, 1)));
    S_quat yaw;
    yaw.Make(S_vector(0, 1, 0), U(0, 6.2831f));
    g_chair->SetRot(yaw);
    UpdateTree(g_chair);
}

// The target of an interaction and the original's copy of it.
struct S_Target {
    void *port, *orig;
};
S_Target TargetFor(uint32_t code)
{
    if (code == INTERACT_DOOR) {
        S_ContactActor *c = ContactNamed(P(0.5f) ? "door" : "openingDoor");
        return {c->port, c->orig};
    }
    if (code >= INTERACT_ENTER_CAR && code <= INTERACT_LEAVE_STATIC_WEAPON)
        return {g_carBytes, g_carBytes};
    if (code == INTERACT_ENTER_JUNKERS88 || code == INTERACT_LEAVE_JUNKERS88 || code == INTERACT_ENTER_SEAT_D)
        return {g_carBytes, g_carBytes};
    S_ContactActor *c = ContactNamed(P(0.7f) ? "otherHuman" : "barrel");
    // (Near the body: the moves up to it and the facings read its position, on both copies.)
    C_Actor *a = reinterpret_cast<C_Actor *>(c->port);
    a->m_position = {g_h.human->m_position.x + U(-2, 2), g_h.human->m_position.y, g_h.human->m_position.z + U(-2, 2), 1.0f};
    std::memcpy(c->orig + 0x30, &a->m_position, 16);
    return {c->port, c->orig};
}

const uint32_t kInteractions[] = {
    INTERACT_DOOR, INTERACT_ENTER_CAR, INTERACT_LEAVE_CAR, INTERACT_ENTER_SUBMARINE, INTERACT_LEAVE_SUBMARINE,
    INTERACT_ENTER_STATIC_WEAPON, INTERACT_LEAVE_STATIC_WEAPON, INTERACT_SIT, INTERACT_STAND_UP, INTERACT_STAND_UP_FAST,
    INTERACT_ENTER_JUNKERS88, INTERACT_LEAVE_JUNKERS88, INTERACT_ENTER_SEAT_D, INTERACT_HEAL, INTERACT_HEAL_END,
    INTERACT_HEAL_HERE, INTERACT_HEAL_HERE_END, INTERACT_MANIPULATE, INTERACT_MANIPULATE_END, INTERACT_MANIPULATE_LONG,
    INTERACT_MANIPULATE_LONG_END, INTERACT_MANIPULATE_LONG_2, INTERACT_MANIPULATE_LONG_2_END, INTERACT_TIE_UP,
    INTERACT_TIE_UP_END, INTERACT_BE_TIED_UP, INTERACT_BE_TIED_UP_END, INTERACT_CUT, INTERACT_CUT_END, INTERACT_BACKSTAB,
    INTERACT_BACKSTAB_END, INTERACT_BE_BACKSTABBED, INTERACT_BE_BACKSTABBED_END, INTERACT_24, INTERACT_24_END, 0x22, 0,
};
uint32_t RandomInteraction() { return kInteractions[I(0, static_cast<int>(std::size(kInteractions)) - 1)]; }

// The interaction under way (what Interact leaves for UpdateInteraction).
void RandomInteractionState()
{
    C_HumanMovement *m = g_h.movement;
    const uint32_t code = RandomInteraction();
    m->m_interaction = P(0.1f) ? 0 : code | (P(0.7f) ? INTERACT_FLAG_BODY : 0);
    const int sitDown = m->GetAnimLength(0x1C, 0, 1);
    m->m_interactionTime = P(0.1f) ? MOVE_NO_TIME
                           : P(0.15f) ? sitDown
                           : P(0.5f) ? I(-1100, 10) : P(0.5f) ? I(1, 20000) : I(9990, 10010);
    if (code == INTERACT_SIT || code == INTERACT_STAND_UP || code == INTERACT_STAND_UP_FAST) {
        PlaceChair();
        m->m_seatFrame = P(0.85f) ? g_chair : nullptr;
        m->m_vehicle = P(0.5f) ? reinterpret_cast<C_Actor *>(ContactNamed("barrel")->port) : nullptr;
    } else {
        m->m_seat = I(0, 4);
        // (Getting in always has its vehicle: both sides call through it. Getting out checks for one.)
        const bool leaving = code == INTERACT_LEAVE_CAR || code == INTERACT_LEAVE_SUBMARINE || code == INTERACT_LEAVE_STATIC_WEAPON ||
                             code == INTERACT_LEAVE_JUNKERS88;
        m->m_vehicle = !leaving || P(0.9f) ? reinterpret_cast<C_Actor *>(g_carBytes) : nullptr;
    }
    // The seat's flags (the pose in it; a seat weapon, the controls, armed, hidden).
    const uint32_t poses[] = {0, SEAT_FLAG_STAND, SEAT_FLAG_CROUCH, SEAT_FLAG_LIE, SEAT_FLAG_ASTRIDE, SEAT_FLAG_SIT};
    uint32_t flags = poses[I(0, 5)];
    if (P(0.3f))
        flags |= SEAT_FLAG_CONTROLS;
    if (P(0.25f))
        flags |= SEAT_FLAG_WEAPON;
    if (P(0.3f))
        flags |= SEAT_FLAG_ARMED;
    if (P(0.2f))
        flags |= SEAT_FLAG_HIDDEN;
    g_seatFlags = flags;
    m->m_seatMode = P(0.8f) ? static_cast<int>(flags) : I(0, 8);
    for (S_ContactActor *c : g_contacts)
        if (std::strcmp(c->name, "barrel") == 0)
            reinterpret_cast<C_Actor *>(c->port)->m_type = ACTOR_BARREL;
}

void TestIdleInteract()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanIdle &idle = m->m_idle;
    std::printf("idle: interact\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomIdle();
        RandomInteractionState();
        // The car for the seats (its type at both sides' offsets), the work's requests the interactions read.
        reinterpret_cast<C_Actor *>(g_carBytes)->m_type = ACTOR_CAR;
        *reinterpret_cast<int *>(g_carBytes + 0x18) = ACTOR_CAR;
        g_h.work->m_blocked2 = I(0, 0xFFFF);
        if (P(0.2f))
            g_h.work->m_mode = WORK_MODE_SWIMMING;
        if (i % 2 == 0) {
            // (A new interaction's vehicle: a car or none. A chair's actor left in m_vehicle would be got out of as a car
            // by the seats' codes.)
            if (m->m_vehicle != reinterpret_cast<C_Actor *>(g_carBytes))
                m->m_vehicle = P(0.7f) ? reinterpret_cast<C_Actor *>(g_carBytes) : nullptr;
            // (The seat's occupant: none, a thing, or the body itself dead (taken out of the seat). Alive, it would be
            // moved out of its own seat for ever; another human is not part of this harness.)
            if (g_idle.occupant == 1)
                g_h.human->m_stats[STAT_HEALTH] = I(-20, 0);
            // (A client's new interaction before the movement's clock has run drops the remote steps.)
            if (P(0.15f))
                m->m_clock = 0;
            // (Ending a heal: the crouch, the heal and the stance back at the front of the queue.)
            if (P(0.1f)) {
                m->m_remoteActions.clear();
                m->m_remoteActions.push_back({10, REMOTE_ACTION_CROUCH, 2});
                m->m_remoteActions.push_back({10, 5, 2});
                m->m_remoteActions.push_back({10, P(0.5f) ? REMOTE_ACTION_STAND : REMOTE_ACTION_PRONE, 0});
            }
            // (Getting up: from a chair or a seat, and already getting up.)
            if (P(0.2f))
                m->m_animState.anim.anim = P(0.5f) ? 0x1D : 0x26;
            else if (P(0.2f))
                m->m_animState.anim.anim = P(0.5f) ? 0x22 : 0x9F;
            const uint32_t code = RandomInteraction();
            const uint32_t interaction = code | (P(0.7f) ? INTERACT_FLAG_BODY : 0) | (P(0.1f) ? 0x800 : 0);
            S_Target target = TargetFor(code);
            int arg = code == INTERACT_SIT ? 0 : I(0, 4);
            if (code == INTERACT_SIT) {
                PlaceChair();
                arg = static_cast<int>(reinterpret_cast<intptr_t>(g_chair));
            }
            // (Labelled by the code: the stats count each interaction apart.)
            static std::map<uint32_t, std::string> labels;
            std::string &label = labels[code];
            if (label.empty()) {
                char text[32];
                std::snprintf(text, sizeof text, "Interact %02x", code);
                label = text;
            }
            DiffMove(label.c_str(), [&] { idle.Interact(static_cast<C_Actor *>(target.port), interaction, arg); return 0; },
                     [&](void *mv) {
                         return IdleOriginal([&] {
                             OriginalAt<T_IInteract>(0x007518e0)(IdleOf(mv), target.orig, interaction, arg);
                             return 0;
                         });
                     });
        } else {
            const int time = P(0.1f) ? 0 : I(1, 200);
            static std::map<uint32_t, std::string> labels;
            const uint32_t code = m->m_interaction & INTERACT_CODE_MASK;
            std::string &label = labels[code];
            if (label.empty()) {
                char text[40];
                std::snprintf(text, sizeof text, "UpdateInteraction %02x", code);
                label = text;
            }
            DiffMove(label.c_str(), [&] { idle.UpdateInteraction(time); return 0; },
                     [&](void *mv) { return IdleOriginal([&] { OriginalAt<T_IInt>(0x00755960)(IdleOf(mv), time); return 0; }); });
        }
    }
}

// The saved games: the stand and fidget times (the files compared), loaded back, or made-up chunks.
void TestIdleSaves()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanIdle &idle = m->m_idle;
    std::printf("idle: saves\n");
    const std::string file = TempFile("idle.chk"), file2 = TempFile("idle_orig.chk");
    for (int i = 0; i < 4000 / QUICK; ++i) {
        RandomIdle();
        if (i % 2 == 0) {
            std::remove(file.c_str());
            std::remove(file2.c_str());
            const bool open = P(0.95f);
            DiffMove("IdleSaveGame",
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), true);
                         return int(idle.SaveGame(c));
                     },
                     [&](void *mv) {
                         return IdleOriginal([&] {
                             C_chunk c;
                             if (open)
                                 c.Open(file2.c_str(), true);
                             return int(OriginalAt<T_IChunk>(0x007513f0)(IdleOf(mv), &c));
                         });
                     });
            CHECK(FileBytes(file) == FileBytes(file2));
            CHECK(!open || FileBytes(file).size() > 10);
        } else {
            // A save of another state (or an unknown chunk, another id) loaded back.
            std::remove(file.c_str());
            {
                C_chunk c;
                c.Open(file.c_str(), true);
                const int kind = I(0, 9);
                c.Begin(kind == 0 ? 0x70 : 0x6F);
                if (kind != 1)
                    c.WriteChunk(0x07, I(0, 50000));
                if (kind == 2)
                    c.WriteChunk(0x33, I(0, 50000));
                if (kind != 3)
                    c.WriteChunk(0x08, I(-100, 30000));
                c.Ascend();
            }
            const bool open = P(0.95f);
            DiffMove("IdleLoadGame",
                     [&] {
                         C_chunk c;
                         if (open)
                             c.Open(file.c_str(), false);
                         return int(idle.LoadGame(c));
                     },
                     [&](void *mv) {
                         return IdleOriginal([&] {
                             C_chunk c;
                             if (open)
                                 c.Open(file.c_str(), false);
                             return int(OriginalAt<T_IChunk>(0x007515c0)(IdleOf(mv), &c));
                         });
                     });
        }
    }
}

void TestIdle()
{
    SetUpIdle();
    TestIdleSmall();
    TestIdleInteract();
    TestIdleSaves();
}

}  // namespace
