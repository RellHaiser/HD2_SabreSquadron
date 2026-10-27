// Part 7a's test (the actors build: build_item.bat also links src/game/clock.cpp, null_actor.cpp,
// wire_cut.cpp, switch_actor.cpp and photo_place.cpp; main runs SimpleActorsTest after AnimationTest on
// every model): the clock, the null actor, the breakable fence, the switch and the photo place against
// oracles written from the original's code. Their engine calls are logged in order through
// item_test.inc's log (frames, the door, the checkpoints, messages); the harness's records give the
// activations, volumes, sounds and script uses; saved games and properties are compared byte for byte.
// The model's boxes give the volumes' shapes. check_item.py reads the "simple:" line.

#include <algorithm>
#include <functional>
#include <limits>

#include "game/clock.h"
#include "game/null_actor.h"
#include "game/photo_place.h"
#include "game/physics.h"
#include "game/switch_actor.h"
#include "game/wire_cut.h"

extern std::vector<bool> g_activations, g_deactivations;
extern int g_actorSaveTag, g_actorCopies, g_actorInits;
extern std::vector<int> g_actorLoads;
extern bool g_textIds;
extern std::vector<int> g_uses;
extern std::map<const I3D_frame *, int> g_frameMaterials;

// --- LS3D's math the clock uses (as LS3DF's 0x1002c530 and 0x10030440) ---------------------------------

S_matrix __stdcall RotateZMatrix(float angle)
{
    S_matrix r;
    r.Identity();
    const float c = std::cos(angle), s = std::sin(angle);
    r.m[0][0] = c;
    r.m[0][1] = s;
    r.m[1][0] = -s;
    r.m[1][1] = c;
    return r;
}
S_quat __stdcall S_quat::RotateByMatrix(const S_matrix &m) const
{
    S_quat a;
    a.Make(m);
    const S_quat &b = *this;
    S_quat r;
    r.s = a.s * b.s - ((a.v.x * b.v.x + a.v.z * b.v.z) + a.v.y * b.v.y);
    r.v.x = (a.v.z * b.v.y - a.v.y * b.v.z) + (a.v.x * b.s + a.s * b.v.x);
    r.v.y = (a.v.y * b.s + a.s * b.v.y) + (a.v.x * b.v.z - a.v.z * b.v.x);
    r.v.z = (a.v.z * b.s + a.s * b.v.z) + (a.v.y * b.v.x - a.v.x * b.v.y);
    return r;
}

// --- the engine: the fence's door, the nearest points and joins, the directions, child lookups -------

// The door: a fake object whose every slot is Hit (the only virtual call the fence makes).
struct S_SaHit {
    const void *door;
    int kind;
    S_vector dir;
    const void *extra;
};
std::vector<S_SaHit> g_saHits;
bool g_saBreaks = true;
bool __attribute__((thiscall)) F_SaDoorHit(void *self, int kind, const S_vector *dir, void *extra)
{
    ItLog("hit %d", kind);
    g_saHits.push_back({self, kind, *dir, extra});
    return g_saBreaks;
}
void *g_saDoorVtable[16];
struct S_SaDoor {
    void **vtable = g_saDoorVtable;
};
const void *g_saHidden;
void C_PhysDoor::HideBrokenPart()
{
    ItLog("hide");
    g_saHidden = this;
}

int C_CheckpointNetwork::FindNearestPoint(S_vector pos, float maxDist, float minDist) const
{
    ItLog("nearest %s %.9g %.9g", ItV(pos).c_str(), maxDist, minDist);
    int best = CHECKPOINT_NONE;
    float bestSq = maxDist * maxDist;
    for (int i = 0; i < m_pointCount; ++i) {
        const float sq = (m_points[i].pos - pos).Square();
        if (sq < bestSq) {
            bestSq = sq;
            best = i;
        }
    }
    return best;
}
bool C_CheckpointNetwork::JoinPoints(int a, int b)
{
    ItLog("join %d %d", a, b);
    return true;
}

// A heading direction in the fake streams: its three floats (bit count -9; another player -10).
void C_NetMessage::WriteDirection(const S_vector &dir, int playerIndex)
{
    auto *s = reinterpret_cast<S_FakeStream *>(this);
    for (float f : {dir.x, dir.y, dir.z}) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        s->bits.emplace_back(bits, playerIndex == -1 ? -9 : -10);
    }
}
void C_NetMessage::ReadDirection(S_vector &dir, int playerIndex)
{
    float v[3] = {};
    for (float &f : v) {
        if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != (playerIndex == -1 ? -9 : -10)) {
            g_readMismatch = true;
            return;
        }
        std::memcpy(&f, &g_readBits[g_readPos++].first, sizeof f);
    }
    dir = S_vector(v[0], v[1], v[2]);
}

I3D_frame *__stdcall F_SaFindChild(I3D_frame *self, const char *name, uint32_t typeMask)
{
    ItLog("child %s %s %x", ItName(self).c_str(), name, typeMask);
    return F_FindChildFrame(self, name, typeMask);
}

// --- helpers ------------------------------------------------------------------------------------------

struct S_Sa {
    bool creators = true, clock = true, clockMission = true, clockSave = true, null = true, wire = true,
         wireMission = true, wireNet = true, wireSave = true, switchFrame = true, switchState = true,
         switchMission = true, switchSave = true, switchProps = true, photo = true, photoSave = true, volume = true;
};
#define SA(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

// Chunk bytes.
struct S_SaBytes {
    std::vector<uint8_t> b;
    void Put(const void *p, size_t n)
    {
        const auto *c = static_cast<const uint8_t *>(p);
        b.insert(b.end(), c, c + n);
    }
    template <typename T> S_SaBytes &Val(const T &v)
    {
        Put(&v, sizeof v);
        return *this;
    }
    S_SaBytes &Head(uint16_t id, uint32_t size)
    {
        Val(id);
        return Val(size);
    }
};
// A reader of `bytes` (entered into their first chunk).
struct S_SaReader {
    C_chunk c;
    explicit S_SaReader(const S_SaBytes &bytes)
    {
        g_files["sa.in"] = bytes.b;
        c.Open("sa.in", false);
        uint16_t id;
        c.Descend(id);
    }
};
// What `save` writes inside chunk 0x777.
std::vector<uint8_t> SaWritten(const std::function<bool(C_chunk &)> &save, bool *result = nullptr)
{
    C_chunk w;
    w.Open("sa.out", true);
    w.Begin(0x777);
    const bool ok = save(w);
    if (result)
        *result = ok;
    const bool balanced = w.m_depth == 1;               // (every chunk begun is left)
    w.Ascend();
    w.Close();
    return balanced ? g_files["sa.out"] : std::vector<uint8_t>{0xBA, 0xD};
}

bool SaLogIs(const std::vector<std::string> &expected)
{
    const bool same = g_itLog == expected;
    if (!same && std::getenv("CAR_TRACE")) {
        std::printf("  log:");
        for (const std::string &x : g_itLog)
            std::printf(" [%s]", x.c_str());
        std::printf("\n  expected:");
        for (const std::string &x : expected)
            std::printf(" [%s]", x.c_str());
        std::printf("\n");
    }
    return same;
}
bool SaSame(const void *a, const void *b, size_t n)
{
    return std::memcmp(a, b, n) == 0;
}
bool SaSameQuat(const S_quat &a, const S_quat &b)
{
    return SaSame(&a, &b, sizeof a);
}
S_quat SaNormalized(S_quat q)
{
    q.Normalize();
    return q;
}
std::vector<std::pair<uint32_t, int>> SaDirBits(const S_vector &d)
{
    std::vector<std::pair<uint32_t, int>> bits;
    for (float f : {d.x, d.y, d.z}) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        bits.emplace_back(u, -9);
    }
    return bits;
}
void SaRead(const std::vector<std::pair<uint32_t, int>> &bits)
{
    g_readBits = bits;
    g_readPos = 0;
    g_readMismatch = false;
}
bool SaReadAll()
{
    return !g_readMismatch && g_readPos == g_readBits.size();
}

