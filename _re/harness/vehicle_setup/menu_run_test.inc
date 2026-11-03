// Part 9d-ii-b's menu state (game/menu_state.cpp: starting, stopping, the input, the update, the
// drawing, the models and frames), in the factory build, run by ControlsTest after MenuTest: the
// driver's CreateFrame / GetState / SetState and IGraph's screen size, keys and characters put in
// for the test; the scene, its sector, the camera, the light, the click sound and the models
// objects of logging slots ("<name> <slot> <arguments>"); the sound module's update, music and the
// diary logged; the cursor's update, buttons, moves and drawing faked. Prints "menurun:" with 1 per
// check met.
#include <deque>
#include <map>
#include <set>

// The cursor's slots the menu state reads (the rest controls_test.inc's).
bool g_mrCursorChanged, g_mrPressed, g_mrReleased, g_mrMoved;
bool C_Cursor::Update(int time)
{
    GfLog("cursor update %d", time);
    return g_mrCursorChanged;
}
bool C_Cursor::IsPressed(uint32_t buttons) const { return g_mrPressed && buttons == 3; }
bool C_Cursor::IsReleased(uint32_t buttons) const { return g_mrReleased && buttons == 3; }
bool C_Cursor::HasMoved() const { return g_mrMoved; }
void C_Cursor::Draw() const { GfLog("cursor draw"); }

// The sound module's music and the diary.
void C_SoundModule::SetBackgroundMusic(int track, int position) { GfLog("music %d %d", track, position); }
void C_SoundModule::CrossfadeMusic(int track, int fadeTime, int position) { GfLog("crossfade %d %d %d", track, fadeTime, position); }
C_Diary g_mrDiary;
C_Diary *C_PlayerProfile::GetDiary() { return &g_mrDiary; }
void C_Diary::AddText(int textId) { GfLog("diary %d", textId); }
// The sound driver (its IsInitialized; the item build's moving_actors_test.inc has its own).
bool g_mrSoundOn = true;
bool __stdcall F_MrSndInitialized(void *) { return g_mrSoundOn; }
void *g_mrSndVtable[8] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&F_MrSndInitialized)};
struct {
    void **vtable;
} g_mrSndDriver = {g_mrSndVtable};
extern "C" ISND_driver *ISndGetDriver() { return reinterpret_cast<ISND_driver *>(&g_mrSndDriver); }
I3D_frame *g_mrClone;
std::deque<I3D_frame *> g_mrClones;                     // (first, when there are any: menu_soldier_test.inc)
std::string MrFrameName(const void *frame);
bool CloneFrameTree(I3D_frame *&copy, I3D_frame *source, I3D_frame *parent)
{
    GfLog("clone tree %s %d", MrFrameName(source).c_str(), parent != nullptr);
    if (!g_mrClones.empty()) {
        copy = g_mrClones.front();
        g_mrClones.pop_front();
        return true;
    }
    copy = g_mrClone;
    return true;
}

