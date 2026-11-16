// MSVC6 containers in the original's copies of objects (included inside the harness's anonymous namespace by
// senses_test.inc and sniper_test.inc; random choices through the harness's P and I).
// ---- MSVC6 containers in the original's copies
// A vector (allocator word, first, last, end) at `at` from `items` (a buffer of its own: the original may grow and
// free it).
void MsvcVectorPut(uint8_t *at, const std::vector<uint32_t> &items, size_t spare = 0)
{
    uint32_t *buffer = nullptr;
    if (!items.empty() || spare) {
        buffer = static_cast<uint32_t *>(std::malloc((items.size() + spare) * 4));
        for (size_t i = 0; i < items.size(); ++i)
            buffer[i] = items[i];
    }
    void **w = reinterpret_cast<void **>(at);
    w[0] = nullptr;
    w[1] = buffer;
    w[2] = buffer + items.size();
    w[3] = buffer + items.size() + spare;
}
std::vector<uint32_t> MsvcVectorGet(const uint8_t *at)
{
    const auto *w = reinterpret_cast<uint32_t *const *>(at);
    if (!w[1])
        return {};
    return std::vector<uint32_t>(w[1], w[2]);
}
// A deque of pointers (0x30 bytes: allocator; the first iterator: block, block end, element, map cell; the last
// iterator; map; map size; size): blocks of 1024. Empty: all zero (a new deque). The first element anywhere in its
// block, sometimes near its end; the last iterator never at a block's end (the original's invariant: a push that fills
// a block adds the next one).
const int DEQUE_BLOCK = 1024;
void MsvcDequePut(uint8_t *at, const std::vector<uint32_t> &items)
{
    std::memset(at, 0, 0x30);
    if (items.empty())
        return;
    const int start = P(0.3f) ? DEQUE_BLOCK - I(1, 4) : I(0, DEQUE_BLOCK - 1);
    const size_t end = start + items.size();
    const size_t blocks = end / DEQUE_BLOCK + 1;
    const size_t first = size_t(I(0, 2));
    const size_t mapSize = first + blocks + size_t(I(0, 2));
    auto **map = static_cast<uint32_t **>(std::calloc(mapSize, 4));
    for (size_t b = 0; b < blocks; ++b)
        map[first + b] = static_cast<uint32_t *>(std::malloc(DEQUE_BLOCK * 4));
    for (size_t i = 0; i < items.size(); ++i) {
        const size_t pos = start + i;
        map[first + pos / DEQUE_BLOCK][pos % DEQUE_BLOCK] = items[i];
    }
    uint32_t *w = reinterpret_cast<uint32_t *>(at);
    const auto put = [&](int word, size_t pos) {
        uint32_t *block = map[first + pos / DEQUE_BLOCK];
        w[word] = uint32_t(uintptr_t(block));
        w[word + 1] = uint32_t(uintptr_t(block + DEQUE_BLOCK));
        w[word + 2] = uint32_t(uintptr_t(block + pos % DEQUE_BLOCK));
        w[word + 3] = uint32_t(uintptr_t(&map[first + pos / DEQUE_BLOCK]));
    };
    put(1, size_t(start));
    put(5, end);
    w[9] = uint32_t(uintptr_t(map));
    w[10] = uint32_t(mapSize);
    w[11] = uint32_t(items.size());
}
std::vector<uint32_t> MsvcDequeGet(const uint8_t *at)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(at);
    std::vector<uint32_t> items;
    const uint32_t size = w[11];
    if (!size)
        return items;
    const auto *cur = reinterpret_cast<const uint32_t *>(uintptr_t(w[3]));
    const auto *blockEnd = reinterpret_cast<const uint32_t *>(uintptr_t(w[2]));
    auto *const *cell = reinterpret_cast<uint32_t *const *>(uintptr_t(w[4]));
    for (uint32_t i = 0; i < size; ++i) {
        if (cur == blockEnd) {
            ++cell;
            cur = *cell;
            blockEnd = cur + DEQUE_BLOCK;
        }
        items.push_back(*cur++);
    }
    // (The last iterator where the size says.)
    if (cur == blockEnd) {
        ++cell;
        cur = *cell;
    }
    if (uint32_t(uintptr_t(cur)) != w[7])
        items.push_back(0xDEADDEAD);
    return items;
}
