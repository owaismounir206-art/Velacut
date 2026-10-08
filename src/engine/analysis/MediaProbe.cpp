// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaProbe.h"

#include "engine/analysis/Fingerprint.h"

#include <QFile>
#include <QFileInfo>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
}

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace velacut::engine {

namespace {

struct FormatContextDeleter
{
    void operator()(AVFormatContext *context) const { avformat_close_input(&context); }
};

ProbeResult failure(ProbeError error, const QString &detail)
{
    ProbeResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

QString averror(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

bool isStillImageCodec(AVCodecID codec)
{
    switch (codec) {
    case AV_CODEC_ID_PNG:
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_WEBP:
    case AV_CODEC_ID_BMP:
    case AV_CODEC_ID_TIFF:
    case AV_CODEC_ID_JPEG2000:
    case AV_CODEC_ID_JPEGXL:
    case AV_CODEC_ID_HEVC: // HEIF stills are demuxed as HEVC items
    case AV_CODEC_ID_AV1:  // AVIF
    case AV_CODEC_ID_GIF:
    case AV_CODEC_ID_SVG:
        return true;
    default:
        return false;
    }
}

// Clockwise rotation in degrees from the display matrix (phones record portrait video as rotated landscape).
int rotationOf(const AVStream *stream)
{
    const AVPacketSideData *side = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                           stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!side || side->size < 9 * static_cast<int>(sizeof(int32_t))) {
        return 0;
    }
    const double counterClockwise = av_display_rotation_get(reinterpret_cast<const int32_t *>(side->data));
    if (std::isnan(counterClockwise)) {
        return 0;
    }
    int degrees = static_cast<int>(std::lround(-counterClockwise)) % 360;
    if (degrees < 0) {
        degrees += 360;
    }
    return degrees;
}

std::optional<Rational> rational(AVRational value)
{
    if (value.num <= 0 || value.den <= 0) {
        return std::nullopt;
    }
    return Rational(value.num, value.den);
}

QString name(const char *text)
{
    return text ? QString::fromLatin1(text) : QString();
}

// Duration of a stream (or of the file) in units of `rate`.
std::optional<std::int64_t> durationIn(const AVFormatContext *context, const AVStream *stream, AVRational rate)
{
    const AVRational unit = av_inv_q(rate);
    if (stream && stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        return av_rescale_q_rnd(stream->duration, stream->time_base, unit, AV_ROUND_NEAR_INF);
    }
    if (context->duration != AV_NOPTS_VALUE && context->duration > 0) {
        return av_rescale_q_rnd(context->duration, AV_TIME_BASE_Q, unit, AV_ROUND_NEAR_INF);
    }
    return std::nullopt;
}

} // namespace

ProbeResult probeMedia(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return failure(ProbeError::Missing, path);
    }
    if (!info.isFile() || !info.isReadable()) {
        return failure(ProbeError::Unreadable, path);
    }
    QString fingerprintError;
    const std::optional<MediaFingerprint> fingerprint = sampledFingerprint(info.absoluteFilePath(), &fingerprintError);
    if (!fingerprint) {
        return failure(ProbeError::Unreadable, fingerprintError);
    }

    AVFormatContext *raw = nullptr;
    const QByteArray encoded = QFile::encodeName(info.absoluteFilePath());
    if (const int code = avformat_open_input(&raw, encoded.constData(), nullptr, nullptr); code < 0) {
        return failure(ProbeError::Unsupported, averror(code));
    }
    std::unique_ptr<AVFormatContext, FormatContextDeleter> context(raw);
    if (const int code = avformat_find_stream_info(context.get(), nullptr); code < 0) {
        return failure(ProbeError::Unsupported, averror(code));
    }

    const AVStream *video = nullptr;
    const AVStream *audio = nullptr;
    for (unsigned i = 0; i < context->nb_streams; ++i) {
        const AVStream *stream = context->streams[i];
        const AVCodecParameters *parameters = stream->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && parameters->width > 0) {
            // Album art of audio files is a one-picture "video" stream: not a video.
            if (!(stream->disposition & AV_DISPOSITION_ATTACHED_PIC) && !video) {
                video = stream;
            }
        } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO && !audio && parameters->sample_rate > 0) {
            audio = stream;
        }
    }
    const QString formatName = name(context->iformat->name);
    const bool imageDemuxer = formatName.startsWith(u"image2"_s) || formatName.endsWith(u"_pipe"_s);

    Media media;
    media.id = MediaId::create();
    media.name = info.fileName();
    media.path = info.absoluteFilePath();
    media.fingerprint = *fingerprint;

    if (video) {
        const AVCodecParameters *parameters = video->codecpar;
        VideoStreamInfo stream;
        stream.width = parameters->width;
        stream.height = parameters->height;
        stream.rotation = rotationOf(video);
        stream.codec = name(avcodec_get_name(parameters->codec_id));
        const auto format = static_cast<AVPixelFormat>(parameters->format);
        stream.pixelFormat = name(av_get_pix_fmt_name(format));
        if (const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(format)) {
            stream.hasAlpha = (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
        }
        if (parameters->color_primaries != AVCOL_PRI_UNSPECIFIED) {
            stream.primaries = name(av_color_primaries_name(parameters->color_primaries));
        }
        if (parameters->color_trc != AVCOL_TRC_UNSPECIFIED) {
            stream.transfer = name(av_color_transfer_name(parameters->color_trc));
        }
        stream.hdr = parameters->color_trc == AVCOL_TRC_SMPTE2084 || parameters->color_trc == AVCOL_TRC_ARIB_STD_B67;
        stream.sampleAspectRatio = rational(parameters->sample_aspect_ratio).value_or(Rational(1));

        const std::optional<std::int64_t> frames = video->nb_frames > 0 ? std::optional<std::int64_t>(video->nb_frames)
                                                                         : std::nullopt;
        const bool still = (imageDemuxer && isStillImageCodec(parameters->codec_id)) ||
                           (isStillImageCodec(parameters->codec_id) && frames.value_or(1) <= 1 &&
                            !durationIn(context.get(), video, AVRational{1, 1000}).value_or(0));
        if (still) {
            media.kind = MediaKind::Image;
        } else {
            media.kind = MediaKind::Video;
            // avg_frame_rate is the measured rate, r_frame_rate the nominal one: different = variable frame rate
            // (phones). Then the nominal rate is the intended one, if plausible (some files report 90000).
            const std::optional<Rational> average = rational(video->avg_frame_rate);
            const std::optional<Rational> base = rational(video->r_frame_rate);
            stream.frameRate = average ? average : base;
            if (average && base) {
                const double ratio = average->toDouble() / base->toDouble();
                stream.variableFrameRate = ratio < 0.99 || ratio > 1.01;
                if (stream.variableFrameRate && base->toDouble() <= 240.0) {
                    stream.frameRate = base;
                }
            }
            if (stream.frameRate) {
                const AVRational rate{static_cast<int>(stream.frameRate->num()), static_cast<int>(stream.frameRate->den())};
                if (const std::optional<std::int64_t> length = durationIn(context.get(), video, rate)) {
                    media.info.duration = RationalTime(*length, *stream.frameRate);
                }
            }
        }
        media.info.video = stream;
    } else if (audio) {
        media.kind = MediaKind::Audio;
    } else {
        return failure(ProbeError::Unsupported, u"no audio or video stream (%1)"_s.arg(formatName));
    }

    if (audio && media.kind != MediaKind::Image) {
        const AVCodecParameters *parameters = audio->codecpar;
        media.info.audio = AudioStreamInfo{name(avcodec_get_name(parameters->codec_id)), parameters->sample_rate,
                                           parameters->ch_layout.nb_channels};
        if (media.kind == MediaKind::Audio) {
            const AVRational rate{parameters->sample_rate, 1};
            if (const std::optional<std::int64_t> samples = durationIn(context.get(), audio, rate)) {
                media.info.duration = RationalTime(*samples, Rational(parameters->sample_rate));
            }
        }
    }
    ProbeResult result;
    result.media = media;
    return result;
}

QString probeErrorCode(ProbeError error)
{
    switch (error) {
    case ProbeError::None:
        return u"none"_s;
    case ProbeError::Missing:
        return u"missing"_s;
    case ProbeError::Unreadable:
        return u"unreadable"_s;
    case ProbeError::Unsupported:
        return u"unsupported"_s;
    case ProbeError::Damaged:
        return u"damaged"_s;
    }
    return {};
}

ProbeError probeErrorFromCode(const QString &code)
{
    for (ProbeError error : {ProbeError::None, ProbeError::Missing, ProbeError::Unreadable, ProbeError::Unsupported}) {
        if (probeErrorCode(error) == code) {
            return error;
        }
    }
    return ProbeError::Damaged;
}

} // namespace velacut::engine
