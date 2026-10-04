// SPDX-License-Identifier: GPL-3.0-or-later
#include "HwVideoDecoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QThread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcHwDec, "vedit.engine.hwdec")

namespace vedit::gpu {

namespace {

enum AVPixelFormat pickHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat target = static_cast<enum AVPixelFormat>(reinterpret_cast<intptr_t>(ctx->opaque));
    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == target) {
            return target;
        }
    }
    return AV_PIX_FMT_NONE;
}

} // namespace

QString decoderTierName(DecoderTier tier)
{
    switch (tier) {
    case DecoderTier::DedicatedHardware:
        return QStringLiteral("Dedicated Hardware (AMF/QSV/oneVPL)");
    case DecoderTier::GenericHardware:
        return QStringLiteral("Generic Hardware (VA-API/D3D11VA/Vulkan)");
    case DecoderTier::SoftwareCpu:
        return QStringLiteral("Software CPU Multithreaded");
    }
    return QStringLiteral("Unknown");
}

QString targetArchitectureName(TargetGpuArchitecture arch)
{
    switch (arch) {
    case TargetGpuArchitecture::AmdRdna3_Phoenix2:
        return QStringLiteral("AMD Radeon 740M (RDNA3 / VCN 4.x / Phoenix2)");
    case TargetGpuArchitecture::IntelXe2_LunarLake:
        return QStringLiteral("Intel Arc 130V (Xe2 Battlemage / Lunar Lake)");
    case TargetGpuArchitecture::Generic:
        return QStringLiteral("Generic Architecture");
    }
    return QStringLiteral("Generic");
}

HwVideoDecoder::HwVideoDecoder(DecoderConfig config)
    : m_config(config)
    , m_cache(config.lruCacheCapacity)
{
    if (m_config.forceArchitecture.has_value()) {
        m_architecture = *m_config.forceArchitecture;
    } else {
        m_architecture = detectTargetArchitecture();
    }
}

HwVideoDecoder::~HwVideoDecoder()
{
    close();
}

TargetGpuArchitecture HwVideoDecoder::detectTargetArchitecture()
{
    // Check PCI DRM devices or CPU info for AMD Phoenix2 / Intel Lunar Lake
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString &card : drm.entryList({QStringLiteral("card?"), QStringLiteral("card??")}, QDir::Dirs | QDir::System)) {
        const QString devPath = drm.filePath(card) + QStringLiteral("/device");
        QFile vendorFile(devPath + QStringLiteral("/vendor"));
        QFile deviceFile(devPath + QStringLiteral("/device"));
        if (vendorFile.open(QIODevice::ReadOnly) && deviceFile.open(QIODevice::ReadOnly)) {
            const QString vendor = QString::fromUtf8(vendorFile.readAll()).trimmed().toLower();
            const QString device = QString::fromUtf8(deviceFile.readAll()).trimmed().toLower();
            if (vendor == QStringLiteral("0x1002")) {
                // AMD vendor ID: Phoenix2 / RDNA3 (e.g. 0x15bf, 0x15c8, 0x1900...)
                return TargetGpuArchitecture::AmdRdna3_Phoenix2;
            }
            if (vendor == QStringLiteral("0x8086")) {
                // Intel vendor ID: Lunar Lake / Battlemage
                return TargetGpuArchitecture::IntelXe2_LunarLake;
            }
        }
    }
    return TargetGpuArchitecture::Generic;
}

bool HwVideoDecoder::isOpen() const
{
    return m_formatContext != nullptr && m_codecContext != nullptr && m_videoStreamIndex >= 0;
}

void HwVideoDecoder::close()
{
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_hwDeviceContext) {
        av_buffer_unref(&m_hwDeviceContext);
    }
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
    }
    m_cache.clear();
    m_videoStreamIndex = -1;
    m_hwPixelFormat = -1;
    m_filePath.clear();
}

