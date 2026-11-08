// The server's stand-ins for src/game/interface_selectors.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/interface.h"
#include "game/interface_internal.h"
#include <cstdint>
#include <cstdio>
#include "common/c_str.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/squad_order.h"
#include "game/team.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

// Not in the server.
int C_InfoSelector::GetSelectedIndex() const
{
    return 0;
}

// Not in the server.
int C_MapCursor::GetHoveredId()
{
    return 0;
}

// Not in the server.
S_HumanOrder *C_InfoSelector::GetSelectedOrder() const
{
    return nullptr;
}

// Not in the server.
void C_InfoSelector::SelectOrder(S_HumanOrder *order)
{
}

// Not in the server.
void C_InfoSelector::SelectOrderIndex(int index)
{
}

// Not in the server.
void C_MapCursor::ClampMotion(float *held, float * /*pressed*/) const
{
}

// Not in the server.
void C_MapCursor::GetScreenPos(int *x, int *y) const
{
}

// Not in the server.
void C_MapCursor::SetFromWorld(const S_vector4 &point)
{
}

// Not in the server.
void C_MapCursor::SetMode(int mode)
{
}
