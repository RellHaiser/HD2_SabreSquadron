// Part 26 of the human harness (HUMAN_BODYWRAP builds, build_bodywrap.bat; included by human_test.cpp after
// locomotion3_test.inc): S_HumanBody (game/human_body.cpp), the body that forwards to the human's work, movement and
// equipment, against the original's (0x005ab330..0x005ac4c0) on the human of the base harness (its m_body: the port's
// work and movement, the original's copies, one equipment block both read).
//
// What the body calls is logged on both sides alike: the port's calls through ld --wrap (every out-of-line method of
// the work, movement, equipment and their parts that human_body.o calls, the table lengths, the camera module), the
// original's through redirects of the same functions. Its inline parts run on each side's objects (the work's flags,
// Die, the movement's flags and fields) and DiffCall compares the objects after. Create and Destroy run on bodies of
// their own (the original's allocations come from the test's blocks).
int g_localPlayerSlot = 99;                             // (game/human.cpp's, not linked here; the original's 0x00982104)
namespace {

#define BW __attribute__((thiscall))

// What the faked functions answer and do (both sides).
struct S_BodyScript {
    bool on = false;                                    // logging (the wrappers do nothing otherwise)
    bool leftFoot = false;                              // IsLeftFootForward
    uint32_t refusedAfter = 0;                          // the work's refused requests after its Interact
    int animLength = 0;                                 // the table lengths
    int useObject = 0;                                  // UseObject's answer
} g_bs;

alignas(16) uint8_t g_cameraBytes[0x100];               // the camera module (both sides)
void *g_cameraVt[64];

// Allocations of the original's Create (by size) and their names.
alignas(16) uint8_t g_newWork[0x100], g_newMovement[0xE00], g_newEquipment[0x400];

void BLog(const void *self, const std::string &what, std::initializer_list<double> nums = {})
{
    if (g_bs.on)
        Log(self, what.c_str(), nums);
}
void BLogV(const void *self, const char *what, const float *v, int n, std::initializer_list<double> more = {})
{
    if (g_bs.on)
        LogV(self, what, v, n, more);
}

// The movement's flags (+0x22C in the original), cleared by ClearFlags on either side.
uint32_t &OrigMovementFlags(void *movement) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(movement) + 0x22C); }

// The chunk tags the sub-objects' saves write (and their loads read back).
const uint32_t TAG_WORK = 0x4B524F57, TAG_MOVEMENT = 0x45564F4D, TAG_EQUIPMENT = 0x50495145;
void SaveTag(C_chunk &c, uint32_t tag) { c.Write(&tag, 4); }
uint32_t LoadTag(C_chunk &c)
{
    uint32_t tag = 0;
    if (c.RemainingSize() >= 4)
        c.Read(&tag, 4);
    return tag;
}

