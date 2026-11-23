#include "app/CatalogRefresh.h"

#include <unordered_map>

#include <orbis/libkernel.h>

#include "network_ps4.h"
#include "providers.h"
#include "sources.h"

namespace ns {

namespace {


bool fetchPage(const std::string& url, std::string& out)
{
    return sourceURL(url) && portalNetworkFetch(url, out, 4 * 1024 * 1024, "");
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
    if (persist && !games.empty()) writeFile(data_ + "/catalog.tsv", serialize(games));
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
    std::vector<Game> next = mergedCatalog(data_ + "/catalog.tsv", *sources, "/app0/assets/catalog.tsv");
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
        std::unordered_map<int, const Game*> old;
        for (const auto& game : next) old.emplace(game.id, &game);
        const bool needsPreview = std::none_of(next.begin(), next.end(),
            [&](const Game& game) { return sources->owns(source, game.url); });
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
                const auto found = old.find(game.id);
                if (found != old.end()) {
                    if (game.image.empty()) game.image = found->second->image;
                    if (game.year.empty()) game.year = found->second->year;
                    if (game.size.empty()) game.size = found->second->size;
                }
                part.push_back(game);
            }
            if (needsPreview && !part.empty() && (page <= 2 || page % 8 == 0)) {
                auto preview = next;
                preview.insert(preview.end(), part.begin(), part.end());
                publish(preview, false);
            }
            if (ids.size() == previousCount) break;
            sceKernelUsleep(350000);
        }
        if (interrupted_ || sources != sourceSnapshot()) return;
        if (part.empty()) {
            ++unavailable;
            continue;
        }
        // Retain previous listings if the source stopped early or its pagination changed.
        for (const auto& game : next)
            if (sources->owns(source, game.url) && ids.insert(game.id).second) part.push_back(game);
        const bool saved = saveProviderCache(data_, source, part, ok, time(nullptr), *sources);
        if (ok && saved) ++updated;
        else ++unavailable;
        next.erase(std::remove_if(next.begin(), next.end(),
                                  [&](const Game& game) { return sources->owns(source, game.url); }), next.end());
        next.insert(next.end(), part.begin(), part.end());
        if (interrupted_ || sources != sourceSnapshot()) return;
        publish(next);
    }

    if (interrupted_ || sources != sourceSnapshot()) return;
    setStatus(std::to_string(updated) + " updated / " + std::to_string(cached) + " cached / " +
              std::to_string(unavailable) + " unavailable | Saved catalogues retained");
}

} // namespace ns
