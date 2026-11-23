#define PORTAL_HOST
#include "../src/sony_art.h"
#include <cassert>
#include <cstring>
#include <iostream>

static int calls = 0;
static std::string lastUrl;
static bool fetchOk(const std::string& url, std::string& out, size_t, const std::string&)
{
    calls++;
    lastUrl = url;
    if (url.find("BR/pt") != std::string::npos) {
        out = "{\"images\":[{\"type\":10,\"url\":\"https://vulcan.dl.playstation.net/img/rnd/202011/1021/icon.png\"},"
              "{\"type\":12,\"url\":\"https://vulcan.dl.playstation.net/img/rnd/202010/2217/background.png\"},"
              "{\"type\":13,\"url\":\"https://vulcan.dl.playstation.net/img/rnd/202010/2217/logo.png\"}]}";
        return true;
    }
    return false; // the US fallback never answers here
}

int main()
{
    using ns::sony::isBypassHost;
    assert(isBypassHost("https://store.playstation.com/store/api/x"));
    assert(isBypassHost("https://vulcan.dl.playstation.net/img/rnd/x.png"));
    assert(isBypassHost("https://apollo2.dl.playstation.net/cdn/UP0006/x.jpg"));
    assert(isBypassHost("https://image.api.playstation.com/vulcan/ap/rnd/x.jpg"));
    assert(!isBypassHost("https://cdn.orbispatches.com/titles/x_icon0.webp"));
    assert(!isBypassHost("https://ps4torrent.net/x"));
    assert(!isBypassHost("http://store.playstation.com/x")); // https only
    assert(!isBypassHost("https://store.playstation.com.evil.net/x"));

    using ns::sony::isSonyPictureAddress;
    assert(isSonyPictureAddress("https://vulcan.dl.playstation.net/ap/rnd/202407/0401/670c294.png"));
    assert(!isSonyPictureAddress("https://cdn.orbispatches.com/titles/CUSA00001_icon0.webp"));
    assert(!isSonyPictureAddress("https://vulcan.dl.playstation.net/../etc"));

    // A real container answer: type 10 is the icon, type 12 the background,
    // the old http:// scheme is upgraded, types 0/13 are not what we take.
    ns::sony::Art art = ns::sony::artFromContainer(
        "{\"id\":\"UP9000-CUSA00900_00-BLOODBORNE000000\",\"images\":["
        "{\"type\":10,\"url\":\"https://vulcan.dl.playstation.net/img/rnd/202011/0713/WSZq.png\"},"
        "{\"type\":12,\"url\":\"https://vulcan.dl.playstation.net/img/rnd/202010/2614/DbHk.png\"},"
        "{\"type\":13,\"url\":\"http://vulcan.dl.playstation.net/img/rnd/202010/2614/anWz.png\"}]}");
    assert(art.icon == "https://vulcan.dl.playstation.net/img/rnd/202011/0713/WSZq.png");
    assert(art.background == "https://vulcan.dl.playstation.net/img/rnd/202010/2614/DbHk.png");

    // Only type 10: no background. Only type 13 (logo): nothing usable.
    art = ns::sony::artFromContainer(
        "{\"images\":[{\"type\":10,\"url\":\"https://image.api.playstation.com/vulcan/ap/rnd/x.png\"}]}");
    assert(art.icon == "https://image.api.playstation.com/vulcan/ap/rnd/x.png" && art.background.empty());
    art = ns::sony::artFromContainer(
        "{\"images\":[{\"type\":13,\"url\":\"https://vulcan.dl.playstation.net/img/rnd/x.png\"}]}");
    assert(art.icon.empty() && art.background.empty());

    // Foreign or malformed picture servers are never accepted.
    art = ns::sony::artFromContainer(
        "{\"images\":[{\"type\":10,\"url\":\"https://evil.example.com/x.png\"}]}");
    assert(art.icon.empty() && art.background.empty());
    art = ns::sony::artFromContainer("{\"images\":[{\"type\":10,\"url\":\"https://vulcan.dl.playstation.net/..%2F..\"}]}");
    assert(art.icon.empty() && art.background.empty());

    // titleArt: the BR store answers first; the API address carries the title.
    ns::sony::Art found;
    assert(ns::sony::titleArt(fetchOk, "CUSA07408", found));
    assert(calls == 1);
    assert(lastUrl == "https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/BR/pt/999/CUSA07408_00");
    assert(found.icon == "https://vulcan.dl.playstation.net/img/rnd/202011/1021/icon.png");
    assert(found.background == "https://vulcan.dl.playstation.net/img/rnd/202010/2217/background.png");

    const auto media = ns::sony::mediaFromContainer(R"({
        "mediaList": {
            "screenshots": [
                {"type":"SCREENSHOT","url":"http:\/\/apollo2.dl.playstation.net\/cdn\/game.jpg"},
                {"url":"https://apollo2.dl.playstation.net/cdn/game.jpg"},
                {"url":"https://example.org/external.jpg"},
                {"url":"https://vulcan.dl.playstation.net/img/game2.jpg"},
                {"url":"https://vulcan.dl.playstation.net/img/game3.jpg"},
                {"url":"https://vulcan.dl.playstation.net/img/game4.jpg"}
            ],
            "previews": [
                {"url":"https://vulcan.dl.playstation.net/gameplay.mp4"},
                {"url":"https://vulcan.dl.playstation.net.trusted.example/trailer.mp4"},
                {"url":"https://apollo2.dl.playstation.net/PREVIEW_TRAILER_VIDEO.mp4"}
            ]
        }
    })");
    assert(media.screenshots.size() == 3);
    assert(media.screenshots[0] == "https://apollo2.dl.playstation.net/cdn/game.jpg");
    assert(media.screenshots[2] == "https://vulcan.dl.playstation.net/img/game3.jpg");
    assert(media.preview == "https://apollo2.dl.playstation.net/PREVIEW_TRAILER_VIDEO.mp4");
    assert(ns::sony::mediaFromContainer(R"({"mediaList":{"previews":[{"url":"https://vulcan.dl.playstation.net/stream.m3u8"}]}})").empty());
    assert(ns::sony::mediaFromContainer("{malformed").empty());

    std::cout << "Sony store art parsing, host routing and region fallback passed.\n";
}
