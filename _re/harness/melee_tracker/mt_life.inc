// Human part 82b (included by mt_diff.cpp): the meleeTracker's mission life cycle, save and load, the frame setup, the
// controller swap and the mission properties (SetController, SaveGame, LoadGame, WriteProperties, ReadProperties,
// SetFrame, CopyFrom, StartMission, StopMission, RestartMission, ReloadModel, SetupFrame), against the original's.
// The controllers are each side's own (real: the port's from melee_tracker_ai.cpp, the original's from its code), their
// bytes compared after each operation (the original's layout on both; pointers by name). The chunks are real on both
// sides over files in memory. Faked alike: the actor base's members (Activate, Deactivate, SetFrame, CopyFrom, SaveGame,
// LoadGame), the movement's (Bind, FreeVolumesAndSounds, Reset, SaveGame, LoadGame: the original's through its vtable),
// the model cache's OpenInto, the effects' FadeInModel, the driver's joint overrides (a set per side: Add, Remove,
// Release; the model's SetJointOverrides), the sight module's AddObserver / AddTarget / SetTargetState, the sound
// module's AddListener, the actors module's actors and GetActor, ActorToId, the tracker's own ReloadModel slot.
// The destructor ends a scenario now and then. (Not here: LoadAnimTable, which inlines the original animation cache's
// internals; the constructor.)