bool HwVideoDecoder::isColorFormatAccelerated(int pixelFormat, int profile) const
{
    // Hardware decoders on typical iGPUs (VCN 4.x / Intel Xe2) support 4:2:0 8-bit (NV12) and 10-bit (P010).
    // Chroma subsampling 4:2:2 and 4:4:4 or unaccelerated High 10 profiles are not hardware accelerated.
    const auto fmt = static_cast<AVPixelFormat>(pixelFormat);
    if (fmt == AV_PIX_FMT_YUV422P || fmt == AV_PIX_FMT_YUV422P10LE || fmt == AV_PIX_FMT_YUV422P10BE ||
        fmt == AV_PIX_FMT_YUV444P || fmt == AV_PIX_FMT_YUV444P10LE || fmt == AV_PIX_FMT_YUV444P10BE ||
        fmt == AV_PIX_FMT_YUV422P12LE || fmt == AV_PIX_FMT_YUV444P12LE) {
        return false;
    }
    // High 4:2:2 and High 4:4:4 profiles for H.264
    if (profile == AV_PROFILE_H264_HIGH_422 || profile == AV_PROFILE_H264_HIGH_444_PREDICTIVE) {
        return false;
    }
    return true;
}

bool HwVideoDecoder::open(const QString &filePath)
{
    close();
    m_filePath = filePath;
    m_diagnostics.clear();

    if (!QFileInfo::exists(filePath)) {
        m_diagnostics << QStringLiteral("File does not exist: ") + filePath;
        return false;
    }

    if (!initStream()) {
        close();
        return false;
    }

    // Cascading Fallback Hierarchy:
    // 1. Primary Dedicated Hardware (AMF on Windows / QSV on Intel)
    // 2. Generic Hardware (VA-API on Linux / D3D11VA on Windows)
    // 3. Software Multithreaded CPU
    bool initialized = false;
    if (m_config.enableHardware) {
        if (tryInitTier(DecoderTier::DedicatedHardware)) {
            initialized = true;
        } else if (tryInitTier(DecoderTier::GenericHardware)) {
            initialized = true;
        }
    }

    if (!initialized) {
        fallbackToSoftware(QStringLiteral("Initial hardware decoder allocation unavailable or unaccelerated"));
        initialized = tryInitTier(DecoderTier::SoftwareCpu);
    }

    return initialized;
}

bool HwVideoDecoder::initStream()
{
    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, QFile::encodeName(m_filePath).constData(), nullptr, nullptr) < 0) {
        m_diagnostics << QStringLiteral("avformat_open_input failed for ") + m_filePath;
        return false;
    }
    m_formatContext = fmtCtx;

    if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
        m_diagnostics << QStringLiteral("avformat_find_stream_info failed");
        return false;
    }

    m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIndex < 0) {
        m_diagnostics << QStringLiteral("No video stream found");
        return false;
    }

    const AVStream *stream = m_formatContext->streams[m_videoStreamIndex];
    m_videoSize = QSize(stream->codecpar->width, stream->codecpar->height);
    if (stream->r_frame_rate.den > 0 && stream->r_frame_rate.num > 0) {
        m_frameRate = av_q2d(stream->r_frame_rate);
    } else if (stream->avg_frame_rate.den > 0 && stream->avg_frame_rate.num > 0) {
        m_frameRate = av_q2d(stream->avg_frame_rate);
    } else {
        m_frameRate = 30.0;
    }

    if (stream->duration != AV_NOPTS_VALUE && stream->time_base.den > 0) {
        m_durationUs = av_rescale_q(stream->duration, stream->time_base, AV_TIME_BASE_Q);
    } else if (m_formatContext->duration != AV_NOPTS_VALUE) {
        m_durationUs = m_formatContext->duration;
    } else {
        m_durationUs = 0;
    }
    m_durationFrames = static_cast<int64_t>(std::llround(m_durationUs * m_frameRate / 1000000.0));

    return true;
}

