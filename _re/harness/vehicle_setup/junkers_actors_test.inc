// Part 7h's test (the actors build: build_item.bat also links src/game/junkers88.cpp; main runs
// JunkersActorsTest after AirActorsTest on every model): the Junkers 88 against oracles written from
// the original's code (the asm for the sums, the conversions and the lists). A Junkers model built
// of the real parts' names, its seats with user properties, fighters loaded by the model cache's
// fake (g_cacheFill), animations from seat_actors_test.inc's cache (their lengths here), the model
// stages' animations and times faked per frame; the engine and game calls are logged in order
// (item_test.inc's log). check_item.py reads the "junk:" line.

#include "game/junkers88.h"

// --- the script module's cutscenes (a minimal declaration: this file sees g_scriptModule as bytes,
// air_actors_test.inc), the camera's target, the first-person view, the hierarchy switch ----------

class C_ScriptModule {
public:
    void RunCutscene(int cutscene);
    void EndCutscene();
};
void C_ScriptModule::RunCutscene(int cutscene)
{
    ItLog("runcutscene %d", cutscene);
}
void C_ScriptModule::EndCutscene()
{
    ItLog("endcutscene");
}
// (The running cutscene's number: +0x834 in the original, +0x830 as g++ lays the class out.)
void JuSetCutscene(bool running, int cutscene)
{
    AaCutscene(running);
    std::memcpy(g_scriptModule + 0x830, &cutscene, 4);
    std::memcpy(g_scriptModule + 0x834, &cutscene, 4);
}
void C_CameraModule::SetTarget(I3D_frame *frame, int speed)
{
    ItLog("cameratarget %s %d", ItName(frame).c_str(), speed);
}
void C_game::ShowFirstPersonView(bool show)
{
    ItLog("firstperson %d", show);
    g_firstPerson = show;
}
void C_GEffectsModule::SetHierarchyOn(I3D_frame *frame, bool on, uint32_t typeMask)
{
    ItLog("hierarchy %s %d %x", ItName(frame).c_str(), on, typeMask);
}
// The static ray (the tracers'): recorded; g_juStaticHit: met at g_juStaticPos.
struct S_JuRay {
    S_vector from, dir;
    uint32_t flags;
};
std::vector<S_JuRay> g_juStaticRays;
bool g_juStaticHit;
S_vector g_juStaticPos;
S_CollisionVolume g_juStaticVolume;
S_CollisionVolume *C_CollisionWorld::RayTestStatic(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                                   S_vector *, uint32_t flags) const
{
    g_juStaticRays.push_back({from, dir, flags});
    if (!g_juStaticHit)
        return nullptr;
    if (hitPos)
        *hitPos = g_juStaticPos;
    return &g_juStaticVolume;
}

// --- the models' stages (slots 0x84 / 0x88 / 0x9C / 0xA0, the tick 0x20), the callbacks (0x28),
// the visuals made (driver 0x50), the FOV (camera 0x74) ----------------------------------------------

struct S_JuModel {
    I3D_animation_set *anim[8] = {};
    int time[8] = {};
};
std::map<const I3D_frame *, S_JuModel> g_juModels;
std::string JuAnimName(const I3D_animation_set *anim)
{
    return anim ? reinterpret_cast<const S_SeAnim *>(anim)->name : "0";
}
void __stdcall F_JuSetAnimation(I3D_frame *self, I3D_animation_set *anim, int stage, float weight, int flags)
{
    ItLog("anim %s %d %s %.9g %d", ItName(self).c_str(), stage, JuAnimName(anim).c_str(), weight, flags);
    g_juModels[self].anim[stage] = anim;
    g_juModels[self].time[stage] = 0;
}
I3D_animation_set *__stdcall F_JuGetAnimation(I3D_frame *self, int stage)
{
    return g_juModels[self].anim[stage];
}
void __stdcall F_JuSetAnimTime(I3D_frame *self, int stage, int time)
{
    ItLog("animtime %s %d %d", ItName(self).c_str(), stage, time);
    g_juModels[self].time[stage] = time;
}
int __stdcall F_JuGetAnimTime(I3D_frame *self, int stage)
{
    return g_juModels[self].time[stage];
}
void __stdcall F_JuTick(I3D_frame *self, int time)
{
    ItLog("tick %s %d", ItName(self).c_str(), time);
    S_JuModel &m = g_juModels[self];
    for (int s = 0; s < 8; ++s) {
        if (m.anim[s])
            m.time[s] += time;
    }
}
std::map<const I3D_frame *, std::pair<I3D_CALLBACK *, uint32_t>> g_juCallbacks;
void __stdcall F_JuSetCallback(I3D_frame *self, I3D_CALLBACK *callback, uint32_t messages)
{
    ItLog("callback %s %s %x", ItName(self).c_str(), !callback ? "0" : callback == &C_Junkers88::PathNote ? "note" : "none",
          messages);
    g_juCallbacks[self] = {callback, messages};
}
int g_juVisuals;
I3D_frame *__stdcall F_JuCreateVisual(void *, uint32_t type)
{
    ItLog("createvisual %u", type);
    I3D_frame *f = MakeFrame(g_frameVtable, FRAME_VISUAL, false);
    g_itNames[f] = "tracer" + std::to_string(g_juVisuals++);
    return f;
}
void __stdcall F_JuSetFov(I3D_frame *self, float fov)
{
    ItLog("fov %s %.9g", ItName(self).c_str(), fov);
}
// The animations' lengths (seat_actors_test.inc's fake, slot 0x08; 1000 ms unless listed).
std::map<std::string, int> g_juAnimEnds;
int __stdcall F_JuAnimEnd(S_SeAnim *self)
{
    auto it = g_juAnimEnds.find(self->name);
    return it == g_juAnimEnds.end() ? 1000 : it->second;
}
// The sphere sweeps (the falls): recorded; g_juSphereMet: met at g_sphereHookHit.
struct S_JuSphere {
    S_vector from, dir;
    float radius;
    uint32_t types, flags;
};
std::vector<S_JuSphere> g_juSpheres;
S_CollisionVolume *g_juSphereMet;
S_CollisionVolume *JuSphere(const S_vector &from, const S_vector &dir, float radius, uint32_t types, uint32_t flags)
{
    g_juSpheres.push_back({from, dir, radius, types, flags});
    return g_juSphereMet;
}

// --- the models the cache loads (g_cacheFill: by the file asked for) ----------------------------------

I3D_frame *JuChild(E_I3D_FRAME_TYPE type, const char *name, I3D_frame *parent)
{
    I3D_frame *f = NewFrame(type, name, parent);
    f->m_name = Stub(f).name.c_str();
    f->m_frameFlags |= 1u;                              // (shown)
    return f;
}
std::map<std::string, int> g_juLoads;
// The bomber's model loaded again (StopMission): its frames back as they were (the test's).
std::function<void()> g_juRestore;
void JuFill(I3D_model *object)
{
    auto *model = reinterpret_cast<I3D_frame *>(object);
    const std::string file = g_cacheLoads.back();
    ++g_juLoads[file];
    if (!g_stubs.count(model)) {                        // (a stub: the hierarchy boxes go through it)
        auto *stub = new S_StubFrame();
        stub->frame = model;
        stub->name = g_itNames.count(model) ? g_itNames[model] : file;
        g_stubs[model] = stub;
    }
    if (file == "ju88.4ds") {
        if (g_juRestore)
            g_juRestore();
    } else if (file == "WeaponOdds") {
        JuChild(FRAME_DUMMY, "odds", model);
        JuChild(FRAME_VISUAL, "trace", model);
    } else if (file == "#aircraftdum" || file == "#airfightdum") {
        JuChild(FRAME_DUMMY, file == "#aircraftdum" ? "camdummy" : "pathdummy", model);
    } else if (file == "la_Bf109Canyon") {
        JuChild(FRAME_VISUAL, "left_wing", model)->m_localMatrix(3) = S_vector(-3.0f, 0.0f, 0.0f);
        JuChild(FRAME_VISUAL, "right_wing", model)->m_localMatrix(3) = S_vector(3.0f, 0.0f, 0.0f);
        I3D_frame *gun = JuChild(FRAME_DUMMY, "weapon_r00", model);
        gun->m_localMatrix(3) = S_vector(1.0f, 0.0f, 2.0f);
        gun->m_localMatrix(2) = S_vector(0.0f, 0.6f, 0.8f);
        JuChild(FRAME_DUMMY, "weapon_l00", model)->m_localMatrix(3) = S_vector(-1.0f, 0.0f, 2.0f);
        JuChild(FRAME_DUMMY, "vrtule", model);
        JuChild(FRAME_VISUAL, "carriage_l00", model);
        JuChild(FRAME_VISUAL, "carriage_r00", model);
        JuChild(FRAME_DUMMY, "BODY", model);
    }
    for (I3D_frame *c = model->m_firstChild; c; c = c->m_nextSibling)
        c->m_ownerModel = model;
}

struct S_Ju {
    bool junk = true, junkProps = true, junkStart = true, junkChatter = true, junkSeats = true, junkParts = true,
         junkBreak = true, junkDamage = true, junkUpdate = true, junkDebris = true, junkTracers = true,
         junkFighters = true, junkFly = true, junkHit = true, junkTarget = true, junkSave = true, junkStop = true;
};
#define JU(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

