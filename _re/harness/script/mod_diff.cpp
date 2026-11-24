// Script parts 6 and 7: the script module (script_module_save.cpp, script_module.cpp; C_ScriptModule, the original's
// 0x00868b18, vtable 0x0080f750) against the original executable's. cmd_diff.cpp's world, fakes and call logger are
// included (CMD_DIFF_NO_MAIN); the module's own calls are logged through mod_calls.s / mod_calls.inc (gen_cmd_calls.py
// mod). Each case builds the same world on both sides, gives each side's module the same state (its scripts compiled
// on world frames, pause sounds, rotations, remembered frames, sound frames, saved values, cutscene flags; the
// original's MSVC6 vectors in blocks of its operator new, its S_RememberedFrame / S_FrameRotation blocks of its
// _aligned_malloc, redirected to the CRT's), runs one operation on each and compares: the answer, the module's state,
// the frames, the chunk file written, and every call each side made.
// Part 6: SaveGame, LoadGame (the original's last save, cut short or not, or a file made for the world: chunks in any
// order, unknown, short), WriteState, ReadState, OnMessage (every client message type).
// Part 7: every other function of the module (the frames it remembers, registers and turns, Update, Tick, the script
// list, the mission's start, stop and restart, AssignScript, the pause sounds, the cutscenes, the saved values, the net
// messages), with its other state (started, the cutscene's camera, animated models, duplicates, the script list,
// pending sounds, the frames' owner models, the screen and the scene's viewport).
// MinGW; build_mod.bat; runs mod_diff.exe ("mod_diff: N checks, 0 failures"). Env: HD2_MOD_CASES (default 40,000;
// HD2_MOD_QUICK 8,000), HD2_MOD_OP (one operation's name), HD2_MOD_SEED, HD2_MOD_SHOW, HD2_MOD_DUMP.
#define HARNESS_NAME "mod_diff"
#define CMD_CALLS_INC "mod_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <malloc.h>

#include "common/chunk.h"

// ---- LS3DF.dll's math the module uses (the original's calls go there too) ----
S_matrix __stdcall RotateXMatrix(float angle)
{
    using T = void *(__stdcall *)(S_matrix *, float);
    static const T f = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?RotateXMatrix@@YG?AUS_matrix@@M@Z")));
    S_matrix r;
    f(&r, angle);
    return r;
}
S_matrix __stdcall RotateYMatrix(float angle)
{
    using T = void *(__stdcall *)(S_matrix *, float);
    static const T f = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?RotateYMatrix@@YG?AUS_matrix@@M@Z")));
    S_matrix r;
    f(&r, angle);
    return r;
}
S_matrix __stdcall RotateZMatrix(float angle)
{
    using T = void *(__stdcall *)(S_matrix *, float);
    static const T f = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?RotateZMatrix@@YG?AUS_matrix@@M@Z")));
    S_matrix r;
    f(&r, angle);
    return r;
}
S_matrix &__stdcall S_matrix::operator*=(const S_matrix &b)
{
    using T = S_matrix *(__stdcall *)(S_matrix *, const S_matrix &);
    static const T f = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("??XS_matrix@@QAGAAU0@ABU0@@Z")));
    f(this, b);
    return *this;
}
S_vector __stdcall S_matrix::GetScale() const
{
    using T = void *(__stdcall *)(const S_matrix *, float *);
    static const T f = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?GetScale@S_matrix@@QBG?AUS_vector@@XZ")));
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}

// The port's side of the written files (sc_shared.inc's in memory, as vm_diff's).
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }

namespace H {

// ---- the original's module ----
constexpr uint32_t O_MODULE = 0x00868b18, O_MODULE_CTOR = 0x004245c0, O_PENDING_SOUNDS = 0x008693d0;
constexpr uint32_t O_MOD_SAVE = 0x00424970, O_MOD_LOAD = 0x004257b0, O_WRITE_STATE = 0x0042aff0,
                   O_READ_STATE = 0x0042b330, O_ON_MESSAGE = 0x00424590;
// Part 7's (all __thiscall but the net messages' __fastcall ones).
constexpr uint32_t O_REMEMBER = 0x00423670, O_RESTORE_FRAME = 0x00423b40, O_REGISTER_SOUND = 0x00423980,
                   O_REGISTER_ANIMATED = 0x00424260, O_UNREGISTER_ANIMATED = 0x00424310,
                   O_REGISTER_DUPLICATE = 0x00424420, O_UNREGISTER_DUPLICATE = 0x00424490, O_ROTATE = 0x004274a0,
                   O_UPDATE = 0x00428b30, O_TICK = 0x00428490, O_LOAD_SCRIPT_LIST = 0x00428600, O_START = 0x004270d0,
                   O_STOP = 0x00427b80, O_RESTART = 0x00424900, O_FREE = 0x004285d0, O_ASSIGN = 0x00427730,
                   O_ALARM = 0x00428b00, O_ADD_PAUSE = 0x00428d10, O_SET_PAUSED = 0x00428f00,
                   O_RUN_CUTSCENE = 0x00423c70, O_END_CUTSCENE = 0x00423eb0, O_WRITE_VALUES = 0x00427010,
                   O_READ_VALUES = 0x00427050, O_BEGIN_MESSAGE = 0x00424540, O_SEND_MESSAGE = 0x00424580;
// g_scriptModuleRestarting; C_str's operator=(const char *) and destructor; the copy protection's call (RunCutscene:
// a pointer in the .dogui section).
constexpr uint32_t O_RESTARTING = 0x008693e0, O_STR_ASSIGN = 0x00405180, O_STR_DTOR = 0x00405160,
                   O_PROTECTION_CALL = 0x00a59000;
constexpr uint32_t O_CHUNK_OPEN = 0x00405620, O_CHUNK_CLOSE = 0x00405750, O_CHUNK_BEGIN = 0x00405820,
                   O_CHUNK_ASCEND = 0x00405890, O_CHUNK_DESCEND = 0x00415c90;
// The module's fields (the original's offsets).
constexpr uint32_t M_INPUT_DELAY = 0x0C, M_STARTED = 0x10, M_MAP_OPEN = 0x11, M_SCRIPTS = 0x14, M_VALUES = 0x24,
                   M_VALUES_AT_START = 0x424, M_CUTSCENE_RUNNING = 0x824, M_FAKE = 0x825, M_CAMERA = 0x828,
                   M_ASPECT = 0x82C, M_FOV = 0x830, M_CUTSCENE = 0x834, M_ASSIGNMENTS = 0x838, M_ROTATIONS = 0x848,
                   M_PAUSE_SOUNDS = 0x858, M_DUPLICATES = 0x868, M_ANIMATED = 0x878, M_ANIMATED_IN_CUTSCENE = 0x888,
                   M_REMEMBERED = 0x898, M_SOUND_FRAMES = 0x8A8;
uint8_t *OMod() { return OriginalAt<uint8_t *>(O_MODULE); }
static_assert(offsetof(S_RememberedFrame, frame) == 0x34 && offsetof(S_RememberedFrame, what) == 0x3C &&
                  offsetof(S_RememberedFrame, on) == 0x40 && sizeof(S_RememberedFrame) <= 0x50,
              "the original's S_RememberedFrame");
static_assert(offsetof(S_FrameRotation, frame) == 0x10 && sizeof(S_FrameRotation) == 0x20, "the original's S_FrameRotation");

void *__cdecl O_AlignedMalloc(size_t size, size_t align) { return _aligned_malloc(size, align); }
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }

// An MSVC6 vector of words (or bytes) at the original's `off`: a block of its operator new with room to grow.
void SetOrigVec(uint8_t *base, uint32_t off, const std::vector<uint32_t> &items, uint32_t elemSize = 4)
{
    uint8_t *buf = static_cast<uint8_t *>(O_New(elemSize * (items.size() + 2) + 4));
    for (size_t i = 0; i < items.size(); ++i)
        std::memcpy(buf + elemSize * i, &items[i], elemSize);
    *reinterpret_cast<uint8_t **>(base + off + 4) = buf;
    *reinterpret_cast<uint8_t **>(base + off + 8) = buf + elemSize * items.size();
    *reinterpret_cast<uint8_t **>(base + off + 12) = buf + elemSize * (items.size() + 2);
}
void FreeOrigVec(uint8_t *base, uint32_t off)
{
    O_Delete(*reinterpret_cast<void **>(base + off + 4));
    std::memset(base + off + 4, 0, 12);
}

// ---- one case ----
enum E_Op {
    OP_SAVE, OP_LOAD, OP_WRITE_STATE, OP_READ_STATE, OP_MESSAGE,
    // Part 7 (script_module.cpp).
    OP_REMEMBER, OP_RESTORE_FRAME, OP_REGISTER_SOUND, OP_REGISTER_ANIMATED, OP_UNREGISTER_ANIMATED,
    OP_REGISTER_DUPLICATE, OP_UNREGISTER_DUPLICATE, OP_ROTATE, OP_UPDATE, OP_TICK, OP_LOAD_SCRIPT_LIST, OP_START,
    OP_STOP, OP_RESTART, OP_FREE, OP_ASSIGN, OP_ALARM, OP_ADD_PAUSE, OP_SET_PAUSED, OP_RUN_CUTSCENE, OP_END_CUTSCENE,
    OP_WRITE_VALUES, OP_READ_VALUES, OP_BEGIN_MESSAGE, OP_SEND_MESSAGE, OP_COUNT
};
const char *const OP_NAMES[] = {"SaveGame", "LoadGame", "WriteState", "ReadState", "OnMessage", "RememberFrame",
                                "RestoreFrame", "RegisterSoundFrame", "RegisterAnimatedModel", "UnregisterAnimatedModel",
                                "RegisterDuplicate", "UnregisterDuplicate", "RotateFrame", "Update", "Tick",
                                "LoadScriptList", "StartMission", "StopMission", "RestartMission", "FreeMissionData",
                                "AssignScript", "BroadcastGlobalAlarm", "AddPauseSound", "SetPauseSoundsPaused",
                                "RunCutscene", "EndCutscene", "WriteSaveGameValues", "ReadSaveGameValues",
                                "BeginScriptNetMessage", "SendScriptNetMessage"};
static_assert(sizeof OP_NAMES / sizeof *OP_NAMES == OP_COUNT, "a name for each operation");
// (The case's weights: part 6's, then part 7's.)
const int OP_WEIGHTS[] = {30, 35, 10, 10, 15, 8, 8, 4, 6, 6, 5, 5, 8, 10, 12, 8, 12, 25, 10, 6, 15, 4, 5, 4, 10, 15, 4, 5, 3, 3};
static_assert(sizeof OP_WEIGHTS / sizeof *OP_WEIGHTS == OP_COUNT, "a weight for each operation");
struct S_Remembered {
    int frame;
    bool parent;                                        // (the frame's own parent, referenced; else none)
    uint32_t what;
    bool on;
    float pos[4], rot[4], velocity[4], volume;
    bool gone;                                          // (only the list holds it: its reference count 1)
    bool lost;                                          // (its name no longer finds it)
};
struct S_Rotation {
    int frame, axis;
    float angle, speed;
};
struct S_ModScript {
    int frame;                                          // -1: a null entry
    int ip, delay;
    bool running;
    bool handlers = false;                              // (mod_x.scr: OnExit, OnGlobalAlarm, the cutscenes' handlers)
    // Part 7: its camera hid the HUD, the HUD panel (EndCutscene shows them); in its cutscene handler (Tick).
    bool hud = false, panel = false, inHandler = false;
};
struct S_ModCase {
    int op;
    uint32_t role;
    bool nullMessage = false;                           // (WriteState, ReadState, OnMessage without a message)
    std::vector<int> pause, soundFrames;
    std::vector<S_Rotation> rotations;
    std::vector<S_Remembered> remembered;
    std::vector<S_ModScript> scripts;
    std::vector<std::pair<int, int>> values;            // (index, value)
    bool fake = false, cutscene = false;
    int shadow = -1;                                    // the humans' shadow frame (a world frame; -1 none)
    int modelActor[2] = {-1, -1};                       // an actor of this kind put on model1 / model2 (-1: the world's)
    std::vector<std::string> groups;                    // the lightmap groups' names
    std::vector<std::string> modelFiles;                // model1's and model2's file names
    std::string file;                                   // LoadGame's
    std::vector<uint32_t> bits;                         // ReadBits' answers, in order (then the default ones)
    std::vector<std::string> texts;                     // ReadString's
    // Part 7: the module's other state.
    bool started = false, mapOpen = false;
    int inputDelay = 0, camera = -1, cutsceneNumber = 0;    // (camera: the cutscene's, a world frame; -1 none)
    float aspect = 0.0f, fov = 0.0f;
    std::vector<std::pair<int, bool>> animated;         // (a model, started in a cutscene)
    std::vector<int> duplicates;
    std::vector<std::pair<std::string, std::string>> assignments;
    std::vector<std::pair<int, int>> valuesAtStart;
    std::vector<std::pair<int, int>> pending;           // (a sound frame or -1: a null entry, its start)
    std::vector<int> owners;                            // each world frame's owner model (a frame index; -1 none)
    int screen[2] = {800, 600};                         // IGraph's ScrnX, ScrnY
    int viewport[4] = {0, 0, 800, 600};                 // the scene's (+0x244)
    float gameFov = 1.0f;                               // the game's cutscene FOV (+0xBC)
    bool inputEnabled = true;                           // the game's (+0x6E)
    bool controllersScripted = false;                   // the humans' scripted controllers hold the module's first script
    // The operation's arguments.
    int frame = 0, axis = 0, angle = 0, speed = 0, time = 0, number = 0;
    uint32_t what = 0;
    bool flag = false, nullName = false;
    std::string name;
    int listKind = 0;                                   // LoadScriptList's file: 0 made, 1 the game's, 2 none
    std::string list, valuesFile;
};

