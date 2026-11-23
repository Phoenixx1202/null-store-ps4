#include "app/Application.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <orbis/SystemService.h>

#include "ui/I18n.h"
#include <orbis/libkernel.h>

#include "core/Log.h"
#include "data_root.h"
#include "data_migration.h"
#include "service_start_ps4.h"
#include "install_log.h"
#include "network_ps4.h"
#include "sources.h"
#include "path_info.h"
#include "startup_ps4.h"
#include "ui/Theme.h"

namespace ns {

namespace {

const char* const kAssets = "/app0/assets";
const char* const kNoController = "Connect a controller to browse the catalog.";
constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr uint64_t kFrameMicros = 1000000 / 60;

// Makes sure `base` can hold downloads: creates its folders and writes a probe
// file. `why` receives the result either way.
bool checkDownloadLocation(const std::string& data, const std::string& base, std::string& why)
{
    if (!validDownloadBase(data, base)) {
        why = "Invalid download location.";
        return false;
    }
    const auto mount = downloadVolume(data, base);
    if ((mount != data && !mountedUsbVolume(mount)) || !downloadDirectoryChain(base)) {
        why = "USB destination is unavailable or read-only (mkdir errno " + std::to_string(errno) + ").";
        return false;
    }
    const std::string downloads = base + "/downloads";
    if (mkdir(downloads.c_str(), 0777) && errno != EEXIST) {
        why = "Cannot create the downloads folder here (errno " + std::to_string(errno) + ").";
        return false;
    }
    struct stat info;
    if (portalPathInfo(downloads.c_str(), &info) || !S_ISDIR(info.st_mode)) {
        why = "The selected downloads path is not a readable directory.";
        return false;
    }

    const std::string probe = downloads + "/.nullstore-write-test-" + std::to_string(getpid());
    const int fd = open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        why = "Cannot write to this location (errno " + std::to_string(errno) + "). Choose another drive.";
        return false;
    }
    const char mark[] = "NULL_store";
    bool wrote = write(fd, mark, sizeof(mark) - 1) == static_cast<ssize_t>(sizeof(mark) - 1);
    int saved = errno;
    if (close(fd) && wrote) {
        wrote = false;
        saved = errno;
    }
    unlink(probe.c_str());
    if (!wrote) {
        why = "Cannot write to this location (errno " + std::to_string(saved) + "). Choose another drive.";
        return false;
    }

    struct statvfs volume;
    if (statvfs(base.c_str(), &volume) == 0) {
        const uint64_t unit = volume.f_frsize ? volume.f_frsize : volume.f_bsize;
        why = "Writable. " + bytesText(static_cast<uint64_t>(volume.f_bavail) * unit) + " free.";
    } else {
        why = "Writable. Free-space details unavailable (errno " + std::to_string(errno) + ").";
    }
    return true;
}

} // namespace

namespace {

// UTF-8 to the UTF-16 the console's keyboard takes.
std::u16string utf16(const std::string& text)
{
    std::u16string out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t code = c;
        size_t length = 1;
        if (c >= 0xF0 && i + 3 < text.size()) {
            code = ((c & 0x07) << 18) | ((text[i + 1] & 0x3F) << 12) | ((text[i + 2] & 0x3F) << 6) | (text[i + 3] & 0x3F);
            length = 4;
        } else if (c >= 0xE0 && i + 2 < text.size()) {
            code = ((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F);
            length = 3;
        } else if (c >= 0xC0 && i + 1 < text.size()) {
            code = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);
            length = 2;
        }
        if (code >= 0x10000) {
            code -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (code >> 10));
            out += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        } else {
            out += static_cast<char16_t>(code);
        }
        i += length;
    }
    return out;
}

} // namespace

Application::Application() = default;

Application::~Application()
{
    // The view and the covers hold textures: release them before the renderer.
    view_.reset();
    art_.reset();
    covers_.reset();
    renderer_.shutdown();
}

