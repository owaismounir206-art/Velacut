// SPDX-License-Identifier: GPL-3.0-or-later
#include "SmoothMotion.h"

#include "common/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QUuid>

#include <cmath>

using namespace Qt::StringLiterals;

namespace velacut::engine {

namespace {

// More than this many frames per second would only repeat frames again (and take long to compute).
constexpr int kMaximumRate = 240;
// The range of the copy is rounded outward to this many seconds.
constexpr int kRangeStep = 2;

} // namespace

QString SmoothCopy::path() const
{
    const auto ms = [](const RationalTime &time) { return time.rescaled(Rational(1000), Rounding::NearestEven).value(); };
    return paths::cacheDir() + u"/proxy/"_s + source.fingerprint.value + u"-smooth-%1-%2-%3.mp4"_s.arg(ms(from)).arg(ms(to)).arg(
                                                                               frameRate.toString().replace(u'/', u'_'));
}

bool SmoothCopy::ready() const
{
    return QFileInfo(path()).size() > 0;
}

Media SmoothCopy::asMedia() const
{
    Media copy = source;
    copy.id = *MediaId::fromString(
        QUuid::createUuidV5(QUuid(u"{6c7b0c2e-6f4a-4bfa-9a63-0d3e5b2a9e11}"_s), path()).toString(QUuid::WithoutBraces));
    copy.path = path();
    copy.relativePath.clear();
    copy.fingerprint.value = source.fingerprint.value + u"-smooth"_s;
    copy.info.duration = (to - from).rescaled(Rational(1000), Rounding::NearestEven);
    if (copy.info.video) {
        copy.info.video->frameRate = frameRate;
        copy.info.video->variableFrameRate = false;
        if (copy.info.video->rotation == 90 || copy.info.video->rotation == 270) {
            std::swap(copy.info.video->width, copy.info.video->height); // ffmpeg turns the pictures upright
        }
        copy.info.video->rotation = 0;
    }
    return copy;
}

std::optional<SmoothCopy> smoothCopyFor(const Clip &clip, const Media &media)
{
    const MediaClipData *data = clip.media();
    if (!data || !data->smooth || data->speed >= 1.0 || data->reversed || data->curve || media.kind != MediaKind::Video ||
        !media.info.video || !media.info.video->frameRate || !media.info.duration || !media.fingerprint.isValid()) {
        return std::nullopt;
    }
    const Rational rate = *media.info.video->frameRate;
    // Enough frames for a different one at every frame of a 60 fps output: the source rate times the slow-down.
    const int factor = std::clamp(static_cast<int>(std::ceil(1.0 / data->speed - 1e-9)), 2, 8);
    Rational target = rate * Rational(factor);
    while (target.toDouble() > kMaximumRate && target.toDouble() > rate.toDouble() * 2) {
        target = target - rate;
    }
    const double length = media.info.duration->toSecondsDouble();
    const double first = data->sourceIn.toSecondsDouble();
    const double last = first + clip.duration.toSecondsDouble() * data->speed;
    const auto step = [](double seconds, bool up) {
        return static_cast<std::int64_t>(up ? std::ceil(seconds / kRangeStep) : std::floor(seconds / kRangeStep)) * kRangeStep;
    };
    const std::int64_t from = std::max<std::int64_t>(0, step(first, false));
    const std::int64_t to = std::min<std::int64_t>(step(length, true), step(last, true));
    if (to <= from) {
        return std::nullopt;
    }
    return SmoothCopy{media, RationalTime(from, Rational(1)), RationalTime(to, Rational(1)), target};
}

QString makeSmoothCopy(const SmoothCopy &copy, const std::function<void(double)> &progress, const std::atomic<bool> *cancel)
{
    QDir().mkpath(QFileInfo(copy.path()).absolutePath());
    const QString partial = copy.path() + u".part.mp4"_s;
    const double seconds = (copy.to - copy.from).toSecondsDouble();
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(u"ffmpeg"_s,
                  {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s,
                   u"-ss"_s, QString::number(copy.from.toSecondsDouble(), 'f', 3), u"-to"_s,
                   QString::number(copy.to.toSecondsDouble(), 'f', 3), u"-i"_s, copy.source.path,
                   u"-map"_s, u"0:v:0"_s, u"-map"_s, u"0:a:0?"_s,
                   u"-vf"_s, u"minterpolate=fps=%1:mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1"_s.arg(copy.frameRate.toString()),
                   u"-c:v"_s, u"libx264"_s, u"-preset"_s, u"veryfast"_s, u"-crf"_s, u"14"_s, u"-pix_fmt"_s, u"yuv420p"_s,
                   u"-c:a"_s, u"aac"_s, u"-b:a"_s, u"192k"_s, u"-progress"_s, u"pipe:1"_s, partial});
    if (!process.waitForStarted(10000)) {
        return QObject::tr("The ffmpeg program is needed for smooth slow motion.");
    }
    QByteArray buffer;
    while (!process.waitForFinished(200)) {
        if (cancel && cancel->load()) {
            process.kill();
            process.waitForFinished();
            QFile::remove(partial);
            return QObject::tr("Cancelled.");
        }
        buffer += process.readAllStandardOutput();
        qsizetype end = 0;
        while ((end = buffer.indexOf('\n')) >= 0) {
            const QByteArray line = buffer.left(end).trimmed();
            buffer.remove(0, end + 1);
            if (line.startsWith("out_time_us=") && progress && seconds > 0) {
                progress(std::clamp(line.mid(12).toDouble() / 1e6 / seconds, 0.0, 1.0));
            }
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || QFileInfo(partial).size() == 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        QFile::remove(partial);
        return QObject::tr("The smooth slow motion of %1 could not be made: %2").arg(QFileInfo(copy.source.path).fileName(), detail);
    }
    QFile::remove(copy.path());
    if (!QFile::rename(partial, copy.path())) {
        return QObject::tr("The smooth slow motion could not be saved in the cache.");
    }
    return {};
}

SmoothCopyQueue::SmoothCopyQueue(QObject *parent)
    : QObject(parent)
{
}

SmoothCopyQueue::~SmoothCopyQueue()
{
    m_cancel = true;
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

void SmoothCopyQueue::request(const SmoothCopy &copy)
{
    if (copy.ready() || m_known.contains(copy.path())) {
        return;
    }
    m_known.insert(copy.path());
    m_queue.push_back(copy);
    startNext();
}

void SmoothCopyQueue::startNext()
{
    if (m_thread || m_queue.empty()) {
        return;
    }
    const SmoothCopy copy = m_queue.front();
    m_queue.pop_front();
    m_progress = 0.0;
    m_cancel = false;
    m_thread = QThread::create([this, copy] {
        const QString error = makeSmoothCopy(copy, [this](double share) {
            QMetaObject::invokeMethod(this, [this, share] {
                m_progress = share;
                emit progressChanged();
            }, Qt::QueuedConnection);
        }, &m_cancel);
        QMetaObject::invokeMethod(this, [this, copy, error] {
            m_thread->wait();
            delete m_thread;
            m_thread = nullptr;
            emit busyChanged();
            if (error.isEmpty()) {
                emit ready(copy.source.id);
            } else {
                emit failed(copy.source.id, error);
            }
            startNext();
        }, Qt::QueuedConnection);
    });
    m_thread->start(QThread::LowPriority);
    emit busyChanged();
    emit progressChanged();
}

} // namespace velacut::engine