// Names a message or a load names (the world's frames, some not).
const char *const MOD_NAMES[] = {"door", "model1", "snd_a", "cam1", "dummy1", "model2", "snd_b", "child1", "nothing", "",
                                 "light1", "visual1", "hlava", "group1", "GROUP2", "weapon"};
constexpr int MOD_NAME_COUNT = int(sizeof MOD_NAMES / sizeof *MOD_NAMES);
const int MESSAGE_TYPES[] = {SCRIPT_MSG_FRAME_SET_ON, SCRIPT_MSG_MORPH_SPEECH, SCRIPT_MSG_SET_LIGHTMAP,
                             SCRIPT_MSG_CREATE_PARTICLE, SCRIPT_MSG_SET_GROUP_LIGHTMAP, SCRIPT_MSG_WEATHER_ON,
                             SCRIPT_MSG_WEATHER_COUNT, SCRIPT_MSG_WEATHER_SPEED, SCRIPT_MSG_THUNDERSTORM,
                             SCRIPT_MSG_WEATHER_SIZE, SCRIPT_MSG_WEATHER_COLOR_IN, SCRIPT_MSG_WEATHER_COLOR_OUT,
                             SCRIPT_MSG_WEATHER_MODE, SCRIPT_MSG_WEATHER_SPACE, SCRIPT_MSG_WEATHER_DIR,
                             SCRIPT_MSG_SUBTITLES_ON, SCRIPT_MSG_SUBTITLES_TEXT};
constexpr int MESSAGE_TYPE_COUNT = int(sizeof MESSAGE_TYPES / sizeof *MESSAGE_TYPES);

std::string g_lastSave;                                 // the original's last save (a later load reads it)
int g_modCases, g_opCases[OP_COUNT], g_loadsOfSaves, g_madeFiles;

// ---- the files LoadGame reads ----
std::string Word(uint32_t v, int n = 4) { return std::string(reinterpret_cast<const char *>(&v), size_t(n)); }
std::string Chunk(uint16_t id, const std::string &data) { return Word(id, 2) + Word(uint32_t(data.size() + 6)) + data; }
std::string Sized(const std::string &s) { return Word(uint32_t(s.size() + 1), 2) + s + std::string(1, '\0'); }
std::string FloatWord(float f)
{
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return Word(w);
}
const char *RandomName() { return MOD_NAMES[I(0, MOD_NAME_COUNT - 1)]; }
// The chunks inside `data` (from `at` to `end`): (id, the whole chunk).
std::vector<std::pair<uint16_t, std::string>> SubChunks(const std::string &data, size_t at, size_t end)
{
    std::vector<std::pair<uint16_t, std::string>> out;
    while (at + 6 <= end) {
        uint16_t id;
        uint32_t size;
        std::memcpy(&id, data.data() + at, 2);
        std::memcpy(&size, data.data() + at + 2, 4);
        if (size < 6 || at + size > end)
            break;
        out.push_back({id, data.substr(at, size)});
        at += size;
    }
    return out;
}
// The script chunks of the original's saves (a made file takes one, changed or not).
std::vector<std::string> g_savedScripts;
void CollectScripts(const std::string &save)
{
    for (const auto &c : SubChunks(save, 6, save.size()))
        if (c.first == 0x44C) {
            g_savedScripts.push_back(c.second);
            if (g_savedScripts.size() > 32)
                g_savedScripts.erase(g_savedScripts.begin());
        }
}
std::string MakeScriptChunk()
{
    const std::string &s = g_savedScripts[size_t(I(0, int(g_savedScripts.size()) - 1))];
    auto subs = SubChunks(s, 6, s.size());
    switch (I(0, 5)) {
    case 0:                                             // its frame gone
        for (auto &x : subs)
            if (x.first == 0x834)
                x.second = Chunk(0x834, std::string("nothing") + std::string(1, '\0'));
        break;
    case 1:                                             // a script file that does not compile (none there)
        for (auto &x : subs)
            if (x.first == 0xC1C)
                x.second = Chunk(0xC1C, std::string("none.scr") + std::string(1, '\0'));
        break;
    case 2:                                             // a sub-chunk missing
        if (!subs.empty())
            subs.erase(subs.begin() + I(0, int(subs.size()) - 1));
        break;
    case 3:                                             // two sub-chunks swapped
        if (subs.size() > 1) {
            const int a = I(0, int(subs.size()) - 1), b = I(0, int(subs.size()) - 1);
            std::swap(subs[size_t(a)], subs[size_t(b)]);
        }
        break;
    case 4:                                             // another frame's (the world's)
        for (auto &x : subs)
            if (x.first == 0x834)
                x.second = Chunk(0x834, std::string(FRAME_NAMES[I(0, WORLD_FRAMES - 1)]) + std::string(1, '\0'));
        break;
    default:                                            // as saved
        break;
    }
    std::string d;
    for (const auto &x : subs)
        d += x.second;
    return Chunk(0x44C, d);
}
std::string MakeModFile()
{
    std::vector<std::string> chunks;
    for (int n = I(0, 7); n > 0; --n) {
        const int kind = I(0, 9);
        std::string d;
        switch (kind) {
        case 0: {                                       // the saved values
            const int count = P(0.8) ? 256 : I(0, 300);
            for (int i = 0; i < count; ++i)
                d += Word(uint32_t(P(0.9) ? 0 : I(-5, 100)));
            chunks.push_back(Chunk(0x1004, d));
            break;
        }
        case 1:                                         // a pause sound
            chunks.push_back(Chunk(0x1036, Sized(RandomName())));
            break;
        case 2: {                                       // a rotation: its sub-chunks in any order, some missing or unknown
            std::vector<std::string> subs;
            if (P(0.9)) {
                const std::string name = RandomName();
                subs.push_back(Chunk(0x1450, name + std::string(1, '\0')));
            }
            // (Always its quaternion: without one both sides set the frame's rotation from their stack's leftovers.)
            subs.push_back(Chunk(0x14B4, FloatWord(float(I(-10, 10)) * 0.1f) + FloatWord(float(I(-10, 10)) * 0.1f) +
                                                 FloatWord(float(I(-10, 10)) * 0.1f) + FloatWord(float(I(-10, 10)) * 0.1f)));
            if (P(0.9))
                subs.push_back(Chunk(0x1518, Word(uint32_t(P(0.9) ? I(0, 2) : I(-2, 5)))));
            if (P(0.9))
                subs.push_back(Chunk(0x157C, FloatWord(float(I(-400, 400)) * 0.01f)));
            if (P(0.9))
                subs.push_back(Chunk(0x15E0, FloatWord(float(I(-200, 400)) * 0.01f)));
            // (No unknown sub-chunk: the original goes on without ascending past it, reading its data as the next
            // header; the port skips it.)
            for (size_t i = subs.size(); i > 1; --i)
                std::swap(subs[i - 1], subs[size_t(I(0, int(i) - 1))]);
            for (const std::string &s : subs)
                d += s;
            chunks.push_back(Chunk(0x13EC, d));
            break;
        }
        case 3: {                                       // the weather
            // (Never fewer than its 10 values: the original sets the ones a short chunk lacks from its stack's
            // leftovers, the port zeroes them.)
            const int count = P(0.85) ? 10 : I(10, 12);
            for (int i = 0; i < count; ++i)
                d += Word(uint32_t(P(0.4) ? I(0, 1) : I(0, 1000)));
            chunks.push_back(Chunk(0x16A8, d));
            break;
        }
        case 4:                                         // the fake cutscene flag
            chunks.push_back(Chunk(0x47E, std::string(1, char(I(0, 2)))));
            break;
        case 5:                                         // an unknown chunk (the load stops)
            if (P(0.3))
                chunks.push_back(Chunk(uint16_t(I(1, 0x7FFF)), Word(U32())));
            break;
        case 6: {                                       // the remembered frames (a name not found ends the chunk there)
            const int count = I(0, 4);
            d += Word(uint32_t(count));
            static const char *const anims[] = {"model1.i3d", "walk.i3d", "open.i3d", "run.i3d", ""};
            for (int i = 0; i < count; ++i) {
                const int f = P(0.12) ? -1 : I(0, WORLD_FRAMES - 1);
                uint32_t what = U32() & 0x3F;
                // (Sounds only for a sound frame, animations only for a model: both sides call the type's slots.)
                if (f < 0 || FRAME_TYPES[f] != FRAME_SOUND)
                    what &= ~uint32_t(SCRIPT_FRAME_SOUND);
                if (f < 0 || FRAME_TYPES[f] != FRAME_MODEL)
                    what &= ~uint32_t(SCRIPT_FRAME_ANIMATION);
                d += Sized(f < 0 ? "nothing" : FRAME_NAMES[f]) + Word(what) + std::string(1, char(I(0, 1)));
                if (what & SCRIPT_FRAME_ON)
                    d += std::string(1, char(I(0, 1)));
                if (what & SCRIPT_FRAME_SOUND)
                    d += FloatWord(float(I(-5, 5))) + FloatWord(float(I(-5, 5))) + FloatWord(float(I(-5, 5))) +
                         FloatWord(float(I(0, 10)) * 0.1f) + Word(uint32_t(I(0, 1000)));
                if (what & SCRIPT_FRAME_POSITION)
                    d += FloatWord(float(I(-50, 50))) + FloatWord(float(I(-50, 50))) + FloatWord(float(I(-50, 50)));
                if (what & SCRIPT_FRAME_ROTATION)
                    d += FloatWord(float(I(-10, 10)) * 0.1f) + FloatWord(float(I(-10, 10)) * 0.1f) +
                         FloatWord(float(I(-10, 10)) * 0.1f) + FloatWord(float(I(-10, 10)) * 0.1f);
                if (what & SCRIPT_FRAME_LINKED)
                    d += Sized(RandomName());
                if (what & SCRIPT_FRAME_ANIMATION) {
                    const int stages = I(0, 3);
                    d += Word(uint32_t(stages));
                    for (int s = 0; s < stages; ++s)
                        d += Word(uint32_t(I(0, 7))) + Word(uint32_t(I(-1, 3000))) + Sized(anims[I(0, 4)]) +
                             std::string(1, char(I(0, 1))) + std::string(1, char(I(0, 1)));
                }
            }
            chunks.push_back(Chunk(0x170C, d));
            break;
        }
        case 7:
        case 8:                                         // a script (one of the original's saved ones)
            if (!g_savedScripts.empty())
                chunks.push_back(MakeScriptChunk());
            break;
        default:
            break;
        }
    }
    for (size_t i = chunks.size(); i > 1; --i)
        std::swap(chunks[i - 1], chunks[size_t(I(0, int(i) - 1))]);
    std::string body;
    for (const std::string &c : chunks)
        body += c;
    return Chunk(0x4444, body);
}

std::vector<uint32_t> g_bitQueue[2];
std::vector<std::string> g_textQueue[2];
size_t g_bitAt[2], g_textAt[2];
std::vector<uint8_t *> g_animSets[2];                   // (the case's animation sets: their counts compared)
uint8_t *g_forcedHuman[2];                              // (a human the case put on a model: often the active player)
uint8_t *g_igraph[2];                                   // (part 7: GetIGraph's, the engine's vtable)
int g_screen[2];                                        // (its ScrnX, ScrnY)

// An original C_str (its operator=: a rep of the original's operator new) in `slot` (zeroed).
void OrigStrAssign(void *slot, const char *text)
{
    using T = void *(TC *)(void *, const char *);
    OriginalAt<T>(O_STR_ASSIGN)(slot, text);
}

