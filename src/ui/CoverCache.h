#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "catalog.h"
#include "graphics/Renderer.h"

// -----------------------------------------------------------------------------
// The picture of each game, as a texture: a square tile with rounded corners.
//
// It is the title's official icon when there is one (src/ui/OfficialArt.h);
// otherwise the cover of the catalogue source, cropped to a square. Finding it
// can take three steps the first time - the game's own page for its title ID,
// the title's page for the icon's address, the icon - so all of it happens on
// worker threads and every answer is kept under the data folder: the next
// visit reads the tile from storage.
//
// The render thread only uploads finished pictures, a few per frame, and
// keeps the most recently shown ones. Until a tile is ready the screens draw
// a placeholder; a tile is never swapped for another once it is shown.
// -----------------------------------------------------------------------------
namespace ns {

struct Cover {
    static constexpr int kSize = 300;

    Texture texture;
    double readyTime = 0.0; // when it was uploaded (CoverCache::now): new tiles fade in
};

class CoverCache {
public:
    // Downloads `url` into `out` (at most `limit` bytes); false when it fails.
    using Fetch = bool (*)(const std::string& url, std::string& out, size_t limit, const std::string& referer);

    CoverCache(Renderer& renderer, std::string basePath, std::string dataPath);
    ~CoverCache();

    CoverCache(const CoverCache&) = delete;
    CoverCache& operator=(const CoverCache&) = delete;

    // Lets the cache download what it does not find on the console.
    void enableDownloads(Fetch fetch);

    // Whether tiles use the titles' official icons (Settings). Changing it
    // drops the tiles made so far: they are made again the new way.
    void setOfficialArt(bool on);

    // The tile of `game`, or nullptr while it is loading or when it has none.
    // `titleId` is the game's title ID when the caller knows it. `order` says
    // how soon this tile is wanted compared to the others asked for in the
    // same frame (its place on screen, in reading order): lower comes first.
    const Cover* get(const Game& game, const std::string& titleId, int order);

    // Uploads finished tiles and evicts old ones; call once per frame with the
    // current time in seconds.
    void pump(double nowSeconds);
    double now() const { return now_; }

    // Moves the official names learnt since the last call into `names`
    // (title ID -> name); true when there was any.
    bool pollNames(std::map<std::string, std::string>& names);

    // Copies the title IDs read from the sources' game pages since the last
    // call into `ids` (game id -> title ID); true when there was any.
    bool pollTitleIds(std::map<int, std::string>& ids);

private:
    enum class State { Loading, Ready, Missing };

    struct Entry {
        State state = State::Loading;
        Cover cover;
        uint64_t lastUsedFrame = 0;
        double missingSince = 0.0;
    };

    struct Request {
        int id = 0;
        std::string titleId;            // from the catalogue entry, when it says
        std::string pageUrl;            // the game's page on its source
        std::string imageUrl;           // the source's cover
        std::string savePath;           // where a downloaded source cover is kept
        std::vector<std::string> paths; // where the source cover may already be
        bool networkDone = false;       // everything that can be fetched has been tried
        int generation = 0;             // the setting it was asked under (setOfficialArt)
    };

    struct Result {
        int id = 0;
        int generation = 0;
        Image image;
        std::string titleId; // with `name`: the official name of the tile's title
        std::string name;
    };

    struct PackedCover {
        unsigned long long offset = 0;
        size_t length = 0;
    };

    std::string titleIdOf(const Request& request) const;
    bool resolve(const Request& request, Result& result) const;
    void fetchAll(const Request& request) const;
    void learn(int gameId, const std::string& titleId) const;
    void finish(Result& result);
    void decodeLoop();
    void downloadLoop();
    static void* decodeEntry(void* self);
    static void* downloadEntry(void* self);

    Renderer& renderer_;
    std::string base_, data_, artRoot_, idsPath_;
    std::map<int, PackedCover> packed_;
    std::map<int, Entry> entries_;
    struct Wanted {
        int id;
        int order;
    };
    std::vector<Wanted> wanted_; // the tiles still loading that were asked for this frame
    std::map<std::string, std::string> freshNames_; // not yet handed to pollNames()
    double now_ = 0.0;
    int generation_ = 0;
    [[maybe_unused]] Fetch fetch_ = nullptr; // set once, before the download workers start

    struct Shared; // queues and the title IDs shared with the worker threads
    Shared* shared_ = nullptr;
};

} // namespace ns
