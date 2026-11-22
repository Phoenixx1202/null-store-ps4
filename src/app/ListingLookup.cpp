#include "app/ListingLookup.h"
#include "sources.h"
#include "network_ps4.h"
#include "download_model.h"
#include "torrent.h"
#include <cstring>

static unsigned char* fetchSwarmPreview(void*,const char* url,size_t* bytes){
    *bytes = 0;
    std::string body;
    if(!portalNetworkFetch(url,body,256*1024,"")||body.empty())return nullptr;
    auto* result=static_cast<unsigned char*>(malloc(body.size()));
    if(!result)return nullptr;
    memcpy(result,body.data(),body.size());
    *bytes=body.size();
    return result;
}

ListingLookup::~ListingLookup() {
    if (running_) pthread_join(thread_, nullptr);
}
bool ListingLookup::start(const Game& game, bool swarm, const std::string& cacheDir) {
    if (running_) return false;
    game_ = game;
    swarm_ = swarm;
    details_ = {};
    cacheDir_ = cacheDir;
    ready_.store(false);
    running_ = pthread_create(&thread_, nullptr, entry, this) == 0;
    return running_;
}
void* ListingLookup::entry(void* arg) {
    auto& lookup = *static_cast<ListingLookup*>(arg);
    lookup.fetch();
    lookup.ready_.store(true);
    return nullptr;
}
bool ListingLookup::take(std::string& url, ListingDetails& details) {
    if (!running_ || !ready_.load()) return false;
    pthread_join(thread_, nullptr);
    running_ = false;
    details = details_;
    url = game_.url;
    return true;
}
void ListingLookup::fetch() {
    const auto table = sourceSnapshot();
    const int source = table->indexForURL(game_.url);
    if (!source) return;
    const auto cacheFile = cacheDir_ + "/" + std::to_string(game_.id) + ".txt";
    const auto prefix = game_.url + "\n";
    const auto cached = readFile(cacheFile);
    uint64_t bytes = 0;
    if (cached.compare(0, prefix.size(), prefix) == 0)
        bytes = strtoull(cached.c_str() + prefix.size(), nullptr, 10);
    if (bytes && !swarm_) { details_.size = bytesText(bytes); return; }
    std::string html;
    if (!portalNetworkFetch(game_.url, html, 4*1024*1024, table->origin(source) + "/")) return;
    details_ = parseListingDetails(html, source);
    const bool wantSize = !bytes && details_.size.empty() && sizeMB(game_.size) == 0;
    const bool wantSwarm = swarm_ && (details_.seeds < 0 || details_.leeches < 0);
    if (wantSize || wantSwarm) {
        auto links = parseDownloadLinksWith(*table, html, source);
        std::string metadata;
        if (links.size() == 1 && portalNetworkFetch(links[0].url, metadata, 8*1024*1024, game_.url)) {
            const auto* raw = reinterpret_cast<const unsigned char*>(metadata.data());
            bytes = torrent_metadata_size(raw, metadata.size());
            if (bytes) writeFile(cacheFile, prefix + std::to_string(bytes));
            if (wantSwarm) torrent_swarm_preview(raw, metadata.size(), fetchSwarmPreview, nullptr,
                                                 &details_.seeds, &details_.leeches);
        }
    }
    if (bytes && details_.size.empty()) details_.size = bytesText(bytes);
}
