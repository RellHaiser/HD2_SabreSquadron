// Part 9e-i's menu soldier (game/menu_soldier.cpp), in the factory build, run by ControlsTest after
// MenuRunTest: the scene, the lights, the camera, the models and the item frames are logging
// objects ("<name> <slot> <arguments>", menu_run_test.inc's), their children found by name; the
// weapon pose, the holster slots, the diving gear and the face texture faked. Prints "menusoldier:"
// with 1 per check met.
#include "game/equipment_frames.h"
#include "game/human_weapon_pose.h"
#define private public
#include "game/menu_soldier.h"
#undef private

// The weapon pose the soldier holds (logged).
C_HumanWeaponPose::C_HumanWeaponPose() { GfLog("pose new"); }
C_HumanWeaponPose::~C_HumanWeaponPose() { GfLog("pose delete"); }
void C_HumanWeaponPose::Attach(I3D_frame *model, C_Human *owner, bool reload)
{
    GfLog("pose attach %s %d %d", MrFrameName(model).c_str(), owner != nullptr, reload);
}
void C_HumanWeaponPose::SetModel(I3D_frame *model, int behaviour)
{
    GfLog("pose model %s %d", MrFrameName(model).c_str(), behaviour);
}
void C_HumanWeaponPose::SetSupportHand(bool on) { GfLog("pose support %d", on); }
void C_HumanWeaponPose::Update(int time) { GfLog("pose update %d", time); }
// The holster slots of the item types (none: HOLSTER_SLOT_COUNT).
std::map<int, int> g_msHolsterSlots;
int C_HumanEquipment::GetHolsterSlot(int typeId)
{
    const auto it = g_msHolsterSlots.find(typeId);
    return it == g_msHolsterSlots.end() ? HOLSTER_SLOT_COUNT : it->second;
}
// The diving gear of the definitions.
std::map<const C_ItemDef *, I3D_frame **> g_msGear;
I3D_frame **C_ItemDef::GetDivingGear()
{
    GfLog("diving gear %s", m_modelName ? m_modelName : "null");
    const auto it = g_msGear.find(this);
    return it == g_msGear.end() ? nullptr : it->second;
}

namespace {

struct S_MenuSoldierResults {
    bool msSetup = true, msFailed = true, msTick = true, msAnims = true, msHolsters = true, msInit = true;
    bool msHead = true, msHands = true, msBackpack = true, msLongGun = true, msBody = true, msClear = true;
    bool msRender = true, msFace = true, msRemove = true;
};

// The children frames found by name (per parent).
std::map<std::pair<const void *, std::string>, S_MrObj *> g_msChildren;
uint32_t __stdcall MsFind(void *self, const char *name, uint32_t mask)
{
    GfLog("%s 48 %s %x", MrName(self).c_str(), name, mask);
    const auto it = g_msChildren.find({self, name});
    return it == g_msChildren.end() ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(it->second));
}
// The animations (logging objects by name; one may fail).
std::map<std::string, S_MrObj *> g_msAnims;
std::string g_msAnimMissing;
// The face texture switched.
bool __attribute__((thiscall)) MsSwitchFace(void *, I3D_frame *model, const char *texture)
{
    GfLog("face %s %s", MrFrameName(model).c_str(), texture);
    return true;
}
// An item type: its carry mode and behaviour, its model's name.
struct C_MsDef : C_ItemDef {
    int carry, behaviour;
    C_MsDef(int c, int b, const char *model) : C_ItemDef(static_cast<E_ItemRecordType>(0), 0), carry(c), behaviour(b)
    {
        m_modelName = const_cast<char *>(model);
    }
    int GetCarryMode() override { return carry; }
    int GetBehaviour() override { return behaviour; }
};

void *g_msSceneVt[MR_SLOTS], *g_msSectorVt[MR_SLOTS], *g_msLightVt[MR_SLOTS], *g_msCameraVt[MR_SLOTS],
    *g_msFrameVt[MR_SLOTS];
void MsInitVtables()
{
    for (void **vt : {g_msSceneVt, g_msSectorVt, g_msLightVt, g_msCameraVt, g_msFrameVt}) {
        FillTraps(vt, std::make_integer_sequence<int, MR_SLOTS>());
        vt[0x00 / 4] = reinterpret_cast<void *>(&MrFake<0x00>);
        vt[0x34 / 4] = reinterpret_cast<void *>(&MrFake<0x34, const char *>);
        vt[0x30 / 4] = reinterpret_cast<void *>(&MrSetOn);
        vt[0x24 / 4] = reinterpret_cast<void *>(&MrFake<0x24>);
        vt[0x1C / 4] = reinterpret_cast<void *>(&MrFake<0x1C, const S_vector *, float>);
    }
    g_msSceneVt[0x20 / 4] = reinterpret_cast<void *>(&MrFake<0x20, int>);
    g_msSceneVt[0x78 / 4] = reinterpret_cast<void *>(&MrFake<0x78, uint32_t>);
    g_msSceneVt[0x80 / 4] = reinterpret_cast<void *>(&MrFake<0x80, void *>);
    g_msSceneVt[0x84 / 4] = reinterpret_cast<void *>(&MrFake<0x84, void *>);
    g_msSceneVt[0x90 / 4] = reinterpret_cast<void *>(&MrFake<0x90, void *>);
    g_msSceneVt[0xEC / 4] = reinterpret_cast<void *>(&MrFake<0xEC, void *, const S_vector *>);
    g_msSceneVt[0x148 / 4] = reinterpret_cast<void *>(&MrFake<0x148, int, int, int, int>);
    g_msSectorVt[0x74 / 4] = reinterpret_cast<void *>(&MrFake<0x74, void *>);
    g_msLightVt[0x74 / 4] = reinterpret_cast<void *>(&MrFake<0x74, int>);
    g_msLightVt[0x78 / 4] = reinterpret_cast<void *>(&MrFake<0x78, float, float, float>);
    g_msLightVt[0x88 / 4] = reinterpret_cast<void *>(&MrFake<0x88, float>);
    g_msLightVt[0x98 / 4] = reinterpret_cast<void *>(&MrFake<0x98, float>);
    g_msLightVt[0xA4 / 4] = reinterpret_cast<void *>(&MrFake<0xA4, float, float>);
    g_msCameraVt[0x74 / 4] = reinterpret_cast<void *>(&MrFake<0x74, float>);
    g_msCameraVt[0x7C / 4] = reinterpret_cast<void *>(&MrFake<0x7C, float, float>);
    g_msCameraVt[0x88 / 4] = reinterpret_cast<void *>(&MrFake<0x88, bool>);
    g_msCameraVt[0x8C / 4] = reinterpret_cast<void *>(&MrFake<0x8C, float>);
    g_msFrameVt[0x0C / 4] = reinterpret_cast<void *>(&MrFake<0x0C, const S_vector *, float>);
    g_msFrameVt[0x20 / 4] = reinterpret_cast<void *>(&MrFake<0x20, int>);
    g_msFrameVt[0x38 / 4] = reinterpret_cast<void *>(&MrFake<0x38, void *, uint32_t>);
    g_msFrameVt[0x48 / 4] = reinterpret_cast<void *>(&MsFind);
    g_msFrameVt[0x80 / 4] = reinterpret_cast<void *>(&MrFake<0x80, void *>);
    g_msFrameVt[0x84 / 4] = reinterpret_cast<void *>(&MrFake<0x84, void *, int, float, int>);
    g_msFrameVt[0x94 / 4] = reinterpret_cast<void *>(&MrFake<0x94, int, float>);
}
// A frame: its matrices valid (the local one the identity).
S_MrObj *MsFrame(const char *name)
{
    S_MrObj *o = MrObj(name, g_msFrameVt);
    I3D_frame *f = As<I3D_frame>(o);
    f->m_localMatrix.Identity();
    f->m_worldMatrix.Identity();
    f->m_worldMatrix.m[3][0] = f->m_worldMatrix.m[3][1] = f->m_worldMatrix.m[3][2] = 0.0f;
    f->m_scale = S_vector(1.0f, 1.0f, 1.0f);
    f->m_frameFlags = FRMFLAGS_LMATRIX_VALID | FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_VALID;
    return o;
}
// A human model: its file and its joints.
struct S_MsModel {
    S_MrObj *model, *hw1, *jd, *jf, *ju, *jq, *jc, *jo;
};
S_MsModel MsModel(const char *name, const char *file)
{
    S_MsModel m;
    const std::string n = name;
    m.model = MsFrame(name);
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(m.model) + 0x134) = file;
    const char *joints[] = {"HW1", "d", "f", "u", "q", "c", "o"};
    S_MrObj **slots[] = {&m.hw1, &m.jd, &m.jf, &m.ju, &m.jq, &m.jc, &m.jo};
    for (int i = 0; i < 7; ++i) {
        *slots[i] = MsFrame((n + "." + joints[i]).c_str());
        g_msChildren[{m.model, joints[i]}] = *slots[i];
    }
    return m;
}
// The log without the lines starting with `prefix`.
std::vector<std::string> MsLogWithout(const std::string &prefix)
{
    std::vector<std::string> out;
    for (const std::string &l : g_gfLog) {
        if (l.rfind(prefix, 0) != 0)
            out.push_back(l);
    }
    return out;
}
bool MsLogIs(const std::vector<std::string> &want, const std::vector<std::string> &got)
{
    if (want == got)
        return true;
    std::printf("menusoldier log:\n");
    for (size_t i = 0; i < std::max(want.size(), got.size()); ++i)
        std::printf("  %s | %s\n", i < want.size() ? want[i].c_str() : "-", i < got.size() ? got[i].c_str() : "-");
    return false;
}
bool MsNear(const S_vector &a, const S_vector &b) { return (a - b).Magnitude() < 1e-5f; }
bool MsSameMatrix(const S_matrix &a, const S_matrix &b)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (std::fabs(a.m[r][c] - b.m[r][c]) > 1e-5f)
                return false;
        }
    }
    return true;
}

