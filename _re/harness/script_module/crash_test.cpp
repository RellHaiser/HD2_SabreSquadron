// Crash reporting (part 11e: game/crash_report.cpp). MinGW; build_crash.bat; run crash_test.exe ("crash: N checks, 0 failures").
// Parts are direct calls; the end runs itself as a child that really faults, and reads the report the child left beside the exe.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game/crash_report.h"
#include "game/game_texts.h"
#include "game/pending.h"
#include "ls3d/igraph.h"

uint32_t g_gameRole;
// The crash report dialog's text (CrashReportProc, not run here).
C_GameTexts g_texts;
C_str C_GameTexts::Get(int) const { return C_str(); }
extern "C" uint32_t GetEngineVersion() { return 1234; }
static std::string g_debugText;
extern "C" void dbgPrintf2(const char *format, ...)
{
    char text[0x11000];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    g_debugText = text;
}

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

bool Has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

std::string ReportFile()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, sizeof(path));
    std::strcpy(std::strrchr(path, '.') + 1, "RPT");
    return path;
}
std::string ReadAll(const std::string &path)
{
    std::string text;
    if (FILE *file = std::fopen(path.c_str(), "rb")) {
        char block[4096];
        size_t got;
        while ((got = std::fread(block, 1, sizeof(block), file)) != 0)
            text.append(block, got);
        std::fclose(file);
    }
    return text;
}

void TestNames()
{
    CHECK(!std::strcmp(ExceptionName(0xC0000005), "ACCESS_VIOLATION"));
    CHECK(!std::strcmp(ExceptionName(0xC0000094), "INT_DIVIDE_BY_ZERO"));
    CHECK(!std::strcmp(ExceptionName(0xC00000FD), "STACK_OVERFLOW"));
    CHECK(!std::strcmp(ExceptionName(0x80000003), "BREAKPOINT"));
    CHECK(!std::strcmp(ExceptionName(0xC000008E), "FLT_DIVIDE_BY_ZERO"));
    CHECK(!std::strcmp(ExceptionName(0xC0000006), "IN_PAGE_ERROR"));
    CHECK(!std::strcmp(ExceptionName(0xC000001D), "ILLEGAL_INSTRUCTION"));
    CHECK(!std::strcmp(ExceptionName(0xC0000096), "PRIV_INSTRUCTION"));
    // Not among them: NTDLL's message.
    CHECK(std::strlen(ExceptionName(0xC0000022)) > 0);
}

void TestReportLimit()
{
    ClearCrashReport();
    CHECK(CrashReportText()[0] == '\0');
    ReportLine("a=%d %s\n", 5, "b");
    CHECK(!std::strcmp(CrashReportText(), "a=5 b\n"));
    // Full: a line that would not fit is dropped whole, a smaller one is still added.
    std::string big(1000, 'x');
    for (int i = 0; i < 200; ++i)
        ReportLine("%s", big.c_str());
    const size_t length = std::strlen(CrashReportText());
    CHECK(length < 0xFFFF && length > 0xFFFF - 1000);
    ReportLine("%s", big.c_str());
    CHECK(std::strlen(CrashReportText()) == length);
    ReportLine("yz");
    CHECK(std::strlen(CrashReportText()) == length + 2);
    ClearCrashReport();
}

void TestLogicalAddress()
{
    char module[MAX_PATH] = "-";
    int section = 0, offset = 0;
    ClearCrashReport();
    CHECK(GetLogicalAddress(reinterpret_cast<const void *>(&TestNames), module, sizeof(module), &section, &offset));
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, sizeof(self));
    CHECK(!std::strcmp(module, self));
    CHECK(section == 1);                                // (.text)
    const char *base = reinterpret_cast<const char *>(GetModuleHandleA(nullptr));
    const IMAGE_NT_HEADERS *headers = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(headers);
    CHECK(offset == reinterpret_cast<const char *>(&TestNames) - base - static_cast<int>(sections[0].VirtualAddress));
    // The second section's offset counts from that section.
    CHECK(GetLogicalAddress(base + sections[1].VirtualAddress + 8, module, sizeof(module), &section, &offset));
    CHECK(section == 2 && offset == 8);
    CHECK(CrashReportText()[0] == '\0');
    // In no module: a line.
    CHECK(!GetLogicalAddress(reinterpret_cast<const void *>(0x10), module, sizeof(module), &section, &offset));
    CHECK(Has(CrashReportText(), "generic error 1") || Has(CrashReportText(), "generic error 2") || Has(CrashReportText(), "generic error 3"));
    CHECK(Has(CrashReportText(), "GetLogicalAddress() : generic error"));
    ClearCrashReport();
}

