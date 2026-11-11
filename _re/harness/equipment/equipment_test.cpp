// The models of the items on a human's body (game/human_equipment.cpp). Part 1: the holsters, the models' places,
// save / load against the original's own save. Part 2a: AttachModel / DetachModel (the frames get world matrices:
// the local one through the parent's; the long gun's geometry against a reference written from the asm's steps and
// constants). The engine, the human and the items faked; game/equipment_frames.cpp,
// common/chunk.cpp, common/c_str.cpp and the LS3DF port's math (ls3df/src/math/math.cpp) linked as they are.
// MinGW; build.bat; run equipment_test.exe ("equipment: N checks, 0 failures").
//
// Save / load is checked against EQUIPMENT_SAVE (default: the original's SMCAM_quick.sav copied to
// %TEMP%\eq_saves\original_SMCAM_quick.sav): every equipment chunk (0x309) of it is loaded into an equipment
// whose owner has the saved items, saved again, and compared byte for byte (but for the fields the original
// fills from its stack when a model is missing).
#include <windows.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/equipment_frames.h"
#include "game/first_person_view.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_anim_tables.h"
#include "game/human_body.h"
#include "game/human_equipment.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)
std::string Vec(const S_vector &v)
{
    char s[96];
    std::snprintf(s, sizeof s, "%g,%g,%g", v.x, v.y, v.z);
    return s;
}

// ---- the file (chunk.cpp over memory)
std::string g_file;
int g_at;
}  // namespace
extern "C" unsigned int __cdecl _controlfp(unsigned int newValue, unsigned int mask);
int RW_DATA_CALL dtaOpen(const char *, bool) { g_at = 0; return 1; }
int RW_DATA_CALL dtaOpenWrite(const char *, int) { g_file.clear(); g_at = 0; return 1; }
bool RW_DATA_CALL dtaClose(int) { return true; }
int RW_DATA_CALL dtaSeek(int, int offset, int origin)
{
    if (origin == 0) g_at = offset;
    else if (origin == 1) g_at += offset;
    else g_at = static_cast<int>(g_file.size()) + offset;
    return g_at;
}
int RW_DATA_CALL dtaRead(int, void *data, int size)
{
    int got = static_cast<int>(g_file.size()) - g_at;
    if (got > size) got = size;
    if (got < 0) got = 0;
    std::memcpy(data, g_file.data() + g_at, got);
    g_at += got;
    return got;
}
int RW_DATA_CALL dtaWrite(int, const void *data, int size)
{
    if (static_cast<size_t>(g_at) + size > g_file.size())
        g_file.resize(g_at + size);
    std::memcpy(&g_file[g_at], data, size);
    g_at += size;
    return size;
}

// ============================================================ the fakes' state
uint32_t g_gameRole = GAME_ROLE_LOCAL;
namespace {
std::vector<std::string> g_log;                                           // engine and human calls, in order
std::map<const I3D_frame *, std::string> &g_names = *new std::map<const I3D_frame *, std::string>;
std::map<std::pair<const I3D_frame *, std::string>, I3D_frame *> g_children;
std::map<const C_Item *, I3D_frame *> g_itemFrames;
std::map<int, C_ItemDef *> g_defs;
std::map<int, C_Item *> g_itemsByUid;
alignas(16) unsigned char g_gameBytes[0x400];
alignas(16) unsigned char g_soundBytes[0x100];
bool g_modelOpens = true;
std::string Name(const I3D_frame *f) { return f ? (g_names.count(f) ? g_names[f] : "?") : "null"; }
void Log(const std::string &s) { g_log.push_back(s); }
}  // namespace

// ---- what the tests set
namespace {
I3D_frame *MakeFrame(E_I3D_FRAME_TYPE type, const char *name);
std::map<const C_ItemDef *, I3D_frame **> g_divingGear;                 // a definition's four diving gear models
bool g_localPlayer, g_firstPerson;
bool g_hitSomething;                                                     // CollisionSphereTest's answer
std::map<int, int> g_itemDefLookups;                                     // GetItemDef calls by type id
I3D_frame *g_handsItemFrame;                                             // C_Human::GetHandsItemFrame
S_vector g_hitPos;
}

// ---- items
C_ItemDef *GetItemDef(int typeId)
{
    ++g_itemDefLookups[typeId];
    return g_defs.count(typeId) ? g_defs[typeId] : nullptr;
}
I3D_frame *C_Item::GetFrame(int index)
{
    Log("itemframe " + std::to_string(index));
    return index == 0 && g_itemFrames.count(this) ? g_itemFrames[this] : nullptr;
}
I3D_frame **C_ItemDef::GetDivingGear() { return g_divingGear.count(this) ? g_divingGear[this] : nullptr; }
C_ItemDef::C_ItemDef(E_ItemRecordType, int) {}
C_ItemDef::~C_ItemDef() {}
void C_ItemDef::Save(int *, void *) {}
void C_ItemDef::Load(int *, const void *) {}
C_ItemContainer::C_ItemContainer(int) {}
C_Item *C_Human::FindItemByUid(int uid) const { return g_itemsByUid.count(uid) ? g_itemsByUid[uid] : nullptr; }
C_Item *C_Human::FindBeltItem(int) const { return nullptr; }
I3D_frame *C_Human::GetHandsItemFrame() const { return g_handsItemFrame; }
bool C_Human::IsLocalPlayer() const { return g_localPlayer; }
C_Human *C_Team::GetActivePlayer() const { return nullptr; }
namespace {
std::map<int, C_Actor *> g_actorsByIndex;
}
C_Actor *C_ActorsModule::GetActor(int index) const { return g_actorsByIndex.count(index) ? g_actorsByIndex[index] : nullptr; }
I3D_frame *GetActivityProp(int) { return nullptr; }

// ---- the human's parts
void C_HumanWeaponPose::SetModel(I3D_frame *model, int behaviour) { Log("pose model " + Name(model) + " " + std::to_string(behaviour)); }
void C_HumanWeaponPose::SetMode(int mode) { Log("pose mode " + std::to_string(mode)); }
void C_HumanWeaponPose::SetStainsVisible(bool on) { Log("pose stains " + std::to_string(on)); }
void C_HumanWeaponPose::SetSupportHand(bool on) { Log("pose support " + std::to_string(on)); }
void C_HumanWeaponPose::RaiseWeapon(int v) { Log("pose raise " + std::to_string(v)); }
void C_HumanWork::SetCarryMode(int mode, bool b) { Log("carry " + std::to_string(mode) + " " + std::to_string(b)); }
void C_HumanAnimMixer::FadeOutTrack(int a, int b) { Log("fadeout " + std::to_string(a) + " " + std::to_string(b)); }
I3D_frame *C_HumanMovement::GetSectorFrame() const { return nullptr; }
void C_HumanMovement::RemoveCollision(bool b) { Log("removecollision " + std::to_string(b)); }
void C_HumanMovement::RestoreCollision(bool b) { Log("restorecollision " + std::to_string(b)); }
void C_HumanMovement::SetModelVisible(bool on) { Log("modelvisible " + std::to_string(on)); }
void C_HumanVisual::RebuildShadow() {}
void C_HumanVisual::Unknown7585d0(bool) {}
void C_FirstPersonView::OnInventoryEvent(int, int) {}

// ---- the game, sound, caches
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
bool C_game::IsFirstPersonView() const { return g_firstPerson; }
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(g_soundBytes); }
int C_SoundModule::Play(const S_SoundRequest &r)
{
    Log("sound " + std::to_string(r.chapter) + " " + std::to_string(r.group));
    char s[160];
    std::snprintf(s, sizeof s, "soundreq %g %g %g flags %u source %p attach %p", r.pos.x, r.pos.y, r.pos.z, r.flags,
                  static_cast<void *>(r.source), static_cast<void *>(r.attachTo));
    Log(s);
    return 1;
}
// The world matrix: the local one through the parent's world one.
void I3D_frame::UpdateWMatrixProc()
{
    const S_matrix local = GetLocalMatrix();
    m_worldMatrix = m_parent ? local * m_parent->GetMatrix() : local;
    m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
}
bool CloneFrameTree(I3D_frame *&out, I3D_frame *src, I3D_frame *)
{
    Log("clone " + Name(src));
    out = MakeFrame(FRAME_VISUAL, ("clone_" + Name(src)).c_str());
    out->m_localMatrix(3) = src->m_localMatrix(3);
    return true;
}
S_CollisionVolume *CollisionSphereTest(const S_vector &from, const S_vector &move, float radius, S_vector *hit, S_vector *,
                                       uint32_t a, uint32_t b)
{
    char s[160];
    std::snprintf(s, sizeof s, "sphere %g %g %g / %g %g %g r %g %x %x", from.x, from.y, from.z, move.x, move.y, move.z, radius, a, b);
    Log(s);
    if (!g_hitSomething)
        return nullptr;
    *hit = g_hitPos;
    static S_CollisionVolume *volume = reinterpret_cast<S_CollisionVolume *>(new unsigned char[256]());
    return volume;
}
namespace {
uint32_t __stdcall F_AnimRelease(void *);
void *g_animVt[4] = {reinterpret_cast<void *>(F_AnimRelease)};
struct {
    void **vt = g_animVt;
} g_animObject;
I3D_animation_set *g_holsterAnim = reinterpret_cast<I3D_animation_set *>(&g_animObject);
int g_animGets;
}
template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *name, I3D_animation_set **out, void(__fastcall *)(const char *, void *),
                                                           void *)
{
    ++g_animGets;
    Log(std::string("animget ") + name);
    *out = g_holsterAnim;
    return 0;
}
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *, const char *name, uint32_t, void *, void *, int)
{
    Log(std::string("open ") + name);
    return g_modelOpens ? 0 : E_FAIL;
}

namespace {
// ============================================================ fake frames
void *g_frameVt[128];
bool g_exiting;
struct S_FakeFrame {
    alignas(16) unsigned char bytes[0x400];
    I3D_frame *frame() { return reinterpret_cast<I3D_frame *>(bytes); }
};
I3D_frame *MakeFrame(E_I3D_FRAME_TYPE type, const char *name)
{
    S_FakeFrame *fake = new S_FakeFrame();
    std::memset(fake->bytes, 0, sizeof(fake->bytes));
    I3D_frame *frame = fake->frame();
    *reinterpret_cast<void ***>(frame) = g_frameVt;
    frame->m_type = type;
    frame->m_refCount = 1;
    frame->m_frameFlags |= FRMFLAGS_WMATRIX_VALID | FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
    frame->m_rot.Identity();
    for (int i = 0; i < 4; ++i) {
        frame->m_worldMatrix.m[i][i] = 1.0f;
        frame->m_localMatrix.m[i][i] = 1.0f;
    }
    frame->m_scale = S_vector(1, 1, 1);
    g_names[frame] = name;
    frame->m_name = const_cast<char *>(g_names[frame].c_str());
    return frame;
}
uint32_t __stdcall F_Release(I3D_frame *self) { if (!g_exiting) Log("release " + Name(self)); return 0; }
uint32_t __stdcall F_GetVisualType(I3D_frame *self) { return self->m_type == FRAME_VISUAL ? I3DVISUAL_OBJECT : 0; }
void __stdcall F_SetLightLevel(I3D_frame *self, float) { Log("light " + Name(self)); }
uint32_t __stdcall F_AnimRelease(void *) { if (!g_exiting) Log("release anim"); return 0; }
void __stdcall F_SetPos(I3D_frame *self, const S_vector &p) { Log("pos " + Name(self) + " " + Vec(p)); self->m_localMatrix(3) = p; }
void __stdcall F_SetDir(I3D_frame *self, const S_vector &d, float roll)
{
    Log("dir " + Name(self) + " " + Vec(d));
    if (roll != 0.0f)
        Log("dir roll " + Name(self) + " " + std::to_string(roll));
}
void __stdcall F_SetWorldDir(I3D_frame *self, const S_vector &d, float)
{
    Log("worlddir " + Name(self) + " " + Vec(d));
    S_matrix m;
    m.Identity();
    m.SetDir3(d);
    m(3) = self->m_localMatrix(3);
    self->SetLocalMatrix(m);
}
void __stdcall F_UpdateBound(I3D_frame *self) { self->m_frameFlags |= FRMFLAGS_BOUND_VALID; }
void __stdcall F_Tick(I3D_frame *self, int t) { Log("tick " + Name(self) + " " + std::to_string(t)); }
void __stdcall F_Update(I3D_frame *self) { Log("update " + Name(self)); }
void __stdcall F_SetOn(I3D_frame *self, bool on)
{
    Log(std::string(on ? "on " : "off ") + Name(self));
    self->m_frameFlags = on ? (self->m_frameFlags | 1) : (self->m_frameFlags & ~1u);
}
void __stdcall F_SetName(I3D_frame *self, const char *name) { Log("name " + Name(self) + " " + name); }
void __stdcall F_LinkTo(I3D_frame *self, I3D_frame *parent, uint32_t flags)
{
    Log("link " + Name(self) + " " + Name(parent) + " " + std::to_string(flags));
    self->m_parent = parent;
}
I3D_frame *__stdcall F_FindChild(I3D_frame *self, const char *name, uint32_t)
{
    auto it = g_children.find({self, name});
    return it == g_children.end() ? nullptr : it->second;
}
HRESULT __stdcall F_Duplicate(I3D_frame *self, I3D_frame *src) { Log("duplicate " + Name(self) + " " + Name(src)); return 0; }
// The body model (I3D_model): its pose stage 7 as the test sets it.
struct S_Stage {
    I3D_animation_set *anim = nullptr;
    float weight = 0.0f;
    int time = 0, flags = 0;
    bool active = false;
} g_stage;
void __stdcall F_RemoveModelFrame(I3D_frame *self, I3D_frame *f) { Log("removemodelframe " + Name(self) + " " + Name(f)); }
void __stdcall F_SetAnimation(I3D_frame *self, I3D_animation_set *a, int stage, float w, int flags)
{
    char s[96];
    std::snprintf(s, sizeof s, " %p %d %g %d", static_cast<void *>(a), stage, w, flags);
    Log("setanim " + Name(self) + s);
}
I3D_animation_set *__stdcall F_GetAnimation(I3D_frame *self, int stage)
{
    Log("getanim " + Name(self) + " " + std::to_string(stage));
    return g_stage.anim;
}
void __stdcall F_StopAnimation(I3D_frame *self, int stage) { Log("stopanim " + Name(self) + " " + std::to_string(stage)); }
void __stdcall F_SetAnimWeight(I3D_frame *self, int stage, float w)
{
    char s[64];
    std::snprintf(s, sizeof s, " %d %g", stage, w);
    Log("animweight " + Name(self) + s);
}
float __stdcall F_GetAnimWeight(I3D_frame *, int) { return g_stage.weight; }
void __stdcall F_SetAnimTime(I3D_frame *self, int stage, int t) { Log("animtime " + Name(self) + " " + std::to_string(stage) + " " + std::to_string(t)); }
int __stdcall F_GetAnimTime(I3D_frame *, int) { return g_stage.time; }
int __stdcall F_GetAnimFlags(I3D_frame *, int) { return g_stage.flags; }
bool __stdcall F_IsAnimActive(I3D_frame *, int) { return g_stage.active; }
void __stdcall F_AdvanceStages(I3D_frame *self, const int *times)
{
    std::string s = "advance " + Name(self);
    for (int i = 0; i < 8; ++i)
        s += " " + std::to_string(times[i]);
    Log(s);
}
// The driver: CreateFrame hands out the next frame the test prepared (else a new one).
void *g_driverVt[64];
struct {
    void **vt = g_driverVt;
} g_driver;
std::vector<I3D_frame *> g_nextFrames;
void *g_sceneVt[128];
struct {
    void **vt = g_sceneVt;
    unsigned char fields[0x400] = {};
} g_sceneObject;
void __stdcall F_UpdateFrameSector(void *, I3D_frame *f, const S_vector &p) { Log("sector " + Name(f) + " " + Vec(p)); }
void __stdcall F_MarkTracked(void *, I3D_frame *f) { Log("tracked " + Name(f)); }
std::map<std::string, I3D_frame *> g_sceneFrames;
I3D_frame *__stdcall F_SceneFindFrame(void *, const char *name, uint32_t)
{
    Log(std::string("scenefind ") + name);
    return g_sceneFrames.count(name) ? g_sceneFrames[name] : nullptr;
}
I3D_frame *__stdcall F_CreateFrame(void *, int type)
{
    Log("createframe " + std::to_string(type));
    if (!g_nextFrames.empty()) {
        I3D_frame *f = g_nextFrames.front();
        g_nextFrames.erase(g_nextFrames.begin());
        return f;
    }
    return MakeFrame(static_cast<E_I3D_FRAME_TYPE>(type), "created");
}
}  // namespace
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }

