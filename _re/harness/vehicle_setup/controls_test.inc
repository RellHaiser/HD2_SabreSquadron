// Part 9c-ii's small controls (game/controls.cpp), in the text build, run by CameraTest after
// ControlTest: the frames GfFrame's / GfVisual's (on / update / find / setdir / setpos / alpha
// logged), the boxes' Render logged by a box subclass put in after Create, the cursor (part 9c-vi)
// faked, a picture's mesh, level, face groups, materials and textures faked. Prints "controls:"
// with 1 per check met.

#define private public
#define protected public
#include "game/controls.h"
#include "game/cursor.h"
#include "game/mp_zone.h"
#include "game/paged_text_box.h"
#undef protected
#undef private

// The cursor (part 9c-vi) faked: one shared state.
struct S_CsCursor {
    int refs = 0;
    uint32_t buttons = 0;
    int still = 0;
    S_vector position;
    S_vector move;
    float screenX = 0.0f, screenY = 0.0f;
    // (Room for the real cursor's fields the inline methods write: SetClicksOff's, part 9d-ii-a.)
    uint8_t room[0x80] = {};
} g_csCursor;
C_Cursor *C_Cursor::Acquire(I3D_scene *scene)
{
    GfLog("cursor acquire %d", scene != nullptr);
    ++g_csCursor.refs;
    return reinterpret_cast<C_Cursor *>(&g_csCursor);
}
void C_Cursor::Release()
{
    GfLog("cursor release");
    --g_csCursor.refs;
}
bool C_Cursor::IsButtonDown(uint32_t buttons) const
{
    return buttons == 0 ? g_csCursor.buttons != 0 : (g_csCursor.buttons & buttons) != 0;
}
void C_Cursor::GetPosition(S_vector *position) const { *position = g_csCursor.position; }
int C_Cursor::GetStillTime() const { return g_csCursor.still; }
void C_Cursor::GetMove(S_vector *move) const { *move = g_csCursor.move; }
void C_Cursor::GetScreenPosition(float *x, float *y, float *unknown30) const
{
    if (x)
        *x = g_csCursor.screenX;
    if (y)
        *y = g_csCursor.screenY;
    if (unknown30)
        *unknown30 = 0.0f;
}

void ScrollTest(I3D_scene *scene, int st);
void CheckTest(I3D_scene *scene);
void EditTest(I3D_scene *scene);
void ComboTest(I3D_scene *scene);
void InfoTest(I3D_scene *scene);
void ListTest(I3D_scene *scene);
void BarTest(I3D_scene *scene);
void ServerTest(I3D_scene *scene);
void HotKeyTest(I3D_scene *scene);
void GroupsTest(I3D_scene *scene);
void MovieTest(I3D_scene *scene);
void FactoryTest(I3D_scene *scene);
void ScreenTest(I3D_scene *scene);
void MenuTest(I3D_scene *scene);
void MenuRunTest(I3D_scene *scene);
void MenuSoldierTest(I3D_scene *scene);
void EquipmentPanelTest(I3D_scene *scene);

namespace {

struct S_ControlsResults {
    bool csText = true, csTextMsgs = true, csTitle = true, csLabel = true, csPicture = true, csPicMsgs = true;
    bool csGroup = true, csGroupInput = true, csSwitch = true, csTextButton = true, csTooltip = true, csButton = true;
    bool csButtonHeld = true, csPaged = true, csPages = true, csHandled = true;
};

// Every message handled by a statement returned true.
bool g_csHandled = true;
// A callback for every notification: "notify <code> <value>".
bool CsNotified(void *, void *, C_Control *, uint32_t notification, uint32_t value)
{
    GfLog("notify %x %u", notification, value);
    return true;
}

// A box logging its render and its deletion ("box <name> ...").
struct C_CsBox : C_TextBox {
    const char *name;
    explicit C_CsBox(const char *n) : name(n) {}
    ~C_CsBox() override { GfLog("box %s deleted", name); }
    void Render() override { GfLog("box %s render", name); }
};
struct C_CsPagedBox : C_PagedTextBox {
    const char *name;
    explicit C_CsPagedBox(const char *n) : name(n) {}
    ~C_CsPagedBox() override { GfLog("box %s deleted", name); }
    void Render() override { GfLog("box %s render", name); }
};

// A child logging its refresh and the input passed to it ("<name> <slot> <id>"), returning g_csReturn.
int g_csReturn = 5;
struct C_CsChild : C_Control {
    const char *name;
    C_CsChild(const char *n, uint32_t id) : C_Control(nullptr, id, nullptr), name(n) {}
    void Refresh() override { GfLog("%s refresh", name); }
    void Tick(int time) override { GfLog("%s tick %d", name, time); }
    bool Unknown20(int value) override
    {
        GfLog("%s u20 %d", name, value);
        return g_csReturn != 0;
    }
    int Log(const char *slot, int id)
    {
        GfLog("%s %s %x", name, slot, id);
        return g_csReturn;
    }
    int OnEnter(int id) override { return Log("enter", id); }
    int OnLeave(int id) override { return Log("leave", id); }
    int OnClick(int id) override { return Log("click", id); }
    int OnInput02(int id) override { return Log("in02", id); }
    int OnInput05(int id) override { return Log("in05", id); }
    int OnRelease(int id) override { return Log("release", id); }
    int OnPress(int id) override { return Log("press", id); }
    int OnWheel(int id, int amount) override
    {
        GfLog("%s wheel %x %d", name, id, amount);
        return g_csReturn;
    }
    int OnInput08(int id) override { return Log("in08", id); }
    int OnInput09(int id) override { return Log("in09", id); }
    int OnInput0A(int id) override { return Log("in0a", id); }
    int OnKeyClick(int id) override { return Log("key", id); }
    int OnInput13(int id) override { return Log("in13", id); }
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("%s message %x %x", name, m->target, m->id);
        m->value = 0x77;
        return g_csReturn != 0;
    }
};

// A visual for the boxes: its world box (0.25, -0.5) .. (0.75, -0.125), up to date.
I3D_frame *CsVisual(const char *name, I3D_frame *parent = nullptr)
{
    I3D_frame *v = GfVisual(name, I3DVISUAL_OBJECT, parent);
    WmField<uint32_t>(v, 0x1D0) |= VISFLAGS_WBOUND_VALID;
    I3D_bbox box;
    box.min = S_vector(0.25f, -0.5f, 0.0f);
    box.max = S_vector(0.75f, -0.125f, 0.0f);
    box.minW = box.maxW = 0.0f;
    WmField<I3D_bbox>(v, 0x170) = box;
    return v;
}
S_ControlMessage CsMessage(uint32_t id, uint32_t value = 0, uintptr_t value2 = 0, const char *text = nullptr, uint32_t target = 0)
{
    S_ControlMessage m;
    m.target = target;
    m.id = id;
    m.value = value;
    m.value2 = value2;
    if (text)
        m.text = C_str(text);
    return m;
}
// The box's runs: "text/style ...".
std::string CsRuns(const C_TextBox &box)
{
    std::string s;
    for (const S_TextRun &r : box.m_runs)
        s += CmText("%s/%d ", r.text.c_str(), r.style);
    return s;
}
// The box placed on the CsVisual's box (800 x 600, a margin of `margin` / 1024 of the width).
bool CsPlaced(const C_TextBox &box, int margin)
{
    const float m = static_cast<float>(static_cast<int>(800.0f * (1.0f / 1024.0f) * static_cast<float>(margin)));
    return box.m_placed && box.m_left == 800.0f * 0.25f + m && box.m_right == 800.0f * 0.75f - m;
}

