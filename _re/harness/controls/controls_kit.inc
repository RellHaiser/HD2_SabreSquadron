// The controls' differential kit (parts 49 on: game/controls.cpp against the original's), included by each part's test
// after its headers. What controls.cpp reaches outside the controls is faked on both sides with the same loggers:
// - the engine's objects (frames, visuals, meshes, their face groups, materials, textures, the graphics interface, its
//   images) are fakes in one arena both sides use (their slots stdcall, their fields the engine's layout), reset before
//   each side's call and compared byte for byte after them;
// - the text boxes: the port's C_TextBox defined here (logging), the original's constructor and methods redirected to the
//   same loggers (its boxes 0x9C blocks: the fields the controls set inline at its offsets: the runs' vector +0x34 / +0x38
//   of 5-byte runs, dimmed +0x71, margin +0x90, colour +0x94);
// - the cursor: one block for both sides (the still time at +0x40 read inline by the original), its methods logged;
// - the rest (the paged boxes, key bindings, texts ...) stubs that fail the test when reached: later parts fake them.
// The controls: the port's real objects (wrapped in C_Logged<T>, Refresh, Tick and HandleMessageCopy logged), the original's raw
// blocks with a copy of the class's own vtable whose Refresh (0x18) and HandleMessageCopy (0x74) slots log first; both
// built from a kind table (each part's) and compared field by field, with the message queue, after each call.

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

namespace {

int checks, failures;
const char *g_case = "";
int g_round;
void Fail(const char *format, ...)
{
    ++failures;
    if (failures > 40)
        return;
    std::printf("FAIL [%s round %d] ", g_case, g_round);
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
}
#define CHECK(cond, ...)               \
    do {                               \
        ++checks;                      \
        if (!(cond))                   \
            Fail(__VA_ARGS__);         \
    } while (0)

std::mt19937 g_rng(49);
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(g_rng) < p; }
float F(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_rng); }
uint32_t U32() { return static_cast<uint32_t>(g_rng()); }
template <class T, size_t N> T Pick(const T (&list)[N]) { return list[I(0, int(N) - 1)]; }
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char text[400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_log.push_back(text);
    // (HD2_CONTROLS_ECHO=round: that round's calls printed as they come, for a crash's last.)
    static const char *echo = std::getenv("HD2_CONTROLS_ECHO");
    if (echo && std::atoi(echo) == g_round)
        std::printf("    > %s\n", text);
}
std::string Joined(const std::vector<std::string> &log)
{
    std::string all;
    for (const std::string &line : log)
        all += line + "; ";
    return all;
}
// A stub reached that this part does not fake.
void Unexpected(const char *what)
{
    Log("UNEXPECTED %s", what);
    ++failures;
    if (failures <= 40)
        std::printf("FAIL [%s round %d] unexpected call: %s\n", g_case, g_round, what);
}

// ---- the trees ----
std::vector<C_Control *> g_portNodes;
std::vector<uint8_t *> g_origNodes;
std::vector<void *> g_origBlocks;   // (the test's blocks of the original's side, freed before the next call)
void *OrigAlloc(size_t bytes)
{
    void *p = std::calloc(1, bytes ? bytes : 1);
    g_origBlocks.push_back(p);
    return p;
}
template <class T> T &OF(uint8_t *block, uint32_t offset) { return *reinterpret_cast<T *>(block + offset); }
template <class T> const T &OF(const uint8_t *block, uint32_t offset) { return *reinterpret_cast<const T *>(block + offset); }
std::string Name(const void *p);

// ---- the engine's fakes: one arena ----
struct alignas(16) S_Arena {
    uint8_t frames[16][0x400];   // frames and visuals (the engine's layout; +0x1E0 a visual's mesh)
    uint8_t mesh[0x20];
    uint8_t level[0x80];         // +0x5C the face groups
    uint8_t groups[4][0x10];     // vtable, material, faces, next
    uint8_t materials[3][0x40];  // +0x08 flags, +0x0C texture
    uint8_t textures[4][0x40];   // +0x18 the file's name
    char names[4][16];
    uint8_t cursor[0x60];        // +0x40 the still time
    uint8_t graph[8];
    uint8_t fonts[16];           // the font manager
    int16_t scrnX, scrnY;
    uint8_t image[8];
    S_vector cursorPos;
    int cursorDown;
    int loadResult;
    int numChildren[16];
    int16_t child[16][48];       // FindChildFrame's answers, by CHILD_NAMES (-1 none)
    alignas(8) uint8_t zone[0x400];   // a multiplayer zone (its team: +0x98 the original's, the part sets the port's)
    S_vector cursorMove;
};
S_Arena g_arena, g_arenaStart;
// The child frames' names the controls ask for (FindChildFrame's answers: S_Arena::child).
const char *const CHILD_NAMES[] = {
    "normal*", "actived*", "inactived*", "barrow up*", "barrow down*", "slider*", "sliderbase*", "page*",
    // (8: the check boxes', the toggle buttons', the highlighted check box's, the side indicator's, the mark's)
    "yes_full*", "yes_empty*", "no_full*", "no_empty*", "activeddown*", "normaldown*", "activedup*", "normalup*",
    "allactived", "allnormal", "xallactived", "xallnormal", "yesactived_full*", "noactived_full*", "noactived_empty*", "allied*",
    "axis*", "text_*", "noactived*", "nonormal*", "yesactived*", "yesnormal*", "emptyactived*", "emptynormal*",
    // (32: the selection list's)
    "select*",
    // (33: the groups': the scrolled text's, the combo box's, the titled combo box's, the chat input's)
    "text*", "scroll*", "listbox*", "cbdown*", "header*", "text18", "game difficulty", "binput*", "bTeam",
    // (42: the skill's)
    "skill_text*", "skill_value*",
    // (44: the item information's)
    "ammo*", "text_info*"};
const int CHILD_NAME_COUNT = 46;
// The names RandomArena draws answers for (the others none): the earlier parts' 33 unless a part asks for more (their
// random streams kept).
int g_arenaNames = 33;
int ChildName(const char *name)
{
    for (int k = 0; k < CHILD_NAME_COUNT; ++k)
        if (std::strcmp(name, CHILD_NAMES[k]) == 0)
            return k;
    return -1;
}
void *g_frameVt[64], *g_meshVt[8], *g_materialVt[32], *g_textureVt[4], *g_graphVt[128], *g_imageVt[8], *g_groupVt[8];
I3D_frame *FrameAt(int i) { return reinterpret_cast<I3D_frame *>(g_arena.frames[i]); }
int FrameIndex(const void *f)
{
    for (int i = 0; i < 16; ++i)
        if (f == g_arena.frames[i])
            return i;
    return -1;
}
std::string ArenaName(const void *p)
{
    const auto *b = static_cast<const uint8_t *>(p);
    const auto *a = reinterpret_cast<const uint8_t *>(&g_arena);
    if (b >= a && b < a + sizeof g_arena) {
        char text[32];
        if (FrameIndex(p) >= 0)
            std::snprintf(text, sizeof text, "f%d", FrameIndex(p));
        else
            std::snprintf(text, sizeof text, "arena+%x", unsigned(b - a));
        return text;
    }
    return "";
}
std::string VecText(const S_vector &v)
{
    char text[64];
    std::snprintf(text, sizeof text, "(%08x %08x %08x)", Bits(v.x), Bits(v.y), Bits(v.z));
    return text;
}
void __stdcall F_SetPos(void *f, const S_vector &pos) { Log("%s SetPos %s", ArenaName(f).c_str(), VecText(pos).c_str()); }
void __stdcall F_SetDir(void *f, const S_vector &dir, float roll) { Log("%s SetDir %s %08x", ArenaName(f).c_str(), VecText(dir).c_str(), Bits(roll)); }
void __stdcall F_Update(void *f) { Log("%s Update", ArenaName(f).c_str()); }
void __stdcall F_SetOn(void *f, bool on) { Log("%s SetOn %d", ArenaName(f).c_str(), on); }
int __stdcall F_NumChildren(void *f)
{
    Log("%s NumChildren", ArenaName(f).c_str());
    return g_arena.numChildren[FrameIndex(f)];
}
void *__stdcall F_FindChild(void *f, const char *name, uint32_t mask)
{
    Log("%s FindChildFrame %s %x", ArenaName(f).c_str(), name, mask);
    for (int k = 0; k < CHILD_NAME_COUNT; ++k)
        if (std::strcmp(name, CHILD_NAMES[k]) == 0 && g_arena.child[FrameIndex(f)][k] >= 0)
            return g_arena.frames[g_arena.child[FrameIndex(f)][k]];
    return nullptr;
}
void __stdcall F_UpdateBound(void *f) { Log("%s UpdateBound", ArenaName(f).c_str()); }
void __stdcall F_LinkTo(void *f, void *parent, uint32_t flags)
{
    Log("%s LinkTo %s %u", ArenaName(f).c_str(), parent ? ArenaName(parent).c_str() : "null", flags);
}
// LS3DF's (the original's import 0x007e5296; thiscall): logged, the matrix taken as made.
void __attribute__((thiscall)) O_UpdateWMatrix(void *f)
{
    Log("%s UpdateWMatrixProc", ArenaName(f).c_str());
    reinterpret_cast<I3D_frame *>(f)->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
}
void __stdcall F_SetAlpha(void *f, float alpha) { Log("%s SetAlpha %08x", ArenaName(f).c_str(), Bits(alpha)); }
void __stdcall F_Other(void *f) { Log("%s other slot", ArenaName(f).c_str()); }
void *__stdcall M_GetLevel(void *m, int index)
{
    Log("%s GetLevel %d", ArenaName(m).c_str(), index);
    return g_arena.level;
}
void __stdcall MAT_SetTexture(void *m, void *texture)
{
    Log("%s SetTexture %s", ArenaName(m).c_str(), texture ? (ArenaName(texture).empty() ? "?" : ArenaName(texture).c_str()) : "null");
    *reinterpret_cast<void **>(static_cast<uint8_t *>(m) + 0x0C) = texture;
}
void __stdcall MAT_Resolve(void *m) { Log("%s ResolveTexture", ArenaName(m).c_str()); }
uint32_t __stdcall T_Release(void *t)
{
    Log("%s Release", ArenaName(t).c_str());
    return 0;
}
int __stdcall G_LoadTexture(void *, const char *name, const char *alpha, uint32_t mode, void **out)
{
    Log("graph LoadTexture %s %s %x", name, alpha ? alpha : "null", mode);
    if (g_arena.loadResult >= 0)
        *out = g_arena.image;
    return g_arena.loadResult;
}
void __stdcall G_Other(void *) { Log("graph other slot"); }
int __stdcall G_ScrnX(void *)
{
    Log("graph ScrnX");
    return g_arena.scrnX;
}
int __stdcall G_ScrnY(void *)
{
    Log("graph ScrnY");
    return g_arena.scrnY;
}
uint32_t __stdcall IM_Release(void *i)
{
    Log("%s Release", ArenaName(i).c_str());
    return 0;
}

