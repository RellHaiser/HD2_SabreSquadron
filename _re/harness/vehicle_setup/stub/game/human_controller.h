// Harness stub of game/human_controller.h (the real one clashes with the stub game/human.h): the
// control indices the seat weapon reads (game/seat_weapon.cpp) and the controllers the spawn point
// sets up (game/spawn_point.cpp; part 7b), under the real names and values.
#pragma once

enum E_ControlAxis {
    AXIS_FIRE = 6,
    AXIS_RELOAD = 13,
};
enum E_ControlButton {
    BUTTON_FIRE = 6,
};

class C_HumanController {
public:
    virtual ~C_HumanController() = default;
    virtual void SetSpectating(bool on) { spectating = on; }
    void SetVip(bool on) { vip = on; }
    bool spectating = false, vip = false;
};
class C_PlayerController : public C_HumanController {};
class C_RemoteController : public C_HumanController {};
