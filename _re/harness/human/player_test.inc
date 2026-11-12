// Part 8 of the human harness (HUMAN_PLAYER builds, build_player.bat; included after mixer_test.inc): the animation
// player, game/human_anim_player.cpp (compiled in player_statics.cpp, whose table gives its update statics). Both sides
// run their own mixer (part 7: the same) on the shared fake model; the animation tables are one set in the original's
// layout, named by the port's globals and the original's alike. The player's calls into the work, the equipment, the
// weapon pose, the human's items and the first-person view are logged on both sides alike.
//
// Part 8a: the small functions (Reset, Init, FindHeadDummy, ReturnToStand, PlayAnimation, the requests' adjustments,
// SaveGame, LoadGame). 8b: what starting an animation means (OnAnimStarted, OnCarryStarted, OnActivityStarted). 8c:
// the update and its tracks.

#include "game/first_person_view.h"
#include "game/items.h"

// ---- the port's side (logged)
namespace {
// What the fakes answer (both sides).
struct S_PlayerScript {
    C_Item *handsItem = nullptr;                        // C_Human::GetHandsItem
    C_Item *handsWeapon = nullptr;                      // C_Human::GetHandsWeapon
    C_Item *beltItem = nullptr;                         // C_Human::FindBeltItem
    I3D_frame *prop[3] = {};                            // GetActivityProp
} g_pl;
}  // namespace

// (With the work's m_forced at the call: the player sets it around its calls.)
void C_HumanWork::SetCarryMode(int carryMode, bool notify) { Log(this, "SetCarryMode", {double(carryMode), double(notify), double(m_forced)}); }
void C_HumanWork::SetWorkingFrame(I3D_frame *frame, bool on) { Log(this, ("SetWorkingFrame " + NameOf(frame)).c_str(), {double(on)}); }
void C_HumanEquipment::SetModel(I3D_frame *model, int place, int uid, int time, int typeId)
{
    // (With the special objects' hand dummy at the call, m_unknown2c.)
    Log(this, ("SetModel " + NameOf(model)).c_str(), {double(place), double(uid), double(time), double(typeId), double(m_unknown2c)});
}
void C_HumanEquipment::SetHolster4Visible(bool on) { Log(this, "SetHolster4Visible", {double(on)}); }
void C_HumanEquipment::SetFlashlight(bool on) { Log(this, "SetFlashlight", {double(on)}); }
void C_HumanWeaponPose::RaiseWeapon(int time) { Log(this, "RaiseWeapon", {double(time)}); }
void C_HumanWeaponPose::LowerWeapon(int time) { Log(this, "LowerWeapon", {double(time)}); }
void C_HumanWeaponPose::PauseWeapon(int time, bool free) { Log(this, "PauseWeapon", {double(time), double(free)}); }
void C_HumanWeaponPose::PauseHands(int time) { Log(this, "PauseHands", {double(time)}); }
void C_HumanWeaponPose::PlayActivity(I3D_animation_set *anim) { Log(this, ("PlayActivity " + NameOf(anim)).c_str()); }
void C_HumanWeaponPose::StartRecoil(I3D_animation_set *anim) { Log(this, ("StartRecoil " + NameOf(anim)).c_str()); }
// (Inline in the original: the pose's two fields, shared; game/human_weapon_pose.cpp's own.)
void C_HumanWeaponPose::SetBlendLength(int time)
{
    m_blendLength = time;
    m_blendTime = static_cast<int>(static_cast<float>(time) * m_blend);
}
C_Item *C_Human::GetHandsItem() const { Log(this, "GetHandsItem"); return g_pl.handsItem; }
C_WeaponItem *C_Human::GetHandsWeapon() const { Log(this, "GetHandsWeapon"); return reinterpret_cast<C_WeaponItem *>(g_pl.handsWeapon); }
C_Item *C_Human::FindBeltItem(int behaviour) const { Log(this, "FindBeltItem", {double(behaviour)}); return g_pl.beltItem; }
I3D_frame *GetActivityProp(int index) { Log(nullptr, "GetActivityProp", {double(index)}); return index >= 0 && index < 3 ? g_pl.prop[index] : nullptr; }
// (The original's reads the activity table: the same here.)
int GetActivityAnimLength(int id, int variant)
{
    if (id >= C_ActivityAnimTable::IDS)
        return -1;
    I3D_animation_set *anim = g_activityAnimTable->GetSlot(id, variant).anim;
    return anim ? anim->GetEndTime() : -1;
}
// (Reached only with an item in the hands or the belt: the tests give none.)
C_ItemDef *GetItemDef(int typeId)
{
    std::printf("player: unexpected GetItemDef(%d)\n", typeId);
    std::exit(3);
}
bool C_CameraModule::GetCameraPos(int, S_vector4 *)
{
    std::printf("player: unexpected GetCameraPos\n");
    std::exit(3);
}
I3D_frame *C_Item::GetFrame(int)
{
    std::printf("player: unexpected C_Item::GetFrame\n");
    std::exit(3);
}
void C_FirstPersonView::OnInventoryEvent(int event, int nextEvent)
{
    Log(this, "OnInventoryEvent", {double(event), double(nextEvent)});
}

