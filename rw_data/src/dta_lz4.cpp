// LZ4 decoder of the archive blocks (an addition of the port: the original only knew LZSS).
//
// A block of type DTA_BLOCK_LZ4 holds one LZ4 *block* (https://github.com/lz4/lz4,
// doc/lz4_Block_format.md) as written by LZ4_compress_default or, at levels 3..12, by the
// high-compression LZ4_compress_HC (lib/lz4hc.c), which only searches harder for matches and
// writes the same format. No frame, no size header (a block always decodes to 32 KB, the last
// one of a file to the rest). The stream is a list of sequences:
//   token     high nibble = literal length, low nibble = match length - 4; a nibble of 15 is
//             continued by bytes added to it, up to and including the first one that is not 255
//   literals  the literal bytes
//   offset    u16 little endian, 1..65535: the match starts this many bytes back in the output
//   match     (length + 4 bytes, copied byte by byte: it may overlap its own output)
// The last sequence has only the token and the literals; the stream ends with them.
#include "dta_internal.h"

#include <cstring>

namespace {

constexpr uint32_t MIN_MATCH = 4;

// Reads a length continued past its nibble of 15. False if the input ends first.
bool ReadLength(const uint8_t *src, uint32_t srcSize, uint32_t &in, uint32_t &length)
{
    uint8_t byte;
    do {
        if (in >= srcSize)
            return false;
        byte = src[in++];
        length += byte;
    } while (byte == 255 && length < 0x01000000);   // (anything past 16 MB is damage)
    return true;
}

} // namespace

uint32_t Lz4Decompress(const uint8_t *src, uint32_t srcSize, uint8_t *dst, uint32_t dstCapacity)
{
    // Like the other decoders it never fails: a damaged stream (a length or offset past the
    // input, a reference before the start of the output) ends the decoding there, and the
    // output stops at `dstCapacity`. A valid block decodes exactly as LZ4_decompress_safe.
    uint32_t in = 0;
    uint32_t out = 0;
    while (in < srcSize) {
        const uint8_t token = src[in++];

        uint32_t literals = token >> 4;
        if (literals == 15 && !ReadLength(src, srcSize, in, literals))
            return out;
        if (literals > srcSize - in)
            literals = srcSize - in;   // cut off: keep what is there
        const uint32_t copied = literals < dstCapacity - out ? literals : dstCapacity - out;
        std::memcpy(dst + out, src + in, copied);
        in += literals;
        out += copied;
        if (copied != literals || in >= srcSize)
            return out;   // the room is full, or this was the last sequence

        if (srcSize - in < 2)
            return out;
        const uint32_t offset = src[in] | src[in + 1] << 8;
        in += 2;
        if (offset == 0 || offset > out)
            return out;

        uint32_t length = token & 0xF;
        if (length == 15 && !ReadLength(src, srcSize, in, length))
            return out;
        length += MIN_MATCH;
        const uint32_t n = length < dstCapacity - out ? length : dstCapacity - out;
        const uint8_t *from = dst + out - offset;
        for (uint32_t i = 0; i < n; ++i)   // byte by byte: the copy may overlap
            dst[out + i] = from[i];
        out += n;
        if (n != length)
            return out;
    }
    return out;
}