// A soldier with a scene and a model put in place (no Setup).
struct S_MsWorld {
    S_MrObj *scene, *sector;
    S_MsModel m;
};
S_MsWorld MsWorld(C_MenuSoldier &s, const char *file = "MODELS\\mp_Na01.i3d")
{
    S_MsWorld w;
    w.scene = MrObj("ss", g_msSceneVt);
    w.sector = MrObj("ssec", g_msSectorVt);
    *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(w.scene) + 0x1F8) = w.sector;
    w.m = MsModel("m1", file);
    s.m_scene = As<I3D_scene>(w.scene);
    s.m_model = As<I3D_model>(w.m.model);
    s.m_pose = new C_HumanWeaponPose();
    s.m_handDummy = As<I3D_frame>(w.m.hw1);
    s.m_jointD = s.m_backpackJoint = As<I3D_frame>(w.m.jd);
    s.m_jointF = As<I3D_frame>(w.m.jf);
    s.m_jointU = As<I3D_frame>(w.m.ju);
    s.m_jointQ = As<I3D_frame>(w.m.jq);
    for (int i = 0; i < 9; ++i)
        s.m_anims[i] = As<I3D_animation_set>(MrObj(CmText("anim%d", i).c_str(), g_msFrameVt));
    return w;
}

} // namespace

template <>
C_ResourceCache<I3D_animation_set>::~C_ResourceCache()
{
}
template <>
HRESULT C_ResourceCache<I3D_animation_set>::OpenInto(I3D_animation_set *, const char *, uint32_t, void *, void *, int)
{
    return I3D_OK;
}
template <>
HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *fileName, I3D_animation_set **out, T_CacheErrorProc, void *)
{
    GfLog("anim %s", fileName);
    if (g_msAnimMissing == fileName)
        return I3DERR_NOFILE;
    S_MrObj *&anim = g_msAnims[fileName];
    if (!anim)
        anim = MrObj(fileName, g_msFrameVt);
    *out = As<I3D_animation_set>(anim);
    return I3D_OK;
}
I3D_animation_set *C_AnimationCache::Create()
{
    return nullptr;
}
C_AnimationCache g_animationCache;