// The arena in a random state (the frames' places, flags, data; which frames have the button's children; the face
// groups' chain and textures).
void RandomArena()
{
    std::memset(&g_arena, 0, sizeof g_arena);
    for (int i = 0; i < 16; ++i) {
        *reinterpret_cast<void ***>(g_arena.frames[i]) = g_frameVt;
        I3D_frame *f = FrameAt(i);
        f->m_frameFlags = FRMFLAGS_WMATRIX_VALID | (P(0.2) ? FRMFLAGS_POS_LOCKED : 0) | (U32() & 0x0F00);
        for (int r = 0; r < 4; ++r) {
            f->m_localMatrix(r) = S_vector(F(-2, 2), F(-2, 2), F(-2, 2));
            f->m_worldMatrix(r) = S_vector(F(-2, 2), F(-2, 2), F(-2, 2));
        }
        f->m_data = reinterpret_cast<S_FrameData *>(uintptr_t(U32()));
        *reinterpret_cast<void **>(g_arena.frames[i] + 0x1E0) = g_arena.mesh;
        g_arena.numChildren[i] = P(0.85) ? 3 : I(0, 5);
        for (int k = 0; k < CHILD_NAME_COUNT; ++k)
            g_arena.child[i][k] = static_cast<int16_t>(k >= g_arenaNames ? -1 : P(0.9) ? I(0, 15) : -1);
        // The bound (+0x10: min, max; valid now and then), the type, the visual's world bound (+0x170, valid).
        f->m_frameFlags |= P(0.8) ? FRMFLAGS_BOUND_VALID : 0;
        const S_vector a(F(-1.5f, 1.5f), F(-1.5f, 1.5f), F(-1, 1)), b(F(-1.5f, 1.5f), F(-1.5f, 1.5f), F(-1, 1));
        f->m_bound.bbox.min = S_vector(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
        f->m_bound.bbox.max = S_vector(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
        if (P(0.05))
            f->m_bound.bbox.max = f->m_bound.bbox.min;
        f->m_type = P(0.8) ? FRAME_VISUAL : static_cast<E_I3D_FRAME_TYPE>(I(0, 8));
        *reinterpret_cast<uint32_t *>(g_arena.frames[i] + 0x1D0) = VISFLAGS_WBOUND_VALID;
        const S_vector c(F(-1, 1), F(-1, 1), F(-1, 1)), d(F(-1, 1), F(-1, 1), F(-1, 1));
        *reinterpret_cast<S_vector *>(g_arena.frames[i] + 0x170) = S_vector(std::min(c.x, d.x), std::min(c.y, d.y), std::min(c.z, d.z));
        // (I3D_bbox: min, its w, max at +0x10, its w.)
        *reinterpret_cast<S_vector *>(g_arena.frames[i] + 0x180) = S_vector(std::max(c.x, d.x), std::max(c.y, d.y), std::max(c.z, d.z));
    }
    *reinterpret_cast<void ***>(g_arena.mesh) = g_meshVt;
    *reinterpret_cast<void **>(g_arena.level + 0x5C) = g_arena.groups[0];
    for (int g = 0; g < 4; ++g) {
        *reinterpret_cast<void ***>(g_arena.groups[g]) = g_groupVt;
        *reinterpret_cast<void **>(g_arena.groups[g] + 4) = g_arena.materials[I(0, 2)];   // (each has one: the models')
        *reinterpret_cast<void **>(g_arena.groups[g] + 0xC) = g < 3 && P(0.8) ? g_arena.groups[g + 1] : nullptr;
    }
    static const char *const files[] = {"menu.bmp", "face.tga", "", "x"};
    for (int t = 0; t < 4; ++t) {
        *reinterpret_cast<void ***>(g_arena.textures[t]) = g_textureVt;
        std::strcpy(g_arena.names[t], files[t]);
        *reinterpret_cast<const char **>(g_arena.textures[t] + 0x18) = P(0.9) ? g_arena.names[t] : nullptr;
    }
    for (int m = 0; m < 3; ++m) {
        *reinterpret_cast<void ***>(g_arena.materials[m]) = g_materialVt;
        *reinterpret_cast<uint32_t *>(g_arena.materials[m] + 8) = P(0.2) ? I3DMATERIAL_TEXTURE_PENDING_BIT : 0;
        *reinterpret_cast<void **>(g_arena.materials[m] + 0xC) = g_arena.textures[I(0, 3)];
    }
    *reinterpret_cast<int *>(g_arena.cursor + 0x40) = P(0.3) ? Pick({0, 299, 300, 301, 499, 500, 501}) : I(0, 2000);
    g_arena.cursorPos = S_vector(F(-1, 1), F(-1, 1), -1.0f);
    g_arena.cursorDown = P(0.4);
    g_arena.cursorMove = S_vector(P(0.3) ? 0.0f : F(-0.2f, 0.2f), P(0.3) ? 0.0f : F(-0.2f, 0.2f), -1.0f);
    g_arena.loadResult = P(0.3) ? -1 : 0;
    g_arena.scrnX = int16_t(Pick({640, 800, 1024, 1600}));
    *reinterpret_cast<int *>(g_arena.zone + 0x98) = P(0.8) ? I(0, 2) : I(-2, 5);
    g_arena.scrnY = int16_t(Pick({480, 600, 768, 1200}));
    *reinterpret_cast<void ***>(g_arena.graph) = g_graphVt;
    *reinterpret_cast<void ***>(g_arena.image) = g_imageVt;
}
void SetUpArena()
{
    for (void *&s : g_frameVt)
        s = reinterpret_cast<void *>(F_Other);
    g_frameVt[0x08 / 4] = reinterpret_cast<void *>(F_SetPos);
    g_frameVt[0x0C / 4] = reinterpret_cast<void *>(F_SetDir);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_Update);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(F_SetOn);
    g_frameVt[0x3C / 4] = reinterpret_cast<void *>(F_NumChildren);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    g_frameVt[0xA8 / 4] = reinterpret_cast<void *>(F_SetAlpha);
    g_frameVt[0x58 / 4] = reinterpret_cast<void *>(F_UpdateBound);
    g_frameVt[0x38 / 4] = reinterpret_cast<void *>(F_LinkTo);
    for (void *&s : g_meshVt)
        s = reinterpret_cast<void *>(F_Other);
    g_meshVt[1] = reinterpret_cast<void *>(M_GetLevel);
    for (void *&s : g_materialVt)
        s = reinterpret_cast<void *>(F_Other);
    g_materialVt[0x1C / 4] = reinterpret_cast<void *>(MAT_SetTexture);
    g_materialVt[0x44 / 4] = reinterpret_cast<void *>(MAT_Resolve);
    for (void *&s : g_textureVt)
        s = reinterpret_cast<void *>(T_Release);
    for (void *&s : g_groupVt)
        s = reinterpret_cast<void *>(F_Other);
    for (void *&s : g_graphVt)
        s = reinterpret_cast<void *>(G_Other);
    g_graphVt[0x3C / 4] = reinterpret_cast<void *>(G_LoadTexture);
    g_graphVt[0x18 / 4] = reinterpret_cast<void *>(G_ScrnX);
    g_graphVt[0x1C / 4] = reinterpret_cast<void *>(G_ScrnY);
    for (void *&s : g_imageVt)
        s = reinterpret_cast<void *>(IM_Release);
}

// ---- the text boxes ----
const uint32_t OB_SIZE = 0x9C, OB_RUNS = 0x34, OB_DIMMED = 0x71, OB_MARGIN = 0x90, OB_COLOUR = 0x94, ORIG_RUN = 5;
// The known boxes: pairs made by ToOrig (named b<i>), and those made during a call (n<k>, per side in order).
std::vector<std::pair<C_TextBox *, uint8_t *>> g_boxes;
std::vector<void *> g_newPortBoxes, g_newOrigBoxes;
void *g_origBoxVt[8];
uint32_t KindBoxAt(size_t node);
C_TextBox *PortBoxOf(size_t node);
std::string BoxName(const void *box)
{
    // (By its control: the original's text control makes its box inline, its constructor not called.)
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        if (!KindBoxAt(i))
            continue;
        if (box == PortBoxOf(i) || (i < g_origNodes.size() && box == OF<void *>(g_origNodes[i], KindBoxAt(i))))
            return "box(c" + std::to_string(i) + ")";
    }
    for (size_t i = 0; i < g_boxes.size(); ++i)
        if (box == g_boxes[i].first || box == g_boxes[i].second)
            return "b" + std::to_string(i);
    for (size_t i = 0; i < g_newPortBoxes.size(); ++i)
        if (box == g_newPortBoxes[i])
            return "n" + std::to_string(i);
    for (size_t i = 0; i < g_newOrigBoxes.size(); ++i)
        if (box == g_newOrigBoxes[i])
            return "n" + std::to_string(i);
    return box ? "box?" : "null";
}
std::string Text(const C_str &s) { return s.m_rep ? "'" + std::string(s.c_str()) + "'" : "(null)"; }
// The loggers (each side's box calls these).
void BoxSetText(const void *b, const C_str &t, int style, int index) { Log("%s SetText %s %d %d", BoxName(b).c_str(), Text(t).c_str(), style, index); }
void BoxSetTextId(const void *b, int id, int style, int index) { Log("%s SetTextId %d %d %d", BoxName(b).c_str(), id, style, index); }
void BoxAddText(const void *b, const C_str &t, int style) { Log("%s AddText %s %d", BoxName(b).c_str(), Text(t).c_str(), style); }
void BoxAddTextId(const void *b, int id, int style) { Log("%s AddTextId %d %d", BoxName(b).c_str(), id, style); }
void BoxRemoveRun(const void *b, int index) { Log("%s RemoveRun %d", BoxName(b).c_str(), index); }
void BoxClear(const void *b) { Log("%s Clear", BoxName(b).c_str()); }
void BoxAttach(const void *b, const void *scene, const void *frame)
{
    Log("%s AttachTo %p %s", BoxName(b).c_str(), scene, frame ? ArenaName(frame).c_str() : "null");
}
void BoxRender(const void *b) { Log("%s Render", BoxName(b).c_str()); }
void BoxDelete(const void *b) { Log("%s delete", BoxName(b).c_str()); }