namespace H {

// The original's stack zeroed before a call that reads words it never wrote (SetupFrame's look target).
__attribute__((noinline)) void ZeroStack()
{
    uint8_t buf[0x2000];
    std::memset(buf, 0, sizeof buf);
    asm volatile("" : : "r"(buf) : "memory");
}

// ---- the files (part 80's: in memory, both sides' dta functions) ----
std::map<std::string, std::string> g_files;
struct S_OpenFile {
    std::string name;
    size_t at = 0;
};
std::map<int, S_OpenFile> g_openFiles;
int g_nextFile = 1;
int FileOpen(const char *name, bool write)
{
    if (!write && !g_files.count(name)) {
        Log("dta open %s -> -1", name);
        return -1;
    }
    if (write)
        g_files[name].clear();
    g_openFiles[g_nextFile] = S_OpenFile{name, 0};
    return g_nextFile++;
}
int FileSeek(int handle, int offset, int origin)
{
    if (!g_openFiles.count(handle))
        return -1;
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    o.at = origin == 0 ? size_t(offset) : origin == 1 ? o.at + offset : f.size() + offset;
    return int(o.at);
}
int FileRead(int handle, void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    const int got = o.at >= f.size() ? 0 : std::max(0, std::min(int(f.size() - o.at), size));
    if (got)
        std::memcpy(data, f.data() + o.at, size_t(got));
    o.at += size_t(got);
    return got;
}
int FileWrite(int handle, const void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    std::string &f = g_files[o.name];
    if (o.at + size_t(size) > f.size())
        f.resize(o.at + size_t(size));
    std::memcpy(&f[o.at], data, size_t(size));
    o.at += size_t(size);
    return size;
}
bool FileClose(int handle) { return g_openFiles.erase(handle) != 0; }
int __stdcall O_dtaOpen(const char *name, bool) { return FileOpen(name, false); }
int __stdcall O_dtaOpenWrite(const char *name, int) { return FileOpen(name, true); }
int __stdcall O_dtaSeek(int h, int offset, int origin) { return FileSeek(h, offset, origin); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
int __stdcall O_dtaWrite(int h, const void *data, int size) { return FileWrite(h, data, size); }
bool __stdcall O_dtaClose(int h) { return FileClose(h); }

// The chunks: each side's (the original's functions on a block of its layout).
using T_ChunkOpen = bool(TC *)(void *, const char *, bool);
using T_ChunkClose = bool(TC *)(void *);
struct S_SideChunk {
    alignas(16) uint8_t orig[0x500] = {};
    C_chunk port;
    void *Ptr(int side) { return side ? static_cast<void *>(&port) : static_cast<void *>(orig); }
};
bool ChunkOpen(int side, S_SideChunk &c, const char *name, bool write)
{
    return side ? c.port.Open(name, write) : OriginalAt<T_ChunkOpen>(0x00405620)(c.orig, name, write);
}
void ChunkClose(int side, S_SideChunk &c) { side ? c.port.Close() : OriginalAt<T_ChunkClose>(0x00405750)(c.orig); }
int CPos(const void *c) { return At<bool>(c, 0) ? FileSeek(At<int>(c, 4), 0, 1) : -1; }

// ---- the world of a life-cycle scenario ----
struct S_Life {
    bool server;
    uint32_t role;
    bool actorSetFrame, actorSave, actorLoad, mvSave, mvLoad;
    uint32_t frameType;
    int missingBone;                                    // the setup's bone lookup fails on it (-1: none)
    bool script;                                        // the model's frame data names a script
    int observerId, targetId, listenerId;               // what the registrations answer
    int aiMode;                                         // the controller at the start (0: none)
    int humans;                                         // the actors module's actors (the first n humans)
    bool headOverrides;                                 // a head override set at the start
    S_vector4 startPos;
    S_quat startRot;
    uint32_t anims;                                     // the shared animation table's loaded entries (bit per id)
    int nullSlot;                                       // an actors module slot GetHuman answers none for (-1: none)
    int c538;                                           // the tracker's m_538 at the start (SetupFrame clears it)
};
// The animation sets in the table (their Release logged).
uint8_t g_animSet[4][0x10];
void *g_animSetVt[0x10];
uint32_t __stdcall AS_Release(void *self)
{
    Log("%s Release", Name(self).c_str());
    return 0;
}
S_Life g_l;
alignas(16) uint8_t g_checkpoints[0x40], g_script[0x40];
uint8_t g_effects[0x10];
void *g_effectsVt[2][0x40];
uint8_t g_cacheObj[0x10];
// The joint override sets the driver makes (one per side, and its item): Add, Remove, Release logged, the items kept.
alignas(16) uint8_t g_lifeSet[2][0x40], g_lifeItem[2][0x40];
void *g_lifeSetVt[0x20], *g_lifeItemVt[0x20];
void *g_lifeItems[2][4];
// The original's movement's vtable (Reset, SaveGame, LoadGame: the test's) and the port's tracker ReloadModel slot.
void *g_mvVt[0x10];

std::string LifeName(const void *p)
{
    for (int side = 0; side < 2; ++side) {
        if (p == g_lifeSet[side])
            return "set";
        if (p == g_lifeItem[side])
            return "joint";
    }
    if (p == g_checkpoints)
        return "checkpoints";
    if (p == g_script)
        return "script";
    if (p == g_effects)
        return "effects";
    for (int i = 0; i < 4; ++i)
        if (p == g_animSet[i])
            return "anim" + std::to_string(i);
    return "";
}

// ---- the fakes ----
void F_ActorActivate(void *self, int server) { Log("%s actor Activate %d", Name(self).c_str(), server & 0xFF); }
void F_ActorDeactivate(void *self, int server) { Log("%s actor Deactivate %d", Name(self).c_str(), server & 0xFF); }
bool F_ActorSetFrame(void *self, void *frame)
{
    Log("%s actor SetFrame %s -> %d", Name(self).c_str(), Name(frame).c_str(), int(g_l.actorSetFrame));
    return g_l.actorSetFrame;
}
void F_ActorCopyFrom(void *self, const void *source) { Log("%s actor CopyFrom %s", Name(self).c_str(), Name(source).c_str()); }
bool F_ActorSave(void *self) { Log("%s actor SaveGame -> %d", Name(self).c_str(), int(g_l.actorSave)); return g_l.actorSave; }
bool F_ActorLoad(void *self) { Log("%s actor LoadGame -> %d", Name(self).c_str(), int(g_l.actorLoad)); return g_l.actorLoad; }
void TC O_ActorActivate(void *self, int server) { F_ActorActivate(self, server); }
void TC O_ActorDeactivate(void *self, int server) { F_ActorDeactivate(self, server); }
int TC O_ActorSetFrame(void *self, void *frame) { return F_ActorSetFrame(self, frame); }
void TC O_ActorCopyFrom(void *self, const void *source) { F_ActorCopyFrom(self, source); }
int TC O_ActorSave(void *self, void *) { return F_ActorSave(self); }
int TC O_ActorLoad(void *self, void *) { return F_ActorLoad(self); }
// The movement's.
void F_MvBind(void *self, void *model, void *tracker) { Log("%s Bind %s %s", Name(self).c_str(), Name(model).c_str(), Name(tracker).c_str()); }
void F_MvFree(void *self) { Log("%s FreeVolumesAndSounds", Name(self).c_str()); }
void F_MvReset(void *self) { Log("%s Reset", Name(self).c_str()); }
bool F_MvSave(void *self) { Log("%s SaveGame -> %d", Name(self).c_str(), int(g_l.mvSave)); return g_l.mvSave; }
bool F_MvLoad(void *self) { Log("%s LoadGame -> %d", Name(self).c_str(), int(g_l.mvLoad)); return g_l.mvLoad; }
void TC O_MvBind(void *self, void *model, void *tracker) { F_MvBind(self, model, tracker); }
void TC O_MvFree(void *self) { F_MvFree(self); }
void TC O_MvReset(void *self) { F_MvReset(self); }
int TC O_MvSave(void *self, void *) { return F_MvSave(self); }
int TC O_MvLoad(void *self, void *) { return F_MvLoad(self); }
void TC O_TrackerReload(void *self) { Log("%s ReloadModel", Name(self).c_str()); }
void TC O_ActorDestructor(void *self) { Log("%s actor destructor", Name(self).c_str()); }
// The model cache, the effects.
HRESULT F_OpenInto(void *model, const char *name, uint32_t flags)
{
    Log("cache OpenInto %s %s %u", Name(model).c_str(), name, flags);
    return 0;
}
HRESULT TC O_OpenInto(void *, void *model, const char *name, uint32_t flags, void *, void *, int) { return F_OpenInto(model, name, flags); }
void TC V_FadeIn(void *, void *model, int time, int turnOn) { Log("effects FadeInModel %s %d %d", Name(model).c_str(), time, turnOn & 0xFF); }
void *__cdecl O_GetGEffects() { return g_effects; }
// The driver's joint overrides and the model's SetJointOverrides (COM: stdcall).
void *__stdcall D_CreateSet(void *)
{
    Log("driver CreateJointOverrideSet");
    return g_lifeSet[g_side];
}
void *__stdcall D_CreateOverride(void *)
{
    Log("driver CreateJointOverride");
    return g_lifeItem[g_side];
}
void __stdcall LS_Add(void *self, void *item, const char *joint)
{
    Log("%s Add %s %s", Name(self).c_str(), Name(item).c_str(), joint);
    const int n = At<int>(self, 0x10);
    if (n < 4) {
        g_lifeItems[g_side][n] = item;
        Put(self, 0x10, n + 1);
    }
}
void __stdcall LS_Remove(void *self, void *item)
{
    Log("%s Remove %s", Name(self).c_str(), Name(item).c_str());
    int n = At<int>(self, 0x10);
    for (int i = 0; i < n; ++i)
        if (g_lifeItems[g_side][i] == item) {
            for (int k = i; k + 1 < n; ++k)
                g_lifeItems[g_side][k] = g_lifeItems[g_side][k + 1];
            Put(self, 0x10, --n);
            break;
        }
}
uint32_t __stdcall LS_Release(void *self)
{
    Log("%s Release", Name(self).c_str());
    return 0;
}
void __stdcall M_SetJointOverrides(void *f, void *set) { Log("%s SetJointOverrides %s", Name(f).c_str(), Name(set).c_str()); }
// The sight and sound registrations.
int F_AddObserver(void *frame, uint32_t mask)
{
    Log("sight AddObserver %s %x -> %d", Name(frame).c_str(), mask, g_l.observerId);
    return g_l.observerId;
}
int F_AddTarget(void *frame, uint32_t mask)
{
    Log("sight AddTarget %s %x -> %d", Name(frame).c_str(), mask, g_l.targetId);
    return g_l.targetId;
}
void F_SetTargetState(int id, int state) { Log("sight SetTargetState %d %d", id, state); }
int TC O_AddObserver(void *, void *frame, uint32_t mask) { return F_AddObserver(frame, mask); }
int TC O_AddTarget(void *, void *frame, uint32_t mask) { return F_AddTarget(frame, mask); }
void TC O_SetTargetState(void *, int id, int state) { F_SetTargetState(id, state); }
int F_AddListener(void *frame, void *proc)
{
    const bool known = proc == reinterpret_cast<void *>(&C_MeleeTracker::OnSoundHeard) || proc == OriginalAt<void *>(0x005d6000);
    Log("sound AddListener %s %s -> %d", Name(frame).c_str(), known ? "OnSoundHeard" : "?", g_l.listenerId);
    return g_l.listenerId;
}
int TC O_AddListener(void *, void *frame, void *proc) { return F_AddListener(frame, proc); }
// The actors module's GetActor (by net id: the humans' 200 + i).
void *F_GetActor(int id)
{
    void *a = id >= 200 && id < 200 + HUMANS ? Human(g_side, id - 200) : nullptr;
    Log("actors GetActor %d -> %s", id, Name(a).c_str());
    return a;
}
void *TC O_GetActor(void *, int id) { return F_GetActor(id); }

}  // namespace H

// ---- the port's side ----
void C_Actor::Activate(bool server) { H::F_ActorActivate(this, server); }
void C_Actor::Deactivate(bool server) { H::F_ActorDeactivate(this, server); }
bool C_Actor::SetFrame(I3D_frame *frame) { return H::F_ActorSetFrame(this, frame); }
void C_Actor::CopyFrom(const C_Actor *source) { H::F_ActorCopyFrom(this, source); }
bool C_Actor::SaveGame(C_chunk &) { return H::F_ActorSave(this); }
bool C_Actor::LoadGame(C_chunk &) { return H::F_ActorLoad(this); }
void C_MeleeTrackerMovement::Bind(I3D_model *model, C_MeleeTracker *tracker) { H::F_MvBind(this, model, tracker); }
void C_MeleeTrackerMovement::FreeVolumesAndSounds() { H::F_MvFree(this); }
void C_MeleeTrackerMovement::Reset() { H::F_MvReset(this); }
bool C_MeleeTrackerMovement::SaveGame(C_chunk &) { return H::F_MvSave(this); }
bool C_MeleeTrackerMovement::LoadGame(C_chunk &) { return H::F_MvLoad(this); }
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *model, const char *name, uint32_t flags, void *, void *, int)
{
    return H::F_OpenInto(model, name, flags);
}
C_GEffectsModule *GetGEffects() { return reinterpret_cast<C_GEffectsModule *>(H::g_effects); }
int C_SightModule::AddObserver(I3D_frame *frame, uint32_t mask) { return H::F_AddObserver(frame, mask); }
int C_SightModule::AddTarget(I3D_frame *frame, uint32_t mask) { return H::F_AddTarget(frame, mask); }
void C_SightModule::SetTargetState(int id, int state) { H::F_SetTargetState(id, state); }
int C_SoundModule::AddListener(I3D_frame *frame, T_SoundHeardProc proc) { return H::F_AddListener(frame, reinterpret_cast<void *>(proc)); }
C_Actor *C_ActorsModule::GetActor(int id) const { return static_cast<C_Actor *>(H::F_GetActor(id)); }
int ActorToId(const C_Actor *actor) { return actor ? actor->GetNetId() : -1; }
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name, false); }
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpen(name, true); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }

namespace H {

// The controller after an operation: its bytes in the original's layout (both sides), the pointers by name.
std::string AiText(int side)
{
    void *ai = Get<void *>(side, "m_ai");
    if (!ai)
        return "ai none";
    const int mode = At<int>(ai, 0x14);
    const uint32_t size = mode == 1 ? 0x4B0u : mode == 7 ? 0x270u : mode == 5 ? 0xD0u : 0xC0u;
    std::string s = "ai mode " + std::to_string(mode);
    for (uint32_t at = 4; at < size; at += 4) {
        // (The w words the original fills from stack words: the facing's and the move's (0x3C, 0x4C, 0x5C), InitBase's
        // two targets' (0x9C, 0xAC); the port's 0.)
        if (at == 0x3C || at == 0x4C || at == 0x5C || (mode != 1 && mode != 7 && (at == 0x9C || at == 0xAC))) {
            s += " -";
            continue;
        }
        const uint32_t w = At<uint32_t>(ai, at);
        const void *p = reinterpret_cast<const void *>(static_cast<uintptr_t>(w));
        const std::string n = w ? Name(p) : "";
        s += " " + (!n.empty() && n != "?" ? n : Hex(w));
    }
    return s;
}

void RandomLife()
{
    g_l.server = P(0.8);
    g_l.role = Pick({7u, 1u, 2u, 3u});
    g_l.actorSetFrame = P(0.85);
    g_l.actorSave = P(0.9);
    g_l.actorLoad = P(0.9);
    g_l.mvSave = P(0.85);
    g_l.mvLoad = P(0.85);
    g_l.frameType = P(0.85) ? uint32_t(FRAME_MODEL) : uint32_t(FRAME_VISUAL);
    g_l.missingBone = P(0.85) ? -1 : I(0, 23);
    g_l.script = P(0.4);
    g_l.observerId = I(-1, 9);
    g_l.targetId = I(-1, 9);
    g_l.listenerId = I(-1, 9);
    g_l.aiMode = P(0.15) ? 0 : Ip({1, 4, 5, 7});
    g_l.humans = I(0, HUMANS);
    g_l.headOverrides = P(0.7);
    g_l.startPos = RandomPos();
    const S_vector4 q = {F(-1, 1), F(-1, 1), F(-1, 1), F(-1, 1)};
    g_l.startRot.v = S_vector(q.x, q.y, q.z);
    g_l.startRot.s = q.w;
    g_l.nullSlot = P(0.3) ? I(0, HUMANS - 1) : -1;
    g_l.anims = U32() & 0x7FFFFF;
    g_l.c538 = P(0.5) ? 0 : I(1, 1000);
    if (P(0.9))
        g_l.anims |= 2;                                 // (entry 1: the table loaded; StartMission's LoadAnimTable not run)
}

// The bones the setup looks for: every one there but the scenario's missing one.
I3D_frame *LifeFindChild(I3D_frame *f, const char *name)
{
    if (f != Model())
        return nullptr;
    static const char letters[] = "bcdefghijklmnopqrstuvwxy";
    for (int i = 0; letters[i]; ++i)
        if (name[0] == letters[i] && name[1] == 0)
            return i == g_l.missingBone ? nullptr : name[0] == 'f' ? FrameAt(1) : FrameAt(ZONE_FRAME + i % 7);
    return nullptr;
}

void SetLife(int side)
{
    SetWorld(side);
    // (The real controllers replace the test's.)
    Set<void *>(side, "m_ai", nullptr);
    Set(side, "m_startPos", g_l.startPos);
    Set(side, "m_startRot", g_l.startRot);
    Set(side, "m_538", g_l.c538);
    Set(side, "m_headOverrides", g_l.headOverrides ? static_cast<void *>(g_lifeSet[side]) : nullptr);
    std::memset(g_lifeSet[side], 0, sizeof g_lifeSet[side]);
    Put(g_lifeSet[side], 0, static_cast<void *>(g_lifeSetVt));
    Put(g_lifeSet[side], 8, static_cast<void *>(g_lifeItems[side]));
    if (g_l.headOverrides) {
        g_lifeItems[side][0] = g_lifeItem[side];
        Put(g_lifeSet[side], 0x10, 1);
    }
    Put(g_lifeItem[side], 0, static_cast<void *>(g_overrideItemVt));
    // The original's movement: the test's vtable (Reset, SaveGame, LoadGame).
    if (!side)
        Put(g_tracker[0] + 0x90, 0, static_cast<void *>(g_mvVt));
    for (int i = 0; i < HUMANS; ++i)
        Put(g_human[side][i], side ? uint32_t(offsetof(C_Human, m_netId)) : 0x10u, 200 + i);
    Model()->m_type = static_cast<E_I3D_FRAME_TYPE>(g_l.frameType);
    g_modelData[side].script = g_l.script ? reinterpret_cast<C_Script *>(g_script) : nullptr;
    // The actors module's actors (the first humans): the original's vector at 0x0089b4c0, the port's.
    static void *actors[2][HUMANS];
    for (int i = 0; i < g_l.humans; ++i)
        actors[side][i] = Human(side, i);
    if (side) {
        auto &v = g_actorsModule.m_actors;
        uint8_t *vb = reinterpret_cast<uint8_t *>(&v);
        Put(vb, 0, static_cast<void *>(actors[1]));
        Put(vb, 4, static_cast<void *>(actors[1] + g_l.humans));
        Put(vb, 8, static_cast<void *>(actors[1] + g_l.humans));
    } else {
        *OriginalAt<void **>(0x0089b4c0) = g_l.humans ? actors[0] : nullptr;
        *OriginalAt<void **>(0x0089b4c4) = actors[0] + g_l.humans;
    }
    g_gameRole = g_l.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_l.role;
    // The shared animation table (StartMission loads it when entry 1 is empty; StopMission releases it).
    for (int i = 0; i < 0x17; ++i) {
        const S_MeleeTrackerAnim a = {(g_l.anims >> i) & 1 ? reinterpret_cast<I3D_animation_set *>(g_animSet[i % 4]) : nullptr, i * 10,
                                      0.5f * float(i)};
        g_meleeTrackerAnims[i] = a;
        std::memcpy(OriginalAt<uint8_t *>(0x008ac5c0) + i * sizeof a, &a, sizeof a);
    }
    g_files.clear();
    g_openFiles.clear();
    g_nextFile = 1;
    static const char *const fileName = "Models\\dog.i3d";   // (the model's file, ReloadModel's)
    std::memcpy(g_arena.frames[0] + 0x134, &fileName, 4);
}

}  // namespace H