bool HwVideoDecoder::tryInitTier(DecoderTier tier)
{
    switch (tier) {
    case DecoderTier::DedicatedHardware:
        return initDedicatedHw();
    case DecoderTier::GenericHardware:
        return initGenericHw();
    case DecoderTier::SoftwareCpu:
        return initSoftwareCpu();
    }
    return false;
}

bool HwVideoDecoder::initDedicatedHw()
{
    const AVStream *stream = m_formatContext->streams[m_videoStreamIndex];
    if (!isColorFormatAccelerated(stream->codecpar->format, stream->codecpar->profile)) {
        m_diagnostics << QStringLiteral("Color format / profile not hardware accelerated (4:2:2 / 4:4:4 / High 10)");
        return false;
    }

    AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;

#if defined(Q_OS_WIN)
    // Windows: AMF for AMD, QSV/oneVPL for Intel
    if (m_architecture == TargetGpuArchitecture::AmdRdna3_Phoenix2) {
        hwType = AV_HWDEVICE_TYPE_D3D11VA;
        hwPixFmt = AV_PIX_FMT_D3D11;
        m_activeDeviceName = QStringLiteral("AMF / Direct3D11 Video");
    } else if (m_architecture == TargetGpuArchitecture::IntelXe2_LunarLake) {
        hwType = AV_HWDEVICE_TYPE_QSV;
        hwPixFmt = AV_PIX_FMT_QSV;
        m_activeDeviceName = QStringLiteral("Intel QSV / oneVPL");
    }
#elif defined(Q_OS_LINUX)
    // Linux: oneVPL / QSV for Intel
    if (m_architecture == TargetGpuArchitecture::IntelXe2_LunarLake) {
        hwType = AV_HWDEVICE_TYPE_QSV;
        hwPixFmt = AV_PIX_FMT_QSV;
        m_activeDeviceName = QStringLiteral("Intel oneVPL / QSV");
    }
#endif

    if (hwType == AV_HWDEVICE_TYPE_NONE) {
        return false;
    }

    AVBufferRef *deviceCtx = nullptr;
    if (av_hwdevice_ctx_create(&deviceCtx, hwType, nullptr, nullptr, 0) < 0) {
        m_diagnostics << QStringLiteral("Dedicated HW device creation failed");
        return false;
    }

    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        av_buffer_unref(&deviceCtx);
        return false;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(decoder);
    if (!codecCtx || avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) {
        av_buffer_unref(&deviceCtx);
        if (codecCtx) avcodec_free_context(&codecCtx);
        return false;
    }

    codecCtx->hw_device_ctx = av_buffer_ref(deviceCtx);
    codecCtx->opaque = reinterpret_cast<void *>(static_cast<intptr_t>(hwPixFmt));
    codecCtx->get_format = pickHwFormat;
    codecCtx->pkt_timebase = stream->time_base;

    if (avcodec_open2(codecCtx, decoder, nullptr) < 0) {
        m_diagnostics << QStringLiteral("Dedicated HW avcodec_open2 failed");
        av_buffer_unref(&deviceCtx);
        avcodec_free_context(&codecCtx);
        return false;
    }

    m_codecContext = codecCtx;
    m_hwDeviceContext = deviceCtx;
    m_hwPixelFormat = hwPixFmt;
    m_activeTier = DecoderTier::DedicatedHardware;
    qCDebug(lcHwDec) << "Initialized Dedicated HW decoder tier:" << m_activeDeviceName;
    return true;
}

