#include "app/TrailerPlayer.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <array>
#include <fcntl.h>
#include <unistd.h>

#include <orbis/AudioOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include "app/TrailerDecoder.h"
#include "app/TrailerStream.h"
#include "core/Log.h"
#include "mapped_module.h"
#include "network_bypass.h"
#include "network_ps4.h"
#include "sony_art.h"

namespace ns {
namespace {

constexpr size_t kMaxFileBytes = 256 * 1024 * 1024; // Only for servers without range support.
template <typename Function>
bool resolve(int module, const char* name, Function& function)
{
    void* address = nullptr;
    if (sceKernelDlsym(module, name, &address) != 0 || !address) return false;
    function = reinterpret_cast<Function>(address);
    return true;
}

class TrailerAudio {
public:
    TrailerAudio(int user, const std::atomic<bool>& cancelled, std::function<int64_t()> playhead)
        : user_(user), cancelled_(cancelled), playhead_(std::move(playhead)) {}

    ~TrailerAudio() {
        stop_ = true;
        if (started_) pthread_join(thread_, nullptr);
    }

    void push(const int16_t* data, int frames) {
        if (!tried_) {
            tried_ = true;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 1024 * 1024);
            started_ = pthread_create(&thread_, &attr, entry, this) == 0;
            pthread_attr_destroy(&attr);
        }
        if (!started_) return;
        size_t count = size_t(frames) * 2;
        while (count && !cancelled_ && !stop_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const size_t slot = (head_ + used_) % samples_.size();
                const size_t copy = std::min({count, samples_.size() - used_, samples_.size() - slot});
                std::copy_n(data, copy, samples_.data() + slot);
                count -= copy;
                data += copy;
                used_ += copy;
                if (count == 0) return;
            }
            // Ignore excessive audio preroll so packets can reach the first video frame.
            if (playhead_() < 0) return;
            sceKernelUsleep(2000);
        }
    }

    void finish() {
        finished_ = true;
        if (started_) pthread_join(thread_, nullptr);
        started_ = false;
    }

    unsigned underruns() const { return underruns_; }

private:
    static constexpr size_t kSamples = 256 * 2;

    static void* entry(void* self) {
        auto& audio = *static_cast<TrailerAudio*>(self);
        try { audio.play(); } catch (...) { NS_LOG_WARN("Trailer audio stopped unexpectedly"); }
        audio.stop_ = true;
        return nullptr;
    }

    void play() {
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT);
        const int module = loadedModule("libSceAudioOut");
        decltype(&sceAudioOutInit) init;
        decltype(&sceAudioOutOpen) open;
        decltype(&sceAudioOutOutput) output;
        decltype(&sceAudioOutClose) close;
        if (module < 0 || !resolve(module, "sceAudioOutInit", init) || !resolve(module, "sceAudioOutOpen", open) ||
            !resolve(module, "sceAudioOutOutput", output) || !resolve(module, "sceAudioOutClose", close)) return;
        const int initialized = init();
        int handle = open(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, kSamples / 2, 48000,
                          ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
        if (handle < 0 && user_ != ORBIS_USER_SERVICE_USER_ID_SYSTEM)
            handle = open(user_, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, kSamples / 2, 48000,
                          ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
        NS_LOG_INFO("Trailer audio init=0x%08X port=0x%08X", unsigned(initialized), unsigned(handle));
        if (handle < 0) return;

        std::array<int16_t, kSamples> block{};
        int64_t sent = 0;
        bool starved = false;
        while (!cancelled_ && !stop_) {
            const int64_t time = playhead_();
            if (time < 0 || (sent * 1000000 / 48000 > time + 15000 && !finished_)) {
                sceKernelUsleep(2000);
                continue;
            }
            size_t count;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                count = std::min(block.size(), used_);
                if (count == block.size() || finished_) {
                    const size_t first = std::min(count, samples_.size() - head_);
                    std::copy_n(samples_.data() + head_, first, block.data());
                    std::copy_n(samples_.data(), count - first, block.data() + first);
                    head_ = (head_ + count) % samples_.size();
                    used_ -= count;
                } else count = 0;
            }
            if (!count) {
                if (finished_) break;
                if (!starved && sent * 1000000 / 48000 + 50000 < time) {
                    starved = true;
                    ++underruns_;
                }
                sceKernelUsleep(2000);
                continue;
            }
            starved = false;
            std::fill(block.begin() + count, block.end(), 0);
            if (output(handle, block.data()) < 0) break;
            sent += kSamples / 2;
        }
        close(handle);
    }

