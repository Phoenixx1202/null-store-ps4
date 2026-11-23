#include "ui/StoreView.h"

#include "core/Log.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace ns {

using namespace theme;
using ui::Hint;
using ui::Key;

namespace {

static_assert(kGridColumns == StoreState::kGridColumns, "The grid's layout and its navigation must agree");
static_assert(kGridColumns * kGridRows == StoreState::kGridPage, "A page jump moves one screen of covers");

const char* const kTabs[] = { "All games", "Favorites", "2024 & newer", "Downloads" };
const char* const kCollections[] = { "ALL GAMES", "FAVORITES", "2024 & NEWER", "DOWNLOADS" };
const char* const kDot = "  \xC2\xB7  "; // middle dot between two facts

constexpr float kHintCenter = kHintTop + kKeySize * 0.5f;
constexpr float kGridClipTop = kGridTop - 16.0f;
constexpr float kGridHeight = static_cast<float>(kGridRows - 1) * kCardPitchY + kCardH;
constexpr float kGridClipBottom = kGridTop + kGridHeight + 14.0f;
// The cover under the cursor: after a pause a shine crosses it, again and again.
constexpr float kSweepPause = 2.0f;
constexpr float kSweepSeconds = 0.9f;
constexpr float kSweepIntensity = 0.30f;
constexpr float kNoticeSeconds = 3.0f;

Rect box(float x, float y, float w, float h)
{
    return Rect(x, y, w, h);
}

Color stateColor(DownloadState state)
{
    switch (state) {
    case DownloadState::Failed: return kError;
    case DownloadState::Paused:
    case DownloadState::Pausing: return kWarning;
    case DownloadState::Complete: return kAccentBright;
    case DownloadState::Queued:
    case DownloadState::Cancelled: return kMuted;
    default: return kAccent;
    }
}

} // namespace

StoreView::StoreView(Renderer& renderer, CoverCache& covers, GameArt& art, const std::string& assetPath)
    : r_(renderer),
      covers_(covers),
      art_(art),
      tabX_(kTabFirstX, kFocusSpeed),
      gridScroll_(0.0f, kScrollSpeed),
      gamePage_(0.0f, kFadeSpeed),
      dialog_(0.0f, kFadeSpeed),
      notice_(0.0f, kFadeSpeed)
{
    Image logo;
    if (logo.load(assetPath + "/brand/nullstore-bag.png", 192)) {
        logo_.create(r_.backend(), logo);
    } else {
        NS_LOG_WARN("Logo missing: %s/brand/nullstore-bag.png", assetPath.c_str());
    }
}

float StoreView::textFitRight(const std::string& text, float right, float baseline, int size, const Color& color,
                              float maxWidth)
{
    const float width = std::min(r_.measureText(text, size), maxWidth);
    return r_.textFit(text, right - width, baseline, size, color, maxWidth);
}

GameText StoreView::textOf(const StoreState& s, const Game& game) const
{
    GameText text = describe(game);
    if (text.titleId.empty()) {
        const auto learnt = s.displayTitleIds.find(game.id);
        if (learnt != s.displayTitleIds.end()) {
            text.titleId = learnt->second;
        }
    }
    if (!text.titleId.empty()) {
        const auto official = s.officialNames.find(text.titleId);
        if (official != s.officialNames.end() && !official->second.empty() && !hasNonLatin(official->second)) {
            text.title = official->second;
        }
    }
    return text;
}

float StoreView::drawFacts(const StoreState& s, const Game& game, float x, float y)
{
    const GameText text = textOf(s, game);
    if (!game.year.empty()) {
        x += ui::chip(r_, x, y, game.year, kText, kGlass) + 10.0f;
    }
    if (!game.size.empty() && game.size != "Size on page") {
        x += ui::chip(r_, x, y, tr(game.size), kText, kGlass) + 10.0f;
    }
    if (!text.titleId.empty()) {
        x += ui::chip(r_, x, y, text.titleId, kText, kGlass) + 10.0f;
    }
    if (!text.version.empty()) {
        x += ui::chip(r_, x, y, "v" + text.version, kText, kGlass) + 10.0f;
    }
    return x;
}

