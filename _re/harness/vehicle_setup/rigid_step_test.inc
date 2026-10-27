// Part 5b's test (the rigid body's step build, build_rigid_step.bat: -DRIGID_STEP_TEST links
// src/game/rigid_body.cpp and rigid_body_step.cpp and runs only RigidStepTest on every model): the
// integration steps in a world of one-sided planes (the harness's ray test hook). FilterContact,
// Collide and Integrate against oracles written from the original's code; StepBox and StepWheel
// against reference versions of the steps on a twin body (the same state on its own frame); then
// drops onto the ground, slopes and water for plausibility. check_rigid_step.py reads the result
// line. CAR_TRACE=1 prints the failing check lines. (Uses rigid_body_test.inc's helpers.)

#define RS_CHECK RB

// --- the world: one-sided infinite planes (n . p + d = 0; the front where it is over 0) ----------------

struct S_RbFace {
    S_vector n;
    float d;
    S_CollisionVolume *volume;
};
std::vector<S_RbFace> g_rbFaces;
struct S_RbRay {
    S_vector from, dir;
    uint32_t types, flags;
};
std::vector<S_RbRay> g_rbRays;
S_CollisionVolume *RbRayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                             uint32_t types, uint32_t flags)
{
    g_rbRays.push_back({from, dir, types, flags});
    const S_RbFace *best = nullptr;
    float bestT = 2.0f;
    for (const S_RbFace &f : g_rbFaces) {
        if ((f.volume->flags & flags) != 0 || (f.volume->type & types) != 0)
            continue;
        const float start = (f.n.x * from.x + f.n.y * from.y) + f.n.z * from.z + f.d;
        const float along = (f.n.x * dir.x + f.n.y * dir.y) + f.n.z * dir.z;
        if (start < 0.0f || along >= 0.0f)
            continue;
        const float t = -start / along;
        if (t > 1.0f || t >= bestT)
            continue;
        best = &f;
        bestT = t;
    }
    if (!best)
        return nullptr;
    *hitPos = S_vector(from.x + dir.x * bestT, from.y + dir.y * bestT, from.z + dir.z * bestT);
    *hitNormal = best->n;
    return best->volume;
}

// A plane through `at` facing `n` (unit).
S_RbFace RbFace(const S_vector &n, const S_vector &at, S_CollisionVolume *volume)
{
    return {n, -((n.x * at.x + n.y * at.y) + n.z * at.z), volume};
}

// --- the engine: the frames' world place, their moves, the particles -------------------------------------

struct S_RbPlace {
    I3D_frame *frame;
    S_vector pos, dir, up;
};
std::vector<S_RbPlace> g_rbPlaces;
void __stdcall F_RbSetWorldPosDir3(I3D_frame *self, const S_vector &pos, const S_vector &dir, const S_vector &up)
{
    g_rbPlaces.push_back({self, pos, dir, up});
    S_matrix m;
    m.Identity();
    m.SetDir3(dir, up);
    m(3) = pos;
    if (self->m_parent)
        m = m * RbInverse(self->m_parent->GetMatrix());
    self->m_localMatrix = m;
    self->m_frameFlags &= ~FRMFLAGS_BOUND_VALID;
}

std::vector<std::pair<I3D_frame *, S_vector>> g_rbSetPos;
void __stdcall F_RbSetPos(I3D_frame *self, const S_vector &pos)
{
    g_rbSetPos.push_back({self, pos});
    if (self->m_parent) {
        self->m_localMatrix(3) = pos * RbInverse(self->m_parent->GetMatrix());
    } else {
        self->m_localMatrix(3) = pos;
    }
}

struct S_RbSplash {
    int effect;
    S_vector pos, dir;
    I3D_frame *parent;
    bool faceCamera, transient;
};
std::vector<S_RbSplash> g_rbSplashes;
int C_ParticlesModule::CreateAt(int effect, const S_vector &pos, const S_vector &dir, I3D_frame *parent, bool faceCamera,
                                bool transient)
{
    g_rbSplashes.push_back({effect, pos, dir, parent, faceCamera, transient});
    return 1;
}

// --- the callback --------------------------------------------------------------------------------------

struct S_RbContactCall {
    int event, unused;
    void *context;
    S_RigidBodyContact contact;
};
std::vector<S_RbContactCall> g_rbContactCalls;
int g_rbAnswer;                                         // (1 refuses; the item actors answer 2)
int __fastcall RbContactCallback(int event, int unused, void *context, const S_RigidBodyContact *contact)
{
    g_rbContactCalls.push_back({event, unused, context, *contact});
    return g_rbAnswer;
}

// --- the oracles (from the original's code: each sum in its order) ------------------------------------------

void RbNormalize(S_vector &v, float sq)
{
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return;
    if (sq >= 1e-8f) {
        const float k = 1.0f / std::sqrt(sq);
        v = S_vector(v.x * k, v.y * k, v.z * k);
    } else if (v.x != 0.0f) {
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    } else {
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
    }
}

float RbDot(const S_vector &a, const S_vector &b)
{
    return (a.z * b.z + a.y * b.y) + a.x * b.x;
}

S_vector RbThrough(const C_RigidBody &b, S_vector t)
{
    t *= b.m_invRotation;
    t = S_vector(t.x * b.m_invInertia.x, t.y * b.m_invInertia.y, t.z * b.m_invInertia.z);
    t *= b.m_rotation;
    return t;
}

// Collide on the body's state (its velocity and spin read; `vel` / `spin` written).
void RbCollideOracle(C_RigidBody &b, S_vector &vel, S_vector &spin, const S_vector &n, const S_vector4 &p, int16_t index)
{
    const S_vector r(p.x - b.m_centreWorld.x, p.y - b.m_centreWorld.y, p.z - b.m_centreWorld.z);
    const S_vector w(r.z * b.m_spin.y - r.y * b.m_spin.z, r.x * b.m_spin.z - r.z * b.m_spin.x,
                     r.y * b.m_spin.x - r.x * b.m_spin.y);
    const S_vector v(b.m_velocity.x - w.x, b.m_velocity.y - w.y, b.m_velocity.z - w.z);
    const S_vector k = RbThrough(b, S_vector(r.y * n.z - r.z * n.y, r.z * n.x - r.x * n.z, r.x * n.y - r.y * n.x));
    const float vn = (v.z * n.z + v.y * n.y) + v.x * n.x;
    if (!(vn < 0.0f))
        return;
    const float j0 = -b.m_bounce * vn;
    const S_vector lever(k.y * r.z - k.z * r.y, k.z * r.x - r.z * k.x, r.y * k.x - k.y * r.x);
    const float im = 1.0f / b.m_mass;
    float den = ((lever.x * n.x + lever.y * n.y) + lever.z * n.z) + im;
    if (std::fabs(den) < 1e-6)
        den = 1e-6f;
    const float j = j0 / den;
    const S_vector pj(j * n.x, j * n.y, j * n.z);
    vel = S_vector(pj.x * im + vel.x, pj.y * im + vel.y, pj.z * im + vel.z);
    const S_vector t = RbThrough(b, S_vector(pj.z * r.y - pj.y * r.z, pj.x * r.z - pj.z * r.x, pj.y * r.x - pj.x * r.y));
    spin = S_vector(spin.x - t.x, spin.y - t.y, spin.z - t.z);
    const float strength = RbLength((pj.x * pj.x + pj.y * pj.y) + pj.z * pj.z);
    if (strength > b.m_hardestHit) {
        b.m_hardestHit = strength;
        b.m_hitPointIndex = static_cast<uint16_t>(index);
        b.m_hitPoint = p;
    }
    S_vector d = v;
    RbNormalize(d, (v.x * v.x + v.y * v.y) + v.z * v.z);
    S_vector along = d.Cross(n).Cross(n);
    RbNormalize(along, (along.x * along.x + along.y * along.y) + along.z * along.z);
    float f = static_cast<float>(static_cast<double>(strength * b.m_friction) * std::sin(static_cast<double>(v.AngleTo(n))));
    const float cap = ((b.m_g * b.m_friction) * b.m_mass) * 3.0f;
    if (f > cap)
        f = cap;
    along = S_vector(along.x * f, along.y * f, along.z * f);
    const float im2 = 1.0f / b.m_mass;
    vel = S_vector(along.x * im2 + vel.x, along.y * im2 + vel.y, along.z * im2 + vel.z);
    const S_vector drag = RbThrough(b, along.Cross(r));
    spin = S_vector(drag.x + spin.x, drag.y + spin.y, drag.z + spin.z);
}

