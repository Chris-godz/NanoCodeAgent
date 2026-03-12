#pragma once

#include <cstdlib>

#ifdef __cplusplus
extern "C" {
#endif
extern char** environ;
#ifdef __cplusplus
}
#endif

namespace process_env {

inline void reset_child_environment() {
#if defined(__linux__) || defined(__GLIBC__)
    clearenv();
#else
    static char* empty[] = {nullptr};
    environ = empty;
#endif

    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("HOME", "/tmp", 1);
}

} // namespace process_env
