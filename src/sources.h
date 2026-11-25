#pragma once
// Built-in adapters and user-defined selectors share URL and link validation.
#include "catalog.h"
#include "source_model.h"
#include "json_min.h"
#include <string>
#include <vector>

inline std::string selectorTag(const std::string& selector) {
    std::string tag;
    for (size_t i = 0; i < selector.size() && selector[i] != '.' && selector[i] != '#'; ++i) tag += selector[i];
    return tag;
}

inline bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

// Supports "tag", ".class", "#id", "tag.class", "tag.class.class".
inline bool selectorMatches(const std::string& tag, const std::string& selector) {
    if (selector.empty()) return false;
    if (tag.size() < 2 || tag[0] != '<') return false;
    size_t i = 0;
    std::string name;
    while (i < selector.size() && selector[i] != '.' && selector[i] != '#') name += selector[i++];
    if (!name.empty()) {
        std::string actual;
        for (size_t k = 1; k < tag.size() && !isspace((unsigned char)tag[k]) && tag[k] != '>' && tag[k] != '/'; ++k) actual += tag[k];
        if (!equalsIgnoreCase(actual, name)) return false;
    }
    while (i < selector.size()) {
        char kind = selector[i++];
        std::string value;
        while (i < selector.size() && selector[i] != '.' && selector[i] != '#') value += selector[i++];
        if (value.empty()) return false;
        if (kind == '.') {
            if (!hasClass(tag, value)) return false;
        } else {
            if (attr(tag, "id") != value) return false;
        }
    }
    return true;
}

inline std::string tagNameOf(const std::string& tag) {
    std::string name;
    if (tag.size() < 2 || tag[0] != '<') return name;
    for (size_t k = 1; k < tag.size() && !isspace((unsigned char)tag[k]) && tag[k] != '>' && tag[k] != '/'; ++k) name += tag[k];
    return name;
}

inline size_t elementEndFor(const std::string& html, const std::string& tagName, size_t from) {
    if (tagName.empty()) return from;
    int depth = 1;
    for (size_t at = from; (at = html.find('<', at)) != std::string::npos; ++at) {
        const bool closing = at + 1 < html.size() && html[at + 1] == '/';
        const size_t name = at + (closing ? 2 : 1), end = name + tagName.size();
        if (end >= html.size() || html.compare(name, tagName.size(), tagName) != 0) continue;
        if (html[end] != '>' && html[end] != '/' && !isspace((unsigned char)html[end])) continue;
        if (closing) {
            if (--depth == 0) return at;
        } else {
            const auto close = html.find('>', end);
            if (close == std::string::npos) break;
            if (html[close - 1] != '/') ++depth;
            at = close;
        }
    }
    return html.size();
}

inline size_t elementEnd(const std::string& html, const std::string& selector, size_t from) {
    return elementEndFor(html, selectorTag(selector), from);
}

// First matching element at or after `from`; returns false when none is found.
inline bool findElement(const std::string& html, const std::string& selector, size_t from, size_t& tagStart, size_t& tagEnd) {
    size_t p = from;
    while ((p = html.find('<', p)) != std::string::npos) {
        size_t close = html.find('>', p);
        if (close == std::string::npos) return false;
        if (selectorMatches(html.substr(p, close - p + 1), selector)) {
            tagStart = p;
            tagEnd = close;
            return true;
        }
        p = close + 1;
    }
    return false;
}

inline std::string elementText(const std::string& html, const std::string& selector, size_t from = 0) {
    if (selector.empty()) return "";
    size_t tagStart = 0, tagEnd = 0;
    if (!findElement(html, selector, from, tagStart, tagEnd)) return "";
    size_t end = elementEndFor(html, tagNameOf(html.substr(tagStart, tagEnd - tagStart + 1)), tagEnd + 1);
    return plain(html.substr(tagEnd + 1, end > tagEnd + 1 ? end - tagEnd - 1 : 0));
}

inline bool attributeIs(const std::string& tag, const std::string& attribute, const std::string& value) {
    return attr(tag, attribute) == value;
}

