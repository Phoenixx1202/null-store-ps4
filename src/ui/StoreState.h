#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "download_model.h"
#include "download_paths.h"
#include "installer.h"
#include "ui/GameText.h"
#include "qr.h"

// -----------------------------------------------------------------------------
// Everything the store shows and how the controller changes it: the catalogue
// with its filters, the download queue and the pickers that open on top.
//
// This is state only. StoreView (src/ui/StoreView.h) draws it and the
// Application (src/app/Application.h) carries out the *Request flags the
// actions raise (start a download, install a package, ...).
// -----------------------------------------------------------------------------
namespace ns {

enum Action {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    CONFIRM,
    BACK,
    SEARCH,   // Triangle: search the catalogue, install a finished download
    FAVORITE, // Square: save a game, cancel and delete a download
    TAB_PREV,
    TAB_NEXT,
    SORT,
    PAGE_PREV,
    PAGE_NEXT,
    SETTINGS  // OPTIONS on the catalogue
};

// The rows of Settings, top to bottom.
enum SettingsRow {
    SETTING_SOURCE,
    SETTING_SORT,
    SETTING_LOCATION,
    SETTING_DEBRID,
    SETTING_DEBRID_KEY,
    SETTING_ARTWORK,
    SETTING_REFRESH,
    SETTING_COUNT
};

// Debrid services: they fetch a torrent on their own servers and hand it out
// as direct downloads. 0 is none.
constexpr int kDebridCount = 6;
inline const char* debridName(int service)
{
    static const char* const names[kDebridCount] = { "Off", "Real-Debrid", "AllDebrid", "Premiumize", "TorBox",
                                                     "Debrid-Link" };
    return names[service >= 0 && service < kDebridCount ? service : 0];
}
// Where the account's API key is found.
inline const char* debridKeyPage(int service)
{
    static const char* const pages[kDebridCount] = { "Torrents download directly", "real-debrid.com/apitoken",
                                                     "alldebrid.com/apikeys", "premiumize.me/account",
                                                     "torbox.app/settings", "debrid-link.com/webapp/apikey" };
    return pages[service >= 0 && service < kDebridCount ? service : 0];
}

inline const char* sortName(int sort)
{
    static const char* const kSorts[] = { "Recently added", "Title A-Z", "Release year", "Smallest first" };
    return kSorts[std::max(0, std::min(3, sort))];
}

// Collections of the side bar; Downloads is a screen of its own.
enum Tab { TAB_ALL = 0, TAB_FAVORITES = 1, TAB_RECENT = 2, TAB_DOWNLOADS = 3 };

struct StoreState {
    // The catalogue grid: eight tiles per row, three rows in view.
    static constexpr int kGridColumns = 8;
    static constexpr int kGridPage = 24;

    std::string base, data;
    std::string status = "Bundled catalogue"; // what the catalogue refresh is doing
    std::string query;
    std::string notice;

    // Catalogue -------------------------------------------------------------------
    std::vector<Game> games;
    // One tile per game: the entries of every source and version of a game
    // (same title ID, or the same name) form a group. groups[i] lists them,
    // newest version first; visible[i] is groups[i][0], the one the tile shows.
    std::vector<int> visible;
    std::vector<std::vector<int>> groups;
    std::vector<std::string> gameKeys;     // per game: what groups it
    std::vector<std::string> gameVersions; // per game: the version its entry names
    int variantSelected = 0;               // the entry chosen on the game page
    std::set<int> favorites;
    // game id -> title ID, for the entries that do not carry one: read from the
    // game's page on its source, or taken from the same game in another source.
    std::map<int, std::string> titleIds;
    // title ID -> the title's official name (orbispatches.com, the PlayStation Store).
    std::map<std::string, std::string> officialNames;
    int selected = 0, tab = 0, sort = 0, key = 0;
    bool detail = false, keyboard = false, dirty = true;
    int sourceFilter = 0;

