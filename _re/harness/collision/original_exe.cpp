// See original_exe.h.
#include "original_exe.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);
#include <cstring>
#include <string>
#include <vector>

namespace {
bool g_loaded;
std::string Env(const char *name, const char *fallback)
{
    const char *v = std::getenv(name);
    return v && *v ? v : fallback;
}
}  // namespace

bool OriginalExeLoaded() { return g_loaded; }

namespace {
const char kReservedVar[] = "HD2_ORIGINAL_RESERVED";
uint32_t ImageSize(const std::string &exe)
{
    FILE *f = std::fopen(exe.c_str(), "rb");
    if (!f)
        return 0;
    uint8_t head[0x2000] = {};
    const size_t got = std::fread(head, 1, sizeof head, f);
    std::fclose(f);
    uint32_t pe = 0, size = 0;
    if (got < 0x40)
        return 0;
    std::memcpy(&pe, head + 0x3C, 4);
    if (pe + 0x54 > got)
        return 0;
    std::memcpy(&size, head + pe + 0x50, 4);
    return size;
}
std::string ExePath() { return Env("HD2_ORIGINAL_EXE", "H:\\Other HD2 Files\\HD2_SabreSquadron_unpacked.exe"); }
}  // namespace

bool RunReservingOriginalBase(int &exitCode)
{
    if (std::getenv(kReservedVar))
        return false;                                   // the child: carry on
    const uint32_t size = ImageSize(ExePath());
    if (!size)
        return false;                                   // no executable: run without it
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, sizeof self);
    SetEnvironmentVariableA(kReservedVar, "1");
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    for (int attempt = 0; attempt < 32; ++attempt) {
        STARTUPINFOA si = {};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi = {};
        std::string line = GetCommandLineA();
        if (!CreateProcessA(self, &line[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
            break;
        AssignProcessToJobObject(job, pi.hProcess);
        // Before the child starts (only its image, ntdll and the first thread's stack are there): hold the
        // original's range, released by LoadOriginalExe just before mapping it.
        if (!VirtualAllocEx(pi.hProcess, reinterpret_cast<void *>(0x400000), size, MEM_RESERVE, PAGE_NOACCESS)) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            continue;                                   // its stack took the range: another layout
        }
        ResumeThread(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        exitCode = static_cast<int>(code);
        return true;
    }
    CloseHandle(job);
    SetEnvironmentVariableA(kReservedVar, nullptr);
    std::printf("  (could not start a copy with the original's range free: run without it)\n");
    return false;
}

bool LoadOriginalExe()
{
    static bool tried;
    if (tried)
        return g_loaded;
    tried = true;
    const std::string exe = ExePath();
    const std::string game = Env("HD2_GAME_DIR", "H:\\HD2_Unmodified");
    // The file's sections copied to their places at its base by hand (the loader maps files of its own while
    // loading one, into the range just freed): in the range RunReservingOriginalBase holds, else where free.
    std::vector<uint8_t> file;
    if (FILE *f = std::fopen(exe.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        file.resize(static_cast<size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(file.data(), 1, file.size(), f) != file.size())
            file.clear();
        std::fclose(f);
    }
    if (file.size() < 0x400) {
        std::printf("  (the original executable %s: not read)\n", exe.c_str());
        return false;
    }
    const auto *fileNt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(file.data() + reinterpret_cast<const IMAGE_DOS_HEADER *>(file.data())->e_lfanew);
    const uint32_t size = fileNt->OptionalHeader.SizeOfImage;
    if (fileNt->OptionalHeader.ImageBase != 0x400000) {
        std::printf("  (the original executable's base is not 0x400000: not used)\n");
        return false;
    }
    void *at = VirtualAlloc(reinterpret_cast<void *>(0x400000), size, std::getenv(kReservedVar) ? MEM_COMMIT : MEM_RESERVE | MEM_COMMIT,
                            PAGE_EXECUTE_READWRITE);
    if (at != reinterpret_cast<void *>(0x400000)) {
        MEMORY_BASIC_INFORMATION mbi = {};
        VirtualQuery(reinterpret_cast<void *>(0x400000), &mbi, sizeof mbi);
        std::printf("  (the original executable's range at 0x400000 is taken: not used; error %lu, state %lx type %lx size %lx, %s)\n",
                    GetLastError(), mbi.State, mbi.Type, static_cast<unsigned long>(mbi.RegionSize), std::getenv(kReservedVar) ? "held" : "not held");
        return false;
    }
    auto *base = static_cast<uint8_t *>(at);
    std::memcpy(base, file.data(), fileNt->OptionalHeader.SizeOfHeaders);
    const auto *sections = IMAGE_FIRST_SECTION(fileNt);
    for (int i = 0; i < fileNt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER &sec = sections[i];
        const uint32_t bytes = std::min(sec.SizeOfRawData, sec.Misc.VirtualSize ? sec.Misc.VirtualSize : sec.SizeOfRawData);
        if (sec.PointerToRawData + bytes <= file.size() && sec.VirtualAddress + bytes <= size)
            std::memcpy(base + sec.VirtualAddress, file.data() + sec.PointerToRawData, bytes);
    }
    // The game's DLLs (LS3DF.dll and what it needs) from its folder.
    SetDllDirectoryA(game.c_str());
    const unsigned int control = _controlfp(0, 0);   // (the DLLs reset the FPU: put the caller's precision back)
    // Resolve the imports (by name or ordinal) into its import address tables.
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int missing = 0;
    for (auto *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); imp->Name; ++imp) {
        const char *dll = reinterpret_cast<const char *>(base + imp->Name);
        HMODULE module = LoadLibraryA(dll);
        if (!module) {
            std::string path = game + "\\";
            path.append(dll, strnlen(dll, MAX_PATH));
            module = LoadLibraryA(path.c_str());
        }
        auto *thunk = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + imp->FirstThunk);
        const auto *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        for (; names->u1.AddressOfData; ++thunk, ++names) {
            FARPROC proc = nullptr;
            if (module) {
                if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
                    proc = GetProcAddress(module, reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(names->u1.Ordinal & 0xFFFF)));
                else
                    proc = GetProcAddress(module, reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData)->Name);
            }
            if (!proc)
                ++missing;
            thunk->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(proc));
        }
    }
    if (missing)
        std::printf("  (the original executable: %d imports not resolved; functions using them cannot be called)\n", missing);
    _controlfp(control, 0x000B031Fu);                      // (_MCW_PC | _MCW_RC | _MCW_EM)
    g_loaded = true;
    return true;
}

// The first five bytes of each function entry patched (RestoreOriginal puts them back).
struct S_SavedEntry {
    uint32_t at;
    uint8_t bytes[5];
};
static std::vector<S_SavedEntry> g_savedEntries;

void RedirectOriginal(uint32_t at, const void *to)
{
    uint8_t *p = OriginalAt<uint8_t *>(at);
    if (p[0] == 0xFF && p[1] == 0x25) {
        uint32_t slot;
        std::memcpy(&slot, p + 2, 4);
        *OriginalAt<const void **>(slot) = to;
        return;
    }
    if (std::none_of(g_savedEntries.begin(), g_savedEntries.end(), [at](const S_SavedEntry &e) { return e.at == at; })) {
        S_SavedEntry e{at, {}};
        std::memcpy(e.bytes, p, 5);
        g_savedEntries.push_back(e);
    }
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(p) + 5));
    p[0] = 0xE9;
    std::memcpy(p + 1, &rel, 4);
}

bool RestoreOriginal(uint32_t at)
{
    for (const S_SavedEntry &e : g_savedEntries)
        if (e.at == at) {
            std::memcpy(OriginalAt<uint8_t *>(at), e.bytes, 5);
            return true;
        }
    return false;
}
