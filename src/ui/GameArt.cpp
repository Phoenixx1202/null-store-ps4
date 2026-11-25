#include "ui/GameArt.h"

#include "catalog.h"
#include "core/Log.h"
#include "sony_art.h"
#include "ui/GameText.h"
#include "ui/OfficialArt.h"

#ifndef NS_HOST
#include <pthread.h>
#include <sys/stat.h>
#endif

namespace ns {

namespace {

[[maybe_unused]] constexpr size_t kMaxPictureBytes = 3 * 1024 * 1024;
constexpr int kBackgroundWidth = 1280; // drawn dimmed behind the page: sharp enough
constexpr size_t kKept = 3;            // titles whose background stays loaded
// The picture's address appears a moment after the title's icon is found: a
// title without a background yet is looked at again after this long.
constexpr double kRetrySeconds = 4.0;

} // namespace

struct GameArt::Shared {
#ifndef NS_HOST
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
    pthread_t thread;
    bool started = false;
    bool quitting = false;
#endif
    std::string next; // the title to look up: only the newest request waits
    bool nextNeedsMedia = false;
    bool waiting = false;
    std::vector<Result> done;
};

GameArt::GameArt(Renderer& renderer, std::string dataPath)
    : renderer_(renderer), root_(std::move(dataPath) + "/art"), shared_(new Shared)
{
#ifndef NS_HOST
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 1024 * 1024);
    shared_->started = pthread_create(&shared_->thread, &attributes, entry, this) == 0;
    pthread_attr_destroy(&attributes);
    if (!shared_->started) {
        NS_LOG_ERROR("Artwork worker could not start; game pages have no background picture");
    }
#endif
}

GameArt::~GameArt()
{
#ifndef NS_HOST
    if (shared_->started) {
        pthread_mutex_lock(&shared_->mutex);
        shared_->quitting = true;
        pthread_cond_signal(&shared_->wake);
        pthread_mutex_unlock(&shared_->mutex);
        pthread_join(shared_->thread, nullptr);
    }
#endif
    entries_.clear();
    delete shared_;
}

void GameArt::enableDownloads(Fetch fetch)
{
    fetch_ = fetch;
}

GameArt::Result GameArt::load(const std::string& titleId) const
{
    Result result;
    result.titleId = titleId;
    const std::string folder = official::folder(root_, titleId);
    const std::string file = folder + "/pic0.webp";
    if (result.background.load(file, kBackgroundWidth)) {
        return result;
    }
#ifndef NS_HOST
    // Not on the console yet: fetch it from the address the cover cache noted.
    std::string address = readFile(folder + "/pic0.txt");
    while (!address.empty() && (address.back() == '\n' || address.back() == '\r' || address.back() == ' ')) {
        address.pop_back();
    }
    std::string bytes;
    if (fetch_ && (official::isPictureAddress(address, titleId) || sony::isSonyPictureAddress(address)) &&
        fetch_(address, bytes, kMaxPictureBytes, official::kSite) && official::isPicture(bytes) &&
        writeFile(file, bytes)) {
        result.background.load(file, kBackgroundWidth);
    }
#endif
    return result;
}

GameArt::Result GameArt::loadMedia(const std::string& titleId) const
{
    Result result;
    result.titleId = titleId;
    result.media = true;
    const std::string folder = official::folder(root_, titleId);
    const std::string manifest = folder + "/game-media-v1.txt";
    const auto saved = readFile(manifest);
    sony::Media media;
    const auto first = saved.find('\n');
    const auto preview = saved.substr(0, first);
    if (sony::isPreviewAddress(preview)) media.preview = preview;
    for (size_t pos = first == std::string::npos ? saved.size() : first + 1;
         pos < saved.size() && media.screenshots.size() < sony::Media::maxScreenshots;) {
        const auto next = saved.find('\n', pos);
        const auto line = saved.substr(pos, next == std::string::npos ? next : next - pos);
        if (sony::isSonyPictureAddress(line)) media.screenshots.push_back(line);
        pos = next == std::string::npos ? saved.size() : next + 1;
    }
#if !defined(NS_HOST) && NS_SONY_ART
    if (media.empty() && fetch_) {
        media = sony::titleMedia(fetch_, titleId);
        if (!media.empty()) {
            mkdir(folder.c_str(), 0777);
            std::string urls = media.preview + "\n";
            for (const auto& url : media.screenshots) urls += url + "\n";
            writeFile(manifest, urls);
        }
    }
#endif
    result.preview = media.preview;
    for (size_t i = 0; i < media.screenshots.size(); ++i) {
        const std::string file = folder + "/screenshot-" + std::to_string(i) + ".img";
        Image image;
#if !defined(NS_HOST) && NS_SONY_ART
        std::string bytes;
        if (!image.load(file, 1280) && fetch_ && fetch_(media.screenshots[i], bytes, kMaxPictureBytes, sony::kStore) &&
            official::isPicture(bytes) &&
            image.decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), 1280)) {
            writeFile(file, bytes);
        }
#else
        image.load(file, 1280);
#endif
        if (!image.empty()) result.screenshots.push_back(std::move(image));
    }
    return result;
}

void* GameArt::entry(void* self)
{
    static_cast<GameArt*>(self)->run();
    return nullptr;
}

