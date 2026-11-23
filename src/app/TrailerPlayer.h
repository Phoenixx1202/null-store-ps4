#pragma once

#include <atomic>
#include <mutex>
#include <pthread.h>
#include <string>

#include "app/TrailerTimeline.h"
#include "graphics/Texture.h"
#include "ui/TrailerState.h"

namespace ns {

class TrailerPlayer {
public:
    explicit TrailerPlayer(std::string root);
    ~TrailerPlayer();

    void open(const std::string& url, int user);
    void stop();
    void pump(RenderBackend& backend);
    const Texture* frame() const { return texture_.valid() ? &texture_ : nullptr; }
    TrailerState status() const;
    int progress() const { return nextUrl_.empty() ? progress_.load() : -1; }

private:
    static void* entry(void* self);
    TrailerState play();

    std::string root_, url_;
    int user_ = -1, nextUser_ = -1;
    std::string nextUrl_;
    std::atomic<int> progress_{-1};
    std::atomic<uint64_t> waitingSince_{0};
    bool decodedBuffering_ = false;
    pthread_t thread_{};
    bool joinable_ = false;
    std::atomic<bool> running_{false}, cancelled_{false};
    std::mutex mutex_;
    TrailerTimeline timeline_;
    Image displayImage_;
    Texture texture_;
    std::atomic<TrailerState> status_{TrailerState::Buffering};
};

} // namespace ns
