// Part 9c-vi-a's harness: the menus' cursor (game/cursor.cpp) alone, on fake engine objects (an
// IGraph, a scene, the "CURZOR" frame, the texture: COM-like tables of stdcall functions logging
// their calls). Prints "cursor:" with 1 per check met (check_cursor.py reads it; the arguments, the
// mutation runner's models, are ignored).
#define private public
#define protected public
#include "game/cursor.h"
#undef private
#undef protected

#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    g_log.push_back(line);
}
bool LogIs(const std::vector<std::string> &lines) { return g_log == lines; }
std::string F(float v)
{
    char s[32];
    std::snprintf(s, sizeof s, "%.9g", static_cast<double>(v));
    return s;
}

constexpr int SLOTS = 96;

// IGraph: the screen 640 x 480; the mouse's place, moves, wheel, buttons set by the test.
struct S_Graph {
    void **vtable;
    int mouseX = 300, mouseY = 200, dx = 0, dy = 0, wheel = 0;
    uint32_t buttons = 0;
    HRESULT textureResult = 0;
} g_graph;
void *g_graphVt[SLOTS];
int __stdcall F_ScrnX(S_Graph *) { return 640; }
int __stdcall F_ScrnY(S_Graph *) { return 480; }
int __stdcall F_DX(S_Graph *g) { return g->dx; }
int __stdcall F_DY(S_Graph *g) { return g->dy; }
int __stdcall F_Wheel(S_Graph *g) { return g->wheel; }
int __stdcall F_SetMouse(S_Graph *, int x, int y)
{
    Log("setmouse %d %d", x, y);
    return 0;
}
int __stdcall F_MouseX(S_Graph *g) { return g->mouseX; }
int __stdcall F_MouseY(S_Graph *g) { return g->mouseY; }
uint32_t __stdcall F_Buttons(S_Graph *g) { return g->buttons; }
// The drawing (part 9d-ii-b's Draw): logged, the vertices kept.
struct S_DrawVertex {
    float x, y, z, rhw;
    uint32_t color, specular;
    float u, v;
};
S_DrawVertex g_drawn[4];
bool __stdcall F_BeginScene(S_Graph *)
{
    Log("begin");
    return true;
}
void __stdcall F_EndScene(S_Graph *) { Log("end"); }
void __stdcall F_SetViewport(S_Graph *, const S_IGraphViewport &v)
{
    Log("viewport %d %d %d %d %g %g", v.x, v.y, v.width, v.height, v.minZ, v.maxZ);
}
void __stdcall F_SetTexture(S_Graph *, void *texture, int stage) { Log("settexture %d %d", texture != nullptr, stage); }
void __stdcall F_Draw(S_Graph *, int kind, int count, const void *vertices, int transformed)
{
    Log("draw %d %d %d", kind, count, transformed);
    std::memcpy(g_drawn, vertices, sizeof g_drawn);
}
struct S_Image {
    void **vtable;
} g_image;
void *g_imageVt[SLOTS];
uint32_t __stdcall F_ImageRelease(S_Image *)
{
    Log("texture release");
    return 0;
}
HRESULT __stdcall F_LoadTexture(S_Graph *g, const char *name, const char *alpha, uint32_t mode, IImage **out)
{
    Log("texture %s %d %x", name, alpha != nullptr, mode);
    *out = reinterpret_cast<IImage *>(&g_image);
    return g->textureResult;
}

// The frames: a real I3D_frame's fields (data, matrices) behind a logging table.
void *g_frameVt[SLOTS];
uint32_t __stdcall F_FrameRelease(I3D_frame *) { return 0; }
void __stdcall F_SetPos(I3D_frame *, const S_vector &p)
{
    Log("setpos %s %s %s", F(p.x).c_str(), F(p.y).c_str(), F(p.z).c_str());
}
void __stdcall F_Update(I3D_frame *) { Log("update"); }
void __stdcall F_SetOn(I3D_frame *, bool on) { Log("on %d", on); }
I3D_frame *NewFrame(uint32_t data)
{
    auto *buf = new (std::align_val_t(16)) unsigned char[0x300]();
    *reinterpret_cast<void ***>(buf) = g_frameVt;
    auto *f = reinterpret_cast<I3D_frame *>(buf);
    f->SetData(reinterpret_cast<S_FrameData *>(static_cast<uintptr_t>(data)));
    return f;
}