inline bool blockedLink(const std::string& tag, const SourceShape& shape) {
    for (size_t i = 0; i < shape.blocked.size(); ++i) {
        std::string rule = shape.blocked[i];
        size_t eq = rule.find('=');
        if (eq == std::string::npos) continue;
        if (attributeIs(tag, rule.substr(0, eq), rule.substr(eq + 1))) return true;
    }
    return false;
}

inline std::string firstImage(const SourceTable& table, const std::string& block, const SourceShape& shape, int source) {
    std::string attributes = "";
    size_t p = 0;
    while ((p = block.find("<img", p)) != std::string::npos) {
        size_t close = block.find('>', p);
        if (close == std::string::npos) break;
        std::string tag = block.substr(p, close - p + 1);
        for (size_t i = 0; i < shape.imageAttributes.size(); ++i) {
            std::string value = attr(tag, shape.imageAttributes[i]);
            if (value.empty() || value.compare(0,5,"data:") == 0) continue;
            std::string full = table.resolve(source, value);
            if (full.empty() || !table.owns(source, full)) continue;
            if (lower(full).find(".svg") != std::string::npos) continue;
            return full;
        }
        p = close + 1;
    }
    return attributes;
}

// A game id is derived from its page URL so it survives restarts and reordering.
inline int gameIdFromURL(const std::string& url) { return stableGameId(url); }

inline int listingNumber(const std::string& text, size_t start) {
    if (start >= text.size() || !isdigit((unsigned char)text[start])) return 0;
    long n = strtol(text.c_str() + start, nullptr, 10);
    return n > 0 && n < 500000000 ? static_cast<int>(n) : 0;
}

inline int builtInLastPage(const std::string& html, SourceKind kind) {
    const bool love = kind == SourceKind::LoveGames;
    const std::string prefix = love ? "/42-" : "/page/";
    int last = 1;
    for (size_t at = 0; (at = html.find(prefix, at)) != std::string::npos;) {
        at += prefix.size();
        const int page = listingNumber(html, at);
        const auto end = html.find_first_not_of("0123456789", at);
        if (page > last && page <= 500 && (!love || end == std::string::npos || html[end] != '-'))
            last = page;
    }
    return last;
}

inline std::vector<Game> parseBuiltInListing(const SourceTable& table, const std::string& html, int index) {
    const auto& source = *table.at(index);
    const bool ps4 = source.kind == SourceKind::PS4Torrent;
    const bool love = source.kind == SourceKind::LoveGames;
    const char* selector = ps4 ? "a.item" : love ? "div" : "article.shortstory-item";
    std::vector<Game> games;
    size_t cursor = 0, open = 0, close = 0;
    while (findElement(html, selector, cursor, open, close)) {
        cursor = close + 1;
        const auto tag = html.substr(open, cursor - open);
        const auto entryId = love ? attr(tag, "id") : std::string();
        if (love && entryId.compare(0, 7, "entryID") != 0) continue;
        // The Love Games template nests many divs and omits some closing tags.
        const auto end = love ? html.find("id=\"entryID", cursor) :
                                html.find(ps4 ? "</a>" : "</article>", cursor);
        const auto block = html.substr(cursor, end - cursor);
        if (love && end != std::string::npos) {
            auto next = html.rfind('<', end);
            cursor = next >= cursor && next != std::string::npos ? next : end;
        } else {
            cursor = end == std::string::npos ? html.size() : end + 1;
        }
        size_t anchor = 0, anchorEnd = 0;
        if (!ps4 && !findElement(block, "a", 0, anchor, anchorEnd)) continue;
        const auto href = attr(ps4 ? tag : block.substr(anchor, anchorEnd - anchor + 1), "href");
        if (href.empty()) continue;
        Game game;
        game.url = table.resolve(index, href);
        if (!table.owns(index, game.url)) continue;
        const auto path = game.url.substr(urlOrigin(game.url).size());
        if (path.empty() || path[0] != '/') continue;
        if (!ps4 && !love && path.compare(0, source.listing.size(), source.listing) != 0) continue;
        if (love && (path.find("/igry_dlja_playstation_4/") == std::string::npos ||
                     path.find("/gde_eshhe_zagruzit_igry/") != std::string::npos)) continue;
        const int number = love ? listingNumber(entryId, 7) : listingNumber(path, path.find_last_of('/') + 1);
        if (!number) continue;
        game.id = (ps4 ? 0 : love ? 1500000000 : 1000000000) + number;
        game.title = elementText(block, ps4 ? ".item__title" : love ? ".titl" : ".title");
        if (game.title.empty()) continue;
        game.image = firstImage(table, block, source.shape, index);
        if (ps4) {
            size_t begin = 0, finish = 0;
            if (findElement(block, "span", 0, begin, finish)) {
                game.year = elementText(block, "span", begin);
                game.size = elementText(block, "span", finish + 1);
            }
        } else if (love) {
            const std::string marker = "Размер:";
            auto at = block.find(marker);
            if (at != std::string::npos) {
                at += marker.size();
                game.size = plain(block.substr(at, block.find("</p>", at) - at));
            }
        } else {
            game.year = elementText(block, ".year").substr(0, 4);
        }
        games.push_back(std::move(game));
    }
    return games;
}

