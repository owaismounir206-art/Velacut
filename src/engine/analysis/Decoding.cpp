// SPDX-License-Identifier: GPL-3.0-or-later
#include "Decoding.h"
#include "Spectrum.h"
#include "fx/Tracking.h"

#include <QFile>
#include <QTransform>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/mem.h>
#include <libavutil/tx.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace velacut::engine {

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

std::optional<Spectrum> extractSpectrum(const QString &path, int framesPerSecond, int bands, const std::atomic<bool> *cancel)
{
    if (framesPerSecond <= 0 || bands <= 0) {
        return std::nullopt;
    }
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
    if (swr_alloc_set_opts2(&rawResampler, &mono, AV_SAMPLE_FMT_FLT, Spectrum::kSampleRate, &codec->ch_layout,
                            codec->sample_fmt, codec->sample_rate, 0, nullptr) < 0) {
        return std::nullopt;
    }
    std::unique_ptr<SwrContext, SwrDeleter> resampler(rawResampler);
    if (swr_init(resampler.get()) < 0) {
        return std::nullopt;
    }

    constexpr int n = Spectrum::kWindow;
    AVTXContext *rawTx = nullptr;
    av_tx_fn transform = nullptr;
    const float scale = 1.0f;
    if (av_tx_init(&rawTx, &transform, AV_TX_FLOAT_RDFT, 0, n, &scale, 0) < 0) {
        return std::nullopt;
    }
    const std::unique_ptr<AVTXContext, void (*)(AVTXContext *)> tx(rawTx, [](AVTXContext *c) { av_tx_uninit(&c); });
    const auto freeBuffer = [](void *p) { av_free(p); };
    const std::unique_ptr<float, decltype(freeBuffer)> window(static_cast<float *>(av_malloc(n * sizeof(float))), freeBuffer);
    const std::unique_ptr<AVComplexFloat, decltype(freeBuffer)> bins(
        static_cast<AVComplexFloat *>(av_malloc((n / 2 + 1) * sizeof(AVComplexFloat))), freeBuffer);
    if (!window || !bins) {
        return std::nullopt;
    }
    std::vector<float> hann(n);
    for (int i = 0; i < n; ++i) {
        hann[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (n - 1));
    }
    // Band edges in FFT bins, log-spaced from kLowHz to kHighHz; a band narrower than a bin still gets one bin.
    std::vector<int> edges(static_cast<size_t>(bands) + 1);
    for (int b = 0; b <= bands; ++b) {
        const double hz = Spectrum::kLowHz * std::pow(Spectrum::kHighHz / Spectrum::kLowHz, static_cast<double>(b) / bands);
        edges[static_cast<size_t>(b)] = std::clamp(static_cast<int>(std::lround(hz * n / Spectrum::kSampleRate)), 1, n / 2);
    }
    // A full-scale sine through the Hann window peaks at n/4 in one bin: 0 dBFS.
    const double reference = std::pow(n / 4.0, 2.0);

    Spectrum spectrum;
    spectrum.framesPerSecond = framesPerSecond;
    spectrum.bands = bands;
    const double hop = static_cast<double>(Spectrum::kSampleRate) / framesPerSecond;
    // Frame f is centred on sample f × hop: its window starts at f × hop − n / 2 (zeros before the start).
    std::vector<float> samples; // samples from `base` on
    std::int64_t base = 0;
    std::int64_t total = 0;
    int frame = 0;
    const auto analyse = [&](bool flush) {
        for (;;) {
            const auto start = static_cast<std::int64_t>(std::llround(frame * hop)) - n / 2;
            if (!flush && start + n > total) {
                break;
            }
            if (flush && start >= total) {
                break;
            }
            for (int i = 0; i < n; ++i) {
                const std::int64_t index = start + i - base;
                const float value = index >= 0 && index < static_cast<std::int64_t>(samples.size())
                                        ? samples[static_cast<size_t>(index)] : 0.0f;
                window.get()[i] = value * hann[static_cast<size_t>(i)];
            }
            transform(tx.get(), bins.get(), window.get(), sizeof(AVComplexFloat));
            for (int b = 0; b < bands; ++b) {
                const int from = edges[static_cast<size_t>(b)];
                const int to = std::max(from + 1, edges[static_cast<size_t>(b) + 1]);
                double power = 0.0;
                for (int k = from; k < to && k <= n / 2; ++k) {
                    const AVComplexFloat c = bins.get()[k];
                    power = std::max(power, static_cast<double>(c.re) * c.re + static_cast<double>(c.im) * c.im);
                }
                const double db = power > 0.0 ? 10.0 * std::log10(power / reference) : Spectrum::kFloorDb;
                const double level = std::clamp((db - Spectrum::kFloorDb) / -Spectrum::kFloorDb, 0.0, 1.0);
                spectrum.levels.append(static_cast<char>(std::lround(level * 255.0)));
            }
            ++frame;
            // Samples before the next window are no longer needed.
            const std::int64_t keepFrom = static_cast<std::int64_t>(std::llround(frame * hop)) - n / 2;
            if (keepFrom - base > 8 * n) {
                const std::int64_t drop = keepFrom - base;
                samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(drop));
                base += drop;
            }
        }
    };
    std::vector<float> converted;
    const auto consume = [&](const uint8_t *const *data, int count) {
        converted.resize(static_cast<size_t>(std::max(0, swr_get_out_samples(resampler.get(), count))));
        uint8_t *out[1] = {reinterpret_cast<uint8_t *>(converted.data())};
        const int produced = swr_convert(resampler.get(), out, static_cast<int>(converted.size()), data, count);
        if (produced > 0) {
            samples.insert(samples.end(), converted.begin(), converted.begin() + produced);
            total += produced;
            analyse(false);
        }
    };

    Packet packet(av_packet_alloc());
    Frame decoded(av_frame_alloc());
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
        while (avcodec_receive_frame(codec.get(), decoded.get()) == 0) {
            consume(decoded->extended_data, decoded->nb_samples);
            av_frame_unref(decoded.get());
        }
    }
    if (cancelled(cancel)) {
        return std::nullopt;
    }
    consume(nullptr, 0);
    analyse(true);
    if (spectrum.levels.isEmpty()) {
        return std::nullopt;
    }
    return spectrum;
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

