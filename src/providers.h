#pragma once
#include "catalog.h"
#include "source_model.h"
#include "queue_store.h"
#include <ctime>
#include <sys/stat.h>

inline std::string providerSlug(int source, const SourceTable& table = *sourceSnapshot()) {
    const Source* item = table.at(source);
    std::string slug = item ? sourceSlug(item->id) : "";
    if (slug.empty()) slug = "source-" + std::to_string(source);
    return slug;
}

inline std::string providerCachePath(const std::string& root, int source, const SourceTable& table = *sourceSnapshot()) {
    const auto* item = table.at(source);
    if (item && item->kind != SourceKind::Custom)
        return root + "/catalog-builtin-" + providerSlug(source, table) + ".tsv";
    return root + "/catalog-source-" + providerSlug(source, table) + ".tsv";
}

inline std::vector<Game> providerCached(const std::string& root, int source, const SourceTable& table = *sourceSnapshot()) {
    auto games = loadCatalog(loadQueue(providerCachePath(root, source, table)));
    const auto* item = table.at(source);
    if (games.empty() && item && item->kind != SourceKind::Custom)
        games = loadCatalog(loadQueue(root + "/catalog-provider-" + std::to_string(source) + ".tsv"));
    for (auto& g : games) if (table.indexForURL(g.url) != source) return {};
    return games;
}

inline bool saveProviderCache(const std::string& root, int source, const std::vector<Game>& games, bool stamp = true, time_t now = time(0), const SourceTable& table = *sourceSnapshot()) {
    if (games.empty()) return false;
    for (auto& g : games) if (table.indexForURL(g.url) != source) return false;
    if (!saveQueue(providerCachePath(root, source, table), serialize(games), 0644)) return false;
    return !stamp || durableReplace(providerCachePath(root, source, table) + ".time", std::to_string((long long)now), 0644);
}

inline bool providerCacheFresh(const std::string& root, int source, time_t now = time(0), const SourceTable& table = *sourceSnapshot()) {
    auto timestamp = strtoll(readFile(providerCachePath(root, source, table) + ".time").c_str(), 0, 10);
    return timestamp > 0 && now >= timestamp && now - timestamp < 6 * 60 * 60 && !providerCached(root, source, table).empty();
}

inline std::string providerCoverPath(const std::string& root, int source, const SourceTable& table = *sourceSnapshot()) {
    const auto* item = table.at(source);
    if (item && item->kind != SourceKind::Custom)
        return root + "/covers/provider-" + std::to_string(source);
    return root + "/covers/source-" + providerSlug(source, table);
}

// Fetched rows take precedence. A short response or unreadable cache must not erase other listings.
inline std::vector<Game> mergedCatalog(const std::string& cacheRoot, const SourceTable& table = *sourceSnapshot(),
                                       const std::string& bundledPath = {}) {
    const auto slash = cacheRoot.find_last_of('/');
    const auto root = slash == std::string::npos ? std::string(".") : cacheRoot.substr(0, slash);
    std::vector<Game> out;
    std::set<int> seen;
    auto append = [&](std::vector<Game> rows, bool builtInsOnly = false) {
        for (auto& game : rows) {
            const auto* source = table.at(table.indexForURL(game.url));
            if (source && (!builtInsOnly || source->kind != SourceKind::Custom) && seen.insert(game.id).second)
                out.push_back(std::move(game));
        }
    };
    for (int source = 1; source <= static_cast<int>(table.items.size()); ++source)
        append(providerCached(root, source, table));
    append(loadCatalog(loadQueue(cacheRoot)));
    if (!bundledPath.empty()) append(loadCatalog(readFile(bundledPath)), true);
    return out;
}
