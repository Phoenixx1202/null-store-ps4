#pragma once

#include <cstdint>
#include <string>

// URL handling for the HTTPS client (no I/O, unit tested).
namespace dusk::net {

struct Url {
    std::string scheme; // lower case
    std::string host;   // lower case
    uint16_t port = 0;  // explicit or the scheme's default
    std::string target; // path + query, starts with '/'
};

// "scheme://host[:port][/path][?query][#fragment]" - the fragment is dropped.
// Refuses user info, IPv6 literals, spaces and control characters.
bool parseUrl(const std::string& text, Url& url);

// Canonical text of a parsed URL (default port omitted).
std::string formatUrl(const Url& url);

// Location header of a redirect, relative to `base` -> absolute URL text, ""
// when it cannot be resolved.
std::string resolveLocation(const Url& base, const std::string& location);

// Dotted-quad IPv4 literal -> address in network byte order.
bool parseIpv4(const std::string& text, uint32_t& addressBe);

} // namespace dusk::net