// The original's.
void *__attribute__((thiscall)) OB_Ctor(void *self)
{
    std::memset(self, 0, OB_SIZE);
    *reinterpret_cast<void ***>(self) = g_origBoxVt;
    OF<uint8_t>(static_cast<uint8_t *>(self), OB_MARGIN) = 10;
    g_newOrigBoxes.push_back(self);
    Log("%s new", BoxName(self).c_str());
    return self;
}
bool __attribute__((thiscall)) OB_SetText(void *b, const C_str &t, int style, int index) { BoxSetText(b, t, uint8_t(style), index); return true; }
bool __attribute__((thiscall)) OB_SetTextId(void *b, int id, int style, int index) { BoxSetTextId(b, id, style, index); return true; }
bool __attribute__((thiscall)) OB_AddText(void *b, const C_str &t, int style) { BoxAddText(b, t, uint8_t(style)); return true; }
bool __attribute__((thiscall)) OB_AddTextId(void *b, int id, int style) { BoxAddTextId(b, id, uint8_t(style)); return true; }
bool __attribute__((thiscall)) OB_RemoveRun(void *b, int index) { BoxRemoveRun(b, index); return true; }
void __attribute__((thiscall)) OB_Clear(void *b) { BoxClear(b); }
void __attribute__((thiscall)) OB_AttachTo(void *b, void *scene, void *frame) { BoxAttach(b, scene, frame); }
void __attribute__((thiscall)) OB_Render(void *b) { BoxRender(b); }
// The box's answers (both sides': a round's).
struct S_BoxAnswers {
    bool editRun = true, hoverAt = true, insert = true;
    int16_t hovered = 0, selection = -1, selectRun = 0, scrollRange = 0;
    float centreX = 0, centreY = 0;
    const char *selected = "sel";
} g_box;
C_str RunText(int index)
{
    char text[32];
    std::snprintf(text, sizeof text, index % 3 == 0 ? "Run%d" : "run%d", index);
    return C_str(text);
}
bool __attribute__((thiscall)) OB_EditRun(void *b, int run)
{
    Log("%s EditRun %d", BoxName(b).c_str(), int16_t(run));   // (16 bits: the original reads a word)
    return g_box.editRun;
}
void __attribute__((thiscall)) OB_TickCaret(void *b, int time) { Log("%s TickCaret %d", BoxName(b).c_str(), time); }
int16_t __attribute__((thiscall)) OB_SelectHovered(void *b)
{
    Log("%s SelectHovered", BoxName(b).c_str());
    return g_box.hovered;
}
int16_t __attribute__((thiscall)) OB_GetSelection(void *b)
{
    Log("%s GetSelection", BoxName(b).c_str());
    return g_box.selection;
}
// (SelectRun's and GetLineCentre's line: 16 bits in the original, the word above it the caller's stack.)
int16_t __attribute__((thiscall)) OB_SelectRun(void *b, int run)
{
    Log("%s SelectRun %d", BoxName(b).c_str(), int16_t(run));
    return g_box.selectRun;
}
int16_t __attribute__((thiscall)) OB_GetScrollRange(void *b)
{
    Log("%s GetScrollRange", BoxName(b).c_str());
    return g_box.scrollRange;
}
bool __attribute__((thiscall)) OB_HoverAt(void *b, int x, int y)
{
    Log("%s HoverAt %d %d", BoxName(b).c_str(), x, y);
    return g_box.hoverAt;
}
bool __attribute__((thiscall)) OB_InsertText(void *b, const C_str &t, int style, int index)
{
    Log("%s InsertText %s %d %d", BoxName(b).c_str(), Text(t).c_str(), uint8_t(style), index);
    return g_box.insert;
}
bool __attribute__((thiscall)) OB_SetSelectedText(void *b, const C_str &t)
{
    Log("%s SetSelectedText %s", BoxName(b).c_str(), Text(t).c_str());
    return true;
}
// (Returned by value: through a hidden pointer, the original's convention for these.)
void *__attribute__((thiscall)) OB_GetSelectedText(void *b, void *out)
{
    Log("%s GetSelectedText", BoxName(b).c_str());
    new (out) C_str(g_box.selected);
    return out;
}
void *__attribute__((thiscall)) OB_GetRunText(void *b, void *out, int index)
{
    Log("%s GetRunText %d", BoxName(b).c_str(), index);
    new (out) C_str(RunText(index));
    return out;
}
void *__attribute__((thiscall)) OB_GetLineCentre(void *b, void *out, int line)
{
    Log("%s GetLineCentre %d", BoxName(b).c_str(), int16_t(line));
    *static_cast<S_vector4 *>(out) = S_vector4{g_box.centreX, g_box.centreY, 0.0f, 0.0f};
    return out;
}
bool __attribute__((thiscall)) OB_SetHover(void *b, int line)
{
    Log("%s SetHover %d", BoxName(b).c_str(), line);
    return true;
}
bool __attribute__((thiscall)) OB_ClearHover(void *b)
{
    Log("%s ClearHover", BoxName(b).c_str());
    return true;
}
void __attribute__((thiscall)) OB_UpdateScrollRange(void *b) { Log("%s UpdateScrollRange", BoxName(b).c_str()); }
bool __attribute__((thiscall)) OB_ScrollTo(void *b, int line) { Log("%s ScrollTo %d", BoxName(b).c_str(), line); return true; }
void *__attribute__((thiscall)) OB_Delete(void *b, unsigned flags)
{
    BoxDelete(b);
    (void)flags;   // (the test's blocks are freed with the round's)
    return b;
}

// The paged boxes' (0xC8 bytes: the page +0x9C, the pages' vector +0xB8 / +0xBC; vtable 0x008154a4).
const uint32_t OPB_SIZE = 0xC8, OPB_PAGES = 0xB8;
int g_pageBreak;   // LastPageBreak's answer
void __attribute__((thiscall)) OPB_AddArea(void *b, void *scene, void *frame) { Log("%s AddArea %p %s", BoxName(b).c_str(), scene, ArenaName(frame).c_str()); }
void __attribute__((thiscall)) OPB_SetPage(void *b, int page) { Log("%s SetPage %d", BoxName(b).c_str(), page); }
void __attribute__((fastcall)) OPB_Relayout(void *b) { Log("%s Relayout", BoxName(b).c_str()); }
int __attribute__((fastcall)) OPB_LastPageBreak(void *b)
{
    Log("%s LastPageBreak", BoxName(b).c_str());
    return g_pageBreak;
}

// ---- the game's texts (Get: its C_str returned through a hidden pointer) ----
C_str TextFor(int id)
{
    char text[32];
    std::snprintf(text, sizeof text, "text#%d", id);
    return C_str(text);
}
void *__attribute__((thiscall)) OG_Get(void *, void *out, int id)
{
    Log("texts Get %d", id);
    new (out) C_str(TextFor(id));
    return out;
}

