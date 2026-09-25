// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QString>

#include <memory>

namespace Mlt {
class Producer;
class Profile;
} // namespace Mlt

namespace vedit::engine {

// One MLT producer per media item, shared by every clip that uses it (clips are cuts of it).
// Producers are bound to a profile, so a cache belongs to one profile.
// Opening a file costs tens of milliseconds, so the UI uses request() (opened in a worker thread,
// ready() emitted when done) while export and tests use open() (synchronous).
class MediaProducerCache : public QObject
{
    Q_OBJECT

public:
    explicit MediaProducerCache(Mlt::Profile &profile, QObject *parent = nullptr);
    ~MediaProducerCache() override;

    // Opens now (any thread). Returns null if the file cannot be opened (see error()).
    std::shared_ptr<Mlt::Producer> open(const Media &media);
    // The producer if already open; otherwise starts opening it in background and returns null.
    std::shared_ptr<Mlt::Producer> producerOrRequest(const Media &media);
    std::shared_ptr<Mlt::Producer> cached(const MediaId &mediaId) const;
    QString error(const MediaId &mediaId) const;
    // Releases every producer (call before destroying the profile or shutting MLT down).
    void clear();

signals:
    // Emitted in the owner's thread when a requested producer is ready (or failed: error() is set).
    void ready(const vedit::MediaId &mediaId);

private:
    Mlt::Profile &m_profile;
    // Own pool: the destructor waits for pending opens, which use the profile.
    QThreadPool m_pool;
    mutable QMutex m_mutex;
    QHash<MediaId, std::shared_ptr<Mlt::Producer>> m_producers;
    QHash<MediaId, QString> m_errors;
    QSet<MediaId> m_pending;
};

} // namespace vedit::engine
