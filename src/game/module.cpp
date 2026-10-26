#include "game/module.h"

#include "common/chunk.h"

C_Module *g_modules[MODULE_COUNT];                           // 0x0089bbe8

namespace {
// Saved games store one child chunk per module (chunk id = module id) inside this one.
const uint16_t CHUNK_MODULES = 0x52DC;                       // 0x008397b8
} // namespace

C_Module::C_Module(int id, const char *name)
    : m_name(name), m_id(id)
{
    g_modules[id] = this;
}

C_Module::~C_Module()
{
    g_modules[m_id] = nullptr;
}

namespace ModuleManager {

template <typename Fn>
static void ForEachModule(Fn fn)
{
    for (C_Module *module : g_modules) {
        if (module)
            fn(*module);
    }
}

void BroadcastInit() { ForEachModule([](C_Module &m) { m.Init(); }); }
void BroadcastDone() { ForEachModule([](C_Module &m) { m.Done(); }); }
void BroadcastTick(int timeDelta) { ForEachModule([=](C_Module &m) { m.Tick(timeDelta); }); }
void BroadcastUpdate(int timeDelta) { ForEachModule([=](C_Module &m) { m.Update(timeDelta); }); }
void BroadcastLoadMissionData(bool server) { ForEachModule([=](C_Module &m) { m.LoadMissionData(server); }); }
void BroadcastFreeMissionData(bool server) { ForEachModule([=](C_Module &m) { m.FreeMissionData(server); }); }
void BroadcastStartMission(bool server) { ForEachModule([=](C_Module &m) { m.StartMission(server); }); }
void BroadcastRestartMission(bool server) { ForEachModule([=](C_Module &m) { m.RestartMission(server); }); }
void BroadcastWriteState(C_BitStream *msg) { ForEachModule([=](C_Module &m) { m.WriteState(msg); }); }
void BroadcastReadState(C_BitStream *msg) { ForEachModule([=](C_Module &m) { m.ReadState(msg); }); }

void BroadcastStopMission(bool server)
{
    // Modules shut down in reverse registration order.
    for (int i = MODULE_COUNT - 1; i >= 0; --i) {
        if (g_modules[i])
            g_modules[i]->StopMission(server);
    }
}

bool DispatchMessage(int moduleId, C_BitStream *msg)
{
    return g_modules[moduleId] ? g_modules[moduleId]->OnMessage(msg) : false;
}

bool CallUnknown28(int moduleId, C_BitStream *msg)
{
    return g_modules[moduleId] ? g_modules[moduleId]->Unknown28(msg) : false;
}

bool SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_MODULES))
        return false;

    for (int id = 0; id < MODULE_COUNT; ++id) {
        if (!g_modules[id])
            continue;
        chunk.Begin(static_cast<uint16_t>(id));
        g_modules[id]->SaveGame(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

bool LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id != CHUNK_MODULES) {
        chunk.Ascend();
        return false;
    }

    while (chunk.RemainingSize() > 0) {
        uint16_t moduleId;
        if (!chunk.Descend(moduleId))
            break;
        g_modules[moduleId]->LoadGame(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

} // namespace ModuleManager
