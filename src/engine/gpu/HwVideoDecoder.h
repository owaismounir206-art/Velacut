// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/gpu/GpuCapabilities.h"
#include "engine/playback/LruFrameCache.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

extern "C" {
struct AVBufferRef;
struct AVCodec;
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
}

namespace vedit::gpu {

enum class TargetGpuArchitecture {
    Generic,
    AmdRdna3_Phoenix2,    // AMD Radeon 740M (VCN 4.x, RDNA3)
    IntelXe2_LunarLake,   // Intel Arc 130V / 140V (Xe2 Battlemage / Lunar Lake)
};

enum class DecoderTier {
    DedicatedHardware, // AMF (Windows AMD) / QSV / oneVPL (Intel)
    GenericHardware,   // VA-API (radeonsi / iHD on Linux), D3D11VA (Windows), Vulkan Video
    SoftwareCpu        // Multithreaded AVCodec CPU worker pool
};

QString decoderTierName(DecoderTier tier);
QString targetArchitectureName(TargetGpuArchitecture arch);

struct DecoderConfig {
    bool enableHardware = true;
    size_t lruCacheCapacity = 24; // Bounded between 15 and 30 frames (UMA memory safety)
    int cpuThreads = 0;           // 0 = automatic (hardware concurrency)
    std::optional<TargetGpuArchitecture> forceArchitecture;
};

// Low-level hardware-accelerated video decoder pipeline with transparent cascading fallback
// and bounded LRU frame cache for zero-copy UMA scrubbing (FASE 2).
class HwVideoDecoder {
public:
    explicit HwVideoDecoder(DecoderConfig config = {});
    ~HwVideoDecoder();

    // Opens a video file and initializes the appropriate hardware/software decoding tier.
    bool open(const QString &filePath);
    void close();

    bool isOpen() const;
    QString filePath() const { return m_filePath; }
    DecoderTier activeTier() const { return m_activeTier; }
    TargetGpuArchitecture detectedArchitecture() const { return m_architecture; }
    QString activeDeviceName() const { return m_activeDeviceName; }
    QSize videoSize() const { return m_videoSize; }
    double frameRate() const { return m_frameRate; }
    int64_t durationFrames() const { return m_durationFrames; }
    int64_t durationUs() const { return m_durationUs; }

    // Seeks and decodes the frame at `timestampSeconds` (or closest preceding keyframe + decode forward).
    // Uses bounded LRU frame cache for immediate zero-latency hits during scrubbing.
    std::optional<engine::VideoFrame> decodeFrameAt(double timestampSeconds,
                                                    bool keyframeOnly = false,
                                                    const std::atomic<bool> *cancel = nullptr);

    // Decodes the frame at `frameIndex`.
    std::optional<engine::VideoFrame> decodeFrame(int64_t frameIndex,
                                                  bool keyframeOnly = false,
                                                  const std::atomic<bool> *cancel = nullptr);

    // Flushes codec and stream buffers.
    void flush();

    // Cache metrics
    size_t cacheSize() const { return m_cache.size(); }
    uint64_t cacheHits() const { return m_cache.hits(); }
    uint64_t cacheMisses() const { return m_cache.misses(); }
    uint64_t cacheEvictions() const { return m_cache.evictions(); }
    void clearCache() { m_cache.clear(); }

    QStringList diagnostics() const { return m_diagnostics; }

private:
    bool initStream();
    bool tryInitTier(DecoderTier tier);
    bool initDedicatedHw();
    bool initGenericHw();
    bool initSoftwareCpu();
    bool isColorFormatAccelerated(int pixelFormat, int profile) const;
    void fallbackToSoftware(const QString &reason);
    TargetGpuArchitecture detectTargetArchitecture();

    DecoderConfig m_config;
    QString m_filePath;
    DecoderTier m_activeTier = DecoderTier::SoftwareCpu;
    TargetGpuArchitecture m_architecture = TargetGpuArchitecture::Generic;
    QString m_activeDeviceName;
    QSize m_videoSize;
    double m_frameRate = 30.0;
    int64_t m_durationFrames = 0;
    int64_t m_durationUs = 0;
    int m_videoStreamIndex = -1;

    AVFormatContext *m_formatContext = nullptr;
    AVCodecContext *m_codecContext = nullptr;
    AVBufferRef *m_hwDeviceContext = nullptr;
    int m_hwPixelFormat = -1; // e.g. AV_PIX_FMT_VAAPI

    engine::BoundedLruCache<int64_t, engine::VideoFrame> m_cache;
    QStringList m_diagnostics;
};

} // namespace vedit::gpu