// Raw storage of the modules only named.
alignas(16) unsigned char g_actorsModuleBytes[0x1000];
namespace {

// ============================================================ the human and his containers

// A container of one item (hands, long gun, backpack, head) or of slots (the belt).
class C_TestContainer : public C_ItemContainer {
public:
    C_TestContainer() : C_ItemContainer(0) {}
    C_Item *item = nullptr;
    std::vector<std::vector<C_Item *>> slots;
    float GetTotalWeight() override { return 0.0f; }
    float GetTotalSpace() override { return 0.0f; }
    int CountItems(int) override { return 0; }
    int GetUsedSlotCount() override
    {
        int n = 0;
        for (auto &s : slots)
            n += !s.empty();
        return n;
    }
    int AddItem(C_Item *, C_Item **, int) override { return 0; }
    int RemoveItem(C_Item *, int) override { return 0; }
    void Clear() override {}
    bool IsEmpty() override { return item == nullptr; }
    std::vector<C_Item *> *GetSlots() override { return slots.empty() ? nullptr : slots.data(); }
    C_Item *GetItem() override { return item; }
    int GetSlotCount() override { return static_cast<int>(slots.size()); }
    void SetLockTime(uint32_t t) override { Log("locktime " + std::to_string(t)); }
};

// An item definition of a behaviour (and record type, pouch kind).
class C_TestDef : public C_ItemDef {
public:
    explicit C_TestDef(int behaviour) : C_ItemDef(static_cast<E_ItemRecordType>(0), 0), m_testBehaviour(behaviour) {}
    int m_testBehaviour;
    int GetCarryMode() override { return 3; }
    int GetBehaviour() override { return m_testBehaviour; }
};
C_TestDef *Def(int typeId, int behaviour, E_ItemRecordType record = ITEM_STATIC, int pouch = 0)
{
    auto *d = static_cast<C_TestDef *>(::operator new(sizeof(C_TestDef)));
    std::memset(static_cast<void *>(d), 0, sizeof(C_TestDef));
    new (d) C_TestDef(behaviour);
    d->m_recordType = record;
    d->m_unknown0c = pouch;
    g_defs[typeId] = d;
    return d;
}
C_Item *Item(int typeId, int uid, const char *frameName)
{
    auto *item = static_cast<C_Item *>(::operator new(sizeof(C_Item)));
    std::memset(static_cast<void *>(item), 0, sizeof(C_Item));
    item->m_typeId = typeId;
    item->m_uid = uid;
    item->m_def = GetItemDef(typeId);
    if (frameName)
        g_itemFrames[item] = MakeFrame(FRAME_MODEL, frameName);
    g_itemsByUid[uid] = item;
    return item;
}

struct S_TestHuman {
    C_Human *human;
    I3D_frame *body;
    C_TestContainer containers[8];
    C_ItemContainer *list[8];
    unsigned char *movement;
    unsigned char *pose;
};
// A human whose model is `modelFile` with the joints and hand dummies.
S_TestHuman *MakeHuman(const char *modelFile)
{
    auto *h = new S_TestHuman();
    const size_t size = sizeof(C_Human) + 0x100;
    auto *bytes = static_cast<unsigned char *>(::operator new(size, std::align_val_t(16)));
    std::memset(bytes, 0, size);
    h->human = reinterpret_cast<C_Human *>(bytes);
    h->body = MakeFrame(FRAME_MODEL, "body");
    *reinterpret_cast<const char **>(reinterpret_cast<unsigned char *>(h->body) + 0x134) = modelFile;
    h->human->m_frame = h->body;
    h->human->m_type = ACTOR_HUMAN;
    h->human->m_stats[STAT_HEALTH] = 100;
    for (int i = 0; i < 8; ++i)
        h->list[i] = &h->containers[i];
    h->human->m_containers = h->list;
    const size_t msize = sizeof(C_HumanMovement) + 0x100;
    h->movement = static_cast<unsigned char *>(::operator new(msize, std::align_val_t(16)));
    std::memset(h->movement, 0, msize);
    const size_t psize = sizeof(C_HumanWeaponPose) + 0x100;
    h->pose = static_cast<unsigned char *>(::operator new(psize, std::align_val_t(16)));
    std::memset(h->pose, 0, psize);
    reinterpret_cast<C_HumanMovement *>(h->movement)->m_weaponPose = reinterpret_cast<C_HumanWeaponPose *>(h->pose);
    h->human->m_body.movement = reinterpret_cast<C_HumanMovement *>(h->movement);
    for (const char *name : {equipment::HAND_DUMMY1, "HW2", equipment::JOINT_C, equipment::JOINT_D, equipment::JOINT_F,
                             equipment::JOINT_O, equipment::JOINT_Q, equipment::JOINT_U})
        g_children[{h->body, name}] = MakeFrame(FRAME_DUMMY, (std::string("j_") + name).c_str());
    return h;
}

// The holster model of a set: a child for every holster dummy name.
I3D_frame *HolsterModel()
{
    I3D_frame *m = MakeFrame(FRAME_MODEL, "holsters");
    for (const equipment::S_HolsterDummy *d = equipment::g_holsterDummies; d->slot != HOLSTER_SLOT_COUNT; ++d)
        if (!g_children.count({m, d->name}))
            g_children[{m, d->name}] = MakeFrame(FRAME_VISUAL, (std::string("h_") + d->name).c_str());
    return m;
}

void Reset()
{
    g_log.clear();
    g_gameRole = GAME_ROLE_LOCAL;
    g_modelOpens = true;
}

int Count(const std::string &line) { return static_cast<int>(std::count(g_log.begin(), g_log.end(), line)); }

// ============================================================ tests

// A model with a holster set (BR01's?), and one that has none.
const equipment::S_HolsterModel *ModelWithSet()
{
    for (const equipment::S_HolsterModel *m = equipment::g_holsterModels; m->model; ++m)
        if (m->set >= 0)
            return m;
    return nullptr;
}

void TestHolsterSetup()
{
    const equipment::S_HolsterModel *hm = ModelWithSet();
    CHECK(hm != nullptr);
    static std::string file = std::string("models\\") + hm->model + ".4ds";
    // The body posed in the holster animation on stage 7 (weight 1, flags 2), stage 7 advanced by 100 ms; the set's
    // model opened, each holster dummy (four a slot at most) taken out of it, kept (a reference), linked to its
    // joint (flags 1), hidden; the set's model unlinked and released; the stage cleared again (weight 0, advanced,
    // stopped). The pose animation from the cache once.
    Reset();
    g_stage = S_Stage();
    I3D_frame *holsters = HolsterModel();
    g_nextFrames = {holsters};
    S_TestHuman *h = MakeHuman(file.c_str());
    auto *eq = new C_HumanEquipment(h->human);
    CHECK(g_animGets == 1 && Count("animget tecko") == 1);
    CHECK(Count("open " + std::string(equipment::g_holsterSets[hm->set])) == 1);
    char anim[64];
    std::snprintf(anim, sizeof anim, "setanim body %p 7 1 2", static_cast<void *>(g_holsterAnim));
    CHECK(Count(anim) == 1 && Count("advance body 0 0 0 0 0 0 0 100") == 2);
    CHECK(Count("animweight body 7 0") == 1 && Count("stopanim body 7") == 1);
    CHECK(Count("link holsters body 0") == 1 && Count("link holsters null 0") == 1 && Count("release holsters") == 1);
    int frames = 0;
    bool fine = true;
    for (int s = 0; s < HOLSTER_SLOT_COUNT; ++s) {
        CHECK(eq->m_holsters[s].frameCount <= 4 && eq->m_holsters[s].itemCount == 0);
        for (int i = 0; i < eq->m_holsters[s].frameCount; ++i) {
            I3D_frame *f = eq->m_holsters[s].frames[i];
            ++frames;
            fine &= f->m_refCount == 2 && Count("removemodelframe holsters " + Name(f)) == 1 && Count("off " + Name(f)) == 1;
            fine &= f->m_parent != nullptr;
        }
    }
    CHECK(frames > 0 && fine);
    // The dummies of the set, in the table's order, a slot's first four.
    {
        int perSlot[HOLSTER_SLOT_COUNT] = {};
        bool order = true;
        for (const equipment::S_HolsterDummy *d = equipment::g_holsterDummies; d->slot != HOLSTER_SLOT_COUNT; ++d) {
            if (perSlot[d->slot] < 4)
                order &= Name(eq->m_holsters[d->slot].frames[perSlot[d->slot]]) == std::string("h_") + d->name;
            ++perSlot[d->slot];
        }
        CHECK(order);
    }
    // No pose animation fetched again for the next human.
    Reset();
    g_nextFrames = {HolsterModel()};
    new C_HumanEquipment(MakeHuman(file.c_str())->human);
    CHECK(g_animGets == 1);
    // A stage 7 active before: put back as it was (its weight as a whole number), its time.
    Reset();
    g_stage.active = true;
    g_stage.anim = reinterpret_cast<I3D_animation_set *>(0x1234);
    g_stage.weight = 0.75f;
    g_stage.time = 333;
    g_stage.flags = 5;
    g_nextFrames = {HolsterModel()};
    new C_HumanEquipment(MakeHuman(file.c_str())->human);
    char back[64];
    std::snprintf(back, sizeof back, "setanim body %p 7 0 5", static_cast<void *>(g_stage.anim));
    CHECK(Count(back) == 1 && Count("animtime body 7 333") == 1);
    g_stage = S_Stage();
    // A model not in the list: no holsters, the pose done all the same.
    Reset();
    S_TestHuman *other = MakeHuman("models\\NOSUCHMODEL.4ds");
    auto *none = new C_HumanEquipment(other->human);
    int any = 0;
    for (auto &s : none->m_holsters)
        any += s.frameCount;
    CHECK(any == 0 && Count("stopanim body 7") == 1);
    for (const std::string &l : g_log)
        CHECK(l.rfind("open ", 0) != 0);
    // The set's model not found: no holsters; the model released all the same.
    Reset();
    g_modelOpens = false;
    I3D_frame *missing = HolsterModel();
    g_nextFrames = {missing};
    auto *failed = new C_HumanEquipment(MakeHuman(file.c_str())->human);
    any = 0;
    for (auto &s : failed->m_holsters)
        any += s.frameCount;
    CHECK(any == 0 && Count("release holsters") == 1);
}

void TestHolsterSlots()
{
    // GetHolsterSlot (0x00791b80): by behaviour, then type ids, then ammunition's pouch kind, then the type range.
    Def(500, 0);
    CHECK(C_HumanEquipment::GetHolsterSlot(500) == 1);
    Def(501, ITEM_BEHAVIOUR_SIDEARM);
    CHECK(C_HumanEquipment::GetHolsterSlot(501) == HOLSTER_SLOT_SIDEARM);
    Def(0x3D, ITEM_BEHAVIOUR_THROWN);
    Def(0x3F, ITEM_BEHAVIOUR_THROWN);
    Def(0x3E, ITEM_BEHAVIOUR_THROWN);
    CHECK(C_HumanEquipment::GetHolsterSlot(0x3D) == 7 && C_HumanEquipment::GetHolsterSlot(0x3F) == 8 &&
          C_HumanEquipment::GetHolsterSlot(0x3E) == 6);
    for (int b : {ITEM_BEHAVIOUR_MINE, ITEM_BEHAVIOUR_STICKY_CHARGE, ITEM_BEHAVIOUR_MISSION_EXPLOSIVE}) {
        Def(0x42, b);
        Def(0xFB, b);
        Def(0x43, b);
        Def(0x44, b);
        CHECK(C_HumanEquipment::GetHolsterSlot(0x42) == 10 && C_HumanEquipment::GetHolsterSlot(0xFB) == 10 &&
              C_HumanEquipment::GetHolsterSlot(0x43) == 11 && C_HumanEquipment::GetHolsterSlot(0x44) == 9);
    }
    Def(0x35, ITEM_BEHAVIOUR_MEDKIT);
    Def(0x36, ITEM_BEHAVIOUR_MEDKIT);
    Def(0x37, ITEM_BEHAVIOUR_MEDKIT);
    CHECK(C_HumanEquipment::GetHolsterSlot(0x35) == 12 && C_HumanEquipment::GetHolsterSlot(0x36) == 13 &&
          C_HumanEquipment::GetHolsterSlot(0x37) == HOLSTER_SLOT_COUNT);
    Def(502, 14);
    CHECK(C_HumanEquipment::GetHolsterSlot(502) == 4);
    // Other behaviours: ammunition by pouch kind (0: 2, 1: 3, other: none) ...
    Def(503, 99, ITEM_AMMO, 0);
    Def(504, 99, ITEM_AMMO, 1);
    Def(505, 99, ITEM_AMMO, 2);
    CHECK(C_HumanEquipment::GetHolsterSlot(503) == 2 && C_HumanEquipment::GetHolsterSlot(504) == 3 &&
          C_HumanEquipment::GetHolsterSlot(505) == HOLSTER_SLOT_COUNT);
    // ... the rest by type id: below 0xEF, 0xFB, 0xFD: 5; else none.
    Def(0xEE, 99);
    Def(0xEF, 99);
    Def(0xFD, 99);
    Def(0xFC, 99);
    CHECK(C_HumanEquipment::GetHolsterSlot(0xEE) == 5 && C_HumanEquipment::GetHolsterSlot(0xEF) == HOLSTER_SLOT_COUNT &&
          C_HumanEquipment::GetHolsterSlot(0xFD) == 5 && C_HumanEquipment::GetHolsterSlot(0xFC) == HOLSTER_SLOT_COUNT);
    Def(0xFB, 99);
    CHECK(C_HumanEquipment::GetHolsterSlot(0xFB) == 5);
    // An unknown type id: none (the port's fix; the original reads a null definition).
    CHECK(C_HumanEquipment::GetHolsterSlot(9999) == HOLSTER_SLOT_COUNT);
}

void TestHolsterCounts()
{
    const equipment::S_HolsterModel *hm = ModelWithSet();
    static std::string file = std::string("models\\") + hm->model + ".4ds";
    Reset();
    g_nextFrames = {HolsterModel()};
    S_TestHuman *h = MakeHuman(file.c_str());
    auto *eq = new C_HumanEquipment(h->human);
    // A slot with frames (the side arm's), shown one a pistol; pouches (2, 3) one frame per three magazines.
    auto shown = [&](int slot) {
        int n = 0;
        for (int i = 0; i < eq->m_holsters[slot].frameCount; ++i)
            n += (eq->m_holsters[slot].frames[i]->m_frameFlags & 1) != 0;
        return n;
    };
    Def(600, ITEM_BEHAVIOUR_SIDEARM);
    Def(601, 99, ITEM_AMMO, 0);
    const int sideFrames = eq->m_holsters[HOLSTER_SLOT_SIDEARM].frameCount;
    const int pouchFrames = eq->m_holsters[2].frameCount;
    eq->AddToHolster(600, 1);
    CHECK(eq->m_holsters[HOLSTER_SLOT_SIDEARM].itemCount == 1 && shown(HOLSTER_SLOT_SIDEARM) == (sideFrames > 0 ? 1 : 0));
    eq->AddToHolster(600, 10);
    CHECK(eq->m_holsters[HOLSTER_SLOT_SIDEARM].itemCount == 11 && shown(HOLSTER_SLOT_SIDEARM) == sideFrames);
    eq->RemoveFromHolster(600, 10);
    CHECK(eq->m_holsters[HOLSTER_SLOT_SIDEARM].itemCount == 1 && shown(HOLSTER_SLOT_SIDEARM) == (sideFrames > 0 ? 1 : 0));
    eq->RemoveFromHolster(600, 5);                                         // below 0: 0, the frames left as they were
    CHECK(eq->m_holsters[HOLSTER_SLOT_SIDEARM].itemCount == 0 && shown(HOLSTER_SLOT_SIDEARM) == (sideFrames > 0 ? 1 : 0));
    // Pouches: 1..3 magazines one frame, 4..6 two ...
    for (int n = 1; n <= 3 * pouchFrames + 2; ++n) {
        eq->AddToHolster(601, 1);
        const int want = std::min(pouchFrames, (n - 1) / 3 + 1);
        CHECK(shown(2) == want);
    }
    const int total = 3 * pouchFrames + 2;
    for (int n = total - 1; n >= 0; --n) {
        eq->RemoveFromHolster(601, 1);
        const int want = n == 0 ? 0 : std::min(pouchFrames, (n - 1) / 3 + 1);
        CHECK(shown(2) == want && eq->m_holsters[2].itemCount == n);
    }
    // No slot: nothing.
    Def(602, ITEM_BEHAVIOUR_MEDKIT);
    eq->AddToHolster(602, 3);
    eq->RemoveFromHolster(602, 3);
    CHECK(pouchFrames > 0 && sideFrames > 0);
    // Slot 4's first frame shown or hidden only with items in it.
    Def(603, 14);
    if (eq->m_holsters[4].frameCount > 0) {
        Reset();
        eq->SetHolster4Visible(true);
        CHECK(g_log.empty());
        eq->AddToHolster(603, 1);
        g_log.clear();
        eq->SetHolster4Visible(false);
        CHECK((g_log == std::vector<std::string>{"off " + Name(eq->m_holsters[4].frames[0])}));
    }
}

// An equipment chunk of no fields.
const std::string EMPTY_CHUNK("\x09\x03\x06\x00\x00\x00", 6);
void LoadChunk(C_HumanEquipment *eq, const std::string &bytes)
{
    g_file = bytes;
    C_chunk in;
    in.Open("x", false);
    eq->LoadGame(in);
    in.Close();
}

void TestHolsterLifetime()
{
    // (No model of the list has set 0: the sets are -1 or 1..9.)
    const equipment::S_HolsterModel *hm = ModelWithSet();
    static std::string file = std::string("models\\") + hm->model + ".4ds";
    Reset();
    g_nextFrames = {HolsterModel()};
    S_TestHuman *h = MakeHuman(file.c_str());
    auto *eq = new C_HumanEquipment(h->human);
    int frames = 0;
    for (auto &s : eq->m_holsters)
        frames += s.frameCount;
    CHECK(frames > 0);
    // The pouch (three frames, a frame per three magazines): four, then six at once: the three frames shown and lit,
    // no fourth looked for.
    Def(611, 99, ITEM_AMMO, 0);
    Def(612, ITEM_BEHAVIOUR_SIDEARM);
    S_HolsterSlot &pouch = eq->m_holsters[2];
    CHECK(pouch.frameCount == 3);
    if (pouch.frameCount != 3)
        return;
    g_log.clear();
    eq->AddToHolster(611, 4);
    eq->AddToHolster(611, 6);
    CHECK(pouch.itemCount == 10);
    for (int i = 0; i < 3; ++i)
        CHECK((pouch.frames[i]->m_frameFlags & 1) != 0 && Count("light " + Name(pouch.frames[i])) > 0);
    eq->AddToHolster(612, 1);
    // A load (an empty belt, empty hands) hides every frame and counts nothing.
    LoadChunk(eq, EMPTY_CHUNK);
    bool hidden = true;
    for (auto &s : eq->m_holsters) {
        hidden &= s.itemCount == 0;
        for (int i = 0; i < s.frameCount; ++i)
            hidden &= (s.frames[i]->m_frameFlags & 1) == 0;
    }
    CHECK(hidden);
    // Released: each frame unlinked and released once, the slots empty, the pose animation released.
    std::vector<I3D_frame *> held;
    for (auto &s : eq->m_holsters)
        for (int i = 0; i < s.frameCount; ++i)
            held.push_back(s.frames[i]);
    g_log.clear();
    pouch.itemCount = 5;
    eq->ReleaseHolsters();
    bool released = true;
    for (auto &s : eq->m_holsters) {
        released &= s.frameCount == 0;
        for (I3D_frame *f : s.frames)
            released &= f == nullptr;
    }
    for (I3D_frame *f : held)
        released &= Count("link " + Name(f) + " null 0") == 1 && Count("release " + Name(f)) == 1;
    CHECK(released && Count("release anim") == 1);
    // Set up again: what was counted is forgotten, the pose animation fetched again.
    const int gets = g_animGets;
    g_nextFrames = {HolsterModel()};
    eq->InitHolsters();
    CHECK(g_animGets == gets + 1 && pouch.itemCount == 0 && pouch.frameCount == 3);
}

void TestModels()
{
    S_TestHuman *h = MakeHuman("models\\NOSUCHMODEL.4ds");
    auto *eq = new C_HumanEquipment(h->human);
    I3D_frame *m = MakeFrame(FRAME_MODEL, "m");
    // Each place keeps its model, uid, type; the hands also 700 ms and shown; the special places (4..7, 9) one model.
    eq->SetModel(m, EQUIPMENT_PLACE_HEAD, 11, C_HumanEquipment::NO_TIME, 1);
    CHECK(eq->GetModel(EQUIPMENT_PLACE_HEAD) == m && eq->m_headUid == 11);
    eq->SetModel(m, EQUIPMENT_PLACE_BACKPACK, 12, C_HumanEquipment::NO_TIME, 2);
    CHECK(eq->GetModel(EQUIPMENT_PLACE_BACKPACK) == m && eq->m_backpackUid == 12 && eq->m_backpackTypeId == 2);
    eq->SetModel(m, EQUIPMENT_PLACE_LONG_GUN, 13, C_HumanEquipment::NO_TIME, 3);
    CHECK(eq->GetModel(EQUIPMENT_PLACE_LONG_GUN) == m && eq->m_longGunUid == 13 && eq->m_longGunTypeId == 3);
    eq->m_handsShown = false;
    eq->SetModel(m, EQUIPMENT_PLACE_HANDS, 14, C_HumanEquipment::NO_TIME, 4);
    CHECK(eq->GetModel(EQUIPMENT_PLACE_HANDS) == m && eq->m_handsUid == 14 && eq->m_handsTypeId == 4 && eq->m_handsTime == 700 &&
          eq->m_handsShown);
    // A null model changes nothing of those places.
    eq->SetModel(nullptr, EQUIPMENT_PLACE_HEAD, 99, C_HumanEquipment::NO_TIME, 9);
    CHECK(eq->m_headModel == m && eq->m_headUid == 11);
    for (int place : {5, 6, 7, 9}) {
        eq->SetModel(m, place, 0, C_HumanEquipment::NO_TIME, 0);
        CHECK(eq->GetModel(place) == m && eq->m_specialModel == m);
    }
    eq->SetModel(m, 4, 0, C_HumanEquipment::NO_TIME, 0);
    CHECK(eq->GetModel(4) == m && eq->m_specialTime == -9);
    CHECK(eq->GetModel(8) == nullptr && eq->GetModel(10) == nullptr && eq->GetModel(-1) == nullptr);
    // The time: taken when given, or while the stored one is negative.
    eq->m_specialTime = 50;
    eq->SetModel(m, 5, 0, C_HumanEquipment::NO_TIME, 0);
    CHECK(eq->m_specialTime == 50);
    eq->SetModel(m, 5, 0, 70, 0);
    CHECK(eq->m_specialTime == 70);
    eq->m_specialTime = -3;
    eq->SetModel(m, 5, 0, C_HumanEquipment::NO_TIME, 0);
    CHECK(eq->m_specialTime == C_HumanEquipment::NO_TIME);
    eq->m_specialTime = 0;
    eq->SetModel(m, 5, 0, C_HumanEquipment::NO_TIME, 0);
    CHECK(eq->m_specialTime == 0);
}


// ---- save / load against the original's save

// The equipment chunks (0x309) of a save: each as its sub-chunks, id -> payload, in order.
struct S_EqChunk {
    std::string bytes;                                                    // the whole chunk
    std::vector<std::pair<int, std::string>> fields;
    const std::string *Field(int id) const
    {
        for (const auto &f : fields)
            if (f.first == id)
                return &f.second;
        return nullptr;
    }
    int Int(int id) const
    {
        const std::string *f = Field(id);
        int v = 0;
        if (f && f->size() >= 4)
            std::memcpy(&v, f->data(), 4);
        return v;
    }
};
S_EqChunk ParseChunk(const std::string &bytes)
{
    S_EqChunk c;
    c.bytes = bytes;
    size_t o = 6;
    while (o + 6 <= bytes.size()) {
        uint16_t id;
        uint32_t size;
        std::memcpy(&id, bytes.data() + o, 2);
        std::memcpy(&size, bytes.data() + o + 2, 4);
        if (size < 6 || o + size > bytes.size())
            break;
        c.fields.push_back({id, bytes.substr(o + 6, size - 6)});
        o += size;
    }
    return c;
}
std::vector<S_EqChunk> EquipmentChunks(const std::string &file)
{
    std::vector<S_EqChunk> out;
    for (size_t i = 0; i + 12 <= file.size(); ++i) {
        if (static_cast<unsigned char>(file[i]) != 0x09 || static_cast<unsigned char>(file[i + 1]) != 0x03)
            continue;
        uint32_t size;
        uint16_t first;
        uint32_t firstSize;
        std::memcpy(&size, file.data() + i + 2, 4);
        std::memcpy(&first, file.data() + i + 6, 2);
        std::memcpy(&firstSize, file.data() + i + 8, 4);
        if (first == 1 && firstSize == 10 && size > 50 && size < 4000 && i + size <= file.size())
            out.push_back(ParseChunk(file.substr(i, size)));
    }
    return out;
}

bool IsRotation(int id) { return id == 0x0E || id == 0x20 || id == 0x22 || id == 0x24 || id == 0x26 || id == 0x28; }
// Four floats each within `ulps` of the other's (the quaternions are normalised again when loaded: see the notes).
bool WithinUlps(const std::string &a, const std::string &b, int ulps)
{
    if (a.size() != 16 || b.size() != 16)
        return false;
    for (int i = 0; i < 4; ++i) {
        int32_t x, y;
        std::memcpy(&x, a.data() + 4 * i, 4);
        std::memcpy(&y, b.data() + 4 * i, 4);
        if (x < 0) x = INT32_MIN - x;
        if (y < 0) y = INT32_MIN - y;
        if (std::llabs(static_cast<long long>(x) - y) > ulps)
            return false;
    }
    return true;
}

void TestSaveRoundTrip()
{
    const char *env = std::getenv("EQUIPMENT_SAVE");
    std::string path = env ? env : std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\eq_saves\\original_SMCAM_quick.sav";
    std::string file;
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
        char buffer[65536];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
            file.append(buffer, n);
        std::fclose(f);
    }
    if (file.empty()) {
        std::printf("  (no save at %s: the round trip not run)\n", path.c_str());
        CHECK(!"the original's save");
        return;
    }
    const std::vector<S_EqChunk> chunks = EquipmentChunks(file);
    CHECK(chunks.size() == 49);
    // The fields the original fills from its stack for a missing model: (link id, pos id, rot id).
    const int models[5][3] = {{0x12, 0x1F, 0x20}, {0x13, 0x21, 0x22}, {0x14, 0x23, 0x24}, {0x15, 0x25, 0x26}, {0x16, 0x27, 0x28}};
    int same = 0, compared = 0, nearRot = 0;
    for (size_t k = 0; k < chunks.size(); ++k) {
        const S_EqChunk &c = chunks[k];
        Reset();
        g_itemsByUid.clear();
        g_actorsByIndex.clear();
        S_TestHuman *h = MakeHuman("models\\NOSUCHMODEL.4ds");
        // The saved items, by uid, of their types (the head's type is not saved: a heavy helmet's).
        // (Only for the places whose model was there: link not 4.)
        auto give = [&](int linkField, int uidField, int typeId, bool longGun) {
            const int uid = c.Int(uidField);
            if (c.Int(linkField) == 4 || uid < 0)
                return;
            if (typeId >= 0 && !GetItemDef(typeId))
                Def(typeId, 3, ITEM_ANIMATED);
            C_Item *item = Item(typeId, uid, ("item" + std::to_string(uid)).c_str());
            if (longGun) {
                I3D_frame *backlink = MakeFrame(FRAME_DUMMY, "backlink");
                backlink->m_parent = g_itemFrames[item];
                g_children[{g_itemFrames[item], "backlink"}] = backlink;
            }
        };
        if (!GetItemDef(700))
            Def(700, 16, ITEM_STATIC)->m_weight = 1.0f;
        give(0x12, 0x17, c.Int(0x18), false);
        give(0x15, 0x1A, c.Int(0x29), true);
        give(0x14, 0x1B, 700, false);
        give(0x16, 0x1C, c.Int(0x19), false);
        for (int field : {0x01, 0x03}) {
            const int id = c.Int(field);
            if (id != -1) {
                C_Actor *a = reinterpret_cast<C_Actor *>(MakeHuman("models\\NOSUCHMODEL.4ds")->human);
                a->m_netId = id;
                g_actorsByIndex[id] = a;
            }
        }
        auto *eq = new C_HumanEquipment(h->human);
        eq->FindJoints(nullptr);
        g_file = c.bytes;
        C_chunk in;
        CHECK(in.Open("x", false));
        CHECK(eq->LoadGame(in));
        in.Close();
        g_file.clear();
        C_chunk out;
        out.Open("x", true);
        CHECK(!eq->SaveGame(out));                                        // (the original reports failure)
        out.Close();
        const S_EqChunk back = ParseChunk(g_file);
        // The same sub-chunks in the same order ...
        bool layout = back.fields.size() == c.fields.size();
        for (size_t i = 0; layout && i < back.fields.size(); ++i)
            layout = back.fields[i].first == c.fields[i].first && back.fields[i].second.size() == c.fields[i].second.size();
        CHECK(layout);
        // ... each the same bytes but a missing model's position and rotation.
        std::vector<int> skip;
        for (const auto &m : models)
            if (c.Int(m[0]) == 4) {
                skip.push_back(m[1]);
                skip.push_back(m[2]);
            }
        std::string diff;
        for (size_t i = 0; layout && i < back.fields.size(); ++i) {
            const int id = c.fields[i].first;
            if (std::find(skip.begin(), skip.end(), id) != skip.end())
                continue;
            ++compared;
            if (back.fields[i].second == c.fields[i].second)
                ++same;
            else if (IsRotation(id) && WithinUlps(back.fields[i].second, c.fields[i].second, 4))
                ++nearRot;                                                // (renormalised on load: the original's SetRot does too)
            else {
                char s[16];
                std::snprintf(s, sizeof s, " 0x%x", id);
                diff += s;
            }
        }
        if (!diff.empty())
            std::printf("  chunk %d differs in%s\n", static_cast<int>(k), diff.c_str());
        CHECK(diff.empty());
    }
    std::printf("  save round trip: %d chunks, %d of %d fields the same, %d rotations renormalised (within 4 ulps)\n",
                static_cast<int>(chunks.size()), same, compared, nearRot);
}