// ---- the original's side: the same loggers (MSVC thiscall; bools as words)
namespace {
#define OT __attribute__((thiscall))
void OT OP_SetCarryMode(void *self, int carryMode, int notify)
{
    Log(self, "SetCarryMode", {double(carryMode), double(B(notify)), double(static_cast<uint8_t *>(self)[0xB0] != 0)});
}
void OT OP_SetWorkingFrame(void *self, void *frame, int on) { Log(self, ("SetWorkingFrame " + NameOf(frame)).c_str(), {double(B(on))}); }
void OT OP_SetModel(void *self, void *model, int place, int uid, int time, int typeId)
{
    Log(self, ("SetModel " + NameOf(model)).c_str(),
        {double(place), double(uid), double(time), double(typeId), double(static_cast<uint8_t *>(self)[0x2C] != 0)});
}
void OT OP_SetHolster4Visible(void *self, int on) { Log(self, "SetHolster4Visible", {double(B(on))}); }
void OT OP_SetFlashlight(void *self, int on) { Log(self, "SetFlashlight", {double(B(on))}); }
void OT OP_RaiseWeapon(void *self, int time) { Log(self, "RaiseWeapon", {double(time)}); }
void OT OP_LowerWeapon(void *self, int time) { Log(self, "LowerWeapon", {double(time)}); }
void OT OP_PauseWeapon(void *self, int time, int free) { Log(self, "PauseWeapon", {double(time), double(B(free))}); }
void OT OP_PauseHands(void *self, int time) { Log(self, "PauseHands", {double(time)}); }
void OT OP_PlayActivity(void *self, void *anim) { Log(self, ("PlayActivity " + NameOf(anim)).c_str()); }
void OT OP_StartRecoil(void *self, void *anim) { Log(self, ("StartRecoil " + NameOf(anim)).c_str()); }
void *OT OP_GetHandsItem(void *self) { Log(self, "GetHandsItem"); return g_pl.handsItem; }
void *OT OP_GetHandsWeapon(void *self) { Log(self, "GetHandsWeapon"); return g_pl.handsWeapon; }
void *OT OP_FindBeltItem(void *self, int behaviour) { Log(self, "FindBeltItem", {double(behaviour)}); return g_pl.beltItem; }
void *__fastcall OP_GetActivityProp(int index) { return GetActivityProp(index); }
void OT OP_OnInventoryEvent(void *self, int event, int nextEvent) { Log(self, "OnInventoryEvent", {double(event), double(nextEvent)}); }
#undef OT

void *PlayerOf(void *movement) { return static_cast<uint8_t *>(movement) + 0xC00; }
// The props' copies (the driver's CreateFrame, then the frame's Duplicate), named by the order each side made them.
bool g_playerOriginalSide = false;
int g_propsMade[2];
I3D_frame *__stdcall F_PlayerCreateFrame(void *driver, int type)
{
    I3D_frame *frame = F_CreateFrame(driver, type);
    g_names[frame] = "propCopy" + std::to_string(g_propsMade[g_playerOriginalSide]++);
    Log(nullptr, "CreateFrame", {double(type)});
    return frame;
}
HRESULT __stdcall F_Duplicate(I3D_frame *self, I3D_frame *source)
{
    Log(self, ("Duplicate " + NameOf(source)).c_str());
    return I3D_OK;
}
I3D_frame *g_hdDummy = nullptr;                        // the model's "HD"
I3D_frame *g_propSources[3];                            // GetActivityProp's answers

// ---- the animation tables: one set in the original's layout, named by both sides' globals
alignas(16) uint8_t g_animTableBytes[2][sizeof(C_HumanAnimTable)];
alignas(16) uint8_t g_carryTableBytes[CARRY_ANIM_TABLES][sizeof(C_CarryAnimTable)];
alignas(16) uint8_t g_activityTableBytes[sizeof(C_ActivityAnimTable)];
const uint32_t O_HUMAN_ANIM_TABLE = 0x0098a474, O_HUMAN_ANIM_TABLE2 = 0x0098a478, O_CARRY_ANIM_TABLES = 0x0098a47c,
               O_ACTIVITY_ANIM_TABLE = 0x0098a498;
// Each slot an animation (now and then none), a blend time and flags.
void FillTables()
{
    for (auto &bytes : g_animTableBytes) {
        auto *slots = reinterpret_cast<S_HumanAnimSlot *>(bytes);
        for (int i = 0; i < C_HumanAnimTable::SLOT_COUNT; ++i)
            slots[i] = {static_cast<uint32_t>(I(0, 15)), P(0.2f) ? 0.0f : U(50, 700), RandomSet(0.15f)};
    }
    for (auto &bytes : g_carryTableBytes) {
        auto *slots = reinterpret_cast<S_CarryAnimSlot *>(bytes);
        for (int i = 0; i < C_CarryAnimTable::SLOT_COUNT; ++i)
            slots[i] = {static_cast<uint32_t>(I(0, 15)), RandomSet(0.15f), P(0.3f) ? 1.0f : U(0, 1), P(0.2f) ? 0.0f : U(50, 700)};
    }
    auto *slots = reinterpret_cast<S_HumanAnimSlot *>(g_activityTableBytes);
    for (int i = 0; i < C_ActivityAnimTable::SLOT_COUNT; ++i)
        slots[i] = {static_cast<uint32_t>(I(0, 15)), P(0.2f) ? 0.0f : U(50, 700), RandomSet(0.15f)};
}
// Which tables the globals name (now and then none: a group without a table).
void RandomTables()
{
    g_humanAnimTable = P(0.05f) ? nullptr : reinterpret_cast<C_HumanAnimTable *>(g_animTableBytes[0]);
    g_humanAnimTable2 = P(0.05f) ? nullptr : reinterpret_cast<C_HumanAnimTable *>(g_animTableBytes[1]);
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i)
        g_carryAnimTables[i] = P(0.05f) ? nullptr : reinterpret_cast<C_CarryAnimTable *>(g_carryTableBytes[i]);
    g_activityAnimTable = reinterpret_cast<C_ActivityAnimTable *>(g_activityTableBytes);
    *OriginalAt<void **>(O_HUMAN_ANIM_TABLE) = g_humanAnimTable;
    *OriginalAt<void **>(O_HUMAN_ANIM_TABLE2) = g_humanAnimTable2;
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i)
        OriginalAt<void **>(O_CARRY_ANIM_TABLES)[i] = g_carryAnimTables[i];
    *OriginalAt<void **>(O_ACTIVITY_ANIM_TABLE) = g_activityAnimTable;
}
}  // namespace

// ---- the update's statics (player_statics.cpp's table)
struct S_PlayerStatic {
    const char *name;
    void *port;
    unsigned size;
    unsigned orig;
};
extern const S_PlayerStatic g_playerStatics[];
extern const int g_playerStaticCount;

