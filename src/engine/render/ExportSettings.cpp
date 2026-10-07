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
                                  std::pair{VideoCodec::AV1, "av1"}, std::pair{VideoCodec::ProRes, "prores"},
                                  std::pair{VideoCodec::VP9, "vp9"}};

// Format, its name (JSON) and its file suffix.
struct FormatInfo
{
    ExportFormat format;
    const char *name;
    const char *suffix;
    bool video;
    bool audio;
};
constexpr std::array kFormats{
    FormatInfo{ExportFormat::Mp4, "mp4", "mp4", true, true},     FormatInfo{ExportFormat::Mov, "mov", "mov", true, true},
    FormatInfo{ExportFormat::WebM, "webm", "webm", true, true},  FormatInfo{ExportFormat::Gif, "gif", "gif", true, false},
    FormatInfo{ExportFormat::Images, "png", "", true, false},    FormatInfo{ExportFormat::Mp3, "mp3", "mp3", false, true},
    FormatInfo{ExportFormat::Wav, "wav", "wav", false, true},    FormatInfo{ExportFormat::M4a, "m4a", "m4a", false, true},
    FormatInfo{ExportFormat::Flac, "flac", "flac", false, true},
};
const FormatInfo &formatInfo(ExportFormat format)
{
    for (const FormatInfo &info : kFormats) {
        if (info.format == format) {
            return info;
        }
    }
    return kFormats.front();
}

// Bits per pixel of ProRes by profile (Apple's published rates for 1080p30: proxy 45, 422 147, HQ 220 Mbit/s).
constexpr double kProResBitsPerPixel[] = {0.73, 2.36, 3.54};
// Bytes per pixel per frame of the pictures (PNG of filmed video) and of a GIF with its own palette: rough means,
// enough for the space check and the estimate.
constexpr double kPngBytesPerPixel = 1.5;
constexpr double kGifBytesPerPixel = 0.15;

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
                                      std::pair{VideoCodec::AV1, 0.5}, std::pair{VideoCodec::VP9, 0.65}};

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

QString exportFormatName(ExportFormat format)
{
    return QLatin1StringView(formatInfo(format).name);
}

std::optional<ExportFormat> exportFormatFromName(QStringView name)
{
    for (const FormatInfo &info : kFormats) {
        if (name == QLatin1StringView(info.name)) {
            return info.format;
        }
    }
    return std::nullopt;
}

QString exportSuffix(ExportFormat format)
{
    return QLatin1StringView(formatInfo(format).suffix);
}

bool hasVideo(ExportFormat format)
{
    return formatInfo(format).video;
}

bool hasAudio(ExportFormat format)
{
    return formatInfo(format).audio;
}

VideoCodec codecFor(ExportFormat format, VideoCodec chosen)
{
    switch (format) {
    case ExportFormat::Mp4:
        return chosen == VideoCodec::HEVC || chosen == VideoCodec::AV1 ? chosen : VideoCodec::H264;
    case ExportFormat::Mov:
        return chosen == VideoCodec::H264 || chosen == VideoCodec::HEVC ? chosen : VideoCodec::ProRes;
    case ExportFormat::WebM:
        return chosen == VideoCodec::AV1 ? chosen : VideoCodec::VP9;
    case ExportFormat::Gif:
    case ExportFormat::Images:
    case ExportFormat::Mp3:
    case ExportFormat::Wav:
    case ExportFormat::M4a:
    case ExportFormat::Flac:
        break;
    }
    return VideoCodec::H264;
}

