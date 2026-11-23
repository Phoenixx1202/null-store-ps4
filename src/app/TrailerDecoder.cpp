#include "app/TrailerDecoder.h"
#include "app/TrailerStream.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <vector>

#if defined(NS_PLATFORM_PS4)
#include <orbis/libkernel.h>
#include "core/Log.h"
#endif

namespace ns {
namespace {

constexpr int kMaxPixels = 1920 * 1088;
constexpr int64_t kMaxDurationUs = 10 * 60 * 1000000LL;

struct Decoder {
    AVIOContext* io = nullptr;
    TrailerStream* network = nullptr;
    int64_t position = 0;
    AVFormatContext* file = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    SwsContext* scale = nullptr;
    SwrContext* resample = nullptr;
    Image image;
    int64_t firstPts = AV_NOPTS_VALUE;
    int64_t lastUs = -1;
    int videoIndex = -1, audioIndex = -1;
    AVChannelLayout audioLayout{};
    int audioRate = 0, audioFormat = -1;

    ~Decoder() {
#if defined(NS_PLATFORM_PS4)
        if (decodedFrames_)
            NS_LOG_INFO("Trailer decoder: frames=%u, video packets over 33ms=%u, slowest decode=%llu ms, slowest conversion=%llu ms",
                        decodedFrames_, slowPackets_, static_cast<unsigned long long>(worstDecodeUs_ / 1000),
                        static_cast<unsigned long long>(worstConversionUs_ / 1000));
#endif
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        avformat_close_input(&file);
        if (network && io) {
            av_freep(&io->buffer);
            avio_context_free(&io);
        } else avio_closep(&io);
        sws_freeContext(scale);
        swr_free(&resample);
        av_channel_layout_uninit(&audioLayout);
    }

    static int read(void* context, uint8_t* data, int count) {
        auto& d = *static_cast<Decoder*>(context);
        try {
            const int n = d.network->readAt(d.position, data, count);
            if (n <= 0) return n == 0 ? AVERROR_EOF : AVERROR(EIO);
            d.position += n;
            return n;
        } catch (...) { return AVERROR(ENOMEM); }
    }

    static int64_t seek(void* context, int64_t offset, int whence) {
        auto& d = *static_cast<Decoder*>(context);
        const int64_t size = d.network->size();
        whence &= ~AVSEEK_FORCE;
        if (whence == AVSEEK_SIZE) return size;
        if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return AVERROR(EINVAL);
        const int64_t base = whence == SEEK_CUR ? d.position : whence == SEEK_END ? size : 0;
        if (offset < -base || offset > size - base) return AVERROR(EINVAL);
        return d.position = base + offset;
    }

    AVCodecContext* openStream(int index, AVCodecID id) {
        if (index < 0) return nullptr;
        const AVCodecParameters* params = file->streams[index]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(id);
        if (!codec || params->codec_id != id) return nullptr;
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (!ctx) return nullptr;
        ctx->max_pixels = kMaxPixels;
        ctx->thread_count = 1;
        if (avcodec_parameters_to_context(ctx, params) < 0 || avcodec_open2(ctx, codec, nullptr) < 0)
            avcodec_free_context(&ctx);
        return ctx;
    }

