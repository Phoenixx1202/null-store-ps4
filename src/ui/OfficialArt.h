#pragma once

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "catalog.h"

// -----------------------------------------------------------------------------
// The official pictures of a title, as orbispatches.com publishes them:
// the page https://orbispatches.com/<TITLEID> names the title's folder on the
// picture server, which holds icon0.webp (the square icon) and, for most
// titles, pic0.webp (the full-screen background).
//
// On the console they are kept under <data>/art/<TITLEID>/:
//   icon0.webp   the icon
//   pic0.txt     address of the background, when the title has one
//   pic0.webp    the background, once a game page asked for it
//   none         the site has no icon for the title
// -----------------------------------------------------------------------------
namespace ns::official {

constexpr const char* kSite = "https://orbispatches.com/";
constexpr const char* kPictures = "https://cdn.orbispatches.com/titles/";

inline std::string folder(const std::string& artRoot, const std::string& titleId)
{
    return artRoot + "/" + titleId;
}

inline bool fileExists(const std::string& path)
{
    if (FILE* file = fopen(path.c_str(), "rb")) {
        fclose(file);
        return true;
    }
    return false;
}

// True when the bytes are a WebP, PNG or JPEG file.
inline bool isPicture(const std::string& bytes)
{
    const auto starts = [&](const char* magic, size_t length) {
        return bytes.size() > length && memcmp(bytes.data(), magic, length) == 0;
    };
    return (starts("RIFF", 4) && bytes.size() > 12 && memcmp(bytes.data() + 8, "WEBP", 4) == 0) ||
           starts("\x89PNG", 4) || starts("\xFF\xD8", 2);
}

// True for an address of one of the title's pictures on the picture server,
// made of plain characters only.
inline bool isPictureAddress(const std::string& address, const std::string& titleId)
{
    const std::string start = std::string(kPictures) + titleId + "_";
    if (address.size() >= 300 || address.compare(0, start.size(), start) != 0 ||
        address.find("..") != std::string::npos) {
        return false;
    }
    for (char raw : address) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (!isalnum(c) && c != '/' && c != '.' && c != '_' && c != '-' && c != ':') {
            return false;
        }
    }
    return true;
}

// The address of `file` ("icon0.webp") among the pictures the title's page
// names, or "" when the page does not name it.
inline std::string pictureAddress(const std::string& page, const std::string& titleId, const char* file)
{
    const std::string start = std::string(kPictures) + titleId + "_";
    const std::string tail = std::string("/") + file;
    for (size_t at = page.find(start); at != std::string::npos; at = page.find(start, at + 1)) {
        size_t end = at;
        while (end < page.size()) {
            const unsigned char c = static_cast<unsigned char>(page[end]);
            if (!isalnum(c) && c != '/' && c != '.' && c != '_' && c != '-' && c != ':') {
                break;
            }
            ++end;
        }
        const std::string address = page.substr(at, end - at);
        if (address.size() > tail.size() &&
            address.compare(address.size() - tail.size(), tail.size(), tail) == 0 &&
            isPictureAddress(address, titleId)) {
            return address;
        }
    }
    return std::string();
}

// The title's name on its page: <title>CUSA02010: Dying Light | ORBISPatches.com</title>.
inline std::string nameFromPage(const std::string& page, const std::string& titleId)
{
    const std::string start = "<title>" + titleId + ": ";
    const size_t at = page.find(start);
    if (at == std::string::npos) {
        return std::string();
    }
    const size_t from = at + start.size();
    const size_t end = page.find("</title>", from);
    if (end == std::string::npos || end - from > 300) {
        return std::string();
    }
    std::string name = page.substr(from, end - from);
    const size_t site = name.rfind(" | ");
    if (site != std::string::npos) {
        name.erase(site);
    }
    return decode(name);
}

} // namespace ns::official
