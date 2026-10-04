// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/gpu/HwVideoDecoder.h"
#include "engine/playback/LruFrameCache.h"

#include <QDir>
#include <QFileInfo>
#include <QTest>

using namespace vedit::gpu;
using namespace vedit::engine;

class TestHwDecoder : public QObject
{
    Q_OBJECT

private slots:
    void lruCacheBoundsAndEviction()
    {
        // 1. Capacity clamped between 15 and 30 (FASE 2 specification)
        BoundedLruCache<int64_t, VideoFrame> cacheSmall(5);
        QCOMPARE(cacheSmall.capacity(), 15UL);

        BoundedLruCache<int64_t, VideoFrame> cacheLarge(50);
        QCOMPARE(cacheLarge.capacity(), 30UL);

        BoundedLruCache<int64_t, VideoFrame> cache(20);
        QCOMPARE(cache.capacity(), 20UL);

        // 2. Insert 25 frames
        for (int i = 0; i < 25; ++i) {
            QImage img(64, 64, QImage::Format_RGBA8888);
            img.fill(Qt::blue);
            cache.insert(i, VideoFrame(img, i, i * 1000));
        }

        // 3. Cache must not exceed capacity (20)
        QCOMPARE(cache.size(), 20UL);
        QCOMPARE(cache.evictions(), 5ULL);

        // 4. Oldest frames (0..4) should have been evicted
        for (int i = 0; i < 5; ++i) {
            QVERIFY(!cache.get(i).has_value());
        }

        // 5. Recent frames (5..24) must still be present
        for (int i = 5; i < 25; ++i) {
            auto frame = cache.get(i);
            QVERIFY(frame.has_value());
            QCOMPARE(frame->position(), i);
        }

        // 6. Accessing frame 5 makes it most recently used; inserting a new frame should evict frame 6 instead
        QVERIFY(cache.get(5).has_value()); // now MRU
        QImage extraImg(64, 64, QImage::Format_RGBA8888);
        cache.insert(25, VideoFrame(extraImg, 25, 25000));

        QCOMPARE(cache.size(), 20UL);
        QVERIFY(cache.get(5).has_value()); // still present!
        QVERIFY(!cache.get(6).has_value()); // evicted!
    }

    void videoFrameNv12Conversion()
    {
        const int w = 64;
        const int h = 64;

        GpuFramePlane yPlane;
        yPlane.width = w;
        yPlane.height = h;
        yPlane.stride = w;
        yPlane.data.resize(w * h);
        yPlane.data.fill(static_cast<char>(128));

        GpuFramePlane uvPlane;
        uvPlane.width = w / 2;
        uvPlane.height = h / 2;
        uvPlane.stride = w;
        uvPlane.data.resize(w * (h / 2));
        uvPlane.data.fill(static_cast<char>(128));

        VideoFrame frame(VideoPixelFormat::Nv12, QSize(w, h), std::move(yPlane), std::move(uvPlane), 1, 33333);
        QCOMPARE(frame.format(), VideoPixelFormat::Nv12);
        QCOMPARE(frame.width(), w);
        QCOMPARE(frame.height(), h);

        QImage converted = frame.toImage();
        QCOMPARE(converted.size(), QSize(w, h));
        QVERIFY(!converted.isNull());
    }

    void videoFrameP010Conversion()
    {
        const int w = 64;
        const int h = 64;

        GpuFramePlane yPlane;
        yPlane.width = w;
        yPlane.height = h;
        yPlane.stride = w * 2;
        yPlane.data.resize(w * h * 2);
        yPlane.data.fill(0);

        GpuFramePlane uvPlane;
        uvPlane.width = w / 2;
        uvPlane.height = h / 2;
        uvPlane.stride = w * 2;
        uvPlane.data.resize(w * (h / 2) * 2);
        uvPlane.data.fill(0);

        VideoFrame frame(VideoPixelFormat::P010, QSize(w, h), std::move(yPlane), std::move(uvPlane), 2, 66666);
        QCOMPARE(frame.format(), VideoPixelFormat::P010);
        QCOMPARE(frame.width(), w);
        QCOMPARE(frame.height(), h);

        QImage converted = frame.toImage();
        QCOMPARE(converted.size(), QSize(w, h));
        QVERIFY(!converted.isNull());
    }