namespace {

// The share of the file done, from a timestamp of the stream.
double doneShare(const AVFormatContext *input, const AVStream *stream, int64_t pts)
{
    if (pts == AV_NOPTS_VALUE || input->duration <= 0) {
        return 0.0;
    }
    const double seconds = static_cast<double>(pts - (stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time)) *
                           av_q2d(stream->time_base);
    return std::clamp(seconds / (static_cast<double>(input->duration) / AV_TIME_BASE), 0.0, 1.0);
}

} // namespace

std::optional<std::vector<float>> extractLevels(const QString &path, int windowsPerSecond, const std::atomic<bool> *cancel,
                                                const DecodeProgress &progress)
{
    Input input = openInput(path);
    if (!input || windowsPerSecond <= 0) {
        return std::nullopt;
    }
    const int streamIndex = av_find_best_stream(input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        return std::nullopt;
    }
    const AVStream *stream = input->streams[streamIndex];
    Codec codec = openDecoder(stream);
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
    std::vector<float> levels;
    const int windowSize = std::max(1, codec->sample_rate / windowsPerSecond);
    double sum = 0.0;
    int filled = 0;
    const auto push = [&] {
        const double rms = std::sqrt(sum / std::max(1, filled));
        levels.push_back(rms > 1e-5 ? static_cast<float>(20.0 * std::log10(rms)) : -100.0f);
        sum = 0.0;
        filled = 0;
    };
    std::vector<float> mixed;
    const auto consume = [&](const uint8_t *const *data, int samples) {
        mixed.resize(static_cast<size_t>(std::max(0, swr_get_out_samples(resampler.get(), samples))));
        uint8_t *out[1] = {reinterpret_cast<uint8_t *>(mixed.data())};
        const int converted = swr_convert(resampler.get(), out, static_cast<int>(mixed.size()), data, samples);
        for (int i = 0; i < converted; ++i) {
            const double v = mixed[static_cast<size_t>(i)];
            sum += v * v;
            if (++filled == windowSize) {
                push();
            }
        }
    };
    Packet packet(av_packet_alloc());
    Frame frame(av_frame_alloc());
    bool ended = false;
    int64_t lastReport = 0;
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
            if (progress && frame->best_effort_timestamp - lastReport > stream->time_base.den / std::max(1, stream->time_base.num)) {
                lastReport = frame->best_effort_timestamp;
                progress(doneShare(input.get(), stream, frame->best_effort_timestamp));
            }
            av_frame_unref(frame.get());
        }
    }
    if (cancelled(cancel)) {
        return std::nullopt;
    }
    consume(nullptr, 0);
    if (filled > 0) {
        push();
    }
    return levels;
}

