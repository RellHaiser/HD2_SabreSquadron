// Part 8d-i's birds (game/birds.cpp) and mesh player (game/mesh_player.cpp) (WORLD_TEST), after
// geffects2_test.inc (its fakes). Bird models are frames of the GEffects test's model vtable with
// "Object03" / "Object01" wings; the mesh player's stream is a logging fake. Prints the "birds:" line.

namespace {

// --- the stream and the textures -----------------------------------------------------------------

struct S_BdTexture {
    void **vtable;
    uint8_t unknown04[0x10];
    int refs;                                           // +0x14
    const char *name;                                   // +0x18
};
void *g_bdTextureVt[SLOTS], *g_bdStreamVt[SLOTS];
S_BdTexture *g_bdVideo;                                 // the stream's picture
int g_bdStreamState;
struct S_BdStream {
    void **vtable;
};
uint32_t __stdcall F_BdTextureRelease(S_BdTexture *self)
{
    GfLog("texrelease %s", self->name);
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __stdcall F_BdStreamRelease(S_BdStream *) { GfLog("stream release"); return 0; }
HRESULT __stdcall F_BdStreamOpen(S_BdStream *, const char *name, uint32_t flags)
{
    GfLog("stream open %s %u", name, flags);
    return std::strstr(name, "missing") ? 1 : 0;
}
void __stdcall F_BdStreamPlay(S_BdStream *) { GfLog("stream play"); }
void __stdcall F_BdStreamStop(S_BdStream *) { GfLog("stream stop"); }
int __stdcall F_BdStreamState(S_BdStream *) { return g_bdStreamState; }
S_BdTexture **__stdcall F_BdStreamTexture(S_BdStream *) { return &g_bdVideo; }
void __stdcall F_BdStreamVideo(S_BdStream *, int unknown) { GfLog("stream video %d", unknown); }
void *__stdcall F_BdCreateStream(void *)
{
    GfLog("stream create");
    auto *s = new S_BdStream();
    s->vtable = g_bdStreamVt;
    return s;
}
S_BdTexture *BdTexture(const char *name)
{
    auto *t = new S_BdTexture();
    t->vtable = g_bdTextureVt;
    t->refs = 1;
    t->name = name;
    g_gfTextureNames[t] = name;
    return t;
}
uint32_t __stdcall F_BdSceneRemove(void *, I3D_frame *frame)
{
    GfLog("sremove %s", GfName(frame));
    return 1;
}

void BdInitVtables()
{
    for (int i = 0; i < SLOTS; ++i)
        g_bdTextureVt[i] = g_bdStreamVt[i] = g_frameVtable[0x10 / 4];
    g_bdTextureVt[0x00 / 4] = reinterpret_cast<void *>(&F_BdTextureRelease);
    g_bdStreamVt[0x04 / 4] = reinterpret_cast<void *>(&F_BdStreamRelease);
    g_bdStreamVt[0x0C / 4] = reinterpret_cast<void *>(&F_BdStreamOpen);
    g_bdStreamVt[0x14 / 4] = reinterpret_cast<void *>(&F_BdStreamPlay);
    g_bdStreamVt[0x18 / 4] = reinterpret_cast<void *>(&F_BdStreamStop);
    g_bdStreamVt[0x2C / 4] = reinterpret_cast<void *>(&F_BdStreamState);
    g_bdStreamVt[0x48 / 4] = reinterpret_cast<void *>(&F_BdStreamTexture);
    g_bdStreamVt[0x4C / 4] = reinterpret_cast<void *>(&F_BdStreamVideo);
}

// A bird model at `pos` with its wings (none: `wings` false).
I3D_frame *BdModel(const std::string &name, const S_vector &pos, bool wings = true)
{
    I3D_frame *m = GfFrame(FRAME_MODEL, name);
    GfPlace(m, pos);
    if (wings) {
        GfFrame(FRAME_DUMMY, "Object03", m);
        GfFrame(FRAME_DUMMY, "Object01", m);
    }
    return m;
}

// The wing's rotation for a phase, as the bird computes it.
S_quat BdWing(float phase)
{
    S_quat q;
    q.Make(S_vector(0.0f, 0.0f, 1.0f), static_cast<float>(std::sin(phase) * 0.75f - 0.4));
    q.Normalize();
    return q;
}
// srand's first seed from `from` whose first `count` rand()s are all 20000 or more (the waits' %
// 20000 and % 5000 then count), seeded.
void BdSeed(int count, unsigned from = 1)
{
    for (unsigned seed = from;; ++seed) {
        std::srand(seed);
        bool all = true;
        for (int i = 0; i < count; ++i)
            all = all && std::rand() >= 20000;
        if (all) {
            std::srand(seed);
            return;
        }
    }
}
bool BdNear(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps;
}

struct S_BdResults {
    bool bdMake = true, bdDelete = true, bdSit = true, bdHop = true, bdFlyAway = true, bdTakeOff = true, bdClimb = true;
    bool bdGlide = true, bdModule = true, bdMovie = true, bdMovieFile = true, bdMovieUpdate = true, bdEffects = true;
};

}  // namespace

void BirdsTest()
{
    S_BdResults r;
    static bool once;
    if (!once) {
        once = true;
        BdInitVtables();
    }
    void *sceneSlots[SCENE_SLOTS], *igraphSlots[SLOTS];
    std::memcpy(sceneSlots, g_sceneVtable, sizeof sceneSlots);
    std::memcpy(igraphSlots, g_igraphVtable, sizeof igraphSlots);
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_GfSceneFind);
    g_sceneVtable[0x44 / 4] = reinterpret_cast<void *>(&F_GfSceneEnum);
    g_sceneVtable[0x84 / 4] = reinterpret_cast<void *>(&F_BdSceneRemove);
    g_igraphVtable[0x38 / 4] = reinterpret_cast<void *>(&F_BdCreateStream);
    uint8_t *scene = reinterpret_cast<uint8_t *>(GetGame()->GetScene());
    I3D_frame *const oldPrimary = WmField<I3D_frame *>(scene, 0x1F8);
    I3D_frame *primary = GfFrame(FRAME_SECTOR, "primary");
    WmField<I3D_frame *>(scene, 0x1F8) = primary;
    I3D_frame *cam = GfFrame(FRAME_CAMERA, "cam");
    WmField<I3D_frame *>(scene, 0x15C) = cam;
    WmField<int>(scene, 0x11C) = 40;
    GetGame()->m_farClip = 300.0f;
    GetGame()->m_missionName = "GFX";
    g_gfScene.clear();
    g_gfEnumFrames.clear();
    g_gfRandoms.clear();
    g_birds.clear();
    g_spareBirdModels.clear();

