// See crash_report.h. All of it runs after the process has faulted, so it asks little of it: fixed buffers, no game state but the role.
#include "game/crash_report.h"

#include <dbghelp.h>
#include <tlhelp32.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/game_texts.h"
#include "game/pending.h"
#include "ls3d/igraph.h"

void (*g_fatalErrorHandler)(const char *report);

namespace {

const size_t REPORT_SIZE = 0x10000;
char s_report[REPORT_SIZE];                             // 0x008af0c0
char s_exceptionText[0x200];                            // 0x008bf128
LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter;          // 0x008bf0c0

const char *const LINE_OF_HASHES =
    "##################################################################################################################################\n";
const char *const UNKNOWN_NAME = "-Unknown-";

// dbghelp's functions: the dll is only looked for on a crash (0x008bf0e8...).
struct S_DbgHelp {
    HMODULE module;
    LPAPI_VERSION(WINAPI *ImagehlpApiVersion)();
    BOOL(WINAPI *SymCleanup)(HANDLE);
    PVOID(WINAPI *SymFunctionTableAccess)(HANDLE, DWORD);
    BOOL(WINAPI *SymGetLineFromAddr)(HANDLE, DWORD, PDWORD, PIMAGEHLP_LINE);
    DWORD(WINAPI *SymGetModuleBase)(HANDLE, DWORD);
    FARPROC SymGetModuleInfo;                           // (only looked for)
    FARPROC SymGetOptions;                              // (only looked for)
    BOOL(WINAPI *SymGetSymFromAddr)(HANDLE, DWORD, PDWORD, PIMAGEHLP_SYMBOL);
    BOOL(WINAPI *SymInitialize)(HANDLE, PCSTR, BOOL);
    DWORD(WINAPI *SymSetOptions)(DWORD);
    BOOL(WINAPI *StackWalk)(DWORD, HANDLE, HANDLE, LPSTACKFRAME, PVOID, PREAD_PROCESS_MEMORY_ROUTINE, PFUNCTION_TABLE_ACCESS_ROUTINE,
                            PGET_MODULE_BASE_ROUTINE, PTRANSLATE_ADDRESS_ROUTINE);
    FARPROC UnDecorateSymbolName;                       // (only looked for)
    DWORD(WINAPI *SymLoadModule)(HANDLE, HANDLE, PCSTR, PCSTR, DWORD, DWORD);
    BOOL(WINAPI *SymSetContext)(HANDLE, PIMAGEHLP_STACK_FRAME, PIMAGEHLP_CONTEXT);
    BOOL(WINAPI *SymEnumSymbols)(HANDLE, ULONG64, PCSTR, PSYM_ENUMERATESYMBOLS_CALLBACK, PVOID);
};
S_DbgHelp s_dbgHelp;

template <class T>
bool Find(T &function, const char *name)
{
    function = reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(s_dbgHelp.module, name)));
    return function != nullptr;
}

bool FindDbgHelpFunctions()
{
    // (All are looked for before any is tested, and the original tests all but SymGetLineFromAddr. Fix: that one too, it is called.)
    bool all = Find(s_dbgHelp.ImagehlpApiVersion, "ImagehlpApiVersion");
    all &= Find(s_dbgHelp.SymCleanup, "SymCleanup");
    all &= Find(s_dbgHelp.SymFunctionTableAccess, "SymFunctionTableAccess");
    all &= Find(s_dbgHelp.SymGetLineFromAddr, "SymGetLineFromAddr");
    all &= Find(s_dbgHelp.SymGetModuleBase, "SymGetModuleBase");
    all &= Find(s_dbgHelp.SymGetModuleInfo, "SymGetModuleInfo");
    all &= Find(s_dbgHelp.SymGetOptions, "SymGetOptions");
    all &= Find(s_dbgHelp.SymGetSymFromAddr, "SymGetSymFromAddr");
    all &= Find(s_dbgHelp.SymInitialize, "SymInitialize");
    all &= Find(s_dbgHelp.SymSetOptions, "SymSetOptions");
    all &= Find(s_dbgHelp.StackWalk, "StackWalk");
    all &= Find(s_dbgHelp.UnDecorateSymbolName, "UnDecorateSymbolName");
    all &= Find(s_dbgHelp.SymLoadModule, "SymLoadModule");
    all &= Find(s_dbgHelp.SymSetContext, "SymSetContext");
    all &= Find(s_dbgHelp.SymEnumSymbols, "SymEnumSymbols");
    return all;
}