namespace {
const S_PlayerStatic &PlayerStatic(const char *name)
{
    for (int i = 0; i < g_playerStaticCount; ++i)
        if (std::strcmp(g_playerStatics[i].name, name) == 0)
            return g_playerStatics[i];
    std::printf("player: no static %s\n", name);
    std::exit(3);
}
template <class T> T &PortStatic(const char *name) { return *static_cast<T *>(PlayerStatic(name).port); }
template <class T> T &OrigStatic(const char *name) { return *OriginalAt<T *>(PlayerStatic(name).orig); }

std::vector<std::vector<uint8_t>> g_staticsBefore, g_staticsPort;
std::vector<std::vector<uint8_t>> TakeStatics()
{
    std::vector<std::vector<uint8_t>> s;
    for (int i = 0; i < g_playerStaticCount; ++i) {
        const auto *p = static_cast<const uint8_t *>(g_playerStatics[i].port);
        s.emplace_back(p, p + g_playerStatics[i].size);
    }
    return s;
}
// The bytes of a static that are compared (not the padding after a bool: S_HumanAnimRef's 13 of 16, S_MixerAnim's
// 17 of 20).
bool StaticByteCompared(const S_PlayerStatic &s, unsigned i)
{
    const std::string n = s.name;
    if (n == "g_curAnim" || n == "g_nextAnim" || n == "g_oldAnim")
        return i % 16 < 13;
    if (n == "g_startAnims" || n == "g_anims")
        return i % 20 < 17;
    return true;
}
void CompareStatics()
{
    for (int i = 0; i < g_playerStaticCount; ++i) {
        const S_PlayerStatic &s = g_playerStatics[i];
        const uint8_t *o = OriginalAt<const uint8_t *>(s.orig);
        for (unsigned k = 0; k < s.size; ++k)
            if (StaticByteCompared(s, k) && g_staticsPort[i][k] != o[k]) {
                NoteDiffer(std::string(s.name) + "+" + std::to_string(k) + " (port " + std::to_string(g_staticsPort[i][k]) + " original " +
                           std::to_string(o[k]) + ")");
                return;
            }
    }
}

// DiffMove (the queues, the model's flags) for the player's functions, the statics compared too: the original's
// start from the port's before its call.
template <class P_, class O_> void DiffPlayer(const char *what, P_ port, O_ original)
{
    g_mixDiffers.clear();
    DiffMove(what,
             [&] {
                 g_staticsBefore = TakeStatics();
                 g_propsMade[0] = g_propsMade[1] = 0;
                 g_playerOriginalSide = false;
                 const int r = port();
                 g_staticsPort = TakeStatics();
                 return r;
             },
             [&](void *movement) {
                 for (int i = 0; i < g_playerStaticCount; ++i)
                     std::memcpy(OriginalAt<void *>(g_playerStatics[i].orig), g_staticsBefore[i].data(), g_playerStatics[i].size);
                 g_playerOriginalSide = true;
                 const int r = original(PlayerOf(movement));
                 g_playerOriginalSide = false;
                 CompareStatics();
                 return r;
             });
    if (!g_mixDiffers.empty()) {
        static int shown;
        if (shown++ < 12)
            std::printf("  %s: %s\n", what, g_mixDiffers.c_str());
    }
    CHECK(g_mixDiffers.empty());
}

// The ids the tests use (body, carry, activity), the boundaries of the player's ranges among them.
const int kPlayerAnims[] = {0, 1, 3, 6, 7, 8, 9, 10, 0x0B, 0x0C, 0x0D, 0x0E, 0x12, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1A, 0x1B, 0x1C, 0x1D,
                            0x1E, 0x1F, 0x20, 0x21, 0x22, 0x24, 0x25, 0x29, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x36, 0x37, 0x38, 0x39, 0x3E,
                            0x3F, 0x40, 0x41, 0x42, 0x59, 0x5C, 0x5D, 0x5E, 0x5F, 0x62, 0x67, 0x68, 0x6A, 0x6B, 0x6C, 0x71, 0x80,
                            0x92, 0x94, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0xA0, 0xA1, 0xA2, 0xA3, 0xA5, 0xA6, 0xBD, 0xBE, 0xBF, 0xC0,
                            0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, HUMAN_ANIM_NONE, HUMAN_ANIM_NONE};
const int kPlayerCarries[] = {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x27, 0x46, 0x47, 0x4C, 0x4D, 0x4F, 0x50, 0x51, 0x52,
                              0x5A, 0x5B, 0x5B, 0x5B, 0x5C, 3};
const int kPlayerActivities[] = {0, 1, 2, 7, 8, 0x11, 0x12, 0x13, 0x16, 0x17, 0x19, 0x1A, 0x1C, 0x1D, 0x1E, 0x33, 0x34, 0x3D, 0x41,
                                 0x42, 0x46, 0x47, 0x49, 0x4A, 0x4B, 0x4B, 0x4B, 0x4C, 5};
S_HumanAnimRef RandomPlayerRef()
{
    S_HumanAnimRef r;
    r.anim = Pick(kPlayerAnims);
    r.stance = I(0, 2);                                 // (a 4th would read past the tables, on both sides)
    r.sub = I(0, 5);
    r.valid = P(0.7f);
    return r;
}
void RandomCarry(int *c)
{
    c[0] = Pick(kPlayerCarries);
    c[1] = P(0.12f) ? 5 : I(0, 2);
    c[2] = I(0, 2);
}
void RandomActivity(int *a)
{
    a[0] = Pick(kPlayerActivities);
    a[1] = I(0, 2);
}
S_MixerAnim RandomMixerAnim()
{
    S_MixerAnim a;
    a.anim = RandomSet(0.4f);
    a.weight = U(0, 1);
    a.mode = I(0, 15);
    a.blendTime = U(0, 500);
    a.crossfade = P(0.5f);
    return a;
}

// The player's fields, its statics, the requests and what the work and the hands answer.
void RandomPlayer()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanWork *w = g_h.work;
    C_HumanAnimPlayer &p = m->m_animPlayer;
    p.m_movement = m;
    p.m_specialMoves = &m->m_specialMoves;
    p.m_locomotion = &m->m_locomotion;
    p.m_owner = g_h.human;
    p.m_pendingAnim = RandomPlayerRef();
    p.m_animDelay = P(0.5f) ? MOVE_NO_TIME : I(-50, 900);
    p.m_animDelayState = I(-1, 1);
    p.m_animBlocked = P(0.4f) ? 0x20000 : P(0.5f) ? 0x40000 : I(0, 15);
    RandomCarry(p.m_pendingCarry);
    p.m_carryDelay = P(0.5f) ? MOVE_NO_TIME : I(-50, 900);
    p.m_carryDelayState = I(-1, 1);
    p.m_carryBlocked = P(0.4f) ? 0x20000 : I(0, 0x1FF);
    RandomActivity(p.m_pendingActivity);
    p.m_activityDelay = P(0.5f) ? MOVE_NO_TIME : I(-50, 900);
    p.m_activityDelayState = I(-1, 1);
    p.m_activityBlocked = P(0.4f) ? 0x20000 : I(0, 15);
    p.m_unknown58 = P(0.4f) ? MOVE_NO_TIME : P(0.2f) ? I(-1, 2) : I(-100, 3000);   // (now and then about the idle's 1)
    p.m_unknown5C = I(0, 3);
    p.m_unknown60 = P(0.5f) ? MOVE_NO_TIME : I(0, 100);
    p.m_unknown70 = RandomVector(0.3f);
    p.m_headDummy = P(0.8f) ? g_hdDummy : nullptr;
    p.m_unknown90 = RandomVector(0.3f);
    p.m_unknownA0 = P(0.5f) ? MOVE_NO_VALUE : U(0, 1);
    // The movement's fields the player reads.
    RandomState(m->m_animState);
    m->m_animState.anim = RandomPlayerRef();
    RandomCarry(&m->m_animState.carryAnim);
    RandomActivity(&m->m_animState.activity);
    m->m_nextAnimState.anim = RandomPlayerRef();
    RandomCarry(&m->m_nextAnimState.carryAnim);
    RandomActivity(&m->m_nextAnimState.activity);
    m->m_handsCarryState = I(0, 4);
    m->m_unknown1AC = P(0.6f) ? 1 : I(0, 8);
    m->m_unknown190 = I(0, 3);
    m->m_unknownA24 = P(0.4f) ? 0 : P(0.5f) ? 1 : I(2, 600);
    m->m_unknownA28 = P(0.5f) ? 0 : I(1, 300);
    m->m_unknown220 = I(0, 6);
    m->m_unknown954 = I(0, 2);
    m->m_unknown96C = P(0.5f);
    m->m_unknown9E0 = I(0, 2);
    // The work's request.
    w->m_request.stance = I(0, 2);
    w->m_request.gait = I(0, 5);
    w->m_request.carryMode = P(0.2f) ? 100 : I(0, 6);
    w->m_request.unknown5C = I(0, 2);
    w->m_request.action = I(0, 3);
    w->m_request.anim = P(0.5f) ? m->m_animState.anim.anim : Pick(kPlayerAnims);
    w->m_request.activity = P(0.5f) ? 0x4B : Pick(kPlayerActivities);
    w->m_request.workingFrame = P(0.6f) ? g_handsModel : nullptr;
    w->m_group = P(0.1f) ? I(0, 3) : I(1, 2);
    w->m_forced = P(0.2f);
    w->m_blocked = I(0, 0x3FF);
    w->m_bodyFlags = I(0, 0x3FF);
    g_h.human->m_role = P(0.5f) ? HUMAN_ROLE_PLAYER : static_cast<E_HumanRole>(I(0, 3));
    // The equipment's flags (+0x?? read inline by both: the shared bytes).
    reinterpret_cast<C_HumanEquipment *>(g_h.equipment)->m_flags = P(0.5f) ? 0 : I(0, 0xFF);
    g_pl.handsItem = g_pl.handsWeapon = g_pl.beltItem = nullptr;
    for (int i = 0; i < 3; ++i)
        g_pl.prop[i] = P(0.6f) ? g_propSources[i] : nullptr;
    // The equipment's places about the special object's (7): now and then a model in them.
    for (int place = 6; place < 9; ++place)
        g_equipmentModel[place] = P(0.3f) ? g_handsModel : nullptr;
    // The weapon pose's blend (shared).
    reinterpret_cast<C_HumanWeaponPose *>(g_h.pose)->m_blend = P(0.3f) ? 0.0f : U(0, 1);
    // The statics.
    PortStatic<S_HumanAnimRef>("g_curAnim") = RandomPlayerRef();
    PortStatic<S_HumanAnimRef>("g_nextAnim") = RandomPlayerRef();
    PortStatic<S_HumanAnimRef>("g_oldAnim") = RandomPlayerRef();
    RandomCarry(&PortStatic<int>("g_curCarry"));
    RandomCarry(&PortStatic<int>("g_nextCarry"));
    RandomCarry(&PortStatic<int>("g_oldCarry"));
    RandomActivity(&PortStatic<int>("g_curActivity"));
    RandomActivity(&PortStatic<int>("g_nextActivity"));
    RandomActivity(&PortStatic<int>("g_oldActivity"));
    PortStatic<int>("g_jointCount") = P(0.5f) ? -1 : I(0, 8);
    auto *starts = &PortStatic<S_MixerAnim>("g_startAnims");
    auto *anims = &PortStatic<S_MixerAnim>("g_anims");
    for (int i = 0; i < MIXER_TRACKS; ++i) {
        starts[i] = RandomMixerAnim();
        anims[i] = RandomMixerAnim();
    }
    PortStatic<bool>("g_changed") = P(0.5f);
    PortStatic<I3D_animation_set *>("g_animSet") = RandomSet(0.5f);
    PortStatic<I3D_animation_set *>("g_carrySet") = RandomSet(0.5f);
    PortStatic<I3D_animation_set *>("g_activitySet") = RandomSet(0.5f);
    PortStatic<float>("g_animSpeed") = P(0.6f) ? 1.0f : U(0.5f, 1.5f);
    PortStatic<float>("g_animBlendTime") = P(0.6f) ? -1.0f : U(0, 400);
    PortStatic<float>("g_carrySpeed") = P(0.6f) ? 1.0f : U(0.5f, 1.5f);
    PortStatic<float>("g_activitySpeed") = P(0.6f) ? 1.0f : U(0.5f, 1.5f);
    PortStatic<const C_HumanAnimTable *>("g_animTable") = P(0.5f) ? g_humanAnimTable : g_humanAnimTable2;
    RandomTables();
}

