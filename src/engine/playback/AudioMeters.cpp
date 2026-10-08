// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioMeters.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>

#include <cmath>

namespace velacut::engine {

namespace {

struct Meter
{
    float peak = 0.0f;
    qint64 at = 0; // ms
};

QMutex s_mutex;
QHash<QByteArray, Meter> s_meters;

qint64 now()
{
    static QElapsedTimer clock = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return clock.elapsed();
}

} // namespace

void AudioMeters::report(const QByteArray &key, float peak)
{
    QMutexLocker lock(&s_mutex);
    Meter &meter = s_meters[key];
    const qint64 t = now();
    const float decayed = meter.peak * std::exp(-(t - meter.at) / 400.0f);
    meter.peak = std::max(peak, decayed);
    meter.at = t;
}

float AudioMeters::level(const QByteArray &key)
{
    QMutexLocker lock(&s_mutex);
    const auto it = s_meters.constFind(key);
    if (it == s_meters.constEnd()) {
        return 0.0f;
    }
    return it->peak * std::exp(-(now() - it->at) / 400.0f);
}

void AudioMeters::clear()
{
    QMutexLocker lock(&s_mutex);
    s_meters.clear();
}

} // namespace velacut::engine
