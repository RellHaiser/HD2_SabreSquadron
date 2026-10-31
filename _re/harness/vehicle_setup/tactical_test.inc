// Part 8e-ii's human effects (the command map's tactical camera; game/camera_module.cpp), in the
// camera build after camera_test.inc's module and keeper tests: the slide sphere, the quaternion's
// SetDir / Slerp / GetDir, the vector's RotateByMatrix, RotateYMatrix and the scene's GetSector
// faked here (the sphere test, the frames' slots and the team are camera_test.inc's and the GEffects
// test's). Prints "tactical:" with 1 per check met.

// The slide sphere: what it was asked; a hit (g_tcSlideVolume, its flags set by the test) at
// g_tcSlideEnd when g_tcSlideHit.
struct S_TcSlide {
    S_vector from, move;
    float radius;
    uint32_t types, flags;
    bool normal;
};
std::vector<S_TcSlide> g_tcSlides;
bool g_tcSlideHit;
S_vector g_tcSlideEnd;
S_CollisionVolume g_tcSlideVolume;
S_CollisionVolume *CollisionSlideSphere(const S_vector &from, const S_vector &move, float radius, S_vector &endPos,
                                        S_vector *endNormal, uint32_t excludeTypes, uint32_t excludeFlags)
{
    g_tcSlides.push_back({from, move, radius, excludeTypes, excludeFlags, endNormal != nullptr});
    if (!g_tcSlideHit)
        return nullptr;
    endPos = g_tcSlideEnd;
    return &g_tcSlideVolume;
}

// The quaternion's SetDir: the direction and roll recorded, the quaternion made {0.25, dir}.
struct S_TcQuatDir {
    const S_quat *self;
    S_vector dir;
    float roll;
};
std::vector<S_TcQuatDir> g_tcQuatDirs;
void __stdcall S_quat::SetDir(const S_vector &dir, float roll)
{
    g_tcQuatDirs.push_back({this, dir, roll});
    *this = S_quat(0.25f, dir.x, dir.y, dir.z);
}
// The slerp: its operands recorded, g_tcSlerpResult returned.
struct S_TcSlerp {
    const S_quat *self, *to;
    float t;
    bool shortest;
};
std::vector<S_TcSlerp> g_tcSlerps;
S_quat g_tcSlerpResult(0.5f, 0.5f, -0.5f, 0.5f);
S_quat __stdcall S_quat::Slerp(const S_quat &q, float t, bool shortestPath) const
{
    g_tcSlerps.push_back({this, &q, t, shortestPath});
    return g_tcSlerpResult;
}
// As LS3DF computes them (the item build's copies).
S_vector __stdcall S_quat::GetDir() const
{
    if (std::fabs(s) >= 1.0f)
        return S_vector(0.0f, 0.0f, 1.0f);
    const float xx = (v.x + v.x) * v.x;
    const float yy = (v.y + v.y) * v.y;
    const float zy = (v.z + v.z) * v.y;
    const float xw = (v.x + v.x) * s;
    return S_vector((v.z + v.z) * v.x - (v.y + v.y) * s, xw + zy, 1.0f - (yy + xx));
}
S_vector __stdcall S_vector::RotateByMatrix(const S_matrix &m) const
{
    return S_vector((x * m.m[0][0] + y * m.m[1][0]) + z * m.m[2][0], (x * m.m[0][1] + y * m.m[1][1]) + z * m.m[2][1],
                    (x * m.m[0][2] + y * m.m[1][2]) + z * m.m[2][2]);
}
S_matrix __stdcall RotateYMatrix(float angle)
{
    S_matrix r;
    r.Identity();
    const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
    const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
    r.m[0][0] = c;
    r.m[0][2] = s;
    r.m[2][0] = -s;
    r.m[2][2] = c;
    return r;
}

