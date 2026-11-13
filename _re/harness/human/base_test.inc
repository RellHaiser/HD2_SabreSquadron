// Part 4 of the human harness (HUMAN_BASE builds, build_base.bat; included by human_test.cpp after part 3): the
// movement base (game/human_movement_base.cpp, linked) against the original's: the dummies, the hit and stance
// spheres and volumes, collision on and off, freezing, the snap to the ground, seat modes, push states, the position.
#include "game/actors_module.h"
#include "game/geffects.h"
#include "game/particles.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/surface_marks.h"

// ---- the collision world's volume calls of the port (the linker's --wrap): the human's own volumes logged (the
// world never files them, as on the original's side), the test world's others filed for real.
namespace {
std::set<const S_CollisionVolume *> g_humanVolumes;
bool IsHumanVolume(const S_CollisionVolume *v) { return v && g_humanVolumes.count(v); }
void LogVolume(const char *what, const S_CollisionVolume *v)
{
    if (std::strcmp(what, "MoveVolume") == 0)
        Log(&g_collisionWorld, (std::string(what) + " " + NameOf(v)).c_str(),
            {v->center.x, v->center.y, v->center.z, v->centerW, v->radius, double(v->flags), double(v->type)});
    else
        Log(&g_collisionWorld, (std::string(what) + " " + NameOf(v)).c_str(), {double(v->flags), double(v->type)});
}
std::string VolumeNames(S_CollisionVolume *const *volumes, uint32_t count)
{
    std::string names;
    for (uint32_t i = 0; i < count; ++i)
        names += " " + NameOf(volumes[i]);
    return names;
}
}  // namespace

#define WORLD_CALL __attribute__((thiscall))
extern "C" {
void WORLD_CALL __real__ZN16C_CollisionWorld9AddVolumeEP17S_CollisionVolume(C_CollisionWorld *, S_CollisionVolume *);
void WORLD_CALL __real__ZN16C_CollisionWorld12RemoveVolumeEP17S_CollisionVolume(C_CollisionWorld *, S_CollisionVolume *);
void WORLD_CALL __real__ZN16C_CollisionWorld10MoveVolumeEP17S_CollisionVolume(C_CollisionWorld *, S_CollisionVolume *);
void WORLD_CALL __real__ZN16C_CollisionWorld16SetIgnoredVolumeEP17S_CollisionVolumei(C_CollisionWorld *, S_CollisionVolume *, int);
void WORLD_CALL __real__ZN16C_CollisionWorld17SetIgnoredVolumesEPP17S_CollisionVolumeji(C_CollisionWorld *, S_CollisionVolume **, uint32_t,
                                                                                        int);

void WORLD_CALL __wrap__ZN16C_CollisionWorld9AddVolumeEP17S_CollisionVolume(C_CollisionWorld *self, S_CollisionVolume *v)
{
    if (IsHumanVolume(v))
        LogVolume("AddVolume", v);
    else
        __real__ZN16C_CollisionWorld9AddVolumeEP17S_CollisionVolume(self, v);
}
void WORLD_CALL __wrap__ZN16C_CollisionWorld12RemoveVolumeEP17S_CollisionVolume(C_CollisionWorld *self, S_CollisionVolume *v)
{
    if (IsHumanVolume(v))
        LogVolume("RemoveVolume", v);
    else
        __real__ZN16C_CollisionWorld12RemoveVolumeEP17S_CollisionVolume(self, v);
}
void WORLD_CALL __wrap__ZN16C_CollisionWorld10MoveVolumeEP17S_CollisionVolume(C_CollisionWorld *self, S_CollisionVolume *v)
{
    if (IsHumanVolume(v))
        LogVolume("MoveVolume", v);
    else
        __real__ZN16C_CollisionWorld10MoveVolumeEP17S_CollisionVolume(self, v);
}
void WORLD_CALL __wrap__ZN16C_CollisionWorld16SetIgnoredVolumeEP17S_CollisionVolumei(C_CollisionWorld *, S_CollisionVolume *v, int unknown)
{
    Log(&g_collisionWorld, ("SetIgnoredVolume " + NameOf(v)).c_str(), {double(unknown)});
}
void WORLD_CALL __wrap__ZN16C_CollisionWorld17SetIgnoredVolumesEPP17S_CollisionVolumeji(C_CollisionWorld *, S_CollisionVolume **volumes,
                                                                                        uint32_t count, int unknown)
{
    Log(&g_collisionWorld, ("SetIgnoredVolumes" + VolumeNames(volumes, count)).c_str(), {double(count), double(unknown)});
}
}  // extern "C"

