// The interface's text boxes and graphics on both sides (parts 66 on), included after interface_kit.inc.
// - The text boxes: the port's C_TextBox defined here (logging); the original builds its own inline (operator new, the
//   graph's ScrnX and ScrnY, AcquireFontManager 0x0063df00) and calls AttachTo 0x00644da0, SetText 0x006446e0, SetTextId
//   0x00644520, AddText 0x00644830, AddTextId 0x00644770, Clear 0x00644c80, Render 0x00648200 (its vtable's +0x04) and the
//   deleting destructor 0x0048ecd0 (+0x0C): redirected to the same loggers. Boxes are named by their first appearance on
//   each side ("box0" ...); a deleted box gives up its name.
// - The graphics: every arena frame has its mesh (GetLevel), level (+0x58 the face groups' count, +0x5C their array;
//   GetFaceGroup), up to 4 face groups (+0x04 the material; DuplicateMaterial: a copy from the pool), the materials
//   (+0x08 flags, +0x0C texture; the colours, SetPower, SetTexture, ResolveTexture logged) and 24 textures (+0x14 the
//   reference count, +0x18 the file's name; Release logged). The frames' DuplicateMesh (0xA0) and SetLightLevel (0xA4).
//   GfxName names them for Name().

namespace H {

// ---- the text boxes ----
std::vector<const void *> g_boxes;   // this side's, in order of first appearance (null: deleted)
std::string BoxName(const void *b)
{
    auto it = std::find(g_boxes.begin(), g_boxes.end(), b);
    if (it == g_boxes.end()) {
        g_boxes.push_back(b);
        it = g_boxes.end() - 1;
    }
    return "box" + std::to_string(it - g_boxes.begin());
}
std::string Text(const C_str &s) { return s.m_rep ? "'" + std::string(s.c_str()) + "'" : "(null)"; }
void BoxAttach(const void *b, const void *scene, const void *frame) { Log("%s AttachTo %s %s", BoxName(b).c_str(), Name(scene).c_str(), Name(frame).c_str()); }
void BoxSetText(const void *b, const C_str &t, int style, int index) { Log("%s SetText %s %d %d", BoxName(b).c_str(), Text(t).c_str(), style, index); }
void BoxSetTextId(const void *b, int id, int style, int index) { Log("%s SetTextId %d %d %d", BoxName(b).c_str(), id, style, index); }
void BoxAddText(const void *b, const C_str &t, int style) { Log("%s AddText %s %d", BoxName(b).c_str(), Text(t).c_str(), style); }
void BoxAddTextId(const void *b, int id, int style) { Log("%s AddTextId %d %d", BoxName(b).c_str(), id, style); }
void BoxClear(const void *b) { Log("%s Clear", BoxName(b).c_str()); }
void BoxRender(const void *b) { Log("%s Render", BoxName(b).c_str()); }
void BoxDelete(const void *b)
{
    Log("%s delete", BoxName(b).c_str());
    *std::find(g_boxes.begin(), g_boxes.end(), b) = nullptr;
}
void __attribute__((thiscall)) OB_AttachTo(void *b, void *scene, void *frame) { BoxAttach(b, scene, frame); }
bool __attribute__((thiscall)) OB_SetText(void *b, const C_str &t, int style, int index)
{
    BoxSetText(b, t, uint8_t(style), index);
    return true;
}
bool __attribute__((thiscall)) OB_SetTextId(void *b, int id, int style, int index)
{
    BoxSetTextId(b, id, style, index);
    return true;
}
bool __attribute__((thiscall)) OB_AddText(void *b, const C_str &t, int style)
{
    BoxAddText(b, t, uint8_t(style));
    return true;
}
bool __attribute__((thiscall)) OB_AddTextId(void *b, int id, int style)
{
    BoxAddTextId(b, id, uint8_t(style));
    return true;
}
void __attribute__((thiscall)) OB_Clear(void *b) { BoxClear(b); }
void __attribute__((thiscall)) OB_Render(void *b) { BoxRender(b); }
void *__attribute__((thiscall)) OB_Delete(void *b, unsigned flags)
{
    BoxDelete(b);
    if (flags & 1)
        std::free(b);
    return b;
}
void *__cdecl O_AcquireFonts()
{
    Log("AcquireFontManager");
    return &g_scene;   // (a pointer the snaps do not look into)
}
int __stdcall G_ScrnX(void *)
{
    Log("graph ScrnX");
    return 1024;
}
int __stdcall G_ScrnY(void *)
{
    Log("graph ScrnY");
    return 768;
}

// ---- the graphics ----
constexpr int GROUPS = 4, MATERIALS = 64, TEXTURES = 24;
struct S_Gfx {
    uint8_t meshes[FRAMES][0x10];
    uint8_t levels[FRAMES][0x80];
    uint8_t groups[FRAMES][GROUPS][0x10];
    uint8_t materials[MATERIALS][0x40];
    uint8_t textures[TEXTURES][0x20];
    int nextMaterial;
};
S_Gfx g_gfx, g_gfxStart;
void *g_meshVt[8], *g_levelVt[16], *g_groupVt[16], *g_materialVt[24], *g_textureVt[4];
const char *const TEXTURE_NAMES[] = {"X_SQUARE.BMP", "x_square.bmp", "X_SQUARE.BMP2", "menu.bmp", "face.tga", "", "icon.bmp", "X_SQUAR.BMP"};
char g_textureNames[TEXTURES][16];
std::string GfxName(const void *p)
{
    const auto *b = static_cast<const uint8_t *>(p);
    const auto in = [b](const void *base, size_t size) { return b >= static_cast<const uint8_t *>(base) && b < static_cast<const uint8_t *>(base) + size; };
    char text[32];
    if (in(g_gfx.materials, sizeof g_gfx.materials))
        std::snprintf(text, sizeof text, "m%d", int((b - g_gfx.materials[0]) / 0x40));
    else if (in(g_gfx.textures, sizeof g_gfx.textures))
        std::snprintf(text, sizeof text, "t%d", int((b - g_gfx.textures[0]) / 0x20));
    else if (in(g_gfx.groups, sizeof g_gfx.groups))
        std::snprintf(text, sizeof text, "g%d.%d", int((b - g_gfx.groups[0][0]) / (0x10 * GROUPS)), int((b - g_gfx.groups[0][0]) / 0x10 % GROUPS));
    else if (in(g_gfx.levels, sizeof g_gfx.levels))
        std::snprintf(text, sizeof text, "level%d", int((b - g_gfx.levels[0]) / 0x80));
    else if (in(g_gfx.meshes, sizeof g_gfx.meshes))
        std::snprintf(text, sizeof text, "mesh%d", int((b - g_gfx.meshes[0]) / 0x10));
    else
        return "";
    return text;
}
void *__stdcall M_GetLevel(void *mesh, int index)
{
    Log("%s GetLevel %d", Name(mesh).c_str(), index);
    return g_gfx.levels[(static_cast<uint8_t *>(mesh) - g_gfx.meshes[0]) / 0x10];
}
void *__stdcall L_GetFaceGroup(void *level, int index)
{
    Log("%s GetFaceGroup %d", Name(level).c_str(), index);
    return g_gfx.groups[(static_cast<uint8_t *>(level) - g_gfx.levels[0]) / 0x80][index & (GROUPS - 1)];
}
void __stdcall G_DuplicateMaterial(void *group)
{
    const int m = g_gfx.nextMaterial < MATERIALS ? g_gfx.nextMaterial++ : MATERIALS - 1;
    Log("%s DuplicateMaterial -> m%d", Name(group).c_str(), m);
    void *&material = *reinterpret_cast<void **>(static_cast<uint8_t *>(group) + 4);
    if (material)
        std::memcpy(g_gfx.materials[m], material, 0x40);
    material = g_gfx.materials[m];
}
void __stdcall MAT_Color(void *m, const S_vector &c, const char *what) { Log("%s %s %s", Name(m).c_str(), what, VecText(c).c_str()); }
void __stdcall MAT_SetAmbient(void *m, const S_vector &c) { MAT_Color(m, c, "SetAmbientColor"); }
void __stdcall MAT_SetDiffuse(void *m, const S_vector &c) { MAT_Color(m, c, "SetDiffuseColor"); }
void __stdcall MAT_SetEmissive(void *m, const S_vector &c) { MAT_Color(m, c, "SetEmissiveColor"); }
void __stdcall MAT_SetSpecular(void *m, const S_vector &c) { MAT_Color(m, c, "SetSpecular"); }
void __stdcall MAT_SetPower(void *m, float power) { Log("%s SetPower %s", Name(m).c_str(), FloatText(power).c_str()); }
void __stdcall MAT_SetTexture(void *m, void *t)
{
    Log("%s SetTexture %s", Name(m).c_str(), Name(t).c_str());
    *reinterpret_cast<void **>(static_cast<uint8_t *>(m) + 0xC) = t;
}
void __stdcall MAT_Resolve(void *m)
{
    Log("%s ResolveTexture", Name(m).c_str());
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(m) + 8) &= ~0x4000000u;
}
void __stdcall GFX_Other(void *p) { Log("%s other slot", Name(p).c_str()); }
uint32_t __stdcall TEX_Release(void *t)
{
    Log("%s Release", Name(t).c_str());
    return 0;
}
void __stdcall F_DuplicateMesh(void *f) { Log("%s DuplicateMesh", Name(f).c_str()); }
void __stdcall F_SetLightLevel(void *f, float level) { Log("%s SetLightLevel %s", Name(f).c_str(), FloatText(level).c_str()); }

