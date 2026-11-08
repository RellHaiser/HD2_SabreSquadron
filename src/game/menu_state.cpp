#include "game/menu_state.h"

#include <windows.h>

#include "game/app.h"
#include "game/campaign.h"
#include "game/cursor.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/screen.h"
#include "game/sound_module.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

std::deque<C_Screen *> g_menuScreens;                   // 0x008ae5c4
C_MenuState *g_menuState;                               // 0x008ae5f8

std::deque<S_ControlMessage> g_deferredMessages;        // 0x008ae55c

namespace {

// A screen's index: its id's top 6 bits.
constexpr int SCREEN_INDEX_SHIFT = 26;
constexpr uint32_t SCREEN_INDEX_MASK = 0x3F;
// The message box's index (made first, its description's id 0).
constexpr uint32_t MESSAGE_BOX_SCREEN = 0;
// The id the popup's screen is opened with (OnEnter's).
constexpr int POPUP_ENTER_ID = 0x10200000;

void Post(uint32_t id, uint32_t value)
{
    S_ControlMessage message;
    message.id = id;
    message.value = value;
    PostControlMessage(message);
}

// The top screen hidden and taken off.
void PopScreen()
{
    g_menuScreens.back()->Hide();
    g_menuScreens.pop_back();
}

} // namespace

// --- the hint ----------------------------------------------------------------------------------------

void S_MenuHint::Reset()
{
    m_on = false;
    m_unknown00 = m_unknown04 = m_unknown08 = -1;
    m_frame = nullptr;
    m_hasText = false;
    m_box.Clear();
}

void S_MenuHint::SetText(const C_str &text, uint8_t style)
{
    m_hasText = true;
    m_box.SetText(text, style, 0);
}

void S_MenuHint::Render(I3D_scene *scene)
{
    if (!m_on || !m_hasText)
        return;
    m_box.AttachTo(scene, m_frame);
    m_box.Render();
}

// --- the menu state ----------------------------------------------------------------------------------

C_MenuState::C_MenuState()
{
    m_state = GAME_STATE_QUIT;
    g_menuState = this;
}

C_MenuState::~C_MenuState()
{
    g_menuState = nullptr;
    m_editing = nullptr;
}