// ---- the original's side: loggers in place of what its body calls (thiscall unless said)
// (The constructors answer `this`: the original's Create keeps what they return.)
void *BW OW_New(void *self)
{
    BLog(nullptr, "new " + NameOf(self));
    return self;
}
void BW OW_SetMovement(void *self, void *m) { BLog(self, "SetMovement " + NameOf(m)); }
void BW OW_SetGroup(void *self, int g) { BLog(self, "SetGroup", {double(g)}); }
void BW OW_Reset(void *self) { BLog(self, "Reset"); }
// (With the work's m_forced, +0xB0: PlayDeath raises it around its Play.)
void BW OW_Play(void *self, int a, bool on) { BLog(self, "Play", {double(a), double(on), double(static_cast<uint8_t *>(self)[0xB0])}); }
void BW OW_PlayWorkAnimation(void *self, int a, bool on) { BLog(self, "PlayWorkAnimation", {double(a), double(on)}); }
void BW OW_SetGait(void *self, int a, bool on) { BLog(self, "SetGait", {double(a), double(on)}); }
void BW OW_SetStance(void *self, int a, bool on) { BLog(self, "SetStance", {double(a), double(on)}); }
void BW OW_SetLookAt(void *self, const float *v, bool on) { BLogV(self, "SetLookAt", v, 4, {double(on)}); }
void BW OW_DoAction(void *self, int a, bool on) { BLog(self, "DoAction", {double(a), double(on)}); }
void BW OW_SetUnknown5C(void *self, bool v, bool on) { BLog(self, "SetUnknown5C", {double(v), double(on)}); }
void BW OW_SetFacingDir(void *self, const float *v, bool on) { BLogV(self, "SetFacingDir", v, 3, {double(on)}); }
void BW OW_SetViewDir(void *self, const float *v) { BLogV(self, "SetViewDir", v, 3); }
void BW OW_SetWorkingFrame(void *self, void *f, bool on) { BLog(self, "SetWorkingFrame " + NameOf(f), {double(on)}); }
void BW OW_SetWorkingFrame2(void *self, void *f, bool on) { BLog(self, "SetWorkingFrame2 " + NameOf(f), {double(on)}); }
void BW OW_ApplyHit(void *self, uint32_t flags, const float *pos, const float *force)
{
    BLog(self, "ApplyHit", {double(flags), pos[0], pos[1], pos[2], force[0], force[1], force[2]});
}
void BW OW_Interact(void *self, void *target, uint32_t interaction, int arg, bool on)
{
    BLog(self, "Interact " + NameOf(target), {double(interaction), double(arg), double(on)});
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 0xC8) = g_bs.refusedAfter;
}
void BW OW_SetLook(void *self, uint32_t flags, const float *at)
{
    if (at)
        BLogV(self, "SetLook", at, 4, {double(flags)});
    else
        BLog(self, "SetLook null", {double(flags)});
}
bool BW OW_WorkSave(void *self, C_chunk *c)
{
    BLog(self, "SaveGame");
    SaveTag(*c, TAG_WORK);
    return true;
}
bool BW OW_WorkLoad(void *self, C_chunk *c)
{
    BLog(self, "LoadGame", {double(LoadTag(*c))});
    return true;
}
bool BW OW_MovementSave(void *self, C_chunk *c)
{
    BLog(self, "SaveGame");
    SaveTag(*c, TAG_MOVEMENT);
    return true;
}
bool BW OW_MovementLoad(void *self, C_chunk *c)
{
    BLog(self, "LoadGame", {double(LoadTag(*c))});
    return true;
}
bool BW OW_EquipmentSave(void *self, C_chunk *c)
{
    BLog(self, "SaveGame");
    SaveTag(*c, TAG_EQUIPMENT);
    return true;
}
bool BW OW_EquipmentLoad(void *self, C_chunk *c)
{
    BLog(self, "LoadGame", {double(LoadTag(*c))});
    return true;
}
void BW OW_ClearFlags(void *self, uint32_t f)
{
    BLog(self, "ClearFlags", {double(f)});
    OrigMovementFlags(self) &= ~f;
}
void BW OW_OnFrozen(void *self, bool on) { BLog(self, "OnFrozen", {double(on)}); }
void BW OW_Unknown7865c0(void *self, int a) { BLog(self, "Unknown7865c0", {double(a)}); }
void BW OW_UpdatePhysics(void *self, int t) { BLog(self, "UpdatePhysics", {double(t)}); }
void BW OW_UpdateAnimation(void *self, int t) { BLog(self, "UpdateAnimation", {double(t)}); }
void BW OW_RemoveCollision(void *self, bool b) { BLog(self, "RemoveCollision", {double(b)}); }
void BW OW_RestoreCollision(void *self, bool b) { BLog(self, "RestoreCollision", {double(b)}); }
void BW OW_SetPlayerControlled(void *self, bool b) { BLog(self, "SetPlayerControlled", {double(b)}); }
void BW OW_Done(void *self) { BLog(self, "Done"); }
void BW OW_Init(void *self) { BLog(self, "Init"); }
void BW OW_Ragdoll(void *self, bool a, bool b) { BLog(self, "Ragdoll", {double(a), double(b)}); }
void BW OW_SetOwner(void *self, void *h) { BLog(self, "SetOwner " + NameOf(h)); }
int BW OW_UseObject(void *self, void *a)
{
    BLog(self, "UseObject " + NameOf(a));
    return g_bs.useObject;
}
bool BW OW_IsLeftFootForward(void *self)
{
    BLog(self, "IsLeftFootForward");
    return g_bs.leftFoot;
}
void BW OW_ShowDeathFace(void *self) { BLog(self, "ShowDeathFace"); }
void BW OW_IdleInteract(void *self, void *target, uint32_t interaction, int arg)
{
    BLog(self, "Interact " + NameOf(target), {double(interaction), double(arg)});
}
void BW OW_PlayAnimation(void *self, const char *name, int a, int b, int c)
{
    BLog(self, std::string("PlayAnimation ") + (name ? name : "null"), {double(a), double(b), double(c)});
}
void *BW OW_EquipmentNew(void *self, void *owner)
{
    BLog(nullptr, "new " + NameOf(self) + " " + NameOf(owner));
    return self;
}
void BW OW_Delete(void *self) { BLog(self, "delete"); }
void BW OW_Unknown7938f0(void *self, int a) { BLog(self, "Unknown7938f0", {double(a)}); }
void BW OW_EquipmentUpdate(void *self, int t) { BLog(self, "Update", {double(t)}); }
int __stdcall OW_ActivityAnimLength(int id, int variant)
{
    BLog(nullptr, "ActivityAnimLength", {double(id), double(variant)});
    return g_bs.animLength;
}
int __stdcall OW_CarryAnimLength(int id, int variant, int mode)
{
    BLog(nullptr, "CarryAnimLength", {double(id), double(variant), double(mode)});
    return g_bs.animLength;
}
void *BW OW_GetCameraModule(void *) { return g_cameraBytes; }
void BW OW_CameraSetBlendTime(void *self, int time) { BLog(self, "SetBlendTime", {double(time)}); }
void *__cdecl OW_AlignedMalloc(size_t size, size_t)
{
    void *p = size <= sizeof g_newWork ? static_cast<void *>(g_newWork) : size <= sizeof g_newEquipment ? static_cast<void *>(g_newEquipment)
                                                                                                        : static_cast<void *>(g_newMovement);
    BLog(nullptr, "alloc " + NameOf(p));
    return p;
}
void __cdecl OW_AlignedFree(void *) {}
}  // namespace