S_quat RbCompose(const S_quat &a, const S_quat &b)
{
    S_quat q;
    q.s = a.s * b.s - ((a.v.z * b.v.z + a.v.y * b.v.y) + a.v.x * b.v.x);
    const S_vector c = a.v.Cross(b.v);
    q.v = S_vector((b.v.x * a.s + a.v.x * b.s) + c.x, (b.v.y * a.s + a.v.y * b.s) + c.y, (b.v.z * a.s + a.v.z * b.s) + c.z);
    return q;
}

// Integrate on the body's state (its frame placed through F_RbSetWorldPosDir3).
void RbIntegrateOracle(C_RigidBody &b, float dt, const S_vector &move)
{
    S_vector a(dt * b.m_spin.x, dt * b.m_spin.y, dt * b.m_spin.z);
    S_matrix m = b.m_invMatrix;
    m.m[3][0] = m.m[3][1] = m.m[3][2] = 0.0f;
    a *= m;
    S_quat q, s;
    q.Make(XYZ4(b.m_right), a.x);
    s.Make(XYZ4(b.m_up), a.y);
    q = RbCompose(q, s);
    s.Make(XYZ4(b.m_dir), a.z);
    q = RbCompose(q, s);
    const S_matrix rot = q.RotationMatrix();
    S_vector dir = XYZ4(b.m_dir), up = XYZ4(b.m_up);
    dir *= rot;
    up *= rot;
    if (b.m_hasCentre) {
        S_matrix t1, t2;
        t1.Identity();
        t2.Identity();
        const S_vector4 &c = b.m_centreWorld;
        t1.m[3][0] = -c.x, t1.m[3][1] = -c.y, t1.m[3][2] = -c.z;
        t2.m[3][0] = c.x, t2.m[3][1] = c.y, t2.m[3][2] = c.z;
        S_matrix about = (t1 * rot) * t2;
        S_vector pos = XYZ4(b.m_pos);
        pos *= about;
        b.m_pos.x = pos.x, b.m_pos.y = pos.y, b.m_pos.z = pos.z;
    }
    S_vector right(up.y * dir.z - dir.y * up.z, up.z * dir.x - up.x * dir.z, up.x * dir.y - up.y * dir.x);
    up = S_vector(right.z * dir.y - dir.z * right.y, dir.z * right.x - right.z * dir.x, right.y * dir.x - right.x * dir.y);
    RbNormalize(right, (right.x * right.x + right.y * right.y) + right.z * right.z);
    RbNormalize(up, (up.x * up.x + up.y * up.y) + up.z * up.z);
    RbNormalize(dir, (dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z);
    b.m_right.x = right.x, b.m_right.y = right.y, b.m_right.z = right.z;
    b.m_up.x = up.x, b.m_up.y = up.y, b.m_up.z = up.z;
    b.m_dir.x = dir.x, b.m_dir.y = dir.y, b.m_dir.z = dir.z;
    b.m_pos.x = move.x + b.m_pos.x;
    b.m_pos.y = move.y + b.m_pos.y;
    b.m_pos.z = move.z + b.m_pos.z;
    F_RbSetWorldPosDir3(b.m_frame, XYZ4(b.m_pos), dir, up);
}

// FilterContact's verdict (the callback left out).
S_CollisionVolume *RbFilterOracle(const C_RigidBody &b, S_CollisionVolume *v, bool *onActor)
{
    const int m = static_cast<int>(v->flags >> 24);
    if (m == 0x55 || m == 0x43)
        return v;
    if (m == 0x1F || m == 0x20 || m == 0x46)
        return nullptr;
    if ((v->flags & 0x40) != 0 && v->actor) {
        if (b.m_ignoreActors)
            return nullptr;
        *onActor = v->actor->GetType() == ACTOR_CAR || v->actor->GetType() == ACTOR_ANIMATION;
    }
    return v;
}

void RbRefresh(C_RigidBody &b)
{
    b.m_matrix = b.m_frame->GetMatrix();
    b.m_invMatrix = RbInverse(b.m_matrix);
    b.m_rotation = b.m_matrix;
    b.m_rotation.m[3][0] = b.m_rotation.m[3][1] = b.m_rotation.m[3][2] = 0.0f;
    b.m_invRotation = b.m_invMatrix;
    b.m_invRotation.m[3][0] = b.m_invRotation.m[3][1] = b.m_invRotation.m[3][2] = 0.0f;
}

// StepBox as read from the original (the twin's state; the contact filter has no callback here).
void RbStepBoxOracle(C_RigidBody &b, float dt)
{
    const float k = dt / b.m_mass;
    b.m_testTrailing = !b.m_testTrailing;
    S_vector &v = b.m_velocity;
    v = S_vector(k * b.m_gravity.x + v.x, k * b.m_gravity.y + v.y, k * b.m_gravity.z + v.z);
    const S_vector move(dt * v.x, dt * v.y, dt * v.z);
    const S_vector from = XYZ4(b.m_centreWorld);
    RbIntegrateOracle(b, dt, move);
    S_matrix w = b.m_frame->GetMatrix();
    if (b.m_hasCentre) {
        const S_vector c = b.m_centre * w;
        b.m_centreWorld.x = c.x, b.m_centreWorld.y = c.y, b.m_centreWorld.z = c.z;
    } else {
        b.m_centreWorld = b.m_pos;
    }
    C_Actor *actors[14] = {};
    S_vector push(0, 0, 0);
    if ((v.x * v.x + v.y * v.y) + v.z * v.z < 1.0f) {
        b.m_testTrailing = true;
        const float damp = 1.0f - dt * 0.5f;
        b.m_spin = S_vector(damp * b.m_spin.x, damp * b.m_spin.y, damp * b.m_spin.z);
    }
    bool contact = false;
    uint32_t mask = 0;
    S_vector pts[14], hits[14];
    S_vector normals[14];
    for (int i = 13; i >= 0; --i) {
        const S_vector p = XYZ4(b.m_points[i]) * w;
        pts[i] = p;
        const S_vector a(p.x - from.x, p.y - from.y, p.z - from.z);
        if (!((a.z * v.z + a.y * v.y) + a.x * v.x > 0.0f) && !b.m_testTrailing)
            continue;
        S_vector u = a;
        RbNormalize(u, (a.z * a.z + a.y * a.y) + a.x * a.x);
        const S_vector ray(u.x * 0.3f + a.x, a.y + u.y * 0.3f, a.z + u.z * 0.3f);
        bool retest = b.m_contactsChanged;
        for (int j = b.m_actorContactCount - 1; j >= 0; --j)
            if (b.m_actorContacts[j].point == i) {
                retest = true;
                break;
            }
        const S_vector4 &last = b.m_lastPoints[i];
        const float mx = p.x - last.x, my = p.y - last.y, mz = p.z - last.z;
        if (!((mz * mz + my * my) + mx * mx > b.m_skin) && !retest) {
            for (int j = 13; j >= 0; --j) {
                if (!b.m_pointHit[j])
                    continue;
                const S_plane &pl = b.m_pointPlanes[j];
                const float den = (ray.x * pl.normal.x + ray.z * pl.normal.z) + ray.y * pl.normal.y;
                if (std::fabs(den) < 1e-8f)
                    continue;
                const float t = -((RbDot(from, pl.normal) + pl.d) / den);
                hits[i] = S_vector(ray.x * t + from.x, ray.y * t + from.y, ray.z * t + from.z);
                const float depth = RbDot(p, pl.normal) - RbDot(pl.normal, hits[i]);
                b.m_groundDistance = depth;
                if (depth <= 0.0f) {
                    contact = true;
                    normals[i] = pl.normal;
                    mask |= 1u << i;
                    if (b.m_pointActors[j]) {
                        actors[i] = b.m_pointActors[j];
                        b.m_pointOnActor[i] = b.m_pointOnActor[j];
                    }
                    break;
                }
            }
            continue;
        }
        S_vector hp = hits[i], hn = XYZ4(b.m_normal);
        S_CollisionVolume *hit = RbRayTest(from, ray, &hp, &hn, 0x88002, 0x200000);
        if (hit && (hit->flags & 0x10000)) {
            b.m_g = 9.0f;
            if (RbLength((v.x * v.x + v.y * v.y) + v.z * v.z) > 1.0f)
                v = S_vector(v.x * 0.75f, v.y * 0.75f, v.z * 0.75f);
            g_rbSplashes.push_back({0x2D, S_vector(hp.x, hp.y + 0.02f, hp.z), S_vector(0, 1, 0), nullptr, false, false});
            hit = RbRayTest(from, ray, &hp, &hn, 0x88002, 0x210000);
        }
        b.m_normal.x = hn.x, b.m_normal.y = hn.y, b.m_normal.z = hn.z;
        hits[i] = hp;
        b.m_lastPoints[i].x = p.x, b.m_lastPoints[i].y = p.y, b.m_lastPoints[i].z = p.z;
        b.m_pointHit[i] = false;
        if (!hit)
            continue;
        S_CollisionVolume *f = RbFilterOracle(b, hit, &b.m_pointOnActor[i]);
        if (!f)
            continue;
        b.m_pointPlanes[i].normal = hn;
        b.m_pointPlanes[i].d = -((hp.z * hn.z + hn.y * hp.y) + hn.x * hp.x);
        b.m_pointHit[i] = true;
        b.m_pointActors[i] = nullptr;
        if (f->flags & 0x40) {
            actors[i] = f->actor;
            b.m_pointActors[i] = f->actor;
        }
        b.m_pointSurface[i] = static_cast<uint16_t>(f->flags >> 24);
        const float depth = ((hn.y * p.y + hn.z * p.z) + hn.x * p.x) - RbDot(hn, hp);
        b.m_groundDistance = depth;
        if (depth <= 0.0f) {
            contact = true;
            normals[i] = hn;
            mask |= 1u << i;
        }
    }
    if (contact) {
        for (int i = 13; i >= 0; --i) {
            if (!(mask & (1u << i)))
                continue;
            const S_vector &p = pts[i];
            const float rx = p.x - b.m_centreWorld.x, ry = p.y - b.m_centreWorld.y, rz = p.z - b.m_centreWorld.z;
            const S_vector tw(rz * b.m_spin.y - ry * b.m_spin.z, rx * b.m_spin.z - rz * b.m_spin.x,
                              ry * b.m_spin.x - rx * b.m_spin.y);
            const S_vector &n = normals[i];
            if (((v.z - tw.z) * n.z + (v.y - tw.y) * n.y) + (v.x - tw.x) * n.x < 0.0f) {
                S_vector vel = v, spin = b.m_spin;
                RbCollideOracle(b, vel, spin, n, {p.x, p.y, p.z, 0.0f}, static_cast<int16_t>(i));
                v = vel;
                b.m_spin = spin;
            }
            if (C_Actor *actor = actors[i]) {
                int j = b.m_actorContactCount - 1;
                while (j >= 0 && b.m_actorContacts[j].actor != actor)
                    --j;
                if (j >= 0) {
                    b.m_actorContacts[j].time = 1000;
                } else {
                    if (b.m_actorContactCount >= b.m_actorContactsCapacity) {
                        b.m_actorContactsCapacity += b.m_actorContactsGrowBy;
                        auto *grown = new S_RigidBodyActorContact[b.m_actorContactsCapacity];
                        for (int c = 0; c < b.m_actorContactCount; ++c)
                            grown[c] = b.m_actorContacts[c];
                        delete[] b.m_actorContacts;
                        b.m_actorContacts = grown;
                    }
                    b.m_actorContacts[b.m_actorContactCount++] = {actor->GetPosition4(), actor, 1000, i, 0};
                }
            }
        }
        w = b.m_frame->GetMatrix();
        for (int i = 13; i >= 0; --i) {
            if (!(mask & (1u << i)))
                continue;
            const S_vector p = XYZ4(b.m_points[i]) * w;
            const S_vector &n = normals[i];
            const S_vector &h = hits[i];
            const float depth = ((p.z * n.z + p.y * n.y) + p.x * n.x) - ((h.z * n.z + h.y * n.y) + h.x * n.x);
            b.m_groundDistance = depth;
            if (!(depth <= 0.0f))
                continue;
            const S_vector away(-depth * n.x, -depth * n.y, -depth * n.z);
            if (actors[i] && !b.m_pointOnActor[i])
                continue;
            if ((push.z * push.z + push.y * push.y) + push.x * push.x < (away.z * away.z + away.y * away.y) + away.x * away.x)
                push = away;
        }
    }
    b.m_pos.x = push.x + b.m_pos.x;
    b.m_pos.y = push.y + b.m_pos.y;
    b.m_pos.z = push.z + b.m_pos.z;
    if (b.m_g < 10.0f) {
        const float speed = RbLength((v.x * v.x + v.y * v.y) + v.z * v.z);
        if (speed > 0.5f) {
            S_vector u = v;
            RbNormalize(u, (u.x * u.x + u.y * u.y) + u.z * u.z);
            float s = speed - dt * 200.0f;
            if (s < 0.5f)
                s = 0.5f;
            v = S_vector(s * u.x, s * u.y, s * u.z);
        }
    }
    F_RbSetPos(b.m_frame, XYZ4(b.m_pos));
    RbRefresh(b);
    if (b.m_hasCentre) {
        const S_vector c = b.m_centre * b.m_matrix;
        b.m_centreWorld.x = c.x, b.m_centreWorld.y = c.y, b.m_centreWorld.z = c.z;
    } else {
        b.m_centreWorld = b.m_pos;
    }
    b.m_contactsChanged = false;
}

// The disc's rim contact (RimContact's oracle).
S_vector4 RbRimOracle(const S_plane &g, const S_vector4 &pos, const S_vector4 &right, float radius, float width, float &height)
{
    S_plane wheel;
    wheel.normal = XYZ4(right);
    wheel.d = -((right.z * pos.z + right.y * pos.y) + pos.x * right.x);
    S_vector lp, ld;
    S_vector base = XYZ4(pos);
    if (wheel.IntersectionLine(g, lp, ld)) {
        S_vector c(right.y * ld.z - right.z * ld.y, right.z * ld.x - right.x * ld.z, right.x * ld.y - right.y * ld.x);
        RbNormalize(c, (c.x * c.x + c.y * c.y) + c.z * c.z);
        base = S_vector(c.x * radius + pos.x, c.y * radius + pos.y, c.z * radius + pos.z);
    }
    float half = width * 0.5f;
    if ((g.normal.z * right.z + g.normal.y * right.y) + right.x * g.normal.x > 0.0f)
        half = -half;
    const S_vector4 at = {right.x * half + base.x, half * right.y + base.y, half * right.z + base.z, right.w};
    height = RbDot(S_vector(at.x, at.y, at.z), g.normal) + g.d;
    if (height <= 0.0f)
        height = 0.0f;
    return at;
}

// StepWheel as read from the original (the twin's state; the filter without a callback).
void RbStepWheelOracle(C_RigidBody &b, float dt)
{
    S_vector &v = b.m_velocity;
    const float speed = RbLength((v.x * v.x + v.y * v.y) + v.z * v.z);
    S_vector torque(0, 0, 0), force(0, 0, 0);
    if (b.m_springForce > 0.0f) {
        S_vector back(-v.x, -v.y, -v.z);
        RbNormalize(back, (back.z * back.z + back.y * back.y) + back.x * back.x);
        if (speed < 1.0f)
            back = S_vector(back.x * speed, back.y * speed, back.z * speed);
        const float res = (b.m_g * b.m_mass) * (b.m_unknown594 / b.m_radius);
        force = S_vector(back.x * res, back.y * res, back.z * res);
        const float c = -b.m_radius;
        const S_vector arm((c * b.m_normal.x + b.m_pos.x) - b.m_pos.x, (c * b.m_normal.y + b.m_pos.y) - b.m_pos.y,
                           (c * b.m_normal.z + b.m_pos.z) - b.m_pos.z);
        torque = S_vector(arm.z * force.y - arm.y * force.z, force.z * arm.x - arm.z * force.x, arm.y * force.x - force.y * arm.x);
        const S_vector a(c * b.m_normal.x, c * b.m_normal.y, c * b.m_normal.z);
        const S_vector tw(a.z * b.m_spin.y - a.y * b.m_spin.z, a.x * b.m_spin.z - a.z * b.m_spin.x,
                          a.y * b.m_spin.x - a.x * b.m_spin.y);
        const S_vector slide(v.x - tw.x, v.y - tw.y, v.z - tw.z);
        S_vector rub = slide;
        RbNormalize(rub, (slide.z * slide.z + slide.y * slide.y) + slide.x * slide.x);
        const float grip = (-b.m_friction * b.m_g) * b.m_mass;
        rub = S_vector(rub.x * grip, rub.y * grip, rub.z * grip);
        const double sine = std::sin(static_cast<double>(slide.AngleTo(XYZ4(b.m_normal))));
        if (sine < 1.0)
            rub = S_vector(static_cast<float>(rub.x * sine), static_cast<float>(rub.y * sine), static_cast<float>(rub.z * sine));
        const float ss = RbLength((slide.x * slide.x + slide.z * slide.z) + slide.y * slide.y);
        if (ss < 1.0f)
            rub = S_vector(rub.x * ss, rub.y * ss, rub.z * ss);
        force = S_vector(rub.x + force.x, rub.y + force.y, rub.z + force.z);
        torque = S_vector((a.z * rub.y - a.y * rub.z) + torque.x, (rub.z * a.x - a.z * rub.x) + torque.y,
                          (a.y * rub.x - rub.y * a.x) + torque.z);
        const S_vector sp(b.m_normal.x * b.m_springForce, b.m_normal.y * b.m_springForce, b.m_normal.z * b.m_springForce);
        force = S_vector(force.x + sp.x, force.y + sp.y, force.z + sp.z);
        const S_vector l(b.m_contactPoint.x - b.m_pos.x, b.m_contactPoint.y - b.m_pos.y, b.m_contactPoint.z - b.m_pos.z);
        torque = S_vector((l.z * sp.y - l.y * sp.z) + torque.x, (l.x * sp.z - l.z * sp.x) + torque.y,
                          (l.y * sp.x - l.x * sp.y) + torque.z);
    }
    const float k = dt / b.m_mass;
    v = S_vector((force.x + b.m_gravity.x) * k + v.x, k * (force.y + b.m_gravity.y) + v.y, k * (force.z + b.m_gravity.z) + v.z);
    const S_vector move(dt * v.x, dt * v.y, dt * v.z);
    const S_vector t = RbThrough(b, torque);
    b.m_spin = S_vector(t.x * dt + b.m_spin.x, t.y * dt + b.m_spin.y, t.z * dt + b.m_spin.z);
    const S_vector4 oldPos = b.m_pos, oldDir = b.m_dir, oldUp = b.m_up, oldRight = b.m_right;
    b.m_centreWorld = b.m_pos;
    RbIntegrateOracle(b, dt, move);
    b.m_springForce = 0.0f;
    b.m_groundDistance = b.m_contactRange;
    const float depth = -((((move.z + move.x) * 0.0f - move.y * 1.0f) + (b.m_radius + b.m_radius)) + b.m_contactRange);
    b.m_pointSurface[0] = 0;
    S_vector4 hit = b.m_contactPoint;
    S_vector hp = XYZ4(hit), hn = XYZ4(b.m_normal);
    bool onActor = false;
    S_CollisionVolume *vol = RbRayTest(XYZ4(oldPos), S_vector(0, depth, 0), &hp, &hn, 0x8002, 0x210000);
    b.m_normal.x = hn.x, b.m_normal.y = hn.y, b.m_normal.z = hn.z;
    hit.x = hp.x, hit.y = hp.y, hit.z = hp.z;
    if (vol && (vol = RbFilterOracle(b, vol, &onActor)) != nullptr) {
        if (b.m_stopOnFlat && (b.m_normal.z + b.m_normal.x) * 0.0f + b.m_normal.y > 0.9f) {
            v = S_vector(0, 0, 0);
            b.m_spin = S_vector(0, 0, 0);
            b.SetState(RIGID_BODY_AT_REST);
            return;
        }
        S_plane g;
        g.normal = hn;
        g.d = -((hit.y * hn.y + hit.z * hn.z) + hit.x * hn.x);
        b.m_groundDistance = (RbDot(XYZ4(b.m_pos), hn) + g.d) - b.m_radius;
        if (b.m_groundDistance <= b.m_contactRange) {
            b.m_pointSurface[0] = static_cast<uint16_t>(vol->flags >> 24);
            hit = RbRimOracle(g, b.m_pos, b.m_right, b.m_radius, b.m_width, b.m_groundDistance);
            b.m_springForce = (b.m_contactRange - b.m_groundDistance) * b.m_stiffness;
        }
    }
    b.m_contactPoint = hit;
    if (b.m_groundDistance < 0.0001f && (v.z * b.m_normal.z + v.y * b.m_normal.y) + v.x * b.m_normal.x < 0.0f) {
        b.m_dir = oldDir, b.m_up = oldUp, b.m_right = oldRight, b.m_pos = oldPos;
        F_RbSetWorldPosDir3(b.m_frame, XYZ4(b.m_pos), XYZ4(b.m_dir), XYZ4(b.m_up));
        b.m_centreWorld = b.m_pos;
        S_vector vel = v, spin = b.m_spin;
        RbCollideOracle(b, vel, spin, XYZ4(b.m_normal), hit, 0);
        v = vel;
        b.m_spin = spin;
        RbIntegrateOracle(b, dt, S_vector(dt * v.x, dt * v.y, dt * v.z));
    }
    b.m_groundDistance = b.m_contactRange;
    const S_vector step(dt * v.x, dt * v.y, dt * v.z);
    const float len = RbLength((step.x * step.x + step.y * step.y) + step.z * step.z);
    S_vector way = step;
    RbNormalize(way, (step.z * step.z + step.x * step.x) + step.y * step.y);
    const float reach = len + b.m_radius;
    way = S_vector(way.x * reach, reach * way.y, reach * way.z);
    S_vector4 n2 = {0, 0, 0, 0};
    hp = XYZ4(hit);
    hn = S_vector(0, 0, 0);
    vol = RbRayTest(XYZ4(oldPos), way, &hp, &hn, 0x8002, 0x210000);
    n2.x = hn.x, n2.y = hn.y, n2.z = hn.z;
    hit.x = hp.x, hit.y = hp.y, hit.z = hp.z;
    if (vol && RbFilterOracle(b, vol, &onActor)) {
        S_plane g;
        g.normal = hn;
        g.d = -((hn.x * hit.x + hn.z * hit.z) + hn.y * hit.y);
        b.m_groundDistance = (RbDot(XYZ4(b.m_pos), hn) + g.d) - b.m_radius;
        if (b.m_groundDistance <= b.m_contactRange)
            hit = RbRimOracle(g, b.m_pos, b.m_right, b.m_radius, b.m_width, b.m_groundDistance);
    }
    if (b.m_groundDistance < 0.0001f && (n2.z * v.z + n2.y * v.y) + n2.x * v.x < 0.0f) {
        b.m_centreWorld = b.m_pos;
        S_vector vel = v, spin = b.m_spin;
        RbCollideOracle(b, vel, spin, XYZ4(n2), hit, 0);
        v = vel;
        b.m_spin = spin;
    }
    RbRefresh(b);
    b.m_centreWorld = b.m_pos;
}

// --- comparing two bodies ----------------------------------------------------------------------------------

bool RbSame4(const S_vector4 &a, const S_vector4 &b, bool w = false)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && (!w || a.w == b.w);
}

