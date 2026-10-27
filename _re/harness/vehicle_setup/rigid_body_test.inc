// Part 5's test (the rigid body build, build_rigid.bat: -DRIGID_TEST links src/game/rigid_body.cpp and
// runs only RigidBodyTest on every model): C_RigidBody on the model's frames (their hierarchy boxes,
// through the visuals' vertices) and on boxes made here. 5a: the integration steps (part 5b) are
// stubs that record their steps. check_rigid.py reads the result line. CAR_TRACE=1 prints the failing
// check lines.

#include "game/rigid_body.h"

// --- the integration steps (part 5b's; recorded) ---------------------------------------------------------

std::vector<std::pair<int, float>> g_rbSteps;          // (mode, dt)
#ifndef RIGID_STEP_TEST                                 // (the step build links the real ones)
void C_RigidBody::StepWheel(float dt) { g_rbSteps.push_back({1, dt}); }
void C_RigidBody::StepBox(float dt) { g_rbSteps.push_back({2, dt}); }
#endif

// --- the engine: the visuals' type and local bound, the frames' bound ------------------------------------

uint32_t __stdcall F_RbVisualType(I3D_frame *self)
{
    return self->m_type == FRAME_VISUAL ? static_cast<uint32_t>(I3DVISUAL_OBJECT) : 0u;
}

// The local box of a visual from its vertices (none: an inverted box).
I3D_bbox RbVertexBox(const I3D_frame *f)
{
    I3D_bbox box = I3D_bbox::Empty();
    const std::vector<float> &v = Stub(f).vertices;
    for (size_t i = 0; i + 2 < v.size(); i += 8)
        box.Expand(S_vector(v[i], v[i + 1], v[i + 2]));
    return box;
}

int g_rbLocalBounds;
void __stdcall F_RbUpdateLocalBound(I3D_frame *self)
{
    ++g_rbLocalBounds;
    *reinterpret_cast<I3D_bbox *>(reinterpret_cast<uint8_t *>(self) + 0x1A0) = RbVertexBox(self);
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(self) + 0x1D0) |= VISFLAGS_BOUND_VALID;
}

int g_rbBounds;
void __stdcall F_RbUpdateBound(I3D_frame *self)
{
    ++g_rbBounds;
    self->m_frameFlags |= FRMFLAGS_BOUND_VALID;
    self->m_bound.center = self->GetWorldPos() + S_vector(0.0f, 0.5f, 0.0f);
    self->m_bound.radius = 1.25f;
}

// --- the callback --------------------------------------------------------------------------------------

struct S_RbCall {
    int event, unused;
    void *context;
    const S_RigidBodyContact *contact;
};
std::vector<S_RbCall> g_rbCalls;
int __fastcall RbCallback(int event, int unused, void *context, const S_RigidBodyContact *contact)
{
    g_rbCalls.push_back({event, unused, context, contact});
    return 0;
}

// --- the oracles ---------------------------------------------------------------------------------------

void RbAddBox(I3D_bbox &box, I3D_frame *f, const S_matrix &parent)
{
    if (f->m_type != FRAME_VISUAL)
        return;
    const S_matrix m = f->GetLocalMatrix() * parent;
    const I3D_bbox own = RbVertexBox(f);
    if (own.min.x <= own.max.x) {
        const float xs[2] = {own.min.x, own.max.x}, ys[2] = {own.min.y, own.max.y}, zs[2] = {own.min.z, own.max.z};
        for (float x : xs)
            for (float y : ys)
                for (float z : zs)
                    box.Expand(S_vector(x, y, z) * m);
    }
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
        RbAddBox(box, c, m);
}

I3D_bbox RbBox(I3D_frame *f)
{
    I3D_bbox box = f->m_type == FRAME_VISUAL ? RbVertexBox(f) : I3D_bbox::Empty();
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
        RbAddBox(box, c, IdentityMatrix());
    return box;
}

bool RbSame(const S_vector &a, const S_vector &b)
{
    return std::memcmp(&a, &b, sizeof(S_vector)) == 0;
}

bool RbSameBox(const I3D_bbox &a, const I3D_bbox &b)
{
    return RbSame(a.min, b.min) && RbSame(a.max, b.max);
}

float RbLength(float sq)
{
    int32_t bits;
    std::memcpy(&bits, &sq, 4);
    if (bits < 0x322bcc77)
        return 0.0f;
    return std::fabs(sq - 1.0f) < 1e-8f ? 1.0f : std::sqrt(sq);
}

// The inverse inertia Setup gives a box of sides (sx, sy, sz) (padded) and mass m: a cube's for
// proportions within 0.5..2; else the longest side's (ties: the later; x only beating both strictly)
// on the two other axes, scaled by the longest and middle sides' squares on its own.
S_vector RbInertia(float sx, float sy, float sz, float m)
{
    const float iy = 1.0f / sy;
    const auto in = [](float r) { return r <= 2.0f && r >= 0.5f; };
    if (in(iy * sx) && in(sx / sz) && in(iy * sz)) {
        const float s = ((sy + sz) + sx) * 0.33333334f;
        const float k = 1.0f / (((s * s) * m) * 0.33333334f);
        return S_vector(k, k, k);
    }
    const float s[3] = {sx, sy, sz};
    const int longest = sx > sy && sx > sz ? 0 : sy >= sx && sy > sz ? 1 : 2;
    int middle;
    if (longest == 0)
        middle = sy > sz ? 1 : 2;
    else if (longest == 1)
        middle = sx > sz ? 0 : 2;
    else
        middle = sy > sx ? 1 : 0;
    const float l2 = s[longest] * s[longest];
    const float k = 1.0f / ((l2 * m) * 0.33333334f);
    float r[3] = {k, k, k};
    r[longest] = (l2 / (s[middle] * s[middle])) * k;
    return S_vector(r[0], r[1], r[2]);
}

// --- the test ------------------------------------------------------------------------------------------

void RbExpect(bool &flag, bool ok, int line)
{
    if (!ok && std::getenv("CAR_TRACE"))
        std::printf("  rigid body check failed: line %d\n", line);
    flag = flag && ok;
}
#define RB(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

struct S_Rb {
    bool box = true, setup = true, wheel = true, frame = true, start = true, state = true, impulse = true,
         step = true, tick = true, topple = true, forget = true, save = true, stop = true, list = true;
};

// A visual under `parent` with the box (x0..x1, y0..y1, z0..z1) of vertices, at `pos`.
I3D_frame *RbBoxFrame(I3D_frame *parent, const char *name, float x0, float x1, float y0, float y1, float z0, float z1,
                      const S_vector &pos)
{
    I3D_frame *f = NewFrame(FRAME_VISUAL, name, parent);
    std::vector<float> &v = Stub(f).vertices;
    for (int c = 0; c < 8; ++c) {
        const float p[8] = {c & 1 ? x1 : x0, c & 2 ? y1 : y0, c & 4 ? z1 : z0, 0, 0, 0, 0, 0};
        v.insert(v.end(), p, p + 8);
    }
    f->m_localMatrix.m[3][0] = pos.x;
    f->m_localMatrix.m[3][1] = pos.y;
    f->m_localMatrix.m[3][2] = pos.z;
    return f;
}

// Turns frame `f` so that its local axis `up` (0 x, 1 y, 2 z) points along the world's y.
void RbTurn(I3D_frame *f, int up)
{
    S_matrix &m = f->m_localMatrix;
    const S_vector p = m(3);
    m.Identity();
    if (up == 0) {                                      // x up: rows x (0,1,0), y (-1,0,0)
        m.m[0][0] = 0, m.m[0][1] = 1, m.m[1][0] = -1, m.m[1][1] = 0;
    } else if (up == 2) {                               // z up: rows y (0,0,-1), z (0,1,0)
        m.m[1][1] = 0, m.m[1][2] = -1, m.m[2][1] = 1, m.m[2][2] = 0;
    }
    m(3) = p;
}

S_matrix RbInverse(const S_matrix &m)
{
    S_matrix r = m;
    r.Inverse(m);
    return r;
}