    // --- a bird made, deleted ---
    {
        I3D_frame *m = BdModel("m", S_vector(5.0f, 2.0f, 7.0f));
        m->m_rot = S_quat(0.70710678f, 0.0f, 0.70710678f, 0.0f);
        m->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
        BdSeed(2);
        const int r1 = std::rand(), r2 = std::rand();
        BdSeed(2);
        g_gfLog.clear();
        auto *b = new C_Bird(m, false);
        WM(r.bdMake, BIRD_TAKING_OFF == 0 && BIRD_CLIMBING == 1 && BIRD_GLIDING == 2 && BIRD_SITTING == 3);
        WM(r.bdMake, GfLogIs({"link m primary 1", "update m", "find m Object03 ffff", "on Object03 0", "find m Object01 ffff",
                              "on Object01 0"}));
        WM(r.bdMake, b->m_unknown40 == r1 % 5000 + 5000 && b->m_timer == static_cast<float>(r2 % 20000 + 3000) &&
                         b->m_startPos == S_vector(5, 2, 7) && b->m_pos == S_vector(5, 2, 7) && b->m_startRot.s == m->m_rot.s &&
                         b->m_startRot.v.y == m->m_rot.v.y && b->m_state == BIRD_SITTING && b->m_speed == 5.0f && !b->m_flapping &&
                         !b->m_flownAway && b->m_velocity == S_vector(0, 0, 0) && b->m_wingPhase == 0.0f &&
                         b->GetModel() == m && !b->OwnsModel() && m->GetData() == reinterpret_cast<S_FrameData *>(b) &&
                         b->m_leftWing && b->m_leftWing->GetShortName() == std::string("Object03") && b->m_rightWing &&
                         b->m_rightWing->GetShortName() == std::string("Object01"));
        // On a model of a bird before: that bird's start.
        I3D_frame *m2 = BdModel("m2", S_vector(1.0f, 1.0f, 1.0f));
        m2->m_data = reinterpret_cast<S_FrameData *>(b);
        g_gfLog.clear();
        auto *b2 = new C_Bird(m2, true);
        WM(r.bdMake, b2->m_startPos == S_vector(5, 2, 7) && m2->GetPos() == S_vector(5, 2, 7) && b2->OwnsModel() &&
                         GfHas({"link m2 primary 1", "update m2", "update m2", "find m2 Object03 ffff"}) &&
                         m2->GetData() == reinterpret_cast<S_FrameData *>(b2));
        // Without wings.
        auto *b3 = new C_Bird(BdModel("m3", S_vector(0, 0, 0), false), true);
        WM(r.bdMake, !b3->m_leftWing && !b3->m_rightWing && !b3->Update(10));
        // Deleted: put back at its start, hidden, spare; its own model removed from the scene.
        GfPlace(m, S_vector(9.0f, 9.0f, 9.0f));
        m->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        g_gfLog.clear();
        delete b;
        WM(r.bdDelete, GfLogIs({"on m 0", "update m"}) && m->GetPos() == S_vector(5, 2, 7) && BdNear(m->m_rot.v.y, 0.70710678f) &&
                           g_spareBirdModels == std::vector<I3D_frame *>({m}) && m->GetData() == nullptr);
        g_gfLog.clear();
        delete b2;
        WM(r.bdDelete, GfLogIs({"sremove m2"}) && g_spareBirdModels.size() == 1 && m2->GetData() == nullptr);
        // Another's data kept.
        auto *b4 = new C_Bird(BdModel("m4", S_vector(0, 0, 0)), false);
        S_FrameData other = {};
        b4->GetModel()->m_data = &other;
        delete b4;
        WM(r.bdDelete, g_spareBirdModels.size() == 2 && g_spareBirdModels[1]->GetData() == &other);
        delete b3;
        g_spareBirdModels.clear();
    }

