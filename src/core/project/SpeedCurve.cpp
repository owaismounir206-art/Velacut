// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpeedCurve.h"

#include <algorithm>
#include <cmath>

namespace velacut {

namespace {

struct SplineData {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> d; // derivatives at points
    std::vector<double> segIntegral; // integral of each segment
    std::vector<double> cumIntegral; // cumulative integral at start of each segment (and end at cumIntegral[M])
};

SplineData buildSpline(const SpeedCurve &curve)
{
    const auto pts = SpeedCurveUtil::normalizedPoints(curve);
    const int n = static_cast<int>(pts.size());
    SplineData s;
    s.x.resize(static_cast<size_t>(n));
    s.y.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        s.x[static_cast<size_t>(i)] = pts[static_cast<size_t>(i)].first;
        s.y[static_cast<size_t>(i)] = pts[static_cast<size_t>(i)].second;
    }

    const int m = n - 1;
    s.d.assign(static_cast<size_t>(n), 0.0);
    s.segIntegral.assign(static_cast<size_t>(m), 0.0);
    s.cumIntegral.assign(static_cast<size_t>(n), 0.0);

    if (m <= 0) {
        return s;
    }

    std::vector<double> h(static_cast<size_t>(m));
    std::vector<double> delta(static_cast<size_t>(m));
    for (int k = 0; k < m; ++k) {
        h[static_cast<size_t>(k)] = std::max(1e-6, s.x[static_cast<size_t>(k + 1)] - s.x[static_cast<size_t>(k)]);
        delta[static_cast<size_t>(k)] = (s.y[static_cast<size_t>(k + 1)] - s.y[static_cast<size_t>(k)]) / h[static_cast<size_t>(k)];
    }

    if (m == 1) {
        s.d[0] = delta[0];
        s.d[1] = delta[0];
    } else {
        // Internal tangents: standard PCHIP weighted harmonic mean
        for (int k = 1; k < n - 1; ++k) {
            const double d0 = delta[static_cast<size_t>(k - 1)];
            const double d1 = delta[static_cast<size_t>(k)];
            if (d0 * d1 <= 0.0) {
                s.d[static_cast<size_t>(k)] = 0.0;
            } else {
                const double h0 = h[static_cast<size_t>(k - 1)];
                const double h1 = h[static_cast<size_t>(k)];
                const double w1 = 2.0 * h1 + h0;
                const double w2 = h1 + 2.0 * h0;
                s.d[static_cast<size_t>(k)] = (w1 + w2) / (w1 / d0 + w2 / d1);
            }
        }
        // Endpoints: non-centered three-point formula clamped
        const auto endTangent = [](double h0, double h1, double d0, double d1) {
            double d = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
            if (d * d0 <= 0.0) {
                d = 0.0;
            } else if ((d0 * d1 <= 0.0) && (std::abs(d) > std::abs(3.0 * d0))) {
                d = 3.0 * d0;
            }
            return d;
        };
        s.d[0] = endTangent(h[0], h[1], delta[0], delta[1]);
        s.d[static_cast<size_t>(n - 1)] = endTangent(h[static_cast<size_t>(m - 1)], h[static_cast<size_t>(m - 2)],
                                                     delta[static_cast<size_t>(m - 1)], delta[static_cast<size_t>(m - 2)]);
    }

    // Fritsch-Carlson limiter to guarantee strict monotonicity
    for (int k = 0; k < m; ++k) {
        const double dSec = delta[static_cast<size_t>(k)];
        if (std::abs(dSec) < 1e-9) {
            s.d[static_cast<size_t>(k)] = 0.0;
            s.d[static_cast<size_t>(k + 1)] = 0.0;
        } else {
            const double alpha = s.d[static_cast<size_t>(k)] / dSec;
            const double beta = s.d[static_cast<size_t>(k + 1)] / dSec;
            const double sumSq = alpha * alpha + beta * beta;
            if (sumSq > 9.0) {
                const double tau = 3.0 / std::sqrt(sumSq);
                s.d[static_cast<size_t>(k)] = tau * alpha * dSec;
                s.d[static_cast<size_t>(k + 1)] = tau * beta * dSec;
            }
        }
    }