bool Application::init()
{
    migrateStoreData(NULL_STORE_LEGACY_DATA_ROOT, NULL_STORE_DATA_ROOT);
    data_ = nullStoreDataRoot();
    mkdir(data_.c_str(), 0777);
    if (!loadStartupModules()) {
        return false;
    }
    mkdir((data_ + "/game-sizes").c_str(), 0777);
    mkdir((data_ + "/covers").c_str(), 0777);
    log::setDirectory(data_.c_str());
    NS_LOG_INFO("NULL_store starting (data root %s)", data_.c_str());
    // Only now: libSceSystemService is one of the modules loaded above.
    int32_t language = 1; // English
    const int32_t languageResult = sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG, &language);
    if (languageResult == 0) {
        setLanguage(languageFromConsole(language));
    }
    NS_LOG_INFO("Console language %d (result 0x%08X)", static_cast<int>(language), static_cast<unsigned>(languageResult));

    const auto config = data_ + "/sources.json";
    std::string error;
    if (access(config.c_str(), F_OK) != 0 && errno == ENOENT) {
        for (int slot = 0; slot < 8; ++slot) {
            const auto input = readFile("/mnt/usb" + std::to_string(slot) + "/NULL_store/sources.json");
            SourceTable table;
            if (parseSourcesJSON(input, table, error) && saveSources(config, table, error)) break;
        }
    }
    if (!loadSourcesIntoTable(config, error)) NS_LOG_ERROR("Source configuration: %s", error.c_str());
    ui_ = std::make_unique<StoreState>(kAssets, data_);
    downloads_ = std::make_unique<DownloadController>(data_, portalNetworkFetch);


    if (!renderer_.init(createRenderBackend(), kWidth, kHeight, std::string(kAssets) + "/font.ttf")) {
        installLog("Startup display initialization failed");
        return false;
    }
    installLog("Startup display ready");
    covers_ = std::make_unique<CoverCache>(renderer_, kAssets, data_);
    covers_->setOfficialArt(ui_->officialArt);
    covers_->enableDownloads(portalNetworkFetch);
    art_ = std::make_unique<GameArt>(renderer_, data_);
    art_->enableDownloads(portalNetworkFetch);
    view_ = std::make_unique<StoreView>(renderer_, *covers_, *art_, kAssets);

    pad_.init();

    installer_ = std::make_unique<PackageInstaller>(data_, ps4InstallBackend());


    setup_ = std::make_unique<SourcesServer>(data_, portalNetworkFetch);
    if (setup_->start(8090, error) || setup_->start(0, error)) {
        const auto address = localLanAddress();
        if (!address.empty()) ui_->setSetupURL(setup_->urlFor(address));
    } else {
        ui_->notice = error;
    }

    refresh_ = std::make_unique<CatalogRefresh>(data_);
    displayedSources_ = sourceSnapshot();
    refresh_->start();
    return true;
}

int Application::run()
{
    if (!init()) {
        return 1;
    }
    // The application paces its own frames (see GLContextPs4.cpp).
    uint64_t deadline = sceKernelGetProcessTime() + kFrameMicros;
    for (;;) {
        frame();
        const uint64_t now = sceKernelGetProcessTime();
        if (now < deadline) {
            sceKernelUsleep(static_cast<unsigned int>(deadline - now));
            deadline += kFrameMicros;
        } else {
            deadline = now + kFrameMicros; // a slow frame: do not try to catch up
        }
    }
    return 0;
}

void Application::frame()
{
    if (textInput_.isOpen()) {
        pad_.ignoreUntilReleased();
    }
    const uint64_t started = sceKernelGetProcessTime();
    pad_.update(static_cast<double>(started) / 1000000.0);
    handleInput();
    handleKeyboard();
    handleRequests();
    const uint64_t requestsDone = sceKernelGetProcessTime();
    syncWorkers();
    const uint64_t renderStart = sceKernelGetProcessTime();
    render();
    const uint64_t finished = sceKernelGetProcessTime();
    if (finished - started >= 1000000 && finished >= nextStallLog_) {
        nextStallLog_ = finished + 5000000;
        NS_LOG_WARN("Slow frame: input/requests=%llu ms updates=%llu ms render=%llu ms",
                    static_cast<unsigned long long>((requestsDone - started) / 1000),
                    static_cast<unsigned long long>((renderStart - requestsDone) / 1000),
                    static_cast<unsigned long long>((finished - renderStart) / 1000));
    }
}

