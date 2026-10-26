// Pool of reusable objects (one template instantiated per type in the original: the shot
// manager's bullet pool, vtable 0x0080fa5c, and shell pool, 0x0080fa58; the AI's event pool,
// 0x00811970). 0x14 bytes in the original.
#pragma once

#include <vector>

// Free objects for reuse; when it runs empty a block of `growBy` new ones is made.
template <class T>
class C_ObjectPool {
public:
    explicit C_ObjectPool(int growBy) : m_growBy(growBy) {}
    ~C_ObjectPool() { Clear(); }
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
    void Put(T *object) { m_free.push_back(object); }
    // Makes `growBy` new objects.
    void Grow()
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