// The graphics in a random state (each frame its mesh; 1 to 4 face groups, now and then none or no material; textures'
// names from TEXTURE_NAMES or none). The frames' +0x1E0 set to their meshes.
void RandomGfx()
{
    std::memset(&g_gfx, 0, sizeof g_gfx);
    for (int i = 0; i < FRAMES; ++i) {
        *reinterpret_cast<void **>(g_arena.frames[i] + 0x1E0) = g_gfx.meshes[i];
        *reinterpret_cast<void ***>(g_gfx.meshes[i]) = g_meshVt;
        *reinterpret_cast<void ***>(g_gfx.levels[i]) = g_levelVt;
        *reinterpret_cast<int *>(g_gfx.levels[i] + 0x58) = P(0.05) ? 0 : I(1, GROUPS);
        *reinterpret_cast<void **>(g_gfx.levels[i] + 0x5C) = g_gfx.groups[i][0];
        for (int g = 0; g < GROUPS; ++g) {
            *reinterpret_cast<void ***>(g_gfx.groups[i][g]) = g_groupVt;
            *reinterpret_cast<void **>(g_gfx.groups[i][g] + 4) = P(0.05) ? nullptr : g_gfx.materials[I(0, 7)];
        }
    }
    for (int m = 0; m < MATERIALS; ++m) {
        *reinterpret_cast<void ***>(g_gfx.materials[m]) = g_materialVt;
        *reinterpret_cast<uint32_t *>(g_gfx.materials[m] + 8) = (P(0.3) ? 0x4000000u : 0u) | (U32() & 0x00F0F0F0u);
        *reinterpret_cast<void **>(g_gfx.materials[m] + 0xC) = P(0.05) ? nullptr : g_gfx.textures[I(0, TEXTURES - 1)];
    }
    for (int t = 0; t < TEXTURES; ++t) {
        *reinterpret_cast<void ***>(g_gfx.textures[t]) = g_textureVt;
        *reinterpret_cast<int *>(g_gfx.textures[t] + 0x14) = I(1, 3);
        std::strcpy(g_textureNames[t], Pick(TEXTURE_NAMES));
        *reinterpret_cast<const char **>(g_gfx.textures[t] + 0x18) = P(0.1) ? nullptr : g_textureNames[t];
    }
    g_gfx.nextMaterial = 8;
}