void MenuSoldierTest(I3D_scene *)
{
    S_MenuSoldierResults r;
    MrInitVtables();
    MsInitVtables();
    g_gfLog.clear();
    const int driverSlots[] = {0x54 / 4, 0x1C / 4, 0x18 / 4};
    void *oldDriver[3];
    for (int i = 0; i < 3; ++i)
        oldDriver[i] = g_driverVtable[driverSlots[i]];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
    g_driverVtable[0x1C / 4] = reinterpret_cast<void *>(&MrGetState);
    g_driverVtable[0x18 / 4] = reinterpret_cast<void *>(&MrSetState);
    void (*const oldFill)(I3D_model *) = g_cacheFill;
    g_cacheFill = [](I3D_model *) {};
    g_msNamer = &MrFrameName;
    // The face texture's slot of the effects module.
    void **const oldEffectsVt = *reinterpret_cast<void ***>(GetGEffects());
    static void *effectsVt[64];
    std::memcpy(effectsVt, oldEffectsVt, sizeof effectsVt);
    // (Its slot in g++'s layout, two destructor slots: from the member pointer, its offset + 1.)
    {
        bool (C_GEffectsModule::*pmf)(I3D_frame *, const char *) = &C_GEffectsModule::SwitchFaceTexture;
        std::ptrdiff_t raw[2];
        std::memcpy(raw, &pmf, sizeof raw);
        effectsVt[(raw[0] - 1) / static_cast<std::ptrdiff_t>(sizeof(void *))] = reinterpret_cast<void *>(&MsSwitchFace);
    }
    *reinterpret_cast<void ***>(GetGEffects()) = effectsVt;
    // The game's camera.
    S_MrObj *gameScene = MrObj("gamescene", g_msSceneVt);
    S_MrObj *gameCamera = MsFrame("gamecamera");
    As<I3D_frame>(gameCamera)->m_worldMatrix.m[3][0] = 1.0f;
    As<I3D_frame>(gameCamera)->m_worldMatrix.m[3][1] = 2.0f;
    As<I3D_frame>(gameCamera)->m_worldMatrix.m[3][2] = 3.0f;
    *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(gameScene) + 0x15C) = gameCamera;
    I3D_scene *const oldGameScene = GetGame()->m_scene;
    GetGame()->m_scene = As<I3D_scene>(gameScene);
    // The items: 5, 6 head gear; 7 a rifle (carry 1); 8 carry 6; 9 carry 2; 10 a backpack; 11 the
    // diving gear; 12 a uniform; 13 a long gun (behaviour 3); 14 a long gun of behaviour 4.
    std::map<int, C_ItemDef *> oldDefs = g_gfItemDefs;
    C_MsDef d5(0, 0, "h5"), d6(0, 0, "h6"), d7(1, 7, "r7"), d8(6, 8, "c8"), d9(2, 9, "c9"), d10(0, 0, "b10"),
        d11(0, 0, "b11"), d12(0, 11, "GE03"), d13(0, 3, "l13"), d14(0, 4, "l14");
    g_gfItemDefs = {{5, &d5}, {6, &d6}, {7, &d7}, {8, &d8}, {9, &d9}, {10, &d10}, {11, &d11}, {12, &d12},
                    {13, &d13}, {14, &d14}};
    I3D_frame *const oldItemModel = g_itemModel;

    // --- set up before the game's camera: the scene in the rectangle, the ambient and the spot
    // light, the camera, the animations (one missing), the naked model ---
    {
        bool ok = true;
        C_MenuSoldier s;
        S_MrObj *scene = MrObj("ss", g_msSceneVt), *sector = MrObj("ssec", g_msSectorVt);
        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(scene) + 0x1F8) = sector;
        S_MrObj *l1 = MsFrame("l1"), *l2 = MsFrame("l2"), *cam = MsFrame("cam");
        float *unknown210 = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(scene) + 0x210);
        unknown210[0] = unknown210[1] = unknown210[2] = 9.0f;
        l1->vtable = l2->vtable = g_msLightVt;
        cam->vtable = g_msCameraVt;
        S_MsModel m = MsModel("m1", "MODELS\\mp_Na01.i3d");
        g_mrMade.clear();
        g_mrMade[FRAME_SCENE] = {scene};
        g_mrMade[FRAME_LIGHT] = {l1, l2};
        g_mrMade[FRAME_CAMERA] = {cam};
        g_mrMade[FRAME_MODEL] = {m.model};
        g_msAnims.clear();
        g_msAnimMissing = "!vpredP";
        I3D_animation_set *const kept = As<I3D_animation_set>(MrObj("kept", g_msFrameVt));
        s.m_anims[8] = kept;
        g_cacheLoads.clear();
        g_gfLog.clear();
        s.Setup(10.0f, 20.0f, 330.0f, 260.0f);
        const float scale = 1.0f / static_cast<float>(std::sqrt(0.8345));
        std::vector<std::string> want = {
            "create 13", "ss 148 10 20 330 260",
            "create 2", "l1 34 C_AeMenu1", "l1 74 4", "l1 1c (0 0 1) 0", "l1 88 0.5", "l1 98 1000", "l1 24",
            "ss 80 l1", "ssec 74 l1", "l1 0",
            "create 2", "l2 34 C_AeMenu1", "l2 74 2", "l2 78 1 1 1", "l2 1c (0 -1 1) 0", "l2 88 0.9", "l2 98 50",
            "l2 a4 0.3 0.4", "l2 24", "ss 80 l2", "ssec 74 l2", "l2 0",
            "create 3", CmText("cam 1c (0 %g %g) 0", -0.08f * scale, 0.91f * scale), "cam 74 0.23", "cam 88 0",
            "cam 8c 1.33333", "cam 7c 4.2 8.6", "ss 80 cam", "ss 90 cam", "cam 24", "cam 0",
            "anim !static02NEW", "anim !walkFN6c4", "anim !vpredP", "anim !drzgran1", "anim !klidMG", "anim (kliddrz21",
            "anim (pzbp", "anim !klidMG",
            "create 9", "pose new", "pose attach m1 0 0", "m1 48 HW1 20", "m1 48 d ffff", "m1 48 f ffff",
            "m1 48 u ffff", "m1 48 q ffff", "m1 48 c ffff", "m1 48 d ffff", "m1 48 o ffff",
            "ss 80 m1", "ss ec m1 (0 0 0)", "m1 0", "m1 c (0 0 -1) 0", "m1 84 !static02NEW 0 1 2", "m1 20 714", "m1 24",
        };
        ok &= MsLogIs(want, g_gfLog);
        ok &= g_cacheLoads == std::vector<std::string>({"mp_Na01"}) && unknown210[0] == 0.0f && unknown210[1] == 0.0f &&
              unknown210[2] == 0.0f;
        ok &= s.m_pos.x == 1.0f && s.m_pos.y == 2.0f && s.m_pos.z == 3.0f && s.m_posW == 1.0f;
        ok &= MsNear(As<I3D_frame>(l1)->GetPos(), S_vector(1.0f, 3.0f, -2.0f)) &&
              MsNear(As<I3D_frame>(l2)->GetPos(), S_vector(1.0f, 8.0f, -3.0f)) &&
              MsNear(As<I3D_frame>(cam)->GetPos(), S_vector(1.0f, 3.4f, -3.0f)) &&
              MsNear(As<I3D_frame>(m.model)->GetPos(), S_vector(1.0f, 2.0f, 3.0f));
        ok &= s.m_scene == As<I3D_scene>(scene) && s.m_model == As<I3D_model>(m.model) && s.m_pose &&
              s.m_handDummy == As<I3D_frame>(m.hw1) && s.m_jointD == As<I3D_frame>(m.jd) &&
              s.m_backpackJoint == As<I3D_frame>(m.jd) && s.m_jointF == As<I3D_frame>(m.jf) &&
              s.m_jointU == As<I3D_frame>(m.ju) && s.m_jointQ == As<I3D_frame>(m.jq);
        ok &= s.m_anims[2] == nullptr && s.m_anims[4] == As<I3D_animation_set>(g_msAnims["!klidMG"]) &&
              s.m_anims[7] == s.m_anims[4] && s.m_anims[8] == kept && s.m_idle == 0 && s.m_idleTime == 5000 && s.m_poseAnim == -1;
        // Animations already there are not asked for again.
        g_gfLog.clear();
        g_mrMade.clear();
        s.m_scene = nullptr;
        s.Setup(0.0f, 0.0f, 1.0f, 1.0f);
        ok &= GfLogIs({"create 13"});
        delete s.m_pose;
        WM(r.msSetup, ok);
    }

    // --- set up without a scene, a light or the camera: stops there; no game camera: the position
    // kept ---
    {
        bool ok = true;
        C_MenuSoldier s;
        S_MrObj *scene = MrObj("ss", g_msSceneVt), *sector = MrObj("ssec", g_msSectorVt);
        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(scene) + 0x1F8) = sector;
        S_MrObj *l1 = MsFrame("l1");
        l1->vtable = g_msLightVt;
        g_mrMade.clear();
        g_mrMade[FRAME_SCENE] = {scene};
        g_mrMade[FRAME_LIGHT] = {l1};
        GetGame()->m_scene = nullptr;
        s.m_pos = S_vector(4.0f, 5.0f, 6.0f);
        g_gfLog.clear();
        s.Setup(0.0f, 0.0f, 2.0f, 2.0f);
        ok &= MsLogIs({"create 13", "ss 148 0 0 2 2", "create 2", "l1 34 C_AeMenu1", "l1 74 4", "l1 1c (0 0 1) 0",
                       "l1 88 0.5", "l1 98 1000", "l1 24", "ss 80 l1", "ssec 74 l1", "l1 0", "create 2"},
                      g_gfLog);
        ok &= MsNear(As<I3D_frame>(l1)->GetPos(), S_vector(4.0f, 6.0f, 1.0f));
        C_MenuSoldier s2;
        g_mrMade.clear();
        g_gfLog.clear();
        s2.Setup(0.0f, 0.0f, 2.0f, 2.0f);
        ok &= GfLogIs({"create 13"}) && s2.m_scene == nullptr;
        // A camera without a scene.
        GetGame()->m_scene = As<I3D_scene>(gameScene);
        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(gameScene) + 0x15C) = nullptr;
        C_MenuSoldier s3;
        s3.m_pos = S_vector(7.0f, 8.0f, 9.0f);
        s3.Setup(0.0f, 0.0f, 2.0f, 2.0f);
        ok &= s3.m_pos.x == 7.0f;
        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(gameScene) + 0x15C) = gameCamera;
        WM(r.msFailed, ok);
    }

    // --- a tick: turned about the axis by 0.009, the animations, the pose and the scene; none
    // without a model or a scene ---
    {
        bool ok = true;
        C_MenuSoldier s;
        S_MsWorld w = MsWorld(s);
        s.m_angle = 0.5f;
        g_gfLog.clear();
        s.Tick(50);
        ok &= MsLogIs({"m1 24", "m1 20 35", "pose update 50", "ss 20 50"}, g_gfLog);
        S_quat q;
        q.Make(S_vector(0.0f, 1.0f, 0.0f), 0.5f + 0.009f);
        q.Normalize();
        const S_quat &got = As<I3D_frame>(w.m.model)->m_rot;
        ok &= s.m_angle == 0.5f + 0.009f && got.v.x == q.v.x && got.v.y == q.v.y && got.v.z == q.v.z && got.s == q.s &&
              (As<I3D_frame>(w.m.model)->m_frameFlags & (FRMFLAGS_CHANGED | FRMFLAGS_ROT_DIRTY)) ==
                  (FRMFLAGS_CHANGED | FRMFLAGS_ROT_DIRTY) &&
              s.m_idleTime == 6000 - 50;
        s.m_axis = S_vector(1.0f, 0.0f, 0.0f);
        s.Tick(0);
        q.Make(S_vector(1.0f, 0.0f, 0.0f), 0.5f + 0.009f + 0.009f);
        ok &= std::fabs(As<I3D_frame>(w.m.model)->m_rot.v.x - q.v.x) < 1e-6f;
        I3D_model *model = s.m_model;
        s.m_model = nullptr;
        g_gfLog.clear();
        s.Tick(10);
        s.m_model = model;
        s.m_scene = nullptr;
        s.Tick(10);
        ok &= g_gfLog.empty();
        delete s.m_pose;
        WM(r.msTick, ok);
    }

    // --- the animations: an idle one on the other stage fading in over 800 ms, a pose on the other
    // of stages 2 and 3 over 400 ms, the model advanced by the time / 1.4, a new idle one at random
    // every 11 to 13 s ---
    {
        bool ok = true;
        C_MenuSoldier s;
        MsWorld(s);
        g_gfLog.clear();
        s.PlayIdle(2);
        ok &= MsLogIs({"m1 84 anim2 1 0 2"}, g_gfLog) && s.m_idleStage == 1 && s.m_idle == 2 && s.m_idleFade == 800;
        g_gfLog.clear();
        s.UpdateAnims(100);
        ok &= MsLogIs({"m1 94 0 1", "m1 94 1 0", "m1 20 71"}, g_gfLog) && s.m_idleFade == 700;
        g_gfLog.clear();
        s.UpdateAnims(700);
        ok &= MsLogIs({"m1 94 0 0.875", "m1 94 1 0.125", "m1 94 0 0", "m1 94 1 1", "m1 20 500"}, g_gfLog) &&
              s.m_idleFade == 0;
        g_gfLog.clear();
        s.PlayIdle(1);
        s.PlayIdle(-1);
        ok &= MsLogIs({"m1 84 anim1 0 0 2", "m1 84 anim0 0 1 2"}, g_gfLog) && s.m_idle == 0 && s.m_idleFade == 0 &&
              s.m_idleStage == 0;
        g_gfLog.clear();
        s.PlayIdle(0);
        s.PlayIdle(-1);
        ok &= MsLogIs({"m1 84 anim0 1 0 2", "m1 84 anim0 1 1 2"}, g_gfLog) && s.m_idleFade == 0 && s.m_idleStage == 1;
        s.m_idleStage = 0;
        g_gfLog.clear();
        s.PlayPose(4);
        ok &= MsLogIs({"m1 84 anim4 3 0 2"}, g_gfLog) && s.m_poseStage == 3 && s.m_poseAnim == 4 && s.m_poseFade == 400;
        g_gfLog.clear();
        s.UpdateAnims(100);
        ok &= MsLogIs({"m1 94 2 0.25", "m1 94 3 0.75", "m1 20 71"}, g_gfLog) && s.m_poseFade == 300;
        g_gfLog.clear();
        s.UpdateAnims(300);
        ok &= MsLogIs({"m1 94 2 0.1875", "m1 94 3 0.8125", "m1 94 2 0", "m1 94 3 1", "m1 20 214"}, g_gfLog);
        g_gfLog.clear();
        s.PlayPose(8);
        s.PlayPose(9);
        s.PlayPose(-1);
        ok &= MsLogIs({"m1 84 anim8 2 0 2"}, g_gfLog) && s.m_poseStage == 2 && s.m_poseAnim == 8;
        g_gfLog.clear();
        s.PlayPose(0);
        ok &= MsLogIs({"m1 84 anim0 3 0 2"}, g_gfLog) && s.m_poseAnim == 0;
        s.m_poseStage = 2;
        // One millisecond of a fade left: weighted, not ended.
        s.m_idleFade = 1;
        s.m_poseFade = 1;
        g_gfLog.clear();
        s.UpdateAnims(0);
        ok &= MsLogIs({"m1 94 1 0.00125", "m1 94 0 0.99875", "m1 94 3 0.000625", "m1 94 2 0.999375", "m1 20 0"}, g_gfLog) &&
              s.m_idleFade == 1 && s.m_poseFade == 1;
        s.m_idleFade = s.m_poseFade = 0;
        // The idle timer.
        s.m_poseFade = 0;
        s.m_idleTime = 30;
        // (A seed whose first draw tells three idles from four.)
        unsigned seed = 77;
        for (;; ++seed) {
            std::srand(seed);
            const int first = std::rand();
            if (first % 3 != first % 4)
                break;
        }
        std::srand(seed);
        const int a = std::rand(), b = std::rand();
        std::srand(seed);
        g_gfLog.clear();
        s.UpdateAnims(30);
        ok &= MsLogIs({"m1 20 21", CmText("m1 84 anim%d 1 0 2", a % 3)}, g_gfLog) && s.m_idleTime == b % 2000 + 11000;
        s.m_idleTime = 31;
        s.m_idleFade = 0;
        g_gfLog.clear();
        s.UpdateAnims(30);
        ok &= g_gfLog.size() == 1 && s.m_idleTime == 1;
        delete s.m_pose;
        WM(r.msAnims, ok);
    }

    // --- the holsters: items shown per frame (three a frame in slots 2 and 3), taken away (none
    // left: all hidden), hidden, released ---
    {
        bool ok = true;
        C_MenuSoldier s;
        MsWorld(s);
        S_MrObj *f20 = MsFrame("h20"), *f21 = MsFrame("h21"), *f50 = MsFrame("h50");
        s.m_holsters[2].frameCount = 2;
        s.m_holsters[2].frames[0] = As<I3D_frame>(f20);
        s.m_holsters[2].frames[1] = As<I3D_frame>(f21);
        s.m_holsters[5].frameCount = 1;
        s.m_holsters[5].frames[0] = As<I3D_frame>(f50);
        g_msHolsterSlots = {{40, 2}, {50, 5}};
        g_gfLog.clear();
        s.AddToHolsterSlot(2, 4);
        ok &= MsLogIs({"h20 on 1", "h20 on 1", "h20 on 1", "h21 on 1"}, g_gfLog) && s.m_holsters[2].itemCount == 4;
        g_gfLog.clear();
        s.AddToHolsterSlot(2, 3);
        ok &= MsLogIs({"h21 on 1", "h21 on 1"}, g_gfLog) && s.m_holsters[2].itemCount == 7;
        g_gfLog.clear();
        s.AddToHolsterSlot(2, 1);
        s.AddToHolsterSlot(HOLSTER_SLOT_COUNT, 1);
        ok &= g_gfLog.empty() && s.m_holsters[2].itemCount == 8;
        g_gfLog.clear();
        s.RemoveFromHolster(40, 4);
        ok &= MsLogIs({"h20 on 1", "h21 on 1"}, g_gfLog) && s.m_holsters[2].itemCount == 4;
        s.m_holsters[2].itemCount = 8;
        g_gfLog.clear();
        s.RemoveFromHolster(40, 6);
        ok &= MsLogIs({"h20 on 1", "h21 on 0"}, g_gfLog) && s.m_holsters[2].itemCount == 2;
        g_gfLog.clear();
        s.RemoveFromHolster(40, 4);
        ok &= MsLogIs({"h20 on 0", "h21 on 0"}, g_gfLog) && s.m_holsters[2].itemCount == 0;
        g_gfLog.clear();
        s.AddToHolster(50, 2);
        ok &= MsLogIs({"h50 on 1"}, g_gfLog) && s.m_holsters[5].itemCount == 2;
        g_gfLog.clear();
        s.RemoveFromHolster(50, 1);
        ok &= MsLogIs({"h50 on 1"}, g_gfLog) && s.m_holsters[5].itemCount == 1;
        g_gfLog.clear();
        s.RemoveFromHolster(99, 1);
        s.AddToHolster(99, 1);
        ok &= g_gfLog.empty();
        s.m_holsters[2].itemCount = 3;
        s.HideHolsters();
        ok &= MsLogIs({"h20 on 0", "h21 on 0", "h50 on 0"}, g_gfLog) && s.m_holsters[2].itemCount == 0 &&
              s.m_holsters[5].itemCount == 0 && s.m_holsters[2].frameCount == 2;
        g_gfLog.clear();
        s.ReleaseHolsters();
        ok &= MsLogIs({"h20 38 null 0", "h20 0", "h21 38 null 0", "h21 0", "h50 38 null 0", "h50 0"}, g_gfLog) &&
              s.m_holsters[2].frameCount == 0 && !s.m_holsters[2].frames[0] && !s.m_holsters[5].frames[0];
        delete s.m_pose;
        WM(r.msHolsters, ok);
    }

    // --- the holsters set up from the model's name past "mp_": the set loaded, its frames found
    // and hung on the joints (the back's on "d", EN06's "1" on "o"), hidden; not listed or no set:
    // none ---
    {
        bool ok = true;
        for (int pass = 0; pass < 2; ++pass) {
            C_MenuSoldier s;
            S_MsWorld w = MsWorld(s, pass == 0 ? "data\\models\\mp_GE03.i3d" : "mp_EN06");
            S_MrObj *hs = MsFrame("hs"), *d1 = MsFrame("d1"), *d6 = MsFrame("d6"), *d41 = MsFrame("d41");
            g_msChildren[{hs, "1"}] = d1;
            g_msChildren[{hs, "6"}] = d6;
            g_msChildren[{hs, "41"}] = d41;
            s.m_holsters[3].itemCount = 5;
            g_mrMade.clear();
            g_mrMade[FRAME_MODEL] = {hs};
            g_cacheLoads.clear();
            g_gfLog.clear();
            s.InitHolsters();
            const char *joint1 = pass == 0 ? "m1.c" : "m1.o";
            ok &= MsLogIs({"m1 48 c ffff", "m1 48 d ffff", "m1 48 o ffff", "create 9", "hs 38 m1 0", "hs 80 d1",
                           CmText("d1 38 %s 1", joint1), "d1 on 0", "hs 80 d41", "d41 38 m1.c 1", "d41 on 0", "hs 80 d6",
                           "d6 38 m1.d 1", "d6 on 0", "hs 38 null 0", "hs 0"},
                          MsLogWithout("hs 48"));
            ok &= g_cacheLoads == std::vector<std::string>({pass == 0 ? "ge_vyb01" : "en_vyb03"}) && g_cacheLoadFlags == 0;
            ok &= s.m_holsters[0].frameCount == 1 && s.m_holsters[0].frames[0] == As<I3D_frame>(d1) &&
                  s.m_holsters[2].frames[0] == As<I3D_frame>(d41) && s.m_holsters[4].frames[0] == As<I3D_frame>(d6) &&
                  s.m_holsters[3].itemCount == 0 && As<I3D_frame>(d1)->m_refCount == 1;
            int finds = 0;
            for (const std::string &l : g_gfLog)
                finds += l.rfind("hs 48", 0) == 0;
            ok &= finds == 24;
            delete s.m_pose;
            (void)w;
        }
        // Not listed, no set, a short name.
        for (const char *file : {"mp_XX01", "mp_CI01.i3d", "ab"}) {
            C_MenuSoldier s;
            MsWorld(s, file);
            g_mrMade.clear();
            g_gfLog.clear();
            s.InitHolsters();
            ok &= MsLogIs({"m1 48 c ffff", "m1 48 d ffff", "m1 48 o ffff"}, g_gfLog);
            delete s.m_pose;
        }
        // A set that does not load: the model released.
        {
            C_MenuSoldier s;
            MsWorld(s, "mp_IT01");
            S_MrObj *hs = MsFrame("hs");
            g_mrMade.clear();
            g_mrMade[FRAME_MODEL] = {hs};
            g_cacheFailName = "it_vyb01";
            g_gfLog.clear();
            s.InitHolsters();
            g_cacheFailName.clear();
            ok &= MsLogIs({"m1 48 c ffff", "m1 48 d ffff", "m1 48 o ffff", "create 9", "hs 38 null 0", "hs 0"}, g_gfLog);
            delete s.m_pose;
        }
        WM(r.msInit, ok);
    }

    // --- head gear: cloned (the old released), on "f" turned to +z at the head's offset; none:
    // the old kept; taken away ---
    {
        bool ok = true;
        C_MenuSoldier s;
        MsWorld(s);
        S_MrObj *src = MsFrame("src5"), *hm = MsFrame("hm"), *hm2 = MsFrame("hm2");
        g_itemModel = As<I3D_frame>(src);
        g_mrClone = As<I3D_frame>(hm);
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HEAD, 5);
        ok &= MsLogIs({"clone tree src5 0", "hm 38 m1.f 0", "hm 1c (0 0 1) 0", "hm 24"}, g_gfLog) &&
              s.m_headModel == As<I3D_frame>(hm) && s.m_headTypeId == 5 &&
              MsNear(As<I3D_frame>(hm)->GetPos(), equipment::g_headModelOffset);
        g_mrClone = As<I3D_frame>(hm2);
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HEAD, 6);
        ok &= MsLogIs({"hm 38 null 0", "release tree hm", "clone tree src5 0", "hm2 38 m1.f 0", "hm2 1c (0 0 1) 0",
                       "hm2 24"},
                      g_gfLog) &&
              s.m_headTypeId == 6;
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HEAD, 999);
        ok &= MsLogIs({"hm2 38 m1.f 0", "hm2 1c (0 0 1) 0", "hm2 24"}, g_gfLog) && s.m_headTypeId == 999;
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_HEAD, -1, false);
        ok &= MsLogIs({"hm2 38 null 0", "release tree hm2"}, g_gfLog) && !s.m_headModel && s.m_headTypeId == -1;
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_HEAD, -1, false);
        s.AddItem(MENU_SOLDIER_HEAD, -1);
        ok &= g_gfLog.empty() && s.m_headTypeId == -1;
        delete s.m_pose;
        WM(r.msHead, ok);
    }

    // --- the hands: cloned (the old released, the support hand off), the flash dropped, the pose's
    // weapon, on "HW1" at 0; the pose 3 + the carry mode (6: pose 5, 0.02 along x, turned to +y);
    // the support hand unless carry mode 0 or 2; taken away: the pose 3 ---
    {
        bool ok = true;
        C_MenuSoldier s;
        MsWorld(s);
        S_MrObj *src = MsFrame("src"), *hands = MsFrame("hands"), *hands2 = MsFrame("hands2"), *fl = MsFrame("fl");
        g_msChildren[{hands, "flash"}] = fl;
        g_itemModel = As<I3D_frame>(src);
        g_mrClone = As<I3D_frame>(hands);
        As<I3D_frame>(hands)->m_localMatrix.m[3][2] = 3.0f;
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HANDS, 7);
        ok &= MsLogIs({"clone tree src 0", "hands 48 flash ffff", "fl 38 null 0", "fl 0", "pose model hands 7",
                       "hands 38 m1.HW1 0", "m1 84 anim4 3 0 2", "hands 24", "pose support 1"},
                      g_gfLog) &&
              s.m_handsModel == As<I3D_frame>(hands) && s.m_handsTypeId == 7 && s.m_supportHand &&
              MsNear(As<I3D_frame>(hands)->GetPos(), S_vector(0.0f, 0.0f, 0.0f));
        g_mrClone = As<I3D_frame>(hands2);
        As<I3D_frame>(hands2)->m_localMatrix.m[3][0] = 5.0f;
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HANDS, 8);
        ok &= MsLogIs({"hands 38 null 0", "pose support 0", "release tree hands", "pose model null -1",
                       "clone tree src 0", "hands2 48 flash ffff", "pose model hands2 8", "hands2 38 m1.HW1 0",
                       "m1 84 anim5 2 0 2", "hands2 1c (0 1 0) 0", "hands2 24", "pose support 1"},
                      g_gfLog) &&
              MsNear(As<I3D_frame>(hands2)->GetPos(), S_vector(0.02f, 0.0f, 0.0f)) && s.m_handsTypeId == 8;
        g_mrClone = As<I3D_frame>(hands);
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HANDS, 9);
        ok &= MsLogIs({"hands2 38 null 0", "pose support 0", "release tree hands2", "pose model null -1",
                       "clone tree src 0", "hands 48 flash ffff", "fl 38 null 0", "fl 0", "pose model hands 9",
                       "hands 38 m1.HW1 0", "m1 84 anim5 3 0 2", "hands 24"},
                      g_gfLog) &&
              !s.m_supportHand;
        // Carry mode 0: no support hand either; an unknown type: the model kept, no pose.
        C_MsDef d0(0, 2, "z");
        g_gfItemDefs[20] = &d0;
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HANDS, 20);
        ok &= !s.m_supportHand && g_gfLog.back() == "hands 24" && MrHas("m1 84 anim3 2 0 2");
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HANDS, 999);
        ok &= MsLogIs({"pose model hands -1", "hands 38 m1.HW1 0"}, g_gfLog) && s.m_handsTypeId == 999;
        s.m_supportHand = true;
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_HANDS, -1, false);
        ok &= MsLogIs({"pose support 0", "pose model null -1", "hands 38 null 0", "release tree hands",
                       "m1 84 anim3 3 0 2"},
                      g_gfLog) &&
              !s.m_handsModel && s.m_handsTypeId == -1 && !s.m_supportHand;
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_HANDS, -1, false);
        ok &= MsLogIs({"m1 84 anim3 2 0 2"}, g_gfLog);
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HANDS, -1);
        ok &= g_gfLog.empty();
        g_gfItemDefs.erase(20);
        delete s.m_pose;
        WM(r.msHands, ok);
    }

    // --- the backpack: cloned (the old and the diving gear unlinked), on "d" turned to +z at the
    // backpack's offset; the diving gear (missing parts cloned): the mask on "f" at the head's
    // offset, two parts on "u" and "q", the bottles the backpack; replaced: the bottles released
    // and cloned again (fix); taken away ---
    {
        bool ok = true;
        C_MenuSoldier s;
        MsWorld(s);
        S_MrObj *src = MsFrame("src"), *bp = MsFrame("bp"), *g0 = MsFrame("g0"), *g1 = MsFrame("g1"),
                *g2 = MsFrame("g2"), *g3 = MsFrame("g3"), *gs[4];
        for (int i = 0; i < 4; ++i)
            gs[i] = MsFrame(CmText("gs%d", i).c_str());
        I3D_frame *gear[4] = {As<I3D_frame>(gs[0]), As<I3D_frame>(gs[1]), As<I3D_frame>(gs[2]), As<I3D_frame>(gs[3])};
        g_msGear[&d11] = gear;
        g_itemModel = As<I3D_frame>(src);
        g_mrClone = As<I3D_frame>(bp);
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_BACKPACK, 10);
        ok &= MsLogIs({"clone tree src 0", "diving gear b10", "bp 38 m1.d 0", "bp 1c (0 0 1) 0", "bp 24"}, g_gfLog) &&
              s.m_backpackModel == As<I3D_frame>(bp) && s.m_backpackTypeId == 10 &&
              MsNear(As<I3D_frame>(bp)->GetPos(), equipment::g_backpackModelOffset);
        // The diving gear: parts 1..3 there already.
        s.m_divingGear[1] = As<I3D_frame>(g1);
        s.m_divingGear[2] = As<I3D_frame>(g2);
        s.m_divingGear[3] = As<I3D_frame>(g3);
        As<I3D_frame>(g1)->m_localMatrix.m[3][0] = 4.0f;
        As<I3D_frame>(g2)->m_localMatrix.m[3][1] = 4.0f;
        g_mrClones = {As<I3D_frame>(MsFrame("bpx")), As<I3D_frame>(g0)};
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_BACKPACK, 11);
        ok &= MsLogIs({"bp 38 null 0", "g1 38 null 0", "g2 38 null 0", "g3 38 null 0", "release tree bp",
                       "clone tree src 0", "diving gear b11", "clone tree gs0 0", "g0 38 m1.f 2", "g0 1c (0 0 1) 0", "g0 on 1", "g0 24",
                       "g1 38 m1.u 2", "g1 1c (0 0 1) 0", "g1 on 1", "g1 24", "g2 38 m1.q 2", "g2 1c (0 0 1) 0",
                       "g2 on 1", "g2 24", "g3 38 m1.d 0", "g3 1c (0 0 1) 0", "g3 24"},
                      g_gfLog) &&
              s.m_backpackModel == As<I3D_frame>(g3) && s.m_divingGear[0] == As<I3D_frame>(g0) &&
              MsNear(As<I3D_frame>(g0)->GetPos(), equipment::g_headModelOffset) &&
              MsNear(As<I3D_frame>(g3)->GetPos(), equipment::g_backpackModelOffset) && s.m_backpackTypeId == 11 &&
              MsNear(As<I3D_frame>(g1)->GetPos(), S_vector(0.0f, 0.0f, 0.0f)) &&
              MsNear(As<I3D_frame>(g2)->GetPos(), S_vector(0.0f, 0.0f, 0.0f));
        // Again: the bottles released, their part cloned again.
        S_MrObj *g3b = MsFrame("g3b");
        g_mrClones = {As<I3D_frame>(MsFrame("bpy")), As<I3D_frame>(g3b)};
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_BACKPACK, 11);
        ok &= MsLogIs({"g3 38 null 0", "g0 38 null 0", "g1 38 null 0", "g2 38 null 0", "g3 38 null 0",
                       "release tree g3", "clone tree src 0", "diving gear b11", "clone tree gs3 0"},
                      std::vector<std::string>(g_gfLog.begin(), g_gfLog.begin() + std::min<size_t>(9, g_gfLog.size()))) &&
              s.m_divingGear[3] == As<I3D_frame>(g3b) && s.m_backpackModel == As<I3D_frame>(g3b);
        // No definition: the model kept; -1: no diving gear.
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_BACKPACK, -1);
        ok &= MsLogIs({"g3b 38 m1.d 0", "g3b 1c (0 0 1) 0", "g3b 24"}, g_gfLog) && s.m_backpackTypeId == -1;
        s.m_backpackTypeId = 10;
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_BACKPACK, -1, false);
        ok &= MsLogIs({"g3b 38 null 0", "release tree g3b"}, g_gfLog) && !s.m_backpackModel &&
              s.m_backpackTypeId == -1 && !s.m_divingGear[3] && s.m_divingGear[0] == As<I3D_frame>(g0);
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_BACKPACK, -1, false);
        s.AddItem(MENU_SOLDIER_BACKPACK, -1);
        ok &= g_gfLog.empty();
        g_msGear.clear();
        delete s.m_pose;
        WM(r.msBackpack, ok);
    }

    // --- the long gun: cloned (the old released), the flash dropped; without "backlink" not
    // placed; on "d" turned to +y, at the backlink's offset + (0.0275, 0, 0.09); with a magazine
    // (not behaviour 4) turned the magazine out, else turned less; tilted about x (0.15 with a
    // magazine, else 0.25) ---
    {
        bool ok = true;
        for (int kind = 0; kind < 3; ++kind) {
            C_MenuSoldier s;
            MsWorld(s);
            S_MrObj *src = MsFrame("src"), *lg = MsFrame("lg"), *bl = MsFrame("bl"), *blp = MsFrame("blp"),
                    *mag = MsFrame("mag");
            As<I3D_frame>(bl)->m_parent = As<I3D_frame>(blp);
            As<I3D_frame>(bl)->m_localMatrix.m[3][0] = 0.1f;
            As<I3D_frame>(bl)->m_localMatrix.m[3][1] = 0.2f;
            As<I3D_frame>(bl)->m_localMatrix.m[3][2] = -0.3f;
            S_matrix turn;
            turn.Identity();
            turn.m[0][0] = 0.0f;
            turn.m[0][1] = 1.0f;
            turn.m[1][0] = -1.0f;
            turn.m[1][1] = 0.0f;
            As<I3D_frame>(blp)->m_localMatrix = turn;
            g_msChildren[{lg, "backlink"}] = bl;
            g_msChildren[{lg, "flash"}] = MsFrame("lfl");
            if (kind != 2)
                g_msChildren[{lg, "mag1"}] = mag;
            g_itemModel = As<I3D_frame>(src);
            g_mrClone = As<I3D_frame>(lg);
            const int type = kind == 1 ? 14 : 13;
            g_gfLog.clear();
            s.AddItem(MENU_SOLDIER_LONG_GUN, type);
            ok &= MsLogIs({"clone tree src 0", "lg 48 flash ffff", "lfl 38 null 0", "lfl 0", "lg 48 backlink 20",
                           "lg 48 mag1 ffff",
                           "lg 38 m1.d 2", "lg 1c (0 1 0) 0", "lg on 1", "lg 24", "lg 24", "lg 24", "lg 24", "lg 24"},
                          g_gfLog) &&
                  s.m_longGunTypeId == type;
            // The same steps on a copy.
            S_MrObj *ref = MsFrame("ref");
            I3D_frame *f = As<I3D_frame>(ref);
            f->SetPosInline(equipment::g_longGunModelOffset);
            const S_vector link = equipment::BacklinkOffset(As<I3D_frame>(bl));
            f->SetPosInline(S_vector(link.x + 0.0275f, link.y, link.z + 0.09f));
            if (kind == 0) {
                S_vector pos = f->GetPos();
                pos.x -= 0.01f;
                pos.z -= 0.08f;
                equipment::RotateAboutYxX(f, -1.9634955f);
                f->SetPosInline(pos);
                pos = f->GetPos();
                equipment::Rotate(f, f->GetLocalMatrix()(0), -0.35f);
                f->SetPosInline(pos);
            } else {
                S_vector pos = f->GetPos();
                equipment::RotateAboutYxX(f, -0.25f);
                f->SetPosInline(pos);
                pos = f->GetPos();
                equipment::Rotate(f, f->GetLocalMatrix()(1), 0.1f);
                f->SetPosInline(pos);
            }
            equipment::Rotate(f, S_vector(1.0f, 0.0f, 0.0f), kind == 2 ? 0.25f : 0.15f);
            ok &= MsSameMatrix(As<I3D_frame>(lg)->GetLocalMatrix(), f->GetLocalMatrix());
            // Replaced: the old released.
            g_mrClone = As<I3D_frame>(MsFrame("lg2"));
            g_gfLog.clear();
            s.AddItem(MENU_SOLDIER_LONG_GUN, type);
            ok &= g_gfLog.size() >= 3 && g_gfLog[0] == "release tree lg" && g_gfLog[1] == "clone tree src 0" &&
                  g_gfLog[2] == "lg2 48 flash ffff" && g_gfLog.size() == 5;
            delete s.m_pose;
        }
        {
            C_MenuSoldier s;
            MsWorld(s);
            S_MrObj *lg = MsFrame("lg");
            s.m_longGunModel = As<I3D_frame>(lg);
            g_gfLog.clear();
            s.AddItem(MENU_SOLDIER_LONG_GUN, -1);
            ok &= MsLogIs({"lg 48 backlink 20", "lg 48 mag1 ffff"}, g_gfLog) && s.m_longGunTypeId == -1;
            s.m_longGunTypeId = 13;
            g_gfLog.clear();
            s.RemoveItem(MENU_SOLDIER_LONG_GUN, 3, false);
            ok &= MsLogIs({"lg 38 null 0", "release tree lg"}, g_gfLog) && !s.m_longGunModel && s.m_longGunTypeId == -1;
            g_gfLog.clear();
            s.RemoveItem(MENU_SOLDIER_LONG_GUN, 3, false);
            s.AddItem(MENU_SOLDIER_LONG_GUN, -1);
            ok &= g_gfLog.empty();
            delete s.m_pose;
        }
        WM(r.msLongGun, ok);
    }

    // --- the uniform: the model "mp_<model name>" opened and set; the old removed, a new pose,
    // the face, the holsters' items carried over, the items again ---
    {
        bool ok = true;
        C_MenuSoldier s;
        S_MsWorld w = MsWorld(s);
        S_MsModel m2 = MsModel("m2", "MODELS\\mp_GE03.i3d");
        S_MrObj *hs = MsFrame("hs"), *d1 = MsFrame("d1"), *hm = MsFrame("hm"), *src = MsFrame("src");
        g_msChildren[{hs, "1"}] = d1;
        s.m_holsters[0].itemCount = 1;
        s.m_supportHand = true;
        s.m_holsters[1].itemCount = -1;
        s.m_headModel = As<I3D_frame>(hm);
        s.m_headTypeId = 5;
        s.m_face = "face01";
        s.m_idle = 1;
        s.m_poseAnim = 6;
        s.m_pos = S_vector(1.0f, 2.0f, 3.0f);
        g_itemModel = As<I3D_frame>(src);
        g_mrClone = As<I3D_frame>(hm);
        g_mrMade.clear();
        g_mrMade[FRAME_MODEL] = {m2.model, hs};
        g_cacheLoads.clear();
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_BODY, 12);
        ok &= MsLogIs({"create 9", "ss 84 m1", "pose delete", "pose new", "pose attach m2 0 0", "m2 48 HW1 20",
                       "m2 48 d ffff", "m2 48 f ffff", "m2 48 u ffff", "m2 48 q ffff", "face m2 face01", "m2 48 c ffff",
                       "m2 48 d ffff", "m2 48 o ffff", "create 9", "hs 38 m2 0", "hs 80 d1", "d1 38 m2.c 1", "d1 on 0",
                       "hs 38 null 0", "hs 0", "d1 on 1", "hm 38 null 0", "release tree hm", "clone tree src 0",
                       "hm 38 m2.f 0", "hm 1c (0 0 1) 0", "hm 24", "ss 80 m2", "ss ec m2 (0 0 0)", "m2 0",
                       "m2 c (0 0 -1) 0", "m2 84 anim1 1 0 2", "m2 84 anim6 3 0 2", "m2 94 0 1", "m2 94 1 0",
                       "m2 94 0 0", "m2 94 1 1", "m2 94 2 0.25", "m2 94 3 0.75", "m2 94 2 0", "m2 94 3 1",
                       "m2 20 714", "m2 24"},
                      MsLogWithout("hs 48"));
        ok &= g_cacheLoads == std::vector<std::string>({"mp_GE03", "ge_vyb01"}) && s.m_model == As<I3D_model>(m2.model) &&
              s.m_handDummy == As<I3D_frame>(m2.hw1) && s.m_holsters[0].itemCount == 1 && !s.m_supportHand &&
              s.m_holsters[1].itemCount == 0 && MsNear(As<I3D_frame>(m2.model)->GetPos(), S_vector(1.0f, 2.0f, 3.0f));
        // The hands, the long gun and the backpack again too.
        {
            C_MenuSoldier s4;
            MsWorld(s4);
            S_MsModel m4 = MsModel("m4", "mp_Na01");
            s4.m_headModel = As<I3D_frame>(MsFrame("oe"));
            s4.m_headTypeId = 5;
            s4.m_handsModel = As<I3D_frame>(MsFrame("oh"));
            s4.m_handsTypeId = 7;
            s4.m_longGunModel = As<I3D_frame>(MsFrame("ol"));
            s4.m_longGunTypeId = 13;
            s4.m_backpackModel = As<I3D_frame>(MsFrame("ob"));
            s4.m_backpackTypeId = 10;
            g_mrClone = As<I3D_frame>(MsFrame("nn"));
            g_gfLog.clear();
            s4.SetModel(As<I3D_model>(m4.model));
            const auto at = [](const char *line) { return std::find(g_gfLog.begin(), g_gfLog.end(), line) - g_gfLog.begin(); };
            ok &= at("release tree oe") < at("release tree ol") && at("release tree ol") < at("release tree oh") &&
                  at("release tree oh") < at("release tree ob") &&
                  MrHas("release tree oh") && MrHas("release tree ol") && MrHas("release tree ob") &&
                  s4.m_handsTypeId == 7 && s4.m_longGunTypeId == 13 && s4.m_backpackTypeId == 10;
            delete s4.m_pose;
        }
        // A model without a scene: nothing.
        s.m_scene = nullptr;
        g_gfLog.clear();
        s.SetModel(As<I3D_model>(w.m.model));
        ok &= g_gfLog.empty() && s.m_model == As<I3D_model>(m2.model);
        s.m_scene = As<I3D_scene>(w.scene);
        // The first model: no holsters carried over.
        C_MenuSoldier s2;
        s2.m_scene = As<I3D_scene>(w.scene);
        S_MsModel m3 = MsModel("m3", "mp_Na01");
        g_gfLog.clear();
        s2.SetModel(As<I3D_model>(m3.model));
        ok &= g_gfLog.size() > 2 && g_gfLog[0] == "pose new" && g_gfLog[1] == "pose attach m3 0 0" &&
              s2.m_model == As<I3D_model>(m3.model);
        delete s2.m_pose;
        delete s.m_pose;
        WM(r.msBody, ok);
    }

    // --- the model cleared: the holsters released, the items away, the naked model (set and
    // removed again), the fields reset, the pose deleted; Release: the scene and the animations ---
    {
        bool ok = true;
        C_MenuSoldier s;
        S_MsWorld w = MsWorld(s);
        S_MsModel n2 = MsModel("n2", "mp_Na01");
        S_MrObj *f0 = MsFrame("h0"), *hm = MsFrame("hm");
        s.m_holsters[0].frameCount = 1;
        s.m_holsters[0].frames[0] = As<I3D_frame>(f0);
        s.m_headModel = As<I3D_frame>(hm);
        s.m_headTypeId = 5;
        s.m_pos = S_vector(1.0f, 2.0f, 3.0f);
        s.m_angle = 2.0f;
        s.m_idle = 2;
        s.m_poseAnim = 4;
        s.m_poseStage = 3;
        s.m_idleStage = 1;
        s.m_idleFade = 10;
        s.m_poseFade = 10;
        s.m_idleTime = 5;
        s.m_supportHand = true;
        for (int i = 0; i < 4; ++i)
            s.m_divingGear[i] = As<I3D_frame>(MsFrame("gx"));
        g_mrMade.clear();
        g_mrMade[FRAME_MODEL] = {n2.model};
        g_gfLog.clear();
        s.Release();
        const std::vector<std::string> log = g_gfLog;
        ok &= log.size() > 8 && log[0] == "h0 38 null 0" && log[1] == "h0 0" && log[2] == "hm 38 null 0" &&
              log[3] == "release tree hm" && log[4] == "m1 84 anim3 2 0 2" && log[5] == "create 9" &&
              log[6] == "ss 84 m1" && log[7] == "pose delete" && log[8] == "pose new";
        const size_t n = log.size();
        ok &= n > 15 && log[n - 12] == "pose delete" && log[n - 11] == "ss 84 n2" && log[n - 10] == "ss 0";
        for (int i = 0; i < 9; ++i)
            ok &= log[n - 9 + i] == CmText("anim%d 0", i);
        ok &= !s.m_model && !s.m_scene && !s.m_pose && s.m_pos.x == 0.0f && s.m_angle == 0.0f && s.m_idle == -1 &&
              s.m_poseAnim == -1 && s.m_poseStage == 2 && s.m_idleStage == 0 && s.m_idleFade == 0 &&
              s.m_poseFade == 0 && s.m_idleTime == 6000 && !s.m_supportHand && !s.m_handDummy && !s.m_jointD &&
              !s.m_jointF && !s.m_backpackJoint && !s.m_headModel && !s.m_divingGear[0] && !s.m_divingGear[3] &&
              s.m_jointU == As<I3D_frame>(n2.ju) && !s.m_anims[0] && !s.m_anims[8];
        // Nothing left: nothing done.
        g_gfLog.clear();
        s.Release();
        ok &= g_gfLog.empty();
        (void)w;
        WM(r.msClear, ok);
    }

    // --- drawn: driver states 5 off and 6 on for the scene's render, put back; no scene: nothing ---
    {
        bool ok = true;
        C_MenuSoldier s;
        MsWorld(s);
        g_mrStates = {{5, 7}, {6, 8}};
        g_gfLog.clear();
        s.Render();
        ok &= MsLogIs({"state 5 0", "state 6 1", "ss 78 0", "state 5 7", "state 6 8"}, g_gfLog);
        s.m_scene = nullptr;
        g_gfLog.clear();
        s.Render();
        ok &= g_gfLog.empty();
        delete s.m_pose;
        WM(r.msRender, ok);
    }

    // --- the face: switched on the model and kept ---
    {
        bool ok = true;
        C_MenuSoldier s;
        MsWorld(s);
        g_gfLog.clear();
        s.SetFace("face07");
        ok &= MsLogIs({"face m1 face07"}, g_gfLog) && std::strcmp(s.m_face, "face07") == 0;
        delete s.m_pose;
        WM(r.msFace, ok);
    }

    // --- the holsters' items: one added, one taken away, all hidden; the uniform taken away: the
    // model cleared ---
    {
        bool ok = true;
        C_MenuSoldier s;
        MsWorld(s);
        S_MrObj *f0 = MsFrame("h0"), *f1 = MsFrame("h1");
        s.m_holsters[0].frameCount = 2;
        s.m_holsters[0].frames[0] = As<I3D_frame>(f0);
        s.m_holsters[0].frames[1] = As<I3D_frame>(f1);
        g_msHolsterSlots = {{5, 0}, {999, 0}};
        g_gfLog.clear();
        s.AddItem(MENU_SOLDIER_HOLSTERS, 5);
        s.AddItem(MENU_SOLDIER_HOLSTERS, 5);
        s.AddItem(MENU_SOLDIER_HOLSTERS, 999);
        ok &= MsLogIs({"h0 on 1", "h1 on 1"}, g_gfLog) && s.m_holsters[0].itemCount == 2;
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_HOLSTERS, 5, false);
        ok &= MsLogIs({"h0 on 1", "h1 on 0"}, g_gfLog) && s.m_holsters[0].itemCount == 1;
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_HOLSTERS, 5, true);
        ok &= MsLogIs({"h0 on 0", "h1 on 0"}, g_gfLog) && s.m_holsters[0].itemCount == 0;
        g_gfLog.clear();
        s.AddItem(0x40, 5);
        s.RemoveItem(0x40, 5, false);
        ok &= g_gfLog.empty();
        // The items reset on their own: the holsters hidden, the naked model.
        s.m_holsters[0].itemCount = 2;
        s.m_longGunModel = As<I3D_frame>(MsFrame("rl"));
        s.m_backpackModel = As<I3D_frame>(MsFrame("rb"));
        S_MsModel n1 = MsModel("n1", "mp_Na01");
        g_mrMade.clear();
        g_mrMade[FRAME_MODEL] = {n1.model};
        g_cacheLoads.clear();
        g_gfLog.clear();
        s.ResetItems();
        ok &= MsLogIs({"rl 38 null 0", "release tree rl", "m1 84 anim3 3 0 2", "rb 38 null 0", "release tree rb", "h0 on 0",
                       "h1 on 0", "create 9"},
                      std::vector<std::string>(g_gfLog.begin(), g_gfLog.begin() + std::min<size_t>(8, g_gfLog.size()))) &&
              s.m_model == As<I3D_model>(n1.model) && s.m_holsters[0].itemCount == 0 &&
              g_cacheLoads == std::vector<std::string>({"mp_Na01"});
        S_MsModel n2 = MsModel("n2", "mp_Na01");
        g_mrMade.clear();
        g_mrMade[FRAME_MODEL] = {n2.model};
        g_gfLog.clear();
        s.RemoveItem(MENU_SOLDIER_BODY, -1, false);
        ok &= !s.m_model && !s.m_pose && MrHas("ss 84 n2");
        g_msHolsterSlots.clear();
        WM(r.msRemove, ok);
    }

    for (int i = 0; i < 3; ++i)
        g_driverVtable[driverSlots[i]] = oldDriver[i];
    g_cacheFill = oldFill;
    g_msNamer = nullptr;
    *reinterpret_cast<void ***>(GetGEffects()) = oldEffectsVt;
    GetGame()->m_scene = oldGameScene;
    g_gfItemDefs = oldDefs;
    g_itemModel = oldItemModel;
    g_mrClone = nullptr;
    g_mrClones.clear();
    g_msChildren.clear();
    g_gfLog.clear();
    std::printf("menusoldier: msSetup %d msFailed %d msTick %d msAnims %d msHolsters %d msInit %d msHead %d msHands %d "
                "msBackpack %d msLongGun %d msBody %d msClear %d msRender %d msFace %d msRemove %d\n",
                r.msSetup, r.msFailed, r.msTick, r.msAnims, r.msHolsters, r.msInit, r.msHead, r.msHands, r.msBackpack,
                r.msLongGun, r.msBody, r.msClear, r.msRender, r.msFace, r.msRemove);
}
