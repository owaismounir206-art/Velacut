// SPDX-License-Identifier: GPL-3.0-or-later
#include "Grade.h"

#include <QFile>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace vedit::fx {

namespace {

constexpr std::array<const char *, Grade::kRanges> kRangeNames{"red", "orange", "yellow", "green",
                                                               "aqua", "blue",   "purple", "magenta"};
// Centre hue of each range, degrees.
constexpr std::array<double, Grade::kRanges> kRangeHues{0, 30, 60, 120, 180, 240, 270, 300};

double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

// How much of each range a hue belongs to: linear between the two neighbouring centres (the weights sum to 1).
std::array<double, Grade::kRanges> rangeWeights(double hue)
{
    std::array<double, Grade::kRanges> weights{};
    for (int i = 0; i < Grade::kRanges; ++i) {
        const double from = kRangeHues[static_cast<size_t>(i)];
        const double to = i + 1 < Grade::kRanges ? kRangeHues[static_cast<size_t>(i) + 1] : 360.0;
        if (hue >= from && hue < to) {
            const double t = (hue - from) / (to - from);
            weights[static_cast<size_t>(i)] = 1.0 - t;
            weights[static_cast<size_t>((i + 1) % Grade::kRanges)] = t;
            break;
        }
    }
    return weights;
}

void toHsl(const std::array<double, 3> &c, double &h, double &s, double &l)
{
    const double maxC = std::max({c[0], c[1], c[2]});
    const double minC = std::min({c[0], c[1], c[2]});
    l = (maxC + minC) / 2;
    const double d = maxC - minC;
    if (d < 1e-9) {
        h = 0;
        s = 0;
        return;
    }
    s = l > 0.5 ? d / (2 - maxC - minC) : d / (maxC + minC);
    if (maxC == c[0]) {
        h = (c[1] - c[2]) / d + (c[1] < c[2] ? 6 : 0);
    } else if (maxC == c[1]) {
        h = (c[2] - c[0]) / d + 2;
    } else {
        h = (c[0] - c[1]) / d + 4;
    }
    h *= 60;
}

double hueChannel(double p, double q, double t)
{
    t = t < 0 ? t + 1 : t > 1 ? t - 1 : t;
    if (t < 1.0 / 6) {
        return p + (q - p) * 6 * t;
    }
    if (t < 0.5) {
        return q;
    }
    if (t < 2.0 / 3) {
        return p + (q - p) * (2.0 / 3 - t) * 6;
    }
    return p;
}

std::array<double, 3> fromHsl(double h, double s, double l)
{
    if (s <= 0) {
        return {l, l, l};
    }
    const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const double p = 2 * l - q;
    const double t = h / 360.0;
    return {hueChannel(p, q, t + 1.0 / 3), hueChannel(p, q, t), hueChannel(p, q, t - 1.0 / 3)};
}

Grade::Curve curveFromJson(const QJsonValue &value)
{
    Grade::Curve curve;
    for (const QJsonValue &point : value.toArray()) {
        const QJsonArray xy = point.toArray();
        if (xy.size() == 2) {
            curve.emplace_back(clamp01(xy[0].toDouble()), clamp01(xy[1].toDouble()));
        }
    }
    std::sort(curve.begin(), curve.end());
    return curve;
}

QJsonArray curveToJson(const Grade::Curve &curve)
{
    QJsonArray array;
    for (const auto &[x, y] : curve) {
        array.append(QJsonArray{x, y});
    }
    return array;
}

} // namespace

QString Grade::rangeName(int range)
{
    return QString::fromLatin1(kRangeNames[static_cast<size_t>(std::clamp(range, 0, kRanges - 1))]);
}

bool Grade::isIdentity() const
{
    return *this == Grade{};
}