void SetUpGfx()
{
    g_frameVt[0xA0 / 4] = reinterpret_cast<void *>(F_DuplicateMesh);
    g_frameVt[0xA4 / 4] = reinterpret_cast<void *>(F_SetLightLevel);
    for (void *&v : g_meshVt)
        v = reinterpret_cast<void *>(GFX_Other);
    g_meshVt[0x04 / 4] = reinterpret_cast<void *>(M_GetLevel);
    for (void *&v : g_levelVt)
        v = reinterpret_cast<void *>(GFX_Other);
    g_levelVt[0x14 / 4] = reinterpret_cast<void *>(L_GetFaceGroup);
    for (void *&v : g_groupVt)
        v = reinterpret_cast<void *>(GFX_Other);
    g_groupVt[0x18 / 4] = reinterpret_cast<void *>(G_DuplicateMaterial);
    for (void *&v : g_materialVt)
        v = reinterpret_cast<void *>(GFX_Other);
    g_materialVt[0x04 / 4] = reinterpret_cast<void *>(MAT_SetAmbient);
    g_materialVt[0x08 / 4] = reinterpret_cast<void *>(MAT_SetDiffuse);
    g_materialVt[0x0C / 4] = reinterpret_cast<void *>(MAT_SetEmissive);
    g_materialVt[0x10 / 4] = reinterpret_cast<void *>(MAT_SetSpecular);
    g_materialVt[0x14 / 4] = reinterpret_cast<void *>(MAT_SetPower);
    g_materialVt[0x1C / 4] = reinterpret_cast<void *>(MAT_SetTexture);
    g_materialVt[0x44 / 4] = reinterpret_cast<void *>(MAT_Resolve);
    for (void *&v : g_textureVt)
        v = reinterpret_cast<void *>(TEX_Release);
    g_graphVt[0x18 / 4] = reinterpret_cast<void *>(G_ScrnX);
    g_graphVt[0x1C / 4] = reinterpret_cast<void *>(G_ScrnY);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x0063df00, reinterpret_cast<const void *>(O_AcquireFonts)},
        {0x00644da0, reinterpret_cast<const void *>(OB_AttachTo)},
        {0x006446e0, reinterpret_cast<const void *>(OB_SetText)},
        {0x00644520, reinterpret_cast<const void *>(OB_SetTextId)},
        {0x00644830, reinterpret_cast<const void *>(OB_AddText)},
        {0x00644770, reinterpret_cast<const void *>(OB_AddTextId)},
        {0x00644c80, reinterpret_cast<const void *>(OB_Clear)},
        {0x00648200, reinterpret_cast<const void *>(OB_Render)},
        {0x0048ecd0, reinterpret_cast<const void *>(OB_Delete)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // (The original CRT's float formatting: its _cfltcvt_init never ran in the mapped copy.)
    OriginalAt<void(__cdecl *)()>(0x007e599c)();
}

}  // namespace H

