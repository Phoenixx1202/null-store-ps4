#include "app/CatalogRefresh.h"

#include <sys/stat.h>

#include <orbis/libkernel.h>

#include "network_ps4.h"
#include "providers.h"

// Implemented in src/graphics/Image.cpp (the program's stb_image instance).
extern "C" int stbi_info_from_memory(const unsigned char* buffer, int length, int* x, int* y, int* components);

namespace ns {

namespace {

const char* const kBundledCatalogue = "/app0/assets/catalog.tsv";
const char* const kBundledCoverIndex = "/app0/assets/covers.idx";
constexpr int kSources = 3;

bool fetchPage(const std::string& url, std::string& out)
{
    return sourceURL(url) && portalNetworkFetch(url, out, 4 * 1024 * 1024, "");
}

bool fileExists(const std::string& path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

std::vector<Game> gamesOfSource(const std::vector<Game>& games, int source)
{
    std::vector<Game> part;
    for (const Game& game : games) {
        if (sourceForURL(game.url) == source) {
            part.push_back(game);
        }
    }
    return part;
}

} // namespace

CatalogRefresh::CatalogRefresh(std::string dataPath) : data_(std::move(dataPath)) {}

void CatalogRefresh::setStatus(const std::string& text)
{
    pthread_mutex_lock(&lock_);
    message_ = text;
    pthread_mutex_unlock(&lock_);
}

void CatalogRefresh::publish(const std::vector<Game>& games)
{
    writeFile(data_ + "/catalog.tsv", serialize(games));
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

void CatalogRefresh::start(int source, bool force)
{
    pthread_mutex_lock(&lock_);
    if (working_) {
        pthread_mutex_unlock(&lock_);
        return;
    }
    source_ = source;
    forced_ = force;
    working_ = true;
    pthread_mutex_unlock(&lock_);

    pthread_t thread;
    if (pthread_create(&thread, nullptr, entry, this) == 0) {
        pthread_detach(thread);
    } else {
        pthread_mutex_lock(&lock_);
        working_ = false;
        message_ = "Could not start the refresh. Try again.";
        pthread_mutex_unlock(&lock_);
    }
}

void* CatalogRefresh::entry(void* self)
{
    static_cast<CatalogRefresh*>(self)->run();
    return nullptr;
}

void CatalogRefresh::run()
{
    std::vector<Game> next = mergedCatalog(kBundledCatalogue, data_ + "/catalog.tsv");
    int updated = 0, cached = 0, unavailable = 0;
    const int chosen = source_;
    const bool force = forced_;

    for (int source = 1; source <= kSources; source++) {
        if (chosen && source != chosen) {
            continue;
        }
        if (!force && providerCacheFresh(data_, source)) {
            cached++;
            continue;
        }
        int pages = source == 1 ? 2 : 1;
        bool ok = true;
        std::vector<Game> part;
        std::set<int> ids;
        for (int page = 1; page <= pages; page++) {
            setStatus(std::string("Refreshing ") + sourceName(source) + " " + std::to_string(page) + "/" +
                      std::to_string(pages));
            std::string html;
            if (!fetchPage(listingURL(source, page), html)) {
                ok = false;
                break;
            }
            std::vector<Game> cards = parseSourceCards(html, source);
            if (cards.empty()) {
                ok = false;
                break;
            }
            pages = std::max(pages, sourceLastPage(html, source));
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
            sceKernelUsleep(350000);
        }
        if (ok && saveProviderCache(data_, source, part)) {
            next.erase(std::remove_if(next.begin(), next.end(),
                                      [&](const Game& game) { return sourceForURL(game.url) == source; }),
                       next.end());
            next.insert(next.end(), part.begin(), part.end());
            updated++;
            publish(next);
        } else {
            unavailable++;
        }
    }

    if (updated) {
        cacheCovers(next, chosen);
    }

    setStatus(std::to_string(updated) + " updated / " + std::to_string(cached) + " cached / " +
              std::to_string(unavailable) + " unavailable | Saved catalogues retained");
    pthread_mutex_lock(&lock_);
    working_ = false;
    pthread_mutex_unlock(&lock_);
}

// Downloads the cover of every game that has none in the package or on disk.
void CatalogRefresh::cacheCovers(std::vector<Game>& games, int chosen)
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
        const int source = sourceForURL(game.url);
        if (chosen && source != chosen) {
            continue;
        }
        const std::string folder = providerCoverPath(data_, source);
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
            saveProviderCache(data_, source, gamesOfSource(games, source), false);
            publish(games);
            setStatus("Caching provider cover art | Catalogue ready to browse");
        }
    }

    for (int source = 1; source <= kSources; source++) {
        const std::vector<Game> part = gamesOfSource(games, source);
        if (!part.empty() && (chosen == 0 || chosen == source)) {
            saveProviderCache(data_, source, part, false);
        }
    }
    publish(games);
}

} // namespace ns
