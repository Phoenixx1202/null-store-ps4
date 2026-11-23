#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>

inline constexpr char kIncludedWorkerPkg[] = "/app0/assets/worker/download-worker.pkg";
inline constexpr char kInstalledWorkerPkg[] = "/user/app/NULL00003/app.pkg";

enum class WorkerPackageMatch { Unverified, Different, Current };

inline bool readWorkerPackageDigest(const char* path, unsigned char (&digest)[32]) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    unsigned char magic[4];
    // PS4 packages have a PFS image digest at 0x440. A display version can be
    // shared by several builds, so only a matching digest allows a launch.
    const bool read = fread(magic, 1, sizeof magic, file) == sizeof magic &&
        memcmp(magic, "\x7f" "CNT", sizeof magic) == 0 && fseek(file, 0x440, SEEK_SET) == 0 &&
        fread(digest, 1, sizeof digest, file) == sizeof digest;
    fclose(file);
    return read && std::any_of(digest, digest + sizeof digest, [](unsigned char byte) { return byte != 0; });
}

inline WorkerPackageMatch workerPackageMatch(const char* installedPath = kInstalledWorkerPkg,
                                              const char* includedPath = kIncludedWorkerPkg) {
    unsigned char installed[32], included[32];
    if (!readWorkerPackageDigest(installedPath, installed) || !readWorkerPackageDigest(includedPath, included))
        return WorkerPackageMatch::Unverified;
    return memcmp(installed, included, sizeof installed) == 0 ?
        WorkerPackageMatch::Current : WorkerPackageMatch::Different;
}
