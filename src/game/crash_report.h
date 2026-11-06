// Crash reporting (part 11e): the original's C_ExceptionHandler (0x006e2db0..0x006e4560), an unhandled exception filter installed before
// WinMain, and the game's handler for its report, OnFatalError 0x006e1ad0.
//
// On a crash the filter loads dbghelp.dll and writes into a 64 KB buffer: the machine, the exception, the registers and the call stack
// (each frame's symbol, line and local variables, when a .pdb is found beside the module). It hands the buffer to g_fatalErrorHandler,
// which appends it to <exe>.RPT next to the executable, and then calls the filter that was installed before.
//
// Differences from the original (each is marked where it is):
// - The report also went to a share of the developer's network (\\Intra\HD2_graf\CRASH_REPORTS): not done.
// - OnFatalError's buffer fits the report (the original's 10,000 bytes overflow on a long call stack, inside the crash handler).
// - The report's file is named from the last '.' of the file name, not the first '.' of the whole path.
// - SymGetLineFromAddr is checked with the other dbghelp functions, and a local variable that cannot be read no longer faults the filter.
#pragma once

#include <windows.h>

extern void (*g_fatalErrorHandler)(const char *report);  // 0x008bf0e4: set by WinMain
void OnFatalError(const char *report);                   // 0x006e1ad0

// The filter itself (0x006e3400; public for the test).
LONG WINAPI ReportUnhandledException(EXCEPTION_POINTERS *exception);

// The name of an exception code (0x006e3940): unknown ones are asked of NTDLL's messages (empty when it has none).
const char *ExceptionName(DWORD code);
// Module file, 1-based section and offset in it of an address (0x006e3c00); false, with a line in the report, when it is in no section.
bool GetLogicalAddress(const void *address, char *module, DWORD moduleSize, int *section, int *offset);

// The report so far (0x008af0c0), and adding to it (0x006e2df0): a line that does not fit is dropped.
const char *CrashReportText();
void ClearCrashReport();
void ReportLine(const char *format, ...);