    // --- sitting, hopping ---
    {
        I3D_frame *m = BdModel("s", S_vector(0.0f, 1.0f, 0.0f));
        auto *b = new C_Bird(m, true);
        b->m_timer = 100.0f;
        g_gfLog.clear();
        WM(r.bdSit, b->Update(50) && g_gfLog.empty() && b->m_timer == 50.0f && b->m_pos == S_vector(0, 1, 0));
        WM(r.bdSit, b->Update(50) && g_gfLog.empty() && b->m_timer == 0.0f);
        // The wait over: a hop up (RandomFloat(0.001) + 0.0001 per ms), the wings flapping.
        g_gfRandoms = {0.0005f};
        GfPlace(m, S_vector(0.5f, 1.5f, 0.5f));
        g_gfLog.clear();
        WM(r.bdHop, b->Update(60) && b->m_flapping && b->m_timer == -60.0f);
        const float vy = static_cast<float>(0.0005f + 0.0001);
        WM(r.bdHop, GfLogIs({"on Object03 1", "on Object01 1", "rand 0.0", "update s"}) && b->m_velocity == S_vector(0, vy, 0) &&
                        b->m_pos.x == 0.0f && b->m_pos.y == 60.0f * vy + 1.0f && m->GetPos() == b->m_pos);
        float phase = 50.0f * 5.0f * 0.007f + 0.0f;      // (the phase goes on while it sits)
        phase = 50.0f * 5.0f * 0.007f + phase;
        phase = 60.0f * 5.0f * 0.007f + phase;
        S_quat wing = BdWing(phase);
        WM(r.bdHop, b->m_wingPhase == phase && BdNear(b->m_leftWing->m_rot.s, wing.s) &&
                        BdNear(b->m_leftWing->m_rot.v.z, wing.v.z) && BdNear(b->m_rightWing->m_rot.s, -wing.s) &&
                        BdNear(b->m_rightWing->m_rot.v.z, wing.v.z));
        // Falling back (1e-6 per ms), above the start: on.
        g_gfLog.clear();
        const float y0 = b->m_pos.y;
        WM(r.bdHop, b->Update(100) && b->m_flapping && b->m_velocity.y == vy - 100.0f * 1e-6f &&
                        b->m_pos.y == 100.0f * (vy - 100.0f * 1e-6f) + y0 && GfLogIs({"update s"}));
        // Below the start: landed (20..40 s), the wings still.
        b->m_pos.y = 0.99f;
        BdSeed(1, 8);
        const int wait = std::rand() % 20000 + 20000;
        BdSeed(1, 8);
        g_gfRandoms = {0.0009f};
        g_gfLog.clear();
        WM(r.bdHop, b->Update(10) && !b->m_flapping && b->m_timer == static_cast<float>(wait) && b->m_velocity == S_vector(0, 0, 0) &&
                        b->m_pos == S_vector(0, 1, 0) && GfLogIs({"on Object03 0", "on Object01 0", "rand 0.0", "update s"}) &&
                        g_gfRandomMax == 0.001f);
        // Exactly at the start: not landed; the timer exactly 0: no hop.
        b->m_flapping = true;
        b->m_pos.y = 1.0f;
        b->m_velocity = S_vector(0, 0, 0);
        WM(r.bdHop, b->Update(1) && b->m_flapping);
        b->m_flapping = false;
        b->m_timer = 5.0f;
        WM(r.bdHop, b->Update(5) && !b->m_flapping && b->m_timer == 0.0f);
        delete b;

        // --- flying off ---
        I3D_frame *f = BdModel("f", S_vector(0, 0, 0));
        auto *fb = new C_Bird(f, true);
        g_gfRandoms = {1.0f, 0.1f, 0.2f};
        std::srand(4);
        const int pick = std::rand() % 100 + 1;
        std::srand(4);
        g_gfLog.clear();
        fb->FlyAway();
        S_vector v(0.2f, 1.0f, 1.1f);
        {
            const float k = 1.0f / std::sqrt((v.z * v.z + v.y * v.y) + v.x * v.x);
            v = S_vector(v.x * k, v.y * k, v.z * k);
        }
        const float scale = 6.0f * 0.001f;
        WM(r.bdFlyAway, GfLogIs({"rand 2.0", "rand 0.5", "rand 0.5", "on Object03 1", "on Object01 1"}) && fb->m_speed == 6.0f &&
                            BdNear(fb->m_velocity.x, scale * v.x, 1e-8f) && BdNear(fb->m_velocity.y, scale * v.y, 1e-8f) &&
                            BdNear(fb->m_velocity.z, scale * v.z, 1e-8f) && fb->m_state == BIRD_TAKING_OFF && fb->m_flapping &&
                            fb->m_flownAway && fb->m_wingPhase == 3.1415927f / static_cast<float>(pick));
        g_gfLog.clear();
        fb->FlyAway();
        WM(r.bdFlyAway, g_gfLog.empty());
        // Facing x: off along x and up.
        I3D_frame *fx = BdModel("fx", S_vector(0, 0, 0));
        fx->m_localMatrix(2) = S_vector(2, 0, 0);
        fx->m_localMatrix(0) = S_vector(0, 0, -1);
        auto *fxb = new C_Bird(fx, true);
        g_gfRandoms = {0.0f, 0.0f, 0.0f};
        fxb->FlyAway();
        WM(r.bdFlyAway, BdNear(fxb->m_velocity.x, fxb->m_velocity.y, 1e-9f) && fxb->m_velocity.x > 0.0f && fxb->m_velocity.z == 0.0f);
        // A square of exactly 1.5 normalized; too short: x made +-1.
        auto *sq = new C_Bird(BdModel("sq", S_vector(0, 0, 0)), true);
        g_gfRandoms = {0.0f, -0.5f, 0.5f};
        sq->FlyAway();
        {
            const float k = 1.0f / std::sqrt(1.5f), s = 0.0f + 5.0f;
            WM(r.bdFlyAway, BdNear(sq->m_velocity.x, s * 0.001f * (0.5f * k), 1e-9f) &&
                                BdNear(sq->m_velocity.y, s * 0.001f * k, 1e-9f));
        }
        delete sq;
        I3D_frame *dn = BdModel("dn", S_vector(0, 0, 0));
        dn->m_localMatrix(2) = S_vector(0, -1, 0);
        dn->m_localMatrix(1) = S_vector(0, 0, 1);
        auto *dnb = new C_Bird(dn, true);
        g_gfRandoms = {0.0f, 0.0f, -1e-5f};
        dnb->FlyAway();
        WM(r.bdFlyAway, dnb->m_velocity.x == 5.0f * 0.001f * -1.0f && dnb->m_velocity.y == 0.0f && dnb->m_velocity.z == 0.0f);
        delete dnb;
        // Without wings: on its way, not flown away (it may go again).
        auto *nw = new C_Bird(BdModel("nw", S_vector(0, 0, 0), false), true);
        g_gfRandoms = {0.0f, 0.0f, 0.0f};
        nw->FlyAway();
        WM(r.bdFlyAway, !nw->m_flownAway && nw->m_flapping && nw->m_state == BIRD_TAKING_OFF);
        delete nw;
        delete fxb;

        // --- taking off, climbing, gliding ---
        GfPlace(cam, S_vector(0, 0, 0));
        fb->m_velocity = S_vector(0.0f, 0.004f, 0.001f);
        g_gfLog.clear();
        WM(r.bdTakeOff, fb->Update(1000) && fb->m_state == BIRD_TAKING_OFF && BdNear(fb->m_pos.y, 4.0f) && GfLogIs({"update f"}));
        WM(r.bdTakeOff, fb->Update(1000) && fb->m_state == BIRD_TAKING_OFF && BdNear(fb->m_pos.y, 8.0f) && BdNear(fb->m_pos.z, 2.0f));
        // Exactly 10 m up: not yet.
        fb->m_velocity = S_vector(0.0f, 0.0f, 0.0f);
        fb->m_pos.y = 10.0f;
        WM(r.bdTakeOff, fb->Update(10) && fb->m_state == BIRD_TAKING_OFF);
        fb->m_velocity = S_vector(0.0f, 0.004f, 0.0f);
        // Past 10 m: climbing at 0.75 the speed, the same time again.
        fb->m_pos.y = 10.5f;
        g_gfLog.clear();
        WM(r.bdTakeOff, fb->Update(100) && fb->m_state == BIRD_CLIMBING && fb->m_speed == 4.5f);
        {
            // The climb's step: gravity, normalized, the speed.
            S_vector c(0.0f, 0.004f - 100.0f * 5.000000555810402e-07f, 0.0f);
            const float k = 1.0f / std::sqrt((c.x * c.x + c.z * c.z) + c.y * c.y);
            c = S_vector(k * c.x, c.y * k, k * c.z);
            c = S_vector(4.5f * 0.001f * c.x, 4.5f * 0.001f * c.y, 4.5f * 0.001f * c.z);
            WM(r.bdTakeOff, BdNear(fb->m_velocity.y, c.y, 1e-9f) &&
                                BdNear(fb->m_pos.y, 10.5f + 100.0f * 0.004f + 100.0f * c.y, 1e-4f) &&
                                std::count(g_gfLog.begin(), g_gfLog.end(), "update f") == 2);
        }
        // Climbing: x and z kept, down past 0: gliding at -0.1 (normalized), the rest of the time.
        fb->m_state = BIRD_CLIMBING;
        fb->m_velocity = S_vector(0.003f, 0.00001f, 0.004f);
        fb->m_pos = S_vector(0, 20, 0);
        g_gfLog.clear();
        WM(r.bdClimb, fb->Update(100) && fb->m_state == BIRD_GLIDING);
        {
            S_vector c(0.003f, 0.00001f - 100.0f * 5.000000555810402e-07f, 0.004f);
            const float k1 = 1.0f / std::sqrt((c.x * c.x + c.z * c.z) + c.y * c.y);
            c = S_vector(k1 * c.x, c.y * k1, k1 * c.z);
            const float k = 1.0f / std::sqrt((c.x * c.x + c.z * c.z) + 0.010000000707805157f);
            const S_vector g(k * c.x, -0.1f * k, k * c.z);
            const float s = 4.5f * 0.001f;
            WM(r.bdClimb, BdNear(fb->m_velocity.x, s * g.x, 1e-8f) && BdNear(fb->m_velocity.y, s * g.y, 1e-8f) &&
                              BdNear(fb->m_velocity.z, s * g.z, 1e-8f));
            const int more = static_cast<int>(g.y * -2e6f);
            WM(r.bdClimb, BdNear(fb->m_pos.x, 100.0f * s * g.x + static_cast<float>(more) * s * g.x, 1e-3f) &&
                              BdNear(fb->m_pos.y, 20.0f + (100.0f + static_cast<float>(more)) * s * g.y, 1e-3f) &&
                              std::count(g_gfLog.begin(), g_gfLog.end(), "update f") == 2);
        }
        // Level (y 0): climbing on.
        const float s = 4.5f * 0.001f;
        fb->m_state = BIRD_CLIMBING;
        fb->m_velocity = S_vector(0.8f, 0.0f, 0.6f);
        WM(r.bdClimb, fb->Update(0) && fb->m_state == BIRD_CLIMBING);
        // A square of exactly 1.5 normalized.
        fb->m_state = BIRD_CLIMBING;
        fb->m_velocity = S_vector(1.0f, 0.5f, 0.5f);
        fb->Update(0);
        {
            const float k = 1.0f / std::sqrt(1.5f);
            WM(r.bdClimb, BdNear(fb->m_velocity.x, s * k, 1e-9f) && BdNear(fb->m_velocity.y, s * (0.5f * k), 1e-9f));
        }
        // Too short to normalize: the first of x, z, y not 0 made +-1.
        fb->m_state = BIRD_CLIMBING;
        fb->m_velocity = S_vector(-1e-5f, 1e-5f, 0.0f);
        fb->Update(0);
        WM(r.bdClimb, fb->m_state == BIRD_CLIMBING && fb->m_velocity.x == s * -1.0f && fb->m_velocity.y == s * 1e-5f &&
                          fb->m_velocity.z == 0.0f);
        fb->m_state = BIRD_CLIMBING;
        fb->m_velocity = S_vector(0.0f, 1e-5f, -1e-5f);
        fb->Update(0);
        WM(r.bdClimb, fb->m_velocity.x == 0.0f && fb->m_velocity.y == s * 1e-5f && fb->m_velocity.z == s * -1.0f);
        fb->m_state = BIRD_CLIMBING;
        fb->m_velocity = S_vector(0.0f, 1e-5f, 0.0f);
        fb->Update(0);
        WM(r.bdClimb, fb->m_velocity.x == 0.0f && fb->m_velocity.y == s && fb->m_velocity.z == 0.0f);
        // Down with x² + y² and x² + 0.01 both exactly 1: neither normalized, y -0.1.
        fb->m_state = BIRD_CLIMBING;
        fb->m_velocity = S_vector(0.994987428188324f, -0.10000024735927582f, 0.0f);
        fb->m_pos = S_vector(0, 20, 0);
        fb->Update(0);
        WM(r.bdClimb, fb->m_state == BIRD_GLIDING && fb->m_velocity.y == s * -0.1f && fb->m_velocity.x == s * 0.994987428188324f);
        // Climbing on: x and z grow (normalized), y less.
        fb->m_state = BIRD_CLIMBING;
        fb->m_velocity = S_vector(0.0f, 1.0f, 0.0f);
        fb->Update(10);
        WM(r.bdClimb, fb->m_state == BIRD_CLIMBING && BdNear(fb->m_velocity.y, 4.5f * 0.001f, 1e-9f));
        // Gliding: gone beyond the far clip or 500 m (the wings dropped, back up to the start's 10 m).
        fb->m_state = BIRD_GLIDING;
        fb->m_velocity = S_vector(0, 0, 0);
        fb->m_pos = S_vector(0, 0, 299.0f);
        WM(r.bdGlide, fb->Update(10) && fb->m_leftWing);
        // Exactly at the far clip: on; the camera's place counts.
        fb->m_pos = S_vector(0, 0, 300.0f);
        WM(r.bdGlide, fb->Update(10));
        GfPlace(cam, S_vector(200.0f, 0, 0));
        fb->m_pos = S_vector(200.0f, 0, 299.0f);
        WM(r.bdGlide, fb->Update(10));
        GfPlace(cam, S_vector(0, 0, 200.0f));
        fb->m_pos = S_vector(299.0f, 0, 200.0f);
        WM(r.bdGlide, fb->Update(10));
        GfPlace(cam, S_vector(0, 0, 0));
        fb->m_pos = S_vector(0, 0, 301.0f);
        WM(r.bdGlide, !fb->Update(10) && !fb->m_leftWing && !fb->m_rightWing && fb->m_pos.y == 10.0f);
        fb->m_leftWing = fb->m_rightWing = m;
        GetGame()->m_farClip = 1000.0f;
        fb->m_pos = S_vector(0, 0, 499.0f);
        WM(r.bdGlide, fb->Update(10));
        fb->m_pos = S_vector(0, 0, 501.0f);
        WM(r.bdGlide, !fb->Update(10));
        fb->m_leftWing = fb->m_rightWing = m;
        fb->m_pos = S_vector(0, 501.0f, 0);
        GfPlace(cam, S_vector(0, 1.0f, 0));
        WM(r.bdGlide, fb->Update(10));
        fb->m_pos = S_vector(-300.0f, 1.0f, 400.5f);
        WM(r.bdGlide, !fb->Update(10));
        GetGame()->m_farClip = 300.0f;
        GfPlace(cam, S_vector(0, 0, 0));
        fb->m_leftWing = nullptr;
        delete fb;
    }

