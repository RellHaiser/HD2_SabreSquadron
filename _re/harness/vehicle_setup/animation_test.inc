// Part 6b's test (the actors build: build_item.bat also links src/game/animation_actor.cpp; main runs
// AnimationTest after ItemTest on every model, which leaves the model loaded): C_AnimationActor against
// oracles written from the original's code, its engine calls logged in order through item_test.inc's
// log (frames, scene) and the harness's records (volumes, sounds, particles, messages, deaths). The
// model's box frames give the collision boxes' shapes. check_item.py reads the result line.

#include "game/animation_actor.h"

// --- the engine: a model's animation, its callbacks and names, the primitives, the sphere test ---------

struct S_AnFakeSet {                                    // an I3D_animation_set (fake COM)
    void **vtable;
    int refCount = 1;
    int playMode = 0;                                   // +0x08
    const char *fileName = nullptr;
    int endTime = 0;
};
int __stdcall F_AnEndTime(S_AnFakeSet *self)
{
    return self->endTime;
}
void *g_anSetVtable[8] = {nullptr, nullptr, reinterpret_cast<void *>(&F_AnEndTime)};
std::map<const I3D_frame *, S_AnFakeSet *> g_anSets;
std::map<const I3D_frame *, int> g_anTimes;
I3D_animation_set *__stdcall F_AnGetAnimation(I3D_frame *self, int stage)
{
    auto it = g_anSets.find(self);
    return stage == 0 && it != g_anSets.end() ? reinterpret_cast<I3D_animation_set *>(it->second) : nullptr;
}
int __stdcall F_AnGetAnimTime(I3D_frame *self, int stage)
{
    return stage == 0 ? g_anTimes[self] : -999;
}
void __stdcall F_AnTick(I3D_frame *self, int time)
{
    ItLog("tick %s %d", ItName(self).c_str(), time);
    g_anTimes[self] += time;
}
std::map<const I3D_frame *, I3D_CALLBACK *> g_anCallbacks;
void __stdcall F_AnSetCallback(I3D_frame *self, I3D_CALLBACK *callback, uint32_t messages)
{
    ItLog("callback %s %x", ItName(self).c_str(), messages);
    g_anCallbacks[self] = callback;
}
void __stdcall F_AnSetName(I3D_frame *self, const char *name)
{
    ItLog("name %s", name);
    g_itNames[self] = name;
}
int __stdcall F_AnNumChildren(I3D_frame *self)
{
    int n = 0;
    for (I3D_frame *c = self->m_firstChild; c; c = c->m_nextSibling)
        ++n;
    return n;
}

// The collision frame's primitives (their flags).
std::vector<uint32_t> g_anPrims;
const I3D_frame *g_anPrimFrame;
int C_CollisionWorld::GetFramePrimitives(const I3D_frame *frame, std::vector<void *> &primitives) const
{
    ItLog("prims %s", ItName(frame).c_str());
    primitives.clear();
    if (frame == g_anPrimFrame)
        for (uint32_t &flags : g_anPrims)
            primitives.push_back(&flags);
    return static_cast<int>(primitives.size());
}

// The wings' sphere test: each call, the volume it reports.
struct S_AnSweep {
    S_vector from, dir;
    float radius;
    uint32_t types, flags;
};
std::vector<S_AnSweep> g_anSweeps;
S_CollisionVolume *g_anSweepHit;
S_CollisionVolume *AnSphereTest(const S_vector &from, const S_vector &dir, float radius, uint32_t types, uint32_t flags)
{
    g_anSweeps.push_back({from, dir, radius, types, flags});
    return g_anSweepHit;
}

// An actor of any kind that records its hits.
class C_AnTarget : public C_Actor {
public:
    explicit C_AnTarget(E_ActorType type) : C_Actor(type) {}
    void CopyFrom(const C_Actor *) override {}
    void StartMission(bool) override {}
    void StopMission(bool) override {}
    void RestartMission(bool) override {}
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker, int flags) override
    {
        hits.push_back({type, amount, pos, dir, attacker, flags});
        g_hitOrder.push_back(this);
    }
    std::vector<C_Human::S_Hit> hits;
};

// The model cache's copy: a "part" and a "BODYCOLB00" box under it (the wreck's shape), named "copy",
// with an animation set of g_anEndTime ms.
int g_anEndTime = 1000;
bool g_anCopyAnimated = true;
std::vector<S_AnFakeSet *> g_anSetsMade;
void AnFillCopy(I3D_model *model)
{
    auto *m = reinterpret_cast<I3D_frame *>(model);
    g_itNames[m] = "copy";
    I3D_frame *part = NewFrame(FRAME_DUMMY, "part", m);
    part->m_name = Stub(part).name.c_str();
    I3D_frame *box = RbBoxFrame(m, "BODYCOLB00", 1, 1, 0, 1, -1, 3, S_vector(0, 0.5f, 0));   // (flat in x)
    box->m_name = Stub(box).name.c_str();
    box->m_userProps = "matix7";                       // (not a material)
    if (g_anCopyAnimated) {
        auto *set = new S_AnFakeSet();
        set->vtable = g_anSetVtable;
        set->endTime = g_anEndTime;
        g_anSetsMade.push_back(set);
        g_anSets[m] = set;
    }
    g_anTimes[m] = 0;
}

// --- the oracles ----------------------------------------------------------------------------------------