int g_moveState, g_moveA30;
bool g_actorAtMinusOne;
// One chunk loaded into a fresh equipment (its owner given the saved items, and `extra` uids) and saved again; `second`
// loaded after it when given.
S_EqChunk RoundTrip(const S_EqChunk &c, const S_EqChunk *second = nullptr, std::vector<int> extra = {})
{
    Reset();
    g_itemsByUid.clear();
    g_actorsByIndex.clear();
    S_TestHuman *h = MakeHuman("models\\NOSUCHMODEL.4ds");
    auto give = [&](int linkField, int uidField, int typeId, bool longGun) {
        const int uid = c.Int(uidField);
        if (c.Int(linkField) == 4 || uid < 0)
            return;
        if (typeId >= 0 && !GetItemDef(typeId))
            Def(typeId, 3, ITEM_ANIMATED);
        C_Item *item = Item(typeId, uid, ("item" + std::to_string(uid)).c_str());
        if (longGun) {
            I3D_frame *backlink = MakeFrame(FRAME_DUMMY, "backlink");
            backlink->m_parent = g_itemFrames[item];
            g_children[{g_itemFrames[item], "backlink"}] = backlink;
        }
    };
    if (!GetItemDef(700))
        Def(700, 16, ITEM_STATIC)->m_weight = 1.0f;
    for (int uid : extra)
        Item(700, uid, ("item" + std::to_string(uid)).c_str());
    auto *movement = reinterpret_cast<C_HumanMovement *>(h->movement);
    movement->m_unknownA30 = g_moveA30;
    movement->m_animState.anim.anim = static_cast<decltype(movement->m_animState.anim.anim)>(g_moveState);
    give(0x12, 0x17, c.Int(0x18), false);
    give(0x15, 0x1A, c.Int(0x29), true);
    give(0x14, 0x1B, 700, false);
    give(0x16, 0x1C, c.Int(0x19), false);
    for (int field : {0x01, 0x03}) {
        const int id = c.Int(field);
        if (id != -1) {
            C_Actor *a = reinterpret_cast<C_Actor *>(MakeHuman("models\\NOSUCHMODEL.4ds")->human);
            a->m_netId = id;
            g_actorsByIndex[id] = a;
        }
    }
    if (g_actorAtMinusOne) {
        C_Actor *a = reinterpret_cast<C_Actor *>(MakeHuman("models\\NOSUCHMODEL.4ds")->human);
        a->m_netId = 77;
        g_actorsByIndex[-1] = a;
    }
    auto *eq = new C_HumanEquipment(h->human);
    eq->FindJoints(nullptr);
    g_file = c.bytes;
    C_chunk in;
    in.Open("x", false);
    eq->LoadGame(in);
    in.Close();
    if (second)
        LoadChunk(eq, second->bytes);
    g_file.clear();
    C_chunk out;
    out.Open("x", true);
    eq->SaveGame(out);
    out.Close();
    return ParseChunk(g_file);
}
// A copy of a chunk with one field's payload replaced.
S_EqChunk WithField(const S_EqChunk &c, int id, const std::string &payload)
{
    std::string bytes = c.bytes.substr(0, 6);
    for (const auto &f : c.fields) {
        const std::string &p = f.first == id ? payload : f.second;
        uint16_t fid = static_cast<uint16_t>(f.first);
        uint32_t size = static_cast<uint32_t>(p.size() + 6);
        bytes.append(reinterpret_cast<const char *>(&fid), 2);
        bytes.append(reinterpret_cast<const char *>(&size), 4);
        bytes += p;
    }
    uint32_t total = static_cast<uint32_t>(bytes.size());
    std::memcpy(&bytes[2], &total, 4);
    return ParseChunk(bytes);
}
std::string IntBytes(int v) { return std::string(reinterpret_cast<const char *>(&v), 4); }
std::string FloatBytes(std::initializer_list<float> v)
{
    std::string s;
    for (float f : v)
        s.append(reinterpret_cast<const char *>(&f), 4);
    return s;
}