// ---- each side's module ----
// The module's own program (its handlers write saved values, which the cases compare): mod_x.scr.
const char *const MOD_PROGRAM =
    "block{\r\n  INTEGER a = 0;\r\n}\r\n"
    "OnExit(){\r\n  SaveGameValue(7, 70);\r\n}\r\n"
    "OnGlobalAlarm(){\r\n  SaveGameValue(8, 80);\r\n}\r\n"
    "OnCutscene(1){\r\n  SaveGameValue(9, 90);\r\n}\r\n"
    "OnCutsceneDone(1){\r\n  SaveGameValue(10, 100);\r\n}\r\n"
    "Delay(10);\r\nDelay(20);\r\nSaveGameValue(11, 1);\r\n";
// A script on a world frame (each side's, compiled from the variables' program or the module's), its state set.
void *NewScript(int frameIndex, const S_ModScript &s)
{
    uint8_t *frame = W().frames[size_t(frameIndex)].bytes;
    const std::string path = s.handlers ? "Scripts\\AFRICA1\\mod_x.scr" : "Scripts\\AFRICA1\\cmd_x.scr";
    // (Compiled as the server, BuildModule's caller sets the case's role after: without the server's role the
    // original's AddCommand adds nothing, and its ParseBlock then pops a brace block's command that is not there.)
    if (g_side) {
        uint8_t *b = static_cast<uint8_t *>(O_New(0x200));
        OriginalAt<T_Ctor>(0x0041dcb0)(b, frame, 0);
        OriginalAt<T_Compile>(0x0042c850)(OriginalAt<void *>(0x008993f0), path.c_str(), b);
        *reinterpret_cast<int *>(b + 0xD8) = s.ip;
        b[0x104] = s.running;
        *reinterpret_cast<int *>(b + 0x18) = s.delay;
        b[7] = s.hud;
        b[8] = s.panel;
        b[9] = s.inHandler;
        return b;                                       // (not named: a script made later may take its address)
    }
    C_Script *t = new C_Script(AsFrame(frame), false);
    g_scriptCompiler.Compile(path.c_str(), t);
    t->m_ip = s.ip;
    t->m_running = s.running;
    t->m_delay = s.delay;
    t->m_hudHiddenByScript = s.hud;
    t->m_hudPanelHiddenByScript = s.panel;
    t->m_inCutsceneHandler = s.inHandler;
    return t;
}
// The lightmap groups (each side's fake module, the original's layout: a vector of groups at +0x0C, each group's name
// a C_str first: its rep's text at +8).
void SetGroups(const std::vector<std::string> &names)
{
    uint8_t *lm = W().lightmaps;
    uint8_t *list = Block(4 * (names.size() + 1));
    for (size_t i = 0; i < names.size(); ++i) {
        uint8_t *group = Block(0x40), *rep = Block(0x110);
        const int one = 1, length = int(names[i].size());
        std::memcpy(rep, &one, 4);
        std::memcpy(rep + 4, &length, 4);
        std::memcpy(rep + 8, names[i].c_str(), names[i].size() + 1);
        *reinterpret_cast<uint8_t **>(group) = names[i].empty() ? nullptr : rep;
        reinterpret_cast<uint8_t **>(list)[i] = group;
        Name(group, "group " + names[i]);
    }
    *reinterpret_cast<uint8_t **>(lm + 0x10) = list;
    *reinterpret_cast<uint8_t **>(lm + 0x14) = list + 4 * names.size();
}
void BuildModule(const S_ModCase &c)
{
    S_World &w = W();
    SetRole(GAME_ROLE_LOCAL);                           // (the scripts compiled as the server; the case's role below)
    SetGroups(c.groups);
    // The quick messages (the game's +0x60).
    uint8_t *quick = Block(0x100);
    Name(quick, "quick messages");
    if (g_side)
        SetGameField(0x60, reinterpret_cast<uint32_t>(quick));
    else
        reinterpret_cast<C_game *>(g_portGameBuf)->m_quickMessages = reinterpret_cast<C_QuickMessages *>(quick);
    // The models' file names (+0x134; empty: none).
    static std::string files[2][2];
    for (int m = 0; m < 2; ++m) {
        files[g_side][m] = c.modelFiles[size_t(m)];
        *reinterpret_cast<const char **>(w.frames[m ? 10 : 2].bytes + 0x134) =
            files[g_side][m].empty() ? nullptr : files[g_side][m].c_str();
    }
    // The sound frames' velocity (+0x1A0) and volume (+0x16C).
    for (int f : {3, 14}) {
        float *v = reinterpret_cast<float *>(w.frames[size_t(f)].bytes + 0x1A0);
        for (int i = 0; i < 4; ++i)
            v[i] = float(f * 3 + i) * 0.5f;
        *reinterpret_cast<float *>(w.frames[size_t(f)].bytes + 0x16C) = float(f) * 0.05f;
    }
    auto frame = [&](int i) { return w.frames[size_t(i)].bytes; };
    std::vector<uint32_t> scripts, pause, rotations, remembered, sounds;
    for (const S_ModScript &s : c.scripts)
        scripts.push_back(s.frame < 0 ? 0u : reinterpret_cast<uint32_t>(NewScript(s.frame, s)));
    SetRole(c.role);
    for (int f : c.pause)
        pause.push_back(reinterpret_cast<uint32_t>(frame(f)));
    for (int f : c.soundFrames)
        sounds.push_back(reinterpret_cast<uint32_t>(frame(f)));
    for (const S_Rotation &r : c.rotations) {
        S_FrameRotation *x = static_cast<S_FrameRotation *>(_aligned_malloc(sizeof(S_FrameRotation), 16));
        std::memset(static_cast<void *>(x), 0, sizeof *x);
        x->frame = AsFrame(frame(r.frame));
        ++x->frame->m_refCount;
        x->axis = r.axis;
        x->angleLeft = r.angle;
        x->speed = r.speed;
        rotations.push_back(reinterpret_cast<uint32_t>(x));
    }
    for (const S_Remembered &r : c.remembered) {
        S_RememberedFrame *x = static_cast<S_RememberedFrame *>(_aligned_malloc(0x50, 16));
        std::memset(static_cast<void *>(x), 0, sizeof *x);
        I3D_frame *f = AsFrame(frame(r.frame));
        x->frame = f;
        if (!r.gone)
            ++f->m_refCount;
        if (r.lost)
            f->m_name = "lost";
        x->parent = r.parent ? f->m_parent : nullptr;
        if (x->parent)
            ++x->parent->m_refCount;
        x->what = r.what;
        x->on = r.on;
        std::memcpy(&x->pos, r.pos, 16);
        std::memcpy(&x->rot, r.rot, 16);
        std::memcpy(&x->soundVelocity, r.velocity, 16);
        x->soundVolume = r.volume;
        remembered.push_back(reinterpret_cast<uint32_t>(x));
    }
    if (g_side) {
        uint8_t *m = OMod();
        SetOrigVec(m, M_SCRIPTS, scripts);
        SetOrigVec(m, M_PAUSE_SOUNDS, pause);
        SetOrigVec(m, M_ROTATIONS, rotations);
        SetOrigVec(m, M_REMEMBERED, remembered);
        SetOrigVec(m, M_SOUND_FRAMES, sounds);
        std::memset(m + M_VALUES, 0, 0x400);
        for (const auto &v : c.values)
            *reinterpret_cast<int *>(m + M_VALUES + 4 * v.first) = v.second;
        m[M_FAKE] = c.fake;
        m[M_CUTSCENE_RUNNING] = c.cutscene;
    } else {
        C_ScriptModule &m = g_scriptModule;
        for (uint32_t s : scripts)
            m.m_scripts.push_back(reinterpret_cast<C_Script *>(s));
        for (uint32_t f : pause)
            m.m_pauseSounds.push_back(reinterpret_cast<I3D_frame *>(f));
        for (uint32_t r : rotations)
            m.m_rotations.push_back(reinterpret_cast<S_FrameRotation *>(r));
        for (uint32_t r : remembered)
            m.m_rememberedFrames.push_back(reinterpret_cast<S_RememberedFrame *>(r));
        for (uint32_t f : sounds)
            m.m_soundFrames.push_back(reinterpret_cast<I3D_frame *>(f));
        std::memset(m.m_saveGameValues, 0, sizeof m.m_saveGameValues);
        for (const auto &v : c.values)
            m.m_saveGameValues[v.first] = v.second;
        m.m_fakeCutscene = c.fake;
        m.m_cutsceneRunning = c.cutscene;
    }
    Name(g_side ? static_cast<void *>(OMod()) : static_cast<void *>(&g_scriptModule), "script module");
    // The humans' movements: blocks large enough for both layouts (the shadow frame: the original's +0xA9C, the port's
    // member), the case's shadow or none.
    auto giveMovement = [&](uint8_t *human, const std::string &name) {
        uint8_t *mv = Block(std::max(sizeof(C_HumanMovement), size_t(0x1000)) + 0x40);
        Name(mv, "movement of " + name);
        I3D_frame *shadow = c.shadow < 0 ? nullptr : AsFrame(w.frames[size_t(c.shadow)].bytes);
        if (g_side) {
            *reinterpret_cast<uint8_t **>(human + 0xA0) = mv;
            *reinterpret_cast<I3D_frame **>(mv + 0xA9C) = shadow;
        } else {
            reinterpret_cast<C_Human *>(human)->m_body.movement = reinterpret_cast<C_HumanMovement *>(mv);
            reinterpret_cast<C_HumanMovement *>(mv)->m_shadow = shadow;
        }
    };
    for (const W_Actor &x : w.actors)
        if (x.type == ACTOR_HUMAN)
            giveMovement(x.bytes, x.name);
    // The case's actors on the models (the original's type +0x18, frame +0x20; the port's members), the human among
    // them the team's active player's candidate.
    static void **actorVtables[2];
    if (!actorVtables[g_side])
        actorVtables[g_side] = GameVtable("C_Actor", VirtualSlots("C_Actor::", g_side != 0));
    g_forcedHuman[g_side] = nullptr;
    for (int m = 0; m < 2; ++m) {
        const int type = c.modelActor[m];
        if (type < 0)
            continue;
        uint8_t *frame = w.frames[m ? 10 : 2].bytes;
        uint8_t *a = Block(ActorBytes(type));
        *reinterpret_cast<void ***>(a) = actorVtables[g_side];
        const std::string name = F("forced actor on model%d", m + 1);
        Name(a, name);
        if (g_side) {
            *reinterpret_cast<int *>(a + 0x18) = type;
            *reinterpret_cast<uint8_t **>(a + 0x20) = frame;
        } else {
            reinterpret_cast<C_Actor *>(a)->m_type = E_ActorType(type);
            reinterpret_cast<C_Actor *>(a)->m_frame = AsFrame(frame);
        }
        if (type == ACTOR_HUMAN) {
            giveMovement(a, name);
            g_forcedHuman[g_side] = a;
        }
        if (!AsFrame(frame)->m_data)
            AsFrame(frame)->m_data = reinterpret_cast<S_FrameData *>(Block(0x20));
        AsFrame(frame)->m_data->actor = reinterpret_cast<C_Actor *>(a);
    }
    // The melee trackers' controllers on the world's frames get their class's vtable (AssignScript calls SetScript
    // there, +0x40).
    static void **controllerVtables[2];
    if (!controllerVtables[g_side])
        controllerVtables[g_side] = GameVtable("C_MeleeTrackerController", VirtualSlots("C_MeleeTrackerController::", g_side != 0));
    for (const W_Actor &x : w.actors)
        if (x.type == ACTOR_MELEE_TRACKER) {
            uint8_t *ai = g_side ? *reinterpret_cast<uint8_t **>(x.bytes + 0x534)
                                 : reinterpret_cast<uint8_t *>(reinterpret_cast<C_MeleeTracker *>(x.bytes)->m_ai);
            if (ai)
                *reinterpret_cast<void ***>(ai) = controllerVtables[g_side];
        }

    // Part 7: the humans' scripted controllers' script (AssignScript clears an AI human's when its file fails): a block
    // standing for an earlier script (not one of the module's: a freed one's address may come back to a new script on
    // one side only).
    if (c.controllersScripted) {
        uint8_t *earlier = Block(0x20);
        Name(earlier, "earlier script");
        for (const W_Actor &x : w.actors)
            if (x.type == ACTOR_HUMAN && x.controller) {
                if (g_side)
                    *reinterpret_cast<uint8_t **>(x.controller + 0x1AC) = earlier;
                else
                    reinterpret_cast<C_ScriptedController *>(x.controller)->m_script = reinterpret_cast<C_Script *>(earlier);
            }
    }
    // Part 7: the frames' owner models (a model's own frames and the ones another model linked into it).
    for (size_t i = 0; i < c.owners.size(); ++i)
        AsFrame(frame(int(i)))->m_ownerModel = c.owners[i] < 0 ? nullptr : AsFrame(frame(c.owners[i]));
    // The screen (IGraph's), the scene's viewport, the game's cutscene FOV and player input.
    g_igraph[g_side] = Block(0x40);
    *reinterpret_cast<void ***>(g_igraph[g_side]) = EngineVtable("IGraph");
    Name(g_igraph[g_side], "igraph");
    g_screen[0] = c.screen[0];
    g_screen[1] = c.screen[1];
    std::memcpy(w.scene + 0x244, c.viewport, 16);
    if (g_side) {
        g_origGame[0x6E] = c.inputEnabled;
        std::memcpy(g_origGame + 0xBC, &c.gameFov, 4);
    } else {
        C_game *g = reinterpret_cast<C_game *>(g_portGameBuf);
        g->m_playerInputEnabled = c.inputEnabled;
        g->m_fov = c.gameFov;
    }
    // The module's references: its animated models, its duplicates and the cutscene's camera each hold one.
    std::vector<uint32_t> animated, inCutscene, duplicates, pending;
    for (const auto &a : c.animated) {
        ++AsFrame(frame(a.first))->m_refCount;
        animated.push_back(reinterpret_cast<uint32_t>(frame(a.first)));
        inCutscene.push_back(a.second);
    }
    for (int d : c.duplicates) {
        ++AsFrame(frame(d))->m_refCount;
        duplicates.push_back(reinterpret_cast<uint32_t>(frame(d)));
    }
    I3D_frame *camera = c.camera < 0 ? nullptr : AsFrame(frame(c.camera));
    if (camera)
        ++camera->m_refCount;
    // The sounds waiting to start (null entries among them).
    for (const auto &p : c.pending) {
        uint8_t *s = nullptr;
        if (p.first >= 0) {
            s = static_cast<uint8_t *>(g_side ? O_New(8) : static_cast<void *>(new S_PendingSound));
            *reinterpret_cast<int *>(s) = p.second;
            *reinterpret_cast<uint8_t **>(s + 4) = frame(p.first);
            ++AsFrame(frame(p.first))->m_refCount;
        }
        pending.push_back(reinterpret_cast<uint32_t>(s));
    }
    if (g_side) {
        uint8_t *m = OMod();
        m[M_STARTED] = c.started;
        m[M_MAP_OPEN] = c.mapOpen;
        *reinterpret_cast<int *>(m + M_INPUT_DELAY) = c.inputDelay;
        *reinterpret_cast<I3D_frame **>(m + M_CAMERA) = camera;
        std::memcpy(m + M_ASPECT, &c.aspect, 4);
        std::memcpy(m + M_FOV, &c.fov, 4);
        *reinterpret_cast<int *>(m + M_CUTSCENE) = c.cutsceneNumber;
        std::memset(m + M_VALUES_AT_START, 0, 0x400);
        for (const auto &v : c.valuesAtStart)
            *reinterpret_cast<int *>(m + M_VALUES_AT_START + 4 * v.first) = v.second;
        SetOrigVec(m, M_ANIMATED, animated);
        SetOrigVec(m, M_ANIMATED_IN_CUTSCENE, inCutscene, 1);
        SetOrigVec(m, M_DUPLICATES, duplicates);
        // The script list: pairs of C_str (8 bytes each).
        std::vector<uint32_t> words;
        for (size_t i = 0; i < 2 * c.assignments.size(); ++i)
            words.push_back(0);
        SetOrigVec(m, M_ASSIGNMENTS, words);
        uint8_t *first = At<uint8_t *>(m, M_ASSIGNMENTS + 4);
        for (size_t i = 0; i < c.assignments.size(); ++i) {
            OrigStrAssign(first + 8 * i, c.assignments[i].first.c_str());
            OrigStrAssign(first + 8 * i + 4, c.assignments[i].second.c_str());
        }
        // (Its end: SetOrigVec made a vector of words.)
        *reinterpret_cast<uint8_t **>(m + M_ASSIGNMENTS + 8) = first + 8 * c.assignments.size();
        SetOrigVec(OriginalAt<uint8_t *>(O_PENDING_SOUNDS), 0, pending);
        *OriginalAt<uint8_t *>(O_RESTARTING) = 0;
    } else {
        C_ScriptModule &m = g_scriptModule;
        m.m_started = c.started;
        m.m_commandMapWasOpen = c.mapOpen;
        m.m_inputDelay = c.inputDelay;
        m.m_cutsceneCamera = static_cast<I3D_camera *>(camera);
        m.m_savedAspectRatio = c.aspect;
        m.m_savedFOV = c.fov;
        m.m_cutscene = c.cutsceneNumber;
        std::memset(m.m_saveGameValuesAtStart, 0, sizeof m.m_saveGameValuesAtStart);
        for (const auto &v : c.valuesAtStart)
            m.m_saveGameValuesAtStart[v.first] = v.second;
        for (size_t i = 0; i < animated.size(); ++i) {
            m.m_animatedModels.push_back(reinterpret_cast<I3D_frame *>(animated[i]));
            m.m_animatedInCutscene.push_back(inCutscene[i] != 0);
        }
        for (uint32_t d : duplicates)
            m.m_duplicates.push_back(reinterpret_cast<I3D_frame *>(d));
        for (const auto &a : c.assignments)
            m.m_assignments.push_back({C_str(a.first.c_str()), C_str(a.second.c_str())});
        for (uint32_t p : pending)
            g_pendingSounds.push_back(reinterpret_cast<S_PendingSound *>(p));
        g_scriptModuleRestarting = false;
    }
}