void StoreView::draw(StoreState& s, float dt, const Texture* trailerFrame, TrailerState trailerStatus, int trailerProgress)
{
    dt_ = dt;
    spinnerPhase_ = std::fmod(spinnerPhase_ + dt, 1.0f);
    if (s.trailerOpen && trailerFrame && trailerFrame->valid() &&
        (trailerStatus == TrailerState::Playing || trailerStatus == TrailerState::Rebuffering)) {
        drawFullscreen(*trailerFrame, "");
        if (trailerStatus == TrailerState::Rebuffering) {
            spinner(kSafeX + 12, kHintCenter, 10);
            r_.text(tr("Buffering trailer…"), kSafeX + 44, ui::baseline(r_, kHintCenter, kFontLabel), kFontLabel, kText);
        }
        return;
    }
    s.clampSelections();
    reflection_ += dt / ui::kReflectionSeconds;
    reflection_ -= std::floor(reflection_);
    ui::background(r_, kGlow);

    const bool queue = s.tab == TAB_DOWNLOADS;
    drawTabBar(s);
    if (queue) {
        drawDownloads(s);
    } else {
        drawFocusedGame(s);
        drawGrid(s);
        drawCatalogueHints(s);
    }

    gamePage_.set(!queue && s.detail && s.current() ? 1.0f : 0.0f);
    const float page = gamePage_.update(dt);
    if (page > 0.01f && !queue && s.current()) {
        drawGamePage(s, page);
    }

    // Dialogs fade in; the one open is told apart by its flag.
    const unsigned open = (s.workerStarting ? 1024u : s.confirmWorkerRemoval ? 512u : s.workerMenu || s.removeMenu ? 256u : 0u) | (s.sourcePicker ? 1u : 0u) | (s.keyboard ? 2u : 0u) | (s.installPicker ? 4u : 0u) |
                          (s.settings ? 8u : 0u) | (s.downloadLocationPicker ? 16u : 0u) |
                          (s.sourceFilterPicker ? 32u : 0u) | (s.sortPicker ? 64u : 0u) | (s.debridPicker ? 128u : 0u) |
                          (s.trailerOpen ? 2048u : 0u) | (s.screenshotOpen ? 4096u : 0u) | (s.versionPicker ? 8192u : 0u);
    if (open != openDialogs_) {
        openDialogs_ = open;
        dialog_.snap(0.0f);
    }
    dialog_.set(open ? 1.0f : 0.0f);
    const float shown = dialog_.update(dt);
    if (open) {
        r_.rect(box(0, 0, kScreenW, kScreenH), kScrim.withAlpha(kScrim.a * shown));
        r_.pushOpacity(shown);
        if (s.workerStarting) {
            const Rect frame = dialog(1000, 340, "Starting download worker");
            spinner(frame.x + 74, frame.y + 147, 12);
            r_.textFit("Please wait while the store saves your queue and checks the worker.",
                       frame.x + 108, frame.y + 155, kFontBody, kDim, frame.w - 164);
        } else if (s.confirmWorkerRemoval) {
            const bool uninstall = s.confirmedWorkerAction == WorkerAction::Uninstall;
            const Rect frame = dialog(1000, 500, uninstall ? "Uninstall download worker?" :
                                      s.confirmedWorkerAction == WorkerAction::Reinstall ?
                                      "Reinstall download worker?" : "Replace download worker?");
            r_.textFit(uninstall ? "Downloads can continue in the open store. Your files and queue are kept." :
                       "The current worker will be removed. You can then install the included build.",
                       frame.x + 56, frame.y + 145, kFontBody, kDim, frame.w - 112);
            option(box(frame.x + 56, frame.y + 195, frame.w - 112, 58), tr("Cancel"), kFontBody, s.menuChoice == 0);
            option(box(frame.x + 56, frame.y + 265, frame.w - 112, 58), uninstall ? "Uninstall worker" : tr("Continue"), kFontBody, s.menuChoice == 1);
            dialogHints(frame, {{{Key::Cross}, tr("Select")}, {{Key::Circle}, tr("Back")}});
        } else if (s.workerMenu || s.removeMenu) {
            const char* remove[] = {"Remove and keep files", "Remove and delete files", "Cancel"};
            const int count = s.workerMenu ? static_cast<int>(s.workerActions.size()) : 3;
            const Rect frame = dialog(1000, 240 + 70 * count, s.workerMenu ? "Background downloads" : "Remove download");
            if (s.workerMenu && s.workerActions.size() == 1) {
                const char* status = s.workerRemovalBusy ? "Updating worker…" :
                                     s.workerInstall == WorkerInstallState::Installing ? "Worker installation is in progress…" :
                                     s.workerInstall == WorkerInstallState::Unknown ? "Could not determine whether the worker is installed." :
                                     "Checking worker installation…";
                const bool busy = s.workerRemovalBusy || s.workerInstall == WorkerInstallState::Installing ||
                                  s.workerInstall == WorkerInstallState::Unchecked;
                if (busy) spinner(frame.x + 68, frame.y + 120, 9);
                r_.textFit(status, frame.x + (busy ? 92 : 56), frame.y + 126, kFontLabel, kDim, frame.w - 148);
            } else if (s.workerMenu && s.workerInstall == WorkerInstallState::Installed) {
                r_.textFit("Worker installed. Build verification unavailable.",
                           frame.x + 56, frame.y + 126, kFontLabel, kDim, frame.w - 112);
            }
            for (int i = 0; i < count; ++i)
                option(box(frame.x + 56, frame.y + 150 + i * 70, frame.w - 112, 58),
                       tr(s.workerMenu ? workerActionLabel(s.workerActions[i]) : remove[i]), kFontBody, i == s.menuChoice);
            dialogHints(frame, {{{Key::Cross}, tr("Select")}, {{Key::Circle}, tr("Back")}});
        }
        if (s.sourcePicker) drawSourcePicker(s);
        if (s.keyboard) drawKeyboard(s);
        if (s.installPicker) drawInstallPicker(s);
        if (s.versionPicker) drawVersionPicker(s);
        if (s.screenshotOpen) drawScreenshot(s);
        if (s.trailerOpen) drawTrailer(s, trailerStatus, trailerProgress);
        // Settings and the choices it opens: only the one on top is drawn.
        if (s.downloadLocationPicker) {
            drawLocationPicker(s);
        } else if (s.sourceFilterPicker) {
            drawSourceFilterPicker(s);
        } else if (s.sortPicker) {
            drawSortPicker(s);
        } else if (s.debridPicker) {
            drawDebridPicker(s);
        } else if (s.settings) {
            drawSettings(s);
        }
        r_.popOpacity();
    }

    drawNotice(s);
    s.dirty = false;
}

void StoreView::spinner(float x, float y, float radius)
{
    constexpr int dots = 8;
    const float bead = std::max(2.0f, radius * 0.23f);
    for (int i = 0; i < dots; ++i) {
        const float angle = (spinnerPhase_ + float(i) / dots) * 6.2831853f;
        r_.roundedRect(box(x + std::cos(angle) * radius - bead, y + std::sin(angle) * radius - bead,
                           bead * 2, bead * 2), bead, kAccentBright.withAlpha(float(i + 1) / dots));
    }
}

// -- Tab bar ---------------------------------------------------------------------

void StoreView::drawTabBar(const StoreState& s)
{
    const float centerY = kTabTop + kTabHeight * 0.5f;

    if (logo_.valid()) {
        r_.image(logo_, box(kSafeX, centerY - 25.0f, 50.0f, 50.0f));
    }
    r_.text("NULL_store", kSafeX + 62.0f, ui::baseline(r_, centerY, 30), 30, kText);

    // L1 / R1 walk through the collections; the one in view is the white pill.
    ui::key(r_, Key::L1, kTabFirstX - 64.0f, centerY - kKeySize * 0.5f, kKeySize);
    tabX_.set(kTabFirstX + static_cast<float>(s.tab) * kTabPitch);
    r_.roundedRect(box(tabX_.update(dt_), kTabTop, kTabWidth, kTabHeight), kTabHeight * 0.5f, kFocusFill);
    for (int i = 0; i < 4; ++i) {
        const float x = kTabFirstX + static_cast<float>(i) * kTabPitch;
        r_.textCentered(tr(kTabs[i]), x + kTabWidth * 0.5f, ui::baseline(r_, centerY, kFontBody), kFontBody,
                        i == s.tab ? kInk : kMuted);
    }
    ui::key(r_, Key::R1, kTabFirstX + 3.0f * kTabPitch + kTabWidth + 22.0f, centerY - kKeySize * 0.5f, kKeySize);
}

// -- Catalogue -------------------------------------------------------------------

// The game under the cursor: its title and facts, above the grid.
void StoreView::drawFocusedGame(StoreState& s)
{
    float x = kSafeX;
    const std::string collection = s.query.empty() ? std::string(tr(kCollections[s.tab])) : trf("SEARCH: \"{0}\"", s.query);
    x += r_.text(collection, x, 196, kFontLabel, kAccentBright, 900);
    x += r_.text(kDot + trf(s.visible.size() == 1 ? "{0} game" : "{0} games", std::to_string(s.visible.size())), x, 196,
                 kFontLabel, kMuted);
    if (!s.query.empty()) {
        ui::hintsFrom(r_, x + 26.0f, 196.0f - 25.0f, { { { Key::Circle }, tr("Clear") } }, 34.0f);
    }

    if (s.refreshing) {
        const auto label = tr("Refreshing catalogue…");
        const float left = kContentRight - r_.measureText(label, kFontSmall);
        spinner(left - 25, 188, 8);
        r_.text(label, left, 196, kFontSmall, kMuted);
    }

    const Game* game = s.current();
    if (!game) {
        return;
    }
    r_.textFit(textOf(s, *game).title, kSafeX, 258, kFontDisplay, kText, kContentW);

    float chipX = drawFacts(s, *game, kSafeX, 282);
    const std::vector<int>* group = s.currentGroup();
    const size_t entries = group ? group->size() : 1;
    chipX += ui::chip(r_, chipX, 282,
                      entries > 1 ? trf("{0} versions", std::to_string(entries)) : std::string(sourceName(sourceForURL(game->url))),
                      kMuted, Color(0, 0, 0, 0)) +
             10.0f;
    if (group && s.favoriteGroup(*group)) {
        ui::chip(r_, chipX, 282, tr("Favorite"), kInk, kAccentBright);
    }
}