    // --- the birds module ---
    {
        I3D_frame *a = BdModel("ptacekletacek01", S_vector(0, 0, 0));
        I3D_frame *b = BdModel("x.PtacekLetacek_b", S_vector(1, 0, 0));
        I3D_frame *c = BdModel("crow", S_vector(2, 0, 0));
        g_gfEnumFrames = {a, b, c};
        g_gfLog.clear();
        CollectBirds();
        WM(r.bdModule, g_gfLog.size() > 0 && g_gfLog[0] == "senum 80" && g_birds.size() == 2 && g_birds[0]->GetModel() == a &&
                           g_birds[1]->GetModel() == b && !g_birds[0]->OwnsModel());
        // Updated: those flown off deleted, the last into their place.
        I3D_frame *d = BdModel("d", S_vector(3, 0, 0));
        g_birds.push_back(new C_Bird(d, false));
        g_birds[0]->m_leftWing = nullptr;
        g_birds[0]->m_timer = 1e9f;
        g_birds[1]->m_timer = 1e9f;
        g_birds[2]->m_timer = 1e9f;
        UpdateBirds(10);
        WM(r.bdModule, g_birds.size() == 2 && g_birds[0]->GetModel() == d && g_birds[1]->GetModel() == b &&
                           g_spareBirdModels == std::vector<I3D_frame *>({a}) && g_birds[1]->m_timer == 1e9f - 10.0f);
        g_birds[1]->m_leftWing = nullptr;
        UpdateBirds(10);
        WM(r.bdModule, g_birds.size() == 1 && g_birds[0]->GetModel() == d);
        // Freed: the spare models shown again.
        g_gfLog.clear();
        FreeBirds();
        WM(r.bdModule, g_birds.empty() && g_spareBirdModels.empty() &&
                           GfLogIs({"on d 0", "update d", "on d 1", "update d", "on x.PtacekLetacek_b 1", "update x.PtacekLetacek_b",
                                    "on ptacekletacek01 1", "update ptacekletacek01"}));
        // A bird's children searched too.
        I3D_frame *p = BdModel("ptacekletacekP", S_vector(0, 0, 0));
        I3D_frame *pc = GfFrame(FRAME_MODEL, "ptacekletacekC", p);
        g_gfEnumFrames = {p, pc};
        CollectBirds();
        WM(r.bdModule, g_birds.size() == 2);
        FreeBirds();
        g_gfEnumFrames.clear();
    }