// ---- the cursor (one block) ----
C_Cursor *TheCursor() { return reinterpret_cast<C_Cursor *>(g_arena.cursor); }
bool g_cursorMade = true;   // Acquire's answer: the cursor, or none
void *__attribute__((fastcall)) OC_Acquire(void *scene)
{
    Log("cursor Acquire %p", scene);
    return g_cursorMade ? g_arena.cursor : nullptr;
}
void __attribute__((fastcall)) OC_Release(void *c) { Log("%s Release", c == g_arena.cursor ? "cursor" : "cursor?"); }
bool __attribute__((thiscall)) OC_IsButtonDown(void *, uint32_t buttons)
{
    Log("cursor IsButtonDown %u", buttons);
    return g_arena.cursorDown != 0;
}
void __attribute__((thiscall)) OC_GetScreen(void *, float *x, float *y, float *buttons)
{
    Log("cursor GetScreenPosition %d %d %d", x != nullptr, y != nullptr, buttons != nullptr);
    if (x)
        *x = g_arena.cursorPos.x * 400.0f + 400.0f;
    if (y)
        *y = g_arena.cursorPos.y * 300.0f + 300.0f;
    if (buttons)
        *buttons = float(g_arena.cursorDown);
}
// (Both return their out pointer, as the original's do: its callers read it through EAX.)
S_vector *__attribute__((thiscall)) OC_GetMove(void *, S_vector *move)
{
    Log("cursor GetMove");
    *move = g_arena.cursorMove;
    return move;
}
S_vector *__attribute__((thiscall)) OC_GetPosition(void *, S_vector *pos)
{
    Log("cursor GetPosition");
    *pos = g_arena.cursorPos;
    return pos;
}

// ---- the callbacks: per side, mapped (part 48's) ----
std::vector<int> g_cbResults;
size_t g_cbNext;
bool CbResult() { return g_cbResults.empty() ? true : g_cbResults[g_cbNext++ % g_cbResults.size()]; }
// A callback's value as logged (a part's: a pointer to a text shown as the text; null: hex).
std::string (*g_cbValue)(uint32_t notification, uint32_t value) = nullptr;
std::string CbValue(uint32_t notification, uint32_t value)
{
    if (g_cbValue)
        return g_cbValue(notification, value);
    char text[16];
    std::snprintf(text, sizeof text, "%x", value);
    return text;
}
#define CALLBACK_PAIR(K)                                                                                                     \
    bool PortCb##K(void *object, void *argument, C_Control *control, uint32_t notification, uint32_t value)                \
    {                                                                                                                        \
        Log("cb%d %p %p %s %x %s", K, object, argument, Name(control).c_str(), notification, CbValue(notification, value).c_str()); \
        return CbResult();                                                                                                   \
    }                                                                                                                        \
    bool __attribute__((thiscall)) OrigCb##K(void *object, void *argument, void *control, uint32_t notification, uint32_t value) \
    {                                                                                                                        \
        Log("cb%d %p %p %s %x %s", K, object, argument, Name(control).c_str(), notification, CbValue(notification, value).c_str()); \
        return CbResult();                                                                                                   \
    }
CALLBACK_PAIR(0)
CALLBACK_PAIR(1)
CALLBACK_PAIR(2)
const uintptr_t g_portCbs[3] = {reinterpret_cast<uintptr_t>(PortCb0), reinterpret_cast<uintptr_t>(PortCb1), reinterpret_cast<uintptr_t>(PortCb2)};
const uintptr_t g_origCbs[3] = {reinterpret_cast<uintptr_t>(OrigCb0), reinterpret_cast<uintptr_t>(OrigCb1), reinterpret_cast<uintptr_t>(OrigCb2)};
// The port's functions the original's are (a part's: the controls' own callbacks).
std::vector<std::pair<uintptr_t, uintptr_t>> g_fnPairs;
uintptr_t ToOrigFn(uintptr_t f)
{
    for (int i = 0; i < 3; ++i)
        if (f == g_portCbs[i])
            return g_origCbs[i];
    for (const auto &pair : g_fnPairs)
        if (f == pair.first)
            return pair.second;
    return f;
}
uint8_t g_objects[2][16];

// (Zeroed, as the port's members are: the original's new edit buffer is not cleared, a flaw the port fixes; typing comes
// only after BeginEdit cleared it.)
void *__cdecl O_New(size_t size) { return std::calloc(1, size ? size : 1); }
void __cdecl O_Delete(void *p) { std::free(p); }

// ---- messages ----
uint32_t OrigText(const C_str &text)
{
    if (!text.m_rep)
        return 0;
    C_str copy(text.c_str());
    void *rep = copy.m_rep;
    copy.m_rep = nullptr;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rep));
}
std::string TextOf(uint32_t rep)
{
    if (!rep)
        return "(null)";
    C_str s;
    s.m_rep = reinterpret_cast<C_str::Rep *>(static_cast<uintptr_t>(rep));
    std::string out = s.c_str();
    s.m_rep = nullptr;
    return out;
}
std::string TextOf(const C_str &s) { return s.m_rep ? std::string(s.c_str()) : "(null)"; }
uint8_t *ToOrigNode(const C_Control *c);
// A word that may be a control (a ticker's value2): the original's for the port's.
uint32_t ToOrigWord(uintptr_t w)
{
    for (size_t i = 0; i < g_portNodes.size() && i < g_origNodes.size(); ++i)
        if (w == reinterpret_cast<uintptr_t>(g_portNodes[i]))
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_origNodes[i]));
    return static_cast<uint32_t>(ToOrigFn(w));
}
void MessageToOrig(const S_ControlMessage &m, uint8_t *o)
{
    const uint32_t words[6] = {m.target, m.id, m.value, ToOrigWord(m.value2), ToOrigWord(m.value3), OrigText(m.text)};
    std::memcpy(o, words, 0x18);
}
// The codes of the messages the original builds on its stack with value2 / value3 unset (the port's Message and Route
// helpers: 0): a 0 of the port's there not compared (the controls that take them read neither). value2: the arrows' and
// the slider's routes (scroll up / down, previous / next, the slider moved), the slider's get, the set position; value3:
// those, the tickers (their value2 always set) and the slider's set (its value2 the value). Part 52: the selection
// list's set count (both), the edit controls' start and stop editing (value3: value2 the control).
const uint32_t STACK_VALUE2_CODES[] = {0x1000023, 0x1000024, 0x1000025, 0x200001b, 0x200001c, 0x1000022, 0x200002b, 0x1000020};
const uint32_t STACK_VALUE3_CODES[] = {0x200007a, 0x1000023, 0x1000024, 0x1000025, 0x200001b, 0x200001c, 0x1000021, 0x1000022, 0x200002b,
                                       0x1000020, 0x20000bf, 0x20000c0};
// A part's (the same allowance for its codes).
std::vector<uint32_t> g_stackValue2Codes, g_stackValue3Codes;
std::string MessageDiffers(const S_ControlMessage &m, const uint8_t *o)
{
    uint32_t w[6];
    std::memcpy(w, o, 0x18);
    for (uint32_t code : g_stackValue2Codes)
        if ((m.id & CONTROL_MSG_MASK) == code && m.value2 == 0)
            w[3] = 0;
    for (uint32_t code : g_stackValue3Codes)
        if ((m.id & CONTROL_MSG_MASK) == code && m.value3 == 0)
            w[4] = 0;
    for (uint32_t code : STACK_VALUE2_CODES)
        if ((m.id & CONTROL_MSG_MASK) == code && m.value2 == 0)
            w[3] = 0;
    for (uint32_t code : STACK_VALUE3_CODES)
        if ((m.id & CONTROL_MSG_MASK) == code && m.value3 == 0)
            w[4] = 0;
    char text[200];
    if (w[0] != m.target || w[1] != m.id || w[2] != m.value || w[3] != ToOrigWord(m.value2) || w[4] != ToOrigWord(m.value3)) {
        std::snprintf(text, sizeof text, "%x %x %x %x %x / %x %x %x %x %x", w[0], w[1], w[2], w[3], w[4], m.target, m.id, m.value,
                      unsigned(m.value2), unsigned(m.value3));
        return text;
    }
    if (TextOf(w[5]) != TextOf(m.text))
        return "text " + TextOf(w[5]) + " / " + TextOf(m.text);
    return "";
}

// MSVC6 vectors (first, last, end at +4, +8, +0xC from `at`), in buffers of the test's (the original may free them).
void OVecPut(uint8_t *block, uint32_t at, const void *data, size_t bytes)
{
    if (bytes == 0 && P(0.5)) {
        OF<void *>(block, at + 4) = OF<void *>(block, at + 8) = OF<void *>(block, at + 12) = nullptr;
        return;
    }
    const size_t spare = P(0.5) ? 0 : size_t(I(1, 3)) * 4;
    auto *buffer = static_cast<uint8_t *>(std::malloc(bytes + spare + 1));
    std::memcpy(buffer, data, bytes);
    OF<uint8_t *>(block, at + 4) = buffer;
    OF<uint8_t *>(block, at + 8) = buffer + bytes;
    OF<uint8_t *>(block, at + 12) = buffer + bytes + spare;
}
size_t OVecBytes(const uint8_t *block, uint32_t at) { return size_t(OF<uint8_t *>(block, at + 8) - OF<uint8_t *>(block, at + 4)); }
uint8_t *OVecData(const uint8_t *block, uint32_t at) { return OF<uint8_t *>(block, at + 4); }

