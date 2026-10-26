#include "common/c_str.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>

C_str::Rep *C_str::CreateRep(const char *text, std::size_t length)
{
    Rep *rep = static_cast<Rep *>(::operator new(offsetof(Rep, chars) + length + 1));
    rep->refCount = 1;
    rep->length = length;
    std::memcpy(rep->chars, text, length);
    rep->chars[length] = '\0';
    return rep;
}

void C_str::Release()
{
    if (m_rep && --m_rep->refCount == 0)                     // 0x004050e0
        ::operator delete(m_rep);
    m_rep = nullptr;
}

void C_str::Assign(const char *text, std::size_t length)
{
    Rep *rep = text ? CreateRep(text, length) : nullptr;
    Release();
    m_rep = rep;
}

C_str &C_str::operator=(const char *text)
{
    Assign(text, text ? std::strlen(text) : 0);
    return *this;
}

C_str &C_str::operator=(const C_str &other)
{
    if (m_rep != other.m_rep) {
        Rep *rep = other.m_rep;
        if (rep)
            ++rep->refCount;
        Release();
        m_rep = rep;
    }
    return *this;
}

C_str &C_str::operator=(C_str &&other) noexcept
{
    if (this != &other) {
        Release();
        m_rep = other.m_rep;
        other.m_rep = nullptr;
    }
    return *this;
}

C_str &C_str::operator+=(const char *text)
{
    const std::size_t extra = text ? std::strlen(text) : 0;
    if (!m_rep)
        return *this = text;
    // 0x00414550: new block holding both parts
    Rep *rep = static_cast<Rep *>(::operator new(offsetof(Rep, chars) + m_rep->length + extra + 1));
    rep->refCount = 1;
    rep->length = m_rep->length + extra;
    std::memcpy(rep->chars, m_rep->chars, m_rep->length);
    std::memcpy(rep->chars + m_rep->length, text, extra);
    rep->chars[rep->length] = '\0';
    Release();
    m_rep = rep;
    return *this;
}

C_str &C_str::operator+=(const C_str &other)
{
    if (!m_rep)
        return *this = other;
    const C_str keep(other);                                 // `other` may be *this
    return *this += keep.c_str();
}

void C_str::MakeUnique()
{
    if (!m_rep)
        m_rep = CreateRep("", 0);
    else if (m_rep->refCount != 1) {
        Rep *copy = CreateRep(m_rep->chars, m_rep->length);
        Release();
        m_rep = copy;
    }
}

bool C_str::EqualsNoCase(const char *text) const
{
    const char *a = c_str();
    const char *b = text ? text : "";
    while (*a && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) {
        ++a;
        ++b;
    }
    return std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b));
}

void C_str::ToLower()
{
    if (!m_rep)
        return;
    MakeUnique();
    for (std::size_t i = 0; i < m_rep->length; ++i)
        m_rep->chars[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(m_rep->chars[i])));
}

void C_str::ToUpper()
{
    if (!m_rep)
        return;
    MakeUnique();
    for (std::size_t i = 0; i < m_rep->length; ++i)
        m_rep->chars[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(m_rep->chars[i])));
}

C_fstr::C_fstr(const char *format, ...)
{
    if (!format)
        return;

    // The original formats into a fixed 2 KB stack buffer.
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    C_str::operator=(buffer);
}