void StoreView::drawGrid(StoreState& s)
{
    const int count = static_cast<int>(s.visible.size());
    if (!count) {
        r_.textCentered(tr("No games found"), kScreenW * 0.5f, 560, kFontTitle, kText);
        r_.textCentered(s.tab == TAB_FAVORITES ? tr("Press SQUARE on a game to save it here.")
                                               : tr("Press TRIANGLE to change your search, or CIRCLE to reset."),
                        kScreenW * 0.5f, 612, kFontBody, kMuted);
        return;
    }

    // A few rows are in view. The window follows the cursor; a different list
    // (collection, search, sort, source) starts where its cursor is.
    const int row = s.selected / kGridColumns;
    const int rows = (count + kGridColumns - 1) / kGridColumns;
    const std::string key = std::to_string(s.tab) + '/' + std::to_string(s.sort) + '/' +
                            std::to_string(s.sourceFilter) + '/' + std::to_string(count) + '/' + s.query;
    const bool sameList = key == gridKey_;
    const int pageTop = (row / kGridRows) * kGridRows;
    const bool samePage = gridTopRow_ == pageTop;
    gridTopRow_ = pageTop;
    if (sameList && samePage) {
        gridScroll_.set(static_cast<float>(gridTopRow_));
    } else {
        gridKey_ = key;
        gridScroll_.snap(static_cast<float>(gridTopRow_));
    }
    const float scroll = gridScroll_.update(dt_);

    // Cards grow towards the cursor and shrink back when it leaves.
    const int focusedId = s.games[s.visible[s.selected]].id;
    // The shine waits for the cursor to rest, and for the catalogue to be in front.
    const bool covered = s.detail || openDialogs_ != 0;
    if (focusedId != restingOn_ || covered) {
        if (focusedId != restingOn_) {
            reflection_ = 0.0f;
        }
        if (focusedId != restingOn_) {
            focusTime_ = 0.0f;
        }
        restingOn_ = focusedId;
        restTime_ = 0.0f;
        sweepTime_ = 0.0f;
    }
    restTime_ += dt_;
    focusTime_ += dt_;
    // Once the cursor has settled on a game, its background picture is
    // fetched, so that it is there when the game's page opens.
    if (focusTime_ > 0.35f && s.officialArt) {
        const std::string titleId = textOf(s, s.games[s.visible[s.selected]]).titleId;
        if (!titleId.empty()) {
            art_.get(titleId);
        }
    }
    float sweep = -1.0f;
    if (restTime_ >= kSweepPause) {
        sweep = sweepTime_ / kSweepSeconds;
        sweepTime_ += std::min(dt_, 1.0f / 30.0f);
        if (sweepTime_ >= kSweepSeconds) {
            restTime_ = 0.0f;
            sweepTime_ = 0.0f;
        }
    }
    cardFocus_[focusedId];
    const float approach = 1.0f - std::exp(-kFocusSpeed * dt_);
    for (auto it = cardFocus_.begin(); it != cardFocus_.end();) {
        const float target = it->first == focusedId ? 1.0f : 0.0f;
        it->second += (target - it->second) * approach;
        if (target == 0.0f && it->second < 0.01f) {
            it = cardFocus_.erase(it);
        } else {
            ++it;
        }
    }

    r_.pushClip(box(0, kGridClipTop, kScreenW, kGridClipBottom - kGridClipTop));
    const int firstRow = std::max(0, static_cast<int>(std::floor(scroll)) - 1);
    const int lastRow = std::min(rows - 1, firstRow + kGridRows + 2);
    struct Raised {
        int index;
        Rect slot;
        float focus;
    };
    // Tiles load in reading order from the top of the window; the rows kept
    // ready above and below it come after.
    const int windowStart = gridTopRow_ * kGridColumns;
    const auto turn = [windowStart](int index) {
        const int place = index - windowStart;
        return place >= 0 && place < kGridColumns * kGridRows ? place : 1000 + std::abs(place);
    };
    std::vector<Raised> raised; // drawn last, over their neighbours
    for (int gridRow = firstRow; gridRow <= lastRow; ++gridRow) {
        for (int column = 0; column < kGridColumns; ++column) {
            const int index = gridRow * kGridColumns + column;
            if (index >= count) {
                break;
            }
            const Game& game = s.games[s.visible[index]];
            const Rect slot(kSafeX + static_cast<float>(column) * (kCardW + kCardGapX),
                            kGridTop + (static_cast<float>(gridRow) - scroll) * kCardPitchY, kCardW, kCardH);
            const auto focus = cardFocus_.find(game.id);
            if (focus != cardFocus_.end() && focus->second > 0.001f) {
                raised.push_back({ index, slot, focus->second });
            } else {
                drawCard(s, index, slot, 0.0f, -1.0f, turn(index));
            }
        }
    }
    std::sort(raised.begin(), raised.end(), [](const Raised& a, const Raised& b) { return a.focus < b.focus; });
    for (const Raised& card : raised) {
        const Game& game = s.games[s.visible[card.index]];
        drawCard(s, card.index, card.slot, card.focus, game.id == focusedId ? sweep : -1.0f, turn(card.index));
    }
    r_.popClip();

    // Where the window is in the whole list.
    if (rows > kGridRows) {
        const Rect track(kContentRight + 30.0f, kGridTop, 4.0f, kGridHeight);
        r_.roundedRect(track, 2.0f, kGlass);
        const float thumb = std::max(48.0f, track.h * kGridRows / static_cast<float>(rows));
        const float travel = clampf(scroll / static_cast<float>(rows - kGridRows), 0.0f, 1.0f);
        r_.roundedRect(box(track.x, track.y + (track.h - thumb) * travel, track.w, thumb), 2.0f,
                       kText.withAlpha(0.55f));
    }
}

