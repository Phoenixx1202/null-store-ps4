#pragma once

#include "game_details.h"
#include <atomic>
#include <pthread.h>

// One bounded metadata request at a time. Only the main thread starts or takes a result.
class ListingLookup {
public:
    ~ListingLookup();
    bool start(const Game& game, bool swarm, const std::string& cacheDir);
    bool take(std::string& url, ListingDetails& details);
    bool busy() const { return running_; }
private:
    static void* entry(void*);
    void fetch();
    pthread_t thread_{};
    bool running_ = false, swarm_ = false;
    std::atomic<bool> ready_{false};
    Game game_;
    ListingDetails details_;
    std::string cacheDir_;
};