// Each module's symbols: its .pdb, else the module itself (0x006e31b0; its list of modules, 0x006e2e80, is walked directly here).
void LoadSymbolFiles(HANDLE process, DWORD processId)
{
    ReportLine("Loading all symbol files...\n");
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;
    MODULEENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Module32First(snapshot, &entry); more; more = Module32Next(snapshot, &entry)) {
        char pdb[MAX_PATH + 1];
        std::strcpy(pdb, entry.szExePath);
        const size_t length = std::strlen(pdb);
        if (length >= 3)
            std::memcpy(pdb + length - 3, "pdb", 3);
        const DWORD base = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(entry.modBaseAddr));
        if (s_dbgHelp.SymLoadModule(process, nullptr, pdb, entry.szModule, base, entry.modBaseSize))
            ReportLine("\t%s\n", pdb);
        else
            s_dbgHelp.SymLoadModule(process, nullptr, entry.szExePath, entry.szModule, base, entry.modBaseSize);
    }
    CloseHandle(snapshot);
}

// A frame's local variable. The context is the fault's ESP.
BOOL CALLBACK ReportLocalVariable(PSYMBOL_INFO symbol, ULONG, PVOID context)   // 0x006e3cf0 (DS 0x0072e3a0)
{
    const DWORD address = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(context)) + static_cast<DWORD>(symbol->Address) - 8;
    // Fix: the original reads it directly, and a bad address faults inside the filter.
    DWORD value = 0;
    SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), &value, sizeof(value), &got);
    ReportLine("\t[%c,%08x] %-20s = %08x\n", (symbol->Flags & SYMFLAG_PARAMETER) ? 'P' : ' ', address, symbol->Name, value);
    return TRUE;
}

// (0x006e3bc0)
bool GetModuleOfAddress(const void *address, char *module, DWORD size)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(address, &info, sizeof(info)))
        return false;
    return GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase), module, size) != 0;
}

// (0x006e3d30)
void ReportCallStack(CONTEXT *context)
{
    STACKFRAME frame = {};
    frame.AddrPC.Offset = context->Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    ReportLine("\nCall stack:\n");

    while (s_dbgHelp.StackWalk(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &frame, context, nullptr,
                               s_dbgHelp.SymFunctionTableAccess, s_dbgHelp.SymGetModuleBase, nullptr)
           && frame.AddrFrame.Offset != 0) {
        char module[0x1000];
        std::strcpy(module, UNKNOWN_NAME);
        GetModuleOfAddress(reinterpret_cast<const void *>(frame.AddrPC.Offset), module, sizeof(module) - 1);

        DWORD displacement = 0;
        IMAGEHLP_LINE line = {};
        line.SizeOfStruct = sizeof(line);
        const BOOL hasLine = s_dbgHelp.SymGetLineFromAddr(GetCurrentProcess(), frame.AddrPC.Offset, &displacement, &line);

        union {
            IMAGEHLP_SYMBOL symbol;
            char bytes[0x218];
        } block = {};
        block.symbol.SizeOfStruct = sizeof(block);
        block.symbol.MaxNameLength = 0x200;
        if (!s_dbgHelp.SymGetSymFromAddr(GetCurrentProcess(), frame.AddrPC.Offset, &displacement, &block.symbol)) {
            ReportLine("  [0x%08X] (%s)\n", frame.AddrPC.Offset, module);
            continue;
        }
        if (!hasLine) {
            ReportLine("  [0x%08X] %s()+0x%X[line:???]  (%s)\n", frame.AddrPC.Offset, block.symbol.Name, displacement, module);
            continue;
        }
        ReportLine("  [0x%08X] %s()+0x%X[line:%i]  (%s, %s)\n", frame.AddrPC.Offset, block.symbol.Name, displacement, line.LineNumber,
                   line.FileName, module);

        IMAGEHLP_STACK_FRAME locals = {};
        locals.InstructionOffset = frame.AddrPC.Offset;
        locals.ReturnOffset = frame.AddrReturn.Offset;
        locals.FrameOffset = frame.AddrFrame.Offset;
        locals.StackOffset = frame.AddrStack.Offset;
        locals.FuncTableEntry = static_cast<LONG>(reinterpret_cast<DWORD_PTR>(frame.FuncTableEntry));
        for (int i = 0; i < 4; ++i)
            locals.Params[i] = frame.Params[i];
        locals.Virtual = frame.Virtual;
        if (s_dbgHelp.SymSetContext(GetCurrentProcess(), &locals, nullptr))
            s_dbgHelp.SymEnumSymbols(GetCurrentProcess(), 0, "[a-zA-Z0-9_]*", ReportLocalVariable, reinterpret_cast<PVOID>(static_cast<DWORD_PTR>(context->Esp)));
    }
}

