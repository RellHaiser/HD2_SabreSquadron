// Part 8f-i's loading screen (game/loader_screen.cpp), in the camera build after
// placement_test.inc: the text boxes, the dedicated server's progress, the game type's text, the
// missions' accessors and the driver's, IGraph's, the scene's, the camera's and the light's slots
// faked (logged); the effects module's Update through a copy of its vtable. Prints "loader:" with
// 1 per check met.

#define private public
#include "game/campaign.h"
#include "game/loader_screen.h"
#include "game/text_box.h"
#undef private

extern bool g_textIds;                                  // (car_stubs.cpp: every text "text <id>")

// The text boxes: logged by their visual's name ("?" before AttachTo).
std::map<const C_TextBox *, std::string> g_lsBoxNames;
int g_lsBoxesLive;
const char *LsBox(const C_TextBox *box)
{
    const auto it = g_lsBoxNames.find(box);
    return it == g_lsBoxNames.end() ? "?" : it->second.c_str();
}
C_TextBox::C_TextBox()
{
    std::memset(m_opaque, 0, sizeof m_opaque);
    m_opaque[0x90] = 10;
    ++g_lsBoxesLive;
    GfLog("tb new");
}
C_TextBox::~C_TextBox()
{
    GfLog("tb %s delete", LsBox(this));
    g_lsBoxNames.erase(this);
    --g_lsBoxesLive;
}
bool C_TextBox::AddText(const C_str &text, uint8_t style)
{
    GfLog("tb %s add '%s' %d", LsBox(this), text.c_str(), style);
    return true;
}
bool C_TextBox::AddTextId(int textId, uint8_t style)
{
    GfLog("tb %s addid %d %d", LsBox(this), textId, style);
    return true;
}
bool C_TextBox::SetText(const C_str &text, uint8_t style, int index)
{
    GfLog("tb %s set '%s' %d %d", LsBox(this), text.c_str(), style, index);
    return true;
}
bool C_TextBox::SetTextId(int textId, int style, int index)
{
    GfLog("tb %s setid %d %d %d", LsBox(this), textId, style, index);
    return true;
}
void C_TextBox::Clear() { GfLog("tb %s clear", LsBox(this)); }
void C_TextBox::AttachTo(I3D_scene *, I3D_frame *visual)
{
    g_lsBoxNames[this] = GfName(visual);
    GfLog("tb attach %s", GfName(visual));
}
void C_TextBox::Render() { GfLog("tb %s render", LsBox(this)); }
void C_NetGame::PrintLoadProgress(int percent) { GfLog("netprogress %d", percent); }
int GetGameTypeTextId(int gameType) { return 7000 + gameType; }
// (Out of line in game/campaign.cpp.)
S_MissionObjective *C_MissionDef::GetObjective(int index) const
{
    return index >= 0 && index < GetObjectiveCount() ? m_objectives[index] : nullptr;
}
S_MpObjective *C_MpMissionDef::GetMpObjective(int index) const
{
    return static_cast<S_MpObjective *>(GetObjective(index));
}

