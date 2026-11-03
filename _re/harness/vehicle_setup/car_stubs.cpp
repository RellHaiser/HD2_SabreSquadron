// The car's surroundings for the harness (src/game/car.cpp runs for real): C_Actor's base methods
// as the real ones do them (game/actor.cpp is too wide to link here), the car's slots ported in
// later parts (empty), the shot manager's storage, the game's texts, the actor creators, and
// the data files of C_chunk (src/common/chunk.cpp) as memory.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "common/chunk.h"
#include "game/car.h"
#include "game/pending.h"
#include "ls3d/rw_data.h"

// --- C_Actor (as game/actor.cpp) --------------------------------------------------------------------

namespace {
C_ActorCreator *g_creators[ACTOR_TYPE_COUNT];
}  // namespace
C_ActorCreator **g_actorCreators = g_creators;

int g_actorInits, g_actorCopies;

C_Actor::C_Actor(E_ActorType type) : m_type(type), m_name("Missing actor name!!") {}
C_Actor::~C_Actor()
{
    if (m_frame)
        m_frame->Release();
}
void C_Actor::CopyFrom(const C_Actor *)
{
    ++g_actorCopies;
}
bool C_Actor::SetFrame(I3D_frame *frame)
{
    if (!frame)
        return false;
    m_frame = frame;
    frame->AddRef();
    Init();
    return true;
}
void C_Actor::Init()
{
    ++g_actorInits;
}
void C_Actor::Done() {}
// (The saved game, part 2f-ii: SaveGame writes CHUNK_ACTOR (0x3039) holding g_actorSaveTag; LoadGame
// enters it as the real one does and logs the tag read, -1 when none.)
int g_actorSaveTag;
std::vector<int> g_actorLoads;
bool C_Actor::SaveGame(C_chunk &chunk)
{
    chunk.Begin(0x3039);
    chunk.Write(g_actorSaveTag);
    chunk.Ascend();
    return true;
}
bool C_Actor::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id != 0x3039) {
        chunk.Ascend();
        return false;
    }
    int tag = -1;
    chunk.Read(tag);
    g_actorLoads.push_back(tag);
    chunk.Ascend();
    return true;
}
int C_Actor::GetSeatAmmoCount(int)
{
    return 0;
}
void C_Actor::GetBoxCorners(S_vector4 *) {}
// (The car's StartMission / StopMission: the `server` of each call.)
std::vector<bool> g_activations, g_deactivations;
void C_Actor::Activate(bool server)
{
    g_activations.push_back(server);
}
void C_Actor::Deactivate(bool server)
{
    g_deactivations.push_back(server);
}

// --- the game's texts ---------------------------------------------------------------------------------

C_GameTexts g_texts;
bool g_textIds;                                         // (part 7a: every text is "text <id>")
C_str C_GameTexts::Get(int id) const
{
    if (id == 99999)                                    // (part 9b: a version's format)
        return C_str("Version %d.%02d");
    if (g_textIds) {
        char text[32];
        std::snprintf(text, sizeof text, "text %d", id);
        return C_str(text);
    }
    return id == 0x1bf0 ? C_str("Car (text 0x1bf0)") : C_str("other text");
}

// --- the car's later parts ------------------------------------------------------------------------------

// (The damage parts' setup, lifetime, saved game and fuse run for real: game/car_parts.cpp.)
// (The mission, the network side and the saved game run for real: game/car_mission.cpp,
// game/car_net.cpp, game/car_save.cpp.)
// (The seats run for real: game/car_seats.cpp; their surroundings are the harness's.)
// (The damage parts' hits and effects run for real too: game/car_parts.cpp.)

// The shot manager the car's callbacks blow minefields up with (game/car_callbacks.cpp; its
// Explode and PlayExplosion are the harness's): storage only, under the real name (this file does
// not see game/shots.h).
alignas(16) unsigned char g_shotManager[0x160];
// The team manager (game/car_hits.cpp asks the player's team; IsMember is the harness's): storage
// only, likewise.
alignas(16) unsigned char g_teamManager[0x140];
// The particle effects (the damage parts'; Create and Destroy are the harness's): storage only.
alignas(16) unsigned char g_particlesModule[0x1048];

// --- the data files as memory (C_chunk) ---------------------------------------------------------------

std::map<std::string, std::vector<uint8_t>> g_files;
namespace {
struct S_OpenFile {
    std::string name;
    size_t pos = 0;
};
std::map<int, S_OpenFile> g_handles;
int g_nextHandle = 1;
}  // namespace
// The files open (part 9a's test: each opened one closed).
size_t DtaOpenCount() { return g_handles.size(); }

bool g_dtaArchivesFirst;                                // the last dtaOpen's flag (part 8's test)
int __stdcall dtaOpen(const char *fileName, bool archivesFirst)
{
    g_dtaArchivesFirst = archivesFirst;
    if (!g_files.count(fileName))
        return -1;
    g_handles[g_nextHandle] = {fileName, 0};
    return g_nextHandle++;
}
bool g_dtaOpenWriteFails;                              // dtaOpenWrite fails (part 9e-iv's test)
int __stdcall dtaOpenWrite(const char *fileName, int)
{
    if (g_dtaOpenWriteFails)
        return -1;
    g_files[fileName].clear();
    g_handles[g_nextHandle] = {fileName, 0};
    return g_nextHandle++;
}
int __stdcall dtaRead(int handle, void *buffer, int size)
{
    S_OpenFile &f = g_handles.at(handle);
    const std::vector<uint8_t> &data = g_files[f.name];
    const size_t n = f.pos >= data.size() ? 0 : std::min(static_cast<size_t>(size), data.size() - f.pos);
    std::memcpy(buffer, data.data() + f.pos, n);
    f.pos += n;
    return static_cast<int>(n);
}
int __stdcall dtaWrite(int handle, const void *buffer, int size)
{
    S_OpenFile &f = g_handles.at(handle);
    std::vector<uint8_t> &data = g_files[f.name];
    if (data.size() < f.pos + size)
        data.resize(f.pos + size);
    std::memcpy(data.data() + f.pos, buffer, static_cast<size_t>(size));
    f.pos += static_cast<size_t>(size);
    return size;
}
int __stdcall dtaSeek(int handle, int position, int origin)
{
    S_OpenFile &f = g_handles.at(handle);
    const size_t size = g_files[f.name].size();
    f.pos = origin == 0 ? position : origin == 1 ? f.pos + position : size + position;
    return static_cast<int>(f.pos);
}
bool __stdcall dtaClose(int handle)
{
    g_handles.erase(handle);
    return true;
}
