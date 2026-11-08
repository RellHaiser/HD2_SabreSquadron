// The server's stand-ins for src/game/interface.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/interface.h"
#include <cstdio>
#include <vector>
#include <windows.h>
#include <mmsystem.h>
#include "common/chunk.h"
#include "game/actor.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/squad_commander.h"
#include "game/team.h"
#include "game/world.h"
#include "game/collision.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

I3D_sound *g_clickSound = nullptr;                      // 0x0089bc8c
// Not in the server.
C_Interface::C_Interface() = default;

// Not in the server.
C_Interface::~C_Interface() = default;

// 0x0049b7e0: not in the server.
bool C_Interface::AddSquadMember(C_Human *human, int slot)
{
    return false;
}

// 0x00494140: not in the server.
bool C_Interface::Done()
{
    return true;
}

// 0x00493f80: not in the server.
bool C_Interface::Init()
{
    return true;
}

// 0x0049d300: not in the server.
bool C_Interface::IsCapturingInput() const
{
    return false;
}

// 0x0049f090: not in the server.
bool C_Interface::IsRespawnScreenShown() const
{
    return false;
}

// 0x0049f8d0: not in the server.
bool C_Interface::LoadGame(C_chunk &chunk)
{
    return true;
}

// 0x0049b870: not in the server.
bool C_Interface::RemoveSquadMember(C_Human *human, int slot)
{
    return false;
}

// 0x0049f600: not in the server.
bool C_Interface::SaveGame(C_chunk &chunk)
{
    return true;
}

// 0x0049f0e0: not in the server.
C_Human *C_Interface::GetShownSquadMember() const
{
    return nullptr;
}

// 0x00494a40: not in the server.
void C_Interface::AddMessage(const S_vector &color, const char *text)
{
}

// 0x0049fc10: not in the server.
void C_Interface::FilterInput(float *held, float *pressed)
{
}

// 0x0049f0b0: not in the server.
void C_Interface::FlashDamage()
{
}

// 0x004a3420: not in the server.
void C_Interface::HandleInput(float *held, float *pressed)
{
}

// 0x0049d340: not in the server.
void C_Interface::HighlightMembers(C_Human *human, int group)
{
}

// 0x0049ef70: not in the server.
void C_Interface::OnActorEvent(int event, C_Actor *actor)
{
}

// 0x0049f410: not in the server.
void C_Interface::OpenCommandMap(bool open)
{
}

// 0x0049fbf0: not in the server.
void C_Interface::RefreshInventory()
{
}

// 0x0049b960: not in the server.
void C_Interface::RemoveFromMap(C_Human *human)
{
}

// 0x00495f00: not in the server.
void C_Interface::Render()
{
}

// 0x004b7780: not in the server.
void C_Interface::RenderOverlay()
{
}

// 0x0049f5e0: not in the server.
void C_Interface::Reset()
{
}

// 0x0049f0c0: not in the server.
void C_Interface::SetObjectiveIcon(int index, int state)
{
}

// 0x0049f250: not in the server.
void C_Interface::SetScreen(int screen)
{
}

// 0x0049dde0: not in the server.
void C_Interface::SetTimer(int ms)
{
}

// 0x0049dc60: not in the server.
void C_Interface::SetViewMode(int viewMode)
{
}

// 0x004ad790: not in the server.
void C_Interface::ShowCountdown(int textId, int ms)
{
}

// 0x0049b980: not in the server.
void C_Interface::ShowEnemy(C_Human *enemy, bool show)
{
}

// 0x0049de90: not in the server.
void C_Interface::ShowInventory(bool show, C_Actor *container)
{
}

// 0x0049de30: not in the server.
void C_Interface::ShowMessage(int textId)
{
}

// 0x0049f5d0: not in the server.
void C_Interface::ShowPhotoFlash()
{
}

// 0x0049f040: not in the server.
void C_Interface::ShowRespawnScreen(bool show)
{
}

// 0x004ad650: not in the server.
void C_Interface::ShowRoundResult(int textId, int axisWins, int alliedWins)
{
}

// 0x0049f020: not in the server.
void C_Interface::ShowScores(bool show)
{
}

// 0x0049de80: not in the server.
void C_Interface::ShowSoundDirection(const S_vector &dir, int chapter)
{
}

// 0x0049f100: not in the server.
void C_Interface::ShowSquadMember(C_Human *human)
{
}

// 0x0049de50: not in the server.
void C_Interface::ShowTimer(int textId, int ms)
{
}

// 0x00493860: not in the server.
void C_Interface::Update(int timeDelta)
{
}
