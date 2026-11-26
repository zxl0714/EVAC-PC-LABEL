#pragma once
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace cave::ps2 {
// Restore an open stream's access and append semantics. Creation, exclusion
// and truncation belonged to the guest's original open operation.
inline FILE* ReopenStateFile(const std::filesystem::path& path, const char* mode) {
    int access = std::strchr(mode, '+') ? O_RDWR : (*mode == 'r' ? O_RDONLY : O_WRONLY);
    if (*mode == 'a') access |= O_APPEND;
#ifdef _WIN32
    const int fd = _wopen(path.c_str(), access | _O_BINARY | _O_NOINHERIT);
    if (fd < 0) return nullptr;
    FILE* result = _fdopen(fd, mode);
    if (!result) _close(fd);
#else
    const int fd = ::open(path.c_str(), access | O_CLOEXEC);
    if (fd < 0) return nullptr;
    FILE* result = fdopen(fd, mode);
    if (!result) ::close(fd);
#endif
    return result;
}
}