    // Settings (OPTIONS on the catalogue) -------------------------------------------
    bool settings = false;
    int settingsSelected = 0;
    bool sourceFilterPicker = false, sortPicker = false;
    std::string setupURL;
    QrSymbol setupCode;
    void setSetupURL(const std::string& url) {
        setupURL = url;
        setupCode = {};
        if (!url.empty()) makeQrSymbol(url, setupCode);
    }
    int sourceChoice = 0, sortChoice = 0;
    int debrid = 0, debridChoice = 0;        // the Debrid service (0: none)
    std::map<int, std::string> debridKeys;   // API key per service, in <data>/debrid-keys.txt
    bool debridPicker = false;
    bool debridKeyRequest = false;           // the console's keyboard asks for the key
    bool noticeStays = false;                // the notice stays until it is cleared
    bool officialArt = true; // game tiles from orbispatches.com when it has the title
    bool refreshRequest = false, artworkChanged = false;

    // Starting a download ----------------------------------------------------------
    std::vector<TorrentLink> sources;
    int sourceSelected = 0;
    bool sourcePicker = false, sourceLoading = false, downloadRequest = false, enqueueRequest = false;
    Game downloadGame;
    std::string sourceError;

    // Download queue ---------------------------------------------------------------
    std::vector<DownloadRow> jobs;
    int jobSelected = 0;
    std::set<int> deletingJobs; // cancelled: hidden while their files are deleted
    bool cancelRequest = false;
    int cancelJob = 0;
    std::string serviceStatus = "Connecting to background service...";

    // Download location -----------------------------------------------------------
    bool downloadLocationPicker = false, downloadLocationRequest = false;
    int downloadLocationSelected = 0;
    std::string downloadBase, requestedDownloadBase;

    // Installing downloaded packages ------------------------------------------------
    bool installPicker = false, installLoading = false, installScanRequest = false, installSubmitRequest = false;
    int installJob = 0, installSelected = 0;
    std::string installPath, installError;
    std::vector<InstallPackage> packages;

    StoreState(std::string basePath, std::string dataPath) : base(std::move(basePath)), data(std::move(dataPath))
    {
        downloadBase = readFile(data + "/download-location.txt");
        while (!downloadBase.empty() && (downloadBase.back() == '\n' || downloadBase.back() == '\r')) {
            downloadBase.pop_back();
        }
        if (!validDownloadBase(data, downloadBase)) {
            downloadBase = data;
        }
        syncDownloadLocation();
        loadSettings();
        games = mergedCatalog(data + "/catalog.tsv");
        shareTitleIds();

        const std::string saved = readFile(data + "/favorites.txt");
        size_t p = 0;
        while (p < saved.size()) {
            favorites.insert(atoi(saved.c_str() + p));
            const auto end = saved.find('\n', p);
            if (end == std::string::npos) {
                break;
            }
            p = end + 1;
        }
        filter();
    }

    // One source writes the title ID into its titles; the others do not. A
    // game listed by both gets the ID from the entry that has it. Then every
    // entry gets the key that groups it with the other entries of its game.
    // Call again when the catalogue changes or more IDs become known; true
    // when the grouping changed (filter() then has to run again).
    bool shareTitleIds()
    {
        std::vector<GameText> texts(games.size());
        std::vector<std::string> names(games.size());
        std::map<std::string, std::string> byName;
        for (size_t i = 0; i < games.size(); ++i) {
            texts[i] = describe(games[i]);
            names[i] = titleKey(texts[i].title);
            if (texts[i].titleId.empty()) {
                const auto learnt = titleIds.find(games[i].id);
                if (learnt != titleIds.end()) {
                    texts[i].titleId = learnt->second;
                }
            }
            if (!texts[i].titleId.empty() && !names[i].empty()) {
                byName.emplace(names[i], texts[i].titleId);
            }
        }
        std::vector<std::string> keys(games.size());
        std::vector<std::string> versions(games.size());
        for (size_t i = 0; i < games.size(); ++i) {
            if (texts[i].titleId.empty()) {
                const auto same = byName.find(names[i]);
                if (same != byName.end()) {
                    titleIds[games[i].id] = same->second;
                    texts[i].titleId = same->second;
                }
            }
            keys[i] = !texts[i].titleId.empty() ? "id:" + texts[i].titleId
                      : !names[i].empty()       ? "name:" + names[i]
                                                : "game:" + std::to_string(games[i].id);
            versions[i] = texts[i].version;
        }
        const bool changed = keys != gameKeys;
        gameKeys.swap(keys);
        gameVersions.swap(versions);
        return changed;
    }

