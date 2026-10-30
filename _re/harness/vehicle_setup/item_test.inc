// Part 6a's test (the actors build, build_item.bat: -DRIGID_STEP_TEST -DITEM_TEST links
// src/game/debris.cpp and item_actor.cpp with the rigid body's sources and runs only ItemTest on
// every model): C_Debris and C_ItemActor against oracles written from the original's code, their
// engine calls logged in order (the frames' slots, the scene, the effects, the temporary list's
// messages, the items). Their bodies are the real ones (part 5), compared with twin bodies set up
// and stepped the same way on frames of their own. check_item.py reads the result line. CAR_TRACE=1
// prints the failing check lines. (Uses rigid_body_test.inc's and rigid_step_test.inc's helpers.)

#include <initializer_list>

#include "game/debris.h"
#include "game/item_actor.h"
#include "game/sound_table.h"

// --- the log: the engine calls the actors make, in order ---------------------------------------------------

std::vector<std::string> g_itLog;
void ItLog(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_itLog.push_back(text);
}
bool ItLogIs(std::initializer_list<std::string> expected)
{
    const bool same = g_itLog == std::vector<std::string>(expected);
    if (!same && std::getenv("CAR_TRACE")) {
        std::printf("  log:");
        for (const std::string &s : g_itLog)
            std::printf(" [%s]", s.c_str());
        std::printf("\n");
    }
    return same;
}
// Drops the body's own calls in front of the log (its steps move the frame: Update, the sector).
void ItDropBody()
{
    size_t i = 0;
    while (i < g_itLog.size() && (g_itLog[i].compare(0, 7, "update ") == 0 || g_itLog[i].compare(0, 7, "sector ") == 0))
        ++i;
    g_itLog.erase(g_itLog.begin(), g_itLog.begin() + static_cast<std::ptrdiff_t>(i));
}
bool ItNoLog()
{
    ItDropBody();
    return g_itLog.empty();
}
bool ItLogIsAfterBody(std::initializer_list<std::string> expected)
{
    ItDropBody();
    return ItLogIs(expected);
}
std::map<const I3D_frame *, std::string> g_itNames;   // names given by SetName (frames without a stub)
std::string ItName(const I3D_frame *f)
{
    if (!f)
        return "0";
    if (f == g_sector)
        return "sector";
    if (g_itNames.count(f))
        return g_itNames.at(f);
    auto it = g_stubs.find(f);
    return it == g_stubs.end() ? "?" : it->second->name;
}
std::string ItV(const S_vector &v)
{
    char text[96];
    std::snprintf(text, sizeof text, "%.9g %.9g %.9g", v.x, v.y, v.z);
    return text;
}

// --- the engine: the frames' slots, the scene, the effects ----------------------------------------------

uint32_t __stdcall F_ItRelease(I3D_frame *self)
{
    ItLog("release %s", ItName(self).c_str());
    return F_Release(self);
}
void __stdcall F_ItSetPosRot(I3D_frame *self, const S_vector &pos, const S_quat &rot)
{
    ItLog("posrot %s %s %.9g %.9g %.9g %.9g", ItName(self).c_str(), ItV(pos).c_str(), rot.s, rot.v.x, rot.v.y, rot.v.z);
    F_SetPosRot(self, pos, rot);
}
void __stdcall F_ItSetWorldDir(I3D_frame *self, const S_vector &dir, float roll)
{
    ItLog("dir %s %s %.9g", ItName(self).c_str(), ItV(dir).c_str(), roll);
}
void __stdcall F_ItUpdate(I3D_frame *self)
{
    ItLog("update %s", ItName(self).c_str());
}
void __stdcall F_ItSetOn(I3D_frame *self, bool on)
{
    ItLog("on %s %d", ItName(self).c_str(), on);
    F_SetOn(self, on);
}
// (Keeping the world transform as the engine does.)
void __stdcall F_ItLinkTo(I3D_frame *self, I3D_frame *parent, uint32_t flags)
{
    ItLog("link %s %s %u", ItName(self).c_str(), ItName(parent).c_str(), flags);
    const S_matrix world = self->GetMatrix();
    F_LinkTo(self, parent, flags);
    if (flags & I3DLINK_KEEP_WORLD)
        self->m_localMatrix = parent ? world * RbInverse(parent->GetMatrix()) : world;
}
void __stdcall F_ItSetHierarchyAlpha(I3D_frame *self, float alpha)
{
    ItLog("alpha %s %.9g", ItName(self).c_str(), alpha);
}
I3D_sector *__stdcall F_ItGetSector(void *, const S_vector &pos)
{
    ItLog("getsector %s", ItV(pos).c_str());
    return reinterpret_cast<I3D_sector *>(g_sector);
}
I3D_frame *g_itFound;
I3D_frame *__stdcall F_ItFindFrame(void *, const char *name, uint32_t mask)
{
    ItLog("find %s %x", name, mask);
    return g_itFound;
}
void __stdcall F_ItUpdateFrameSector(void *, I3D_frame *frame, const S_vector &pos)
{
    ItLog("sector %s %s", ItName(frame).c_str(), ItV(pos).c_str());
}

bool C_GEffectsModule::ClearModelTransparency(I3D_frame *model)
{
    ItLog("clear %s", ItName(model).c_str());
    return model != nullptr;
}
void C_GEffectsModule::FadeOutModel(I3D_frame *model, int time, bool unknown)
{
    ItLog("fade %s %d %d", ItName(model).c_str(), time, unknown);
}
void FreeFrameData(I3D_frame *frame)
{
    ItLog("freedata %s", ItName(frame).c_str());
}

// --- the temporary list, its messages, the items -------------------------------------------------------

std::vector<C_Actor *> g_itDestroyed;
void C_TemporaryActorsList::DestroyActor(C_Actor *actor)
{
    ItLog("destroy");
    g_itDestroyed.push_back(actor);
}
struct S_ItTemp {
    S_FakeStream *stream;
    bool reliable;
};
std::vector<S_ItTemp> g_itTemps;
C_BitStream *CreateTempActorMessage(int id, bool reliable)
{
    ItLog("msg %d %d", id, reliable);
    g_streams.push_back(new S_FakeStream{id, false, {}, false});
    g_itTemps.push_back({g_streams.back(), reliable});
    return reinterpret_cast<C_BitStream *>(g_streams.back());
}
void SendTempActorMessage(C_BitStream *msg)
{
    ItLog("send");
    reinterpret_cast<S_FakeStream *>(msg)->sent = true;
}
// Strings and axis-angle pairs in the fake streams: a string is its index in g_itStrings (bit count
// -6); an axis's floats -4 (player -1, else -8), the angle -5.
std::vector<std::string> g_itStrings;
void C_BitStream::WriteString(const char *text)
{
    g_itStrings.push_back(text);
    reinterpret_cast<S_FakeStream *>(this)->bits.emplace_back(static_cast<uint32_t>(g_itStrings.size() - 1), -6);
}
void C_BitStream::ReadString(char *buffer, int bufferSize)
{
    ItLog("readstring %d", bufferSize);
    buffer[0] = 0;
    if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != -6) {
        g_readMismatch = true;
        return;
    }
    const std::string &s = g_itStrings.at(g_readBits[g_readPos++].first);
    std::snprintf(buffer, static_cast<size_t>(bufferSize), "%s", s.c_str());
}
uint32_t ItF(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}
void C_NetMessage::WriteAxisAngle(const S_vector &axis, float angle, int playerIndex)
{
    auto *s = reinterpret_cast<S_FakeStream *>(this);
    for (float f : {axis.x, axis.y, axis.z})
        s->bits.emplace_back(ItF(f), playerIndex == -1 ? -4 : -8);
    s->bits.emplace_back(ItF(angle), -5);
}
void C_NetMessage::ReadAxisAngle(S_vector &axis, float &angle, int playerIndex)
{
    float v[4] = {};
    for (int i = 0; i < 4; ++i) {
        const int tag = i == 3 ? -5 : playerIndex == -1 ? -4 : -8;
        if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != tag) {
            g_readMismatch = true;
            return;
        }
        std::memcpy(&v[i], &g_readBits[g_readPos++].first, sizeof v[i]);
    }
    axis = S_vector(v[0], v[1], v[2]);
    angle = v[3];
}

std::vector<C_Item *> g_itDestroyedItems;
void C_ItemsModule::DestroyItem(C_Item *item)
{
    ItLog("destroyitem");
    g_itDestroyedItems.push_back(item);
}
I3D_frame *g_itItemFrame;
I3D_frame *C_Item::GetFrame(int index)
{
    ItLog("itemframe %d", index);
    return g_itItemFrame;
}
class C_ItTestDef : public C_ItemDef {
public:
    C_ItTestDef() : C_ItemDef(static_cast<E_ItemRecordType>(0), 0) {}
    int GetCarryMode() override { return 0; }
    int GetBehaviour() override { return behaviour; }
    int GetAmmoItemId() override { return 0; }
    int behaviour = 0;
};

// --- the oracles (from the original's code) --------------------------------------------------------------