// ---- the port's side: the same loggers through ld --wrap
extern "C" {
void BW __wrap__ZN11C_HumanWorkC1Ev(C_HumanWork *self)
{
    std::memset(static_cast<void *>(self), 0, sizeof(C_HumanWork));
    g_names[self] = "newWork";
    BLog(nullptr, "alloc newWork");
    OW_New(self);
}
void BW __wrap__ZN11C_HumanWork11SetMovementEP15C_HumanMovement(C_HumanWork *s, C_HumanMovement *m) { OW_SetMovement(s, m); }
void BW __wrap__ZN11C_HumanWork8SetGroupEi(C_HumanWork *s, int g) { OW_SetGroup(s, g); }
void BW __wrap__ZN11C_HumanWork5ResetEv(C_HumanWork *s) { OW_Reset(s); }
void BW __wrap__ZN11C_HumanWork4PlayEib(C_HumanWork *s, int a, bool on) { OW_Play(s, a, on); }
void BW __wrap__ZN11C_HumanWork17PlayWorkAnimationEib(C_HumanWork *s, int a, bool on) { OW_PlayWorkAnimation(s, a, on); }
void BW __wrap__ZN11C_HumanWork7SetGaitEib(C_HumanWork *s, int a, bool on) { OW_SetGait(s, a, on); }
void BW __wrap__ZN11C_HumanWork9SetStanceEib(C_HumanWork *s, int a, bool on) { OW_SetStance(s, a, on); }
void BW __wrap__ZN11C_HumanWork9SetLookAtERK9S_vector4b(C_HumanWork *s, const S_vector4 &v, bool on) { OW_SetLookAt(s, &v.x, on); }
void BW __wrap__ZN11C_HumanWork8DoActionEib(C_HumanWork *s, int a, bool on) { OW_DoAction(s, a, on); }
void BW __wrap__ZN11C_HumanWork12SetUnknown5CEbb(C_HumanWork *s, bool v, bool on) { OW_SetUnknown5C(s, v, on); }
void BW __wrap__ZN11C_HumanWork12SetFacingDirERK8S_vectorb(C_HumanWork *s, const S_vector &v, bool on) { OW_SetFacingDir(s, &v.x, on); }
void BW __wrap__ZN11C_HumanWork10SetViewDirERK8S_vector(C_HumanWork *s, const S_vector &v) { OW_SetViewDir(s, &v.x); }
void BW __wrap__ZN11C_HumanWork15SetWorkingFrameEP9I3D_frameb(C_HumanWork *s, I3D_frame *f, bool on) { OW_SetWorkingFrame(s, f, on); }
void BW __wrap__ZN11C_HumanWork16SetWorkingFrame2EP9I3D_frameb(C_HumanWork *s, I3D_frame *f, bool on) { OW_SetWorkingFrame2(s, f, on); }
void BW __wrap__ZN11C_HumanWork8ApplyHitEjRK8S_vectorS2_(C_HumanWork *s, uint32_t flags, const S_vector &p, const S_vector &f)
{
    OW_ApplyHit(s, flags, &p.x, &f.x);
}
void BW __wrap__ZN11C_HumanWork8InteractEP7C_Actorjib(C_HumanWork *s, C_Actor *t, uint32_t i, int a, bool on) { OW_Interact(s, t, i, a, on); }
void BW __wrap__ZN11C_HumanWork7SetLookEjPK9S_vector4(C_HumanWork *s, uint32_t flags, const S_vector4 *at)
{
    OW_SetLook(s, flags, at ? &at->x : nullptr);
}
bool BW __wrap__ZN11C_HumanWork8SaveGameER7C_chunk(C_HumanWork *s, C_chunk &c) { return OW_WorkSave(s, &c); }
bool BW __wrap__ZN11C_HumanWork8LoadGameER7C_chunk(C_HumanWork *s, C_chunk &c) { return OW_WorkLoad(s, &c); }

void BW __wrap__ZN15C_HumanMovementC1Ev(C_HumanMovement *self)
{
    std::memset(static_cast<void *>(self), 0xCD, sizeof(C_HumanMovement));   // (not 0: what Create writes shows)
    g_names[self] = "newMovement";
    BLog(nullptr, "alloc newMovement");
    OW_New(self);
}
void BW __wrap__ZN15C_HumanMovementD1Ev(C_HumanMovement *s) { OW_Delete(s); }
void BW __wrap__ZN15C_HumanMovement10ClearFlagsEj(C_HumanMovement *s, uint32_t f)
{
    BLog(s, "ClearFlags", {double(f)});
    s->m_flags &= ~f;
}
void BW __wrap__ZN15C_HumanMovement8OnFrozenEb(C_HumanMovement *s, bool on) { OW_OnFrozen(s, on); }
void BW __wrap__ZN15C_HumanMovement13Unknown7865c0Ei(C_HumanMovement *s, int a) { OW_Unknown7865c0(s, a); }
void BW __wrap__ZN15C_HumanMovement13UpdatePhysicsEi(C_HumanMovement *s, int t) { OW_UpdatePhysics(s, t); }
void BW __wrap__ZN15C_HumanMovement15UpdateAnimationEi(C_HumanMovement *s, int t) { OW_UpdateAnimation(s, t); }
void BW __wrap__ZN15C_HumanMovement15RemoveCollisionEb(C_HumanMovement *s, bool b) { OW_RemoveCollision(s, b); }
void BW __wrap__ZN15C_HumanMovement16RestoreCollisionEb(C_HumanMovement *s, bool b) { OW_RestoreCollision(s, b); }
void BW __wrap__ZN15C_HumanMovement19SetPlayerControlledEb(C_HumanMovement *s, bool b) { OW_SetPlayerControlled(s, b); }
void BW __wrap__ZN15C_HumanMovement15SetModelVisibleEb(C_HumanMovement *s, bool b) { BLog(s, "SetModelVisible", {double(b)}); }
void BW __wrap__ZN15C_HumanMovement4DoneEv(C_HumanMovement *s) { OW_Done(s); }
void BW __wrap__ZN15C_HumanMovement4InitEv(C_HumanMovement *s) { OW_Init(s); }
void BW __wrap__ZN15C_HumanMovement5ResetEv(C_HumanMovement *s) { OW_Reset(s); }
void BW __wrap__ZN15C_HumanMovement7RagdollEbb(C_HumanMovement *s, bool a, bool b) { OW_Ragdoll(s, a, b); }
void BW __wrap__ZN15C_HumanMovement8SetOwnerEP7C_Human(C_HumanMovement *s, C_Human *h) { OW_SetOwner(s, h); }
int BW __wrap__ZN15C_HumanMovement9UseObjectEP7C_Actor(C_HumanMovement *s, C_Actor *a) { return OW_UseObject(s, a); }
bool BW __wrap__ZNK15C_HumanMovement17IsLeftFootForwardEv(const C_HumanMovement *s) { return OW_IsLeftFootForward(const_cast<C_HumanMovement *>(s)); }
bool BW __wrap__ZN15C_HumanMovement8SaveGameER7C_chunk(C_HumanMovement *s, C_chunk &c) { return OW_MovementSave(s, &c); }
bool BW __wrap__ZN15C_HumanMovement8LoadGameER7C_chunk(C_HumanMovement *s, C_chunk &c) { return OW_MovementLoad(s, &c); }
void BW __wrap__ZN12C_HumanMorph13ShowDeathFaceEv(void *s) { OW_ShowDeathFace(s); }
void BW __wrap__ZN11C_HumanIdle8InteractEP7C_Actorji(void *s, C_Actor *t, uint32_t i, int a) { OW_IdleInteract(s, t, i, a); }
void BW __wrap__ZN17C_HumanAnimPlayer13PlayAnimationEPKciii(void *s, const char *name, int a, int b, int c) { OW_PlayAnimation(s, name, a, b, c); }

void BW __wrap__ZN16C_HumanEquipmentC1EP7C_Human(C_HumanEquipment *self, C_Human *owner)
{
    std::memset(static_cast<void *>(self), 0, sizeof(C_HumanEquipment));
    g_names[self] = "newEquipment";
    BLog(nullptr, "alloc newEquipment");
    OW_EquipmentNew(self, owner);
}
void BW __wrap__ZN16C_HumanEquipmentD1Ev(C_HumanEquipment *s) { OW_Delete(s); }
void BW __wrap__ZN16C_HumanEquipment4DoneEv(C_HumanEquipment *s) { OW_Done(s); }
void BW __wrap__ZN16C_HumanEquipment4InitEv(C_HumanEquipment *s) { OW_Init(s); }
void BW __wrap__ZN16C_HumanEquipment5ResetEv(C_HumanEquipment *s) { OW_Reset(s); }
void BW __wrap__ZN16C_HumanEquipment6UpdateEi(C_HumanEquipment *s, int t) { OW_EquipmentUpdate(s, t); }
void BW __wrap__ZN16C_HumanEquipment13Unknown7938f0Ei(C_HumanEquipment *s, int a) { OW_Unknown7938f0(s, a); }
bool BW __wrap__ZN16C_HumanEquipment8SaveGameER7C_chunk(C_HumanEquipment *s, C_chunk &c) { return OW_EquipmentSave(s, &c); }
bool BW __wrap__ZN16C_HumanEquipment8LoadGameER7C_chunk(C_HumanEquipment *s, C_chunk &c) { return OW_EquipmentLoad(s, &c); }

int __wrap__Z21GetActivityAnimLengthii(int id, int variant) { return OW_ActivityAnimLength(id, variant); }
int __wrap__Z18GetCarryAnimLengthiii(int id, int variant, int mode) { return OW_CarryAnimLength(id, variant, mode); }
C_CameraModule *BW __wrap__ZN6C_game15GetCameraModuleEv(C_game *) { return reinterpret_cast<C_CameraModule *>(g_cameraBytes); }
void BW __wrap__ZN14C_CameraModule12SetBlendTimeEi(C_CameraModule *s, int time) { OW_CameraSetBlendTime(s, time); }
}  // extern "C"