// ---- the original's controls: a vtable copy per class, its Refresh, Tick and HandleMessageCopy logged ----
struct S_LoggedVt {
    uint32_t original;
    uint32_t slots[0x40];
};
std::vector<S_LoggedVt *> g_loggedVts;
uint32_t LoggedVt(uint32_t original)
{
    for (S_LoggedVt *v : g_loggedVts)
        if (v->original == original)
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(v->slots));
    return 0;
}
uint32_t OriginalSlot(const void *self, uint32_t slot)
{
    const uint32_t vt = *static_cast<const uint32_t *>(self);
    for (S_LoggedVt *v : g_loggedVts)
        if (vt == reinterpret_cast<uintptr_t>(v->slots))
            return OriginalAt<uint32_t *>(v->original)[slot / 4];
    return OriginalAt<uint32_t *>(vt)[slot / 4];
}
void __attribute__((thiscall)) O_LoggedRefresh(void *self)
{
    Log("refresh %s", Name(self).c_str());
    reinterpret_cast<void(__attribute__((thiscall)) *)(void *)>(OriginalSlot(self, 0x18))(self);
}
void __attribute__((thiscall)) O_LoggedTick(void *self, int time)
{
    Log("tick %s %d", Name(self).c_str(), time);
    reinterpret_cast<void(__attribute__((thiscall)) *)(void *, int)>(OriginalSlot(self, 0x1C))(self, time);
}
bool __attribute__((thiscall)) O_LoggedCopy(void *self, const uint32_t *message)
{
    Log("copy %s target %x id %x value %x", Name(self).c_str(), message[0], message[1], message[2]);
    return reinterpret_cast<bool(__attribute__((thiscall)) *)(void *, const uint32_t *)>(OriginalSlot(self, 0x74))(self, message);
}
void AddLoggedVt(uint32_t original)
{
    if (LoggedVt(original))
        return;
    auto *v = new S_LoggedVt;
    v->original = original;
    std::memcpy(v->slots, OriginalAt<void *>(original), sizeof v->slots);
    v->slots[0x18 / 4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(O_LoggedRefresh));
    v->slots[0x74 / 4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(O_LoggedCopy));
    v->slots[0x1C / 4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(O_LoggedTick));
    g_loggedVts.push_back(v);
}

template <class T> class C_Logged : public T {
public:
    using T::T;
    void Refresh() override
    {
        Log("refresh %s", Name(this).c_str());
        T::Refresh();
    }
    void Tick(int time) override
    {
        Log("tick %s %d", Name(this).c_str(), time);
        T::Tick(time);
    }
    bool HandleMessageCopy(const S_ControlMessage &message) override
    {
        Log("copy %s target %x id %x value %x", Name(this).c_str(), message.target, message.id, message.value);
        return T::HandleMessageCopy(message);
    }
};

// ---- the kinds (each part's table) ----
struct S_Kind {
    const char *name;
    uint32_t vtable;   // the original's
    uint32_t size;     // the original's
    C_Control *(*make)(I3D_scene *scene, uint32_t id, C_Control *parent);
    // A node of this kind given a random state of its own fields (after the base's).
    void (*randomize)(C_Control *c);
    // The class's fields into the original's block; their difference (none: "").
    void (*toOrig)(C_Control *c, uint8_t *o);
    std::string (*differs)(C_Control *c, const uint8_t *o);
    uint32_t boxAt = 0;   // a text control's box (+0x6C), else 0
    C_TextBox *(*portBox)(C_Control *c) = nullptr;   // its box (null: a C_TextControl's)
    bool boxRequired = false;   // its box used unchecked everywhere (DropBox leaves it)
    // A control it owns outside its children (the mark's title; deleted with it): the original's offset and the port's
    // field (0 / null: none).
    uint32_t ownedAt = 0;
    C_Control **(*ownedSlot)(C_Control *c) = nullptr;
};
std::vector<const S_Kind *> g_nodeKinds;   // per node
const S_Kind *g_kinds = nullptr;   // the part's kinds
int g_kindCount = 0;
// A node's owned control on each side (null: none, or the kind owns none).
C_Control *PortOwned(size_t node) { return g_nodeKinds[node]->ownedSlot ? *g_nodeKinds[node]->ownedSlot(g_portNodes[node]) : nullptr; }
uint8_t *OrigOwned(size_t node, const uint8_t *o) { return g_nodeKinds[node]->ownedAt ? OF<uint8_t *>(o, g_nodeKinds[node]->ownedAt) : nullptr; }
// Nodes adopted during the call (made by it: the original's with its real vtable, not the logged copy).
std::vector<bool> g_freshNodes;
bool g_differsFresh;   // (set for the kinds' differs: the node was made by the call)
const S_Kind *KindByVtable(uint32_t vtable)
{
    for (int k = 0; k < g_kindCount; ++k)
        if (g_kinds[k].vtable == vtable)
            return &g_kinds[k];
    return nullptr;
}
uint32_t KindBoxAt(size_t node) { return node < g_nodeKinds.size() ? g_nodeKinds[node]->boxAt : 0; }
C_TextBox *PortBoxOf(size_t node)
{
    if (!KindBoxAt(node))
        return nullptr;
    if (g_nodeKinds[node]->portBox)
        return g_nodeKinds[node]->portBox(g_portNodes[node]);
    return static_cast<C_TextControl *>(g_portNodes[node])->m_box;
}

std::string Name(const void *p)
{
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (p == g_portNodes[i] || (i < g_origNodes.size() && p == g_origNodes[i]))
            return "c" + std::to_string(i);
    if (!ArenaName(p).empty())
        return ArenaName(p);
    char text[24];
    std::snprintf(text, sizeof text, "%p", p);
    return text;
}
uint8_t *ToOrigNode(const C_Control *c)
{
    for (size_t i = 0; i < g_portNodes.size() && i < g_origNodes.size(); ++i)
        if (c == g_portNodes[i])
            return g_origNodes[i];
    return reinterpret_cast<uint8_t *>(const_cast<C_Control *>(c));
}
uint8_t *ToOrigPtr(const void *p)
{
    if (!p)
        return nullptr;
    for (size_t i = 0; i < g_portNodes.size() && i < g_origNodes.size(); ++i)
        if (p == g_portNodes[i])
            return g_origNodes[i];
    for (auto &b : g_boxes)
        if (p == b.first)
            return b.second;
    return static_cast<uint8_t *>(const_cast<void *>(p));
}
// A pointer field: the same thing on both sides (a control, a box, an arena object, the cursor), by name.
std::string PtrName(const void *p)
{
    if (!p)
        return "null";
    for (auto &b : g_boxes)
        if (p == b.first || p == b.second)
            return BoxName(p);
    for (void *b : g_newPortBoxes)
        if (p == b)
            return BoxName(p);
    for (void *b : g_newOrigBoxes)
        if (p == b)
            return BoxName(p);
    if (p == g_arena.cursor)
        return "cursor";
    return Name(p);
}

// The text control's box (+0x6C) into the original's: a box of its own made for it.
void BoxToOrig(C_TextBox *box, uint8_t *o, uint32_t at)
{
    if (!box) {
        OF<void *>(o, at) = nullptr;
        return;
    }
    auto *paged = dynamic_cast<C_PagedTextBox *>(box);
    auto *ob = static_cast<uint8_t *>(OrigAlloc(OPB_SIZE));
    OF<uint32_t>(ob, 0) = paged ? 0x008154a4 : 0x00810458;   // (the original's own: its slots redirected)
    if (paged) {
        OF<int>(ob, 0x9C) = paged->m_page;
        auto *pages = static_cast<uint8_t *>(OrigAlloc(paged->m_pages.size() * 4 + 1));
        OF<uint8_t *>(ob, OPB_PAGES) = paged->m_pages.empty() && P(0.5) ? nullptr : pages;
        OF<uint8_t *>(ob, OPB_PAGES + 4) = OF<uint8_t *>(ob, OPB_PAGES) + paged->m_pages.size() * 4;
    }
    OF<int16_t>(ob, 4) = box->m_screenWidth;
    OF<int16_t>(ob, 6) = box->m_screenHeight;
    OF<void *>(ob, 0x28) = box->m_fonts;
    OF<int>(ob, 0x78) = box->m_scroll;
    OF<bool>(ob, OB_DIMMED) = box->m_dimmed;
    OF<uint8_t>(ob, OB_MARGIN) = box->m_margin;
    OF<uint32_t>(ob, OB_COLOUR) = box->m_colour;
    auto *runs = static_cast<uint8_t *>(OrigAlloc(box->m_runs.size() * ORIG_RUN + 1));
    OF<uint8_t *>(ob, OB_RUNS) = runs;
    OF<uint8_t *>(ob, OB_RUNS + 4) = runs + box->m_runs.size() * ORIG_RUN;
    OF<void *>(o, at) = ob;
    g_boxes.push_back({box, ob});
}
std::string BoxDiffers(C_TextBox *box, const uint8_t *o, uint32_t at)
{
    const uint8_t *ob = OF<uint8_t *>(o, at);
    // (Both or neither; a box ToOrig paired, its pair; else one made in the call on each side.)
    if ((box == nullptr) != (ob == nullptr))
        return "box " + std::string(ob ? "set" : "null") + " / " + (box ? "set" : "null");
    if (!box)
        return "";
    bool paired = false;
    for (auto &b : g_boxes) {
        if (b.first == box) {
            paired = true;
            if (b.second != ob)
                return "box: not its pair";
        } else if (b.second == ob) {
            return "box: another's pair";
        }
    }
    (void)paired;
    char text[120];
    if (auto *paged = dynamic_cast<C_PagedTextBox *>(box)) {
        const size_t pages = size_t(OF<uint8_t *>(ob, OPB_PAGES + 4) - OF<uint8_t *>(ob, OPB_PAGES)) / 4;
        if (OF<uint32_t>(ob, 0) != 0x008154a4 || OF<int>(ob, 0x9C) != paged->m_page || pages != paged->m_pages.size())
            return "paged box";
    }
    const size_t runs = size_t(OF<uint8_t *>(ob, OB_RUNS + 4) - OF<uint8_t *>(ob, OB_RUNS)) / ORIG_RUN;
    if (OF<bool>(ob, OB_DIMMED) != box->m_dimmed || OF<uint8_t>(ob, OB_MARGIN) != box->m_margin || OF<uint32_t>(ob, OB_COLOUR) != box->m_colour ||
        runs != box->m_runs.size() || OF<int16_t>(ob, 4) != box->m_screenWidth || OF<int16_t>(ob, 6) != box->m_screenHeight ||
        OF<void *>(ob, 0x28) != box->m_fonts || OF<int>(ob, 0x78) != box->m_scroll) {
        std::snprintf(text, sizeof text, "box fields %d %d %x %u / %d %d %x %u", OF<bool>(ob, OB_DIMMED), OF<uint8_t>(ob, OB_MARGIN),
                      OF<uint32_t>(ob, OB_COLOUR), unsigned(runs), box->m_dimmed, box->m_margin, box->m_colour, unsigned(box->m_runs.size()));
        return text;
    }
    return "";
}
void RandomBoxFields(C_TextBox *box)
{
    box->m_dimmed = P(0.3);
    box->m_margin = P(0.5) ? 10 : uint8_t(I(0, 255));
    box->m_colour = P(0.5) ? 0 : U32();
    box->m_runs.resize(size_t(P(0.3) ? 0 : I(1, 6)));
    box->m_screenWidth = int16_t(I(0, 2000));
    box->m_screenHeight = int16_t(I(0, 2000));
    box->m_fonts = reinterpret_cast<C_FontManager *>(g_arena.fonts);
    box->m_scroll = P(0.5) ? 0 : I(-5, 400);
}
void RandomPagedBox(C_PagedTextBox *&box)
{
    box = new C_PagedTextBox();
    RandomBoxFields(box);
    box->m_page = I(-1, 5);
    box->m_pages.resize(size_t(I(0, 5)), nullptr);
    g_pageBreak = I(-1, 4);
}
void RandomBox(C_TextBox *&box)
{
    // (Always one: the controls use it unchecked but in Refresh and SetInactive, whose tests take it away.)
    box = new C_TextBox();
    RandomBoxFields(box);
}