// Turns frame `f` about the world's z (or x; cosine c, sine s), its place kept (and its GetRot).
void RbTilt(I3D_frame *f, float c, float s, bool aboutX = false)
{
    S_matrix t;
    t.Identity();
    if (aboutX)
        t.m[1][1] = c, t.m[1][2] = s, t.m[2][1] = -s, t.m[2][2] = c;
    else
        t.m[0][0] = c, t.m[0][1] = s, t.m[1][0] = -s, t.m[1][1] = c;
    S_matrix &m = f->m_localMatrix;
    const S_vector p = m(3);
    m(3) = S_vector(0, 0, 0);
    m = m * t;
    m(3) = p;
    f->m_rot.Make(m);
}

// Topple's push as the original works it out (true: the body rests): the frame's axes as directions
// from the centre of mass, normalised (unless within 1e-8 of unit), turned upward; the strictly most
// upright one; its side over 2.5 times the first other side or the second (x: y, z; y: x, z; z: x,
// y), the base the first unless only the second is short; the push along the two others' sum.
bool RbToppleOracle(const C_RigidBody &b, I3D_frame *f, S_vector &impulse, S_vector4 &at)
{
    const S_matrix &w = f->GetMatrix();
    const S_vector4 &cw = b.m_centreWorld;
    const auto normalize = [](S_vector &v) {
        const float sq = (v.x * v.x + v.z * v.z) + v.y * v.y;
        if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
            const float k = 1.0f / std::sqrt(sq);
            v = S_vector(v.x * k, v.y * k, v.z * k);
        }
    };
    S_vector axes[3];
    for (int i = 0; i < 3; ++i) {
        S_vector v = S_vector(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f) * w;
        v = S_vector(v.x - cw.x, v.y - cw.y, v.z - cw.z);
        normalize(v);
        if (v.y < 0.0f)
            v = S_vector(v.x * -1.0f, v.y * -1.0f, v.z * -1.0f);
        axes[i] = v;
    }
    int a;
    if (axes[0].y > axes[1].y && axes[0].y > axes[2].y)
        a = 0;
    else if (axes[1].y > axes[0].y && axes[1].y > axes[2].y)
        a = 1;
    else if (axes[2].y > axes[0].y && axes[2].y > axes[1].y)
        a = 2;
    else
        return true;
    const I3D_bbox hb = RbBox(f);
    const float s[3] = {hb.max.x - hb.min.x, hb.max.y - hb.min.y, hb.max.z - hb.min.z};
    const int first = a == 0 ? 1 : 0, second = a == 2 ? 1 : 2;
    const bool overFirst = s[a] > s[first] * 2.5f, overSecond = s[second] * 2.5f < s[a];
    if (!overFirst && !overSecond)
        return true;
    const float ratio = 1.0f / ((overFirst ? s[first] : s[second]) / s[a]);
    S_vector dir(axes[second].x + axes[first].x, axes[second].y + axes[first].y, axes[second].z + axes[first].z);
    normalize(dir);
    dir = S_vector(dir.x * 0.12f, dir.y * 0.12f, dir.z * 0.12f);
    const S_vector4 &p0 = b.m_points[0];
    const float reach = RbLength((p0.x * p0.x + p0.y * p0.y) + p0.z * p0.z);
    impulse = S_vector(b.m_mass * dir.x, dir.y * b.m_mass, dir.z * b.m_mass);
    at = {cw.x, reach * ratio + cw.y, cw.z, 0.0f};
    return false;
}

