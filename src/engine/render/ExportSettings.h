// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/time/RationalTime.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

namespace vedit::engine {

// "Quality" in the export window (plain language, SPEC 0bis rule 7): mapped to encoder settings here.
enum class ExportQuality
{
    Low,
    Recommended,
    High,
};

// Video codec offered in the export window's "Advanced" section (SPEC §5.15). H.264 is the default: it plays
// everywhere; HEVC and AV1 make smaller files.
enum class VideoCodec
{
    H264,
    HEVC,
    AV1,
};
QString videoCodecName(VideoCodec codec);      // "h264", "hevc", "av1": also the JSON value
std::optional<VideoCodec> videoCodecFromName(QStringView name);

// Hardware encoding (SPEC §5.15): Auto uses the GPU encoder when the probe verified one, Off always encodes in
// software. There is no "forced" mode: if the GPU encoder fails, the export restarts in software (never a failed
// export because of a driver).
enum class HardwareEncoder
{
    Auto,
    Off,
};
QString hardwareEncoderName(HardwareEncoder encoder); // "auto" | "off": the JSON value
std::optional<HardwareEncoder> hardwareEncoderFromName(QStringView name);

// What the user chose in the export window. Phase 1: MP4, H.264 + AAC, software encoder (hardware encoders with
// fallback arrive in Phase 8).
struct ExportSettings
{
    QString outputPath;
    QSize size;
    Rational frameRate;
    ExportQuality quality = ExportQuality::Recommended;
    VideoCodec videoCodec = VideoCodec::H264;
    HardwareEncoder hardwareEncoder = HardwareEncoder::Auto;
    qint64 maxFileSizeMB = 0; // 0 = no size target; otherwise bitrate is computed to respect it
    bool normalizeLoudness = false;
    double targetLufs = -14.0;
    // A picture attached to the file as its cover (SPEC §5.13ter): MP4/MOV only; empty = none.
    QString coverImage;

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

// The encoder vedit-render will actually use, after the user's choice meets the machine (SPEC §5.15 and 1bis
// rule 3). Pure function, tested: the renderer just follows it.
struct EncoderPlan
{
    QByteArray vcodec;         // "libx264", "h264_vaapi", "libsvtav1"…
    bool hardware = false;     // a GPU encoder (no pix_fmt forcing, no x264 preset)
    QByteArray qualityOption;  // "crf" (software) or "quality" (VA-API & co.)
    int qualityValue = 21;     // scale: lower = better looking
    QByteArray preset;         // only for x264/x265 (SVT-AV1 takes a numeric preset)
    qint64 videoBitrate = 0;   // > 0: average-bitrate mode (maximum file size)
    qint64 videoMaxBitrate = 0; // constant quality cap, or the average bitrate's ceiling
    int audioBitrate = 192000;  // bit/s
    bool twoPass = false;       // software only: two passes to respect a size target
};
// `availableHardwareEncoders`: verified by the GPU probe (GpuCapabilities::video.encoders), passed to
// vedit-render through the job. `duration` of the sequence, needed only by the size-target mode.
EncoderPlan planEncoder(const ExportSettings &settings, const QStringList &availableHardwareEncoders,
                        const RationalTime &duration);

// Video average bitrate that respects settings.maxFileSizeMB (0 when there is no size target). The container
// and audio take a share; the codec family needs fewer bits for the same look (HEVC ~0.6×, AV1 ~0.5× H.264).
qint64 targetVideoBitrate(const ExportSettings &settings, const RationalTime &duration);

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
