// The original's layouts: checks that hold in a 32-bit build only.
//
// The engine's classes keep LS3DRF.dll's sizes and field offsets, which the game reads directly. They
// hold pointers and virtual tables, so they have those layouts only where a pointer is 4 bytes. A
// 64-bit build has its own layouts and no binary to match; it still compiles each check's
// expression, so a renamed field is noticed there too. (Plain data - vectors, matrices, the
// Direct3D structures - keeps its layout everywhere and keeps plain static_asserts.)
#pragma once

#include <cstdint>

#if UINTPTR_MAX == 0xFFFFFFFFu
#define LS3D_ABI32 1
#else
#define LS3D_ABI32 0
#endif

// static_assert(cond, what) in a 32-bit build.
#define LS3D_LAYOUT32(cond, what) static_assert(!LS3D_ABI32 || (cond), what)