namespace {

// Decodes the video of `path` from `fromSeconds` to `toSeconds` (file time) as grey pictures of `width` × `height`,
// calling `take` with each picture and its time. False if the file has no readable video or the work was cancelled.
bool decodeGreyFrames(const QString &path, int width, int height, double fromSeconds, double toSeconds,
                      const std::atomic<bool> *cancel, const DecodeProgress &progress, AVRational *frameRate,
                      const std::function<void(const std::vector<uint8_t> &, double)> &take)
{
    Input input = openInput(path);
    if (!input) {
        return false;
    }
    const int streamIndex = av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        return false;
    }
    AVStream *stream = input->streams[streamIndex];
    Codec codec = openDecoder(stream);
    if (!codec) {
        return false;
    }
    if (frameRate) {
        const AVRational rate = stream->avg_frame_rate.num > 0 ? stream->avg_frame_rate : stream->r_frame_rate;
        *frameRate = rate.num > 0 && rate.den > 0 ? rate : AVRational{30, 1};
    }
    const int64_t start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    if (fromSeconds > 0.0) {
        const auto target = static_cast<int64_t>(fromSeconds / av_q2d(stream->time_base)) + start;
        av_seek_frame(input.get(), streamIndex, target, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec.get());
    }
    const double span = std::max(1e-3, (std::isfinite(toSeconds) ? toSeconds
                                                                  : (input->duration > 0 ? double(input->duration) / AV_TIME_BASE : 1.0)) -
                                           fromSeconds);
    std::unique_ptr<SwsContext, SwsDeleter> scaler;
    int scalerFormat = -1;
    int scalerWidth = 0;
    int scalerHeight = 0;
    std::vector<uint8_t> grey(static_cast<size_t>(width) * static_cast<size_t>(height));
    Packet packet(av_packet_alloc());
    Frame frame(av_frame_alloc());
    bool ended = false;
    bool past = false;
    int taken = 0;
    while (!ended && !past && !cancelled(cancel)) {
        if (av_read_frame(input.get(), packet.get()) < 0) {
            avcodec_send_packet(codec.get(), nullptr);
            ended = true;
        } else {
            if (packet->stream_index == streamIndex) {
                avcodec_send_packet(codec.get(), packet.get());
            }
            av_packet_unref(packet.get());
        }
        while (!past && avcodec_receive_frame(codec.get(), frame.get()) == 0) {
            const int64_t pts = frame->best_effort_timestamp;
            const double seconds = pts == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(pts - start) * av_q2d(stream->time_base);
            if (seconds + 1e-6 < fromSeconds) {
                av_frame_unref(frame.get());
                continue;
            }
            if (seconds > toSeconds) {
                past = true;
                break;
            }
            if (!scaler || scalerFormat != frame->format || scalerWidth != frame->width || scalerHeight != frame->height) {
                scaler.reset(sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), width, height,
                                            AV_PIX_FMT_GRAY8, SWS_AREA, nullptr, nullptr, nullptr));
                scalerFormat = frame->format;
                scalerWidth = frame->width;
                scalerHeight = frame->height;
            }
            if (scaler) {
                uint8_t *planes[1] = {grey.data()};
                const int strides[1] = {width};
                sws_scale(scaler.get(), frame->data, frame->linesize, 0, frame->height, planes, strides);
                take(grey, seconds);
                if (progress && ++taken % 30 == 0) {
                    progress(std::clamp((seconds - fromSeconds) / span, 0.0, 1.0));
                }
            }
            av_frame_unref(frame.get());
        }
    }
    return !cancelled(cancel);
}

} // namespace

