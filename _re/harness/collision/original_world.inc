// The original's collision world (g_collisionWorld at 0x008cb480) as a copy of the port's, for calling the
// original's code that reads it (include after the game headers and original_exe.h, inside any namespace).
//
// Up to the dynamic grid the layouts agree; after it the original's vectors are 16 bytes (the port's 12): its free and
// empty cell lists (+0x3F0, +0x400) are empty, nothing is ignored (+0x410..+0x42C), by its own offsets. The grid is
// given as a shadow with MSVC6's vectors. `substitute(volume)` names what the original sees in place of each dynamic
// volume of the port's: itself, a copy in the original's layout, or nullptr to leave it out. The original must never
// add, move or remove volumes for real (its allocator was never started): redirect those to loggers
// (RedirectOriginal) or give it no grid.
struct S_MsvcCell {
    int x, z;
    uint32_t allocator;
    S_CollisionVolume **first, **last, **end;
};
const uint32_t kOrigWorld = 0x008cb480;

template <class F, class S> auto WithOriginalWorldMapped(bool grid, S substitute, F f) -> decltype(f())
{
    std::vector<S_MsvcCell> cells;
    std::vector<S_MsvcCell *> table;
    std::vector<std::vector<S_CollisionVolume *>> lists;
    S_CollisionGrid copy{};
    const S_CollisionGrid *g = grid ? g_collisionWorld.m_grid : nullptr;
    if (g) {
        copy = *g;
        const int n = g->cellsX * g->cellsZ;
        cells.resize(n);
        table.assign(n, nullptr);
        lists.resize(n);
        for (int i = 0; i < n; ++i)
            if (S_CollisionGridCell *c = g->cells[i]) {
                for (S_CollisionVolume *v : c->volumes)
                    if (S_CollisionVolume *s = substitute(v))
                        lists[i].push_back(s);
                S_CollisionVolume **data = lists[i].data();
                cells[i] = {c->x, c->z, 0, data, data + lists[i].size(), data + lists[i].size()};
                table[i] = &cells[i];
            }
        copy.cells = reinterpret_cast<S_CollisionGridCell **>(table.data());
    }
    auto *world = OriginalAt<uint8_t *>(kOrigWorld);
    std::memcpy(world, static_cast<const void *>(&g_collisionWorld), 0x3EC);
    *reinterpret_cast<const S_CollisionGrid **>(world + 0x3EC) = g ? &copy : nullptr;
    std::memset(world + 0x3F0, 0, 0x42C - 0x3F0);
    return f();
}
