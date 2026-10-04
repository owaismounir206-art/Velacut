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

constexpr std::array kCodecNames{std::pair{VideoCodec::H264, "h264"}, std::pair{VideoCodec::HEVC, "hevc"},
                                  std::pair{VideoCodec::AV1, "av1"}};

constexpr std::array kHardwareNames{std::pair{HardwareEncoder::Auto, "auto"}, std::pair{HardwareEncoder::Off, "off"}};

constexpr std::array kErrorCodes{
    std::pair{RenderError::None, "none"},
    std::pair{RenderError::MltUnavailable, "mlt-unavailable"},
    std::pair{RenderError::ProjectUnreadable, "project-unreadable"},
    std::pair{RenderError::SequenceMissing, "sequence-missing"},
    std::pair{RenderError::NothingToExport, "nothing-to-export"},
    std::pair{RenderError::OutputNotWritable, "output-not-writable"},
    std::pair{RenderError::EncoderFailed, "encoder-failed"},
};

// The GPU encoder suffixes, best first (docs/GPU_COMPATIBILITY.md): VA-API is the common path on Linux (AMD
// Radeon, Intel Arc and older Intel iGPUs, in a virtual machine with passthrough), QSV and NVENC are the vendor
// ones, Vulkan Video the newest. Each is only used if the probe really encoded frames with it.
constexpr std::array kHardwareSuffixes{"vaapi", "qsv", "nvenc", "vulkan"};

// Constant-quality level of the hardware encoders: lower is better. The VA-API range is driver dependent
// (0–32 on Mesa/AMD, 0–52 on Intel); values valid everywhere, chosen from measured exports on a Radeon 740M.
constexpr std::array kHardwareQuality{std::pair{ExportQuality::Low, 28}, std::pair{ExportQuality::Recommended, 18},
                                      std::pair{ExportQuality::High, 8}};

// The same look needs fewer bits in the newer codecs; used for the size estimate and the size-target bitrate.
constexpr std::array kCodecSizeFactor{std::pair{VideoCodec::H264, 1.0}, std::pair{VideoCodec::HEVC, 0.6},
                                      std::pair{VideoCodec::AV1, 0.5}};

int even(double value)
{
    return std::max(2, static_cast<int>(std::lround(value / 2.0)) * 2);
}

double codecSizeFactor(VideoCodec codec)
{
    for (const auto &[value, factor] : kCodecSizeFactor) {
        if (value == codec) {
            return factor;
        }
    }
    return 1.0;
}

} // namespace

QString videoCodecName(VideoCodec codec)
{
    for (const auto &[value, name] : kCodecNames) {
        if (value == codec) {
            return QLatin1StringView(name);
        }
    }
    return u"h264"_s;
}

std::optional<VideoCodec> videoCodecFromName(QStringView name)
{
    for (const auto &[value, codecName] : kCodecNames) {
        if (name == QLatin1StringView(codecName)) {
            return value;
        }
    }
    return std::nullopt;
}

QString hardwareEncoderName(HardwareEncoder encoder)
{
    for (const auto &[value, name] : kHardwareNames) {
        if (value == encoder) {
            return QLatin1StringView(name);
        }
    }
    return u"auto"_s;
}

std::optional<HardwareEncoder> hardwareEncoderFromName(QStringView name)
{
    for (const auto &[value, encoderName] : kHardwareNames) {
        if (name == QLatin1StringView(encoderName)) {
            return value;
        }
    }
    return std::nullopt;
}

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
                       {u"codec"_s, videoCodecName(videoCodec)},
                       {u"hardwareEncoder"_s, hardwareEncoderName(hardwareEncoder)},
                       {u"maxFileSizeMB"_s, static_cast<double>(maxFileSizeMB)},
                       {u"normalizeLoudness"_s, normalizeLoudness},
                       {u"targetLufs"_s, targetLufs},
                       {u"cover"_s, coverImage}};
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
    if (const auto codec = videoCodecFromName(json.value(u"codec"_s).toString())) {
        settings.videoCodec = *codec;
    }
    if (const auto hardware = hardwareEncoderFromName(json.value(u"hardwareEncoder"_s).toString())) {
        settings.hardwareEncoder = *hardware;
    }
    if (json.contains(u"maxFileSizeMB"_s)) {
        settings.maxFileSizeMB = std::max<qint64>(0, std::llround(json.value(u"maxFileSizeMB"_s).toDouble()));
    }
    if (json.contains(u"normalizeLoudness"_s)) {
        settings.normalizeLoudness = json.value(u"normalizeLoudness"_s).toBool();
    }
    if (json.contains(u"targetLufs"_s)) {
        settings.targetLufs = json.value(u"targetLufs"_s).toDouble(-14.0);
    }
    settings.coverImage = json.value(u"cover"_s).toString();
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