void C_MenuState::HandleMessage(S_ControlMessage *message)
{
    const uint32_t id = message->id;
    if (id & MENU_MSG_DEFER) {
        g_deferredMessages.push_back(*message);
        return;
    }
    if (id & MENU_MSG_DEFERRED) {
        if (id & MENU_DEFERRED_POST) {
            while (!g_deferredMessages.empty()) {
                g_deferredMessages.front().id &= ~MENU_MSG_DEFER;
                PostControlMessage(g_deferredMessages.front());
                g_deferredMessages.pop_front();
            }
        }
        if (message->id & MENU_DEFERRED_DROP)
            g_deferredMessages.clear();
        return;
    }
    if (message->target != 0) {
        // (Fix: the original took the screen without checking the index.)
        const uint32_t index = message->target >> SCREEN_INDEX_SHIFT;
        if (index < m_screens.size())
            m_screens[index]->HandleMessage(message);
        return;
    }
    switch (id & CONTROL_MSG_MASK) {
    case MENU_MSG_OPEN_SCREEN:
        if (!g_menuScreens.empty())
            g_menuScreens.back()->Hide();
        // (Fix: the original read one past the screens for the index of their count.)
        if (message->value >= m_screens.size())
            return;
        g_menuScreens.push_back(m_screens[message->value]);
        g_menuScreens.back()->Open(0);
        return;
    case MENU_MSG_OPEN_POPUP:
        // (Fix: the original read the top of an empty stack and took the screen unchecked.)
        m_underPopup = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
        if (message->value >= m_screens.size())
            return;
        g_menuScreens.push_back(m_screens[message->value]);
        g_menuScreens.back()->Open(POPUP_ENTER_ID);
        return;
    case MENU_MSG_BACK:
        if (g_menuScreens.size() == 1) {
            // The last screen: the quit question, the quit kept back for its answer.
            Post(MENU_MSG_OPEN_POPUP, QUIT_SCREEN);
            Post(MENU_MSG_DEFER | MENU_MSG_QUIT, 0);
        } else if ((message->value & SCREEN_INDEX_MASK << SCREEN_INDEX_SHIFT) == QUIT_SCREEN << SCREEN_INDEX_SHIFT) {
            // Back from the quit question: the kept quit dropped, the question closed.
            Post(MENU_MSG_DEFERRED | 0x4000000 | 0x1000 | MENU_DEFERRED_DROP, 1);
            Post(0x4000000 | 0x1000 | MENU_MSG_CLOSE_POPUP, 0);
        } else {
            if (g_menuScreens.empty())
                return;
            PopScreen();
            if (g_menuScreens.empty())
                return;
            g_menuScreens.back()->Open(0);
        }
        return;
    case MENU_MSG_QUIT:
        GetSoundModule()->StopAll(true, false);
        m_state = GAME_STATE_QUIT;
        return;
    case MENU_MSG_CLOSE_POPUP:
        if (g_menuScreens.empty())
            return;
        PopScreen();
        m_underPopup = nullptr;
        if (g_menuScreens.empty())
            return;
        g_menuScreens.back()->Open(0);
        return;
    case MENU_MSG_PREVIOUS:
        while (!g_menuScreens.empty())
            PopScreen();
        m_state = GAME_STATE_BACK;
        return;
    case MENU_MSG_NEXT:
        while (!g_menuScreens.empty())
            PopScreen();
        m_state = GAME_STATE_ENDED;
        return;
    case MENU_MSG_MAIN_MENU: {
        S_ControlMessage reset;
        reset.id = MENU_MSG_RESET_GAME;
        HandleMessage(&reset);
        m_state = GAME_STATE_MAIN_MENU;
        return;
    }
    case MENU_MSG_RESET_GAME:
        g_gameProfile.ResetGame();
        return;
    case MENU_MSG_RELEASE_FRAME:
        for (size_t i = 0; i < m_frames.size(); ++i) {
            if (reinterpret_cast<uintptr_t>(m_frames[i]) == message->value2) {
                ReleaseFrameTree(m_frames[i]);
                m_frames.erase(m_frames.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
        return;
    case CONTROL_MSG_NOTIFY: {
        // The cursor's notifications to the top screen (its frame made).
        if (g_menuScreens.empty() || !g_menuScreens.back() || !g_menuScreens.back()->GetScreenFrame())
            return;
        C_Screen *top = g_menuScreens.back();
        if (message->value == CONTROL_NOTIFY_05)
            top->OnInput05(static_cast<int>(m_cursor->Pick(m_scene, top->GetScreenFrame())));
        else if (message->value == CONTROL_NOTIFY_LEAVE)
            top->OnLeave(0);
        return;
    }
    case MENU_MSG_ADD_TICKED: {
        auto *control = reinterpret_cast<C_Control *>(message->value2);
        if (!control)
            return;
        for (C_Control *c : m_tickedControls)
            if (c == control)
                return;
        m_tickedControls.push_back(control);
        return;
    }
    case MENU_MSG_REMOVE_TICKED: {
        auto *control = reinterpret_cast<C_Control *>(message->value2);
        if (!control)
            return;
        for (size_t i = 0; i < m_tickedControls.size(); ++i) {
            if (m_tickedControls[i] == control) {
                m_tickedControls.erase(m_tickedControls.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
        return;
    }
    case MENU_MSG_MESSAGE_BOX_SHOWN:
        // (Fix: the original read the top of an empty stack and took the box unchecked.)
        m_underPopup = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
        if (m_screens.empty())
            return;
        g_menuScreens.push_back(m_screens[MESSAGE_BOX_SCREEN]);
        g_menuScreens.back()->Open(0);
        return;
    case MENU_MSG_MESSAGE_BOX_CLOSED:
        // Only the box itself taken off.
        if (g_menuScreens.empty() || (g_menuScreens.back()->GetId() >> SCREEN_INDEX_SHIFT & SCREEN_INDEX_MASK) != MESSAGE_BOX_SCREEN)
            return;
        PopScreen();
        m_underPopup = nullptr;
        return;
    case CONTROL_MSG_START_EDITING: {
        // The control editing asked to stop first (refused: not started).
        if (m_editing && m_editing->OnInput0B(0) == 0)
            return;
        m_editing = reinterpret_cast<C_Control *>(message->value2);
        m_editing->BeginEdit(0);
        return;
    }
    case CONTROL_MSG_STOP_EDITING:
        if (m_editing)
            m_editing->OnInput0B(0);
        if (reinterpret_cast<uintptr_t>(m_editing) != message->value2)
            return;
        m_editing = nullptr;
        m_keyboardOff = false;
        SetKeyboardPolling(true);
        m_cursor->SetClicksOff(m_keyboardOff);
        return;
    case MENU_MSG_KEYBOARD_OFF:
        m_keyboardOff = message->value != 0;
        SetKeyboardPolling(!m_keyboardOff);
        m_cursor->SetClicksOff(m_keyboardOff);
        return;
    default:
        OnMessage(message);
        return;
    }
}

void C_MenuState::HandleMessages()
{
    while (!g_controlMessages.empty()) {
        S_ControlMessage message = g_controlMessages.front();
        g_controlMessages.pop_front();
        HandleMessage(&message);
    }
}

C_Control *C_MenuState::FindControl(uint32_t id)
{
    if (id == 0)
        return nullptr;
    const uint32_t index = id >> SCREEN_INDEX_SHIFT;
    if (index >= m_screens.size())
        return nullptr;
    return m_screens[index]->FindControl(id, true);
}

void C_MenuState::ShowMessage(int kind, const C_str &text, uint32_t yesMessage, uint32_t noMessage)
{
    if (m_messageBox)
        m_messageBox->ShowText(kind, text, yesMessage, noMessage);
}

void C_MenuState::ShowMessageId(int kind, uint32_t textId, uint32_t yesMessage, uint32_t noMessage)
{
    if (m_messageBox)
        m_messageBox->ShowTextId(kind, textId, yesMessage, noMessage);
}

void C_MenuState::CloseMessage()
{
    if (m_messageBox)
        m_messageBox->Close();
}

// --- starting, stopping, the input, the update and the drawing (part 9d-ii-b) -----------------------

namespace {

// The scene's render callback's flags (the phases it is called in) and the phase the screens
// are drawn in.
constexpr uint32_t MENU_RENDER_PHASES = 4;
constexpr int MENU_RENDER_PHASE = 2;
// Start's arguments for the in-game menu.
const void *const START_GAME = reinterpret_cast<void *>(-1);
const void *const START_GAME_2 = reinterpret_cast<void *>(-2);
// The music not touched after a mission, at an operation's end and in the game.
constexpr int KIND_DEBRIEFING = 5, KIND_OPERATION_END = 6;
// The music crossfaded to the menus' (track 37, the previous menus' own in 2 s).
constexpr int MUSIC_FADED_TRACK = 37;
constexpr int MUSIC_FADE_TIME = 2000;
// The camera: looking along +z from (0.5, -0.375, -15.5), orthogonal (scale 2); 0.2 .. 20 m;
// a quarter turn's view.
const S_vector CAMERA_POS(0.5f, -0.375f, -15.5f);
constexpr float CAMERA_NEAR = 0.2f, CAMERA_FAR = 20.0f;
constexpr float CAMERA_FOV = 1.5707964f;
constexpr float CAMERA_ORTHO_SCALE = 2.0f;
// The light: ambient (type 4), white at 0.9.
constexpr int LIGHT_AMBIENT = 4;
constexpr float LIGHT_POWER = 0.9f;
// The click sound (type 3).
constexpr int CLICK_SOUND_TYPE = 3;
// The message box's model's place.
const S_vector MESSAGE_BOX_POS(0.5f, -0.375f, -0.5f);
// The driver's states LoadScreens sets while the models load (as the loader screen's).
constexpr int DRIVER_STATE_11 = 0x11, DRIVER_STATE_15 = 0x15, DRIVER_STATE_19 = 0x19;
// The driver's state Render clears for the second drawing (the depth buffer's clear; inferred).
constexpr int DRIVER_STATE_5 = 5;
// The keys: the control and the alt and the shift keys (left / right), and N; Escape.
constexpr int KEY_LCONTROL = 0x1D, KEY_RCONTROL = 0x9D, KEY_LALT = 0x38, KEY_RALT = 0xB8;
constexpr int KEY_LSHIFT = 0x2A, KEY_RSHIFT = 0x36;
constexpr int KEY_N = 0x4E;
constexpr uint8_t KEY_ESCAPE = 1;
// The controls' action pressed taken as Escape (C_Controls' 46th).
constexpr int CONTROL_ESCAPE = 46;
// The cursor's buttons: the left and the right.
constexpr uint32_t CURSOR_BUTTONS = 3;
// The typed characters read at once (HandleInput's buffer).
constexpr int TYPED_CHARS = 0x3FF;

// The scene's callback: the screens drawn in their phase (by the menu state made last).
int __stdcall MenuRenderCallback(int phase, int, int)  // 0x0066e3f0
{
    // (The original first drew an object at 0x008ae5fc, never set, and tested a flag at 0x0084068c,
    // always 1.)
    if (phase == MENU_RENDER_PHASE)
        g_menuState->DrawScreens();
    return 0;
}

} // namespace

bool ReadTypedChars(wchar_t *out, int size)
{
    char typed[TYPED_CHARS + 1];
    int count = 0;
    out[0] = 0;
    do {
#ifndef HD2_DEDICATED   // no keyboard in LS3DRF.dll
        const char c = static_cast<char>(GetIGraph()->ReadChar());
#else
        const char c = 0;
#endif
        typed[count++] = c;
        if (c == 0)
            break;
    } while (count < TYPED_CHARS);
    // (Fix: the original passed a buffer without its 0 when all 0x3FF were characters.)
    typed[count] = 0;
    MultiByteToWideChar(CP_ACP, 0, typed, -1, out, size);
    return count > 1;
}

bool C_MenuState::Start(void *arg)
{
    m_startArg = arg;
    if (arg == START_GAME_2 || arg == START_GAME)
        m_mode = MENU_MODE_GAME;
    else if (!arg)
        m_mode = MENU_MODE_MAIN;
    else
        m_mode = static_cast<C_CampaignScreen *>(arg)->GetKind();

    m_scene = reinterpret_cast<I3D_scene *>(I3DGetDriver()->CreateFrame(FRAME_SCENE));
    m_scene->SetRenderCallback(reinterpret_cast<void *>(&MenuRenderCallback), MENU_RENDER_PHASES);
    // The whole screen.
    const int *viewport = m_scene->GetViewportRect();
    if (viewport[0] != 0 || viewport[1] != 0 || viewport[2] != GetIGraph()->ScrnX() ||
        viewport[3] != GetIGraph()->ScrnY()) {
        const int bottom = GetIGraph()->ScrnY();
        const int right = GetIGraph()->ScrnX();
        m_scene->SetViewport(0, 0, right, bottom);
    }

    auto *camera = static_cast<I3D_camera *>(I3DGetDriver()->CreateFrame(FRAME_CAMERA));
    m_camera = camera;
    m_scene->SetActiveCamera(camera);
    m_scene->AddFrame(camera);
    camera->SetName("ActivCamera");
    camera->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    camera->SetPosInline(CAMERA_POS);
    camera->SetRange(CAMERA_NEAR, CAMERA_FAR);
    camera->SetFOV(CAMERA_FOV);
    camera->SetOrthogonal(true);
    camera->SetOrthoScale(CAMERA_ORTHO_SCALE);
    camera->Update();

    OnStart(m_startArg);
    LoadScreens();
    m_cursor = C_Cursor::Acquire(m_scene);

    auto *light = static_cast<I3D_light *>(I3DGetDriver()->CreateFrame(FRAME_LIGHT));
    m_light = light;
    light->SetLightType(LIGHT_AMBIENT);
    light->SetName("ambient");
    light->SetAmbientColor(S_vector(1.0f, 1.0f, 1.0f));
    light->SetAmbientPower(LIGHT_POWER);
    m_scene->AddFrame(light);
    m_scene->GetPrimarySector()->AddLight(light);
    light->Release();

    auto *sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    m_clickSound = sound;
    if (ISndGetDriver()->IsInitialized())
        g_soundCache.OpenInto(sound, "button.wav");
    sound->SetSoundType(CLICK_SOUND_TYPE);
    sound->SetName("sound_click");
    sound->SetOn(false);
    sound->SetVolume(1.0f);
    m_scene->AddFrame(sound);
    m_scene->GetPrimarySector()->AddSound(sound);
    sound->Release();

    if (m_mode != KIND_DEBRIEFING && m_mode != KIND_OPERATION_END && m_mode != MENU_MODE_GAME) {
        if (GetSoundModule()->GetBackgroundMusic() == -1)
            GetSoundModule()->SetBackgroundMusic(0, 0);
        else if (GetSoundModule()->GetBackgroundMusic() == MUSIC_FADED_TRACK)
            GetSoundModule()->CrossfadeMusic(0, MUSIC_FADE_TIME, 0);
    }
    // (The original cleared a flag at 0x008ae609 here, never read.)
    if (m_startArg && m_startArg != START_GAME && m_startArg != START_GAME_2) {
        const std::vector<int> &texts = static_cast<C_CampaignScreen *>(m_startArg)->GetDiaryTexts();
        for (int text : texts)
            g_gameProfile.GetPlayerProfile()->GetDiary()->AddText(text);
    }
    m_inputLock = -1;
    m_underPopup = nullptr;
    m_state = GAME_STATE_RUNNING;
    return true;
}

bool C_MenuState::Stop()
{
    OnStop();
    g_menuScreens.clear();
    while (!m_screens.empty()) {
        delete m_screens.back();
        m_screens.pop_back();
    }
    ReleaseModels();
    m_tickedControls.clear();
    if (m_cursor) {
        m_cursor->Release();
        m_cursor = nullptr;
    }
    if (m_camera) {
        m_camera->Release();
        m_camera = nullptr;
    }
    if (m_scene) {
        m_scene->SetActiveCamera(nullptr);
        m_scene->Close();
        m_scene->Release();
        m_scene = nullptr;
    }
    m_light = nullptr;
    g_controlMessages.clear();
    return true;
}

bool C_MenuState::HandleInput(float *held, float *pressed)
{
    if (m_keyboardOff)
        return true;
    OnInput(held, pressed);
    if (g_menuScreens.empty())
        return true;
#ifndef HD2_DEDICATED   // no keyboard in LS3DRF.dll
    const int pressedKey = GetIGraph()->GetAsciiKey();
#else
    const int pressedKey = 0;
#endif
    // The characters typed, to the control editing.
    if (!g_gameProfile.UsesUnicodeFont()) {
#ifndef HD2_DEDICATED   // no keyboard in LS3DRF.dll
        const uint8_t c = static_cast<uint8_t>(GetIGraph()->ReadChar());
#else
        const uint8_t c = 0;
#endif
        if (c != 0 && m_editing && m_editing->OnChar(c))
            return true;
    } else {
        wchar_t typed[TYPED_CHARS];
        bool taken = false;
        if (ReadTypedChars(typed, TYPED_CHARS) && typed[0] != 0) {
            // (The original also printed each one, "KEY VALUE :: %d", to the debugger.)
            for (const wchar_t *c = typed; *c != 0; ++c)
                if (m_editing && m_editing->OnChar(*c))
                    taken = true;
            if (taken)
                return true;
        }
    }
    uint8_t key = static_cast<uint8_t>(pressedKey);
#ifndef HD2_DEDICATED   // no keyboard in LS3DRF.dll
    if (key == KEY_N) {
        if (GetIGraph()->IsAltDown() && GetIGraph()->IsShiftDown() && GetIGraph()->IsCtrlDown()) {
            S_ControlMessage message;
            message.id = MENU_MSG_SECRET_KEY;
            PostControlMessage(message);
            return true;
        }
    } else if (key == 0) {
        // No key: a held control, alt or shift key (the left one if it is down), else the
        // controls' escape.
        if (GetIGraph()->IsCtrlDown())
            key = GetIGraph()->IsKeyDown(KEY_LCONTROL) ? KEY_LCONTROL : KEY_RCONTROL;
        else if (GetIGraph()->IsAltDown())
            key = GetIGraph()->IsKeyDown(KEY_LALT) ? KEY_LALT : KEY_RALT;
        else if (GetIGraph()->IsShiftDown())
            key = GetIGraph()->IsKeyDown(KEY_LSHIFT) ? KEY_LSHIFT : KEY_RSHIFT;
        else if (pressed && pressed[CONTROL_ESCAPE] != 0.0f)
            key = KEY_ESCAPE;
        else
            return true;
    }
#else
    if (key == 0) {
        if (pressed && pressed[CONTROL_ESCAPE] != 0.0f)
            key = KEY_ESCAPE;
        else
            return true;
    }
#endif
    if (g_menuScreens.back()->Unknown20(key) || key != KEY_ESCAPE)
        return true;
    // Escape not taken: the drag ended, the menu told.
    if (m_dragging) {
        g_menuScreens.back()->OnInput09(0);
        m_dragging = false;
    }
    S_ControlMessage message;
    message.id = MENU_MSG_ESCAPE;
    message.value = g_menuScreens.back()->GetId();
    PostControlMessage(message);
    return true;
}

void C_MenuState::Update(int timeDelta)
{
    OnUpdate(timeDelta);
    if (m_mode != MENU_MODE_GAME || (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL)
        GetSoundModule()->Update(timeDelta);
    // (Quirk kept: the menu's and the sound module's updates again.)
    OnUpdate(timeDelta);
    GetSoundModule()->Update(timeDelta);
    if (m_timing)
        m_time += timeDelta;
    HandleMessages();
    if (g_menuScreens.empty())
        return;
    CursorInput(timeDelta);
    if (m_editing)
        m_editing->Tick(timeDelta);
    g_menuScreens.back()->Tick(timeDelta);
    if (m_popup)
        m_popup->Tick(timeDelta);
    for (size_t i = m_tickedControls.size(); i-- > 0;)
        m_tickedControls[i]->Tick(timeDelta);
}

void C_MenuState::Render()
{
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    if (!OnRender() && m_scene) {
        C_Screen *top = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
        if (m_underPopup && top) {
            // The screen under the popup first, then the popup over it (the driver's state 5
            // cleared for the second).
            const int state5 = I3DGetDriver()->GetState(DRIVER_STATE_5);
            top->GetScreenFrame()->SetOn(false);
            m_underPopup->GetScreenFrame()->SetOn(true);
            m_scene->Render(0);
            I3DGetDriver()->SetState(DRIVER_STATE_5, 0);
            top->GetScreenFrame()->SetOn(true);
            m_underPopup->GetScreenFrame()->SetOn(false);
            m_scene->Render(0);
            I3DGetDriver()->SetState(DRIVER_STATE_5, state5);
            m_underPopup->GetScreenFrame()->SetOn(true);
        } else if (m_popup && m_popup->HasPopup()) {
            // The top first, then the popup's own popup over it.
            // (Quirk kept: the top read unchecked when the stack is empty.)
            const int state5 = I3DGetDriver()->GetState(DRIVER_STATE_5);
            top->GetScreenFrame()->SetOn(true);
            m_popup->GetScreenFrame()->SetOn(false);
            m_scene->Render(0);
            I3DGetDriver()->SetState(DRIVER_STATE_5, 0);
            top->GetScreenFrame()->SetOn(false);
            m_popup->GetScreenFrame()->SetOn(true);
            m_scene->Render(0);
            I3DGetDriver()->SetState(DRIVER_STATE_5, state5);
            top->GetScreenFrame()->SetOn(true);
            m_popup->GetScreenFrame()->SetOn(false);
        } else {
            m_scene->Render(0);
        }
    }
#endif
    m_cursor->Draw();
}

void C_MenuState::DrawScreens()
{
    C_Screen *top = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
    // (The original batched the fonts here under a flag at 0x008ae608, never set.)
    if (m_underPopup && m_underPopup->GetScreenFrame()->IsOn())
        m_underPopup->Refresh();
    if (top && top->GetScreenFrame()->IsOn())
        top->Refresh();
    OnDrawScreens();
    if (m_popup && m_popup->HasPopup() && !m_underPopup)
        m_popup->Refresh();
    m_hint.Render(m_scene);
}

I3D_frame *C_MenuState::PickFrame()
{
    S_vector from;
    m_cursor->GetPosition(&from);
    from.z += 0.1f;
    const S_vector dir(0.0f, 0.0f, 1.0f);
    I3D_collision_info info;
    // (Quirk kept: the top read unchecked when the stack is empty.)
    return m_scene->IntersectFrame(from, dir, g_menuScreens.back()->GetScreenFrame(), &info, 0x140000, 0, 0, 0);
}

I3D_frame *C_MenuState::CloneVisual(const C_str &name, I3D_frame *parent, I3D_frame *source)
{
    I3D_frame *copy = nullptr;
    CloneFrameTree(copy, source, nullptr);
    auto *visual = static_cast<I3D_object_visual *>(copy);
    visual->DuplicateMesh();
    visual->GetVisualMesh()->DuplicateMaterials();
    copy->SetName(name.c_str());
    copy->LinkTo(parent, 0);
    m_frames.push_back(copy);
    return copy;
}

I3D_frame *C_MenuState::CloneFrame(const C_str &name, I3D_frame *parent, I3D_frame *source)
{
    I3D_frame *copy = nullptr;
    CloneFrameTree(copy, source, nullptr);
    copy->SetName(name.c_str());
    copy->LinkTo(parent, 0);
    m_frames.push_back(copy);
    return copy;
}

I3D_frame *C_MenuState::AddFrame(const C_str &name, int type, I3D_frame *parent, const S_vector &pos)
{
    I3D_frame *frame = I3DGetDriver()->CreateFrame(static_cast<E_I3D_FRAME_TYPE>(type));
    frame->LinkTo(parent, 0);
    frame->SetName(name.c_str());
    frame->SetPos(pos);
    m_models.push_back(frame);
    return frame;
}

I3D_frame *C_MenuState::LoadModel(const char *name, const char *file)
{
    auto *model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (g_modelCache.OpenInto(model, file) != 0) {
        // (Fix: the original kept the model it could not load.)
        model->Release();
        return nullptr;
    }
    if (name)
        model->SetName(name);
    model->LinkTo(m_scene->GetPrimarySector(), 0);
    model->SetOn(false);
    m_models.push_back(model);
    return model;
}

bool C_MenuState::CreateMessageBox(const char *name, const char *file)
{
    I3D_frame *model = LoadModel(name, file);
    if (!model)
        return false;
    model->SetPos(MESSAGE_BOX_POS);
    auto *desc = new S_ScreenDesc(name, file, 0);
    auto *box = new C_MessageBox(m_scene, model);
    m_messageBox = box;
    if (!box->Build(desc)) {
        // (Quirk kept: the box itself not deleted, nor kept among the screens.)
        delete desc;
        return false;
    }
    m_screens.push_back(m_messageBox);
    delete desc;
    return true;
}

bool C_MenuState::LoadScreens()
{
    I3D_driver *driver = I3DGetDriver();
    const bool state15 = driver->GetState(DRIVER_STATE_15) != 0;
    const bool state19 = I3DGetDriver()->GetState(DRIVER_STATE_19) != 0;
    const bool state11 = I3DGetDriver()->GetState(DRIVER_STATE_11) != 0;
    I3DGetDriver()->SetState(DRIVER_STATE_15, 0);
    I3DGetDriver()->SetState(DRIVER_STATE_19, 0);
    I3DGetDriver()->SetState(DRIVER_STATE_11, m_mode == MENU_MODE_GAME ? 0 : 1);
    LoadModel("CURZOR", "kurzor_menu");
    CreateMessageBox("CONFIRM", "confirm");
    BuildScreens();
    I3DGetDriver()->SetState(DRIVER_STATE_15, state15);
    I3DGetDriver()->SetState(DRIVER_STATE_19, state19);
    I3DGetDriver()->SetState(DRIVER_STATE_11, state11);
    return true;
}

void C_MenuState::ReleaseModels()
{
    while (!m_frames.empty()) {
        ReleaseFrameTree(m_frames.back());
        m_frames.pop_back();
    }
    while (!m_models.empty()) {
        m_models.back()->Release();
        m_models.pop_back();
    }
}

void C_MenuState::PlayClick()
{
    if (m_clickSound && !m_clickSound->IsPlaying(0))
        m_clickSound->SetOn(true);
}

void C_MenuState::CursorInput(int timeDelta)
{
    if (m_inputLock != -1)
        return;
    const uint32_t id = m_cursor->Pick(m_scene, g_menuScreens.back()->GetScreenFrame());
    if (!m_cursor->Update(timeDelta) && m_hoveredId == id)
        return;
    m_hoveredId = id;
    const int target = static_cast<int>(id);
    if (m_keyboardOff) {
        if (m_cursor->HasMoved()) {
            m_dragging = false;
            g_menuScreens.back()->OnInput05(target);
        }
        return;
    }
    if (m_cursor->IsPressed(CURSOR_BUTTONS)) {
        // A press away from the control editing: its editing ended first (refused: nothing).
        if (m_editing && m_editing->GetId() != id) {
            if (m_editing->OnInput0B(target) != CONTROL_HANDLED)
                return;
            m_editing = nullptr;
        }
        g_menuScreens.back()->OnPress(target);
    } else if (m_cursor->IsReleased(CURSOR_BUTTONS)) {
        if (m_dragging) {
            m_dragging = false;
            g_menuScreens.back()->OnInput09(target);
            g_menuScreens.back()->OnInput05(target);
        }
        g_menuScreens.back()->OnRelease(target);
    }
    if (m_cursor->IsDoubleClicked())
        g_menuScreens.back()->OnInput13(target);
    if (m_cursor->IsClicked() && (!m_editing || m_editing->GetId() == id) &&
        g_menuScreens.back()->OnKeyClick(target) == CONTROL_HANDLED)
        PlayClick();
    if (m_cursor->HasMoved()) {
        if (!m_dragging)
            g_menuScreens.back()->OnInput05(target);
        if (m_cursor->IsButtonDown(CURSOR_BUTTONS)) {
            // A drag: its start, then each move.
            if (!m_dragging) {
                g_menuScreens.back()->OnInput0A(target);
                m_dragging = true;
            }
            g_menuScreens.back()->OnInput08(target);
            return;
        }
    }
    if (m_cursor->HasWheel()) {
        g_menuScreens.back()->OnWheel(target, static_cast<int>(m_cursor->GetWheel()));
        // The id under the cursor again (the wheel may have scrolled the screen).
        C_Screen *top = g_menuScreens.back();
        top->OnInput05(static_cast<int>(m_cursor->Pick(m_scene, top->GetScreenFrame())));
    }
}