std::optional<std::vector<float>> extractFrameDifferences(const QString &path, std::vector<double> *times,
                                                          const std::atomic<bool> *cancel, const DecodeProgress &progress)
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 36;
    constexpr int kBins = 32;
    std::vector<uint8_t> previous;
    std::array<float, kBins> previousHistogram{};
    std::vector<float> differences;
    const bool ok = decodeGreyFrames(path, kWidth, kHeight, 0.0, std::numeric_limits<double>::infinity(), cancel, progress,
                                     nullptr, [&](const std::vector<uint8_t> &current, double seconds) {
        std::array<float, kBins> histogram{};
        for (uint8_t value : current) {
            histogram[static_cast<size_t>(value * kBins / 256)] += 1.0f / static_cast<float>(current.size());
        }
        float difference = 0.0f;
        if (!previous.empty()) {
            double pixels = 0.0;
            for (size_t i = 0; i < current.size(); ++i) {
                pixels += std::abs(int(current[i]) - int(previous[i]));
            }
            float bins = 0.0f;
            for (size_t b = 0; b < histogram.size(); ++b) {
                bins += std::abs(histogram[b] - previousHistogram[b]);
            }
            // Both in 0…1: the pixels say "something moved", the histogram says "it is another picture".
            difference = static_cast<float>(0.5 * pixels / (255.0 * current.size()) + 0.25 * bins);
        }
        differences.push_back(std::clamp(difference, 0.0f, 1.0f));
        if (times) {
            times->push_back(seconds);
        }
        previous = current;
        previousHistogram = histogram;
    });
    if (!ok || (differences.empty() && !cancelled(cancel))) {
        return std::nullopt;
    }
    return differences;
}

std::optional<std::vector<fx::CameraStep>> extractCameraSteps(const QString &path, double fromSeconds, double toSeconds,
                                                              Rational *frameRate, const std::atomic<bool> *cancel,
                                                              const DecodeProgress &progress)
{
    AVRational rate{30, 1};
    constexpr int kWidth = 160;
    constexpr int kHeight = 90;
    std::vector<uint8_t> previous;
    std::vector<fx::CameraStep> steps;
    const bool ok = decodeGreyFrames(path, kWidth, kHeight, fromSeconds, toSeconds, cancel, progress, &rate,
                                     [&](const std::vector<uint8_t> &current, double) {
        steps.push_back(previous.empty() ? fx::CameraStep{} : fx::estimateCameraStep(previous, current, kWidth, kHeight));
        previous = current;
    });
    if (!ok || steps.empty()) {
        return std::nullopt;
    }
    if (frameRate) {
        *frameRate = Rational(rate.num, rate.den);
    }
    return steps;
}