namespace {
// The redirects of the original's body callees.
void InstallBodyRedirects()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0077e1e0, reinterpret_cast<const void *>(OW_New)},             // C_HumanWork()
        {0x0077e200, reinterpret_cast<const void *>(OW_SetMovement)},
        {0x0077e220, reinterpret_cast<const void *>(OW_SetGroup)},
        {0x0077e280, reinterpret_cast<const void *>(OW_Reset)},           // the work's
        {0x0077f060, reinterpret_cast<const void *>(OW_Play)},
        {0x0077f4b0, reinterpret_cast<const void *>(OW_PlayWorkAnimation)},
        {0x0077f500, reinterpret_cast<const void *>(OW_SetStance)},
        {0x0077f5f0, reinterpret_cast<const void *>(OW_SetGait)},
        {0x0077f6c0, reinterpret_cast<const void *>(OW_SetLookAt)},
        {0x0077f7e0, reinterpret_cast<const void *>(OW_DoAction)},
        {0x0077f830, reinterpret_cast<const void *>(OW_SetUnknown5C)},
        {0x0077f8c0, reinterpret_cast<const void *>(OW_SetFacingDir)},
        {0x0077faf0, reinterpret_cast<const void *>(OW_SetViewDir)},
        {0x0077fb70, reinterpret_cast<const void *>(OW_SetWorkingFrame)},
        {0x0077fbc0, reinterpret_cast<const void *>(OW_SetWorkingFrame2)},
        {0x0077fc10, reinterpret_cast<const void *>(OW_ApplyHit)},
        {0x00780010, reinterpret_cast<const void *>(OW_Interact)},
        {0x00780090, reinterpret_cast<const void *>(OW_SetLook)},
        {0x00780280, reinterpret_cast<const void *>(OW_WorkSave)},
        {0x00780560, reinterpret_cast<const void *>(OW_WorkLoad)},
        {0x00785a20, reinterpret_cast<const void *>(OW_New)},             // C_HumanMovement()
        {0x00785db0, reinterpret_cast<const void *>(OW_Delete)},          // ~C_HumanMovement
        {0x00785f80, reinterpret_cast<const void *>(OW_SetOwner)},
        {0x00786040, reinterpret_cast<const void *>(OW_Init)},            // the movement's
        {0x00786490, reinterpret_cast<const void *>(OW_Done)},            // the movement's
        {0x00784db0, reinterpret_cast<const void *>(OW_Reset)},           // the movement's
        {0x00784430, reinterpret_cast<const void *>(OW_MovementSave)},
        {0x00784770, reinterpret_cast<const void *>(OW_MovementLoad)},
        {0x007865c0, reinterpret_cast<const void *>(OW_Unknown7865c0)},
        {0x00786600, reinterpret_cast<const void *>(OW_UseObject)},
        {0x007872b0, reinterpret_cast<const void *>(OW_Ragdoll)},
        {0x00787530, reinterpret_cast<const void *>(OW_SetPlayerControlled)},
        {0x007876d0, reinterpret_cast<const void *>(OW_UpdateAnimation)},
        {0x007881b0, reinterpret_cast<const void *>(OW_UpdatePhysics)},
        {0x0075c730, reinterpret_cast<const void *>(OW_OnFrozen)},
        {0x0075cbd0, reinterpret_cast<const void *>(OW_ClearFlags)},
        {0x0075cdc0, reinterpret_cast<const void *>(OW_RemoveCollision)},
        {0x0075cf20, reinterpret_cast<const void *>(OW_RestoreCollision)},
        {0x0075ea70, reinterpret_cast<const void *>(OW_IsLeftFootForward)},
        {0x0075d120, reinterpret_cast<const void *>(OW_ActivityAnimLength)},
        {0x0075d090, reinterpret_cast<const void *>(OW_CarryAnimLength)},
        {0x00781770, reinterpret_cast<const void *>(OW_ShowDeathFace)},
        {0x007518e0, reinterpret_cast<const void *>(OW_IdleInteract)},
        {0x0077d270, reinterpret_cast<const void *>(OW_PlayAnimation)},
        {0x0078ce60, reinterpret_cast<const void *>(OW_EquipmentNew)},
        {0x0078ce90, reinterpret_cast<const void *>(OW_Delete)},          // ~C_HumanEquipment
        {0x0078cec0, reinterpret_cast<const void *>(OW_Init)},            // the equipment's
        {0x0078e290, reinterpret_cast<const void *>(OW_Done)},            // the equipment's
        {0x00793140, reinterpret_cast<const void *>(OW_EquipmentUpdate)},
        {0x007938f0, reinterpret_cast<const void *>(OW_Unknown7938f0)},
        {0x00793d10, reinterpret_cast<const void *>(OW_Reset)},           // the equipment's
        {0x00793d20, reinterpret_cast<const void *>(OW_EquipmentSave)},
        {0x00794890, reinterpret_cast<const void *>(OW_EquipmentLoad)},
        {0x0069a620, reinterpret_cast<const void *>(OW_GetCameraModule)},
        {0x007e53c2, reinterpret_cast<const void *>(OW_AlignedMalloc)},   // _aligned_malloc (import thunk)
        {0x007e559c, reinterpret_cast<const void *>(OW_AlignedFree)},     // _aligned_free
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // The camera module: the original calls its SetBlendTime through slot 0x70.
    for (void *&slot : g_cameraVt)
        slot = nullptr;
    g_cameraVt[0x70 / 4] = reinterpret_cast<void *>(OW_CameraSetBlendTime);
    *reinterpret_cast<void ***>(g_cameraBytes) = g_cameraVt;
    g_names[g_cameraBytes] = "camera";
    g_names[g_newWork] = "newWork";
    g_names[g_newMovement] = "newMovement";
    g_names[g_newEquipment] = "newEquipment";
    g_names[&g_h.movement->m_idle] = g_names[o_movement + 0xCB0] = "idle";
}

