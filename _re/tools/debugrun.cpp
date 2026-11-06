// A small debugger for running the port: starts the game under the Win32 debug API and logs what a person at a debugger would look at first.
// DLL loads, OutputDebugString, exceptions with registers and a symbolized stack (dbghelp + the exe's PDB; the DLLs' exports), the end of the process.
// After --timeout seconds every thread's stack is logged and the game is killed (a hang's picture; or how far it got).
//
// Build: cl /nologo /EHsc /W3 /Zi debugrun.cpp dbghelp.lib user32.lib psapi.lib  (x86: the game is 32 bits)
// Usage: debugrun [--timeout seconds] [--log file] [--first-chance N] [--stay] <exe> [arguments]
//   --first-chance N  full report of the first N first-chance exceptions (default 20)
//   --stay            at a crash leave the process suspended until the timeout instead of letting it die
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>

static FILE *g_log = stdout;

static void Log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(g_log, format, args);
    va_end(args);
    fflush(g_log);
}

static HANDLE g_process;
static std::map<DWORD, HANDLE> g_threads;
static std::map<DWORD64, std::string> g_modules;

static std::string ModuleOf(DWORD64 address)
{
    auto it = g_modules.upper_bound(address);
    if (it == g_modules.begin())
        return "?";
    --it;
    return it->second;
}

static std::string Describe(DWORD64 address)
{
    char text[1024];
    char buffer[sizeof(SYMBOL_INFO) + 512] = {};
    SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 512;
    DWORD64 offset = 0;
    const std::string module = ModuleOf(address);
    if (SymFromAddr(g_process, address, &offset, symbol)) {
        IMAGEHLP_LINE64 line = {sizeof(line)};
        DWORD lineOffset = 0;
        if (SymGetLineFromAddr64(g_process, address, &lineOffset, &line)) {
            const char *file = strrchr(line.FileName, '\\');
            sprintf_s(text, "%08llx %s!%s+0x%llx (%s:%lu)", address, module.c_str(), symbol->Name, offset, file ? file + 1 : line.FileName, line.LineNumber);
        } else
            sprintf_s(text, "%08llx %s!%s+0x%llx", address, module.c_str(), symbol->Name, offset);
    } else
        sprintf_s(text, "%08llx %s", address, module.c_str());
    return text;
}

static void DumpStack(HANDLE thread, const CONTEXT &original, int limit)
{
    CONTEXT context = original;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < limit; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, g_process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (!frame.AddrPC.Offset)
            break;
        Log("  #%02d %s\n", i, Describe(frame.AddrPC.Offset).c_str());
    }
}

static void DumpContext(HANDLE thread)
{
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_FULL | CONTEXT_FLOATING_POINT;
    if (!GetThreadContext(thread, &context)) {
        Log("  (no context: %lu)\n", GetLastError());
        return;
    }
    Log("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n  eip=%08lx esp=%08lx ebp=%08lx efl=%08lx fcw=%04lx fsw=%04lx\n", context.Eax, context.Ebx, context.Ecx, context.Edx,
        context.Esi, context.Edi, context.Eip, context.Esp, context.Ebp, context.EFlags, context.FloatSave.ControlWord & 0xFFFF, context.FloatSave.StatusWord & 0xFFFF);
    unsigned char code[16] = {};
    SIZE_T got = 0;
    if (ReadProcessMemory(g_process, reinterpret_cast<void *>(static_cast<size_t>(context.Eip)), code, sizeof code, &got) && got) {
        Log("  code:");
        for (SIZE_T i = 0; i < got; ++i)
            Log(" %02x", code[i]);
        Log("\n");
    }
    DWORD words[8] = {};
    if (ReadProcessMemory(g_process, reinterpret_cast<void *>(static_cast<size_t>(context.Esp)), words, sizeof words, &got) && got) {
        Log("  stack:");
        for (SIZE_T i = 0; i < got / 4; ++i)
            Log(" %08lx", words[i]);
        Log("\n");
    }
    DumpStack(thread, context, 40);
}

static std::string ReadRemoteName(const LOAD_DLL_DEBUG_INFO &info, HANDLE file)
{
    char path[MAX_PATH] = {};
    if (file && GetFinalPathNameByHandleA(file, path, sizeof path, FILE_NAME_NORMALIZED)) {
        const char *slash = strrchr(path, '\\');
        return slash ? slash + 1 : path;
    }
    (void)info;
    return "?";
}

