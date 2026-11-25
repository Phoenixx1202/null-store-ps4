// PC preview of the store's screens.
//
// Draws the real layout code (src/ui/StoreView.cpp) through the software render
// backend and writes PNG files, so a screen can be checked without a console:
//
//   build/preview-tool [scene ...|all]      -> build/preview/<scene>.png
//
// Run it from the repository root (it reads assets/). Covers come from
// assets/covers or build/preview-data/covers when those folders have any.
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "graphics/Renderer.h"
#include "ui/CoverCache.h"
#include "ui/GameArt.h"
#include "ui/StoreState.h"
#include "ui/I18n.h"
#include "ui/StoreView.h"
#include "ui/Theme.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

using namespace ns;

namespace {

const char* const kAssets = "assets";
const char* const kData = "build/preview-data";
const char* const kOutput = "build/preview";

DownloadRow sampleJob(int id, const char* title, DownloadState state, uint64_t done, uint64_t total)
{
    DownloadRow job;
    job.id = id;
    job.title = title;
    job.state = state;
    job.phase = stateName(state);
    job.done = done;
    job.total = total;
    job.rate = state == DownloadState::Active ? 2500000 : 0;
    job.peers = state == DownloadState::Active ? 14 : 0;
    return job;
}

void showQueue(StoreState& ui)
{
    ui.tab = TAB_DOWNLOADS;
    ui.serviceStatus = "Background service connected (preview)";
    ui.jobs.push_back(sampleJob(1, "Generated sample / sample.torrent", DownloadState::Active, 34000000, 100000000));
    ui.jobs.push_back(sampleJob(2, "Second sample / waiting", DownloadState::Queued, 0, 0));
    ui.jobs.push_back(sampleJob(3, "Finished sample", DownloadState::Complete, 700000000, 700000000));
    DownloadRow failed = sampleJob(4, "Unreachable sample", DownloadState::Failed, 1200000, 90000000);
    failed.error = "No peers answered. Retry later or try another source.";
    ui.jobs.push_back(failed);
}

struct Scene {
    const char* name;
    std::function<void(StoreState&)> setup;
    // Seconds to let pass, in 30 fps steps, after the scene has settled.
    float rest = 0.0f;
};

const std::vector<Scene>& scenes()
{
    static const std::vector<Scene> list = {
        { "catalogue", [](StoreState&) {} },
        { "catalogue-shine", [](StoreState&) {}, 0.43f },
        { "catalogue-scrolled", [](StoreState& ui) {
             // Further down the list, with a few favourites marked.
             for (int index : { 1, 4, 34, 37 }) {
                 if (index < int(ui.visible.size())) ui.favorites.insert(ui.games[ui.visible[index]].id);
             }
             ui.selected = std::min(37, std::max(0, int(ui.visible.size()) - 1));
         } },
        { "favorites-empty", [](StoreState& ui) {
             ui.tab = TAB_FAVORITES;
             ui.favorites.clear();
             ui.filter();
         } },
        { "details", [](StoreState& ui) { ui.action(CONFIRM); } },
        { "details-id", [](StoreState& ui) {
             ui.selected = 2; // "[PS4] ... (CUSAxxxxx) [1.xx]"
             ui.action(CONFIRM);
         } },
        { "details-versions", [](StoreState& ui) {
             // The game with the most entries, its second one chosen.
             size_t best = 0;
             for (size_t g = 0; g < ui.groups.size(); ++g) {
                 if (ui.groups[g].size() > ui.groups[best].size()) best = g;
             }
             ui.selected = static_cast<int>(best);
             ui.action(CONFIRM);
             if (!ui.canDownloadVersion()) {
                 ui.action(CONFIRM);
                 ui.action(DOWN);
                 ui.action(CONFIRM);
             }
         } },
        { "details-version-picker", [](StoreState& ui) {
             const auto grouped = std::find_if(ui.groups.begin(), ui.groups.end(), [](const auto& g) { return g.size() > 1; });
             if (grouped == ui.groups.end()) return;
             ui.selected = static_cast<int>(grouped - ui.groups.begin());
             ui.action(CONFIRM);
             ui.action(CONFIRM);
         } },
        { "details-screenshot", [](StoreState& ui) {
             ui.action(CONFIRM);
             ui.screenshotOpen = ui.detail;
         } },
        { "search", [](StoreState& ui) {
             ui.action(SEARCH);
             ui.query = "GOD";
             ui.filter();
         } },
        { "settings", [](StoreState& ui) {
             ui.status = "3 updated / 0 cached / 0 unavailable";
             ui.action(SETTINGS);
             ui.settingsSelected = SETTING_LOCATION;
         } },
        { "download-checking", [](StoreState& ui) {
             ui.action(CONFIRM);
             ui.action(CONFIRM); // the game page's Download
         } },
        { "settings-debrid", [](StoreState& ui) {
             ui.action(SETTINGS);
             ui.settingsSelected = SETTING_DEBRID;
             ui.action(CONFIRM);
             ui.debridChoice = 1;
         } },
        { "settings-source", [](StoreState& ui) {
             ui.action(SETTINGS);
             ui.action(CONFIRM);
         } },
        { "settings-sort", [](StoreState& ui) {
             ui.action(SETTINGS);
             ui.action(DOWN);
             ui.action(CONFIRM);
         } },
        { "download", [](StoreState& ui) {
             ui.action(CONFIRM);
             ui.action(CONFIRM);
             ui.sourceLoading = false;
             ui.sources = { { "Base game (v1.00)", "" }, { "Update (v1.05)", "" }, { "DLC pack", "" } };
         } },
        { "downloads", showQueue },
        { "background-service", [](StoreState& ui) {
             showQueue(ui);
             ui.action(MANAGE);
             ui.updateWorkerOptions(WorkerInstallState::Current, true);
         } },
        { "trailer-loading", [](StoreState& ui) {
             ui.action(CONFIRM);
             ui.trailerOpen = ui.detail;
         } },
        { "downloads-empty", [](StoreState& ui) {
             ui.tab = TAB_DOWNLOADS;
             ui.serviceStatus = "In-app downloads | Keep NULL_store open.";
         } },
        { "install", [](StoreState& ui) {
             showQueue(ui);
             ui.installPicker = true;
             InstallPackage package;
             package.jobId = 1;
             package.name = "Generated sample - base.pkg";
             package.titleId = "TEST00001";
             package.bytes = 100000000;
             package.note = "Ready to install";
             ui.packages.push_back(package);
         } },
        { "location", [](StoreState& ui) {
             ui.action(SETTINGS);
             ui.settingsSelected = SETTING_LOCATION;
             ui.action(CONFIRM);
         } },
        { "notice", [](StoreState& ui) { ui.notice = "Added to Downloads."; ui.noticeStays = true; } },
    };
    return list;
}

} // namespace