// A whole random human for a player test: the movement's, the mixer's and the player's state.
void RandomPlayerHuman()
{
    RandomMove();
    RandomMixer();
    RandomPlayer();
}

void SetUpPlayer()
{
    SetUpMove();
    SetUpMixer();
    // The original's player functions part 6 redirected to loggers run for real here (and the mixer's saves).
    for (uint32_t at : {0x0077d870u, 0x00777ee0u, 0x0077d8c0u, 0x0077dc90u, 0x00777ea0u, 0x00779f90u, 0x00776610u, 0x00776f90u})
        if (!RestoreOriginal(at)) {
            std::printf("player: %08x was not redirected\n", at);
            std::exit(3);
        }
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0077f750, reinterpret_cast<const void *>(OP_SetCarryMode)},
        {0x0077fb70, reinterpret_cast<const void *>(OP_SetWorkingFrame)},
        {0x007916f0, reinterpret_cast<const void *>(OP_SetModel)},
        {0x00791d20, reinterpret_cast<const void *>(OP_SetHolster4Visible)},
        {0x00793610, reinterpret_cast<const void *>(OP_SetFlashlight)},
        {0x007b9160, reinterpret_cast<const void *>(OP_RaiseWeapon)},
        {0x007b91c0, reinterpret_cast<const void *>(OP_LowerWeapon)},
        {0x007b9270, reinterpret_cast<const void *>(OP_PauseWeapon)},
        {0x007b9220, reinterpret_cast<const void *>(OP_PauseHands)},
        {0x007ae780, reinterpret_cast<const void *>(OP_PlayActivity)},
        {0x007ae7f0, reinterpret_cast<const void *>(OP_StartRecoil)},
        {0x005a2ac0, reinterpret_cast<const void *>(OP_GetHandsItem)},
        {0x005a2b10, reinterpret_cast<const void *>(OP_GetHandsWeapon)},
        {0x005a6730, reinterpret_cast<const void *>(OP_FindBeltItem)},
        {0x007d7b20, reinterpret_cast<const void *>(OP_GetActivityProp)},
        {0x00492930, reinterpret_cast<const void *>(OP_OnInventoryEvent)},
        {0x0077f4b0, reinterpret_cast<const void *>(OM_PlayWorkAnimation)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // The first-person view: the original's game's +0x5C, the port's game's own (the same object).
    GetGame()->m_firstPersonView = reinterpret_cast<C_FirstPersonView *>(g_origFirstPerson);
    *reinterpret_cast<void **>(g_origGame + 0x5C) = g_origFirstPerson;
    g_names[g_origFirstPerson] = "firstPersonView";
    // The weapon pose's blend fields (shared: the original's inline SetBlendLength writes them).
    C_HumanWeaponPose *pose = reinterpret_cast<C_HumanWeaponPose *>(g_h.pose);
    ShareBlock(&pose->m_blendLength, 4, "pose blendLength");
    ShareBlock(&pose->m_blendTime, 4, "pose blendTime");
    ShareBlock(&pose->m_blend, 4, "pose blend");
    // The head dummy "HD" of the model.
    // The activities' props and their copies.
    for (int i = 0; i < 3; ++i) {
        g_propSources[i] = NewFrame(FRAME_MODEL, "", S_vector(0, 0, 0), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)));
        g_names[g_propSources[i]] = "prop" + std::to_string(i);
    }
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_PlayerCreateFrame);
    g_frameVt[0x4C / 4] = reinterpret_cast<void *>(F_Duplicate);
    g_hdDummy = ModelDummy("HD", S_vector(0.0f, 1.7f, 0.05f), 0.05f);
    g_names[g_hdDummy] = "HD";
    ShareBlock(&g_hdDummy->m_localMatrix, sizeof(S_matrix), "HD matrix");
    // (The w of m_unknown70 and m_unknown90: the original's LoadGame copies a 16-byte stack temp in, OnAnimStarted
    // stores one's; the port keeps the field's own.)
    g_skipWords.insert({"m_animPlayer", 0x7C});
    g_skipWords.insert({"m_animPlayer", 0x9C});
    FillTables();
    RandomTables();
    for (int i = 0; i < 2; ++i)
        g_names[g_animTableBytes[i]] = "animTable" + std::to_string(i + 1);
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i)
        g_names[g_carryTableBytes[i]] = "carryTable" + std::to_string(i);
    g_names[g_activityTableBytes] = "activityTable";
}

