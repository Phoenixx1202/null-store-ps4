#include "ui/CoverCache.h"

#include <cstdio>
#include <chrono>
#include <cstring>

#include "core/Log.h"
#include "providers.h"
#include "sony_art.h"
#include "ui/GameText.h"
#include "ui/OfficialArt.h"

#ifndef NS_SONY_ART
#define NS_SONY_ART 0
#endif

#ifndef NS_HOST
#include <pthread.h>
#include <sys/stat.h>

#include <orbis/libkernel.h>

#endif

namespace ns {

namespace {

constexpr int kMaxDimension = 1024;    // decode size; the tile itself is Cover::kSize square
constexpr size_t kMaxCovers = 120;     // about 43 MB of textures: five screens of tiles
constexpr size_t kMaxFileBytes = 3 * 1024 * 1024;
constexpr float kCornerRadius = 18.0f; // 12 px on a grid card
constexpr double kRetrySeconds = 60.0; // before a tile that could not be had is tried again
const char* const kNoTitleId = "-";    // the game's page was read and names no title ID
// Written into <art>/<ID>/none. A file from before the store was asked in all
// of its regions is asked again once.
const char* const kNoneMark = "regions-5";

// The name kept for a title, "" when there is none the screens can show.
std::string keptName(const std::string& folder)
{
    std::string name = readFile(folder + "/name.txt");
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
        name.pop_back();
    }
    return name == kNoTitleId || hasNonLatin(name) ? std::string() : cleanName(name);
}

// The name was looked up: with an answer the screens can show, or none at all.
bool nameKnown(const std::string& folder)
{
    const std::string name = readFile(folder + "/name.txt");
    return !name.empty() && !hasNonLatin(name);
}

// Neither the site nor the store had a picture, when they were last asked.
bool knownWithoutIcon(const std::string& folder)
{
    return readFile(folder + "/none").compare(0, strlen(kNoneMark), kNoneMark) == 0;
}
[[maybe_unused]] constexpr size_t kMaxPending = 64; // the rows in view and their neighbours
[[maybe_unused]] constexpr int kUploadsPerFrame = 4;
[[maybe_unused]] constexpr int kDownloadWorkers = 2;
[[maybe_unused]] constexpr int kMaxSourceDimension = 2048;
[[maybe_unused]] constexpr size_t kMaxPageBytes = 4 * 1024 * 1024;
[[maybe_unused]] constexpr unsigned kDownloadPauseMicros = 100000; // between two requests of a worker

bool isTitleId(const std::string& text)
{
    return text.size() == 9 && titleIdAt(text, 0);
}

// Share of the pixels of a row that are "PlayStation blue".
int blueShare(const Image& image, int row)
{
    int blue = 0;
    int total = 0;
    const uint8_t* pixels = image.data() + static_cast<size_t>(row) * image.width() * 4;
    for (int x = 0; x < image.width(); x += 3) {
        const uint8_t* p = pixels + static_cast<size_t>(x) * 4;
        if (p[2] > p[0] * 1.35 && p[2] > p[1] * 1.05) {
            blue++;
        }
        total++;
    }
    return total ? blue * 100 / total : 0;
}

// Share of the pixels of a column, from row `top` down, that are "PlayStation blue".
int blueColumnShare(const Image& image, int column, int top)
{
    int blue = 0;
    int total = 0;
    for (int y = top; y < image.height(); y += 3) {
        const uint8_t* p = image.data() + (static_cast<size_t>(y) * image.width() + column) * 4;
        if (p[2] > p[0] * 1.35 && p[2] > p[1] * 1.05) {
            blue++;
        }
        total++;
    }
    return total ? blue * 100 / total : 0;
}

// Height of the blue banner at the top of a box-art scan (0 when there is none).
int bannerHeight(const Image& image)
{
    if (image.empty() || blueShare(image, std::min(image.height() - 1, 3)) <= 30) {
        return 0;
    }
    const int limit = image.height() * 18 / 100;
    int trim = image.height() * 7 / 100;
    for (int row = trim; row < limit; row++) {
        if (blueShare(image, row) < 15) {
            trim = row;
            break;
        }
    }
    return trim;
}

// The square tile of a picture. An official icon is square already. A
// source's cover is box art, taller than wide: it is shown whole, without
// its own banner, centred over a dark, soft enlargement of itself, so
// nothing of it is cut off.
Image tile(const Image& picture, bool boxArt)
{
    if (picture.empty()) {
        return Image();
    }
    const int size = Cover::kSize;
    const float top = boxArt ? static_cast<float>(bannerHeight(picture)) : 0.0f;
    // A scan that shows the edge of the blue case on a side: leave it out.
    int trimLeft = 0;
    int trimRight = 0;
    if (boxArt) {
        const int limit = picture.width() * 12 / 100;
        const int from = static_cast<int>(top);
        while (trimLeft < limit && blueColumnShare(picture, trimLeft, from) > 45) {
            ++trimLeft;
        }
        while (trimRight < limit && blueColumnShare(picture, picture.width() - 1 - trimRight, from) > 45) {
            ++trimRight;
        }
    }
    const float x0 = static_cast<float>(trimLeft);
    const float width = static_cast<float>(picture.width() - trimLeft - trimRight);
    const float height = static_cast<float>(picture.height()) - top;
    const float side = std::min(width, height);
    const float left = x0 + (width - side) * 0.5f;
    const float down = top + (height - side) * (boxArt ? 0.3f : 0.5f);

    Image out;
    if (!boxArt || height <= width * 1.1f) {
        out = picture.resampled(left, down, side, side, size, size);
    } else {
        // Backdrop: the cover's middle, reduced to a few pixels and enlarged
        // again (which blurs it), then darkened.
        constexpr int kBlur = 20;
        out = picture.resampled(left, down, side, side, kBlur, kBlur).resampled(0, 0, kBlur, kBlur, size, size);
        for (size_t i = 0; i < out.bytes(); i += 4) {
            uint8_t* p = out.data() + i;
            p[0] = static_cast<uint8_t>(p[0] * 2 / 5);
            p[1] = static_cast<uint8_t>(p[1] * 2 / 5);
            p[2] = static_cast<uint8_t>(p[2] * 2 / 5);
            p[3] = 255;
        }
        const int coverW = std::max(1, static_cast<int>(static_cast<float>(size) * width / height + 0.5f));
        const Image cover = picture.resampled(x0, top, width, height, coverW, size);
        const int offset = (size - coverW) / 2;
        for (int y = 0; y < size; ++y) {
            memcpy(out.data() + (static_cast<size_t>(y) * size + offset) * 4,
                   cover.data() + static_cast<size_t>(y) * coverW * 4, static_cast<size_t>(coverW) * 4);
        }
    }
    out.roundCorners(kCornerRadius);
    return out;
}

} // namespace