void RigidBodyTest(const char *path)
{
    if (!LoadModel(path))
        return;
    std::printf("== %s\n", g_modelFile.c_str());
    g_frameVtable[0x04 / 4] = reinterpret_cast<void *>(&F_RbVisualType);
    g_frameVtable[0x90 / 4] = reinterpret_cast<void *>(&F_RbUpdateLocalBound);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_RbUpdateBound);
    g_countUpdates = true;
    GetGame()->m_scene = g_carScene;
    S_Rb r;
    const size_t bodies0 = g_rigidBodies.size();

    // --- The hierarchy box of every frame (through visuals only; their vertices' boxes).
    std::vector<I3D_frame *> frames;
    for (S_StubFrame *s : g_frames)
        frames.push_back(s->frame);
    for (I3D_frame *f : frames)
        RB(r.box, RbSameBox(GetHierarchyBox(f), RbBox(f)));
    // A visual's own box is updated once; a box with nothing is the empty one (w kept).
    {
        I3D_frame *empty = NewFrame(FRAME_DUMMY, "rb.empty", nullptr);
        I3D_frame *v = RbBoxFrame(nullptr, "rb.once", -1, 1, -2, 2, -3, 3, S_vector(0, 0, 0));
        const int before = g_rbLocalBounds;
        GetHierarchyBox(v);
        GetHierarchyBox(v);
        RB(r.box, g_rbLocalBounds == before + 1);
        const I3D_bbox &e = GetHierarchyBox(empty);
        RB(r.box, e.min.x == 1e16f && e.min.y == 1e16f && e.min.z == 1e16f && e.max.x == -1e16f &&
                      e.max.y == -1e16f && e.max.z == -1e16f);
        // Under a dummy: not reached.
        I3D_frame *dummy = NewFrame(FRAME_DUMMY, "rb.dummy", v);
        RbBoxFrame(dummy, "rb.hidden", -9, 9, -9, 9, -9, 9, S_vector(0, 0, 0));
        RB(r.box, RbSameBox(GetHierarchyBox(v), RbVertexBox(v)));
        // A visual child without vertices is skipped, its children are not.
        I3D_frame *bare = NewFrame(FRAME_VISUAL, "rb.bare", v);
        bare->m_localMatrix.m[3][0] = 5.0f;
        RbBoxFrame(bare, "rb.under", -1, 1, -1, 1, -1, 1, S_vector(0, 0, 0));
        const I3D_bbox &b = GetHierarchyBox(v);
        RB(r.box, RbSameBox(b, RbBox(v)) && b.max.x == 6.0f);
    }

    // --- Bodies on the model's frames (with a box) and on boxes of every proportion.
    std::vector<I3D_frame *> bodyFrames;
    for (I3D_frame *f : frames) {
        if (bodyFrames.size() < 10 && RbBox(f).min.x <= RbBox(f).max.x)
            bodyFrames.push_back(f);
    }
    const float shapes[][3] = {{1, 1, 1}, {1, 1, 3}, {3, 1, 1}, {1, 3, 1}, {2, 2, 1}, {1, 2, 2}, {2, 1, 2},
                               {0.5f, 2, 3}, {3, 2, 0.5f}, {2, 3, 0.5f}, {0.2f, 0.2f, 0.2f}};
    int shapeIndex = 0;
    for (const auto &s : shapes) {
        char name[32];
        std::snprintf(name, sizeof name, "rb.shape%d", shapeIndex++);
        I3D_frame *sf = RbBoxFrame(nullptr, name, -s[0] * 0.4f, s[0] * 0.6f, -s[1] * 0.5f, s[1] * 0.5f, -s[2] * 0.3f,
                                   s[2] * 0.7f, S_vector(4.0f, 1.0f, -2.0f));
        if (shapeIndex % 2 == 0)
            RbTilt(sf, 0.8f, 0.6f);                     // (turned: the saved rotation means something)
        bodyFrames.push_back(sf);
    }
    int k = 0;
    int context = 0;
    for (I3D_frame *f : bodyFrames) {
        ++k;
        auto *body = new C_RigidBody();
        RB(r.list, g_rigidBodies.back() == body && g_rigidBodies.size() == bodies0 + 1);
        // SetFrame: the place, the matrices, at rest.
        body->m_state = RIGID_BODY_MOVING;
        g_rbCalls.clear();
        RB(r.frame, body->SetFrame(f));
        const S_matrix &w = f->GetMatrix();
        const S_matrix inv = RbInverse(w);
        RB(r.frame, body->m_frame == f && std::memcmp(&body->m_pos, &w.m[3], 16) == 0 &&
                        std::memcmp(&body->m_matrix, &w, sizeof w) == 0 &&
                        std::memcmp(&body->m_invMatrix, &inv, sizeof inv) == 0 &&
                        std::memcmp(&body->m_right, &w.m[0], 16) == 0 && std::memcmp(&body->m_up, &w.m[1], 16) == 0 &&
                        std::memcmp(&body->m_dir, &w.m[2], 16) == 0 && body->m_state == RIGID_BODY_AT_REST &&
                        g_rbCalls.empty());
        bool rot = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                rot &= body->m_rotation.m[i][j] == (i == 3 && j < 3 ? 0.0f : w.m[i][j]);
                rot &= body->m_invRotation.m[i][j] == (i == 3 && j < 3 ? 0.0f : inv.m[i][j]);
            }
        RB(r.frame, rot);
        // Setup: from moving with a slow speed: at rest, the callback told (it is set first).
        body->m_state = RIGID_BODY_MOVING;
        S_RigidBodyParams p;
        // (Straight up and just over 1e-5 squared: moving; 0.1 up also just over Start's 0.01.)
        const bool slow = k % 3 == 0;
        p.SetVelocity(slow         ? S_vector(0.001f, 0.002f, 0.0f)
                      : k % 5 == 2 ? S_vector(0.0f, 0.1f, 0.0f)
                      : k % 5 == 4 ? S_vector(0.003f, 0.002f, 0.001f)
                                   : S_vector(1.5f, 2.0f * k, -0.5f));
        p.velocity.w = 0.25f;
        p.weight = 2.0f + static_cast<float>(k);
        p.friction = 0.4f;
        p.callback = RbCallback;
        p.context = &context;
        p.moveCollision = k % 2 == 0;
        p.unknown41 = k % 4 == 1;
        body->Setup(p);
        const I3D_bbox box = RbBox(f);
        const float dx = box.max.x - box.min.x, dy = box.max.y - box.min.y, dz = box.max.z - box.min.z;
        RB(r.setup, body->m_context == &context && body->m_callback == RbCallback &&
                        body->m_restVolume == p.unknown41 && body->m_moveCollision == p.moveCollision &&
                        body->m_mass == p.weight && body->m_friction == 0.4f &&
                        RbSame(body->m_velocity, XYZ4(p.velocity)) && body->m_velocityW == 0.25f);
        RB(r.setup, slow ? body->m_state == RIGID_BODY_AT_REST && g_rbCalls.size() == 1 && g_rbCalls[0].event == 0 &&
                               g_rbCalls[0].context == &context && !g_rbCalls[0].contact
                         : body->m_state == RIGID_BODY_MOVING && g_rbCalls.empty());
        RB(r.setup, RbSame(body->m_centre, S_vector(dx * 0.5f + box.min.x, 0.5f * dy + box.min.y, dz * 0.5f + box.min.z)) &&
                        body->m_hasCentre);
        const float sx = dx + 0.05f, sy = dy + 0.05f, sz = dz + 0.05f;
        RB(r.setup, RbSame(body->m_invInertia, RbInertia(sx, sy, sz, p.weight)));
        const float smallest = std::min(sx, std::min(sy, sz)) * 0.5f;
        RB(r.setup, body->m_skin == std::min(smallest, 0.04f));
        const float cx = dx * 0.5f + box.min.x, cy = dy * 0.5f + box.min.y, cz = dz * 0.5f + box.min.z;
        const S_vector pts[14] = {
            box.min, S_vector(box.min.x, box.min.y, box.max.z), S_vector(box.max.x, box.min.y, box.max.z),
            S_vector(box.max.x, box.min.y, box.min.z), box.max, S_vector(box.min.x, box.max.y, box.min.z),
            S_vector(box.min.x, box.max.y, box.max.z), S_vector(box.max.x, box.max.y, box.min.z),
            S_vector(cx, cy, box.min.z), S_vector(cx, cy, box.max.z), S_vector(cx, box.min.y, cz),
            S_vector(cx, box.max.y, cz), S_vector(box.min.x, cy, cz), S_vector(box.max.x, cy, cz)};
        bool points = true;
        for (int i = 0; i < 14; ++i)
            points &= RbSame(XYZ4(body->m_points[i]), pts[i]);
        RB(r.setup, points);
        const float g = 10.0f, m0 = p.weight * 0.0f;
        RB(r.setup, body->m_gravity.x == m0 * g && body->m_gravity.y == (p.weight * -1.0f) * g &&
                        body->m_gravity.z == m0 * g && body->m_bounce == 1.01f && body->m_contactRange == 0.04f &&
                        body->m_mode == RIGID_BODY_BOX && body->m_stiffness == (g * p.weight) * 12.5f &&
                        body->m_unknown584 == p.weight * 4.0f && body->m_maxStep == 0.028f && body->m_g == 10.0f);
        // SetFrame again with the callback set: the state is set directly (no call, no volume).
        body->m_state = RIGID_BODY_MOVING;
        g_rbCalls.clear();
        g_volumeLog.clear();
        body->SetFrame(f);
        RB(r.frame, body->m_state == RIGID_BODY_AT_REST && g_rbCalls.empty() && g_volumeLog.empty());

        // Start: the volume and the timers; at rest unless it moves at 0.1 m/s.
        C_Human actorA;
        body->m_owner = static_cast<C_Actor *>(&actorA);
        for (int i = 0; i < 14; ++i) {
            body->m_pointHit[i] = body->m_pointOnActor[i] = true;
            body->m_pointActors[i] = static_cast<C_Actor *>(&actorA);
            body->m_lastPoints[i] = {1, 2, 3, 7};
            body->m_pointSurface[i] = 9;
        }
        body->m_spin = S_vector(1, 2, 3);
        body->m_pos = body->m_right = body->m_up = body->m_dir = {9, 9, 9, 9};
        body->m_slowTime = 3.0f;
        body->m_stillTime = 99;
        body->m_hardestHit = 4.0f;
        body->m_hitPointIndex = 5;
        body->m_unknown5B8 = 6;
        body->m_springForce = 2.0f;
        body->m_contactPoint = {1, 2, 3, 4};
        body->m_contactsChanged = false;
        body->m_nudged = false;
        body->m_state = RIGID_BODY_AT_REST;
        g_volumeLog.clear();
        g_rbCalls.clear();
        body->Start();
        const S_vector4 pos = body->m_pos;
        RB(r.start, std::memcmp(&body->m_pos, &w.m[3], 16) == 0 && std::memcmp(&body->m_right, &w.m[0], 16) == 0 &&
                        std::memcmp(&body->m_up, &w.m[1], 16) == 0 && std::memcmp(&body->m_dir, &w.m[2], 16) == 0);
        float sq = (box.min.x * box.min.x + box.min.y * box.min.y) + box.min.z * box.min.z;
        const float sqMax = (box.max.x * box.max.x + box.max.y * box.max.y) + box.max.z * box.max.z;
        if (sqMax > sq)
            sq = sqMax;
        const S_CollisionVolume &vol = body->m_volume;
        RB(r.start, g_volumeLog.size() == 2 && g_volumeLog[0].op == 'A' && g_volumeLog[1].op == 'M' &&
                        g_volumeLog[0].volume == &body->m_volume && vol.flags == 0x40 && vol.radius == RbLength(sq) &&
                        RbSame(vol.center, XYZ4(pos)) && vol.centerW == pos.w && RbSameBox(vol.bbox, box) &&
                        vol.matrix == &body->m_matrix && vol.invMatrix == &body->m_invMatrix && vol.frame == f &&
                        vol.actor == nullptr && body->m_owner == nullptr &&
                        vol.type == (p.moveCollision ? 0x801u : 0x80802u));
        bool cleared = true;
        for (int i = 0; i < 14; ++i)
            cleared &= !body->m_pointHit[i] && !body->m_pointOnActor[i] && !body->m_pointActors[i] &&
                       body->m_lastPoints[i].x == 0 && body->m_lastPoints[i].y == 0 && body->m_lastPoints[i].z == 0 &&
                       body->m_lastPoints[i].w == 7 && body->m_pointSurface[i] == 0;
        RB(r.start, cleared && RbSame(XYZ4(body->m_centreWorld), body->m_centre * body->m_matrix) &&
                        RbSame(body->m_spin, S_vector(0, 0, 0)) && body->m_restDelay == 0.6f && body->m_stillTime == 0 &&
                        std::memcmp(&body->m_restPos, &pos, 16) == 0 && body->m_hardestHit == 0 &&
                        body->m_hitPointIndex == 0 && body->m_unknown5B8 == 0 && body->m_timeScale == 1.0f &&
                        body->m_nudged && body->m_contactsChanged && body->m_springForce == 0 &&
                        body->m_contactPoint.x == 0 && body->m_contactPoint.z == 0 && body->m_contactPoint.w == 4);
        const float v2 = (body->m_velocity.x * body->m_velocity.x + body->m_velocity.y * body->m_velocity.y) +
                         body->m_velocity.z * body->m_velocity.z;
        RB(r.start, body->m_state == (v2 < 0.01f ? RIGID_BODY_AT_REST : RIGID_BODY_MOVING) &&
                        body->m_slowTime == 0.0f && g_rbCalls.empty());

        // SetState: the same state does nothing; coming to rest tells the callback; moving clears the
        // slow time; at rest (or fallen) a body with m_restVolume gets the doubled box.
        body->m_state = RIGID_BODY_MOVING;
        body->m_slowTime = 1.5f;
        g_rbCalls.clear();
        g_volumeLog.clear();
        body->SetState(RIGID_BODY_MOVING);
        RB(r.state, body->m_slowTime == 1.5f && g_rbCalls.empty() && g_volumeLog.empty());
        C_Human owner;
        body->SetOwner(static_cast<C_Actor *>(&owner));
        RB(r.state, body->m_owner == static_cast<C_Actor *>(&owner) && vol.actor == body->m_owner);
        body->m_volume.actor = nullptr;
        body->m_volume.frame = nullptr;
        body->m_volume.matrix = nullptr;
        body->m_volume.invMatrix = nullptr;
        body->SetState(RIGID_BODY_AT_REST);
        RB(r.state, body->m_state == RIGID_BODY_AT_REST && g_rbCalls.size() == 1 && g_rbCalls[0].event == 0 &&
                        g_rbCalls[0].unused == 0 && g_rbCalls[0].context == &context && !g_rbCalls[0].contact);
        if (body->m_restVolume) {
            RB(r.state, g_volumeLog.size() == 1 && g_volumeLog[0].op == 'M' && vol.actor == body->m_owner &&
                            vol.frame == f && vol.radius == RbLength(sq) + RbLength(sq) &&
                            RbSame(vol.bbox.min, S_vector(box.min.x * 2, box.min.y * 2, box.min.z * 2)) &&
                            RbSame(vol.bbox.max, S_vector(box.max.x * 2, box.max.y * 2, box.max.z * 2)) &&
                            vol.flags == 0x40 && RbSame(vol.center, XYZ4(body->m_pos)) && vol.matrix == &body->m_matrix &&
                            vol.invMatrix == &body->m_invMatrix);
        } else {
            RB(r.state, g_volumeLog.empty() && vol.actor == nullptr);
            body->m_volume.matrix = &body->m_matrix;
            body->m_volume.invMatrix = &body->m_invMatrix;
        }
        body->SetState(RIGID_BODY_MOVING);
        RB(r.state, body->m_state == RIGID_BODY_MOVING && body->m_slowTime == 0.0f && g_rbCalls.size() == 1);
        g_volumeLog.clear();
        body->SetState(RIGID_BODY_FELL);
        RB(r.state, body->m_state == RIGID_BODY_FELL && g_rbCalls.size() == 1 &&
                        g_volumeLog.size() == (body->m_restVolume ? 1u : 0u));
        body->SetState(RIGID_BODY_AT_REST);
        RB(r.state, g_rbCalls.size() == 1);
        body->m_callback = nullptr;
        body->SetState(RIGID_BODY_MOVING);
        body->SetState(RIGID_BODY_AT_REST);
        RB(r.state, g_rbCalls.size() == 1 && body->m_state == RIGID_BODY_AT_REST);
        body->m_callback = RbCallback;

        // The impulses: woken, the speed and spin (about the centre of mass, 1.1 times for ApplyImpulse);
        // ApplyImpulse keeps the hardest hit when there is a hit material.
        const S_vector v0 = body->m_velocity, s0 = body->m_spin;
        body->m_slowTime = 0.5f;
        body->m_stillTime = 50;
        g_rbCalls.clear();
        const S_vector4 at = {body->m_centreWorld.x + 0.3f, body->m_centreWorld.y - 0.2f, body->m_centreWorld.z + 0.1f, 1};
        const S_vector j(2.0f, -1.0f, 0.5f * k);
        RB(r.impulse, body->ApplyImpulse(5, at, j, 7, 8, 9));
        {
            const float rx = at.x - body->m_centreWorld.x, ry = at.y - body->m_centreWorld.y, rz = at.z - body->m_centreWorld.z;
            const float jx = j.x * 1.1f, jy = j.y * 1.1f, jz = j.z * 1.1f, im = 1.0f / body->m_mass;
            S_vector t(jz * ry - jy * rz, rz * jx - jz * rx, jy * rx - ry * jx);
            t = t * body->m_invRotation;
            t = S_vector(t.x * body->m_invInertia.x, t.y * body->m_invInertia.y, t.z * body->m_invInertia.z) * body->m_rotation;
            RB(r.impulse, body->m_state == RIGID_BODY_MOVING && body->m_slowTime == 0 && body->m_stillTime == 0 &&
                              g_rbCalls.empty() &&
                              RbSame(body->m_velocity, S_vector(jx * im + v0.x, jy * im + v0.y, jz * im + v0.z)) &&
                              RbSame(body->m_spin, S_vector(s0.x - t.x, s0.y - t.y, s0.z - t.z)) &&
                              body->m_hardestHit == 0);
        }
        body->m_hitMaterial = 7;
        body->m_hitPointIndex = 3;
        body->m_slowTime = 0.5f;                        // (moving already: cleared by the impulse itself)
        body->m_stillTime = 50;
        body->ApplyImpulse(0, at, j, 0, 0, 0);
        const float hard = RbLength((j.z * j.z + j.y * j.y) + j.x * j.x);
        RB(r.impulse, body->m_hardestHit == hard && body->m_hitPointIndex == 0 && body->m_pointSurface[0] == 7 &&
                          std::memcmp(&body->m_hitPoint, &at, 16) == 0 && body->m_slowTime == 0 &&
                          body->m_stillTime == 0);
        const S_vector4 at2 = {at.x + 1, at.y, at.z, 1};
        body->ApplyImpulse(0, at2, S_vector(j.x * 0.5f, j.y * 0.5f, j.z * 0.5f), 0, 0, 0);
        body->m_hitPointIndex = 3;
        body->ApplyImpulse(0, at2, j, 0, 0, 0);
        RB(r.impulse, body->m_hardestHit == hard && body->m_hitPointIndex == 3 && body->m_hitPoint.x == at.x);
        body->m_hitMaterial = -1;
        body->m_hardestHit = 0;
        body->ApplyImpulse(0, at2, j, 0, 0, 0);
        RB(r.impulse, body->m_hardestHit == 0);
        // The hit's size: none under 1e-8 squared; material 0 is one.
        body->m_hitMaterial = 0;
        body->m_hitPointIndex = 3;
        body->ApplyImpulse(0, at2, S_vector(1e-5f, 0, 0), 0, 0, 0);
        RB(r.impulse, body->m_hardestHit == 0 && body->m_hitPointIndex == 3);
        body->ApplyImpulse(0, at2, S_vector(0.01f, 0, 0), 0, 0, 0);
        RB(r.impulse, body->m_hardestHit == RbLength(0.01f * 0.01f) && body->m_hitPointIndex == 0 &&
                          body->m_pointSurface[0] == 0);
        body->m_hitMaterial = -1;
        body->m_hardestHit = 0;
        {
            const S_vector v1 = body->m_velocity, s1 = body->m_spin;
            body->m_state = RIGID_BODY_AT_REST;
            body->m_slowTime = 0.5f;
            body->m_stillTime = 50;
            body->AddImpulse(j, at);
            const float rx = at.x - body->m_centreWorld.x, ry = at.y - body->m_centreWorld.y, rz = at.z - body->m_centreWorld.z;
            const float im = 1.0f / body->m_mass;
            S_vector t(ry * j.z - rz * j.y, rz * j.x - rx * j.z, rx * j.y - ry * j.x);
            t = t * body->m_invRotation;
            t = S_vector(t.x * body->m_invInertia.x, t.y * body->m_invInertia.y, t.z * body->m_invInertia.z) * body->m_rotation;
            RB(r.impulse, body->m_state == RIGID_BODY_MOVING && body->m_slowTime == 0 && body->m_stillTime == 0 &&
                              RbSame(body->m_velocity, S_vector(im * j.x + v1.x, im * j.y + v1.y, im * j.z + v1.z)) &&
                              RbSame(body->m_spin, S_vector(s1.x - t.x, s1.y - t.y, s1.z - t.z)) && body->m_hardestHit == 0);
            body->m_slowTime = 0.5f;                    // (moving already)
            body->m_stillTime = 50;
            body->AddImpulse(S_vector(0, 0, 0), at);
            RB(r.impulse, body->m_slowTime == 0 && body->m_stillTime == 0);
        }
        // Wake and Nudge.
        body->m_state = RIGID_BODY_AT_REST;
        body->m_slowTime = 0.5f;
        body->m_stillTime = 50;
        RB(r.impulse, body->Wake(1, 2, 3, 4, 5, 6, 7, 8) && body->m_state == RIGID_BODY_MOVING && body->m_slowTime == 0 && body->m_stillTime == 0);
        body->m_slowTime = 0.5f;                        // (moving already)
        body->m_stillTime = 50;
        body->Wake(0, 0, 0, 0, 0, 0, 0, 0);
        RB(r.impulse, body->m_slowTime == 0 && body->m_stillTime == 0);
        body->m_slowTime = 0.5f;
        body->m_stillTime = 50;
        body->m_nudged = false;
        body->Nudge();
        RB(r.impulse, body->m_nudged && body->m_slowTime == 0.5f && body->m_stillTime == 50);
        body->Nudge();
        RB(r.impulse, body->m_slowTime == 0 && body->m_stillTime == 0);
        body->m_state = RIGID_BODY_AT_REST;
        body->m_nudged = false;
        body->m_slowTime = 0.5f;
        body->Nudge();
        RB(r.impulse, !body->m_nudged && body->m_slowTime == 0.5f);
        body->m_state = RIGID_BODY_MOVING;

        // Step: unless slow for longer than the rest delay or still for over 2 s; frames over 83 ms are
        // slowed; longer than m_maxStep, split.
        const auto step = [&](uint32_t time) {
            g_rbSteps.clear();
            body->m_timeScale = 5.0f;
            body->Step(time);
        };
        body->m_slowTime = 0.0f;
        body->m_stillTime = 0;
        step(20);
        RB(r.step, g_rbSteps.size() == 1 && g_rbSteps[0].first == 2 && g_rbSteps[0].second == 20 * 0.001f &&
                       body->m_timeScale == 1.0f);
        step(0);
        RB(r.step, g_rbSteps.empty() && body->m_timeScale == 1.0f);
        step(28);
        RB(r.step, g_rbSteps.size() == 1 && g_rbSteps[0].second == 28 * 0.001f);
        step(50);
        RB(r.step, g_rbSteps.size() == 2 && g_rbSteps[0].second == (50 * 0.001f) / 2.0f && g_rbSteps[1] == g_rbSteps[0]);
        step(100);
        const float dt83 = 83 * 0.001f;
        const int n83 = static_cast<int>(dt83 / 0.028f) + 1;
        RB(r.step, body->m_timeScale == 83.0f / 100.0f && static_cast<int>(g_rbSteps.size()) == n83 &&
                       g_rbSteps[0].second == dt83 / static_cast<float>(n83));
        step(84);
        RB(r.step, body->m_timeScale == 83.0f / 84.0f && static_cast<int>(g_rbSteps.size()) == n83);
        step(83);
        RB(r.step, body->m_timeScale == 1.0f && static_cast<int>(g_rbSteps.size()) == n83);
        body->m_slowTime = body->m_restDelay;
        step(20);
        RB(r.step, g_rbSteps.size() == 1);
        body->m_slowTime = body->m_restDelay + 0.001f;
        step(20);
        RB(r.step, g_rbSteps.empty() && body->m_timeScale == 5.0f);
        body->m_slowTime = 0.0f;
        body->m_stillTime = 2000;
        step(20);
        RB(r.step, g_rbSteps.size() == 1);
        body->m_stillTime = 2001;
        step(20);
        RB(r.step, g_rbSteps.empty());
        body->m_stillTime = 0;
        body->m_mode = RIGID_BODY_DISC;
        step(60);
        RB(r.step, g_rbSteps.size() == 3 && g_rbSteps[0].first == 1);
        body->m_mode = RIGID_BODY_BOX;

        // Tick: nothing at rest; else the step, the slow and still times, rest, the frame and volume.
        const auto tick = [&](uint32_t time) {
            g_rbSteps.clear();
            g_volumeLog.clear();
            g_sectorFrames.clear();
            g_frameUpdates.clear();
            g_rbCalls.clear();
            body->Tick(time);
        };
        body->m_state = RIGID_BODY_AT_REST;
        tick(20);
        RB(r.tick, g_rbSteps.empty() && g_frameUpdates.empty() && g_volumeLog.empty());
        body->m_state = RIGID_BODY_MOVING;
        body->m_hardestHit = 3.0f;
        tick(0);
        RB(r.tick, g_rbSteps.empty() && g_frameUpdates.empty() && g_volumeLog.empty() && body->m_hardestHit == 3.0f);
        body->m_velocity = S_vector(0.5f, 0, 0);
        body->m_spin = S_vector(0, 0, 0);
        body->m_slowTime = 0.3f;
        body->m_restPos = body->m_pos;
        body->m_stillTime = 100;
        body->m_hardestHit = 3.0f;
        body->m_hitPointIndex = 4;
        f->m_frameFlags &= ~FRMFLAGS_BOUND_VALID;
        // (The frame moved meanwhile: the matrices are taken again.)
        f->m_localMatrix.m[3][0] += 0.05f;
        tick(200);
        const S_matrix &w2 = f->GetMatrix();
        RB(r.tick, g_rbSteps.size() == 3 && body->m_slowTime == 0.0f && body->m_stillTime == 183 &&
                       g_frameUpdates[f] == 1 && g_sectorFrames.size() == 1 && g_sectorFrames[0].frame == f &&
                       RbSame(g_sectorFrames[0].pos, f->m_bound.center) && g_volumeLog.size() == 1 &&
                       g_volumeLog[0].op == 'M' && RbSame(vol.center, f->m_bound.center) && vol.centerW == 1.25f &&
                       std::memcmp(&body->m_matrix, &w2, sizeof w2) == 0 && body->m_pos.x == w2.m[3][0] &&
                       body->m_hardestHit == 0 && body->m_hitPointIndex == 0 &&
                       RbSame(XYZ4(body->m_centreWorld), body->m_centre * body->m_matrix));
        {
            const S_matrix inv2 = RbInverse(w2);
            bool rot2 = true;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) {
                    rot2 &= body->m_rotation.m[i][j] == (i == 3 && j < 3 ? 0.0f : w2.m[i][j]);
                    rot2 &= body->m_invRotation.m[i][j] == (i == 3 && j < 3 ? 0.0f : inv2.m[i][j]);
                }
            RB(r.tick, rot2 && std::memcmp(&body->m_invMatrix, &inv2, sizeof inv2) == 0 &&
                           std::memcmp(&body->m_pos, &w2.m[3], 16) == 0 && std::memcmp(&body->m_right, &w2.m[0], 16) == 0 &&
                           std::memcmp(&body->m_up, &w2.m[1], 16) == 0 && std::memcmp(&body->m_dir, &w2.m[2], 16) == 0);
            // Without a centre of mass the centre is the frame's place (taken after it moved).
            body->m_hasCentre = false;
            f->m_localMatrix.m[3][2] += 0.05f;
            tick(20);
            RB(r.tick, std::memcmp(&body->m_centreWorld, &f->GetMatrix().m[3], 16) == 0);
            body->m_hasCentre = true;
            body->m_stillTime = 183;
        }
        body->m_velocity = S_vector(0.2f, 0, 0.1f);
        body->m_spin = S_vector(0.05f, 0, 0);
        body->m_slowTime = 0.3f;
        tick(40);
        RB(r.tick, body->m_slowTime == (40 * 0.001f) + 0.3f && body->m_stillTime == 223);
        body->m_spin = S_vector(0.05f, 0, 0.1f);         // (0.112 rad/s)
        tick(40);
        RB(r.tick, body->m_slowTime == 0.0f);
        body->m_spin = S_vector(0.0f, 0, 0);
        body->m_velocity = S_vector(0.1f, 0.3f, 0);     // (0.316 m/s)
        body->m_slowTime = 0.1f;
        tick(40);
        RB(r.tick, body->m_slowTime == 0.0f);
        body->m_velocity = S_vector(0, 0, 0);
        // Moving 0.2 m: still no longer (0.202 m: x and y both needed; 0.192 m: still).
        body->m_restPos = body->m_pos;
        body->m_restPos.x -= 0.13f;
        body->m_restPos.y -= 0.155f;
        body->m_stillTime = 300;
        tick(40);
        RB(r.tick, body->m_stillTime == 0 && std::memcmp(&body->m_restPos, &body->m_pos, 16) == 0);
        body->m_restPos.x -= 0.12f;
        body->m_restPos.y -= 0.15f;
        body->m_stillTime = 300;
        tick(40);
        RB(r.tick, body->m_stillTime == 340 && body->m_restPos.x != body->m_pos.x);
        body->m_restPos = body->m_pos;
        // Slow for exactly the rest delay: not yet.
        body->m_mode = RIGID_BODY_DISC;
        body->m_slowTime = 0.5f;
        body->m_stillTime = 0;
        body->m_restDelay = (20 * 0.001f) + 0.5f;
        tick(20);
        RB(r.tick, body->m_state == RIGID_BODY_MOVING && body->m_slowTime == body->m_restDelay);
        body->m_restDelay = 0.6f;
        // Slow for longer than the rest delay, nothing under it: rest (Topple: true for this one?).
        body->m_slowTime = 0.6f;
        body->m_stillTime = 0;
        body->m_mode = RIGID_BODY_DISC;                 // (Topple: true for a disc)
        tick(20);
        RB(r.tick, body->m_state == RIGID_BODY_AT_REST && g_rbCalls.size() == 1);
        body->m_state = RIGID_BODY_MOVING;
        body->m_slowTime = 0.0f;
        body->m_stillTime = 2000;
        tick(1);
        RB(r.tick, body->m_state == RIGID_BODY_AT_REST && body->m_stillTime == 2001);
        body->m_state = RIGID_BODY_MOVING;
        body->m_stillTime = 1990;
        tick(10);
        RB(r.tick, body->m_state == RIGID_BODY_MOVING && body->m_stillTime == 2000);
        body->m_mode = RIGID_BODY_BOX;
        // Falling past 85 m/s.
        body->m_stillTime = 0;
        body->m_slowTime = 0.0f;
        body->m_velocity = S_vector(0, -86, 0);
        tick(10);
        RB(r.tick, body->m_state == RIGID_BODY_FELL);
        body->m_state = RIGID_BODY_MOVING;
        body->m_velocity = S_vector(0, -85, 0);
        tick(10);
        RB(r.tick, body->m_state == RIGID_BODY_MOVING);
        body->m_velocity = S_vector(0, 0, 0);
        // The actors under it: their times run out (the rest keep their order); at rest time, one
        // moving wakes it.
        C_Human actors[3];
        for (int i = 0; i < 3; ++i)
            static_cast<C_Actor *>(&actors[i])->m_position = {static_cast<float>(i), 1, 2, 1};
        body->m_actorContacts = new S_RigidBodyActorContact[4];
        body->m_actorContactsCapacity = 4;
        body->m_actorContactCount = 3;
        const int times[3] = {100, 30, 40};             // (the third reaches 0: kept)
        for (int i = 0; i < 3; ++i) {
            S_RigidBodyActorContact &c = body->m_actorContacts[i];
            c.actor = static_cast<C_Actor *>(&actors[i]);
            c.pos = static_cast<C_Actor *>(&actors[i])->m_position;
            c.time = times[i];
            c.point = i;
        }
        body->m_contactsChanged = false;
        tick(40);
        RB(r.tick, body->m_actorContactCount == 2 && body->m_actorContacts[0].point == 0 &&
                       body->m_actorContacts[1].point == 2 && body->m_actorContacts[0].time == 60 &&
                       body->m_actorContacts[1].time == 0 && body->m_contactsChanged);
        tick(40);
        RB(r.tick, body->m_actorContactCount == 1 && body->m_actorContacts[0].point == 0);
        body->m_actorContacts[0].time = 0;
        body->m_contactsChanged = false;
        tick(1);
        RB(r.tick, body->m_actorContactCount == 0 && body->m_contactsChanged);
        body->m_actorContactCount = 2;
        body->m_actorContacts[0] = {static_cast<C_Actor *>(&actors[0])->m_position,
                                    static_cast<C_Actor *>(&actors[0]), 500, 0, 0};
        body->m_actorContacts[1] = {static_cast<C_Actor *>(&actors[1])->m_position,
                                    static_cast<C_Actor *>(&actors[1]), 500, 1, 0};
        body->m_slowTime = 0.7f;
        body->m_contactsChanged = false;
        tick(10);
        RB(r.tick, body->m_state == RIGID_BODY_MOVING && body->m_slowTime > 0.7f && !body->m_contactsChanged &&
                       body->m_actorContacts[0].time == 500);
        // Under 4 mm: nothing; then the last record that moved over 4 mm (only it: the loop stops).
        static_cast<C_Actor *>(&actors[0])->m_position.y += 0.003f;
        tick(10);
        RB(r.tick, body->m_slowTime > 0.7f && !body->m_contactsChanged &&
                       body->m_actorContacts[0].pos.y != static_cast<C_Actor *>(&actors[0])->m_position.y);
        static_cast<C_Actor *>(&actors[0])->m_position.y += 0.01f;
        static_cast<C_Actor *>(&actors[1])->m_position.y += 0.0042f;
        body->m_stillTime = 900;
        tick(10);
        RB(r.tick, body->m_slowTime == 0.0f && body->m_stillTime == 0 && body->m_contactsChanged &&
                       body->m_actorContacts[1].pos.y == static_cast<C_Actor *>(&actors[1])->m_position.y &&
                       body->m_actorContacts[0].pos.y != static_cast<C_Actor *>(&actors[0])->m_position.y);
        // The first record moving (the loop reaches it).
        body->m_slowTime = 0.7f;
        body->m_contactsChanged = false;
        tick(10);
        RB(r.tick, body->m_slowTime == 0.0f && body->m_contactsChanged &&
                       body->m_actorContacts[0].pos.y == static_cast<C_Actor *>(&actors[0])->m_position.y);
        // One record under a disc at rest time: the records decide (Topple, true for a disc, is not asked).
        body->m_mode = RIGID_BODY_DISC;
        body->m_actorContactCount = 1;
        body->m_slowTime = 0.7f;
        tick(10);
        RB(r.tick, body->m_state == RIGID_BODY_MOVING);
        body->m_mode = RIGID_BODY_BOX;
        body->m_actorContactCount = 2;
        // ForgetActor: its records and its points.
        body->m_pointActors[0] = static_cast<C_Actor *>(&actors[1]);
        body->m_pointActors[3] = static_cast<C_Actor *>(&actors[1]);
        body->m_pointActors[5] = static_cast<C_Actor *>(&actors[0]);
        body->m_slowTime = 0.4f;
        body->m_stillTime = 70;
        body->m_contactsChanged = false;
        body->ForgetActor(static_cast<C_Actor *>(&actors[1]));
        RB(r.forget, body->m_actorContactCount == 1 && body->m_actorContacts[0].actor == static_cast<C_Actor *>(&actors[0]) &&
                         !body->m_pointActors[0] && !body->m_pointActors[3] && body->m_pointActors[5] && body->m_slowTime == 0 &&
                         body->m_stillTime == 0 && body->m_contactsChanged);
        body->m_actorContactCount = 3;
        body->m_actorContacts[1] = body->m_actorContacts[0];
        body->m_actorContacts[2] = body->m_actorContacts[0];
        body->m_actorContacts[2].actor = static_cast<C_Actor *>(&actors[2]);
        ForgetActor(static_cast<C_Actor *>(&actors[0]));
        RB(r.forget, body->m_actorContactCount == 1 &&
                         body->m_actorContacts[0].actor == static_cast<C_Actor *>(&actors[2]) && !body->m_pointActors[5]);
        // Every listed body forgets it.
        {
            C_RigidBody second;
            second.m_actorContacts = new S_RigidBodyActorContact[1];
            second.m_actorContactsCapacity = 1;
            second.m_actorContactCount = 1;
            second.m_actorContacts[0] = body->m_actorContacts[0];
            second.m_pointActors[7] = static_cast<C_Actor *>(&actors[2]);
            body->m_pointActors[9] = static_cast<C_Actor *>(&actors[2]);
            ForgetActor(static_cast<C_Actor *>(&actors[2]));
            RB(r.forget, body->m_actorContactCount == 0 && second.m_actorContactCount == 0 && !body->m_pointActors[9] &&
                             !second.m_pointActors[7]);
            body->m_actorContactCount = 1;
        }

        // Topple (boxes only): see the dedicated boxes below; with actors under it, false.
        body->m_mode = RIGID_BODY_BOX;
        RB(r.topple, !body->Topple());
        body->m_mode = RIGID_BODY_DISC;
        RB(r.topple, body->Topple());
        body->m_mode = RIGID_BODY_BOX;

        // The saved game: the record's bytes and their loading on another body of the frame.
        body->m_unknown5AC = 13;
        body->m_hitMaterial = 21;
        body->m_groundDistance = 0.5f;
        body->m_contactPoint = {7, 8, 9, 1};
        body->m_velocity = S_vector(1.25f, -2.5f, 0.75f);
        body->m_spin = S_vector(0.5f, -0.25f, 1.5f);
        body->m_width = 0.3f;
        body->m_radius = 0.45f;
        body->m_unknown594 = 0.2f;
        body->m_springForce = 3.5f;
        body->m_slowTime = 0.35f;
        body->m_maxStep = 0.025f;
        body->m_g = 9.0f;
        g_files.erase("rb.sav");
        C_chunk out;
        out.Open("rb.sav", true);
        RB(r.save, body->SaveGame(out));
        out.Close();
        const std::vector<uint8_t> bytes = g_files["rb.sav"];
        RB(r.save, bytes.size() == 0xA4);
        if (bytes.size() == 0xA4) {
            const auto at32 = [&](int o) { int32_t v; std::memcpy(&v, &bytes[o], 4); return v; };
            const auto atf = [&](int o) { float v; std::memcpy(&v, &bytes[o], 4); return v; };
            const S_quat &q = f->GetRot();
            const S_vector &wp = f->GetWorldPos();
            RB(r.save, at32(0) == 5 && at32(4) == body->m_mode && atf(8) == wp.x && atf(0xC) == wp.y && atf(0x10) == wp.z &&
                           atf(0x14) == body->m_centre.x && atf(0x1C) == body->m_centre.z && atf(0x20) == q.s &&
                           atf(0x24) == q.v.x && atf(0x2C) == q.v.z && atf(0x30) == body->m_maxStep &&
                           atf(0x34) == body->m_mass && atf(0x38) == body->m_invInertia.x && atf(0x40) == body->m_invInertia.z &&
                           atf(0x44) == body->m_velocity.x && atf(0x50) == body->m_spin.x && atf(0x58) == body->m_spin.z &&
                           atf(0x5C) == body->m_friction && atf(0x60) == body->m_bounce && atf(0x64) == body->m_contactRange &&
                           atf(0x68) == body->m_stiffness && atf(0x6C) == body->m_unknown584 && atf(0x70) == body->m_width &&
                           atf(0x74) == body->m_radius && atf(0x78) == body->m_unknown594 && atf(0x7C) == body->m_springForce &&
                           atf(0x80) == 7 && atf(0x88) == 9 && atf(0x8C) == body->m_contactRange &&
                           atf(0x90) == body->m_slowTime && at32(0x94) == 13 && at32(0x98) == 21 && bytes[0x9C] == 1 &&
                           atf(0xA0) == body->m_g);
        }
        // Loading: a body on a copy of the frame, elsewhere.
        {
            I3D_frame *copy = RbBoxFrame(nullptr, "rb.copy", -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f, S_vector(9, 9, 9));
            C_RigidBody other;
            other.SetFrame(copy);
            other.m_hasCentre = true;
            other.m_centre = S_vector(0, 0, 0);
            other.m_volume.radius = 3.0f;
            C_chunk in;
            in.Open("rb.sav", false);
            g_volumeLog.clear();
            g_frameUpdates.clear();
            for (int i = 0; i < 14; ++i)
                other.m_pointHit[i] = true;
            other.m_stillTime = 77;
            RB(r.save, other.LoadGame(in));
            in.Close();
            const S_matrix &cw = copy->GetMatrix();
            // (The stub's Update only counts: the rotation stays in m_rot, normalised.)
            S_quat q = f->GetRot();
            q.Normalize();
            RB(r.save, copy->m_rot.s == q.s && RbSame(copy->m_rot.v, q.v) && g_frameUpdates[copy] == 1 &&
                           std::memcmp(&other.m_right, &cw.m[0], 16) == 0 && std::memcmp(&other.m_dir, &cw.m[2], 16) == 0);
            RB(r.save, RbSame(copy->GetWorldPos(), f->GetWorldPos()) && RbSame(other.m_centre, body->m_centre) &&
                           other.m_mode == body->m_mode && other.m_mass == body->m_mass && other.m_g == body->m_g &&
                           RbSame(other.m_velocity, body->m_velocity) && RbSame(other.m_spin, body->m_spin) &&
                           RbSame(other.m_invInertia, body->m_invInertia) && other.m_friction == body->m_friction &&
                           other.m_bounce == body->m_bounce && other.m_contactRange == body->m_contactRange &&
                           other.m_groundDistance == body->m_contactRange && other.m_stiffness == body->m_stiffness &&
                           other.m_unknown584 == body->m_unknown584 && other.m_width == body->m_width &&
                           other.m_radius == body->m_radius && other.m_unknown594 == body->m_unknown594 &&
                           other.m_springForce == body->m_springForce && other.m_contactPoint.y == 8 &&
                           other.m_slowTime == body->m_slowTime && other.m_unknown5AC == 13 && other.m_hitMaterial == 21 &&
                           other.m_maxStep == body->m_maxStep);
            RB(r.save, std::memcmp(&other.m_matrix, &cw, sizeof cw) == 0 && std::memcmp(&other.m_pos, &cw.m[3], 16) == 0 &&
                           std::memcmp(&other.m_restPos, &cw.m[3], 16) == 0 && other.m_stillTime == 0 &&
                           RbSame(other.m_volume.center, XYZ4(other.m_pos)) && g_volumeLog.size() == 1 &&
                           g_volumeLog[0].op == 'M' && !other.m_pointHit[0] && !other.m_pointHit[13] &&
                           other.m_gravity.y == (other.m_mass * -1.0f) * other.m_g &&
                           RbSame(XYZ4(other.m_centreWorld), other.m_centre * other.m_matrix) &&
                           std::memcmp(&other.m_up, &cw.m[1], 16) == 0 && other.m_volume.radius == 3.0f);
            // Not a version 5 record, or too short a chunk: nothing loaded.
            g_files["rb.bad"] = bytes;
            g_files["rb.bad"][0] = 4;
            C_chunk bad;
            bad.Open("rb.bad", false);
            other.m_mass = 1234.0f;
            RB(r.save, !other.LoadGame(bad) && other.m_mass == 1234.0f);
            bad.Close();
            C_chunk shortOut;
            shortOut.Open("rb.short", true);
            shortOut.Begin(0x123);
            shortOut.Write(bytes.data(), 0x40);
            shortOut.Ascend();
            shortOut.Close();
            C_chunk shortIn;
            shortIn.Open("rb.short", false);
            uint16_t id = 0;
            shortIn.Descend(id);
            RB(r.save, !other.LoadGame(shortIn) && other.m_mass == 1234.0f);
            shortIn.Close();
            other.m_frame = nullptr;
        }

        // Stop: out of the collision world, the actors' records freed.
        g_volumeLog.clear();
        body->Stop();
        RB(r.stop, g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' && g_volumeLog[0].volume == &body->m_volume &&
                       !body->m_actorContacts && body->m_actorContactCount == 0 && body->m_actorContactsCapacity == 0);
        delete body;
        RB(r.list, g_rigidBodies.size() == bodies0);
    }

    // The list: a body leaves it from anywhere (the last takes its place).
    {
        const std::vector<C_RigidBody *> before = g_rigidBodies;
        auto *a = new C_RigidBody();
        auto *b = new C_RigidBody();
        auto *c = new C_RigidBody();
        std::vector<C_RigidBody *> expect = before;
        expect.push_back(c);
        expect.push_back(b);
        delete a;
        RB(r.list, g_rigidBodies == expect);
        delete b;
        expect.pop_back();
        RB(r.list, g_rigidBodies == expect);
        delete c;
        RB(r.list, g_rigidBodies == before);
    }

    // SetupWheel: a disc.
    {
        C_RigidBody wheel;
        wheel.SetFrame(bodyFrames.back());
        wheel.m_state = RIGID_BODY_MOVING;
        g_rbCalls.clear();
        S_WheelDebrisParams p;
        p.SetVelocity(S_vector(0.001f, 0, 0));
        p.radius = 0.4f;
        p.width = 0.3f;
        p.weight = 10.0f;
        p.unknown1C = 0.7f;
        p.unknown20 = 0.2f;
        p.callback = RbCallback;
        p.moveCollision = true;
        wheel.m_context = &context;
        wheel.SetupWheel(p);
        const float inv = 1.0f / (((0.4f * 0.4f) * 10.0f) * 0.5f);
        RB(r.wheel, wheel.m_mass == 10.0f && wheel.m_radius == 0.4f && wheel.m_width == 0.3f &&
                        RbSame(wheel.m_invInertia, S_vector(inv, inv, inv)) && wheel.m_friction == 0.7f &&
                        wheel.m_unknown594 == 0.2f && wheel.m_contactRange == 0.03f && wheel.m_bounce == 1.05f &&
                        wheel.m_mode == RIGID_BODY_DISC && wheel.m_stiffness == (10.0f * 10.0f) * 37.03704f &&
                        wheel.m_context == nullptr && wheel.m_callback == RbCallback && wheel.m_restVolume &&
                        wheel.m_moveCollision && wheel.m_gravity.y == (10.0f * -1.0f) * 10.0f &&
                        wheel.m_state == RIGID_BODY_AT_REST && g_rbCalls.empty());
        p.moveCollision = false;
        p.SetVelocity(S_vector(0, 0, 4));
        wheel.m_state = RIGID_BODY_MOVING;
        wheel.SetupWheel(p);
        RB(r.wheel, !wheel.m_restVolume && wheel.m_moveCollision && wheel.m_state == RIGID_BODY_MOVING &&
                        wheel.m_velocity.z == 4.0f);
    }

    // Topple: the most upright axis (its direction from the centre of mass) is the height; a box more
    // than 2.5 times taller than a side is pushed over (a twin given the oracle's push must move the
    // same, at 0.12 m/s); else it rests. Upright, tilted and off-centre boxes, the ratio's edges, uneven
    // bases, and ties (two axes as upright, at the world's origin: it rests).
    {
        I3D_frame *tall = NewFrame(FRAME_VISUAL, "rb.tall", nullptr);
        // The half sides along the up axis and its first and second others, the tilt about the world's
        // z (or x; radians), the centre's offset (a share of each half side); topples 1 / 0, -1 either,
        // -2 a tie. (1, 0.15, 0.1 at 0.6 off: the up axis's direction is 0.41 long, normalised.)
        struct S_ToppleCase {
            float up, first, second, tilt, offset;
            int topples;
            bool aboutX;
        };
        const S_ToppleCase cases[] = {
            {0.7f, 0.1f, 0.1f, 0, 0, 1, false},
            {0.5f, 0.5f, 0.5f, 0, 0, 0, false},
            {0.1f, 0.7f, 0.1f, 0, 0, 0, false},
            {0.245f, 0.1f, 0.1f, 0, 0, 0, false},
            {0.255f, 0.1f, 0.1f, 0, 0, 1, false},
            {0.7f, 0.1f, 0.3f, 0, 0, 1, false},
            {0.7f, 0.3f, 0.12f, 0, 0, 1, false},
            {0.7f, 0.1f, 0.15f, 0.25f, 0, 1, false},
            {0.7f, 0.15f, 0.1f, -0.3f, 0.5f, -1, false},
            {0.6f, 0.2f, 0.25f, 0.2f, -0.4f, -1, false},
            {1.0f, 0.15f, 0.1f, 0, 0.6f, 1, false},
            {0.7f, 0.1f, 0.15f, 0.3f, 0, 1, true},
            {0.6f, 0.15f, 0.2f, 0.35f, 0.3f, -1, true},
            {0.65f, 0.12f, 0.18f, -0.45f, -0.35f, -1, false},
            {0.7f, 0.1f, 0.1f, 0, 0, -2, false},
            {0.7f, 0.1f, 0.1f, 0, 0, -2, true},
        };
        for (int up = 0; up < 3; ++up) {
            const int first = up == 0 ? 1 : 0, second = up == 2 ? 1 : 2;
            for (const S_ToppleCase &tc : cases) {
                const bool tie = tc.topples == -2;
                float h[3];
                h[up] = tc.up, h[first] = tc.first, h[second] = tc.second;
                std::vector<float> &v = Stub(tall).vertices;
                v.clear();
                for (int c = 0; c < 8; ++c) {
                    const float p[8] = {(c & 1 ? h[0] : -h[0]) + h[0] * tc.offset, (c & 2 ? h[1] : -h[1]) + h[1] * tc.offset,
                                        (c & 4 ? h[2] : -h[2]) + h[2] * tc.offset, 0, 0, 0, 0, 0};
                    v.insert(v.end(), p, p + 8);
                }
                *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(tall) + 0x1D0) &= ~VISFLAGS_BOUND_VALID;
                tall->m_localMatrix.Identity();
                tall->m_localMatrix(3) = tie ? S_vector(0, 0, 0) : S_vector(1.5f, 3.0f, -2.0f);
                RbTurn(tall, up);
                if (tie)
                    RbTilt(tall, 0.70710677f, 0.70710677f, tc.aboutX);
                else if (tc.tilt != 0)
                    RbTilt(tall, std::cos(tc.tilt), std::sin(tc.tilt), tc.aboutX);
                const auto make = [&](C_RigidBody &b) {
                    b.SetFrame(tall);
                    S_RigidBodyParams p;
                    p.weight = 4.0f;
                    b.Setup(p);
                    b.Start();
                    b.m_velocity = S_vector(0, 0, 0);
                    b.m_spin = S_vector(0, 0, 0);
                    b.m_state = RIGID_BODY_AT_REST;
                };
                C_RigidBody box, twin;
                make(box);
                make(twin);
                S_vector impulse;
                S_vector4 at;
                const bool expect = RbToppleOracle(twin, tall, impulse, at);
                const bool rests = box.Topple();
                bool same = box.m_state == (expect ? RIGID_BODY_AT_REST : RIGID_BODY_MOVING);
                if (!expect) {
                    twin.AddImpulse(impulse, at);
                    const S_vector &u = box.m_velocity;
                    same = same && RbSame(box.m_velocity, twin.m_velocity) && RbSame(box.m_spin, twin.m_spin) &&
                           std::fabs(std::sqrt((u.x * u.x + u.y * u.y) + u.z * u.z) - 0.12f) < 1e-4f;
                }
                RB(r.topple, rests == expect && same && (tc.topples < 0 || expect == (tc.topples == 0)) &&
                                 (!tie || expect));
                box.Stop();
                twin.Stop();
            }
        }
        tall->m_localMatrix.Identity();
    }

    std::printf("rigid body: box %d setup %d wheel %d frame %d start %d state %d impulse %d step %d tick %d topple %d "
                "forget %d save %d stop %d list %d\n",
                r.box, r.setup, r.wheel, r.frame, r.start, r.state, r.impulse, r.step, r.tick, r.topple, r.forget, r.save,
                r.stop, r.list);
    g_countUpdates = false;
}