// The exception, the registers and the call stack (0x006e3700).
void ReportException(EXCEPTION_POINTERS *exception)
{
    const EXCEPTION_RECORD *record = exception->ExceptionRecord;
    char machine[0x1000];
    std::strcpy(machine, UNKNOWN_NAME);
    DWORD machineSize = sizeof(machine);
    GetComputerNameA(machine, &machineSize);

    SYSTEMTIME time;
    GetLocalTime(&time);
    char date[0x40];
    std::sprintf(date, "%2.2d.%2.2d.%4.4d  (%2.2d:%2.2d:%2.2d)", time.wDay, time.wMonth, time.wYear, time.wHour, time.wMinute, time.wSecond);
    // (The build's date and time as they stand in the original.)
    ReportLine("Machine: \"%s\"  LS3DVer: %d  BuildID: [%s %s]  Date: %s \nException code: %08X %s\n", machine, GetEngineVersion(), "Aug 18 2004",
               "16:38:15", date, record->ExceptionCode, ExceptionName(record->ExceptionCode));

    char module[0x1000];
    std::strcpy(module, UNKNOWN_NAME);
    int section = 0, offset = 0;
    if (GetLogicalAddress(record->ExceptionAddress, module, sizeof(module), &section, &offset))
        ReportLine("Fault address:  %08X %02X:%08X %s\n", record->ExceptionAddress, section, offset, module);
    else
        ReportLine("Fault address:  %08X ??:???????? %s\n", record->ExceptionAddress, module);

    CONTEXT *context = exception->ContextRecord;
    ReportLine("\n[EAX=%08X] [EBX=%08X] [ECX=%08X] [EDX=%08X] [ESI=%08X] [EDI=%08X]\n", context->Eax, context->Ebx, context->Ecx, context->Edx,
               context->Esi, context->Edi);
    ReportLine("[CS:EIP=%04X:%08X] [SS:ESP=%04X:%08X] [EBP=%08X]\n[DS=%04X] [ES=%04X] [FS=%04X] [GS=%04X]\n", context->SegCs, context->Eip,
               context->SegSs, context->Esp, context->Ebp, context->SegDs, context->SegEs, context->SegFs, context->SegGs);
    ReportLine("Flags:%08X\n", context->EFlags);
    ReportCallStack(context);
    ReportLine("\n");
}

void ReportWithDbgHelp(EXCEPTION_POINTERS *exception)
{
    s_dbgHelp.module = LoadLibraryA("dbghelp.dll");
    if (!s_dbgHelp.module) {
        ReportLine("C_ExceptionHandler::C_ExceptionHandler() - Error LoadLibrary( \"owndbghelp.dll\" ): GetLastError() = %lu\n", GetLastError());
        return;
    }
    if (!FindDbgHelpFunctions()) {
        ReportLine("C_ExceptionHandler::C_ExceptionHandler() - GetProcAddress(): some required function not found.");
        FreeLibrary(s_dbgHelp.module);
        return;
    }
    s_dbgHelp.SymSetOptions(0x112);                     // SYMOPT_UNDNAME, SYMOPT_LOAD_LINES, SYMOPT_NO_UNQUALIFIED_LOADS
    s_dbgHelp.SymInitialize(GetCurrentProcess(), nullptr, FALSE);
    LoadSymbolFiles(GetCurrentProcess(), GetCurrentProcessId());
    const LPAPI_VERSION version = s_dbgHelp.ImagehlpApiVersion();
    ReportLine("Using ImageHlp API version: %i.%i.%i\n", version->MajorVersion, version->MinorVersion, version->Revision);
    ReportException(exception);
    s_dbgHelp.SymCleanup(GetCurrentProcess());
    FreeLibrary(s_dbgHelp.module);
}

// Installed before WinMain, removed after it (0x006e33d0, 0x006e33f0).
struct S_FilterInstaller {
    S_FilterInstaller() { s_previousFilter = SetUnhandledExceptionFilter(ReportUnhandledException); }
    ~S_FilterInstaller() { SetUnhandledExceptionFilter(s_previousFilter); }
};
S_FilterInstaller s_filterInstaller;                    // 0x008bf0e0

// Adds one copy of the report to a file (the ends of OnFatalError).
void AppendToFile(const char *path, const char *text)
{
    FILE *file = std::fopen(path, "at");
    if (!file)
        return;
    std::fseek(file, 0, SEEK_END);
    std::fwrite(text, 1, std::strlen(text), file);
    std::fclose(file);
}

}  // namespace

// (For the test: stands in for the filter found when installing.)
LPTOP_LEVEL_EXCEPTION_FILTER CrashPreviousFilterForTest(LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
    const LPTOP_LEVEL_EXCEPTION_FILTER before = s_previousFilter;
    s_previousFilter = filter;
    return before;
}

const char *CrashReportText()
{
    return s_report;
}

void ClearCrashReport()
{
    s_report[0] = '\0';
}

void ReportLine(const char *format, ...)
{
    char line[0x4000];
    va_list args;
    va_start(args, format);
    wvsprintfA(line, format, args);
    va_end(args);
    const size_t used = std::strlen(s_report);
    const size_t added = std::strlen(line);
    if (used + added < REPORT_SIZE - 1)
        std::memcpy(s_report + used, line, added + 1);
}