Grade Grade::fromJson(const QJsonObject &params)
{
    Grade grade;
    const char *channels[] = {"r", "g", "b"};
    for (int c = 0; c < 3; ++c) {
        const QString suffix = QString::fromLatin1(channels[c]);
        grade.balance[static_cast<size_t>(c)] = std::clamp(params.value(u"balance."_s + suffix).toDouble(1.0), 0.0, 4.0);
        grade.shadows.colour[static_cast<size_t>(c)] = std::clamp(params.value(u"shadows."_s + suffix).toDouble(), -1.0, 1.0);
        grade.midtones.colour[static_cast<size_t>(c)] = std::clamp(params.value(u"midtones."_s + suffix).toDouble(), -1.0, 1.0);
        grade.highlights.colour[static_cast<size_t>(c)] =
            std::clamp(params.value(u"highlights."_s + suffix).toDouble(), -1.0, 1.0);
    }
    grade.shadows.level = std::clamp(params.value(u"shadows.level"_s).toDouble(), -1.0, 1.0);
    grade.midtones.level = std::clamp(params.value(u"midtones.level"_s).toDouble(), -1.0, 1.0);
    grade.highlights.level = std::clamp(params.value(u"highlights.level"_s).toDouble(), -1.0, 1.0);
    for (int i = 0; i < kRanges; ++i) {
        const QString prefix = u"hsl."_s + rangeName(i) + u'.';
        HslRange &range = grade.hsl[static_cast<size_t>(i)];
        range.hue = std::clamp(params.value(prefix + u"hue"_s).toDouble(), -1.0, 1.0);
        range.saturation = std::clamp(params.value(prefix + u"saturation"_s).toDouble(), -1.0, 1.0);
        range.lightness = std::clamp(params.value(prefix + u"lightness"_s).toDouble(), -1.0, 1.0);
    }
    grade.master = curveFromJson(params.value(u"curve.master"_s));
    grade.channel[0] = curveFromJson(params.value(u"curve.red"_s));
    grade.channel[1] = curveFromJson(params.value(u"curve.green"_s));
    grade.channel[2] = curveFromJson(params.value(u"curve.blue"_s));
    return grade;
}

QJsonObject Grade::toJson() const
{
    QJsonObject params;
    const char *channels[] = {"r", "g", "b"};
    for (int c = 0; c < 3; ++c) {
        const QString suffix = QString::fromLatin1(channels[c]);
        const auto set = [&params](const QString &key, double value, double neutral) {
            if (value != neutral) {
                params.insert(key, value);
            }
        };
        set(u"balance."_s + suffix, balance[static_cast<size_t>(c)], 1.0);
        set(u"shadows."_s + suffix, shadows.colour[static_cast<size_t>(c)], 0.0);
        set(u"midtones."_s + suffix, midtones.colour[static_cast<size_t>(c)], 0.0);
        set(u"highlights."_s + suffix, highlights.colour[static_cast<size_t>(c)], 0.0);
    }
    for (const auto &[key, wheel] : {std::pair{u"shadows"_s, shadows}, {u"midtones"_s, midtones}, {u"highlights"_s, highlights}}) {
        if (wheel.level != 0) {
            params.insert(key + u".level"_s, wheel.level);
        }
    }
    for (int i = 0; i < kRanges; ++i) {
        const HslRange &range = hsl[static_cast<size_t>(i)];
        const QString prefix = u"hsl."_s + rangeName(i) + u'.';
        if (range.hue != 0) {
            params.insert(prefix + u"hue"_s, range.hue);
        }
        if (range.saturation != 0) {
            params.insert(prefix + u"saturation"_s, range.saturation);
        }
        if (range.lightness != 0) {
            params.insert(prefix + u"lightness"_s, range.lightness);
        }
    }
    const std::pair<QString, const Curve *> curves[] = {
        {u"curve.master"_s, &master}, {u"curve.red"_s, &channel[0]}, {u"curve.green"_s, &channel[1]}, {u"curve.blue"_s, &channel[2]}};
    for (const auto &[key, curve] : curves) {
        if (!curve->empty()) {
            params.insert(key, curveToJson(*curve));
        }
    }
    return params;
}