void TestSaveFieldVariations()
{
    const char *env = std::getenv("EQUIPMENT_SAVE");
    std::string path = env ? env : std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\eq_saves\\original_SMCAM_quick.sav";
    std::string file;
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
        char buffer[65536];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
            file.append(buffer, n);
        std::fclose(f);
    }
    const std::vector<S_EqChunk> chunks = EquipmentChunks(file);
    if (chunks.empty()) {
        CHECK(!"the original's save");
        return;
    }
    // Bases: one with a long gun (link 2), one with a backpack (2), one with a head model (2).
    const S_EqChunk *gun = nullptr, *pack = nullptr, *head = nullptr;
    for (const S_EqChunk &c : chunks) {
        if (!gun && c.Int(0x15) == 2)
            gun = &c;
        if (!pack && c.Int(0x16) == 2)
            pack = &c;
        if (!head && c.Int(0x14) == 2)
            head = &c;
    }
    CHECK(gun && pack && head);
    if (!gun || !pack || !head)
        return;
    // Each state field set to a value of its own: saved back as loaded.
    struct S_Var {
        int id;
        std::string payload;
    };
    const S_Var vars[] = {
        {0x02, IntBytes(6)},                      // give-to place
        {0x04, IntBytes(7)},                      // take-from place
        {0x05, IntBytes(1)},                      // take-from attach
        {0x09, IntBytes(0)},                      // unknown2c (saved 1)
        {0x0A, IntBytes(0)},                      // hands shown
        {0x0B, IntBytes(0x15)},                   // flags
        {0x0C, FloatBytes({0.375f})},             // unknownD0
        {0x0D, IntBytes(5)},                      // special place
        {0x0E, FloatBytes({0.6f, 0.0f, 0.8f, 0.0f})},   // special rotation (kept as it is: not a model's)
        {0x0F, FloatBytes({1.5f, -2.0f, 3.25f})}, // special position
        {0x10, IntBytes(321)},                    // flashlight time
        {0x11, IntBytes(654)},                    // lens flare time
        {0x1D, IntBytes(-7)},                     // special time (no special model: link 4)
        {0x2A, IntBytes(456)},                    // hands time
    };
    for (const S_Var &v : vars) {
        const S_EqChunk back = RoundTrip(WithField(*gun, v.id, v.payload));
        const std::string *got = back.Field(v.id);
        if (!got || *got != v.payload)
            std::printf("  field 0x%x not kept\n", v.id);
        CHECK(got && *got == v.payload);
    }
    // A long gun and a backpack switched off: kept off.
    {
        const S_EqChunk back = RoundTrip(WithField(*gun, 0x2C, IntBytes(0)));
        CHECK(back.Int(0x2C) == 0);
        const S_EqChunk back2 = RoundTrip(WithField(*pack, 0x2B, IntBytes(0)));
        CHECK(back2.Int(0x2B) == 0);
    }
    // A head model's and a backpack's own rotation (a unit quaternion): kept.
    const std::string rot = FloatBytes({0.0f, 0.6f, 0.0f, 0.8f});
    {
        const S_EqChunk back = RoundTrip(WithField(*head, 0x24, rot));
        CHECK(back.Field(0x24) && WithinUlps(*back.Field(0x24), rot, 1));
        const S_EqChunk back2 = RoundTrip(WithField(*pack, 0x28, rot));
        CHECK(back2.Field(0x28) && WithinUlps(*back2.Field(0x28), rot, 1));
        // ... and positions.
        const std::string pos = FloatBytes({0.125f, -0.25f, 0.5f});
        CHECK(RoundTrip(WithField(*head, 0x23, pos)).Field(0x23) && *RoundTrip(WithField(*head, 0x23, pos)).Field(0x23) == pos);
        CHECK(*RoundTrip(WithField(*pack, 0x27, pos)).Field(0x27) == pos);
        CHECK(*RoundTrip(WithField(*gun, 0x25, pos)).Field(0x25) == pos);
    }
    // Given and taken by actors: their ids back.
    {
        const S_EqChunk back = RoundTrip(WithField(WithField(*gun, 0x01, IntBytes(41)), 0x03, IntBytes(42)));
        CHECK(back.Int(0x01) == 41 && back.Int(0x03) == 42);
    }
    // A long gun whose saved link is "elsewhere" (0) is loaded onto its joint and saved there (2); a head model
    // elsewhere likewise.
    CHECK(RoundTrip(WithField(*gun, 0x15, IntBytes(0))).Int(0x15) == 2);
    // A uid of -1: the model not looked for (saved as none).
    // (Even with an item of uid -1 there.)
    CHECK(RoundTrip(WithField(*head, 0x1B, IntBytes(-1)), nullptr, {-1}).Int(0x14) == 4);
    CHECK(RoundTrip(WithField(*gun, 0x1A, IntBytes(-1)), nullptr, {-1}).Int(0x15) == 4);
    CHECK(Count("update item-1") == 0);
    CHECK(RoundTrip(WithField(*pack, 0x1C, IntBytes(-1)), nullptr, {-1}).Int(0x16) == 4);
    // Given to / taken from -1: no actor, even with one at index -1.
    g_actorAtMinusOne = true;
    {
        const S_EqChunk back = RoundTrip(WithField(WithField(*gun, 0x01, IntBytes(-1)), 0x03, IntBytes(-1)));
        CHECK(back.Int(0x01) == -1 && back.Int(0x03) == -1);
    }
    g_actorAtMinusOne = false;

    // A second load of a chunk with no fields: the defaults (no types, uids 0, no models).
    {
        S_EqChunk varied = *gun;
        for (const S_Var &v : {S_Var{0x09, IntBytes(0)}, S_Var{0x0C, FloatBytes({0.375f})}, S_Var{0x17, IntBytes(91)},
                               S_Var{0x18, IntBytes(5)}, S_Var{0x19, IntBytes(6)}, S_Var{0x1B, IntBytes(93)},
                               S_Var{0x1C, IntBytes(94)}})
            varied = WithField(varied, v.id, v.payload);
        const S_EqChunk once = RoundTrip(varied);
        CHECK(once.Int(0x18) == 5 && once.Int(0x19) == 6 && once.Int(0x29) != -1 && once.Int(0x17) == 91 && once.Int(0x1A) != 0 &&
              once.Int(0x1B) == 93 && once.Int(0x1C) == 94 && once.Int(0x09) == 0 && once.Int(0x0C) != 0);
        const S_EqChunk empty = ParseChunk(EMPTY_CHUNK);
        const S_EqChunk back = RoundTrip(varied, &empty);
        CHECK(back.Int(0x18) == -1 && back.Int(0x19) == -1 && back.Int(0x29) == -1);
        CHECK(back.Int(0x17) == 0 && back.Int(0x1A) == 0 && back.Int(0x1B) == 0 && back.Int(0x1C) == 0);
        CHECK(back.Int(0x09) == 1 && back.Int(0x0C) == 0 && back.Int(0x0A) == 1 && back.Int(0x0B) == 0);
        for (int link = 0x12; link <= 0x16; ++link)
            CHECK(back.Int(link) == 4);
    }
    // Given and taken by actor 0.
    {
        const S_EqChunk back = RoundTrip(WithField(WithField(*gun, 0x01, IntBytes(0)), 0x03, IntBytes(0)));
        CHECK(back.Int(0x01) == 0 && back.Int(0x03) == 0);
    }
    // A model of uid 0 is found (only -1 is none) ...
    CHECK(RoundTrip(WithField(*head, 0x1B, IntBytes(0))).Int(0x14) == 2);
    CHECK(RoundTrip(WithField(*gun, 0x1A, IntBytes(0))).Int(0x15) == 2);
    CHECK(RoundTrip(WithField(*pack, 0x1C, IntBytes(0))).Int(0x16) == 2);
    // ... and one saved as none is not, its item there or not.
    CHECK(RoundTrip(WithField(*head, 0x14, IntBytes(4)), nullptr, {head->Int(0x1B)}).Int(0x14) == 4);
    CHECK(RoundTrip(WithField(*gun, 0x15, IntBytes(4)), nullptr, {gun->Int(0x1A)}).Int(0x15) == 4);
    CHECK(RoundTrip(WithField(*pack, 0x16, IntBytes(4)), nullptr, {pack->Int(0x1C)}).Int(0x16) == 4);
    // The head model and the backpack updated twice: attached, then their transform put back.
    auto updates = [](int uid) { return Count("update item" + std::to_string(uid)); };
    RoundTrip(*head);
    CHECK(updates(head->Int(0x1B)) == 2);
    RoundTrip(*pack);
    CHECK(updates(pack->Int(0x1C)) == 2);
    // The long gun hidden while the movement's A30 is above 0 or its state is 0x5F.
    CHECK(gun->Int(0x2C) == 1 && RoundTrip(*gun).Int(0x2C) == 1);
    g_moveA30 = 1;
    CHECK(RoundTrip(*gun).Int(0x2C) == 0);
    g_moveA30 = 0;
    g_moveState = 0x5F;
    CHECK(RoundTrip(*gun).Int(0x2C) == 0);
    g_moveState = 0x5E;
    CHECK(RoundTrip(*gun).Int(0x2C) == 1);
    g_moveState = 0;
    // A special object from the scene (time -4): found by its saved name, linked to the hand, its transform back; saved
    // under its name, or under the hands model's when there is one.
    {
        g_sceneFrames["lamp"] = MakeFrame(FRAME_MODEL, "lamp");
        const std::string name("lamp\0", 5);
        const std::string pos = FloatBytes({0.5f, 0.25f, -1.0f});
        S_EqChunk c = *gun;
        for (const S_Var &v : {S_Var{0x1D, IntBytes(-4)}, S_Var{0x13, IntBytes(2)}, S_Var{0x12, IntBytes(4)}, S_Var{0x1E, name},
                               S_Var{0x21, pos}, S_Var{0x22, rot}})
            c = WithField(c, v.id, v.payload);
        const S_EqChunk back = RoundTrip(c);
        CHECK(back.Int(0x13) == 2 && back.Field(0x1E) && *back.Field(0x1E) == name);
        CHECK(back.Field(0x21) && *back.Field(0x21) == pos && back.Field(0x22) && WithinUlps(*back.Field(0x22), rot, 1));
        CHECK(Count("link lamp j_HW1 1") == 1 || Count("link lamp j_" + std::string(equipment::HAND_DUMMY1) + " 1") == 1);
        // In the second hand (link 1).
        CHECK(RoundTrip(WithField(c, 0x13, IntBytes(1))).Int(0x13) == 1);
        S_EqChunk withHands = c;
        for (const S_Var &v : {S_Var{0x12, IntBytes(2)}, S_Var{0x17, IntBytes(95)}, S_Var{0x18, IntBytes(5)}})
            withHands = WithField(withHands, v.id, v.payload);
        const S_EqChunk back2 = RoundTrip(withHands);
        CHECK(back2.Field(0x1E) && *back2.Field(0x1E) == std::string("item95\0", 7));
        // Not in the scene: none.
        CHECK(RoundTrip(WithField(c, 0x1E, std::string("nolamp\0", 7))).Int(0x13) == 4);
    }
    // Not an equipment chunk: nothing read, false.
    {
        S_TestHuman *h = MakeHuman("models\\NOSUCHMODEL.4ds");
        auto *eq = new C_HumanEquipment(h->human);
        std::string bytes = gun->bytes;
        bytes[0] = 0x0A;
        g_file = bytes;
        C_chunk in;
        in.Open("x", false);
        CHECK(!eq->LoadGame(in));
        in.Close();
    }
}