inline std::vector<Game> parseListingWith(const SourceTable& table, const std::string& html, int source) {
    std::vector<Game> games;
    const Source* source_ = table.at(source);
    if (!source_ || !source_->enabled || html.empty()) return games;
    if (source_->kind != SourceKind::Custom) return parseBuiltInListing(table, html, source);
    if (source_->shape.item.empty()) return games;
    size_t p = 0;
    while ((p = html.find('<', p)) != std::string::npos) {
        size_t close = html.find('>', p);
        if (close == std::string::npos) break;
        std::string tag = html.substr(p, close - p + 1);
        if (!selectorMatches(tag, source_->shape.item)) { p = close + 1; continue; }
        size_t end = elementEndFor(html, tagNameOf(tag), close + 1);
        std::string block = html.substr(close + 1, end > close + 1 ? end - close - 1 : 0);
        p = end > close + 1 ? end : close + 1;
        const bool anchorCard = tagNameOf(tag) == "a";
        size_t a = 0, ae = 0;
        if (!anchorCard && !findElement(block, "a", 0, a, ae)) continue;
        const std::string anchor = anchorCard ? tag : block.substr(a, ae - a + 1);
        Game game;
        game.url = table.resolve(source, attr(anchor, "href"));
        if (!table.owns(source, game.url)) continue;
        if (game.url.find("/page/") != std::string::npos) continue;
        game.id = gameIdFromURL(game.url);
        game.title = elementText(block, source_->shape.title);
        if (game.title.empty()) {
            size_t textEnd = block.find("</a>", ae + 1);
            game.title = anchorCard ? plain(block) : plain(block.substr(ae + 1, textEnd == std::string::npos ? 0 : textEnd - ae - 1));
        }
        if (game.title.empty()) game.title = attr(anchor, "title");
        game.image = firstImage(table, block, source_->shape, source);
        game.size = elementText(block, source_->shape.size);
        if (game.title.empty()) continue;
        games.push_back(game);
    }
    return games;
}

inline std::vector<Game> parseListing(const std::string& html, int source) { return parseListingWith(*sourceSnapshot(), html, source); }

inline std::vector<TorrentLink> parseDownloadLinksWith(const SourceTable& table, const std::string& html, int source) {
    std::vector<TorrentLink> links;
    const Source* source_ = table.at(source);
    if (!source_ || !source_->enabled || html.empty()) return links;
    std::set<std::string> seen;
    size_t p = 0;
    std::string title;
    while ((p = html.find('<', p)) != std::string::npos) {
        size_t close = html.find('>', p);
        if (close == std::string::npos) break;
        std::string tag = html.substr(p, close - p + 1);
        p = close + 1;
        if (source_->kind != SourceKind::Custom && hasClass(tag, "dl__title"))
            title = plain(html.substr(p, html.find("</", p) - p));
        if (tag.size() > 2 && tag[1] == 'a' && (isspace((unsigned char)tag[2]) || tag[2] == '>')) {
            std::string url = table.resolve(source, attr(tag, "href"));
            if (!table.owns(source, url) || blockedLink(tag, source_->shape)) continue;
            bool download = false;
            for (size_t i = 0; i < source_->shape.linkClasses.size() && !download; ++i)
                download = hasClass(tag, source_->shape.linkClasses[i]);
            std::string path = lower(url);
            for (size_t i = 0; i < source_->shape.linkMarkers.size() && !download; ++i)
                download = path.find(lower(source_->shape.linkMarkers[i])) != std::string::npos;
            if (!download) continue;
            if (source_->shape.linkMarkers.empty() && source_->shape.linkClasses.empty()) continue;
            if (!seen.insert(url).second) continue;
            size_t textEnd = html.find("</a>", close + 1);
            std::string label = title.empty() ? plain(html.substr(close + 1, textEnd == std::string::npos ? 0 : textEnd - close - 1)) : title;
            if (label.empty()) label = attr(tag, "title");
            if (label.empty()) label = "Torrent " + std::to_string(links.size() + 1);
            links.push_back(TorrentLink{label, url});
        }
    }
    return links;
}

