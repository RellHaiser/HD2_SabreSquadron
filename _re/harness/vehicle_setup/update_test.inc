// Part 8e-iii's camera update (game/camera_module.cpp: Update, the car's camera, the scripted view,
// the debug camera), in the camera build after tactical_test.inc: the mouse (IGraph's deltas and
// buttons), the controls' values, PoseGetRoll, the camera's SetWorldDir and part 8e-iv's
// PlaceCamera / MoveCamera faked here. Prints "update:" with 1 per check met.

// (Part 8e-iv's, faked.)
void C_CameraModule::PlaceCamera(int timeDelta) { GfLog("place %d", timeDelta); }
void C_CameraModule::MoveCamera(int timeDelta) { GfLog("move %d", timeDelta); }

// The controls: g_upValues per (action, pressed); 0 otherwise.
C_Controls g_controls;
std::map<std::pair<int, bool>, float> g_upValues;
float C_Controls::GetValue(int action, bool pressed)
{
    const auto it = g_upValues.find({action, pressed});
    return it == g_upValues.end() ? 0.0f : it->second;
}
// The roll of the matrix asked (g_upRoll).
float g_upRoll;
const S_matrix *g_upRollMatrix;
float PoseGetRoll(const S_matrix &m)
{
    g_upRollMatrix = &m;
    return g_upRoll;
}

