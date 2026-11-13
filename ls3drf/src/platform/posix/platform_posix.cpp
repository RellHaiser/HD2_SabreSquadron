// The POSIX platform (platform/platform.h): Linux, the BSDs and macOS. A server engine needs no
// cursor or message box; the clocks are the monotonic ones.
#include "platform/platform.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <sched.h>

#if defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#if __has_include(<sys/utsname.h>)
#include <sys/utsname.h>
#define LS3D_HAVE_UNAME 1
#endif

namespace ls3d::platform {

namespace {

uint64_t MonotonicNs()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000000000u + static_cast<uint64_t>(t.tv_nsec);
}

}  // namespace

// ---------------------------------------------------------------- time

// (Wraps after 49.7 days, like timeGetTime; callers compare differences.)
uint32_t Milliseconds() { return static_cast<uint32_t>(MonotonicNs() / 1000000u); }

void Pause(uint32_t ms)
{
    if (!ms) {
        sched_yield();
        return;
    }
    timespec t = {static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) == -1 && errno == EINTR) {
    }
}

uint64_t CounterFrequency() { return 1000000000u; }      // (MonotonicNs's unit)

// ---------------------------------------------------------------- the mouse

void ReleaseCursorClip() {}

// ---------------------------------------------------------------- the process

// The executable's directory where the system tells it; otherwise the current directory.
std::string LogDirectory()
{
    std::string path;
#if defined(__linux__)
    char buffer[4096];
    const ssize_t n = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (n > 0)
        path.assign(buffer, static_cast<size_t>(n));
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof buffer;
    if (_NSGetExecutablePath(buffer, &size) == 0)
        path = buffer;
#endif
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

int StopMessage(const char *caption, const char *text)
{
    std::fprintf(stderr, "%s: %s\n", caption ? caption : "", text ? text : "");
    std::fflush(stderr);
    return 1;                           // IDOK
}

void InitSystem() {}

OsVersion GetOsVersion()
{
    OsVersion v;
#ifdef LS3D_HAVE_UNAME
    utsname u;
    if (uname(&u) == 0) {
        v.name = u.sysname;
        std::sscanf(u.release, "%d.%d.%d", &v.major, &v.minor, &v.build);
        v.extra = u.version;
        return v;
    }
#endif
    v.name = "POSIX";
    return v;
}

}  // namespace ls3d::platform
