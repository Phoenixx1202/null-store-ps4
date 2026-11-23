#pragma once
#include "catalog.h"
#include <map>
#include <set>

struct ListingTitle {
    std::string key;
    std::string productId;
};

inline ListingTitle listingTitle(const std::string& title) {
    auto text = lower(title);
    for (const char* dash : {"–", "—"}) {
        const std::string glyph = dash;
        for (size_t pos = 0; (pos = text.find(glyph, pos)) != std::string::npos;)
            text.replace(pos, glyph.size(), "-");
    }

    ListingTitle result;
    bool pendingSpace = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '[' || ch == '(') {
            auto end = text.find(ch == '[' ? ']' : ')', i + 1);
            if (end != std::string::npos) {
                auto tag = text.substr(i + 1, end - i - 1);
                bool productId = tag.size() == 9 && tag.compare(0, 4, "cusa") == 0 &&
                                 tag.find_first_not_of("0123456789", 4) == std::string::npos;
                bool version = !tag.empty() && tag.front() >= '0' && tag.front() <= '9' &&
                               tag.back() >= '0' && tag.back() <= '9' &&
                               tag.find('.') != std::string::npos &&
                               tag.find_first_not_of("0123456789.") == std::string::npos;
                if (productId && !result.productId.empty() && result.productId != tag)
                    return {lower(title), {}};
                if (productId) result.productId = tag;
                if (productId || (ch == '[' && (version || tag == "ps4" || tag == "repack"))) {
                    i = end;
                    pendingSpace = true;
                    continue;
                }
            }
        }
        if (ch == ' ' || ch == '\t') {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !result.key.empty()) result.key += ' ';
        result.key += ch;
        pendingSpace = false;
    }
    return result;
}

inline std::vector<std::vector<int>> groupCatalogListings(const std::vector<Game>& games) {
    struct Candidate {
        std::vector<int> listings;
        std::set<std::string> productIds;
        std::set<std::string> years;
    };
    std::map<std::string, Candidate> candidates;
    std::vector<std::vector<int>> groups;
    for (size_t index = 0; index < games.size(); ++index) {
        auto title = listingTitle(games[index].title);
        if (title.key.empty()) {
            groups.push_back({int(index)});
            continue;
        }
        auto& candidate = candidates[title.key];
        candidate.listings.push_back(int(index));
        if (!title.productId.empty()) candidate.productIds.insert(title.productId);
        const auto& year = games[index].year;
        if (year.size() == 4 && year.find_first_not_of("0123456789") == std::string::npos)
            candidate.years.insert(year);
    }
    for (auto& pair : candidates) {
        auto& candidate = pair.second;
        if (candidate.productIds.size() > 1 || candidate.years.size() > 1) {
            for (int index : candidate.listings) groups.push_back({index});
        } else {
            groups.push_back(std::move(candidate.listings));
        }
    }
    std::sort(groups.begin(), groups.end(), [](const std::vector<int>& a, const std::vector<int>& b) {
        return a.front() < b.front();
    });
    return groups;
}