const char *ExceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case EXCEPTION_GUARD_PAGE: return "GUARD_PAGE";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_SINGLE_STEP: return "SINGLE_STEP";
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_INVALID_HANDLE: return "INVALID_HANDLE";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_INVALID_DISPOSITION: return "INVALID_DISPOSITION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT: return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION: return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW: return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK: return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW: return "FLT_UNDERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "INT_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    default: break;
    }
    FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, GetModuleHandleA("NTDLL.DLL"), code, 0, s_exceptionText,
                   sizeof(s_exceptionText), nullptr);
    return s_exceptionText;
}

bool GetLogicalAddress(const void *address, char *module, DWORD moduleSize, int *section, int *offset)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(address, &info, sizeof(info))) {
        ReportLine("C_ExceptionHandler::GetLogicalAddress() : generic error 1\n");
        return false;
    }
    if (!GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase), module, moduleSize)) {
        ReportLine("C_ExceptionHandler::GetLogicalAddress() : generic error 2 ( GetLastError() = %i )\n", GetLastError());
        return false;
    }
    if (info.AllocationBase) {
        const char *base = static_cast<const char *>(info.AllocationBase);
        const IMAGE_NT_HEADERS *headers = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
        const IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(headers);
        const DWORD rva = static_cast<DWORD>(static_cast<const char *>(address) - base);
        for (unsigned i = 0; i < headers->FileHeader.NumberOfSections; ++i) {
            const DWORD start = sections[i].VirtualAddress;
            const DWORD size = sections[i].Misc.VirtualSize > sections[i].SizeOfRawData ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
            if (start <= rva && rva <= start + size) {
                *section = static_cast<int>(i) + 1;
                *offset = static_cast<int>(rva - start);
                return true;
            }
        }
    }
    ReportLine("C_ExceptionHandler::GetLogicalAddress() : generic error 3\n");
    return false;
}

LONG WINAPI ReportUnhandledException(EXCEPTION_POINTERS *exception)   // 0x006e3400 (DS 0x0072d880)
{
    ClearCrashReport();
    ReportLine("\n%s", LINE_OF_HASHES);
    ReportLine("C_ExceptionHandler::UnhandledExceptionFilter() : exception raised\n");
    ReportLine("%s", LINE_OF_HASHES);
    ReportWithDbgHelp(exception);
    ReportLine("%s", LINE_OF_HASHES);
    if (g_fatalErrorHandler)
        g_fatalErrorHandler(s_report);
#ifdef HD2_DEDICATED   // the server ends there, without the previous filter
    _exit(1);
#else
    if (s_previousFilter)
        return s_previousFilter(exception);
    return EXCEPTION_CONTINUE_SEARCH;
#endif
}

void OnFatalError(const char *report)
{
    static char s_buffer[REPORT_SIZE + 64];             // (fix: the original's new char[10000] overflows on a long report)
    static char s_path[MAX_PATH];                       // 0x008aefa0
    if (!report)
        report = "### NO crash info generated ###";
    std::strcpy(s_buffer, report);
    std::strcat(s_buffer, "\r\n");
    std::strcat(s_buffer, "RELEASE - ");
    std::strcat(s_buffer, (g_gameRole & 7) == 7 ? "SINGLEPLAYER" : "MULTIPLAYER");
    dbgPrintf2("%s", s_buffer);                         // (the original passes the report as the format)

    GetModuleFileNameA(nullptr, s_path, sizeof(s_path));
    // Fix: the file name's last '.' (the original's is the first of the whole path, so a '.' in a directory's name cut the path there).
    char *fileName = std::strrchr(s_path, '\\');
    fileName = fileName ? fileName + 1 : s_path;
    char *dot = std::strrchr(fileName, '.');
    if (dot && std::strlen(dot + 1) >= 3)
        std::strcpy(dot + 1, "RPT");
    AppendToFile(s_path, s_buffer);
    // Left out: the second copy, to \\Intra\HD2_graf\CRASH_REPORTS\<file name>, the developer's own network share.
}

// The crash report dialog: its explanation and the report (`lParam`). OK (0) sends it; Cancel or closing (1) does not.
INT_PTR CALLBACK CrashReportProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)   // 0x00731e10 (DS 0x0078ca60)
{
    switch (message) {
    case WM_CLOSE:
        EndDialog(dialog, 1);
        break;
    case WM_INITDIALOG:
        SetDlgItemTextA(dialog, 0x3FC, g_texts.Get(0x578).c_str());
        SetDlgItemTextA(dialog, 0x3FA, reinterpret_cast<LPCSTR>(lParam));
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
            EndDialog(dialog, 0);
        else if (LOWORD(wParam) == IDCANCEL)
            EndDialog(dialog, 1);
        break;
    default:
        break;
    }
    return FALSE;
}
