// The pair of checkpoint networks and the loader of their data (see checkpoints.h).
#include "game/checkpoints.h"

#include <cstring>

#include "common/chunk.h"

namespace {

// Saved game chunks of C_Checkpoints.
enum : uint16_t {
    CHUNK_CHECKPOINTS_STATE = 0x81,                     // the walking network's joins
    CHUNK_CHECKPOINTS_82 = 0x82,                        // written empty
    CHUNK_CHECKPOINTS_84 = 0x84,                        // written empty
};

} // namespace

C_CheckpointNetwork::~C_CheckpointNetwork()             // 0x00523bb0
{
    Clear();
}

C_Checkpoints::~C_Checkpoints()                         // 0x00523b20
{
    // (The road network goes first, then the walking network.)
}

void C_Checkpoints::Clear()                             // 0x00523e10
{
    C_CheckpointNetwork::Clear();
    m_roads.Clear();
}

void C_Checkpoints::ResetState()                        // 0x00523e30
{
    C_CheckpointNetwork::ResetState();
}

bool C_Checkpoints::SaveGame(C_chunk &chunk)            // 0x00523f30
{
    chunk.Begin(CHUNK_CHECKPOINTS_STATE);
    SaveState(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_CHECKPOINTS_82);
    chunk.Ascend();
    chunk.Begin(CHUNK_CHECKPOINTS_84);
    chunk.Ascend();
    return true;
}

bool C_Checkpoints::LoadGame(C_chunk &chunk)            // 0x005241a0
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0;
        chunk.Descend(id);
        if (id == CHUNK_CHECKPOINTS_STATE)
            LoadState(chunk);
        chunk.Ascend();
    }
    return true;
}

int __stdcall LoadCheckpointNetwork(C_chunk *chunk, C_CheckpointNetwork *network)   // 0x00523c00
{
    // The header: the version, the size of the data, then version-dependent counts.
    int header[7];
    if (!chunk->Read(&header[0], 4))
        return 0;
    int headerInts;
    const uint32_t version = static_cast<uint32_t>(header[0]);
    if (version == CHECKPOINTS_VERSION_1)
        headerInts = 5;
    else if (version == CHECKPOINTS_VERSION_2)
        headerInts = 6;
    else if (version >= CHECKPOINTS_VERSION_3 && version <= CHECKPOINTS_VERSION_9)
        headerInts = 7;
    else
        return 0;
    const int headerSize = headerInts * 4;
    if (!chunk->Read(&header[1], headerSize - 4))
        return 0;
    const int size = header[1];
    // Fix: a size smaller than the header overran the buffer.
    if (size < headerSize)
        return 0;
    auto *data = static_cast<uint8_t *>(operator new(static_cast<size_t>(size)));
    std::memcpy(data, header, static_cast<size_t>(headerSize));
    if (size != headerSize && !chunk->Read(data + headerSize, size - headerSize)) {
        operator delete(data);
        return 0;
    }
    const uint8_t *end = network->Load(data);
    const int result = end == data + size ? header[0] : 0;
    operator delete(data);
    return result;
}