// ---- 8a: the small functions
typedef void(__attribute__((thiscall)) * T_PInit)(void *, void *, void *, void *, void *);
typedef void(__attribute__((thiscall)) * T_PPlay)(void *, const char *, int, int, int);
typedef void(__attribute__((thiscall)) * T_PPtr2)(void *, const void *, void *);
typedef void(__attribute__((thiscall)) * T_PPtr3)(void *, const void *, const void *, void *);
const char *const kPlayerAnimNames[] = {"walk.i3d", "anims\\run.i3d", "stand.i3d", "carry.i3d", "missing.i3d", "", nullptr};

void TestPlayerSmall()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanAnimPlayer &p = m->m_animPlayer;
    std::printf("player: small\n");
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomPlayerHuman();
        switch (i % 8) {
        case 0:
            DiffPlayer("Reset", [&] { p.Reset(); return 0; }, [&](void *self) { OriginalAt<T_MVoid>(0x00777ea0)(self); return 0; });
            break;
        case 1:
            // (The pointers Init sets, cleared first: RandomPlayer gives them their values already.)
            p.m_specialMoves = nullptr;
            p.m_movement = nullptr;
            p.m_owner = nullptr;
            p.m_locomotion = nullptr;
            DiffPlayer("Init", [&] { p.Init(m, &m->m_specialMoves, &m->m_locomotion, g_h.human); return 0; },
                       [&](void *self) {
                           OriginalAt<T_PInit>(0x00777ee0)(self, o_movement, o_movement + 0xAA0, o_movement + 0xAC0, o_human);
                           return 0;
                       });
            break;
        case 2:
            DiffPlayer("FindHeadDummy", [&] { p.FindHeadDummy(); return 0; },
                       [&](void *self) { OriginalAt<T_MVoid>(0x0077d870)(self); return 0; });
            break;
        case 3:
            DiffPlayer("ReturnToStand", [&] { p.ReturnToStand(); return 0; },
                       [&](void *self) { OriginalAt<T_MVoid>(0x0077d800)(self); return 0; });
            break;
        case 4: {
            const char *name = kPlayerAnimNames[I(0, static_cast<int>(sizeof kPlayerAnimNames / sizeof kPlayerAnimNames[0]) - 1)];
            const int blendTime = I(0, 600), fadeOut = I(0, 600), loop = P(0.5f) ? 0 : P(0.5f) ? 1 : 0x100;
            DiffPlayer("PlayAnimation", [&] { p.PlayAnimation(name, blendTime, fadeOut, loop); return 0; },
                       [&](void *self) { OriginalAt<T_PPlay>(0x0077d270)(self, name, blendTime, fadeOut, loop); return 0; });
            break;
        }
        case 5:
            // (Now and then nothing asked while a moving animation plays: the heavy weapon's 8 later ids.)
            if (P(0.3f)) {
                PortStatic<S_HumanAnimRef>("g_nextAnim").anim = HUMAN_ANIM_NONE;
                PortStatic<S_HumanAnimRef>("g_curAnim").anim = I(0xBE, 0xCD);
            }
            DiffPlayer("AdjustAnimRequest",
                       [&] { p.AdjustAnimRequest(PortStatic<S_HumanAnimRef>("g_curAnim"), &PortStatic<S_HumanAnimRef>("g_nextAnim")); return 0; },
                       [&](void *self) {
                           OriginalAt<T_PPtr2>(0x0077caf0)(self, &OrigStatic<S_HumanAnimRef>("g_curAnim"), &OrigStatic<S_HumanAnimRef>("g_nextAnim"));
                           return 0;
                       });
            break;
        case 6:
            DiffPlayer("AdjustCarryRequest",
                       [&] { p.AdjustCarryRequest(PortStatic<S_HumanAnimRef>("g_curAnim"), &PortStatic<int>("g_curActivity")); return 0; },
                       [&](void *self) {
                           OriginalAt<T_PPtr2>(0x0077cd30)(self, &OrigStatic<S_HumanAnimRef>("g_curAnim"), &OrigStatic<int>("g_curActivity"));
                           return 0;
                       });
            break;
        default:
            DiffPlayer("AdjustActivityRequest",
                       [&] {
                           p.AdjustActivityRequest(PortStatic<S_HumanAnimRef>("g_curAnim"), &PortStatic<int>("g_curCarry"),
                                                   &PortStatic<int>("g_nextActivity"));
                           return 0;
                       },
                       [&](void *self) {
                           OriginalAt<T_PPtr3>(0x0077d190)(self, &OrigStatic<S_HumanAnimRef>("g_curAnim"), &OrigStatic<int>("g_curCarry"),
                                                           &OrigStatic<int>("g_nextActivity"));
                           return 0;
                       });
            break;
        }
    }
}

