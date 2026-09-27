// SPDX-License-Identifier: GPL-3.0-or-later
#include "Decoding.h"

#include <QFile>
#include <QTransform>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <memory>

namespace vedit::engine {

namespace {

struct InputDeleter
{
    void operator()(AVFormatContext *context) const { avformat_close_input(&context); }
};
struct CodecDeleter
{
    void operator()(AVCodecContext *context) const { avcodec_free_context(&context); }
};
struct FrameDeleter
{
    void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};
struct PacketDeleter
{
    void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
struct SwsDeleter
{
    void operator()(SwsContext *context) const { sws_freeContext(context); }
};
struct SwrDeleter
{
    void operator()(SwrContext *context) const { swr_free(&context); }
};

using Input = std::unique_ptr<AVFormatContext, InputDeleter>;
using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;

bool cancelled(const std::atomic<bool> *cancel)
{
    return cancel && cancel->load();
}

Input openInput(const QString &path)
{
    AVFormatContext *raw = nullptr;
    if (avformat_open_input(&raw, QFile::encodeName(path).constData(), nullptr, nullptr) < 0) {
        return nullptr;
    }
    Input input(raw);
    if (avformat_find_stream_info(input.get(), nullptr) < 0) {
        return nullptr;
    }
    return input;
}

Codec openDecoder(const AVStream *stream)
{
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        return nullptr;
    }
    Codec codec(avcodec_alloc_context3(decoder));
    if (!codec || avcodec_parameters_to_context(codec.get(), stream->codecpar) < 0) {
        return nullptr;
    }
    codec->thread_count = 2; // background work: leave the other cores to playback
    codec->pkt_timebase = stream->time_base;
    if (avcodec_open2(codec.get(), decoder, nullptr) < 0) {
        return nullptr;
    }
    return codec;
}

int rotationOf(const AVStream *stream)
{
    const AVPacketSideData *side = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                           stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!side || side->size < 9 * static_cast<int>(sizeof(int32_t))) {
        return 0;
    }
    const double counterClockwise = av_display_rotation_get(reinterpret_cast<const int32_t *>(side->data));
    if (std::isnan(counterClockwise)) {
        return 0;
    }
    return ((static_cast<int>(std::lround(-counterClockwise)) % 360) + 360) % 360;
}

int even(double value)
{
    return std::max(2, static_cast<int>(std::lround(value / 2.0)) * 2);
}

// Decodes forward from the current read position until a frame at or after `target` (stream time base).
// Returns false at the end of the stream without any frame.
bool decodeUntil(AVFormatContext *input, AVCodecContext *codec, int streamIndex, int64_t target, AVFrame *frame,
                 const std::atomic<bool> *cancel)
{
    Packet packet(av_packet_alloc());
    bool haveFrame = false;
    bool draining = false;
    while (!cancelled(cancel)) {
        const int received = avcodec_receive_frame(codec, frame);
        if (received == 0) {
            haveFrame = true;
            const int64_t pts = frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE || pts >= target) {
                return true;
            }
            continue;
        }
        if (received == AVERROR_EOF) {
            return haveFrame; // the last frame decoded is the closest one
        }
        if (draining) {
            return haveFrame;
        }
        const int read = av_read_frame(input, packet.get());
        if (read < 0) {
            avcodec_send_packet(codec, nullptr); // flush the decoder's delayed frames
            draining = true;
            continue;
        }
        if (packet->stream_index == streamIndex) {
            avcodec_send_packet(codec, packet.get());
        }
        av_packet_unref(packet.get());
    }
    return false;
}

} // namespace

