#pragma once

#include "json_lite.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

inline constexpr char kIncludedWorkerPkg[] = "/app0/assets/worker/download-worker.pkg";
inline constexpr char kInstalledWorkerPkg[] = "/user/app/NULL00003/app.pkg";
inline constexpr char kInstalledWorkerManifest[] = "/user/app/NULL00003/app.json";

enum class WorkerPackageMatch { Unverified, Different, Current };

// BGFT can confirm an exact install staged and submitted by this process,
// even when the app sandbox hides the installed files. Keep this in memory
// so a later app session must check the installed package again.
inline std::atomic<bool> workerInstallConfirmedThisSession{false};

inline bool readWorkerPackageDigest(const char* path, unsigned char (&digest)[32], long offset = 0x440) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    unsigned char magic[4];
    // PS4 packages have a PFS image digest at 0x440. A display version can be
    // shared by several builds, so only a matching digest allows a launch.
    const bool read = fread(magic, 1, sizeof magic, file) == sizeof magic &&
        memcmp(magic, "\x7f" "CNT", sizeof magic) == 0 && fseek(file, offset, SEEK_SET) == 0 &&
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


// The system writes this manifest when it installs a package. Its digest
// matches the signed package header, including when the PFS file is unreadable.
inline WorkerPackageMatch workerManifestMatch(const char* installedManifest, const char* includedPkg) {
    FILE* file = fopen(installedManifest, "rb");
    if (!file) return WorkerPackageMatch::Unverified;
    char bytes[8192];
    const size_t size = fread(bytes, 1, sizeof bytes, file);
    const bool complete = feof(file) && !ferror(file);
    fclose(file);
    if (!complete || size == 0) return WorkerPackageMatch::Unverified;

    const auto parsed = json::parse(std::string(bytes, size));
    const auto& digest = parsed["packageDigest"];
    if (digest.type != json::Value::String || digest.text.size() != 64 ||
        digest.text.find_first_not_of("0123456789ABCDEFabcdef") != std::string::npos)
        return WorkerPackageMatch::Unverified;
    unsigned char bundled[32];
    if (!readWorkerPackageDigest(includedPkg, bundled, 0xFE0)) return WorkerPackageMatch::Unverified;
    char expected[65];
    for (size_t i = 0; i < sizeof bundled; ++i) snprintf(expected + 2 * i, 3, "%02X", bundled[i]);
    const auto upperHex = [](char digit) {
        return digit >= 'a' && digit <= 'f' ? static_cast<char>(digit - 'a' + 'A') : digit;
    };
    return std::equal(digest.text.begin(), digest.text.end(), expected,
                      [&](char left, char right) { return upperHex(left) == right; }) ?
        WorkerPackageMatch::Current : WorkerPackageMatch::Different;
}

inline WorkerPackageMatch installedWorkerPackageMatch(int& packageError, int& manifestError) {
    errno = 0;
    const auto package = workerPackageMatch();
    packageError = errno;
    manifestError = 0;
    if (package != WorkerPackageMatch::Unverified) return package;
    errno = 0;
    const auto match = workerManifestMatch(kInstalledWorkerManifest, kIncludedWorkerPkg);
    manifestError = errno;
    return match;
}
