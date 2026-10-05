// SPDX-License-Identifier: GPL-3.0-or-later
// vedit-render: exports a frozen copy of a project in a separate process (docs/ARCHITECTURE.md §5.4, D-07/D-08).
//
//   vedit-render --job job.json
//   job.json: {"project": "<frozen .vproj>", "sequence": "<sequence id>", "settings": ExportSettings}
//
// Writes one JSON object per line on stdout:
//   {"event":"progress","frame":N,"total":T}   {"event":"warning","message":"…"}
//   {"event":"done","output":"…"}   {"event":"error","code":"…","detail":"…"}   {"event":"cancelled"}
// Exit code 0 = done, 1 = error, 2 = cancelled. SIGTERM/SIGINT cancel cleanly (no partial file is left).
//
//   vedit-render --backwards file --output copy.mp4
// The file played backwards for the preview of reversed clips; progress lines as for an export.
//
//   vedit-render --probe file…
// Reads the metadata of media files for the import (D-07: a file that crashes the demuxer is rejected instead of
// crashing the editor). For each file: {"event":"probing","path":"…"} then {"event":"media","path":"…","media":{…}}
// or {"event":"media-error","path":"…","code":"…","detail":"…"}.
#include "core/serialization/ProjectFile.h"
#include "engine/analysis/MediaProbe.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/render/Renderer.h"

#include <QCommandLineParser>
#include <QGuiApplication>

#include "engine/gpu/GpuTransitions.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <csignal>
#include <cstdio>

using namespace vedit;
using namespace vedit::engine;
using namespace Qt::StringLiterals;