std::string RemText(const S_RememberedFrame *r)
{
    // (A sound frame's velocity and volume: the rest's are the heap's leftovers on both sides; the port's volume 0.)
    std::string s = F("remembered %s parent %s what %02x on %d pos ", NameOf(reinterpret_cast<uint32_t>(r->frame)).c_str(),
                      NameOf(reinterpret_cast<uint32_t>(r->parent)).c_str(), r->what, int(r->on)) +
                    VectorText(reinterpret_cast<uint32_t>(&r->pos), 4) + " rot " + VectorText(reinterpret_cast<uint32_t>(&r->rot), 4);
    if (r->frame && r->frame->m_type == FRAME_SOUND)
        s += " velocity " + VectorText(reinterpret_cast<uint32_t>(&r->soundVelocity), 4) + " volume " + FloatText(r->soundVolume);
    return s;
}
std::string RotText(const S_FrameRotation *r)
{
    return F("rotation %s axis %d angle %s speed %s", NameOf(reinterpret_cast<uint32_t>(r->frame)).c_str(), r->axis,
             FloatText(r->angleLeft).c_str(), FloatText(r->speed).c_str());
}
std::string ValuesText(const int *v)
{
    std::string s = "values";
    for (int i = 0; i < 256; ++i)
        if (v[i])
            s += F(" %d=%d", i, v[i]);
    return s;
}
std::vector<std::string> ModState()
{
    std::vector<std::string> out;
    // The world's humans' scripted controllers' script (AssignScript sets an AI human's): the module's script by its
    // place in the list, null or another.
    {
        std::vector<uint32_t> scripts;
        if (g_side)
            for (uint32_t q : OrigVec(OMod(), M_SCRIPTS, 4))
                scripts.push_back(At<uint32_t>(reinterpret_cast<void *>(q), 0));
        else
            for (const C_Script *t : g_scriptModule.m_scripts)
                scripts.push_back(reinterpret_cast<uint32_t>(t));
        for (const W_Actor &x : W().actors) {
            if (x.type != ACTOR_HUMAN || !x.controller)
                continue;
            const uint32_t s = g_side ? At<uint32_t>(x.controller, 0x1AC)
                                      : reinterpret_cast<uint32_t>(reinterpret_cast<const C_ScriptedController *>(x.controller)->m_script);
            const auto it = std::find(scripts.begin(), scripts.end(), s);
            out.push_back(x.name + " controller's script " +
                          (!s ? std::string("null") : it != scripts.end() ? F("%d", int(it - scripts.begin())) : NameOf(s)));
        }
    }
    auto names = [](const char *what, const std::vector<uint32_t> &items) {
        std::string s = what;
        for (uint32_t p : items)
            s += " " + NameOf(p);
        return s;
    };
    if (g_side) {
        uint8_t *m = OMod();
        out.push_back(F("module delay %d started %d map %d cutscene %d fake %d camera %s number %d", At<int>(m, M_INPUT_DELAY),
                        int(m[M_STARTED]), int(m[M_MAP_OPEN]), int(m[M_CUTSCENE_RUNNING]), int(m[M_FAKE]),
                        NameOf(At<uint32_t>(m, M_CAMERA)).c_str(), At<int>(m, M_CUTSCENE)) +
                      " aspect " + FloatText(At<float>(m, M_ASPECT)) + " fov " + FloatText(At<float>(m, M_FOV)));
        out.push_back(ValuesText(reinterpret_cast<const int *>(m + M_VALUES)));
        out.push_back("at start " + ValuesText(reinterpret_cast<const int *>(m + M_VALUES_AT_START)));
        for (uint32_t q : OrigVec(m, M_SCRIPTS, 4)) {
            const uint32_t s = At<uint32_t>(reinterpret_cast<void *>(q), 0);
            const uint8_t *b = reinterpret_cast<const uint8_t *>(s);
            // (Its kind by its vtable; not its name: a script made in the case may take a freed one's address.)
            const uint32_t vt = b ? At<uint32_t>(b, 0) : 0;
            const char *kind = vt == O_HUMAN_VTABLE ? "human" : vt == O_TRACKER_VTABLE ? "melee tracker" : "plain";
            out.push_back(!b ? std::string("script null")
                             : F("script %s on %s file %s ip %d running %d delay %d finished %d hud %d %d handler %d", kind,
                                 NameOf(At<uint32_t>(b, 0x100)).c_str(), OrigStr(At<uint32_t>(b, 0xCC)).c_str(),
                                 At<int>(b, 0xD8), int(b[0x104]), At<int>(b, 0x18), int(b[0xD4]), int(b[7]), int(b[8]),
                                 int(b[9])));
        }
        auto words = [&](uint32_t off) {
            std::vector<uint32_t> v;
            for (uint32_t q : OrigVec(m, off, 4))
                v.push_back(At<uint32_t>(reinterpret_cast<void *>(q), 0));
            return v;
        };
        out.push_back(names("pause sounds", words(M_PAUSE_SOUNDS)));
        out.push_back(names("sound frames", words(M_SOUND_FRAMES)));
        out.push_back(names("duplicates", words(M_DUPLICATES)));
        std::string animated = names("animated", words(M_ANIMATED)) + " in cutscene";
        for (uint32_t q : OrigVec(m, M_ANIMATED_IN_CUTSCENE, 1))
            animated += F(" %d", int(At<uint8_t>(reinterpret_cast<void *>(q), 0)));
        out.push_back(animated);
        for (uint32_t r : words(M_ROTATIONS))
            out.push_back(RotText(reinterpret_cast<const S_FrameRotation *>(r)));
        for (uint32_t r : words(M_REMEMBERED))
            out.push_back(RemText(reinterpret_cast<const S_RememberedFrame *>(r)));
        for (uint32_t q : OrigVec(OriginalAt<uint8_t *>(O_PENDING_SOUNDS), 0, 4)) {
            const uint32_t p = At<uint32_t>(reinterpret_cast<void *>(q), 0);
            out.push_back(!p ? std::string("pending null")
                             : F("pending %d %s", At<int>(reinterpret_cast<void *>(p), 0), NameOf(At<uint32_t>(reinterpret_cast<void *>(p), 4)).c_str()));
        }
        for (uint32_t q : OrigVec(m, M_ASSIGNMENTS, 8))
            out.push_back("assignment " + OrigStr(At<uint32_t>(reinterpret_cast<void *>(q), 0)) + " " +
                          OrigStr(At<uint32_t>(reinterpret_cast<void *>(q), 4)));
        float fov;
        std::memcpy(&fov, g_origGame + 0xBC, 4);
        out.push_back(F("game input %d restarting %d fov ", int(g_origGame[0x6E]), int(*OriginalAt<uint8_t *>(O_RESTARTING))) +
                      FloatText(fov));
    } else {
        const C_ScriptModule &m = g_scriptModule;
        out.push_back(F("module delay %d started %d map %d cutscene %d fake %d camera %s number %d", m.m_inputDelay,
                        int(m.m_started), int(m.m_commandMapWasOpen), int(m.m_cutsceneRunning), int(m.m_fakeCutscene),
                        NameOf(reinterpret_cast<uint32_t>(m.m_cutsceneCamera)).c_str(), m.m_cutscene) +
                      " aspect " + FloatText(m.m_savedAspectRatio) + " fov " + FloatText(m.m_savedFOV));
        out.push_back(ValuesText(m.m_saveGameValues));
        out.push_back("at start " + ValuesText(m.m_saveGameValuesAtStart));
        for (const C_Script *t : m.m_scripts)
            out.push_back(!t ? std::string("script null")
                             : F("script %s on %s file %s ip %d running %d delay %d finished %d hud %d %d handler %d",
                                 dynamic_cast<const C_HumanScript *>(t)          ? "human"
                                 : dynamic_cast<const C_MeleeTrackerScript *>(t) ? "melee tracker"
                                                                                 : "plain",
                                 NameOf(reinterpret_cast<uint32_t>(t->m_frame)).c_str(),
                                 PortStr(t->m_fileName).c_str(), t->m_ip, int(t->m_running), t->m_delay, int(t->m_finished),
                                 int(t->m_hudHiddenByScript), int(t->m_hudPanelHiddenByScript), int(t->m_inCutsceneHandler)));
        auto words = [](const std::vector<I3D_frame *> &v) {
            std::vector<uint32_t> out;
            for (I3D_frame *f : v)
                out.push_back(reinterpret_cast<uint32_t>(f));
            return out;
        };
        out.push_back(names("pause sounds", words(m.m_pauseSounds)));
        out.push_back(names("sound frames", words(m.m_soundFrames)));
        out.push_back(names("duplicates", words(m.m_duplicates)));
        std::string animated = names("animated", words(m.m_animatedModels)) + " in cutscene";
        for (bool b : m.m_animatedInCutscene)
            animated += F(" %d", int(b));
        out.push_back(animated);
        for (const S_FrameRotation *r : m.m_rotations)
            out.push_back(RotText(r));
        for (const S_RememberedFrame *r : m.m_rememberedFrames)
            out.push_back(RemText(r));
        for (const S_PendingSound *p : g_pendingSounds)
            out.push_back(!p ? std::string("pending null")
                             : F("pending %d %s", p->start, NameOf(reinterpret_cast<uint32_t>(p->sound)).c_str()));
        for (const auto &a : m.m_assignments)
            out.push_back("assignment " + PortStr(a.frameName) + " " + PortStr(a.scriptName));
        const C_game *g = reinterpret_cast<const C_game *>(g_portGameBuf);
        out.push_back(F("game input %d restarting %d fov ", int(g->m_playerInputEnabled), int(g_scriptModuleRestarting)) +
                      FloatText(g->m_fov));
    }
    return out;
}
// Frees what the module holds (the frames' references not: the world goes).
void ResetModule()
{
    if (g_side) {
        uint8_t *m = OMod();
        for (uint32_t q : OrigVec(m, M_SCRIPTS, 4))
            if (uint8_t *b = reinterpret_cast<uint8_t *>(At<uint32_t>(reinterpret_cast<void *>(q), 0))) {
                OriginalAt<T_Void>(0x0041df20)(b);
                O_Delete(b);
            }
        for (uint32_t off : {M_ROTATIONS, M_REMEMBERED})
            for (uint32_t q : OrigVec(m, off, 4))
                _aligned_free(reinterpret_cast<void *>(At<uint32_t>(reinterpret_cast<void *>(q), 0)));
        for (uint32_t q : OrigVec(m, M_ASSIGNMENTS, 8))
            for (uint32_t half : {0u, 4u})
                OriginalAt<T_Void>(O_STR_DTOR)(reinterpret_cast<void *>(q + half));
        for (uint32_t off : {M_SCRIPTS, M_PAUSE_SOUNDS, M_ROTATIONS, M_REMEMBERED, M_SOUND_FRAMES, M_ANIMATED,
                             M_ANIMATED_IN_CUTSCENE, M_DUPLICATES, M_ASSIGNMENTS})
            FreeOrigVec(m, off);
        uint8_t *pending = OriginalAt<uint8_t *>(O_PENDING_SOUNDS);
        for (uint32_t q : OrigVec(pending, 0, 4))
            O_Delete(reinterpret_cast<void *>(At<uint32_t>(reinterpret_cast<void *>(q), 0)));
        FreeOrigVec(pending, 0);
    } else {
        C_ScriptModule &m = g_scriptModule;
        for (C_Script *t : m.m_scripts)
            delete t;
        m.m_scripts.clear();
        for (S_FrameRotation *r : m.m_rotations)
            _aligned_free(r);
        m.m_rotations.clear();
        for (S_RememberedFrame *r : m.m_rememberedFrames)
            _aligned_free(r);
        m.m_rememberedFrames.clear();
        m.m_pauseSounds.clear();
        m.m_soundFrames.clear();
        m.m_animatedModels.clear();
        m.m_animatedInCutscene.clear();
        m.m_duplicates.clear();
        m.m_assignments.clear();
        for (S_PendingSound *p : g_pendingSounds)
            delete p;
        g_pendingSounds.clear();
    }
}