// --- the picture's mesh: a level of face groups (each a material with a texture) ---
struct S_CsTexture {
    void **vtable;
    uint8_t pad[0x14];
    const char *name;                                   // +0x18
};
struct S_CsMaterial {
    void **vtable;
    int refs = 1;
    uint32_t flags = 0;                                 // +0x08
    S_CsTexture *texture = nullptr;                     // +0x0C
    const char *name;
};
struct S_CsGroup {
    void **vtable;
    S_CsMaterial *material;                             // +0x04
    int faces = 0;
    S_CsGroup *next = nullptr;                          // +0x0C
};
struct S_CsLevel {
    void **vtable;
    uint8_t pad[0x58];
    S_CsGroup *groups = nullptr;                        // +0x5C
};
struct S_CsMesh {
    void **vtable;
    S_CsLevel *level;
};
void *g_csTextureVt[4], *g_csMaterialVt[20], *g_csMeshVt[4];
const char *CsTexName(const void *t) { return t ? static_cast<const S_CsTexture *>(t)->name : "-"; }
uint32_t __stdcall F_CsTexRelease(S_CsTexture *t)
{
    GfLog("texture release %s", t->name);
    return 0;
}
void __stdcall F_CsSetTexture(S_CsMaterial *m, S_CsTexture *t)
{
    GfLog("settexture %s %s", m->name, CsTexName(t));
    m->texture = t;
}
void __stdcall F_CsResolve(S_CsMaterial *m)
{
    GfLog("resolve %s", m->name);
    m->flags &= ~I3DMATERIAL_TEXTURE_PENDING_BIT;
}
S_CsLevel *__stdcall F_CsGetLevel(S_CsMesh *mesh, int index)
{
    GfLog("getlevel %d", index);
    return mesh->level;
}
S_CsTexture g_csTexA, g_csTexB, g_csTexC, g_csLoaded;
HRESULT __stdcall F_CsLoadTexture(void *, const char *name, const char *alpha, uint32_t mode, void **out)
{
    GfLog("load %s %d %x", name, alpha != nullptr, mode);
    if (std::strcmp(name, "good.bmp") != 0)
        return -1;
    *out = &g_csLoaded;
    return 0;
}

} // namespace

