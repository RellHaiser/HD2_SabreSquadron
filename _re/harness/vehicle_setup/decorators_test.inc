// Part 8d-ii's decorators (game/decorators.cpp: load, start, switch, free) (WORLD_TEST), after
// birds_test.inc (its fakes). decorations.bin and decorator_sl.bin are made up in g_files; models
// are frames of the GEffects test's vtable, meshes the GEffects 2 test's. Prints the "decorators:" line.

namespace {

std::string DcI(int v) { return std::string(reinterpret_cast<const char *>(&v), 4); }
std::string DcF(float v) { return std::string(reinterpret_cast<const char *>(&v), 4); }
std::string DcB(int v) { return std::string(1, static_cast<char>(v)); }
std::string DcChunk(uint16_t id, const std::string &payload)
{
    std::string out(6, '\0');
    const uint32_t size = static_cast<uint32_t>(6 + payload.size());
    std::memcpy(&out[0], &id, 2);
    std::memcpy(&out[2], &size, 4);
    return out + payload;
}

// A decorator's values as decorations.bin of `version` holds them (the defaults: what is not read).
struct S_DcDecorator {
    std::string name;
    float chance = 1.0f;
    int bendable = 1;
    float height = 0.0f;
    int randomRotation = 1;
    int animation = 0;
    float amplitude = 0.2f;
    float frequency = 1.0f;
    int alignToGround = 0;
};
std::string DcDecorator(const S_DcDecorator &d, int version)
{
    std::string s = DcChunk(1, d.name + std::string(1, '\0')) + DcF(d.chance) + DcB(d.bendable);
    if (version > 3)
        s += DcF(d.height);
    if (version > 4)
        s += DcB(d.randomRotation);
    if (version > 5)
        s += DcI(d.animation) + DcF(d.amplitude);
    if (version > 6)
        s += DcF(d.frequency);
    if (version > 7)
        s += DcB(d.alignToGround);
    return s;
}
std::string DcSet(const std::vector<S_DcDecorator> &ds, int version, float minSlope = 0.95f, float cover = 0.5f,
                  int count = -1)
{
    std::string s = DcI(count < 0 ? static_cast<int>(ds.size()) : count);
    if (version > 2)
        s += DcF(minSlope);
    if (version > 8)
        s += DcF(cover);
    for (const S_DcDecorator &d : ds)
        s += DcDecorator(d, version);
    return DcChunk(2, s);
}
bool DcIs(const S_Decorator *d, const S_DcDecorator &e)
{
    return d && std::string(d->model.c_str()) == e.name && d->chance == e.chance && d->bendable == (e.bendable != 0) &&
           d->height == e.height && d->randomRotation == (e.randomRotation != 0) && d->animation == e.animation &&
           d->amplitude == e.amplitude && d->frequency == e.frequency && d->alignToGround == (e.alignToGround != 0) &&
           !d->pool && !d->loadFailed && !d->mesh && d->vertices.empty() && d->restPositions.empty();
}
const char *const DC_FILE = "missions\\GFX\\decorations.bin";
const char *const DC_LIGHT = "missions\\GFX\\decorator_sl.bin";
void DcFile(const char *name, const std::string &data)
{
    g_files[name].assign(data.begin(), data.end());
}

struct S_DcResults {
    bool dcMake = true, dcLoad = true, dcVersions = true, dcLoadEnd = true, dcLight = true, dcStart = true, dcGrid = true;
    bool dcOn = true, dcClear = true, dcFree = true, dcCover = true, dcModule = true;
};

}  // namespace

