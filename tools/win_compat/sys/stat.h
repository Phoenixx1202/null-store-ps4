// <sys/stat.h> with the POSIX fields and calls the store's storage headers
// use (st_blocks, st_blksize, fstat, lstat) on top of the Microsoft runtime.
#pragma once
#include_next <sys/stat.h>

#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif

#ifdef __cplusplus
struct ns_win_stat {
    unsigned st_dev;
    unsigned st_ino;
    unsigned short st_mode;
    short st_nlink;
    short st_uid;
    short st_gid;
    unsigned st_rdev;
    long long st_size;
    long long st_blocks;
    long st_blksize;
    long long st_atime;
    long long st_mtime;
    long long st_ctime;
};

inline void ns_win_stat_copy(const struct _stat64& from, struct ns_win_stat* to)
{
    to->st_dev = from.st_dev;
    to->st_ino = from.st_ino;
    to->st_mode = from.st_mode;
    to->st_nlink = from.st_nlink;
    to->st_uid = from.st_uid;
    to->st_gid = from.st_gid;
    to->st_rdev = from.st_rdev;
    to->st_size = from.st_size;
    to->st_blocks = (from.st_size + 511) / 512;
    to->st_blksize = 4096;
    to->st_atime = from.st_atime;
    to->st_mtime = from.st_mtime;
    to->st_ctime = from.st_ctime;
}

inline int fstat(int fd, struct ns_win_stat* out)
{
    struct _stat64 info;
    if (_fstat64(fd, &info) != 0) {
        return -1;
    }
    ns_win_stat_copy(info, out);
    return 0;
}

inline int lstat(const char* path, struct ns_win_stat* out)
{
    struct _stat64 info;
    if (_stat64(path, &info) != 0) {
        return -1;
    }
    ns_win_stat_copy(info, out);
    return 0;
}

// `struct stat` and stat() from here on mean the POSIX-shaped ones above.
inline int ns_win_stat(const char* path, struct ns_win_stat* out) { return lstat(path, out); }
#define stat ns_win_stat
#endif

// mkdir(path, mode): the Microsoft runtime takes no mode.
#include <direct.h>
#ifdef __cplusplus
inline int mkdir(const char* path, int) { return _mkdir(path); }
#endif