struct CoverCache::Shared {
#ifndef NS_HOST
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t decodeWake = PTHREAD_COND_INITIALIZER;
    pthread_cond_t downloadWake = PTHREAD_COND_INITIALIZER;
    pthread_t decodeThread;
    pthread_t downloadThreads[kDownloadWorkers];
    bool decoding = false;
    int downloaders = 0;
    std::chrono::steady_clock::time_point retryArtworkSite{};
    bool quitting = false;
#endif
    // The workers take what the screens asked for most recently first, and
    // among those the one that comes first on screen: tiles fill in reading
    // order, and what scrolled past waits.
    std::vector<Request> pending;
    std::vector<Request> downloads;
    struct Priority {
        uint64_t frame = 0; // the last frame the tile was asked for
        int order = 0;      // its place among the tiles of that frame
    };
    std::map<int, Priority> priority;

    bool before(const Request& a, const Request& b) const
    {
        const auto pa = priority.find(a.id);
        const auto pb = priority.find(b.id);
        const Priority x = pa == priority.end() ? Priority() : pa->second;
        const Priority y = pb == priority.end() ? Priority() : pb->second;
        return x.frame != y.frame ? x.frame > y.frame : x.order < y.order;
    }

    // Removes and returns the request to serve next. Call with the lock held.
    Request take(std::vector<Request>& queue) const
    {
        size_t best = 0;
        for (size_t i = 1; i < queue.size(); ++i) {
            if (before(queue[i], queue[best])) {
                best = i;
            }
        }
        Request request = std::move(queue[best]);
        queue.erase(queue.begin() + static_cast<long>(best));
        return request;
    }