double evaluateCurve(const Grade::Curve &curve, double x)
{
    if (curve.empty()) {
        return x;
    }
    // The points with the ends added.
    std::vector<std::pair<double, double>> p;
    if (curve.front().first > 0) {
        p.emplace_back(0.0, 0.0);
    }
    for (const auto &point : curve) {
        if (p.empty() || point.first > p.back().first) {
            p.push_back(point);
        }
    }
    if (p.back().first < 1) {
        p.emplace_back(1.0, 1.0);
    }
    const size_t n = p.size();
    if (n == 1 || x <= p.front().first) {
        return p.front().second;
    }
    if (x >= p.back().first) {
        return p.back().second;
    }
    // Fritsch–Carlson monotone cubic Hermite interpolation.
    std::vector<double> delta(n - 1), m(n);
    for (size_t i = 0; i + 1 < n; ++i) {
        delta[i] = (p[i + 1].second - p[i].second) / (p[i + 1].first - p[i].first);
    }
    m[0] = delta[0];
    m[n - 1] = delta[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) {
        m[i] = delta[i - 1] * delta[i] <= 0 ? 0.0 : (delta[i - 1] + delta[i]) / 2;
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        if (delta[i] == 0) {
            m[i] = m[i + 1] = 0;
            continue;
        }
        const double a = m[i] / delta[i];
        const double b = m[i + 1] / delta[i];
        const double s = a * a + b * b;
        if (s > 9) {
            const double t = 3 / std::sqrt(s);
            m[i] = t * a * delta[i];
            m[i + 1] = t * b * delta[i];
        }
    }
    size_t k = 0;
    while (k + 2 < n && x > p[k + 1].first) {
        ++k;
    }
    const double h = p[k + 1].first - p[k].first;
    const double t = (x - p[k].first) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * p[k].second + (t3 - 2 * t2 + t) * h * m[k] + (-2 * t3 + 3 * t2) * p[k + 1].second +
           (t3 - t2) * h * m[k + 1];
}

std::array<double, 3> applyGrade(const Grade &grade, std::array<double, 3> c)
{
    // White balance.
    for (int i = 0; i < 3; ++i) {
        c[static_cast<size_t>(i)] *= grade.balance[static_cast<size_t>(i)];
    }
    // Wheels: lift (shadows), gain (highlights), then gamma (midtones), per channel.
    for (size_t i = 0; i < 3; ++i) {
        const double lift = 0.25 * (grade.shadows.colour[i] + grade.shadows.level);
        const double gain = 1.0 + 0.5 * (grade.highlights.colour[i] + grade.highlights.level);
        const double gamma = std::pow(2.0, -(grade.midtones.colour[i] + grade.midtones.level));
        double v = clamp01(c[i]);
        v = v * gain + lift * (1.0 - v);
        c[i] = std::pow(clamp01(v), gamma);
    }
    // HSL per colour range: greys are not touched (the changes follow the saturation).
    bool anyHsl = false;
    for (const Grade::HslRange &range : grade.hsl) {
        anyHsl = anyHsl || range.hue != 0 || range.saturation != 0 || range.lightness != 0;
    }
    if (anyHsl) {
        double h = 0, s = 0, l = 0;
        toHsl(c, h, s, l);
        if (s > 1e-6) {
            const std::array<double, Grade::kRanges> weights = rangeWeights(h);
            double hueShift = 0, saturation = 0, lightness = 0;
            for (size_t i = 0; i < weights.size(); ++i) {
                hueShift += weights[i] * grade.hsl[i].hue;
                saturation += weights[i] * grade.hsl[i].saturation;
                lightness += weights[i] * grade.hsl[i].lightness;
            }
            h = std::fmod(h + 30.0 * hueShift + 360.0, 360.0);
            const double strength = std::min(1.0, s * 2); // weak colours move less
            l = clamp01(l + 0.25 * lightness * strength);
            s = clamp01(s * (1.0 + saturation));
            c = fromHsl(h, s, l);
        }
    }
    // Curves: the master one, then each channel.
    for (size_t i = 0; i < 3; ++i) {
        c[i] = evaluateCurve(grade.channel[i], evaluateCurve(grade.master, clamp01(c[i])));
    }
    for (double &v : c) {
        v = clamp01(v);
    }
    return c;
}

// ---- .cube ------------------------------------------------------------------------------------------------------

