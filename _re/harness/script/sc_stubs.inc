// Script part 1: link stubs for what script.cpp reaches that the compiler test never calls (a call ends the test).
#include "game/human.h"
#include "game/items.h"

namespace H {
[[noreturn]] void Unexpected(const char *what)
{
    std::printf("sc_diff: unexpected call: %s [%s]\n", what, g_current.c_str());
    std::exit(3);
}
}  // namespace H
#define UNEXPECTED H::Unexpected(__func__)

int C_Human::GetControllerType() const { UNEXPECTED; }
void C_Human::SetAIOptimized(int) { UNEXPECTED; }
void C_Human::Suspend(bool) { UNEXPECTED; }
I3D_frame *C_Item::GetFrame(int) { UNEXPECTED; }
int C_Script::CallFunction(int, const int *, const int *) { UNEXPECTED; }
bool C_Script::ExecuteCommand(const S_ScriptCommand &) { UNEXPECTED; }
void SetFrameData(I3D_frame *, const S_FrameData &) { UNEXPECTED; }
int RW_DATA_CALL dtaOpenWrite(const char *, int) { UNEXPECTED; }
int RW_DATA_CALL dtaSeek(int, int, int) { UNEXPECTED; }
int RW_DATA_CALL dtaWrite(int, const void *, int) { UNEXPECTED; }
uint32_t g_gameRole = GAME_ROLE_LOCAL;
bool g_scriptModuleRestarting;