namespace {

// The scene's GetSector (0xE0): the position logged, g_tcSector returned.
I3D_sector *g_tcSector;
I3D_sector *__stdcall F_TcGetSector(void *, const S_vector &pos)
{
    GfLog("sector %.4f %.4f %.4f", pos.x, pos.y, pos.z);
    return g_tcSector;
}
void __stdcall F_TcDuplicate(I3D_frame *self, I3D_frame *source)
{
    GfLog("duplicate %s %s", GfName(self), GfName(source));
}
void __stdcall F_TcAspect(I3D_frame *self, float aspect)
{
    GfLog("aspect %s %.4f", GfName(self), aspect);
}

// NormalizeSummed's arithmetic.
S_vector TcNorm(S_vector v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return v;
    if (square >= 1e-8f) {
        const float k = 1.0f / std::sqrt(square);
        return S_vector(v.x * k, v.y * k, v.z * k);
    }
    if (v.x != 0.0f)
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    else if (v.z != 0.0f)
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    else
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
    return v;
}
bool TcVec(const S_vector &a, const S_vector &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool TcPos(const S_vector4 &a, float x, float y, float z)
{
    return a.x == x && a.y == y && a.z == z;
}
bool TcSlide(size_t i, const S_vector &from, const S_vector &move)
{
    if (i >= g_tcSlides.size())
        return false;
    const S_TcSlide &s = g_tcSlides[i];
    return TcVec(s.from, from) && TcVec(s.move, move) && s.radius == 0.2f && s.types == 0x18100 &&
           s.flags == 0x600000 && !s.normal;
}
bool TcSphere(size_t i, const S_vector &from, const S_vector &dir)
{
    if (i >= g_cmSpheres.size())
        return false;
    const S_CmSphere &s = g_cmSpheres[i];
    return TcVec(s.from, from) && TcVec(s.dir, dir) && s.radius == 0.2f && s.types == 0x18100 && s.flags == 0x600000;
}
std::string TcSetPos(const char *name, float x, float y, float z)
{
    return CmText("setpos %s %.4f %.4f %.4f", name, x, y, z);
}
std::string TcSetDir(const char *name, const S_vector &d)
{
    return CmText("setdir %s %.5f %.5f %.5f %.6f", name, d.x, d.y, d.z, 0.0f);
}

alignas(16) uint8_t g_tcMem[sizeof(C_CameraHumanEffects)];
// A human effects made as the module makes it (its camera "tactical"), the log cleared.
C_CameraHumanEffects *TcNew()
{
    g_cmCreate.push_back(GfFrame(FRAME_CAMERA, "tactical"));
    std::memset(g_tcMem, 0xCD, sizeof g_tcMem);
    C_CameraHumanEffects *e = new (g_tcMem) C_CameraHumanEffects;
    e->m_humans.clear();
    g_gfLog.clear();
    g_tcSlides.clear();
    g_cmSpheres.clear();
    return e;
}

// A human: its body frame at `body`, its frame at `frame` (looking along +z), named for the log.
C_Human *TcHuman(const char *name, const S_vector &body, const S_vector &frame)
{
    static std::deque<std::vector<uint8_t>> mems;
    mems.emplace_back(sizeof(C_Human) + 16, 0);
    uint8_t *p = mems.back().data();
    p += (16 - reinterpret_cast<uintptr_t>(p) % 16) % 16;
    C_Human *h = reinterpret_cast<C_Human *>(p);
    h->m_type = ACTOR_HUMAN;
    h->bodyFrame = GfFrame(FRAME_DUMMY, std::string(name) + "-body");
    h->bodyFrame->m_localMatrix(3) = body;
    h->m_frame = GfFrame(FRAME_MODEL, name);
    h->m_frame->m_localMatrix(3) = frame;
    g_cmHumanNames[h] = name;
    return h;
}

void TacticalTest(S_CameraResults &r)
{
    C_game *game = GetGame();
    I3D_scene *scene = game->GetScene();
    g_sceneVtable[0xE0 / 4] = reinterpret_cast<void *>(&F_TcGetSector);
    g_gfCameraVt[0x4C / 4] = reinterpret_cast<void *>(&F_TcDuplicate);
    g_gfCameraVt[0x8C / 4] = reinterpret_cast<void *>(&F_TcAspect);
    I3D_frame *const oldPrimary = WmField<I3D_frame *>(scene, 0x1F8);
    I3D_frame *primary = GfFrame(FRAME_SECTOR, "primary");
    I3D_frame *outside = GfFrame(FRAME_SECTOR, "outside");
    WmField<I3D_frame *>(scene, 0x1F8) = primary;
    g_tcSector = reinterpret_cast<I3D_sector *>(primary);
    g_sphereTestHook = &CmSphereHook;
    g_sphereHookSetsHit = true;
    g_cmHit = false;
    g_tcSlideHit = false;
    g_gfMembers.clear();
    std::vector<S_EnemyRecord *> &enemies = g_teamManager.GetPlayerTeam()->GetEnemyRecords().m_records;
    enemies.clear();

    // --- made; the zoom, the motion, the state ---
    {
        C_CameraHumanEffects *e = TcNew();
        I3D_frame *cam = reinterpret_cast<I3D_frame *>(e->m_camera);
        WM(r.tcBasics, std::strcmp(GfName(cam), "tactical") == 0 && !e->m_gameCamera && !e->IsTactical() &&
                           e->m_zoom == 1.0f && e->m_height == 5.0f && e->m_motionX == 0.0f && e->m_motionY == 0.0f &&
                           e->m_clickTime == 0 && e->m_humans.empty());
        e->SetTacticalZoom(2.5f);
        e->SetTacticalMotion(1.5f, -2.0f);
        e->m_tactical = true;
        WM(r.tcBasics, e->m_zoom == 2.5f && e->m_motionX == 1.5f && e->m_motionY == -2.0f && e->IsTactical());
        e->~C_CameraHumanEffects();
        WM(r.tcBasics, GfLogIs({"release tactical"}));
    }

    // --- on: behind and above the active soldier; off: the scene's camera back ---
    {
        C_CameraHumanEffects *e = TcNew();
        I3D_frame *sceneCam = CmCamera("scenecam", S_vector(0.0f, 0.0f, 0.0f));
        WmField<float>(sceneCam, 0x138) = 1.5f;
        CmActive() = sceneCam;
        const float oldFov = game->m_fov, oldNear = game->m_nearClip, oldFar = game->m_farClip;
        game->m_fov = 1.1f;
        game->m_nearClip = 0.5f;
        game->m_farClip = 300.0f;
        C_Human *player = TcHuman("player", S_vector(10.0f, 1.0f, 20.0f), S_vector(0.0f, 0.0f, 0.0f));
        player->bodyFrame->m_localMatrix.m[3][3] = 0.75f;
        player->m_frame->m_localMatrix.m[2][0] = 3.0f;
        player->m_frame->m_localMatrix.m[2][1] = 0.5f;
        player->m_frame->m_localMatrix.m[2][2] = 4.0f;
        g_gfActive = player;
        e->m_flightSpeed = 9.0f;
        e->m_mode = TACTICAL_DRAG;
        e->SetTactical(true);
        const S_vector view = TcNorm(S_vector(3.0f, 0.5f, 4.0f), (4.0f * 4.0f + 3.0f * 3.0f) + 0.5f * 0.5f);
        const S_vector flat = TcNorm(S_vector(view.x, 0.0f, view.z), view.z * view.z + view.x * view.x);
        const S_vector move((10.0f - flat.x * 7.0f) - 10.0f, ((1.0f - flat.y * 7.0f) + 3.0f) - 1.0f,
                            (20.0f - flat.z * 7.0f) - 20.0f);
        const S_vector pos(10.0f + move.x, move.y + 1.0f, move.z + 20.0f);
        const S_vector dir = TcNorm(S_vector(flat.x, -0.35f, flat.z), (flat.z * flat.z + flat.x * flat.x) + 0.35f * 0.35f);
        WM(r.tcOn, GfVecIs(g_gfLog, {"duplicate tactical scenecam", "fov tactical 1.1000", "range tactical 0.5 300.0",
                                     "aspect tactical 1.5000", TcSetPos("tactical", pos.x, pos.y, pos.z),
                                     "update tactical", TcSetDir("tactical", dir), "update tactical",
                                     "active tactical"}));
        WM(r.tcOn, e->IsTactical() && e->m_mode == TACTICAL_IDLE && e->m_flightSpeed == 0.0f &&
                       e->m_gameCamera == reinterpret_cast<I3D_camera *>(sceneCam) && CmActive() == e->m_camera &&
                       TcPos(e->m_pos, pos.x, pos.y, pos.z) && e->m_pos.w == 0.75f && TcVec(g_cmLastDir, dir) &&
                       g_cmLastRoll == 0.0f && g_tcSlides.size() == 1 && TcSlide(0, S_vector(10.0f, 1.0f, 20.0f), move));
        g_gfLog.clear();
        e->SetTactical(true);
        WM(r.tcOn, g_gfLog.empty() && e->IsTactical());

        // Off: the scene's camera back, the module's Unknown40(0), the faded humans back.
        C_Human *h1 = TcHuman("h1", S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, 0.0f));
        C_Human *h2 = TcHuman("h2", S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, 0.0f));
        e->m_humans = {{h1, 0.5f}, {h2, 1.0f}};
        e->m_mode = TACTICAL_MOVE;
        g_gfLog.clear();
        e->SetTactical(false);
        WM(r.tcOff, GfLogIs({"active scenecam", "cam40 0", "unknown94 h2 0.0000", "unknown94 h1 0.0000"}) &&
                        !e->IsTactical() && e->m_mode == TACTICAL_IDLE && e->m_humans.empty() && CmActive() == sceneCam);
        g_gfLog.clear();
        e->SetTactical(false);
        WM(r.tcOff, g_gfLog.empty());

        // Looking straight up: the flat direction y (its axis).
        player->m_frame->m_localMatrix.m[2][0] = 0.0f;
        player->m_frame->m_localMatrix.m[2][1] = 2.0f;
        player->m_frame->m_localMatrix.m[2][2] = 0.0f;
        g_tcSlides.clear();
        e->SetTactical(true);
        WM(r.tcOn, TcSlide(0, S_vector(10.0f, 1.0f, 20.0f), S_vector(0.0f, ((1.0f - 7.0f) + 3.0f) - 1.0f, 0.0f)) &&
                       TcVec(g_cmLastDir, TcNorm(S_vector(0.0f, -0.35f, 0.0f), 0.35f * 0.35f)));
        // A soldier's view along -x (the sum's order).
        e->SetTactical(false);
        player->m_frame->m_localMatrix.m[2][0] = -0.3f;
        player->m_frame->m_localMatrix.m[2][1] = 0.7f;
        player->m_frame->m_localMatrix.m[2][2] = 0.1f;
        g_tcSlides.clear();
        e->SetTactical(true);
        {
            const S_vector v2 = TcNorm(S_vector(-0.3f, 0.7f, 0.1f), (0.1f * 0.1f + -0.3f * -0.3f) + 0.7f * 0.7f);
            const S_vector f2 = TcNorm(S_vector(v2.x, 0.0f, v2.z), v2.z * v2.z + v2.x * v2.x);
            WM(r.tcOn, TcSlide(0, S_vector(10.0f, 1.0f, 20.0f),
                               S_vector((10.0f - f2.x * 7.0f) - 10.0f, ((1.0f - f2.y * 7.0f) + 3.0f) - 1.0f,
                                        (20.0f - f2.z * 7.0f) - 20.0f)));
        }
        game->m_fov = oldFov;
        game->m_nearClip = oldNear;
        game->m_farClip = oldFar;
        g_gfActive = nullptr;
        CmActive() = nullptr;
    }