S_HumanBody &PortBody() { return g_h.human->m_body; }
void *OrigBody() { return o_human + 0x98; }
template <class R, class... A> R BCall(uint32_t at, void *self, A... a)
{
    typedef R(BW * T)(void *, A...);
    return OriginalAt<T>(at)(self, a...);
}
uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
int Bits(float f)
{
    int b;
    std::memcpy(&b, &f, 4);
    return b;
}

// The state the body reads: the game's role, the human's (local player, simulated, carried, its net id), the work's,
// the movement's fields, the equipment's flags.
const uint32_t BW_ROLES[] = {7, 1, 2, 3, 5, 6};
C_Actor *g_bwActors[3];
void RandomBodyState()
{
    g_gameRole = BW_ROLES[I(0, 5)];
    g_script.localPlayer = P(0.5f);
    // (ToOriginal gives the original's the same: the human's id, or 99.)
    g_localPlayerSlot = g_script.localPlayer ? g_h.human->m_netId : 99;
    g_script.serverSimulated = P(0.3f);
    g_script.activeIsOwner = P(0.5f);
    C_Human *h = g_h.human;
    h->m_carried = P(0.3f) ? g_bwActors[I(0, 2)] : nullptr;
    S_HumanBody &b = PortBody();
    b.crippled = P(0.3f);
    b.group = I(0, 5);
    C_HumanWork *w = g_h.work;
    w->m_request.stance = I(0, 4);
    w->m_request.gait = I(0, 6);
    w->m_mode = P(0.3f) ? 5 : I(0, 8);
    w->m_refused = P(0.5f) ? 0u : uint32_t(I(0, 0xFFFF)) | (P(0.5f) ? 0x4000u : 0u);
    w->m_bodyFlags = uint32_t(std::rand()) * 65599u + uint32_t(std::rand());
    w->m_forced = P(0.5f);
    C_HumanMovement *m = g_h.movement;
    m->m_flags = uint32_t(I(0, 0xFFFF)) | (P(0.3f) ? 8u : 0u);
    static const int STATES[] = {0, 8, 0x0E, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 3, 0x20};
    m->m_animState.anim.anim = STATES[I(0, 9)];
    m->m_useObject = P(0.5f) ? g_bwActors[I(0, 2)] : nullptr;
    m->m_unknown1AC = P(0.5f) ? 0 : I(1, 3);
    m->m_unknown1B0 = I(0, 3);
    m->m_unknown18C = P(0.5f);
    m->m_unknown9C0 = P(0.5f) ? 0 : I(1, 900);
    m->m_visual.m_lighting.m_intensity = U(0, 1.5f);
    m->m_visual.m_lighting.m_level = U(0, 1.5f);
    m->m_ground = P(0.3f) ? nullptr : reinterpret_cast<decltype(m->m_ground)>(g_bwActors[I(0, 2)]);
    m->m_visual.m_sector = P(0.3f) ? nullptr : reinterpret_cast<I3D_sector *>(g_bwActors[I(0, 2)]);
    m->m_stanceVolumeCount = I(0, 4);
    // The velocity: zero, tiny, exactly unit length, or any.
    const int v = I(0, 4);
    S_vector vel = v == 0 ? S_vector(0, 0, 0) : v == 1 ? S_vector(U(-1e-5f, 1e-5f), 0, U(-1e-5f, 1e-5f)) : v == 2 ? S_vector(0, 0, 1)
                                                                                         : S_vector(U(-6, 6), U(-2, 2), U(-6, 6));
    std::memcpy(&m->m_locomotion.m_velocity, &vel, 12);
    uint32_t equipmentFlags = uint32_t(I(0, 0xFFFF));
    std::memcpy(g_h.equipment + 0x50, &equipmentFlags, 4);
    g_bs.leftFoot = P(0.5f);
    g_bs.refusedAfter = P(0.5f) ? 0u : (P(0.5f) ? 0x4000u : uint32_t(I(0, 0x3FFF)));
    g_bs.animLength = I(-1, 3000);
    g_bs.useObject = I(0, 2);
}

// One body call on both sides through DiffCall (the logs, the objects after).
template <class P, class O> void BodyCall(const char *what, P port, O original)
{
    g_bs.on = true;
    DiffCall(what, port, [&](void *) { return original(); });
    g_bs.on = false;
}