std::optional<std::vector<fx::SubjectPoint>> extractSubjectPath(const QString &path, double fromSeconds, double toSeconds,
                                                                int samplesPerSecond, std::vector<double> *times,
                                                                const std::atomic<bool> *cancel, const DecodeProgress &progress)
{
    constexpr int kWidth = 160;
    constexpr int kHeight = 90;
    std::vector<uint8_t> previous;
    std::vector<fx::SubjectPoint> points;
    double next = fromSeconds;
    const double step = 1.0 / std::max(1, samplesPerSecond);
    const bool ok = decodeGreyFrames(path, kWidth, kHeight, fromSeconds, toSeconds, cancel, progress, nullptr,
                                     [&](const std::vector<uint8_t> &current, double seconds) {
        // A point every `step`, from this frame and the one before (what moves).
        if (seconds + 1e-6 >= next && !previous.empty()) {
            points.push_back(fx::findSubject(previous, current, kWidth, kHeight));
            if (times) {
                times->push_back(seconds);
            }
            next += step;
        }
        previous = current;
    });
    if (!ok || points.empty()) {
        return std::nullopt;
    }
    return points;
}

std::optional<std::vector<ShotSample>> extractShotSamples(const QString &path, int samplesPerSecond, double *seconds,
                                                          const std::atomic<bool> *cancel)
{
    constexpr int kWidth = 160;
    constexpr int kHeight = 90;
    std::vector<uint8_t> previous;
    std::vector<ShotSample> samples;
    double next = 0.0;
    double last = 0.0;
    const double step = 1.0 / std::max(1, samplesPerSecond);
    const bool ok = decodeGreyFrames(path, kWidth, kHeight, 0.0, std::numeric_limits<double>::infinity(), cancel, {}, nullptr,
                                     [&](const std::vector<uint8_t> &current, double time) {
        last = time;
        if (time + 1e-6 < next) {
            return;
        }
        next += step;
        double gradient = 0.0;
        double sum = 0.0;
        for (int y = 1; y < kHeight - 1; ++y) {
            for (int x = 1; x < kWidth - 1; ++x) {
                const int at = y * kWidth + x;
                gradient += std::abs(int(current[static_cast<size_t>(at + 1)]) - int(current[static_cast<size_t>(at - 1)])) +
                            std::abs(int(current[static_cast<size_t>(at + kWidth)]) - int(current[static_cast<size_t>(at - kWidth)]));
                sum += current[static_cast<size_t>(at)];
            }
        }
        const double pixels = (kWidth - 2.0) * (kHeight - 2.0);
        double motion = 0.0;
        if (previous.size() == current.size()) {
            for (size_t i = 0; i < current.size(); ++i) {
                motion += std::abs(int(current[i]) - int(previous[i]));
            }
            motion /= 255.0 * static_cast<double>(current.size());
        }
        samples.push_back(ShotSample{time, static_cast<float>(std::min(1.0, gradient / pixels / 64.0)),
                                     static_cast<float>(sum / pixels / 255.0), static_cast<float>(motion)});
        previous = current;
    });
    if (!ok || samples.empty()) {
        return std::nullopt;
    }
    if (seconds) {
        *seconds = last + step;
    }
    return samples;
}

std::optional<std::vector<TrackedPoint>> extractTrackedPath(const QString &path, double fromSeconds, double toSeconds,
                                                            double x, double y, double radius, double storedAspect,
                                                            const std::atomic<bool> *cancel, const DecodeProgress &progress)
{
    constexpr int kWidth = 320;
    const int height = std::max(16, static_cast<int>(std::lround(kWidth / std::max(0.1, storedAspect) / 2.0)) * 2);
    std::optional<fx::PointTracker> tracker;
    std::vector<TrackedPoint> points;
    const bool ok = decodeGreyFrames(path, kWidth, height, fromSeconds, toSeconds, cancel, progress, nullptr,
                                     [&](const std::vector<uint8_t> &frame, double seconds) {
        if (!tracker) {
            tracker.emplace(frame, kWidth, height, x * kWidth, y * height,
                            std::max(4, static_cast<int>(std::lround(radius * kWidth))));
            points.push_back(TrackedPoint{seconds, x, y, false});
            return;
        }
        const fx::PointTracker::Point point = tracker->update(frame);
        points.push_back(TrackedPoint{seconds, point.x / kWidth, point.y / height, point.lost});
    });
    if (!ok || points.empty()) {
        return std::nullopt;
    }
    return points;
}

} // namespace velacut::engine