    // Compute segment and cumulative integrals
    double running = 0.0;
    s.cumIntegral[0] = 0.0;
    for (int k = 0; k < m; ++k) {
        const double hk = h[static_cast<size_t>(k)];
        const double yk = s.y[static_cast<size_t>(k)];
        const double yk1 = s.y[static_cast<size_t>(k + 1)];
        const double dk = s.d[static_cast<size_t>(k)];
        const double dk1 = s.d[static_cast<size_t>(k + 1)];
        const double seg = hk * (0.5 * (yk + yk1) + (hk / 12.0) * (dk - dk1));
        s.segIntegral[static_cast<size_t>(k)] = std::max(1e-6, seg);
        running += s.segIntegral[static_cast<size_t>(k)];
        s.cumIntegral[static_cast<size_t>(k + 1)] = running;
    }

    return s;
}

double evalSegmentSpeed(const SplineData &s, int k, double t)
{
    const double hk = s.x[static_cast<size_t>(k + 1)] - s.x[static_cast<size_t>(k)];
    const double yk = s.y[static_cast<size_t>(k)];
    const double yk1 = s.y[static_cast<size_t>(k + 1)];
    const double dk = s.d[static_cast<size_t>(k)];
    const double dk1 = s.d[static_cast<size_t>(k + 1)];

    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h0 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h1 = -2.0 * t3 + 3.0 * t2;
    const double h2 = t3 - 2.0 * t2 + t;
    const double h3 = t3 - t2;

    const double v = yk * h0 + yk1 * h1 + hk * dk * h2 + hk * dk1 * h3;
    return std::clamp(v, 0.05, 100.0);
}

double evalSegmentIntegral(const SplineData &s, int k, double t)
{
    const double hk = s.x[static_cast<size_t>(k + 1)] - s.x[static_cast<size_t>(k)];
    const double yk = s.y[static_cast<size_t>(k)];
    const double yk1 = s.y[static_cast<size_t>(k + 1)];
    const double dk = s.d[static_cast<size_t>(k)];
    const double dk1 = s.d[static_cast<size_t>(k + 1)];

    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;

    const double s0 = 0.5 * t4 - t3 + t;
    const double s1 = -0.5 * t4 + t3;
    const double s2 = 0.25 * t4 - (2.0 / 3.0) * t3 + 0.5 * t2;
    const double s3 = 0.25 * t4 - (1.0 / 3.0) * t3;

    return hk * (yk * s0 + yk1 * s1 + hk * dk * s2 + hk * dk1 * s3);
}

} // namespace

SpeedCurve SpeedCurveUtil::preset(const QString &id)
{
    const QString key = id.trimmed().toLower();
    SpeedCurve curve;
    curve.preset = key;

    if (key == QStringLiteral("montage")) {
        // Fast rhythm: fast -> slow -> fast -> slow -> fast
        curve.points = {{0.0, 2.0}, {0.25, 0.5}, {0.5, 2.0}, {0.75, 0.5}, {1.0, 2.0}};
    } else if (key == QStringLiteral("hero")) {
        // Hero moment: rapid entrance, dramatic action slow-mo, rapid exit
        curve.points = {{0.0, 2.5}, {0.2, 2.5}, {0.4, 0.3}, {0.6, 0.3}, {0.8, 2.5}, {1.0, 2.5}};
    } else if (key == QStringLiteral("bullet")) {
        // Bullet time: build-up, sudden freeze/slow-mo, acceleration
        curve.points = {{0.0, 1.0}, {0.2, 3.0}, {0.4, 0.2}, {0.6, 0.2}, {0.8, 3.0}, {1.0, 1.0}};
    } else if (key == QStringLiteral("jump")) {
        // Jump ramp: normal pace with abrupt bursts of high speed
        curve.points = {{0.0, 0.6}, {0.3, 0.6}, {0.4, 3.0}, {0.6, 0.6}, {0.7, 3.0}, {1.0, 0.6}};
    } else if (key == QStringLiteral("flash_in")) {
        // Flash in: extreme speed deceleration to normal
        curve.points = {{0.0, 5.0}, {0.2, 3.0}, {0.5, 1.0}, {1.0, 1.0}};
    } else if (key == QStringLiteral("flash_out")) {
        // Flash out: normal speed accelerating to extreme speed
        curve.points = {{0.0, 1.0}, {0.5, 1.0}, {0.8, 3.0}, {1.0, 5.0}};
    } else {
        curve.preset = QStringLiteral("custom");
        curve.points = {{0.0, 1.0}, {1.0, 1.0}};
    }
    return curve;
}

