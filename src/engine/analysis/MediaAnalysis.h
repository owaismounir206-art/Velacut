// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"
#include "engine/analysis/Decoding.h"

#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace vedit::engine {

// Thumbnails and waveforms of media items, computed in background (never on the UI thread) and cached on disk by
// fingerprint, so they survive restarts and are shared by every project (docs/FILE_FORMAT.md §6.3).
class MediaAnalysis : public QObject
{
    Q_OBJECT

public:
    static constexpr int kThumbnailHeight = 90;
    static constexpr int kMaxThumbnails = 60;

    explicit MediaAnalysis(QString cacheRoot, QObject *parent = nullptr);
    ~MediaAnalysis() override;
    static QString defaultCacheRoot();

    // Frames evenly spaced over the media (one per second, at most kMaxThumbnails; one for images), each
    // thumbnailWidth(media) × kThumbnailHeight, side by side. Null until ready: then thumbnailsReady() is emitted.
    QImage thumbnails(const Media &media);
    static int thumbnailCount(const Media &media);
    // Min/max peaks per 1/100 s. Null until ready: then waveformReady() is emitted. Never ready without audio.
    std::shared_ptr<const Waveform> waveform(const Media &media);

signals:
    void thumbnailsReady(const vedit::MediaId &mediaId);
    void waveformReady(const vedit::MediaId &mediaId);

private:
    QString directoryOf(const Media &media) const;
    QString thumbnailFile(const Media &media) const;
    QString waveformFile(const Media &media) const;
    void finishThumbnails(const Media &media, const QImage &strip);
    void finishWaveform(const Media &media, std::shared_ptr<const Waveform> waveform);

    QString m_root;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    QCache<QString, QImage> m_thumbnails;      // by fingerprint, cost in KiB
    QHash<QString, std::shared_ptr<const Waveform>> m_waveforms;
    QSet<QString> m_pending;                   // "t:" / "w:" + fingerprint
};

} // namespace vedit::engine
