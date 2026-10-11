// SPDX-License-Identifier: GPL-3.0-or-later
// Preview benchmark (docs/PERFORMANCE.md): the real MLT graph of six typical timelines, measured in two ways.
//
//  * pipeline: TimelineProjection + direct get_frame/get_image on this thread, like the consumer's single render
//    thread (real_time = 1): ms per frame in sequential playback (mean, p95), latency of a seek to a random frame,
//    latency of "change an effect parameter, show the same frame again" (projection update + render).
//  * player: the TimelinePlayer of the app with its sdl2_audio consumer (SDL dummy audio, which keeps real time):
//    frames shown per second while playing and frames dropped, a simulated scrub drag (60 events per second) and a
//    simulated effect slider drag (60 commands per second, merged like the inspector's).
//
// Media are generated with ffmpeg in --media-dir (default: ./bench-media) the first time: 10 s, 30 fps, H.264 with
// a 2 s GOP, B-frames and grain (so that decoding costs what a camera file costs, not what a flat test pattern does).
//
//   preview_bench [--size 1080|2160|all] [--scenario S1..S6|all] [--cpu-transitions] [--no-player] [--media-dir DIR]
#include "core/commands/EditCommand.h"
#include "core/edit/TimelineEditor.h"
#include "core/project/Project.h"
#include "engine/analysis/MediaProbe.h"
#include "engine/analysis/Proxy.h"
#include "engine/render/Renderer.h"
#include "engine/gpu/GpuTransitions.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/playback/TimelinePlayer.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/timeline/TimelineProjection.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUndoStack>

#include <mlt++/Mlt.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <numeric>
#include <random>

using namespace velacut;
using namespace velacut::engine;
using namespace Qt::StringLiterals;

namespace {

QTextStream out(stdout);

struct Stats
{
    double mean = 0;
    double p95 = 0;
    double max = 0;
};

Stats statsOf(std::vector<double> values)
{
    Stats s;
    if (values.empty()) {
        return s;
    }
    std::sort(values.begin(), values.end());
    s.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    s.p95 = values[std::min(values.size() - 1, static_cast<size_t>(std::ceil(0.95 * static_cast<double>(values.size()))) - 1)];
    s.max = values.back();
    return s;
}

QString ms(double value)
{
    return QString::number(value, 'f', value < 10 ? 1 : 0);
}

void waitMs(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    }
    return true;
}

bool generate(const QString &path, int width, int height, const QString &pattern, int frequency)
{
    if (QFileInfo::exists(path)) {
        return true;
    }
    const QString ffmpeg = QStandardPaths::findExecutable(u"ffmpeg"_s);
    if (ffmpeg.isEmpty()) {
        return false;
    }
    out << "generating " << path << "…\n";
    out.flush();
    const QStringList arguments{u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s,
                                u"%1=size=%2x%3:rate=30"_s.arg(pattern).arg(width).arg(height), u"-f"_s, u"lavfi"_s, u"-i"_s,
                                u"sine=frequency=%1:sample_rate=48000"_s.arg(frequency), u"-t"_s, u"10"_s, u"-vf"_s,
                                u"noise=alls=10:allf=t"_s, u"-c:v"_s, u"libx264"_s, u"-preset"_s, u"veryfast"_s, u"-crf"_s,
                                u"20"_s, u"-g"_s, u"60"_s, u"-bf"_s, u"2"_s, u"-pix_fmt"_s, u"yuv420p"_s, u"-c:a"_s, u"aac"_s,
                                u"-shortest"_s, path};
    return QProcess::execute(ffmpeg, arguments) == 0;
}

// ---- scenarios ----------------------------------------------------------------------------------------------------

struct Scenario
{
    QString name;
    QString description;
    ProjectData data;
    int from = 0;  // measured range [from, to)
    int to = 0;
    ClipId effectClip; // the clip whose first effect's "amount" is changed (null: no parameter test)
    EffectId effectId;
    int proxyHeight = 0; // the preview reads proxies of this height (S7p)
};

