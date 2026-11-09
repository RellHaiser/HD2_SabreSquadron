// Media files the tests write: RIFF chunks, uncompressed AVI files and silent WAV files
// (compare_show, test_video_ffmpeg).
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// The pixel bytes of an AVI frame WriteAvi writes (`c`: the byte within the pixel).
inline uint8_t AviPixel(int x, int y, int frame, int c, int bits)
{
    uint8_t value = static_cast<uint8_t>(x * 13 + y * 7 + frame * 29 + c * 50 + (x ^ y) * 3);
    return bits == 16 && c == 1 ? static_cast<uint8_t>(value & 0x7f) : value;
}

struct S_riff {
    std::vector<uint8_t> v;
    void U16(int x) { v.push_back(uint8_t(x)), v.push_back(uint8_t(x >> 8)); }
    void U32(uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (i * 8))); }
    void Tag(const char *t) { v.insert(v.end(), t, t + 4); }
    // A chunk: its size is filled in by End.
    size_t Begin(const char *tag)
    {
        Tag(tag);
        U32(0);
        return v.size();
    }
    size_t List(const char *list, const char *type)
    {
        const size_t at = Begin(list);
        Tag(type);
        return at;
    }
    void End(size_t at)
    {
        const uint32_t size = static_cast<uint32_t>(v.size() - at);
        std::memcpy(&v[at - 4], &size, 4);
        if (size & 1)
            v.push_back(0);
    }
    bool Save(const std::string &path) const
    {
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;
        std::fwrite(v.data(), 1, v.size(), f);
        std::fclose(f);
        return true;
    }
};

// An uncompressed AVI at 10 frames a second (`bits`: 16 (RGB555), 24 or 32), every frame its own
// picture; with `audioMs`, a silent 22 kHz mono stream too.
inline bool WriteAvi(const std::string &path, int width, int height, int bits, int frames, int audioMs)
{
    const int rowBytes = (width * bits / 8 + 3) & ~3;
    const uint32_t frameSize = static_cast<uint32_t>(rowBytes * height);
    const uint32_t audioBytes = static_cast<uint32_t>(22050 * 2 * audioMs / 1000);
    S_riff r;
    const size_t riff = r.List("RIFF", "AVI ");
    const size_t hdrl = r.List("LIST", "hdrl");
    const size_t avih = r.Begin("avih");
    r.U32(100000);                          // microseconds a frame
    r.U32(frameSize * 10 + 44100);
    r.U32(0);
    r.U32(0x10);                            // AVIF_HASINDEX
    r.U32(static_cast<uint32_t>(frames));
    r.U32(0);
    r.U32(audioMs ? 2 : 1);
    r.U32(frameSize);
    r.U32(static_cast<uint32_t>(width));
    r.U32(static_cast<uint32_t>(height));
    for (int i = 0; i < 4; ++i)
        r.U32(0);
    r.End(avih);
    {
        const size_t strl = r.List("LIST", "strl");
        const size_t strh = r.Begin("strh");
        r.Tag("vids");
        r.Tag("DIB ");
        r.U32(0);
        r.U16(0);
        r.U16(0);
        r.U32(0);
        r.U32(1);                           // scale / rate: 10 a second
        r.U32(10);
        r.U32(0);
        r.U32(static_cast<uint32_t>(frames));
        r.U32(frameSize);
        r.U32(0xffffffff);
        r.U32(0);
        r.U16(0);
        r.U16(0);
        r.U16(width);
        r.U16(height);
        r.End(strh);
        const size_t strf = r.Begin("strf");
        r.U32(40);
        r.U32(static_cast<uint32_t>(width));
        r.U32(static_cast<uint32_t>(height));
        r.U16(1);
        r.U16(bits);
        r.U32(0);                           // BI_RGB
        r.U32(frameSize);
        for (int i = 0; i < 4; ++i)
            r.U32(0);
        r.End(strf);
        r.End(strl);
    }
    if (audioMs) {
        const size_t strl = r.List("LIST", "strl");
        const size_t strh = r.Begin("strh");
        r.Tag("auds");
        r.U32(0);
        r.U32(0);
        r.U16(0);
        r.U16(0);
        r.U32(0);
        r.U32(2);                           // scale: the block; rate: bytes a second
        r.U32(44100);
        r.U32(0);
        r.U32(audioBytes / 2);
        r.U32(audioBytes);
        r.U32(0xffffffff);
        r.U32(2);
        for (int i = 0; i < 4; ++i)
            r.U16(0);
        r.End(strh);
        const size_t strf = r.Begin("strf");
        r.U16(WAVE_FORMAT_PCM);
        r.U16(1);
        r.U32(22050);
        r.U32(44100);
        r.U16(2);
        r.U16(16);
        r.End(strf);
        r.End(strl);
    }
    r.End(hdrl);
    struct S_index {
        const char *id;
        uint32_t offset, size;
    };
    std::vector<S_index> index;
    const size_t movi = r.List("LIST", "movi");
    const size_t moviTag = movi;            // the 'movi' type: the index's offsets count from there
    if (audioMs) {
        const size_t chunk = r.Begin("01wb");
        index.push_back({"01wb", static_cast<uint32_t>(chunk - 8 - moviTag), audioBytes});
        r.v.insert(r.v.end(), audioBytes, 0);
        r.End(chunk);
    }
    for (int f = 0; f < frames; ++f) {
        const size_t chunk = r.Begin("00db");
        index.push_back({"00db", static_cast<uint32_t>(chunk - 8 - moviTag), frameSize});
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < rowBytes; ++x) {
                const int c = x % (bits / 8);
                const int px = x / (bits / 8);
                r.v.push_back(px < width ? AviPixel(px, y, f, c, bits) : 0);
            }
        }
        r.End(chunk);
    }
    r.End(movi);
    const size_t idx1 = r.Begin("idx1");
    for (const S_index &e : index) {
        r.Tag(e.id);
        r.U32(0x10);                        // AVIIF_KEYFRAME
        r.U32(e.offset);
        r.U32(e.size);
    }
    r.End(idx1);
    r.End(riff);
    return r.Save(path);
}

inline bool WriteWav(const std::string &path, int ms)
{
    const uint32_t bytes = static_cast<uint32_t>(22050 * 2 * ms / 1000);
    S_riff r;
    const size_t riff = r.List("RIFF", "WAVE");
    const size_t fmt = r.Begin("fmt ");
    r.U16(WAVE_FORMAT_PCM);
    r.U16(1);
    r.U32(22050);
    r.U32(44100);
    r.U16(2);
    r.U16(16);
    r.End(fmt);
    const size_t data = r.Begin("data");
    r.v.insert(r.v.end(), bytes, 0);
    r.End(data);
    r.End(riff);
    return r.Save(path);
}

}  // namespace
