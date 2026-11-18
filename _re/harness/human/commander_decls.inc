// Part 45's declarations (HUMAN_COMMANDER builds, build_commander.bat; included by human_test.cpp before squad_test.inc,
// whose GetControllerType fakes answer through CmControllerType there).
namespace {
// The answers of the squad controller's fakes (commander_fakes.inc) and the controllers' types.
struct S_CmAnswers {
    bool seatWeapon = false;                            // HasSeatWeapon
    bool projectile = false;                            // HasProjectileSeatWeapon
    bool launcher = false;                              // FindWeapon (a launcher, the weapon 0)
    bool selectedPlayer = false;                        // the selected soldier's controller a player's (else the squad's)
} g_cm;
// A human's controller type (both sides, not logged: the port's fixes ask where the original does not): the squad's,
// but the selected soldier's a player's when g_cm.selectedPlayer.
int CmControllerType(const void *human);
}  // namespace
