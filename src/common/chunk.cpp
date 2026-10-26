#include "common/chunk.h"

#include <cstring>

#include "ls3d/rw_data.h"

namespace {

#pragma pack(push, 1)
struct ChunkHeader {
    uint16_t id;
    uint32_t size;
};
#pragma pack(pop)
static_assert(sizeof(ChunkHeader) == 6, "chunk header is 6 bytes on disk");

constexpr int SEEK_FROM_START = 0;
constexpr int SEEK_FROM_CURRENT = 1;
constexpr int SEEK_FROM_END = 2;

} // namespace

C_chunk::~C_chunk()
{
    Close();
}

bool C_chunk::Open(const char *fileName, bool write)
{
    if (m_open)
        return false;

    m_fileSize = -1;
    m_handle = write ? dtaOpenWrite(fileName, 0x10000) : dtaOpen(fileName, 0);
    if (m_handle == DTA_INVALID_HANDLE)
        return false;

    m_writing = write;
    if (!write)
        m_fileSize = dtaSeek(m_handle, 0, SEEK_FROM_END);
    dtaSeek(m_handle, 0, SEEK_FROM_START);
    m_open = true;
    return true;
}

bool C_chunk::Close()
{
    if (!m_open)
        return false;

    // Unfinished chunks still need their sizes patched.
    if (m_writing) {
        while (m_depth != 0)
            Ascend();
    }
    dtaClose(m_handle);
    m_handle = -1;
    m_open = false;
    return true;
}

int C_chunk::Tell() const
{
    return dtaSeek(m_handle, 0, SEEK_FROM_CURRENT);
}

bool C_chunk::Descend(uint16_t &id)
{
    if (!m_open)
        return false;

    ChunkHeader header;
    if (dtaRead(m_handle, &header, sizeof(header)) != sizeof(header))
        return false;

    m_stack[m_depth++] = Tell() - static_cast<int>(sizeof(header)) + static_cast<int>(header.size);
    id = header.id;
    return true;
}

int C_chunk::RemainingSize() const
{
    if (!m_open || m_depth == 0)
        return 0;
    const int pos = Tell();
    if (pos == -1)
        return -1;
    return m_stack[m_depth - 1] - pos;
}

bool C_chunk::Read(void *buffer, int size)
{
    if (!m_open || m_writing)
        return false;

    if (m_depth != 0) {
        int remaining = m_stack[m_depth - 1] - Tell();
        if (remaining < 0)
            remaining = 0;
        if (remaining < size)
            return false;
    }
    return dtaRead(m_handle, buffer, size) >= size;
}

bool C_chunk::ReadString(char *buffer, int bufferSize)
{
    int size = RemainingSize();
    if (size > bufferSize - 1)
        size = bufferSize - 1;
    if (size < 0)
        size = 0;
    const bool ok = Read(buffer, size);
    buffer[ok ? size : 0] = '\0';
    return ok;
}

int C_chunk::ReadSizedString(char *buffer, int bufferSize)
{
    uint16_t length = 0;
    if (!m_open || m_writing || !Read(length) || length == 0) {
        buffer[0] = '\0';
        return 0;
    }
    if (length <= bufferSize) {
        Read(buffer, length);
        return length;
    }
    // Too long: keep the start, skip the rest.
    Read(buffer, bufferSize);
    buffer[bufferSize - 1] = '\0';
    char skip;
    for (int left = length - bufferSize; left > 0; --left)
        Read(&skip, 1);
    return bufferSize;
}

void C_chunk::WriteSizedString(const char *text)
{
    const uint16_t length = static_cast<uint16_t>(std::strlen(text) + 1);
    Write(length);
    Write(text, length);
}

bool C_chunk::Begin(uint16_t id)
{
    if (!m_open)
        return false;

    m_stack[m_depth++] = Tell();
    const ChunkHeader header = {id, 0};
    return dtaWrite(m_handle, &header, sizeof(header)) == sizeof(header);
}

bool C_chunk::Write(const void *data, int size)
{
    if (!m_open || !m_writing)
        return false;
    return dtaWrite(m_handle, data, size) == size;
}

void C_chunk::WriteChunk(uint16_t id, const void *data, int size)
{
    if (!m_open)
        return;
    Begin(id);
    Write(data, size);
    Ascend();
}

void C_chunk::WriteStringChunk(uint16_t id, const char *text)
{
    WriteChunk(id, text, static_cast<int>(std::strlen(text)) + 1);
}

bool C_chunk::Ascend(uint32_t *size)
{
    if (!m_open)
        return false;

    // At depth 0 the original reads the slot just below the stack, i.e. the file size.
    const int top = m_depth != 0 ? m_stack[m_depth - 1] : m_fileSize;
    if (m_writing) {
        const int end = Tell();
        const uint32_t chunkSize = static_cast<uint32_t>(end - top);
        dtaSeek(m_handle, top + 2, SEEK_FROM_START);   // skip the id field
        dtaWrite(m_handle, &chunkSize, sizeof(chunkSize));
        dtaSeek(m_handle, end, SEEK_FROM_START);
        if (size)
            *size = chunkSize;
    } else {
        dtaSeek(m_handle, top, SEEK_FROM_START);
    }
    if (m_depth != 0)
        --m_depth;
    return true;
}
