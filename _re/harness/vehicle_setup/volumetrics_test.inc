// Part 8d-iv's volumetrics and clouds (game/volumetrics.cpp) (WORLD_TEST), after decorators2_test.inc
// (its fakes). volumetrics.bin is made up in g_files; IGraph's textures, render states, world
// matrix and draws and the driver's states are logging fakes (the draws keep their vertices).
// Prints the "volumetrics:" line.

namespace {

std::vector<std::vector<S_LitVertex>> g_vlDraws;        // each draw's vertices
std::vector<int> g_vlDrawCounts;
S_matrix g_vlWorld;
int g_vlRender0D, g_vlRender0E, g_vlState3;
struct S_VlImage {
    void **vtable;
    std::string name;
};
void *g_vlImageVt[SLOTS];
std::vector<S_VlImage *> g_vlImages;
uint32_t __stdcall F_VlImageRelease(S_VlImage *self)
{
    GfLog("imgrelease %s", self->name.c_str());
    return 0;
}
HRESULT __stdcall F_VlLoadTexture(void *, const char *name, const char *alpha, uint32_t mode, IImage **out)
{
    GfLog("loadtex %s %s %x", name, alpha ? alpha : "-", mode);
    if (std::strstr(name, "missing"))
        return 1;
    auto *i = new S_VlImage{g_vlImageVt, name};
    g_vlImages.push_back(i);
    *out = reinterpret_cast<IImage *>(i);
    return 0;
}
void __stdcall F_VlSetTexture(void *, void *texture, int stage)
{
    GfLog("settex %s %d", texture ? reinterpret_cast<S_VlImage *>(texture)->name.c_str() : "-", stage);
}
void __stdcall F_VlSetRenderState(void *, int state, int value) { GfLog("rset %x %d", state, value); }
int __stdcall F_VlGetRenderState(void *, int state)
{
    GfLog("rget %x", state);
    return state == 0xD ? g_vlRender0D : state == 0xE ? g_vlRender0E : 0;
}
void __stdcall F_VlSetWorld(void *, const S_matrix &m)
{
    GfLog("world");
    g_vlWorld = m;
}
void __stdcall F_VlDraw(void *, int kind, int count, const void *vertices, int transformed)
{
    GfLog("draw %d %d %d", kind, count, transformed);
    const auto *v = static_cast<const S_LitVertex *>(vertices);
    g_vlDraws.emplace_back(v, v + count * 3);
    g_vlDrawCounts.push_back(count);
}
void __stdcall F_VlSetState(void *, int state, uint32_t value) { GfLog("dset %d %u", state, value); }
uint32_t __stdcall F_VlGetState(void *, int state)
{
    GfLog("dget %d", state);
    return state == 3 ? static_cast<uint32_t>(g_vlState3) : 0;
}

std::string VlI(int v) { return std::string(reinterpret_cast<const char *>(&v), 4); }
std::string VlF(float v) { return std::string(reinterpret_cast<const char *>(&v), 4); }
std::string VlChunk(uint16_t id, const std::string &payload)
{
    std::string out(6, '\0');
    const uint32_t size = static_cast<uint32_t>(6 + payload.size());
    std::memcpy(&out[0], &id, 2);
    std::memcpy(&out[2], &size, 4);
    return out + payload;
}
std::string VlName(uint16_t id, const std::string &name) { return VlChunk(id, name + std::string(1, '\0')); }

uint32_t VlColor(float alpha255) { return static_cast<uint32_t>(static_cast<int>(std::nearbyint(alpha255))) << 24 | 0xFFFFFFu; }
bool VlNear(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

struct S_VlResults {
    bool vlCone = true, vlConeLoad = true, vlBuild = true, vlBuildCone = true, vlTexture = true, vlRender = true;
    bool vlRenderCone = true, vlShaft = true, vlShaftLoad = true, vlRefresh = true, vlDraw = true, vlFade = true;
    bool vlList = true, vlPrune = true, vlListRender = true, vlSort = true, vlClouds = true, vlCloudDraw = true;
    bool vlModule = true;
};

}  // namespace

// (LS3DF's: SetDir3, the translation cleared.)
void S_matrix::SetDir(const S_vector &dir, const S_vector &up)
{
    SetDir3(dir, up);
    m[3][0] = m[3][1] = m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

void VolumetricsTest()
{
    S_VlResults r;
    static bool once;
    if (!once) {
        once = true;
        for (int i = 0; i < SLOTS; ++i)
            g_vlImageVt[i] = g_frameVtable[0x10 / 4];
        g_vlImageVt[0] = reinterpret_cast<void *>(&F_VlImageRelease);
    }
    void *igraphSlots[SLOTS], *driverSlots[SLOTS];
    std::memcpy(igraphSlots, g_igraphVtable, sizeof igraphSlots);
    std::memcpy(driverSlots, g_driverVtable, sizeof driverSlots);
    g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_VlLoadTexture);
    g_igraphVtable[0x44 / 4] = reinterpret_cast<void *>(&F_VlSetTexture);
    g_igraphVtable[0x74 / 4] = reinterpret_cast<void *>(&F_VlSetRenderState);
    g_igraphVtable[0x78 / 4] = reinterpret_cast<void *>(&F_VlGetRenderState);
    g_igraphVtable[0x84 / 4] = reinterpret_cast<void *>(&F_VlSetWorld);
    g_igraphVtable[0x88 / 4] = reinterpret_cast<void *>(&F_VlDraw);
    g_driverVtable[0x18 / 4] = reinterpret_cast<void *>(&F_VlSetState);
    g_driverVtable[0x1C / 4] = reinterpret_cast<void *>(&F_VlGetState);
    void *sceneSlots[SCENE_SLOTS];
    std::memcpy(sceneSlots, g_sceneVtable, sizeof sceneSlots);
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_GfSceneFind);
    uint8_t *scene = reinterpret_cast<uint8_t *>(GetGame()->GetScene());
    I3D_frame *cam = GfFrame(FRAME_CAMERA, "vcam");
    WmField<I3D_frame *>(scene, 0x15C) = cam;
    GetGame()->m_missionName = "GFX";
    const char *const VL_FILE = "missions\\GFX\\volumetrics.bin";
    g_files.erase(VL_FILE);
    g_gfScene.clear();

