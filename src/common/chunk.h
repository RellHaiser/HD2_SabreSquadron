// C_chunk — reader/writer for the engine's chunked binary files (.bin, .dat, saves).
//
// File format: a tree of chunks, each starting with a 6-byte header
//   uint16_t id; uint32_t size;   // size includes the header itself
// followed by payload and/or child chunks. Reading descends into a chunk and
// remembers where it ends; Ascend() skips to that end. Writing records where a
// chunk starts and back-patches its size on Ascend().
//
// All I/O goes through the rw_data.dll handles so archives (.dta) are transparent.
#pragma once

#include <cstdint>

class C_chunk {
public:
    static constexpr int MAX_DEPTH = 256;

    C_chunk() = default;
    ~C_chunk();                                    // 0x00405690
    C_chunk(const C_chunk &) = delete;
    C_chunk &operator=(const C_chunk &) = delete;

    bool Open(const char *fileName, bool write);   // 0x00405620
    bool Close();                                  // 0x00405750
    bool IsOpen() const { return m_open; }
    bool IsWriting() const { return m_writing; }

    // --- reading ---------------------------------------------------------
    // Reads the next chunk header and enters the chunk. Returns false at EOF.
    bool Descend(uint16_t &id);                    // 0x00415c90 (an inline copy)
    // Bytes left in the current chunk (0 when no chunk is entered), or -1 on error.
    int RemainingSize() const;
    bool AtEnd() const { return RemainingSize() <= 0; }
    bool Read(void *buffer, int size);             // 0x00405940
    template <typename T> bool Read(T &value) { return Read(&value, sizeof(T)); }
    // Reads the remainder of the current chunk as a string (at most bufferSize-1 chars).
    // (0x00415d00 reads up to 999 chars into a C_str and ascends.)
    bool ReadString(char *buffer, int bufferSize);
    // Reads a string stored as uint16 length (including the terminator) + chars;
    // longer strings are truncated. Returns the number of bytes kept.
    int ReadSizedString(char *buffer, int bufferSize);   // 0x00415e30

    // --- writing ---------------------------------------------------------
    bool Begin(uint16_t id);                       // 0x00405820
    bool Write(const void *data, int size);        // 0x004059b0
    template <typename T> bool Write(const T &value) { return Write(&value, sizeof(T)); }
    // Writes a complete chunk: header, payload, size patch.
    void WriteChunk(uint16_t id, const void *data, int size);   // 0x004059f0
    template <typename T> void WriteChunk(uint16_t id, const T &value) { WriteChunk(id, &value, sizeof(T)); }
    void WriteStringChunk(uint16_t id, const char *text);   // 0x004256a0
    void WriteSizedString(const char *text);       // 0x00414dd0

    // --- both ------------------------------------------------------------
    // Leaves the current chunk (reading: skip to its end; writing: patch its size, also
    // stored to `size` when given; every caller of the original passes null).
    bool Ascend(uint32_t *size = nullptr);         // 0x00405890

private:
    int Tell() const;

    bool m_open = false;          // +0x000
    int m_handle = -1;            // +0x004  rw_data handle
    bool m_writing = false;       // +0x008
    int m_fileSize = -1;          // +0x00C  only known when reading
    int m_stack[MAX_DEPTH] = {};  // +0x010  chunk end (read) / start (write) positions
    int m_depth = 0;              // +0x410
};