namespace {

struct S_MenuRunResults {
    bool mrStart = true, mrModes = true, mrMusic = true, mrScreens = true, mrStop = true, mrChars = true, mrKeys = true;
    bool mrEscape = true, mrUpdate = true, mrRender = true, mrDraw = true, mrCallback = true, mrCursor = true;
    bool mrDrag = true, mrWheel = true, mrFrames = true, mrModels = true, mrTyped = true;
};

// The logging objects: zeroed (an engine object's fields at their offsets), named.
struct S_MrObj {
    void **vtable;
    uint8_t body[0x2FC];
};
std::map<const void *, std::string> g_mrNames;
std::string MrName(const void *p)
{
    const auto it = g_mrNames.find(p);
    return it != g_mrNames.end() ? it->second : p ? std::string(GfName(static_cast<const I3D_frame *>(p))) : "null";
}
std::map<std::pair<const void *, int>, uint32_t> g_mrRet;

std::string MrArg(int v) { return CmText("%d", v); }
std::string MrArg(uint32_t v) { return CmText("%x", v); }
std::string MrArg(float v) { return CmText("%g", v); }
std::string MrArg(bool v) { return v ? "1" : "0"; }
std::string MrArg(const char *v) { return v ? v : "null"; }
std::string MrArg(const S_vector *v) { return CmText("(%g %g %g)", v->x, v->y, v->z); }
std::string MrArg(void *v) { return MrName(v); }

template <int SLOT, class... A> uint32_t __stdcall MrFake(void *self, A... a)
{
    std::string s = MrName(self) + CmText(" %x", SLOT);
    ((s += " " + MrArg(a)), ...);
    GfLog("%s", s.c_str());
    return g_mrRet[{self, SLOT}];
}
// A frame's SetOn (its on bit kept).
uint32_t __stdcall MrSetOn(I3D_frame *self, bool on)
{
    GfLog("%s on %d", MrName(self).c_str(), on);
    self->m_frameFlags = on ? self->m_frameFlags | 1 : self->m_frameFlags & ~1u;
    return 0;
}
// A scene's render callback kept.
void *g_mrCallback;
uint32_t __stdcall MrSetRenderCallback(void *self, void *callback, uint32_t flags)
{
    GfLog("%s callback %x", MrName(self).c_str(), flags);
    g_mrCallback = callback;
    return 0;
}

constexpr int MR_SLOTS = 96;
void *g_mrSceneVt[MR_SLOTS], *g_mrSectorVt[MR_SLOTS], *g_mrCameraVt[MR_SLOTS], *g_mrLightVt[MR_SLOTS],
    *g_mrSoundVt[MR_SLOTS], *g_mrModelVt[MR_SLOTS];
void MrInitVtables()
{
    for (void **vt : {g_mrSceneVt, g_mrSectorVt, g_mrCameraVt, g_mrLightVt, g_mrSoundVt, g_mrModelVt}) {
        FillTraps(vt, std::make_integer_sequence<int, MR_SLOTS>());
        vt[0x00 / 4] = reinterpret_cast<void *>(&MrFake<0x00>);
        vt[0x34 / 4] = reinterpret_cast<void *>(&MrFake<0x34, const char *>);
        vt[0x30 / 4] = reinterpret_cast<void *>(&MrSetOn);
        vt[0x24 / 4] = reinterpret_cast<void *>(&MrFake<0x24>);
    }
    g_mrSceneVt[0x74 / 4] = reinterpret_cast<void *>(&MrSetRenderCallback);
    g_mrSceneVt[0x78 / 4] = reinterpret_cast<void *>(&MrFake<0x78, uint32_t>);
    g_mrSceneVt[0x80 / 4] = reinterpret_cast<void *>(&MrFake<0x80, void *>);
    g_mrSceneVt[0x90 / 4] = reinterpret_cast<void *>(&MrFake<0x90, void *>);
    g_mrSceneVt[0xF8 / 4] = reinterpret_cast<void *>(&MrFake<0xF8>);
    g_mrSceneVt[0x148 / 4] = reinterpret_cast<void *>(&MrFake<0x148, int, int, int, int>);
    g_mrSceneVt[0x104 / 4] = reinterpret_cast<void *>(
        &MrFake<0x104, const S_vector *, const S_vector *, void *, void *, uint32_t, int, int, int>);
    g_mrSectorVt[0x74 / 4] = reinterpret_cast<void *>(&MrFake<0x74, void *>);
    g_mrSectorVt[0x7C / 4] = reinterpret_cast<void *>(&MrFake<0x7C, void *>);
    g_mrCameraVt[0x1C / 4] = reinterpret_cast<void *>(&MrFake<0x1C, const S_vector *, float>);
    g_mrCameraVt[0x7C / 4] = reinterpret_cast<void *>(&MrFake<0x7C, float, float>);
    g_mrCameraVt[0x74 / 4] = reinterpret_cast<void *>(&MrFake<0x74, float>);
    g_mrCameraVt[0x88 / 4] = reinterpret_cast<void *>(&MrFake<0x88, bool>);
    g_mrCameraVt[0x84 / 4] = reinterpret_cast<void *>(&MrFake<0x84, float>);
    g_mrLightVt[0x74 / 4] = reinterpret_cast<void *>(&MrFake<0x74, int>);
    g_mrLightVt[0x84 / 4] = reinterpret_cast<void *>(&MrFake<0x84, const S_vector *>);
    g_mrLightVt[0x90 / 4] = reinterpret_cast<void *>(&MrFake<0x90, float>);
    g_mrSoundVt[0x74 / 4] = reinterpret_cast<void *>(&MrFake<0x74, const char *, uint32_t>);
    g_mrSoundVt[0x7C / 4] = reinterpret_cast<void *>(&MrFake<0x7C, int>);
    g_mrSoundVt[0x8C / 4] = reinterpret_cast<void *>(&MrFake<0x8C, float>);
    g_mrSoundVt[0x78 / 4] = reinterpret_cast<void *>(&MrFake<0x78, int>);
    g_mrModelVt[0x38 / 4] = reinterpret_cast<void *>(&MrFake<0x38, void *, uint32_t>);
    g_mrModelVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
    g_mrModelVt[0x48 / 4] = reinterpret_cast<void *>(&MrFake<0x48, const char *, uint32_t>);
    g_mrModelVt[0xA0 / 4] = reinterpret_cast<void *>(&MrFake<0xA0>);
}
std::deque<S_MrObj *> g_mrObjects;
S_MrObj *MrObj(const char *name, void **vtable)
{
    auto *o = new S_MrObj();
    o->vtable = vtable;
    g_mrNames[o] = name;
    g_mrObjects.push_back(o);
    return o;
}
template <class T> T *As(S_MrObj *o) { return reinterpret_cast<T *>(o); }
// A campaign screen for Start (campaign.cpp not linked: zeroed memory of its size, only its inline
// getters read): its kind (the field found by its getter) and its diary texts.
alignas(16) uint8_t g_mrScreenMemory[sizeof(C_CampaignScreen)];
void MrSetScreen(C_CampaignScreen &screen, int kind, std::vector<int> texts)
{
    auto *bytes = reinterpret_cast<uint8_t *>(&screen);
    auto *textsField = reinterpret_cast<uint8_t *>(const_cast<std::vector<int> *>(&screen.GetDiaryTexts()));
    for (size_t off = sizeof(void *); off + sizeof(int) <= sizeof screen; off += sizeof(int)) {
        if (bytes + off >= textsField && bytes + off < textsField + sizeof(std::vector<int>))
            continue;
        int &field = *reinterpret_cast<int *>(bytes + off);
        const int old = field;
        field = 0x5A5A5A5A;
        if (screen.GetKind() == 0x5A5A5A5A) {
            field = kind;
            break;
        }
        field = old;
    }
    const_cast<std::vector<int> &>(screen.GetDiaryTexts()) = std::move(texts);
}

// The driver's frames made by type, in order; its states.
std::map<int, std::deque<S_MrObj *>> g_mrMade;
std::map<int, uint32_t> g_mrStates;
uint32_t __stdcall MrCreateFrame(void *, int type)
{
    GfLog("create %d", type);
    std::deque<S_MrObj *> &q = g_mrMade[type];
    if (q.empty())
        return 0;
    S_MrObj *o = q.front();
    q.pop_front();
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o));
}
uint32_t __stdcall MrGetState(void *, int state) { return g_mrStates[state]; }
uint32_t __stdcall MrSetState(void *, int state, uint32_t value)
{
    GfLog("state %x %u", state, value);
    g_mrStates[state] = value;
    return 0;
}
// IGraph's: the screen, the key pressed, the characters, the keys held.
int g_mrKey;
std::deque<int> g_mrChars;
std::set<int> g_mrDown;
bool g_mrShift, g_mrCtrl, g_mrAlt;
int __stdcall MrScrnX(void *) { return 800; }
int __stdcall MrScrnY(void *) { return 600; }
int __stdcall MrGetKey(void *) { return g_mrKey; }
uint32_t __stdcall MrReadChar(void *)
{
    if (g_mrChars.empty())
        return 0;
    const int c = g_mrChars.front();
    g_mrChars.pop_front();
    return static_cast<uint32_t>(c);
}
bool __stdcall MrIsKeyDown(void *, int key)
{
    GfLog("isdown %x", key);
    return g_mrDown.count(key) != 0;
}
bool __stdcall MrShift(void *) { return g_mrShift; }
bool __stdcall MrCtrl(void *) { return g_mrCtrl; }
bool __stdcall MrAlt(void *) { return g_mrAlt; }

// The sound module's update (its vtable: a module's).
struct C_MrSound : C_Module {
    C_MrSound() : C_Module(0, "MrSound") {}
    void Update(int time) override { GfLog("sound update %d", time); }
};

// A screen logging its slots ("<name> <slot> <id>"), Unknown20 / OnKeyClick / HasPopup set.
bool g_mrTake;
int g_mrClick = 1;
struct C_MrScreen : C_Screen {
    const char *name;
    bool popup = false;
    C_MrScreen(I3D_scene *scene, const char *n, uint32_t id) : C_Screen(scene, As<I3D_frame>(MrObj(n, g_mrModelVt))), name(n)
    {
        m_id = id;
    }
    int Log(const char *slot, int id)
    {
        GfLog("%s %s %x", name, slot, id);
        return 1;
    }
    void Refresh() override { GfLog("%s refresh", name); }
    void Tick(int time) override { GfLog("%s tick %d", name, time); }
    bool Unknown20(int key) override
    {
        GfLog("%s key %x", name, key);
        return g_mrTake;
    }
    bool HasPopup() const override { return popup; }
    int OnInput09(int id) override { return Log("in09", id); }
    int OnInput05(int id) override { return Log("in05", id); }
    int OnPress(int id) override { return Log("press", id); }
    int OnRelease(int id) override { return Log("release", id); }
    int OnInput13(int id) override { return Log("in13", id); }
    int OnKeyClick(int id) override
    {
        Log("click", id);
        return g_mrClick;
    }
    int OnInput0A(int id) override { return Log("in0a", id); }
    int OnInput08(int id) override { return Log("in08", id); }
    int OnWheel(int id, int amount) override
    {
        GfLog("%s wheel %x %d", name, id, amount);
        return 1;
    }
};