void GameArt::run()
{
#ifndef NS_HOST
    pthread_mutex_lock(&shared_->mutex);
    for (;;) {
        while (!shared_->quitting && !shared_->waiting) {
            pthread_cond_wait(&shared_->wake, &shared_->mutex);
        }
        if (shared_->quitting) {
            break;
        }
        const std::string titleId = shared_->next;
        const bool needsMedia = shared_->nextNeedsMedia;
        shared_->waiting = false;
        pthread_mutex_unlock(&shared_->mutex);
        Result result = load(titleId);
        pthread_mutex_lock(&shared_->mutex);
        shared_->done.push_back(std::move(result));
        if (shared_->quitting || !needsMedia) continue;
        pthread_mutex_unlock(&shared_->mutex);
        Result media = loadMedia(titleId);
        pthread_mutex_lock(&shared_->mutex);
        shared_->done.push_back(std::move(media));
    }
    pthread_mutex_unlock(&shared_->mutex);
#endif
}

const GameArtwork* GameArt::get(const std::string& titleId)
{
    if (titleId.size() != 9 || !titleIdAt(titleId, 0)) {
        return nullptr; // only ever a plain title ID goes into a path
    }
    auto [entry, inserted] = entries_.try_emplace(titleId);
    Entry& item = entry->second;
    item.lastUsedFrame = renderer_.frame();
    const auto current = [&]() -> const GameArtwork* {
        return item.art.background.valid() || !item.art.screenshots.empty() || !item.art.preview.empty() ? &item.art : nullptr;
    };
    if (!inserted && (item.state != State::Missing || now_ - item.missingSince < kRetrySeconds)) return current();
    item.state = State::Loading;
    const bool needsMedia = !item.mediaRequested;
    item.mediaRequested = true;
    if (needsMedia) item.mediaPending = true;

#ifndef NS_HOST
    if (shared_->started) {
        pthread_mutex_lock(&shared_->mutex);
        const bool keepMedia = shared_->waiting && shared_->next == titleId && shared_->nextNeedsMedia;
        if (shared_->waiting && shared_->next != titleId) {
            // Re-arm a displaced lookup when its title is visited again.
            const auto dropped = entries_.find(shared_->next);
            if (dropped != entries_.end()) {
                dropped->second.state = State::Missing;
                dropped->second.missingSince = now_ - kRetrySeconds;
                if (shared_->nextNeedsMedia) {
                    dropped->second.mediaPending = false;
                    dropped->second.mediaRequested = false;
                }
            }
        }
        shared_->next = titleId;
        shared_->nextNeedsMedia = needsMedia || keepMedia;
        shared_->waiting = true;
        pthread_cond_signal(&shared_->wake);
        pthread_mutex_unlock(&shared_->mutex);
    } else {
        item.mediaPending = false;
        item.state = State::Missing;
        item.missingSince = now_;
    }
#else
    Result result = load(titleId);
    finish(result);
    if (needsMedia) {
        Result media = loadMedia(titleId);
        finish(media);
    }
#endif
    return current();
}

void GameArt::finish(Result& result)
{
    const auto found = entries_.find(result.titleId);
    if (found == entries_.end()) {
        return;
    }
    Entry& item = found->second;
    if (result.media) {
        item.mediaPending = false;
        item.art.screenshots.clear();
        for (auto& image : result.screenshots) {
            const float width = std::min(float(image.width()), image.height() * (16.0f / 9.0f));
            const float height = width * (9.0f / 16.0f);
            Image thumbnail = image.resampled((image.width() - width) * 0.5f, (image.height() - height) * 0.5f,
                                              width, height, 240, 135);
            thumbnail.roundCorners(14.0f);
            GameScreenshot shot;
            if (shot.image.create(renderer_.backend(), image) &&
                shot.thumbnail.create(renderer_.backend(), thumbnail)) item.art.screenshots.push_back(std::move(shot));
        }
        item.art.preview = std::move(result.preview);
        return;
    }
    if (!result.background.empty() && item.art.background.create(renderer_.backend(), result.background, true)) {
        item.art.readyTime = now_;
        item.state = State::Ready;
    } else {
        item.state = State::Missing;
        item.missingSince = now_;
    }
}

void GameArt::pump(double nowSeconds)
{
    now_ = nowSeconds;
#ifndef NS_HOST
    std::vector<Result> ready;
    pthread_mutex_lock(&shared_->mutex);
    ready.swap(shared_->done);
    pthread_mutex_unlock(&shared_->mutex);
    for (Result& result : ready) {
        finish(result);
    }
#endif

    // Keep the backgrounds of the titles looked at last. A title that is still
    // loading and no longer looked at is forgotten: its request may have been
    // overtaken before the worker took it, and it can be asked for again.
    const uint64_t frame = renderer_.frame();
    for (auto it = entries_.begin(); it != entries_.end();) {
        const bool stale = it->second.lastUsedFrame + 2 < frame;
        if (stale && it->second.state == State::Loading) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    while (entries_.size() > kKept) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.lastUsedFrame + 2 < frame &&
                (oldest == entries_.end() || it->second.lastUsedFrame < oldest->second.lastUsedFrame)) {
                oldest = it;
            }
        }
        if (oldest == entries_.end()) {
            break;
        }
        entries_.erase(oldest);
    }
}

} // namespace ns