// The base's fields (part 48's) into the original's block, and back compared.
const uint32_t O_CHILDREN = 0x04, O_ROUTES = 0x18, O_CALLBACKS = 0x2C, O_ANY = 0x40, O_PARENT = 0x4C, O_FRAME = 0x50, O_TYPE = 0x54,
               O_VALUE = 0x58, O_INDEX = 0x5C, O_ID = 0x60, O_INACTIVE = 0x64, O_SHOWN = 0x65, O_TAKES = 0x66, O_SCENE = 0x68;
void ToOrig()
{
    for (void *p : g_origBlocks)
        std::free(p);
    g_origBlocks.clear();
    g_origNodes.clear();
    g_boxes.clear();
    g_freshNodes.assign(g_portNodes.size(), false);
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        g_origNodes.push_back(static_cast<uint8_t *>(OrigAlloc(g_nodeKinds[i]->size + 0x40)));
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        C_Control &c = *g_portNodes[i];
        uint8_t *o = g_origNodes[i];
        OF<uint32_t>(o, 0) = LoggedVt(g_nodeKinds[i]->vtable);
        std::vector<uint32_t> children;
        for (C_Control *child : c.m_children)
            children.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ToOrigNode(child))));
        OVecPut(o, O_CHILDREN, children.data(), children.size() * 4);
        std::vector<uint8_t> routes(c.m_routes.size() * 0x18);
        for (size_t k = 0; k < c.m_routes.size(); ++k)
            MessageToOrig(c.m_routes[k], routes.data() + k * 0x18);
        OVecPut(o, O_ROUTES, routes.data(), routes.size());
        std::vector<uint32_t> callbacks;
        for (const S_ControlCallback &cb : c.m_callbacks) {
            callbacks.push_back(cb.notification);
            callbacks.push_back(ToOrigWord(reinterpret_cast<uintptr_t>(cb.object)));
            callbacks.push_back(static_cast<uint32_t>(ToOrigFn(reinterpret_cast<uintptr_t>(cb.function))));
        }
        OVecPut(o, O_CALLBACKS, callbacks.data(), callbacks.size() * 4);
        OF<uint32_t>(o, O_ANY) = c.m_anyCallback.notification;
        OF<uint32_t>(o, O_ANY + 4) = ToOrigWord(reinterpret_cast<uintptr_t>(c.m_anyCallback.object));
        OF<uint32_t>(o, O_ANY + 8) = static_cast<uint32_t>(ToOrigFn(reinterpret_cast<uintptr_t>(c.m_anyCallback.function)));
        OF<uint8_t *>(o, O_PARENT) = c.m_parent ? ToOrigNode(c.m_parent) : nullptr;
        OF<I3D_frame *>(o, O_FRAME) = c.m_frame;
        OF<int>(o, O_TYPE) = c.m_type;
        OF<uint32_t>(o, O_VALUE) = c.m_value;
        OF<int>(o, O_INDEX) = c.m_index;
        OF<uint32_t>(o, O_ID) = c.m_id;
        OF<bool>(o, O_INACTIVE) = c.m_inactive;
        OF<bool>(o, O_SHOWN) = c.m_shown;
        OF<bool>(o, O_TAKES) = c.m_takesInput;
        OF<I3D_scene *>(o, O_SCENE) = c.m_scene;
        g_nodeKinds[i]->toOrig(&c, o);
    }
}
void CompareTrees()
{
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        C_Control &c = *g_portNodes[i];
        const uint8_t *o = g_origNodes[i];
        const std::string n = "c" + std::to_string(i) + " (" + g_nodeKinds[i]->name + ") ";
        CHECK(OF<uint32_t>(o, 0) == (g_freshNodes[i] ? g_nodeKinds[i]->vtable : LoggedVt(g_nodeKinds[i]->vtable)), "%svtable", n.c_str());
        CHECK(OVecBytes(o, O_CHILDREN) == c.m_children.size() * 4, "%schildren", n.c_str());
        if (OVecBytes(o, O_CHILDREN) == c.m_children.size() * 4)
            for (size_t k = 0; k < c.m_children.size(); ++k)
                CHECK(PtrName(reinterpret_cast<uint8_t **>(OVecData(o, O_CHILDREN))[k]) == PtrName(c.m_children[k]), "%schild %u", n.c_str(),
                      unsigned(k));
        CHECK(OVecBytes(o, O_ROUTES) == c.m_routes.size() * 0x18, "%sroutes %u / %u", n.c_str(), unsigned(OVecBytes(o, O_ROUTES) / 0x18),
              unsigned(c.m_routes.size()));
        if (OVecBytes(o, O_ROUTES) == c.m_routes.size() * 0x18)
            for (size_t k = 0; k < c.m_routes.size(); ++k) {
                const std::string why = MessageDiffers(c.m_routes[k], OVecData(o, O_ROUTES) + k * 0x18);
                CHECK(why.empty(), "%sroute %u: %s", n.c_str(), unsigned(k), why.c_str());
            }
        CHECK(OVecBytes(o, O_CALLBACKS) == c.m_callbacks.size() * 12, "%scallbacks", n.c_str());
        if (OVecBytes(o, O_CALLBACKS) == c.m_callbacks.size() * 12)
            for (size_t k = 0; k < c.m_callbacks.size(); ++k) {
                const uint32_t *w = reinterpret_cast<uint32_t *>(OVecData(o, O_CALLBACKS) + k * 12);
                const S_ControlCallback &cb = c.m_callbacks[k];
                CHECK(w[0] == cb.notification && w[1] == ToOrigWord(reinterpret_cast<uintptr_t>(cb.object)) &&
                          w[2] == ToOrigFn(reinterpret_cast<uintptr_t>(cb.function)),
                      "%scallback %u", n.c_str(), unsigned(k));
            }
        CHECK(OF<uint32_t>(o, O_ANY) == c.m_anyCallback.notification &&
                  OF<uint32_t>(o, O_ANY + 4) == ToOrigWord(reinterpret_cast<uintptr_t>(c.m_anyCallback.object)) &&
                  OF<uint32_t>(o, O_ANY + 8) == ToOrigFn(reinterpret_cast<uintptr_t>(c.m_anyCallback.function)),
              "%sany callback", n.c_str());
        CHECK(PtrName(OF<uint8_t *>(o, O_PARENT)) == PtrName(c.m_parent), "%sparent", n.c_str());
        CHECK(OF<I3D_frame *>(o, O_FRAME) == c.m_frame, "%sframe %s / %s", n.c_str(), PtrName(OF<I3D_frame *>(o, O_FRAME)).c_str(),
              PtrName(c.m_frame).c_str());
        CHECK(OF<int>(o, O_TYPE) == c.m_type && OF<uint32_t>(o, O_VALUE) == c.m_value && OF<int>(o, O_INDEX) == c.m_index &&
                  OF<uint32_t>(o, O_ID) == c.m_id,
              "%stype/value/index/id", n.c_str());
        CHECK(OF<bool>(o, O_INACTIVE) == c.m_inactive && OF<bool>(o, O_SHOWN) == c.m_shown && OF<bool>(o, O_TAKES) == c.m_takesInput,
              "%sflags %d%d%d / %d%d%d", n.c_str(), OF<bool>(o, O_INACTIVE), OF<bool>(o, O_SHOWN), OF<bool>(o, O_TAKES), c.m_inactive,
              c.m_shown, c.m_takesInput);
        CHECK(OF<I3D_scene *>(o, O_SCENE) == c.m_scene, "%sscene", n.c_str());
        g_differsFresh = g_freshNodes[i];
        const std::string why = g_nodeKinds[i]->differs(&c, o);
        CHECK(why.empty(), "%s%s", n.c_str(), why.c_str());
    }
}