int main(int argc, char** argv)
{
    std::error_code ignored;
    std::filesystem::create_directories(kData, ignored);
    std::filesystem::create_directories(kOutput, ignored);

    Renderer renderer;
    if (!renderer.init(createRenderBackend(), 1920, 1080, std::string(kAssets) + "/font.ttf")) {
        fprintf(stderr, "Run the preview from the repository root: assets/font.ttf was not found.\n");
        return 1;
    }
    CoverCache covers(renderer, kAssets, kData);
    // Official artwork is shown for the titles that have it under
    // build/preview-data/art/<title ID>/ (icon0.webp, pic0.webp).
    GameArt art(renderer, kData);
    StoreView view(renderer, covers, art, kAssets);

    std::vector<std::string> wanted;
    std::string suffix;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.compare(0, 7, "--lang=") == 0) {
            setLanguage(languageFromName(arg.substr(7)));
            suffix = currentLanguage() == Language::English ? std::string() : "-" + arg.substr(7);
            continue;
        }
        wanted.push_back(arg);
    }
    if (wanted.empty()) {
        wanted.push_back("catalogue");
    }
    const bool all = wanted.size() == 1 && wanted[0] == "all";

    int written = 0;
    double clock = 0.0;
    for (const Scene& scene : scenes()) {
        bool selected = all;
        for (const std::string& name : wanted) {
            selected = selected || name == scene.name;
        }
        if (!selected) {
            continue;
        }
        StoreState ui(kAssets, kData);
        if (ui.games.empty()) {
            for (int i = 0; i < 48; ++i) {
                Game game;
                game.id = i + 1;
                game.title = "Preview game " + std::to_string(i + 1);
                game.year = "2025";
                game.size = "1.4 GB";
                ui.games.push_back(game);
            }
            ui.shareTitleIds();
            ui.filter();
        }
        scene.setup(ui);

        // Twice: the first pass sets the animations off, a long step later
        // the second shows them finished (covers loaded, cards grown).
        for (int pass = 0; pass < 2; ++pass) {
            for (int frame = 0; frame < 16; ++frame) {
                covers.pump(clock += 1.0);
                art.pump(clock);
            }
            renderer.beginFrame(theme::kBackground);
            view.draw(ui, pass ? 10.0f : 0.0f);
            renderer.endFrame();
        }
        for (float waited = 0.0f; waited < scene.rest; waited += 1.0f / 30.0f) {
            covers.pump(clock += 1.0);
            art.pump(clock);
            renderer.beginFrame(theme::kBackground);
            view.draw(ui, 1.0f / 30.0f);
            renderer.endFrame();
        }

        std::vector<uint8_t> pixels;
        const std::string path = std::string(kOutput) + "/" + scene.name + suffix + ".png";
        if (!renderer.capture(pixels) || !stbi_write_png(path.c_str(), 1920, 1080, 4, pixels.data(), 1920 * 4)) {
            fprintf(stderr, "Could not write %s\n", path.c_str());
            return 1;
        }
        printf("%s\n", path.c_str());
        ++written;
    }
    if (!written) {
        fprintf(stderr, "Unknown scene. Scenes:");
        for (const Scene& scene : scenes()) {
            fprintf(stderr, " %s", scene.name);
        }
        fprintf(stderr, " all\n");
        return 1;
    }
    return 0;
}
