// Part 8's world modules (WORLD_TEST): game/vertex_anim.cpp, lightmap_groups.cpp and tshift.cpp
// (8a). Each argument names a mission ("mission NAME" in a .txt file): the vertex animations load
// tables\Vertanim.def and that mission's Vertanim.bin from H:/HD2_Unpacked (WORLD_DATA: another
// folder) into the harness's memory files; the rest are made up.
//
// Prints per mission "== NAME" and the lines vanim:, lmaps:, tshift: with 1 per check met.

extern bool g_dtaArchivesFirst;                        // (car_stubs.cpp: dtaOpen's last flag)

namespace {

std::string g_wmData = "H:/HD2_Unpacked";
std::vector<std::string> g_wmLog;
bool g_wmTrace;

void WmLog(const char *format, ...)
{
    char text[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_wmLog.push_back(text);
}
int WmLogged(const std::string &line)
{
    return static_cast<int>(std::count(g_wmLog.begin(), g_wmLog.end(), line));
}
std::string ItName(const I3D_frame *f)
{
    if (!f)
        return "0";
    auto it = g_stubs.find(f);
    return it == g_stubs.end() ? "?" : it->second->name;
}
bool WmLogIs(std::initializer_list<std::string> expected)
{
    return std::vector<std::string>(expected) == g_wmLog;
}

#define WM(flag, cond)                                                                                     \
    do {                                                                                                   \
        if (!(cond)) {                                                                                     \
            (flag) = false;                                                                                \
            if (g_wmTrace)                                                                                 \
                std::printf("  failed line %d: %s\n", __LINE__, #cond);                                    \
        }                                                                                                  \
    } while (0)

bool WmReadFile(const std::string &path, std::vector<uint8_t> &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    out.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// --- the engine: visuals of their own types ----------------------------------------------------------

void *g_wmObjVtable[SLOTS], *g_wmLmapVtable[SLOTS], *g_wmDummyVtable[SLOTS];
std::map<const I3D_frame *, int> g_wmRefreshes;
std::map<std::string, I3D_frame *> g_wmScene, g_wmModels;
std::vector<uint32_t> g_wmFindMasks;

uint32_t __stdcall F_WmObjType(I3D_frame *) { return I3DVISUAL_OBJECT; }
uint32_t __stdcall F_WmLmapType(I3D_frame *) { return I3DVISUAL_LMAP; }
uint32_t __stdcall F_WmDummyType(I3D_frame *) { return 0; }
void __stdcall F_WmRefreshLevels(I3D_frame *self) { ++g_wmRefreshes[self]; }
void __stdcall F_WmSelectLightmap(I3D_frame *self, int index)
{
    WmLog("select %s %d", ItName(self).c_str(), index);
    *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(self) + 0x210) = index;
}
void __stdcall F_WmUpdate(I3D_frame *self) { WmLog("update %s", ItName(self).c_str()); }
uint32_t __stdcall F_WmRelease(I3D_frame *self)
{
    WmLog("release %s", ItName(self).c_str());
    return static_cast<uint32_t>(--self->m_refCount);
}
I3D_frame *__stdcall F_WmSceneFind(void *, const char *name, uint32_t mask)
{
    g_wmFindMasks.push_back(mask);
    auto it = g_wmScene.find(name);
    return it == g_wmScene.end() ? nullptr : it->second;
}
I3D_frame *__stdcall F_WmDriverFind(void *, I3D_scene *, const char *name)
{
    auto it = g_wmModels.find(name);
    return it == g_wmModels.end() ? nullptr : it->second;
}

void WmInitVtables()
{
    for (int i = 0; i < SLOTS; ++i)
        g_wmObjVtable[i] = g_wmLmapVtable[i] = g_wmDummyVtable[i] = g_frameVtable[i];
    g_wmObjVtable[0x04 / 4] = reinterpret_cast<void *>(&F_WmObjType);
    g_wmObjVtable[0xB0 / 4] = reinterpret_cast<void *>(&F_WmRefreshLevels);
    g_wmLmapVtable[0x04 / 4] = reinterpret_cast<void *>(&F_WmLmapType);
    g_wmLmapVtable[0xC8 / 4] = reinterpret_cast<void *>(&F_WmSelectLightmap);
    g_wmLmapVtable[0x24 / 4] = reinterpret_cast<void *>(&F_WmUpdate);
    g_wmLmapVtable[0x00 / 4] = reinterpret_cast<void *>(&F_WmRelease);
    g_wmDummyVtable[0x04 / 4] = reinterpret_cast<void *>(&F_WmDummyType);
}

I3D_frame *WmFrame(void **vtable, const std::string &name)
{
    I3D_frame *f = NewFrame(FRAME_VISUAL, name, nullptr);
    *reinterpret_cast<void ***>(f) = vtable;
    f->m_name = g_stubs[f]->name.c_str();
    return f;
}

template <class T> T &WmField(void *base, int offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(base) + offset);
}

// An object visual with a mesh of `levels` detail levels of `count` vertices each (texture
// coordinates 7 and 8, the rest 0).
I3D_frame *WmObject(const std::string &name, int levels, int count)
{
    I3D_frame *f = WmFrame(g_wmObjVtable, name);
    auto *mesh = new S_Mesh();
    mesh->vtable = g_meshVtable;
    mesh->levelCount = static_cast<uint32_t>(levels);
    mesh->refs = 1;
    for (int l = 0; l < levels; ++l) {
        S_Level *level = NewLevel();
        level->count = count;
        level->own.assign(static_cast<size_t>(count) * 8, 0.0f);
        for (int v = 0; v < count; ++v) {
            level->own[v * 8 + 6] = 7.0f;
            level->own[v * 8 + 7] = 8.0f;
        }
        level->data = level->own.data();
        mesh->levels.push_back(level);
    }
    WmField<S_Mesh *>(f, 0x1E0) = mesh;
    return f;
}
S_Level *WmLevel(I3D_frame *f, int level)
{
    return WmField<S_Mesh *>(f, 0x1E0)->levels[static_cast<size_t>(level)];
}

// --- the vertex animations -----------------------------------------------------------------------------

S_DetailSettings g_wmDetails;

// An animation parsed here (the test's own reading of Vertanim.def).
struct S_WmAnimation {
    std::string name;
    int lastFrame = 0, levels = 0;
    std::vector<int> counts;
    std::vector<std::vector<const uint8_t *>> frames;   // [frame][level] -> the file's vertices
};
std::vector<S_WmAnimation> WmParseDef(const std::vector<uint8_t> &d)
{
    std::vector<S_WmAnimation> out;
    size_t p = 4;
    while (p + 28 <= d.size()) {
        S_WmAnimation a;
        a.name = std::string(reinterpret_cast<const char *>(&d[p]), strnlen(reinterpret_cast<const char *>(&d[p]), 20));
        std::memcpy(&a.lastFrame, &d[p + 20], 4);
        std::memcpy(&a.levels, &d[p + 24], 4);
        p += 28;
        for (int l = 0; l < a.levels; ++l) {
            int c;
            std::memcpy(&c, &d[p], 4);
            a.counts.push_back(c);
            p += 4;
        }
        for (int f = 0; f <= a.lastFrame; ++f) {
            a.frames.emplace_back();
            for (int l = 0; l < a.levels; ++l) {
                a.frames.back().push_back(&d[p]);
                p += static_cast<size_t>(a.counts[l]) * 32;
            }
        }
        if (p > d.size())
            break;
        out.push_back(a);
    }
    return out;
}
bool WmSameAnimation(const S_VertexAnimation *a, const S_WmAnimation &w)
{
    if (!a || w.name != a->name || a->lastFrame != w.lastFrame || a->levelCount != w.levels)
        return false;
    for (int l = 0; l < w.levels; ++l) {
        if (a->vertexCounts[l] != w.counts[l])
            return false;
    }
    for (int f = 0; f <= w.lastFrame; ++f) {
        for (int l = 0; l < w.levels; ++l) {
            const S_VertexAnimVertex *v = a->frames[f][l];
            if ((reinterpret_cast<uintptr_t>(v) & 15) != 0 ||
                std::memcmp(v, w.frames[f][l], static_cast<size_t>(w.counts[l]) * 32) != 0)
                return false;
        }
    }
    return true;
}

// A made-up animation: `levels` levels of `count` vertices, vertex v of frame f at level l at
// (f + v, 10 l + v, -f) with the normal (v, f, l) and the 4th components 99 (never copied).
S_VertexAnimation *WmMakeAnimation(const char *name, int lastFrame, int levels, int count)
{
    auto *a = new S_VertexAnimation;
    std::strncpy(a->name, name, sizeof a->name);
    a->lastFrame = lastFrame;
    a->levelCount = levels;
    a->vertexCounts = new int[levels];
    for (int l = 0; l < levels; ++l)
        a->vertexCounts[l] = count;
    a->frames = new S_VertexAnimVertex **[lastFrame + 1];
    for (int f = 0; f <= lastFrame; ++f) {
        a->frames[f] = new S_VertexAnimVertex *[levels];
        for (int l = 0; l < levels; ++l) {
            auto *v = static_cast<S_VertexAnimVertex *>(_aligned_malloc(count * sizeof(S_VertexAnimVertex), 16));
            for (int i = 0; i < count; ++i) {
                v[i].pos = S_vector4{static_cast<float>(f + i) + 0.25f, static_cast<float>(10 * l + i) + 0.5f,
                                     -static_cast<float>(f) - 0.125f, 99.0f};
                v[i].normal = S_vector4{static_cast<float>(i) + 0.75f, static_cast<float>(f) * 3.0f + 0.375f,
                                        static_cast<float>(l) + 0.0625f, 99.0f};
            }
            a->frames[f][l] = v;
        }
    }
    return a;
}
// Whether level `level` of the visual holds the blend of frames `from` and `to` by t (the texture
// coordinates kept).
bool WmBlended(I3D_frame *visual, int level, const S_VertexAnimation *a, int from, int to, float t)
{
    const S_Level *lv = WmLevel(visual, level);
    const float s = 1.0f - t;
    for (int i = 0; i < a->vertexCounts[level]; ++i) {
        const S_VertexAnimVertex &p = a->frames[from][level][i], &q = a->frames[to][level][i];
        const float want[8] = {p.pos.x * s + q.pos.x * t,       p.pos.y * s + q.pos.y * t,
                               p.pos.z * s + q.pos.z * t,       p.normal.x * s + q.normal.x * t,
                               p.normal.y * s + q.normal.y * t, p.normal.z * s + q.normal.z * t,
                               7.0f,                            8.0f};
        if (std::memcmp(want, lv->data + i * 8, sizeof want) != 0)
            return false;
    }
    return lv->locks == 0;
}
bool WmUntouched(I3D_frame *visual, int level)
{
    const S_Level *lv = WmLevel(visual, level);
    for (int i = 0; i < lv->count * 8; ++i) {
        if (lv->data[i] != (i % 8 == 6 ? 7.0f : i % 8 == 7 ? 8.0f : 0.0f))
            return false;
    }
    return true;
}
void WmClear(I3D_frame *visual)
{
    for (S_Level *lv : WmField<S_Mesh *>(visual, 0x1E0)->levels) {
        for (int i = 0; i < lv->count * 8; ++i)
            lv->data[i] = i % 8 == 6 ? 7.0f : i % 8 == 7 ? 8.0f : 0.0f;
    }
}

// A Vertanim.bin record.
struct S_WmRecord {
    int maxLevel;
    std::string frame;
    int startFrame, time, frameTime;
    char pingPong;
    std::string animation;
    int cull;
};
std::vector<uint8_t> WmBin(int version, const std::vector<S_WmRecord> &records)
{
    std::vector<uint8_t> out(4);
    std::memcpy(out.data(), &version, 4);
    auto put = [&](const void *p, size_t n) {
        out.insert(out.end(), static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
    };
    for (const S_WmRecord &r : records) {
        char name[0x80] = {}, anim[0x14] = {};
        std::strncpy(name, r.frame.c_str(), sizeof name);
        std::strncpy(anim, r.animation.c_str(), sizeof anim);
        put(&r.maxLevel, 4);
        put(name, sizeof name);
        put(&r.startFrame, 4);
        put(&r.time, 4);
        put(&r.frameTime, 4);
        put(&r.pingPong, 1);
        put(anim, sizeof anim);
        if (version != 0)
            put(&r.cull, 4);
    }
    return out;
}

bool WmPlayerIs(const S_VertexAnimPlayer *p, const S_WmRecord &r, I3D_frame *visual, const S_VertexAnimation *a, int version)
{
    return p->maxLevel == r.maxLevel && p->visual == visual && p->startFrame == r.startFrame && p->time == 0 &&
           p->frameTime == r.frameTime && p->pingPong == (r.pingPong != 0) && p->animation == a &&
           p->cull == (version != 0 ? r.cull : 1) && p->frame == 0 && p->nextFrame == 1 && !p->backwards &&
           !p->stopped && p->loop && p->distant == 0;
}

}  // namespace

S_DetailSettings *C_GameProfile::GetDetailSettings()
{
    return &g_wmDetails;
}

struct S_VanimResults {
    bool vaLoad = true, vaCut = true, vaDone = true, vaFind = true, vaPlayers = true, vaBin = true, vaBlend = true,
         vaReset = true, vaUpdate = true, vaPing = true, vaTick = true, vaStart = true, vaStop = true, vaRemove = true;
};

void VertexAnimTest(const std::string &mission, S_VanimResults &r)
{
    C_VertexAnimModule &m = g_vertexAnimModule;
    I3D_scene *scene = GetGame()->m_scene;
    const uint32_t oldRole = g_gameRole;
    g_gameRole = GAME_ROLE_LOCAL;
    g_wmDetails.switches[DETAIL_VERTEX_ANIMATION] = true;

    // --- Tables\Vertanim.def: every animation as the file has it, 16-byte aligned --------------------
    std::vector<uint8_t> def;
    const bool haveDef = WmReadFile(g_wmData + "/Tables/Vertanim.def", def);
    WM(r.vaLoad, haveDef);
    const std::vector<S_WmAnimation> parsed = WmParseDef(def);
    g_files["Tables\\Vertanim.def"] = def;
    g_dtaArchivesFirst = true;
    m.Init();
    WM(r.vaLoad, !parsed.empty() && m.m_animations.size() == parsed.size() && m.m_started == 0 && !g_dtaArchivesFirst);
    for (size_t i = 0; i < parsed.size() && i < m.m_animations.size(); ++i)
        WM(r.vaLoad, WmSameAnimation(m.m_animations[i], parsed[i]));
    // Again: the old ones replaced (not added to); m_started back to 0.
    m.m_started = 1;
    m.Init();
    WM(r.vaLoad, m.m_animations.size() == parsed.size() && m.m_started == 0);
    // The server alone: nothing (the animations kept).
    g_gameRole = GAME_ROLE_SERVER;
    m.m_started = 1;
    g_files["Tables\\Vertanim.def"] = std::vector<uint8_t>(4, 0);
    m.Init();
    WM(r.vaLoad, m.m_animations.size() == parsed.size() && m.m_started == 1);
    g_gameRole = GAME_ROLE_LOCAL;
    // The header alone: none.
    m.Init();
    WM(r.vaLoad, m.m_animations.empty());
    // No file: the old ones kept.
    g_files["Tables\\Vertanim.def"] = def;
    m.Init();
    g_files.erase("Tables\\Vertanim.def");
    m.Init();
    WM(r.vaLoad, m.m_animations.size() == parsed.size());

    // Files cut short: the animations before the cut (each place of the first two animations'
    // records: the name, the counts, the vertex counts, the vertices).
    if (parsed.size() >= 2) {
        const size_t first = 28 + parsed[0].counts.size() * 4 +
                             static_cast<size_t>(parsed[0].lastFrame + 1) * parsed[0].counts[0] * 32;
        const size_t cuts[] = {4 + 10, 4 + 21, 4 + 26, 4 + 30, 4 + 33, 4 + 1000, 4 + first - 1, 4 + first,
                               4 + first + 5, 4 + first + 30, 4 + first + 40};
        for (size_t cut : cuts) {
            g_files["Tables\\Vertanim.def"] = std::vector<uint8_t>(def.begin(), def.begin() + static_cast<long>(cut));
            m.Init();
            // (A read that gets a part counts: cut in the last frame's vertices, the animation is kept,
            // its last bytes not read.)
            const size_t want = cut >= 4 + first - 1 ? 1 : 0;
            WM(r.vaCut, m.m_animations.size() == want &&
                            (want == 0 || cut == 4 + first - 1 || WmSameAnimation(m.m_animations[0], parsed[0])));
        }
        // A level of no vertices ends the loading (its read gets nothing).
        std::vector<uint8_t> zero(def.begin(), def.begin() + static_cast<long>(4 + first));
        const char head[20] = "zero";
        const int zeroLast = 0, zeroLevels = 1, zeroCount = 0;
        zero.insert(zero.end(), head, head + 20);
        zero.insert(zero.end(), reinterpret_cast<const uint8_t *>(&zeroLast), reinterpret_cast<const uint8_t *>(&zeroLast) + 4);
        zero.insert(zero.end(), reinterpret_cast<const uint8_t *>(&zeroLevels), reinterpret_cast<const uint8_t *>(&zeroLevels) + 4);
        zero.insert(zero.end(), reinterpret_cast<const uint8_t *>(&zeroCount), reinterpret_cast<const uint8_t *>(&zeroCount) + 4);
        zero.insert(zero.end(), def.begin() + static_cast<long>(4 + first), def.end());
        g_files["Tables\\Vertanim.def"] = zero;
        m.Init();
        WM(r.vaCut, m.m_animations.size() == 1);
    }
    g_files["Tables\\Vertanim.def"] = def;
    m.Init();

    // Nothing saved: both report success.
    {
        C_chunk none;
        WM(r.vaLoad, m.SaveGame(none) && m.LoadGame(none));
    }

    // --- FindAnimation: by the exact name ------------------------------------------------------------
    for (size_t i = 0; i < parsed.size() && i < m.m_animations.size(); ++i) {
        WM(r.vaFind, m.FindAnimation(C_str(parsed[i].name.c_str())) == m.m_animations[i]);
        std::string upper = parsed[i].name;
        for (char &c : upper)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        WM(r.vaFind, upper == parsed[i].name || !m.FindAnimation(C_str(upper.c_str())));
        WM(r.vaFind, !m.FindAnimation(C_str((parsed[i].name + "x").c_str())));
        if (parsed[i].name.size() > 1)
            WM(r.vaFind, !m.FindAnimation(C_str(parsed[i].name.substr(0, parsed[i].name.size() - 1).c_str())));
    }
    WM(r.vaFind, !m.FindAnimation(C_str()));

    // --- the mission's Vertanim.bin -------------------------------------------------------------------
    void *const oldFind = g_sceneVtable[0x7C / 4];
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_WmSceneFind);
    g_wmScene.clear();
    std::vector<uint8_t> bin;
    const std::string binPath = "Missions\\" + mission + "\\Vertanim.bin";
    GetGame()->m_missionName = mission.c_str();
    if (WmReadFile(g_wmData + "/MISSIONS/" + mission + "/Vertanim.bin", bin)) {
        g_files[binPath] = bin;
        int version;
        std::memcpy(&version, bin.data(), 4);
        const size_t size = 4 + 0x80 + 13 + 0x14 + (version ? 4 : 0);
        std::vector<S_WmRecord> records;
        std::vector<I3D_frame *> visuals;
        for (size_t p = 4; p + size <= bin.size(); p += size) {
            S_WmRecord rec;
            std::memcpy(&rec.maxLevel, &bin[p], 4);
            rec.frame = reinterpret_cast<const char *>(&bin[p + 4]);
            std::memcpy(&rec.startFrame, &bin[p + 0x84], 4);
            std::memcpy(&rec.time, &bin[p + 0x88], 4);
            std::memcpy(&rec.frameTime, &bin[p + 0x8C], 4);
            rec.pingPong = static_cast<char>(bin[p + 0x90]);
            rec.animation = std::string(reinterpret_cast<const char *>(&bin[p + 0x91]), strnlen(reinterpret_cast<const char *>(&bin[p + 0x91]), 0x14));
            rec.cull = 1;
            if (version)
                std::memcpy(&rec.cull, &bin[p + 0xA5], 4);
            records.push_back(rec);
            if (!g_wmScene.count(rec.frame))
                g_wmScene[rec.frame] = WmObject(rec.frame, 2, 2000);
            visuals.push_back(g_wmScene[rec.frame]);
        }
        g_wmFindMasks.clear();
        g_dtaArchivesFirst = true;
        m.LoadMissionData(false);
        WM(r.vaBin, !g_dtaArchivesFirst);
        // (A record of an animation the table lacks is dropped: Brest's water.)
        std::vector<size_t> kept;
        for (size_t i = 0; i < records.size(); ++i) {
            if (m.FindAnimation(C_str(records[i].animation.c_str())))
                kept.push_back(i);
        }
        WM(r.vaBin, !records.empty() && m.m_players.size() == kept.size() &&
                        std::count(g_wmFindMasks.begin(), g_wmFindMasks.end(), FRAME_MASK_ALL) == static_cast<long>(records.size()));
        for (size_t k = 0; k < kept.size() && k < m.m_players.size(); ++k) {
            const size_t i = kept[k];
            WM(r.vaBin, WmPlayerIs(m.m_players[k], records[i], visuals[i], m.FindAnimation(C_str(records[i].animation.c_str())), version));
        }
        m.FreeMissionData(false);
        WM(r.vaBin, m.m_players.empty());
    } else {
        // No file: no players.
        g_files.erase(binPath);
        m.LoadMissionData(false);
        WM(r.vaBin, m.m_players.empty());
    }

    // Made-up files: version 0 and 1, an unknown animation, a missing frame and a dummy (the rest of
    // their record read as the next one), cut short anywhere.
    const std::string anim0 = parsed.empty() ? "" : parsed[0].name;
    const S_VertexAnimation *a0 = m.FindAnimation(C_str(anim0.c_str()));
    I3D_frame *va = WmObject("wm_va", 2, 2000), *vb = WmObject("wm_vb", 2, 2000), *vd = WmFrame(g_wmDummyVtable, "wm_vd");
    g_wmScene["wm_va"] = va;
    g_wmScene["wm_vb"] = vb;
    g_wmScene["wm_vd"] = vd;
    const S_WmRecord ra = {1, "wm_va", -1, 77, 310, 1, anim0, 0}, rb = {0, "wm_vb", 3, 5, 120, 0, anim0, 1},
                     rn = {0, "wm_vb", 0, 0, 100, 0, "nosuch", 1};
    for (int version : {0, 1}) {
        g_files[binPath] = WmBin(version, {ra, rn, rb});
        m.LoadMissionData(false);
        WM(r.vaPlayers, m.m_players.size() == 2 && WmPlayerIs(m.m_players[0], ra, va, a0, version) &&
                            WmPlayerIs(m.m_players[1], rb, vb, a0, version));
        // Loaded again: the old players replaced.
        m.LoadMissionData(false);
        WM(r.vaPlayers, m.m_players.size() == 2);
        // The server side, or a server alone: not loaded (the players kept).
        g_files[binPath] = WmBin(version, {rb});
        m.LoadMissionData(true);
        g_gameRole = GAME_ROLE_SERVER;
        m.LoadMissionData(false);
        m.FreeMissionData(false);
        WM(r.vaPlayers, m.m_players.size() == 2);
        g_gameRole = GAME_ROLE_LOCAL;
        m.FreeMissionData(true);
        WM(r.vaPlayers, m.m_players.size() == 2);
        m.FreeMissionData(false);
        WM(r.vaPlayers, m.m_players.empty());
    }
    // A missing frame: its next 4 bytes are the next record's maximal level, its name from there.
    {
        std::vector<uint8_t> file = WmBin(1, {{0, "wm_none", 0, 0, 0, 0, "", 0}});
        const int wantLevel = 0x12345;
        std::memcpy(&file[4 + 0x84], &wantLevel, 4);   // (the missing record's startFrame)
        const char name[] = "wm_vb";
        // Its time, frame time, ping-pong, animation and cull: the next record's name.
        file.resize(4 + 0x88);
        file.insert(file.end(), name, name + sizeof name);
        file.resize(4 + 0x88 + 0x80, 0);
        const int startFrame = 4, time = 9, frameTime = 140, cull = 0;
        const char pingPong = 1;
        char anim[0x14] = {};
        std::strncpy(anim, anim0.c_str(), sizeof anim);
        auto put = [&](const void *p, size_t n) {
            file.insert(file.end(), static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
        };
        put(&startFrame, 4);
        put(&time, 4);
        put(&frameTime, 4);
        put(&pingPong, 1);
        put(anim, sizeof anim);
        put(&cull, 4);
        g_files[binPath] = file;
        m.LoadMissionData(false);
        WM(r.vaPlayers, m.m_players.size() == 1 &&
                            WmPlayerIs(m.m_players[0], {wantLevel, "", startFrame, 0, frameTime, pingPong, "", cull}, vb, a0, 1));
        // A dummy instead (not an object visual): the same.
        file.erase(file.begin() + 4 + 4, file.begin() + 4 + 4 + 8);
        const char dummy[8] = "wm_vd";
        file.insert(file.begin() + 4 + 4, dummy, dummy + 8);
        g_files[binPath] = file;
        m.LoadMissionData(false);
        WM(r.vaPlayers, m.m_players.size() == 1 && m.m_players[0]->visual == vb && m.m_players[0]->maxLevel == wantLevel);
        m.FreeMissionData(false);
    }
    {
        const std::vector<uint8_t> whole = WmBin(1, {ra, rb});
        const size_t one = whole.size() / 2 - 2;
        for (size_t cut : {size_t(0), size_t(2), size_t(4), size_t(6), size_t(4 + 0x60), size_t(4 + 0x86),
                           size_t(4 + 0x8A), size_t(4 + 0x8E), size_t(4 + 0x90), size_t(4 + 0x91), size_t(4 + 0xA0),
                           size_t(4 + 0xA6), one + 4, one + 4 + 2}) {
            g_files[binPath] = std::vector<uint8_t>(whole.begin(), whole.begin() + static_cast<long>(cut));
            m.LoadMissionData(false);
            // (A part of the last field read counts.)
            const size_t want = cut >= 4 + 0xA6 ? 1 : 0;
            WM(r.vaPlayers, m.m_players.size() == want && (want == 0 || m.m_players[0]->visual == va));
        }
        g_files.erase(binPath);
        m.LoadMissionData(false);
        WM(r.vaPlayers, m.m_players.size() == 1);
        m.FreeMissionData(false);
        if (!anim0.empty()) {
            const std::string nearName = "x" + anim0.substr(1);
            I3D_frame *vx = WmObject(nearName, 2, 2000);
            g_wmScene[nearName] = vx;
            const std::vector<uint8_t> v0 = WmBin(0, {{2, nearName, 1, 2, 90, 0, anim0, 1}});
            g_files[binPath] = std::vector<uint8_t>(v0.begin(), v0.begin() + 4 + 0x91 + 1);
            m.LoadMissionData(false);
            WM(r.vaPlayers, m.m_players.size() == 1 && m.m_players[0]->visual == vx && m.m_players[0]->animation == a0 &&
                                m.m_players[0]->frameTime == 90 && m.m_players[0]->cull == 1);
            m.FreeMissionData(false);
        }
    }

    // --- a player: the blend of its frames ------------------------------------------------------------
    S_VertexAnimation *made = WmMakeAnimation("made", 4, 3, 5);
    I3D_frame *pv = WmObject("wm_pv", 3, 5);
    S_VertexAnimPlayer p;
    p.Init(pv, made);
    WM(r.vaBlend, p.visual == pv && p.animation == made && p.frame == 0 && p.nextFrame == 1 && p.time == 0 &&
                      !p.backwards && p.distant == 0 && p.frameTime == 250 && p.loop && !p.stopped && !p.pingPong &&
                      p.cull == 1 && p.maxLevel == 0 && p.startFrame == 0);
    p.backwards = true;
    p.distant = 1;
    p.frame = 3;
    p.nextFrame = 2;
    p.time = 6;
    p.Init(pv, made);
    WM(r.vaBlend, p.frame == 0 && p.nextFrame == 1 && p.time == 0 && !p.backwards && p.distant == 0);
    p.cull = 0;
    p.maxLevel = 2;
    p.frameTime = 100;
    int stamp = 0;
    auto drawLevel = [&](int level, bool textureLod) {
        WmField<int>(pv, 0x1E4) = level;
        uint32_t &flags = WmField<uint32_t>(pv, 0x1D0);
        flags = textureLod ? flags | VISFLAGS_TEXTURE_LOD : flags & ~VISFLAGS_TEXTURE_LOD;
    };
    drawLevel(1, false);
    g_wmRefreshes.clear();
    p.Update(40);
    WM(r.vaBlend, p.time == 40 && p.frame == 0 && p.nextFrame == 1 && WmBlended(pv, 1, made, 0, 1, 0.4f) &&
                      WmUntouched(pv, 0) && WmUntouched(pv, 2) && g_wmRefreshes[pv] == 1);
    WmClear(pv);
    // A texture-LOD object: level 0 whatever its +0x1E4.
    drawLevel(2, true);
    p.Update(30);
    WM(r.vaBlend, p.time == 70 && WmBlended(pv, 0, made, 0, 1, 0.7f) && WmUntouched(pv, 1) && WmUntouched(pv, 2) &&
                      g_wmRefreshes[pv] == 2);
    WmClear(pv);
    // A level past the maximal one: nothing written.
    drawLevel(2, false);
    p.maxLevel = 1;
    p.Update(10);
    WM(r.vaBlend, p.time == 80 && WmUntouched(pv, 2) && g_wmRefreshes[pv] == 2);
    p.maxLevel = 2;
    p.Update(0);
    WM(r.vaBlend, WmBlended(pv, 2, made, 0, 1, 0.8f) && g_wmRefreshes[pv] == 3);
    WmClear(pv);
    {
        S_VertexAnimation *two = WmMakeAnimation("two", 4, 3, 5);
        two->levelCount = 2;                            // (level 2 there: the would-be data past it)
        two->vertexCounts[2] = 3;
        S_VertexAnimation *keep = p.animation;
        p.animation = two;
        p.Update(0);
        WM(r.vaBlend, WmUntouched(pv, 2) && g_wmRefreshes[pv] == 4 && WmLevel(pv, 2)->locks == 0);
        p.animation = keep;
        g_wmRefreshes[pv] = 3;
        two->levelCount = 3;
        delete two;
    }
    // Culled: only after the visual was drawn in the scene's last frame.
    p.cull = 1;
    WmField<int>(scene, 0x11C) = 41;
    WmField<int>(pv, 0x11C) = 40;
    p.Update(5);
    WM(r.vaBlend, p.time == 85 && WmUntouched(pv, 2) && g_wmRefreshes[pv] == 3);
    WmField<int>(pv, 0x11C) = 41;
    p.Update(5);
    WM(r.vaBlend, p.time == 90 && WmBlended(pv, 2, made, 0, 1, 0.9f) && g_wmRefreshes[pv] == 4);
    WmClear(pv);
    (void)stamp;
    // No visual, no animation, stopped: nothing.
    p.visual = nullptr;
    p.Update(5);
    p.visual = pv;
    p.animation = nullptr;
    p.Update(5);
    p.animation = made;
    p.stopped = true;
    p.Update(5);
    WM(r.vaBlend, p.time == 90 && WmUntouched(pv, 2) && g_wmRefreshes[pv] == 4);
    p.stopped = false;

    // --- round: the frames step at the frame time (exactly it: not yet), wrap, the end ---------------
    p.Update(10);
    WM(r.vaUpdate, p.time == 100 && p.frame == 0 && p.nextFrame == 1 && WmBlended(pv, 2, made, 0, 1, 1.0f));
    p.Update(1);
    WM(r.vaUpdate, p.time == 1 && p.frame == 1 && p.nextFrame == 2 && WmBlended(pv, 2, made, 1, 2, 0.01f));
    // Over two frame times in one: the time back to 0 (one step only).
    p.Update(250);
    WM(r.vaUpdate, p.time == 0 && p.frame == 2 && p.nextFrame == 3 && WmBlended(pv, 2, made, 2, 3, 0.0f));
    p.Update(200);
    WM(r.vaUpdate, p.time == 100 && p.frame == 3 && p.nextFrame == 4);
    p.Update(50);
    WM(r.vaUpdate, p.time == 50 && p.frame == 4 && p.nextFrame == 0 && WmBlended(pv, 2, made, 4, 0, 0.5f));
    // (Over the frame time once more after the step: back to 0.)
    p.frame = 3;
    p.nextFrame = 4;
    p.time = 100;
    p.Update(150);
    WM(r.vaUpdate, p.time == 0 && p.frame == 4 && p.nextFrame == 0 && WmBlended(pv, 2, made, 4, 0, 0.0f));
    p.time = 50;
    p.Update(100);
    WM(r.vaUpdate, p.time == 50 && p.frame == 0 && p.nextFrame == 1);
    // Without loop: past the last frame it stops at its end.
    p.frame = 3;
    p.nextFrame = 4;
    p.loop = false;
    p.Update(100);
    WM(r.vaUpdate, p.frame == 4 && p.nextFrame == 0 && !p.stopped && p.time == 50);
    WmClear(pv);
    const int refreshes = g_wmRefreshes[pv];
    p.Update(60);
    WM(r.vaUpdate, p.stopped && p.frame == 4 && p.nextFrame == 0 && p.time == 100 && WmUntouched(pv, 2) &&
                       g_wmRefreshes[pv] == refreshes);
    p.Update(500);
    WM(r.vaUpdate, p.stopped && p.time == 100 && p.frame == 4);
    p.stopped = false;
    p.loop = true;

    // --- ping-pong: up to the last frame, back down to 0 ----------------------------------------------
    p.pingPong = true;
    p.frame = 0;
    p.nextFrame = 1;
    p.time = 0;
    p.backwards = false;
    const int steps[][3] = {{1, 2, 0}, {2, 3, 0}, {3, 4, 0}, {4, 3, 1}, {3, 2, 1}, {2, 1, 1}, {1, 0, 1}, {0, 1, 0}, {1, 2, 0}};
    for (const auto &step : steps) {
        p.Update(101);
        WM(r.vaPing, p.frame == step[0] && p.nextFrame == step[1] && p.backwards == (step[2] != 0) && p.time == 1 &&
                         WmBlended(pv, 2, made, step[0], step[1], 0.01f));
        p.time = 0;
    }
    // The turns set the frame (here it was not next to the end).
    p.frame = 1;
    p.nextFrame = 4;
    p.backwards = false;
    p.time = 0;
    p.Update(101);
    WM(r.vaPing, p.frame == 4 && p.nextFrame == 3 && p.backwards);
    p.frame = 3;
    p.nextFrame = 0;
    p.time = 0;
    p.Update(101);
    WM(r.vaPing, p.frame == 0 && p.nextFrame == 1 && !p.backwards);
    // Without loop: at the top it stops.
    p.frame = 3;
    p.nextFrame = 4;
    p.backwards = false;
    p.loop = false;
    p.Update(101);
    WM(r.vaPing, p.stopped && p.frame == 3 && p.nextFrame == 4 && p.time == 100 && !p.backwards);
    // Going down it does not stop.
    p.stopped = false;
    p.frame = 1;
    p.nextFrame = 0;
    p.backwards = true;
    p.time = 0;
    p.Update(101);
    WM(r.vaPing, !p.stopped && p.frame == 0 && p.nextFrame == 1 && !p.backwards);
    p.loop = true;
    p.pingPong = false;

    // --- Reset: frame 0 at the drawn level, the counters back ------------------------------------------
    p.frame = 3;
    p.nextFrame = 4;
    p.time = 70;
    p.backwards = true;
    p.stopped = true;
    p.cull = 1;
    WmField<int>(pv, 0x11C) = 40;                       // (Reset does not ask)
    drawLevel(1, false);
    WmClear(pv);
    const int before = g_wmRefreshes[pv];
    p.Reset();
    WM(r.vaReset, p.frame == 0 && p.nextFrame == 1 && p.time == 0 && !p.backwards && p.stopped &&
                      WmBlended(pv, 1, made, 0, 0, 0.0f) && WmUntouched(pv, 0) && WmUntouched(pv, 2) &&
                      g_wmRefreshes[pv] == before + 1);
    // A level past the maximal one: nothing (the counters too).
    p.frame = 3;
    p.time = 70;
    p.backwards = true;
    p.maxLevel = 0;
    WmClear(pv);
    p.Reset();
    WM(r.vaReset, p.frame == 3 && p.time == 70 && p.backwards && WmUntouched(pv, 1) && g_wmRefreshes[pv] == before + 1);
    p.visual = nullptr;
    p.Reset();
    p.visual = pv;
    p.animation = nullptr;
    p.Reset();
    WM(r.vaReset, p.frame == 3 && p.time == 70);
    p.animation = made;
    p.maxLevel = 2;
    p.stopped = false;

    // --- the module's Tick / Update: near ones every frame, far ones in the ticks ----------------------
    I3D_frame *cam = NewFrame(FRAME_CAMERA, "wm_cam", nullptr);
    I3D_frame *oldCamera = WmField<I3D_frame *>(scene, 0x15C);
    WmField<I3D_frame *>(scene, 0x15C) = cam;
    WmField<float>(cam, 0x120) = 1.2f;
    WmField<float>(cam, 0x138) = 1.25f;
    cam->m_localMatrix(3) = S_vector(10.0f, 2.0f, -3.0f);
    const float scale = std::tan(1.2f * 0.5f) / 1.25f;
    const float edge = 1.7f / (0.02f * scale * 2.0f);  // the distance where the size is 0.02
    struct S_At {
        float distance;
        int cull;
        int distant;
    };
    const S_At at[] = {{edge * 0.99f, 1, 0}, {edge * 1.01f, 1, 1}, {edge * 1.01f, 0, 0}, {edge * 3.0f, 1, 1},
                       {1.0f, 1, 0}, {0.0f, 1, 0}};
    std::vector<S_VertexAnimPlayer *> players;
    std::vector<I3D_frame *> pvs;
    for (size_t i = 0; i < std::size(at); ++i) {
        I3D_frame *v = WmObject("wm_t" + std::to_string(i), 3, 5);
        // (Off along a slanted direction: every component counts.)
        const S_vector dir = S_vector(0.48f, -0.6f, 0.64f);
        v->m_localMatrix(3) = S_vector(10.0f, 2.0f, -3.0f) + dir * at[i].distance;
        auto *pl = new S_VertexAnimPlayer;
        pl->Init(v, made);
        pl->cull = at[i].cull;
        pl->maxLevel = 2;
        pl->frameTime = 1000;
        pl->distant = 1 - at[i].distant;
        WmField<int>(v, 0x11C) = WmField<int>(scene, 0x11C);
        players.push_back(pl);
        pvs.push_back(v);
        m.AddPlayer(pl);
    }
    // Not yet 300 ms: no sorting; the far ones (as set) advance in the tick, the near ones in the update.
    g_vertexAnimSortTime = 0;
    m.Tick(299);
    for (size_t i = 0; i < players.size(); ++i)
        WM(r.vaTick, players[i]->distant == 1 - at[i].distant && players[i]->time == (players[i]->distant == 1 ? 299 : 0));
    m.Update(7);
    for (size_t i = 0; i < players.size(); ++i)
        WM(r.vaTick, players[i]->time == (players[i]->distant == 1 ? 299 : 7));
    // At 300: sorted (the remainder kept), then the far ones advance.
    m.Tick(5);
    WM(r.vaTick, g_vertexAnimSortTime == 4);
    for (size_t i = 0; i < players.size(); ++i) {
        const int old = 1 - at[i].distant == 1 ? 299 : 7;
        WM(r.vaTick, players[i]->distant == at[i].distant && players[i]->time == old + (at[i].distant == 1 ? 5 : 0));
    }
    // Exactly 300: sorted.
    for (S_VertexAnimPlayer *pl : players)
        pl->distant = 1 - pl->distant;
    g_vertexAnimSortTime = 0;
    m.Tick(299);
    m.Tick(1);
    WM(r.vaTick, g_vertexAnimSortTime == 0);
    for (size_t i = 0; i < players.size(); ++i)
        WM(r.vaTick, players[i]->distant == at[i].distant);
    // A wide view (the size's edge at 1.2 m): 1 m exactly near, 0 m near.
    {
        WmField<float>(cam, 0x138) = std::tan(0.6f) / (1.7f / (0.04f * 1.2f));
        I3D_frame *v1 = WmObject("wm_one", 3, 5), *v0 = WmObject("wm_zero", 3, 5), *v2 = WmObject("wm_far", 3, 5);
        v1->m_localMatrix(3) = S_vector(11.0f, 2.0f, -3.0f);
        v0->m_localMatrix(3) = S_vector(10.0f, 2.0f, -3.0f);
        v2->m_localMatrix(3) = S_vector(10.0f, 3.25f, -3.0f);
        S_VertexAnimPlayer q1, q0, q2;
        q1.Init(v1, made);
        q0.Init(v0, made);
        q2.Init(v2, made);
        q0.distant = q1.distant = 1;
        std::vector<S_VertexAnimPlayer *> kept;
        kept.swap(m.m_players);
        m.AddPlayer(&q1);
        m.AddPlayer(&q0);
        m.AddPlayer(&q2);
        g_vertexAnimSortTime = 300;
        m.Tick(0);
        WM(r.vaTick, q1.distant == 0 && q0.distant == 0 && q2.distant == 1);
        // (The edge at 0.4 m: 0 m still near, 1 m far.)
        WmField<float>(cam, 0x138) = std::tan(0.6f) / (1.7f / (0.04f * 0.4f));
        q0.distant = 1;
        g_vertexAnimSortTime = 300;
        m.Tick(0);
        WM(r.vaTick, q1.distant == 1 && q0.distant == 0 && q2.distant == 1);
        m.m_players.swap(kept);
        WmField<float>(cam, 0x138) = 1.25f;
    }
    // (Unsigned: a negative frame time gathers a huge time, then sorts.)
    for (S_VertexAnimPlayer *pl : players)
        pl->distant = 1 - pl->distant;
    g_vertexAnimSortTime = 4;
    m.Tick(-5);
    for (size_t i = 0; i < players.size(); ++i)
        WM(r.vaTick, players[i]->distant == at[i].distant);
    WM(r.vaTick, g_vertexAnimSortTime == 0xFFFFFFFFu - 300);
    // The detail switch off, or a server alone: nothing moves.
    for (S_VertexAnimPlayer *pl : players)
        pl->time = 0;
    g_vertexAnimSortTime = 0;
    g_wmDetails.switches[DETAIL_VERTEX_ANIMATION] = false;
    m.Tick(400);
    m.Update(400);
    g_wmDetails.switches[DETAIL_VERTEX_ANIMATION] = true;
    g_gameRole = GAME_ROLE_SERVER;
    m.Tick(400);
    m.Update(400);
    g_gameRole = GAME_ROLE_LOCAL;
    for (S_VertexAnimPlayer *pl : players)
        WM(r.vaTick, pl->time == 0);
    WM(r.vaTick, g_vertexAnimSortTime == 0);
    m.Update(3);
    for (size_t i = 0; i < players.size(); ++i)
        WM(r.vaTick, players[i]->time == (at[i].distant == 0 ? 3 : 0));

    // --- StartMission: random start frames for negative ones, once ----------------------------------
    S_VertexAnimation *one = WmMakeAnimation("one", 1, 1, 1);
    S_VertexAnimation *three = WmMakeAnimation("three", 3, 1, 1);
    const struct {
        S_VertexAnimation *a;
        int start;
        bool ping;
    } starts[] = {{made, -1, false}, {made, -1, true}, {one, -2, true}, {three, -1, false}, {three, -1, true},
                  {made, 0, false}, {made, 2, true}};
    for (S_VertexAnimPlayer *pl : players)
        m.RemovePlayer(pl, false);
    WM(r.vaStart, m.m_players.empty());
    std::vector<S_VertexAnimPlayer *> sp;
    for (const auto &s : starts) {
        auto *pl = new S_VertexAnimPlayer;
        pl->Init(pvs[0], s.a);
        pl->startFrame = s.start;
        pl->pingPong = s.ping;
        pl->backwards = true;
        pl->frame = 9;
        pl->nextFrame = 9;
        sp.push_back(pl);
        m.AddPlayer(pl);
    }
    auto expectStart = [&](unsigned seed) {
        std::srand(seed);
        bool ok = true;
        for (size_t i = 0; i < sp.size(); ++i) {
            const auto &s = starts[i];
            int frame = 0, next = 1;
            if (s.start < 0) {
                const int count = s.a->lastFrame;
                frame = !s.ping ? std::rand() % count : count == 1 ? 0 : std::rand() % (count - 1);
                next = frame + 1 >= count ? 0 : frame + 1;
            }
            ok = ok && sp[i]->frame == frame && sp[i]->nextFrame == next && !sp[i]->backwards;
        }
        return ok;
    };
    m.m_started = 0;
    std::srand(1234);
    m.StartMission(false);
    WM(r.vaStart, expectStart(1234) && m.m_started == 1);
    // Started already: nothing.
    for (S_VertexAnimPlayer *pl : sp) {
        pl->frame = 9;
        pl->backwards = true;
    }
    m.StartMission(true);
    WM(r.vaStart, sp[0]->frame == 9 && sp[5]->frame == 9 && sp[0]->backwards);
    // RestartMission: again from the start.
    std::srand(99);
    m.RestartMission(true);
    WM(r.vaStart, expectStart(99) && m.m_started == 1);
    // A server alone: no restart.
    for (S_VertexAnimPlayer *pl : sp)
        pl->frame = 9;
    g_gameRole = GAME_ROLE_SERVER;
    m.m_started = 0;
    m.StartMission(false);
    m.m_started = 1;
    m.RestartMission(false);
    WM(r.vaStart, sp[0]->frame == 9 && sp[5]->frame == 9 && m.m_started == 1);
    g_gameRole = GAME_ROLE_LOCAL;

    // --- RemovePlayer: the last one fills the place ----------------------------------------------------
    S_VertexAnimPlayer outsider;
    m.RemovePlayer(&outsider, false);
    WM(r.vaRemove, m.m_players.size() == sp.size());
    m.RemovePlayer(sp[1], false);
    WM(r.vaRemove, m.m_players.size() == sp.size() - 1 && m.m_players[1] == sp.back() && m.m_players[0] == sp[0] &&
                       sp[1]->frame == 9 + 0 * sp[1]->frame);
    m.RemovePlayer(sp.back(), true);
    WM(r.vaRemove, m.m_players.size() == sp.size() - 2 && m.m_players[1] == sp[5] && m.m_players[0] == sp[0]);
    m.RemovePlayer(sp[5], false);
    WM(r.vaRemove, m.m_players.size() == sp.size() - 3 && m.m_players[1] == sp[4] && m.m_players.back() == sp[3]);

    {
        std::vector<S_VertexAnimPlayer *> kept;
        kept.swap(m.m_players);
        S_VertexAnimPlayer pa, pb;
        m.AddPlayer(&pa);
        m.AddPlayer(&pa);
        m.AddPlayer(&pb);
        m.RemovePlayer(&pa, false);
        WM(r.vaRemove, m.m_players.size() == 2 && m.m_players[0] == &pb && m.m_players[1] == &pa);
        m.RemovePlayer(&pa, false);
        m.RemovePlayer(&pa, false);
        WM(r.vaRemove, m.m_players.size() == 1 && m.m_players[0] == &pb);
        m.m_players.swap(kept);
    }

    // --- StopMission: the players deleted, once started -------------------------------------------------
    m.m_started = 0;
    m.StopMission(false);
    WM(r.vaStop, m.m_players.size() == sp.size() - 3);
    g_gameRole = GAME_ROLE_SERVER;
    m.m_started = 1;
    m.StopMission(false);
    WM(r.vaStop, m.m_players.size() == sp.size() - 3 && m.m_started == 1);
    g_gameRole = GAME_ROLE_LOCAL;
    m.StopMission(true);
    WM(r.vaStop, m.m_players.empty() && m.m_started == 0);

    // --- Done: the animations deleted (a client's only) -------------------------------------------------
    g_gameRole = GAME_ROLE_SERVER;
    m.Done();
    WM(r.vaDone, m.m_animations.size() == parsed.size());
    g_gameRole = GAME_ROLE_LOCAL;
    m.Done();
    WM(r.vaDone, m.m_animations.empty() && !m.FindAnimation(C_str(anim0.c_str())));

    delete sp[1];
    delete sp[5];
    for (S_VertexAnimPlayer *pl : players)
        delete pl;
    delete made;
    delete one;
    delete three;
    WmField<I3D_frame *>(scene, 0x15C) = oldCamera;
    g_sceneVtable[0x7C / 4] = oldFind;
    g_gameRole = oldRole;
}

// --- the lightmap groups ------------------------------------------------------------------------------------

struct S_LmapsResults {
    bool lmLoad = true, lmFind = true, lmSet = true, lmGroup = true, lmSave = true, lmLoadGame = true,
         lmRestart = true, lmFree = true;
};

namespace {

// A chunk file in memory: a container chunk (0x1234) of these chunks (id, payload or children).
struct S_WmChunk {
    uint16_t id;
    std::string text;                                   // (with its terminator)
    std::vector<S_WmChunk> children;
};
void WmPutChunk(std::vector<uint8_t> &out, const S_WmChunk &c)
{
    const size_t at = out.size();
    out.resize(at + 6);
    std::memcpy(&out[at], &c.id, 2);
    if (c.children.empty())
        out.insert(out.end(), c.text.c_str(), c.text.c_str() + c.text.size() + 1);
    for (const S_WmChunk &k : c.children)
        WmPutChunk(out, k);
    const uint32_t size = static_cast<uint32_t>(out.size() - at);
    std::memcpy(&out[at + 2], &size, 4);
}

}  // namespace

void LightmapGroupsTest(S_LmapsResults &r)
{
    C_LightMapGroupsModule &m = g_lightMapGroupsModule;
    I3D_scene *scene = GetGame()->m_scene;
    void *const oldFind = g_sceneVtable[0x7C / 4];
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_WmSceneFind);
    void *const oldDriverFind = g_driverVtable[0x88 / 4];
    g_driverVtable[0x88 / 4] = reinterpret_cast<void *>(&F_WmDriverFind);
    g_wmScene.clear();
    g_wmModels.clear();
    WM(r.lmFind, GetLightMapGroups() == &m);

    // Visuals: lightmapped ones in the scene and among the models, a dummy, an object.
    std::map<std::string, I3D_frame *> lm;
    for (const char *name : {"l1", "l2", "l3", "l4"}) {
        lm[name] = WmFrame(g_wmLmapVtable, name);
        g_wmScene[name] = lm[name];
    }
    lm["m1"] = WmFrame(g_wmLmapVtable, "m1");
    g_wmModels["m1"] = lm["m1"];
    I3D_frame *dummy = WmFrame(g_wmDummyVtable, "d1"), *object = WmObject("o1", 1, 1);
    g_wmScene["d1"] = dummy;
    g_wmScene["o1"] = object;
    int index = 0;
    for (auto &kv : lm)
        WmField<int>(kv.second, 0x210) = 10 + index++;   // l1 10, l2 11, l3 12, l4 13, m1 14

    // --- LoadGroups ----------------------------------------------------------------------------------
    WM(r.lmLoad, !m.LoadGroups(nullptr));
    const S_WmChunk file = {0x1234, "", {{0x110, "orphan", {}},
                                         {0x100, "groupA", {}},
                                         {0x110, "l1", {}},
                                         {0x110, "L1", {}},
                                         {0x110, "m1", {}},
                                         {0x100, "GROUPA", {}},
                                         {0x110, "d1", {}},
                                         {0x100, "", {}},
                                         {0x110, "o1", {}},
                                         {0x100, "groupB", {}},
                                         {0x110, "l2", {}},
                                         {0x110, "l3", {}},
                                         {0x110, "nosuch", {}},
                                         {0x100, "groupC", {}},
                                         {0x120, "", {{0x110, "nested", {}}, {0x100, "groupD", {}}, {0x110, "l4", {}}}},
                                         {0x110, "after", {}}}};
    std::vector<uint8_t> bytes;
    WmPutChunk(bytes, file);
    g_files["wm_groups.bin"] = bytes;
    auto load = [&](const std::vector<uint8_t> &data) {
        g_files["wm_groups.bin"] = data;
        C_chunk chunk;
        chunk.Open("wm_groups.bin", false);
        uint16_t id;
        chunk.Descend(id);
        const bool ok = m.LoadGroups(&chunk);
        return ok && chunk.RemainingSize() == 0;
    };
    // (Something to drop first: a group and a switch, released without its lightmap set back.)
    m.m_groups.push_back(new S_LightmapGroup("old"));
    m.SetFrameLightmap(lm["l4"], 3);
    g_wmLog.clear();
    WM(r.lmLoad, load(bytes));
    auto frames = [&](int g) {
        std::string out;
        for (const C_str &f : m.m_groups[static_cast<size_t>(g)]->frames)
            out += std::string(f) + ",";
        return out;
    };
    // (The unknown chunk 0x120 is entered and not left: its children are read, then its end ends
    // the loading.)
    WM(r.lmLoad, m.m_groups.size() == 4 && std::string(m.m_groups[0]->name) == "groupA" &&
                     std::string(m.m_groups[1]->name) == "groupB" && std::string(m.m_groups[2]->name) == "groupC" &&
                     std::string(m.m_groups[3]->name) == "groupD" && frames(0) == "l1,m1,d1,o1," &&
                     frames(1) == "l2,l3,nosuch," && frames(2) == "nested," && frames(3) == "l4," &&
                     m.m_changes.empty() && WmLogIs({"release l4"}) && lm["l4"]->m_refCount == 1 &&
                     WmField<int>(lm["l4"], 0x210) == 3);
    WmField<int>(lm["l4"], 0x210) = 13;
    // Again: the same (the old groups replaced).
    WM(r.lmLoad, load(bytes) && m.m_groups.size() == 4 && frames(0) == "l1,m1,d1,o1,");
    // A container ending in a header cut short: its id is the last one's, so an empty frame name is
    // read and the container left.
    {
        std::vector<uint8_t> small;
        WmPutChunk(small, {0x1234, "", {{0x100, "g", {}}, {0x110, "f", {}}}});
        small.insert(small.end(), {0x10, 0x01, 0x09});
        const uint32_t size = static_cast<uint32_t>(small.size());
        std::memcpy(&small[2], &size, 4);
        g_files["wm_groups.bin"] = small;
        C_chunk chunk;
        chunk.Open("wm_groups.bin", false);
        uint16_t id;
        chunk.Descend(id);
        WM(r.lmLoad, m.LoadGroups(&chunk) && m.m_groups.size() == 1 && frames(0) == "f,," && chunk.RemainingSize() == 0);
    }
    // An empty container: no groups.
    {
        std::vector<uint8_t> empty;
        WmPutChunk(empty, {0x1234, "", {}});
        empty.resize(6);
        const uint32_t six = 6;
        std::memcpy(&empty[2], &six, 4);
        WM(r.lmLoad, load(empty) && m.m_groups.empty());
    }
    load(bytes);

    // --- FindGroup: case insensitive, the last of equal names ---------------------------------------
    WM(r.lmFind, m.FindGroup("groupa") == m.m_groups[0] && m.FindGroup("GROUPB") == m.m_groups[1] &&
                     !m.FindGroup("group") && !m.FindGroup(nullptr) && !m.FindGroup("") && m.FindGroup("groupC") == m.m_groups[2]);
    m.m_groups.push_back(new S_LightmapGroup("GroupB"));
    WM(r.lmFind, m.FindGroup("groupb") == m.m_groups.back());
    WM(r.lmFind, m.m_groups[0]->HasFrame("L1") && m.m_groups[2]->HasFrame("NESTED") && !m.m_groups[0]->HasFrame("l2") &&
                     !m.m_groups[3]->HasFrame("l1") && !m.m_groups[0]->HasFrame(""));
    delete m.m_groups.back();
    m.m_groups.pop_back();

    // --- SetFrameLightmap: switched, remembered with its original; back: forgotten --------------------
    g_wmLog.clear();
    m.SetFrameLightmap(nullptr, 4);
    m.SetFrameLightmap(lm["l1"], 4);
    WM(r.lmSet, m.m_changes.size() == 1 && m.m_changes[0]->frame == lm["l1"] && m.m_changes[0]->original == 10 &&
                    lm["l1"]->m_refCount == 2 && WmField<int>(lm["l1"], 0x210) == 4 && WmLogIs({"select l1 4", "update l1"}));
    m.SetFrameLightmap(lm["l2"], 0);
    m.SetFrameLightmap(lm["l3"], 1);
    m.SetFrameLightmap(lm["l1"], 5);
    WM(r.lmSet, m.m_changes.size() == 3 && m.m_changes[0]->original == 10 && WmField<int>(lm["l1"], 0x210) == 5 &&
                    lm["l1"]->m_refCount == 2 && m.m_changes[1]->original == 11 && m.m_changes[2]->original == 12);
    g_wmLog.clear();
    m.SetFrameLightmap(lm["l1"], 10);
    WM(r.lmSet, m.m_changes.size() == 2 && m.m_changes[0]->frame == lm["l3"] && m.m_changes[1]->frame == lm["l2"] &&
                    lm["l1"]->m_refCount == 1 && WmLogIs({"select l1 10", "update l1", "release l1"}));
    // The last one back: just removed.
    m.SetFrameLightmap(lm["l2"], 11);
    WM(r.lmSet, m.m_changes.size() == 1 && m.m_changes[0]->frame == lm["l3"] && lm["l2"]->m_refCount == 1);
    // Set to its original first: remembered all the same.
    m.SetFrameLightmap(lm["l4"], 13);
    WM(r.lmSet, m.m_changes.size() == 2 && m.m_changes[1]->original == 13 && lm["l4"]->m_refCount == 2);

    // --- SetGroupLightmap: the group's lightmapped visuals, last first ----------------------------------
    g_wmLog.clear();
    g_wmFindMasks.clear();
    m.SetGroupLightmap(m.m_groups[0], 7);
    WM(r.lmGroup, WmLogIs({"select m1 7", "update m1", "select l1 7", "update l1"}) && m.m_changes.size() == 4 &&
                      m.m_changes[2]->frame == lm["m1"] && m.m_changes[3]->frame == lm["l1"] &&
                      g_wmFindMasks == std::vector<uint32_t>(4, FRAME_MASK_ANY));
    g_wmLog.clear();
    m.SetGroupLightmap(nullptr, 2);
    m.SetGroupLightmap(m.m_groups[2], 2);
    GetGame()->m_scene = nullptr;
    m.SetGroupLightmap(m.m_groups[0], 2);
    GetGame()->m_scene = scene;
    WM(r.lmGroup, g_wmLog.empty());
    m.SetGroupLightmap(m.m_groups[1], 3);
    WM(r.lmGroup, WmLogIs({"select l3 3", "update l3", "select l2 3", "update l2"}) && m.m_changes.size() == 5);

    // --- SaveGame / LoadGame ------------------------------------------------------------------------
    // Now: l3 3 (12), l4 13 (13), m1 7 (14), l1 7 (10), l2 3 (11).
    {
        C_chunk out;
        out.Open("wm_save.bin", true);
        WM(r.lmSave, m.SaveGame(out));
        out.Close();
        std::vector<uint8_t> want;
        auto put = [&](const void *p, size_t n) {
            want.insert(want.end(), static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
        };
        const int count = 5;
        put(&count, 4);
        for (auto [name, level] : std::vector<std::pair<std::string, int>>{{"l2", 3}, {"l1", 7}, {"m1", 7}, {"l4", 13}, {"l3", 3}}) {
            put(&level, 4);
            const uint16_t length = static_cast<uint16_t>(name.size() + 1);
            put(&length, 2);
            put(name.c_str(), name.size() + 1);
        }
        WM(r.lmSave, g_files["wm_save.bin"] == want);
        // Nothing switched: the count alone.
        std::vector<S_LightmapChange *> keep;
        keep.swap(m.m_changes);
        C_chunk none;
        none.Open("wm_none.bin", true);
        m.SaveGame(none);
        none.Close();
        WM(r.lmSave, g_files["wm_none.bin"] == std::vector<uint8_t>(4, 0));
        keep.swap(m.m_changes);

        // Loaded into a fresh state: each one switched again (the scene's, else the models').
        m.RestartMission(false);
        for (auto &kv : lm)
            WM(r.lmLoadGame, kv.second->m_refCount == 1);
        g_wmLog.clear();
        g_wmFindMasks.clear();
        C_chunk in;
        in.Open("wm_save.bin", false);
        WM(r.lmLoadGame, m.LoadGame(in));
        WM(r.lmLoadGame, WmLogIs({"select l2 3", "update l2", "select l1 7", "update l1", "select m1 7", "update m1",
                                  "select l4 13", "update l4", "select l3 3", "update l3"}) &&
                             m.m_changes.size() == 5 && m.m_changes[0]->frame == lm["l2"] && m.m_changes[4]->frame == lm["l3"] &&
                             m.m_changes[3]->original == 13 &&
                             m.m_changes[0]->original == 11 && g_wmFindMasks == std::vector<uint32_t>(5, FRAME_MASK_ALL));
        in.Close();
        // Inside a chunk: the count and the entries checked against its end; unknown names, a dummy,
        // a name longer than 1023 (its rest skipped), cut short.
        m.RestartMission(false);
        std::vector<uint8_t> body;
        auto putb = [&](const void *p, size_t n) {
            body.insert(body.end(), static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
        };
        const int n = 3;
        putb(&n, 4);
        const std::string longName = std::string(1022, 'x') + "yz";
        for (auto [name, level] : std::vector<std::pair<std::string, int>>{{"nosuch", 1}, {longName, 2}, {"l2", 6}}) {
            putb(&level, 4);
            const uint16_t length = static_cast<uint16_t>(name.size() + 1);
            putb(&length, 2);
            putb(name.c_str(), name.size() + 1);
        }
        const std::string cut = std::string(1022, 'x') + "y";   // (the long name as read: 1022 characters + 1)
        lm[cut] = WmFrame(g_wmLmapVtable, cut.substr(0, 1022));
        g_wmScene[cut.substr(0, 1022)] = lm[cut];
        auto loadChunk = [&](size_t size) {
            std::vector<uint8_t> data(6);
            const uint16_t id = 0x4321;
            const uint32_t total = static_cast<uint32_t>(6 + size);
            std::memcpy(&data[0], &id, 2);
            std::memcpy(&data[2], &total, 4);
            data.insert(data.end(), body.begin(), body.begin() + static_cast<long>(size));
            data.insert(data.end(), {0x55, 0x66, 0x77, 0x88, 0x99, 0xAA});
            g_files["wm_lg.bin"] = data;
            C_chunk c;
            c.Open("wm_lg.bin", false);
            uint16_t chunkId;
            c.Descend(chunkId);
            g_wmLog.clear();
            const bool ok = m.LoadGame(c);
            const std::vector<std::string> got = g_wmLog;
            m.RestartMission(false);
            g_wmLog = got;
            return ok;
        };
        const std::string x = cut.substr(0, 1022);
        auto dump = [&]() {
            if (g_wmTrace)
                for (const std::string &l : g_wmLog)
                    std::printf("    log %s\n", l.size() > 60 ? (l.substr(0, 20) + "..." + l.substr(l.size() - 20)).c_str() : l.c_str());
            return true;
        };
        WM(r.lmLoadGame, loadChunk(body.size()) && dump() && WmLogIs({"select " + x + " 2", "update " + x, "select l2 6", "update l2"}));
        // Cut after the count: no frame (the names empty); the count cut: nothing read.
        WM(r.lmLoadGame, loadChunk(4) && g_wmLog.empty());
        I3D_frame *unnamed = WmFrame(g_wmLmapVtable, "");
        g_wmScene[""] = unnamed;
        WM(r.lmLoadGame, loadChunk(3) && g_wmLog.empty() && loadChunk(1) && g_wmLog.empty());
        // Cut in the last entry's length, or without its level: the name empty.
        WM(r.lmLoadGame, loadChunk(body.size() - 4) &&
                             WmLogIs({"select " + x + " 2", "update " + x, "select  6", "update "}));
        WM(r.lmLoadGame, loadChunk(body.size() - 3 - 2 - 4) &&
                             WmLogIs({"select " + x + " 2", "update " + x, "select  0", "update "}));
        g_wmScene.erase("");
        // (Cut in the level, its bytes are read as the length; the name then is what the buffer held:
        // not tested.)
    }

    // --- RestartMission / StopMission: every switch set back, last first, and released -----------------
    m.SetFrameLightmap(lm["l1"], 1);
    m.SetFrameLightmap(lm["m1"], 2);
    m.SetFrameLightmap(lm["l3"], 3);
    g_wmLog.clear();
    m.RestartMission(true);
    WM(r.lmRestart, WmLogIs({"select l3 12", "update l3", "select m1 14", "update m1", "select l1 10", "update l1",
                             "release l3", "release m1", "release l1"}) &&
                        m.m_changes.empty() && WmField<int>(lm["l1"], 0x210) == 10);
    m.SetFrameLightmap(lm["l2"], 4);
    g_wmLog.clear();
    m.StopMission(true);
    WM(r.lmRestart, WmLogIs({"select l2 11", "update l2", "release l2"}) && m.m_changes.empty());

    // --- FreeMissionData: a client's groups and switches dropped (not set back) -----------------------
    m.SetFrameLightmap(lm["l2"], 4);
    g_wmLog.clear();
    m.FreeMissionData(true);
    WM(r.lmFree, m.m_groups.size() == 4 && m.m_changes.size() == 1 && g_wmLog.empty());
    m.FreeMissionData(false);
    WM(r.lmFree, m.m_groups.empty() && m.m_changes.empty() && WmLogIs({"release l2"}) && WmField<int>(lm["l2"], 0x210) == 4);
    for (auto &kv : lm)
        WM(r.lmFree, kv.second->m_refCount == 1);

    g_sceneVtable[0x7C / 4] = oldFind;
    g_driverVtable[0x88 / 4] = oldDriverFind;
}

// --- texture shifting ---------------------------------------------------------------------------------------

struct S_TshiftResults {
    bool tsFind = true, tsAdd = true, tsRemove = true, tsFree = true;
};

void TShiftTest(S_TshiftResults &r)
{
    C_TShiftModule &m = g_tshiftModule;
    I3D_frame *a = WmFrame(g_wmObjVtable, "tsa"), *b = WmFrame(g_wmObjVtable, "tsb");
    auto *ma = reinterpret_cast<I3D_mesh *>(0x1000), *mb = reinterpret_cast<I3D_mesh *>(0x2000);
    WM(r.tsFind, !m.HasFrame(a) && !m.GetEntry(ma));
    S_TShiftEntry e1{ma, a, true, false, 1, 2};
    S_TShiftEntry e2{mb, b, false, true, 3, 4};
    S_TShiftEntry e3{ma, b, true, true, 5, 6};
    m.AddEntry(e1);
    m.AddEntry(e2);
    WM(r.tsFind, m.HasFrame(a) && m.HasFrame(b) && !m.HasFrame(nullptr) && m.GetEntry(ma) == &m.m_entries[0] &&
                     m.GetEntry(mb) == &m.m_entries[1] && !m.GetEntry(nullptr) && m.GetEntry(mb)->unknown0C == 3 &&
                     m.GetEntry(ma)->unknown14 == 0 && m.GetEntry(ma)->unknown18 == 0);
    // The last of equal meshes.
    m.AddEntry(e3);
    WM(r.tsFind, m.GetEntry(ma) == &m.m_entries[2] && m.GetEntry(ma)->frame == b);
    WM(r.tsFind, m.HasMesh(ma) && m.HasMesh(mb) && !m.HasMesh(nullptr));
    // AddVisual: on, the rest 0, once per mesh.
    auto *mc = reinterpret_cast<I3D_mesh *>(0x3000);
    m.AddVisual(a, mc);
    m.AddVisual(b, mc);
    m.AddVisual(b, ma);
    WM(r.tsAdd, m.m_entries.size() == 4 && m.m_entries[3].mesh == mc && m.m_entries[3].frame == a && m.m_entries[3].on &&
                    !m.m_entries[3].reverse && m.m_entries[3].unknown0C == 0 && m.m_entries[3].unknown10 == 0 &&
                    m.m_entries[3].unknown14 == 0 && m.m_entries[3].unknown18 == 0);
    // RemoveMesh / RemoveFrame: the last match out, the order kept.
    m.RemoveMesh(reinterpret_cast<I3D_mesh *>(0x4000));
    m.RemoveMesh(ma);
    WM(r.tsRemove, m.m_entries.size() == 3 && m.m_entries[0].mesh == ma && m.m_entries[0].frame == a &&
                       m.m_entries[1].mesh == mb && m.m_entries[2].mesh == mc);
    m.RemoveFrame(nullptr);
    m.RemoveFrame(a);
    WM(r.tsRemove, m.m_entries.size() == 2 && m.m_entries[0].frame == a && m.m_entries[1].frame == b);
    m.AddEntry(e3);
    m.RemoveFrame(b);
    WM(r.tsRemove, m.m_entries.size() == 2 && m.m_entries[0].frame == a && m.m_entries[1].mesh == mb);
    // RemoveModel: by the model above (the nearest), else the topmost frame.
    I3D_frame *top = NewFrame(FRAME_DUMMY, "ts_top", nullptr);
    I3D_frame *model = NewFrame(FRAME_MODEL, "ts_model", top);
    I3D_frame *inner = NewFrame(FRAME_MODEL, "ts_inner", model);
    I3D_frame *part = NewFrame(FRAME_DUMMY, "ts_part", inner);
    I3D_frame *leaf = NewFrame(FRAME_VISUAL, "ts_leaf", part);
    I3D_frame *loose = NewFrame(FRAME_VISUAL, "ts_loose", top);
    m.FreeMissionData(false);
    m.AddEntry({ma, leaf});
    m.AddEntry({mb, loose});
    m.AddEntry({mc, inner});
    m.AddEntry({ma, nullptr});
    m.RemoveModel(top);
    WM(r.tsRemove, m.m_entries.size() == 3 && m.m_entries[0].frame == leaf && m.m_entries[1].frame == inner);
    m.RemoveModel(model);
    WM(r.tsRemove, m.m_entries.size() == 2 && m.m_entries[0].frame == leaf && !m.m_entries[1].frame);
    m.RemoveModel(inner);
    WM(r.tsRemove, m.m_entries.size() == 1 && !m.m_entries[0].frame);
    m.RemoveModel(nullptr);
    WM(r.tsRemove, m.m_entries.empty());
    m.AddEntry(e1);
    m.AddEntry(e2);
    m.AddEntry(e3);
    m.FreeMissionData(true);
    WM(r.tsFree, m.m_entries.size() == 3);
    m.FreeMissionData(false);
    WM(r.tsFree, m.m_entries.empty() && !m.HasFrame(a) && !m.GetEntry(ma));
}

void SightTest();                                       // (sight_test.inc, part 8b)
void GEffectsTest();                                    // (geffects_test.inc, part 8c)

void WorldModulesTest(const char *path)
{
    if (const char *data = std::getenv("WORLD_DATA"))
        g_wmData = data;
    g_wmTrace = std::getenv("CAR_TRACE") != nullptr;
    std::string mission = "-";
    if (FILE *f = std::fopen(path, "r")) {
        char line[256] = {};
        if (std::fgets(line, sizeof line, f)) {
            char name[200] = {};
            if (std::sscanf(line, "mission %199s", name) == 1)
                mission = name;
        }
        std::fclose(f);
    }
    static bool once;
    if (!once) {
        once = true;
        WmInitVtables();
    }
    GetGame()->m_scene = g_carScene;
    std::printf("== %s\n", mission.c_str());
    S_VanimResults va;
    VertexAnimTest(mission, va);
    std::printf("vanim: vaLoad %d vaCut %d vaDone %d vaFind %d vaPlayers %d vaBin %d vaBlend %d vaReset %d vaUpdate %d "
                "vaPing %d vaTick %d vaStart %d vaStop %d vaRemove %d\n",
                va.vaLoad, va.vaCut, va.vaDone, va.vaFind, va.vaPlayers, va.vaBin, va.vaBlend, va.vaReset, va.vaUpdate,
                va.vaPing, va.vaTick, va.vaStart, va.vaStop, va.vaRemove);
    S_LmapsResults lmr;
    LightmapGroupsTest(lmr);
    std::printf("lmaps: lmLoad %d lmFind %d lmSet %d lmGroup %d lmSave %d lmLoadGame %d lmRestart %d lmFree %d\n",
                lmr.lmLoad, lmr.lmFind, lmr.lmSet, lmr.lmGroup, lmr.lmSave, lmr.lmLoadGame, lmr.lmRestart, lmr.lmFree);
    S_TshiftResults ts;
    TShiftTest(ts);
    std::printf("tshift: tsFind %d tsAdd %d tsRemove %d tsFree %d\n", ts.tsFind, ts.tsAdd, ts.tsRemove, ts.tsFree);
    SightTest();
    GEffectsTest();
    std::fflush(stdout);
}