void TestPlayerSaves()
{
    C_HumanAnimPlayer &p = g_h.movement->m_animPlayer;
    const std::string portFile = TempFile("player_port.chk"), origFile = TempFile("player_orig.chk");
    std::printf("player: saves\n");
    for (int i = 0; i < 4000 / QUICK; ++i) {
        RandomPlayerHuman();
        const bool open = P(0.95f);
        std::remove(portFile.c_str());
        std::remove(origFile.c_str());
        DiffPlayer("SaveGame",
                   [&] {
                       C_chunk c;
                       if (open)
                           c.Open(portFile.c_str(), true);
                       return int(p.SaveGame(c));
                   },
                   [&](void *self) {
                       C_chunk c;
                       if (open)
                           c.Open(origFile.c_str(), true);
                       return int(B(OriginalAt<T_XChunk>(0x0077d8c0)(self, &c)));
                   });
        const std::vector<uint8_t> savedPort = FileBytes(portFile), savedOrig = FileBytes(origFile);
        CHECK(savedPort == savedOrig);
        CHECK(!open || savedPort.size() > 100);
        // Loaded back into another state (or nothing open, or another chunk first, or an unknown chunk inside).
        const bool loadOpen = P(0.97f);
        if (P(0.1f)) {
            C_chunk c;
            c.Open(portFile.c_str(), true);
            c.Begin(P(0.5f) ? 0x123 : 0x1BC);
            const S_vector v(U(-1, 1), U(-1, 1), U(-1, 1));
            c.WriteChunk(0x0E, &v, sizeof v);
            c.WriteChunk(0x40, U(0.5f, 1.5f));
            c.Ascend();
        }
        RandomPlayerHuman();
        DiffPlayer("LoadGame",
                   [&] {
                       C_chunk c;
                       if (loadOpen)
                           c.Open(portFile.c_str(), false);
                       return int(p.LoadGame(c));
                   },
                   [&](void *self) {
                       C_chunk c;
                       if (loadOpen)
                           c.Open(portFile.c_str(), false);
                       return int(B(OriginalAt<T_XChunk>(0x0077dc90)(self, &c)));
                   });
    }
}