namespace {
// ---- the original's side of the same calls
void __attribute__((thiscall)) O_AddVolumeLog(void *, S_CollisionVolume *v)
{
    if (IsHumanVolume(v))
        LogVolume("AddVolume", v);
}
void __attribute__((thiscall)) O_RemoveVolumeLog(void *, S_CollisionVolume *v)
{
    if (IsHumanVolume(v))
        LogVolume("RemoveVolume", v);
}
void __attribute__((thiscall)) O_MoveVolumeLog(void *, S_CollisionVolume *v)
{
    if (IsHumanVolume(v))
        LogVolume("MoveVolume", v);
}
void __attribute__((thiscall)) O_SetIgnoredVolume(void *, S_CollisionVolume *v, int unknown)
{
    Log(&g_collisionWorld, ("SetIgnoredVolume " + NameOf(v)).c_str(), {double(unknown)});
}
void __attribute__((thiscall)) O_SetIgnoredVolumes(void *, S_CollisionVolume **volumes, uint32_t count, int unknown)
{
    Log(&g_collisionWorld, ("SetIgnoredVolumes" + VolumeNames(volumes, count)).c_str(), {double(count), double(unknown)});
}

void __attribute__((thiscall)) O_StartDeath(void *self, uint32_t hitFlags, const float *force, void *deathAnim, int kind, void *carrier)
{
    LogV(self, "StartDeath", force, 3, {double(hitFlags), double(deathAnim != nullptr), double(kind), double(carrier != nullptr)});
}
void __attribute__((thiscall)) O_FindJoints(void *self, void *) { Log(self, "FindJoints"); }
void __attribute__((thiscall)) O_MixerRelink(void *self, void *, void *) { Log(self, "MixerRelink"); }
void __attribute__((thiscall)) O_MorphRelink(void *self, void *) { Log(self, "MorphRelink"); }
void __attribute__((thiscall)) O_MorphSetUnknown1(void *self, int speed) { Log(self, "MorphSetUnknown1", {double(speed)}); }
void __attribute__((thiscall)) O_MorphSetUnknown2(void *self, int speed) { Log(self, "MorphSetUnknown2", {double(speed)}); }
void __attribute__((thiscall)) O_MorphSetUnknown3(void *self, int speed) { Log(self, "MorphSetUnknown3", {double(speed)}); }

// The movement base's own functions: the original's run (not redirected) in HUMAN_BASE builds.
bool BaseKeepsOriginal(uint32_t address)
{
    static const uint32_t base[] = {0x0075f5e0, 0x0075c6b0, 0x0075c050, 0x0075ebd0, 0x0075cdc0,
                                    0x0075cf20, 0x0075d1b0, 0x0075c4d0, 0x0075bcd0, 0x0075c0e0};
    return std::find(std::begin(base), std::end(base), address) != std::end(base);
}
void InstallBaseRedirects()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x006ef7b0, reinterpret_cast<const void *>(O_AddVolumeLog)},
        {0x006efbc0, reinterpret_cast<const void *>(O_MoveVolumeLog)},
        {0x006ef9b0, reinterpret_cast<const void *>(O_RemoveVolumeLog)},
        {0x006efe40, reinterpret_cast<const void *>(O_SetIgnoredVolume)},
        {0x006efee0, reinterpret_cast<const void *>(O_SetIgnoredVolumes)},
        {0x007b0d70, reinterpret_cast<const void *>(O_StartDeath)},
        {0x0078d0f0, reinterpret_cast<const void *>(O_FindJoints)},
        {0x00772860, reinterpret_cast<const void *>(O_MixerRelink)},
        {0x00781f50, reinterpret_cast<const void *>(O_MorphRelink)},
        {0x007816f0, reinterpret_cast<const void *>(O_MorphSetUnknown1)},
        {0x00781710, reinterpret_cast<const void *>(O_MorphSetUnknown2)},
        {0x00781730, reinterpret_cast<const void *>(O_MorphSetUnknown3)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
}