QImage extractThumbnailStrip(const QString &path, int count, int height, const std::atomic<bool> *cancel)
{
    Input input = openInput(path);
    if (!input || count < 1 || height < 2) {
        return {};
    }
    int streamIndex = -1;
    for (unsigned i = 0; i < input->nb_streams; ++i) {
        const AVStream *stream = input->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            streamIndex = static_cast<int>(i);
            break;
        }
    }
    if (streamIndex < 0) {
        return {};
    }
    AVStream *stream = input->streams[streamIndex];
    Codec codec = openDecoder(stream);
    if (!codec || codec->width <= 0 || codec->height <= 0) {
        return {};
    }
    // Size as displayed, then the size of the decoded (unrotated) picture that gives it.
    const int rotation = rotationOf(stream);
    const AVRational sar = stream->codecpar->sample_aspect_ratio.num > 0 ? stream->codecpar->sample_aspect_ratio
                                                                         : AVRational{1, 1};
    double displayWidth = codec->width * av_q2d(sar);
    double displayHeight = codec->height;
    const bool sideways = rotation == 90 || rotation == 270;
    if (sideways) {
        std::swap(displayWidth, displayHeight);
    }
    const int thumbWidth = even(height * displayWidth / displayHeight);
    const int scaledWidth = sideways ? height : thumbWidth;
    const int scaledHeight = sideways ? thumbWidth : height;

    const int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    int64_t duration = stream->duration;
    if (duration == AV_NOPTS_VALUE || duration <= 0) {
        duration = input->duration > 0 ? av_rescale_q(input->duration, AV_TIME_BASE_Q, stream->time_base) : 0;
    }
    if (duration <= 0) {
        count = 1; // a still picture
    }

    QImage strip(thumbWidth * count, height, QImage::Format_RGB32);
    strip.fill(Qt::black);
    Frame frame(av_frame_alloc());
    std::unique_ptr<SwsContext, SwsDeleter> scaler;
    for (int i = 0; i < count && !cancelled(cancel); ++i) {
        const int64_t target = start + (duration > 0 ? duration * (2 * i + 1) / (2 * count) : 0);
        if (i > 0 || target > start) {
            av_seek_frame(input.get(), streamIndex, target, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codec.get());
        }
        if (!decodeUntil(input.get(), codec.get(), streamIndex, target, frame.get(), cancel)) {
            continue; // leaves this frame black
        }
        scaler.reset(sws_getCachedContext(scaler.release(), frame->width, frame->height,
                                          static_cast<AVPixelFormat>(frame->format), scaledWidth, scaledHeight,
                                          AV_PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!scaler) {
            return {};
        }
        QImage scaled(scaledWidth, scaledHeight, QImage::Format_RGB32);
        uint8_t *destination[4] = {scaled.bits(), nullptr, nullptr, nullptr};
        const int destinationStride[4] = {static_cast<int>(scaled.bytesPerLine()), 0, 0, 0};
        sws_scale(scaler.get(), frame->data, frame->linesize, 0, frame->height, destination, destinationStride);
        if (rotation != 0) {
            scaled = scaled.transformed(QTransform().rotate(rotation));
        }
        for (int y = 0; y < height && y < scaled.height(); ++y) {
            std::copy_n(reinterpret_cast<const QRgb *>(scaled.constScanLine(y)), std::min(thumbWidth, scaled.width()),
                        reinterpret_cast<QRgb *>(strip.scanLine(y)) + i * thumbWidth);
        }
        av_frame_unref(frame.get());
    }
    return cancelled(cancel) ? QImage() : strip;
}

std::optional<Waveform> extractWaveform(const QString &path, int bucketsPerSecond, const std::atomic<bool> *cancel)
{
    Input input = openInput(path);
    if (!input) {
        return std::nullopt;
    }
    const int streamIndex = av_find_best_stream(input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        return std::nullopt;
    }
    Codec codec = openDecoder(input->streams[streamIndex]);
    if (!codec || codec->sample_rate <= 0) {
        return std::nullopt;
    }
    SwrContext *rawResampler = nullptr;
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    if (swr_alloc_set_opts2(&rawResampler, &mono, AV_SAMPLE_FMT_FLT, codec->sample_rate, &codec->ch_layout,
                            codec->sample_fmt, codec->sample_rate, 0, nullptr) < 0) {
        return std::nullopt;
    }
    std::unique_ptr<SwrContext, SwrDeleter> resampler(rawResampler);
    if (swr_init(resampler.get()) < 0) {
        return std::nullopt;
    }

    Waveform waveform;
    waveform.bucketsPerSecond = bucketsPerSecond;
    const int bucketSize = std::max(1, codec->sample_rate / bucketsPerSecond);
    float low = 0.0f;
    float high = 0.0f;
    int filled = 0;
    const auto push = [&] {
        const auto toInt8 = [](float value) { return static_cast<char>(std::lround(std::clamp(value, -1.0f, 1.0f) * 127.0f)); };
        waveform.peaks.append(toInt8(low));
        waveform.peaks.append(toInt8(high));
        low = high = 0.0f;
        filled = 0;
    };
    std::vector<float> mixed;
    const auto consume = [&](const uint8_t *const *data, int samples) {
        mixed.resize(static_cast<size_t>(swr_get_out_samples(resampler.get(), samples)));
        uint8_t *out[1] = {reinterpret_cast<uint8_t *>(mixed.data())};
        const int converted = swr_convert(resampler.get(), out, static_cast<int>(mixed.size()), data, samples);
        for (int i = 0; i < converted; ++i) {
            low = std::min(low, mixed[static_cast<size_t>(i)]);
            high = std::max(high, mixed[static_cast<size_t>(i)]);
            if (++filled == bucketSize) {
                push();
            }
        }
    };

    Packet packet(av_packet_alloc());
    Frame frame(av_frame_alloc());
    bool ended = false;
    while (!ended && !cancelled(cancel)) {
        if (av_read_frame(input.get(), packet.get()) < 0) {
            avcodec_send_packet(codec.get(), nullptr);
            ended = true;
        } else {
            if (packet->stream_index == streamIndex) {
                avcodec_send_packet(codec.get(), packet.get());
            }
            av_packet_unref(packet.get());
        }
        while (avcodec_receive_frame(codec.get(), frame.get()) == 0) {
            consume(frame->extended_data, frame->nb_samples);
            av_frame_unref(frame.get());
        }
    }
    if (cancelled(cancel)) {
        return std::nullopt;
    }
    consume(nullptr, 0); // samples still buffered in the resampler
    if (filled > 0) {
        push();
    }
    return waveform;
}