namespace H {

uint8_t g_effectsOrig[0x10];
void *__cdecl O_GetGEffectsOrig() { return g_effectsOrig; }
std::string g_lifeSave[2];
enum { L_CONTROLLER, L_SAVE, L_LOAD_OWN, L_WRITE_PROPS, L_READ_PROPS, L_SET_FRAME, L_COPY_FROM, L_START, L_STOP, L_RESTART,
       L_RELOAD, L_SETUP, L_OPS, L_DESTRUCT = L_OPS };
struct S_LOp {
    int kind, a, b;
    std::string props;                                  // ReadProperties' file
    int c = 0;                                          // the load's cut (odd: at byte c / 2 of the leaves)
};
S_LOp RandomLifeOp()
{
    S_LOp op;
    op.kind = I(0, L_OPS - 1);
    op.a = I(0, 7);
    op.b = P(0.3) ? Ip({1, 4, 5, 7, 0, 3, 10, 11, 12, 13, 14}) : I(0, 40);
    op.c = int(U32() & 0xFFFF);
    if (op.kind == L_READ_PROPS) {
        const uint32_t w[5] = {1, U32() % 400, U32() % 100, U32() % 50, U32() % 120};
        op.props.assign(reinterpret_cast<const char *>(w), sizeof w);
        if (P(0.2))
            op.props.resize(size_t(I(0, 19)));
    }
    return op;
}
std::string FileHex(const std::string &f)
{
    std::string hex;
    char t[4];
    for (unsigned char ch : f) {
        std::snprintf(t, sizeof t, "%02x", ch);
        hex += t;
    }
    return hex;
}
using T_LV = void(TC *)(void *);
using T_LVI = void(TC *)(void *, int);
using T_LBP = bool(TC *)(void *, void *);
void RunLife(int side, const S_LOp &op)
{
    C_MeleeTracker &m = Mt();
    void *x = g_tracker[side];
    switch (op.kind) {
    case L_CONTROLLER: {
        static const int modes[] = {1, 4, 5, 7, 4, 5, 4, 5};
        const int mode = modes[op.a & 7];
        side ? m.SetController(mode) : OriginalAt<T_LVI>(0x005d5890)(x, mode);
        break;
    }
    case L_SAVE: {
        if (!Get<void *>(side, "m_ai") && g_l.mvSave) {
            Log("skipped: no controller");   // (both save its mode unchecked)
            break;
        }
        S_SideChunk c;
        ChunkOpen(side, c, "s.sav", true);
        const bool r = side ? m.C_MeleeTracker::SaveGame(c.port) : OriginalAt<T_LBP>(0x005d3310)(x, c.orig);
        Log("depth %d", At<int>(c.Ptr(side), 0x410));   // (the chunks it left open: Close would end them)
        ChunkClose(side, c);
        // (The sightings are saved raw: their source pointers, each side's humans, by name.)
        std::string f = g_files["s.sav"];
        for (int i = 0; i < HUMANS; ++i) {
            void *h = Human(side, i);
            const std::string pat(reinterpret_cast<const char *>(&h), 4), mark = {char(0xEE), char(0xEE), char(0xEE), char(i)};
            for (size_t at = f.find(pat); at != std::string::npos; at = f.find(pat, at + 4))
                f.replace(at, 4, mark);
        }
        // (Pushed whole: Log's buffer would cut the file short.)
        g_log.push_back("result " + std::to_string(int(r)) + " file " + FileHex(f));
        g_lifeSave[side] = g_files["s.sav"];
        break;
    }
    case L_LOAD_OWN: {
        if (g_lifeSave[side].empty())
            break;
        std::string f = g_lifeSave[side];
        // (Now and then cut short inside the tracker's own leaves (chunk 0x2A9 before its sightings): cut in the
        // controller's, the mode could read 0, for which the original calls through a null controller; cut in the
        // sightings or the seen list, the original reads an id leaf's stack word.)
        if (op.b < 10 && f.size() >= 12) {
            size_t at = 12, cut = 12;
            while (at + 6 <= f.size()) {
                uint16_t id = 0;
                uint32_t size = 0;
                std::memcpy(&id, &f[at], 2);
                std::memcpy(&size, &f[at + 2], 4);
                if (id == 0x13 || size < 6)
                    break;
                at += size;
                cut = std::min(at, f.size());
            }
            // (At one of ten steps, at any byte (a leaf read short), or right after a leaf's header (its read gets
            // nothing: the local's value before stays).)
            std::vector<size_t> dataStarts;
            for (size_t at = 12; at + 6 <= cut;) {
                uint32_t size = 0;
                std::memcpy(&size, &f[at + 2], 4);
                dataStarts.push_back(at + 6);
                at += size;
            }
            if (op.b && (op.c & 3) == 3 && !dataStarts.empty())
                f.resize(dataStarts[size_t(op.c >> 2) % dataStarts.size()]);
            else if (op.b && (op.c & 1))
                f.resize(12 + size_t(op.c >> 2) % (cut - 12 + 1));
            else
                f.resize(cut - std::min(cut - 12, size_t(op.b) * ((cut - 12) / 10)));
        }
        // (Or a wrong top chunk, a wrong state chunk, a leaf 7 (never saved) in place of 8, an empty file.)
        if (op.b == 10 && f.size() >= 2)
            f[0] = char(0x86);
        else if (op.b == 11 && f.size() >= 8)
            f[6] = char(0xAA);
        else if (op.b == 12) {
            for (size_t at = 12; at + 6 <= f.size();) {
                uint16_t id = 0;
                uint32_t size = 0;
                std::memcpy(&id, &f[at], 2);
                std::memcpy(&size, &f[at + 2], 4);
                if (id == 8) {
                    f[at] = 7;
                    break;
                }
                if (id == 0x13 || size < 6)
                    break;
                at += size;
            }
        } else if (op.b == 13)
            f.clear();
        else if (op.b == 14 && f.size() >= 6)
            f.resize(6);                                // (the top chunk's header alone)
        g_files["l.sav"] = f;
        S_SideChunk c;
        ChunkOpen(side, c, "l.sav", false);
        ZeroStack();
        const bool r = side ? m.C_MeleeTracker::LoadGame(c.port) : OriginalAt<T_LBP>(0x005d3a50)(x, c.orig);
        // (The depth too: the chunks it left open.)
        Log("result %d left at %d depth %d", int(r), CPos(c.Ptr(side)), At<int>(c.Ptr(side), 0x410));
        ChunkClose(side, c);
        break;
    }
    case L_WRITE_PROPS: {
        S_SideChunk c;
        ChunkOpen(side, c, "p.sav", true);
        const bool r = side ? m.C_MeleeTracker::WriteProperties(c.port) : OriginalAt<T_LBP>(0x005d65d0)(x, c.orig);
        ChunkClose(side, c);
        g_log.push_back("result " + std::to_string(int(r)) + " file " + FileHex(g_files["p.sav"]));
        break;
    }
    case L_READ_PROPS: {
        g_files["r.sav"] = op.props;
        S_SideChunk c;
        ChunkOpen(side, c, "r.sav", false);
        const bool r = side ? m.C_MeleeTracker::ReadProperties(c.port) : OriginalAt<T_LBP>(0x005d6630)(x, c.orig);
        Log("result %d left at %d depth %d", int(r), CPos(c.Ptr(side)), At<int>(c.Ptr(side), 0x410));
        ChunkClose(side, c);
        break;
    }
    case L_SET_FRAME: {
        I3D_frame *frame = (op.a & 1) ? FrameAt(2) : Model();
        Log("result %d", int(side ? m.C_MeleeTracker::SetFrame(frame) : OriginalAt<T_LBP>(0x005d4d70)(x, frame)));
        break;
    }
    case L_COPY_FROM: {
        void *source = Human(side, op.a & 3);
        side ? m.C_MeleeTracker::CopyFrom(static_cast<C_Actor *>(source)) : OriginalAt<void(TC *)(void *, void *)>(0x005d4c80)(x, source);
        break;
    }
    case L_START:
        if ((op.a & 1) && !(side ? g_meleeTrackerAnims[1].anim != nullptr : *OriginalAt<void **>(0x008ac5cc) != nullptr)) {
            Log("skipped: the animation table would load");   // (LoadAnimTable: not here)
            break;
        }
        ZeroStack();
        side ? m.C_MeleeTracker::StartMission(op.a & 1) : OriginalAt<T_LVI>(0x005d5290)(x, op.a & 1);
        break;
    case L_STOP:
        if ((op.a & 1) && !Get<void *>(side, "m_ai")) {
            Log("skipped: no controller");   // (the original detaches it unchecked: the port's fix)
            break;
        }
        side ? m.C_MeleeTracker::StopMission(op.a & 1) : OriginalAt<T_LVI>(0x005d5390)(x, op.a & 1);
        break;
    case L_RESTART:
        if (!Get<void *>(side, "m_ai")) {
            Log("skipped: no controller");
            break;
        }
        ZeroStack();
        side ? m.C_MeleeTracker::RestartMission(op.a & 1) : OriginalAt<T_LVI>(0x005d45f0)(x, op.a & 1);
        break;
    case L_RELOAD:
        side ? m.C_MeleeTracker::ReloadModel() : OriginalAt<T_LV>(0x005d5560)(x);
        break;
    case L_SETUP:
        ZeroStack();
        side ? m.SetupFrame() : OriginalAt<T_LV>(0x005d4db0)(x);
        break;
    case L_DESTRUCT:
        // (The last operation: the destructor frees the three lists' arrays.)
        side ? m.C_MeleeTracker::~C_MeleeTracker() : OriginalAt<T_LV>(0x005d4cf0)(x);
        break;
    default:
        break;
    }
}
std::string LifeSnap(int side)
{
    std::string s = Snap(side) + " | " + AiText(side);
    const I3D_frame *f = Model();
    s += " | model rot " + VecText(f->m_rot.v) + " " + FloatText(f->m_rot.s) + " flags " + Hex(f->m_frameFlags);
    s += " | overrides " + std::to_string(At<int>(g_lifeSet[side], 0x10));
    s += " | anims";
    for (int i = 0; i < 0x17; ++i) {
        S_MeleeTrackerAnim a;
        if (side)
            a = g_meleeTrackerAnims[i];
        else
            std::memcpy(&a, OriginalAt<uint8_t *>(0x008ac5c0) + i * 12, sizeof a);
        s += " " + Name(a.anim) + "/" + std::to_string(a.blendTime) + "/" + FloatText(a.speed);
    }
    return s;
}
void TestLife()
{
    g_lifeMasks = true;
    RandomWorld();
    RandomLife();
    g_nullSlot = g_l.nullSlot;
    const S_World start = g_w;
    const S_Life life = g_l;
    std::vector<S_LOp> ops;
    for (int n = I(1, 8); n > 0; --n)
        ops.push_back(RandomLifeOp());
    if (P(0.15))
        ops.push_back(S_LOp{L_DESTRUCT, 0, 0, std::string()});
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_w = start;
        g_l = life;
        g_log.clear();
        g_lifeSave[side].clear();
        SetLife(side);
        if (g_l.aiMode)
            side ? Mt().SetController(g_l.aiMode) : OriginalAt<T_LVI>(0x005d5890)(g_tracker[0], g_l.aiMode);
        g_log.push_back(LifeSnap(side));
        for (const S_LOp &op : ops) {
            Log("-- %d %d %d", op.kind, op.a, op.b);
            RunLife(side, op);
            g_log.push_back(LifeSnap(side));
        }
        g_runs[side].log.swap(g_log);
    }
    static const int dump = std::getenv("HD2_MT_LIFE_DUMP") ? std::atoi(std::getenv("HD2_MT_LIFE_DUMP")) : -1;
    if (g_round == dump)
        for (size_t i = 0; i < std::max(g_runs[0].log.size(), g_runs[1].log.size()); ++i)
            std::printf("%s\n  %s\n", i < g_runs[0].log.size() ? g_runs[0].log[i].c_str() : "-",
                        i < g_runs[1].log.size() ? g_runs[1].log[i].c_str() : "-");
    CompareLogs(g_runs[0].log, g_runs[1].log);
    ShowLogs(g_runs[0].log, g_runs[1].log);
    g_lifeMasks = false;
    g_nullSlot = -1;
}