// The scene: its "CURZOR" frame (or none); a ray's hit (logged).
struct S_Scene {
    void **vtable;
    I3D_frame *cursor = nullptr;
    I3D_frame *hit = nullptr;
} g_scene;
void *g_sceneVt[2 * SLOTS];
I3D_frame *__stdcall F_FindFrame(S_Scene *s, const char *name, uint32_t mask)
{
    Log("find %s %x", name, mask);
    return s->cursor;
}
I3D_frame *__stdcall F_Intersect(S_Scene *s, const S_vector &from, const S_vector &dir, I3D_frame *frame,
                                 void *info, uint32_t flags, int a, int b, int c)
{
    Log("ray %s %s %s %s %s %s %p %d %x %d %d %d", F(from.x).c_str(), F(from.y).c_str(), F(from.z).c_str(),
        F(dir.x).c_str(), F(dir.y).c_str(), F(dir.z).c_str(), static_cast<void *>(frame), info != nullptr, flags, a, b, c);
    return s->hit;
}

} // namespace

// The heap: fresh memory filled (no zero by luck), the deletions counted.
int g_deletes;
void *operator new(std::size_t size)
{
    void *p = std::malloc(size ? size : 1);
    std::memset(p, 0xA5, size);
    return p;
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t size) noexcept
{
    if (p && size == sizeof(C_Cursor))
        ++g_deletes;
    std::free(p);
}

extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graph); }

