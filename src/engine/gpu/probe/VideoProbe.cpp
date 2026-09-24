// SPDX-License-Identifier: GPL-3.0-or-later
#include "Probes.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}


#include <map>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;

namespace vedit::gpu::probe {

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kFrames = 5;

struct BufferDeleter
{
    void operator()(AVBufferRef *buffer) const { av_buffer_unref(&buffer); }
};
struct CodecContextDeleter
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
using BufferPtr = std::unique_ptr<AVBufferRef, BufferDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

struct DeviceKind
{
    AVHWDeviceType type;
    AVPixelFormat hwFormat;
    const char *name;
};

const DeviceKind kDevices[] = {
    {AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI, "vaapi"},
    {AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, "cuda"},
    {AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, "qsv"},
    {AV_HWDEVICE_TYPE_VULKAN, AV_PIX_FMT_VULKAN, "vulkan"},
};

FramePtr makeSoftwareFrame(AVPixelFormat format, int index)
{
    FramePtr frame(av_frame_alloc());
    frame->format = format;
    frame->width = kWidth;
    frame->height = kHeight;
    if (av_frame_get_buffer(frame.get(), 0) < 0) {
        return nullptr;
    }
    // A moving gradient, so encoders produce real (non-trivial) data.
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            frame->data[0][y * frame->linesize[0] + x] = static_cast<uint8_t>((x + y + index * 8) & 0xff);
        }
    }
    const int chromaHeight = kHeight / 2;
    if (format == AV_PIX_FMT_NV12) {
        for (int y = 0; y < chromaHeight; ++y) {
            std::fill_n(frame->data[1] + y * frame->linesize[1], kWidth, static_cast<uint8_t>(128));
        }
    } else {
        for (int plane = 1; plane <= 2; ++plane) {
            for (int y = 0; y < chromaHeight; ++y) {
                std::fill_n(frame->data[plane] + y * frame->linesize[plane], kWidth / 2, static_cast<uint8_t>(128));
            }
        }
    }
    frame->pts = index;
    return frame;
}

// Encodes kFrames frames; returns the packets (empty on failure). `device` null = software encoder.
std::vector<PacketPtr> encode(const AVCodec *codec, const DeviceKind *device, AVBufferRef *deviceContext)
{
    std::vector<PacketPtr> packets;
    CodecContextPtr context(avcodec_alloc_context3(codec));
    context->width = kWidth;
    context->height = kHeight;
    context->time_base = AVRational{1, 30};
    context->framerate = AVRational{30, 1};
    context->gop_size = kFrames;
    context->max_b_frames = 0;
    BufferPtr frames;
    const bool uploadToDevice = device && device->type != AV_HWDEVICE_TYPE_CUDA;
    if (uploadToDevice) {
        frames.reset(av_hwframe_ctx_alloc(deviceContext));
        auto *framesContext = reinterpret_cast<AVHWFramesContext *>(frames->data);
        framesContext->format = device->hwFormat;
        framesContext->sw_format = AV_PIX_FMT_NV12;
        framesContext->width = kWidth;
        framesContext->height = kHeight;
        framesContext->initial_pool_size = 8;
        if (av_hwframe_ctx_init(frames.get()) < 0) {
            return packets;
        }
        context->pix_fmt = device->hwFormat;
        context->hw_frames_ctx = av_buffer_ref(frames.get());
    } else if (device) {
        context->pix_fmt = AV_PIX_FMT_NV12; // NVENC takes system-memory frames
        context->hw_device_ctx = av_buffer_ref(deviceContext);
    } else {
        context->pix_fmt = AV_PIX_FMT_YUV420P;
        // Fastest settings of each software encoder (only a few frames of test stream are needed).
        const QByteArray name(codec->name);
        if (name == "libx264" || name == "libx265") {
            av_opt_set(context->priv_data, "preset", "ultrafast", 0);
        }
        if (name == "libx265") {
            av_opt_set(context->priv_data, "x265-params", "log-level=none", 0);
        }
        if (name == "libsvtav1") {
            av_opt_set(context->priv_data, "preset", "12", 0);
        }
        if (name == "libvpx-vp9" || name == "libaom-av1") {
            av_opt_set(context->priv_data, "deadline", "realtime", 0);
            av_opt_set_int(context->priv_data, "cpu-used", 8, 0);
        }
    }
    if (avcodec_open2(context.get(), codec, nullptr) < 0) {
        return packets;
    }
    PacketPtr packet(av_packet_alloc());
    const auto drain = [&]() {
        while (avcodec_receive_packet(context.get(), packet.get()) == 0) {
            packets.emplace_back(av_packet_clone(packet.get()));
            av_packet_unref(packet.get());
        }
    };
    for (int i = 0; i < kFrames; ++i) {
        FramePtr software = makeSoftwareFrame(uploadToDevice || device ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P, i);
        if (!software) {
            return {};
        }
        FramePtr input;
        if (uploadToDevice) {
            input.reset(av_frame_alloc());
            if (av_hwframe_get_buffer(frames.get(), input.get(), 0) < 0 ||
                av_hwframe_transfer_data(input.get(), software.get(), 0) < 0) {
                return {};
            }
            input->pts = i;
        } else {
            input = std::move(software);
        }
        if (avcodec_send_frame(context.get(), input.get()) < 0) {
            return {};
        }
        drain();
    }
    avcodec_send_frame(context.get(), nullptr);
    drain();
    return packets;
}