RationalTime exportedDuration(const ExportSettings &settings, const RationalTime &sequenceDuration)
{
    if (settings.range.isEmpty()) {
        return sequenceDuration;
    }
    const Rational rate = sequenceDuration.rate();
    const RationalTime start = settings.range.start.rescaled(rate, Rounding::NearestEven);
    const RationalTime end = std::min(settings.range.end().rescaled(rate, Rounding::NearestEven), sequenceDuration);
    return end > start ? end - start : RationalTime(0, rate);
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
    QJsonObject json{{u"output"_s, outputPath},
                       {u"format"_s, exportFormatName(format)},
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
    if (!range.isEmpty()) {
        json.insert(u"rangeStart"_s, range.start.toString());
        json.insert(u"rangeDuration"_s, range.duration.toString());
    }
    return json;
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
    if (const auto format = exportFormatFromName(json.value(u"format"_s).toString())) {
        settings.format = *format;
    }
    const auto rangeStart = RationalTime::fromString(json.value(u"rangeStart"_s).toString());
    const auto rangeDuration = RationalTime::fromString(json.value(u"rangeDuration"_s).toString());
    if (rangeStart && rangeDuration && rangeStart->hasSameRate(*rangeDuration) && !rangeStart->isNegative()
        && !rangeDuration->isNegative()) {
        settings.range = TimeRange(*rangeStart, *rangeDuration);
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

qint64 targetVideoBitrate(const ExportSettings &settings, const RationalTime &duration)
{
    const VideoCodec codec = codecFor(settings.format, settings.videoCodec);
    const bool sized = settings.format == ExportFormat::Mp4 || settings.format == ExportFormat::WebM
                       || (settings.format == ExportFormat::Mov && codec != VideoCodec::ProRes);
    if (settings.maxFileSizeMB <= 0 || duration.value() <= 0 || !sized) {
        return 0;
    }
    // What is left for video once the container and the audio took their share.
    const qint64 totalBits = settings.maxFileSizeMB * 1024 * 1024 * 8;
    const qint64 audioBits = std::llround(encoderParameters(settings).audioBitrate * duration.toSecondsDouble());
    const double containerShare = 0.03; // index, cover, subtitles, MP4 overhead
    qint64 videoBits = static_cast<qint64>(totalBits * (1.0 - containerShare)) - audioBits;
    // The newer codecs need fewer bits; do not promise a size that lets the encoder waste quality.
    videoBits = std::llround(videoBits * codecSizeFactor(codecFor(settings.format, settings.videoCodec)));
    // Bits over the whole video → average bit rate.
    return std::max<qint64>(200000, std::llround(videoBits / duration.toSecondsDouble()));
}

EncoderPlan planEncoder(const ExportSettings &settings, const QStringList &availableHardwareEncoders,
                        const RationalTime &duration)
{
    const EncoderParameters parameters = encoderParameters(settings);
    const VideoCodec codec = codecFor(settings.format, settings.videoCodec);

    EncoderPlan plan;
    plan.audioBitrate = parameters.audioBitrate;
    plan.videoMaxBitrate = parameters.videoMaxBitrate;
    plan.qualityOption = "crf";
    plan.qualityValue = parameters.crf;
    plan.pixelFormat = "yuv420p";
    plan.acodec = settings.format == ExportFormat::WebM ? "libopus" : "aac";

    switch (settings.format) {
    case ExportFormat::Mp3:
    case ExportFormat::Wav:
    case ExportFormat::M4a:
    case ExportFormat::Flac:
        // The sound alone.
        plan.vcodec.clear();
        plan.pixelFormat.clear();
        plan.qualityOption.clear();
        plan.videoMaxBitrate = 0;
        plan.acodec = settings.format == ExportFormat::Mp3 ? "libmp3lame"
                    : settings.format == ExportFormat::Wav ? "pcm_s16le"
                    : settings.format == ExportFormat::Flac ? "flac" : "aac";
        return plan;
    case ExportFormat::Images:
        plan.vcodec = "png";
        plan.pixelFormat = "rgb24";
        plan.qualityOption.clear();
        plan.videoMaxBitrate = 0;
        plan.acodec.clear();
        return plan;
    case ExportFormat::Gif:
        // A nearly lossless video first; the GIF and its palette are made from it (Renderer).
        plan.vcodec = "libx264";
        plan.qualityValue = 10;
        plan.preset = "ultrafast";
        plan.videoMaxBitrate = 0;
        plan.acodec.clear();
        return plan;
    case ExportFormat::Mp4:
    case ExportFormat::Mov:
    case ExportFormat::WebM:
        break;
    }

    switch (codec) {
    case VideoCodec::H264:
        plan.vcodec = "libx264";
        plan.preset = parameters.preset;
        break;
    case VideoCodec::HEVC:
        plan.vcodec = "libx265";
        plan.preset = parameters.preset;
        break;
    case VideoCodec::AV1:
        plan.vcodec = "libsvtav1"; // its preset is a number, not a name: the default
        break;
    case VideoCodec::VP9:
        // Constant quality needs the bitrate at 0; rows in parallel and the faster "good" speed.
        plan.vcodec = "libvpx-vp9";
        plan.qualityValue = settings.quality == ExportQuality::Low ? 40 : settings.quality == ExportQuality::High ? 24 : 32;
        plan.options = {{"vb", "0"}, {"row-mt", "1"}, {"deadline", "good"}, {"cpu-used", "4"}};
        plan.videoMaxBitrate = 0;
        break;
    case VideoCodec::ProRes:
        // For editing in another program: proxy, 422 or 422 HQ, 10 bits, the sound uncompressed.
        plan.vcodec = "prores_ks";
        plan.qualityOption = "profile";
        plan.qualityValue = settings.quality == ExportQuality::Low ? 0 : settings.quality == ExportQuality::High ? 3 : 2;
        plan.pixelFormat = "yuv422p10le";
        plan.videoMaxBitrate = 0;
        plan.acodec = "pcm_s16le";
        return plan;
    }

    // Hardware: only an encoder the probe verified on this very machine (SPEC 1bis rule 1: nothing assumed).
    if (settings.hardwareEncoder == HardwareEncoder::Auto) {
        for (const char *suffix : kHardwareSuffixes) {
            const QString candidate = u"%1_%2"_s.arg(videoCodecName(codec)).arg(QLatin1StringView(suffix));
            if (availableHardwareEncoders.contains(candidate)) {
                plan.vcodec = candidate.toLatin1();
                plan.hardware = true;
                plan.qualityOption = "quality";
                plan.preset.clear();
                plan.options.clear();
                plan.pixelFormat.clear();
                if (plan.videoMaxBitrate == 0) {
                    plan.videoMaxBitrate = parameters.videoMaxBitrate;
                }
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
        std::erase_if(plan.options, [](const auto &option) { return option.first == "vb"; });
    }
    return plan;
}

qint64 estimatedFileSize(const ExportSettings &settings, const RationalTime &duration)
{
    const double seconds = duration.toSecondsDouble();
    const double pixels = static_cast<double>(settings.size.width()) * settings.size.height();
    const double frames = seconds * settings.frameRate.toDouble();
    const EncoderParameters parameters = encoderParameters(settings);
    switch (settings.format) {
    case ExportFormat::Mp3:
    case ExportFormat::M4a:
        return std::llround(parameters.audioBitrate * seconds / 8.0) + 16 * 1024;
    case ExportFormat::Wav:
        return std::llround(48000.0 * 2 * 2 * seconds) + 1024; // 16 bits, stereo, 48 kHz
    case ExportFormat::Flac:
        return std::llround(48000.0 * 2 * 2 * seconds * 0.6) + 16 * 1024;
    case ExportFormat::Images:
        return std::llround(pixels * frames * kPngBytesPerPixel);
    case ExportFormat::Gif:
        return std::llround(pixels * frames * kGifBytesPerPixel) + 1024;
    case ExportFormat::Mp4:
    case ExportFormat::Mov:
    case ExportFormat::WebM:
        break;
    }
    const VideoCodec codec = codecFor(settings.format, settings.videoCodec);
    if (codec == VideoCodec::ProRes) {
        const int profile = settings.quality == ExportQuality::Low ? 0 : settings.quality == ExportQuality::High ? 2 : 1;
        return std::llround((pixels * settings.frameRate.toDouble() * kProResBitsPerPixel[profile] + 48000.0 * 32) * seconds / 8.0)
               + 64 * 1024;
    }
    if (settings.maxFileSizeMB > 0 && targetVideoBitrate(settings, duration) > 0) {
        return settings.maxFileSizeMB * 1024 * 1024; // a promise, not an estimate
    }
    // Assumption: constant quality averages ~70% of the cap. Rough; to be calibrated on real footage (PROGRESS).
    const double bitsPerSecond = parameters.videoMaxBitrate * 0.7 * codecSizeFactor(codec) + parameters.audioBitrate;
    return std::llround(bitsPerSecond * seconds / 8.0) + 64 * 1024;
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