void StoreView::drawCard(const StoreState& s, int index, const Rect& slot, float focus, float sweep, int order)
{
    const Game& game = s.games[s.visible[index]];
    const GameText text = textOf(s, game);
    const bool grouped = index < int(s.groups.size());
    const bool favorite = grouped ? s.favoriteGroup(s.groups[index]) : s.favorites.count(game.id) != 0;
    const size_t entries = grouped ? s.groups[index].size() : 1;
    const float scale = 1.0f + (kCardFocusScale - 1.0f) * focus;
    const Rect art(slot.x - slot.w * (scale - 1.0f) * 0.5f, slot.y - slot.h * (scale - 1.0f) * 0.5f, slot.w * scale,
                   slot.h * scale);
    const float radius = kCardRadius * scale;
    if (focus > 0.02f) {
        ui::shadow(r_, art, radius, focus * 0.6f);
    }

    // A cover that has just arrived fades in over its placeholder.
    const Cover* cover = covers_.get(game, text.titleId, order);
    const float arrived =
        cover ? clampf(static_cast<float>((covers_.now() - cover->readyTime) / 0.2), 0.0f, 1.0f) : 0.0f;
    if (arrived < 1.0f) {
        r_.roundedRect(art, radius, Color(0.07f, 0.13f, 0.11f));
        r_.roundedOutline(art, radius, 1.0f, kGlassBorder);
        r_.pushClip(art);
        if (!cover && covers_.loading(game.id)) spinner(art.right() - 24, art.bottom() - 22, 8);
        r_.textLines(text.title, art.x + 16.0f, art.y + 44.0f, kFontSmall, kMuted, art.w - 32.0f, 4);
        if (entries <= 1) { // with several entries the badge takes this place
            r_.text(sourceName(sourceForURL(game.url)), art.x + 16.0f, art.bottom() - 18.0f, 15, kDim, art.w - 32.0f);
        }
        r_.popClip();
    }
    if (cover) {
        // The covers around the cursor step back a little.
        const float light = 0.78f + 0.22f * focus;
        r_.image(cover->texture, art, Rect(0, 0, 1, 1), Color(light, light, light, arrived));
    }

    if (favorite) {
        const Rect badge(art.right() - 40.0f, art.y + 10.0f, 30.0f, 30.0f);
        r_.roundedRect(badge, 15.0f, kInk.withAlpha(0.82f));
        r_.buttonGlyph(ButtonGlyph::Star, badge, kAccentBright);
    }
    if (entries > 1) {
        // More than one download of this game: how many.
        const std::string label = trf("{0} versions", std::to_string(entries));
        const float width = std::ceil(r_.measureText(label, 15)) + 20.0f;
        const Rect badge(art.x + 10.0f, art.bottom() - 36.0f, width, 26.0f);
        r_.roundedRect(badge, 13.0f, kInk.withAlpha(0.82f));
        r_.text(label, badge.x + 10.0f, ui::baseline(r_, badge.y + 13.0f, 15), 15, kAccentBright);
    }
    if (sweep >= 0.0f) {
        ui::glassSweep(r_, art, radius, sweep, kSweepIntensity * arrived);
    }
    if (focus > 0.02f) {
        ui::selectionReflection(r_, art, radius, reflection_, focus);
    }
}

void StoreView::drawCatalogueHints(StoreState& s)
{
    float x = kSafeX;
    if (!s.visible.empty()) {
        const auto pages = (s.visible.size() + StoreState::kGridPage - 1) / StoreState::kGridPage;
        const auto page = s.selected / StoreState::kGridPage + 1;
        x += r_.text(std::string(tr("Page")) + " " + std::to_string(page) + " / " + std::to_string(pages), x,
                     ui::baseline(r_, kHintCenter, kFontLabel), kFontLabel, kMuted) +
             32.0f;
    }
    ui::hintsFrom(r_, x, kHintTop, { { { Key::Touchpad }, tr(sortName(s.sort)) } }, kKeySize);
    ui::hintsUntil(r_, kContentRight, kHintTop,
                   { { { Key::Cross }, tr("Details") },
                     { { Key::Triangle }, tr("Search") },
                     { { Key::Square }, tr("Favorite") },
                     { { Key::L2, Key::R2 }, tr("Page") },
                     { { Key::Options }, tr("Settings") } },
                   kKeySize);
}

// -- Game page -------------------------------------------------------------------

void StoreView::drawGamePage(StoreState& s, float amount)
{
    const Game& game = *s.current();
    const GameText text = textOf(s, game);
    const Cover* cover = covers_.get(game, text.titleId, -1); // before any tile of the grid
    const float slide = (1.0f - amount) * 28.0f;
    r_.pushOpacity(amount);

    // Behind everything: the title's own background picture, dimmed for the
    // text; until it is there, the tile enlarged far past its resolution.
    const GameArtwork* scene = text.titleId.empty() || !s.officialArt ? nullptr : art_.get(text.titleId);
    s.setGameMedia(game.id, scene ? static_cast<int>(scene->screenshots.size()) : 0,
                   scene ? scene->preview : std::string());
    const bool haveBackground = scene && scene->background.valid();
    const float arrived = haveBackground ?
        clampf(static_cast<float>((art_.now() - scene->readyTime) / 0.35), 0.0f, 1.0f) : 0.0f;
    ui::background(r_, kGlow, 0.6f);
    if (cover && arrived < 1.0f) {
        r_.image(cover->texture, box(0, -420, kScreenW, kScreenW), Rect(0, 0, 1, 1), Color(1, 1, 1, 0.20f));
    }
    if (haveBackground) {
        r_.imageCover(scene->background, box(0, 0, kScreenW, kScreenH), Color(0.62f, 0.62f, 0.62f, arrived));
    }
    r_.gradientVertical(box(0, 0, kScreenW, kScreenH), Color(0, 0, 0, 0.25f), Color(0, 0, 0, 0.72f));
    if (haveBackground) {
        // Darker on the left, under the tile and the text.
        r_.gradientHorizontal(box(0, 0, kScreenW, kScreenH), Color(0, 0, 0, 0.45f * arrived), Color(0, 0, 0, 0.0f));
    }

    // On the left: the game's tile, the same picture as in the grid.
    const Rect art(150, 200 + slide, 420, 420);
    ui::shadow(r_, art, 26.0f, 1.0f);
    if (cover) {
        r_.image(cover->texture, art);
    } else {
        r_.roundedRect(art, 26.0f, Color(0.07f, 0.13f, 0.11f));
        r_.roundedOutline(art, 26.0f, 1.0f, kGlassBorder);
        if (covers_.loading(game.id)) spinner(art.x + art.w * 0.5f, art.y + art.h * 0.55f, 18);
        r_.textLines(text.title, art.x + 32.0f, art.y + 80.0f, kFontBody, kMuted, art.w - 64.0f, 6);
    }

    // The facts, the source and the download are those of the entry chosen
    // in the list of versions (the only one when the game has one).
    const Game& chosen = s.variant() ? *s.variant() : game;
    const std::vector<int>* group = s.currentGroup();
    const bool several = group && group->size() > 1;
    const bool favorite = group ? s.favoriteGroup(*group) : s.favorites.count(game.id) != 0;
    const auto overview = englishOverview(chosen.title);
    float lineY = art.bottom() + 52;
    if (!overview.summary.empty()) {
        r_.textLines(overview.summary, art.x, lineY, 22, kText, art.w, 5);
        lineY += 145;
    }
    auto detail = s.listingDetails.find(chosen.url);
    if (detail != s.listingDetails.end()) {
        const auto& d = detail->second;
        auto field = [&](const std::string& value) {
            if (value.empty() || lineY > kHintTop - 24) return;
            r_.textFit(value, art.x, lineY, kFontLabel, kMuted, art.w);
            lineY += 35;
        };
        std::string swarm;
        if (d.seeds >= 0) swarm = "Seeds " + std::to_string(d.seeds);
        if (d.leeches >= 0) swarm += (swarm.empty() ? "" : "   ") + std::string("Leechers ") + std::to_string(d.leeches);
        field(swarm);
        field(d.genres);
        field(d.publisher.empty() ? d.company : d.publisher);
        if (!d.screen.empty()) field("Text: " + d.screen);
        if (!d.audio.empty()) field("Audio: " + d.audio);
    }
    const float x = 660.0f;
    const float top = 240.0f + slide;
    r_.text(std::string(sourceName(sourceForURL(chosen.url))) + kDot + tr("GAME DETAILS"), x, top, kFontLabel,
            kAccentBright);
    r_.textLines(text.title, x, top + 78.0f, 56, kText, 1150.0f, 2);
    // What follows sits under the title, whether it took one line or two.
    const float below = top + (r_.measureText(text.title, 56) > 1150.0f ? 61.0f : 0.0f);

    const float chipY = below + 116.0f;
    float chipX = drawFacts(s, chosen, x, chipY);
    chipX += ui::chip(r_, chipX, chipY, "PlayStation 4", kMuted, Color(0, 0, 0, 0)) + 10.0f;
    if (favorite) {
        ui::chip(r_, chipX, chipY, tr("Favorite"), kInk, kAccentBright);
    }

    const int queued = s.matchingDownload(chosen);
    const bool downloadReady = s.canDownloadVersion();
    const char* action = tr(downloadReady && queued >= 0 ? "Go to downloads" : "Download");
    const char* versionAction = tr(downloadReady ? "Change version" : "Select version");
    const float versionY = below + 198.0f;
    if (several) {
        const auto version = describe(chosen).version;
        std::string source = sourceName(sourceForURL(chosen.url));
        if (!version.empty()) source += std::string(kDot) + "v" + version;
        const std::string value = downloadReady ? source : trf("{0} versions", std::to_string(group->size()));
        settingRow(box(x, versionY, 850.0f, 64.0f), versionAction, value, s.detailFocus == DetailFocus::Version);
    }
    const float buttonY = several ? versionY + 88.0f : versionY;
    const float buttonWidth = std::max(220.0f, r_.measureText(action, kFontBody) + 64.0f);
    const Rect download(x, buttonY, buttonWidth, 54.0f);
    const bool downloadFocused = downloadReady && s.detailFocus == DetailFocus::Download;
    if (downloadFocused && (queued < 0 || s.jobs[queued].state == DownloadState::Active ||
                            s.jobs[queued].state == DownloadState::Queued)) {
        const float pulse = 0.5f - 0.5f * std::cos(reflection_ * 6.2831853f);
        const float spread = 3.0f + 5.0f * pulse;
        r_.roundedOutline(download.inset(-spread), download.h * 0.5f + spread,
                          2.0f, kAccentBright.withAlpha(0.35f - 0.2f * pulse));
    }
    if (!downloadReady) r_.pushOpacity(0.45f);
    ui::pillButton(r_, download, action, kFontBody, downloadFocused);
    if (!downloadReady) r_.popOpacity();

    const float mediaY = std::max(buttonY + 116.0f, 660.0f);
    if (s.officialArt && !s.mediaCount() && art_.loadingMedia(text.titleId)) {
        spinner(x + 13, mediaY - 6, 10);
        r_.text(tr("Loading media…"), x + 43, mediaY, kFontLabel, kMuted);
    }
    if (scene && s.mediaCount() > 0) {
        r_.text(tr(s.screenshotCount > 0 ? "Screenshots" : "Trailer / preview"), x, mediaY, kFontLabel, kText);
        float shotX = x;
        for (int i = 0; i < s.screenshotCount; ++i) {
            const Rect image(shotX, mediaY + 15.0f, 240.0f, 135.0f);
            const bool focused = s.detailFocus == DetailFocus::Media && s.mediaSelected == i;
            r_.image(scene->screenshots[i].thumbnail, image);
            r_.roundedOutline(image, 14.0f, focused ? 3.0f : 1.0f, focused ? kAccentBright : kGlassBorder);
            shotX += 258.0f;
        }
        if (!s.trailerURL.empty()) {
            const bool focused = s.detailFocus == DetailFocus::Media && s.mediaSelected == s.screenshotCount;
            const Rect tile(shotX, mediaY + 15.0f, 240.0f, 135.0f);
            option(tile, tr("Play trailer"), kFontLabel, focused);
        }
    }
    const char* confirm = action;
    if (s.detailFocus == DetailFocus::Version) confirm = versionAction;
    else if (s.detailFocus == DetailFocus::Media)
        confirm = tr(s.mediaSelected < s.screenshotCount ? "View screenshot" : "Play trailer");
    std::vector<Hint> hints = {
        {{Key::Cross}, confirm},
        {{Key::Square}, favorite ? tr("Remove favorite") : tr("Favorite")},
    };
    if (!s.trailerURL.empty()) hints.push_back({{Key::L3}, tr("Play trailer")});
    hints.push_back({{Key::Circle}, tr("Back")});
    ui::hintsUntil(r_, kContentRight, kHintTop, hints, kKeySize);
    r_.popOpacity();
}