// ---- 8b: what starting an animation means
typedef void(__attribute__((thiscall)) * T_PStarted)(void *, const void *, const void *, int);
typedef void(__attribute__((thiscall)) * T_PStarted2)(void *, const void *, const void *);
// A body to pick up (anim 0x5E): a frame whose data names the human (the original's copy on its side).
S_FrameData g_bodyData;
I3D_frame *g_bodyFrame = nullptr;
I3D_frame *g_carFrame = nullptr;
template <class F> int WithOriginalBody(F call)
{
    C_Actor *was = g_bodyData.actor;
    if (was == g_h.human)
        g_bodyData.actor = reinterpret_cast<C_Actor *>(o_human);
    const int r = call();
    g_bodyData.actor = was;
    return r;
}
// What the starting tests vary beyond RandomPlayerHuman: a vehicle with a door handle, the ladder in use, a body to
// pick up, the trail spheres.
void RandomStarted()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanWork *w = g_h.work;
    if (P(0.3f))
        m->m_vehicle = reinterpret_cast<C_Actor *>(g_carBytes);
    m->m_useObject = P(0.5f) ? reinterpret_cast<C_Actor *>(Ladder()->port) : nullptr;
    if (P(0.5f))
        w->m_request.workingFrame = g_bodyFrame;
    g_bodyData.actor = P(0.8f) ? static_cast<C_Actor *>(g_h.human) : nullptr;
    // (An activity that takes the working frame's reference needs one: the original's and the port's alike.)
    if (!w->m_request.workingFrame)
        w->m_request.workingFrame = P(0.5f) ? g_handsModel : g_bodyFrame;
    m->m_trailTime = I(0, 500);
    m->m_trailCount = I(0, 2);
    // The ladder's ends: one above the other, a little apart (about OnAnimStarted's 0.05 of the step off its top), or
    // further.
    C_Ladder *ladder = reinterpret_cast<C_Ladder *>(Ladder()->port);
    const float rise = U(2, 5), apart = P(0.3f) ? 0.0f : P(0.4f) ? U(0.08f, 0.17f) : U(0.2f, 1.0f);
    ladder->m_bottom = {U(-2, 2), U(-1, 1), U(-2, 2), 0.0f};
    ladder->m_top = {ladder->m_bottom.x + apart, ladder->m_bottom.y + rise, ladder->m_bottom.z, 0.0f};
    std::memcpy(Ladder()->orig + 0xA0, &ladder->m_top, 16);
    std::memcpy(Ladder()->orig + 0xB0, &ladder->m_bottom, 16);
}

void TestPlayerStarted()
{
    C_HumanMovement *m = g_h.movement;
    C_HumanAnimPlayer &p = m->m_animPlayer;
    g_bodyFrame = NewFrame(FRAME_DUMMY, "body", S_vector(21, 0.2f, 20), Identity(), Box(S_vector(0.3f, 0.2f, 0.9f)), "", g_sector);
    g_bodyFrame->SetData(&g_bodyData);
    g_carFrame = NewFrame(FRAME_MODEL, "carModel", S_vector(24, 1, 22), Identity(), Box(S_vector(1, 1, 2)), "", g_sector);
    I3D_frame *handle = NewFrame(FRAME_DUMMY, "klika", S_vector(23, 1.1f, 21), Identity(), Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_carFrame);
    g_names[handle] = "klika";
    UpdateTree(g_carFrame);
    // (The vehicle's frame: the port's field and the original's +0x20.)
    reinterpret_cast<C_Actor *>(g_carBytes)->m_frame = g_carFrame;
    *reinterpret_cast<I3D_frame **>(g_carBytes + 0x20) = g_carFrame;
    std::printf("player: started\n");
    for (int i = 0; i < 60000 / QUICK; ++i) {
        RandomPlayerHuman();
        RandomStarted();
        switch (i % 3) {
        case 0: {
            // (A group with a table: without one the original reads near null; the port's documented fix.)
            g_h.work->m_group = I(1, 2);
            g_humanAnimTable = reinterpret_cast<C_HumanAnimTable *>(g_animTableBytes[0]);
            g_humanAnimTable2 = reinterpret_cast<C_HumanAnimTable *>(g_animTableBytes[1]);
            *OriginalAt<void **>(O_HUMAN_ANIM_TABLE) = g_humanAnimTable;
            *OriginalAt<void **>(O_HUMAN_ANIM_TABLE2) = g_humanAnimTable2;
            // (A door's animation with a vehicle: without one the original calls through null; the port's documented
            // fix.)
            if (PortStatic<S_HumanAnimRef>("g_curAnim").anim >= 0 && PortStatic<S_HumanAnimRef>("g_curAnim").anim <= 3)
                m->m_vehicle = reinterpret_cast<C_Actor *>(g_carBytes);
            // (A body picked up names its actor: without one the original reads through null; the port's documented
            // fix.)
            if (PortStatic<S_HumanAnimRef>("g_curAnim").anim == 0x5E && g_h.work->m_request.workingFrame) {
                g_h.work->m_request.workingFrame = g_bodyFrame;
                g_bodyData.actor = g_h.human;
            }
            const bool single = P(0.6f);
            DiffPlayer("OnAnimStarted",
                       [&] {
                           p.OnAnimStarted(PortStatic<S_HumanAnimRef>("g_curAnim"), PortStatic<S_HumanAnimRef>("g_oldAnim"), single);
                           return 0;
                       },
                       [&](void *self) {
                           return WithOriginalBody([&] {
                               OriginalAt<T_PStarted>(0x0077a2c0)(self, &OrigStatic<S_HumanAnimRef>("g_curAnim"),
                                                                  &OrigStatic<S_HumanAnimRef>("g_oldAnim"), single);
                               return 0;
                           });
                       });
            break;
        }
        case 1:
            // (Started only with a carry of the table: UpdateCarryTrack's set comes from its slot.)
            while ((&PortStatic<int>("g_curCarry"))[0] * 3 + (&PortStatic<int>("g_curCarry"))[1] >= C_CarryAnimTable::SLOT_COUNT)
                RandomCarry(&PortStatic<int>("g_curCarry"));
            DiffPlayer("OnCarryStarted", [&] { p.OnCarryStarted(&PortStatic<int>("g_curCarry"), &PortStatic<int>("g_oldCarry")); return 0; },
                       [&](void *self) {
                           OriginalAt<T_PStarted2>(0x0077bcd0)(self, &OrigStatic<int>("g_curCarry"), &OrigStatic<int>("g_oldCarry"));
                           return 0;
                       });
            break;
        default:
            // (Started only with an activity of the table: UpdateActivityTrack's set comes from its slot.)
            while (PortStatic<int>("g_curActivity") >= C_ActivityAnimTable::IDS)
                RandomActivity(&PortStatic<int>("g_curActivity"));
            DiffPlayer("OnActivityStarted",
                       [&] { p.OnActivityStarted(&PortStatic<int>("g_curActivity"), &PortStatic<int>("g_oldActivity")); return 0; },
                       [&](void *self) {
                           OriginalAt<T_PStarted2>(0x0077c5a0)(self, &OrigStatic<int>("g_curActivity"), &OrigStatic<int>("g_oldActivity"));
                           return 0;
                       });
            break;
        }
    }
}

