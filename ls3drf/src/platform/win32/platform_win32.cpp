// The Win32 platform (platform/platform.h): time, threads, locks and the cursor clip, the Windows
// calls IGraph made itself (IGraph.cpp), moved here unchanged. (The window is
// platform/null/window_null.cpp in the DLL; the Win32 window lives on in tests/win32 for the tests.)
#include "platform/platform.h"

#include <mmsystem.h>

namespace ls3d::platform {

// ---------------------------------------------------------------- time

uint32_t Milliseconds() { return timeGetTime(); }
uint32_t TickCount() { return GetTickCount(); }
void Pause(uint32_t ms) { Sleep(ms); }

// ---------------------------------------------------------------- threads and locks

// The handles are the Windows thread handles and critical sections.
Thread *StartThread(ThreadProc proc, void *param)
{
    DWORD threadId;
    return reinterpret_cast<Thread *>(CreateThread(nullptr, 0, proc, param, 0, &threadId));
}
void SetPriority(Thread *thread, int priority) { SetThreadPriority(reinterpret_cast<HANDLE>(thread), priority); }
void Suspend(Thread *thread) { SuspendThread(reinterpret_cast<HANDLE>(thread)); }
void Resume(Thread *thread) { ResumeThread(reinterpret_cast<HANDLE>(thread)); }
void Terminate(Thread *thread, uint32_t exitCode) { TerminateThread(reinterpret_cast<HANDLE>(thread), exitCode); }
void Join(Thread *thread) { WaitForSingleObject(reinterpret_cast<HANDLE>(thread), INFINITE); }
void Release(Thread *thread) { CloseHandle(reinterpret_cast<HANDLE>(thread)); }

Mutex *NewMutex()
{
    auto *section = new CRITICAL_SECTION;
    InitializeCriticalSection(section);
    return reinterpret_cast<Mutex *>(section);
}
void DeleteMutex(Mutex *mutex)
{
    auto *section = reinterpret_cast<CRITICAL_SECTION *>(mutex);
    DeleteCriticalSection(section);
    delete section;
}
void Lock(Mutex *mutex) { EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(mutex)); }
void Unlock(Mutex *mutex) { LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(mutex)); }

// ---------------------------------------------------------------- the mouse and keyboard

void ReleaseCursorClip() { ClipCursor(nullptr); }

}  // namespace ls3d::platform
