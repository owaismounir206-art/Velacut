// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/time/RationalTime.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace velacut::engine {

// "Quality" in the export window (plain language, SPEC 0bis rule 7): mapped to encoder settings here.
enum class ExportQuality
{
    Low,
    Recommended,
    High,
};

// Video codec offered in the export window's "Advanced" section (SPEC §5.15). H.264 is the default: it plays
// everywhere; HEVC and AV1 make smaller files; ProRes (MOV) is for editing elsewhere; VP9 and AV1 go in WebM.
enum class VideoCodec
{
    H264,
    HEVC,
    AV1,
    ProRes,
    VP9,
};
QString videoCodecName(VideoCodec codec);      // "h264", "hevc", "av1", "prores", "vp9": also the JSON value
std::optional<VideoCodec> videoCodecFromName(QStringView name);

// What the export makes (SPEC §5.15): a video file, an animated GIF, a folder of PNG pictures (one per frame) or the
// sound alone.
enum class ExportFormat
{
    Mp4,
    Mov,
    WebM,
    Gif,
    Images,
    Mp3,
    Wav,
    M4a,
    Flac,
};
QString exportFormatName(ExportFormat format); // "mp4", "mov", "webm", "gif", "png", "mp3", "wav", "m4a", "flac"
std::optional<ExportFormat> exportFormatFromName(QStringView name);
// The file's suffix; empty for Images (the output is a folder).
QString exportSuffix(ExportFormat format);
bool hasVideo(ExportFormat format);
bool hasAudio(ExportFormat format);
// The codec really used for a video format: the chosen one if the format holds it, otherwise the format's own (MP4:
// H.264, HEVC, AV1; MOV: ProRes, H.264, HEVC; WebM: VP9, AV1).
VideoCodec codecFor(ExportFormat format, VideoCodec chosen);

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

// What the user chose in the export window.
struct ExportSettings
{
    // A file, or for Images the folder that receives the pictures ("name_00001.png"…).
    QString outputPath;
    ExportFormat format = ExportFormat::Mp4;
    // Only this part of the sequence (between the In and Out points); empty = all of it.
    TimeRange range;
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

// The encoder velacut-render will actually use, after the user's choice meets the machine (SPEC §5.15 and 1bis
// rule 3). Pure function, tested: the renderer just follows it.
struct EncoderPlan
{
    QByteArray vcodec;         // "libx264", "h264_vaapi", "libsvtav1", "prores_ks", "libvpx-vp9"…
    QByteArray pixelFormat;    // forced on software encoders ("yuv420p", ProRes "yuv422p10le")
    bool hardware = false;     // a GPU encoder (no pix_fmt forcing, no x264 preset)
    QByteArray qualityOption;  // "crf" (software), "quality" (VA-API & co.), "profile" (ProRes)
    int qualityValue = 21;     // scale: lower = better looking
    QByteArray preset;         // only for x264/x265 (SVT-AV1 takes a numeric preset)
    qint64 videoBitrate = 0;   // > 0: average-bitrate mode (maximum file size)
    qint64 videoMaxBitrate = 0; // constant quality cap, or the average bitrate's ceiling
    int audioBitrate = 192000;  // bit/s
    bool twoPass = false;       // software only: two passes to respect a size target
    QByteArray acodec = "aac";  // the sound's codec for the format; empty = no sound
    // Other options of the encoder, as MLT passes them to FFmpeg (VP9: "row-mt", "cpu-used"…).
    std::vector<std::pair<QByteArray, QByteArray>> options;
};
// `availableHardwareEncoders`: verified by the GPU probe (GpuCapabilities::video.encoders), passed to
// velacut-render through the job. `duration` of the sequence, needed only by the size-target mode.
EncoderPlan planEncoder(const ExportSettings &settings, const QStringList &availableHardwareEncoders,
                        const RationalTime &duration);

// Video average bitrate that respects settings.maxFileSizeMB (0 when there is no size target). The container
// and audio take a share; the codec family needs fewer bits for the same look (HEVC ~0.6×, AV1 ~0.5× H.264).
qint64 targetVideoBitrate(const ExportSettings &settings, const RationalTime &duration);

// How long the export is: the range if there is one (clamped to the sequence), else the sequence.
RationalTime exportedDuration(const ExportSettings &settings, const RationalTime &sequenceDuration);

// Expected size in bytes of an export of `duration` (an estimate: constant quality varies with the content).
qint64 estimatedFileSize(const ExportSettings &settings, const RationalTime &duration);

// Failures reported by velacut-render: codes on the wire, translated messages in the editor.
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

} // namespace velacut::engine
