#pragma once

#include <map>
#include <string>
#include <vector>

#include "graphics/Renderer.h"
#include "ui/Animation.h"
#include "ui/CoverCache.h"
#include "ui/GameArt.h"
#include "ui/GameText.h"
#include "ui/StoreState.h"
#include "ui/Widgets.h"

// -----------------------------------------------------------------------------
// Draws a StoreState: the tab bar, the catalogue grid with the focused game's
// details, the game page, the download queue and the dialogs that open on top.
// One function per part of the screen.
//
// The view keeps only what animation needs between frames (where the grid has
// scrolled to, how far a card has grown); everything else comes from the state.
// -----------------------------------------------------------------------------
namespace ns {

class StoreView {
public:
    StoreView(Renderer& renderer, CoverCache& covers, GameArt& art, const std::string& assetPath);

    // Draws one frame (between Renderer::beginFrame and endFrame). `dt` is the
    // time since the previous frame, in seconds.
    void draw(StoreState& state, float dt);

private:
    // Catalogue
    void drawTabBar(const StoreState& s);
    void drawFocusedGame(StoreState& s);
    void drawGrid(StoreState& s);
    // `index`: the tile's place in s.visible (and its group in s.groups).
    void drawCard(const StoreState& s, int index, const Rect& slot, float focus, float sweep, int order);
    void drawVersions(StoreState& s, float x, float y);
    void drawCatalogueHints(StoreState& s);
    void drawGamePage(StoreState& s, float amount);
    // The game's name, title ID and version as the screens show them.
    GameText textOf(const StoreState& s, const Game& game) const;
    // Year, size, title ID and version as chips from `x`; returns the x after them.
    float drawFacts(const StoreState& s, const Game& game, float x, float y);

    // Download queue
    void drawDownloads(StoreState& s);
    void drawJob(const DownloadRow& job, const Rect& rect, bool focused);

    // Dialogs
    Rect dialog(float width, float height, const std::string& title);
    void dialogHints(const Rect& frame, const std::vector<ui::Hint>& hints);
    void option(const Rect& rect, const std::string& label, int size, bool focused);
    // A row of Settings: its name on the left, its current value on the right.
    void settingRow(const Rect& rect, const std::string& label, const std::string& value, bool focused);
    void drawSourcePicker(const StoreState& s);
    void drawKeyboard(const StoreState& s);
    void drawInstallPicker(const StoreState& s);
    void drawSettings(const StoreState& s);
    void drawLocationPicker(const StoreState& s);
    void drawSourceFilterPicker(const StoreState& s);
    void drawSortPicker(const StoreState& s);
    void drawNotice(StoreState& s);
    void drawDebridPicker(const StoreState& s);

    float textFitRight(const std::string& text, float right, float baseline, int size, const Color& color,
                       float maxWidth);

    Renderer& r_;
    CoverCache& covers_;
    GameArt& art_;
    Texture logo_;

    float dt_ = 0.0f;
    std::string noticeText_; // the notice on screen, and for how long
    float noticeAge_ = 0.0f;
    float loadingTime_ = 0.0f; // the bar of "Checking torrent link"
    Smooth tabX_;                    // the white pill sliding between tabs
    Smooth gridScroll_;              // first row in view, in rows
    int gridTopRow_ = 0;
    std::string gridKey_;            // which list the scroll position belongs to
    std::map<int, float> cardFocus_; // game id -> how far its card has grown
    float reflection_ = 0.0f;        // where the selection's highlight is, in turns
    int restingOn_ = 0;              // the game the cursor has been resting on
    float restTime_ = 0.0f;          // for how long, in seconds
    float sweepTime_ = 0.0f;         // progress of the shine crossing its cover
    float focusTime_ = 0.0f;         // since the cursor last moved to another game
    Smooth gamePage_;
    Smooth dialog_;
    unsigned openDialogs_ = 0;
    Smooth notice_;
};

} // namespace ns