typedef void(__attribute__((thiscall)) * T_MVoid)(void *);
typedef void(__attribute__((thiscall)) * T_MInt)(void *, int);
typedef void(__attribute__((thiscall)) * T_MIntInt)(void *, int, int);
typedef void(__attribute__((thiscall)) * T_MVec)(void *, const float *);
typedef int(__attribute__((thiscall)) * T_MStanceSpheres)(void *, int, S_vector4 *);
typedef bool(__attribute__((thiscall)) * T_MBoolVoid)(void *);
// The movement of the original's copy (the calls get the locomotion's).
void *MovementOf(void *locomotion) { return static_cast<uint8_t *>(locomotion) - 0xAC0; }

// ---- the model: its shot and stance dummies (boxes: their half width is the sphere's radius), the feet.
I3D_frame *g_shotDummy[2], *g_stanceDummy[STANCE_DUMMY_COUNT], *g_footU, *g_footQ, *g_pelvis;
S_CollisionVolume g_trailVolumes[4];
const S_vector kStanceDummyAt[STANCE_DUMMY_COUNT] = {{0, 0.3f, 0}, {0, 0.9f, 0}, {0, 1.5f, 0}, {0, 0.3f, 0}, {0, 0.9f, 0},
                                                     {0, 0.2f, 0.6f}, {0, 0.2f, 0.2f}, {0, 0.2f, -0.2f}, {0, 0.2f, -0.6f}};

I3D_frame *ModelDummy(const char *name, const S_vector &at, float radius)
{
    I3D_frame *f = NewFrame(FRAME_DUMMY, name, at, Identity(), Box(S_vector(radius, radius, radius)), "", g_h.frame);
    static_cast<I3D_dummy *>(f)->SetBBox(Box(S_vector(radius, radius * 1.3f, radius)));
    return f;
}
// A whole shared object kept apart per side and compared word by word (exactly).
// (`skipW`: the w words of its 16-byte rows left out: the original copies undefined ones in.)
void ShareBlock(void *block, size_t size, const std::string &name, bool skipW = false)
{
    for (size_t i = 0; i + 4 <= size; i += 4)
        if (!(skipW && i % 16 == 12))
            g_sharedWords.push_back({reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(block) + i), name + "+" + std::to_string(i)});
}
void SetUpBase()
{
    C_HumanMovement *m = g_h.movement;
    // The original's joint names (0x0098a348): its static initialiser copies them in (the test's copy of the image
    // never ran its initialisers).
    OriginalAt<void (*)()>(0x007830e0)();
    g_shotDummy[0] = ModelDummy("shot1", S_vector(0.12f, 1.5f, 0.06f), 0.22f);   // (not above each other: their line is read)
    g_shotDummy[1] = ModelDummy("shot2", S_vector(-0.05f, 0.8f, 0.0f), 0.3f);
    static const char *const names[STANCE_DUMMY_COUNT] = {"stand1", "stand2", "stand3", "kneel1", "kneel2", "lie1", "lie2", "lie3", "lie4"};
    for (int i = 0; i < STANCE_DUMMY_COUNT; ++i)
        g_stanceDummy[i] = ModelDummy(names[i], kStanceDummyAt[i], 0.25f + 0.02f * i);
    g_footU = ModelDummy("u", S_vector(-0.15f, 0.05f, 0.1f), 0.05f);
    g_footQ = ModelDummy("q", S_vector(0.15f, 0.05f, -0.1f), 0.05f);
    // (The feet turned a little: their local direction's x is read.)
    for (I3D_frame *foot : {g_footU, g_footQ}) {
        S_quat turn;
        turn.Make(S_vector(0, 1, 0), foot == g_footU ? 0.3f : -0.4f);
        foot->SetRot(turn);
        UpdateTree(foot);
    }
    g_pelvis = ModelDummy("a", S_vector(0, 0.9f, 0), 0.1f);
    for (I3D_frame *f : {g_shotDummy[0], g_shotDummy[1], g_footU, g_footQ})
        g_sharedFrames.push_back(f);
    for (I3D_frame *f : g_stanceDummy)
        g_sharedFrames.push_back(f);
    for (int i = 0; i < 4; ++i) {
        m->m_trailVolumes[i] = &g_trailVolumes[i];
        g_names[&g_h.stanceVolumes[i]] = "stanceVolume" + std::to_string(i);
        g_names[&g_trailVolumes[i]] = "trailVolume" + std::to_string(i);
        g_humanVolumes.insert(&g_h.stanceVolumes[i]);
        g_humanVolumes.insert(&g_trailVolumes[i]);
    }
    for (int i = 0; i < 2; ++i) {
        g_names[&g_h.hitVolumes[i]] = "hitVolume" + std::to_string(i);
        g_humanVolumes.insert(&g_h.hitVolumes[i]);
    }
    g_names[&g_collisionWorld] = "world";
    // The volumes whole (their frames, flags and types too; the world never files them here).
    for (int i = 0; i < 4; ++i) {
        ShareBlock(&g_h.stanceVolumes[i], sizeof(S_CollisionVolume), "stanceVolume" + std::to_string(i));
        ShareBlock(&g_trailVolumes[i], sizeof(S_CollisionVolume), "trailVolume" + std::to_string(i));
    }
    for (int i = 0; i < 2; ++i)
        ShareBlock(&g_h.hitVolumes[i], sizeof(S_CollisionVolume), "hitVolume" + std::to_string(i));
}