// ============================================================ part 2: attach / detach

struct S_Rig {
    S_TestHuman *h;
    C_HumanEquipment *eq;
    C_HumanMovement *mv;
    C_HumanWeaponPose *pose;
};
S_Rig Rig()
{
    Reset();
    g_localPlayer = g_firstPerson = false;
    S_Rig r;
    r.h = MakeHuman("models\\NOSUCHMODEL.4ds");
    r.eq = new C_HumanEquipment(r.h->human);
    r.eq->FindJoints(nullptr);
    r.h->human->m_body.equipment = r.eq;
    r.mv = reinterpret_cast<C_HumanMovement *>(r.h->movement);
    r.pose = reinterpret_cast<C_HumanWeaponPose *>(r.h->pose);
    g_log.clear();
    return r;
}
bool Same(const S_vector &a, const S_vector &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool Near(const S_vector &a, const S_vector &b, float eps = 1e-5f)
{
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
bool NearM(const S_matrix &a, const S_matrix &b, float eps = 1e-5f)
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
            if (std::fabs(a.m[r][c] - b.m[r][c]) > eps)
                return false;
    return true;
}
bool Logged(const std::string &prefix)
{
    return std::any_of(g_log.begin(), g_log.end(), [&](const std::string &l) { return l.rfind(prefix, 0) == 0; });
}
// The index of the first log line starting with `prefix` (-1: none).
int At(const std::string &prefix)
{
    for (int i = 0; i < static_cast<int>(g_log.size()); ++i)
        if (g_log[i].rfind(prefix, 0) == 0)
            return i;
    return -1;
}

// The local matrix a long gun gets on the back (AttachModel, 0x0078e89f..0x0078f0ef; its constants read there): its
// world direction (0, 1, 0) (here with the joint at the origin), the offset; the backlink dummy `b` brought to the joint
// (+0.0275, +0.09); turned about local -z and then local x (magazine) or y, the position kept; then about x by 0.15
// (a magazine) or 0.25; lifted: tilted back by the maximum and slid down by it.
S_matrix RefLongGun(const S_vector &b, bool magazineBranch, bool magazine, bool lifted, const S_vector &offset,
                    float dx, float dz, float yxAngle)
{
    I3D_frame *f = MakeFrame(FRAME_MODEL, "ref");
    I3D_frame *bl = MakeFrame(FRAME_DUMMY, "refbacklink");
    bl->m_parent = f;
    bl->SetPosInline(b);
    S_matrix m;
    m.Identity();
    m.SetDir3(S_vector(0.0f, 1.0f, 0.0f));
    f->SetLocalMatrix(m);
    f->SetPosInline(offset);
    const S_vector link = -bl->GetPos() * f->GetLocalMatrix();
    f->SetPosInline(S_vector(link.x + dx, link.y, link.z + dz));
    float maxTilt, maxSlide;
    if (magazineBranch) {
        maxTilt = 0.29f;
        maxSlide = 0.038f;
        S_vector p = f->GetPos();
        p.x -= 0.01f;
        p.z -= 0.08f;
        equipment::RotateAboutYxX(f, yxAngle);
        f->SetPosInline(p);
        p = f->GetPos();
        equipment::Rotate(f, f->GetLocalMatrix()(0), -0.35f);
        f->SetPosInline(p);
    } else {
        maxTilt = 0.4f;
        maxSlide = 0.068f;
        S_vector p = f->GetPos();
        equipment::RotateAboutYxX(f, -0.25f);
        f->SetPosInline(p);
        p = f->GetPos();
        equipment::Rotate(f, f->GetLocalMatrix()(1), 0.1f);
        f->SetPosInline(p);
    }
    equipment::Rotate(f, S_vector(1.0f, 0.0f, 0.0f), magazine ? 0.15f : 0.25f);
    if (lifted) {
        equipment::Rotate(f, S_vector(1.0f, 0.0f, 0.0f), -maxTilt);
        const S_vector p = f->GetPos();
        f->SetPosInline(S_vector(p.x, p.y, p.z - maxSlide));
    }
    return f->GetLocalMatrix();
}
// A gun model with its backlink dummy at `b` (and a magazine frame).
I3D_frame *Gun(const char *name, const S_vector &b, bool magazine)
{
    I3D_frame *gun = MakeFrame(FRAME_MODEL, name);
    I3D_frame *backlink = MakeFrame(FRAME_DUMMY, (std::string(name) + "_backlink").c_str());
    backlink->m_parent = gun;
    backlink->SetPosInline(b);
    g_children[{gun, equipment::BACKLINK_DUMMY}] = backlink;
    if (magazine)
        g_children[{gun, equipment::MAGAZINE_FRAME}] = MakeFrame(FRAME_VISUAL, (std::string(name) + "_mag").c_str());
    return gun;
}

void TestAttachBody()
{
    // The head: on joint f (flags 2), facing z, shown, at its offset (the original's save has the head models there),
    // updated; the stains shown unless the item weighs 0.1 or less; a null model: nothing.
    {
        S_Rig r = Rig();
        Def(800, 16)->m_weight = 0.5f;
        Item(800, 801, "helmet_item");
        I3D_frame *helmet = MakeFrame(FRAME_VISUAL, "helmet");
        r.eq->m_headModel = helmet;
        r.eq->m_headUid = 801;
        r.eq->AttachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(Count("link helmet j_f 2") == 1 && Count("worlddir helmet 0,0,1") == 1 && Count("on helmet") == 1);
        CHECK(Same(helmet->GetPos(), equipment::g_headModelOffset) && At("update helmet") > At("worlddir helmet"));
        CHECK(Count("pose stains 1") == 1 && Count("light helmet") == 1);
        CHECK(equipment::g_headModelOffset.x == 0.0167346001f && equipment::g_headModelOffset.y == 0.138341993f &&
              equipment::g_headModelOffset.z == -0.0175083f);
        // A cap (0.1): no stains; 0.10001: stains.
        GetItemDef(800)->m_weight = 0.1f;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(Count("pose stains 1") == 0);
        GetItemDef(800)->m_weight = 0.10001f;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(Count("pose stains 1") == 1);
        // No item of that uid (or a negative uid): stains.
        GetItemDef(800)->m_weight = 0.05f;
        r.eq->m_headUid = 802;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(Count("pose stains 1") == 1);
        r.eq->m_headUid = -1;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(Count("pose stains 1") == 1);
        // Uid 0 is an item (a light one: no stains).
        Item(800, 0, "cap_item");
        r.eq->m_headUid = 0;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(Count("pose stains 1") == 0);
        r.eq->m_headModel = nullptr;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(g_log.empty());
        // Detached: unlinked, gone, no stains.
        r.eq->m_headModel = helmet;
        g_log.clear();
        r.eq->DetachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(Count("link helmet null 0") == 1 && r.eq->m_headModel == nullptr && Count("pose stains 0") == 1);
        g_log.clear();
        r.eq->DetachModel(EQUIPMENT_PLACE_HEAD);
        CHECK(g_log.empty());
    }
    // A backpack: on joint d (flags 2), facing z, shown while not seated, at its offset, updated, lit.
    {
        S_Rig r = Rig();
        I3D_frame *pack = MakeFrame(FRAME_VISUAL, "pack");
        r.eq->m_backpackModel = pack;
        r.eq->m_backpackTypeId = -1;
        g_itemDefLookups.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_BACKPACK);
        CHECK(g_itemDefLookups.count(-1) == 0);
        CHECK(Count("link pack j_d 2") == 1 && Count("worlddir pack 0,0,1") == 1 && Count("on pack") == 1);
        CHECK(Same(pack->GetPos(), equipment::g_backpackModelOffset) && Count("update pack") == 1 && Count("light pack") == 1);
        CHECK(equipment::g_backpackModelOffset.x == -0.000953259994f && equipment::g_backpackModelOffset.y == 0.157157004f &&
              equipment::g_backpackModelOffset.z == -0.132352993f);
        r.mv->m_seatMode = 1;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_BACKPACK);
        CHECK(Count("on pack") == 0 && Count("link pack j_d 2") == 1);
        r.mv->m_seatMode = 0;
        // An aqualung (a definition with diving gear): the four models copied once; the mask on the head, two on joints
        // u and q at their origin, the bottles in place of the backpack model.
        // (A fifth model after them: never copied.)
        static I3D_frame *gear[5] = {MakeFrame(FRAME_MODEL, "mask"), MakeFrame(FRAME_MODEL, "hoseu"), MakeFrame(FRAME_MODEL, "hoseq"),
                                     MakeFrame(FRAME_MODEL, "bottles"), MakeFrame(FRAME_MODEL, "extra")};
        for (int i = 0; i < 5; ++i)
            gear[i]->m_localMatrix(3) = S_vector(1.0f + i, 2.0f, 3.0f);
        C_TestDef *aqualung = Def(810, 16);
        g_divingGear[aqualung] = gear;
        r.eq->m_backpackTypeId = 810;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_BACKPACK);
        CHECK(Count("clone mask") == 1 && Count("clone bottles") == 1 && r.eq->m_backpackModel == r.eq->m_divingGear[3]);
        CHECK(Count("link clone_mask j_f 2") == 1 && Count("on clone_mask") == 1 && Same(r.eq->m_divingGear[0]->GetPos(), equipment::g_headModelOffset));
        CHECK(Count("link clone_hoseu j_u 2") == 1 && Count("link clone_hoseq j_q 2") == 1 && Count("on clone_hoseu") == 1 &&
              Count("on clone_hoseq") == 1);
        CHECK(Count("worlddir clone_hoseu 0,0,1") == 1 && Count("update clone_hoseq") == 1);
        CHECK(Same(r.eq->m_divingGear[1]->GetPos(), S_vector(0, 0, 0)) && Count("link clone_bottles j_d 2") == 1);
        CHECK(Same(r.eq->m_divingGear[2]->GetPos(), S_vector(0, 0, 0)) && Count("clone extra") == 0);
        for (const char *g : {"clone_mask", "clone_hoseu", "clone_hoseq"})
            CHECK(Count(std::string("worlddir ") + g + " 0,0,1") == 1 && Count(std::string("update ") + g) == 1 &&
                  Count(std::string("on ") + g) == 1 && Count(std::string("light ") + g) == 1);
        CHECK(Count("light clone_bottles") == 2);
        r.eq->m_backpackModel = pack;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_BACKPACK);
        CHECK(Count("clone mask") == 0 && r.eq->m_backpackModel == r.eq->m_divingGear[3]);
        // Detached: unlinked with the gear.
        g_log.clear();
        r.eq->DetachModel(EQUIPMENT_PLACE_BACKPACK);
        CHECK(r.eq->m_backpackModel == nullptr && Count("link clone_bottles null 0") == 2 && Count("link clone_mask null 0") == 1 &&
              Count("link clone_hoseq null 0") == 1);
        g_divingGear.clear();
    }
}