    // Keeps a queue within `limit` by forgetting what is wanted least: its
    // game asks again if it comes back on screen. Call with the lock held.
    void trim(std::vector<Request>& queue, size_t limit)
    {
        while (queue.size() > limit) {
            size_t worst = 0;
            for (size_t i = 1; i < queue.size(); ++i) {
                if (before(queue[worst], queue[i])) {
                    worst = i;
                }
            }
            dropped.push_back(queue[worst].id);
            queue.erase(queue.begin() + static_cast<long>(worst));
        }
    }
    std::vector<int> dropped; // requests discarded because the user moved on
    std::vector<Result> done;
    // Title IDs read from the sources' game pages: game id -> ID or kNoTitleId.
    std::map<int, std::string> titleIds;
    std::map<int, std::string> freshIds; // not yet handed to pollTitleIds()
    bool official = true;   // tiles use the titles' official icons
    int generation = 0;     // bumped when `official` changes

    void lock()
    {
#ifndef NS_HOST
        pthread_mutex_lock(&mutex);
#endif
    }
    void unlock()
    {
#ifndef NS_HOST
        pthread_mutex_unlock(&mutex);
#endif
    }
};

CoverCache::CoverCache(Renderer& renderer, std::string basePath, std::string dataPath)
    : renderer_(renderer),
      base_(std::move(basePath)),
      data_(std::move(dataPath)),
      artRoot_(data_ + "/art"),
      idsPath_(data_ + "/title-ids.tsv"),
      shared_(new Shared)
{
    // Index of the covers packed into the PKG: "<id>\t<offset>\t<length>" lines.
    const std::string index = readFile(base_ + "/covers.idx");
    size_t at = 0;
    while (at < index.size()) {
        int id = 0;
        unsigned long long offset = 0;
        unsigned long long length = 0;
        if (sscanf(index.c_str() + at, "%d\t%llu\t%llu", &id, &offset, &length) == 3 && length &&
            length <= kMaxFileBytes) {
            packed_[id] = PackedCover{ offset, static_cast<size_t>(length) };
        }
        const auto next = index.find('\n', at);
        if (next == std::string::npos) {
            break;
        }
        at = next + 1;
    }

    // Title IDs learnt in earlier runs: "<game id>\t<title ID or ->" lines.
    const std::string saved = readFile(idsPath_);
    for (at = 0; at < saved.size();) {
        size_t end = saved.find('\n', at);
        if (end == std::string::npos) {
            end = saved.size();
        }
        const std::string line = saved.substr(at, end - at);
        const size_t tab = line.find('\t');
        const int id = atoi(line.c_str());
        if (tab != std::string::npos && id > 0) {
            const std::string value = line.substr(tab + 1);
            if (isTitleId(value) || value == kNoTitleId) {
                shared_->titleIds[id] = value;
            }
        }
        at = end + 1;
    }
    shared_->freshIds = shared_->titleIds;

#ifndef NS_HOST
    mkdir(artRoot_.c_str(), 0777);
    // Decoding a JPEG needs more stack than the C library's small default.
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 1024 * 1024);
    shared_->decoding = pthread_create(&shared_->decodeThread, &attributes, decodeEntry, this) == 0;
    pthread_attr_destroy(&attributes);
    if (!shared_->decoding) {
        NS_LOG_ERROR("Cover worker could not start; covers are decoded on the render thread");
    }
#endif
}

CoverCache::~CoverCache()
{
#ifndef NS_HOST
    pthread_mutex_lock(&shared_->mutex);
    shared_->quitting = true;
    pthread_cond_broadcast(&shared_->decodeWake);
    pthread_cond_broadcast(&shared_->downloadWake);
    const int downloaders = shared_->downloaders;
    pthread_mutex_unlock(&shared_->mutex);
    if (shared_->decoding) {
        pthread_join(shared_->decodeThread, nullptr);
    }
    for (int i = 0; i < downloaders; ++i) {
        pthread_join(shared_->downloadThreads[i], nullptr);
    }
#endif
    entries_.clear();
    delete shared_;
}

