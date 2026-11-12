// Part 7 of the human harness (HUMAN_MIXER builds, build_mixer.bat; included after move_test.inc): the animation mixer,
// game/human_anim_mixer.cpp, against the original's (C_HumanMovement +0x260, the original's layout: the movement's
// m_mixer field compares it whole). The model's eight stages are a fake both sides drive through the model's vtable
// (SetAnimation 0x84 .. AdvanceStages 0xB0, SetPoseWeight 0xC4, NumTracks / GetTracks 0xD0 / 0xD4): their animation,
// weight, time, flags and activity kept per model, shared words (restored for the original, compared after). The
// animation sets are fakes too (Release 0x00, GetEndTime 0x08; reference count and play mode shared).
//
// Part 7a: the tracks and the queues (Init, Done, Relink, UpdateStages, Play, SyncTrackTime, Update, the velocities and
// moves, QueueAnim / QueueCarry / PushCarry / ClearQueue / LockQueue, the track getters and setters, PlayTrack3,
// FadeOutTrack3, SetHoldTime, NotifyFrames, MixerBlendProgress). 7b: the note keys (NoteHook / OnNote and the note
// sounds, AttachModelUpdateHook / DetachModelUpdateHook) and saved games (SaveGame, LoadGame).

namespace {
// ---- the fake animation sets
struct S_FakeAnimSet {
    void **vt;
    int refCount;                                       // +0x04 (I3D_animation_set's)
    int playMode;                                       // +0x08
    const char *fileName;                               // +0x0C
    int endTime;                                        // the fake's GetEndTime
};
constexpr int ANIM_SETS = 6;
S_FakeAnimSet g_animSets[ANIM_SETS];
void *g_animSetVt[16];
const char *const kAnimSetNames[ANIM_SETS] = {"anims\\walk.i3d", "anims\\run.i3d", "anims\\stand.i3d", "carry.i3d", "anims\\sub\\idle.i3d",
                                              nullptr};
const int kAnimSetEnds[ANIM_SETS] = {900, 640, 2400, 1200, 333, 1700};
uint32_t __stdcall FA_Release(S_FakeAnimSet *self)
{
    Log(self, "AnimRelease");
    return static_cast<uint32_t>(--self->refCount);
}
int __stdcall FA_GetEndTime(S_FakeAnimSet *self) { return self->endTime; }
void __stdcall FA_Unexpected(void *)
{
    std::printf("unexpected I3D_animation_set call\n");
    std::exit(3);
}
I3D_animation_set *AnimSet(int i) { return reinterpret_cast<I3D_animation_set *>(&g_animSets[i]); }
I3D_animation_set *RandomSet(float none = 0.25f) { return P(none) ? nullptr : AnimSet(I(0, ANIM_SETS - 1)); }

// ---- the fake model stages (two models: the human's and another for Relink)
struct S_ModelStages {
    void *anim[MIXER_STAGES];
    float weight[MIXER_STAGES];
    int time[MIXER_STAGES];
    int flags[MIXER_STAGES];
    int active[MIXER_STAGES];
    int poseWeight;
};
S_ModelStages g_modelStages[2];
I3D_frame *g_mixModel[2];
int ModelIndex(const void *self)
{
    for (int i = 0; i < 2; ++i)
        if (g_mixModel[i] == self)
            return i;
    return -1;
}
S_ModelStages *StagesOf(const void *self)
{
    const int i = ModelIndex(self);
    if (i < 0) {
        std::printf("model stage call on %s\n", NameOf(self).c_str());
        std::exit(3);
    }
    return &g_modelStages[i];
}
bool StageOk(int stage)
{
    if (stage >= 0 && stage < MIXER_STAGES)
        return true;
    std::printf("stage %d\n", stage);
    std::exit(3);
}
// (Set by part 11's Init / Reset tests: the first track's animation is played with a crossfade the original leaves
// unset, so its weight is undefined there: logged and stored as -1 on both sides.)
bool g_undefinedTrack0Weight = false;
void __stdcall FM_SetAnimation(I3D_frame *self, void *anim, int stage, float weight, int flags)
{
    if (g_undefinedTrack0Weight && anim && stage < 2)
        weight = -1.0f;
    Log(self, ("SetAnimation " + NameOf(anim)).c_str(), {double(stage), weight, double(flags)});
    S_ModelStages *s = StagesOf(self);
    StageOk(stage);
    s->anim[stage] = anim;
    s->weight[stage] = weight;
    s->time[stage] = 0;
    s->flags[stage] = flags;
    s->active[stage] = anim != nullptr;
}
void *__stdcall FM_GetAnimation(I3D_frame *self, int stage) { return StageOk(stage) ? StagesOf(self)->anim[stage] : nullptr; }
void __stdcall FM_StopAnimation(I3D_frame *self, int stage)
{
    Log(self, "StopAnimation", {double(stage)});
    StageOk(stage);
    StagesOf(self)->active[stage] = 0;
}
void __stdcall FM_SetAnimWeight(I3D_frame *self, int stage, float weight)
{
    Log(self, "SetAnimWeight", {double(stage), weight});
    StageOk(stage);
    StagesOf(self)->weight[stage] = weight;
}
float __stdcall FM_GetAnimWeight(I3D_frame *self, int stage) { return StageOk(stage) ? StagesOf(self)->weight[stage] : 0.0f; }
void __stdcall FM_SetAnimTime(I3D_frame *self, int stage, int time)
{
    Log(self, "SetAnimTime", {double(stage), double(time)});
    StageOk(stage);
    StagesOf(self)->time[stage] = time;
}
int __stdcall FM_GetAnimTime(I3D_frame *self, int stage) { return StageOk(stage) ? StagesOf(self)->time[stage] : 0; }
bool __stdcall FM_IsAnimActive(I3D_frame *self, int stage) { return StageOk(stage) && StagesOf(self)->active[stage] != 0; }
void __stdcall FM_AdvanceStages(I3D_frame *self, const int *times)
{
    S_Call c{NameOf(self) + ".AdvanceStages", {}};
    for (int i = 0; i < MIXER_STAGES; ++i)
        c.nums.push_back(times[i]);
    g_calls.push_back(c);
    S_ModelStages *s = StagesOf(self);
    for (int i = 0; i < MIXER_STAGES; ++i)
        if (s->anim[i])
            s->time[i] += times[i];
}
void __stdcall FM_SetPoseWeight(I3D_frame *self, int value)
{
    Log(self, "SetPoseWeight", {double(value)});
    StagesOf(self)->poseWeight = value;
}
// The model's parts (NotifyFrames): their frame at +0xEC, OnStage at their vtable's 0x00 (logged).
struct S_FakePart {
    void **vt;
    uint8_t pad[0xE8];
    I3D_frame *frame;                                   // +0xEC
};
constexpr int MODEL_PARTS = 4;
S_FakePart g_parts[MODEL_PARTS];
S_FakePart *g_partList[MODEL_PARTS];
void *g_partVt[4];
int g_numParts = MODEL_PARTS;                           // what NumTracks answers (both sides)
void __stdcall FP_OnStage(S_FakePart *self, int stage) { Log(self, "OnStage", {double(stage)}); }
int __stdcall FM_NumTracks(I3D_frame *) { return g_numParts; }
void *__stdcall FM_GetTracks(I3D_frame *) { return g_partList; }
// SetCallback (0x28): the original's NoteHook stored as the port's (one value for both sides: the frames' hook word is
// shared); logged as which hook.
I3D_CALLBACK *CanonHook(I3D_CALLBACK *cb)
{
    if (cb == reinterpret_cast<I3D_CALLBACK *>(OriginalAt<void *>(0x007751e0)))
        return &C_HumanAnimMixer::NoteHook;
    return cb;
}
void __stdcall FM_SetCallback(I3D_frame *self, I3D_CALLBACK *cb, uint32_t messages)
{
    cb = CanonHook(cb);
    Log(self, "SetCallback", {double(cb == nullptr ? 0 : cb == &C_HumanAnimMixer::NoteHook ? 1 : 2), double(messages)});
    self->m_callback = cb;
}

// ---- the original's side
// The animation cache's Get (0x0041fc10, thiscall): a fake set by its file name (one reference more).
const char *g_cacheMissing = nullptr;                   // a name the cache does not find (both sides)
HRESULT CacheGet(const void *self, const char *name, I3D_animation_set **out)
{
    Log(self, (std::string("CacheGet ") + name).c_str());
    *out = nullptr;
    if (g_cacheMissing && std::strcmp(name, g_cacheMissing) == 0)
        return static_cast<HRESULT>(0x80004005);
    for (int i = 0; i < ANIM_SETS; ++i) {
        const char *file = kAnimSetNames[i];
        if (!file)
            continue;
        const char *slash = std::strrchr(file, '\\');
        if (std::strcmp(slash ? slash + 1 : file, name) == 0 || std::strcmp(file, name) == 0) {
            ++g_animSets[i].refCount;
            *out = AnimSet(i);
            return I3D_OK;
        }
    }
    return static_cast<HRESULT>(0x80004005);
}
HRESULT __attribute__((thiscall)) OX_CacheGet(void *self, const char *name, I3D_animation_set **out, void *, void *)
{
    return CacheGet(self, name, out);
}
void *MixerOf(void *locomotion) { return static_cast<uint8_t *>(locomotion) - 0xAC0 + 0x260; }
// The original's C runtime sscanf and atoi take its locks, which its start-up (never run here) sets up: the test's own
// serve its note keys.
int __cdecl OX_sscanf(const char *text, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int r = std::vsscanf(text, format, args);
    va_end(args);
    return r;
}
int __cdecl OX_atoi(const char *text) { return std::atoi(text); }
}  // namespace