// ---- the message queue (part 48's) ----
uint8_t *const ORIG_QUEUE = OriginalAt<uint8_t *>(0x008ae590);
std::vector<const uint8_t *> OrigQueue()
{
    std::vector<const uint8_t *> out;
    const uint32_t count = OF<uint32_t>(ORIG_QUEUE, 0x2C);
    if (count == 0)
        return out;
    uint8_t *first = OF<uint8_t *>(ORIG_QUEUE, 0x4), *next = OF<uint8_t *>(ORIG_QUEUE, 0xC);
    uint8_t **node = OF<uint8_t **>(ORIG_QUEUE, 0x10);
    size_t pos = size_t(next - first) / 0x18;
    for (uint32_t i = 0; i < count; ++i, ++pos)
        out.push_back(node[pos / 170] + (pos % 170) * 0x18);
    return out;
}
void ClearQueues()
{
    g_controlMessages.clear();
    uint8_t **map = OF<uint8_t **>(ORIG_QUEUE, 0x24);
    const uint32_t mapSize = OF<uint32_t>(ORIG_QUEUE, 0x28);
    if (map) {
        uint8_t **firstNode = OF<uint8_t **>(ORIG_QUEUE, 0x10), **lastNode = OF<uint8_t **>(ORIG_QUEUE, 0x20);
        for (uint8_t **n = firstNode; n && n <= lastNode && n < map + mapSize; ++n)
            std::free(*n);
        std::free(map);
    }
    std::memset(ORIG_QUEUE, 0, 0x30);
}
void CompareQueues()
{
    const std::vector<const uint8_t *> o = OrigQueue();
    CHECK(o.size() == g_controlMessages.size(), "queue %u / %u", unsigned(o.size()), unsigned(g_controlMessages.size()));
    if (o.size() == g_controlMessages.size())
        for (size_t i = 0; i < o.size(); ++i) {
            const std::string why = MessageDiffers(g_controlMessages[i], o[i]);
            CHECK(why.empty(), "queued %u: %s", unsigned(i), why.c_str());
        }
}

// The controls a call made (the scroll bar's arrows and slider ...): new children on both sides, paired by their place,
// adopted into the trees to be compared (their kind the original's vtable's).
void AdoptNewNodes()
{
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        C_Control &c = *g_portNodes[i];
        uint8_t *o = g_origNodes[i];
        const size_t on = OVecBytes(o, O_CHILDREN) / 4;
        for (size_t k = 0; k < c.m_children.size() && k < on; ++k) {
            C_Control *pc = c.m_children[k];
            uint8_t *oc = reinterpret_cast<uint8_t **>(OVecData(o, O_CHILDREN))[k];
            if (std::find(g_portNodes.begin(), g_portNodes.end(), pc) != g_portNodes.end())
                continue;
            const S_Kind *kind = KindByVtable(OF<uint32_t>(oc, 0));
            CHECK(kind, "a new child of c%u: the original's vtable %x unknown", unsigned(i), OF<uint32_t>(oc, 0));
            if (!kind)
                continue;
            g_portNodes.push_back(pc);
            g_origNodes.push_back(oc);
            g_nodeKinds.push_back(kind);
            g_freshNodes.push_back(true);
        }
        C_Control *po = PortOwned(i);
        uint8_t *oo = OrigOwned(i, o);
        if (po && oo && std::find(g_portNodes.begin(), g_portNodes.end(), po) == g_portNodes.end()) {
            const S_Kind *kind = KindByVtable(OF<uint32_t>(oo, 0));
            CHECK(kind, "an owned control of c%u: the original's vtable %x unknown", unsigned(i), OF<uint32_t>(oo, 0));
            if (kind) {
                g_portNodes.push_back(po);
                g_origNodes.push_back(oo);
                g_nodeKinds.push_back(kind);
                g_freshNodes.push_back(true);
            }
        }
    }
}

// ---- one call on both sides ----
std::string ArenaDiffers(const S_Arena &a, const S_Arena &b)
{
    const auto *x = reinterpret_cast<const uint8_t *>(&a), *y = reinterpret_cast<const uint8_t *>(&b);
    for (size_t i = 0; i < sizeof a; ++i)
        if (x[i] != y[i]) {
            char text[80];
            std::snprintf(text, sizeof text, "arena +%x (%s): %02x / %02x", unsigned(i),
                          ArenaName(reinterpret_cast<const uint8_t *>(&g_arena) + (i & ~size_t(0x3FF))).c_str(), y[i], x[i]);
            return text;
        }
    return "";
}
template <class PortCall, class OrigCall> void Diff(PortCall port, OrigCall orig)
{
    ToOrig();
    ClearQueues();
    g_newPortBoxes.clear();
    g_newOrigBoxes.clear();
    g_arenaStart = g_arena;
    g_log.clear();
    g_cbNext = 0;
    port();
    const std::vector<std::string> portLog = g_log;
    const S_Arena portArena = g_arena;
    g_arena = g_arenaStart;
    g_log.clear();
    g_cbNext = 0;
    orig();
    CHECK(g_log == portLog, "calls: %s / %s", Joined(g_log).c_str(), Joined(portLog).c_str());
    const std::string arena = ArenaDiffers(portArena, g_arena);
    CHECK(arena.empty(), "%s", arena.c_str());
    AdoptNewNodes();
    CompareTrees();
    CompareQueues();
}

}  // namespace

// ---- the port's side of the fakes ----
// (As the original's text control makes its box inline: the screen's size, the font manager.)
C_TextBox::C_TextBox()
{
    m_screenWidth = static_cast<int16_t>(GetIGraph()->ScrnX());
    m_screenHeight = static_cast<int16_t>(GetIGraph()->ScrnY());
    m_fonts = AcquireFontManager();
}
C_FontManager *AcquireFontManager()
{
    Log("AcquireFontManager");
    return reinterpret_cast<C_FontManager *>(g_arena.fonts);
}
C_TextBox::~C_TextBox() { BoxDelete(this); }
void C_TextBox::UpdateScrollRange() { Log("%s UpdateScrollRange", BoxName(this).c_str()); }
void C_TextBox::Render() { BoxRender(this); }
bool C_TextBox::ScrollTo(int line)
{
    Log("%s ScrollTo %d", BoxName(this).c_str(), line);
    return true;
}
bool C_TextBox::SetText(const C_str &text, uint8_t style, int index)
{
    BoxSetText(this, text, style, index);
    return true;
}
bool C_TextBox::SetTextId(int textId, int style, int index)
{
    BoxSetTextId(this, textId, style, index);
    return true;
}
bool C_TextBox::AddText(const C_str &text, uint8_t style)
{
    BoxAddText(this, text, style);
    return true;
}
bool C_TextBox::AddTextId(int textId, uint8_t style)
{
    BoxAddTextId(this, textId, style);
    return true;
}
bool C_TextBox::RemoveRun(int index)
{
    BoxRemoveRun(this, index);
    return true;
}
void C_TextBox::Clear() { BoxClear(this); }
void C_TextBox::AttachTo(I3D_scene *scene, I3D_frame *visual) { BoxAttach(this, scene, visual); }
// (Not this part's: they fail the test.)
bool C_TextBox::InsertText(const C_str &text, uint8_t style, int index) { return OB_InsertText(this, text, style, index); }
C_str C_TextBox::GetRunText(int index) const
{
    Log("%s GetRunText %d", BoxName(this).c_str(), index);
    return RunText(index);
}
bool C_TextBox::EditRun(int run) { return OB_EditRun(this, run); }
void C_TextBox::TickCaret(int time) { OB_TickCaret(this, time); }
int16_t C_TextBox::GetSelection() { return OB_GetSelection(this); }
int16_t C_TextBox::SelectRun(int run) { return OB_SelectRun(this, run); }   // (logged as 16 bits, as the original's)
C_str C_TextBox::GetSelectedText() const
{
    Log("%s GetSelectedText", BoxName(this).c_str());
    return C_str(g_box.selected);
}
bool C_TextBox::SetSelectedText(const C_str &text) { return OB_SetSelectedText(this, text); }
bool C_TextBox::SetHover(int line) { return OB_SetHover(this, line); }
bool C_TextBox::HoverAt(int x, int y) { return OB_HoverAt(this, x, y); }
bool C_TextBox::ClearHover() { return OB_ClearHover(this); }
int16_t C_TextBox::SelectHovered() { return OB_SelectHovered(this); }
int16_t C_TextBox::GetScrollRange() { return OB_GetScrollRange(this); }
S_vector4 C_TextBox::GetLineCentre(int line)
{
    Log("%s GetLineCentre %d", BoxName(this).c_str(), int16_t(line));
    return S_vector4{g_box.centreX, g_box.centreY, 0.0f, 0.0f};
}
// (The base's destructor logs the delete.)
C_PagedTextBox::~C_PagedTextBox() { m_pages.clear(); }
void C_PagedTextBox::Render() { BoxRender(this); }
void C_PagedTextBox::AddArea(I3D_scene *scene, I3D_frame *visual) { OPB_AddArea(this, scene, visual); }
void C_PagedTextBox::SetPage(int page) { OPB_SetPage(this, page); }
void C_PagedTextBox::Relayout() { OPB_Relayout(this); }
int C_PagedTextBox::LastPageBreak() const { return OPB_LastPageBreak(const_cast<C_PagedTextBox *>(this)); }

