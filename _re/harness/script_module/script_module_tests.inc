// The tests (script_module_test.cpp).
namespace {

C_ScriptModule *Module() { return reinterpret_cast<C_ScriptModule *>(g_scriptModuleStorage); }

void ResetWorld()
{
    g_sceneFrames.clear();
    g_driverFrames.clear();
    g_models.clear();
    g_cacheAnims.clear();
    g_findMasks.clear();
    std::memset(g_weather, 0, sizeof g_weather);
    g_log.clear();
    g_gameRole = GAME_ROLE_LOCAL;
    g_scriptLoadResult = true;
    g_assignGivesScript = true;
    for (S_PendingSound *p : g_pendingSounds)
        delete p;
    g_pendingSounds.clear();
    C_ScriptModule *m = Module();
    m->m_scripts.clear();
    m->m_rotations.clear();
    m->m_pauseSounds.clear();
    m->m_rememberedFrames.clear();
    m->m_soundFrames.clear();
    m->m_fakeCutscene = false;
    std::memset(m->m_saveGameValues, 0, sizeof m->m_saveGameValues);
}

// A parser for what SaveGame wrote.
struct S_Reader {
    const std::string &data;
    size_t at;
    explicit S_Reader(const std::string &d) : data(d), at(0) {}
    template <class T> T Get()
    {
        T v;
        std::memcpy(&v, data.data() + at, sizeof v);
        at += sizeof v;
        return v;
    }
    // A chunk's id; its end in `end`.
    uint16_t Chunk(size_t *end)
    {
        const size_t start = at;
        const uint16_t id = Get<uint16_t>();
        *end = start + Get<uint32_t>();
        return id;
    }
    std::string Sized()
    {
        const uint16_t n = Get<uint16_t>();
        std::string s = data.substr(at, n ? n - 1u : 0u);
        at += n;
        return s;
    }
    std::string Rest(size_t end)
    {
        std::string s(data.c_str() + at);
        at = end;
        return s;
    }
};

I3D_animation_set *NewAnim(const char *file, int mode)
{
    auto *buf = static_cast<char *>(std::calloc(1, 0x100));
    auto *a = reinterpret_cast<I3D_animation_set *>(buf);
    a->m_fileName = _strdup(file);
    a->m_playMode = mode;
    return a;
}
uint32_t __stdcall F_AnimRelease(I3D_animation_set *a) { Log("anim release %s", a->GetFileName()); return 0; }
void *g_animVt[16] = {reinterpret_cast<void *>(F_AnimRelease)};
I3D_animation_set *NewCacheAnim(const char *file)
{
    I3D_animation_set *a = NewAnim(file, 0);
    *reinterpret_cast<void ***>(a) = g_animVt;
    return a;
}
S_RememberedFrame *Remember(I3D_frame *frame, uint32_t what)
{
    auto *r = new (_aligned_malloc(sizeof(S_RememberedFrame), 16)) S_RememberedFrame;
    r->frame = frame;
    r->what = what;
    Module()->m_rememberedFrames.push_back(r);
    return r;
}
void WriteTo(std::string *out)
{
    C_chunk chunk;
    chunk.Open("save", true);
    CHECK(Module()->SaveGame(chunk));
    chunk.Close();
    *out = g_file;
}
bool LoadFrom(const std::string &in)
{
    g_file = in;
    C_chunk chunk;
    chunk.Open("save", false);
    uint16_t id;
    chunk.Descend(id);                                  // (a wrapping chunk, as in the game's file)
    const bool result = Module()->LoadGame(chunk);
    chunk.Close();
    return result;
}
std::string Wrap(const std::string &content)
{
    std::string out;
    const uint16_t id = 0x7777;
    const uint32_t size = static_cast<uint32_t>(content.size() + 6);
    out.append(reinterpret_cast<const char *>(&id), 2);
    out.append(reinterpret_cast<const char *>(&size), 4);
    return out + content;
}
std::string MakeChunk(uint16_t id, const std::string &content) { return Wrap(content).replace(0, 2, reinterpret_cast<const char *>(&id), 2); }
template <class T> std::string Bytes(T v) { return std::string(reinterpret_cast<const char *>(&v), sizeof v); }
std::string Sized(const std::string &s) { return Bytes(static_cast<uint16_t>(s.size() + 1)) + s + std::string(1, '\0'); }
std::string Z(const std::string &s) { return s + std::string(1, '\0'); }

void TestSave()
{
    ResetWorld();
    C_ScriptModule *m = Module();
    m->m_saveGameValues[0] = 11;
    m->m_saveGameValues[255] = 22;
    // Reverse order everywhere: the last first.
    m->m_pauseSounds.push_back(NewFrame("pause1", FRAME_SOUND));
    m->m_pauseSounds.push_back(NewFrame("pause2", FRAME_SOUND));
    I3D_frame *rotating = NewFrame("rotating", FRAME_DUMMY);
    rotating->m_rot.v = S_vector(0.1f, 0.2f, 0.3f);
    rotating->m_rot.s = 0.4f;
    auto *rotation = static_cast<S_FrameRotation *>(_aligned_malloc(sizeof(S_FrameRotation), 16));
    rotation->frame = rotating;
    rotation->axis = 2;
    rotation->angleLeft = 1.5f;
    rotation->speed = 0.25f;
    m->m_rotations.push_back(rotation);
    for (int i = 0; i < 16; ++i)
        g_weather[i] = 100u + static_cast<uint32_t>(i);
    I3D_frame *parent = NewFrame("parent", FRAME_DUMMY);
    I3D_frame *full = NewFrame("full", FRAME_SOUND);
    full->m_frameFlags |= 1;
    full->m_parent = parent;
    full->m_localMatrix(3) = S_vector(1, 2, 3);
    full->m_rot.v = S_vector(0.5f, 0.6f, 0.7f);
    full->m_rot.s = 0.8f;
    *reinterpret_cast<S_vector *>(reinterpret_cast<char *>(full) + 0x1A0) = S_vector(7, 8, 9);
    *reinterpret_cast<float *>(reinterpret_cast<char *>(full) + 0x16C) = 0.75f;
    Remember(full, SCRIPT_FRAME_ON | SCRIPT_FRAME_SOUND | SCRIPT_FRAME_POSITION | SCRIPT_FRAME_ROTATION | SCRIPT_FRAME_LINKED);
    I3D_frame *modelFrame = NewFrame("model", FRAME_MODEL);
    S_ModelState &ms = g_models[modelFrame];
    ms.anims[0] = NewAnim("zero.i3d", I3DANIM_PLAY_ONCE);
    ms.anims[1] = NewAnim("anims\\walk.i3d", I3DANIM_LOOP);
    ms.anims[6] = NewAnim("once.i3d", I3DANIM_PLAY_ONCE);
    ms.anims[7] = NewAnim("eighth.i3d", I3DANIM_LOOP);  // (the eighth stage is not saved)
    ms.times[0] = 50;
    ms.times[1] = 500;
    ms.times[6] = 600;
    ms.active[1] = true;
    Remember(modelFrame, SCRIPT_FRAME_ANIMATION);
    // Gone: only the list holds it; or its name finds nothing. A non-model's animation bit: nothing.
    I3D_frame *alone = NewFrame("alone", FRAME_DUMMY);
    alone->m_refCount = 1;
    Remember(alone, SCRIPT_FRAME_ON);
    I3D_frame *lost = NewFrame("lost", FRAME_DUMMY);
    g_sceneFrames.erase("lost");
    Remember(lost, SCRIPT_FRAME_ON);
    I3D_frame *plain = NewFrame("plain", FRAME_DUMMY);
    Remember(plain, SCRIPT_FRAME_ANIMATION);
    // Scripts: none in a slot is skipped.
    m->m_scripts.push_back(NewScript(NewFrame("scripted1", FRAME_DUMMY), "one.bin"));
    m->m_scripts.push_back(nullptr);
    m->m_scripts.push_back(NewScript(NewFrame("scripted2", FRAME_DUMMY), "two.bin"));
    m->m_fakeCutscene = true;
    g_log.clear();
    g_findMasks.clear();
    std::string out;
    WriteTo(&out);
    CHECK(TakeLog() == "restore lost\nrestore alone\nsave script scripted2\nsave script scripted1\n");
    CHECK(m->m_rememberedFrames.size() == 3);
    CHECK(!g_findMasks.empty() && g_findMasks[0] == 0xCFFFF);

    S_Reader r(out);
    size_t end;
    CHECK(r.Chunk(&end) == 0x1004 && end == 6 + 0x400);
    CHECK(r.Get<int>() == 11);
    r.at = end - 4;
    CHECK(r.Get<int>() == 22);
    CHECK(r.Chunk(&end) == 0x1036 && r.Sized() == "pause2" && r.at == end);
    CHECK(r.Chunk(&end) == 0x1036 && r.Sized() == "pause1" && r.at == end);
    size_t rotationEnd, sub;
    CHECK(r.Chunk(&rotationEnd) == 0x13EC);
    CHECK(r.Chunk(&sub) == 0x1450 && r.Rest(sub) == "rotating");
    CHECK(r.Chunk(&sub) == 0x14B4 && r.Get<float>() == 0.4f && r.Get<float>() == 0.1f && r.Get<float>() == 0.2f && r.Get<float>() == 0.3f && r.at == sub);
    CHECK(r.Chunk(&sub) == 0x1518 && r.Get<int>() == 2);
    CHECK(r.Chunk(&sub) == 0x157C && r.Get<float>() == 1.5f);
    CHECK(r.Chunk(&sub) == 0x15E0 && r.Get<float>() == 0.25f && r.at == rotationEnd);
    CHECK(r.Chunk(&end) == 0x16A8 && end - r.at == 40);
    static const int ORDER[] = {0, 10, 5, 6, 3, 4, 11, 12, 13, 14};
    for (int param : ORDER)
        CHECK(r.Get<uint32_t>() == 100u + static_cast<uint32_t>(param));
    CHECK(r.Chunk(&end) == 0x170C);
    CHECK(r.Get<int>() == 3);
    // "plain" (it was swapped in order by the removals: the list is now full, model, plain).
    CHECK(r.Sized() == "plain" && r.Get<uint32_t>() == SCRIPT_FRAME_ANIMATION && r.Get<char>() == 0);
    // The model: count, stages.
    CHECK(r.Sized() == "model" && r.Get<uint32_t>() == SCRIPT_FRAME_ANIMATION && r.Get<char>() == 0);
    CHECK(r.Get<int>() == 3);
    CHECK(r.Get<int>() == 0 && r.Get<int>() == 50 && r.Sized() == "zero.i3d" && r.Get<char>() == 0 && r.Get<char>() == 0);
    CHECK(r.Get<int>() == 1 && r.Get<int>() == 500 && r.Sized() == "walk.i3d" && r.Get<char>() == 1 && r.Get<char>() == 1);
    CHECK(r.Get<int>() == 6 && r.Get<int>() == 600 && r.Sized() == "once.i3d" && r.Get<char>() == 0 && r.Get<char>() == 0);
    // The sound with everything.
    CHECK(r.Sized() == "full" && r.Get<uint32_t>() == 0x37u);
    CHECK(r.Get<char>() == 1 && r.Get<char>() == 1);
    CHECK(r.Get<float>() == 7 && r.Get<float>() == 8 && r.Get<float>() == 9 && r.Get<float>() == 0.75f && r.Get<int>() == 1234);
    CHECK(r.Get<float>() == 1 && r.Get<float>() == 2 && r.Get<float>() == 3);
    CHECK(r.Get<float>() == 0.5f && r.Get<float>() == 0.6f && r.Get<float>() == 0.7f && r.Get<float>() == 0.8f);
    CHECK(r.Sized() == "parent" && r.at == end);
    // Scripts: frame, file, state.
    size_t scriptEnd;
    CHECK(r.Chunk(&scriptEnd) == 0x44C);
    CHECK(r.Chunk(&sub) == 0x834 && r.Rest(sub) == "scripted2");
    CHECK(r.Chunk(&sub) == 0xC1C && r.Rest(sub) == "two.bin");
    CHECK(r.Chunk(&sub) == 0x1644 && out.compare(r.at, 5, "STATE") == 0);
    r.at = scriptEnd;
    CHECK(r.Chunk(&scriptEnd) == 0x44C);
    r.at = scriptEnd;
    CHECK(r.Chunk(&end) == 0x47E && r.Get<char>() == 1 && r.at == end && r.at == out.size());

    // A quaternion not yet made from the matrix is made first.
    ResetWorld();
    I3D_frame *matrixRot = NewFrame("matrix", FRAME_DUMMY);
    matrixRot->m_frameFlags &= ~FRMFLAGS_ROT_DIRTY;
    Remember(matrixRot, SCRIPT_FRAME_ROTATION);
    g_rotMade = 0;
    WriteTo(&out);
    CHECK(g_rotMade == 1 && (matrixRot->m_frameFlags & FRMFLAGS_ROT_DIRTY));
}

void TestLoad()
{
    ResetWorld();
    C_ScriptModule *m = Module();
    // What was running goes first: scripts ended, frames restored.
    I3D_frame *oldScripted = NewFrame("old", FRAME_DUMMY);
    m->m_scripts.push_back(NewScript(oldScripted, "old.bin"));
    m->m_scripts.push_back(nullptr);
    Remember(NewFrame("oldframe", FRAME_DUMMY), SCRIPT_FRAME_ON);

    I3D_frame *rotating = NewFrame("rotating", FRAME_DUMMY);
    I3D_frame *driverOnly = NewFrame("driveronly", FRAME_DUMMY, false);
    NewFrame("parent", FRAME_DUMMY);
    I3D_frame *sound = NewFrame("sound", FRAME_SOUND);
    I3D_frame *quiet = NewFrame("quiet", FRAME_SOUND);
    I3D_frame *model = NewFrame("model", FRAME_MODEL);
    model->m_frameFlags |= 0;
    char *const modelFile = _strdup("own.i3d");
    *reinterpret_cast<const char **>(reinterpret_cast<char *>(model) + 0x134) = modelFile;
    g_cacheAnims["walk.i3d"] = NewCacheAnim("walk.i3d");
    g_cacheAnims["run.i3d"] = NewCacheAnim("run.i3d");
    std::string values(0x400, '\0');
    std::memcpy(&values[4], "\x63\0\0\0", 4);
    std::string file;
    file += MakeChunk(0x1004, values);
    file += MakeChunk(0x1036, Sized("pausing"));
    const float q[4] = {0.5f, 0.1f, 0.7f, 0.5f};            // s, x, y, z (of length 1)
    file += MakeChunk(0x13EC, MakeChunk(0x1450, Z("rotating")) + MakeChunk(0x14B4, std::string(reinterpret_cast<const char *>(q), 16)) +
                                  MakeChunk(0x1518, Bytes(1)) + MakeChunk(0x157C, Bytes(3.2f)) + MakeChunk(0x15E0, Bytes(0.5f)));
    std::string weather;
    static const uint32_t W[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
    weather.assign(reinterpret_cast<const char *>(W), 40);
    file += MakeChunk(0x16A8, weather);
    std::string frames = Bytes(4);
    // The sound, on: its start deferred. Everything else.
    frames += Sized("sound") + Bytes(0x37u) + std::string(1, '\1') + std::string(1, '\1');
    frames += Bytes(7.0f) + Bytes(8.0f) + Bytes(9.0f) + Bytes(0.5f) + Bytes(321);
    frames += Bytes(1.0f) + Bytes(2.0f) + Bytes(3.0f);
    frames += Bytes(0.0f) + Bytes(0.0f) + Bytes(0.0f) + Bytes(2.0f);   // (normalized)
    frames += Sized("parent");
    // A sound off: no deferred start.
    frames += Sized("quiet") + Bytes(0x10u) + std::string(1, '\0') + Bytes(0.0f) + Bytes(0.0f) + Bytes(0.0f) + Bytes(1.0f) + Bytes(5);
    // The model: own animation (stopped), one by the cache (running), one not found, again one stopped.
    frames += Sized("model") + Bytes(8u) + std::string(1, '\1') + Bytes(4);
    frames += Bytes(2) + Bytes(100) + Sized("own.i3d") + std::string(1, '\0') + std::string(1, '\0');
    frames += Bytes(3) + Bytes(200) + Sized("walk.i3d") + std::string(1, '\1') + std::string(1, '\1');
    frames += Bytes(4) + Bytes(300) + Sized("missing.i3d") + std::string(1, '\0') + std::string(1, '\0');
    frames += Bytes(5) + Bytes(400) + Sized("run.i3d") + std::string(1, '\0') + std::string(1, '\0');
    // Not in the scene: the rest is not read.
    frames += Sized("nosuch") + Bytes(1u) + std::string(1, '\1') + std::string("junk");
    file += MakeChunk(0x170C, frames);
    file += MakeChunk(0x44C, MakeChunk(0x834, Z("driveronly")) + MakeChunk(0xC1C, Z("a.bin")) + MakeChunk(0x1644, "SAVED"));
    file += MakeChunk(0x44C, MakeChunk(0x834, Z("nowhere")) + MakeChunk(0xC1C, Z("b.bin")));
    file += MakeChunk(0x47E, std::string(1, '\1'));
    g_log.clear();
    CHECK(LoadFrom(Wrap(file)));
    const std::string log = TakeLog();
    const std::string want =
        "exit script old\ndelete script old\nrestore oldframe\n"
        "pause sound pausing\n"
        "rotate rotating 1 183 28\n"
        "weather 0 1\nweather 10 2\nweather 5 3\nweather 6 4\nweather 3 5\nweather 4 6\nweather 11 7\nweather 12 8\nweather 13 9\nweather 14 0\nrain 1\n"
        "remember sound 55\non sound 1\non sound 1\nupdate sound\n"
        "velocity sound 7 8 9\nvolume sound 0.5\nplay sound 321\non sound 0\n"
        "update sound\nupdate sound\nlink sound parent 0\nupdate sound\n"
        "remember quiet 16\non quiet 0\nvelocity quiet 0 0 0\nvolume quiet 1\nplay quiet 5\n"
        "remember model 8\non model 1\n"
        "anim model null 0 1 0\nanim model null 1 1 0\nanim model null 2 1 0\nanim model null 3 1 0\nanim model null 4 1 0\nanim model null 5 1 0\nanim model null 6 1 0\n"
        "time model 2 100\ntick model 1\nstop model 2\n"
        "cache walk.i3d\nanim model walk.i3d 3 1 0\nanim release walk.i3d\ntime model 3 200\ntick model 1\nregister model\nupdate model\n"
        "cache missing.i3d\n"
        "cache run.i3d\nanim model run.i3d 5 1 0\nanim release run.i3d\ntime model 5 400\ntick model 1\nregister model\nupdate model\nstop model 5\n"
        "assign driveronly a.bin\nload script driveronly SAVED\n";
    CHECK(log == want);
    if (log != want)
        std::printf("--- got\n%s--- want\n%s", log.c_str(), want.c_str());
    CHECK(m->m_saveGameValues[1] == 0x63);
    CHECK(m->m_scripts.empty() && m->m_rememberedFrames.empty());
    CHECK(m->m_fakeCutscene);
    CHECK(g_pendingSounds.size() == 1 && g_pendingSounds[0]->start == 321 && g_pendingSounds[0]->sound == static_cast<void *>(sound));
    CHECK(sound->m_refCount == 3 && quiet->m_refCount == 2);
    (void)driverOnly;
    // The position and the rotation are the frame's.
    CHECK(sound->m_localMatrix(3).x == 1.0f && sound->m_localMatrix(3).y == 2.0f && sound->m_localMatrix(3).z == 3.0f);
    CHECK(sound->m_rot.s == 1.0f && sound->m_rot.v.x == 0.0f);          // (normalized: 0 0 0 2 -> 1)
    CHECK(std::fabs(rotating->m_rot.s - 0.5f) < 1e-5f && std::fabs(rotating->m_rot.v.x - 0.1f) < 1e-5f &&
          std::fabs(rotating->m_rot.v.y - 0.7f) < 1e-5f && std::fabs(rotating->m_rot.v.z - 0.5f) < 1e-5f);
    // The play modes, from the file's byte.
    CHECK(g_cacheAnims["walk.i3d"]->GetPlayMode() == I3DANIM_LOOP && g_cacheAnims["run.i3d"]->GetPlayMode() == I3DANIM_PLAY_ONCE);

    // A locked position stays; a stored file gives the loop mode.
    ResetWorld();
    I3D_frame *locked = NewFrame("locked", FRAME_DUMMY);
    locked->m_frameFlags |= FRMFLAGS_POS_LOCKED;
    locked->m_localMatrix(3) = S_vector(9, 9, 9);
    file = MakeChunk(0x170C, Bytes(1) + Sized("locked") + Bytes(1u) + std::string(1, '\1') + Bytes(1.0f) + Bytes(2.0f) + Bytes(3.0f));
    CHECK(LoadFrom(Wrap(file)));
    CHECK(locked->m_localMatrix(3).x == 9.0f);

    // The count read is the count used: the first frame read, nothing after the last.
    ResetWorld();
    NewFrame("first", FRAME_DUMMY);
    NewFrame("second", FRAME_DUMMY);
    file = MakeChunk(0x170C, Bytes(1) + Sized("first") + Bytes(0u) + std::string(1, '\1') + Sized("second") + Bytes(0u) + std::string(1, '\1'));
    CHECK(LoadFrom(Wrap(file)));
    CHECK(TakeLog() == "remember first 0\non first 1\n");
    // A chunk cut before its count, and what the zeros give: nothing done.
    ResetWorld();
    CHECK(LoadFrom(Wrap(MakeChunk(0x170C, ""))));
    CHECK(g_log.empty());
    // A model's animation chunk without a count: only the stages cleared.
    ResetWorld();
    NewFrame("bare", FRAME_MODEL);
    file = MakeChunk(0x170C, Bytes(1) + Sized("bare") + Bytes(8u) + std::string(1, '\1'));
    CHECK(LoadFrom(Wrap(file)));
    CHECK(TakeLog().find("time ") == std::string::npos);
    // A truncated file: a chunk's header not all there.
    ResetWorld();
    CHECK(!LoadFrom(Wrap(std::string("\x47\x04\x06", 3))));
    // Weather: no rain when off, or in snow mode.
    for (int variant = 0; variant < 3; ++variant) {
        ResetWorld();
        uint32_t w[10] = {};
        w[0] = variant != 0;
        w[9] = variant == 2;
        std::string data(reinterpret_cast<const char *>(w), 40);
        CHECK(LoadFrom(Wrap(MakeChunk(0x16A8, data))));
        CHECK((TakeLog().find("rain 1") != std::string::npos) == (variant == 1));
    }
    // A frame's position and rotation chunk with the rest: on a rotation chunk, the file's frame is gone: false.
    ResetWorld();
    CHECK(!LoadFrom(Wrap(MakeChunk(0x13EC, MakeChunk(0x1450, Z("gone"))))));
    // Unknown chunk ids, inside and outside.
    ResetWorld();
    CHECK(!LoadFrom(Wrap(MakeChunk(0x4444, "x"))));
    NewFrame("gone", FRAME_DUMMY);
    CHECK(LoadFrom(Wrap(MakeChunk(0x13EC, MakeChunk(0x1450, Z("gone")) + MakeChunk(0x9999, "x")))));
    // A script's failures: the wrong first chunk; a wrong second; no script made; the wrong third; its load fails.
    ResetWorld();
    NewFrame("f", FRAME_DUMMY);
    CHECK(!LoadFrom(Wrap(MakeChunk(0x44C, MakeChunk(0x835, Z("f"))))));
    ResetWorld();
    NewFrame("f", FRAME_DUMMY);
    CHECK(!LoadFrom(Wrap(MakeChunk(0x44C, MakeChunk(0x834, Z("f")) + MakeChunk(0xC1D, Z("a"))))));
    ResetWorld();
    NewFrame("f", FRAME_DUMMY);
    g_assignGivesScript = false;
    CHECK(!LoadFrom(Wrap(MakeChunk(0x44C, MakeChunk(0x834, Z("f")) + MakeChunk(0xC1C, Z("a"))))));
    ResetWorld();
    NewFrame("f", FRAME_DUMMY);
    CHECK(!LoadFrom(Wrap(MakeChunk(0x44C, MakeChunk(0x834, Z("f")) + MakeChunk(0xC1C, Z("a")) + MakeChunk(0x1645, "x")))));
    ResetWorld();
    NewFrame("f", FRAME_DUMMY);
    g_scriptLoadResult = false;
    CHECK(!LoadFrom(Wrap(MakeChunk(0x44C, MakeChunk(0x834, Z("f")) + MakeChunk(0xC1C, Z("a")) + MakeChunk(0x1644, "x")))));
}

void TestState()
{
    ResetWorld();
    C_ScriptModule *m = Module();
    alignas(4) unsigned char buffer[0x800];
    C_BitStream stream;
    stream.Attach(buffer, sizeof buffer * 8);
    // Not the server: nothing.
    g_gameRole = GAME_ROLE_CLIENT;
    m->WriteState(&stream);
    CHECK(stream.BitPosition() == 0);
    g_gameRole = GAME_ROLE_SERVER;
    m->WriteState(nullptr);
    // The weather off: one bit; the sounds.
    I3D_frame *on = NewFrame("on", FRAME_SOUND);
    on->m_frameFlags |= 1;
    I3D_frame *off = NewFrame("off", FRAME_SOUND);
    m->m_soundFrames.push_back(off);
    m->m_soundFrames.push_back(on);
    m->WriteState(&stream);
    C_BitStream in;
    in.Attach(buffer, sizeof buffer * 8);
    CHECK(in.ReadBits(1) == 0);
    CHECK(in.ReadBits(16) == 2);
    char name[64];
    in.ReadString(name, 64);
    CHECK(std::strcmp(name, "on") == 0 && in.ReadBits(1) == 1);
    in.ReadString(name, 64);
    CHECK(std::strcmp(name, "off") == 0 && in.ReadBits(1) == 0);
    // Back on a client.
    const size_t writtenBits = stream.BitPosition();
    C_BitStream replay;
    replay.Attach(buffer, sizeof buffer * 8);
    g_gameRole = GAME_ROLE_SERVER;
    m->ReadState(&replay);
    CHECK(replay.BitPosition() == 0);                // (the server does not read)
    g_gameRole = GAME_ROLE_CLIENT;
    m->ReadState(nullptr);
    g_log.clear();
    g_findMasks.clear();
    m->ReadState(&replay);
    CHECK(TakeLog() == "weather 0 0\non on 1\non off 0\n");
    CHECK(replay.BitPosition() == writtenBits && g_findMasks.size() == 2 && g_findMasks[0] == 0xFFFF);

    // The weather on: all values.
    ResetWorld();
    g_weather[0] = 5;
    for (int i = 1; i < 14; ++i)
        g_weather[i] = 1000u + static_cast<uint32_t>(i);
    g_weather[14] = 1;
    C_BitStream all;
    all.Attach(buffer, sizeof buffer * 8);
    g_gameRole = GAME_ROLE_SERVER;
    m->WriteState(&all);
    const size_t bits = all.BitPosition();
    CHECK(bits == 1 + 13 * 32 + 1 + 16);
    C_BitStream back;
    back.Attach(buffer, sizeof buffer * 8);
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    m->ReadState(&back);
    std::string want = "weather 0 1\n";
    for (int i = 1; i < 14; ++i)
        want += "weather " + std::to_string(i) + " " + std::to_string(1000 + i) + "\n";
    want += "weather 14 1\n";
    CHECK(TakeLog() == want);
    CHECK(back.BitPosition() == bits);
    // No scene: nothing written or set.
    GetGame()->m_scene = nullptr;
    C_BitStream none;
    none.Attach(buffer, sizeof buffer * 8);
    g_gameRole = GAME_ROLE_SERVER;
    m->WriteState(&none);
    CHECK(none.BitPosition() == 0);
    g_gameRole = GAME_ROLE_CLIENT;
    C_BitStream none2;
    none2.Attach(buffer, sizeof buffer * 8);
    g_log.clear();
    m->ReadState(&none2);
    CHECK(g_log.empty());
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(&g_scene);
}

// A message as the server sends it.
std::string Send(const std::function<void(C_BitStream &)> &write, int type)
{
    static unsigned char buffer[0x400];
    std::memset(buffer, 0, sizeof buffer);
    C_BitStream out;
    out.Attach(buffer, sizeof buffer * 8);
    out.WriteBits(static_cast<uint32_t>(type), static_cast<unsigned>(g_scriptMessageTypeBits));
    write(out);
    out.WriteBits(0xFFFFFFFFu, 32);                     // (what a longer read would take)
    C_BitStream in;
    in.Attach(buffer, sizeof buffer * 8);
    g_log.clear();
    CHECK(Module()->OnMessage(&in));
    return TakeLog();
}

void TestMessages()
{
    ResetWorld();
    C_ScriptModule *m = Module();
    CHECK(!m->OnMessage(nullptr));
    g_gameRole = GAME_ROLE_CLIENT;
    const auto setOn = [](const char *name, bool on) {
        return Send([&](C_BitStream &o) { o.WriteString(name); o.WriteBits(on, 1); }, SCRIPT_MSG_FRAME_SET_ON);
    };
    // Plain frames, found in the scene or the driver's; none.
    NewFrame("plain", FRAME_DUMMY);
    CHECK(setOn("plain", true) == "remember plain 32\non plain 1\nupdate plain\n");
    NewFrame("driver", FRAME_DUMMY, false);
    CHECK(setOn("driver", false) == "remember driver 32\non driver 0\nupdate driver\n");
    CHECK(setOn("nosuch", true).empty());
    // Sounds.
    NewFrame("sound", FRAME_SOUND);
    CHECK(setOn("sound", true) == "remember sound 48\nplay sound 0\nactive sound 1 0\nupdate sound\n");
    CHECK(setOn("sound", false) == "remember sound 48\nactive sound 0 0\nupdate sound\n");
    // Models without an actor: shadow made; taken off and released; none to take.
    NewFrame("bare", FRAME_MODEL);
    CHECK(setOn("bare", true) == "remember bare 32\ncreate shadow bare\non bare 1\nupdate bare\n");
    g_shadow = NewFrame("shadow", FRAME_DUMMY);
    CHECK(setOn("bare", false) == "remember bare 32\nget shadow bare\non shadow 0\nupdate shadow\nrelease shadow\non bare 0\nupdate bare\n");
    g_shadow = nullptr;
    CHECK(setOn("bare", false) == "remember bare 32\nget shadow bare\non bare 0\nupdate bare\n");
    // Actors.
    static S_FrameData data[4];
    alignas(16) static unsigned char actors[4][0x200], moves[0x2000];
    const auto actorFrame = [&](const char *name, int index, int type) {
        I3D_frame *f = NewFrame(name, FRAME_MODEL);
        auto *a = reinterpret_cast<C_Actor *>(actors[index]);
        a->m_type = static_cast<E_ActorType>(type);
        data[index].actor = a;
        f->m_data = &data[index];
        return a;
    };
    C_Actor *animation = actorFrame("animation", 0, ACTOR_ANIMATION);
    (void)animation;
    CHECK(setOn("animation", true) == "remember animation 32\ncreate shadow animation\ncollisions 1\non animation 1\nupdate animation\n");
    actorFrame("junkers", 1, ACTOR_JUNKERS88);
    CHECK(setOn("junkers", false) == "remember junkers 32\nget shadow junkers\nhierarchy junkers 0 0x2088\nupdate junkers\n");
    actorFrame("air", 2, ACTOR_AIR_ATTACK);
    CHECK(setOn("air", true) == "remember air 32\ncreate shadow air\nhierarchy air 1 0x2088\nupdate air\n");
    C_Actor *human = actorFrame("human", 3, ACTOR_HUMAN);
    reinterpret_cast<C_Human *>(human)->m_body.movement = reinterpret_cast<C_HumanMovement *>(moves);
    g_activePlayer = nullptr;
    CHECK(setOn("human", true) == "remember human 32\nhuman names\non human 1\nupdate human\n");
    I3D_frame *humanShadow = NewFrame("hshadow", FRAME_DUMMY);
    reinterpret_cast<C_HumanMovement *>(moves)->m_shadow = humanShadow;
    g_activePlayer = reinterpret_cast<C_Human *>(human);
    CHECK(setOn("human", true) == "remember human 32\nhuman names\non hshadow 1\non human 1\ncamera target\nupdate human\n");
    CHECK(setOn("human", false) == "remember human 32\nhuman shadow off\non human 0\nupdate human\n");
    g_activePlayer = nullptr;

    // The others.
    NewFrame("speaker", FRAME_DUMMY);
    CHECK(Send([](C_BitStream &o) { o.WriteString("speaker"); o.WriteBits(777, 32); o.WriteBits(5, 8); o.WriteBits(50, 8); }, SCRIPT_MSG_MORPH_SPEECH) ==
          "dubbing 777 speaker 00000000 5 50\n");
    CHECK(Send([](C_BitStream &o) { o.WriteString("nosuch"); o.WriteBits(777, 32); o.WriteBits(5, 8); o.WriteBits(50, 8); }, SCRIPT_MSG_MORPH_SPEECH).empty());
    static unsigned char groupsStorage[0x100];
    g_groups = reinterpret_cast<C_LightMapGroupsModule *>(groupsStorage);
    NewFrame("visual", FRAME_VISUAL);
    NewFrame("driverv", FRAME_VISUAL, false);
    g_visualType = 0x50414D4C;
    CHECK(Send([](C_BitStream &o) { o.WriteString("visual"); o.WriteBits(3, 8); }, SCRIPT_MSG_SET_LIGHTMAP) == "frame lightmap visual 3\n");
    CHECK(Send([](C_BitStream &o) { o.WriteString("driverv"); o.WriteBits(4, 8); }, SCRIPT_MSG_SET_LIGHTMAP) == "frame lightmap driverv 4\n");
    g_visualType = 0;
    CHECK(Send([](C_BitStream &o) { o.WriteString("visual"); o.WriteBits(3, 8); }, SCRIPT_MSG_SET_LIGHTMAP).empty());
    CHECK(Send([](C_BitStream &o) { o.WriteBits(9, 8); o.WriteString("plain"); }, SCRIPT_MSG_CREATE_PARTICLE) == "particle 9 plain 1 0\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(9, 8); o.WriteString("nosuch"); }, SCRIPT_MSG_CREATE_PARTICLE).empty());
    g_groupFound = true;
    CHECK(Send([](C_BitStream &o) { o.WriteString("group"); o.WriteBits(2, 8); }, SCRIPT_MSG_SET_GROUP_LIGHTMAP) == "group lightmap 2\n");
    g_groupFound = false;
    CHECK(Send([](C_BitStream &o) { o.WriteString("group"); o.WriteBits(2, 8); }, SCRIPT_MSG_SET_GROUP_LIGHTMAP).empty());
    g_groups = nullptr;
    CHECK(Send([](C_BitStream &o) { o.WriteString("group"); o.WriteBits(2, 8); }, SCRIPT_MSG_SET_GROUP_LIGHTMAP).empty());

    CHECK(Send([](C_BitStream &o) { o.WriteBits(1, 1); }, SCRIPT_MSG_WEATHER_ON) == "weather 0 1\nrain 1\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(0, 1); }, SCRIPT_MSG_WEATHER_ON) == "weather 0 0\nrain 0\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(11, 32); }, SCRIPT_MSG_WEATHER_COUNT) == "weather 10 11\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(12, 32); }, SCRIPT_MSG_WEATHER_SPEED) == "weather 5 12\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(13, 32); }, SCRIPT_MSG_WEATHER_COLOR_IN) == "weather 3 13\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(14, 32); }, SCRIPT_MSG_WEATHER_COLOR_OUT) == "weather 4 14\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(0xFFFE, 16); }, SCRIPT_MSG_THUNDERSTORM) == "lightning -2\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(21, 32); o.WriteBits(22, 32); }, SCRIPT_MSG_WEATHER_SIZE) == "weather 6 21\nweather 7 22\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(1, 1); }, SCRIPT_MSG_WEATHER_MODE) == "weather 14 1\nrain 0\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(0, 1); }, SCRIPT_MSG_WEATHER_MODE) == "weather 14 0\nrain 1\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(2, 32); o.WriteBits(4, 32); }, SCRIPT_MSG_WEATHER_SPACE) ==
          "weather 8 1073741824\nweather 9 1082130432\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(31, 32); o.WriteBits(32, 32); o.WriteBits(33, 32); }, SCRIPT_MSG_WEATHER_DIR) == "weather 11 31\nweather 12 32\nweather 13 33\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(1, 1); }, SCRIPT_MSG_SUBTITLES_ON) == "subtitles 1\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(0, 1); }, SCRIPT_MSG_SUBTITLES_ON) == "subtitles 0\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(90000001, 32); }, SCRIPT_MSG_SUBTITLES_TEXT) == "subtitle 90000001\n");
    CHECK(Send([](C_BitStream &) {}, 0x1F0).empty());   // (an unknown type)
    // No scene: the weather's effects only; nothing else.
    GetGame()->m_scene = nullptr;
    CHECK(Send([](C_BitStream &o) { o.WriteBits(1, 1); }, SCRIPT_MSG_WEATHER_ON) == "rain 1\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(1, 1); }, SCRIPT_MSG_WEATHER_MODE) == "rain 0\n");
    CHECK(Send([](C_BitStream &o) { o.WriteBits(11, 32); }, SCRIPT_MSG_WEATHER_COUNT).empty());
    CHECK(Send([](C_BitStream &o) { o.WriteBits(1, 32); o.WriteBits(2, 32); }, SCRIPT_MSG_WEATHER_SIZE).empty());
    CHECK(Send([](C_BitStream &o) { o.WriteBits(1, 32); o.WriteBits(2, 32); }, SCRIPT_MSG_WEATHER_SPACE).empty());
    CHECK(Send([](C_BitStream &o) { o.WriteBits(1, 32); o.WriteBits(2, 32); o.WriteBits(3, 32); }, SCRIPT_MSG_WEATHER_DIR).empty());
    CHECK(Send([](C_BitStream &o) { o.WriteString("x"); o.WriteBits(1, 8); }, SCRIPT_MSG_SET_LIGHTMAP).empty());
    CHECK(setOn("plain", true).empty());
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(&g_scene);
    // Not a client: nothing.
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(setOn("plain", true).empty());
    g_gameRole = GAME_ROLE_LOCAL;
    CHECK(!setOn("plain", true).empty());
}

void RunTests()
{
    TestSave();
    TestLoad();
    TestState();
    TestMessages();
}

}  // namespace