Effect libraryEffect(const QString &type, const QString &preset, double amount)
{
    Effect effect;
    effect.id = EffectId::create();
    effect.type = type;
    effect.preset = AssetRef{u"vedit.core"_s, preset, 1};
    if (amount >= 0) {
        effect.params[u"amount"_s] = Param(amount);
    }
    return effect;
}

class Builder
{
public:
    explicit Builder(ProjectData data)
        : m_project(std::move(data))
    {
    }
    bool apply(EditResult result)
    {
        if (!result.ok()) {
            out << "edit failed: " << result.error << '\n';
            return false;
        }
        m_stack.push(new EditCommand(m_project, result.text, std::move(result.script)));
        return true;
    }
    TimelineEditor editor() const { return TimelineEditor(m_project.data(), m_project.data().mainSequenceId); }
    const Track &mainTrack() const { return m_project.data().mainSequence()->visualTracks.front(); }
    const ProjectData &data() const { return m_project.data(); }

private:
    Project m_project;
    QUndoStack m_stack;
};

RationalTime frames(int n)
{
    return RationalTime(n, Rational(30));
}

std::optional<Scenario> makeScenario(const QString &name, const Media &a, const Media &b, int height)
{
    ProjectData data = ProjectData::createEmpty(name);
    data.settings.frameRate = Rational(30);
    const int width = height * 16 / 9;
    data.settings.defaultCanvas = Canvas{width, height, CanvasPreset::Landscape16x9};
    data.sequences.front().canvas = data.settings.defaultCanvas;
    data.media = {a, b};
    Builder builder(data);
    Scenario s;
    s.name = name;
    s.from = 60;
    s.to = 120;
    if (!builder.apply(builder.editor().insertMedia(a.id, frames(0)))) {
        return std::nullopt;
    }
    const ClipId first = builder.mainTrack().clips.front().id;
    const auto addEffect = [&](const Effect &effect) {
        return builder.apply(builder.editor().updateClips({first}, [&](Clip &c) { c.effects.push_back(effect); }, u"effect"_s));
    };
    if (name == u"S1") {
        s.description = u"1 clip H.264, no effect"_s;
    } else if (name == u"S2" || name == u"S6") {
        const Effect blur = libraryEffect(u"vedit.effect"_s, u"effects/blur-strong"_s, 1.0);
        if (!addEffect(blur)) {
            return std::nullopt;
        }
        s.effectClip = first;
        s.effectId = blur.id;
        s.description = u"1 clip + blur 1.0"_s;
        if (name == u"S6") {
            TextClipData text;
            text.text = u"Velacut benchmark"_s;
            if (!builder.apply(builder.editor().insertText(frames(0), text, frames(300))) ||
                !addEffect(libraryEffect(u"vedit.filter"_s, u"filters/vivid"_s, -1))) {
                return std::nullopt;
            }
            s.description = u"blur 1.0 + text overlay + colour filter"_s;
        }
    } else if (name == u"S3") {
        const Effect zoom = libraryEffect(u"vedit.effect"_s, u"effects/zoom-blur"_s, 1.0);
        if (!addEffect(zoom)) {
            return std::nullopt;
        }
        s.effectClip = first;
        s.effectId = zoom.id;
        s.description = u"1 clip + zoom blur 1.0"_s;
    } else if (name == u"S4") {
        if (!builder.apply(builder.editor().splitClip(first, frames(150))) ||
            !builder.apply(builder.editor().addTransition(builder.mainTrack().clips.front().id,
                                                          AssetRef{u"vedit.core"_s, u"transitions/dissolve"_s, 1}, frames(30)))) {
            return std::nullopt;
        }
        s.from = 135;
        s.to = 165;
        s.description = u"split clip (same file) + 1 s dissolve"_s;
    } else if (name == u"S4b") {
        // Same file, parts far apart (a split, the middle deleted): A reads source 105–134, B source 165–194.
        if (!builder.apply(builder.editor().splitClip(first, frames(120))) ||
            !builder.apply(builder.editor().splitClip(builder.mainTrack().clips.back().id, frames(180))) ||
            !builder.apply(builder.editor().rippleDeleteClips({builder.mainTrack().clips.at(1).id})) ||
            !builder.apply(builder.editor().addTransition(builder.mainTrack().clips.front().id,
                                                          AssetRef{u"vedit.core"_s, u"transitions/dissolve"_s, 1}, frames(30)))) {
            return std::nullopt;
        }
        s.from = 105;
        s.to = 135;
        s.description = u"same file, distant parts + 1 s dissolve"_s;
    } else if (name == u"S7" || name == u"S7p") {
        // `a` is the 4K HEVC 10-bit file here (see main).
        s.description = name == u"S7" ? u"4K HEVC 10 bit, no proxy"_s : u"4K HEVC 10 bit, 720p proxy"_s;
        s.proxyHeight = name == u"S7p" ? 720 : 0;
    } else if (name == u"S5") {
        if (!builder.apply(builder.editor().insertMedia(b.id, frames(300))) ||
            !builder.apply(builder.editor().addTransition(first, AssetRef{u"vedit.core"_s, u"transitions/blur-dissolve"_s, 1},
                                                          frames(30)))) {
            return std::nullopt;
        }
        s.from = 285;
        s.to = 315;
        s.description = u"2 files + 1 s blur dissolve"_s;
    } else {
        return std::nullopt;
    }
    s.data = builder.data();
    return s;
}