void TestBodyCalls()
{
    std::printf("body: the calls\n");
    for (int i = 0; i < 30000 / QUICK; ++i) {
        RandomBodyState();
        S_HumanBody &b = PortBody();
        const int kind = i % 40;
        const int a = I(-2, 40), c = I(-2, 40);
        const bool on = P(0.5f);
        const S_vector v(U(-3, 3), U(-3, 3), U(-3, 3)), v2(U(-3, 3), U(-3, 3), U(-3, 3));
        const S_vector4 v4 = {U(-30, 30), U(-3, 3), U(-30, 30), U(-1, 1)};
        C_Actor *actor = P(0.2f) ? nullptr : g_bwActors[I(0, 2)];
        I3D_frame *frame = P(0.3f) ? nullptr : g_h.frame;
        switch (kind) {
        case 0:
            BodyCall("Reset", [&] { b.Reset(); return 0; }, [&] { BCall<void>(0x005ab4c0, OrigBody()); return 0; });
            break;
        case 1:
            BodyCall("Update", [&] { b.Update(a); return 0; }, [&] { BCall<void>(0x005abc20, OrigBody(), a); return 0; });
            break;
        case 2:
            BodyCall("Tick", [&] { b.Tick(a); return 0; }, [&] { BCall<void>(0x005abc70, OrigBody(), a); return 0; });
            break;
        case 3:
            BodyCall("SetFacingDir", [&] { b.SetFacingDir(v); return 0; }, [&] { BCall<void>(0x005ac0a0, OrigBody(), &v); return 0; });
            break;
        case 4:
            BodyCall("SetLookAt", [&] { b.SetLookAt(v4); return 0; }, [&] { BCall<void>(0x005ac080, OrigBody(), &v4); return 0; });
            break;
        case 5:
            BodyCall("SetViewDir", [&] { b.SetViewDir(v); return 0; }, [&] { BCall<void>(0x005ac0c0, OrigBody(), &v); return 0; });
            break;
        case 6:
            BodyCall("TakeFlags", [&] { return int(b.TakeFlags()); }, [&] { return int(BCall<uint32_t>(0x005ac0d0, OrigBody())); });
            break;
        case 7: {
            const uint32_t flags = uint32_t(I(0, 0xFFFF));
            BodyCall("ApplyHit", [&] { b.ApplyHit(flags, v, v2); return 0; },
                     [&] { BCall<void>(0x005abbd0, OrigBody(), flags, &v, &v2); return 0; });
            break;
        }
        case 8:
            BodyCall("Die", [&] { b.Die(actor); return 0; }, [&] { BCall<void>(0x005abbe0, OrigBody(), static_cast<void *>(actor)); return 0; });
            break;
        case 9:
            BodyCall("PlayDeath", [&] { b.PlayDeath(); return 0; }, [&] { BCall<void>(0x005abbf0, OrigBody()); return 0; });
            break;
        case 10:
            BodyCall("Cripple", [&] { b.Cripple(); return 0; }, [&] { BCall<void>(0x005ac370, OrigBody()); return 0; });
            break;
        case 11:
            BodyCall("Ragdoll", [&] { b.Ragdoll(); return 0; }, [&] { BCall<void>(0x005ac420, OrigBody()); return 0; });
            break;
        case 12:
            BodyCall("ResetMovement", [&] { b.ResetMovement(); return 0; }, [&] { BCall<void>(0x005ac440, OrigBody()); return 0; });
            break;
        case 13:
            BodyCall("Play", [&] { b.Play(a); return 0; }, [&] { BCall<void>(0x005abb30, OrigBody(), a); return 0; });
            break;
        case 14:
            BodyCall("SetStance", [&] { b.SetStance(a); return 0; }, [&] { BCall<void>(0x005abb80, OrigBody(), a); return 0; });
            break;
        case 15:
            BodyCall("GetSpeed", [&] { return Bits(b.GetSpeed()); }, [&] { return Bits(BCall<float>(0x005ac230, OrigBody())); });
            break;
        case 16:
            BodyCall("CanRespawn", [&] { return int(b.CanRespawn()); }, [&] { return int(BCall<uint32_t>(0x005ac330, OrigBody()) & 0xFF); });
            break;
        case 17:
            BodyCall("PlayWorkAnimation", [&] { b.PlayWorkAnimation(a, frame); return 0; },
                     [&] { BCall<void>(0x005abb50, OrigBody(), a, static_cast<void *>(frame)); return 0; });
            break;
        case 18: {
            static const int ACTIONS[] = {-1, 0, 1, 5, 12, 13, 14, 15, 19, 20, 21, 22, 23, 29, 30, 31, 32};
            const int action = P(0.8f) ? ACTIONS[I(0, 16)] : I(-5, 40);
            BodyCall("DoAction", [&] { b.DoAction(action, frame, actor); return 0; },
                     [&] { BCall<void>(0x005abe30, OrigBody(), action, static_cast<void *>(frame), static_cast<void *>(actor)); return 0; });
            break;
        }
        case 19: {
            const int pose = I(-2, 10);
            BodyCall("SetPose", [&] { b.SetPose(pose, &v4); return 0; },
                     [&] { BCall<void>(0x005abf60, OrigBody(), pose, static_cast<const void *>(&v4)); return 0; });
            break;
        }
        case 20:
        case 21: {
            uint32_t interaction = uint32_t(I(0, 16)) | (P(0.5f) ? uint32_t(I(0, 0x3F)) << 10 : 0u);
            BodyCall("Interact", [&] { b.Interact(actor, interaction, c); return 0; },
                     [&] { BCall<void>(0x005abcc0, OrigBody(), static_cast<void *>(actor), interaction, c); return 0; });
            break;
        }
        case 22: {
            static const char *const NAMES[] = {"walk", "", "idle_b"};
            const char *name = P(0.2f) ? nullptr : NAMES[I(0, 2)];
            const int x = I(-1, 9);
            BodyCall("PlayAnimation", [&] { b.PlayAnimation(name, a, c, x); return 0; },
                     [&] { BCall<void>(0x005ac4a0, OrigBody(), name, a, c, x); return 0; });
            break;
        }
        case 23:
            BodyCall("SetPlayerControlled", [&] { b.SetPlayerControlled(on); return 0; },
                     [&] { BCall<void>(0x005ac070, OrigBody(), on); return 0; });
            break;
        case 24:
            BodyCall("SetCarryMode", [&] { b.SetCarryMode(a % 5); return 0; }, [&] { BCall<void>(0x005ac110, OrigBody(), a % 5); return 0; });
            break;
        case 25:
            BodyCall("Freeze", [&] { b.Freeze(on); return 0; }, [&] { BCall<void>(0x005ac170, OrigBody(), on); return 0; });
            break;
        case 26:
            BodyCall("RestoreCollision", [&] { b.RestoreCollision(on); return 0; }, [&] { BCall<void>(0x005ac1b0, OrigBody(), on); return 0; });
            break;
        case 27:
            BodyCall("RemoveCollision", [&] { b.RemoveCollision(on); return 0; }, [&] { BCall<void>(0x005ac1c0, OrigBody(), on); return 0; });
            break;
        case 28: {
            const uint32_t flags = P(0.5f) ? 8u : uint32_t(I(0, 0xFF));
            BodyCall("SetMovementFlags", [&] { b.SetMovementFlags(flags); return 0; },
                     [&] { BCall<void>(0x005ac390, OrigBody(), flags); return 0; });
            BodyCall("ClearMovementFlags", [&] { b.ClearMovementFlags(flags); return 0; },
                     [&] { BCall<void>(0x005ac3e0, OrigBody(), flags); return 0; });
            break;
        }
        case 29:
            BodyCall("GetMovementStance", [&] { return int(b.GetMovementStance()); }, [&] { return BCall<int>(0x005ac3f0, OrigBody()); });
            BodyCall("GetStance", [&] { return b.GetStance(); }, [&] { return BCall<int>(0x005ac210, OrigBody()); });
            BodyCall("GetWorkState", [&] { return b.GetWorkState(); }, [&] { return BCall<int>(0x005ac220, OrigBody()); });
            break;
        case 30:
            BodyCall("GetClimbState", [&] { return b.GetClimbState(); }, [&] { return BCall<int>(0x005ac2b0, OrigBody()); });
            BodyCall("GetClimbTarget", [&] { return int(Word(reinterpret_cast<void *>(static_cast<intptr_t>(b.GetClimbTarget())))); },
                     [&] { return int(Word(reinterpret_cast<void *>(static_cast<intptr_t>(BCall<int>(0x005ac2e0, OrigBody()))))); });
            break;
        case 31:
            BodyCall("IsWorkState5", [&] { return int(b.IsWorkState5()); }, [&] { return int(BCall<uint32_t>(0x005ac310, OrigBody()) & 0xFF); });
            BodyCall("GetEquipmentFlag1", [&] { return int(b.GetEquipmentFlag1()); },
                     [&] { return int(BCall<uint32_t>(0x005ac400, OrigBody()) & 0xFF); });
            break;
        case 32:
            BodyCall("GetVisibilityFactor", [&] { return Bits(b.GetVisibilityFactor()); }, [&] { return Bits(BCall<float>(0x005ac350, OrigBody())); });
            BodyCall("GetLightLevel", [&] { return Bits(b.GetLightLevel()); }, [&] { return Bits(BCall<float>(0x005ac360, OrigBody())); });
            break;
        case 33:
            BodyCall("IsLeftFootForward", [&] { return int(b.IsLeftFootForward()); },
                     [&] { return int(BCall<uint32_t>(0x005ac410, OrigBody()) & 0xFF); });
            BodyCall("GetGround", [&] { return int(Word(b.GetGround())); }, [&] { return int(Word(BCall<const void *>(0x005ac430, OrigBody()))); });
            BodyCall("GetSector", [&] { return int(Word(b.GetSector())); }, [&] { return int(Word(BCall<const void *>(0x005ac4c0, OrigBody()))); });
            // (C_Actor::GetSector: the same function on the human's +0x98; the port's answers null for other actors,
            // where the original reads whatever is there.)
            BodyCall("C_Actor::GetSector", [&] { return int(Word(static_cast<C_Actor *>(g_h.human)->GetSector())); },
                     [&] { return int(Word(BCall<const void *>(0x005ac4c0, OrigBody()))); });
            break;
        case 34:
            BodyCall("Unknown5ac470", [&] { b.Unknown5ac470(a); return 0; }, [&] { BCall<void>(0x005ac470, OrigBody(), a); return 0; });
            break;
        case 35: {
            // (The spheres' address: each side's own movement's.)
            int portOut = -1, origOut = -1;
            BodyCall("GetUnknown6E0",
                     [&] { return int(reinterpret_cast<uint8_t *>(b.GetUnknown6E0(&portOut)) == reinterpret_cast<uint8_t *>(g_h.movement->m_stanceSpheres)) * 2 + 1; },
                     [&] { return int(BCall<uint8_t *>(0x005ac1d0, OrigBody(), &origOut) == o_movement + 0x6E0) * 2 + 1; });
            CHECK(portOut == origOut);
            break;
        }
        case 36: {
            const int x = I(0, 3);
            BodyCall("GetActivityAnimLength", [&] { return S_HumanBody::GetActivityAnimLength(a, c); },
                     [&] { return BCall<int>(0x005ac1f0, OrigBody(), a, c); });
            BodyCall("GetCarryAnimLength", [&] { return S_HumanBody::GetCarryAnimLength(a, c, x); },
                     [&] { return BCall<int>(0x005ac200, OrigBody(), a, c, x); });
            break;
        }
        case 37:
        case 38: {
            // The commands: the port's record (the original's layout: the kind at +0x08, arguments +0x0C..+0x14, data +0x20).
            S_BodyCommand cmd;
            cmd.kind = P(0.9f) ? I(0, 6) : I(-2, 9);
            cmd.arg0 = cmd.kind == BODY_COMMAND_ACTION ? I(0, 32) : cmd.kind == BODY_COMMAND_POSE ? I(-1, 9) : cmd.kind == BODY_COMMAND_INTERACT ? I(0, 15) : a;
            cmd.arg1 = cmd.kind == BODY_COMMAND_WORK_ANIMATION || cmd.kind == BODY_COMMAND_ACTION
                               ? static_cast<int>(reinterpret_cast<intptr_t>(frame)) : c;
            cmd.arg2 = actor;
            std::memcpy(cmd.data, &v4, 16);
            BodyCall("Execute", [&] { b.Execute(cmd); return 0; }, [&] { BCall<void>(0x005abd90, OrigBody(), static_cast<void *>(&cmd)); return 0; });
            break;
        }
        default:
            BodyCall("SetModelVisible", [&] { b.SetModelVisible(on); return 0; },
                     [&] {
                         // (The original calls the movement's model (+0xA9C) itself, slot 0x30: the port's movement
                         // does, out of line; here a fake model logs it under the movement's name.)
                         static void *modelVt[16];
                         static void *model[2] = {modelVt, nullptr};
                         modelVt[0x30 / 4] = reinterpret_cast<void *>(+[](void *, bool b2) __attribute__((stdcall)) {
                             BLog(g_h.movement, "SetModelVisible", {double(b2)});
                         });
                         void *saved;
                         std::memcpy(&saved, o_movement + 0xA9C, 4);
                         void *m = static_cast<void *>(model);
                         std::memcpy(o_movement + 0xA9C, &m, 4);
                         BCall<void>(0x005ac0f0, OrigBody(), on);
                         std::memcpy(o_movement + 0xA9C, &saved, 4);
                         return 0;
                     });
            break;
        }
    }
}