AVPixelFormat pickHardwareFormat(AVCodecContext *context, const AVPixelFormat *formats)
{
    const auto wanted = static_cast<AVPixelFormat>(reinterpret_cast<intptr_t>(context->opaque));
    for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == wanted) {
            return *format;
        }
    }
    return formats[0]; // software fallback: detected as "not working" below
}

bool decodesInHardware(AVCodecID codecId, const DeviceKind &device, AVBufferRef *deviceContext,
                       const std::vector<PacketPtr> &packets)
{
    // The default decoder of a codec may be a software library without hwaccel (e.g. libdav1d for AV1):
    // pick the first decoder of the codec that supports this device type.
    const AVCodec *decoder = nullptr;
    void *iterator = nullptr;
    while (const AVCodec *candidate = av_codec_iterate(&iterator)) {
        if (!av_codec_is_decoder(candidate) || candidate->id != codecId) {
            continue;
        }
        for (int i = 0; const AVCodecHWConfig *config = avcodec_get_hw_config(candidate, i); ++i) {
            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == device.type) {
                decoder = candidate;
                break;
            }
        }
        if (decoder) {
            break;
        }
    }
    if (!decoder) {
        return false;
    }
    CodecContextPtr context(avcodec_alloc_context3(decoder));
    context->hw_device_ctx = av_buffer_ref(deviceContext);
    context->opaque = reinterpret_cast<void *>(static_cast<intptr_t>(device.hwFormat));
    context->get_format = pickHardwareFormat;
    if (avcodec_open2(context.get(), decoder, nullptr) < 0) {
        return false;
    }
    FramePtr frame(av_frame_alloc());
    bool hardwareFrame = false;
    const auto receive = [&]() {
        while (avcodec_receive_frame(context.get(), frame.get()) == 0) {
            hardwareFrame = hardwareFrame || frame->format == device.hwFormat;
            av_frame_unref(frame.get());
        }
    };
    for (const PacketPtr &packet : packets) {
        if (avcodec_send_packet(context.get(), packet.get()) < 0) {
            return false;
        }
        receive();
    }
    avcodec_send_packet(context.get(), nullptr);
    receive();
    return hardwareFrame;
}

const AVCodec *firstEncoder(std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (const AVCodec *codec = avcodec_find_encoder_by_name(name)) {
            return codec;
        }
    }
    return nullptr;
}

} // namespace

VideoInfo probeVideo()
{
    av_log_set_level(AV_LOG_QUIET);
    VideoInfo info;
    std::map<QString, BufferPtr> opened;
    for (const DeviceKind &device : kDevices) {
        AVBufferRef *context = nullptr;
        if (av_hwdevice_ctx_create(&context, device.type, nullptr, nullptr, 0) == 0) {
            opened.emplace(QString::fromLatin1(device.name), BufferPtr(context));
            info.devices.append(QString::fromLatin1(device.name));
        }
    }

    // Encoders: a real encode of a few frames on each available device.
    const char *families[] = {"h264", "hevc", "av1"};
    const std::pair<const char *, const char *> encoderDevices[] = {
        {"vaapi", "vaapi"}, {"qsv", "qsv"}, {"vulkan", "vulkan"}, {"nvenc", "cuda"}};
    for (const auto &[suffix, deviceName] : encoderDevices) {
        const auto it = opened.find(QString::fromLatin1(deviceName));
        if (it == opened.end()) {
            continue;
        }
        const DeviceKind *device = nullptr;
        for (const DeviceKind &kind : kDevices) {
            if (qstrcmp(kind.name, deviceName) == 0) {
                device = &kind;
            }
        }
        for (const char *family : families) {
            const QByteArray name = QByteArray(family) + '_' + suffix;
            const AVCodec *codec = avcodec_find_encoder_by_name(name.constData());
            if (codec && !encode(codec, device, it->second.get()).empty()) {
                info.encoders.append(QString::fromLatin1(name));
            }
        }
    }

    // Decoders: encode a short stream in software, decode it on each device.
    const std::pair<AVCodecID, const AVCodec *> streams[] = {
        {AV_CODEC_ID_H264, firstEncoder({"libx264"})},
        {AV_CODEC_ID_HEVC, firstEncoder({"libx265"})},
        {AV_CODEC_ID_VP9, firstEncoder({"libvpx-vp9"})},
        {AV_CODEC_ID_AV1, firstEncoder({"libsvtav1", "libaom-av1"})},
    };
    for (const auto &[codecId, encoder] : streams) {
        if (!encoder || opened.empty()) {
            continue;
        }
        const std::vector<PacketPtr> packets = encode(encoder, nullptr, nullptr);
        if (packets.empty()) {
            continue;
        }
        for (const DeviceKind &device : kDevices) {
            const auto it = opened.find(QString::fromLatin1(device.name));
            if (it != opened.end() && decodesInHardware(codecId, device, it->second.get(), packets)) {
                info.decoders.append(QString::fromLatin1(avcodec_get_name(codecId)) + u'@' + QString::fromLatin1(device.name));
            }
        }
    }
    info.status = ProbeStatus::Ok;
    return info;
}

} // namespace vedit::gpu::probe