void StoreView::drawVersionPicker(const StoreState& s)
{
    const auto* group = s.currentGroup();
    if (!group || group->empty()) return;
    const int count = static_cast<int>(group->size());
    constexpr int kShown = 5;
    const int shown = std::min(kShown, count);
    const Rect frame = dialog(1170, 250.0f + shown * 70.0f, tr("Select version"));
    const int chosen = std::clamp(s.versionChoice, 0, count - 1);
    const int first = std::max(0, std::min(chosen - kShown / 2, count - kShown));
    for (int row = 0; row < shown; ++row) {
        const int place = first + row;
        const Game& game = s.games[(*group)[place]];
        const GameText text = describe(game);
        std::string label = sourceName(sourceForURL(game.url));
        label += kDot + (text.version.empty() ? std::string(tr("Version not given")) : "v" + text.version);
        if (!game.size.empty()) {
            if (game.size != "Size on page") label += kDot + tr(game.size);
        }
        settingRow(box(frame.x + 56.0f, frame.y + 120.0f + row * 70.0f, frame.w - 112.0f, 58.0f), label,
                   withoutEmptyBrackets(latinOnly(game.title)), place == chosen);
    }
    if (count > kShown) {
        textFitRight(std::to_string(chosen + 1) + " / " + std::to_string(count), frame.right() - 56.0f,
                     frame.bottom() - 48.0f, kFontSmall, kMuted, 230.0f);
    }
    dialogHints(frame, {{{Key::Cross}, tr("Select version")}, {{Key::Circle}, tr("Back")}});
}

// -- Download queue --------------------------------------------------------------

void StoreView::drawDownloads(StoreState& s)
{
    r_.text(tr("Downloads"), kSafeX, 224, 46, kText);
    r_.text(std::string(tr("TRANSFER QUEUE")) + kDot + tr("UP TO 2 ACTIVE DOWNLOADS"), kSafeX, 262, kFontSmall,
            kAccentBright);

    const int count = static_cast<int>(s.jobs.size());
    if (!count) {
        r_.textCentered(tr("Your download queue is empty"), kScreenW * 0.5f, 560, kFontTitle, kText);
        r_.textCentered(tr("Open a game and select Download to add its torrent."), kScreenW * 0.5f, 612, kFontBody,
                        kMuted);
    }
    const int start = s.jobSelected / 5 * 5;
    for (int i = start; i < std::min(start + 5, count); ++i) {
        const auto& job = s.jobs[i];
        const auto name = textOf(s, s.downloadListing(job)).title;
        drawJob(job, name.empty() ? tr("Download") : name, box(kSafeX, 292.0f + static_cast<float>(i - start) * 136.0f, kContentW, 122.0f),
                i == s.jobSelected);
    }

    float x = kSafeX;
    if (count > 5) {
        x += r_.text(std::to_string(s.jobSelected + 1) + " / " + std::to_string(count), x,
                     ui::baseline(r_, kHintCenter, kFontLabel), kFontLabel, kMuted) +
             32.0f;
    }
    r_.textFit(tr("New downloads: ") + tr(s.downloadLocationName()), x,
               ui::baseline(r_, kHintCenter, kFontLabel), kFontLabel, kDim, 420.0f);
    // What the controller does depends on the download under the cursor:
    // only a finished one can be installed.
    std::vector<Hint> hints;
    if (count) {
        const DownloadRow& job = s.jobs[s.jobSelected];
        const bool finished = job.state == DownloadState::Complete;
        if (!finished) {
            hints.push_back({ { Key::Cross }, tr("Pause / Resume") });
        } else {
            hints.push_back({ { Key::Triangle }, tr("Install") });
        }
        hints.push_back({ { Key::Square }, finished ? tr("Delete") : tr("Cancel") });
    }
    hints.push_back({ { Key::L3 }, "Worker" });
    hints.push_back({ { Key::Circle }, tr("Back") });
    ui::hintsUntil(r_, kContentRight, kHintTop, hints, kKeySize);
}

