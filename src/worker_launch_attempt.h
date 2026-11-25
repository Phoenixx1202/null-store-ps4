#pragma once

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <string>

inline bool recordWorkerLaunch(const std::string& root, const std::string& path, std::string& error) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (fd < 0) {
        error = errno == EEXIST ?
            "This worker's previous start failed or was interrupted. Keep NULL_store open to download." :
            "Could not save the worker start record (errno " + std::to_string(errno) + ").";
        return false;
    }
    int syncError = fsync(fd) == 0 ? 0 : errno;
    if (close(fd) != 0 && !syncError) syncError = errno;
    const int dir = open(root.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir < 0 && !syncError) syncError = errno;
    if (dir >= 0) {
        if (fsync(dir) != 0 && !syncError) syncError = errno;
        close(dir);
    }
    if (!syncError) return true;
    unlink(path.c_str());
    error = "Could not sync the worker start record (errno " + std::to_string(syncError) + ").";
    return false;
}