void ControlsTest()
{
    S_ControlsResults r;
    g_controlMessages.clear();
    const bool oldIds = g_textIds;
    g_textIds = true;
    void *const oldLoad = g_igraphVtable[0x3C / 4], *const oldX = g_igraphVtable[0x18 / 4], *const oldY = g_igraphVtable[0x1C / 4];
    g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_CsLoadTexture);
    g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&F_TbScrnX);
    g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&F_TbScrnY);
    g_csTextureVt[0] = reinterpret_cast<void *>(&F_CsTexRelease);
    for (void *&slot : g_csMaterialVt)
        slot = nullptr;
    g_csMaterialVt[0x1C / 4] = reinterpret_cast<void *>(&F_CsSetTexture);
    g_csMaterialVt[0x44 / 4] = reinterpret_cast<void *>(&F_CsResolve);
    g_csMeshVt[1] = reinterpret_cast<void *>(&F_CsGetLevel);
    I3D_scene *const scene = GetGame()->GetScene();
    // A box kept for the test (the font manager not made again with each control's box) and the
    // fake font 3 first in its list, a style of it.
    C_TextBox keeper;
    C_TbFont font3;
    font3.m_id = 3;
    keeper.m_fonts->m_fonts.insert(keeper.m_fonts->m_fonts.begin(), &font3);
    const int st = TbStyle(0.0f, 1, 1);
    g_gfLog.clear();

    // --- the text control: its box made on Create (not placed); drawn then its children while
    // shown; the box deleted with it ---
    {
        I3D_frame *frame = CsVisual("txframe");
        auto *c = new C_TextControl(scene, 0x00100000, nullptr);
        WM(r.csText, !c->m_box && c->m_takesInput);
        WM(r.csText, c->Create(frame, frame, nullptr) && c->m_box && !c->m_box->m_placed && c->m_frame == frame);
        delete c->m_box;
        c->m_box = new C_CsBox("t");
        c->m_children = {new C_CsChild("k1", 0), new C_CsChild("k2", 0)};
        g_gfLog.clear();
        c->Refresh();
        WM(r.csText, GfLogIs({"box t render", "k1 refresh", "k2 refresh"}));
        c->m_shown = false;
        g_gfLog.clear();
        c->Refresh();
        WM(r.csText, g_gfLog.empty());
        c->m_shown = true;
        // Inactive: the box dimmed.
        c->SetInactive(true);
        WM(r.csText, c->m_inactive && c->m_box->m_dimmed);
        c->SetInactive(false);
        WM(r.csText, !c->m_inactive && !c->m_box->m_dimmed);
        g_gfLog.clear();
        C_Control *base = c;
        delete base;
        WM(r.csText, !g_gfLog.empty() && g_gfLog.front() == "box t deleted");
        // No box: refreshed, nothing.
        C_TextControl none(scene, 0, nullptr);
        none.m_children = {new C_CsChild("k3", 0)};
        g_gfLog.clear();
        none.Refresh();
        none.SetInactive(true);
        WM(r.csText, g_gfLog.empty() && none.m_inactive);
    }

    // --- the text control's messages: its box's ---
    {
        I3D_frame *frame = CsVisual("txmsg");
        C_TextControl c(scene, 0x00200000, nullptr);
        c.Create(frame, frame, nullptr);
        C_TextBox &box = *c.m_box;
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "a");
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_TEXT_ID, 4, 42);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, CsRuns(box) == "a/3 text 42/4 ");
        m = CsMessage(CONTROL_MSG_SET_TEXT, 5, 0, "b");
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_RUN_TEXT, 6, 1, "c");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, CsRuns(box) == "b/5 c/6 ");
        m = CsMessage(CONTROL_MSG_SET_TEXT_ID, 7, 43);
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_GET_RUNS, 99);
        WM(r.csTextMsgs, CsRuns(box) == "text 43/7 c/6 " && c.HandleMessage(&m) && m.value == 2);
        m = CsMessage(CONTROL_MSG_REMOVE_RUN, 0);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, CsRuns(box) == "c/6 ");
        m = CsMessage(CONTROL_MSG_CLEAR_TEXT);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, box.NumRuns() == 0);
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 1, 0, "d");
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_CLEAR | 0x80000000);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, box.NumRuns() == 0);
        m = CsMessage(CONTROL_MSG_SET_COLOUR, 0x11223344);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, box.m_colour == 0x11223344 && !box.m_placed);
        // Placed: on a refresh (the children not refreshed), a margin, facing front.
        c.m_children = {new C_CsChild("k1", 0)};
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, CsPlaced(box, 10) && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_SET_MARGIN, 0);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, box.m_margin == 0 && CsPlaced(box, 0));
        box.m_placed = false;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_FACE_FRONT);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, box.m_placed && GfLogIs({"setdir txmsg 0.00000 0.00000 1.00000 0.000000"}));
        // A route and the base's codes: the base's.
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_ADD_TEXT, 1, 0, "e");
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 33);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csTextMsgs, c.m_routes.size() == 1 && box.NumRuns() == 0 && c.m_value == 33);
    }

    // --- the title: its frame turned back, its id, updated; the box placed with a margin of 1; not
    // taking input ---
    {
        I3D_frame *frame = CsVisual("tiframe");
        C_TitleControl c(scene, 0x00300000, nullptr);
        WM(r.csTitle, !c.m_takesInput && !c.m_box);
        g_gfLog.clear();
        WM(r.csTitle, c.Create(frame, frame, nullptr) && GfLogIs({"setdir tiframe 0.00000 0.00000 -1.00000 0.000000", "update tiframe"}) &&
                          frame->GetData() == reinterpret_cast<S_FrameData *>(0x00300000) && c.m_box->m_margin == 1 &&
                          CsPlaced(*c.m_box, 1));
        frame->SetData(nullptr);
    }

    // --- the label: turned, placed (its margin kept); only its box drawn; its own text messages ---
    {
        I3D_frame *frame = CsVisual("lbframe");
        C_LabelControl c(scene, 0x00400000, nullptr);
        g_gfLog.clear();
        WM(r.csLabel, c.Create(frame, frame, nullptr) && GfLogIs({"setdir lbframe 0.00000 0.00000 -1.00000 0.000000", "update lbframe"}) &&
                          frame->GetData() == reinterpret_cast<S_FrameData *>(0x00400000) && c.m_box->m_margin == 10 &&
                          CsPlaced(*c.m_box, 10) && c.m_takesInput);
        frame->SetData(nullptr);
        delete c.m_box;
        c.m_box = new C_CsBox("l");
        c.m_children = {new C_CsChild("k1", 0)};
        g_gfLog.clear();
        c.Refresh();
        c.m_shown = false;
        c.Refresh();
        WM(r.csLabel, GfLogIs({"box l render"}));
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_TEXT, 2, 0, "x");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csLabel, CsRuns(*c.m_box) == "x/2 ");
        m = CsMessage(CONTROL_MSG_CLEAR);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csLabel, c.m_box->NumRuns() == 0);
        m = CsMessage(CONTROL_MSG_ADD_TEXT_ID, 2, 7);
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_NOTIFY_LEAVE | CONTROL_MSG_CLEAR);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csLabel, CsRuns(*c.m_box) == "text 7/2 " && c.m_routes.size() == 1);
    }

    // --- the picture: its visual's first face group and its texture; the box placed (margin 1);
    // faded while inactive ---
    S_CsMaterial matA, matB, matC;
    S_CsGroup groupA, groupB, groupC;
    S_CsLevel level;
    S_CsMesh mesh;
    {
        g_csTexA.vtable = g_csTexB.vtable = g_csTexC.vtable = g_csLoaded.vtable = g_csTextureVt;
        g_csTexA.name = "a.bmp";
        g_csTexB.name = "b.bmp";
        g_csTexC.name = "c.bmp";
        g_csLoaded.name = "good.bmp";
        matA.vtable = matB.vtable = matC.vtable = g_csMaterialVt;
        matA.name = "ma";
        matB.name = "mb";
        matC.name = "mc";
        matA.texture = &g_csTexA;
        matA.flags = I3DMATERIAL_TEXTURE_PENDING_BIT;
        matB.texture = &g_csTexB;
        matC.texture = &g_csTexC;
        groupA.material = &matA;
        groupB.material = &matB;
        groupC.material = &matC;
        groupA.next = &groupB;
        groupB.next = &groupC;
        level.groups = &groupA;
        mesh.vtable = g_csMeshVt;
        mesh.level = &level;
        I3D_frame *frame = CsVisual("picframe");
        WmField<void *>(frame, 0x1E0) = &mesh;
        C_PictureControl c(scene, 0x00500000, nullptr);
        g_gfLog.clear();
        WM(r.csPicture, c.Create(frame, frame, nullptr) && GfLogIs({"getlevel 0", "resolve ma"}) &&
                            c.m_faceGroup == reinterpret_cast<I3D_face_group *>(&groupA) &&
                            c.m_texture == reinterpret_cast<I3D_texture *>(&g_csTexA) && c.m_box->m_margin == 1 && CsPlaced(*c.m_box, 1));
        g_gfLog.clear();
        c.SetInactive(true);
        c.SetInactive(false);
        WM(r.csPicture, GfLogIs({"alpha picframe 0.400", "alpha picframe 1.000"}) && !c.m_inactive && !c.m_box->m_dimmed);
        // A face group without its material: no texture.
        groupA.material = nullptr;
        C_PictureControl d(scene, 0, nullptr);
        WM(r.csPicture, d.Create(frame, frame, nullptr) && !d.m_texture);
        groupA.material = &matA;
    }

    // --- the picture's messages: the texture given (shown) or none (hidden), the original back, one
    // loaded (not loaded: the original), another face group by its texture's name ---
    {
        I3D_frame *frame = CsVisual("picmsg");
        WmField<void *>(frame, 0x1E0) = &mesh;
        C_PictureControl c(scene, 0x00600000, nullptr);
        c.Create(frame, frame, nullptr);
        c.m_routes = {CsMessage(CONTROL_NOTIFY_SHOWN | 0x11, 0, 0, nullptr, 0xA0), CsMessage(CONTROL_NOTIFY_HIDDEN | 0x12, 0, 0, nullptr, 0xA1)};
        g_controlMessages.clear();
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_TEXTURE, 0, reinterpret_cast<uintptr_t>(&g_csTexC));
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, GfLogIs({"settexture ma c.bmp", "on picmsg 1", "update picmsg"}) && g_controlMessages.size() == 1 &&
                            g_controlMessages[0].id == 0x11);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_TEXTURE, 0, 0);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, GfLogIs({"on picmsg 0", "update picmsg", "settexture ma -"}) && g_controlMessages.size() == 2);
        g_controlMessages.clear();
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_RESTORE_TEXTURE);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, GfLogIs({"settexture ma a.bmp"}));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_LOAD_TEXTURE, 0, 0, "good.bmp");
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_LOAD_TEXTURE, 0, 0, "bad.bmp");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, GfLogIs({"load good.bmp 0 400", "settexture ma good.bmp", "texture release good.bmp", "load bad.bmp 0 400",
                                 "settexture ma a.bmp"}));
        // By name: the face group whose texture is named so (none: kept; no name: nothing).
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_FIND_FACE_GROUP, 0, 0, "c.bmp");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, c.m_faceGroup == reinterpret_cast<I3D_face_group *>(&groupC) &&
                            c.m_texture == reinterpret_cast<I3D_texture *>(&g_csTexC) && GfLogIs({"getlevel 0"}));
        m = CsMessage(CONTROL_MSG_FIND_FACE_GROUP, 0, 0, "zz.bmp");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, c.m_faceGroup == reinterpret_cast<I3D_face_group *>(&groupC));
        m = CsMessage(CONTROL_MSG_FIND_FACE_GROUP, 0, 0, "b.bmp");
        matB.flags = I3DMATERIAL_TEXTURE_PENDING_BIT;
        g_gfLog.clear();
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, c.m_faceGroup == reinterpret_cast<I3D_face_group *>(&groupB) &&
                            c.m_texture == reinterpret_cast<I3D_texture *>(&g_csTexB) && GfLogIs({"getlevel 0", "resolve mb"}));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_FIND_FACE_GROUP, 0, 0, "");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_RESTORE_TEXTURE);
        g_gfLog.clear();
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, GfLogIs({"settexture mb b.bmp"}));
        // SET_TEXTURE on a face group without its material: nothing.
        groupB.material = nullptr;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_TEXTURE, 0, reinterpret_cast<uintptr_t>(&g_csTexC));
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, g_gfLog.empty());
        groupB.material = &matB;
        // The refresh: the children's and the box placed; the text messages the text control's.
        c.m_children = {new C_CsChild("k1", 0)};
        c.m_box->m_placed = false;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, GfLogIs({"k1 message 0 200002c"}) && c.m_box->m_placed);
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 2, 0, "p");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPicMsgs, CsRuns(*c.m_box) == "p/2 ");
        g_controlMessages.clear();
    }

    // --- the group: the input to the child it is over (taking input), by the input's id ---
    {
        C_ControlGroup g(scene, 0x00100000, nullptr);
        WM(r.csGroup, g.m_focus == -1 && g.m_takesInput);
        auto *a = new C_CsChild("a", 0x00110000), *b = new C_CsChild("b", 0x00120000), *x = new C_CsChild("x", 0x00130000);
        x->m_takesInput = false;
        g.m_children = {a, b, x};
        g_gfLog.clear();
        g_csReturn = 0;
        WM(r.csGroup, g.OnClick(5) == 0 && g.OnInput02(5) == 0 && g.OnPress(5) == 0 && g.OnInput08(5) == 0 && g.OnInput09(5) == 0 &&
                          g.OnInput0A(5) == 0 && g.OnRelease(0x00120000) == 0 && g.OnWheel(5, 2) == CONTROL_NOT_HANDLED &&
                          g.OnKeyClick(5) == CONTROL_NOT_HANDLED && g.OnInput13(5) == CONTROL_NOT_HANDLED && g.OnLeave(5) == CONTROL_HANDLED &&
                          !g.Unknown20(1) && g_gfLog.empty());
        g.Tick(10);
        WM(r.csGroup, g_gfLog.empty());
        // The enter: the child with the id (taking input) the focus, its result.
        g_csReturn = 9;
        WM(r.csGroup, g.OnEnter(0x00130000) == 0 && g.m_focus == -1 && g.OnEnter(0x00140000) == 0 && g.m_focus == -1);
        WM(r.csGroup, g.OnEnter(0x00120678) == 9 && g.m_focus == 1 && GfLogIs({"b enter 120678"}));
        g_gfLog.clear();
        WM(r.csGroup, g.OnClick(3) == 9 && g.OnInput02(4) == 9 && g.OnPress(5) == 9 && g.OnWheel(6, 7) == 9 && g.OnInput08(8) == 9 &&
                          g.OnInput09(9) == 9 && g.OnInput0A(10) == 9 && g.OnKeyClick(11) == 9 && g.OnInput13(12) == 9);
        WM(r.csGroup, GfLogIs({"b click 3", "b in02 4", "b press 5", "b wheel 6 7", "b in08 8", "b in09 9", "b in0a a", "b key b",
                               "b in13 c"}));
        g_gfLog.clear();
        g.Tick(25);
        WM(r.csGroup, g.Unknown20(4) && GfLogIs({"b tick 25", "b u20 4"}));
        // Unknown20: the focus taking input; the enter's id 0xF asks it.
        b->m_takesInput = false;
        g_gfLog.clear();
        WM(r.csGroup, !g.Unknown20(4) && g_gfLog.empty());
        b->m_takesInput = true;
        g_gfLog.clear();
        WM(r.csGroup, g.OnEnter(0xF) == CONTROL_NOT_HANDLED && g.m_focus == 1 && GfLogIs({"b u20 15"}));
        g_csReturn = 0;
        WM(r.csGroup, g.OnEnter(0xF) == 0 && g.m_focus == 1);
        g_csReturn = 9;
        // The release: the focus' only when it is the child with the id.
        g_gfLog.clear();
        WM(r.csGroup, g.OnRelease(0x00110000) == 0 && g.OnRelease(0x00120000) == 9 && GfLogIs({"b release 120000"}));
        // The leave: the focus' (not handled, 0: kept); handled: no focus.
        g_csReturn = 0;
        g_gfLog.clear();
        WM(r.csGroup, g.OnLeave(1) == 0 && g.m_focus == 1 && GfLogIs({"b leave 1"}));
        g_csReturn = 9;
        WM(r.csGroup, g.OnLeave(2) == CONTROL_HANDLED && g.m_focus == -1);
        // A message: the child its target names (its result), else the group's.
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_VALUE, 5, 0, nullptr, 0x00110000);
        g_gfLog.clear();
        g_csReturn = 0;
        WM(r.csGroup, !g.HandleMessage(&m) && m.value == 0x77 && GfLogIs({"a message 110000 100000b"}) && g.m_value == 0);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 5, 0, nullptr, 0x00150000);
        WM(r.csGroup, g.HandleMessage(&m) && g.m_value == 5);
        g_csReturn = 9;
        S_ControlDesc desc(5, "g", 0);
        desc.children = {new S_ControlDesc(2, "kid", 0)};
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "grframe");
        C_ControlGroup h(scene, 0, nullptr);
        g_gfLog.clear();
        WM(r.csGroup, h.Create(frame, frame, &desc) && h.m_frame == frame && h.m_type == 5 && GfLogIs({"child grframe grframe kid 2"}));
    }
    // --- the group's cursor move: over the focus its; over another child the leave then (left) the
    // enter; over none the leave ---
    {
        C_ControlGroup g(scene, 0x00100000, nullptr);
        auto *a = new C_CsChild("a", 0x00110000), *b = new C_CsChild("b", 0x00120000);
        g.m_children = {a, b};
        g_csReturn = 9;
        g.m_focus = 0;
        g_gfLog.clear();
        WM(r.csGroupInput, g.OnInput05(0x00110000) == 9 && GfLogIs({"a in05 110000"}));
        g_gfLog.clear();
        WM(r.csGroupInput, g.OnInput05(0x00120000) == 9 && g.m_focus == 1 && GfLogIs({"a leave 120000", "b enter 120000"}));
        g_csReturn = 0;
        g_gfLog.clear();
        WM(r.csGroupInput, g.OnInput05(0x00110000) == CONTROL_HANDLED && g.m_focus == 1 && GfLogIs({"b leave 110000"}));
        g_gfLog.clear();
        WM(r.csGroupInput, g.OnInput05(0x00160000) == 0 && g.m_focus == 1 && GfLogIs({"b leave 160000"}));
        g_csReturn = 9;
        g_gfLog.clear();
        WM(r.csGroupInput, g.OnInput05(0x00160000) == CONTROL_HANDLED && g.m_focus == -1 && GfLogIs({"b leave 160000"}));
        // No focus: over a child, the enter.
        g_gfLog.clear();
        WM(r.csGroupInput, g.OnInput05(0x00110000) == 9 && g.m_focus == 0 && GfLogIs({"a enter 110000"}));
        g_gfLog.clear();
    }

    // --- the frame switch: frames added by name (off), one on at a time; its children the frames;
    // not taking input ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "swframe");
        I3D_frame *f0 = GfVisual("sw0", I3DVISUAL_OBJECT, frame), *f1 = GfVisual("sw1", I3DVISUAL_OBJECT, frame);
        C_FrameSwitch s(scene, 0x00700000, nullptr);
        WM(r.csSwitch, !s.m_takesInput && s.m_selected == -1 && !s.IsChild(0) && s.Create(frame, frame, nullptr));
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_FRAME, 0, reinterpret_cast<uintptr_t>("sw1"));
        WM(r.csSwitch, s.HandleMessage(&m));
        m = CsMessage(CONTROL_MSG_ADD_FRAME, 0, reinterpret_cast<uintptr_t>("nothere"));
        WM(r.csSwitch, s.HandleMessage(&m));
        m = CsMessage(CONTROL_MSG_ADD_FRAME, 0, reinterpret_cast<uintptr_t>("sw0"));
        g_csHandled &= s.HandleMessage(&m);
        WM(r.csSwitch, s.m_frames == (std::vector<I3D_frame *>{f1, f0}) && s.IsChild(1) && !s.IsChild(2) && !s.IsChild(-1) &&
                           GfLogIs({"find swframe sw1 10001", "on sw1 0", "find swframe nothere 10001", "find swframe sw0 10001", "on sw0 0"}));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SELECT_FRAME, 1);
        g_csHandled &= s.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SELECT_FRAME, 0);
        g_csHandled &= s.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SELECT_FRAME, 2);
        g_csHandled &= s.HandleMessage(&m);
        WM(r.csSwitch, s.m_selected == 0 && GfLogIs({"on sw0 1", "on sw0 0", "on sw1 1", "on sw1 0"}));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_HIDE_FRAME);
        g_csHandled &= s.HandleMessage(&m);
        WM(r.csSwitch, GfLogIs({"on sw1 0"}) && s.m_selected == 0);
        s.m_selected = -1;
        g_gfLog.clear();
        g_csHandled &= s.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SELECT_FRAME, 5);
        g_csHandled &= s.HandleMessage(&m);
        WM(r.csSwitch, g_gfLog.empty() && s.m_selected == -1);
        // Ignored; the base's others (and routes).
        s.m_value = 3;
        m = CsMessage(CONTROL_MSG_IGNORED_25, 9);
        g_csHandled &= s.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 7);
        g_csHandled &= s.HandleMessage(&m);
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SELECT_FRAME);
        g_csHandled &= s.HandleMessage(&m);
        WM(r.csSwitch, s.m_value == 7 && s.m_routes.size() == 1 && g_gfLog.empty());
    }

    // --- the text button: its normal and active frames (the active one where the normal is, off), the
    // box on the normal one; the active one shown while the cursor is over it ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "tbframe");
        I3D_frame *normal = CsVisual("normaltb1", frame), *active = CsVisual("activedtb1", frame);
        normal->m_localMatrix(3) = S_vector(1.0f, 2.0f, 3.0f);
        C_TextButton b(scene, 0x00800000, nullptr);
        WM(r.csTextButton, b.m_takesInput && !b.GetHighlightFrame());
        g_gfLog.clear();
        WM(r.csTextButton, b.Create(frame, frame, nullptr) && b.m_normalFrame == normal && b.m_activeFrame == active &&
                               b.GetHighlightFrame() == active && active->GetPos() == S_vector(1.0f, 2.0f, 3.0f) &&
                               normal->GetData() == reinterpret_cast<S_FrameData *>(0x00800000) &&
                               active->GetData() == reinterpret_cast<S_FrameData *>(0x00800000) && CsPlaced(*b.m_box, 10));
        WM(r.csTextButton, GfLogIs({"find tbframe normal* 10001", "find tbframe actived* 10001", "on activedtb1 0", "update activedtb1"}));
        b.m_routes = {CsMessage(CONTROL_NOTIFY_ENTER | 1, 0, 0, nullptr, 0xE), CsMessage(CONTROL_NOTIFY_LEAVE | 2, 0, 0, nullptr, 0xF),
                      CsMessage(CONTROL_NOTIFY_CLICK | 3, 0, 0, nullptr, 0xC)};
        g_controlMessages.clear();
        b.m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.csTextButton, b.OnEnter(1) == CONTROL_HANDLED &&
                               GfLogIs({"on activedtb1 1", "on normaltb1 0", "update activedtb1", "update normaltb1", "notify 4003000 0"}));
        g_gfLog.clear();
        WM(r.csTextButton, b.OnLeave(1) == CONTROL_HANDLED &&
                               GfLogIs({"on normaltb1 1", "on activedtb1 0", "update normaltb1", "update activedtb1", "notify 4004000 0"}));
        g_gfLog.clear();
        b.OnClick(1);
        WM(r.csTextButton, GfLogIs({"notify 4001000 0"}));
        b.m_anyCallback = S_ControlCallback();
        g_controlMessages.clear();
        b.OnEnter(1);
        b.OnLeave(1);
        WM(r.csTextButton, b.OnClick(1) == CONTROL_HANDLED && g_controlMessages.size() == 3 && g_controlMessages[0].target == 0xE &&
                               g_controlMessages[1].target == 0xF && g_controlMessages[2].target == 0xC);
        b.m_inactive = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        WM(r.csTextButton, b.OnEnter(1) == 0 && b.OnClick(1) == 0 && g_gfLog.empty() && b.OnLeave(1) == CONTROL_HANDLED &&
                               g_controlMessages.size() == 1);
        b.m_inactive = false;
        g_controlMessages.clear();
        // The refresh: on the normal frame (the children not refreshed); white text.
        b.m_box->m_placed = false;
        b.m_children = {new C_CsChild("k1", 0)};
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_REFRESH);
        g_csHandled &= b.HandleMessage(&m);
        WM(r.csTextButton, b.m_box->m_placed && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_WHITE_TEXT, 1);
        g_csHandled &= b.HandleMessage(&m);
        WM(r.csTextButton, b.m_box->m_colour == 0xFFFFFFFF);
        m = CsMessage(CONTROL_MSG_WHITE_TEXT, 0);
        g_csHandled &= b.HandleMessage(&m);
        WM(r.csTextButton, b.m_box->m_colour == 0);
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 1, 0, "t");
        g_csHandled &= b.HandleMessage(&m);
        WM(r.csTextButton, CsRuns(*b.m_box) == "t/1 ");
        // A missing frame: false; a frame with its place locked: kept there.
        I3D_frame *other = GfFrame(FRAME_DUMMY, "tbother");
        CsVisual("normaltb2", other);
        C_TextButton c(scene, 0, nullptr);
        WM(r.csTextButton, !c.Create(other, other, nullptr));
        I3D_frame *third = GfFrame(FRAME_DUMMY, "tbthird");
        CsVisual("normaltb3", third)->m_localMatrix(3) = S_vector(4.0f, 5.0f, 6.0f);
        I3D_frame *locked = CsVisual("activedtb3", third);
        locked->m_frameFlags |= FRMFLAGS_POS_LOCKED;
        C_TextButton d(scene, 0, nullptr);
        WM(r.csTextButton, d.Create(third, third, nullptr) && locked->GetPos() == S_vector(0.0f, 0.0f, 0.0f));
    }

    // --- the tooltip: announced; shown by the cursor once it has stayed 500 ms (SHOW arms it), hidden
    // when the cursor moves or a button is down; the cursor released with it ---
    {
        I3D_frame *frame = CsVisual("ttframe");
        frame->m_localMatrix(3) = S_vector(0.0f, 0.0f, 7.0f);
        frame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        auto *t = new C_Tooltip(scene, 0x00900000, nullptr);
        WM(r.csTooltip, !t->m_takesInput && !t->m_armed);
        g_controlMessages.clear();
        g_gfLog.clear();
        const int refs = g_csCursor.refs;
        WM(r.csTooltip, t->Create(frame, frame, nullptr) && t->m_cursor && g_csCursor.refs == refs + 1 && GfLogIs({"cursor acquire 1"}) &&
                            g_controlMessages.size() == 1 && g_controlMessages[0].target == 0 &&
                            g_controlMessages[0].id == CONTROL_MSG_ADD_TICKER && g_controlMessages[0].value == 0 &&
                            g_controlMessages[0].value2 == reinterpret_cast<uintptr_t>(t) && g_controlMessages[0].value3 == 0 &&
                            !t->m_box->m_placed);
        g_controlMessages.clear();
        // Not armed: nothing.
        g_csCursor.still = 1000;
        g_gfLog.clear();
        t->Tick(10);
        WM(r.csTooltip, g_gfLog.empty());
        S_ControlMessage m = CsMessage(CONTROL_MSG_SHOW);
        t->m_shown = false;
        g_csHandled &= t->HandleMessage(&m);
        WM(r.csTooltip, t->m_armed && !t->m_shown && g_gfLog.empty());
        // Armed, still 500 ms: by the cursor, placed, shown.
        g_csCursor.still = 500;
        g_csCursor.buttons = 0;
        g_csCursor.position = S_vector(0.25f, -0.5f, -1.0f);
        t->Tick(10);
        WM(r.csTooltip, GfLogIs({"setpos ttframe 0.4000 -0.4850 7.0000", "update ttframe", "on ttframe 1", "update ttframe"}) &&
                            t->m_shown && t->m_box->m_placed);
        // Moved (under 500 ms) or a button down: hidden (when shown).
        g_csCursor.still = 499;
        g_gfLog.clear();
        t->Tick(10);
        t->Tick(10);
        WM(r.csTooltip, !t->m_shown && GfLogIs({"on ttframe 0", "update ttframe"}) && t->m_armed);
        g_csCursor.still = 600;
        g_csCursor.buttons = 4;
        t->m_shown = true;
        g_gfLog.clear();
        t->Tick(10);
        WM(r.csTooltip, !t->m_shown && GfLogIs({"on ttframe 0", "update ttframe"}));
        g_csCursor.buttons = 0;
        // HIDE: disarmed and hidden.
        t->m_shown = true;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_HIDE);
        g_csHandled &= t->HandleMessage(&m);
        t->Tick(10);
        WM(r.csTooltip, !t->m_armed && !t->m_shown && GfLogIs({"on ttframe 0", "update ttframe"}));
        // The text messages, routes.
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 1, 0, "tip");
        g_csHandled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_NOTIFY_SHOWN | CONTROL_MSG_SHOW);
        g_csHandled &= t->HandleMessage(&m);
        WM(r.csTooltip, CsRuns(*t->m_box) == "tip/1 " && t->m_routes.size() == 1 && !t->m_armed);
        g_gfLog.clear();
        delete t;
        WM(r.csTooltip, g_csCursor.refs == refs && !g_gfLog.empty() && g_gfLog.front() == "cursor release");
        // No text control made: true, nothing announced.
        C_Tooltip u(scene, 0, nullptr);
        g_controlMessages.clear();
        frame->SetData(nullptr);
    }

    // --- the button: the cursor; its three frames (the others where the normal is, off); the active
    // one while the cursor is over it, the inactive one while inactive ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "btframe");
        I3D_frame *normal = GfVisual("normalbt", I3DVISUAL_OBJECT, frame), *active = GfVisual("activedbt", I3DVISUAL_OBJECT, frame),
                  *inactive = GfVisual("inactivedbt", I3DVISUAL_OBJECT, frame);
        normal->m_localMatrix(3) = S_vector(1.0f, 1.0f, 1.0f);
        auto *b = new C_Button(scene, 0x00A00000, nullptr);
        const int refs = g_csCursor.refs;
        g_gfLog.clear();
        WM(r.csButton, b->Create(frame, frame, nullptr) && b->m_cursor && g_csCursor.refs == refs + 1 && b->m_normalFrame == normal &&
                           b->m_activeFrame == active && b->m_inactiveFrame == inactive && active->GetPos() == S_vector(1.0f, 1.0f, 1.0f) &&
                           inactive->GetPos() == S_vector(1.0f, 1.0f, 1.0f) &&
                           inactive->GetData() == reinterpret_cast<S_FrameData *>(0x00A00000) &&
                           normal->GetData() == reinterpret_cast<S_FrameData *>(0x00A00000) &&
                           active->GetData() == reinterpret_cast<S_FrameData *>(0x00A00000));
        WM(r.csButton, GfLogIs({"cursor acquire 1", "find btframe normal* 10001", "find btframe actived* 10001",
                                "find btframe inactived* 10001", "on normalbt 1", "update normalbt", "on activedbt 0", "update activedbt",
                                "on inactivedbt 0", "update inactivedbt"}));
        g_gfLog.clear();
        WM(r.csButton, b->OnEnter(3) == CONTROL_HANDLED &&
                           GfLogIs({"on activedbt 1", "on normalbt 0", "on inactivedbt 0", "update inactivedbt", "update activedbt",
                                    "update normalbt"}));
        b->m_held = true;
        g_gfLog.clear();
        WM(r.csButton, b->OnLeave(3) == CONTROL_HANDLED && !b->m_held &&
                           GfLogIs({"on normalbt 1", "on activedbt 0", "on inactivedbt 0", "update normalbt", "update activedbt",
                                    "update inactivedbt"}));
        g_gfLog.clear();
        b->SetInactive(true);
        WM(r.csButton, b->m_inactive &&
                           GfLogIs({"on normalbt 0", "on activedbt 0", "on inactivedbt 1", "update normalbt", "update activedbt",
                                    "update inactivedbt"}));
        b->m_held = true;
        g_gfLog.clear();
        WM(r.csButton, b->OnEnter(3) == 0 && b->OnLeave(3) == CONTROL_HANDLED && !b->m_held && g_gfLog.empty());
        b->SetInactive(false);
        g_gfLog.clear();
        b->m_routes = {CsMessage(CONTROL_NOTIFY_KEY_CLICK | 4, 0, 0, nullptr, 0x40), CsMessage(CONTROL_NOTIFY_CLICK | 5, 0, 0, nullptr, 0x50)};
        g_controlMessages.clear();
        b->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.csButton, b->OnKeyClick(3) == CONTROL_HANDLED && g_controlMessages.size() == 1 && g_controlMessages[0].target == 0x40 &&
                           GfLogIs({"notify 4012000 0"}));
        b->m_anyCallback = S_ControlCallback();
        g_controlMessages.clear();
        g_gfLog.clear();
        delete b;
        WM(r.csButton, g_csCursor.refs == refs && GfLogIs({"cursor release"}));
        // Not three frames: false (the cursor taken).
        I3D_frame *two = GfFrame(FRAME_DUMMY, "bttwo");
        GfVisual("normalbt2", I3DVISUAL_OBJECT, two);
        GfVisual("activedbt2", I3DVISUAL_OBJECT, two);
        C_Button c(scene, 0, nullptr);
        WM(r.csButton, !c.Create(two, two, nullptr) && c.m_cursor && !c.m_normalFrame);
    }
    // --- the button held: its press, then every frame once the cursor has stayed 300 ms; released, or
    // no button down: stopped ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "bhframe");
        GfVisual("normalbh", I3DVISUAL_OBJECT, frame);
        GfVisual("activedbh", I3DVISUAL_OBJECT, frame);
        GfVisual("inactivedbh", I3DVISUAL_OBJECT, frame);
        C_Button b(scene, 0x00B00000, nullptr);
        b.Create(frame, frame, nullptr);
        b.m_routes = {CsMessage(CONTROL_NOTIFY_PRESS | 6, 0, 0, nullptr, 0x60)};
        g_controlMessages.clear();
        b.m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.csButtonHeld, b.OnPress(1) == CONTROL_HANDLED && b.m_held && b.m_repeatTime == 300 && g_controlMessages.size() == 1 &&
                               GfLogIs({"notify 4006000 0"}));
        g_csCursor.buttons = 2;
        g_csCursor.still = 299;
        b.Tick(10);
        WM(r.csButtonHeld, g_controlMessages.size() == 1);
        g_csCursor.still = 300;
        g_gfLog.clear();
        b.Tick(10);
        WM(r.csButtonHeld, g_controlMessages.size() == 2 && b.m_repeatTime == -1 && GfLogIs({"notify 4006000 0"}));
        b.m_anyCallback = S_ControlCallback();
        g_csCursor.still = 0;
        b.Tick(10);
        b.Tick(10);
        WM(r.csButtonHeld, g_controlMessages.size() == 4);
        // No button down: stopped.
        g_csCursor.buttons = 0;
        b.Tick(10);
        WM(r.csButtonHeld, !b.m_held && b.m_repeatTime == 300 && g_controlMessages.size() == 4);
        b.Tick(10);
        WM(r.csButtonHeld, g_controlMessages.size() == 4);
        // Released: stopped; inactive: no press.
        b.OnPress(1);
        WM(r.csButtonHeld, b.OnRelease(1) == CONTROL_HANDLED && !b.m_held && g_controlMessages.size() == 5);
        b.m_inactive = true;
        WM(r.csButtonHeld, b.OnPress(1) == CONTROL_HANDLED && !b.m_held && g_controlMessages.size() == 5);
        g_controlMessages.clear();
        g_csCursor.still = 0;
    }

    // --- the paged text control: a paged box; the text messages its box's; a refresh reattaches
    // (nothing); dimmed while inactive; deleted with it ---
    {
        I3D_frame *frame = CsVisual("pgframe");
        auto *c = new C_PagedTextControl(scene, 0x00C00000, nullptr);
        WM(r.csPaged, c->Create(frame, frame, nullptr) && c->m_box && !c->m_box->m_placed);
        delete c->m_box;
        c->m_box = new C_CsPagedBox("p");
        C_PagedTextBox &box = *c->m_box;
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "a");
        g_csHandled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_TEXT_ID, 4, 42);
        g_csHandled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_RUN_TEXT, 6, 1, "c");
        g_csHandled &= c->HandleMessage(&m);
        WM(r.csPaged, CsRuns(box) == "a/3 c/6 ");
        m = CsMessage(CONTROL_MSG_SET_TEXT, 5, 0, "b");
        g_csHandled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_GET_RUNS);
        g_csHandled &= c->HandleMessage(&m);
        WM(r.csPaged, CsRuns(box) == "b/5 c/6 " && m.value == 2);
        m = CsMessage(CONTROL_MSG_SET_TEXT_ID, 7, 43);
        g_csHandled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_REMOVE_RUN, 1);
        g_csHandled &= c->HandleMessage(&m);
        WM(r.csPaged, CsRuns(box) == "text 43/7 ");
        m = CsMessage(CONTROL_MSG_CLEAR_TEXT);
        g_csHandled &= c->HandleMessage(&m);
        WM(r.csPaged, box.NumRuns() == 0);
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 1, 0, "d");
        g_csHandled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_CLEAR);
        g_csHandled &= c->HandleMessage(&m);
        WM(r.csPaged, box.NumRuns() == 0);
        m = CsMessage(CONTROL_MSG_SET_MARGIN, 3);
        g_csHandled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_REFRESH);
        c->m_children = {new C_CsChild("k1", 0)};
        g_gfLog.clear();
        g_csHandled &= c->HandleMessage(&m);
        WM(r.csPaged, box.m_margin == 3 && !box.m_placed && g_gfLog.empty());
        // Not the text control's: FACE_FRONT, SET_COLOUR (the base's: nothing).
        m = CsMessage(CONTROL_MSG_SET_COLOUR, 5);
        g_csHandled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_FACE_FRONT);
        g_csHandled &= c->HandleMessage(&m);
        WM(r.csPaged, box.m_colour == 0 && g_gfLog.empty());
        c->SetInactive(true);
        WM(r.csPaged, c->m_inactive && box.m_dimmed);
        c->SetInactive(false);
        g_gfLog.clear();
        c->Refresh();
        WM(r.csPaged, GfLogIs({"box p render", "k1 refresh"}));
        g_gfLog.clear();
        C_Control *base = c;
        delete base;
        WM(r.csPaged, !g_gfLog.empty() && g_gfLog.front() == "box p deleted");
    }
    // --- the pages: areas by their frames' names, the page, its counts, a refresh lays out again ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "pcframe");
        I3D_frame *area = CsVisual("pcarea", frame);
        C_PagesControl c(scene, 0x00D00000, nullptr);
        c.Create(frame, frame, nullptr);
        delete c.m_box;
        c.m_box = new C_CsPagedBox("q");
        C_PagedTextBox &box = *c.m_box;
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_AREA, 0, 0, "pcarea");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, box.m_areas.size() == 1 && box.m_placed &&
                          GfLogIs({"find pcframe pcarea ffff", "setdir pcarea 0.00000 0.00000 -1.00000 0.000000", "update pcarea"}) &&
                          area->GetData() == reinterpret_cast<S_FrameData *>(0x00D00000));
        m = CsMessage(CONTROL_MSG_ADD_AREA, 0, 0, "nothere");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, box.m_areas.size() == 1);
        m = CsMessage(CONTROL_MSG_ADD_TEXT, static_cast<uint32_t>(st), 0, "x");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, CsRuns(box) == CmText("x/%d ", st));
        box.m_dirty = false;
        m = CsMessage(CONTROL_MSG_REFRESH);
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_GET_PAGES, 9);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, !box.m_dirty && box.NumPages() == 1 && m.value == 1);
        m = CsMessage(CONTROL_MSG_GET_PAGE_BREAK, 9);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, m.value == 0xFFFFFFFF);
        // (A second page, by hand.)
        box.m_pages.push_back(new S_TextPage());
        m = CsMessage(CONTROL_MSG_SET_PAGE, 1);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, box.m_page == 1);
        m = CsMessage(CONTROL_MSG_CLEAR);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, box.NumRuns() == 0);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "y");
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, CsRuns(box) == "y/2 ");
        c.m_children = {new C_CsChild("k1", 0)};
        g_gfLog.clear();
        c.Refresh();
        c.m_shown = false;
        c.Refresh();
        WM(r.csPages, GfLogIs({"box q render"}));
        // Without a box: the counts 0, nothing else.
        delete c.m_box;
        c.m_box = nullptr;
        m = CsMessage(CONTROL_MSG_GET_PAGES, 9);
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, m.value == 0);
        m = CsMessage(CONTROL_MSG_GET_PAGE_BREAK, 9);
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_PAGE, 1);
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_REFRESH);
        g_csHandled &= c.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_AREA, 0, 0, "pcarea");
        g_gfLog.clear();
        g_csHandled &= c.HandleMessage(&m);
        WM(r.csPages, GfLogIs({"find pcframe pcarea ffff", "setdir pcarea 0.00000 0.00000 -1.00000 0.000000", "update pcarea"}));
        area->SetData(nullptr);
    }

    WM(r.csHandled, g_csHandled);
    ScrollTest(scene, st);
    CheckTest(scene);
    EditTest(scene);
    ComboTest(scene);
    InfoTest(scene);
    ListTest(scene);
    BarTest(scene);
    ServerTest(scene);
    HotKeyTest(scene);
    GroupsTest(scene);
    MovieTest(scene);
