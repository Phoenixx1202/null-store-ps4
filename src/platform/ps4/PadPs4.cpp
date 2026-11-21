// DualShock 4 backend (libScePad + libSceUserService).
#include <cstring>

#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include "core/Log.h"
#include "input/Gamepad.h"

namespace ns::input {

namespace {

float normalizeAxis(uint8_t raw)
{
    const float v = (static_cast<float>(raw) - 128.0f) / 127.0f;
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

class Ps4PadBackend final : public PadBackend {
public:
    bool init() override
    {
        // The startup code has loaded libScePad and libSceUserService
        // (src/startup_ps4.h); calling into them before that is fatal.
        const int32_t padResult = scePadInit();
        if (padResult != 0) {
            NS_LOG_WARN("scePadInit returned 0x%08X", static_cast<unsigned>(padResult));
        }
        OrbisUserServiceInitializeParams params;
        memset(&params, 0, sizeof(params));
        params.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
        const int32_t userResult = sceUserServiceInitialize(&params);
        if (userResult != 0) {
            NS_LOG_WARN("sceUserServiceInitialize returned 0x%08X", static_cast<unsigned>(userResult));
        }
        open();
        return true;
    }

    void shutdown() override
    {
        if (handle_ >= 0) {
            scePadClose(handle_);
            handle_ = -1;
        }
    }

    bool read(RawPadState& out) override
    {
        OrbisPadData data;
        memset(&data, 0, sizeof(data));
        if (handle_ < 0 || scePadReadState(handle_, &data) < 0 || !data.connected) {
            // The signed-in user or the controller may have changed: look
            // again every couple of seconds.
            if (++missedReads_ % 120 == 0) {
                open();
            }
            return false;
        }
        missedReads_ = 0;
        out.connected = true;

        const uint32_t b = data.buttons;
        uint32_t bits = 0;
        if (b & ORBIS_PAD_BUTTON_UP) bits |= BtnUp;
        if (b & ORBIS_PAD_BUTTON_DOWN) bits |= BtnDown;
        if (b & ORBIS_PAD_BUTTON_LEFT) bits |= BtnLeft;
        if (b & ORBIS_PAD_BUTTON_RIGHT) bits |= BtnRight;
        if (b & ORBIS_PAD_BUTTON_CROSS) bits |= BtnCross;
        if (b & ORBIS_PAD_BUTTON_CIRCLE) bits |= BtnCircle;
        if (b & ORBIS_PAD_BUTTON_TRIANGLE) bits |= BtnTriangle;
        if (b & ORBIS_PAD_BUTTON_SQUARE) bits |= BtnSquare;
        if (b & ORBIS_PAD_BUTTON_L1) bits |= BtnL1;
        if (b & ORBIS_PAD_BUTTON_R1) bits |= BtnR1;
        if (b & ORBIS_PAD_BUTTON_L2) bits |= BtnL2;
        if (b & ORBIS_PAD_BUTTON_R2) bits |= BtnR2;
        if (b & ORBIS_PAD_BUTTON_L3) bits |= BtnL3;
        if (b & ORBIS_PAD_BUTTON_R3) bits |= BtnR3;
        if (b & ORBIS_PAD_BUTTON_OPTIONS) bits |= BtnOptions;
        if (b & ORBIS_PAD_BUTTON_TOUCH_PAD) bits |= BtnTouchpad;
        out.buttons = bits;
        out.leftX = normalizeAxis(data.leftStick.x);
        out.leftY = normalizeAxis(data.leftStick.y);
        return true;
    }

    int userId() const override { return userId_; }

private:
    void open()
    {
        sceUserServiceGetInitialUser(&userId_);
        if (handle_ >= 0) {
            scePadClose(handle_);
        }
        handle_ = scePadOpen(userId_, 0, 0, nullptr);
        if (handle_ < 0 && !loggedOpenError_) {
            NS_LOG_WARN("scePadOpen(user=0x%X) failed: 0x%08X", static_cast<unsigned>(userId_),
                        static_cast<unsigned>(handle_));
            loggedOpenError_ = true;
        }
    }

    int32_t handle_ = -1;
    int32_t userId_ = -1;
    unsigned missedReads_ = 0;
    bool loggedOpenError_ = false;
};

} // namespace

std::unique_ptr<PadBackend> createPadBackend()
{
    return std::make_unique<Ps4PadBackend>();
}

} // namespace ns::input