std::optional<CubeLut> CubeLut::parse(const QByteArray &text, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<CubeLut> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    CubeLut lut;
    int expected = 0;
    int lineNumber = 0;
    for (const QByteArray &rawLine : text.split('\n')) {
        ++lineNumber;
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QList<QByteArray> words = line.simplified().split(' ');
        const QByteArray keyword = words.front().toUpper();
        if (keyword == "TITLE") {
            lut.m_title = QString::fromUtf8(line.mid(5).trimmed()).remove(u'"');
        } else if (keyword == "LUT_3D_SIZE" || keyword == "LUT_1D_SIZE") {
            lut.m_3d = keyword == "LUT_3D_SIZE";
            lut.m_size = words.value(1).toInt();
            const int limit = lut.m_3d ? 256 : 65536;
            if (lut.m_size < 2 || lut.m_size > limit) {
                return fail(u"line %1: invalid size"_s.arg(lineNumber));
            }
            expected = lut.m_3d ? lut.m_size * lut.m_size * lut.m_size : lut.m_size;
            lut.m_table.reserve(static_cast<size_t>(expected));
        } else if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
            if (words.size() != 4) {
                return fail(u"line %1: invalid domain"_s.arg(lineNumber));
            }
            std::array<double, 3> &target = keyword == "DOMAIN_MIN" ? lut.m_min : lut.m_max;
            for (int i = 0; i < 3; ++i) {
                target[static_cast<size_t>(i)] = words[i + 1].toDouble();
            }
        } else if (keyword == "LUT_3D_INPUT_RANGE" || keyword == "LUT_1D_INPUT_RANGE") {
            if (words.size() == 3) {
                lut.m_min.fill(words[1].toDouble());
                lut.m_max.fill(words[2].toDouble());
            }
        } else {
            if (words.size() != 3 || expected == 0) {
                return fail(u"line %1: unexpected '%2'"_s.arg(lineNumber).arg(QString::fromUtf8(line.left(40))));
            }
            bool ok = true;
            std::array<float, 3> value{};
            for (int i = 0; i < 3 && ok; ++i) {
                value[static_cast<size_t>(i)] = words[i].toFloat(&ok);
            }
            if (!ok) {
                return fail(u"line %1: invalid number"_s.arg(lineNumber));
            }
            lut.m_table.push_back(value);
        }
    }
    if (expected == 0) {
        return fail(u"no LUT_3D_SIZE or LUT_1D_SIZE"_s);
    }
    if (static_cast<int>(lut.m_table.size()) != expected) {
        return fail(u"%1 values instead of %2"_s.arg(lut.m_table.size()).arg(expected));
    }
    for (int i = 0; i < 3; ++i) {
        if (!(lut.m_max[static_cast<size_t>(i)] > lut.m_min[static_cast<size_t>(i)])) {
            return fail(u"invalid domain"_s);
        }
    }
    return lut;
}

std::optional<CubeLut> CubeLut::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return std::nullopt;
    }
    return parse(file.read(64 * 1024 * 1024), error);
}

std::array<double, 3> CubeLut::sample(std::array<double, 3> rgb) const
{
    const int n = m_size;
    std::array<double, 3> position{};
    for (size_t i = 0; i < 3; ++i) {
        position[i] = std::clamp((rgb[i] - m_min[i]) / (m_max[i] - m_min[i]), 0.0, 1.0) * (n - 1);
    }
    if (!m_3d) {
        std::array<double, 3> out{};
        for (size_t i = 0; i < 3; ++i) {
            const int i0 = std::min(static_cast<int>(position[i]), n - 2);
            const double t = position[i] - i0;
            out[i] = m_table[static_cast<size_t>(i0)][i] * (1 - t) + m_table[static_cast<size_t>(i0) + 1][i] * t;
        }
        return out;
    }
    const int r0 = std::min(static_cast<int>(position[0]), n - 2);
    const int g0 = std::min(static_cast<int>(position[1]), n - 2);
    const int b0 = std::min(static_cast<int>(position[2]), n - 2);
    const double tr = position[0] - r0, tg = position[1] - g0, tb = position[2] - b0;
    const auto at = [this, n](int r, int g, int b) -> const std::array<float, 3> & {
        return m_table[static_cast<size_t>(r + g * n + b * n * n)];
    };
    std::array<double, 3> out{};
    for (size_t i = 0; i < 3; ++i) {
        const double c00 = at(r0, g0, b0)[i] * (1 - tr) + at(r0 + 1, g0, b0)[i] * tr;
        const double c10 = at(r0, g0 + 1, b0)[i] * (1 - tr) + at(r0 + 1, g0 + 1, b0)[i] * tr;
        const double c01 = at(r0, g0, b0 + 1)[i] * (1 - tr) + at(r0 + 1, g0, b0 + 1)[i] * tr;
        const double c11 = at(r0, g0 + 1, b0 + 1)[i] * (1 - tr) + at(r0 + 1, g0 + 1, b0 + 1)[i] * tr;
        const double c0 = c00 * (1 - tg) + c10 * tg;
        const double c1 = c01 * (1 - tg) + c11 * tg;
        out[i] = c0 * (1 - tb) + c1 * tb;
    }
    return out;
}

} // namespace vedit::fx