using T_ItBits = std::vector<std::pair<uint32_t, int>>;
void ItAdd(T_ItBits &bits, const S_vector &v, int tag)
{
    for (float f : {v.x, v.y, v.z})
        bits.emplace_back(ItF(f), tag);
}
// The item's direction normalization: the sum ((y² + z²) + x²); NaN scales; a (nearly) zero vector
// becomes the unit along its first non-zero axis of x, z, y (y when none).
S_vector ItNormalize(S_vector v)
{
    const float sq = (v.y * v.y + v.z * v.z) + v.x * v.x;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return v;
    if (!(sq < 1e-8f)) {
        const float k = 1.0f / std::sqrt(sq);
        return S_vector(v.x * k, v.y * k, v.z * k);
    }
    if (v.x != 0.0f)
        v.x = v.x < 0.0f ? -1.0f : 1.0f;
    else if (v.z != 0.0f)
        v.z = v.z < 0.0f ? -1.0f : 1.0f;
    else
        v.y = v.y < 0.0f ? -1.0f : 1.0f;
    return v;
}
// The push's length: ((x² + y²) + z²); 0 under 1e-8 (its bits compared as an int), 1 within 1e-8 of 1.
float ItLength(const S_vector &v)
{
    const float sq = (v.x * v.x + v.y * v.y) + v.z * v.z;
    if (static_cast<int32_t>(ItF(sq)) < 0x322bcc77)
        return 0.0f;
    if (!(std::fabs(sq - 1.0f) < 1e-8f))
        return std::sqrt(sq);
    return 1.0f;
}
// The item's body as SetupItem / ResetBody make it (the original's parameters), on `frame`.
void ItTwin(C_RigidBody &twin, I3D_frame *frame, const S_vector &velocity, C_Actor *owner)
{
    S_RigidBodyParams p;
    p.friction = 0.8f;
    p.velocity = {velocity.x, velocity.y, velocity.z, 0.0f};
    p.weight = g_itemWeight;
    p.unknown41 = g_itemSize < 2;
    twin.SetFrame(frame);
    twin.Setup(p);
    twin.Start();
    twin.SetOwner(owner);
}
// Two bodies in the same state (on frames of their own).
bool ItSameBody(const C_RigidBody &a, const C_RigidBody &b)
{
    return std::memcmp(&a.m_pos, &b.m_pos, 16) == 0 && std::memcmp(&a.m_dir, &b.m_dir, 16) == 0 &&
           std::memcmp(&a.m_velocity, &b.m_velocity, 12) == 0 && std::memcmp(&a.m_spin, &b.m_spin, 12) == 0 &&
           std::memcmp(&a.m_invInertia, &b.m_invInertia, 12) == 0 && std::memcmp(&a.m_centre, &b.m_centre, 12) == 0 &&
           std::memcmp(&a.m_gravity, &b.m_gravity, 16) == 0 && a.m_state == b.m_state && a.m_mode == b.m_mode &&
           a.m_stillTime == b.m_stillTime && a.m_slowTime == b.m_slowTime && a.m_mass == b.m_mass &&
           a.m_friction == b.m_friction && a.m_restVolume == b.m_restVolume && a.m_moveCollision == b.m_moveCollision &&
           a.m_radius == b.m_radius && a.m_width == b.m_width && a.m_restDelay == b.m_restDelay;
}
// The body's collision volume removed (Stop) and added (Start) since the log was cleared: "R", "A".
std::string ItVolumeOps(const C_RigidBody &b)
{
    std::string ops;
    for (const S_VolumeOp &v : g_volumeLog)
        if (v.volume == &b.m_volume)
            ops += v.op;
    if (std::getenv("CAR_TRACE"))
        std::printf("  volume ops: %s\n", ops.c_str());
    return ops;
}
S_FakeStream *ItLastTemp()
{
    return g_itTemps.empty() ? nullptr : g_itTemps.back().stream;
}
bool ItSent(const T_ItBits &bits, int id)
{
    const S_FakeStream *s = ItLastTemp();
    const bool same = s && s->sent && s->id == id && g_itTemps.back().reliable && s->bits == bits;
    if (!same && s && std::getenv("CAR_TRACE")) {
        std::printf("  bits:");
        for (const auto &b : s->bits)
            std::printf(" %08x/%d", b.first, b.second);
        std::printf("\n");
    }
    return same;
}
void ItRead(const T_ItBits &bits)
{
    g_readBits = bits;
    g_readPos = 0;
    g_readMismatch = false;
}
bool ItReadAll()
{
    return !g_readMismatch && g_readPos == g_readBits.size();
}

// --- the test ------------------------------------------------------------------------------------------

struct S_It {
    bool creator = true, frame = true, setup = true, life = true, update = true, tick = true, hit = true, push = true,
         contact = true, sound = true, message = true, mission = true, save = true, teleport = true, placement = true,
         respawn = true, debris = true, debrisLife = true;
};
#define IT(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