void StoreView::drawJob(const DownloadRow& job, const std::string& title, const Rect& rect, bool focused)
{
    const Color tone = stateColor(job.state);
    ui::surface(r_, rect, kRowRadius, focused);
    if (focused) {
        ui::selectionReflection(r_, rect, kRowRadius, reflection_, 1.0f);
    }
    const float left = rect.x + 28.0f;
    const float right = rect.right() - 28.0f;

    const float badge = ui::chipWidth(r_, tr(stateName(job.state)));
    ui::chip(r_, right - badge, rect.y + 14.0f, tr(stateName(job.state)), tone, Color(0, 0, 0, 0));
    r_.textFit(title, left, rect.y + 42.0f, 28, kText, rect.w - 56.0f - badge - 24.0f);

    const double fraction = job.total ? std::min(1., double(job.done) / job.total) : 0;
    ui::progressBar(r_, box(left, rect.y + 60.0f, rect.w - 56.0f, 8.0f), static_cast<float>(fraction), tone);

    // A transfer through a Debrid service has no peers to count.
    bool viaDebrid = false;
    for (int service = 1; service < kDebridCount; service++) {
        viaDebrid = viaDebrid || job.phase.find(debridName(service)) != std::string::npos;
    }
    const std::string stats = bytesText(job.done) + " / " + (job.total ? bytesText(job.total) : tr("size pending")) +
                              kDot + std::to_string(int(fraction * 100)) + "%" + kDot +
                              bytesText((uint64_t)job.rate) + "/s" +
                              (viaDebrid ? std::string() : kDot + trf("{0} peers", std::to_string(job.peers)));
    const float used = r_.textFit(stats, left, rect.y + 100.0f, kFontLabel, kText, 900.0f);
    const bool failed = !job.error.empty();
    textFitRight(trMessage(failed ? job.error : job.phase), right, rect.y + 100.0f, kFontSmall, failed ? kError : kMuted,
                 rect.w - 56.0f - used - 40.0f);
}

// -- Dialogs ---------------------------------------------------------------------

// Centred panel with its title; returns the frame for the caller's content.
Rect StoreView::dialog(float width, float height, const std::string& title)
{
    const Rect frame(std::floor((kScreenW - width) * 0.5f), std::floor((kScreenH - height) * 0.5f), width, height);
    ui::panel(r_, frame, kPanelRadius);
    r_.textFit(title, frame.x + 56.0f, frame.y + 80.0f, kFontTitle, kText, width - 112.0f);
    return frame;
}

void StoreView::dialogHints(const Rect& frame, const std::vector<Hint>& hints)
{
    ui::hintsFrom(r_, frame.x + 56.0f, frame.bottom() - 78.0f, hints, kKeySize);
}

// A choice of a dialog: white, like the active tab, when it has the cursor.
void StoreView::option(const Rect& rect, const std::string& label, int size, bool focused)
{
    r_.roundedRect(rect, kRowRadius, focused ? kFocusFill : kGlass);
    if (!focused) {
        r_.roundedOutline(rect, kRowRadius, 1.0f, kGlassBorder);
    }
    r_.textFit(label, rect.x + 26.0f, ui::baseline(r_, rect.y + rect.h * 0.5f, size), size, focused ? kInk : kText,
               rect.w - 52.0f);
}

void StoreView::drawSourcePicker(const StoreState& s)
{
    const float centerX = kScreenW * 0.5f;
    const float centerY = kScreenH * 0.5f;
    if (s.sourceLoading) {
        r_.rect(box(0, 0, kScreenW, kScreenH), Color(0.0f, 0.0f, 0.0f, 0.93f));
        spinner(centerX, centerY - 52.0f, 20);
        r_.textCentered(tr("Checking torrent link"), centerX, centerY + 25.0f, kFontTitle, kText);
        ui::hintsUntil(r_, kContentRight, kHintTop, {{{Key::Circle}, tr("Back")}}, kKeySize);
        return;
    }
    if (!s.sourceError.empty()) {
        r_.textCentered(trMessage(s.sourceError), centerX, centerY - 10.0f, kFontBody, kError);
        ui::hintsFrom(r_, centerX - ui::keyWidth(r_, Key::Circle, kKeySize) * 0.5f - 40.0f, centerY + 30.0f,
                      { { { Key::Circle }, tr("Back") } }, kKeySize);
        return;
    }
    const Rect frame = dialog(1100, 660, tr("Choose a torrent"));
    const float x = frame.x + 56.0f;
    const float width = frame.w - 112.0f;
    r_.textFit(describe(s.downloadGame).title, x, frame.y + 124.0f, kFontBody, kAccentBright, width);
    {
        const int start = s.sourceSelected / 5 * 5;
        for (int i = start; i < std::min(start + 5, int(s.sources.size())); ++i) {
            option(box(x, frame.y + 164.0f + static_cast<float>(i - start) * 74.0f, width, 62.0f), s.sources[i].title,
                   kFontBody, i == s.sourceSelected);
        }
    }
    dialogHints(frame, { { { Key::Cross }, tr("Add to downloads") }, { { Key::Circle }, tr("Back") } });
}

// On-screen keyboard, the fallback when the console's own cannot open.
void StoreView::drawKeyboard(const StoreState& s)
{
    const Rect frame = dialog(1240, 660, tr("Search games"));
    const float x = frame.x + 56.0f;
    const float width = frame.w - 112.0f;
    const Rect field(x, frame.y + 112.0f, width, 64.0f);
    ui::surface(r_, field, 16.0f);
    r_.textFit(s.searchDraft + "_", field.x + 22.0f, ui::baseline(r_, field.y + 32.0f, 32), 32, kText, width - 44.0f);

    const float keyW = (width - 9.0f * 12.0f) / 10.0f;
    for (int i = 0; i < 40; ++i) {
        const std::string label = i < 26   ? std::string(1, static_cast<char>('A' + i))
                                  : i < 36 ? std::string(1, static_cast<char>('0' + i - 26))
                                  : i == 36 ? tr("Space")
                                  : i == 37 ? tr("Back")
                                  : i == 38 ? tr("Clear")
                                            : tr("Done");
        const Rect keyRect(x + static_cast<float>(i % 10) * (keyW + 12.0f),
                           frame.y + 200.0f + static_cast<float>(i / 10) * 76.0f, keyW, 64.0f);
        const bool focused = i == s.key;
        const int size = i < 36 ? 30 : kFontLabel;
        r_.roundedRect(keyRect, 14.0f, focused ? kFocusFill : kGlass);
        r_.textCentered(label, keyRect.x + keyW * 0.5f, ui::baseline(r_, keyRect.y + 32.0f, size), size,
                        focused ? kInk : kText);
    }
    dialogHints(frame, { { { Key::Cross }, tr("Select") }, { { Key::Circle }, tr("Close") } });
}