// ---- pipeline measurements -----------------------------------------------------------------------------------------

struct PipelineResult
{
    Stats sequential;
    double seekMean = 0;
    Stats param;
    bool hasParam = false;
    QSize frameSize;
};

double renderOnce(Mlt::Tractor &tractor, int position, int width, int height, QSize *size = nullptr)
{
    QElapsedTimer timer;
    timer.start();
    tractor.seek(position);
    std::unique_ptr<Mlt::Frame> frame(tractor.get_frame());
    mlt_image_format format = mlt_image_rgba;
    int w = width;
    int h = height;
    const uint8_t *image = frame ? frame->get_image(format, w, h) : nullptr;
    if (!image) {
        out << "no image at " << position << '\n';
    }
    if (size) {
        *size = QSize(w, h);
    }
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

// --preview-height: frames requested at this height (the width follows), as the preview asks for them (Phase 3).
int g_previewHeight = 0;
// --view WxH: the player's preview area (device pixels); quality Auto.
QSize g_viewSize;

PipelineResult measurePipeline(const Scenario &scenario)
{
    PipelineResult result;
    Project project(scenario.data);
    QUndoStack stack;
    auto profile = makeProfile(project.data(), project.data().mainSequenceId);
    MediaProducerCache cache(*profile);
    cache.setProxyHeight(scenario.proxyHeight);
    TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
    projection.build(project.data(), project.data().mainSequenceId);
    Mlt::Tractor &tractor = *projection.tractor();
    int w = profile->width();
    int h = profile->height();
    if (g_previewHeight > 0 && g_previewHeight < h) {
        w = std::max(2, static_cast<int>(std::lround(w * double(g_previewHeight) / h / 2.0)) * 2);
        h = g_previewHeight;
    }

    // Warm-up (opens decoders, fills caches), then sequential playback over the range.
    renderOnce(tractor, scenario.from, w, h);
    std::vector<double> times;
    for (int p = scenario.from; p < scenario.to; ++p) {
        times.push_back(renderOnce(tractor, p, w, h, &result.frameSize));
    }
    result.sequential = statsOf(times);

    // Seeks to random frames (fixed seed: same positions every run).
    std::mt19937 random(42);
    std::uniform_int_distribution<int> anywhere(0, projection.duration() - 1);
    std::vector<double> seeks;
    for (int i = 0; i < 6; ++i) {
        seeks.push_back(renderOnce(tractor, anywhere(random), w, h));
    }
    result.seekMean = statsOf(seeks).mean;

    // Same frame again after an effect parameter change: projection update + render.
    if (!scenario.effectClip.isNull()) {
        result.hasParam = true;
        ChangeSet lastChanges;
        QObject::connect(&project, &Project::changed, [&](const ChangeSet &changes) { lastChanges = changes; });
        const int at = (scenario.from + scenario.to) / 2;
        renderOnce(tractor, at, w, h);
        std::vector<double> params;
        for (int i = 0; i < 6; ++i) {
            const double amount = i % 2 ? 1.0 : 0.9;
            TimelineEditor editor(project.data(), project.data().mainSequenceId);
            EditResult edit = editor.updateClips({scenario.effectClip}, [&](Clip &c) {
                for (Effect &e : c.effects) {
                    if (e.id == scenario.effectId) {
                        e.params[u"amount"_s] = Param(amount);
                    }
                }
            }, u"param"_s);
            QElapsedTimer timer;
            timer.start();
            stack.push(new EditCommand(project, edit.text, std::move(edit.script)));
            projection.update(project.data(), lastChanges);
            const double update = static_cast<double>(timer.nsecsElapsed()) / 1e6;
            params.push_back(update + renderOnce(tractor, at, w, h));
        }
        result.param = statsOf(params);
    }
    projection.releaseRetired(std::chrono::milliseconds(0));
    return result;
}

// ---- player measurements -------------------------------------------------------------------------------------------

struct PlayerResult
{
    double fps = 0;
    double dropped = 0; // fraction of the frames played that were not shown
    double scrubMaxGap = 0;
    double scrubSettle = -1;
    int sliderFrames = 0;
    double sliderRate = 0;
    double sliderSettle = -1;
    bool hasSlider = false;
};

PlayerResult measurePlayer(const Scenario &scenario)
{
    PlayerResult result;
    Project project(scenario.data);
    QUndoStack stack;
    TimelinePlayer player;
    player.setProxyHeight(scenario.proxyHeight); // no proxy made in background during the other scenarios
    if (g_viewSize.isValid()) {
        player.setViewSize(g_viewSize);
    }
    player.setSequence(&project, project.data().mainSequenceId);
    FrameSink *sink = player.sink();
    QElapsedTimer clock;
    clock.start();
    std::vector<std::pair<qint64, int>> shown; // (ms, position) of every frame delivered to the sink
    QObject::connect(sink, &FrameSink::frameReady, [&] {
        int position = -1;
        sink->latest(nullptr, &position);
        shown.emplace_back(clock.elapsed(), position);
    });
    // Media open in background: wait for the producers, then for a frame of the start.
    waitMs(1500);
    player.seek(scenario.from);
    if (!waitFor([&] { return player.shownPosition() == scenario.from; }, 5000)) {
        out << "player: no frame at " << scenario.from << '\n';
    }
    waitMs(300);

    // Playback over the range (S4/S5: the 1 s window, S1-S3/S6: 2 s).
    const quint64 before = sink->framesReceived();
    const int start = player.position();
    const qint64 t0 = clock.elapsed();
    player.play();
    const int played = scenario.to - scenario.from;
    waitFor([&] { return player.position() >= scenario.to - 1 || clock.elapsed() - t0 > 8000; }, 9000);
    const double seconds = static_cast<double>(clock.elapsed() - t0) / 1000.0;
    player.pause();
    const quint64 received = sink->framesReceived() - before;
    const int advanced = std::max(1, player.position() - start);
    result.fps = static_cast<double>(received) / seconds;
    result.dropped = std::max(0.0, 1.0 - static_cast<double>(received) / std::max(advanced, played));
    waitMs(500);

    // Scrub drag: 60 events per second across the range (back and forth), then release on the last position.
    player.setScrubMuted(true);
    shown.clear();
    const qint64 dragStart = clock.elapsed();
    const int span = scenario.to - scenario.from;
    int target = scenario.from;
    for (int i = 0; i < 90; ++i) { // 1.5 s
        const double t = i / 89.0;
        target = scenario.from + static_cast<int>(std::lround((t < 0.5 ? t * 2 : 2 - t * 2) * (span - 1) * 0.8 + span * 0.1));
        player.scrubSeek(target);
        waitMs(16);
    }
    const qint64 release = clock.elapsed();
    player.commitSeek(target);
    waitFor([&] { return !shown.empty() && shown.back().second == target && shown.back().first >= release; }, 5000);
    const bool settled = !shown.empty() && shown.back().second == target;
    result.scrubSettle = settled ? static_cast<double>(shown.back().first - release) : -1;
    qint64 last = dragStart;
    for (const auto &[time, position] : shown) {
        if (time > release) {
            break;
        }
        result.scrubMaxGap = std::max(result.scrubMaxGap, static_cast<double>(time - last));
        last = time;
    }
    result.scrubMaxGap = std::max(result.scrubMaxGap, static_cast<double>(release - last));
    waitMs(500);

    // Effect slider drag on a still frame: 60 merged commands per second for 1.5 s.
    if (!scenario.effectClip.isNull()) {
        result.hasSlider = true;
        shown.clear();
        const quint64 sliderBefore = sink->framesReceived();
        const qint64 sliderStart = clock.elapsed();
        for (int i = 0; i < 90; ++i) {
            TimelineEditor editor(project.data(), project.data().mainSequenceId);
            const double amount = 1.0 - 0.5 * i / 89.0;
            EditResult edit = editor.updateClips({scenario.effectClip}, [&](Clip &c) {
                for (Effect &e : c.effects) {
                    if (e.id == scenario.effectId) {
                        e.params[u"amount"_s] = Param(amount);
                    }
                }
            }, u"param"_s);
            stack.push(new EditCommand(project, edit.text, std::move(edit.script), MergeKey{u"bench"_s, 1}));
            waitMs(16);
        }
        const qint64 lastChange = clock.elapsed();
        result.sliderFrames = static_cast<int>(sink->framesReceived() - sliderBefore);
        result.sliderRate = result.sliderFrames / (static_cast<double>(lastChange - sliderStart) / 1000.0);
        // Settled: the last frame delivered after the last change, once nothing arrives for 1 s.
        size_t count = shown.size();
        QElapsedTimer quiet;
        quiet.start();
        while (quiet.elapsed() < 1000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            if (shown.size() != count) {
                count = shown.size();
                quiet.restart();
            }
        }
        if (!shown.empty() && shown.back().first > lastChange) {
            result.sliderSettle = static_cast<double>(shown.back().first - lastChange);
        }
    }
    player.close();
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    if (qEnvironmentVariableIsEmpty("SDL_AUDIODRIVER")) {
        qputenv("SDL_AUDIODRIVER", "dummy");
    }
    QGuiApplication app(argc, argv);
    QStringList arguments = app.arguments();
    const auto option = [&](const QString &name, const QString &fallback) {
        const qsizetype index = arguments.indexOf(name);
        return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : fallback;
    };
    const QString sizeOption = option(u"--size"_s, u"all"_s);
    const QString scenarioOption = option(u"--scenario"_s, u"all"_s);
    const QString mediaDir = QDir(option(u"--media-dir"_s, u"bench-media"_s)).absolutePath();
    const bool cpuTransitions = arguments.contains(u"--cpu-transitions"_s);
    const bool withPlayer = !arguments.contains(u"--no-player"_s);
    g_previewHeight = option(u"--preview-height"_s, u"0"_s).toInt();
    if (const QStringList view = option(u"--view"_s, QString()).split(u'x'); view.size() == 2) {
        g_viewSize = QSize(view[0].toInt(), view[1].toInt());
    }

    if (!MltRuntime::waitUntilReady()) {
        out << "MLT could not be initialized\n";
        return 1;
    }
    if (!cpuTransitions) {
        GpuTransitions::initialize(); // as the app does when GPU effects are allowed
    }
    QDir().mkpath(mediaDir);

    QList<int> heights;
    if (sizeOption == u"all") {
        heights = {1080, 2160};
    } else {
        heights = {sizeOption.toInt()};
    }
    QStringList names{u"S1"_s, u"S2"_s, u"S3"_s, u"S4"_s, u"S4b"_s, u"S5"_s, u"S6"_s, u"S7"_s, u"S7p"_s};
    if (scenarioOption != u"all") {
        names = scenarioOption.split(u',');
    }

    out << "| Scenario | Canvas | Frame | Seq. mean ms | Seq. p95 ms | Seek ms | Param ms (mean/p95) | Play fps | Dropped | "
           "Scrub max gap ms | Scrub settle ms | Slider frames/s | Slider settle ms |\n";
    out << "|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    out.flush();
    for (const int height : heights) {
        const int width = height * 16 / 9;
        const QString a = mediaDir + u"/a_%1p.mp4"_s.arg(height);
        const QString b = mediaDir + u"/b_%1p.mp4"_s.arg(height);
        if (!generate(a, width, height, u"testsrc2"_s, 440) || !generate(b, width, height, u"testsrc"_s, 660)) {
            out << "ffmpeg is needed to generate the media\n";
            return 1;
        }
        const ProbeResult probeA = probeMedia(a);
        const ProbeResult probeB = probeMedia(b);
        if (!probeA.media || !probeB.media) {
            out << "probe failed\n";
            return 1;
        }
        for (const QString &name : names) {
            const bool hevc = name.startsWith(u"S7"_s);
            if (hevc && height != 2160) {
                continue; // a 4K project only
            }
            Media first = *probeA.media;
            if (hevc) {
                const QString file = mediaDir + u"/hevc10_2160p.mp4"_s;
                if (!QFileInfo::exists(file)) {
                    out << "generating " << file << "…\n";
                    out.flush();
                    QProcess::execute(QStandardPaths::findExecutable(u"ffmpeg"_s),
                                      {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s,
                                       u"testsrc2=size=3840x2160:rate=30"_s, u"-f"_s, u"lavfi"_s, u"-i"_s,
                                       u"sine=frequency=440:sample_rate=48000"_s, u"-t"_s, u"10"_s, u"-vf"_s, u"noise=alls=10:allf=t"_s,
                                       u"-c:v"_s, u"libx265"_s, u"-preset"_s, u"ultrafast"_s, u"-x265-params"_s, u"log-level=error"_s,
                                       u"-pix_fmt"_s, u"yuv420p10le"_s, u"-c:a"_s, u"aac"_s, u"-shortest"_s, file});
                }
                const ProbeResult probe = probeMedia(file);
                if (!probe.media) {
                    out << "HEVC media missing\n";
                    continue;
                }
                first = *probe.media;
                if (name == u"S7p" && !QFileInfo::exists(proxyPath(first, 720))) {
                    out << "making the proxy…\n";
                    out.flush();
                    const std::atomic<bool> cancel{false};
                    Renderer::renderProxy(file, proxyPath(first, 720), 720, {}, {}, cancel);
                }
            }
            const std::optional<Scenario> scenario = makeScenario(name, first, *probeB.media, height);
            if (!scenario) {
                out << "scenario " << name << " could not be built\n";
                continue;
            }
            const PipelineResult pipeline = measurePipeline(*scenario);
            PlayerResult player;
            if (withPlayer) {
                player = measurePlayer(*scenario);
            }
            out << "| " << name << " " << scenario->description << " | " << width << "×" << height << " | "
                << pipeline.frameSize.width() << "×" << pipeline.frameSize.height() << " | " << ms(pipeline.sequential.mean)
                << " | " << ms(pipeline.sequential.p95) << " | " << ms(pipeline.seekMean) << " | "
                << (pipeline.hasParam ? ms(pipeline.param.mean) + u" / "_s + ms(pipeline.param.p95) : u"–"_s) << " | ";
            if (withPlayer) {
                out << ms(player.fps) << " | " << QString::number(player.dropped * 100, 'f', 0) << " % | "
                    << ms(player.scrubMaxGap) << " | " << (player.scrubSettle < 0 ? u"never"_s : ms(player.scrubSettle)) << " | "
                    << (player.hasSlider ? ms(player.sliderRate) : u"–"_s) << " | "
                    << (player.hasSlider ? (player.sliderSettle < 0 ? u"none"_s : ms(player.sliderSettle)) : u"–"_s) << " |\n";
            } else {
                out << "– | – | – | – | – | – |\n";
            }
            out.flush();
        }
    }
    GpuTransitions::shutdown();
    MltRuntime::shutdown();
    return 0;
}