// A random human (as part 3's) with the model's dummies and the base's state.
void RandomHumanBase()
{
    RandomHuman3();
    C_HumanMovement *m = g_h.movement;
    C_Human *h = g_h.human;
    // The model where the body is (or a little off it), turned.
    const S_vector4 &p = h->m_position;
    g_h.frame->SetPosInline(S_vector(p.x + (P(0.5f) ? U(-0.3f, 0.3f) : 0.0f), p.y, p.z + (P(0.5f) ? U(-0.3f, 0.3f) : 0.0f)));
    S_quat yaw;
    yaw.Make(S_vector(0, 1, 0), U(0, 6.2831f));
    g_h.frame->SetRot(yaw);
    UpdateTree(g_h.frame);
    // Some dummies missing (a model without them).
    m->m_shotDummies[0] = g_shotDummy[0];
    m->m_shotDummies[1] = g_shotDummy[1];
    for (int i = 0; i < STANCE_DUMMY_COUNT; ++i)
        m->m_stanceDummies[i] = g_stanceDummy[i];   // (every human model has all nine)
    m->m_joints[HUMAN_JOINT_U].frame = g_footU;
    m->m_joints[HUMAN_JOINT_Q].frame = g_footQ;
    m->m_joints[HUMAN_JOINT_A].frame = g_pelvis;
    m->m_joints[HUMAN_JOINT_F1].frame = g_jointF1;   // (part 3's: the relink tests clear them)
    m->m_joints[HUMAN_JOINT_D1].frame = g_jointD1;
    m->m_joints[HUMAN_JOINT_C1].frame = g_jointC1;
    for (int i = 0; i < STANCE_DUMMY_COUNT; ++i) {
        m->m_dummyPos[i] = {U(-50, 50), U(0, 5), U(-50, 50), 1.0f};
        m->m_dummyOffset[i] = {U(-1, 1), U(0, 2), U(-1, 1), 0.0f};
        m->m_dummyRadius[i] = U(0.1f, 0.5f);
    }
    for (int i = 0; i < 2; ++i)
        m->m_hitOffsets[i] = {U(-1, 1), U(0, 2), U(-1, 1), 0.0f};
    // The counts, the stance changes, the push state, the volumes' scales.
    m->m_stanceVolumeCount = I(-1, 4);
    m->m_hitVolumeCount = I(0, 2);
    m->m_trailCount = P(0.6f) ? 0 : I(1, 4);
    m->m_flags = P(0.7f) ? m->m_flags : m->m_flags | MOVE_FLAG_FROZEN;
    if (P(0.2f))
        m->m_flags |= I(1, 3) * 2;                      // (flags 2 / 4: SnapToGround after ClearFlags)
    m->m_stanceChange = P(0.4f) ? (P(0.5f) ? 0 : -1) : MOVE_NO_TIME;
    m->m_unknown9A0 = P(0.5f) ? MOVE_NO_TIME : I(0, 2) == 0 ? 0 : I(1, 300);
    const int stances[] = {3, HUMAN_STANCE_STAND, HUMAN_STANCE_CROUCH, HUMAN_STANCE_PRONE};   // (3: none)
    m->m_nextStance = stances[I(0, 3)];
    m->m_nextStance2 = 3;                            // (only ever none: one stance at a time)
    m->m_unknown1AC = P(0.6f) ? 1 : I(0, 9);
    m->m_unknown1B0 = I(0, 9);
    m->m_unknown9B8 = P(0.3f) ? 0 : P(0.5f) ? MOVE_NO_TIME : I(1, 300);
    m->m_unknown9F0 = {P(0.5f) ? MOVE_NO_VALUE : U(-1, 1), U(-1, 1), U(-1, 1), 0.0f};
    if (P(0.3f))
        m->m_animState.anim.anim = I(0x71, 0x9A);
    m->m_stanceVolumeScale = P(0.05f) ? 0.0f : U(0.6f, 1.2f);
    m->m_hitVolumeScale[0] = P(0.05f) ? 0.0f : U(0.6f, 1.2f);
    m->m_hitVolumeScale[1] = U(0.6f, 1.2f);
    h->m_stats[STAT_HEALTH] = P(0.2f) ? I(-5, 0) : I(1, 100);
    m->m_useState = P(0.5f) ? MOVE_NO_TIME : I(0, 3);
    if (P(0.2f))
        g_h.work->m_mode = P(0.5f) ? 5 : 6;
    for (S_CollisionVolume &v : g_h.stanceVolumes)
        v.flags = COLLISION_SHAPE_SPHERE, v.type = 0x4000;
    for (S_CollisionVolume &v : g_h.hitVolumes)
        v.flags = 0x2000003, v.type = 0x100000;
}

