#include "vvv/util/detect_debugger.hpp"

// TODO(Reiner): test windows support
#ifndef _WIN32

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cctype>

bool vvv::debuggerIsAttached() {
    char buf[4096];

    const int status_fd = ::open("/proc/self/status", O_RDONLY);
    if (status_fd == -1)
        return false;

    const ssize_t num_read = ::read(status_fd, buf, sizeof(buf) - 1);
    ::close(status_fd);

    if (num_read <= 0)
        return false;

    buf[num_read] = '\0';
    constexpr char tracerPidString[] = "TracerPid:";
    const auto tracer_pid_ptr = ::strstr(buf, tracerPidString);
    if (!tracer_pid_ptr)
        return false;

    for (const char *characterPtr = tracer_pid_ptr + sizeof(tracerPidString) - 1; characterPtr <= buf + num_read; ++characterPtr) {
        if (::isspace(*characterPtr))
            continue;
        else
            return ::isdigit(*characterPtr) != 0 && *characterPtr != '0';
    }

    return false;
}
#else
bool vvv::debuggerIsAttached() {
    // TODO(Patrick): fix detect_debugger in Windows
    // return IsDebuggerPresent();
    return false;
}
#endif