void Application::handleInput()
{
    using namespace input;
    StoreState& ui = *ui_;

    if (!pad_.connected()) {
        if (ui.notice.empty()) {
            ui.notice = kNoController;
            ui.noticeStays = true;
        }
        return;
    }
    if (ui.notice == kNoController) {
        ui.notice.clear();
        ui.noticeStays = false;
    }

    struct Binding {
        Button button;
        Action action;
    };
    // Square saves a game, or cancels a download; OPTIONS opens Settings from
    // the catalogue and does nothing on the download queue.
    const Binding bindings[] = {
        { BtnUp, UP },          { BtnDown, DOWN },       { BtnLeft, LEFT },       { BtnRight, RIGHT },
        { BtnCircle, BACK },    { BtnTriangle, SEARCH }, { BtnSquare, FAVORITE }, { BtnL1, TAB_PREV },
        { BtnL3, MANAGE }, { BtnR1, TAB_NEXT },    { BtnTouchpad, SORT },   { BtnL2, PAGE_PREV },    { BtnR2, PAGE_NEXT },
    };
    for (const Binding& binding : bindings) {
        if (pad_.pressed(binding.button)) {
            ui.action(binding.action);
        }
    }

    // Cross pauses or resumes the download under the cursor, and confirms
    // everywhere else.
    if (pad_.pressed(BtnCross)) {
        if (ui.tab == TAB_DOWNLOADS && !ui.dialogOpen()) {
            downloads_->toggle(ui.selectedJob());
        } else {
            ui.action(CONFIRM);
        }
    }
    if (pad_.pressed(BtnOptions) && (ui.tab != TAB_DOWNLOADS || ui.settings)) {
        ui.action(SETTINGS);
    }
}

// Search uses the console's own keyboard; the on-screen grid of the store is
// the fallback when that dialog cannot open.
void Application::handleKeyboard()
{
    StoreState& ui = *ui_;
    if (!ui.keyboard) {
        keyboardFallback_ = false;
    }
    if (ui.debridKeyRequest && !textInput_.isOpen()) {
        ui.debridKeyRequest = false;
        std::string error;
        keyboardTitle_ = utf16(tr("Debrid API key"));
        keyboardHint_ = utf16(tr("Paste or type the key"));
        if (textInput_.begin(pad_.userId(), ui.debridKey(), error, keyboardTitle_.c_str(), keyboardHint_.c_str(),
                             false)) {
            keyboardForKey_ = true;
        } else {
            ui.notice = error;
        }
    }
    if (ui.keyboard && !keyboardFallback_ && !textInput_.isOpen()) {
        std::string error;
        ui.keyboard = false;
        keyboardForKey_ = false;
        keyboardTitle_ = utf16(tr("Search games"));
        keyboardHint_ = utf16(tr("Game title"));
        if (!textInput_.begin(pad_.userId(), ui.query, error, keyboardTitle_.c_str(), keyboardHint_.c_str())) {
            ui.keyboard = true;
            keyboardFallback_ = true;
            ui.notice = error;
        }
    }
    if (textInput_.isOpen()) {
        std::string text, error;
        const NativeTextInput::Result result = textInput_.poll(text, error);
        if (result != NativeTextInput::Pending) {
            pad_.ignoreUntilReleased();
            if (keyboardForKey_) {
                keyboardForKey_ = false;
                if (result == NativeTextInput::Accepted) {
                    ui.setDebridKey(text);
                } else if (result == NativeTextInput::Failed) {
                    ui.notice = error;
                }
            } else if (result == NativeTextInput::Accepted) {
                ui.query = text;
                ui.selected = 0;
                ui.filter();
            } else if (result == NativeTextInput::Failed) {
                ui.notice = error;
            }
        }
    }
}