int main()
{
    g_graph.vtable = g_graphVt;
    g_graphVt[0x18 / 4] = reinterpret_cast<void *>(&F_ScrnX);
    g_graphVt[0x1C / 4] = reinterpret_cast<void *>(&F_ScrnY);
    g_graphVt[0x3C / 4] = reinterpret_cast<void *>(&F_LoadTexture);
    g_graphVt[0xEC / 4] = reinterpret_cast<void *>(&F_DX);
    g_graphVt[0xF0 / 4] = reinterpret_cast<void *>(&F_DY);
    g_graphVt[0xF4 / 4] = reinterpret_cast<void *>(&F_Wheel);
    g_graphVt[0xF8 / 4] = reinterpret_cast<void *>(&F_SetMouse);
    g_graphVt[0xFC / 4] = reinterpret_cast<void *>(&F_MouseX);
    g_graphVt[0x100 / 4] = reinterpret_cast<void *>(&F_MouseY);
    g_graphVt[0x104 / 4] = reinterpret_cast<void *>(&F_Buttons);
    g_graphVt[0x68 / 4] = reinterpret_cast<void *>(&F_BeginScene);
    g_graphVt[0x6C / 4] = reinterpret_cast<void *>(&F_EndScene);
    g_graphVt[0x7C / 4] = reinterpret_cast<void *>(&F_SetViewport);
    g_graphVt[0x44 / 4] = reinterpret_cast<void *>(&F_SetTexture);
    g_graphVt[0x88 / 4] = reinterpret_cast<void *>(&F_Draw);
    g_image.vtable = g_imageVt;
    g_imageVt[0] = reinterpret_cast<void *>(&F_ImageRelease);
    g_frameVt[0] = reinterpret_cast<void *>(&F_FrameRelease);
    g_frameVt[0x08 / 4] = reinterpret_cast<void *>(&F_SetPos);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(&F_Update);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(&F_SetOn);
    g_scene.vtable = g_sceneVt;
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(&F_FindFrame);
    g_sceneVt[0x104 / 4] = reinterpret_cast<void *>(&F_Intersect);
    auto *scene = reinterpret_cast<I3D_scene *>(&g_scene);

    bool make = true, update = true, clamp = true, clicks = true, query = true, pick = true, draw = true;
#define CHECK(flag, cond)                                                                                              \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            flag = false;                                                                                              \
            std::printf("  failed line %d: %s\n", __LINE__, #cond);                                                    \
        }                                                                                                              \
    } while (0)

    // --- made: no frame, no texture: none; then the shared one, counted ---
    CHECK(make, g_cursorX == -1.0f && g_cursorY == -1.0f);
    int deletes = g_deletes;
    CHECK(make, C_Cursor::Acquire(scene) == nullptr && LogIs({"find CURZOR ffff"}) && g_deletes == deletes + 1);
    I3D_frame *frame = NewFrame(0);
    g_scene.cursor = frame;
    g_graph.textureResult = -1;
    g_log.clear();
    deletes = g_deletes;
    CHECK(make, C_Cursor::Acquire(scene) == nullptr && g_log.size() == 6 && g_log.back() == "texture x_kursor.tga 0 402" &&
                    g_deletes == deletes + 1);
    g_graph.textureResult = 0;
    g_cursorX = g_cursorY = -1.0f;
    g_graph.dx = 3;
    g_graph.dy = -2;
    g_graph.wheel = 120;
    g_log.clear();
    C_Cursor *c = C_Cursor::Acquire(scene);
    // The mouse to the screen's centre (no place yet), read; the frame placed and off.
    const std::string place200 = "setpos " + F(300.0f / 640.0f) + " " +
                                 F(static_cast<float>(-1.0f / 480.0f * 200.0f * 0.75)) + " -1";
    CHECK(make, c && c->m_refs == 1 && c->m_frame == frame && c->m_texture == reinterpret_cast<IImage *>(&g_image) &&
                    LogIs({"find CURZOR ffff", "setmouse 320 240", place200, "update", "on 0", "texture x_kursor.tga 0 402"}));
    CHECK(make, g_cursorX == 300.0f && g_cursorY == 200.0f && c->m_moveX == 3.0f && c->m_moveY == -2.0f &&
                    c->m_wheel == 120.0f && c->m_screenWidth == 640.0f && c->m_screenHeight == 480.0f && c->m_stillTime == 0 &&
                    c->m_hoveredId == 0 && c->m_wheelTotal == 0.0f && !c->m_clicksOff && c->m_delay == 0 &&
                    c->m_clickedId == 0 && c->m_pressedId == 0 && c->m_pressTime == 0 && c->m_pickedId == 0);
    CHECK(make, C_Cursor::Acquire(scene) == c && c->m_refs == 2);
    g_log.clear();
    c->Release();
    CHECK(make, c->m_refs == 1 && g_log.empty());
    deletes = g_deletes;
    c->Release();
    CHECK(make, LogIs({"texture release"}) && g_deletes == deletes + 1);
    // Again: the mouse to its last place.
    g_graph.mouseX = 310;
    g_log.clear();
    c = C_Cursor::Acquire(scene);
    CHECK(make, c && g_log.size() == 6 && g_log[1] == "setmouse 300 200" && g_cursorX == 310.0f);
    g_cursorY = -1.0f;
    C_Cursor::Acquire(scene);
    c->Release();
    c->Release();
    g_log.clear();
    c = C_Cursor::Acquire(scene);
    CHECK(make, g_log[1] == "setmouse 320 240");
    g_cursorX = -1.0f;
    g_cursorY = 200.0f;
    c->Release();
    g_log.clear();
    c = C_Cursor::Acquire(scene);
    CHECK(make, g_log[1] == "setmouse 320 240");
    g_cursorX = 300.7f;
    g_cursorY = 199.9f;
    c->Release();
    g_log.clear();
    c = C_Cursor::Acquire(scene);
    CHECK(make, g_log[1] == "setmouse 300 199");

    // --- the update: a delay skips it; nothing changed: false; a move: the place, the frame ---
    g_cursorX = 300.0f;
    g_cursorY = 200.0f;
    g_graph.dx = g_graph.dy = g_graph.wheel = 0;
    c->m_wheel = 0.0f;
    c->SetDelay(50);
    g_log.clear();
    CHECK(update, !c->Update(20) && c->m_delay == 30 && g_log.empty() && c->m_stillTime == 20 && c->m_pressTime == 20);
    g_graph.dx = 5;
    CHECK(update, !c->Update(40) && c->m_delay == -10 && g_cursorX == 300.0f && c->m_moveX == 5.0f);
    g_graph.dx = 0;
    CHECK(update, !c->Update(10) && c->m_stillTime == 70 && g_log.empty());
    // A delay ending at 0: the next update not skipped; at 1: skipped.
    c->SetDelay(20);
    CHECK(update, !c->Update(20) && c->m_delay == 0);
    g_graph.dx = 1;
    CHECK(update, c->Update(1) && g_cursorX == 301.0f);
    c->SetDelay(1);
    CHECK(update, !c->Update(1) && g_cursorX == 301.0f && c->m_delay == 0);
    g_graph.dx = -1;
    CHECK(update, c->Update(1) && g_cursorX == 300.0f);
    g_graph.dx = 0;
    g_log.clear();
    g_graph.dx = 7;
    g_graph.dy = -3;
    g_graph.wheel = 2;
    CHECK(update, c->Update(10) && g_cursorX == 307.0f && g_cursorY == 197.0f && c->m_stillTime == 0 &&
                      c->m_wheelTotal == 2.0f && c->m_wheel == 2.0f);
    CHECK(update, LogIs({"setpos " + F(307.0f / 640.0f) + " " + F(static_cast<float>(-1.0f / 480.0f * 197.0f * 0.75)) + " -1",
                         "update"}));
    // Each change alone: the move y, the wheel, the buttons, the picked id.
    g_graph.dx = 0;
    g_graph.wheel = 0;
    CHECK(update, c->Update(1) && g_cursorY == 194.0f);
    g_graph.dy = 0;
    g_graph.wheel = -1;
    CHECK(update, c->Update(1) && c->m_wheelTotal == 1.0f && g_cursorX == 307.0f);
    g_graph.wheel = 0;
    CHECK(update, !c->Update(1));
    g_graph.buttons = 4;
    CHECK(update, c->Update(1) && c->m_buttons == 4 && c->m_lastButtons == 0);
    CHECK(update, !c->Update(1) && c->m_lastButtons == 4);
    g_graph.buttons = 0;
    CHECK(update, c->Update(1));
    c->m_pickedId = 9;
    CHECK(update, c->Update(1) && c->m_hoveredId == 9 && c->m_stillTime == 0);
    CHECK(update, !c->Update(5) && c->m_stillTime == 5 && c->GetStillTime() == 5);
    // Kept on the screen: the move cut.
    g_graph.dx = 400;
    CHECK(clamp, c->Update(1) && g_cursorX == 640.0f && c->m_moveX == 400.0f - 67.0f);
    g_graph.dx = -700;
    CHECK(clamp, c->Update(1) && g_cursorX == 0.0f && c->m_moveX == -640.0f);
    g_graph.dx = 0;
    g_graph.dy = 300;
    CHECK(clamp, c->Update(1) && g_cursorY == 480.0f && c->m_moveY == 286.0f);
    g_graph.dy = -500;
    CHECK(clamp, c->Update(1) && g_cursorY == 0.0f && c->m_moveY == -480.0f);
    g_graph.dy = 640;
    g_graph.dx = 640;
    CHECK(clamp, c->Update(1) && g_cursorY == 480.0f && g_cursorX == 640.0f && c->m_moveX == 640.0f && c->m_moveY == 480.0f);
    // A place just off 0 kept.
    g_cursorX = 0.125f;
    g_cursorY = 0.125f;
    g_graph.dx = 0;
    g_graph.dy = 0;
    c->m_pickedId = 3;
    CHECK(clamp, c->Update(1) && g_cursorX == 0.125f && g_cursorY == 0.125f && c->m_moveX == 0.0f && c->m_moveY == 0.0f);
    g_graph.dx = g_graph.dy = 0;

    // --- the clicks: pressed over a control, released over it: a click; again within 400 ms: a
    // double; elsewhere: none ---
    c->m_pickedId = 5;
    g_graph.buttons = 1;
    c->Update(1);
    CHECK(clicks, c->m_pressedId == 5 && !c->IsClicked());
    g_graph.buttons = 0;
    c->Update(100);
    CHECK(clicks, c->IsClicked() && !c->IsDoubleClicked() && c->m_clickedId == 5 && c->m_pressedId == 0 && c->m_pressTime == 0);
    g_graph.buttons = 1;
    c->Update(50);
    g_graph.buttons = 0;
    c->Update(349);
    CHECK(clicks, c->IsClicked() && c->IsDoubleClicked() && c->m_clickedId == 0);
    g_graph.buttons = 2;
    c->Update(10);
    g_graph.buttons = 0;
    c->Update(10);
    CHECK(clicks, c->IsClicked() && !c->IsDoubleClicked() && c->m_clickedId == 5);
    // The flags last one update.
    c->m_pickedId = 6;
    c->Update(10);
    CHECK(clicks, !c->IsClicked() && !c->IsDoubleClicked());
    // A second click too late: none double.
    g_graph.buttons = 1;
    c->Update(10);
    g_graph.buttons = 0;
    c->Update(10);
    g_graph.buttons = 1;
    c->Update(10);
    g_graph.buttons = 0;
    c->Update(400);
    CHECK(clicks, c->IsClicked() && !c->IsDoubleClicked() && c->m_clickedId == 6);
    g_graph.buttons = 1;
    c->Update(10);
    g_graph.buttons = 0;
    c->Update(389);
    CHECK(clicks, c->IsDoubleClicked());
    // The double click's flag lasts one update; a second click at 400 ms: none.
    c->m_pickedId = 8;
    c->Update(1);
    CHECK(clicks, !c->IsDoubleClicked() && !c->IsClicked());
    g_graph.buttons = 1;
    c->Update(10);
    g_graph.buttons = 0;
    c->Update(10);
    g_graph.buttons = 1;
    c->Update(10);
    g_graph.buttons = 0;
    c->Update(390);
    CHECK(clicks, c->IsClicked() && !c->IsDoubleClicked() && c->m_clickedId == 8);
    c->m_pickedId = 6;
    // Released over another: no click (the last click's id kept).
    g_graph.buttons = 1;
    c->Update(10);
    c->m_pickedId = 7;
    g_graph.buttons = 0;
    c->Update(10);
    CHECK(clicks, !c->IsClicked() && !c->IsDoubleClicked() && c->m_clickedId == 8 && c->m_pressedId == 0);
    // The clicks off: none tracked.
    c->SetClicksOff(true);
    g_graph.buttons = 1;
    c->Update(10);
    g_graph.buttons = 0;
    c->Update(10);
    CHECK(clicks, c->m_clicksOff && !c->IsClicked() && c->m_pressedId == 0 && c->m_pressTime == 20);
    c->SetClicksOff(false);

    // --- the queries ---
    c->m_buttons = 6;
    c->m_lastButtons = 2;
    CHECK(query, c->IsButtonDown(0) && c->IsButtonDown(4) && !c->IsButtonDown(1) && c->WasButtonDown(2) && !c->WasButtonDown(4) &&
                     c->IsPressed(4) && !c->IsPressed(2) && !c->IsReleased(2) && c->IsPressed(0) == false);
    c->m_buttons = 0;
    CHECK(query, !c->IsButtonDown(0) && c->IsReleased(2) && c->IsReleased(0) && !c->IsReleased(4) && !c->IsPressed(0));
    c->m_lastButtons = 0;
    CHECK(query, !c->WasButtonDown(0) && !c->IsReleased(0));
    g_cursorX = 160.0f;
    g_cursorY = 100.0f;
    S_vector p;
    c->GetPosition(&p);
    CHECK(query, p.x == 0.25f && p.y == static_cast<float>(-(100.0f / 480.0f) * 0.75) && p.z == -1.0f);
    c->m_moveX = 32.0f;
    c->m_moveY = -48.0f;
    p = S_vector(5.0f, 5.0f, 5.0f);
    c->GetMove(&p);
    CHECK(query, p.x == 0.05f && p.y == static_cast<float>(-(-48.0f / 480.0f) * 0.75) && p.z == -1.0f);
    float x = 0.0f, y = 0.0f, b = 0.0f;
    c->m_buttons = 0x40400000;
    c->GetScreenPosition(&x, &y, &b);
    CHECK(query, x == 160.0f && y == 100.0f && b == 3.0f);
    c->GetScreenPosition(nullptr, nullptr, nullptr);
    CHECK(query, c->HasMoved());
    c->m_moveX = 0.0f;
    CHECK(query, c->HasMoved());
    c->m_moveY = 0.0f;
    CHECK(query, !c->HasMoved());
    c->m_moveX = 1.0f;
    CHECK(query, c->HasMoved());
    c->m_wheel = 0.0f;
    CHECK(query, !c->HasWheel() && c->GetWheel() == 0.0f);
    c->m_wheel = -2.0f;
    CHECK(query, c->HasWheel() && c->GetWheel() == -2.0f);
    c->m_hoveredId = 11;
    CHECK(query, c->GetHoveredId() == 11);

    // --- the pick: a ray from the place (z -0.9) along +z under the frame given ---
    I3D_frame *under = NewFrame(0);
    g_scene.hit = NewFrame(0x1C100002);
    g_log.clear();
    const uint32_t picked = c->Pick(scene, under);
    char ray[200];
    std::snprintf(ray, sizeof ray, "ray 0.25 %s %s 0 0 1 %p 1 140000 0 0 0", F(static_cast<float>(-(100.0f / 480.0f) * 0.75)).c_str(),
                  F(-1.0f + 0.1f).c_str(), static_cast<void *>(under));
    CHECK(pick, LogIs({ray}) && c->m_pickedId == 0x1C100002 && picked == 0x1C100002);
    g_scene.hit = nullptr;
    CHECK(pick, c->Pick(scene, under) == 0 && c->m_pickedId == 0);

    // --- drawn: its texture on a fan of two triangles from the place, 30 pixels of a 1024 x 768 screen
    // scaled to the screen (640 x 480): 18.75 x 18.75; the whole screen's viewport ---
    {
        g_cursorX = 100.0f;
        g_cursorY = 50.5f;
        g_log.clear();
        c->Draw();
        CHECK(draw, LogIs({"begin", "viewport 0 0 640 480 0 1", "settexture 1 0", "draw 4 2 1", "end"}));
        const float w = 640.0f * (1.0f / 1024.0f) * 30.0f, h = 480.0f * (1.0f / 768.0f) * 30.0f;
        const float expect[4][4] = {{100.0f, 50.5f, 0.0f, 0.0f}, {w + 100.0f, 50.5f, 0.0f, 1.0f},
                                    {w + 100.0f, h + 50.5f, 1.0f, 1.0f}, {100.0f, h + 50.5f, 1.0f, 0.0f}};
        for (int i = 0; i < 4; ++i) {
            const S_DrawVertex &v = g_drawn[i];
            CHECK(draw, v.x == expect[i][0] && v.y == expect[i][1] && v.u == expect[i][2] && v.v == expect[i][3] &&
                            v.z == 0.0284113996f && v.rhw == 35.1971397f && v.color == 0xFFFFFFFFu && v.specular == 0);
        }
        CHECK(draw, w == 18.75f && h == 18.75f);
    }

    // Released: the texture; the next one new.
    g_log.clear();
    c->Release();
    CHECK(make, LogIs({"texture release"}));

    std::printf("cursor: cuMake %d cuUpdate %d cuClamp %d cuClicks %d cuQuery %d cuPick %d cuDraw %d\n", make, update, clamp,
                clicks, query, pick, draw);
    return 0;
}