void TestBaseVolumes()
{
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomHumanBase();
        C_HumanMovement *m = g_h.movement;
        // (Dummies missing only where they are skipped: UpdateStanceVolumes, the original's, would index with -10000.)
        const int kind = i % 12;
        if ((kind == 1 || kind == 4 || kind == 5) && P(0.3f))
            for (int k = 0; k < STANCE_DUMMY_COUNT; ++k)
                if (P(0.2f))
                    m->m_stanceDummies[k] = nullptr;
        switch (kind) {
        case 0:
            DiffCall("UpdateHitSpheres", [&] { m->UpdateHitSpheres(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x0075bcd0)(MovementOf(self)); return 0; });
            break;
        case 1: {
            const S_vector pos(U(-50, 50), U(0, 5), U(-50, 50));
            alignas(16) const float padded[4] = {pos.x, pos.y, pos.z, 0.0f};
            if (P(0.5f)) {
                DiffCall("MoveHitSpheres", [&] { m->MoveHitSpheres(pos); return 0; },
                         [&](void *self) { OriginalAt<T_MVec>(0x0075c050)(MovementOf(self), padded); return 0; });
            } else {
                DiffCall("MoveDummies", [&] { m->MoveDummies(pos); return 0; },
                         [&](void *self) { OriginalAt<T_MVec>(0x0075c6b0)(MovementOf(self), padded); return 0; });
            }
            break;
        }
        case 2:
        case 3: {
            const int stance = I(0, 3);
            DiffCall("UpdateStanceVolumes", [&] { m->UpdateStanceVolumes(stance); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x0075c0e0)(MovementOf(self), stance); return 0; });
            break;
        }
        case 4: {
            const int stance = I(0, 3);
            alignas(16) S_vector4 outPort[4] = {}, outOrig[4] = {};
            DiffCall("GetStanceSpheres", [&] { return m->GetStanceSpheres(stance, outPort); },
                     [&](void *self) { return OriginalAt<T_MStanceSpheres>(0x0075c450)(MovementOf(self), stance, outOrig); });
            CHECK(std::memcmp(outPort, outOrig, sizeof outPort) == 0);
            break;
        }
        case 5:
            if (P(0.5f))
                DiffCall("UpdateDummies", [&] { m->UpdateDummies(); return 0; },
                         [&](void *self) { OriginalAt<T_MVoid>(0x0075c4d0)(MovementOf(self)); return 0; });
            else
                DiffCall("UpdateLieDummies", [&] { m->UpdateLieDummies(); return 0; },
                         [&](void *self) { OriginalAt<T_MVoid>(0x0075c5c0)(MovementOf(self)); return 0; });
            break;
        case 6: {
            const bool on = P(0.5f);
            DiffCall("OnFrozen", [&] { m->OnFrozen(on); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x0075c730)(MovementOf(self), on); return 0; });
            break;
        }
        case 7: {
            const bool remove = P(0.5f);
            if (P(0.5f))
                DiffCall("RemoveCollision", [&] { m->RemoveCollision(remove); return 0; },
                         [&](void *self) { OriginalAt<T_MInt>(0x0075cdc0)(MovementOf(self), remove); return 0; });
            else
                DiffCall("RestoreCollision", [&] { m->RestoreCollision(remove); return 0; },
                         [&](void *self) { OriginalAt<T_MInt>(0x0075cf20)(MovementOf(self), remove); return 0; });
            break;
        }
        case 8:
            DiffCall("SnapToGround", [&] { m->SnapToGround(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x0075c950)(MovementOf(self)); return 0; });
            break;
        case 9: {
            const uint32_t flags = static_cast<uint32_t>(I(0, 15)) | (P(0.2f) ? 0x100u : 0u);
            DiffCall("ClearFlags", [&] { m->ClearFlags(flags); return 0; },
                     [&](void *self) { OriginalAt<T_MInt>(0x0075cbd0)(MovementOf(self), static_cast<int>(flags)); return 0; });
            break;
        }
        case 10: {
            const int mode = I(-1, 5);
            const bool on = P(0.5f);
            if (P(0.5f)) {
                DiffCall("SetSeatMode", [&] { m->SetSeatMode(mode, on); return 0; },
                         [&](void *self) { OriginalAt<T_MIntInt>(0x0075d1b0)(MovementOf(self), mode, on); return 0; });
            } else {
                const int state = I(0, 9);
                DiffCall("SetPushState", [&] { m->SetPushState(state, on); return 0; },
                         [&](void *self) { OriginalAt<T_MIntInt>(0x0075e6b0)(MovementOf(self), state, on); return 0; });
            }
            break;
        }
        default:
            if (P(0.5f)) {
                const int which = I(0, 4);
                const int value = I(0, 5);
                DiffCall("SetMorph", [&] { m->SetMorph(which, static_cast<uint32_t>(value)); return 0; },
                         [&](void *self) { OriginalAt<T_MIntInt>(0x0075d160)(MovementOf(self), which, value); return 0; });
            } else {
                g_h.human->m_velocity = {U(-3, 3), 0.0f, U(-3, 3), 0.0f};
                DiffCall("IsLeftFootForward", [&] { return int(m->IsLeftFootForward()); },
                         [&](void *self) { return int(OriginalAt<T_MBoolVoid>(0x0075ea70)(MovementOf(self))); });
            }
            break;
        }
    }
}