void StoreView::drawInstallPicker(const StoreState& s)
{
    const Rect frame = dialog(1300, 780, s.installJob ? tr("Install downloaded packages") : "Install download worker");
    const float x = frame.x + 56.0f;
    const float width = frame.w - 112.0f;
    r_.textFit(tr("Choose a package. Install the base game before updates or DLC."), x, frame.y + 122.0f, kFontLabel,
               kMuted, width);
    if (s.installLoading) {
        spinner(x + 15, frame.y + 220, 12);
        r_.text(tr("Checking downloaded PKG files..."), x + 46, frame.y + 230.0f, kFontBody, kText);
    } else if (s.packages.empty()) {
        r_.textLines(trMessage(s.installError), x, frame.y + 230.0f, kFontBody, kError, width, 6);
    } else {
        const int start = s.installSelected / 4 * 4;
        for (int i = start; i < std::min(start + 4, int(s.packages.size())); ++i) {
            const InstallPackage& package = s.packages[i];
            const Rect row(x, frame.y + 152.0f + static_cast<float>(i - start) * 118.0f, width, 106.0f);
            const bool focused = i == s.installSelected;
            ui::surface(r_, row, kRowRadius, focused);
            if (focused) {
                ui::selectionReflection(r_, row, kRowRadius, reflection_, 1.0f);
            }
            r_.textFit(package.name, row.x + 26.0f, row.y + 38.0f, kFontBody, kText, width - 52.0f);
            r_.textFit(package.titleId + kDot + bytesText(package.bytes), row.x + 26.0f, row.y + 68.0f, kFontSmall,
                       kAccentBright, width - 52.0f);
            r_.textFit(installStatusText(package), row.x + 26.0f, row.y + 94.0f, kFontSmall, kMuted, width - 52.0f);
        }
    }
    if (!s.installError.empty() && !s.packages.empty()) {
        r_.textLines(trMessage(s.installError), x, frame.bottom() - 132.0f, kFontSmall, kError, width, 2);
    }
    dialogHints(frame, { { { Key::Cross }, tr("Install selected package") }, { { Key::Circle }, tr("Close") } });
}

void StoreView::settingRow(const Rect& rect, const std::string& label, const std::string& value, bool focused)
{
    r_.roundedRect(rect, kRowRadius, focused ? kFocusFill : kGlass);
    if (!focused) {
        r_.roundedOutline(rect, kRowRadius, 1.0f, kGlassBorder);
    }
    const float baseline = ui::baseline(r_, rect.y + rect.h * 0.5f, kFontBody);
    const float labelWidth = r_.text(label, rect.x + 26.0f, baseline, kFontBody, focused ? kInk : kText, rect.w * 0.62f);
    const float valueRight = rect.right() - 26.0f;
    textFitRight(value, valueRight, ui::baseline(r_, rect.y + rect.h * 0.5f, kFontLabel), kFontLabel,
                 focused ? Color(kInk.r, kInk.g, kInk.b, 0.7f) : kMuted, rect.w - 52.0f - labelWidth - 40.0f);
}

void StoreView::drawSettings(const StoreState& s)
{
    const Rect frame = dialog(1160, 788, tr("Settings"));
    const float x = frame.x + 56.0f;
    const float width = frame.w - 112.0f;
    const std::string& key = s.debridKey();
    const std::string keyText = !s.debrid      ? tr("Choose a service first")
                                : key.empty()  ? tr("Not set")
                                               : tr("Saved") + std::string(kDot) + "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" +
                                                    key.substr(key.size() > 4 ? key.size() - 4 : 0);
    const std::string rows[SETTING_COUNT][2] = {
        { tr("Catalog source"), tr(sourceName(s.sourceFilter)) },
        { tr("Sort games by"), tr(sortName(s.sort)) },
        { tr("Download location"), tr(s.downloadLocationName()) },
        { tr("Debrid service"), tr(debridName(s.debrid)) },
        { tr("Debrid API key"), keyText },
        { tr("Official artwork"), s.officialArt ? tr("On  \xC2\xB7  official icons") : tr("Off  \xC2\xB7  source covers") },
        { tr("Refresh catalogue"), trMessage(s.status) },
    };
    for (int i = 0; i < SETTING_COUNT; ++i) {
        settingRow(box(x, frame.y + 120.0f + static_cast<float>(i) * 74.0f, width, 62.0f), rows[i][0], rows[i][1],
                   i == s.settingsSelected);
    }
    r_.textRight("NULL_store v01.01", frame.right() - 56.0f, ui::baseline(r_, frame.bottom() - 59.0f, kFontSmall),
                 kFontSmall, kDim);
    dialogHints(frame, { { { Key::Cross }, s.settingsSelected == SETTING_ARTWORK      ? tr("Turn on / off")
                                           : s.settingsSelected == SETTING_REFRESH    ? tr("Refresh now")
                                           : s.settingsSelected == SETTING_DEBRID_KEY ? tr("Enter key")
                                                                                      : tr("Change") },
                         { { Key::Circle }, tr("Close") } });
}

void StoreView::drawLocationPicker(const StoreState& s)
{
    const Rect frame = dialog(1100, 840, tr("Choose download location"));
    const float x = frame.x + 56.0f;
    const float width = frame.w - 112.0f;
    r_.textFit(tr("New downloads will use this location. Existing jobs stay where they are."), x, frame.y + 120.0f,
               kFontSmall, kMuted, width);
    const int start = s.downloadLocationSelected / 8 * 8;
    for (int i = start; i < std::min(start + 8, int(s.folderChoices.size())); ++i)
        option(box(x, frame.y + 148 + (i - start) * 62, width, 54), s.folderChoices[i].first,
               kFontLabel, i == s.downloadLocationSelected);
    r_.textFit(s.browseFolder, x, frame.y + 684, kFontBody, kDim, width);
    dialogHints(frame, { { { Key::Cross }, tr("Use location") }, { { Key::Circle }, tr("Back") } });
}

void StoreView::drawQr(const QrSymbol& code, float x, float y, int boxSize)
{
    if (!code.size) return;
    const int scale = code.scaleFor(boxSize);
    const float side = float(code.pixelsFor(boxSize));
    r_.rect(box(x, y, side, side), Color::hex(0xffffff));
    for (int row = 0; row < code.size; ++row)
        for (int col = 0; col < code.size; ++col)
            if (code.dark(col, row))
                r_.rect(box(x + (4 + col) * scale, y + (4 + row) * scale, scale, scale), Color::hex(0));
}

void StoreView::drawFullscreen(const Texture& texture, const std::string& caption)
{
    r_.rect(box(0, 0, kScreenW, kScreenH), Color::hex(0));
    const float scale = std::min(kScreenW / texture.width(), kScreenH / texture.height());
    const float width = texture.width() * scale;
    const float height = texture.height() * scale;
    r_.image(texture, box((kScreenW - width) * 0.5f, (kScreenH - height) * 0.5f, width, height));
    r_.gradientVertical(box(0, kScreenH - 150.0f, kScreenW, 150.0f), Color(0, 0, 0, 0), Color(0, 0, 0, 0.8f));
    if (!caption.empty()) r_.text(caption, kSafeX, ui::baseline(r_, kHintCenter, kFontLabel), kFontLabel, kText);
    ui::hintsUntil(r_, kContentRight, kHintTop, {{{Key::Circle}, tr("Back")}}, kKeySize);
}

