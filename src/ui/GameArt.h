#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "graphics/Renderer.h"

// -----------------------------------------------------------------------------
// The official background picture of a title, for its game page.
//
// The cover cache leaves the picture's address under <data>/art/<title ID>/
// when it finds the title's icon (src/ui/OfficialArt.h); this fetches the
// picture itself the first time a title is asked for and keeps it there. The
// screens ask as soon as the cursor rests on a game, so the background is
// usually there when its page opens. Everything happens on a worker thread.
// -----------------------------------------------------------------------------
namespace ns {

struct GameScreenshot {
    Texture image;
    Texture thumbnail;
};

struct GameArtwork {
    Texture background;     // 16:9
    double readyTime = 0.0; // when it was uploaded (GameArt::now): it fades in
    std::vector<GameScreenshot> screenshots;
    std::string preview;
};

class GameArt {
public:
    // Downloads `url` into `out` (at most `limit` bytes); false when it fails.
    using Fetch = bool (*)(const std::string& url, std::string& out, size_t limit, const std::string& referer);

    GameArt(Renderer& renderer, std::string dataPath);
    ~GameArt();

    GameArt(const GameArt&) = delete;
    GameArt& operator=(const GameArt&) = delete;

    // Lets the pictures that are not on the console yet be downloaded.
    void enableDownloads(Fetch fetch);

    // The background of the title, or nullptr while it is loading or when it
    // has none. Asking is what starts the lookup.
    const GameArtwork* get(const std::string& titleId);

    // Uploads what the worker finished; call once per frame with the current
    // time in seconds.
    void pump(double nowSeconds);
    bool loadingMedia(const std::string& id) const {
        const auto it = entries_.find(id);
        return it != entries_.end() && it->second.mediaPending;
    }
    double now() const { return now_; }

private:
    enum class State { Loading, Ready, Missing };

    struct Entry {
        State state = State::Loading;
        GameArtwork art;
        uint64_t lastUsedFrame = 0;
        double missingSince = 0.0;
        bool mediaRequested = false, mediaPending = false;
    };

    struct Result {
        std::string titleId;
        Image background;
        bool media = false;
        std::vector<Image> screenshots;
        std::string preview;
    };

    Result load(const std::string& titleId) const;
    Result loadMedia(const std::string& titleId) const;
    void finish(Result& result);
    void run();
    static void* entry(void* self);

    Renderer& renderer_;
    std::string root_; // <data>/art
    std::map<std::string, Entry> entries_;
    double now_ = 0.0;
    [[maybe_unused]] Fetch fetch_ = nullptr; // set once, before a title is asked for

    struct Shared; // what the worker and the render thread exchange
    Shared* shared_ = nullptr;
};

} // namespace ns
