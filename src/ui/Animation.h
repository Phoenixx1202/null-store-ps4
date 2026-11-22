#pragma once

#include <cstdlib> // must precede <cmath> with the toolchain's libc++ (::abs)
#include <cmath>

namespace ns {

// Exponential approach towards a target: smooth, never overshoots and copes
// with targets that change mid-flight (the focus moving quickly). Frame-rate
// independent: `dt` is in seconds.
struct Smooth {
    float value = 0.0f;
    float target = 0.0f;
    float speed = 12.0f; // higher = snappier

    Smooth() = default;
    Smooth(float initial, float rate) : value(initial), target(initial), speed(rate) {}

    void snap(float v) { value = target = v; }
    void set(float v) { target = v; }

    float update(float dt)
    {
        value += (target - value) * (1.0f - std::exp(-speed * dt));
        if (std::fabs(target - value) < 0.0005f) {
            value = target;
        }
        return value;
    }
};

} // namespace ns
