// POSIX calls for building the PC preview (tools/preview.cpp) with the
// Microsoft C runtime. Only what the store's storage headers use; the PS4
// build never sees this folder.
#pragma once
#include <direct.h>
#include <io.h>
#include <process.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef long long ssize_t;
typedef int mode_t;

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

inline int fsync(int fd) { return _commit(fd); }
inline int fchmod(int, mode_t) { return 0; }
inline int mkstemp(char* pattern)
{
    if (_mktemp_s(pattern, strlen(pattern) + 1) != 0) {
        return -1;
    }
    return _open(pattern, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
}
