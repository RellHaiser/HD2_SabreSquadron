#include "game/cursor.h"

#include <cstring>

#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

float g_cursorX = -1.0f;                                // 0x00840668
float g_cursorY = -1.0f;                                // 0x0084066c

namespace {

C_Cursor *g_cursor;                                     // 0x008ae558

// The interface's y: 3/4 of the screen's width, down the screen.
constexpr double CURSOR_Y_SCALE = 0.75;
// The second click of a double click within (ms).
constexpr int DOUBLE_CLICK_TIME = 400;
constexpr uint32_t PICK_FLAGS = 0x140000;
constexpr float PICK_DEPTH = 0.1f;
constexpr uint32_t TEXTURE_MODE = 0x402;
// The drawn cursor: 30 pixels of a 1024 x 768 screen, scaled to the screen's size; its quad's
// depth and reciprocal w (the original's constants), white.
constexpr float DRAWN_SIZE = 30.0f;
constexpr float DRAWN_Z = 0.0284113996f, DRAWN_RHW = 35.1971397f;
constexpr uint32_t DRAWN_COLOR = 0xFFFFFFFF;
// A fan of two triangles.
constexpr int PRIMITIVE_FAN = 4;

// A pre-transformed vertex (the fonts' and the interface's layout).
struct S_CursorVertex {
    float x, y, z, rhw;
    uint32_t color, specular;
    float u, v;
};
// (Statics of the original's, 0x008ae4d8: the specular words never set.)
S_CursorVertex g_cursorQuad[4];                         // 0x008ae4d8

} // namespace

C_Cursor::~C_Cursor()
{
    Reset();
}

void C_Cursor::Reset()
{
    m_screenHeight = 0.0f;
    m_screenWidth = 0.0f;
    m_wheelTotal = 0.0f;
    m_moveY = 0.0f;
    m_moveX = 0.0f;
    m_clickedId = 0;
    m_pressedId = 0;
    m_hoveredId = 0;
    m_pickedId = 0;
    m_stillTime = 0;
    m_refs = 0;
    m_pressTime = 0;
    m_delay = 0;
    m_frame = nullptr;
    m_clicksOff = false;
}

C_Cursor *C_Cursor::Acquire(I3D_scene *scene)
{
    if (!g_cursor) {
        g_cursor = new C_Cursor();
        if (!g_cursor->Init(scene)) {
            delete g_cursor;
            g_cursor = nullptr;
            return nullptr;
        }
    }
    ++g_cursor->m_refs;
    return g_cursor;
}

void C_Cursor::Release()
{
    if (--m_refs != 0)
        return;
    // (The shared one deleted, whichever this is: the original's.)
    if (m_texture) {
        m_texture->Release();
        m_texture = nullptr;
    }
    delete g_cursor;
    g_cursor = nullptr;
}

bool C_Cursor::Init(I3D_scene *scene)
{
    m_screenWidth = static_cast<float>(GetIGraph()->ScrnX());
    m_hoveredId = 0;
    m_pressTime = 0;
    m_screenHeight = static_cast<float>(GetIGraph()->ScrnY());
    m_stillTime = 0;
    m_frame = scene->FindFrame("CURZOR", 0xFFFF);
    if (!m_frame)
        return false;
#ifndef HD2_DEDICATED   // no mouse in LS3DRF.dll
    IGraph *graph = GetIGraph();
    // (Truncated, as __ftol.)
    if (g_cursorX == -1.0f || g_cursorY == -1.0f)
        graph->SetMousePos(static_cast<int>(m_screenWidth * 0.5f), static_cast<int>(m_screenHeight * 0.5f));
    else
        graph->SetMousePos(static_cast<int>(g_cursorX), static_cast<int>(g_cursorY));
    g_cursorX = static_cast<float>(GetIGraph()->GetMouseX());
    g_cursorY = static_cast<float>(GetIGraph()->GetMouseY());
    m_moveX = static_cast<float>(GetIGraph()->GetMouseDeltaX());
    m_moveY = static_cast<float>(GetIGraph()->GetMouseDeltaY());
    m_wheel = static_cast<float>(GetIGraph()->GetMouseWheel());
#endif
    PlaceFrame();
    m_frame->SetOn(false);
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
    if (GetIGraph()->LoadTexture("x_kursor.tga", nullptr, TEXTURE_MODE, &m_texture) < 0) {
        m_texture = nullptr;
        return false;
    }
#else
    m_texture = nullptr;
#endif
    return true;
}

void C_Cursor::PlaceFrame()
{
    const S_vector place(g_cursorX / m_screenWidth,
                         static_cast<float>(-1.0f / m_screenHeight * g_cursorY * CURSOR_Y_SCALE), -1.0f);
    m_frame->SetPos(place);
    m_frame->Update();
}

