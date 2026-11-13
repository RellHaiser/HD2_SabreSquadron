// The Win32 terms the engine's interfaces are written in (HRESULT, DWORD, GUID, __stdcall, ...), on
// every platform.
//
// On Windows they come from <windows.h>. Elsewhere this header declares the few the engine uses,
// with their Win32 sizes (DWORD and LONG stay 32 bits on LP64 systems), and the calling-convention
// keywords expand to the compiler's default: off Windows there is no binary to stay compatible with,
// and the engine and its client are built with the same header.
//
// LS3D_POSIX is 1 when the build is not a Windows one: off Windows, or on Windows with
// LS3D_FORCE_POSIX defined (a MinGW build that checks the POSIX side on a Windows machine).
#pragma once

#include <cstdint>

#if defined(_WIN32) && !defined(LS3D_FORCE_POSIX)
#define LS3D_POSIX 0
#include <windows.h>
#else
#define LS3D_POSIX 1
#endif

// A function the shared library exports (the Windows build also lists its exports in ls3drf.def).
#if defined(_WIN32)
#define LS3D_EXPORT __declspec(dllexport)
#else
#define LS3D_EXPORT __attribute__((visibility("default")))
#endif

#if LS3D_POSIX

// ---------------------------------------------------------------- calling conventions

// (MinGW's own headers define these already; LS3D_FORCE_POSIX keeps them.)
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __thiscall
#define __thiscall
#endif
#ifndef WINAPI
#define WINAPI
#endif

// ---------------------------------------------------------------- types

typedef int32_t HRESULT;
typedef int32_t LONG;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef int BOOL;
typedef int INT;
typedef unsigned int UINT;
typedef void *LPVOID;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// Window and module handles: opaque here (the engine only stores and passes them).
typedef struct HWND__ *HWND;
typedef struct HINSTANCE__ *HINSTANCE;
typedef struct HMENU__ *HMENU;

// A file time (100 ns units since 1601), as the model and animation files store it.
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;

// 8-byte aligned as on Windows (the i386 System V ABI aligns 64-bit members to 4 only).
typedef union alignas(8) _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    int64_t QuadPart;
} LARGE_INTEGER;

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) (static_cast<HRESULT>(hr) < 0)
#endif

static_assert(sizeof(GUID) == 16, "GUID layout");
static_assert(sizeof(LARGE_INTEGER) == 8 && alignof(LARGE_INTEGER) == 8, "LARGE_INTEGER layout");

#endif  // LS3D_POSIX