qint64 targetVideoBitrate(const ExportSettings &settings, const RationalTime &duration)
{
    if (settings.maxFileSizeMB <= 0 || duration.value() <= 0) {
        return 0;
    }
    // What is left for video once the container and the audio took their share.
    const qint64 totalBits = settings.maxFileSizeMB * 1024 * 1024 * 8;
    const qint64 audioBits = std::llround(encoderParameters(settings).audioBitrate * duration.toSecondsDouble());
    const double containerShare = 0.03; // index, cover, subtitles, MP4 overhead
    qint64 videoBits = static_cast<qint64>(totalBits * (1.0 - containerShare)) - audioBits;
    // The newer codecs need fewer bits; do not promise a size that lets the encoder waste quality.
    videoBits = std::llround(videoBits * codecSizeFactor(settings.videoCodec));
    // Bits over the whole video → average bit rate.
    return std::max<qint64>(200000, std::llround(videoBits / duration.toSecondsDouble()));
}

EncoderPlan planEncoder(const ExportSettings &settings, const QStringList &availableHardwareEncoders,
                        const RationalTime &duration)
{
    static constexpr std::array kSoftwareVcodec{std::pair{VideoCodec::H264, "libx264"},
                                               std::pair{VideoCodec::HEVC, "libx265"},
                                               std::pair{VideoCodec::AV1, "libsvtav1"}};
    const EncoderParameters parameters = encoderParameters(settings);

    EncoderPlan plan;
    plan.audioBitrate = parameters.audioBitrate;
    plan.videoMaxBitrate = parameters.videoMaxBitrate;
    plan.qualityOption = "crf";
    plan.qualityValue = parameters.crf;
    for (const auto &[codec, vcodec] : kSoftwareVcodec) {
        if (codec == settings.videoCodec) {
            plan.vcodec = vcodec;
        }
    }
    if (settings.videoCodec != VideoCodec::AV1) { // SVT-AV1's preset is a number, not a name: leave the default
        plan.preset = parameters.preset;
    }

    // Hardware: only an encoder the probe verified on this very machine (SPEC 1bis rule 1: nothing assumed).
    if (settings.hardwareEncoder == HardwareEncoder::Auto) {
        for (const char *suffix : kHardwareSuffixes) {
            const QString candidate = u"%1_%2"_s.arg(videoCodecName(settings.videoCodec)).arg(QLatin1StringView(suffix));
            if (availableHardwareEncoders.contains(candidate)) {
                plan.vcodec = candidate.toLatin1();
                plan.hardware = true;
                plan.qualityOption = "quality";
                plan.preset.clear();
                for (const auto &[quality, level] : kHardwareQuality) {
                    if (quality == settings.quality) {
                        plan.qualityValue = level;
                    }
                }
                break;
            }
        }
    }

    // Maximum file size: average bitrate instead of constant quality (SPEC §5.15). Software does two passes
    // (the second pass respects the size closely); hardware encoders cannot, so they get VBR with a ceiling and
    // the size ends up close to the target (measured on the Radeon 740M and noted in docs/GPU_COMPATIBILITY.md).
    if (const qint64 bitrate = targetVideoBitrate(settings, duration); bitrate > 0) {
        plan.videoBitrate = bitrate;
        plan.videoMaxBitrate = bitrate + bitrate / 2;
        plan.twoPass = !plan.hardware;
    }
    return plan;
}

qint64 estimatedFileSize(const ExportSettings &settings, const RationalTime &duration)
{
    if (settings.maxFileSizeMB > 0) {
        return settings.maxFileSizeMB * 1024 * 1024; // a promise, not an estimate
    }
    const EncoderParameters parameters = encoderParameters(settings);
    // Assumption: constant quality averages ~70% of the cap. Rough; to be calibrated on real footage (PROGRESS).
    const double bitsPerSecond = parameters.videoMaxBitrate * 0.7 * codecSizeFactor(settings.videoCodec) +
                                 parameters.audioBitrate;
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