// Carries out what the state asked for during this frame's actions.
void Application::handleRequests()
{
    StoreState& ui = *ui_;

    if (!downloadsReady_ && ((splashHidden_ && !resumeAttempted_) || ui.downloadRequest || ui.enqueueRequest || ui.startWorker)) {
        resumeAttempted_ = true;
        downloadsReady_ = downloads_->start();
    }
    if (ui.startWorker) { ui.startWorker = false; downloads_->setup(); }
    if (ui.uninstallWorker) {
        ui.uninstallWorker = false;
        if (!installerReady_) installerReady_ = installer_->start();
        if (!installerReady_ || !downloadsReady_) ui.notice = "Download or installer thread unavailable. Reopen the store.";
        else if (installer_->canRemoveWorker(ui.notice) && downloads_->stopForRemoval(ui.notice)) {
            reinstallAfterRemoval_ = ui.reinstallWorker;
            ui.workerRemovalBusy = true;
            workerStopDeadline_ = sceKernelGetProcessTime() + 30000000;
            ui.notice = "Stopping the worker and returning downloads to the store…";
        }
    }
    if (ui.stopWorker) { ui.stopWorker = false; downloads_->stopService(); }
    if (ui.probeTorrent) {
        ui.probeTorrent = false;
        Game game;
        game.id = 1999000001;
        game.title = "Big Buck Bunny connection test";
        game.url = "https://webtorrent.io/torrents/big-buck-bunny.torrent";
        downloads_->enqueue(game, {"One verified piece", game.url}, ui.notice);
    }
    if (ui.cancelRequest) {
        ui.cancelRequest = false;
        downloads_->remove(ui.cancelJob, ui.removeFiles); // stops the transfer, then deletes its files
    }

    if (ui.refreshRequest) {
        ui.refreshRequest = false;
        refresh_->start(ui.sourceFilter, true);
        ui.notice = "Refreshing the catalogue.";
    }

    if (ui.artworkChanged) {
        ui.artworkChanged = false;
        covers_->setOfficialArt(ui.officialArt);
    }

    if (ui.downloadLocationRequest) {
        ui.downloadLocationRequest = false;
        std::string why;
        if (checkDownloadLocation(data_, ui.requestedDownloadBase, why)) {
            if (writeFile(data_ + "/download-location.txt", ui.requestedDownloadBase + "\n")) {
                ui.downloadBase = ui.requestedDownloadBase;
                ui.notice = "Download location saved. " + why;
            } else {
                ui.notice = "Could not save download location; check internal storage.";
            }
        } else {
            ui.notice = why;
        }

    }

    if (ui.installScanRequest) {
        ui.installScanRequest = false;
        if (!installerReady_) installerReady_ = installer_->start();
        if (installerReady_) {
            installer_->scan(ui.installJob, ui.installPath);
        } else {
            ui.installLoading = false;
            ui.installError = "Installer worker could not start. Restart NULL_store.";
        }
    }
    if (ui.installSubmitRequest) {
        ui.installSubmitRequest = false;
        if (ui.installSelected >= 0 && ui.installSelected < int(ui.packages.size())) {
            if (ui.installJob == 0 && portalWorkerRunning())
                ui.installError = "The worker is running. Finish its downloads and stop it before updating.";
            else
                installer_->submit(ui.packages[ui.installSelected].path);
        }
    }

    if (ui.downloadRequest) {
        ui.downloadRequest = false;
        if (downloadsReady_) {
            downloads_->discover(ui.downloadGame);
        } else {
            ui.sourceLoading = false;
            ui.sourceError = "Download service could not start. Restart NULL_store and retry.";
        }
    }
}