void TestAttachLongGun()
{
    const S_vector b(-0.010044088f, 0.060267403f, 0.38182217f);       // w_98k's backlink (H:\HD2_Unpacked)
    const S_vector offset = equipment::g_longGunModelOffset;
    // No backlink dummy: nothing.
    {
        S_Rig r = Rig();
        r.eq->m_longGunModel = MakeFrame(FRAME_MODEL, "plain");
        r.eq->AttachModel(EQUIPMENT_PLACE_LONG_GUN);
        CHECK(!Logged("link plain"));
    }
    struct S_Case {
        bool magazine;
        int behaviour;
        bool lifted;
        int seat;
    };
    for (const S_Case &c : {S_Case{false, 5, false, 0}, S_Case{true, 5, false, 0}, S_Case{true, 4, false, 0},
                            S_Case{false, 5, true, 0}, S_Case{true, 5, true, 0}, S_Case{false, 5, false, 1}}) {
        S_Rig r = Rig();
        Def(820, c.behaviour);
        I3D_frame *gun = Gun("gun", b, c.magazine);
        r.eq->m_longGunModel = gun;
        r.eq->m_longGunTypeId = 820;
        r.eq->m_longGunLift = c.lifted ? 1 : 0;
        r.eq->m_longGunTilt = 0.123f;
        r.eq->m_longGunSlide = 0.456f;
        r.mv->m_seatMode = c.seat;
        r.eq->AttachModel(EQUIPMENT_PLACE_LONG_GUN);
        const bool magazineBranch = c.magazine && c.behaviour != 4;
        const S_matrix want = RefLongGun(b, magazineBranch, c.magazine, c.lifted, offset, 0.0275f, 0.09f, -1.9634955f);
        CHECK(NearM(gun->GetLocalMatrix(), want));
        if (!NearM(gun->GetLocalMatrix(), want))
            std::printf("  long gun case mag %d beh %d lift %d\n", c.magazine, c.behaviour, c.lifted);
        CHECK(Count("link gun j_d 2") == 1 && Count("worlddir gun 0,1,0") == 1 && Count("on gun") == (c.seat < 1 ? 1 : 0));
        CHECK(r.eq->m_longGunMaxTilt == (magazineBranch ? 0.29f : 0.4f) && r.eq->m_longGunMaxSlide == (magazineBranch ? 0.038f : 0.068f));
        CHECK(r.eq->m_longGunTilt == (c.lifted ? r.eq->m_longGunMaxTilt : 0.0f) &&
              r.eq->m_longGunSlide == (c.lifted ? r.eq->m_longGunMaxSlide : 0.0f));
        CHECK(Count("light gun") == 0 && Count("update gun") == 5);
        // Detached: unlinked.
        g_log.clear();
        r.eq->DetachModel(EQUIPMENT_PLACE_LONG_GUN);
        CHECK(Count("link gun null 0") == 1 && r.eq->m_longGunModel == nullptr);
    }
    // A visual long gun is lit.
    {
        S_Rig r = Rig();
        Def(821, 5);
        I3D_frame *gun = Gun("vgun", b, false);
        gun->m_type = FRAME_VISUAL;
        r.eq->m_longGunModel = gun;
        r.eq->m_longGunTypeId = 821;
        r.eq->AttachModel(EQUIPMENT_PLACE_LONG_GUN);
        CHECK(Count("light vgun") == 1);
    }
    // The constants against the original's (0x0098ada0).
    CHECK(offset.x == 0.175592005f && offset.y == 0.180546999f && offset.z == -0.133314997f);
}

