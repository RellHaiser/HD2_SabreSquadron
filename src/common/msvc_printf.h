// printf as the game's MSVC6 CRT does it (statically linked: _vsnprintf 0x007e5ad2, its _output 0x007ebcaa). The
// float conversions (%e %E %f %g %G) differ from today's C libraries and are shown to the player (weights "%3.1f", the
// equipment screen's stats "%3.0f", the scores, the upgrade bar's values), so the port makes them as the original's:
// the value's 17 significant digits (zeros after them), rounded at the precision by the next digit being '5' or more
// (12.5 -> "13", 0.125 -> "0.13"; not to even), a zero without its sign, three exponent digits ("1e+030"), and the
// infinities and NaNs as the digits "1#INF", "1#QNAN", "1#IND" rounded the same way ("1.#INF00", "1.$"), a '#' point
// on %g too. An unknown conversion is its character alone (MSVC6's). The others are the C library's, one conversion at a
// time (%lld and %zu as C has them: MSVC6 knows neither and the original uses neither; %I32d is MSVC6's "I32d").
#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// (Header only: c_str.cpp's C_fstr uses it and every build compiling c_str.cpp has it so.)
// VsnPrintf: vsnprintf's contract (the whole length returned, `buffer` always ended when `size` > 0) with the
// original's float conversions; SnPrintf the same with its arguments.
namespace msvc6 {
namespace detail {

// _fltout's result: the sign, the decimal point's place (the value 0.d1d2... times 10 to decpt) and the digits; `tie`:
// the value's exact digits past the 17th a 5 and zeros (the original's 80-bit digits go either way there, the port's
// up: the 17th digit only, shown by no format of the game's).
struct S_Flt {
    bool negative = false;
    int decpt = 0;
    std::string mantissa;
    bool tie = false;
};

// A positive finite double's exact decimal digits (integer arithmetic only: the C library's conversions are wrong under
// the game's 24-bit x87 precision) and the place of its point, the value 0.d1d2... times 10 to `decpt`.
inline std::string ExactDigits(uint64_t bits, int &decpt)
{
    int exponent = int((bits >> 52) & 0x7FF);
    uint64_t mantissa = bits & ((uint64_t(1) << 52) - 1);
    if (exponent == 0)
        exponent = 1;
    else
        mantissa |= uint64_t(1) << 52;
    exponent -= 1075;   // the value: mantissa times 2 to exponent
    const uint32_t BASE = 1000000000;
    uint32_t n[130] = {};   // base 1e9, the least first (5 to 1074 times 2 to 53: under 90 limbs)
    int used = 0;
    for (uint64_t m = mantissa; m; m /= BASE)
        n[used++] = uint32_t(m % BASE);
    const auto times = [&](uint32_t k) {
        uint64_t carry = 0;
        for (int i = 0; i < used; ++i) {
            const uint64_t t = uint64_t(n[i]) * k + carry;
            n[i] = uint32_t(t % BASE);
            carry = t / BASE;
        }
        while (carry) {
            n[used++] = uint32_t(carry % BASE);
            carry /= BASE;
        }
    };
    int shift = 0;   // (the number is n times 10 to -shift)
    if (exponent > 0) {
        for (int e = exponent; e > 0; e -= 29)
            times(uint32_t(1) << std::min(e, 29));
    } else if (exponent < 0) {
        shift = -exponent;
        for (int e = -exponent; e > 0; e -= 13) {
            uint32_t five = 1;
            for (int k = std::min(e, 13); k > 0; --k)
                five *= 5;
            times(five);
        }
    }
    std::string digits = std::to_string(n[used - 1]);
    for (int i = used - 2; i >= 0; --i) {
        char part[16];
        std::snprintf(part, sizeof part, "%09u", n[i]);
        digits += part;
    }
    decpt = int(digits.size()) - shift;
    return digits;
}

// The value's 17 significant digits ($I10_OUTPUT's; rounded up from a 5); a zero is "0" without its sign; the
// infinities and NaNs their names as digits after a 1 (the point after it).
inline S_Flt FltOut(double value)
{
    S_Flt f;
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    const bool sign = (bits >> 63) != 0;
    const uint64_t exponent = (bits >> 52) & 0x7FF, fraction = bits & ((uint64_t(1) << 52) - 1);
    if (exponent == 0x7FF) {
        f.negative = sign;
        f.decpt = 1;
        // (A signalling NaN quieted on its way through the x87: a quiet one's name.)
        if (fraction == 0)
            f.mantissa = "1#INF";
        else if (sign && fraction == (uint64_t(1) << 51))
            f.mantissa = "1#IND";   // (the indefinite: what arithmetic makes)
        else
            f.mantissa = "1#QNAN";
        return f;
    }
    if (value == 0.0) {
        f.mantissa = "0";
        return f;
    }
    f.negative = sign;
    std::string digits = ExactDigits(bits & ~(uint64_t(1) << 63), f.decpt);
    if (digits.size() > 17) {
        const size_t rest = digits.find_first_not_of('0', 18);
        f.tie = digits[17] == '5' && rest == std::string::npos;
        const bool up = digits[17] >= '5';
        digits.resize(17);
        if (up) {
            size_t at = 17;
            while (at > 0 && digits[at - 1] == '9')
                digits[--at] = '0';
            if (at == 0) {
                digits.insert(digits.begin(), '1');
                digits.resize(17);
                ++f.decpt;
            } else {
                ++digits[at - 1];
            }
        }
    }
    const size_t last = digits.find_last_not_of('0');
    digits.resize(last + 1);
    f.mantissa = digits;
    return f;
}

// _fptostr: `digits` digits of the mantissa (zeros after it), rounded up when the next is '5' or more (carried through
// the 9s; a carry out of the first: one digit more, the point one further). Fewer than none: none, not rounded.
inline std::string FpToStr(S_Flt &f, int digits)
{
    std::string buffer = "0";
    size_t m = 0;
    for (int k = 0; k < digits; ++k)
        buffer.push_back(m < f.mantissa.size() ? f.mantissa[m++] : '0');
    if (digits >= 0 && m < f.mantissa.size() && f.mantissa[m] >= '5') {
        size_t at = buffer.size() - 1;
        while (buffer[at] == '9')
            buffer[at--] = '0';
        ++buffer[at];
    }
    if (buffer[0] == '1')
        ++f.decpt;
    else
        buffer.erase(0, 1);
    return buffer;
}

// The digits laid out with `ndec` decimals and the point at decpt (_cftof2; the zeros before the digits as many as
// fit: for %g all of them, its decimals its digits and those zeros).
inline std::string LayoutF(const S_Flt &f, const std::string &digits, int ndec)
{
    std::string out = f.negative ? "-" : "";
    size_t d = 0;
    if (f.decpt <= 0)
        out.push_back('0');
    else
        for (int k = 0; k < f.decpt; ++k)
            out.push_back(d < digits.size() ? digits[d++] : '0');
    if (ndec > 0) {
        out.push_back('.');
        const int zeros = f.decpt >= 0 ? 0 : std::min(-f.decpt, ndec);
        out.append(size_t(zeros), '0');
        for (int k = zeros; k < ndec; ++k)
            out.push_back(d < digits.size() ? digits[d++] : '0');
    }
    return out;
}

// The digits as d.ddd and an exponent of three digits at least (_cftoe2; a zero's exponent 0).
inline std::string LayoutE(const S_Flt &f, const std::string &digits, int ndec, bool caps)
{
    std::string out = f.negative ? "-" : "";
    out.push_back(digits.empty() ? '0' : digits[0]);
    if (ndec > 0) {
        out.push_back('.');
        for (int k = 1; k <= ndec; ++k)
            out.push_back(size_t(k) < digits.size() ? digits[size_t(k)] : '0');
    }
    int exponent = f.mantissa[0] != '0' ? f.decpt - 1 : 0;
    out.push_back(caps ? 'E' : 'e');
    out.push_back(exponent < 0 ? '-' : '+');
    if (exponent < 0)
        exponent = -exponent;
    char text[16];
    std::snprintf(text, sizeof text, "%03d", exponent);
    out += text;
    return out;
}

inline std::string CftoE(double value, int ndec, bool caps)
{
    S_Flt f = FltOut(value);
    const std::string digits = FpToStr(f, ndec + 1);
    return LayoutE(f, digits, ndec, caps);
}

inline std::string CftoF(double value, int ndec)
{
    S_Flt f = FltOut(value);
    const std::string digits = FpToStr(f, ndec + f.decpt);
    return LayoutF(f, digits, ndec);
}

// %g: rounded to `ndec` significant digits; e-style when the exponent (after rounding) is below -4 or not below them.
inline std::string CftoG(double value, int ndec, bool caps)
{
    S_Flt f = FltOut(value);
    // (A carry's extra digit, a zero, past the laid out ones.)
    const std::string digits = FpToStr(f, ndec);
    const int magnitude = f.decpt - 1;
    if (magnitude < -4 || magnitude >= ndec)
        return LayoutE(f, digits, ndec - 1, caps);
    return LayoutF(f, digits, ndec - f.decpt);
}

// _forcdecpt: a point after the first digits (before an exponent) when there is none.
inline void ForceDecimalPoint(std::string &text)
{
    if (text.find('.') != std::string::npos)
        return;
    size_t at = 0;
    while (at < text.size() && text[at] != 'e' && text[at] != 'E')
        ++at;
    text.insert(at, 1, '.');
}

// _cropzeros: the zeros at the end of the decimals (before an exponent) away, and a point left alone.
inline void CropZeros(std::string &text)
{
    const size_t point = text.find('.');
    if (point == std::string::npos)
        return;
    size_t stop = point + 1;
    while (stop < text.size() && text[stop] != 'e' && text[stop] != 'E')
        ++stop;
    size_t end = stop;
    while (end > point + 1 && text[end - 1] == '0')
        --end;
    if (end == point + 1)
        end = point;
    text.erase(end, stop - end);
}

} // namespace detail

inline int VsnPrintf(char *buffer, std::size_t size, const char *format, va_list args)
{
    va_list ap;
    va_copy(ap, args);
    std::string out;
    char piece[512];
    const char *p = format;
    while (*p) {
        if (*p != '%') {
            out.push_back(*p++);
            continue;
        }
        ++p;
        if (*p == '%') {
            out.push_back('%');
            ++p;
            continue;
        }
        // Flags, width, precision, length, conversion.
        bool left = false, plus = false, space = false, alternate = false, zero = false;
        for (;; ++p) {
            if (*p == '-')
                left = true;
            else if (*p == '+')
                plus = true;
            else if (*p == ' ')
                space = true;
            else if (*p == '#')
                alternate = true;
            else if (*p == '0')
                zero = true;
            else
                break;
        }
        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = true;
                width = -width;
            }
            ++p;
        } else {
            while (*p >= '0' && *p <= '9')
                width = width * 10 + (*p++ - '0');
        }
        int precision = -1;
        if (*p == '.') {
            ++p;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(ap, int);
                ++p;
            } else {
                while (*p >= '0' && *p <= '9')
                    precision = precision * 10 + (*p++ - '0');
            }
        }
        std::string length;
        bool wide64 = false;
        for (;;) {
            if (p[0] == 'I' && p[1] == '6' && p[2] == '4') {
                wide64 = true;
                p += 3;
            } else if (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'w' || *p == 'z' || *p == 'j' || *p == 't') {
                length.push_back(*p++);
            } else {
                break;
            }
        }
        if (length == "ll")
            wide64 = true;
        const char conversion = *p;
        if (!conversion)
            break;
        ++p;
        if (conversion == 'e' || conversion == 'E' || conversion == 'f' || conversion == 'g' || conversion == 'G') {
            const double value = va_arg(ap, double);
            if (precision < 0)
                precision = 6;
            else if (precision == 0 && (conversion == 'g' || conversion == 'G'))
                precision = 1;
            std::string text = conversion == 'f'   ? detail::CftoF(value, precision)
                               : conversion == 'g' || conversion == 'G' ? detail::CftoG(value, precision, conversion == 'G')
                                                                         : detail::CftoE(value, precision, conversion == 'E');
            // (# : a point always, %g's too.)
            if (alternate && (precision == 0 || conversion == 'g' || conversion == 'G'))
                detail::ForceDecimalPoint(text);
            if ((conversion == 'g' || conversion == 'G') && !alternate)
                detail::CropZeros(text);
            std::string prefix;
            if (text[0] == '-') {
                prefix = "-";
                text.erase(0, 1);
            } else if (plus) {
                prefix = "+";
            } else if (space) {
                prefix = " ";
            }
            const int padding = width - int(text.size() + prefix.size());
            if (padding > 0 && !left && !zero)
                out.append(size_t(padding), ' ');
            out += prefix;
            if (padding > 0 && !left && zero)
                out.append(size_t(padding), '0');
            out += text;
            if (padding > 0 && left)
                out.append(size_t(padding), ' ');
            continue;
        }
        // The others the C library's: the conversion alone with its flags, width and precision.
        std::string spec = "%";
        spec += left ? "-" : "";
        spec += plus ? "+" : "";
        spec += space ? " " : "";
        spec += alternate ? "#" : "";
        spec += zero ? "0" : "";
        if (width)
            spec += std::to_string(width);
        if (precision >= 0)
            spec += "." + std::to_string(precision);
        int made = 0;
        switch (conversion) {
        case 'd':
        case 'i':
            spec += wide64 ? "ll" : length == "h" ? "h" : "";
            spec.push_back(conversion);
            made = wide64 ? std::snprintf(piece, sizeof piece, spec.c_str(), va_arg(ap, long long))
                          : std::snprintf(piece, sizeof piece, spec.c_str(), va_arg(ap, int));
            break;
        case 'o':
        case 'u':
        case 'x':
        case 'X':
            spec += wide64 ? "ll" : length == "h" ? "h" : "";
            spec.push_back(conversion);
            made = wide64 ? std::snprintf(piece, sizeof piece, spec.c_str(), va_arg(ap, unsigned long long))
                          : std::snprintf(piece, sizeof piece, spec.c_str(), va_arg(ap, unsigned));
            break;
        case 'c':
            spec.push_back('c');
            made = std::snprintf(piece, sizeof piece, spec.c_str(), va_arg(ap, int));
            break;
        case 's': {
            const char *text = va_arg(ap, const char *);
            spec.push_back('s');
            // (A null: MSVC6's "(null)".)
            const std::string s = text ? text : "(null)";
            const int need = std::snprintf(nullptr, 0, spec.c_str(), s.c_str());
            std::string big(size_t(need) + 1, '\0');
            std::snprintf(&big[0], big.size(), spec.c_str(), s.c_str());
            big.resize(size_t(need));
            out += big;
            continue;
        }
        case 'p':
            // (MSVC6's: eight upper-case hex digits.)
            made = std::snprintf(piece, sizeof piece, "%08X", unsigned(reinterpret_cast<uintptr_t>(va_arg(ap, void *))));
            break;
        case 'n':
            *va_arg(ap, int *) = int(out.size());
            continue;
        default:
            // (Not one MSVC6 knows: its character alone.)
            out.push_back(conversion);
            continue;
        }
        if (made > 0)
            out.append(piece, std::min(size_t(made), sizeof piece - 1));
    }
    va_end(ap);
    if (size > 0) {
        const size_t n = std::min(out.size(), size - 1);
        std::memcpy(buffer, out.data(), n);
        buffer[n] = '\0';
    }
    return int(out.size());
}

inline int SnPrintf(char *buffer, std::size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int made = VsnPrintf(buffer, size, format, args);
    va_end(args);
    return made;
}

} // namespace msvc6
