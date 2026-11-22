#include "net/Url.h"

#include <cctype>
#include <cstring>

namespace dusk::net {

namespace {

std::string lower(std::string text)
{
    for (char& c : text) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

uint16_t defaultPort(const std::string& scheme)
{
    return scheme == "https" ? 443 : (scheme == "http" ? 80 : 0);
}

bool validHost(const std::string& host)
{
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '-') {
        return false;
    }
    for (char c : host) {
        if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

} // namespace

bool parseIpv4(const std::string& text, uint32_t& addressBe)
{
    uint8_t bytes[4];
    size_t pos = 0;
    for (int part = 0; part < 4; ++part) {
        unsigned value = 0;
        size_t digits = 0;
        while (pos < text.size() && isdigit(static_cast<unsigned char>(text[pos])) && digits < 3) {
            value = value * 10 + static_cast<unsigned>(text[pos] - '0');
            ++pos;
            ++digits;
        }
        if (digits == 0 || value > 255) {
            return false;
        }
        bytes[part] = static_cast<uint8_t>(value);
        if (part < 3) {
            if (pos >= text.size() || text[pos] != '.') {
                return false;
            }
            ++pos;
        }
    }
    if (pos != text.size()) {
        return false;
    }
    memcpy(&addressBe, bytes, sizeof(bytes));
    return true;
}

bool parseUrl(const std::string& text, Url& url)
{
    const size_t separator = text.find("://");
    if (separator == std::string::npos || separator == 0) {
        return false;
    }
    Url out;
    out.scheme = lower(text.substr(0, separator));
    for (char c : out.scheme) {
        if (!isalpha(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    const size_t start = separator + 3;
    const size_t end = text.find_first_of("/?#", start);
    const std::string authority = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (authority.empty() || authority.find('@') != std::string::npos || authority[0] == '[') {
        return false;
    }
    std::string host = authority;
    uint32_t port = defaultPort(out.scheme);
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        const std::string digits = authority.substr(colon + 1);
        if (digits.empty() || digits.size() > 5) {
            return false;
        }
        port = 0;
        for (char c : digits) {
            if (!isdigit(static_cast<unsigned char>(c))) {
                return false;
            }
            port = port * 10 + static_cast<uint32_t>(c - '0');
        }
    }
    if (!validHost(host) || port == 0 || port > 65535) {
        return false;
    }
    out.host = lower(host);
    out.port = static_cast<uint16_t>(port);
    std::string target = end == std::string::npos ? std::string() : text.substr(end);
    const size_t hash = target.find('#');
    if (hash != std::string::npos) {
        target.erase(hash);
    }
    if (target.empty() || target[0] == '?') {
        target.insert(0, "/");
    }
    for (char c : target) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) {
            return false;
        }
    }
    out.target = target;
    url = out;
    return true;
}

std::string formatUrl(const Url& url)
{
    std::string text = url.scheme + "://" + url.host;
    if (url.port != defaultPort(url.scheme)) {
        text += ":" + std::to_string(url.port);
    }
    return text + url.target;
}

std::string resolveLocation(const Url& base, const std::string& location)
{
    size_t first = 0;
    size_t last = location.size();
    while (first < last && isspace(static_cast<unsigned char>(location[first]))) {
        ++first;
    }
    while (last > first && isspace(static_cast<unsigned char>(location[last - 1]))) {
        --last;
    }
    const std::string value = location.substr(first, last - first);
    if (value.empty()) {
        return std::string();
    }
    Url resolved;
    if (value.find("://") != std::string::npos) {
        return parseUrl(value, resolved) ? formatUrl(resolved) : std::string();
    }
    if (value.compare(0, 2, "//") == 0) {
        return parseUrl(base.scheme + ":" + value, resolved) ? formatUrl(resolved) : std::string();
    }
    Url candidate = base;
    if (value[0] == '/') {
        candidate.target = value;
    } else {
        std::string directory = base.target.substr(0, base.target.find('?'));
        directory.erase(directory.rfind('/') + 1);
        candidate.target = directory + value;
    }
    return parseUrl(formatUrl(candidate), resolved) ? formatUrl(resolved) : std::string();
}

} // namespace dusk::net
