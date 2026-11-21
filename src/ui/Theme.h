#pragma once

#include "core/Geometry.h"

// -----------------------------------------------------------------------------
// The store's look: colours, type sizes and TV-safe layout metrics, in 1080p
// pixels. Layout code refers to these names only, so the look can change here
// without touching the screens.
// -----------------------------------------------------------------------------
namespace ns::theme {

// Screen / safe area ------------------------------------------------------------
constexpr float kScreenW = 1920.0f;
constexpr float kScreenH = 1080.0f;
constexpr float kSafeX = 96.0f;
constexpr float kContentW = kScreenW - 2.0f * kSafeX;
constexpr float kContentRight = kScreenW - kSafeX;

// Colours -------------------------------------------------------------------------
constexpr Color kBackground = Color::hex(0x050c0a);
constexpr Color kText = Color::hex(0xffffff);
constexpr Color kMuted = Color::hex(0xb0c7bc);
constexpr Color kDim = Color::hex(0x71877c);
constexpr Color kAccent = Color::hex(0x31b878);
constexpr Color kAccentBright = Color::hex(0x5ef0a8);
constexpr Color kInk = Color::hex(0x0a1a14);              // text on a white (focused) control
constexpr Color kGlow = Color(0.16f, 0.72f, 0.42f);       // light behind the content
constexpr Color kGlass = Color(1.0f, 1.0f, 1.0f, 0.07f);  // resting surface
constexpr Color kGlassBorder = Color(1.0f, 1.0f, 1.0f, 0.14f);
constexpr Color kGlassFocus = Color(1.0f, 1.0f, 1.0f, 0.16f);
constexpr Color kFocusFill = Color(1.0f, 1.0f, 1.0f, 0.92f); // focused pill
constexpr Color kKeyFill = Color(1.0f, 1.0f, 1.0f, 0.12f);
constexpr Color kKeyBorder = Color(1.0f, 1.0f, 1.0f, 0.16f);
constexpr Color kPanel = Color(0.035f, 0.085f, 0.066f, 0.96f); // dialogs
constexpr Color kScrim = Color(0.0f, 0.0f, 0.0f, 0.62f);       // dim behind a dialog
constexpr Color kError = Color::hex(0xff8f8f);
constexpr Color kWarning = Color::hex(0xfbbf24);

// Type sizes ----------------------------------------------------------------------
constexpr int kFontDisplay = 52;
constexpr int kFontTitle = 40;
constexpr int kFontBody = 26;
constexpr int kFontLabel = 22;
constexpr int kFontSmall = 19;

// Tab bar and hint bar (the shape of the console's own shell) ----------------------
constexpr float kKeySize = 38.0f;
constexpr float kTabTop = 66.0f;
constexpr float kTabHeight = 62.0f;
constexpr float kTabWidth = 194.0f;
constexpr float kTabPitch = 212.0f;
constexpr float kTabFirstX = 510.0f;
constexpr float kHintTop = 998.0f;

// Catalogue grid: eight square tiles across the content width, three rows in view --
constexpr int kGridColumns = 8;
constexpr int kGridRows = 3;
constexpr float kCardW = 195.0f;
constexpr float kCardH = 195.0f;
constexpr float kCardGapX = 24.0f;
constexpr float kCardPitchY = 219.0f;
constexpr float kCardRadius = 12.0f;
constexpr float kCardFocusScale = 1.08f;
constexpr float kGridTop = 346.0f;

constexpr float kPanelRadius = 26.0f;
constexpr float kRowRadius = 18.0f;

// Motion (exponential approach rates, 1/s) ----------------------------------------
constexpr float kFocusSpeed = 16.0f;
constexpr float kScrollSpeed = 12.0f;
constexpr float kFadeSpeed = 9.0f;

} // namespace ns::theme
