#pragma once

#include "torrent.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Downloads through a Debrid service: the torrent goes to the service, which
// fetches it on its own servers; the console then downloads the files from
// the service over HTTPS, resuming where a file stopped.
//
// Services (the numbers of the "debrid=" setting): 1 Real-Debrid,
// 2 AllDebrid, 3 Premiumize, 4 TorBox, 5 Debrid-Link.
//
// The settings come from <data>/settings.txt ("debrid=<n>") and the keys
// from <data>/debrid-keys.txt ("<n>=<key>"), both written by the store.
// -----------------------------------------------------------------------------

// The network, as the console provides it (src/network_bypass.cpp); tests
// give their own.
struct DebridNet {
    // Any method; true when an answer arrived, whatever its status.
    std::function<bool(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                       const std::string& body, std::string& out, int& status)>
        request;
    // A GET of the bytes from `offset` up to `end` (exclusive; 0: the rest),
    // straight to `write` (see PortalStream). Safe to call from several threads.
    std::function<bool(const std::string& url, uint64_t offset, uint64_t end,
                       const std::function<bool(uint64_t, uint64_t)>& begin,
                       const std::function<bool(const unsigned char*, size_t)>& write,
                       const std::function<bool()>& stopped, std::string& why)>
        download;
    // Waits a little (tests do not wait).
    std::function<void(int milliseconds)> sleep;
};

struct DebridConfig {
    int service = 0; // 0: none
    std::string key;
    bool enabled() const { return service > 0 && !key.empty(); }
};

DebridConfig debridConfig(const std::string& dataRoot);
const char* debridServiceName(int service);

// Like torrent_run(): 0 when every file arrived, 1 when stopped with the files
// kept, -1 on failure (callbacks->error says why). `statePath` keeps the
// transfer's id at the service, so a resumed download does not add the
// torrent again.
int debridRun(const DebridNet& net, const DebridConfig& config, const std::string& metainfo,
              const std::string& statePath, const std::string& destination, const torrent_callbacks* callbacks);

// For tests: the magnet link of a torrent (its info hash, name and trackers).
std::string debridMagnet(const std::string& metainfo);