// A control logging its typed characters, its editing's end and its tick.
bool g_mrCharTaken;
int g_mrStopEdit = 1;
struct C_MrEdit : C_Control {
    explicit C_MrEdit(uint32_t id) : C_Control(nullptr, id, nullptr) {}
    bool OnChar(int c) override
    {
        GfLog("char %d", c);
        return g_mrCharTaken;
    }
    int OnInput0B(int id) override
    {
        GfLog("edit stop %x", id);
        return g_mrStopEdit;
    }
    void Tick(int time) override { GfLog("edit tick %d", time); }
};
struct C_MrTicked : C_Control {
    const char *name;
    explicit C_MrTicked(const char *n) : C_Control(nullptr, 0, nullptr), name(n) {}
    void Tick(int time) override { GfLog("%s tick %d", name, time); }
};

// A menu logging its hooks.
bool g_mrRendered;
struct C_MrMenu : C_MenuState {
    void OnMessage(S_ControlMessage *m) override { GfLog("menu message %x %x", m->id, m->value); }
    bool BuildScreens() override
    {
        GfLog("build screens");
        return true;
    }
    void OnStop() override { GfLog("on stop %d", static_cast<int>(g_menuScreens.size())); }
    bool OnStart(void *) override
    {
        GfLog("on start");
        return false;
    }
    bool OnInput(float *, float *) override
    {
        GfLog("on input");
        return false;
    }
    void OnUpdate(int t) override { GfLog("on update %d", t); }
    bool OnRender() override
    {
        GfLog("on render");
        return g_mrRendered;
    }
    bool OnDrawScreens() override
    {
        GfLog("on draw");
        return true;
    }
};

// The objects Start makes: the scene (its sector), the camera, the cursor's and the box's models,
// the light, the sound.
struct S_MrWorld {
    S_MrObj *scene, *sector, *camera, *light, *sound, *cursorModel, *boxModel;
};
S_MrWorld MrWorld(const char *tag)
{
    S_MrWorld w;
    const std::string t = tag;
    w.scene = MrObj("scene", g_mrSceneVt);
    w.sector = MrObj("sector", g_mrSectorVt);
    *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(w.scene) + 0x1F8) = w.sector;
    w.camera = MrObj("camera", g_mrCameraVt);
    w.light = MrObj("light", g_mrLightVt);
    w.sound = MrObj("sound", g_mrSoundVt);
    w.cursorModel = MrObj("cursormodel", g_mrModelVt);
    w.boxModel = MrObj("boxmodel", g_mrModelVt);
    g_mrMade.clear();
    g_mrMade[FRAME_SCENE] = {w.scene};
    g_mrMade[FRAME_CAMERA] = {w.camera};
    g_mrMade[FRAME_LIGHT] = {w.light};
    g_mrMade[FRAME_SOUND] = {w.sound};
    g_mrMade[FRAME_MODEL] = {w.cursorModel, w.boxModel};
    return w;
}
int *MrViewport(S_MrObj *scene) { return reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(scene) + 0x244); }

// The log from the first line starting with `from` (none: all).
std::vector<std::string> MrLogFrom(const std::string &from)
{
    auto it = std::find_if(g_gfLog.begin(), g_gfLog.end(), [&](const std::string &l) { return l.rfind(from, 0) == 0; });
    return std::vector<std::string>(it, g_gfLog.end());
}
bool MrHas(const std::string &line) { return std::find(g_gfLog.begin(), g_gfLog.end(), line) != g_gfLog.end(); }

} // namespace

std::string MrFrameName(const void *frame) { return MrName(frame); }