void *__cdecl O_LifeGetGame() { return g_origGame; }
void SetUpLife()
{
    g_findChild = LifeFindChild;
    for (void *&s : g_lifeSetVt)
        s = reinterpret_cast<void *>(OV_Other);
    g_lifeSetVt[0] = reinterpret_cast<void *>(LS_Release);
    g_lifeSetVt[1] = reinterpret_cast<void *>(LS_Add);
    g_lifeSetVt[2] = reinterpret_cast<void *>(LS_Remove);
    for (void *&s : g_animSetVt)
        s = reinterpret_cast<void *>(OV_Other);
    g_animSetVt[0] = reinterpret_cast<void *>(AS_Release);
    for (int i = 0; i < 4; ++i)
        Put(g_animSet[i], 0, static_cast<void *>(g_animSetVt));
    for (void *&s : g_mvVt)
        s = reinterpret_cast<void *>(V_Other);
    g_mvVt[0] = reinterpret_cast<void *>(O_MvReset);
    g_mvVt[1] = reinterpret_cast<void *>(O_MvSave);
    g_mvVt[2] = reinterpret_cast<void *>(O_MvLoad);
    g_trackerVt[0][0xE0 / 4] = reinterpret_cast<void *>(O_TrackerReload);
    g_trackerVt[1][Slot(&C_MeleeTracker::ReloadModel) / 4] = reinterpret_cast<void *>(O_TrackerReload);
    for (int side = 0; side < 2; ++side)
        for (void *&s : g_effectsVt[side])
            s = reinterpret_cast<void *>(V_Other);
    g_effectsVt[0][0x6C / 4] = reinterpret_cast<void *>(V_FadeIn);
    g_effectsVt[1][Slot(&C_GEffectsModule::FadeInModel) / 4] = reinterpret_cast<void *>(V_FadeIn);
    Put(g_effectsOrig, 0, static_cast<void *>(g_effectsVt[0]));
    Put(g_effects, 0, static_cast<void *>(g_effectsVt[1]));
    g_driverVt[0x74 / 4] = reinterpret_cast<void *>(D_CreateOverride);
    g_driverVt[0x78 / 4] = reinterpret_cast<void *>(D_CreateSet);
    g_frameVt[0xB4 / 4] = reinterpret_cast<void *>(M_SetJointOverrides);
    g_soundVt[0x44 / 4] = reinterpret_cast<void *>(O_AddListener);
    Put(g_origGame, 0x54, static_cast<void *>(g_checkpoints));
    reinterpret_cast<C_game *>(g_portGame)->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_checkpoints);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x00610240, reinterpret_cast<const void *>(O_ActorActivate)},
        {0x00610170, reinterpret_cast<const void *>(O_ActorDestructor)},
        {0x00610510, reinterpret_cast<const void *>(O_ActorDeactivate)},
        {0x00610200, reinterpret_cast<const void *>(O_ActorSetFrame)},
        {0x006101f0, reinterpret_cast<const void *>(O_ActorCopyFrom)},
        {0x00610780, reinterpret_cast<const void *>(O_ActorSave)},
        {0x00610a80, reinterpret_cast<const void *>(O_ActorLoad)},
        {0x005c8370, reinterpret_cast<const void *>(O_MvBind)},
        {0x005c83a0, reinterpret_cast<const void *>(O_MvFree)},
        {0x004290a0, reinterpret_cast<const void *>(O_OpenInto)},
        {0x00486510, reinterpret_cast<const void *>(O_GetGEffectsOrig)},
        {0x0046f350, reinterpret_cast<const void *>(O_AddObserver)},
        {0x0046f9d0, reinterpret_cast<const void *>(O_AddTarget)},
        {0x0046f700, reinterpret_cast<const void *>(O_SetTargetState)},
        {0x00475dd0, reinterpret_cast<const void *>(O_GetActor)},
        {0x006987d0, reinterpret_cast<const void *>(O_LifeGetGame)},
        {0x007e53a4, reinterpret_cast<const void *>(O_dtaOpen)},
        {0x007e53aa, reinterpret_cast<const void *>(O_dtaOpenWrite)},
        {0x007e53b0, reinterpret_cast<const void *>(O_dtaClose)},
        {0x007e5392, reinterpret_cast<const void *>(O_dtaWrite)},
        {0x007e5398, reinterpret_cast<const void *>(O_dtaSeek)},
        {0x007e539e, reinterpret_cast<const void *>(O_dtaRead)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H
