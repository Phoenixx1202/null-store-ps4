#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// HTTP/HTTPS over the console's own sockets (src/network_bypass.cpp): follows
// up to five redirects, reads Content-Length, chunked and read-until-close
// bodies. Never uses libSceHttp.

// GET; true for a 200 answer of at most `cap` bytes.
bool portalSocketFetch(const std::string& url, std::string& out, size_t cap, const std::string& referer);

struct PortalRequest {
    std::string method = "GET";       // GET, POST, PUT, DELETE
    std::vector<std::string> headers; // "Name: value"
    std::string body;                 // sent with POST and PUT
    std::string referer;
};

// Any method. True when an answer arrived, whatever its status (an API
// explains an error in the body, kept in `out` like any other).
bool portalSocketRequest(const std::string& url, const PortalRequest& request, std::string& out, size_t cap,
                         int& status);

// A large GET straight to its destination, asking for the bytes from
// `offset` on. begin(start, total) is told where the body starts (0 when the
// server sends the whole file again) and the file's size (0: unknown);
// write() takes the bytes in order; either returning false stops. stopped()
// is asked while waiting on the network. There is no overall time limit:
// only a connection idle for 25 s fails.
struct PortalStream {
    uint64_t offset = 0;
    uint64_t end = 0; // exclusive; 0: up to the end of the file
    std::function<bool(uint64_t start, uint64_t total)> begin;
    std::function<bool(const unsigned char*, size_t)> write;
    std::function<bool()> stopped;
};
// True when the file arrived (or was already complete: a 416 answer to the range).
bool portalSocketDownload(const std::string& url, PortalStream& stream, std::string& why);