// Create and Destroy on bodies of their own: the logs and the fields by name.
std::string BodyFields(const uint8_t *body)
{
    void *p[5];
    std::memcpy(p, body, 4);
    std::memcpy(p + 1, body + 8, 12);
    int group;
    std::memcpy(&group, body + 4, 4);
    return NameOf(p[0]) + " " + std::to_string(group) + " " + NameOf(p[1]) + " " + NameOf(p[2]) + " " + NameOf(p[3]) + " " +
           std::to_string(body[0x14]);
}
void TestBodyLife()
{
    std::printf("body: create and destroy\n");
    for (int i = 0; i < 3000 / QUICK; ++i) {
        RandomBodyState();
        const int group = I(0, 6);
        S_HumanBody pb;
        alignas(16) uint8_t ob[0x18];
        std::memset(ob, 0xCD, sizeof ob);
        pb.crippled = true;
        g_calls.clear();
        g_bs.on = true;
        const bool pr = pb.Create(g_h.human, group);
        const std::vector<S_Call> portCalls = g_calls;
        const std::string portFields = BodyFields(reinterpret_cast<const uint8_t *>(&pb));
        g_calls.clear();
        ToOriginal();
        std::memset(g_newMovement, 0xCD, sizeof g_newMovement);   // (as the port's fake constructor leaves its movement)
        const bool orr = (BCall<uint32_t>(0x005ab330, ob, static_cast<void *>(o_human), group) & 0xFF) != 0;
        const std::vector<S_Call> origCalls = g_calls;
        g_bs.on = false;
        std::string why;
        CHECK(pr == orr);
        // The new movement's m_unknown18C (+0x18C on both sides): cleared for the humans another machine simulates.
        CHECK(reinterpret_cast<const uint8_t *>(pb.movement)[0x18C] == g_newMovement[0x18C]);
        CHECK(CallsAgree(portCalls, origCalls, why));
        if (!why.empty() || portFields != BodyFields(ob))
            std::printf("  Create: %s; port %s / original %s\n", why.c_str(), portFields.c_str(), BodyFields(ob).c_str());
        CHECK(portFields == BodyFields(ob));
        // Destroyed (the port's objects are its own allocations).
        g_calls.clear();
        g_bs.on = true;
        const bool pd = pb.Destroy();
        const std::vector<S_Call> portDone = g_calls;
        g_calls.clear();
        const bool od = (BCall<uint32_t>(0x005aba90, ob) & 0xFF) != 0;
        const std::vector<S_Call> origDone = g_calls;
        g_bs.on = false;
        why.clear();
        CHECK(pd == od);
        CHECK(CallsAgree(portDone, origDone, why));
        if (!why.empty())
            std::printf("  Destroy: %s\n", why.c_str());
        CHECK(BodyFields(reinterpret_cast<const uint8_t *>(&pb)) == BodyFields(ob));
    }
}