    // --- the move: slides, 5 cm off what it hits ---
    {
        C_CameraHumanEffects *e = TcNew();
        e->m_pos = {4.0f, 5.0f, 6.0f, 0.75f};
        S_CollisionVolume *hit = e->Move(S_vector(1.0f, -2.0f, 0.5f));
        WM(r.tcMove, !hit && TcPos(e->m_pos, 4.0f + 1.0f, -2.0f + 5.0f, 0.5f + 6.0f) && e->m_pos.w == 0.75f &&
                         TcSlide(0, S_vector(4.0f, 5.0f, 6.0f), S_vector(1.0f, -2.0f, 0.5f)) &&
                         GfLogIs({TcSetPos("tactical", 5.0f, 3.0f, 6.5f), "update tactical"}));
        g_tcSlideHit = true;
        g_tcSlideEnd = S_vector(1.0f, 1.0f, 1.0f);
        e->m_pos = {4.0f, 5.0f, 6.0f, 0.75f};
        g_gfLog.clear();
        hit = e->Move(S_vector(-9.0f, -9.0f, -9.0f));
        {
            const S_vector back = TcNorm(S_vector(3.0f, 4.0f, 5.0f), (5.0f * 5.0f + 4.0f * 4.0f) + 3.0f * 3.0f);
            WM(r.tcMove, hit == &g_tcSlideVolume &&
                             TcPos(e->m_pos, back.x * 0.05f + 1.0f, back.y * 0.05f + 1.0f, back.z * 0.05f + 1.0f) &&
                             e->m_pos.w == 0.75f && g_gfLog.size() == 2 && g_gfLog[1] == "update tactical");
        }
        // Stopped where it was: 5 cm up.
        e->m_pos = {1.0f, 1.0f, 1.0f, 0.75f};
        hit = e->Move(S_vector(0.0f, -1.0f, 0.0f));
        WM(r.tcMove, hit && TcPos(e->m_pos, 1.0f, 1.0f * 0.05f + 1.0f, 1.0f));
        g_tcSlideHit = false;
    }

    // --- looked at: behind and above a human, his vehicle's, the game camera's view ---
    {
        C_CameraHumanEffects *e = TcNew();
        I3D_frame *kept = CmCamera("kept", S_vector(1.0f, 2.0f, 3.0f));
        kept->m_localMatrix.m[3][3] = 0.5f;
        kept->m_localMatrix.m[2][0] = 0.0f;
        kept->m_localMatrix.m[2][1] = -3.0f;
        kept->m_localMatrix.m[2][2] = 4.0f;
        e->m_gameCamera = reinterpret_cast<I3D_camera *>(kept);
        e->LookAt(nullptr);
        {
            const S_vector d = S_vector(0.0f, -3.0f, 4.0f).NormalizedGame();
            const S_vector move(-(d.x * 3.0f), 1.5f - d.y * 3.0f, -(d.z * 3.0f));
            const S_vector pos(1.0f + move.x, move.y + 2.0f, move.z + 3.0f);
            WM(r.tcLookAt, TcSlide(0, S_vector(1.0f, 2.0f, 3.0f), move) && TcPos(e->m_pos, pos.x, pos.y, pos.z) &&
                               e->m_pos.w == 0.5f &&
                               TcVec(g_cmLastDir, S_vector(1.0f - pos.x, 2.0f - pos.y, 3.0f - pos.z)) &&
                               GfLogIs({TcSetPos("tactical", pos.x, pos.y, pos.z), "update tactical",
                                        TcSetDir("tactical", g_cmLastDir), "update tactical"}));
        }
        C_Human *h = TcHuman("looked", S_vector(5.0f, 0.0f, 7.0f), S_vector(0.0f, 0.0f, 0.0f));
        h->lookDir = S_vector(0.6f, -0.1f, 0.8f);
        alignas(16) static uint8_t vehicleMem[sizeof(C_Car)];
        C_Actor *vehicle = reinterpret_cast<C_Actor *>(vehicleMem);
        struct S_Case {
            C_Actor *vehicle;
            int type;
            bool raised;
        };
        const S_Case cases[] = {{nullptr, 0, false},
                                {vehicle, ACTOR_CAR, true},
                                {vehicle, ACTOR_SUBMARINE, true},
                                {vehicle, ACTOR_STATIC_WEAPON, true},
                                {vehicle, ACTOR_JUNKERS88, true},
                                {vehicle, ACTOR_HUMAN, false},
                                {vehicle, ACTOR_TRAIN, false}};
        for (const S_Case &c : cases) {
            h->vehicle = c.vehicle;
            vehicle->m_type = static_cast<E_ActorType>(c.type);
            g_tcSlides.clear();
            g_gfLog.clear();
            e->LookAt(h);
            const float back = c.raised ? 5.0f : 3.0f;
            const float y = c.raised ? 0.0f + 1.5f : 0.0f;
            const S_vector move(-(0.6f * back), 1.5f - -0.1f * back, -(0.8f * back));
            const S_vector pos(5.0f + move.x, move.y + y, move.z + 7.0f);
            WM(r.tcLookAt, TcSlide(0, S_vector(5.0f, y, 7.0f), move) && TcPos(e->m_pos, pos.x, pos.y, pos.z) &&
                               TcVec(g_cmLastDir, S_vector(5.0f - pos.x, 0.0f - pos.y, 7.0f - pos.z)) &&
                               g_gfLog.size() == 4);
        }
        h->vehicle = nullptr;
    }

    // --- the mode: the look point, the map point, the double click's flight ---
    {
        C_CameraHumanEffects *e = TcNew();
        I3D_frame *cam = reinterpret_cast<I3D_frame *>(e->m_camera);
        cam->m_localMatrix.m[2][0] = 0.0f;
        cam->m_localMatrix.m[2][1] = 0.0f;
        cam->m_localMatrix.m[2][2] = 2.0f;
        cam->m_localMatrix.m[2][3] = 0.125f;
        e->m_pos = {1.0f, 2.0f, 3.0f, 1.0f};
        e->m_mode = TACTICAL_IDLE;
        e->m_lookPoint = {7.0f, 7.0f, 7.0f, 7.0f};
        e->m_target = {9.0f, 9.0f, 9.0f, 9.0f};
        e->SetTacticalMode(TACTICAL_IDLE, nullptr);
        WM(r.tcMode, g_gfLog.empty() && CmVec4(e->m_lookPoint, 7.0f, 7.0f, 7.0f, 7.0f));
        const S_vector4 point = {4.0f, 0.0f, -4.0f, 1.0f};
        e->SetTacticalMode(TACTICAL_MOVE, &point);
        WM(r.tcMode, e->m_mode == TACTICAL_MOVE && GfLogIs({"sector 1.0000 2.0000 3.0000"}) &&
                         CmVec4(e->m_lookPoint, 0.0f * 10.0f + 1.0f, 0.0f * 10.0f + 2.0f, 1.0f * 10.0f + 3.0f, 0.125f) &&
                         e->m_lookDistance == 10.0f && CmVec4(e->m_target, 4.0f, 0.0f, -4.0f, 1.0f) && e->m_clickTime == 0);
        // Outside the primary sector: 2 m.
        g_tcSector = reinterpret_cast<I3D_sector *>(outside);
        e->SetTacticalMode(TACTICAL_TURN, nullptr);
        WM(r.tcMode, e->m_mode == TACTICAL_TURN && e->m_lookDistance == 2.0f &&
                         CmVec4(e->m_lookPoint, 1.0f, 2.0f, 1.0f * 2.0f + 3.0f, 0.125f) &&
                         CmVec4(e->m_target, 4.0f, 0.0f, -4.0f, 1.0f));
        g_tcSector = reinterpret_cast<I3D_sector *>(primary);
        // From the turns (and from 10 .. 12) the point is not moved; from 1, 3, 8, 9 it is.
        for (int from = 0; from <= 12; ++from) {
            e->m_mode = from;
            e->m_flightTime = 500.0f;
            e->m_lookPoint = {7.0f, 7.0f, 7.0f, 7.0f};
            g_gfLog.clear();
            e->SetTacticalMode(from == TACTICAL_UP ? TACTICAL_DOWN : TACTICAL_UP, nullptr);
            const bool moved = from == 0 || from == 1 || from == 3 || from == 8 || from == 9;
            WM(r.tcMode, (e->m_lookPoint.x != 7.0f) == moved && g_gfLog.size() == (moved ? 1u : 0u));
        }
        // A drag: 500 ms for the second click; idle keeps the time.
        e->m_mode = TACTICAL_IDLE;
        e->m_clickTime = 0;
        e->SetTacticalMode(TACTICAL_DRAG, &point);
        WM(r.tcMode, e->m_mode == TACTICAL_DRAG && e->m_clickTime == 500);
        e->SetTacticalMode(TACTICAL_IDLE, nullptr);
        WM(r.tcMode, e->m_mode == TACTICAL_IDLE && e->m_clickTime == 500);
        // The second: the flight (its speed reset).
        e->m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
        const S_vector4 distant = {12.0f, 0.0f, 0.0f, 1.0f};
        e->m_flightSpeed = 4.0f;
        e->m_keepSpeed = false;
        g_cmSpheres.clear();
        e->SetTacticalMode(TACTICAL_DRAG, &distant);
        WM(r.tcMode, e->m_mode == TACTICAL_FLY && e->m_clickTime == 0 && e->m_flightSpeed == 0.0f && !e->m_keepSpeed &&
                         g_cmSpheres.size() == 1 && TcPos(e->m_target, 12.0f * 0.75f, 0.0f, 0.0f));
        // Flying: idle and an early drag do nothing; a drag after 300 ms keeps the speed for the
        // next flight.
        e->m_flightTime = 299.0f;
        e->m_flightSpeed = 3.0f;
        e->SetTacticalMode(TACTICAL_IDLE, &point);
        e->SetTacticalMode(TACTICAL_DRAG, &point);
        WM(r.tcMode, e->m_mode == TACTICAL_FLY && TcPos(e->m_target, 9.0f, 0.0f, 0.0f) && !e->m_keepSpeed &&
                         e->m_flightTime == 299.0f);
        e->m_flightTime = 300.0f;
        g_gfLog.clear();
        e->SetTacticalMode(TACTICAL_DRAG, &point);
        WM(r.tcMode, e->m_mode == TACTICAL_DRAG && e->m_keepSpeed && e->m_clickTime == 500 &&
                         CmVec4(e->m_target, 4.0f, 0.0f, -4.0f, 1.0f) && e->m_flightTime == 300.0f && g_gfLog.empty());
        g_cmSpheres.clear();
        e->SetTacticalMode(TACTICAL_IDLE, nullptr);
        e->SetTacticalMode(TACTICAL_DRAG, &distant);
        WM(r.tcMode, e->m_mode == TACTICAL_FLY && e->m_flightSpeed == 3.0f && !e->m_keepSpeed && e->m_clickTime == 0);
        // Another mode ends it (the time reset).
        e->m_flightTime = 50.0f;
        e->SetTacticalMode(TACTICAL_MOVE, nullptr);
        WM(r.tcMode, e->m_mode == TACTICAL_MOVE && e->m_flightTime == 0.0f);
    }

