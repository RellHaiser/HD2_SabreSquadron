// Pool of reusable objects (one template instantiated per type in the original: the shot
// manager's bullet pool, vtable 0x0080fa5c, and shell pool, 0x0080fa58; the AI's event pool,
// 0x00811970). 0x14 bytes in the original.
#pragma once

#include <vector>

// Free objects for reuse; when it runs empty a block of `growBy` new ones is made.
template <class T>
class C_ObjectPool {
public:
    explicit C_ObjectPool(int growBy) : m_growBy(growBy) {}     // (0x005ce2f0 (DS 0x005e6a80), which also fills it)
    // The other instances' destructors (vtables 0x00810f74..0x00810fb0, 0x0080fa5c, 0x008120c0 / c4): 0x00589a90 (DS 0x00591770),
    // 0x00589ba0 (DS 0x005918d0), 0x00589c20 (DS 0x00591970), 0x00589ca0 (DS 0x00591a10), 0x00589d30 (DS 0x00591ad0), 0x00589dc0 (DS 0x00591b90),
    // 0x00589e50 (DS 0x00591c50), 0x00589ee0 (DS 0x00591d10), 0x00589f70 (DS 0x00591dd0), 0x0058a000 (DS 0x00591e90), 0x0058a090 (DS 0x00591f50),
    // 0x0058a120 (DS 0x00592010), 0x0058a1b0 (DS 0x005920d0), 0x0058a240 (DS 0x00592190), 0x0058a2d0 (DS 0x00592250), 0x005892a0 (DS 0x00590d40),
    // 0x00453020 (DS 0x0046d560), 0x005cee20 (DS 0x005e78d0), 0x005cee90 (DS 0x005e7960), 0x005cef10 (DS 0x005e7a00)
    ~C_ObjectPool() { Clear(); }                        // (0x00589b10 (DS 0x00591810), 0x005ce400 (DS 0x005e6be0))
    C_ObjectPool(const C_ObjectPool &) = delete;
    C_ObjectPool &operator=(const C_ObjectPool &) = delete;

    T *Get()
    {
        if (m_free.empty())
            Grow();
        T *object = m_free.back();
        m_free.pop_back();
        return object;
    }
    void Put(T *object) { m_free.push_back(object); }   // (the list's spare cells, 0x00533de0 (DS 0x00525990), 0x0058b3f0 (DS 0x005939b0), 0x0058b420 (DS 0x005939f0), 0x005cb000 (DS 0x005e2860), 0x005ce250 (DS 0x005e6970), 0x00453460 (DS 0x0046d700), are the vector here)
    // Makes `growBy` new objects.
    void Grow()                                         // (0x0058a360 (DS 0x00592310), 0x0058a3f0 (DS 0x005923d0), 0x005cef90 (DS 0x005e7aa0), 0x005cf000 (DS 0x005e7b30))
    {
        for (int i = 0; i < m_growBy; ++i)
            m_free.push_back(new T);
    }
    // Deletes the free objects.
    void Clear()
    {
        for (T *object : m_free)
            delete object;
        m_free.clear();
    }

private:
    std::vector<T *> m_free;                                // +0x04 (a linked list in the original)
    int m_growBy;                                           // +0x0C
};
