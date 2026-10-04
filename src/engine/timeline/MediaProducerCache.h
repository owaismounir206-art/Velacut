// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"
#include "engine/analysis/Spectrum.h"

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
    // Held while a worker builds a producer through MLT's loader (see the .cpp). Never take it on the interface
    // thread.
    static QMutex &constructionMutex();

    explicit MediaProducerCache(Mlt::Profile &profile, QObject *parent = nullptr);
    ~MediaProducerCache() override;

    // Opens now (any thread). Returns null if the file cannot be opened (see error()).
    // `speed` ≠ 1 (negative: backwards) gives a "timewarp" producer: frame n shows source frame n × speed, or the
    // source played backwards (verified by phase2_probe); `preservePitch` keeps the voice at its pitch.
    std::shared_ptr<Mlt::Producer> open(const Media &media, double speed = 1.0, bool preservePitch = true);
    // The producer if already open; otherwise starts opening it in background and returns null.
    std::shared_ptr<Mlt::Producer> producerOrRequest(const Media &media, double speed = 1.0, bool preservePitch = true);
    std::shared_ptr<Mlt::Producer> cached(const MediaId &mediaId, double speed = 1.0, bool preservePitch = true) const;
    QString error(const MediaId &mediaId) const;
    // Releases every producer (call before destroying the profile or shutting MLT down).
    void clear();
    // Preview: reversed clips read the backwards copy when it exists (ReverseProxyQueue); the export never does.
    void setUseReverseProxies(bool use) { m_useReverseProxies = use; }
    // Forgets the producers of a media item (e.g. its backwards copy became ready).
    void forget(const MediaId &mediaId);

    // Spectrum of a media item's audio (audio visualizers, beats), cached on disk by fingerprint next to the
    // waveform (<cache>/media/<fingerprint>/). spectrum() reads or computes it now (export, tests);
    // spectrumOrRequest() computes a missing one in background and emits ready() when done (preview).
    // Null for media without audio.
    std::shared_ptr<const Spectrum> spectrum(const Media &media);
    std::shared_ptr<const Spectrum> spectrumOrRequest(const Media &media);

signals:
    // Emitted in the owner's thread when a requested producer is ready (or failed: error() is set).
    void ready(const vedit::MediaId &mediaId);

private:
    Mlt::Profile &m_profile;
    // Own pool: the destructor waits for pending opens, which use the profile.
    QThreadPool m_pool;
    mutable QMutex m_mutex;
    static QString keyOf(const MediaId &mediaId, double speed, bool preservePitch);

    QHash<QString, std::shared_ptr<Mlt::Producer>> m_producers; // by keyOf()
    QHash<MediaId, QString> m_errors;
    QHash<QString, std::shared_ptr<const Spectrum>> m_spectra; // by fingerprint
    QSet<QString> m_noSpectrum;                                // fingerprints without audio (or unreadable)
    QSet<QString> m_pending;
    bool m_useReverseProxies = false;
};

} // namespace vedit::engine
