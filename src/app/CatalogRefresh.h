#pragma once

#include <pthread.h>
#include <string>
#include <vector>

#include "catalog.h"

// -----------------------------------------------------------------------------
// Background refresh of the catalogue.
//
// A worker thread re-reads the listing pages of the catalogue sources, saves
// them per provider under the data folder and then caches cover art that is
// not in the package. The main loop polls for the status line and for a new
// catalogue to show.
// -----------------------------------------------------------------------------
namespace ns {

class CatalogRefresh {
public:
    explicit CatalogRefresh(std::string dataPath);

    // Starts a refresh unless one is running. `source` 0 refreshes them all;
    // `force` ignores the freshness of the saved copies.
    void start(int source = 0, bool force = false);

    // Copies the worker's status line into `current`; true when it changed.
    bool pollStatus(std::string& current);
    // Moves a newly published catalogue into `games`; true when there was one.
    bool pollCatalogue(std::vector<Game>& games);

private:
    static void* entry(void* self);
    void run();
    void setStatus(const std::string& text);
    void publish(const std::vector<Game>& games);
    void cacheCovers(std::vector<Game>& games, int chosen);

    std::string data_;
    pthread_mutex_t lock_ = PTHREAD_MUTEX_INITIALIZER;
    bool working_ = false;
    bool ready_ = false;
    int source_ = 0;
    bool forced_ = false;
    std::string message_;
    std::vector<Game> refreshed_;
};

} // namespace ns
