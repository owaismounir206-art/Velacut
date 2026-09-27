// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExportSettings.h"

#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace vedit::engine {

namespace {

constexpr std::array kQualityNames{std::pair{ExportQuality::Low, "low"}, std::pair{ExportQuality::Recommended, "recommended"},
                                   std::pair{ExportQuality::High, "high"}};

constexpr std::array kErrorCodes{
    std::pair{RenderError::None, "none"},
    std::pair{RenderError::MltUnavailable, "mlt-unavailable"},
    std::pair{RenderError::ProjectUnreadable, "project-unreadable"},
    std::pair{RenderError::SequenceMissing, "sequence-missing"},
    std::pair{RenderError::NothingToExport, "nothing-to-export"},
    std::pair{RenderError::OutputNotWritable, "output-not-writable"},
    std::pair{RenderError::EncoderFailed, "encoder-failed"},
};

int even(double value)
{
    return std::max(2, static_cast<int>(std::lround(value / 2.0)) * 2);
}

} // namespace

QJsonObject ExportSettings::toJson() const
{
    QString quality;
    for (const auto &[value, name] : kQualityNames) {
        if (value == this->quality) {
            quality = QLatin1StringView(name);
        }
    }
    return QJsonObject{{u"output"_s, outputPath},
                       {u"width"_s, size.width()},
                       {u"height"_s, size.height()},
                       {u"frameRate"_s, frameRate.toString()},
                       {u"quality"_s, quality},
                       {u"normalizeLoudness"_s, normalizeLoudness},
                       {u"targetLufs"_s, targetLufs}};
}

std::optional<ExportSettings> ExportSettings::fromJson(const QJsonObject &json)
{
    ExportSettings settings;
    settings.outputPath = json.value(u"output"_s).toString();
    settings.size = QSize(json.value(u"width"_s).toInt(), json.value(u"height"_s).toInt());
    const std::optional<Rational> rate = Rational::fromString(json.value(u"frameRate"_s).toString());
    if (settings.outputPath.isEmpty() || settings.size.width() < 2 || settings.size.height() < 2 || !rate ||
        rate->num() <= 0) {
        return std::nullopt;
    }
    settings.frameRate = *rate;
    const QString quality = json.value(u"quality"_s).toString();
    for (const auto &[value, name] : kQualityNames) {
        if (quality == QLatin1StringView(name)) {
            settings.quality = value;
        }
    }
    if (json.contains(u"normalizeLoudness"_s)) {
        settings.normalizeLoudness = json.value(u"normalizeLoudness"_s).toBool();
    }
    if (json.contains(u"targetLufs"_s)) {
        settings.targetLufs = json.value(u"targetLufs"_s).toDouble(-14.0);
    }
    return settings;
}

QSize scaledToShortSide(QSize canvas, int shortSide)
{
    if (canvas.isEmpty()) {
        return {};
    }
    const double scale = static_cast<double>(shortSide) / std::min(canvas.width(), canvas.height());
    return QSize(even(canvas.width() * scale), even(canvas.height() * scale));
}

EncoderParameters encoderParameters(const ExportSettings &settings)
{
    // Bits per pixel of the bitrate cap: generous enough that the cap only bounds hard content.
    double bitsPerPixel = 0.10;
    EncoderParameters parameters;
    switch (settings.quality) {
    case ExportQuality::Low:
        parameters.crf = 26;
        parameters.preset = "veryfast";
        parameters.audioBitrate = 128000;
        bitsPerPixel = 0.06;
        break;
    case ExportQuality::Recommended:
        parameters.crf = 21;
        parameters.preset = "fast";
        break;
    case ExportQuality::High:
        parameters.crf = 17;
        parameters.preset = "fast";
        parameters.audioBitrate = 256000;
        bitsPerPixel = 0.16;
        break;
    }
    const double pixelsPerSecond = static_cast<double>(settings.size.width()) * settings.size.height() *
                                   settings.frameRate.toDouble();
    parameters.videoMaxBitrate = std::max<qint64>(500000, std::llround(pixelsPerSecond * bitsPerPixel));
    return parameters;
}

qint64 estimatedFileSize(const ExportSettings &settings, const RationalTime &duration)
{
    const EncoderParameters parameters = encoderParameters(settings);
    // Assumption: constant quality averages ~70% of the cap. Rough; to be calibrated on real footage (PROGRESS).
    const double bitsPerSecond = parameters.videoMaxBitrate * 0.7 + parameters.audioBitrate;
    return std::llround(bitsPerSecond * duration.toSecondsDouble() / 8.0) + 64 * 1024;
}

QString renderErrorCode(RenderError error)
{
    for (const auto &[value, code] : kErrorCodes) {
        if (value == error) {
            return QLatin1StringView(code);
        }
    }
    return {};
}

RenderError renderErrorFromCode(const QString &code)
{
    for (const auto &[value, name] : kErrorCodes) {
        if (code == QLatin1StringView(name)) {
            return value;
        }
    }
    return RenderError::EncoderFailed;
}

} // namespace vedit::engine