// ---- the answers the module's calls need ----
void SetUpModAnswers()
{
    // The team's active player: half the time the human the case put on a model (a frame message's camera target).
    T_Answer activePlayer = g_answers["C_Team::GetActivePlayer"];
    g_answers["C_Team::GetActivePlayer"] = [activePlayer](const std::vector<uint32_t> &a) {
        if (g_forcedHuman[g_side] && NextFake() % 2)
            return reinterpret_cast<uint32_t>(g_forcedHuman[g_side]);
        return activePlayer(a);
    };
    // ReadBits: the case's values first (a message's type, ...), then small ones (a count of 16 bits stays small).
    g_answers["C_BitStream::ReadBits"] = [](const std::vector<uint32_t> &a) {
        const int bits = int(a[1]);
        uint32_t v;
        if (g_bitAt[g_side] < g_bitQueue[g_side].size())
            v = g_bitQueue[g_side][g_bitAt[g_side]++];
        else
            v = uint32_t(FakeInt());
        // (A count of 16 bits stays small: ReadState's sound count is signed, and a negative one counts down through
        // 2^32 on both sides.)
        if (bits == 16)
            v %= 5;
        return bits >= 32 ? v : v & ((1u << bits) - 1);
    };
    g_answers["C_BitStream::ReadString"] = [](const std::vector<uint32_t> &a) {
        char *buf = reinterpret_cast<char *>(a[1]);
        const int size = int(a[2]);
        std::string s = g_textAt[g_side] < g_textQueue[g_side].size() ? g_textQueue[g_side][g_textAt[g_side]++]
                                                                       : std::string(MOD_NAMES[NextFake() % MOD_NAME_COUNT]);
        if (int(s.size()) >= size)
            s.resize(size_t(std::max(0, size - 1)));
        std::memcpy(buf, s.c_str(), s.size() + 1);
        return 0u;
    };
    // FindGroup (the original's is inlined there: the port's answered unlogged by the same search, last to first).
    g_answers["C_LightMapGroupsModule::FindGroup"] = [](const std::vector<uint32_t> &a) {
        const uint8_t *lm = reinterpret_cast<const uint8_t *>(a[0]);
        const char *name = reinterpret_cast<const char *>(a[1]);
        uint8_t **first = *reinterpret_cast<uint8_t **const *>(lm + 0x10), **last = *reinterpret_cast<uint8_t **const *>(lm + 0x14);
        if (!first)
            return 0u;
        for (uint8_t **g = last; g != first;) {
            --g;
            const uint8_t *rep = *reinterpret_cast<uint8_t *const *>(*g);
            if (_stricmp(rep ? reinterpret_cast<const char *>(rep + 8) : "", name) == 0)
                return reinterpret_cast<uint32_t>(*g);
        }
        return 0u;
    };
    g_extraQuiet.insert("C_LightMapGroupsModule::FindGroup");
    // (The original's AssignScript reads a human's controller type inline: the port's call answered from the world.)
    g_extraQuiet.insert("C_Human::GetControllerType");
    // (The original's cache copies its entries with an inline AddRef and destroys the old ones with Release when its
    // vector grows: the sets' Release calls are not logged; their counts are compared after the case.)
    g_extraQuiet.insert("I3D_animation_set::Release");
    g_outStrings.insert("C_BitStream::ReadString");
    // A model's animations: the same set for a stage each time it is asked (by the model's name and the stage).
    g_answers["I3D_model::GetAnimation"] = [](const std::vector<uint32_t> &a) {
        const std::string key = "anim of " + NameOf(a[0]) + F(" stage %d", int(a[1]));
        for (const auto &n : g_names[g_side])
            if (n.second == key)
                return n.first;
        const uint32_t h = Mix(g_fakeBase, uint32_t(std::hash<std::string>()(key)));
        if (h % 3 == 0)
            return 0u;
        uint8_t *anim = Block(0x40);
        *reinterpret_cast<void ***>(anim) = EngineVtable("I3D_animation_set");
        static const char *const files[] = {"anims\\walk.i3d", "run.i3d", "", "a\\b\\open.i3d", "model1.i3d"};
        const char *file = files[(h >> 4) % 5];
        *reinterpret_cast<const char **>(anim + 0x0C) = (h >> 8) % 5 ? file : nullptr;
        *reinterpret_cast<int *>(anim + 0x08) = int((h >> 12) % 3);
        *reinterpret_cast<int *>(anim + 4) = 1;
        Name(anim, key);
        g_animSets[g_side].push_back(anim);
        return reinterpret_cast<uint32_t>(anim);
    };
    // The animation cache's (both sides run it: the original's LoadGame inlines its Get): a new set (reference 1),
    // its Release counting down (+4).
    g_answers["I3D_driver::CreateAnimationSet"] = [](const std::vector<uint32_t> &) {
        uint8_t *anim = Block(0x40);
        *reinterpret_cast<void ***>(anim) = EngineVtable("I3D_animation_set");
        *reinterpret_cast<int *>(anim + 4) = 1;
        Name(anim, F("anim set %d", W().created++));
        g_animSets[g_side].push_back(anim);
        return reinterpret_cast<uint32_t>(anim);
    };
    g_answers["I3D_animation_set::Release"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(--*reinterpret_cast<int *>(a[0] + 4));
    };
    // Part 7: the screen (GetIGraph unlogged, as the other plain getters: the original's RunCutscene asks it for the
    // window handle in its copy protection, which the port leaves out; that GetHWND unlogged too).
    g_answers["GetIGraph"] = [](const std::vector<uint32_t> &) { return reinterpret_cast<uint32_t>(g_igraph[g_side]); };
    g_extraQuiet.insert("GetIGraph");
    g_extraQuiet.insert("IGraph::GetHWND");
    g_answers["IGraph::ScrnX"] = [](const std::vector<uint32_t> &) { return uint32_t(g_screen[0]); };
    g_answers["IGraph::ScrnY"] = [](const std::vector<uint32_t> &) { return uint32_t(g_screen[1]); };
    // A message to send: mostly one (the world's bit stream).
    g_answers["C_NetGame::CreateMessage"] = [](const std::vector<uint32_t> &) {
        return NextFake() % 4 ? reinterpret_cast<uint32_t>(W().bitstream) : 0u;
    };
    // EnumFrames: the callback on each of the frame's descendants in the type mask (depth first), listed first (the
    // callback relinks some). (Logged here without the callback: each side's own function.)
    g_extraQuiet.insert("I3D_frame::EnumFrames");
    g_answers["I3D_frame::EnumFrames"] = [](const std::vector<uint32_t> &a) {
        using T_Enum = uint32_t(__stdcall *)(I3D_frame *, void *);
        I3D_frame *root = reinterpret_cast<I3D_frame *>(a[0]);
        const T_Enum callback = reinterpret_cast<T_Enum>(a[1]);
        g_log[g_side].push_back(F("I3D_frame::EnumFrames(%s, context %s, types %08x, mask %s)", NameOf(a[0]).c_str(),
                                  NameOf(a[2]).c_str(), a[3], ArgText('s', a[4]).c_str()));
        std::vector<I3D_frame *> found;
        std::function<void(I3D_frame *)> walk = [&](I3D_frame *f) {
            for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling) {
                if (a[3] & (1u << c->m_type))
                    found.push_back(c);
                walk(c);
            }
        };
        walk(root);
        for (I3D_frame *f : found)
            if (callback(f, reinterpret_cast<void *>(a[2])))
                break;
        return 0u;
    };
}
// The original's copy protection's call (RunCutscene; the window handle in, a word out): nothing.
uint32_t __stdcall O_Protection(uint32_t) { return 0; }
// The animation caches: the port's g_animationCache, the original's at 0x008aed28 (constructed here: its static
// initialiser never ran), one search directory each; emptied after each case (before the world's blocks go).
constexpr uint32_t O_ANIMATION_CACHE = 0x008aed28, O_CACHE_CTOR = 0x006d5850, O_ANIMATION_CACHE_CLEAR = 0x006d9890;
constexpr uint32_t O_MODEL_CACHE = 0x008aeda0, O_MODEL_CACHE_CTOR = 0x006d51e0, O_MODEL_CACHE_CLEAR = 0x006d77e0;
void SetUpCaches()
{
    // (Its maps' std::_Lockit: the lock's critical section (0x0098bca8) set up here, its state (0x0098bcc0) 2 = ready:
    // the original's lazy set-up registers an atexit with its CRT, never started.)
    if (*OriginalAt<uint32_t *>(0x0098bcc0) != 2) {
        InitializeCriticalSection(OriginalAt<CRITICAL_SECTION *>(0x0098bca8));
        *OriginalAt<uint32_t *>(0x0098bcc0) = 2;
    }
    g_current = "the original's cache";
    OriginalAt<T_Void>(O_CACHE_CTOR)(OriginalAt<void *>(O_ANIMATION_CACHE));
    // (Its entries' limit (+0x34): the constructor leaves it; the port's 256.)
    *OriginalAt<uint32_t *>(O_ANIMATION_CACHE + 0x34) = 256;
    using T_AddDir = void(TC *)(void *, const char *);
    void **vt = *OriginalAt<void ***>(O_ANIMATION_CACHE);
    g_current = "the original's cache's directory";
    reinterpret_cast<T_AddDir>(vt[0x18 / 4])(OriginalAt<void *>(O_ANIMATION_CACHE), "anims");
    g_current = "the port's cache's directory";
    g_animationCache.AddDir("anims");
    // Part 7: the model cache (StopMission reloads a model through its OpenInto on both sides), one directory.
    g_current = "the original's model cache";
    OriginalAt<T_Void>(O_MODEL_CACHE_CTOR)(OriginalAt<void *>(O_MODEL_CACHE));
    *OriginalAt<uint32_t *>(O_MODEL_CACHE + 0x34) = 256;
    // (Its entries' vector (+0x14: first +0x18, last +0x1C, end +0x20) with room for 256: grown, it copies its entries
    // with an inline AddRef and releases the old copies, a Release the port's does not make.)
    {
        uint8_t *buf = static_cast<uint8_t *>(O_New(8 * 256));
        *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x18) = buf;
        *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x1C) = buf;
        *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x20) = buf + 8 * 256;
    }
    void **mvt = *OriginalAt<void ***>(O_MODEL_CACHE);
    reinterpret_cast<T_AddDir>(mvt[0x18 / 4])(OriginalAt<void *>(O_MODEL_CACHE), "models");
    g_modelCache.AddDir("models");
    g_current = "";
}
void ClearCache()
{
    if (g_side) {
        OriginalAt<T_Void>(O_ANIMATION_CACHE_CLEAR)(OriginalAt<void *>(O_ANIMATION_CACHE));
        OriginalAt<T_Void>(O_MODEL_CACHE_CLEAR)(OriginalAt<void *>(O_MODEL_CACHE));
    } else {
        g_animationCache.Clear();
        g_modelCache.Clear();
    }
}