// The hands item: taken in hand 1, or put away (place 10).
void TestAttachHands()
{
    auto handsRig = [](int typeId, int behaviour, bool magazine) {
        S_Rig r = Rig();
        if (behaviour >= 0)
            Def(typeId, behaviour)->m_armedSound = 77;
        r.eq->m_handsModel = Gun("rifle", S_vector(0.01f, 0.05f, 0.3f), magazine);
        r.eq->m_handsTypeId = typeId;
        r.eq->m_handsTime = 500;
        g_log.clear();
        return r;
    };
    // In the hand: shown, on HW1 (flags 6) at its origin facing x, updated; the grip 2 (1 for thrown or melee items),
    // the pose's model, the other grip 2; the armed sound (at the origin, flags 0, from the human, attached to his
    // hands item); lit; the hands time cleared.
    {
        S_Rig r = handsRig(830, 5, false);
        r.eq->m_handsModel->m_type = FRAME_VISUAL;
        r.eq->m_handsModel->m_localMatrix(3) = S_vector(1.0f, 2.0f, 3.0f);
        I3D_frame *itemFrame = MakeFrame(FRAME_MODEL, "handsitem");
        g_handsItemFrame = itemFrame;
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        g_handsItemFrame = nullptr;
        char s[160];
        std::snprintf(s, sizeof s, "soundreq 0 0 0 flags 0 source %p attach %p", static_cast<void *>(r.h->human),
                      static_cast<void *>(itemFrame));
        CHECK(Count(s) == 1 && Count("light rifle") == 1);
        CHECK(Count("on rifle") == 2 && Count("link rifle j_HW1 6") == 1 && Count("worlddir rifle 1,0,0") == 1 &&
              Same(r.eq->m_handsModel->GetPos(), S_vector(0, 0, 0)));
        CHECK(At("on rifle") < At("link rifle") && At("update rifle") > At("worlddir rifle"));
        CHECK(r.mv->m_morph.m_grip2Request == 2 && r.mv->m_morph.m_gripRequest == 2 && Count("pose model rifle 5") == 1);
        CHECK(Count("sound 3 77") == 1 && r.eq->m_handsTime == C_HumanEquipment::NO_TIME);   // chapter 3: items
    }
    {
        S_Rig r = handsRig(831, ITEM_BEHAVIOUR_THROWN, false);
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(r.mv->m_morph.m_grip2Request == 1 && Count("pose model rifle " + std::to_string(ITEM_BEHAVIOUR_THROWN)) == 1);
    }
    {
        // A melee component: grip 1.
        S_Rig r = handsRig(832, 5, false);
        auto *comp = static_cast<C_ItemComponent *>(::operator new(64));
        std::memset(static_cast<void *>(comp), 0, 64);
        comp->m_kind = ICK_MELEE;
        GetItemDef(832)->m_component = comp;
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(r.mv->m_morph.m_grip2Request == 1);
        GetItemDef(832)->m_component = nullptr;
    }
    {
        // An unknown type: grip 2, the pose's behaviour -1, no sound.
        S_Rig r = handsRig(9998, -1, false);
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(r.mv->m_morph.m_grip2Request == 2 && Count("pose model rifle -1") == 1 && !Logged("sound "));
        // Type -1: no sound either.
        S_Rig r2 = handsRig(-1, -1, false);
        r2.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(!Logged("sound "));
    }
    {
        // The local player in first person: no sound; in third person (or another human in first person): the sound.
        S_Rig r = handsRig(833, 5, false);
        g_localPlayer = g_firstPerson = true;
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(!Logged("sound "));
        g_firstPerson = false;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(Logged("sound "));
        g_localPlayer = false;
        g_firstPerson = true;
        g_log.clear();
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(Logged("sound "));
        g_firstPerson = false;
    }
    {
        // No hand dummy or no model: only the names refreshed.
        S_Rig r = handsRig(834, 5, false);
        r.eq->m_handDummy1 = nullptr;
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(!Logged("link rifle") && r.eq->m_handsTime == 500 && Logged("modelvisible"));
    }
    // Put away (the flag set): place 10. A rifle (behaviour 3..7, 12) with a backlink goes on the back like a long gun;
    // smaller things are hidden.
    for (int behaviour : {0, 2, 3, 7, 8, 11, 12, 13}) {
        S_Rig r = handsRig(840 + behaviour, behaviour, false);
        r.eq->m_flags = EQUIPMENT_HANDS_PUT_AWAY;
        r.eq->m_handsTime = C_HumanEquipment::NO_TIME;
        r.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        const bool rifle = (behaviour >= 3 && behaviour <= 7) || behaviour == 12;
        CHECK(Logged("link rifle j_d 2") == rifle && (Count("off rifle") == 1) == !rifle && r.eq->m_flags == EQUIPMENT_HANDS_PUT_AWAY);
    }
    // Place 10 with a take pending (the hands time set) and alive: first taken into the hand (the flag kept).
    {
        S_Rig r = handsRig(850, 5, false);
        r.eq->m_flags = EQUIPMENT_HANDS_PUT_AWAY;
        r.h->human->m_stats[STAT_HEALTH] = 1;
        r.eq->AttachModel(10);
        CHECK(Count("link rifle j_HW1 6") == 1 && At("link rifle j_HW1 6") < At("link rifle j_d 2") &&
              r.eq->m_flags == EQUIPMENT_HANDS_PUT_AWAY && r.eq->m_handsTime == C_HumanEquipment::NO_TIME);
        // Through place 3 the hands time is cleared first: straight onto the back.
        S_Rig h = handsRig(853, 5, false);
        h.eq->m_flags = EQUIPMENT_HANDS_PUT_AWAY;
        h.eq->AttachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(Count("link rifle j_HW1 6") == 0 && Count("link rifle j_d 2") == 1);
        // Dead: not.
        S_Rig d = handsRig(851, 5, false);
        d.eq->m_flags = EQUIPMENT_HANDS_PUT_AWAY;
        d.h->human->m_stats[STAT_HEALTH] = 0;
        d.eq->AttachModel(10);
        CHECK(Count("link rifle j_HW1 6") == 0 && d.eq->m_handsTime == C_HumanEquipment::NO_TIME);
        // No backlink: left where it is.
        S_Rig n = handsRig(852, 5, false);
        n.eq->m_handsModel = MakeFrame(FRAME_MODEL, "nobacklink");
        n.eq->m_handsTime = C_HumanEquipment::NO_TIME;
        n.eq->AttachModel(10);
        CHECK(!Logged("link nobacklink") && !Logged("off nobacklink"));
    }
    // On the back: the geometry, alone or beside a long gun (its offset's x mirrored), with or without a backpack and
    // a magazine.
    const S_vector b(0.01f, 0.05f, 0.3f);
    for (int k = 0; k < 8; ++k) {
        const bool magazine = k & 1, longGun = k & 2, backpack = k & 4;
        S_Rig r = handsRig(860, 5, magazine);
        r.eq->m_handsTime = C_HumanEquipment::NO_TIME;
        if (longGun)
            r.eq->m_longGunModel = MakeFrame(FRAME_MODEL, "longgun");
        if (backpack)
            r.eq->m_backpackModel = MakeFrame(FRAME_MODEL, "backpack");
        r.eq->AttachModel(10);
        S_vector offset = equipment::g_longGunModelOffset;
        if (longGun)
            offset.x = -offset.x;
        // The reference: like a long gun, but beside one: +0.01 (with a backpack) or +0.025 and z +0.06, turned by
        // -1.4279966 (the original's 0xbfb6c898) with a magazine, neither the second turn nor (without a magazine) the
        // turn about y.
        S_matrix want;
        if (!longGun) {
            if (magazine) {
                want = RefLongGun(b, true, true, false, offset, 0.0275f, 0.09f, -1.9634955f);
            } else {
                // (Only the turn about y here: place 10 has no -0.25 turn, 0x007902a0..0x0079060c.)
                I3D_frame *f = MakeFrame(FRAME_MODEL, "ref");
                I3D_frame *bl = MakeFrame(FRAME_DUMMY, "refbacklink");
                bl->m_parent = f;
                bl->SetPosInline(b);
                S_matrix m;
                m.Identity();
                m.SetDir3(S_vector(0.0f, 1.0f, 0.0f));
                f->SetLocalMatrix(m);
                f->SetPosInline(offset);
                const S_vector link = -bl->GetPos() * f->GetLocalMatrix();
                f->SetPosInline(S_vector(link.x + 0.0275f, link.y, link.z + 0.09f));
                const S_vector p = f->GetPos();
                equipment::Rotate(f, f->GetLocalMatrix()(1), 0.1f);
                f->SetPosInline(p);
                equipment::Rotate(f, S_vector(1.0f, 0.0f, 0.0f), 0.25f);
                want = f->GetLocalMatrix();
            }
        } else {
            I3D_frame *f = MakeFrame(FRAME_MODEL, "ref");
            I3D_frame *bl = MakeFrame(FRAME_DUMMY, "refbacklink");
            bl->m_parent = f;
            bl->SetPosInline(b);
            S_matrix m;
            m.Identity();
            m.SetDir3(S_vector(0.0f, 1.0f, 0.0f));
            f->SetLocalMatrix(m);
            f->SetPosInline(offset);
            const S_vector link = -bl->GetPos() * f->GetLocalMatrix();
            f->SetPosInline(S_vector((backpack ? 0.01f : 0.025f) + link.x, link.y, link.z + 0.06f));
            if (magazine) {
                const S_vector p = f->GetPos();
                equipment::RotateAboutYxX(f, -1.4279966f);
                f->SetPosInline(S_vector(p.x - 0.01f, p.y, p.z - 0.08f));
            }
            equipment::Rotate(f, S_vector(1.0f, 0.0f, 0.0f), 0.15f);
            want = f->GetLocalMatrix();
        }
        CHECK(NearM(r.eq->m_handsModel->GetLocalMatrix(), want));
        if (!NearM(r.eq->m_handsModel->GetLocalMatrix(), want))
            std::printf("  put away case %d\n", k);
        CHECK(Count("worlddir rifle 0,1,0") == 1 && Count("on rifle") == 1 && Count("light rifle") == 0);
    }
    // Seated: not shown.
    {
        S_Rig r = handsRig(861, 5, false);
        r.eq->m_handsTime = C_HumanEquipment::NO_TIME;
        r.mv->m_seatMode = 1;
        r.eq->AttachModel(10);
        CHECK(Count("link rifle j_d 2") == 1 && Count("on rifle") == 0);
    }
    // Detached from the hands: the support hand released first (event 1), unlinked, the pose's model none, the grips 0.
    {
        S_Rig r = handsRig(870, 5, false);
        r.pose->m_hand = 0x80000000u;
        r.mv->m_morph.m_gripRequest = r.mv->m_morph.m_grip2Request = 2;
        I3D_frame *rifle = r.eq->m_handsModel;
        r.eq->DetachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(Count("pose support 0") == 1 && Count("link rifle null 0") == 1 && Count("pose model null -1") == 1);
        CHECK(r.eq->m_handsModel == nullptr && r.mv->m_morph.m_gripRequest == 0 && r.mv->m_morph.m_grip2Request == 0 &&
              Logged("modelvisible") && rifle != nullptr);
        r.pose->m_hand = 0;
        r.eq->m_handsModel = rifle;
        g_log.clear();
        r.eq->DetachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(Count("pose support 0") == 0 && Count("link rifle null 0") == 1);
        g_log.clear();
        r.eq->DetachModel(EQUIPMENT_PLACE_HANDS);
        CHECK(g_log.empty());
    }
}

// A special object (places 4..9).
I3D_frame *Special(const char *name)
{
    I3D_frame *f = MakeFrame(FRAME_MODEL, name);
    f->m_bound.bbox.min = S_vector(-0.2f, 0.0f, -0.1f);
    f->m_bound.bbox.max = S_vector(0.2f, 0.5f, 0.3f);
    f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
    return f;
}
void TestAttachSpecial()
{
    // Place 4 (both hands): kept (a reference), its rotation and world position remembered; the bound's centre put
    // halfway between the hand dummies, 0.1 m along the facing; on HW1 (flags 3); shown unless faded out; grips 2.
    {
        S_Rig r = Rig();
        I3D_frame *box = Special("box");
        box->m_localMatrix(3) = S_vector(5.0f, 1.0f, 2.0f);
        box->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        r.eq->m_handDummy1->m_localMatrix(3) = S_vector(0.2f, 1.0f, 0.4f);
        r.eq->m_handDummy2->m_localMatrix(3) = S_vector(-0.2f, 1.2f, 0.4f);
        r.eq->m_handDummy1->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        r.eq->m_handDummy2->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        r.h->human->m_direction = {0.6f, 0.0f, 0.8f, 0.0f};
        r.eq->m_specialModel = box;
        const int refs = box->m_refCount;
        r.eq->AttachModel(4);
        CHECK(r.eq->m_specialPlace == 4 && box->m_refCount == refs + 1 && Same(r.eq->m_specialPos, S_vector(5.0f, 1.0f, 2.0f)));
        // between (0, 1.1, 0.4) + 0.1 of the facing (0.06, 0.08 in z), minus the centre (0, 0.25, 0.1), plus the world
        // position; no roll.
        CHECK(Near(box->GetPos(), S_vector(0.06f + 5.0f, 1.85f, 0.48f - 0.1f + 2.0f)) && !Logged("dir roll"));
        CHECK(Count("link box j_HW1 3") == 1 && Count("on box") == 1 && Logged("dir box ") && r.mv->m_morph.m_gripRequest == 2 &&
              r.mv->m_morph.m_grip2Request == 2);
        // Faded out: hidden. An empty bound (min above max): only kept and placed.
        r.h->human->m_fadedOut = true;
        g_log.clear();
        r.eq->AttachModel(4);
        CHECK(Count("off box") == 1);
        r.h->human->m_fadedOut = false;
        // A flat bound (min x = max x): placed.
        box->m_bound.bbox.min.x = box->m_bound.bbox.max.x;
        g_log.clear();
        r.eq->AttachModel(4);
        CHECK(Logged("link box j_HW1 3"));
        box->m_bound.bbox.min.x = 1.0f;
        g_log.clear();
        const int refs2 = box->m_refCount;
        r.eq->m_specialPlace = 0;
        r.eq->AttachModel(4);
        CHECK(g_log.empty() && box->m_refCount == refs2 + 1 && r.eq->m_specialPlace == 4);
    }
    // Places 5..8: in a hand (HW1 for 8 or while unknown2c, else HW2), at its origin; the position remembered except for
    // 6 and 7.
    for (int place : {5, 6, 7, 8}) {
        for (bool hand1 : {true, false}) {
            S_Rig r = Rig();
            I3D_frame *thing = Special("thing");
            thing->m_localMatrix(3) = S_vector(3.0f, 4.0f, 5.0f);
            thing->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            r.eq->m_specialModel = thing;
            r.eq->m_unknown2c = hand1;
            r.eq->m_specialPos = S_vector(9, 9, 9);
            r.eq->AttachModel(place);
            const bool inHand1 = place == 8 || hand1;
            CHECK(r.eq->m_specialPlace == place && thing->m_refCount == 2);
            CHECK(Count(std::string("link thing ") + (inHand1 ? "j_HW1" : "j_HW2") + " 2") == 1 && Count("link thing null 0") == 1 &&
                  At("link thing null 0") < At("link thing j_"));
            CHECK(Same(r.eq->m_specialPos, place == 6 || place == 7 ? S_vector(9, 9, 9) : S_vector(3, 4, 5)));
            CHECK(Same(thing->GetPos(), S_vector(0, 0, 0)) && Count("on thing") == 1 && Count("update thing") == 1);
        }
    }
    // Place 9: the special animation on stage 6 (play mode 2), advanced a second, then following the hand.
    {
        S_Rig r = Rig();
        static unsigned char animTable[0x2600];
        static unsigned char anim[0x200];
        g_humanAnimTable = reinterpret_cast<C_HumanAnimTable *>(animTable);
        g_humanAnimTable->m_slots[288].anim = reinterpret_cast<I3D_animation_set *>(0x1288);
        g_humanAnimTable->m_slots[291].anim = reinterpret_cast<I3D_animation_set *>(0x1291);
        g_stage.anim = reinterpret_cast<I3D_animation_set *>(anim);
        I3D_frame *door = Special("door");
        r.eq->m_specialModel = door;
        r.eq->m_unknownD0 = 0.5f;
        r.eq->AttachModel(9);
        CHECK(Count("getanim door 6") == 1 && Count("tick door 100") == 1);
        char s[64];
        std::snprintf(s, sizeof s, "setanim door %p 6 1 2", reinterpret_cast<void *>(0x1288));
        CHECK(Count(s) == 1 && reinterpret_cast<I3D_animation_set *>(anim)->m_playMode == 2 && Count("tick door 1000") == 1);
        CHECK(r.eq->m_specialPlace == 9 && door->m_refCount == 2 && Count("tracked door") == 1 && At("tick door 1000") < At("tracked door"));
        g_stage.anim = nullptr;
    }
    // Null models and other places: nothing.
    {
        S_Rig r = Rig();
        for (int place : {4, 5, 6, 7, 8, 9, 11, -1})
            r.eq->AttachModel(place);
        CHECK(g_log.empty());
    }
}