bool HwVideoDecoder::initGenericHw()
{
    const AVStream *stream = m_formatContext->streams[m_videoStreamIndex];
    if (!isColorFormatAccelerated(stream->codecpar->format, stream->codecpar->profile)) {
        m_diagnostics << QStringLiteral("Color format / profile not hardware accelerated on Generic HW");
        return false;
    }

    AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
    const char *driverName = nullptr;

#if defined(Q_OS_LINUX)
    hwType = AV_HWDEVICE_TYPE_VAAPI;
    hwPixFmt = AV_PIX_FMT_VAAPI;
    if (m_architecture == TargetGpuArchitecture::AmdRdna3_Phoenix2) {
        driverName = "radeonsi";
        m_activeDeviceName = QStringLiteral("AMD VA-API (radeonsi_drv_video.so)");
    } else if (m_architecture == TargetGpuArchitecture::IntelXe2_LunarLake) {
        driverName = "iHD";
        m_activeDeviceName = QStringLiteral("Intel VA-API (iHD_drv_video.so)");
    } else {
        m_activeDeviceName = QStringLiteral("Generic VA-API");
    }
#elif defined(Q_OS_WIN)
    hwType = AV_HWDEVICE_TYPE_D3D11VA;
    hwPixFmt = AV_PIX_FMT_D3D11;
    m_activeDeviceName = QStringLiteral("Direct3D11 Video");
#else
    hwType = AV_HWDEVICE_TYPE_VULKAN;
    hwPixFmt = AV_PIX_FMT_VULKAN;
    m_activeDeviceName = QStringLiteral("Vulkan Video");
#endif

    AVBufferRef *deviceCtx = nullptr;
    AVDictionary *opts = nullptr;
    if (driverName) {
        av_dict_set(&opts, "connection_type", "drm", 0);
    }
    const int err = av_hwdevice_ctx_create(&deviceCtx, hwType, nullptr, opts, 0);
    if (opts) {
        av_dict_free(&opts);
    }
    if (err < 0) {
        m_diagnostics << QStringLiteral("Generic HW device creation failed");
        return false;
    }

    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        av_buffer_unref(&deviceCtx);
        return false;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(decoder);
    if (!codecCtx || avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) {
        av_buffer_unref(&deviceCtx);
        if (codecCtx) avcodec_free_context(&codecCtx);
        return false;
    }

    codecCtx->hw_device_ctx = av_buffer_ref(deviceCtx);
    codecCtx->opaque = reinterpret_cast<void *>(static_cast<intptr_t>(hwPixFmt));
    codecCtx->get_format = pickHwFormat;
    codecCtx->pkt_timebase = stream->time_base;

    if (avcodec_open2(codecCtx, decoder, nullptr) < 0) {
        m_diagnostics << QStringLiteral("Generic HW avcodec_open2 failed");
        av_buffer_unref(&deviceCtx);
        avcodec_free_context(&codecCtx);
        return false;
    }

    m_codecContext = codecCtx;
    m_hwDeviceContext = deviceCtx;
    m_hwPixelFormat = hwPixFmt;
    m_activeTier = DecoderTier::GenericHardware;
    qCDebug(lcHwDec) << "Initialized Generic HW decoder tier:" << m_activeDeviceName;
    return true;
}

bool HwVideoDecoder::initSoftwareCpu()
{
    const AVStream *stream = m_formatContext->streams[m_videoStreamIndex];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        m_diagnostics << QStringLiteral("Software decoder not found for codec ID ") + QString::number(stream->codecpar->codec_id);
        return false;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(decoder);
    if (!codecCtx || avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) {
        if (codecCtx) avcodec_free_context(&codecCtx);
        return false;
    }

    const int threads = m_config.cpuThreads > 0 ? m_config.cpuThreads : std::max(2, QThread::idealThreadCount());
    codecCtx->thread_count = threads;
    codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    codecCtx->pkt_timebase = stream->time_base;

    if (avcodec_open2(codecCtx, decoder, nullptr) < 0) {
        m_diagnostics << QStringLiteral("Software avcodec_open2 failed");
        avcodec_free_context(&codecCtx);
        return false;
    }

    m_codecContext = codecCtx;
    m_activeTier = DecoderTier::SoftwareCpu;
    m_activeDeviceName = QStringLiteral("CPU Multi-threaded (%1 worker threads)").arg(threads);
    qCDebug(lcHwDec) << "Initialized Software CPU decoder tier:" << m_activeDeviceName;
    return true;
}