// Brings what the workers produced into the state the view draws.
void Application::syncWorkers()
{
    StoreState& ui = *ui_;

    if (ui.sourceFilterPicker && ui.setupURL.empty() && setup_->port() > 0) {
        const auto now = sceKernelGetProcessTime();
        if (now >= nextSetupAddressCheck_) {
            nextSetupAddressCheck_ = now + 5000000;
            const auto address = localLanAddress();
            if (!address.empty()) ui.setSetupURL(setup_->urlFor(address));
        }
    }
    const auto table = sourceSnapshot();
    if (table != displayedSources_) {
        const auto* previous = displayedSources_->at(ui.sourceFilter);
        ui.sourceFilter = previous ? table->indexForId(previous->id) : 0;
        ui.sourceChoice = ui.sourceFilter;
        for (const auto& item : table->items) {
            if (item.kind != SourceKind::Custom) continue;
            const auto* old = displayedSources_->at(displayedSources_->indexForId(item.id));
            if (!old || sourceToJSON(*old) != sourceToJSON(item)) {
                const auto path = providerCachePath(data_, table->indexForId(item.id), *table);
                unlink((path + ".time").c_str());
            }
        }
        displayedSources_ = table;
        ui.games = mergedCatalog(data_ + "/catalog.tsv", *sourceSnapshot(), std::string(kAssets) + "/catalog.tsv");
        ui.shareTitleIds();
        ui.filter();
        sizeAttempts_.clear();
        detailAttempts_.clear();
        refresh_->start(0, false, true);
    }
    if (ui.workerMenu) {
        if (!installerReady_) installerReady_ = installer_->start();
        const uint64_t now = sceKernelGetProcessTime();
        if (installerReady_ && now >= nextWorkerCheck_) {
            nextWorkerCheck_ = now + 2000000;
            installer_->checkWorker();
        }
        auto installed = installer_->workerInstallState();
        bool running = false, verified = false;
        downloads_->workerStatus(running, verified);
        if (verified && (installed == WorkerInstallState::Installed || running)) installed = WorkerInstallState::Current;
        if (ui.workerInstall != installed || ui.workerRunning != running ||
            ui.workerRemovalBusy != workerMenuWasBusy_) {
            ui.updateWorkerOptions(installed, running);
            workerMenuWasBusy_ = ui.workerRemovalBusy;
        }
    } else {
        nextWorkerCheck_ = 0;
    }
    const std::string connection = downloads_->connectionStatus();
    if (connection != ui.serviceStatus) {
        ui.serviceStatus = connection;
    }
    downloads_->takeNotice(ui.notice);
    if (ui.workerRemovalBusy) {
        std::string error;
        bool running = false, verified = false;
        downloads_->workerStatus(running, verified);
        if (workerStopDeadline_ && !running && downloads_->ownsQueue()) {
            workerStopDeadline_ = 0;
            ui.notice = "Uninstalling download worker…";
            installer_->removeWorker();
        } else if (workerStopDeadline_ && sceKernelGetProcessTime() >= workerStopDeadline_) {
            workerStopDeadline_ = 0;
            ui.workerRemovalBusy = false;
            ui.notice = "Could not return downloads to the store. Worker removal cancelled.";
        } else if (installer_->takeWorkerRemoval(error)) {
            ui.workerRemovalBusy = false;
            if (error.empty() && reinstallAfterRemoval_) ui.openWorkerInstaller();
            ui.notice = error.empty() ? "Download worker removed. Downloads continue while the store is open." : error;
        }
    }
    if (installerReady_) installer_->snapshot(installVersion_, ui.packages, ui.installLoading, ui.installError);

    std::vector<TorrentLink> found;
    std::string sourceTitle, sourceError;
    if (downloads_->takeSources(found, sourceTitle, sourceError) && ui.sourcePicker) {
        ui.sources = found;
        ui.sourceLoading = false;
        ui.sourceError = sourceError;
        if (found.size() == 1 && sourceError.empty()) {
            ui.enqueueRequest = true; // nothing to choose from
        }
    }
    if (ui.enqueueRequest) {
        ui.enqueueRequest = false;
        if (ui.sourceSelected >= 0 && ui.sourceSelected < int(ui.sources.size())) {
            downloads_->enqueue(ui.downloadGame, ui.sources[ui.sourceSelected], ui.notice);
            ui.sourcePicker = false;
        }
    }
    if (downloads_->snapshot(downloadVersion_, ui.jobs)) {
        ui.hideDeletedJobs();
    }

    // Title IDs the cover cache read from the sources' game pages, passed on
    // to the same games in the other sources.
    const bool learnt = covers_->pollTitleIds(ui.titleIds);
    covers_->pollNames(ui.officialNames);

    refresh_->pollStatus(ui.status);
    const Game* selected = ui.current();
    const int selectedId = selected ? selected->id : -1;
    const Game* variant = ui.detail ? ui.variant() : nullptr;
    const int variantId = variant ? variant->id : -1;
    const bool refreshed = refresh_->pollCatalogue(ui.games);
    // Title IDs learnt in the background regroup the catalogue the next time
    // the list is rebuilt (search, sort, tab...): applying them now would move
    // tiles, and the cursor, while the user is browsing.
    if (learnt || refreshed) {
        ui.shareTitleIds();
    }
    if (refreshed) {
        ui.filter(false, variantId > 0 ? variantId : selectedId);
        if (ui.detail) {
            const auto* group = ui.currentGroup();
            ui.variantSelected = 0;
            if (group) {
                for (size_t i = 0; i < group->size(); ++i)
                    if (ui.games[(*group)[i]].id == variantId) ui.variantSelected = static_cast<int>(i);
            }
        }
    }
    updateDetails(refreshed);
}