void TestDetachSpecial()
{
    // Places 4 and 8: put down 0.6 m in front of the human (its height added twice: the original's), back in the scene's
    // primary sector (flags 3), turned as it was, released; the grips 0; place 8 also forgets the place.
    for (int place : {4, 8}) {
        S_Rig r = Rig();
        I3D_frame *sector = MakeFrame(FRAME_SECTOR, "primary");
        *reinterpret_cast<I3D_frame **>(g_sceneObject.fields + 0x1D8 - 4) = sector;
        *reinterpret_cast<I3D_frame **>(g_sceneObject.fields + 0x1F8 - 4) = sector;
        I3D_frame *box = Special("box");
        box->m_bound.bbox.min.y = -0.25f;
        r.eq->m_specialModel = box;
        r.eq->m_specialPlace = place;
        r.eq->m_specialTime = 5;
        r.eq->m_specialRot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        r.h->body->m_localMatrix(3) = S_vector(10.0f, 2.0f, 20.0f);
        r.h->body->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        r.mv->m_morph.m_gripRequest = r.mv->m_morph.m_grip2Request = 2;
        r.eq->DetachModel(place);
        CHECK(r.eq->m_specialModel == nullptr && r.eq->m_specialTime == C_HumanEquipment::NO_TIME && Count("release box") == 1);
        CHECK(Count("link box null 1") == 1 && Count("link box primary 3") == 1 && Logged("sector box "));
        // The body faces z: (10, 2, 20.6); then y: 2 - (-0.25) + 2.
        CHECK(Near(box->GetPos(), S_vector(10.0f, 4.25f, 20.6f)));
        CHECK(r.mv->m_morph.m_gripRequest == 0 && r.mv->m_morph.m_grip2Request == 0);
        CHECK(r.eq->m_specialPlace == (place == 8 ? EQUIPMENT_PLACE_NONE : 4));
    }
    // Place 5: back where it was, in the primary sector (flags 0).
    {
        S_Rig r = Rig();
        I3D_frame *sector = MakeFrame(FRAME_SECTOR, "primary");
        *reinterpret_cast<I3D_frame **>(g_sceneObject.fields + 0x1D8 - 4) = sector;
        *reinterpret_cast<I3D_frame **>(g_sceneObject.fields + 0x1F8 - 4) = sector;
        I3D_frame *lamp = Special("lamp");
        r.eq->m_specialModel = lamp;
        r.eq->m_specialPlace = 5;
        r.eq->m_specialPos = S_vector(7.0f, 8.0f, 9.0f);
        r.eq->DetachModel(5);
        CHECK(Same(lamp->GetPos(), S_vector(7.0f, 8.0f, 9.0f)) && Count("link lamp primary 0") == 1 && Count("release lamp") == 1);
        CHECK(Count("update lamp") == 2 && At("sector lamp") > At("update lamp") && r.eq->m_specialPlace == EQUIPMENT_PLACE_NONE &&
              r.eq->m_specialModel == nullptr);
    }
    // Places 6 and 7: unlinked (flags 1) and released; held at another place: that place detached instead; place 7 still
    // linked: one release more.
    for (int place : {6, 7}) {
        S_Rig r = Rig();
        I3D_frame *thing = Special("thing");
        thing->m_parent = r.eq->m_handDummy1;
        r.eq->m_specialModel = thing;
        r.eq->m_specialPlace = place;
        r.eq->DetachModel(place);
        CHECK(Count("release thing") == (place == 7 ? 2 : 1) && Count("link thing null 1") == 1 && r.eq->m_specialModel == nullptr &&
              r.eq->m_specialPlace == EQUIPMENT_PLACE_NONE);
        // Not linked: one release.
        S_Rig u = Rig();
        I3D_frame *loose = Special("loose");
        u.eq->m_specialModel = loose;
        u.eq->m_specialPlace = EQUIPMENT_PLACE_NONE;
        u.eq->DetachModel(place);
        CHECK(Count("release loose") == 1);
        // Held at place 5: detached from there (back to its position).
        S_Rig o = Rig();
        I3D_frame *other = Special("other");
        o.eq->m_specialModel = other;
        o.eq->m_specialPlace = 5;
        o.eq->m_specialPos = S_vector(1.0f, 2.0f, 3.0f);
        o.eq->DetachModel(place);
        CHECK(Same(other->GetPos(), S_vector(1.0f, 2.0f, 3.0f)) && Count("release other") == 1);
    }
    // Place 9: thrown 0.4 m forward (the special end animation), 0.5 m up; the sphere swept from 0.5 m up by the
    // difference with 0.5 added to its y again (0x0079139a: the original's); something within 0.5 m: kept back from
    // it; farther: at the start.
    for (int hit = 0; hit < 4; ++hit) {
        S_Rig r = Rig();
        static unsigned char animTable[0x2600];
        g_humanAnimTable = reinterpret_cast<C_HumanAnimTable *>(animTable);
        g_humanAnimTable->m_slots[291].anim = reinterpret_cast<I3D_animation_set *>(0x1291);
        I3D_frame *crate = Special("crate");
        r.eq->m_specialModel = crate;
        r.eq->m_specialPlace = 9;
        r.h->body->m_localMatrix(3) = S_vector(1.0f, 0.0f, 1.0f);
        r.h->body->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        g_hitSomething = hit > 0;
        g_hitPos = hit == 1 ? S_vector(1.0f, 0.5f, 1.3f) : hit == 2 ? S_vector(1.0f, 0.5f, 1.6f) : S_vector(1.0f, 0.5f, 1.5f);
        r.eq->DetachModel(9);
        g_hitSomething = false;
        char s[64];
        std::snprintf(s, sizeof s, "setanim crate %p 6 1 2", reinterpret_cast<void *>(0x1291));
        CHECK(Count(s) == 1 && Count("tick crate 1000") == 1 && Count("sphere 1 0.5 1 / 0 1 0.4 r 0.33 c8002 230000") == 1);
        // (Hit 3: exactly 0.5 m away: at the start.)
        const S_vector want = hit == 0 ? S_vector(1.0f, 0.5f, 1.4f) : hit == 1 ? S_vector(1.0f, 0.5f, 0.8f) : S_vector(1.0f, 0.5f, 1.0f);
        CHECK(Count("removecollision 0") == 1 && Count("restorecollision 0") == 1 &&
              At("removecollision 0") < At("sphere") && At("restorecollision 0") > At("sphere"));
        CHECK(Near(crate->GetPos(), want));
        if (!Near(crate->GetPos(), want))
            std::printf("  thrown %d: %s\n", hit, Vec(crate->GetPos()).c_str());
        CHECK(Count("tracked body") == 1 && Count("release crate") == 1 && r.eq->m_specialModel == nullptr &&
              r.eq->m_specialPlace == EQUIPMENT_PLACE_NONE && r.eq->m_specialTime == C_HumanEquipment::NO_TIME);
    }
    // Facing between x and z (0.6, 0.8): the throw and the step back along it.
    for (int hit = 0; hit < 2; ++hit) {
        S_Rig r = Rig();
        I3D_frame *crate = Special("crate");
        r.eq->m_specialModel = crate;
        r.eq->m_specialPlace = 9;
        S_matrix m;
        m.Identity();
        m.SetDir3(S_vector(0.6f, 0.0f, 0.8f));
        m(3) = S_vector(1.0f, 0.0f, 1.0f);
        r.h->body->SetLocalMatrix(m);
        r.h->body->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        g_hitSomething = hit > 0;
        g_hitPos = S_vector(1.18f, 0.5f, 1.24f);                       // 0.3 m along the facing
        r.eq->DetachModel(9);
        g_hitSomething = false;
        const S_vector want = hit == 0 ? S_vector(1.24f, 0.5f, 1.32f) : S_vector(1.0f - 0.6f * 0.2f, 0.5f, 1.0f - 0.8f * 0.2f);
        CHECK(Near(crate->GetPos(), want));
        if (!Near(crate->GetPos(), want))
            std::printf("  thrown along: %s\n", Vec(crate->GetPos()).c_str());
    }
    // Nothing held: nothing.
    {
        S_Rig r = Rig();
        for (int place : {4, 5, 6, 7, 8, 9, 10, -1})
            r.eq->DetachModel(place);
        CHECK(g_log.empty());
    }
}
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // The game's x87 precision (Direct3D's single-precision mode) for the LS3DF math built for x87 (build.bat).
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC
    g_frameVt[0x00 / 4] = reinterpret_cast<void *>(F_Release);
    g_frameVt[0x04 / 4] = reinterpret_cast<void *>(F_GetVisualType);
    g_frameVt[0x08 / 4] = reinterpret_cast<void *>(F_SetPos);
    g_frameVt[0x0C / 4] = reinterpret_cast<void *>(F_SetDir);
    g_frameVt[0x1C / 4] = reinterpret_cast<void *>(F_SetWorldDir);
    g_frameVt[0x20 / 4] = reinterpret_cast<void *>(F_Tick);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_Update);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(F_SetOn);
    g_frameVt[0x34 / 4] = reinterpret_cast<void *>(F_SetName);
    g_frameVt[0x38 / 4] = reinterpret_cast<void *>(F_LinkTo);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    g_frameVt[0x4C / 4] = reinterpret_cast<void *>(F_Duplicate);
    g_frameVt[0x80 / 4] = reinterpret_cast<void *>(F_RemoveModelFrame);
    g_frameVt[0x84 / 4] = reinterpret_cast<void *>(F_SetAnimation);
    g_frameVt[0x88 / 4] = reinterpret_cast<void *>(F_GetAnimation);
    g_frameVt[0x8C / 4] = reinterpret_cast<void *>(F_StopAnimation);
    g_frameVt[0x94 / 4] = reinterpret_cast<void *>(F_SetAnimWeight);
    g_frameVt[0x98 / 4] = reinterpret_cast<void *>(F_GetAnimWeight);
    g_frameVt[0x9C / 4] = reinterpret_cast<void *>(F_SetAnimTime);
    g_frameVt[0xA0 / 4] = reinterpret_cast<void *>(F_GetAnimTime);
    g_frameVt[0xA8 / 4] = reinterpret_cast<void *>(F_GetAnimFlags);
    g_frameVt[0xAC / 4] = reinterpret_cast<void *>(F_IsAnimActive);
    g_frameVt[0xB0 / 4] = reinterpret_cast<void *>(F_AdvanceStages);
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_CreateFrame);
    g_frameVt[0xA4 / 4] = reinterpret_cast<void *>(F_SetLightLevel);
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(F_SceneFindFrame);
    g_sceneVt[0xEC / 4] = reinterpret_cast<void *>(F_UpdateFrameSector);
    g_sceneVt[0x144 / 4] = reinterpret_cast<void *>(F_MarkTracked);
    g_frameVt[0x58 / 4] = reinterpret_cast<void *>(F_UpdateBound);
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(&g_sceneObject);

    TestHolsterSetup();
    TestHolsterSlots();
    TestHolsterCounts();
    TestHolsterLifetime();
    TestModels();
    TestSaveRoundTrip();
    TestSaveFieldVariations();
    TestAttachBody();
    TestAttachLongGun();
    TestAttachHands();
    TestAttachSpecial();
    TestDetachSpecial();
    std::printf("equipment: %d checks, %d failures\n", checks, failures);
    g_exiting = true;
    return failures != 0;
}
