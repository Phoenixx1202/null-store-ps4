#pragma once

#include <string>
#include <vector>

#include "graphics/Renderer.h"

// -----------------------------------------------------------------------------
// Stateless drawing helpers shared by the screens: the backdrop, glass
// surfaces, controller keys and their hints, chips, progress bars.
// -----------------------------------------------------------------------------
namespace ns::ui {

// A button of the controller, drawn as a glass key: face buttons are circles
// with their symbol, the others are pills with their name.
enum class Key { Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3, Options, Touchpad };

float keyWidth(Renderer& r, Key key, float size);
// Draws the key with its top-left at (x, y); returns its width.
float key(Renderer& r, Key key, float x, float y, float size);

// One entry of a hint bar: the key (or pair of keys) and what it does.
struct Hint {
    std::vector<Key> keys;
    std::string label;
};

float hintsWidth(Renderer& r, const std::vector<Hint>& hints, float size);
// Draws the hints from `x` to the right; returns the x after the last one.
float hintsFrom(Renderer& r, float x, float y, const std::vector<Hint>& hints, float size);
// Draws the hints so the last one ends at `right`.
void hintsUntil(Renderer& r, float right, float y, const std::vector<Hint>& hints, float size);

// Full-screen backdrop: a dark vertical gradient with a soft pool of light.
void background(Renderer& r, const Color& glow, float strength = 1.0f);

// Translucent resting surface, brighter when focused.
void surface(Renderer& r, const Rect& rect, float radius, bool focused = false);
// Opaque dark panel of dialogs, with a hairline border and a shadow.
void panel(Renderer& r, const Rect& rect, float radius);
// Pill that turns white when focused; draws its label centred.
void pillButton(Renderer& r, const Rect& rect, const std::string& label, int size, bool focused);

void shadow(Renderer& r, const Rect& rect, float radius, float strength);
// What marks the item under the cursor: a fine white outline with a highlight
// that travels round it. `phase` in [0, 1) is where the highlight is.
void selectionReflection(Renderer& r, const Rect& rect, float radius, float phase, float strength);
// A diagonal shine crossing a rounded picture from left to right as
// `progress` goes from 0 to 1.
void glassSweep(Renderer& r, const Rect& rect, float radius, float progress, float intensity);
// How long a selection reflection takes to go round, in seconds.
constexpr float kReflectionSeconds = 2.6f;

// Small rounded label; returns its width. `fill` alpha 0 draws an outline chip.
float chip(Renderer& r, float x, float y, const std::string& label, const Color& text, const Color& fill);
float chipWidth(Renderer& r, const std::string& label);

void progressBar(Renderer& r, const Rect& rect, float fraction, const Color& fill);

// Baseline that centres text of `size` vertically on `centerY`.
float baseline(Renderer& r, float centerY, int size);

} // namespace ns::ui