    // --- the flight's start ---
    {
        C_CameraHumanEffects *e = TcNew();
        I3D_frame *cam = reinterpret_cast<I3D_frame *>(e->m_camera);
        cam->m_localMatrix.Identity();
        S_quat made;
        made.Make(cam->GetMatrix());
        struct S_Case {
            float distance, seconds;
        };
        const S_Case cases[] = {{10.0f, 1.875f}, {4.0f, 1.25f}, {5.0f, 1.875f}, {34.5f, 1.875f}, {35.0f, 2.5f},
                                {80.0f, 2.5f}};
        for (const S_Case &c : cases) {
            e->m_pos = {1.0f, 2.0f, 3.0f, 1.0f};
            e->m_target = {1.0f, 2.0f, 3.0f + c.distance, 0.5f};
            e->m_flightTime = 77.0f;
            g_cmSpheres.clear();
            g_tcQuatDirs.clear();
            e->StartFlight();
            const S_vector move(0.0f * 0.75f, 0.0f * 0.75f, ((3.0f + c.distance) - 3.0f) * 0.75f);
            const float third = c.seconds * 0.33333334f;
            WM(r.tcFlight, TcSphere(0, S_vector(1.0f, 2.0f, 3.0f), S_vector(0.0f, 0.0f, (3.0f + c.distance) - 3.0f)) &&
                               e->m_flightDistance == move.z && e->m_flightTime == 0.0f &&
                               e->m_flightAccel == (move.z * 0.6666667f) / (third * third) &&
                               TcPos(e->m_target, 1.0f, 2.0f, move.z + 3.0f) && e->m_target.w == 0.5f &&
                               KpQuat(e->m_flightFrom, made.s, made.v.x, made.v.y, made.v.z) &&
                               g_tcQuatDirs.size() == 1 && g_tcQuatDirs[0].self == &e->m_flightTo &&
                               TcVec(g_tcQuatDirs[0].dir, move) && g_tcQuatDirs[0].roll == 0.0f &&
                               KpQuat(e->m_flightTo, 0.25f, move.x, move.y, move.z));
        }
        // An odd direction: the sum's order.
        e->m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
        e->m_target = {3.3f, -1.7f, 2.9f, 1.0f};
        e->StartFlight();
        {
            const float length = std::sqrt((2.9f * 2.9f + -1.7f * -1.7f) + 3.3f * 3.3f);
            const S_vector move(3.3f * 0.75f, -1.7f * 0.75f, 2.9f * 0.75f);
            const float distance = std::sqrt((move.z * move.z + move.y * move.y) + move.x * move.x);
            const float third = (length < 5.0f ? 1.25f : 1.875f) * 0.33333334f;
            WM(r.tcFlight, e->m_flightDistance == distance && e->m_flightAccel == (distance * 0.6666667f) / (third * third) &&
                               TcPos(e->m_target, move.x + 0.0f, move.y + 0.0f, move.z + 0.0f));
        }
        // Within 1 m: not moved (the distance and acceleration 0); exactly 1: not moved either.
        // Just beyond: moved.
        e->m_pos = {1.0f, 2.0f, 3.0f, 1.0f};
        e->m_target = {2.25f, 2.0f, 3.0f, 1.0f};
        e->StartFlight();
        WM(r.tcFlight, e->m_flightDistance == 1.25f * 0.75f && TcPos(e->m_target, 1.25f * 0.75f + 1.0f, 2.0f, 3.0f));
        // 5.2 m across all three axes (each axis' part of the sum counts).
        e->m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
        e->m_target = {3.0f, 3.0f, 3.0f, 1.0f};
        e->StartFlight();
        {
            const float distance = std::sqrt((2.25f * 2.25f + 2.25f * 2.25f) + 2.25f * 2.25f);
            const float third = 1.875f * 0.33333334f;
            WM(r.tcFlight, e->m_flightDistance == distance && e->m_flightAccel == (distance * 0.6666667f) / (third * third));
        }
        for (float d : {0.5f, 1.0f, 0.00001f}) {
            e->m_pos = {1.0f, 2.0f, 3.0f, 1.0f};
            e->m_target = {1.0f + d, 2.0f, 3.0f, 1.0f};
            g_tcQuatDirs.clear();
            e->StartFlight();
            WM(r.tcFlight, e->m_flightDistance == 0.0f && e->m_flightAccel == 0.0f && TcPos(e->m_target, 1.0f, 2.0f, 3.0f) &&
                               g_tcQuatDirs.size() == 1 && TcVec(g_tcQuatDirs[0].dir, S_vector(d * 0.0f, 0.0f, 0.0f)));
        }
        // Something in the way: the point moved to it.
        g_cmHit = true;
        g_sphereHookHit = S_vector(1.0f, 2.0f, 11.0f);
        e->m_pos = {1.0f, 2.0f, 3.0f, 1.0f};
        e->m_target = {6.0f, -3.0f, 40.0f, 0.25f};
        e->StartFlight();
        WM(r.tcFlight, TcPos(e->m_target, 0.0f * 0.75f + 1.0f, 0.0f * 0.75f + 2.0f, (11.0f - 3.0f) * 0.75f + 3.0f) &&
                           e->m_target.w == 0.25f &&
                           e->m_flightDistance == (11.0f - 3.0f) * 0.75f &&
                           e->m_flightAccel == ((11.0f - 3.0f) * 0.75f * 0.6666667f) /
                                                   ((1.875f * 0.33333334f) * (1.875f * 0.33333334f)));
        g_cmHit = false;
    }

    // --- the update by mode ---
    {
        C_CameraHumanEffects *e = TcNew();
        I3D_frame *cam = reinterpret_cast<I3D_frame *>(e->m_camera);
        // Its right axis (0.8, 0.1, -0.6), its view (0.6, 0.2, 0.8) not unit.
        cam->m_localMatrix.Identity();
        cam->m_localMatrix.m[0][0] = 0.8f;
        cam->m_localMatrix.m[0][1] = 0.1f;
        cam->m_localMatrix.m[0][2] = -0.6f;
        cam->m_localMatrix.m[2][0] = 0.6f;
        cam->m_localMatrix.m[2][1] = 0.2f;
        cam->m_localMatrix.m[2][2] = 0.8f;
        const S_vector right(0.8f, 0.1f, -0.6f);
        const S_vector view = TcNorm(S_vector(0.6f, 0.2f, 0.8f), (0.6f * 0.6f + 0.2f * 0.2f) + 0.8f * 0.8f);
        const S_vector flat = TcNorm(S_vector(view.x, 0.0f, view.z), view.z * view.z + view.x * view.x);
        e->m_zoom = 2.0f;
        const float t = 100.0f * 2.0f * 0.001f;
        const S_vector start(1.0f, 2.0f, 3.0f);
        auto reset = [&](int mode, float mx, float my) {
            e->m_pos = {start.x, start.y, start.z, 1.0f};
            e->m_mode = mode;
            e->m_motionX = mx;
            e->m_motionY = my;
            e->m_clickTime = 0;
            g_tcSlides.clear();
            g_cmSpheres.clear();
            g_gfLog.clear();
        };

        // Idle: only the sector asked.
        reset(TACTICAL_IDLE, 1.0f, 1.0f);
        e->UpdateTactical(100);
        WM(r.tcUpdate, GfLogIs({"sector 1.0000 2.0000 3.0000"}) && g_tcSlides.empty());
        reset(13, 1.0f, 1.0f);
        e->UpdateTactical(100);
        WM(r.tcUpdate, g_gfLog.size() == 1 && g_tcSlides.empty());
        // The double click's time runs out.
        reset(TACTICAL_IDLE, 0.0f, 0.0f);
        e->m_clickTime = 150;
        e->m_keepSpeed = true;
        e->UpdateTactical(100);
        WM(r.tcUpdate, e->m_clickTime == 50 && e->m_keepSpeed);
        e->UpdateTactical(100);
        WM(r.tcUpdate, e->m_clickTime == 0 && !e->m_keepSpeed);
        e->m_keepSpeed = true;
        e->m_keepSpeed = true;
        e->m_clickTime = 101;
        e->UpdateTactical(100);
        WM(r.tcUpdate, e->m_clickTime == 1 && e->m_keepSpeed);
        e->m_clickTime = 100;
        e->UpdateTactical(100);
        WM(r.tcUpdate, e->m_clickTime == 0 && !e->m_keepSpeed);
        e->m_keepSpeed = true;
        e->UpdateTactical(100);
        WM(r.tcUpdate, e->m_clickTime == 0 && e->m_keepSpeed);

        // Moved: sideways, then forward along the ground; a quarter outside the primary sector.
        for (float factor : {1.0f, 0.25f}) {
            g_tcSector = reinterpret_cast<I3D_sector *>(factor == 1.0f ? primary : outside);
            reset(TACTICAL_MOVE, 1.5f, -2.0f);
            e->UpdateTactical(100);
            const S_vector side(right.x * factor * t * 10.0f * -1.5f, right.y * factor * t * 10.0f * -1.5f,
                                right.z * factor * t * 10.0f * -1.5f);
            const S_vector after(start.x + side.x, side.y + start.y, side.z + start.z);
            const S_vector forward(flat.x * factor * t * 10.0f * -2.0f, flat.y * factor * t * 10.0f * -2.0f,
                                   flat.z * factor * t * 10.0f * -2.0f);
            WM(r.tcMoveMode, g_tcSlides.size() == 2 && TcSlide(0, start, side) && TcSlide(1, after, forward) &&
                                 TcPos(e->m_pos, after.x + forward.x, forward.y + after.y, forward.z + after.z));
        }
        g_tcSector = reinterpret_cast<I3D_sector *>(primary);
        reset(TACTICAL_MOVE, 0.0f, 1.0f);
        e->UpdateTactical(100);
        WM(r.tcMoveMode, g_tcSlides.size() == 1 && TcSlide(0, start, S_vector(flat.x * t * 10.0f, 0.0f, flat.z * t * 10.0f)));
        reset(TACTICAL_MOVE, -1.0f, 0.0f);
        e->UpdateTactical(100);
        WM(r.tcMoveMode, g_tcSlides.size() == 1 && TcSlide(0, start, S_vector(right.x * t * 10.0f, right.y * t * 10.0f,
                                                                              right.z * t * 10.0f)));
        reset(TACTICAL_MOVE, 0.0f, 0.0f);
        e->UpdateTactical(100);
        WM(r.tcMoveMode, g_tcSlides.empty());

        // Turned: pitched about its right axis, then about y.
        for (int mode : {static_cast<int>(TACTICAL_TURN), 4, 5, 6, 7}) {
            reset(mode, 0.5f, -0.25f);
            e->UpdateTactical(100);
            float angle = t * -0.25f * -2.1f;
            S_quat q;
            q.Make(right, angle);
            const S_vector pitched = view.RotateByMatrix(q.RotationMatrix());
            const S_vector want = pitched.RotateByMatrix(RotateYMatrix(t * 0.5f * -2.1f));
            WM(r.tcTurn, TcVec(g_cmLastDir, want) && g_cmLastRoll == 0.0f && g_tcSlides.empty() &&
                             GfVecIs(g_gfLog, {"sector 1.0000 2.0000 3.0000", TcSetDir("tactical", want),
                                               "update tactical"}));
        }
        // Past the limits.
        for (float y : {0.995f, -0.995f}) {
            const float z = std::sqrt(1.0f - y * y);
            cam->m_localMatrix.m[2][0] = 0.0f;
            cam->m_localMatrix.m[2][1] = y;
            cam->m_localMatrix.m[2][2] = z;
            const S_vector v = TcNorm(S_vector(0.0f, y, z), (0.0f + y * y) + z * z);
            const float my = y > 0.0f ? -1.0f : 1.0f;
            reset(TACTICAL_TURN, 0.0f, my);
            e->UpdateTactical(100);
            const double pitch = std::asin(static_cast<double>(v.y));
            float angle = t * my * -2.1f;
            const float sum = static_cast<float>(angle + pitch);
            if (y > 0.0f)
                angle = static_cast<float>(pitch - 1.5533430f);
            else
                angle = static_cast<float>(-1.5533430f - pitch);
            S_quat q;
            q.Make(right, angle);
            const S_vector want = v.RotateByMatrix(q.RotationMatrix()).RotateByMatrix(RotateYMatrix(t * 0.0f * -2.1f));
            WM(r.tcTurn, (y > 0.0f ? sum >= 1.5533430f : sum <= -1.5533430f) && TcVec(g_cmLastDir, want));
        }
        // Up to the limit exactly is past it (the view 30 degrees up, then down).
        {
            for (float target : {1.5533430f, -1.5533430f}) {
                const float y = target > 0.0f ? 0.5f : -0.5f;
                const float z = std::sqrt(0.75f);
                cam->m_localMatrix.m[2][1] = y;
                cam->m_localMatrix.m[2][2] = z;
                const S_vector v = TcNorm(S_vector(0.0f, y, z), (0.0f * 0.0f + y * y) + z * z);
                const double pitch = std::asin(static_cast<double>(v.y));
                // The motion that brings the sum exactly there.
                auto sumOf = [&](float my) { return static_cast<float>(t * my * -2.1f + pitch); };
                const float my0 = static_cast<float>((target - pitch) / (t * -2.1));
                float my = my0, lo = my0, hi = my0;
                bool exact = sumOf(my0) == target;
                for (int k = 0; k < 5000 && !exact; ++k) {
                    lo = std::nextafter(lo, -1e9f);
                    hi = std::nextafter(hi, 1e9f);
                    if (sumOf(lo) == target) {
                        my = lo;
                        exact = true;
                    } else if (sumOf(hi) == target) {
                        my = hi;
                        exact = true;
                    }
                }
                reset(TACTICAL_TURN, 0.0f, my);
                e->UpdateTactical(100);
                const float angle = target > 0.0f ? static_cast<float>(pitch - 1.5533430f)
                                                  : static_cast<float>(-1.5533430f - pitch);
                S_quat q;
                q.Make(right, angle);
                const S_vector want = v.RotateByMatrix(q.RotationMatrix()).RotateByMatrix(RotateYMatrix(t * 0.0f * -2.1f));
                WM(r.tcTurn, exact && TcVec(g_cmLastDir, want));
            }
            const float z = std::sqrt(0.75f);
            cam->m_localMatrix.m[2][1] = 0.5f;
            cam->m_localMatrix.m[2][2] = z;
            const S_vector v = TcNorm(S_vector(0.0f, 0.5f, z), (0.0f * 0.0f + 0.5f * 0.5f) + z * z);
            // Within the limits: the angle as asked.
            for (float my : {-0.4f, 0.4f}) {
                reset(TACTICAL_TURN, 0.0f, my);
                e->UpdateTactical(100);
                const float angle = t * my * -2.1f;
                S_quat q;
                q.Make(right, angle);
                const S_vector want = v.RotateByMatrix(q.RotationMatrix()).RotateByMatrix(RotateYMatrix(t * 0.0f * -2.1f));
                WM(r.tcTurn, TcVec(g_cmLastDir, want));
            }
        }
        cam->m_localMatrix.m[2][0] = 0.6f;
        cam->m_localMatrix.m[2][1] = 0.2f;
        cam->m_localMatrix.m[2][2] = 0.8f;

        // Dragged: the ground follows the cursor.
        for (float factor : {1.0f, 0.25f}) {
            g_tcSector = reinterpret_cast<I3D_sector *>(factor == 1.0f ? primary : outside);
            reset(TACTICAL_DRAG, 0.3f, -0.7f);
            e->UpdateTactical(100);
            const S_vector a(flat.x * t * 10.0f * -0.7f, flat.y * t * 10.0f * -0.7f, flat.z * t * 10.0f * -0.7f);
            const S_vector b(right.x * t * 10.0f * 0.3f, right.y * t * 10.0f * 0.3f, right.z * t * 10.0f * 0.3f);
            const S_vector d(a.x * 100.0f - b.x * 100.0f, a.y * 100.0f - b.y * 100.0f, a.z * 100.0f - b.z * 100.0f);
            WM(r.tcDrag, g_tcSlides.size() == 1 && TcSlide(0, start, S_vector(d.x * factor, d.y * factor, d.z * factor)));
        }
        g_tcSector = reinterpret_cast<I3D_sector *>(primary);
        reset(TACTICAL_DRAG, 0.0f, 0.0f);
        e->UpdateTactical(100);
        WM(r.tcDrag, g_tcSlides.empty());
        // Only sideways (the forward part 0).
        reset(TACTICAL_DRAG, 1.0f, 0.0f);
        e->UpdateTactical(100);
        WM(r.tcDrag, g_tcSlides.size() == 1 &&
                         TcSlide(0, start, S_vector(0.0f * 100.0f - right.x * t * 10.0f * 1.0f * 100.0f,
                                                    0.0f * 100.0f - right.y * t * 10.0f * 1.0f * 100.0f,
                                                    0.0f * 100.0f - right.z * t * 10.0f * 1.0f * 100.0f)));

        // Up and down: 12 m/s, through a water surface when there is room.
        for (int mode : {TACTICAL_UP, TACTICAL_DOWN}) {
            const float sign = mode == TACTICAL_UP ? 1.0f : -1.0f;
            for (float factor : {1.0f, 0.25f}) {
                g_tcSector = reinterpret_cast<I3D_sector *>(factor == 1.0f ? primary : outside);
                reset(mode, 0.0f, 0.0f);
                e->UpdateTactical(100);
                WM(r.tcUpDown, g_tcSlides.size() == 1 && TcSlide(0, start, S_vector(0.0f, t * factor * (sign * 12.0f), 0.0f)) &&
                                   TcPos(e->m_pos, start.x, start.y + t * factor * (sign * 12.0f), start.z) &&
                                   g_cmSpheres.empty());
            }
            g_tcSector = reinterpret_cast<I3D_sector *>(primary);
            g_tcSlideHit = true;
            g_tcSlideEnd = S_vector(1.0f, 2.0f + sign * 0.5f, 3.0f);
            const S_vector stopped(1.0f, (g_tcSlideEnd.y + -sign * 0.05f), 3.0f);
            // Not water: stopped.
            g_tcSlideVolume.flags = 0xFFFEFFFFu;
            reset(mode, 0.0f, 0.0f);
            e->UpdateTactical(100);
            WM(r.tcUpDown, g_cmSpheres.empty() && TcPos(e->m_pos, stopped.x, stopped.y, stopped.z));
            // Water with room: 0.4 m on.
            g_tcSlideVolume.flags = COLLISION_FLAG_WATER;
            reset(mode, 0.0f, 0.0f);
            e->UpdateTactical(100);
            WM(r.tcUpDown, g_cmSpheres.size() == 1 && TcSphere(0, stopped, S_vector(0.0f, sign * 0.6f, 0.0f)) &&
                               TcPos(e->m_pos, stopped.x, stopped.y + sign * 0.4f, stopped.z) &&
                               GfVecIs(g_gfLog, {"sector 1.0000 2.0000 3.0000",
                                                 TcSetPos("tactical", stopped.x, stopped.y, stopped.z), "update tactical",
                                                 TcSetPos("tactical", stopped.x, stopped.y + sign * 0.4f, stopped.z),
                                                 "update tactical"}));
            // Water without: stopped.
            g_cmHit = true;
            reset(mode, 0.0f, 0.0f);
            e->UpdateTactical(100);
            WM(r.tcUpDown, g_cmSpheres.size() == 1 && TcPos(e->m_pos, stopped.x, stopped.y, stopped.z));
            g_cmHit = false;
            g_tcSlideHit = false;
            g_tcSlideVolume.flags = 0;
        }

        // Zoomed: along the view, 8 m/s, the same outside.
        for (int mode : {TACTICAL_PLUS, TACTICAL_MINUS}) {
            const float speed = mode == TACTICAL_PLUS ? 8.0f : -8.0f;
            for (I3D_frame *sector : {primary, outside}) {
                g_tcSector = reinterpret_cast<I3D_sector *>(sector);
                reset(mode, 0.0f, 0.0f);
                e->UpdateTactical(100);
                WM(r.tcZoom, g_tcSlides.size() == 1 &&
                                 TcSlide(0, start, S_vector(view.x * t * speed, view.y * t * speed, view.z * t * speed)));
            }
        }
        g_tcSector = reinterpret_cast<I3D_sector *>(primary);

        // The flight: turned as distant as it has come, sped up for its first third, slowed for its last.
        struct S_Case {
            float remaining, speed;
            float wantSpeed;
            bool arrived;
        };
        const float accel = 3.0f;
        const S_Case cases[] = {
            {9.0f, 2.0f, 100.0f * accel * 0.001f + 2.0f, false},     // in the first third
            {4.0f, 2.0f, 2.0f, false},                               // the middle
            {4.0f, 0.4f, 0.4f, false},                               // slowly
            {6.5f, 2.0f, 100.0f * accel * 0.001f + 2.0f, false},     // short of two thirds
            {5.0f, 2.0f, 2.0f, false},
            {2.6f, 2.0f, 2.0f, false},
            {2.0f, 2.0f, 2.0f - 100.0f * accel * 0.001f, false},     // the last third
            {0.1f, 2.0f, 2.0f - 100.0f * accel * 0.001f, true},      // there
            {2.0f, 0.2f, 0.2f - 100.0f * accel * 0.001f, true},      // stopped
            {9.0f, -1.0f, 100.0f * accel * 0.001f + -1.0f, true},    // backwards
        };
        for (const S_Case &c : cases) {
            reset(TACTICAL_FLY, 0.0f, 0.0f);
            e->m_target = {start.x + 0.6f * c.remaining, start.y, start.z + 0.8f * c.remaining, 1.0f};
            e->m_flightDistance = 7.5f;
            e->m_flightAccel = accel;
            e->m_flightSpeed = c.speed;
            e->m_flightTime = 40.0f;
            g_tcSlerps.clear();
            e->UpdateTactical(100);
            S_vector way(e->m_target.x - start.x, e->m_target.y - start.y, e->m_target.z - start.z);
            const float sq = (way.x * way.x + way.z * way.z) + way.y * way.y;
            const float remaining = std::fabs(sq - 1.0f) < 1e-8f ? 1.0f : std::sqrt(sq);
            const float done = remaining / 7.5f * 0.9f;
            float step = 100.0f * c.wantSpeed * 0.001f;
            way = TcNorm(way, sq);
            if (c.arrived)
                step = remaining;
            const S_vector move(way.x * step, way.y * step, way.z * step);
            WM(r.tcFly, g_tcSlerps.size() == 1 && g_tcSlerps[0].self == &e->m_flightFrom &&
                            g_tcSlerps[0].to == &e->m_flightTo && g_tcSlerps[0].t == 1.0f - (1.0f < done ? 1.0f : done) &&
                            !g_tcSlerps[0].shortest && TcVec(g_cmLastDir, g_tcSlerpResult.GetDir()) &&
                            e->m_flightSpeed == c.wantSpeed && e->m_flightTime == (c.arrived ? 0.0f : 140.0f) &&
                            e->m_mode == (c.arrived ? TACTICAL_IDLE : TACTICAL_FLY) &&
                            TcSlide(0, start, move) &&
                            TcPos(e->m_pos, move.x + start.x, move.y + start.y, move.z + start.z) &&
                            g_gfLog.size() == (c.arrived ? 5u : 4u));
        }
        // Exactly at two thirds and one third: neither sped up nor slowed.
        for (float k : {0.6666667f, 0.33333334f}) {
            reset(TACTICAL_FLY, 0.0f, 0.0f);
            e->m_target = {start.x, start.y, start.z + 4.0f, 1.0f};
            float distance = 4.0f / k;
            for (int i = 0; i < 100 && distance * k != 4.0f; ++i)
                distance = std::nextafter(distance, distance * k < 4.0f ? 100.0f : 0.0f);
            e->m_flightDistance = distance;
            e->m_flightAccel = accel;
            e->m_flightSpeed = 2.0f;
            e->UpdateTactical(100);
            WM(r.tcFly, distance * k == 4.0f && e->m_flightSpeed == 2.0f);
        }
        // Turned fully once 0.9 of the way remains... and beyond.
        reset(TACTICAL_FLY, 0.0f, 0.0f);
        e->m_target = {start.x, start.y, start.z + 9.0f, 1.0f};
        e->m_flightDistance = 7.5f;
        e->m_flightSpeed = 1.0f;
        e->m_flightAccel = 0.0f;
        g_tcSlerps.clear();
        e->UpdateTactical(100);
        WM(r.tcFly, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == 0.0f);
        // Arrived: the look point in front.
        reset(TACTICAL_FLY, 0.0f, 0.0f);
        e->m_target = {start.x, start.y + 0.05f, start.z, 1.0f};
        e->m_flightDistance = 7.5f;
        e->m_flightSpeed = 1.0f;
        e->UpdateTactical(100);
        {
            const S_vector dir = S_vector(0.6f, 0.2f, 0.8f).NormalizedGame();
            WM(r.tcFly, e->m_mode == TACTICAL_IDLE && e->m_lookDistance == 10.0f &&
                            e->m_lookPoint.x == dir.x * 10.0f + e->m_pos.x && e->m_lookPoint.z == dir.z * 10.0f + e->m_pos.z &&
                            g_gfLog.size() == 5 &&
                            g_gfLog[4] == CmText("sector %.4f %.4f %.4f", e->m_pos.x, e->m_pos.y, e->m_pos.z));
        }
        // Something in the way: there.
        reset(TACTICAL_FLY, 0.0f, 0.0f);
        e->m_target = {start.x, start.y, start.z + 9.0f, 1.0f};
        e->m_flightDistance = 7.5f;
        e->m_flightSpeed = 2.0f;
        g_tcSlideHit = true;
        g_tcSlideEnd = S_vector(1.5f, 2.5f, 3.1f);
        e->UpdateTactical(100);
        WM(r.tcFly, TcPos(e->m_pos, 1.5f, 2.5f, 3.1f) && e->m_pos.w == 1.0f && e->m_mode == TACTICAL_FLY &&
                        g_cmLastRoll == 0.0f);
        g_tcSlideHit = false;
        // A sideways way (the sums' orders).
        reset(TACTICAL_FLY, 0.0f, 0.0f);
        e->m_target = {start.x - 2.7f, start.y + 1.3f, start.z + 0.4f, 1.0f};
        e->m_flightDistance = 7.5f;
        e->m_flightSpeed = 2.0f;
        e->m_flightAccel = 0.0f;
        e->UpdateTactical(100);
        {
            S_vector way(e->m_target.x - start.x, e->m_target.y - start.y, e->m_target.z - start.z);
            way = TcNorm(way, (way.x * way.x + way.z * way.z) + way.y * way.y);
            const float step = 100.0f * 2.0f * 0.001f;
            WM(r.tcFly, TcSlide(0, start, S_vector(way.x * step, way.y * step, way.z * step)) &&
                            TcPos(e->m_pos, way.x * step + start.x, way.y * step + start.y, way.z * step + start.z));
        }
        // Stopped exactly (the speed 0): there.
        reset(TACTICAL_FLY, 0.0f, 0.0f);
        e->m_target = {start.x, start.y, start.z + 2.0f, 1.0f};
        e->m_flightDistance = 7.5f;
        e->m_flightSpeed = 0.5f;
        e->m_flightAccel = 5.0f;
        e->UpdateTactical(100);
        WM(r.tcFly, e->m_flightSpeed == 0.0f && e->m_mode == TACTICAL_IDLE && TcPos(e->m_pos, start.x, start.y, start.z + 2.0f));
        // 1e-4 left exactly (its square 1e-8): there.
        {
            const float x = 0x1.f75106p-15f, z = 0x1.4f8b58p-14f;   // (about 6e-5, 8e-5)
            reset(TACTICAL_FLY, 0.0f, 0.0f);
            e->m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
            e->m_target = {x, 0.0f, z, 1.0f};
            e->m_flightDistance = 7.5f;
            e->m_flightSpeed = 1.0f;
            e->m_flightAccel = 0.0f;
            e->UpdateTactical(100);
            const float remaining = std::sqrt(x * x + z * z);
            const S_vector way = TcNorm(S_vector(x, 0.0f, z), x * x + z * z);
            WM(r.tcFly, x * x + z * z == 1e-8f && e->m_mode == TACTICAL_IDLE &&
                            TcPos(e->m_pos, way.x * remaining + 0.0f, way.y * remaining + 0.0f, way.z * remaining + 0.0f));
        }
    }

    // --- the humans: faded near the camera ---
    {
        C_CameraHumanEffects *e = TcNew();
        C_Human *a = TcHuman("a", S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 1.9f, 0.0f));
        // The segment (0, 0, 0) .. (0, 2, 0); the camera 0.5 m, 1 m, 2 m off it.
        float tt;
        const float half = SegmentDistance2(S_vector(0.5f, 1.0f, 0.0f), S_vector(0.0f, 0.0f, 0.0f),
                                            S_vector(0.0f, 1.9f + 0.1f, 0.0f), &tt);
        e->m_pos = {0.5f, 1.0f, 0.0f, 1.0f};
        WM(r.tcHumans, e->GetTransparency(a) == 1.0f && half == 0.25f);
        e->m_pos = {1.0f, 1.0f, 0.0f, 1.0f};
        WM(r.tcHumans, e->GetTransparency(a) == 1.0f - (1.0f - 0.4f) * 0.5f);
        e->m_pos = {1.4f, 1.0f, 0.0f, 1.0f};
        WM(r.tcHumans, e->GetTransparency(a) == 1.0f - (1.4f * 1.4f - 0.4f) * 0.5f);
        e->m_pos = {2.0f, 1.0f, 0.0f, 1.0f};
        WM(r.tcHumans, e->GetTransparency(a) == 0.0f);
        // Past 2.4 (3 squared), under 0.6 (0.5).
        for (float d2 : {3.0f, 0.5f}) {
            const float x = std::sqrt(d2);
            e->m_pos = {x, 1.0f, 0.0f, 1.0f};
            const float got2 = SegmentDistance2(S_vector(x, 1.0f, 0.0f), S_vector(0.0f, 0.0f, 0.0f),
                                                S_vector(0.0f, 1.9f + 0.1f, 0.0f), &tt);
            WM(r.tcHumans, e->GetTransparency(a) == (got2 > 2.4f ? 0.0f : 1.0f - (got2 - 0.4f) * 0.5f));
        }
        // Above the frame: by the frame's 0.1 m.
        e->m_pos = {0.0f, 3.0f, 0.0f, 1.0f};
        {
            const float d2 = SegmentDistance2(S_vector(0.0f, 3.0f, 0.0f), S_vector(0.0f, 0.0f, 0.0f),
                                              S_vector(0.0f, 1.9f + 0.1f, 0.0f), &tt);
            WM(r.tcHumans, d2 == 1.0f && e->GetTransparency(a) == 1.0f - (1.0f - 0.4f) * 0.5f);
        }
        // The limits: 2.4 still fades (to 0 by the line), 0.4 is the line's.
        for (float d2 : {2.4f, 0.4f, 0.39999998f, 2.4000001f}) {
            float x = std::sqrt(d2);
            for (int k = 0; k < 50 && x * x != d2; ++k)
                x = std::nextafter(x, x * x < d2 ? 9.0f : 0.0f);
            e->m_pos = {x, 1.0f, 0.0f, 1.0f};
            const float got = e->GetTransparency(a);
            const float want = d2 > 2.4f ? 0.0f : d2 < 0.4f ? 1.0f : 1.0f - (d2 - 0.4f) * 0.5f;
            WM(r.tcHumans, x * x != d2 || got == want);
        }

        // Listed when not 0, set when changed, a 0 dropped at its next change (quirk kept).
        e->m_pos = {1.0f, 1.0f, 0.0f, 1.0f};
        g_gfLog.clear();
        e->UpdateHuman(a);
        const float nearValue = 1.0f - (1.0f - 0.4f) * 0.5f;
        WM(r.tcHumans, e->m_humans.size() == 1 && e->m_humans[0].human == a && e->m_humans[0].transparency == nearValue &&
                           GfLogIs({CmText("unknown94 a %.4f", nearValue)}));
        g_gfLog.clear();
        e->UpdateHuman(a);
        WM(r.tcHumans, g_gfLog.empty() && e->m_humans.size() == 1);
        e->m_pos = {0.5f, 1.0f, 0.0f, 1.0f};
        e->UpdateHuman(a);
        WM(r.tcHumans, e->m_humans.size() == 1 && e->m_humans[0].transparency == 1.0f && GfLogIs({"unknown94 a 1.0000"}));
        e->m_pos = {5.0f, 1.0f, 0.0f, 1.0f};
        g_gfLog.clear();
        e->UpdateHuman(a);
        WM(r.tcHumans, e->m_humans.size() == 1 && e->m_humans[0].transparency == 0.0f && GfLogIs({"unknown94 a 0.0000"}));
        g_gfLog.clear();
        e->UpdateHuman(a);
        WM(r.tcHumans, g_gfLog.empty() && e->m_humans.size() == 1);
        e->m_pos = {0.5f, 1.0f, 0.0f, 1.0f};
        e->UpdateHuman(a);
        WM(r.tcHumans, e->m_humans.empty() && GfLogIs({"unknown94 a 1.0000"}));
        // Not listed, 0: nothing.
        e->m_pos = {5.0f, 1.0f, 0.0f, 1.0f};
        g_gfLog.clear();
        e->UpdateHuman(a);
        WM(r.tcHumans, e->m_humans.empty() && g_gfLog.empty());
        // The one in the middle dropped, the others kept in order.
        C_Human *b = TcHuman("b", S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 1.9f, 0.0f));
        C_Human *c = TcHuman("c", S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 1.9f, 0.0f));
        e->m_humans = {{c, 0.5f}, {a, 0.0f}, {b, 0.25f}};
        e->m_pos = {0.5f, 1.0f, 0.0f, 1.0f};
        e->UpdateHuman(a);
        WM(r.tcHumans, e->m_humans.size() == 2 && e->m_humans[0].human == c && e->m_humans[1].human == b &&
                           e->m_humans[1].transparency == 0.25f);
        // The same human listed twice: the later one.
        e->m_humans = {{a, 0.5f}, {a, 0.25f}};
        e->UpdateHuman(a);
        WM(r.tcHumans, e->m_humans[0].transparency == 0.5f && e->m_humans[1].transparency == 1.0f);

        // The team's known enemies, then its members, each from the last.
        S_EnemyRecord ra = {a, 1, true, 0}, rb = {b, 1, true, 0};
        enemies = {&ra, &rb};
        g_gfMembers = {c};
        e->m_humans.clear();
        e->m_pos = {0.5f, 1.0f, 0.0f, 1.0f};
        g_gfLog.clear();
        e->UpdateHumans(5);
        WM(r.tcHumans, GfLogIs({"unknown94 b 1.0000", "unknown94 a 1.0000", "unknown94 c 1.0000"}) &&
                           e->m_humans.size() == 3 && e->m_humans[0].human == b && e->m_humans[1].human == a &&
                           e->m_humans[2].human == c);
        // ... after every mode's move.
        e->m_humans.clear();
        e->m_mode = TACTICAL_IDLE;
        g_gfLog.clear();
        e->UpdateTactical(10);
        WM(r.tcHumans, e->m_humans.size() == 3 && g_gfLog.size() == 4);
        enemies.clear();
        g_gfMembers.clear();

        // Removed: the last takes its place, keeping the removed one's value (quirk kept).
        e->m_humans = {{a, 0.5f}, {b, 0.75f}, {c, 0.25f}};
        g_gfLog.clear();
        e->Remove(a);
        WM(r.tcHumans, GfLogIs({"unknown94 a 0.0000"}) && e->m_humans.size() == 2 && e->m_humans[0].human == c &&
                           e->m_humans[0].transparency == 0.5f && e->m_humans[1].human == b &&
                           e->m_humans[1].transparency == 0.75f);
        g_gfLog.clear();
        e->Remove(a);
        WM(r.tcHumans, g_gfLog.empty() && e->m_humans.size() == 2);
        e->Remove(b);
        WM(r.tcHumans, GfLogIs({"unknown94 b 0.0000"}) && e->m_humans.size() == 1 && e->m_humans[0].human == c);
        e->m_humans.clear();
        g_gfLog.clear();
        e->Remove(c);
        WM(r.tcHumans, g_gfLog.empty());
        // Twice listed: the later one.
        e->m_humans = {{a, 0.5f}, {b, 0.75f}, {a, 0.25f}};
        e->Remove(a);
        WM(r.tcHumans, e->m_humans.size() == 2 && e->m_humans[0].human == a && e->m_humans[1].human == b);

        // Reset: only when off, from the last.
        e->m_humans = {{a, 0.5f}, {b, 0.75f}};
        g_gfLog.clear();
        e->ResetHumans(true);
        WM(r.tcHumans, g_gfLog.empty() && e->m_humans.size() == 2);
        e->ResetHumans(false);
        WM(r.tcHumans, GfLogIs({"unknown94 b 0.0000", "unknown94 a 0.0000"}) && e->m_humans.empty());
    }

    WmField<I3D_frame *>(scene, 0x1F8) = oldPrimary;
    g_sphereTestHook = nullptr;
    g_sphereHookSetsHit = false;
    g_cmHit = false;
    g_gfActive = nullptr;
}

}  // namespace
