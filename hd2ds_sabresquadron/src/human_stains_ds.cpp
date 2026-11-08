// The server's stand-ins for src/game/human_stains.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/human_stains.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "common/chunk.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_weapon_pose.h"
#include "game/items.h"
#include "game/pending.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

C_StainManager g_stainManager;
// 0x00796c00: not in the server.
void C_StainManager::Done()
{
}

// 0x00796890: not in the server.
void C_StainManager::Init(int)
{
}

// 0x00796bf0: not in the server.
void C_StainManager::Reset()
{
}

// 0x00796830: not in the server.
C_StainManager::~C_StainManager()
{
}