// The box volume as the original's StartMission builds it (inline in the switch's 0x00613920 and the
// photo place's 0x0060b820).
struct S_SaVolume {
    I3D_bbox box;
    S_vector center;
    float centerW, radius;
    S_matrix inv;
};
S_SaVolume SaVolumeOracle(I3D_frame *f)
{
    S_SaVolume e;
    I3D_bbox tmp;
    e.box = *F_GetHierarchyBBox(nullptr, tmp, f);
    S_vector c(0, 0, 0);
    float half = 0.0f;
    // FCOMP max.x, TEST AH,0x41, JP: on unless min.x > max.x (or unordered).
    if (e.box.min.x < e.box.max.x || e.box.min.x == e.box.max.x) {
        const float hx = (e.box.max.x - e.box.min.x) * 0.5f;
        const float hy = (e.box.max.y - e.box.min.y) * 0.5f;
        const float hz = (e.box.max.z - e.box.min.z) * 0.5f;
        c = S_vector(e.box.min.x + hx, e.box.min.y + hy, e.box.min.z + hz);
        half = std::sqrt((hz * hz + hy * hy) + hx * hx);
    }
    const S_vector w = f->m_parent ? f->GetMatrix().GetScale() : f->m_scale;
    const float yz = w.y <= w.z ? w.z : w.y;
    e.radius = (w.x <= yz ? yz : w.x) * half;
    e.center = c * f->GetMatrix();
    e.centerW = f->GetMatrix().m[0][3];
    e.inv.Inverse(f->GetMatrix());
    return e;
}
bool SaSameVolume(const S_CollisionVolume &v, const S_matrix &inv, I3D_frame *f, uint32_t type, const C_Actor *actor)
{
    const S_SaVolume e = SaVolumeOracle(f);
    static const uint8_t zeros[0x0C] = {};
    return v.flags == COLLISION_SHAPE_BOX && v.frame == f && v.type == type && v.cell == nullptr &&
           SaSame(&v.bbox, &e.box, sizeof e.box) && SaSame(&v.center, &e.center, 12) && SaSame(&v.centerW, &e.centerW, 4) &&
           v.matrix == &f->GetMatrix() && v.invMatrix == &inv && SaSame(&inv, &e.inv, sizeof inv) && v.actor == actor &&
           SaSame(&v.radius, &e.radius, 4) && v.capsuleRadius == 0.0f && SaSame(v.unknown54, zeros, sizeof zeros);
}

