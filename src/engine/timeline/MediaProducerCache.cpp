// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaProducerCache.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include "engine/analysis/ReverseProxy.h"

#include <mlt++/Mlt.h>

Q_LOGGING_CATEGORY(lcMedia, "vedit.engine.media")

using namespace Qt::StringLiterals;

namespace vedit::engine {

// MLT's loader and timewarp producers are not safe to construct concurrently: a test run under load once left a
// worker spinning forever inside producer_timewarp_init (mlt_properties_get) while another producer was being built,
// and the editor hung when it closed (waiting for that worker). The workers of every cache build their producers
// one at a time; the interface thread never takes this lock (it would wait for a slow file).
QMutex &MediaProducerCache::constructionMutex()
{
    static QMutex mutex;
    return mutex;
}

MediaProducerCache::MediaProducerCache(Mlt::Profile &profile, QObject *parent)
    : QObject(parent)
    , m_profile(profile)
{
    m_pool.setMaxThreadCount(2);
}

MediaProducerCache::~MediaProducerCache()
{
    m_pool.waitForDone();
    clear();
}

QString MediaProducerCache::keyOf(const MediaId &mediaId, double speed, bool preservePitch)
{
    if (speed == 1.0) {
        return mediaId.toString();
    }
    return mediaId.toString() + u'@' + QString::number(speed, 'g', 12) + (preservePitch ? u"p"_s : u""_s);
}

std::shared_ptr<Mlt::Producer> MediaProducerCache::open(const Media &media, double speed, bool preservePitch)
{
    if (media.kind == MediaKind::Image) {
        speed = 1.0; // a still has no speed
    }
    const QString key = keyOf(media.id, speed, preservePitch);
    if (auto existing = cached(media.id, speed, preservePitch)) {
        return existing;
    }
    std::shared_ptr<Mlt::Producer> producer;
    QString error;
    if (!QFileInfo::exists(media.path) && media.kind != MediaKind::ImageSequence) {
        error = QCoreApplication::translate("vedit::engine::MediaProducerCache", "The file is missing: %1").arg(media.path);
    } else {
        // The "loader" producer adds MLT's normalizers: the image keeps its aspect ratio inside the profile with
        // transparent borders, and the rotation stored in the file is applied (verified by projection_probe).
        QByteArray resource = QFile::encodeName(media.path);
        double warp = speed;
        if (speed < 0 && m_useReverseProxies && reverseProxyReady(media)) {
            // Already backwards, every frame a keyframe: read forwards (same frame numbering as timewarp:-1).
            resource = QFile::encodeName(reverseProxyPath(media));
            warp = -speed;
        }
        if (warp != 1.0) {
            resource = "timewarp:" + QByteArray::number(warp, 'g', 12) + ':' + resource;
        }
        {
            QMutexLocker construction(&constructionMutex());
            producer = std::make_shared<Mlt::Producer>(m_profile, resource.constData());
        }
        if (!producer->is_valid()) {
            producer.reset();
            error = QCoreApplication::translate("vedit::engine::MediaProducerCache",
                                                "This file cannot be opened: the format is not supported or the file is damaged.");
        } else if (media.kind == MediaKind::Image) {
            // Stills: any duration (the default length of MLT's image producer is only 10 minutes at 25 fps).
            producer->set("length", 0x7fffffff);
            producer->set("out", 0x7ffffffe);
        } else if (speed != 1.0) {
            producer->set("warp_pitch", preservePitch ? 1 : 0);
        }
    }
    QMutexLocker lock(&m_mutex);
    if (producer) {
        m_producers.insert(key, producer);
        m_errors.remove(media.id);
    } else {
        qCWarning(lcMedia) << "cannot open" << media.path << error;
        m_errors.insert(media.id, error);
    }
    return producer;
}

std::shared_ptr<Mlt::Producer> MediaProducerCache::producerOrRequest(const Media &media, double speed, bool preservePitch)
{
    const QString key = keyOf(media.id, media.kind == MediaKind::Image ? 1.0 : speed, preservePitch);
    {
        QMutexLocker lock(&m_mutex);
        if (auto it = m_producers.constFind(key); it != m_producers.constEnd()) {
            return *it;
        }
        if (m_pending.contains(key) || m_errors.contains(media.id)) {
            return nullptr;
        }
        m_pending.insert(key);
    }
    m_pool.start([this, media, speed, preservePitch, key] {
        open(media, speed, preservePitch);
        // Delivered in the owner's thread; dropped by Qt if the cache is destroyed first.
        QMetaObject::invokeMethod(this, [this, key, id = media.id] {
            {
                QMutexLocker lock(&m_mutex);
                m_pending.remove(key);
            }
            emit ready(id);
        });
    });
    return nullptr;
}

std::shared_ptr<Mlt::Producer> MediaProducerCache::cached(const MediaId &mediaId, double speed, bool preservePitch) const
{
    QMutexLocker lock(&m_mutex);
    return m_producers.value(keyOf(mediaId, speed, preservePitch));
}

QString MediaProducerCache::error(const MediaId &mediaId) const
{
    QMutexLocker lock(&m_mutex);
    return m_errors.value(mediaId);
}

void MediaProducerCache::forget(const MediaId &mediaId)
{
    QMutexLocker lock(&m_mutex);
    const QString prefix = mediaId.toString();
    for (auto it = m_producers.begin(); it != m_producers.end();) {
        it = it.key().startsWith(prefix) ? m_producers.erase(it) : std::next(it);
    }
}

void MediaProducerCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_producers.clear();
    m_errors.clear();
}

std::shared_ptr<const Spectrum> MediaProducerCache::spectrum(const Media &media)
{
    if (!media.info.audio || !media.fingerprint.isValid()) {
        return nullptr;
    }
    const QString key = media.fingerprint.value;
    {
        QMutexLocker lock(&m_mutex);
        if (auto cached = m_spectra.value(key)) {
            return cached;
        }
        if (m_noSpectrum.contains(key)) {
            return nullptr;
        }
    }
    std::shared_ptr<const Spectrum> result;
    if (std::optional<Spectrum> loaded = cachedSpectrum(media)) {
        result = std::make_shared<const Spectrum>(std::move(*loaded));
    }
    QMutexLocker lock(&m_mutex);
    if (result) {
        m_spectra.insert(key, result);
    } else {
        m_noSpectrum.insert(key);
    }
    return result;
}

std::shared_ptr<const Spectrum> MediaProducerCache::spectrumOrRequest(const Media &media)
{
    if (!media.info.audio || !media.fingerprint.isValid()) {
        return nullptr;
    }
    const QString key = media.fingerprint.value;
    const QString pending = QStringLiteral("spectrum:") + key;
    {
        QMutexLocker lock(&m_mutex);
        if (auto cached = m_spectra.value(key)) {
            return cached;
        }
        if (m_noSpectrum.contains(key) || m_pending.contains(pending)) {
            return nullptr;
        }
        m_pending.insert(pending);
    }
    m_pool.start([this, media, pending] {
        spectrum(media);
        QMetaObject::invokeMethod(this, [this, pending, id = media.id] {
            {
                QMutexLocker lock(&m_mutex);
                m_pending.remove(pending);
            }
            emit ready(id);
        });
    });
    return nullptr;
}

} // namespace vedit::engine