void TestFatalErrorFile()
{
    const std::string path = ReportFile();
    DeleteFileA(path.c_str());
    g_gameRole = 7;
    OnFatalError("first");
    std::string text = ReadAll(path);
    CHECK(text.find("first\r\r\nRELEASE - SINGLEPLAYER") == 0 || text.find("first\r\nRELEASE - SINGLEPLAYER") == 0);
    CHECK(Has(g_debugText, "first") && Has(g_debugText, "RELEASE - SINGLEPLAYER"));
    // Appended; the role's other values are multiplayer; no report.
    g_gameRole = 3;
    OnFatalError(nullptr);
    text = ReadAll(path);
    CHECK(Has(text, "first") && Has(text, "### NO crash info generated ###") && Has(text, "RELEASE - MULTIPLAYER"));
    g_gameRole = 0xF8;
    OnFatalError("third");
    CHECK(Has(ReadAll(path), "third"));
    CHECK(Has(g_debugText, "MULTIPLAYER"));
    g_gameRole = 0xF;
    OnFatalError("fourth");
    CHECK(Has(g_debugText, "SINGLEPLAYER"));
    // A report as long as the buffer goes whole (the original overflows at 10,000 bytes). A '%' is plain text.
    DeleteFileA(path.c_str());
    std::string big(0xFFFE, 'r');
    big[100] = '%';
    OnFatalError(big.c_str());
    text = ReadAll(path);
    CHECK(text.size() >= big.size() && text.compare(0, big.size(), big) == 0);
    CHECK(g_debugText.size() >= big.size());
    DeleteFileA(path.c_str());
}

// ---- a real crash, in a child ----
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
int FaultingFunction(volatile int *address)
{
    return *address = 7;
}

int ChildCrash()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    g_fatalErrorHandler = OnFatalError;
    g_gameRole = 7;
    return FaultingFunction(reinterpret_cast<volatile int *>(0x10));
}
int ChildNoHandler()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    g_fatalErrorHandler = nullptr;
    return FaultingFunction(reinterpret_cast<volatile int *>(0x10));
}
LONG WINAPI PreviousFilter(EXCEPTION_POINTERS *) { ExitProcess(0x5150); }