// ---- the port's text box ----
C_TextBox::C_TextBox()
{
    m_screenWidth = static_cast<int16_t>(GetIGraph()->ScrnX());
    m_screenHeight = static_cast<int16_t>(GetIGraph()->ScrnY());
    m_fonts = static_cast<C_FontManager *>(H::O_AcquireFonts());
}
C_TextBox::~C_TextBox() { H::BoxDelete(this); }
void C_TextBox::UpdateScrollRange() { H::Log("%s UpdateScrollRange", H::BoxName(this).c_str()); }
void C_TextBox::Render() { H::BoxRender(this); }
bool C_TextBox::ScrollTo(int line)
{
    H::Log("%s ScrollTo %d", H::BoxName(this).c_str(), line);
    return true;
}
void C_TextBox::AttachTo(I3D_scene *scene, I3D_frame *visual) { H::BoxAttach(this, scene, visual); }
bool C_TextBox::SetText(const C_str &text, uint8_t style, int index)
{
    H::BoxSetText(this, text, style, index);
    return true;
}
bool C_TextBox::SetTextId(int textId, int style, int index)
{
    H::BoxSetTextId(this, textId, style, index);
    return true;
}
bool C_TextBox::AddText(const C_str &text, uint8_t style)
{
    H::BoxAddText(this, text, style);
    return true;
}
bool C_TextBox::AddTextId(int textId, uint8_t style)
{
    H::BoxAddTextId(this, textId, style);
    return true;
}
void C_TextBox::Clear() { H::BoxClear(this); }
