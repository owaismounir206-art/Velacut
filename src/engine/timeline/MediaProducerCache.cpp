// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaProducerCache.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <mlt++/Mlt.h>

Q_LOGGING_CATEGORY(lcMedia, "vedit.engine.media")

namespace vedit::engine {

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

std::shared_ptr<Mlt::Producer> MediaProducerCache::open(const Media &media)
{
    if (auto existing = cached(media.id)) {
        return existing;
    }
    std::shared_ptr<Mlt::Producer> producer;
    QString error;
    if (!QFileInfo::exists(media.path) && media.kind != MediaKind::ImageSequence) {
        error = QCoreApplication::translate("vedit::engine::MediaProducerCache", "The file is missing: %1").arg(media.path);
    } else {
        // The "loader" producer adds MLT's normalizers: the image keeps its aspect ratio inside the profile with
        // transparent borders, and the rotation stored in the file is applied (verified by projection_probe).
        const QByteArray resource = QFile::encodeName(media.path);
        producer = std::make_shared<Mlt::Producer>(m_profile, resource.constData());
        if (!producer->is_valid()) {
            producer.reset();
            error = QCoreApplication::translate("vedit::engine::MediaProducerCache",
                                                "This file cannot be opened: the format is not supported or the file is damaged.");
        } else if (media.kind == MediaKind::Image) {
            // Stills: any duration (the default length of MLT's image producer is only 10 minutes at 25 fps).
            producer->set("length", 0x7fffffff);
            producer->set("out", 0x7ffffffe);
        }
    }
    QMutexLocker lock(&m_mutex);
    if (producer) {
        m_producers.insert(media.id, producer);
        m_errors.remove(media.id);
    } else {
        qCWarning(lcMedia) << "cannot open" << media.path << error;
        m_errors.insert(media.id, error);
    }
    return producer;
}

std::shared_ptr<Mlt::Producer> MediaProducerCache::producerOrRequest(const Media &media)
{
    {
        QMutexLocker lock(&m_mutex);
        if (auto it = m_producers.constFind(media.id); it != m_producers.constEnd()) {
            return *it;
        }
        if (m_pending.contains(media.id) || m_errors.contains(media.id)) {
            return nullptr;
        }
        m_pending.insert(media.id);
    }
    m_pool.start([this, media] {
        open(media);
        // Delivered in the owner's thread; dropped by Qt if the cache is destroyed first.
        QMetaObject::invokeMethod(this, [this, id = media.id] {
            {
                QMutexLocker lock(&m_mutex);
                m_pending.remove(id);
            }
            emit ready(id);
        });
    });
    return nullptr;
}

std::shared_ptr<Mlt::Producer> MediaProducerCache::cached(const MediaId &mediaId) const
{
    QMutexLocker lock(&m_mutex);
    return m_producers.value(mediaId);
}

QString MediaProducerCache::error(const MediaId &mediaId) const
{
    QMutexLocker lock(&m_mutex);
    return m_errors.value(mediaId);
}

void MediaProducerCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_producers.clear();
    m_errors.clear();
}

} // namespace vedit::engine