void CoverCache::setOfficialArt(bool on)
{
    shared_->lock();
    const bool changed = shared_->official != on;
    shared_->official = on;
    if (changed) {
        shared_->generation++;
        shared_->pending.clear();
        shared_->downloads.clear();
        shared_->priority.clear();
    }
    const int generation = shared_->generation;
    shared_->unlock();
    if (changed) {
        generation_ = generation;
        entries_.clear();
    }
}

void CoverCache::enableDownloads(Fetch fetch)
{
#ifndef NS_HOST
    if (!fetch || fetch_ || !shared_->decoding) {
        return;
    }
    fetch_ = fetch;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 1024 * 1024);
    pthread_mutex_lock(&shared_->mutex);
    for (int i = 0; i < kDownloadWorkers; ++i) {
        if (pthread_create(&shared_->downloadThreads[shared_->downloaders], &attributes, downloadEntry, this) == 0) {
            shared_->downloaders++;
        }
    }
    const int started = shared_->downloaders;
    pthread_mutex_unlock(&shared_->mutex);
    pthread_attr_destroy(&attributes);
    if (!started) {
        NS_LOG_ERROR("Cover download workers could not start; only saved covers are shown");
    }
#else
    (void)fetch;
#endif
}

// The title ID of the request's game: from its catalogue entry, or as read
// from its page. "" when it is not known yet, kNoTitleId when it has none.
std::string CoverCache::titleIdOf(const Request& request) const
{
    if (!request.titleId.empty()) {
        return request.titleId;
    }
    shared_->lock();
    const auto found = shared_->titleIds.find(request.id);
    const std::string id = found == shared_->titleIds.end() ? std::string() : found->second;
    shared_->unlock();
    return id;
}

void CoverCache::learn(int gameId, const std::string& titleId) const
{
    shared_->lock();
    shared_->titleIds[gameId] = titleId;
    shared_->freshIds[gameId] = titleId;
    if (FILE* file = fopen(idsPath_.c_str(), "ab")) {
        fprintf(file, "%d\t%s\n", gameId, titleId.c_str());
        fclose(file);
    }
    shared_->unlock();
}

bool CoverCache::pollNames(std::map<std::string, std::string>& names)
{
    const bool any = !freshNames_.empty();
    for (auto& learnt : freshNames_) {
        names[learnt.first] = std::move(learnt.second);
    }
    freshNames_.clear();
    return any;
}

bool CoverCache::pollTitleIds(std::map<int, std::string>& ids)
{
    shared_->lock();
    const bool any = !shared_->freshIds.empty();
    for (const auto& learnt : shared_->freshIds) {
        if (isTitleId(learnt.second)) {
            ids[learnt.first] = learnt.second;
        }
    }
    shared_->freshIds.clear();
    shared_->unlock();
    return any;
}

// A local tile can be displayed while the network workers look for its official artwork.
bool CoverCache::resolve(const Request& request, Result& result) const
{
    result.id = request.id;
    result.generation = request.generation;
    shared_->lock();
    const bool useOfficial = shared_->official;
    shared_->unlock();
    const std::string titleId = useOfficial ? titleIdOf(request) : std::string(kNoTitleId);
    const bool network = fetch_ != nullptr && !request.networkDone;
    Image picture;
    bool needsOfficial = titleId.empty() && !request.pageUrl.empty();

    if (isTitleId(titleId)) {
        const std::string folder = official::folder(artRoot_, titleId);
        const std::string name = keptName(folder);
        if (!name.empty()) {
            result.titleId = titleId;
            result.name = name;
        }
        if (picture.load(folder + "/icon0.webp", kMaxDimension)) {
            result.image = tile(picture, false);
            return network && !nameKnown(folder);
        }
        needsOfficial = !knownWithoutIcon(folder);
    }

    // No official icon: the cover of the source.
    for (const std::string& path : request.paths) {
        std::vector<uint8_t> bytes;
        if (readBinaryFile(path, bytes, kMaxFileBytes) && picture.decode(bytes.data(), bytes.size(), kMaxDimension)) {
            break;
        }
    }
    if (picture.empty()) {
        const auto packed = packed_.find(request.id);
        if (packed != packed_.end()) {
            if (FILE* file = fopen((base_ + "/covers.pack").c_str(), "rb")) {
                std::vector<uint8_t> bytes(packed->second.length);
                if (!fseek(file, static_cast<long>(packed->second.offset), SEEK_SET) &&
                    fread(bytes.data(), 1, bytes.size(), file) == bytes.size()) {
                    picture.decode(bytes.data(), bytes.size(), kMaxDimension);
                }
                fclose(file);
            }
        }
    }
    result.image = tile(picture, true);
    result.retryOfficial = needsOfficial && fetch_ != nullptr;
    return network && (needsOfficial || (picture.empty() && !request.triedSource && !request.imageUrl.empty()));
}