QStringList SpeedCurveUtil::presetIds()
{
    return {
        QStringLiteral("montage"),
        QStringLiteral("hero"),
        QStringLiteral("bullet"),
        QStringLiteral("jump"),
        QStringLiteral("flash_in"),
        QStringLiteral("flash_out"),
        QStringLiteral("custom")
    };
}

QString SpeedCurveUtil::presetTitle(const QString &id, const QString &lang)
{
    const QString key = id.trimmed().toLower();
    const bool it = lang.startsWith(QStringLiteral("it"), Qt::CaseInsensitive);

    if (key == QStringLiteral("montage")) {
        return it ? QStringLiteral("Montaggio") : QStringLiteral("Montage");
    }
    if (key == QStringLiteral("hero")) {
        return it ? QStringLiteral("Eroe") : QStringLiteral("Hero");
    }
    if (key == QStringLiteral("bullet")) {
        return it ? QStringLiteral("Proiettile") : QStringLiteral("Bullet time");
    }
    if (key == QStringLiteral("jump")) {
        return it ? QStringLiteral("Salto") : QStringLiteral("Jump");
    }
    if (key == QStringLiteral("flash_in")) {
        return QStringLiteral("Flash in");
    }
    if (key == QStringLiteral("flash_out")) {
        return QStringLiteral("Flash out");
    }
    if (key == QStringLiteral("custom")) {
        return it ? QStringLiteral("Personalizzata") : QStringLiteral("Custom");
    }
    return id;
}