// ---- the operations ----
using T_ChunkOpen = bool(TC *)(void *, const char *, bool);
using T_ChunkVoid = bool(TC *)(void *);
using T_ChunkBegin = bool(TC *)(void *, uint16_t);
using T_ChunkAscend = bool(TC *)(void *, uint32_t *);
using T_ChunkDescend = bool(TC *)(void *, uint16_t *);
using T_ModChunk = bool(TC *)(void *, void *);
using T_ModMsg = void(TC *)(void *, void *);
using T_ModOnMessage = bool(TC *)(void *, void *);
std::string Hex(const std::string &s)
{
    std::string h;
    for (unsigned char ch : s)
        h += F("%02x", ch);
    return h;
}
std::string ChunkPlace(const void *c)
{
    const bool open = At<uint8_t>(c, 0) != 0;
    return F("open %d at %d depth %d", int(open), open ? FileSeek(At<int>(c, 4), 0, 1) : -1, At<int>(c, 0x410));
}
// Part 7's operations (script_module.cpp).
using T_OpVoid = void(TC *)(void *);
using T_OpInt = void(TC *)(void *, int);
using T_OpFrame = void(TC *)(void *, void *);
using T_OpFrameBool = bool(TC *)(void *, void *);
using T_OpRemember = void(TC *)(void *, void *, uint32_t);
using T_OpRotate = void(TC *)(void *, void *, int, int, int);
using T_OpAssign = bool(TC *)(void *, void *, const char *);
using T_OpName = void(TC *)(void *, const char *);
using T_OpBegin = void *(__attribute__((fastcall)) *)(int, int);
using T_OpSend = void(__attribute__((fastcall)) *)(void *);
std::string RunOp7(const S_ModCase &c)
{
    uint8_t *m = OMod();
    uint8_t *fb = W().frames[size_t(c.frame)].bytes;
    I3D_frame *f = AsFrame(fb);
    C_ScriptModule &p = g_scriptModule;
    const bool o = g_side != 0;
    const char *name = c.nullName ? nullptr : c.name.c_str();
    auto frameBool = [&](uint32_t orig, bool (C_ScriptModule::*port)(I3D_frame *)) {
        return F("%s %d", OP_NAMES[c.op], int(o ? OriginalAt<T_OpFrameBool>(orig)(m, fb) : (p.*port)(f)));
    };
    auto intOp = [&](uint32_t orig, void (C_ScriptModule::*port)(int), int v) {
        if (o)
            OriginalAt<T_OpInt>(orig)(m, v);
        else
            (p.*port)(v);
        return F("%s %d", OP_NAMES[c.op], v);
    };
    auto boolOp = [&](uint32_t orig, void (C_ScriptModule::*port)(bool)) {
        if (o)
            OriginalAt<T_OpInt>(orig)(m, int(c.flag));
        else
            (p.*port)(c.flag);
        return F("%s %d", OP_NAMES[c.op], int(c.flag));
    };
    switch (c.op) {
    case OP_REMEMBER:
        if (o)
            OriginalAt<T_OpRemember>(O_REMEMBER)(m, fb, c.what);
        else
            p.RememberFrame(f, c.what);
        return F("RememberFrame %02x", c.what);
    case OP_RESTORE_FRAME:
        if (o)
            OriginalAt<T_OpFrame>(O_RESTORE_FRAME)(m, fb);
        else
            p.RestoreFrame(f);
        return "RestoreFrame";
    case OP_REGISTER_SOUND:
        if (o)
            OriginalAt<T_OpFrame>(O_REGISTER_SOUND)(m, fb);
        else
            p.RegisterSoundFrame(f);
        return "RegisterSoundFrame";
    case OP_REGISTER_ANIMATED: return frameBool(O_REGISTER_ANIMATED, &C_ScriptModule::RegisterAnimatedModel);
    case OP_UNREGISTER_ANIMATED: return frameBool(O_UNREGISTER_ANIMATED, &C_ScriptModule::UnregisterAnimatedModel);
    case OP_REGISTER_DUPLICATE: return frameBool(O_REGISTER_DUPLICATE, &C_ScriptModule::RegisterDuplicate);
    case OP_UNREGISTER_DUPLICATE: return frameBool(O_UNREGISTER_DUPLICATE, &C_ScriptModule::UnregisterDuplicate);
    case OP_ROTATE:
        if (o)
            OriginalAt<T_OpRotate>(O_ROTATE)(m, fb, c.axis, c.angle, c.speed);
        else
            p.RotateFrame(f, c.axis, c.angle, c.speed);
        return F("RotateFrame %d %d %d", c.axis, c.angle, c.speed);
    case OP_UPDATE: return intOp(O_UPDATE, &C_ScriptModule::Update, c.time);
    case OP_TICK: return intOp(O_TICK, &C_ScriptModule::Tick, c.time);
    case OP_LOAD_SCRIPT_LIST: return boolOp(O_LOAD_SCRIPT_LIST, &C_ScriptModule::LoadScriptList);
    case OP_START: return boolOp(O_START, &C_ScriptModule::StartMission);
    case OP_STOP: return boolOp(O_STOP, &C_ScriptModule::StopMission);
    case OP_RESTART: return boolOp(O_RESTART, &C_ScriptModule::RestartMission);
    case OP_FREE: return boolOp(O_FREE, &C_ScriptModule::FreeMissionData);
    case OP_ASSIGN:
        return F("AssignScript %d", int(o ? OriginalAt<T_OpAssign>(O_ASSIGN)(m, fb, name) : p.AssignScript(f, name)));
    case OP_ALARM:
        if (o)
            OriginalAt<T_OpVoid>(O_ALARM)(m);
        else
            p.BroadcastGlobalAlarm();
        return "BroadcastGlobalAlarm";
    case OP_ADD_PAUSE:
        if (o)
            OriginalAt<T_OpName>(O_ADD_PAUSE)(m, name);
        else
            p.AddPauseSound(name);
        return "AddPauseSound";
    case OP_SET_PAUSED: return boolOp(O_SET_PAUSED, &C_ScriptModule::SetPauseSoundsPaused);
    case OP_RUN_CUTSCENE: return intOp(O_RUN_CUTSCENE, &C_ScriptModule::RunCutscene, c.number);
    case OP_END_CUTSCENE:
        if (o)
            OriginalAt<T_OpVoid>(O_END_CUTSCENE)(m);
        else
            p.EndCutscene();
        return "EndCutscene";
    case OP_WRITE_VALUES: {
        if (o) {
            alignas(16) uint8_t ch[0x500] = {};
            OriginalAt<T_ChunkOpen>(O_CHUNK_OPEN)(ch, "o_vals.sav", true);
            OriginalAt<T_ChunkBegin>(O_CHUNK_BEGIN)(ch, 0x1004);
            const bool r = OriginalAt<T_ModChunk>(O_WRITE_VALUES)(m, ch);
            const std::string place = ChunkPlace(ch);
            OriginalAt<T_ChunkAscend>(O_CHUNK_ASCEND)(ch, nullptr);
            OriginalAt<T_ChunkVoid>(O_CHUNK_CLOSE)(ch);
            return F("WriteSaveGameValues %d %s file ", int(r), place.c_str()) + Hex(g_overrides["o_vals.sav"]);
        }
        C_chunk ch;
        ch.Open("p_vals.sav", true);
        ch.Begin(0x1004);
        const bool r = p.WriteSaveGameValues(ch);
        const std::string place = ChunkPlace(&ch);
        ch.Ascend();
        ch.Close();
        return F("WriteSaveGameValues %d %s file ", int(r), place.c_str()) + Hex(g_overrides["p_vals.sav"]);
    }
    case OP_READ_VALUES: {
        g_overrides["mod_vals.sav"] = c.valuesFile;
        if (o) {
            alignas(16) uint8_t ch[0x500] = {};
            const bool open = OriginalAt<T_ChunkOpen>(O_CHUNK_OPEN)(ch, "mod_vals.sav", false);
            uint16_t id = 0;
            const bool in = OriginalAt<T_ChunkDescend>(O_CHUNK_DESCEND)(ch, &id);
            const bool r = OriginalAt<T_ModChunk>(O_READ_VALUES)(m, ch);
            const std::string place = ChunkPlace(ch);
            OriginalAt<T_ChunkAscend>(O_CHUNK_ASCEND)(ch, nullptr);
            OriginalAt<T_ChunkVoid>(O_CHUNK_CLOSE)(ch);
            return F("ReadSaveGameValues open %d in %d %04x result %d %s", int(open), int(in), id, int(r), place.c_str());
        }
        C_chunk ch;
        const bool open = ch.Open("mod_vals.sav", false);
        uint16_t id = 0;
        const bool in = ch.Descend(id);
        const bool r = p.ReadSaveGameValues(ch);
        const std::string place = ChunkPlace(&ch);
        ch.Ascend();
        ch.Close();
        return F("ReadSaveGameValues open %d in %d %04x result %d %s", int(open), int(in), id, int(r), place.c_str());
    }
    case OP_BEGIN_MESSAGE: {
        void *msg = o ? OriginalAt<T_OpBegin>(O_BEGIN_MESSAGE)(c.number, int(c.flag)) : BeginScriptNetMessage(c.number, c.flag);
        return F("BeginScriptNetMessage %d %d -> %s", c.number, int(c.flag), NameOf(reinterpret_cast<uint32_t>(msg)).c_str());
    }
    case OP_SEND_MESSAGE: {
        void *msg = c.flag ? static_cast<void *>(W().bitstream) : nullptr;
        if (o)
            OriginalAt<T_OpSend>(O_SEND_MESSAGE)(msg);
        else
            SendScriptNetMessage(static_cast<C_BitStream *>(msg));
        return "SendScriptNetMessage";
    }
    default:
        return "?";
    }
}