// The position set (the model placed, the weapon pose's death reset) and the model relinked (its dummies and joints
// found again, the volumes moved to it).
void TestBaseModel()
{
    typedef void(__attribute__((thiscall)) * T_MModels)(void *, I3D_frame *, I3D_frame *);
    typedef void(__attribute__((thiscall)) * T_MVec4)(void *, const S_vector4 *);
    for (int i = 0; i < 3000 / QUICK; ++i) {
        RandomHumanBase();
        C_HumanMovement *m = g_h.movement;
        for (S_CollisionVolume *v : {&g_h.stanceVolumes[0], &g_h.stanceVolumes[1], &g_h.stanceVolumes[2], &g_h.stanceVolumes[3],
                                     &g_h.hitVolumes[0], &g_h.hitVolumes[1], &g_trailVolumes[0], &g_trailVolumes[1], &g_trailVolumes[2],
                                     &g_trailVolumes[3]})
            v->frame = P(0.5f) ? nullptr : g_animRoot;
        for (int k = 0; k < STANCE_DUMMY_COUNT; ++k)
            m->m_stanceDummies[k] = P(0.5f) ? nullptr : g_stanceDummy[k];
        m->m_shotDummies[0] = m->m_shotDummies[1] = nullptr;
        switch (i % 3) {
        case 0: {
            const S_vector4 pos = {U(3, 61), U(1, 5), U(3, 61), P(0.5f) ? 1.0f : U(0, 2)};
            DiffCall("SetPosition", [&] { m->SetPosition(pos); return 0; },
                     [&](void *self) { OriginalAt<T_MVec4>(0x0075e9c0)(MovementOf(self), &pos); return 0; });
            break;
        }
        case 1:
            DiffCall("RelinkJoints", [&] { m->RelinkJoints(); return 0; },
                     [&](void *self) { OriginalAt<T_MVoid>(0x0075d620)(MovementOf(self)); return 0; });
            break;
        default: {
            // (Another model: one without the dummies.)
            I3D_frame *model = P(0.7f) ? g_h.frame : g_animRoot;
            I3D_frame *previous = P(0.5f) ? g_h.frame : nullptr;
            DiffCall("RelinkModel", [&] { m->RelinkModel(model, previous); return 0; },
                     [&](void *self) { OriginalAt<T_MModels>(0x0075d4e0)(MovementOf(self), model, previous); return 0; });
            break;
        }
        }
    }
}