static const char *ExceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "float divide by zero";
    case EXCEPTION_FLT_INVALID_OPERATION: return "float invalid operation";
    case 0xE06D7363: return "C++ exception";
    case 0xC0000374: return "heap corruption";
    case 0xC0000409: return "stack buffer overrun or fast fail";
    case 0x406D1388: return "thread name";
    default: return "?";
    }
}

int main(int argc, char **argv)
{
    int timeoutSeconds = 0;
    int firstChanceReports = 20;
    bool stay = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; ++i) {
        const std::string option = argv[i];
        if (option == "--timeout")
            timeoutSeconds = atoi(argv[++i]);
        else if (option == "--log")
            fopen_s(&g_log, argv[++i], "w");
        else if (option == "--first-chance")
            firstChanceReports = atoi(argv[++i]);
        else if (option == "--stay")
            stay = true;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: debugrun [--timeout s] [--log file] [--first-chance N] [--stay] exe [args]\n");
        return 2;
    }
    const std::string exe = argv[i];
    std::string commandLine = "\"" + exe + "\"";
    for (int a = i + 1; a < argc; ++a)
        commandLine += std::string(" ") + argv[a];
    std::string directory = exe;
    directory.erase(directory.find_last_of("\\/"));

    // The game turns off the Sticky Keys hot key and restores it on a clean exit only; a run killed here would leave it off until the next sign-in.
    STICKYKEYS stickyKeys = {sizeof(STICKYKEYS), 0};
    SystemParametersInfoA(SPI_GETSTICKYKEYS, sizeof(stickyKeys), &stickyKeys, 0);

    STARTUPINFOA startup = {sizeof(startup)};
    PROCESS_INFORMATION info = {};
    if (!CreateProcessA(exe.c_str(), &commandLine[0], nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS, nullptr, directory.c_str(), &startup, &info)) {
        Log("CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }
    g_process = info.hProcess;
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS);
    SymInitialize(g_process, directory.c_str(), FALSE);

    const ULONGLONG start = GetTickCount64();
    bool crashed = false;
    bool timedOut = false;
    bool loaderBreakSeen = false;
    DWORD exitCode = 0;
    int firstChanceTotal = 0;
    for (;;) {
        DEBUG_EVENT event;
        const DWORD wait = timeoutSeconds ? 500 : INFINITE;
        const BOOL got = WaitForDebugEvent(&event, wait);
        // (Also when events keep coming: a game raising exceptions every frame never lets the wait run out.)
        if (timeoutSeconds && GetTickCount64() - start > static_cast<ULONGLONG>(timeoutSeconds) * 1000) {
            DebugBreakProcess(g_process);       // Everything stops at its next event, the break.
            timeoutSeconds = 0;
            timedOut = true;
            Log("---- timeout: threads\n");
        }
        if (!got)
            continue;
        DWORD status = DBG_CONTINUE;
        const ULONG seconds = static_cast<ULONG>((GetTickCount64() - start) / 1000);
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT: {
            g_threads[event.dwThreadId] = event.u.CreateProcessInfo.hThread;
            g_modules[reinterpret_cast<size_t>(event.u.CreateProcessInfo.lpBaseOfImage)] = "exe";
            SymLoadModuleEx(g_process, event.u.CreateProcessInfo.hFile, exe.c_str(), nullptr, reinterpret_cast<size_t>(event.u.CreateProcessInfo.lpBaseOfImage), 0, nullptr, 0);
            Log("[%lus] process %lu at %p\n", seconds, event.dwProcessId, event.u.CreateProcessInfo.lpBaseOfImage);
            {
                IMAGEHLP_MODULE64 module = {sizeof(module)};
                if (SymGetModuleInfo64(g_process, reinterpret_cast<size_t>(event.u.CreateProcessInfo.lpBaseOfImage), &module))
                    Log("symbols: type %d, %s\n", static_cast<int>(module.SymType), module.LoadedPdbName);
                else
                    Log("symbols: none (%lu)\n", GetLastError());
            }
            break;
        }
        case CREATE_THREAD_DEBUG_EVENT:
            g_threads[event.dwThreadId] = event.u.CreateThread.hThread;
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            g_threads.erase(event.dwThreadId);
            break;
        case LOAD_DLL_DEBUG_EVENT: {
            const std::string name = ReadRemoteName(event.u.LoadDll, event.u.LoadDll.hFile);
            g_modules[reinterpret_cast<size_t>(event.u.LoadDll.lpBaseOfDll)] = name;
            SymLoadModuleEx(g_process, event.u.LoadDll.hFile, name.c_str(), nullptr, reinterpret_cast<size_t>(event.u.LoadDll.lpBaseOfDll), 0, nullptr, 0);
            Log("[%lus] dll %p %s\n", seconds, event.u.LoadDll.lpBaseOfDll, name.c_str());
            if (event.u.LoadDll.hFile)
                CloseHandle(event.u.LoadDll.hFile);
            break;
        }
        case OUTPUT_DEBUG_STRING_EVENT: {
            const OUTPUT_DEBUG_STRING_INFO &text = event.u.DebugString;
            if (!text.fUnicode) {
                std::string buffer(text.nDebugStringLength, '\0');
                SIZE_T got = 0;
                ReadProcessMemory(g_process, text.lpDebugStringData, &buffer[0], buffer.size(), &got);
                Log("[%lus] dbg: %s", seconds, buffer.c_str());
                if (buffer.empty() || buffer[strlen(buffer.c_str()) - 1] != '\n')
                    Log("\n");
            }
            break;
        }
        case EXCEPTION_DEBUG_EVENT: {
            const EXCEPTION_RECORD &record = event.u.Exception.ExceptionRecord;
            const bool first = event.u.Exception.dwFirstChance != 0;
            const DWORD code = record.ExceptionCode;
            status = DBG_EXCEPTION_NOT_HANDLED;
            if (code == EXCEPTION_BREAKPOINT) {
                status = DBG_CONTINUE;
                if (timedOut) {
                    // Our break at the timeout: every thread's stack, then the end.
                    for (const auto &thread : g_threads) {
                        Log("thread %lu:\n", thread.first);
                        DumpContext(thread.second);
                    }
                    TerminateProcess(g_process, 0xDEAD);
                } else if (loaderBreakSeen) {
                    // A __debugbreak of the program's: a run-time check.
                    Log("[%lus] breakpoint at %s thread %lu\n", seconds, Describe(reinterpret_cast<size_t>(record.ExceptionAddress)).c_str(), event.dwThreadId);
                    DumpContext(g_threads[event.dwThreadId]);
                    crashed = true;
                    TerminateProcess(g_process, code);
                }
                loaderBreakSeen = true;
                break;
            }
            if (code == 0x406D1388 || code == 0x4001000A || code == 0x40010006) {   // Thread names and debug strings raised as exceptions.
                break;
            }
            ++firstChanceTotal;
            if (!first || firstChanceTotal <= firstChanceReports) {
                Log("[%lus] %s exception %08lx (%s) at %s thread %lu\n", seconds, first ? "first-chance" : "UNHANDLED", code, ExceptionName(code),
                    Describe(reinterpret_cast<size_t>(record.ExceptionAddress)).c_str(), event.dwThreadId);
                if (code == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
                    Log("  %s of %08llx\n", record.ExceptionInformation[0] == 0 ? "read" : record.ExceptionInformation[0] == 1 ? "write" : "execute", static_cast<unsigned long long>(record.ExceptionInformation[1]));
                DumpContext(g_threads[event.dwThreadId]);
            }
            if (!first) {
                crashed = true;
                if (stay) {
                    Log("(left suspended; kill it or wait for the timeout)\n");
                    Sleep(static_cast<DWORD>(timeoutSeconds ? timeoutSeconds * 1000 : 600000));
                }
                TerminateProcess(g_process, code);
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            exitCode = event.u.ExitProcess.dwExitCode;
            Log("[%lus] process ended, code %ld (0x%08lx)%s\n", seconds, static_cast<long>(exitCode), exitCode, crashed ? " after a crash" : "");
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
            SystemParametersInfoA(SPI_SETSTICKYKEYS, sizeof(stickyKeys), &stickyKeys, 0);
            SymCleanup(g_process);
            return crashed ? 3 : 0;
        default:
            break;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
    }
}