    int user_;
    const std::atomic<bool>& cancelled_;
    std::function<int64_t()> playhead_;
    pthread_t thread_{};
    bool tried_ = false, started_ = false;
    std::atomic<bool> stop_{false}, finished_{false};
    std::atomic<unsigned> underruns_{0};
    std::mutex mutex_;
    std::array<int16_t, 96000> samples_{};
    size_t head_ = 0, used_ = 0;
};

struct TemporaryTrailer {
    std::string path;
    std::string error;
    bool tooLarge = false;
    int fd = -1;
    ~TemporaryTrailer() {
        if (fd >= 0) close(fd);
        if (!path.empty()) unlink(path.c_str());
    }

    bool fetch(const std::string& root, const std::string& url, const std::atomic<bool>& cancelled, std::atomic<int>& progress) {
        const std::string completed = root + "/trailer-preview.mp4";
        path = completed + ".part";
        fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
        if (fd < 0) return false;
        size_t written = 0;
        char header[12]{};
        const auto started = sceKernelGetProcessTime();
        PortalStream stream;
        uint64_t length = 0;
        stream.begin = [&](uint64_t offset, uint64_t total) {
            length = total;
            tooLarge = total > kMaxFileBytes;
            return offset == 0 && !tooLarge;
        };
        stream.write = [&](const unsigned char* bytes, size_t available) {
            if (available > kMaxFileBytes - written) { tooLarge = true; return false; }
            if (written < sizeof header) {
                memcpy(header + written, bytes, std::min(available, sizeof header - written));
            }
            while (available) {
                const ssize_t count = write(fd, bytes, available);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) return false;
                bytes += count;
                available -= size_t(count);
                written += size_t(count);
            }
            if (length) progress = static_cast<int>(std::min<uint64_t>(100, written * 100 / length));
            return true;
        };
        stream.stopped = [&] { return cancelled || sceKernelGetProcessTime() - started > 600000000; };
        const bool fetched = portalSocketDownload(url, stream, error);
        const int closed = close(fd);
        fd = -1;
        if (!fetched || closed != 0 || (length && length != written) || written < sizeof header ||
            memcmp(header + 4, "ftyp", 4) != 0 ||
            rename(path.c_str(), completed.c_str()) != 0) {
            NS_LOG_WARN("Trailer transfer stopped after %zu bytes (%s)", written, error.c_str());
            return false;
        }
        path = completed;
        progress = 100;
        NS_LOG_INFO("Trailer download complete (%zu bytes)", written);
        return true;
    }
};

} // namespace

TrailerPlayer::TrailerPlayer(std::string root) : root_(std::move(root)) {}

TrailerPlayer::~TrailerPlayer()
{
    stop();
    if (joinable_) pthread_join(thread_, nullptr);
}

void TrailerPlayer::open(const std::string& url, int user)
{
    texture_.destroy();
    if (running_) {
        stop();
        nextUrl_ = url;
        nextUser_ = user;
        return;
    }
    if (joinable_) pthread_join(thread_, nullptr);
    timeline_.reset();
    displayImage_ = {};
    progress_ = -1;
    waitingSince_ = 0;
    decodedBuffering_ = false;
    status_ = TrailerState::Buffering;
    NS_LOG_INFO("Trailer playback requested");
    cancelled_ = false;
    url_ = url;
    user_ = user;
    running_ = true;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 4 * 1024 * 1024);
    joinable_ = pthread_create(&thread_, &attributes, entry, this) == 0;
    pthread_attr_destroy(&attributes);
    if (!joinable_) {
        running_ = false;
        status_ = TrailerState::PlayerFailed;
    }
}

void TrailerPlayer::stop() { nextUrl_.clear(); cancelled_ = true; }

TrailerState TrailerPlayer::status() const
{
    if (!nextUrl_.empty()) return TrailerState::Buffering;
    const auto state = status_.load();
    const uint64_t since = waitingSince_;
    if (state == TrailerState::Playing && (decodedBuffering_ || (since && sceKernelGetProcessTime() - since >= 400000)))
        return TrailerState::Rebuffering;
    return state;
}

void TrailerPlayer::pump(RenderBackend& backend)
{
    if (!nextUrl_.empty()) {
        if (running_) return;
        std::string url = std::move(nextUrl_);
        nextUrl_.clear();
        open(url, nextUser_);
    }
    Image& next = displayImage_;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t now = sceKernelGetProcessTime();
        const bool ready = timeline_.present(now, next);
        timeline_.checkForGap(now);
        decodedBuffering_ = timeline_.buffering();
        if (!ready) return;
    }
    const bool allocated = texture_.width() == next.width() && texture_.height() == next.height();
    if (!(allocated ? texture_.update(next) : texture_.create(backend, next))) {
        stop();
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = TrailerState::PlayerFailed;
        NS_LOG_WARN("Trailer texture upload failed (%dx%d)", next.width(), next.height());
    }
}