// Fetches whatever the game's tile still lacks, in order: its title ID, the
// title's official icon, and failing that the source's cover.
void CoverCache::fetchAll(const Request& request) const
{
#ifndef NS_HOST
    if (request.fetchSourceFirst) {
        std::string bytes;
        Image check;
        if (sourceURL(request.imageUrl) && fetch_(request.imageUrl, bytes, kMaxFileBytes, request.pageUrl) &&
            check.decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), kMaxSourceDimension)) {
            const auto slash = request.savePath.find_last_of('/');
            if (slash != std::string::npos) mkdir(request.savePath.substr(0, slash).c_str(), 0777);
            writeFile(request.savePath, bytes);
        }
        return;
    }
    shared_->lock();
    const bool useOfficial = shared_->official;
    shared_->unlock();
    std::string titleId = useOfficial ? titleIdOf(request) : std::string(kNoTitleId);
    if (titleId.empty() && sourceURL(request.pageUrl)) {
        std::string page;
        if (fetch_(request.pageUrl, page, kMaxPageBytes, "")) {
            titleId = findPs4TitleId(page);
            learn(request.id, titleId.empty() ? kNoTitleId : titleId);
        }
    }

    bool haveIcon = false;
    if (isTitleId(titleId)) {
        const std::string folder = official::folder(artRoot_, titleId);
        mkdir(folder.c_str(), 0777);
        Image cachedIcon;
        haveIcon = cachedIcon.load(folder + "/icon0.webp", kMaxDimension);
        const bool wantIcon = !haveIcon && !knownWithoutIcon(folder);
        const bool wantName = !nameKnown(folder);
        std::string name;
        bool siteNamedNone = false; // the site's page was read and names no icon
        if (wantIcon || wantName) {
            std::string page;
            const auto started = std::chrono::steady_clock::now();
            shared_->lock();
            const bool available = started >= shared_->retryArtworkSite;
            shared_->unlock();
            if (available && fetch_(std::string(official::kSite) + titleId, page, 1024 * 1024, "")) {
                name = official::nameFromPage(page, titleId);
                if (hasNonLatin(name)) {
                    name.clear(); // "復活邪神2": the store may know it in Latin letters
                }
                const std::string icon = official::pictureAddress(page, titleId, "icon0.webp");
                std::string bytes;
                if (icon.empty()) {
                    siteNamedNone = true;
                } else if (wantIcon && fetch_(icon, bytes, kMaxFileBytes, official::kSite) &&
                           official::isPicture(bytes)) {
                    haveIcon = writeFile(folder + "/icon0.webp", bytes);
                }
                // The game page fetches the background itself, from here.
                const std::string background = official::pictureAddress(page, titleId, "pic0.webp");
                if (!background.empty() && !official::fileExists(folder + "/pic0.txt")) {
                    writeFile(folder + "/pic0.txt", background + "\n");
                }
            } else if (available && std::chrono::steady_clock::now() - started >= std::chrono::seconds(5)) {
                shared_->lock();
                shared_->retryArtworkSite = std::chrono::steady_clock::now() + std::chrono::seconds(45);
                shared_->unlock();
            }
        }
        // What orbispatches.com does not have, the PlayStation Store may.
        bool storeAnswered = false;
        if (NS_SONY_ART && ((wantIcon && !haveIcon) || (wantName && name.empty()))) {
            sony::Art art;
            storeAnswered = sony::titleArt(fetch_, titleId, art);
            if (name.empty()) {
                name = art.name;
            }
            std::string bytes;
            if (wantIcon && !haveIcon && !art.icon.empty() && fetch_(art.icon, bytes, kMaxFileBytes, sony::kStore) &&
                official::isPicture(bytes)) {
                haveIcon = writeFile(folder + "/icon0.webp", bytes);
            }
            if (!art.background.empty() && !official::fileExists(folder + "/pic0.txt")) {
                writeFile(folder + "/pic0.txt", art.background + "\n");
            }
        }
        if (wantName && (!name.empty() || storeAnswered || siteNamedNone)) {
            // Asked once: a title without a known name keeps the source's.
            writeFile(folder + "/name.txt", (name.empty() ? std::string(kNoTitleId) : cleanName(name)) + "\n");
        }
        if (wantIcon && !haveIcon && siteNamedNone && (!NS_SONY_ART || storeAnswered)) {
            writeFile(folder + "/none", std::string(kNoneMark) + "\n"); // neither has a picture of this title
        }
    }

