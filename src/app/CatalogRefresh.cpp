#include "app/CatalogRefresh.h"

#include <sys/stat.h>

#include <orbis/libkernel.h>

#include "network_ps4.h"
#include "providers.h"
#include "sources.h"

// Implemented in src/graphics/Image.cpp (the program's stb_image instance).
extern "C" int stbi_info_from_memory(const unsigned char* buffer, int length, int* x, int* y, int* components);

namespace ns {

namespace {


const char* const kBundledCoverIndex = "/app0/assets/covers.idx";


bool fetchPage(const std::string& url, std::string& out)
{
    return sourceURL(url) && portalNetworkFetch(url, out, 4 * 1024 * 1024, "");
}

bool fileExists(const std::string& path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

} // namespace

CatalogRefresh::CatalogRefresh(std::string dataPath) : data_(std::move(dataPath)) {}

CatalogRefresh::~CatalogRefresh()
{
    pthread_mutex_lock(&lock_);
    closing_ = true;
    interrupted_ = true;
    pthread_mutex_unlock(&lock_);
    if (joinable_) pthread_join(thread_, nullptr);
    pthread_mutex_destroy(&lock_);
}

void CatalogRefresh::setStatus(const std::string& text)
{
    pthread_mutex_lock(&lock_);
    message_ = text;
    pthread_mutex_unlock(&lock_);
}

void CatalogRefresh::publish(const std::vector<Game>& games, bool persist)
{
    if (persist) writeFile(data_ + "/catalog.tsv", serialize(games));
    pthread_mutex_lock(&lock_);
    refreshed_ = games;
    ready_ = true;
    pthread_mutex_unlock(&lock_);
}

bool CatalogRefresh::pollStatus(std::string& current)
{
    pthread_mutex_lock(&lock_);
    const bool changed = !message_.empty() && message_ != current;
    if (changed) {
        current = message_;
    }
    pthread_mutex_unlock(&lock_);
    return changed;
}

bool CatalogRefresh::pollCatalogue(std::vector<Game>& games)
{
    pthread_mutex_lock(&lock_);
    const bool ready = ready_;
    if (ready) {
        games = refreshed_;
        ready_ = false;
    }
    pthread_mutex_unlock(&lock_);
    return ready;
}

void CatalogRefresh::start(int source, bool force, bool sourcesChanged)
{
    pthread_mutex_lock(&lock_);
    if (working_) {
        if (force || sourcesChanged) {
            source_ = source;
            forced_ = force;
            pending_ = true;
            interrupted_ = true;
            ready_ = false;
        }
        pthread_mutex_unlock(&lock_);
        return;
    }
    source_ = source;
    forced_ = force;
    working_ = true;
    pthread_mutex_unlock(&lock_);

    if (joinable_) pthread_join(thread_, nullptr);
    joinable_ = pthread_create(&thread_, nullptr, entry, this) == 0;
    if (!joinable_) {
        pthread_mutex_lock(&lock_);
        working_ = false;
        message_ = "Could not start the refresh. Try again.";
        pthread_mutex_unlock(&lock_);
    }
}

void* CatalogRefresh::entry(void* self)
{
    auto* refresh = static_cast<CatalogRefresh*>(self);
    for (;;) {
        refresh->run();
        pthread_mutex_lock(&refresh->lock_);
        const bool again = refresh->pending_ && !refresh->closing_;
        refresh->pending_ = false;
        if (again) refresh->interrupted_ = false;
        else refresh->working_ = false;
        pthread_mutex_unlock(&refresh->lock_);
        if (!again) break;
    }
    return nullptr;
}

void CatalogRefresh::run()
{
    const auto sources = sourceSnapshot();
    std::vector<Game> next = mergedCatalog(data_ + "/catalog.tsv", *sources);
    int updated = 0, cached = 0, unavailable = 0;
    pthread_mutex_lock(&lock_);
    const int chosen = source_;
    const bool force = forced_;
    pthread_mutex_unlock(&lock_);

    // Check additions first, so a phone save can show new cards during a long built-in refresh.
    std::vector<int> order;
    for (int i = 1; i <= static_cast<int>(sources->items.size()); ++i)
        if (sources->at(i)->kind == SourceKind::Custom) order.push_back(i);
    for (int i = 1; i <= static_cast<int>(sources->items.size()); ++i)
        if (sources->at(i)->kind != SourceKind::Custom) order.push_back(i);
    for (int source : order) {
        if (interrupted_ || sources != sourceSnapshot()) return;
        if (!sources->at(source)->enabled || (chosen && source != chosen)) {
            continue;
        }
        if (!force && providerCacheFresh(data_, source, time(nullptr), *sources)) {
            cached++;
            continue;
        }
        const auto& provider = *sources->at(source);
        const bool builtIn = provider.kind != SourceKind::Custom;
        int pages = builtIn ? 1 : provider.maxPages;
        bool ok = true;
        std::vector<Game> part;
        std::set<int> ids;
        for (int page = 1; page <= pages; page++) {
            if (interrupted_ || sources != sourceSnapshot()) return;
            setStatus(std::string("Refreshing ") + sources->label(source) + " " + std::to_string(page) + "/" +
                      std::to_string(pages));
            std::string html;
            const auto url = sources->listingURL(source, page);
            if (url.empty()) break;
            if (!fetchPage(url, html)) {
                ok = false;
                break;
            }
            std::vector<Game> cards = parseListingWith(*sources, html, source);
            if (cards.empty()) {
                if (builtIn) ok = false;
                break;
            }
            if (builtIn) pages = std::max(pages, builtInLastPage(html, provider.kind));
            size_t previousCount = ids.size();
            for (Game& game : cards) {
                if (!ids.insert(game.id).second) {
                    continue;
                }
                // Keep what an earlier refresh knew and this listing omits.
                for (const Game& old : next) {
                    if (old.id == game.id) {
                        if (game.image.empty()) game.image = old.image;
                        if (game.year.empty()) game.year = old.year;
                        break;
                    }
                }
                part.push_back(game);
            }
            if (page == 1 && !part.empty() && std::none_of(next.begin(), next.end(),
                    [&](const Game& g) { return sources->owns(source, g.url); })) {
                auto preview = next;
                preview.insert(preview.end(), part.begin(), part.end());
                publish(preview, false);
            }
            if (ids.size() == previousCount) break;
            sceKernelUsleep(350000);
        }
        if (interrupted_ || sources != sourceSnapshot()) return;
        if (ok && saveProviderCache(data_, source, part, true, time(nullptr), *sources)) {
            next.erase(std::remove_if(next.begin(), next.end(),
                                      [&](const Game& game) { return sources->indexForURL(game.url) == source; }),
                       next.end());
            next.insert(next.end(), part.begin(), part.end());
            updated++;
            publish(next);
        } else {
            unavailable++;
        }
    }

    if (interrupted_ || sources != sourceSnapshot()) return;
    publish(mergedCatalog(data_ + "/catalog.tsv", *sources));
    if (updated && sources == sourceSnapshot()) {
        cacheCovers(next, chosen, *sources);
    }

    setStatus(std::to_string(updated) + " updated / " + std::to_string(cached) + " cached / " +
              std::to_string(unavailable) + " unavailable | Saved catalogues retained");
}

// Downloads the cover of every game that has none in the package or on disk.
void CatalogRefresh::cacheCovers(const std::vector<Game>& games, int chosen, const SourceTable& sources)
{
    std::set<int> packaged;
    const std::string index = readFile(kBundledCoverIndex);
    for (size_t p = 0; p < index.size();) {
        packaged.insert(atoi(index.c_str() + p));
        const auto end = index.find('\n', p);
        if (end == std::string::npos) {
            break;
        }
        p = end + 1;
    }

    int artwork = 0;
    for (const Game& game : games) {
        if (interrupted_ || &sources != sourceSnapshot().get()) return;
        const int source = sources.indexForURL(game.url);
        if (!source || (chosen && source != chosen)) {
            continue;
        }
        const std::string folder = providerCoverPath(data_, source, sources);
        mkdir(folder.c_str(), 0777);
        const std::string filename = std::to_string(game.id) + ".jpg";
        if (packaged.count(game.id) || fileExists(folder + "/" + filename) ||
            fileExists(data_ + "/covers/" + filename)) {
            continue;
        }

        std::string bytes;
        if (sourceURL(game.image) && portalNetworkFetch(game.image, bytes, 3 * 1024 * 1024, "")) {
            int w = 0, h = 0, components = 0;
            if (stbi_info_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                      static_cast<int>(bytes.size()), &w, &h, &components) &&
                w <= 2048 && h <= 2048) {
                writeFile(folder + "/" + filename, bytes);
            }
        }
        sceKernelUsleep(200000);
        if (++artwork % 20 == 0) {
            setStatus("Caching provider cover art | Catalogue ready to browse");
        }
    }


}

} // namespace ns