void Application::updateDetails(bool refreshed)
{
    auto& ui = *ui_;
    std::string url;
    ListingDetails facts;
    if (lookup_.take(url, facts) && !facts.empty()) {
        auto& previous = ui.listingDetails[url];
        if (facts.size.empty()) facts.size = previous.size;
        previous = std::move(facts);
        refreshed = true;
    }
    if (refreshed) {
        for (auto& game : ui.games) {
            auto it = ui.listingDetails.find(game.url);
            if (it != ui.listingDetails.end() && !it->second.size.empty()) game.size = it->second.size;
        }
    }
    const auto now = sceKernelGetProcessTime();
    if (lookup_.busy() || now < nextLookup_ || ui.tab == TAB_DOWNLOADS || ui.dialogOpen()) return;
    nextLookup_ = now + 500000;
    if (ui.detail) {
        const auto* game = ui.variant();
        if (game && !detailAttempts_.count(game->url) && lookup_.start(*game, true, data_ + "/game-sizes"))
            detailAttempts_.insert(game->url);
        return;
    }
    const int start = ui.selected / StoreState::kGridPage * StoreState::kGridPage;
    for (int i = start; i < std::min(start + StoreState::kGridPage, int(ui.visible.size())); ++i) {
        const Game& game = ui.games[ui.visible[i]];
        if (!sizeMB(game.size) && !sizeAttempts_.count(game.url)) {
            if (lookup_.start(game, false, data_ + "/game-sizes")) sizeAttempts_.insert(game.url);
            break;
        }
    }
}

void Application::render()
{
    // Animations advance by the time that really passed, capped so a long
    // stall does not make them jump.
    const uint64_t now = sceKernelGetProcessTime();
    const float dt = lastFrameMicros_ ? std::min(0.1f, static_cast<float>(now - lastFrameMicros_) / 1000000.0f)
                                      : 1.0f / 60.0f;
    lastFrameMicros_ = now;

    covers_->pump(static_cast<double>(now) / 1000000.0);
    art_->pump(static_cast<double>(now) / 1000000.0);
    renderer_.beginFrame(theme::kBackground);
    view_->draw(*ui_, dt);
    renderer_.endFrame();

    if (!splashHidden_) {
        // The system keeps its splash screen up until the application says
        // its first frame is on screen.
        splashHidden_ = true;
        const int32_t result = sceSystemServiceHideSplashScreen();
        NS_LOG_INFO("First frame presented (hide splash: 0x%08X)", static_cast<unsigned>(result));
    }
}

} // namespace ns