    // --- the mesh player ---
    {
        auto chunk = [](uint16_t id, const std::string &payload) {
            std::string out(6, '\0');
            const uint32_t size = static_cast<uint32_t>(6 + payload.size());
            std::memcpy(&out[0], &id, 2);
            std::memcpy(&out[2], &size, 4);
            return out + payload;
        };
        const std::string body = chunk(0x44C, std::string("film.bik", 9)) + chunk(0x4B0, std::string("screen", 7)) +
                                 chunk(0x4B0, std::string("nosuch", 7)) + chunk(0x123, "xx") +
                                 chunk(0x4B0, std::string("plain", 6)) + chunk(0x4B0, std::string("dummy", 6)) +
                                 chunk(0x4B0, std::string("bare", 5));
        const std::string file = chunk(1, body);
        const std::string name = "missions\\GFX\\meshplayer.bin";
        g_files[name].assign(file.begin(), file.end());
        S_BdTexture *own = BdTexture("own.bmp");
        g_bdVideo = BdTexture("video");
        S_GfMaterial *m0 = GfMaterial("m0", nullptr), *m1 = GfMaterial("m1", nullptr);
        m0->texture = reinterpret_cast<S_GfTexture *>(own);
        I3D_frame *screen = GfSkinned("screen", GfMesh("sm", {GfLevel({GfGroup("g0", m0), GfGroup("g1", m1), GfGroup("g2", nullptr)})}),
                                      nullptr, I3DVISUAL_OBJECT);
        I3D_frame *plain = GfSkinned("plain", GfMesh("pm", {GfLevel({GfGroup("p0", GfMaterial("pmat", nullptr))})}), nullptr,
                                     I3DVISUAL_OBJECT, false);
        I3D_frame *dummy = GfFrame(FRAME_DUMMY, "dummy");
        WmField<uint32_t>(dummy, 0x1D0) = VISFLAGS_OBJECT_PARAMS;
        I3D_frame *bare = GfSkinned("bare", GfMesh("bm", {GfLevel({})}), nullptr, I3DVISUAL_OBJECT);
        g_gfScene = {{"screen", screen}, {"plain", plain}, {"dummy", dummy}, {"bare", bare}};
        C_MeshPlayer p;
        g_gfLog.clear();
        p.Load();
        WM(r.bdMovie, GfHas({"sfind screen ffff", "sfind nosuch ffff", "sfind plain ffff", "stream create",
                             "stream open GameMovie\\film.bik 0", "settex m1 video", "settex m0 video", "stream play"}) &&
                          std::count_if(g_gfLog.begin(), g_gfLog.end(), [](const std::string &l) { return l.compare(0, 7, "settex ") == 0; }) == 2);
        WM(r.bdMovie, p.m_frames == std::vector<I3D_frame *>({screen, bare}) && p.m_textures.size() == 2 &&
                          p.m_textures[0] == reinterpret_cast<I3D_texture *>(own) && !p.m_textures[1] && own->refs == 2 &&
                          std::string(p.m_movie.c_str()) == "film.bik" && p.m_stream);
        // Updated: a picture when a screen was drawn; from the start at the end.
        g_gfLog.clear();
        g_bdStreamState = 0;
        p.Update(16);
        WM(r.bdMovieUpdate, g_gfLog.empty());
        WmField<int>(screen, 0x11C) = 40;
        p.Update(16);
        WM(r.bdMovieUpdate, GfLogIs({"stream video 0"}));
        WmField<int>(screen, 0x11C) = 0;
        WmField<int>(bare, 0x11C) = 40;
        WmField<int>(screen, 0x11C) = 40;
        g_bdStreamState = 1;
        g_gfLog.clear();
        p.Update(16);
        WM(r.bdMovieUpdate, GfLogIs({"stream play", "stream video 0"}));
        g_bdStreamState = 2;
        WmField<int>(screen, 0x11C) = 0;
        g_gfLog.clear();
        p.Update(16);
        WM(r.bdMovieUpdate, GfLogIs({"stream video 0"}));
        WmField<int>(bare, 0x11C) = 0;
        // Freed: the own textures back.
        g_gfLog.clear();
        p.Free();
        WM(r.bdMovie, GfLogIs({"settex m1 own.bmp", "settex m0 own.bmp", "texrelease own.bmp", "stream stop", "stream release"}) &&
                          !p.m_stream && p.m_frames.empty() && p.m_textures.empty() && own->refs == 1 &&
                          std::string(p.m_movie.c_str()).empty());
        g_gfLog.clear();
        p.Free();
        p.Update(16);
        WM(r.bdMovie, g_gfLog.empty());
        // A movie that does not open; no file; no visual.
        g_files[name].assign(file.begin(), file.end());
        std::string missing = chunk(1, chunk(0x44C, std::string("missingfilm", 12)) + chunk(0x4B0, std::string("screen", 7)));
        g_files[name].assign(missing.begin(), missing.end());
        g_gfLog.clear();
        p.Load();
        WM(r.bdMovieFile, GfHas({"stream open GameMovie\\missingfilm 0", "stream release"}) && !p.m_stream &&
                              std::string(p.m_movie.c_str()).empty() && p.m_frames.empty());
        g_files.erase(name);
        g_gfLog.clear();
        p.Load();
        WM(r.bdMovieFile, g_gfLog.empty() && !p.m_stream);
        std::string none = chunk(1, chunk(0x44C, std::string("film2", 6)) + chunk(0x4B0, std::string("nosuch", 7)));
        g_files[name].assign(none.begin(), none.end());
        g_gfLog.clear();
        p.Load();
        WM(r.bdMovieFile, GfLogIs({"sfind nosuch ffff"}) && !p.m_stream && std::string(p.m_movie.c_str()) == "film2");
        // Only frames that are no object visuals: the movie plays, on nothing.
        std::string only = chunk(1, chunk(0x44C, std::string("film3", 6)) + chunk(0x4B0, std::string("plain", 6)));
        g_files[name].assign(only.begin(), only.end());
        g_gfLog.clear();
        p.Load();
        WM(r.bdMovieFile, GfHas({"stream open GameMovie\\film3 0", "stream play"}) && p.m_frames.empty() && p.m_stream &&
                              std::string(p.m_movie.c_str()) == "film3");
        // An empty top chunk.
        std::string empty = chunk(1, "");
        g_files[name].assign(empty.begin(), empty.end());
        g_gfLog.clear();
        p.Load();
        WM(r.bdMovieFile, GfLogIs({"stream stop", "stream release"}) && !p.m_stream);
        g_files.erase(name);
        g_gfScene.clear();
    }