template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *fileName, I3D_animation_set **out, T_CacheErrorProc, void *)
{
    return CacheGet(this, fileName, out);
}

namespace {
// Differences found after both sides ran (arrays the functions change, float results).
std::string g_mixDiffers;
template <class P_, class O_> void DiffMix(const char *what, P_ port, O_ original)
{
    g_mixDiffers.clear();
    DiffCall(what, port, [&](void *self) { return original(MixerOf(self)); });
    if (!g_mixDiffers.empty()) {
        static int shown;
        if (shown++ < 12)
            std::printf("  %s: %s\n", what, g_mixDiffers.c_str());
    }
    CHECK(g_mixDiffers.empty());
}
void NoteDiffer(const std::string &why)
{
    if (g_mixDiffers.empty())
        g_mixDiffers = why;
}
// A float result of both sides: exact.
int FloatBits(float f)
{
    int bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
template <size_t N> void CompareBytes(const void *port, const void *orig, const char *what)
{
    if (std::memcmp(port, orig, N) != 0)
        NoteDiffer(std::string(what) + " differ");
}

typedef void(__attribute__((thiscall)) * T_XPtr)(void *, void *);
typedef void(__attribute__((thiscall)) * T_XPtr2)(void *, void *, void *);
typedef void(__attribute__((thiscall)) * T_XStages)(void *, int, int, int, const void *);
typedef void(__attribute__((thiscall)) * T_XPlay)(void *, void *, const void *, const int *, const int *, int);
typedef void(__attribute__((thiscall)) * T_XSync)(void *, const void *, int);
typedef void(__attribute__((thiscall)) * T_XVel)(void *, const S_vector4 *, float, float, S_vector4 *);
typedef void(__attribute__((thiscall)) * T_XMove)(void *, const S_vector4 *, int);
typedef void(__attribute__((thiscall)) * T_XMoveTo)(void *, const S_vector4 *, float, int);
typedef void(__attribute__((thiscall)) * T_XIntFloat)(void *, int, float);
typedef void(__attribute__((thiscall)) * T_XInt2)(void *, int, int);
typedef float(__attribute__((thiscall)) * T_XQueue)(void *, void *);
typedef void(__attribute__((thiscall)) * T_XCPtr)(void *, const void *);
typedef int(__attribute__((thiscall)) * T_XGetInt)(void *, int);
typedef int(__attribute__((thiscall)) * T_XBool2)(void *, int, int);
typedef float(__attribute__((thiscall)) * T_XGetFloat)(void *, int);
typedef const int *(__attribute__((thiscall)) * T_XEvents)(void *, int, int *);
typedef void(__attribute__((thiscall)) * T_XTrack3)(void *, void *, int, int, int);
typedef void(__attribute__((thiscall)) * T_XNotify)(void *, int, void **, int);
typedef float(__stdcall * T_XBlend)(float, float, float);

// The anim ids the tests use: the stand group, stance-specific ones, others, moving ones, none.
const int kAnimIds[] = {0x2C, 0x42, 0x2D,0x2E, 0x37, 0x38, 0x3F, 0x40, 0x0E, 0x1A, 0x20, 0x21, 0x22, 0x50, 0x6A, 190, 195, 205, HUMAN_ANIM_NONE};
const int kCarryIds[] = {0x5B, 0x5B, 3, 7, 12};
const int kActivityIds[] = {0x4B, 0x4B, 5, 9};
template <size_t N> int Pick(const int (&ids)[N]) { return ids[I(0, static_cast<int>(N) - 1)]; }
S_HumanAnimRef RandomRef()
{
    S_HumanAnimRef r;
    r.anim = Pick(kAnimIds);
    r.stance = I(0, 2);
    r.sub = I(0, 2);
    r.valid = P(0.7f);
    return r;
}
void RandomState(S_HumanAnimState &s)
{
    s.anim = RandomRef();
    s.carryAnim = Pick(kCarryIds);
    s.carryVariant = I(0, 2);
    s.unknown18 = I(0, 3);
    s.activity = Pick(kActivityIds);
    s.activityVariant = I(0, 2);
}
int MaybeTime(float none, int lo, int hi) { return P(none) ? MOVE_NO_TIME : I(lo, hi); }
S_vector4 RandomVector(float scale)
{
    S_vector4 v;
    v.x = U(-scale, scale);
    v.y = U(-scale * 0.3f, scale * 0.3f);
    v.z = U(-scale, scale);
    v.w = U(-1, 1);
    return v;
}
// A random mixer (and model stages) for the next call; `same` keeps the anims close to each other so that the
// "same as" branches run.
void RandomMixer()
{
    C_HumanAnimMixer &x = g_h.movement->m_mixer;
    x.m_model = static_cast<I3D_model *>(g_mixModel[0]);
    x.m_noteFrame = P(0.9f) ? g_pelvis : nullptr;
    x.m_owner = g_h.human;
    for (S_MixerTrack &t : x.m_tracks) {
        t.current = P(0.25f) ? -1 : I(0, 1);
        t.time = P(0.05f) ? 0x7FFFFC00 + I(0, 100) : I(-1, 2500);
        t.hold = P(0.6f) ? 0 : P(0.5f) ? -1 : P(0.2f) ? 1 : I(1, 900);
        t.eventCount = I(0, MIXER_TRACK_EVENTS);
        for (int &e : t.events)
            e = I(0, 30);
        t.length = I(200, 2500);
        t.reversed = P(0.2f);
    }
    for (S_MixerStage &s : x.m_stages) {
        s.weight = P(0.2f) ? 1.0f : U(0, 1);
        s.blendTime = P(0.2f) ? 0.0f : U(40, 500);
        s.blendElapsed = P(0.25f) ? MIXER_NO_BLEND : P(0.1f) ? U(-30, 0) : U(0, 600);
        s.crossfade = P(0.5f);
        s.flags = P(0.25f) ? MIXER_STAGE_FADE_OUT : 0;
        s.rate = P(0.7f) ? 1.0f : P(0.5f) ? 0.0f : U(0.2f, 2.0f);
        s.speed = P(0.7f) ? 1.0f : U(0.3f, 1.2f);
        s.finished = P(0.3f);
        s.tag = I(-1, 3);
    }
    RandomState(x.m_previous);
    RandomState(x.m_current);
    if (P(0.3f))
        x.m_previous.anim = x.m_current.anim;
    x.m_track3FadeOut = P(0.5f) ? -1 : P(0.1f) ? 0 : I(0, 600);
    x.m_blendProgress = P(0.3f) ? 0.0f : P(0.3f) ? 1.0f : U(0, 1);
    x.m_velocityA = RandomVector(3.0f);
    x.m_weightA = P(0.3f) ? 0.0f : U(0, 1);
    x.m_velocityB = P(0.1f) ? S_vector4{} : RandomVector(P(0.3f) ? 0.5f : 4.0f);
    x.m_weightB = P(0.2f) ? 0.0f : U(0, 1.5f);
    x.m_maxSpeed = P(0.4f) ? 0.0f : U(0.3f, 3.0f);
    x.m_move = RandomVector(0.5f);
    x.m_addMove = RandomVector(1.0f);
    x.m_addMoveTime = I(0, 600);
    x.m_addMoveDuration = MaybeTime(0.5f, 0, 600);
    x.m_moveToSpeed = P(0.1f) ? 0.0f : U(0.2f, 4.0f);
    x.m_moveToTarget = RandomVector(3.0f);
    x.m_moveToTarget.x += g_mixModel[0]->GetWorldPos().x;
    x.m_moveToTarget.z += g_mixModel[0]->GetWorldPos().z;
    if (P(0.05f)) {
        const S_vector at = g_mixModel[0]->GetWorldPos();
        x.m_moveToTarget.x = at.x;
        x.m_moveToTarget.y = at.y;
        x.m_moveToTarget.z = at.z;
    }
    x.m_moveToTime = I(0, 600);
    x.m_moveToDuration = MaybeTime(0.5f, 0, 600);
    x.m_extraMove = P(0.6f) ? S_vector4{} : RandomVector(1.0f);
    x.m_extraMoveTime = I(0, 500);
    x.m_extraMoveDuration = I(1, 600);
    x.m_addMove2 = RandomVector(1.0f);
    x.m_addMove2Time = I(0, 600);
    x.m_addMove2Duration = MaybeTime(0.5f, 0, 600);
    x.m_slide = RandomVector(1.0f);
    x.m_slideVelocity = RandomVector(0.1f);
    x.m_slideStepTime = I(0, 79);
    x.m_slideTime = I(0, 400);
    x.m_slideDuration = MaybeTime(0.5f, 1, 800);
    x.m_speedScale = P(0.5f) ? 1.0f : U(0.5f, 1.5f);
    x.m_stepSoundTime = I(0, 400);
    x.m_holdTime = MaybeTime(0.6f, 0, 1500);
    auto queue = [](auto &q) {
        q.read = I(0, 1);
        q.write = P(0.4f) ? q.read : I(0, 1);
        q.locked = P(0.3f);
    };
    queue(x.m_animQueue);
    for (S_HumanAnimRef &e : x.m_animQueue.entries)
        e = P(0.3f) ? x.m_current.anim : RandomRef();
    queue(x.m_carryQueue);
    for (S_MixerCarry &e : x.m_carryQueue.entries) {
        e.anim = Pick(kCarryIds);
        e.variant = I(0, 2);
        e.unknown08 = I(0, 2);
    }
    queue(x.m_activityQueue);
    for (S_MixerActivity &e : x.m_activityQueue.entries) {
        e.activity = Pick(kActivityIds);
        e.variant = I(0, 2);
    }
    for (int m = 0; m < 2; ++m) {
        S_ModelStages &s = g_modelStages[m];
        for (int i = 0; i < MIXER_STAGES; ++i) {
            s.anim[i] = RandomSet(0.3f);
            s.weight[i] = P(0.2f) ? 0.0f : U(0, 1);
            s.time[i] = I(0, 2500);
            if (s.anim[i] && P(0.1f))                   // (at its end: nothing left to play)
                s.time[i] = reinterpret_cast<S_FakeAnimSet *>(s.anim[i])->endTime - I(0, 1);
            s.flags[i] = I(0, 3);
            s.active[i] = P(0.8f);
        }
        s.poseWeight = I(0, 2);
    }
    for (S_FakeAnimSet &a : g_animSets)
        a.playMode = I(1, 2);
    g_animSets[ANIM_SETS - 1].endTime = P(0.3f) ? 0 : P(0.2f) ? 1 : kAnimSetEnds[ANIM_SETS - 1];   // (an empty animation)
    g_script.localPlayer = P(0.4f);
    g_script.serverSimulated = P(0.2f);
    g_gameRole = P(0.5f) ? GAME_ROLE_LOCAL : (P(0.5f) ? GAME_ROLE_CLIENT : GAME_ROLE_SERVER);
}

void SetUpMixer()
{
    for (void *&slot : g_animSetVt)
        slot = reinterpret_cast<void *>(FA_Unexpected);
    g_animSetVt[0x00 / 4] = reinterpret_cast<void *>(FA_Release);
    g_animSetVt[0x08 / 4] = reinterpret_cast<void *>(FA_GetEndTime);
    for (int i = 0; i < ANIM_SETS; ++i) {
        S_FakeAnimSet &a = g_animSets[i];
        a.vt = g_animSetVt;
        a.refCount = 1;
        a.playMode = 2;
        a.fileName = kAnimSetNames[i];
        a.endTime = kAnimSetEnds[i];
        g_names[&a] = "anim" + std::to_string(i);
        g_sharedWords.push_back({reinterpret_cast<uint32_t *>(&a.refCount), "anim refs " + std::to_string(i)});
        g_sharedWords.push_back({reinterpret_cast<uint32_t *>(&a.playMode), "anim mode " + std::to_string(i)});
    }
    g_mixModel[0] = g_h.frame;
    g_mixModel[1] = NewFrame(FRAME_MODEL, "human2", S_vector(22, 1, 20), Identity(), Box(S_vector(0.3f, 0.9f, 0.3f)), "", g_sector);
    I3D_frame *note2 = NewFrame(FRAME_DUMMY, "a", S_vector(22, 1.9f, 20), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_mixModel[1]);
    g_names[note2] = "a2";
    for (int m = 0; m < 2; ++m)
        ShareBlock(&g_modelStages[m], sizeof(S_ModelStages), m ? "stages2" : "stages");
    for (I3D_frame *f : {g_h.frame, g_pelvis, g_mixModel[1], note2})
        g_sharedWords.push_back({reinterpret_cast<uint32_t *>(&f->m_callback), "hook " + NameOf(f)});
    g_frameVt[0x28 / 4] = reinterpret_cast<void *>(FM_SetCallback);
    g_frameVt[0x84 / 4] = reinterpret_cast<void *>(FM_SetAnimation);
    g_frameVt[0x88 / 4] = reinterpret_cast<void *>(FM_GetAnimation);
    g_frameVt[0x8C / 4] = reinterpret_cast<void *>(FM_StopAnimation);
    g_frameVt[0x94 / 4] = reinterpret_cast<void *>(FM_SetAnimWeight);
    g_frameVt[0x98 / 4] = reinterpret_cast<void *>(FM_GetAnimWeight);
    g_frameVt[0x9C / 4] = reinterpret_cast<void *>(FM_SetAnimTime);
    g_frameVt[0xA0 / 4] = reinterpret_cast<void *>(FM_GetAnimTime);
    g_frameVt[0xAC / 4] = reinterpret_cast<void *>(FM_IsAnimActive);
    g_frameVt[0xB0 / 4] = reinterpret_cast<void *>(FM_AdvanceStages);
    g_frameVt[0xC4 / 4] = reinterpret_cast<void *>(FM_SetPoseWeight);
    g_frameVt[0xD0 / 4] = reinterpret_cast<void *>(FM_NumTracks);
    g_frameVt[0xD4 / 4] = reinterpret_cast<void *>(FM_GetTracks);
    for (void *&slot : g_partVt)
        slot = reinterpret_cast<void *>(FP_OnStage);
    I3D_frame *partFrames[MODEL_PARTS] = {g_pelvis, g_footU, g_footQ, g_shotDummy[0]};
    for (int i = 0; i < MODEL_PARTS; ++i) {
        g_parts[i].vt = g_partVt;
        g_parts[i].frame = partFrames[i];
        g_partList[i] = &g_parts[i];
        g_names[&g_parts[i]] = "part" + std::to_string(i);
    }
    // The original's mixer functions the earlier parts redirected to loggers run for real here.
    for (uint32_t at : {0x00773ef0u, 0x00773f80u, 0x00773de0u, 0x00773cb0u, 0x007751b0u, 0x00773fc0u, 0x00772e60u, 0x00772860u})
        if (!RestoreOriginal(at)) {
            std::printf("mixer: %08x was not redirected\n", at);
            std::exit(3);
        }
    RedirectOriginal(0x0041fc10, reinterpret_cast<const void *>(OX_CacheGet));
    RedirectOriginal(0x007e604c, reinterpret_cast<const void *>(OX_sscanf));
    RedirectOriginal(0x007e65f0, reinterpret_cast<const void *>(OX_atoi));
    g_names[&g_animationCache] = g_names[OriginalAt<void *>(0x008aed28)] = "animationCache";
    g_names[&g_h.movement->m_mixer] = g_names[o_movement + 0x260] = "mixer";
}

// ---- 7a: the tracks and the queues
void TestMixerSmall()
{
    C_HumanAnimMixer &x = g_h.movement->m_mixer;
    for (int i = 0; i < 60000 / QUICK; ++i) {
        RandomMixer();
        const int track = I(0, 3);
        const int track3 = I(0, 2);
        switch (i % 30) {
        case 0:
            DiffMix("Init", [&] { x.Init(g_h.human); return 0; },
                    [&](void *self) { OriginalAt<T_XPtr>(0x00772520)(self, o_human); return 0; });
            break;
        case 1:
            DiffMix("Done", [&] { x.Done(); return 0; }, [&](void *self) { OriginalAt<T_MVoid>(0x00772810)(self); return 0; });
            break;
        case 2: {
            // (Onto the other model from this one, from none, or from itself.)
            const int kind = I(0, 3);
            I3D_model *to = static_cast<I3D_model *>(g_mixModel[kind == 3 ? 0 : 1]);
            I3D_model *from = kind == 0 ? nullptr : static_cast<I3D_model *>(g_mixModel[kind == 2 ? 1 : 0]);
            DiffMix("Relink", [&] { x.Relink(to, from); return 0; },
                    [&](void *self) { OriginalAt<T_XPtr2>(0x00772860)(self, to, from); return 0; });
            break;
        }
        case 3:
        case 4:
        case 5: {
            const int step = P(0.1f) ? -1 : P(0.1f) ? 0 : I(1, 150);
            const bool start = P(0.2f);
            S_MixerAnim anims[MIXER_TRACKS];
            for (S_MixerAnim &a : anims)
                a.anim = RandomSet(0.4f);
            const bool withAnims = P(0.7f);
            const int unused = I(0, 100);
            DiffMix("UpdateStages", [&] { x.UpdateStages(unused, step, start, withAnims ? anims : nullptr); return 0; },
                    [&](void *self) {
                        OriginalAt<T_XStages>(0x00772940)(self, unused, step, start, withAnims ? anims : nullptr);
                        return 0;
                    });
            break;
        }
        case 6:
        case 7: {
            const int time = P(0.1f) ? I(200, 700) : I(1, 120);
            DiffMix("Update", [&] { x.Update(time); return 0; },
                    [&](void *self) { OriginalAt<T_MInt>(0x00772e60)(self, time); return 0; });
            break;
        }
        case 8:
        case 9: {
            // The requests: anims for the tracks, the body's / carry / activity ids (often those blending out).
            S_MixerAnim anims[MIXER_TRACKS];
            for (S_MixerAnim &a : anims) {
                a.anim = RandomSet(0.35f);
                a.weight = P(0.5f) ? 1.0f : U(0.1f, 1.0f);
                a.mode = P(0.4f) ? 2 : P(0.5f) ? 1 : (P(0.5f) ? MIXER_SYNC_TIME_LEFT : MIXER_SYNC_SHARE) | I(1, 2);
                a.blendTime = P(0.2f) ? 0.0f : U(50, 400);
                a.crossfade = P(0.5f);
            }
            S_HumanAnimRef anim = P(0.4f) ? x.m_previous.anim : RandomRef();
            int carry[3] = {P(0.4f) ? x.m_previous.carryAnim : Pick(kCarryIds), P(0.5f) ? x.m_previous.carryVariant : I(0, 2), I(0, 2)};
            int activity[2] = {P(0.4f) ? x.m_previous.activity : Pick(kActivityIds),
                               P(0.5f) ? x.m_previous.activityVariant : I(0, 2)};
            const bool restart = P(0.3f);
            S_MixerAnim portAnims[MIXER_TRACKS], origAnims[MIXER_TRACKS];
            DiffMix("Play",
                    [&] {
                        std::memcpy(portAnims, anims, sizeof anims);
                        x.Play(portAnims, &anim, carry, activity, restart);
                        return 0;
                    },
                    [&](void *self) {
                        std::memcpy(origAnims, anims, sizeof anims);
                        OriginalAt<T_XPlay>(0x00773830)(self, origAnims, &anim, carry, activity, restart);
                        CompareBytes<sizeof anims>(portAnims, origAnims, "anims");
                        return 0;
                    });
            break;
        }
        case 10: {
            S_MixerAnim anims[MIXER_TRACKS];
            for (S_MixerAnim &a : anims)
                a.mode = P(0.3f) ? 2 : (P(0.5f) ? MIXER_SYNC_TIME_LEFT : MIXER_SYNC_SHARE) | I(0, 3);
            DiffMix("SyncTrackTime", [&] { x.SyncTrackTime(anims, track3); return 0; },
                    [&](void *self) { OriginalAt<T_XSync>(0x007736c0)(self, anims, track3); return 0; });
            break;
        }
        case 11:
        case 12: {
            const S_vector4 v = RandomVector(4.0f);
            const float weight = P(0.2f) ? 0.0f : U(0, 1.5f);
            const float maxSpeed = P(0.4f) ? 0.0f : U(0.3f, 3.0f);
            const S_vector4 seed = RandomVector(1.0f);
            S_vector4 portResult = seed, origResult = seed;
            const bool replace = i % 30 == 12;
            DiffMix(replace ? "ReplaceVelocity" : "SetVelocity",
                    [&] {
                        if (replace)
                            x.ReplaceVelocity(v, weight, maxSpeed, &portResult);
                        else
                            x.SetVelocity(v, weight, maxSpeed, &portResult);
                        return 0;
                    },
                    [&](void *self) {
                        OriginalAt<T_XVel>(replace ? 0x00773de0 : 0x00773cb0)(self, &v, weight, maxSpeed, &origResult);
                        // (x, y, z: the w of the result is the caller's.)
                        CompareBytes<12>(&portResult, &origResult, "result");
                        return 0;
                    });
            break;
        }
        case 13: {
            const S_vector4 v = RandomVector(2.0f);
            const int duration = I(0, 900);
            const int which = I(0, 3);
            const float speed = U(0.1f, 4.0f);
            static const char *const names[] = {"AddMove", "AddMove2", "Slide", "MoveTo"};
            DiffMix(names[which],
                    [&] {
                        if (which == 0)
                            x.AddMove(v, duration);
                        else if (which == 1)
                            x.AddMove2(v, duration);
                        else if (which == 2)
                            x.Slide(v, duration < 1 ? 1 : duration);
                        else
                            x.MoveTo(v, speed, duration);
                        return 0;
                    },
                    [&](void *self) {
                        if (which == 0)
                            OriginalAt<T_XMove>(0x00773ef0)(self, &v, duration);
                        else if (which == 1)
                            OriginalAt<T_XMove>(0x00773f80)(self, &v, duration);
                        else if (which == 2)
                            OriginalAt<T_XMove>(0x00773fc0)(self, &v, duration < 1 ? 1 : duration);
                        else
                            OriginalAt<T_XMoveTo>(0x00773f30)(self, &v, speed, duration);
                        return 0;
                    });
            break;
        }
        case 14: {
            S_HumanAnimRef seed = P(0.3f) ? x.m_previous.anim : P(0.2f) ? x.m_current.anim : RandomRef();
            if (P(0.2f))
                seed = x.m_animQueue.entries[x.m_animQueue.read];
            S_HumanAnimRef portRef = seed, origRef = seed;
            int portF = 0;
            DiffMix("QueueAnim", [&] { portF = FloatBits(x.QueueAnim(&portRef)); return 0; },
                    [&](void *self) {
                        const int f = FloatBits(OriginalAt<T_XQueue>(0x007741f0)(self, &origRef));
                        if (f != portF)
                            NoteDiffer("result");
                        if (!portRef.IsSame(origRef) || portRef.valid != origRef.valid)
                            NoteDiffer("anim " + std::to_string(portRef.anim) + " / " + std::to_string(origRef.anim));
                        return 0;
                    });
            break;
        }
        case 15: {
            int seed[3] = {P(0.3f) ? x.m_previous.carryAnim : P(0.2f) ? x.m_current.carryAnim : Pick(kCarryIds), I(0, 2), I(0, 2)};
            if (P(0.3f)) {
                seed[0] = x.m_previous.carryAnim;
                seed[1] = x.m_previous.carryVariant;
            } else if (P(0.2f)) {
                seed[0] = x.m_carryQueue.entries[x.m_carryQueue.read].anim;
                seed[1] = x.m_carryQueue.entries[x.m_carryQueue.read].variant;
            }
            int portCarry[3], origCarry[3];
            int portF = 0;
            DiffMix("QueueCarry",
                    [&] {
                        std::memcpy(portCarry, seed, sizeof seed);
                        portF = FloatBits(x.QueueCarry(portCarry));
                        return 0;
                    },
                    [&](void *self) {
                        std::memcpy(origCarry, seed, sizeof seed);
                        if (FloatBits(OriginalAt<T_XQueue>(0x007748d0)(self, origCarry)) != portF)
                            NoteDiffer("result");
                        CompareBytes<sizeof seed>(portCarry, origCarry, "carry");
                        return 0;
                    });
            break;
        }
        case 16: {
            const int carry[3] = {Pick(kCarryIds), I(0, 2), I(0, 2)};
            DiffMix("PushCarry", [&] { x.PushCarry(carry); return 0; },
                    [&](void *self) { OriginalAt<T_XCPtr>(0x00774e80)(self, carry); return 0; });
            int activity[2] = {Pick(kActivityIds), I(0, 2)};
            DiffMix("QueueActivity", [&] { return FloatBits(x.QueueActivity(activity)); },
                    [&](void *self) { return FloatBits(OriginalAt<T_XQueue>(0x00774f70)(self, activity)); });
            break;
        }
        case 17:
            DiffMix("ClearQueue", [&] { x.ClearQueue(track); return 0; },
                    [&](void *self) { OriginalAt<T_MInt>(0x00775770)(self, track); return 0; });
            DiffMix("LockQueue", [&] { x.LockQueue(track); return 0; },
                    [&](void *self) { OriginalAt<T_MInt>(0x007758b0)(self, track); return 0; });
            break;
        case 18: {
            const int ms = I(0, 2000);
            DiffMix("GetTrackTime", [&] { return x.GetTrackTime(track); },
                    [&](void *self) { return OriginalAt<T_XGetInt>(0x007750c0)(self, track); });
            DiffMix("HasTrackPlayed", [&] { return int(x.HasTrackPlayed(track, ms)); },
                    [&](void *self) { return int(B(OriginalAt<T_XBool2>(0x00775670)(self, track, ms))); });
            break;
        }
        case 19:
            // (The tracks' lengths not zero here: the original divides as the port does.)
            DiffMix("GetTrackProgress", [&] { return FloatBits(x.GetTrackProgress(track)); },
                    [&](void *self) { return FloatBits(OriginalAt<T_XGetFloat>(0x00775bd0)(self, track)); });
            DiffMix("GetBlendProgress", [&] { return FloatBits(x.GetBlendProgress(track)); },
                    [&](void *self) { return FloatBits(OriginalAt<T_XGetFloat>(0x007758e0)(self, track)); });
            break;
        case 20: {
            const int time = I(0, 600);
            const float rate = P(0.3f) ? 0.0f : U(0.1f, 2.0f);
            DiffMix("FadeOutTrack", [&] { x.FadeOutTrack(track, time); return 0; },
                    [&](void *self) { OriginalAt<T_XInt2>(0x00775120)(self, track, time); return 0; });
            DiffMix("SetTrackRate", [&] { x.SetTrackRate(track, rate); return 0; },
                    [&](void *self) { OriginalAt<T_XIntFloat>(0x007751b0)(self, track, rate); return 0; });
            break;
        }
        case 21: {
            // (HoldTrack and ReleaseTrack on a track without an animation: the port's documented fix, left out.)
            const int time = I(0, 900);
            if (x.m_tracks[track].current == -1)
                x.m_tracks[track].current = I(0, 1);
            DiffMix("HoldTrack", [&] { x.HoldTrack(track, time); return 0; },
                    [&](void *self) { OriginalAt<T_XInt2>(0x007756c0)(self, track, time); return 0; });
            DiffMix("ReleaseTrack", [&] { x.ReleaseTrack(track); return 0; },
                    [&](void *self) { OriginalAt<T_MInt>(0x00775700)(self, track); return 0; });
            break;
        }
        case 22: {
            const float share = U(0, 1);
            DiffMix("HoldTrackAt", [&] { x.HoldTrackAt(track, share); return 0; },
                    [&](void *self) { OriginalAt<T_XIntFloat>(0x00775b70)(self, track, share); return 0; });
            DiffMix("MirrorTrackTime", [&] { x.MirrorTrackTime(track); return 0; },
                    [&](void *self) { OriginalAt<T_MInt>(0x00775b00)(self, track); return 0; });
            break;
        }
        case 23: {
            int portCount = -5, origCount = -5;
            const int *portEvents = nullptr;
            DiffMix("GetEvents",
                    [&] {
                        portEvents = x.GetEvents(track3, &portCount);
                        return 0;
                    },
                    [&](void *self) {
                        const int *events = OriginalAt<T_XEvents>(0x00775740)(self, track3, &origCount);
                        // (Both at the same offset of their mixers.)
                        if (reinterpret_cast<const uint8_t *>(events) - static_cast<const uint8_t *>(self) !=
                            reinterpret_cast<const uint8_t *>(portEvents) - reinterpret_cast<const uint8_t *>(&x))
                            NoteDiffer("events pointer");
                        if (portCount != origCount)
                            NoteDiffer("count");
                        return 0;
                    });
            break;
        }
        case 24: {
            const int tag = I(-1, 3);
            DiffMix("SetTrackTag", [&] { x.SetTrackTag(track, tag); return 0; },
                    [&](void *self) { OriginalAt<T_XInt2>(0x00775a10)(self, track, tag); return 0; });
            break;
        }
        case 25: {
            const int tag = I(-1, 3);
            DiffMix("CheckTrackFinished", [&] { return int(x.CheckTrackFinished(track, tag)); },
                    [&](void *self) { return int(B(OriginalAt<T_XBool2>(0x00775a50)(self, track, tag))); });
            DiffMix("GetTrackTag", [&] { return x.GetTrackTag(track); },
                    [&](void *self) { return OriginalAt<T_XGetInt>(0x00775ad0)(self, track); });
            break;
        }
        case 26: {
            I3D_animation_set *anim = AnimSet(I(0, ANIM_SETS - 1));
            const int blendTime = I(0, 500), fadeOut = I(0, 600);
            const bool loop = P(0.4f);
            DiffMix("PlayTrack3", [&] { x.PlayTrack3(anim, blendTime, fadeOut, loop); return 0; },
                    [&](void *self) { OriginalAt<T_XTrack3>(0x00775c40)(self, anim, blendTime, fadeOut, loop); return 0; });
            break;
        }
        case 27: {
            const int time = I(0, 600);
            DiffMix("FadeOutTrack3", [&] { x.FadeOutTrack3(time); return 0; },
                    [&](void *self) { OriginalAt<T_MInt>(0x00775dd0)(self, time); return 0; });
            break;
        }
        case 28: {
            const int length = I(0, 3000);
            const float share = P(0.3f) ? U(0, 0.22f) : U(0.22f, 1.2f);
            DiffMix("SetHoldTime", [&] { x.SetHoldTime(length, share); return 0; },
                    [&](void *self) { OriginalAt<T_XIntFloat>(0x00776570)(self, length, share); return 0; });
            break;
        }
        default: {
            // The frames to tell about (count + 1 entries: the one past the last moves into a found one's place).
            I3D_frame *pool[] = {g_pelvis, g_footU, g_footQ, g_shotDummy[0], g_shotDummy[1], g_stanceDummy[0], nullptr};
            const int count = I(0, 5);
            I3D_frame *frames[7];
            for (int k = 0; k <= count; ++k)
                frames[k] = pool[I(0, 6)];
            g_numParts = I(0, MODEL_PARTS);
            I3D_frame *portFrames[7], *origFrames[7];
            DiffMix("NotifyFrames",
                    [&] {
                        std::memcpy(portFrames, frames, sizeof frames);
                        x.NotifyFrames(track3, portFrames, count);
                        return 0;
                    },
                    [&](void *self) {
                        std::memcpy(origFrames, frames, sizeof frames);
                        OriginalAt<T_XNotify>(0x00775970)(self, track3, reinterpret_cast<void **>(origFrames), count);
                        CompareBytes<sizeof(void *) * 6>(portFrames, origFrames, "frames");
                        return 0;
                    });
            g_numParts = MODEL_PARTS;
            break;
        }
        }
    }
    // MixerBlendProgress (no state).
    for (int i = 0; i < 20000 / QUICK; ++i) {
        const float duration = P(0.05f) ? 0.0f : U(1, 600);
        const float step = U(0, 150);
        const float elapsed = U(-50, 700);
        const float p = MixerBlendProgress(elapsed, step, duration);
        const float o = OriginalAt<T_XBlend>(0x00772900)(elapsed, step, duration);
        CHECK(FloatBits(p) == FloatBits(o) || (p != p && o != o));
    }
}

// ---- 7b: the note keys and saved games
// The models' frame data: the human's model names the test human (the original's side: its copy), the other none.
S_FrameData g_mixData;
I3D_frame *g_noteChild;                                 // an "a" under the human's note frame ('$' sounds attach to it)
typedef int(__stdcall * T_XNoteHook)(void *, uint32_t, const char *, int);
typedef void(__attribute__((thiscall)) * T_XOnNote)(void *, uint32_t, const char *, int);
typedef void(__fastcall * T_XHook)(void *);
typedef int(__attribute__((thiscall)) * T_XChunk)(void *, C_chunk *);
const char *const kNotes[] = {"0", "1", "^", "c", "d", "e1", "e2", "f", "h", "h2", "k", "l", "ll", "lr", "m", "n", "p", "p2", "s",
                              "s1", "s2", "s3", "t", "u", "w", "w2", "y", "z", "q", "", "!3 7", "!1 0", "!2 12", "!9",
                              "$step.wav 2 30", "$crate\\hit.wav 1.5 12", "$x 3", "$nothing", "@12", "@0", "@7x", nullptr};
const char *RandomNote() { return kNotes[I(0, static_cast<int>(sizeof kNotes / sizeof kNotes[0]) - 1)]; }
// The human's actor on the original's side (its copy) for the original's call.
template <class F> int WithOriginalActor(F call)
{
    C_Actor *was = g_mixData.actor;
    if (was == g_h.human)
        g_mixData.actor = reinterpret_cast<C_Actor *>(o_human);
    const int r = call();
    g_mixData.actor = was;
    return r;
}
void RandomNoteSetting()
{
    g_mixData.actor = P(0.85f) ? static_cast<C_Actor *>(g_h.human) : nullptr;
    g_h.frame->SetData(P(0.9f) ? &g_mixData : nullptr);
    g_firstPerson = P(0.4f);
    g_origFirstPerson[0x58] = g_firstPerson && P(0.5f);
    g_origFirstPerson[0x59] = g_firstPerson && !g_origFirstPerson[0x58];
    *reinterpret_cast<void **>(g_origGame + 0x5C) = g_origFirstPerson;
}

void TestMixerNotes()
{
    C_HumanAnimMixer &x = g_h.movement->m_mixer;
    g_noteChild = NewFrame(FRAME_DUMMY, "a", S_vector(0.0f, 0.1f, 0.2f), Identity(), Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_pelvis);
    g_names[g_noteChild] = "noteChild";
    UpdateTree(g_h.frame);
    g_origSoundVt[0x50 / 4] = reinterpret_cast<void *>(OM_PlayDubbing);
    std::printf("mixer: notes\n");
    for (int i = 0; i < 30000 / QUICK; ++i) {
        RandomMixer();
        RandomNoteSetting();
        // (Room for the note's event: a sixth is the port's documented fix, the original writes it over the count.)
        for (S_MixerTrack &t : x.m_tracks)
            if (t.eventCount == MIXER_TRACK_EVENTS)
                t.eventCount = I(0, MIXER_TRACK_EVENTS - 1);
        const char *note = RandomNote();
        // (A hold note on a track without an animation: the port's documented fix in HoldTrack, left out.)
        if (note && note[0] == 'p') {
            S_MixerTrack &held = x.m_tracks[note[1] == '2' ? 2 : 0];
            if (held.current == -1)
                held.current = I(0, 1);
        }
        const int time = I(0, 2000);
        const uint32_t message = I(0, 3);
        // (The note frame of the human's model, of the other model (no actor), or a frame without a model.)
        I3D_frame *frame = P(0.8f) ? g_pelvis : P(0.5f) ? g_mixModel[1]->FindChildFrame("a", FRAME_MASK_ANY) : g_noteChild;
        switch (i % 3) {
        case 0:
        case 1:
            DiffMix("NoteHook", [&] { return C_HumanAnimMixer::NoteHook(frame, message, reinterpret_cast<uintptr_t>(note), time); },
                    [&](void *) {
                        return WithOriginalActor([&] { return OriginalAt<T_XNoteHook>(0x007751e0)(frame, message, note, time); });
                    });
            break;
        default:
            DiffMix("OnNote", [&] { x.OnNote(message, note, time); return 0; },
                    [&](void *self) { OriginalAt<T_XOnNote>(0x00775250)(self, message, note, time); return 0; });
            break;
        }
    }
    // The hooks of models without an actor.
    std::printf("mixer: hooks\n");
    for (int i = 0; i < 6000 / QUICK; ++i) {
        RandomMixer();
        RandomNoteSetting();
        I3D_frame *pool[] = {g_h.frame, g_mixModel[1], g_pelvis, nullptr};
        I3D_frame *model = pool[I(0, 3)];
        for (I3D_frame *f : {g_pelvis, g_mixModel[1]->FindChildFrame("a", FRAME_MASK_ANY)})
            f->m_callback = P(0.5f) ? nullptr : &C_HumanAnimMixer::NoteHook;
        const bool attach = P(0.5f);
        DiffMix(attach ? "AttachModelUpdateHook" : "DetachModelUpdateHook",
                [&] {
                    if (attach)
                        AttachModelUpdateHook(model);
                    else
                        DetachModelUpdateHook(model);
                    return 0;
                },
                [&](void *) {
                    return WithOriginalActor([&] {
                        OriginalAt<T_XHook>(attach ? 0x007764d0 : 0x00776520)(model);
                        return 0;
                    });
                });
    }
    g_h.frame->SetData(nullptr);
}

void TestMixerSaves()
{
    C_HumanAnimMixer &x = g_h.movement->m_mixer;
    const std::string portFile = TempFile("mixer_port.chk"), origFile = TempFile("mixer_orig.chk");
    std::printf("mixer: saves\n");
    for (int i = 0; i < 4000 / QUICK; ++i) {
        RandomMixer();
        const bool open = P(0.95f);
        std::remove(portFile.c_str());
        std::remove(origFile.c_str());
        DiffMix("SaveGame",
                [&] {
                    C_chunk c;
                    if (open)
                        c.Open(portFile.c_str(), true);
                    return int(x.SaveGame(c));
                },
                [&](void *self) {
                    C_chunk c;
                    if (open)
                        c.Open(origFile.c_str(), true);
                    return int(B(OriginalAt<T_XChunk>(0x00776610)(self, &c)));
                });
        const std::vector<uint8_t> savedPort = FileBytes(portFile), savedOrig = FileBytes(origFile);
        if (savedPort != savedOrig) {
            static int shown;
            size_t at = 0;
            while (at < savedPort.size() && at < savedOrig.size() && savedPort[at] == savedOrig[at])
                ++at;
            if (shown++ < 8)
                std::printf("  mixer SaveGame: files differ at %zu (sizes %zu / %zu)\n", at, savedPort.size(), savedOrig.size());
        }
        CHECK(savedPort == savedOrig);
        CHECK(!open || savedPort.size() > 1000);
        // Loaded back into another state (or nothing open, or another chunk first). (Not a stage's animation without
        // its play mode after it: the original reads the mode anyway, from no chunk, and gets a stack word; SaveGame
        // always writes it.)
        const bool loadOpen = P(0.97f);
        if (P(0.1f)) {
            C_chunk c;
            c.Open(portFile.c_str(), true);
            c.Begin(0x123);
            c.WriteChunk(0x26, U(0.5f, 1.5f));
            c.WriteStringChunk(static_cast<uint16_t>(0x50 + 6 * I(0, 7)), P(0.5f) ? "walk.i3d" : "");
            c.Ascend();
        }
        RandomMixer();
        DiffMix("LoadGame",
                [&] {
                    C_chunk c;
                    if (loadOpen)
                        c.Open(portFile.c_str(), false);
                    return int(x.LoadGame(c));
                },
                [&](void *self) {
                    C_chunk c;
                    if (loadOpen)
                        c.Open(portFile.c_str(), false);
                    return int(B(OriginalAt<T_XChunk>(0x00776f90)(self, &c)));
                });
    }
}

void TestMixer()
{
    SetUpMixer();
    TestMixerSmall();
    TestMixerNotes();
    TestMixerSaves();
}
}  // namespace