inline std::vector<TorrentLink> parseDownloadLinks(const std::string& html, int source) {
    return parseDownloadLinksWith(*sourceSnapshot(), html, source);
}

inline std::string shapeListJSON(const std::vector<std::string>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ", ";
        out += jsonEscape(values[i]);
    }
    return out + "]";
}

inline std::string sourceToJSON(const Source& source) {
    std::string out = "  {\n";
    out += "    \"id\": " + jsonEscape(source.id) + ",\n";
    out += "    \"label\": " + jsonEscape(source.label) + ",\n";
    out += "    \"base\": " + jsonEscape(source.base) + ",\n";
    out += "    \"listing\": " + jsonEscape(source.listing) + ",\n";
    out += "    \"pageListing\": " + jsonEscape(source.pageListing) + ",\n";
    out += "    \"maxPages\": " + std::to_string(source.maxPages) + ",\n";
    out += std::string("    \"enabled\": ") + (source.enabled ? "true" : "false") + ",\n";
    out += "    \"shape\": {\n";
    out += "      \"item\": " + jsonEscape(source.shape.item) + ",\n";
    out += "      \"title\": " + jsonEscape(source.shape.title) + ",\n";
    out += "      \"size\": " + jsonEscape(source.shape.size) + ",\n";
    out += "      \"imageAttributes\": " + shapeListJSON(source.shape.imageAttributes) + ",\n";
    out += "      \"linkClasses\": " + shapeListJSON(source.shape.linkClasses) + ",\n";
    out += "      \"linkMarkers\": " + shapeListJSON(source.shape.linkMarkers) + ",\n";
    out += "      \"blocked\": " + shapeListJSON(source.shape.blocked) + "\n";
    out += "    }\n  }";
    return out;
}

inline std::string sourcesToJSON(const SourceTable& table) {
    std::string out = "{\n  \"version\": 1,\n  \"sources\": [\n";
    for (size_t i = 0; i < table.items.size(); ++i) {
        if (i) out += ",\n";
        out += sourceToJSON(table.items[i]);
    }
    if (table.items.empty()) out += "";
    out += "\n  ]\n}\n";
    return out;
}

inline std::vector<std::string> stringList(const JValue* value) {
    std::vector<std::string> out;
    if (!value) return out;
    for (size_t i = 0; i < value->items.size(); ++i)
        if (value->items[i].type == JValue::String) out.push_back(value->items[i].text);
    return out;
}

