// Harness stub of ls3d/i3d.h: frames found by name (harness.cpp makes one per name, each with
// its own actor).
#pragma once

#include <cstdint>

#include "ls3d/math.h"

constexpr uint32_t FRAME_MASK_ALL = 0xCFFFF;

class C_Actor;

class I3D_frame {
public:
    C_Actor *actor = nullptr;
};

class I3D_scene {
public:
    I3D_frame *FindFrame(const char *name, uint32_t typeMask);
};

class I3D_driver {
public:
    I3D_frame *FindFrame(I3D_scene *scene, const char *name);
};
I3D_driver *I3DGetDriver();
