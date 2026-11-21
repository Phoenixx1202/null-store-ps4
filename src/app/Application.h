#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "app/CatalogRefresh.h"
#include "download_controller.h"
#include "graphics/Renderer.h"
#include "input/Gamepad.h"
#include "installer.h"
#include "text_input_ps4.h"
#include "ui/CoverCache.h"
#include "ui/GameArt.h"
#include "ui/StoreState.h"
#include "ui/StoreView.h"

// -----------------------------------------------------------------------------
// The store application: owns the renderer, the controller, the store's state
// and the workers (catalogue refresh, downloads, package installer) and runs
// the frame loop that ties them together.
// -----------------------------------------------------------------------------
namespace ns {

class Application {
public:
    Application();
    ~Application();

    // Runs until the console closes the application. Returns non-zero when
    // start-up failed.
    int run();

private:
    bool init();
    void frame();
    void handleInput();
    void handleKeyboard();
    void handleRequests();
    void syncWorkers();
    void render();


    std::string data_;
    std::unique_ptr<StoreState> ui_;
    std::unique_ptr<DownloadController> downloads_;
    std::unique_ptr<PackageInstaller> installer_;
    std::unique_ptr<CatalogRefresh> refresh_;
    std::unique_ptr<CoverCache> covers_;
    std::unique_ptr<GameArt> art_;
    std::unique_ptr<StoreView> view_;
    Renderer renderer_;
    input::Gamepad pad_;
    NativeTextInput textInput_;

    bool downloadsReady_ = false;
    bool installerReady_ = false;
    bool keyboardFallback_ = false;
    bool keyboardForKey_ = false; // the open keyboard types the Debrid key
    std::u16string keyboardTitle_, keyboardHint_; // held while the keyboard is open
    bool splashHidden_ = false;
    uint64_t lastFrameMicros_ = 0;
    uint64_t downloadVersion_ = 0;
    uint64_t installVersion_ = 0;
};

} // namespace ns