    bool picture(const TrailerOutput& output) {
#if defined(NS_PLATFORM_PS4)
        const auto began = sceKernelGetProcessTime();
#endif
        const int w = frame->width, h = frame->height;
        if (w < 2 || h < 2 || w > 1920 || h > 1088) return false;
        const int width = std::min(w, output.maxVideoWidth);
        const int height = std::max(1, h * width / w);
        scale = sws_getCachedContext(scale, w, h, static_cast<AVPixelFormat>(frame->format),
                                     width, height, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale) return false;
        // Swscale's SIMD writers require padding at the end of destination rows.
        const int stride = FFALIGN(width * 4, 64);
        scaled_.resize(size_t(stride) * height + 64);
        uint8_t* planes[4] = {scaled_.data()};
        const int strides[4] = {stride};
        if (sws_scale(scale, frame->data, frame->linesize, 0, h, planes, strides) != height) return false;
        if (image.width() != width || image.height() != height) image.reset(width, height);
        for (int y = 0; y < height; ++y)
            std::copy_n(scaled_.data() + size_t(y) * stride, width * 4, image.data() + size_t(y) * width * 4);
        const auto* stream = file->streams[videoIndex];
        const auto ts = frame->best_effort_timestamp;
        const auto rate = av_guess_frame_rate(file, file->streams[videoIndex], frame);
        const int64_t interval = rate.num > 0 && rate.den > 0 ?
            av_rescale_q(1, av_inv_q(rate), AVRational{1, 1000000}) : 33333;
        int64_t us = lastUs < 0 ? 0 : lastUs + std::clamp<int64_t>(interval, 1000, 1000000);
        if (ts != AV_NOPTS_VALUE) {
            if (firstPts == AV_NOPTS_VALUE) firstPts = ts;
            const int64_t delta = av_sat_sub64(ts, firstPts);
            us = av_rescale_q(delta, stream->time_base, AVRational{1, 1000000});
        }
        if (us < 0 || us > kMaxDurationUs || (lastUs >= 0 && us > lastUs + 5000000)) return false;
        lastUs = std::max(lastUs, us);
#if defined(NS_PLATFORM_PS4)
        ++decodedFrames_;
        worstConversionUs_ = std::max(worstConversionUs_, sceKernelGetProcessTime() - began);
#endif
        output.video(image, lastUs);
        return true;
    }