    // --- a cone: made, loaded ---
    {
        S_VolumetricCone c(2.5f);
        WM(r.vlCone, std::string(c.texture.c_str()).empty() && c.angle == 0.7853982f && c.length == 2.0f && c.alphaNear == 0.5f &&
                         c.alphaFar == 0.5f && c.base == 2.5f && !c.flat && !c.image && c.rotate == 0.1f && c.segments == 3 &&
                         c.spread == 3.1415927f && c.scrollU == 0.0f && c.scrollV == 0.0f && c.uvScale == 1.0f);
        const std::string body = VlName(0x3F2, "beam") + VlChunk(0x3FC, VlF(30.0f)) + VlChunk(0x406, VlF(4.0f)) +
                                 VlChunk(0x410, VlF(0.8f)) + VlChunk(0x41A, VlF(0.2f)) + VlChunk(0x424, VlI(5)) +
                                 VlChunk(0x42E, VlF(0.3f)) + VlChunk(0x438, VlI(4)) + VlChunk(0x442, VlF(90.0f)) +
                                 VlChunk(0x44C, VlF(0.5f)) + VlChunk(0x456, VlF(-0.25f)) + VlChunk(0x460, VlF(2.0f)) +
                                 VlChunk(0x999, VlF(7.0f));
        const std::string file = VlChunk(0x28, body);
        g_files[VL_FILE].assign(file.begin(), file.end());
        C_chunk chunk;
        chunk.Open(VL_FILE, false);
        WM(r.vlConeLoad, c.Load(chunk) && std::string(c.texture.c_str()) == "beam" && c.angle == 30.0f && c.length == 4.0f &&
                             c.alphaNear == 0.8f && c.alphaFar == 0.2f && c.flat && c.rotate == 0.3f && c.segments == 4 &&
                             c.spread == 90.0f && c.scrollU == 0.5f && c.scrollV == -0.25f && c.uvScale == 2.0f);
        // A flag 0: not flat.
        const std::string f2 = VlChunk(0x28, VlChunk(0x424, VlI(0)));
        g_files[VL_FILE].assign(f2.begin(), f2.end());
        C_chunk chunk2;
        chunk2.Open(VL_FILE, false);
        WM(r.vlConeLoad, c.Load(chunk2) && !c.flat && c.length == 4.0f);
        g_files.erase(VL_FILE);
    }