namespace {

std::atomic<bool> s_cancel{false};
static_assert(std::atomic<bool>::is_always_lock_free);

extern "C" void onTerminate(int)
{
    s_cancel = true;
}

void emitEvent(const QJsonObject &event)
{
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
}

int fail(RenderError error, const QString &detail)
{
    emitEvent({{u"event"_s, u"error"_s}, {u"code"_s, renderErrorCode(error)}, {u"detail"_s, detail}});
    return 1;
}

int run(const QString &jobPath)
{
    QFile file(jobPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(RenderError::ProjectUnreadable, file.errorString());
    }
    const QJsonObject job = QJsonDocument::fromJson(file.readAll()).object();
    const std::optional<ExportSettings> settings = ExportSettings::fromJson(job.value(u"settings"_s).toObject());
    if (!settings) {
        return fail(RenderError::ProjectUnreadable, u"invalid job settings"_s);
    }
    const ProjectLoadResult loaded = projectfile::load(job.value(u"project"_s).toString());
    if (!loaded.ok()) {
        return fail(RenderError::ProjectUnreadable, loaded.error);
    }
    const std::optional<SequenceId> sequenceId = SequenceId::fromString(job.value(u"sequence"_s).toString());
    if (!sequenceId) {
        return fail(RenderError::SequenceMissing, job.value(u"sequence"_s).toString());
    }
    QStringList hardwareEncoders;
    for (const QJsonValue &encoder : job.value(u"encoders"_s).toObject().value(u"video"_s).toArray()) {
        hardwareEncoders.append(encoder.toString());
    }
    // GPU path of the transitions (offscreen OpenGL), when the editor uses it; the CPU otherwise or if it fails.
    if (job.value(u"gpuEffects"_s).toBool()) {
        GpuTransitions::initialize();
    }
    int lastReported = -1;
    const Renderer::Result result = Renderer::render(
        *loaded.project, *sequenceId, *settings,
        [&lastReported](int frame, int total) {
            if (frame != lastReported) {
                lastReported = frame;
                emitEvent({{u"event"_s, u"progress"_s}, {u"frame"_s, frame}, {u"total"_s, total}});
            }
        },
        s_cancel, hardwareEncoders);
    for (const QString &warning : result.warnings) {
        emitEvent({{u"event"_s, u"warning"_s}, {u"message"_s, warning}});
    }
    if (const GpuTransitions *gpu = GpuTransitions::instance(); gpu && !gpu->failure().isEmpty()) {
        emitEvent({{u"event"_s, u"warning"_s}, {u"message"_s, QString(u"GPU transitions: "_s + gpu->failure())}});
    }
    GpuTransitions::shutdown();
    switch (result.status) {
    case Renderer::Status::Done:
        emitEvent({{u"event"_s, u"done"_s}, {u"output"_s, settings->outputPath}});
        return 0;
    case Renderer::Status::Cancelled:
        emitEvent({{u"event"_s, u"cancelled"_s}});
        return 2;
    case Renderer::Status::Failed:
        break;
    }
    return fail(result.error, result.detail);
}

int reverse(const QString &input, const QString &output)
{
    int lastReported = -1;
    const Renderer::Result result = Renderer::renderReversed(
        input, output,
        [&lastReported](int frame, int total) {
            if (frame != lastReported) {
                lastReported = frame;
                emitEvent({{u"event"_s, u"progress"_s}, {u"frame"_s, frame}, {u"total"_s, total}});
            }
        },
        s_cancel);
    switch (result.status) {
    case Renderer::Status::Done:
        emitEvent({{u"event"_s, u"done"_s}, {u"output"_s, output}});
        return 0;
    case Renderer::Status::Cancelled:
        emitEvent({{u"event"_s, u"cancelled"_s}});
        return 2;
    case Renderer::Status::Failed:
        break;
    }
    return fail(result.error, result.detail);
}

int probe(const QStringList &paths)
{
    for (const QString &path : paths) {
        emitEvent({{u"event"_s, u"probing"_s}, {u"path"_s, path}});
        const ProbeResult result = probeMedia(path);
        if (result.media) {
            emitEvent({{u"event"_s, u"media"_s}, {u"path"_s, path}, {u"media"_s, projectjson::mediaToJson(*result.media)}});
        } else {
            emitEvent({{u"event"_s, u"media-error"_s},
                       {u"path"_s, path},
                       {u"code"_s, probeErrorCode(result.error)},
                       {u"detail"_s, result.detail}});
        }
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    std::signal(SIGTERM, onTerminate);
    std::signal(SIGINT, onTerminate);
    // Texts are drawn with QPainter, which needs a QGuiApplication: on the "offscreen" platform (no display).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"vedit-render"_s);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Exports a vedit project (used by vedit)."_s);
    parser.addHelpOption();
    const QCommandLineOption jobOption(u"job"_s, u"Export job (JSON)."_s, u"file"_s);
    const QCommandLineOption probeOption(u"probe"_s, u"Read the metadata of the media files given as arguments."_s);
    // Not "--reverse": QGuiApplication takes "-reverse" (right-to-left layout) out of the arguments.
    const QCommandLineOption reverseOption(u"backwards"_s, u"Write the given media file played backwards (preview proxy)."_s,
                                           u"file"_s);
    const QCommandLineOption outputOption(u"output"_s, u"Output file (with --reverse)."_s, u"file"_s);
    parser.addOption(jobOption);
    parser.addOption(probeOption);
    parser.addOption(reverseOption);
    parser.addOption(outputOption);
    parser.addPositionalArgument(u"files"_s, u"Media files (with --probe)."_s);
    parser.process(app);
    if (parser.isSet(probeOption)) {
        return probe(parser.positionalArguments());
    }
    if (parser.isSet(reverseOption)) {
        MltRuntime::initializeAsync();
        return reverse(parser.value(reverseOption), parser.value(outputOption));
    }
    if (!parser.isSet(jobOption)) {
        parser.showHelp(1);
    }
    MltRuntime::initializeAsync();
    // Every MLT object is released by run(). The factory is deliberately not closed: this process ends here, and
    // Mlt::Factory::close() unloads the modules (and FFmpeg, x264) while their global caches are still allocated,
    // which LeakSanitizer then reports as leaks from "<unknown module>" (verified: none without the unload).
    return run(parser.value(jobOption));
}