std::string RunOp(const S_ModCase &c)
{
    uint8_t *m = OMod();
    void *ms = c.nullMessage ? nullptr : W().bitstream;
    switch (c.op) {
    case OP_SAVE: {
        if (g_side) {
            alignas(16) uint8_t ch[0x500] = {};
            OriginalAt<T_ChunkOpen>(O_CHUNK_OPEN)(ch, "o_mod.sav", true);
            OriginalAt<T_ChunkBegin>(O_CHUNK_BEGIN)(ch, 0x4444);
            const bool r = OriginalAt<T_ModChunk>(O_MOD_SAVE)(m, ch);
            const std::string place = ChunkPlace(ch);
            OriginalAt<T_ChunkAscend>(O_CHUNK_ASCEND)(ch, nullptr);
            OriginalAt<T_ChunkVoid>(O_CHUNK_CLOSE)(ch);
            g_lastSave = g_overrides["o_mod.sav"];
            CollectScripts(g_lastSave);
            return F("save %d %s file ", int(r), place.c_str()) + Hex(g_lastSave);   // (not through F: its 1,200 characters cut the file)
        }
        C_chunk ch;
        ch.Open("p_mod.sav", true);
        ch.Begin(0x4444);
        const bool r = g_scriptModule.SaveGame(ch);
        const std::string place = ChunkPlace(&ch);
        ch.Ascend();
        ch.Close();
        return F("save %d %s file ", int(r), place.c_str()) + Hex(g_overrides["p_mod.sav"]);   // (not through F: its 1,200 characters cut the file)
    }
    case OP_LOAD: {
        g_overrides["mod_load.sav"] = c.file;
        if (g_side) {
            alignas(16) uint8_t ch[0x500] = {};
            const bool open = OriginalAt<T_ChunkOpen>(O_CHUNK_OPEN)(ch, "mod_load.sav", false);
            uint16_t id = 0;
            const bool in = OriginalAt<T_ChunkDescend>(O_CHUNK_DESCEND)(ch, &id);
            const bool r = OriginalAt<T_ModChunk>(O_MOD_LOAD)(m, ch);
            const std::string place = ChunkPlace(ch);
            OriginalAt<T_ChunkAscend>(O_CHUNK_ASCEND)(ch, nullptr);
            OriginalAt<T_ChunkVoid>(O_CHUNK_CLOSE)(ch);
            return F("load open %d in %d %04x result %d %s", int(open), int(in), id, int(r), place.c_str());
        }
        C_chunk ch;
        const bool open = ch.Open("mod_load.sav", false);
        uint16_t id = 0;
        const bool in = ch.Descend(id);
        const bool r = g_scriptModule.LoadGame(ch);
        const std::string place = ChunkPlace(&ch);
        ch.Ascend();
        ch.Close();
        return F("load open %d in %d %04x result %d %s", int(open), int(in), id, int(r), place.c_str());
    }
    case OP_WRITE_STATE:
        if (g_side)
            OriginalAt<T_ModMsg>(O_WRITE_STATE)(m, ms);
        else
            g_scriptModule.WriteState(static_cast<C_BitStream *>(ms));
        return "write state";
    case OP_READ_STATE:
        if (g_side)
            OriginalAt<T_ModMsg>(O_READ_STATE)(m, ms);
        else
            g_scriptModule.ReadState(static_cast<C_BitStream *>(ms));
        return "read state";
    case OP_MESSAGE:
        return F("message %d", int(g_side ? OriginalAt<T_ModOnMessage>(O_ON_MESSAGE)(m, ms)
                                          : g_scriptModule.OnMessage(static_cast<C_BitStream *>(ms))));
    default:
        return RunOp7(c);
    }
}

int g_onlyOp = -1;
// Part 7: the module's other state and the operation's arguments.
template <class F> void RandomPart7(S_ModCase &c, F anyFrame)
{
    const int op = c.op;
    // (AssignScript reads each script's frame unchecked when it looks for the frame's: no null entries where it runs, on
    // both sides.)
    if (op == OP_ASSIGN || op == OP_START || op == OP_RESTART)
        c.scripts.erase(std::remove_if(c.scripts.begin(), c.scripts.end(), [](const S_ModScript &s) { return s.frame < 0; }),
                        c.scripts.end());
    const bool mission = op == OP_START || op == OP_STOP || op == OP_RESTART || op == OP_FREE || op == OP_TICK;
    c.started = P(mission ? 0.7 : 0.5);
    c.mapOpen = P(0.3);
    c.inputDelay = P(0.5) ? 0 : P(0.2) ? 1 : P(0.6) ? I(1, 600) : I(-50, 2000);
    c.controllersScripted = P(0.5);
    c.cutsceneNumber = I(0, 2);
    c.aspect = float(I(100, 200)) * 0.01f;
    c.fov = float(I(30, 120)) * 0.017453292f;
    if (op == OP_END_CUTSCENE || op == OP_STOP || op == OP_FREE || op == OP_RESTART)
        c.cutscene = P(0.7);
    else if (op == OP_RUN_CUTSCENE)
        c.cutscene = P(0.2);
    // (A running cutscene's camera always: EndCutscene uses it unchecked on both sides; a camera's slots.)
    c.camera = c.cutscene || P(0.3) ? (P(0.5) ? 4 : 13) : -1;
    auto unique = [&](std::vector<int> &out, int n, auto pick) {
        for (; n > 0; --n) {
            const int f = pick();
            if (std::find(out.begin(), out.end(), f) == out.end())
                out.push_back(f);
        }
    };
    auto modelOrAny = [&] { return P(0.6) ? (P(0.5) ? 2 : 10) : anyFrame(); };
    std::vector<int> animated;
    unique(animated, P(0.4) ? 0 : I(1, 3), modelOrAny);
    for (int a : animated)
        c.animated.push_back({a, P(0.5)});
    unique(c.duplicates, P(0.5) ? 0 : I(1, 3), modelOrAny);
    static const char *const scriptNames[] = {"cmd_x", "mod_x", "mod_x.scr", "MOD_X.SCR", "cmd_x.Scr", "none", ""};
    for (int n = P(0.5) ? 0 : I(1, 3); n > 0; --n)
        c.assignments.push_back({RandomName(), scriptNames[I(0, 6)]});
    for (int n = P(0.5) ? 0 : I(1, 6); n > 0; --n)
        c.valuesAtStart.push_back({I(0, 255), P(0.5) ? I(1, 3) : I(-100, 100000)});
    for (int n = P(0.6) ? 0 : I(1, 3); n > 0; --n)
        c.pending.push_back({P(0.15) ? -1 : (P(0.5) ? 3 : 14), I(0, 5000)});
    // The owner models: a model's own frames, a frame another model linked in, none.
    c.owners.assign(WORLD_FRAMES, -1);
    for (int f : {1, 11, 8, 16, 18, 19, 20, 21, 22})
        c.owners[size_t(f)] = P(0.5) ? 2 : P(0.3) ? 10 : -1;
    for (int f : {12, 17})
        c.owners[size_t(f)] = P(0.5) ? 10 : P(0.3) ? 2 : -1;
    static const int screens[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1600, 1200}, {1366, 768}, {333, 777}};
    const int s = I(0, 6);
    c.screen[0] = screens[s][0];
    c.screen[1] = screens[s][1];
    c.viewport[1] = P(0.5) ? 0 : I(0, 200);
    c.viewport[2] = P(0.7) ? c.screen[0] : I(0, 2000);
    c.viewport[3] = P(0.7) ? c.screen[1] : I(0, 2000);
    c.gameFov = float(I(20, 120)) * 0.017453292f;
    c.inputEnabled = P(0.5);

    // The arguments.
    c.frame = anyFrame();
    if (op == OP_RESTORE_FRAME && !c.remembered.empty() && P(0.6))
        c.frame = c.remembered[size_t(I(0, int(c.remembered.size()) - 1))].frame;
    if (op == OP_REMEMBER && !c.remembered.empty() && P(0.4))
        c.frame = c.remembered[size_t(I(0, int(c.remembered.size()) - 1))].frame;
    if (op == OP_REGISTER_SOUND && !c.soundFrames.empty() && P(0.4))
        c.frame = c.soundFrames[size_t(I(0, int(c.soundFrames.size()) - 1))];
    if ((op == OP_REGISTER_ANIMATED || op == OP_UNREGISTER_ANIMATED) && P(0.7))
        c.frame = !c.animated.empty() && P(0.6) ? c.animated[size_t(I(0, int(c.animated.size()) - 1))].first : modelOrAny();
    if ((op == OP_REGISTER_DUPLICATE || op == OP_UNREGISTER_DUPLICATE) && P(0.7))
        c.frame = !c.duplicates.empty() && P(0.6) ? c.duplicates[size_t(I(0, int(c.duplicates.size()) - 1))] : modelOrAny();
    if (op == OP_ROTATE && !c.rotations.empty() && P(0.4))
        c.frame = c.rotations[size_t(I(0, int(c.rotations.size()) - 1))].frame;
    c.what = P(0.8) ? U32() & 0x3F : U32();
    c.axis = P(0.9) ? I(0, 2) : I(-1, 4);
    c.angle = I(-720, 720);
    c.speed = P(0.9) ? I(-360, 720) : I(-100000, 100000);
    c.time = P(0.8) ? I(0, 200) : I(-100, 5000);
    // (The edges: the input delay running out exactly (to 0 or 1); a rotation landing exactly on its angle (1000 ms at
    // its own speed: 1000 * 0.001f is 1.0f) or starting at 0.)
    if (op == OP_TICK && c.inputDelay > 0 && P(0.3))
        c.time = c.inputDelay - I(0, 1);
    if (op == OP_UPDATE && !c.rotations.empty() && P(0.3)) {
        S_Rotation &r = c.rotations[size_t(I(0, int(c.rotations.size()) - 1))];
        if (P(0.3))
            r.angle = 0.0f;
        else {
            r.speed = r.angle;
            c.time = 1000;
        }
    }
    c.number = op == OP_BEGIN_MESSAGE ? I(0, 600) : I(0, 2);
    c.flag = P(op == OP_START || op == OP_STOP || op == OP_FREE ? 0.8 : 0.5);
    if (op == OP_ASSIGN) {
        c.name = scriptNames[I(0, 6)];
        c.nullName = P(0.05);
    } else
        c.name = RandomName();
    // LoadScriptList's file: made (frame and script names in turn), the game's, none. (Never an entry without its
    // script: the original's C_chunk 0x00415d00 then assigns its buffer's leftovers, the last name it read.)
    c.listKind = P(0.7) ? 0 : P(0.5) ? 1 : 2;
    if (c.listKind == 0) {
        std::string d;
        for (int n = I(0, 5); n > 0; --n) {
            const std::string frameName = P(0.85) ? FRAME_NAMES[I(0, WORLD_FRAMES - 1)] : P(0.5) ? "nothing" : std::string(size_t(I(100, 990)), 'x');
            d += Chunk(1, frameName + std::string(1, '\0'));
            d += Chunk(1, std::string(scriptNames[I(0, 6)]) + std::string(1, '\0'));
        }
        c.list = Chunk(0, d);
    }
    // ReadSaveGameValues' file: its 256 values, or fewer, or more.
    {
        std::string d;
        const int count = P(0.7) ? 256 : I(0, 300);
        for (int i = 0; i < count; ++i)
            d += Word(uint32_t(P(0.8) ? 0 : I(-5, 100)));
        c.valuesFile = Chunk(0x1004, d);
    }
}
S_ModCase RandomModCase()
{
    S_ModCase c;
    if (g_onlyOp >= 0)
        c.op = g_onlyOp;
    else {
        int total = 0;
        for (int w : OP_WEIGHTS)
            total += w;
        int r = I(0, total - 1);
        c.op = 0;
        while (r >= OP_WEIGHTS[c.op])
            r -= OP_WEIGHTS[c.op++];
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT, 0};
    c.role = roles[P(0.6) ? 0 : I(1, 4)];
    // (A load, a mission's start and AssignScript as the server: a client's compile crashes the original's compiler on
    // a brace block, AssignScript's. The game compiles only there.)
    if ((c.op == OP_LOAD || c.op == OP_START || c.op == OP_ASSIGN) && !(c.role & GAME_ROLE_SERVER))
        c.role |= GAME_ROLE_SERVER;
    auto anyFrame = [] { return P(0.3) ? (P(0.5) ? 3 : 14) : P(0.3) ? (P(0.5) ? 2 : 10) : I(0, WORLD_FRAMES - 1); };
    for (int n = I(0, 3); n > 0; --n)
        c.pause.push_back(anyFrame());
    for (int n = I(0, 3); n > 0; --n)
        c.soundFrames.push_back(anyFrame());
    for (int n = P(0.5) ? 0 : I(1, 3); n > 0; --n)
        c.rotations.push_back({anyFrame(), P(0.9) ? I(0, 2) : I(-1, 4), float(I(-700, 700)) * 0.01f, float(I(-300, 600)) * 0.01f});
    // The frames with a parent (a LINKED remembered frame's parent is saved by name, unchecked on both sides).
    static const int children[] = {1, 11, 8, 12, 16, 18, 19, 17, 20, 21, 22};
    std::set<int> used;
    for (int n = P(0.3) ? 0 : I(1, 5); n > 0; --n) {
        S_Remembered r{};
        r.what = U32() & 0x3F;
        r.frame = (r.what & SCRIPT_FRAME_LINKED) ? children[I(0, 10)] : anyFrame();
        // (The game remembers a frame's sound only for a sound frame, its animations only for a model: both sides call
        // the type's own slots unchecked.)
        const int type = FRAME_TYPES[r.frame];
        if (type != FRAME_SOUND)
            r.what &= ~uint32_t(SCRIPT_FRAME_SOUND);
        if (type != FRAME_MODEL)
            r.what &= ~uint32_t(SCRIPT_FRAME_ANIMATION);
        if (used.count(r.frame))
            continue;
        used.insert(r.frame);
        r.parent = P(0.6);
        r.on = P(0.5);
        for (int i = 0; i < 4; ++i) {
            r.pos[i] = float(I(-100, 100)) * 0.5f;
            r.rot[i] = float(I(-10, 10)) * 0.1f;
            r.velocity[i] = float(I(-10, 10));
        }
        r.volume = float(I(0, 10)) * 0.1f;
        r.gone = P(0.15);
        r.lost = P(0.1);
        c.remembered.push_back(r);
    }
    for (int n = P(0.3) ? 0 : I(1, 3); n > 0; --n)
        c.scripts.push_back({P(0.1) ? -1 : anyFrame(), I(0, 30), P(0.5) ? 0 : I(1, 5000), P(0.5), P(0.5), P(0.2), P(0.2), P(0.3)});
    for (int n = P(0.5) ? 0 : I(1, 6); n > 0; --n)
        c.values.push_back({I(0, 255), P(0.5) ? I(1, 3) : I(-100, 100000)});
    c.fake = P(0.3);
    c.cutscene = P(0.3);
    c.shadow = P(0.5) ? -1 : I(0, WORLD_FRAMES - 1);
    // (The kinds a frame's message treats apart, on the models often.)
    static const int kinds[] = {ACTOR_HUMAN, ACTOR_ANIMATION, ACTOR_JUNKERS88, ACTOR_AIR_ATTACK, ACTOR_DOOR};
    for (int &k : c.modelActor)
        k = P(0.5) ? -1 : kinds[I(0, 4)];
    static const char *const groupNames[] = {"group1", "Group2", "", "light"};
    for (int n = I(0, 3); n > 0; --n)
        c.groups.push_back(groupNames[I(0, 3)]);
    static const char *const modelFiles[] = {"", "model1.i3d", "walk.i3d", "open.i3d"};
    c.modelFiles = {modelFiles[I(0, 3)], modelFiles[I(0, 3)]};
    if (c.op == OP_LOAD) {
        if (!g_lastSave.empty() && P(0.5)) {
            c.file = g_lastSave;
            ++g_loadsOfSaves;
        } else {
            c.file = MakeModFile();
            ++g_madeFiles;
        }
    }
    // (Signalling NaNs often: the original copies some weather values through the FPU, which quiets them.)
    auto word = [] { return P(0.15) ? (P(0.5) ? 0x7F800001u : 0xFF9CB5E5u) : U32(); };
    if (c.op == OP_MESSAGE) {
        // (A frame switched on or off a quarter of the time: its branches are the most.)
        c.bits.push_back(P(0.25)  ? uint32_t(SCRIPT_MSG_FRAME_SET_ON)
                         : P(0.9) ? uint32_t(MESSAGE_TYPES[I(0, MESSAGE_TYPE_COUNT - 1)])
                                  : uint32_t(I(0, 511)));
        for (int n = 0; n < 4; ++n)
            c.bits.push_back(P(0.3) ? uint32_t(I(0, 1)) : P(0.5) ? uint32_t(I(0, 300)) : word());
    }
    if (c.op == OP_READ_STATE) {
        c.bits.push_back(uint32_t(I(0, 1)));
        for (int n = 0; n < 12; ++n)
            c.bits.push_back(P(0.5) ? uint32_t(I(0, 3)) : word());
    }
    for (int n = 0; n < 6; ++n)
        c.texts.push_back(MOD_NAMES[I(0, MOD_NAME_COUNT - 1)]);
    // (A frame switched on or off: the models often.)
    if (c.op == OP_MESSAGE && c.bits[0] == uint32_t(SCRIPT_MSG_FRAME_SET_ON) && P(0.6)) {
        const int m = I(0, 1);
        c.texts[0] = m ? "model2" : "model1";
        if (P(0.7))
            c.modelActor[m] = P(0.5) ? ACTOR_HUMAN : kinds[I(0, 4)];
    }
    // (A frame's lightmap: the visual frames often, whose visual type may be a lightmap's.)
    if (c.op == OP_MESSAGE && c.bits[0] == uint32_t(SCRIPT_MSG_SET_LIGHTMAP) && P(0.7))
        c.texts[0] = P(0.5) ? "door" : "visual1";
    // (A group's lightmap: a group's name often.)
    if (c.op == OP_MESSAGE && c.bits[0] == uint32_t(SCRIPT_MSG_SET_GROUP_LIGHTMAP) && !c.groups.empty() && P(0.6))
        c.texts[0] = P(0.7) ? c.groups[size_t(I(0, int(c.groups.size()) - 1))] : std::string("GROUP1");
    // (Now and then no message at all.)
    c.nullMessage = (c.op == OP_WRITE_STATE || c.op == OP_READ_STATE || c.op == OP_MESSAGE) && P(0.03);
    if (c.op >= OP_REMEMBER)
        RandomPart7(c, anyFrame);
    return c;
}