void ItemTest(const char *path)
{
    if (!LoadModel(path))
        return;
    g_itNames.clear();                                  // (the last model's frames are freed: their
                                                        // addresses come back)
    std::printf("== %s\n", g_modelFile.c_str());
    void *const oldFrame[] = {g_frameVtable[0x00 / 4], g_frameVtable[0x08 / 4], g_frameVtable[0x18 / 4],
                              g_frameVtable[0x1C / 4], g_frameVtable[0x24 / 4], g_frameVtable[0x30 / 4],
                              g_frameVtable[0x38 / 4], g_frameVtable[0x54 / 4]};
    g_frameVtable[0x04 / 4] = reinterpret_cast<void *>(&F_RbVisualType);
    g_frameVtable[0x90 / 4] = reinterpret_cast<void *>(&F_RbUpdateLocalBound);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_RbUpdateBound);
    g_frameVtable[0x10 / 4] = reinterpret_cast<void *>(&F_RbSetWorldPosDir3);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_RbSetPos);
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x18 / 4] = reinterpret_cast<void *>(&F_ItSetPosRot);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_ItSetWorldDir);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x54 / 4] = reinterpret_cast<void *>(&F_ItSetHierarchyAlpha);
    void *const oldScene[] = {g_sceneVtable[0x7C / 4], g_sceneVtable[0xE0 / 4], g_sceneVtable[0xEC / 4]};
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_ItFindFrame);
    g_sceneVtable[0xE0 / 4] = reinterpret_cast<void *>(&F_ItGetSector);
    g_sceneVtable[0xEC / 4] = reinterpret_cast<void *>(&F_ItUpdateFrameSector);
    g_rayTestHook = &RbRayTest;
    g_rbFaces.clear();
    GetGame()->m_scene = g_carScene;
    const uint8_t oldRole = g_gameRole;
    S_It r;

    // The model's boxes give the items' shapes (each model a different set), its frame count the rest.
    std::vector<I3D_bbox> shapes;
    for (S_StubFrame *s : g_frames) {
        const I3D_bbox b = RbBox(s->frame);
        if (shapes.size() < 3 && b.min.x <= b.max.x && b.max.x - b.min.x > 0.05f && b.max.y - b.min.y > 0.05f &&
            b.max.z - b.min.z > 0.05f && b.max.x - b.min.x < 6.0f && b.max.z - b.min.z < 6.0f)
            shapes.push_back(b);
    }
    if (shapes.empty()) {
        I3D_bbox b;
        b.min = S_vector(-0.2f, -0.1f, -0.3f);
        b.max = S_vector(0.2f, 0.1f, 0.3f);
        shapes.push_back(b);
    }
    int seed = static_cast<int>(g_frames.size()) * 13 + static_cast<int>(shapes.size());
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    int frameNo = 0;
    // An item frame of shape `k` at `pos`, tilted, and its twin.
    const auto frames = [&](int k, const S_vector &pos, I3D_frame *&f, I3D_frame *&twinFrame) {
        const I3D_bbox &b = shapes[static_cast<size_t>(k) % shapes.size()];
        char name[32];
        std::snprintf(name, sizeof name, "it.f%d", frameNo);
        f = RbBoxFrame(nullptr, name, b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z, pos);
        std::snprintf(name, sizeof name, "it.twin%d", frameNo++);
        twinFrame = RbBoxFrame(nullptr, name, b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z, pos);
        if (k % 2) {
            RbTilt(f, 0.8f, 0.6f);
            RbTilt(twinFrame, 0.8f, 0.6f);
        }
    };
    C_ItTestDef def;
    def.m_textId = 0x1bf0;
    def.m_category = 7;
    const auto newItem = [&]() {
        C_Item *item = new C_FakeItem();
        item->m_def = &def;
        return item;
    };
    C_ActorCreator *itemCreator = g_actorCreators[ACTOR_ITEM];
    C_ActorCreator *debrisCreator = g_actorCreators[ACTOR_DEBRIS];
    // An item actor on a new frame of shape `k` with a new item, set up (role `role`) moving at `v`,
    // and its twin body.
    struct S_ItSet {
        C_ItemActor *actor;
        C_Item *item;
        I3D_frame *frame, *twinFrame;
    };
    const auto newActor = [&](int k, const S_vector &pos, const S_vector &v, C_RigidBody &twin) {
        S_ItSet s;
        frames(k, pos, s.frame, s.twinFrame);
        s.actor = static_cast<C_ItemActor *>(itemCreator->Create());
        s.actor->SetFrame(s.frame);
        s.actor->m_netId = 40 + k;
        s.item = newItem();
        g_itItemFrame = s.frame;
        s.actor->SetupItem(s.item, v);
        ItTwin(twin, s.twinFrame, v, s.actor);
        return s;
    };
    const auto done = [&](S_ItSet &s) {
        itemCreator->Destroy(s.actor);
        delete s.item;
    };
    g_itemWeight = rnd(0.5f, 6.0f);
    g_itemSize = 1;
    def.behaviour = 0;
    g_coop = false;

    // --- The creators: their names and actors (the items' empty slots, the defaults).
    {
        IT(r.creator, itemCreator && std::strcmp(itemCreator->GetName(), "Item") == 0 && debrisCreator &&
                          std::strcmp(debrisCreator->GetName(), "Debris") == 0);
        C_chunk none;
        g_itLog.clear();
        C_Actor *a = itemCreator->Create();
        auto *it = dynamic_cast<C_ItemActor *>(a);
        IT(r.creator, it && a->GetType() == ACTOR_ITEM && it->m_item == nullptr && a->GetMaterialId(nullptr) == 11 &&
                          !a->WriteProperties(none) && !a->ReadProperties(none) &&
                          std::strcmp(*a->GetName(), "Missing actor name!!") == 0);
        a->CopyFrom(a);
        a->RestartMission(true);
        a->Unknown20(nullptr);
        a->WriteState(nullptr);
        a->ReadState(nullptr);
        IT(r.creator, g_itLog.empty() && it->m_item == nullptr && it->m_lifeTime == -1 && it->m_respawnTimer == -1 &&
                          it->m_soundTimer == 0 && it->m_age == 0 && !it->m_fading);
        itemCreator->Destroy(a);
        itemCreator->Destroy(nullptr);
        C_Actor *d = debrisCreator->Create();
        auto *debris = dynamic_cast<C_Debris *>(d);
        IT(r.creator, debris && d->GetType() == ACTOR_DEBRIS && d->GetMaterialId(nullptr) == 11 &&
                          !d->WriteProperties(none) && !d->ReadProperties(none) && !d->SaveGame(none) &&
                          !d->LoadGame(none) && debris->m_lifeTime == 0 && !debris->m_fading && !debris->m_fadeOut);
        d->CopyFrom(d);
        d->RestartMission(true);
        d->Tick(50);
        d->OnMessage(nullptr);
        d->Unknown20(nullptr);
        IT(r.creator, g_itLog.empty());
        debrisCreator->Destroy(d);
        debrisCreator->Destroy(nullptr);
    }

    // --- SetFrame: the item forgotten, the model fully opaque, then the base's.
    {
        I3D_frame *f, *tf;
        frames(0, S_vector(0, 2, 0), f, tf);
        auto *it = static_cast<C_ItemActor *>(itemCreator->Create());
        C_Item *item = newItem();
        it->m_item = item;
        const int refs = f->m_refCount;
        const int inits = g_actorInits;
        g_itLog.clear();
        IT(r.frame, it->SetFrame(f) && it->m_item == nullptr && it->m_frame == f && f->m_refCount == refs + 1 &&
                        g_actorInits == inits + 1 && ItLogIs({"alpha " + ItName(f) + " 1"}));
        itemCreator->Destroy(it);
        delete item;
    }

    // --- SetupItem: the body (the twin's), the counters, the name; the life by the role.
    {
        const S_vector v(rnd(-2, 2), rnd(0, 3), rnd(-2, 2));
        g_gameRole = GAME_ROLE_SERVER;
        for (int k = 0; k < 4; ++k) {
            g_itemSize = k;                                 // (under 2: the doubled rest volume)
            I3D_frame *f, *tf;
            frames(k, S_vector(rnd(-5, 5), rnd(1, 4), rnd(-5, 5)), f, tf);
            auto *it = static_cast<C_ItemActor *>(itemCreator->Create());
            it->SetFrame(f);
            C_Item *item = newItem();
            g_itItemFrame = f;
            it->m_soundTimer = 9;
            it->m_respawnTimer = 9;
            it->m_age = 9;
            it->m_fading = true;
            def.m_textId = k == 2 ? 77 : 0x1bf0;
            g_itLog.clear();
            it->SetupItem(item, v);
            C_RigidBody twin;
            ItTwin(twin, tf, v, it);
            IT(r.setup, it->m_item == item && it->GetItem() == item && ItSameBody(it->m_body, twin) && it->m_body.m_frame == f &&
                            it->m_body.m_callback == &C_ItemActor::OnContact && it->m_body.m_context == it &&
                            it->m_body.m_owner == it && it->m_soundTimer == 0 && it->m_respawnTimer == -1 &&
                            it->m_age == 0 && it->m_fading && ItLogIs({"itemframe 0"}) &&
                            std::strcmp(*it->GetName(), k == 2 ? "other text" : "Car (text 0x1bf0)") == 0);
            itemCreator->Destroy(it);
            delete item;
        }
        g_itemSize = 1;
        def.m_textId = 0x1bf0;
        // Without an item: only the item is set.
        {
            auto *it = static_cast<C_ItemActor *>(itemCreator->Create());
            it->m_item = reinterpret_cast<C_Item *>(&def);
            it->m_lifeTime = 5;
            it->m_soundTimer = 6;
            it->m_respawnTimer = 7;
            it->m_age = 8;
            g_itLog.clear();
            it->SetupItem(nullptr, v);
            IT(r.setup, it->m_item == nullptr && it->m_lifeTime == 5 && it->m_soundTimer == 6 && it->m_respawnTimer == 7 &&
                            it->m_age == 8 && g_itLog.empty() && it->m_body.m_frame == nullptr &&
                            std::strcmp(*it->GetName(), "Missing actor name!!") == 0);
            itemCreator->Destroy(it);
        }
        // The life: 30 to 40 s on a multiplayer server (not co-op, not the pick-ups), else none (and
        // no random number drawn).
        struct S_LifeCase {
            uint8_t role;
            bool coop;
            int behaviour;
            bool lives;
        };
        const S_LifeCase cases[] = {{GAME_ROLE_SERVER, false, 0, true},  {GAME_ROLE_SERVER, true, 0, false},
                                    {GAME_ROLE_SERVER, false, 20, false}, {GAME_ROLE_SERVER, false, 21, false},
                                    {GAME_ROLE_SERVER, false, 19, true}, {3, false, 0, true},
                                    {GAME_ROLE_CLIENT, false, 0, false},  {GAME_ROLE_LOCAL, false, 0, false},
                                    {GAME_ROLE_LOCAL, true, 20, false}};
        int n = 0;
        for (const S_LifeCase &c : cases) {
            g_gameRole = c.role;
            g_coop = c.coop;
            def.behaviour = c.behaviour;
            const unsigned s = 1000u + static_cast<unsigned>(seed & 0xFFF) + static_cast<unsigned>(n++);
            std::srand(s);
            const int expected = static_cast<int>(RandFloat() * 10000.0f + 30000.0f);
            std::srand(s);
            C_RigidBody twin;
            S_ItSet set = newActor(n, S_vector(0, 3, 0), v, twin);
            const int next = std::rand();
            std::srand(s);
            const int first = std::rand();
            IT(r.life, c.lives ? set.actor->m_lifeTime == expected && expected >= 30000 && expected <= 40000 && next != first
                               : set.actor->m_lifeTime == -1 && next == first);
            done(set);
        }
        g_gameRole = GAME_ROLE_SERVER;
        g_coop = false;
        def.behaviour = 0;
    }

    // --- Update: the body every frame (with or without the item), the hit sound's pause to 0.
    {
        C_RigidBody twin;
        S_ItSet s = newActor(1, S_vector(1, 5, -1), S_vector(rnd(-1, 1), 0, rnd(-1, 1)), twin);
        for (int dt : {16, 33, 50}) {
            s.actor->Update(dt);
            twin.Tick(static_cast<uint32_t>(dt));
            IT(r.update, ItSameBody(s.actor->m_body, twin));
        }
        s.actor->m_item = nullptr;
        s.actor->Update(20);
        twin.Tick(20);
        IT(r.update, ItSameBody(s.actor->m_body, twin) && s.actor->m_soundTimer == 0);
        s.actor->m_item = s.item;
        const int timers[][3] = {{30, 20, 10}, {10, 20, 0}, {20, 20, 0}, {0, 20, 0}, {2000, 1, 1999}, {-5, 20, 0}};
        for (const auto &t : timers) {
            s.actor->m_soundTimer = t[0];
            s.actor->Update(t[1]);
            twin.Tick(static_cast<uint32_t>(t[1]));
            IT(r.update, s.actor->m_soundTimer == t[2]);
        }
        done(s);
    }

    // --- Tick: the age, the body on a server only (not with a client side), the return to the
    // placement, the life's fade and end; nothing without the item.
    {
        const uint8_t roles[] = {GAME_ROLE_SERVER, GAME_ROLE_CLIENT, 3, GAME_ROLE_LOCAL};
        for (uint8_t role : roles) {
            g_gameRole = role;
            C_RigidBody twin;
            S_ItSet s = newActor(2, S_vector(-2, 6, 3), S_vector(rnd(-1, 1), 1, 0), twin);
            s.actor->m_lifeTime = -1;
            s.actor->m_age = 100;
            g_itLog.clear();
            s.actor->Tick(30);
            if (!(role & GAME_ROLE_CLIENT))
                twin.Tick(30);
            IT(r.tick, s.actor->m_age == 130 && ItSameBody(s.actor->m_body, twin) && ItNoLog());
            // Without the item: nothing.
            s.actor->m_item = nullptr;
            s.actor->m_respawnTimer = 5;
            s.actor->m_lifeTime = 5;
            s.actor->Tick(30);
            IT(r.tick, s.actor->m_age == 130 && ItSameBody(s.actor->m_body, twin) && s.actor->m_respawnTimer == 5 &&
                           s.actor->m_lifeTime == 5 && ItNoLog());
            s.actor->m_item = s.item;
            done(s);
        }
        g_gameRole = GAME_ROLE_SERVER;
        // The return: counted down, at under 1 Respawn (logged by its message), then never again.
        {
            C_RigidBody twin;
            S_ItSet s = newActor(0, S_vector(0, 2, 0), S_vector(0, 0, 0), twin);
            S_ItemPlacement place;
            place.item = s.item;
            place.pos = {3, 1, 4, 1};
            place.rot = S_quat(1, 0, 0, 0);
            g_temporaryActors.m_respawnItems.assign(1, place);
            s.actor->m_lifeTime = -1;
            s.actor->SetRespawnTime(50);
            g_itLog.clear();
            s.actor->Tick(30);
            IT(r.tick, s.actor->m_respawnTimer == 20 && ItNoLog());
            s.actor->Tick(19);
            IT(r.tick, s.actor->m_respawnTimer == 1 && ItNoLog());
            s.actor->Tick(1);
            IT(r.tick, s.actor->m_respawnTimer == -1 && !ItNoLog() && g_itLog[0] == "msg 40 1");
            g_itLog.clear();
            s.actor->Tick(500);
            IT(r.tick, s.actor->m_respawnTimer == -1 && ItNoLog());
            g_temporaryActors.m_respawnItems.clear();
            done(s);
        }
        // The life: the fade-out at 500 ms left (once), the end under 1 ms: the model off, the clients
        // told, the actor destroyed (not with a client side: the message does it).
        for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_SERVER, uint8_t(3)}) {
            g_gameRole = role;
            C_RigidBody twin;
            S_ItSet s = newActor(3, S_vector(0, 2, 0), S_vector(0, 0, 0), twin);
            const std::string f = ItName(s.frame);
            s.actor->m_lifeTime = 600;
            s.actor->m_fading = false;
            g_itLog.clear();
            s.actor->Tick(99);
            IT(r.tick, s.actor->m_lifeTime == 501 && ItNoLog() && !s.actor->m_fading);
            s.actor->Tick(1);
            IT(r.tick, s.actor->m_lifeTime == 500 && s.actor->m_fading && ItLogIsAfterBody({"fade " + f + " 500 0"}));
            g_itLog.clear();
            s.actor->Tick(100);
            s.actor->Tick(399);
            IT(r.tick, s.actor->m_lifeTime == 1 && ItNoLog());
            g_itTemps.clear();
            g_itDestroyed.clear();
            s.actor->Tick(1);
            if (role == GAME_ROLE_SERVER)
                IT(r.tick, ItLogIsAfterBody({"clear " + f, "on " + f + " 0", "update " + f, "msg 43 1", "send", "destroy"}) &&
                               g_itDestroyed.size() == 1 && g_itDestroyed[0] == s.actor);
            else
                IT(r.tick, ItLogIsAfterBody({"clear " + f, "on " + f + " 0", "update " + f, "msg 43 1", "send"}));
            IT(r.tick, s.actor->m_lifeTime == -1 && ItSent({{0, 3}}, 43));
            // The fade-out over the time left.
            s.actor->m_lifeTime = 600;
            s.actor->m_fading = false;
            g_itLog.clear();
            s.actor->Tick(150);
            IT(r.tick, s.actor->m_lifeTime == 450 && s.actor->m_fading && ItLogIsAfterBody({"fade " + f + " 450 0"}));
            // A fade-out under way is not started again; the end comes before it.
            s.actor->m_lifeTime = 300;
            s.actor->m_fading = true;
            g_itLog.clear();
            s.actor->Tick(10);
            IT(r.tick, ItNoLog() && s.actor->m_lifeTime == 290);
            s.actor->m_lifeTime = 400;
            s.actor->m_fading = false;
            s.actor->Tick(400);
            ItDropBody();
            IT(r.tick, !s.actor->m_fading && s.actor->m_lifeTime == -1 && !ItNoLog() && g_itLog[0] == "clear " + f);
            // Both at once: the return first.
            S_ItemPlacement place;
            place.item = s.item;
            place.pos = {1, 1, 1, 1};
            place.rot = S_quat(1, 0, 0, 0);
            g_temporaryActors.m_respawnItems.assign(1, place);
            s.actor->m_respawnTimer = 10;
            s.actor->m_lifeTime = 10;
            g_itLog.clear();
            s.actor->Tick(10);
            ItDropBody();
            IT(r.tick, g_itLog.size() > 2 && g_itLog[0] == "msg 43 1" &&
                           std::find(g_itLog.begin(), g_itLog.end(), "clear " + f) != g_itLog.end() &&
                           std::find(g_itLog.begin(), g_itLog.end(), "clear " + f) >
                               std::find(g_itLog.begin(), g_itLog.end(), "send"));
            g_temporaryActors.m_respawnItems.clear();
            done(s);
        }
        g_gameRole = GAME_ROLE_SERVER;
    }

    // --- Hit: a push from 300 ms on (not the pick-ups), a bullet's by 0.05 per damage point.
    {
        struct S_HitCase {
            int age, behaviour, type;
            float amount;
            bool pushes;
        };
        const S_HitCase cases[] = {{299, 0, HIT_BULLET, 40, false}, {300, 0, HIT_BULLET, 40, true},
                                   {301, 0, 2, 40, true},           {5000, 20, HIT_BULLET, 40, false},
                                   {5000, 21, 3, 10, false},        {5000, 19, 0, 25, true},
                                   {400, 0, HIT_BULLET, -8, true}};
        int k = 0;
        for (const S_HitCase &c : cases) {
            C_RigidBody twin;
            S_ItSet s = newActor(k++, S_vector(rnd(-3, 3), 2, rnd(-3, 3)), S_vector(0, 0, 0), twin);
            def.behaviour = c.behaviour;
            s.actor->m_age = c.age;
            const S_vector pos(rnd(-3, 3), rnd(1, 3), rnd(-3, 3)), dir(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
            g_itLog.clear();
            g_itTemps.clear();
            s.actor->Hit(static_cast<E_HitType>(c.type), c.amount, pos, dir, nullptr, 0);
            S_vector impulse = dir;
            if (c.type == HIT_BULLET) {
                const float k5 = c.amount * 0.05f;
                impulse = S_vector(impulse.x * k5, impulse.y * k5, impulse.z * k5);
            }
            if (c.pushes) {
                twin.AddImpulse(impulse, {pos.x, pos.y, pos.z, 0.0f});
                T_ItBits bits = {{2, 3}};
                ItAdd(bits, pos, -1);
                ItAdd(bits, ItNormalize(impulse), -2);
                bits.emplace_back(ItF(ItLength(impulse)), 32);
                IT(r.hit, ItSameBody(s.actor->m_body, twin) && ItLogIs({"msg " + std::to_string(s.actor->m_netId) + " 1", "send"}) &&
                              ItSent(bits, s.actor->m_netId));
            } else {
                IT(r.hit, ItSameBody(s.actor->m_body, twin) && g_itLog.empty());
            }
            done(s);
        }
        def.behaviour = 0;
    }

    // --- Push: the impulse at the point, then (not in a local game) its point, direction and length
    // to the clients; without the item nothing. The direction's edge cases.
    {
        const S_vector impulses[] = {S_vector(3, -4, 12),     S_vector(0.6f, 0.8f, 0),  S_vector(0, 0, 0),
                                     S_vector(1e-5f, 0, 0),   S_vector(-1e-5f, 0, 2e-5f), S_vector(0, 0, -3e-5f),
                                     S_vector(0, -1e-5f, 0),  S_vector(0, 1, 1e-9f),    S_vector(rnd(-9, 9), rnd(-9, 9), rnd(-9, 9))};
        int k = 0;
        for (const S_vector &impulse : impulses) {
            for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_SERVER, GAME_ROLE_LOCAL, GAME_ROLE_CLIENT}) {
                g_gameRole = role;
                C_RigidBody twin;
                S_ItSet s = newActor(k++, S_vector(0, 3, 0), S_vector(0, 0, 0), twin);
                const S_vector point(rnd(-1, 1), rnd(2, 4), rnd(-1, 1));
                g_itLog.clear();
                g_itTemps.clear();
                s.actor->Push(impulse, point);
                twin.AddImpulse(impulse, {point.x, point.y, point.z, 0.0f});
                if (role == GAME_ROLE_LOCAL) {
                    IT(r.push, ItSameBody(s.actor->m_body, twin) && g_itLog.empty());
                } else {
                    T_ItBits bits = {{2, 3}};
                    ItAdd(bits, point, -1);
                    ItAdd(bits, ItNormalize(impulse), -2);
                    bits.emplace_back(ItF(ItLength(impulse)), 32);
                    IT(r.push, ItSameBody(s.actor->m_body, twin) && ItSent(bits, s.actor->m_netId) &&
                                   ItLogIs({"msg " + std::to_string(s.actor->m_netId) + " 1", "send"}));
                }
                // Without the item.
                s.actor->m_item = nullptr;
                g_itLog.clear();
                s.actor->Push(impulse, point);
                IT(r.push, ItSameBody(s.actor->m_body, twin) && g_itLog.empty());
                s.actor->m_item = s.item;
                // OnPush: the clients' side.
                s.actor->OnPush(impulse, point);
                if (!(role & GAME_ROLE_SERVER))
                    twin.AddImpulse(impulse, {point.x, point.y, point.z, 0.0f});
                IT(r.push, ItSameBody(s.actor->m_body, twin));
                done(s);
            }
        }
        g_gameRole = GAME_ROLE_SERVER;
    }

    // --- The body's callback: a human's volume refused; another contact sounds (clients) and
    // counts; at rest the server tells where it lies.
    {
        C_RigidBody twin;
        S_ItSet s = newActor(1, S_vector(0, 2, 0), S_vector(0, 0, 0), twin);
        S_CollisionVolume volume;
        S_RigidBodyContact contact = {};
        contact.volume = &volume;
        struct S_Case {
            uint32_t flags, type;
            uint8_t role;
            bool context;
            int answer;
            bool sound;
        };
        const S_Case cases[] = {
            {COLLISION_FLAG_DYNAMIC, 0x100, GAME_ROLE_CLIENT, true, 1, false},
            {COLLISION_FLAG_DYNAMIC | 0x05000000, 0x10000, GAME_ROLE_LOCAL, true, 1, false},
            {COLLISION_FLAG_DYNAMIC, 0x10102, GAME_ROLE_CLIENT, true, 1, false},
            {COLLISION_FLAG_DYNAMIC, 0x2, GAME_ROLE_CLIENT, true, 2, true},
            {COLLISION_FLAG_DYNAMIC, 0x20000, GAME_ROLE_LOCAL, true, 2, true},
            {0x07000000, 0x100, GAME_ROLE_CLIENT, true, 2, true},
            {0x07000000 | COLLISION_FLAG_PRIMITIVE, 0x10000, 3, true, 2, true},
            {0x07000000, 0, GAME_ROLE_SERVER, true, 2, false},
            {0x07000000, 0, GAME_ROLE_CLIENT, false, 2, false},
        };
        for (const S_Case &c : cases) {
            volume.flags = c.flags;
            volume.type = c.type;
            g_gameRole = c.role;
            s.actor->m_soundTimer = 0;
            g_soundRequests.clear();
            g_itLog.clear();
            const int answer = C_ItemActor::OnContact(RIGID_BODY_EVENT_CONTACT, 0, c.context ? s.actor : nullptr, &contact);
            IT(r.contact, answer == c.answer && g_soundRequests.size() == (c.sound ? 1u : 0u) && g_itLog.empty());
        }
        // At rest: a multiplayer server sends the placement.
        for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_SERVER, uint8_t(3), GAME_ROLE_CLIENT, GAME_ROLE_LOCAL}) {
            g_gameRole = role;
            g_itLog.clear();
            const int answer = C_ItemActor::OnContact(RIGID_BODY_EVENT_REST, 0, s.actor, &contact);
            const bool sends = (role & GAME_ROLE_SERVER) && role != GAME_ROLE_LOCAL;
            IT(r.contact, answer == 0 && (sends ? ItLogIs({"msg 41 1", "send"}) : g_itLog.empty()));
        }
        // Other events: nothing.
        g_gameRole = GAME_ROLE_SERVER;
        g_itLog.clear();
        g_soundRequests.clear();
        IT(r.contact, C_ItemActor::OnContact(2, 0, s.actor, &contact) == 0 &&
                          C_ItemActor::OnContact(-1, 0, s.actor, &contact) == 0 && g_itLog.empty() && g_soundRequests.empty());
        done(s);
    }

    // --- The hit sound: the item's category as the group of chapter 5 (silent to the AI) at the
    // model, then 2 s quiet; not under way, nor for categories under 1, nor without the item.
    {
        C_RigidBody twin;
        S_ItSet s = newActor(2, S_vector(rnd(-3, 3), 2, rnd(-3, 3)), S_vector(0, 0, 0), twin);
        const struct {
            int timer, category;
            bool item, plays;
        } cases[] = {{0, 7, true, true}, {49, 3, true, true}, {50, 7, true, false}, {0, 0, true, false},
                     {0, -2, true, false}, {0, 7, false, false}, {-5, 1, true, true}};
        for (const auto &c : cases) {
            s.actor->m_soundTimer = c.timer;
            def.m_category = c.category;
            s.actor->m_item = c.item ? s.item : nullptr;
            g_soundRequests.clear();
            s.actor->PlayHitSound();
            if (c.plays) {
                const S_SoundRequest &q = g_soundRequests.empty() ? S_SoundRequest() : g_soundRequests[0];
                const S_matrix &w = s.frame->GetMatrix();
                S_SoundRequest e;
                e.pos = w(3);
                e.posW = w.m[3][3];
                e.flags = SOUND_SILENT_TO_AI;
                e.chapter = SOUND_CHAPTER_FALLING_HIT;
                e.group = c.category;
                IT(r.sound, g_soundRequests.size() == 1 && std::memcmp(&q, &e, sizeof q) == 0 &&
                                s.actor->m_soundTimer == 2000);
            } else {
                IT(r.sound, g_soundRequests.empty() && s.actor->m_soundTimer == c.timer);
            }
        }
        s.actor->m_item = s.item;
        def.m_category = 7;
        done(s);
    }

    // --- The messages: removed, placed, pushed, moved onto a frame (the clients' side); other
    // kinds nothing.
    {
        for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_CLIENT, GAME_ROLE_SERVER, uint8_t(3)}) {
            g_gameRole = role;
            const bool client = !(role & GAME_ROLE_SERVER);
            // Removed.
            {
                C_RigidBody twin;
                S_ItSet s = newActor(0, S_vector(0, 2, 0), S_vector(0, 0, 0), twin);
                const std::string f = ItName(s.frame);
                ItRead({{0, 3}});
                g_itLog.clear();
                g_itDestroyed.clear();
                s.actor->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
                IT(r.message, ItReadAll() && ItLogIs({"clear " + f, "on " + f + " 0", "update " + f, "destroy"}) &&
                                  g_itDestroyed.size() == 1 && g_itDestroyed[0] == s.actor);
                done(s);
            }
            // Placed: the frame there, the body anew at rest.
            {
                C_RigidBody twin;
                S_ItSet s = newActor(1, S_vector(0, 2, 0), S_vector(1, 0, 0), twin);
                const std::string f = ItName(s.frame);
                const S_vector pos(rnd(-5, 5), rnd(0, 2), rnd(-5, 5));
                const S_vector axis = S_vector(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)).Normalized();
                const float angle = rnd(-3, 3);
                T_ItBits bits = {{1, 3}};
                ItAdd(bits, pos, -1);
                ItAdd(bits, axis, -4);
                bits.emplace_back(ItF(angle), -5);
                ItRead(bits);
                g_itLog.clear();
                g_volumeLog.clear();
                s.actor->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
                if (client) {
                    S_quat q;
                    q.Make(axis, angle);
                    F_SetPosRot(s.twinFrame, pos, q);
                    C_RigidBody fresh;
                    ItTwin(fresh, s.twinFrame, S_vector(0, 0, 0), s.actor);
                    char text[160];
                    std::snprintf(text, sizeof text, "posrot %s %s %.9g %.9g %.9g %.9g", f.c_str(), ItV(pos).c_str(), q.s,
                                  q.v.x, q.v.y, q.v.z);
                    IT(r.message, ItReadAll() && ItLogIs({text, "update " + f, "itemframe 0"}) &&
                                      ItSameBody(s.actor->m_body, fresh) && ItVolumeOps(s.actor->m_body) == "R" + ItVolumeOps(fresh));
                } else {
                    IT(r.message, ItReadAll() && g_itLog.empty() && ItSameBody(s.actor->m_body, twin));
                }
                done(s);
            }
            // Pushed: the direction times the length.
            {
                C_RigidBody twin;
                S_ItSet s = newActor(2, S_vector(0, 2, 0), S_vector(0, 0, 0), twin);
                const S_vector point(rnd(-1, 1), rnd(1, 3), rnd(-1, 1));
                const S_vector dir = S_vector(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)).Normalized();
                const float length = rnd(0, 40);
                T_ItBits bits = {{2, 3}};
                ItAdd(bits, point, -1);
                ItAdd(bits, dir, -2);
                bits.emplace_back(ItF(length), 32);
                ItRead(bits);
                g_itLog.clear();
                s.actor->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
                if (client)
                    twin.AddImpulse(S_vector(dir.x * length, dir.y * length, dir.z * length), {point.x, point.y, point.z, 0});
                IT(r.message, ItReadAll() && g_itLog.empty() && ItSameBody(s.actor->m_body, twin));
                done(s);
            }
            // Moved onto the frame named (when it is found).
            for (bool found : {true, false}) {
                C_RigidBody twin;
                S_ItSet s = newActor(3, S_vector(0, 2, 0), S_vector(0, 0, 0), twin);
                I3D_frame *target = NewFrame(FRAME_DUMMY, "it.dest", nullptr);
                target->m_localMatrix.m[3][0] = 7.0f;
                g_itFound = found ? target : nullptr;
                g_itStrings.assign(1, "it.dest");
                ItRead({{3, 3}, {0, -6}});
                g_itLog.clear();
                s.actor->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
                const bool attached = !g_itLog.empty() && std::find(g_itLog.begin(), g_itLog.end(), "link " + ItName(s.frame) + " it.dest 1") != g_itLog.end();
                IT(r.message, ItReadAll() && g_itLog.size() >= 2 && g_itLog[0] == "readstring 127" &&
                                  g_itLog[1] == "find it.dest ffff" && attached == (found && client) &&
                                  (attached || g_itLog.size() == 2));
                g_itFound = nullptr;
                done(s);
            }
            // Other kinds.
            {
                C_RigidBody twin;
                S_ItSet s = newActor(0, S_vector(0, 2, 0), S_vector(0, 0, 0), twin);
                for (uint32_t kind : {4u, 5u, 7u}) {
                    ItRead({{kind, 3}});
                    g_itLog.clear();
                    s.actor->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
                    IT(r.message, ItReadAll() && g_itLog.empty() && ItSameBody(s.actor->m_body, twin));
                }
                done(s);
            }
        }
        g_gameRole = GAME_ROLE_SERVER;
    }

    // --- The mission: activated (the fade-out forgotten); stopped: the body, the frame let go, the
    // item destroyed, deactivated; ReleaseItem: the frame's data freed, the frame and item let go.
    {
        C_RigidBody twin;
        S_ItSet s = newActor(1, S_vector(0, 2, 0), S_vector(0, 0, 0), twin);
        const std::string f = ItName(s.frame);
        s.actor->m_fading = true;
        g_activations.clear();
        g_deactivations.clear();
        g_itLog.clear();
        s.actor->StartMission(false);
        IT(r.mission, g_activations == std::vector<bool>{false} && !s.actor->m_fading && g_itLog.empty());
        s.actor->StartMission(true);
        IT(r.mission, g_activations == std::vector<bool>({false, true}));
        const int refs = s.frame->m_refCount;
        g_itDestroyedItems.clear();
        g_volumeLog.clear();
        s.actor->StopMission(true);
        IT(r.mission, ItVolumeOps(s.actor->m_body) == "R" && ItLogIs({"release " + f, "destroyitem"}) && g_itDestroyedItems == std::vector<C_Item *>{s.item} &&
                          s.actor->m_item == nullptr && s.actor->m_frame == nullptr && s.frame->m_refCount == refs - 1 &&
                          g_deactivations == std::vector<bool>{true});
        // Again: nothing left to let go.
        g_itLog.clear();
        s.actor->StopMission(false);
        IT(r.mission, g_itLog.empty() && g_deactivations == std::vector<bool>({true, false}));
        itemCreator->Destroy(s.actor);
        // ReleaseItem.
        C_RigidBody twin2;
        S_ItSet t = newActor(2, S_vector(0, 2, 0), S_vector(0, 0, 0), twin2);
        const int refs2 = t.frame->m_refCount;
        g_itLog.clear();
        t.actor->ReleaseItem();
        IT(r.mission, ItLogIs({"freedata " + ItName(t.frame), "release " + ItName(t.frame)}) && t.actor->m_item == nullptr &&
                          t.actor->m_frame == nullptr && t.frame->m_refCount == refs2 - 1);
        done(t);
    }

    // --- The saved game: chunk 1 (the life, the fade-out flag as 4 bytes, the world position, the
    // rotation scalar first), chunk 3 (the body's), no actor chunk; loaded back onto a frame elsewhere
    // (put into the sector there), the return forgotten. A chunk of another id is entered.
    {
        g_gameRole = GAME_ROLE_SERVER;
        C_RigidBody twin;
        S_ItSet s = newActor(1, S_vector(rnd(-4, 4), rnd(1, 3), rnd(-4, 4)), S_vector(rnd(-1, 1), 0, rnd(-1, 1)), twin);
        for (int t = 0; t < 3; ++t) {
            s.actor->Update(20);
            twin.Tick(20);
        }
        s.actor->m_lifeTime = 12345;
        s.actor->m_fading = true;
        const size_t loads = g_actorLoads.size();
        C_chunk w;
        w.Open("it.sav", true);
        w.Begin(0x777);
        IT(r.save, s.actor->SaveGame(w));
        w.Ascend();
        w.Close();
        // The twin body's save, for chunk 3's payload.
        C_chunk tw;
        tw.Open("it.twin", true);
        twin.SaveGame(tw);
        tw.Close();
        const std::vector<uint8_t> &data = g_files["it.sav"];
        const std::vector<uint8_t> &body = g_files["it.twin"];
        std::vector<uint8_t> e;
        const auto put = [&](const void *p, size_t n) {
            const auto *b = static_cast<const uint8_t *>(p);
            e.insert(e.end(), b, b + n);
        };
        const auto header = [&](uint16_t id, uint32_t size) {
            put(&id, 2);
            put(&size, 4);
        };
        header(0x777, static_cast<uint32_t>(6 + 6 + 36 + 6 + body.size()));
        header(1, 6 + 36);
        const int life = 12345, fading = 1;
        put(&life, 4);
        put(&fading, 4);
        const S_matrix &m = s.frame->GetMatrix();
        put(&m.m[3][0], 12);
        S_quat q;
        q.Make(m);
        float file[4];
        q.ToFile(file);
        put(file, 16);
        header(3, static_cast<uint32_t>(6 + body.size()));
        put(body.data(), body.size());
        IT(r.save, data == e);

        // Loaded onto another actor's frame.
        C_RigidBody twin2;
        S_ItSet t = newActor(2, S_vector(9, 9, 9), S_vector(0, 0, 0), twin2);
        const std::string f = ItName(t.frame);
        t.actor->m_respawnTimer = 77;
        t.actor->m_lifeTime = 5;
        t.actor->m_fading = false;
        C_chunk rd;
        rd.Open("it.sav", false);
        uint16_t id;
        rd.Descend(id);
        g_itLog.clear();
        IT(r.save, t.actor->LoadGame(rd));
        // (The last update is the body's LoadGame's; the rotation is checked below, without a body.)
        const S_vector at(m.m[3][0], m.m[3][1], m.m[3][2]);
        IT(r.save, t.actor->m_lifeTime == 12345 && t.actor->m_fading && t.actor->m_respawnTimer == -1 &&
                       ItLogIs({"link " + f + " 0 0", "update " + f, "getsector " + ItV(at), "link " + f + " sector 5",
                                "update " + f}) &&
                       t.frame->GetParent() == g_sector && std::memcmp(&t.frame->m_localMatrix.m[3][0], &at, 12) == 0 &&
                       g_actorLoads.size() == loads &&
                       std::memcmp(&t.actor->m_body.m_velocity, &twin.m_velocity, 12) == 0 &&
                       std::memcmp(&t.actor->m_body.m_spin, &twin.m_spin, 12) == 0 && rd.RemainingSize() == 0);
        rd.Close();
        F_LinkTo(t.frame, nullptr, 0);                  // (out of the shared sector before the model goes)
        done(t);
        // The body's chunk first: each chunk is left after it is read.
        {
            std::vector<uint8_t> &x = g_files["it.swapped"];
            const size_t state = 6 + 6 + 36;           // (the end of chunk 1 in the saved file)
            x.assign(e.begin(), e.begin() + 6);
            x.insert(x.end(), e.begin() + static_cast<std::ptrdiff_t>(state), e.end());
            x.insert(x.end(), e.begin() + 6, e.begin() + static_cast<std::ptrdiff_t>(state));
            C_RigidBody twin4;
            S_ItSet v = newActor(0, S_vector(5, 5, 5), S_vector(0, 0, 0), twin4);
            v.actor->m_lifeTime = 5;
            C_chunk rd3;
            rd3.Open("it.swapped", false);
            rd3.Descend(id);
            IT(r.save, v.actor->LoadGame(rd3) && v.actor->m_lifeTime == 12345 &&
                           std::memcmp(&v.actor->m_body.m_velocity, &twin.m_velocity, 12) == 0 && rd3.RemainingSize() == 0);
            rd3.Close();
            F_LinkTo(v.frame, nullptr, 0);
            done(v);
        }
        // The fade-out flag of 0 and other values; a chunk of another id entered (its chunk 1 read).
        {
            std::vector<uint8_t> &x = g_files["it.odd"];
            x.clear();
            const auto add = [&](const void *p, size_t n) {
                const auto *b = static_cast<const uint8_t *>(p);
                x.insert(x.end(), b, b + n);
            };
            const auto head = [&](uint16_t hid, uint32_t size) {
                add(&hid, 2);
                add(&size, 4);
            };
            const int oddLife = 999, oddFading = 2;
            const float pos[3] = {1, 2, 3}, rot[4] = {0, 0, 1, 0};
            head(0x777, 6 + 6 + 6 + 36);
            head(9, 6 + 6 + 36);
            head(1, 6 + 36);
            add(&oddLife, 4);
            add(&oddFading, 4);
            add(pos, 12);
            add(rot, 16);
            C_RigidBody twin3;
            S_ItSet u = newActor(3, S_vector(0, 2, 0), S_vector(0, 0, 0), twin3);
            C_chunk rd2;
            rd2.Open("it.odd", false);
            rd2.Descend(id);
            u.actor->m_fading = false;
            g_itLog.clear();
            const std::string uf = ItName(u.frame);
            IT(r.save, u.actor->LoadGame(rd2) && u.actor->m_lifeTime == 999 && u.actor->m_fading &&
                           u.frame->m_localMatrix.m[3][0] == 1.0f && u.frame->m_localMatrix.m[3][1] == 2.0f &&
                           u.frame->m_localMatrix.m[3][2] == 3.0f && u.frame->m_rot.s == 0.0f && u.frame->m_rot.v.x == 0.0f &&
                           u.frame->m_rot.v.y == 1.0f && u.frame->m_rot.v.z == 0.0f &&
                           ItLogIs({"link " + uf + " 0 0", "update " + uf, "getsector 1 2 3", "link " + uf + " sector 5"}));
            rd2.Close();
            F_LinkTo(u.frame, nullptr, 0);
            done(u);
        }
        done(s);
    }

    // --- TeleportTo: onto the target's origin, facing z, back into the world there (its sector),
    // the actor's place and direction, the body anew; the clients told the target's name (not in a
    // local game). Without a frame only the message. OnTeleport: the clients'.
    {
        for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_SERVER, GAME_ROLE_LOCAL}) {
            g_gameRole = role;
            C_RigidBody twin;
            S_ItSet s = newActor(1, S_vector(1, 2, 3), S_vector(1, 0, 0), twin);
            const std::string f = ItName(s.frame);
            I3D_frame *target = NewFrame(FRAME_DUMMY, "it.target", nullptr);
            target->m_name = Stub(target).name.c_str();
            RbTilt(target, 0.6f, 0.8f, true);
            target->m_localMatrix.m[3][0] = rnd(-9, 9);
            target->m_localMatrix.m[3][1] = rnd(0, 3);
            target->m_localMatrix.m[3][2] = rnd(-9, 9);
            // The oracle: the same moves on the twin's frame (under the target keeping its place, at
            // the target's origin, out again keeping its place).
            F_LinkTo(s.twinFrame, target, 0);
            s.twinFrame->m_localMatrix = s.frame->GetMatrix() * RbInverse(target->GetMatrix());
            s.twinFrame->m_localMatrix(3) = S_vector(0, 0, 0);
            const S_matrix world = s.twinFrame->GetMatrix();
            F_LinkTo(s.twinFrame, nullptr, 0);
            s.twinFrame->m_localMatrix = world;
            C_RigidBody fresh;
            ItTwin(fresh, s.twinFrame, S_vector(0, 0, 0), s.actor);
            g_itLog.clear();
            g_itTemps.clear();
            s.actor->TeleportTo(target);
            std::vector<std::string> expected = {"link " + f + " it.target 1", "dir " + f + " 0 0 1 0", "update " + f,
                                                 "link " + f + " 0 1", "sector " + f + " " + ItV(world(3)), "on " + f + " 1",
                                                 "update " + f, "itemframe 0"};
            if (role != GAME_ROLE_LOCAL) {
                expected.push_back("msg 41 1");
                expected.push_back("send");
            }
            const S_vector dir = ItNormalize(world(2));
            IT(r.teleport, g_itLog == expected && s.frame->GetParent() == nullptr &&
                               std::memcmp(&s.frame->GetMatrix(), &world, sizeof world) == 0 &&
                               std::memcmp(&s.actor->m_position, &world.m[3][0], 16) == 0 &&
                               s.actor->m_direction.x == dir.x && s.actor->m_direction.y == dir.y &&
                               s.actor->m_direction.z == dir.z && s.actor->m_direction.w == world.m[2][3] &&
                               ItSameBody(s.actor->m_body, fresh) && (s.frame->m_frameFlags & 1u));
            if (role != GAME_ROLE_LOCAL)
                IT(r.teleport, ItSent({{3, 3}, {static_cast<uint32_t>(g_itStrings.size() - 1), -6}}, 41) &&
                                   g_itStrings.back() == "it.target");
            // Without a frame: only the message.
            I3D_frame *own = s.actor->m_frame;
            s.actor->m_frame = nullptr;
            g_itLog.clear();
            s.actor->TeleportTo(target);
            IT(r.teleport, role == GAME_ROLE_LOCAL ? g_itLog.empty() : ItLogIs({"msg 41 1", "send"}));
            s.actor->m_frame = own;
            // OnTeleport: on a client.
            for (uint8_t side : std::initializer_list<uint8_t>{GAME_ROLE_CLIENT, GAME_ROLE_SERVER, uint8_t(3)}) {
                g_gameRole = side;
                g_itLog.clear();
                s.actor->OnTeleport(target);
                IT(r.teleport, side == GAME_ROLE_CLIENT ? g_itLog.size() == 8 && g_itLog[0] == "link " + f + " it.target 1"
                                                        : g_itLog.empty());
            }
            done(s);
        }
        g_gameRole = GAME_ROLE_SERVER;
    }

    // --- SendPlacement: the world position and the rotation as axis and angle; without the item
    // nothing.
    {
        C_RigidBody twin;
        S_ItSet s = newActor(1, S_vector(rnd(-4, 4), rnd(1, 3), rnd(-4, 4)), S_vector(0, 0, 0), twin);
        RbTilt(s.frame, 0.28f, 0.96f, true);
        g_itLog.clear();
        g_itTemps.clear();
        s.actor->SendPlacement();
        const S_matrix &m = s.frame->GetMatrix();
        S_quat q;
        q.Make(m);
        S_vector4 axis = {};
        float angle;
        q.Inverse(reinterpret_cast<S_vector &>(axis), angle);
        T_ItBits bits = {{1, 3}};
        ItAdd(bits, m(3), -1);
        ItAdd(bits, S_vector(axis.x, axis.y, axis.z), -4);
        bits.emplace_back(ItF(angle), -5);
        IT(r.placement, ItLogIs({"msg 41 1", "send"}) && ItSent(bits, 41));
        s.actor->m_item = nullptr;
        g_itLog.clear();
        s.actor->SendPlacement();
        IT(r.placement, g_itLog.empty());
        s.actor->m_item = s.item;
        // OnPlacement on a client without the item: the frame placed, the body left alone.
        g_gameRole = GAME_ROLE_CLIENT;
        s.actor->m_item = nullptr;
        g_itLog.clear();
        g_volumeLog.clear();
        s.actor->OnPlacement(S_vector(1, 2, 3), S_vector(0, 1, 0), 1.0f);
        IT(r.placement, g_itLog.size() == 2 && g_itLog[1] == "update " + ItName(s.frame) && g_volumeLog.empty() &&
                            ItSameBody(s.actor->m_body, twin));
        s.actor->m_item = s.item;
        g_gameRole = GAME_ROLE_SERVER;
        // OnPlacement on a server: nothing.
        g_itLog.clear();
        s.actor->OnPlacement(S_vector(1, 2, 3), S_vector(0, 1, 0), 1.0f);
        IT(r.placement, g_itLog.empty());
        done(s);
    }

    // --- Respawn: back to the item's placement (the frame, the body anew), the clients told; not in
    // a local game, nor without a placement of the item.
    {
        for (uint8_t role : std::initializer_list<uint8_t>{GAME_ROLE_SERVER, uint8_t(3), GAME_ROLE_CLIENT, GAME_ROLE_LOCAL}) {
            g_gameRole = role;
            C_RigidBody twin;
            S_ItSet s = newActor(2, S_vector(0, 2, 0), S_vector(1, 1, 0), twin);
            const std::string f = ItName(s.frame);
            S_ItemPlacement other, own;
            other.item = reinterpret_cast<C_Item *>(&def);
            other.pos = {9, 9, 9, 1};
            other.rot = S_quat(1, 0, 0, 0);
            own.item = s.item;
            own.pos = {rnd(-5, 5), rnd(0, 2), rnd(-5, 5), 1};
            own.rot = S_quat(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
            own.rot.Normalize();
            // Not there: nothing.
            g_temporaryActors.m_respawnItems.assign(1, other);
            g_itLog.clear();
            s.actor->Respawn();
            IT(r.respawn, g_itLog.empty() && ItSameBody(s.actor->m_body, twin));
            g_temporaryActors.m_respawnItems.push_back(own);
            S_ItemPlacement again = own;                    // (a second of the item's: the first wins)
            again.pos = {-1, -2, -3, 1};
            g_temporaryActors.m_respawnItems.push_back(again);
            g_itLog.clear();
            g_itTemps.clear();
            g_volumeLog.clear();
            s.actor->Respawn();
            if (role == GAME_ROLE_LOCAL) {
                IT(r.respawn, g_itLog.empty() && ItSameBody(s.actor->m_body, twin));
            } else {
                const S_vector pos(own.pos.x, own.pos.y, own.pos.z);
                F_SetPosRot(s.twinFrame, pos, own.rot);
                C_RigidBody fresh;
                ItTwin(fresh, s.twinFrame, S_vector(0, 0, 0), s.actor);
                S_vector4 axis = {};
                float angle;
                own.rot.Inverse(reinterpret_cast<S_vector &>(axis), angle);
                T_ItBits bits = {{1, 3}};
                ItAdd(bits, pos, -1);
                ItAdd(bits, S_vector(axis.x, axis.y, axis.z), -4);
                bits.emplace_back(ItF(angle), -5);
                char text[160];
                std::snprintf(text, sizeof text, "posrot %s %s %.9g %.9g %.9g %.9g", f.c_str(), ItV(pos).c_str(),
                              own.rot.s, own.rot.v.x, own.rot.v.y, own.rot.v.z);
                IT(r.respawn, ItLogIs({"msg 42 1", text, "update " + f, "itemframe 0", "send"}) && ItSent(bits, 42) &&
                                  ItSameBody(s.actor->m_body, fresh) && ItVolumeOps(s.actor->m_body) == "R" + ItVolumeOps(fresh));
            }
            g_temporaryActors.m_respawnItems.clear();
            done(s);
        }
        g_gameRole = GAME_ROLE_SERVER;
    }

    // --- Debris: Setup / SetupWheel (the body as the parameters make it, a life of 30 to 40 s), the
    // mission's start and stop; the life in Update: the fade-out at 500 ms left and the removal at
    // the end with fade-out, else it stays.
    {
        I3D_frame *f, *tf;
        frames(0, S_vector(0, 3, 0), f, tf);
        auto *d = static_cast<C_Debris *>(debrisCreator->Create());
        g_itLog.clear();
        IT(r.debris, d->SetFrame(f) && d->m_frame == f && g_itLog.empty());
        S_DebrisParams p;
        p.velocity = {rnd(-2, 2), rnd(0, 2), rnd(-2, 2), 0};
        p.weight = rnd(1, 20);
        p.friction = rnd(0.2f, 1);
        p.moveCollision = true;
        p.unknown41 = seed & 1;
        for (bool fadeOut : {true, false}) {
            const unsigned sd = 500u + static_cast<unsigned>(seed & 0xFF) + fadeOut;
            std::srand(sd);
            const int expected = static_cast<int>(RandFloat() * 10000.0f + 30000.0f);
            std::srand(sd);
            g_itLog.clear();
            d->Setup(p, fadeOut);
            C_RigidBody twin;
            twin.SetFrame(tf);
            twin.Setup(p);
            twin.Start();
            IT(r.debris, d->m_lifeTime == expected && d->m_fadeOut == fadeOut && ItSameBody(d->m_body, twin) &&
                             d->m_body.m_frame == f && d->m_body.m_owner == d && g_itLog.empty());
            for (int dt : {20, 40}) {
                d->Update(dt);
                twin.Tick(static_cast<uint32_t>(dt));
                IT(r.debris, ItSameBody(d->m_body, twin) && d->m_lifeTime == (dt == 20 ? expected - 20 : expected - 60));
            }
            d->m_body.Stop();
            twin.Stop();
        }
        // The wheel (a fresh debris, as the game's): the fade-out flag left as it was.
        {
            I3D_frame *wf, *wtf;
            frames(1, S_vector(0, 3, 0), wf, wtf);
            auto *w = static_cast<C_Debris *>(debrisCreator->Create());
            w->SetFrame(wf);
            S_WheelDebrisParams wp;
            wp.velocity = {rnd(-2, 2), 0, rnd(-2, 2), 0};
            wp.radius = rnd(0.2f, 0.5f);
            wp.width = rnd(0.1f, 0.3f);
            wp.weight = rnd(10, 30);
            wp.unknown1C = 0.7f;
            wp.unknown20 = 0.3f;
            wp.moveCollision = seed & 2;
            w->m_fadeOut = true;
            const unsigned sd = 900u + static_cast<unsigned>(seed & 0xFF);
            std::srand(sd);
            const int expected = static_cast<int>(RandFloat() * 10000.0f + 30000.0f);
            std::srand(sd);
            w->SetupWheel(wp);
            C_RigidBody twin;
            twin.SetFrame(wtf);
            twin.SetupWheel(wp);
            twin.Start();
            IT(r.debris, w->m_lifeTime == expected && w->m_fadeOut && ItSameBody(w->m_body, twin) && w->m_body.m_owner == w &&
                             w->m_body.m_frame == wf);
            w->Update(30);
            twin.Tick(30);
            IT(r.debris, ItSameBody(w->m_body, twin));
            twin.Stop();
            debrisCreator->Destroy(w);
        }
        // The mission.
        g_activations.clear();
        g_deactivations.clear();
        d->m_fading = true;
        d->StartMission(true);
        IT(r.debris, g_activations == std::vector<bool>{true} && !d->m_fading);
        g_volumeLog.clear();
        d->StopMission(false);
        IT(r.debris, g_deactivations == std::vector<bool>{false} && d->m_frame == f && ItVolumeOps(d->m_body) == "R");
        // The life.
        const std::string fn = ItName(f);
        for (bool fadeOut : {true, false}) {
            d->m_fadeOut = fadeOut;
            d->m_fading = false;
            d->m_lifeTime = 600;
            g_itLog.clear();
            d->Update(99);
            IT(r.debrisLife, d->m_lifeTime == 501 && ItNoLog() && !d->m_fading);
            d->Update(1);
            IT(r.debrisLife, d->m_lifeTime == 500 && d->m_fading == fadeOut &&
                                 (fadeOut ? ItLogIsAfterBody({"fade " + fn + " 500 0"}) : ItNoLog()));
            g_itLog.clear();
            d->Update(100);
            d->Update(399);
            IT(r.debrisLife, d->m_lifeTime == 1 && ItNoLog());
            g_itDestroyed.clear();
            d->Update(1);
            IT(r.debrisLife, d->m_lifeTime == 0 &&
                                 (fadeOut ? ItLogIsAfterBody({"clear " + fn, "destroy"}) && g_itDestroyed == std::vector<C_Actor *>{d}
                                          : ItNoLog()));
            // Below 0 it is counted once more, then no longer.
            g_itLog.clear();
            d->Update(10);
            IT(r.debrisLife, d->m_lifeTime == -10 && (fadeOut ? ItLogIsAfterBody({"clear " + fn, "destroy"}) : ItNoLog()));
            g_itLog.clear();
            d->Update(10);
            IT(r.debrisLife, d->m_lifeTime == -10 && ItNoLog());
            // The fade-out over the time left; the end straight away (no fade-out with it).
            d->m_lifeTime = 700;
            d->m_fading = false;
            g_itLog.clear();
            d->Update(250);
            IT(r.debrisLife, d->m_lifeTime == 450 && d->m_fading == fadeOut &&
                                 (fadeOut ? ItLogIsAfterBody({"fade " + fn + " 450 0"}) : ItNoLog()));
            d->m_lifeTime = 1;
            d->m_fading = false;
            g_itLog.clear();
            d->Update(1);
            IT(r.debrisLife, d->m_lifeTime == 0 && !d->m_fading &&
                                 (fadeOut ? ItLogIsAfterBody({"clear " + fn, "destroy"}) : ItNoLog()));
            // A fade-out under way is not started again.
            d->m_lifeTime = 300;
            d->m_fading = true;
            g_itLog.clear();
            d->Update(10);
            IT(r.debrisLife, ItNoLog() && d->m_lifeTime == 290);
        }
        debrisCreator->Destroy(d);
    }

    g_gameRole = oldRole;
    g_rayTestHook = nullptr;
    g_frameVtable[0x00 / 4] = oldFrame[0];
    g_frameVtable[0x08 / 4] = oldFrame[1];
    g_frameVtable[0x18 / 4] = oldFrame[2];
    g_frameVtable[0x1C / 4] = oldFrame[3];
    g_frameVtable[0x24 / 4] = oldFrame[4];
    g_frameVtable[0x30 / 4] = oldFrame[5];
    g_frameVtable[0x38 / 4] = oldFrame[6];
    g_frameVtable[0x54 / 4] = oldFrame[7];
    g_sceneVtable[0x7C / 4] = oldScene[0];
    g_sceneVtable[0xE0 / 4] = oldScene[1];
    g_sceneVtable[0xEC / 4] = oldScene[2];
    ClearStreams();
    g_itTemps.clear();
    std::printf("items: creator %d frame %d setup %d life %d update %d tick %d hit %d push %d contact %d sound %d "
                "message %d mission %d save %d teleport %d placement %d respawn %d debris %d debrisLife %d\n",
                r.creator, r.frame, r.setup, r.life, r.update, r.tick, r.hit, r.push, r.contact, r.sound, r.message,
                r.mission, r.save, r.teleport, r.placement, r.respawn, r.debris, r.debrisLife);
}