S_vector4 WorldPos4Of(const S_matrix &m)
{
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

// The driver's FindFrame (slot 0x88): the name asked for kept whole; g_aaDriverFinds found.
std::string g_juFound;
I3D_frame *__stdcall F_JuDriverFind(void *, void *, const char *name)
{
    g_juFound = name;
    return g_aaDriverFinds;
}

int JuLogged(const std::string &line)
{
    return static_cast<int>(std::count(g_itLog.begin(), g_itLog.end(), line));
}

void JunkersActorsTest()
{
    S_Ju r;
    const auto reset = [&] {
        g_itLog.clear();
        g_activations.clear();
        g_deactivations.clear();
        g_volumeLog.clear();
        g_aaRays.clear();
        g_soundLog.clear();
        g_soundRequests.clear();
        g_soundFiles.clear();
        g_aaVelocity.clear();
        g_rbSplashes.clear();
        g_particlesDestroyed.clear();
        g_weaponEvents.clear();
        g_juStaticRays.clear();
        g_juSpheres.clear();
        g_cacheLoads.clear();
        g_juLoads.clear();
    };
    // The fakes: frames (logged as the earlier parts'), models' stages, sounds, the driver's frames
    // and visuals, the caches, the rays.
    const int frameSlots[] = {0x00, 0x08, 0x1C, 0x20, 0x24, 0x28, 0x30, 0x34, 0x38, 0x3C, 0x4C, 0x54, 0x58, 0x74,
                              0x84, 0x88, 0x9C, 0xA0};
    constexpr int FRAME_SLOTS = sizeof frameSlots / sizeof frameSlots[0];
    void *oldFrame[FRAME_SLOTS];
    for (int i = 0; i < FRAME_SLOTS; ++i)
        oldFrame[i] = g_frameVtable[frameSlots[i] / 4];
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_ObSetWorldDir);
    g_frameVtable[0x20 / 4] = reinterpret_cast<void *>(&F_JuTick);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x28 / 4] = reinterpret_cast<void *>(&F_JuSetCallback);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x34 / 4] = reinterpret_cast<void *>(&F_SeSetName);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x4C / 4] = reinterpret_cast<void *>(&F_ObDuplicate);
    g_frameVtable[0x54 / 4] = reinterpret_cast<void *>(&F_ItSetHierarchyAlpha);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_WaUpdateBound);
    g_frameVtable[0x74 / 4] = reinterpret_cast<void *>(&F_JuSetFov);
    g_frameVtable[0x84 / 4] = reinterpret_cast<void *>(&F_JuSetAnimation);
    g_frameVtable[0x88 / 4] = reinterpret_cast<void *>(&F_JuGetAnimation);
    g_frameVtable[0x9C / 4] = reinterpret_cast<void *>(&F_JuSetAnimTime);
    g_frameVtable[0xA0 / 4] = reinterpret_cast<void *>(&F_JuGetAnimTime);
    void *const oldCreate = g_driverVtable[0x54 / 4], *const oldVisual = g_driverVtable[0x50 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_MaCreateFrame);
    g_driverVtable[0x50 / 4] = reinterpret_cast<void *>(&F_JuCreateVisual);
    const int soundSlots[] = {0x00, 0x08, 0x24, 0x38, 0xB0};
    void *oldSound[5];
    for (int i = 0; i < 5; ++i)
        oldSound[i] = g_soundVtable[soundSlots[i] / 4];
    g_soundVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_soundVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_soundVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_soundVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_soundVtable[0xB0 / 4] = reinterpret_cast<void *>(&F_AaSoundVelocity);
    void *const oldAnimEnd = g_seAnimVtable[2];
    g_seAnimVtable[0] = reinterpret_cast<void *>(&F_SeAnimRelease);
    g_seAnimVtable[2] = reinterpret_cast<void *>(&F_JuAnimEnd);
    g_rayTestHook = &AaRay;
    g_sphereTestHook = &JuSphere;
    g_sphereHookSetsHit = true;
    void (*const oldFill)(I3D_model *) = g_cacheFill;
    g_cacheFill = &JuFill;
    const int oldSplashStep = g_rbSplashStep, oldSplashHandle = g_rbSplashHandle;
    g_rbSplashStep = 1;
    g_rbSplashHandle = 500;
    const bool oldSoundOn = g_maSoundOn;
    g_maSoundOn = true;
    const uint8_t oldRole = g_gameRole;
    g_gameRole = GAME_ROLE_LOCAL;
    const bool oldTextIds = g_textIds;
    g_textIds = true;
    const uint32_t oldTime = GetGame()->m_time;
    GetGame()->m_time = 20000;
    const bool oldFirstPerson = g_firstPerson;
    g_firstPerson = false;
    I3D_scene *const oldScenePtr = GetGame()->m_scene;
    GetGame()->m_scene = g_carScene;
    auto *sceneBase = reinterpret_cast<uint8_t *>(g_carScene);
    I3D_sector *const oldPrimary = *reinterpret_cast<I3D_sector **>(sceneBase + 0x1F8);
    *reinterpret_cast<I3D_sector **>(sceneBase + 0x1F8) = reinterpret_cast<I3D_sector *>(g_sector);
    I3D_frame *const oldCamera = *reinterpret_cast<I3D_frame **>(sceneBase + 0x15C);
    I3D_frame *camera = JuChild(FRAME_DUMMY, "jucamera", nullptr);
    *reinterpret_cast<I3D_frame **>(sceneBase + 0x15C) = camera;
    g_itNames[g_sector] = "sector";
    S_HudOption hud;
    S_PlayMode playMode;
    S_HudOption *const oldHud = g_gameProfile.m_hudOption;
    S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
    g_gameProfile.m_hudOption = &hud;
    g_gameProfile.m_playMode = &playMode;
    const int oldFailSeat = g_failInitSeat;
    int seed = static_cast<int>(g_frames.size()) * 13 + 7;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    // RandomFloat's next values (the harness's generator).
    const auto nextRandom = [](uint32_t &random, float max) { return AaNextRandom(random, max); };

    // --- the creator and a new Junkers: g_junkers88, its name (text 0x1BFE), the property defaults -------
    C_ActorCreator *creator = g_actorCreators[ACTOR_JUNKERS88];
    JU(r.junk, creator && std::string(creator->GetName()) == "Junkers88");
    {
        auto *other = static_cast<C_Junkers88 *>(creator->Create());
        JU(r.junk, other->GetType() == ACTOR_JUNKERS88 && g_junkers88 == other &&
                       std::string(other->m_name.c_str()) == "text 7166" && other->m_maxHealth == 1000.0f &&
                       other->m_fighterHealth == 10.0f && other->m_fighterDamage == 30.0f &&
                       other->m_unknown328 == 1.04719758f && other->m_unknown32C == 0x1BFE &&
                       other->m_firepower == 1.0f && other->m_interceptorHealth == 1.0f && other->GetSeatCount() == 5);
        // (The seats' flags table: the file's values until the first start.)
        static bool firstJunkers = true;
        JU(r.junk, !firstJunkers || (other->GetSeatFlags(0) == 1 && other->GetSeatFlags(1) == 0x102 &&
                                     other->GetSeatFlags(2) == 0x20 && other->GetSeatFlags(3) == 0x20 &&
                                     other->GetSeatFlags(4) == 0));
        firstJunkers = false;
        // (The script's SetupJunkers88: the firepower and the interceptors' health factor.)
        other->SetInterceptors(2.5f, 3.5f);
        JU(r.junk, other->m_firepower == 2.5f && other->m_interceptorHealth == 3.5f);
        I3D_frame *frame;
        creator->Destroy(other);
        JU(r.junk, g_junkers88 == nullptr && FindJunkersTargetFor(nullptr, &frame) == nullptr);
    }

    // The bomber: a model of the parts (intact, damaged "d", stump "f"), seats, propellers, blur.
    const float px = rnd(-100.0f, 100.0f), pz = rnd(-100.0f, 100.0f);
    I3D_frame *ju = RbBoxFrame(nullptr, "ju88", -10.0f, 10.0f, -2.0f, 2.0f, -8.0f, 8.0f, S_vector(px, 300.0f, pz));
    LinkUnder(ju, g_sector);
    ju->m_name = Stub(ju).name.c_str();
    ju->m_frameFlags |= 1u;
    static const char *const juFile = "ju88.4ds";
    std::memcpy(reinterpret_cast<uint8_t *>(ju) + 0x134, &juFile, sizeof juFile);
    std::map<std::string, I3D_frame *> f;
    const auto child = [&](E_I3D_FRAME_TYPE type, const char *name, I3D_frame *parent = nullptr) {
        I3D_frame *c = JuChild(type, name, parent ? parent : ju);
        f[name] = c;
        return c;
    };
    child(FRAME_DUMMY, "vrtule_l00")->m_localMatrix(3) = S_vector(-4.0f, 0.0f, 1.0f);
    child(FRAME_DUMMY, "vrtule_r00")->m_localMatrix(3) = S_vector(4.0f, 0.0f, 1.0f);
    child(FRAME_DUMMY, "BODY");
    child(FRAME_VISUAL, "bluur_l00");
    child(FRAME_VISUAL, "bluur_r00");
    static const char *const seatProps[5] = {"SEAT8 name12", "SEAT08,x", "name5 SEAT3", "SEAT12", nullptr};
    for (int s = 0; s < 5; ++s) {
        char name[8];
        std::snprintf(name, sizeof name, "SEAT%02d", s);
        child(FRAME_DUMMY, name)->m_userProps = seatProps[s];
        f[name]->m_localMatrix(3) = S_vector(0.0f, 1.0f, static_cast<float>(s));
    }
    child(FRAME_DUMMY, "cameraFPV00", f["SEAT00"]);
    // (Made in this order: a dependent part before its engine, another after.)
    const auto part = [&](const char *name, bool stump, float x) {
        const std::string base = name;
        I3D_frame *intact = child(FRAME_VISUAL, base.c_str());
        intact->m_localMatrix(3) = S_vector(x, 0.5f, -1.0f);
        child(FRAME_VISUAL, (base + "d").c_str());
        if (stump)
            child(FRAME_VISUAL, (base + "f").c_str());
        return intact;
    };
    part("left_wing00", false, -2.0f);
    part("engine_l00", true, -4.0f);
    child(FRAME_VISUAL, "bolt", f["engine_l00"]);
    child(FRAME_VISUAL, "piece", f["engine_l00d"]);
    part("listy_l00", false, -4.0f);
    part("engine_r00", false, 4.0f);
    part("flap_r00", false, 3.0f);
    part("listy_r00", true, 4.0f);
    part("left_wing01", true, -6.0f);
    part("flap_l01", false, -7.0f);
    part("tail00", false, 0.0f);
    child(FRAME_VISUAL, "fin00d");                      // (no intact frame)

    // (The frames as built: the model's reload puts them back.)
    struct S_JuFrameState {
        I3D_frame *frame, *parent;
        S_matrix local;
        uint32_t flags;
    };
    std::vector<S_JuFrameState> built;
    for (I3D_frame *c : [&] {
             std::vector<I3D_frame *> all;
             std::function<void(I3D_frame *)> walk = [&](I3D_frame *x) {
                 for (I3D_frame *k = x->m_firstChild; k; k = k->m_nextSibling) {
                     all.push_back(k);
                     walk(k);
                 }
             };
             walk(ju);
             return all;
         }())
        built.push_back({c, c->m_parent, c->m_localMatrix, c->m_frameFlags});
    g_juRestore = [&] {
        // (Out of whichever list they are in first: thrown pieces are in the sector's.)
        for (const S_JuFrameState &s : built)
            Unlink(s.frame);
        for (const S_JuFrameState &s : built)
            s.frame->m_firstChild = nullptr;
        ju->m_firstChild = nullptr;
        for (const S_JuFrameState &s : built) {
            LinkUnder(s.frame, s.parent);
            s.frame->m_localMatrix = s.local;
            s.frame->m_frameFlags = s.flags;
        }
    };

    auto *j = static_cast<C_Junkers88 *>(creator->Create());
    JU(r.junk, g_junkers88 == j && j->SetFrame(ju) && j->GetFrame() == ju);

    // --- the properties: 100 health, 200 a fighter's, 300 its damage, 400, 500; short chunks keep the
    // value; an unknown chunk is entered (its inner chunk read) ------------------------------------------
    {
        S_SaBytes in;
        in.Head(0x777, 6 + 10 + 10 + 10 + 10 + 10 + 8 + 6 + 10)
            .Head(100, 10).Val(2000.0f).Head(200, 10).Val(20.0f).Head(300, 10).Val(5.0f).Head(400, 10).Val(0.5f)
            .Head(500, 10).Val(7).Head(200, 8).Val(uint16_t(3)).Head(0x55, 6 + 10).Head(300, 10).Val(6.0f);
        S_SaReader rd(in);
        JU(r.junkProps, j->ReadProperties(rd.c) && j->m_maxHealth == 2000.0f && j->m_fighterHealth == 20.0f &&
                            j->m_fighterDamage == 6.0f && j->m_unknown328 == 0.5f && j->m_unknown32C == 7);
        j->m_maxHealth = 1000.0f;
        j->m_fighterHealth = 10.0f;
        j->m_fighterDamage = 30.0f;
    }

    // --- the start: a server's nothing but the activation; a client's: the chatter, the propellers, the
    // sounds, the seats and their weapons, the parts, the camera path, the damage look, the tracers' model
    {
        reset();
        j->StartMission(true);
        JU(r.junkStart, g_activations == std::vector<bool>{true} && g_itLog.empty() && !j->m_camera);
        g_failInitSeat = 1;
        g_maSoundsMade = 0;
        hud.shown = false;
        j->m_firepower = j->m_interceptorHealth = 3.0f;
        j->m_health = 5.0f;
        j->m_damageRate = 9.0f;
        j->m_slowTime = 4.0f;
        j->m_hitsByOthers = 2;
        j->m_engineDown[0] = j->m_engineDown[1] = true;
        reset();
        uint32_t random = g_waRandom;
        j->StartMission(false);
        JU(r.junkStart, g_activations == std::vector<bool>{false} && j->m_propeller[0] == f["vrtule_l00"] &&
                            j->m_propeller[1] == f["vrtule_r00"] && j->m_body == f["BODY"] && j->m_blur[0] == f["bluur_l00"] &&
                            j->m_blur[1] == f["bluur_r00"] && j->m_health == 1000.0f && j->m_damageRate == 0.0f &&
                            !j->m_engineDown[0] && !j->m_engineDown[1] && j->m_slowTime == 0.0f && j->m_hitsByOthers == 0 &&
                            hud.shown && j->m_firepower == 1.0f && j->m_interceptorHealth == 1.0f);
        // The sounds: the motors on the propellers (on, looping), the damage loops and the explosions in
        // the sector (off); each at a random pitch.
        static const char *const files[6] = {"ju88mtr1.wav", "ju88mtr2.wav", "DMGMTR01.wav", "DMGMTR02.wav",
                                             "expmtr01.wav", "expmtr02.wav"};
        I3D_frame *const parents[6] = {f["vrtule_r00"], f["vrtule_l00"], g_sector, g_sector, g_sector, g_sector};
        bool soundsOk = true;
        for (int i = 0; i < 6; ++i) {
            const auto *snd = reinterpret_cast<const I3D_frame *>(j->m_sounds[i]);
            const S_FakeSound &s = g_sounds[snd];
            const float pitch = 1.25f - nextRandom(random, 0.5f);
            soundsOk = soundsOk && snd && s.file == files[i] && s.type == I3DSOUND_POINT && s.loop == (i < 4) &&
                       s.range[0] == (i < 4 ? 3.0f : 100.0f) && s.range[1] == (i < 4 ? 100.0f : 200.0f) &&
                       s.range[2] == 0.5f && s.range[3] == 0.5f && s.playAt == 0 && s.volume == 1.0f &&
                       s.frequency == pitch && snd->m_parent == parents[i] && snd->IsOn() == (i < 2) &&
                       AaSoundCalls(snd) == std::string(i < 2 ? "PVFOX" : "PVFoX");
        }
        JU(r.junkStart, soundsOk);
        // The seats: their dummies, the flags table ("SEAT" at the start), seat 0's weapon (seat 1's
        // refused).
        JU(r.junkStart, j->m_seats[0].frame == f["SEAT00"] && j->m_seats[4].frame == f["SEAT04"] &&
                            j->GetSeatFlags(0) == 0x100 && j->GetSeatFlags(1) == 0x101 && j->GetSeatFlags(2) == 0 &&
                            j->GetSeatFlags(3) == 0x6 && j->GetSeatFlags(4) == 0 && j->GetSeatFlags(5) == 0 &&
                            j->GetSeatFlags(-1) == 0 && j->m_seats[1].flags == 0x101 && j->m_seatWeapons.size() == 5 &&
                            j->m_seatWeapons[0] && !j->m_seatWeapons[1] && !j->m_seatWeapons[2] &&
                            g_weaponEvents == std::vector<std::string>{"make", "init 0", "make", "init 1", "delete 1"});
        // The parts (made in the model's order): the stages' frames, the groups, the hangings and the
        // pieces thrown with the last stages.
        const auto p = [&](int i) { return j->m_parts[i]; };
        const bool countsOk = j->m_parts.size() == 10 && j->m_damage.left == 7 && j->m_damage.initial == 7 &&
                              j->m_engines.left == 2 && j->m_engines.initial == 2 && j->m_outer.left == 3 &&
                              j->m_outer.initial == 3 && j->m_dependent.size() == 4;
        JU(r.junkStart, countsOk);
        if (!countsOk) {
            std::fprintf(stderr, "junk: %zu parts\n", j->m_parts.size());
            std::exit(4);
        }
        JU(r.junkStart, p(0)->frames[1] == f["left_wing00d"] && p(1)->frames[1] == f["engine_l00d"] &&
                            p(1)->frames[0] == f["engine_l00"] && p(1)->frames[2] == f["engine_l00f"] && p(1)->stages == 2 &&
                            p(3)->stages == 1 && !p(3)->frames[2] && p(9)->stages == 0 && !p(9)->frames[0] &&
                            p(1)->engine && p(1)->outer && p(3)->engine && p(6)->outer && !p(6)->engine && !p(7)->outer &&
                            !p(0)->outer && !p(2)->outer &&
                            p(5)->index == 5 && p(9)->index == 9);
        JU(r.junkStart, j->m_damage.parts == std::vector<C_Junkers88::S_Part *>{p(1), p(3), p(6), p(7), p(8), p(9)} &&
                            j->m_dependent == std::vector<C_Junkers88::S_Part *>{p(0), p(2), p(4), p(5)} &&
                            j->m_engineParts[0] == p(1) && j->m_engineParts[1] == p(3) && j->m_bladeParts[0] == p(2) &&
                            j->m_bladeParts[1] == p(5) && p(2)->blades == 0 && p(5)->blades == 1 && p(1)->blades == -1);
        JU(r.junkStart, p(1)->hanging == std::vector<C_Junkers88::S_Part *>{p(0), p(2)} &&
                            p(3)->hanging == std::vector<C_Junkers88::S_Part *>{p(4), p(5)} && p(6)->hanging.empty() &&
                            p(0)->thrown == std::vector<C_Junkers88::S_Part *>{p(1), p(2), p(6), p(7)} &&
                            p(6)->thrown == std::vector<C_Junkers88::S_Part *>{p(7)} && p(1)->thrown.empty());
        // (The other stages where the intact frame is, hidden; the blades' intact frames hidden.)
        const S_vector wingAt = f["engine_l00"]->GetWorldPos();
        JU(r.junkStart, RbSame(f["engine_l00d"]->GetPos(), wingAt) && RbSame(f["engine_l00f"]->GetPos(), wingAt) &&
                            !f["engine_l00d"]->IsOn() && !f["engine_l00f"]->IsOn() && f["engine_l00"]->IsOn() &&
                            !f["listy_l00"]->IsOn() && !f["listy_r00"]->IsOn() && f["left_wing00"]->IsOn() &&
                            JuLogged("on listy_l00 0") == 1 && JuLogged("on engine_l00d 0") == 1 &&
                            JuLogged("update listy_l00") == 1);
        // The camera path ("#aircraftA1" at 1 ms; its notes), the fall (the body, 3 m), the damage look
        // ("#adamage0"), the hierarchy's sounds and models off, the tracers' model.
        C_Junkers88::S_Path *cam = j->m_camera;
        JU(r.junkStart, cam && cam->owner == ju && cam->model && cam->follow && cam->index == 1 &&
                            std::string(cam->name.c_str()) == "#aircraftA" && g_juLoads["#aircraftdum"] == 1 &&
                            JuAnimName(F_JuGetAnimation(cam->model, 0)) == "#aircraftA1" && F_JuGetAnimTime(cam->model, 0) == 1 &&
                            g_juCallbacks[cam->model->GetChild(0)].first == &C_Junkers88::PathNote &&
                            g_juCallbacks[cam->model->GetChild(0)].second == 0x1000);
        JU(r.junkStart, j->m_fall.frame == f["BODY"] && j->m_fall.radius == 3.0f && j->m_fall.falling &&
                            j->m_fall.pos.x == ju->GetWorldPos().x && JuAnimName(F_JuGetAnimation(ju, 1)) == "#adamage0" &&
                            g_seAnims["#adamage0"]->playMode == 2 && JuLogged("hierarchy ju88 0 88") == 1 &&
                            g_juLoads["WeaponOdds"] == 1 && j->m_trace && ItName(j->m_trace) == "trace" &&
                            j->m_trace->m_refCount == 2);
        // The chatter: the six groups' sounds and pauses.
        JU(r.junkStart, j->m_chatter[0].sounds.size() == 8 && j->m_chatter[1].sounds.size() == 6 &&
                            j->m_chatter[2].sounds.size() == 10 && j->m_chatter[3].sounds.size() == 7 &&
                            j->m_chatter[4].sounds.size() == 3 && j->m_chatter[5].sounds.size() == 12 &&
                            j->m_chatter[5].sounds[11] == 0x55D6007 && j->m_chatter[0].sounds[7] == 0x55D601E &&
                            j->m_chatter[2].sounds[9] == 0x55D6012 && j->m_chatter[4].sounds[0] == 0x55D6021 &&
                            j->m_chatter[0].minDelay == 2000 && j->m_chatter[0].range == 2000 &&
                            j->m_chatter[2].minDelay == 2000 && j->m_chatter[2].range == 2000 &&
                            j->m_chatter[3].minDelay == 300 && j->m_chatter[3].range == 300 &&
                            j->m_chatter[5].minDelay == 3000 && j->m_chatter[5].range == 6500 &&
                            j->m_chatter[4].minDelay == 0 && j->m_chatter[4].range == 0);
    }

    // --- the chatter: in turn (wrapping), one at a time; group 0 holds group 1 back 1.5 s ------------------
    {
        reset();
        std::srand(11);
        j->m_chatter[1].next = 5;
        j->m_chatter[1].delay = 0;
        j->Chatter(1);
        std::srand(11);
        const int pause = std::rand() % 2000 + 2000;
        JU(r.junkChatter, j->m_chatter[1].delay == pause && j->m_chatter[1].next == 0 && g_soundRequests.size() == 1 &&
                              g_soundFiles[0] == std::to_string(0x55D600D) + ".wav" &&
                              g_soundRequests[0].flags == (SOUND_SILENT_TO_AI | SOUND_AMBIENT) &&
                              g_soundRequests[0].chapter == 0 && !g_soundRequests[0].attachTo);
        j->Chatter(1);
        JU(r.junkChatter, g_soundRequests.size() == 1 && j->m_chatter[1].next == 0);
        j->m_chatter[0].delay = 0;
        j->m_chatter[0].next = 0;
        j->Chatter(0);
        JU(r.junkChatter, j->m_chatter[1].delay == 1500 && j->m_chatter[0].next == 1 && g_soundRequests.size() == 2 &&
                              g_soundFiles[1] == std::to_string(0x55D6017) + ".wav");
        // (Group 4 has no pause: none; rand taken all the same.)
        std::srand(12);
        j->Chatter(4);
        std::srand(12);
        std::rand();
        const int after = std::rand();
        std::srand(12);
        std::rand();
        JU(r.junkChatter, j->m_chatter[4].delay == 0 && g_soundRequests.size() == 3 && std::rand() == after &&
                              g_soundFiles[2] == std::to_string(0x55D6021) + ".wav" && j->m_chatter[4].next == 1);
        // (No sounds: nothing.)
        std::vector<int> kept;
        kept.swap(j->m_chatter[2].sounds);
        j->m_chatter[2].delay = 0;
        j->Chatter(2);
        kept.swap(j->m_chatter[2].sounds);
        JU(r.junkChatter, g_soundRequests.size() == 3 && j->m_chatter[2].delay == 0);
        // Group 5 without fighters around: the pause cleared, nothing.
        j->m_chatter[5].delay = 0;
        j->Chatter(5);
        JU(r.junkChatter, g_soundRequests.size() == 3 && j->m_chatter[5].delay == 0);
        for (C_Junkers88::S_Chatter &c : j->m_chatter)
            c.delay = 0;
    }

    // --- the seats: blocked, occupied (on the seat's dummy, along it; the weapon's gunner), left,
    // exited, the text, the camera, the weapons' slots -------------------------------------------------------
    {
        C_Human *gunner = new C_Human();
        I3D_frame *body = JuChild(FRAME_DUMMY, "gunner", nullptr);
        LinkUnder(body, g_sector);
        gunner->m_frame = body;
        JU(r.junkSeats, !j->IsSeatBlocked(0) && j->SetSeatBlocked(2, true) && !j->SetSeatBlocked(2, true) &&
                            j->m_seats[2].blocked && j->SetSeatBlocked(2, false) && !j->SetSeatBlocked(5, true) &&
                            !j->SetSeatBlocked(-1, true));
        S_FakeWeapon &weapon = Fake(j->m_seatWeapons[0]);
        body->m_localMatrix(3) = S_vector(5.0f, 6.0f, 7.0f);
        reset();
        j->SetSeatOccupant(0, gunner);
        JU(r.junkSeats, j->GetSeatOccupant(0) == gunner && j->FindSeat(gunner) == 0 && body->m_parent == f["SEAT00"] &&
                            body->GetPos().x == 0.0f && body->GetPos().z == 0.0f &&
                            SaLogIs({"link gunner SEAT00 0", "worlddir gunner 0 0 1 0", "update gunner"}) &&
                            weapon.gunner == gunner && weapon.gunners == 1);
        j->SetSeatOccupant(6, gunner);
        j->SetSeatOccupant(1, nullptr);
        JU(r.junkSeats, !j->GetSeatOccupant(1) && !j->GetSeatOccupant(6) && j->FindSeat(nullptr) == 4 &&
                            j->FindSeat(reinterpret_cast<C_Actor *>(camera)) == -1);
        // The camera: first person "cameraFPV*" under the dummy; else the camera path's dummy (the
        // occupant out of the seat meanwhile).
        g_firstPerson = true;
        reset();
        JU(r.junkSeats, j->GetSeatCamera(0) == f["cameraFPV00"] &&
                            SaLogIs({"link gunner 0 0", "link gunner SEAT00 0", "update gunner"}));
        g_firstPerson = false;
        JU(r.junkSeats, j->GetSeatCamera(0) == j->m_camera->model->GetChild(0) && !j->GetSeatCamera(5) &&
                            j->GetSeatText(0) == 12 && j->GetSeatText(2) == 5 && j->GetSeatText(3) == -1 &&
                            j->GetSeatText(-1) == -1 && j->IsSeatFirstPersonOnly(4) && j->IsSeatFirstPersonOnly(0) && !j->IsSeatFirstPersonOnly(5) &&
                            !j->IsSeatFirstPersonOnly(-1));
        // The weapons' slots: seat 0's, the others none.
        static int defMarker;
        weapon.rounds = 17;
        weapon.def = reinterpret_cast<C_ItemDef *>(&defMarker);
        weapon.aim = S_vector(1.0f, 2.0f, 3.0f);
        weapon.origin = S_vector(4.0f, 5.0f, 6.0f);
        weapon.c8 = true;
        int a8, b8, c8;
        JU(r.junkSeats, j->HasSeatWeapon(0) && !j->HasSeatWeapon(1) && !j->HasSeatWeapon(5) && !j->HasSeatWeapon(-1) &&
                            j->GetSeatWeaponRounds(0) == 17 && j->GetSeatWeaponRounds(1) == 0 &&
                            j->GetSeatWeaponDef(0) == weapon.def && j->GetSeatWeaponDef(1) == nullptr &&
                            RbSame(j->GetSeatWeaponAim(0), S_vector(1, 2, 3)) && j->GetSeatWeaponAim(1).x == 0.0f &&
                            RbSame(j->GetSeatWeaponOrigin(0), S_vector(4, 5, 6)) && j->GetSeatWeaponOrigin(3).y == 0.0f && j->GetSeatWeaponOrigin(3).x == 0.0f &&
                            j->UnknownAC(0) == 0.0f && j->UnknownAC(2) == 0.0f && j->UnknownC8(0, &a8, &b8, &c8) &&
                            weapon.c8Args[0] == &a8 && weapon.c8Args[2] == &c8 && !j->UnknownC8(1, nullptr, nullptr, nullptr));
        // (Third person: an AI's controls only; first person: anyone's.)
        float axes[4], buttons[4];
        gunner->controllerType = HUMAN_CONTROLLER_PLAYER;
        j->ApplySeatControls(0, axes, buttons);
        const int playerControls = weapon.controls;
        gunner->controllerType = HUMAN_CONTROLLER_ENEMY;
        j->ApplySeatControls(0, axes, buttons);
        const int aiControls = weapon.controls;
        j->ApplySeatControls(3, axes, buttons);
        g_firstPerson = true;
        gunner->controllerType = HUMAN_CONTROLLER_PLAYER;
        j->ApplySeatControls(0, axes, buttons);
        j->ApplySeatControls(4, axes, buttons);
        g_firstPerson = false;
        JU(r.junkSeats, playerControls == 0 && aiControls == 1 && weapon.controls == 2 && weapon.axes == axes &&
                            weapon.buttons == buttons);
        j->ShowSeatView(0, true);
        j->ShowSeatView(2, false);
        JU(r.junkSeats, weapon.views == 1 && weapon.view);
        // Left: the occupant returned, the seat free (its weapon's gunner too); exited: the same, the
        // seat still blocked.
        j->SetSeatBlocked(0, true);
        JU(r.junkSeats, j->LeaveSeat(0) == gunner && !j->GetSeatOccupant(0) && !j->m_seats[0].blocked &&
                            !weapon.gunner && weapon.gunners == 2 && !j->LeaveSeat(7) && j->LeaveSeat(2) == nullptr);
        j->SetSeatOccupant(0, gunner);
        j->SetSeatBlocked(0, true);
        JU(r.junkSeats, j->ExitSeat(0) && !j->GetSeatOccupant(0) && j->m_seats[0].blocked && !weapon.gunner &&
                            !j->ExitSeat(-2) && !j->ExitSeat(5));
        j->SetSeatBlocked(0, false);
        // The tick: the seat weapons'; the chatter group 5 while alive.
        weapon.ticks.clear();
        j->Tick(100);
        JU(r.junkSeats, weapon.ticks == std::vector<int>{100});
        // (Seat 5 is past the end: nothing.)
        reset();
        j->SetSeatOccupant(5, gunner);
        JU(r.junkSeats, g_itLog.empty() && body->m_parent == f["SEAT00"]);
        delete gunner;
    }

    // A sound that does not load: none (its frame released, the next made); no sound driver: none at
    // all (whatever was there cleared). The seats made anew (unblocked, their flags cleared); a seat
    // without its dummy: no flags.
    {
        j->ReleaseSounds();
        void *const oldOpen = g_soundVtable[0x74 / 4];
        g_soundVtable[0x74 / 4] = reinterpret_cast<void *>(&F_AaSoundOpen);
        g_aaFailFile = "DMGMTR01.wav";
        reset();
        j->LoadSounds();
        int releases = 0, links = 0, updates = 0;
        for (const std::string &line : g_itLog) {
            releases += line.rfind("release ", 0) == 0 ? 1 : 0;
            updates += line.rfind("update ", 0) == 0 ? 1 : 0;
            links += line.rfind("link ", 0) == 0 && line.size() > 13 &&
                             line.compare(line.size() - 13, 13, " vrtule_r00 0") == 0
                         ? 1
                         : 0;
        }
        JU(r.junkStart, !j->m_sounds[2] && j->m_sounds[0] && j->m_sounds[1] && j->m_sounds[3] && j->m_sounds[5] &&
                            releases == 1 && links == 1 && updates == 5);
        g_aaFailFile.clear();
        g_soundVtable[0x74 / 4] = oldOpen;
        j->ReleaseSounds();
        g_maSoundOn = false;
        static int soundMarker;
        for (I3D_sound *&sound : j->m_sounds)
            sound = reinterpret_cast<I3D_sound *>(&soundMarker);
        j->LoadSounds();
        bool none = true;
        for (I3D_sound *sound : j->m_sounds)
            none = none && !sound;
        JU(r.junkStart, none);
        g_maSoundOn = true;
        j->LoadSounds();
        j->m_seats[3].blocked = true;
        j->m_seats[3].flags = 0x40;
        I3D_frame *const seat3 = f["SEAT03"];
        Unlink(seat3);
        j->InitSeats();
        JU(r.junkStart, !j->m_seats[3].frame && !j->m_seats[3].blocked && j->m_seats[3].flags == 0 &&
                            j->GetSeatFlags(3) == 0 && j->GetSeatFlags(1) == 0x101 && j->m_seats[1].flags == 0);
        LinkUnder(seat3, ju);
        j->InitSeats();
        JU(r.junkStart, j->m_seats[3].frame == seat3 && j->GetSeatFlags(3) == 0x6);
    }

    // A new start: the model loaded again (its frames as built), the parts made anew.
    const auto restart = [&] {
        j->StopMission(false);
        j->StartMission(false);
    };
    const auto p = [&](int i) { return j->m_parts[i]; };
    using PartList = std::vector<C_Junkers88::S_Part *>;

    // --- the parts: on to the next stage (the current frame off, its children onto the next one, the
    // next's own pieces thrown, the next on), the parts hanging on it too, those thrown with the last
    // stage; the blades stop their engine (the blur, the motor, smoke, the damage sounds; quirk kept:
    // the other side's engine part falls) ----------------------------------------------------------------
    {
        reset();
        restart();
        JU(r.junkParts, g_juLoads["ju88.4ds"] == 1 && j->m_parts.size() == 10 && f["engine_l00"]->IsOn() &&
                            f["bolt"]->m_parent == f["engine_l00"]);
        // Engine left: stage 1 (its blades and the left wing's root with it).
        reset();
        uint32_t random = g_waRandom;
        j->m_velocity = {10.0f, -20.0f, 30.0f, 1.0f};
        j->Advance(p(1));
        const float a = nextRandom(random, 6.0f) - 3.0f, b = nextRandom(random, 2.0f) + 4.0f,
                    c = nextRandom(random, 6.0f) - 3.0f;
        const float s1 = nextRandom(random, 4.0f) - 2.0f, s2 = nextRandom(random, 4.0f) - 2.0f,
                    s3 = nextRandom(random, 4.0f) - 2.0f;
        JU(r.junkParts, p(1)->stage == 1 && !f["engine_l00"]->IsOn() && f["engine_l00d"]->IsOn() &&
                            f["bolt"]->m_parent == f["engine_l00d"] && f["piece"]->m_parent == g_sector &&
                            p(2)->stage == 1 && !f["listy_l00"]->IsOn() && f["listy_l00d"]->IsOn() && p(2)->blades == -1 &&
                            p(0)->stage == 1 && !f["left_wing00"]->IsOn() && f["left_wing00d"]->IsOn());
        JU(r.junkParts, j->m_debris.size() == 1 && j->m_debris[0]->frame == f["piece"] &&
                            j->m_debris[0]->velocity.x == 10.0f * 0.8f + c && j->m_debris[0]->velocity.y == -20.0f * 0.8f + b &&
                            j->m_debris[0]->velocity.z == 30.0f * 0.8f + a && j->m_debris[0]->spin.x == s3 &&
                            j->m_debris[0]->spin.y == s2 && j->m_debris[0]->spin.z == s1 &&
                            j->m_debris[0]->floor == f["piece"]->GetPos().y && JuLogged("link piece sector 1") == 1 &&
                            JuLogged("link bolt engine_l00d 1") == 1 && JuLogged("update engine_l00") == 1 &&
                            JuLogged("update bolt") == 1);
        // (The blades: the left blur off, the left motor off, smoke at the left propeller, its damage
        // sounds there; the right engine's part fell off.)
        const auto *dmg = reinterpret_cast<const I3D_frame *>(j->m_sounds[3]);
        const auto *exp = reinterpret_cast<const I3D_frame *>(j->m_sounds[5]);
        JU(r.junkParts, !f["bluur_l00"]->IsOn() && f["bluur_r00"]->IsOn() && AaSoundCalls(j->m_sounds[1]) == "o" &&
                            AaSoundCalls(j->m_sounds[0]).empty() && j->m_engineDown[0] && !j->m_engineDown[1] &&
                            g_rbSplashes.size() == 1 && g_rbSplashes[0].effect == 0x10 &&
                            g_rbSplashes[0].parent == f["vrtule_l00"] && RbSame(g_rbSplashes[0].pos, f["vrtule_l00"]->GetWorldPos()) &&
                            RbSame(g_rbSplashes[0].dir, S_vector(0, 0, -1)) && g_rbSplashes[0].faceCamera &&
                            !g_rbSplashes[0].transient && dmg->m_parent == f["vrtule_l00"] && exp->m_parent == f["vrtule_l00"] &&
                            AaSoundCalls(dmg) == "O" && AaSoundCalls(exp) == "OV" && g_sounds[exp].volume == 4.0f &&
                            dmg->GetPos().x == 0.0f);
        int propLinks = 0;
        for (const std::string &line : g_itLog)
            propLinks += line.rfind("link ", 0) == 0 && line.size() > 13 &&
                                 line.compare(line.size() - 13, 13, " vrtule_l00 0") == 0
                             ? 1
                             : 0;
        JU(r.junkParts, propLinks == 2);
        JU(r.junkParts, p(3)->stage == 1 && p(3)->hideIntact && j->m_damage.left == 6 && j->m_outer.left == 4 &&
                            j->m_damage.parts == PartList{p(1), p(9), p(6), p(7), p(8)} && j->m_outer.parts == PartList{p(3)});
        // Stage 2: the stump; the wing's root at its last stage throws the parts listed with it.
        reset();
        j->Advance(p(1));
        JU(r.junkParts, p(1)->stage == 2 && !f["engine_l00d"]->IsOn() && f["engine_l00f"]->IsOn() &&
                            f["bolt"]->m_parent == f["engine_l00f"] && p(2)->stage == 2 && !f["listy_l00d"]->IsOn() &&
                            p(0)->stage == 2 && !f["left_wing00d"]->IsOn() && j->m_debris.size() == 4 &&
                            j->m_debris[1]->frame == f["flap_l01"] && j->m_debris[2]->frame == f["left_wing01"] &&
                            j->m_debris[3]->frame == f["engine_l00f"] && g_rbSplashes.empty());
        // (At its last stage nothing more; a part without its intact frame: shown at once.)
        reset();
        j->Advance(p(1));
        j->Advance(p(9));
        JU(r.junkParts, p(1)->stage == 2 && g_itLog.size() == 2 && p(9)->stage == 1 && f["fin00d"]->IsOn() &&
                            !p(9)->HasNext() && p(1)->Shown() == f["engine_l00f"] && !p(0)->Shown() && !p(2)->HasNext());
        // The right blades: the right blur, the right motor, smoke at the right propeller (the left
        // engine's part not falling: past stage 0).
        reset();
        j->Advance(p(5));
        JU(r.junkParts, !f["bluur_r00"]->IsOn() && AaSoundCalls(j->m_sounds[0]) == "o" && j->m_engineDown[1] &&
                            g_rbSplashes.size() == 1 && g_rbSplashes[0].parent == f["vrtule_r00"] &&
                            reinterpret_cast<const I3D_frame *>(j->m_sounds[2])->m_parent == f["vrtule_r00"] &&
                            AaSoundCalls(j->m_sounds[4]) == "OV" && p(5)->stage == 1 && f["listy_r00d"]->IsOn() &&
                            p(1)->stage == 2);
        // (The intact frame hidden as well when its engine fell: at stage 2; the parts hanging on it
        // to its stage.)
        reset();
        j->Advance(p(3));
        JU(r.junkParts, p(3)->stage == 2 && !f["engine_r00"]->IsOn() && !f["engine_r00d"]->IsOn() &&
                            JuLogged("on engine_r00 0") == 1 && JuLogged("update engine_r00") == 1 && p(5)->stage == 2 && f["listy_r00f"]->IsOn() &&
                            p(4)->stage == 2 && !f["flap_r00"]->IsOn() && !f["flap_r00d"]->IsOn());
    }

    // (The right blades first: the left engine's part falls; it was the first of the list.)
    {
        restart();
        JU(r.junkParts, j->m_damage.parts[0] == p(1));
        j->Advance(p(5));
        JU(r.junkParts, p(1)->stage == 1 && p(1)->hideIntact && j->m_outer.parts == PartList{p(1)} &&
                            j->m_damage.parts.size() == 5);
    }

    // (A model without blades or engines: none of either.)
    {
        j->StopMission(false);
        for (const char *name : {"listy_l00", "listy_l00d", "listy_r00", "listy_r00d", "listy_r00f", "engine_l00", "engine_l00d",
                                 "engine_l00f", "engine_r00", "engine_r00d"})
            Unlink(f[name]);
        static int partMarker;
        auto *const marker = reinterpret_cast<C_Junkers88::S_Part *>(&partMarker);
        j->m_bladeParts[0] = j->m_bladeParts[1] = j->m_engineParts[0] = j->m_engineParts[1] = marker;
        j->StartMission(false);
        JU(r.junkParts, !j->m_bladeParts[0] && !j->m_bladeParts[1] && !j->m_engineParts[0] && !j->m_engineParts[1] &&
                            j->m_parts.size() == 6);
    }

    // A model with more parts: a two-letter part and one under another part (both found; a one-letter
    // "d" is none); the right wing's root ("right_wing00d", turned, with a stump) made first (hanging on
    // the right engine made after it; thrown with it the right outer wing made before and the engine's
    // parts made after) and again last
    // (hanging on that engine too, thrown with nothing; the wings it is thrown with: the right outer
    // wing and the engine's parts); the stages where the intact frame is, turned as it. A part's next
    // stage; loaded: the stage (a short chunk: 0), the shown frame switched on.
    {
        using Part = C_Junkers88::S_Part;
        j->StopMission(false);
        const auto first = [&](I3D_frame *x) {
            Unlink(x);
            x->m_parent = ju;
            x->m_nextSibling = ju->m_firstChild;
            ju->m_firstChild = x;
        };
        I3D_frame *before = JuChild(FRAME_VISUAL, "right_wing00d", ju);
        first(before);
        I3D_frame *outerRight = JuChild(FRAME_VISUAL, "right_wing01d", ju);
        first(outerRight);
        I3D_frame *rootIntact = JuChild(FRAME_VISUAL, "right_wing00", ju);
        S_quat turn;
        turn.Make(RotateYMatrix(0.5f));
        rootIntact->SetRot(turn);
        rootIntact->m_localMatrix(3) = S_vector(2.0f, 0.5f, -1.0f);
        I3D_frame *rootStump = JuChild(FRAME_VISUAL, "right_wing00f", ju);
        I3D_frame *after = JuChild(FRAME_VISUAL, "right_wing00d", ju);
        I3D_frame *ad = JuChild(FRAME_VISUAL, "ad", ju);
        I3D_frame *zd = JuChild(FRAME_VISUAL, "zd", ad);
        JuChild(FRAME_VISUAL, "d", ju);
        reset();
        j->StartMission(false);
        const auto part = [&](I3D_frame *damaged) -> Part * {
            for (Part *x : j->m_parts)
                if (x->frames[1] == damaged)
                    return x;
            return nullptr;
        };
        Part *pBefore = part(before), *pAfter = part(after), *pOuter = part(outerRight);
        Part *engineR = part(f["engine_r00d"]), *engineL = part(f["engine_l00d"]), *wingL = part(f["left_wing00d"]);
        JU(r.junkParts, j->m_parts.size() == 15 && pOuter && pOuter->index == 0 && pBefore && pBefore->index == 1 &&
                            pAfter && part(ad) && part(zd) && engineR && engineL && wingL);
        if (j->m_parts.size() == 15 && pOuter && pBefore && pAfter && engineR && engineL && wingL) {
            JU(r.junkParts, engineR->hanging == PartList{pBefore, part(f["flap_r00d"]), part(f["listy_r00d"]), pAfter} &&
                                engineL->hanging == PartList{wingL, part(f["listy_l00d"])} &&
                                pBefore->thrown == PartList{pOuter, engineR, part(f["flap_r00d"]), part(f["listy_r00d"])} &&
                                pAfter->thrown == PartList{part(f["listy_r00d"]), part(f["flap_r00d"]), pOuter, engineR} &&
                                wingL->thrown.size() == 4 && pBefore->frames[0] == rootIntact &&
                                pBefore->frames[2] == rootStump && pBefore->stages == 2);
            const S_quat &q = rootIntact->GetRot();
            JU(r.junkParts, std::fabs(before->GetRot().s - q.s) < 1e-6f && std::fabs(before->GetRot().v.y - q.v.y) < 1e-6f &&
                                std::fabs(rootStump->GetRot().v.y - q.v.y) < 1e-6f && q.v.y != 0.0f &&
                                JuLogged("update right_wing00d") == 2 && JuLogged("update right_wing00f") == 2);
            // (The next stage: the damaged frame from 0, the stump from 1.)
            wingL->stage = 0;
            pAfter->stage = 1;
            const bool next0 = wingL->HasNext(), next1 = pAfter->HasNext();
            wingL->stage = 1;
            const bool none1 = !wingL->HasNext();
            pAfter->stage = 2;
            const bool none2 = !pAfter->HasNext();
            wingL->stage = pAfter->stage = 0;
            JU(r.junkParts, next0 && next1 && none1 && none2);
            // (Loaded: a short chunk's stage 0; the intact frame shown: on again.)
            S_SaBytes shortChunk;
            shortChunk.Head(0x777, 6);
            S_SaReader rd(shortChunk);
            pBefore->stage = 2;
            pBefore->Load(rd.c);
            S_SaBytes e;
            e.Head(0x777, 6 + 5).Val(0).Val(uint8_t(1));
            S_SaReader rd2(e);
            reset();
            pAfter->Load(rd2.c);
            JU(r.junkParts, pBefore->stage == 0 && pAfter->stage == 0 && pAfter->hideIntact && rootIntact->IsOn() &&
                                JuLogged("on right_wing00 1") == 1);
        }
    }

    // (Without the tracers' model: none, its frame released all the same.)
    {
        g_cacheFailName = "WeaponOdds";
        j->StopMission(false);
        static int traceMarker;
        j->m_trace = reinterpret_cast<I3D_frame *>(&traceMarker);
        reset();
        j->StartMission(false);
        g_cacheFailName.clear();
        JU(r.junkStart, !j->m_trace && JuLogged("release new") == 1);
    }

    // --- the parts broken: advanced, into the broken list (no next stage) or the outer / engines list
    // by kind (neither: it stays), out of its list (the last in its place) ---------------------------------
    {
        restart();
        JU(r.junkBreak, j->m_parts.size() == 10 && p(1)->stage == 0 && j->m_damage.parts.size() == 6);
        PartList done, outer, engines;
        // The tail: one stage, broken.
        j->BreakPart(4, j->m_damage.parts, done, outer, engines, 1);
        JU(r.junkBreak, p(8)->stage == 1 && done == PartList{p(8)} && outer.empty() && engines.empty() &&
                            j->m_damage.parts == PartList{p(1), p(3), p(6), p(7), p(9)});
        // The left wing's tip: two stages, outer: its list.
        j->BreakPart(2, j->m_damage.parts, done, outer, engines, 1);
        JU(r.junkBreak, p(6)->stage == 1 && outer == PartList{p(6)} && j->m_damage.parts == PartList{p(1), p(3), p(9), p(7)});
        // (Twice at once: broken.)
        j->BreakPart(0, outer, done, outer, engines, 1);
        JU(r.junkBreak, p(6)->stage == 2 && outer.empty() && done == PartList{p(8), p(6)});
        j->BreakPart(0, j->m_damage.parts, done, outer, engines, 2);
        // (Its blades with it: the right engine's part falls out of the list too, quirk kept.)
        JU(r.junkBreak, p(1)->stage == 2 && done.size() == 3 && done[2] == p(1) &&
                            j->m_damage.parts == PartList{p(9), p(7)} && j->m_outer.parts == PartList{p(3)});
        // (In the list twice: the later one taken out.)
        PartList twice{p(8), p(8), p(6)};
        done.clear();
        j->BreakPart(0, twice, done, outer, engines, 1);
        JU(r.junkBreak, twice == PartList{p(8), p(6)} && done == PartList{p(8)});
    }

    // --- the damage: none without a rate; the rate a second times the firepower; the chatter 1; the look
    // in five steps (the fifth not shown); under 75 % the parts break down to a quarter at 0, under 25 %
    // the engines; at 0 an outer part, the look gone, shot down, the fall, the cutscene 31 -------------
    {
        restart();
        const float step = 1.0f / (j->m_maxHealth * 0.2f);
        const auto level = [step](float health) { return static_cast<int>(5.0f - step * health); };
        reset();
        j->UpdateDamage(100);
        JU(r.junkDamage, j->m_health == 1000.0f && g_itLog.empty() && g_soundRequests.empty());
        j->m_damageRate = 30.0f;
        j->m_firepower = 1.5f;
        j->m_chatter[1].delay = 0;
        reset();
        j->UpdateDamage(100);
        float health = 1000.0f - static_cast<float>(100) * 1.5f * 30.0f * 0.001f;
        JU(r.junkDamage, j->m_health == health && g_soundRequests.size() == 1 && j->m_chatter[1].delay != 0 &&
                             JuLogged("anim ju88 1 #adamage0 1 0") == 0);
        // (A level crossed: its look; level 4: both engines smoke, quirk kept: at the right propeller.)
        j->m_damage.left = j->m_engines.left = 0;
        for (const float before : {801.0f, 600.0f, 401.0f, 201.0f}) {
            j->m_health = before;
            reset();
            j->UpdateDamage(100);
            health = before - static_cast<float>(100) * 1.5f * 30.0f * 0.001f;
            const int to = level(health);
            const bool shown = to != level(before) && to <= 4;
            JU(r.junkDamage, j->m_health == health &&
                                 JuLogged("anim ju88 1 #adamage" + std::to_string(to) + " 1 0") == (shown ? 1 : 0) &&
                                 JuLogged("animrelease #adamage" + std::to_string(to)) == (shown ? 1 : 0) &&
                                 g_rbSplashes.size() == (to == 4 ? 2u : 0u) && (to != 4 || (j->m_engineDown[0] && j->m_engineDown[1] &&
                                                                                          g_rbSplashes[0].parent == f["vrtule_r00"] &&
                                                                                          g_rbSplashes[1].parent == f["vrtule_r00"] &&
                                                                                          AaSoundCalls(j->m_sounds[2]).empty() &&
                                                                                          AaSoundCalls(j->m_sounds[4]).empty())));
        }
        JU(r.junkDamage, level(796.5f) == 1 && level(595.5f) == 2 && level(396.5f) == 3 && level(196.5f) == 4);
        // Under 75 %: breaking down to the count of the health (a quarter at 0).
        restart();
        j->m_damageRate = 30.0f;
        j->m_chatter[1].delay = 9999;
        j->m_engines.left = 0;
        j->m_health = 760.0f;
        j->UpdateDamage(100);
        JU(r.junkDamage, j->m_damage.left == 7);
        j->m_health = 750.0f;
        j->m_damage.parts = PartList{p(8), p(7)};
        std::srand(31);
        j->UpdateDamage(100);
        std::srand(31);
        const int pick = std::rand() % 2;
        const auto target = [](float h, float max, float part, int initial, float share) {
            const float above = h / (max * part) - 1.0f;
            return static_cast<int>(above * static_cast<float>(static_cast<int>(initial * share)) +
                                    static_cast<float>(initial));
        };
        JU(r.junkDamage, target(745.5f, 1000.0f, 0.75f, 7, 0.75f) == 6 && j->m_damage.left == 6 &&
                             j->m_damage.parts.size() == 1 && j->m_damage.broken == PartList{pick == 0 ? p(8) : p(7)} &&
                             (pick == 0 ? p(8) : p(7))->stage == 1);
        // (Fix: none left to break: the count down by one, then nothing.)
        j->m_damage.parts.clear();
        j->m_health = 400.0f;
        j->UpdateDamage(100);
        JU(r.junkDamage, target(395.5f, 1000.0f, 0.75f, 7, 0.75f) == 4 && j->m_damage.left == 5);
        // (With parts enough: down to the count, 4 at 396.)
        restart();
        j->m_damageRate = 1.0f;
        j->m_firepower = 1.0f;
        j->m_chatter[1].delay = 9999;
        j->m_engines.left = 0;
        j->m_health = 396.0f;
        j->UpdateDamage(1);
        JU(r.junkDamage, target(j->m_health, 1000.0f, 0.75f, 7, 0.75f) == 4 && j->m_damage.left == 4 &&
                             j->m_damage.parts.size() + j->m_damage.broken.size() + j->m_outer.parts.size() >= 3);
        // Under 25 %: the engines (their own list, else an engine part not broken yet, both stages).
        restart();
        j->m_damageRate = 30.0f;
        j->m_chatter[1].delay = 9999;
        j->m_damage.left = 0;
        p(2)->blades = p(5)->blades = -1;
        j->m_engines.initial = j->m_engines.left = 10;
        j->m_engines.parts = PartList{p(3)};
        j->m_health = 100.0f;
        j->UpdateDamage(100);
        JU(r.junkDamage, target(95.5f, 1000.0f, 0.25f, 10, 0.3f) == 8 && j->m_engines.left == 8 &&
                             j->m_engines.parts.empty() && j->m_engines.broken == PartList{p(3), p(3)} && p(3)->stage == 2 &&
                             p(1)->stage == 0 && j->m_damage.parts == PartList{p(1), p(9), p(6), p(7), p(8)});
        // (The only engine left the first of the list.)
        j->m_engines.left = 9;
        j->UpdateDamage(100);
        JU(r.junkDamage, p(1)->stage == 2 && j->m_engines.broken.size() == 3 && j->m_engines.broken[2] == p(1));
        // (An engine with a stump in the engines' list: one stage, into the outer list.)
        restart();
        j->m_damageRate = 30.0f;
        j->m_chatter[1].delay = 9999;
        j->m_damage.left = 0;
        p(2)->blades = p(5)->blades = -1;
        j->m_engines.initial = j->m_engines.left = 10;
        j->m_engines.parts = PartList{p(1)};
        j->m_health = 100.0f;
        j->UpdateDamage(100);
        JU(r.junkDamage, p(1)->stage == 1 && j->m_outer.parts == PartList{p(1)} && j->m_engines.broken == PartList{p(3)});
        // At 0: the chatter 4, an outer part (its list, else a part not broken yet), the look gone, shot
        // down ("#jushotdwn<n>", the input off, first person off, the camera at the body), the fall,
        // the fighters disengaged, the cutscene 31.
        restart();
        j->m_damageRate = 30.0f;
        j->m_chatter[1].delay = 9999;
        j->m_chatter[4].delay = 0;
        // (ResetChatter keeps the groups' next sounds.)
        j->m_chatter[4].next = 0;
        j->m_damage.left = j->m_engines.left = 0;
        j->m_outer.parts = PartList{p(6)};
        j->m_health = 1.0f;
        j->m_fall.falling = false;
        GetGame()->m_playerInputEnabled = true;
        g_firstPerson = true;
        std::srand(41);
        reset();
        j->UpdateDamage(100);
        std::srand(41);
        std::rand();
        std::rand();
        const int down = std::rand() % 8 + 1;
        JU(r.junkDamage, j->m_health == 0.0f && j->m_outer.left == 2 && p(6)->stage == 1 && j->m_outer.parts.empty() &&
                             j->m_outer.broken == PartList{p(6)} && g_soundRequests.size() == 1 &&
                             g_soundFiles[0] == std::to_string(0x55D6021) + ".wav" &&
                             JuLogged("anim ju88 1 0 1 0") == 1 && JuLogged("getanim #adamage-1") == 0 &&
                             JuLogged("getanim #adamage5") == 0 &&
                             JuLogged("anim ju88 1 #jushotdwn" + std::to_string(down) + " 1 0") == 1 &&
                             g_seAnims["#jushotdwn" + std::to_string(down)]->playMode == 1 &&
                             !GetGame()->m_playerInputEnabled && !g_firstPerson && JuLogged("firstperson 0") == 1 &&
                             JuLogged("cameratarget BODY 45") == 1 && j->m_fall.falling &&
                             j->m_fall.pos.y == f["BODY"]->GetWorldPos().y && JuLogged("runcutscene 31") == 1);
        // (Down: nothing more.)
        reset();
        j->UpdateDamage(100);
        JU(r.junkDamage, g_itLog.empty() && j->m_health == 0.0f);
        // (No outer part listed: the last part not broken yet that is one, both stages.)
        restart();
        j->m_damageRate = 30.0f;
        j->m_chatter[1].delay = 9999;
        j->m_damage.left = j->m_engines.left = 0;
        p(2)->blades = p(5)->blades = -1;
        j->m_health = 1.0f;
        playMode.unknown0C = 2;
        j->SetState(1);
        j->m_fighters[0]->StartAttack();
        j->UpdateDamage(100);
        JU(r.junkDamage, p(6)->stage == 2 && j->m_outer.broken == PartList{p(6)} &&
                             j->m_damage.parts == PartList{p(1), p(3), p(9), p(7), p(8)} && j->m_debris.size() == 1 &&
                             !j->m_fighters[0]->attacking &&
                             g_juCallbacks[j->m_fighters[0]->path->model->GetChild(0)].first == &C_Junkers88::NoPathNote);
        // (The outer part the first of the list.)
        restart();
        j->m_damageRate = 30.0f;
        j->m_chatter[1].delay = 9999;
        j->m_damage.left = j->m_engines.left = 0;
        p(2)->blades = p(5)->blades = -1;
        j->m_damage.parts = PartList{p(6)};
        j->m_health = 1.0f;
        j->UpdateDamage(100);
        JU(r.junkDamage, p(6)->stage == 2 && j->m_outer.broken == PartList{p(6)} && j->m_damage.parts.empty());
        // (No engine left anywhere: the count only; at 0 no outer part either: none broken.)
        restart();
        j->m_damageRate = 30.0f;
        j->m_chatter[1].delay = 9999;
        j->m_damage.left = 0;
        p(2)->blades = p(5)->blades = -1;
        j->m_engines.initial = j->m_engines.left = 10;
        j->m_damage.parts = PartList{p(8)};
        j->m_health = 100.0f;
        j->UpdateDamage(100);
        JU(r.junkDamage, j->m_engines.left == 8 && p(8)->stage == 0 && j->m_engines.broken.empty());
        j->m_engines.left = 0;
        j->m_health = 1.0f;
        j->UpdateDamage(100);
        JU(r.junkDamage, j->m_health == 0.0f && p(8)->stage == 0 && j->m_outer.broken.empty());
        g_firstPerson = false;
    }

    // (At 0.5 the chatter all the same; a hit leaving 0.25: kept.)
    {
        restart();
        j->m_damage.left = j->m_engines.left = j->m_outer.left = 0;
        j->m_health = 0.5f;
        j->m_firepower = 1.0f;
        j->m_damageRate = 25.0f;
        j->m_chatter[1].delay = 0;
        reset();
        j->UpdateDamage(10);
        JU(r.junkDamage, j->m_health == 0.5f - static_cast<float>(10) * 1.0f * 25.0f * 0.001f && j->m_health > 0.0f &&
                             g_soundRequests.size() == 1 && j->m_chatter[1].delay != 0);
        j->m_damageRate = 0.0f;
    }

    // --- the update: nothing during another cutscene than 31 or hidden; the chatter's pauses; the camera
    // path moving the bomber; the model's animations; the seat weapons; the velocity; the propellers;
    // landing: the crash, the camera slowing over 7 s (its fractions of a ms kept) -----------------------
    {
        restart();
        C_Junkers88::S_Path *cam = j->m_camera;
        g_itNames[cam->model] = "campath";
        I3D_frame *dummy = cam->model->GetChild(0);
        JuSetCutscene(true, 5);
        reset();
        j->Update(20);
        JU(r.junkUpdate, g_itLog.empty());
        JuSetCutscene(false, 0);
        ju->m_frameFlags &= ~1u;
        j->Update(20);
        ju->m_frameFlags |= 1u;
        JU(r.junkUpdate, g_itLog.empty());
        // The pauses run down (not under 0); the path's dummy places the bomber.
        const int delays[6] = {100, 1, 0, 5, 30, 21};
        static int occupantMarker;
        j->m_seats[0].occupant = reinterpret_cast<C_Actor *>(&occupantMarker);
        for (int i = 0; i < 6; ++i)
            j->m_chatter[i].delay = delays[i];
        dummy->m_localMatrix = S_matrix();
        dummy->m_localMatrix.Identity();
        dummy->m_localMatrix(3) = S_vector(px + 5.0f, 280.0f, pz - 7.0f);
        dummy->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        const S_vector before = ju->GetWorldPos();
        const float beforeW = ju->GetMatrix().m[3][3];
        S_FakeWeapon &weapon = Fake(j->m_seatWeapons[0]);
        weapon.updates.clear();
        JuSetCutscene(true, 31);
        reset();
        j->Update(20);
        JuSetCutscene(false, 0);
        const S_vector &after = ju->GetWorldPos();
        const float k = 1000.0f / 20.0f;
        const bool seatKept = j->m_seats[0].occupant == reinterpret_cast<C_Actor *>(&occupantMarker);
        j->m_seats[0].occupant = nullptr;
        JU(r.junkUpdate, j->m_chatter[0].delay == 80 && j->m_chatter[1].delay == 0 && j->m_chatter[2].delay == 0 &&
                             j->m_chatter[3].delay == 0 && j->m_chatter[4].delay == 10 && j->m_chatter[5].delay == 1 && seatKept &&
                             F_JuGetAnimTime(cam->model, 0) == 21 && RbSame(after, S_vector(px + 5.0f, 280.0f, pz - 7.0f)) &&
                             j->m_velocity.x == (after.x - before.x) * k && j->m_velocity.y == (after.y - before.y) * k &&
                             j->m_velocity.z == (after.z - before.z) * k && j->m_velocity.w == beforeW &&
                             weapon.updates == std::vector<int>{20} && JuLogged("tick campath 20") == 1 &&
                             JuLogged("tick ju88 20") == 1 && JuLogged("update ju88") >= 1);
        // (The path's animation over: just short of its end, then the next one the rest of the time in.)
        g_juAnimEnds["#aircraftA1"] = 30;
        reset();
        j->Update(20);
        JU(r.junkUpdate, JuLogged("tick campath 8") == 1 && JuLogged("anim campath 0 #aircraftA2 1 0") == 1 &&
                             JuLogged("tick campath 11") == 1 && cam->index == 2 && F_JuGetAnimTime(cam->model, 0) == 11 &&
                             JuLogged("animtime campath 0 0") == 1 && JuLogged("animrelease #aircraftA2") == 1 &&
                             JuLogged("update campath") == 2);
        g_juAnimEnds.erase("#aircraftA1");
        g_juAnimEnds["#aircraftA2"] = 31;
        j->Update(20);
        g_juAnimEnds.erase("#aircraftA2");
        JU(r.junkUpdate, cam->index == 3 && F_JuGetAnimTime(cam->model, 0) == 0);
        // (Zero ms: the velocity kept.)
        j->m_velocity = {1.0f, 2.0f, 3.0f, 4.0f};
        j->Update(0);
        JU(r.junkUpdate, j->m_velocity.x == 1.0f && j->m_velocity.w == 4.0f);
        // The propellers by the game's time: the blur (left 0.001, right -0.0008 a ms), the blades'
        // damaged and stump frames.
        GetGame()->m_time = 12345;
        p(5)->frames[1]->m_frameFlags |= 1u;
        p(5)->frames[2]->m_frameFlags |= 1u;
        reset();
        j->TurnPropellers();
        char text[96];
        const float t = static_cast<float>(12345u);
        std::vector<std::string> spins;
        const auto spin = [&](const char *name, float angle) {
            std::snprintf(text, sizeof text, "worlddir %s 0 0 1 %.9g", name, angle);
            spins.push_back(text);
            spins.push_back(std::string("update ") + name);
        };
        spin("bluur_l00", t * 0.001f);
        spin("bluur_r00", -(t * 0.0008f));
        spin("listy_r00d", t * 0.02f);
        spin("listy_r00f", t * 0.002f);
        JU(r.junkUpdate, SaLogIs(spins));
        p(5)->frames[2]->m_frameFlags &= ~1u;
        f["bluur_r00"]->m_frameFlags &= ~1u;
        p(2)->frames[1]->m_frameFlags |= 1u;
        reset();
        j->TurnPropellers();
        spins.clear();
        spin("bluur_l00", t * 0.001f);
        spin("listy_r00d", t * 0.02f);
        spin("listy_l00d", t * 0.016f);
        JU(r.junkUpdate, SaLogIs(spins));
        I3D_frame *leftStump = JuChild(FRAME_VISUAL, "listy_l00f", nullptr);
        p(2)->frames[2] = leftStump;
        reset();
        j->TurnPropellers();
        p(2)->frames[2] = nullptr;
        spin("listy_l00f", t * 0.0016f);
        JU(r.junkUpdate, SaLogIs(spins));
        f["bluur_r00"]->m_frameFlags |= 1u;
        GetGame()->m_time = 20000;
        // Landing: the body's sphere (3 m) met on its way: there, the crash (both effects, a sound at the
        // camera), the bomber's look gone, the camera path stopped, the slow motion.
        S_CollisionVolume ground;
        g_juSphereMet = &ground;
        g_sphereHookHit = S_vector(px, 1.0f, pz);
        // (Not met: the body's place taken.)
        f["BODY"]->m_localMatrix(3) = S_vector(0.5f, -1.0f, 2.0f);
        f["BODY"]->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        const S_vector4 fallStart = j->m_fall.pos;
        g_juSphereMet = nullptr;
        j->m_fall.Update();
        const S_vector bodyAt = f["BODY"]->GetWorldPos();
        JU(r.junkUpdate, j->m_fall.pos.x == bodyAt.x && j->m_fall.pos.y == bodyAt.y && j->m_fall.pos.z == bodyAt.z &&
                             g_juSpheres.size() == 1 &&
                             RbSame(g_juSpheres[0].dir, S_vector(bodyAt.x - fallStart.x, bodyAt.y - fallStart.y,
                                                                 bodyAt.z - fallStart.z)));
        f["BODY"]->m_localMatrix(3) = S_vector(0.0f, 0.0f, 0.0f);
        f["BODY"]->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        g_juSpheres.clear();
        g_juSphereMet = &ground;
        const S_vector4 fallFrom = j->m_fall.pos;
        j->m_slowFraction = 0.5f;
        reset();
        j->Update(20);
        g_juSphereMet = nullptr;
        JU(r.junkUpdate, g_juSpheres.size() == 1 && RbSame(g_juSpheres[0].from, S_vector(fallFrom.x, fallFrom.y, fallFrom.z)) &&
                             g_juSpheres[0].radius == 3.0f && g_juSpheres[0].types == 0xFFFFFFFF && g_juSpheres[0].flags == 0 &&
                             !j->m_fall.falling && RbSame(f["BODY"]->GetPos(), S_vector(px, 1.0f, pz)) &&
                             g_rbSplashes.size() == 2 && g_rbSplashes[0].effect == 0x67 && g_rbSplashes[1].effect == 0x68 &&
                             RbSame(g_rbSplashes[0].pos, S_vector(px, 1.0f, pz)) && RbSame(g_rbSplashes[1].dir, S_vector(0, 1, 0)) &&
                             !g_rbSplashes[0].parent && g_rbSplashes[0].faceCamera && !g_rbSplashes[0].transient &&
                             RbSame(g_rbSplashes[0].dir, S_vector(0, 1, 0)) && g_rbSplashes[1].faceCamera &&
                             !g_rbSplashes[1].transient &&
                             g_soundRequests.size() == 1 && g_soundRequests[0].chapter == 6 && g_soundRequests[0].group == 5 &&
                             g_soundRequests[0].attachTo == camera && g_soundRequests[0].channel == 4 &&
                             g_soundRequests[0].flags == SOUND_SILENT_TO_AI && JuLogged("anim ju88 1 0 1 0") == 1 &&
                             !cam->follow && j->m_slowTime == 7000.0f && j->m_slowFraction == 0.0f &&
                             JuLogged("update BODY") == 1);
        // The slow motion: the path moves by the slowed time, its fractions kept.
        float slow = 7000.0f, fraction = 0.0f;
        int moved = 0;
        bool slowOk = true;
        for (int n = 0; n < 6 && slowOk; ++n) {
            const int ms = n == 3 ? 7 : 20;
            const int time = F_JuGetAnimTime(cam->model, 0);
            j->Update(ms);
            slow = slow - static_cast<float>(ms);
            if (slow <= 0.0f)
                slow = 0.0f;
            const float slowed = slow * 0.000142857141f * static_cast<float>(ms);
            int whole = static_cast<int>(slowed);
            fraction = (slowed - static_cast<float>(whole)) + fraction;
            if (fraction > 1.0f) {
                const int extra = static_cast<int>(fraction);
                whole += extra;
                fraction = fraction - static_cast<float>(extra);
            }
            moved += whole;
            slowOk = j->m_slowTime == slow && j->m_slowFraction == fraction &&
                     F_JuGetAnimTime(cam->model, 0) == time + whole;
        }
        JU(r.junkUpdate, slowOk && moved > 0);
        // (Run out: the path stays.)
        j->m_slowTime = 15.0f;
        j->Update(20);
        const int stopped = F_JuGetAnimTime(cam->model, 0);
        j->Update(20);
        JU(r.junkUpdate, j->m_slowTime == 0.0f && F_JuGetAnimTime(cam->model, 0) == stopped);
        // (0.5 left: slowed all the same; 0.25 left kept; a fraction of exactly 1 kept.)
        j->m_slowTime = 0.5f;
        j->m_slowFraction = 0.0f;
        reset();
        j->Update(20);
        JU(r.junkUpdate, j->m_slowTime == 0.0f && JuLogged("tick campath 0") == 1);
        j->m_slowTime = 20.25f;
        j->Update(20);
        JU(r.junkUpdate, j->m_slowTime == 0.25f);
        j->m_slowTime = 20.0f;
        j->m_slowFraction = 1.0f;
        reset();
        j->Update(20);
        JU(r.junkUpdate, j->m_slowFraction == 1.0f && JuLogged("tick campath 0") == 1);
        j->m_slowTime = j->m_slowFraction = 0.0f;
        // (Each update: the damage, the debris, the propellers, the tracers.)
        j->m_damageRate = 30.0f;
        const float healthBefore = j->m_health;
        I3D_frame *chunk = JuChild(FRAME_VISUAL, "chunk", nullptr);
        LinkUnder(chunk, camera);
        chunk->m_localMatrix(3) = S_vector(px, 50.0f, pz);
        j->ThrowDebris(chunk);
        const float fallSpeed = j->m_debris.back()->velocity.y;
        j->Fire(S_vector(px, 310.0f, pz), S_vector(0.0f, 0.0f, 1.0f));
        const float toGo = j->m_tracers.back()->distance;
        reset();
        j->Update(20);
        int turns = 0;
        for (const std::string &line : g_itLog)
            turns += line.rfind("worlddir bluur_l00 ", 0) == 0 ? 1 : 0;
        JU(r.junkUpdate, j->m_health < healthBefore && j->m_debris.back()->velocity.y < fallSpeed &&
                             j->m_tracers.back()->distance < toGo && turns == 1);
        j->m_damageRate = 0.0f;
    }

    // --- the debris: thrown at 0.8 of the bomber's velocity plus a random push (up 4..6 m/s), spinning
    // up to 2 rad/s about each axis; falling (9.81 m/s^2), turning; gone 500 m under where it started;
    // saved and found again by its name -------------------------------------------------------------------
    {
        restart();
        I3D_frame *rock = JuChild(FRAME_VISUAL, "rock", nullptr);
        LinkUnder(rock, camera);
        rock->m_localMatrix(3) = S_vector(px, 50.0f, pz);
        j->m_velocity = {2.0f, 4.0f, 6.0f, 7.0f};
        uint32_t random = g_waRandom;
        reset();
        j->ThrowDebris(rock);
        const float a = nextRandom(random, 6.0f) - 3.0f, b = nextRandom(random, 2.0f) + 4.0f,
                    c = nextRandom(random, 6.0f) - 3.0f;
        const float s1 = nextRandom(random, 4.0f) - 2.0f, s2 = nextRandom(random, 4.0f) - 2.0f,
                    s3 = nextRandom(random, 4.0f) - 2.0f;
        JU(r.junkDebris, j->m_debris.size() == 1 && rock->m_parent == g_sector && SaLogIs({"random 6", "random 2", "random 6", "link rock sector 1", "update rock", "random 4", "random 4",
                                                                                         "random 4"}));
        C_Junkers88::S_Debris *d = j->m_debris[0];
        JU(r.junkDebris, d->frame == rock && d->velocity.x == 2.0f * 0.8f + c && d->velocity.y == 4.0f * 0.8f + b &&
                             d->velocity.z == 6.0f * 0.8f + a && d->spin.x == s3 && d->spin.y == s2 && d->spin.z == s1 &&
                             d->floor == 50.0f);
        // (A 100 ms step: the speed, the place, the turn.)
        const C_Junkers88::S_Debris was = *d;
        const S_quat rot = rock->GetRot();
        JU(r.junkDebris, d->Update(100));
        const float t = 100.0f * 0.001f;
        const float vy = was.velocity.y - t * 9.81f;
        S_quat turned = rot.RotateByMatrix((RotateXMatrix(t * was.spin.x) * RotateYMatrix(t * was.spin.y)) *
                                           RotateZMatrix(t * was.spin.z));
        turned.Normalize();
        JU(r.junkDebris, d->velocity.y == vy && rock->GetPos().x == t * was.velocity.x + px &&
                             rock->GetPos().y == vy * t + 50.0f && rock->GetPos().z == pz + t * was.velocity.z &&
                             SaSame(&rock->m_rot, &turned, sizeof turned));
        // Saved: the motion, its frame's name, place and turn ({s, x, y, z}); read back: found by the
        // name, into the sector there.
        S_SaBytes e;
        const S_matrix w = rock->GetMatrix();
        S_quat q;
        q.Make(w);
        e.Head(0x777, 6 + 12 + 12 + 4 + 2 + 5 + 12 + 16);
        e.Put(&d->velocity, 12);
        e.Put(&d->spin, 12);
        e.Val(d->floor).Val(uint16_t(5));
        e.Put("rock", 5);
        e.Put(&w.m[3][0], 12);
        e.Val(q.s).Val(q.v.x).Val(q.v.y).Val(q.v.z);
        JU(r.junkDebris, SaWritten([&](C_chunk &ch) {
                             d->Save(ch);
                             return true;
                         }) == e.b);
        g_aaDriverFinds = rock;
        void *const oldFind = g_driverVtable[0x88 / 4];
        g_driverVtable[0x88 / 4] = reinterpret_cast<void *>(&F_JuDriverFind);
        Unlink(rock);
        LinkUnder(rock, camera);
        rock->m_localMatrix.Identity();
        S_SaReader rd(e);
        reset();
        C_Junkers88::S_Debris loaded(rd.c);
        JU(r.junkDebris, loaded.frame == rock && loaded.floor == d->floor && loaded.velocity.y == d->velocity.y &&
                             loaded.spin.z == d->spin.z && rock->m_parent == g_sector && RbSame(rock->GetPos(), AaVec(WorldPos4Of(w))) &&
                             std::fabs(rock->m_rot.s - q.s) < 1e-6f && g_juFound == "rock" &&
                             JuLogged("link rock sector 1") == 1 && JuLogged("update rock") == 1);
        // (Not found: nothing linked; a long name read whole, kept to 254 characters; none: empty.)
        g_aaDriverFinds = nullptr;
        S_SaBytes longName;
        const std::string name(300, 'n');
        longName.Head(0x777, 6 + 28 + 2 + 301 + 28);
        longName.Put(&d->velocity, 12);
        longName.Put(&d->spin, 12);
        longName.Val(d->floor).Val(uint16_t(301));
        longName.Put(name.c_str(), 301);
        longName.Put(&w.m[3][0], 12);
        longName.Val(q.s).Val(q.v.x).Val(q.v.y).Val(q.v.z);
        S_SaReader rd2(longName);
        reset();
        C_Junkers88::S_Debris missing(rd2.c);
        JU(r.junkDebris, !missing.frame && g_juFound == std::string(254, 'n') &&
                             missing.floor == d->floor);
        // (Found after a long name: the rest skipped byte by byte, the place after it; 255 characters
        // without an end kept to 254; an empty name and a one-byte one.)
        const auto named = [&](uint16_t length, const std::string &chars) {
            S_SaBytes b;
            b.Head(0x777, 6 + 28 + 2 + static_cast<int>(chars.size()) + 28);
            b.Put(&d->velocity, 12);
            b.Put(&d->spin, 12);
            b.Val(d->floor).Val(length);
            b.Put(chars.data(), static_cast<int>(chars.size()));
            b.Val(px + 1.0f).Val(7.0f).Val(pz - 1.0f);
            b.Val(q.s).Val(q.v.x).Val(q.v.y).Val(q.v.z);
            return b;
        };
        g_aaDriverFinds = rock;
        for (const auto &c : {std::make_pair(uint16_t(301), std::string(300, 'n') + '\0'),
                              std::make_pair(uint16_t(0xFF), std::string(255, 'n')),
                              std::make_pair(uint16_t(0), std::string()),
                              std::make_pair(uint16_t(1), std::string(1, '\0'))}) {
            Unlink(rock);
            LinkUnder(rock, camera);
            rock->m_localMatrix.Identity();
            S_SaBytes b = named(c.first, c.second);
            S_SaReader rd3(b);
            g_juFound = "?";
            C_Junkers88::S_Debris found(rd3.c);
            const std::string want = c.first == 0 || c.first == 1 ? std::string() : std::string(254, 'n');
            JU(r.junkDebris, found.frame == rock && g_juFound == want &&
                                 RbSame(rock->GetPos(), S_vector(px + 1.0f, 7.0f, pz - 1.0f)) &&
                                 std::fabs(rock->m_rot.s - q.s) < 1e-6f);
        }
        S_SaBytes cut;
        cut.Head(0x777, 6 + 28);
        cut.Put(&d->velocity, 12);
        cut.Put(&d->spin, 12);
        cut.Val(d->floor);
        S_SaReader rd5(cut);
        g_juFound = "?";
        g_aaDriverFinds = nullptr;
        C_Junkers88::S_Debris unnamed(rd5.c);
        JU(r.junkDebris, g_juFound.empty());
        // (Cut before the floor: 0.)
        S_SaBytes cutFloor;
        cutFloor.Head(0x777, 6 + 24);
        cutFloor.Put(&d->velocity, 12);
        cutFloor.Put(&d->spin, 12);
        S_SaReader rd6(cutFloor);
        C_Junkers88::S_Debris noFloor(rd6.c);
        JU(r.junkDebris, noFloor.floor == 0.0f && noFloor.spin.x == d->spin.x);
        Unlink(rock);
        LinkUnder(rock, g_sector);
        rock->m_localMatrix = w;
        g_driverVtable[0x88 / 4] = oldFind;
        // Gone 500 m under its start: false (moved all the same); the list drops it.
        d->velocity.y = -10000.0f;
        JU(r.junkDebris, !d->Update(100) && rock->GetPos().y < 50.0f - 500.0f);
        d->velocity.y = 0.0f;
        rock->m_localMatrix(3) = S_vector(px, 50.0f - 499.9f, pz);
        JU(r.junkDebris, d->Update(1) && rock->GetPos().y >= 50.0f - 500.0f);
        // (Exactly 500 m under: kept, turned and updated; 10 m more: gone.)
        rock->m_localMatrix(3) = S_vector(px, 50.0f - 500.0f, pz);
        reset();
        JU(r.junkDebris, d->Update(0) && JuLogged("update rock") == 1);
        rock->m_localMatrix(3) = S_vector(px, 50.0f - 510.0f, pz);
        JU(r.junkDebris, !d->Update(0));
        // (Two, the first gone: the other kept.)
        I3D_frame *pebble = JuChild(FRAME_VISUAL, "pebble", nullptr);
        LinkUnder(pebble, camera);
        pebble->m_localMatrix(3) = S_vector(px, 50.0f, pz);
        j->ThrowDebris(pebble);
        C_Junkers88::S_Debris *kept = j->m_debris[1];
        kept->velocity.y = 0.0f;
        reset();
        j->UpdateDebris(1);
        JU(r.junkDebris, j->m_debris.size() == 1 && j->m_debris[0] == kept);
        rock->m_localMatrix(3) = S_vector(px, 50.0f, pz);
        d = new C_Junkers88::S_Debris(rock, S_vector4{0.0f, 0.0f, 0.0f, 0.0f});
        j->m_debris.insert(j->m_debris.begin(), d);
        d->velocity.y = -5000.0f;
        kept->velocity.y = -5000.0f;
        reset();
        j->UpdateDebris(100);
        JU(r.junkDebris, j->m_debris.empty() && g_itLog.empty());
    }

    // --- the tracers: a copy of the "trace" visual in the sector (5, 5, 25 as large) along the gun, 500 m
    // to go (to what the static ray meets within a metre); 0.5 m a ms with a jitter; a whizz near the
    // camera; spent ones back in the pool -------------------------------------------------------------------
    {
        restart();
        const S_vector from(px, 300.0f, pz), along(0.0f, 0.0f, 1.0f);
        reset();
        j->Fire(from, along);
        JU(r.junkTracers, j->m_tracers.size() == 1 && j->m_tracerPool.empty());
        C_Junkers88::S_Tracer *tr = j->m_tracers[0];
        I3D_frame *frame = tr->frame;
        const std::string tn = ItName(frame);
        char type[32];
        std::snprintf(type, sizeof type, "createvisual %u", j->m_trace->GetVisualType());
        JU(r.junkTracers, tr->distance == 500.0f && frame->m_parent == g_sector && frame->m_scale.z == 25.0f &&
                              frame->m_scale.x == 5.0f && RbSame(frame->GetPos(), from) && g_juStaticRays.size() == 1 &&
                              RbSame(g_juStaticRays[0].from, from) && RbSame(g_juStaticRays[0].dir, along) &&
                              g_juStaticRays[0].flags == 0 &&
                              SaLogIs({type, "duplicate " + tn + " trace", "link " + tn + " sector 0",
                                       "worlddir " + tn + " 0 0 1 0", "update " + tn}));
        // (Something within a metre: that far.)
        g_juStaticHit = true;
        g_juStaticPos = S_vector(px + 0.3f, 300.2f, pz + 0.4f);
        tr->Place(from, along);
        const float dx = g_juStaticPos.x - from.x, dy = g_juStaticPos.y - from.y, dz = g_juStaticPos.z - from.z;
        const float square = (dz * dz + dy * dy) + dx * dx;
        JU(r.junkTracers, tr->distance == PrLength(square));
        g_juStaticPos = S_vector(px, 301.0f, pz);
        tr->Place(from, along);
        JU(r.junkTracers, tr->distance == 1.0f);
        g_juStaticPos = from;
        tr->Place(from, along);
        JU(r.junkTracers, tr->distance == 0.0f);
        g_juStaticHit = false;
        tr->Place(from, along);
        // Flying: 0.5 m a ms along its direction, a jitter (the third random on x); within 25 m of the
        // camera a whizz there.
        camera->m_localMatrix(3) = S_vector(px + 1.0f, 300.0f, pz + 20.0f);
        camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        uint32_t random = g_waRandom;
        reset();
        JU(r.junkTracers, tr->Update(10));
        const float ja = nextRandom(random, 0.4f) - 0.2f, jb = nextRandom(random, 0.4f) - 0.2f,
                    jc = nextRandom(random, 0.4f) - 0.2f;
        const S_vector at((0.0f * 5.0f + from.x) + jc, (0.0f * 5.0f + from.y) + jb, (1.0f * 5.0f + from.z) + ja);
        JU(r.junkTracers, tr->distance == 495.0f && RbSame(frame->GetPos(), at) && frame->GetPos().x == at.x &&
                              frame->GetPos().z == at.z && g_soundRequests.size() == 1 && g_soundRequests[0].chapter == 4 &&
                              g_soundRequests[0].group == 0x0B && !g_soundRequests[0].attachTo &&
                              g_soundRequests[0].channel == 0 && RbSame(g_soundRequests[0].pos, at) &&
                              SaLogIs({"random 0.400000006", "random 0.400000006", "random 0.400000006", "update " + tn}));
        // (25 m away: none; a tilted tracer along its local z.)
        camera->m_localMatrix(3) = S_vector(at.x, at.y, at.z + 5.0f + 25.0f);
        camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        frame->m_localMatrix(2) = S_vector(0.0f, 3.0f, 4.0f);
        reset();
        const S_vector at2 = frame->GetPos();
        random = g_waRandom;
        tr->Update(10);
        const float ka = nextRandom(random, 0.4f) - 0.2f, kb = nextRandom(random, 0.4f) - 0.2f,
                    kc = nextRandom(random, 0.4f) - 0.2f;
        S_vector dir(0.0f, 3.0f, 4.0f);
        PrNormalize(dir, (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
        const S_vector at3((dir.x * 5.0f + at2.x) + kc, (dir.y * 5.0f + at2.y) + kb, (dir.z * 5.0f + at2.z) + ka);
        JU(r.junkTracers, frame->GetPos().y == at3.y && frame->GetPos().z == at3.z && g_soundRequests.empty());
        frame->m_localMatrix(2) = S_vector(0.0f, 0.0f, 1.0f);
        // Saved: its place, its world direction, the distance; read back: a new copy there.
        S_SaBytes e;
        const S_matrix &w = frame->GetMatrix();
        S_vector wd(w.m[2][0], w.m[2][1], w.m[2][2]);
        PrNormalize(wd, (wd.y * wd.y + wd.z * wd.z) + wd.x * wd.x);
        e.Head(0x777, 6 + 28);
        e.Put(&w.m[3][0], 12);
        e.Put(&wd, 12);
        e.Val(tr->distance);
        JU(r.junkTracers, SaWritten([&](C_chunk &ch) {
                              tr->Save(ch);
                              return true;
                          }) == e.b);
        S_SaReader rd(e);
        reset();
        C_Junkers88::S_Tracer loaded(j->m_trace, rd.c);
        JU(r.junkTracers, loaded.distance == tr->distance && RbSame(loaded.frame->GetPos(), AaVec(WorldPos4Of(w))) &&
                              loaded.frame->m_parent == g_sector && loaded.frame->m_scale.y == 5.0f &&
                              JuLogged("duplicate " + ItName(loaded.frame) + " trace") == 1);
        // Spent (under 0.5 m a ms to go): off, into the pool; the next shot takes it again.
        tr->distance = 3.0f;
        reset();
        j->UpdateTracers(10);
        JU(r.junkTracers, j->m_tracers.empty() && j->m_tracerPool.size() == 1 && !frame->IsOn() &&
                              SaLogIs({"on " + tn + " 0"}));
        reset();
        j->Fire(S_vector(px, 310.0f, pz), along);
        JU(r.junkTracers, j->m_tracers.size() == 1 && j->m_tracers[0] == tr && j->m_tracerPool.empty() &&
                              frame->IsOn() && tr->distance == 500.0f && frame->GetPos().y == 310.0f &&
                              SaLogIs({"on " + tn + " 1", "worlddir " + tn + " 0 0 1 0", "update " + tn}));
        // (Exactly 0 left: spent too; a little more: on.)
        tr->distance = 5.0f;
        JU(r.junkTracers, !tr->Update(10));
        tr->distance = 5.01f;
        JU(r.junkTracers, tr->Update(10));
        // (Exactly 25 m above the camera: no whizz.)
        tr->distance = 400.0f;
        frame->m_localMatrix(2) = S_vector(0.0f, 0.0f, 1.0f);
        uint32_t random4 = g_waRandom;
        const float qa = nextRandom(random4, 0.4f) - 0.2f, qb = nextRandom(random4, 0.4f) - 0.2f,
                    qc = nextRandom(random4, 0.4f) - 0.2f;
        const S_vector at4 = frame->GetPos();
        const S_vector next4((0.0f * 5.0f + at4.x) + qc, (0.0f * 5.0f + at4.y) + qb, (1.0f * 5.0f + at4.z) + qa);
        camera->m_localMatrix(3) = S_vector(next4.x, next4.y + 25.0f, next4.z);
        camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        reset();
        tr->Update(10);
        JU(r.junkTracers, RbSame(frame->GetPos(), next4) && g_soundRequests.empty() &&
                              camera->GetWorldPos().y - next4.y == 25.0f);
        // (Without the tracers' model: none fired.)
        I3D_frame *trace = j->m_trace;
        j->m_trace = nullptr;
        j->Fire(from, along);
        j->m_trace = trace;
        JU(r.junkTracers, j->m_tracers.size() == 1);
        // (Two, the first spent: the other kept.)
        j->Fire(S_vector(px, 310.0f, pz), along);
        JU(r.junkTracers, j->m_tracers.size() == 2);
        C_Junkers88::S_Tracer *second = j->m_tracers[1];
        j->m_tracers[0]->distance = 1.0f;
        second->distance = 400.0f;
        j->UpdateTracers(10);
        JU(r.junkTracers, j->m_tracers.size() == 1 && j->m_tracers[0] == second);
        S_SaBytes shortChunk;
        shortChunk.Head(0x777, 6);
        S_SaReader rd2(shortChunk);
        reset();
        C_Junkers88::S_Tracer empty(j->m_trace, rd2.c);
        JU(r.junkTracers, RbSame(empty.frame->GetPos(), S_vector(0, 0, 0)) &&
                              JuLogged("worlddir " + ItName(empty.frame) + " 0 0 1 0") == 1 && empty.distance == 0.0f);
        empty.frame->Release();
        loaded.frame->Release();
    }

    // --- the fighters (Junkers88_SetState): once, `count` of the paths 1..9 at random, the hit points by
    // the difficulty; each a model "interceptor_<path>" under the bomber, its motor and guns, its path
    // ("#airfight_0<n>_A1" on), the damage looks, its sphere in the world, its guns' frames ------------------
    using Fighter = C_Junkers88::S_Fighter;
    const auto fighterFrame = [](const Fighter *fighter, const char *name) {
        return fighter->model->FindChildFrame(name, FRAME_MASK_ANY);
    };
    {
        restart();
        playMode.unknown0C = 1;
        std::srand(51);
        uint32_t random = g_waRandom;
        reset();
        j->SetState(3);
        // (The oracle: the pick, then seven rands each: the motor's file and start, the guns' starts.)
        std::srand(51);
        std::vector<int> paths = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        struct S_Expect {
            int path, motor, motorAt, gunAt[5];
            float pitch[6];
        };
        std::vector<S_Expect> expect;
        for (int k = 0; k < 3; ++k) {
            S_Expect x = {};
            const int pick = std::rand() % static_cast<int>(paths.size());
            x.path = paths[pick];
            paths[pick] = paths.back();
            paths.pop_back();
            x.motor = std::rand() % 4 + 1;
            x.motorAt = std::rand() % 5000;
            for (int g = 0; g < 5; ++g)
                x.gunAt[g] = std::rand() % 5000;
            for (float &pitch : x.pitch)
                pitch = 1.25f - nextRandom(random, 0.5f);
            expect.push_back(x);
        }
        bool fightersOk = j->m_fighters.size() == 3 && g_juLoads["la_Bf109Canyon"] == 3;
        for (int k = 0; k < 3 && fightersOk; ++k) {
            const Fighter *fighter = j->m_fighters[k];
            const S_Expect &x = expect[k];
            const S_FakeSound &motor = g_sounds[reinterpret_cast<const I3D_frame *>(fighter->motor)];
            fightersOk = fighter->pathIndex == x.path && fighter->health == 0.85f * 10.0f && fighter->model &&
                         ItName(fighter->model) == "interceptor_" + std::to_string(x.path) && fighter->model->m_parent == ju &&
                         motor.file == "motor" + std::to_string(x.motor) + ".wav" && motor.loop && motor.range[0] == 20.0f &&
                         motor.range[1] == 400.0f && motor.range[2] == 0.3f && motor.range[3] == 0.3f &&
                         motor.playAt == x.motorAt && motor.frequency == x.pitch[0] && motor.volume == 1.0f &&
                         !reinterpret_cast<const I3D_frame *>(fighter->motor)->IsOn() &&
                         reinterpret_cast<const I3D_frame *>(fighter->motor)->m_parent == fighter->model && !fighter->gun;
            for (int g = 0; g < 5 && fightersOk; ++g) {
                const S_FakeSound &gun = g_sounds[reinterpret_cast<const I3D_frame *>(fighter->guns[g])];
                fightersOk = gun.file == "kul_d" + std::to_string(g + 1) + ".wav" && !gun.loop && gun.range[0] == 10.0f &&
                             gun.range[1] == 300.0f && gun.range[2] == 0.3f && gun.playAt == x.gunAt[g] &&
                             gun.frequency == x.pitch[g + 1];
            }
            // (The path: its dummy model, the first animation 1 ms in, the notes; the looks.)
            const C_Junkers88::S_Path *path = fighter->path;
            fightersOk = fightersOk && path && path->owner == fighter->model && path->follow && path->index == 1 &&
                         std::string(path->name.c_str()) == "#airfight_0" + std::to_string(x.path) + "_A" &&
                         JuAnimName(F_JuGetAnimation(path->model, 0)) == "#airfight_0" + std::to_string(x.path) + "_A1" &&
                         F_JuGetAnimTime(path->model, 0) == 1 && g_juCallbacks[path->model->GetChild(0)].first == &C_Junkers88::PathNote &&
                         JuAnimName(F_JuGetAnimation(fighter->model, 1)) == "#adamage0" &&
                         JuAnimName(F_JuGetAnimation(fighter->model, 2)) == "#bfcanygrup" &&
                         g_seAnims["#bfcanygrup"]->playMode == 2;
            // (The sphere: in the world, the bomber's; the guns' frames, the propeller's among them; the
            // carriage hidden.)
            fightersOk = fightersOk && fighter->inWorld && fighter->volume.flags == COLLISION_SHAPE_SPHERE &&
                         fighter->volume.type == COLLISION_TYPE_LIGHT_OBJECT && fighter->volume.actor == j &&
                         fighter->volume.frame == fighter->model && fighter->volume.invMatrix == &fighter->invMatrix &&
                         fighter->gunFrames == std::vector<I3D_frame *>{fighterFrame(fighter, "vrtule"),
                                                                        fighterFrame(fighter, "weapon_l00"),
                                                                        fighterFrame(fighter, "weapon_r00")} &&
                         !fighterFrame(fighter, "carriage_l00")->IsOn() && !fighterFrame(fighter, "carriage_r00")->IsOn() &&
                         fighter->fireTime == 0 && fighter->hitTime[0] == 0 && fighter->hitTime[1] == 0 &&
                         !fighter->attacking && !fighter->down && fighter->smoke[0] == -1 && fighter->smoke[1] == -1 &&
                         fighter->fall.frame == fighterFrame(fighter, "BODY") && fighter->fall.radius == 2.0f;
        }
        JU(r.junkFighters, fightersOk);
        int added = 0;
        for (const S_VolumeOp &op : g_volumeLog)
            added += op.op == 'A';
        JU(r.junkFighters, added == 3 && JuLogged("on carriage_r00 0") == 3 && JuLogged("on carriage_l00 0") == 3 &&
                               JuLogged("update carriage_l00") == 3);
        int fighterLinks = 0;
        for (const std::string &line : g_itLog)
            fighterLinks += line.rfind("link interceptor_", 0) == 0 && line.size() > 7 &&
                                    line.compare(line.size() - 7, 7, " ju88 0") == 0
                                ? 1
                                : 0;
        JU(r.junkFighters, fighterLinks == 3);
        g_cacheFailName = "la_Bf109Canyon";
        reset();
        Fighter *failed = new Fighter(4, "la_Bf109Canyon", 10.0f);
        g_cacheFailName.clear();
        JU(r.junkFighters, !failed->model && !failed->path && !failed->motor && JuLogged("release interceptor_4") == 1);
        delete failed;
        // (Once: more do nothing.)
        j->SetState(2);
        JU(r.junkFighters, j->m_fighters.size() == 3);
        // (The difficulty: 0.7 for 0, 1 for others; at most nine, Fix.)
        restart();
        playMode.unknown0C = 0;
        j->SetState(12);
        JU(r.junkFighters, j->m_fighters.size() == 9 && j->m_fighters[0]->health == 0.7f * 10.0f);
        std::vector<int> seen;
        for (const Fighter *fighter : j->m_fighters)
            seen.push_back(fighter->pathIndex);
        std::sort(seen.begin(), seen.end());
        JU(r.junkFighters, seen == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9});
        restart();
        playMode.unknown0C = 2;
        j->SetState(1);
        JU(r.junkFighters, j->m_fighters.size() == 1 && j->m_fighters[0]->health == 10.0f);
        // (A model that does not load: a fighter without one, nothing else.)
        restart();
        g_cacheFailName = "la_Bf109Canyon";
        reset();
        j->SetState(1);
        g_cacheFailName.clear();
        JU(r.junkFighters, j->m_fighters.size() == 1 && !j->m_fighters[0]->model && !j->m_fighters[0]->path &&
                               !j->m_fighters[0]->motor && g_volumeLog.empty());
        j->Update(20);
        JU(r.junkFighters, j->m_fighters.size() == 1);
        playMode.unknown0C = 0;
    }

    // --- in flight: the path's notes start / end an attack (the bomber's damage rate by the fighter's,
    // the chatter 0); a fighter's update: its path, the model, the velocity to its sounds, the motor on,
    // attacking: a gun sound, tracers each 200 ms from a gun, hit sounds on the bomber; its sphere moved;
    // hit: shot down at 0 (the chatter 3, "#bfshotdwn<n>", smoke at the wings, out of the world, falling);
    // gone at the end of that animation or when it lands (the crash) ---------------------------------------
    {
        restart();
        playMode.unknown0C = 2;
        j->SetState(2);
        Fighter *fighter = j->m_fighters[0];
        I3D_frame *pathDummy = fighter->path->model->GetChild(0);
        g_itNames[fighter->path->model] = "fighterpath";
        // The notes: a fighter's own path's (message 0x1000, a note).
        // (A sound file of chatter group `g`.)
        const auto inGroup = [&](int g, const std::string &file) {
            for (const int id : j->m_chatter[g].sounds)
                if (file == std::to_string(id) + ".wav")
                    return true;
            return false;
        };
        j->m_chatter[0].delay = 0;
        const int attackers = g_attackingFighters;
        reset();
        const int started = C_Junkers88::PathNote(pathDummy, 0x1000, reinterpret_cast<uintptr_t>("x fire_start"), 0);
        JU(r.junkFly, fighter->attacking && j->m_damageRate == 30.0f && g_soundRequests.size() == 1 && j->m_chatter[0].delay != 0 &&
                          g_attackingFighters == attackers + 1 && inGroup(0, g_soundFiles[0]) && started == 0);
        const int ignored[3] = {C_Junkers88::PathNote(pathDummy, 0x800, reinterpret_cast<uintptr_t>("fire_end"), 0),
                                C_Junkers88::PathNote(pathDummy, 0x1000, 0, 0),
                                C_Junkers88::PathNote(pathDummy, 0x1000, reinterpret_cast<uintptr_t>("other"), 0)};
        JU(r.junkFly, fighter->attacking && j->m_damageRate == 30.0f && ignored[0] == 0 && ignored[1] == 0 && ignored[2] == 0);
        const int ended = C_Junkers88::PathNote(pathDummy, 0x1000, reinterpret_cast<uintptr_t>("fire_end;"), 0);
        JU(r.junkFly, !fighter->attacking && j->m_damageRate == 0.0f && g_attackingFighters == attackers && ended == 0 &&
                          C_Junkers88::NoPathNote(pathDummy, 0x1000, reinterpret_cast<uintptr_t>("fire_start"), 0) == 0);
        // (Two attacks add up; a rate left at 0.25 kept.)
        j->StartAttack(camera);
        j->StartAttack(camera);
        const float twice = j->m_damageRate;
        j->m_damageRate = 30.25f;
        j->EndAttack(camera);
        JU(r.junkFly, twice == 60.0f && j->m_damageRate == 0.25f);
        j->m_damageRate = 0.0f;
        // (The bomber at 0.5: the chatter all the same; at 0: none.)
        const float bomberHealth = j->m_health;
        for (const float h : {0.5f, 0.0f}) {
            j->m_health = h;
            j->m_chatter[0].delay = 0;
            reset();
            fighter->StartAttack();
            fighter->StopAttack();
            JU(r.junkFly, g_soundFiles.size() == (h != 0.0f ? 1u : 0u) && !fighter->attacking && g_attackingFighters == attackers);
        }
        j->m_health = bomberHealth;
        // (Below 0: 0; another frame's notes change the rate only, Fix: no fighter called.)
        C_Junkers88::PathNote(pathDummy, 0x1000, reinterpret_cast<uintptr_t>("fire_end"), 0);
        C_Junkers88::PathNote(camera, 0x1000, reinterpret_cast<uintptr_t>("fire_start"), 0);
        JU(r.junkFly, j->m_damageRate == 30.0f && !fighter->attacking);
        j->m_damageRate = 0.0f;
        C_Junkers88 *const kept = g_junkers88;
        g_junkers88 = nullptr;
        C_Junkers88::PathNote(pathDummy, 0x1000, reinterpret_cast<uintptr_t>("fire_start"), 0);
        g_junkers88 = kept;
        JU(r.junkFly, !fighter->attacking);
        {
            C_Junkers88::S_Path *path = fighter->path;
            const std::string kept = path->name.c_str();
            const int keptIndex = path->index;
            S_SaBytes e;
            e.Head(0x777, 6 + 2 + 7 + 4 + 1).Val(uint16_t(7));
            e.Put("#other", 7);
            e.Val(1).Val(true);
            S_SaReader rd(e);
            reset();
            path->Load(rd.c);
            const std::string dummyName = ItName(pathDummy);
            JU(r.junkFly, std::string(path->name.c_str()) == "#other" && path->index == 1 &&
                              JuLogged("getanim #other1") == 1 && F_JuGetAnimTime(path->model, 0) == 0 &&
                              SaLogIs({"callback " + dummyName + " none 1000", "getanim #other1",
                                       "anim fighterpath 0 #other1 1 0", "animrelease #other1", "animtime fighterpath 0 0",
                                       "tick fighterpath 0", "update fighterpath", "callback " + dummyName + " note 1000"}));
            path->name = kept.c_str();
            path->index = keptIndex - 1;
            path->Next(0);
            g_cacheFailName = "#airfightdum";
            reset();
            C_Junkers88::S_Path missing(fighter->model, "#airfightdum", "#x");
            g_cacheFailName.clear();
            JU(r.junkFly, !missing.model && JuLogged("release new") == 1 && g_itLog.size() == 2 && missing.index == 0);
            C_Junkers88::S_Path made(fighter->model, "#airfightdum", "#x");
            JU(r.junkFly, made.model && made.model->GetRot().s == 1.0f && made.index == 1);
        }
        // A step not attacking: the path places the model (its dummy's place under the bomber), the
        // velocity (by 1 / (ms * 0.001)), the motor on, the sphere's centre from the bound.
        pathDummy->m_localMatrix(3) = S_vector(px + 30.0f, 320.0f, pz + 40.0f);
        pathDummy->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        fighter->model->GetBound();
        fighter->model->m_bound.center = S_vector(1.5f, 2.5f, -3.5f);
        const S_vector before = fighter->model->GetWorldPos();
        reset();
        JU(r.junkFly, fighter->Update(20));
        const S_vector &after = fighter->model->GetWorldPos();
        const float k = 1.0f / (static_cast<float>(20) * 0.001f);
        const S_vector v((after.x - before.x) * k, (after.y - before.y) * k, (after.z - before.z) * k);
        bool moved = false;
        for (const S_VolumeOp &op : g_volumeLog)
            moved = moved || (op.op == 'M' && op.volume == &fighter->volume);
        S_matrix inv;
        inv.Inverse(fighter->model->GetMatrix());
        JU(r.junkFly, RbSame(after, S_vector(px + 30.0f, 320.0f, pz + 40.0f)) &&
                          RbSame(g_aaVelocity[reinterpret_cast<const I3D_frame *>(fighter->motor)], v) &&
                          AaSoundCalls(fighter->motor) == "O" && moved && SaSame(&fighter->invMatrix, &inv, sizeof inv) &&
                          fighter->volume.centerW == fighter->model->GetBound().radius &&
                          fighter->volume.center.x == 1.5f && fighter->volume.center.z == -3.5f &&
                          JuLogged("tick interceptor_" + std::to_string(fighter->pathIndex) + " 20") == 1 &&
                          JuLogged("tick fighterpath 20") == 1);
        // Attacking: a gun sound (on, from the start), the tracers (one each 200 ms: the first at once),
        // the two hit sounds on the bomber (their own pauses).
        for (I3D_frame *barrelFrame : fighter->gunFrames) {
            const S_vector z = barrelFrame->m_localMatrix(2);
            barrelFrame->m_localMatrix(2) = S_vector(z.x * 2.0f, z.y * 2.0f, z.z * 2.0f);
            barrelFrame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        }
        fighter->attacking = true;
        std::srand(61);
        uint32_t random = g_waRandom;
        reset();
        fighter->Update(20);
        std::srand(61);
        I3D_sound *gun = fighter->guns[std::rand() % 5];
        I3D_frame *barrel = fighter->gunFrames[static_cast<uint32_t>(std::rand()) % 3];
        const int pause0 = std::rand() % 100 + 200;
        const int pause1 = std::rand() % 125 + 250;
        const float h0 = nextRandom(random, 1.0f) - 0.5f, h1 = nextRandom(random, 1.0f) - 0.5f,
                    h2 = nextRandom(random, 1.0f) - 0.5f;
        const float i0 = nextRandom(random, 1.0f) - 0.5f;
        S_vector barrelDir = barrel->GetMatrix()(2);
        PrNormalize(barrelDir, (barrelDir.y * barrelDir.y + barrelDir.z * barrelDir.z) + barrelDir.x * barrelDir.x);
        JU(r.junkFly, fighter->gun == gun && AaSoundCalls(gun) == "OP" && g_sounds[gun].playAt == 0 &&
                          fighter->fireTime == 180 && j->m_tracers.size() == 1 &&
                          RbSame(j->m_tracers[0]->frame->GetPos(), barrel->GetWorldPos()) && fighter->hitTime[0] == pause0 - 20 &&
                          fighter->hitTime[1] == pause1 - 20 && g_soundRequests.size() == 2 &&
                          g_soundRequests[0].chapter == 4 && g_soundRequests[0].group == 0x12 &&
                          g_soundRequests[0].attachTo == ju && g_soundRequests[0].channel == 1 &&
                          g_soundRequests[0].pos.x == h0 && g_soundRequests[0].pos.y == h1 && g_soundRequests[0].pos.z == h2 &&
                          g_soundRequests[1].group == 0x0B && g_soundRequests[1].channel == 0 && g_soundRequests[1].pos.x == i0 &&
                          g_soundRequests[1].chapter == 4 &&
                          RbSame(g_juStaticRays.back().dir, barrelDir) &&
                          RbSame(g_aaVelocity[reinterpret_cast<const I3D_frame *>(gun)], S_vector(0, 0, 0)));
        // (The gun still playing: kept; ended: another; 400 ms: two tracers.)
        void *const oldPlaying = g_soundVtable[0x78 / 4];
        g_soundVtable[0x78 / 4] = reinterpret_cast<void *>(&F_AaSoundPlaying);
        reset();
        fighter->Update(20);
        g_soundVtable[0x78 / 4] = oldPlaying;
        JU(r.junkFly, fighter->gun == gun && AaSoundCalls(gun).empty() && fighter->fireTime == 160 && j->m_tracers.size() == 1 &&
                          JuLogged("playing " + ItName(gun) + " 0") == 1);
        // (Exactly due: none yet.)
        fighter->fireTime = 20;
        const int hitTimes[2] = {fighter->hitTime[0], fighter->hitTime[1]};
        fighter->hitTime[0] = fighter->hitTime[1] = 20;
        reset();
        fighter->Update(20);
        JU(r.junkFly, fighter->fireTime == 0 && j->m_tracers.size() == 1 && g_soundRequests.empty() &&
                          fighter->hitTime[0] == 0 && fighter->hitTime[1] == 0);
        fighter->hitTime[0] = hitTimes[0];
        fighter->hitTime[1] = hitTimes[1];
        fighter->fireTime = 160;
        g_sounds[gun].ended = true;
        fighter->Update(400);
        g_sounds[gun].ended = false;
        JU(r.junkFly, fighter->fireTime == 160 && j->m_tracers.size() == 3);
        // The fade: the fighters' alpha by the game's time (0 until 9 s, full from 11 s).
        for (const uint32_t time : {9000u, 9001u, 10000u, 10999u, 11001u, 30000u}) {
            GetGame()->m_time = time;
            reset();
            j->Update(10);
            const float alpha = time > 11000 ? 1.0f : time > 9000 ? 1.0f - static_cast<float>(11000 - static_cast<int>(time)) * 0.0005f : 0.0f;
            char line[64];
            std::snprintf(line, sizeof line, "alpha %s %.9g", ItName(fighter->model).c_str(), alpha);
            JU(r.junkFly, JuLogged(line) == 1);
        }
        GetGame()->m_time = 20000;
        // Hit to 0: shot down (once).
        j->m_chatter[3].delay = 0;
        fighter->attacking = true;
        j->m_damageRate = 30.0f;
        fighter->Damage(9.75f);
        JU(r.junkFly, fighter->health == 0.25f && !fighter->down);
        std::srand(71);
        reset();
        fighter->Damage(0.25f);
        std::srand(71);
        std::rand();
        const int shotdown = std::rand() % 3 + 1;
        const std::string anim = "#bfshotdwn" + std::to_string(shotdown);
        bool removed = false;
        for (const S_VolumeOp &op : g_volumeLog)
            removed = removed || (op.op == 'R' && op.volume == &fighter->volume);
        JU(r.junkFly, fighter->down && fighter->health == 0.0f && !fighter->inWorld && removed && !fighter->attacking &&
                          j->m_damageRate == 0.0f && g_soundRequests.size() == 1 &&
                          JuLogged("callback " + ItName(pathDummy) + " none 1000") == 1 &&
                          JuAnimName(F_JuGetAnimation(fighter->model, 1)) == anim && g_seAnims[anim]->playMode == 1 &&
                          fighter->fall.falling && fighter->fall.pos.x == fighterFrame(fighter, "BODY")->GetWorldPos().x &&
                          g_rbSplashes.size() == 2 && g_rbSplashes[0].effect == 0x10 &&
                          g_rbSplashes[0].parent == fighterFrame(fighter, "left_wing") &&
                          g_rbSplashes[1].parent == fighterFrame(fighter, "right_wing") &&
                          RbSame(g_rbSplashes[1].pos, fighterFrame(fighter, "right_wing")->GetWorldPos()) &&
                          RbSame(g_rbSplashes[0].dir, S_vector(0, 0, -1)) && fighter->smoke[0] != -1 &&
                          g_rbSplashes[0].faceCamera && g_rbSplashes[1].faceCamera && !g_rbSplashes[0].transient &&
                          inGroup(3, g_soundFiles[0]) &&
                          fighter->smoke[1] == fighter->smoke[0] + 1);
        reset();
        fighter->Damage(1.0f);
        fighter->ShootDown();
        JU(r.junkFly, g_itLog.empty() && fighter->health == 0.0f);
        // Down: falling on until its animation ends (the smoke finished): gone; the bomber drops it
        // (a sound at the camera).
        g_juAnimEnds[anim] = 50;
        reset();
        JU(r.junkFly, fighter->Update(30) && g_juSpheres.size() == 1 && g_juSpheres[0].radius == 2.0f);
        int releasing = 1 + (fighter->motor ? 1 : 0) + (fighter->path && fighter->path->model ? 1 : 0);
        for (I3D_sound *sound : fighter->guns)
            releasing += sound ? 1 : 0;
        const int smoke0 = fighter->smoke[0];
        fighter->smoke[1] = -1;
        g_juAnimEnds[anim] = F_JuGetAnimTime(fighter->model, 1) + 30;
        Fighter *other = j->m_fighters[1];
        reset();
        j->Update(30);
        g_juAnimEnds.erase(anim);
        std::vector<std::string> finishes;
        int released = 0;
        for (const std::string &line : g_itLog) {
            if (line.rfind("finish ", 0) == 0)
                finishes.push_back(line);
            released += line.rfind("release ", 0) == 0 ? 1 : 0;
        }
        JU(r.junkFly, j->m_fighters.size() == 1 && j->m_fighters[0] == other &&
                          finishes == std::vector<std::string>{"finish " + std::to_string(smoke0)} && released == releasing &&
                          releasing == 8 &&
                          JuLogged("anim " + ItName(fighter->model) + " 1 0 1 0") == 1 && g_soundRequests.size() == 1 &&
                          g_soundRequests[0].chapter == 6 && g_soundRequests[0].attachTo == camera);
        // (Landing instead: the crash where it met the ground.)
        j->m_chatter[3].delay = 0;
        j->m_health = 0.5f;
        reset();
        other->Damage(100.0f);
        j->m_health = bomberHealth;
        JU(r.junkFly, other->down && g_soundFiles.size() == 1 && inGroup(3, g_soundFiles[0]));
        S_CollisionVolume ground;
        g_juSphereMet = &ground;
        g_sphereHookHit = S_vector(px, 2.0f, pz);
        reset();
        JU(r.junkFly, !other->Update(20) && g_rbSplashes.size() == 2 && g_rbSplashes[0].effect == 0x67 &&
                          g_rbSplashes[1].effect == 0x68 && RbSame(g_rbSplashes[0].pos, S_vector(px, 2.0f, pz)) &&
                          !g_rbSplashes[0].parent && RbSame(g_rbSplashes[0].dir, S_vector(0, 1, 0)) &&
                          RbSame(g_rbSplashes[1].dir, S_vector(0, 1, 0)) && g_rbSplashes[0].faceCamera &&
                          g_rbSplashes[1].faceCamera && !g_rbSplashes[0].transient && !g_rbSplashes[1].transient);
        g_juSphereMet = nullptr;
    }

    // --- the hits: on a fighter's model its hit points (by 1 / the setup's health factor; 0: none) and the
    // chatter 2; the bomber's own frames nothing; only two hits from others than a player --------------
    {
        restart();
        playMode.unknown0C = 2;
        j->SetState(2);
        Fighter *f0 = j->m_fighters[0];
        I3D_frame *wing = fighterFrame(f0, "left_wing");
        const auto hitBy = [&](C_Human *who, I3D_frame *part, float amount) {
            j->Hit(HIT_BULLET, amount, S_vector(0, 0, 0), S_vector(0, 0, 1), who,
                   static_cast<int>(reinterpret_cast<intptr_t>(part)));
        };
        C_Human *player = new C_Human();
        player->controllerType = HUMAN_CONTROLLER_PLAYER;
        C_Human *enemy = new C_Human();
        enemy->controllerType = HUMAN_CONTROLLER_ENEMY;
        f["tail00"]->m_ownerModel = ju;
        j->m_chatter[2].delay = 0;
        reset();
        hitBy(enemy, f["tail00"], 4.0f);
        JU(r.junkHit, j->m_hitsByOthers == 0 && f0->health == 10.0f && g_soundRequests.empty());
        hitBy(player, wing, 4.0f);
        JU(r.junkHit, f0->health == 6.0f && j->m_hitsByOthers == 0 && g_soundRequests.size() == 1);
        j->m_interceptorHealth = 2.0f;
        hitBy(player, wing, 4.0f);
        JU(r.junkHit, f0->health == 4.0f);
        j->m_interceptorHealth = 0.0f;
        hitBy(player, wing, 4.0f);
        JU(r.junkHit, f0->health == 4.0f);
        j->m_interceptorHealth = 1.0f;
        // (Others: two, then none; a frame of no fighter after the count.)
        hitBy(enemy, camera, 1.0f);
        hitBy(enemy, wing, 1.0f);
        JU(r.junkHit, j->m_hitsByOthers == 2 && f0->health == 3.0f);
        hitBy(enemy, wing, 1.0f);
        hitBy(nullptr, wing, 1.0f);
        JU(r.junkHit, j->m_hitsByOthers == 2 && f0->health == 3.0f);
        hitBy(player, nullptr, 1.0f);
        JU(r.junkHit, f0->health == 3.0f);
        // (Down by a hit.)
        hitBy(player, wing, 5.0f);
        JU(r.junkHit, f0->down && f0->health == 0.0f);
        delete player;
        delete enemy;
    }

    // --- the squads' targets: none in the game's first 10 s or with the bomber down; else the nearest
    // fighter in front of the gunner whose line meets one of the bomber's volumes first -------------------
    {
        restart();
        playMode.unknown0C = 2;
        j->SetState(3);
        C_Human *gunner = new C_Human();
        I3D_frame *eye = JuChild(FRAME_DUMMY, "eye", nullptr);
        LinkUnder(eye, camera);
        camera->m_localMatrix.Identity();
        camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        eye->m_localMatrix(3) = S_vector(px, 300.0f, pz - 100.0f);
        gunner->bodyFrame = eye;
        gunner->lookDir = S_vector(0.0f, 0.0f, 2.0f);
        const auto place = [](Fighter *fighter, const S_vector &at) {
            fighter->model->m_localMatrix(3) = at;
            fighter->model->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            Unlink(fighter->model);
            LinkUnder(fighter->model, g_sector);
        };
        place(j->m_fighters[0], S_vector(px, 300.0f, pz - 50.0f));
        place(j->m_fighters[1], S_vector(px + 1.0f, 300.0f, pz - 70.0f));
        place(j->m_fighters[2], S_vector(px, 300.0f, pz - 160.0f));   // (behind)
        static S_CollisionVolume bomberBox;
        bomberBox = S_CollisionVolume();
        bomberBox.actor = j;
        g_aaPlanes = {{S_vector(0, 0, 1), pz - 80.0f, &bomberBox, S_vector(0, 0, -1)},
                      {S_vector(0, 0, 1), pz - 120.0f, &bomberBox, S_vector(0, 0, 1)}};
        I3D_frame *aimed = nullptr;
        GetGame()->m_time = 9999;
        JU(r.junkTarget, !j->FindTarget(gunner, &aimed));
        GetGame()->m_time = 10000;
        reset();
        JU(r.junkTarget, j->FindTarget(gunner, &aimed) == j && aimed == j->m_fighters[1]->model &&
                             g_aaRays.size() == 2 && g_aaRays[0].types == 0 && g_aaRays[0].flags == 0 &&
                             RbSame(g_aaRays[0].from, S_vector(px, 300.0f, pz - 100.0f)) &&
                             RbSame(g_aaRays[0].dir, j->m_fighters[1]->model->GetWorldPos() - eye->GetWorldPos()));
        // (Another actor's volume first: not seen; the nearer down: the other; the bomber down: none.)
        static S_CollisionVolume otherBox;
        otherBox = S_CollisionVolume();
        g_aaPlanes[0].volume = &otherBox;
        aimed = nullptr;
        JU(r.junkTarget, !j->FindTarget(gunner, &aimed) && !aimed);
        g_aaPlanes[0].volume = &bomberBox;
        j->m_fighters[1]->down = true;
        JU(r.junkTarget, FindJunkersTargetFor(gunner, &aimed) == j && aimed == j->m_fighters[0]->model);
        j->m_fighters[1]->down = false;
        gunner->lookDir = S_vector(0.0f, 0.0f, -1.0f);
        reset();
        JU(r.junkTarget, j->FindTarget(gunner, &aimed) == j && aimed == j->m_fighters[2]->model && g_aaRays.size() == 1);
        gunner->lookDir = S_vector(0.0f, 0.0f, 1.0f);
        j->m_health = 0.0f;
        JU(r.junkTarget, !j->FindTarget(gunner, &aimed));
        j->m_health = 1000.0f;
        GetGame()->m_time = 20000;
        // The chatter 5: a fighter between 20 and 60 m picks the sound by its bearing (from behind round,
        // twelve sectors) to the bomber's z axis as seen from above (any length, tilted, tiny).
        const S_matrix juWas = ju->m_localMatrix;
        ju->m_localMatrix(3) = S_vector(px + 3.0f, 290.0f, pz - 4.0f);
        ju->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        const S_vector at = ju->GetWorldPos();
        bool bearingOk = true;
        const float angles[5] = {0.3f, 2.0f, -2.5f, 3.1f, -0.01f};
        const S_vector axes[6] = {S_vector(0.0f, 0.0f, 1.0f), S_vector(0.6f, 0.9f, 1.4f), S_vector(0.1f, 0.3f, 0.9f),
                                  S_vector(1e-9f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, 1e-9f), S_vector(1.0f, 3.0f, 1.0f)};
        for (int n = 0; n < 30; ++n) {
            const S_vector &axis = axes[n / 5];
            const float a = angles[n % 5];
            ju->m_localMatrix(2) = axis;
            ju->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            place(j->m_fighters[0], S_vector(at.x + 30.0f * std::sin(a), at.y + 5.0f, at.z + 30.0f * std::cos(a)));
            place(j->m_fighters[1], S_vector(at.x + 100.0f, at.y, at.z));
            place(j->m_fighters[2], S_vector(at.x, at.y + 20.0f, at.z));   // (exactly 20 m: not)
            j->m_chatter[5].delay = 0;
            reset();
            j->Chatter(5);
            const S_vector &p0 = j->m_fighters[0]->model->GetWorldPos();
            float to[2] = {p0.x - at.x, p0.z - at.z};
            const float len = std::sqrt(to[1] * to[1] + to[0] * to[0]);
            to[0] = to[0] * (1.0f / len);
            to[1] = (1.0f / len) * to[1];
            const float heading[2] = {axis.x, axis.z};
            int sector = static_cast<int>(((3.14159274f - AngleBetween2D(heading, to)) * 0.159154937f) * 12.0f);
            sector = sector > 11 ? 11 : sector < 0 ? 0 : sector;
            bearingOk = bearingOk && g_soundRequests.size() == 1 &&
                        g_soundFiles[0] == std::to_string(0x55D5FFC + sector) + ".wav" && j->m_chatter[5].delay >= 3000;
        }
        JU(r.junkTarget, bearingOk);
        ju->m_localMatrix(2) = S_vector(0.0f, 0.0f, 1.0f);
        ju->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        // (Near: 15 m up and 15 m along, 22 m along.)
        place(j->m_fighters[0], S_vector(at.x, at.y + 15.0f, at.z + 15.0f));
        j->m_chatter[5].delay = 0;
        reset();
        j->Chatter(5);
        const bool upAlong = g_soundRequests.size() == 1;
        place(j->m_fighters[0], S_vector(at.x + 22.0f, at.y, at.z));
        j->m_chatter[5].delay = 0;
        reset();
        j->Chatter(5);
        JU(r.junkTarget, upAlong && g_soundRequests.size() == 1);
        // (The tick: the chatter 5 while the bomber is alive.)
        place(j->m_fighters[0], S_vector(at.x + 30.0f * std::sin(0.3f), at.y, at.z + 30.0f * std::cos(0.3f)));
        j->m_chatter[5].delay = 0;
        reset();
        j->Tick(10);
        const bool ticked = g_soundRequests.size() == 1 && j->m_chatter[5].delay >= 3000;
        j->m_chatter[5].delay = 0;
        j->m_health = 0.0f;
        reset();
        j->Tick(10);
        j->m_health = 1000.0f;
        JU(r.junkTarget, ticked && g_soundRequests.empty());
        // (60 m: not; none near: the pause cleared.)
        place(j->m_fighters[0], S_vector(at.x, at.y, at.z + 60.0f));
        j->m_chatter[5].delay = 0;
        reset();
        j->Chatter(5);
        JU(r.junkTarget, g_soundRequests.empty() && j->m_chatter[5].delay == 0);
        ju->m_localMatrix = juWas;
        ju->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        // (The gunner elsewhere: in front by the line to each fighter against the view (any length,
        // across, at right angles: not); the nearest by the squares (a tie: the first).)
        {
            const S_vector eyeAt(200.0f, 300.0f, 0.0f);
            eye->m_localMatrix(3) = eyeAt;
            eye->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            bomberBox.actor = j;
            g_aaPlanes = {{S_vector(1, 0, 0), 200.2f, &bomberBox, S_vector(-1, 0, 0)},
                          {S_vector(1, 0, 0), 199.8f, &bomberBox, S_vector(1, 0, 0)},
                          {S_vector(0, 0, 1), 0.2f, &bomberBox, S_vector(0, 0, -1)},
                          {S_vector(0, 0, 1), -0.2f, &bomberBox, S_vector(0, 0, 1)}};
            bool wasDown[3];
            for (int k = 0; k < 3; ++k)
                wasDown[k] = j->m_fighters[k]->down;
            const auto aim = [&](const S_vector &view, std::vector<S_vector> offsets) {
                gunner->lookDir = view;
                for (Fighter *fighter : j->m_fighters)
                    fighter->down = true;
                for (size_t k = 0; k < offsets.size(); ++k) {
                    place(j->m_fighters[k], S_vector(eyeAt.x + offsets[k].x, eyeAt.y + offsets[k].y, eyeAt.z + offsets[k].z));
                    j->m_fighters[k]->down = false;
                }
                I3D_frame *got = nullptr;
                const C_Actor *found = j->FindTarget(gunner, &got);
                for (Fighter *fighter : j->m_fighters)
                    fighter->down = true;
                if (!found)
                    return -1;
                for (int k = 0; k < 3; ++k)
                    if (j->m_fighters[k]->model == got)
                        return k;
                return -2;
            };
            const int seen[12] = {
                aim(S_vector(1, 0, 1), {S_vector(-3, 0, 1), S_vector(2, 0, 4)}),
                aim(S_vector(1, 0, 1), {S_vector(0.5f, 5, -0.6f), S_vector(4, 0, 5)}),
                aim(S_vector(0.2f, 0, 2), {S_vector(-1, 0, 0.3f)}),
                aim(S_vector(0, 0, 1), {S_vector(3, 0, 0)}),
                aim(S_vector(0, 0, 1), {S_vector(3, 0, 0.5f)}),
                aim(S_vector(0, 1, 1), {S_vector(0, 1, -0.5f)}),
                aim(S_vector(0, 0, 1), {S_vector(1, 0, 5), S_vector(-1, 0, 6)}),
                aim(S_vector(0, 0, 1), {S_vector(0, 3, 4), S_vector(0, 0, 4.5f)}),
                aim(S_vector(0, 0, 1), {S_vector(3, 0, 4), S_vector(-3, 0, 4)}),
                aim(S_vector(1e-9f, 0, 0), {S_vector(3, 0, 0.5f)}),
                aim(S_vector(0, 0, 1e-9f), {S_vector(-3, 0, 0.5f)}),
                aim(S_vector(0, 1e-9f, 0), {S_vector(0, 1, -0.5f)}),
            };
            for (int k = 0; k < 3; ++k)
                j->m_fighters[k]->down = wasDown[k];
            JU(r.junkTarget, seen[0] == 1 && seen[1] == 1 && seen[2] == 0 && seen[3] == -1 && seen[4] == 0 &&
                                 seen[5] == 0 && seen[6] == 0 && seen[7] == 1 && seen[8] == 0 && seen[9] == 0 &&
                                 seen[10] == 0 && seen[11] == 0);
        }
        delete gunner;
        g_aaPlanes.clear();
    }

    // --- the saved game: the base's chunk, then 1000 holding the state (the frame shown, the velocity, the
    // camera path, the fighters, the debris, the factors, the health twice, the rate, the engines, the
    // fall, the tracers, the parts' groups and stages, the hits); read back into a new start the same
    // again ----------------------------------------------------------------------------------------------------
    {
        restart();
        j->m_velocity = {1.0f, 2.0f, 3.0f, 4.0f};
        j->m_health = 640.0f;
        j->m_damageRate = 60.0f;
        j->m_firepower = 1.5f;
        j->m_interceptorHealth = 2.5f;
        j->m_engineDown[1] = true;
        j->m_hitsByOthers = 1;
        j->BreakPart(4, j->m_damage.parts, j->m_damage.broken, j->m_outer.parts, j->m_engines.parts, 1);
        const C_Junkers88::S_Path *cam = j->m_camera;
        const auto groupBytes = [](S_SaBytes &b, uint16_t id, const C_Junkers88::S_PartGroup &g) {
            b.Head(id, static_cast<uint32_t>(6 + 4 + 4 + 4 * g.parts.size() + 4 + 4 * g.broken.size())).Val(g.left);
            b.Val(static_cast<int>(g.parts.size()));
            for (const C_Junkers88::S_Part *part : g.parts)
                b.Val(part->index);
            b.Val(static_cast<int>(g.broken.size()));
            for (const C_Junkers88::S_Part *part : g.broken)
                b.Val(part->index);
        };
        S_SaBytes inner;
        inner.Head(2000, 6);
        inner.Head(3000, 18).Val(1.0f).Val(2.0f).Val(3.0f);
        inner.Head(4000, 6 + 2 + 11 + 4 + 1 + 4).Val(uint16_t(11));
        inner.Put("#aircraftA", 11);
        inner.Val(cam->index).Val(cam->follow).Val(F_JuGetAnimTime(cam->model, 0));
        inner.Head(5000, 10).Val(0).Head(6000, 10).Val(0);
        inner.Head(0x4E84, 10).Val(1.5f).Head(0x4EE8, 10).Val(2.5f).Head(7000, 10).Val(640.0f).Head(7000, 10).Val(640.0f);
        inner.Head(8000, 10).Val(60.0f).Head(9000, 7).Val(false).Head(10000, 7).Val(true);
        inner.Head(12000, 6 + 13);
        inner.Put(&j->m_fall.pos, 12);
        inner.Val(j->m_fall.falling);
        inner.Head(13000, 10).Val(0);
        groupBytes(inner, 14000, j->m_damage);
        groupBytes(inner, 15000, j->m_engines);
        groupBytes(inner, 16000, j->m_outer);
        inner.Head(20000, 10).Val(1);
        inner.Head(17000, static_cast<uint32_t>(6 + 5 * j->m_parts.size()));
        for (const C_Junkers88::S_Part *part : j->m_parts)
            inner.Val(part->stage).Val(part->hideIntact);
        S_SaBytes e;
        e.Head(0x777, static_cast<uint32_t>(6 + 10 + 6 + inner.b.size()));
        e.Head(0x3039, 10).Val(g_actorSaveTag).Head(1000, static_cast<uint32_t>(6 + inner.b.size()));
        e.Put(inner.b.data(), inner.b.size());
        bool saved = false;
        const std::vector<uint8_t> bytes = SaWritten([&](C_chunk &c) { return j->SaveGame(c); }, &saved);
        JU(r.junkSave, saved && bytes == e.b);
        // (Hidden: no 2000.)
        ju->m_frameFlags &= ~1u;
        const std::vector<uint8_t> hidden = SaWritten([&](C_chunk &c) { return j->SaveGame(c); });
        ju->m_frameFlags |= 1u;
        JU(r.junkSave, hidden.size() == bytes.size() - 6);
        // A state with all of it: fighters (one down), debris, tracers; saved, read into a new start,
        // saved again the same.
        playMode.unknown0C = 2;
        j->SetState(2);
        j->m_fighters[1]->Damage(100.0f);
        j->m_fighters[0]->attacking = true;
        j->m_fighters[0]->fireTime = 33;
        j->m_fighters[0]->model->Tick(70);
        j->ThrowDebris(f["tail00"]);
        j->Fire(S_vector(px, 310.0f, pz), S_vector(0.0f, 0.0f, 1.0f));
        j->m_tracers[0]->distance = 123.0f;
        void *const oldFind = g_driverVtable[0x88 / 4];
        g_driverVtable[0x88 / 4] = reinterpret_cast<void *>(&F_JuDriverFind);
        g_aaDriverFinds = f["tail00"];
        F_JuSetAnimTime(j->m_fighters[0]->model, 0, 345);
        F_JuSetAnimTime(j->m_fighters[0]->model, 1, 678);
        const int fighterTime = F_JuGetAnimTime(j->m_fighters[0]->model, 0);
        auto *downAnim = reinterpret_cast<S_SeAnim *>(F_JuGetAnimation(j->m_fighters[1]->model, 1));
        const std::string downName = downAnim->name;
        static std::string downFile;
        downFile = "anims\\" + downName;
        downAnim->fileName = downFile.c_str();
        F_JuSetAnimTime(j->m_camera->model, 0, 4321);
        j->m_fall.falling = false;
        j->m_fall.pos.x += 3.0f;
        const float savedFallX = j->m_fall.pos.x;
        std::vector<int> groupIndices[5];
        {
            const PartList *lists[5] = {&j->m_damage.broken, &j->m_engines.parts, &j->m_engines.broken,
                                        &j->m_outer.parts, &j->m_outer.broken};
            for (int k = 0; k < 5; ++k)
                for (const C_Junkers88::S_Part *part : *lists[k])
                    groupIndices[k].push_back(part->index);
        }
        const std::vector<uint8_t> full = SaWritten([&](C_chunk &c) { return j->SaveGame(c); });
        downAnim->fileName = downAnim->name.c_str();
        std::vector<uint8_t> downBytes(2, 0);
        downBytes[0] = static_cast<uint8_t>(downName.size() + 1);
        downBytes.insert(downBytes.end(), downName.begin(), downName.end());
        downBytes.push_back(0);
        const auto *fallPos = reinterpret_cast<const uint8_t *>(&j->m_fighters[1]->fall.pos);
        downBytes.insert(downBytes.end(), fallPos, fallPos + 12);
        JU(r.junkSave, std::search(full.begin(), full.end(), downBytes.begin(), downBytes.end()) != full.end());
        j->m_engines.parts.push_back(p(0));
        j->m_engines.broken.push_back(p(0));
        j->m_damage.broken.push_back(p(0));
        j->m_outer.broken.push_back(p(0));
        restart();
        JU(r.junkSave, j->m_fighters.empty() && j->m_health == 1000.0f && j->m_engines.parts.empty() &&
                           j->m_engines.broken.empty() && j->m_damage.broken.empty() && j->m_outer.broken.empty() &&
                           j->m_outer.parts.empty());
        j->m_velocity = {};
        for (PartList *list : {&j->m_damage.broken, &j->m_engines.parts, &j->m_engines.broken, &j->m_outer.parts,
                               &j->m_outer.broken})
            list->push_back(p(0));
        // (A short group chunk: the count left, a list of one without its index: part 0; no broken.)
        {
            S_SaBytes shortGroup;
            shortGroup.Head(0x777, 6 + 8).Val(3).Val(1);
            S_SaReader rg(shortGroup);
            C_Junkers88::S_PartGroup g;
            j->LoadParts(rg.c, g);
            JU(r.junkSave, g.left == 3 && g.parts == PartList{p(0)} && g.broken.empty());
        }
        S_SaBytes in;
        in.Put(full.data(), full.size());
        // (Two bytes after the state: still to read once it is loaded.)
        uint32_t outerSize;
        std::memcpy(&outerSize, in.b.data() + 2, 4);
        outerSize += 2;
        std::memcpy(in.b.data() + 2, &outerSize, 4);
        in.Val(uint16_t(0));
        S_SaReader rd(in);
        PartList groups[5];
        for (int k = 0; k < 5; ++k)
            for (const int index : groupIndices[k])
                groups[k].push_back(p(index));
        reset();
        JU(r.junkSave, j->LoadGame(rd.c) && j->m_health == 640.0f && j->m_damageRate == 60.0f && j->m_firepower == 1.5f &&
                           j->m_interceptorHealth == 2.5f && j->m_engineDown[1] && !j->m_engineDown[0] &&
                           j->m_hitsByOthers == 1 && j->m_velocity.x == 1.0f && j->m_velocity.z == 3.0f &&
                           j->m_fighters.size() == 2 && j->m_debris.size() == 1 && j->m_tracers.size() == 1 &&
                           j->m_tracers[0]->distance == 123.0f && j->m_fighters[1]->down && !j->m_fighters[0]->down &&
                           j->m_fighters[0]->attacking && j->m_fighters[0]->fireTime == 33 && p(8)->stage == 1 &&
                           JuLogged("hierarchy ju88 1 88") == 1 && g_juFound == "tail00" && rd.c.RemainingSize() == 2 &&
                           j->m_damage.broken == groups[0] && j->m_engines.parts == groups[1] &&
                           j->m_engines.broken == groups[2] && j->m_outer.parts == groups[3] && j->m_outer.broken == groups[4] &&
                           F_JuGetAnimTime(j->m_camera->model, 0) == 4321 && !j->m_fall.falling &&
                           j->m_fall.pos.x == savedFallX);
        // (The fighters: their paths' animations on again, the notes off meanwhile; the down one's
        // animation by its file's name.)
        const Fighter *back0 = j->m_fighters[0], *back1 = j->m_fighters[1];
        JU(r.junkSave, back0->inWorld && !back1->inWorld && back0->path->index == 1 &&
                           F_JuGetAnimTime(back0->model, 1) == fighterTime && back0->motor && back0->guns[4] &&
                           JuAnimName(F_JuGetAnimation(back0->model, 1)) == "#adamage0" && back0->fall.radius == 2.0f &&
                           back0->volume.actor == j && back0->gunFrames.size() == 3 &&
                           JuAnimName(F_JuGetAnimation(back1->model, 1)) == downName &&
                           JuAnimName(F_JuGetAnimation(back1->model, 1)).rfind("#bfshotdwn", 0) == 0 &&
                           g_juCallbacks[back0->path->model->GetChild(0)].first == &C_Junkers88::PathNote);
        // (Its stage 0's time as saved: the load ticks the animated stages only.)
        F_JuSetAnimTime(back0->model, 0, fighterTime);
        const std::vector<uint8_t> again = SaWritten([&](C_chunk &c) { return j->SaveGame(c); });
        JU(r.junkSave, again == full);
        g_driverVtable[0x88 / 4] = oldFind;
        g_aaDriverFinds = nullptr;
        {
            S_SaBytes fc;
            fc.Head(0x777, 6 + 4 + 4 + 3 + 8 + 4 + 8 + 2 + 15 + 4 + 1 + 4)
                .Val(2).Val(10.0f).Val(false).Val(false).Val(false).Val(-1).Val(-1).Val(0).Val(0).Val(0).Val(uint16_t(15));
            fc.Put("#airfight_02_A", 15);
            fc.Val(1).Val(true).Val(0);
            S_SaReader rf(fc);
            Fighter *cut = new Fighter(rf.c, "la_Bf109Canyon");
            JU(r.junkSave, cut->model && cut->pathIndex == 2 && cut->health == 10.0f &&
                               JuAnimName(F_JuGetAnimation(cut->model, 1)) == "#adamage0" &&
                               F_JuGetAnimTime(cut->model, 1) == 0);
            delete cut;
        }
        // (Another first chunk: left at once (what follows it still to read); an unknown chunk inside:
        // skipped; empty lists' chunks: none.)
        S_SaBytes odd;
        odd.Head(0x777, 6 + 10 + 10 + 2).Head(0x3039, 10).Val(g_actorSaveTag).Head(999, 10).Val(7).Val(uint16_t(5));
        S_SaReader rd2(odd);
        j->m_health = 5.0f;
        JU(r.junkSave, j->LoadGame(rd2.c) && j->m_health == 5.0f && rd2.c.RemainingSize() == 2);
        S_SaBytes empties;
        empties.Head(0x777, 6 + 10 + 6 + 18).Head(0x3039, 10).Val(g_actorSaveTag).Head(1000, 6 + 18).Head(5000, 6)
            .Head(6000, 6).Head(13000, 6);
        S_SaReader rd4(empties);
        const size_t fighters = j->m_fighters.size(), debris = j->m_debris.size(), tracers = j->m_tracers.size();
        JU(r.junkSave, j->LoadGame(rd4.c) && j->m_fighters.size() == fighters && j->m_debris.size() == debris &&
                           j->m_tracers.size() == tracers);
        S_SaBytes skip;
        skip.Head(0x777, 6 + 10 + 6 + 6 + 10).Head(0x3039, 10).Val(g_actorSaveTag).Head(1000, 6 + 6 + 10).Head(1234, 6)
            .Head(7000, 10).Val(77.0f);
        S_SaReader rd3(skip);
        JU(r.junkSave, j->LoadGame(rd3.c) && j->m_health == 77.0f);
    }

    // --- the stop: the seats emptied (into the sector), the sounds, the parts (the model loaded again),
    // the camera path, the seat weapons, the look, the fighters, the body, the scripts' own cutscene
    // ended, the tracers and their model, the camera's field of view; a server's only deactivates; the
    // restart by the game's roles ------------------------------------------------------------------------
    {
        restart();
        playMode.unknown0C = 2;
        j->SetState(1);
        C_Human *rider = new C_Human();
        I3D_frame *body = JuChild(FRAME_DUMMY, "rider", nullptr);
        LinkUnder(body, g_sector);
        rider->m_frame = body;
        j->SetSeatOccupant(2, rider);
        j->Fire(S_vector(px, 300.0f, pz), S_vector(0.0f, 0.0f, 1.0f));
        j->Fire(S_vector(px, 301.0f, pz), S_vector(0.0f, 0.0f, 1.0f));
        j->m_tracers[1]->distance = 1.0f;
        j->UpdateTracers(10);
        const std::string t0 = ItName(j->m_tracers[0]->frame), t1 = ItName(j->m_tracerPool[0]->frame);
        g_itNames[j->m_camera->model] = "campath";
        I3D_frame *trace = j->m_trace;
        const int traceRefs = trace->m_refCount;
        JuSetCutscene(true, -1);
        reset();
        j->StopMission(false);
        JuSetCutscene(false, 0);
        JU(r.junkStop, g_deactivations == std::vector<bool>{false} && body->m_parent == g_sector &&
                           JuLogged("link rider sector 1") == 1 && !j->GetSeatOccupant(2) && !j->m_sounds[0] &&
                           !j->m_sounds[5] && j->m_parts.empty() && j->m_damage.parts.empty() && j->m_outer.left == 3 &&
                           g_juLoads["ju88.4ds"] == 1 && !j->m_camera && JuLogged("release campath") == 1 &&
                           j->m_seatWeapons.empty() && g_weaponEvents == std::vector<std::string>{"delete 0"} &&
                           JuLogged("anim ju88 1 0 1 0") == 1 && j->m_fighters.empty() && !j->m_body &&
                           JuLogged("endcutscene") == 1 && j->m_tracers.empty() && j->m_tracerPool.empty() &&
                           JuLogged("release " + t0) == 1 && JuLogged("release " + t1) == 1 && !j->m_trace &&
                           trace->m_refCount == traceRefs - 1);
        char fov[64];
        std::snprintf(fov, sizeof fov, "fov jucamera %.9g", GetGame()->GetCutsceneFOV());
        int removed = 0;
        for (const S_VolumeOp &op : g_volumeLog)
            removed += op.op == 'R';
        JU(r.junkStop, JuLogged(fov) == 1 && removed == 1 && JuLogged("release snd0") + JuLogged("release snd1") >= 0);
        // (Another cutscene: not ended; a server's stop: the deactivation only.)
        j->StartMission(false);
        JuSetCutscene(true, 31);
        reset();
        j->StopMission(false);
        JuSetCutscene(false, 0);
        JU(r.junkStop, JuLogged("endcutscene") == 0);
        reset();
        j->StopMission(true);
        JU(r.junkStop, g_deactivations == std::vector<bool>{true} && g_itLog.empty());
        // The restart: a client's (stop, start), a server's, both (the client's around the server's).
        const auto roles = [&](uint8_t role, std::vector<bool> stops, std::vector<bool> starts) {
            g_gameRole = role;
            reset();
            j->RestartMission(false);
            return g_deactivations == stops && g_activations == starts;
        };
        JU(r.junkStop, roles(GAME_ROLE_CLIENT, {false}, {false}) && roles(GAME_ROLE_SERVER, {true}, {true}) &&
                           roles(GAME_ROLE_LOCAL, {false, true}, {true, false}) && j->m_camera);
        g_gameRole = GAME_ROLE_LOCAL;
        delete rider;
    }

    g_failInitSeat = oldFailSeat;
    reset();
    j->StopMission(false);
    creator->Destroy(j);
    g_aaPlanes.clear();
    g_rayTestHook = nullptr;
    g_sphereTestHook = nullptr;
    g_sphereHookSetsHit = false;
    g_cacheFill = oldFill;
    g_rbSplashStep = oldSplashStep;
    g_rbSplashHandle = oldSplashHandle;
    g_seAnimVtable[2] = oldAnimEnd;
    for (int i = 0; i < FRAME_SLOTS; ++i)
        g_frameVtable[frameSlots[i] / 4] = oldFrame[i];
    g_driverVtable[0x54 / 4] = oldCreate;
    g_driverVtable[0x50 / 4] = oldVisual;
    for (int i = 0; i < 5; ++i)
        g_soundVtable[soundSlots[i] / 4] = oldSound[i];
    g_maSoundOn = oldSoundOn;
    g_gameRole = oldRole;
    g_textIds = oldTextIds;
    GetGame()->m_time = oldTime;
    g_firstPerson = oldFirstPerson;
    g_gameProfile.m_hudOption = oldHud;
    g_gameProfile.m_playMode = oldPlayMode;
    *reinterpret_cast<I3D_sector **>(sceneBase + 0x1F8) = oldPrimary;
    *reinterpret_cast<I3D_frame **>(sceneBase + 0x15C) = oldCamera;
    GetGame()->m_scene = oldScenePtr;
    g_sector->m_firstChild = nullptr;
    for (S_StubFrame *stub : g_frames)
        g_itNames.erase(stub->frame);
    reset();
    std::printf("junk: junk %d junkProps %d junkStart %d junkChatter %d junkSeats %d junkParts %d junkBreak %d "
                "junkDamage %d junkUpdate %d junkDebris %d junkTracers %d junkFighters %d junkFly %d junkHit %d "
                "junkTarget %d junkSave %d junkStop %d\n",
                r.junk, r.junkProps, r.junkStart, r.junkChatter, r.junkSeats, r.junkParts, r.junkBreak, r.junkDamage,
                r.junkUpdate, r.junkDebris, r.junkTracers, r.junkFighters, r.junkFly, r.junkHit, r.junkTarget,
                r.junkSave, r.junkStop);
}