    // "1.10" is newer than "1.9"; a named version beats none.
    static bool newerVersion(const std::string& a, const std::string& b)
    {
        if (a.empty() || b.empty()) {
            return !a.empty() && b.empty();
        }
        size_t i = 0, j = 0;
        while (i < a.size() || j < b.size()) {
            const long x = i < a.size() ? strtol(a.c_str() + i, nullptr, 10) : 0;
            const long y = j < b.size() ? strtol(b.c_str() + j, nullptr, 10) : 0;
            if (x != y) {
                return x > y;
            }
            i = a.find('.', i);
            j = b.find('.', j);
            i = i == std::string::npos ? a.size() : i + 1;
            j = j == std::string::npos ? b.size() : j + 1;
        }
        return false;
    }

    // Settings kept between runs: "<name>=<value>" lines in <data>/settings.txt.
    // The download location has a file of its own (download-location.txt).
    void loadSettings()
    {
        const std::string text = readFile(data + "/settings.txt");
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) {
                end = text.size();
            }
            const std::string line = text.substr(at, end - at);
            const size_t equals = line.find('=');
            if (equals != std::string::npos) {
                const std::string name = line.substr(0, equals);
                const int value = atoi(line.c_str() + equals + 1);
                if (name == "source") sourceFilter = std::max(0, std::min(int(sourceSnapshot()->items.size()), value));
                if (name == "sort") sort = std::max(0, std::min(3, value));
                if (name == "official-art") officialArt = value != 0;
                if (name == "debrid") debrid = std::max(0, std::min(kDebridCount - 1, value));
            }
            at = end + 1;
        }
        // "<service>=<key>" lines.
        const std::string keys = readFile(data + "/debrid-keys.txt");
        for (size_t from = 0; from < keys.size();) {
            size_t end = keys.find('\n', from);
            if (end == std::string::npos) {
                end = keys.size();
            }
            const std::string line = keys.substr(from, end - from);
            const size_t equals = line.find('=');
            const int service = atoi(line.c_str());
            if (equals != std::string::npos && service > 0 && service < kDebridCount && equals + 1 < line.size()) {
                debridKeys[service] = line.substr(equals + 1);
            }
            from = end + 1;
        }
    }

    const std::string& debridKey() const
    {
        static const std::string none;
        const auto found = debridKeys.find(debrid);
        return found == debridKeys.end() ? none : found->second;
    }

    // The key typed for the chosen service: spaces and line breaks around it go.
    void setDebridKey(std::string key)
    {
        while (!key.empty() && isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
        while (!key.empty() && isspace(static_cast<unsigned char>(key[0]))) key.erase(0, 1);
        if (debrid <= 0) {
            return;
        }
        if (key.empty()) {
            debridKeys.erase(debrid);
        } else {
            debridKeys[debrid] = key;
        }
        std::string text;
        for (const auto& entry : debridKeys) {
            text += std::to_string(entry.first) + "=" + entry.second + "\n";
        }
        notice = !writeFile(data + "/debrid-keys.txt", text) ? "Could not save the Debrid key."
                 : key.empty()                                  ? "Debrid key removed."
                                                                : "Debrid key saved.";
    }

    void saveSettings()
    {
        const std::string text = "source=" + std::to_string(sourceFilter) + "\nsort=" + std::to_string(sort) +
                                 "\nofficial-art=" + (officialArt ? "1" : "0") + "\ndebrid=" + std::to_string(debrid) +
                                 "\n";
        if (!writeFile(data + "/settings.txt", text)) {
            notice = "Could not save settings. Check storage permissions.";
        }
    }

    // Any dialog is open: the controller talks to it, not to the screen below.
    bool dialogOpen() const
    {
        return settings || sourceFilterPicker || sortPicker || debridPicker || downloadLocationPicker || installPicker ||
               sourcePicker || keyboard;
    }

    // Leaves out the jobs whose files are being deleted, unless deleting them
    // failed. Call after every new snapshot of the queue.
    void hideDeletedJobs()
    {
        for (auto it = deletingJobs.begin(); it != deletingJobs.end();) {
            const int id = *it;
            const bool listed =
                std::any_of(jobs.begin(), jobs.end(), [id](const DownloadRow& job) { return job.id == id; });
            it = listed ? std::next(it) : deletingJobs.erase(it);
        }
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                                  [this](const DownloadRow& job) {
                                      return deletingJobs.count(job.id) && job.error.find("delete") == std::string::npos;
                                  }),
                   jobs.end());
        clampSelections();
    }

    // Points downloadLocationSelected at the entry that matches downloadBase.
    void syncDownloadLocation()
    {
        if (downloadBase == data) {
            downloadLocationSelected = 0;
            return;
        }
        for (int i = 0; i < 8; i++) {
            if (downloadBase == "/mnt/usb" + std::to_string(i) + "/NULL_store") {
                downloadLocationSelected = i + 1;
            }
        }
    }

    std::string downloadLocationName(int index) const
    {
        return index == 0 ? "Internal storage" : "USB " + std::to_string(index - 1);
    }

    std::string downloadLocationPath(int index) const
    {
        return index == 0 ? data : "/mnt/usb" + std::to_string(index - 1) + "/NULL_store";
    }

    // `follow`: the game under the cursor stays under it, wherever its tile
    // moves (when the list is regrouped in the background, not when the user
    // changes the sort, the search or the source).
    void filter(bool follow = false)
    {
        const Game* before = follow ? current() : nullptr;
        const int keepId = before ? before->id : -1;

        std::vector<int> matches;
        for (size_t i = 0; i < games.size(); ++i) {
            const Game& game = games[i];
            if ((!sourceFilter || sourceForURL(game.url) == sourceFilter) &&
                (tab != TAB_RECENT || atoi(game.year.c_str()) >= 2024) &&
                lower(game.title).find(lower(query)) != std::string::npos) {
                matches.push_back(static_cast<int>(i));
            }
        }
        // "Recently added" interleaves the sources: each keeps its own order.
        std::map<int, int> ranks;
        std::map<int, int> sourceRanks;
        for (int index : matches) {
            ranks[index] = sourceRanks[sourceForURL(games[index].url)]++;
        }
        std::stable_sort(matches.begin(), matches.end(), [&](int a, int b) {
            const Game& x = games[a];
            const Game& y = games[b];
            if (sort == 1) return lower(x.title) < lower(y.title);
            if (sort == 2) return x.year > y.year;
            if (sort == 3) return sizeMB(x.size) < sizeMB(y.size);
            return ranks[a] != ranks[b] ? ranks[a] < ranks[b] : sourceForURL(x.url) < sourceForURL(y.url);
        });

        // A game takes the place of its first entry in that order.
        const bool keyed = gameKeys.size() == games.size();
        std::map<std::string, size_t> where;
        groups.clear();
        for (int index : matches) {
            const std::string key = keyed ? gameKeys[index] : "game:" + std::to_string(games[index].id);
            const auto found = where.find(key);
            if (found == where.end()) {
                where.emplace(key, groups.size());
                groups.push_back({ index });
            } else {
                groups[found->second].push_back(index);
            }
        }
        for (auto& group : groups) {
            if (group.size() > 1 && keyed) {
                std::stable_sort(group.begin(), group.end(), [&](int a, int b) {
                    return newerVersion(gameVersions[a], gameVersions[b]);
                });
            }
        }
        if (tab == TAB_FAVORITES) {
            groups.erase(std::remove_if(groups.begin(), groups.end(),
                                        [&](const std::vector<int>& group) { return !favoriteGroup(group); }),
                         groups.end());
        }

        visible.clear();
        selected = std::max(0, std::min(selected, int(groups.size()) - 1));
        for (size_t g = 0; g < groups.size(); ++g) {
            visible.push_back(groups[g][0]);
            for (int index : groups[g]) {
                if (games[index].id == keepId) {
                    selected = static_cast<int>(g);
                }
            }
        }
        dirty = true;
    }

    bool favoriteGroup(const std::vector<int>& group) const
    {
        for (int index : group) {
            if (favorites.count(games[index].id)) {
                return true;
            }
        }
        return false;
    }

    // The tile under the cursor (its newest entry).
    Game* current()
    {
        if (visible.empty() || selected >= int(visible.size()) || visible[selected] >= int(games.size())) {
            return nullptr;
        }
        return &games[visible[selected]];
    }

    // Every entry of the game under the cursor.
    const std::vector<int>* currentGroup() const
    {
        return selected >= 0 && selected < int(groups.size()) ? &groups[selected] : nullptr;
    }

    // The entry chosen on the game page.
    Game* variant()
    {
        const std::vector<int>* group = currentGroup();
        if (!group || group->empty()) {
            return current();
        }
        const int index = (*group)[std::max(0, std::min(variantSelected, int(group->size()) - 1))];
        return index < int(games.size()) ? &games[index] : nullptr;
    }

    int selectedJob() { return jobs.empty() ? 0 : jobs[std::min(jobSelected, int(jobs.size()) - 1)].id; }

    // Keeps the list cursors inside lists that shrank since the last frame.
    void clampSelections()
    {
        jobSelected = std::max(0, std::min(jobSelected, int(jobs.size()) - 1));
        installSelected = std::max(0, std::min(installSelected, int(packages.size()) - 1));
    }

    void beginDownload()
    {
        Game* chosen = variant();
        if (!chosen) {
            return;
        }
        downloadGame = *chosen;
        sourcePicker = true;
        sourceLoading = true;
        sourceSelected = 0;
        sources.clear();
        sourceError.clear();
        downloadRequest = true;
        dirty = true;
    }

    // A game is a favorite as a whole, whichever of its entries was saved.
    void favorite()
    {
        Game* game = current();
        const std::vector<int>* group = currentGroup();
        if (!game || !group) {
            return;
        }
        if (favoriteGroup(*group)) {
            for (int index : *group) {
                favorites.erase(games[index].id);
            }
        } else {
            favorites.insert(game->id);
        }
        std::string text;
        for (int id : favorites) {
            text += std::to_string(id) + "\n";
        }
        if (!writeFile(data + "/favorites.txt", text)) {
            notice = "Could not save favorites. Check storage permissions.";
        }
        filter();
    }

    void action(Action a)
    {
        dirty = true;
        notice.clear();

        // The dialogs of Settings: closing one goes back to Settings.
        if (downloadLocationPicker) {
            if (a == BACK) {
                downloadLocationPicker = false;
                return;
            }
            if (a == UP) downloadLocationSelected = std::max(0, downloadLocationSelected - 1);
            if (a == DOWN) downloadLocationSelected = std::min(8, downloadLocationSelected + 1);
            if (a == CONFIRM) {
                requestedDownloadBase = downloadLocationPath(downloadLocationSelected);
                downloadLocationRequest = true;
                downloadLocationPicker = false;
            }
            return;
        }
        if (sourceFilterPicker) {
            if (a == BACK) {
                sourceFilterPicker = false;
                return;
            }
            if (a == UP) sourceChoice = std::max(0, sourceChoice - 1);
            if (a == DOWN) sourceChoice = std::min(int(sourceSnapshot()->items.size()), sourceChoice + 1);
            if (a == CONFIRM) {
                sourceFilterPicker = false;
                if (sourceChoice != sourceFilter) {
                    sourceFilter = sourceChoice;
                    selected = 0;
                    saveSettings();
                    filter();
                }
            }
            return;
        }
        if (sortPicker) {
            if (a == BACK) {
                sortPicker = false;
                return;
            }
            if (a == UP) sortChoice = std::max(0, sortChoice - 1);
            if (a == DOWN) sortChoice = std::min(3, sortChoice + 1);
            if (a == CONFIRM) {
                sortPicker = false;
                if (sortChoice != sort) {
                    sort = sortChoice;
                    saveSettings();
                    filter();
                }
            }
            return;
        }
        if (debridPicker) {
            if (a == BACK) {
                debridPicker = false;
                return;
            }
            if (a == UP) debridChoice = std::max(0, debridChoice - 1);
            if (a == DOWN) debridChoice = std::min(kDebridCount - 1, debridChoice + 1);
            if (a == CONFIRM) {
                debridPicker = false;
                if (debridChoice != debrid) {
                    debrid = debridChoice;
                    saveSettings();
                }
                if (debrid > 0) {
                    settingsSelected = SETTING_DEBRID_KEY;
                    debridKeyRequest = debridKey().empty(); // a new service: its key next
                }
            }
            return;
        }
        if (settings) {
            if (a == BACK || a == SETTINGS) {
                settings = false;
                return;
            }
            if (a == UP) settingsSelected = std::max(0, settingsSelected - 1);
            if (a == DOWN) settingsSelected = std::min(SETTING_COUNT - 1, settingsSelected + 1);
            if (a == CONFIRM) {
                switch (settingsSelected) {
                case SETTING_SOURCE:
                    sourceFilterPicker = true;
                    sourceChoice = sourceFilter;
                    break;
                case SETTING_SORT:
                    sortPicker = true;
                    sortChoice = sort;
                    break;
                case SETTING_LOCATION:
                    syncDownloadLocation();
                    downloadLocationPicker = true;
                    break;
                case SETTING_DEBRID:
                    debridPicker = true;
                    debridChoice = debrid;
                    break;
                case SETTING_DEBRID_KEY:
                    if (debrid > 0) {
                        debridKeyRequest = true;
                    } else {
                        debridPicker = true; // a service first
                        debridChoice = 1;
                    }
                    break;
                case SETTING_ARTWORK:
                    officialArt = !officialArt;
                    artworkChanged = true;
                    saveSettings();
                    break;
                case SETTING_REFRESH:
                    refreshRequest = true;
                    break;
                default:
                    break;
                }
            }
            return;
        }
        if (installPicker) {
            if (a == BACK) {
                installPicker = false;
                return;
            }
            if (!packages.empty()) {
                if (a == UP) installSelected = std::max(0, installSelected - 1);
                if (a == DOWN) installSelected = std::min(int(packages.size()) - 1, installSelected + 1);
                if (a == CONFIRM && !installLoading) installSubmitRequest = true;
            }
            return;
        }
        if (sourcePicker) {
            if (a == BACK) {
                sourcePicker = false;
                return;
            }
            if (!sources.empty()) {
                if (a == UP) sourceSelected = std::max(0, sourceSelected - 1);
                if (a == DOWN) sourceSelected = std::min(int(sources.size()) - 1, sourceSelected + 1);
                if (a == CONFIRM) enqueueRequest = true;
            }
            return;
        }
        if (tab == TAB_DOWNLOADS) {
            const DownloadRow* job = jobs.empty() ? nullptr : &jobs[std::min(jobSelected, int(jobs.size()) - 1)];
            if (a == SEARCH) { // Triangle installs the packages of a finished download
                if (!job || job->state != DownloadState::Complete) {
                    return;
                }
                installJob = job->id;
                installPath = validDownloadDir(data, installJob, job->outputDir) ? job->outputDir
                                                                                  : defaultDownloadDir(data, installJob);
                installSelected = 0;
                packages.clear();
                installPicker = installLoading = installScanRequest = true;
                installError.clear();
                return;
            }
            if (a == FAVORITE) { // Square cancels the download and deletes its files
                if (!job) {
                    return;
                }
                cancelJob = job->id;
                cancelRequest = true;
                deletingJobs.insert(cancelJob);
                hideDeletedJobs();
                notice = "Download removed. Its files are being deleted.";
                return;
            }
            if (a == BACK) {
                tab = TAB_ALL;
                filter();
                return;
            }
            if (a == TAB_PREV || a == TAB_NEXT) {
                tab = a == TAB_NEXT ? TAB_ALL : TAB_RECENT;
                filter();
                return;
            }
            if (a == UP) jobSelected = std::max(0, jobSelected - 1);
            if (a == DOWN) jobSelected = std::min(std::max(0, int(jobs.size()) - 1), jobSelected + 1);
            return;
        }
        if (a == SETTINGS) {
            if (!detail && !keyboard) {
                settings = true;
                settingsSelected = 0;
            }
            return;
        }
        if (a == FAVORITE && !keyboard) {
            favorite();
            return;
        }
        if (a == BACK) {
            if (keyboard) {
                keyboard = false;
            } else if (detail) {
                detail = false;
            } else {
                query.clear();
                tab = TAB_ALL;
                filter();
            }
            return;
        }
        if (keyboard) { // on-screen fallback when the system keyboard is unavailable
            if (a == LEFT) key = (key + 39) % 40;
            if (a == RIGHT) key = (key + 1) % 40;
            if (a == UP) key = (key + 30) % 40;
            if (a == DOWN) key = (key + 10) % 40;
            if (a == CONFIRM) {
                const std::string keys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 <OK";
                if (key < 36 && query.size() < 64) query += keys[key];
                if (key == 36 && query.size() < 64) query += ' ';
                if (key == 37 && !query.empty()) query.pop_back();
                if (key == 38) query.clear();
                if (key == 39) keyboard = false;
                selected = 0;
                filter();
            }
            return;
        }
        if (a == SEARCH) {
            keyboard = true;
            detail = false;
            key = 0;
            return;
        }
        if (detail) { // the game page: up and down choose the entry to download
            const std::vector<int>* group = currentGroup();
            const int entries = group ? int(group->size()) : 1;
            if (a == UP) variantSelected = std::max(0, variantSelected - 1);
            if (a == DOWN) variantSelected = std::min(entries - 1, variantSelected + 1);
            if (a == CONFIRM) beginDownload();
            return;
        }
        if (a == TAB_PREV || a == TAB_NEXT) {
            tab = (tab + (a == TAB_NEXT ? 1 : 3)) % 4;
            selected = 0;
            filter();
            return;
        }
        if (a == SORT) {
            sort = (sort + 1) % 4;
            saveSettings();
            filter();
            return;
        }
        const int count = static_cast<int>(visible.size());
        if (!count) {
            return;
        }
        if (a == LEFT) selected = std::max(0, selected - 1);
        if (a == RIGHT) selected = std::min(count - 1, selected + 1);
        if (a == UP) selected = std::max(0, selected - kGridColumns);
        if (a == DOWN) selected = std::min(count - 1, selected + kGridColumns);
        if (a == PAGE_PREV) selected = std::max(0, selected - kGridPage);
        if (a == PAGE_NEXT) selected = std::min(count - 1, selected + kGridPage);
        if (a == CONFIRM) {
            detail = true;
            variantSelected = 0;
        }
    }
};

} // namespace ns
