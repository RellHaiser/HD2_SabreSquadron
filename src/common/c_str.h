// C_str — reference-counted, copy-on-write string from the Insanity/LS3D SDK.
// C_fstr — C_str built from a printf-style format.
//
// Representation: a single pointer to a heap block { refCount, length, chars[] };
// a null pointer is the empty string.
#pragma once

#include <cstddef>
#include <cstring>

class C_str {
public:
    C_str() = default;
    C_str(const char *text) { Assign(text, text ? std::strlen(text) : 0); }
    C_str(const char *text, std::size_t length) { Assign(text, length); }
    C_str(const C_str &other) : m_rep(other.m_rep) { AddRef(); }   // 0x004145b0
    C_str(C_str &&other) noexcept : m_rep(other.m_rep) { other.m_rep = nullptr; }
    ~C_str() { Release(); }                                  // 0x00405160

    C_str &operator=(const char *text);                      // 0x00405180
    C_str &operator=(const C_str &other);                    // 0x004145d0
    C_str &operator=(C_str &&other) noexcept;

    const char *c_str() const { return m_rep ? m_rep->chars : ""; }
    operator const char *() const { return c_str(); }
    std::size_t Size() const { return m_rep ? m_rep->length : 0; }
    bool IsEmpty() const { return Size() == 0; }

    char operator[](std::size_t i) const { return c_str()[i]; }
    // Mutable access detaches a shared buffer first (copy-on-write).
    char &operator[](std::size_t i) { MakeUnique(); return m_rep->chars[i]; }   // 0x0041d670

    bool operator==(const char *text) const { return std::strcmp(c_str(), text ? text : "") == 0; }
    bool operator!=(const char *text) const { return !(*this == text); }
    bool operator==(const C_str &other) const { return m_rep == other.m_rep || *this == other.c_str(); }
    bool operator!=(const C_str &other) const { return !(*this == other); }
    // Appends (builds a new buffer, like the SDK's operator+, whose buffer setup is 0x00414550).
    C_str &operator+=(const C_str &other);
    C_str &operator+=(const char *text);

    // Case-insensitive comparison.
    bool EqualsNoCase(const char *text) const;

    void ToLower();
    void ToUpper();

protected:
    struct Rep {
        int refCount;
        std::size_t length;
        char chars[1];
    };

    void Assign(const char *text, std::size_t length);
    void AddRef() { if (m_rep) ++m_rep->refCount; }
    void Release();
    void MakeUnique();
    static Rep *CreateRep(const char *text, std::size_t length);   // 0x004050a0

    Rep *m_rep = nullptr;
};

class C_fstr : public C_str {
public:
    explicit C_fstr(const char *format, ...);                // 0x00405100
};