void DecoratorsTest()
{
    S_DcResults r;
    GetGame()->m_missionName = "GFX";
    S_DetailSettings &details = *g_gameProfile.GetDetailSettings();
    const bool oldSwitch = details.switches[DETAIL_DECORATORS];
    g_files.erase(DC_FILE);
    g_files.erase(DC_LIGHT);

    // --- made ---
    {
        C_Decorators d;
        const C_DecoratorGrid &g = d.m_grid;
        WM(r.dcMake, !d.m_on && g.m_sizeX == 60 && g.m_sizeZ == 60 && g.m_cellSize == 0.5f && g.m_originX == -1 &&
                         g.m_originZ == -1 && g.m_shiftX == 0 && g.m_shiftZ == 0 && !g.m_cells && g.m_bends.empty() &&
                         g.m_fadeAlpha == 0.05f && !g.m_fading && g.m_fadeTime == 0 && g.m_stance == 0 &&
                         !g.m_firstPerson && !d.m_light && d.m_sets.empty() && d.m_pools.empty() && d.m_animated.empty());
        WM(r.dcMake, d.m_lightX == 0.0f && d.m_lightZ == 0.0f && d.m_unknown7C == 0 && d.m_unknown80 == 0 &&
                         d.m_lightCellSize == 0.0f && d.m_lightWidth == 0 && d.m_lightHeight == 0 && g.m_subX == 0.0f &&
                         g.m_subZ == 0.0f && g.m_time == 0);
        S_DecoratorBend bend;
        WM(r.dcMake, !bend.model && bend.target == 0.0f && bend.angle == 0.0f && bend.speed == 0.0f &&
                         bend.light == 0.0f && bend.lightTarget == 0.0f && bend.springing == 0 && bend.unknown1C == 8000 &&
                         !bend.cell);
        S_DecoratorSet set;
        WM(r.dcMake, set.minSlope == 0.95f && set.groundCover == 0.5f && set.decorators.empty());
        set.decorators.push_back(new S_Decorator());
        set.minSlope = set.groundCover = 2.0f;
        set.Reset();
        WM(r.dcMake, set.minSlope == 0.95f && set.groundCover == 0.5f && set.decorators.empty());
        S_Decorator dec;
        WM(r.dcMake, DcIs(&dec, S_DcDecorator{""}));
        S_DecoratorCell cell;
        WM(r.dcMake, cell.set == 0 && !cell.model && cell.decorator == -1 && !cell.bend && !cell.bending);
        // Reset: the defaults back, the mesh released.
        dec.chance = 5.0f;
        dec.bendable = dec.randomRotation = false;
        dec.loadFailed = dec.alignToGround = true;
        dec.height = 3.0f;
        dec.animation = 2;
        dec.amplitude = dec.frequency = 7.0f;
        dec.model = "x";
        dec.pool = reinterpret_cast<S_DecoratorPool *>(&cell);
        dec.mesh = reinterpret_cast<I3D_mesh *>(GfMesh("rm", {}));
        float vertex = 0.0f;
        dec.vertices.push_back(&vertex);
        dec.restPositions.resize(3);
        g_gfLog.clear();
        dec.Reset();
        WM(r.dcMake, DcIs(&dec, S_DcDecorator{""}) && GfLogIs({"release rm"}) && dec.restPositions.capacity() == 0);
        dec.mesh = reinterpret_cast<I3D_mesh *>(GfMesh("dm", {}));
    }
    WM(r.dcMake, GfLogIs({"release rm", "release dm"}));

    // --- decorations.bin of each version (each reads what the one before did and more) ---
    for (int version = 1; version <= 10; ++version) {
        const S_DcDecorator a{"Grass01", 40.0f, 0, 2.5f, 0, 2, 0.3f, 3.0f, 1};
        const S_DcDecorator b{"flower", 75.0f, 2, -1.0f, 5, 1, 0.6f, 0.5f, 0};
        DcFile(DC_FILE, DcChunk(static_cast<uint16_t>(version),
                                DcI(40) + DcF(0.75f) + DcI(2) + DcSet({a, b}, version, 0.8f, 0.25f)));
        C_Decorators d;
        d.Load();
        // (What this version does not hold keeps its default.)
        S_DcDecorator ea{"Grass01", 40.0f, 0}, eb{"flower", 75.0f, 2};
        if (version > 3)
            ea.height = 2.5f, eb.height = -1.0f;
        if (version > 4)
            ea.randomRotation = 0, eb.randomRotation = 5;
        if (version > 5)
            ea.animation = 2, ea.amplitude = 0.3f, eb.animation = 1, eb.amplitude = 0.6f;
        if (version > 6)
            ea.frequency = 3.0f, eb.frequency = 0.5f;
        if (version > 7)
            ea.alignToGround = 1;
        bool ok;
        if (version == 1) {
            ok = d.m_sets.empty() && d.m_grid.m_sizeX == 60 && d.m_grid.m_cellSize == 0.5f;
        } else {
            const S_DecoratorSet *s = d.m_sets.size() == 3 ? d.m_sets[2] : nullptr;
            ok = d.m_grid.m_sizeX == 40 && d.m_grid.m_sizeZ == 40 && d.m_grid.m_cellSize == 0.75f && !d.m_sets[0] &&
                 !d.m_sets[1] && s && s->decorators.size() == 2 && DcIs(s->decorators[0], ea) && DcIs(s->decorators[1], eb) &&
                 s->minSlope == (version > 2 ? 0.8f : 0.95f) && s->groundCover == (version > 8 ? 0.25f : 0.5f) &&
                 !d.m_grid.m_cells && d.m_pools.empty() && d.m_animated.empty() && !d.m_on;
        }
        if (!ok)
            std::printf("dcVersions: version %d\n", version);
        WM(r.dcVersions, ok);
    }

    // --- materials: several, one again (the first set deleted), the last without its header ---
    {
        const int v = 9;
        S_DcDecorator x{"x"}, y{"y"}, z{"z"};
        DcFile(DC_FILE, DcChunk(9, DcI(8) + DcF(2.0f) + DcI(3) + DcSet({x}, v) + DcI(0) + DcSet({y, z}, v) + DcI(3) +
                                       DcSet({z}, v, 0.5f) + DcI(1)));
        C_Decorators d;
        g_gfLog.clear();
        d.Load();
        WM(r.dcLoad, d.m_sets.size() == 4 && d.m_sets[0] && d.m_sets[0]->decorators.size() == 2 &&
                         DcIs(d.m_sets[0]->decorators[1], z) && d.m_sets[1] && d.m_sets[1]->decorators.empty() &&
                         d.m_sets[1]->minSlope == 0.95f && !d.m_sets[2] && d.m_sets[3] && d.m_sets[3]->minSlope == 0.5f &&
                         d.m_sets[3]->decorators.size() == 1 && DcIs(d.m_sets[3]->decorators[0], z) && g_gfLog.empty() &&
                         d.m_grid.m_sizeX == 8 && d.m_grid.m_cellSize == 2.0f);
        // A set with fewer decorators than it says (the file ends): the load stops there, though its
        // top chunk says there is more.
        std::string setChunk = DcSet({x}, v, 0.95f, 0.5f, 3);
        const uint32_t setLonger = static_cast<uint32_t>(setChunk.size() + 100);
        std::memcpy(&setChunk[2], &setLonger, 4);
        std::string top = DcChunk(9, DcI(8) + DcF(2.0f) + DcI(1) + setChunk);
        const uint32_t longer = static_cast<uint32_t>(top.size() + 200);
        std::memcpy(&top[2], &longer, 4);
        DcFile(DC_FILE, top);
        C_Decorators d2;
        d2.Load();
        WM(r.dcLoad, d2.m_sets.size() == 2 && !d2.m_sets[0] && d2.m_sets[1] && d2.m_sets[1]->decorators.size() == 1 &&
                         DcIs(d2.m_sets[1]->decorators[0], x));
        // Loaded again: the old set at that material deleted (its decorators with it).
        d2.m_sets[1]->decorators[0]->mesh = reinterpret_cast<I3D_mesh *>(GfMesh("old", {}));
        DcFile(DC_FILE, DcChunk(9, DcI(8) + DcF(2.0f) + DcI(1) + DcSet({y}, v)));
        g_gfLog.clear();
        d2.Load();
        WM(r.dcLoad, GfLogIs({"release old"}) && d2.m_sets.size() == 2 && DcIs(d2.m_sets[1]->decorators[0], y));
        // A set's chunk without its count: no decorators (the next material read on).
        DcFile(DC_FILE, DcChunk(9, DcI(8) + DcF(2.0f) + DcI(0) + DcChunk(2, "") + DcI(1) + DcSet({x}, v)));
        C_Decorators d7;
        d7.Load();
        WM(r.dcLoadEnd, d7.m_sets.size() == 2 && d7.m_sets[0]->decorators.empty() && d7.m_sets[1] &&
                            d7.m_sets[1]->decorators.size() == 1 && DcIs(d7.m_sets[1]->decorators[0], x));
        // A set loaded again: its old decorators and values gone.
        {
            C_chunk chunk;
            S_DecoratorSet again;
            again.decorators.push_back(new S_Decorator());
            again.groundCover = 3.0f;
            const std::string set = DcSet({y}, v, 0.4f);
            DcFile(DC_FILE, set);
            chunk.Open(DC_FILE, false);
            WM(r.dcLoadEnd, again.Load(chunk, v) && again.decorators.size() == 1 && DcIs(again.decorators[0], y) &&
                                again.minSlope == 0.4f && again.groundCover == 0.5f);
            // No header: false (reset all the same).
            again.groundCover = 3.0f;
            WM(r.dcLoadEnd, !again.Load(chunk, v) && again.decorators.empty() && again.groundCover == 0.5f);
        }
        // A material's index cut short by its chunk: material 0, its set's header not there.
        DcFile(DC_FILE, DcChunk(9, DcI(8) + DcF(2.0f) + "ab"));
        C_Decorators d3;
        d3.Load();
        WM(r.dcLoadEnd, d3.m_sets.size() == 1 && d3.m_sets[0] && d3.m_sets[0]->decorators.empty());
        // Cut short by the file's end (the chunk says there is more): its 2 bytes read (material
        // 0x6261), then no set.
        std::string cut = DcChunk(9, DcI(8) + DcF(2.0f) + "ab");
        const uint32_t cutLonger = static_cast<uint32_t>(cut.size() + 100);
        std::memcpy(&cut[2], &cutLonger, 4);
        DcFile(DC_FILE, cut);
        C_Decorators d8;
        d8.Load();
        WM(r.dcLoadEnd, d8.m_sets.size() == 0x6262 && d8.m_sets[0x6261] && d8.m_sets[0x6261]->decorators.empty() &&
                            !d8.m_sets[0]);
        // A name chunk longer than the buffer: 999 characters.
        DcFile(DC_FILE, DcChunk(9, DcI(8) + DcF(2.0f) + DcI(0) +
                                       DcChunk(2, DcI(1) + DcF(0.95f) + DcF(0.5f) + DcChunk(1, std::string(1200, 'n')) +
                                                      DcF(1.0f) + DcB(1) + std::string(18, '\0'))));
        C_Decorators d4;
        d4.Load();
        WM(r.dcLoadEnd, d4.m_sets.size() == 1 && d4.m_sets[0]->decorators.size() == 1 &&
                            std::string(d4.m_sets[0]->decorators[0]->model.c_str()) == std::string(999, 'n'));
        // An empty top chunk; no file at all.
        DcFile(DC_FILE, DcChunk(9, ""));
        C_Decorators d5;
        d5.Load();
        WM(r.dcLoadEnd, d5.m_sets.empty() && d5.m_grid.m_sizeX == 60);
        DcFile(DC_FILE, DcChunk(9, DcI(5)));
        d5.Load();
        WM(r.dcLoadEnd, d5.m_sets.empty() && d5.m_grid.m_sizeX == 5 && d5.m_grid.m_sizeZ == 5 && d5.m_grid.m_cellSize == 0.5f);
        g_files.erase(DC_FILE);
        C_Decorators d6;
        d6.Load();
        WM(r.dcLoadEnd, d6.m_sets.empty() && d6.m_grid.m_sizeX == 60);
    }

    // --- the light map ---
    {
        const std::string light = DcChunk(0x10, DcI(3) + DcI(2) + DcF(2.0f) + DcF(10.0f) + DcF(20.0f) + DcI(5) + DcI(6) +
                                                    std::string("\x01\x02\x03\x04\x05\xff", 6));
        DcFile(DC_LIGHT, light);
        // Not without decorations.bin.
        C_Decorators d;
        d.Load();
        WM(r.dcLight, !d.m_light && d.m_lightWidth == 0);
        DcFile(DC_FILE, DcChunk(1, ""));
        d.Load();
        WM(r.dcLight, d.m_light && d.m_lightWidth == 3 && d.m_lightHeight == 2 && d.m_lightCellSize == 2.0f &&
                          d.m_lightX == 10.0f && d.m_lightZ == 20.0f && d.m_unknown7C == 5 && d.m_unknown80 == 6 &&
                          std::memcmp(d.m_light, "\x01\x02\x03\x04\x05\xff", 6) == 0);
        // Again without the light map: none.
        g_files.erase(DC_LIGHT);
        d.Load();
        WM(r.dcLight, !d.m_light);
        // A short one: none of the bytes read (cleared).
        DcFile(DC_LIGHT, DcChunk(0x10, DcI(2) + DcI(2) + DcF(1.0f) + DcF(0.0f) + DcF(0.0f) + DcI(0) + DcI(0) + "\x07"));
        d.Load();
        WM(r.dcLight, d.m_light && d.m_light[0] == 0 && d.m_light[3] == 0 && d.m_lightWidth == 2);
        d.Clear();
        WM(r.dcLight, !d.m_light);
        g_files.erase(DC_LIGHT);
        g_files.erase(DC_FILE);
    }

    // --- started: the pools, the animated decorators, the grid ---
    I3D_frame *p1 = GfFrame(FRAME_MODEL, "p1"), *p2 = GfFrame(FRAME_MODEL, "p2");
    {
        const int v = 9;
        S_DcDecorator a{"Grass01"}, b{"flower"}, c{"GRASS01"};
        a.animation = 1;
        c.animation = 2;
        DcFile(DC_FILE, DcChunk(9, DcI(4) + DcF(1.0f) + DcI(0) + DcSet({a, b}, v) + DcI(2) + DcSet({c}, v)));
        C_Decorators d;
        d.Load();
        S_Decorator *da = d.m_sets[0]->decorators[0], *db = d.m_sets[0]->decorators[1], *dc = d.m_sets[2]->decorators[0];
        details.switches[DETAIL_DECORATORS] = true;
        g_firstPerson = true;
        d.m_grid.m_fading = true;
        d.m_grid.m_fadeTime = 7;
        d.m_grid.m_fadeAlpha = 1.0f;
        d.m_grid.m_stance = 9;
        g_gfLog.clear();
        d.Start();
        WM(r.dcStart, d.m_pools.size() == 2 && std::string(d.m_pools[0]->name.c_str()) == "grass01" &&
                          std::string(d.m_pools[1]->name.c_str()) == "flower" && da->pool == d.m_pools[0] &&
                          dc->pool == d.m_pools[0] && db->pool == d.m_pools[1] && d.m_pools[0]->models.empty() &&
                          d.m_animated == std::vector<S_Decorator *>({dc, da}) && d.m_on && g_decoratorsEnabled &&
                          g_gfLog.empty());
        const C_DecoratorGrid &g = d.m_grid;
        bool cells = g.m_cells != nullptr;
        for (int i = 0; cells && i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                cells = cells && g.m_cells[i][j].decorator == -1 && !g.m_cells[i][j].model && !g.m_cells[i][j].bend;
        WM(r.dcGrid, cells && g.m_originX == 100000 && g.m_originZ == 100000 && !g.m_fading && g.m_fadeTime == 0 &&
                         g.m_fadeAlpha == 0.05f && g.m_stance == 0 && g.m_firstPerson && g.m_sizeX == 4);
        // Started again (no Free): the pools found, the animated listed twice.
        d.m_grid.Free();
        g_firstPerson = false;
        d.Start();
        WM(r.dcStart, d.m_pools.size() == 2 && d.m_animated == std::vector<S_Decorator *>({dc, da, dc, da}) &&
                          !d.m_grid.m_firstPerson);
        // Off by the detail option: started hidden.
        d.m_grid.Free();
        details.switches[DETAIL_DECORATORS] = false;
        d.m_on = false;
        d.Start();
        WM(r.dcStart, !d.m_on && !g_decoratorsEnabled && d.m_grid.m_cells);
        // An empty grid: no cells.
        d.m_grid.Free();
        d.m_grid.m_sizeX = 0;
        d.m_grid.m_originX = 3;
        d.Start();
        WM(r.dcGrid, !d.m_grid.m_cells && d.m_grid.m_originX == 3);
        d.m_grid.m_sizeX = 4;
        d.m_grid.Init();

        // --- cleared: the animated meshes, the sets, the pools (their models), the light map ---
        da->mesh = reinterpret_cast<I3D_mesh *>(GfMesh("ma", {}));
        db->mesh = reinterpret_cast<I3D_mesh *>(GfMesh("mb", {}));
        dc->mesh = reinterpret_cast<I3D_mesh *>(GfMesh("mc", {}));
        d.m_pools[0]->models = {p1, p2};
        d.m_light = new uint8_t[4];
        d.m_animated = {da, dc};
        g_gfLog.clear();
        d.Clear();
        WM(r.dcClear, GfLogIs({"release mc", "release ma", "release mb", "release p2", "release p1"}) && d.m_sets.empty() &&
                          d.m_pools.empty() && d.m_animated.empty() && !d.m_light && d.m_grid.m_cells);
        d.m_grid.Free();
        g_files.erase(DC_FILE);
    }

    // --- switched on and off: each cell's model, rows of m_sizeZ, m_sizeX each ---
    {
        C_Decorators d;
        S_DecoratorCell rows[3][3];
        S_DecoratorCell *rowPtrs[3] = {rows[0], rows[1], rows[2]};
        d.m_grid.m_cells = rowPtrs;
        d.m_grid.m_sizeX = 2;
        d.m_grid.m_sizeZ = 3;
        I3D_frame *c01 = GfFrame(FRAME_MODEL, "c01"), *c20 = GfFrame(FRAME_MODEL, "c20"), *c02 = GfFrame(FRAME_MODEL, "c02");
        rows[0][1].model = c01;
        rows[2][0].model = c20;
        rows[0][2].model = c02;
        g_decoratorsEnabled = true;
        g_gfLog.clear();
        d.SetOn(true);
        WM(r.dcOn, d.m_on && GfLogIs({"on c01 1", "update c01", "on c20 1", "update c20"}));
        g_gfLog.clear();
        d.SetOn(true);
        WM(r.dcOn, d.m_on && g_gfLog.empty());
        // Not on while the option is off; off then.
        g_decoratorsEnabled = false;
        d.SetOn(false);
        WM(r.dcOn, !d.m_on && GfLogIs({"on c01 0", "update c01", "on c20 0", "update c20"}));
        g_gfLog.clear();
        d.SetOn(true);
        WM(r.dcOn, !d.m_on && g_gfLog.empty());
        // No cells: only the flag.
        d.m_grid.m_cells = nullptr;
        g_decoratorsEnabled = true;
        d.SetOn(true);
        WM(r.dcOn, d.m_on && g_gfLog.empty());

        // --- freed: the bends (their cells told), the cells' models, the cells ---
        C_Decorators f;
        f.m_grid.m_sizeX = 2;
        f.m_grid.m_sizeZ = 2;
        f.m_grid.Init();
        S_DecoratorCell *const *cs = f.m_grid.m_cells;
        I3D_frame *f01 = GfFrame(FRAME_MODEL, "f01"), *f10 = GfFrame(FRAME_MODEL, "f10");
        cs[0][1].model = f01;
        cs[1][0].model = f10;
        auto *b1 = new S_DecoratorBend(), *b2 = new S_DecoratorBend(), *b3 = new S_DecoratorBend();
        b1->cell = &cs[0][1];
        b1->springing = 1;
        b2->cell = &cs[1][1];
        b3->springing = 1;
        cs[0][1].bend = b1;
        cs[1][1].bend = b2;
        cs[0][1].bending = cs[1][1].bending = true;
        f.m_grid.m_bends = {b1, b2, b3};
        f.m_on = true;
        // (The cells go with the grid: what the bends tell them is seen on a grid without cells.)
        {
            C_DecoratorGrid probe;
            probe.m_sizeX = 1;
            S_DecoratorCell pc[2];
            pc[0].bending = pc[1].bending = true;
            auto *q1 = new S_DecoratorBend(), *q2 = new S_DecoratorBend();
            q1->cell = &pc[0];
            q1->springing = 1;
            q2->cell = &pc[1];
            pc[0].bend = q1;
            pc[1].bend = q2;
            probe.m_bends = {q1, q2};
            probe.Free();
            WM(r.dcFree, !pc[0].bend && !pc[0].bending && !pc[1].bend && pc[1].bending && probe.m_bends.empty());
        }
        g_gfLog.clear();
        f.Free();
        WM(r.dcFree, GfLogIs({"release f01", "release f10"}) && !f.m_grid.m_cells && f.m_grid.m_bends.empty() && !f.m_on &&
                         f.m_grid.m_sizeX == 2);
        // An empty grid: nothing freed.
        C_DecoratorGrid e;
        e.m_sizeX = 0;
        auto *eb = new S_DecoratorBend();
        e.m_bends = {eb};
        e.m_cells = rowPtrs;
        e.Free();
        WM(r.dcFree, e.m_bends.size() == 1 && e.m_cells == rowPtrs);
        delete eb;
        e.m_bends.clear();
        e.m_cells = nullptr;
        d.m_grid.m_cells = nullptr;
    }

    // --- ground cover ---
    {
        C_Decorators d;
        d.m_sets = {nullptr, new S_DecoratorSet()};
        d.m_sets[1]->groundCover = 0.3f;
        WM(r.dcCover, d.GetGroundCover(1) == 0.3f && d.GetGroundCover(0) == 1.0f && d.GetGroundCover(2) == 1.0f &&
                          d.GetGroundCover(static_cast<unsigned>(-1)) == 1.0f);
    }

    // --- through the GEffects module: freed, loaded and started again with the mission ---
    {
        C_GEffectsModule &m = g_geffectsModule;
        void *const oldEnum = g_sceneVtable[0x44 / 4];
        g_sceneVtable[0x44 / 4] = reinterpret_cast<void *>(&F_GfSceneEnum);
        g_gfEnumFrames.clear();
        DcFile(DC_FILE, DcChunk(9, DcI(2) + DcF(1.0f) + DcI(1) + DcSet({S_DcDecorator{"Bush", 50.0f}}, 9, 0.9f, 0.2f)));
        details.switches[DETAIL_DECORATORS] = true;
        m.RestartMission(false);
        C_Decorators &d = *g_decorators;
        WM(r.dcModule, &d == &m.m_decorators && d.m_sets.size() == 2 && d.m_pools.size() == 1 && d.m_grid.m_cells && d.m_on &&
                           d.m_grid.m_sizeX == 2 && m.GetGroundCover(1) == 0.2f);
        g_files.erase(DC_FILE);
        m.RestartMission(false);
        WM(r.dcModule, d.m_sets.empty() && d.m_pools.empty() && d.m_grid.m_cells && d.m_on);
        d.Free();
        FreeBirds();
        g_sceneVtable[0x44 / 4] = oldEnum;
    }

    details.switches[DETAIL_DECORATORS] = oldSwitch;
    g_decoratorsEnabled = false;
    std::printf("decorators: dcMake %d dcLoad %d dcVersions %d dcLoadEnd %d dcLight %d dcStart %d dcGrid %d dcOn %d dcClear %d "
                "dcFree %d dcCover %d dcModule %d\n",
                r.dcMake, r.dcLoad, r.dcVersions, r.dcLoadEnd, r.dcLight, r.dcStart, r.dcGrid, r.dcOn, r.dcClear, r.dcFree,
                r.dcCover, r.dcModule);
}