namespace {

struct S_LoaderResults {
    bool lsQuiet = true, lsOpenFail = true, lsScene = true, lsTexts = true, lsMpTexts = true, lsProgress = true;
    bool lsDraw = true, lsClose = true, lsWait = true, lsPlayers = true;
};

// The scene the driver makes (its own vtable slots logged), the primary sector, the states.
alignas(16) uint8_t g_lsSceneMem[0x300];
I3D_scene *LsScene() { return reinterpret_cast<I3D_scene *>(g_lsSceneMem); }
HRESULT g_lsOpenResult;
std::map<int, uint32_t> g_lsStates;
std::deque<int> g_lsKeys;                               // GetAsciiKey's (then 0x39)
uint32_t __stdcall F_LsSceneRelease(void *self)
{
    GfLog(self == g_lsSceneMem ? "scene release" : "scene release other");
    return 0;
}
HRESULT __stdcall F_LsOpen(void *, const char *name, uint32_t flags, void *callback, void *context)
{
    GfLog("open %s %u %d %d", name, flags, callback != nullptr, context != nullptr);
    return g_lsOpenResult;
}
void __stdcall F_LsViewport(void *, int left, int top, int right, int bottom)
{
    GfLog("viewport %d %d %d %d", left, top, right, bottom);
}
void __stdcall F_LsRender(void *, uint32_t flags) { GfLog("render %u", flags); }
// The scene's FindFrame: g_plScene's frames (logged when found).
I3D_frame *__stdcall F_LsFind(void *, const char *name, uint32_t mask)
{
    const auto it = g_plScene.find(name);
    if (it == g_plScene.end())
        return nullptr;
    GfLog("sfind %s %x", name, mask);
    return it->second;
}
uint32_t __stdcall F_LsGetState(void *, int state)
{
    GfLog("getstate %x", state);
    return g_lsStates[state];
}
void __stdcall F_LsSetState(void *, int state, uint32_t value) { GfLog("setstate %x %u", state, value); }
void __stdcall F_LsWinMessages(void *) { GfLog("winmsg"); }
int __stdcall F_LsScrnX(void *) { return 640; }
int __stdcall F_LsScrnY(void *) { return 480; }
void __stdcall F_LsClear(void *, uint32_t color, float z, uint32_t flags) { GfLog("clear %u %.2f %u", color, z, flags); }
bool __stdcall F_LsBegin(void *)
{
    GfLog("begin");
    return true;
}
void __stdcall F_LsEnd(void *) { GfLog("end"); }
bool __stdcall F_LsFlip(void *)
{
    GfLog("flip");
    return true;
}
int g_lsRenderState = 7;
void __stdcall F_LsSetRenderState(void *, int state, int value)
{
    GfLog("rstate %d %d", state, value);
    if (state == 0)
        g_lsRenderState = value;
}
int __stdcall F_LsGetRenderState(void *, int state)
{
    GfLog("getrstate %d", state);
    return state == 0 ? g_lsRenderState : 0;
}
void __stdcall F_LsReadKeyboard(void *) { GfLog("readkb"); }
int __stdcall F_LsAsciiKey(void *)
{
    if (g_lsKeys.empty())
        return 0x39;
    const int key = g_lsKeys.front();
    g_lsKeys.pop_front();
    return key;
}
void __stdcall F_LsOrthoScale(I3D_frame *self, float scale) { GfLog("orthoscale %s %.2f", GfName(self), scale); }
void __stdcall F_LsOrtho(I3D_frame *self, bool on) { GfLog("ortho %s %d", GfName(self), on); }
void __stdcall F_LsLightType(I3D_frame *self, int type) { GfLog("lighttype %s %d", GfName(self), type); }
void __stdcall F_LsU84(I3D_frame *self, const S_vector &v) { GfLog("u84 %s %.2f %.2f %.2f", GfName(self), v.x, v.y, v.z); }
void __stdcall F_LsU90(I3D_frame *self, float v) { GfLog("u90 %s %.2f", GfName(self), v); }
void __stdcall F_LsAddLight(I3D_frame *self, I3D_frame *light) { GfLog("addlight %s %s", GfName(self), GfName(light)); }
// The effects module's Update (its vtable copied, the slot replaced).
void __attribute__((thiscall)) F_LsGfUpdate(void *, int timeDelta) { GfLog("gfupdate %d", timeDelta); }

// A mission (raw: its constructor is in game/campaign.cpp): the menus' texts, the map's
// name, its briefing, objectives with their sides' texts.
alignas(16) uint8_t g_lsMissionMem[sizeof(C_MpMissionDef)];
S_MpObjective *g_lsObjectives[2];
C_MpMissionDef *LsMission(int objectives)
{
    static bool made = false;
    auto *m = reinterpret_cast<C_MpMissionDef *>(g_lsMissionMem);
    if (!made) {
        new (&m->m_objectives) std::vector<S_MissionObjective *>();
        new (&m->m_mapName) C_str();
        for (int i = 0; i < 2; ++i) {
            alignas(16) static uint8_t mem[2][sizeof(S_MpObjective)];
            g_lsObjectives[i] = reinterpret_cast<S_MpObjective *>(mem[i]);
            g_lsObjectives[i]->alliedText = 21 + i;
            g_lsObjectives[i]->axisText = 31 + i;
        }
        made = true;
    }
    m->m_texts[0] = 11;
    m->m_texts[1] = 12;
    m->m_briefingText = 13;
    m->m_mapName = C_str("Map Name");
    m->m_objectives.clear();
    for (int i = 0; i < objectives; ++i)
        m->m_objectives.push_back(g_lsObjectives[i]);
    return m;
}

// The loader scene holds these visuals (the others missing).
void LsVisuals(std::initializer_list<const char *> names)
{
    g_plScene.clear();
    for (const char *name : names)
        g_plScene[name] = GfFrame(FRAME_VISUAL, name);
    g_plScene["loadbar"] = GfFrame(FRAME_VISUAL, "loadbar");
}
// The driver's next frames: the scene, the camera, the light.
I3D_frame *g_lsCamera, *g_lsLight;
void LsQueueFrames()
{
    g_cmCreate.clear();
    g_cmCreate.push_back(reinterpret_cast<I3D_frame *>(g_lsSceneMem));
    g_lsCamera = CmCamera("lscam", S_vector(0.0f, 0.0f, 0.0f));
    g_cmCreate.push_back(g_lsCamera);
    g_lsLight = GfFrame(FRAME_LIGHT, "light");
    g_cmCreate.push_back(g_lsLight);
}
// The draw's log with the boxes of `boxes` (in the screen's order: the last first).
void LsDrawLog(std::vector<std::string> &out, const std::vector<std::string> &boxes, int oldState)
{
    for (const char *l : {"winmsg", "render 0", "getrstate 0", "rstate 0 1", "begin"})
        out.push_back(l);
    for (const std::string &b : boxes)
        out.push_back("tb " + b + " render");
    out.push_back("end");
    out.push_back(CmText("rstate 0 %d", oldState));
    out.push_back("flip");
    out.push_back("clear 0 1.00 3");
}
// The setup's log up to the text boxes.
std::vector<std::string> LsSetupLog(const char *name, bool viewport)
{
    std::vector<std::string> l = {"create 13"};
    if (viewport)
        l.push_back("viewport 0 0 640 480");
    for (const char *s : {"getstate 15", "getstate 19", "getstate 11", "setstate 15 0", "setstate 19 1", "setstate 11 1"})
        l.push_back(s);
    l.push_back(CmText("open missions\\%s\\loader 0 0 0", name));
    for (const char *s : {"setstate 15 1", "setstate 19 0", "setstate 11 1", "create 3", "active lscam",
                          "worlddir lscam 0.00000 0.00000 1.00000 0.000000", "range lscam 0.2 500.0", "ortho lscam 1",
                          "orthoscale lscam 2.00", "update lscam", "create 2", "name light C_LoaderScreen", "lighttype light 4",
                          "u84 light 1.00 1.00 1.00", "u90 light 1.00", "mode light 11", "addlight primary light"})
        l.push_back(s);
    return l;
}
// A box's start: found, hidden, made, attached, cleared.
void LsBoxLog(std::vector<std::string> &l, const std::string &visual)
{
    l.push_back("sfind " + visual + " ffff");
    l.push_back("on " + visual + " 0");
    l.push_back("tb new");
    l.push_back("tb attach " + visual);
    l.push_back("tb " + visual + " clear");
}

// Part 8f-ii's test (mission_test.inc), run inside these fakes.
void MissionLoaderTest();

void LoaderTest(S_CameraResults &)
{
    S_LoaderResults r;
    C_GEffectsModule *effects = GetGEffects();
    void **const oldEffectsVt = *reinterpret_cast<void ***>(effects);
    static void *effectsVt[40];
    std::memcpy(effectsVt, oldEffectsVt, sizeof effectsVt);
    effectsVt[0x14 / 4] = reinterpret_cast<void *>(&F_LsGfUpdate);   // (Update: g++ has two destructor slots)
    *reinterpret_cast<void ***>(effects) = effectsVt;
    std::vector<std::pair<void **, int>> saved;
    const auto set = [&saved](void **vt, int offset, void *f) {
        saved.push_back({vt, offset});
        saved.push_back({reinterpret_cast<void **>(vt[offset / 4]), -1});
        vt[offset / 4] = f;
    };
    set(g_sceneVtable, 0x00, reinterpret_cast<void *>(&F_LsSceneRelease));
    set(g_sceneVtable, 0xF4, reinterpret_cast<void *>(&F_LsOpen));
    set(g_sceneVtable, 0x148, reinterpret_cast<void *>(&F_LsViewport));
    set(g_sceneVtable, 0x78, reinterpret_cast<void *>(&F_LsRender));
    set(g_sceneVtable, 0x7C, reinterpret_cast<void *>(&F_LsFind));
    set(g_driverVtable, 0x18, reinterpret_cast<void *>(&F_LsSetState));
    set(g_driverVtable, 0x1C, reinterpret_cast<void *>(&F_LsGetState));
    set(g_igraphVtable, 0x14, reinterpret_cast<void *>(&F_LsWinMessages));
    set(g_igraphVtable, 0x18, reinterpret_cast<void *>(&F_LsScrnX));
    set(g_igraphVtable, 0x1C, reinterpret_cast<void *>(&F_LsScrnY));
    set(g_igraphVtable, 0x64, reinterpret_cast<void *>(&F_LsClear));
    set(g_igraphVtable, 0x68, reinterpret_cast<void *>(&F_LsBegin));
    set(g_igraphVtable, 0x6C, reinterpret_cast<void *>(&F_LsEnd));
    set(g_igraphVtable, 0x70, reinterpret_cast<void *>(&F_LsFlip));
    set(g_igraphVtable, 0x74, reinterpret_cast<void *>(&F_LsSetRenderState));
    set(g_igraphVtable, 0x78, reinterpret_cast<void *>(&F_LsGetRenderState));
    set(g_igraphVtable, 0x9C, reinterpret_cast<void *>(&F_LsReadKeyboard));
    set(g_igraphVtable, 0xA0, reinterpret_cast<void *>(&F_LsAsciiKey));
    set(g_gfCameraVt, 0x1C, reinterpret_cast<void *>(&F_UpWorldDir));
    set(g_gfCameraVt, 0x7C, reinterpret_cast<void *>(&F_GfRange));
    set(g_gfCameraVt, 0x84, reinterpret_cast<void *>(&F_LsOrthoScale));
    set(g_gfCameraVt, 0x88, reinterpret_cast<void *>(&F_LsOrtho));
    set(g_gfLightVt, 0x34, reinterpret_cast<void *>(&F_CmSetName));
    set(g_gfLightVt, 0x74, reinterpret_cast<void *>(&F_LsLightType));
    set(g_gfLightVt, 0x84, reinterpret_cast<void *>(&F_LsU84));
    set(g_gfLightVt, 0x90, reinterpret_cast<void *>(&F_LsU90));
    set(g_gfLightVt, 0x94, reinterpret_cast<void *>(&F_GfMode));
    set(g_gfSectorVt, 0x74, reinterpret_cast<void *>(&F_LsAddLight));
    *reinterpret_cast<void ***>(g_lsSceneMem) = g_sceneVtable;
    I3D_frame *primary = GfFrame(FRAME_SECTOR, "primary");
    WmField<I3D_frame *>(g_lsSceneMem, 0x1F8) = primary;
    g_lsStates = {{0x15, 1}, {0x19, 0}, {0x11, 1}};
    const bool oldTextIds = g_textIds;
    g_textIds = true;
    S_MultiplayerSettings *const oldSettings = g_mpSettings;
    static S_MultiplayerSettings settings;
    g_mpSettings = nullptr;
    const bool oldCoop = g_coop;
    g_coop = false;
    C_LoaderScreen *ls = new C_LoaderScreen;

    // --- not shown: counted only (a dedicated server prints it) ---
    {
        ls->AddPlayer("p", 1);
        g_gfLog.clear();
        g_cmCreate.clear();
        ls->Start("m1", false, false, true, nullptr);
        WM(r.lsQuiet, ls->m_shown && !ls->m_scene && ls->m_progress == 0.0f && ls->m_twoParts && !ls->m_waitForKey &&
                          ls->m_players[0].empty() && g_gfLog.empty());
        ls->SetProgress(LOADER_STAGE_SCENE2, 0.5f);
        WM(r.lsQuiet, ls->m_progress == 0.25f * 0.5f + 0.15f && GfLogIs({"winmsg"}));
        g_gfLog.clear();
        g_mpSettings = &settings;
        settings.dedicated = true;
        ls->SetProgress(LOADER_STAGE_ACTORS, 1.0f);
        WM(r.lsQuiet, GfLogIs({"winmsg", CmText("netprogress %d", static_cast<int>((0.05f * 1.0f + 0.4f) * 100.0f))}));
        settings.dedicated = false;
        g_gfLog.clear();
        ls->SetProgress(LOADER_STAGE_ACTORS, 1.0f);
        WM(r.lsQuiet, g_gfLog.empty());
        // Finish: only a two-part screen; FinishLoading: only a one-part one.
        ls->FinishLoading();
        WM(r.lsQuiet, ls->m_shown);
        ls->Finish();
        WM(r.lsQuiet, !ls->m_shown && ls->m_progress == 0.0f && GfLogIs({"winmsg"}));
        ls->Start("m1", false, true, false, nullptr);
        ls->Finish();
        WM(r.lsQuiet, ls->m_shown);
        g_gfLog.clear();
        ls->FinishLoading();
        WM(r.lsQuiet, !ls->m_shown && GfLogIs({"winmsg"}));      // (no scene: no wait)
        // Closed: progress ignored (and the close leaves 1).
        ls->SetProgress(LOADER_STAGE_SCENE, 1.0f);
        ls->Finish();
        ls->FinishLoading();
        WM(r.lsQuiet, ls->m_progress == 1.0f);           // (a closed screen's close leaves 1)
        g_mpSettings = nullptr;
    }

    // --- the scene does not open: let go, not started ---
    {
        LsVisuals({});
        LsQueueFrames();
        g_lsOpenResult = -5;
        ls->AddPlayer("p", 1);
        WmField<int>(g_lsSceneMem, 0x244) = 0;
        WmField<int>(g_lsSceneMem, 0x248) = 0;
        WmField<int>(g_lsSceneMem, 0x24C) = 640;
        WmField<int>(g_lsSceneMem, 0x250) = 480;
        g_gfLog.clear();
        ls->Start("m2", true, false, false, nullptr);
        std::vector<std::string> want = {"create 13", "getstate 15", "getstate 19", "getstate 11", "setstate 15 0",
                                         "setstate 19 1", "setstate 11 1", "open missions\\m2\\loader 0 0 0", "setstate 15 1",
                                         "setstate 19 0", "setstate 11 1", "scene release"};
        WM(r.lsOpenFail, GfVecIs(g_gfLog, want) && !ls->m_scene && !ls->m_shown && ls->m_players[0].size() == 1);
        ls->m_players[0].clear();
        // The viewport set unless it is the screen.
        const int rects[][4] = {{1, 0, 640, 480}, {0, 1, 640, 480}, {0, 0, 639, 480}, {0, 0, 640, 481}};
        for (const auto &rect : rects) {
            for (int k = 0; k < 4; ++k)
                WmField<int>(g_lsSceneMem, 0x244 + 4 * k) = rect[k];
            LsQueueFrames();
            g_gfLog.clear();
            ls->Start("m2", true, false, false, nullptr);
            WM(r.lsOpenFail, g_gfLog.size() == want.size() + 1 && g_gfLog[1] == "viewport 0 0 640 480");
        }
        g_lsOpenResult = 0;
    }

    // --- the scene: the camera, the light, a single mission's texts ---
    {
        LsVisuals({"briefing", "textbar", "title", "text_h&dversion", "MP_map survey", "MP_progress", "MP_designated"});
        LsQueueFrames();
        WmField<int>(g_lsSceneMem, 0x244) = 3;
        g_gfLog.clear();
        C_MpMissionDef *mission = LsMission(0);
        ls->Start("m3", true, true, false, mission);
        std::vector<std::string> want = LsSetupLog("m3", true);
        LsBoxLog(want, "text_h&dversion");
        want.push_back("tb text_h&dversion setid 2006 40 0");
        LsBoxLog(want, "MP_designated");
        want.push_back("tb MP_designated add 'text 2954 :' 81");
        LsBoxLog(want, "MP_progress");
        want.push_back("tb MP_progress add 'text 2953 :' 82");
        LsBoxLog(want, "MP_map survey");
        want.push_back("tb MP_map survey add 'text 2952 :' 82");
        LsBoxLog(want, "title");
        want.push_back("tb title addid 12 80");
        LsBoxLog(want, "textbar");
        LsBoxLog(want, "briefing");
        want.push_back("tb briefing addid 11 84");
        want.push_back("sfind loadbar ffff");
        const std::vector<std::string> boxes = {"text_h&dversion", "MP_designated", "MP_progress", "MP_map survey",
                                                "title", "textbar", "briefing"};
        LsDrawLog(want, boxes, 7);
        LsDrawLog(want, boxes, 7);
        const S_vector &at = g_lsCamera->m_localMatrix(3);
        WM(r.lsScene, GfVecIs(g_gfLog, want) && ls->m_shown && ls->m_scene == LsScene() && ls->m_waitForKey &&
                          !ls->m_twoParts && ls->m_camera == reinterpret_cast<I3D_camera *>(g_lsCamera) &&
                          ls->m_light == reinterpret_cast<I3D_light *>(g_lsLight) && ls->m_loadBar == g_plScene["loadbar"] &&
                          at.x == 0.5f && at.y == -0.375f && at.z == -15.5f && g_lsBoxesLive == 7);
        WM(r.lsTexts, ls->m_texts[0] && ls->m_texts[1] && ls->m_texts[2] && !ls->m_texts[3] && ls->m_texts[11] &&
                          ls->m_texts[14] && reinterpret_cast<const uint8_t *>(ls->m_texts[0])[0x90] == 5 &&
                          reinterpret_cast<const uint8_t *>(ls->m_texts[0])[0x71] == 0);
        WM(r.lsDraw, ls->m_loadBar->m_scale.x == 0.0f && ls->m_loadBar->m_scale.y == 1.0f && ls->m_loadBar->m_scale.z == 1.0f);

        // The progress: drawn once it moved on by more than 0.003 (the first table).
        g_gfLog.clear();
        ls->SetProgress(LOADER_STAGE_SCENE, 0.01f);
        WM(r.lsProgress, g_gfLog.empty() && ls->m_progress == 0.0f);
        ls->SetProgress(LOADER_STAGE_SCENE, 0.011f);
        std::vector<std::string> draw;
        LsDrawLog(draw, boxes, 7);
        WM(r.lsProgress, GfVecIs(g_gfLog, draw) && ls->m_progress == 0.3f * 0.011f + 0.0f &&
                             ls->m_loadBar->m_scale.x == ls->m_progress);
        const float tables[2][6][2] = {{{0.0f, 0.3f}, {0.3f, 0.5f}, {0.8f, 0.05f}, {0.85f, 0.05f}, {0.9f, 0.1f}, {1.0f, 0.0f}},
                                       {{0.0f, 0.15f}, {0.15f, 0.25f}, {0.4f, 0.05f}, {0.45f, 0.05f}, {0.5f, 0.05f},
                                        {0.55f, 0.45f}}};
        for (int two = 0; two < 2; ++two) {
            ls->m_twoParts = two != 0;
            for (int stage = 0; stage < LOADER_STAGE_COUNT; ++stage) {
                ls->m_progress = -1.0f;
                ls->SetProgress(stage, 0.75f);
                WM(r.lsProgress, ls->m_progress == tables[two][stage][1] * 0.75f + tables[two][stage][0]);
            }
            // (Backwards: kept.)
            ls->m_progress = 0.95f;
            ls->SetProgress(LOADER_STAGE_SCENE, 0.5f);
            WM(r.lsProgress, ls->m_progress == 0.95f);
        }
        ls->m_twoParts = false;
        // A dedicated server: printed, not drawn.
        g_mpSettings = &settings;
        settings.dedicated = true;
        ls->m_progress = 0.0f;
        g_gfLog.clear();
        ls->SetProgress(LOADER_STAGE_SOUNDS, 1.0f);
        WM(r.lsDraw, GfLogIs({"winmsg", CmText("netprogress %d", static_cast<int>((0.05f * 1.0f + 0.85f) * 100.0f))}));
        settings.dedicated = false;
        g_mpSettings = nullptr;

        // The close: drawn at 1, then "press space" until it is pressed, the boxes and the frames
        // let go.
        g_lsKeys = {0, 0x1C};
        g_gfLog.clear();
        ls->FinishLoading();
        want.clear();
        LsDrawLog(want, boxes, 7);
        want.push_back("gfupdate 100");
        want.push_back("tb textbar setid 2040 80 0");
        for (int k = 0; k < 3; ++k) {
            want.push_back("winmsg");
            want.push_back("readkb");
            LsDrawLog(want, boxes, 7);
        }
        want.push_back("tb textbar set '' 80 0");
        for (int k = 0; k < 3; ++k) {
            want.push_back("clear 0 1.00 3");
            want.push_back("flip");
        }
        for (const std::string &b : boxes) {
            want.push_back("tb " + b + " clear");
            want.push_back("tb " + b + " delete");
        }
        for (const char *s : {"release light", "release loadbar", "scene release", "release lscam"})
            want.push_back(s);
        WM(r.lsWait, GfVecIs(g_gfLog, want) && g_lsKeys.empty());
        WM(r.lsClose, !ls->m_shown && ls->m_progress == 0.0f && !ls->m_scene && !ls->m_camera && !ls->m_light &&
                          !ls->m_loadBar && g_lsBoxesLive == 0 && !ls->m_texts[0] && !ls->m_texts[14]);
    }

    // --- the close without the wait, without the bar's box ---
    {
        LsVisuals({"title"});
        LsQueueFrames();
        ls->Start("m4", true, false, true, nullptr);
        g_gfLog.clear();
        ls->Finish();
        std::vector<std::string> want;
        LsDrawLog(want, {"title"}, 7);
        for (const char *s : {"tb title clear", "tb title delete", "release light", "release loadbar", "scene release",
                              "release lscam"})
            want.push_back(s);
        WM(r.lsClose, GfVecIs(g_gfLog, want));
        LsVisuals({"title"});
        LsQueueFrames();
        ls->Start("m4", true, true, true, nullptr);
        g_lsKeys = {};
        g_gfLog.clear();
        ls->Finish();
        WM(r.lsWait, g_gfLog.size() > 11 && g_gfLog[10] == "gfupdate 100" && g_gfLog[11] == "winmsg");
        // No load bar, no light: no release; a destroyed screen closes.
        g_plScene.clear();
        LsQueueFrames();
        ls->Start("m4", true, false, true, nullptr);
        g_gfLog.clear();
        ls->m_light = nullptr;
        delete ls;
        ls = new C_LoaderScreen;
        want.clear();
        LsDrawLog(want, {}, 7);
        want.push_back("scene release");
        want.push_back("release lscam");
        WM(r.lsClose, GfVecIs(g_gfLog, want));
    }

    // --- a network map's texts (a cooperative one's briefing and title), the players ---
    {
        g_mpSettings = &settings;
        for (int k = 0; k < 4; ++k) {
            // k: 0 deathmatch, objectives; 1 another game type, cooperative; 2 no objectives; 3
            // no mission.
            settings.gameType = k == 1 ? 3 : 0;
            g_coop = k == 1;
            LsVisuals({"briefing", "title", "MP_list1", "MP_list2", "MP_objective1", "MP_objective2", "MP_title",
                       "MP_text_screen", "MP_team allied1", "MP_team allied2"});
            LsQueueFrames();
            ls->AddPlayer("alpha", 1);
            ls->AddPlayer("bravo", 0);
            ls->AddPlayer("char%lie", 2);
            ls->AddPlayer("", 1);
            ls->AddPlayer(nullptr, 1);
            ls->AddPlayer("delta", 1);
            WM(r.lsPlayers, ls->m_players[0].size() == 2 && ls->m_players[1].size() == 2 &&
                                ls->m_players[1][1] == C_str("char%lie"));
            C_MpMissionDef *mission = k == 3 ? nullptr : LsMission(k == 2 ? 0 : 2);
            g_gfLog.clear();
            ls->Start("m%5", true, false, false, mission);
            std::vector<std::string> want = LsSetupLog("m%5", true);   // (the rect still off)
            const std::string type = CmText("%d", 7000 + settings.gameType);
            LsBoxLog(want, "MP_team allied2");
            want.push_back("tb MP_team allied2 add 'text 2620' 80");
            if (settings.gameType == 0)
                want.push_back("tb MP_team allied2 setid 20521 80 0");
            LsBoxLog(want, "MP_team allied1");
            want.push_back("tb MP_team allied1 add 'text 2621' 80");
            if (settings.gameType == 0)
                want.push_back("tb MP_team allied1 setid 20520 80 0");
            LsBoxLog(want, "MP_text_screen");
            want.push_back("tb MP_text_screen addid " + type + " 79");
            LsBoxLog(want, "MP_title");
            want.push_back(mission ? "tb MP_title add 'Map Name' 80" : "tb MP_title add 'm%5' 80");
            for (int side = 2; side >= 1; --side) {
                const std::string box = CmText("MP_objective%d", side);
                LsBoxLog(want, box);
                want.push_back("tb " + box + " add 'text 2950 :' 82");
                if (!mission)
                    continue;
                want.push_back("tb " + box + " add ' ' 82");
                for (int o = 0; o < (k == 2 ? 0 : 2); ++o) {
                    want.push_back("tb " + box + (side == 1 ? " add '=' 85" : " add '!' 85"));
                    want.push_back(CmText("tb %s add ' text %d' 82", box.c_str(), (side == 1 ? 21 : 31) + o));
                }
            }
            LsBoxLog(want, "MP_list2");
            for (const char *s : {"tb MP_list2 add 'text 2951 :' 83", "tb MP_list2 add ' ' 83",
                                  "tb MP_list2 add 'bravo' 83", "tb MP_list2 add 'char%lie' 83"})
                want.push_back(s);
            LsBoxLog(want, "MP_list1");
            for (const char *s : {"tb MP_list1 add 'text 2951 :' 83", "tb MP_list1 add ' ' 83",
                                  "tb MP_list1 add 'alpha' 83", "tb MP_list1 add 'delta' 83"})
                want.push_back(s);
            LsBoxLog(want, "title");
            if (mission)
                want.push_back(k == 1 ? "tb title add 'Map Name' 80" : "tb title addid 12 80");
            LsBoxLog(want, "briefing");
            if (mission)
                want.push_back(k == 1 ? "tb briefing addid 13 84" : "tb briefing addid 11 84");
            want.push_back("sfind loadbar ffff");
            const std::vector<std::string> boxes = {"MP_team allied2", "MP_team allied1", "MP_text_screen", "MP_title",
                                                    "MP_objective2", "MP_objective1", "MP_list2", "MP_list1", "title",
                                                    "briefing"};
            LsDrawLog(want, boxes, 7);
            LsDrawLog(want, boxes, 7);
            const bool none = !mission || k == 2;
            WM(r.lsMpTexts, GfVecIs(g_gfLog, want) && ls->m_players[0].empty() && ls->m_players[1].empty() &&
                                (reinterpret_cast<const uint8_t *>(ls->m_texts[5])[0x71] != 0) == none &&
                                (reinterpret_cast<const uint8_t *>(ls->m_texts[6])[0x71] != 0) == none &&
                                reinterpret_cast<const uint8_t *>(ls->m_texts[3])[0x71] == 0);
            ls->FinishLoading();
        }
        // The texts' labels absent (-1): a single mission's briefing and title without texts.
        LsVisuals({"briefing", "title"});
        LsQueueFrames();
        g_coop = false;
        C_MpMissionDef *mission = LsMission(0);
        mission->m_texts[0] = mission->m_texts[1] = -1;
        g_gfLog.clear();
        ls->Start("m6", true, false, false, mission);
        int adds = 0;
        for (const std::string &l : g_gfLog)
            adds += l.find(" add") != std::string::npos;
        WM(r.lsTexts, adds == 0 && g_lsBoxesLive == 2);
        ls->FinishLoading();
        g_coop = true;
        mission->m_briefingText = -1;
        LsQueueFrames();
        LsVisuals({"briefing"});
        g_gfLog.clear();
        ls->Start("m6", true, false, false, mission);
        adds = 0;
        for (const std::string &l : g_gfLog)
            adds += l.find(" add") != std::string::npos;
        WM(r.lsTexts, adds == 0);
        ls->FinishLoading();
    }

    delete ls;
    MissionLoaderTest();
    g_coop = oldCoop;
    g_mpSettings = oldSettings;
    g_textIds = oldTextIds;
    g_plScene.clear();
    g_cmCreate.clear();
    for (size_t i = saved.size(); i > 0; i -= 2)
        saved[i - 2].first[saved[i - 2].second / 4] = reinterpret_cast<void *>(saved[i - 1].first);
    *reinterpret_cast<void ***>(effects) = oldEffectsVt;
    std::printf("loader: lsQuiet %d lsOpenFail %d lsScene %d lsTexts %d lsMpTexts %d lsProgress %d lsDraw %d lsClose %d "
                "lsWait %d lsPlayers %d\n",
                r.lsQuiet, r.lsOpenFail, r.lsScene, r.lsTexts, r.lsMpTexts, r.lsProgress, r.lsDraw, r.lsClose, r.lsWait,
                r.lsPlayers);
}

}  // namespace
