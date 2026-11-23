#include "app/TrailerDecoder.h"
#include "app/TrailerStream.h"
#include "app/TrailerTimeline.h"
#include "ui/TrailerState.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <fstream>
#include <thread>

#include <cstdio>
#include <string>

namespace {
bool localRange(const std::string& path, PortalStream& s, std::string& why)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { why = "http 404"; return false; }
    const uint64_t size = static_cast<uint64_t>(file.tellg());
    const uint64_t end = std::min(size, s.end);
    if (!s.begin(s.offset, size) || s.offset > end) return false;
    file.seekg(s.offset);
    std::array<uint8_t, 16384> data;
    for (uint64_t pos = s.offset; pos < end;) {
        const auto n = std::min<uint64_t>(data.size(), end - pos);
        if (s.stopped() || !file.read(reinterpret_cast<char*>(data.data()), n) || !s.write(data.data(), n)) return false;
        pos += n;
    }
    return true;
}

void testTimeline()
{
    ns::TrailerTimeline timeline;
    ns::Image shown;
    constexpr uint64_t start = 1000000;
    const auto queue = [&](size_t first, uint64_t now) {
        for (size_t i = 0; i < ns::TrailerTimeline::startupFrames; ++i) {
            ns::Image frame(2, 2);
            frame.data()[0] = static_cast<uint8_t>(first + i);
            timeline.push(frame, static_cast<int64_t>(first + i) * 40000, now);
            if (i + 1 < ns::TrailerTimeline::startupFrames) assert(!timeline.present(now, shown));
        }
    };
    queue(0, start);
    assert(timeline.playhead(start) == 0 && timeline.present(start, shown) && shown.data()[0] == 0);
    assert(!timeline.present(start + 39000, shown));
    assert(timeline.present(start + 80000, shown) && shown.data()[0] == 2 && timeline.skipped() == 1);
    for (size_t i = 3; i < ns::TrailerTimeline::startupFrames; ++i)
        assert(timeline.present(start + i * 40000, shown) && shown.data()[0] == i);
    assert(timeline.empty());
    timeline.checkForGap(start + 400000);
    assert(!timeline.buffering());
    timeline.checkForGap(start + 480000);
    assert(timeline.buffering() && timeline.playhead(start + 800000) == -1 && timeline.gaps() == 1);
    queue(ns::TrailerTimeline::startupFrames, start + 800000);
    assert(!timeline.buffering());
    assert(timeline.present(start + 800000, shown) && shown.data()[0] == ns::TrailerTimeline::startupFrames);
    timeline.reset();
    assert(!timeline.started() && timeline.empty());
    ns::Image shortTrailer(2, 2);
    timeline.push(shortTrailer, 0, start);
    timeline.finish(start);
    assert(timeline.present(start, shown));
    timeline.checkForGap(start + 120000);
    assert(!timeline.buffering()); // End of playback does not trigger another refill.
}

constexpr int64_t kMiB = 1024 * 1024;
constexpr int64_t kInitialNetworkLimit = 8 * kMiB + 256 * 1024;
constexpr int64_t kTestSize = 36 * kMiB;
std::atomic<int64_t> networkLimit{kInitialNetworkLimit};

uint8_t sample(int64_t offset) { return static_cast<uint8_t>((offset / 65536) % 251); }

bool throttledRange(const std::string&, PortalStream& s, std::string&)
{
    using namespace std::chrono_literals;
    if (!s.begin(s.offset, kTestSize)) return false;
    std::array<uint8_t, 65536> data;
    for (int64_t pos = s.offset; pos < static_cast<int64_t>(s.end); ) {
        while (pos >= networkLimit && !s.stopped()) std::this_thread::sleep_for(1ms);
        if (s.stopped()) return false;
        const int64_t end = std::min({pos + static_cast<int64_t>(data.size()), static_cast<int64_t>(s.end),
                                      int64_t(networkLimit), (pos / 65536 + 1) * 65536});
        std::fill_n(data.data(), end - pos, sample(pos));
        if (!s.write(data.data(), end - pos)) return false;
        pos = end;
    }
    return true;
}

