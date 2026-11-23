#pragma once

#include <array>
#include <utility>

#include "graphics/Image.h"

namespace ns {

// Shared by the decoder, audio output and render loop. The caller holds
// the playback lock while reading or changing the queue and its clock.
class TrailerTimeline {
public:
    static constexpr size_t startupFrames = 10;
    static constexpr size_t capacity = 16;
    bool full() const { return count_ == capacity; }
    bool empty() const { return count_ == 0; }
    bool started() const { return start_ != 0; }
    bool buffering() const { return pausedAt_ != 0; }

    void reset() { *this = {}; }

    void push(Image& image, int64_t timeUs, uint64_t now) {
        auto& frame = frames_[(head_ + count_) % capacity];
        std::swap(frame.image, image);
        frame.timeUs = timeUs;
        ++count_;
        if (count_ >= startupFrames) {
            resume(now);
            start(now);
        }
    }

    void start(uint64_t now) {
        if (count_ && !started()) start_ = now;
    }

    void finish(uint64_t now) {
        draining_ = true;
        resume(now);
        start(now);
    }

    int64_t playhead(uint64_t now) const {
        if (!start_ || pausedAt_) return -1;
        return static_cast<int64_t>(now - start_);
    }

    bool present(uint64_t now, Image& image) {
        const int64_t timeUs = playhead(now);
        bool ready = false;
        while (count_ && frames_[head_].timeUs <= timeUs) {
            if (ready) ++skipped_;
            lastShownUs_ = frames_[head_].timeUs;
            std::swap(image, frames_[head_].image);
            head_ = (head_ + 1) % capacity;
            --count_;
            ready = true;
        }
        return ready;
    }

    void checkForGap(uint64_t now) {
        constexpr int64_t maxFrameGap = 100000;
        if (!draining_ && empty() && lastShownUs_ >= 0 && playhead(now) - lastShownUs_ > maxFrameGap) {
            playheadAtPause_ = playhead(now);
            pausedAt_ = now;
            ++gaps_;
        }
    }

    unsigned skipped() const { return skipped_; }
    unsigned gaps() const { return gaps_; }

private:
    void resume(uint64_t now) {
        if (pausedAt_) {
            start_ += now - pausedAt_;
            // Resume at the next queued frame when its timestamp fell during
            // the gap. The renderer and audio keep using the same clock.
            if (count_ && frames_[head_].timeUs < playheadAtPause_)
                start_ += playheadAtPause_ - frames_[head_].timeUs;
            pausedAt_ = 0;
        }
    }

    struct Frame { Image image; int64_t timeUs = 0; };
    std::array<Frame, capacity> frames_;
    size_t head_ = 0, count_ = 0;
    uint64_t start_ = 0, pausedAt_ = 0;
    int64_t lastShownUs_ = -1, playheadAtPause_ = 0;
    bool draining_ = false;
    unsigned skipped_ = 0, gaps_ = 0;
};

} // namespace ns