std::optional<fx::LoudnessResult> extractLoudness(const QString &path, const std::atomic<bool> *cancel)
{
    Input input = openInput(path);
    if (!input) {
        return std::nullopt;
    }
    const int streamIndex = av_find_best_stream(input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        return std::nullopt;
    }
    Codec codec = openDecoder(input->streams[streamIndex]);
    if (!codec || codec->sample_rate <= 0) {
        return std::nullopt;
    }
    SwrContext *rawResampler = nullptr;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    const int targetSampleRate = 48000;
    if (swr_alloc_set_opts2(&rawResampler, &stereo, AV_SAMPLE_FMT_FLT, targetSampleRate, &codec->ch_layout,
                            codec->sample_fmt, codec->sample_rate, 0, nullptr) < 0) {
        return std::nullopt;
    }
    std::unique_ptr<SwrContext, SwrDeleter> resampler(rawResampler);
    if (swr_init(resampler.get()) < 0) {
        return std::nullopt;
    }

    std::vector<float> allSamples;
    std::vector<float> chunk;
    const auto consume = [&](const uint8_t *const *data, int samples) {
        const int outCap = swr_get_out_samples(resampler.get(), samples);
        if (outCap <= 0) {
            return;
        }
        chunk.resize(static_cast<size_t>(outCap * 2));
        uint8_t *out[1] = {reinterpret_cast<uint8_t *>(chunk.data())};
        const int converted = swr_convert(resampler.get(), out, outCap, data, samples);
        if (converted > 0) {
            const size_t convertedFloats = static_cast<size_t>(converted * 2);
            allSamples.insert(allSamples.end(), chunk.begin(), chunk.begin() + convertedFloats);
        }
    };

    Packet packet(av_packet_alloc());
    Frame frame(av_frame_alloc());
    bool ended = false;
    while (!ended && !cancelled(cancel)) {
        if (av_read_frame(input.get(), packet.get()) < 0) {
            avcodec_send_packet(codec.get(), nullptr);
            ended = true;
        } else {
            if (packet->stream_index == streamIndex) {
                avcodec_send_packet(codec.get(), packet.get());
            }
            av_packet_unref(packet.get());
        }
        while (avcodec_receive_frame(codec.get(), frame.get()) == 0) {
            consume(frame->extended_data, frame->nb_samples);
            av_frame_unref(frame.get());
        }
    }
    if (cancelled(cancel)) {
        return std::nullopt;
    }
    consume(nullptr, 0); // flush buffered samples
    if (allSamples.empty()) {
        return std::nullopt;
    }
    return fx::measureLoudness(allSamples.data(), 2, targetSampleRate, static_cast<std::int64_t>(allSamples.size() / 2));
}

QImage extractFrame(const QString &path, double seconds, int maxHeight)
{
    Input input = openInput(path);
    if (!input) {
        return {};
    }
    int streamIndex = -1;
    for (unsigned i = 0; i < input->nb_streams; ++i) {
        const AVStream *stream = input->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            streamIndex = static_cast<int>(i);
            break;
        }
    }
    if (streamIndex < 0) {
        return {};
    }
    AVStream *stream = input->streams[streamIndex];
    Codec codec = openDecoder(stream);
    if (!codec || codec->width <= 0 || codec->height <= 0) {
        return {};
    }
    const int rotation = rotationOf(stream);
    const AVRational sar = stream->codecpar->sample_aspect_ratio.num > 0 ? stream->codecpar->sample_aspect_ratio
                                                                         : AVRational{1, 1};
    // Square pixels at the decoded height (or less), before the rotation.
    int height = codec->height;
    if (maxHeight > 0) {
        const bool sideways = rotation == 90 || rotation == 270;
        const double displayHeight = sideways ? codec->width * av_q2d(sar) : codec->height;
        if (displayHeight > maxHeight) {
            height = even(codec->height * maxHeight / displayHeight);
        }
    }
    const int width = even(height * codec->width * av_q2d(sar) / codec->height);

    const int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    const int64_t target = start + av_rescale_q(static_cast<int64_t>(std::llround(std::max(0.0, seconds) * AV_TIME_BASE)),
                                                AV_TIME_BASE_Q, stream->time_base);
    if (target > start) {
        av_seek_frame(input.get(), streamIndex, target, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec.get());
    }
    Frame frame(av_frame_alloc());
    if (!decodeUntil(input.get(), codec.get(), streamIndex, target, frame.get(), nullptr)) {
        return {};
    }
    std::unique_ptr<SwsContext, SwsDeleter> scaler(
        sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), width, height,
                       AV_PIX_FMT_RGB32, SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!scaler) {
        return {};
    }
    QImage image(width, height, QImage::Format_RGB32);
    uint8_t *destination[4] = {image.bits(), nullptr, nullptr, nullptr};
    const int destinationStride[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    sws_scale(scaler.get(), frame->data, frame->linesize, 0, frame->height, destination, destinationStride);
    return rotation != 0 ? image.transformed(QTransform().rotate(rotation)) : image;
}

} // namespace vedit::engine
