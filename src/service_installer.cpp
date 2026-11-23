// NULL_store Background Service - the optional, separately installed setup
// application: one screen that installs or starts the download service.
#include <pthread.h>
#include <string>
#include <sys/stat.h>

#include <orbis/Pad.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include "data_root.h"
#include "installer.h"
#include "graphics/Renderer.h"
#include "network_ps4.h"
#include "service_protocol.h"
#include "service_start_ps4.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static bool busy = false;
static std::string result = "Press X to install or start the optional service.";

static void* install(void*)
{
    std::string error;
    bool ok = portalStartPackagedWorker(error);
    const auto backend = ps4InstallBackend();
    if (!ok && !portalWorkerRunning() && backend.checkWorker() == WorkerInstallState::Missing) {
        PackageInstaller installer(nullStoreDataRoot(), backend);
        if (installer.start()) {
            installer.scan(0, "");
            uint64_t version = 0;
            bool submitted = false;
            error = "Check PS4 Notifications for the worker installation.";
            for (int i = 0; i < 600; ++i) {
                bool loading = false;
                std::vector<InstallPackage> files;
                std::string why;
                if (installer.snapshot(version, files, loading, why) && !loading) {
                    if (!why.empty()) { error = why; break; }
                    if (!files.empty()) {
                        const auto& pkg = files.front();
                        if (pkg.state == InstallState::Error) { error = pkg.note; break; }
                        if (pkg.state == InstallState::Complete) { ok = portalStartPackagedWorker(error); break; }
                        if (pkg.state == InstallState::Ready && !submitted && !portalWorkerRunning()) {
                            installer.submit(pkg.path);
                            submitted = true;
                        }
                    }
                }
                sceKernelUsleep(100000);
            }
        }
    }
    pthread_mutex_lock(&mutex);
    result = ok ? "Service connected. You can close this setup app and use NULL_store." : error;
    busy = false;
    pthread_mutex_unlock(&mutex);
    return nullptr;
}

static void draw(ns::Renderer& renderer, const ns::Texture& logo, const std::string& text, bool working)
{
    using namespace ns;
    using namespace ns::theme;
    renderer.beginFrame(kBackground);
    ui::background(renderer, kGlow);
    if (logo.valid()) {
        renderer.image(logo, Rect(kSafeX, 72, 64, 64));
    }
    renderer.text("NULL_store", kSafeX + 80, ui::baseline(renderer, 104, 36), 36, kText);

    renderer.text("Background service setup", kSafeX, 300, 64, kText);
    renderer.textLines("NULL_store downloads work without this service while the app is open. Install the service "
                       "to keep downloads running after closing NULL_store.",
                       kSafeX, 380, 32, kMuted, 1500, 3);

    const Rect status(kSafeX, 560, kContentW, 190);
    ui::surface(renderer, status, kPanelRadius);
    renderer.textLines(text, status.x + 40, status.y + 70, 32, kAccentBright, status.w - 80, 3);

    if (working) {
        renderer.text("Please wait for setup to finish.", kSafeX, ui::baseline(renderer, kHintTop + kKeySize * 0.5f, kFontLabel),
                      kFontLabel, kMuted);
    } else {
        ui::hintsUntil(renderer, kContentRight, kHintTop,
                       { { { ui::Key::Cross }, "Install / Start service" }, { { ui::Key::Circle }, "Close" } }, kKeySize);
    }
    renderer.endFrame();
}

int main()
{
    mkdir(nullStoreDataRoot(), 0777);
    portalNetworkInit();

    ns::Renderer renderer;
    if (!renderer.init(ns::createRenderBackend(), 1920, 1080, "/app0/assets/font.ttf")) {
        return 1;
    }
    ns::Texture logo;
    ns::Image logoImage;
    if (logoImage.load("/app0/assets/brand/nullstore-bag.png", 192)) {
        logo.create(renderer.backend(), logoImage);
    }

    OrbisUserServiceInitializeParams params = {};
    params.priority = 256;
    sceUserServiceInitialize(&params);
    int user = -1;
    sceUserServiceGetInitialUser(&user);
    scePadInit();
    const int pad = scePadOpen(user, 0, 0, nullptr);
    uint32_t previous = 0;

    for (int frame = 0;; ++frame) {
        OrbisPadData state = {};
        if (pad >= 0 && scePadReadState(pad, &state) >= 0 && state.connected) {
            const uint32_t edge = state.buttons & ~previous;
            previous = state.buttons;
            pthread_mutex_lock(&mutex);
            const bool working = busy;
            pthread_mutex_unlock(&mutex);
            if (!working && (edge & ORBIS_PAD_BUTTON_CROSS)) {
                pthread_mutex_lock(&mutex);
                busy = true;
                result = "Installing background service...";
                pthread_mutex_unlock(&mutex);
                pthread_t thread;
                if (pthread_create(&thread, nullptr, install, nullptr) == 0) {
                    pthread_detach(thread);
                } else {
                    pthread_mutex_lock(&mutex);
                    busy = false;
                    result = "Could not start setup worker. Restart this app.";
                    pthread_mutex_unlock(&mutex);
                }
            } else if (!working && (edge & ORBIS_PAD_BUTTON_CIRCLE)) {
                return sceSystemServiceLoadExec("exit", nullptr);
            }
        }

        pthread_mutex_lock(&mutex);
        const std::string text = result;
        const bool working = busy;
        pthread_mutex_unlock(&mutex);
        draw(renderer, logo, text, working);
        if (frame == 0) {
            sceSystemServiceHideSplashScreen();
        }
        sceKernelUsleep(16000); // the context does not wait for vertical sync
    }
}