void TestBase()
{
    SetUpBase();
    TestBaseVolumes();
    TestBaseModel();
}
}  // namespace

// ---- the port's side: what the movement base calls outside itself (logged; HUMAN_BASE builds)
namespace {
alignas(16) uint8_t g_fakeSoundModule[0x200], g_fakeGEffects[0x200];
int g_soundSlot = 3;                                    // what C_SoundModule::Play answers (both sides)
bool g_firstPerson = false;                             // what C_game::IsFirstPersonView answers
I3D_frame *g_equipmentModel[16];                        // C_HumanEquipment::GetModel's answers by place (a getter: not logged)
void LogCreateAt(const void *self, int effect, const S_vector &pos, const S_vector &dir, const void *parent, bool faceCamera, bool transient)
{
    Log(self, ("CreateAt " + NameOf(parent)).c_str(),
        {double(effect), pos.x, pos.y, pos.z, dir.x, dir.y, dir.z, double(faceCamera), double(transient)});
}
// A sound request, logged whole (its frame and source by name; posW is not read).
void LogSoundRequest(const void *self, const S_SoundRequest &r)
{
    Log(self, ("SoundPlay " + NameOf(r.attachTo) + " " + NameOf(r.source) + " " + (r.fileName ? r.fileName : "")).c_str(),
        {r.pos.x, r.pos.y, r.pos.z, double(r.flags), double(r.chapter), double(r.group), r.nearRange, r.farRange, double(r.channel),
         r.nearScale, r.farScale, r.volume});
}
void LogSoundStop(const void *self, int *slot)
{
    Log(self, "SoundStop", {double(*slot)});
    *slot = -1;
}
}  // namespace
#ifndef HUMAN_MORPH   // (game/human_morph.cpp's own in HUMAN_MORPH builds)
void C_HumanMorph::Init(C_Human *) { Log(this, "MorphInit"); }
void C_HumanMorph::Done() { Log(this, "MorphDone"); }
void C_HumanMorph::SetBlinking(bool on) { Log(this, "SetBlinking", {double(on)}); }
void C_HumanMorph::Relink(I3D_frame *) { Log(this, "MorphRelink"); }
void C_HumanMorph::SetUnknown1(int speed) { Log(this, "MorphSetUnknown1", {double(speed)}); }
void C_HumanMorph::SetUnknown2(int speed) { Log(this, "MorphSetUnknown2", {double(speed)}); }
void C_HumanMorph::SetUnknown3(int speed) { Log(this, "MorphSetUnknown3", {double(speed)}); }
#endif
#ifndef HUMAN_MIXER   // (game/human_anim_mixer.cpp's own in HUMAN_MIXER builds)
void C_HumanAnimMixer::Relink(I3D_model *, I3D_model *) { Log(this, "MixerRelink"); }
#endif
void C_HumanEquipment::FindJoints(I3D_frame *) { Log(this, "FindJoints"); }
I3D_frame *C_HumanEquipment::GetModel(int place) const { return place >= 0 && place < 16 ? g_equipmentModel[place] : nullptr; }
void C_HumanEquipment::OnAnimationEvent(int event) { Log(this, "OnAnimationEvent", {double(event)}); }
#ifndef HUMAN_MOVE   // (game/human_movement.cpp's own in HUMAN_MOVE builds)
void C_HumanMovement::UpdateModelNames() { Log(this, "UpdateModelNames"); }
int C_HumanMovement::UseObject(C_Actor *object) { Log(this, ("UseObject " + NameOf(object)).c_str()); return 0; }
#endif
#ifndef HUMAN_DEAD   // (game/human_pose_dead.cpp's own in HUMAN_DEAD builds)
void C_HumanWeaponPose::PickUpBody() { Log(this, "PickUpBody"); }
void C_HumanWeaponPose::StartDeath(uint32_t hitFlags, const float *force, I3D_animation_set *deathAnim, int kind, C_Human *carrier)
{
    LogV(this, "StartDeath", force, 3, {double(hitFlags), double(deathAnim != nullptr), double(kind), double(carrier != nullptr)});
}
#endif
#ifndef HUMAN_WORK   // (game/human_work.cpp's own in HUMAN_WORK builds)
void C_HumanWork::PlayWorkAnimation(int animation, bool on) { Log(this, "PlayWorkAnimation", {double(animation), double(on)}); }
#endif
int C_ParticlesModule::CreateAt(int effect, const S_vector &pos, const S_vector &dir, I3D_frame *parent, bool faceCamera, bool transient)
{
    LogCreateAt(this, effect, pos, dir, parent, faceCamera, transient);
    return 0;
}
int C_SoundModule::Play(const S_SoundRequest &request)
{
    LogSoundRequest(this, request);
    return g_soundSlot;
}
void C_SoundModule::Stop(int *slot) { LogSoundStop(this, slot); }
void C_SoundModule::SetChannelLimit(int channel, int limit) { Log(this, "SetChannelLimit", {double(channel), double(limit)}); }
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(g_fakeSoundModule); }
C_GEffectsModule *GetGEffects() { return reinterpret_cast<C_GEffectsModule *>(g_fakeGEffects); }
C_Actor *g_actorAtIndex0 = nullptr;                      // what GetActor(0) answers (move_test.inc's saves: the vehicle)
C_Actor *C_ActorsModule::GetActor(int index) const { Log(this, "GetActor", {double(index)}); return index == 0 ? g_actorAtIndex0 : nullptr; }
C_Actor *C_ActorsModule::GetActorById(int id) const { Log(this, "GetActorById", {double(id)}); return nullptr; }
C_Actor *C_TemporaryActorsList::FindActor(int id) const { Log(this, "FindTemporaryActor", {double(id)}); return nullptr; }
bool C_game::IsFirstPersonView() const { return g_firstPerson; }
void SendExplosionMessage(const S_vector &pos, int material) { Log(nullptr, "SendExplosionMessage", {pos.x, pos.y, pos.z, double(material)}); }
void SurfaceMarks_Clear() { Log(nullptr, "SurfaceMarks_Clear"); }
S_SurfaceMark *SurfaceMarks_NewMark(int) { Log(nullptr, "SurfaceMarks_NewMark"); return nullptr; }
int SurfaceMarks_FindKind(int material) { return material; }
void SurfaceMarks_AddFootprint(S_SurfaceMark *, const S_vector &pos, float angle, I3D_frame *) { Log(nullptr, "AddFootprint", {pos.x, pos.y, pos.z, angle}); }
bool SurfaceMarks_IsBloodAt(const S_vector &pos) { Log(nullptr, "IsBloodAt", {pos.x, pos.y, pos.z}); return false; }