#else
    (void)request;
#endif
}

void* CoverCache::decodeEntry(void* self)
{
    static_cast<CoverCache*>(self)->decodeLoop();
    return nullptr;
}

void* CoverCache::downloadEntry(void* self)
{
    static_cast<CoverCache*>(self)->downloadLoop();
    return nullptr;
}

void CoverCache::decodeLoop()
{
#ifndef NS_HOST
    pthread_mutex_lock(&shared_->mutex);
    for (;;) {
        while (!shared_->quitting && shared_->pending.empty()) {
            pthread_cond_wait(&shared_->decodeWake, &shared_->mutex);
        }
        if (shared_->quitting) {
            break;
        }
        Request request = shared_->take(shared_->pending);
        const bool workers = shared_->downloaders > 0;
        pthread_mutex_unlock(&shared_->mutex);
        if (!workers) {
            request.networkDone = true; // nothing can be fetched: use what there is
        }
        Result result;
        const bool fetchFirst = resolve(request, result);
        pthread_mutex_lock(&shared_->mutex);

        if (fetchFirst) {
            request.fetchSourceFirst = !request.triedSource && result.image.empty() && sourceURL(request.imageUrl);
            if (!result.image.empty()) {
                result.provisional = true;
                shared_->done.push_back(std::move(result));
            }
            shared_->downloads.push_back(std::move(request));
            shared_->trim(shared_->downloads, kMaxPending);
            pthread_cond_signal(&shared_->downloadWake);
        } else {
            shared_->done.push_back(std::move(result));
        }
    }
    pthread_mutex_unlock(&shared_->mutex);
#endif
}

void CoverCache::downloadLoop()
{
#ifndef NS_HOST
    pthread_mutex_lock(&shared_->mutex);
    for (;;) {
        while (!shared_->quitting && shared_->downloads.empty()) {
            pthread_cond_wait(&shared_->downloadWake, &shared_->mutex);
        }
        if (shared_->quitting) {
            break;
        }
        Request request = shared_->take(shared_->downloads);
        pthread_mutex_unlock(&shared_->mutex);
        fetchAll(request);
        sceKernelUsleep(kDownloadPauseMicros);
        pthread_mutex_lock(&shared_->mutex);

        // Back to the decoder, which builds the tile from what was saved.
        request.networkDone = !request.fetchSourceFirst;
        request.triedSource |= request.fetchSourceFirst;
        shared_->pending.push_back(std::move(request));
        pthread_cond_signal(&shared_->decodeWake);
    }
    pthread_mutex_unlock(&shared_->mutex);
#endif
}

