#include "ui/GameArt.h"

#include "catalog.h"
#include "core/Log.h"
#include "sony_art.h"
#include "ui/GameText.h"
#include "ui/OfficialArt.h"

#ifndef NS_HOST
#include <pthread.h>
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
        shared_->waiting = false;
        pthread_mutex_unlock(&shared_->mutex);
        Result result = load(titleId);
        pthread_mutex_lock(&shared_->mutex);
        shared_->done.push_back(std::move(result));
    }
    pthread_mutex_unlock(&shared_->mutex);
#endif
}

const GameArtwork* GameArt::get(const std::string& titleId)
{
    if (titleId.size() != 9 || !titleIdAt(titleId, 0)) {
        return nullptr; // only ever a plain title ID goes into a path
    }
    auto found = entries_.find(titleId);
    if (found != entries_.end()) {
        Entry& item = found->second;
        item.lastUsedFrame = renderer_.frame();
        if (item.state == State::Ready) {
            return &item.art;
        }
        if (item.state != State::Missing || now_ - item.missingSince < kRetrySeconds) {
            return nullptr;
        }
        entries_.erase(found);
    }
    Entry& created = entries_[titleId];
    created.lastUsedFrame = renderer_.frame();

#ifndef NS_HOST
    if (shared_->started) {
        pthread_mutex_lock(&shared_->mutex);
        shared_->next = titleId;
        shared_->waiting = true;
        pthread_cond_signal(&shared_->wake);
        pthread_mutex_unlock(&shared_->mutex);
    } else {
        created.state = State::Missing;
        created.missingSince = now_;
    }
    return nullptr;
#else
    Result result = load(titleId);
    finish(result);
    return created.state == State::Ready ? &created.art : nullptr;
#endif
}

void GameArt::finish(Result& result)
{
    const auto found = entries_.find(result.titleId);
    if (found == entries_.end()) {
        return;
    }
    Entry& item = found->second;
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