void StoreView::drawScreenshot(StoreState& s)
{
    const Game* game = s.current();
    if (!game) return;
    const auto id = textOf(s, *game).titleId;
    const auto* art = art_.get(id);
    if (!art || s.mediaSelected >= static_cast<int>(art->screenshots.size())) return;
    const auto caption = std::to_string(s.mediaSelected + 1) + " / " + std::to_string(art->screenshots.size()) +
        (art->screenshots.size() > 1 ? std::string(kDot) + tr("Left / Right") : std::string());
    drawFullscreen(art->screenshots[s.mediaSelected].image, caption);
}

void StoreView::drawTrailer(const StoreState& s, TrailerState status, int progress)
{
    const bool downloading = status == TrailerState::DownloadingFile;
    const bool busy = downloading || status == TrailerState::Buffering || status == TrailerState::Rebuffering ||
                      status == TrailerState::Playing;
    const bool useQr = offerTrailerLink(status) && !s.trailerURL.empty() && s.trailerCode.size > 0;
    const Rect frame = dialog(840, useQr ? 640.0f : 320.0f, tr("Trailer / preview"));
    std::string label;
    switch (status) {
    case TrailerState::Buffering:
    case TrailerState::Rebuffering:
    case TrailerState::Playing: label = tr("Buffering trailer…"); break;
    case TrailerState::DownloadingFile:
        label = tr(progress == 100 ? "Preparing playback…" : "This source requires a full download.");
        if (progress >= 0 && progress < 100) label += "  " + std::to_string(progress) + "%";
        break;
    case TrailerState::LinkGone: label = tr("The trailer link is no longer available."); break;
    case TrailerState::ConnectionFailed: label = tr("The PS4 could not load the trailer link."); break;
    case TrailerState::Unsupported: label = tr("This trailer is unsupported on this PS4."); break;
    case TrailerState::PlayerFailed: label = tr("This trailer could not play on this PS4."); break;
    case TrailerState::Finished: label = tr("Trailer finished"); break;
    }
    if (busy) spinner(frame.x + 71, frame.y + 131, 12);
    r_.textLines(label, frame.x + (busy ? 110.0f : 56.0f), frame.y + 140.0f,
                 kFontBody, kText, frame.w - (busy ? 166.0f : 112.0f), 2);
    if (useQr) {
        const int side = s.trailerCode.pixelsFor(300);
        drawQr(s.trailerCode, frame.x + (frame.w - side) * 0.5f, frame.y + 198.0f, 300);
        r_.textFit(tr("Open this link on your phone"), frame.x + 56.0f, frame.y + 532.0f, kFontBody, kMuted, frame.w - 112.0f);
    }
    dialogHints(frame, {{{Key::Circle}, tr("Back")}});
}

void StoreView::drawSourceFilterPicker(const StoreState& s)
{
    const Rect frame = dialog(1200, 650, tr("Catalog source"));
    const int start = (s.sourceChoice / 5) * 5;
    const int count = int(sourceSnapshot()->items.size()) + 1;
    for (int i = start; i < std::min(count, start + 5); ++i) {
        option(box(frame.x + 56, frame.y + 122 + float(i - start) * 64, 560, 54),
               tr(sourceName(i)), kFontBody, i == s.sourceChoice);
    }
    const float x = frame.right() - 390, y = frame.y + 128;
    r_.textFit("Add a source from your phone", x, y - 24, kFontLabel, kDim, 340);
    drawQr(s.setupCode, x, y, 280);
    r_.textFit(s.setupURL.empty() ? "Phone setup unavailable" : s.setupURL,
               frame.x + 56, frame.bottom() - 155, kFontBody, kText, frame.w - 112);
    dialogHints(frame, { { { Key::Cross }, tr("Choose source") }, { { Key::Square }, s.separateListings ? "Group listings" : "Separate listings" }, { { Key::Circle }, tr("Back") } });
}

void StoreView::drawSortPicker(const StoreState& s)
{
    const Rect frame = dialog(900, 560, tr("Sort games by"));
    for (int i = 0; i < 4; i++) {
        option(box(frame.x + 56.0f, frame.y + 122.0f + static_cast<float>(i) * 76.0f, frame.w - 112.0f, 64.0f),
               tr(sortName(i)), kFontBody, i == s.sortChoice);
    }
    dialogHints(frame, { { { Key::Cross }, tr("Choose order") }, { { Key::Circle }, tr("Back") } });
}

void StoreView::drawDebridPicker(const StoreState& s)
{
    const Rect frame = dialog(1100, 740, tr("Debrid service"));
    const float x = frame.x + 56.0f;
    const float width = frame.w - 112.0f;
    r_.textFit(tr("Get the API key of your account at the address shown, then enter it in Settings."), x,
               frame.y + 120.0f, kFontSmall, kMuted, width);
    for (int i = 0; i < kDebridCount; i++) {
        settingRow(box(x, frame.y + 148.0f + static_cast<float>(i) * 70.0f, width, 58.0f), tr(debridName(i)),
                   tr(debridKeyPage(i)), i == s.debridChoice);
    }
    dialogHints(frame, { { { Key::Cross }, tr("Choose service") }, { { Key::Circle }, tr("Back") } });
}

// A message for the user, in the top left corner (over the store's name) for
// a few seconds; one that stays (no controller) stays until it is resolved.
void StoreView::drawNotice(StoreState& s)
{
    if (s.notice != noticeText_) {
        noticeText_ = s.notice;
        noticeAge_ = 0.0f;
        notice_.snap(0.0f);
    }
    if (s.notice.empty()) {
        return;
    }
    noticeAge_ += dt_;
    notice_.set(s.noticeStays || noticeAge_ < kNoticeSeconds ? 1.0f : 0.0f);
    const float shown = notice_.update(dt_);
    if (noticeAge_ >= kNoticeSeconds && !s.noticeStays && shown < 0.01f) {
        s.notice.clear();
        noticeText_.clear();
        return;
    }
    const std::string message = trMessage(s.notice);
    const float width = std::min(r_.measureText(message, kFontBody), 1500.0f);
    const Rect toast(kSafeX - 6.0f, kTabTop - (1.0f - shown) * 16.0f, width + 84.0f, kTabHeight);
    r_.pushOpacity(shown);
    ui::shadow(r_, toast, 30.0f, 0.8f);
    r_.roundedRect(toast, 30.0f, kPanel);
    r_.roundedOutline(toast, 30.0f, 1.0f, kGlassBorder);
    r_.roundedRect(box(toast.x + 24.0f, toast.y + toast.h * 0.5f - 6.0f, 12.0f, 12.0f), 6.0f, kAccentBright);
    r_.textFit(message, toast.x + 52.0f, ui::baseline(r_, toast.y + toast.h * 0.5f, kFontBody), kFontBody, kText,
               width + 2.0f);
    r_.popOpacity();
}

} // namespace ns