// ---- 8c: the update and its tracks
// What the update's tests keep to: every table present and a group with one (the original reads near null without;
// the port's documented fixes), a vehicle with its door handle and a body with its actor for the animations that use
// them (likewise), room for an event on each track (the original writes a sixth over the count).
void RandomPlayerUpdate(bool whole)
{
    C_HumanMovement *m = g_h.movement;
    RandomStarted();
    g_humanAnimTable = reinterpret_cast<C_HumanAnimTable *>(g_animTableBytes[0]);
    g_humanAnimTable2 = reinterpret_cast<C_HumanAnimTable *>(g_animTableBytes[1]);
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i)
        g_carryAnimTables[i] = reinterpret_cast<C_CarryAnimTable *>(g_carryTableBytes[i]);
    *OriginalAt<void **>(O_HUMAN_ANIM_TABLE) = g_humanAnimTable;
    *OriginalAt<void **>(O_HUMAN_ANIM_TABLE2) = g_humanAnimTable2;
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i)
        OriginalAt<void **>(O_CARRY_ANIM_TABLES)[i] = g_carryAnimTables[i];
    PortStatic<const C_HumanAnimTable *>("g_animTable") = P(0.5f) ? g_humanAnimTable : g_humanAnimTable2;
    g_h.work->m_group = I(1, 2);
    m->m_vehicle = reinterpret_cast<C_Actor *>(g_carBytes);
    // (The ladder animations' velocity, the locomotion's UpdateVelocity: with no ladder in use, or up the ladder with
    // the body track held, the original passes an uninitialised vector; the locomotion's documented fixes.)
    m->m_useObject = reinterpret_cast<C_Actor *>(Ladder()->port);
    const auto ladderUp = [](const S_HumanAnimRef &r) { return r.anim == 0x6A; };
    bool up = ladderUp(m->m_nextAnimState.anim) || ladderUp(m->m_animPlayer.m_pendingAnim) ||
              ladderUp(PortStatic<S_HumanAnimRef>("g_nextAnim"));
    for (const S_HumanAnimRef &e : m->m_mixer.m_animQueue.entries)
        up = up || ladderUp(e);
    if (up && m->m_mixer.m_tracks[0].hold > 0)
        m->m_mixer.m_tracks[0].hold = 0;
    if (g_h.work->m_request.workingFrame)
        g_h.work->m_request.workingFrame = g_bodyFrame;
    g_bodyData.actor = g_h.human;
    for (S_MixerTrack &t : m->m_mixer.m_tracks) {
        if (t.eventCount == MIXER_TRACK_EVENTS)
            t.eventCount = I(0, MIXER_TRACK_EVENTS - 1);
        // (A track without an animation is not held: ReleaseTrack of one is the mixer's documented fix, the original
        // writing the rate of the stage before the pair.)
        if (t.current == -1)
            t.hold = 0;
    }
    // (The activity's stages now and then finished with the tags of OnActivityStarted's props.)
    for (int s = 4; s < 6; ++s) {
        if (P(0.5f)) {
            m->m_mixer.m_stages[s].tag = I(0x50, 0x55);
            m->m_mixer.m_stages[s].finished = P(0.7f);
        }
    }
    // (A track test starts as Update leaves it: no set chosen yet, none to play. Update itself clears them.)
    if (!whole) {
        PortStatic<I3D_animation_set *>("g_animSet") = nullptr;
        PortStatic<I3D_animation_set *>("g_carrySet") = nullptr;
        PortStatic<I3D_animation_set *>("g_activitySet") = nullptr;
        for (int i = 0; i < MIXER_TRACKS; ++i) {
            (&PortStatic<S_MixerAnim>("g_anims"))[i].anim = nullptr;
            (&PortStatic<S_MixerAnim>("g_startAnims"))[i].anim = nullptr;
        }
    }
    // The arm joints the hit activities tell the model's parts about (NotifyFrames): now and then a part's frame.
    for (int i = 0; i < 8; ++i)
        m->m_joints[8 + i].frame = P(0.4f) ? g_parts[I(0, MODEL_PARTS - 1)].frame : g_jointFrame[8 + i];
    // (The requests now and then the current state's, so that the "same" branches run.)
    if (P(0.2f))
        m->m_nextAnimState.anim = m->m_animState.anim;
    if (P(0.2f)) {
        m->m_nextAnimState.carryAnim = m->m_animState.carryAnim;
        m->m_nextAnimState.carryVariant = m->m_animState.carryVariant;
    }
    if (P(0.2f)) {
        m->m_nextAnimState.activity = m->m_animState.activity;
        m->m_nextAnimState.activityVariant = m->m_animState.activityVariant;
    }
}

void TestPlayerUpdate()
{
    C_HumanAnimPlayer &p = g_h.movement->m_animPlayer;
    std::printf("player: update\n");
    for (int i = 0; i < 80000 / QUICK; ++i) {
        RandomPlayerHuman();
        RandomPlayerUpdate(i % 4 == 0);
        const auto original = [&](uint32_t at) {
            return [at](void *self) {
                return WithOriginalBody([&] {
                    OriginalAt<T_MVoid>(at)(self);
                    return 0;
                });
            };
        };
        switch (i % 4) {
        case 0:
            DiffPlayer("Update", [&] { p.Update(); return 0; }, original(0x00779f90));
            break;
        case 1:
            DiffPlayer("UpdateAnimTrack", [&] { p.UpdateAnimTrack(); return 0; }, original(0x00777f30));
            break;
        case 2:
            DiffPlayer("UpdateCarryTrack", [&] { p.UpdateCarryTrack(); return 0; }, original(0x00778f80));
            break;
        default:
            DiffPlayer("UpdateActivityTrack", [&] { p.UpdateActivityTrack(); return 0; }, original(0x00779850));
            break;
        }
    }
}

void TestPlayer()
{
    SetUpPlayer();
    TestPlayerSmall();
    TestPlayerSaves();
    TestPlayerStarted();
    TestPlayerUpdate();
}
}  // namespace
