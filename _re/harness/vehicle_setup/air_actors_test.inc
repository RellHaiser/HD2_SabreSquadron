// Part 7g's test (the actors build: build_item.bat also links src/game/air_attack.cpp; main runs
// AirActorsTest after PropActorsTest on every model): the air attack against oracles written from
// the original's code (the asm for the flight's sums and the shots' aim). A small world through the
// ray hook: ground of a chosen material, border planes, a wall, the target's box; the dynamic ray
// through world_actors_test.inc's fake; the sounds through the harness's fake sound frames.
// check_item.py reads the "air:" line.

#include "game/air_attack.h"

// --- the game's state the plane reads ---------------------------------------------------------------

// The script module (its cutscene flag at +0x824): bytes under the name (this file does not see
// C_ScriptModule; a global's name carries no type).
alignas(16) unsigned char g_scriptModule[0x1000];
// (The flag: +0x824 in the original, +0x820 as g++ lays the class out here.)
void AaCutscene(bool on)
{
    g_scriptModule[0x820] = on;
    g_scriptModule[0x824] = on;
}
int g_aaStartDelay;
C_SightModule g_sightModule;
int C_SightModule::GetStartDelay() const
{
    return g_aaStartDelay;
}
I3D_sector *g_aaActorSector;
I3D_sector *C_Actor::GetSector() const
{
    return g_aaActorSector;
}
// The camera module (the whizz near the camera): its place.
S_vector4 g_aaCameraPos = {0.0f, 0.0f, 0.0f, 1.0f};
alignas(16) uint8_t g_aaCameraModule[0x100];
C_CameraModule *C_game::GetCameraModule()
{
    return reinterpret_cast<C_CameraModule *>(g_aaCameraModule);
}
bool C_CameraModule::GetCameraPos(int index, S_vector4 *pos)
{
    ItLog("camerapos %d", index);
    *pos = g_aaCameraPos;
    return true;
}
// S_matrix::SetDir3(dir) (LS3DF 0x1002c7d0, from the LS3DF port): looking along `dir`, the up
// vector as near world y as it can be.
void __stdcall S_matrix::SetDir3(const S_vector &dir)
{
    S_vector &x = (*this)(0), &y = (*this)(1), &z = (*this)(2);
    z = dir;
    const float sq = dir.z * dir.z + dir.y * dir.y + dir.x * dir.x;
    if (std::fabs(sq - 1.0f) < 1e-08f) {
        z = dir;
    } else if (!(sq < 1e-08f)) {
        const float inv = 1.0f / std::sqrt(sq);
        z = S_vector(inv * dir.x, inv * dir.y, inv * dir.z);
    } else if (dir.x != 0.0f) {
        z.x = dir.x < 0.0f ? -1.0f : 1.0f;
    } else if (dir.z != 0.0f) {
        z.z = dir.z < 0.0f ? -1.0f : 1.0f;
    } else {
        z.y = dir.y < 0.0f ? -1.0f : 1.0f;
    }
    S_vector p(z.x * z.y, z.y * z.y, z.z * z.y);
    S_vector up(-p.x, 1.0f - p.y, -p.z);
    if (up.y * up.y + up.z * up.z + up.x * up.x < 1e-08f) {
        p = S_vector(z.x * z.z, z.y * z.z, z.z * z.z);
        up = S_vector(-p.x, -p.y, 1.0f - p.z);
        if (up.x * up.x + up.z * up.z + up.y * up.y < 1e-08f)
            up = S_vector(1.0f, 0.0f, 0.0f);
    }
    const float inv = 1.0f / std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
    y = S_vector(up.x * inv, up.y * inv, up.z * inv);
    x = S_vector(y.y * z.z - y.z * z.y, y.z * z.x - y.x * z.z, y.x * z.y - y.y * z.x);
}
// A sound open failing for one file (the rest as the fake's).
std::string g_aaFailFile;
HRESULT __stdcall F_AaSoundOpen(I3D_frame *self, const char *fileName, uint32_t flags)
{
    if (g_aaFailFile == fileName)
        return I3DERR_NOFILE;
    return F_SoundOpen(self, fileName, flags);
}
// The driver's FindFrame(scene, name) (slot 0x88): logged, g_aaDriverFinds.
I3D_frame *g_aaDriverFinds;
I3D_frame *__stdcall F_AaDriverFind(void *, void *, const char *name)
{
    ItLog("driverfind %s", name);
    return g_aaDriverFinds;
}
S_vector AaVec(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}
// The sounds' velocity (slot 0xB0).
std::map<const I3D_frame *, S_vector> g_aaVelocity;
void __stdcall F_AaSoundVelocity(I3D_frame *self, const S_vector &v)
{
    g_aaVelocity[self] = v;
    ItLog("velocity %s %s", ItName(self).c_str(), ItV(v).c_str());
}
// SetLoop (slot 0x90) and IsPlaying (0x78) logged where a case needs their arguments.
void __stdcall F_AaSoundLoop(I3D_frame *self, bool loop)
{
    F_SoundLoop(self, loop);
    ItLog("loop %s %d", ItName(self).c_str(), loop ? 1 : 0);
}
bool __stdcall F_AaSoundPlaying(I3D_frame *self, int arg)
{
    ItLog("playing %s %d", ItName(self).c_str(), arg);
    return F_SoundPlaying(self, arg);
}
// The calls one sound got (g_soundLog's letters, in turn).
std::string AaSoundCalls(const void *sound)
{
    std::string s;
    for (const auto &e : g_soundLog)
        if (e.first == sound)
            s += e.second;
    return s;
}
// RandomFloat(max) after `random` (the harness's generator).
float AaNextRandom(uint32_t &random, float max)
{
    random = random * 1103515245u + 12345u;
    return static_cast<float>((random >> 8) & 0xFFFF) / 65536.0f * max;
}

// The world of the rays (CollisionRayTest): planes (normal . p = level) met from either side within
// the ray, the nearest first; each with its volume (material, kind) and the normal it reports.
struct S_AaPlane {
    S_vector normal;
    float level;
    S_CollisionVolume *volume;
    S_vector reported;
    bool on = true;
};
std::vector<S_AaPlane> g_aaPlanes;
struct S_AaRay {
    S_vector from, dir;
    uint32_t types, flags;
};
std::vector<S_AaRay> g_aaRays;
S_CollisionVolume *AaRay(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                         uint32_t types, uint32_t flags)
{
    g_aaRays.push_back({from, dir, types, flags});
    const S_AaPlane *best = nullptr;
    float bestT = 2.0f;
    for (const S_AaPlane &p : g_aaPlanes) {
        if (!p.on)
            continue;
        if (types == 0xFFFFFFFF && (p.volume->flags & 0xFF))   // (every dynamic volume's type excluded)
            continue;
        const float denom = p.normal.Dot(dir);
        if (std::fabs(denom) < 1e-9f)
            continue;
        const float t = (p.level - p.normal.Dot(from)) / denom;
        if (t < 0.0f || t > 1.0f || t >= bestT)
            continue;
        bestT = t;
        best = &p;
    }
    if (!best)
        return nullptr;
    if (hitPos)
        *hitPos = from + dir * bestT;
    if (hitNormal)
        *hitNormal = best->reported;
    return best->volume;
}
S_CollisionVolume MakeAaVolume(int material, uint32_t shape = 0)
{
    S_CollisionVolume v;
    v.flags = (static_cast<uint32_t>(material) << 24) | shape;
    return v;
}

struct S_Aa {
    bool air = true, airProps = true, airStart = true, airStop = true, airTarget = true, airFly = true, airClimb = true,
         airStates = true, airFire = true, airBullets = true, airSounds = true, airDust = true, airHit = true,
         airSave = true, airFind = true, airRay = true;
};
#define AA(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