    // --- through the GEffects module ---
    {
        C_GEffectsModule &m = g_geffectsModule;
        I3D_frame *a = BdModel("aptacekletacek", S_vector(0, 0, 0));
        g_gfEnumFrames = {a};
        g_gameRole = GAME_ROLE_SERVER;
        m.StartMission(true);
        WM(r.bdEffects, g_birds.size() == 1);
        g_birds[0]->m_timer = 1000.0f;
        m.Update(100);
        WM(r.bdEffects, g_birds.size() == 1 && g_birds[0]->m_timer == 900.0f);
        g_birds[0]->m_leftWing = nullptr;
        m.Update(100);
        WM(r.bdEffects, g_birds.empty());
        CollectBirds();
        m.RestartMission(false);
        WM(r.bdEffects, g_birds.size() == 1 && g_spareBirdModels.empty());
        g_gameRole = GAME_ROLE_LOCAL;
        FreeBirds();
        g_gfEnumFrames.clear();
        // The mesh player with the mission's data.
        I3D_frame *tv = GfSkinned("tv", GfMesh("tvm", {GfLevel({GfGroup("tg", GfMaterial("tm", nullptr))})}), nullptr,
                                  I3DVISUAL_OBJECT);
        g_gfScene = {{"tv", tv}};
        auto chunk = [](uint16_t id, const std::string &payload) {
            std::string out(6, '\0');
            const uint32_t size = static_cast<uint32_t>(6 + payload.size());
            std::memcpy(&out[0], &id, 2);
            std::memcpy(&out[2], &size, 4);
            return out + payload;
        };
        const std::string file = chunk(1, chunk(0x44C, std::string("news", 5)) + chunk(0x4B0, std::string("tv", 3)));
        g_files["missions\\GFX\\meshplayer.bin"].assign(file.begin(), file.end());
        g_gfLog.clear();
        m.LoadMissionData(false);
        WM(r.bdEffects, GfHas({"stream open GameMovie\\news 0", "settex tm video", "stream play"}));
        WmField<int>(tv, 0x11C) = 40;
        g_gfLog.clear();
        m.Update(16);
        WM(r.bdEffects, GfHas({"stream video 0"}));
        g_gfLog.clear();
        m.FreeMissionData(false);
        WM(r.bdEffects, GfHas({"settex tm 0", "stream stop", "stream release"}));
        g_files.erase("missions\\GFX\\meshplayer.bin");
        g_gfScene.clear();
    }

    std::memcpy(g_sceneVtable, sceneSlots, sizeof sceneSlots);
    std::memcpy(g_igraphVtable, igraphSlots, sizeof igraphSlots);
    WmField<I3D_frame *>(scene, 0x1F8) = oldPrimary;
    WmField<I3D_frame *>(scene, 0x15C) = nullptr;
    std::printf("birds: bdMake %d bdDelete %d bdSit %d bdHop %d bdFlyAway %d bdTakeOff %d bdClimb %d bdGlide %d bdModule %d "
                "bdMovie %d bdMovieFile %d bdMovieUpdate %d bdEffects %d\n",
                r.bdMake, r.bdDelete, r.bdSit, r.bdHop, r.bdFlyAway, r.bdTakeOff, r.bdClimb, r.bdGlide, r.bdModule, r.bdMovie,
                r.bdMovieFile, r.bdMovieUpdate, r.bdEffects);
}