bool C_Cursor::Update(int time)
{
    m_lastButtons = m_buttons;
#ifndef HD2_DEDICATED   // no mouse in LS3DRF.dll
    m_buttons = GetIGraph()->GetMouseButtons();
#else
    m_buttons = 0;
#endif
    m_stillTime += time;
    m_pressTime += time;
    m_clicked = false;
    m_doubleClicked = false;
#ifndef HD2_DEDICATED   // no mouse in LS3DRF.dll
    m_moveX = static_cast<float>(GetIGraph()->GetMouseDeltaX());
    m_moveY = static_cast<float>(GetIGraph()->GetMouseDeltaY());
    m_wheel = static_cast<float>(GetIGraph()->GetMouseWheel());
#else
    m_moveX = m_moveY = m_wheel = 0.0f;
#endif
    if (m_delay > 0) {
        m_delay -= time;
        return false;
    }
    if (!(m_moveX != 0.0f || m_moveY != 0.0f || m_wheel != 0.0f) && m_buttons == m_lastButtons &&
        m_pickedId == m_hoveredId)
        return false;
    m_stillTime = 0;
    m_hoveredId = m_pickedId;
    g_cursorX += m_moveX;
    g_cursorY += m_moveY;
    m_wheelTotal = m_wheel + m_wheelTotal;
    // Kept on the screen, the move cut by what it lost.
    if (g_cursorX < 0.0f) {
        m_moveX -= g_cursorX;
        g_cursorX = 0.0f;
    } else if (g_cursorX > m_screenWidth) {
        m_moveX -= g_cursorX - m_screenWidth;
        g_cursorX = m_screenWidth;
    }
    if (g_cursorY < 0.0f) {
        m_moveY -= g_cursorY;
        g_cursorY = 0.0f;
    } else if (g_cursorY > m_screenHeight) {
        m_moveY -= g_cursorY - m_screenHeight;
        g_cursorY = m_screenHeight;
    }
    if (!m_clicksOff) {
        if (IsPressed(0)) {
            m_pressedId = m_hoveredId;
        } else if (IsReleased(0)) {
            const uint32_t pressedId = m_pressedId;
            m_clicked = pressedId == m_hoveredId;
            m_doubleClicked = m_clicked && m_pressTime < DOUBLE_CLICK_TIME && m_clickedId == m_hoveredId;
            if (m_clicked)
                m_clickedId = pressedId;
            if (m_doubleClicked)
                m_clickedId = 0;
            m_pressedId = 0;
            m_pressTime = 0;
        }
    }
    PlaceFrame();
    return true;
}

void C_Cursor::Draw() const
{
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    const float x = g_cursorX, y = g_cursorY;
    const float width = m_screenWidth * (1.0f / 1024.0f) * DRAWN_SIZE;
    const float height = m_screenHeight * (1.0f / 768.0f) * DRAWN_SIZE;
    IGraph *graph = GetIGraph();
    S_IGraphViewport viewport = {0, 0, graph->ScrnX(), GetIGraph()->ScrnY(), 0.0f, 1.0f};
    const float right = width + x, bottom = height + y;
    // The corners (u, v): the place (0, 0), across (0, 1), across and down (1, 1), down (1, 0).
    const float corners[4][4] = {{x, y, 0.0f, 0.0f}, {right, y, 0.0f, 1.0f}, {right, bottom, 1.0f, 1.0f},
                                 {x, bottom, 1.0f, 0.0f}};
    for (int i = 0; i < 4; ++i) {
        S_CursorVertex &v = g_cursorQuad[i];
        v.x = corners[i][0];
        v.y = corners[i][1];
        v.z = DRAWN_Z;
        v.rhw = DRAWN_RHW;
        v.color = DRAWN_COLOR;
        v.u = corners[i][2];
        v.v = corners[i][3];
    }
    GetIGraph()->BeginScene();
    GetIGraph()->SetViewport(viewport);
    GetIGraph()->SetTexture(m_texture, 0);
    GetIGraph()->DrawPrimitives(PRIMITIVE_FAN, 2, g_cursorQuad, 1);
    GetIGraph()->EndScene();
#endif
}

uint32_t C_Cursor::Pick(I3D_scene *scene, I3D_frame *frame)
{
    S_vector from;
    GetPosition(&from);
    from.z += PICK_DEPTH;
    const S_vector dir(0.0f, 0.0f, 1.0f);
    I3D_collision_info info;
    I3D_frame *hit = scene->IntersectFrame(from, dir, frame, &info, PICK_FLAGS, 0, 0, 0);
    m_pickedId = hit ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hit->GetData())) : 0;
    return m_pickedId;
}

bool C_Cursor::Matches(uint32_t buttons, uint32_t state)
{
    return buttons == 0 ? state != 0 : (state & buttons) != 0;
}

bool C_Cursor::IsButtonDown(uint32_t buttons) const
{
    return Matches(buttons, m_buttons);
}

bool C_Cursor::WasButtonDown(uint32_t buttons) const
{
    return Matches(buttons, m_lastButtons);
}

bool C_Cursor::IsPressed(uint32_t buttons) const
{
    return IsButtonDown(buttons) && !WasButtonDown(buttons);
}

bool C_Cursor::IsReleased(uint32_t buttons) const
{
    return !IsButtonDown(buttons) && WasButtonDown(buttons);
}

void C_Cursor::GetPosition(S_vector *position) const
{
    // (The y: -(y / height) * 0.75 here, (-1 / height) * y * 0.75 for the frame: the original's.)
    const auto y = static_cast<float>(-(g_cursorY / m_screenHeight) * CURSOR_Y_SCALE);
    position->x = g_cursorX / m_screenWidth;
    position->y = y;
    position->z = -1.0f;
}

void C_Cursor::GetMove(S_vector *move) const
{
    const auto y = static_cast<float>(-(m_moveY / m_screenHeight) * CURSOR_Y_SCALE);
    move->x = m_moveX / m_screenWidth;
    move->y = y;
    move->z = -1.0f;
}

void C_Cursor::GetScreenPosition(float *x, float *y, float *buttons) const
{
    if (x)
        *x = g_cursorX;
    if (y)
        *y = g_cursorY;
    // (The buttons' bits as they are: the original's.)
    if (buttons)
        std::memcpy(buttons, &m_buttons, sizeof(*buttons));
}

bool C_Cursor::HasMoved() const
{
    return m_moveX != 0.0f || m_moveY != 0.0f;
}

int C_Cursor::GetStillTime() const
{
    return m_stillTime;
}

void C_Cursor::SetDelay(int time)
{
    m_delay = time;
}
