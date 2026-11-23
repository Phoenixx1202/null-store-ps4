#include "app/TrailerStream.h"

#include <algorithm>
#include <new>
#include <cstring>
#include <utility>

#include "core/Log.h"

#ifdef NS_PLATFORM_PS4
#include <orbis/libkernel.h>
#else
#include <time.h>
#endif

namespace ns {
namespace {
constexpr int64_t kMiB = 1024 * 1024;
constexpr int64_t kPrefix = 256 * 1024;
constexpr int64_t kRange = 4 * kMiB;
constexpr int64_t kCapacity = 20 * kMiB;
constexpr int64_t kReadAhead = 16 * kMiB;

void waitBriefly(std::unique_lock<std::mutex>& lock, unsigned microseconds)
{
    lock.unlock();
#ifdef NS_PLATFORM_PS4
    // The bundled libc++ timer uses a clock ID from the host build.
    sceKernelUsleep(microseconds);
#else
    const timespec until{0, static_cast<long>(microseconds) * 1000};
    nanosleep(&until, nullptr);
#endif
    lock.lock();
}
}

TrailerStream::TrailerStream(std::string url, Download download, const std::atomic<bool>& cancelled,
                             std::function<void(bool)> buffering)
    : url_(std::move(url)), download_(download), cancelled_(cancelled), buffering_(std::move(buffering)) {}

TrailerStream::~TrailerStream()
{
    closing_ = true;
    if (joinable_) pthread_join(thread_, nullptr);
}

TrailerStream::Result TrailerStream::fetch(int64_t start, int64_t count, const Write& write, int64_t& knownSize)
{
    if (count <= 0 || stopping()) return Result::Failed;
    PortalStream s;
    s.offset = start;
    s.end = start + count;
    s.stopped = [&] { return stopping(); };
    bool sequential = false;
    int64_t written = 0;
    s.begin = [&](uint64_t offset, uint64_t total) {
        sequential = !total || (start != 0 && offset == 0);
        if (sequential || offset != uint64_t(start) || total > uint64_t(INT64_MAX) ||
            total <= offset || (knownSize && total != uint64_t(knownSize))) return false;
        knownSize = static_cast<int64_t>(total);
        count = std::min(count, knownSize - start);
        return true;
    };
    s.write = [&](const uint8_t* data, size_t n) {
        if (s.stopped() || n > size_t(count - written)) return false;
        write(data, n);
        written += n;
        received_ += n;
        return true;
    };
    std::string why;
    const bool ok = download_(url_, s, why);
    if (ok && knownSize > start && written == count) return Result::Ready;
    if (why == "http 404" || why == "http 410") return Result::Missing;
    if (sequential || why == "http 200") return Result::NeedsFile;
    return Result::Failed;
}

TrailerStream::Result TrailerStream::open()
{
    prefix_.start = 0;
    prefix_.data.reserve(kPrefix);
    result_ = fetch(0, kPrefix, [&](const uint8_t* bytes, size_t n) {
        prefix_.data.insert(prefix_.data.end(), bytes, bytes + n);
    }, size_);
    return result_;
}

bool TrailerStream::prepare(int64_t mediaOffset, int64_t durationUs)
{
    if (stopping() || result_ != Result::Ready || mediaOffset >= size_) return false;
    mediaOffset = std::max<int64_t>(0, mediaOffset);
    // Estimate eight seconds from the container's average bitrate. Bound
    // startup latency and use a larger threshold to recover from underruns.
    const double estimate = durationUs > 0 ? 8000000.0 * (size_ / double(durationUs)) : 4.0 * kMiB;
    const auto target = static_cast<int64_t>(std::clamp(estimate, 2.0 * kMiB, 10.0 * kMiB));
    startupBytes_ = std::min(target, size_ - mediaOffset);
    resumeBytes_ = std::min<int64_t>(target + target / 2, 12 * kMiB);
    ringSize_ = static_cast<size_t>(std::min(kCapacity, size_));
    NS_LOG_INFO("Trailer buffer setup: offset=%lld, startup=%lld, resume=%lld, capacity=%zu",
                static_cast<long long>(mediaOffset), static_cast<long long>(startupBytes_),
                static_cast<long long>(resumeBytes_), ringSize_);
    ring_.reset(new (std::nothrow) uint8_t[ringSize_]);
    if (!ring_) {
        NS_LOG_WARN("Trailer buffer allocation failed; using temporary file");
        result_ = Result::NeedsFile;
        return false;
    }
    begin_ = end_ = consumed_ = mediaOffset;
    const Block& seed = prefix_.contains(mediaOffset) ? prefix_ : index_;
    if (seed.contains(mediaOffset)) {
        const auto offset = static_cast<size_t>(mediaOffset - seed.start);
        append(seed.data.data() + offset, seed.data.size() - offset);
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    joinable_ = pthread_create(&thread_, &attr, entry, this) == 0;
    pthread_attr_destroy(&attr);
    if (!joinable_) {
        NS_LOG_WARN("Trailer buffer worker did not start");
        result_ = Result::NeedsFile;
        return false;
    }
    NS_LOG_INFO("Trailer buffer worker ready");
    std::unique_lock<std::mutex> lock(mutex_);
    if (!waitFor(lock, mediaOffset + startupBytes_)) return false;
    NS_LOG_INFO("Trailer startup buffer ready: %lld bytes", static_cast<long long>(end_ - mediaOffset));
    return true;
}

void* TrailerStream::entry(void* self)
{
    auto& stream = *static_cast<TrailerStream*>(self);
    try {
        stream.fill();
    } catch (...) {
        std::lock_guard<std::mutex> lock(stream.mutex_);
        stream.transferResult_ = Result::Failed;
    }
    return nullptr;
}

void TrailerStream::fill()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping() && end_ < size_) {
        const int64_t count = std::min(kRange, size_ - end_);
        if (end_ - consumed_ + count > kReadAhead) {
            waitBriefly(lock, 50000);
            continue;
        }
        const int64_t from = end_;
        int64_t knownSize = size_;
        lock.unlock();
        Result outcome = fetch(from, count, [&](const uint8_t* bytes, size_t n) {
            std::lock_guard<std::mutex> guard(mutex_);
            append(bytes, n);
        }, knownSize);
        lock.lock();
        if (outcome == Result::Failed && !stopping() && end_ > from) {
            // A connection can drop mid-range. Keep bytes already received and
            // continue from the next byte on the following request.
            outcome = Result::Ready;
        }
        if (outcome != Result::Ready) {
            transferResult_ = outcome;
            break;
        }
    }
}

