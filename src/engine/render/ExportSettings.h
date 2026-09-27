// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/time/RationalTime.h"

#include <QJsonObject>
#include <QSize>
#include <QString>

#include <optional>

namespace vedit::engine {

// "Quality" in the export window (plain language, SPEC 0bis rule 7): mapped to encoder settings here.
enum class ExportQuality
{
    Low,
    Recommended,
    High,
};

// What the user chose in the export window. Phase 1: MP4, H.264 + AAC, software encoder (hardware encoders with
// fallback arrive in Phase 8).
struct ExportSettings
{
    QString outputPath;
    QSize size;
    Rational frameRate;
    ExportQuality quality = ExportQuality::Recommended;
    bool normalizeLoudness = false;
    double targetLufs = -14.0;

    QJsonObject toJson() const;
    static std::optional<ExportSettings> fromJson(const QJsonObject &json);
    friend bool operator==(const ExportSettings &, const ExportSettings &) = default;
};

// Canvas scaled so that its short side is `shortSide` ("720p", "1080p" of any orientation), even dimensions.
QSize scaledToShortSide(QSize canvas, int shortSide);

struct EncoderParameters
{
    int crf = 21;                  // constant quality…
    qint64 videoMaxBitrate = 0;    // …capped (bit/s), which also bounds the size estimate
    QByteArray preset;             // x264 speed/efficiency trade-off
    int audioBitrate = 192000;     // bit/s
};
EncoderParameters encoderParameters(const ExportSettings &settings);

// Expected size in bytes of an export of `duration` (an estimate: constant quality varies with the content).
qint64 estimatedFileSize(const ExportSettings &settings, const RationalTime &duration);

// Failures reported by vedit-render: codes on the wire, translated messages in the editor.
enum class RenderError
{
    None,
    MltUnavailable,
    ProjectUnreadable,
    SequenceMissing,
    NothingToExport,
    OutputNotWritable,
    EncoderFailed,
};
QString renderErrorCode(RenderError error);
RenderError renderErrorFromCode(const QString &code);

} // namespace vedit::engine