// SaveGame (the files byte for byte) and LoadGame (saved files, then chunks out of order, unknown or short), inside a
// parent chunk as the human's (BODY_PARENT), with a chunk after it read next (where the load left the file).
const uint16_t BODY_PARENT = 0x0BD0, BODY_NEXT = 0x0BD1;
int LoadBody(const std::string &file, bool original)
{
    C_chunk c;
    c.Open(file.c_str(), false);
    uint16_t id = 0;
    if (!c.Descend(id) || id != BODY_PARENT)
        return -1;
    const int r = original ? int(BCall<uint32_t>(0x005ab7a0, OrigBody(), &c) & 0xFF) : int(PortBody().LoadGame(c));
    c.Ascend();
    uint16_t next = 0;
    const bool more = c.Descend(next);
    BLog(nullptr, "next chunk", {double(more), double(next)});
    return r;
}
template <class F> int SaveBody(const std::string &file, F save)
{
    C_chunk c;
    c.Open(file.c_str(), true);
    c.Begin(BODY_PARENT);
    const int r = save(c);
    c.Ascend();
    c.Begin(BODY_NEXT);
    c.Ascend();
    return r;
}
void TestBodySave()
{
    std::printf("body: saved games\n");
    const std::string portFile = TempFile("bw_port.sav"), origFile = TempFile("bw_orig.sav");
    for (int i = 0; i < 1500 / QUICK; ++i) {
        RandomBodyState();
        std::remove(portFile.c_str());
        std::remove(origFile.c_str());
        BodyCall("SaveGame",
                 [&] { return SaveBody(portFile, [&](C_chunk &c) { return int(PortBody().SaveGame(c)); }); },
                 [&] {
                     return SaveBody(origFile, [&](C_chunk &c) { return int(BCall<uint32_t>(0x005ab4e0, OrigBody(), &c) & 0xFF); });
                 });
        CHECK(FileBytes(portFile) == FileBytes(origFile));
        // Another file to load: the chunks in another order, repeated or missing, a state chunk without its byte, a
        // sub-object's chunk without its tag. (No unknown chunks: the original does not leave one, reads on at the
        // garbage headers past it and, once the file ends, loops for ever with the last id; the port stops there.)
        if (P(0.5f)) {
            SaveBody(portFile, [&](C_chunk &c) {
                static const uint16_t IDS[] = {2000, 0x7D1, 0x7D2, 0x7D3};
                const int n = I(0, 6);
                for (int k = 0; k < n; ++k) {
                    const uint16_t id = IDS[I(0, 3)];
                    c.Begin(id);
                    if (id == 2000) {
                        if (P(0.8f)) {
                            const uint8_t crippled = uint8_t(I(0, 2));
                            c.Write(&crippled, 1);
                        }
                    } else {
                        const uint32_t tag = uint32_t(I(0, 0x7FFFFFFF));
                        if (P(0.8f))
                            c.Write(&tag, 4);
                    }
                    c.Ascend();
                }
                return 0;
            });
        }
        BodyCall("LoadGame", [&] { return LoadBody(portFile, false); }, [&] { return LoadBody(portFile, true); });
    }
    std::remove(portFile.c_str());
    std::remove(origFile.c_str());
}

void TestBodyWrap()
{
    for (int k = 0; k < 3; ++k) {
        g_bwActors[k] = reinterpret_cast<C_Actor *>(new uint8_t[0x400]());
        g_names[g_bwActors[k]] = "actor" + std::to_string(k);
    }
    InstallBodyRedirects();
    TestBodyCalls();
    TestBodyLife();
    TestBodySave();
}
}  // namespace