void TrailerStream::append(const uint8_t* bytes, size_t n)
{
    const size_t slot = static_cast<size_t>(end_ % ringSize_);
    const size_t first = std::min(n, ringSize_ - slot);
    std::memcpy(ring_.get() + slot, bytes, first);
    if (first < n) std::memcpy(ring_.get(), bytes + first, n - first);
    end_ += n;
    begin_ = std::max(begin_, end_ - static_cast<int64_t>(ringSize_));
}

bool TrailerStream::waitFor(std::unique_lock<std::mutex>& lock, int64_t end)
{
    while (end_ < end && transferResult_ == Result::Ready && !stopping()) waitBriefly(lock, 10000);
    if (stopping()) return false;
    if (end_ >= end) return true;
    result_ = transferResult_;
    return false;
}

int TrailerStream::randomRead(int64_t position, uint8_t* dst, int count)
{
    const Block* block = &prefix_;
    if (!prefix_.contains(position)) {
        if (!index_.contains(position)) {
            constexpr int64_t span = 512 * 1024;
            index_.start = position - position % span;
            index_.data.clear();
            int64_t knownSize = size_;
            result_ = fetch(index_.start, std::min(span, size_ - index_.start), [&](const uint8_t* bytes, size_t n) {
                index_.data.insert(index_.data.end(), bytes, bytes + n);
            }, knownSize);
            if (result_ != Result::Ready) return -1;
        }
        block = &index_;
    }
    const int bytes = static_cast<int>(std::min<int64_t>(count, block->end() - position));
    std::memcpy(dst, block->data.data() + (position - block->start), static_cast<size_t>(bytes));
    return bytes;
}

int TrailerStream::readAt(int64_t position, uint8_t* dst, int count)
{
    if (position < 0 || count <= 0 || stopping() || result_ != Result::Ready) return -1;
    if (position >= size_) return 0;
    if (!joinable_) return randomRead(position, dst, count);
    std::unique_lock<std::mutex> lock(mutex_);
    if (position < begin_ || position - end_ >= kRange) {
        // Index lookups and unusual jumps do not move the playback window.
        lock.unlock();
        if (buffering_) buffering_(true);
        const int bytes = randomRead(position, dst, count);
        if (buffering_) buffering_(false);
        return bytes;
    }
    if (position >= end_) {
        if (buffering_) buffering_(true);
        consumed_ = std::max(consumed_, position);
        const bool refilled = waitFor(lock, position + std::min(resumeBytes_, size_ - position));
        if (buffering_) buffering_(false);
        if (!refilled) return -1;
    }
    const auto slot = static_cast<size_t>(position % ringSize_);
    const int bytes = static_cast<int>(std::min<int64_t>(count, end_ - position));
    const size_t first = std::min(static_cast<size_t>(bytes), ringSize_ - slot);
    std::memcpy(dst, ring_.get() + slot, first);
    if (first < static_cast<size_t>(bytes)) std::memcpy(dst + first, ring_.get(), bytes - first);
    consumed_ = std::max(consumed_, position + bytes);
    return bytes;
}
} // namespace ns