void RunModCase(const S_ModCase &c)
{
    ++g_modCases;
    ++g_opCases[c.op];
    // (The original's frees held until the case ends: its compiler frees an expression node a new group still points
    // to; reused, the block would corrupt the script. sc_shared.inc's compilations do the same.)
    g_holdFrees = true;
    g_current = F("case %d: %s", g_modCases, OP_NAMES[c.op]);
    static const bool trace = std::getenv("HD2_MOD_TRACE") != nullptr;   // (each case named first: for a hang)
    if (trace)
        std::printf("%s\n", g_current.c_str());
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    // The script lists (LoadScriptList; StartMission's): the case's, the game's own or none.
    for (const char *file : {"missions\\AFRICA1\\Scripts.dta", "missions\\AFRICA1\\mpscripts.dta"}) {
        const std::string key = Lower(file);
        g_overrides.erase(key);
        g_cache.erase(key);
        if (c.listKind == 0)
            g_overrides[key] = c.list;
        else if (c.listKind == 2)
            g_cache[key] = std::string("\x01missing", 8);
    }
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        g_animSets[side].clear();
        BuildWorld(side, worldSeed);
        g_fakeBase = fakeBase ^ 0x5A5A5A5Au;
        g_fakeCount = 0;
        BuildModule(c);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_bitQueue[side] = c.bits;
        g_textQueue[side] = c.texts;
        g_bitAt[side] = g_textAt[side] = 0;
        g_log[side].clear();
        std::srand(randSeed);
        g_case = SIDE_NAMES[side];
        if (trace)
            std::printf("  %s%s\n", g_case, c.op == OP_LOAD ? (" file " + Hex(c.file)).c_str() : "");
        const std::string answer = RunOp(c);
        g_case = "";
        std::vector<std::string> &out = side ? r.orig : r.port;
        out.push_back(answer);
        for (const std::string &s : ModState())
            out.push_back(s);
        for (const std::string &s : FrameStates())
            out.push_back(s);
        // (The local matrices' rotation rows too: a rotation turns them in place, Update's.)
        for (const S_FakeFrame &fr : W().frames)
            out.push_back("matrix " + fr.name + " " + VectorText(reinterpret_cast<uint32_t>(&AsFrame(fr.bytes)->m_localMatrix.m[0][0]), 12));
        for (const uint8_t *anim : g_animSets[side])
            out.push_back(F("%s refs %d mode %d", NameOf(reinterpret_cast<uint32_t>(anim)).c_str(), At<int>(anim, 4), At<int>(anim, 8)));
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
    }
    if (g_show) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
    }
    if (!Compare(g_current, r) && std::getenv("HD2_MOD_DUMP")) {
        for (int side = 1; side >= 0; --side) {
            std::printf("  -- %s\n", SIDE_NAMES[side]);
            for (const std::string &s : side ? r.orig : r.port)
                std::printf("    %s\n", s.c_str());
        }
    }
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        ResetModule();
        ClearCache();
    }
    ReleaseHeldFrees();
    FreeWorld(0);
    FreeWorld(1);
}

LONG WINAPI DebugText(EXCEPTION_POINTERS *e)
{
    const EXCEPTION_RECORD *r = e->ExceptionRecord;
    if (r->ExceptionCode != 0x40010006u || r->NumberParameters < 2)   // DBG_PRINTEXCEPTION_C
        return EXCEPTION_CONTINUE_SEARCH;
    if (*g_case)
        g_log[g_side].push_back("debug " + std::string(reinterpret_cast<const char *>(r->ExceptionInformation[1]),
                                                       size_t(r->ExceptionInformation[0])));
    return EXCEPTION_CONTINUE_EXECUTION;
}
void SetUpMod()
{
    SetUpCmd();
    using G = C_GEffectsModule;
    g_virtuals.push_back({"C_GEffectsModule::SetRain", 0x40, PortSlot(&G::SetRain), "b", 'v'});
    g_virtuals.push_back({"C_GEffectsModule::StartMpLightning", 0x48, PortSlot(&G::StartMpLightning), "i", 'v'});
    g_virtuals.push_back({"C_MeleeTrackerController::SetScript", 0x40, PortSlot(&C_MeleeTrackerController::SetScript), "p", 'v'});
    SetUpModAnswers();
    RedirectOriginal(0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite));
    RedirectOriginal(0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek));
    RedirectOriginal(0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite));
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(&O_AlignedMalloc));
    RedirectOriginal(0x007e559c, reinterpret_cast<const void *>(&O_AlignedFree));
    *OriginalAt<uint32_t *>(O_PROTECTION_CALL) = reinterpret_cast<uint32_t>(&O_Protection);
    // The original's module (its static initialiser never ran).
    using T_ModuleCtor = void *(TC *)(void *, int, const char *);
    OriginalAt<T_ModuleCtor>(O_MODULE_CTOR)(OMod(), MODULE_SCRIPT, "Script");
    SetUpCaches();
    g_overrides[Lower("Scripts\\AFRICA1\\mod_x.scr")] = MOD_PROGRAM;
    if (const char *f = std::getenv("HD2_MOD_PROGRAM"))            // (another program for the module's scripts)
        if (FILE *in = std::fopen(f, "rb")) {
            std::string text;
            char buf[4096];
            for (size_t n; (n = std::fread(buf, 1, sizeof buf, in)) > 0;)
                text.append(buf, n);
            std::fclose(in);
            g_overrides[Lower("Scripts\\AFRICA1\\mod_x.scr")] = text;
        }
    // The debug texts both sides print (OutputDebugStringA raises DBG_PRINTEXCEPTION_C: the text logged as a call).
    AddVectoredExceptionHandler(1, DebugText);
    if (const char *op = std::getenv("HD2_MOD_OP"))
        for (int i = 0; i < OP_COUNT; ++i)
            if (Lower(op) == Lower(OP_NAMES[i]))
                g_onlyOp = i;
}

}  // namespace H

int main()
{
    using namespace H;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("mod_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *d = std::getenv("HD2_UNPACKED"))
        g_data = d;
    if (const char *s = std::getenv("HD2_MOD_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    g_show = std::getenv("HD2_MOD_SHOW") != nullptr;
    int cases = std::getenv("HD2_MOD_QUICK") ? 8000 : 40000;
    if (const char *s = std::getenv("HD2_MOD_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpMod();
    SetMission("AFRICA1");
    CompileProgram();
    for (int n = 0; n < cases; ++n)
        RunModCase(RandomModCase());
    std::string ops;
    for (int i = 0; i < OP_COUNT; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_NAMES[i], g_opCases[i]);
    std::printf("mod_diff: %d cases (%s; LoadGame: %d of the original's saves, %d made files), %d calls logged, %d checks, "
                "%d failures\n",
                g_modCases, ops.c_str(), g_loadsOfSaves, g_madeFiles, g_calls[0], checks, failures);
    return failures != 0;
}
