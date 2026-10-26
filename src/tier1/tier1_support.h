// Support definitions for the mechanically translated (tier 1) sources.
//
// Tier-1 code is a faithful, compilable rendering of the decompiler output for
// functions that have not been hand-ported yet. It keeps raw offsets and the
// decompiler's helper operations, which are given their exact meaning here.
// Build with a 32-bit compiler (pointer == 32 bits), e.g. i686 g++ -fpermissive.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <type_traits>
#include <malloc.h>
#include <winsock2.h>
#include <windows.h>

#undef NAN

namespace tier1 {

static_assert(sizeof(void *) == 4, "tier-1 code assumes a 32-bit target");

// ---- types the decompiler uses ----------------------------------------------
typedef uint32_t code(...);          // "some function" (called through a pointer)
typedef long double float10;         // x87 80-bit temporaries
typedef int32_t int3;                // 24-bit integers (upper 3 bytes of a register)
typedef uint32_t uint3;
typedef uint32_t undefined3;
typedef uint64_t undefined5;
typedef uint64_t undefined6;
typedef uint64_t undefined7;
typedef long double unkbyte10;
typedef long double unkuint10;
typedef unsigned char uchar;          // Ghidra builtin names that appear in layouts
typedef unsigned short ushort;
typedef unsigned long ulong;
typedef unsigned long long qword;

// ---- byte-piece access: x._off_size_ -----------------------------------------
struct uint24 {
    uint8_t b[3];
    operator uint32_t() const { return b[0] | (b[1] << 8) | (b[2] << 16); }
    uint24 &operator=(uint32_t v) { b[0] = uint8_t(v); b[1] = uint8_t(v >> 8); b[2] = uint8_t(v >> 16); return *this; }
};
template <int N> struct PieceType;
template <> struct PieceType<1> { typedef uint8_t type; };
template <> struct PieceType<2> { typedef uint16_t type; };
template <> struct PieceType<3> { typedef uint24 type; };
template <> struct PieceType<4> { typedef uint32_t type; };
template <> struct PieceType<8> { typedef uint64_t type; };
#define GH_PIECE(x, off, size) \
    (*reinterpret_cast<typename ::tier1::PieceType<size>::type *>(reinterpret_cast<uint8_t *>(&(x)) + (off)))

// ---- CONCATxy(a, b): a placed above y bytes of b ------------------------------
template <int Y, typename A, typename B>
inline uint64_t gh_concat(A a, B b)
{
    const uint64_t mask = Y >= 8 ? ~0ull : ((1ull << (Y * 8)) - 1);
    return (static_cast<uint64_t>(static_cast<uint64_t>(a)) << (Y * 8)) | (static_cast<uint64_t>(b) & mask);
}
#define CONCAT11(a, b) static_cast<uint16_t>(::tier1::gh_concat<1>(a, b))
#define CONCAT12(a, b) static_cast<uint32_t>(::tier1::gh_concat<2>(a, b))
#define CONCAT13(a, b) static_cast<uint32_t>(::tier1::gh_concat<3>(a, b))
#define CONCAT14(a, b) ::tier1::gh_concat<4>(a, b)
#define CONCAT21(a, b) static_cast<uint32_t>(::tier1::gh_concat<1>(a, b))
#define CONCAT22(a, b) static_cast<uint32_t>(::tier1::gh_concat<2>(a, b))
#define CONCAT24(a, b) ::tier1::gh_concat<4>(a, b)
#define CONCAT31(a, b) static_cast<uint32_t>(::tier1::gh_concat<1>(a, b))
#define CONCAT32(a, b) ::tier1::gh_concat<2>(a, b)
#define CONCAT44(a, b) ::tier1::gh_concat<4>(a, b)
#define CONCAT26(a, b) ::tier1::gh_concat<6>(a, b)
#define CONCAT35(a, b) ::tier1::gh_concat<5>(a, b)
#define CONCAT53(a, b) ::tier1::gh_concat<3>(a, b)
#define CONCAT62(a, b) ::tier1::gh_concat<2>(a, b)
#define CONCAT71(a, b) ::tier1::gh_concat<1>(a, b)

// ---- SUBxy(v, off): y bytes of v starting at byte off --------------------------
template <typename T>
inline uint64_t gh_bits(T v)
{
    uint64_t r = 0;
    std::memcpy(&r, &v, sizeof(T) < 8 ? sizeof(T) : 8);
    return r;
}
#define GH_SUB(v, off, n) static_cast<uint64_t>((::tier1::gh_bits(v) >> ((off) * 8)) & ((n) >= 8 ? ~0ull : ((1ull << ((n) * 8)) - 1)))
#define SUB21(v, off) static_cast<uint8_t>(GH_SUB(v, off, 1))
#define SUB41(v, off) static_cast<uint8_t>(GH_SUB(v, off, 1))
#define SUB42(v, off) static_cast<uint16_t>(GH_SUB(v, off, 2))
#define SUB81(v, off) static_cast<uint8_t>(GH_SUB(v, off, 1))
#define SUB82(v, off) static_cast<uint16_t>(GH_SUB(v, off, 2))
#define SUB84(v, off) static_cast<uint32_t>(GH_SUB(v, off, 4))
#define SUB101(v, off) static_cast<uint8_t>(GH_SUB(static_cast<double>(v), off, 1))
#define SUB104(v, off) static_cast<uint32_t>(GH_SUB(static_cast<double>(v), off, 4))

#define ZEXT14(v) static_cast<uint32_t>(static_cast<uint8_t>(v))
#define ZEXT24(v) static_cast<uint32_t>(static_cast<uint16_t>(v))
#define ZEXT48(v) static_cast<uint64_t>(static_cast<uint32_t>(v))
#define ZEXT816(v) static_cast<uint64_t>(v)
#define SEXT14(v) static_cast<int32_t>(static_cast<int8_t>(v))
#define SEXT24(v) static_cast<int32_t>(static_cast<int16_t>(v))
#define SEXT48(v) static_cast<int64_t>(static_cast<int32_t>(v))
#define SEXT816(v) static_cast<int64_t>(v)

// ---- flags -------------------------------------------------------------------
template <typename A, typename B> inline bool CARRY1(A a, B b) { return uint8_t(uint8_t(a) + uint8_t(b)) < uint8_t(a); }
template <typename A, typename B> inline bool CARRY2(A a, B b) { return uint16_t(uint16_t(a) + uint16_t(b)) < uint16_t(a); }
template <typename A, typename B> inline bool CARRY4(A a, B b) { return uint32_t(uint32_t(a) + uint32_t(b)) < uint32_t(a); }
template <typename A, typename B> inline bool SCARRY4(A a, B b)
{
    const int32_t x = int32_t(a), y = int32_t(b), r = int32_t(uint32_t(x) + uint32_t(y));
    return (x >= 0) == (y >= 0) && (r >= 0) != (x >= 0);
}
template <typename A, typename B> inline bool SBORROW4(A a, B b)
{
    const int32_t x = int32_t(a), y = int32_t(b), r = int32_t(uint32_t(x) - uint32_t(y));
    return (x >= 0) != (y >= 0) && (r >= 0) != (x >= 0);
}
template <typename A, typename B> inline bool SCARRY1(A a, B b)
{
    const int8_t x = int8_t(a), y = int8_t(b), r = int8_t(uint8_t(x) + uint8_t(y));
    return (x >= 0) == (y >= 0) && (r >= 0) != (x >= 0);
}
template <typename A, typename B> inline bool SBORROW1(A a, B b) { return SBORROW4(int8_t(a), int8_t(b)); }
template <typename A, typename B> inline bool SBORROW2(A a, B b) { return SBORROW4(int16_t(a), int16_t(b)); }
template <typename T> inline int POPCOUNT(T v)
{
    uint64_t x = gh_bits(v);
    int n = 0;
    for (; x; x &= x - 1) ++n;
    return n;
}

// ---- floating point ----------------------------------------------------------
template <typename T> inline T ABS(T v) { return v < 0 ? -v : v; }
inline long double SQRT(long double v) { return std::sqrt(v); }
template <typename T> inline bool NAN(T v) { return std::isnan(static_cast<long double>(v)); }
// NaN literal (the decompiler prints a NaN constant as "NAN" as well).
constexpr float gh_nan = __builtin_nanf("");
inline long double ROUND(long double v) { return std::nearbyint(v); }
inline int64_t TRUNC(long double v) { return static_cast<int64_t>(v); }
inline long double fsin(long double v) { return std::sin(v); }
inline long double fcos(long double v) { return std::cos(v); }
inline long double fpatan(long double y, long double x) { return std::atan2(y, x); }
inline long double fptan(long double v) { return std::tan(v); }
inline long double f2xm1(long double v) { return std::exp2(v) - 1; }
inline long double fyl2x(long double x, long double y) { return y * std::log2(x); }
inline long double fscale(long double v, long double s) { return std::ldexp(v, int(s)); }

// ---- registers / environment the decompiler could not resolve ---------------
extern void *ExceptionList;
extern uint32_t in_FS_OFFSET;
[[noreturn]] inline void halt_baddata() { std::abort(); }
[[noreturn]] inline void halt_unimplemented() { std::abort(); }
inline void swi(int) { std::abort(); }

// ---- reinterpretation of a register value as another type ---------------------
// The decompiler sometimes reuses one variable for differently typed values
// (e.g. a float kept in a pointer-typed variable); this keeps the exact bits.
template <typename To, typename From>
inline To gh_bitcast(const From &v)
{
    To out{};
    std::memcpy(&out, &v, sizeof(To) < sizeof(From) ? sizeof(To) : sizeof(From));
    return out;
}

// Switch on a variable the decompiler typed as float or pointer: the original
// switched on the integer bits (case labels are those bits).
template <typename T>
inline int32_t gh_switch(const T &v)
{
    if constexpr (std::is_floating_point<T>::value) {
        const float f = static_cast<float>(v);
        return gh_bitcast<int32_t>(f);
    } else if constexpr (std::is_pointer<T>::value) {
        return static_cast<int32_t>(reinterpret_cast<uintptr_t>(v));
    } else {
        return static_cast<int32_t>(v);
    }
}

// ---- compiler built-ins the decompiler recognised (fixed-size copies)
inline void builtin_strncpy(void *dst, const void *src, size_t n) { std::memcpy(dst, src, n); }
inline void builtin_memcpy(void *dst, const void *src, size_t n) { std::memcpy(dst, src, n); }

// ---- C library adapters (the decompiler passes code addresses as data pointers)
inline int atexit(const void *fn) { return ::atexit(reinterpret_cast<void (*)()>(const_cast<void *>(fn))); }

// ---- runtime helpers of the original binary ----------------------------------
void *tier1_operator_new(uint32_t size);
void tier1_operator_delete(void *p);
int64_t tier1_ftol(...);
int64_t tier1_allmul(...);
uint64_t tier1_aulldiv(...);
int64_t tier1_allrem(...);
uint64_t tier1_aullrem(...);
int64_t tier1_allshl(...);
void tier1_chkstk(...);
[[noreturn]] void tier1_throw(...);
long double _CIasin(...);
long double _CIacos(...);
long double _CIpow(...);
void std_Xlen(...);
void std_Xran(...);
void std_Lockit_ctor(...);
void std_Lockit_dtor(...);
void __EH_prolog(...);
void eh_vector_constructor_iterator(void *array, unsigned int elementSize, int count, void *ctor, void *dtor);
void eh_vector_destructor_iterator(void *array, unsigned int elementSize, int count, void *dtor);

} // namespace tier1