inline bool sourceFromJSON(const JValue& value, Source& source, std::string& error) {
    source = Source();
    source.label = value.str("label");
    source.base = value.str("base");
    source.id = sourceSlug(value.str("id"));
    source.listing = value.str("listing", "/");
    source.pageListing = value.str("pageListing");
    source.maxPages = value.integer("maxPages", 20);
    source.enabled = value.boolean("enabled", true);
    if (source.id.empty()) source.id = sourceSlug(source.label);
    if (source.base.compare(0, 8, "https://") != 0) { error = "base must start with https://"; return false; }
    if (urlHost(source.base).empty()) { error = "base has no host"; return false; }
    if (source.id.empty()) { error = "id and label are both empty"; return false; }
    if (source.maxPages < 1) source.maxPages = 1;
    if (source.maxPages > 500) source.maxPages = 500;
    const JValue* shape = value.get("shape");
    if (shape) {
        source.shape.item = shape->str("item");
        source.shape.title = shape->str("title");
        source.shape.size = shape->str("size");
        source.shape.imageAttributes = stringList(shape->list("imageAttributes"));
        source.shape.linkClasses = stringList(shape->list("linkClasses"));
        source.shape.linkMarkers = stringList(shape->list("linkMarkers"));
        source.shape.blocked = stringList(shape->list("blocked"));
    }
    if (source.shape.imageAttributes.empty()) {
        source.shape.imageAttributes.push_back("data-src");
        source.shape.imageAttributes.push_back("src");
    }
    if (source.shape.item.empty()) { error = "shape.item is required"; return false; }
    if (source.shape.linkClasses.empty() && source.shape.linkMarkers.empty()) {
        source.shape.linkMarkers.push_back(".torrent");
        source.shape.linkMarkers.push_back("do=download");
    }
    return true;
}

inline bool parseSourcesJSON(const std::string& text, SourceTable& table, std::string& error) {
    JValue root;
    if (text.empty()) { error = "sources.json is empty"; return false; }
    if (!jsonParse(text, root)) { error = "sources.json is not valid JSON"; return false; }
    if (root.type != JValue::Object) { error = "sources.json must be an object"; return false; }
    const JValue* list = root.list("sources");
    if (!list) { error = "sources.json needs a \"sources\" array"; return false; }
    SourceTable parsed;
    for (size_t i = 0; i < list->items.size(); ++i) {
        if (list->items[i].type != JValue::Object) { error = "source " + std::to_string(i + 1) + " is not an object"; return false; }
        Source source;
        std::string why;
        if (!sourceFromJSON(list->items[i], source, why)) {
            error = "source " + std::to_string(i + 1) + ": " + why;
            return false;
        }
        int existing = parsed.indexForId(source.id);
        if (existing) source.id = source.id + "-" + std::to_string(i + 1);
        parsed.items.push_back(source);
    }
    table = parsed;
    error.clear();
    return true;
}

inline bool loadSources(const std::string& path, SourceTable& table, std::string& error) {
    return parseSourcesJSON(readFile(path), table, error);
}

inline bool saveSources(const std::string& path, const SourceTable& table, std::string& error) {
    if (!writeFile(path, sourcesToJSON(table))) { error = "could not write " + path; return false; }
    error.clear();
    return true;
}

inline SourceTable additionalSources(const SourceTable& table) {
    SourceTable additional;
    for (const auto& source : table.items)
        if (source.kind == SourceKind::Custom) additional.items.push_back(source);
    return additional;
}

inline bool appendAdditional(SourceTable& table, Source source, std::string& error, bool migrating = false) {
    for (const auto& existing : table.items) {
        if (sameHost(urlHost(existing.base), urlHost(source.base))) {
            error = existing.kind == SourceKind::Custom ? "This site is already in your sources."
                                                       : "This site is already included in NULL_store.";
            return false;
        }
    }
    if (table.indexForId(source.id)) {
        if (!migrating) { error = "This source ID is already used. Choose a different name or ID."; return false; }
        const auto prefix = source.id.substr(0, 24);
        for (unsigned suffix = 2; table.indexForId(source.id); ++suffix)
            source.id = prefix + "-" + std::to_string(suffix);
    }
    table.items.push_back(source);
    return true;
}

inline bool installAdditionalSources(const std::string& path, const SourceTable& additional, std::string& error) {
    SourceTable effective = builtInSources();
    for (const auto& source : additional.items)
        if (!appendAdditional(effective, source, error)) return false;
    if (!saveSources(path, additional, error)) return false;
    replaceSources(effective);
    return true;
}

inline bool loadSourcesIntoTable(const std::string& path, std::string& error) {
    const auto text = readFile(path);
    SourceTable additional;
    if (!text.empty() && !parseSourcesJSON(text, additional, error)) return false;
    SourceTable effective = builtInSources();
    for (const auto& source : additional.items)
        appendAdditional(effective, source, error, true);
    replaceSources(effective);
    error.clear();
    return true;
}