void SimpleActorsTest()
{
    const int slots[] = {0x24, 0x30, 0x38, 0x3C, 0x48};
    void *oldFrame[5];
    for (int i = 0; i < 5; ++i)
        oldFrame[i] = g_frameVtable[slots[i] / 4];
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x48 / 4] = reinterpret_cast<void *>(&F_SaFindChild);
    for (void *&slot : g_saDoorVtable)
        slot = reinterpret_cast<void *>(&F_SaDoorHit);
    const uint8_t oldRole = g_gameRole;
    g_textIds = true;
    S_Sa r;

    // The model's boxes as the volumes' shapes.
    std::vector<I3D_bbox> shapes;
    for (S_StubFrame *s : g_frames) {
        const I3D_bbox b = RbBox(s->frame);
        if (shapes.size() < 3 && b.min.x <= b.max.x && b.max.x - b.min.x > 0.05f && b.max.y - b.min.y > 0.05f &&
            b.max.z - b.min.z > 0.05f)
            shapes.push_back(b);
    }
    while (shapes.size() < 3) {
        I3D_bbox b;
        b.min = S_vector(-0.4f, -0.1f, -0.8f - shapes.size());
        b.max = S_vector(0.6f, 0.9f, 1.2f + shapes.size());
        shapes.push_back(b);
    }
    int seed = static_cast<int>(g_frames.size()) * 17 + 3;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    int frameNo = 0;
    // A frame placed, turned and scaled (under a placed parent when `parented`).
    const auto placed = [&](E_I3D_FRAME_TYPE type, const std::string &name, bool parented) {
        I3D_frame *parent = nullptr;
        if (parented) {
            parent = AnFrame(FRAME_DUMMY, name + ".p" + std::to_string(frameNo), nullptr);
            parent->m_localMatrix.m[3][0] = rnd(-30, 30);
            parent->m_localMatrix.m[3][2] = rnd(-30, 30);
            parent->m_localMatrix.m[0][0] = rnd(0.5f, 2.0f);   // (a scaled parent: the world scale differs)
        }
        I3D_frame *f = AnFrame(type, name + std::to_string(frameNo++), parent);
        RbTilt(f, 0.6f, 0.8f);
        f->m_localMatrix.m[3][0] = rnd(-10, 10);
        f->m_localMatrix.m[3][1] = rnd(-2, 4);
        f->m_localMatrix.m[3][2] = rnd(-10, 10);
        f->m_scale = S_vector(rnd(0.5f, 2), rnd(0.5f, 2), rnd(0.5f, 2));
        return f;
    };
    // A box visual of shape `k` under `parent` (named for FindChildFrame).
    const auto boxFrame = [&](I3D_frame *parent, const char *name, int k) {
        const I3D_bbox &b = shapes[static_cast<size_t>(k) % shapes.size()];
        I3D_frame *f = RbBoxFrame(parent, name, b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z,
                                  S_vector(rnd(-1, 1), rnd(0, 1), rnd(-1, 1)));
        f->m_name = Stub(f).name.c_str();
        return f;
    };
    S_FrameData scriptData = {};
    scriptData.script = reinterpret_cast<C_Script *>(&scriptData);
    S_FrameData noScript = {};
    const auto reset = [&] {
        g_itLog.clear();
        g_activations.clear();
        g_deactivations.clear();
        g_volumeLog.clear();
        g_soundRequests.clear();
        g_uses.clear();
        g_saHits.clear();
        g_saHidden = nullptr;
        g_actorLoads.clear();
        ClearStreams();
        g_itTemps.clear();
    };

    // --- the creators ---------------------------------------------------------------------------------
    {
        struct S_Kind {
            E_ActorType type;
            const char *name, *text;
        };
        const S_Kind kinds[] = {{ACTOR_CLOCK, "Clock", "Missing actor name!!"},
                                {ACTOR_NULL, "NullActor", "Missing actor name!!"},
                                {ACTOR_WIRE_CUT, "WireCut", "text 7158"},
                                {ACTOR_SWITCH, "Switch", "text 7155"},
                                {ACTOR_PHOTO_PLACE, "Photo place", "text -1"}};
        for (const S_Kind &k : kinds) {
            C_ActorCreator *creator = g_actorCreators[k.type];
            SA(r.creators, creator && std::strcmp(creator->GetName(), k.name) == 0);
            if (!creator)
                continue;
            C_Actor *a = creator->Create();
            SA(r.creators, a && a->GetType() == k.type && std::strcmp(*a->GetName(), k.text) == 0 && !a->GetFrame());
            creator->Destroy(a);
        }
        auto *w = static_cast<C_WireCut *>(g_actorCreators[ACTOR_WIRE_CUT]->Create());
        SA(r.creators, w->m_door == nullptr && w->GetPoint(0) == CHECKPOINT_NONE && w->GetPoint(1) == CHECKPOINT_NONE &&
                           !w->IsCut());
        delete w;
        auto *s = static_cast<C_Switch *>(g_actorCreators[ACTOR_SWITCH]->Create());
        SA(r.creators, s->GetState() == 0 && s->m_sound == -1 && s->m_volume.flags == 0 && s->m_volume.frame == nullptr);
        delete s;
        auto *p = static_cast<C_PhotoPlace *>(g_actorCreators[ACTOR_PHOTO_PLACE]->Create());
        SA(r.creators, p->m_textId == -1 && !p->m_started && p->GetState() == 0 && p->m_volume.flags == 0);
        delete p;
    }

    // --- the clock ------------------------------------------------------------------------------------
    {
        struct S_ClockScene {
            I3D_frame *root, *hour, *minute, *second, *firstSecond;
        };
        static const char hourName[] = "clockmodel.hour";
        // The hands: "sec" under a dial (found first), "SEC" later (wins: the last match, any case),
        // "hour" by its short name, under a dummy (the whole hierarchy is searched), decoys.
        const auto clockScene = [&](bool minute, bool second) {
            S_ClockScene s;
            s.root = placed(FRAME_MODEL, "sa.clock", true);
            I3D_frame *dial = AnFrame(FRAME_DUMMY, "dial", s.root);
            s.firstSecond = second ? AnFrame(FRAME_VISUAL, "sec", dial) : nullptr;
            AnFrame(FRAME_VISUAL, "hours", s.root);
            s.hour = AnFrame(FRAME_VISUAL, "hourhand", AnFrame(FRAME_DUMMY, "hourdial", s.root));
            s.hour->m_name = hourName;
            s.hour->m_nameOffset = 11;
            s.minute = minute ? AnFrame(FRAME_VISUAL, "min", s.root) : nullptr;
            AnFrame(FRAME_VISUAL, "minute", s.root);
            s.second = second ? AnFrame(FRAME_VISUAL, "SEC", s.root) : nullptr;
            for (I3D_frame *f : {s.hour, s.minute, s.second, s.firstSecond}) {
                if (!f)
                    continue;
                RbTilt(f, rnd(-1, 1), rnd(-1, 1), rnd(0, 1) > 0.5f);
                f->m_frameFlags |= 0x134;               // (SetRot clears these)
            }
            // The hour hand's rotation from its matrix (GetRot makes it).
            s.hour->m_frameFlags &= ~FRMFLAGS_ROT_DIRTY;
            s.hour->m_rot = S_quat();
            s.hour->m_rot.s = 7.0f;
            return s;
        };
        C_ActorCreator *creator = g_actorCreators[ACTOR_CLOCK];
        const float rates[3] = {-0.104719758f, -0.00174532935f, -0.000145444108f};   // sec, min, hour
        for (int variant = 0; variant < 3; ++variant) {
            const bool hasMinute = variant != 1, hasSecond = variant != 2;
            S_ClockScene s = clockScene(hasMinute, hasSecond);
            auto *a = static_cast<C_Clock *>(creator->Create());
            a->m_hour = a->m_minute = a->m_second = s.root;
            const int inits = g_actorInits;
            SA(r.clock, a->SetFrame(s.root) && a->GetFrame() == s.root && g_actorInits == inits + 1 && !a->m_hour &&
                            !a->m_minute && !a->m_second);
            reset();
            // The server side: nothing.
            a->m_time = 5.0f;
            a->m_tick = 0.5f;
            a->StartMission(true);
            SA(r.clockMission, !a->m_hour && !a->m_second && a->m_time == 5.0f && a->m_tick == 0.5f && g_itLog.empty() &&
                                   g_activations.empty());
            // The client side: the hands, their rotations, the time from 0; no activation.
            S_quat hourStart;
            hourStart.Make(s.hour->m_localMatrix);
            const S_quat minuteStart = hasMinute ? s.minute->m_rot : S_quat();
            const S_quat secondStart = hasSecond ? s.second->m_rot : S_quat();
            a->StartMission(false);
            SA(r.clockMission, a->m_hour == s.hour && a->m_minute == s.minute && a->m_second == s.second &&
                                   SaSameQuat(a->m_hourRot, hourStart) && (s.hour->m_frameFlags & FRMFLAGS_ROT_DIRTY) &&
                                   (!hasMinute || SaSameQuat(a->m_minuteRot, minuteStart)) &&
                                   (!hasSecond || SaSameQuat(a->m_secondRot, secondStart)) && a->m_time == 0.0f &&
                                   a->m_tick == 0.0f && g_itLog.empty() && g_activations.empty());
            // The steps: the hands move once over a second has gathered (not at exactly 1), with the
            // second hand only.
            float time = 0.0f, tick = 0.0f;
            const int steps[] = {300, 400, 300, 0, 1, 2345, 999, 500, 500, 1, -1000};
            for (int ms : steps) {
                g_itLog.clear();
                for (I3D_frame *f : {s.hour, s.minute, s.second})
                    if (f)
                        f->m_frameFlags |= 0x134;
                a->Update(ms);
                const float secs = static_cast<float>(static_cast<uint32_t>(ms)) * 0.001f;
                time = secs + time;
                tick = secs + tick;
                const bool moves = hasSecond && tick > 1.0f;
                std::vector<std::string> log;
                bool rots = true;
                if (moves) {
                    I3D_frame *const hands[3] = {s.second, s.minute, s.hour};
                    const S_quat starts[3] = {secondStart, minuteStart, hourStart};
                    for (int h = 0; h < 3; ++h) {
                        if (!hands[h])
                            continue;
                        log.push_back("update " + ItName(hands[h]));
                        const S_quat e = SaNormalized(starts[h].RotateByMatrix(RotateZMatrix(time * rates[h])));
                        rots = rots && SaSameQuat(hands[h]->m_rot, e) && !(hands[h]->m_frameFlags & 0x134) &&
                               (hands[h]->m_frameFlags & 0x40000008) == 0x40000008;
                    }
                    tick -= static_cast<float>(static_cast<int>(tick));
                }
                SA(r.clock, rots && g_itLog == log && SaSame(&a->m_time, &time, 4) && SaSame(&a->m_tick, &tick, 4));
            }
            // StopMission: the server side nothing; the client side the hands back at the start.
            g_itLog.clear();
            a->StopMission(true);
            SA(r.clockMission, g_itLog.empty());
            a->StopMission(false);
            std::vector<std::string> log;
            for (I3D_frame *f : {s.second, s.minute, s.hour})
                if (f)
                    log.push_back("update " + ItName(f));
            SA(r.clockMission, g_itLog == log && SaSameQuat(s.hour->m_rot, SaNormalized(hourStart)) &&
                                   (!hasMinute || SaSameQuat(s.minute->m_rot, SaNormalized(minuteStart))) &&
                                   (!hasSecond || SaSameQuat(s.second->m_rot, SaNormalized(secondStart))) &&
                                   (s.hour->m_frameFlags & 0x40000008) == 0x40000008);
            // RestartMission: the client side's stop and start (the time from 0 again).
            a->Update(1500);
            g_itLog.clear();
            a->RestartMission(true);
            SA(r.clockMission, g_itLog == log && a->m_time == 0.0f && a->m_tick == 0.0f &&
                                   SaSameQuat(a->m_hourRot, SaNormalized(hourStart)) && g_activations.empty() &&
                                   g_deactivations.empty());
            // CopyFrom: the base's.
            const int copies = g_actorCopies;
            a->CopyFrom(a);
            SA(r.clock, g_actorCopies == copies + 1);
            // SaveGame: the two floats raw.
            a->m_time = 12.5f;
            a->m_tick = 0.75f;
            bool saved = false;
            S_SaBytes e;
            e.Head(0x777, 14).Val(12.5f).Val(0.75f);
            SA(r.clockSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == e.b && saved);
            // LoadGame: the time to whole ms, the hands turned by Update, the gathered part restored.
            const float loads[][2] = {{2.5f, 0.25f}, {1.0009f, 0.5f}, {-0.5f, 0.125f}, {3.75f, 0.0f}};
            for (const auto &l : loads) {
                a->RestartMission(false);
                a->m_time = 50.0f;                      // (LoadGame starts both from 0)
                a->m_tick = 0.5f;
                S_SaBytes in;
                in.Head(0x777, 14).Val(l[0]).Val(l[1]);
                S_SaReader rd(in);
                g_itLog.clear();
                const bool ok = a->LoadGame(rd.c);
                const int ms = static_cast<int>(l[0] * 1000.0f);
                const float secs = static_cast<float>(static_cast<uint32_t>(ms)) * 0.001f;
                const bool moves = hasSecond && secs > 1.0f;
                std::vector<std::string> moved;
                if (moves)
                    for (I3D_frame *f : {s.second, s.minute, s.hour})
                        if (f)
                            moved.push_back("update " + ItName(f));
                const S_quat e2 = SaNormalized(SaNormalized(hourStart).RotateByMatrix(RotateZMatrix(secs * rates[2])));
                SA(r.clockSave, ok && SaSame(&a->m_time, &secs, 4) && a->m_tick == l[1] && g_itLog == moved &&
                                    (!moves || SaSameQuat(s.hour->m_rot, e2)) && rd.c.RemainingSize() == 0);
            }
            // A short chunk: the second float 0; an empty one: Update(0).
            {
                a->RestartMission(false);
                S_SaBytes in;
                in.Head(0x777, 10).Val(1.5f);
                S_SaReader rd(in);
                const float secs = static_cast<float>(1500u) * 0.001f;
                SA(r.clockSave, a->LoadGame(rd.c) && SaSame(&a->m_time, &secs, 4) && a->m_tick == 0.0f);
                S_SaBytes none;
                none.Head(0x777, 6);
                S_SaReader rd2(none);
                g_itLog.clear();
                SA(r.clockSave, a->LoadGame(rd2.c) && a->m_time == 0.0f && a->m_tick == 0.0f && g_itLog.empty());
            }
            creator->Destroy(a);
        }
    }

    // --- the null actor ---------------------------------------------------------------------------------
    {
        struct C_SaNull : public C_NullActor {
            std::string calls;
            void StartMission(bool server) override
            {
                calls += server ? "start1 " : "start0 ";
                C_NullActor::StartMission(server);
            }
            void StopMission(bool server) override
            {
                calls += server ? "stop1 " : "stop0 ";
                C_NullActor::StopMission(server);
            }
        };
        C_SaNull n;
        reset();
        n.RestartMission(false);
        SA(r.null, n.calls == "stop1 start1 " && g_activations == std::vector<bool>{true} &&
                       g_deactivations == std::vector<bool>{true});
        auto *a = static_cast<C_NullActor *>(g_actorCreators[ACTOR_NULL]->Create());
        reset();
        a->StartMission(false);
        a->StopMission(false);
        a->StartMission(true);
        SA(r.null, g_activations == std::vector<bool>({false, true}) && g_deactivations == std::vector<bool>{false});
        const int copies = g_actorCopies;
        a->CopyFrom(nullptr);
        SA(r.null, g_actorCopies == copies);
        a->CopyFrom(&n);
        SA(r.null, g_actorCopies == copies + 1);
        g_actorSaveTag = 4321;
        bool saved = false;
        S_SaBytes e;
        e.Head(0x777, 16).Head(0x3039, 10).Val(4321);
        SA(r.null, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == e.b && saved);
        {
            S_SaReader rd(e);
            SA(r.null, a->LoadGame(rd.c) && g_actorLoads == std::vector<int>{4321});
            S_SaBytes other;
            other.Head(0x777, 12).Head(5, 6);
            S_SaReader rd2(other);
            SA(r.null, a->LoadGame(rd2.c) && g_actorLoads.size() == 1);
        }
        C_chunk none;
        SA(r.null, a->WriteProperties(none) && a->ReadProperties(none));
        g_actorCreators[ACTOR_NULL]->Destroy(a);
    }

    // --- the breakable fence ----------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_WIRE_CUT];
        alignas(16) static uint8_t networkMemory[sizeof(C_CheckpointNetwork)];
        std::memset(networkMemory, 0, sizeof networkMemory);
        auto &network = *reinterpret_cast<C_CheckpointNetwork *>(networkMemory);
        static S_Checkpoint points[4];
        network.m_points = points;
        network.m_pointCount = 4;
        C_Checkpoints *const oldCheckpoints = GetGame()->m_checkpoints;
        GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(&network);
        S_SaDoor door;
        // The fence, its points: 0 near it, 1 beyond its mirror image, 2 and 3 far.
        I3D_frame *fence = placed(FRAME_VISUAL, "sa.fence", true);
        const S_vector at = fence->GetWorldPos();
        const S_vector side(rnd(0.3f, 0.9f), rnd(-0.1f, 0.1f), rnd(0.2f, 0.6f));
        points[0].pos = at + side;
        const S_vector mirrorOracle((at.x - points[0].pos.x) + at.x, (at.y - points[0].pos.y) + at.y,
                                    (at.z - points[0].pos.z) + at.z);
        points[1].pos = mirrorOracle - side * 0.3f;
        points[2].pos = at + S_vector(40, 0, 0);
        points[3].pos = at + S_vector(0, 0, -40);
        const auto fenceActor = [&] {
            auto *a = static_cast<C_WireCut *>(creator->Create());
            a->SetFrame(fence);
            a->SetDoor(reinterpret_cast<C_PhysDoor *>(&door));
            a->m_netId = 61;
            return a;
        };
        C_WireCut *a = fenceActor();

        // StartMission: a local game's server finds the points; others none (server) or keep them.
        g_gameRole = GAME_ROLE_LOCAL;
        reset();
        a->m_cutDir = S_vector(1, 2, 3);
        a->StartMission(true);
        SA(r.wireMission, a->m_points[0] == 0 && a->m_points[1] == 1 && !a->IsCut() &&
                              g_activations == std::vector<bool>{true} &&
                              ItLogIs({"nearest " + ItV(at) + " 5 -1", "nearest " + ItV(mirrorOracle) + " 5 -1"}));
        g_gameRole = GAME_ROLE_SERVER;
        reset();
        a->m_points[0] = 2;
        a->m_points[1] = 3;
        a->StartMission(true);
        SA(r.wireMission, a->m_points[0] == CHECKPOINT_NONE && a->m_points[1] == CHECKPOINT_NONE && g_itLog.empty());
        g_gameRole = GAME_ROLE_CLIENT;
        reset();
        a->m_points[0] = 2;
        a->m_points[1] = 3;
        a->m_cutDir = S_vector(1, 2, 3);
        a->StartMission(false);
        SA(r.wireMission, a->m_points[0] == 2 && a->m_points[1] == 3 && !a->IsCut() && g_itLog.empty() &&
                              g_activations == std::vector<bool>{false});
        // No point near: the second is not looked for.
        g_gameRole = GAME_ROLE_LOCAL;
        {
            const S_vector keep[2] = {points[0].pos, points[1].pos};
            points[0].pos = at + S_vector(0, 0, 50);
            points[1].pos = at + S_vector(0, 5.01f, 0);
            reset();
            a->StartMission(true);
            SA(r.wireMission, a->m_points[0] == CHECKPOINT_NONE && a->m_points[1] == CHECKPOINT_NONE &&
                                  ItLogIs({"nearest " + ItV(at) + " 5 -1"}));
            points[0].pos = keep[0];
            points[1].pos = keep[1];
        }
        // StopMission: the server side resets the points' types and forgets them.
        for (int k = 0; k < 3; ++k) {
            reset();
            a->m_points[0] = k == 1 ? CHECKPOINT_NONE : 0;
            a->m_points[1] = 1;
            points[0].type = points[1].type = 7;
            a->StopMission(k != 2);
            if (k == 2)
                SA(r.wireMission, a->m_points[0] == 0 && a->m_points[1] == 1 && points[0].type == 7 && points[1].type == 7 &&
                                      g_deactivations == std::vector<bool>{false});
            else
                SA(r.wireMission, a->m_points[0] == CHECKPOINT_NONE && a->m_points[1] == CHECKPOINT_NONE &&
                                      points[0].type == (k == 1 ? 7 : 0) && points[1].type == 0 &&
                                      g_deactivations == std::vector<bool>{true});
        }
        reset();
        a->RestartMission(true);
        SA(r.wireMission, g_activations.empty() && g_deactivations.empty() && g_itLog.empty());
        const int copies = g_actorCopies;
        a->CopyFrom(a);
        SA(r.wire, g_actorCopies == copies + 1);

        // IsCut: the direction's square from 1e-16 (NaN cut).
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const struct {
                S_vector d;
                bool cut;
            } cases[] = {{S_vector(0, 0, 0), false},      {S_vector(1e-9f, 0, 0), false},  {S_vector(0, 0, 1e-7f), true},
                         {S_vector(0, -2e-8f, 0), true},  {S_vector(nan, 0, 0), true},     {S_vector(-0.3f, 0, 0), true},
                         {S_vector(6e-9f, 7e-9f, 5e-9f), true}, {S_vector(5e-9f, 5e-9f, 5e-9f), false}};
            for (const auto &k : cases) {
                a->m_cutDir = k.d;
                SA(r.wire, a->IsCut() == k.cut);
            }
        }

        // Cut: the door breaks (or not), the broken part hidden on request, the way opened (a local
        // game), the clients told the direction, the script used, the direction kept.
        for (int k = 0; k < 6; ++k) {
            const bool hide = k & 1, local = k < 4, breaks = k != 2 && k != 3, script = k != 5;
            g_gameRole = local ? GAME_ROLE_LOCAL : GAME_ROLE_SERVER;
            g_saBreaks = breaks;
            fence->SetData(script ? &scriptData : &noScript);
            a->m_points[0] = 0;
            a->m_points[1] = 1;
            points[0].type = points[1].type = 0;
            a->m_cutDir = S_vector(0, 0, 0);
            reset();
            const S_vector dir(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
            const bool cut = a->Cut(dir, hide);
            const bool hitOk = g_saHits.size() == 1 && g_saHits[0].door == &door && g_saHits[0].kind == 3 &&
                               RbSame(g_saHits[0].dir, dir) && g_saHits[0].extra == nullptr;
            if (!breaks) {
                SA(r.wire, !cut && hitOk && ItLogIs({"hit 3"}) && !a->IsCut() && g_streams.empty() && g_uses.empty());
                continue;
            }
            std::vector<std::string> log = {"hit 3"};
            if (hide)
                log.push_back("hide");
            if (local)
                log.push_back("join 0 1");
            log.push_back("msg 61 1");
            log.push_back("send");
            SA(r.wire, cut && hitOk && g_itLog == log && g_saHidden == (hide ? &door : nullptr) &&
                           points[0].type == (local ? 7 : 0) && points[1].type == (local ? 7 : 0) &&
                           g_streams.size() == 1 && g_streams[0]->bits == SaDirBits(dir) && g_streams[0]->sent &&
                           g_itTemps.size() == 1 && g_itTemps[0].reliable &&
                           g_uses == (script ? std::vector<int>{1} : std::vector<int>{}) && RbSame(a->m_cutDir, dir));
        }
        g_saBreaks = true;
        // A local game with one point missing: both joined as they are, the found one's type set.
        {
            g_gameRole = GAME_ROLE_LOCAL;
            a->m_points[0] = CHECKPOINT_NONE;
            a->m_points[1] = 1;
            points[0].type = points[1].type = 0;
            reset();
            a->Cut(S_vector(0, 0, 1), false);
            SA(r.wire, g_itLog.size() > 1 && g_itLog[1] == "join 65535 1" && points[0].type == 0 && points[1].type == 7);
        }

        // The network: WriteState (bit, direction), ReadState and OnMessage break the door on clients
        // only (no hiding, script, message or direction of their own).
        {
            reset();
            a->m_cutDir = S_vector(0, 0, 0);
            g_streams.push_back(new S_FakeStream{0, false, {}, false});
            a->WriteState(reinterpret_cast<C_BitStream *>(g_streams.back()));
            std::vector<std::pair<uint32_t, int>> e = {{0, 1}};
            SA(r.wireNet, g_streams.back()->bits == e);
            const S_vector dir(0.5f, -0.25f, 2.0f);
            a->m_cutDir = dir;
            g_streams.push_back(new S_FakeStream{0, false, {}, false});
            a->WriteState(reinterpret_cast<C_BitStream *>(g_streams.back()));
            e = {{1, 1}};
            for (const auto &b : SaDirBits(dir))
                e.push_back(b);
            SA(r.wireNet, g_streams.back()->bits == e);
            const uint8_t roles[] = {GAME_ROLE_CLIENT, GAME_ROLE_SERVER, 3, GAME_ROLE_LOCAL};
            for (uint8_t role : roles) {
                g_gameRole = role;
                for (int how = 0; how < 3; ++how) {
                    reset();
                    a->m_cutDir = S_vector(0, 0, 0);
                    a->m_points[0] = 0;
                    a->m_points[1] = 1;
                    points[0].type = points[1].type = 0;
                    const S_vector got(rnd(-1, 1), 0.0f, rnd(-1, 1));
                    std::vector<std::pair<uint32_t, int>> in;
                    if (how == 1)
                        in.emplace_back(1, 1);
                    if (how == 2)
                        in.emplace_back(0, 1);
                    if (how < 2)
                        for (const auto &b : SaDirBits(got))
                            in.push_back(b);
                    SaRead(in);
                    if (how == 0)
                        a->OnMessage(nullptr);
                    else
                        a->ReadState(nullptr);
                    const bool client = !(role & GAME_ROLE_SERVER) && how != 2;
                    SA(r.wireNet, SaReadAll() && !a->IsCut() && g_streams.empty() && g_uses.empty() &&
                                      points[0].type == 0 && g_saHidden == nullptr &&
                                      g_itLog == (client ? std::vector<std::string>{"hit 3"} : std::vector<std::string>{}) &&
                                      (!client || (g_saHits.size() == 1 && RbSame(g_saHits[0].dir, got) && g_saHits[0].kind == 3 &&
                                                   g_saHits[0].door == &door)));
                }
            }
        }
        // GetMaterialId: the part's, 11 for a negative one.
        {
            I3D_frame *part = AnFrame(FRAME_VISUAL, "sa.part", fence);
            const int ids[] = {5, 0, -1, -7, 300};
            for (int id : ids) {
                g_frameMaterials[part] = id;
                SA(r.wire, a->GetMaterialId(part) == (id < 0 ? 11 : id));
            }
            g_frameMaterials.erase(part);
            SA(r.wire, a->GetMaterialId(part) == 11);
        }

        // SaveGame: chunk 1000, the direction's three floats.
        {
            const S_vector dir(0.25f, -1.0f, 3.0f);
            a->m_cutDir = dir;
            bool saved = false;
            S_SaBytes e;
            e.Head(0x777, 24).Head(1000, 18).Val(dir.x).Val(dir.y).Val(dir.z);
            SA(r.wireSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == e.b && saved);
        }
        // LoadGame: chunk 1000 read (the last one wins; a short one not read), other chunks entered and
        // not left; then a cut made again without hiding (a local game: the way opened).
        g_gameRole = GAME_ROLE_LOCAL;
        fence->SetData(&scriptData);
        for (int k = 0; k < 5; ++k) {
            const S_vector dir(rnd(-1, 1), rnd(-1, 1), 1.5f), dir2(-0.5f, 0.25f, rnd(-1, 1));
            S_SaBytes in;
            S_vector expect = dir;
            int depth = 1;
            if (k == 0) {
                in.Head(0x777, 24).Head(1000, 18).Val(dir.x).Val(dir.y).Val(dir.z);
            } else if (k == 1) {
                in.Head(0x777, 24).Head(1000, 18).Val(0.0f).Val(0.0f).Val(0.0f);
                expect = S_vector(0, 0, 0);
            } else if (k == 2) {
                in.Head(0x777, 30).Head(5, 24).Head(1000, 18).Val(dir.x).Val(dir.y).Val(dir.z);
                depth = 2;
            } else if (k == 3) {
                in.Head(0x777, 20).Head(1000, 14).Val(dir.x).Val(dir.y);
                expect = S_vector(0, 0, 2.0f);              // (the kept direction)
            } else {
                in.Head(0x777, 42).Head(1000, 18).Val(dir.x).Val(dir.y).Val(dir.z).Head(1000, 18).Val(dir2.x).Val(dir2.y).Val(dir2.z);
                expect = dir2;
            }
            a->m_cutDir = k == 3 ? S_vector(0, 0, 2.0f) : S_vector(0, 0, 0);
            a->m_points[0] = 0;
            a->m_points[1] = 1;
            S_SaReader rd(in);
            reset();
            const bool ok = a->LoadGame(rd.c);
            const bool cut = k != 1;
            SA(r.wireSave, ok && RbSame(a->m_cutDir, expect) && rd.c.m_depth == depth && rd.c.RemainingSize() == 0 &&
                               g_itLog == (cut ? std::vector<std::string>{"hit 3", "join 0 1", "msg 61 1", "send"}
                                               : std::vector<std::string>{}) &&
                               (!cut || (RbSame(g_saHits[0].dir, expect) && g_uses == std::vector<int>{1})));
        }
        fence->SetData(nullptr);
        creator->Destroy(a);
        GetGame()->m_checkpoints = oldCheckpoints;
        network.m_points = nullptr;
        network.m_pointCount = 0;
    }

    // --- the switch ---------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_SWITCH];
        struct S_SwitchScene {
            I3D_frame *root, *base, *decor;
            std::vector<I3D_frame *> states;            // "1", "2", ...
        };
        // The states "1".."count" ("1" under "base" when `nested`, "3" under "decor" and locked).
        const auto switchScene = [&](int count, bool nested, bool parented) {
            S_SwitchScene s;
            s.root = placed(FRAME_MODEL, "sa.sw", parented);
            s.base = nested ? AnFrame(FRAME_DUMMY, "base", s.root) : s.root;
            s.decor = AnFrame(FRAME_DUMMY, "decor", s.root);
            for (int i = 1; i <= count; ++i) {
                static const char *names[] = {"1", "2", "3", "4", "5"};
                I3D_frame *parent = i == 1 ? s.base : i == 3 ? s.decor : s.root;
                I3D_frame *f = boxFrame(parent, names[i - 1], i);
                if (i == 3)
                    f->m_frameFlags |= FRMFLAGS_POS_LOCKED;
                s.states.push_back(f);
            }
            return s;
        };
        const auto children = [](I3D_frame *f) {
            std::vector<I3D_frame *> c;
            for (I3D_frame *k = f->m_firstChild; k; k = k->m_nextSibling)
                c.push_back(k);
            return c;
        };
        // SetState's log after the script: "1" looked up, the state's frame linked under "1"'s parent,
        // updated, the frame's children hidden from the last, the state's shown.
        const auto stateLog = [&](const S_SwitchScene &s, int state, std::vector<std::string> &log) {
            const std::string root = ItName(s.root);
            log.push_back("child " + root + " 1 ffff");
            if (s.states.empty())
                return;
            log.push_back("child " + root + " " + std::to_string(state) + " ffff");
            if (state > static_cast<int>(s.states.size()))
                return;
            I3D_frame *f = s.states[static_cast<size_t>(state - 1)];
            log.push_back("link " + ItName(f) + " " + ItName(s.states[0]->m_parent) + " 0");
            log.push_back("update " + ItName(f));
            // (The children as they will be after the link: f moved to the end of its new parent's.)
            std::vector<I3D_frame *> c = children(s.root);
            if (s.states[0]->m_parent == s.root) {
                c.erase(std::remove(c.begin(), c.end(), f), c.end());
                c.push_back(f);
            }
            for (size_t i = c.size(); i-- > 0;)
                log.push_back("on " + ItName(c[i]) + " 0");
            log.push_back("on " + ItName(f) + " 1");
        };

        // SetFrame: the states counted and put at "1" under its parent (a locked one not moved), then
        // state 0 set (that is 1).
        for (int k = 0; k < 2; ++k) {
            const bool nested = k == 1;
            S_SwitchScene s = switchScene(3, nested, nested);
            auto *a = static_cast<C_Switch *>(creator->Create());
            reset();
            SA(r.switchFrame, !a->SetFrame(nullptr) && g_itLog.empty() && !a->GetFrame());
            const S_vector firstPos = s.states[0]->m_localMatrix(3);
            const S_vector lockedPos = s.states[2]->m_localMatrix(3);
            const uint32_t lockedFlags = s.states[2]->m_frameFlags;
            const std::string root = ItName(s.root), parent = ItName(s.base);
            std::vector<std::string> log = {"child " + root + " 1 ffff", "child " + root + " 1 ffff",
                                            "child " + root + " 2 ffff", "link 2 " + parent + " 0",
                                            "child " + root + " 3 ffff", "link 3 " + parent + " 0",
                                            "child " + root + " 4 ffff"};
            reset();
            g_gameRole = GAME_ROLE_SERVER;
            s.root->SetData(&scriptData);
            const bool ok = a->SetFrame(s.root);
            // Then state 1: "1" linked again under its parent (its own place), the actor frame's
            // children hidden from the last (in the flat scene "1" was moved to the end), "1" shown.
            const std::vector<std::string> tail =
                nested ? std::vector<std::string>{"child " + root + " 1 ffff", "child " + root + " 1 ffff", "link 1 base 0",
                                                  "update 1", "on decor 0", "on base 0", "on 1 1"}
                       : std::vector<std::string>{"child " + root + " 1 ffff", "child " + root + " 1 ffff",
                                                  "link 1 " + root + " 0", "update 1", "on 1 0", "on 3 0", "on 2 0",
                                                  "on decor 0", "on 1 1"};
            log.insert(log.end(), tail.begin(), tail.end());
            SA(r.switchFrame, ok && SaLogIs(log) && a->GetFrame() == s.root && a->m_stateCount == 3 && a->GetState() == 1 &&
                                  s.states[1]->m_parent == s.base && s.states[2]->m_parent == s.base &&
                                  RbSame(s.states[1]->m_localMatrix(3), firstPos) &&
                                  (s.states[1]->m_frameFlags & 0x40000120) == 0x40000000 &&
                                  RbSame(s.states[2]->m_localMatrix(3), lockedPos) &&
                                  !(s.states[2]->m_frameFlags & 0x40000000) && (lockedFlags & 0x120) == (s.states[2]->m_frameFlags & 0x120) &&
                                  g_uses == std::vector<int>{1} && g_soundRequests.empty());
            s.root->SetData(nullptr);
            creator->Destroy(a);
        }

        // SetState: 1..3 (else 1), the script told (a server, not quiet, with a script), the state's frame
        // shown, the sound (the object manipulation chapter, at the frame); a missing frame ends it.
        {
            S_SwitchScene s = switchScene(3, false, true);
            auto *a = static_cast<C_Switch *>(creator->Create());
            a->SetFrame(s.root);
            a->m_sound = 9;
            const int states[] = {2, 4, -2, 3, 0, 1, 2};
            for (int k = 0; k < 7; ++k) {
                const int state = states[k];
                const int shown = state < 1 || state > 3 ? 1 : state;
                g_gameRole = k == 3 ? GAME_ROLE_CLIENT : k == 6 ? GAME_ROLE_LOCAL : GAME_ROLE_SERVER;
                a->m_quiet = k == 4;
                s.root->SetData(k == 5 ? &noScript : &scriptData);
                if (k == 6)
                    a->m_sound = -1;
                reset();
                std::vector<std::string> log;
                stateLog(s, shown, log);
                // (The state's frame moved off "1"'s place: SetState puts it back; "3" is locked.)
                const S_vector onePos = s.states[0]->m_localMatrix(3);
                I3D_frame *target = s.states[static_cast<size_t>(shown - 1)];
                if (shown == 2)
                    target->m_localMatrix.m[3][0] += 1.0f;
                const S_vector lockedPos = s.states[2]->m_localMatrix(3);
                a->SetState(state);
                const bool placedOk = shown == 3 ? RbSame(target->m_localMatrix(3), lockedPos)
                                                 : RbSame(target->m_localMatrix(3), onePos);
                const bool told = k != 3 && k != 4 && k != 5;
                const S_matrix &w = s.root->GetMatrix();
                bool sound = g_soundRequests.size() == (k == 6 ? 0u : 1u);
                if (sound && k != 6) {
                    const S_SoundRequest &q = g_soundRequests[0];
                    sound = RbSame(q.pos, w(3)) && q.posW == w.m[3][3] && q.flags == SOUND_SILENT_TO_AI &&
                            q.attachTo == nullptr && q.source == nullptr && q.chapter == SOUND_CHAPTER_OBJECT_MANIPULATION &&
                            q.group == 9 && q.fileName == nullptr && q.channel == SOUND_CHANNEL_NONE && q.nearScale == 1.0f &&
                            q.farScale == 1.0f && q.volume == 1.0f && q.nearRange == 0.0f && q.farRange == 0.0f;
                }
                SA(r.switchState, a->GetState() == shown && SaLogIs(log) && placedOk &&
                                      g_uses == (told ? std::vector<int>{shown} : std::vector<int>{}) && sound &&
                                      s.states[static_cast<size_t>(shown - 1)]->IsOn());
            }
            // Sound 0 is a sound.
            a->m_sound = 0;
            reset();
            a->SetState(1);
            SA(r.switchState, g_soundRequests.size() == 1 && g_soundRequests[0].group == 0);
            a->m_quiet = false;
            a->m_sound = 9;
            g_gameRole = GAME_ROLE_SERVER;
            // A missing state frame: nothing after the lookup.
            {
                S_SwitchScene t = switchScene(2, false, false);
                auto *b = static_cast<C_Switch *>(creator->Create());
                b->SetFrame(t.root);
                b->m_sound = 4;
                t.root->SetData(&scriptData);
                reset();
                b->SetState(3);
                const std::string root = ItName(t.root);
                SA(r.switchState, b->GetState() == 3 && ItLogIs({"child " + root + " 1 ffff", "child " + root + " 3 ffff"}) &&
                                      g_uses == std::vector<int>{3} && g_soundRequests.empty());
                // Toggle: on to the next, back to 1 after the last frame.
                const int from[] = {1, 2};
                const int to[] = {2, 1};
                for (int i = 0; i < 2; ++i) {
                    b->m_state = from[i];
                    reset();
                    b->Toggle();
                    SA(r.switchState, b->GetState() == to[i] && g_uses == std::vector<int>{to[i]});
                }
                t.root->SetData(nullptr);
                creator->Destroy(b);
            }
            // "1" missing: only its lookup.
            {
                I3D_frame *bare = placed(FRAME_MODEL, "sa.bare", false);
                auto *b = static_cast<C_Switch *>(creator->Create());
                b->SetFrame(bare);
                reset();
                b->SetState(2);
                SA(r.switchState, b->GetState() == 2 && ItLogIs({"child " + ItName(bare) + " 1 ffff"}) && b->m_stateCount == 0);
                // Without a frame: the state only.
                auto *c = static_cast<C_Switch *>(creator->Create());
                reset();
                c->SetState(3);
                SA(r.switchState, c->GetState() == 3 && g_itLog.empty() && g_uses.empty());
                creator->Destroy(b);
                creator->Destroy(c);
            }
            // Toggle with four frames: 3 -> 4, clamped to 1; the last -> 1.
            {
                S_SwitchScene t = switchScene(4, false, false);
                auto *b = static_cast<C_Switch *>(creator->Create());
                {
                    // (Counted from 0 again on another frame.)
                    S_SwitchScene two = switchScene(2, false, false);
                    b->SetFrame(t.root);
                    b->SetFrame(two.root);
                    SA(r.switchState, b->m_stateCount == 2);
                }
                b->SetFrame(t.root);
                SA(r.switchState, b->m_stateCount == 4);
                b->m_state = 3;
                b->Toggle();
                SA(r.switchState, b->GetState() == 1);
                b->m_state = 4;
                b->Toggle();
                SA(r.switchState, b->GetState() == 1);
                b->m_state = 2;
                b->Toggle();
                SA(r.switchState, b->GetState() == 3);
                creator->Destroy(b);
            }
            // WriteState / ReadState: 8 bits.
            {
                a->SetState(3);
                g_streams.push_back(new S_FakeStream{0, false, {}, false});
                a->WriteState(reinterpret_cast<C_BitStream *>(g_streams.back()));
                const std::vector<std::pair<uint32_t, int>> e = {{3, 8}};
                SA(r.switchState, g_streams.back()->bits == e);
                SaRead({{2, 8}});
                reset();
                a->ReadState(nullptr);
                SA(r.switchState, SaReadAll() && a->GetState() == 2 && g_uses == std::vector<int>{2});
            }
            // CopyFrom: the state and the sound (a null source: nothing).
            {
                auto *b = static_cast<C_Switch *>(creator->Create());
                b->m_state = 3;
                b->m_sound = 21;
                b->m_stateCount = 7;
                b->m_initialState = 2;
                const int copies = g_actorCopies;
                a->CopyFrom(nullptr);
                SA(r.switchState, g_actorCopies == copies);
                const int count = a->m_stateCount, initial = a->m_initialState;
                a->CopyFrom(b);
                SA(r.switchState, g_actorCopies == copies + 1 && a->m_state == 3 && a->m_sound == 21 &&
                                      a->m_stateCount == count && a->m_initialState == initial);
                a->m_sound = 9;
                creator->Destroy(b);
            }

            // StartMission: the volume (the server side, or a client's), not quiet, the state kept.
            const struct {
                bool server;
                uint8_t role;
                bool volume;
            } starts[] = {{true, GAME_ROLE_SERVER, true}, {false, GAME_ROLE_CLIENT, true}, {false, 3, false},
                          {true, GAME_ROLE_LOCAL, true},  {false, GAME_ROLE_LOCAL, false}};
            for (const auto &k : starts) {
                g_gameRole = k.role;
                a->m_quiet = true;
                a->m_state = 2;
                a->m_initialState = 0;
                a->m_volume = S_CollisionVolume();
                a->m_volume.capsuleRadius = 3.0f;       // (a volume made again starts from zeros)
                a->m_volume.unknown54[0] = 7;
                a->m_volume.cell = reinterpret_cast<S_CollisionGridCell *>(&scriptData);
                reset();
                a->StartMission(k.server);
                if (k.volume)
                    SA(r.switchMission, g_volumeLog.size() == 2 && g_volumeLog[0].op == 'A' && g_volumeLog[1].op == 'M' &&
                                            g_volumeLog[0].volume == &a->m_volume && g_volumeLog[1].volume == &a->m_volume &&
                                            !a->m_quiet && a->m_initialState == 2 &&
                                            g_activations == std::vector<bool>{k.server});
                else
                    SA(r.switchMission, g_volumeLog.empty() && a->m_quiet && a->m_initialState == 0 &&
                                            g_activations == std::vector<bool>{k.server});
                if (k.volume)
                    SA(r.volume, SaSameVolume(a->m_volume, a->m_invMatrix, s.root, COLLISION_TYPE_SWITCH, a));
            }
            // Without a frame: only the activation.
            {
                auto *b = static_cast<C_Switch *>(creator->Create());
                g_gameRole = GAME_ROLE_SERVER;
                b->m_quiet = true;
                reset();
                b->StartMission(true);
                SA(r.switchMission, g_volumeLog.empty() && b->m_quiet && g_activations == std::vector<bool>{true});
                creator->Destroy(b);
            }
            // StopMission: the volume out; the server side back at the kept state (the script told).
            g_gameRole = GAME_ROLE_SERVER;
            s.root->SetData(&scriptData);
            a->m_quiet = false;
            for (int server = 0; server < 2; ++server) {
                a->m_initialState = 3;
                a->m_state = 1;
                reset();
                std::vector<std::string> log;
                if (server)
                    stateLog(s, 3, log);
                a->StopMission(server != 0);
                SA(r.switchMission, g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' && g_volumeLog[0].volume == &a->m_volume &&
                                        a->GetState() == (server ? 3 : 1) && g_itLog == log &&
                                        g_uses == (server ? std::vector<int>{3} : std::vector<int>{}) &&
                                        g_deactivations == std::vector<bool>{server != 0});
            }
            // RestartMission: quietly stopped and started.
            a->m_initialState = 2;
            a->m_state = 3;
            reset();
            a->RestartMission(false);
            SA(r.switchMission, g_uses.empty() && !a->m_quiet && a->GetState() == 2 && a->m_initialState == 2 &&
                                    g_volumeLog.size() == 3 && g_volumeLog[0].op == 'R' && g_volumeLog[1].op == 'A' &&
                                    g_deactivations == std::vector<bool>{true} && g_activations == std::vector<bool>{true});

            // SaveGame: the base's chunk, the state raw; LoadGame quietly (a short one: 0, that is 1).
            {
                g_actorSaveTag = 99;
                a->m_state = 3;
                bool saved = false;
                S_SaBytes e;
                e.Head(0x777, 20).Head(0x3039, 10).Val(99).Val(3);
                SA(r.switchSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == e.b && saved);
                a->m_state = 1;
                S_SaBytes in;
                in.Head(0x777, 20).Head(0x3039, 10).Val(98).Val(2);
                S_SaReader rd(in);
                reset();
                SA(r.switchSave, a->LoadGame(rd.c) && a->GetState() == 2 && g_actorLoads == std::vector<int>{98} &&
                                     g_uses.empty() && !a->m_quiet && !g_itLog.empty());
                S_SaBytes shortOne;
                shortOne.Head(0x777, 16).Head(0x3039, 10).Val(97);
                S_SaReader rd2(shortOne);
                reset();
                SA(r.switchSave, a->LoadGame(rd2.c) && a->GetState() == 1 && g_uses.empty() && !a->m_quiet);
                // Slots 0x24 / 0x28: the state raw; reading it tells the script.
                a->m_state = 2;
                S_SaBytes raw;
                raw.Head(0x777, 10).Val(2);
                SA(r.switchSave, SaWritten([&](C_chunk &c) { return a->Unknown24(c); }, &saved) == raw.b && saved);
                S_SaBytes in2;
                in2.Head(0x777, 10).Val(3);
                S_SaReader rd3(in2);
                reset();
                SA(r.switchSave, a->Unknown28(rd3.c) && a->GetState() == 3 && g_uses == std::vector<int>{3});
                S_SaBytes none;
                none.Head(0x777, 8).Val(static_cast<uint16_t>(1));
                S_SaReader rd4(none);
                a->m_state = 5;
                reset();
                SA(r.switchSave, a->Unknown28(rd4.c) && a->GetState() == 1 && g_uses == std::vector<int>{1});
            }
            // Properties: chunk 0 the state (set), 1 the sound; WriteProperties returns false; another
            // id entered and not left.
            {
                a->m_state = 2;
                a->m_sound = 13;
                bool written = true;
                S_SaBytes e;
                e.Head(0x777, 26).Head(0, 10).Val(2).Head(1, 10).Val(13);
                SA(r.switchProps, SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &written) == e.b && !written);
                S_SaBytes in;
                in.Head(0x777, 6 + 10 + 10 + 16).Head(1, 10).Val(4).Head(0, 10).Val(3).Head(9, 16).Head(1, 10).Val(8);
                S_SaReader rd(in);
                reset();
                SA(r.switchProps, a->ReadProperties(rd.c) && a->GetState() == 3 && a->m_sound == 8 &&
                                      g_uses == std::vector<int>{3} && rd.c.m_depth == 2);
                S_SaBytes empty;
                empty.Head(0x777, 6);
                S_SaReader rd2(empty);
                SA(r.switchProps, a->ReadProperties(rd2.c) && a->GetState() == 3);
                // Short chunks: 0 (the port's value for the original's stack value).
                S_SaBytes shortOnes;
                shortOnes.Head(0x777, 22).Head(0, 8).Val(static_cast<uint16_t>(3)).Head(1, 8).Val(static_cast<uint16_t>(5));
                S_SaReader rd3(shortOnes);
                SA(r.switchProps, a->ReadProperties(rd3.c) && a->GetState() == 1 && a->m_sound == 0);
            }
            s.root->SetData(nullptr);
            creator->Destroy(a);
        }
    }

    // --- the photo place --------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_PHOTO_PLACE];
        for (int k = 0; k < 3; ++k) {
            // Two boxes; nothing (an empty box); a box flat in x.
            I3D_frame *f = placed(FRAME_MODEL, "sa.photo", k != 0);   // (the boxes' frame: its own scale)
            if (k == 0) {
                boxFrame(f, "a", 0);
                boxFrame(f, "b", 2);
            } else if (k == 2) {
                RbBoxFrame(f, "flat", 0.5f, 0.5f, -1.0f, rnd(0, 2), -2.0f, rnd(0, 3), S_vector(rnd(-1, 1), 0, 0));
            }
            auto *a = static_cast<C_PhotoPlace *>(creator->Create());
            a->SetFrame(f);
            a->m_textId = 55;
            a->m_photographed = true;
            g_gameRole = GAME_ROLE_SERVER;
            reset();
            a->StartMission(true);
            SA(r.photo, a->m_started && !a->m_photographed && std::strcmp(*a->GetName(), "text 55") == 0 &&
                            g_volumeLog.size() == 1 && g_volumeLog[0].op == 'A' && g_volumeLog[0].volume == &a->m_volume &&
                            g_activations == std::vector<bool>{true});
            SA(r.volume, SaSameVolume(a->m_volume, a->m_invMatrix, f, COLLISION_TYPE_PLACE, a));
            // Started already: only the activation.
            a->TakePhoto(nullptr);
            a->m_textId = 56;
            reset();
            a->StartMission(false);
            SA(r.photo, a->GetState() == 1 && g_volumeLog.empty() && std::strcmp(*a->GetName(), "text 55") == 0 &&
                            g_activations == std::vector<bool>{false});
            reset();
            a->StopMission(false);
            SA(r.photo, !a->m_started && a->GetState() == 1 && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' &&
                            g_volumeLog[0].volume == &a->m_volume && g_deactivations == std::vector<bool>{false});
            reset();
            a->RestartMission(true);
            SA(r.photo, a->GetState() == 0 && g_activations.empty() && g_deactivations.empty() && g_volumeLog.empty());
            // CopyFrom: the text.
            auto *b = static_cast<C_PhotoPlace *>(creator->Create());
            b->m_textId = 77;
            const int copies = g_actorCopies;
            a->CopyFrom(b);
            SA(r.photo, a->m_textId == 77 && g_actorCopies == copies + 1);
            creator->Destroy(b);
            // GetMaterialId: the part's (-1 none), else the frame's, else 11.
            I3D_frame *part = AnFrame(FRAME_VISUAL, "sa.ppart", f);
            const int partIds[] = {4, -1, -1, -2, -1};
            const int frameIds[] = {6, 8, -1, 9, -3};
            for (int i = 0; i < 5; ++i) {
                g_frameMaterials[part] = partIds[i];
                g_frameMaterials[f] = frameIds[i];
                const int e = partIds[i] != -1 ? partIds[i] : frameIds[i] != -1 ? frameIds[i] : 11;
                SA(r.photo, a->GetMaterialId(part) == e);
                SA(r.photo, a->GetMaterialId(f) == (frameIds[i] != -1 ? frameIds[i] : 11));
            }
            g_frameMaterials.erase(part);
            g_frameMaterials.erase(f);
            // Properties: chunk 100 the text (another id entered and not left; a short one not read).
            a->m_textId = 31;
            bool written = false;
            S_SaBytes e;
            e.Head(0x777, 16).Head(100, 10).Val(31);
            SA(r.photoSave, SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &written) == e.b && written);
            S_SaBytes in;
            in.Head(0x777, 32).Head(100, 10).Val(41).Head(3, 16).Head(100, 10).Val(42);
            S_SaReader rd(in);
            SA(r.photoSave, a->ReadProperties(rd.c) && a->m_textId == 42 && rd.c.m_depth == 2);
            S_SaBytes shortOne;
            shortOne.Head(0x777, 14).Head(100, 8).Val(static_cast<uint16_t>(5));
            S_SaReader rd2(shortOne);
            SA(r.photoSave, a->ReadProperties(rd2.c) && a->m_textId == 42);
            // The saved game: chunk 3000, the flag as a byte (any nonzero byte read as set).
            a->m_photographed = true;
            S_SaBytes sv;
            sv.Head(0x777, 13).Head(3000, 7).Val(static_cast<uint8_t>(1));
            bool saved = false;
            SA(r.photoSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
            const uint8_t bytes[] = {0, 2, 1};
            for (uint8_t v : bytes) {
                S_SaBytes l;
                l.Head(0x777, 19).Head(4, 13).Head(3000, 7).Val(v);
                S_SaReader rl(l);
                a->m_photographed = v == 0;
                SA(r.photoSave, a->LoadGame(rl.c) && a->GetState() == (v != 0) && rl.c.m_depth == 2);
            }
            S_SaBytes empty;
            empty.Head(0x777, 12).Head(3000, 6);
            S_SaReader re(empty);
            SA(r.photoSave, a->LoadGame(re.c) && a->GetState() == 1);
            creator->Destroy(a);
        }
    }

    g_gameRole = oldRole;
    g_textIds = false;
    for (int i = 0; i < 5; ++i)
        g_frameVtable[slots[i] / 4] = oldFrame[i];
    reset();
    std::printf("simple: creators %d clock %d clockMission %d clockSave %d null %d wire %d wireMission %d wireNet %d "
                "wireSave %d switchFrame %d switchState %d switchMission %d switchSave %d switchProps %d photo %d "
                "photoSave %d volume %d\n",
                r.creators, r.clock, r.clockMission, r.clockSave, r.null, r.wire, r.wireMission, r.wireNet, r.wireSave,
                r.switchFrame, r.switchState, r.switchMission, r.switchSave, r.switchProps, r.photo, r.photoSave,
                r.volume);
}
