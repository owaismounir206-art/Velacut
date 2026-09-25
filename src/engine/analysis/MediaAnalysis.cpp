// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaAnalysis.h"

#include "common/Paths.h"
#include "core/serialization/ProjectFile.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QPointer>

#include <cmath>

Q_LOGGING_CATEGORY(lcAnalysis, "vedit.engine.analysis")

using namespace Qt::StringLiterals;

namespace vedit::engine {

namespace {

constexpr quint32 kWaveformMagic = 0x56574631; // "VWF1"

QByteArray encodeWaveform(const Waveform &waveform)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << kWaveformMagic << qint32(waveform.bucketsPerSecond) << waveform.peaks;
    return bytes;
}

std::shared_ptr<const Waveform> decodeWaveform(const QByteArray &bytes)
{
    QDataStream stream(bytes);
    quint32 magic = 0;
    qint32 perSecond = 0;
    auto waveform = std::make_shared<Waveform>();
    stream >> magic >> perSecond >> waveform->peaks;
    if (stream.status() != QDataStream::Ok || magic != kWaveformMagic || perSecond <= 0) {
        return nullptr;
    }
    waveform->bucketsPerSecond = perSecond;
    return waveform;
}

} // namespace

MediaAnalysis::MediaAnalysis(QString cacheRoot, QObject *parent)
    : QObject(parent)
    , m_root(std::move(cacheRoot))
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
    , m_thumbnails(96 * 1024) // 96 MiB of decoded strips; the rest is reloaded from disk when needed
{
    m_pool.setMaxThreadCount(2);
}

MediaAnalysis::~MediaAnalysis()
{
    *m_cancel = true;
    m_pool.clear();
    m_pool.waitForDone();
}

QString MediaAnalysis::defaultCacheRoot()
{
    return paths::cacheDir() + u"/media"_s;
}

QString MediaAnalysis::directoryOf(const Media &media) const
{
    return m_root + u"/"_s + media.fingerprint.value;
}

QString MediaAnalysis::thumbnailFile(const Media &media) const
{
    return directoryOf(media) + u"/thumbnails-h%1-n%2.jpg"_s.arg(kThumbnailHeight).arg(thumbnailCount(media));
}

QString MediaAnalysis::waveformFile(const Media &media) const
{
    return directoryOf(media) + u"/waveform-100.bin"_s;
}

int MediaAnalysis::thumbnailCount(const Media &media)
{
    if (media.kind == MediaKind::Image || !media.info.duration) {
        return 1;
    }
    return std::clamp(static_cast<int>(std::ceil(media.info.duration->toSecondsDouble())), 1, kMaxThumbnails);
}

QImage MediaAnalysis::thumbnails(const Media &media)
{
    if (media.kind == MediaKind::Audio || !media.fingerprint.isValid()) {
        return {};
    }
    const QString key = media.fingerprint.value;
    if (const QImage *cached = m_thumbnails.object(key)) {
        return *cached;
    }
    const QString file = thumbnailFile(media);
    if (QFile::exists(file)) {
        QImage strip(file); // a few ms: JPEG decode of a small strip
        if (!strip.isNull()) {
            m_thumbnails.insert(key, new QImage(strip), static_cast<int>(strip.sizeInBytes() / 1024));
            return strip;
        }
    }
    if (m_pending.contains(u"t:"_s + key)) {
        return {};
    }
    m_pending.insert(u"t:"_s + key);
    QPointer<MediaAnalysis> self(this);
    const int count = thumbnailCount(media);
    m_pool.start([self, media, count, file, cancel = m_cancel] {
        const QImage strip = extractThumbnailStrip(media.path, count, kThumbnailHeight, cancel.get());
        if (!strip.isNull()) {
            QByteArray jpeg;
            QBuffer buffer(&jpeg);
            buffer.open(QIODevice::WriteOnly);
            strip.save(&buffer, "JPG", 85);
            projectfile::writeAtomically(file, jpeg);
        }
        QMetaObject::invokeMethod(self.get(), [self, media, strip] {
            if (self) {
                self->finishThumbnails(media, strip);
            }
        });
    });
    return {};
}

void MediaAnalysis::finishThumbnails(const Media &media, const QImage &strip)
{
    m_pending.remove(u"t:"_s + media.fingerprint.value);
    if (strip.isNull()) {
        qCInfo(lcAnalysis) << "no thumbnails for" << media.path;
        return;
    }
    m_thumbnails.insert(media.fingerprint.value, new QImage(strip), static_cast<int>(strip.sizeInBytes() / 1024));
    emit thumbnailsReady(media.id);
}

std::shared_ptr<const Waveform> MediaAnalysis::waveform(const Media &media)
{
    if (!media.info.audio || !media.fingerprint.isValid()) {
        return nullptr;
    }
    const QString key = media.fingerprint.value;
    if (auto cached = m_waveforms.value(key)) {
        return cached;
    }
    const QString file = waveformFile(media);
    QFile stored(file);
    if (stored.open(QIODevice::ReadOnly)) {
        if (auto loaded = decodeWaveform(stored.readAll())) {
            m_waveforms.insert(key, loaded);
            return loaded;
        }
    }
    if (m_pending.contains(u"w:"_s + key)) {
        return nullptr;
    }
    m_pending.insert(u"w:"_s + key);
    QPointer<MediaAnalysis> self(this);
    m_pool.start([self, media, file, cancel = m_cancel] {
        std::shared_ptr<const Waveform> result;
        if (std::optional<Waveform> waveform = extractWaveform(media.path, 100, cancel.get())) {
            projectfile::writeAtomically(file, encodeWaveform(*waveform));
            result = std::make_shared<const Waveform>(std::move(*waveform));
        }
        QMetaObject::invokeMethod(self.get(), [self, media, result] {
            if (self) {
                self->finishWaveform(media, result);
            }
        });
    });
    return nullptr;
}

void MediaAnalysis::finishWaveform(const Media &media, std::shared_ptr<const Waveform> waveform)
{
    m_pending.remove(u"w:"_s + media.fingerprint.value);
    if (!waveform) {
        qCInfo(lcAnalysis) << "no waveform for" << media.path;
        return;
    }
    // Waveforms are small (2 bytes per 1/100 s): kept for the session.
    m_waveforms.insert(media.fingerprint.value, waveform);
    emit waveformReady(media.id);
}

} // namespace vedit::engine
