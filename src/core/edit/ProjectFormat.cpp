// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectFormat.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace velacut {

namespace {

int even(double value)
{
    return std::max(2, static_cast<int>(std::lround(value / 2.0)) * 2);
}

CanvasPreset presetFor(int width, int height)
{
    const double ratio = static_cast<double>(width) / height;
    const auto near = [ratio](double target) { return std::abs(ratio - target) / target < 0.01; };
    if (near(16.0 / 9.0)) {
        return CanvasPreset::Landscape16x9;
    }
    if (near(9.0 / 16.0)) {
        return CanvasPreset::Portrait9x16;
    }
    if (near(1.0)) {
        return CanvasPreset::Square1x1;
    }
    if (near(4.0 / 5.0)) {
        return CanvasPreset::Portrait4x5;
    }
    if (near(3.0 / 4.0)) {
        return CanvasPreset::Portrait3x4;
    }
    if (ratio > 2.3 && ratio < 2.42) {
        return CanvasPreset::Cinema21x9;
    }
    return CanvasPreset::Custom;
}

bool isEmpty(const Sequence &sequence)
{
    for (const auto *tracks : {&sequence.visualTracks, &sequence.audioTracks}) {
        for (const Track &track : *tracks) {
            if (!track.clips.empty() || !track.transitions.empty()) {
                return false;
            }
        }
    }
    return sequence.markers.empty();
}

} // namespace

std::optional<Canvas> canvasForMedia(const Media &media)
{
    if (media.kind == MediaKind::Audio || !media.info.video || media.info.video->width <= 0 ||
        media.info.video->height <= 0) {
        return std::nullopt;
    }
    const VideoStreamInfo &video = *media.info.video;
    double width = video.width * video.sampleAspectRatio.toDouble();
    double height = video.height;
    if (video.rotation == 90 || video.rotation == 270) {
        std::swap(width, height);
    }
    const double scale = std::min({1.0, 3840.0 / std::max(width, height), 2160.0 / std::min(width, height)});
    Canvas canvas;
    canvas.width = even(width * scale);
    canvas.height = even(height * scale);
    canvas.preset = presetFor(canvas.width, canvas.height);
    return canvas;
}

std::optional<Rational> frameRateForMedia(const Media &media)
{
    if (media.kind != MediaKind::Video && media.kind != MediaKind::ImageSequence) {
        return std::nullopt;
    }
    if (!media.info.video || !media.info.video->frameRate || media.info.video->frameRate->toDouble() <= 0.0) {
        return std::nullopt;
    }
    static const std::array kStandard{Rational(24000, 1001), Rational(24), Rational(25), Rational(30000, 1001),
                                      Rational(30), Rational(48), Rational(50), Rational(60000, 1001), Rational(60)};
    double fps = media.info.video->frameRate->toDouble();
    while (fps > 61.0) {
        fps /= 2.0; // high-speed recordings: 120 → 60, 240 → 60
    }
    if (fps < 23.0) {
        return Rational(30); // screen recordings, timelapses: a smooth project rate
    }
    return *std::min_element(kStandard.begin(), kStandard.end(), [fps](const Rational &a, const Rational &b) {
        return std::abs(a.toDouble() - fps) < std::abs(b.toDouble() - fps);
    });
}

EditResult insertMediaAdoptingFormat(const ProjectData &project, const SequenceId &sequenceId, const MediaId &mediaId,
                                     const RationalTime &position, Placement placement, std::optional<TimeRange> sourceRange)
{
    const Sequence *sequence = project.findSequence(sequenceId);
    const Media *media = project.findMedia(mediaId);
    if (!sequence || !media || !isEmpty(*sequence)) {
        return TimelineEditor(project, sequenceId).insertMedia(mediaId, position, sourceRange, placement);
    }
    const std::optional<Canvas> canvas = canvasForMedia(*media);
    const std::optional<Rational> rate = frameRateForMedia(*media);
    ProjectData adopted = project;
    EditScript script;
    if (rate && *rate != project.settings.frameRate) {
        ProjectSettings settings = project.settings;
        settings.frameRate = *rate;
        script.push_back(edits::setSettings(project.settings, settings));
        adopted.settings = settings;
    }
    if (canvas && !(*canvas == sequence->canvas)) {
        script.push_back(edits::setCanvas(sequenceId, sequence->canvas, *canvas));
        adopted.sequences[static_cast<size_t>(adopted.sequenceIndex(sequenceId))].canvas = *canvas;
    }
    EditResult insert = TimelineEditor(adopted, sequenceId)
                            .insertMedia(mediaId, RationalTime(0, adopted.settings.frameRate), sourceRange, placement);
    if (!insert.ok() || script.empty()) {
        return insert;
    }
    for (auto &edit : insert.script) {
        script.push_back(std::move(edit));
    }
    insert.script = std::move(script);
    return insert;
}

} // namespace velacut
