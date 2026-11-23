#pragma once

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "catalog.h"
#include "json_lite.h"

// -----------------------------------------------------------------------------
// The official pictures of a title, as the PlayStation Store publishes them.
//
// When orbispatches.com has no icon for a title, the cover cache asks Sony's
// store API for it instead:
//
//   https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/<country>/<lang>/999/<TITLEID>_00
//
// The answer is JSON whose "images" array names the title's pictures:
//
//   type 10  the product art, 1024x1024      -> the square icon
//   type 12  the background art, 1920x1080   -> the game page's background
//
// The pictures themselves live on the Sony picture servers
// (vulcan.dl.playstation.net and image.api.playstation.com). Fetching them may
// need the DNS bypass of src/network_bypass.cpp: the hosts of the store API and
// of these picture servers are the only ones portalNetworkFetch sends there.
// -----------------------------------------------------------------------------
namespace ns::sony {

constexpr const char* kApi = "https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/";
constexpr const char* kStore = "https://store.playstation.com/";

// The store API and picture hosts whose files the DNS bypass fetches.
inline bool isBypassHost(const std::string& url)
{
    size_t p = url.compare(0, 8, "https://") == 0 ? 8 : 0;
    if (!p) {
        return false;
    }
    const auto end = url.find_first_of("/?#", p);
    const std::string host = url.substr(p, end == std::string::npos ? std::string::npos : end - p);
    return host == "store.playstation.com" || host == "vulcan.dl.playstation.net" ||
           host == "apollo2.dl.playstation.net" || host == "image.api.playstation.com";
}

// True for an address of one of the title's pictures on a Sony picture
// server, made of plain characters only.
inline bool isSonyPictureAddress(const std::string& address)
{
    if (address.size() < 20 || address.size() >= 600 || !isBypassHost(address) || address.find("..") != std::string::npos) {
        return false;
    }
    for (char raw : address) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (!isalnum(c) && c != '/' && c != '.' && c != '_' && c != '-' && c != ':' && c != '%') {
            return false;
        }
    }
    return true;
}

struct Art {
    std::string name;       // the product's name, "" when the store gives none
    std::string icon;       // the square product art, "" when the store names none
    std::string background; // the 1920x1080 art, "" when the store names none
};

// The icon and background addresses of the title in a store API answer,
// validated against the picture servers. "" for either when it has none.
inline Art artFromContainer(const std::string& page)
{
    Art art;
    const std::string images = "\"images\":[";
    const auto start = page.find(images);
    if (start == std::string::npos) {
        return art;
    }
    const size_t limit = page.size() < start + images.size() + 8192 ? page.size() : start + images.size() + 8192;
    const size_t closing = page.find(']', start);
    const size_t endCap = closing == std::string::npos ? limit : (closing < limit ? closing : limit);
    for (size_t at = start + images.size(); at < endCap;) {
        const size_t entry = page.find("{\"type\":", at);
        if (entry == std::string::npos || entry >= endCap) {
            break;
        }
        const size_t end = page.find('}', entry);
        if (end == std::string::npos || end >= endCap) {
            break;
        }
        const std::string item = page.substr(entry, end - entry);
        const std::string urlMark = "\"url\":\"";
        const auto u = item.find(urlMark);
        if (u != std::string::npos) {
            const int type = atoi(item.c_str() + 8); // {"type": is 8 characters
            std::string address;
            for (size_t i = u + urlMark.size(); i < item.size(); ++i) {
                const char c = item[i];
                if (c == '"') {
                    break;
                }
                if (c != '\\') {
                    address += c;
                } else if (++i < item.size()) {
                    address += item[i]; // the only escapes a URL has: \/ and \u002F
                }
            }
            // The API answers with http:// for old /cdn/ pictures; the
            // picture servers serve those over https as well.
            if (address.compare(0, 7, "http://") == 0) {
                address.replace(0, 4, "https");
            }
            if (isSonyPictureAddress(address)) {
                if (type == 10 || (type == 0 && art.icon.empty())) {
                    art.icon = address;
                } else if (type == 12) {
                    art.background = address;
                }
            }
        }
        at = end + 1;
    }
    return art;
}