void MenuRunTest(I3D_scene *)
{
    S_MenuRunResults r;
    MrInitVtables();
    g_controlMessages.clear();
    g_menuScreens.clear();
    g_gfLog.clear();
    // The engine's slots this test needs, put back at the end.
    const int driverSlots[] = {0x54 / 4, 0x1C / 4, 0x18 / 4};
    const int igraphSlots[] = {0x18 / 4, 0x1C / 4, 0xA0 / 4, 0xA8 / 4, 0xB0 / 4, 0xBC / 4, 0xC0 / 4, 0xC4 / 4};
    void *oldDriver[3], *oldIgraph[8];
    for (int i = 0; i < 3; ++i)
        oldDriver[i] = g_driverVtable[driverSlots[i]];
    for (int i = 0; i < 8; ++i)
        oldIgraph[i] = g_igraphVtable[igraphSlots[i]];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
    g_driverVtable[0x1C / 4] = reinterpret_cast<void *>(&MrGetState);
    g_driverVtable[0x18 / 4] = reinterpret_cast<void *>(&MrSetState);
    g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&MrScrnX);
    g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&MrScrnY);
    g_igraphVtable[0xA0 / 4] = reinterpret_cast<void *>(&MrGetKey);
    g_igraphVtable[0xA8 / 4] = reinterpret_cast<void *>(&MrReadChar);
    g_igraphVtable[0xB0 / 4] = reinterpret_cast<void *>(&MrIsKeyDown);
    g_igraphVtable[0xBC / 4] = reinterpret_cast<void *>(&MrShift);
    g_igraphVtable[0xC0 / 4] = reinterpret_cast<void *>(&MrCtrl);
    g_igraphVtable[0xC4 / 4] = reinterpret_cast<void *>(&MrAlt);
    // The sound module: a module's vtable; its music's field found by its inline getter.
    C_MrSound soundVt;
    void *const oldSoundVt = *reinterpret_cast<void **>(g_soundModuleMemory);
    *reinterpret_cast<void **>(g_soundModuleMemory) = *reinterpret_cast<void **>(&soundVt);
    int musicOffset = -1;
    {
        alignas(16) uint8_t probe[sizeof g_soundModuleMemory] = {};
        for (int off = 4; off < 0x100 && musicOffset < 0; off += 4) {
            std::memset(probe, 0, sizeof probe);
            *reinterpret_cast<int *>(probe + off) = 12345;
            if (reinterpret_cast<C_SoundModule *>(probe)->GetBackgroundMusic() == 12345)
                musicOffset = off;
        }
    }
    int &music = *reinterpret_cast<int *>(g_soundModuleMemory + (musicOffset < 0 ? 0x100 : musicOffset));
    const int oldMusic = music;
    music = -1;
    C_PlayerProfile player;
    C_PlayerProfile *const oldPlayer = g_gameProfile.m_player;
    g_gameProfile.m_player = &player;
    const int oldRefs = g_csCursor.refs;
    C_Cursor *cursor = reinterpret_cast<C_Cursor *>(&g_csCursor);
    void (*const oldFill)(I3D_model *) = g_cacheFill;
    g_cacheFill = [](I3D_model *) {};
    g_msNamer = &MrFrameName;

    // --- started on a campaign screen: the scene (the whole screen, the callback), the camera,
    // the cursor's model, the message box, the menu's screens, the cursor, the light, the click
    // sound, the music, the diary's texts; running ---
    C_CampaignScreen &campaignScreen = *reinterpret_cast<C_CampaignScreen *>(g_mrScreenMemory);
    MrSetScreen(campaignScreen, 3, {7, 9});
    {
        C_MrMenu m;
        S_MrWorld w = MrWorld("a");
        MrViewport(w.scene)[2] = 640;
        g_mrStates = {{0x15, 1}, {0x19, 0}, {0x11, 1}};
        g_cacheLoads.clear();
        m.m_inputLock = 4;
        m.m_underPopup = reinterpret_cast<C_Screen *>(w.scene);
        g_gfLog.clear();
        const bool ok = m.Start(&campaignScreen);
        WM(r.mrStart, musicOffset >= 0 && ok && m.m_state == GAME_STATE_RUNNING && m.m_mode == 3 && m.m_startArg == &campaignScreen &&
                          m.m_scene == As<I3D_scene>(w.scene) && m.m_camera == As<I3D_camera>(w.camera) &&
                          m.m_light == As<I3D_light>(w.light) && m.m_clickSound == As<I3D_sound>(w.sound) &&
                          m.m_cursor == cursor && m.m_inputLock == -1 && !m.m_underPopup && g_mrCallback);
        WM(r.mrStart, m.m_models == std::vector<I3D_frame *>({As<I3D_frame>(w.cursorModel), As<I3D_frame>(w.boxModel)}) &&
                          m.m_screens.size() == 1 && m.m_messageBox && m.m_screens[0] == m.m_messageBox &&
                          m.m_messageBox->GetId() == 0 &&
                          m.m_messageBox->GetScreenFrame() == As<I3D_frame>(w.boxModel) &&
                          g_cacheLoads == std::vector<std::string>({"kurzor_menu", "confirm"}));
        const std::vector<std::string> expected = {
            "create 13", "scene callback 4", "scene 148 0 0 800 600", "create 3", "scene 90 camera", "scene 80 camera",
            "camera 34 ActivCamera", "camera 1c (0 0 1) 0", "camera 7c 0.2 20", "camera 74 1.5708", "camera 88 1",
            "camera 84 2", "camera 24", "on start", "state 15 0", "state 19 0", "state 11 1", "create 9",
            "cursormodel 34 CURZOR", "cursormodel 38 sector 0", "cursormodel on 0", "create 9", "boxmodel 34 CONFIRM",
            "boxmodel 38 sector 0", "boxmodel on 0", "boxmodel 8 (0.5 -0.375 -0.5)", "cursor acquire 1"};
        std::vector<std::string> head(g_gfLog.begin(), g_gfLog.begin() + std::min(g_gfLog.size(), expected.size()));
        WM(r.mrStart, GfVecIs(head, expected));
        const std::vector<std::string> tail = MrLogFrom("build screens");
        const std::vector<std::string> expectedTail = {
            "build screens", "state 15 1", "state 19 0", "state 11 1", "cursor acquire 1", "create 2", "light 74 4",
            "light 34 ambient", "light 84 (1 1 1)", "light 90 0.9", "scene 80 light", "sector 74 light", "light 0",
            "create 4", "sound 74 button.wav 10", "sound 7c 3", "sound 34 sound_click", "sound on 0", "sound 8c 1",
            "scene 80 sound", "sector 7c sound", "sound 0", "music 0 0", "diary 7", "diary 9"};
        WM(r.mrStart, GfVecIs(tail, expectedTail));
        const S_vector &pos = As<I3D_frame>(w.camera)->m_localMatrix(3);
        WM(r.mrStart, pos == S_vector(0.5f, -0.375f, -15.5f) &&
                          (As<I3D_frame>(w.camera)->m_frameFlags & (0x40000000 | 0x120)) == 0x40000000);
        m.Stop();
    }

    // --- the modes (the music, the diary only from a campaign screen); the whole screen kept;
    // no sound device: none opened; a locked camera place kept ---
    {
        const auto start = [&](void *arg, int track) {
            C_MrMenu m;
            S_MrWorld w = MrWorld("m");
            MrViewport(w.scene)[2] = 800;
            MrViewport(w.scene)[3] = 600;
            As<I3D_frame>(w.camera)->m_frameFlags |= 0x80;
            music = track;
            g_gfLog.clear();
            m.Start(arg);
            const int mode = m.m_mode;
            const bool viewport = MrHas("scene 148 0 0 800 600");
            const bool locked = As<I3D_frame>(w.camera)->m_localMatrix(3) == S_vector(0.0f, 0.0f, 0.0f);
            m.Stop();
            return CmText("%d %d %d", mode, viewport, locked);
        };
        WM(r.mrModes, start(nullptr, 5) == "10 0 1" && !MrHas("music 0 0") && !MrHas("diary 7"));
        WM(r.mrModes, start(reinterpret_cast<void *>(-1), -1) == "11 0 1" && !MrHas("music 0 0") && MrHas("state 11 0"));
        WM(r.mrModes, start(reinterpret_cast<void *>(-2), -1) == "11 0 1" && !MrHas("music 0 0"));
        MrSetScreen(campaignScreen, 5, {7, 9});
        WM(r.mrModes, start(&campaignScreen, -1) == "5 0 1" && !MrHas("music 0 0") && MrHas("diary 9"));
        MrSetScreen(campaignScreen, 6, {7, 9});
        WM(r.mrModes, start(&campaignScreen, -1) == "6 0 1" && !MrHas("music 0 0"));
        MrSetScreen(campaignScreen, 1, {7, 9});
        g_mrSoundOn = false;
        WM(r.mrModes, start(&campaignScreen, 3) == "1 0 1" && !MrHas("sound 74 button.wav 10") && MrHas("sound 7c 3"));
        g_mrSoundOn = true;
        {
            C_MrMenu m;
            S_MrWorld w = MrWorld("v");
            MrViewport(w.scene)[0] = 5;
            MrViewport(w.scene)[2] = 800;
            MrViewport(w.scene)[3] = 600;
            g_gfLog.clear();
            m.Start(nullptr);
            WM(r.mrModes, MrHas("scene 148 0 0 800 600"));
            m.Stop();
        }
        // The music: none playing: track 0; 37: crossfaded to 0 in 2 s; another kept.
        start(nullptr, -1);
        WM(r.mrMusic, MrHas("music 0 0") && !MrHas("crossfade 0 2000 0"));
        start(nullptr, 37);
        WM(r.mrMusic, MrHas("crossfade 0 2000 0") && !MrHas("music 0 0"));
        start(nullptr, 36);
        WM(r.mrMusic, !MrHas("crossfade 0 2000 0") && !MrHas("music 0 0"));
        start(reinterpret_cast<void *>(-1), 37);
        WM(r.mrMusic, !MrHas("crossfade 0 2000 0"));
        MrSetScreen(campaignScreen, 3, {7, 9});
    }

    // --- the screens: a model not loaded: none (released); the message box's not loaded: no box
    // (the screens still built) ---
    {
        C_MrMenu m;
        S_MrWorld w = MrWorld("s");
        g_cacheFailName = "confirm";
        g_gfLog.clear();
        m.Start(nullptr);
        WM(r.mrScreens, !m.m_messageBox && m.m_screens.empty() && MrHas("boxmodel 0") && MrHas("build screens") &&
                            m.m_models == std::vector<I3D_frame *>({As<I3D_frame>(w.cursorModel)}) &&
                            !MrHas("boxmodel 34 CONFIRM"));
        m.Stop();
        g_cacheFailName = "kurzor_menu";
        C_MrMenu n;
        MrWorld("t");
        g_gfLog.clear();
        n.Start(nullptr);
        WM(r.mrScreens, n.m_messageBox && n.m_models.size() == 1 && MrHas("cursormodel 0"));
        n.Stop();
        g_cacheFailName.clear();
        // The box made directly: true (its screen's id 0); its model not loaded: false. The screens: true.
        {
            C_MrMenu b;
            S_MrWorld v = MrWorld("b");
            b.m_scene = As<I3D_scene>(v.scene);
            g_mrMade[FRAME_MODEL] = {v.boxModel};
            WM(r.mrScreens, b.CreateMessageBox("BOX", "box") && b.m_messageBox && b.m_messageBox->GetId() == 0 &&
                                b.m_screens.size() == 1);
            g_cacheFailName = "box";
            g_mrMade[FRAME_MODEL] = {v.cursorModel};
            C_MessageBox *made = b.m_messageBox;
            WM(r.mrScreens, !b.CreateMessageBox("BOX", "box") && b.m_messageBox == made && b.m_screens.size() == 1);
            g_cacheFailName.clear();
            g_mrMade[FRAME_MODEL] = {v.cursorModel, v.boxModel};
            WM(r.mrScreens, b.LoadScreens());
            for (C_Screen *screen : b.m_screens)
                delete screen;
            b.m_screens.clear();
        }
        // The model loader directly: no name: not named.
        C_MrMenu o;
        S_MrWorld v = MrWorld("u");
        o.m_scene = As<I3D_scene>(v.scene);
        g_gfLog.clear();
        I3D_frame *model = o.LoadModel(nullptr, "file");
        WM(r.mrScreens, model == As<I3D_frame>(v.cursorModel) &&
                            GfLogIs({"create 9", "cursormodel 38 sector 0", "cursormodel on 0"}));
    }

    // --- stopped: OnStop, the stack emptied, the screens deleted, the frames' trees and the models
    // released (the last first), the cursor, the camera, the scene (its camera cleared, closed),
    // the queue; true ---
    {
        C_MrMenu m;
        S_MrWorld w = MrWorld("x");
        m.Start(nullptr);
        auto *s1 = new C_MrScreen(As<I3D_scene>(w.scene), "x1", 0x04000000);
        m.m_screens.push_back(s1);
        g_menuScreens = {s1};
        S_MrObj *f1 = MrObj("f1", g_mrModelVt), *f2 = MrObj("f2", g_mrModelVt);
        m.m_frames = {As<I3D_frame>(f1), As<I3D_frame>(f2)};
        C_MrTicked t("t1");
        m.m_tickedControls = {&t};
        PostControlMessage(MsMessage(0x2000006, 1));
        const int refs = g_csCursor.refs;
        g_gfLog.clear();
        const bool ok = m.Stop();
        WM(r.mrStop, ok && g_menuScreens.empty() && m.m_screens.empty() && m.m_frames.empty() && m.m_models.empty() &&
                         m.m_tickedControls.empty() && !m.m_cursor && !m.m_camera && !m.m_scene && !m.m_light &&
                         m.m_clickSound == As<I3D_sound>(w.sound) && g_controlMessages.empty() && g_csCursor.refs == refs - 3);
        WM(r.mrStop, GfLogIs({"on stop 1", "cursor release", "cursor release", "release tree f2", "release tree f1",
                              "boxmodel 0", "cursormodel 0", "cursor release", "camera 0", "scene 90 null", "scene f8",
                              "scene 0"}));
        // (Nothing made: nothing released.)
        C_MrMenu n;
        g_gfLog.clear();
        WM(r.mrStop, n.Stop() && GfLogIs({"on stop 0"}));
    }

    // --- the input: keyboard off: nothing; OnInput; no screen: nothing more; the typed character
    // to the control editing (taken: done) ---
    C_MrMenu menu;
    S_MrWorld world = MrWorld("i");
    menu.m_scene = As<I3D_scene>(world.scene);
    menu.m_cursor = cursor;
    C_MrScreen top(menu.m_scene, "top", 0x04000000), under(menu.m_scene, "under", 0x08000000),
        pop(menu.m_scene, "pop", 0x0C000000);
    float pressed[64] = {};
    {
        bool ok = true;
        menu.m_keyboardOff = true;
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && g_gfLog.empty();
        menu.m_keyboardOff = false;
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input"});
        g_menuScreens = {&under, &top};
        C_MrEdit edit(0x04300000);
        menu.m_editing = &edit;
        g_mrChars = {1};
        g_mrCharTaken = true;
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "char 1"});
        g_mrChars = {'a'};
        g_mrKey = 0x1C;
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "char 97"});
        g_mrCharTaken = false;
        g_mrChars = {0xE9};
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "char 233", "top key 1c"});
        menu.m_editing = nullptr;
        g_mrChars = {'b'};
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "top key 1c"}) && g_mrChars.empty();
        // The wide characters (the Unicode font): each to the control editing, one taken: done.
        g_gameProfile.m_unicodeFont = true;
        menu.m_editing = &edit;
        g_mrChars = {'x', 'y'};
        g_mrCharTaken = true;
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "char 120", "char 121"});
        g_mrCharTaken = false;
        g_mrChars = {'z'};
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "char 122", "top key 1c"});
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "top key 1c"});
        g_gameProfile.m_unicodeFont = false;
        menu.m_editing = nullptr;
        WM(r.mrChars, ok);
    }

    // --- the key to the top (its low byte); none: the held control / alt / shift key (left if
    // down), else the controls' escape; Ctrl + Alt + Shift + N posted ---
    {
        bool ok = true;
        const auto key = [&](int pressedKey) {
            g_mrKey = pressedKey;
            g_gfLog.clear();
            menu.HandleInput(nullptr, pressed);
            return g_gfLog.empty() ? std::string("-") : g_gfLog.back();
        };
        g_mrTake = true;
        ok &= key(0x139) == "top key 39";
        g_mrCtrl = true;
        ok &= key(0) == "top key 9d" && MrHas("isdown 1d");
        g_mrDown = {0x1D};
        ok &= key(0) == "top key 1d";
        g_mrCtrl = false;
        g_mrAlt = true;
        ok &= key(0) == "top key b8";
        g_mrDown = {0x38};
        ok &= key(0) == "top key 38";
        g_mrAlt = false;
        g_mrShift = true;
        ok &= key(0) == "top key 36";
        g_mrDown = {0x2A};
        ok &= key(0) == "top key 2a";
        g_mrShift = false;
        ok &= key(0) == "on input" && ScQueue().empty();
        pressed[46] = 1.0f;
        ok &= key(0) == "top key 1";
        pressed[46] = 0.0f;
        g_mrKey = 0;
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, nullptr) && GfLogIs({"on input"});
        WM(r.mrKeys, ok);
        // N: with all three: posted, not passed; else passed.
        g_mrShift = g_mrCtrl = g_mrAlt = true;
        ok &= key(0x4E) == "on input" && ScQueue() == "0 200007d 0 0 ; ";
        ok &= menu.HandleInput(nullptr, pressed) && ScQueue() == "0 200007d 0 0 ; ";
        g_mrShift = false;
        ok &= key(0x4E) == "top key 4e" && ScQueue().empty();
        g_mrShift = true;
        g_mrCtrl = false;
        ok &= key(0x4E) == "top key 4e";
        g_mrCtrl = true;
        g_mrAlt = false;
        ok &= key(0x4E) == "top key 4e";
        g_mrShift = g_mrCtrl = g_mrAlt = false;
        g_mrDown.clear();
        WM(r.mrKeys, ok && ScQueue().empty());
    }

    // --- Escape not taken by the top: the drag ended, MENU_MSG_ESCAPE with its id; taken, or
    // another key: nothing ---
    {
        bool ok = true;
        g_mrTake = false;
        g_mrKey = 1;
        menu.m_dragging = true;
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "top key 1", "top in09 0"}) && !menu.m_dragging &&
              ScQueue() == "0 200000a 4000000 0 ; ";
        g_gfLog.clear();
        ok &= menu.HandleInput(nullptr, pressed) && GfLogIs({"on input", "top key 1"}) && ScQueue() == "0 200000a 4000000 0 ; ";
        g_mrKey = 0x101;
        menu.HandleInput(nullptr, pressed);
        ok &= ScQueue() == "0 200000a 4000000 0 ; ";
        g_mrKey = 2;
        menu.HandleInput(nullptr, pressed);
        ok &= ScQueue().empty();
        g_mrTake = true;
        g_mrKey = 1;
        menu.HandleInput(nullptr, pressed);
        ok &= ScQueue().empty();
        g_mrKey = 0;
        WM(r.mrEscape, ok);
    }

    // --- the update: OnUpdate and the sound module's twice (the first not in the network game's
    // in-game menu), the timer, the queue; with a screen: the cursor's input, the ticks ---
    {
        C_MrEdit edit(0x04300000);
        C_MrTicked t1("t1"), t2("t2");
        menu.m_editing = &edit;
        menu.m_popup = &pop;
        menu.m_tickedControls = {&t1, &t2};
        menu.m_timing = true;
        menu.m_time = 5;
        menu.m_inputLock = 0;
        PostControlMessage(MsMessage(0x2000006, 3));
        g_gfLog.clear();
        menu.Update(40);
        WM(r.mrUpdate, menu.m_time == 45 &&
                           GfLogIs({"on update 40", "sound update 40", "on update 40", "sound update 40",
                                    "menu message 2000006 3", "edit tick 40", "top tick 40", "pop tick 40", "t2 tick 40",
                                    "t1 tick 40"}));
        menu.m_timing = false;
        menu.m_mode = MENU_MODE_GAME;
        g_gameRole = GAME_ROLE_SERVER;
        menu.m_editing = nullptr;
        menu.m_popup = nullptr;
        menu.m_tickedControls.clear();
        g_gfLog.clear();
        menu.Update(7);
        WM(r.mrUpdate, menu.m_time == 45 &&
                           GfLogIs({"on update 7", "on update 7", "sound update 7", "top tick 7"}));
        g_gameRole = GAME_ROLE_LOCAL;
        g_gfLog.clear();
        menu.Update(8);
        WM(r.mrUpdate, GfLogIs({"on update 8", "sound update 8", "on update 8", "sound update 8", "top tick 8"}));
        menu.m_mode = 0;
        g_menuScreens.clear();
        menu.m_tickedControls = {&t1};
        PostControlMessage(MsMessage(0x2000006, 4));
        g_gfLog.clear();
        menu.Update(9);
        WM(r.mrUpdate, GfLogIs({"on update 9", "sound update 9", "on update 9", "sound update 9", "menu message 2000006 4"}));
        menu.m_tickedControls.clear();
        g_menuScreens = {&under, &top};
        // (The cursor's input with the lock open: its update.)
        menu.m_inputLock = -1;
        g_gfLog.clear();
        menu.Update(2);
        WM(r.mrUpdate, MrHas("cursor update 2"));
        menu.m_inputLock = 0;
    }

    // --- drawn: OnRender's done: the cursor only; the popup's under screen first (driver state 5
    // cleared, restored); the popup's own popup; else the scene once; the cursor ---
    S_MrObj *topFrame = reinterpret_cast<S_MrObj *>(top.GetScreenFrame());
    (void)topFrame;
    {
        bool ok = true;
        g_mrRendered = true;
        g_gfLog.clear();
        menu.Render();
        ok &= GfLogIs({"on render", "cursor draw"});
        g_mrRendered = false;
        g_gfLog.clear();
        menu.Render();
        ok &= GfLogIs({"on render", "scene 78 0", "cursor draw"});
        menu.m_underPopup = &under;
        g_mrStates[5] = 3;
        g_gfLog.clear();
        menu.Render();
        ok &= GfLogIs({"on render", "top on 0", "under on 1", "scene 78 0", "state 5 0", "top on 1", "under on 0",
                       "scene 78 0", "state 5 3", "under on 1", "cursor draw"});
        // (No top: the scene once.)
        g_menuScreens.clear();
        g_gfLog.clear();
        menu.Render();
        ok &= GfLogIs({"on render", "scene 78 0", "cursor draw"});
        g_menuScreens = {&under, &top};
        menu.m_underPopup = nullptr;
        menu.m_popup = &pop;
        g_gfLog.clear();
        menu.Render();
        ok &= GfLogIs({"on render", "scene 78 0", "cursor draw"});
        pop.popup = true;
        g_gfLog.clear();
        menu.Render();
        ok &= GfLogIs({"on render", "top on 1", "pop on 0", "scene 78 0", "state 5 0", "top on 0", "pop on 1",
                       "scene 78 0", "state 5 3", "top on 1", "pop on 0", "cursor draw"});
        // (No scene: the cursor.)
        I3D_scene *scene = menu.m_scene;
        menu.m_scene = nullptr;
        g_gfLog.clear();
        menu.Render();
        ok &= GfLogIs({"on render", "cursor draw"});
        menu.m_scene = scene;
        pop.popup = false;
        menu.m_popup = nullptr;
        WM(r.mrRender, ok);
    }

    // --- the screens drawn: the one under the popup and the top (when on), the menu's, the popup's
    // own popup (none under), the hint ---
    {
        bool ok = true;
        const auto on = [](C_Screen &s, bool v) {
            I3D_frame *f = s.GetScreenFrame();
            f->m_frameFlags = v ? f->m_frameFlags | 1 : f->m_frameFlags & ~1u;
        };
        on(top, true);
        on(under, true);
        g_gfLog.clear();
        menu.DrawScreens();
        ok &= GfLogIs({"top refresh", "on draw"});
        menu.m_underPopup = &under;
        g_gfLog.clear();
        menu.DrawScreens();
        ok &= GfLogIs({"under refresh", "top refresh", "on draw"});
        on(under, false);
        on(top, false);
        g_gfLog.clear();
        menu.DrawScreens();
        ok &= GfLogIs({"on draw"});
        menu.m_popup = &pop;
        pop.popup = true;
        g_gfLog.clear();
        menu.DrawScreens();
        ok &= GfLogIs({"on draw"});
        menu.m_underPopup = nullptr;
        g_gfLog.clear();
        menu.DrawScreens();
        ok &= GfLogIs({"on draw", "pop refresh"});
        pop.popup = false;
        g_gfLog.clear();
        menu.DrawScreens();
        ok &= GfLogIs({"on draw"});
        menu.m_popup = nullptr;
        g_menuScreens.clear();
        on(top, true);
        g_gfLog.clear();
        menu.DrawScreens();
        ok &= GfLogIs({"on draw"});
        menu.m_hint.m_on = true;
        menu.m_hint.m_hasText = true;
        menu.m_hint.m_frame = CsVisual("mrhint");
        menu.m_hint.m_box.Clear();
        menu.m_hint.m_box.m_placed = false;
        menu.DrawScreens();
        ok &= menu.m_hint.m_box.m_placed && menu.m_hint.m_box.m_depth == -1.0f;
        menu.m_hint.Reset();
        g_menuScreens = {&under, &top};
        WM(r.mrDraw, ok);
    }

    // --- the scene's callback: the screens of the menu state made last, in phase 2 only ---
    {
        bool ok = g_mrCallback != nullptr;
        auto *callback = reinterpret_cast<int(__stdcall *)(int, int, int)>(g_mrCallback);
        C_MenuState *const old = g_menuState;
        g_menuState = &menu;
        g_gfLog.clear();
        ok &= callback(1, 0, 0) == 0 && callback(3, 2, 2) == 0 && g_gfLog.empty();
        ok &= callback(2, 0, 0) == 0 && GfLogIs({"top refresh", "on draw"});
        g_menuState = old;
        WM(r.mrCallback, ok);
    }

    // --- the cursor's input (the lock -1 only; changed or another id): a press (the editing ended
    // away from its control; refused: nothing), a release (the drag ended), a double click, a click
    // (the editing's control only; handled: the click sound), a move ---
    menu.m_inputLock = -1;
    const auto input = [&](uint32_t id) {
        g_msPicked = id;
        g_gfLog.clear();
        menu.CursorInput(16);
    };
    {
        bool ok = true;
        menu.m_inputLock = 0;
        input(0x04100000);
        ok &= g_gfLog.empty();
        menu.m_inputLock = -1;
        menu.m_hoveredId = 0x04100000;
        g_mrCursorChanged = false;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16"}) && menu.m_hoveredId == 0x04100000;
        input(0x04200000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16"}) && menu.m_hoveredId == 0x04200000;
        g_mrCursorChanged = true;
        input(0x04200000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16"});
        g_mrPressed = true;
        input(0x04200000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top press 4200000"});
        C_MrEdit edit(0x04300000);
        menu.m_editing = &edit;
        g_mrStopEdit = 0;
        input(0x04200000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "edit stop 4200000"}) && menu.m_editing == &edit;
        g_mrStopEdit = 7;
        input(0x04200000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "edit stop 4200000"}) && menu.m_editing == &edit;
        g_mrStopEdit = 1;
        input(0x04200000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "edit stop 4200000", "top press 4200000"}) && !menu.m_editing;
        g_mrReleased = true;
        input(0x04200000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top press 4200000"});
        g_mrReleased = false;
        menu.m_editing = &edit;
        input(0x04300000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top press 4300000"}) && menu.m_editing == &edit;
        g_mrPressed = false;
        menu.m_editing = nullptr;
        WM(r.mrCursor, ok);
        g_mrReleased = true;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top release 4100000"});
        menu.m_dragging = true;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top in09 4100000", "top in05 4100000", "top release 4100000"}) &&
              !menu.m_dragging;
        g_mrReleased = false;
        cursor->m_doubleClicked = true;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top in13 4100000"});
        cursor->m_doubleClicked = false;
        cursor->m_clicked = true;
        menu.m_clickSound = As<I3D_sound>(world.sound);
        g_mrRet[{world.sound, 0x78}] = 0;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top click 4100000", "sound 78 0", "sound on 1"});
        g_mrRet[{world.sound, 0x78}] = 1;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top click 4100000", "sound 78 0"});
        g_mrClick = 7;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top click 4100000"});
        g_mrClick = 1;
        menu.m_editing = &edit;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16"});
        input(0x04300000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top click 4300000", "sound 78 0"});
        menu.m_editing = nullptr;
        menu.m_clickSound = nullptr;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top click 4100000"});
        cursor->m_clicked = false;
        // The keyboard off: a move only (the drag ended).
        menu.m_keyboardOff = true;
        g_mrPressed = true;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16"});
        g_mrMoved = true;
        menu.m_dragging = true;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top in05 4100000"}) && !menu.m_dragging;
        g_mrPressed = false;
        g_mrMoved = false;
        menu.m_keyboardOff = false;
        WM(r.mrCursor, ok);
    }

    // --- a move: OnInput05 (not while dragging); with a button held: the drag's start (once),
    // each move; the wheel then not ---
    {
        bool ok = true;
        g_mrMoved = true;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top in05 4100000"});
        g_csCursor.buttons = 1;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top in05 4100000", "top in0a 4100000", "top in08 4100000"}) &&
              menu.m_dragging;
        cursor->m_wheel = 2.0f;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top in08 4100000"}) && menu.m_dragging;
        g_csCursor.buttons = 0;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top wheel 4100000 2", "pick 1 top", "top in05 4100000"});
        menu.m_dragging = false;
        cursor->m_wheel = 0.0f;
        g_mrMoved = false;
        WM(r.mrDrag, ok);
    }

    // --- the wheel: its turn (truncated) to the top, then the id under the cursor again ---
    {
        bool ok = true;
        cursor->m_wheel = -2.75f;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16", "top wheel 4100000 -2", "pick 1 top", "top in05 4100000"});
        cursor->m_wheel = 1.5f;
        menu.m_keyboardOff = true;
        input(0x04100000);
        ok &= GfLogIs({"pick 1 top", "cursor update 16"});
        menu.m_keyboardOff = false;
        cursor->m_wheel = 0.0f;
        WM(r.mrWheel, ok);
    }

    // --- the frames: the one picked (a ray from the cursor's place + 0.1 along +z on the top);
    // a visual's copy (its own mesh) and a frame tree's, named, linked, kept; a new frame placed ---
    {
        bool ok = true;
        g_csCursor.position = S_vector(0.25f, -0.5f, -1.0f);
        S_MrObj *hit = MrObj("hit", g_mrModelVt);
        g_mrRet[{world.scene, 0x104}] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hit));
        g_gfLog.clear();
        // (The collision information: a local of its, "?".)
        ok &= menu.PickFrame() == As<I3D_frame>(hit) && GfLogIs({"scene 104 (0.25 -0.5 -0.9) (0 0 1) top ? 140000 0 0 0"});
        S_MrObj *mesh = MrObj("mesh", g_mrModelVt);
        mesh->vtable[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
        S_MrObj *copy = MrObj("copy", g_mrModelVt);
        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(copy) + 0x1E0) = mesh;
        g_mrClone = As<I3D_frame>(copy);
        S_MrObj *source = MrObj("source", g_mrModelVt), *parent = MrObj("parent", g_mrModelVt);
        menu.m_frames.clear();
        g_gfLog.clear();
        ok &= menu.CloneVisual(C_str("vis"), As<I3D_frame>(parent), As<I3D_frame>(source)) == As<I3D_frame>(copy) &&
              GfLogIs({"clone tree source 0", "copy a0", "mesh 18", "copy 34 vis", "copy 38 parent 0"}) &&
              menu.m_frames == std::vector<I3D_frame *>({As<I3D_frame>(copy)});
        S_MrObj *copy2 = MrObj("copy2", g_mrModelVt);
        g_mrClone = As<I3D_frame>(copy2);
        g_gfLog.clear();
        ok &= menu.CloneFrame(C_str(), As<I3D_frame>(parent), As<I3D_frame>(source)) == As<I3D_frame>(copy2) &&
              GfLogIs({"clone tree source 0", "copy2 34 ", "copy2 38 parent 0"}) &&
              menu.m_frames == std::vector<I3D_frame *>({As<I3D_frame>(copy), As<I3D_frame>(copy2)});
        S_MrObj *made = MrObj("made", g_mrModelVt);
        g_mrMade[FRAME_DUMMY] = {made};
        menu.m_models.clear();
        g_gfLog.clear();
        ok &= menu.AddFrame(C_str("new"), FRAME_DUMMY, As<I3D_frame>(parent), S_vector(1.0f, 2.0f, 3.0f)) == As<I3D_frame>(made) &&
              GfLogIs({"create 6", "made 38 parent 0", "made 34 new", "made 8 (1 2 3)"}) &&
              menu.m_models == std::vector<I3D_frame *>({As<I3D_frame>(made)});
        menu.m_frames.clear();
        menu.m_models.clear();
        WM(r.mrFrames, ok);
    }

    // --- the models released (the frames' trees, then the models, the last first); the click
    // sound played when not playing ---
    {
        bool ok = true;
        S_MrObj *a = MrObj("ma", g_mrModelVt), *b = MrObj("mb", g_mrModelVt), *c = MrObj("fc", g_mrModelVt);
        menu.m_models = {As<I3D_frame>(a), As<I3D_frame>(b)};
        menu.m_frames = {As<I3D_frame>(c)};
        g_gfLog.clear();
        menu.ReleaseModels();
        ok &= menu.m_models.empty() && menu.m_frames.empty() && GfLogIs({"release tree fc", "mb 0", "ma 0"});
        menu.m_clickSound = nullptr;
        g_gfLog.clear();
        menu.PlayClick();
        ok &= g_gfLog.empty();
        menu.m_clickSound = As<I3D_sound>(world.sound);
        g_mrRet[{world.sound, 0x78}] = 0;
        menu.PlayClick();
        ok &= GfLogIs({"sound 78 0", "sound on 1"});
        menu.m_clickSound = nullptr;
        WM(r.mrModels, ok);
    }

    // --- the typed characters as wide ones: until the first 0 (more than one read: true), at most
    // 0x3FF ---
    {
        bool ok = true;
        wchar_t out[8] = {1, 1, 1};
        g_mrChars = {'h', 'i', 0, 'x'};
        ok &= ReadTypedChars(out, 8) && out[0] == L'h' && out[1] == L'i' && out[2] == 0 && g_mrChars.size() == 1;
        g_mrChars.clear();
        out[0] = 7;
        ok &= !ReadTypedChars(out, 8) && out[0] == 0;
        g_mrChars = {'q'};
        ok &= ReadTypedChars(out, 8) && out[0] == L'q' && out[1] == 0;
        std::vector<wchar_t> big(0x400, 1);
        g_mrChars.assign(0x500, 'k');
        ok &= ReadTypedChars(big.data(), 0x400) && g_mrChars.size() == 0x500 - 0x3FF && big[0x3FE] == L'k' && big[0x3FF] == 0;
        g_mrChars.clear();
        WM(r.mrTyped, ok);
    }

    g_menuScreens.clear();
    menu.m_scene = nullptr;
    menu.m_cursor = nullptr;
    g_controlMessages.clear();
    for (int i = 0; i < 3; ++i)
        g_driverVtable[driverSlots[i]] = oldDriver[i];
    for (int i = 0; i < 8; ++i)
        g_igraphVtable[igraphSlots[i]] = oldIgraph[i];
    *reinterpret_cast<void **>(g_soundModuleMemory) = oldSoundVt;
    music = oldMusic;
    g_gameProfile.m_player = oldPlayer;
    g_cacheFill = oldFill;
    const_cast<std::vector<int> &>(campaignScreen.GetDiaryTexts()) = std::vector<int>();
    g_msNamer = nullptr;
    g_csCursor.refs = oldRefs + (g_csCursor.refs - oldRefs);
    g_csCursor.position = S_vector();
    g_gfLog.clear();
    std::printf("menurun: mrStart %d mrModes %d mrMusic %d mrScreens %d mrStop %d mrChars %d mrKeys %d mrEscape %d "
                "mrUpdate %d mrRender %d mrDraw %d mrCallback %d mrCursor %d mrDrag %d mrWheel %d mrFrames %d mrModels %d "
                "mrTyped %d\n",
                r.mrStart, r.mrModes, r.mrMusic, r.mrScreens, r.mrStop, r.mrChars, r.mrKeys, r.mrEscape, r.mrUpdate,
                r.mrRender, r.mrDraw, r.mrCallback, r.mrCursor, r.mrDrag, r.mrWheel, r.mrFrames, r.mrModels, r.mrTyped);
}