    void sound(const TrailerOutput& output) {
        if (!output.audio || frame->sample_rate < 8000 || frame->sample_rate > 96000 ||
            frame->ch_layout.nb_channels < 1 || frame->ch_layout.nb_channels > 8) return;
        if (!resample || audioRate != frame->sample_rate || audioFormat != frame->format ||
            av_channel_layout_compare(&audioLayout, &frame->ch_layout)) {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            swr_free(&resample);
            if (swr_alloc_set_opts2(&resample, &stereo, AV_SAMPLE_FMT_S16, 48000, &frame->ch_layout,
                                    static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr) < 0 ||
                swr_init(resample) < 0) { swr_free(&resample); return; }
            av_channel_layout_uninit(&audioLayout);
            if (av_channel_layout_copy(&audioLayout, &frame->ch_layout) < 0) return;
            audioRate = frame->sample_rate;
            audioFormat = frame->format;
        }
        const int capacity = swr_get_out_samples(resample, frame->nb_samples);
        if (capacity <= 0 || capacity > 16384) return;
        samples_.resize(size_t(capacity) * 2);
        auto* bytes = reinterpret_cast<uint8_t*>(samples_.data());
        const int n = swr_convert(resample, &bytes, capacity, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
        if (n > 0) output.audio(samples_.data(), n);
    }

    bool drain(AVCodecContext* ctx, const AVPacket* input, const TrailerOutput& output) {
#if defined(NS_PLATFORM_PS4)
        const bool pictureStream = ctx == video;
        auto began = pictureStream ? sceKernelGetProcessTime() : 0;
#endif
        if (avcodec_send_packet(ctx, input) < 0) return false;
        while (!output.cancelled()) {
            const int status = avcodec_receive_frame(ctx, frame);
#if defined(NS_PLATFORM_PS4)
            if (pictureStream) {
                const auto elapsed = sceKernelGetProcessTime() - began;
                worstDecodeUs_ = std::max(worstDecodeUs_, elapsed);
                slowPackets_ += elapsed > 33000 ? 1 : 0;
            }
#endif
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return true;
            if (status < 0) return false;
            if (ctx == video) {
                if (!picture(output)) return false;
            } else sound(output);
            av_frame_unref(frame);
#if defined(NS_PLATFORM_PS4)
            if (pictureStream) began = sceKernelGetProcessTime();
#endif
        }
        return false;
    }
private:
    std::vector<uint8_t> scaled_;
    std::vector<int16_t> samples_;
#if defined(NS_PLATFORM_PS4)
    unsigned decodedFrames_ = 0, slowPackets_ = 0;
    uint64_t worstDecodeUs_ = 0, worstConversionUs_ = 0;
#endif
};

bool decode(const char* file, TrailerStream* network, const TrailerOutput& output, std::string& error)
{
    Decoder d;
    d.file = avformat_alloc_context();
    if (!d.file || !d.packet || !d.frame) { error = "allocating decoder"; return false; }
    d.file->max_streams = 12;
    d.file->interrupt_callback = {[](void* p) -> int {
        return static_cast<const TrailerOutput*>(p)->cancelled();
    }, const_cast<TrailerOutput*>(&output)};
    AVDictionary* options = nullptr;
    // A preview is self-contained. Refuse any secondary files named by a container.
    d.file->io_open = [](AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) -> int {
        return AVERROR(EPERM);
    };
    // Open the one permitted file explicitly. Custom IO stays owned by the caller.
    d.network = network;
    if (network) {
        constexpr int bytes = 64 * 1024;
        auto* buffer = static_cast<uint8_t*>(av_malloc(bytes));
        if (buffer) d.io = avio_alloc_context(buffer, bytes, 0, &d, Decoder::read, nullptr, Decoder::seek);
        if (!d.io) { av_free(buffer); error = "allocating stream buffer"; return false; }
    } else if (avio_open(&d.io, file, AVIO_FLAG_READ) < 0) { error = "reading trailer file"; return false; }
    d.file->pb = d.io;
    d.file->flags |= AVFMT_FLAG_CUSTOM_IO;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    av_dict_set(&options, "enable_drefs", "0", 0);
    const int opened = avformat_open_input(&d.file, file, av_find_input_format("mov"), &options);
    av_dict_free(&options);
    if (opened < 0) { error = "opening MP4"; return false; }
    // MP4 declares its tracks in the header. Avoid probing arbitrary codecs before applying limits.
    d.videoIndex = av_find_best_stream(d.file, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    d.audioIndex = av_find_best_stream(d.file, AVMEDIA_TYPE_AUDIO, -1, d.videoIndex, nullptr, 0);
    if (d.file->duration > kMaxDurationUs || !(d.video = d.openStream(d.videoIndex, AV_CODEC_ID_H264))) {
        error = "unsupported video"; return false;
    }
    d.audio = d.openStream(d.audioIndex, AV_CODEC_ID_AAC);
    bool prepare = network != nullptr;
    int status = 0;
    while (!output.cancelled() && (status = av_read_frame(d.file, d.packet)) >= 0) {
        if (prepare && (d.packet->stream_index == d.videoIndex || d.packet->stream_index == d.audioIndex)) {
            prepare = false;
            if (!network->prepare(d.packet->pos, d.file->duration)) {
                error = "filling the trailer buffer";
                return false;
            }
        }
        bool ok = true;
        if (d.packet->stream_index == d.videoIndex) ok = d.drain(d.video, d.packet, output);
        if (d.audio && d.packet->stream_index == d.audioIndex && !d.drain(d.audio, d.packet, output))
            avcodec_free_context(&d.audio); // Video remains usable when an audio track fails.
        av_packet_unref(d.packet);
        if (!ok) { error = "decoding a video frame"; return false; }
    }
    if (status != AVERROR_EOF || output.cancelled() || !d.drain(d.video, nullptr, output) || d.lastUs < 0) {
        error = "incomplete trailer"; return false;
    }
    if (d.audio) d.drain(d.audio, nullptr, output);
    return !output.cancelled();
}
} // namespace

bool decodeTrailer(TrailerStream& stream, const TrailerOutput& output, std::string& error)
{
    return decode("preview.mp4", &stream, output, error);
}

bool decodeTrailer(const std::string& file, const TrailerOutput& output, std::string& error)
{
    return decode(file.c_str(), nullptr, output, error);
}
} // namespace ns