void* TrailerPlayer::entry(void* self)
{
    auto& player = *static_cast<TrailerPlayer*>(self);
    TrailerState outcome = TrailerState::PlayerFailed;
    try {
        outcome = player.play();
    } catch (...) {
        NS_LOG_WARN("Trailer stopped during playback setup");
    }
    {
        std::lock_guard<std::mutex> lock(player.mutex_);
        if (!player.cancelled_) player.status_ = outcome;
    }
    player.running_ = false;
    return nullptr;
}

TrailerState TrailerPlayer::play()
{
    if (!sony::isPreviewAddress(url_)) return TrailerState::LinkGone;
    if (!portalNetworkInit()) return TrailerState::ConnectionFailed;
    bool started = false;
    unsigned stalls = 0;
    uint64_t stalledUs = 0;
    TrailerStream stream(url_, portalSocketDownload, cancelled_, [&](bool waiting) {
        const uint64_t now = sceKernelGetProcessTime();
        const uint64_t since = waitingSince_.exchange(waiting ? now : 0);
        if (waiting || !since || !started || cancelled_) return;
        const uint64_t elapsed = now - since;
        if (elapsed >= 400000) {
            ++stalls;
            stalledUs += elapsed;
            NS_LOG_INFO("Trailer buffered again after %llu ms", static_cast<unsigned long long>(elapsed / 1000));
        }
    });
    const auto opened = stream.open();
    if (opened == TrailerStream::Result::Missing) return TrailerState::LinkGone;
    if (opened == TrailerStream::Result::Failed || cancelled_) return TrailerState::ConnectionFailed;
    NS_LOG_INFO("Trailer access: %s, size=%lld", opened == TrailerStream::Result::Ready ? "range" : "sequential",
                static_cast<long long>(stream.size()));
    TrailerAudio audio(user_, cancelled_, [&] {
        std::lock_guard<std::mutex> lock(mutex_);
        return timeline_.playhead(sceKernelGetProcessTime());
    });
    TrailerOutput output;
    output.maxVideoWidth = 768;
    output.cancelled = [&] { return cancelled_.load(); };
    output.audio = [&](const int16_t* data, int frames) { if (!cancelled_) audio.push(data, frames); };
    output.video = [&](Image& image, int64_t timeUs) {
        while (!cancelled_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!timeline_.full()) {
                    const int width = image.width(), height = image.height();
                    timeline_.push(image, timeUs, sceKernelGetProcessTime());
                    if (timeline_.started() && !started) {
                        started = true;
                        status_ = TrailerState::Playing;
                        NS_LOG_INFO("Trailer video queued (%dx%d), startup target=%lld bytes, range received=%llu",
                                    width, height, static_cast<long long>(stream.startupBytes()),
                                    static_cast<unsigned long long>(stream.received()));
                    }
                    return;
                }
            }
            sceKernelUsleep(2000);
        }
    };
    std::string error;
    bool finished = opened == TrailerStream::Result::Ready && decodeTrailer(stream, output, error);
    waitingSince_ = 0;
    if (!cancelled_ && !finished && !started && stream.result() == TrailerStream::Result::NeedsFile) {
        TemporaryTrailer file;
        status_ = TrailerState::DownloadingFile;
        if (!file.fetch(root_, url_, cancelled_, progress_)) {
            if (file.error == "http 404" || file.error == "http 410") return TrailerState::LinkGone;
            return file.tooLarge ? TrailerState::Unsupported : TrailerState::ConnectionFailed;
        }
        error.clear();
        finished = decodeTrailer(file.path, output, error);
    }
    unsigned skipped = 0, decodeGaps = 0;
    if (finished) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            timeline_.finish(sceKernelGetProcessTime());
            status_ = TrailerState::Playing;
        }
        audio.finish();
        while (!cancelled_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (timeline_.empty()) break;
            }
            sceKernelUsleep(2000);
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        skipped = timeline_.skipped();
        decodeGaps = timeline_.gaps();
    }
    if (!finished && !cancelled_) NS_LOG_WARN("Trailer stopped: %s", error.c_str());
    else NS_LOG_INFO("Trailer %s (stalls=%u, buffered wait=%llu ms, decoded gaps=%u, video skips=%u, audio underruns=%u)",
                     finished ? "finished" : "cancelled", stalls,
                     static_cast<unsigned long long>(stalledUs / 1000), decodeGaps, skipped, audio.underruns());
    if (finished) return TrailerState::Finished;
    if (stream.result() == TrailerStream::Result::Missing) return TrailerState::LinkGone;
    if (stream.result() == TrailerStream::Result::Failed) return TrailerState::ConnectionFailed;
    return error == "unsupported video" ? TrailerState::Unsupported : TrailerState::PlayerFailed;
}

} // namespace ns
