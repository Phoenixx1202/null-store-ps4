#pragma once

#include "network_bypass.h"
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

namespace ns {

// The decoder can read the MP4 index out of order. A separate worker keeps
// the contiguous playback window filled without waiting for decoder reads.
class TrailerStream {
public:
    enum class Result { Ready, NeedsFile, Missing, Failed };
    using Download = decltype(&portalSocketDownload);
    TrailerStream(std::string url, Download download, const std::atomic<bool>& cancelled,
                  std::function<void(bool)> buffering = {});
    ~TrailerStream();
    TrailerStream(const TrailerStream&) = delete;
    TrailerStream& operator=(const TrailerStream&) = delete;

    Result open();
    bool prepare(int64_t mediaOffset, int64_t durationUs);
    int readAt(int64_t position, uint8_t* dst, int count);
    int64_t size() const { return size_; }
    Result result() const { return result_; }
    uint64_t received() const { return received_.load(); }
    int64_t startupBytes() const { return startupBytes_; }

private:
    struct Block {
        int64_t start = -1;
        std::vector<uint8_t> data;
        int64_t end() const { return start + static_cast<int64_t>(data.size()); }
        bool contains(int64_t pos) const { return pos >= start && pos < end(); }
    };
    using Write = std::function<void(const uint8_t*, size_t)>;
    Result fetch(int64_t start, int64_t count, const Write& write, int64_t& knownSize);
    int randomRead(int64_t position, uint8_t* dst, int count);
    bool waitFor(std::unique_lock<std::mutex>& lock, int64_t end);
    bool stopping() const { return cancelled_ || closing_; }
    static void* entry(void* self);
    void fill();
    void append(const uint8_t* bytes, size_t n);

    std::string url_;
    Download download_;
    const std::atomic<bool>& cancelled_;
    std::function<void(bool)> buffering_;
    int64_t size_ = 0, startupBytes_ = 0, resumeBytes_ = 0;
    Block prefix_, index_;
    Result result_ = Result::Failed;
    std::atomic<uint64_t> received_{0};
    std::atomic<bool> closing_{false};
    pthread_t thread_{};
    bool joinable_ = false;
    std::mutex mutex_;
    std::unique_ptr<uint8_t[]> ring_;
    size_t ringSize_ = 0;
    int64_t begin_ = 0, end_ = 0, consumed_ = 0;
    Result transferResult_ = Result::Ready;
};

} // namespace ns