// The product's own "name" in a store API answer: the key of the outer
// object, not the "name" of the nested entries (content descriptors...).
inline std::string nameFromContainer(const std::string& page)
{
    int depth = 0;
    for (size_t i = 0; i < page.size(); ++i) {
        const char c = page[i];
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            --depth;
        } else if (c == '"') {
            const bool key = depth == 1 && page.compare(i, 8, "\"name\":\"") == 0;
            size_t j = i + 1;
            if (key) {
                j = i + 8;
            }
            std::string value;
            for (; j < page.size() && page[j] != '"'; ++j) {
                if (page[j] == '\\' && j + 1 < page.size()) {
                    ++j;
                    if (page[j] == 'u' && j + 4 < page.size()) {
                        const unsigned code = static_cast<unsigned>(strtoul(page.substr(j + 1, 4).c_str(), nullptr, 16));
                        value += utf8(code);
                        j += 4;
                        continue;
                    }
                }
                value += page[j];
            }
            if (key) {
                return value;
            }
            i = j; // past the closing quote of this string
        }
    }
    return std::string();
}

// The API address of the title's container for one store region.
inline std::string containerURL(const std::string& titleId, const char* country, const char* language)
{
    return std::string(kApi) + country + "/" + language + "/999/" + titleId + "_00";
}

inline constexpr const char* kStoreRegions[] = {"BR/pt", "US/en", "GB/en", "HK/en", "JP/ja"};
inline std::string regionalContainerURL(const std::string& titleId, const char* region)
{
    const char* slash = strchr(region, '/');
    const std::string country(region, static_cast<size_t>(slash - region));
    return containerURL(titleId, country.c_str(), slash + 1);
}

struct Media {
    static constexpr size_t maxScreenshots = 3;
    std::vector<std::string> screenshots;
    std::string preview;
    bool empty() const { return screenshots.empty() && preview.empty(); }
};

inline bool isPreviewAddress(const std::string& url)
{
    return isSonyPictureAddress(url) && url.size() >= 4 &&
           lower(url.substr(url.size() - 4)) == ".mp4";
}

inline Media mediaFromContainer(const std::string& page)
{
    const auto document = json::parse(page);
    const auto& list = document["mediaList"];
    Media media;
    for (const auto& shot : list["screenshots"].items) {
        auto url = shot["url"].str();
        if (url.compare(0, 7, "http://") == 0) url.replace(0, 4, "https");
        if (!isSonyPictureAddress(url) ||
            std::find(media.screenshots.begin(), media.screenshots.end(), url) != media.screenshots.end()) continue;
        media.screenshots.push_back(std::move(url));
        if (media.screenshots.size() == Media::maxScreenshots) break;
    }
    for (const auto& preview : list["previews"].items) {
        auto url = preview["url"].str();
        if (url.compare(0, 7, "http://") == 0) url.replace(0, 4, "https");
        if (!isPreviewAddress(url)) continue;
        const bool isTrailer = lower(url).find("trailer") != std::string::npos;
        if (media.preview.empty() || isTrailer) media.preview = std::move(url);
        if (isTrailer) break;
    }
    return media;
}

inline Media titleMedia(bool (*fetch)(const std::string&, std::string&, size_t, const std::string&),
                        const std::string& titleId)
{
    for (const char* region : {"US/en", "GB/en", "BR/pt", "HK/en", "JP/ja"}) {
        std::string page;
        if (!fetch(regionalContainerURL(titleId, region), page, 4 * 1024 * 1024, "")) continue;
        auto media = mediaFromContainer(page);
        if (!media.empty()) return media;
    }
    return {};
}

// True when the name has no letters of another script than Latin (the
// store's font has only Latin ones).
inline bool latinName(const std::string& name)
{
    for (unsigned char c : name) {
        if ((c >= 0xCD && c <= 0xDF) || (c >= 0xE3 && c <= 0xEF) || c >= 0xF0) {
            return false;
        }
    }
    return !name.empty();
}

// Asks the store for the title's pictures: the user's own region first, then
// the US, European, Asian and Japanese stores (a title ID belongs to one
// region; a European or Asian release is not in the American store). True when the store answered (with or without
// pictures, leaving ""); false when it could not be asked at all, so the
// caller retries later instead of giving up.
inline bool titleArt(bool (*fetch)(const std::string&, std::string&, size_t, const std::string&),
                     const std::string& titleId, Art& art)
{
    art = Art();
    if (titleId.size() != 9) {
        return false;
    }
    bool answered = false;
    for (const char* region : kStoreRegions) {
        const std::string url = regionalContainerURL(titleId, region);
        std::string page;
        if (fetch(url, page, 4 * 1024 * 1024, "")) {
            answered = true;
            std::string name = art.name;
            if (name.empty()) {
                name = nameFromContainer(page);
                if (!latinName(name)) {
                    name.clear(); // a later region may name it in Latin letters
                }
            }
            art = artFromContainer(page);
            art.name = name;
            if (!art.icon.empty() || !art.background.empty()) {
                return true;
            }
            // The store answered and named no usable picture: try the other
            // region before telling the caller there is none.
        }
    }
    return answered;
}

} // namespace ns::sony
