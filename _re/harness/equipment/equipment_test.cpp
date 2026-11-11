// The models of the items on a human's body (game/human_equipment.cpp), part 1: the holsters, the models' places,
// save / load against the original's own save. The engine, the human and the items faked; game/equipment_frames.cpp,
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

// ---- items
C_ItemDef *GetItemDef(int typeId) { return g_defs.count(typeId) ? g_defs[typeId] : nullptr; }
I3D_frame *C_Item::GetFrame(int index)
{
    Log("itemframe " + std::to_string(index));
    return index == 0 && g_itemFrames.count(this) ? g_itemFrames[this] : nullptr;
}
I3D_frame **C_ItemDef::GetDivingGear() { return nullptr; }
C_ItemDef::C_ItemDef(E_ItemRecordType, int) {}
C_ItemDef::~C_ItemDef() {}
void C_ItemDef::Save(int *, void *) {}
void C_ItemDef::Load(int *, const void *) {}
C_ItemContainer::C_ItemContainer(int) {}
C_Item *C_Human::FindItemByUid(int uid) const { return g_itemsByUid.count(uid) ? g_itemsByUid[uid] : nullptr; }
C_Item *C_Human::FindBeltItem(int) const { return nullptr; }
I3D_frame *C_Human::GetHandsItemFrame() const { return nullptr; }
bool C_Human::IsLocalPlayer() const { return false; }
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
void C_HumanMovement::RemoveCollision(bool) {}
void C_HumanMovement::RestoreCollision(bool) {}
void C_HumanMovement::SetModelVisible(bool on) { Log("modelvisible " + std::to_string(on)); }
void C_HumanVisual::RebuildShadow() {}
void C_HumanVisual::Unknown7585d0(bool) {}
void C_FirstPersonView::OnInventoryEvent(int, int) {}

// ---- the game, sound, caches
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
bool C_game::IsFirstPersonView() const { return false; }
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(g_soundBytes); }
int C_SoundModule::Play(const S_SoundRequest &r) { Log("sound " + std::to_string(r.chapter) + " " + std::to_string(r.group)); return 1; }
void I3D_frame::UpdateWMatrixProc() {}
bool CloneFrameTree(I3D_frame *&, I3D_frame *, I3D_frame *) { return false; }
S_CollisionVolume *CollisionSphereTest(const S_vector &, const S_vector &, float, S_vector *, S_vector *, uint32_t, uint32_t)
{
    return nullptr;
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
    frame->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
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
void __stdcall F_SetDir(I3D_frame *self, const S_vector &d, float) { Log("dir " + Name(self) + " " + Vec(d)); }
void __stdcall F_SetWorldDir(I3D_frame *self, const S_vector &d, float) { Log("worlddir " + Name(self) + " " + Vec(d)); }
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
I3D_animation_set *__stdcall F_GetAnimation(I3D_frame *, int) { return g_stage.anim; }
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
void *g_sceneVt[64];
struct {
    void **vt = g_sceneVt;
} g_sceneObject;
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
    h->pose = static_cast<unsigned char *>(::operator new(0x1000, std::align_val_t(16)));
    std::memset(h->pose, 0, 0x1000);
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
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(&g_sceneObject);

    TestHolsterSetup();
    TestHolsterSlots();
    TestHolsterCounts();
    TestHolsterLifetime();
    TestModels();
    TestSaveRoundTrip();
    TestSaveFieldVariations();
    std::printf("equipment: %d checks, %d failures\n", checks, failures);
    g_exiting = true;
    return failures != 0;
}