template <typename Ready>
void await(Ready ready)
{
    using namespace std::chrono_literals;
    const auto until = std::chrono::steady_clock::now() + 5s;
    while (!ready() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(1ms);
    assert(ready());
}

void testBuffer(bool cancel)
{
    using namespace std::chrono_literals;
    std::atomic<bool> stopped{false}, stalled{false}, returned{false};
    ns::TrailerStream stream("", throttledRange, stopped, [&](bool waiting) { stalled = waiting; });
    assert(stream.open() == ns::TrailerStream::Result::Ready);
    assert(stream.prepare(0, 144000000) && stream.startupBytes() == 2 * kMiB);
    // The producer reaches this ceiling even while the consumer is idle.
    await([&] { return stream.received() == kInitialNetworkLimit; });
    std::array<uint8_t, 65536> data;
    for (int64_t pos = 0; pos < kInitialNetworkLimit; pos += data.size()) {
        assert(stream.readAt(pos, data.data(), data.size()) == int(data.size()));
        assert(data.front() == sample(pos) && data.back() == sample(pos + data.size() - 1));
    }
    std::thread reader([&] {
        const int n = stream.readAt(kInitialNetworkLimit, data.data(), data.size());
        assert(n == (cancel ? -1 : int(data.size())));
        returned = true;
    });
    await([&] { return stalled.load(); });
    if (cancel) {
        stopped = true;
    } else {
        networkLimit = kInitialNetworkLimit + kMiB;
        await([&] { return stream.received() == uint64_t(networkLimit.load()); });
        assert(stalled && !returned); // One chunk is insufficient to resume.
        networkLimit = kTestSize;
    }
    reader.join();
    if (cancel) return;
    assert(data.front() == sample(kInitialNetworkLimit) && !stalled);
    // The compressed buffer wraps as the rest of the trailer streams.
    for (int64_t pos = kInitialNetworkLimit + data.size(); pos < kTestSize;) {
        const int n = stream.readAt(pos, data.data(), data.size());
        assert(n > 0 && data.front() == sample(pos) && data[n - 1] == sample(pos + n - 1));
        pos += n;
    }
    assert(stream.readAt(kTestSize, data.data(), data.size()) == 0);
}

}

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    const std::string mode = argv[2];
    if (mode == "timeline") {
        testTimeline();
        std::puts("Decoded frame scheduling passed");
        return 0;
    }
    if (mode == "buffer" || mode == "buffer-cancel") {
        testBuffer(mode == "buffer-cancel");
        std::puts("Background trailer buffering passed");
        return 0;
    }
    std::atomic<bool> stopped{false};
    ns::TrailerStream stream(argv[1], localRange, stopped);
    ns::TrailerStream gone("", localRange, stopped);
    assert(gone.open() == ns::TrailerStream::Result::Missing && !ns::offerTrailerLink(ns::TrailerState::LinkGone));
    assert(ns::offerTrailerLink(ns::TrailerState::Unsupported) && !ns::offerTrailerLink(ns::TrailerState::Finished));
    ns::TrailerStream noRanges("", [](const std::string&, PortalStream& s, std::string& why) {
        std::array<uint8_t, 16384> bytes{};
        assert(s.begin(0, 104857600));
        while (s.write(bytes.data(), bytes.size())) {}
        why = "http 200";
        return false;
    }, stopped);
    assert(noRanges.open() == ns::TrailerStream::Result::NeedsFile);
    const bool ranged = mode == "stream" || mode == "stream-cancel";
    if (ranged && stream.open() != ns::TrailerStream::Result::Ready) return 1;
    int frames = 0;
    int64_t samples = 0, last = -1;
    bool valid = true;
    ns::TrailerOutput out;
    out.cancelled = [&] { return stopped.load(); };
    out.video = [&](ns::Image& image, int64_t us) {
        valid &= !image.empty() && image.bytes() == size_t(image.width()) * image.height() * 4 && us >= last;
        if (ranged && !frames && stream.size() > (4 << 20))
            valid &= stream.received() >= uint64_t(stream.startupBytes()) && stream.startupBytes() > 0;
        ++frames;
        if ((mode == "cancel" || mode == "stream-cancel") && frames == 4) stopped = true;
        last = us;
        image = {}; // Transfer ownership like the render thread.
    };
    out.audio = [&](const int16_t*, int n) { samples += n; };
    std::string reason;
    const bool completed = ranged ? ns::decodeTrailer(stream, out, reason) : ns::decodeTrailer(argv[1], out, reason);
    std::printf("complete=%d frames=%d stereo_samples=%lld last_frame_us=%lld %s\n",
                completed, frames, static_cast<long long>(samples), static_cast<long long>(last), reason.c_str());
    if (mode == "invalid") return completed || frames != 0;
    if (mode == "cancel" || mode == "stream-cancel") return completed || !valid || frames != 4;
    return !completed || !valid || frames < 12 || last < 500000 || ((mode == "audio" || mode == "stream") ? samples < 24000 : samples != 0);
}