DWORD RunChild(const char *argument)
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, sizeof(path));
    char command[MAX_PATH + 32];
    std::sprintf(command, "\"%s\" %s", path, argument);
    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessA(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        return 0xFFFFFFFF;
    WaitForSingleObject(process.hProcess, 60000);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

void TestRealCrash()
{
    const std::string path = ReportFile();
    DeleteFileA(path.c_str());
    const DWORD code = RunChild("crash");
    CHECK(code == 0xC0000005);                          // (the filter passes it on: the process dies of it)
    const std::string text = ReadAll(path);
    CHECK(Has(text, "C_ExceptionHandler::UnhandledExceptionFilter() : exception raised"));
    CHECK(Has(text, "Loading all symbol files..."));
    CHECK(Has(text, "Using ImageHlp API version: "));
    CHECK(Has(text, "LS3DVer: 1234  BuildID: [Aug 18 2004 16:38:15]"));
    CHECK(Has(text, "Exception code: C0000005 ACCESS_VIOLATION"));
    CHECK(Has(text, "Fault address:  ") && Has(text, " 01:") && Has(text, ".exe"));
    CHECK(Has(text, "[EAX=") && Has(text, "[EDI=") && Has(text, "[CS:EIP=") && Has(text, "[SS:ESP=") && Has(text, "[EBP=") && Has(text, "[GS="));
    CHECK(Has(text, "Flags:"));
    CHECK(Has(text, "Call stack:"));
    CHECK(Has(text, "  [0x"));
    CHECK(Has(text, "RELEASE - SINGLEPLAYER"));
    // The frames name their module; the date is today's; the machine is named; three lines of hashes and the blank line before the last.
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, sizeof(self));
    CHECK(Has(text, ("(" + std::string(self) + ")").c_str()) || Has(text, (", " + std::string(self) + ")").c_str()));
    SYSTEMTIME now;
    GetLocalTime(&now);
    char date[32];
    std::sprintf(date, "Date: %2.2d.%2.2d.%4.4d  (", now.wDay, now.wMonth, now.wYear);
    CHECK(Has(text, date));
    char machine[256];
    DWORD machineSize = sizeof(machine);
    GetComputerNameA(machine, &machineSize);
    CHECK(Has(text, ("Machine: \"" + std::string(machine) + "\"").c_str()));
    size_t hashes = 0;
    for (size_t at = text.find("#\r\n"); at != std::string::npos; at = text.find("#\r\n", at + 1))
        ++hashes;
    CHECK(hashes == 3);
    CHECK(text.compare(0, 2, "\r\n") == 0 || text[0] == '\n');
#ifdef _MSC_VER
    // With a .pdb beside the exe (MSVC): the symbols loaded, the fault's own frame with its line and its parameter.
    CHECK(Has(text, ".pdb\n") || Has(text, ".pdb\r\n"));
    CHECK(Has(text, "FaultingFunction()+0x") && Has(text, "[line:"));
    CHECK(Has(text, "crash_test.cpp"));
    CHECK(Has(text, "\t[P,") && Has(text, "address"));
    CHECK(Has(text, "ChildCrash()+0x"));
#endif
    // The fault's own address starts the stack, and the registers are those of the fault.
    const size_t eip = text.find("[CS:EIP=");
    CHECK(eip != std::string::npos);
    if (eip != std::string::npos) {
        const std::string address = text.substr(eip + 13, 8);
        CHECK(Has(text, ("Fault address:  " + address).c_str()));
        CHECK(Has(text, ("  [0x" + address).c_str()));
    }
    // Without a handler nothing is written, and it still dies of it.
    DeleteFileA(path.c_str());
    CHECK(RunChild("nohandler") == 0xC0000005);
    CHECK(ReadAll(path).empty());
    // The filter that stood before is called last, after the report.
    DeleteFileA(path.c_str());
    CHECK(RunChild("previous") == 0x5150);
    CHECK(Has(ReadAll(path), "ACCESS_VIOLATION"));
    DeleteFileA(path.c_str());
}
}  // namespace

LPTOP_LEVEL_EXCEPTION_FILTER CrashPreviousFilterForTest(LPTOP_LEVEL_EXCEPTION_FILTER filter);

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 1 && !std::strcmp(argv[1], "crash"))
        return ChildCrash();
    if (argc > 1 && !std::strcmp(argv[1], "nohandler"))
        return ChildNoHandler();
    if (argc > 1 && !std::strcmp(argv[1], "previous")) {
        // (Stands in for the filter found when installing: the handler's own is replaced, and calls this after reporting.)
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        g_fatalErrorHandler = OnFatalError;
        CrashPreviousFilterForTest(PreviousFilter);
        return FaultingFunction(reinterpret_cast<volatile int *>(0x10));
    }
    // Installed before main, as the original's.
    LPTOP_LEVEL_EXCEPTION_FILTER installed = SetUnhandledExceptionFilter(nullptr);
    CHECK(installed == ReportUnhandledException);
    SetUnhandledExceptionFilter(installed);

    TestNames();
    TestReportLimit();
    TestLogicalAddress();
    TestFatalErrorFile();
    TestRealCrash();
    std::printf("crash: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