C_Cursor *C_Cursor::Acquire(I3D_scene *scene) { return static_cast<C_Cursor *>(OC_Acquire(scene)); }
void C_Cursor::Release() { OC_Release(this); }
bool C_Cursor::IsButtonDown(uint32_t buttons) const { return OC_IsButtonDown(const_cast<C_Cursor *>(this), buttons); }
void C_Cursor::GetPosition(S_vector *position) const { OC_GetPosition(const_cast<C_Cursor *>(this), position); }
// (The original's reads +0x40 inline: no log.)
int C_Cursor::GetStillTime() const { return *reinterpret_cast<const int *>(reinterpret_cast<const uint8_t *>(this) + 0x40); }
void C_Cursor::SetDelay(int) { Unexpected("C_Cursor::SetDelay"); }
void C_Cursor::GetMove(S_vector *move) const { OC_GetMove(const_cast<C_Cursor *>(this), move); }
void C_Cursor::GetScreenPosition(float *x, float *y, float *buttons) const { OC_GetScreen(const_cast<C_Cursor *>(this), x, y, buttons); }

// (The original's own on both sides: text_box.cpp's is a later part's.)
int EncodeUtf8(char *out, const uint32_t *codePoint, int maxBytes)
{
    return OriginalAt<int(__attribute__((fastcall)) *)(char *, const uint32_t *, int)>(0x0064d910)(out, codePoint, maxBytes);
}
bool IsLegalUtf8Range(const uint8_t *, const uint8_t *) { Unexpected("IsLegalUtf8Range"); return false; }
bool HasUtf8Sequences(const char *) { Unexpected("HasUtf8Sequences"); return false; }
// The item table (a part's: g_itemDef set; null: none faked).
static C_ItemDef *(*g_itemDef)(int typeId) = nullptr;
C_ItemDef *GetItemDef(int typeId)
{
    if (g_itemDef)
        return g_itemDef(typeId);
    Unexpected("GetItemDef");
    return nullptr;
}
int GetGameTypeTextId(int) { Unexpected("GetGameTypeTextId"); return 0; }
const C_Controls::S_Press *C_Controls::PollPress(int, bool) { Unexpected("C_Controls::PollPress"); return nullptr; }
void __stdcall I3D_visual::UpdateWBoundProc() { Unexpected("I3D_visual::UpdateWBoundProc"); }
void I3D_frame::UpdateWMatrixProc() { O_UpdateWMatrix(this); }
void __stdcall S_quat::Make(const S_matrix &) { Unexpected("S_quat::Make"); }
bool C_KeyBindings::Bind(int, int, int, int) { Unexpected("C_KeyBindings::Bind"); return false; }
C_str C_KeyBindings::GetBindingName(int) const { Unexpected("C_KeyBindings::GetBindingName"); return C_str(); }
// (Its loaded picture or none without a name, as item_defs.cpp's; loading it is item_defs.cpp's part.)
IImage *C_ItemDef::GetImage()
{
    if (m_image || !m_imageName)
        return m_image;
    Unexpected("C_ItemDef::GetImage");
    return nullptr;
}
S_NetPlayerState *C_NetGame::LockPlayers() { Unexpected("C_NetGame::LockPlayers"); return nullptr; }
void C_NetGame::UnlockPlayers() { Unexpected("C_NetGame::UnlockPlayers"); }
C_str C_GameTexts::Get(int id) const
{
    Log("texts Get %d", id);
    return TextFor(id);
}
extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(g_arena.graph); }
extern "C" I3D_driver *I3DGetDriver() { Unexpected("I3DGetDriver"); return nullptr; }
// The factory (control_factory.cpp is a later part's): faked.
bool C_Control::CreateChild(I3D_frame *frame, I3D_frame *, const S_ControlDesc *desc)
{
    Log("CreateChild %s %s %p", Name(this).c_str(), ArenaName(frame).c_str(), static_cast<const void *>(desc));
    return true;
}

namespace {
void *__stdcall O_GetIGraph() { return g_arena.graph; }
void **__cdecl O_Fstr(void **dest, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    C_str s(text);
    *dest = s.m_rep;
    s.m_rep = nullptr;
    return dest;
}
void *__cdecl O_AcquireFonts() { return AcquireFontManager(); }
bool __attribute__((thiscall)) O_CreateChild(void *self, void *frame, void *, void *desc)
{
    Log("CreateChild %s %s %p", Name(self).c_str(), ArenaName(frame).c_str(), desc);
    return true;
}

void SetUpKit()
{
    SetUpArena();
    g_origBoxVt[0] = reinterpret_cast<void *>(OB_UpdateScrollRange);
    g_origBoxVt[1] = reinterpret_cast<void *>(OB_Render);
    g_origBoxVt[2] = reinterpret_cast<void *>(OB_ScrollTo);
    g_origBoxVt[3] = reinterpret_cast<void *>(OB_Delete);
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x007e566f, reinterpret_cast<const void *>(O_New)},
        {0x007e2120, reinterpret_cast<const void *>(O_Delete)},
        {0x007e530e, reinterpret_cast<const void *>(O_GetIGraph)},
        {0x0065d470, reinterpret_cast<const void *>(O_CreateChild)},
        {0x00496cc0, reinterpret_cast<const void *>(OB_Ctor)},
        {0x0063df00, reinterpret_cast<const void *>(O_AcquireFonts)},
        // (The box's vtable 0x00810458: UpdateScrollRange, Render, ScrollTo, the deleting destructor.)
        {0x006441a0, reinterpret_cast<const void *>(OB_UpdateScrollRange)},
        {0x00648200, reinterpret_cast<const void *>(OB_Render)},
        {0x00644250, reinterpret_cast<const void *>(OB_ScrollTo)},
        {0x0048ecd0, reinterpret_cast<const void *>(OB_Delete)},
        {0x006446e0, reinterpret_cast<const void *>(OB_SetText)},
        {0x00644520, reinterpret_cast<const void *>(OB_SetTextId)},
        {0x00644830, reinterpret_cast<const void *>(OB_AddText)},
        {0x00644770, reinterpret_cast<const void *>(OB_AddTextId)},
        {0x00644bb0, reinterpret_cast<const void *>(OB_RemoveRun)},
        {0x00644c80, reinterpret_cast<const void *>(OB_Clear)},
        {0x00644da0, reinterpret_cast<const void *>(OB_AttachTo)},
        {0x0066a9e0, reinterpret_cast<const void *>(OC_Acquire)},
        {0x0066aa80, reinterpret_cast<const void *>(OC_Release)},
        {0x0066a3a0, reinterpret_cast<const void *>(OC_IsButtonDown)},
        {0x0066a420, reinterpret_cast<const void *>(OC_GetPosition)},
        {0x0066a450, reinterpret_cast<const void *>(OC_GetMove)},
        {0x007e5296, reinterpret_cast<const void *>(O_UpdateWMatrix)},
        {0x00644090, reinterpret_cast<const void *>(OB_SetHover)},
        {0x00644120, reinterpret_cast<const void *>(OB_ClearHover)},
        // The paged box (its vtable 0x008154a4: Render, the deleting destructor) and its methods.
        {0x0064ae70, reinterpret_cast<const void *>(OB_Render)},
        {0x006622f0, reinterpret_cast<const void *>(OB_Delete)},
        {0x0064b200, reinterpret_cast<const void *>(OPB_AddArea)},
        {0x0064bb30, reinterpret_cast<const void *>(OPB_SetPage)},
        {0x0064bb80, reinterpret_cast<const void *>(OPB_Relayout)},
        {0x0064bb90, reinterpret_cast<const void *>(OPB_LastPageBreak)},
        {0x006e5380, reinterpret_cast<const void *>(OG_Get)},
        {0x00405100, reinterpret_cast<const void *>(O_Fstr)},
        {0x00643c20, reinterpret_cast<const void *>(OB_EditRun)},
        {0x00644280, reinterpret_cast<const void *>(OB_TickCaret)},
        {0x00643f10, reinterpret_cast<const void *>(OB_SelectHovered)},
        {0x00643fb0, reinterpret_cast<const void *>(OB_GetSelection)},
        {0x00643f50, reinterpret_cast<const void *>(OB_SelectRun)},
        {0x00644230, reinterpret_cast<const void *>(OB_GetScrollRange)},
        {0x006440e0, reinterpret_cast<const void *>(OB_HoverAt)},
        {0x00644b20, reinterpret_cast<const void *>(OB_InsertText)},
        {0x00644320, reinterpret_cast<const void *>(OB_SetSelectedText)},
        {0x006443a0, reinterpret_cast<const void *>(OB_GetSelectedText)},
        {0x00643dd0, reinterpret_cast<const void *>(OB_GetRunText)},
        {0x00643ca0, reinterpret_cast<const void *>(OB_GetLineCentre)},
        {0x0066a480, reinterpret_cast<const void *>(OC_GetScreen)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    std::memset(ORIG_QUEUE, 0, 0x30);
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    std::printf("controls: exception %08lx at %p (linked %08x) [%s round %d]\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, unsigned(at - base + 0x20000000), g_case, g_round);
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW) {
        const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        char text[160];
        const int n = std::snprintf(text, sizeof text, "first chance %08lx at %08x (linked %08x) [%s round %d]\n", code, unsigned(at),
                                    unsigned(at - base + 0x20000000), g_case, g_round);
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, DWORD(n), &written, nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace
