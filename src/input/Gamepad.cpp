#include "input/Gamepad.h"

#include "core/Log.h"

namespace ns::input {

namespace {

constexpr float kStickThreshold = 0.57f;
constexpr double kRepeatDelay = 0.40;    // before a held direction starts repeating
constexpr double kRepeatInterval = 0.117; // then about eight steps per second

} // namespace

Gamepad::Gamepad() = default;

Gamepad::~Gamepad()
{
    shutdown();
}

bool Gamepad::init()
{
    backend_ = createPadBackend();
    if (!backend_ || !backend_->init()) {
        NS_LOG_ERROR("Gamepad backend initialization failed");
        backend_.reset();
        return false;
    }
    return true;
}

void Gamepad::shutdown()
{
    if (backend_) {
        backend_->shutdown();
        backend_.reset();
    }
}

int Gamepad::userId() const
{
    return backend_ ? backend_->userId() : -1;
}

void Gamepad::update(double now)
{
    RawPadState raw;
    const bool connected = backend_ && backend_->read(raw) && raw.connected;
    if (connected != connected_) {
        NS_LOG_INFO("Controller %s", connected ? "connected" : "disconnected");
        connected_ = connected;
    }
    pressed_ = 0;
    if (!connected) {
        held_ = 0;
        return;
    }

    uint32_t buttons = raw.buttons;
    if (raw.leftX < -kStickThreshold) buttons |= BtnLeft;
    if (raw.leftX > kStickThreshold) buttons |= BtnRight;
    if (raw.leftY < -kStickThreshold) buttons |= BtnUp;
    if (raw.leftY > kStickThreshold) buttons |= BtnDown;

    if (waitRelease_) {
        held_ = buttons;
        if (buttons == 0) {
            waitRelease_ = false;
        }
        return;
    }

    pressed_ = buttons & ~held_;
    if (buttons != held_) {
        directionSince_ = now;
        lastRepeat_ = now;
    } else if ((buttons & kDirections) && now - directionSince_ >= kRepeatDelay &&
               now - lastRepeat_ >= kRepeatInterval) {
        pressed_ |= buttons & kDirections;
        lastRepeat_ = now;
    }
    held_ = buttons;
}

} // namespace ns::input
