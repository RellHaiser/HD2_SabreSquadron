// Function-entry coverage for the MSVC client (build_game.py --coverage). Every other source is compiled
// with /Gh, which makes each function call _penter before its prologue; this file is compiled WITHOUT /Gh.
//
// The hits go to a memory-mapped file, one byte per byte of the loaded image (index: the address after
// the function's `call _penter`, minus the image base), so they survive a crash or a killed process: the
// system writes a file mapping's pages back whatever way the process ends. The file starts with a header
// naming the image (its PE TimeDateStamp and SizeOfImage); a file of another build is started afresh.
// Runs of the same build OR their hits together.
//
// File: %HD2_COVERAGE_FILE%, else HD2_Port.cov beside the exe. Read by _re/tools/client_coverage.py.
//
// _penter saves EAX, ECX, EDX and the flags and touches neither the x87 stack nor its control word, so
// the instrumented code computes what the plain release build computes; only timing differs.
#include <windows.h>

#include <cstdint>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

struct S_CoverageHeader {
    char magic[8];          // "HD2COV1\0"
    uint32_t timeDateStamp; // the image's
    uint32_t sizeOfImage;
    uint32_t headerSize;    // the hits start here
    uint32_t reserved[3];
};

volatile LONG s_state = 0;  // 0 not started, 1 starting, 2 ready, 3 failed
uint8_t *s_hits = nullptr;
uint32_t s_size = 0;

void Start()
{
    if (InterlockedCompareExchange(&s_state, 1, 0) != 0) {
        while (s_state == 1)
            Sleep(0);
        return;
    }
    const uint8_t *base = reinterpret_cast<const uint8_t *>(&__ImageBase);
    const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + __ImageBase.e_lfanew);
    const uint32_t stamp = nt->FileHeader.TimeDateStamp;
    const uint32_t size = nt->OptionalHeader.SizeOfImage;

    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("HD2_COVERAGE_FILE", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        n = GetModuleFileNameA(nullptr, path, MAX_PATH);
        char *dot = nullptr;
        for (char *p = path; *p; ++p)
            if (*p == '.')
                dot = p;
            else if (*p == '\\' || *p == '/')
                dot = nullptr;
        if (!dot || dot + 5 > path + MAX_PATH) {
            s_state = 3;
            return;
        }
        lstrcpyA(dot, ".cov");
    }
    const DWORD total = sizeof(S_CoverageHeader) + size;
    HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        s_state = 3;
        return;
    }
    // Another build's file (or a short one): start afresh.
    S_CoverageHeader header = {};
    DWORD read = 0;
    const bool same = ReadFile(file, &header, sizeof header, &read, nullptr) && read == sizeof header &&
                      lstrcmpA(header.magic, "HD2COV1") == 0 && header.timeDateStamp == stamp &&
                      header.sizeOfImage == size && GetFileSize(file, nullptr) == total;
    if (!same) {
        SetFilePointer(file, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(file);
        SetFilePointer(file, total, nullptr, FILE_BEGIN);
        SetEndOfFile(file);
    }
    HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READWRITE, 0, total, nullptr);
    CloseHandle(file);
    if (!mapping) {
        s_state = 3;
        return;
    }
    uint8_t *view = static_cast<uint8_t *>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, total));
    CloseHandle(mapping);   // (the view keeps the mapping)
    if (!view) {
        s_state = 3;
        return;
    }
    if (!same) {
        S_CoverageHeader *h = reinterpret_cast<S_CoverageHeader *>(view);
        lstrcpyA(h->magic, "HD2COV1");
        h->timeDateStamp = stamp;
        h->sizeOfImage = size;
        h->headerSize = sizeof(S_CoverageHeader);
    }
    s_size = size;
    s_hits = view + sizeof(S_CoverageHeader);
    InterlockedExchange(&s_state, 2);
}

} // namespace

extern "C" void __stdcall CoverageHit(uint32_t returnAddress)
{
    if (s_state != 2) {
        if (s_state == 3)
            return;
        Start();
        if (s_state != 2)
            return;
    }
    const uint32_t rva = returnAddress - reinterpret_cast<uint32_t>(&__ImageBase);
    if (rva < s_size && !s_hits[rva])
        s_hits[rva] = 1;
}

extern "C" __declspec(naked) void __cdecl _penter()
{
    __asm {
        push eax
        push ecx
        push edx
        pushfd
        mov eax, [esp + 16]     // the return address: the instrumented function's first instruction + 5
        push eax
        call CoverageHit        // __stdcall: pops its argument
        popfd
        pop edx
        pop ecx
        pop eax
        ret
    }
}