#ifdef FACTORY_TEST
    FactoryTest(scene);
    ScreenTest(scene);
    MenuTest(scene);
    MenuRunTest(scene);
    MenuSoldierTest(scene);
    EquipmentPanelTest(scene);
#endif
    ReleaseTextStyle(st);
    keeper.m_fonts->m_fonts.erase(keeper.m_fonts->m_fonts.begin());
    g_gfLog.clear();
    g_controlMessages.clear();
    g_textIds = oldIds;
    g_igraphVtable[0x3C / 4] = oldLoad;
    g_igraphVtable[0x18 / 4] = oldX;
    g_igraphVtable[0x1C / 4] = oldY;
    std::printf("controls: csText %d csTextMsgs %d csTitle %d csLabel %d csPicture %d csPicMsgs %d csGroup %d csGroupInput %d "
                "csSwitch %d csTextButton %d csTooltip %d csButton %d csButtonHeld %d csPaged %d csPages %d csHandled %d\n",
                r.csText, r.csTextMsgs, r.csTitle, r.csLabel, r.csPicture, r.csPicMsgs, r.csGroup, r.csGroupInput, r.csSwitch,
                r.csTextButton, r.csTooltip, r.csButton, r.csButtonHeld, r.csPaged, r.csPages, r.csHandled);
}

#include "scroll_test.inc"
#include "check_test.inc"
#include "edit_test.inc"
#include "combo_test.inc"
#include "info_test.inc"
#include "list_test.inc"
#include "bar_test.inc"
#include "server_test.inc"
#include "hotkey_test.inc"
#include "groups_test.inc"
#include "movie_test.inc"
#ifdef FACTORY_TEST
#include "factory_test.inc"
#include "screen_test.inc"
#include "menu_test.inc"
#include "menu_run_test.inc"
#include "menu_soldier_test.inc"
#include "equipment_panel_test.inc"
#endif