void AirActorsTest()
{
    S_Aa r;
    const auto reset = [&] {
        g_itLog.clear();
        g_activations.clear();
        g_deactivations.clear();
        g_volumeLog.clear();
        g_waRays.clear();
        g_aaRays.clear();
        g_soundLog.clear();
        g_aaVelocity.clear();
        g_rbSplashes.clear();
        g_particlesMade.clear();
        g_particlesDestroyed.clear();
        ClearStreams();
    };
    // The fakes this test needs: frames (logged as the prop test's), sounds (created by the driver),
    // the ray world.
    const int frameSlots[] = {0x00, 0x08, 0x0C, 0x1C, 0x20, 0x24, 0x30, 0x34, 0x38, 0x3C, 0x58, 0x90};
    constexpr int FRAME_SLOTS = sizeof frameSlots / sizeof frameSlots[0];
    void *oldFrame[FRAME_SLOTS];
    for (int i = 0; i < FRAME_SLOTS; ++i)
        oldFrame[i] = g_frameVtable[frameSlots[i] / 4];
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_frameVtable[0x0C / 4] = reinterpret_cast<void *>(&F_ObSetDir);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_ObSetWorldDir);
    g_frameVtable[0x20 / 4] = reinterpret_cast<void *>(&F_SeTick);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x34 / 4] = reinterpret_cast<void *>(&F_SeSetName);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_WaUpdateBound);
    g_frameVtable[0x90 / 4] = reinterpret_cast<void *>(&F_SeLocalBound);
    void *const oldCreate = g_driverVtable[0x54 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_MaCreateFrame);
    const int soundSlots[] = {0x00, 0x08, 0x24, 0x38, 0xB0};
    void *oldSound[5];
    for (int i = 0; i < 5; ++i)
        oldSound[i] = g_soundVtable[soundSlots[i] / 4];
    g_soundVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_soundVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_soundVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_soundVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_soundVtable[0xB0 / 4] = reinterpret_cast<void *>(&F_AaSoundVelocity);
    g_rayTestHook = &AaRay;
    const int oldSplashStep = g_rbSplashStep, oldSplashHandle = g_rbSplashHandle;
    g_rbSplashStep = 1;                                 // (the particles told apart)
    g_rbSplashHandle = 300;
    void *const oldSceneFind = g_sceneVtable[0x7C / 4];
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_MaFindFrame);
    const bool oldSoundOn = g_maSoundOn;
    g_maSoundOn = true;
    const uint8_t oldRole = g_gameRole;
    g_gameRole = GAME_ROLE_SERVER;
    I3D_scene *const oldScenePtr = GetGame()->m_scene;
    GetGame()->m_scene = g_carScene;
    auto *sceneBase = reinterpret_cast<uint8_t *>(g_carScene);
    I3D_sector *const oldPrimary = *reinterpret_cast<I3D_sector **>(sceneBase + 0x1F8);
    *reinterpret_cast<I3D_sector **>(sceneBase + 0x1F8) = reinterpret_cast<I3D_sector *>(g_sector);
    I3D_frame *const oldCamera = *reinterpret_cast<I3D_frame **>(sceneBase + 0x15C);
    I3D_frame *camera = NewFrame(FRAME_DUMMY, "aacamera", nullptr);
    *reinterpret_cast<I3D_frame **>(sceneBase + 0x15C) = camera;
    g_itNames[g_sector] = "sector";
    int seed = static_cast<int>(g_frames.size()) * 17 + 3;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };

    C_ActorCreator *creator = g_actorCreators[ACTOR_AIR_ATTACK];
    AA(r.air, creator && std::string(creator->GetName()) == "AirAttack");
    // The plane: a model, its z axis tilted (StartMission levels it), its parts.
    const float px = rnd(-50.0f, 50.0f), pz = rnd(-50.0f, 50.0f);
    I3D_frame *plane = RbBoxFrame(nullptr, "plane", -5.0f, 5.0f, -1.0f, 1.5f, -4.0f, 4.5f, S_vector(px, 60.0f, pz));
    LinkUnder(plane, g_sector);
    plane->m_name = Stub(plane).name.c_str();
    plane->m_frameFlags |= 1u;                           // (shown)
    plane->m_localMatrix(2) = S_vector(0.48f, 0.6f, 0.64f);
    plane->m_frameFlags &= ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY);
    plane->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
    std::map<std::string, I3D_frame *> part;
    for (const char *name : {"listy", "carriage_l00", "carriage_r00", "bilboard", "bilboard_l00", "bilboard_r00",
                             "bilboard_l01", "bilboard_r02", "bluur"}) {
        I3D_frame *f = NewFrame(FRAME_DUMMY, name, plane);
        f->m_name = Stub(f).name.c_str();
        part[name] = f;
    }

    auto *a = static_cast<C_AirAttack *>(creator->Create());
    AA(r.air, a->GetType() == ACTOR_AIR_ATTACK && !g_airAttacks.empty() && g_airAttacks.back() == a &&
                  a->m_hitPoints == 100.0f && a->m_damage == 70.0f && !a->m_started && !a->m_motorSound &&
                  !a->m_gunSound && a->m_smoke == -1);
    a->m_hitPoints = 1.0f;
    a->m_damage = 2.0f;
    AA(r.air, a->SetFrame(plane) && a->GetFrame() == plane && a->m_hitPoints == 100.0f && a->m_damage == 70.0f);
    a->SetNetId(90);
    // (Another plane comes and goes from the list: the last one swapped into its place.)
    {
        auto *b = static_cast<C_AirAttack *>(creator->Create());
        auto *c = static_cast<C_AirAttack *>(creator->Create());
        const size_t n = g_airAttacks.size();
        creator->Destroy(b);
        AA(r.air, g_airAttacks.size() == n - 1 && g_airAttacks[n - 3] == a && g_airAttacks[n - 2] == c);
        creator->Destroy(c);
        AA(r.air, g_airAttacks.size() == n - 2 && g_airAttacks.back() == a);
    }

    // --- the properties: chunk 2 the hit points, 4 the damage (reported false); read: others skipped ---
    {
        S_SaBytes e;
        e.Head(0x777, 6 + 10 + 10).Head(2, 10).Val(100.0f).Head(4, 10).Val(70.0f);
        bool written = true;
        AA(r.airProps, SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &written) == e.b && !written);
        S_SaBytes in;
        in.Head(0x777, 6 + 6 + 10 + 10 + 10 + 8).Head(1, 6).Head(2, 10).Val(250.0f).Head(3, 10).Val(9.0f).Head(4, 10)
            .Val(12.0f).Head(2, 8).Val(uint16_t(7));
        S_SaReader rd(in);
        AA(r.airProps, a->ReadProperties(rd.c) && a->m_hitPoints == 250.0f && a->m_damage == 12.0f);
        a->m_hitPoints = 120.0f;
        a->m_damage = 30.0f;
    }

    // --- the start (a server): levelled, idle, the parts hidden, the flashes, the sounds, the box ---------
    {
        reset();
        g_maSoundsMade = 0;
        std::srand(5);
        const uint32_t random0 = g_waRandom;
        a->StartMission(true);
        const S_matrix &world = plane->GetMatrix();
        S_vector d0(0.48f, 0.6f, 0.64f);
        PrNormalize(d0, (d0.x * d0.x + d0.y * d0.y) + d0.z * d0.z);
        S_vector d1(d0.x, 0.0f, d0.z);
        PrNormalize(d1, (d1.x * d1.x + d1.y * d1.y) + d1.z * d1.z);
        AA(r.airStart, g_activations == std::vector<bool>{true} && a->m_started && a->m_idle && !a->m_target &&
                           a->m_direction.x == d1.x && a->m_direction.y == 0.0f && a->m_direction.z == d1.z &&
                           a->m_direction.w == world.m[2][3] && a->m_position.x == world.m[3][0] &&
                           a->m_position.y == world.m[3][1] && a->m_position.z == world.m[3][2] &&
                           a->m_position.w == world.m[3][3] && a->m_health == 120.0f && a->m_speed == 100.0f &&
                           a->m_clearance == 30.0f && a->m_ceiling == 100.0f && a->m_smoke == -1 && a->m_fireTime == 0 &&
                           a->m_dustTime == 0 && a->m_turnRate == 0.0f && a->m_climbRate == 0.0f && a->m_roll == 0.0f &&
                           !a->m_rolling && !a->m_gone && !a->m_targetInSight && a->m_propeller == part["bluur"] &&
                           !a->m_motorSound && !a->m_gunSound);
        AA(r.airStart, a->m_flashes.size() == 5 && a->m_flashes[0]->frame == part["bilboard"] &&
                           a->m_flashes[1]->frame == part["bilboard_l00"] && a->m_flashes[2]->frame == part["bilboard_r00"] &&
                           a->m_flashes[3]->frame == part["bilboard_l01"] && a->m_flashes[4]->frame == part["bilboard_r02"] &&
                           a->m_flashes[1]->time == -1 && part["bilboard_l00"]->m_scale.x == 5.0f &&
                           part["bilboard_r02"]->m_scale.z == 5.0f && part["bilboard"]->m_scale.x == 1.0f);
        // The sounds: under the plane, positional, looping, off, from a random place; the motors at a
        // random pitch.
        uint32_t rr = random0;
        const auto nextR = [&](float max) {
            rr = rr * 1103515245u + 12345u;
            return static_cast<float>((rr >> 8) & 0xFFFF) / 65536.0f * max;
        };
        std::srand(5);
        bool soundsOk = true;
        for (int i = 0; i < 4; ++i) {
            const I3D_sound *snd = a->m_motorSounds[i];
            const S_FakeSound &f = g_sounds[snd];
            const int at = std::rand() % 5000;
            const float pitch = 1.25f - nextR(0.5f);
            uint32_t flags;
            std::memcpy(&flags, reinterpret_cast<const uint8_t *>(snd) + 0x178, 4);
            soundsOk = soundsOk && snd && f.file == "motor" + std::to_string(i + 1) + ".wav" && f.type == I3DSOUND_POINT &&
                       f.loop && f.range[0] == 20.0f && f.range[1] == 500.0f && f.range[2] == 0.3f && f.range[3] == 0.3f &&
                       !reinterpret_cast<const I3D_frame *>(snd)->IsOn() && f.playAt == at && f.frequency == pitch &&
                       (flags & CACHE_OPEN_INTO) && reinterpret_cast<const I3D_frame *>(snd)->m_parent == plane &&
                       reinterpret_cast<const I3D_frame *>(snd)->GetPos().x == 0.0f;
        }
        for (int i = 0; i < 5; ++i) {
            const I3D_sound *snd = a->m_gunSounds[i];
            const S_FakeSound &f = g_sounds[snd];
            soundsOk = soundsOk && snd && f.file == "kul_d" + std::to_string(i + 1) + ".wav" && f.loop && f.type == 1 &&
                       f.range[0] == 20.0f && f.range[1] == 250.0f && f.range[2] == 0.3f && f.playAt == std::rand() % 500 &&
                       f.frequencies == 0;
        }
        AA(r.airStart, soundsOk);
        // (The shots' sounds into the cache through one more sound, released.)
        I3D_frame *preload = nullptr;
        for (auto &entry : g_itNames)
            if (entry.second == "snd9")
                preload = const_cast<I3D_frame *>(entry.first);
        AA(r.airStart, preload && g_sounds[preload].opens == 10 && g_sounds[preload].file == "sand05.wav");
        // The box: the hierarchy's (a pike's), its centre's w the local centre's x.
        I3D_bbox hb;
        const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hb, plane);
        const float centerX = box.min.x + (box.max.x - box.min.x) * 0.5f;
        AA(r.airStart, g_volumeLog.size() == 1 && g_volumeLog[0].op == 'A' && g_volumeLog[0].volume == &a->m_volume &&
                           a->m_volume.type == COLLISION_TYPE_PIKE && a->m_volume.flags == COLLISION_SHAPE_BOX &&
                           a->m_volume.actor == a && a->m_volume.frame == plane && a->m_volume.centerW == centerX &&
                           RbSameBox(a->m_volume.bbox, box) && a->m_volume.invMatrix == &a->m_invMatrix);
        AA(r.airStart, SaLogIs({"on listy 0", "on carriage_l00 0", "on carriage_r00 0", "on bilboard 0", "on bilboard_l00 0", "on bilboard_r00 0", "on bilboard_l01 0", "on bilboard_r02 0", "createframe 4", "link snd0 plane 0", "random 0.5", "update snd0", "createframe 4", "link snd1 plane 0", "random 0.5", "update snd1", "createframe 4", "link snd2 plane 0", "random 0.5", "update snd2", "createframe 4", "link snd3 plane 0", "random 0.5", "update snd3", "createframe 4", "link snd4 plane 0", "update snd4", "createframe 4", "link snd5 plane 0", "update snd5", "createframe 4", "link snd6 plane 0", "update snd6", "createframe 4", "link snd7 plane 0", "update snd7", "createframe 4", "link snd8 plane 0", "update snd8", "createframe 4", "release snd9"}));
        // Started: only the direction again.
        a->m_direction = {0.0f, 0.0f, 1.0f, 0.0f};
        reset();
        a->StartMission(false);
        AA(r.airStart, g_itLog.empty() && g_activations == std::vector<bool>{false} && a->m_direction.x == d1.x &&
                           a->m_motorSounds[0] && g_volumeLog.empty());
    }

    // --- the stop: the box out, the smoke gone, every sound and list freed --------------------------------
    {
        a->m_smoke = 33;
        a->m_motorSound = a->m_motorSounds[1];
        a->m_gunSound = a->m_gunSounds[2];
        a->m_bullets.push_back(new C_AirAttack::S_Bullet);
        I3D_frame *impact = MakeSound();
        g_itNames[impact] = "impact";
        a->m_impactSounds.push_back(reinterpret_cast<I3D_sound *>(impact));
        I3D_frame *whizz = MakeSound();
        g_itNames[whizz] = "whizz";
        a->m_whizzSounds.push_back(reinterpret_cast<I3D_sound *>(whizz));
        a->AddDust({1.0f, 2.0f, 3.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f});
        const int stopDust = a->m_dusts.back()->particle;
        reset();
        a->StopMission(true);
        AA(r.airStop, !a->m_started && g_deactivations == std::vector<bool>{true} && g_volumeLog.size() == 1 &&
                          g_volumeLog[0].op == 'R' && g_particlesDestroyed == std::vector<int>{33} && a->m_smoke == -1 &&
                          !a->m_motorSound && !a->m_gunSound && a->m_bullets.empty() && a->m_impactSounds.empty() &&
                          a->m_whizzSounds.empty() && a->m_flashes.empty() && a->m_dusts.empty() && !a->m_motorSounds[3] &&
                          !a->m_gunSounds[4]);
        AA(r.airStop, SaLogIs({"release snd0", "release snd1", "release snd2", "release snd3", "release snd4", "release snd5",
                               "release snd6", "release snd7", "release snd8", "release impact", "release whizz",
                               "finish " + std::to_string(stopDust), "release new"}));
        reset();
        a->StopMission(false);
        AA(r.airStop, g_deactivations == std::vector<bool>{false} && g_itLog.empty() && g_volumeLog.empty());
        // A sound that does not load: none (its frame released); no sound driver: nothing cached.
        g_maSoundOn = false;
        g_aaFailFile = "motor2.wav";
        void *const oldOpen = g_soundVtable[0x74 / 4];
        g_soundVtable[0x74 / 4] = reinterpret_cast<void *>(&F_AaSoundOpen);
        reset();
        g_maSoundsMade = 0;
        a->StartMission(true);
        AA(r.airStop, !a->m_motorSounds[1] && a->m_motorSounds[0] && a->m_motorSounds[2] &&
                          std::count(g_itLog.begin(), g_itLog.end(), "release snd1") == 1 && g_maSoundsMade == 9);
        g_soundVtable[0x74 / 4] = oldOpen;
        g_aaFailFile.clear();
        g_maSoundOn = true;
        // The restart: a server's stop and start.
        reset();
        a->RestartMission(false);
        AA(r.airStop, a->m_started && g_deactivations == std::vector<bool>{true} &&
                          g_activations == std::vector<bool>{true} && a->m_motorSounds[1]);
    }

    // --- the target: a run (state 2, 35 m clear) or circling; shot down: back in state 2 ----------------
    I3D_frame *target = NewFrame(FRAME_DUMMY, "aatarget", nullptr);
    target->m_name = Stub(target).name.c_str();
    {
        a->m_speed = a->m_clearance = a->m_ceiling = 1.0f;
        a->m_state = C_AirAttack::AIR_ATTACK_LEAVING;
        a->SetTarget(false, target);
        AA(r.airTarget, !a->m_idle && a->m_state == C_AirAttack::AIR_ATTACK_PASSING && a->m_target == target &&
                            a->m_speed == 100.0f && a->m_clearance == 35.0f && a->m_ceiling == 100.0f);
        a->m_state = C_AirAttack::AIR_ATTACK_LEAVING;
        a->m_clearance = 1.0f;
        a->SetTarget(true, target);
        AA(r.airTarget, a->m_idle && a->m_state == C_AirAttack::AIR_ATTACK_LEAVING && a->m_clearance == 30.0f &&
                            a->m_ceiling == 100.0f && a->m_target == target);
        a->m_idle = false;
        a->SetTarget(false, nullptr);
        AA(r.airTarget, a->m_idle && a->m_state == C_AirAttack::AIR_ATTACK_LEAVING);
        a->m_state = C_AirAttack::AIR_ATTACK_SHOT_DOWN;
        a->SetTarget(false, nullptr);
        AA(r.airTarget, !a->m_idle && a->m_state == C_AirAttack::AIR_ATTACK_PASSING && !a->m_target);
        a->m_idle = true;
        a->m_state = C_AirAttack::AIR_ATTACK_LEAVING;
        a->m_target = nullptr;
    }

    // --- the update's conditions: no cutscene, shown, not gone, the sight module's start over ----------
    {
        a->m_position.y = 60.0f;
        reset();
        a->Update(20);
        AA(r.airFly, !g_itLog.empty());
        reset();
        AaCutscene(true);
        a->Update(20);
        AaCutscene(false);
        AA(r.airFly, g_itLog.empty());
        plane->m_frameFlags &= ~1u;
        a->Update(20);
        plane->m_frameFlags |= 1u;
        AA(r.airFly, g_itLog.empty());
        g_aaStartDelay = 2001;
        a->AddDust({1.0f, 2.0f, 3.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f});
        a->m_dusts.back()->time = 20;
        const int dustParticle = a->m_dusts.back()->particle;
        reset();
        a->Update(20);
        AA(r.airFly, a->m_dusts.empty() && SaLogIs({"finish " + std::to_string(dustParticle), "release new"}));
        g_aaStartDelay = 2000;
        a->m_gone = true;
        reset();
        a->Update(20);
        AA(r.airFly, g_itLog.empty());
        a->m_gone = false;
    }

    // --- the flight (circling over ground 60 m below, nothing ahead): the propeller, the motor, the turn
    // (0.75 rad/s), the bank, the move; the frame, the motor's velocity -------------------------------------
    static S_CollisionVolume ground, border, wall, targetBox, planeBox;
    ground = MakeAaVolume(1);
    border = MakeAaVolume(MATERIAL_STOP);
    wall = MakeAaVolume(7);
    targetBox = MakeAaVolume(0, COLLISION_SHAPE_BOX);
    targetBox.frame = target;
    planeBox = MakeAaVolume(0, COLLISION_SHAPE_BOX);
    planeBox.actor = a;
    g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
    const auto setPlane = [&](const S_vector &pos, const S_vector &dir) {
        a->m_position = {pos.x, pos.y, pos.z, 1.0f};
        a->m_direction = {dir.x, dir.y, dir.z, 0.0f};
    };
    const auto turnOracle = [](S_vector d, float t, float rate) {
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        return d.RotateByMatrix(RotateYMatrix(t * rate * 0.001f));
    };
    {
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->m_clearance = 30.0f;
        a->m_ceiling = 100.0f;
        a->m_roll = 0.0f;
        a->m_motorSound = nullptr;
        a->m_idle = true;
        const S_quat propRot = part["bluur"]->GetRot();
        std::srand(9);
        reset();
        uint32_t pitchRandom = g_waRandom;
        a->Update(20);
        std::srand(9);
        I3D_sound *motor = a->m_motorSounds[std::rand() % 4];
        // (The motor on at a random pitch; the rays straight down, then ahead, with no flags.)
        const float motorPitch = 1.25f - AaNextRandom(pitchRandom, 0.5f);
        AA(r.airFly, g_sounds[motor].frequency == motorPitch && AaSoundCalls(motor) == "OF" &&
                         g_aaRays.size() == 2 && g_aaRays[0].dir.x == 0.0f && g_aaRays[0].dir.z == 0.0f &&
                         g_aaRays[1].flags == 0);
        S_vector d = turnOracle(S_vector(0.6f, 0.0f, 0.8f), 20.0f, 0.75f);
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        const float step = 20.0f * 100.0f * 0.001f;
        const S_vector pos(step * d.x + px, step * d.y + 60.0f, step * d.z + pz);
        const float roll = 20.0f * 0.0005f + 0.0f;
        AA(r.airFly, a->m_turnRate == 0.75f && a->m_motorSound == motor && RbSame(AaVec(a->m_direction), d) &&
                         a->m_direction.x == d.x && a->m_direction.z == d.z && a->m_position.x == pos.x &&
                         a->m_position.y == pos.y && a->m_position.z == pos.z && a->m_roll == roll && !a->m_targetInSight);
        AA(r.airFly, SaSameQuat(part["bluur"]->m_rot,
                                SaNormalized(propRot.RotateByMatrix(RotateZMatrix(20.0f * 0.002f)))));
        AA(r.airFly, g_aaRays.size() == 2 && g_aaRays[0].from.y == 60.0f + 1000.0f && g_aaRays[0].dir.y == -10000.0f &&
                         g_aaRays[0].types == 0xFFFFFFFF && g_aaRays[0].flags == 0 && g_aaRays[1].dir.x == 0.6f * 30.0f &&
                         g_aaRays[1].types == 0xFFFFFFFF && g_sounds[motor].frequencies >= 1);
        const S_vector velocity((100.0f * d.x) * 0.5f, (d.y * 100.0f) * 0.5f, (d.z * 100.0f) * 0.5f);
        AA(r.airFly, RbSame(g_aaVelocity[motor], velocity));
        char rollText[32];
        std::snprintf(rollText, sizeof rollText, "%.9g", roll);
        AA(r.airFly, SaLogIs({"update bluur", "random 0.5", "update " + ItName(motor), "setpos plane " + ItV(pos),
                              "setdir plane " + ItV(d) + " " + rollText, "update plane",
                              "velocity " + ItName(motor) + " " + ItV(velocity), "update " + ItName(motor)}));
        // (A stale bound: made again for the box.)
        plane->m_frameFlags &= ~0x100u;
        reset();
        a->Update(20);
        AA(r.airFly, !g_itLog.empty() && g_itLog.back() == "bound plane");
        // (The box each update: its centre the bound's, the radius as w, the inverse matrix, moved.)
        {
            a->m_volume.center = S_vector(9.0f, 9.0f, 9.0f);
            a->m_volume.centerW = 9.0f;
            std::memset(&a->m_invMatrix, 0, sizeof a->m_invMatrix);
            reset();
            a->Update(20);
            const I3D_bound_volume &bv = plane->GetBound();
            S_matrix inv;
            inv.Inverse(plane->GetMatrix());
            bool moved = false;
            for (const auto &v : g_volumeLog)
                moved = moved || (v.op == 'M' && v.volume == &a->m_volume);
            AA(r.airFly, a->m_volume.center.x == bv.center.x && a->m_volume.center.y == bv.center.y &&
                             a->m_volume.center.z == bv.center.z && a->m_volume.centerW == bv.radius &&
                             SaSame(&a->m_invMatrix, &inv, sizeof inv) && moved && bv.radius != 9.0f);
        }
        // Banked to the rate (not past it), back to level without a turn, a roll left alone; past half a
        // turn the roll comes round.
        a->m_roll = 0.749f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 0.75f);
        a->m_roll = 0.8f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 0.8f - 20.0f * 0.0005f);
        a->m_roll = 0.76f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 0.75f);
        a->m_idle = false;
        a->m_state = C_AirAttack::AIR_ATTACK_PASSING;
        a->m_target = nullptr;
        a->m_roll = 0.004f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 0.0f && a->m_turnRate == 0.0f);
        a->m_roll = -0.5f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 20.0f * 0.0005f + -0.5f);
        a->m_roll = -0.004f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 0.0f);
        a->m_rolling = true;
        a->m_roll = 0.3f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 0.3f);
        a->m_idle = true;
        a->m_roll = 0.3f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 0.3f);
        a->m_idle = false;
        a->m_rolling = false;
        a->m_roll = 3.2f;
        a->Update(20);
        AA(r.airFly, a->m_roll == (3.2f - 20.0f * 0.0005f) - 6.28318548f);
        a->m_roll = 3.0f;
        a->Update(20);
        AA(r.airFly, a->m_roll == 3.0f - 20.0f * 0.0005f);
    }

    // --- the ground and what is ahead: no ground, level; low (under the ceiling, or the target in sight):
    // climbing by the slope; something ahead within the clearance too; nothing near: back to level ------
    {
        a->m_idle = false;
        a->m_state = C_AirAttack::AIR_ATTACK_PASSING;
        a->m_target = nullptr;
        a->m_roll = 0.0f;
        a->m_rolling = false;
        a->m_climbRate = 0.0f;
        g_aaPlanes.clear();
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.3f, 0.8f));
        a->Update(20);
        AA(r.airClimb, a->m_direction.y == 0.0f && a->m_climbRate == 0.0f);
        // Low over a slope (its normal y 0.8): 2 / 0.8 a second; then the direction up by it.
        g_aaPlanes = {{S_vector(0, 1, 0), 40.0f, &ground, S_vector(0.0f, 0.8f, 0.6f)}};
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->m_climbRate = 0.5f;
        a->Update(20);
        S_vector d(0.6f, 0.0f, 0.8f);
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        // (SetTurn(0, 0) first each update: the rate starts from 0.)
        const float climb = 2.0f / 0.8f + 0.0f;
        d.y = 20.0f * climb * 0.001f + d.y;
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        AA(r.airClimb, a->m_climbRate == climb && a->m_direction.y == d.y && a->m_direction.x == d.x);
        // (A flat ground: 1 a second; above the ceiling and not in sight: none; just over the clearance: none.)
        g_aaPlanes = {{S_vector(0, 1, 0), 40.0f, &ground, S_vector(0.0f, 0.0f, 1.0f)}};
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->m_climbRate = 0.0f;
        a->Update(20);
        AA(r.airClimb, a->m_climbRate == 1.0f);
        g_aaPlanes = {{S_vector(0, 1, 0), 40.0f, &ground, S_vector(0, 1, 0)}};
        a->m_ceiling = 19.0f;
        a->m_climbRate = 0.0f;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airClimb, a->m_climbRate == 0.0f);
        a->m_ceiling = 20.0f;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airClimb, a->m_climbRate == 2.0f / 1.0f);
        a->m_climbRate = 0.0f;
        a->m_clearance = 20.0f;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airClimb, a->m_climbRate == 0.0f);
        a->m_clearance = 30.0f;
        a->m_ceiling = 100.0f;
        // A wall ahead within the clearance (its normal y 0.25): 8 a second.
        g_aaPlanes = {{S_vector(1, 0, 0), px + 10.0f, &wall, S_vector(-0.9f, 0.25f, 0.0f)}};
        setPlane(S_vector(px, 60.0f, pz), S_vector(1.0f, 0.0f, 0.0f));
        a->m_climbRate = 0.0f;
        a->Update(20);
        AA(r.airClimb, a->m_climbRate == 2.0f / 0.25f && g_aaRays.size() >= 2);
        // (A wall straight up: 1 a second; its normal y 0.5: 4.)
        for (const float ny : {0.0f, 0.5f}) {
            g_aaPlanes = {{S_vector(1, 0, 0), px + 10.0f, &wall, S_vector(-1.0f, ny, 0.0f)}};
            setPlane(S_vector(px, 60.0f, pz), S_vector(1.0f, 0.0f, 0.0f));
            a->m_climbRate = 0.0f;
            a->Update(20);
            AA(r.airClimb, a->m_climbRate == (ny == 0.0f ? 1.0f : 4.0f));
        }
        // (The wall's distance exactly 1 m with 1.25 clear; exactly 0 with 0.3: near.)
        for (const float clear : {1.25f, 0.3f}) {
            g_aaPlanes = {{S_vector(1, 0, 0), clear == 1.25f ? 1.0f : 0.0f, &wall, S_vector(-1.0f, 0.5f, 0.0f)}};
            setPlane(S_vector(0.0f, 60.0f, 0.0f), S_vector(1.0f, 0.0f, 0.0f));
            a->m_clearance = clear;
            a->m_climbRate = 0.0f;
            reset();
            a->Update(20);
            AA(r.airClimb, a->m_climbRate == 4.0f && g_aaRays.size() == 2 && g_aaRays[1].dir.x == clear);
        }
        a->m_clearance = 30.0f;
        // Low and climbing already (y 0.2): levelled, the slope's climb added, the move up by it.
        {
            g_aaPlanes = {{S_vector(0, 1, 0), 40.0f, &ground, S_vector(0.0f, 0.8f, 0.6f)}};
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.2f, 0.8f));
            a->m_climbRate = 0.0f;
            a->Update(20);
            S_vector c(0.6f, 0.2f, 0.8f);
            c.y = c.y - (20.0f * 0.0003f) / (1.0f - c.y);
            PrNormalize(c, (c.x * c.x + c.y * c.y) + c.z * c.z);
            const float climb2 = 2.0f / 0.8f + 0.0f;
            c.y = 20.0f * climb2 * 0.001f + c.y;
            PrNormalize(c, (c.x * c.x + c.y * c.y) + c.z * c.z);
            const float step2 = 20.0f * a->m_speed * 0.001f;
            AA(r.airClimb, a->m_climbRate == climb2 && a->m_direction.y == c.y && a->m_direction.x == c.x &&
                               a->m_position.y == step2 * c.y + 60.0f);
        }
        // (Exactly the clearance away: not near; level again at 0.001 instead.)
        g_aaPlanes = {{S_vector(1, 0, 0), px + 30.0f, &wall, S_vector(-1.0f, 0.0f, 0.0f)}};
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.8f, 0.6f, 0.0f));
        a->m_climbRate = 0.0f;
        a->Update(20);
        S_vector e(0.8f, 0.6f, 0.0f);
        PrNormalize(e, (e.x * e.x + e.y * e.y) + e.z * e.z);
        g_aaPlanes.clear();
        // (No ground below: level at once, the ahead test meets nothing.)
        AA(r.airClimb, a->m_climbRate == 0.0f && a->m_direction.y == 0.0f);
        g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)},
                      {S_vector(1, 0, 0), px + 30.0f, &wall, S_vector(-1.0f, 0.0f, 0.0f)}};
        const auto levelling = [&](float y, float rate, bool upRateAhead) {
            const float yy = y;
            float expect;
            if (yy > 0.0f) {
                expect = yy - (20.0f * (upRateAhead ? 0.001f : rate)) / (1.0f - yy);
                if (expect < 0.0f)
                    expect = 0.0f;
            } else {
                expect = (20.0f * 0.0015f) / (yy - 1.0f) + yy;
                if (expect > 0.0f)
                    expect = 0.0f;
            }
            return expect;
        };
        // Ahead exactly 30 m (the wall): up 0.001 a ms; down as always.
        S_vector dirs[3] = {S_vector(0.6f, 0.0f, 0.8f), S_vector(0.6f, 0.0f, 0.8f), S_vector(0.6f, 0.0f, 0.8f)};
        (void)dirs;
        setPlane(S_vector(px, 60.0f, pz), S_vector(1.0f, 0.0f, 0.0f));
        a->m_direction.y = 0.0f;
        a->m_climbRate = 0.0f;
        a->Update(20);
        AA(r.airClimb, a->m_direction.y == 0.0f && a->m_climbRate == 0.0f);
        // (The levelling itself: one step from a climb and from a dive, ahead met at its end or not.)
        // (And a slight climb levelled past 0: 0.)
        for (int k = 0; k < 6; ++k) {
            const bool ahead = k >= 2 && k != 4;
            const float y0 = k >= 4 ? 0.001f : k % 2 == 0 ? 0.2f : -0.2f;
            g_aaPlanes[1] = {S_vector(0, 0, 1), pz + 30.0f, &wall, S_vector(0.0f, 0.0f, -1.0f), ahead};
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.0f, y0, 1.0f));
            a->m_climbRate = 0.0f;
            a->Update(20);
            S_vector e2(0.0f, levelling(y0, 0.0003f, ahead), 1.0f);
            PrNormalize(e2, (e2.x * e2.x + e2.y * e2.y) + e2.z * e2.z);
            AA(r.airClimb, a->m_direction.y == e2.y && a->m_climbRate == 0.0f);
        }
        g_aaPlanes.resize(1);
    }
    a->m_clearance = 30.0f;

    // --- the states: leaving to 400 m (rolling); approaching (turning, seeing the target, firing); over
    // it to 80 m past; shot down: gone at 500 m --------------------------------------------------------
    {
        a->m_idle = false;
        a->m_target = target;
        a->m_rolling = false;
        a->m_motorSound = a->m_motorSounds[0];
        // Leaving, 400 m out (x and z): approaching, 35 m clear, another motor at a random pitch.
        target->m_localMatrix(3) = S_vector(px + 240.0f, 0.0f, pz + 320.0f);
        target->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        a->m_state = C_AirAttack::AIR_ATTACK_LEAVING;
        a->m_rolling = true;
        a->m_roll = 0.0f;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        std::srand(3);
        reset();
        a->Update(20);
        AA(r.airStates, a->m_state == C_AirAttack::AIR_ATTACK_LEAVING && a->m_roll == 20.0f * 0.003f + 0.0f);
        target->m_localMatrix(3) = S_vector(px + 240.01f, 0.0f, pz + 320.0f);
        target->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        // (A seed choosing another motor than the first.)
        int motorSeed = 3;
        for (;; ++motorSeed) {
            std::srand(motorSeed);
            if ((std::rand() & 3) != 0)
                break;
        }
        a->m_ceiling = 40.0f;
        std::srand(motorSeed);
        reset();
        a->Update(20);
        std::srand(motorSeed);
        I3D_sound *next = a->m_motorSounds[std::rand() & 3];
        AA(r.airStates, a->m_state == C_AirAttack::AIR_ATTACK_APPROACHING && a->m_clearance == 35.0f &&
                            a->m_ceiling == 100.0f && !a->m_rolling && a->m_motorSound == next &&
                            next != a->m_motorSounds[0] && AaSoundCalls(a->m_motorSounds[0]) == "o" &&
                            AaSoundCalls(next) == "OF");
        // (The old motor off, the new one on; level, unturned; the victory roll's 0.06 eased by 0.01.)
        S_vector d(0.6f, 0.0f, 0.8f);
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        const float step = 20.0f * 100.0f * 0.001f;
        const S_vector pos(step * d.x + px, step * d.y + 60.0f, step * d.z + pz);
        const float roll = (20.0f * 0.003f + 20.0f * 0.003f) - 20.0f * 0.0005f;
        char rollText[32];
        std::snprintf(rollText, sizeof rollText, "%.9g", roll);
        const S_vector velocity((100.0f * d.x) * 0.5f, (d.y * 100.0f) * 0.5f, (d.z * 100.0f) * 0.5f);
        AA(r.airStates, a->m_roll == roll &&
                            SaLogIs({"update bluur", "update " + ItName(a->m_motorSounds[0]), "random 0.5", "update " + ItName(next),
                                     "setpos plane " + ItV(pos), "setdir plane " + ItV(d) + " " + rollText, "update plane",
                                     "velocity " + ItName(next) + " " + ItV(velocity), "update " + ItName(next)}));
    }

    // --- approaching: turning towards the target (0.4 rad/s either way), the direction normalised; in sight
    // when the first volume on the way (past the borders) is the target's box; over it at 50 m -----------
    const auto placeTarget = [&](const S_vector &at) {
        target->m_localMatrix(3) = at;
        target->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
    };
    // (The target's box: a plane across the line from the plane, half a metre short of the target.)
    const auto boxAcross = [&](const S_vector &from, const S_vector &to) {
        S_vector n = to - from;
        n = n * (1.0f / std::sqrt(n.Dot(n)));
        return S_AaPlane{n, n.Dot(to) - 0.5f, &targetBox, S_vector(-n.x, -n.y, -n.z)};
    };
    {
        a->m_idle = false;
        a->m_target = target;
        a->m_state = C_AirAttack::AIR_ATTACK_APPROACHING;
        a->m_rolling = false;
        a->m_roll = 0.0f;
        a->m_clearance = 35.0f;
        a->m_ceiling = 100.0f;
        a->m_fireTime = 0;
        placeTarget(S_vector(px - 100.0f, 60.0f, pz + 200.0f));
        g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
        setPlane(S_vector(px, 60.0f, pz), S_vector(1.2f, 0.0f, 1.6f));
        reset();
        a->Update(20);
        const float heading[2] = {1.2f, 1.6f}, toward[2] = {-100.0f, 200.0f};
        const float angle = AngleBetween2D(heading, toward);
        const float rate = angle > 0.0f ? 0.4f : -0.4f;
        S_vector d(1.2f, 0.0f, 1.6f);
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        d = d.RotateByMatrix(RotateYMatrix(20.0f * rate * 0.001f));
        PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
        AA(r.airStates, a->m_state == C_AirAttack::AIR_ATTACK_APPROACHING && a->m_turnRate == rate && rate > 0.0f &&
                            !a->m_targetInSight && a->m_direction.x == d.x && a->m_direction.z == d.z &&
                            a->m_roll == 20.0f * 0.0005f && g_aaRays.size() == 3 && g_aaRays[0].types == 0 &&
                            g_aaRays[0].flags == 0 && g_aaRays[0].dir.x == (px - 100.0f) - px && g_aaRays[0].dir.y == 0.0f);
        // (To the right: -0.4.)
        placeTarget(S_vector(px + 200.0f, 60.0f, pz - 100.0f));
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airStates, a->m_turnRate == -0.4f);
        // In sight (its box first on the line): a flash (the burst starts), none shot beyond 200 m.
        placeTarget(S_vector(px - 100.0f, 60.0f, pz + 200.0f));
        g_aaPlanes.push_back(boxAcross(S_vector(px, 60.0f, pz), S_vector(px - 100.0f, 60.0f, pz + 200.0f)));
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        for (C_AirAttack::S_Flash *f : a->m_flashes)
            f->time = -1;
        std::srand(21);
        reset();
        a->Update(20);
        std::srand(21);
        C_AirAttack::S_Flash *flash = a->m_flashes[static_cast<uint32_t>(std::rand()) % 5];
        AA(r.airStates, a->m_targetInSight && a->m_bullets.empty() && a->m_fireTime == 5 && flash->time == 100 - 20 &&
                            flash->frame->IsOn() && std::count(g_itLog.begin(), g_itLog.end(), "on " + ItName(flash->frame) + " 1") == 1 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "update " + ItName(flash->frame)) == 1);
        // (Straight ahead: -0.4; a little to either side: towards it.)
        {
            int small = 0;
            for (const float side : {0.0f, 10.0f, -10.0f}) {
                placeTarget(S_vector(px + side, 60.0f, pz + 200.0f));
                g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
                setPlane(S_vector(px, 60.0f, pz), S_vector(0.0f, 0.0f, 1.0f));
                a->Update(20);
                const float h[2] = {0.0f, 1.0f}, to[2] = {(px + side) - px, 200.0f};
                const float angle2 = AngleBetween2D(h, to);
                small += angle2 > 0.0f && angle2 < 0.5f;
                AA(r.airStates, a->m_turnRate == (angle2 > 0.0f ? 0.4f : -0.4f) && (side != 0.0f || angle2 == 0.0f));
            }
            AA(r.airStates, small == 1);
            placeTarget(S_vector(px - 100.0f, 60.0f, pz + 200.0f));
            g_aaPlanes.push_back(boxAcross(S_vector(px, 60.0f, pz), S_vector(px - 100.0f, 60.0f, pz + 200.0f)));
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        }
        // (Another frame's box first: not in sight; a static border on the way: seen past it.)
        targetBox.frame = plane;
        a->Update(20);
        AA(r.airStates, !a->m_targetInSight);
        targetBox.frame = target;
        targetBox.flags = 0;
        a->Update(20);
        AA(r.airStates, !a->m_targetInSight);
        targetBox.flags = COLLISION_SHAPE_BOX;
        g_aaPlanes.push_back({S_vector(1, 0, 0), px - 50.0f, &border, S_vector(1, 0, 0)});
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airStates, a->m_targetInSight);
        g_aaPlanes.resize(1);
        // Over it (under 50 m, x and z): passing, 25 m clear up to 40 m; exactly 50 m: not yet.
        placeTarget(S_vector(px + 50.0f, 60.0f, pz));
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airStates, a->m_state == C_AirAttack::AIR_ATTACK_APPROACHING);
        placeTarget(S_vector(px + 30.0f, 0.0f, pz + 39.99f));
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airStates, a->m_state == C_AirAttack::AIR_ATTACK_PASSING && a->m_clearance == 25.0f && a->m_ceiling == 40.0f);
        // Passing: 80 m past, leaving (now and then rolling); exactly 80: not yet.
        placeTarget(S_vector(px + 80.0f, 60.0f, pz));
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airStates, a->m_state == C_AirAttack::AIR_ATTACK_PASSING && !a->m_targetInSight);
        int rolls = 0, straight = 0;
        for (int s2 = 1; s2 < 12; ++s2) {
            placeTarget(S_vector(px + 80.01f, 60.0f, pz));
            a->m_state = C_AirAttack::AIR_ATTACK_PASSING;
            a->m_rolling = false;
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
            std::srand(s2);
            a->Update(20);
            std::srand(s2);
            const bool roll = (std::rand() & 3) == 0;
            AA(r.airStates, a->m_state == C_AirAttack::AIR_ATTACK_LEAVING && a->m_rolling == roll);
            (roll ? rolls : straight)++;
        }
        AA(r.airStates, rolls > 0 && straight > 0);
        // Shot down past 500 m: gone (the smoke finished, the dust removed); exactly 500: not yet.
        a->m_rolling = false;
        a->m_state = C_AirAttack::AIR_ATTACK_SHOT_DOWN;
        a->m_smoke = 44;
        placeTarget(S_vector(px + 400.0f, 60.0f, pz + 300.0f));
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airStates, !a->m_gone && a->m_smoke == 44);
        a->AddDust({1.0f, 2.0f, 3.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f});
        placeTarget(S_vector(px + 400.01f, 60.0f, pz + 300.0f));
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        reset();
        a->Update(20);
        AA(r.airStates, a->m_gone && a->m_smoke == -1 && a->m_dusts.empty() &&
                            std::count(g_itLog.begin(), g_itLog.end(), "finish 44") == 1 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "release new") == 1);
        // (Gone with no smoke: none finished; the dust left (over sand) all removed.)
        a->m_gone = false;
        a->m_smoke = -1;
        ground.flags = static_cast<uint32_t>(42) << 24;
        a->AddDust({1.0f, 2.0f, 3.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f});
        a->AddDust({4.0f, 2.0f, 3.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f});
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        reset();
        a->Update(20);
        ground.flags = static_cast<uint32_t>(1) << 24;
        AA(r.airStates, a->m_gone && a->m_smoke == -1 && a->m_dusts.empty() && g_particlesDestroyed.empty() &&
                            std::count(g_itLog.begin(), g_itLog.end(), "finish -1") == 0 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "release new") == 2);
        a->m_gone = false;
        // In sight only on the approach: cleared idle, leaving, passing and shot down.
        a->m_target = nullptr;
        for (int k = 0; k < 4; ++k) {
            a->m_idle = k == 0;
            a->m_state = k == 0 ? C_AirAttack::AIR_ATTACK_APPROACHING : k == 1 ? C_AirAttack::AIR_ATTACK_LEAVING
                         : k == 2 ? C_AirAttack::AIR_ATTACK_PASSING : C_AirAttack::AIR_ATTACK_SHOT_DOWN;
            a->m_targetInSight = true;
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
            a->Update(20);
            AA(r.airStates, !a->m_targetInSight && !a->m_gone);
        }
        a->m_idle = false;
        a->m_target = target;
        a->m_state = C_AirAttack::AIR_ATTACK_APPROACHING;
    }

    // --- in sight: the target's height approached (0.001 a ms, not past it); the bursts: a shot each 25 ms
    // (a flash), aimed short of the target and to a side, the ray 1.2 times as long at the scene only; on
    // sand a bullet, landing in twice the miss distance (ms) -----------------------------------------------
    {
        a->m_clearance = 35.0f;
        a->m_ceiling = 100.0f;
        // Above the plane: climbing to it (the target 20 m up, 30 m away: wanted y 0.555).
        // (Then reaching it: 4 m up; climbing to it from 0.1; low (ground 20 m below): climbing 2 a second
        // too, the direction normalised before the height.)
        for (int k = 0; k < 6; ++k) {
            const float tys[6] = {80.0f, 40.0f, 60.0f, 64.0f, 100.0f, 60.0f};
            const float y0s[6] = {0.0f, 0.0f, 0.1f, 0.0f, 0.1f, 0.0f};
            const float ty = tys[k], y0 = y0s[k];
            const float groundLevel = k == 5 ? 40.0f : 0.0f;
            placeTarget(S_vector(px + 30.0f, ty, pz + 400.0f));
            g_aaPlanes = {{S_vector(0, 1, 0), groundLevel, &ground, S_vector(0, 1, 0)},
                          boxAcross(S_vector(px, 60.0f, pz), S_vector(px + 30.0f, ty, pz + 400.0f))};
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.0f, y0, 1.0f));
            a->m_fireTime = 1000;
            a->Update(20);
            // (The oracle: the turn (approaching), then the height.)
            const float heading[2] = {0.0f, 1.0f}, toward[2] = {30.0f, 400.0f};
            const float rate = AngleBetween2D(heading, toward) > 0.0f ? 0.4f : -0.4f;
            S_vector d(0.0f, y0, 1.0f);
            PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
            if (y0 != 0.0f) {
                // (No ground near ahead: back to level first.)
                d.y = d.y - (20.0f * 0.0003f) / (1.0f - d.y);
                if (d.y < 0.0f)
                    d.y = 0.0f;
            }
            PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
            d = d.RotateByMatrix(RotateYMatrix(20.0f * rate * 0.001f));
            if (groundLevel != 0.0f)
                d.y = 20.0f * (2.0f / 1.0f + 0.0f) * 0.001f + d.y;
            PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
            S_vector to(30.0f, ty - 60.0f, 400.0f);
            PrNormalize(to, (to.y * to.y + to.x * to.x) + to.z * to.z);
            if (to.y < d.y)
                d.y = std::max(to.y, d.y - 20.0f * 0.001f);
            else if (to.y > d.y)
                d.y = std::min(to.y, 20.0f * 0.001f + d.y);
            PrNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
            AA(r.airFire, a->m_targetInSight && a->m_direction.y == d.y && a->m_direction.x == d.x && a->m_fireTime == 980);
        }
        // A shot: the target 100 m away on sand, 60 m down.
        static S_CollisionVolume sand;
        sand = MakeAaVolume(42);
        S_vector at(px + 60.0f, 0.0f, pz + 80.0f);
        placeTarget(at);
        g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &sand, S_vector(0, 1, 0)}, boxAcross(S_vector(px, 60.0f, pz), at)};
        a->m_rolling = false;
        const auto shotOracle = [&](const S_vector &pos, uint32_t &random, S_vector &shot, S_vector &hit, int &time) {
            const auto next = [&](float max) {
                random = random * 1103515245u + 12345u;
                return static_cast<float>((random >> 8) & 0xFFFF) / 65536.0f * max;
            };
            S_vector d(at.x - pos.x, at.y - pos.y, at.z - pos.z);
            const float distance = PrLength((d.x * d.x + d.z * d.z) + d.y * d.y);
            PrNormalize(d, (d.x * d.x + d.z * d.z) + d.y * d.y);
            S_vector up(d.x, 1000.0f, d.z);
            PrNormalize(up, (up.z * up.z + up.x * up.x) + 1e+06f);
            S_vector side(up.z * d.y - d.z * up.y, d.z * up.x - up.z * d.x, d.x * up.y - up.x * d.y);
            PrNormalize(side, (side.x * side.x + side.y * side.y) + side.z * side.z);
            S_vector flat(d.x, 0.0f, d.z);
            PrNormalize(flat, flat.x * flat.x + flat.z * flat.z);
            const float k = next(50.0f) + (distance - 125.0f);
            const S_vector aim(at.x - (flat.x * k) * 0.15f, at.y - (k * flat.y) * 0.15f, at.z - (flat.z * k) * 0.15f);
            const float j = next(6.0f) - 3.0f;
            shot = S_vector(((aim.x + side.x * j) - pos.x) * 1.2f, ((aim.y + side.y * j) - pos.y) * 1.2f,
                            ((aim.z + side.z * j) - pos.z) * 1.2f);
            const float t = (0.0f - pos.y) / shot.y;
            hit = pos + shot * t;
            const float mx = hit.x - at.x, my = hit.y - at.y, mz = hit.z - at.z;
            const float miss = PrLength((mx * mx + mz * mz) + my * my);
            time = static_cast<int>(miss + miss);
        };
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, -0.8f, 0.0f));
        a->m_fireTime = 0;
        for (C_AirAttack::S_Bullet *b : a->m_bullets)
            delete b;
        a->m_bullets.clear();
        for (C_AirAttack::S_Flash *f : a->m_flashes)
            f->time = -1;
        uint32_t random = g_waRandom;
        std::srand(31);
        reset();
        a->Update(60);                                  // (three shots: -60, -35, -10, then 15)
        std::srand(31);
        const S_vector from(px, 60.0f, pz);
        // (Each shot: landed already when its time is under 61 ms (a splash where it struck, a ray along it),
        // else still on its way.)
        bool shotsOk = a->m_fireTime == 15;
        int flashes = 0, landed = 0;
        for (int k = 0; k < 3 && shotsOk; ++k) {
            flashes += a->m_flashes[static_cast<uint32_t>(std::rand()) % 5]->time == 100 - 60;
            S_vector shot, hit;
            int time;
            shotOracle(from, random, shot, hit, time);
            if (time - 60 < 1) {
                bool found = false;
                for (size_t q = 0; q < g_rbSplashes.size() && !found; ++q)
                    found = RbSame(g_rbSplashes[q].pos, hit) && g_rbSplashes[q].effect == 4;
                bool ray = false;
                for (const S_WaRay &w : g_waRays)
                    ray = ray || (w.dir.x == shot.x && w.dir.y == shot.y && w.dir.z == shot.z);
                shotsOk = found && ray;
                ++landed;
            } else {
                bool found = false;
                for (const C_AirAttack::S_Bullet *b : a->m_bullets)
                    found = found || (b->time == time - 60 && b->shot.x == shot.x && RbSame(AaVec(b->pos), hit));
                shotsOk = found;
            }
        }
        shotsOk = shotsOk && static_cast<int>(a->m_bullets.size()) == 3 - landed;
        AA(r.airFire, shotsOk && flashes >= 1);
        // (The rays: the sight's, the ground's, ahead, then the shots': at the scene only, flags 0x820000.)
        int fireRays = 0;
        for (const S_AaRay &ray : g_aaRays)
            fireRays += ray.types == 0xFFFFFFFF && ray.flags == 0x820000 && ray.from.x == px && ray.from.y == 60.0f;
        AA(r.airFire, fireRays == 3);
        // Not on sand: no bullet; beyond 200 m: no shot (a flash all the same); exactly 200 m: none either.
        sand.flags = static_cast<uint32_t>(41) << 24;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, -0.8f, 0.0f));
        a->m_bullets.clear();
        a->m_fireTime = 0;
        a->Update(20);
        AA(r.airFire, a->m_bullets.empty() && a->m_fireTime == 5);
        sand.flags = static_cast<uint32_t>(42) << 24;
        // (A target at exactly 200 m: no shot; a little nearer: one.)
        for (int k = 0; k < 2; ++k) {
            const S_vector distant(px + 120.0f, 60.0f - (k == 0 ? 160.0f : 159.9f), pz);
            placeTarget(distant);
            g_aaPlanes = {{S_vector(0, 1, 0), -200.0f, &sand, S_vector(0, 1, 0)}, boxAcross(S_vector(px, 60.0f, pz), distant)};
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, -0.8f, 0.0f));
            a->m_bullets.clear();
            a->m_fireTime = 0;
            reset();
            a->Update(20);
            int shots = 0;
            for (const S_AaRay &ray : g_aaRays)
                shots += ray.flags == 0x820000;
            AA(r.airFire, shots == k);
        }
        for (C_AirAttack::S_Bullet *b : a->m_bullets)
            delete b;
        a->m_bullets.clear();
        // Without flashes: no flash (Fix), the shot all the same.
        placeTarget(at);
        g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &sand, S_vector(0, 1, 0)}, boxAcross(S_vector(px, 60.0f, pz), at)};
        std::vector<C_AirAttack::S_Flash *> flashesKept;
        flashesKept.swap(a->m_flashes);
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, -0.8f, 0.0f));
        a->m_fireTime = 0;
        reset();
        a->Update(20);
        AA(r.airFire, a->m_bullets.size() + g_rbSplashes.size() == 1);
        flashesKept.swap(a->m_flashes);
        for (C_AirAttack::S_Bullet *b : a->m_bullets)
            delete b;
        a->m_bullets.clear();
        // (Exactly due (0 left): no shot yet.)
        for (C_AirAttack::S_Flash *f : a->m_flashes)
            f->time = -1;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, -0.8f, 0.0f));
        a->m_fireTime = 20;
        reset();
        a->Update(20);
        int dueShots = 0;
        for (const S_AaRay &ray : g_aaRays)
            dueShots += ray.flags == 0x820000;
        AA(r.airFire, a->m_fireTime == 0 && dueShots == 0 && a->m_flashes[0]->time == -1 && a->m_flashes[4]->time == -1);
        // One shot in 1 ms: its ray, the bullet's place and time (twice the miss, less the 1 ms).
        for (int k = 0; k < 4; ++k) {
            const S_vector at2(px + (k < 2 ? 60.0f : -150.0f), 0.0f, pz + (k % 2 == 0 ? 80.0f : -40.0f));
            placeTarget(at2);
            g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &sand, S_vector(0, 1, 0)}, boxAcross(S_vector(px, 60.0f, pz), at2)};
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, -0.8f, 0.0f));
            a->m_fireTime = 0;
            uint32_t random1 = g_waRandom;
            reset();
            a->Update(1);
            S_vector shot, hit;
            int time;
            const S_vector keep = at;
            at = at2;
            shotOracle(from, random1, shot, hit, time);
            at = keep;
            bool shotOk = false;
            for (const S_AaRay &ray : g_aaRays)
                shotOk = shotOk || (ray.flags == 0x820000 && ray.dir.x == shot.x && ray.dir.y == shot.y && ray.dir.z == shot.z);
            if (time - 1 >= 1)
                shotOk = shotOk && a->m_bullets.size() == 1 && a->m_bullets[0]->time == time - 1 &&
                         a->m_bullets[0]->shot.x == shot.x && a->m_bullets[0]->shot.z == shot.z && RbSame(AaVec(a->m_bullets[0]->pos), hit);
            if (!shotOk)
                PrShow("aashot", k, time);
            AA(r.airFire, shotOk && a->m_fireTime == 24);
            for (C_AirAttack::S_Bullet *b : a->m_bullets)
                delete b;
            a->m_bullets.clear();
        }
        placeTarget(at);
    }

    // --- the bullets landing: a particle, a human on the line hit (the shot's damage), a gun sound if none,
    // sometimes an impact (a pooled sound) and a whizz near the camera; the flashes off after 100 ms -------
    C_Human *victim = new C_Human();
    {
        a->m_state = C_AirAttack::AIR_ATTACK_PASSING;
        a->m_idle = false;
        a->m_target = nullptr;
        a->m_impactSounds.clear();
        a->m_whizzSounds.clear();
        g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
        for (C_AirAttack::S_Bullet *b : a->m_bullets)
            delete b;
        a->m_bullets.clear();
        auto *b = new C_AirAttack::S_Bullet;
        b->pos = {px + 5.0f, 0.5f, pz + 6.0f, 1.0f};
        b->shot = {12.0f, -50.0f, 16.0f, 0.0f};
        b->time = 20;
        a->m_bullets.push_back(b);
        a->m_gunSound = nullptr;
        a->m_damage = 33.0f;
        g_waWall = S_CollisionVolume();
        g_waWall.actor = victim;
        g_waHitPos = S_vector(1.0f, 2.0f, 3.0f);
        g_waHitNormal = S_vector(0.0f, 0.0f, 1.0f);
        g_waBlockAt = 0;
        g_aaCameraPos = {px + 8.0f, 2.0f, pz + 8.0f, 1.0f};
        // (A seed whose impact and whizz both come.)
        int seed2 = 1;
        for (;; ++seed2) {
            std::srand(seed2);
            std::rand();
            if ((std::rand() & 3) != 0)
                continue;
            std::rand();
            if ((std::rand() & 3) == 0)
                break;
        }
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        const size_t hits = victim->hits.size();
        const int ignores = g_ignores, clears = g_ignoreClears;
        uint32_t random = g_waRandom;
        g_maSoundsMade = 100;
        std::srand(seed2);
        reset();
        void *const oldLoop = g_soundVtable[0x90 / 4];
        g_soundVtable[0x90 / 4] = reinterpret_cast<void *>(&F_AaSoundLoop);
        a->Update(20);
        g_soundVtable[0x90 / 4] = oldLoop;
        std::srand(seed2);
        I3D_sound *gun = a->m_gunSounds[std::rand() % 5];
        std::rand();
        const int impactFile = std::rand() % 5 + 1;
        std::rand();
        const int whizzFile = std::rand() % 5 + 1;
        const auto next = [&](float max) {
            random = random * 1103515245u + 12345u;
            return static_cast<float>((random >> 8) & 0xFFFF) / 65536.0f * max;
        };
        const float pitch = 1.5f - next(1.0f);
        const float ra = next(4.0f) - 2.0f, rb = next(4.0f) - 2.0f, rc = next(4.0f) - 2.0f;
        AA(r.airBullets, a->m_bullets.empty() && g_rbSplashes.size() == 1 && g_rbSplashes[0].effect == 4 &&
                             RbSame(g_rbSplashes[0].pos, S_vector(px + 5.0f, 0.5f, pz + 6.0f)) &&
                             RbSame(g_rbSplashes[0].dir, S_vector(0, 1, 0)) && !g_rbSplashes[0].parent &&
                             g_rbSplashes[0].faceCamera && !g_rbSplashes[0].transient);
        AA(r.airBullets, g_waRays.size() == 1 && RbSame(g_waRays[0].from, S_vector(px + 5.0f - 12.0f, 0.5f + 50.0f, pz + 6.0f - 16.0f)) &&
                             RbSame(g_waRays[0].dir, S_vector(12.0f, -50.0f, 16.0f)) && g_waRays[0].types == 0 &&
                             g_waRays[0].flags == 0 && g_ignores == ignores + 1 && g_ignoreClears == clears + 1 && g_ignoreArg == 0 &&
                             victim->hits.size() == hits + 1 && victim->hits.back().type == HIT_BULLET &&
                             victim->hits.back().amount == 33.0f && RbSame(victim->hits.back().pos, S_vector(1, 2, 3)) &&
                             RbSame(victim->hits.back().dir, S_vector(0, 0, 1)) && !victim->hits.back().attacker &&
                             victim->hits.back().flags == 0);
        AA(r.airBullets, a->m_gunSound == gun && g_sounds[gun].volume == 1.0f &&
                             a->m_impactSounds.size() == 1 && a->m_whizzSounds.size() == 1);
        if (a->m_impactSounds.empty() || a->m_whizzSounds.empty()) {
            std::fprintf(stderr, "air: no impact / whizz sound\n");
            std::exit(4);
        }
        const I3D_frame *impact = reinterpret_cast<const I3D_frame *>(a->m_impactSounds[0]);
        const I3D_frame *whizz = reinterpret_cast<const I3D_frame *>(a->m_whizzSounds[0]);
        const S_FakeSound &fi = g_sounds[impact], &fw = g_sounds[whizz];
        char impactName[16], whizzName[16];
        std::snprintf(impactName, sizeof impactName, "sand%02d.wav", impactFile);
        std::snprintf(whizzName, sizeof whizzName, "whizz%d.wav", whizzFile);
        const S_vector whizzAt(rc + g_aaCameraPos.x, rb + g_aaCameraPos.y, ra + g_aaCameraPos.z);
        AA(r.airSounds, fi.file == impactName && fi.type == 1 && !fi.loop && fi.range[0] == 10.0f && fi.range[1] == 30.0f &&
                            fi.range[2] == 0.5f && fi.range[3] == 0.5f && fi.playAt == 0 && fi.frequency == pitch &&
                            impact->IsOn() && impact->m_parent == g_sector && RbSame(impact->GetPos(), S_vector(px + 5.0f, 0.5f, pz + 6.0f)));
        AA(r.airSounds, fw.file == whizzName && fw.range[1] == 30.0f && fw.frequencies == 0 && fw.playAt == 0 && whizz->IsOn() &&
                            RbSame(whizz->GetPos(), whizzAt) && std::count(g_itLog.begin(), g_itLog.end(), "camerapos 2") == 1);
        // (Their calls: on, played, the impact's pitch; under the primary sector, not looping, updated; the
        // gun's on at full volume.)
        const auto logged = [&](const std::string &line) { return std::count(g_itLog.begin(), g_itLog.end(), line); };
        AA(r.airSounds, AaSoundCalls(impact) == "OPF" && AaSoundCalls(whizz) == "OP" && AaSoundCalls(gun) == "OV" &&
                            logged("link " + ItName(impact) + " sector 0") == 1 && logged("link " + ItName(whizz) + " sector 0") == 1 &&
                            logged("loop " + ItName(impact) + " 0") == 1 && logged("loop " + ItName(whizz) + " 0") == 1 &&
                            logged("update " + ItName(impact)) == 1 && logged("update " + ItName(whizz)) == 1 &&
                            g_ignoreClearArg == 0);
        // A pooled sound that has stopped plays again where the next one lands (no new frame).
        g_sounds[impact].ended = true;
        auto *b2 = new C_AirAttack::S_Bullet;
        b2->pos = {px + 1.0f, 0.25f, pz - 2.0f, 1.0f};
        b2->shot = {0.0f, -10.0f, 0.0f, 0.0f};
        b2->time = 20;
        a->m_bullets.push_back(b2);
        g_waBlockAt = -1;
        int seed3 = 1;
        for (;; ++seed3) {
            std::srand(seed3);
            if ((std::rand() & 3) == 0 && (std::rand() & 3) != 0)
                break;
        }
        std::srand(seed3);
        reset();
        void *const oldPlaying = g_soundVtable[0x78 / 4];
        g_soundVtable[0x78 / 4] = reinterpret_cast<void *>(&F_AaSoundPlaying);
        a->Update(20);
        g_soundVtable[0x78 / 4] = oldPlaying;
        AA(r.airSounds, AaSoundCalls(impact) == "PO" &&
                            std::count(g_itLog.begin(), g_itLog.end(), "playing " + ItName(impact) + " 0") == 1);
        AA(r.airSounds, a->m_impactSounds.size() == 1 && g_sounds[impact].playAt == 0 && g_sounds[impact].plays == 2 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "update " + ItName(impact)) == 1);
        AA(r.airSounds, std::count(g_itLog.begin(), g_itLog.end(), "setpos " + ItName(impact) + " " + ItV(S_vector(px + 1.0f, 0.25f, pz - 2.0f))) == 1 &&
                            g_waRays.size() == 1 && victim->hits.size() == hits + 1);
        // A bullet with time left: kept (1 ms is not yet).
        auto *b3 = new C_AirAttack::S_Bullet;
        b3->pos = {0, 0, 0, 1};
        b3->time = 21;
        a->m_bullets.push_back(b3);
        a->Update(20);
        AA(r.airBullets, a->m_bullets.size() == 1 && a->m_bullets[0]->time == 1);
        delete a->m_bullets[0];
        a->m_bullets.clear();
        // The flashes: off once their 100 ms run out (exactly 0 left: still on).
        a->m_flashes[2]->time = 40;
        a->m_flashes[3]->time = 39;
        a->m_flashes[4]->time = -1;
        a->m_flashes[0]->time = 10;
        a->m_flashes[0]->frame->SetOn(true);
        a->m_flashes[2]->frame->SetOn(true);
        a->m_flashes[3]->frame->SetOn(true);
        reset();
        a->Update(20);
        a->Update(20);
        AA(r.airBullets, a->m_flashes[2]->time == 0 && a->m_flashes[2]->frame->IsOn() && a->m_flashes[3]->time == -1 &&
                             !a->m_flashes[3]->frame->IsOn() && a->m_flashes[4]->time == -1 &&
                             std::count(g_itLog.begin(), g_itLog.end(), "update " + ItName(a->m_flashes[3]->frame)) == 1 &&
                             a->m_flashes[0]->time == -10 && !a->m_flashes[0]->frame->IsOn());
        // (The one at exactly 0: off the next update.)
        a->Update(20);
        AA(r.airBullets, a->m_flashes[2]->time == -20 && !a->m_flashes[2]->frame->IsOn());
        // Two bullets, the first landing: the second kept in its place.
        {
            auto *first = new C_AirAttack::S_Bullet;
            first->pos = {px, 0.0f, pz, 1.0f};
            first->shot = {0.0f, -10.0f, 0.0f, 0.0f};
            first->time = 5;
            auto *second = new C_AirAttack::S_Bullet;
            second->pos = {px, 0.0f, pz, 1.0f};
            second->time = 500;
            a->m_bullets = {first, second};
            a->Update(20);
            AA(r.airBullets, a->m_bullets.size() == 1 && a->m_bullets[0] == second && second->time == 480);
            delete second;
            a->m_bullets.clear();
        }
    }

    // --- the guns' sound on the approach (a human target outdoors within 400 m), its velocity; fading out
    // otherwise (0.004 a ms), off at 0 ----------------------------------------------------------------------
    {
        static S_FrameData targetData;
        targetData = {};
        targetData.actor = victim;
        target->m_data = &targetData;
        g_aaActorSector = reinterpret_cast<I3D_sector *>(g_sector);
        a->m_state = C_AirAttack::AIR_ATTACK_APPROACHING;
        a->m_gunSound = nullptr;
        a->m_target = target;
        g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
        placeTarget(S_vector(px + 235.0f, 60.0f, pz + 313.0f));
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        std::srand(41);
        reset();
        a->Update(20);
        std::srand(41);
        I3D_sound *gun = a->m_gunSounds[std::rand() % 5];
        const S_vector v((100.0f * a->m_direction.x) * 0.5f, (a->m_direction.y * 100.0f) * 0.5f,
                         (a->m_direction.z * 100.0f) * 0.5f);
        AA(r.airSounds, a->m_gunSound == gun && g_sounds[gun].volume == 1.0f && reinterpret_cast<I3D_frame *>(gun)->IsOn() &&
                            RbSame(g_aaVelocity[gun], v) && std::count(g_itLog.begin(), g_itLog.end(), "update " + ItName(gun)) == 1 &&
                            AaSoundCalls(gun) == "OV");
        // (Exactly 400 m (standing still at the origin): none.)
        {
            const float speed = a->m_speed;
            a->m_speed = 0.0f;
            a->m_gunSound = nullptr;
            placeTarget(S_vector(240.0f, 60.0f, 320.0f));
            setPlane(S_vector(0.0f, 60.0f, 0.0f), S_vector(0.6f, 0.0f, 0.8f));
            a->Update(20);
            AA(r.airSounds, !a->m_gunSound && plane->GetWorldPos().x == 0.0f);
            placeTarget(S_vector(240.0f, 60.0f, 319.99f));
            setPlane(S_vector(0.0f, 60.0f, 0.0f), S_vector(0.6f, 0.0f, 0.8f));
            a->Update(20);
            AA(r.airSounds, a->m_gunSound != nullptr);
            a->m_speed = speed;
        }
        // (Beyond 400 m, another sector, not a human, idle: none.)
        placeTarget(S_vector(px + 250.0f, 60.0f, pz + 330.0f));
        a->m_gunSound = nullptr;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airSounds, !a->m_gunSound);
        placeTarget(S_vector(px + 30.0f, 60.0f, pz + 300.0f));
        g_aaActorSector = nullptr;
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        a->Update(20);
        AA(r.airSounds, !a->m_gunSound);
        g_aaActorSector = reinterpret_cast<I3D_sector *>(g_sector);
        targetData.actor = a;
        a->Update(20);
        AA(r.airSounds, !a->m_gunSound);
        targetData.actor = victim;
        a->m_idle = true;
        a->Update(20);
        AA(r.airSounds, !a->m_gunSound);
        a->m_idle = false;
        // Fading: 1 - 0.08 a step; at or under 0 off.
        a->m_state = C_AirAttack::AIR_ATTACK_PASSING;
        a->m_gunSound = gun;
        g_sounds[gun].volume = 1.0f;
        std::memcpy(reinterpret_cast<uint8_t *>(gun) + 0x16C, &g_sounds[gun].volume, 4);
        a->Update(20);
        AA(r.airSounds, a->m_gunSound == gun && g_sounds[gun].volume == 1.0f - 20.0f * 0.004f);
        const float half = 0.5f;
        std::memcpy(reinterpret_cast<uint8_t *>(gun) + 0x16C, &half, 4);
        a->Update(20);
        AA(r.airSounds, a->m_gunSound == gun && g_sounds[gun].volume == 0.5f - 20.0f * 0.004f);
        const float edge = 20.0f * 0.004f;
        std::memcpy(reinterpret_cast<uint8_t *>(gun) + 0x16C, &edge, 4);
        reset();
        a->Update(20);
        AA(r.airSounds, !a->m_gunSound && g_sounds[gun].volume == 0.0f && !reinterpret_cast<I3D_frame *>(gun)->IsOn() &&
                            std::count(g_itLog.begin(), g_itLog.end(), "update " + ItName(gun)) == 1);
        target->m_data = nullptr;
    }

    // --- the dust (Tick): within 60 m of the camera (x and z) over sand or a minefield 40 m below, 2 m over
    // it each 3 s; moving on at 37 m/s over sand, 3 s at most ------------------------------------------------
    {
        static S_CollisionVolume desert;
        desert = MakeAaVolume(42);
        g_aaPlanes = {{S_vector(0, 1, 0), 30.0f, &desert, S_vector(0, 1, 0)}};
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        camera->m_localMatrix(3) = S_vector(px + 36.0f, 0.0f, pz + 47.9f);
        camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        a->m_dustTime = 0;
        for (C_AirAttack::S_Dust *dd : a->m_dusts)
            delete dd;
        a->m_dusts.clear();
        reset();
        a->Tick(10);
        AA(r.airDust, a->m_dusts.size() == 1 && a->m_dustTime == 3000 && g_aaRays.size() == 1 &&
                          g_aaRays[0].dir.y == -40.0f && g_aaRays[0].types == 0xFFFFFFFF && g_rbSplashes.size() == 1 &&
                          g_rbSplashes[0].effect == 0x3B && RbSame(g_rbSplashes[0].pos, S_vector(px, 32.0f, pz)) &&
                          RbSame(g_rbSplashes[0].dir, S_vector(0, 0, -1)) && g_rbSplashes[0].transient &&
                          g_rbSplashes[0].faceCamera && a->m_dusts.size() == 1 && g_rbSplashes[0].parent == a->m_dusts[0]->frame);
        C_AirAttack::S_Dust *dust = a->m_dusts[0];
        AA(r.airDust, dust->time == 3000 && RbSame(AaVec(dust->dir), S_vector(0.6f, 0.0f, 0.8f)) &&
                          RbSame(dust->frame->GetPos(), S_vector(px, 32.0f, pz)) && dust->frame->m_parent == g_sector &&
                          SaLogIs({"createframe 6", "link new sector 0", "on new 1", "update new"}));
        // The timer: 1000 left after 2 s; run out: the next one at once.
        a->Tick(2000);
        AA(r.airDust, a->m_dustTime == 1000 && a->m_dusts.size() == 1);
        a->Tick(1001);
        AA(r.airDust, a->m_dustTime == 3000 && a->m_dusts.size() == 2);
        // (Exactly 60 m: none; not sand: none; a minefield: dust; gone / cutscene / hidden: nothing.)
        a->m_dustTime = 0;
        camera->m_localMatrix(3) = S_vector(px + 36.0f, 0.0f, pz + 48.0f);
        camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        a->Tick(10);
        AA(r.airDust, a->m_dusts.size() == 2);
        camera->m_localMatrix(3) = S_vector(px, 0.0f, pz);
        camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        desert.flags = static_cast<uint32_t>(41) << 24;
        a->Tick(10);
        AA(r.airDust, a->m_dusts.size() == 2);
        desert.flags = static_cast<uint32_t>(0x44) << 24;
        a->Tick(10);
        AA(r.airDust, a->m_dusts.size() == 3);
        desert.flags = static_cast<uint32_t>(42) << 24;
        a->m_dustTime = 0;
        a->m_gone = true;
        a->Tick(10);
        a->m_gone = false;
        AaCutscene(true);
        a->Tick(10);
        AaCutscene(false);
        plane->m_frameFlags &= ~1u;
        a->Tick(10);
        plane->m_frameFlags |= 1u;
        a->m_dustTime = 50;
        a->Tick(10);
        PrShow("d1204", a->m_dusts.size(), a->m_dustTime);
        AA(r.airDust, a->m_dusts.size() == 3 && a->m_dustTime == 40);
        // The dust moving: 37 m/s along its direction, 10 m up and 50 down to the sand, 2 m over it.
        dust = a->m_dusts[0];
        const S_vector before = dust->frame->GetPos();
        reset();
        AA(r.airDust, dust->Update(100));
        const S_vector move(0.6f * 0.001f * 37.0f, 0.0f * 0.001f * 37.0f, 0.8f * 0.001f * 37.0f);
        const S_vector from(move.x * 100.0f + before.x, (move.y * 100.0f + before.y) + 10.0f, before.z + move.z * 100.0f);
        AA(r.airDust, dust->time == 2900 && g_aaRays.size() == 1 && RbSame(g_aaRays[0].from, from) &&
                          g_aaRays[0].dir.y == -50.0f && RbSame(dust->frame->GetPos(), S_vector(from.x, 32.0f, from.z)) &&
                          SaLogIs({"update new"}));
        desert.flags = static_cast<uint32_t>(41) << 24;
        AA(r.airDust, !dust->Update(10));
        desert.flags = static_cast<uint32_t>(0x44) << 24;
        AA(r.airDust, dust->Update(10));
        desert.flags = static_cast<uint32_t>(42) << 24;
        g_aaPlanes.clear();
        AA(r.airDust, !dust->Update(10));
        g_aaPlanes = {{S_vector(0, 1, 0), 30.0f, &desert, S_vector(0, 1, 0)}};
        dust->time = 10;
        AA(r.airDust, !dust->Update(10));
        dust->time = 11;
        AA(r.airDust, dust->Update(10));
        // (The plane's update drops the ended ones: the effect finished, the frame released.)
        // (The first has 1 ms left, the second 5: both end; the third goes on.)
        a->m_dusts[1]->time = 5;
        const int ended = a->m_dusts[1]->particle;
        C_AirAttack::S_Dust *third = a->m_dusts[2];
        setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
        reset();
        a->Update(20);
        AA(r.airDust, a->m_dusts.size() == 1 && a->m_dusts[0] == third &&
                          std::count(g_itLog.begin(), g_itLog.end(), "finish " + std::to_string(ended)) == 1 &&
                          std::count(g_itLog.begin(), g_itLog.end(), "release new") == 2);
        // (1 ms left: the next one now.)
        a->m_dustTime = 1;
        a->Tick(10);
        AA(r.airDust, a->m_dustTime == 3000 && a->m_dusts.size() == 2);
        // The direction levelled and normalised (tiny ones: a whole axis, x first, then z, else y).
        const S_vector dirs[] = {S_vector(3.0f, 0.0f, 4.0f), S_vector(0.00001f, 0.0f, 0.0f), S_vector(-0.00001f, 0.0f, 0.0f),
                                 S_vector(0.0f, 0.0f, 0.00001f), S_vector(0.0f, 0.0f, -0.00001f), S_vector(0.0f, 5.0f, 0.0f)};
        const S_vector want[] = {S_vector(0.6f, 0.0f, 0.8f), S_vector(1.0f, 0.0f, 0.0f), S_vector(-1.0f, 0.0f, 0.0f),
                                 S_vector(0.0f, 0.0f, 1.0f), S_vector(0.0f, 0.0f, -1.0f), S_vector(0.0f, 1.0f, 0.0f)};
        for (int k = 0; k < 6; ++k) {
            a->AddDust({px, 32.0f, pz, 1.0f}, {dirs[k].x, dirs[k].y, dirs[k].z, 0.0f});
            const S_vector got = AaVec(a->m_dusts.back()->dir);
            AA(r.airDust, k == 0 ? RbSame(got, want[k]) : got.x == want[k].x && got.y == want[k].y && got.z == want[k].z);
        }
        // (A dust without its effect: none finished.)
        a->m_dusts.back()->particle = -1;
        reset();
        delete a->m_dusts.back();
        a->m_dusts.pop_back();
        AA(r.airDust, SaLogIs({"release new"}));
        for (C_AirAttack::S_Dust *dd : a->m_dusts)
            delete dd;
        a->m_dusts.clear();
    }

    // --- the hits: any kind takes the hit points; under 0 shot down: smoke from a metre under it, pointing
    // down, the frame's script told; none at 0 ---------------------------------------------------------------
    {
        static S_FrameData planeData;
        static int planeScript;
        planeData = {};
        planeData.actor = a;
        planeData.script = reinterpret_cast<C_Script *>(&planeScript);
        plane->m_data = &planeData;
        a->m_health = 120.0f;
        a->m_state = C_AirAttack::AIR_ATTACK_APPROACHING;
        a->m_target = target;
        a->m_idle = false;
        const int deaths = g_deaths;
        reset();
        a->Hit(HIT_MELEE, 50.0f, S_vector(0, 0, 0), S_vector(0, 0, 0), nullptr, 0);
        AA(r.airHit, a->m_health == 70.0f && a->m_state == C_AirAttack::AIR_ATTACK_APPROACHING && g_particlesMade.empty());
        a->Hit(HIT_BULLET, 70.0f, S_vector(0, 0, 0), S_vector(0, 0, 0), nullptr, 0);
        AA(r.airHit, a->m_health == 0.0f && a->m_state == C_AirAttack::AIR_ATTACK_APPROACHING && g_particlesMade.empty());
        a->m_health = 10.0f;
        a->Hit(HIT_EXPLOSION, 10.5f, S_vector(0, 0, 0), S_vector(0, 0, 0), nullptr, 0);
        S_matrix smoke;
        smoke.Identity();
        smoke.SetDir3(S_vector(0.0f, -1.0f, 0.0f));
        const S_matrix &world = plane->GetMatrix();
        smoke.m[3][0] = world.m[3][0];
        smoke.m[3][1] = world.m[3][1] - 1.0f;
        smoke.m[3][2] = world.m[3][2];
        AA(r.airHit, a->m_health == 0.0f && a->m_state == C_AirAttack::AIR_ATTACK_PASSING && !a->m_idle && !a->m_target &&
                         g_particlesMade.size() == 1 && g_particlesMade[0].effect == 0x10 && g_particlesMade[0].parent == plane &&
                         g_particlesMade[0].faceCamera && !g_particlesMade[0].transient &&
                         SaSame(&g_particlesMade[0].matrix, &smoke, sizeof smoke) && a->m_smoke == g_particlesMade[0].handle &&
                         g_deaths == deaths + 1);
        a->Hit(HIT_BULLET, 5.0f, S_vector(0, 0, 0), S_vector(0, 0, 0), nullptr, 0);
        AA(r.airHit, a->m_health == 0.0f && g_particlesMade.size() == 1 && g_deaths == deaths + 1);
        plane->m_data = nullptr;
        a->m_health = 1.0f;
        a->Hit(HIT_BULLET, 5.0f, S_vector(0, 0, 0), S_vector(0, 0, 0), nullptr, 0);
        AA(r.airHit, g_deaths == deaths + 1 && g_particlesMade.size() == 2);
    }

    // --- the saved game: the base's chunk, then the state's (bullets last); read back (a target by its name
    // through the driver, else the scene; an unknown chunk entered) -----------------------------------------
    {
        a->m_state = C_AirAttack::AIR_ATTACK_LEAVING;
        a->m_idle = true;
        a->m_targetInSight = false;
        a->m_rolling = true;
        a->m_gone = false;
        a->m_roll = 0.25f;
        a->m_speed = 99.0f;
        a->m_clearance = 31.0f;
        a->m_ceiling = 101.0f;
        a->m_health = 55.0f;
        a->m_fireTime = 7;
        a->m_dustTime = 1234;
        a->m_target = target;
        auto *b = new C_AirAttack::S_Bullet;
        b->pos = {1.0f, 2.0f, 3.0f, 1.0f};
        b->shot = {4.0f, 5.0f, 6.0f, 0.0f};
        b->time = 77;
        a->m_bullets.push_back(b);
        const std::string name = target->m_name;
        S_SaBytes e;
        e.Head(0x777, static_cast<uint32_t>(6 + 10 + 10 + 7 * 4 + 10 * 7 + 6 + name.size() + 1 + 6 + 4 + 28));
        e.Head(0x3039, 10).Val(g_actorSaveTag).Head(1000, 10).Val(0);
        e.Head(1500, 7).Val(uint8_t(1)).Head(2000, 7).Val(uint8_t(0)).Head(2500, 7).Val(uint8_t(1)).Head(3000, 7).Val(uint8_t(0));
        e.Head(3500, 10).Val(0.25f).Head(4000, 10).Val(99.0f).Head(4500, 10).Val(31.0f).Head(5000, 10).Val(101.0f);
        e.Head(5500, 10).Val(55.0f).Head(6000, 10).Val(7).Head(8000, 10).Val(1234);
        e.Head(7000, 6 + static_cast<uint32_t>(name.size()) + 1);
        e.Put(name.c_str(), name.size() + 1);
        e.Head(6500, 6 + 4 + 28).Val(1).Val(77).Val(4.0f).Val(5.0f).Val(6.0f).Val(1.0f).Val(2.0f).Val(3.0f);
        bool saved = false;
        const std::vector<uint8_t> bytes = SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved);
        AA(r.airSave, saved && bytes == e.b);
        // (Shot down: state 3.)
        a->m_state = C_AirAttack::AIR_ATTACK_SHOT_DOWN;
        const std::vector<uint8_t> down = SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, nullptr);
        a->m_state = C_AirAttack::AIR_ATTACK_LEAVING;
        AA(r.airSave, down.size() == bytes.size() && down[22] == 3 && bytes[22] == 0 &&
                          std::equal(down.begin() + 23, down.end(), bytes.begin() + 23));
        // (No target: no 7000.)
        a->m_target = nullptr;
        const std::vector<uint8_t> noTarget = SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, nullptr);
        AA(r.airSave, noTarget.size() == bytes.size() - (6 + name.size() + 1));
        // Read back into a cleared plane: the target found by the scene (the driver finds nothing).
        for (C_AirAttack::S_Bullet *bb : a->m_bullets)
            delete bb;
        a->m_bullets.clear();
        a->m_state = 3;
        a->m_idle = a->m_rolling = false;
        a->m_roll = a->m_speed = a->m_clearance = a->m_ceiling = a->m_health = 0.0f;
        a->m_fireTime = a->m_dustTime = 0;
        g_maSceneFrames[name] = target;
        g_aaDriverFinds = nullptr;
        void *const oldFind = g_driverVtable[0x88 / 4];
        g_driverVtable[0x88 / 4] = reinterpret_cast<void *>(&F_AaDriverFind);
        S_SaBytes in;
        in.Put(bytes.data(), bytes.size());
        S_SaReader rd(in);
        reset();
        AA(r.airSave, a->LoadGame(rd.c) && a->m_state == 0 && a->m_idle && a->m_rolling && !a->m_gone && a->m_roll == 0.25f &&
                          a->m_speed == 99.0f && a->m_clearance == 31.0f && a->m_ceiling == 101.0f && a->m_health == 55.0f &&
                          a->m_fireTime == 7 && a->m_dustTime == 1234 && a->m_target == target && a->m_bullets.size() == 1 &&
                          a->m_bullets[0]->time == 77 && a->m_bullets[0]->shot.y == 5.0f && a->m_bullets[0]->pos.z == 3.0f &&
                          SaLogIs({"driverfind " + name, "find " + name + " ffff"}));
        // (The driver's: the scene not asked.)
        static int other;
        g_aaDriverFinds = reinterpret_cast<I3D_frame *>(&other);
        S_SaReader rd2(in);
        reset();
        a->LoadGame(rd2.c);
        AA(r.airSave, a->m_target == reinterpret_cast<I3D_frame *>(&other) && SaLogIs({"driverfind " + name}));
        g_driverVtable[0x88 / 4] = oldFind;
        // (An unknown chunk: entered, its inner chunk read; a short int keeps the value.)
        S_SaBytes odd;
        odd.Head(0x777, 6 + 10 + 6 + 10 + 8).Head(0x3039, 10).Val(g_actorSaveTag).Head(0x55, 6 + 10).Head(4000, 10).Val(12.0f)
            .Head(6000, 8).Val(uint16_t(3));
        S_SaReader rd3(odd);
        a->m_fireTime = 9;
        a->LoadGame(rd3.c);
        AA(r.airSave, a->m_speed == 12.0f && a->m_fireTime == 9);
        // (An empty bullets chunk: none; a chunk after the bullets: read.)
        S_SaBytes after;
        after.Head(0x777, 6 + 10 + 6 + 10 + 10).Head(0x3039, 10).Val(g_actorSaveTag).Head(6500, 6).Head(6500, 10).Val(0)
            .Head(4000, 10).Val(13.0f);
        for (C_AirAttack::S_Bullet *bb : a->m_bullets)
            delete bb;
        a->m_bullets.clear();
        S_SaReader rd4(after);
        a->LoadGame(rd4.c);
        AA(r.airSave, a->m_bullets.empty() && a->m_speed == 13.0f);
        for (C_AirAttack::S_Bullet *bb : a->m_bullets)
            delete bb;
        a->m_bullets.clear();
        a->m_target = nullptr;
        g_maSceneFrames.clear();
    }

    // --- the squads' targets: the nearest seen (its box first on the line past the borders), approaching
    // or passing, not gone, within 150 m ----------------------------------------------------------------------
    {
        I3D_frame *body = NewFrame(FRAME_DUMMY, "aabody", nullptr);
        body->m_localMatrix(3) = S_vector(0.0f, 0.0f, 0.0f);
        body->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        auto *gunner = new C_Human();
        gunner->bodyFrame = body;
        auto *b = static_cast<C_AirAttack *>(creator->Create());
        I3D_frame *planeB = NewFrame(FRAME_DUMMY, "planeb", nullptr);
        b->SetFrame(planeB);
        static S_CollisionVolume boxA, boxB;
        boxA = MakeAaVolume(0, COLLISION_SHAPE_BOX);
        boxA.actor = a;
        boxB = MakeAaVolume(0, COLLISION_SHAPE_BOX);
        boxB.actor = b;
        const auto place = [](I3D_frame *f, const S_vector &p) {
            f->m_localMatrix(3) = p;
            f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        };
        const auto world2 = [&](float ax, float bz) {
            place(plane, S_vector(ax, 0.0f, 0.0f));
            place(planeB, S_vector(0.0f, 0.0f, bz));
            g_aaPlanes = {{S_vector(1, 0, 0), ax - 1.0f, &boxA, S_vector(-1, 0, 0)},
                          {S_vector(0, 0, 1), bz - 1.0f, &boxB, S_vector(0, 0, -1)}};
        };
        a->m_state = C_AirAttack::AIR_ATTACK_APPROACHING;
        b->m_state = C_AirAttack::AIR_ATTACK_PASSING;
        a->m_gone = b->m_gone = false;
        world2(100.0f, 120.0f);
        reset();
        AA(r.airFind, FindAirAttackFor(gunner) == a && g_aaRays.size() == 2 && g_aaRays[0].types == 0 && g_aaRays[0].flags == 0);
        world2(130.0f, 120.0f);
        AA(r.airFind, FindAirAttackFor(gunner) == b);
        // (Not seen, gone, leaving, shot down: not targets; 150 m: too far.)
        world2(100.0f, 120.0f);
        boxA.actor = b;
        AA(r.airFind, FindAirAttackFor(gunner) == b);
        boxA.actor = a;
        a->m_gone = true;
        AA(r.airFind, FindAirAttackFor(gunner) == b);
        a->m_gone = false;
        a->m_state = C_AirAttack::AIR_ATTACK_LEAVING;
        AA(r.airFind, FindAirAttackFor(gunner) == b);
        a->m_state = C_AirAttack::AIR_ATTACK_SHOT_DOWN;
        AA(r.airFind, FindAirAttackFor(gunner) == b);
        a->m_state = C_AirAttack::AIR_ATTACK_APPROACHING;
        world2(150.0f, 150.0f);
        AA(r.airFind, FindAirAttackFor(gunner) == nullptr);
        world2(149.99f, 150.0f);
        AA(r.airFind, FindAirAttackFor(gunner) == a);
        // (Two at the same distance: the later in the list.)
        world2(120.0f, 120.0f);
        AA(r.airFind, FindAirAttackFor(gunner) == a);
        // (The gunner off the origin: the line and the distance from it, y counted.)
        place(body, S_vector(-10.0f, 0.0f, 0.0f));
        world2(141.0f, 300.0f);
        AA(r.airFind, FindAirAttackFor(gunner) == nullptr);
        world2(130.0f, 300.0f);
        AA(r.airFind, FindAirAttackFor(gunner) == a);
        place(body, S_vector(0.0f, 20.0f, 0.0f));
        world2(149.0f, 300.0f);
        AA(r.airFind, FindAirAttackFor(gunner) == nullptr);
        world2(140.0f, 300.0f);
        AA(r.airFind, FindAirAttackFor(gunner) == a);
        place(body, S_vector(0.0f, 0.0f, 0.0f));
        // The pause: every plane's sounds off (the two playing, the guns', the motors'), back on.
        b->m_motorSound = nullptr;
        a->m_motorSound = a->m_motorSounds[2];
        a->m_gunSound = a->m_gunSounds[1];
        reset();
        PauseAirAttackSounds(true);
        std::vector<std::pair<const I3D_frame *, char>> expect;
        const auto off = [&](I3D_sound *s, char c) { expect.push_back({reinterpret_cast<const I3D_frame *>(s), c}); };
        off(a->m_motorSound, 'o');
        off(a->m_gunSound, 'o');
        for (int k = 5; k-- > 0;)
            off(a->m_gunSounds[k], 'o');
        for (int k = 4; k-- > 0;)
            off(a->m_motorSounds[k], 'o');
        AA(r.airFind, g_soundLog == expect);
        reset();
        PauseAirAttackSounds(false);
        AA(r.airFind, g_soundLog.size() == 11 && g_soundLog[0].second == 'O' && g_soundLog[10].second == 'O');
        creator->Destroy(b);
        place(plane, S_vector(px, 60.0f, pz));
        delete gunner;
    }

    // --- the ray past the borders: on 5 cm past each one facing the ray (STOP and PASS), to the next; a
    // border edge-on is a hit; under 20 cm left: none; the outputs written back whatever the result -------
    {
        g_aaPlanes = {{S_vector(0, 1, 0), 30.0f, &border, S_vector(0, 1, 0)},
                      {S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
        S_vector4 hit = {7, 7, 7, 7}, normal = {8, 8, 8, 8};
        reset();
        const S_CollisionVolume *v =
            RayTestPastBorders({px, 60.0f, pz, 1.0f}, {0.0f, -100.0f, 0.0f, 0.0f}, &hit, &normal, 3, 5);
        const S_vector first(px, 60.0f + -100.0f * ((30.0f - 60.0f) / -100.0f), pz);
        const S_vector start(first.x + 0.0f, first.y + -0.05f, first.z + 0.0f);
        AA(r.airRay, v == &ground && g_aaRays.size() == 2 && g_aaRays[0].types == 3 && g_aaRays[0].flags == 5 &&
                         RbSame(g_aaRays[1].from, start) && g_aaRays[1].dir.y == -40.0f - start.y &&
                         RbSame(AaVec(hit), start + g_aaRays[1].dir * ((0.0f - start.y) / g_aaRays[1].dir.y)) && hit.w == 7.0f && normal.y == 1.0f && normal.w == 8.0f);
        border.flags = static_cast<uint32_t>(MATERIAL_PASS) << 24;
        AA(r.airRay, RayTestPastBorders({px, 60.0f, pz, 1.0f}, {0.0f, -100.0f, 0.0f, 0.0f}, nullptr, nullptr, 0, 0) == &ground);
        border.flags = static_cast<uint32_t>(75) << 24;
        AA(r.airRay, RayTestPastBorders({px, 60.0f, pz, 1.0f}, {0.0f, -100.0f, 0.0f, 0.0f}, nullptr, nullptr, 0, 0) == &border);
        border.flags = static_cast<uint32_t>(MATERIAL_STOP) << 24;
        // (Edge-on: the reported normal across the ray.)
        g_aaPlanes[0].reported = S_vector(1.0f, 0.000009f, 0.0f);
        AA(r.airRay, RayTestPastBorders({px, 60.0f, pz, 1.0f}, {0.0f, -100.0f, 0.0f, 0.0f}, nullptr, nullptr, 0, 0) == &border);
        g_aaPlanes[0].reported = S_vector(1.0f, 0.000011f, 0.0f);
        AA(r.airRay, RayTestPastBorders({px, 60.0f, pz, 1.0f}, {0.0f, -100.0f, 0.0f, 0.0f}, nullptr, nullptr, 0, 0) == &ground);
        // (Edge-on to a ray along y and z.)
        g_aaPlanes[0].reported = S_vector(0.0f, 0.70710678f, 0.70710678f);
        AA(r.airRay, RayTestPastBorders({px, 60.0f, pz, 1.0f}, {0.0f, -100.0f, 100.0f, 0.0f}, nullptr, nullptr, 0, 0) == &border);
        g_aaPlanes[0].reported = S_vector(0, 1, 0);
        // (Close to the end: none, the border's place written back.)
        g_aaPlanes = {{S_vector(0, 1, 0), 0.15f, &border, S_vector(0, 1, 0)},
                      {S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
        hit = {7, 7, 7, 7};
        reset();
        AA(r.airRay, !RayTestPastBorders({px, 10.0f, pz, 1.0f}, {0.0f, -10.0f, 0.0f, 0.0f}, &hit, nullptr, 0, 0) &&
                         g_aaRays.size() == 1 && std::fabs(hit.y - 0.15f) < 1e-5f);
        g_aaPlanes[0].level = 0.25f;
        reset();
        AA(r.airRay, RayTestPastBorders({px, 10.0f, pz, 1.0f}, {0.0f, -10.0f, 0.0f, 0.0f}, nullptr, nullptr, 0, 0) == &ground &&
                         g_aaRays.size() == 2);
        // (Nothing: none, the outputs as they were.)
        g_aaPlanes.clear();
        hit = {7, 7, 7, 7};
        normal = {8, 8, 8, 8};
        AA(r.airRay, !RayTestPastBorders({px, 10.0f, pz, 1.0f}, {0.0f, -10.0f, 0.0f, 0.0f}, &hit, &normal, 0, 0) &&
                         hit.x == 7.0f && hit.w == 7.0f && normal.z == 8.0f);
        // (A diagonal ray: its step along the normalised direction.)
        g_aaPlanes = {{S_vector(0, 1, 0), 30.0f, &border, S_vector(0, 1, 0)},
                      {S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
        reset();
        RayTestPastBorders({px, 60.0f, pz, 1.0f}, {30.0f, -60.0f, 20.0f, 0.0f}, nullptr, nullptr, 0, 0);
        S_vector n(30.0f, -60.0f, 20.0f);
        PrNormalize(n, (n.y * n.y + n.z * n.z) + n.x * n.x);
        const S_vector borderAt = S_vector(px, 60.0f, pz) + S_vector(30.0f, -60.0f, 20.0f) * ((30.0f - 60.0f) / -60.0f);
        AA(r.airRay, g_aaRays.size() == 2 && RbSame(g_aaRays[1].from, S_vector(borderAt.x + n.x * 0.05f, borderAt.y + n.y * 0.05f,
                                                                                  borderAt.z + n.z * 0.05f)));
    }
    // --- more of the start (every field reset from garbage, the box's centre off 0, the frame's z axis mostly
    // up or none) and of the stop (no smoke: none destroyed); the whizz within 10 m only ------------------
    {
        static int junk;
        for (int k = 0; k < 2; ++k) {
            auto *c = static_cast<C_AirAttack *>(creator->Create());
            I3D_frame *pc = RbBoxFrame(nullptr, k == 0 ? "planec" : "planed", -2.0f, 6.0f, -1.0f, 1.0f, -1.0f, 1.0f,
                                       S_vector(10.0f, 60.0f, 10.0f));
            LinkUnder(pc, g_sector);
            pc->m_name = Stub(pc).name.c_str();
            pc->m_frameFlags |= 1u;
            pc->m_localMatrix(2) = k == 0 ? S_vector(0.3f, 5.0f, 0.4f) : S_vector(0.0f, 0.0f, 0.0f);
            pc->m_frameFlags &= ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY);
            pc->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
            c->SetFrame(pc);
            c->m_targetInSight = c->m_rolling = c->m_gone = true;
            c->m_dustTime = c->m_fireTime = 5;
            c->m_turnRate = c->m_climbRate = c->m_roll = 1.0f;
            c->m_smoke = 7;
            c->m_state = C_AirAttack::AIR_ATTACK_SHOT_DOWN;
            c->m_motorSound = c->m_gunSound = reinterpret_cast<I3D_sound *>(&junk);
            c->m_target = target;
            reset();
            c->StartMission(true);
            S_vector want(0.0f, 1.0f, 0.0f);
            if (k == 0) {
                S_vector d0(0.3f, 5.0f, 0.4f);
                PrNormalize(d0, (d0.x * d0.x + d0.y * d0.y) + d0.z * d0.z);
                want = S_vector(d0.x, 0.0f, d0.z);
                PrNormalize(want, (want.x * want.x + want.y * want.y) + want.z * want.z);
            }
            AA(r.airStart, c->m_direction.x == want.x && c->m_direction.y == want.y && c->m_direction.z == want.z &&
                               !c->m_targetInSight && !c->m_rolling && !c->m_gone && c->m_dustTime == 0 &&
                               c->m_fireTime == 0 && c->m_turnRate == 0.0f && c->m_climbRate == 0.0f && c->m_roll == 0.0f &&
                               c->m_smoke == -1 && c->m_idle && c->m_state == C_AirAttack::AIR_ATTACK_SHOT_DOWN &&
                               !c->m_motorSound && !c->m_gunSound && !c->m_target);
            if (k == 0) {
                I3D_bbox hb;
                const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hb, pc);
                const float centerX = box.min.x + (box.max.x - box.min.x) * 0.5f;
                AA(r.airStart, centerX != 0.0f && c->m_volume.centerW == centerX);
            }
            reset();
            c->StopMission(true);
            AA(r.airStop, g_particlesDestroyed.empty() && !c->m_started);
            creator->Destroy(c);
        }
        // The whizz: exactly 10 m from the camera, or 11 along z: none; 9.9: one.
        a->m_state = C_AirAttack::AIR_ATTACK_PASSING;
        a->m_idle = false;
        a->m_target = nullptr;
        a->m_gunSound = nullptr;
        g_waBlockAt = -1;
        g_aaPlanes = {{S_vector(0, 1, 0), 0.0f, &ground, S_vector(0, 1, 0)}};
        int seed4 = 1;
        for (;; ++seed4) {
            std::srand(seed4);
            if ((std::rand() & 3) != 0 && (std::rand() & 3) == 0)
                break;
        }
        const S_vector4 cams[3] = {{7.0f, 0.5f, 10.0f, 1.0f}, {1.0f, 0.5f, 13.0f, 1.0f}, {1.0f, 0.5f, 11.9f, 1.0f}};
        for (int k = 0; k < 3; ++k) {
            g_aaCameraPos = cams[k];
            auto *b = new C_AirAttack::S_Bullet;
            b->pos = {1.0f, 0.5f, 2.0f, 1.0f};
            b->shot = {0.0f, -10.0f, 0.0f, 0.0f};
            b->time = 5;
            a->m_bullets.push_back(b);
            setPlane(S_vector(px, 60.0f, pz), S_vector(0.6f, 0.0f, 0.8f));
            std::srand(seed4);
            reset();
            a->Update(20);
            const int plays = static_cast<int>(std::count_if(g_soundLog.begin(), g_soundLog.end(),
                                                             [](const auto &e) { return e.second == 'P'; }));
            AA(r.airBullets, a->m_bullets.empty() && plays == (k == 2) &&
                                 std::count(g_itLog.begin(), g_itLog.end(), "camerapos 2") == 1);
        }
    }
    delete victim;
    g_waBlockAt = -1;
    g_aaActorSector = nullptr;
    creator->Destroy(a);
    g_aaPlanes.clear();
    g_rayTestHook = nullptr;
    g_rbSplashStep = oldSplashStep;
    g_rbSplashHandle = oldSplashHandle;
    g_sceneVtable[0x7C / 4] = oldSceneFind;
    for (int i = 0; i < FRAME_SLOTS; ++i)
        g_frameVtable[frameSlots[i] / 4] = oldFrame[i];
    g_driverVtable[0x54 / 4] = oldCreate;
    for (int i = 0; i < 5; ++i)
        g_soundVtable[soundSlots[i] / 4] = oldSound[i];
    g_maSoundOn = oldSoundOn;
    g_gameRole = oldRole;
    *reinterpret_cast<I3D_sector **>(sceneBase + 0x1F8) = oldPrimary;
    *reinterpret_cast<I3D_frame **>(sceneBase + 0x15C) = oldCamera;
    GetGame()->m_scene = oldScenePtr;
    g_sector->m_firstChild = nullptr;
    for (S_StubFrame *stub : g_frames)
        g_itNames.erase(stub->frame);
    reset();
    std::printf("air: air %d airProps %d airStart %d airStop %d airTarget %d airFly %d airClimb %d airStates %d "
                "airFire %d airBullets %d airSounds %d airDust %d airHit %d airSave %d airFind %d airRay %d\n",
                r.air, r.airProps, r.airStart, r.airStop, r.airTarget, r.airFly, r.airClimb, r.airStates, r.airFire,
                r.airBullets, r.airSounds, r.airDust, r.airHit, r.airSave, r.airFind, r.airRay);
}