bool RbSameMatrix(const S_matrix &a, const S_matrix &b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// The fields the steps change (bitwise; the w the original leaves unset are not compared).
int RbDiff(const C_RigidBody &a, const C_RigidBody &b)
{
    int bad = 0;
    const auto check = [&](bool ok, int what) {
        if (!ok && !bad)
            bad = what;
    };
    check(RbSame(a.m_velocity, b.m_velocity), 1);
    check(RbSame(a.m_spin, b.m_spin), 2);
    check(RbSame4(a.m_pos, b.m_pos), 3);
    check(RbSame4(a.m_dir, b.m_dir) && RbSame4(a.m_up, b.m_up) && RbSame4(a.m_right, b.m_right), 4);
    check(RbSameMatrix(a.m_matrix, b.m_matrix) && RbSameMatrix(a.m_invMatrix, b.m_invMatrix) &&
              RbSameMatrix(a.m_rotation, b.m_rotation) && RbSameMatrix(a.m_invRotation, b.m_invRotation), 5);
    check(RbSame4(a.m_centreWorld, b.m_centreWorld), 6);
    check(a.m_groundDistance == b.m_groundDistance && a.m_springForce == b.m_springForce, 7);
    check(RbSame4(a.m_contactPoint, b.m_contactPoint, true), 8);
    check(a.m_hardestHit == b.m_hardestHit && a.m_hitPointIndex == b.m_hitPointIndex && RbSame4(a.m_hitPoint, b.m_hitPoint), 9);
    check(a.m_g == b.m_g && a.m_testTrailing == b.m_testTrailing && a.m_contactsChanged == b.m_contactsChanged &&
              a.m_state == b.m_state, 10);
    bool points = true;
    for (int i = 0; i < 14; ++i) {
        points &= RbSame4(a.m_lastPoints[i], b.m_lastPoints[i]) && a.m_pointHit[i] == b.m_pointHit[i] &&
                  a.m_pointActors[i] == b.m_pointActors[i] && a.m_pointOnActor[i] == b.m_pointOnActor[i] &&
                  a.m_pointSurface[i] == b.m_pointSurface[i];
        if (a.m_pointHit[i] || b.m_pointHit[i])
            points &= RbSame(a.m_pointPlanes[i].normal, b.m_pointPlanes[i].normal) && a.m_pointPlanes[i].d == b.m_pointPlanes[i].d;
    }
    check(points, 11);
    bool records = a.m_actorContactCount == b.m_actorContactCount && a.m_actorContactsCapacity == b.m_actorContactsCapacity;
    for (int i = 0; records && i < a.m_actorContactCount; ++i) {
        const S_RigidBodyActorContact &x = a.m_actorContacts[i], &y = b.m_actorContacts[i];
        records = RbSame4(x.pos, y.pos, true) && x.actor == y.actor && x.time == y.time && x.point == y.point;
    }
    check(records, 12);
    check(a.m_normal.x == b.m_normal.x && a.m_normal.y == b.m_normal.y && a.m_normal.z == b.m_normal.z, 13);
    return bad;
}

// `to` takes `from`'s state on its own frame (a copy of `from`'s, at the same place).
void RbCopy(C_RigidBody &to, const C_RigidBody &from, I3D_frame *frame)
{
    S_RigidBodyActorContact *contacts = to.m_actorContacts;
    std::memcpy(static_cast<void *>(&to), &from, sizeof(C_RigidBody));
    to.m_frame = frame;
    frame->m_localMatrix = from.m_frame->m_localMatrix;
    delete[] contacts;
    to.m_actorContacts = nullptr;
    if (from.m_actorContactsCapacity) {
        to.m_actorContacts = new S_RigidBodyActorContact[from.m_actorContactsCapacity];
        for (int i = 0; i < from.m_actorContactCount; ++i)
            to.m_actorContacts[i] = from.m_actorContacts[i];
    }
    to.m_volume.matrix = &to.m_matrix;
    to.m_volume.invMatrix = &to.m_invMatrix;
    to.m_volume.frame = frame;
}

// --- the test ------------------------------------------------------------------------------------------

struct S_Rs {
    bool filter = true, collide = true, integrate = true, box = true, boxRays = true, boxContact = true, water = true,
         actors = true, wheel = true, drop = true;
};

void RigidStepTest(const char *path)
{
    if (!LoadModel(path))
        return;
    std::printf("== %s\n", g_modelFile.c_str());
    g_frameVtable[0x04 / 4] = reinterpret_cast<void *>(&F_RbVisualType);
    g_frameVtable[0x90 / 4] = reinterpret_cast<void *>(&F_RbUpdateLocalBound);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_RbUpdateBound);
    g_frameVtable[0x10 / 4] = reinterpret_cast<void *>(&F_RbSetWorldPosDir3);
    void *const oldSetPos = g_frameVtable[0x08 / 4];
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_RbSetPos);
    g_rayTestHook = &RbRayTest;
    g_countUpdates = true;
    GetGame()->m_scene = g_carScene;
    S_Rs r;
    // The model's hash: its first box frames' sizes (each model a different set of bodies).
    std::vector<I3D_frame *> modelFrames;
    for (S_StubFrame *s : g_frames) {
        const I3D_bbox b = RbBox(s->frame);
        if (modelFrames.size() < 6 && b.min.x <= b.max.x && b.max.x - b.min.x > 0.05f && b.max.y - b.min.y > 0.05f &&
            b.max.z - b.min.z > 0.05f)
            modelFrames.push_back(s->frame);
    }
    int seed = static_cast<int>(modelFrames.size()) * 7 + static_cast<int>(g_frames.size());
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };

    S_CollisionVolume ground, slope, waterSurface, deep, actorVolume, carVolume, paper, canvas, cold;
    ground.flags = 0x07000000;
    slope.flags = 0x09000000;
    waterSurface.flags = 0x1F000000 | COLLISION_FLAG_WATER;
    deep.flags = 0x0B000000;
    C_Human human;
    C_Human carLike;                                     // (its type set to a car's)
    static_cast<C_Actor *>(&carLike)->m_type = ACTOR_CAR;
    static_cast<C_Actor *>(&human)->m_position = {1, 0, 2, 1};
    static_cast<C_Actor *>(&carLike)->m_position = {-1, 0, 3, 1};
    actorVolume.flags = 0x05000000 | COLLISION_FLAG_DYNAMIC;
    actorVolume.actor = static_cast<C_Actor *>(&human);
    carVolume.flags = 0x06000000 | COLLISION_FLAG_DYNAMIC;
    carVolume.actor = static_cast<C_Actor *>(&carLike);
    paper.flags = 0x55000000 | COLLISION_FLAG_DYNAMIC;
    paper.actor = static_cast<C_Actor *>(&human);
    canvas.flags = 0x43000000;
    cold.flags = 0x46000000;
    int context = 0;

    // --- FilterContact: the callback first (a contact record; true refuses), then the materials, then
    // an actor's volume.
    {
        I3D_frame *f = RbBoxFrame(nullptr, "rs.filter", -0.2f, 0.2f, -0.1f, 0.1f, -0.3f, 0.3f, S_vector(0, 1, 0));
        C_RigidBody b;
        b.SetFrame(f);
        b.m_callback = RbContactCallback;
        b.m_context = &context;
        const S_vector4 pos = {1, 2, 3, 4}, normal = {0, 1, 0, 0.5f};
        bool onActor = true;
        g_rbContactCalls.clear();
        g_rbAnswer = 1;
        RS_CHECK(r.filter, !b.FilterContact(&ground, pos, normal, &onActor) && g_rbContactCalls.size() == 1 &&
                               g_rbContactCalls[0].event == 1 && g_rbContactCalls[0].context == &context &&
                               g_rbContactCalls[0].contact.volume == &ground && g_rbContactCalls[0].contact.posW == 4 &&
                               g_rbContactCalls[0].contact.pos.z == 3 && g_rbContactCalls[0].contact.normalW == 0.5f &&
                               g_rbContactCalls[0].contact.normal.y == 1 && onActor);
        g_rbAnswer = 0;
        RS_CHECK(r.filter, b.FilterContact(&ground, pos, normal, &onActor) == &ground && onActor && g_rbContactCalls.size() == 2);
        g_rbAnswer = 2;
        RS_CHECK(r.filter, b.FilterContact(&ground, pos, normal, &onActor) == &ground && g_rbContactCalls.size() == 3);
        b.m_callback = nullptr;
        RS_CHECK(r.filter, !b.FilterContact(&waterSurface, pos, normal, &onActor) && !b.FilterContact(&cold, pos, normal, &onActor));
        S_CollisionVolume swim;
        swim.flags = 0x20000000;
        RS_CHECK(r.filter, !b.FilterContact(&swim, pos, normal, &onActor) && b.FilterContact(&canvas, pos, normal, &onActor) == &canvas);
        b.m_ignoreActors = true;
        onActor = false;
        RS_CHECK(r.filter, b.FilterContact(&paper, pos, normal, &onActor) == &paper && !onActor &&
                               !b.FilterContact(&actorVolume, pos, normal, &onActor) &&
                               !b.FilterContact(&carVolume, pos, normal, &onActor));
        S_CollisionVolume canvasActor;                  // (tent canvas on an actor: counts, its actor not asked)
        canvasActor.flags = 0x43000000 | COLLISION_FLAG_DYNAMIC;
        canvasActor.actor = static_cast<C_Actor *>(&carLike);
        RS_CHECK(r.filter, b.FilterContact(&canvasActor, pos, normal, &onActor) == &canvasActor && !onActor);
        b.m_ignoreActors = false;
        onActor = true;
        RS_CHECK(r.filter, b.FilterContact(&actorVolume, pos, normal, &onActor) == &actorVolume && !onActor);
        RS_CHECK(r.filter, b.FilterContact(&carVolume, pos, normal, &onActor) == &carVolume && onActor);
        static_cast<C_Actor *>(&carLike)->m_type = ACTOR_ANIMATION;
        onActor = false;
        RS_CHECK(r.filter, b.FilterContact(&carVolume, pos, normal, &onActor) == &carVolume && onActor);
        static_cast<C_Actor *>(&carLike)->m_type = ACTOR_CAR;
        // A dynamic volume without an actor, an actor's without the dynamic flag: onActor untouched.
        S_CollisionVolume lone, notDynamic;
        lone.flags = 0x05000000 | COLLISION_FLAG_DYNAMIC;
        notDynamic.flags = 0x05000000;
        notDynamic.actor = static_cast<C_Actor *>(&human);
        onActor = true;
        RS_CHECK(r.filter, b.FilterContact(&lone, pos, normal, &onActor) == &lone && b.FilterContact(&notDynamic, pos, normal, &onActor) &&
                               onActor);
    }

    // --- The bodies: the model's box frames and made boxes, each set up as a box.
    std::vector<I3D_frame *> bodyFrames = modelFrames;
    const float shapes[][3] = {{0.4f, 0.4f, 0.4f}, {0.3f, 1.2f, 0.3f}, {1.0f, 0.2f, 0.6f}, {0.15f, 0.15f, 0.9f}};
    int si = 0;
    for (const auto &s : shapes) {
        char name[32];
        std::snprintf(name, sizeof name, "rs.shape%d", si++);
        I3D_frame *sf = RbBoxFrame(nullptr, name, -s[0] * 0.45f, s[0] * 0.55f, -s[1] * 0.5f, s[1] * 0.5f, -s[2] * 0.6f,
                                   s[2] * 0.4f, S_vector(rnd(-2, 2), rnd(1, 3), rnd(-2, 2)));
        RbTilt(sf, 0.8f, 0.6f, si % 2 == 0);
        bodyFrames.push_back(sf);
    }
    I3D_frame *twinFrame = RbBoxFrame(nullptr, "rs.twin", 0, 0, 0, 0, 0, 0, S_vector(0, 0, 0));
    const auto setup = [&](C_RigidBody &b, I3D_frame *f, float weight) {
        b.SetFrame(f);
        S_RigidBodyParams p;
        p.SetVelocity(S_vector(0.5f, 0.0f, 0.0f));
        p.weight = weight;
        p.friction = rnd(0.2f, 0.9f);
        b.Setup(p);
        b.Start();
    };
    const auto twinOf = [&](C_RigidBody &twin, const C_RigidBody &b) {
        Stub(twinFrame).vertices = Stub(b.m_frame).vertices;
        twinFrame->m_parent = nullptr;
        RbCopy(twin, b, twinFrame);
        // (A model frame's place under its parent becomes the twin's world place.)
        twinFrame->m_localMatrix = b.m_frame->GetMatrix();
    };

    for (size_t fi = 0; fi < bodyFrames.size(); ++fi) {
        I3D_frame *f = bodyFrames[fi];
        const S_matrix place = f->m_localMatrix;
        C_RigidBody body;
        setup(body, f, rnd(1.0f, 20.0f));

        // --- Collide: oracle on random points, normals and states.
        for (int c = 0; c < 7; ++c) {
            body.m_velocity = S_vector(rnd(-3, 3), rnd(-5, 1), rnd(-3, 3));
            if (c == 6)
                body.m_velocity = S_vector(60.0f, -120.0f, 20.0f);   // (fast and oblique: the friction's cap)
            body.m_spin = S_vector(rnd(-2, 2), rnd(-2, 2), rnd(-2, 2));
            body.m_hardestHit = c == 3 ? 1e9f : rnd(0, 2);
            body.m_hitPointIndex = 77;
            S_vector n(rnd(-0.3f, 0.3f), 1.0f, rnd(-0.3f, 0.3f));
            n = n * (1.0f / n.Magnitude());
            const S_vector4 p = {body.m_centreWorld.x + rnd(-0.5f, 0.5f), body.m_centreWorld.y - rnd(0.1f, 0.6f),
                                 body.m_centreWorld.z + rnd(-0.5f, 0.5f), 1};
            if (c == 4)
                body.m_friction = 50.0f;                // (the cap)
            C_RigidBody twin;
            twinOf(twin, body);
            S_vector v = twin.m_velocity, s = twin.m_spin;
            RbCollideOracle(twin, v, s, n, p, static_cast<int16_t>(c));
            twin.m_velocity = v;
            twin.m_spin = s;
            body.Collide(body.m_velocity, body.m_spin, n, p, 0.02f, static_cast<int16_t>(c), 0);
            RS_CHECK(r.collide, RbSame(body.m_velocity, twin.m_velocity) && RbSame(body.m_spin, twin.m_spin) &&
                                    body.m_hardestHit == twin.m_hardestHit && body.m_hitPointIndex == twin.m_hitPointIndex &&
                                    RbSame4(body.m_hitPoint, twin.m_hitPoint, true));
        }
        {
            // Moving away: nothing. A huge mass straight under the centre: the denominator's floor.
            const S_vector v0 = body.m_velocity = S_vector(0, 2, 0), s0 = body.m_spin = S_vector(0, 0, 0);
            body.m_hardestHit = 0;
            body.Collide(body.m_velocity, body.m_spin, S_vector(0, 1, 0), body.m_centreWorld, 0.02f, 3, 0);
            RS_CHECK(r.collide, RbSame(body.m_velocity, v0) && RbSame(body.m_spin, s0) && body.m_hardestHit == 0);
            const float mass = body.m_mass;
            body.m_mass = 1e7f;
            body.m_velocity = S_vector(0, -2, 0);
            C_RigidBody twin;
            twinOf(twin, body);
            const S_vector4 under = {body.m_centreWorld.x, body.m_centreWorld.y - 0.3f, body.m_centreWorld.z, 1};
            S_vector v = twin.m_velocity, s = twin.m_spin;
            RbCollideOracle(twin, v, s, S_vector(0, 1, 0), under, 5);
            body.Collide(body.m_velocity, body.m_spin, S_vector(0, 1, 0), under, 0.02f, 5, 0);
            RS_CHECK(r.collide, RbSame(body.m_velocity, v) && RbSame(body.m_spin, s) && body.m_velocity.y < 0.0f);   // (the floor)
            body.m_mass = mass;
        }

        // --- Integrate: the oracle, with and without a centre of mass.
        for (int c = 0; c < 3; ++c) {
            body.m_spin = c == 2 ? S_vector(0, 0, 0) : S_vector(rnd(-6, 6), rnd(-6, 6), rnd(-6, 6));
            body.m_hasCentre = c != 1;
            const S_vector move(rnd(-0.1f, 0.1f), rnd(-0.1f, 0.1f), rnd(-0.1f, 0.1f));
            C_RigidBody twin;
            twinOf(twin, body);
            g_rbPlaces.clear();
            body.Integrate(0.028f, move);
            RbIntegrateOracle(twin, 0.028f, move);
            RS_CHECK(r.integrate, g_rbPlaces.size() == 2 && g_rbPlaces[0].frame == body.m_frame &&
                                      RbSame(g_rbPlaces[0].pos, g_rbPlaces[1].pos) && RbSame(g_rbPlaces[0].dir, g_rbPlaces[1].dir) &&
                                      RbSame(g_rbPlaces[0].up, g_rbPlaces[1].up) && RbSame4(body.m_pos, twin.m_pos) &&
                                      RbSame4(body.m_dir, twin.m_dir) && RbSame4(body.m_up, twin.m_up) &&
                                      RbSame4(body.m_right, twin.m_right));
            // Unit axes square to each other.
            const S_vector x = XYZ4(body.m_right), y = XYZ4(body.m_up), z = XYZ4(body.m_dir);
            RS_CHECK(r.integrate, std::fabs(x.Dot(y)) < 1e-4f && std::fabs(y.Dot(z)) < 1e-4f && std::fabs(x.Dot(z)) < 1e-4f &&
                                      std::fabs(x.Dot(x) - 1) < 1e-4f && std::fabs(z.Dot(z) - 1) < 1e-4f);
            body.m_hasCentre = true;
            RbRefresh(body);
        }
        body.Stop();
        f->m_localMatrix = place;
    }

    // --- StepBox against the reference on a twin, in worlds of growing contact.
    const auto stepBoth = [&](C_RigidBody &b, bool wheel, float dt) {
        C_RigidBody twin;
        twinOf(twin, b);
        g_rbRays.clear();
        g_rbSplashes.clear();
        g_rbSetPos.clear();
        wheel ? b.StepWheel(dt) : b.StepBox(dt);
        const std::vector<S_RbRay> rays = g_rbRays;
        const std::vector<S_RbSplash> splashes = g_rbSplashes;
        const size_t setPos = g_rbSetPos.size();
        g_rbRays.clear();
        g_rbSplashes.clear();
        wheel ? RbStepWheelOracle(twin, dt) : RbStepBoxOracle(twin, dt);
        bool same = rays.size() == g_rbRays.size() && splashes.size() == g_rbSplashes.size();
        for (size_t i = 0; same && i < rays.size(); ++i)
            same = RbSame(rays[i].from, g_rbRays[i].from) && RbSame(rays[i].dir, g_rbRays[i].dir) &&
                   rays[i].types == g_rbRays[i].types && rays[i].flags == g_rbRays[i].flags;
        for (size_t i = 0; same && i < splashes.size(); ++i)
            same = splashes[i].effect == g_rbSplashes[i].effect && RbSame(splashes[i].pos, g_rbSplashes[i].pos) &&
                   RbSame(splashes[i].dir, g_rbSplashes[i].dir) && !splashes[i].parent && !splashes[i].faceCamera &&
                   !splashes[i].transient;
        const int diff = RbDiff(b, twin);
        if (std::getenv("CAR_TRACE") && (diff || !same))
            std::printf("  step: diff %d rays %d/%d same %d\n", diff, static_cast<int>(rays.size()),
                        static_cast<int>(g_rbRays.size()), same);
        return std::make_pair(diff == 0 && same && setPos == (wheel ? 0u : 1u), rays);
    };
    const float dt = 0.028f;
    // (Root frames only: a frame under a parent would place the body and its twin a bit apart. The
    // model's frames lend their boxes and world places.)
    std::vector<I3D_frame *> stepFrames;
    for (I3D_frame *mf : modelFrames) {
        const I3D_bbox b = RbBox(mf);
        I3D_frame *rf = RbBoxFrame(nullptr, "rs.model", b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z, S_vector(0, 0, 0));
        rf->m_localMatrix = mf->GetMatrix();
        stepFrames.push_back(rf);
    }
    for (size_t fi = modelFrames.size(); fi < bodyFrames.size(); ++fi)
        stepFrames.push_back(bodyFrames[fi]);
    for (size_t fi = 0; fi < stepFrames.size(); ++fi) {
        I3D_frame *f = stepFrames[fi];
        const S_matrix place = f->m_localMatrix;
        C_RigidBody body;
        setup(body, f, rnd(2.0f, 30.0f));
        // Falling through empty space: all the points tested by rays (every other step only the leading
        // ones), from the old centre, 0.3 m past each.
        g_rbFaces.clear();
        body.m_velocity = S_vector(rnd(-2, 2), rnd(-3, -1), rnd(-2, 2));
        body.m_spin = S_vector(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
        body.m_testTrailing = false;
        for (int s = 0; s < 3; ++s) {
            const auto res = stepBoth(body, false, dt);
            RS_CHECK(r.boxRays, res.first && (s != 0 || res.second.size() == 14));
        }
        // Slow: every point tested, the spin damped; unmoved points use the planes they met.
        body.m_velocity = S_vector(0.1f, -0.2f, 0.05f);
        g_rbFaces.push_back(RbFace(S_vector(0, 1, 0), S_vector(0, XYZ4(body.m_pos).y - 0.9f, 0), &ground));
        for (int s = 0; s < 3; ++s)
            RS_CHECK(r.box, stepBoth(body, false, dt).first);
        // Down onto the ground: the contacts, the impulses, the way out; then resting on it (the
        // points that moved less than the skin keep their planes).
        float floorY = 1e9f;
        for (int i = 0; i < 14; ++i)
            floorY = std::min(floorY, (XYZ4(body.m_points[i]) * f->GetMatrix()).y - 0.05f);
        g_rbFaces.clear();
        g_rbFaces.push_back(RbFace(S_vector(0, 1, 0), S_vector(0, floorY, 0), &ground));
        body.m_velocity = S_vector(rnd(-1, 1), -4.0f, rnd(-1, 1));
        body.m_spin = S_vector(rnd(-2, 2), rnd(-2, 2), rnd(-2, 2));
        bool touched = false;
        // (Every other step by rays: a flat box would pass the floor between them, the squared move
        // staying under the skin.)
        for (int s = 0; s < 40; ++s) {
            body.m_contactsChanged = s % 2 == 0;
            RS_CHECK(r.boxContact, stepBoth(body, false, dt).first);
            touched |= body.m_hardestHit > 0;
            body.m_hardestHit = 0;
        }
        RS_CHECK(r.boxContact, touched);
        // A slope and a wall; contacts changed (every point by ray).
        g_rbFaces.push_back(RbFace(S_vector(-0.6f, 0.8f, 0), S_vector(XYZ4(body.m_pos).x, floorY + 0.1f, 0), &slope));
        g_rbFaces.push_back(RbFace(S_vector(0, 0, -1), S_vector(0, 0, XYZ4(body.m_pos).z + 0.3f), &deep));
        body.m_velocity = S_vector(0.5f, -1.0f, 2.0f);
        body.m_contactsChanged = true;
        for (int s = 0; s < 12; ++s)
            RS_CHECK(r.boxContact, stepBoth(body, false, dt).first);
        // Water over the ground: g 9, a splash, the second ray past the water; slowed in it.
        g_rbFaces.clear();
        g_rbFaces.push_back(RbFace(S_vector(0, 1, 0), S_vector(0, XYZ4(body.m_pos).y - 0.6f, 0), &waterSurface));
        g_rbFaces.push_back(RbFace(S_vector(0, 1, 0), S_vector(0, XYZ4(body.m_pos).y - 2.0f, 0), &deep));
        body.m_velocity = S_vector(2.0f, -12.0f, 0.5f);    // (fast enough to be slowed by dt x 200 in it)
        body.m_g = 10.0f;
        body.m_contactsChanged = true;
        bool splashed = false;
        for (int s = 0; s < 30; ++s) {
            const auto res = stepBoth(body, false, dt);
            RS_CHECK(r.water, res.first);
            splashed |= body.m_g == 9.0f;
        }
        const float speed = body.m_velocity.Magnitude();
        RS_CHECK(r.water, splashed && speed <= 0.5001f);
        // Actors' volumes: the records (appended, their time renewed, grown past the capacity).
        body.m_g = 10.0f;
        g_rbFaces.clear();
        C_Human crowd[6];
        S_CollisionVolume crowdVolumes[6];
        for (int a = 0; a < 6; ++a) {
            static_cast<C_Actor *>(&crowd[a])->m_position = {static_cast<float>(a), 0, 0, 1};
            crowdVolumes[a].flags = 0x05000000 | COLLISION_FLAG_DYNAMIC;
            crowdVolumes[a].actor = static_cast<C_Actor *>(&crowd[a]);
        }
        // (A face per actor, each just under one point's reach: the nearest wins per ray.)
        const float baseY = XYZ4(body.m_pos).y;
        for (int a = 0; a < 6; ++a)
        {
            S_vector n(rnd(-0.2f, 0.2f), 1, rnd(-0.2f, 0.2f));
            n = n * (1.0f / n.Magnitude());
            g_rbFaces.push_back(RbFace(n, S_vector(rnd(-0.3f, 0.3f), baseY - 0.2f - 0.03f * a, rnd(-0.3f, 0.3f)), &crowdVolumes[a]));
        }
        g_rbFaces.push_back(RbFace(S_vector(0, 1, 0), S_vector(0, baseY - 0.25f, 0), &carVolume));
        body.m_velocity = S_vector(0.3f, -2.0f, -0.2f);
        body.m_contactsChanged = true;
        for (int s = 0; s < 20; ++s) {
            RS_CHECK(r.actors, stepBoth(body, false, dt).first);
            body.m_contactsChanged = s % 3 == 0;
        }
        RS_CHECK(r.actors, body.m_actorContactCount > 0);
        // The same place without actors: the points' actors cleared.
        g_rbFaces.clear();
        g_rbFaces.push_back(RbFace(S_vector(0, 1, 0), S_vector(0, baseY - 0.25f, 0), &ground));
        for (int s = 0; s < 4; ++s) {
            body.m_contactsChanged = true;
            RS_CHECK(r.actors, stepBoth(body, false, dt).first);
        }
        body.Stop();
        f->m_localMatrix = place;
    }

    // --- StepWheel against the reference: on flat ground, a slope, into a wall, stopping on the flat
    // (and not on a steep slope), over ground the filter refuses, from high up (the rays miss at first:
    // the contact point kept).
    for (int w = 0; w < 7; ++w) {
        I3D_frame *f = RbBoxFrame(nullptr, "rs.wheel", -0.15f, 0.15f, -0.4f, 0.4f, -0.4f, 0.4f,
                                  S_vector(rnd(-1, 1), w == 6 ? 3.0f : 0.45f + rnd(0.0f, 0.3f), rnd(-1, 1)));
        RbTilt(f, std::cos(0.3f * w), std::sin(0.3f * w), w % 2 == 1);
        C_RigidBody wheel;
        wheel.SetFrame(f);
        S_WheelDebrisParams p;
        p.SetVelocity(S_vector(rnd(-3, 3), rnd(-1, 1), rnd(-3, 3)));
        p.radius = 0.4f;
        p.width = 0.3f;
        p.weight = rnd(5, 30);
        p.unknown1C = rnd(0.3f, 1.0f);
        p.unknown20 = rnd(0.01f, 0.2f);
        p.moveCollision = true;
        wheel.SetupWheel(p);
        wheel.Start();
        wheel.m_spin = S_vector(rnd(-5, 5), rnd(-5, 5), rnd(-5, 5));
        g_rbFaces.clear();
        g_rbFaces.push_back(RbFace(S_vector(0, 1, 0), S_vector(0, 0, 0), &ground));
        if (w == 1)
            g_rbFaces.back() = RbFace(S_vector(0.3f, 1.0f, 0.1f) * (1.0f / std::sqrt(1.1f)), S_vector(0, 0, 0), &slope);
        if (w == 2)
            g_rbFaces.push_back(RbFace(S_vector(-1, 0, 0), S_vector(XYZ4(wheel.m_pos).x + 0.6f, 0, 0), &deep));
        if (w == 4)
            g_rbFaces.back() = RbFace(S_vector(0.6f, 0.8f, 0.0f), XYZ4(wheel.m_pos) - S_vector(0, 0.6f, 0), &slope);
        if (w == 5)
            g_rbFaces.back().volume = &cold;
        if (w == 6)
            wheel.m_contactPoint = {1, 2, 3, 4};
        wheel.m_stopOnFlat = w == 3 || w == 4;
        bool grounded = false;
        for (int s = 0; s < 60 && wheel.m_state != RIGID_BODY_AT_REST; ++s) {
            RS_CHECK(r.wheel, stepBoth(wheel, true, dt).first);
            grounded |= wheel.m_springForce > 0.0f;
        }
        if (std::getenv("CAR_TRACE"))
            std::printf("  wheel %d: state %d grounded %d pos %g %g %g v %g %g %g\n", w, wheel.m_state, grounded, wheel.m_pos.x,
                        wheel.m_pos.y, wheel.m_pos.z, wheel.m_velocity.x, wheel.m_velocity.y, wheel.m_velocity.z);
        RS_CHECK(r.wheel, w == 3   ? wheel.m_state == RIGID_BODY_AT_REST && RbSame(wheel.m_velocity, S_vector(0, 0, 0))
                          : w == 5 ? !grounded
                                   : grounded);
        // Over the ground by at least the half width less the contact range (a wheel lying flat).
        const S_RbFace &floor = g_rbFaces[0];
        RS_CHECK(r.wheel, w == 5 || floor.n.Dot(XYZ4(wheel.m_pos)) + floor.d > 0.11f);
        wheel.Stop();
    }

    // --- Drops: a box let fall onto the ground comes to rest on it (Step and Tick, the real way).
    {
        I3D_frame *f = RbBoxFrame(nullptr, "rs.drop", -0.3f, 0.3f, -0.2f, 0.2f, -0.25f, 0.25f, S_vector(0, 1.5f, 0));
        C_RigidBody b;
        setup(b, f, 8.0f);
        g_rbFaces.clear();
        g_rbFaces.push_back(RbFace(S_vector(0, 1, 0), S_vector(0, 0, 0), &ground));
        b.m_velocity = S_vector(0.4f, 0.0f, -0.3f);
        b.m_spin = S_vector(1.0f, 0.5f, -0.7f);
        for (int t = 0; t < 400 && b.m_state != RIGID_BODY_AT_REST; ++t)
            b.Tick(20);
        float lowest = 1e9f;
        for (int i = 0; i < 8; ++i)
            lowest = std::min(lowest, (XYZ4(b.m_points[i]) * f->GetMatrix()).y);
        RS_CHECK(r.drop, b.m_state == RIGID_BODY_AT_REST && lowest > -0.05f && lowest < 0.05f &&
                             XYZ4(b.m_pos).y < 0.4f && std::isfinite(XYZ4(b.m_pos).x));
        b.Stop();
    }

    g_rayTestHook = nullptr;
    g_frameVtable[0x08 / 4] = oldSetPos;
    g_countUpdates = false;
    std::printf("rigid step: filter %d collide %d integrate %d box %d boxRays %d boxContact %d water %d actors %d wheel %d "
                "drop %d\n",
                r.filter, r.collide, r.integrate, r.box, r.boxRays, r.boxContact, r.water, r.actors, r.wheel, r.drop);
}
