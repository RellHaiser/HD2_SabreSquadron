// Harness stub of ls3d/i3d.h.
#pragma once

#include <cstdint>

class I3D_driver {
public:
    void SetState(int state, uint32_t value);
};
I3D_driver *I3DGetDriver();