// A collision box as the original's 0x005b48b0 makes it.
struct S_AnBox {
    S_matrix inv, mat;
    S_vector center;
    I3D_bbox bbox;
    float radius;
    uint32_t flags;
};
S_AnBox AnBoxOracle(I3D_frame *f, uint32_t flags, bool add)
{
    S_AnBox e;
    const S_vector s = f->m_scale;
    I3D_bbox box;
    S_vector c(0, 0, 0);
    float half = 0.0f;
    if (f->GetType() == FRAME_DUMMY) {
        box = *reinterpret_cast<const I3D_bbox *>(reinterpret_cast<const uint8_t *>(f) + 0x120);
        c = S_vector((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f);
    } else {
        I3D_bbox tmp;
        box = *F_GetHierarchyBBox(nullptr, tmp, f);
        if (box.min.x <= box.max.x) {
            const S_vector h((box.max.x - box.min.x) * 0.5f, (box.max.y - box.min.y) * 0.5f, (box.max.z - box.min.z) * 0.5f);
            c = S_vector(box.min.x + h.x, box.min.y + h.y, box.min.z + h.z);
            half = std::sqrt((h.z * h.z + h.y * h.y) + h.x * h.x);
        }
    }
    e.flags = flags;
    const char *props = f->GetUserProps();
    if (add && std::strncmp(props, "matid", 5) == 0 && std::strlen(props) > 5)
        e.flags |= static_cast<uint32_t>(std::atoi(props + 5)) << 24;
    box.min.x *= s.x;
    box.min.y *= s.y;
    box.min.z *= s.z;
    box.max.x *= s.x;
    box.max.y *= s.y;
    box.max.z *= s.z;
    e.center = c * f->GetMatrix();
    e.bbox = box;
    e.mat = f->GetMatrix();
    const float k[3] = {1.0f / s.x, 1.0f / s.y, 1.0f / s.z};
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            e.mat.m[r][col] = k[r] * e.mat.m[r][col];
    e.inv.Inverse(e.mat);
    if (f->GetType() == FRAME_DUMMY) {
        const float dx = box.max.x - box.min.x, dy = box.max.y - box.min.y, dz = box.max.z - box.min.z;
        const float sq = (dz * dz + dy * dy) + dx * dx;
        float length = std::sqrt(sq);
        if (static_cast<int32_t>(ItF(sq)) < static_cast<int32_t>(ItF(1e-8f)))
            length = 0.0f;
        else if (std::fabs(sq - 1.0f) < 1e-8f)
            length = 1.0f;
        e.radius = length * 0.5f;
    } else {
        const S_vector w = f->m_parent ? f->GetMatrix().GetScale() : f->m_scale;
        float largest = w.x;
        if (w.y > largest || w.z > largest)
            largest = w.y > w.z ? w.y : w.z;
        e.radius = largest * half;
    }
    return e;
}
bool AnSameBox(const C_AnimationActor::S_Collision &c, const S_AnBox &e)
{
    return std::memcmp(&c.matrix, &e.mat, sizeof e.mat) == 0 && std::memcmp(&c.invMatrix, &e.inv, sizeof e.inv) == 0 &&
           std::memcmp(&c.volume.center, &e.center, 12) == 0 && std::memcmp(&c.volume.bbox.min, &e.bbox.min, 12) == 0 &&
           std::memcmp(&c.volume.bbox.max, &e.bbox.max, 12) == 0 && c.volume.radius == e.radius &&
           c.volume.flags == e.flags && c.volume.matrix == &c.matrix && c.volume.invMatrix == &c.invMatrix;
}

// --- the test ---------------------------------------------------------------------------------------------

struct S_An {
    bool creator = true, frame = true, props = true, copy = true, start = true, collisions = true, step = true,
         stop = true, hit = true, messages = true, save = true, load = true, mission = true, notes = true,
         armour = true, wings = true, restart = true;
};
#define AN(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

I3D_frame *AnFrame(E_I3D_FRAME_TYPE type, const std::string &name, I3D_frame *parent)
{
    I3D_frame *f = NewFrame(type, name, parent);
    f->m_name = Stub(f).name.c_str();
    return f;
}
void AnSetBBox(I3D_frame *dummy, const S_vector &min, const S_vector &max)
{
    I3D_bbox &b = *reinterpret_cast<I3D_bbox *>(reinterpret_cast<uint8_t *>(dummy) + 0x120);
    b.min = min;
    b.max = max;
}
std::string AnString(const C_str &s)
{
    return static_cast<const char *>(s);
}

void AnimationTest()
{
    void *const oldFrame[] = {g_frameVtable[0x00 / 4], g_frameVtable[0x20 / 4], g_frameVtable[0x24 / 4],
                              g_frameVtable[0x28 / 4], g_frameVtable[0x30 / 4], g_frameVtable[0x34 / 4],
                              g_frameVtable[0x38 / 4], g_frameVtable[0x3C / 4], g_frameVtable[0x58 / 4],
                              g_frameVtable[0x88 / 4], g_frameVtable[0xA0 / 4]};
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x20 / 4] = reinterpret_cast<void *>(&F_AnTick);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x28 / 4] = reinterpret_cast<void *>(&F_AnSetCallback);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x34 / 4] = reinterpret_cast<void *>(&F_AnSetName);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_RbUpdateBound);
    g_frameVtable[0x88 / 4] = reinterpret_cast<void *>(&F_AnGetAnimation);
    g_frameVtable[0xA0 / 4] = reinterpret_cast<void *>(&F_AnGetAnimTime);
    void *const oldFind = g_sceneVtable[0x7C / 4];
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_ItFindFrame);
    GetGame()->m_scene = g_carScene;
    g_sphereTestHook = &AnSphereTest;
    g_reliableActorStreams = true;
    g_cacheFill = &AnFillCopy;
    S_MaterialDef *const oldMaterials = g_materials;
    static S_MaterialDef materials[16];                 // (the default material, 11, included)
    for (S_MaterialDef &m : materials)
        m.penetrationCost = 100;
    materials[1].penetrationCost = 506;
    materials[2].penetrationCost = 1501;
    materials[3].penetrationCost = 505;
    g_materials = materials;
    const uint8_t oldRole = g_gameRole;
    g_gameRole = GAME_ROLE_SERVER;
    S_An r;
    C_ActorCreator *creator = g_actorCreators[ACTOR_ANIMATION];

    // The model's boxes as the collision boxes' shapes.
    std::vector<I3D_bbox> shapes;
    for (S_StubFrame *s : g_frames) {
        const I3D_bbox b = RbBox(s->frame);
        if (shapes.size() < 3 && b.min.x <= b.max.x && b.max.x - b.min.x > 0.05f && b.max.y - b.min.y > 0.05f &&
            b.max.z - b.min.z > 0.05f)
            shapes.push_back(b);
    }
    while (shapes.size() < 3) {
        I3D_bbox b;
        b.min = S_vector(-0.5f, -0.2f, -1.0f - shapes.size());
        b.max = S_vector(0.7f, 0.3f, 1.0f + shapes.size());
        shapes.push_back(b);
    }
    int seed = static_cast<int>(g_frames.size()) * 31 + 7;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    int sceneNo = 0;
    // The actor's frame: a model under a parent (placed, turned, scaled), its "particle", the boxes
    // "BODYCOLB00".. of the shapes (BODYCOLB01 a scaled dummy), a gap ("BODYCOLB03" not found), a
    // "matid2" property on the first.
    struct S_AnScene {
        I3D_frame *parent, *frame, *particle;
        std::vector<I3D_frame *> boxes;
        std::string name;
    };
    const auto scene = [&](bool boxes = true) {
        S_AnScene s;
        s.name = "an.f" + std::to_string(sceneNo++);
        s.parent = AnFrame(FRAME_DUMMY, s.name + "p", nullptr);
        s.parent->m_localMatrix.m[3][0] = rnd(-20, 20);
        s.parent->m_localMatrix.m[3][2] = rnd(-20, 20);
        s.frame = AnFrame(FRAME_MODEL, s.name, s.parent);
        RbTilt(s.frame, 0.8f, 0.6f);
        s.frame->m_localMatrix.m[3][0] = rnd(-5, 5);
        s.frame->m_localMatrix.m[3][1] = rnd(0, 3);
        s.frame->m_localMatrix.m[3][2] = rnd(-5, 5);
        s.frame->m_scale = S_vector(1.25f, 0.75f, 1.5f);
        s.particle = AnFrame(FRAME_DUMMY, "particle", s.frame);
        s.particle->m_localMatrix.m[3][1] = 2.0f;
        if (boxes) {
            const I3D_bbox &b0 = shapes[0];
            I3D_frame *box0 = RbBoxFrame(s.frame, "BODYCOLB00", b0.min.x, b0.max.x, b0.min.y, b0.max.y, b0.min.z, b0.max.z,
                                         S_vector(rnd(-1, 1), 0, rnd(-1, 1)));
            box0->m_name = Stub(box0).name.c_str();
            box0->m_userProps = "matid2";
            box0->m_scale = S_vector(rnd(0.5f, 2), rnd(0.5f, 2), rnd(0.5f, 2));
            RbTilt(box0, 0.6f, 0.8f, true);
            s.boxes.push_back(box0);
            I3D_frame *box1 = AnFrame(FRAME_DUMMY, "BODYCOLB01", s.frame);
            AnSetBBox(box1, shapes[1].min, shapes[1].max);
            box1->m_userProps = "matidq";               // (no number: material 0)
            box1->m_scale = S_vector(1.5f, 0.5f, 2.0f);
            box1->m_localMatrix.m[3][1] = 1.0f;
            s.boxes.push_back(box1);
            const I3D_bbox &b2 = shapes[2];
            I3D_frame *box3 = RbBoxFrame(s.frame, "BODYCOLB03", b2.min.x, b2.max.x, b2.min.y, b2.max.y, b2.min.z, b2.max.z,
                                         S_vector(0, 0, 0));
            box3->m_name = Stub(box3).name.c_str();
        }
        return s;
    };
    // A new actor on a new scene, its properties set.
    const auto actor = [&](S_AnScene &s) {
        auto *a = static_cast<C_AnimationActor *>(creator->Create());
        a->SetFrame(s.frame);
        a->m_netId = 60;
        a->m_animName = "Anims\\Plane.v2.5ds";
        a->m_nameTextId = 0x1bf0;
        a->m_hitPoints = 1000.0f;
        a->m_explosionSound = 7;
        a->m_particleId = 12;
        a->m_collisionFrameName = "collframe";
        return a;
    };
    const auto done = [&](C_AnimationActor *a) {
        g_itLog.clear();
        creator->Destroy(a);
    };
    S_FrameData scriptData = {};
    scriptData.script = reinterpret_cast<C_Script *>(&scriptData);

    // --- The creator and the defaults.
    {
        AN(r.creator, creator && std::strcmp(creator->GetName(), "Animation") == 0);
        C_Actor *base = creator->Create();
        auto *a = dynamic_cast<C_AnimationActor *>(base);
        C_chunk none;
        AN(r.creator, a && base->GetType() == ACTOR_ANIMATION && a->m_explosionSound == -1 && a->m_particleId == -1 &&
                          a->m_nameTextId == -1 && a->GetArmour() == ANIMATION_ARMOUR_NONE && !a->ShowsNoInfo() &&
                          a->m_hitPoints == 0.0f && a->m_damage == 0.0f && !a->m_permanentRefresh && !a->IsStopped() &&
                          !a->m_animating && a->m_model == nullptr && AnString(a->m_animName).empty() &&
                          AnString(a->m_collisionFrameName).empty() && a->m_collisions.empty() && a->m_wings.empty() &&
                          base->WriteProperties(none) && base->GetHitPoints() == 100);
        I3D_frame *part = AnFrame(FRAME_VISUAL, "an.part", nullptr);
        g_frameMaterials[part] = 5;
        I3D_frame *part0 = AnFrame(FRAME_VISUAL, "an.part0", nullptr);
        g_frameMaterials[part0] = 0;
        I3D_frame *bare = AnFrame(FRAME_VISUAL, "an.bare", nullptr);
        AN(r.creator, base->GetMaterialId(part) == 5 && base->GetMaterialId(part0) == 0 && base->GetMaterialId(bare) == 11);
        g_frameMaterials.erase(part);
        g_frameMaterials.erase(part0);
        creator->Destroy(base);
        creator->Destroy(nullptr);
    }

    // --- SetFrame: a model frame only (the base keeps any frame).
    {
        auto *a = static_cast<C_AnimationActor *>(creator->Create());
        I3D_frame *model = AnFrame(FRAME_MODEL, "an.model", nullptr);
        I3D_frame *visual = AnFrame(FRAME_VISUAL, "an.visual", nullptr);
        const int refs = model->m_refCount;
        AN(r.frame, a->SetFrame(model) && a->m_frame == model && model->m_refCount == refs + 1);
        auto *b = static_cast<C_AnimationActor *>(creator->Create());
        AN(r.frame, !b->SetFrame(visual) && b->m_frame == visual && !b->SetFrame(nullptr));
        done(a);
        done(b);
    }

    // --- ReadProperties: the eight chunks, a hit points floor of -1, an empty refresh chunk, the
    // strings' 999 characters, a chunk of another id entered; an empty chunk refused.
    {
        std::vector<uint8_t> &x = g_files["an.props"];
        x.clear();
        const auto add = [&](const void *p, size_t n) {
            const auto *b = static_cast<const uint8_t *>(p);
            x.insert(x.end(), b, b + n);
        };
        const auto head = [&](uint16_t id, uint32_t size) {
            add(&id, 2);
            add(&size, 4);
        };
        const auto text = [&](uint16_t id, const std::string &s) {
            head(id, static_cast<uint32_t>(6 + s.size() + 1));
            add(s.c_str(), s.size() + 1);
        };
        const auto i32 = [&](uint16_t id, int v) {
            head(id, 10);
            add(&v, 4);
        };
        const auto f32 = [&](uint16_t id, float v) {
            head(id, 10);
            add(&v, 4);
        };
        const auto u8 = [&](uint16_t id, uint8_t v) {
            head(id, 7);
            add(&v, 1);
        };
        const size_t start = x.size();
        head(0x1234, 0);                                // (size patched below)
        text(0, "Anims\\Plane.5ds");
        f32(1, -5.0f);
        u8(2, 1);
        i32(3, 7);
        i32(4, 12);
        text(5, "collframe");
        i32(6, 0x1bf0);
        u8(7, 3);
        // Another id holding a sound id: read as the actor's.
        head(9, 6 + 10);
        i32(3, 44);
        const uint32_t total = static_cast<uint32_t>(x.size() - start);
        std::memcpy(&x[start + 2], &total, 4);
        auto *a = static_cast<C_AnimationActor *>(creator->Create());
        C_chunk c;
        c.Open("an.props", false);
        uint16_t id;
        c.Descend(id);
        AN(r.props, a->ReadProperties(c) && AnString(a->m_animName) == "Anims\\Plane.5ds" && a->m_hitPoints == -1.0f &&
                        a->m_permanentRefresh && a->m_explosionSound == 44 && a->m_particleId == 12 &&
                        AnString(a->m_collisionFrameName) == "collframe" && a->m_nameTextId == 0x1bf0 && a->ShowsNoInfo());
        c.Close();
        // Hit points -1 and over, an empty refresh chunk, a long name, a 0 flag.
        x.clear();
        head(0x1234, 0);
        f32(1, rnd(10, 90));
        head(2, 6);
        text(0, std::string(1200, 'k'));
        u8(7, 0);
        f32(1, -0.75f);                                 // (over -1: kept)
        const float last = -0.75f;
        const uint32_t total2 = static_cast<uint32_t>(x.size());
        std::memcpy(&x[2], &total2, 4);
        a->m_permanentRefresh = true;
        C_chunk c2;
        c2.Open("an.props", false);
        c2.Descend(id);
        AN(r.props, a->ReadProperties(c2) && a->m_hitPoints == last && a->m_permanentRefresh &&
                        AnString(a->m_animName) == std::string(999, 'k') && !a->ShowsNoInfo());
        c2.Close();
        x.clear();
        head(0x1234, 6);
        C_chunk c3;
        c3.Open("an.props", false);
        c3.Descend(id);
        a->m_hitPoints = 3.0f;
        AN(r.props, !a->ReadProperties(c3) && a->m_hitPoints == 3.0f);
        c3.Close();
        done(a);
    }

    // --- CopyFrom: the properties but the name's text id.
    {
        auto *a = static_cast<C_AnimationActor *>(creator->Create());
        auto *b = static_cast<C_AnimationActor *>(creator->Create());
        a->m_animName = "x.5ds";
        a->m_collisionFrameName = "cf";
        a->m_permanentRefresh = true;
        a->m_showNoInfo = true;
        a->m_hitPoints = 77.0f;
        a->m_explosionSound = 3;
        a->m_particleId = 4;
        a->m_nameTextId = 5;
        const int copies = g_actorCopies;
        b->CopyFrom(a);
        AN(r.copy, AnString(b->m_animName) == "x.5ds" && AnString(b->m_collisionFrameName) == "cf" && b->m_permanentRefresh &&
                       b->m_showNoInfo && b->m_hitPoints == 77.0f && b->m_explosionSound == 3 && b->m_particleId == 4 &&
                       b->m_nameTextId == -1 && g_actorCopies == copies + 1);
        done(a);
        done(b);
    }

    // --- StartMission: the counters, the name, activated; a listen server's server side stops there;
    // the particle frame, the boxes, the copy (the name without its extension, through the model
    // cache): its callbacks, place, name, hidden; the armour; the wings of "#mchescape".
    {
        struct S_Case {
            uint8_t role;
            bool server, full;
        };
        const S_Case cases[] = {{GAME_ROLE_SERVER, true, true}, {GAME_ROLE_CLIENT, false, true}, {3, true, false},
                                {3, false, true}, {GAME_ROLE_LOCAL, true, false}, {GAME_ROLE_LOCAL, false, true}};
        for (const S_Case &k : cases) {
            g_gameRole = k.role;
            S_AnScene s = scene();
            C_AnimationActor *a = actor(s);
            a->m_animating = a->m_destroy = a->m_animationHeld = true;
            a->m_damage = 5.0f;
            a->m_refreshes = 9;
            a->m_collisionFrame = s.parent;
            g_activations.clear();
            g_itLog.clear();
            g_volumeLog.clear();
            const size_t loads = g_cacheLoads.size();
            a->StartMission(k.server);
            AN(r.start, !a->m_animating && !a->m_destroy && !a->m_animationHeld && a->m_damage == 0.0f &&
                            a->m_refreshes == 0 && AnString(*a->GetName()) == "Car (text 0x1bf0)" &&
                            g_activations == std::vector<bool>{k.server});
            if (!k.full) {
                AN(r.start, a->m_model == nullptr && a->m_collisions.empty() && a->m_collisionFrame == s.parent &&
                                g_cacheLoads.size() == loads && g_itLog.empty());
                done(a);
                continue;
            }
            I3D_frame *copy = reinterpret_cast<I3D_frame *>(a->m_model);
            if (!copy) {
                AN(r.start, false);
                done(a);
                continue;
            }
            AN(r.start, a->m_collisionFrame == nullptr && a->m_particleFrame == s.particle &&
                            copy == g_createdModels.back() && g_cacheLoads.size() == loads + 1 &&
                            g_cacheLoads.back() == "Anims\\Plane.v2");
            // The boxes: BODYCOLB00 and 01 (03 after the gap is not reached), each added and moved.
            AN(r.collisions, a->m_collisions.size() == 2 && a->m_collisions[0]->volume.frame == s.boxes[0] &&
                                 a->m_collisions[1]->volume.frame == s.boxes[1] &&
                                 a->m_collisions[0]->volume.actor == a && a->m_collisions[1]->volume.type == 0x400000);
            for (size_t i = 0; i < a->m_collisions.size(); ++i)
                AN(r.collisions, AnSameBox(*a->m_collisions[i], AnBoxOracle(s.boxes[i], COLLISION_SHAPE_BOX, true)));
            AN(r.collisions, g_volumeLog.size() == 4 && g_volumeLog[0].op == 'A' && g_volumeLog[1].op == 'M' &&
                                 g_volumeLog[0].volume == &a->m_collisions[0]->volume && g_volumeLog[2].op == 'A' &&
                                 (a->m_collisions[0]->volume.flags >> 24) == 2);
            // The copy, in the frame's place under its parent, named, hidden; callbacks on it and its
            // children (the last first).
            const std::string n = s.name + "#aanim";
            AN(r.start, ItLogIs({"update BODYCOLB00", "update BODYCOLB01", "callback copy 1000", "callback BODYCOLB00 1000",
                                 "callback part 1000", "link copy " + s.name + "p 0", "name " + n, "on " + n + " 0",
                                 "update " + n}));
            S_quat rot = s.frame->m_rot;
            rot.Normalize();
            AN(r.start, copy->m_parent == s.parent &&
                            std::memcmp(&copy->m_localMatrix.m[3][0], &s.frame->m_localMatrix.m[3][0], 12) == 0 &&
                            std::memcmp(&copy->m_rot, &rot, 16) == 0 && std::memcmp(&copy->m_scale, &s.frame->m_scale, 12) == 0 &&
                            !(copy->m_frameFlags & 1u) && g_anCallbacks[copy] == &C_AnimationActor::NoteProc &&
                            g_anCallbacks[copy->m_firstChild] == &C_AnimationActor::NoteProc && a->m_wings.empty());
            // The armour from the first hard material (none here: BODYCOLB00's matid is a property).
            AN(r.armour, a->GetArmour() == ANIMATION_ARMOUR_NONE);
            done(a);
        }
        g_gameRole = GAME_ROLE_SERVER;
        // Without the text id: an empty name; a load that fails: the copy let go.
        {
            S_AnScene s = scene();
            C_AnimationActor *a = actor(s);
            a->m_nameTextId = -1;
            a->m_animName = "broken";
            g_cacheFailName = "broken";
            g_itLog.clear();
            a->StartMission(true);
            g_cacheFailName.clear();
            AN(r.start, AnString(*a->GetName()).empty() && a->m_model == nullptr && a->m_collisions.size() == 2 &&
                            g_itLog.size() == 3 && g_itLog[2] == "release ?" && g_createdModels.back()->m_refCount == 0);
            done(a);
        }
        // The armour: the first frame with a hard material (light then heavy: light wins).
        for (int k = 0; k < 3; ++k) {
            S_AnScene s = scene();
            g_frameMaterials[s.particle] = k == 0 ? 1 : k == 1 ? 2 : 3;
            g_frameMaterials[s.boxes[1]] = 2;
            C_AnimationActor *a = actor(s);
            a->StartMission(true);
            AN(r.armour, a->GetArmour() == (k == 0 ? ANIMATION_ARMOUR_LIGHT : ANIMATION_ARMOUR_HEAVY));
            g_frameMaterials.erase(s.particle);
            g_frameMaterials.erase(s.boxes[1]);
            done(a);
        }
        // The wings of "#mchescape" (any case): right, then left; another model none.
        for (int k = 0; k < 4; ++k) {
            S_AnScene s = scene(false);
            static const char *const files[] = {"#MCHescape", "#mchescape", "#mchescape2", "mchescape"};
            *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(s.frame) + 0x134) = files[k];
            I3D_frame *left = AnFrame(FRAME_VISUAL, "left_wing", s.frame);
            I3D_frame *right = k == 1 ? nullptr : AnFrame(FRAME_VISUAL, "right_wing", s.frame);
            const int refs = left->m_refCount;
            C_AnimationActor *a = actor(s);
            a->StartMission(true);
            if (k < 2) {
                AN(r.wings, a->m_wings.size() == (k == 0 ? 2u : 1u) && a->m_wings.back()->frame == left &&
                                (k == 1 || a->m_wings[0]->frame == right) && left->m_refCount == refs + 1);
                const I3D_bound_volume &bound = left->GetBound();
                AN(r.wings, a->m_wings.back()->lastPos.x == bound.center.x && a->m_wings.back()->lastPos.w == bound.radius);
            } else {
                AN(r.wings, a->m_wings.empty());
            }
            a->StopMission(true);
            AN(r.wings, a->m_wings.empty() && left->m_refCount == refs);
            done(a);
        }
    }

    // --- The boxes on their own: a point dummy, a unit one, a visual without geometry, a visual under a
    // scaled parent (its world scale), removed from the world when deleted.
    {
        I3D_frame *point = AnFrame(FRAME_DUMMY, "an.point", nullptr);
        AnSetBBox(point, S_vector(0.5f, 0.5f, 0.5f), S_vector(0.5f, 0.5f, 0.5f));
        I3D_frame *unit = AnFrame(FRAME_DUMMY, "an.unit", nullptr);
        AnSetBBox(unit, S_vector(0, 0, 0), S_vector(0, 1, 0));
        I3D_frame *empty = AnFrame(FRAME_VISUAL, "an.empty", nullptr);
        empty->m_localMatrix.m[3][0] = 4.0f;
        // (World scales with x, then y the largest.)
        I3D_frame *scaledX = AnFrame(FRAME_DUMMY, "an.scaledx", nullptr);
        I3D_frame *scaledY = AnFrame(FRAME_DUMMY, "an.scaledy", nullptr);
        const float diagonals[2][3] = {{2.5f, 1.5f, 1.0f}, {1.0f, 2.5f, 1.5f}};
        for (int i = 0; i < 3; ++i) {
            scaledX->m_localMatrix.m[i][i] = diagonals[0][i];
            scaledY->m_localMatrix.m[i][i] = diagonals[1][i];
        }
        const I3D_bbox &b = shapes[0];
        I3D_frame *child = RbBoxFrame(scaledX, "an.child", b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z,
                                      S_vector(1, 2, 3));
        child->m_scale = S_vector(0.5f, 2.0f, 1.0f);
        I3D_frame *child2 = RbBoxFrame(scaledY, "an.child2", b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z,
                                       S_vector(-1, 0, 2));
        for (I3D_frame *f : {point, unit, empty, child, child2}) {
            g_volumeLog.clear();
            auto *c = new C_AnimationActor::S_Collision(f, COLLISION_SHAPE_BOX, 0x400000, nullptr);
            AN(r.collisions, AnSameBox(*c, AnBoxOracle(f, COLLISION_SHAPE_BOX, true)) && g_volumeLog.size() == 2 &&
                                 g_volumeLog[0].op == 'A' && g_volumeLog[0].volume == &c->volume);
            if (f == point)
                AN(r.collisions, c->volume.radius == 0.0f);
            if (f == unit)
                AN(r.collisions, c->volume.radius == 0.5f);
            if (f == empty)
                AN(r.collisions, c->volume.radius == 0.0f && c->volume.center.x == 4.0f);
            delete c;
            AN(r.collisions, g_volumeLog.size() == 3 && g_volumeLog[2].op == 'R');
        }
    }

    // --- The step: a destroy request stops it; the animation runs (unless held) to its end (compared
    // unsigned), then the wreck's boxes; the boxes refresh while it stands with the permanent refresh, and
    // after a load; Tick only on a server without a client side.
    {
        S_AnScene s = scene();
        C_AnimationActor *a = actor(s);
        a->StartMission(true);
        I3D_frame *copy = reinterpret_cast<I3D_frame *>(a->m_model);
        const std::string cn = ItName(copy);
        // No refresh without the permanent refresh.
        g_volumeLog.clear();
        g_itLog.clear();
        a->Update(20);
        AN(r.step, g_volumeLog.empty() && g_itLog.empty());
        a->m_permanentRefresh = true;
        s.boxes[0]->m_localMatrix.m[3][0] += 1.0f;
        s.boxes[0]->m_userProps = "matid1";               // (a refresh keeps the first material)
        a->Update(20);
        AN(r.step, g_volumeLog.size() == 2 && g_volumeLog[0].volume == &a->m_collisions[1]->volume &&
                       g_volumeLog[1].op == 'M' && AnSameBox(*a->m_collisions[0], AnBoxOracle(s.boxes[0], COLLISION_SHAPE_BOX | (2u << 24), false)));
        // Animating or stopped: only with refreshes left (counted down).
        g_volumeLog.clear();
        a->m_animating = true;
        a->Update(20);
        AN(r.step, g_volumeLog.empty());
        a->m_animating = false;
        a->m_stopped = true;
        a->m_refreshes = 2;
        a->Update(20);
        a->Update(20);
        a->Update(20);
        AN(r.step, g_volumeLog.size() == 4 && a->m_refreshes == 0);
        a->m_stopped = false;
        // Tick: a server without a client side only.
        for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_SERVER, GAME_ROLE_CLIENT, 3, GAME_ROLE_LOCAL}) {
            g_gameRole = role;
            g_volumeLog.clear();
            a->Tick(100);
            AN(r.step, g_volumeLog.size() == (role == GAME_ROLE_SERVER ? 2u : 0u));
        }
        g_gameRole = GAME_ROLE_SERVER;
        a->m_permanentRefresh = false;
        // The animation: ticks until the end, the last one the rest less 1, then the wreck's boxes.
        g_anTimes[copy] = 900;
        a->m_animating = true;
        g_itLog.clear();
        a->Update(50);
        AN(r.step, ItLogIs({"tick " + cn + " 50"}) && g_anTimes[copy] == 950);
        g_itLog.clear();
        g_volumeLog.clear();
        a->Update(50);
        AN(r.step, ItLogIs({"tick " + cn + " 49", "update BODYCOLB00"}) && !a->m_animating && a->m_collisions.size() == 1 &&
                       a->m_collisions[0]->volume.frame == copy->m_firstChild->m_nextSibling && g_volumeLog.size() == 4 &&
                       AnSameBox(*a->m_collisions[0], AnBoxOracle(copy->m_firstChild->m_nextSibling, COLLISION_SHAPE_BOX, true)) &&
                       a->m_collisions[0]->volume.radius > 0.0f &&
                       g_volumeLog[0].op == 'R' && g_volumeLog[1].op == 'R');
        // Held: no tick; exactly at the end: finished.
        a->m_animating = true;
        a->HoldAnimation();
        g_anTimes[copy] = 100;
        g_itLog.clear();
        a->Update(50);
        AN(r.step, g_itLog.empty() && g_anTimes[copy] == 100 && a->m_animating);
        g_anTimes[copy] = 950;
        a->Update(50);
        AN(r.step, !a->m_animating && g_anTimes[copy] == 999);
        a->m_animationHeld = false;
        // A time below 0 is huge unsigned: finished at once.
        a->m_animating = true;
        g_anTimes[copy] = -500;
        g_itLog.clear();
        a->Update(50);
        AN(r.step, !a->m_animating && !g_itLog.empty() && g_itLog[0] == "tick " + cn + " 1499");
        // Without an animation set nothing runs.
        a->m_animating = true;
        S_AnFakeSet *set = g_anSets[copy];
        g_anSets.erase(copy);
        g_itLog.clear();
        a->Update(50);
        AN(r.step, a->m_animating && g_itLog.empty());
        g_anSets[copy] = set;
        // A destroy request: stopped first.
        a->m_animating = false;
        a->m_destroy = true;
        a->Update(10);
        AN(r.step, !a->m_destroy && a->IsStopped());
        done(a);
    }

    // --- Stop: the collision frame's primitives off, the boxes gone, the sound and particles (clients,
    // not at once, not loading), the copy shown playing, the frame hidden, the script told (server),
    // the clients told (multiplayer server); once only; without the copy not stopped.
    {
        struct S_Case {
            uint8_t role;
            bool immediately, found, sound, particle, death, message;
        };
        const S_Case cases[] = {{GAME_ROLE_SERVER, false, true, false, false, true, true},
                                {GAME_ROLE_CLIENT, false, true, true, true, false, false},
                                {3, false, false, true, true, true, true},
                                {GAME_ROLE_LOCAL, false, true, true, true, true, false},
                                {GAME_ROLE_LOCAL, true, true, false, false, true, false},
                                {3, true, true, false, false, true, true}};
        for (const S_Case &k : cases) {
            g_gameRole = k.role;
            S_AnScene s = scene();
            C_AnimationActor *a = actor(s);
            a->StartMission(k.role == GAME_ROLE_SERVER);
            I3D_frame *copy = reinterpret_cast<I3D_frame *>(a->m_model);
            I3D_frame *collisionFrame = AnFrame(FRAME_DUMMY, "collframe", nullptr);
            g_itFound = k.found ? collisionFrame : nullptr;
            g_anPrimFrame = collisionFrame;
            g_anPrims = {0x01000001, 0x02000020, 0};
            s.frame->SetData(&scriptData);
            g_itLog.clear();
            g_volumeLog.clear();
            g_soundRequests.clear();
            g_soundFiles.clear();
            g_particlesMade.clear();
            ClearStreams();
            const int deaths = g_deaths;
            a->Stop(k.immediately);
            std::vector<std::string> expected = {"find collframe ffff"};
            if (k.found)
                expected.push_back("prims collframe");
            const std::string cn = ItName(copy);
            for (const char *e : {"on ", "update "})
                expected.push_back(std::string(e) + cn + (e[0] == 'o' ? " 1" : ""));
            expected.push_back("on " + s.name + " 0");
            expected.push_back("update " + s.name);
            AN(r.stop, g_itLog == expected && a->IsStopped() && a->m_animating && g_anSets[copy]->playMode == 1 &&
                           a->m_collisionFrame == (k.found ? collisionFrame : nullptr) && a->m_collisions.empty() &&
                           g_volumeLog.size() == 2 && g_volumeLog[0].op == 'R' && (copy->m_frameFlags & 1u) &&
                           !(s.frame->m_frameFlags & 1u));
            AN(r.stop, g_anPrims == (k.found ? std::vector<uint32_t>{0x01000021, 0x02000020, 0x20}
                                             : std::vector<uint32_t>{0x01000001, 0x02000020, 0}));
            if (k.sound) {
                S_SoundRequest e;
                e.flags = SOUND_SILENT_TO_AI;
                e.attachTo = s.particle;
                e.source = a;
                e.chapter = SOUND_CHAPTER_EXPLOSION;
                e.group = 7;
                AN(r.stop, g_soundRequests.size() == 1 && std::memcmp(&g_soundRequests[0], &e, sizeof e) == 0);
            } else {
                AN(r.stop, g_soundRequests.empty());
            }
            AN(r.stop, k.particle ? g_particlesMade.size() == 1 && g_particlesMade[0].effect == 12 &&
                                        std::memcmp(&g_particlesMade[0].matrix, &s.particle->GetMatrix(), sizeof(S_matrix)) == 0 &&
                                        g_particlesMade[0].parent == nullptr && g_particlesMade[0].faceCamera &&
                                        !g_particlesMade[0].transient
                                  : g_particlesMade.empty());
            AN(r.stop, g_deaths == deaths + (k.death ? 1 : 0));
            AN(r.stop, k.message ? g_streams.size() == 1 && g_streams[0]->id == 60 && g_streams[0]->sent &&
                                       g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>{{k.immediately ? 1u : 0u, 1}}
                                 : g_streams.empty());
            // Once only.
            g_itLog.clear();
            g_soundRequests.clear();
            a->Stop(false);
            AN(r.stop, g_itLog.empty() && g_soundRequests.empty());
            s.frame->SetData(nullptr);
            g_itFound = nullptr;
            done(a);
        }
        // Without the particle frame: the sound on the frame, no particles; without the sound id none.
        g_gameRole = GAME_ROLE_CLIENT;
        {
            S_AnScene s = scene();
            C_AnimationActor *a = actor(s);
            a->StartMission(false);
            a->m_particleFrame = nullptr;
            g_soundRequests.clear();
            g_particlesMade.clear();
            a->Stop(false);
            AN(r.stop, g_soundRequests.size() == 1 && g_soundRequests[0].attachTo == s.frame && g_particlesMade.empty());
            done(a);
            S_AnScene t = scene();
            C_AnimationActor *b = actor(t);
            b->StartMission(false);
            b->m_explosionSound = -1;
            b->m_particleId = -1;
            b->m_collisionFrameName = "";                // (no collision frame: no search)
            g_soundRequests.clear();
            g_particlesMade.clear();
            g_itLog.clear();
            b->Stop(false);
            AN(r.stop, g_soundRequests.empty() && g_particlesMade.empty() && !g_itLog.empty() &&
                           g_itLog[0].compare(0, 5, "find ") != 0 && b->m_collisionFrame == nullptr);
            done(b);
        }
        // Without a frame: nothing more after the boxes.
        {
            auto *a = static_cast<C_AnimationActor *>(creator->Create());
            g_itLog.clear();
            a->Stop(false);
            AN(r.stop, !a->IsStopped() && g_itLog.empty());
            done(a);
        }
        // Without the copy (its load failed): the frame hidden but not stopped; a copy without an
        // animation: stopped, not animating.
        {
            S_AnScene s = scene();
            C_AnimationActor *a = actor(s);
            a->m_animName = "broken";
            g_cacheFailName = "broken";
            a->StartMission(false);
            g_cacheFailName.clear();
            a->Stop(true);
            AN(r.stop, !a->IsStopped() && !(s.frame->m_frameFlags & 1u));
            done(a);
            g_anCopyAnimated = false;
            S_AnScene t = scene();
            C_AnimationActor *b = actor(t);
            b->StartMission(false);
            g_anCopyAnimated = true;
            b->Stop(true);
            AN(r.stop, b->IsStopped() && !b->m_animating);
            done(b);
        }
        g_gameRole = GAME_ROLE_SERVER;
    }

    // --- Hit: the damage adds up with hit points over 0, not stopped, not destroyed yet; past the hit
    // points it is destroyed on the next step.
    {
        S_AnScene s = scene();
        C_AnimationActor *a = actor(s);
        a->StartMission(true);
        const S_vector p(0, 0, 0);
        a->m_hitPoints = 0.0f;
        a->Hit(HIT_BULLET, 30.0f, p, p, nullptr, 0);
        a->m_hitPoints = -1.0f;
        a->Hit(HIT_BULLET, 30.0f, p, p, nullptr, 0);
        AN(r.hit, a->m_damage == 0.0f && !a->m_destroy);
        a->m_hitPoints = 100.0f;
        a->Hit(HIT_BULLET, 30.0f, p, p, nullptr, 0);
        a->Hit(HIT_EXPLOSION, 70.0f, p, p, nullptr, 0);
        AN(r.hit, a->m_damage == 100.0f && !a->m_destroy);
        a->Hit(HIT_MELEE, 0.5f, p, p, nullptr, 0);
        AN(r.hit, a->m_damage == -1.0f && a->m_destroy && !a->IsStopped());
        a->m_destroy = false;
        a->Hit(HIT_MELEE, 500.0f, p, p, nullptr, 0);
        AN(r.hit, a->m_damage == -1.0f && !a->m_destroy);
        a->m_damage = 0.0f;
        a->m_stopped = true;
        a->Hit(HIT_MELEE, 500.0f, p, p, nullptr, 0);
        AN(r.hit, a->m_damage == 0.0f && !a->m_destroy);
        a->m_stopped = false;
        a->Hit(HIT_MELEE, 500.0f, p, p, nullptr, 0);
        ClearStreams();
        a->Update(10);
        // (Stopped not at once: the clients' message says so.)
        AN(r.hit, a->IsStopped() && !a->m_destroy && g_streams.size() == 1 &&
                      g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>{{0u, 1}});
        done(a);
    }

    // --- Messages: OnMessage stops it on clients (at once or not); the state is the stopped bit; a
    // stopped state stops it at once and runs its animation to the end.
    {
        for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_CLIENT, GAME_ROLE_SERVER}) {
            for (uint32_t bit : {0u, 1u}) {
                g_gameRole = role;
                S_AnScene s = scene();
                C_AnimationActor *a = actor(s);
                a->StartMission(role == GAME_ROLE_SERVER);
                g_soundRequests.clear();
                ItRead({{bit, 1}});
                a->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
                AN(r.messages, ItReadAll() && a->IsStopped() == (role == GAME_ROLE_CLIENT) &&
                                   g_soundRequests.size() == (role == GAME_ROLE_CLIENT && !bit ? 1u : 0u));
                S_FakeStream out{0, false, {}, false};
                a->WriteState(reinterpret_cast<C_BitStream *>(&out));
                AN(r.messages, out.bits == std::vector<std::pair<uint32_t, int>>{{a->IsStopped() ? 1u : 0u, 1}});
                done(a);
            }
        }
        g_gameRole = GAME_ROLE_CLIENT;
        for (uint32_t bit : {0u, 1u}) {
            S_AnScene s = scene();
            C_AnimationActor *a = actor(s);
            a->StartMission(false);
            I3D_frame *copy = reinterpret_cast<I3D_frame *>(a->m_model);
            g_soundRequests.clear();
            ItRead({{bit, 1}});
            a->ReadState(reinterpret_cast<C_BitStream *>(&g_readBits));
            AN(r.messages, ItReadAll() && a->IsStopped() == (bit != 0) && !a->m_animating &&
                               g_anTimes[copy] == (bit ? g_anEndTime - 1 : 0) && g_soundRequests.empty() &&
                               (bit ? a->m_collisions.size() == 1 && a->m_collisions[0]->volume.frame != s.boxes[0]
                                    : a->m_collisions.size() == 2));
            // (Stopped, the animation over: the state is still "stopped".)
            S_FakeStream out{0, false, {}, false};
            a->WriteState(reinterpret_cast<C_BitStream *>(&out));
            AN(r.messages, out.bits == std::vector<std::pair<uint32_t, int>>{{bit, 1}});
            done(a);
        }
        g_gameRole = GAME_ROLE_SERVER;
    }

    // --- SaveGame: one of intact (1000), destroyed (2000), animating (3000: the time), then the held
    // flag (4000); LoadGame reads the first only (quiet: no sound, script, message), then the wings'
    // places, the refresh; another id or none: ascended.
    {
        std::vector<std::vector<uint8_t>> saves;
        for (int state = 0; state < 3; ++state) {
            S_AnScene s = scene();
            C_AnimationActor *a = actor(s);
            a->StartMission(true);
            I3D_frame *copy = reinterpret_cast<I3D_frame *>(a->m_model);
            if (state > 0) {
                a->m_stopped = true;
                a->m_animating = state == 2;
                g_anTimes[copy] = 321;
            }
            a->m_animationHeld = state == 1;
            C_chunk w;
            w.Open("an.sav", true);
            w.Begin(0x777);
            AN(r.save, a->SaveGame(w));
            w.Ascend();
            w.Close();
            std::vector<uint8_t> e;
            const auto put = [&](const void *p, size_t n) {
                const auto *b = static_cast<const uint8_t *>(p);
                e.insert(e.end(), b, b + n);
            };
            const auto head = [&](uint16_t id, uint32_t size) {
                put(&id, 2);
                put(&size, 4);
            };
            const uint16_t ids[] = {1000, 2000, 3000};
            head(0x777, 6 + (state == 2 ? 10 : 6) + 7);
            head(ids[state], state == 2 ? 10 : 6);
            if (state == 2) {
                const int t = 321;
                put(&t, 4);
            }
            head(4000, 7);
            const uint8_t held = state == 1;
            put(&held, 1);
            AN(r.save, g_files["an.sav"] == e);
            saves.push_back(e);
            done(a);
        }
        // Loading each state as a server (quiet), with the permanent refresh and a wing.
        for (int state = 0; state < 3; ++state) {
            g_gameRole = 3;
            S_AnScene s = scene();
            I3D_frame *wingFrame = AnFrame(FRAME_VISUAL, "right_wing", s.frame);
            *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(s.frame) + 0x134) = "#mchescape";
            C_AnimationActor *a = actor(s);
            a->StartMission(false);
            I3D_frame *copy = reinterpret_cast<I3D_frame *>(a->m_model);
            a->m_permanentRefresh = true;
            s.frame->SetData(&scriptData);
            wingFrame->m_localMatrix.m[3][0] += 3.0f;
            wingFrame->m_frameFlags &= ~FRMFLAGS_BOUND_VALID;
            g_files["an.load"] = saves[static_cast<size_t>(state)];
            C_chunk rd;
            rd.Open("an.load", false);
            uint16_t id;
            rd.Descend(id);
            const int deaths = g_deaths;
            g_soundRequests.clear();
            ClearStreams();
            g_volumeLog.clear();
            g_itLog.clear();
            AN(r.load, a->LoadGame(rd) && rd.m_depth == 1);
            const I3D_bound_volume &bound = wingFrame->GetBound();
            AN(r.load, g_deaths == deaths && g_soundRequests.empty() && g_streams.empty() && a->m_refreshes == 2 &&
                           !a->m_animationHeld && a->m_wings[0]->lastPos.x == bound.center.x &&
                           a->IsStopped() == (state != 0) && a->m_animating == (state == 2) &&
                           g_anTimes[copy] == (state == 0 ? 0 : state == 1 ? g_anEndTime - 1 : 321) &&
                           (state != 0 || (g_volumeLog.size() == 2 && g_volumeLog[0].op == 'M' &&
                                           g_volumeLog[0].volume == &a->m_collisions[1]->volume)));
            if (state == 2)
                AN(r.load, ItLogIs({"find collframe ffff", "on " + ItName(copy) + " 1", "update " + ItName(copy), "on " + s.name + " 0",
                                    "update " + s.name, "tick " + ItName(copy) + " 321"}));
            rd.Close();
            // Not quiet after it.
            g_soundRequests.clear();
            s.frame->SetData(nullptr);
            done(a);
        }
        g_gameRole = GAME_ROLE_SERVER;
        // The held flag first: read; another id: entered and left; nothing: the actor's chunk left.
        {
            S_AnScene s = scene();
            C_AnimationActor *a = actor(s);
            a->StartMission(true);
            std::vector<uint8_t> &x = g_files["an.load"];
            x.clear();
            const auto add = [&](const void *p, size_t n) {
                const auto *b = static_cast<const uint8_t *>(p);
                x.insert(x.end(), b, b + n);
            };
            const auto head = [&](uint16_t hid, uint32_t size) {
                add(&hid, 2);
                add(&size, 4);
            };
            head(0x777, 6 + 7);
            head(4000, 7);
            const uint8_t one = 1;
            add(&one, 1);
            head(0x778, 6 + 6);
            head(5000, 6);
            head(0x779, 6);
            C_chunk rd;
            rd.Open("an.load", false);
            uint16_t id;
            rd.Descend(id);
            AN(r.load, a->LoadGame(rd) && a->m_animationHeld && rd.m_depth == 1 && rd.RemainingSize() == 0 &&
                           a->m_refreshes == 0);
            rd.Ascend();
            rd.Descend(id);
            AN(r.load, id == 0x778 && a->LoadGame(rd) && rd.m_depth == 1 && rd.RemainingSize() == 0);
            rd.Ascend();
            rd.Descend(id);
            AN(r.load, id == 0x779 && a->LoadGame(rd) && rd.m_depth == 0 && !a->IsStopped());
            rd.Close();
            done(a);
        }
    }

    // --- The mission: StopMission (a listen server's server side only deactivates): the wings let go,
    // the collision frame's primitives on again, the copy gone, the frame shown, the damage reset;
    // RestartMission stops and starts each side.
    {
        for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_SERVER, 3, GAME_ROLE_CLIENT, GAME_ROLE_LOCAL}) {
            g_gameRole = role;
            S_AnScene s = scene();
            I3D_frame *wingFrame = AnFrame(FRAME_VISUAL, "left_wing", s.frame);
            *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(s.frame) + 0x134) = "#mchescape";
            C_AnimationActor *a = actor(s);
            a->StartMission(role == GAME_ROLE_SERVER);
            I3D_frame *copy = reinterpret_cast<I3D_frame *>(a->m_model);
            const std::string cn = ItName(copy);
            I3D_frame *collisionFrame = AnFrame(FRAME_DUMMY, "collframe", nullptr);
            g_itFound = collisionFrame;
            g_anPrimFrame = collisionFrame;
            g_anPrims = {0x20, 0x01000000};
            a->Stop(true);
            a->m_damage = 5.0f;
            const int wingRefs = wingFrame->m_refCount;
            const int copyRefs = copy->m_refCount;
            g_deactivations.clear();
            g_itLog.clear();
            g_volumeLog.clear();
            const bool server = role != GAME_ROLE_CLIENT;
            a->StopMission(server);
            if (role == 3 || role == GAME_ROLE_LOCAL) {
                AN(r.mission, g_deactivations == std::vector<bool>{true} && g_itLog.empty() && a->m_model && a->IsStopped());
                g_itLog.clear();
                a->StopMission(false);
            }
            AN(r.mission, ItLogIs({"release left_wing", "prims collframe", "link " + cn + " 0 0", "release " + cn,
                                   "on " + s.name + " 1", "update " + s.name}) &&
                              a->m_wings.empty() && wingFrame->m_refCount == wingRefs - 1 && a->m_collisionFrame == nullptr &&
                              g_anPrims == std::vector<uint32_t>({0, 0x01000000}) && a->m_model == nullptr &&
                              copy->m_refCount == copyRefs - 1 && !a->IsStopped() && !a->m_animating && a->m_damage == 0.0f &&
                              (s.frame->m_frameFlags & 1u));
            g_itFound = nullptr;
            // Restart: each side stopped, then each side started.
            g_activations.clear();
            g_deactivations.clear();
            a->RestartMission(true);
            const std::vector<bool> sides = role == 3 ? std::vector<bool>{true, false}
                                            : role == GAME_ROLE_CLIENT ? std::vector<bool>{false}
                                            : role == GAME_ROLE_LOCAL ? std::vector<bool>{true, false}
                                                                      : std::vector<bool>{true};
            AN(r.restart, g_deactivations == sides && g_activations == sides);
            done(a);
        }
        g_gameRole = GAME_ROLE_SERVER;
        // Standing: StopMission removes its boxes.
        {
            S_AnScene t = scene();
            C_AnimationActor *b = actor(t);
            b->StartMission(true);
            g_volumeLog.clear();
            b->StopMission(true);
            AN(r.mission, b->m_collisions.empty() && g_volumeLog.size() == 2 && g_volumeLog[0].op == 'R' &&
                              g_volumeLog[1].op == 'R');
            done(b);
        }
        // SetCollisionsOn: off removes the boxes, on makes them from the frame again.
        S_AnScene s = scene();
        C_AnimationActor *a = actor(s);
        a->StartMission(true);
        g_volumeLog.clear();
        a->SetCollisionsOn(false);
        AN(r.mission, a->m_collisions.empty() && g_volumeLog.size() == 2 && g_volumeLog[0].op == 'R');
        a->SetCollisionsOn(true);
        AN(r.mission, a->m_collisions.size() == 2 && a->m_collisions[0]->volume.frame == s.boxes[0]);
        a->SetCollisionsOn(true);
        AN(r.mission, a->m_collisions.size() == 2);
        done(a);
    }

    // --- The notes: "$<file> <near> <far>" and "@<group>" at the frame; other notes and messages nothing.
    {
        I3D_frame *f = AnFrame(FRAME_VISUAL, "an.noted", nullptr);
        const auto note = [&](uint32_t message, const char *text) {
            g_soundRequests.clear();
            g_soundFiles.clear();
            return C_AnimationActor::NoteProc(f, message, reinterpret_cast<uintptr_t>(text), 5);
        };
        S_SoundRequest e;
        e.nearRange = 5.0f;
        e.farRange = 30.5f;
        e.attachTo = f;
        e.flags = SOUND_SILENT_TO_AI;
        AN(r.notes, note(0x1000, "$snd\\boom.wav 5 30.5") == 0 && g_soundRequests.size() == 1 &&
                        g_soundFiles[0] == "snd\\boom.wav");
        e.fileName = g_soundRequests.empty() ? nullptr : g_soundRequests[0].fileName;
        AN(r.notes, !g_soundRequests.empty() && std::memcmp(&g_soundRequests[0], &e, sizeof e) == 0);
        AN(r.notes, note(0x1001, "$a 1.5 2.25 more") == 0 && g_soundRequests.size() == 1 && g_soundFiles[0] == "a" &&
                        g_soundRequests[0].nearRange == 1.5f && g_soundRequests[0].farRange == 2.25f);
        S_SoundRequest g;
        g.group = 12;
        g.chapter = SOUND_CHAPTER_ACTION;
        g.attachTo = f;
        g.flags = SOUND_SILENT_TO_AI;
        AN(r.notes, note(0x1000, "@12") == 0 && g_soundRequests.size() == 1 &&
                        std::memcmp(&g_soundRequests[0], &g, sizeof g) == 0);
        for (const char *bad : {"$a 5", "$a", "x 1 2", "", "@"}) {
            const int result = note(0x1000, bad);
            AN(r.notes, result == 0 && (bad[0] == '@' ? g_soundRequests.size() == 1 && g_soundRequests[0].group == 0
                                                       : g_soundRequests.empty()));
        }
        AN(r.notes, note(0x0FFF, "@12") == 0 && g_soundRequests.empty() && note(0x1000, nullptr) == 0 && g_soundRequests.empty());
    }

    // --- The armour callback: over 1500 heavy, over 505 light (and the enumeration stops), else on.
    {
        auto *a = static_cast<C_AnimationActor *>(creator->Create());
        I3D_frame *f = AnFrame(FRAME_VISUAL, "an.armour", nullptr);
        const uint32_t costs[] = {100, 505, 506, 1500, 1501};
        const int kinds[] = {ANIMATION_ARMOUR_NONE, ANIMATION_ARMOUR_NONE, ANIMATION_ARMOUR_LIGHT, ANIMATION_ARMOUR_LIGHT,
                             ANIMATION_ARMOUR_HEAVY};
        for (int i = 0; i < 5; ++i) {
            materials[0].penetrationCost = costs[i];
            g_frameMaterials[f] = 0;
            a->m_armour = ANIMATION_ARMOUR_NONE;
            const uint32_t result = C_AnimationActor::ArmourProc(f, a);
            AN(r.armour, a->m_armour == kinds[i] && result == (kinds[i] == ANIMATION_ARMOUR_NONE ? 0u : 2u));
        }
        materials[0].penetrationCost = 100;
        g_frameMaterials.erase(f);
        // Without a material: the actor's default, 11.
        materials[11].penetrationCost = 1501;
        a->m_armour = ANIMATION_ARMOUR_NONE;
        AN(r.armour, C_AnimationActor::ArmourProc(f, a) == 2 && a->m_armour == ANIMATION_ARMOUR_HEAVY);
        materials[11].penetrationCost = 100;
        done(a);
    }

    // --- The wings: under 0.2 m nothing; then the sphere from the last place along the move: a car, or
    // a human in one (the car first), is hit and counts; a human afoot is hit and does not; a static
    // volume or another kind nothing; a model's part by its model's actor. The actor hit by its wing.
    {
        I3D_frame *f = AnFrame(FRAME_VISUAL, "an.wing", nullptr);
        const int refs = f->m_refCount;
        auto *wing = new C_AnimationActor::S_Wing(f);
        AN(r.wings, f->m_refCount == refs + 1 && wing->lastPos.y == f->GetBound().center.y && wing->lastPos.w == 1.25f);
        C_AnTarget car(ACTOR_CAR), barrel(ACTOR_BARREL), gun(ACTOR_STATIC_WEAPON);
        C_Human walker, driver, gunner;
        driver.vehicle = &car;
        gunner.vehicle = &gun;                          // (a vehicle that is not a car)
        I3D_frame *carFrame = AnFrame(FRAME_MODEL, "an.car", nullptr);
        I3D_frame *carPart = AnFrame(FRAME_VISUAL, "an.carpart", carFrame);
        I3D_frame *other = AnFrame(FRAME_VISUAL, "an.other", nullptr);
        // A part whose owner is not a model: its owner's actor does not count.
        I3D_frame *dummyOwner = AnFrame(FRAME_DUMMY, "an.dummyowner", nullptr);
        I3D_frame *dummyPart = AnFrame(FRAME_VISUAL, "an.dummypart", dummyOwner);
        S_FrameData carData = {}, walkerData = {}, driverData = {}, barrelData = {}, gunnerData = {};
        gunnerData.actor = &gunner;
        carData.actor = &car;
        walkerData.actor = &walker;
        driverData.actor = &driver;
        barrelData.actor = &barrel;
        carFrame->SetData(&carData);
        carPart->m_ownerModel = carFrame;
        dummyOwner->SetData(&carData);
        dummyPart->m_ownerModel = dummyOwner;
        S_CollisionVolume volume;
        g_anSweepHit = &volume;
        struct S_Case {
            uint32_t flags;
            S_FrameData *data;
            I3D_frame *frame;
            bool counts;
            int carHits, humanHits;
        };
        I3D_frame *walkerFrame = AnFrame(FRAME_VISUAL, "an.walker", nullptr);
        walkerFrame->SetData(&walkerData);
        I3D_frame *driverFrame = AnFrame(FRAME_VISUAL, "an.driver", nullptr);
        driverFrame->SetData(&driverData);
        I3D_frame *barrelFrame = AnFrame(FRAME_VISUAL, "an.barrel", nullptr);
        barrelFrame->SetData(&barrelData);
        I3D_frame *gunnerFrame = AnFrame(FRAME_VISUAL, "an.gunner", nullptr);
        gunnerFrame->SetData(&gunnerData);
        const S_Case cases[] = {{COLLISION_FLAG_DYNAMIC, nullptr, carFrame, true, 1, 0},
                                {COLLISION_FLAG_DYNAMIC, nullptr, walkerFrame, false, 0, 1},
                                {COLLISION_FLAG_DYNAMIC, nullptr, driverFrame, true, 1, 1},
                                {COLLISION_FLAG_DYNAMIC, nullptr, barrelFrame, false, 0, 0},
                                {0x07000000, nullptr, carFrame, false, 0, 0},
                                {COLLISION_FLAG_DYNAMIC, nullptr, carPart, true, 1, 0},
                                {COLLISION_FLAG_DYNAMIC, nullptr, other, false, 0, 0},
                                {COLLISION_FLAG_DYNAMIC, nullptr, dummyPart, false, 0, 0},
                                {COLLISION_FLAG_DYNAMIC, nullptr, gunnerFrame, false, 0, 1}};
        for (const S_Case &k : cases) {
            // Under 0.2 m: nothing.
            const S_vector4 before = wing->lastPos;
            f->m_localMatrix.m[3][0] += 0.1f;
            f->m_frameFlags &= ~FRMFLAGS_BOUND_VALID;
            g_anSweeps.clear();
            AN(r.wings, !wing->Sweep() && g_anSweeps.empty() && std::memcmp(&wing->lastPos, &before, 16) == 0);
            f->m_localMatrix.m[3][0] += 0.2f;
            f->m_localMatrix.m[3][1] += rnd(-0.5f, 0.5f);
            f->m_localMatrix.m[3][2] += rnd(0.1f, 0.6f);
            f->m_frameFlags &= ~FRMFLAGS_BOUND_VALID;
            volume.flags = k.flags;
            volume.frame = k.frame;
            car.hits.clear();
            walker.hits.clear();
            driver.hits.clear();
            gunner.hits.clear();
            gun.hits.clear();
            g_hitOrder.clear();
            const bool counts = wing->Sweep();
            const I3D_bound_volume &bound = f->GetBound();
            const S_vector move(bound.center.x - before.x, bound.center.y - before.y, bound.center.z - before.z);
            const size_t humanHits = walker.hits.size() + driver.hits.size() + gunner.hits.size();
            AN(r.wings, counts == k.counts && g_anSweeps.size() == 1 && g_anSweeps[0].radius == bound.radius &&
                            g_anSweeps[0].types == 0x6FD800 && g_anSweeps[0].flags == 0xFFFFFFFF &&
                            std::memcmp(&g_anSweeps[0].from, &before, 12) == 0 && std::memcmp(&g_anSweeps[0].dir, &move, 12) == 0 &&
                            car.hits.size() == static_cast<size_t>(k.carHits) && humanHits == static_cast<size_t>(k.humanHits) &&
                            barrel.hits.empty() && gun.hits.empty() && wing->lastPos.x == bound.center.x &&
                            wing->lastPos.w == bound.radius);
            if (!car.hits.empty())
                AN(r.wings, car.hits[0].type == HIT_EXPLOSION && car.hits[0].amount == 1000000.0f &&
                                std::memcmp(&car.hits[0].pos, &before, 12) == 0 && std::memcmp(&car.hits[0].dir, &move, 12) == 0 &&
                                car.hits[0].attacker == nullptr && car.hits[0].flags == 0 &&
                                g_hitOrder[0] == &car);
            if (!driver.hits.empty())
                AN(r.wings, driver.hits[0].amount == 1000000.0f && std::memcmp(&driver.hits[0].dir, &move, 12) == 0);
        }
        g_anSweepHit = nullptr;
        delete wing;
        AN(r.wings, f->m_refCount == refs);
        // The actor's wing hits a car: the actor hits itself (100000 at the frame, along its z axis).
        S_AnScene s = scene(false);
        I3D_frame *wf = AnFrame(FRAME_VISUAL, "right_wing", s.frame);
        *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(s.frame) + 0x134) = "#mchescape";
        C_AnimationActor *a = actor(s);
        a->StartMission(true);
        a->m_permanentRefresh = true;
        a->m_hitPoints = 99999.0f;
        volume.flags = COLLISION_FLAG_DYNAMIC;
        volume.frame = carFrame;
        g_anSweepHit = &volume;
        wf->m_localMatrix.m[3][2] += 1.0f;
        wf->m_frameFlags &= ~FRMFLAGS_BOUND_VALID;
        a->Update(10);
        AN(r.wings, a->m_destroy && a->m_damage == -1.0f);
        a->m_hitPoints = 100001.0f;
        a->m_destroy = false;
        a->m_damage = 0.0f;
        wf->m_localMatrix.m[3][2] += 1.0f;
        wf->m_frameFlags &= ~FRMFLAGS_BOUND_VALID;
        a->Update(10);
        AN(r.wings, !a->m_destroy && a->m_damage == 100000.0f);
        g_anSweepHit = nullptr;
        a->StopMission(true);
        done(a);
    }

    g_gameRole = oldRole;
    g_materials = oldMaterials;
    g_cacheFill = nullptr;
    g_sphereTestHook = nullptr;
    g_reliableActorStreams = false;
    g_itFound = nullptr;
    g_anPrimFrame = nullptr;
    g_sceneVtable[0x7C / 4] = oldFind;
    const int slots[] = {0x00, 0x20, 0x24, 0x28, 0x30, 0x34, 0x38, 0x3C, 0x58, 0x88, 0xA0};
    for (int i = 0; i < 11; ++i)
        g_frameVtable[slots[i] / 4] = oldFrame[i];
    ClearStreams();
    std::printf("animation: creator %d frame %d props %d copy %d start %d collisions %d step %d stop %d hit %d messages %d "
                "save %d load %d mission %d notes %d armour %d wings %d restart %d\n",
                r.creator, r.frame, r.props, r.copy, r.start, r.collisions, r.step, r.stop, r.hit, r.messages, r.save,
                r.load, r.mission, r.notes, r.armour, r.wings, r.restart);
}