    void architectureTargetingAndNames()
    {
        QCOMPARE(targetArchitectureName(TargetGpuArchitecture::AmdRdna3_Phoenix2),
                 QStringLiteral("AMD Radeon 740M (RDNA3 / VCN 4.x / Phoenix2)"));
        QCOMPARE(targetArchitectureName(TargetGpuArchitecture::IntelXe2_LunarLake),
                 QStringLiteral("Intel Arc 130V (Xe2 Battlemage / Lunar Lake)"));

        QCOMPARE(decoderTierName(DecoderTier::DedicatedHardware),
                 QStringLiteral("Dedicated Hardware (AMF/QSV/oneVPL)"));
        QCOMPARE(decoderTierName(DecoderTier::GenericHardware),
                 QStringLiteral("Generic Hardware (VA-API/D3D11VA/Vulkan)"));
        QCOMPARE(decoderTierName(DecoderTier::SoftwareCpu),
                 QStringLiteral("Software CPU Multithreaded"));
    }

    void cascadingFallbackToCpu()
    {
        DecoderConfig config;
        config.enableHardware = false; // Force CPU fallback tier
        config.cpuThreads = 4;
        config.lruCacheCapacity = 20;

        HwVideoDecoder decoder(config);
        QCOMPARE(decoder.activeTier(), DecoderTier::SoftwareCpu);

        // Find existing test video in project build directory
        QString testVideo;
        for (const QString &candidate : {QStringLiteral("/home/owais/Velacut/build/testmedia/smoke_720p30.mp4"),
                                         QStringLiteral("testmedia/smoke_720p30.mp4")}) {
            if (QFileInfo::exists(candidate)) {
                testVideo = candidate;
                break;
            }
        }

        if (testVideo.isEmpty()) {
            QSKIP("No test MP4 found in dev-home/Videos");
        }

        QVERIFY(decoder.open(testVideo));
        QVERIFY(decoder.isOpen());
        QCOMPARE(decoder.activeTier(), DecoderTier::SoftwareCpu);
        QVERIFY(decoder.videoSize().width() > 0);
        QVERIFY(decoder.videoSize().height() > 0);

        // Decode first frame
        auto frame1 = decoder.decodeFrame(0);
        QVERIFY(frame1.has_value());
        QVERIFY(!frame1->isNull());
        QCOMPARE(decoder.cacheSize(), 1UL);

        // Second decode of frame 0 should hit LRU cache
        const uint64_t initialHits = decoder.cacheHits();
        auto frame2 = decoder.decodeFrame(0);
        QVERIFY(frame2.has_value());
        QCOMPARE(decoder.cacheHits(), initialHits + 1);

        decoder.close();
        QVERIFY(!decoder.isOpen());
    }

    void hardwareDecodingWithTargetGpu()
    {
        DecoderConfig config;
        config.enableHardware = true;
        config.forceArchitecture = TargetGpuArchitecture::AmdRdna3_Phoenix2;
        config.lruCacheCapacity = 24;

        HwVideoDecoder decoder(config);
        QCOMPARE(decoder.detectedArchitecture(), TargetGpuArchitecture::AmdRdna3_Phoenix2);

        QString testVideo;
        for (const QString &candidate : {QStringLiteral("/home/owais/Velacut/build/testmedia/smoke_720p30.mp4"),
                                         QStringLiteral("testmedia/smoke_720p30.mp4")}) {
            if (QFileInfo::exists(candidate)) {
                testVideo = candidate;
                break;
            }
        }

        if (testVideo.isEmpty()) {
            QSKIP("No test MP4 found in dev-home/Videos");
        }

        QVERIFY(decoder.open(testVideo));
        QVERIFY(decoder.isOpen());

        // Decode frame
        auto frame = decoder.decodeFrame(0);
        QVERIFY(frame.has_value());
        QVERIFY(!frame->isNull());

        // Scrub forward 5 frames
        for (int i = 1; i <= 5; ++i) {
            auto f = decoder.decodeFrame(i);
            QVERIFY(f.has_value());
        }
        QVERIFY(decoder.cacheSize() >= 5);

        decoder.close();
    }
};

QTEST_MAIN(TestHwDecoder)
#include "tst_hwdecoder.moc"