const Cover* CoverCache::get(const Game& game, const std::string& titleId, int order)
{
    auto found = entries_.find(game.id);
    if (found != entries_.end()) {
        Entry& entry = found->second;
        entry.lastUsedFrame = renderer_.frame();
        if (entry.state == State::Ready) {
            if (entry.upgrading) wanted_.push_back({ game.id, order });
            if (entry.upgrading || !entry.retryAt || now_ < entry.retryAt) return &entry.cover;
            entry.upgrading = true;
            entry.retryAt = 0;
        } else {
            if (entry.state == State::Loading) wanted_.push_back({ game.id, order });
            if (entry.state != State::Missing || now_ - entry.missingSince < kRetrySeconds) return nullptr;
            entry.state = State::Loading;
        }
    }

    Request request;
    request.id = game.id;
    request.titleId = isTitleId(titleId) ? titleId : describe(game).titleId;
    request.pageUrl = game.url;
    request.imageUrl = game.image;
    const std::string file = "/" + std::to_string(game.id) + ".jpg";
    request.savePath = providerCoverPath(data_, sourceForURL(game.url)) + file;
    request.paths = { request.savePath, data_ + "/covers" + file, base_ + "/covers" + file };
    request.generation = generation_;
    Entry& entry = entries_[game.id];
    entry.lastUsedFrame = renderer_.frame();

#ifndef NS_HOST
    if (shared_->decoding) {
        pthread_mutex_lock(&shared_->mutex);
        shared_->priority[game.id] = Shared::Priority{ renderer_.frame(), order };
        shared_->pending.push_back(std::move(request));
        shared_->trim(shared_->pending, kMaxPending);
        pthread_cond_signal(&shared_->decodeWake);
        pthread_mutex_unlock(&shared_->mutex);
        return entry.state == State::Ready ? &entry.cover : nullptr;
    }
#endif
    request.networkDone = true;
    Result result;
    resolve(request, result);
    finish(result);
    return entry.state == State::Ready ? &entry.cover : nullptr;
}

void CoverCache::finish(Result& result)
{
    if (!result.name.empty()) {
        freshNames_[result.titleId] = result.name;
    }
    if (result.generation != generation_) {
        return; // made under the previous setting
    }
    const auto found = entries_.find(result.id);
    if (found == entries_.end()) {
        return; // evicted while it was loading
    }
    Entry& entry = found->second;
    entry.upgrading = result.provisional;
    entry.retryAt = !result.provisional && result.retryOfficial ? now_ + kRetrySeconds : 0;
    if (!result.image.empty() && entry.cover.texture.create(renderer_.backend(), result.image, true)) {
        entry.cover.readyTime = now_;
        entry.state = State::Ready;
    } else if (entry.state != State::Ready) {
        entry.state = State::Missing;
        entry.missingSince = now_;
    }
}

void CoverCache::pump(double nowSeconds)
{
    now_ = nowSeconds;
#ifndef NS_HOST
    std::vector<Result> ready;
    std::vector<int> dropped;
    pthread_mutex_lock(&shared_->mutex);
    const size_t take = std::min(shared_->done.size(), static_cast<size_t>(kUploadsPerFrame));
    for (size_t i = 0; i < take; i++) {
        ready.push_back(std::move(shared_->done[i]));
    }
    shared_->done.erase(shared_->done.begin(), shared_->done.begin() + static_cast<long>(take));
    dropped.swap(shared_->dropped);
    // What is on screen now keeps its turn; what was finished or given up
    // needs none.
    for (const Wanted& tile : wanted_) {
        shared_->priority[tile.id] = Shared::Priority{ renderer_.frame(), tile.order };
    }
    for (const Result& result : ready) {
        if (!result.provisional) shared_->priority.erase(result.id);
    }
    for (int id : dropped) {
        shared_->priority.erase(id);
    }
    pthread_mutex_unlock(&shared_->mutex);

    for (Result& result : ready) {
        finish(result);
    }
    for (int id : dropped) {
        const auto found = entries_.find(id);
        if (found != entries_.end()) {
            if (found->second.state == State::Loading) entries_.erase(found);
            else {
                found->second.upgrading = false;
                found->second.retryAt = now_ + 1;
            }
        }
    }
#endif
    wanted_.clear();

    // Keep the tiles shown most recently.
    const uint64_t frame = renderer_.frame();
    while (entries_.size() > kMaxCovers) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.state != State::Loading && it->second.lastUsedFrame + 1 < frame &&
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
