#pragma once
#include "sources.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "download_model.h"
#include "download_paths.h"
#include "installer.h"
#include "game_details.h"
#ifdef _WIN32
#include <filesystem>
#else
#include <dirent.h>
#endif
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
    MANAGE,
    SETTINGS  // OPTIONS on the catalogue
};

enum class WorkerAction { Start, Stop, Install, Replace, Reinstall, Uninstall, Test };

inline const char* workerActionLabel(WorkerAction action) {
    switch (action) {
        case WorkerAction::Start: return "Start worker";
        case WorkerAction::Stop: return "Stop worker";
        case WorkerAction::Install:
        case WorkerAction::Replace: return "Install included worker";
        case WorkerAction::Reinstall: return "Reinstall worker";
        case WorkerAction::Uninstall: return "Uninstall worker";
        case WorkerAction::Test: return "Test torrent connection";
    }
    return "";
}

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
    std::map<int, std::string> titleIds, displayTitleIds;
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
    std::map<std::string, ListingDetails> listingDetails;
    std::string searchDraft, browseFolder;
    std::vector<std::pair<std::string,std::string>> folderChoices;
    bool separateListings = false;
    bool confirmWorkerRemoval = false, uninstallWorker = false, workerRemovalBusy = false;
    bool workerMenu = false, removeMenu = false, removeFiles = false;
    bool startWorker = false, stopWorker = false, probeTorrent = false;
    bool workerRunning = false, workerStarting = false, reinstallWorker = false;
    WorkerAction confirmedWorkerAction = WorkerAction::Uninstall;
    WorkerInstallState workerInstall = WorkerInstallState::Unchecked;
    std::vector<WorkerAction> workerActions{WorkerAction::Test};

    void updateWorkerOptions(WorkerInstallState installed, bool running) {
        workerInstall = installed;
        workerRunning = running;
        const bool hadOptions = workerActions.size() > 1;
        const int previous = std::clamp(menuChoice, 0, static_cast<int>(workerActions.size()) - 1);
        const WorkerAction selectedAction = workerActions[previous];
        workerActions.clear();
        if (!workerRemovalBusy) {
            if (running) workerActions.push_back(WorkerAction::Stop);
            if (installed == WorkerInstallState::Current) {
                if (!running) workerActions.push_back(WorkerAction::Start);
                workerActions.push_back(WorkerAction::Reinstall);
                workerActions.push_back(WorkerAction::Uninstall);
            } else if (installed == WorkerInstallState::Installed || installed == WorkerInstallState::Different) {
                workerActions.push_back(WorkerAction::Replace);
                workerActions.push_back(WorkerAction::Uninstall);
            } else if (!running && (installed == WorkerInstallState::Missing || installed == WorkerInstallState::Unknown)) {
                workerActions.push_back(WorkerAction::Install);
            }
        }
        workerActions.push_back(WorkerAction::Test);
        if (workerMenu) {
            const auto found = std::find(workerActions.begin(), workerActions.end(), selectedAction);
            menuChoice = !hadOptions || found == workerActions.end() ? 0 : static_cast<int>(found - workerActions.begin());
        }
    }

    void openWorkerInstaller() {
        installJob = installSelected = 0;
        installPath.clear();
        packages.clear();
        installError.clear();
        installPicker = installLoading = installScanRequest = true;
    }
    int menuChoice = 0;
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
        downloadBase = migratedDownloadPath(data, downloadBase);
        if (!validDownloadBase(data, downloadBase)) {
            downloadBase = data;
        }
        separateListings = readFile(data + "/catalog-display.txt") == "separate";
        loadSettings();
        games = mergedCatalog(data + "/catalog.tsv", *sourceSnapshot(), base + "/catalog.tsv");
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
        std::vector<Game> candidates(games.size());
        std::vector<std::string> keys(games.size()), versions(games.size());
        displayTitleIds.clear();
        for (size_t i = 0; i < games.size(); ++i) {
            texts[i] = describe(games[i]);
            names[i] = titleKey(texts[i].title);
            if (texts[i].titleId.empty()) {
                const auto learnt = titleIds.find(games[i].id);
                if (learnt != titleIds.end()) texts[i].titleId = learnt->second;
            }
            candidates[i].title = names[i];
            candidates[i].year = games[i].year;
            if (!texts[i].titleId.empty()) candidates[i].title += " (" + texts[i].titleId + ")";
            versions[i] = texts[i].version;
        }
        for (const auto& group : groupCatalogListings(candidates)) {
            std::string product;
            for (int index : group)
                if (!texts[index].titleId.empty()) product = texts[index].titleId;
            for (int index : group) {
                if (!product.empty()) displayTitleIds[games[index].id] = product;
                keys[index] = !product.empty() ? "id:" + product :
                              group.size() > 1 ? "name:" + names[index] :
                                                 "game:" + std::to_string(games[index].id);
            }
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
        const auto table = sourceSnapshot();
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
                if (name == "source") {
                    // A custom-only installation recorded the index within sources.json.
                    SourceTable old;
                    std::string error;
                    if (value > 0 && loadSources(data + "/sources.json", old, error) && old.at(value))
                        sourceFilter = table->indexForURL(old.at(value)->base);
                    else
                        sourceFilter = table->at(value) ? value : 0;
                }
                if (name == "source-id") sourceFilter = table->indexForId(line.substr(equals + 1));
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
        const auto table = sourceSnapshot();
        const auto* source = table->at(sourceFilter);
        const std::string text = "source-id=" + (source ? source->id : std::string()) + "\nsort=" + std::to_string(sort) +
                                 "\nofficial-art=" + (officialArt ? "1" : "0") + "\ndebrid=" + std::to_string(debrid) +
                                 "\n";
        if (!writeFile(data + "/settings.txt", text)) {
            notice = "Could not save settings. Check storage permissions.";
        }
    }

    // Any dialog is open: the controller talks to it, not to the screen below.
    bool dialogOpen() const
    {
        return workerStarting || confirmWorkerRemoval || removeMenu || workerMenu || settings || sourceFilterPicker || sortPicker || debridPicker || downloadLocationPicker || installPicker ||
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

    std::string downloadLocationName() const {
        return downloadBase==data?"Internal storage":englishText(downloadBase,"Selected folder");
    }
    void browseDownloadFolder(const std::string& folder){
        browseFolder=folder.empty()?"/":folder;
        downloadLocationSelected=0;
        folderChoices.clear();
        if(browseFolder=="/"){
            #ifdef PORTAL_HOST
            folderChoices.push_back({"Internal storage",data});
            #else
            folderChoices.push_back({"/data  ·  Internal storage","/data"});
            #endif
#ifndef PORTAL_HOST
            for(int port=0;port<8;++port){
                auto path="/mnt/usb"+std::to_string(port);
                if(mountedUsbVolume(path))
                    folderChoices.push_back({"USB "+std::to_string(port),path});
            }
#endif
            return;
        }
        const bool selectable=validDownloadBase(data,folder);
        if(selectable)folderChoices.push_back({"Use this folder",folder});
        if(!downloadDirectoryChain(folder))return;
#ifdef _WIN32
        std::error_code ec;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            const auto name = it->path().filename().string();
#else
        DIR* dir=opendir(folder.c_str());
        if(!dir)return;
        while(auto* entry=readdir(dir)){
            const std::string name=entry->d_name;
#endif
            if(name.empty()||name[0]=='.'||name=="downloads")continue;
            const auto path=folder+"/"+name;
            struct stat info={};
            if(validDownloadBase(data,path)&&portalPathInfo(path.c_str(),&info)==0&&S_ISDIR(info.st_mode))
                folderChoices.push_back({englishText(name,"Folder with a non-English name"),path});
            if(folderChoices.size()>=1024)break;
        }
#ifndef _WIN32
        closedir(dir);
#endif
        std::sort(folderChoices.begin()+(selectable?1:0),folderChoices.end(),[](const std::pair<std::string,std::string>& a,
                                                                const std::pair<std::string,std::string>& b){
            return lower(a.first)<lower(b.first);
        });
    }
    // `follow`: the game under the cursor stays under it, wherever its tile
    // moves (when the list is regrouped in the background, not when the user
    // changes the sort, the search or the source).
    void filter(bool follow = false, int keepId = -1)
    {
        const auto table = sourceSnapshot();
        const Game* before = follow ? current() : nullptr;
        if (before) keepId = before->id;

        std::vector<int> matches;
        std::vector<int> ranks(games.size()), sources(games.size());
        std::vector<int> sourceCounts(table->items.size() + 1);
        std::vector<std::string> names(sort == 1 ? games.size() : 0);
        std::vector<double> sizes(sort == 3 ? games.size() : 0);
        const std::string search = lower(query);
        for (size_t i = 0; i < games.size(); ++i) {
            const Game& game = games[i];
            const int source = table->indexForURL(game.url);
            if ((sourceFilter && sourceFilter != source) || (tab == TAB_RECENT && atoi(game.year.c_str()) < 2024))
                continue;
            if (sort == 1 || !search.empty()) {
                auto name = lower(game.title);
                if (!search.empty() && name.find(search) == std::string::npos) continue;
                if (sort == 1) names[i] = std::move(name);
            }
            if (sort == 3) sizes[i] = sizeMB(game.size);
            sources[i] = source;
            ranks[i] = sourceCounts[source]++;
            matches.push_back(static_cast<int>(i));
        }
        std::stable_sort(matches.begin(), matches.end(), [&](int a, int b) {
            if (sort == 1) return names[a] < names[b];
            if (sort == 2) return games[a].year > games[b].year;
            if (sort == 3) return sizes[a] < sizes[b];
            return ranks[a] != ranks[b] ? ranks[a] < ranks[b] : sources[a] < sources[b];
        });

        // A game takes the place of its first entry in that order.
        const bool keyed = gameKeys.size() == games.size();
        std::map<std::string, size_t> where;
        groups.clear();
        for (int index : matches) {
            const std::string key = keyed && !separateListings ? gameKeys[index] : "game:" + std::to_string(games[index].id);
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
        if (workerStarting || startWorker) return;
        dirty = true;
        notice.clear();

        // The dialogs of Settings: closing one goes back to Settings.
        if (confirmWorkerRemoval) {
            if (a == UP || a == DOWN) menuChoice = 1 - menuChoice;
            if (a == BACK || a == CONFIRM) {
                uninstallWorker = a == CONFIRM && menuChoice == 1;
                reinstallWorker = uninstallWorker && confirmedWorkerAction != WorkerAction::Uninstall;
                confirmWorkerRemoval = false;
                workerMenu = !uninstallWorker;
                const auto found = std::find(workerActions.begin(), workerActions.end(), confirmedWorkerAction);
                menuChoice = found == workerActions.end() ? 0 : static_cast<int>(found - workerActions.begin());
            }
            return;
        }
        if (workerMenu || removeMenu) {
            const int last = removeMenu ? 2 : static_cast<int>(workerActions.size()) - 1;
            if (a == BACK) { workerMenu = removeMenu = false; return; }
            if (a == UP) menuChoice = std::max(0, menuChoice - 1);
            if (a == DOWN) menuChoice = std::min(last, menuChoice + 1);
            if (a == CONFIRM) {
                if (removeMenu && menuChoice < 2) {
                    cancelRequest = true;
                    removeFiles = menuChoice == 1;
                    deletingJobs.insert(cancelJob);
                    hideDeletedJobs();
                } else if (workerMenu) {
                    switch (workerActions[menuChoice]) {
                        case WorkerAction::Start: startWorker = true; break;
                        case WorkerAction::Stop: stopWorker = true; break;
                        case WorkerAction::Install: openWorkerInstaller(); break;
                        case WorkerAction::Test: probeTorrent = true; break;
                        case WorkerAction::Replace:
                        case WorkerAction::Reinstall:
                        case WorkerAction::Uninstall:
                            confirmedWorkerAction = workerActions[menuChoice];
                            confirmWorkerRemoval = true;
                            menuChoice = 0;
                            break;
                    }
                }
                workerMenu = removeMenu = false;
            }
            return;
        }
        if(downloadLocationPicker){
            if(a==BACK){
                if(browseFolder=="/"){downloadLocationPicker=false;}
                else if(browseFolder=="/data"||(browseFolder==downloadVolume(data,browseFolder)&&browseFolder.compare(0,8,"/mnt/usb")==0))browseDownloadFolder("/");
                else browseDownloadFolder(browseFolder.substr(0,browseFolder.rfind('/')));
            }
            if(a==UP)downloadLocationSelected=std::max(0,downloadLocationSelected-1);
            if(a==DOWN)downloadLocationSelected=std::max(0,std::min(int(folderChoices.size())-1,downloadLocationSelected+1));
            if(a==CONFIRM&&!folderChoices.empty()){
                const auto path=folderChoices[downloadLocationSelected].second;
                if(validDownloadBase(data,browseFolder)&&downloadLocationSelected==0){
                    requestedDownloadBase=path;
                    downloadLocationRequest=true;downloadLocationPicker=false;
                }else browseDownloadFolder(path);
            }
            return;
        }
        if (sourceFilterPicker) {
            if (a == FAVORITE) {
                separateListings = !separateListings;
                writeFile(data + "/catalog-display.txt", separateListings ? "separate" : "grouped");
                filter();
                return;
            }
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
                    browseDownloadFolder("/");
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
            if (a == MANAGE) { workerMenu = true; menuChoice = 0; return; }
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
                removeMenu = true;
                menuChoice = 2;
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
                if (key < 36 && searchDraft.size() < 64) searchDraft += keys[key];
                if (key == 36 && searchDraft.size() < 64) searchDraft += ' ';
                if (key == 37 && !searchDraft.empty()) searchDraft.pop_back();
                if (key == 38) searchDraft.clear();
                if (key == 39) { keyboard = false; query = searchDraft; selected = 0; filter(); }
            }
            return;
        }
        if (a == SEARCH) {
            searchDraft = query;
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
        if (a == PAGE_PREV && selected >= kGridPage) selected -= kGridPage;
        if (a == PAGE_NEXT && selected / kGridPage < (count - 1) / kGridPage) selected = std::min(count - 1, selected + kGridPage);
        if (a == CONFIRM) {
            detail = true;
            variantSelected = 0;
        }
    }
};

} // namespace ns