    // --- built: the flat fan, the cone's segments ---
    {
        S_VolumetricCone c(0.0f);
        c.flat = true;
        c.angle = 60.0f;
        c.length = 3.0f;
        c.alphaNear = 0.6f;
        c.alphaFar = 0.3f;
        c.uvScale = 2.0f;
        g_gfLog.clear();
        c.Build(false, 1.5f);
        const float half = static_cast<float>(std::tan(60.0f * 0.0174533f * 0.5f)) * 3.0f;
        const float u1 = 0.5f - half / 3.0f * 2.0f, u2 = half / 3.0f * 2.0f + 0.5f;
        WM(r.vlBuild, g_gfLog.empty() && c.fan[0].pos == S_vector(0, 0, 1.5f) && c.fan[0].color == VlColor(0.6f * 255.0f) &&
                          c.fan[0].u == 0.5f && c.fan[0].v == 0.0f && c.fan[1].pos == S_vector(half, 0, 3.0f + 1.5f) &&
                          c.fan[1].color == VlColor(0.3f * 255.0f) && c.fan[1].u == u1 && c.fan[1].v == 2.0f &&
                          c.fan[2].pos == S_vector(-half, 0, 3.0f + 1.5f) && c.fan[2].u == u2 && c.fan[2].v == 2.0f &&
                          c.fanUV[0][0] == 0.5f && c.fanUV[0][1] == 0.0f && c.fanUV[1][0] == u1 && c.fanUV[1][1] == 2.0f &&
                          c.fanUV[2][0] == u2 && c.fanUV[2][1] == 2.0f && c.alphas[0][0] == 0.6f && c.alphas[0][1] == 0.3f &&
                          c.alphas[0][2] == 0.3f);
        // A cone of 3 segments over 90 degrees: the ends' outer rims clear.
        c.flat = false;
        c.angle = 90.0f;
        c.segments = 3;
        c.Build(false, 0.5f);
        const float rad = 90.0f * 0.0174533f;
        float a = -0.5f * rad + 1.5708f;
        const float step = rad / 3.0f;
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            const float next = step + a;
            ok = ok && c.cone[i][0].pos == S_vector(0, 0, 0.5f) && c.cone[i][0].u == 0.5f && c.cone[i][0].v == 0.5f &&
                 c.cone[i][0].color == VlColor(0.6f * 255.0f) &&
                 c.cone[i][1].pos == S_vector(std::cos(a) * 3.0f, 0, std::sin(a) * 3.0f + 0.5f) &&
                 c.cone[i][2].pos == S_vector(std::cos(next) * 3.0f, 0, std::sin(next) * 3.0f + 0.5f) &&
                 c.alphas[i][0] == 0.6f && c.alphas[i][1] == (i == 0 ? 0.0f : 0.3f) && c.alphas[i][2] == (i == 2 ? 0.0f : 0.3f) &&
                 c.cone[i][1].color == VlColor(c.alphas[i][1] * 255.0f) && c.cone[i][2].color == VlColor(c.alphas[i][2] * 255.0f);
            a = next;
        }
        WM(r.vlBuildCone, ok);
        // One segment: both rims kept.
        c.segments = 1;
        c.Build(false, 0.0f);
        WM(r.vlBuildCone, c.alphas[0][1] == 0.3f && c.alphas[0][2] == 0.3f);
        // The texture: reloaded with .bmp when it has no extension; none without a name.
        c.texture = "shaft";
        g_gfLog.clear();
        c.Build(true, 0.0f);
        WM(r.vlTexture, GfLogIs({"loadtex shaft.bmp shaft.bmp 404"}) && c.image);
        c.texture = "shaft.tga";
        g_gfLog.clear();
        c.Build(true, 0.0f);
        WM(r.vlTexture, GfLogIs({"imgrelease shaft.bmp", "loadtex shaft.tga shaft.tga 404"}) && c.image);
        c.texture = "";
        g_gfLog.clear();
        c.Build(true, 0.0f);
        WM(r.vlTexture, GfLogIs({"imgrelease shaft.tga"}) && !c.image);
        c.texture = "missing";
        c.Build(true, 0.0f);
        WM(r.vlTexture, !c.image);
        c.Build(false, 0.0f);
        WM(r.vlTexture, !c.image);
    }

    // --- rendered: the fan's texture scrolled, the cone's turned; the fade's alpha ---
    {
        S_VolumetricCone c(0.25f);
        c.flat = true;
        c.angle = 40.0f;
        c.scrollU = 0.5f;
        c.scrollV = -2.0f;
        c.Build(false, 0.0f);
        g_gfLog.clear();
        g_vlDraws.clear();
        c.Render(100, 0.0f, 0.5f);
        WM(r.vlRender, g_gfLog.empty() && g_vlDraws.empty());
        c.image = reinterpret_cast<IImage *>(new S_VlImage{g_vlImageVt, "fanimg"});
        c.Render(100, 0.0f, 0.5f);
        const int t = static_cast<int>(0.25f * 1000.0f + 100.0f);
        const float du = static_cast<float>(t) * 0.5f * 0.001f, dv = static_cast<float>(t) * -2.0f * 0.001f;
        WM(r.vlRender, GfLogIs({"settex fanimg 0", "draw 3 1 0"}) && g_vlDraws.size() == 1 && g_vlDraws[0].size() == 3 &&
                           g_vlDraws[0][1].u == du + c.fanUV[1][0] && g_vlDraws[0][1].v == dv + c.fanUV[1][1] &&
                           g_vlDraws[0][0].u == du + 0.5f && g_vlDraws[0][2].v == dv + c.fanUV[2][1] &&
                           g_vlDraws[0][0].color == VlColor(0.5f * 0.5f * 255.0f) &&
                           g_vlDraws[0][2].color == VlColor(0.5f * 0.5f * 255.0f));
        // A fade under 0: the colours kept.
        c.fan[0].color = 0x12345678;
        c.Render(100, 0.0f, -1.0f);
        WM(r.vlRender, c.fan[0].color == 0x12345678);
        c.Render(100, 0.0f, 0.0f);
        WM(r.vlRender, c.fan[0].color == 0x00FFFFFFu);
        // The cone: its rims' coordinates turned by (phase + base) x rotate, a step a segment.
        c.flat = false;
        c.segments = 2;
        c.spread = 60.0f;
        c.rotate = 0.5f;
        c.alphas[0][0] = 1.0f;
        c.alphas[0][1] = 0.5f;
        c.alphas[1][2] = 0.25f;
        g_vlDraws.clear();
        g_gfLog.clear();
        c.Render(0, 2.0f, 0.8f);
        float a = (2.0f + 0.25f) * 0.5f;
        const float step = 60.0f * 0.0174533f / 2.0f;
        bool ok = g_vlDrawCounts.back() == 2 && g_vlDraws.size() == 1 && g_vlDraws[0].size() == 6 &&
                  GfLogIs({"settex fanimg 0", "draw 3 2 0"});
        for (int i = 0; i < 2 && ok; ++i) {
            const S_LitVertex *v = &g_vlDraws[0][i * 3];
            const float next = step + a;
            ok = v[1].u == (std::cos(a) + 1.0f) * 0.5f && v[1].v == (std::sin(a) + 1.0f) * 0.5f &&
                 v[2].u == (std::cos(next) + 1.0f) * 0.5f && v[2].v == (std::sin(next) + 1.0f) * 0.5f &&
                 v[0].color == VlColor(0.8f * c.alphas[i][0] * 255.0f) && v[1].color == VlColor(0.8f * c.alphas[i][1] * 255.0f) &&
                 v[2].color == VlColor(0.8f * c.alphas[i][2] * 255.0f);
            a = next;
        }
        WM(r.vlRenderCone, ok);
        c.cone[0][0].color = 7;
        c.Render(0, 2.0f, -0.5f);
        WM(r.vlRenderCone, c.cone[0][0].color == 7);
    }

    // --- a shaft: made, loaded (its light, distances, cones), refreshed ---
    I3D_frame *sector = GfFrame(FRAME_SECTOR, "vsector");
    I3D_frame *light = GfFrame(FRAME_LIGHT, "lamp", sector);
    light->m_frameFlags |= 1u;
    GfPlace(light, S_vector(3.0f, 4.0f, 0.0f));
    g_gfScene = {{"lamp", light}};
    {
        std::srand(5);
        const float rf = RandFloat();
        std::srand(5);
        S_Volumetric v(nullptr);
        WM(r.vlShaft, !v.frame && v.cones.empty() && v.distance == 0.0f && v.depthDistance == 10.0f && v.fadeInEnd == 0.0f &&
                          v.fadeInStart == 0.0f && !v.visible && v.distanceTurn == 0.0f && v.viewTurn == 0.0f &&
                          v.fadeOutEnd == 30.0f && v.fadeOutStart == 25.0f && v.base == 0.0f && v.viewDir.x == 0.0f &&
                          v.viewDir.y == 0.0f && v.viewAngle == rf * 6.2831855f);
        const std::string cone1 = VlChunk(0x30, VlName(0x3F2, "c1") + VlChunk(0x406, VlF(5.0f)));
        const std::string cone2 = VlChunk(0x30, VlChunk(0x406, VlF(6.0f)));
        const std::string body = VlName(10, "lamp") + VlChunk(0x14, VlF(12.0f)) + VlChunk(0x19, VlF(4.0f)) +
                                 VlChunk(0x1C, VlF(2.0f)) + VlChunk(0x1E, VlF(0.25f)) + VlChunk(0x23, VlF(0.75f)) +
                                 VlChunk(0x26, VlF(1.5f)) + VlChunk(0x28, cone1 + cone2) + VlChunk(0x32, VlF(40.0f)) +
                                 VlChunk(0x3C, VlF(50.0f)) + VlChunk(0x77, VlF(1.0f));
        const std::string file = VlChunk(5, body);
        g_files[VL_FILE].assign(file.begin(), file.end());
        C_chunk chunk;
        chunk.Open(VL_FILE, false);
        g_gfLog.clear();
        WM(r.vlShaftLoad, v.Load(chunk) && v.frame == light && v.depthDistance == 12.0f && v.fadeInEnd == 4.0f &&
                              v.fadeInStart == 2.0f && v.distanceTurn == 0.25f && v.viewTurn == 0.75f && v.base == 1.5f &&
                              v.fadeOutStart == 40.0f && v.fadeOutEnd == 50.0f && v.cones.size() == 2 &&
                              std::string(v.cones[0]->texture.c_str()) == "c1" && v.cones[0]->length == 5.0f &&
                              v.cones[1]->length == 6.0f && v.cones[0]->base == 0.0f + 5.0f && v.cones[1]->base == 1.0f + 5.0f);
        // Loaded again: the old cones gone; a light not found ends the load (true all the same).
        const std::string f2 = VlChunk(5, VlChunk(0x14, VlF(3.0f)) + VlName(10, "nolight") + VlChunk(0x32, VlF(9.0f)));
        g_files[VL_FILE].assign(f2.begin(), f2.end());
        C_chunk chunk2;
        chunk2.Open(VL_FILE, false);
        WM(r.vlShaftLoad, v.Load(chunk2) && !v.frame && v.cones.empty() && v.depthDistance == 3.0f && v.fadeOutStart == 40.0f);
        g_files.erase(VL_FILE);
        // Refreshed: the light's distance from the origin; the cones built (with their textures
        // only when named).
        v.frame = light;
        v.base = 2.0f;
        v.cones.push_back(new S_VolumetricCone(0.0f));
        v.cones.push_back(new S_VolumetricCone(0.0f));
        v.cones[0]->texture = "named";
        g_gfLog.clear();
        v.Refresh(true);
        WM(r.vlRefresh, v.viewAngle == 5.0f && GfLogIs({"loadtex named.bmp named.bmp 404"}) && v.cones[0]->cone[0][0].pos.z == 2.0f &&
                            v.cones[1]->cone[0][0].pos.z == 0.0f);
        g_gfLog.clear();
        v.Refresh(false);
        WM(r.vlRefresh, g_gfLog.empty() && v.cones[1]->cone[0][0].pos.z == 2.0f && v.cones[0]->image);
    }

    // --- drawn: turned to the camera, the render state by the distance, the fades ---
    {
        S_Volumetric v(light);
        auto *c = new S_VolumetricCone(0.0f);
        c->image = reinterpret_cast<IImage *>(new S_VlImage{g_vlImageVt, "dimg"});
        c->segments = 1;
        c->Build(false, 0.0f);
        c->alphas[0][0] = 1.0f;
        v.cones.push_back(c);
        v.viewAngle = 2.0f;
        v.viewTurn = 0.5f;
        v.distanceTurn = 0.1f;
        auto drawAt = [&](float distance, float *fadeOut) {
            v.distance = distance;
            g_vlDraws.clear();
            g_gfLog.clear();
            c->cone[0][0].color = 0x01020304;
            const bool drawn = v.Draw(50, S_vector(3.0f, 4.0f, -10.0f), 0.3f);
            *fadeOut = c->cone[0][0].color == 0x01020304 ? -1.0f
                                                         : static_cast<float>(c->cone[0][0].color >> 24) / 255.0f;
            return drawn;
        };
        float f = 0.0f;
        WM(r.vlDraw, drawAt(20.0f, &f) && f == -1.0f && GfHas({"world", "rset d 1", "settex dimg 0", "draw 3 1 0"}));
        // The phase: (angle x view turn + distance turn x distance) + yaw.
        {
            const float phase = 2.0f * 0.5f + 0.1f * 20.0f + 0.3f;
            const float a = (phase + 0.0f) * c->rotate;
            WM(r.vlDraw, g_vlDraws.size() == 1 && g_vlDraws[0][1].u == (std::cos(a) + 1.0f) * 0.5f);
        }
        // Its z axis kept, turned to face the camera; w cleared.
        WM(r.vlDraw, VlNear(g_vlWorld.m[2][2], 1.0f) && g_vlWorld.m[0][3] == 0.0f && g_vlWorld.m[3][1] == 4.0f);
        // Within the depth distance: state 0xD off; off: not drawn.
        WM(r.vlDraw, drawAt(10.0f, &f) && GfHas({"rset d 0"}));
        light->m_frameFlags &= ~1u;
        WM(r.vlDraw, !drawAt(20.0f, &f) && g_vlDraws.empty());
        light->m_frameFlags |= 1u;
        S_Volumetric none(nullptr);
        WM(r.vlDraw, !none.Draw(0, S_vector(0, 0, 0), 0.0f));
        // Fading out from 25 to 30; beyond: none.
        WM(r.vlFade, drawAt(27.5f, &f) && VlNear(f, 0.5f, 0.004f) && !drawAt(30.0f, &f) && drawAt(25.0f, &f) && f == -1.0f);
        // Fading in from 2 to 4 (when set): full beyond 4, none within 2.
        v.fadeInEnd = 4.0f;
        v.fadeInStart = 2.0f;
        WM(r.vlFade, drawAt(3.0f, &f) && VlNear(f, 0.5f, 0.004f) && drawAt(4.0f, &f) && f == 1.0f && !drawAt(2.0f, &f) &&
                         drawAt(10.0f, &f) && f == 1.0f);
        // A fade of exactly 0 (the start at the end): none.
        v.fadeInStart = 3.0f;
        v.fadeInEnd = 3.0f + 1e-6f;
        WM(r.vlFade, !drawAt(3.0f, &f));
        light->m_frameFlags |= 1u;
    }

    // --- the list: loaded (version 1), pruned, freed ---
    C_Volumetrics &vols = g_geffectsModule.m_volumetrics;
    {
        C_Volumetrics list;
        const std::string shaft1 = VlChunk(5, VlName(10, "lamp") + VlChunk(0x28, VlChunk(0x30, VlName(0x3F2, "t1"))));
        const std::string shaft2 = VlChunk(5, VlName(10, "gone"));
        std::string file = VlChunk(1, shaft1 + shaft2);
        g_files[VL_FILE].assign(file.begin(), file.end());
        g_gfLog.clear();
        list.Load();
        WM(r.vlList, list.m_shafts.size() == 1 && list.m_shafts[0]->frame == light && list.m_shafts[0]->cones.size() == 1 &&
                         list.m_shafts[0]->cones[0]->image && GfHas({"loadtex t1.bmp t1.bmp 404"}) && list.m_viewDir.x == 1.0f);
        // Another version: none; no file: none (freed first).
        file = VlChunk(2, shaft1);
        g_files[VL_FILE].assign(file.begin(), file.end());
        list.Load();
        WM(r.vlList, list.m_shafts.empty());
        list.m_shafts.push_back(new S_Volumetric(light));
        g_files.erase(VL_FILE);
        list.Load();
        WM(r.vlList, list.m_shafts.empty());
        // Pruned: those without their light deleted, the others refreshed (in order).
        auto *a = new S_Volumetric(light), *b = new S_Volumetric(nullptr), *c = new S_Volumetric(nullptr), *d = new S_Volumetric(light);
        a->cones.push_back(new S_VolumetricCone(0.0f));
        a->cones[0]->texture = "pa";
        d->cones.push_back(new S_VolumetricCone(0.0f));
        d->cones[0]->texture = "pd";
        list.m_shafts = {a, b, c, d};
        g_gfLog.clear();
        list.Prune(true);
        WM(r.vlPrune, list.m_shafts == std::vector<S_Volumetric *>({a, d}) &&
                          GfLogIs({"loadtex pa.bmp pa.bmp 404", "loadtex pd.bmp pd.bmp 404"}) && a->viewAngle == 5.0f);
        g_gfLog.clear();
        list.Prune(false);
        WM(r.vlPrune, list.m_shafts.size() == 2 && g_gfLog.empty());
        // Freed: the clouds too; the camera's direction back to (1, 0).
        list.m_yaw = 3.0f;
        list.m_unknown14 = 2.0f;
        list.m_viewDir = {0.0f, 1.0f};
        g_clouds.images[0] = reinterpret_cast<IImage *>(new S_VlImage{g_vlImageVt, "cl0"});
        g_cloudVertices[1] = new S_LitVertex[6];
        g_gfLog.clear();
        list.Free();
        WM(r.vlList, list.m_shafts.empty() && list.m_yaw == 0.0f && list.m_unknown14 == 0.0f && list.m_viewDir.x == 1.0f &&
                         list.m_viewDir.y == 0.0f && GfHas({"imgrelease cl0", "imgrelease pa.bmp"}) && !g_clouds.images[0] &&
                         !g_cloudVertices[1]);
    }

    // --- rendered: the states kept, the camera's turn, the visible shafts (sorted) drawn ---
    {
        C_Volumetrics list;
        GfPlace(cam, S_vector(0.0f, 0.0f, 0.0f));
        WmField<uint32_t>(sector, 0x160) = 1;
        I3D_frame *l2 = GfFrame(FRAME_LIGHT, "lamp2", sector), *l3 = GfFrame(FRAME_LIGHT, "lamp3", sector);
        I3D_frame *l4 = GfFrame(FRAME_LIGHT, "lamp4", sector), *orphan = GfFrame(FRAME_LIGHT, "orphan");
        for (I3D_frame *f : {light, l2, l3, l4, orphan})
            f->m_frameFlags |= 1u;
        GfPlace(light, S_vector(0.0f, 0.0f, 5.0f));
        GfPlace(l2, S_vector(1.0f, 0.0f, 12.0f));
        GfPlace(l3, S_vector(0.0f, 0.0f, -5.0f));                 // behind
        GfPlace(l4, S_vector(0.0f, 0.0f, 40.0f));                 // beyond its fade
        auto shaft = [&](I3D_frame *f, const std::string &img) {
            auto *s = new S_Volumetric(f);
            auto *c = new S_VolumetricCone(0.0f);
            c->image = reinterpret_cast<IImage *>(new S_VlImage{g_vlImageVt, img});
            c->segments = 1;
            s->cones.push_back(c);
            s->viewAngle = 0.0f;
            s->viewDir = {0.0f, 1.0f};
            return s;
        };
        S_Volumetric *s1 = shaft(light, "i1"), *s2 = shaft(l2, "i2"), *s3 = shaft(l3, "i3"), *s4 = shaft(l4, "i4"),
                     *s5 = shaft(orphan, "i5");
        list.m_shafts = {s1, s5, s2, s3, s4};
        list.m_viewDir = {0.0f, 1.0f};
        g_vlState3 = 1;
        g_vlRender0E = 1;
        g_vlRender0D = 0;
        g_gfLog.clear();
        list.Render(77);
        std::vector<std::string> textures;
        for (const std::string &l : g_gfLog)
            if (l.compare(0, 7, "settex ") == 0)
                textures.push_back(l);
        WM(r.vlListRender, list.m_shafts.size() == 4 && s1->visible && s2->visible && !s3->visible && !s4->visible &&
                               s1->distance == 5.0f && VlNear(s2->distance, std::sqrt(145.0f)) &&
                               GfHas({"dget 3", "rget e", "rget d", "rset e 0", "dset 3 1"}) &&
                               g_gfLog.size() >= 3 && g_gfLog[g_gfLog.size() - 3] == "dset 3 1" &&
                               g_gfLog[g_gfLog.size() - 2] == "rset e 1" && g_gfLog.back() == "rset d 0");
        WM(r.vlSort, textures == std::vector<std::string>({"settex i2 0", "settex i1 0"}) && list.m_shafts[0] == s2 &&
                         list.m_shafts[1] == s1);
        // The turns: the camera's (facing z: none), each shaft's round the viewer.
        const float to2[2] = {1.0f / std::sqrt(145.0f), 12.0f / std::sqrt(145.0f)};
        const float n2[2] = {0.0f, 1.0f};
        WM(r.vlListRender, list.m_yaw == 0.0f && list.m_viewDir.x == 0.0f && list.m_viewDir.y == 1.0f && s1->viewAngle == 0.0f &&
                               VlNear(s2->viewAngle, AngleBetween2D(n2, to2)) && VlNear(s2->viewDir.x, to2[0]));
        // The camera turned to x: its turn summed; the sector not drawn: nothing visible.
        cam->m_localMatrix(2) = S_vector(1.0f, 0.0f, 0.0f);
        cam->m_localMatrix(0) = S_vector(0.0f, 0.0f, -1.0f);
        cam->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        WmField<uint32_t>(sector, 0x160) = 0;
        list.Render(77);
        const float zdir[2] = {0.0f, 1.0f}, xdir[2] = {1.0f, 0.0f};
        WM(r.vlListRender, VlNear(list.m_yaw, AngleBetween2D(zdir, xdir)) && list.m_viewDir.x == 1.0f && !s1->visible &&
                               !s2->visible);
        // A shaft in no sector: not visible.
        WmField<uint32_t>(sector, 0x160) = 1;
        I3D_frame *dummy = GfFrame(FRAME_DUMMY, "vdummy");
        I3D_frame *l6 = GfFrame(FRAME_LIGHT, "lamp6", dummy);
        GfPlace(l6, S_vector(5.0f, 0.0f, 0.0f));
        S_Volumetric *s6 = shaft(l6, "i6");
        list.m_shafts.push_back(s6);
        list.Render(77);
        WM(r.vlListRender, !s6->visible);
        cam->m_localMatrix(2) = S_vector(0.0f, 0.0f, 1.0f);
        cam->m_localMatrix(0) = S_vector(1.0f, 0.0f, 0.0f);
        cam->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        for (S_Volumetric *s : list.m_shafts)
            for (S_VolumetricCone *c : s->cones)
                c->image = nullptr;
        list.Free();
    }

    // --- the clouds: loaded, freed, drawn ---
    {
        g_gfLog.clear();
        LoadClouds();
        WM(r.vlClouds, GfLogIs({"loadtex clouder.tga - 502", "loadtex clouder2.tga - 502"}) && g_clouds.images[0] &&
                           g_clouds.images[1] && g_cloudVertices[0] && g_cloudVertices[1]);
        // Drawn: a billboard in each 16 m cell in front of the camera, within the fades.
        GfPlace(cam, S_vector(8.0f, 20.0f, 8.0f));
        g_vlDraws.clear();
        g_vlDrawCounts.clear();
        g_gfLog.clear();
        DrawClouds(0);
        // (The expected quads, by the same arithmetic: each cell's place and fade.)
        int want[2] = {0, 0};
        const S_vector P(8.0f, 20.0f, 8.0f);
        S_LitVertex first[2][6] = {};
        bool seen[2] = {false, false};
        for (int ix = 0 - 6; ix < 0 + 6; ++ix) {
            for (int iy = 1 - 6; iy < 1 + 6; ++iy) {
                for (int iz = 0 - 6; iz < 0 + 6; ++iz) {
                    const float index = static_cast<float>(iz * 3 + iy * 5 + ix * 2);
                    const float s = std::sin(index);
                    const float size = s * 0.75f * 4.0f + 12.2f;
                    const S_vector pos((static_cast<float>(ix) + s) * 16.0f, (std::cos(index) + static_cast<float>(iy)) * 16.0f,
                                       (static_cast<float>(iz) + s) * 16.0f);
                    S_vector dd(P.x - pos.x, P.y - pos.y, P.z - pos.z);
                    const float d2 = (dd.x * dd.x + dd.z * dd.z) + dd.y * dd.y;
                    const float k = 1.0f / std::sqrt(d2);
                    const S_vector n(-(dd.x * k), -(dd.y * k), -(dd.z * k));
                    if ((n.x * 0.0f + n.z * 1.0f) + n.y * 0.0f < 0.0f)
                        continue;
                    float alpha = 0.4f;
                    if (!(d2 < 400.0f)) {
                        if (d2 > 2025.0f)
                            alpha = (1.0f - (d2 - 2025.0f) * 0x1.5aa2fep-12f) * 0.4f;
                    } else if (!(d2 > 100.0f)) {
                        alpha = 0.0f;
                    } else {
                        alpha = static_cast<float>(static_cast<float>(static_cast<float>(std::sqrt(d2) - 10.0) * 0.1) * 0.4);
                    }
                    alpha = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
                    if (!(alpha > 0.001f))
                        continue;
                    const int which = s <= 0.0f ? 1 : 0;
                    if (!seen[which]) {
                        seen[which] = true;
                        S_matrix m;
                        m.Identity();
                        m.SetDir(n, S_vector(0, 1, 0));
                        m(3) = pos;
                        const uint32_t color = VlColor(alpha * 255.0f);
                        first[which][2] = {S_vector(-size, -size, 0) * m, color, 0, 0};
                        first[which][1] = {S_vector(size, -size, 0) * m, color, 1, 0};
                        first[which][0] = {S_vector(size, size, 0) * m, color, 1, 1};
                        first[which][3] = {S_vector(-size, size, 0) * m, color, 0, 1};
                        first[which][4] = first[which][0];
                        first[which][5] = first[which][2];
                    }
                    want[which] += 6;
                }
            }
        }
        auto same = [](const S_LitVertex &a, const S_LitVertex &b) {
            return a.pos == b.pos && a.color == b.color && a.u == b.u && a.v == b.v;
        };
        bool firstOk = g_vlDraws.size() == 2;
        for (int w = 0; w < 2 && firstOk; ++w)
            for (int k = 0; k < 6; ++k)
                firstOk = firstOk && same(g_vlDraws[w][k], first[w][k]);
        WM(r.vlCloudDraw, want[0] > 0 && want[1] > 0 && g_vlDrawCounts == std::vector<int>({want[0] / 3, want[1] / 3}) &&
                              GfLogIs({"dset 3 0", "settex clouder.tga 0", "world", "draw 3 " + std::to_string(want[0] / 3) + " 0",
                                       "settex clouder2.tga 0", "world", "draw 3 " + std::to_string(want[1] / 3) + " 0"}) &&
                              firstOk);
        // Without the first texture: nothing.
        IImage *keep = g_clouds.images[0];
        g_clouds.images[0] = nullptr;
        g_gfLog.clear();
        DrawClouds(0);
        WM(r.vlCloudDraw, g_gfLog.empty());
        g_clouds.images[0] = keep;
        g_gfLog.clear();
        FreeClouds();
        WM(r.vlClouds, GfLogIs({"imgrelease clouder.tga", "imgrelease clouder2.tga"}) && !g_clouds.images[0] && !g_clouds.images[1] &&
                           !g_cloudVertices[0] && !g_cloudVertices[1]);
        FreeClouds();
        GfPlace(cam, S_vector(0.0f, 0.0f, 0.0f));
    }

    // --- through the GEffects module ---
    {
        C_GEffectsModule &m = g_geffectsModule;
        const std::string file = VlChunk(1, VlChunk(5, VlName(10, "lamp")));
        g_files[VL_FILE].assign(file.begin(), file.end());
        GfPlace(light, S_vector(0.0f, 0.0f, 5.0f));
        m.m_volumetrics.Load();
        WM(r.vlModule, vols.m_shafts.size() == 1);
        g_gfLog.clear();
        m.m_time = 4321;
        m.RenderVolumetrics();
        WM(r.vlModule, GfHas({"dget 3", "rset e 0", "dset 3 1"}) && vols.m_shafts[0]->visible);
        g_gfLog.clear();
        m.SetCloudsOn(true);
        WM(r.vlModule, GfLogIs({"loadtex clouder.tga - 502", "loadtex clouder2.tga - 502"}));
        g_gfLog.clear();
        m.SetCloudsOn(false);
        WM(r.vlModule, GfLogIs({"imgrelease clouder.tga", "imgrelease clouder2.tga"}));
        m.m_volumetrics.Free();
        WM(r.vlModule, vols.m_shafts.empty());
        g_files.erase(VL_FILE);
    }

    // ==== more cases (mutation run 1's survivors) ====
    {
        // Chunks: a flag cut short (not flat), the scale's chunk left before the next one.
        S_VolumetricCone c(0.0f);
        const std::string f1 = VlChunk(0x28, VlChunk(0x424, "ab") + VlChunk(0x460, VlF(3.0f)) + VlChunk(0x406, VlF(7.0f)));
        g_files[VL_FILE].assign(f1.begin(), f1.end());
        C_chunk ch;
        ch.Open(VL_FILE, false);
        WM(r.vlConeLoad, c.Load(ch) && !c.flat && c.uvScale == 3.0f && c.length == 7.0f);
        // A shaft: the far fade's chunk left before the next; cones without a light: their base the count.
        const std::string f2 = VlChunk(5, VlChunk(0x3C, VlF(60.0f)) + VlChunk(0x32, VlF(33.0f)) +
                                              VlChunk(0x28, VlChunk(0x30, "") + VlChunk(0x30, "")));
        g_files[VL_FILE].assign(f2.begin(), f2.end());
        C_chunk ch2;
        ch2.Open(VL_FILE, false);
        S_Volumetric v(nullptr);
        WM(r.vlShaftLoad, v.Load(ch2) && v.fadeOutEnd == 60.0f && v.fadeOutStart == 33.0f && v.cones.size() == 2 &&
                              v.cones[0]->base == 0.0f && v.cones[1]->base == 1.0f);
        g_files.erase(VL_FILE);
        // Two segments: both ends' rims clear; nothing past the count written.
        c.segments = 2;
        c.flat = false;
        c.Build(false, 0.0f);
        WM(r.vlBuildCone, c.alphas[0][1] == 0.0f && c.alphas[1][2] == 0.0f && c.alphas[0][2] == 0.5f && c.alphas[2][1] == 0.0f &&
                              c.cone[2][1].pos == S_vector(0, 0, 0) && c.cone[2][1].color == 0u);
        c.image = reinterpret_cast<IImage *>(new S_VlImage{g_vlImageVt, "x2"});
        c.Render(0, 0.0f, 0.3f);
        WM(r.vlRenderCone, c.cone[0][0].color == VlColor(0.3f * 0.5f * 255.0f) && c.cone[2][1].u == 0.0f);
        c.Render(0, 0.0f, 0.0f);
        WM(r.vlRenderCone, c.cone[0][0].color == 0x00FFFFFFu);
        // A fan rendered twice: its rest coordinates kept.
        c.flat = true;
        c.Build(false, 0.0f);
        const float u2 = c.fanUV[2][0], v2 = c.fanUV[2][1];
        c.Render(10, 0.0f, 1.0f);
        c.Render(10, 0.0f, 1.0f);
        WM(r.vlRender, c.fanUV[2][0] == u2 && c.fanUV[2][1] == v2);
    }
    {
        // Draw: turned to face the camera (z kept, the camera off to the side).
        S_Volumetric v(light);
        GfPlace(light, S_vector(3.0f, 4.0f, 0.0f));
        v.distance = 20.0f;
        v.Draw(0, S_vector(13.0f, 4.0f, 0.0f), 0.0f);
        S_matrix want = light->GetMatrix();
        want.SetDir3(want(2), S_vector(-(13.0f - 3.0f), -(4.0f - 4.0f), -(0.0f - 0.0f)));
        WM(r.vlDraw, VlNear(g_vlWorld.m[0][0], want.m[0][0]) && VlNear(g_vlWorld.m[0][1], want.m[0][1]) &&
                         VlNear(g_vlWorld.m[1][0], want.m[1][0]) && VlNear(g_vlWorld.m[1][1], want.m[1][1]) &&
                         !(VlNear(g_vlWorld.m[1][1], 1.0f) && VlNear(g_vlWorld.m[0][0], 1.0f)));
        // The camera on the frame's other side (x 1 against 3): turned the other way.
        v.Draw(0, S_vector(1.0f, 4.0f, 0.0f), 0.0f);
        S_matrix want2 = light->GetMatrix();
        want2.SetDir3(want2(2), S_vector(-(1.0f - 3.0f), 0.0f, 0.0f));
        WM(r.vlDraw, VlNear(g_vlWorld.m[0][1], want2.m[0][1]) && VlNear(g_vlWorld.m[1][0], want2.m[1][0]) &&
                         !VlNear(want2.m[0][1], want.m[0][1]));
    }
    {
        // Render: the camera off the origin, turning (its turns summed), shafts sideways, at the fade's
        // end, at equal distances, near the camera.
        C_Volumetrics list;
        g_clouds.images[0] = reinterpret_cast<IImage *>(new S_VlImage{g_vlImageVt, "cloudA"});
        g_cloudVertices[0] = new S_LitVertex[0x3CC00 / sizeof(S_LitVertex)];
        g_cloudVertices[1] = new S_LitVertex[0x3CC00 / sizeof(S_LitVertex)];
        auto setCam = [&](const S_vector &pos, const S_vector &z, const S_vector &x) {
            GfPlace(cam, pos);
            cam->m_localMatrix(2) = z;
            cam->m_localMatrix(0) = x;
            cam->m_localMatrix(1) = z.Cross(x);
            cam->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        };
        const S_vector C(100.0f, 0.0f, 200.0f);
        setCam(C, S_vector(0, 0, 1), S_vector(1, 0, 0));
        WmField<uint32_t>(sector, 0x160) = 1;
        auto make = [&](const std::string &name, const S_vector &off) {
            I3D_frame *f = GfFrame(FRAME_LIGHT, name, sector);
            f->m_frameFlags |= 1u;
            GfPlace(f, S_vector(C.x + off.x, C.y + off.y, C.z + off.z));
            auto *s = new S_Volumetric(f);
            auto *cn = new S_VolumetricCone(0.0f);
            cn->image = reinterpret_cast<IImage *>(new S_VlImage{g_vlImageVt, name});
            cn->segments = 1;
            s->cones.push_back(cn);
            s->viewAngle = 1.0f;
            s->viewDir = {0.0f, 1.0f};
            return s;
        };
        S_Volumetric *side = make("side", S_vector(5, 0, 0)), *slight = make("slight", S_vector(-4, 0, 0.6f)),
                     *edge = make("edge", S_vector(0, 0, 30.0f)), *e1 = make("e1", S_vector(3, 0, 4)),
                     *e2 = make("e2", S_vector(-3, 0, 4)), *px = make("px", S_vector(1e-5f, 0, 0)),
                     *pz = make("pz", S_vector(0, 0, 1e-5f)), *p0 = make("p0", S_vector(0, 0, 0));
        list.m_shafts = {side, slight, edge, e1, e2, px, pz, p0};
        list.m_viewDir = {0.0f, 1.0f};
        g_vlState3 = 0;
        g_gfLog.clear();
        list.Render(5);
        std::vector<std::string> textures;
        for (const std::string &l : g_gfLog)
            if (l.compare(0, 7, "settex ") == 0)
                textures.push_back(l);
        const float slightN[2] = {-4.0f / std::sqrt(16.36f), 0.6f / std::sqrt(16.36f)};
        (void)slightN;
        WM(r.vlListRender, side->visible && side->distance == 5.0f && slight->visible && !edge->visible && e1->visible &&
                               e2->visible && GfHas({"settex cloudA 0"}) && GfHas({"dset 3 1"}) && g_gfLog.back() == "rset d 0" &&
                               std::count(g_gfLog.begin(), g_gfLog.end(), std::string("dset 3 0")) == 2);
        // (Equal distances: the first kept first.)
        const auto at = [&](const std::string &t) { return std::find(textures.begin(), textures.end(), "settex " + t + " 0") - textures.begin(); };
        WM(r.vlSort, at("e1") < at("e2") && at("side") < at("e1") && at("e2") < at("slight"));
        // Next to the camera: their directions the unit forms.
        WM(r.vlListRender, px->visible && pz->visible && px->viewDir.x == 1.0f && px->viewDir.y == 0.0f && pz->viewDir.x == 0.0f &&
                               pz->viewDir.y == 1.0f && p0->visible && p0->viewDir.x == 0.0f && p0->viewDir.y == 0.0f);
        // The view turns summed (from 1).
        const float sideTo[2] = {1.0f, 0.0f}, start[2] = {0.0f, 1.0f};
        WM(r.vlListRender, VlNear(side->viewAngle, AngleBetween2D(start, sideTo) + 1.0f));
        // The camera's turns summed: to x, then back to z.
        setCam(C, S_vector(1, 0, 0), S_vector(0, 0, -1));
        list.Render(5);
        const float t1 = list.m_yaw;
        setCam(C, S_vector(0, 0, 1), S_vector(1, 0, 0));
        list.Render(5);
        const float zd[2] = {0, 1}, xd[2] = {1, 0};
        WM(r.vlListRender, VlNear(t1, AngleBetween2D(zd, xd)) && VlNear(list.m_yaw, AngleBetween2D(xd, zd) + t1) && t1 != 0.0f);
        // Diagonal and tilted cameras: the direction on the ground normalized.
        setCam(C, S_vector(0.6f, 0, 0.8f), S_vector(0.8f, 0, -0.6f));
        list.Render(5);
        WM(r.vlListRender, VlNear(list.m_viewDir.x, 0.6f) && VlNear(list.m_viewDir.y, 0.8f));
        setCam(C, S_vector(0.6f, 0.8f, 0), S_vector(0, 0, -1));
        list.Render(5);
        WM(r.vlListRender, VlNear(list.m_viewDir.x, 1.0f) && list.m_viewDir.y == 0.0f);
        setCam(C, S_vector(0, 0.6f, 0.8f), S_vector(1, 0, 0));
        list.Render(5);
        WM(r.vlListRender, list.m_viewDir.x == 0.0f && VlNear(list.m_viewDir.y, 1.0f));
        // Looking up: no direction on the ground (0, 0); nearly up: the side's unit form.
        setCam(C, S_vector(0, 1, 0), S_vector(1, 0, 0));
        list.Render(5);
        WM(r.vlListRender, list.m_viewDir.x == 0.0f && list.m_viewDir.y == 0.0f);
        setCam(C, S_vector(1e-5f, 1, 0), S_vector(1, 0, 0));
        list.Render(5);
        WM(r.vlListRender, list.m_viewDir.x == 1.0f && list.m_viewDir.y == 0.0f);
        setCam(C, S_vector(0, 1, 1e-5f), S_vector(1, 0, 0));
        list.Render(5);
        WM(r.vlListRender, list.m_viewDir.x == 0.0f && list.m_viewDir.y == 1.0f);
        // A short camera direction: x, z, y made 1 (seen on the ground).
        setCam(C, S_vector(1e-5f, 0, 0), S_vector(0, 0, -1));
        list.Render(5);
        WM(r.vlListRender, list.m_viewDir.x == 1.0f && list.m_viewDir.y == 0.0f);
        setCam(C, S_vector(0, 0, 1e-5f), S_vector(1, 0, 0));
        list.Render(5);
        WM(r.vlListRender, list.m_viewDir.x == 0.0f && list.m_viewDir.y == 1.0f);
        setCam(C, S_vector(0, 0, 1), S_vector(1, 0, 0));
        for (S_Volumetric *s : list.m_shafts)
            for (S_VolumetricCone *cn : s->cones)
                cn->image = nullptr;
        list.Free();
        FreeClouds();
    }
    {
        // The clouds compared vertex by vertex: the camera 3 m before a cloud, and short directions.
        g_gfLog.clear();
        LoadClouds();
        auto expect = [&](const S_vector &P, const S_vector &F, const S_vector &U, std::vector<S_LitVertex> out[2]) {
            const int cx = static_cast<int>(P.x * 0.0625f), cy = static_cast<int>(P.y * 0.0625f), cz = static_cast<int>(P.z * 0.0625f);
            for (int ix = cx - 6; ix < cx + 6; ++ix)
                for (int iy = cy - 6; iy < cy + 6; ++iy)
                    for (int iz = cz - 6; iz < cz + 6; ++iz) {
                        const float index = static_cast<float>(iz * 3 + iy * 5 + ix * 2);
                        const float s = std::sin(index);
                        const float size = s * 0.75f * 4.0f + 12.2f;
                        const S_vector pos((static_cast<float>(ix) + s) * 16.0f, (std::cos(index) + static_cast<float>(iy)) * 16.0f,
                                           (static_cast<float>(iz) + s) * 16.0f);
                        const S_vector dd(P.x - pos.x, P.y - pos.y, P.z - pos.z);
                        const float d2 = (dd.x * dd.x + dd.z * dd.z) + dd.y * dd.y;
                        const float k = 1.0f / std::sqrt(d2);
                        const S_vector n(-(dd.x * k), -(dd.y * k), -(dd.z * k));
                        if ((n.x * F.x + n.z * F.z) + n.y * F.y < 0.0f)
                            continue;
                        float alpha = 0.4f;
                        if (!(d2 < 400.0f)) {
                            if (d2 > 2025.0f)
                                alpha = (1.0f - (d2 - 2025.0f) * 0x1.5aa2fep-12f) * 0.4f;
                        } else if (!(d2 > 100.0f)) {
                            alpha = 0.0f;
                        } else {
                            alpha = static_cast<float>(static_cast<float>(static_cast<float>(std::sqrt(d2) - 10.0) * 0.1) * 0.4);
                        }
                        alpha = alpha < 0.0f ? 0.0f : alpha;
                        if (!(alpha > 0.001f))
                            continue;
                        S_matrix m;
                        m.Identity();
                        m.SetDir(n, U);
                        m(3) = pos;
                        const uint32_t color = VlColor(alpha * 255.0f);
                        S_LitVertex q[6];
                        q[2] = {S_vector(-size, -size, 0) * m, color, 0, 0};
                        q[1] = {S_vector(size, -size, 0) * m, color, 1, 0};
                        q[0] = {S_vector(size, size, 0) * m, color, 1, 1};
                        q[3] = {S_vector(-size, size, 0) * m, color, 0, 1};
                        q[4] = q[0];
                        q[5] = q[2];
                        out[s <= 0.0f ? 1 : 0].insert(out[s <= 0.0f ? 1 : 0].end(), q, q + 6);
                    }
        };
        auto sameAll = [](const std::vector<S_LitVertex> &a, const std::vector<S_LitVertex> &b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (!(a[i].pos == b[i].pos && a[i].color == b[i].color && a[i].u == b[i].u && a[i].v == b[i].v))
                    return false;
            return true;
        };
        auto run = [&](const S_vector &P, const S_vector &F, const S_vector &Fwant, const S_vector &U) {
            GfPlace(cam, P);
            cam->m_localMatrix(2) = F;
            cam->m_localMatrix(1) = U;
            cam->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            std::vector<S_LitVertex> want[2];
            expect(P, Fwant, U, want);
            g_vlDraws.clear();
            DrawClouds(0);
            size_t k = 0;
            bool ok = true;
            for (int w = 0; w < 2; ++w) {
                if (want[w].empty())
                    continue;
                ok = ok && k < g_vlDraws.size() && sameAll(g_vlDraws[k], want[w]);
                ++k;
            }
            return ok && k == g_vlDraws.size();
        };
        // (Cell (1, 2, 1)'s cloud, the camera 3 m before it, 24 / 40 / 24 the cell indices' scale.)
        const float index = static_cast<float>(1 * 3 + 2 * 5 + 1 * 2);
        const S_vector beside((1.0f + std::sin(index)) * 16.0f, (std::cos(index) + 2.0f) * 16.0f, (1.0f + std::sin(index)) * 16.0f - 3.0f);
        WM(r.vlCloudDraw, run(beside, S_vector(0, 0, 1), S_vector(0, 0, 1), S_vector(0, 1, 0)) &&
                              run(S_vector(24.0f, 40.0f, 24.0f), S_vector(0, 0, 1), S_vector(0, 0, 1), S_vector(0, 1, 0)) &&
                              run(S_vector(100.0f, 40.0f, 100.0f), S_vector(0, 0, 1), S_vector(0, 0, 1), S_vector(0, 1, 0)) &&
                              run(S_vector(100.0f, 40.0f, 100.0f), S_vector(-1, 0, 0), S_vector(-1, 0, 0), S_vector(0, 1, 0)) &&
                              run(S_vector(-30.0f, 17.0f, 50.0f), S_vector(0.6f, 0, 0.8f), S_vector(0.6f, 0, 0.8f), S_vector(0, 1, 0)));
        const S_vector P2(10.0f, 30.0f, 10.0f);
        bool shortOk = true;
        const S_vector f[7] = {S_vector(1e-5f, 0, 0), S_vector(-1e-5f, 0, 0), S_vector(0, 0, 1e-5f), S_vector(0, 0, -1e-5f),
                               S_vector(0, 1e-5f, 0), S_vector(0, -1e-5f, 0), S_vector(0, 0, 0)};
        const S_vector fw[7] = {S_vector(1, 0, 0), S_vector(-1, 0, 0), S_vector(0, 0, 1), S_vector(0, 0, -1),
                                S_vector(0, 1, 0), S_vector(0, -1, 0), S_vector(0, 1, 0)};
        for (int i = 0; i < 7; ++i) {
            const bool ok = run(P2, f[i], fw[i], S_vector(0, 1, 0));
            if (!ok && g_wmTrace)
                std::printf("  short cloud direction %d\n", i);
            shortOk = shortOk && ok;
        }
        WM(r.vlCloudDraw, shortOk);
        FreeClouds();
        GfPlace(cam, S_vector(0, 0, 0));
        cam->m_localMatrix(2) = S_vector(0, 0, 1);
        cam->m_localMatrix(1) = S_vector(0, 1, 0);
        cam->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
    }

    WmField<uint32_t>(sector, 0x160) = 0;
    g_gfScene.clear();
    std::memcpy(g_igraphVtable, igraphSlots, sizeof igraphSlots);
    std::memcpy(g_driverVtable, driverSlots, sizeof driverSlots);
    std::memcpy(g_sceneVtable, sceneSlots, sizeof sceneSlots);
    WmField<I3D_frame *>(scene, 0x15C) = nullptr;
    std::printf("volumetrics: vlCone %d vlConeLoad %d vlBuild %d vlBuildCone %d vlTexture %d vlRender %d vlRenderCone %d "
                "vlShaft %d vlShaftLoad %d vlRefresh %d vlDraw %d vlFade %d vlList %d vlPrune %d vlListRender %d vlSort %d "
                "vlClouds %d vlCloudDraw %d vlModule %d\n",
                r.vlCone, r.vlConeLoad, r.vlBuild, r.vlBuildCone, r.vlTexture, r.vlRender, r.vlRenderCone, r.vlShaft,
                r.vlShaftLoad, r.vlRefresh, r.vlDraw, r.vlFade, r.vlList, r.vlPrune, r.vlListRender, r.vlSort, r.vlClouds,
                r.vlCloudDraw, r.vlModule);
}