namespace {

int g_upMouseX, g_upMouseY;
uint32_t g_upButtons;
int __stdcall F_UpMouseX(void *) { return g_upMouseX; }
int __stdcall F_UpMouseY(void *) { return g_upMouseY; }
uint32_t __stdcall F_UpButtons(void *) { return g_upButtons; }
// The camera's SetWorldDir (0x1C): logged, the last kept exactly.
S_vector g_upWorldDir;
float g_upWorldRoll;
void __stdcall F_UpWorldDir(I3D_frame *self, const S_vector &dir, float roll)
{
    GfLog("worlddir %s %.5f %.5f %.5f %.6f", GfName(self), dir.x, dir.y, dir.z, roll);
    g_upWorldDir = dir;
    g_upWorldRoll = roll;
}

// A car whose seats' flags are g_upSeatFlags.
uint32_t g_upSeatFlags;
int g_upSeatAsked;
class C_UpCar : public C_Actor {
public:
    explicit C_UpCar(E_ActorType type) : C_Actor(type) {}
    void CopyFrom(const C_Actor *) override {}
    void StartMission(bool) override {}
    void StopMission(bool) override {}
    void RestartMission(bool) override {}
    uint32_t GetSeatFlags(int seat) override
    {
        g_upSeatAsked = seat;
        return g_upSeatFlags;
    }
};

bool UpVec(const S_vector &a, const S_vector &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool UpVec4(const S_vector4 &a, const S_vector &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
S_vector UpXyz(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}

// A module ready to update: the game camera "game", the camera dummy "dummy" (looking along +z),
// view `view`, the direction (0.6, 0, 0.8); the listener's sector not due.
C_CameraModule *UpNew(int view = 1)
{
    C_CameraModule *m = CmNew();
    m->m_gameCamera = reinterpret_cast<I3D_camera *>(CmCamera("game", S_vector(0.0f, 0.0f, 0.0f)));
    m->m_camera = GfFrame(FRAME_DUMMY, "dummy");
    m->m_view = view;
    m->m_viewSettings = &m->m_settings[view];
    m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
    m->m_listenerTime = 1000000;
    for (S_CameraSettings &settings : m->m_settings)
        settings.flags = 0;
    g_upMouseX = g_upMouseY = 0;
    g_upButtons = 0;
    g_upValues.clear();
    g_selectedMember = nullptr;
    g_gfActive = nullptr;
    g_gfLog.clear();
    g_prints.clear();
    return m;
}

// The mouse's turns as Update makes them.
S_vector UpYaw(S_vector dir, int yaw)
{
    dir.Normalize();
    S_quat q;
    q.Make(S_vector(0.0f, -1.0f, 0.0f), static_cast<float>(yaw) * 0.004f);
    dir.Normalize();
    dir = dir.RotateByMatrix(q.RotationMatrix());
    dir.Normalize();
    return dir;
}
S_vector UpPitch(S_vector dir, int pitch, float maxPitch, float minPitch)
{
    dir.Normalize();
    S_vector axis(1.0f, 0.0f, 0.0f);
    if (!((dir.x == 0.0f && dir.y == 1.0f && dir.z == 0.0f) || (dir.x == 0.0f && dir.y == -1.0f && dir.z == 0.0f)))
        axis = S_vector(dir.y * 0.0f - dir.z, dir.z * 0.0f - dir.x * 0.0f, dir.x - dir.y * 0.0f).NormalizedGame();
    float angle = static_cast<float>(pitch) * 0.004f;
    const double current = std::asin(static_cast<double>(dir.y));
    const float after = static_cast<float>(current - angle);
    if (after >= maxPitch)
        angle = static_cast<float>(current - maxPitch);
    if (after <= minPitch)
        angle = static_cast<float>(current - minPitch);
    S_quat q;
    q.Make(axis, angle);
    dir.Normalize();
    dir = dir.RotateByMatrix(q.RotationMatrix());
    dir.Normalize();
    return dir;
}

void UpdateTest(S_CameraResults &r)
{
    C_game *game = GetGame();
    const bool oldInput = game->m_playerInputEnabled;
    game->m_playerInputEnabled = true;
    g_igraphVtable[0xEC / 4] = reinterpret_cast<void *>(&F_UpMouseX);
    g_igraphVtable[0xF0 / 4] = reinterpret_cast<void *>(&F_UpMouseY);
    g_igraphVtable[0x104 / 4] = reinterpret_cast<void *>(&F_UpButtons);
    g_gfCameraVt[0x1C / 4] = reinterpret_cast<void *>(&F_UpWorldDir);
    g_tcSector = reinterpret_cast<I3D_sector *>(GfFrame(FRAME_SECTOR, "listener"));
    S_DetailSettings *detail = g_gameProfile.GetDetailSettings();
    detail->invertMouse = false;
    static C_Human player, other;
    static C_UpCar car(ACTOR_CAR), boat(ACTOR_SUBMARINE);
    player.vehicle = nullptr;
    player.movement = reinterpret_cast<C_HumanMovement *>(&other);
    player.lookDir = S_vector(0.0f, 0.6f, -0.8f);
    g_gfHeadUnder = false;

    // --- gated: +0x26E (but the debug camera), the debug camera, the scripted view ---
    {
        C_CameraModule *m = UpNew();
        m->m_unknown26E = true;
        m->Update(16);
        WM(r.upGate, g_gfLog.empty() && m->m_time == 0);
        m->m_unknown26E = false;
        m->m_scriptedViewOn = true;
        m->Update(16);
        WM(r.upGate, g_gfLog.empty() && m->m_time == 0);
        m->m_scriptedViewOn = false;
        m->m_debugCamera = true;
        m->m_unknown26E = true;
        m->Update(16);
        WM(r.upGate, g_gfLog.size() == 2 && g_gfLog[1] == "update game" && m->m_time == 0);
        m->m_debugCamera = m->m_unknown26E = false;
        g_gfLog.clear();
        m->Update(16);
        WM(r.upGate, GfLogIs({"place 16", "move 16", "update game"}) && m->m_time == 16);
        // The saved game's update.
        C_chunk chunk;
        g_gfLog.clear();
        WM(r.upGate, m->LoadGame(chunk) && GfLogIs({"cam40 0", "place 100", "move 100", "update game"}) &&
                         m->m_time == 116);
        // In a car's seat: the car's camera (not a seat weapon's, not another vehicle's).
        g_gfActive = &player;
        player.vehicle = &car;
        player.vehicleSeat = 3;
        g_upSeatFlags = SEAT_FLAG_WEAPON;
        g_gfLog.clear();
        m->Update(16);
        WM(r.upGate, g_upSeatAsked == 3 && GfLogIs({"place 16", "move 16", "update game"}) && g_prints.empty());
        g_upSeatFlags = 0;
        g_gfLog.clear();
        m->Update(16);
        WM(r.upGate, GfLogIs({"place 16", "move 16", "update game"}) && g_prints.size() == 1);
        player.vehicle = &boat;
        g_prints.clear();
        m->Update(16);
        WM(r.upGate, g_prints.empty());
        player.vehicle = nullptr;
        g_gfActive = nullptr;
    }

    // --- the mouse: turned about y, pitched within the view's limits ---
    {
        C_CameraModule *m = UpNew();
        const S_vector start(0.6f, 0.0f, 0.8f);
        g_upMouseX = 10;
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, UpYaw(start, 10)) && m->m_wantedDir.w == 0.5f);
        // A member selected: not turned (but in view 10).
        static C_Human member;
        g_selectedMember = &member;
        m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, start));
        m->m_view = 10;
        m->m_viewSettings = &m->m_settings[10];
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, UpYaw(start, 10)));
        g_selectedMember = nullptr;
        m->m_view = 1;
        m->m_viewSettings = &m->m_settings[1];
        // Pitched (inverted too).
        g_upMouseX = 0;
        g_upMouseY = 5;
        m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, UpPitch(start, 5, 0.5f, -0.5f)));
        detail->invertMouse = true;
        m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, UpPitch(start, -5, 0.5f, -0.5f)));
        detail->invertMouse = false;
        // With a member selected only with CAMERA_SETTINGS_PITCH.
        g_selectedMember = &member;
        m->m_settings[1].flags = 0;
        m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, start));
        m->m_settings[1].flags = CAMERA_SETTINGS_PITCH;
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, UpPitch(start, 5, 0.5f, -0.5f)));
        g_selectedMember = nullptr;
        m->m_settings[1].flags = 0;
        // The limits (the view's) and their edges.
        struct S_Case {
            float y;
            int pitch;
        };
        const S_Case cases[] = {{0.43f, -30}, {-0.43f, 30}, {0.3f, -30}, {-0.3f, 30}, {0.47f, -5}, {0.0f, 100}};
        for (const S_Case &c : cases) {
            const S_vector d(0.0f, c.y, std::sqrt(1.0f - c.y * c.y));
            m->m_wantedDir = {d.x, d.y, d.z, 0.5f};
            g_upMouseY = c.pitch;
            m->m_settings[1].maxPitch = 0.45f;
            m->m_settings[1].minPitch = -0.45f;
            m->Update(16);
            WM(r.upMouse, UpVec4(m->m_wantedDir, UpPitch(d, c.pitch, 0.45f, -0.45f)));
        }
        // Exactly at the limits: limited.
        for (float limit : {0.45f, -0.45f}) {
            const S_vector d(0.0f, 0.2f, std::sqrt(1.0f - 0.04f));
            const double current = std::asin(static_cast<double>(S_vector(d).Normalized().y));
            int found = 0;
            for (int p = -400; p <= 400 && !found; ++p)
                if (static_cast<float>(current - static_cast<float>(p) * 0.004f) == limit)
                    found = p;
            m->m_wantedDir = {d.x, d.y, d.z, 0.5f};
            g_upMouseY = found;
            m->Update(16);
            const S_vector want = UpPitch(d, found, 0.45f, -0.45f);
            WM(r.upMouse, found == 0 || UpVec4(m->m_wantedDir, want));
        }
        m->m_settings[1].maxPitch = 0.5f;
        m->m_settings[1].minPitch = -0.5f;
        // Straight up or down: about x.
        for (float y : {1.0f, -1.0f}) {
            m->m_wantedDir = {0.0f, y, 0.0f, 0.5f};
            g_upMouseY = 5;
            m->Update(16);
            WM(r.upMouse, UpVec4(m->m_wantedDir, UpPitch(S_vector(0.0f, y, 0.0f), 5, 0.5f, -0.5f)));
        }
        // Not a unit direction: normalized first (the sum's order).
        m->m_wantedDir = {1.2f, 0.3f, -2.0f, 0.5f};
        g_upMouseY = 0;
        g_upMouseX = 7;
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, UpYaw(S_vector(1.2f, 0.3f, -2.0f), 7)));
        // Many turns (each rounding of the normalizations shows).
        g_upMouseY = 0;
        for (int yaw = -40; yaw <= 40; yaw += 3) {
            m->m_wantedDir = {1.2f, 0.3f, -2.0f, 0.5f};
            g_upMouseX = yaw;
            m->Update(16);
            WM(r.upMouse, UpVec4(m->m_wantedDir, yaw == 0 ? S_vector(1.2f, 0.3f, -2.0f) : UpYaw(S_vector(1.2f, 0.3f, -2.0f), yaw)));
        }
        g_upMouseX = 0;
        for (int p = -40; p <= 40; p += 3) {
            m->m_wantedDir = {0.5f, 0.3f, 0.8f, 0.5f};
            g_upMouseY = p;
            m->Update(16);
            WM(r.upMouse, UpVec4(m->m_wantedDir, p == 0 ? S_vector(0.5f, 0.3f, 0.8f) : UpPitch(S_vector(0.5f, 0.3f, 0.8f), p, 0.5f, -0.5f)));
        }
        // The mouse off, the input off: still.
        m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
        g_upMouseY = 5;
        m->m_mouseLook = false;
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, start));
        m->m_mouseLook = true;
        game->m_playerInputEnabled = false;
        m->Update(16);
        WM(r.upMouse, UpVec4(m->m_wantedDir, start));
        game->m_playerInputEnabled = true;
    }

    // --- the first-person view: the soldier's view, kept out of the water's surface ---
    {
        C_CameraModule *m = UpNew(2);
        g_gfActive = &player;
        g_upMouseX = 10;
        m->m_waterOffset = 9.0f;
        m->Update(16);
        WM(r.upFirst, UpVec4(m->m_wantedDir, S_vector(0.0f, 0.6f, -0.8f)) && m->m_waterOffset == 0.0f &&
                          GfLogIs({"headwater", "place 16", "move 16", "update game"}));
        // A member selected: the mouse's, the water checked too.
        static C_Human member;
        g_selectedMember = &member;
        m->m_settings[2].flags = CAMERA_SETTINGS_PITCH;
        m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
        g_upMouseX = 0;
        g_upMouseY = 5;
        g_gfLog.clear();
        m->Update(16);
        WM(r.upFirst, UpVec4(m->m_wantedDir, UpPitch(S_vector(0.6f, 0.0f, 0.8f), 5, m->m_settings[2].maxPitch,
                                                     m->m_settings[2].minPitch)) &&
                          g_gfLog.size() == 4 && g_gfLog[0] == "headwater");
        g_selectedMember = nullptr;
        // No soldier: not asked.
        g_gfActive = nullptr;
        g_gfLog.clear();
        m->Update(16);
        WM(r.upFirst, GfLogIs({"place 16", "move 16", "update game"}));
        g_gfActive = &player;
        // Under water: out of the band (level 5, band 0.2).
        g_gfHeadUnder = true;
        m->m_waterLevel = 5.0f;
        m->m_settings[2].waterBand = 0.2f;
        struct S_Case {
            float y, offset;
        };
        const float below = (5.0f - 0.1f) - 0.2f, above = (0.2f + 5.0f) + 0.1f;
        const S_Case cases[] = {{4.85f, below - 4.85f}, {4.5f, 0.0f}, {below, 0.0f},
                                {5.1f, above - 5.1f}, {5.5f, 0.0f}, {above, 0.0f}, {5.0f, above - 5.0f}};
        for (const S_Case &c : cases) {
            m->m_camera->m_localMatrix(3) = S_vector(0.0f, c.y, 0.0f);
            m->m_waterOffset = 9.0f;
            m->Update(16);
            WM(r.upFirst, m->m_waterOffset == c.offset);
        }
        g_gfHeadUnder = false;
        m->m_camera->m_localMatrix(3) = S_vector(0.0f, 4.85f, 0.0f);
        m->Update(16);
        WM(r.upFirst, m->m_waterOffset == 0.0f);
        // Another view: not asked.
        m->m_view = 1;
        m->m_viewSettings = &m->m_settings[1];
        g_gfHeadUnder = true;
        m->m_waterOffset = 9.0f;
        g_gfLog.clear();
        m->Update(16);
        WM(r.upFirst, m->m_waterOffset == 0.0f && g_gfLog.size() == 3);
        g_gfHeadUnder = false;
        g_gfActive = nullptr;
    }

    // --- the camera dummy's direction taken ---
    {
        C_CameraModule *m = UpNew();
        m->m_camera->m_localMatrix.m[2][0] = 0.0f;
        m->m_camera->m_localMatrix.m[2][1] = -3.0f;
        m->m_camera->m_localMatrix.m[2][2] = 4.0f;
        m->m_camera->m_localMatrix.m[2][3] = 0.25f;
        const S_vector own = S_vector(0.0f, -3.0f, 4.0f).NormalizedGame();
        static C_Human member;
        struct S_Case {
            int view;
            uint32_t flags;
            bool selected, taken;
        };
        const S_Case cases[] = {{1, 0x09, false, true}, {1, 0x09, true, false}, {7, 0x09, true, true},
                                {8, 0x09, true, true},  {9, 0x09, true, true},  {6, 0x09, true, false},
                                {1, 0x08, false, false}, {1, 0x01, false, false}};
        for (const S_Case &c : cases) {
            m->m_view = c.view;
            m->m_viewSettings = &m->m_settings[c.view];
            m->m_settings[c.view].flags = c.flags;
            g_selectedMember = c.selected ? &member : nullptr;
            m->m_wantedDir = {1.2f, 0.3f, -2.0f, 0.5f};
            m->Update(16);
            S_vector norm = S_vector(1.2f, 0.3f, -2.0f);
            if (c.flags & 1)
                norm.Normalize();
            WM(r.upFollow, c.taken ? UpVec4(m->m_wantedDir, own) && m->m_wantedDir.w == 0.25f
                                   : UpVec4(m->m_wantedDir, norm) && m->m_wantedDir.w == 0.5f);
        }
        // Other directions of the dummy (the normalizations' roundings).
        m->m_view = 1;
        m->m_viewSettings = &m->m_settings[1];
        m->m_settings[1].flags = 0x09;
        g_selectedMember = nullptr;
        const S_vector others[] = {S_vector(0.3f, 0.7f, -0.2f), S_vector(-1.7f, 0.05f, 2.9f), S_vector(0.1f, -0.1f, 0.1f),
                                   S_vector(5.0f, 1.0f, 3.0f), S_vector(-0.33f, 0.66f, 0.99f)};
        for (const S_vector &o : others) {
            m->m_camera->m_localMatrix.m[2][0] = o.x;
            m->m_camera->m_localMatrix.m[2][1] = o.y;
            m->m_camera->m_localMatrix.m[2][2] = o.z;
            m->m_wantedDir = {1.2f, 0.3f, -2.0f, 0.5f};
            m->Update(16);
            S_vector want = o.NormalizedGame();
            want.Normalize();
            WM(r.upFollow, UpVec4(m->m_wantedDir, want));
        }
        // Without a camera dummy: not normalized.
        m->m_camera = nullptr;
        m->m_view = 1;
        m->m_viewSettings = &m->m_settings[1];
        m->m_settings[1].flags = 0x09;
        g_selectedMember = nullptr;
        m->m_wantedDir = {1.2f, 0.3f, -2.0f, 0.5f};
        m->Update(16);
        WM(r.upFollow, UpVec4(m->m_wantedDir, S_vector(1.2f, 0.3f, -2.0f)));
    }

    // --- the listener's sector, the offsets, +0x294 ---
    {
        C_CameraModule *m = UpNew();
        I3D_frame *active = CmCamera("active", S_vector(4.0f, 5.0f, 6.0f));
        CmActive() = active;
        m->m_listenerTime = 10;
        m->Update(16);
        WM(r.upTail, m->m_listenerSector == g_tcSector && m->m_listenerTime == 494 &&
                         GfLogIs({"place 16", "move 16", "update game", "sector 4.0000 5.0000 6.0000"}));
        m->m_listenerSector = nullptr;
        m->m_listenerTime = 0;
        m->Update(1100);
        WM(r.upTail, m->m_listenerSector == g_tcSector && m->m_listenerTime == 400);
        m->m_listenerSector = nullptr;
        m->m_listenerTime = 16;
        m->Update(16);
        WM(r.upTail, !m->m_listenerSector && m->m_listenerTime == 0);
        m->Update(16);
        WM(r.upTail, m->m_listenerSector == g_tcSector && m->m_listenerTime == 484);
        m->m_listenerTime = 0;
        m->Update(500);
        WM(r.upTail, m->m_listenerTime == 0);
        m->m_listenerTime = 1000000;
        CmActive() = nullptr;
        // The offsets: shrink 1 per second after their delays.
        struct S_Case {
            S_vector v;
            int delay, wantDelay;
            float shrink;                               // (-1: unchanged)
        };
        const S_Case cases[] = {{S_vector(0.0f, 3.0f, 4.0f), 0, 0, 0.016f},
                                {S_vector(0.0f, 3.0f, 4.0f), 100, 84, -1.0f},
                                {S_vector(0.0f, 3.0f, 4.0f), 16, 0, 0.016f},
                                {S_vector(0.0f, 3.0f, 4.0f), 10, 0, 0.022f},
                                {S_vector(1.0f, -2.0f, 0.5f), 0, 0, 0.016f},
                                {S_vector(0.006f, 0.0f, 0.008f), 0, 0, 0.016f},
                                {S_vector(0.0f, 0.0f, 0.0f), 100, 100, -1.0f},
                                {S_vector(0.6f, 0.0f, 0.8f), 0, 0, 0.016f},
                                {S_vector(0.0f, 3.0f, 4.0f), 17, 1, -1.0f},
                                {S_vector(0.0f, 0.18f, 0.24f), 0, 0, 0.016f}};
        for (int which = 0; which < 2; ++which) {
            for (const S_Case &c : cases) {
                S_vector4 &offset = which == 0 ? m->m_offsetDir : m->m_offsetPos;
                int &delay = which == 0 ? m->m_offsetDirDelay : m->m_offsetPosDelay;
                offset = {c.v.x, c.v.y, c.v.z, 0.75f};
                delay = c.delay;
                m->Update(16);
                // (As the original: shrunk once the delay is over.)
                S_vector want = c.v;
                float length = c.v.Magnitude();
                int left = c.delay;
                if (length != 0.0f) {
                    if (left != 0)
                        left -= 16;
                    if (left <= 0) {
                        length -= static_cast<float>(16 - left) * 0.001f;
                        if (length < 0.0f)
                            length = 0.0f;
                        want.Normalize();
                        want *= length;
                    }
                }
                WM(r.upTail, UpVec4(offset, want) && offset.w == 0.75f && delay == c.wantDelay);
                offset = {0.0f, 0.0f, 0.0f, 0.75f};
                delay = 0;
            }
        }
        // +0x294 counts up to 5000 while +0x290.
        m->m_unknown290 = true;
        m->m_unknown294 = 4980;
        m->Update(16);
        WM(r.upTail, m->m_unknown294 == 4996);
        m->Update(16);
        WM(r.upTail, m->m_unknown294 == 5000);
        m->m_unknown294 = 4984;
        m->Update(16);
        WM(r.upTail, m->m_unknown294 == 5000);
        m->m_unknown290 = false;
        m->m_unknown294 = 4990;
        m->Update(16);
        WM(r.upTail, m->m_unknown294 == 4990);
    }

    // --- the car's camera ---
    {
        C_CameraModule *m = UpNew();
        g_gfActive = &player;
        player.vehicle = &car;
        player.vehicleSeat = 1;
        player.health = 800;
        player.localPlayer = false;
        g_upSeatFlags = 0;
        I3D_frame *dummy = m->m_camera;
        dummy->m_localMatrix.m[2][0] = 0.3f;
        dummy->m_localMatrix.m[2][1] = 0.2f;
        dummy->m_localMatrix.m[2][2] = 0.9f;
        I3D_frame *cam = reinterpret_cast<I3D_frame *>(m->m_gameCamera);
        cam->m_localMatrix.m[2][0] = -0.5f;
        cam->m_localMatrix.m[2][1] = 0.1f;
        cam->m_localMatrix.m[2][2] = 2.0f;
        g_upRoll = 0.125f;
        auto expect = [&](float yaw, float pitch) {
            const S_vector view = S_vector(0.3f, 0.2f, 0.9f).NormalizedGame();
            const float viewPitch = static_cast<float>(std::atan2(static_cast<double>(view.y),
                                                                  static_cast<double>(std::sqrt(view.z * view.z + view.x * view.x))));
            float heading = static_cast<float>(std::atan2(static_cast<double>(view.x), static_cast<double>(view.z)) + yaw);
            if (heading < 0.0f)
                heading = heading + 6.2831855f;
            else if (heading > 6.2831855f)
                heading = heading - 6.2831855f;
            float elevation = viewPitch + pitch;
            if (elevation > 0.5235988f)
                elevation = 0.5235988f;
            else if (elevation < -0.5235988f)
                elevation = -0.5235988f;
            const double ce = std::cos(static_cast<double>(elevation));
            S_vector d(static_cast<float>(ce * std::sin(static_cast<double>(heading))),
                       static_cast<float>(std::sin(static_cast<double>(elevation))),
                       static_cast<float>(std::cos(static_cast<double>(heading)) * ce));
            d.Normalize();
            return d;
        };
        g_upValues[{0x34, false}] = 0.2f;
        g_upValues[{0x35, false}] = 0.1f;
        g_tcQuatDirs.clear();
        m->Update(16);
        const float yaw1 = 0.2f * 0.9424779f + 0.0f, pitch1 = 0.0f - 0.1f * 0.9424779f;
        WM(r.upCar, m->m_carYaw == yaw1 && m->m_carPitch == pitch1 && UpVec4(m->m_wantedDir, expect(yaw1, pitch1)) &&
                        m->m_wantedDir.w == 0.5f && g_upSeatAsked == 1 && g_upRollMatrix == &dummy->GetMatrix());
        WM(r.upCar, GfLogIs({"place 16", "move 16", "update game"}) && g_tcQuatDirs.size() == 1 &&
                        UpVec(g_tcQuatDirs[0].dir, S_vector(-0.5f, 0.1f, 2.0f).NormalizedGame()) &&
                        g_tcQuatDirs[0].roll == 0.125f && (cam->m_frameFlags & FRMFLAGS_ROT_DIRTY) &&
                        g_prints.size() == 1 && g_prints[0] == CmText("Yaw %.2f, Pitch %.2f, Roll %.2f", yaw1, pitch1, 0.125f));
        // Inverted, and limited (30 degrees aside, 25 up or down).
        detail->invertMouse = true;
        m->m_carYaw = 0.5f;
        m->m_carPitch = -0.4f;
        m->Update(16);
        WM(r.upCar, m->m_carYaw == 0.5235988f && m->m_carPitch == -0.4f + 0.1f * 0.9424779f);
        detail->invertMouse = false;
        g_upValues[{0x34, false}] = -0.2f;
        g_upValues[{0x35, false}] = -0.3f;
        m->m_carYaw = -0.5f;
        m->m_carPitch = 0.3f;
        m->Update(16);
        WM(r.upCar, m->m_carYaw == -0.5235988f && m->m_carPitch == 0.43633232f);
        g_upValues[{0x35, false}] = 0.3f;
        m->m_carPitch = -0.3f;
        m->Update(16);
        WM(r.upCar, m->m_carPitch == -0.43633232f);
        g_upValues[{0x34, false}] = 0.2f;
        m->m_carYaw = 0.4f;
        m->Update(16);
        WM(r.upCar, m->m_carYaw == 0.5235988f);
        // Nothing asked: not limited.
        g_upValues.clear();
        m->m_carYaw = 0.6f;
        m->m_carPitch = 0.5f;
        m->Update(16);
        WM(r.upCar, m->m_carYaw == 0.6f && m->m_carPitch == 0.5f);
        g_upValues[{0x34, false}] = 0.2f;
        g_upValues[{0x35, false}] = -0.3f;
        // No input, or a dead soldier: as it was.
        m->m_carYaw = 0.1f;
        m->m_carPitch = 0.05f;
        game->m_playerInputEnabled = false;
        m->Update(16);
        WM(r.upCar, m->m_carYaw == 0.1f && m->m_carPitch == 0.05f && UpVec4(m->m_wantedDir, expect(0.1f, 0.05f)));
        game->m_playerInputEnabled = true;
        player.health = 0;
        m->Update(16);
        WM(r.upCar, m->m_carYaw == 0.1f && m->m_carPitch == 0.05f);
        player.health = 1;
        m->Update(16);
        WM(r.upCar, m->m_carYaw != 0.1f);
        player.health = 800;
        g_upValues.clear();
        // The heading below 0: plus 2 pi; the elevation limited.
        dummy->m_localMatrix.m[2][0] = -0.3f;
        m->m_carYaw = -0.2f;
        m->m_carPitch = 0.0f;
        m->Update(16);
        {
            const S_vector view = S_vector(-0.3f, 0.2f, 0.9f).NormalizedGame();
            float heading = static_cast<float>(std::atan2(static_cast<double>(view.x), static_cast<double>(view.z)) + -0.2f);
            heading = heading + 6.2831855f;
            const float elevation = static_cast<float>(std::atan2(static_cast<double>(view.y),
                                                                  static_cast<double>(std::sqrt(view.z * view.z + view.x * view.x))));
            const double ce = std::cos(static_cast<double>(elevation));
            S_vector d(static_cast<float>(ce * std::sin(static_cast<double>(heading))), static_cast<float>(std::sin(static_cast<double>(elevation))),
                       static_cast<float>(std::cos(static_cast<double>(heading)) * ce));
            d.Normalize();
            WM(r.upCar, UpVec4(m->m_wantedDir, d));
        }
        for (float y : {3.0f, -3.0f}) {
            dummy->m_localMatrix.m[2][0] = 0.3f;
            dummy->m_localMatrix.m[2][1] = y;
            m->m_carYaw = 0.0f;
            m->Update(16);
            const S_vector view = S_vector(0.3f, y, 0.9f).NormalizedGame();
            const float heading = static_cast<float>(std::atan2(static_cast<double>(view.x), static_cast<double>(view.z)) + 0.0f);
            const float elevation = y > 0.0f ? 0.5235988f : -0.5235988f;
            const double ce = std::cos(static_cast<double>(elevation));
            S_vector d(static_cast<float>(ce * std::sin(static_cast<double>(heading))), static_cast<float>(std::sin(static_cast<double>(elevation))),
                       static_cast<float>(std::cos(static_cast<double>(heading)) * ce));
            d.Normalize();
            WM(r.upCar, UpVec4(m->m_wantedDir, d));
        }
        // Straight along z (the heading 0, kept); 0.6 radians up (limited to 30 degrees).
        dummy->m_localMatrix.m[2][0] = 0.0f;
        dummy->m_localMatrix.m[2][1] = 0.2f;
        m->m_carYaw = m->m_carPitch = 0.0f;
        g_upValues.clear();
        m->Update(16);
        {
            const S_vector view = S_vector(0.0f, 0.2f, 0.9f).NormalizedGame();
            const float elevation = static_cast<float>(std::atan2(static_cast<double>(view.y), static_cast<double>(std::sqrt(view.z * view.z + view.x * view.x))));
            S_vector d(static_cast<float>(std::cos(static_cast<double>(elevation)) * std::sin(0.0)), static_cast<float>(std::sin(static_cast<double>(elevation))),
                       static_cast<float>(std::cos(0.0) * std::cos(static_cast<double>(elevation))));
            d.Normalize();
            WM(r.upCar, UpVec4(m->m_wantedDir, d) && m->m_wantedDir.x == 0.0f);
        }
        for (float sign : {1.0f, -1.0f}) {
            dummy->m_localMatrix.m[2][1] = sign * static_cast<float>(std::tan(0.6)) * 0.9f;
            m->Update(16);
            const S_vector view = S_vector(0.0f, dummy->m_localMatrix.m[2][1], 0.9f).NormalizedGame();
            const float limited = sign * 0.5235988f;
            const double ce = std::cos(static_cast<double>(limited));
            const float heading = static_cast<float>(std::atan2(static_cast<double>(view.x), static_cast<double>(view.z)));
            S_vector d(static_cast<float>(ce * std::sin(static_cast<double>(heading))), static_cast<float>(std::sin(static_cast<double>(limited))),
                       static_cast<float>(std::cos(static_cast<double>(heading)) * ce));
            d.Normalize();
            WM(r.upCar, UpVec4(m->m_wantedDir, d));
        }
        dummy->m_localMatrix.m[2][0] = 0.3f;
        dummy->m_localMatrix.m[2][1] = 0.2f;
        // The game camera's rotation set.
        cam->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        cam->m_frameFlags &= ~FRMFLAGS_ROT_DIRTY;
        m->Update(16);
        WM(r.upCar, (cam->m_frameFlags & FRMFLAGS_ROT_DIRTY) && cam->m_rot.s != 1.0f);
        // The soldier's view: a seat weapon's, a hidden seat's in view 9, an armed one's (the
        // local player's).
        struct S_Case {
            uint32_t flags;
            int view;
            bool local, own;
        };
        const S_Case cases[] = {{SEAT_FLAG_WEAPON, 1, false, true}, {SEAT_FLAG_HIDDEN, 9, false, true},
                                {SEAT_FLAG_HIDDEN, 1, false, false}, {SEAT_FLAG_ARMED, 1, true, true},
                                {SEAT_FLAG_ARMED, 1, false, false}, {SEAT_FLAG_HIDDEN | SEAT_FLAG_ARMED, 9, false, true}};
        for (const S_Case &c : cases) {
            g_upSeatFlags = c.flags;
            m->m_view = c.view;
            player.localPlayer = c.local;
            m->m_carYaw = m->m_carPitch = 0.0f;
            m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
            m->UpdateCarCamera(16);
            WM(r.upCar, UpVec4(m->m_wantedDir, c.own ? S_vector(0.0f, 0.6f, -0.8f).Normalized() : expect(0.0f, 0.0f)));
        }
        player.localPlayer = false;
        player.vehicle = nullptr;
        g_gfActive = nullptr;
        g_upSeatFlags = 0;
    }

    // --- the scripted view ---
    {
        C_CameraModule *m = UpNew();
        I3D_frame *cam = reinterpret_cast<I3D_frame *>(m->m_gameCamera);
        m->m_scriptedViewOn = true;
        m->Update(16);
        WM(r.upScripted, g_gfLog.empty());
        I3D_frame *script = GfFrame(FRAME_DUMMY, "script");
        script->m_localMatrix(3) = S_vector(1.0f, 2.0f, 3.0f);
        script->m_localMatrix.m[2][0] = 0.3f;
        script->m_localMatrix.m[2][1] = -0.2f;
        script->m_localMatrix.m[2][2] = 0.9f;
        script->m_localMatrix.m[2][3] = 0.25f;
        m->m_scriptedView = script;
        m->m_gameCamera = nullptr;
        m->Update(16);
        WM(r.upScripted, g_gfLog.empty());
        m->m_gameCamera = reinterpret_cast<I3D_camera *>(cam);
        game->m_time = 12345;
        auto place = [&](S_vector &dirOut) {
            S_vector dir(0.3f, -0.2f, 0.9f);
            dir.Normalize();
            dirOut = dir;
            S_vector pos(1.0f - dir.x * 10.0f, 2.0f - dir.y * 10.0f, 3.0f - dir.z * 10.0f);
            S_vector side(dir.y * 0.0f - dir.z, dir.z * 0.0f - dir.x * 0.0f, dir.x - dir.y * 0.0f);
            S_vector up(dir.y * side.z - dir.z * side.y, dir.z * side.x - side.z * dir.x, side.y * dir.x - dir.y * side.x);
            up = TcNorm(up, (up.z * up.z + up.y * up.y) + up.x * up.x);
            side = side.NormalizedGame();
            const float phase = static_cast<float>(12345.0 * 0.0015f);
            const float a = static_cast<float>(std::sin(std::cos(static_cast<double>(phase))) * 0.30000001192092896);
            const float wave = static_cast<float>(std::sin(static_cast<double>(phase + 3.0f)) + 1.0);
            const float b = static_cast<float>(std::cos(static_cast<double>(wave)) * 0.30000001192092896);
            return S_vector((up.x * a + pos.x) + side.x * b, (pos.y + up.y * a) + side.y * b, (pos.z + up.z * a) + side.z * b);
        };
        S_vector dir;
        const S_vector pos = place(dir);
        g_gfLog.clear();
        m->Update(16);
        WM(r.upScripted, cam->m_localMatrix(3) == pos && UpVec(g_upWorldDir, dir) && g_upWorldRoll == 0.0f &&
                             g_gfLog.size() == 2 && g_gfLog[1] == "update game" && m->m_time == 0);
        // The shake: ended (its offsets 0, their w kept) ...
        m->m_shakeTime = 10;
        m->m_shakePos = {1.0f, 2.0f, 3.0f, 7.0f};
        m->m_shakeDir = {1.0f, 2.0f, 3.0f, 7.0f};
        g_gfLog.clear();
        m->Update(16);
        WM(r.upScripted, m->m_shakeTime == 0 && CmVec4(m->m_shakePos, 0.0f, 0.0f, 0.0f, 7.0f) &&
                             CmVec4(m->m_shakeDir, 0.0f, 0.0f, 0.0f, 7.0f) && g_gfLog.size() == 2);
        m->m_shakePos = {1.0f, 2.0f, 3.0f, 7.0f};
        m->m_shakeTime = 0;
        m->Update(16);
        WM(r.upScripted, m->m_shakePos.x == 1.0f && m->m_shakeTime == 0);
        m->m_shakeTime = -5;
        m->Update(16);
        WM(r.upScripted, m->m_shakePos.x == 1.0f && m->m_shakeTime == -5);
        m->m_shakeTime = 16;
        g_gfLog.clear();
        m->Update(16);
        WM(r.upScripted, m->m_shakeTime == 0 && m->m_shakePos.x == 0.0f && g_gfLog.size() == 3);
        m->m_shakeTime = 1;
        m->m_shakePos = {1.0f, 2.0f, 3.0f, 7.0f};
        m->Update(16);
        WM(r.upScripted, m->m_shakeTime == 0 && m->m_shakePos.x == 0.0f);
        // ... running, the offsets turned with the camera.
        cam->m_localMatrix.m[0][0] = 0.0f;
        cam->m_localMatrix.m[0][2] = -1.0f;
        cam->m_localMatrix.m[2][0] = 1.0f;
        cam->m_localMatrix.m[2][2] = 0.0f;
        m->m_shakeTime = 50;
        m->m_offsetPos = {0.1f, 0.2f, 0.3f, 1.0f};
        m->m_offsetDir = {0.01f, 0.0f, -0.02f, 1.0f};
        g_gfLog.clear();
        m->Update(16);
        {
            const double left = 34.0;
            const float a = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(0.07f * left))) *
                                               static_cast<double>(static_cast<float>(left * 6e-05f)));
            const float b = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(left * 0.08f))) *
                                               static_cast<double>(static_cast<float>(left * 4e-05f)));
            const float c = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(left * 0.05f))) *
                                               static_cast<double>(static_cast<float>(left * 7e-05f)));
            WM(r.upScripted, m->m_shakeTime == 34 && CmVec4(m->m_shakePos, a, b, c, 7.0f) &&
                                 CmVec4(m->m_shakeDir, c * 0.15f, b * -0.25f, a * 0.15f, 7.0f));
            S_matrix mat = cam->GetMatrix();
            mat(3) = pos;
            S_vector off(0.1f, 0.2f, 0.3f);
            off *= mat;
            off = S_vector(off.x - pos.x, off.y - pos.y, off.z - pos.z);
            const S_vector p2((pos.x + a) + off.x, off.y + (pos.y + b), off.z + (pos.z + c));
            mat(3) = p2;
            S_vector turn(0.01f, 0.0f, -0.02f);
            turn *= mat;
            turn = S_vector(turn.x - p2.x, turn.y - p2.y, turn.z - p2.z);
            const S_vector shaken(dir.x + c * 0.15f, dir.y + b * -0.25f, dir.z + a * 0.15f);
            WM(r.upScripted, cam->m_localMatrix(3) == p2 &&
                                 UpVec(g_upWorldDir, S_vector(shaken.x + turn.x, turn.y + shaken.y, turn.z + shaken.z)) &&
                                 g_upWorldRoll == 0.0f && g_gfLog.size() == 3 && g_gfLog[2] == "update game");
        }
        m->m_scriptedViewOn = false;
        game->m_time = 0;
    }

    // --- the debug camera ---
    {
        C_CameraModule *m = UpNew();
        I3D_frame *cam = reinterpret_cast<I3D_frame *>(m->m_gameCamera);
        m->m_debugCamera = true;
        m->m_debugSpeed = 5;
        m->m_wantedPos = {1.0f, 2.0f, 3.0f, 1.0f};
        auto turned = [](S_vector d, float lookX, float lookY) {
            float heading = static_cast<float>(std::atan2(static_cast<double>(d.x), static_cast<double>(d.z)));
            double elevation = std::atan2(static_cast<double>(d.y), static_cast<double>(std::sqrt(d.z * d.z + d.x * d.x)));
            if (lookX != 0.0f)
                heading = lookX * 0.9424779f + heading;
            if (lookY != 0.0f)
                elevation = static_cast<float>(elevation - static_cast<double>(lookY * 0.9424779f));
            if (elevation < -3.1415927f)
                elevation = static_cast<float>(elevation + 3.1415927f);
            else if (elevation > 3.1415927f)
                elevation = static_cast<float>(elevation - 3.1415927f);
            if (elevation > 1.4835299f)
                elevation = 1.4835299f;
            else if (elevation < -1.4835299f)
                elevation = -1.4835299f;
            const double ce = std::cos(elevation);
            S_vector out(static_cast<float>(ce * std::sin(static_cast<double>(heading))), static_cast<float>(std::sin(elevation)),
                         static_cast<float>(std::cos(static_cast<double>(heading)) * ce));
            out.Normalize();
            return out;
        };
        // Not turned without the look actions.
        m->Update(16);
        WM(r.upDebug, UpVec4(m->m_wantedDir, S_vector(0.6f, 0.0f, 0.8f)) && cam->m_localMatrix(3) == S_vector(1.0f, 2.0f, 3.0f) &&
                          UpVec(g_upWorldDir, S_vector(0.6f, 0.0f, 0.8f)) && GfVecIs(g_gfLog, {CmText("worlddir game %.5f %.5f %.5f %.6f", 0.6f, 0.0f, 0.8f, 0.0f), "update game"}));
        struct S_Look {
            float x, y;
        };
        const S_Look looks[] = {{0.2f, 0.0f}, {0.0f, 0.1f}, {-0.3f, -0.2f}, {0.0f, -2.0f}, {0.0f, 2.0f}, {0.0f, 6.0f}, {0.0f, -6.0f},
                                {0.0f, 4.0f}, {0.0f, -4.0f}, {0.5f, 0.0f}, {0.0f, 0.5f}, {0.7f, 0.0f}, {-0.13f, 0.0f}, {1.1f, 0.0f}};
        for (const S_Look &l : looks) {
            m->m_wantedDir = {0.6f, 0.1f, 0.8f, 0.5f};
            g_upValues[{0x34, false}] = l.x;
            g_upValues[{0x35, false}] = l.y;
            m->Update(16);
            WM(r.upDebug, UpVec4(m->m_wantedDir, turned(S_vector(0.6f, 0.1f, 0.8f), l.x, l.y)) && m->m_wantedDir.w == 0.5f);
        }
        // Steep without the look actions: not limited.
        g_upValues.clear();
        m->m_wantedDir = {0.0f, 0.999f, 0.04f, 0.5f};
        m->Update(16);
        WM(r.upDebug, UpVec4(m->m_wantedDir, S_vector(0.0f, 0.999f, 0.04f)));
        detail->invertMouse = true;
        m->m_wantedDir = {0.6f, 0.1f, 0.8f, 0.5f};
        g_upValues[{0x35, false}] = 0.1f;
        m->Update(16);
        WM(r.upDebug, UpVec4(m->m_wantedDir, turned(S_vector(0.6f, 0.1f, 0.8f), 0.0f, -0.1f)));
        detail->invertMouse = false;
        g_upValues.clear();
        // The buttons: forward, back (both: there and back).
        m->m_wantedDir = {0.6f, 0.3f, 0.8f, 0.5f};
        const float t = static_cast<float>(16) * 0.001f;
        g_upButtons = 1;
        m->Update(16);
        WM(r.upDebug, UpVec4(m->m_wantedPos, S_vector(t * 0.6f * 10.0f + 1.0f, t * 0.3f * 10.0f + 2.0f, t * 0.8f * 10.0f + 3.0f)) &&
                          cam->m_localMatrix(3) == UpXyz(m->m_wantedPos));
        m->m_wantedPos = {1.0f, 2.0f, 3.0f, 1.0f};
        g_upButtons = 2;
        m->Update(16);
        WM(r.upDebug, UpVec4(m->m_wantedPos, S_vector(1.0f - t * 0.6f * 10.0f, 2.0f - t * 0.3f * 10.0f, 3.0f - t * 0.8f * 10.0f)));
        m->m_wantedPos = {1.0f, 2.0f, 3.0f, 1.0f};
        g_upButtons = 3;
        m->Update(16);
        {
            const S_vector there(t * 0.6f * 10.0f + 1.0f, t * 0.3f * 10.0f + 2.0f, t * 0.8f * 10.0f + 3.0f);
            WM(r.upDebug, UpVec4(m->m_wantedPos, S_vector(there.x - t * 0.6f * 10.0f, there.y - t * 0.3f * 10.0f,
                                                          there.z - t * 0.8f * 10.0f)));
        }
        m->m_wantedPos = {1.0f, 2.0f, 3.0f, 1.0f};
        m->m_mouseLook = false;
        m->Update(16);
        WM(r.upDebug, UpVec4(m->m_wantedPos, S_vector(1.0f, 2.0f, 3.0f)));
        m->m_mouseLook = true;
        game->m_playerInputEnabled = false;
        m->Update(16);
        WM(r.upDebug, UpVec4(m->m_wantedPos, S_vector(1.0f, 2.0f, 3.0f)));
        game->m_playerInputEnabled = true;
        g_upButtons = 0;
        // The speed steps (0.2 .. 200 m/s).
        g_upValues[{0x36, true}] = 0.25f;
        m->m_debugSpeed = 8;
        m->Update(16);
        WM(r.upDebug, m->m_debugSpeed == 9 && g_prints.size() == 1 && g_prints[0] == "Debug camera speed 200.00");
        m->Update(16);
        WM(r.upDebug, m->m_debugSpeed == 9 && g_prints.size() == 2);
        g_upValues[{0x36, true}] = -1.0f;
        m->m_debugSpeed = 1;
        g_prints.clear();
        m->Update(16);
        WM(r.upDebug, m->m_debugSpeed == 0 && g_prints.size() == 1 && g_prints[0] == "Debug camera speed 0.20");
        m->Update(16);
        WM(r.upDebug, m->m_debugSpeed == 0);
        g_upValues[{0x36, true}] = 0.0f;
        g_upValues[{0x36, false}] = 1.0f;
        g_prints.clear();
        m->Update(16);
        WM(r.upDebug, m->m_debugSpeed == 0 && g_prints.empty());
        // Each speed.
        g_upValues.clear();
        const float speeds[10] = {0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f};
        for (int i = 0; i < 10; ++i) {
            m->m_debugSpeed = i;
            m->m_wantedPos = {0.0f, 0.0f, 0.0f, 1.0f};
            m->m_wantedDir = {0.0f, 0.0f, 1.0f, 0.5f};
            g_upButtons = 1;
            m->Update(16);
            WM(r.upDebug, m->m_wantedPos.z == t * 1.0f * speeds[i] + 0.0f);
        }
        g_upButtons = 0;
        m->m_debugCamera = false;
    }

    game->m_playerInputEnabled = oldInput;
    g_igraphVtable[0x104 / 4] = g_igraphVtable[0x100 / 4];
    g_gfActive = nullptr;
    g_selectedMember = nullptr;
}

}  // namespace
