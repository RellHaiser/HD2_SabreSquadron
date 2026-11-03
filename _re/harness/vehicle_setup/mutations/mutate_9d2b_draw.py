"""Part 9d-ii-b mutations: the menus' cursor drawn, C_Cursor::Draw (game/cursor.cpp).

Run with the cursor harness: --build build_cursor.bat --check check_cursor.py (cursor_test.cpp's
"cuDraw" checks). By hand: the size, the corners, the vertices' fields, the calls and their order.
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
M = os.path.join(ROOT, r"src/game/cursor.cpp")

MUTATIONS = [
    (M, "M draw size", "constexpr float DRAWN_SIZE = 30.0f;", "constexpr float DRAWN_SIZE = 32.0f;"),
    (M, "M draw z", "constexpr float DRAWN_Z = 0.0284113996f, DRAWN_RHW = 35.1971397f;",
     "constexpr float DRAWN_Z = 0.0284f, DRAWN_RHW = 35.1971397f;"),
    (M, "M draw rhw", "constexpr float DRAWN_Z = 0.0284113996f, DRAWN_RHW = 35.1971397f;",
     "constexpr float DRAWN_Z = 0.0284113996f, DRAWN_RHW = 35.2f;"),
    (M, "M draw color", "constexpr uint32_t DRAWN_COLOR = 0xFFFFFFFF;", "constexpr uint32_t DRAWN_COLOR = 0xFFFFFF;"),
    (M, "M draw fan", "constexpr int PRIMITIVE_FAN = 4;", "constexpr int PRIMITIVE_FAN = 3;"),
    (M, "M draw width", "    const float width = m_screenWidth * (1.0f / 1024.0f) * DRAWN_SIZE;",
     "    const float width = m_screenWidth * (1.0f / 768.0f) * DRAWN_SIZE;"),
    (M, "M draw height", "    const float height = m_screenHeight * (1.0f / 768.0f) * DRAWN_SIZE;",
     "    const float height = m_screenHeight * (1.0f / 1024.0f) * DRAWN_SIZE;"),
    (M, "M draw viewport", "    S_IGraphViewport viewport = {0, 0, graph->ScrnX(), GetIGraph()->ScrnY(), 0.0f, 1.0f};",
     "    S_IGraphViewport viewport = {0, 0, graph->ScrnX(), GetIGraph()->ScrnY(), 0.0f, 0.0f};"),
    (M, "M draw uv1", "{right, y, 0.0f, 1.0f}", "{right, y, 1.0f, 0.0f}"),
    (M, "M draw uv3", "{x, bottom, 1.0f, 0.0f}", "{x, bottom, 0.0f, 1.0f}"),
    (M, "M draw corner2", "{right, bottom, 1.0f, 1.0f}", "{right, y, 1.0f, 1.0f}"),
    (M, "M draw texture", "    GetIGraph()->SetTexture(m_texture, 0);", "    GetIGraph()->SetTexture(m_texture, 1);"),
    (M, "M draw order", "    GetIGraph()->BeginScene();\n    GetIGraph()->SetViewport(viewport);",
     "    GetIGraph()->SetViewport(viewport);\n    GetIGraph()->BeginScene();"),
    (M, "M draw end", "    GetIGraph()->EndScene();\n", "\n"),
    (M, "M draw count", "    GetIGraph()->DrawPrimitives(PRIMITIVE_FAN, 2, g_cursorQuad, 1);",
     "    GetIGraph()->DrawPrimitives(PRIMITIVE_FAN, 4, g_cursorQuad, 1);"),
    (M, "M draw place", "    const float x = g_cursorX, y = g_cursorY;", "    const float x = g_cursorY, y = g_cursorX;"),
]