std::vector<std::pair<double, double>> SpeedCurveUtil::normalizedPoints(const SpeedCurve &curve)
{
    if (curve.points.empty()) {
        return {{0.0, 1.0}, {1.0, 1.0}};
    }

    std::vector<std::pair<double, double>> pts = curve.points;
    for (auto &[x, y] : pts) {
        x = std::clamp(x, 0.0, 1.0);
        y = std::clamp(y, 0.05, 100.0);
    }
    std::sort(pts.begin(), pts.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    // Clean duplicate X coordinates
    std::vector<std::pair<double, double>> clean;
    clean.reserve(pts.size() + 2);
    for (const auto &p : pts) {
        if (!clean.empty() && std::abs(clean.back().first - p.first) < 1e-6) {
            clean.back().second = p.second;
        } else {
            clean.push_back(p);
        }
    }

    if (clean.empty()) {
        return {{0.0, 1.0}, {1.0, 1.0}};
    }
    if (clean.front().first > 0.0) {
        clean.insert(clean.begin(), {0.0, clean.front().second});
    }
    if (clean.back().first < 1.0) {
        clean.push_back({1.0, clean.back().second});
    }
    return clean;
}

double SpeedCurveUtil::speedAt(const SpeedCurve &curve, double u)
{
    const SplineData s = buildSpline(curve);
    const int m = static_cast<int>(s.x.size()) - 1;
    if (m <= 0) {
        return 1.0;
    }

    const double clampedU = std::clamp(u, 0.0, 1.0);
    int k = 0;
    while (k < m - 1 && clampedU > s.x[static_cast<size_t>(k + 1)]) {
        ++k;
    }
    const double hk = s.x[static_cast<size_t>(k + 1)] - s.x[static_cast<size_t>(k)];
    const double t = (hk > 1e-6) ? (clampedU - s.x[static_cast<size_t>(k)]) / hk : 0.0;
    return evalSegmentSpeed(s, k, std::clamp(t, 0.0, 1.0));
}

double SpeedCurveUtil::integratedTime(const SpeedCurve &curve, double u)
{
    const SplineData s = buildSpline(curve);
    const int m = static_cast<int>(s.x.size()) - 1;
    if (m <= 0) {
        return std::clamp(u, 0.0, 1.0);
    }

    const double clampedU = std::clamp(u, 0.0, 1.0);
    if (clampedU <= 0.0) {
        return 0.0;
    }
    if (clampedU >= 1.0) {
        return s.cumIntegral[static_cast<size_t>(m)];
    }

    int k = 0;
    while (k < m - 1 && clampedU > s.x[static_cast<size_t>(k + 1)]) {
        ++k;
    }
    const double hk = s.x[static_cast<size_t>(k + 1)] - s.x[static_cast<size_t>(k)];
    const double t = (hk > 1e-6) ? (clampedU - s.x[static_cast<size_t>(k)]) / hk : 0.0;
    const double partial = evalSegmentIntegral(s, k, std::clamp(t, 0.0, 1.0));
    return s.cumIntegral[static_cast<size_t>(k)] + partial;
}

double SpeedCurveUtil::averageSpeed(const SpeedCurve &curve)
{
    return integratedTime(curve, 1.0);
}

double SpeedCurveUtil::progressAtSource(const SpeedCurve &curve, double normalizedSource)
{
    const SplineData s = buildSpline(curve);
    const int m = static_cast<int>(s.x.size()) - 1;
    if (m <= 0) {
        return std::clamp(normalizedSource, 0.0, 1.0);
    }

    const double total = s.cumIntegral[static_cast<size_t>(m)];
    if (total <= 1e-6) {
        return std::clamp(normalizedSource, 0.0, 1.0);
    }

    const double targetIntegral = std::clamp(normalizedSource, 0.0, 1.0) * total;
    if (targetIntegral <= 0.0) {
        return 0.0;
    }
    if (targetIntegral >= total) {
        return 1.0;
    }

    // Locate segment
    int k = 0;
    while (k < m - 1 && s.cumIntegral[static_cast<size_t>(k + 1)] < targetIntegral) {
        ++k;
    }

    const double segBase = s.cumIntegral[static_cast<size_t>(k)];
    const double segTarget = targetIntegral - segBase;
    const double segTotal = s.segIntegral[static_cast<size_t>(k)];
    const double hk = s.x[static_cast<size_t>(k + 1)] - s.x[static_cast<size_t>(k)];

    // Newton-Raphson to solve evalSegmentIntegral(s, k, t) == segTarget
    double t = (segTotal > 1e-6) ? std::clamp(segTarget / segTotal, 0.0, 1.0) : 0.5;
    for (int iter = 0; iter < 6; ++iter) {
        const double f = evalSegmentIntegral(s, k, t) - segTarget;
        const double fp = hk * evalSegmentSpeed(s, k, t);
        if (std::abs(f) < 1e-7 || fp <= 1e-6) {
            break;
        }
        t = std::clamp(t - f / fp, 0.0, 1.0);
    }

    return std::clamp(s.x[static_cast<size_t>(k)] + t * hk, 0.0, 1.0);
}

RationalTime SpeedCurveUtil::sourceTimeAt(const SpeedCurve &curve,
                                         const RationalTime &sourceIn,
                                         const RationalTime &duration,
                                         const RationalTime &timelineOffset,
                                         bool reversed)
{
    const double durSec = duration.toSecondsDouble();
    if (durSec <= 0.0) {
        return sourceIn;
    }
    const double offSec = timelineOffset.toSecondsDouble();
    const double u = std::clamp(offSec / durSec, 0.0, 1.0);
    const double integral = integratedTime(curve, u);
    const double sourceOffsetSec = integral * durSec;

    const Rational rate = sourceIn.rate();
    if (!reversed) {
        return sourceIn + RationalTime(std::llround(sourceOffsetSec * rate.toDouble()), rate);
    }

    const double totalSourceSec = averageSpeed(curve) * durSec;
    const double playedBackwards = std::max(0.0, totalSourceSec - sourceOffsetSec);
    return sourceIn + RationalTime(std::llround(playedBackwards * rate.toDouble()), rate);
}

RationalTime SpeedCurveUtil::timelineOffsetAtSourceTime(const SpeedCurve &curve,
                                                       const RationalTime &sourceIn,
                                                       const RationalTime &duration,
                                                       const RationalTime &sourceTime,
                                                       bool reversed)
{
    const double durSec = duration.toSecondsDouble();
    const double avg = averageSpeed(curve);
    const double totalSourceSec = avg * durSec;
    if (durSec <= 0.0 || totalSourceSec <= 0.0) {
        return RationalTime(0, duration.rate());
    }

    const double sourceDeltaSec = (sourceTime - sourceIn).toSecondsDouble();
    double normSource = std::clamp(sourceDeltaSec / totalSourceSec, 0.0, 1.0);
    if (reversed) {
        normSource = 1.0 - normSource;
    }

    const double u = progressAtSource(curve, normSource);
    const Rational rate = duration.rate();
    return RationalTime(std::llround(u * durSec * rate.toDouble()), rate);
}

} // namespace velacut
