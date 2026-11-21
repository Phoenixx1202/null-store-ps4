#pragma once

#include <cstdint>
#include <memory>

// -----------------------------------------------------------------------------
// DualShock 4 input.
//
// A platform backend (libScePad on the PS4) reports the raw state; the Gamepad
// adds edge detection and key-repeat for the directions, so the application
// never looks at raw button masks. The left stick doubles as the d-pad.
// -----------------------------------------------------------------------------
namespace ns::input {

enum Button : uint32_t {
    BtnUp = 1u << 0,
    BtnDown = 1u << 1,
    BtnLeft = 1u << 2,
    BtnRight = 1u << 3,
    BtnCross = 1u << 4,
    BtnCircle = 1u << 5,
    BtnTriangle = 1u << 6,
    BtnSquare = 1u << 7,
    BtnL1 = 1u << 8,
    BtnR1 = 1u << 9,
    BtnL2 = 1u << 10,
    BtnR2 = 1u << 11,
    BtnL3 = 1u << 12,
    BtnR3 = 1u << 13,
    BtnOptions = 1u << 14,
    BtnTouchpad = 1u << 15,
};

constexpr uint32_t kDirections = BtnUp | BtnDown | BtnLeft | BtnRight;

struct RawPadState {
    bool connected = false;
    uint32_t buttons = 0; // Button bits
    float leftX = 0.0f;   // -1..1
    float leftY = 0.0f;   // -1..1 (positive = down)
};

class PadBackend {
public:
    virtual ~PadBackend() = default;
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    // False while no controller answers; the backend keeps looking for one.
    virtual bool read(RawPadState& out) = 0;
    // The signed-in user the controller belongs to (-1 when unknown).
    virtual int userId() const = 0;
};

// Implemented by the platform layer.
std::unique_ptr<PadBackend> createPadBackend();

class Gamepad {
public:
    Gamepad();
    ~Gamepad();

    Gamepad(const Gamepad&) = delete;
    Gamepad& operator=(const Gamepad&) = delete;

    bool init();
    void shutdown();

    // Reads the controller; call once per frame.
    void update(double nowSeconds);

    bool connected() const { return connected_; }
    int userId() const;

    // Went down this frame. Directions also repeat while they stay held.
    bool pressed(Button button) const { return (pressed_ & button) != 0; }
    bool held(Button button) const { return (held_ & button) != 0; }
    uint32_t heldMask() const { return held_; }

    // Ignores everything until all buttons have been let go (after a system
    // dialog closes, so the button that dismissed it does not act in the app).
    void ignoreUntilReleased() { waitRelease_ = true; }

private:
    std::unique_ptr<PadBackend> backend_;
    bool connected_ = false;
    bool waitRelease_ = false;
    uint32_t held_ = 0;
    uint32_t pressed_ = 0;
    double directionSince_ = 0.0;
    double lastRepeat_ = 0.0;
};

} // namespace ns::input