void HwVideoDecoder::fallbackToSoftware(const QString &reason)
{
    m_diagnostics << QStringLiteral("Falling back to CPU software decoding: ") + reason;
    qCWarning(lcHwDec) << "Hardware decoding fallback triggered:" << reason;
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_hwDeviceContext) {
        av_buffer_unref(&m_hwDeviceContext);
    }
    m_hwPixelFormat = -1;
    initSoftwareCpu();
}

void HwVideoDecoder::flush()
{
    if (m_codecContext) {
        avcodec_flush_buffers(m_codecContext);
    }
}

std::optional<engine::VideoFrame> HwVideoDecoder::decodeFrameAt(double timestampSeconds,
                                                               bool keyframeOnly,
                                                               const std::atomic<bool> *cancel)
{
    const int64_t frameIndex = static_cast<int64_t>(std::llround(std::max(0.0, timestampSeconds) * m_frameRate));
    return decodeFrame(frameIndex, keyframeOnly, cancel);
}

std::optional<engine::VideoFrame> HwVideoDecoder::decodeFrame(int64_t frameIndex,
                                                             bool keyframeOnly,
                                                             const std::atomic<bool> *cancel)
{
    if (!isOpen()) {
        return std::nullopt;
    }

    // 1. Check Bounded LRU Cache (FASE 2 UMA optimization)
    if (auto cached = m_cache.get(frameIndex)) {
        return cached;
    }

    const AVStream *stream = m_formatContext->streams[m_videoStreamIndex];
    const int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    const int64_t targetPts = start + av_rescale_q(frameIndex, AVRational{1, static_cast<int>(std::lround(m_frameRate))}, stream->time_base);

    // Seek to closest preceding keyframe
    if (av_seek_frame(m_formatContext, m_videoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD) >= 0) {
        flush();
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *swFrame = av_frame_alloc();

    const auto cleanup = [&] {
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&swFrame);
    };

    bool reachedTarget = false;

    while (!reachedTarget && (!cancel || !cancel->load())) {
        const int readRet = av_read_frame(m_formatContext, packet);
        if (readRet < 0) {
            avcodec_send_packet(m_codecContext, nullptr); // Flush decoder
        } else if (packet->stream_index == m_videoStreamIndex) {
            if (avcodec_send_packet(m_codecContext, packet) < 0) {
                av_packet_unref(packet);
                // Hardware decode error: transparent runtime fallback to CPU
                if (m_activeTier != DecoderTier::SoftwareCpu) {
                    fallbackToSoftware(QStringLiteral("Runtime avcodec_send_packet failure"));
                    cleanup();
                    return decodeFrame(frameIndex, keyframeOnly, cancel);
                }
                break;
            }
        }
        av_packet_unref(packet);

        while (avcodec_receive_frame(m_codecContext, frame) == 0) {
            AVFrame *effectiveFrame = frame;
            // If frame is in hardware format (e.g. VAAPI surface), transfer directly into UMA memory
            if (frame->format == m_hwPixelFormat) {
                if (av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
                    av_frame_unref(frame);
                    if (m_activeTier != DecoderTier::SoftwareCpu) {
                        fallbackToSoftware(QStringLiteral("av_hwframe_transfer_data failure"));
                        cleanup();
                        return decodeFrame(frameIndex, keyframeOnly, cancel);
                    }
                    continue;
                }
                effectiveFrame = swFrame;
            }

            const int64_t currentPts = effectiveFrame->pts != AV_NOPTS_VALUE ? effectiveFrame->pts : targetPts;
            if (keyframeOnly || currentPts >= targetPts || readRet < 0) {
                // Extract into VideoFrame (NV12 or P010 semi-planar directly without CPU RGB conversion!)
                engine::VideoPixelFormat outFormat = engine::VideoPixelFormat::Rgba8888;
                engine::GpuFramePlane planeY;
                engine::GpuFramePlane planeUV;

                const int w = effectiveFrame->width;
                const int h = effectiveFrame->height;

                if (effectiveFrame->format == AV_PIX_FMT_NV12) {
                    outFormat = engine::VideoPixelFormat::Nv12;
                    planeY.width = w;
                    planeY.height = h;
                    planeY.stride = effectiveFrame->linesize[0];
                    planeY.data = QByteArray(reinterpret_cast<const char *>(effectiveFrame->data[0]), planeY.stride * h);

                    planeUV.width = w / 2;
                    planeUV.height = h / 2;
                    planeUV.stride = effectiveFrame->linesize[1];
                    planeUV.data = QByteArray(reinterpret_cast<const char *>(effectiveFrame->data[1]), planeUV.stride * (h / 2));
                } else if (effectiveFrame->format == AV_PIX_FMT_P010LE || effectiveFrame->format == AV_PIX_FMT_P010BE) {
                    outFormat = engine::VideoPixelFormat::P010;
                    planeY.width = w;
                    planeY.height = h;
                    planeY.stride = effectiveFrame->linesize[0];
                    planeY.data = QByteArray(reinterpret_cast<const char *>(effectiveFrame->data[0]), planeY.stride * h);

                    planeUV.width = w / 2;
                    planeUV.height = h / 2;
                    planeUV.stride = effectiveFrame->linesize[1];
                    planeUV.data = QByteArray(reinterpret_cast<const char *>(effectiveFrame->data[1]), planeUV.stride * (h / 2));
                } else if (effectiveFrame->format == AV_PIX_FMT_YUV420P) {
                    // Convert planar YUV420P to semi-planar NV12 for unified zero-copy GPU shader presentation
                    outFormat = engine::VideoPixelFormat::Nv12;
                    planeY.width = w;
                    planeY.height = h;
                    planeY.stride = w;
                    planeY.data.resize(w * h);
                    for (int y = 0; y < h; ++y) {
                        std::copy_n(effectiveFrame->data[0] + y * effectiveFrame->linesize[0], w,
                                    reinterpret_cast<uint8_t *>(planeY.data.data()) + y * w);
                    }

                    planeUV.width = w / 2;
                    planeUV.height = h / 2;
                    planeUV.stride = w;
                    planeUV.data.resize(w * (h / 2));
                    auto *uvDst = reinterpret_cast<uint8_t *>(planeUV.data.data());
                    for (int y = 0; y < h / 2; ++y) {
                        const uint8_t *uSrc = effectiveFrame->data[1] + y * effectiveFrame->linesize[1];
                        const uint8_t *vSrc = effectiveFrame->data[2] + y * effectiveFrame->linesize[2];
                        for (int x = 0; x < w / 2; ++x) {
                            uvDst[y * w + x * 2] = uSrc[x];
                            uvDst[y * w + x * 2 + 1] = vSrc[x];
                        }
                    }
                } else {
                    // Fallback to RGBA8888 QImage
                    QImage img(w, h, QImage::Format_RGBA8888);
                    av_image_copy_to_buffer(img.bits(), img.sizeInBytes(), effectiveFrame->data,
                                            effectiveFrame->linesize, static_cast<AVPixelFormat>(effectiveFrame->format),
                                            w, h, 1);
                    outFormat = engine::VideoPixelFormat::Rgba8888;
                    engine::VideoFrame videoFrame(img, static_cast<int>(frameIndex), currentPts);
                    m_cache.insert(frameIndex, videoFrame);
                    av_frame_unref(swFrame);
                    av_frame_unref(frame);
                    cleanup();
                    return videoFrame;
                }

                engine::VideoFrame videoFrame(outFormat, QSize(w, h), std::move(planeY), std::move(planeUV),
                                              static_cast<int>(frameIndex), currentPts);
                m_cache.insert(frameIndex, videoFrame);
                av_frame_unref(swFrame);
                av_frame_unref(frame);
                cleanup();
                return videoFrame;
            }
            av_frame_unref(swFrame);
            av_frame_unref(frame);
        }

        if (readRet < 0) {
            break;
        }
    }

    cleanup();
    return std::nullopt;
}

} // namespace vedit::gpu
